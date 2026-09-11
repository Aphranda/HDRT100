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
SCHEMA_V1 = 1
SCHEMA = 2
SCHEMA_V3 = 3
SCHEMA_V4 = 4
SCHEMA_V5 = 5
HEADER = struct.Struct("<IHHIIIII")
RECORD_V1 = struct.Struct("<IIiiI")
RECORD = struct.Struct("<IIiiIIIIII")
RECORD_V3 = struct.Struct("<IIiiIIIIIIiIII")
RECORD_V4 = struct.Struct("<IIiiIIIIIIiIIIII")
RECORD_V5 = struct.Struct("<IIiiIIIIIIiIIIIIIIIQQQ")

CAPTURE_KIND_MASTER = 1
CAPTURE_KIND_FOLLOWER_COMMAND = 2
CAPTURE_KIND_FOLLOWER_STATE = 3
CAPTURE_KIND_FOLLOWER_EVIDENCE = 4


def _decode_capture_kind(value: int) -> str:
    return {
        CAPTURE_KIND_MASTER: "master_local_evidence",
        CAPTURE_KIND_FOLLOWER_COMMAND: "follower_applied_command",
        CAPTURE_KIND_FOLLOWER_STATE: "follower_state_transition",
        CAPTURE_KIND_FOLLOWER_EVIDENCE: "follower_local_evidence",
    }.get(value, "unknown")


