from tools.calibration_ring_validate.calibration_bias_train import (
    aggregate,
    bias_snapshot_passed,
    parse_bias_snapshot,
    parse_loopback_snapshot,
)


def valid_snapshot() -> dict[str, int]:
    return {
        "valid": 1,
        "flags": 0x1F,
        "reject_reason": 0,
        "generation": 7,
        "sample_count": 3,
        "accepted_count": 3,
        "rejected_count": 0,
        "persona_generation": 1,
        "profile_crc32": 2,
        "topology_generation": 3,
        "first_epoch": 10,
        "last_epoch": 12,
        "mean_bias_ns": -4,
        "spread_ns": 0,
        "table_crc32": 100,
    }


def test_parse_and_accept_bias_snapshot() -> None:
    raw = "1,31,0,7,3,3,0,1,2,3,10,12,-4,0,100"
    snapshot = parse_bias_snapshot(raw)
    assert snapshot == valid_snapshot()
    assert bias_snapshot_passed(snapshot) is True


def test_parse_loopback_snapshot_preserves_raw_reject_evidence() -> None:
    raw = "0,1,250000000,4,128,1,1,3,12,92,0,0,0,0,0,0,0,0"
    snapshot = parse_loopback_snapshot(raw)
    assert snapshot["produced_words"] == 128
    assert snapshot["edge_mask"] == 0x01
    assert snapshot["reject_reason"] == 3
    assert snapshot["t1_ns"] == 92
    assert snapshot["result_valid"] == 0


def test_bias_snapshot_rejects_partial_samples() -> None:
    snapshot = valid_snapshot()
    snapshot["accepted_count"] = 2
    snapshot["rejected_count"] = 1
    assert bias_snapshot_passed(snapshot) is False


def test_aggregate_requires_every_node_and_one_generation() -> None:
    nodes = []
    for node in range(4):
        nodes.append({
            "node": node,
            "board_id": f"n{node}",
            **valid_snapshot(),
            "passed": True,
        })
    result = aggregate(nodes, ["n0", "n1", "n2", "n3"])
    assert result["passed"] is True
    nodes[3]["generation"] = 8
    result = aggregate(nodes, ["n0", "n1", "n2", "n3"])
    assert result["passed"] is False
    assert result["gate_failures"] == ["bias_generation"]
