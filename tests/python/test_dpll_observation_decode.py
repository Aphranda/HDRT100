from __future__ import annotations

import json
import struct
import zlib

from tools.dpll_observation_decode.dpll_observation_decode import (
    HEADER,
    MAGIC,
    RECORD,
    RECORD_V1,
    SCHEMA,
    RECORD_V3,
    SCHEMA_V3,
    RECORD_V4,
    SCHEMA_V4,
    RECORD_V5,
    SCHEMA_V5,
    SCHEMA_V1,
    run,
)


def test_decode_verifies_header_and_emits_monitor_shape(tmp_path):
    records = b"".join(
        RECORD.pack(
            10 + index, 1000 + index * 4, -20 + index, 3,
            5 | (7 << 16), 1, 9, 0, 0, 0,
        )
        for index in range(3)
    )
    capture = HEADER.pack(
        MAGIC,
        SCHEMA,
        RECORD.size,
        3,
        0,
        1000,
        1008,
        zlib.crc32(records) & 0xFFFFFFFF,
    ) + records
    input_path = tmp_path / "dpll.bin"
    output_path = tmp_path / "samples.json"
    input_path.write_bytes(capture)

    result = run(input_path, output_path, "no5")
    payload = json.loads(output_path.read_text(encoding="utf-8"))
    assert result["record_count"] == 3
    assert payload["NO5"][1]["dpll_vector"]["last_phase_error_ns"] == -19
    assert payload["NO5"][0]["dpll_vector"]["gate_reject_code"] == 7
    assert payload["NO5"][2]["elapsed_s"] == 0.008


def test_decode_rejects_bad_payload_crc(tmp_path):
    records = RECORD_V1.pack(1, 10, 0, 0, 0)
    capture = HEADER.pack(
        MAGIC, SCHEMA_V1, RECORD_V1.size, 1, 0, 10, 10, 0,
    ) + records
    path = tmp_path / "bad.bin"
    path.write_bytes(capture)
    try:
        run(path, tmp_path / "out.json", "NO5")
    except ValueError as exc:
        assert "payload CRC" in str(exc)
    else:
        raise AssertionError("bad CRC must be rejected")


def test_decode_keeps_legacy_schema_v1_compatible(tmp_path):
    records = RECORD_V1.pack(1, 10, -2, 3, 5)
    capture = HEADER.pack(
        MAGIC, SCHEMA_V1, RECORD_V1.size, 1, 0, 10, 10,
        zlib.crc32(records) & 0xFFFFFFFF,
    ) + records
    path = tmp_path / "legacy.bin"
    path.write_bytes(capture)
    result = run(path, tmp_path / "legacy.json", "NO1")
    assert result["schema"] == "HAOFV_DPLL_OBSERVATION_CAPTURE_V2"
    assert result["samples"]["NO1"][0]["capture_kind"] == "master_local_evidence"


def test_decode_exposes_follower_applied_command_fields(tmp_path):
    context = 2 | (3 << 8) | (5 << 16) | (2 << 24)
    effective_time = 0x123456789ABCDEF0
    records = RECORD.pack(
        17, 1004, -77, 321, 1 | (0 << 16), context, 44, 91,
        effective_time & 0xFFFFFFFF, effective_time >> 32,
    )
    capture = HEADER.pack(
        MAGIC, SCHEMA, RECORD.size, 1, 0, 1000, 1004,
        zlib.crc32(records) & 0xFFFFFFFF,
    ) + records
    path = tmp_path / "follower.bin"
    path.write_bytes(capture)
    sample = run(path, tmp_path / "follower.json", "NO3")["samples"]["NO3"][0]
    assert sample["capture_kind"] == "follower_applied_command"
    assert sample["dpll_vector"]["dco_phase_offset_ns"] == -77
    assert sample["follower_command"] == {
        "source_slot_id": 3,
        "control_generation": 44,
        "command_seq": 91,
        "effective_vdc_time_ns": effective_time,
        "phase_offset_ns": -77,
        "period_adjust_ppb": 321,
        "lock_state": 5,
        "quality": 2,
        "applied": True,
    }


