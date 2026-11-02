# Bug: battery heater stays on after V_IN is lost (STANDBY)

**Status:** recovered in flight with TC 184 (2026-11-02); fix on branch `fix-battery-heater-stuck-on`, not yet bench tested. Found from flight data (RACHUTSREPORT_2026-11-02.csv)
**Component:** RPU firmware, `src/RPU/RPUBattery.cpp` (`manageHeater`), called from `tickStandby()` in `src/RPU/RPU.cpp`
**Severity:** high. The battery heated to 60 C at 18 km altitude and was drained with no charging.

## Summary

In STANDBY the battery heater is only supposed to run while docked
(`V_IN > CFG_V_DOCKED`, 13.0 V). `manageHeater()` enforces that by *skipping*
the thermostat when the heater is not permitted. It never drives the
`BATTERY_HEATER` pin low in that case. If `V_IN` disappears while the pin is
high, the pin stays high and the heater runs with no thermostat control.

## Code

`src/RPU/RPUBattery.cpp`:

```cpp
bool batteryHeaterAllowed(float vin, float vbat, float v_crit_batt)
{
  return (vin > CFG_V_DOCKED) && (vbat >= v_crit_batt);
}

void manageHeater(float bat_temp, float setpoint,
                  float vin, float vbat, float v_crit,
                  uint32_t& on_ticks, uint32_t& total_ticks)
{
  total_ticks++;
  if (batteryHeaterAllowed(vin, vbat, v_crit)) {
    if (adjustHeaters(bat_temp, setpoint)) {   // only place the pin is written
      on_ticks++;
    }
  }
  // no else: pin is left in whatever state it was in
}
```

`adjustHeaters()` is the only thing that writes the pin in STANDBY, and it is
only reached through the `if` above. The ops doc
(`docs/RACHuTS_Profiler_2026_Description_of_Operations.md`) says the heater is
disabled when the battery is below `V_CRIT_BATT` "until V_IN > 13.0V", and that
it is enabled when docked. The code does not turn it off when the permissive
becomes false.

The pin is otherwise cleared only at boot (`RPU.cpp:753`) and by
`powerdownSensors()` on entry to STANDBY (`enterStandby()`). Neither runs when
`V_IN` is lost while already in STANDBY.

## Evidence from flight data

Report period 2026-11-02 00:04 to 03:39 UTC, state STANDBY, ~18 km, 6 WDT resets
(constant, no new resets).

| Time (UTC) | vin | bat_duty | chg_i | bat_t | pcb_t | heater_i (mA) | Notes |
|---|---|---|---|---|---|---|---|
| 00:24 - 00:44 | ~15.2 - 15.6 | varies | 0.2 - 1.6 | 19.0 - 19.5 | 7 - 8.5 | 0 or ~640 | Heater cycles normally around setpoint 20 C (on at 19.0, off at 19.5) |
| 00:49 | **0.0** | 19 | 0.0 | 20.5 | 7.0 | **627** | V_IN lost; heater current stays on |
| 01:04 | 0.0 | 0 | 0.0 | 28.5 | 6.5 | 616 | |
| 02:04 | 0.0 | 0 | 0.0 | 45.0 | 14.5 | 601 | |
| 03:39 | 0.0 | 0 | 0.0 | **60.0** | 28.5 | 587 | still heating |

- `heater_i` never returns to 0 after V_IN is lost. It decays slowly
  (627 to 587 mA) as `bat_v` sags from 12.3 V to 11.9 V.
- `bat_t` rises about 1 C per 5 min, slowing as the enclosure warms. `pcb_t`
  rises with it, which confirms real heating and not a sensor fault.
- All sensors and the pump are off (their currents are 0), so the heater
  (~7 W) is the only significant load.
- At 18 km there is almost no convective cooling, so temperature keeps climbing
  well past the 20 C setpoint.
- The battery is discharging with no charge current while the heater runs.

## In-flight recovery (current firmware)

The firmware now flying already has a path that clears the pin: a
go-standby command. `enterStandby()` calls `powerdownSensors()`, which drives
`BATTERY_HEATER` low (`RPUBattery.cpp:35`). The `RPU_GO_STANDBY` handler does
not check the current state, so it works even though the RPU is already in
STANDBY. With V_IN still at 0, `manageHeater()` keeps skipping the thermostat
and nothing sets the pin high again, so the heater stays off. The same bug
that left it stuck on now holds it off. The command needs only the dock serial
link, not dock power (the ops sequence already relies on `SEND_STATUS` working
before dock power is turned on). It also resends the setpoint stored in the
RACHuTS EEPROM (`rpu_bat_temp`).

Sequence:

1. **TC 184 (RPUGOSTANDBY)** to turn the heater off.
2. **TC 143 (GETPUSTATUS)** to confirm: `heater_i` should be 0, the heater
   duty should stop rising, and `bat_t` should start falling. A fresh status
   reply also confirms that dock commands are reaching the RPU.
