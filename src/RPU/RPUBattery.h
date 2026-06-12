/*
 * RPUBattery.h
 *
 * Battery thermal management for the RPU.  Provides sensor state updates,
 * heater permissive logic (docking voltage and critical battery voltage
 * checks), bang-bang heater control with hysteresis, and sensor powerdown.
 * Heater duty-cycle tracking is accumulated via on_ticks / total_ticks
 * counters owned by the caller.
 */
#pragma once
#include "RPUConfig.h"
#include "TSensor1WireBus.h"

void powerdownSensors();
bool batteryHeaterAllowed(float vin, float vbat, float v_crit_batt);
bool adjustHeaters(float temperature, float setpoint);
void updateTemperatures(TSensor1Bus& bat, TSensor1Bus& pcb, TSensor1Bus& pump, float& bat_temp, float& pcb_temp, float& pump_temp);
void manageHeater(float bat_temp, float setpoint,
                  float vin, float vbat, float v_crit,
                  uint32_t& on_ticks, uint32_t& total_ticks);
