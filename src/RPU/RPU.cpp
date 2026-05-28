#include "ProfilerHardware.h"
#include "RPUUtil.h"
#include "rpu_version.h"
#include <RS41.h>
#include <Watchdog_t4.h>

#define STATUS_PRINT_INTERVAL_MS 1000

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------
TinyGPSPlus profiler_gps;
TSensor1Bus TempBattery(ONE_WIRE_1);
TSensor1Bus TempPump(ONE_WIRE_2);
TSensor1Bus TempPCB(ONE_WIRE_3);

RS41 rs41(RS41_SERIAL, RS41_ENABLE);

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
static uint16_t loraSerialNumber = 0;

// ---------------------------------------------------------------------------
// Watchdog
// ---------------------------------------------------------------------------
WDT_T4<WDT1> wdt;

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------
elapsedMillis status_timer;

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum class RPUState    { STANDBY, MEASURE };
enum class StandbyState { IDLE, WAITING_FOR_COMMAND };
enum class MeasureState { INIT, SAMPLING, LOGGING };

static RPUState     rpu_state     = RPUState::STANDBY;
static StandbyState standby_state = StandbyState::IDLE;
static MeasureState measure_state = MeasureState::INIT;

// --- STANDBY substates ---
static void enterStandbyIdle()
{
  standby_state = StandbyState::IDLE;
  Serial.println("Standby: IDLE");
}

static void tickStandbyIdle()
{
}

static void enterStandbyWaiting()
{
  standby_state = StandbyState::WAITING_FOR_COMMAND;
  Serial.println("Standby: WAITING_FOR_COMMAND");
}

static void tickStandbyWaiting()
{
}

// --- MEASURE substates ---
static void enterMeasureInit()
{
  measure_state = MeasureState::INIT;
  Serial.println("Measure: INIT");
}

static void tickMeasureInit()
{
}

static void enterMeasureSampling()
{
  measure_state = MeasureState::SAMPLING;
  Serial.println("Measure: SAMPLING");
}

static void tickMeasureSampling()
{
}

static void enterMeasureLogging()
{
  measure_state = MeasureState::LOGGING;
  Serial.println("Measure: LOGGING");
}

static void tickMeasureLogging()
{
}

// --- Top-level states ---
static void enterStandby()
{
  rpu_state = RPUState::STANDBY;
  Serial.println("State: STANDBY");
  enterStandbyIdle();
}

static void tickStandby()
{
  switch (standby_state)
  {
    case StandbyState::IDLE:                tickStandbyIdle();    break;
    case StandbyState::WAITING_FOR_COMMAND: tickStandbyWaiting(); break;
  }
}

static void enterMeasure()
{
  rpu_state = RPUState::MEASURE;
  Serial.println("State: MEASURE");
  enterMeasureInit();
}

static void tickMeasure()
{
  switch (measure_state)
  {
    case MeasureState::INIT:    tickMeasureInit();    break;
    case MeasureState::SAMPLING: tickMeasureSampling(); break;
    case MeasureState::LOGGING:  tickMeasureLogging();  break;
  }
}

static void printStatus()
{
  if (status_timer < STATUS_PRINT_INTERVAL_MS)
    return;
  status_timer = 0;
  Serial.printf("Status: state=%s elapsed=%.3fs\n",
    rpu_state == RPUState::STANDBY ? "STANDBY" : "MEASURE",
    millis() / 1000.0f);
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
  delay(2000); // Allow time to open serial monitor before banner
  Serial.println("RPU " + getRPUIdentifier(RPU_VERSION));

  WDT_timings_t wdt_config;
  wdt_config.timeout = 10; // seconds
  wdt.begin(wdt_config);
  if (wdt.expired())
    Serial.println("Reset caused by watchdog");

  GPS_SERIAL.begin(9600);
  GPS_SERIAL.addMemoryForRead(GPS_Serial_Buffer, sizeof(GPS_Serial_Buffer));

  OPC_SERIAL.begin(9600, SERIAL_8N1_RXINV_TXINV);
  TSEN_SERIAL.begin(9600);
  TDLAS_SERIAL.begin(115200);
  TDLAS_SERIAL.addMemoryForRead(TDLAS_Serial_Buffer, sizeof(TDLAS_Serial_Buffer));
  GONDOLA_SERIAL.begin(115200);

  analogReadResolution(12);
  analogReadAveraging(32);

  // Derive LoRa ID from last two bytes of the Teensy MAC address
  {
    uint8_t mac[6];
    readTeensyMAC(mac);
    loraSerialNumber = ((uint16_t)mac[4] << 8) | mac[5];
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("LoRa ID: %04X\n", loraSerialNumber);
  }

  rs41.init();

  if (!LoRa.begin(868E6))
    Serial.println("Starting LoRa failed!");
  else
    Serial.println("LoRa initialization complete");

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);

  delay(1000);

  TempPCB.PrintSensorAddress();
  if (!TempPCB.ValidateAddrCRC())
    Serial.println("PCB Temp sensor CRC bad — check sensor connection");

  TempPump.PrintSensorAddress();
  if (!TempPump.ValidateAddrCRC())
    Serial.println("Pump Temp sensor CRC bad — check sensor connection");

  TempBattery.PrintSensorAddress();
  if (!TempBattery.ValidateAddrCRC())
    Serial.println("Battery Temp sensor CRC bad — check sensor connection");

  Serial.println(rs41.banner());
  Serial.println("RS41 meta data: " + rs41.meta_data());
  Serial.println(rs41.sensor_data_var_names);

  analogWrite(PUMP_PWM,        0);   // Reset FlexPWM peripheral to zero duty cycle

}

// =============================================================================
// loop
// =============================================================================
void loop()
{
  wdt.feed();

  printStatus();

  switch (rpu_state)
  {
    case RPUState::STANDBY: tickStandby(); break;
    case RPUState::MEASURE: tickMeasure(); break;
  }
}
