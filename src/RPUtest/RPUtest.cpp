#include "ProfilerHardware.h"
#include <RS41.h>

// =============================================================================
// RACHuTS Profiler Hardware Test/Checkout code
// Version 1.2 - Added periodic LoRa telemetry
// Version 1.1 - Improved from V1.0
// Changes (V1.2):
//   - Added LoRa beacon every LORA_TX_INTERVAL cycles containing:
//     elapsed time [ms], GPS altitude [m], battery voltage [V],
//     charge current [A], battery temperature [C], PCB temperature [C]
// Changes (V1.1):
//   - Clamped PWM output in AdjustPump() to prevent analogWrite wraparound
//   - Moved backEMF accumulator to local variable; removed stale global state
//   - Fixed #CHARGER command using `if` instead of `else if` in parseCommand()
//   - Replaced magic pin numbers with named constants in parseCommand()
//   - Added missing pinMode(TSEN_ENABLE, OUTPUT) in setup()
//   - Simplified AdjustHeaters() removing redundant else branch
//   - Replaced String-based DataLine with snprintf to avoid heap fragmentation
//   - Removed unnecessary global String comma = ","
// =============================================================================

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
char nextFilename[14]; // Max 8.3 filename + null terminator

// ---------------------------------------------------------------------------
// Temperature readings
// ---------------------------------------------------------------------------
float PCBTemp     = 0.0f;
float PumpTemp    = 0.0f;
float BatteryTemp = 0.0f;

// ---------------------------------------------------------------------------
// GPS serial buffer
// ---------------------------------------------------------------------------
uint8_t GPS_Serial_Buffer[4096];
// ---------------------------------------------------------------------------  
// TDLAS serial buffer
// ---------------------------------------------------------------------------

uint8_t TDLAS_Serial_Buffer[1028]; // Buffer for TDLAS serial data; size depends on expected line length

// ---------------------------------------------------------------------------
// Pump Back-EMF control
// ---------------------------------------------------------------------------
float BEMF1_V    = 0.0f;
float BEMF1_SP   = 9.0f;   // Set point for large pumps [V]
float error1     = 0.0f;
float error2     = 0.0f;
float Kp         = 30.0f;
int   BEMF1_pwm  = 0;
float VBat       = 0.0f;
bool  pumpEnabled = false;

// ---------------------------------------------------------------------------
// Subsystem current monitors
// ---------------------------------------------------------------------------
float OPC_I             = 0.0f;
float TSEN_I            = 0.0f;
float TDLAS_I           = 0.0f;
float Battery_Heater_I  = 0.0f;

// ---------------------------------------------------------------------------
// Heater control
// ---------------------------------------------------------------------------
float Battery_Heater_Setpoint = 20.0f; // [°C]

// ---------------------------------------------------------------------------
// LoRa telemetry
// ---------------------------------------------------------------------------
// Transmit a status packet every LORA_TX_INTERVAL loop cycles.
static const uint8_t LORA_TX_INTERVAL = 2;
static uint8_t loopCount = 0;    // incremented each loop(); rolls over naturally

// Serial number derived from the last two bytes of the Teensy's burned-in
// Ethernet MAC address.  Populated once in setup() via readTeensyMAC().
// e.g. MAC ....:C2:21  →  serialNumber = 0xC221 = 49697  →  "C221"
static uint16_t loraSerialNumber = 0;
static bool     loraEnabled      = true;

// ---------------------------------------------------------------------------
// Serial receive buffers
// ---------------------------------------------------------------------------
String OPCString    = "";
String TSENString   = "";
String TDLASString  = "";
String SerialString = "";

ROPCData opcData;
TDLASData tdlasData;

