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

// ---------------------------------------------------------------------------
// Configurable parameters (settable via command)
// ---------------------------------------------------------------------------
static float    bat_v_crit      = CFG_V_CRIT_BATT;
static float    bat_v_low       = CFG_V_LOW_BATT;
static float    bat_t_setpoint  = CFG_BATTERY_T_SETPOINT;

// ---------------------------------------------------------------------------
// MEASURE parameters (set by RPU_GO_MEASURE command)
// ---------------------------------------------------------------------------
static int32_t MeasureDuration = CFG_MEASURE_DURATION;
static int32_t MeasureRate     = CFG_MEASURE_RATE;

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

// ---------------------------------------------------------------------------
// GPS / TDLAS serial buffers
// ---------------------------------------------------------------------------
static uint8_t GPS_Serial_Buffer[4096];
static uint8_t TDLAS_Serial_Buffer[1028];


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

void enterStandby(RPUState& state)
{
  sensorsEnabled = SensorsEnabled_t{};
  powerdownSensors();
  state = RPUState::STANDBY;
  Serial.println("Entering STANDBY");
}

void enterMeasure(RPUState& state)
{
  if (sensorsEnabled.opc)   { digitalWrite(OPC_ENABLE,   HIGH); }
  if (sensorsEnabled.tdlas) { digitalWrite(TDLAS_ENABLE, HIGH); }
  if (sensorsEnabled.tsen)  { digitalWrite(TSEN_ENABLE,  HIGH); }
  if (sensorsEnabled.rs41)  { digitalWrite(RS41_ENABLE,  HIGH); }
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
static void sendTM()
{
}

static bool dockComms()
{
  int8_t   tmp1;
  uint32_t tmp3;

  switch (rpucomm.RX()) {
    case ASCII_MESSAGE:
      switch (rpucomm.ascii_rx.msg_id) {
        case RPU_SEND_STATUS:
          tmp3 = now();
          tmp1 = rpucomm.TX_Status(tmp3, readVBat(), readChargeI(),
                                   bat_t, pcb_t,
                                   digitalRead(BATTERY_HEATER));
          DEBUG_SERIAL.println("Received RPU_SEND_STATUS");
          return false;

        case RPU_SEND_RECORDS:
          DEBUG_SERIAL.println("Received RPU_SEND_RECORDS");
          sendTM();
          return false;

        case RPU_GO_MEASURE: {
          int8_t OPC_Power = 0, TDLAS_Power = 0, TSEN_Power = 0;
          tmp1 = rpucomm.RX_GoMeasure(&MeasureDuration, &MeasureRate,
                                      &OPC_Power, &TDLAS_Power, &TSEN_Power);
          rpucomm.TX_Ack(RPU_GO_MEASURE, tmp1);
          if (tmp1) {
            DEBUG_SERIAL.println("Received RPU_GO_MEASURE");
            sensorsEnabled.opc   = OPC_Power;
            sensorsEnabled.tdlas = TDLAS_Power;
            sensorsEnabled.tsen  = TSEN_Power;
            enterMeasure(rpu_state);
            return true;
          }
          return false;
        }

        case RPU_GO_STANDBY:
          rpucomm.TX_Ack(RPU_GO_STANDBY, true);
          DEBUG_SERIAL.println("Received RPU_GO_STANDBY");
          enterStandby(rpu_state);
          return true;

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
          uint32_t rate = getRPUReportInterval();
          tmp1 = rpucomm.Get_uint32(&rate);
          if (tmp1) { 
            setRPUReportInterval(rate); 
          }
          rpucomm.TX_Ack(RPU_SET_STATUS_RATE, tmp1);
          DEBUG_SERIAL.printf("STATUS_RATE=%lu\n", getRPUReportInterval());
          return false;
        }

        default:
          rpucomm.TX_Ack(rpucomm.ascii_rx.msg_id, false);
          return false;
      }

    case ACK_MESSAGE:
      DEBUG_SERIAL.print("ACK/NAK for msg: ");
      DEBUG_SERIAL.println(rpucomm.ack_id);
      rpucomm.ack_value ? DEBUG_SERIAL.println("ACK") : DEBUG_SERIAL.println("NAK");
      return false;

    case NO_MESSAGE:
    default:
      return false;
  }
}

static void tickStandby()
{
  updateTemperatures(TempBattery, TempPCB, bat_t, pcb_t);
  manageHeater(bat_t, bat_t_setpoint,
               vin, bat_v, bat_v_crit,
               heater_on_ticks, heater_total_ticks);
}

