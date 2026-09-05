#!/usr/bin/env python3
"""Export persisted SYNC_IO analyzer segments while the TDMA ring is stopped.

The firmware intentionally rejects StorageAO maintenance queries during product
RUN.  This host tool preserves that owner boundary: it can issue an explicit
bounded ring STOP, verifies the stopped snapshot, paginates ``/traces/run``,
downloads a bounded number of immutable ``analyzer_*.bin`` files, and invokes
the offline decoder/batch index.  It never restarts or reconfigures the ring.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[2]
for path in (ROOT, ROOT / "tools"):
    if str(path) not in sys.path:
        sys.path.insert(0, str(path))

from tools.analyzer_trace_batch_index.analyzer_trace_batch_index import (  # noqa: E402
    build_index,
    write_csv,
)
from tools.analyzer_trace_decode.analyzer_trace_decode import decode  # noqa: E402
from tools.scpi_common.scpi_serial import (  # noqa: E402
    open_serial_port,
    read_scpi_response,
)


TRACE_DIRECTORY = "/traces/run"
ANALYZER_NAME = re.compile(r"^analyzer_(\d+)\.bin$")
RUNTIME_FIELD_COUNT = 40
RUNTIME_RING_ENABLED = 0
RUNTIME_UP_RUNNING = 4
RUNTIME_DOWN_RUNNING = 5
RUNTIME_ADAPTER_STARTED = 8
RUNTIME_CONFIG_SEQ = 38
RUNTIME_APPLIED_CONFIG_SEQ = 39


@dataclass(frozen=True)
class Board:
    name: str
    port: str


Query = Callable[[str], str]


def parse_board(value: str) -> Board:
    if "=" not in value:
        raise ValueError("board must be NAME=PORT")
    name, port = (part.strip() for part in value.split("=", 1))
    if not name or not port:
        raise ValueError("board name and port must be non-empty")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,32}", name):
        raise ValueError("board name must contain only letters, digits, '_' or '-'")
    return Board(name.upper(), port.upper())


def _csv_fields(response: str) -> list[str]:
    return [item.strip().strip('"')
            for item in next(csv.reader([response]), [])]


def parse_runtime_status(response: str) -> dict[str, int]:
    fields = _csv_fields(response)
    if len(fields) != RUNTIME_FIELD_COUNT:
        raise ValueError(
            f"TDMA runtime field count {len(fields)}, expected {RUNTIME_FIELD_COUNT}")
    try:
        values = [int(value, 0) for value in fields]
    except ValueError as exc:
        raise ValueError(f"invalid TDMA runtime snapshot: {response!r}") from exc
    return {
        "ring_enabled": values[RUNTIME_RING_ENABLED],
        "ring_up_running": values[RUNTIME_UP_RUNNING],
        "ring_down_running": values[RUNTIME_DOWN_RUNNING],
        "ring_adapter_started": values[RUNTIME_ADAPTER_STARTED],
        "ring_config_seq": values[RUNTIME_CONFIG_SEQ],
        "ring_applied_config_seq": values[RUNTIME_APPLIED_CONFIG_SEQ],
    }


def runtime_stopped(status: dict[str, int]) -> bool:
    return (
        status["ring_enabled"] == 0
        and status["ring_up_running"] == 0
        and status["ring_down_running"] == 0
        and status["ring_adapter_started"] == 0
        and status["ring_config_seq"] == status["ring_applied_config_seq"]
    )


def parse_catalog_page(response: str, path: str, offset: int) -> dict[str, Any]:
    fields = _csv_fields(response)
    if len(fields) != 8 or fields[0] != "OK":
        raise ValueError(f"catalog page rejected: {response!r}")
    if fields[1] != path or int(fields[2], 0) != offset:
        raise ValueError(
            f"catalog page identity mismatch: path={fields[1]!r}, offset={fields[2]!r}")
    truncated = int(fields[6], 0) != 0
    entries: list[dict[str, Any]] = []
    for item in fields[7].split(";"):
        if not item or item == "EMPTY":
            continue
        parts = item.split(",")
        if len(parts) != 3 and truncated:
            break
        if len(parts) != 3:
            raise ValueError(f"malformed catalog entry: {item!r}")
        entries.append({
            "name": parts[0],
            "size": int(parts[1], 0),
            "kind": parts[2],
        })
    returned = int(fields[3], 0)
    if not truncated and returned != len(entries):
        raise ValueError(
            f"catalog returned_count {returned} != parsed entries {len(entries)}")
    complete = int(fields[5], 0) != 0
    next_offset = int(fields[4], 0)
    if not complete and next_offset <= offset:
        raise ValueError("catalog pagination made no progress")
    return {
        "path": fields[1],
        "offset": offset,
        "returned": returned,
        "next_offset": next_offset,
        "complete": complete,
        "truncated": truncated,
        "entries": entries,
        "raw": response,
    }


def discover_analyzer_segments(query: Query, *, page_limit: int = 16,
                               max_pages: int = 64) -> tuple[list[dict[str, Any]],
                                                              list[dict[str, Any]]]:
    if page_limit <= 0 or page_limit > 16:
        raise ValueError("page_limit must be in [1, 16]")
    if max_pages <= 0 or max_pages > 256:
        raise ValueError("max_pages must be in [1, 256]")
    offset = 0
    current_limit = page_limit
    pages: list[dict[str, Any]] = []
    segments: list[dict[str, Any]] = []
    for _ in range(max_pages):
        command = f'MMEM:CAT:PAGE? "{TRACE_DIRECTORY}",{offset},{current_limit}'
        raw_attempts: list[str] = []
        page: dict[str, Any] | None = None
        last_error: ValueError | None = None
        for _attempt in range(3):
            response = query(command)
            raw_attempts.append(response)
            try:
                page = parse_catalog_page(response, TRACE_DIRECTORY, offset)
                break
            except ValueError as exc:
                last_error = exc
                time.sleep(0.05)
        if page is None:
            raise ValueError(
                f"catalog page failed after {len(raw_attempts)} attempts: "
                f"{last_error}; raw={raw_attempts!r}")
        page["raw_attempts"] = raw_attempts
        pages.append(page)
        if page["truncated"]:
            if current_limit == 1:
                raise ValueError(
                    f"catalog page at offset {offset} was truncated at limit 1")
            page["retry_with_page_limit"] = max(1, current_limit // 2)
            current_limit = int(page["retry_with_page_limit"])
            continue
        for entry in page["entries"]:
            match = ANALYZER_NAME.fullmatch(str(entry["name"]))
            if match and entry["kind"] == "FILE":
                segments.append({
                    **entry,
                    "session_from_name": int(match.group(1), 10),
                    "remote_path": f'{TRACE_DIRECTORY}/{entry["name"]}',
                })
        if page["complete"]:
            break
        offset = int(page["next_offset"])
    else:
        raise ValueError(f"catalog exceeded max_pages={max_pages}")
    # FAT directory order is the only available creation-order evidence.
    # ``session_from_name`` is board uptime and restarts from a small value
    # after reboot, so numeric filename sorting would select an older boot.
    return segments, pages


def parse_storage_read(response: str, expected_path: str,
                       expected_offset: int) -> dict[str, Any]:
    fields = _csv_fields(response)
    if len(fields) != 10 or fields[0] != "OK":
        raise ValueError(f"storage read rejected: {response!r}")
    offset = int(fields[2], 0)
    requested = int(fields[3], 0)
    returned = int(fields[4], 0)
    if offset != expected_offset:
        raise ValueError(f"storage read offset {offset} != {expected_offset}")
    payload = bytes.fromhex(fields[9])
    if len(payload) != returned or returned > requested:
        raise ValueError(
            f"storage read length mismatch: payload={len(payload)}, "
            f"returned={returned}, requested={requested}")
    if int(fields[8], 0) != 0:
        raise ValueError(f"storage read error {fields[8]}")
    return {
        "job_id": int(fields[1], 0),
        "offset": offset,
        "requested": requested,
        "returned": returned,
        "file_size": int(fields[5], 0),
        "eof": int(fields[6], 0) != 0,
        "path_hash": int(fields[7], 0),
        "payload": payload,
        "remote_path": expected_path,
        "raw": response,
    }


def download_file(query: Query, remote_path: str, *, chunk_size: int = 128,
                  expected_size: int | None = None) -> tuple[bytes, list[dict[str, Any]]]:
    if chunk_size <= 0 or chunk_size > 128:
        raise ValueError("chunk_size must be in [1, 128]")
    data = bytearray()
    pages: list[dict[str, Any]] = []
    file_size: int | None = None
    path_hash: int | None = None
    while file_size is None or len(data) < file_size:
        request = chunk_size if file_size is None else min(chunk_size, file_size - len(data))
        response = query(
            f'SYSTem:STORage:FILE:READ? "{remote_path}",{len(data)},{request}')
        page = parse_storage_read(response, remote_path, len(data))
        pages.append({key: value for key, value in page.items() if key != "payload"})
        if file_size is None:
            file_size = int(page["file_size"])
            path_hash = int(page["path_hash"])
            if expected_size is not None and file_size != expected_size:
                raise ValueError(
                    f"catalog size {expected_size} != file read size {file_size}")
        elif int(page["file_size"]) != file_size:
            raise ValueError("file size changed during download")
        if int(page["path_hash"]) != path_hash:
            raise ValueError("path hash changed during download")
        payload = page["payload"]
        if not isinstance(payload, bytes) or (not payload and not page["eof"]):
            raise ValueError("file download made no progress")
        data.extend(payload)
        if page["eof"]:
            break
    if file_size is None or len(data) != file_size:
        raise ValueError(f"incomplete file download {len(data)}/{file_size}")
    return bytes(data), pages


def _serial_query(board: Board, args: argparse.Namespace) -> Query:
    def execute(text: str) -> str:
        with open_serial_port(board.port, args.baud, args.timeout,
                              args.settle) as ser:
            ser.reset_input_buffer()
            ser.write((text + "\n").encode("ascii"))
            ser.flush()
            return read_scpi_response(
                ser, text, args.timeout, require_match=True)
    return execute


def _wait_stopped(query: Query, timeout_s: float) -> tuple[dict[str, int], list[str]]:
    deadline = time.monotonic() + timeout_s
    raw_snapshots: list[str] = []
    last: dict[str, int] = {}
    while time.monotonic() < deadline:
        raw = query("SYSTem:TDMA:RING:STATus?")
        raw_snapshots.append(raw)
        last = parse_runtime_status(raw)
        if runtime_stopped(last):
            return last, raw_snapshots
        time.sleep(0.05)
    raise TimeoutError(f"TDMA ring STOP timeout: last={last}")


def export_stopped_board(board: Board, args: argparse.Namespace, query: Query,
                         preflight: dict[str, Any]) -> dict[str, Any]:
    segments, catalog_pages = discover_analyzer_segments(
        query, page_limit=args.page_limit, max_pages=args.max_pages)
    selected = segments[-args.latest_per_board:]
    board_dir = args.out_dir / board.name.lower()
    board_dir.mkdir(parents=True, exist_ok=True)
    downloaded: list[dict[str, Any]] = []
    local_paths: list[Path] = []
    for segment in selected:
        raw, read_pages = download_file(
            query, str(segment["remote_path"]), chunk_size=args.chunk_size,
            expected_size=int(segment["size"]))
        local_path = board_dir / str(segment["name"])
        local_path.write_bytes(raw)
        decoded = decode(local_path)
        tick_hz = int(decoded["header"].get("metadata", {}).get(
            "hardware_tick_hz", 0))
        if tick_hz:
            decoded = decode(local_path, tick_hz=tick_hz)
        decoded_path = local_path.with_suffix(".json")
        decoded_path.write_text(
            json.dumps(decoded, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8", newline="\n")
        local_paths.append(local_path)
        downloaded.append({
            "remote": segment,
            "local_path": str(local_path),
            "decoded_path": str(decoded_path),
            "download_size": len(raw),
            "read_pages": read_pages,
            "checks": decoded["checks"],
            "record_count": decoded["header"]["record_count"],
            "dropped_records": decoded["header"]["dropped_records"],
            "discontinuity_count": decoded["discontinuity_count"],
            "computed_file_crc32": decoded["computed_file_crc32"],
        })
    if not local_paths:
        raise RuntimeError(f"{board.name}: no analyzer segments found")
    index = build_index(local_paths)
    index_path = board_dir / "index.json"
    index_path.write_text(
        json.dumps(index, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8", newline="\n")
    write_csv(board_dir / "index.csv", index)
    return {
        "board": board.name,
        "port": board.port,
        **preflight,
        "catalog_pages": catalog_pages,
        "segment_count_found": len(segments),
        "segment_count_selected": len(selected),
        "downloaded": downloaded,
        "index_path": str(index_path),
        "passed": all(all(row["checks"].values()) for row in downloaded),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", action="append", required=True,
                        metavar="NO1=COM5")
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--stop-ring", action="store_true",
                        help="explicitly stop a running TDMA ring before export")
    parser.add_argument("--latest-per-board", type=int, default=4)
    parser.add_argument("--page-limit", type=int, default=16)
    parser.add_argument("--max-pages", type=int, default=64)
    parser.add_argument("--chunk-size", type=int, default=128)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--stop-timeout", type=float, default=5.0)
    return parser.parse_args()


def run(args: argparse.Namespace) -> dict[str, Any]:
    boards = [parse_board(value) for value in args.board]
    if len({board.name for board in boards}) != len(boards):
        raise ValueError("duplicate board name")
    if len({board.port for board in boards}) != len(boards):
        raise ValueError("duplicate board port")
    if args.latest_per_board <= 0 or args.latest_per_board > 64:
        raise ValueError("latest_per_board must be in [1, 64]")
    if args.timeout <= 0 or args.settle < 0 or args.stop_timeout <= 0:
        raise ValueError("timeouts must be positive and settle must be non-negative")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    queries = {board.name: _serial_query(board, args) for board in boards}
    preflight: dict[str, dict[str, Any]] = {}
    for board in boards:
        query = queries[board.name]
        identity = query("*IDN?")
        if "DHRT100" not in identity:
            raise RuntimeError(
                f"{board.name}/{board.port}: unexpected identity {identity!r}")
        before_raw = query("SYSTem:TDMA:RING:STATus?")
        before = parse_runtime_status(before_raw)
        preflight[board.name] = {
            "identity": identity,
            "runtime_before_raw": before_raw,
            "runtime_before": before,
            "stop_response": None,
            "stop_snapshots": [],
            "runtime_stopped": {},
        }
    running = [board for board in boards
               if not runtime_stopped(preflight[board.name]["runtime_before"])]
    if running and not args.stop_ring:
        names = ", ".join(board.name for board in running)
        raise RuntimeError(
            f"TDMA ring is running on {names}; rerun with --stop-ring")
    # Preserve the ring as one state-machine phase: request STOP on every
    # running node before waiting or issuing any StorageAO maintenance query.
    for board in running:
        preflight[board.name]["stop_response"] = queries[board.name](
            "SYSTem:TDMA:RING:STOP")
    for board in boards:
        stopped, snapshots = _wait_stopped(
            queries[board.name], args.stop_timeout)
        preflight[board.name]["runtime_stopped"] = stopped
        preflight[board.name]["stop_snapshots"] = snapshots
    results = [
        export_stopped_board(
            board, args, queries[board.name], preflight[board.name])
        for board in boards
    ]
    summary = {
        "schema": "HAOFV_SYNC_IO_ANALYZER_STOPPED_EXPORT_V1",
        "trace_directory": TRACE_DIRECTORY,
        "stop_ring_requested": bool(args.stop_ring),
        "latest_per_board": args.latest_per_board,
        "boards": results,
        "ring_restart_performed": False,
        "owner_boundary": {
            "storage_queries_only_while_stopped": True,
            "core1_realtime_path_touched": False,
            "restart_delegated_to_tdma_gate": True,
        },
        "passed": all(bool(row["passed"]) for row in results),
    }
    (args.out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8", newline="\n")
    return summary


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
    except (OSError, RuntimeError, TimeoutError, ValueError) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