def decode(path: Path, board: str) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError(f"capture is shorter than header: {path}")
    magic, schema, record_size, record_count, dropped, start_ms, end_ms, payload_crc = (
        HEADER.unpack_from(data, 0)
    )
    if magic != MAGIC:
        raise ValueError(f"unexpected capture magic 0x{magic:08X}")
    if schema not in (SCHEMA_V1, SCHEMA, SCHEMA_V3, SCHEMA_V4, SCHEMA_V5):
        raise ValueError(f"unsupported capture schema {schema}")
    expected_record = (RECORD_V1 if schema == SCHEMA_V1 else
                       RECORD_V5 if schema == SCHEMA_V5 else
                       RECORD_V4 if schema == SCHEMA_V4 else
                       RECORD_V3 if schema == SCHEMA_V3 else RECORD)
    if record_size != expected_record.size:
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
        observation_source = 0
        observation_reference = 0
        observation_delay = 0
        observation_jitter = 0
        observation_delay_generation = 0
        observation_bias_generation = 0
        observation_correlation_flags = 0
        observation_reference_tx_phase = 0
        observation_local_rx_phase = 0
        observation_common_effective_time = 0
        observation_expected_window_start = 0
        observation_observed_time = 0
        raw_phase = 0
        if schema == SCHEMA_V1:
            update_seq, timestamp_ms, phase_ns, frequency_ppb, state_and_gate = (
                RECORD_V1.unpack_from(payload, index * record_size)
            )
            source_slot = 0
            kind = CAPTURE_KIND_MASTER
            lock_state = 0
            quality = 0
            control_generation = 0
            command_seq = 0
            effective_time = 0
            raw_phase = phase_ns
        elif schema == SCHEMA:
            (update_seq, timestamp_ms, phase_ns, frequency_ppb, state_and_gate,
             context, control_generation, command_seq, effective_lo,
             effective_hi) = RECORD.unpack_from(payload, index * record_size)
            kind = context & 0xFF
            source_slot = (context >> 8) & 0xFF
            lock_state = (context >> 16) & 0xFF
            quality = (context >> 24) & 0xFF
            effective_time = effective_lo | (effective_hi << 32)
            raw_phase = phase_ns
        elif schema == SCHEMA_V3:
            (update_seq, timestamp_ms, phase_ns, frequency_ppb, state_and_gate,
             context, control_generation, command_seq, effective_lo,
             effective_hi, raw_phase, source_reference, observation_delay,
             observation_jitter) = RECORD_V3.unpack_from(
                 payload, index * record_size)
            kind = context & 0xFF
            source_slot = (context >> 8) & 0xFF
            lock_state = (context >> 16) & 0xFF
            quality = (context >> 24) & 0xFF
            effective_time = effective_lo | (effective_hi << 32)
            observation_source = source_reference & 0xFFFF
            observation_reference = (source_reference >> 16) & 0xFFFF
        elif schema == SCHEMA_V4:
            (update_seq, timestamp_ms, phase_ns, frequency_ppb, state_and_gate,
             context, control_generation, command_seq, effective_lo,
             effective_hi, raw_phase, source_reference, observation_delay,
             observation_jitter, observation_delay_generation,
             observation_bias_generation) = RECORD_V4.unpack_from(
                 payload, index * record_size)
            kind = context & 0xFF
            source_slot = (context >> 8) & 0xFF
            lock_state = (context >> 16) & 0xFF
            quality = (context >> 24) & 0xFF
            effective_time = effective_lo | (effective_hi << 32)
            observation_source = source_reference & 0xFFFF
            observation_reference = (source_reference >> 16) & 0xFFFF
        else:
            (update_seq, timestamp_ms, phase_ns, frequency_ppb, state_and_gate,
             context, control_generation, command_seq, effective_lo,
             effective_hi, raw_phase, source_reference, observation_delay,
             observation_jitter, observation_delay_generation,
             observation_bias_generation, observation_correlation_flags,
             observation_reference_tx_phase, observation_local_rx_phase,
             observation_common_effective_time,
             observation_expected_window_start,
             observation_observed_time) = RECORD_V5.unpack_from(
                 payload, index * record_size)
            kind = context & 0xFF
            source_slot = (context >> 8) & 0xFF
            lock_state = (context >> 16) & 0xFF
            quality = (context >> 24) & 0xFF
            effective_time = effective_lo | (effective_hi << 32)
            observation_source = source_reference & 0xFFFF
            observation_reference = (source_reference >> 16) & 0xFFFF
        state = state_and_gate & 0xFFFF
        gate = (state_and_gate >> 16) & 0xFFFF
        sample: dict[str, Any] = {
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
                    "last_phase_error_ns": (
                        0 if kind == CAPTURE_KIND_FOLLOWER_COMMAND else phase_ns),
                    "last_frequency_error_ppb": (
                        0 if kind == CAPTURE_KIND_FOLLOWER_COMMAND else frequency_ppb),
                    "dco_phase_offset_ns": (
                        phase_ns if kind == CAPTURE_KIND_FOLLOWER_COMMAND else 0),
                    "dco_period_adjust_ppb": (
                        frequency_ppb if kind == CAPTURE_KIND_FOLLOWER_COMMAND else 0),
                    "raw_phase_error_ns": raw_phase,
                    "observation_source_slot_id": observation_source,
                    "observation_reference_slot_id": observation_reference,
                    "observation_delay_ns": observation_delay,
                    "observation_jitter_ns": observation_jitter,
                    "gate_reject_code": gate,
                },
                "trigger_sequence": update_seq,
                "trigger_interval_ms": None,
                "simultaneous_feedback": False,
                "error": "",
                "capture_kind": _decode_capture_kind(kind),
            }
        if kind == CAPTURE_KIND_FOLLOWER_EVIDENCE:
            sample["follower_observation"] = {
                "source_slot_id": (observation_source
                                    if schema in (SCHEMA_V3, SCHEMA_V4, SCHEMA_V5)
                                    else source_slot),
                "sample_seq": update_seq,
                "phase_error_ns": phase_ns,
                "frequency_error_ppb": frequency_ppb,
                "lock_state": lock_state,
                "quality": quality,
                "gate_reject_code": gate,
            }
            if schema in (SCHEMA_V3, SCHEMA_V4, SCHEMA_V5):
                sample["follower_observation"].update({
                    "reference_slot_id": observation_reference,
                    "follow_master_slot_id": source_slot,
                })
                sample["observation"] = {
                    "source_slot_id": observation_source,
                    "reference_slot_id": observation_reference,
                    "follow_master_slot_id": source_slot,
                    "delay_ns": observation_delay,
                    "jitter_ns": observation_jitter,
                    "raw_phase_error_ns": raw_phase,
                }
                if schema in (SCHEMA_V4, SCHEMA_V5):
                    sample["observation"].update({
                        "delay_generation": observation_delay_generation,
                        "bias_generation": observation_bias_generation,
                    })
                if schema == SCHEMA_V5:
                    sample["observation"].update({
                        "correlation_flags": observation_correlation_flags,
                        "reference_tx_phase_ns": observation_reference_tx_phase,
                        "local_rx_phase_ns": observation_local_rx_phase,
                        "common_effective_time_ns": observation_common_effective_time,
                        "expected_window_start_ns": observation_expected_window_start,
                        "observed_time_ns": observation_observed_time,
                    })
        elif schema in (SCHEMA_V3, SCHEMA_V4, SCHEMA_V5) and kind == CAPTURE_KIND_MASTER:
            sample["observation"] = {
                "source_slot_id": observation_source,
                "reference_slot_id": observation_reference,
                "delay_ns": observation_delay,
                "jitter_ns": observation_jitter,
                "raw_phase_error_ns": raw_phase,
            }
            if schema in (SCHEMA_V4, SCHEMA_V5):
                sample["observation"].update({
                    "delay_generation": observation_delay_generation,
                    "bias_generation": observation_bias_generation,
                })
            if schema == SCHEMA_V5:
                sample["observation"].update({
                    "correlation_flags": observation_correlation_flags,
                    "reference_tx_phase_ns": observation_reference_tx_phase,
                    "local_rx_phase_ns": observation_local_rx_phase,
                    "common_effective_time_ns": observation_common_effective_time,
                    "expected_window_start_ns": observation_expected_window_start,
                    "observed_time_ns": observation_observed_time,
                })
        elif kind in (CAPTURE_KIND_FOLLOWER_COMMAND,
                      CAPTURE_KIND_FOLLOWER_STATE):
            sample["follower_command"] = {
                "source_slot_id": source_slot,
                "control_generation": control_generation,
                "command_seq": command_seq,
                "effective_vdc_time_ns": effective_time,
                "phase_offset_ns": phase_ns,
                "period_adjust_ppb": frequency_ppb,
                "lock_state": lock_state,
                "quality": quality,
                "applied": kind == CAPTURE_KIND_FOLLOWER_COMMAND,
            }
        samples.append(sample)
    return {
        "schema": "HAOFV_DPLL_OBSERVATION_CAPTURE_V2",
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
