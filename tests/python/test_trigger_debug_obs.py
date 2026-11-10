"""Reads with a REFMEM-READ-01 key go through ControlPlane (M5/M6)."""

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "domains" / "trigger" / "trigger_debug_demo"))

from obs_read import (  # noqa: E402
    ControlPlaneTransport,
    format_rows,
    local_row,
    read_boot_plan,
    read_key,
    read_node_health,
    read_ring_status,
    read_switch_obs,
)


class FakeTransport:
    def __init__(self, response: str) -> None:
        self.response = response

    def execute(self, command: str) -> str:
        return self.response


def obs_page(key: int, offset: int, rows: list[list[int]]) -> str:
    parts: list[int] = [key, offset, len(rows)]
    for row in rows:
        parts.extend(row)
    return ",".join(str(value) for value in parts)


def obs_row(slot: int, values: tuple[int, ...], *, valid: int = 1,
            stale: int = 0, age: int = 0) -> list[int]:
    return [slot, valid, 1, 7, age, stale, *values]


RING_ROW = (1, 1, 1, 1, 4, 0)
BOOT_ROW = (1, 0, 1, 0, 2)
NODE_ROW = (1, 100, 0, 0)


def test_read_switch_obs_formats_per_slot_rows():
    response = "1,0,2,0,1,1,1,0,0,1,1,1,1,1,183,526,0,4,4"
    text = read_switch_obs(FakeTransport(response), 1, 0, 2)
    assert "key=1" in text
    assert "slot=1" in text
    assert "stale=0" in text
    assert "value=4,4" in text


def test_read_switch_obs_rejects_bad_key():
    with pytest.raises(Exception):
        read_switch_obs(FakeTransport('-200,"OBS_KEY"'), 1)


def test_local_row_skips_stale_peer_rows():
    page = read_key(FakeTransport(obs_page(6, 0, [
        obs_row(0, RING_ROW),
        obs_row(1, (0, 0, 0, 0, 0, 0), valid=0, stale=1, age=0xFFFFFFFF),
    ])), 6)
    row = local_row(page)
    assert row is not None and row["slot"] == 0 and row["valid"] == 1
    assert [r["valid"] for r in page["rows"]] == [1, 0]


def test_read_ring_status_maps_key6_schema():
    page = obs_page(6, 0, [
        obs_row(0, RING_ROW, age=183),
        obs_row(1, (0, 0, 0, 0, 0, 0), valid=0, stale=1, age=0xFFFFFFFF),
    ])
    data = read_ring_status(FakeTransport(page))
    assert data["valid"] == 1 and data["slot"] == 0
    assert (data["enabled"], data["up"], data["down"], data["adapter"]) == \
        (1, 1, 1, 1)
    assert data["nodes"] == 4 and data["local_slot"] == 0
    assert data["stale"] == 0 and data["age_ms"] == 183
    assert len(data["rows"]) == 2


def test_read_boot_plan_maps_key5_schema():
    data = read_boot_plan(FakeTransport(obs_page(5, 0, [obs_row(0, BOOT_ROW)])))
    assert (data["action"], data["fail_step"], data["boot_done"],
            data["half_ring"], data["profile_source"]) == (1, 0, 1, 0, 2)


def test_read_node_health_returns_per_slot_rows():
    page = obs_page(8, 0, [
        obs_row(0, NODE_ROW),
        obs_row(1, (0, 0, 0, 0), valid=0, stale=1, age=0xFFFFFFFF),
    ])
    rows = read_node_health(FakeTransport(page))
    assert [row["slot"] for row in rows] == [0, 1]
    assert rows[0]["valid"] == 1 and rows[0]["heartbeat"] == 100
    assert rows[1]["valid"] == 0 and rows[1]["stale"] == 1


def test_format_rows_names_schema_columns():
    text = format_rows({
        "key": 6, "offset": 0, "count": 1,
        "rows": [{"slot": 0, "valid": 1, "epoch": 1, "seq": 7,
                  "age_ms": 0, "stale": 0,
                  "value": [1, 1, 1, 1, 4, 0]}],
    }, ("enabled", "up", "down", "adapter", "nodes", "local_slot"))
    assert "key=6" in text
    assert "slot=0 valid=1" in text
    assert "enabled=1" in text and "local_slot=0" in text


def test_transport_routes_refmem_queries_through_control_plane():
    raw = FakeTransport("1,0,1,0,1,1,1,0,0,1,1")
    wrapped = ControlPlaneTransport(raw)
    assert wrapped.execute("SYSTem:REFMEM:OBS? 1,0,1").startswith("1,0,1")


def test_transport_routes_parameterized_obs_reads():
    # An argument-carrying OBS read is a read too: the wrapper must use
    # ControlPlane.query, which turns ``-200`` into a rejected read.
    wrapped = ControlPlaneTransport(FakeTransport('-200,"OBS_KEY"'))
    with pytest.raises(Exception):
        wrapped.execute("SYSTem:REFMEM:OBS? 6,0,1")


def test_transport_passes_non_refmem_commands():
    raw = FakeTransport("OK")
    assert ControlPlaneTransport(raw).execute("*IDN?") == "OK"


def test_transport_routes_ctrl_execute_through_write():
    raw = FakeTransport("QUEUED,7")
    wrapped = ControlPlaneTransport(raw)
    assert wrapped.execute("SYSTem:CTRL:EXECute 45,15,15,1,5") == "QUEUED,7"