// =============================================================================
// readTeensyMAC
//
// Reads the 6-byte Ethernet MAC address burned into the Teensy 4.x hardware
// fuse registers (HW_OCOTP_MAC0 / HW_OCOTP_MAC1).  No external library is
// required — these registers are always present on the iMX RT1062.
//
// Byte order matches the standard Teensy teensyMAC() convention:
//   mac[0..1]  ← HW_OCOTP_MAC1  (most-significant bytes)
//   mac[2..5]  ← HW_OCOTP_MAC0  (least-significant bytes)
// =============================================================================
static void readTeensyMAC(uint8_t mac[6])
{
  for (uint8_t i = 0; i < 2; i++)
    mac[i] = (HW_OCOTP_MAC1 >> ((1 - i) * 8)) & 0xFF;
  for (uint8_t i = 0; i < 4; i++)
    mac[i + 2] = (HW_OCOTP_MAC0 >> ((3 - i) * 8)) & 0xFF;
}


// =============================================================================
// AdjustPump
//
// Reads back-EMF from pump, computes proportional error, and updates PWM.
// PWM is clamped to [0, 255] to prevent analogWrite() wraparound.
// The accumulator is a local variable so stale state cannot carry over
// between calls.
// =============================================================================
void AdjustPump()
{
  if (!pumpEnabled)
  {
    analogWrite(PUMP_PWM, 0);
    return;
  }

  analogWrite(PUMP_PWM, 0);       // Turn off pump briefly to read back-EMF
  delayMicroseconds(100);         // Allow inductive spike to collapse

  long accumulator = 0;
  for (int i = 0; i < 32; i++)
    accumulator += analogRead(PUMP_BEMF);

  BEMF1_V   = VBat - (accumulator / (4095.0f * 32.0f)) * 18.0f;
  error1    = BEMF1_V - BEMF1_SP;
  BEMF1_pwm = constrain(int(BEMF1_pwm - error1 * Kp), 0, 255);

  analogWrite(PUMP_PWM, BEMF1_pwm);
  delay(10);
}


// =============================================================================
// analogReadAvg
//
// Returns the average of `numSamples` successive analogRead() calls on `pin`.
// Using a long accumulator prevents overflow for up to ~4M samples on a 12-bit
// ADC (max raw value 4095 * 4,194,304 < 2^32).
// =============================================================================
static float analogReadAvg(int pin, int numSamples)
{
  long accumulator = 0;
  for (int i = 0; i < numSamples; i++)
    accumulator += analogRead(pin);
  return accumulator / (float)numSamples;
}


// =============================================================================
// CheckCurrents
//
// Reads current monitor for each subsystem via analog multiplexer.
// Each channel is sampled 100 times and averaged to reduce noise.
// =============================================================================
static const int CURRENT_AVG_SAMPLES = 100;

void CheckCurrents()
{
  digitalWrite(DEN, HIGH);
  delayMicroseconds(100);

  // OPC
  digitalWrite(DSEL_0, LOW);
  digitalWrite(DSEL_1, LOW);
  delayMicroseconds(1000);
  OPC_I = analogReadAvg(SWITCH_IMON, CURRENT_AVG_SAMPLES) / 4095.0f * 3300.0f * K_ILIS / R_ILIS;

  // TSEN
  digitalWrite(DSEL_0, HIGH);
  digitalWrite(DSEL_1, LOW);
  delayMicroseconds(1000);
  TSEN_I = analogReadAvg(SWITCH_IMON, CURRENT_AVG_SAMPLES) / 4095.0f * 3300.0f * K_ILIS / R_ILIS;

  // TDLAS
  digitalWrite(DSEL_0, LOW);
  digitalWrite(DSEL_1, HIGH);
  delayMicroseconds(1000);
  TDLAS_I = analogReadAvg(SWITCH_IMON, CURRENT_AVG_SAMPLES) / 4095.0f * 3300.0f * K_ILIS / R_ILIS;

  // Battery Heater
  digitalWrite(DSEL_0, HIGH);
  digitalWrite(DSEL_1, HIGH);
  delayMicroseconds(1000);
  Battery_Heater_I = analogReadAvg(SWITCH_IMON, CURRENT_AVG_SAMPLES) / 4095.0f * 3300.0f * K_ILIS / R_ILIS;
}


// =============================================================================
// AdjustHeaters
//
// Simple bang-bang heater controller with 1°C hysteresis band.
// Returns true if heater is ON, false if OFF.
// =============================================================================
bool AdjustHeaters(float temperature, float setpoint)
{
  bool heat = (temperature < setpoint - 0.5f);
  digitalWrite(BATTERY_HEATER, heat ? HIGH : LOW);
  return heat;
}


