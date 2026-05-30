#include "RPUBattery.h"
#include "RPUConfig.h"
#include "ProfilerHardware.h"

void powerdownSensors()
{
  digitalWrite(OPC_ENABLE,     LOW);
  digitalWrite(TSEN_ENABLE,    LOW);
  digitalWrite(TDLAS_ENABLE,   LOW);
  digitalWrite(RS41_ENABLE,    LOW);
  digitalWrite(BATTERY_HEATER, LOW);
  analogWrite(PUMP_PWM,        0);
}

bool batteryHeaterAllowed(float vin, float vbat, float v_crit_batt)
{
  return (vin > CFG_V_DOCKED) && (vbat >= v_crit_batt);
}

// Bang-bang controller with 0.5°C hysteresis. Returns true if heater is ON.
bool adjustHeaters(float temperature, float setpoint)
{
  bool heat = (temperature < setpoint - 0.5f);
  digitalWrite(BATTERY_HEATER, heat ? HIGH : LOW);
  return heat;
}

void updateTemperatures(TSensor1Bus& bat, TSensor1Bus& pcb, float& bat_temp, float& pcb_temp)
{
  bat.ManageState(bat_temp);
  pcb.ManageState(pcb_temp);
}

void manageHeater(float bat_temp, float setpoint,
                  float vin, float vbat, float v_crit,
                  uint32_t& on_ticks, uint32_t& total_ticks)
{
  total_ticks++;
  if (batteryHeaterAllowed(vin, vbat, v_crit)) {
    if (adjustHeaters(bat_temp, setpoint)) {
      on_ticks++;
    }
  }
}
