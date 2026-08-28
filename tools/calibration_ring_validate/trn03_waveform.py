#!/usr/bin/env python3
"""Save raw TRN-03B captures first, then analyze independent copies."""

from __future__ import annotations

import argparse
import csv
import html
import json
import sys
import time
import zlib
from pathlib import Path
from typing import Any, Sequence

ROOT = Path(__file__).resolve().parents[2]
for tool_path in (ROOT / "tools", ROOT / "tools" / "tdma_ring_monitor",
                  ROOT / "tools" / "calibration_ring_validate"):
    if str(tool_path) not in sys.path:
        sys.path.insert(0, str(tool_path))

from calibration_data_train import parse_storage_read  # noqa: E402
from tdma_start_ring import Board, board_command  # noqa: E402


CAPTURE_SCHEMAS = {
    "HAOFV_TRN03_RING_CAPTURE_V1",
    "HAOFV_TRN03_RING_CAPTURE_V2",
    "HAOFV_TRN03_RING_CAPTURE_V3",
}
DEFAULT_WINDOW_NS = 1000
APP_REALTIME_LOAD_CALIBRATION_MASK = 1 << 2
APP_REALTIME_PHASE_CALIBRATION = 3
APP_REALTIME_SCHEDULE_HEADER_FIELDS = 8
APP_REALTIME_SCHEDULE_PHASE_FIELDS = (
    "start_cycle", "end_cycle", "wcet_cycles", "last_start_cycle",
    "last_runtime_cycles", "max_runtime_cycles", "run_count", "skip_count",
    "start_miss_count", "overrun_count", "deadline_miss_count",
)

TRANSPORT_RESULT_OK = "OK"
TRANSPORT_HEADER_SIZE = 32


def read_tdma_load_mask(board: Board, args: argparse.Namespace,
                        retry_count: int) -> int:
    last = ""
    for attempt in range(retry_count + 1):
        last = board_command(board, "SYSTem:TDMA:LOAD:MASK?", args)
        try:
            return int(last.strip().strip('"'), 0)
        except ValueError:
            if attempt != retry_count:
                time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: invalid TDMA load mask: {last!r}")


def _write_tdma_load_mask(board: Board, args: argparse.Namespace,
                          enabled_mask: int, retry_count: int) -> None:
    last = ""
    for attempt in range(retry_count + 1):
        last = board_command(
            board, f"SYSTem:TDMA:LOAD:MASK {enabled_mask}", args)
        if last.strip().strip('"') == "OK":
            return
        try:
            if read_tdma_load_mask(board, args, 0) == enabled_mask:
                return
        except RuntimeError:
            pass
        if attempt != retry_count:
            time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: TDMA load mask {enabled_mask} rejected: "
        f"{last!r}")


def read_tdma_schedule(board: Board, args: argparse.Namespace
                       ) -> dict[str, Any]:
    raw = board_command(board, "SYSTem:TDMA:SCHEDule?", args)
    try:
        values = [int(value.strip().strip('"'), 0)
                  for value in next(csv.reader([raw]), [])]
    except ValueError as exc:
        raise RuntimeError(
            f"{board.address}: invalid TDMA schedule: {raw!r}") from exc
    if len(values) < APP_REALTIME_SCHEDULE_HEADER_FIELDS:
        raise RuntimeError(
            f"{board.address}: truncated TDMA schedule: {raw!r}")
    phase_count = values[3]
    expected = (APP_REALTIME_SCHEDULE_HEADER_FIELDS +
                phase_count * len(APP_REALTIME_SCHEDULE_PHASE_FIELDS))
    if phase_count <= APP_REALTIME_PHASE_CALIBRATION or len(values) != expected:
        raise RuntimeError(
            f"{board.address}: invalid TDMA schedule shape: "
            f"phase_count={phase_count}, fields={len(values)}, "
            f"expected={expected}")
    phases = []
    for phase in range(phase_count):
        start = (APP_REALTIME_SCHEDULE_HEADER_FIELDS +
                 phase * len(APP_REALTIME_SCHEDULE_PHASE_FIELDS))
        phases.append(dict(zip(
            APP_REALTIME_SCHEDULE_PHASE_FIELDS,
            values[start:start + len(APP_REALTIME_SCHEDULE_PHASE_FIELDS)])))
    return {
        "version": values[0],
        "sys_clock_hz": values[1],
        "cycle_cycles": values[2],
        "phase_count": phase_count,
        "enabled_mask": values[4],
        "quarantined_mask": values[5],
        "cycle_count": values[6],
        "schedule_miss_count": values[7],
        "phases": phases,
    }