// =============================================================================
// InitializeSD
//
// Initialises SD card and finds the next available log filename
// (LOG000.TXT, LOG001.TXT, ...).
// =============================================================================
void InitializeSD()
{
  if (!SD.begin(chipSelect))
  {
    Serial.println("SD card failed, or not present");
    return;
  }
  Serial.println("SD card initialized.");

  // LOG000.TXT .. LOG999.TXT = 10 chars + null = 11 bytes, safely within the
  // 14-byte nextFilename buffer.  Cap at 999 to guarantee the format string
  // never exceeds the buffer and to stay within 8.3 filename limits.
  int fileIndex = 0;
  do {
    if (fileIndex > 999)
    {
      Serial.println("ERROR: No available log filename (LOG000-LOG999 all exist)");
      return;
    }
    snprintf(nextFilename, sizeof(nextFilename), "LOG%03d.TXT", fileIndex);
    fileIndex++;
  } while (SD.exists(nextFilename));

  Serial.print("Next available filename: ");
  Serial.println(nextFilename);
}


// =============================================================================
// parseOPCString
//
// Parses a 10-field comma-separated ROPC string into an ROPCData struct.
// Expected format: ROPC_time,d300,d500,d700,d1000,d2000,d2500,d3000,d5000,alarm
// Example:         2540,5563,2755,942,114,39,22,16,0,0
//
// Returns true on success, false if the field count is wrong or any token is
// missing (i.e. the string is malformed).
// =============================================================================
static const uint8_t ROPC_FIELD_COUNT = 10;

bool parseOPCString(const String& raw, ROPCData& out)
{
  char buf[64];
  raw.toCharArray(buf, sizeof(buf));

  const char* tokens[ROPC_FIELD_COUNT];
  uint8_t count = 0;

  tokens[count++] = strtok(buf, ",");
  while (count < ROPC_FIELD_COUNT)
  {
    const char* t = strtok(NULL, ",");
    if (!t)
    {
      Serial.printf("OPC parse error: expected %u fields, got %u\n",
                    ROPC_FIELD_COUNT, count);
      return false;
    }
    tokens[count++] = t;
  }

  // Verify no extra fields are present
  if (strtok(NULL, ",") != nullptr)
  {
    Serial.println("OPC parse error: too many fields");
    return false;
  }

  out.ROPC_time = (uint32_t)atol(tokens[0]);
  out.d300      = (uint16_t)atoi(tokens[1]);
  out.d500      = (uint16_t)atoi(tokens[2]);
  out.d700      = (uint16_t)atoi(tokens[3]);
  out.d1000     = (uint16_t)atoi(tokens[4]);
  out.d2000     = (uint16_t)atoi(tokens[5]);
  out.d2500     = (uint16_t)atoi(tokens[6]);
  out.d3000     = (uint16_t)atoi(tokens[7]);
  out.d5000     = (uint16_t)atoi(tokens[8]);
  out.alarm     = (uint8_t) atoi(tokens[9]);

  return true;
}


// =============================================================================
// parseTDLASString
//
// Parses a 7-field comma-separated TDLAS string into a TDLASData struct.
// Expected format: mr_avg,bkg,peak,ratio,batt,therm_1,therm_2
// Example:         1.23,0.45,512.3,0.0023,3.80,22.1,21.8
//
// Returns true on success, false if the field count is wrong or any token is
// missing (i.e. the string is malformed).
// =============================================================================
static const uint8_t TDLAS_FIELD_COUNT = 12;

