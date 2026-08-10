#include <TimeLib.h>
#include <RS41.h>
#include <Watchdog_t4.h>

#include "ProfilerHardware.h"
#include <EEPROM.h>
#include "RPUConfig.h"
#include "RPUComm.h"
#include "RPUUtil.h"
#include "rpu_version.h"
#include "RPUTypes.h"
#include "RPUAnalog.h"
#include "RPUBattery.h"
#include "RPUStatus.h"
#include "RPUConsole.h"
#include "RPUOPC.h"
#include "RPUTDLAS.h"
#include "RPUTSEN.h"
#include "RPURecordBuffer.h"

// ---------------------------------------------------------------------------
// Configurable parameters (settable via command)
// ---------------------------------------------------------------------------
static float    bat_v_crit      = CFG_V_CRIT_BATT;
static float    bat_v_low       = CFG_V_LOW_BATT;
static float    bat_t_setpoint  = CFG_BATTERY_T_SETPOINT;

// ---------------------------------------------------------------------------
// MEASURE parameters (set by RPU_GO_MEASURE command)
// ---------------------------------------------------------------------------
static int32_t MeasureDurationSecs = CFG_MEASURE_DURATION;
static int32_t SaveRateSecs     = CFG_REPORT_RATE;

// ---------------------------------------------------------------------------
// Interfaces
// ---------------------------------------------------------------------------
static TinyGPSPlus profiler_gps;
static TSensor1Bus TempBattery(ONE_WIRE_1);
static TSensor1Bus TempPump(ONE_WIRE_2);
static TSensor1Bus TempPCB(ONE_WIRE_3);
static RS41 rs41(RS41_SERIAL, RS41_ENABLE);
static RPUComm rpucomm(&Serial1);

// ---------------------------------------------------------------------------
// Other global variables
// ---------------------------------------------------------------------------
WDT_T4<WDT1> wdt;
static uint16_t rpu_id     = 0;
static float vin           = 0.0f;
static float bat_v         = 0.0f;
static float charge_i      = 0.0f;
static float v_5V          = 0.0f;
static float pump_i        = 0.0f;
static float opc_i         = 0.0f;
static float tsen_i        = 0.0f;
static float tdlas_i       = 0.0f;
static float heater_i      = 0.0f;
static ROPCData opcData;
static TDLASData tdlasData;
static String   tsenData;
static TSENData tsenRaw;
RPURecord rpu_record;

// ---------------------------------------------------------------------------
// GPS / TDLAS / Dock serial buffers
// ---------------------------------------------------------------------------
static constexpr size_t RPU_TM_MAX_RECORDS  = 160;
static constexpr size_t RPU_TM_BUFFER_BYTES = RPU_BLOCK_HDR_BYTES + RPU_TM_MAX_RECORDS * RPU_RECORD_BYTES;
static_assert(RPU_TM_BUFFER_BYTES < 8092, "RPU_TM_BUFFER_BYTES must leave 100 bytes of headroom below 8192");

static uint8_t GPS_Serial_Buffer[4096];
static uint8_t TDLAS_Serial_Buffer[1028];

// Extra TX ring-buffer space for DOCK_SERIAL, sized to hold the largest
// single TX_Bin() write (a full RPU_TM_MAX_RECORDS-record TM block, ~7616
// bytes including framing) so that sendRPURecords() doesn't block the main
// loop while bytes drain out at 115200 baud.
static uint8_t Dock_Serial_TX_Buffer[RPU_TM_BUFFER_BYTES];


// ---------------------------------------------------------------------------
// Battery and heater state
// ---------------------------------------------------------------------------
static float    bat_t              = 0.0f;
static float    pcb_t              = 0.0f;
static float    pump_t           = 0.0f;
static uint32_t heater_on_ticks    = 0;
static uint32_t heater_total_ticks = 0;

// ---------------------------------------------------------------------------
// Pump
// ---------------------------------------------------------------------------
static PumpState pump;

// ---------------------------------------------------------------------------
// Sensor enable state
// ---------------------------------------------------------------------------
static SensorsEnabled_t sensorsEnabled;

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
static RPUState rpu_state = RPUState::STANDBY;

