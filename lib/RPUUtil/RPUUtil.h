#pragma once
#include <Arduino.h>
#include <stdint.h>

class elapsedSeconds {
  elapsedMillis _ms;
public:
  operator uint32_t()            const { return _ms / 1000; }
  elapsedSeconds& operator=(uint32_t s) { _ms = s * 1000; return *this; }
};

// Reads the 6-byte Ethernet MAC address burned into the Teensy 4.x fuse
// registers (HW_OCOTP_MAC0 / HW_OCOTP_MAC1) into mac[0..5].
void readTeensyMAC(uint8_t mac[6]);

// Returns a board+firmware identifier of the form "<MAC_LOW2>-<version>",
// e.g. "C221-v1.2.3-4-gabcdef1".  Pass RPU_VERSION from rpu_version.h.
String getRPUIdentifier(const char* version);
