import argparse
from types import SimpleNamespace

import pytest

import tools.calibration_ring_validate.calibration_ring_topology as topology
from tools.calibration_ring_validate.calibration_ring_topology import (
    compact_pair_results,
    counter_delta,
    counter_regressed,
    render_ring_order,
    wait_started_with_transport_recovery,
)


def test_render_ring_order_from_unique_cycle():
    adjacency = {"A": ["C"], "C": ["B"], "B": ["A"]}
    assert render_ring_order(adjacency, "A", 3) == ["A", "C", "B"]


def test_render_ring_order_rejects_branch_or_open_chain():
    assert render_ring_order({"A": ["B", "C"], "B": ["A"], "C": []},
                             "A", 3) == []


def test_render_ring_order_supports_eight_node_permuted_cycle():
    adjacency = {
        "A": ["F"], "F": ["C"], "C": ["H"], "H": ["B"],
        "B": ["G"], "G": ["D"], "D": ["E"], "E": ["A"],
    }
    assert render_ring_order(adjacency, "A", 8) == [
        "A", "F", "C", "H", "B", "G", "D", "E"]
    assert render_ring_order({"A": ["B"], "B": ["C"], "C": []},
                             "A", 3) == []


def test_counter_delta_wraps():
    assert counter_delta(0xFFFFFFFE, 1) == 3
    assert not counter_regressed(0xFFFFFFFE, 1)


def test_counter_delta_rejects_snapshot_regression():
    assert counter_delta(100, 99) == 0
    assert counter_regressed(100, 99)


def test_compact_pair_results_keeps_physical_evidence_only():
    rows = compact_pair_results([{
        "driver": "A",
        "receiver": "B",
        "detected": True,
        "rx_delta": 3,
        "rx_words_delta": 99,
        "rx_edges_delta": 2,
        "magic_fail_delta": 1,
        "receiver_status": {"large": "omitted"},
        "receiver_phys": {
            "last_bad_header0": 0x54,
            "last_bad_header1": 0x44,
        },
    }])
    assert rows == [{
        "driver": "A",
        "receiver": "B",
        "detected": True,
        "rx_frames": 3,
        "rx_counter_regressed": False,
        "rx_words": 99,
        "rx_edges": 2,
        "magic_fail": 1,
        "bad_header": [0x54, 0x44, 0, 0],
    }]


def test_wait_started_uses_one_bounded_short_open_recovery(monkeypatch):
    attempts = []
    closes = []

    def fake_wait(board, args):
        attempts.append((board.address, args.keep_open))
        if args.keep_open:
            raise RuntimeError("persistent query timed out")
        return {"ring_enabled": 1, "ring_adapter_started": 1}

    monkeypatch.setattr(topology, "wait_started", fake_wait)
    monkeypatch.setattr(
        topology, "close_persistent_connections", lambda: closes.append(True))
    recoveries = []
    result = wait_started_with_transport_recovery(
        SimpleNamespace(address="B"),
        argparse.Namespace(keep_open=True, short_open=False),
        recoveries,
        "A->B",
    )
    assert result["ring_adapter_started"] == 1
    assert attempts == [("B", True), ("B", False)]
    assert closes == [True]
    assert recoveries[0]["reason"] == "persistent query timed out"
    assert recoveries[0]["action"] == "BOUNDED_SHORT_OPEN_STATUS_RETRY"
    assert recoveries[0]["recovered"] is True


def test_wait_started_does_not_loop_after_short_open_failure(monkeypatch):
    monkeypatch.setattr(
        topology, "wait_started",
        lambda board, args: (_ for _ in ()).throw(RuntimeError("timeout")))
    with pytest.raises(RuntimeError, match="timeout"):
        wait_started_with_transport_recovery(
            SimpleNamespace(address="B"),
            argparse.Namespace(keep_open=False, short_open=True),
            [],
            "A->B",
        )
