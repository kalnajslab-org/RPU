import sys
import time

from PyQt6.QtCore import QByteArray, Qt, QTimer
from PyQt6.QtWidgets import (QApplication, QButtonGroup, QRadioButton, QComboBox, QDoubleSpinBox, QFileDialog, QCheckBox,
                             QLabel, QLineEdit, QMainWindow, QPushButton, QSplitter, QToolBar)

from . import __version__
from .config import CONFIG_PATH, load_config, save_config
from .parser import drop_stale_round_robin, parse_line
from .sources import FileSource, SerialSource, SyntheticSource, list_usb_ports
from .widgets import LogView, ValuePanel

BAUDS = ["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"]
SOURCES = ["Serial", "File", "Synthetic"]
FLUSH_MS = 200


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"RPU Console v{__version__}")
        self.cfg = load_config()
        self.source = None
        self._line_count = 0
        self._rate_t = time.monotonic()

        self.panel = ValuePanel(self.cfg["stale_s"])
        self.log = LogView(int(self.cfg["max_log_lines"]))
        self.splitter = QSplitter(Qt.Orientation.Horizontal)
        self.splitter.addWidget(self.panel)
        self.splitter.addWidget(self.log)
        self.splitter.setStretchFactor(1, 1)
        self.setCentralWidget(self.splitter)

        self._build_toolbar()
        self.state_label = QLabel("Disconnected")
        self.rate_label = QLabel("0 lines/s")
        cfg_label = QLabel(f"Config: {CONFIG_PATH}")
        cfg_label.setToolTip(f"Settings are saved in {CONFIG_PATH}")
        self.statusBar().addWidget(self.state_label, 1)
        self.statusBar().addPermanentWidget(self.rate_label)
        self.statusBar().addPermanentWidget(cfg_label)

        self.timer = QTimer(self)
        self.timer.timeout.connect(self._flush)
        self.timer.start(FLUSH_MS)

        self._restore_geometry()
        self.panel.reset()
        self._on_source_changed()

    # ---- toolbar -------------------------------------------------------
    def _build_toolbar(self):
        tb = QToolBar("Source")
        tb.setMovable(False)
        self.addToolBar(tb)

        self.source_box = QComboBox()
        self.source_box.addItems(SOURCES)
        self.source_box.setCurrentText(self.cfg["source"])
        self.source_box.currentTextChanged.connect(self._on_source_changed)
        tb.addWidget(QLabel(" Source "))
        tb.addWidget(self.source_box)

        self.port_box = QComboBox()
        self.port_box.setMinimumWidth(240)
        self.refresh_btn = QPushButton("Refresh")
        self.refresh_btn.clicked.connect(self._refresh_ports)
        self.baud_box = QComboBox()
        self.baud_box.setEditable(True)
        self.baud_box.addItems(BAUDS)
        self.baud_box.setCurrentText(str(self.cfg["baud"]))
        self.serial_widgets = [
            tb.addWidget(QLabel(" Port ")), tb.addWidget(self.port_box),
            tb.addWidget(self.refresh_btn),
            tb.addWidget(QLabel(" Baud ")), tb.addWidget(self.baud_box),
        ]

        self.file_edit = QLineEdit(self.cfg["file_path"])
        self.file_edit.setMinimumWidth(240)
        self.browse_btn = QPushButton("Browse...")
        self.browse_btn.clicked.connect(self._browse)
        self.rate_spin = QDoubleSpinBox()
        self.rate_spin.setRange(0.1, 1000)
        self.rate_spin.setSuffix(" lines/s")
        self.rate_spin.setValue(float(self.cfg["replay_hz"]))
        self.loop_box = QCheckBox("Loop")
        self.loop_box.setChecked(bool(self.cfg["loop_file"]))
        self.file_widgets = [
            tb.addWidget(QLabel(" File ")), tb.addWidget(self.file_edit),
            tb.addWidget(self.browse_btn), tb.addWidget(self.rate_spin),
            tb.addWidget(self.loop_box),
        ]

        tb.addSeparator()
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._toggle_connection)
        tb.addWidget(self.connect_btn)

        tb.addSeparator()
        self.standby_btn = QRadioButton("Standby")
        self.measure_btn = QRadioButton("Measure")
        self.mode_group = QButtonGroup(self)
        self.mode_group.addButton(self.standby_btn)
        self.mode_group.addButton(self.measure_btn)
        # clicked fires only on user clicks; programmatic setChecked() won't resend.
        self.standby_btn.clicked.connect(lambda: self._send_command("s"))
        self.measure_btn.clicked.connect(lambda: self._send_command("m"))
        tb.addWidget(self.standby_btn)
        tb.addWidget(self.measure_btn)
        self._update_command_buttons()

    def _on_source_changed(self, *_):
        src = self.source_box.currentText()
        for a in self.serial_widgets:
            a.setVisible(src == "Serial")
        for a in self.file_widgets:
            a.setVisible(src == "File")
        if src == "Serial":
            self._refresh_ports()

    def _refresh_ports(self):
        wanted = self.port_box.currentData() or self.cfg["port"]
        self.port_box.clear()
        for dev, desc in list_usb_ports():
            self.port_box.addItem(f"{dev}  ({desc})", dev)
        idx = self.port_box.findData(wanted)
        if idx >= 0:
            self.port_box.setCurrentIndex(idx)
        elif self.port_box.count() == 0:
            self.port_box.addItem("(no USB serial ports found)", "")

    def _browse(self):
        path, _ = QFileDialog.getOpenFileName(self, "Replay file", self.file_edit.text())
        if path:
            self.file_edit.setText(path)

    # ---- connection ----------------------------------------------------
    def _toggle_connection(self):
        if self.source is not None:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        kind = self.source_box.currentText()
        if kind == "Serial":
            port = self.port_box.currentData()
            if not port:
                self.state_label.setText("No serial port selected")
                return
            try:
                baud = int(self.baud_box.currentText())
            except ValueError:
                self.state_label.setText("Invalid baud rate")
                return
            self.source = SerialSource(port, baud)
        elif kind == "File":
            path = self.file_edit.text().strip()
            if not path:
                self.state_label.setText("No file selected")
                return
            self.source = FileSource(path, self.rate_spin.value(), self.loop_box.isChecked())
        else:
            self.source = SyntheticSource()
        self.source.line_received.connect(self._on_line)
        self.source.status.connect(self.state_label.setText)
        self.source.finished.connect(self._on_source_finished)
        if kind == "Serial":
            self.source.command_sent.connect(self._on_command_sent)
        self.panel.reset()
        self.source.start()
        self._set_connected(True)

    def _disconnect(self):
        if self.source is not None:
            src, self.source = self.source, None
            if isinstance(src, SerialSource) and src.isRunning():
                src.send("s")  # leave the RPU in standby when we let go of it
            src.stop()
        self._set_connected(False)
        self.state_label.setText("Disconnected")

    def _on_source_finished(self):
        # Source ended by itself (open failed, port unplugged, playback done).
        if self.source is not None and not self.source.isRunning():
            self.source = None
            self._set_connected(False)

    def _on_command_sent(self, cmd):
        self.log.add_line(f">>> {cmd}")

    def _send_command(self, cmd):
        if isinstance(self.source, SerialSource):
            self.source.send(cmd)

    def _update_command_buttons(self):
        enabled = isinstance(self.source, SerialSource)
        self.standby_btn.setEnabled(enabled)
        self.measure_btn.setEnabled(enabled)
        if not enabled:
            self._set_mode(None)

    def _set_mode(self, mode):
        """Show the RPU mode ("standby", "measure", or None = unknown) in the radio group."""
        self.mode_group.setExclusive(False)  # needed to be able to clear both
        self.standby_btn.setChecked(mode == "standby")
        self.measure_btn.setChecked(mode == "measure")
        self.mode_group.setExclusive(True)

    def _set_connected(self, on):
        self._update_command_buttons()
        self.connect_btn.setText("Disconnect" if on else "Connect")
        for w in (self.source_box, self.port_box, self.baud_box, self.refresh_btn,
                  self.file_edit, self.browse_btn, self.rate_spin, self.loop_box):
            w.setEnabled(not on)

    # ---- data ----------------------------------------------------------
    def _on_line(self, line):
        self._line_count += 1
        self.log.add_line(line)
        if line.startswith("Entering STANDBY"):
            self._set_mode("standby")
        elif line.startswith("Entering MEASURE"):
            self._set_mode("measure")
        fields = drop_stale_round_robin(parse_line(line))
        if fields:
            self.panel.update_fields(fields)

    def _flush(self):
        self.panel.flush()
        self.log.flush()
        now = time.monotonic()
        if now - self._rate_t >= 1.0:
            self.rate_label.setText(f"{self._line_count / (now - self._rate_t):.0f} lines/s")
            self._line_count = 0
            self._rate_t = now

    # ---- config --------------------------------------------------------
    def _restore_geometry(self):
        geo = self.cfg.get("geometry")
        if geo and self.restoreGeometry(QByteArray.fromHex(geo.encode())):
            pass
        else:
            self.resize(1200, 750)
        sizes = self.cfg.get("splitter")
        if sizes:
            self.splitter.setSizes([int(s) for s in sizes])
        else:
            self.splitter.setSizes([380, 820])

    def _collect_config(self):
        baud = self.baud_box.currentText().strip()
        self.cfg.update({
            "source": self.source_box.currentText(),
            "port": self.port_box.currentData() or self.cfg["port"],
            "baud": int(baud) if baud.isdigit() else self.cfg["baud"],
            "file_path": self.file_edit.text().strip(),
            "replay_hz": self.rate_spin.value(),
            "loop_file": self.loop_box.isChecked(),
            "geometry": bytes(self.saveGeometry().toHex()).decode(),
            "splitter": self.splitter.sizes(),
        })

    def closeEvent(self, event):
        self._disconnect()
        self._collect_config()
        save_config(self.cfg)
        super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