def validate_capture_schedule(before: dict[str, Any],
                              after: dict[str, Any]) -> dict[str, Any]:
    calibration_before = before["phases"][APP_REALTIME_PHASE_CALIBRATION]
    calibration_after = after["phases"][APP_REALTIME_PHASE_CALIBRATION]

    def delta(field: str) -> int:
        return ((int(calibration_after[field]) -
                 int(calibration_before[field])) & 0xFFFFFFFF)

    deltas = {
        field: delta(field) for field in (
            "run_count", "skip_count", "start_miss_count",
            "overrun_count", "deadline_miss_count")
    }
    deltas["schedule_miss_count"] = (
        (int(after["schedule_miss_count"]) -
         int(before["schedule_miss_count"])) & 0xFFFFFFFF)
    errors = []
    if (int(after["enabled_mask"]) &
            APP_REALTIME_LOAD_CALIBRATION_MASK) == 0:
        errors.append("calibration_load_not_enabled")
    if (int(after["quarantined_mask"]) &
            APP_REALTIME_LOAD_CALIBRATION_MASK) != 0:
        errors.append("calibration_load_quarantined")
    if deltas["run_count"] == 0:
        errors.append("calibration_phase_not_serviced")
    for field in ("start_miss_count", "overrun_count",
                  "deadline_miss_count", "schedule_miss_count"):
        if deltas[field] != 0:
            errors.append(f"{field}_grew")
    if (int(calibration_after["last_runtime_cycles"]) >
            int(calibration_after["wcet_cycles"])):
        errors.append("last_runtime_exceeded_wcet")
    if (int(calibration_after["max_runtime_cycles"]) >
            int(calibration_after["wcet_cycles"])):
        errors.append("max_runtime_exceeded_wcet")
    return {
        "passed": not errors,
        "errors": errors,
        "deltas": deltas,
        "calibration_phase_before": calibration_before,
        "calibration_phase_after": calibration_after,
    }


def _transport_identity_crc32(transport: bytes | bytearray) -> int:
    identity_input = (
        transport[0:14] + transport[15:16] + transport[16:24])
    return zlib.crc32(identity_input) & 0xFFFFFFFF


def _transport_packet_crc32(transport: bytes | bytearray) -> int:
    crc_input = bytearray(transport)
    crc_input[28:32] = b"\0\0\0\0"
    if len(crc_input) >= 32 and (crc_input[13] & 0x04) != 0:
        crc_input = crc_input[:32]
    return zlib.crc32(crc_input) & 0xFFFFFFFF


def _single_bit_repairs(transport: bytes, observed_crc32: int, *,
                        identity: bool) -> list[dict[str, int]]:
    offsets = (list(range(14)) + list(range(15, 24)) if identity else
               list(range(28)) + list(range(32, len(transport))))
    crc_function = (_transport_identity_crc32 if identity else
                    _transport_packet_crc32)
    repairs: list[dict[str, int]] = []
    candidate = bytearray(transport)
    for offset in offsets:
        for bit in range(8):
            candidate[offset] ^= 1 << bit
            if crc_function(candidate) == observed_crc32:
                repairs.append({"transport_byte_offset": offset, "bit": bit})
            candidate[offset] ^= 1 << bit
    return repairs


def validate_capture(value: object) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("schema") not in CAPTURE_SCHEMAS:
        raise ValueError(
            "capture schema must be one of " +
            ", ".join(sorted(CAPTURE_SCHEMAS)))
    node = int(value.get("node", -1))
    node_count = int(value.get("node_count", 0))
    bit_period_ns = int(value.get("bit_period_ns", 0))
    rx_bytes = value.get("rx_bytes")
    tx_bytes = value.get("tx_bytes")
    if (not 2 <= node_count <= 8 or not 0 <= node < node_count or
            bit_period_ns <= 0 or not isinstance(rx_bytes, list) or
            not isinstance(tx_bytes, list) or
            int(value.get("rx_byte_count", -1)) != len(rx_bytes) or
            int(value.get("tx_byte_count", -1)) != len(tx_bytes) or
            any(not isinstance(item, int) or not 0 <= item <= 0xFF
                for item in rx_bytes + tx_bytes) or
            int(value.get("rx_produced_bytes", -1)) < len(rx_bytes) or
            int(value.get("tx_produced_bytes", -1)) < len(tx_bytes)):
        raise ValueError("invalid TRN-03B capture metadata")
    # Capture validation is deliberately structural only. Raw waveform files
    # remain valid even when no frame magic, length or CRC can be decoded;
    # protocol interpretation belongs to the later analysis pass.
    if (int(value.get("capture_version", 0)) >= 2 and
            int(value.get("tx_complete_frame_count", -1)) < 0):
        raise ValueError("invalid TX capture counter")
    if int(value.get("capture_version", 0)) >= 3:
        sck_words = value.get("rx_sck_words")
        sck_word_count = int(value.get("sck_word_count", -1))
        sck_sample_count = int(value.get("sck_sample_count", -1))
        sck_sample_period_ns = int(value.get("sck_sample_period_ns", 0))
        if (not isinstance(sck_words, list) or
                sck_word_count != len(sck_words) or
                not 0 <= sck_sample_count <= sck_word_count * 32 or
                sck_sample_period_ns <= 0 or
                any(not isinstance(word, int) or
                    not 0 <= word <= 0xFFFFFFFF for word in sck_words)):
            raise ValueError("invalid raw SCK capture metadata")
    return value


