#include "ProfilerHardware.h"
#include "RPUUtil.h"
#include "rpu_version.h"
#include <RS41.h>
#include <Watchdog_t4.h>

#define STATUS_PRINT_INTERVAL_S   1UL
#define V_CRIT_BATT               10.0f          // [V] disable heater below this battery voltage
#define STATUS_TX_INTERVAL_S      60UL

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
elapsedSeconds status_timer;
elapsedSeconds status_tx_timer;

// ---------------------------------------------------------------------------
// Telemetry state
// ---------------------------------------------------------------------------
static float BatteryTemp = 0.0f;
static float PCBTemp     = 0.0f;

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum class RPUState { STANDBY, MEASURE };

static RPUState rpu_state = RPUState::STANDBY;

static void enterStandby()
{
  rpu_state = RPUState::STANDBY;
  Serial.println("State: STANDBY");
}

static float readVin()
{
  long acc = 0;
  for (int i = 0; i < 32; i++) acc += analogRead(VIN_VMON);
  return (acc / (4095.0f * 32)) * 3.3f * 11.7f / 1.37f;
}

static float readVBat()
{
  long acc = 0;
  for (int i = 0; i < 32; i++) acc += analogRead(BAT_VMON);
  return (acc / (4095.0f * 32)) * 3.3f * 12.49f / 2.49f;
}

static float readChargeI()
{
  long acc = 0;
  for (int i = 0; i < 32; i++) acc += analogRead(CHARGE_IMON);
  return (acc / (4095.0f * 32)) * 3.3f;
}

static void sendStandbyStatus()
{
  char buf[200];
  int n = snprintf(buf, sizeof(buf),
    "{\"id\":\"%04X\",\"ver\":\"%s\",\"vbat\":%.2f,\"bat_t\":%.2f,\"chg_i\":%.3f,\"pcb_t\":%.2f,\"heat\":%d,\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"sats\":%lu}",
    loraSerialNumber,
    RPU_VERSION,
    readVBat(),
    BatteryTemp,
    readChargeI(),
    PCBTemp,
    digitalRead(BATTERY_HEATER) ? 100 : 0,
    profiler_gps.location.lat(),
    profiler_gps.location.lng(),
    profiler_gps.altitude.meters(),
    profiler_gps.satellites.value());

  if (n >= (int)sizeof(buf))
    Serial.println("WARNING: standby status JSON truncated");

  GONDOLA_SERIAL.println(buf);

  LoRa.beginPacket();
  LoRa.print(buf);
  LoRa.endPacket();

  Serial.print("Standby TX: ");
  Serial.println(buf);
}

static void batteryHeater()
{
  float vin  = readVin();
  float vbat = readVBat();
  bool enable = (vin > 13.0f) && (vbat >= V_CRIT_BATT);
  digitalWrite(BATTERY_HEATER, enable ? HIGH : LOW);
}

static void powerdownSensors()
{
  digitalWrite(OPC_ENABLE,    LOW);
  digitalWrite(TSEN_ENABLE,   LOW);
  digitalWrite(TDLAS_ENABLE,  LOW);
  digitalWrite(RS41_ENABLE,   LOW);
  digitalWrite(BATTERY_HEATER, LOW);
  analogWrite(PUMP_PWM,       0);
}

static void tickStandby()
{
  while (GPS_SERIAL.available() > 0)
    profiler_gps.encode(GPS_SERIAL.read());

  TempBattery.ManageState(BatteryTemp);
  TempPCB.ManageState(PCBTemp);

  powerdownSensors();
  batteryHeater();

  if (status_tx_timer >= STATUS_TX_INTERVAL_S)
  {
    status_tx_timer = 0;
    sendStandbyStatus();
  }
}

static void enterMeasure()
{
  rpu_state = RPUState::MEASURE;
  Serial.println("State: MEASURE");
}

static void tickMeasure()
{
}

static void printStatus()
{
  if (status_timer < STATUS_PRINT_INTERVAL_S)
    return;
  status_timer = 0;
  Serial.printf("Status: state=%s elapsed=%.3fs vbat=%.2fV vin=%.2fV bat_heater=%s\n",
    rpu_state == RPUState::STANDBY ? "STANDBY" : "MEASURE",
    millis() / 1000.0f,
    readVBat(),
    readVin(),
    digitalRead(BATTERY_HEATER) ? "ON" : "OFF");
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
