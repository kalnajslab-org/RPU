/*
 * RPUStatus.h
 *
 * Periodic status reporting for the RPU.  Owns the report and console timers
 * internally; callers use setters to change intervals.
 *
 * rpuReport() — transmits a JSON status packet via LoRa and the docking
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

// ---------------------------------------------------------------------------
// Interval setters / getters
// ---------------------------------------------------------------------------
void     setRPUReportInterval(uint32_t seconds);
uint32_t getRPUReportInterval();
void     setConsoleStatusInterval(uint32_t seconds);
uint32_t getConsoleStatusInterval();

// ---------------------------------------------------------------------------
// Reporting functions
// ---------------------------------------------------------------------------
void rpuReport(
    uint16_t board_id, const char* ver, RPUState state,
    float vbat, float bat_t, float chg_i, float pcb_t,
    uint32_t& on_ticks, uint32_t& total_ticks,
    TinyGPSPlus& gps);

void consoleReport(
    RPUState state, float elapsed_s,
    float vbat, float vin, bool heater_on);
