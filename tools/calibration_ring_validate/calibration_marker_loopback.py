#!/usr/bin/env python3
"""Validate real MARK dual-slot edges on one physical board (diagnostic only)."""

from __future__ import annotations

import argparse
import csv
from dataclasses import asdict
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tdma_start_ring import (  # noqa: E402
    board_command, close_persistent_connections, discover,
)
from tdma_field_parse import parse_status_named  # noqa: E402
from calibration_ring_validate.calibration_clk_codebook_eval import (  # noqa: E402
    marker_raw_waveform, marker_logical_bits,
)
from calibration_ring_validate.calibration_marker_waveform import (  # noqa: E402
    validate_marker_window,
)

FIELDS = (
    "version", "state", "scope", "flags", "reject_reason", "epoch", "codebook",
    "sample_period_ns", "marker_samples", "tx_words", "tx_dma_remaining",
    "tx_drained", "requested_words", "produced_words", "edge_mask",
    "t1_lo", "t1_hi", "t2_lo", "t2_hi", "t3_lo", "t3_hi", "t4_lo", "t4_hi",
)
V2_FIELDS = FIELDS + ("capture_generation",)
PINS = (26, 27, 29, 24)
REQUIRED_FLAGS = 127
MARKER_LEAD_WORDS = 8
MARKER_TAIL_WORDS = 2
REJECT_EDGE_MISSING = 1
STATUS_QUERY = "READ:CALibration:MARKer:LOOP?"
MAX_MARKER_DISTANCE = 1536


def evaluate_full_marker(samples: list[int], times: list[int], *,
                         epoch: int, codebook: int,
                         pins: tuple[int, ...] = PINS) -> dict[str, object]:
    """Decode all four real pad waveforms without adding synthetic samples."""
    waveform, vector = marker_raw_waveform(codebook_id=codebook, epoch=epoch, source_node=0)
    logical, _, _ = marker_logical_bits(codebook_id=codebook, epoch=epoch, source_node=0)
    first_fall = next(index for index in range(1, len(waveform))
                      if waveform[index - 1] and not waveform[index])
    channels = []
    for pin, timestamp in zip(pins, times):
        channel = [(sample >> (pin - 24)) & 1 for sample in samples]
        anchor = timestamp // 4 - first_fall
        candidates = []
        for start in range(anchor - 2, anchor + 3):
            if start < 0 or start + len(waveform) > len(channel):
                continue
            distance = sum(expected != actual for expected, actual in
                           zip(waveform, channel[start:start + len(waveform)]))
            candidates.append((distance, start))
        if not candidates:
            channels.append({"pin": pin, "passed": False, "error": "capture_truncated"})
            continue
        candidates.sort()
        distance, start = candidates[0]
        validation = validate_marker_window(channel, start,
                                            half_chip_samples=vector.half_chip_samples,
                                            expected_header=vector.header)
        decoded = []
        half = vector.half_chip_samples
        for index in range(len(logical)):
            offset = start + index * half * 2
            first = sum(channel[offset:offset + half])
            second = sum(channel[offset + half:offset + 2 * half])
            decoded.append(None if first * 2 == half or second * 2 == half or
                           (first * 2 > half) == (second * 2 > half)
                           else int(first * 2 > half))
        bit_errors = sum(actual != expected for actual, expected in zip(decoded, logical))
        margin = candidates[1][0] - distance if len(candidates) > 1 else 0
        channels.append({
            "pin": pin, "marker_start_sample": start,
            "raw_sample_count": len(waveform), "logical_bit_count": len(logical),
            "best_distance": distance, "margin": margin,
            "logical_bit_errors": bit_errors, "marker_validation": validation,
            "passed": (validation["all_flags_valid"] and bit_errors == 0 and
                       distance <= MAX_MARKER_DISTANCE and margin > 0),
        })
    return {"passed": len(channels) == 4 and all(row["passed"] for row in channels),
            "channels": channels, "synthetic_prefix_samples": 0,
            "alignment_radius_samples": 2,
            "max_best_distance": MAX_MARKER_DISTANCE,
            "source_node": 0, "epoch": epoch, "codebook": codebook}