static void tickMeasure()
{
  static elapsedMillis tick_timer;
  if (tick_timer < 1000) { return; }
  tick_timer = 0;

  // --- RS41 Radiosonde -------------------------------------------------------
  RS41::RS41SensorData_t sensor_data = rs41.decoded_sensor_data(false);

  if (sensor_data.valid)
  {
    Serial.printf("RS41: frame=%lu air_temp=%.2fC humidity=%.2f%% hsensor_temp=%.2fC pres=%.2fmb "
                  "int_temp=%.2fC status=%u err=%u pcb_supply=%.3fV lsm303_temp=%.2fC heater=%d "
                  "hdgXY=%ld hdgXZ=%ld hdgYZ=%ld accelX=%ld accelY=%ld accelZ=%ld\n",
      (unsigned long)sensor_data.frame_count,
      sensor_data.air_temp_degC,
      sensor_data.humdity_percent,
      sensor_data.hsensor_temp_degC,
      sensor_data.pres_mb,
      sensor_data.internal_temp_degC,
      sensor_data.module_status,
      sensor_data.module_error,
      sensor_data.pcb_supply_V,
      sensor_data.lsm303_temp_degC,
      sensor_data.pcb_heater_on,
      sensor_data.mag_hdgXY_deg,
      sensor_data.mag_hdgXZ_deg,
      sensor_data.mag_hdgYZ_deg,
      sensor_data.accelX_mG,
      sensor_data.accelY_mG,
      sensor_data.accelZ_mG);
  }

  // --- OPC -------------------------------------------------------------------
  readOPC(opcData);

  // --- TSEN -------------------------------------------------------------------
  TSEN_SERIAL.print("*01A?\r");
  TSEN_SERIAL.flush();
  delay(10);
  Serial.print("TSEN: ");
  while(TSEN_SERIAL.available() >0)
    Serial.write(TSEN_SERIAL.read());
  Serial.println();

  // --- TDLAS -----------------------------------------------------------------
  
  readTDLAS(tdlasData);
  if (0) { 
    // --- Analog voltage / current monitors ------------------------------------
    Serial.print("Battery Voltage: ");    Serial.println(bat_v);
    Serial.print("Input Voltage: ");      Serial.println(vin);
    Serial.print("Charge Current: ");     Serial.println(charge_i);
    Serial.print("5V Supply: ");          Serial.println(v_5V);
    Serial.print("Pump Current (mA): ");  Serial.println(pump_i);
    Serial.print("OPC Current: ");        Serial.println(opc_i);
    Serial.print("TSEN Current: ");       Serial.println(tsen_i);
    Serial.print("TDLAS Current: ");      Serial.println(tdlas_i);
    Serial.print("Battery Heater Current: "); Serial.println(heater_i);


    // --- Temperature sensorsEnabled ---------------------------------------------------
    TempPCB.ManageState(pcb_t);
    Serial.print("PCB Temp: ");     Serial.println(pcb_t);

    TempPump.ManageState(pump_t);
    Serial.print("Pump Temp: ");    Serial.println(pump_t);

    TempBattery.ManageState(bat_t);
    Serial.print("Battery Temp: "); Serial.println(bat_t);
  }

  // --- Control loops ---------------------------------------------------------
  adjustPump(pump, bat_v);
  adjustHeaters(bat_t, bat_t_setpoint);

  // --- Write combined data line to SD ----------------------------------------
  // Single CSV row containing all collected variables.
  // Fields: serial_hex, elapsed_ms,
  //         VBat, vin, charge_i, v_5V, pump_i,
  //         opc_i, tsen_i, tdlas_i, heater_i, pump.bemf_v, pump.pwm,
  //         GPS_lat, GPS_lng, GPS_alt_m, GPS_satellites, GPS_date, GPS_time, GPS_age_s,
  //         pcb_t, pump_t, bat_t,
  //         ROPC_time, d300, d500, d700, d1000, d2000, d2500, d3000, d5000, OPC_alarm,
  //         TDLAS_mr_avg, TDLAS_bkg, TDLAS_peak, TDLAS_ratio, TDLAS_batt, TDLAS_therm_1, TDLAS_therm_2, TDLAS_indx, TDLAS_spec_1..4,
  //         RS41_frame, RS41_air_temp, RS41_humidity, RS41_hsensor_temp, RS41_pres,
  //         RS41_internal_temp, RS41_module_status, RS41_module_error, RS41_pcb_supply_V,
  //         RS41_lsm303_temp, RS41_pcb_heater_on,
  //         RS41_mag_hdgXY, RS41_mag_hdgXZ, RS41_mag_hdgYZ,
  //         RS41_accelX, RS41_accelY, RS41_accelZ
  char DataLine[960];
  snprintf(DataLine, sizeof(DataLine),
    "%04X,%lu,"
    "%.3f,%.3f,%.3f,%.3f,%.3f,"
    "%.3f,%.3f,%.3f,%.3f,%.3f,%d,"
    "%.6f,%.6f,%.2f,%lu,%lu,%lu,%lu,"
    "%.2f,%.2f,%.2f,"
    "%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
    "%.4f,%.4f,%.4f,%.6f,%.3f,%.2f,%.2f,%d,%.4f,%.4f,%.4f,%.4f,"
    "%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u,%.3f,%.2f,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
    rpu_id, millis(),
    bat_v, vin, charge_i, v_5V, pump_i,
    opc_i, tsen_i, tdlas_i, heater_i, pump.bemf_v, pump.pwm,
    profiler_gps.location.lat(), profiler_gps.location.lng(), profiler_gps.altitude.meters(),
    profiler_gps.satellites.value(), profiler_gps.date.value(), profiler_gps.time.value(), profiler_gps.location.age() / 1000,
    pcb_t, pump_t, bat_t,
    opcData.ROPC_time, opcData.d300, opcData.d500, opcData.d700, opcData.d1000,
    opcData.d2000, opcData.d2500, opcData.d3000, opcData.d5000, opcData.alarm,
    tdlasData.mr_avg, tdlasData.bkg, tdlasData.peak, tdlasData.ratio,
    tdlasData.batt, tdlasData.therm_1, tdlasData.therm_2,
    tdlasData.indx, tdlasData.spec_1, tdlasData.spec_2, tdlasData.spec_3, tdlasData.spec_4,
    sensor_data.valid ? (unsigned long)sensor_data.frame_count  : 0UL,
    sensor_data.valid ? sensor_data.air_temp_degC               : 0.0f,
    sensor_data.valid ? sensor_data.humdity_percent             : 0.0f,
    sensor_data.valid ? sensor_data.hsensor_temp_degC           : 0.0f,
    sensor_data.valid ? sensor_data.pres_mb                     : 0.0f,
    sensor_data.valid ? sensor_data.internal_temp_degC          : 0.0f,
    sensor_data.valid ? sensor_data.module_status               : 0u,
    sensor_data.valid ? sensor_data.module_error                : 0u,
    sensor_data.valid ? sensor_data.pcb_supply_V                : 0.0f,
    sensor_data.valid ? sensor_data.lsm303_temp_degC            : 0.0f,
    sensor_data.valid ? sensor_data.pcb_heater_on               : 0,
    sensor_data.valid ? sensor_data.mag_hdgXY_deg               : 0.0f,
    sensor_data.valid ? sensor_data.mag_hdgXZ_deg               : 0.0f,
    sensor_data.valid ? sensor_data.mag_hdgYZ_deg               : 0.0f,
    sensor_data.valid ? sensor_data.accelX_mG                   : 0.0f,
    sensor_data.valid ? sensor_data.accelY_mG                   : 0.0f,
    sensor_data.valid ? sensor_data.accelZ_mG                   : 0.0f);

  //Serial.println(DataLine);
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

  Serial.println("RATCHuTS Profiling Unit " + getRPUIdentifier(RPU_VERSION));

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
  wdt_config.timeout = 10; // seconds
  wdt.begin(wdt_config);

  GPS_SERIAL.begin(9600);
  GPS_SERIAL.addMemoryForRead(GPS_Serial_Buffer, sizeof(GPS_Serial_Buffer));

  OPC_SERIAL.begin(9600, SERIAL_8N1_RXINV_TXINV);

  TSEN_SERIAL.begin(9600);

  TDLAS_SERIAL.begin(115200);
  TDLAS_SERIAL.addMemoryForRead(TDLAS_Serial_Buffer, sizeof(TDLAS_Serial_Buffer));
  
  DOCK_SERIAL.begin(115200);

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

  rpuReport(rpu_id, RPU_VERSION, rpu_state,
            vin, v_5V, bat_v, bat_t, charge_i, pcb_t,
            pump_i, opc_i, tsen_i, tdlas_i, heater_i,
            heater_on_ticks, heater_total_ticks,
            profiler_gps);

  
  // The state can be changed from the console
  consoleRead(rpu_state, sensorsEnabled);


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
