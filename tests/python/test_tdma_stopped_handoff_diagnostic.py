"""The stopped-handoff check must report the capture's own failure first.

The check wraps ValueError into AcceptanceError, so the assertions look at the message.

A capture that never obtained a complete board record leaves `nodes` empty while the
raw bookkeeping fields (`stopped` / `boards` / `board_records`) are complete. Checking
the field sets first then reports `nodes board set mismatch`, which points at a symptom
and cost two rounds of hunting: the real cause is the summary's own `error`.
"""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/hardware_acceptance"))

import workflow_steps as p3  # noqa: E402

BOARDS = ["0010071E65B5CB38", "FB276192BEF9CCE1", "2BD5090FE009FA2A", "A1E549202D18ED6A"]


def _summary(**overrides):
    summary = {
        "left_running": False,
        "leave_running_requested": False,
        "stopped": {b: {} for b in BOARDS},
        "boards": {b: {} for b in BOARDS},
        "board_records": {b: {} for b in BOARDS},
        "nodes": {b: {} for b in BOARDS},
        "actions": [{"action": "START", "node": b, "response": "OK"} for b in BOARDS] +
                   [{"action": "STOP_BEFORE_EXPORT", "node": b, "response": "OK"} for b in BOARDS],
    }
    summary.update(overrides)
    return summary


def test_capture_error_is_reported_instead_of_the_empty_nodes_field(tmp_path):
    summary = _summary(nodes={}, phase="TRN-03B", stage="process-image",
                       error="RuntimeError: no complete board record available after failed startup")
    with pytest.raises(p3.AcceptanceError) as caught:
        p3.validate_tdma_stopped_handoff(tmp_path, summary, BOARDS, "build")
    message = str(caught.value)
    assert "no complete board record available" in message, message
    assert "TRN-03B" in message and "process-image" in message, message
    assert "nodes board set mismatch" not in message, (
        "the symptom must not be the headline when the capture reported its own error")


def test_field_mismatch_is_still_reported_without_a_capture_error(tmp_path):
    summary = _summary(nodes={})
    with pytest.raises(p3.AcceptanceError) as caught:
        p3.validate_tdma_stopped_handoff(tmp_path, summary, BOARDS, "build")
    assert "nodes board set mismatch" in str(caught.value)