def csv_u32(raw: str) -> list[int]:
    values = [int(value.strip(), 0) for value in next(csv.reader([raw]), [])]
    if not values or any(not 0 <= value <= 0xFFFFFFFF for value in values):
        raise ValueError("invalid unsigned response")
    return values


def parse_status(raw: str) -> dict[str, int]:
    values = csv_u32(raw)
    fields = {len(FIELDS): FIELDS, len(V2_FIELDS): V2_FIELDS}.get(len(values))
    if fields is None or (values[0], len(values)) not in ((1, len(FIELDS)), (2, len(V2_FIELDS))):
        raise ValueError("MARK loop status version/width mismatch")
    result = dict(zip(fields, values))
    for edge in range(1, 5):
        result[f"t{edge}_ns"] = (result[f"t{edge}_lo"] |
                                  result[f"t{edge}_hi"] << 32)
    return result


def parse_data(raw: str, *, epoch: int, total: int,
               offset: int, count: int) -> list[int]:
    values = csv_u32(raw)
    if len(values) != count + 4 or values[:4] != [epoch, total, offset, count]:
        raise ValueError("MARK raw capture identity/length mismatch")
    return values[4:]


def decode_edges(words: list[int], period_ns: int) -> dict[str, object]:
    if period_ns != 4 or not words:
        raise ValueError("expected nonempty 4 ns raw capture")
    if any(not 0 <= word <= 0xFFFFFFFF for word in words):
        raise ValueError("capture word outside u32")
    samples = [(word >> (8 * byte)) & 0xFF for word in words for byte in range(4)]
    times: list[int | None] = []
    for pin in PINS:
        mask = 1 << (pin - 24)
        times.append(next((index * period_ns for index in range(1, len(samples))
                           if samples[index - 1] & mask and not samples[index] & mask),
                          None))
    mask = sum(1 << (pin - 24) for pin in PINS)
    return {"times_ns": times, "pre_edge_high": samples[0] & mask == mask,
            "samples": samples}


def evaluate(status: dict[str, int], words: list[int], *, epoch: int,
             codebook: int, requested_words: int,
             fault_idle_high: bool = False,
             require_full_marker: bool = False) -> dict[str, object]:
    failures: list[str] = []
    expected = {"scope": 1, "epoch": epoch, "codebook": codebook,
                "sample_period_ns": 4, "requested_words": requested_words,
                "produced_words": requested_words, "tx_dma_remaining": 0,
                "tx_drained": 1}
    if status["version"] not in (1, 2):
        failures.append("version")
    if status["version"] == 2 and status.get("capture_generation", 0) == 0:
        failures.append("capture_generation")
    if require_full_marker and status["version"] != 2:
        failures.append("full_marker_requires_generation")
    for field, value in expected.items():
        if status[field] != value:
            failures.append(field)
    if len(words) != requested_words:
        failures.append("raw_word_count")
    decoded = decode_edges(words, status["sample_period_ns"])
    times = decoded["times_ns"]
    if not decoded["pre_edge_high"]:
        failures.append("missing_pre_edge_high")
    waveform, vector = marker_raw_waveform(codebook_id=codebook, epoch=epoch, source_node=0)
    if status["marker_samples"] != vector.raw_samples:
        failures.append("marker_samples")
    if status["tx_words"] != vector.raw_words + MARKER_LEAD_WORDS + MARKER_TAIL_WORDS:
        failures.append("tx_words")
    required_flags = REQUIRED_FLAGS & ~8 if fault_idle_high else REQUIRED_FLAGS
    if status["flags"] & required_flags != required_flags:
        failures.append("flags")
    metrics: dict[str, object] = {}
    if fault_idle_high:
        if status["flags"] & 8:
            failures.append("unexpected_fault_sync_match")
        if status["state"] != 3 or status["reject_reason"] != REJECT_EDGE_MISSING:
            failures.append("expected_missing_edges_rejection")
        if status["edge_mask"] != 0 or any(value is not None for value in times):
            failures.append("unexpected_fault_edge")
        if any(status[f"t{edge}_ns"] != 0 for edge in range(1, 5)):
            failures.append("unexpected_fault_timestamp")
    else:
        if status["state"] != 2 or status["reject_reason"] != 0:
            failures.append("state_or_reject")
        if status["edge_mask"] != 15:
            failures.append("edge_mask")
        if any(value is None for value in times):
            failures.append("raw_missing_edge")
        else:
            if not 0 < times[0] < times[1] < times[2] < times[3]:
                failures.append("edge_order")
            for edge, timestamp in enumerate(times, 1):
                if status[f"t{edge}_ns"] != timestamp:
                    failures.append(f"t{edge}_raw_mismatch")
            first_fall = next(index for index in range(1, len(waveform))
                              if waveform[index - 1] == 1 and waveform[index] == 0)
            start = times[0] // 4
            length = min(len(decoded["samples"]) - start, len(waveform) - first_fall)
            mismatches = sum(((decoded["samples"][start + index] >> 2) & 1) !=
                             waveform[first_fall + index] for index in range(length))
            if length < 32 or mismatches:
                failures.append("tx_marker_prefix")
            metrics = {
                "origin_rtt_ns": times[3] - times[0],
                "responder_residence_ns": times[2] - times[1],
                "forward_path_ns": times[1] - times[0],
                "return_path_ns": times[3] - times[2],
                "tx_prefix_compared_samples": length,
                "tx_prefix_mismatches": mismatches,
            }
            if require_full_marker:
                metrics["full_marker"] = evaluate_full_marker(
                    decoded["samples"], times, epoch=epoch, codebook=codebook)
                if not metrics["full_marker"]["passed"]:
                    failures.append("full_marker")
    return {"passed": not failures, "failures": failures, "raw_times_ns": times,
            "pre_edge_high": decoded["pre_edge_high"], **metrics,
            "production_training_certified": False,
            "full_marker_correlation_measured": "full_marker" in metrics}