def save_ring_capture(board: Board, args: argparse.Namespace, *,
                      calibration_generation: int,
                      capture_epoch: int,
                      original_load_mask: int | None = None
                      ) -> dict[str, object]:
    retry_count = int(getattr(args, "capture_latch_retries", 1))
    if original_load_mask is None:
        original_load_mask = read_tdma_load_mask(
            board, args, retry_count)
    capture_load_mask = (
        original_load_mask | APP_REALTIME_LOAD_CALIBRATION_MASK)
    load_mask_changed = capture_load_mask != original_load_mask
    last = ""
    ready_status: list[int] = []
    latch_attempts = 0
    schedule_before = read_tdma_schedule(board, args)
    schedule_after: dict[str, Any] | None = None
    schedule_validation: dict[str, Any] | None = None
    try:
        for latch_attempt in range(retry_count + 1):
            latch_attempts = latch_attempt + 1
            latch = board_command(
                board,
                f"CALibration:RING:CAPTure:LATCh "
                f"{calibration_generation},{capture_epoch}", args)
            latch_values = [value.strip().strip('"')
                            for value in next(csv.reader([latch]), [])]
            if (len(latch_values) != 2 or
                    int(latch_values[0], 0) != calibration_generation or
                    int(latch_values[1], 0) != capture_epoch):
                last = f"latch rejected: {latch!r}"
                if latch_attempt == retry_count:
                    raise RuntimeError(f"{board.address}: {last}")
                continue
            if load_mask_changed:
                _write_tdma_load_mask(
                    board, args, capture_load_mask, retry_count)
            deadline = time.monotonic() + args.capture_timeout
            while time.monotonic() < deadline:
                last = board_command(
                    board, "READ:CALibration:RING:CAPTure?", args)
                status = [int(value.strip().strip('"'), 0)
                          for value in next(csv.reader([last]), [])]
                if (len(status) >= 10 and
                        status[2] == calibration_generation and
                        status[3] == capture_epoch and status[0] == 2):
                    ready_status = status
                    break
                if (len(status) >= 10 and
                        status[2] == calibration_generation and
                        status[3] == capture_epoch and status[0] == 3):
                    break
                time.sleep(0.01)
            if ready_status:
                break
        if ready_status:
            schedule_after = read_tdma_schedule(board, args)
            schedule_validation = validate_capture_schedule(
                schedule_before, schedule_after)
            if not schedule_validation["passed"]:
                raise RuntimeError(
                    f"{board.address}: ring capture disturbed TDMA schedule: "
                    f"{','.join(schedule_validation['errors'])}; "
                    f"validation={json.dumps(schedule_validation, separators=(',', ':'))}; "
                    f"before={json.dumps(schedule_before, separators=(',', ':'))}; "
                    f"after={json.dumps(schedule_after, separators=(',', ':'))}")
    finally:
        if load_mask_changed:
            _write_tdma_load_mask(
                board, args, original_load_mask, retry_count)
    if not ready_status:
        schedule_diagnostic: object
        try:
            schedule_diagnostic = read_tdma_schedule(board, args)
        except Exception as exc:  # noqa: BLE001 - preserve timeout evidence
            schedule_diagnostic = {
                "read_error": f"{type(exc).__name__}: {exc}"}
        raise RuntimeError(
            f"{board.address}: ring capture latch timeout: {last!r}; "
            f"schedule={json.dumps(schedule_diagnostic, separators=(',', ':'))}")

    response = board_command(
        board,
        f"CALibration:RING:CAPTure:SAVE "
        f"{calibration_generation},{capture_epoch}", args)
    values = [value.strip().strip('"')
              for value in next(csv.reader([response]), [])]
    if len(values) != 3 or values[0] != "OK":
        raise RuntimeError(
            f"{board.address}: ring capture SD save rejected: {response!r}")
    job_id = int(values[1], 0)
    path = values[2]
    deadline = time.monotonic() + args.capture_timeout
    last = ""
    while time.monotonic() < deadline:
        last = board_command(board, "SYSTem:STORage:JOB?", args)
        job = [value.strip().strip('"')
               for value in next(csv.reader([last]), [])]
        if len(job) >= 8 and int(job[1], 0) == job_id:
            if job[0] == "DONE":
                return {"node_id": board.address, "sd_path": path,
                        "job_id": job_id, "size": int(job[4], 0),
                        "latch_attempts": latch_attempts,
                        "latch_status": ready_status,
                        "load_mask_before": original_load_mask,
                        "load_mask_during_capture": capture_load_mask,
                        "load_mask_restored": original_load_mask,
                        "schedule_before": schedule_before,
                        "schedule_after": schedule_after,
                        "schedule_validation": schedule_validation,
                        "capture_debug": ({
                            "core1_service_count": ready_status[10],
                            "intent_read_fail_count": ready_status[11],
                            "last_seen_sequence": ready_status[12],
                            "copy_attempt_count": ready_status[13],
                            "copy_fail_count": ready_status[14],
                            "consumed_sequence": ready_status[15],
                        } if len(ready_status) >= 16 else {})}
            if job[0] == "FAILED":
                raise RuntimeError(
                    f"{board.address}: ring capture SD job failed: {last!r}")
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: ring capture SD job timeout: {last!r}")


