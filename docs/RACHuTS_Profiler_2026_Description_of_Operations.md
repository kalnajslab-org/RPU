# RACHuTS Profiler 2026 Description of Operations

The overall design philosophy is that the profiler operation is as simple as possible, with as much of the control logic implemented in the RACHuTS main board/StratoRACHuTS as possible. The Profiler is a state machine with two primary modes/states:

---

## States

### STANDBY

This is the default state, and the Profiler returns to this state on startup and when other actions are complete or during a low-battery event. There are no explicit configuration options for STANDBY; the saved config is used to set setpoints.

- All sensors that can be powered down are powered down.
- Power consumption is minimized.
- If docked (V_IN > 13V) then the charger is on and battery heater is enabled.
- Battery thermal management is on at the saved setpoint.
- Profiler sends status messages every 30 min (configurable) via LoRa and over the docking connector containing: Battery Voltage, Battery Temperature, Charge Current, PCB Temperature, Heater Duty Cycle, GPS Lat, Long, Alt, # Sats. These status messages are packaged into RACHuTS reports and sent via TM.
- The Profiler responds to commands, particularly to send any available data.
- If the battery voltage drops below V_CRIT_BATT then the heater is disabled (until V_IN > 13.0V).

### MEASURE

This is the state for performing measurements, both while docked as well as for profiling. This state is entered through a PUcomm command which specifies how long to remain in this state and the measurement rate. The profiler remains in this state for the specified duration, until commanded to STANDBY, or until the battery is depleted, at which time it returns to STANDBY.

- When entering MEASURE, parameters are passed to the profiler indicating how long to remain in MEASURE, which sensors should be powered on, and what the reported measurement rate should be (sensors may report faster than this, but their data is averaged to this rate).
- In MEASURE the profiler accumulates data to RAM, and expects an offload request from the mainboard before the buffer is full.
- The Profiler sends status messages (see STANDBY) over LoRa every 30 min.
- If the battery falls below V_LOW_BATT & V_IN < 13.0V, then profiler switches to STANDBY.
- A subsequent GO_MEASURE command while still in the MEASURE mode will extend the duration of the MEASURE period, allowing continuous measurement while docked.
- The profiler will offload stored data on command from StratoRACHuTS.

---

## Global Commands

Outside of the profiler states, there are several global commands that the profiler needs to accept.

- The profiler needs to send any data from the buffer on a `SEND_RECORDS` command. The profiler should send the next 8 kB chunk of data (~190 lines), or whatever data is remaining in the buffer, whichever is smaller.
- If no data is available to send after a `SEND_RECORDS` command then a `NO_MORE_RECORDS` response should be sent.
- The `BATT_T_SET`, `V_LOW_BATT`, `V_CRIT_BATT`, and `STATUS_RATE` can all be configured via TC.
- The profiler should send the current status on a `SEND_STATUS` request. This can be used to check for a successful dock, or to inquire as to profiler status from the ground via a TC.

---

## General Docked Measurement and Profile Sequence

### Flight Level / Docked Measurements

For flight level or docked measurements, the sequence of operations is:

1. A TC is sent from the ground requesting a flight level measurement for a specified duration (can be very long for continuous measurements), at a specified measurement rate with the specified instruments powered on.
2. If the duration is < 1800 s (30 min) then StratoRACHuTS commands the profiler into MEASURE for the requested time. If the duration is > 1800 s, then StratoRACHuTS divides the duration into 1800 s chunks and commands the profiler into MEASURE for 1800 + 60 s.
3. After the completion of the measurement duration, or after 1800 s, StratoRACHuTS commands `SEND_RECORDS` repeatedly until no more records remain.
4. If the duration of the flight level measurement was > 1800 s, StratoRACHuTS repeats steps 2 & 3 until the duration is up. The additional 60 s added to each MEASURE period allows for continuous measurement while the current records are downloaded and the next measurement command is sent.
5. At the end of the flight level measurement, the profiler returns to STANDBY.

### Profile Sequence

For a profile, the sequence of operations is:

1. A TC is sent from the ground requesting a profile, with a specific length, preprofile and dwell time, which instruments should be powered on, and the measurement rate.
2. StratoRACHuTS receives this command and requests a RACHuTS Authorization (RA) from the Zephyr OBC.
3. If an RAAck is received, a profile can commence. StratoRACHuTS calculates the total preprofile + descent + dwell + ascent time and commands the profiler to MEASURE mode for this duration with the appropriate configuration.
4. StratoRACHuTS waits for the preprofile duration.
5. StratoRACHuTS turns the power to the docking connector off and commands the MCB to reel out the descent distance.
6. On completion of the reel out, StratoRACHuTS sends the MCB motion data TMs (or in real time if configured) and waits for the dwell period.
7. StratoRACHuTS commands the MCB to reel in 98% (check this) of the descent distance.
8. On completion of the reel in, StratoRACHuTS sends the MCB motion data TMs (or in real time) and sends a dock command to the MCB for 2× the remaining distance.
9. On completion of the dock motion, StratoRACHuTS sends a `SEND_STATUS` command to the profiler.
10. If the Profiler does not respond, StratoRACHuTS sends the MCB a re-dock command (3×).
11. Once communication is established, StratoRACHuTS turns on the power to the docking connector.
12. StratoRACHuTS sends the profiler `SEND_RECORDS` commands until no more records are available.
13. At this point the profiler will be in STANDBY as MEASURE has timed out and can remain here until the next operation is commanded.