def run_trial(exchange, args, epoch: int) -> dict[str, object]:
    result: dict[str, object] = {"epoch": epoch, "passed": False}

    def wait_state(states, expected_epoch=None):
        deadline = time.monotonic() + args.capture_timeout
        last = None
        while time.monotonic() < deadline:
            last = parse_status(exchange(STATUS_QUERY))
            if last["state"] in states and (expected_epoch is None or
                                             last["epoch"] == expected_epoch):
                return last
            time.sleep(0.02)
        raise RuntimeError(f"MARK loop state timeout: {last}")

    def stop():
        response = exchange("CALibration:MARKer:LOOP:STOP")
        if response.strip('"') != "OK":
            raise RuntimeError(f"MARK loop STOP rejected: {response}")
        time.sleep(0.02)
        return wait_state({0})

    try:
        stop()
        request = [args.codebook, epoch, args.capture_words, int(args.fault_idle_high)]
        capture_started = time.monotonic()
        response = exchange("CALibration:MARKer:LOOP:STARt " +
                            ",".join(map(str, request)))
        if csv_u32(response) != request:
            raise RuntimeError(f"MARK loop START identity mismatch: {response}")
        status = wait_state({2, 3}, epoch)
        result["capture_elapsed_s"] = time.monotonic() - capture_started
        result["status"] = status
        generation = status.get("capture_generation", 0)
        if status["version"] == 2 and generation == 0:
            raise RuntimeError("MARK capture generation missing")
        if args.capture_words > 256 and (status["version"] != 2 or generation == 0):
            raise RuntimeError("full capture requires generation-bound firmware")
        if not 1 <= status["produced_words"] <= args.capture_words:
            raise RuntimeError("MARK capture incomplete or exceeds request")
        words = []
        while len(words) < status["produced_words"]:
            offset = len(words)
            count = min(32, status["produced_words"] - offset)
            command = f"READ:CALibration:MARKer:LOOP:DATA? {epoch},{offset},{count}"
            if generation:
                command += f",{generation}"
            raw = exchange(command)
            words.extend(parse_data(raw, epoch=epoch, total=status["produced_words"],
                                    offset=offset, count=count))
        result["capture"] = {"schema": "HAOFV_MARKER_LOOP_RAW_V1", "epoch": epoch,
                             "capture_generation": generation,
                             "sample_period_ns": 4, "gpio_base": 24,
                             "sample_bits": 8, "words": words}
        if parse_status(exchange(STATUS_QUERY)) != status:
            raise RuntimeError("MARK snapshot changed during raw download")
        result.update(evaluate(status, words, epoch=epoch, codebook=args.codebook,
                               requested_words=args.capture_words,
                               fault_idle_high=args.fault_idle_high,
                               require_full_marker=getattr(args, "full_marker", False)))
    except (ValueError, RuntimeError, OSError) as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
        result["passed"] = False
    finally:
        try:
            result["cleanup"] = stop()
        except (ValueError, RuntimeError, OSError) as exc:
            result["cleanup_error"] = f"{type(exc).__name__}: {exc}"
            result["passed"] = False
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", required=True)
    parser.add_argument("--expected-build", required=True)
    parser.add_argument("--codebook", type=int, choices=range(4), default=1)
    parser.add_argument("--epoch", type=int, default=1)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--capture-words", type=int, default=256)
    parser.add_argument("--fault-idle-high", action="store_true")
    parser.add_argument("--full-marker", action="store_true",
                        help="require complete measured MARK on all four pads")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--capture-timeout", type=float, default=5.0)
    parser.add_argument("--settle", type=float, default=0.05)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    if not (1 <= args.repeats <= 100 and 1 <= args.epoch <= 255 and
            args.epoch + args.repeats - 1 <= 255 and 16 <= args.capture_words <= 2048):
        parser.error("invalid repeat/epoch/capture bounds")
    args.board_ids = [args.board_id]
    args.keep_open = True
    args.action_timeout = args.timeout
    args.out_dir.mkdir(parents=True, exist_ok=False)
    output = {"schema": "HAOFV_MARKER_LOOP_DIAGNOSTIC_V1", "passed": False,
              "measurement_scope": "CS_TO_DATA_LOOPBACK_DIAGNOSTIC",
              "physical_board_count": 1, "logical_slot_count": 2,
              "common_capture_clock": True, "source_node": 0,
              "pins_t1_t2_t3_t4": list(PINS),
              "fault_idle_high": args.fault_idle_high,
              "full_marker_required": args.full_marker,
              "production_training_certified": False,
              "parameter_flash_saved": False, "trials": [], "transcript": []}
    try:
        boards = discover(args)
        board = boards.get(args.board_id)
        if board is None or board.build != args.expected_build:
            raise RuntimeError("target UID/build not found; no mutation performed")
        output["board"] = asdict(board)

        def exchange(command):
            raw = board_command(board, command, args)
            output["transcript"].append({"command": command, "response": raw,
                                         "monotonic": time.monotonic()})
            if raw == "<timeout>":
                raise RuntimeError(f"SCPI timeout: {command}")
            return raw

        exchange("SYSTem:TDMA:RING:STOP")
        deadline = time.monotonic() + args.capture_timeout
        while True:
            ring = parse_status_named(exchange("SYSTem:REFMEM:SYNC:TDMA:STATus?"))
            if ring["ring_up_running"] == 0 and ring["ring_down_running"] == 0:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("TDMA STOP not confirmed")
            time.sleep(0.02)
        for epoch in range(args.epoch, args.epoch + args.repeats):
            result = run_trial(exchange, args, epoch)
            output["trials"].append(result)
            (args.out_dir / f"trial-{epoch}.json").write_text(
                json.dumps(result, indent=2) + "\n", encoding="utf-8")
            print(f"epoch={epoch} passed={result['passed']} "
                  f"edges={result.get('raw_times_ns')} error={result.get('error', '')}", flush=True)
            if "cleanup_error" in result:
                break
        output["final_idn"] = exchange("*IDN?")
        output["final_build"] = exchange("SYST:FW:BUILD?").strip('"')
        output["final_error"] = exchange("SYST:ERR?")
        output["passed"] = (len(output["trials"]) == args.repeats and
                            all(trial["passed"] for trial in output["trials"]) and
                            output["final_idn"] == board.idn and
                            output["final_build"] == args.expected_build and
                            output["final_error"].startswith('0,'))
    except (RuntimeError, ValueError, OSError) as exc:
        output["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        close_persistent_connections()
        (args.out_dir / "summary.json").write_text(
            json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"passed={output['passed']} out_dir={args.out_dir}")
    return 0 if output["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
