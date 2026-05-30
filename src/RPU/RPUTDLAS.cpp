#include "RPUTDLAS.h"
#include <Arduino.h>

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

  out.mr_avg  = atof(tokens[0]);
  out.bkg     = atof(tokens[1]);
  out.peak    = atof(tokens[2]);
  out.ratio   = atof(tokens[3]);
  out.batt    = atof(tokens[4]);
  out.therm_1 = atof(tokens[5]);
  out.therm_2 = atof(tokens[6]);
  out.indx    = atof(tokens[7]);
  out.spec_1  = atof(tokens[8]);
  out.spec_2  = atof(tokens[9]);
  out.spec_3  = atof(tokens[10]);
  out.spec_4  = atof(tokens[11]);

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

  Serial.printf("TDLAS: mr_avg=%.4f bkg=%.4f peak=%.4f ratio=%.6f batt=%.3fV"
                " therm_1=%.2fC therm_2=%.2f Idx=%d"
                " spec_1=%.4f spec_2=%.4f spec_3=%.4f spec_4=%.4f\n",
    data.mr_avg, data.bkg, data.peak, data.ratio,
    data.batt, data.therm_1, data.therm_2, data.indx,
    data.spec_1, data.spec_2, data.spec_3, data.spec_4);

  buf = "";
  return true;
}
