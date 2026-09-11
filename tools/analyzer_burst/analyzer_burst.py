"""Capture and decode finite, simultaneous TDMA pad samples through SYNC_IO.

Only the analyzer mailbox is mutated. The running TDMA ring remains owned by
TDMA. Captured segments come from Core0/StorageAO; no software timestamp is
substituted for a sample index. Passing this tool is not a B0 pipeline proof.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import secrets
import struct
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))
from tools.analyzer_trace_export.analyzer_trace_export import (  # noqa: E402
    download_file,
)
from tools.calibration_ring_validate.trn03_closed_loop import (  # noqa: E402
    RUNTIME_FIELDS, parse_snapshot,
)
from tools.scpi_common.scpi_serial import SerialSession  # noqa: E402

FACT_FIELDS = (
    "schema", "state", "capture_sequence", "end_reason", "timing_valid",
    "requested_words", "captured_words", "clk_sys_hz", "clkdiv_256",
    "sample_cycles", "samples_per_word", "sample_bits", "pin_base",
    "source_mask", "trigger_pin", "trigger_level", "profile_identity",
    "persona_generation", "pio_fdebug", "dma_ctrl", "manager_error", "conflict_mask",
    "capture_tag", "dma_remaining",
)
EXPORT_FIELDS = ("capture_sequence", "failure_count", "last_failed_job", "last_error", "retry_count")
HEADER = struct.Struct("<IHH5I24I16s")
MAGIC = 0x54534241
ERROR_FIELDS = (
    "ring_adapter_rx_bad_count", "ring_adapter_rx_transport_bad_count",
    "ring_adapter_rx_schedule_bad_count", "ring_adapter_rx_profile_bad_count",
)


def decode_segment(data: bytes) -> dict:
    if len(data) < HEADER.size:
        raise ValueError("truncated burst header")
    magic, schema, header_size, session, segment, first, count, crc, *rest = HEADER.unpack_from(data)
    if (magic, schema, header_size) != (MAGIC, 1, HEADER.size):
        raise ValueError("unsupported burst schema")
    facts = dict(zip(FACT_FIELDS, rest[:-1], strict=True))
    build = rest[-1].split(b"\0", 1)[0].decode("ascii")
    if not re.fullmatch(r"\d{14}", build):
        raise ValueError("invalid build identity")
    if (facts["schema"] != 1 or facts["state"] != 2 or
            facts["capture_sequence"] == 0 or facts["persona_generation"] == 0 or
            facts["capture_tag"] != session or session == 0 or
            facts["samples_per_word"] != 5 or facts["sample_bits"] != 6 or
            facts["sample_cycles"] != 2 or facts["clk_sys_hz"] == 0 or
            not 256 <= facts["clkdiv_256"] <= 16 * 256 or
            facts["clkdiv_256"] % 256 != 0 or facts["pin_base"] > 26 or
            facts["trigger_pin"] > 31 or
            facts["source_mask"] != 63 << facts["pin_base"] or
            not facts["source_mask"] & (1 << facts["trigger_pin"]) or
            facts["trigger_level"] != 0):
        raise ValueError("invalid capture metadata")
    pins = [(facts["profile_identity"] >> shift) & 31 for shift in range(0, 30, 5)]
    if (set(pins) != set(range(facts["pin_base"], facts["pin_base"] + 6)) or
            facts["profile_identity"] >> 30 or facts["trigger_pin"] not in (pins[1], pins[4]) or
            facts["timing_valid"] not in (0, 1) or not 1 <= facts["end_reason"] <= 5):
        raise ValueError("invalid capture metadata")
    if not (0 <= first <= first + count <= facts["captured_words"] <= facts["requested_words"] <= 8192
            and facts["requested_words"] > 0 and count <= 1024):
        raise ValueError("invalid word range")
    if count == 0 and (facts["captured_words"] != 0 or segment != 0):
        raise ValueError("empty segment within capture")
    payload = data[header_size:]
    if len(payload) != count * 4 or zlib.crc32(payload) != crc:
        raise ValueError("burst payload size/CRC mismatch")
    words = list(struct.unpack(f"<{count}I", payload))
    if any(word >> 30 for word in words):
        raise ValueError("nonzero packed-word padding")
    return {"session": session, "segment_index": segment, "first_word": first,
            "word_count": count, "build_id": build, "capture": facts, "words": words}


def assemble_capture(segments: list[dict]) -> dict:
    if not segments:
        raise ValueError("no capture segments")
    ordered = sorted(segments, key=lambda row: row["segment_index"])
    base = ordered[0]
    words: list[int] = []
    for index, row in enumerate(ordered):
        if (row["segment_index"] != index or row["first_word"] != len(words) or
                row["capture"] != base["capture"] or row["session"] != base["session"] or
                row["build_id"] != base["build_id"]):
            raise ValueError("missing/duplicate segment or mixed capture identity")
        words.extend(row["words"])
    facts = base["capture"]
    if len(words) != facts["captured_words"]:
        raise ValueError("incomplete capture")
    levels = [(word >> shift) & 63 for word in words for shift in (24, 18, 12, 6, 0)]
    period_ns = facts["sample_cycles"] * facts["clkdiv_256"] * 1e9 / (256 * facts["clk_sys_hz"])
    valid = (facts["timing_valid"] == 1 and facts["end_reason"] == 1 and
             facts["pio_fdebug"] & 1 == 0 and  # analyzer's PIO0 SM0 RXSTALL
             facts["dma_ctrl"] & (3 << 29) == 0 and
             facts["manager_error"] == facts["conflict_mask"] == 0 and
             facts["dma_remaining"] == 0 and
             facts["captured_words"] == facts["requested_words"])
    return {"schema": "HAOFV_ANALYZER_FINITE_CAPTURE_V1", "build_id": base["build_id"],
            "session": base["session"], "capture": facts, "sample_period_ns": period_ns,
            "sample_count": len(levels), "sample_zero": "first retained PIO IN, relative local time",
            "timing_valid": valid, "b0_pipeline_accepted": False,
            "levels": levels}


def download_capture(query, out_dir: Path, observed: dict, build: str) -> tuple[dict, list]:
    """Read the exact files bound to the completed epoch; no catalog or SAVE.

    StorageAO writes fixed-size segments except for the final partial segment.
    Every header is checked against the published snapshot, including its tag.
    This function can also recover a previously recorded RELEASED epoch.
    """
    if observed["state"] != 3:
        raise ValueError("capture export has not released its lease")
    expected = {**observed, "state": 2}
    segments, downloads = [], []
    for index in range(max(1, (observed["captured_words"] + 1023) // 1024)):
        first = index * 1024
        count = min(1024, observed["captured_words"] - first)
        name = (f'burst_{observed["capture_tag"]:08d}_'
                f'{observed["capture_sequence"]:08d}_{index:04d}.bin')
        remote = "/traces/run/" + name
        data, pages = download_file(query, remote, expected_size=HEADER.size + count * 4)
        path = out_dir / name
        path.write_bytes(data)
        decoded = decode_segment(data)
        if decoded["build_id"] != build or decoded["capture"] != expected:
            raise ValueError("downloaded capture identity mismatch; raw file retained")
        segments.append(decoded)
        downloads.append({"remote_path": remote, "local_path": str(path),
            "sha256": hashlib.sha256(data).hexdigest(), "pages": pages})
    return assemble_capture(segments), downloads


def capture(args: argparse.Namespace) -> dict:
    args.out_dir.mkdir(parents=True, exist_ok=False)
    report: dict = {"passed": False, "identity": args.identity, "port": args.port,
                    "scope": "finite analyzer integration, not B0 flight acceptance",
                    "queries": [], "error": ""}
    try:
        with SerialSession(args.port, 115200, 3.0, 0.2, read_timeout_s=0.02) as session:
            def query(command: str) -> str:
                raw = session.execute(command)
                report["queries"].append({"at_s": time.monotonic(), "command": command, "raw": raw})
                return raw

            identity = query("*IDN?")
            build = query("SYSTem:FW:BUILD?").strip().strip('"')
            if args.identity not in identity or build != args.expected_build:
                raise ValueError("live board/build identity mismatch")
            def runtime() -> dict:
                return parse_snapshot(query("SYSTem:TDMA:RING:STATus?"), RUNTIME_FIELDS, args.identity)
            def status() -> dict:
                return parse_snapshot(query("REALtime:IO:ANALyzer:BURSt:STATe?"), FACT_FIELDS, args.identity)

            before = runtime()
            old_sequence = status()["capture_sequence"]
            report["before"] = before
            capture_tag = secrets.randbits(32) or 1
            report["capture_tag"] = capture_tag
            command = f"REALtime:IO:ANALyzer:BURSt:ARM {args.words},{args.clkdiv},1000000,{args.trigger_rx},{capture_tag}"
            report["arm_response"] = query(command)
            deadline = time.monotonic() + args.export_timeout
            while time.monotonic() < deadline:
                observed = status()
                report["last_status"] = observed
                if observed["capture_sequence"] != old_sequence and observed["capture_tag"] == capture_tag:
                    if observed["state"] == 4:
                        raise RuntimeError(f"analyzer resource/ARM rejected: {observed}")
                    if observed["state"] == 2:
                        export = parse_snapshot(query("REALtime:IO:ANALyzer:BURSt:EXPORT?"),
                                                EXPORT_FIELDS, args.identity)
                        report["export_status"] = export
                        if (export["capture_sequence"] == observed["capture_sequence"] and
                                export["failure_count"] != 0):
                            report["storage_job"] = query("SYSTem:STORage:JOB?")
                            raise RuntimeError("StorageAO export failed; frozen capture retained for explicit SAVE retry")
                    if observed["state"] == 3:
                        break
                time.sleep(0.25)
            else:
                report["storage_job"] = query("SYSTem:STORage:JOB?")
                report["analyzer_control"] = query("REALtime:IO:ANALyzer:STATe?")
                raise TimeoutError("finite capture/export did not release its lease")
            report["after"] = runtime()
            report["storage_job"] = query("SYSTem:STORage:JOB?")
            assembled, report["downloads"] = download_capture(query, args.out_dir, observed, build)
            (args.out_dir / "capture.json").write_text(json.dumps(assembled) + "\n", encoding="utf-8")
            report["capture"] = {k: v for k, v in assembled.items() if k != "levels"}
            after = report["after"]
            report["tdma_error_deltas"] = {k: after[k] - before[k] for k in ERROR_FIELDS}
            report["tdma_rx_delta"] = after["ring_adapter_rx_count"] - before["ring_adapter_rx_count"]
            report["passed"] = (assembled["timing_valid"] and report["tdma_rx_delta"] > 0 and
                all(value == 0 for value in report["tdma_error_deltas"].values()) and
                before["ring_enabled"] == after["ring_enabled"] == 1)
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        (args.out_dir / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--identity", required=True)
    parser.add_argument("--expected-build", required=True)
    parser.add_argument("--words", type=int, default=8192)
    parser.add_argument("--clkdiv", type=int, default=1)
    parser.add_argument("--trigger-rx", type=int, choices=(0, 1), default=0)
    parser.add_argument("--export-timeout", type=float, default=120.0)
    parser.add_argument("--out-dir", required=True, type=Path)
    args = parser.parse_args()
    report = capture(args)
    print(json.dumps({k: report[k] for k in ("passed", "error")}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
