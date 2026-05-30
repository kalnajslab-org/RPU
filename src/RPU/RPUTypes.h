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
