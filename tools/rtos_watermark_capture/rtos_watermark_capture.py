#!/usr/bin/env python3
"""Capture and summarize FreeRTOS heap/task watermarks over USB CDC.

The tool only issues the read-only ``SYST:RTOS:STATUS?`` query.  A capture is
intended to be run with the 128 KiB startup baseline before any stack or heap
reduction is considered.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from contextlib import ExitStack
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import (  # noqa: E402
    open_serial_port,
    read_scpi_response,
)


STATUS_COMMAND = "SYST:RTOS:STATus?"
SCHEMA = "HAOFV_RTOS_WATERMARK_V1"
DEFAULT_MIN_HEAP_FREE_BYTES = 24 * 1024
DEFAULT_MIN_STACK_FREE_PERCENT = 25.0
DEFAULT_MIN_STACK_FREE_BYTES = 512
DEFAULT_OTA_FREE_PERCENT = 60.0


def parse_board(value: str) -> tuple[str, str]:
    name, separator, port = value.partition("=")
    name = name.strip().upper()
    port = port.strip()
    if not separator or not name or not port:
        raise argparse.ArgumentTypeError("board must be NAME=PORT, for example NO1=COM3")
    return name, port


def parse_status_response(response: str) -> dict[str, Any]:
    """Parse the CSV tuple emitted by ``scpi_cmd_rtos_status_q``."""
    if response == "<timeout>":
        raise ValueError("SCPI response timeout")
    try:
        fields = next(csv.reader([response], skipinitialspace=True))
    except csv.Error as exc:
        raise ValueError(f"invalid RTOS CSV: {exc}") from exc
    if len(fields) < 3:
        raise ValueError(f"RTOS response has only {len(fields)} fields")

    def integer(raw: str, label: str) -> int:
        try:
            value = int(raw.strip())
        except ValueError as exc:
            raise ValueError(f"{label} is not an integer: {raw!r}") from exc
        if value < 0:
            raise ValueError(f"{label} must be non-negative")
        return value

    heap_free = integer(fields[0], "heap_free_bytes")
    heap_min_free = integer(fields[1], "heap_min_free_bytes")
    task_count = integer(fields[2], "task_count")
    expected = 3 + task_count * 5
    if len(fields) != expected:
        raise ValueError(
            f"RTOS response task count={task_count} expects {expected} fields, "
            f"got {len(fields)}"
        )

    tasks: list[dict[str, Any]] = []
    offset = 3
    for index in range(task_count):
        name = fields[offset].strip()
        if len(name) >= 2 and name[0] == '"' and name[-1] == '"':
            name = name[1:-1]
        if not name:
            raise ValueError(f"task {index} has an empty name")
        stack_words = integer(fields[offset + 1], f"{name}.stack_words")
        stack_free_words = integer(fields[offset + 2], f"{name}.stack_free_words")
        stack_used_words = integer(fields[offset + 3], f"{name}.used_words")
        priority = integer(fields[offset + 4], f"{name}.priority")
        if stack_free_words > stack_words:
            raise ValueError(f"{name}.stack_free_words exceeds stack_words")
        if stack_used_words > stack_words:
            raise ValueError(f"{name}.used_words exceeds stack_words")
        tasks.append({
            "name": name,
            "stack_words": stack_words,
            "stack_free_words": stack_free_words,
            "used_words": stack_used_words,
            "priority": priority,
        })
        offset += 5

    return {
        "heap_free_bytes": heap_free,
        "heap_min_free_bytes": heap_min_free,
        "task_count": task_count,
        "tasks": tasks,
    }


def _task_summary(samples: list[dict[str, Any]], args: argparse.Namespace) -> dict[str, Any]:
    by_name: dict[str, list[dict[str, Any]]] = {}
    for sample in samples:
        for task in sample["tasks"]:
            by_name.setdefault(task["name"], []).append(task)

    result: dict[str, Any] = {}
    for name, values in by_name.items():
        declared = max(item["stack_words"] for item in values)
        free_min = min(item["stack_free_words"] for item in values)
        used_max = max(item["used_words"] for item in values)
        free_percent = (100.0 * free_min / declared) if declared else 0.0
        minimum_free_bytes = free_min * 4
        result[name] = {
            "stack_words": declared,
            "stack_free_words_min": free_min,
            "used_words_max": used_max,
            "free_percent_min": round(free_percent, 2),
            "free_bytes_min": minimum_free_bytes,
            "watermark_pass": (
                free_percent >= args.min_stack_free_percent
                and minimum_free_bytes >= args.min_stack_free_bytes
            ),
            "ota_reduction_candidate": (
                name.lower() == "ota" and free_percent >= args.ota_free_percent
            ),
        }
    return result


def summarize_board(samples: list[dict[str, Any]], args: argparse.Namespace) -> dict[str, Any]:
    if not samples:
        raise ValueError("cannot summarize an empty sample set")
    heap_free_min = min(item["heap_free_bytes"] for item in samples)
    heap_min_free_min = min(item["heap_min_free_bytes"] for item in samples)
    tasks = _task_summary(samples, args)
    task_gate = all(item["watermark_pass"] for item in tasks.values())
    heap_gate = heap_min_free_min >= args.min_heap_free_bytes
    return {
        "sample_count": len(samples),
        "heap_free_bytes_min": heap_free_min,
        "heap_min_free_bytes_min": heap_min_free_min,
        "tasks": tasks,
        "gate": {
            "min_heap_free_bytes": args.min_heap_free_bytes,
            "heap_pass": heap_gate,
            "min_stack_free_percent": args.min_stack_free_percent,
            "min_stack_free_bytes": args.min_stack_free_bytes,
            "task_watermark_pass": task_gate,
            "pass": heap_gate and task_gate,
        },
    }


def _query(ser: Any, timeout: float) -> dict[str, Any]:
    ser.reset_input_buffer()
    ser.write((STATUS_COMMAND + "\n").encode("ascii"))
    ser.flush()
    return parse_status_response(read_scpi_response(ser, STATUS_COMMAND, timeout))


def capture(args: argparse.Namespace) -> dict[str, Any]:
    boards = dict(args.board)
    captured_at = datetime.now(timezone.utc).isoformat(timespec="seconds")
    board_samples: dict[str, list[dict[str, Any]]] = {name: [] for name in boards}
    errors: dict[str, list[str]] = {name: [] for name in boards}

    with ExitStack() as stack:
        serials = {
            name: stack.enter_context(
                open_serial_port(port, args.baud, args.timeout, args.settle,
                                 read_timeout_s=0.2)
            )
            for name, port in boards.items()
        }
        for sample_index in range(args.samples):
            for name, ser in serials.items():
                try:
                    sample = _query(ser, args.timeout)
                    sample["sample_index"] = sample_index
                    sample["captured_at_utc"] = datetime.now(timezone.utc).isoformat(
                        timespec="milliseconds"
                    )
                    board_samples[name].append(sample)
                except (OSError, ValueError) as exc:
                    errors[name].append(f"sample {sample_index}: {exc}")
            if sample_index + 1 < args.samples and args.interval > 0:
                time.sleep(args.interval)

    boards_out: dict[str, Any] = {}
    for name, port in boards.items():
        entry: dict[str, Any] = {"port": port, "errors": errors[name]}
        if board_samples[name]:
            entry.update(summarize_board(board_samples[name], args))
        else:
            entry["sample_count"] = 0
            entry["gate"] = {"pass": False}
        boards_out[name] = entry
    return {
        "schema": SCHEMA,
        "command": STATUS_COMMAND,
        "captured_at_utc": captured_at,
        "requested_samples": args.samples,
        "boards": boards_out,
    }


def render_text(report: dict[str, Any]) -> str:
    lines = [
        f"schema={report['schema']} captured_at_utc={report['captured_at_utc']}",
        "board samples heap_min_free task_gate overall",
    ]
    for name, board in report["boards"].items():
        gate = board.get("gate", {})
        lines.append(
            f"{name} {board.get('sample_count', 0)} "
            f"{board.get('heap_min_free_bytes_min', 'NA')} "
            f"{'PASS' if gate.get('task_watermark_pass') else 'FAIL'} "
            f"{'PASS' if gate.get('pass') else 'FAIL'}"
        )
        for task_name, task in board.get("tasks", {}).items():
            candidate = " OTA-CANDIDATE" if task["ota_reduction_candidate"] else ""
            lines.append(
                f"  {task_name}: free_min={task['stack_free_words_min']} words "
                f"({task['free_percent_min']:.2f}%) used_max={task['used_words_max']}"
                f"{candidate}"
            )
        for error in board.get("errors", []):
            lines.append(f"  ERROR: {error}")
    return "\n".join(lines)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", action="append", type=parse_board, required=True,
                        metavar="NAME=PORT", help="board name and USB CDC port")
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.5)
    parser.add_argument("--out", type=Path, help="write JSON report")
    parser.add_argument("--text-out", type=Path, help="write human-readable report")
    parser.add_argument("--min-heap-free-bytes", type=int,
                        default=DEFAULT_MIN_HEAP_FREE_BYTES)
    parser.add_argument("--min-stack-free-percent", type=float,
                        default=DEFAULT_MIN_STACK_FREE_PERCENT)
    parser.add_argument("--min-stack-free-bytes", type=int,
                        default=DEFAULT_MIN_STACK_FREE_BYTES)
    parser.add_argument("--ota-free-percent", type=float,
                        default=DEFAULT_OTA_FREE_PERCENT)
    parser.add_argument("--strict", action="store_true",
                        help="return non-zero when any board fails the safety gate")
    args = parser.parse_args(argv)
    if args.samples <= 0 or args.interval < 0 or args.timeout <= 0 or args.settle < 0:
        parser.error("samples must be >0; interval/settle >=0; timeout >0")
    if len({name for name, _ in args.board}) != len(args.board):
        parser.error("board names must be unique")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    report = capture(args)
    encoded = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(encoded, encoding="utf-8", newline="\n")
    if args.text_out is not None:
        args.text_out.parent.mkdir(parents=True, exist_ok=True)
        args.text_out.write_text(render_text(report) + "\n", encoding="utf-8", newline="\n")
    print(render_text(report))
    if not args.strict:
        return 0 if all(board.get("sample_count", 0) > 0 for board in report["boards"].values()) else 1
    return 0 if all(board.get("gate", {}).get("pass", False) for board in report["boards"].values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
