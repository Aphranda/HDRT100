"""Unit tests for the four-board OBS contract checker."""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "refmem"))

from obs_hil_matrix import INVALID_AGE, check_page  # noqa: E402


def page(rows, key=1, offset=0, count=None):
    return {"key": key, "offset": offset,
            "count": len(rows) if count is None else count, "rows": rows}


def row(slot, valid, values, age=0, stale=0):
    return {"slot": slot, "valid": valid, "epoch": 1, "seq": 1,
            "age_ms": age, "stale": stale, "value": values}


def test_local_valid_peer_stale_passes():
    rows = [row(0, 1, [1, 2]), row(1, 0, [0, 0], INVALID_AGE, 1)]
    assert check_page(1, 2, page(rows), local_slot=0, count=2) == []


def test_local_invalid_is_reported():
    rows = [row(0, 0, [0, 0], INVALID_AGE, 1), row(1, 0, [0, 0], INVALID_AGE, 1)]
    assert any("local row not valid" in problem
               for problem in check_page(1, 2, page(rows), local_slot=0, count=2))


def test_local_stale_is_reported():
    rows = [row(0, 1, [1, 2], age=5, stale=1), row(1, 0, [0, 0], INVALID_AGE, 1)]
    assert any("local row stale=1" in problem
               for problem in check_page(1, 2, page(rows), local_slot=0, count=2))


def test_peer_valid_is_reported():
    rows = [row(0, 1, [1, 2]), row(1, 1, [3, 4])]
    assert any("peer row valid=1" in problem
               for problem in check_page(1, 2, page(rows), local_slot=0, count=2))


def test_invalid_row_must_be_zero_and_stale():
    rows = [row(0, 1, [1, 2]), row(1, 0, [9, 9])]
    problems = check_page(1, 2, page(rows), local_slot=0, count=2)
    assert any("age=0" in problem for problem in problems)
    assert any("has values" in problem for problem in problems)


def test_valid_flag_must_be_boolean():
    rows = [row(0, 2, [1, 2]), row(1, 0, [0, 0], INVALID_AGE, 1)]
    assert any("valid=2" in problem
               for problem in check_page(1, 2, page(rows), local_slot=0, count=2))


def test_page_header_mismatch():
    rows = [row(0, 1, [1, 2]), row(1, 0, [0, 0], INVALID_AGE, 1)]
    problems = check_page(1, 2, page(rows, key=2), local_slot=0, count=2)
    assert any("page key=2" in problem for problem in problems)
    problems = check_page(1, 2, page(rows), local_slot=0, count=3)
    assert any("page count=2" in problem for problem in problems)


def test_value_width():
    rows = [row(0, 1, [1]), row(1, 0, [0, 0], INVALID_AGE, 1)]
    problems = check_page(1, 2, page(rows), local_slot=0, count=2)
    assert any("1 values != 2" in problem for problem in problems)


def test_slot_mismatch():
    rows = [row(1, 1, [1, 2])]
    problems = check_page(1, 2, page(rows, offset=0, count=1), local_slot=1, count=1)
    assert any("row 0 slot=1" in problem for problem in problems)
