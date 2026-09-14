"""Decode immutable board-clock diagnostic records and export frozen RAM.

No function in this module obtains a live TDMA sample through SCPI. The host
only arms/starts a finite acquisition and reads its frozen result afterwards.
"""
from __future__ import annotations

import csv
import re
import struct
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIELDS_PATH = ROOT / "components/diagnostics/inc/diagnostics_tdma_record_fields.def"
MAGIC, SAMPLE, END = 0x524D4454, 0x504D4153, 0x444E4554
STATUS_FIELDS = ("state", "epoch", "interval_us", "requested", "written",
                 "missed", "reason", "bytes", "job_id")


def field_schema(version=2):
    if version not in (1, 2):
        raise ValueError("unknown record schema")
    fields = re.findall(r"^RECORD_(U32|I32|U64)\((\w+), (\w+),",
                        FIELDS_PATH.read_text(encoding="utf-8"), re.M)
    # V2 appends observer diagnostics. The original V1 ordering is immutable,
    # allowing offline review of sealed evidence from previous builds.
    return [field for field in fields if version == 2 or field[1] != "event"]


def named_snapshot(values, version=2):
    groups = {}
    cursor = 0
    for kind, group, name in field_schema(version):
        value = values[cursor]
        cursor += 1
        if kind == "U64":
            value |= values[cursor] << 32
            cursor += 1
        elif kind == "I32" and value >= 0x80000000:
            value -= 0x100000000
        groups.setdefault(group, {})[name] = value
    assert cursor == len(values)
    runtime = groups["runtime"]
    runtime["ring_local_node"] = runtime.pop("ring_local_slot_id")
    runtime["ring_reference_node"] = runtime.pop("ring_reference_slot_id")
    runtime["node_index"] = runtime["ring_local_node"]
    process = groups.pop("process")
    process["local_node"] = process.pop("local_slot")
    fifo = groups.pop("fifo")
    fifo["tx_active_buffer"] = fifo.pop("tx_active_slot")
    groups["flight"] = {"process": process, "fifo": fifo}
    raw_schedule = groups.pop("schedule")
    schedule = {k: v for k, v in raw_schedule.items() if not k.startswith("phase")}
    schedule["phase_count"] = raw_schedule["phase_count"]
    schedule["phases"] = [
        {k.split("_", 1)[1]: v for k, v in raw_schedule.items()
         if k.startswith(f"phase{i}_")}
        for i in range(raw_schedule["phase_count"])]
    groups["schedule"] = schedule
    return groups


