from __future__ import annotations

import argparse
from pathlib import Path

import pytest

from tools.calibration_ring_validate.calibration_marker_train import (
    FLAG_DIAGNOSTIC_ONLY,
    MARKER_FIELDS,
    REQUIRED_FLAGS,
    build_offset_matrix,
    capture_phase_delay_cycles,
    derive_link_offset_candidates,
    matrix_trial_identity,
    parse_marker_status,
    physical_timing_budget,
    topology_matches,
    validate_ring,
)
from tools.calibration_ring_validate.calibration_marker_offsets import (
    aggregate_matrix_summary,
    aggregate_pair_summaries,
    aggregate_summaries,
    derive_link_delay_offset_plan,
)
from tools.scpi_common.scpi_serial import scpi_response_matches_command


def marker_row(slot: int, count: int = 4, *, sequence: int = 11,
               flags: int = REQUIRED_FLAGS | FLAG_DIAGNOSTIC_ONLY) -> str:
    values: dict[str, int | str] = {field: 0 for field in MARKER_FIELDS}
    values.update({
        "tag": "MARKERTRN",
        "version": 1,
        "state": 3,
        "flags": flags,
        "board_id_lo": slot + 1,
        "build_id_lo": 0x101,
        "role": 1 if slot == 0 else 2,
        "logical_slot": slot,
        "reference_slot": 0,
        "predecessor_slot": (slot - 1) % count,
        "successor_slot": (slot + 1) % count,
        "train_epoch": 7,
        "train_sequence": sequence,
        "marker_id": 3,
        "marker_codebook_id": 1,
        "marker_crc32": 0x12345678,
        "observed_crc32": 0x12345678,
        "marker_flags": 0x3F,
        "correlation_reject_reason": 0,
        "best_lag_sample": 17 + slot,
        "best_distance": slot,
        "calibration_generation": 4,
        "topology_generation": 5,
        "topology_crc32": 0x23456789,
        "profile_crc32": 0x3456789A,
        "schedule_crc32": 0x456789AB,
        "tick_resolution_ns": 4,
        "offset_sample_count": 0,
        "marker_capture_tick_lo": 1400 if slot == 0 else 1000 + slot * 100,
        "marker_forward_tick_lo": 1000 if slot == 0 else 1012 + slot * 100,
        "marker_return_tick_lo": 1400 if slot == 0 else 0,
        "forward_residence_ticks_lo": 0 if slot == 0 else 12,
        "loop_rtt_ticks_lo": 400 if slot == 0 else 0,
        "dma_capture_count": 1,
    })
    return ",".join(str(values[field]) for field in MARKER_FIELDS)


def test_parse_marker_status_field_contract() -> None:
    parsed = parse_marker_status(marker_row(0))
    assert parsed["tag"] == "MARKERTRN"
    assert parsed["marker_capture_tick"] == 1400
    assert parsed["marker_forward_tick"] == 1000
    assert parsed["marker_return_tick"] == 1400
    assert parsed["loop_rtt_ticks"] == 400
    assert parsed["marker_flags"] == 0x3F
    assert parsed["correlation_reject_reason"] == 0
    assert parsed["best_lag_sample"] == 17
    assert parsed["best_distance"] == 0


def test_parse_marker_status_rejects_field_drift() -> None:
    with pytest.raises(ValueError, match="field count"):
        parse_marker_status(marker_row(0) + ",0")
    with pytest.raises(ValueError, match="tag"):
        parse_marker_status(marker_row(0).replace("MARKERTRN", "CLKTRAIN", 1))


def test_marker_capture_save_accepts_composite_scpi_response() -> None:
    response = '"OK",17,"/cal/marker_node0_loop_g27_e51.json"'
    assert scpi_response_matches_command(
        "CALibration:MARKer:CAPTure:SAVE", response) is True


def test_marker_control_uses_calibration_inject_not_business_trigger() -> None:
    source = (Path("tools/calibration_ring_validate") /
              "calibration_marker_train.py").read_text(encoding="utf-8")
    assert "CALibration:MARKer:ARM" in source
    assert "CALibration:MARKer:INJect" in source
    assert "CALibration:MARKer:TRIGger" not in source


def test_validate_ring_accepts_common_epoch_and_order() -> None:
    result = validate_ring([parse_marker_status(marker_row(slot))
                            for slot in range(4)])
    assert result["passed"] is True
    assert result["diagnostic_only"] is True
    assert result["errors"] == []
    assert result["link_offset_model"] == (
        "marker_capture + base_half_chip + per_link_offset")
    offsets = result["link_offset_candidates"]
    assert len(offsets) == 3
    assert offsets[0]["base_half_chip_ns"] == 40
    assert offsets[0]["offset_sample_count"] == 0
    assert offsets[0]["offset_ns"] == 0
    assert offsets[0]["sample_anchor_after_marker_ns"] == 40
    assert offsets[0]["observed_best_lag_sample"] == 18