def download_ring_capture(board: Board, capture_file: dict[str, object],
                          args: argparse.Namespace,
                          local_path: Path) -> dict[str, object]:
    path = str(capture_file["sd_path"])
    data = bytearray()
    file_size: int | None = None
    path_hash: int | None = None
    while file_size is None or len(data) < file_size:
        requested = (128 if file_size is None else
                     min(128, file_size - len(data)))
        response = board_command(
            board,
            f'SYSTem:STORage:FILE:READ? "{path}",{len(data)},{requested}',
            args)
        page = parse_storage_read(response, len(data))
        if file_size is None:
            file_size = int(page["file_size"])
            path_hash = int(page["path_hash"])
        elif int(page["file_size"]) != file_size:
            raise RuntimeError("ring capture file size changed during download")
        payload = page["payload"]
        assert isinstance(payload, bytes)
        if not payload and not bool(page["eof"]):
            raise RuntimeError("ring capture read made no progress before EOF")
        data.extend(payload)
        if bool(page["eof"]):
            break
    if file_size is None or len(data) != file_size:
        raise RuntimeError(
            f"ring capture download incomplete: {len(data)}/{file_size}")
    capture = validate_capture(json.loads(data.decode("utf-8")))
    local_path.parent.mkdir(parents=True, exist_ok=True)
    local_path.write_bytes(data)
    return {
        **capture_file,
        "local_path": str(local_path),
        "download_size": len(data),
        "path_hash": path_hash,
        "capture": capture,
    }


def byte_bits(values: Sequence[int]) -> list[int]:
    return [((int(value) >> shift) & 1)
            for value in values for shift in range(7, -1, -1)]


def raw_sck_samples(capture: dict[str, Any]) -> list[int]:
    words = capture.get("rx_sck_words")
    sample_count = int(capture.get("sck_sample_count", 0))
    if not isinstance(words, list) or sample_count <= 0:
        return []
    return [
        (int(words[index >> 5]) >> (index & 31)) & 1
        for index in range(sample_count)
    ]


def analyze_sck_timing(
        capture: dict[str, Any], *,
        frequency_tolerance_percent: float = 5.0,
        duty_tolerance_percent: float = 10.0) -> dict[str, Any]:
    samples = raw_sck_samples(capture)
    tick_ns = int(capture.get("sck_sample_period_ns", 0))
    if not samples or tick_ns <= 0:
        return {
            "available": False,
            "reason": "raw SCK samples are absent from this capture version",
        }
    rising = [index for index in range(1, len(samples))
              if samples[index - 1] == 0 and samples[index] == 1]
    cycles: list[dict[str, int]] = []
    for rise, next_rise in zip(rising, rising[1:]):
        falling = next((index for index in range(rise + 1, next_rise)
                        if samples[index - 1] == 1 and samples[index] == 0),
                       None)
        if falling is None:
            continue
        cycles.append({
            "period_samples": next_rise - rise,
            "high_samples": falling - rise,
            "low_samples": next_rise - falling,
        })
    if not cycles:
        return {
            "available": False,
            "reason": "raw SCK window has no complete high/low cycle",
            "sample_count": len(samples),
            "transition_count": sum(
                samples[index] != samples[index - 1]
                for index in range(1, len(samples))),
        }

    def median(values: list[int]) -> float:
        ordered = sorted(values)
        middle = len(ordered) // 2
        if len(ordered) % 2:
            return float(ordered[middle])
        return (ordered[middle - 1] + ordered[middle]) / 2.0

    period_values = [row["period_samples"] for row in cycles]
    high_values = [row["high_samples"] for row in cycles]
    low_values = [row["low_samples"] for row in cycles]
    period_samples = median(period_values)
    high_samples = median(high_values)
    low_samples = median(low_values)
    period_ns = period_samples * tick_ns
    high_ns = high_samples * tick_ns
    low_ns = low_samples * tick_ns
    actual_hz = 1_000_000_000.0 / period_ns
    target_hz = int(capture.get("baud_hz", 0))
    frequency_error_percent = (None if target_hz <= 0 else
                               100.0 * abs(actual_hz - target_hz) / target_hz)
    duty_percent = 100.0 * high_ns / period_ns
    duty_error_percent = abs(duty_percent - 50.0)
    frequency_ok = (frequency_error_percent is not None and
                    frequency_error_percent <= frequency_tolerance_percent)
    duty_ok = duty_error_percent <= duty_tolerance_percent
    failures = []
    if not frequency_ok:
        failures.append("frequency")
    if not duty_ok:
        failures.append("duty")
    return {
        "available": True,
        "sample_period_ns": tick_ns,
        "sample_count": len(samples),
        "complete_cycle_count": len(cycles),
        "period_ns": period_ns,
        "clock_high_ns": high_ns,
        "clock_low_ns": low_ns,
        "actual_hz": actual_hz,
        "target_hz": target_hz,
        "frequency_error_percent": frequency_error_percent,
        "frequency_tolerance_percent": frequency_tolerance_percent,
        "frequency_ok": frequency_ok,
        "duty_percent": duty_percent,
        "duty_error_percent": duty_error_percent,
        "duty_tolerance_percent": duty_tolerance_percent,
        "duty_ok": duty_ok,
        "period_min_ns": min(period_values) * tick_ns,
        "period_max_ns": max(period_values) * tick_ns,
        "period_span_ns": (max(period_values) - min(period_values)) * tick_ns,
        "clock_high_min_ns": min(high_values) * tick_ns,
        "clock_high_max_ns": max(high_values) * tick_ns,
        "clock_low_min_ns": min(low_values) * tick_ns,
        "clock_low_max_ns": max(low_values) * tick_ns,
        "passed": not failures,
        "gate_failures": failures,
        "cycles": cycles,
    }


