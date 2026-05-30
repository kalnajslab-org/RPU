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
float readVmon5V();
float readIPump();
void  readSwitchCurrents(float& opc_i, float& tsen_i, float& tdlas_i, float& bat_heater_i);
