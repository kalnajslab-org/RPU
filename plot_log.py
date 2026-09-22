"""
plot_log.py — RACHuTS Profiler SD card log reader and plotter

Usage:
    python plot_log.py LOG000.TXT          # plot default views
    python plot_log.py LOG000.TXT --list   # print column names
    python plot_log.py LOG000.TXT --cols PCBTemp BatteryTemp VBat

The script loads the log into a pandas DataFrame and offers pre-built
plot groups (power, temperatures, GPS, OPC, TDLAS, RS41).  You can also
pass --cols to plot any arbitrary set of columns on one figure.
"""

import argparse
import sys
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec


# ---------------------------------------------------------------------------
# Column definitions (matches the header written by Profiler.cpp)
# ---------------------------------------------------------------------------
COLUMNS = [
    "serial_hex", "elapsed_ms",
    "VBat", "vin", "charge_status", "charge_imon", "vmon_5V", "I_pump",
    "OPC_I", "TSEN_I", "TDLAS_I", "Battery_Heater_I", "BEMF1_V", "BEMF1_pwm",
    "GPS_lat", "GPS_lng", "GPS_alt_m", "GPS_satellites",
    "GPS_date", "GPS_time", "GPS_age_s",
    "PCBTemp", "PumpTemp", "BatteryTemp",
    "ROPC_time", "d300", "d500", "d700", "d1000", "d2000", "d2500", "d3000", "d5000", "OPC_alarm",
    "TDLAS_mixing_ratio", "TDLAS_background", "TDLAS_peak", "TDLAS_ratio",
    "TDLAS_laser_temp", "TDLAS_mr_max_ratio", "TDLAS_status", "TDLAS_cluster_idx",
    "TDLAS_cluster_1", "TDLAS_cluster_2", "TDLAS_cluster_3", "TDLAS_cluster_4",
    "RS41_frame", "RS41_air_temp", "RS41_humidity", "RS41_hsensor_temp", "RS41_pres",
    "RS41_internal_temp", "RS41_module_status", "RS41_module_error", "RS41_pcb_supply_V",
    "RS41_lsm303_temp", "RS41_pcb_heater_on",
    "RS41_mag_hdgXY", "RS41_mag_hdgXZ", "RS41_mag_hdgYZ",
    "RS41_accelX", "RS41_accelY", "RS41_accelZ",
]


# ---------------------------------------------------------------------------
# Pre-built plot groups
# ---------------------------------------------------------------------------
PLOT_GROUPS = {
    "power": {
        "title": "Power & Currents",
        "subplots": [
            {"cols": ["VBat", "vin", "vmon_5V"],         "ylabel": "Voltage (V)"},
            {"cols": ["charge_imon", "charge_status"],    "ylabel": "Charge (V)"},
            {"cols": ["I_pump", "OPC_I", "TSEN_I", "TDLAS_I", "Battery_Heater_I"],
                                                          "ylabel": "Current (mA / raw)"},
            {"cols": ["BEMF1_V"],                         "ylabel": "Back-EMF (V)"},
            {"cols": ["BEMF1_pwm"],                       "ylabel": "Pump PWM"},
        ],
    },
    "temperatures": {
        "title": "Temperatures",
        "subplots": [
            {"cols": ["PCBTemp", "PumpTemp", "BatteryTemp"], "ylabel": "Board Temps (°C)"},
            {"cols": ["TDLAS_laser_temp"],                   "ylabel": "TDLAS Laser Temp (°C)"},
            {"cols": ["RS41_air_temp", "RS41_hsensor_temp", "RS41_internal_temp",
                      "RS41_lsm303_temp"],                   "ylabel": "RS41 Temps (°C)"},
        ],
    },
    "gps": {
        "title": "GPS",
        "subplots": [
            {"cols": ["GPS_alt_m"],      "ylabel": "Altitude (m)"},
            {"cols": ["GPS_lat"],        "ylabel": "Latitude (°)"},
            {"cols": ["GPS_lng"],        "ylabel": "Longitude (°)"},
            {"cols": ["GPS_satellites"], "ylabel": "Satellites"},
            {"cols": ["GPS_age_s"],      "ylabel": "Fix Age (s)"},
        ],
    },
    "opc": {
        "title": "ROPC Particle Counts",
        "subplots": [
            {"cols": ["d300", "d500", "d700"],                "ylabel": "Counts (small)"},
            {"cols": ["d1000", "d2000", "d2500"],             "ylabel": "Counts (medium)"},
            {"cols": ["d3000", "d5000"],                      "ylabel": "Counts (large)"},
            {"cols": ["OPC_alarm"],                           "ylabel": "Alarm"},
        ],
    },
    "tdlas": {
        "title": "TDLAS",
        "subplots": [
            {"cols": ["TDLAS_mixing_ratio", "TDLAS_mr_max_ratio"], "ylabel": "Mixing Ratio"},
            {"cols": ["TDLAS_background", "TDLAS_peak"],           "ylabel": "Background / Peak"},
            {"cols": ["TDLAS_ratio"],                              "ylabel": "Ratio"},
            {"cols": ["TDLAS_status", "TDLAS_cluster_idx"],        "ylabel": "Status / Cluster Idx"},
            {"cols": ["TDLAS_cluster_1", "TDLAS_cluster_2",
                      "TDLAS_cluster_3", "TDLAS_cluster_4"],       "ylabel": "Cluster Values"},
        ],
    },
    "rs41": {
        "title": "RS41 Radiosonde",
        "subplots": [
            {"cols": ["RS41_air_temp"],             "ylabel": "Air Temp (°C)"},
            {"cols": ["RS41_humidity"],             "ylabel": "Humidity (%)"},
            {"cols": ["RS41_pres"],                 "ylabel": "Pressure (mb)"},
            {"cols": ["RS41_pcb_supply_V"],         "ylabel": "PCB Supply (V)"},
            {"cols": ["RS41_accelX", "RS41_accelY", "RS41_accelZ"], "ylabel": "Accel (mG)"},
        ],
    },
}