def decode_record(data: bytes, *, expected_build=None, expected_board=None,
                  expected_epoch=None):
    if len(data) % 4 or len(data) < 128:
        raise ValueError("record truncated or misaligned")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    h = words[:16]
    version = h[1]
    value_count = sum(2 if kind == "U64" else 1 for kind, _, _ in field_schema(version))
    if (h[:4] != (MAGIC, version, 16, value_count) or h[11] != 1000000 or
            h[12] != (value_count + 31) // 32 or any(h[13:])):
        raise ValueError("unknown record schema")
    build, board = h[7] | h[8] << 32, h[9] | h[10] << 32
    for label, actual, expected in (("build", build, expected_build),
                                    ("board", board, expected_board),
                                    ("epoch", h[6], expected_epoch)):
        if expected is not None and actual != int(expected):
            raise ValueError(f"record {label} mismatch")
    cursor = 16
    values = [0] * value_count
    samples = []
    footer = None
    while cursor < len(words):
        if len(words) - cursor < 2:
            raise ValueError("truncated record packet")
        tag, count = words[cursor:cursor + 2]
        if tag == END:
            if count != 16 or cursor + count != len(words):
                raise ValueError("bad terminal record")
            f = words[cursor:]
            if f[10] != cursor * 4 or f[11] != zlib.crc32(data[:cursor * 4]):
                raise ValueError("record CRC/length mismatch")
            if f[2] != h[5] or f[12] != h[6] or any(f[13:]):
                raise ValueError("terminal identity mismatch")
            footer = dict(requested=f[2], written=f[3], missed=f[4], reason=f[5],
                          trigger_us=f[6] | f[7] << 32,
                          completed_us=f[8] | f[9] << 32)
            break
        if tag != SAMPLE or count < 12 + h[12] or cursor + count > len(words):
            raise ValueError("bad sample packet")
        p = words[cursor:cursor + count]
        bitmap = p[12:12 + h[12]]
        if value_count % 32 and bitmap[-1] >> (value_count % 32):
            raise ValueError("unknown bitmap fields")
        changed = [i for i in range(value_count) if bitmap[i // 32] & (1 << (i % 32))]
        if len(changed) != count - 12 - h[12] or p[11] != 0:
            raise ValueError("delta length mismatch")
        if not samples and (p[2] != 0xFFFFFFFF or len(changed) != value_count):
            raise ValueError("complete baseline missing")
        for index, value in zip(changed, p[12 + h[12]:], strict=True):
            values[index] = value
        samples.append(dict(slot=p[2], valid_mask=p[3],
                            target_us=p[4] | p[5] << 32,
                            started_us=p[6] | p[7] << 32,
                            completed_us=p[8] | p[9] << 32,
                            skipped_before=p[10], snapshot=named_snapshot(values, version)))
        cursor += count
    if footer is None or not samples or footer["written"] != len(samples) - 1:
        raise ValueError("incomplete record or sample count mismatch")
    errors = []
    previous = -1
    skipped = 0
    for sample in samples[1:]:
        slot = sample["slot"]
        if not previous < slot < h[5] or sample["skipped_before"] != slot - previous - 1:
            raise ValueError("record slot sequence mismatch")
        if sample["target_us"] != footer["trigger_us"] + slot * h[4]:
            raise ValueError("record target clock mismatch")
        skipped += sample["skipped_before"]
        previous = slot
    if footer["reason"] == 0:
        skipped += h[5] - previous - 1
    if skipped != footer["missed"]:
        raise ValueError("record missing-slot count mismatch")
    for sample in samples:
        if sample["valid_mask"] != 0x3F:
            errors.append(f"snapshot_unavailable:{sample['slot']}")
        if not sample["target_us"] <= sample["started_us"] <= sample["completed_us"] <= footer["completed_us"]:
            errors.append(f"snapshot_clock_invalid:{sample['slot']}")
    if footer["reason"] != 0:
        errors.append(f"record_terminal_reason:{footer['reason']}")
    if footer["missed"]:
        errors.append("record_missed_sampling_slots")
    return dict(schema=f"HAOFV_TDMA_BOARD_RECORD_V{version}", build=str(build), board=f"{board:016X}",
                epoch=h[6], interval_us=h[4], baseline=samples[0], samples=samples[1:],
                terminal=footer, errors=errors, collection_passed=not errors,
                timing_scope="Core0 snapshot interval; not simultaneous fields or per-cycle WCET")


def record_status(response):
    if response.strip().strip('"') == "BUSY":
        return None
    values = next(csv.reader([response]))
    if len(values) != len(STATUS_FIELDS):
        raise ValueError("invalid recorder control status")
    return dict(zip(STATUS_FIELDS, (int(v.strip().strip('"'), 0) for v in values), strict=True))


def export_frozen(board, args, command, *, epoch: int, path: Path):
    status = record_status(command(board, "SYSTem:TDMA:RECord:STATus?", args))
    if status is None or status["state"] != 5 or status["epoch"] != epoch:
        raise RuntimeError(f"recorder did not freeze requested epoch: {status}")
    data = bytearray()
    while len(data) < status["bytes"]:
        size = min(256, status["bytes"] - len(data))
        response = command(board, f"SYSTem:TDMA:RECord:READ? {len(data)},{size}", args)
        fields = next(csv.reader([response]))
        if len(fields) != 3 or int(fields[0]) != len(data) or int(fields[1]) != size:
            raise ValueError("frozen export range mismatch")
        chunk = bytes.fromhex(fields[2].strip().strip('"'))
        if len(chunk) != size:
            raise ValueError("frozen export chunk length mismatch")
        data.extend(chunk)
    path.write_bytes(data)
    decoded = decode_record(data, expected_build=args.expected_build,
                            expected_board=int(board.address, 16), expected_epoch=epoch)
    return dict(path=str(path), status=status, decoded=decoded)
