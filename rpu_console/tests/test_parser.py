import math
from pathlib import Path

from rpu_console.parser import parse_line

SAMPLE = Path(__file__).resolve().parent.parent / "sample_console_01.txt"


def test_rs41():
    f = parse_line("RS41: air_t=21.87C pres=1012.7mb rh=-999.00% hsensor_t=-999.00C hdg=16.75deg")
    assert f["rs41.air_t"] == 21.87
    assert f["rs41.pres"] == 1012.7
    assert f["rs41.rh"] == -999.0


def test_flags_and_ropc_ints():
    assert parse_line("RS41 flags: hi_t=0 ptu=1")["rs41flag.ptu"] == 1
    assert parse_line("ROPC: time=68 d300=272 alarm=0")["ropc.d300"] == 272


def test_tdlas_nan_and_space():
    f = parse_line("TDLAS parse: mixing_ratio=nan peak=1.5 cluster_1= nan cluster_2= nan")
    assert math.isnan(f["tdlasp.mixing_ratio"])
    assert math.isnan(f["tdlasp.cluster_1"])
    assert f["tdlasp.peak"] == 1.5
    assert parse_line("TDLAS: laser_temp=27.46C")["tdlas.laser_temp"] == 27.46


def test_json():
    f = parse_line('{"elapsed_s":70,"v5":5.02}')
    assert f == {"elapsed_s": 70, "v5": 5.02}


def test_unknown_and_bad_json():
    assert parse_line("") == {}
    assert parse_line("hello world") == {}
    assert parse_line("{broken") == {}


def test_sample_file_parses():
    last = {}
    for line in SAMPLE.read_text().splitlines():
        last.update(parse_line(line))
    assert "elapsed_s" in last and "rs41.air_t" in last and "tdlas.status" in last


def test_round_robin_keeps_last_values():
    from rpu_console.parser import drop_stale_round_robin
    shown = {}
    for line in SAMPLE.read_text().splitlines():
        shown.update(drop_stale_round_robin(parse_line(line)))
    assert shown["v5"] == 5.02 and shown["tsen_i"] == 16 and shown["pump_i"] == 144
    assert shown["bat_v"] == 12.43 and shown["bat_t"] == 26 and shown["bemf_v"] > 8
    # idx 0 record: nothing round-robin survives
    rec = parse_line(next(l for l in SAMPLE.read_text().splitlines()
                          if l.startswith("{") and '"round_robin_idx":0' in l))
    out = drop_stale_round_robin(rec)
    assert "v5" not in out and "bat_v" not in out and "elapsed_s" in out
