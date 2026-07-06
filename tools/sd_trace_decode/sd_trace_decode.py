#!/usr/bin/env python3
"""Decode RP2350_TRIG SD trace binary files.

The firmware writes trace files under /traces/run and /traces/fault as a
little-endian header followed by fixed 16-byte records. This tool is offline
only: copy the .bin/.idx files from the SD card first, then decode them here.
"""

from __future__ import annotations

import argparse
import binascii
import csv
import json
import struct
import sys
from pathlib import Path
from typing import Any


TRACE_MAGIC = 0x43525452
TRACE_SCHEMA = 1
HEADER_STRUCT = struct.Struct("<IHHIIIIIII")
RECORD_STRUCT = struct.Struct("<IHBBII")

DOMAIN_NAMES = {
    1: "storage",
    2: "trigger",
    3: "sync_io",
}

SEVERITY_NAMES = {
    0: "debug",
    1: "info",
    2: "warn",
    3: "error",
}

EVENT_NAMES = {
    (1, 1): "storage.boot_snapshot_due",
    (2, 10): "trigger.scpi_arm",
    (2, 20): "trigger.ao_init",
    (2, 21): "trigger.queue_post",
    (2, 22): "trigger.queue_full",
    (2, 23): "trigger.queue_null",
    (2, 30): "trigger.execute",
    (2, 31): "trigger.state_change",
    (2, 32): "trigger.error_change",
    (2, 33): "trigger.dma_rollover",
    (2, 34): "trigger.enc_z_pulse",
    (2, 35): "trigger.event_ignored",
    (2, 36): "trigger.source_config",
    (2, 37): "trigger.edge_config",
    (2, 38): "trigger.gate_config",
    (2, 39): "trigger.safe_config",
    (2, 40): "trigger.resource_busy",
    (2, 41): "trigger.io_arm_failed",
    (2, 42): "trigger.io_lost",
    (2, 43): "trigger.runtime_sample",
    (2, 100): "trigger.scpi_fault",
    (3, 10): "sync_io.init_ok",
    (3, 11): "sync_io.init_fail",
    (3, 20): "sync_io.capture_start",
    (3, 21): "sync_io.capture_stop",
    (3, 22): "sync_io.capture_drop",
    (3, 23): "sync_io.capture_fail",
    (3, 30): "sync_io.pulse_fifo_full",
    (3, 31): "sync_io.pulse_invalid",
    (3, 40): "sync_io.clock_start",
    (3, 41): "sync_io.clock_stop",
    (3, 42): "sync_io.clock_fail",
    (3, 50): "sync_io.seq_arm_fail",
    (3, 51): "sync_io.seq_armed",
    (3, 52): "sync_io.seq_disarm",
    (3, 53): "sync_io.seq_gate_invalid",
    (3, 54): "sync_io.seq_pio_no_space",
    (3, 55): "sync_io.seq_runtime",
    (3, 60): "sync_io.enc_arm_fail",
    (3, 61): "sync_io.enc_armed",
    (3, 62): "sync_io.enc_disarm",
    (3, 63): "sync_io.enc_pio_no_space",
    (3, 64): "sync_io.enc_runtime",
}

TRIGGER_STATE_NAMES = {
    0: "IDLE",
    1: "SEQ_CONFIGURED",
    2: "SEQ_ARMED",
    3: "ENC_CONFIGURED",
    4: "ENC_ARMED",
    5: "FAULT",
}