// ---------------------------------------------------------------------------
// MEASURE-session reference state (captured/reset once per GO_MEASURE)
// ---------------------------------------------------------------------------
static uint32_t MeasureStartMillis = 0;
static double   GPSStartLat        = 0.0;
static double   GPSStartLon        = 0.0;
static bool     GPSStartCaptured   = false;
static elapsedMillis measure_timer;
static elapsedMillis save_timer;

void enterStandby(RPUState& state)
{
  sensorsEnabled = SensorsEnabled_t{};
  pump.enabled = false;
  powerdownSensors();
  state = RPUState::STANDBY;
  Serial.println("Entering STANDBY");
}

void enterMeasure(RPUState& state)
{
  if (sensorsEnabled.opc)   { digitalWrite(OPC_ENABLE,   HIGH); pump.enabled = true; }
  if (sensorsEnabled.tdlas) { digitalWrite(TDLAS_ENABLE, HIGH); }
  if (sensorsEnabled.tsen)  { digitalWrite(TSEN_ENABLE,  HIGH); }
  if (sensorsEnabled.rs41)  { digitalWrite(RS41_ENABLE,  HIGH); }
  MeasureStartMillis = millis();
  GPSStartCaptured   = false;
  measure_timer      = 0;
  save_timer         = 0;
  rpu_record.resetRotation();
  state = RPUState::MEASURE;
  Serial.println("Entering MEASURE");
}

void enterError(RPUState& state)
{
  state = RPUState::ERROR;
  Serial.println("ERROR: entering ERROR state");
}

// ---------------------------------------------------------------------------
// Communications
// ---------------------------------------------------------------------------
// Records popped from rpu_records but not yet ACKed by the dock. Held here so
// a NAK (or lost ACK) can be retransmitted from the same buffer instead of
// popping (and thereby losing) the next batch from the FIFO.
static uint8_t tm_buf[RPU_TM_BUFFER_BYTES];
static size_t  tm_pending_records = 0;

size_t getPendingTMRecordCount()
{
  return tm_pending_records;
}

static void sendRPURecords()
{
  if (tm_pending_records == 0) {
    while (tm_pending_records < RPU_TM_MAX_RECORDS &&
           rpu_records.pop(&tm_buf[RPU_BLOCK_HDR_BYTES + tm_pending_records * RPU_RECORD_BYTES], RPU_RECORD_BYTES)) {
      tm_pending_records++;
    }
  }

  DEBUG_SERIAL.printf("sendRPURecords: sending %u record(s)\n", (unsigned)tm_pending_records);

  if (tm_pending_records > 0) {
    rpu_record.encodeBlockHeader(tm_buf, RPU_BLOCK_HDR_BYTES);
    rpucomm.AssignBinaryTXBuffer(tm_buf, sizeof(tm_buf), RPU_BLOCK_HDR_BYTES + tm_pending_records * RPU_RECORD_BYTES);
    rpucomm.TX_Bin(RPU_PROFILE_RECORD);
  } else {
    rpucomm.TX_ASCII(RPU_NO_MORE_RECORDS);
  }
}

