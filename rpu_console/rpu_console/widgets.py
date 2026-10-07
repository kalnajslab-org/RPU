import math
import time

from PyQt6.QtGui import QFont, QFontDatabase
from PyQt6.QtWidgets import (QCheckBox, QGridLayout, QGroupBox, QHBoxLayout, QLabel,
                             QPlainTextEdit, QPushButton, QScrollArea, QVBoxLayout, QWidget)

# (group, label, field key, format). Keys come from the JSON record in parser.py.
FIELDS = [
    ("Time / GPS", "Elapsed (s)", "elapsed_s", "{:d}"),
    ("Time / GPS", "Altitude", "alt", "{}"),
    ("Time / GPS", "Lat delta", "lat_delta", "{:.5f}"),
    ("Time / GPS", "Lon delta", "lon_delta", "{:.5f}"),
    ("Time / GPS", "Satellites", "sats", "{:d}"),
    ("Time / GPS", "GPS age (s)", "gps_age_s", "{:d}"),
    ("Time / GPS", "Round robin", "round_robin_idx", "{:d}"),
    ("RS41", "Air T (C)", "rs41_air_t", "{:.2f}"),
    ("RS41", "Pressure (mb)", "rs41_pres", "{:.1f}"),
    ("RS41", "Humidity (%)", "rs41_humidity", "{:.2f}"),
    ("RS41", "H sensor T (C)", "rs41_hsensor_t", "{:.2f}"),
    ("RS41", "Heading (deg)", "rs41_hdg", "{:.2f}"),
    ("RS41", "Status", "rs41_status", "{:d}"),
    ("OPC", "d300", "opc_d300", "{:d}"),
    ("OPC", "d500", "opc_d500", "{:d}"),
    ("OPC", "d700", "opc_d700", "{:d}"),
    ("OPC", "d1000", "opc_d1000", "{:d}"),
    ("OPC", "d2000", "opc_d2000", "{:d}"),
    ("OPC", "d2500", "opc_d2500", "{:d}"),
    ("OPC", "d3000", "opc_d3000", "{:d}"),
    ("OPC", "d5000", "opc_d5000", "{:d}"),
    ("TSEN", "Air T raw", "tsen_airt", "{:d}"),
    ("TSEN", "Pressure", "tsen_pres", "{:d}"),
    ("TSEN", "Press T", "tsen_ptemp", "{:d}"),
    ("TDLAS", "Mixing ratio", "tdlas_mixing_ratio", "{:.2f}"),
    ("TDLAS", "Background", "tdlas_background", "{}"),
    ("TDLAS", "Peak", "tdlas_peak", "{}"),
    ("TDLAS", "Ratio", "tdlas_ratio", "{}"),
    ("TDLAS", "Laser T (C)", "tdlas_laser_temp", "{:.2f}"),
    ("TDLAS", "MR max ratio", "tdlas_mr_max_ratio", "{}"),
    ("TDLAS", "Status", "tdlas_status", "{:d}"),
    ("TDLAS", "Cluster idx", "tdlas_cluster_idx", "{:d}"),
    ("TDLAS", "Cluster 1", "tdlas_cluster1", "{:.2f}"),
    ("TDLAS", "Cluster 2", "tdlas_cluster2", "{:.2f}"),
    ("TDLAS", "Cluster 3", "tdlas_cluster3", "{:.2f}"),
    ("TDLAS", "Cluster 4", "tdlas_cluster4", "{:.2f}"),
    ("Power / Temps", "5V", "v5", "{:.2f}"),
    ("Power / Temps", "Battery V", "bat_v", "{:.2f}"),
    ("Power / Temps", "Battery T", "bat_t", "{:d}"),
    ("Power / Temps", "Pump T", "pump_t", "{:d}"),
    ("Power / Temps", "PCB T", "pcb_t", "{:d}"),
    ("Power / Temps", "Heater", "heater_stat", "{:d}"),
    ("Power / Temps", "BEMF V", "bemf_v", "{:.3f}"),
    ("Currents (mA)", "TSEN", "tsen_i", "{:d}"),
    ("Currents (mA)", "OPC", "opc_i", "{:d}"),
    ("Currents (mA)", "Pump", "pump_i", "{:d}"),
    ("Currents (mA)", "TDLAS", "tdlas_i", "{:d}"),
]

