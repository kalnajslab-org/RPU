#include "RPUTDLAS.h"
#include <Arduino.h>
#include "RPUConsole.h"

static const uint8_t TDLAS_FIELD_COUNT = 12;

static bool parseTDLASString(const String& raw, TDLASData& out)
{
  char buf[128];
  raw.toCharArray(buf, sizeof(buf));

  const char* tokens[TDLAS_FIELD_COUNT];
  uint8_t count = 0;

  tokens[count++] = strtok(buf, ",");
  while (count < TDLAS_FIELD_COUNT) {
    const char* t = strtok(NULL, ",");
    if (!t) {
      Serial.printf("TDLAS parse error: expected %u fields, got %u\n", TDLAS_FIELD_COUNT, count);
      return false;
    }
    tokens[count++] = t;
  }

  if (strtok(NULL, ",") != nullptr) {
    Serial.println("TDLAS parse error: too many fields");
    return false;
  }

  if (getDebugPrintEnabled()) {
    Serial.printf("TDLAS parse: mixing_ratio=%s background=%s peak=%s ratio=%s laser_temp=%s mr_max_ratio=%s status=%s cluster_idx=%s cluster_1=%s cluster_2=%s cluster_3=%s cluster_4=%s\n",
                  tokens[0], tokens[1], tokens[2], tokens[3], tokens[4], tokens[5], tokens[6], tokens[7], tokens[8], tokens[9], tokens[10], tokens[11]);
  }
  out.mixing_ratio = atof(tokens[0]);
  out.background   = atof(tokens[1]);
  out.peak         = atof(tokens[2]);
  out.ratio        = atof(tokens[3]);
  out.laser_temp   = atof(tokens[4]);
  out.mr_max_ratio = atof(tokens[5]);
  out.status       = atoi(tokens[6]);
  out.cluster_idx  = atoi(tokens[7]);
  out.cluster_1    = atof(tokens[8]);
  out.cluster_2    = atof(tokens[9]);
  out.cluster_3    = atof(tokens[10]);
  out.cluster_4    = atof(tokens[11]);

  return true;
}

bool readTDLAS(TDLASData& data)
{
  static String buf = "";

  while (TDLAS_SERIAL.available() > 0) {
    char c = TDLAS_SERIAL.read();
    if (c == '\n') { break; }
    buf += c;
  }

  if (buf.length() == 0) { return false; }

  if (!parseTDLASString(buf, data)) {
    buf = "";
    return false;
  }

  if (getDebugPrintEnabled()) {
    Serial.printf("TDLAS: mixing_ratio=%.4f background=%.4f peak=%.4f ratio=%.6f"
                  " laser_temp=%.2fC mr_max_ratio=%.4f status=%d cluster_idx=%d"
                  " cluster_1=%.4f cluster_2=%.4f cluster_3=%.4f cluster_4=%.4f\n",
      data.mixing_ratio, data.background, data.peak, data.ratio,
      data.laser_temp, data.mr_max_ratio, data.status, data.cluster_idx,
      data.cluster_1, data.cluster_2, data.cluster_3, data.cluster_4);
  }

  buf = "";
  return true;
}