static bool dockComms()
{
  int8_t   tmp1;

  switch (rpucomm.RX()) {
    case ASCII_MESSAGE:
      switch (rpucomm.ascii_rx.msg_id) {
        case RPU_SEND_STATUS: {
          DEBUG_SERIAL.println("Received RPU_SEND_STATUS");
          String json = getStatusJSON(rpu_id, RPU_VERSION, rpu_state,
              vin, v_5V, bat_v, bat_t, charge_i, pcb_t,
              pump_i, opc_i, tsen_i, tdlas_i, heater_i,
              heater_on_ticks, heater_total_ticks,
              profiler_gps);
          rpucomm.TX_Status(json.c_str());
          return false;
        }

        case RPU_SEND_RECORDS:
          DEBUG_SERIAL.println("Received RPU_SEND_RECORDS");
          sendRPURecords();
          return false;

        case RPU_RESET:
          rpucomm.TX_Ack(RPU_RESET, true);
          DEBUG_SERIAL.println("Received RPU_RESET - rebooting via WDT");
          delay((CFG_WDT_TIMEOUT_S + 2) * 1000UL);
          return false;

        case RPU_GO_MEASURE: {
          int8_t OPC_Power = 0, TDLAS_Power = 0, TSEN_Power = 0, RS41_Power = 0;
          float  BatTSetpoint = bat_t_setpoint;
          int32_t new_duration = MeasureDurationSecs;
          int32_t new_rate     = SaveRateSecs;
          tmp1 = rpucomm.RX_GoMeasure(&new_duration, &new_rate, &BatTSetpoint,
                                      &OPC_Power, &TDLAS_Power, &TSEN_Power, &RS41_Power);
          if (tmp1 && new_duration != 0 && new_duration <= new_rate) {
            DEBUG_SERIAL.printf("RPU_GO_MEASURE: duration (%lds) must be 0 or greater than rate (%lds); setting duration to %lds\n",
                                 (long)new_duration, (long)new_rate, (long)(new_rate + 1));
            new_duration = new_rate + 1;
          }
          rpucomm.TX_Ack(RPU_GO_MEASURE, tmp1);
          if (tmp1) {
            DEBUG_SERIAL.println("Received RPU_GO_MEASURE");
            MeasureDurationSecs  = new_duration;
            SaveRateSecs         = new_rate;
            DEBUG_SERIAL.printf("MEASURE duration=%lds rate=%lds\n",
                                 (long)MeasureDurationSecs, (long)SaveRateSecs);
            bat_t_setpoint       = BatTSetpoint;
            sensorsEnabled.opc   = OPC_Power;
            sensorsEnabled.tdlas = TDLAS_Power;
            sensorsEnabled.tsen  = TSEN_Power;
            sensorsEnabled.rs41  = RS41_Power;
            enterMeasure(rpu_state);
            return true;
          }
          return false;
        }

        case RPU_GO_STANDBY: {
          float BatTSetpoint = bat_t_setpoint;
          tmp1 = rpucomm.RX_GoStandby(&BatTSetpoint);
          rpucomm.TX_Ack(RPU_GO_STANDBY, tmp1);
          if (tmp1) {
            DEBUG_SERIAL.println("Received RPU_GO_STANDBY");
            bat_t_setpoint = BatTSetpoint;
            enterStandby(rpu_state);
            return true;
          }
          return false;
        }

        case RPU_SET_BATT_T:
          tmp1 = rpucomm.Get_float(&bat_t_setpoint);
          rpucomm.TX_Ack(RPU_SET_BATT_T, tmp1);
          DEBUG_SERIAL.printf("BATT_T_SET=%.2f\n", bat_t_setpoint);
          return false;

        case RPU_SET_V_LOW_BATT:
          tmp1 = rpucomm.Get_float(&bat_v_low);
          rpucomm.TX_Ack(RPU_SET_V_LOW_BATT, tmp1);
          DEBUG_SERIAL.printf("bat_v_low=%.2f\n", bat_v_low);
          return false;

        case RPU_SET_V_CRIT_BATT:
          tmp1 = rpucomm.Get_float(&bat_v_crit);
          rpucomm.TX_Ack(RPU_SET_V_CRIT_BATT, tmp1);
          DEBUG_SERIAL.printf("bat_v_crit=%.2f\n", bat_v_crit);
          return false;

        case RPU_SET_STATUS_RATE: {
          uint32_t rate = getRPUStatusInterval();
          tmp1 = rpucomm.Get_uint32(&rate);
          if (tmp1) { 
            setRPUStatusInterval(rate); 
          }
          rpucomm.TX_Ack(RPU_SET_STATUS_RATE, tmp1);
          DEBUG_SERIAL.printf("STATUS_RATE=%lu\n", getRPUStatusInterval());
          return false;
        }

        default:
          rpucomm.TX_Ack(rpucomm.ascii_rx.msg_id, false);
          return false;
      }

    case ACK_MESSAGE:
      DEBUG_SERIAL.print("ACK/NAK for msg: ");
      DEBUG_SERIAL.print(rpucomm.ack_id);
      rpucomm.ack_value ? DEBUG_SERIAL.println(" ACK") : DEBUG_SERIAL.println("NAK");
      if (rpucomm.ack_id == RPU_PROFILE_RECORD && rpucomm.ack_value) {
        tm_pending_records = 0;
      }
      return false;

    case NO_MESSAGE:
    default:
      return false;
  }
}

