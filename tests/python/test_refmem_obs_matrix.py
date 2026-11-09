"""Unit tests for the four-board OBS contract checker."""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "refmem"))

from obs_hil_matrix import INVALID_AGE, check_page  # noqa: E402


def page(rows, key=1, offset=0):
    return {"key": key, "offset": offset, "count": len(rows), "rows": rows}


def row(slot, valid, values, age=0, stale=0):
    return {"slot": slot, "valid": valid, "epoch": 1, "seq": 1,
            "age_ms": age, "stale": stale, "value": values}


def test_local_valid_peer_stale_passes():
    rows = [row(0, 1, [1, 2]), row(1, 0, [0, 0], INVALID_AGE, 1)]
    assert check_page(1, 2, page(rows), local_slot=0) == []


def test_peer_valid_is_reported():
    rows = [row(0, 1, [1, 2]), row(1, 1, [3, 4])]
    problems = check_page(1, 2, page(rows), local_slot=0)
    assert any("peer row valid=1" in problem for problem in problems)


def test_invalid_row_must_be_zero_and_stale():
    rows = [row(0, 1, [1, 2]), row(1, 0, [9, 9])]
    problems = check_page(1, 2, page(rows), local_slot=0)
    assert any("age=0" in problem for problem in problems)
    assert any("has values" in problem for problem in problems)


def test_value_width():
    rows = [row(0, 1, [1]), row(1, 0, [0, 0], INVALID_AGE, 1)]
    problems = check_page(1, 2, page(rows), local_slot=0)
    assert any("1 values != 2" in problem for problem in problems)


def test_slot_mismatch():
    rows = [row(1, 1, [1, 2])]
    problems = check_page(1, 2, page(rows), local_slot=1)
    assert any("row 0 slot=1" in problem for problem in problems)
