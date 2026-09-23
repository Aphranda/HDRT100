#!/usr/bin/env python3
"""Measure independent SCK launch offsets using real concurrent board slots."""

from __future__ import annotations

import argparse
import csv
from dataclasses import asdict
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from calibration_ring_validate import calibration_marker_loopback as marker  # noqa: E402

PINS = (25, 28, 29, 24)
FIELDS = marker.V2_FIELDS + (
    "offset_samples", "origin_lead_samples", "origin_high_samples", "launch_guard_samples",
)
STATUS_QUERY = "READ:CAL:SCK:LOOP?"


def parse_status(raw: str) -> dict[str, int]:
    values = [int(value.strip(), 0) for value in next(csv.reader([raw]), [])]
    if len(values) != len(FIELDS) or values[0] != 1:
        raise ValueError("SCK loop status version/width mismatch")
    for field, value in zip(FIELDS, values):
        if not ((-10 <= value <= 10) if field == "offset_samples"
                else (0 <= value <= 0xFFFFFFFF)):
            raise ValueError(f"SCK status invalid {field}")
    status = dict(zip(FIELDS, values))
    for edge in range(1, 5):
        status[f"t{edge}_ns"] = status[f"t{edge}_lo"] | status[f"t{edge}_hi"] << 32
    return status


