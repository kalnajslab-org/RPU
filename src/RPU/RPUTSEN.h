/*
 * RPUTSEN.h
 *
 * TSEN (temperature sensor string) serial query and reading.
 *
 * readTSEN() — sends the query command to TSEN_SERIAL, then reads any
 *              response into data.  Returns true if a response was read.
 *              Call every loop iteration.
 */
#pragma once
#include <Arduino.h>
#include "ProfilerHardware.h"


bool readTSEN(String& data);
