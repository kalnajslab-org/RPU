#include "RPUStatus.h"
#include "RPUConfig.h"
#include "RPUUtil.h"
#include "ProfilerHardware.h"
#include <LoRa.h>

// ---------------------------------------------------------------------------
// Module-private timers and intervals
// ---------------------------------------------------------------------------
static elapsedSeconds rpu_report_timer;
static elapsedSeconds console_report_timer;
static uint32_t       wdt_count = 0;
static uint32_t rpu_report_interval_s      = CFG_RPU_REPORT_INTERVAL_S;
static uint32_t console_report_interval_s  = CFG_CONSOLE_REPORT_INTERVAL_S;

// ---------------------------------------------------------------------------
// Interval setters / getters
// ---------------------------------------------------------------------------
void setWDTCount(uint32_t count)
{
  wdt_count = count;
}

void setRPUReportInterval(uint32_t seconds)
{
  rpu_report_interval_s = seconds;
}

uint32_t getRPUReportInterval()
{
  return rpu_report_interval_s;
}

void setConsoleStatusInterval(uint32_t seconds)
{
  console_report_interval_s = seconds;
}

uint32_t getConsoleStatusInterval()
{
  return console_report_interval_s;
}

// ---------------------------------------------------------------------------
// Reporting functions
// ---------------------------------------------------------------------------
static const char* stateStr(RPUState state)
{
  switch (state)
  {
    case RPUState::STANDBY: return "STANDBY";
    case RPUState::MEASURE: return "MEASURE";
    case RPUState::ERROR:   return "ERROR";
    default:                return "UNKNOWN";
  }
}
void rpuReport(
    uint16_t board_id, const char* ver, RPUState state,
    float vin, float v_5V, float vbat, float bat_t, float chg_i, float pcb_t,
    float pump_i, float opc_i, float tsen_i, float tdlas_i, float heater_i,
    uint32_t& on_ticks, uint32_t& total_ticks,
    TinyGPSPlus& gps)
{
  static bool first_call = true;
  if (!first_call && rpu_report_timer < rpu_report_interval_s) {
    return;
  }
  first_call = false;
  rpu_report_timer = 0;

  uint8_t heater_duty = (total_ticks > 0)
                        ? (uint8_t)(on_ticks * 100 / total_ticks)
                        : 0;
  on_ticks    = 0;
  total_ticks = 0;

  char buf[350];
  int n = snprintf(buf, sizeof(buf),
    "{\"id\":\"%04X\",\"ver\":\"%s\",\"state\":\"%s\",\"wdt_n\":%lu,\"vin\":%.1f,\"v5\":%.1f,\"bat_v\":%.1f,\"bat_duty\":%d,\"chg_i\":%.1f,\"bat_t\":%.1f,\"pcb_t\":%.1f,\"pump_i\":%.1f,\"opc_i\":%.1f,\"tsen_i\":%.1f,\"tdlas_i\":%.1f,\"heater_i\":%.1f,\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"sats\":%lu}",
    board_id, ver,
    stateStr(state),
    wdt_count,
    vin, v_5V, vbat, heater_duty, chg_i, bat_t, pcb_t,
    pump_i, opc_i, tsen_i, tdlas_i, heater_i,
    gps.location.lat(), gps.location.lng(),
    gps.altitude.meters(), gps.satellites.value());

  if (n >= (int)sizeof(buf)) {
    Serial.println("WARNING: RPU status JSON truncated");
  }

  DOCK_SERIAL.println(buf);

  LoRa.beginPacket();
  LoRa.print(buf);
  LoRa.endPacket();

  Serial.println(buf);
}

void consoleReport(
    RPUState state, float elapsed_s,
    float vbat, float vin, bool heater_on)
{
  static bool first_call = true;
  if (!first_call && console_report_timer < console_report_interval_s) {
    return;
  }
  first_call = false;
  console_report_timer = 0;
  Serial.printf("state=%s elapsed=%.1fs bat_v=%.2fV vin=%.2fV bat_heater=%s\n",
    stateStr(state),
    elapsed_s, vbat, vin,
    heater_on ? "ON" : "OFF");
}
