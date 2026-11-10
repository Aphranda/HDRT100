"""REFTMEM unified read path for the demo pages (REFTMEM-READ-01 / M5/M6).

Every read that has an OBS key goes through ``ControlPlane.read`` so the page
sees the same per-slot rows as the CLI and the host tests.  Keys, schemas and
freshness semantics come from ``config/refmem/read_catalog.yaml`` and the
REFMEM guide (``docs/refmem/REFMEM_UNIFIED_CONTROL_COMMANDS.md``); this module
only names the columns so the UI does not index raw lists.

Rich queries that have no OBS key yet (``GUIDE:PLAN?`` 27 fields, ``READ:*``,
``RING:STATus?`` detail) stay on the local SCPI path and must be labelled as
diagnostic exceptions where they are offered.
"""

from __future__ import annotations

import sys
from pathlib import Path

#: REFMEM-READ-01 key ids (config/refmem/read_catalog.yaml).
OBS_KEYS = {
    "switch1": 1,
    "switch2": 2,
    "trigger_state": 3,
    "guide_plan": 4,
    "boot_plan": 5,
    "ring_status": 6,
    "cal_status": 7,
    "node_health": 8,
}

#: ``offset``/``count`` are slot indexes; the four-board ring has four slots.
OBS_NODE_COUNT = 4

SWITCH_SCHEMA = ("value", "desired")
TRIGGER_STATE_SCHEMA = ("state", "run_state", "epoch")
GUIDE_PLAN_SCHEMA = ("action", "generation", "status", "trials_done",
                     "passed", "failed", "abort_site")
BOOT_PLAN_SCHEMA = ("action", "fail_step", "boot_done", "half_ring",
                    "profile_source")
RING_STATUS_SCHEMA = ("enabled", "up", "down", "adapter", "nodes",
                      "local_slot")
CAL_STATUS_SCHEMA = ("persisted_valid", "generation", "reject_reason")
NODE_HEALTH_SCHEMA = ("state", "heartbeat", "stale_count", "fault_code")


def _control_plane(transport):
    refmem = Path(__file__).resolve().parents[3] / "refmem"
    if str(refmem) not in sys.path:
        sys.path.insert(0, str(refmem))
    from control_plane import ControlPlane

    return ControlPlane(transport)


# --------------------------------------------------------------- raw key page
def read_key(transport, key: int, offset: int = 0,
             count: int = OBS_NODE_COUNT) -> dict:
    """One ``SYSTem:REFMEM:OBS?`` page: ``key/offset/count/rows``."""
    return _control_plane(transport).read(int(key), int(offset), int(count))


def local_row(page: dict) -> dict | None:
    """The coordinator's own row (the only row a local provider publishes)."""
    for row in page.get("rows") or ():
        if row.get("valid"):
            return row
    return None


def _row_meta(row: dict) -> dict:
    # ``row_epoch``/``row_seq`` are the row-header publish counters; they are
    # not the schema ``epoch`` (e.g. trigger run id) and must not collide.
    return {
        "slot": row.get("slot", 0),
        "valid": row.get("valid", 0),
        "row_epoch": row.get("epoch", 0),
        "row_seq": row.get("seq", 0),
        "age_ms": row.get("age_ms", 0xFFFFFFFF),
        "stale": row.get("stale", 1),
    }


def _row_values(row: dict, schema: tuple[str, ...]) -> dict:
    words = list(row.get("value") or ())
    return {name: (words[index] if index < len(words) else 0)
            for index, name in enumerate(schema)}


def _named_page(transport, key: int, schema: tuple[str, ...]) -> dict:
    page = read_key(transport, key)
    row = local_row(page)
    result = {"key": key, "rows": page.get("rows") or [], "valid": 0,
              "slot": -1, "row_epoch": 0, "row_seq": 0,
              "age_ms": 0xFFFFFFFF, "stale": 1}
    result.update({name: 0 for name in schema})
    if row is not None:
        result.update(_row_meta(row))
        result.update(_row_values(row, schema))
    return result


# ------------------------------------------------------------ named helpers
def read_ring_status(transport) -> dict:
    """key 6: ``enabled/up/down/adapter/nodes/local_slot`` for the local board."""
    return _named_page(transport, OBS_KEYS["ring_status"], RING_STATUS_SCHEMA)


