/*
* Profiler.h
*  Author: Lars Kalnajs
*  Created: April 2025

This header file defines the hardware interface for the RACHuTS profiler board based on the Teensy 4.1
*/


#include "TSensor1WireBus.h"
#include <TinyGPS++.h>
#include <SD.h>
#include <SPI.h>
#include <LoRa.h> // Docs for LoRa are at https://github.com/sandeepmistry/arduino-LoRa.git


// LoRa Module
#define PU_LORA_CS         10
#define PU_LORA_MOSI       11
#define PU_LORA_MISO       12
#define PU_LORA_SCK        13

//LoRa Settings
#define LORA_FREQ           868E6
#define LORA_BW             250E3
#define LORA_SF             9
#define LORA_POWER          19

//BTS7200-4EPA Coefficients
#define K_ILIS 670.0  //Current sense ratio from spec sheet
#define R_ILIS 1200.0   //Sense current to voltage resistro value

/* GPS */
#define GPS_RESET 28
#define GPS_EXTINT 31
#define GPS_SERIAL Serial3

/*DIO*/
#define CHARGER_SHTDWN 34 //High = charger off
#define OPC_ENABLE 2   //High = OPC on
#define TSEN_ENABLE 3   //High = TSEN on
#define RS41_ENABLE 41   //High = RSS421 on
#define TDLAS_ENABLE 4   //High = TDLAS on
#define BATTERY_HEATER 5 //High = battery heater on
#define ONE_WIRE_1  32 //One wire temperature sensor on battery
#define ONE_WIRE_2  33 //One wire temperature sensor remote
#define ONE_WIRE_3  39 //One wire temperature sensor on PCB
#define RS232_FORCEON 29 //RS232 Driver stay on
#define RS232_FORCEOFF 30 //RS232 Driver stay off
#define PULSE_LED 35 //Heartbeat LED
#define DSEL_0 36 //BTS72004 Diagnostic Select 0
#define DSEL_1 37 //BTS72004 Diagnostic Select 1
#define DEN 6 //BTS72004 Diagnotstic Enable
#define PUMP_PWM 9 //Pump power

/*Serial Ports*/
#define OPC_SERIAL Serial2
#define TSEN_SERIAL Serial5
#define TDLAS_SERIAL Serial4
#define RS41_SERIAL Serial6
#define GONDOLA_SERIAL Serial1

/* Serial Port Pins*/
#define TDLAS_TX_PIN   17
#define TDLAS_RX_PIN   16
#define RS41_TX_PIN    24
#define RS41_RX_PIN    25
#define OPC_TX_PIN      8
#define OPC_RX_PIN      7

// ---------------------------------------------------------------------------
// ROPC (OPC) parsed data structure
// Fields match the 10-field CSV output:
//   ROPC_time,d300,d500,d700,d1000,d2000,d2500,d3000,d5000,alarm
// ---------------------------------------------------------------------------
struct ROPCData {
  uint32_t ROPC_time; // instrument timestamp [ms]
  uint16_t d300;      // particle count  >0.3 µm
  uint16_t d500;      // particle count  >0.5 µm
  uint16_t d700;      // particle count  >0.7 µm
  uint16_t d1000;     // particle count  >1.0 µm
  uint16_t d2000;     // particle count  >2.0 µm
  uint16_t d2500;     // particle count  >2.5 µm
  uint16_t d3000;     // particle count  >3.0 µm
  uint16_t d5000;     // particle count  >5.0 µm
  uint8_t  alarm;     // instrument alarm flag
};

// ---------------------------------------------------------------------------
// TDLAS parsed data structure
// Fields match the 12-field CSV output:
//   mr_avg,bkg,peak,ratio,batt,therm_1,therm_2,indx,spec_1,spec_2,spec_3,spec_4
// ---------------------------------------------------------------------------
struct TDLASData {
  float mr_avg;  // 1 s average mixing ratio
  float bkg;     // background signal
  float peak;    // absorption peak
  float ratio;   // peak/background ratio
  float batt;    // battery voltage [V]
  float therm_1; // thermistor 1 temperature
  float therm_2; // thermistor 2 temperature
  int8_t indx;    // TDLAS index (0-255)
  float spec_1;   // TDLAS spectrum value 1
  float spec_2;   // TDLAS spectrum value 2
  float spec_3;   // TDLAS spectrum value 3
  float spec_4;   // TDLAS spectrum value 4
};

/*Amalog Channels*/
#define PUMP_IMON 18 //Pump current monitor (A4)
#define BAT_VMON 19 //Battery voltage monitor (A5)
#define CHARGE_IMON 22 //Charger current monitor (A8)
#define VIN_VMON 23 //Vin voltage monitor (A9)
#define VMON_3V3 26 //3.3V voltage monitor (A12)
#define VMON_5V 27 //5V voltage monitor (A13)
#define SWITCH_IMON 38 //Switch current monitor (A14)
#define CHARGE_STATUS 39 //Charger status (A15)
#define PUMP_BEMF 40 //Pump BEMF (A16)

