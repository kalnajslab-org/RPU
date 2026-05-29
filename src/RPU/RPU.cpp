#include <TimeLib.h>
#include <RS41.h>
#include <Watchdog_t4.h>

#include "ProfilerHardware.h"
#include "RPUConfig.h"
#include "RPUComm.h"
#include "RPUUtil.h"
#include "rpu_version.h"
#include "RPUTypes.h"
#include "RPUAnalog.h"
#include "RPUBattery.h"
#include "RPUStatus.h"
#include "RPUConsole.h"

// ---------------------------------------------------------------------------
// Configurable parameters (settable via command)
// ---------------------------------------------------------------------------
static float    V_CRIT_BATT               = CFG_V_CRIT_BATT;
static float    V_LOW_BATT                = CFG_V_LOW_BATT;
static float    Battery_T_Setpoint        = CFG_BATTERY_T_SETPOINT;

// ---------------------------------------------------------------------------
// MEASURE parameters (set by RPU_GO_MEASURE command)
// ---------------------------------------------------------------------------
static int32_t MeasureDuration = CFG_MEASURE_DURATION;
static int32_t MeasureRate     = CFG_MEASURE_RATE;
static int8_t  OPC_Power       = 0;
static int8_t  TDLAS_Power     = 0;
static int8_t  TSEN_Power      = 0;

// ---------------------------------------------------------------------------
// Device objects
// ---------------------------------------------------------------------------
TinyGPSPlus profiler_gps;
TSensor1Bus TempBattery(ONE_WIRE_1);
TSensor1Bus TempPump(ONE_WIRE_2);
TSensor1Bus TempPCB(ONE_WIRE_3);

RS41 rs41(RS41_SERIAL, RS41_ENABLE);

RPUComm rpucomm(&Serial1);

// ---------------------------------------------------------------------------
// SD card
// ---------------------------------------------------------------------------
const int chipSelect = BUILTIN_SDCARD;
char nextFilename[14];

// ---------------------------------------------------------------------------
// GPS / TDLAS serial buffers
// ---------------------------------------------------------------------------
uint8_t GPS_Serial_Buffer[4096];
uint8_t TDLAS_Serial_Buffer[1028];

// ---------------------------------------------------------------------------
// LoRa
// ---------------------------------------------------------------------------
static uint16_t rpu_id = 0;

// ---------------------------------------------------------------------------
// Watchdog
// ---------------------------------------------------------------------------
WDT_T4<WDT1> wdt;


// ---------------------------------------------------------------------------
// Battery and heater state
// ---------------------------------------------------------------------------
static float    BatteryTemp        = 0.0f;
static float    PCBTemp            = 0.0f;
static float    PumpTemp           = 0.0f;
static uint32_t heater_on_ticks    = 0;
static uint32_t heater_total_ticks = 0;

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
static RPUState rpu_state = RPUState::STANDBY;

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
                                   BatteryTemp, PCBTemp,
                                   digitalRead(BATTERY_HEATER));
          DEBUG_SERIAL.println("Received RPU_SEND_STATUS");
          return false;

        case RPU_SEND_RECORDS:
          DEBUG_SERIAL.println("Received RPU_SEND_RECORDS");
          sendTM();
          return false;

        case RPU_GO_MEASURE:
          tmp1 = rpucomm.RX_GoMeasure(&MeasureDuration, &MeasureRate,
                                      &OPC_Power, &TDLAS_Power, &TSEN_Power);
          rpucomm.TX_Ack(RPU_GO_MEASURE, tmp1);
          if (tmp1) {
            DEBUG_SERIAL.println("Received RPU_GO_MEASURE");
            enterMeasure(rpu_state);
            return true;
          }
          return false;

        case RPU_GO_STANDBY:
          rpucomm.TX_Ack(RPU_GO_STANDBY, true);
          DEBUG_SERIAL.println("Received RPU_GO_STANDBY");
          enterStandby(rpu_state);
          return true;

        case RPU_SET_BATT_T:
          tmp1 = rpucomm.Get_float(&Battery_T_Setpoint);
          rpucomm.TX_Ack(RPU_SET_BATT_T, tmp1);
          DEBUG_SERIAL.printf("[nominal] BATT_T_SET=%.2f\n", Battery_T_Setpoint);
          return false;

        case RPU_SET_V_LOW_BATT:
          tmp1 = rpucomm.Get_float(&V_LOW_BATT);
          rpucomm.TX_Ack(RPU_SET_V_LOW_BATT, tmp1);
          DEBUG_SERIAL.printf("[nominal] V_LOW_BATT=%.2f\n", V_LOW_BATT);
          return false;

        case RPU_SET_V_CRIT_BATT:
          tmp1 = rpucomm.Get_float(&V_CRIT_BATT);
          rpucomm.TX_Ack(RPU_SET_V_CRIT_BATT, tmp1);
          DEBUG_SERIAL.printf("[nominal] V_CRIT_BATT=%.2f\n", V_CRIT_BATT);
          return false;

        case RPU_SET_STATUS_RATE: {
          uint32_t rate = getRPUReportInterval();
          tmp1 = rpucomm.Get_uint32(&rate);
          if (tmp1) { setRPUReportInterval(rate); }
          rpucomm.TX_Ack(RPU_SET_STATUS_RATE, tmp1);
          DEBUG_SERIAL.printf("[nominal] STATUS_RATE=%lu\n", getRPUReportInterval());
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
  while (GPS_SERIAL.available() > 0) {
    profiler_gps.encode(GPS_SERIAL.read());
  }

  float vin  = readVin();
  float vbat = readVBat();

  powerdownSensors();
  updateTemperatures(TempBattery, TempPCB, BatteryTemp, PCBTemp);
  manageHeater(BatteryTemp, Battery_T_Setpoint,
               vin, vbat, V_CRIT_BATT,
               heater_on_ticks, heater_total_ticks);
}

static void tickMeasure()
{
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

  // Trigger STARTUP conversion on all temperature sensors (750 ms each).
  // Three sensors total ~2.25 s, providing a convenient delay for the
  // developer to connect a serial monitor before the banner is printed.
  TempPCB.ManageState(PCBTemp);
  TempPump.ManageState(PumpTemp);
  TempBattery.ManageState(BatteryTemp);

  Serial.println("RATCHuTS Profiling Unit " + getRPUIdentifier(RPU_VERSION));

  // Check for watchdog reset before initializing the WDT, as wdt.begin()
  // clears the reset-cause register.
  if (wdt.expired()) {
    Serial.println("WARNING: previous run was reset by the watchdog — the loop"
                   " stalled for > 10 s without calling wdt.feed().");
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
}

// =============================================================================
// loop
// =============================================================================
void loop()
{
  wdt.feed();

  float vbat = readVBat();
  float vin  = readVin();

  //----------------------------------------------------
  // Activities that run in all states
  //----------------------------------------------------

  // Check for incoming commands from RATCHuTS over the docking connector
  dockComms();

  consoleReport(rpu_state, millis() / 1000.0f,
                vbat, vin, digitalRead(BATTERY_HEATER));

  rpuReport(rpu_id, RPU_VERSION, rpu_state,
            vbat, BatteryTemp, readChargeI(), PCBTemp,
            heater_on_ticks, heater_total_ticks,
            profiler_gps);

  
  // The state can be changed from the console
  consoleRead(rpu_state);

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
