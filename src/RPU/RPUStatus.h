/*
 * RPUStatus.h
 *
 * Periodic status reporting for the RPU.  Owns the report and console timers
 * internally; callers use setters to change intervals.
 *
 * sendStatus() — transmits a JSON status packet via LoRa and the docking
 *   connector at the configured interval.  The packet includes board ID,
 *   firmware version, state, voltages, temperatures, heater duty cycle, and
 *   GPS fix.
 *
 * consoleReport() — prints a one-line human-readable status summary to the
 *   USB serial console at the configured interval.
 */
#pragma once
#include <Arduino.h>
#include <TinyGPS++.h>
#include "RPUTypes.h"

// Call once in setup() before wdt.begin() to record whether the last reset
// was caused by the watchdog.  Included in every sendStatus() JSON payload.
void setWDTCount(uint32_t count);

// ---------------------------------------------------------------------------
// Interval setters / getters
// ---------------------------------------------------------------------------
void     setRPUStatusInterval(uint32_t seconds);
uint32_t getRPUStatusInterval();
void     setConsoleStatusInterval(uint32_t seconds);
uint32_t getConsoleStatusInterval();

// ---------------------------------------------------------------------------
// Reporting functions
// ---------------------------------------------------------------------------
void sendStatus(
    uint16_t board_id, const char* ver, RPUState state,
    float vin, float v_5V, float vbat, float bat_t, float chg_i, float pcb_t,
    float pump_i, float opc_i, float tsen_i, float tdlas_i, float heater_i,
    uint32_t& on_ticks, uint32_t& total_ticks,
    TinyGPSPlus& gps);

void consoleReport(
    RPUState state, float elapsed_s,
    float vbat, float vin, bool heater_on);