bool parseTDLASString(const String& raw, TDLASData& out)
{
  char buf[128];
  raw.toCharArray(buf, sizeof(buf));

  const char* tokens[TDLAS_FIELD_COUNT];
  uint8_t count = 0;

  tokens[count++] = strtok(buf, ",");
  while (count < TDLAS_FIELD_COUNT)
  {
    const char* t = strtok(NULL, ",");
    if (!t)
    {
      Serial.printf("TDLAS parse error: expected %u fields, got %u\n",
                    TDLAS_FIELD_COUNT, count);
      return false;
    }
    tokens[count++] = t;
  }

  if (strtok(NULL, ",") != nullptr)
  {
    Serial.println("TDLAS parse error: too many fields");
    return false;
  }

  out.mr_avg  = atof(tokens[0]);
  out.bkg     = atof(tokens[1]);
  out.peak    = atof(tokens[2]);
  out.ratio   = atof(tokens[3]);
  out.batt    = atof(tokens[4]);
  out.therm_1 = atof(tokens[5]);
  out.therm_2 = atof(tokens[6]);
  out.indx = atof(tokens[7]);
  out.spec_1 = atof(tokens[8]);
  out.spec_2 = atof(tokens[9]);
  out.spec_3 = atof(tokens[10]);
  out.spec_4 = atof(tokens[11]);

  return true;
}


// =============================================================================
// parseOnOff
//
// Helper: returns 1, 0, or -1 (unrecognised) for an On/Off argument token.
// =============================================================================
static int parseOnOff(const char* token)
{
  int val = atoi(token);
  if (val == 1) return 1;
  if (val == 0) return 0;
  return -1;
}


