#!/usr/bin/env python3
"""Decode a DPLL residual capture saved by the board StorageAO.

The firmware capture is intentionally compact and binary so the Core1 path
only appends fixed-size SRAM records.  This tool is a maintenance-side
decoder: it verifies the SD file header and payload CRC, emits the same
``samples.json`` shape used by ``dpll_residual_analyze``, and never talks to a
board or changes TDMA state.
"""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path
from typing import Any


MAGIC = 0x4C504444  # DDPL
SCHEMA = 1
HEADER = struct.Struct("<IHHIIIII")
RECORD = struct.Struct("<IIiiI")


def decode(path: Path, board: str) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError(f"capture is shorter than header: {path}")
    magic, schema, record_size, record_count, dropped, start_ms, end_ms, payload_crc = (
        HEADER.unpack_from(data, 0)
    )
    if magic != MAGIC:
        raise ValueError(f"unexpected capture magic 0x{magic:08X}")
    if schema != SCHEMA:
        raise ValueError(f"unsupported capture schema {schema}")
    if record_size != RECORD.size:
        raise ValueError(f"unexpected record size {record_size}")
    expected_size = HEADER.size + record_count * record_size
    if expected_size != len(data):
        raise ValueError(f"capture size {len(data)} != expected {expected_size}")
    payload = data[HEADER.size:]
    actual_payload_crc = zlib.crc32(payload) & 0xFFFFFFFF
    if actual_payload_crc != payload_crc:
        raise ValueError(
            f"payload CRC 0x{actual_payload_crc:08X} != "
            f"header 0x{payload_crc:08X}"
        )

    board_name = board.upper()
    samples: list[dict[str, Any]] = []
    for index in range(record_count):
        update_seq, timestamp_ms, phase_ns, frequency_ppb, state_and_gate = (
            RECORD.unpack_from(payload, index * record_size)
        )
        state = state_and_gate & 0xFFFF
        gate = (state_and_gate >> 16) & 0xFFFF
        samples.append(
            {
                "ts_utc": "",
                "elapsed_s": max(0.0, (timestamp_ms - start_ms) / 1000.0),
                "board": board_name,
                "port": "SD",
                "tdma": {},
                "vdc_status": {},
                "dpll_status": {},
                "readiness": {},
                "vdc_vector": {},
                "dpll_vector": {
                    "state": state,
                    "dpll_update_seq": update_seq,
                    "last_phase_error_ns": phase_ns,
                    "last_frequency_error_ppb": frequency_ppb,
                    "gate_reject_code": gate,
                },
                "trigger_sequence": update_seq,
                "trigger_interval_ms": None,
                "simultaneous_feedback": False,
                "error": "",
            }
        )
    return {
        "schema": "HAOFV_DPLL_OBSERVATION_CAPTURE_V1",
        "board": board_name,
        "source": str(path),
        "record_count": record_count,
        "dropped_count": dropped,
        "start_ms": start_ms,
        "end_ms": end_ms,
        "payload_crc32": f"0x{payload_crc:08X}",
        "samples": {board_name: samples},
    }


def run(input_path: Path, output_path: Path, board: str) -> dict[str, Any]:
    result = decode(input_path, board)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(result["samples"], ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    result["output"] = str(output_path)
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="downloaded SD .bin capture")
    parser.add_argument("--board", required=True, help="NO1..NO8 identity for this capture")
    parser.add_argument("--output", type=Path, required=True, help="samples.json for residual analyzer")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        result = run(args.input, args.output, args.board)
    except (OSError, ValueError, struct.error) as exc:
        print(f"FAILED: {exc}")
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
