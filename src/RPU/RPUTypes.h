/*
 * RPUTypes.h
 *
 * Shared type definitions for the RPU firmware.  Defines the top-level
 * RPUState enum and the inline state-transition helpers enterStandby() and
 * enterMeasure(), which are called from multiple compilation units.
 */
#pragma once
#include <Arduino.h>

enum class RPUState { STANDBY, MEASURE, ERROR };

// ---------------------------------------------------------------------------
// Sensor enable flags — set when powering up for MEASURE, cleared on powerdown
// ---------------------------------------------------------------------------
struct SensorsEnabled_t {
  bool opc   = false;
  bool tdlas = false;
  bool tsen  = false;
  bool rs41  = false;
};

void enterStandby(RPUState& state);
void enterMeasure(RPUState& state);
void enterError(RPUState& state);

// True once the RTC has been set, manually or by GPS (see RPU.cpp). Used to
// decide whether the RTC is a valid fallback epoch-time source.
bool isRTCSet();

// True once a valid GPS fix has disciplined the RTC (see RPU.cpp). Once
// true, manual RTC sets (console 't' command) are refused, since a GPS-
// verified time should not be clobbered by operator entry.
bool isRTCSetByGPS();

// Marks the RTC as set by the console 't' command (manual entry).
void setRTCSetManually();
