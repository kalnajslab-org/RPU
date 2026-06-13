/*
 * RPUConfig.h
 *
 * Default configuration constants for the RPU firmware.  All values here
 * represent compile-time defaults; the runtime variables initialised from
 * these can be updated via the console or docking-connector commands.
 */
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Battery thresholds
// ---------------------------------------------------------------------------
constexpr float    CFG_V_CRIT_BATT        = 10.0f;        // [V]  heater disabled below this
constexpr float    CFG_V_LOW_BATT         = 11.0f;        // [V]  switch to STANDBY if undocked below this
constexpr float    CFG_V_DOCKED           = 13.0f;        // [V]  V_IN threshold indicating docked

// ---------------------------------------------------------------------------
// Battery heater
// ---------------------------------------------------------------------------
constexpr float    CFG_BATTERY_T_SETPOINT = 20.0f;        // [°C] battery heater target temperature
constexpr float    CFG_HEATER_HYSTERESIS  =  0.5f;        // [°C] bang-bang dead-band half-width

// ---------------------------------------------------------------------------
// Report intervals
// ---------------------------------------------------------------------------
constexpr uint32_t CFG_RPU_STATUS_INTERVAL_S     = 30UL * 60UL; // [s] LoRa/docking status report
constexpr uint32_t CFG_CONSOLE_REPORT_INTERVAL_S = 0UL;         // [s] USB serial status print (0 = off)

// ---------------------------------------------------------------------------
// Pump back-EMF controller
// ---------------------------------------------------------------------------
constexpr float CFG_PUMP_BEMF_SETPOINT = 9.0f;  // [V] target back-EMF for large pumps
constexpr float CFG_PUMP_KP            = 30.0f; // proportional gain

// ---------------------------------------------------------------------------
// ADC averaging
// ---------------------------------------------------------------------------
constexpr int CFG_ADC_SAMPLES = 100; // samples averaged per analog read

// ---------------------------------------------------------------------------
// Watchdog
// ---------------------------------------------------------------------------
constexpr uint32_t CFG_WDT_TIMEOUT_S = 10; // [s] hardware watchdog timeout

// ---------------------------------------------------------------------------
// EEPROM layout
// ---------------------------------------------------------------------------
constexpr int CFG_EEPROM_WDT_COUNT_ADDR = 0;  // uint32_t: lifetime watchdog reset count

// ---------------------------------------------------------------------------
// MEASURE defaults
// ---------------------------------------------------------------------------
constexpr int32_t  CFG_MEASURE_DURATION   = 0;            // [s] 0 = run until commanded to STANDBY
constexpr int32_t  CFG_MEASURE_RATE       = 1;            // [s] measurement interval
