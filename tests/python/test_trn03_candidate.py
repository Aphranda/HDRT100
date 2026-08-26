from __future__ import annotations

from copy import deepcopy

from tools.calibration_ring_validate.trn03_candidate import (
    build_candidate,
    refresh_candidate_crc32,
)


BOARD_ORDER = ["n0", "n1", "n2", "n3"]


def config() -> dict:
    links = []
    for link in range(4):
        links.append({
            "link_index": link,
            "marker_to_data_cycles": 10,
            "forward_residence_cycles": 1,
            "rx_arm_lead_cycles": 2,
            "codeword_cycles": 20,
            "guard_cycles": 0,
            "loop_delay_cycles": 3,
            "link_budget_cycles": 36,
            "sample_period_ns": 4,
            "link_base_delay_ns": 40,
            "marker_destination_node": (link + 1) % 4,
            "data_destination_node": link,
            "marker_phase_delay_cycles": [9, 10, 11, 11][link],
            "sck_phase_delay_cycles": 10,
            "data_phase_delay_cycles": 15,
        })
    return {
        "node_count": 4,
        "calibration_generation": 210,
        "topology_generation": 3,
        "topology_crc32": 100,
        "profile_crc32": 200,
        "schedule_crc32": 300,
        "baud_hz": 10_000_000,
        "links": links,
        "offset_matrix": {
            "active_row_id": 0,
            "rows": [{
                "row_id": 0,
                "marker_offset_sample_counts_by_node": [1, -1, 0, 1],
                "sck_offset_sample_counts_by_node": [0, 0, 0, 0],
                "data_offset_sample_counts_by_node": [5, 5, 5, 5],
            }],
        },
    }


def closed_loop(rtt: int = 400) -> dict:
    selected_row = deepcopy(config()["offset_matrix"]["rows"][0])
    return {
        "passed": True,
        "stage": "process-image",
        "calibration_generation": 210,
        "offset_row_id": 0,
        "offset_row": selected_row,
        "board_ids_in_physical_node_order": list(BOARD_ORDER),
        "ring_capture": {"capture_completed": True},
        "ring_analysis": {"passed": True},
        "nodes": {
            board: {
                "node_index": node,
                "runtime_after": {
                    "ring_feedback_round_trip_ns": rtt if node == 0 else 0,
                },
            }
            for node, board in enumerate(BOARD_ORDER)
        },
    }


def p3() -> dict:
    trials = []
    for link, source in enumerate(BOARD_ORDER):
        destination = BOARD_ORDER[(link + 1) % 4]
        for repeat in range(3):
            trials.append({
                "source": source,
                "destination": destination,
                "frequency_hz": 10_000_000,
                "signal_group": 0,
                "repeat_index": repeat + 1,
                "path_sum_ns": 180,
                "passed": True,
            })
    return {
        "passed": True,
        "board_ids_in_physical_order": list(BOARD_ORDER),
        "trials": trials,
    }


def bias_set() -> dict:
    return {
        "schema": "HAOFV_CALIBRATION_BIAS_SET_V1",
        "passed": True,
        "board_ids_in_physical_node_order": list(BOARD_ORDER),
        "nodes": [{
            "node": node,
            "board_id": board,
            "valid": 1,
            "flags": 0x1F,
            "generation": 7,
            "sample_count": 3,
            "accepted_count": 3,
            "mean_bias_ns": 10,
            "spread_ns": 0,
            "table_crc32": 0x1000 + node,
        } for node, board in enumerate(BOARD_ORDER)],
    }


def test_candidate_accepts_replayed_hardware_evidence() -> None:
    result = build_candidate(
        config(), [closed_loop(), closed_loop(404)], p3(), bias_set(),
        evidence_ages_seconds=[1, 2, 3, 4])
    assert result["passed"] is True
    assert result["state"] == "active_candidate"
    assert result["active"] is False
    assert result["gate_failures"] == []
    assert result["bias_generation"] == 7
    assert [link["delay_ns"] for link in result["path_links"]] == [80] * 4
    assert all(link["path_base_residual_ns"] == 0
               for link in result["path_links"])
    assert result["candidate_crc32"] != 0
    original_crc = result["candidate_crc32"]
    result["sources"] = [{"sha256": "abc"}]
    assert refresh_candidate_crc32(result) != original_crc


def test_candidate_rejects_missing_bias_and_loop_rtt() -> None:
    bad_bias = bias_set()
    bad_bias["passed"] = False
    bad_bias["nodes"][2]["valid"] = 0
    result = build_candidate(
        config(), [closed_loop(0), closed_loop(0)], p3(), bad_bias)
    assert result["passed"] is False
    assert result["state"] == "rejected_staging"
    assert "bias_set_gate" in result["gate_failures"]
    assert "node2:bias" in result["gate_failures"]
    assert "closed_loop0:loop_rtt" in result["gate_failures"]


def test_candidate_rejects_cycle_replay_and_stale_evidence() -> None:
    bad_config = config()
    bad_config["links"][1]["link_budget_cycles"] += 1
    result = build_candidate(
        bad_config, [closed_loop(), closed_loop()], p3(), bias_set(),
        evidence_ages_seconds=[1, 4000],
        maximum_evidence_age_seconds=3600)
    assert result["passed"] is False
    assert "link1:cycle_budget" in result["gate_failures"]
    assert "freshness" in result["gate_failures"]


def test_candidate_rejects_path_base_residual() -> None:
    bad_p3 = deepcopy(p3())
    for trial in bad_p3["trials"]:
        if trial["source"] == "n1":
            trial["path_sum_ns"] = 200
    result = build_candidate(
        config(), [closed_loop(), closed_loop()], bad_p3, bias_set(),
        maximum_path_base_residual_ns=4)
    assert result["passed"] is False
    assert "link1:path_base_residual" in result["gate_failures"]


def test_candidate_rejects_closed_loop_from_different_offset_row() -> None:
    wrong_row = closed_loop()
    wrong_row["offset_row_id"] = 31
    wrong_row["offset_row"]["row_id"] = 31
    result = build_candidate(
        config(), [wrong_row, closed_loop()], p3(), bias_set())
    assert result["passed"] is False
    assert "closed_loop0:offset_row_id" in result["gate_failures"]
    assert "closed_loop0:offset_row" in result["gate_failures"]
