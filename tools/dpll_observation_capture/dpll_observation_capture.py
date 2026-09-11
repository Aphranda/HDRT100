#!/usr/bin/env python3
"""Capture DPLL residuals to board SD, download, and render offline SVG.

This maintenance tool follows the same boundary as TRN-03 waveform capture:
the board records fixed-size samples in SRAM, StorageAO writes the frozen image
to SD, and the host downloads/decodes it after the run.  It never adds a TDMA
frame or polls during the capture window.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
# Support both ``python -m tools.dpll_observation_capture...`` and the
# documented direct ``python tools/...py`` invocation on Windows.
for path in (ROOT, ROOT / "tools", ROOT / "tools" / "calibration_ring_validate"):
    if str(path) not in sys.path:
        sys.path.insert(0, str(path))

from calibration_data_train import parse_storage_read  # noqa: E402
from tools.dpll_observation_decode.dpll_observation_decode import (  # noqa: E402
    decode,
)
from tools.dpll_residual_analyze.dpll_residual_analyze import (  # noqa: E402
    load_monitor_samples,
    write_reports,
)
from scpi_common.scpi_serial import (  # noqa: E402
    STORAGE_FILE_READ_MAX_BYTES,
    open_serial_port,
    read_scpi_response,
)


@dataclass(frozen=True)
class Board:
    name: str
    port: str


ROLE_STATUS_FIELDS = (
    "mode", "follow_master_slot_id", "requested_generation",
    "applied_generation", "pending", "follower_apply_count",
    "follower_no_command_count", "follower_wrong_source_count",
    "follower_stale_command_count", "follower_invalid_command_count",
    "follower_local_evidence_bypass_count", "last_follower_source_slot_id",
    "last_follower_control_generation", "last_follower_command_seq",
    "last_follower_quality", "last_follower_effective_vdc_time_lo",
    "last_follower_effective_vdc_time_hi",
)
FOLLOWER_MODE = 1
FOLLOWER_COUNTER_FIELDS = (
    "follower_apply_count", "follower_no_command_count",
    "follower_wrong_source_count", "follower_stale_command_count",
    "follower_invalid_command_count", "follower_local_evidence_bypass_count",
)
REFMEM_VDC_FOLLOWER_RX_FIELDS = (
    "active", "active_intent_seq", "next_window_seq",
    "last_processed_completed_seq", "submitted_count", "frame_ready_count",
    "accepted_count", "invalid_count", "timeout_count", "window_miss_count",
    "submit_reject_count", "last_result", "last_rx_result", "last_error",
    "last_source_slot", "last_command_seq",
)
REFMEM_VDC_FOLLOWER_RX_COUNTER_FIELDS = (
    "submitted_count", "frame_ready_count", "accepted_count", "invalid_count",
    "timeout_count", "window_miss_count", "submit_reject_count",
)


def parse_board(value: str) -> Board:
    if "=" not in value:
        raise ValueError("board must be NAME=PORT")
    name, port = (part.strip() for part in value.split("=", 1))
    name = name.upper()
    if name not in {f"NO{i}" for i in range(1, 9)} or not port:
        raise ValueError("board name must be NO1..NO8 and port must be non-empty")
    return Board(name, port)


def command(ser: Any, text: str, timeout: float) -> str:
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, text, timeout, require_match=True)


def query(board: Board, text: str, args: argparse.Namespace) -> str:
    with open_serial_port(board.port, args.baud, args.timeout, args.settle) as ser:
        identity = command(ser, "*IDN?", args.timeout)
        if "DHRT100" not in identity:
            raise RuntimeError(f"{board.name}/{board.port}: unexpected identity {identity!r}")
        return command(ser, text, args.timeout)


def parse_role_status(response: str) -> dict[str, int]:
    fields = [item.strip().strip('"')
              for item in next(csv.reader([response]), [])]
    if len(fields) != len(ROLE_STATUS_FIELDS):
        raise ValueError(f"invalid DPLL role status {response!r}")
    try:
        return {name: int(value, 0) for name, value in
                zip(ROLE_STATUS_FIELDS, fields)}
    except ValueError as exc:
        raise ValueError(f"invalid DPLL role status {response!r}") from exc


def role_status_delta(before: dict[str, int], after: dict[str, int]) -> dict[str, int]:
    """Return wrap-safe follower counter deltas for one capture window."""
    return {
        field: (after[field] - before[field]) & 0xFFFFFFFF
        for field in FOLLOWER_COUNTER_FIELDS
    }


def parse_refmem_vdc_follower_rx(response: str) -> dict[str, int]:
    fields = [item.strip().strip('"')
              for item in next(csv.reader([response]), [])]
    if len(fields) != len(REFMEM_VDC_FOLLOWER_RX_FIELDS):
        raise ValueError(f"invalid RefMem VDC follower RX status {response!r}")
    try:
        return {name: int(value, 0) for name, value in
                zip(REFMEM_VDC_FOLLOWER_RX_FIELDS, fields)}
    except ValueError as exc:
        raise ValueError(
            f"invalid RefMem VDC follower RX status {response!r}") from exc


def refmem_vdc_follower_rx_delta(
        before: dict[str, int], after: dict[str, int]) -> dict[str, int]:
    return {
        field: (after[field] - before[field]) & 0xFFFFFFFF
        for field in REFMEM_VDC_FOLLOWER_RX_COUNTER_FIELDS
    }


def summarize_follower_transport(
        role_after: dict[str, int], rx_before: dict[str, int],
        rx_after: dict[str, int], rx_delta: dict[str, int]) -> dict[str, Any]:
    """Report RefMem receive progress without claiming Domain application."""
    if role_after["mode"] != FOLLOWER_MODE:
        return {"mode": "not_follower", "transport_verified": False}
    expected_source = role_after["follow_master_slot_id"]
    errors = sum(rx_delta[field] for field in (
        "invalid_count", "timeout_count", "window_miss_count",
        "submit_reject_count",
    ))
    accepted = rx_delta["accepted_count"]
    source_matches = accepted > 0 and rx_after["last_source_slot"] == expected_source
    sequence_advanced = (
        accepted > 0 and
        ((rx_after["last_command_seq"] - rx_before["last_command_seq"]) &
         0xFFFFFFFF) != 0
    )
    transport_verified = (
        rx_after["active"] != 0 and
        rx_delta["submitted_count"] > 0 and
        rx_delta["frame_ready_count"] > 0 and accepted > 0 and source_matches and
        sequence_advanced and errors == 0
    )
    return {
        "mode": "follower_refmem_vdc_rx",
        "expected_source_slot": expected_source,
        "rx_before": rx_before,
        "rx_after": rx_after,
        "rx_counter_delta": rx_delta,
        "rx_active": rx_after["active"] != 0,
        "accepted_frame_delta": accepted,
        "source_matches_configured_master": source_matches,
        "command_sequence_advanced": sequence_advanced,
        "error_counter_delta": errors,
        "transport_verified": transport_verified,
        "reason": ("validated_refmem_receive" if transport_verified else
                   "refmem_receive_not_verified"),
    }


def summarize_follower_observation(
        samples: list[dict[str, Any]], role_delta: dict[str, int],
        follower: bool) -> dict[str, Any]:
    """Summarize the follower's local TDMA evidence and applied commands.

    FOLLOWER runs the same timestamp/window/path-delay observation as MASTER,
    but never uses that evidence to run local PI or promote local lock.  Keep
    the older command-only summary as a compatibility path for captures made
    before local evidence records were added.
    """
    if not follower:
        return {"mode": "master_local_evidence"}
    evidence = [
        sample["follower_observation"] for sample in samples
        if sample.get("capture_kind") == "follower_local_evidence" and
        isinstance(sample.get("follower_observation"), dict)
    ]
    if evidence:
        sample_sequences = [int(item.get("sample_seq", 0)) for item in evidence]
        source_slots = sorted({int(item.get("source_slot_id", 0))
                               for item in evidence})
        accepted = [item for item in evidence
                    if int(item.get("gate_reject_code", 0)) == 0]
        sequence_strict = all(
            current > previous for previous, current in
            zip(sample_sequences, sample_sequences[1:]))
        record_count = len(evidence)
        bypass_delta = role_delta["follower_local_evidence_bypass_count"]
        return {
            "mode": "follower_local_evidence",
            "local_evidence_record_count": record_count,
            "accepted_observation_sample_count": len(accepted),
            "follower_local_evidence_bypass_delta": bypass_delta,
            "capture_records_covered_by_bypass_counter":
                bypass_delta >= record_count,
            "source_slots": source_slots,
            "first_sample_seq": sample_sequences[0] if sample_sequences else 0,
            "last_sample_seq": sample_sequences[-1] if sample_sequences else 0,
            "sample_sequence_strict": sequence_strict,
            "local_pi_lock_evidence": False,
            "multi_point_ready": (
                record_count >= 2 and bypass_delta >= record_count and
                sequence_strict
            ),
            "reason": "captured_follower_local_evidence",
        }
    applied = [
        sample["follower_command"] for sample in samples
        if sample.get("capture_kind") == "follower_applied_command" and
        isinstance(sample.get("follower_command"), dict) and
        sample["follower_command"].get("applied") is True
    ]
    command_sequences = [int(command["command_seq"]) for command in applied]
    effective_times = [int(command["effective_vdc_time_ns"])
                       for command in applied]
    source_slots = sorted({int(command["source_slot_id"]) for command in applied})
    generations = sorted({int(command["control_generation"])
                          for command in applied})
    sequence_strict = all(
        current > previous for previous, current in
        zip(command_sequences, command_sequences[1:]))
    time_strict = all(
        current > previous for previous, current in
        zip(effective_times, effective_times[1:]))
    record_count = len(applied)
    apply_delta = role_delta["follower_apply_count"]
    return {
        "mode": "follower_validated_command_apply",
        "applied_command_record_count": record_count,
        "follower_apply_count_delta": apply_delta,
        "capture_records_covered_by_apply_counter": apply_delta >= record_count,
        "source_slots": source_slots,
        "control_generations": generations,
        "first_command_seq": command_sequences[0] if command_sequences else 0,
        "last_command_seq": command_sequences[-1] if command_sequences else 0,
        "first_effective_vdc_time_ns": effective_times[0] if effective_times else 0,
        "last_effective_vdc_time_ns": effective_times[-1] if effective_times else 0,
        "command_sequence_strict": sequence_strict,
        "effective_time_strict": time_strict,
        "local_evidence_bypass_delta": role_delta[
            "follower_local_evidence_bypass_count"],
        "multi_point_ready": (
            record_count >= 2 and apply_delta >= record_count and
            sequence_strict and time_strict and
            role_delta["follower_local_evidence_bypass_count"] == 0
        ),
        "reason": (
            "captured_validated_follower_commands" if record_count else
            "no_follower_command_apply_in_capture_window"
        ),
    }


def parse_save(response: str) -> tuple[int, str, int]:
    fields = [item.strip().strip('"') for item in next(csv.reader([response]), [])]
    if len(fields) != 4 or fields[0].upper() != "QUEUED":
        raise ValueError(f"unexpected TRACE:SAVE response: {response!r}")
    return int(fields[1], 0), fields[2], int(fields[3], 0)


def wait_job(board: Board, job_id: int, args: argparse.Namespace) -> dict[str, Any]:
    deadline = time.monotonic() + args.timeout * 20.0
    last = ""
    while time.monotonic() < deadline:
        last = query(board, "SYSTem:STORage:JOB?", args)
        fields = [item.strip().strip('"') for item in next(csv.reader([last]), [])]
        if len(fields) >= 8 and int(fields[1], 0) == job_id:
            state = fields[0].upper()
            if state == "DONE":
                return {"state": state, "size": int(fields[4], 0), "path": fields[3]}
            if state == "FAILED":
                raise RuntimeError(f"{board.name}: StorageAO job failed: {last!r}")
        time.sleep(0.05)
    raise TimeoutError(f"{board.name}: StorageAO job timeout: {last!r}")


def download(board: Board, path: str, args: argparse.Namespace) -> bytes:
    data = bytearray()
    file_size: int | None = None
    while file_size is None or len(data) < file_size:
        requested = (STORAGE_FILE_READ_MAX_BYTES if file_size is None else
                     min(STORAGE_FILE_READ_MAX_BYTES, file_size - len(data)))
        response = query(
            board,
            f'SYSTem:STORage:FILE:READ? "{path}",{len(data)},{requested}',
            args,
        )
        page = parse_storage_read(response, len(data))
        if file_size is None:
            file_size = int(page["file_size"])
        elif int(page["file_size"]) != file_size:
            raise RuntimeError("capture file changed during download")
        payload = page["payload"]
        if not isinstance(payload, bytes) or (not payload and not page["eof"]):
            raise RuntimeError("capture download made no progress")
        data.extend(payload)
        if page["eof"]:
            break
    if file_size is None or len(data) != file_size:
        raise RuntimeError(f"incomplete capture download {len(data)}/{file_size}")
    return bytes(data)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", action="append", required=True, metavar="NO5=COM25")
    parser.add_argument("--duration-s", type=float, default=30.0)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument(
        "--skip-capture", action="store_true",
        help="observe trace status without arming, saving, or downloading SD data")
    return parser.parse_args()


def run(args: argparse.Namespace) -> dict[str, Any]:
    boards = [parse_board(value) for value in args.board]
    if len({board.name for board in boards}) != len(boards):
        raise ValueError("duplicate board name")
    if args.duration_s <= 0:
        raise ValueError("duration must be positive")
    args.out_dir.mkdir(parents=True, exist_ok=True)

    # Keep the raw SCPI snapshots as evidence.  A zero-sample recorder is a
    # diagnostic result, not a reason to discard the role/transport state
    # which explains it.
    role_before_raw = {
        board.name: query(board, "SYSTem:SYNC:VDC:DPLL:ROLE:STATus?", args)
        for board in boards
    }
    role_before = {
        name: parse_role_status(response)
        for name, response in role_before_raw.items()
    }
    refmem_rx_before_raw = {
        board.name: query(board, "SYSTem:REFMEM:SYNC:TDMA:VDC:RX?", args)
        for board in boards
    }
    refmem_rx_before = {
        name: parse_refmem_vdc_follower_rx(response)
        for name, response in refmem_rx_before_raw.items()
    }

    if args.skip_capture:
        # Keep the internal status probe available when waveform collection is
        # disabled by the acceptance policy.  This path deliberately emits no
        # TRACE:ARM/STOP/SAVE or file-read commands.
        board_results: list[dict[str, Any]] = []
        for board in boards:
            status = query(board, "SYSTem:SYNC:VDC:DPLL:TRACe:STATus?", args)
            observed_at_unix_ns = time.time_ns()
            observed_at_utc = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            fields = [item.strip().strip('"')
                      for item in next(csv.reader([status]), [])]
            if len(fields) != 8:
                raise ValueError(f"{board.name}: invalid trace status {status!r}")
            board_results.append({
                "board": board.name,
                "port": board.port,
                "status": fields,
                "trace_status_raw": status,
                "observation_timestamp_unix_ns": observed_at_unix_ns,
                "observation_timestamp_utc": observed_at_utc,
                "follower_status_before": role_before[board.name],
                "follower_status_before_raw": role_before_raw[board.name],
                "follower_status_after": role_before[board.name],
                "follower_status_after_raw": role_before_raw[board.name],
                "follower_status_delta": {
                    field: 0 for field in FOLLOWER_COUNTER_FIELDS
                },
                "refmem_vdc_follower_rx_before": refmem_rx_before[board.name],
                "refmem_vdc_follower_rx_before_raw": refmem_rx_before_raw[board.name],
                "refmem_vdc_follower_rx_after": refmem_rx_before[board.name],
                "refmem_vdc_follower_rx_after_raw": refmem_rx_before_raw[board.name],
                "refmem_vdc_follower_rx_delta": {
                    field: 0 for field in REFMEM_VDC_FOLLOWER_RX_COUNTER_FIELDS
                },
                "follower_transport": summarize_follower_transport(
                    role_before[board.name], refmem_rx_before[board.name],
                    refmem_rx_before[board.name], {
                        field: 0 for field in REFMEM_VDC_FOLLOWER_RX_COUNTER_FIELDS
                    }),
                "follower_observation": summarize_follower_observation(
                    [], {field: 0 for field in FOLLOWER_COUNTER_FIELDS},
                    role_before[board.name]["mode"] == FOLLOWER_MODE),
            })
        result = {
            "schema": "HAOFV_DPLL_OBSERVATION_CAPTURE_RUN_V1",
            "duration_s": args.duration_s,
            "boards": board_results,
            "analysis": {},
            "combined_convergence_svg": None,
            "passed": True,
            "failures": [],
            "missing_master_boards": [],
            "capture_skipped": True,
            "capture_skip_reason": "disabled_by_acceptance_policy",
            "realtime_path_untouched": True,
        }
        (args.out_dir / "summary.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        return result

    armed: list[Board] = []
    try:
        for board in boards:
            # Register the board before sending ARM so a partially parsed
            # composite response (or a transport timeout after the firmware
            # accepted ARM) is still followed by STOP in the cleanup path.
            armed.append(board)
            response = query(board, "SYSTem:SYNC:VDC:DPLL:TRACe:ARM", args)
            if not response.lstrip().lstrip('"').upper().startswith("OK"):
                raise RuntimeError(f"{board.name}: trace arm rejected: {response!r}")
        time.sleep(args.duration_s)
    finally:
        for board in armed:
            try:
                query(board, "SYSTem:SYNC:VDC:DPLL:TRACe:STOP", args)
            except (OSError, RuntimeError, TimeoutError):
                # Preserve the original capture error while making a best
                # effort to release every board's fixed SRAM recorder.
                pass

    decoded_inputs: list[Path] = []
    board_results: list[dict[str, Any]] = []
    failures: list[dict[str, Any]] = []
    missing_master_boards: list[str] = []
    for board in boards:
        status = query(board, "SYSTem:SYNC:VDC:DPLL:TRACe:STATus?", args)
        observed_at_unix_ns = time.time_ns()
        observed_at_utc = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        status_fields = [item.strip().strip('"') for item in next(csv.reader([status]), [])]
        if len(status_fields) != 8:
            raise ValueError(f"{board.name}: invalid trace status {status!r}")
        role_after_raw = query(board, "SYSTem:SYNC:VDC:DPLL:ROLE:STATus?", args)
        role_after = parse_role_status(role_after_raw)
        role_window = role_status_delta(role_before[board.name], role_after)
        refmem_rx_after_raw = query(
            board, "SYSTem:REFMEM:SYNC:TDMA:VDC:RX?", args)
        refmem_rx_after = parse_refmem_vdc_follower_rx(refmem_rx_after_raw)
        refmem_rx_window = refmem_vdc_follower_rx_delta(
            refmem_rx_before[board.name], refmem_rx_after)
        follower_transport = summarize_follower_transport(
            role_after, refmem_rx_before[board.name], refmem_rx_after,
            refmem_rx_window)
        sample_count = int(status_fields[2], 0)
        follower = role_after["mode"] == FOLLOWER_MODE
        if sample_count == 0:
            trace_absent_reason = (
                "follower_no_local_observation_in_capture_window" if follower else
                "master_dpll_update_not_published_in_capture_window"
            )
            evidence = {
                "board": board.name,
                "port": board.port,
                "status": status_fields,
                "trace_status_raw": status,
                "sample_count": 0,
                "trace_absent_reason": trace_absent_reason,
                "observation_timestamp_unix_ns": observed_at_unix_ns,
                "observation_timestamp_utc": observed_at_utc,
                "follower_status_before": role_before[board.name],
                "follower_status_before_raw": role_before_raw[board.name],
                "follower_status_after": role_after,
                "follower_status_after_raw": role_after_raw,
                "follower_status_delta": role_window,
                "refmem_vdc_follower_rx_before": refmem_rx_before[board.name],
                "refmem_vdc_follower_rx_before_raw": refmem_rx_before_raw[board.name],
                "refmem_vdc_follower_rx_after": refmem_rx_after,
                "refmem_vdc_follower_rx_after_raw": refmem_rx_after_raw,
                "refmem_vdc_follower_rx_delta": refmem_rx_window,
                "follower_transport": follower_transport,
                "follower_observation": summarize_follower_observation(
                    [], role_window, follower),
            }
            board_results.append(evidence)
            if not follower:
                missing_master_boards.append(board.name)
                failures.append({
                    "board": board.name,
                    "reason": trace_absent_reason,
                    "sample_count": 0,
                    "evidence": evidence,
                })
            continue
        save_response = query(board, "SYSTem:SYNC:VDC:DPLL:TRACe:SAVE", args)
        job_id, sd_path, sample_count = parse_save(save_response)
        job = wait_job(board, job_id, args)
        raw = download(board, sd_path, args)
        raw_path = args.out_dir / f"{board.name.lower()}_dpll_capture.bin"
        raw_path.write_bytes(raw)
        samples_path = args.out_dir / f"{board.name.lower()}_samples.json"
        decoded = decode(raw_path, board.name)
        samples_path.write_text(
            json.dumps(decoded["samples"], ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        decoded_inputs.append(samples_path)
        decoded_samples = decoded["samples"][board.name]
        board_results.append({
            "board": board.name,
            "port": board.port,
            "status": status_fields,
            "trace_status_raw": status,
            "observation_timestamp_unix_ns": observed_at_unix_ns,
            "observation_timestamp_utc": observed_at_utc,
            "follower_status_before": role_before[board.name],
            "follower_status_before_raw": role_before_raw[board.name],
            "follower_status_after": role_after,
            "follower_status_after_raw": role_after_raw,
            "follower_status_delta": role_window,
            "refmem_vdc_follower_rx_before": refmem_rx_before[board.name],
            "refmem_vdc_follower_rx_before_raw": refmem_rx_before_raw[board.name],
            "refmem_vdc_follower_rx_after": refmem_rx_after,
            "refmem_vdc_follower_rx_after_raw": refmem_rx_after_raw,
            "refmem_vdc_follower_rx_delta": refmem_rx_window,
            "follower_transport": follower_transport,
            "sd_path": sd_path,
            "job": job,
            "sample_count": sample_count,
            "download_size": len(raw),
            "raw_path": str(raw_path),
            "samples_path": str(samples_path),
            "follower_observation": summarize_follower_observation(
                decoded_samples, role_window, follower),
        })

    series = load_monitor_samples(decoded_inputs)
    analysis_dir = args.out_dir / "analysis"
    analysis = (write_reports(
        series,
        analysis_dir,
        input_paths=decoded_inputs,
        rolling_window=5,
        lock_threshold_ns=10000,
        mad_multiplier=6.0,
    ) if decoded_inputs else {
        "reason": "no_dco_updates_in_capture_window",
        "combined_svg": None,
    })
    result = {
        "schema": "HAOFV_DPLL_OBSERVATION_CAPTURE_RUN_V1",
        "duration_s": args.duration_s,
        "boards": board_results,
        "analysis": analysis,
        "combined_convergence_svg": analysis.get("combined_svg"),
        "passed": not failures,
        "failures": failures,
        "missing_master_boards": missing_master_boards,
        "realtime_path_untouched": True,
    }
    (args.out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return result


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        if result.get("passed") is False:
            return 2
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        print(f"FAILED: {exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