def evaluate(status, words, *, epoch, codebook, requested_words, offset_samples,
             fault_idle_low=False):
    waveform, vector = marker.marker_raw_waveform(codebook_id=codebook, epoch=epoch, source_node=0)
    expected = dict(version=1, scope=2, epoch=epoch, codebook=codebook,
                    sample_period_ns=4, requested_words=requested_words,
                    produced_words=requested_words, offset_samples=offset_samples,
                    origin_lead_samples=64, origin_high_samples=64, launch_guard_samples=128,
                    marker_samples=vector.raw_samples, tx_dma_remaining=0, tx_drained=1,
                    tx_words=(320 + offset_samples + vector.raw_samples + 31) // 32)
    failures = [field for field, value in expected.items() if status.get(field) != value]
    if not status.get("capture_generation"):
        failures.append("capture_generation")
    if len(words) != requested_words or any(not 0 <= word <= 0xFFFFFFFF for word in words):
        failures.append("raw_word_count_or_value")
    samples = [(word >> (8 * byte)) & 255 for word in words for byte in range(4)]
    times, rises = [], []
    for pin in PINS:
        mask = 1 << (pin - 24)
        rises.append(next((i for i in range(1, len(samples))
                           if samples[i] & mask and not samples[i - 1] & mask), None))
        times.append(next((i * 4 for i in range(1, len(samples))
                           if samples[i - 1] & mask and not samples[i] & mask), None))
    mask = sum(1 << (pin - 24) for pin in PINS)
    if not samples or samples[0] & mask:
        failures.append("missing_pre_origin_low")
    if samples and any((sample ^ samples[0]) & (1 << (26 - 24)) for sample in samples):
        failures.append("unexpected_cs_activity")
    required = 119 if fault_idle_low else 127
    if status["flags"] & required != required:
        failures.append("flags")
    metrics = {}
    if fault_idle_low:
        if status["state"] != 3 or status["reject_reason"] != 1 or status["flags"] & 8:
            failures.append("expected_missing_edges_rejection")
        if any(sample & mask for sample in samples) or status["edge_mask"] != 0:
            failures.append("unexpected_fault_edge")
        if any(status[f"t{edge}_ns"] for edge in range(1, 5)):
            failures.append("unexpected_fault_timestamp")
    elif any(value is None for value in times + rises):
        failures.append("raw_missing_origin")
    else:
        if status["state"] != 2 or status["reject_reason"] or status["edge_mask"] != 15:
            failures.append("state_or_reject")
        if rises[0] < 64:
            failures.append("origin_lead_truncated")
        if not 0 < times[0] < times[1] < times[2] < times[3]:
            failures.append("edge_order")
        for edge, (timestamp, rise) in enumerate(zip(times, rises), 1):
            if status[f"t{edge}_ns"] != timestamp:
                failures.append(f"t{edge}_raw_mismatch")
            if abs(timestamp // 4 - rise - 64) > (0 if edge == 1 else 1):
                failures.append(f"t{edge}_origin_width")
        code_times = [timestamp + (128 + offset_samples + vector.half_chip_samples) * 4
                      for timestamp in times]
        full = marker.evaluate_full_marker(samples, code_times, epoch=epoch, codebook=codebook,
                                           pins=PINS)
        if not full["passed"]:
            failures.append("full_codeword")
        for index, channel in enumerate(full["channels"]):
            if "marker_start_sample" not in channel:
                continue
            pin = PINS[index]
            signal = [(sample >> (pin - 24)) & 1 for sample in samples]
            code_start = channel["marker_start_sample"]
            tolerance = 0 if index == 0 else 1
            if any(signal[times[index] // 4 + tolerance:code_start - tolerance]):
                failures.append(f"pin{pin}_launch_guard")
            tail_start = code_start + len(waveform)
            if tail_start + 64 > len(signal):
                failures.append(f"pin{pin}_tail_truncated")
            elif any(signal[tail_start + tolerance:]):
                failures.append(f"pin{pin}_tail_activity")
        tx_start = times[0] // 4 + 128 + offset_samples
        tx_samples = [(sample >> (PINS[0] - 24)) & 1 for sample in samples]
        if tx_samples[tx_start:tx_start + len(waveform)] != waveform:
            failures.append("tx_launch_offset_or_codeword")
        if any(tx_samples[times[0] // 4:tx_start]):
            failures.append("tx_launch_guard")
        metrics = dict(full_codeword=full, origin_rtt_ns=times[3] - times[0],
                       responder_residence_ns=times[2] - times[1],
                       forward_path_ns=times[1] - times[0], return_path_ns=times[3] - times[2],
                       measured_launch_gap_samples=tx_start - times[0] // 4)
    return dict(passed=not failures, failures=failures, raw_times_ns=times, **metrics,
                production_training_certified=False, destination_phase_trained=False)


def run_trial(exchange, args, epoch):
    result = dict(epoch=epoch, offset_samples=args.offset_samples, passed=False)

    def wait_state(states, expected_epoch=None):
        deadline = time.monotonic() + args.capture_timeout
        last = None
        while time.monotonic() < deadline:
            last = parse_status(exchange(STATUS_QUERY))
            if last["state"] in states and (expected_epoch is None or last["epoch"] == expected_epoch):
                return last
            time.sleep(.02)
        raise RuntimeError(f"SCK state timeout: {last}")

    def stop():
        if exchange("CAL:SCK:LOOP:STOP").strip('"') != "OK":
            raise RuntimeError("SCK STOP rejected")
        time.sleep(.02)
        return wait_state({0})

    try:
        stop()
        request = [args.codebook, epoch, args.capture_words, args.offset_samples,
                   int(args.fault_idle_low)]
        started = time.monotonic()
        response = exchange("CAL:SCK:LOOP:START " + ",".join(map(str, request)))
        if [int(value) for value in next(csv.reader([response]), [])] != request:
            raise RuntimeError("SCK START identity mismatch")
        status = wait_state({2, 3}, epoch)
        result.update(status=status, capture_elapsed_s=time.monotonic() - started)
        generation = status["capture_generation"]
        if not generation or status["scope"] != 2 or status["offset_samples"] != args.offset_samples:
            raise RuntimeError("SCK capture identity mismatch")
        if status["produced_words"] != args.capture_words:
            raise RuntimeError("SCK capture incomplete")
        words = []
        while len(words) < args.capture_words:
            offset = len(words)
            count = min(32, args.capture_words - offset)
            raw = exchange(f"READ:CAL:SCK:LOOP:DATA? {epoch},{offset},{count},{generation}")
            words.extend(marker.parse_data(raw, epoch=epoch, total=args.capture_words,
                                            offset=offset, count=count))
        result["capture"] = dict(schema="HAOFV_SCK_LOOP_RAW_V1", epoch=epoch,
                                 capture_generation=generation, gpio_base=24,
                                 sample_bits=8, sample_period_ns=4, words=words)
        if parse_status(exchange(STATUS_QUERY)) != status:
            raise RuntimeError("SCK snapshot changed during raw download")
        result.update(evaluate(status, words, epoch=epoch, codebook=args.codebook,
                               requested_words=args.capture_words, offset_samples=args.offset_samples,
                               fault_idle_low=args.fault_idle_low))
    except (RuntimeError, ValueError, OSError) as exc:
        result.update(passed=False, error=f"{type(exc).__name__}: {exc}")
    finally:
        try:
            result["cleanup"] = stop()
        except (RuntimeError, ValueError, OSError) as exc:
            result.update(passed=False, cleanup_error=str(exc))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", required=True)
    parser.add_argument("--expected-build", required=True)
    parser.add_argument("--codebook", type=int, choices=range(4), default=1)
    parser.add_argument("--epoch", type=int, default=1)
    parser.add_argument("--offset-samples", type=int, action="append")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--capture-words", type=int, default=2048)
    parser.add_argument("--fault-idle-low", action="store_true")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3)
    parser.add_argument("--capture-timeout", type=float, default=5)
    parser.add_argument("--settle", type=float, default=.05)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    offsets = args.offset_samples or [0]
    count = len(offsets) * args.repeats
    if (not 1 <= args.repeats <= 100 or not 1 <= args.epoch <= args.epoch + count - 1 <= 255 or
            any(not -10 <= offset <= 10 for offset in offsets) or args.capture_words != 2048):
        parser.error("invalid offset/repeat/epoch/full capture bounds")
    if (any(not math.isfinite(value) or value <= 0 for value in (args.timeout, args.capture_timeout)) or
            not math.isfinite(args.settle) or args.settle < 0):
        parser.error("timeouts must be finite/positive and settle finite/nonnegative")
    args.board_ids, args.keep_open, args.action_timeout = [args.board_id], True, args.timeout
    args.out_dir.mkdir(parents=True, exist_ok=False)
    output = dict(schema="HAOFV_SCK_LOOP_DIAGNOSTIC_V1", phase="TRN-01", passed=False,
                  physical_board_count=1, logical_slot_count=2, common_capture_clock=True,
                  measurement_scope="SCK_TO_DATA_LAUNCH_OFFSET_DIAGNOSTIC",
                  production_training_certified=False, destination_phase_trained=False,
                  parameter_flash_saved=False, pins_t1_t2_t3_t4=list(PINS),
                  fault_idle_low=args.fault_idle_low,
                  offset_samples=offsets, trials=[], transcript=[])
    try:
        board = marker.discover(args).get(args.board_id)
        if board is None or board.build != args.expected_build:
            raise RuntimeError("target UID/build not found; no mutation performed")
        output["board"] = asdict(board)

        def exchange(command):
            response = marker.board_command(board, command, args)
            output["transcript"].append(dict(command=command, response=response,
                                              monotonic=time.monotonic()))
            if response == "<timeout>":
                raise RuntimeError(f"SCPI timeout: {command}")
            return response

        exchange("SYST:TDMA:RING:STOP")
        deadline = time.monotonic() + args.capture_timeout
        while True:
            ring = marker.parse_status_named(exchange("SYST:REFMEM:SYNC:TDMA:STAT?"))
            if not ring["ring_up_running"] and not ring["ring_down_running"]:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("TDMA STOP not confirmed")
            time.sleep(.02)
        for offset in offsets:
            args.offset_samples = offset
            for _ in range(args.repeats):
                epoch = args.epoch + len(output["trials"])
                trial = run_trial(exchange, args, epoch)
                output["trials"].append(trial)
                (args.out_dir / f"trial-{epoch}.json").write_text(json.dumps(trial, indent=2) + "\n", encoding="utf-8")
                print(f"epoch={epoch} offset={offset} passed={trial['passed']} failures={trial.get('failures')} error={trial.get('error', '')}", flush=True)
                if "cleanup_error" in trial:
                    raise RuntimeError("cleanup failed; stopping scan")
        output["final_idn"] = exchange("*IDN?")
        output["final_build"] = exchange("SYST:FW:BUILD?").strip('"')
        output["final_error"] = exchange("SYST:ERR?")
        output["passed"] = (len(output["trials"]) == count and all(t["passed"] for t in output["trials"]) and
                            output["final_idn"] == board.idn and output["final_build"] == args.expected_build and
                            output["final_error"].startswith("0,"))
    except (RuntimeError, ValueError, OSError) as exc:
        output["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        marker.close_persistent_connections()
        (args.out_dir / "summary.json").write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"passed={output['passed']} out_dir={args.out_dir}")
    return 0 if output["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
