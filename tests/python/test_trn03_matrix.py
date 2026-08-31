from __future__ import annotations

import itertools
from pathlib import Path

import pytest

from tools.calibration_ring_validate.trn03_matrix import (
    BOARD_SYS_CLOCK_HZ,
    NORMAL_PIO_BIT_CYCLES,
    NORMAL_PIO_PERSONA,
    build_matrix,
    derive_data_refinement_candidates,
    profile_crc32,
)
from tools.calibration_ring_validate.trn03_stage import (
    FLIGHT_DATA_REARM_SAMPLES,
    FLIGHT_SCK_REARM_SAMPLES,
    load_config,
    sck_replay_phase_is_safe,
)


ROOT = Path(__file__).resolve().parents[2]


def evidence(level: int = 9) -> tuple[dict, dict]:
    profile = profile_crc32(level)
    generation = 103
    identity = {
        "calibration_generation": [generation],
        "topology_generation": [21],
        "topology_crc32": [100],
        "profile_crc32": [profile],
        "schedule_crc32": [200],
        "sample_period_ns": [4],
    }
    data_links = [{
        "link": link,
        "marker_source_node": link,
        "marker_destination_node": (link + 1) % 4,
        "data_source_node": (link + 1) % 4,
        "data_destination_node": link,
        "marker_direction": "forward",
        "data_direction": "reverse",
        "trial_count": 3,
        "accepted_count": 3,
        "offset_histogram": {"0": 3},
        "offset_span_sample": 0,
        "gate_failures": [],
        "passed": True,
    } for link in range(4)]
    trials = []
    for link in range(4):
        for repeat in range(1, 4):
            trials.append({
                "link": link,
                "repeat_index": repeat,
                "passed": True,
                "link_marker_offset_sample": [-1, 0, 1, 1][link],
                "calibrated_data_offset_sample_count": 5,
                "source": {
                    "training_window_start_ns": 60,
                    "training_window_end_ns": 60,
                    "marker_to_data_samples": 1000,
                    "expected_sample_count": 3210,
                    "guard_sample_count": 0,
                    "link_base_delay_ns": 40,
                },
            })
    data = {
        "phase": "TRN-02D_REPEAT_MATRIX",
        "passed": True,
        "node_ids_in_loop_order": ["n0", "n1", "n2", "n3"],
        "repeats": 3,
        "max_offset_span_sample": 1,
        "matrix": {
            "passed": True,
            "expected_trial_count": 12,
            "trial_count": 12,
            "accepted_count": 12,
            "identity": identity,
            "identity_failures": [],
            "gate_failures": [],
            "links": data_links,
        },
        "trials": trials,
    }
    residence_identity = dict(identity)
    residence_identity["tick_resolution_ns"] = \
        residence_identity.pop("sample_period_ns")
    residence = {
        "phase": "TRN-01_RESIDENCE_MATRIX",
        "passed": True,
        "node_ids_in_loop_order": ["n0", "n1", "n2", "n3"],
        "trial_count": 4,
        "matrix": {
            "passed": True,
            "identity": residence_identity,
            "failures": [],
            "links": [{
                "link_index": link,
                "source_node": link,
                "destination_node": (link + 1) % 4,
                "forward_residence_ticks": [1, 1, 1],
                "forward_residence_span_ticks": 0,
                "selected_forward_residence_ticks": 1,
                "repeat_count": 3,
                "passed": True,
            } for link in range(4)],
            "loops": [{"node": node, "loop_delay_ticks": [10 + node]}
                      for node in range(4)],
        },
    }
    return data, residence


def sck_evidence(data: dict) -> dict:
    identity = dict(data["matrix"]["identity"])
    topology_generation = identity.pop("topology_generation")[0]
    identity["topology_generation_by_node"] = {
        str(node): [topology_generation] for node in range(4)}
    return {
        "phase": "TRN-SCK_OFFSET_MATRIX",
        "passed": True,
        "node_ids_in_loop_order": list(data["node_ids_in_loop_order"]),
        "training_parameters": {
            "link_base_delay_ns_by_link": [40, 40, 40, 40],
        },
        "matrix": {
            "passed": True,
            "identity": identity,
            "offset_matrix": {
                "schema": "HAOFV_SCK_OFFSET_MATRIX_V2",
                "candidate_values_by_node": [[0], [0], [0], [0]],
                "full_matrix_row_count": 1,
                "active_row_id": 0,
                "rows": [{
                    "row_id": 0,
                    "sck_offset_sample_counts_by_node": [0, 0, 0, 0],
                }],
            },
        },
    }


