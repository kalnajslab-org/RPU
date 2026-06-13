#include "RPUStatus.h"
#include "RPUConfig.h"
#include "RPUcomm.h"
#include "RPUUtil.h"
#include "RPURecordBuffer.h"
#include <LoRa.h>

// ---------------------------------------------------------------------------
// Module-private timers and intervals
// ---------------------------------------------------------------------------
static elapsedSeconds rpu_status_timer;
static elapsedSeconds console_report_timer;
static uint32_t       wdt_count = 0;
static uint32_t rpu_status_interval_s      = CFG_RPU_STATUS_INTERVAL_S;
static uint32_t console_report_interval_s  = CFG_CONSOLE_REPORT_INTERVAL_S;

// ---------------------------------------------------------------------------
// Interval setters / getters
// ---------------------------------------------------------------------------
void setWDTCount(uint32_t count)
{
  wdt_count = count;
}

void setRPUStatusInterval(uint32_t seconds)
{
  rpu_status_interval_s = seconds;
}

uint32_t getRPUStatusInterval()
{
  return rpu_status_interval_s;
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
static RPUPacket buildStatusPacket(
    uint16_t board_id, const char* ver, RPUState state,
    float vin, float v_5V, float vbat, float bat_t, float chg_i, float pcb_t,
    float pump_i, float opc_i, float tsen_i, float tdlas_i, float heater_i,
    uint8_t heater_duty,
    TinyGPSPlus& gps)
{
  RPUPacket pkt;
  pkt.setBoardId(board_id);
  pkt.setVer(ver);
  pkt.setState((uint8_t)state);
  pkt.setWdtCount((uint8_t)wdt_count);
  pkt.setBufferedRecords((uint8_t)rpu_records.size());
  pkt.setVin(vin);
  pkt.setV5V(v_5V);
  pkt.setBatV(vbat);
  pkt.setHeaterDuty(heater_duty);
  pkt.setChgI(chg_i);
  pkt.setBatT(bat_t);
  pkt.setPcbT(pcb_t);
  pkt.setPumpI(pump_i);
  pkt.setOpcI(opc_i);
  pkt.setTsenI(tsen_i);
  pkt.setTdlasI(tdlas_i);
  pkt.setHeaterI(heater_i);
  pkt.setLat(gps.location.lat());
  pkt.setLon(gps.location.lng());
  pkt.setAlt(gps.altitude.meters());
  pkt.setSats((uint8_t)gps.satellites.value());
  pkt.setGpsDate(gps.date.value());
  pkt.setGpsTime(gps.time.value());
  return pkt;
}

void sendStatus(
    uint16_t board_id, const char* ver, RPUState state,
    float vin, float v_5V, float vbat, float bat_t, float chg_i, float pcb_t,
    float pump_i, float opc_i, float tsen_i, float tdlas_i, float heater_i,
    uint32_t& on_ticks, uint32_t& total_ticks,
    TinyGPSPlus& gps)
{
  static bool first_call = true;
  if (!first_call && rpu_status_timer < rpu_status_interval_s) {
    return;
  }
  first_call = false;
  rpu_status_timer = 0;

  uint8_t heater_duty = (total_ticks > 0)
                        ? (uint8_t)(on_ticks * 100 / total_ticks)
                        : 0;
  on_ticks    = 0;
  total_ticks = 0;

  // Build bit-packed binary payload for LoRa
  RPUPacket pkt = buildStatusPacket(board_id, ver, state,
      vin, v_5V, vbat, bat_t, chg_i, pcb_t,
      pump_i, opc_i, tsen_i, tdlas_i, heater_i,
      heater_duty, gps);

  uint8_t pkt_buf[RPU_PKT_BYTES];
  pkt.encode(pkt_buf, sizeof(pkt_buf));

  LoRa.beginPacket();
  LoRa.write(pkt_buf, RPU_PKT_BYTES);
  LoRa.endPacket();

  // Decode the packet just sent and report it as JSON
  RPUPacket decoded;
  decoded.decode(pkt_buf, sizeof(pkt_buf));

  String json = decoded.toJSON();

  Serial.println(json);
}

String getStatusJSON(
    uint16_t board_id, const char* ver, RPUState state,
    float vin, float v_5V, float vbat, float bat_t, float chg_i, float pcb_t,
    float pump_i, float opc_i, float tsen_i, float tdlas_i, float heater_i,
    uint32_t on_ticks, uint32_t total_ticks,
    TinyGPSPlus& gps)
{
  uint8_t heater_duty = (total_ticks > 0)
                        ? (uint8_t)(on_ticks * 100 / total_ticks)
                        : 0;

  RPUPacket pkt = buildStatusPacket(board_id, ver, state,
      vin, v_5V, vbat, bat_t, chg_i, pcb_t,
      pump_i, opc_i, tsen_i, tdlas_i, heater_i,
      heater_duty, gps);

  return pkt.toJSON();
}

void consoleReport(
    RPUState state, float elapsed_s,
    float vbat, float vin, bool heater_on)
{
  if (console_report_interval_s == 0) {
    return;
  }

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