def latest_complete_packet(values: Sequence[int]) -> list[int] | None:
    """Return the newest complete 0x54,0x44,length packet in a byte stream."""
    fallback: list[int] | None = None
    for start in range(len(values) - 4, -1, -1):
        if values[start] != 0x54 or values[start + 1] != 0x44:
            continue
        frame_bytes = 4 + int(values[start + 2]) + (int(values[start + 3]) << 8)
        if frame_bytes >= 4 and start + frame_bytes <= len(values):
            packet = [int(value)
                      for value in values[start:start + frame_bytes]]
            # PHY and transport intentionally share the 0x54,0x44 magic.  A
            # backward-only search otherwise mistakes the nested transport
            # header for a newer PHY packet (version/class look like length).
            # Prefer the candidate whose payload is a self-consistent TDMA
            # transport frame, while retaining V1/generic capture support.
            if (frame_bytes >= 4 + TRANSPORT_HEADER_SIZE and
                    packet[4:6] == [0x54, 0x44] and
                    packet[6] == 1 and
                    packet[10] == TRANSPORT_HEADER_SIZE and
                    packet[8] + (packet[9] << 8) == frame_bytes - 4):
                return packet
            if fallback is None:
                fallback = packet
    return fallback


def decode_transport_evidence(
        physical_packet: Sequence[int] | None) -> dict[str, Any] | None:
    """Decode and CRC-check the TDMA transport frame inside a PHY packet."""
    if physical_packet is None:
        return None
    packet = bytes(int(value) for value in physical_packet)
    if len(packet) < 4:
        return {"valid": False, "result": "PHY_HEADER_TOO_SHORT"}
    frame_size = packet[2] | (packet[3] << 8)
    if packet[:2] != b"TD" or len(packet) != 4 + frame_size:
        return {"valid": False, "result": "PHY_HEADER_MISMATCH"}
    transport = packet[4:]
    if len(transport) < TRANSPORT_HEADER_SIZE:
        return {"valid": False, "result": "TRANSPORT_HEADER_TOO_SHORT"}
    if transport[:2] != b"TD":
        return {"valid": False, "result": "BAD_MAGIC"}
    if transport[2] != 1:
        return {"valid": False, "result": "BAD_VERSION"}
    if transport[6] != TRANSPORT_HEADER_SIZE:
        return {"valid": False, "result": "BAD_HEADER"}
    packet_size = transport[4] | (transport[5] << 8)
    if packet_size != len(transport):
        return {"valid": False, "result": "BAD_PACKET_SIZE",
                "packet_size": packet_size}

    read_u32 = lambda offset: int.from_bytes(
        transport[offset:offset + 4], "little")
    identity_crc32 = _transport_identity_crc32(transport)
    observed_identity_crc32 = read_u32(24)
    transport_crc32 = _transport_packet_crc32(transport)
    observed_transport_crc32 = read_u32(28)
    identity_repairs = _single_bit_repairs(
        transport, observed_identity_crc32, identity=True)
    transport_repairs = _single_bit_repairs(
        transport, observed_transport_crc32, identity=False)
    transport_repairs_after_identity: list[dict[str, object]] = []
    for identity_repair in identity_repairs:
        repaired = bytearray(transport)
        repaired[identity_repair["transport_byte_offset"]] ^= (
            1 << identity_repair["bit"])
        for remaining in _single_bit_repairs(
                bytes(repaired), observed_transport_crc32, identity=False):
            transport_repairs_after_identity.append({
                "identity_repair": identity_repair,
                "remaining_transport_repair": remaining,
            })
    result = (TRANSPORT_RESULT_OK
              if transport_crc32 == observed_transport_crc32 and
              identity_crc32 == observed_identity_crc32 else
              "TRANSPORT_CRC_MISMATCH"
              if transport_crc32 != observed_transport_crc32 else
              "IDENTITY_CRC_MISMATCH")
    return {
        "valid": result == TRANSPORT_RESULT_OK,
        "result": result,
        "packet_size": packet_size,
        "origin_node": transport[7],
        "sequence": read_u32(8),
        "payload_class": transport[12],
        "flags": transport[13],
        "hop_count": transport[14],
        "hop_limit": transport[15],
        "schedule_crc32": read_u32(16),
        "profile_crc32": read_u32(20),
        "identity_crc32": observed_identity_crc32,
        "expected_identity_crc32": identity_crc32,
        "identity_crc_xor": observed_identity_crc32 ^ identity_crc32,
        "identity_single_bit_repairs": identity_repairs,
        "transport_crc32": observed_transport_crc32,
        "expected_transport_crc32": transport_crc32,
        "transport_crc_xor": observed_transport_crc32 ^ transport_crc32,
        "transport_single_bit_repairs": transport_repairs,
        "transport_repairs_after_identity": transport_repairs_after_identity,
    }