def test_build_matrix_derives_residence_and_budget(tmp_path: Path) -> None:
    data, residence = evidence()
    with pytest.raises(ValueError, match="SCK replay phase cannot re-arm"):
        build_matrix(9, data, residence)

    data, residence = evidence(7)
    matrix = build_matrix(7, data, residence)
    link0 = matrix["links"][0]
    assert link0["marker_to_data_cycles"] == 240
    assert link0["forward_residence_cycles"] == 1
    assert link0["rx_arm_lead_cycles"] == 4
    assert link0["codeword_cycles"] == 771
    assert link0["guard_cycles"] == 0
    assert link0["loop_delay_cycles"] == 3
    assert link0["link_budget_cycles"] == 1019
    assert link0["marker_offset_sample_count"] == -1
    assert link0["sck_offset_sample_count"] == 0
    assert link0["data_offset_sample_count"] == 5
    assert link0["sample_period_ns"] == 4
    assert link0["link_base_delay_ns"] == 40
    assert link0["marker_phase_delay_cycles"] == 9
    assert link0["data_phase_delay_cycles"] == 15
    assert link0["sck_phase_delay_cycles"] == 10
    assert matrix["offset_matrix"]["full_matrix_row_count"] == 1
    assert matrix["offset_matrix"]["rows"][0][
        "marker_offset_sample_counts_by_node"] == [1, -1, 0, 1]
    assert matrix["offset_matrix"]["rows"][0][
        "sck_offset_sample_counts_by_node"] == [0, 0, 0, 0]
    assert link0["marker_source_node"] == 0
    assert link0["marker_destination_node"] == 1
    assert link0["data_source_node"] == 1
    assert link0["data_destination_node"] == 0
    assert link0["source_evidence"]["forward_residence_ticks"] == [1, 1, 1]
    path = tmp_path / "matrix.json"
    path.write_text(__import__("json").dumps(matrix), encoding="utf-8")
    assert load_config(path)["node_count"] == 4


def test_build_matrix_requires_independent_sck_v2_schema() -> None:
    data, residence = evidence(7)
    sck = sck_evidence(data)
    matrix = build_matrix(7, data, residence, sck=sck)
    assert matrix["offset_matrix"]["rows"][0][
        "sck_offset_sample_counts_by_node"] == [0, 0, 0, 0]

    sck["matrix"]["offset_matrix"]["schema"] = \
        "HAOFV_SCK_OFFSET_MATRIX_V1"
    with pytest.raises(ValueError, match="independently trained V2"):
        build_matrix(7, data, residence, sck=sck)


def test_build_matrix_retimes_unsafe_observed_sck_row() -> None:
    data, residence = evidence(7)
    sck = sck_evidence(data)
    offset = sck["matrix"]["offset_matrix"]
    candidates = [[1], [0, 1], [0, 1], [0, 1]]
    rows = [
        {"row_id": row_id,
         "sck_offset_sample_counts_by_node": list(values)}
        for row_id, values in enumerate(itertools.product(*candidates))
    ]
    offset["candidate_values_by_node"] = candidates
    offset["full_matrix_row_count"] = len(rows)
    offset["active_row_id"] = len(rows) - 1
    offset["rows"] = rows

    matrix = build_matrix(7, data, residence, sck=sck)
    selection = matrix["derivation"]["sck_replay_selection"]
    assert selection["requested_offset_sample_counts_by_node"] == [1, 1, 1, 1]
    assert selection["selected_offset_sample_counts_by_node"] == [1, 1, 1, 1]
    assert selection["requested_row_replay_safe"] is True
    assert selection["selection_reason"] == "observed_row_replay_safe"
    assert matrix["offset_matrix"]["active_row_id"] == 7
    assert matrix["offset_matrix"]["full_matrix_row_count"] == 8


def test_build_matrix_retains_adjacent_data_refinement_as_non_active_row(
        tmp_path: Path) -> None:
    data, residence = evidence(7)
    matrix = build_matrix(
        7, data, residence,
        data_refinement_candidates={0: [6]})
    offset_matrix = matrix["offset_matrix"]
    assert offset_matrix["full_matrix_row_count"] == 2
    active = offset_matrix["rows"][offset_matrix["active_row_id"]]
    assert active["data_offset_sample_counts_by_node"] == [5, 5, 5, 5]
    refinement = next(
        row for row in offset_matrix["rows"]
        if row["data_offset_sample_counts_by_node"] == [6, 5, 5, 5])
    assert refinement["row_id"] != offset_matrix["active_row_id"]
    assert matrix["derivation"]["data_refinement_candidates_by_node"] == {
        "0": [6]}
    path = tmp_path / "matrix.json"
    path.write_text(__import__("json").dumps(matrix), encoding="utf-8")
    replay = load_config(path, offset_row_id=refinement["row_id"])
    assert replay["links"][0]["data_offset_sample_count"] == 6


