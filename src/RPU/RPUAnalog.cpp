#include "RPUAnalog.h"
#include "ProfilerHardware.h"

float readVin()
{
  long acc = 0;
  for (int i = 0; i < 32; i++) acc += analogRead(VIN_VMON);
  return (acc / (4095.0f * 32)) * 3.3f * 11.7f / 1.37f;
}

float readVBat()
{
  long acc = 0;
  for (int i = 0; i < 32; i++) acc += analogRead(BAT_VMON);
  return (acc / (4095.0f * 32)) * 3.3f * 12.49f / 2.49f;
}

float readChargeI()
{
  long acc = 0;
  for (int i = 0; i < 32; i++) acc += analogRead(CHARGE_IMON);
  return (acc / (4095.0f * 32)) * 3.3f;
}