def _display_bits(values: Sequence[int], window_bits: int) -> list[int]:
    """Return the first raw samples without searching for a frame boundary."""
    shown = byte_bits(values)[:window_bits]
    return shown + [0] * (window_bits - len(shown))


def best_alignment(reference: Sequence[int], candidate: Sequence[int],
                   max_lag_bits: int = 64) -> dict[str, int] | None:
    if not reference or not candidate:
        return None
    best: tuple[int, int, int] | None = None
    for lag in range(-max_lag_bits, max_lag_bits + 1):
        ref_start = max(0, -lag)
        candidate_start = max(0, lag)
        overlap = min(len(reference) - ref_start,
                      len(candidate) - candidate_start)
        if overlap <= 0:
            continue
        distance = sum(
            reference[ref_start + index] !=
            candidate[candidate_start + index]
            for index in range(overlap))
        # Compare mismatch ratio first, then prefer larger overlap/smaller lag.
        score = (distance * 1000000) // overlap
        row = (score, -overlap, abs(lag), lag, distance, overlap)
        if best is None or row[:3] < best[:3]:
            best = row  # type: ignore[assignment]
    if best is None:
        return None
    return {"lag_bits": best[3], "distance": best[4],
            "overlap_bits": best[5]}


def _step_path(samples: Sequence[int], *, x0: float, y0: float,
               bit_width: float, height: float) -> str:
    if not samples:
        return ""
    points = [f"M{x0:.2f},{y0 + (1 - samples[0]) * height:.2f}"]
    for index, value in enumerate(samples):
        left = x0 + index * bit_width
        right = left + bit_width
        y = y0 + (1 - value) * height
        if index:
            previous_y = y0 + (1 - samples[index - 1]) * height
            points.append(f"L{left:.2f},{previous_y:.2f} L{left:.2f},{y:.2f}")
        points.append(f"L{right:.2f},{y:.2f}")
    return " ".join(points)


def _inbound_sources(config: dict[str, Any], node: int
                     ) -> tuple[int, int, int]:
    links = config.get("links")
    if not isinstance(links, list):
        raise ValueError("TRN-03 matrix links missing")
    marker_matches = [link for link in links
                      if int(link.get("marker_destination_node", -1)) == node]
    data_matches = [link for link in links
                    if int(link.get("data_destination_node", -1)) == node]
    if len(marker_matches) != 1 or len(data_matches) != 1:
        raise ValueError(f"node{node} direction mapping is incomplete")
    return (int(marker_matches[0]["marker_source_node"]),
            int(data_matches[0]["data_source_node"]),
            int(marker_matches[0]["link_index"]))