def test_link_offset_candidate_rejects_uncorrelated_lag() -> None:
    record = parse_marker_status(marker_row(1))
    record["state"] = 4
    record["correlation_reject_reason"] = 4
    candidate = derive_link_offset_candidates([record])[0]
    assert candidate["correlation_accepted"] is False
    assert candidate["offset_sample_count"] is None
    assert candidate["offset_ns"] is None
    assert candidate["sample_anchor_after_marker_ns"] is None
    assert candidate["observed_best_lag_sample"] == 18
    assert candidate["correlation_reject_name"] == "POLARITY"


def test_full_four_node_offset_matrix_is_retained() -> None:
    matrix = build_offset_matrix(4)
    assert len(matrix) == 194481
    assert matrix[0]["offset_sample_counts_by_node"] == [-10, -10, -10, -10]
    assert matrix[-1]["offset_sample_counts_by_node"] == [10, 10, 10, 10]
    current_subset = [row for row in matrix
                      if row["offset_sample_counts_by_node"][0] == 0
                      and row["offset_sample_counts_by_node"][2] == 0]
    assert len(current_subset) == 441
    assert all(len(row["offset_sample_counts_by_node"]) == 4 for row in matrix)


def test_offset_matrix_can_retain_sparse_execution_values() -> None:
    matrix = build_offset_matrix(4, (-5, 0, 5))
    assert len(matrix) == 81
    assert matrix[0]["offset_ns_by_node"] == [-20, -20, -20, -20]
    assert matrix[-1]["offset_ns_by_node"] == [20, 20, 20, 20]


def test_capture_delay_mapping_checks_codebook_specific_pio_bounds() -> None:
    assert capture_phase_delay_cycles(1, -10) == 0
    assert capture_phase_delay_cycles(1, 10) == 20
    with pytest.raises(ValueError, match="outside PIO delay"):
        capture_phase_delay_cycles(0, -10)


def test_fixed_epoch_matrix_changes_only_generation_identity() -> None:
    assert [matrix_trial_identity(
        trial_index=index, epoch_start=79, generation_start=40,
        fixed_epoch=True) for index in range(3)] == [
            (79, 40), (79, 41), (79, 42)]
    assert [matrix_trial_identity(
        trial_index=index, epoch_start=79, generation_start=40,
        fixed_epoch=False) for index in range(3)] == [
            (79, 40), (80, 40), (81, 40)]


def test_per_link_base_is_half_of_each_independent_link_delay() -> None:
    plan = derive_link_delay_offset_plan(
        [80.0, 80.0, 80.0], [28.0, 24.0, 28.0])
    assert plan["offset_sample_counts_by_link"] == [-3, -4, -3]
    assert [link["link_base_delay_ns"] for link in plan["links"]] == [
        40.0, 40.0, 40.0]
    assert all(link["resolved_window_delay_ns"] in (28.0, 24.0)
               for link in plan["links"])
    assert plan["all_offsets_pio_loadable"] is True


def test_per_link_base_preserves_quantization_residual() -> None:
    plan = derive_link_delay_offset_plan([82.0], [28.0])
    link = plan["links"][0]
    assert link["link_base_delay_ns"] == 41.0
    assert link["offset_sample_count"] == -3
    assert link["applied_offset_ns"] == -12.0
    assert link["quantization_residual_ns"] == -1.0
    assert link["resolved_window_delay_ns"] == 29.0


def test_matrix_aggregate_groups_incoming_link_by_source_node_offset() -> None:
    summary = {
        "phase": "TRN-01_FOUR_NODE_OFFSET_MATRIX",
        "node_ids_in_loop_order": ["node0", "node1"],
        "full_matrix_row_count": 9,
        "selected_row_count": 2,
        "selected_row_ids": [1, 7],
        "selection_filters_by_node": {"1": 0},
        "passed_row_ids": [],
        "trial_results": [],
    }
    for row_id, offset, distance in ((1, -1, 30), (7, 1, 10)):
        summary["trial_results"].append({
            "row_id": row_id,
            "epoch": 20 + row_id,
            "offset_sample_counts_by_node": [offset, 0],
            "accepted_nodes": [1] if distance == 10 else [],
            "passed": False,
            "capture_files": [{}, {}],
            "nodes": [{
                "node": 1,
                "incoming_link": {"source_node": 0, "destination_node": 1},
                "state": 3 if distance == 10 else 4,
                "correlation_reject_reason": 0 if distance == 10 else 6,
                "best_lag_sample": 1,
                "best_distance": distance,
                "polarity": 0,
            }],
        })
    result = aggregate_matrix_summary(summary)
    assert result["all_capture_files_saved"] is True
    effects = result["incoming_link_effects_by_source_node_offset"]
    assert [effect["source_node_offset_sample_count"] for effect in effects] == [-1, 1]
    assert [effect["accepted_count"] for effect in effects] == [0, 1]


