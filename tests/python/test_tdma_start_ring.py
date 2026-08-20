from argparse import Namespace

import pytest

from tools.tdma_ring_monitor.tdma_start_ring import resolve_board_ids


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


@pytest.mark.parametrize("board_ids", [
    ["ONLY"],
    [f"NODE{i}" for i in range(9)],
    ["SAME", "SAME"],
])
def test_resolve_rejects_invalid_board_sets(board_ids):
    with pytest.raises(ValueError):
        resolve_board_ids(args(board_id=board_ids))