static void tickStandby()
{
  updateTemperatures(TempBattery, TempPCB, TempPump, bat_t, pcb_t, pump_t);
  manageHeater(bat_t, bat_t_setpoint,
               vin, bat_v, bat_v_crit,
               heater_on_ticks, heater_total_ticks);
}

static void buildAndSaveRPURecord(const RS41::RS41SensorData_t& sensor_data, bool rs41_ok, bool heater_on)
{

  // Fast fields (period = 1)
  rpu_record.setElapsedS((millis() - MeasureStartMillis) / 1000);
  rpu_record.setAlt(profiler_gps.altitude.meters());
  rpu_record.setLatDelta(GPSStartCaptured ? (profiler_gps.location.lat() - GPSStartLat) : 0.0);
  rpu_record.setLonDelta(GPSStartCaptured ? (profiler_gps.location.lng() - GPSStartLon) : 0.0);
  rpu_record.setSats((uint8_t)profiler_gps.satellites.value());
  rpu_record.setGpsAge(profiler_gps.location.age() / 1000);

  rpu_record.setOpcD300(opcData.d300);
  rpu_record.setOpcD2000(opcData.d2000);

  rpu_record.setTsenAirt(tsenRaw.airt_raw);
  rpu_record.setTsenPres(tsenRaw.pres_raw);
  rpu_record.setTsenPtemp(tsenRaw.ptemp_raw);

  rpu_record.setRs41AirT(    rs41_ok ? sensor_data.air_temp_degC     : 0.0f);
  rpu_record.setRs41Pres(    rs41_ok ? sensor_data.pres_mb           : 0.0f);
  rpu_record.setRs41Humidity(rs41_ok ? sensor_data.humdity_percent   : 0.0f);
  rpu_record.setRs41HSensorT(rs41_ok ? sensor_data.hsensor_temp_degC : 0.0f);

  rpu_record.setTdlasMrAvg(tdlasData.mr_avg);
  rpu_record.setTdlasBkg(tdlasData.bkg);
  rpu_record.setTdlasPeak(tdlasData.peak);
  rpu_record.setTdlasRatio(tdlasData.ratio);
  rpu_record.setTdlasMaxVmr(tdlasData.max_vmr);
  rpu_record.setTdlasLaserT(tdlasData.laser_t);
  rpu_record.setTdlasSpec1(tdlasData.spec_1);
  rpu_record.setTdlasSpec2(tdlasData.spec_2);
  rpu_record.setTdlasSpec3(tdlasData.spec_3);
  rpu_record.setTdlasSpec4(tdlasData.spec_4);


  // Slow / round-robin fields (period = 8)
  rpu_record.setOpcD500(opcData.d500);
  rpu_record.setOpcD700(opcData.d700);
  rpu_record.setOpcD1000(opcData.d1000);
  rpu_record.setOpcD3000(opcData.d3000);
  rpu_record.setOpcD5000(opcData.d5000);
  rpu_record.setOpcD2500(opcData.d2500);

  rpu_record.setRs41Hdg(rs41_ok ? sensor_data.heading_deg : 0);
  if (rs41_ok) {
    const RS41::RS41StatusFlags_t& f = sensor_data.flags;
    uint8_t status = (f.high_internal_temp  ? RPU_REC_RS41_HIGH_INTERNAL_TEMP : 0u)
                   | (f.regen_temp_low      ? RPU_REC_RS41_REGEN_TEMP_LOW     : 0u)
                   | (f.ptu_failure         ? RPU_REC_RS41_PTU_FAILURE        : 0u)
                   | (f.flash_failure       ? RPU_REC_RS41_FLASH_FAILURE      : 0u)
                   | (f.low_input_voltage   ? RPU_REC_RS41_LOW_INPUT_VOLTAGE  : 0u)
                   | (f.not_calibrated      ? RPU_REC_RS41_NOT_CALIBRATED     : 0u)
                   | (f.no_pressure_module  ? RPU_REC_RS41_NO_PRESSURE_MODULE : 0u)
                   | (f.disconnected_boom   ? RPU_REC_RS41_DISCONNECTED_BOOM  : 0u);
    rpu_record.setRs41Status(status);
  } else {
    rpu_record.setRs41Status(0);
  }
  rpu_record.setBemfV(pump.bemf_v);

  rpu_record.setTsenI(tsen_i);
  rpu_record.setOpcI(opc_i);
  rpu_record.setPumpI(pump_i);
  rpu_record.setTdlasI(tdlas_i);
  rpu_record.setV5V(v_5V);

  rpu_record.setBatT(bat_t);
  rpu_record.setPumpT(pump_t);
  rpu_record.setPcbT(pcb_t);
  rpu_record.setBatV(bat_v);
  rpu_record.setHeaterStat(heater_on ? 0x1 : 0x0);

  if (!rpu_records.push(rpu_record)) {
    Serial.println("WARNING: rpu_records buffer full — record dropped");
  }

  rpu_record.advanceRotation();

  if (getDebugPrintEnabled()) {
    uint8_t record_buf[RPU_RECORD_BYTES];
    rpu_record.encode(record_buf, sizeof(record_buf));

    RPURecord decoded_record;
    decoded_record.decode(record_buf, sizeof(record_buf));
    Serial.println(decoded_record.toJSON());
  }
}

