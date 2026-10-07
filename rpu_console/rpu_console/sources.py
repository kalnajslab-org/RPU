"""Line sources: serial port, file playback, synthetic data."""
import json
import math
import random

import serial
from PyQt6.QtCore import QThread, pyqtSignal
from serial.tools import list_ports

USB_NAME_HINTS = ("usbserial", "usbmodem", "ttyUSB", "ttyACM")


def list_usb_ports():
    """Return [(device, description)] for attached USB serial ports."""
    ports = []
    for p in list_ports.comports():
        if p.vid is not None or any(h in p.device for h in USB_NAME_HINTS):
            ports.append((p.device, p.description or p.device))
    return sorted(ports)


class LineSource(QThread):
    line_received = pyqtSignal(str)
    status = pyqtSignal(str)

    def stop(self):
        self.requestInterruption()
        self.wait(3000)


class SerialSource(LineSource):
    def __init__(self, port, baud, parent=None):
        super().__init__(parent)
        self.port = port
        self.baud = baud

    def run(self):
        try:
            ser = serial.Serial(self.port, self.baud, timeout=0.2)
        except (serial.SerialException, OSError, ValueError) as e:
            self.status.emit(f"Open failed: {e}")
            return
        self.status.emit(f"Connected {self.port} @ {self.baud}")
        try:
            while not self.isInterruptionRequested():
                raw = ser.readline()
                if raw:
                    text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                    self.line_received.emit(text)
        except (serial.SerialException, OSError) as e:
            self.status.emit(f"Serial error: {e}")
        finally:
            ser.close()


class FileSource(LineSource):
    def __init__(self, path, rate_hz, loop, parent=None):
        super().__init__(parent)
        self.path = path
        self.delay_ms = int(1000 / max(rate_hz, 0.1))
        self.loop = loop

    def run(self):
        try:
            with open(self.path, "r", encoding="utf-8", errors="replace") as f:
                lines = f.read().splitlines()
        except OSError as e:
            self.status.emit(f"Open failed: {e}")
            return
        self.status.emit(f"Playing {self.path} ({len(lines)} lines)")
        while not self.isInterruptionRequested():
            for line in lines:
                if self.isInterruptionRequested():
                    return
                self.line_received.emit(line)
                self.msleep(self.delay_ms)
            if not self.loop:
                self.status.emit("Playback finished")
                return


class SyntheticSource(LineSource):
    """One 7-line group per second, in the same format as the firmware output."""

    def run(self):
        self.status.emit("Synthetic data")
        t = 0
        while not self.isInterruptionRequested():
            t += 1
            for line in self.group(t):
                self.line_received.emit(line)
            self.msleep(1000)

    @staticmethod
    def group(t):
        r = random.uniform
        air_t = 20 + 5 * math.sin(t / 30) + r(-0.1, 0.1)
        pres = 1012.7 - t * 0.05
        hdg = (t * 3.0) % 360
        lt = 27.4 + r(-0.05, 0.05)
        d300 = int(r(200, 300))
        bins = {k: int(r(0, v)) for k, v in
                (("d500", 70), ("d700", 40), ("d1000", 15), ("d2000", 5),
                 ("d2500", 2), ("d3000", 1), ("d5000", 1))}
        rec = {
            "elapsed_s": t, "alt": 0, "lat_delta": 0.0, "lon_delta": 0.0,
            "sats": 0, "gps_age_s": 15, "opc_d300": d300, "opc_d2000": bins["d2000"],
            "tsen_airt": int(3500 + r(-3, 3)), "tsen_pres": 34395, "tsen_ptemp": 33688,
            "rs41_air_t": round(air_t, 2), "rs41_pres": round(pres, 1),
            "rs41_humidity": -20.0, "rs41_hsensor_t": -100.0,
            "tdlas_mixing_ratio": 0.0, "tdlas_background": 155, "tdlas_peak": round(r(0, 2), 1),
            "tdlas_ratio": 0.0, "tdlas_laser_temp": round(lt, 2), "tdlas_mr_max_ratio": 0.0,
            "tdlas_status": 0, "tdlas_cluster_idx": t % 5,
            "tdlas_cluster1": 0.0, "tdlas_cluster2": 0.0, "tdlas_cluster3": 0.0, "tdlas_cluster4": 0.0,
            "round_robin_idx": t % 8,
            "opc_d500": bins["d500"], "opc_d700": bins["d700"], "opc_d1000": bins["d1000"],
            "opc_d3000": bins["d3000"], "opc_d5000": bins["d5000"], "opc_d2500": bins["d2500"],
            "rs41_hdg": round(hdg, 2), "bemf_v": 0.0, "rs41_status": 0,
            "tsen_i": 16, "opc_i": 88, "pump_i": 148, "tdlas_i": 112,
            "v5": round(5.0 + r(-0.03, 0.03), 2), "bat_t": 26, "pump_t": 28, "pcb_t": 28,
            "bat_v": round(12.4 + r(-0.05, 0.05), 2), "heater_stat": 0,
        }
        return [
            f"RS41: air_t={air_t:.2f}C pres={pres:.1f}mb rh=-999.00% hsensor_t=-999.00C hdg={hdg:.2f}deg",
            "RS41 flags: hi_t=0 regen_lo=0 ptu=1 flash=0 lo_v=0 uncal=0 no_p=0 boom=0",
            f"ROPC: time={t} d300={d300} " + " ".join(f"{k}={v}" for k, v in bins.items()) + " alarm=0",
            f"TSEN: airt_raw={rec['tsen_airt']} ptemp_raw=8624312 pres_raw=8805256",
            f"TDLAS parse: mixing_ratio=nan background=155 peak={rec['tdlas_peak']} ratio=0.0 "
            f"laser_temp={lt:.2f} mr_max_ratio=nan status=0 cluster_idx={rec['tdlas_cluster_idx']} "
            "cluster_1= nan cluster_2= nan cluster_3= nan cluster_4= nan",
            f"TDLAS: mixing_ratio=nan background=155.0000 peak={rec['tdlas_peak']:.4f} ratio=0.000000 "
            f"laser_temp={lt:.2f}C mr_max_ratio=nan status=0 cluster_idx={rec['tdlas_cluster_idx']} "
            "cluster_1=nan cluster_2=nan cluster_3=nan cluster_4=nan",
            json.dumps(rec, separators=(",", ":")),
        ]
