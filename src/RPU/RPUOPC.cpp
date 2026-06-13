#include "RPUOPC.h"
#include "RPUConfig.h"
#include "ProfilerHardware.h"
#include "RPUConsole.h"

// ---------------------------------------------------------------------------
// OPC (ROPC particle counter)
// ---------------------------------------------------------------------------
static const uint8_t ROPC_FIELD_COUNT = 10;

static bool parseOPCString(const String& raw, ROPCData& out)
{
  char buf[64];
  raw.toCharArray(buf, sizeof(buf));

  const char* tokens[ROPC_FIELD_COUNT];
  uint8_t count = 0;

  tokens[count++] = strtok(buf, ",");
  while (count < ROPC_FIELD_COUNT) {
    const char* t = strtok(NULL, ",");
    if (!t) {
      Serial.printf("OPC parse error: expected %u fields, got %u\n", ROPC_FIELD_COUNT, count);
      return false;
    }
    tokens[count++] = t;
  }

  if (strtok(NULL, ",") != nullptr) {
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

bool readOPC(ROPCData& data)
{
  static String buf = "";

  while (OPC_SERIAL.available()) {
    char c = OPC_SERIAL.read();
    buf += c;
    if (c == '\n') { break; }
  }
  OPC_SERIAL.flush();

  if (buf.length() == 0) { return false; }

  if (!parseOPCString(buf, data)) {
    buf = "";
    return false;
  }

  if (getDebugJsonEnabled()) {
    Serial.printf("ROPC: time=%lu d300=%u d500=%u d700=%u d1000=%u d2000=%u d2500=%u d3000=%u d5000=%u alarm=%u\n",
      data.ROPC_time, data.d300, data.d500, data.d700, data.d1000,
      data.d2000, data.d2500, data.d3000, data.d5000, data.alarm);
  }

  buf = "";
  return true;
}

// ---------------------------------------------------------------------------
// Pump back-EMF controller
// ---------------------------------------------------------------------------
void adjustPump(PumpState& pump, float vbat)
{
  if (!pump.enabled) {
    analogWrite(PUMP_PWM, 0);
    return;
  }

  analogWrite(PUMP_PWM, 0);   // Turn off pump briefly to read back-EMF
  delayMicroseconds(100);     // Allow inductive spike to collapse

  long acc = 0;
  for (int i = 0; i < CFG_ADC_SAMPLES; i++) {
    acc += analogRead(PUMP_BEMF);
  }

  pump.bemf_v = vbat - (acc / (4095.0f * CFG_ADC_SAMPLES)) * 18.0f;
  float error = pump.bemf_v - pump.setpoint;
  pump.pwm    = constrain(int(pump.pwm - error * pump.kp), 0, 255);

  analogWrite(PUMP_PWM, pump.pwm);
  delay(10);
}
