"""Host tests for the T2 workflow generation continuity helpers.

Splitting the run across invocations used to start a new deployment generation
while the grid/T2 stayed bound to the previous one, so step 18 waited forever on
``cursor epoch == dep_gen``.  These helpers keep one generation for ranges that
cannot re-write the grid (steps 8/9) and never bump the reused value.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "sync_trigger"))

import t2_workflow as workflow  # noqa: E402


def test_ranges_without_grid_steps_reuse_the_recording():
    assert workflow.reuses_recorded_generation([11, 12, 18, 20]) is True
    assert workflow.reuses_recorded_generation(list(range(11, 21))) is True
    assert workflow.reuses_recorded_generation([8, 9, 11]) is False
    assert workflow.reuses_recorded_generation([9]) is False
    assert workflow.reuses_recorded_generation([]) is False


def test_generation_reuse_and_fresh_epoch(tmp_path):
    marker = tmp_path / "last_generation.json"
    workflow.save_last_generation(1234567, str(marker))
    assert workflow.load_last_generation(str(marker)) == 1234567

    # A subset that cannot write the grid keeps the recorded value untouched.
    assert workflow.generation_for_selection([11, 18], marker=str(marker)) == 1234567
    # A range containing step 8/9 starts a new epoch.
    fresh = workflow.generation_for_selection([8, 9, 18], marker=str(marker))
    assert fresh != 1234567


def test_explicit_generation_wins(tmp_path):
    marker = tmp_path / "last_generation.json"
    workflow.save_last_generation(99, str(marker))
    assert workflow.generation_for_selection(
        [11, 18], requested=4242, marker=str(marker)) == 4242


def test_missing_marker_is_none(tmp_path):
    assert workflow.load_last_generation(str(tmp_path / "absent.json")) is None