TRIGGER_EVENT_NAMES = {
    0: "CONFIGURE_SEQ",
    1: "SET_SOURCE_PIN",
    2: "SET_EDGE",
    3: "SET_GATE",
    4: "SET_SAFE_STATE",
    5: "ARM",
    6: "DISARM",
    7: "DMA_ROLLOVER",
    8: "FAULT",
    9: "CLEAR_FAULT",
    10: "SET_TRIGGER_WIDTH",
    11: "FIRE_TRIGGER",
    12: "SET_PULSE_WIDTH",
    13: "FIRE_PULSE",
    14: "SET_MARKER_WIDTH",
    15: "FIRE_MARKER",
    16: "SET_SAMPLE_RATE",
    17: "SET_SAMPLE_STATE",
    18: "SET_CLOCK_FREQ",
    19: "SET_CLOCK_STATE",
    20: "RESET",
    21: "CONFIGURE_ENC",
    22: "SET_ENC_TARGET",
    23: "SET_ENC_PINS",
    24: "ENC_Z_PULSE",
    25: "SET_PCNT_DECODE",
    26: "SET_PCNT_DIR",
    27: "SET_PCNT_FILTER",
    28: "SET_PCNT_GATE",
    29: "SET_PCNT_CMP",
    30: "SET_PCNT_PRESET",
    31: "PCNT_CLEAR",
    32: "RUNTIME_SAMPLE",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace_bin", type=Path, help="trace .bin copied from the SD card")
    parser.add_argument("--idx", type=Path, help="optional matching .idx file")
    parser.add_argument("--csv", action="store_true", help="write CSV instead of JSON")
    parser.add_argument("--output", "-o", type=Path, help="output path; default is stdout")
    parser.add_argument("--allow-bad-crc", action="store_true", help="decode even when CRC checks fail")
    return parser.parse_args()


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def parse_idx(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def parse_int(value: str) -> int:
    if value.lower().startswith("0x"):
        return int(value, 16)
    return int(value, 10)


def parse_hex_u32(value: str) -> int:
    return int(value[2:] if value.lower().startswith("0x") else value, 16)


def split_u16_pair(value: int) -> tuple[int, int]:
    return ((value >> 16) & 0xFFFF, value & 0xFFFF)


def decode_runtime_flags(flags: int) -> dict[str, bool]:
    return {
        "running": bool(flags & (1 << 0)),
        "pio_enabled": bool(flags & (1 << 1)),
        "dma_busy": bool(flags & (1 << 2)),
        "dma_irq_enabled": bool(flags & (1 << 3)),
        "tx_fifo_empty": bool(flags & (1 << 4)),
        "tx_fifo_full": bool(flags & (1 << 5)),
    }


def extended_timestamps(records: list[dict[str, Any]]) -> None:
    epoch = 0
    previous: int | None = None
    for record in records:
        current = int(record["timestamp_ms"])
        if previous is not None and current < previous:
            epoch += 1 << 32
        record["timestamp_ms_unwrapped"] = epoch + current
        previous = current


def decode_event_details(record: dict[str, Any]) -> dict[str, Any]:
    domain = int(record["domain"])
    event_id = int(record["event_id"])
    arg0 = int(record["arg0"])
    arg1 = int(record["arg1"])
    details: dict[str, Any] = {}

    if domain == 2 and event_id in (21, 22, 30, 31, 32, 35):
        trigger_event = arg0
        details["trigger_event"] = trigger_event
        details["trigger_event_name"] = TRIGGER_EVENT_NAMES.get(trigger_event, "UNKNOWN")

    if domain == 2 and event_id in (30, 31, 35):
        after_state, before_state = split_u16_pair(arg1)
        details["before_state"] = before_state
        details["before_state_name"] = TRIGGER_STATE_NAMES.get(before_state, "UNKNOWN")
        details["after_state"] = after_state
        details["after_state_name"] = TRIGGER_STATE_NAMES.get(after_state, "UNKNOWN")

    if domain == 2 and event_id == 32:
        details["error_code"] = arg1

    if domain == 2 and event_id == 33:
        details["rollover_count_low32"] = arg0
        details["seq_index"] = arg1

    if domain == 2 and event_id == 34:
        details["enc_rev_count"] = arg0
        details["enc_count"] = arg1

    if domain == 2 and event_id == 36:
        details["before_source_pin"] = arg0
        details["after_source_pin"] = arg1

    if domain == 2 and event_id == 37:
        details["before_edge"] = arg0
        details["after_edge"] = arg1

    if domain == 2 and event_id == 38:
        details["before_gate_enabled"] = bool(arg0)
        details["after_gate_enabled"] = bool(arg1)

    if domain == 2 and event_id == 39:
        details["before_safe_state"] = arg0
        details["after_safe_state"] = arg1

    if domain == 2 and event_id in (40, 41, 42):
        details["before_state"] = arg0
        details["before_state_name"] = TRIGGER_STATE_NAMES.get(arg0, "UNKNOWN")
        details["after_state"] = arg1
        details["after_state_name"] = TRIGGER_STATE_NAMES.get(arg1, "UNKNOWN")

    if domain == 2 and event_id == 43:
        state = arg0 & 0xFF
        edge = (arg0 >> 8) & 0xFF
        details["state"] = state
        details["state_name"] = TRIGGER_STATE_NAMES.get(state, "UNKNOWN")
        details["edge"] = edge
        details["gate_enabled"] = bool(arg0 & (1 << 16))
        details["seq_index_low16"] = (arg1 >> 16) & 0xFFFF
        details["trigger_count_low16"] = arg1 & 0xFFFF

    if domain == 3 and event_id in (55, 64):
        details.update(decode_runtime_flags(arg0))
        details["transfer_count_low16"] = arg1 & 0xFFFF

    return details


def decode_trace(path: Path, idx_path: Path | None) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < HEADER_STRUCT.size:
        raise ValueError(f"{path} is too small for a trace header")

    header_values = HEADER_STRUCT.unpack_from(data, 0)
    header = {
        "magic": header_values[0],
        "schema": header_values[1],
        "header_len": header_values[2],
        "sequence": header_values[3],
        "event_count": header_values[4],
        "start_ms": header_values[5],
        "end_ms": header_values[6],
        "tick_hz": header_values[7],
        "flags": header_values[8],
        "crc32": header_values[9],
    }

    payload_offset = int(header["header_len"])
    event_count = int(header["event_count"])
    expected_size = payload_offset + event_count * RECORD_STRUCT.size
    if payload_offset < HEADER_STRUCT.size:
        raise ValueError(f"header_len {payload_offset} is smaller than {HEADER_STRUCT.size}")
    if len(data) < expected_size:
        raise ValueError(f"{path} has {len(data)} bytes, expected at least {expected_size}")

    payload = data[payload_offset:expected_size]
    computed_crc = crc32(payload)
    records: list[dict[str, Any]] = []
    for index in range(event_count):
        offset = index * RECORD_STRUCT.size
        timestamp_ms, event_id, domain, severity, arg0, arg1 = RECORD_STRUCT.unpack_from(payload, offset)
        record = {
            "index": index,
            "timestamp_ms": timestamp_ms,
            "event_id": event_id,
            "event_name": EVENT_NAMES.get((domain, event_id), "unknown"),
            "domain": domain,
            "domain_name": DOMAIN_NAMES.get(domain, "unknown"),
            "severity": severity,
            "severity_name": SEVERITY_NAMES.get(severity, "unknown"),
            "arg0": arg0,
            "arg1": arg1,
        }
        record["details"] = decode_event_details(record)
        records.append(record)
    extended_timestamps(records)

    idx_values: dict[str, str] | None = parse_idx(idx_path) if idx_path else None
    checks = {
        "magic_ok": header["magic"] == TRACE_MAGIC,
        "schema_ok": header["schema"] == TRACE_SCHEMA,
        "size_ok": len(data) == expected_size,
        "crc_ok": computed_crc == header["crc32"],
        "idx_ok": True,
    }

    if idx_values is not None:
        idx_checks = [
            idx_values.get("magic") == "RP2350_TRIG_TRACE_IDX",
            parse_int(idx_values.get("schema", "0")) == 1,
            parse_int(idx_values.get("sequence", "0")) == header["sequence"],
            parse_int(idx_values.get("size", "0")) == expected_size,
            parse_int(idx_values.get("event_count", "0")) == event_count,
            parse_hex_u32(idx_values.get("crc32", "0")) == header["crc32"],
            idx_values.get("complete") == "1",
        ]
        checks["idx_ok"] = all(idx_checks)

    return {
        "source": str(path),
        "idx_source": str(idx_path) if idx_path else None,
        "header": header,
        "computed_crc32": computed_crc,
        "checks": checks,
        "idx": idx_values,
        "records": records,
    }


def write_json(decoded: dict[str, Any], output: Path | None) -> None:
    text = json.dumps(decoded, indent=2, ensure_ascii=False) + "\n"
    if output is None:
        print(text, end="")
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8", newline="\n")


def write_csv(decoded: dict[str, Any], output: Path | None) -> None:
    fieldnames = [
        "index",
        "timestamp_ms",
        "timestamp_ms_unwrapped",
        "domain",
        "domain_name",
        "event_id",
        "event_name",
        "severity",
        "severity_name",
        "arg0",
        "arg1",
        "details",
    ]
    if output is None:
        writer = csv.DictWriter(sys.stdout, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        for record in decoded["records"]:
            row = dict(record)
            row["details"] = json.dumps(row["details"], ensure_ascii=False, separators=(",", ":"))
            writer.writerow(row)
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fieldnames, lineterminator="\n")
            writer.writeheader()
            for record in decoded["records"]:
                row = dict(record)
                row["details"] = json.dumps(row["details"], ensure_ascii=False, separators=(",", ":"))
                writer.writerow(row)


def main() -> int:
    args = parse_args()
    decoded = decode_trace(args.trace_bin, args.idx)
    checks = decoded["checks"]
    bad_checks = [name for name, ok in checks.items() if not ok]
    if args.csv:
        write_csv(decoded, args.output)
    else:
        write_json(decoded, args.output)
    if bad_checks and not args.allow_bad_crc:
        print(f"trace check failed: {', '.join(bad_checks)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
