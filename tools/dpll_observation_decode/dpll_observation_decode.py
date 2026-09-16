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
SCHEMA_V6 = 6
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
AUTO_KINDS = {5: "auto_offer", 6: "auto_apply", 7: "auto_ack",
              8: "auto_hold", 9: "auto_observation"}
LOCAL_KINDS = {10: "local_observation", 11: "local_commit"}
DECODED_COMMAND = struct.Struct("<QQIIIIIIIIIiIBBBB")


def _decode_auto(data: bytes, board: str, start_ms: int, index: int) -> dict[str, Any]:
    def u32(offset: int) -> int:
        return struct.unpack_from("<I", data, offset)[0]

    def u64(offset: int) -> int:
        return struct.unpack_from("<Q", data, offset)[0]

    def i64(offset: int) -> int:
        return struct.unpack_from("<q", data, offset)[0]

    kind = u32(20) & 255
    if kind not in AUTO_KINDS or u32(0) != index + 1:
        raise ValueError("invalid AUTO capture kind/index")
    event: dict[str, Any] = {"capture_index": u32(0), "timestamp_ms": u32(4)}
    if kind in (5, 6):
        names = ("target_arm_epoch", "basis_source_output_ns_lo", "control_session",
                 "command_seq", "schedule_crc32", "target_clock_epoch_id",
                 "target_clock_run_id", "target_observer_epoch", "basis_measurement_sequence",
                 "expected_target_model_token", "expected_applied_command_seq",
                 "signed_delta_rate_ppb", "reserved", "schema_version", "source_slot",
                 "target_slot", "flags")
        command = dict(zip(names, DECODED_COMMAND.unpack_from(data, 32)))
        if command["schema_version"] != 3 or command["flags"] != 3 or command["reserved"]:
            raise ValueError("invalid AUTO decoded command identity")
        event.update(command=command, command_representation="decoded_semantic_fields_not_wire")
        if kind == 5:
            event.update(error_ppb_lo=i64(8), error_ppb_hi=i64(24),
                         first_source_model_token=u32(16), first_reference_model_token=u32(96))
        else:
            event.update(actual_rate_ppb=struct.unpack_from("<i", data, 8)[0],
                         dco_update_seq=u32(12))
    elif kind == 7:
        wire = data[32:96]
        if wire[0] != 4 or wire[3] != 0x1f or zlib.crc32(wire[:60]) != u32(92):
            raise ValueError("invalid AUTO ACK schema/CRC")
        # Explicit RATE wire offsets; this is the original received feedback.
        event.update(command_seq=u32(8), prior_model_token=u32(12), raw_feedback_hex=wire.hex(),
                     feedback={"source_slot": wire[1], "target_slot": wire[2],
                               "clock_epoch_id": u32(36), "clock_run_id": u32(40),
                               "observer_epoch": u32(52), "measurement_sequence": u32(56),
                               "tick_hz": u32(60), "source_arm_epoch": u64(44),
                               "absolute_output_ns_lo": u64(64), "coordinate_ns": u64(72),
                               "model_token": u32(80), "applied_command_seq": u32(84),
                               "control_session": u32(88)})
    else:
        event.update(control_session=u32(16), source_slot=(u32(20) >> 8) & 255,
                     reference_slot=(u32(20) >> 16) & 255,
                     error_ppb_lo=i64(8), error_ppb_hi=i64(24),
                     source_first_ns=u64(32), source_last_ns=u64(40),
                     reference_first_lo=u64(48), reference_first_hi=u64(56),
                     reference_last_lo=u64(64), reference_last_hi=u64(72),
                     source_model_token=u32(80), reference_model_token=u32(84),
                     first_measurement_sequence=u32(88), last_measurement_sequence=u32(92),
                     coordinate_domain=u32(96), source_endpoint_width_ns=1)
        if event["coordinate_domain"] != 3:
            raise ValueError("AUTO observation must use RATE coordinates")
    return {"board": board, "port": "SD", "capture_kind": AUTO_KINDS[kind],
            "elapsed_s": ((u32(4) - start_ms) & 0xffffffff) / 1000,
            "auto_control": event}


