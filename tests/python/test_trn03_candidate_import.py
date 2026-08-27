from __future__ import annotations

from types import SimpleNamespace

import pytest

from tools.calibration_ring_validate import trn03_candidate_import as importer


def crc_golden_snapshot() -> dict:
    links = []
    for index in range(4):
        links.append({
            "link_index": index,
            "source_node": index,
            "destination_node": (index + 1) % 4,
            "profile_crc32": 0x1234,
            "topology_generation": 7,
            "bias_generation": 9,
            "sample_count": 10,
            "accepted_count": 10,
            "jitter_ns": 1,
            "asymmetry_ns": 1,
            "residence_ns": 0,
            "raw_path_sum_ns": 0,
            "corrected_path_sum_ns": 20,
            "delay_estimate_ns": 10,
            "clock_rate_error_bound_ns": 0,
            "reject_reason": 0,
            "reference_accepted": 1,
            "active_eligible": 1,
        })
    return {
        "version": 2,
        "valid": 1,
        "active": 0,
        "flags": 0x17D,
        "link_count": 4,
        "topology_generation": 7,
        "topology_crc32": 0x5678,
        "bias_generation": 9,
        "profile_crc32": 0x1234,
        "schedule_crc32": 0x9ABC,
        "calibration_generation": 11,
        "freshness_us": 100,
        "cumulative_delay_ns": 40,
        "forwarding_residence_ns": 4,
        "predicted_ring_round_trip_ns": 44,
        "ring_round_trip_ns": 44,
        "residual_ns": 0,
        "links": links,
    }


def board_package() -> dict:
    links = []
    for index in range(2):
        links.append({
            field: value
            for field, value in zip(importer.LINK_FIELDS, (
                index, index, (index + 1) % 2, 20, 3, 7, 3, 3, 1, 1,
                5, 160, 160, 80, 4, 0, 1, 1,
            ))
        })
    return {
        "header": {
            field: value
            for field, value in zip(importer.HEADER_FIELDS, (
                2, 3, 10, 7, 20, 30, 40, 3_600_000_000, 1_000_000,
                180, 20, 8, 4, 4, 0x12345678,
            ))
        },
        "links": links,
    }


def test_snapshot_crc_matches_c_golden_vector() -> None:
    # The same fixture and constant are asserted by the C host unit test.
    assert importer.snapshot_crc32(crc_golden_snapshot()) == 0x33AE8F3F


def test_query_parser_requires_exact_tag_and_field_shape() -> None:
    fields = ("tag", "first", "second")
    assert importer.parse_query("CALPATH,1,-2", fields, "CALPATH") == {
        "tag": "CALPATH", "first": 1, "second": -2,
    }
    with pytest.raises(RuntimeError, match="invalid CALPATH response"):
        importer.parse_query("CALPATH,1", fields, "CALPATH")
    with pytest.raises(RuntimeError, match="invalid CALPATH response"):
        importer.parse_query("OTHER,1,2", fields, "CALPATH")


def test_stage_board_rejects_bad_board_crc(monkeypatch: pytest.MonkeyPatch) -> None:
    package = board_package()
    candidate_queries = 0

    monkeypatch.setattr(
        importer, "checked_action",
        lambda board, command, args: {"command": command, "passed": True})

    def query(board, command, fields, tag, args):
        nonlocal candidate_queries
        if tag == "CALPATHLINK":
            index = int(command.rsplit(" ", 1)[1])
            return {"tag": tag, "valid": 1, **package["links"][index]}
        candidate_queries += 1
        expected_bitmap = 3
        if candidate_queries == 1:
            return {
                "tag": tag, "import_active": 1, "complete": 0,
                "candidate_valid": 0, "valid_link_bitmap": 0,
            }
        if candidate_queries == 2:
            return {
                "tag": tag, "import_active": 1, "complete": 1,
                "candidate_valid": 0,
                "valid_link_bitmap": expected_bitmap,
            }
        return {
            "tag": tag, "import_active": 0, "complete": 1,
            "candidate_valid": 1, "valid_link_bitmap": expected_bitmap,
            "calculated_table_crc32": 0xDEADBEEF,
            "candidate_table_crc32": 0xDEADBEEF,
        }

    monkeypatch.setattr(importer, "_query", query)
    board = SimpleNamespace(address="node0")
    with pytest.raises(RuntimeError, match="FINALIZE readback mismatch"):
        importer.stage_board(board, package, SimpleNamespace())
