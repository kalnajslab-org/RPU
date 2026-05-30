/*
 * RPUTDLAS.h
 *
 * TDLAS (Tunable Diode Laser Absorption Spectrometer) serial reading and
 * parsing.  readTDLAS() accumulates characters from TDLAS_SERIAL into a line
 * buffer, parses a completed line into TDLASData, prints a summary, and
 * returns true on success.  Call every loop iteration.
 */
#pragma once
#include <Arduino.h>
#include "ProfilerHardware.h"


bool readTDLAS(TDLASData& data);