def read_boot_plan(transport) -> dict:
    """key 5: the boot service verdict fields (five of the 20 PLAN? fields)."""
    return _named_page(transport, OBS_KEYS["boot_plan"], BOOT_PLAN_SCHEMA)


def read_guide_plan(transport) -> dict:
    """key 4: the guide round verdict (seven of the 27 PLAN? fields)."""
    return _named_page(transport, OBS_KEYS["guide_plan"], GUIDE_PLAN_SCHEMA)


def read_trigger_state(transport) -> dict:
    """key 3: ``state/run_state/epoch`` (epoch here is the trigger run id)."""
    return _named_page(transport, OBS_KEYS["trigger_state"],
                       TRIGGER_STATE_SCHEMA)


def read_cal_status(transport) -> dict:
    """key 7: calibration training-store status."""
    return _named_page(transport, OBS_KEYS["cal_status"], CAL_STATUS_SCHEMA)


def read_node_health(transport, offset: int = 0,
                     count: int = OBS_NODE_COUNT) -> list[dict]:
    """key 8: per-slot node health rows (local valid, peers stale)."""
    page = read_key(transport, OBS_KEYS["node_health"], offset, count)
    rows = []
    for row in page.get("rows") or ():
        entry = _row_meta(row)
        entry.update(_row_values(row, NODE_HEALTH_SCHEMA))
        rows.append(entry)
    return rows


def format_rows(page: dict, schema: tuple[str, ...] | None = None) -> str:
    """Human-readable OBS page for log/readback text."""
    lines = [f"key={page.get('key')} offset={page.get('offset')} "
             f"count={page.get('count')}"]
    for row in page.get("rows") or ():
        meta = _row_meta(row)
        body = ""
        if schema:
            values = _row_values(row, schema)
            body = " " + " ".join(f"{name}={values[name]}" for name in schema)
        lines.append(f"slot={meta['slot']} valid={meta['valid']} "
                     f"age={meta['age_ms']} stale={meta['stale']}{body}")
    return "\n".join(lines)


# ------------------------------------------------------------------ pages
def read_switch_obs(transport, key: int, offset: int = 0,
                    count: int = OBS_NODE_COUNT) -> str:
    """Read one switch key (1/2) and format the per-slot rows for the page."""
    page = read_key(transport, key, offset, count)
    text = f"key={page['key']} offset={page['offset']} count={page['count']}"
    for row in page["rows"]:
        values = ",".join(str(value) for value in row["value"])
        text += (f"; slot={row['slot']} valid={row['valid']}"
                 f" age={row['age_ms']} stale={row['stale']} value={values}")
    return text


class ObsReaderMixin:
    """Page mixin: OBS reads go through the app's Dispatcher-serialized path."""

    def read_obs(self, key: int, offset: int = 0,
                 count: int = OBS_NODE_COUNT) -> dict:
        app = getattr(self, "app", None)
        reader = getattr(app, "read_obs", None)
        if reader is None:
            raise RuntimeError("未连接：OBS 读需要设备会话")
        return reader(key, offset, count)


class ControlPlaneTransport:
    """Wrap the demo transport so REFMEM read queries share ControlPlane.

    Writes and non-REFTMEM commands pass through unchanged; every
    ``SYSTem:REFMEM:...?`` read goes through ``ControlPlane.query`` so the
    pages, CLI and tests use one host read path (M5).
    """

    def __init__(self, raw) -> None:
        self._raw = raw

    def execute(self, command: str) -> str:
        stripped = command.strip()
        header = stripped.split(" ", 1)[0]
        # Reads may carry arguments (``OBS? 6,0,4``), so test the header for
        # the query marker instead of requiring the whole line to end in "?".
        if stripped.startswith("SYSTem:REFMEM:") and header.endswith("?"):
            return _control_plane(self._raw).query(stripped)
        if stripped.startswith("SYSTem:CTRL:EXECute"):
            fields = stripped.split(None, 1)
            if len(fields) == 2:
                values = [int(piece) for piece in fields[1].split(",")
                          if piece.strip()]
                plane = _control_plane(self._raw)
                seq = plane.write(values[0], values[3:], values[1], values[2])
                return f"QUEUED,{seq}"
        return self._raw.execute(command)

    def __getattr__(self, name):
        return getattr(self._raw, name)
