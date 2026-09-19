#!/usr/bin/env python3
"""Capture single-board physical RJ45 calibration-loopback diagnostics.

This tool records firmware timestamps without promoting a diagnostic reference
loopback into formal calibration or VDC admission evidence.
"""
from __future__ import annotations

import argparse
import csv
from contextlib import contextmanager
import hashlib
import json
import math
import sys
import time
from pathlib import Path
from typing import Callable, Iterator

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.hardware_acceptance.sequence_trigger_acceptance import (
    open_visa_resource,
    visa_command,
)
from tools.scpi_common.board_identity import (
    normalize_build_response,
    parse_idn_response,
)
from tools.scpi_common.scpi_serial import open_serial_port, read_scpi_response


SCHEMA = "HAOFV_CALIBRATION_LOOPBACK_DIAGNOSTIC_V2"
PHYSICAL_FLAGS = 0x01 | 0x04
DIAGNOSTIC_ONLY_FLAG = 0x02
SNAPSHOT_FIELDS = (
    "armed", "complete", "sample_hz", "sample_period_ns", "produced_words",
    "edge_mask", "flags", "reject_reason", "epoch", "t1_clk_tx",
    "t2_clk_rx", "t3_data_tx", "t4_data_rx", "result_valid",
    "residence_ns", "raw_path_sum_ns", "delay_estimate_ns", "active_eligible",
)
LIMITATIONS = (
    "diagnostic_reference_loopback_only",
    "whole_rj45_paths_include_transceivers",
    "no_deembedded_cable_delay",
    "no_formal_vdc_calibration",
    "no_sync_accuracy",
    "no_multi_board",
    "no_rf",
    "no_waveform",
)


def query(serial, command: str, timeout_s: float) -> str:
    serial.write((command + "\n").encode("ascii"))
    return read_scpi_response(serial, command, timeout_s, require_match=True)


@contextmanager
def open_transport(args: argparse.Namespace) -> Iterator[Callable[[str], str]]:
    if args.visa_resource:
        with open_visa_resource(args.visa_resource, args.timeout_s) as instrument:
            yield lambda text: visa_command(instrument, text, args.timeout_s)
    else:
        with open_serial_port(
                args.port, args.baud, args.timeout_s, args.settle_s) as serial:
            yield lambda text: query(serial, text, args.timeout_s)


def parse_ints(text: str) -> list[int]:
    try:
        return [int(part.strip().strip('"'), 0)
                for part in next(csv.reader([text], strict=True), [])]
    except (csv.Error, ValueError) as exc:
        raise ValueError(f"invalid integer response: {text!r}") from exc


def parse_snapshot(text: str) -> dict[str, int | list[int]]:
    values = parse_ints(text)
    if len(values) != len(SNAPSHOT_FIELDS):
        raise ValueError(
            f"READ:CAL:LOOP? returned {len(values)} fields, expected "
            f"{len(SNAPSHOT_FIELDS)}")
    snapshot: dict[str, int | list[int]] = dict(zip(SNAPSHOT_FIELDS, values))
    snapshot["raw_fields"] = values
    snapshot["clk_path_ns"] = values[10] - values[9]
    snapshot["data_path_ns"] = values[12] - values[11]
    snapshot["computed_residence_ns"] = values[11] - values[10]
    snapshot["computed_raw_path_sum_ns"] = (
        int(snapshot["clk_path_ns"]) + int(snapshot["data_path_ns"]))
    return snapshot


def parse_schedule(text: str) -> dict[str, int | list[int]]:
    values = parse_ints(text)
    if len(values) < 8:
        raise ValueError("SYST:TDMA:SCHEDULE? returned a truncated snapshot")
    return {
        "enabled_mask": values[4],
        "quarantined_mask": values[5],
        "raw_fields": values,
    }


def raw_measurement_passed(snapshot: dict[str, int | list[int]], words: int,
                           baseline_epoch: int, observed_epochs: set[int]) -> bool:
    flags = int(snapshot["flags"])
    epoch = int(snapshot["epoch"])
    return (
        snapshot["armed"] == 0
        and snapshot["complete"] == 1
        and int(snapshot["sample_hz"]) > 0
        and int(snapshot["sample_period_ns"]) > 0
        and snapshot["produced_words"] == words
        and (int(snapshot["edge_mask"]) & 0x0f) == 0x0f
        and (flags & PHYSICAL_FLAGS) == PHYSICAL_FLAGS
        and (flags & DIAGNOSTIC_ONLY_FLAG) == 0
        and epoch != 0
        and epoch != baseline_epoch
        and epoch not in observed_epochs
        and 0 < int(snapshot["t1_clk_tx"]) <= int(snapshot["t2_clk_rx"])
        <= int(snapshot["t3_data_tx"]) <= int(snapshot["t4_data_rx"])
        and int(snapshot["clk_path_ns"]) > 0
        and int(snapshot["data_path_ns"]) > 0
        and int(snapshot["computed_residence_ns"]) > 0
        and int(snapshot["computed_raw_path_sum_ns"]) > 0
    )


