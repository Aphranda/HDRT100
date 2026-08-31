from argparse import Namespace

import pytest

from tools.tdma_ring_monitor.tdma_start_ring import (
    persistent_sessions_enabled,
    resolve_board_ids,
)


def args(board_id=None, reference_id=None, forward_id=None):
    return Namespace(board_id=board_id,
                     reference_id=reference_id,
                     forward_id=forward_id)


def test_resolve_three_to_eight_board_ring_order():
    three = ["REF", "FWD1", "FWD2"]
    eight = [f"NODE{i}" for i in range(8)]
    assert resolve_board_ids(args(board_id=three)) == three
    assert resolve_board_ids(args(board_id=eight)) == eight


def test_resolve_legacy_two_board_ids():
    assert resolve_board_ids(
        args(reference_id="REF", forward_id="FWD")) == ["REF", "FWD"]


def test_validation_sessions_are_persistent_by_default():
    options = args()
    options.keep_open = True
    assert persistent_sessions_enabled(options) is True


def test_imported_helpers_do_not_open_persistent_sessions_implicitly():
    assert persistent_sessions_enabled(args()) is False


def test_acceptance_environment_enables_persistent_sessions(monkeypatch):
    monkeypatch.setenv("HAOFV_ACCEPTANCE_PERSISTENT_SESSIONS", "1")
    assert persistent_sessions_enabled(args()) is True
    options = args()
    options.short_open = True
    assert persistent_sessions_enabled(options) is False


def test_short_open_disables_persistent_sessions():
    options = args()
    options.short_open = True
    assert persistent_sessions_enabled(options) is False


@pytest.mark.parametrize("board_ids", [
    ["ONLY"],
    [f"NODE{i}" for i in range(9)],
    ["SAME", "SAME"],
])
def test_resolve_rejects_invalid_board_sets(board_ids):
    with pytest.raises(ValueError):
        resolve_board_ids(args(board_id=board_ids))
