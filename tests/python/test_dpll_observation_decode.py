from __future__ import annotations

import json
import struct
import zlib

from tools.dpll_observation_decode.dpll_observation_decode import (
    HEADER,
    MAGIC,
    RECORD,
    SCHEMA,
    run,
)


def test_decode_verifies_header_and_emits_monitor_shape(tmp_path):
    records = b"".join(
        RECORD.pack(10 + index, 1000 + index * 4, (-20 + index), 3, 5 | (7 << 16))
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
    records = RECORD.pack(1, 10, 0, 0, 0)
    capture = HEADER.pack(MAGIC, SCHEMA, RECORD.size, 1, 0, 10, 10, 0) + records
    path = tmp_path / "bad.bin"
    path.write_bytes(capture)
    try:
        run(path, tmp_path / "out.json", "NO5")
    except ValueError as exc:
        assert "payload CRC" in str(exc)
    else:
        raise AssertionError("bad CRC must be rejected")