def render_node_svg(config: dict[str, Any],
                    captures: dict[int, dict[str, Any]], *, node: int,
                    svg_path: Path,
                    window_duration_ns: int = DEFAULT_WINDOW_NS
                    ) -> dict[str, Any]:
    if node not in captures:
        raise ValueError(f"node{node} capture missing")
    marker_source, data_source, marker_link = _inbound_sources(config, node)
    local = validate_capture(captures[node])
    bit_period_ns = int(local["bit_period_ns"])
    window_bits = max(1, window_duration_ns // bit_period_ns)
    observed_raw = local["rx_bytes"]
    # The four RX streams are the raw loop-traffic observations. Do not use
    # the optional TX history as a capture source: that history is a software
    # frame record and may legitimately be absent on PIO flight followers.
    logical_raw = captures[marker_source]["rx_bytes"]
    physical_raw = captures[data_source]["rx_bytes"]
    observed_packet = latest_complete_packet(observed_raw)
    logical_packet = latest_complete_packet(logical_raw)
    physical_packet = latest_complete_packet(physical_raw)
    observed_transport = decode_transport_evidence(observed_packet)
    logical_transport = decode_transport_evidence(logical_packet)
    physical_transport = decode_transport_evidence(physical_packet)
    # Alignment operates on the captured streams exactly as stored. Complete
    # frame decoding below is supplementary protocol evidence and never
    # changes the samples rendered or compared.
    observed = byte_bits(observed_raw)
    logical = byte_bits(logical_raw)
    physical = byte_bits(physical_raw)
    logical_alignment = best_alignment(logical, observed)
    physical_alignment = best_alignment(physical, observed)
    sck_samples = raw_sck_samples(local)
    sck_timing = analyze_sck_timing(local)

    width, height = 1400, 620
    x0, plot_width = 250.0, 1100.0
    bit_width = plot_width / window_bits
    tracks = [
        (f"node{marker_source} raw loop RX observation", logical_raw, "#3465a4"),
        (f"node{data_source} raw loop RX observation", physical_raw, "#75507b"),
        (f"node{node} raw loop RX observation", observed_raw, "#4e9a06"),
    ]
    paths: list[str] = []
    sck_y = 155.0
    paths.append(
        f'<text x="20" y="{sck_y + 18:.0f}" class="label">'
        f'node{node} physical RX SCK from node{marker_source}</text>')
    if sck_samples:
        shown_sck_samples = sck_samples[:max(
            1, window_duration_ns // int(local["sck_sample_period_ns"]))]
        sck_sample_width = (
            plot_width * int(local["sck_sample_period_ns"]) /
            window_duration_ns)
        paths.append(
            f'<path d="{_step_path(shown_sck_samples, x0=x0, y0=sck_y, bit_width=sck_sample_width, height=45)}" '
            f'stroke="#cc0000" fill="none" stroke-width="2"/>')
    else:
        paths.append(
            f'<text x="{x0:.0f}" y="{sck_y + 25:.0f}" class="missing">'
            f'NO RAW SCK SAMPLES</text>')
    for index, (label, bits, color) in enumerate(tracks):
        y = 260.0 + index * 105.0
        shown = _display_bits(bits, window_bits)
        paths.append(
            f'<text x="20" y="{y + 18:.0f}" class="label">'
            f'{html.escape(label)}</text>')
        if bits:
            paths.append(
                f'<path d="{_step_path(shown, x0=x0, y0=y, bit_width=bit_width, height=45)}" '
                f'stroke="{color}" fill="none" stroke-width="3"/>')
        else:
            paths.append(
                f'<text x="{x0:.0f}" y="{y + 25:.0f}" class="missing">'
                f'NO SAMPLES</text>')
    ticks = []
    tick_step_ns = max(100, window_duration_ns // 10)
    for tick_ns in range(0, window_duration_ns + 1, tick_step_ns):
        x = x0 + plot_width * tick_ns / window_duration_ns
        ticks.append(
            f'<line x1="{x:.2f}" y1="125" x2="{x:.2f}" y2="515" '
            f'class="grid"/><text x="{x:.2f}" y="545" class="tick">'
            f'{tick_ns}</text>')
    logical_delay = (None if logical_alignment is None else
                     logical_alignment["lag_bits"] * bit_period_ns)
    physical_delay = (None if physical_alignment is None else
                      physical_alignment["lag_bits"] * bit_period_ns)
    status = ("local raw RX window has no SCK-clocked samples"
              if not observed_raw else
              "raw loop windows captured; no complete frame found in local window"
              if observed_packet is None else
              "raw loop windows captured; local frame decoding available")
    if observed_transport is not None and not observed_transport["valid"]:
        status += f'; RX transport {observed_transport["result"]}'
    sck_note = (f'SCK {float(sck_timing["actual_hz"]) / 1_000_000:.3f} MHz; '
                f'high/low {float(sck_timing["clock_high_ns"]):g}/'
                f'{float(sck_timing["clock_low_ns"]):g} ns; '
                f'duty {float(sck_timing["duty_percent"]):.2f}%'
                if sck_timing.get("available") else
                f'SCK timing unavailable: {sck_timing.get("reason", "unknown")}')
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="620" '
        'viewBox="0 0 1400 620">\n'
        '<style>.title{font:700 20px sans-serif}.note,.label{font:15px sans-serif}'
        '.tick{font:12px monospace;text-anchor:middle}.grid{stroke:#ddd;stroke-width:1}'
        '.missing{font:700 18px monospace;fill:#c00}</style>\n'
        f'<text x="20" y="30" class="title">TRN-03B node{node}: '
        f'raw loop traffic analysis at link{marker_link}</text>\n'
        f'<text x="20" y="58" class="note">window {window_duration_ns / 1000:g} us; '
        f'bit period {bit_period_ns} ns; marker source node{marker_source}; '
        f'DATA source node{data_source}</text>\n'
        f'<text x="20" y="83" class="note">{html.escape(sck_note)}</text>\n'
        f'<text x="20" y="108" class="note">marker-source raw shift '
        f'{logical_delay if logical_delay is not None else "N/A"} ns; '
        f'DATA-source raw shift '
        f'{physical_delay if physical_delay is not None else "N/A"} ns; '
        f'{html.escape(status)}</text>\n'
        + "".join(ticks) + "".join(paths) +
        '<text x="800" y="590" class="tick">time (ns)</text>\n</svg>\n')
    svg_path.parent.mkdir(parents=True, exist_ok=True)
    svg_path.write_text(svg, encoding="utf-8")
    return {
        "node": node,
        "marker_link": marker_link,
        "marker_source_node": marker_source,
        "data_source_node": data_source,
        "rx_sample_count": len(byte_bits(observed_raw)),
        "rx_complete_frame_found": observed_packet is not None,
        "logical_reference_sample_count": len(logical),
        "physical_source_sample_count": len(physical),
        "logical_alignment": logical_alignment,
        "physical_alignment": physical_alignment,
        "logical_best_delay_ns": logical_delay,
        "physical_best_delay_ns": physical_delay,
        "sck_timing": sck_timing,
        "rx_transport": observed_transport,
        "logical_reference_transport": logical_transport,
        "physical_source_transport": physical_transport,
        "status": status,
        "svg": str(svg_path),
    }


def analyze_capture_set(config: dict[str, Any], capture_paths: Sequence[Path],
                        out_dir: Path, window_duration_ns: int, *,
                        frequency_tolerance_percent: float = 5.0,
                        duty_tolerance_percent: float = 10.0
                        ) -> dict[str, Any]:
    captures: dict[int, dict[str, Any]] = {}
    for path in capture_paths:
        capture = validate_capture(json.loads(path.read_text(encoding="utf-8")))
        node = int(capture["node"])
        if node in captures:
            raise ValueError(f"duplicate node{node} capture")
        captures[node] = capture
    node_count = int(config.get("node_count", 0))
    if set(captures) != set(range(node_count)):
        raise ValueError("capture set does not cover every configured node")
    analyses = [
        render_node_svg(
            config, captures, node=node,
            svg_path=out_dir / f"node{node}_ring_capture_1us.svg",
            window_duration_ns=window_duration_ns)
        for node in range(node_count)
    ]
    for analysis in analyses:
        analysis["sck_timing"] = analyze_sck_timing(
            captures[int(analysis["node"])],
            frequency_tolerance_percent=frequency_tolerance_percent,
            duty_tolerance_percent=duty_tolerance_percent)
    gate_failures = [
        f"node{analysis['node']}:sck_{reason}"
        for analysis in analyses
        for reason in (
            analysis["sck_timing"].get("gate_failures", [])
            if analysis["sck_timing"].get("available") else ["unavailable"])
    ]
    result = {
        "schema": "HAOFV_TRN03_RING_WAVEFORM_ANALYSIS_V1",
        "calibration_generation": int(config["calibration_generation"]),
        "window_duration_ns": window_duration_ns,
        "frequency_tolerance_percent": frequency_tolerance_percent,
        "duty_tolerance_percent": duty_tolerance_percent,
        "passed": not gate_failures,
        "gate_failures": gate_failures,
        "nodes": analyses,
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "ring_capture_analysis.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--capture", action="append", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--window-duration-ns", type=int,
                        default=DEFAULT_WINDOW_NS)
    parser.add_argument("--frequency-tolerance-percent", type=float,
                        default=5.0)
    parser.add_argument("--duty-tolerance-percent", type=float, default=10.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if (args.window_duration_ns <= 0 or
                args.frequency_tolerance_percent < 0 or
                args.duty_tolerance_percent < 0):
            raise ValueError("window and timing tolerances must be non-negative")
        result = analyze_capture_set(
            json.loads(args.config.read_text(encoding="utf-8")),
            args.capture, args.out_dir, args.window_duration_ns,
            frequency_tolerance_percent=args.frequency_tolerance_percent,
            duty_tolerance_percent=args.duty_tolerance_percent)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