static void tickMeasure()
{
  if (measure_timer < 1000) {
    return;
  }
  measure_timer = (uint32_t)measure_timer % 1000;

  // Saved-record cadence is independent of the 1 Hz sampling above.
  uint32_t save_interval_ms = (uint32_t)(SaveRateSecs > 0 ? SaveRateSecs : 1) * 1000;

  // --- Temperatures ------------------------------------------------------------
  updateTemperatures(TempBattery, TempPCB, TempPump, bat_t, pcb_t, pump_t);

  // --- RS41 Radiosonde -------------------------------------------------------
  RS41::RS41SensorData_t sensor_data = rs41.decoded_sensor_data(false);
  bool rs41_ok = sensor_data.valid;
  if (getDebugPrintEnabled()) {
    Serial.printf("RS41: air_t=%.2fC pres=%.1fmb rh=%.2f%% hsensor_t=%.2fC hdg=%.2fdeg\n",
      sensor_data.air_temp_degC, sensor_data.pres_mb, sensor_data.humdity_percent,
      sensor_data.hsensor_temp_degC, sensor_data.heading_deg);
    const RS41::RS41StatusFlags_t& f = sensor_data.flags;
    Serial.printf("RS41 flags: hi_t=%d regen_lo=%d ptu=%d flash=%d lo_v=%d uncal=%d no_p=%d boom=%d\n",
      f.high_internal_temp, f.regen_temp_low, f.ptu_failure, f.flash_failure,
      f.low_input_voltage, f.not_calibrated, f.no_pressure_module, f.disconnected_boom);
  }

  // --- OPC -------------------------------------------------------------------
  bool gotOPC = readOPC(opcData);

  // --- TSEN -------------------------------------------------------------------
  bool gotTSEN = readTSEN(tsenData, tsenRaw);

  // --- TDLAS -----------------------------------------------------------------

  bool gotTDLAS = readTDLAS(tdlasData);

  // --- GPS start reference (captured once per measurement session) -----------
  if (!GPSStartCaptured && profiler_gps.location.isValid()) {
    GPSStartLat      = profiler_gps.location.lat();
    GPSStartLon      = profiler_gps.location.lng();
    GPSStartCaptured = true;

    rpu_record.setGpsLat(GPSStartLat);
    rpu_record.setGpsLon(GPSStartLon);

    struct tm t = {};
    t.tm_year = profiler_gps.date.year() - 1900;
    t.tm_mon  = profiler_gps.date.month() - 1;
    t.tm_mday = profiler_gps.date.day();
    t.tm_hour = profiler_gps.time.hour();
    t.tm_min  = profiler_gps.time.minute();
    t.tm_sec  = profiler_gps.time.second();
    rpu_record.setEpochTime((uint32_t)mktime(&t));
  }

  // --- Control loops ---------------------------------------------------------
  adjustPump(pump, bat_v);
  bool heater_on = adjustHeaters(bat_t, bat_t_setpoint);

  // --- Build and save RPURecord (gated by SaveRateSecs) -----------------------
  if (save_timer >= save_interval_ms) {
    save_timer = (uint32_t)save_timer % save_interval_ms;
    buildAndSaveRPURecord(sensor_data, rs41_ok, heater_on);
  }

  // --- End MEASURE once the configured duration has elapsed (0 = unlimited) ----
  if (MeasureDurationSecs != 0 &&
      (millis() - MeasureStartMillis) >= (uint32_t)MeasureDurationSecs * 1000UL) {
    Serial.println("MEASURE duration reached");
    enterStandby(rpu_state);
  }
}