3. **Find out why V_IN dropped at 00:49** before turning dock power back on.
   Did RACHuTS switch dock power off (TC 145 or an automatic power-off), or did
   the dock connection fail?
   - If dock power was switched off, **TC 144 (PUPOWERON)** brings V_IN back.
     The thermostat then takes control again: at 60 C it holds the heater off
     until the battery cools below 19.5 C (with a 20 C setpoint). The charger
     also starts recharging the battery, which matters at 11.9 V and falling.
   - TC 144 alone would also clear the pin within one tick, but only if dock
     power is the cause. Send TC 184 first either way.
4. **Last resort: TC 183 (RPURESET)**, if TC 184 gets no ack. `setup()` drives
   the pin low and then enters STANDBY. It adds a watchdog reset to the count
   and resets the RPU's setpoint to `CFG_BATTERY_T_SETPOINT` (20 C) until the
   next go-standby or go-measure.

Urgency: 60 C is near the upper limit for typical lithium cells (check the
limit for this battery), and the heater is draining it, so do steps 1 and 2
promptly.

### Avoiding a recurrence until a fix is flashed

The pin can stick high whenever the heater happens to be on in STANDBY and the
heater stops being permitted. That happens in two cases:

- V_IN drops below `CFG_V_DOCKED` (13.0 V), for example from any dock
  power-off while in STANDBY.
- The battery falls below `V_CRIT_BATT` (default 10.0 V).

Operational rule: **send TC 184 after every dock power-off while the RPU is in
STANDBY.**

Profiles are not at risk: in MEASURE, `tickMeasure()` runs the thermostat
every tick without the permissive check, and the end of a measurement goes
through `enterStandby()`, which clears the pin.

## Suggested fix

Drive the pin low when the heater is not permitted:

```cpp
if (batteryHeaterAllowed(vin, vbat, v_crit)) {
  if (adjustHeaters(bat_temp, setpoint)) on_ticks++;
} else {
  digitalWrite(BATTERY_HEATER, LOW);
}
```

Optionally also add an over-temperature cutoff in `adjustHeaters()` /
`manageHeater()` that forces the heater off above some limit regardless of
permissive state, as a backstop against any other stuck-on path.

## Related observation (please review)

In MEASURE, `RPU.cpp:711` calls `adjustHeaters(bat_t, bat_t_setpoint)`
directly, without the `batteryHeaterAllowed()` permissive. So in MEASURE the
heater is not gated on V_IN or on `V_CRIT_BATT` at all. This may be intentional
(profiling is done undocked, so V_IN is expected to be low), but it does not
match the ops doc, which describes the V_CRIT_BATT cutoff generally. Worth
confirming the intended behavior.

## Fix implemented (branch `fix-battery-heater-stuck-on`)

The heater permissive was also changed so the battery is thermally controlled
whenever the battery is healthy, docked or not:

- `batteryHeaterAllowed()` no longer requires `V_IN > CFG_V_DOCKED`. It returns
  false only after `bat_v` drops below `V_CRIT_BATT`, and that cutoff latches
  until `V_IN > CFG_V_DOCKED` (charging). The latch keeps the heater's ~7 W load
  from chattering it on and off around the threshold, and matches the ops doc.
- `manageHeater()` (STANDBY) drives `BATTERY_HEATER` low when the heater is not
  allowed. This is the stuck-on fix.
- `tickMeasure()` now applies the same permissive, so the V_CRIT_BATT cutoff
  also applies in MEASURE.
- The ops doc STANDBY section was updated to match.

Trade-off: undocked in STANDBY, the heater now runs off the battery with
nothing recharging it, which can pull the battery down to `V_CRIT_BATT` and
leave less energy for a profile. `V_CRIT_BATT` is the only battery protection
for the heater, and it is fixed at `CFG_V_CRIT_BATT` (10.0 V). The RPU has an
`RPU_SET_V_CRIT_BATT` handler, but RACHuTS has no TC for it and never sends
it, so changing it requires a reflash.

Not changed: `bat_v_low` (`V_LOW_BATT`) is still never read, so the ops doc's
"switch to STANDBY if undocked below V_LOW_BATT" is not implemented. Heater
duty ticks are still counted only in STANDBY.

## How to verify

- Bench: in STANDBY with the heater on (battery below setpoint), remove V_IN.
  Before the fix the pin stays high and `heater_i` stays non-zero. After the
  fix it should go to 0 within one STANDBY tick.
- Check that `heater_total_ticks` / `heater_on_ticks` (duty cycle reporting)
  still behave sensibly when the heater is disallowed.
- Undocked heating: in STANDBY with V_IN removed and the battery below the
  setpoint, the heater should come on and cycle on the thermostat.
- Latch: get `bat_v` below `V_CRIT_BATT`, either by replacing the battery
  with a variable bench supply and lowering it below 10 V, or with a temporary
  test build that sets `CFG_V_CRIT_BATT` just below the battery's present
  voltage (e.g. 12.5 V; restore it before flight). V_CRIT_BATT cannot be
  changed at run time with the existing tools. The heater should turn off and
  stay off after `bat_v` recovers, until V_IN > 13 V is applied. Check in both
  STANDBY and MEASURE.

## Caveats

- This diagnosis comes from reading the code and the report CSV. It has not
  been reproduced on the bench.
- In the CSV, GPS fields are identical in every row with `sats` = 0, so
  position/altitude look like a stale fix. This does not affect the heater
  analysis.
