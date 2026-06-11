/*
 * RPUTSEN.h
 *
 * TSEN (temperature sensor string) serial query and reading.
 *
 * readTSEN() — sends the query command to TSEN_SERIAL, then reads any
 *              response into data and parses it into tsen.  Returns true
 *              if a valid "#AAA PPPPPP TTTTTT\r" response was parsed.
 *              Call every loop iteration.
 */
#pragma once
#include <Arduino.h>
#include "ProfilerHardware.h"


bool readTSEN(String& data, TSENData& tsen);
