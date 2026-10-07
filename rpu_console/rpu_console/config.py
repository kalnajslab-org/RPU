import json
from pathlib import Path

CONFIG_PATH = Path.home() / ".rpu_console.json"

DEFAULTS = {
    "source": "Synthetic",
    "port": "",
    "baud": 115200,
    "file_path": "",
    "replay_hz": 7.0,
    "loop_file": True,
    "max_log_lines": 5000,
    "stale_s": 5.0,
    "geometry": "",
    "splitter": [],
}


def load_config(path=CONFIG_PATH):
    cfg = dict(DEFAULTS)
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            cfg.update({k: v for k, v in data.items() if k in DEFAULTS})
    except (OSError, ValueError):
        pass
    return cfg


def save_config(cfg, path=CONFIG_PATH):
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
        return True
    except OSError:
        return False