def _validate_auto_observations(samples: list[dict[str, Any]]) -> None:
    """Check retained endpoints independently, without replaying controller code."""
    for index, sample in enumerate(samples):
        kind = sample["capture_kind"]
        if kind in ("auto_observation", "auto_hold"):
            e = sample["auto_control"]
            source_delta = e["source_last_ns"] - e["source_first_ns"]
            ref_lo = e["reference_last_lo"] - e["reference_first_hi"]
            ref_hi = e["reference_last_hi"] - e["reference_first_lo"]
            if (source_delta <= 1 or ref_lo <= 0 or ref_hi < ref_lo or
                    e["reference_first_hi"] < e["reference_first_lo"] or
                    e["reference_last_hi"] < e["reference_last_lo"] or
                    e["last_measurement_sequence"] <= e["first_measurement_sequence"]):
                raise ValueError("invalid AUTO observation endpoints")
            expected_lo = (source_delta - 1) * 10**9 // ref_hi - 10**9
            expected_hi = -(-(source_delta + 1) * 10**9 // ref_lo) - 10**9
            if (e["error_ppb_lo"], e["error_ppb_hi"]) != (expected_lo, expected_hi):
                raise ValueError("AUTO observation interval disagrees with endpoints")
            if kind == "auto_observation" and (index + 1 == len(samples) or
                    samples[index + 1]["capture_kind"] != "auto_offer"):
                raise ValueError("orphan AUTO observation")
        if kind == "auto_offer":
            if not index or samples[index - 1]["capture_kind"] != "auto_observation":
                raise ValueError("orphan AUTO offer")
            e = sample["auto_control"]
            o = samples[index - 1]["auto_control"]
            c = e["command"]
            if not (o["timestamp_ms"] == e["timestamp_ms"] and
                    o["control_session"] == c["control_session"] and
                    o["source_slot"] == c["target_slot"] and
                    o["reference_slot"] == c["source_slot"] and
                    o["last_measurement_sequence"] == c["basis_measurement_sequence"] and
                    o["source_model_token"] == c["expected_target_model_token"] == e["first_source_model_token"] and
                    o["reference_model_token"] == e["first_reference_model_token"] and
                    (o["error_ppb_lo"], o["error_ppb_hi"]) == (e["error_ppb_lo"], e["error_ppb_hi"])):
                raise ValueError("AUTO offer/observation identity mismatch")


def _decode_local(data: bytes, board: str, start_ms: int, index: int) -> dict[str, Any]:
    def u32(offset): return struct.unpack_from("<I", data, offset)[0]
    def i32(offset): return struct.unpack_from("<i", data, offset)[0]
    def u64(offset): return struct.unpack_from("<Q", data, offset)[0]
    def i64(offset): return struct.unpack_from("<q", data, offset)[0]
    context = u32(20)
    kind = context & 255
    if kind not in LOCAL_KINDS or u32(0) != index + 1 or context >> 25:
        raise ValueError("invalid LOCAL capture kind/index/context")
    e = dict(capture_index=u32(0), timestamp_ms=u32(4), control_session=u32(16),
             source_slot=(context >> 8) & 255, reference_slot=(context >> 16) & 255)
    if not e['control_session'] or e['source_slot'] == e['reference_slot']:
        raise ValueError("invalid LOCAL source/session")
    if kind == 10:
        if context >> 24 or u32(96) != 4:
            raise ValueError("invalid LOCAL observation domain")
        e.update(error_ppb_lo=i64(8), error_ppb_hi=i64(24),
                 source_first_ns=u64(32), source_last_ns=u64(40),
                 reference_first_lo=u64(48), reference_first_hi=u64(56),
                 reference_last_lo=u64(64), reference_last_hi=u64(72),
                 source_model_token=u32(80), reference_model_token=u32(84),
                 first_measurement_sequence=u32(88), last_measurement_sequence=u32(92),
                 coordinate_domain=u32(96))
    else:
        e.update(applied=bool(context & (1 << 24)), before_rate_ppb=i32(8), before_dco_seq=u32(12),
                 after_rate_ppb=i32(24), after_dco_seq=u32(28), owner_token=u32(32),
                 candidate_serial=u32(36), role_generation=u32(40), ring_config_seq=u32(44),
                 schedule_crc32=u32(48), source_model_token=u32(52), reference_model_token=u32(56),
                 reference_receive_count=u32(60), source_first_width_ns=u32(64), source_last_width_ns=u32(68),
                 delta_rate_ppb=i32(72), remote_command_seq=u32(76), path_table_crc32=u32(80),
                 directed_delay_ns=u32(84), local_arm_epoch=u64(88), local_observer_epoch=u32(96))
    return dict(board=board, port="SD", capture_kind=LOCAL_KINDS[kind],
                elapsed_s=((u32(4) - start_ms) & 0xffffffff) / 1000, local_control=e)


def _validate_local_observations(samples: list[dict[str, Any]]) -> None:
    used = set()
    previous = {}
    for index, sample in enumerate(samples):
        kind = sample['capture_kind']
        if kind == 'local_observation':
            if index + 1 == len(samples) or samples[index + 1]['capture_kind'] != 'local_commit':
                raise ValueError('orphan LOCAL observation')
        if kind != 'local_commit':
            continue
        if not index or samples[index - 1]['capture_kind'] != 'local_observation':
            raise ValueError('orphan LOCAL commit')
        e, o = sample['local_control'], samples[index - 1]['local_control']
        for key in ('timestamp_ms', 'control_session', 'source_slot', 'reference_slot',
                    'source_model_token', 'reference_model_token'):
            if e[key] != o[key]:
                raise ValueError('LOCAL pair identity mismatch')
        for key in ('owner_token', 'candidate_serial', 'role_generation', 'ring_config_seq',
                    'schedule_crc32', 'source_model_token', 'reference_model_token',
                    'reference_receive_count', 'path_table_crc32', 'local_arm_epoch',
                    'local_observer_epoch', 'before_dco_seq'):
            if not e[key]:
                raise ValueError('LOCAL identity missing')
        delta = o['source_last_ns'] - o['source_first_ns']
        slo, shi = delta - e['source_first_width_ns'], delta + e['source_last_width_ns']
        rlo = o['reference_last_lo'] - o['reference_first_hi']
        rhi = o['reference_last_hi'] - o['reference_first_lo']
        if (slo <= 0 or rlo <= 0 or rhi < rlo or
                o['reference_first_hi'] < o['reference_first_lo'] or
                o['reference_last_hi'] < o['reference_last_lo'] or
                o['last_measurement_sequence'] <= o['first_measurement_sequence']):
            raise ValueError('invalid LOCAL interval')
        lo, hi = slo * 10**9 // rhi - 10**9, -(-shi * 10**9 // rlo) - 10**9
        if (lo, hi) != (o['error_ppb_lo'], o['error_ppb_hi']):
            raise ValueError('LOCAL interval disagrees with endpoints')
        key = (e['control_session'], e['owner_token'], e['candidate_serial'])
        if key in used:
            raise ValueError('LOCAL candidate consumed twice')
        used.add(key)
        if e['applied']:
            if (e['after_dco_seq'] != e['before_dco_seq'] + 1 or
                    e['after_rate_ppb'] - e['before_rate_ppb'] != e['delta_rate_ppb'] or
                    not ((lo > 0 and e['delta_rate_ppb'] < 0) or (hi < 0 and e['delta_rate_ppb'] > 0))):
                raise ValueError('LOCAL commit lacks negative feedback or actual adoption')
        elif (e['after_dco_seq'], e['after_rate_ppb']) != (e['before_dco_seq'], e['before_rate_ppb']):
            raise ValueError('LOCAL non-applied record changed DCO')
        lifetime = (e['control_session'], e['owner_token'], e['local_arm_epoch'])
        if lifetime in previous:
            prior = previous[lifetime]
            if (prior['remote_command_seq'] != e['remote_command_seq'] or
                    (prior['after_dco_seq'], prior['after_rate_ppb']) !=
                    (e['before_dco_seq'], e['before_rate_ppb'])):
                raise ValueError('LOCAL history has an unrecorded or remote DCO change')
        previous[lifetime] = e


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
    if schema not in (SCHEMA_V1, SCHEMA, SCHEMA_V3, SCHEMA_V4, SCHEMA_V5, SCHEMA_V6):
        raise ValueError(f"unsupported capture schema {schema}")
    expected_record = (RECORD_V1 if schema == SCHEMA_V1 else
                       RECORD_V5 if schema in (SCHEMA_V5, SCHEMA_V6) else
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
        if schema == SCHEMA_V6:
            raw_record = payload[index * record_size:(index + 1) * record_size]
            kind = struct.unpack_from("<I", raw_record, 20)[0] & 255
            if kind in AUTO_KINDS:
                samples.append(_decode_auto(raw_record, board_name, start_ms, index))
                continue
            if kind in LOCAL_KINDS:
                samples.append(_decode_local(raw_record, board_name, start_ms, index))
                continue
            if kind not in (1, 2, 3, 4):
                raise ValueError(f"unknown schema6 capture kind {kind}")
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
                                    if schema in (SCHEMA_V3, SCHEMA_V4, SCHEMA_V5, SCHEMA_V6)
                                    else source_slot),
                "sample_seq": update_seq,
                "phase_error_ns": phase_ns,
                "frequency_error_ppb": frequency_ppb,
                "lock_state": lock_state,
                "quality": quality,
                "gate_reject_code": gate,
            }
            if schema in (SCHEMA_V3, SCHEMA_V4, SCHEMA_V5, SCHEMA_V6):
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
                if schema in (SCHEMA_V4, SCHEMA_V5, SCHEMA_V6):
                    sample["observation"].update({
                        "delay_generation": observation_delay_generation,
                        "bias_generation": observation_bias_generation,
                    })
                if schema in (SCHEMA_V5, SCHEMA_V6):
                    sample["observation"].update({
                        "correlation_flags": observation_correlation_flags,
                        "reference_tx_phase_ns": observation_reference_tx_phase,
                        "local_rx_phase_ns": observation_local_rx_phase,
                        "common_effective_time_ns": observation_common_effective_time,
                        "expected_window_start_ns": observation_expected_window_start,
                        "observed_time_ns": observation_observed_time,
                    })
        elif schema in (SCHEMA_V3, SCHEMA_V4, SCHEMA_V5, SCHEMA_V6) and kind == CAPTURE_KIND_MASTER:
            sample["observation"] = {
                "source_slot_id": observation_source,
                "reference_slot_id": observation_reference,
                "delay_ns": observation_delay,
                "jitter_ns": observation_jitter,
                "raw_phase_error_ns": raw_phase,
            }
            if schema in (SCHEMA_V4, SCHEMA_V5, SCHEMA_V6):
                sample["observation"].update({
                    "delay_generation": observation_delay_generation,
                    "bias_generation": observation_bias_generation,
                })
            if schema in (SCHEMA_V5, SCHEMA_V6):
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
    if schema == SCHEMA_V6:
        _validate_auto_observations(samples)
        _validate_local_observations(samples)
    return {
        "schema": "HAOFV_DPLL_OBSERVATION_CAPTURE_V2",
        "binary_schema": schema,
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
