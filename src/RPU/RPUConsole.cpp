#include "RPUConsole.h"
#include "RPUStatus.h"
#include "RPUConfig.h"
#include "ProfilerHardware.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <etl/string.h>
#include <etl/vector.h>

void consoleRead(RPUState& rpu_state, SensorsEnabled_t& sensorsEnabled, bool& pump_enabled)
{
  static etl::string<128> line;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.empty()) { return; }

      etl::vector<etl::string<32>, 8> tokens;
      etl::string<32> current;

      for (char ch : line) {
        if (ch == ' ' || ch == ',') {
          if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
          }
        } else if (!current.full()) {
          current.push_back(tolower((unsigned char)ch));
        }
      }
      if (!current.empty() && !tokens.full()) {
        tokens.push_back(current);
      }

      line.clear();

      if (tokens.empty()) { return; }

      if (tokens[0] == "h") {
        Serial.println("Commands:");
        Serial.println("  h        - help");
        Serial.println("  b        - reboot");
        Serial.println("  p        - toggle pump on/off");
        Serial.println("  m        - enter MEASURE state");
        Serial.println("  s        - enter STANDBY state");
        Serial.println("  w        - reset WDT trigger count in EEPROM to 0");
        Serial.println("  c <s>    - set console status print interval [s]");
        Serial.println("  r <s>    - set RPU status report interval [s]");
      } else if (tokens[0] == "p") {
        pump_enabled = !pump_enabled;
        Serial.printf("pump %s\n", pump_enabled ? "ON" : "OFF");
      } else if (tokens[0] == "b") {
        Serial.println("Rebooting via WDT");
        delay(10000);
      } else if (tokens[0] == "w") {
        uint32_t zero = 0;
        EEPROM.put(CFG_EEPROM_WDT_COUNT_ADDR, zero);
        setWDTCount(0);
        Serial.println("WDT count reset to 0");
      } else if (tokens[0] == "m") {
        sensorsEnabled.opc = sensorsEnabled.tdlas = sensorsEnabled.tsen = sensorsEnabled.rs41 = true;
        enterMeasure(rpu_state);  // enables sensorsEnabled per flags
      } else if (tokens[0] == "s") {
        enterStandby(rpu_state);
      } else if (tokens[0] == "c") {
        if (tokens.size() < 2) {
          Serial.printf("console status interval: %lus\n", getConsoleStatusInterval());
        } else {
          uint32_t s = (uint32_t)atol(tokens[1].c_str());
          setConsoleStatusInterval(s);
          Serial.printf("console status interval set to %lus\n", s);
        }
      } else if (tokens[0] == "r") {
        if (tokens.size() < 2) {
          Serial.printf("RPU status report interval: %lus\n", getRPUStatusInterval());
        } else {
          uint32_t s = (uint32_t)atol(tokens[1].c_str());
          setRPUStatusInterval(s);
          Serial.printf("RPU status report interval set to %lus\n", s);
        }
      } else {
        Serial.printf("Unknown command: %s  (send 'h' for help)\n", tokens[0].c_str());
      }

    } else if (!line.full()) {
      line.push_back(c);
    }
  }
}
