/*
 * RPUOPC.h
 *
 * OPC (ROPC particle counter) serial reading and parsing, and the closed-loop
 * back-EMF pump speed controller for the RACHuTS profiler pump.
 *
 * readOPC()    — reads a line from OPC_SERIAL, parses it into ROPCData, prints
 *                a summary if successful.  Call every loop iteration.
 *
 * adjustPump() — proportional back-EMF controller.  Reads back-EMF during a
 *                brief PWM-off window and adjusts duty cycle toward the target
 *                voltage.  PWM is clamped to [0, 255].
 */
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "RPUConfig.h"
#include "ProfilerHardware.h"


struct PumpState {
  bool  enabled  = false;
  float setpoint = CFG_PUMP_BEMF_SETPOINT; // [V] back-EMF target
  float kp       = CFG_PUMP_KP;            // proportional gain
  int   pwm      = 0;                      // current PWM value [0–255]
  float bemf_v   = 0.0f;                   // last measured back-EMF [V]
};

// Reads one line from OPC_SERIAL, parses it into data, returns true on success.
bool readOPC(ROPCData& data);

// Runs one iteration of the pump controller.
void adjustPump(PumpState& pump, float vbat);