static void tickError()
{
}

// =============================================================================
// setup
// =============================================================================
void setup()
{
  pinMode(PULSE_LED,       OUTPUT);
  pinMode(CHARGER_SHTDWN,  INPUT);   // Open-drain; do not drive unless needed
  pinMode(OPC_ENABLE,      OUTPUT);
  pinMode(TDLAS_ENABLE,    OUTPUT);
  pinMode(TSEN_ENABLE,     OUTPUT);
  pinMode(BATTERY_HEATER,  OUTPUT);
  pinMode(CHARGE_IMON,     INPUT);
  pinMode(RS232_FORCEOFF,  OUTPUT);
  pinMode(RS232_FORCEON,   OUTPUT);
  pinMode(DSEL_0,          OUTPUT);
  pinMode(DSEL_1,          OUTPUT);
  pinMode(SWITCH_IMON,     INPUT);
  pinMode(PUMP_PWM,        OUTPUT);

  digitalWrite(OPC_ENABLE,     HIGH);
  digitalWrite(TDLAS_ENABLE,   HIGH);
  digitalWrite(BATTERY_HEATER, LOW);
  digitalWrite(TSEN_ENABLE,    HIGH);
  digitalWrite(RS232_FORCEOFF, HIGH);
  digitalWrite(RS232_FORCEON,  HIGH);
  digitalWrite(ONE_WIRE_1,     HIGH);
  digitalWrite(PUMP_PWM,       LOW);

  Serial.begin(115200);

  // Trigger STARTUP conversion on all temperature sensorsEnabled (750 ms each).
  // Three sensorsEnabled total ~2.25 s, providing a convenient delay for the
  // developer to connect a serial monitor before the banner is printed.
  TempPCB.ManageState(pcb_t);
  TempPump.ManageState(pump_t);
  TempBattery.ManageState(bat_t);

  Serial.println("RACHuTS Profiling Unit " + getRPUIdentifier(RPU_VERSION));
  Serial.printf("CPU frequency: %lu Hz\n", F_CPU_ACTUAL);

  // Check for watchdog reset before initializing the WDT, as wdt.begin()
  // clears the reset-cause register.
  {
    bool wdt_fired = wdt.expired();
    uint32_t wdt_count = 0;
    EEPROM.get(CFG_EEPROM_WDT_COUNT_ADDR, wdt_count);
    if (wdt_count == 0xFFFFFFFF) { 
      // uninitialized EEPROM
      wdt_count = 0; 
    } 
    if (wdt_fired) {
      wdt_count++;
      EEPROM.put(CFG_EEPROM_WDT_COUNT_ADDR, wdt_count);
      Serial.println("WARNING: previous run was reset by the watchdog — the loop"
                     " stalled for > 10 s without calling wdt.feed().");
    }
    setWDTCount(wdt_count);
    Serial.printf("WDT reset count: %lu\n", wdt_count);
  }

  WDT_timings_t wdt_config;
  wdt_config.timeout = CFG_WDT_TIMEOUT_S;
  wdt.begin(wdt_config);

  GPS_SERIAL.begin(9600);
  GPS_SERIAL.addMemoryForRead(GPS_Serial_Buffer, sizeof(GPS_Serial_Buffer));

  OPC_SERIAL.begin(9600, SERIAL_8N1_RXINV_TXINV);

  TSEN_SERIAL.begin(9600);

  TDLAS_SERIAL.begin(115200);
  TDLAS_SERIAL.addMemoryForRead(TDLAS_Serial_Buffer, sizeof(TDLAS_Serial_Buffer));
  
  DOCK_SERIAL.begin(115200);
  DOCK_SERIAL.addMemoryForWrite(Dock_Serial_TX_Buffer, sizeof(Dock_Serial_TX_Buffer));

  analogReadResolution(12);
  analogReadAveraging(32);

  {
    uint8_t mac[6];
    readTeensyMAC(mac);
    rpu_id = ((uint16_t)mac[4] << 8) | mac[5];
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("LoRa ID: %04X\n", rpu_id);
  }

  rs41.init();

  if (!LoRa.begin(868E6)) {
    Serial.println("Starting LoRa failed!");
  } else {
    Serial.println("LoRa initialization complete");
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);

  delay(1000);

  TempPCB.PrintSensorAddress();
  if (!TempPCB.ValidateAddrCRC()) {
    Serial.println("PCB Temp sensor CRC bad — check sensor connection");
  }

  TempPump.PrintSensorAddress();
  if (!TempPump.ValidateAddrCRC()) {
    Serial.println("Pump Temp sensor CRC bad — check sensor connection");
  }

  TempBattery.PrintSensorAddress();
  if (!TempBattery.ValidateAddrCRC()) {
    Serial.println("Battery Temp sensor CRC bad — check sensor connection");
  }

  Serial.println(rs41.banner());
  Serial.println("RS41 meta data: " + rs41.meta_data());
  Serial.println(rs41.sensor_data_var_names);

  analogWrite(PUMP_PWM, 0); // Reset FlexPWM to zero duty cycle — ensures pump is off
  
  enterStandby(rpu_state);
}