// =============================================================================
// parseCommand
//
// Interprets text commands received over the debug serial port.
//
// Supported commands:
//   #Temp,<float>   - Set battery heater temperature setpoint [°C]
//   #BEMF,<float>   - Set ROPC pump back-EMF setpoint [V]
//   #PUMP,<1/0>     - Enable/disable pump
//   #TDLAS,<1/0>    - Enable/disable TDLAS
//   #ROPC,<1/0>     - Enable/disable OPC
//   #TSEN,<1/0>     - Enable/disable TSEN
//   #RS41,<1/0>     - Enable/disable RS41
//   #CHARGER,<1/0>  - Enable/disable battery charger
//   #HELP           - Print command list
// =============================================================================
void parseCommand(const String& commandToParse)
{
  // ---- #HELP ----------------------------------------------------------------
  if (commandToParse.startsWith("#HELP"))
  {
    Serial.println("Available Commands:");
    Serial.println("  #Temp,<float>  - Set Battery Heater Temp Setpoint [C]");
    Serial.println("  #BEMF,<float>  - Set ROPC Pump Back EMF Setpoint [V]");
    Serial.println("  #PUMP,<1/0>    - Enable(1)/Disable(0) ROPC Pump");
    Serial.println("  #TDLAS,<1/0>   - Enable(1)/Disable(0) TDLAS");
    Serial.println("  #RS41,<1/0>    - Enable(1)/Disable(0) RS41");
    Serial.println("  #ROPC,<1/0>    - Enable(1)/Disable(0) ROPC OPC");
    Serial.println("  #TSEN,<1/0>    - Enable(1)/Disable(0) TSEN");
    Serial.println("  #CHARGER,<1/0> - Enable(1)/Disable(0) Battery Charger");
    Serial.println("  #LORA,<1/0>    - Enable(1)/Disable(0) LoRa Transmissions");
    return;
  }

  // Parse command name and argument token once, shared by all branches below.
  // arg is NULL if the command contains no comma (missing argument).
  char commandArray[64];
  commandToParse.toCharArray(commandArray, sizeof(commandArray));
  strtok(commandArray, ",");           // skip command name token
  const char* arg = strtok(NULL, ","); // argument token (may be NULL)

  // ---- #Temp ----------------------------------------------------------------
  if (commandToParse.startsWith("#Temp"))
  {
    if (!arg) { Serial.println("Temp: missing argument"); return; }
    float flt1 = atof(arg);
    Serial.print("Setting Battery Heater temperature to: ");
    Serial.print(flt1);
    Serial.println(" [C]");
    Battery_Heater_Setpoint = flt1;
  }

  // ---- #BEMF ----------------------------------------------------------------
  else if (commandToParse.startsWith("#BEMF"))
  {
    if (!arg) { Serial.println("BEMF: missing argument"); return; }
    float flt1 = atof(arg);
    Serial.print("Setting ROPC Pump Back EMF to: ");
    Serial.print(flt1);
    Serial.println(" [V]");
    BEMF1_SP = flt1;
  }

  // ---- #PUMP ----------------------------------------------------------------
  else if (commandToParse.startsWith("#PUMP"))
  {
    int onOff = arg ? parseOnOff(arg) : -1;
    if      (onOff == 1) { pumpEnabled = true;  Serial.println("PUMP enabled");  }
    else if (onOff == 0) { pumpEnabled = false; Serial.println("PUMP disabled"); }
    else Serial.println("PUMP: unrecognised argument, use 1 to enable, 0 to disable");
  }

  // ---- #TDLAS ---------------------------------------------------------------
  else if (commandToParse.startsWith("#TDLAS"))
  {
    int onOff = arg ? parseOnOff(arg) : -1;
    if (onOff == 1)
    {
      TDLAS_SERIAL.begin(9600);
      digitalWrite(TDLAS_ENABLE, HIGH);
      Serial.println("TDLAS enabled");
    }
    else if (onOff == 0)
    {
      // Set TX/RX to INPUT to prevent back-feeding current when disabled
      pinMode(TDLAS_TX_PIN, INPUT);
      pinMode(TDLAS_RX_PIN, INPUT);
      digitalWrite(TDLAS_ENABLE, LOW);
      Serial.println("TDLAS disabled");
    }
    else Serial.println("TDLAS: unrecognised argument, use 1 to enable, 0 to disable");
  }

  // ---- #RS41 ----------------------------------------------------------------
  else if (commandToParse.startsWith("#RS41"))
  {
    int onOff = arg ? parseOnOff(arg) : -1;
    if (onOff == 1)
    {
      RS41_SERIAL.begin(56700);
      digitalWrite(RS41_ENABLE, HIGH);
      Serial.println("RS41 enabled");
    }
    else if (onOff == 0)
    {
      pinMode(RS41_TX_PIN, INPUT);
      pinMode(RS41_RX_PIN, INPUT);
      digitalWrite(RS41_ENABLE, LOW);
      Serial.println("RS41 disabled");
    }
    else Serial.println("RS41: unrecognised argument, use 1 to enable, 0 to disable");
  }

  // ---- #ROPC ----------------------------------------------------------------
  else if (commandToParse.startsWith("#ROPC"))
  {
    int onOff = arg ? parseOnOff(arg) : -1;
    if (onOff == 1)
    {
      OPC_SERIAL.begin(9600, SERIAL_8N1_RXINV_TXINV);
      digitalWrite(OPC_ENABLE, HIGH);
      Serial.println("ROPC enabled");
    }
    else if (onOff == 0)
    {
      pinMode(OPC_TX_PIN, INPUT);
      pinMode(OPC_RX_PIN, INPUT);
      digitalWrite(OPC_ENABLE, LOW);
      Serial.println("ROPC disabled");
    }
    else Serial.println("ROPC: unrecognised argument, use 1 to enable, 0 to disable");
  }

  // ---- #TSEN ----------------------------------------------------------------
  else if (commandToParse.startsWith("#TSEN"))
  {
    int onOff = arg ? parseOnOff(arg) : -1;
    if      (onOff == 1) { digitalWrite(TSEN_ENABLE, HIGH); Serial.println("TSEN enabled");  }
    else if (onOff == 0) { digitalWrite(TSEN_ENABLE, LOW);  Serial.println("TSEN disabled"); }
    else Serial.println("TSEN: unrecognised argument, use 1 to enable, 0 to disable");
  }

  // ---- #CHARGER -------------------------------------------------------------
  else if (commandToParse.startsWith("#CHARGER"))
  {
    int onOff = arg ? parseOnOff(arg) : -1;
    // Charger logic is inverted: LOW = enabled, HIGH = shutdown
    if      (onOff == 1) { digitalWrite(CHARGER_SHTDWN, LOW);  Serial.println("CHARGER enabled");  }
    else if (onOff == 0) { digitalWrite(CHARGER_SHTDWN, HIGH); Serial.println("CHARGER disabled"); }
    else Serial.println("CHARGER: unrecognised argument, use 1 to enable, 0 to disable");
  }

  // ---- #LORA ----------------------------------------------------------------
  else if (commandToParse.startsWith("#LORA"))
  {
    int onOff = arg ? parseOnOff(arg) : -1;
    if      (onOff == 1) { loraEnabled = true;  Serial.println("LoRa TX enabled");  }
    else if (onOff == 0) { loraEnabled = false; Serial.println("LoRa TX disabled"); }
    else Serial.println("LORA: unrecognised argument, use 1 to enable, 0 to disable");
  }

  // ---- Unrecognised ---------------------------------------------------------
  else
  {
    Serial.println("Command not recognized. Send #HELP for a list of commands.");
  }
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
  pinMode(TSEN_ENABLE,     OUTPUT);  // FIX: was missing in original
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

  GPS_SERIAL.begin(9600);
  GPS_SERIAL.addMemoryForRead(GPS_Serial_Buffer, sizeof(GPS_Serial_Buffer));

  OPC_SERIAL.begin(9600, SERIAL_8N1_RXINV_TXINV);
  TSEN_SERIAL.begin(9600);
  TDLAS_SERIAL.begin(115200);
  TDLAS_SERIAL.addMemoryForRead(TDLAS_Serial_Buffer, sizeof(TDLAS_Serial_Buffer));
  GONDOLA_SERIAL.begin(115200);

  analogReadResolution(12);
  analogReadAveraging(32);

  InitializeSD();

  // Write file header
  {
    File dataFile = SD.open(nextFilename, FILE_WRITE);
    if (dataFile)
    {
      dataFile.println("serial_hex,elapsed_ms,"
        "VBat,vin,charge_imon,vmon_5V,I_pump,"
        "OPC_I,TSEN_I,TDLAS_I,Battery_Heater_I,BEMF1_V,BEMF1_pwm,"
        "GPS_lat,GPS_lng,GPS_alt_m,GPS_satellites,GPS_date,GPS_time,GPS_age_s,"
        "PCBTemp,PumpTemp,BatteryTemp,"
        "ROPC_time,d300,d500,d700,d1000,d2000,d2500,d3000,d5000,OPC_alarm,"
        "TDLAS_mr_avg,TDLAS_bkg,TDLAS_peak,TDLAS_ratio,TDLAS_batt,TDLAS_therm_1,TDLAS_therm_2,TDLAS_indx,TDLAS_spec_1,TDLAS_spec_2,TDLAS_spec_3,TDLAS_spec_4,"
        "RS41_frame,RS41_air_temp,RS41_humidity,RS41_hsensor_temp,RS41_pres,"
        "RS41_internal_temp,RS41_module_status,RS41_module_error,RS41_pcb_supply_V,"
        "RS41_lsm303_temp,RS41_pcb_heater_on,"
        "RS41_mag_hdgXY,RS41_mag_hdgXZ,RS41_mag_hdgYZ,"
        "RS41_accelX,RS41_accelY,RS41_accelZ");
      dataFile.close();
    }
  }

  // Derive LoRa serial number from last two bytes of the Teensy MAC address
  {
    uint8_t mac[6];
    readTeensyMAC(mac);
    loraSerialNumber = ((uint16_t)mac[4] << 8) | mac[5];
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("LoRa Serial Number: %04X\n", loraSerialNumber);
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
}


// =============================================================================
// loop
// =============================================================================
void loop()
{
  File dataFile = SD.open(nextFilename, FILE_WRITE);

  // --- Heartbeat LED ---------------------------------------------------------
  digitalWrite(PULSE_LED, LOW);
  delay(500);
  digitalWrite(PULSE_LED, HIGH);
  delay(500);

  // --- RS41 Radiosonde -------------------------------------------------------
  RS41::RS41SensorData_t sensor_data = rs41.decoded_sensor_data(false);

  if (sensor_data.valid)
  {
    Serial.printf("RS41: frame=%lu air_temp=%.2fC humidity=%.2f%% hsensor_temp=%.2fC pres=%.2fmb "
                  "int_temp=%.2fC status=%u err=%u pcb_supply=%.3fV lsm303_temp=%.2fC heater=%d "
                  "hdgXY=%.2f hdgXZ=%.2f hdgYZ=%.2f accelX=%.2f accelY=%.2f accelZ=%.2f\n",
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

  // --- Gondola serial loopback test ------------------------------------------
  Serial.println("Testing Gondola Serial");
  GONDOLA_SERIAL.println("Passed...");
  while (GONDOLA_SERIAL.available() > 0)
    Serial.print((char)GONDOLA_SERIAL.read());

  // --- OPC -------------------------------------------------------------------
  while (OPC_SERIAL.available())
  {
    char c = OPC_SERIAL.read();
    OPCString += c;
    if (c == '\n') break;
  }
  OPC_SERIAL.flush();
  if (parseOPCString(OPCString, opcData))
  {
    Serial.printf("ROPC: time=%lu d300=%u d500=%u d700=%u d1000=%u d2000=%u d2500=%u d3000=%u d5000=%u alarm=%u\n",
      opcData.ROPC_time, opcData.d300, opcData.d500, opcData.d700, opcData.d1000,
      opcData.d2000, opcData.d2500, opcData.d3000, opcData.d5000, opcData.alarm);
  }

  OPCString = "";

  // --- TSEN -------------------------------------------------------------------
  TSEN_SERIAL.print("*01A?\r");
  TSEN_SERIAL.flush();
  delay(10);
  Serial.print("TSEN: ");
  while(TSEN_SERIAL.available() >0)
    Serial.write(TSEN_SERIAL.read());
  Serial.println();

  // --- TDLAS -----------------------------------------------------------------
  
  while (TDLAS_SERIAL.available() > 0)
  {
    char c = TDLAS_SERIAL.read();
    //Serial.write(c); // Echo TDLAS string as it's received for debugging
    if (c == '\n') break;
    TDLASString += c;
  }
  if (parseTDLASString(TDLASString, tdlasData))
  {
    
    Serial.printf("TDLAS: mr_avg=%.4f bkg=%.4f peak=%.4f ratio=%.6f batt=%.3fV therm_1=%.2fC Laser T=%.2f Bits, Idx=%d, spec_1=%.4f, spec_2=%.4f, spec_3=%.4f, spec_4=%.4f\n",
      tdlasData.mr_avg, tdlasData.bkg, tdlasData.peak, tdlasData.ratio,
      tdlasData.batt, tdlasData.therm_1, tdlasData.therm_2, tdlasData.indx, tdlasData.spec_1, tdlasData.spec_2, tdlasData.spec_3, tdlasData.spec_4);
  }
  TDLASString = "";

  // --- Debug serial command input --------------------------------------------
  while (Serial.available())
  {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r')
    {
      if (SerialString.length() > 0)
      {
        parseCommand(SerialString);
        SerialString = "";
      }
    }
    else
    {
      SerialString += ch;
    }
  }

  // --- Analog voltage / current monitors ------------------------------------
  VBat          = analogReadAvg(BAT_VMON,    CURRENT_AVG_SAMPLES) / 4095.0f * 3.3f * 12.49f / 2.49f;
  float vin       = analogReadAvg(VIN_VMON,    CURRENT_AVG_SAMPLES) / 4095.0f * 3.3f * 11.7f  / 1.37f;
  float charge_imon = analogReadAvg(CHARGE_IMON,  CURRENT_AVG_SAMPLES) / 4095.0f * 3.3f;
  float vmon_5V     = analogReadAvg(VMON_5V,      CURRENT_AVG_SAMPLES) / 4095.0f * 3.3f * 2.0f;
  float I_pump      = analogReadAvg(PUMP_IMON,    CURRENT_AVG_SAMPLES) / 4095.0f * 3.3f * 1000.0f;

  Serial.print("Battery Voltage: ");    Serial.println(VBat);
  Serial.print("Input Voltage: ");      Serial.println(vin);
  Serial.print("Charge Current: ");     Serial.println(charge_imon);
  Serial.print("5V Supply: ");          Serial.println(vmon_5V);
  Serial.print("Pump Current (mA): ");  Serial.println(I_pump);
  Serial.print("OPC Current: ");        Serial.println(OPC_I);
  Serial.print("TSEN Current: ");       Serial.println(TSEN_I);
  Serial.print("TDLAS Current: ");      Serial.println(TDLAS_I);
  Serial.print("Battery Heater Current: "); Serial.println(Battery_Heater_I);

  // --- GPS -------------------------------------------------------------------
  while (GPS_SERIAL.available() > 0)
    profiler_gps.encode(GPS_SERIAL.read());

  Serial.printf("GPS: %f, %f, %f, %d, %d, %d, %d\n",
    profiler_gps.location.lat(),
    profiler_gps.location.lng(),
    profiler_gps.altitude.meters(),
    profiler_gps.satellites.value(),
    profiler_gps.date.value(),
    profiler_gps.time.value(),
    profiler_gps.location.age() / 1000);

  // --- Temperature sensors ---------------------------------------------------
  TempPCB.ManageState(PCBTemp);
  Serial.print("PCB Temp: ");     Serial.println(PCBTemp);

  TempPump.ManageState(PumpTemp);
  Serial.print("Pump Temp: ");    Serial.println(PumpTemp);

  TempBattery.ManageState(BatteryTemp);
  Serial.print("Battery Temp: "); Serial.println(BatteryTemp);

  // --- Control loops ---------------------------------------------------------
  AdjustPump();
  AdjustHeaters(BatteryTemp, Battery_Heater_Setpoint);
  CheckCurrents();

  // --- Write combined data line to SD ----------------------------------------
  // Single CSV row containing all collected variables.
  // Fields: serial_hex, elapsed_ms,
  //         VBat, vin, charge_imon, vmon_5V, I_pump,
  //         OPC_I, TSEN_I, TDLAS_I, Battery_Heater_I, BEMF1_V, BEMF1_pwm,
  //         GPS_lat, GPS_lng, GPS_alt_m, GPS_satellites, GPS_date, GPS_time, GPS_age_s,
  //         PCBTemp, PumpTemp, BatteryTemp,
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
    loraSerialNumber, millis(),
    VBat, vin, charge_imon, vmon_5V, I_pump,
    OPC_I, TSEN_I, TDLAS_I, Battery_Heater_I, BEMF1_V, BEMF1_pwm,
    profiler_gps.location.lat(), profiler_gps.location.lng(), profiler_gps.altitude.meters(),
    profiler_gps.satellites.value(), profiler_gps.date.value(), profiler_gps.time.value(), profiler_gps.location.age() / 1000,
    PCBTemp, PumpTemp, BatteryTemp,
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

  dataFile.println(DataLine);

  // --- LoRa telemetry beacon -------------------------------------------------
  // Transmit a compact CSV status packet every LORA_TX_INTERVAL loop cycles.
  // Payload format (all fields comma-separated):
  //   serial_hex, elapsed_ms, lat, lng, altitude_m, vbat_V, charge_I_A,
  //   bat_temp_C, pcb_temp_C, rs41_air_temp_C, tdlas_mr_avg
  ++loopCount;
  if (loraEnabled && loopCount >= LORA_TX_INTERVAL)
  {
    loopCount = 0;

    char loraBuf[128];
    snprintf(loraBuf, sizeof(loraBuf),
      "%04X,%lu,%.6f,%.6f,%.2f,%.3f,%.3f,%.2f,%.2f,%.2f,%.4f",
      loraSerialNumber,
      millis(),
      profiler_gps.location.lat(),
      profiler_gps.location.lng(),
      profiler_gps.altitude.meters(),
      VBat,
      charge_imon,
      BatteryTemp,
      PCBTemp,
      sensor_data.valid ? sensor_data.air_temp_degC : 0.0f,
      tdlasData.mr_avg);

    LoRa.beginPacket();
    LoRa.print(loraBuf);
    LoRa.endPacket();

    Serial.print("LoRa TX: ");
    Serial.println(loraBuf);

   //dataFile.print("LORA_TX: ");
    //dataFile.println(loraBuf);
  }

  dataFile.close();
}