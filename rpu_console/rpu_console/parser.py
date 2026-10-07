"""Turn console lines into a flat dict of field name -> value."""
import json
import math
import re

KV_RE = re.compile(r"(\w+)=\s*(\S+)")
NUM_RE = re.compile(r"^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?")

# Line prefix -> field namespace. Longer prefixes first.
PREFIXES = (
    ("RS41 flags:", "rs41flag"),
    ("RS41:", "rs41"),
    ("ROPC:", "ropc"),
    ("TSEN:", "tsen"),
    ("TDLAS parse:", "tdlasp"),
    ("TDLAS:", "tdlas"),
)


def _value(text):
    """Parse '27.46C' -> 27.46, 'nan' -> nan; fall back to the raw string."""
    if text.lower() == "nan":
        return math.nan
    m = NUM_RE.match(text)
    if m:
        num = m.group(0)
        try:
            return int(num) if re.fullmatch(r"[-+]?\d+", num) else float(num)
        except ValueError:
            pass
    return text


def parse_line(line):
    """Return {field: value} for a line, or {} if it is not recognised."""
    line = line.strip()
    if not line:
        return {}
    if line.startswith("{"):
        try:
            data = json.loads(line)
        except ValueError:
            return {}
        return data if isinstance(data, dict) else {}
    for prefix, ns in PREFIXES:
        if line.startswith(prefix):
            return {f"{ns}.{k}": _value(v) for k, v in KV_RE.findall(line[len(prefix):])}
    return {}


# The JSON record rotates through these fields by round_robin_idx. Only the
# group for the current index is valid; the rest are zero/sentinel filler.
ROUND_ROBIN_FIELDS = {
    3: ("bemf_v",),
    4: ("tsen_i", "opc_i", "pump_i", "tdlas_i", "v5"),
    5: ("bat_t", "pump_t", "pcb_t", "bat_v"),
}


def drop_stale_round_robin(fields):
    """Remove round-robin fields that are not current, so the display keeps the last real value."""
    idx = fields.get("round_robin_idx")
    if idx is None:
        return fields
    current = ROUND_ROBIN_FIELDS.get(idx, ())
    stale = {k for group in ROUND_ROBIN_FIELDS.values() for k in group} - set(current)
    return {k: v for k, v in fields.items() if k not in stale}
