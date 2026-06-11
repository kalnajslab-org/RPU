#include "RPUTSEN.h"

bool readTSEN(String& data)
{
  delay(10);

  data = "";
  while (TSEN_SERIAL.available() > 0)
    data += (char)TSEN_SERIAL.read();

  if (data.length() == 0) { return false; }

  TSEN_SERIAL.print("*01A?\r");
  TSEN_SERIAL.flush();

  return true;
}
