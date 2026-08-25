from __future__ import annotations

from pathlib import Path

import pytest

from tools.calibration_ring_validate.trn03_matrix import (
    BOARD_SYS_CLOCK_HZ,
    NORMAL_PIO_BIT_CYCLES,
    NORMAL_PIO_PERSONA,
    build_matrix,
    profile_crc32,
)
from tools.calibration_ring_validate.trn03_stage import load_config


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
                "source": {
                    "training_window_start_ns": 60,
                    "training_window_end_ns": 60,
                    "marker_to_data_samples": 1000,
                    "expected_sample_count": 3210,
                    "guard_sample_count": 0,
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


def test_build_matrix_derives_residence_and_budget(tmp_path: Path) -> None:
    data, residence = evidence()
    matrix = build_matrix(9, data, residence)
    link0 = matrix["links"][0]
    assert link0["marker_to_data_cycles"] == 720
    assert link0["forward_residence_cycles"] == 1
    assert link0["rx_arm_lead_cycles"] == 11
    assert link0["codeword_cycles"] == 2309
    assert link0["guard_cycles"] == 0
    assert link0["loop_delay_cycles"] == 8
    assert link0["link_budget_cycles"] == 3049
    assert link0["marker_source_node"] == 0
    assert link0["marker_destination_node"] == 1
    assert link0["data_source_node"] == 1
    assert link0["data_destination_node"] == 0
    assert link0["source_evidence"]["forward_residence_ticks"] == [1, 1, 1]
    path = tmp_path / "matrix.json"
    path.write_text(__import__("json").dumps(matrix), encoding="utf-8")
    assert load_config(path)["node_count"] == 4


def test_build_matrix_rejects_mismatched_residence_identity() -> None:
    data, residence = evidence()
    residence["matrix"]["identity"]["schedule_crc32"] = [201]
    with pytest.raises(ValueError, match="residence_schedule_crc32_mismatch"):
        build_matrix(9, data, residence)


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
    for level, row in ((7, "{10000000u, 1000000u, 4096u, 0u}"),
                       (8, "{25000000u, 1000000u, 4096u, 0u}"),
                       (9, "{30000000u, 1000000u, 4096u, 0u}")):
        assert row in profile
        assert profile_crc32(level) != 0
