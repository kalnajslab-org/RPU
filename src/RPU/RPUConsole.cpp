#include "RPUConsole.h"
#include "RPUStatus.h"
#include <Arduino.h>
#include <etl/string.h>
#include <etl/vector.h>

void consoleRead(RPUState& rpu_state)
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

      for (uint8_t i = 0; i < tokens.size(); i++) {
        Serial.printf("  token[%u]: %s\n", i, tokens[i].c_str());
      }

      if (tokens[0] == "h") {
        Serial.println("Commands:");
        Serial.println("  h        - help");
        Serial.println("  m        - enter MEASURE state");
        Serial.println("  s        - enter STANDBY state");
        Serial.println("  c <s>    - set console status print interval [s]");
        Serial.println("  r <s>    - set RPU status report interval [s]");
      } else if (tokens[0] == "m") {
        enterMeasure(rpu_state);
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
          Serial.printf("RPU status report interval: %lus\n", getRPUReportInterval());
        } else {
          uint32_t s = (uint32_t)atol(tokens[1].c_str());
          setRPUReportInterval(s);
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
