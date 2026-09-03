#include "RPUTSEN.h"
#include "RPUConsole.h"

bool readTSEN(String& data, TSENData& tsen)
{
  delay(10);

  data = "";
  while (TSEN_SERIAL.available() > 0)
    data += (char)TSEN_SERIAL.read();

  // Always re-issue the query, even if this call got nothing back — otherwise
  // a single missed/late response permanently stalls the request/response
  // ping-pong, since nothing else would ever prompt the sensor again.
  TSEN_SERIAL.print("*01A?\r");
  TSEN_SERIAL.flush();

  if (data.length() == 0) {
    if (getDebugPrintEnabled()) {
      Serial.printf("TSEN: No Data Received\n");
    }
    return false; }

  // Expected response: "#AAA PPPPPP TTTTTT\r" (19 chars), all fields hex.
  if (data.length() != 19 || data[0] != '#') { 
    if (getDebugPrintEnabled()) {
      Serial.printf("TSEN: Data Corrupted\n");
    }
    return false; }

  tsen.airt_raw  = (uint16_t)strtoul(data.substring(1, 4).c_str(),  NULL, 16);
  tsen.ptemp_raw = (uint32_t)strtoul(data.substring(5, 11).c_str(), NULL, 16);
  tsen.pres_raw  = (uint32_t)strtoul(data.substring(12, 18).c_str(), NULL, 16);

  if (getDebugPrintEnabled()) {
    Serial.printf("TSEN: airt_raw=%u ptemp_raw=%lu pres_raw=%lu\n",
      tsen.airt_raw, tsen.ptemp_raw, tsen.pres_raw);
  }

  return true;
}