# Values the firmware uses for "no data".
SENTINELS = {-999.0, -100.0, -20.0}
NO_DATA = "--"


def format_value(fmt, value):
    if isinstance(value, str):
        return value
    if isinstance(value, float) and math.isnan(value):
        return NO_DATA
    if value in SENTINELS:
        return NO_DATA
    try:
        return fmt.format(value)
    except (ValueError, TypeError):
        # e.g. "{:d}" given a float
        return str(value)


class ValuePanel(QScrollArea):
    """Grouped value labels, flushed on a timer; stale values are greyed."""

    def __init__(self, stale_s=5.0, parent=None):
        super().__init__(parent)
        self.stale_s = stale_s
        self._labels = {}      # key -> (QLabel, fmt)
        self._pending = {}
        self._updated = {}     # key -> monotonic time
        self.setWidgetResizable(True)

        body = QWidget()
        col = QVBoxLayout(body)
        mono = QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)
        grids = {}
        for group, label, key, fmt in FIELDS:
            if group not in grids:
                box = QGroupBox(group)
                grids[group] = QGridLayout(box)
                col.addWidget(box)
            grid = grids[group]
            row = grid.rowCount()
            val = QLabel(NO_DATA)
            val.setFont(mono)
            val.setMinimumWidth(80)
            grid.addWidget(QLabel(label), row, 0)
            grid.addWidget(val, row, 1)
            self._labels[key] = (val, fmt)
        col.addStretch(1)
        self.setWidget(body)

    def update_fields(self, fields):
        now = time.monotonic()
        for k, v in fields.items():
            if k in self._labels:
                self._pending[k] = v
                self._updated[k] = now

    def flush(self):
        for k, v in self._pending.items():
            label, fmt = self._labels[k]
            label.setText(format_value(fmt, v))
        self._pending.clear()
        now = time.monotonic()
        for k, (label, _) in self._labels.items():
            stale = k not in self._updated or now - self._updated[k] > self.stale_s
            label.setEnabled(not stale)

    def reset(self):
        self._pending.clear()
        self._updated.clear()
        for label, _ in self._labels.values():
            label.setText(NO_DATA)
            label.setEnabled(False)


class LogView(QWidget):
    """Scrolling read-only canvas of raw incoming lines."""

    def __init__(self, max_lines=5000, parent=None):
        super().__init__(parent)
        self.text = QPlainTextEdit()
        self.text.setReadOnly(True)
        self.text.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.text.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
        self.text.setMaximumBlockCount(max_lines)
        self._buffer = []
        self.pause = QCheckBox("Pause")
        clear = QPushButton("Clear")
        clear.clicked.connect(self.text.clear)

        bar = QHBoxLayout()
        bar.addWidget(self.pause)
        bar.addWidget(clear)
        bar.addStretch(1)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addLayout(bar)
        lay.addWidget(self.text)

    def add_line(self, line):
        self._buffer.append(line)

    def flush(self):
        """Append buffered lines; keep the view pinned to the bottom unless the user scrolled up."""
        if not self._buffer or self.pause.isChecked():
            if self.pause.isChecked():
                self._buffer = self._buffer[-self.text.maximumBlockCount():]
            return
        sb = self.text.verticalScrollBar()
        at_bottom = sb.value() >= sb.maximum() - 2
        self.text.appendPlainText("\n".join(self._buffer))
        self._buffer.clear()
        if at_bottom:
            sb.setValue(sb.maximum())
