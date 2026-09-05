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
    return parser.parse_args()


def run(args: argparse.Namespace) -> dict[str, Any]:
    boards = [parse_board(value) for value in args.board]
    if len({board.name for board in boards}) != len(boards):
        raise ValueError("duplicate board name")
    if args.duration_s <= 0:
        raise ValueError("duration must be positive")
    args.out_dir.mkdir(parents=True, exist_ok=True)

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
    for board in boards:
        status = query(board, "SYSTem:SYNC:VDC:DPLL:TRACe:STATus?", args)
        status_fields = [item.strip().strip('"') for item in next(csv.reader([status]), [])]
        if len(status_fields) != 8:
            raise ValueError(f"{board.name}: invalid trace status {status!r}")
        if int(status_fields[2], 0) == 0:
            raise RuntimeError(
                f"{board.name}: trace contains no samples; DPLL update service "
                "is not active during the capture window"
            )
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
        board_results.append({
            "board": board.name,
            "port": board.port,
            "status": status_fields,
            "sd_path": sd_path,
            "job": job,
            "sample_count": sample_count,
            "download_size": len(raw),
            "raw_path": str(raw_path),
            "samples_path": str(samples_path),
        })

    series = load_monitor_samples(decoded_inputs)
    analysis_dir = args.out_dir / "analysis"
    analysis = write_reports(
        series,
        analysis_dir,
        input_paths=decoded_inputs,
        rolling_window=5,
        lock_threshold_ns=10000,
        mad_multiplier=6.0,
    )
    result = {
        "schema": "HAOFV_DPLL_OBSERVATION_CAPTURE_RUN_V1",
        "duration_s": args.duration_s,
        "boards": board_results,
        "analysis": analysis,
        "realtime_path_untouched": True,
    }
    (args.out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return result


def main() -> int:
    args = parse_args()
    try:
        print(json.dumps(run(args), ensure_ascii=False, indent=2))
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        print(f"FAILED: {exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