def firmware_calibration_passed(snapshot: dict[str, int | list[int]]) -> bool:
    return (snapshot["result_valid"] == 1 and
            snapshot["reject_reason"] == 0 and
            snapshot["active_eligible"] == 1)


def _is_no_error(value: object) -> bool:
    return isinstance(value, str) and value.strip() == '0,"No error"'


def _write_summary(out_dir: Path, summary: dict[str, object]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    out_dir = Path(args.out_dir)
    records: list[dict[str, object]] = []
    quiet_windows: list[dict[str, object]] = []
    run_results: list[dict[str, object]] = []
    cleanup_failures: list[str] = []
    failure: str | None = None
    identity: dict[str, str] = {}
    final_error: str | None = None
    schedule_before: dict[str, int | list[int]] | None = None
    schedule_after: dict[str, int | list[int]] | None = None
    state_changed = False

    def transact(send: Callable[[str], str], text: str, *, run_index: int | None = None,
                 phase: str | None = None) -> str:
        record: dict[str, object] = {"command": text}
        if run_index is not None:
            record["run"] = run_index
        if phase is not None:
            record["phase"] = phase
        record["host_monotonic_start_s"] = time.perf_counter()
        try:
            response = send(text)
            record["response"] = response
            return response
        except Exception as exc:
            record["error"] = f"{type(exc).__name__}: {exc}"
            raise
        finally:
            record["host_monotonic_end_s"] = time.perf_counter()
            records.append(record)

    try:
        with open_transport(args) as send:
            try:
                idn_raw = transact(send, "*IDN?", phase="identity")
                build_raw = transact(send, "SYST:FW:BUILD?", phase="identity")
                parsed_idn = parse_idn_response(idn_raw)
                build = normalize_build_response(build_raw)
                identity = {
                    "idn": parsed_idn.idn,
                    "serial_number": parsed_idn.serial_number,
                    "build": build,
                }
                if parsed_idn.serial_number != args.serial_number:
                    raise RuntimeError(
                        f"serial mismatch: expected {args.serial_number}, "
                        f"observed {parsed_idn.serial_number}")
                if build != args.expected_build:
                    raise RuntimeError(
                        f"build mismatch: expected {args.expected_build}, observed {build}")

                schedule_before = parse_schedule(transact(
                    send, "SYST:TDMA:SCHEDULE?", phase="load_guard_before"))
                for stop in ("TRIG:STOP", "CONF:SEQ:LINK OFF", "SYST:TDMA:RING:STOP"):
                    state_changed = True
                    transact(send, stop, phase="offline_setup")

                observed_epochs: set[int] = set()
                for run_index in range(1, args.runs + 1):
                    baseline_response = transact(
                        send, "READ:CAL:LOOP?", run_index=run_index, phase="baseline")
                    baseline = parse_snapshot(baseline_response)
                    baseline_epoch = int(baseline["epoch"])
                    state_changed = True
                    transact(send, f"CAL:LOOP:START {args.words}",
                             run_index=run_index, phase="start")
                    snapshot: dict[str, int | list[int]] | None = None
                    if args.quiet_capture:
                        quiet_start = time.perf_counter()
                        records_before = len(records)
                        time.sleep(args.duration_s)
                        quiet_end = time.perf_counter()
                        quiet_windows.append({
                            "run": run_index,
                            "host_monotonic_start_s": quiet_start,
                            "host_monotonic_end_s": quiet_end,
                            "elapsed_s": quiet_end - quiet_start,
                            "records_before": records_before,
                            "records_after": len(records),
                        })
                        response = transact(send, "READ:CAL:LOOP?",
                                            run_index=run_index, phase="terminal")
                        snapshot = parse_snapshot(response)
                    else:
                        deadline = time.monotonic() + args.duration_s
                        while time.monotonic() < deadline:
                            response = transact(send, "READ:CAL:LOOP?",
                                                run_index=run_index, phase="poll")
                            snapshot = parse_snapshot(response)
                            if (snapshot["complete"] == 1 and
                                    snapshot["epoch"] != baseline_epoch):
                                break
                            time.sleep(args.poll_s)
                    if snapshot is None:
                        raise TimeoutError(
                            f"run {run_index} produced no terminal calibration snapshot")
                    if (snapshot["complete"] != 1 or
                            snapshot["epoch"] == baseline_epoch):
                        raise TimeoutError(
                            f"run {run_index} did not complete before the capture deadline")
                    raw_passed = raw_measurement_passed(
                        snapshot, args.words, baseline_epoch, observed_epochs)
                    formal_passed = firmware_calibration_passed(snapshot)
                    run_results.append({
                        "run": run_index,
                        "baseline_epoch": baseline_epoch,
                        "snapshot": snapshot,
                        "epoch_unique": snapshot["epoch"] not in observed_epochs,
                        "raw_measurement_passed": raw_passed,
                        "firmware_calibration_passed": formal_passed,
                        "active_eligible": snapshot["active_eligible"] == 1,
                    })
                    observed_epochs.add(int(snapshot["epoch"]))
                    transact(send, "CAL:LOOP:STOP", run_index=run_index, phase="stop")
                    state_changed = False
                    if run_index != args.runs:
                        time.sleep(args.inter_run_s)
            except Exception as exc:
                failure = f"{type(exc).__name__}: {exc}"
            finally:
                if state_changed:
                    try:
                        transact(send, "CAL:LOOP:STOP", phase="cleanup")
                    except Exception as exc:
                        cleanup_failures.append(f"CAL:LOOP:STOP: {type(exc).__name__}: {exc}")
                if identity.get("serial_number") == args.serial_number and identity.get("build") == args.expected_build:
                    try:
                        schedule_after = parse_schedule(transact(
                            send, "SYST:TDMA:SCHEDULE?", phase="load_guard_after"))
                    except Exception as exc:
                        cleanup_failures.append(
                            f"SYST:TDMA:SCHEDULE?: {type(exc).__name__}: {exc}")
                    try:
                        final_error = transact(send, "SYST:ERR?", phase="final_error")
                    except Exception as exc:
                        cleanup_failures.append(f"SYST:ERR?: {type(exc).__name__}: {exc}")
    except Exception as exc:
        failure = failure or f"{type(exc).__name__}: {exc}"

    load_guard_unchanged = (
        schedule_before is not None and schedule_after is not None and
        schedule_before["enabled_mask"] == schedule_after["enabled_mask"] and
        schedule_before["quarantined_mask"] == schedule_after["quarantined_mask"])
    raw_passed = (len(run_results) == args.runs and
                  all(row["raw_measurement_passed"] is True for row in run_results))
    formal_passed = (len(run_results) == args.runs and
                     all(row["firmware_calibration_passed"] is True
                         for row in run_results))
    active_eligible = (len(run_results) == args.runs and
                       all(row["active_eligible"] is True for row in run_results))
    passed = (failure is None and not cleanup_failures and raw_passed and
              load_guard_unchanged and _is_no_error(final_error))
    summary: dict[str, object] = {
        "schema": SCHEMA,
        "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "scope": "single_board_physical_rj45_reference_loopback",
        "transport": {
            "kind": "USBTMC" if args.visa_resource else "CDC",
            "endpoint": args.visa_resource or args.port,
        },
        "identity": identity,
        "settings": {
            "serial_number": args.serial_number,
            "expected_build": args.expected_build,
            "words": args.words,
            "runs": args.runs,
            "duration_s": args.duration_s,
            "quiet_capture": args.quiet_capture,
        },
        "runs_requested": args.runs,
        "runs_completed": len(run_results),
        "runs_raw_passed": sum(
            row["raw_measurement_passed"] is True for row in run_results),
        "run_results": run_results,
        "quiet_windows": quiet_windows,
        "raw_measurement_passed": raw_passed,
        "firmware_calibration_passed": formal_passed,
        "active_eligible": active_eligible,
        "formal_calibration_claimed": False,
        "load_guard": {
            "before": schedule_before,
            "after": schedule_after,
            "unchanged": load_guard_unchanged,
        },
        "final_error": final_error,
        "failure": failure,
        "cleanup_failures": cleanup_failures,
        "passed": passed,
        "limitations": list(LIMITATIONS),
        "evidence_boundary": (
            "Raw single-board physical RJ45 reference-loopback timestamps only; "
            "not formal calibration, VDC admission, synchronization accuracy, "
            "multi-board, RF, or waveform evidence."),
        "records": records,
    }
    _write_summary(out_dir, summary)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if passed else 1


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="USB CDC port, for example COM8")
    parser.add_argument("--visa-resource", help="USBTMC VISA resource")
    parser.add_argument("--serial-number", required=True)
    parser.add_argument("--expected-build", required=True)
    parser.add_argument("--words", type=int, default=128)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--duration-s", type=float, default=5.0)
    parser.add_argument("--poll-s", type=float, default=0.1)
    parser.add_argument("--inter-run-s", type=float, default=0.05)
    parser.add_argument("--quiet-capture", action="store_true")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout-s", type=float, default=2.0)
    parser.add_argument("--settle-s", type=float, default=1.0)
    parser.add_argument("--out-dir", default="out/pytest/build-calibration-loopback")
    args = parser.parse_args(argv)
    if bool(args.port) == bool(args.visa_resource):
        parser.error("provide exactly one CDC port or --visa-resource")
    time_values = (args.duration_s, args.poll_s, args.inter_run_s,
                   args.timeout_s, args.settle_s)
    if (args.words <= 0 or args.runs <= 0 or args.baud <= 0 or
            not all(math.isfinite(value) for value in time_values) or
            args.duration_s <= 0 or
            args.poll_s <= 0 or args.inter_run_s < 0 or args.timeout_s <= 0 or
            args.settle_s < 0):
        parser.error("counts/timeouts must be positive; settle/inter-run may be zero")
    return args


def main(argv: list[str] | None = None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main())
