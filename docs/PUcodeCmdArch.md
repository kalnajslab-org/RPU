# PU Code Command Architecture


## Overview

This document describes the reconstructed command architecture of the legacy RACHuTS PUcode, based on a Claude analysis of prior firmware and documentation. It is likely that there are mistakes in the analysis, but it does provide an approximate overview.

It does not define the 2026 RPU protocol, but the two are expected to be similar.

The PU firmware uses a layered serial communication stack on **Serial1** as the sole channel for structured RACHuTS/PIB commands. Five additional serial ports exist for instrument data and debug — they are not part of the command protocol.

---

## Serial Interfaces Summary

| Port | Object / Macro | Role | Baud | Protocol |
|------|---------------|------|------|----------|
| Serial1 | `PUComm pucomm` | RACHuTS/PIB commands (bidirectional) | 115200 | PUComm/SerialComm framed |
| Serial | `DEBUG_SERIAL` | USB debug input/output | 115200 | Plain text |
| Serial2 | `FLASH_SERIAL` | Fluorescence sensor data (inbound only) | 9600 | ASCII lines |
| Serial3 | `TSEN_SERIAL` | Thermal sensor data (inbound only) | 9600 | ASCII lines |
| Serial4 | `OPC_SERIAL` | Particle counter data (inbound only) | 9600 | ASCII lines |
| Serial5 | `GPS_SERIAL` | uBlox GPS data (inbound only) | 9600 | NMEA / TinyGPS++ |

**Only Serial1 handles RACHuTS commands.** The instrument serials are one-way data sinks buffered in `checkForSerial()` and parsed by dedicated functions (`parseFLASH()`, `getTSEN()`, `parseOPC()`).

---

## Protocol Stack (Serial1)

The command interface is built from two layered classes:

```
PUComm          — RACHuTS message definitions, typed parameter encode/decode
    └── SerialComm  — frame detection, checksum validation, raw buffer management
            └── Serial1 (Teensy 3.6 UART, 115200 bps)
```

**Source files:**

| File | Purpose |
|------|---------|
| `SerialComm.h / .cpp` | Low-level framing, checksums, RX/TX primitives |
| `PUComm.h / .cpp` | RACHuTS message IDs, typed RX_* / TX_* methods |
| `RACHuTS_PU_V2_5.ino` | Application logic: setup, mode state machine, `SerComRX()` dispatch |

---

## Frame Format

```
ASCII command:   # <msg_id> , <param1> , <param2> , ... ; <checksum>
Binary response: ! <bin_id> [binary data] ; <checksum>
ACK/NAK frame:  ? <msg_id> , <ack_value> ;
String message:  " <str_id> <string data> ; <checksum>
```

Checksum is XOR-based over the payload bytes.

---

## Command Reception Flow

```
PIB transmits ASCII frame over Serial1
        │
        ▼
SerialComm::RX()          (SerialComm.cpp)
  ├── serial_stream->available() / read() / peek()
  ├── Detect frame delimiter: '#' '!' '?' '"'
  ├── Route to: Read_ASCII() / Read_Bin() / Read_Ack() / Read_String()
  ├── Parse msg_id and comma-delimited parameters into ascii_rx.buffer
  └── Validate XOR checksum → return message type enum

        │
        ▼
SerComRX()                (RACHuTS_PU_V2_5.ino:1093)
  switch (pucomm.RX())
    ├── ASCII_MESSAGE  → switch on pucomm.ascii_rx.msg_id
    ├── ACK_MESSAGE    → store ack_id / ack_value for waiting callers
    └── NO_MESSAGE     → return false

        │
        ▼
PUComm::RX_*()            (PUComm.cpp)
  Extract typed parameters from ascii_rx.buffer
  (Get_float, Get_int32, Get_uint16, etc.)

        │
        ▼
Application action
  ├── Mode-change commands → set global Mode, return true (exit mode loop)
  ├── Data requests        → call sendTM() / sendTSEN(), transmit binary record
  └── Parameter updates    → update setpoint globals, TX_Ack back to PIB
```