// =============================================================================
// loop
// =============================================================================
void loop()
{
  wdt.feed();

  //----------------------------------------------------
  // Activities that run in all states
  //----------------------------------------------------


  // Analog voltage / current monitors
  bat_v        = readVBat();
  vin         = readVin();
  charge_i = readChargeI();
  v_5V     = readVmon5V();
  pump_i      = readIPump();
  readSwitchCurrents(opc_i, tsen_i, tdlas_i, heater_i);

  // GPS values (updated asynchronously as GPS data is received)
  while (GPS_SERIAL.available() > 0) {
    profiler_gps.encode(GPS_SERIAL.read());
  }

  // Check for incoming commands from RATCHuTS over the docking connector
  dockComms();

  consoleReport(rpu_state, millis() / 1000.0f,
                bat_v, vin, digitalRead(BATTERY_HEATER));

  sendStatus(rpu_id, RPU_VERSION, rpu_state,
            vin, v_5V, bat_v, bat_t, charge_i, pcb_t,
            pump_i, opc_i, tsen_i, tdlas_i, heater_i,
            heater_on_ticks, heater_total_ticks,
            profiler_gps);

  
  // The state can be changed from the console
  consoleRead(rpu_state, sensorsEnabled, pump.enabled, MeasureDurationSecs, SaveRateSecs);


  //----------------------------------------------------
  // State machine tick
  //----------------------------------------------------
  switch (rpu_state)
  {
    case RPUState::STANDBY:
      tickStandby();
      break;
    case RPUState::MEASURE:
      tickMeasure();
      break;
    case RPUState::ERROR:
      tickError();
      break;
    default:
      Serial.println("ERROR: unknown RPU state — entering ERROR");
      enterError(rpu_state);
      break;
  }
}