def test_decode_exposes_follower_local_evidence_fields(tmp_path):
    context = 4 | (2 << 8) | (1 << 16) | (3 << 24)
    records = RECORD.pack(
        23, 1008, -19, 7, 1, context, 0, 0, 0, 0,
    )
    capture = HEADER.pack(
        MAGIC, SCHEMA, RECORD.size, 1, 0, 1000, 1008,
        zlib.crc32(records) & 0xFFFFFFFF,
    ) + records
    path = tmp_path / "follower-evidence.bin"
    path.write_bytes(capture)
    sample = run(path, tmp_path / "follower-evidence.json", "NO2")[
        "samples"]["NO2"][0]
    assert sample["capture_kind"] == "follower_local_evidence"
    assert sample["dpll_vector"]["last_phase_error_ns"] == -19
    assert sample["follower_observation"] == {
        "source_slot_id": 2,
        "sample_seq": 23,
        "phase_error_ns": -19,
        "frequency_error_ppb": 7,
        "lock_state": 1,
        "quality": 3,
        "gate_reject_code": 0,
    }


def test_decode_schema_v3_separates_observed_path_from_follow_master(tmp_path):
    context = 4 | (0 << 8) | (5 << 16) | (3 << 24)
    records = RECORD_V3.pack(
        23, 1008, -19, 7, 1, context, 0, 0, 0, 0,
        -21, 2 | (1 << 16), 480, 6,
    )
    capture = HEADER.pack(
        MAGIC, SCHEMA_V3, RECORD_V3.size, 1, 0, 1000, 1008,
        zlib.crc32(records) & 0xFFFFFFFF,
    ) + records
    path = tmp_path / "follower-evidence-v3.bin"
    path.write_bytes(capture)
    sample = run(path, tmp_path / "v3.json", "NO2")[
        "samples"]["NO2"][0]
    assert sample["observation"] == {
        "source_slot_id": 2,
        "reference_slot_id": 1,
        "follow_master_slot_id": 0,
        "delay_ns": 480,
        "jitter_ns": 6,
        "raw_phase_error_ns": -21,
    }


def test_decode_schema_v4_preserves_path_generations(tmp_path):
    context = 1 | (2 << 8) | (5 << 16) | (4 << 24)
    records = RECORD_V4.pack(
        24, 1012, 17, -3, 1, context, 8, 0, 0, 0,
        19, 2 | (1 << 16), 480, 6, 11, 13,
    )
    capture = HEADER.pack(
        MAGIC, SCHEMA_V4, RECORD_V4.size, 1, 0, 1000, 1012,
        zlib.crc32(records) & 0xFFFFFFFF,
    ) + records
    path = tmp_path / "master-v4.bin"
    path.write_bytes(capture)
    sample = run(path, tmp_path / "v4.json", "NO1")["samples"]["NO1"][0]
    assert sample["observation"]["delay_generation"] == 11
    assert sample["observation"]["bias_generation"] == 13


def test_decode_schema_v5_preserves_common_time_and_phase_provenance(tmp_path):
    context = 1 | (0 << 8) | (5 << 16) | (3 << 24)
    records = RECORD_V5.pack(
        25, 1016, 17, -3, 1, context, 0, 0, 0, 0,
        19, 2 | (1 << 16), 480, 6, 11, 13, 3, 120, 760,
        0x100000000 + 120, 0x100000000 + 10120, 0x100000000 + 880,
    )
    capture = HEADER.pack(
        MAGIC, SCHEMA_V5, RECORD_V5.size, 1, 0, 1000, 1016,
        zlib.crc32(records) & 0xFFFFFFFF,
    ) + records
    path = tmp_path / "master-v5.bin"
    path.write_bytes(capture)
    sample = run(path, tmp_path / "v5.json", "NO1")["samples"]["NO1"][0]
    assert sample["observation"] == {
        "source_slot_id": 2,
        "reference_slot_id": 1,
        "delay_ns": 480,
        "jitter_ns": 6,
        "raw_phase_error_ns": 19,
        "delay_generation": 11,
        "bias_generation": 13,
        "correlation_flags": 3,
        "reference_tx_phase_ns": 120,
        "local_rx_phase_ns": 760,
        "common_effective_time_ns": 0x100000000 + 120,
        "expected_window_start_ns": 0x100000000 + 10120,
        "observed_time_ns": 0x100000000 + 880,
    }
