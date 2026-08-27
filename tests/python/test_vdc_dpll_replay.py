from __future__ import annotations

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = ROOT / "tools" / "vdc_dpll_replay"
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

from vdc_dpll_replay import (  # noqa: E402
    DIAGNOSTIC_ONLY,
    SCHEMA,
    demo_trace,
    parse_int_list,
    validate_trace,
)


def test_demo_trace_is_diagnostic_and_supports_eight_nodes() -> None:
    trace = validate_trace(demo_trace("settle", 16, 8))
    assert trace["node_count"] == 8
    assert len(trace["samples"]) == 16
    assert {row["source_node"] for row in trace["samples"]} == set(range(8))
    assert all(row["timestamp_flags"] == DIAGNOSTIC_ONLY
               for row in trace["samples"])


def test_trace_requires_monotonic_sequence() -> None:
    trace = demo_trace("settle", 8, 4)
    trace["samples"][1]["sample_seq"] = 1
    with pytest.raises(ValueError, match="strictly increasing"):
        validate_trace(trace)


def test_trace_rejects_more_than_eight_nodes() -> None:
    trace = demo_trace("settle", 9, 9)
    assert trace["schema"] == SCHEMA
    with pytest.raises(ValueError, match="2..8"):
        validate_trace(trace)


def test_parameter_scan_values_are_unique_and_support_hex() -> None:
    assert parse_int_list("32768, 0x10000,32768") == [32768, 65536]
    with pytest.raises(ValueError, match="empty"):
        parse_int_list("32768,,65536")