---

## Command Dispatch Table

Handled in `SerComRX()` — `RACHuTS_PU_V2_5.ino:1093–1215`.

| ID | Symbolic Name | Parameters | Action |
|----|--------------|-----------|--------|
| 1 | `PU_SEND_STATUS` | — | Transmit ASCII status telemetry via `TX_Status()` |
| 2 | `PU_SEND_PROFILE_RECORD` | — | Transmit next binary profile record; wait for ACK |
| 3 | `PU_SEND_TSEN_RECORD` | — | Transmit next TSEN binary record; wait for ACK |
| 4 | `PU_RESET` | — | ACK then watchdog reset (`WRITE_RESTART`) |
| 5 | `PU_SET_HEATERS` | 2× float (setpoints) | Update heater targets; ACK |
| 6 | `PU_GO_LOWPOWER` | mode params | Mode → `LOWPOWER`; return true |
| 7 | `PU_GO_IDLE` | mode params | Mode → `IDLE`; return true |
| 8 | `PU_GO_WARMUP` | mode params | Mode → `WARMUP`; return true |
| 9 | `PU_GO_PREPROFILE` | mode params | Mode → `PREPROFILE`; return true |
| 10 | `PU_GO_PROFILE` | 5× int32, 4× int8 | Mode → `PROFILE`; return true |
| 11 | `PU_UPDATE_GPS` | uint32, 2× float, uint16 | Update GPS fix globals |
| 12 | `PU_LORA_STATUS` | LoRa status fields | Update LoRa status globals |

Mode-changing commands (IDs 6–10) return `true` from `SerComRX()`, causing the current mode loop in `loop()` to exit and re-enter with the new mode.

### PU_GO_PROFILE Parameter Detail (ID 10)

```c
// Extracted by PUComm::RX_Profile()
int32_t  t_down        // descent duration (s)
int32_t  t_dwell       // dwell duration (s)
int32_t  t_up          // ascent duration (s)
int32_t  rate_profile  // profile sample rate
int32_t  rate_dwell    // dwell sample rate
int8_t   TSEN_Power    // thermal sensor enable
int8_t   ROPC_Power    // OPC enable
int8_t   FLASH_Power   // fluorescence sensor enable
int8_t   LoRa_TM       // LoRa telemetry enable
```

---

## Acknowledgment Handshake

Binary data responses (profile records, TSEN records) use a send-and-wait ACK pattern:

1. PU transmits binary frame (`!bin_id [data] ; checksum`)
2. PU polls `SerComRX()` with a 6-second timeout waiting for `ACK_MESSAGE`
3. PIB sends `?msg_id,1;` (ACK) or `?msg_id,0;` (NAK)
4. On ACK: increment `TMRecordOffset` and continue
5. On NAK or timeout: retry or abort

See `sendTM()` — `RACHuTS_PU_V2_5.ino:840–894`.

---

## Debug Channel (Serial / USB)

A separate plain-text command parser (`parseCommand()`) handles USB debug input. It is **not** part of the PUComm protocol and does not use framing or checksums.

Supported debug commands: `#stop`, `#hk`, `#pump`, `#reset`, `#charge`, `#warmUp`, `#profile`, `#setHeater`.

---

## Instrument Data Channels (Serial2–5)

These four UARTs feed sensor data into line buffers inside `checkForSerial()` (`RACHuTS_PU_V2_5.ino:1043–1087`). No commands flow outbound on these ports.

| Port | Sensor | Parser |
|------|--------|--------|
| Serial2 | FLASH (fluorescence) | `parseFLASH()` — triggered on `\r` |
| Serial3 | TSEN (thermal) | `getTSEN()` — triggered on `\r` |
| Serial4 | OPC (particle counter) | `parseOPC()` — triggered on `\r` |
| Serial5 | uBlox GPS | `TinyGPS++` decoder, char-by-char |
