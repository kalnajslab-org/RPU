/*
 * RPUAnalog.h
 *
 * ADC voltage and current readings for the RPU hardware.
 * All functions average 32 samples and apply board-specific scaling constants
 * to return calibrated engineering-unit values.
 */
#pragma once

float readVin();
float readVBat();
float readChargeI();
