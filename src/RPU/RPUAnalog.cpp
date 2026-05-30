#include "RPUAnalog.h"
#include "RPUConfig.h"
#include "ProfilerHardware.h"

float readVin()
{
  long acc = 0;
  for (int i = 0; i < CFG_ADC_SAMPLES; i++) {
    acc += analogRead(VIN_VMON);
  }
  return (acc / (4095.0f * CFG_ADC_SAMPLES)) * 3.3f * 11.7f / 1.37f;
}

float readVBat()
{
  long acc = 0;
  for (int i = 0; i < CFG_ADC_SAMPLES; i++) {
    acc += analogRead(BAT_VMON);
  }
  return (acc / (4095.0f * CFG_ADC_SAMPLES)) * 3.3f * 12.49f / 2.49f;
}

float readChargeI()
{
  long acc = 0;
  for (int i = 0; i < CFG_ADC_SAMPLES; i++) {
    acc += analogRead(CHARGE_IMON);
  }
  return (acc / (4095.0f * CFG_ADC_SAMPLES)) * 3.3f;
}

float readVmon5V()
{
  long acc = 0;
  for (int i = 0; i < CFG_ADC_SAMPLES; i++) {
    acc += analogRead(VMON_5V);
  }
  return (acc / (4095.0f * CFG_ADC_SAMPLES)) * 3.3f * 2.0f;
}

float readIPump()
{
  long acc = 0;
  for (int i = 0; i < CFG_ADC_SAMPLES; i++) {
    acc += analogRead(PUMP_IMON);
  }
  return (acc / (4095.0f * CFG_ADC_SAMPLES)) * 3.3f * 1000.0f; // [mA]
}

// Reads per-subsystem currents via the BTS7200 analog mux (DSEL_0/DSEL_1).
// Returns currents in mA using the K_ILIS/R_ILIS sense coefficients.
void readSwitchCurrents(float& opc_i, float& tsen_i, float& tdlas_i, float& bat_heater_i)
{
  digitalWrite(DEN, HIGH);
  delayMicroseconds(100);

  auto readChannel = [](int dsel0, int dsel1) -> float {
    digitalWrite(DSEL_0, dsel0);
    digitalWrite(DSEL_1, dsel1);
    delayMicroseconds(1000);
    long acc = 0;
    for (int i = 0; i < CFG_ADC_SAMPLES; i++) {
      acc += analogRead(SWITCH_IMON);
    }
    return (acc / (4095.0f * CFG_ADC_SAMPLES)) * 3300.0f * K_ILIS / R_ILIS;
  };

  opc_i        = readChannel(LOW,  LOW);
  tsen_i       = readChannel(HIGH, LOW);
  tdlas_i      = readChannel(LOW,  HIGH);
  bat_heater_i = readChannel(HIGH, HIGH);
}
