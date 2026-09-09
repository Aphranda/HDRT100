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