# ---------------------------------------------------------------------------
# load_log
# ---------------------------------------------------------------------------
def load_log(path: str) -> pd.DataFrame:
    """
    Parse a Profiler SD log file into a DataFrame.

    - Lines beginning with the header keyword or 'LORA_TX:' are skipped.
    - Lines that don't produce exactly len(COLUMNS) fields are skipped with
      a warning so partial/corrupt rows don't abort the load.
    """
    rows = []
    skipped = 0

    with open(path, "r", errors="replace") as fh:
        for lineno, raw in enumerate(fh, start=1):
            line = raw.strip()
            if not line:
                continue
            # Skip header and LoRa TX lines
            if line.startswith("serial_hex") or line.startswith("LORA_TX:"):
                continue
            fields = line.split(",")
            if len(fields) != len(COLUMNS):
                skipped += 1
                if skipped <= 5:
                    print(f"  [skip] line {lineno}: expected {len(COLUMNS)} fields, "
                          f"got {len(fields)}", file=sys.stderr)
                continue
            rows.append(fields)

    if skipped > 5:
        print(f"  [skip] ... and {skipped - 5} more malformed lines", file=sys.stderr)

    if not rows:
        raise ValueError(f"No valid data rows found in '{path}'")

    df = pd.DataFrame(rows, columns=COLUMNS)

    # Convert numeric columns (leave serial_hex as string)
    for col in COLUMNS[1:]:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    # Convenience time axis in seconds
    df["elapsed_s"] = df["elapsed_ms"] / 1000.0

    print(f"Loaded {len(df)} rows from '{path}'  ({skipped} skipped)")
    return df


# ---------------------------------------------------------------------------
# plot_group
# ---------------------------------------------------------------------------
def plot_group(df: pd.DataFrame, group_key: str):
    group = PLOT_GROUPS[group_key]
    subplots = group["subplots"]
    fig, axes = plt.subplots(len(subplots), 1, figsize=(12, 2.5 * len(subplots)),
                             sharex=True)
    if len(subplots) == 1:
        axes = [axes]

    fig.suptitle(group["title"], fontsize=13, fontweight="bold")

    for ax, sp in zip(axes, subplots):
        for col in sp["cols"]:
            if col in df.columns:
                ax.plot(df["elapsed_s"], df[col], label=col, linewidth=0.8)
        ax.set_ylabel(sp["ylabel"])
        ax.legend(fontsize=7, loc="upper right")
        ax.grid(True, linewidth=0.4)

    axes[-1].set_xlabel("Elapsed Time (s)")
    fig.tight_layout()
    return fig


# ---------------------------------------------------------------------------
# plot_cols  (arbitrary column selection)
# ---------------------------------------------------------------------------
def plot_cols(df: pd.DataFrame, cols: list[str]):
    missing = [c for c in cols if c not in df.columns]
    if missing:
        print(f"Unknown columns: {missing}", file=sys.stderr)
        cols = [c for c in cols if c in df.columns]
    if not cols:
        raise ValueError("No valid columns to plot")

    fig, ax = plt.subplots(figsize=(12, 4))
    for col in cols:
        ax.plot(df["elapsed_s"], df[col], label=col, linewidth=0.9)
    ax.set_xlabel("Elapsed Time (s)")
    ax.legend(fontsize=8)
    ax.grid(True, linewidth=0.4)
    fig.tight_layout()
    return fig


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="RACHuTS Profiler log reader / plotter")
    parser.add_argument("logfile", help="Path to LOG*.TXT file from SD card")
    parser.add_argument("--list", action="store_true",
                        help="Print all column names and exit")
    parser.add_argument("--group", choices=list(PLOT_GROUPS.keys()),
                        help="Plot a single named group instead of all groups")
    parser.add_argument("--cols", nargs="+", metavar="COL",
                        help="Plot arbitrary columns by name")
    args = parser.parse_args()

    if args.list:
        print("Available columns:")
        for col in COLUMNS + ["elapsed_s"]:
            print(f"  {col}")
        return

    df = load_log(args.logfile)

    if args.cols:
        plot_cols(df, args.cols)
    elif args.group:
        plot_group(df, args.group)
    else:
        for key in PLOT_GROUPS:
            plot_group(df, key)

    plt.show()

    return df   # useful when running interactively


if __name__ == "__main__":
    main()
