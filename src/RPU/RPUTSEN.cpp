#include "RPUTSEN.h"
#include "RPUConsole.h"

bool readTSEN(String& data, TSENData& tsen)
{
  delay(10);

  data = "";
  while (TSEN_SERIAL.available() > 0)
    data += (char)TSEN_SERIAL.read();

  if (data.length() == 0) { return false; }

  TSEN_SERIAL.print("*01A?\r");
  TSEN_SERIAL.flush();

  // Expected response: "#AAA PPPPPP TTTTTT\r" (19 chars), all fields hex.
  if (data.length() != 19 || data[0] != '#') { return false; }

  tsen.airt_raw  = (uint16_t)strtoul(data.substring(1, 4).c_str(),  NULL, 16);
  tsen.ptemp_raw = (uint32_t)strtoul(data.substring(5, 11).c_str(), NULL, 16);
  tsen.pres_raw  = (uint32_t)strtoul(data.substring(12, 18).c_str(), NULL, 16);

  if (getDebugJsonEnabled()) {
    Serial.printf("TSEN: airt_raw=%u ptemp_raw=%lu pres_raw=%lu\n",
      tsen.airt_raw, tsen.ptemp_raw, tsen.pres_raw);
  }

  return true;
}