def test_build_matrix_rejects_unbounded_data_refinement() -> None:
    data, residence = evidence(7)
    with pytest.raises(ValueError, match="must be adjacent"):
        build_matrix(
            7, data, residence,
            data_refinement_candidates={0: [7]})
    with pytest.raises(ValueError, match="already observed"):
        build_matrix(
            7, data, residence,
            data_refinement_candidates={0: [5]})


def data_refinement_evidence(data: dict, configured_offset: int = 6) -> dict:
    identity = dict(data["matrix"]["identity"])
    identity["topology_generation"] = [99]
    return {
        "phase": "TRN-02B_SELECTED_LINK_REPEAT",
        "passed": True,
        "node_ids_in_loop_order": list(data["node_ids_in_loop_order"]),
        "repeats": 32,
        "selected_links": [0],
        "training_parameters": {
            "node_data_offset_samples": [configured_offset, 5, 5, 5],
        },
        "matrix": {
            "passed": True,
            "accepted_count": 32,
            "identity": identity,
            "identity_failures": [],
            "gate_failures": [],
            "links": [{
                "link": 0,
                "data_destination_node": 0,
                "passed": True,
                "calibrated_offset_histogram": {"4": 9, "5": 23},
            }],
        },
    }


def test_derive_data_refinement_candidates_requires_accepted_evidence() -> None:
    data, _ = evidence(7)
    refinement = data_refinement_evidence(data)
    candidates, records = derive_data_refinement_candidates(
        data, [("refinement.json", refinement)])
    assert candidates == {0: [6]}
    assert records[0]["path"] == "refinement.json"
    assert records[0]["accepted_count"] == 32
    assert records[0]["calibrated_offset_histogram"] == {"4": 9, "5": 23}

    refinement["matrix"]["identity"]["profile_crc32"] = [1]
    with pytest.raises(ValueError, match="profile_crc32 mismatch"):
        derive_data_refinement_candidates(
            data, [("refinement.json", refinement)])


def test_derive_data_refinement_candidates_rejects_failed_summary() -> None:
    data, _ = evidence(7)
    refinement = data_refinement_evidence(data)
    refinement["passed"] = False
    with pytest.raises(ValueError, match="not accepted"):
        derive_data_refinement_candidates(
            data, [("refinement.json", refinement)])


def test_sck_rearm_boundary_uses_raw_sample_grid() -> None:
    assert sck_replay_phase_is_safe(
        phase_delay_cycles=10, baud_hz=10_000_000,
        sample_period_ns=4, destination_node=1)
    assert sck_replay_phase_is_safe(
        phase_delay_cycles=11, baud_hz=10_000_000,
        sample_period_ns=4, destination_node=1)
    assert not sck_replay_phase_is_safe(
        phase_delay_cycles=1, baud_hz=10_000_000,
        sample_period_ns=4, destination_node=1)
    assert sck_replay_phase_is_safe(
        phase_delay_cycles=11, baud_hz=10_000_000,
        sample_period_ns=4, destination_node=0)


def test_build_matrix_rejects_mismatched_residence_identity() -> None:
    data, residence = evidence()
    residence["matrix"]["identity"]["schedule_crc32"] = [201]
    with pytest.raises(ValueError, match="residence_schedule_crc32_mismatch"):
        build_matrix(9, data, residence)


def test_build_matrix_rejects_evidence_loaded_on_wrong_physical_link() -> None:
    data, residence = evidence(7)
    data["matrix"]["links"][1]["marker_destination_node"] = 3
    with pytest.raises(ValueError, match="direction does not match loop order"):
        build_matrix(7, data, residence)


def test_matrix_runtime_facts_match_code_sources() -> None:
    board = (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(
        encoding="utf-8")
    pio = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    profile = (ROOT / "components/tdma/src/tdma_operating_profile.c").read_text(
        encoding="utf-8")
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(
        encoding="utf-8")
    assert f"BOARD_SYS_CLOCK_HZ  {BOARD_SYS_CLOCK_HZ}u" in board
    assert f"const uint32_t bit_cycles = {NORMAL_PIO_BIT_CYCLES}u" in pio
    assert f"TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL = {NORMAL_PIO_PERSONA}u" in header
    assert (f"TDMA_PIO_SPI_FLIGHT_SCK_REARM_CYCLES "
            f"{FLIGHT_SCK_REARM_SAMPLES}u" in header)
    assert (f"TDMA_PIO_SPI_FLIGHT_DATA_REARM_CYCLES "
            f"{FLIGHT_DATA_REARM_SAMPLES}u" in header)
    for level, row in ((7, "{10000000u, 1000000u, 4096u, 0u}"),
                       (8, "{25000000u, 1000000u, 4096u, 0u}"),
                       (9, "{30000000u, 1000000u, 4096u, 0u}")):
        assert row in profile
        assert profile_crc32(level) != 0
