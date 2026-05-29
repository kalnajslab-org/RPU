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

inline void enterStandby(RPUState& state)
{
  state = RPUState::STANDBY;
  Serial.println("Entering STANDBY");
}

inline void enterMeasure(RPUState& state)
{
  state = RPUState::MEASURE;
  Serial.println("Entering MEASURE");
}

inline void enterError(RPUState& state)
{
  state = RPUState::ERROR;
  Serial.println("ERROR: entering ERROR state");
}
