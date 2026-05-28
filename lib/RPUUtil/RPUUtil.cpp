#include "RPUUtil.h"
#include <imxrt.h>
#include <Arduino.h>

void readTeensyMAC(uint8_t mac[6])
{
    for (uint8_t i = 0; i < 2; i++)
        mac[i] = (HW_OCOTP_MAC1 >> ((1 - i) * 8)) & 0xFF;
    for (uint8_t i = 0; i < 4; i++)
        mac[i + 2] = (HW_OCOTP_MAC0 >> ((3 - i) * 8)) & 0xFF;
}

String getRPUIdentifier(const char* version)
{
    uint8_t mac[6];
    readTeensyMAC(mac);
    uint16_t boardId = ((uint16_t)mac[4] << 8) | mac[5];
    char buf[48];
    snprintf(buf, sizeof(buf), "%04X-%s", boardId, version);
    return String(buf);
}
