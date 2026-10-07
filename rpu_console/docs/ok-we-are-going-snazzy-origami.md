# rpu_console: design and status (v0.3.0)

This began as the implementation plan and has been updated to describe what is built.

## Purpose
The RPU firmware prints debug text on its USB serial port. `rpu_console` is a PyQt6 desktop app that shows live-updating value labels and a scrolling log of the raw lines, and can send a few commands to the RPU.

## Input format
`sample_console_01.txt` (644 lines) shows the stream. A 7-line group repeats about once per second:

1. `RS41: air_t=.. pres=.. rh=.. hsensor_t=.. hdg=..`
2. `RS41 flags: hi_t=0 regen_lo=0 ptu=1 ...`
3. `ROPC: time=.. d300=.. d500=.. ... alarm=..`
4. `TSEN: airt_raw=.. ptemp_raw=.. pres_raw=..`
5. `TDLAS parse: key=value ...` (note `cluster_1= nan` with a space)
6. `TDLAS: key=value ...` (units attached, e.g. `27.46C`; values can be `nan`)
7. A one-line JSON record, the finalized TM record: `{"elapsed_s":70,"alt":0,...,"heater_stat":0}`

The firmware also prints replies to console commands (see "Serial control").

## Layout
```
rpu_console/
  pyproject.toml          # setuptools; deps PyQt6, pyserial, pyobjc (macOS); script `rpu-console`
  README.md
  docs/                   # this file
  tools/make_icon.py      # renders resources/icon.png with QPainter
  tests/test_parser.py    # parser and round-robin tests (pytest)
  sample_console_01.txt
  rpu_console/
    __init__.py           # __version__ (single source for title and pip metadata)
    __main__.py           # python -m rpu_console
    app.py                # main window, toolbar, serial control, app icon/name
    sources.py            # QThread line sources: serial, file, synthetic
    parser.py             # line -> fields; round-robin slot table and filter
    config.py             # JSON config in the home directory
    widgets.py            # value-label panel and scrolling log
    resources/icon.png    # shipped as package data
```

## Design
- **Sources** (`sources.py`). Each is a `QThread` emitting `line_received(str)` and `status(str)`. The GUI never touches the port directly.
  - `SerialSource(port, baud)`: pyserial `readline()` with a 0.2 s timeout so the thread stops cleanly. Commands are queued with `send()` and written from the same thread (newline-terminated). `list_usb_ports()` lists only USB ports (those with a VID, or `usbserial`/`usbmodem`/`ttyUSB`/`ttyACM` in the device name).
  - `FileSource(path, rate_hz, loop)`: replays a captured log at a configurable line rate, optionally looping.
  - `SyntheticSource`: one 7-line group per second in the firmware's format, with drifting values (including RS41 humidity and H-sensor temperature, varying currents) and `round_robin_idx` cycling 0–5.
- **Parser** (`parser.py`).
  - JSON lines go through `json.loads` and are the primary source for the labels.
  - The `key=value` lines are parsed with `(\w+)=\s*(\S+)`; unit suffixes are stripped and `nan` is handled. Fields are namespaced by prefix (`rs41.air_t`, `ropc.d300`, ...). The labels do not currently use these.
  - Unrecognised lines are shown in the log and ignored by the parser.
- **Round-robin fields.** The JSON record carries one slot of slow fields per record, selected by `round_robin_idx` (period 6). The table in `ROUND_ROBIN_FIELDS` mirrors `RPURecord::encode()` in the RPUComm library:

  | Slot | Fields |
  |---|---|
  | 0 | OPC d500, d700 |
  | 1 | OPC d1000, d2500 |
  | 2 | OPC d3000, d5000 |
  | 3 | RS41 heading, BEMF V, RS41 status |
  | 4 | 5V, TSEN/OPC/pump/TDLAS currents |
  | 5 | battery T, pump T, PCB T, battery V, heater |

  `drop_stale_round_robin()` removes fields that are not current, so the display keeps the last real value instead of the zero filler. These labels are marked `(rN)` with a tooltip, and they use a longer grey-out timeout (4x the normal 5 s) because they refresh only about every 6 s.
- **Display values.** `nan` and the "no data" sentinels (-999, -100, -20) show as `--`. Labels flush from a 200 ms timer, not per line, and go grey when stale.
- **Main window** (`app.py`, `widgets.py`).
  - Toolbar: source selector (Serial/File/Synthetic), port combo with Refresh, baud combo, file path with Browse, replay rate, Loop, Connect/Disconnect, and the Standby/Measure radio group.
  - Left pane: grouped value labels from a `FIELDS` table `(group, label, key, format)`, so adding a field takes one line.
  - Right pane: read-only monospace log (max 5000 lines) with Pause and Clear. It follows new lines unless the user scrolls up.
  - Status bar: connection state, lines/s, and the config file path.
- **Serial control.**
  - On connect: send `s` (standby), then send `d` until the firmware replies `debug print ON`. `d` is a toggle, so it is resent after an `OFF` reply, or after 1.5 s with no reply, up to 6 tries; the status bar reports a failure.
  - On disconnect or window close: send `s`.
  - Standby/Measure radio buttons send `s` / `m` (plain `m`, so the firmware's current duration and rate apply). The radio group follows the firmware's `Entering STANDBY` / `Entering MEASURE` lines and shows neither when the mode is unknown. The buttons are enabled only for a serial connection.
  - Sent commands are echoed in the log as `>>> m`.
- **Config** (`config.py`). JSON at `~/.rpu_console.json` (the path is shown in the status bar). It stores: source, port, baud, file path, replay rate, loop flag, window geometry, splitter sizes, max log lines, stale timeout. Missing or corrupt files fall back to defaults. The baud default is 115200, because the firmware's debug baud is not defined in this repo.
- **Icon and name.** `resources/icon.png` (a cylinder with a lightning bolt and a tether string, drawn by `tools/make_icon.py`) is set with `setWindowIcon`, which also sets the macOS Dock icon. The Dock name "RPU Console" is set through `pyobjc` before the `QApplication` is created; it is cosmetic and is skipped silently if `pyobjc` is missing.
- **Packaging.** `pyproject.toml` declares PyQt6, pyserial, and `pyobjc-framework-Cocoa` (macOS only), the `rpu-console` script, the icon as package data, and a dynamic version read from `rpu_console.__version__`. Install with `pip install -e rpu_console/`, or from GitHub (see the README).

## Decisions
- PyQt6 rather than PyQt5.
- Serial I/O is line-oriented; partial lines are buffered by `readline()`.
- No `flush()` after writing a command: on some ports it blocked the serial thread.
- Labels cover the JSON fields only; there are no plots yet.

## Verification
- `pytest rpu_console/tests` covers the parser (all line types, `nan`, spacing, bad JSON), a full parse of the sample file, and the round-robin filtering.
- Smoke tests were run headless (`QT_QPA_PLATFORM=offscreen`) for file replay, synthetic data, config save and restore, and the radio group.
- Serial behaviour (startup commands, debug retry, disconnect standby, mode radios) was tested against a pty acting as a fake firmware, not against real hardware. Testing with a real RPU on USB is still to do.

## Known gaps and ideas
- Not yet tested against a real USB serial device.
- The firmware has a stale comment in `RPUcomm.cpp` saying the round-robin period is 8 while the code uses `% 6`.
- If the firmware rejects `m` or `s`, the radio group can show the wrong mode until the next `Entering ...` line.
- The `key=value` lines are parsed but unused by the labels.
