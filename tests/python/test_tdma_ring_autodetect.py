from tools.tdma_ring_monitor.tdma_ring_autodetect import (
    compact_pair_results,
    counter_delta,
    render_ring_order,
)


def test_render_ring_order_from_unique_cycle():
    adjacency = {"A": ["C"], "C": ["B"], "B": ["A"]}
    assert render_ring_order(adjacency, "A", 3) == ["A", "C", "B"]


def test_render_ring_order_rejects_branch_or_open_chain():
    assert render_ring_order({"A": ["B", "C"], "B": ["A"], "C": []},
                             "A", 3) == []
    assert render_ring_order({"A": ["B"], "B": ["C"], "C": []},
                             "A", 3) == []


def test_counter_delta_wraps():
    assert counter_delta(0xFFFFFFFE, 1) == 3


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
        "rx_words": 99,
        "rx_edges": 2,
        "magic_fail": 1,
        "bad_header": [0x54, 0x44, 0, 0],
    }]