def test_aggregate_rotated_references_covers_every_directed_link() -> None:
    board_ids = [f"board-{slot}" for slot in range(4)]
    summaries = []
    for reference in range(4):
        records = [parse_marker_status(marker_row(slot)) for slot in range(4)]
        accepted_slot = (reference + 2) % 4
        for record in records:
            slot = int(record["logical_slot"])
            record["reference_slot"] = reference
            record["role"] = 1 if slot == reference else 2
            if slot != accepted_slot:
                record["state"] = 4
                record["correlation_reject_reason"] = 4
            else:
                record["best_lag_sample"] = 1
                record["offset_sample_count"] = 1
        summaries.append({
            "board_ids_in_physical_order": board_ids,
            "reference_slot": reference,
            "epoch": 10 + reference,
            "generation": 10 + reference,
            "records": records,
        })
    result = aggregate_summaries(summaries)
    assert result["passed"] is True
    assert result["complete_link_count"] == 4
    assert all(link["offset_mean_ns"] == 4 for link in result["links"])
    assert all(link["sample_anchor_min_ns"] == 44 for link in result["links"])
    assert all(link["accepted_evidence_count"] == 1 for link in result["links"])
    assert all(link["rejected_evidence_count"] == 2 for link in result["links"])


def test_aggregate_pair_trials_keeps_four_offsets_independent() -> None:
    ring_board_ids = [f"board-{slot}" for slot in range(4)]
    summaries = []
    for source_slot, source_board_id in enumerate(ring_board_ids):
        destination_board_id = ring_board_ids[(source_slot + 1) % 4]
        records = [parse_marker_status(marker_row(slot, count=2))
                   for slot in range(2)]
        records[1]["state"] = 4
        records[1]["correlation_reject_reason"] = 6
        records[1]["best_lag_sample"] = source_slot * 5
        summaries.append({
            "board_ids_in_physical_order": [source_board_id, destination_board_id],
            "reference_slot": 0,
            "epoch": 20 + source_slot,
            "generation": 20 + source_slot,
            "records": records,
        })

    result = aggregate_pair_summaries(summaries, ring_board_ids)
    assert result["passed"] is False
    assert result["complete_trial_link_count"] == 4
    assert result["accepted_offset_link_count"] == 0
    assert result["offsets_are_independent_per_directed_link_and_node_state"] is True
    assert len(result["links"]) == 4
    assert all(link["next_offset_sweep_samples"] == list(range(-10, 11))
               for link in result["links"])
    assert all(link["next_sample_anchors_after_marker_ns"] ==
               list(range(0, 81, 4)) for link in result["links"])
    assert [link["rejected_evidence"][0]["observed_best_lag_sample"]
            for link in result["links"]] == [0, 5, 10, 15]
    assert all(link["failed_correlation_lag_is_not_offset"] is True
               for link in result["links"])


def test_validate_ring_rejects_sequence_and_flags() -> None:
    records = [parse_marker_status(marker_row(slot)) for slot in range(4)]
    records[2] = parse_marker_status(marker_row(2, sequence=12))
    records[3] = parse_marker_status(marker_row(
        3, flags=REQUIRED_FLAGS & ~(1 << 1)))
    result = validate_ring(records)
    assert result["passed"] is False
    assert "train_sequence" in result["errors"]
    assert "slot_3_flags" in result["errors"]


def test_validate_ring_rejects_premature_active_evidence() -> None:
    records = [parse_marker_status(marker_row(
        slot, flags=REQUIRED_FLAGS)) for slot in range(4)]
    result = validate_ring(records)
    assert result["passed"] is False
    assert result["diagnostic_only"] is False
    assert "diagnostic_only" in result["errors"]


def test_topology_readback_requires_exact_mapping() -> None:
    status = {
        "ring_enabled": 1,
        "ring_adapter_started": 1,
        "ring_node_count": 4,
        "ring_local_slot_id": 1,
        "ring_reference_slot_id": 0,
    }
    assert topology_matches(status, 4, 1, 0) is True
    assert topology_matches(status, 2, 1, 0) is False
    assert topology_matches(status, 4, 0, 0) is False
    assert topology_matches({**status, "ring_enabled": 0}, 4, 1, 0) is False


def test_physical_budget_separates_propagation_from_pwd() -> None:
    args = argparse.Namespace(
        codebook=1,
        observed_link_delay_ns=81.0,
        driver_propagation_typ_ns=19.0,
        driver_propagation_max_ns=41.0,
        receiver_propagation_typ_ns=36.0,
        receiver_propagation_max_ns=60.0,
        driver_pwd_max_ns=6.0,
        receiver_pwd_max_ns=6.0,
        driver_enable_max_ns=78.0,
        receiver_enable_max_ns=20.0,
    )
    budget = physical_timing_budget(args)
    assert budget["transceiver_propagation_typ_ns"] == 55.0
    assert budget["transceiver_propagation_max_ns"] == 101.0
    assert budget["transceiver_pwd_max_ns"] == 12.0
    assert budget["half_chip_after_transceiver_pwd_ns"] == 28.0
    assert budget["observed_link_delay_ns"] == 81.0
    assert budget["observed_link_delay_includes_driver_line_and_receiver"] is True
    assert budget["observed_link_delay_is_end_to_end_training_delay"] is True
    assert budget["do_not_add_component_propagation_to_observed_delay"] is True
    assert budget["component_propagation_deembedding_is_line_diagnostic_only"] is True
