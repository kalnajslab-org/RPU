#include "RPUBattery.h"
#include "RPUConfig.h"
#include "ProfilerHardware.h"

// When powering a sensor off, its UART is stopped and both pins floated
// before the rail is cut; otherwise the idle-high TX line and the RX pull-up
// that begin() configures would back-power the sensor through its I/O
// protection diodes. Idempotent, so it is safe to call in either state.
static void setSensorPower(uint8_t enable_pin, HardwareSerialIMXRT& port,
                           uint8_t tx_pin, uint8_t rx_pin,
                           uint32_t baud, uint16_t format, bool on)
{
  if (on) {
    digitalWrite(enable_pin, HIGH);
    port.begin(baud, format);
  } else {
    port.end();
    pinMode(tx_pin, INPUT);
    pinMode(rx_pin, INPUT);
    digitalWrite(enable_pin, LOW);
  }
}

void setSensorsPower(const SensorsEnabled_t& en)
{
  setSensorPower(OPC_ENABLE,   OPC_SERIAL,   OPC_TX_PIN,   OPC_RX_PIN,   OPC_BAUD,   OPC_SERIAL_FORMAT, en.opc);
  setSensorPower(TSEN_ENABLE,  TSEN_SERIAL,  TSEN_TX_PIN,  TSEN_RX_PIN,  TSEN_BAUD,  SERIAL_8N1,        en.tsen);
  setSensorPower(TDLAS_ENABLE, TDLAS_SERIAL, TDLAS_TX_PIN, TDLAS_RX_PIN, TDLAS_BAUD, SERIAL_8N1,        en.tdlas);
  setSensorPower(RS41_ENABLE,  RS41_SERIAL,  RS41_TX_PIN,  RS41_RX_PIN,  RS41_BAUD,  SERIAL_8N1,        en.rs41);
}

void powerdownSensors()
{
  setSensorsPower(SensorsEnabled_t{});
  digitalWrite(BATTERY_HEATER, LOW);
  analogWrite(PUMP_PWM,        0);
}

bool batteryHeaterAllowed(float vin, float vbat, float v_crit_batt)
{
  return (vin > CFG_V_DOCKED) && (vbat >= v_crit_batt);
}

// Bang-bang controller with 0.5°C hysteresis. Returns true if heater is ON.
bool adjustHeaters(float temperature, float setpoint)
{
  bool heat = (temperature < setpoint - 0.5f);
  digitalWrite(BATTERY_HEATER, heat ? HIGH : LOW);
  return heat;
}

void updateTemperatures(TSensor1Bus& bat, TSensor1Bus& pcb, TSensor1Bus& pump, float& bat_temp, float& pcb_temp, float& pump_temp)
{
  bat.ManageState(bat_temp);
  pcb.ManageState(pcb_temp);
  pump.ManageState(pump_temp);
}

void manageHeater(float bat_temp, float setpoint,
                  float vin, float vbat, float v_crit,
                  uint32_t& on_ticks, uint32_t& total_ticks)
{
  total_ticks++;
  if (batteryHeaterAllowed(vin, vbat, v_crit)) {
    if (adjustHeaters(bat_temp, setpoint)) {
      on_ticks++;
    }
  }
}
