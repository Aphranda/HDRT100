#!/usr/bin/env python3
"""Probe per-board TDMA status and active VDC PATH_DELAY by *IDN? address."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tdma_start_ring import board_command, discover  # noqa: E402
from tdma_field_parse import FIELDS as TDMA_FIELDS  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True)
    parser.add_argument("--expected-build")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def parse_values(raw: str) -> list[object]:
    values: list[object] = []
    for field in raw.split(","):
        text = field.strip().strip('"')
        try:
            values.append(int(text, 0))
        except ValueError:
            values.append(text)
    return values


def main() -> int:
    args = parse_args()
    if len(args.board_id) < 2 or len(args.board_id) > 8:
        raise SystemExit("board count must be in [2, 8]")
    args.board_ids = list(args.board_id)
    args.baud = 115200
    args.settle = 0.2
    boards = discover(args)
    missing = set(args.board_id) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    if args.expected_build:
        wrong = {address: boards[address].build for address in args.board_id
                 if boards[address].build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")

    result: dict[str, object] = {"boards": {}, "passed": True}
    for address in args.board_id:
        board = boards[address]
        tdma_raw = board_command(
            board, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args)
        path_raw = board_command(
            board, "SYSTem:SYNC:VDC:PATH:DELay?", args)
        tdma_values = parse_values(tdma_raw)
        tdma = {name: tdma_values[index] if index < len(tdma_values) else None
                for index, name in enumerate(TDMA_FIELDS)}
        path = parse_values(path_raw)
        result["boards"][address] = {
            "port": board.port,
            "build": board.build,
            "tdma": tdma,
            "path_delay_raw": path_raw,
            "path_delay": path,
        }
        if not path or path[0] not in ("OK", "MISSING"):
            result["passed"] = False

    out_dir = args.out_dir or ROOT / "build" / (
        f"tdma_path_delay_probe_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    print(f"out_dir={out_dir}")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
