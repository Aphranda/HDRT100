#!/usr/bin/env python3
"""Read-only preflight for the unattended HAWF night run.

Checks the four-board identity/build/ring/error-queue state before step24/29.
It never starts, stops, stages, or flashes anything.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tdma_start_ring import (  # type: ignore  # noqa: E402
    board_command,
    close_persistent_connections,
    discover,
    status,
)


def _query_build(board, args: argparse.Namespace) -> str:
    return board_command(board, "SYSTem:FW:BUILD?", args).strip().strip('"')


def _query_error(board, args: argparse.Namespace) -> str:
    return board_command(board, "SYSTem:ERR?", args).strip()


def evaluate(records: dict[str, dict], expected_build: str | None) -> dict:
    errors: list[str] = []
    board_ids = sorted(records)
    if len(board_ids) != 4:
        errors.append(f"expected 4 boards, found {len(board_ids)}")
    builds = {board_id: record.get("build", "") for board_id, record in records.items()}
    distinct = sorted(set(builds.values()))
    if len(distinct) != 1:
        errors.append("board build mismatch: " + ", ".join(
            f"{board_id}={builds[board_id]}" for board_id in board_ids))
    elif expected_build and distinct[0] != expected_build:
        errors.append(f"expected build {expected_build}, found {distinct[0]}")
    for board_id, record in records.items():
        ring = record.get("ring") or {}
        if ring.get("ring_enabled") != 0:
            errors.append(f"{board_id}: ring_enabled={ring.get('ring_enabled')} "
                          "(preflight requires stopped)")
        if not str(record.get("error", "")).startswith(("0,", "+0,")):
            errors.append(f"{board_id}: SCPI error queue not empty: "
                          f"{record.get('error')!r}")
    return {
        "schema": "HDRT100_HAWF_NIGHT_PREFLIGHT_V1",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "expected_build": expected_build,
        "passed": not errors,
        "errors": errors,
        "boards": records,
        "builds": builds,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address; repeat four times")
    parser.add_argument("--expected-build")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=0.05)
    parser.add_argument("--out", type=Path,
                        default=ROOT / "out/HardwareAcceptance/night-preflight.json")
    args = parser.parse_args()

    board_args = argparse.Namespace(
        board_ids=args.board_id, baud=args.baud, timeout=args.timeout,
        settle=args.settle, keep_open=True)
    boards = discover(board_args)
    missing = sorted(set(args.board_id) - set(boards))
    if missing:
        print(f"missing boards: {', '.join(missing)}", file=sys.stderr)
        return 2
    records: dict[str, dict] = {}
    try:
        for board_id in args.board_id:
            board = boards[board_id]
            try:
                ring = status(board, board_args)
                error = _query_error(board, board_args)
                build = _query_build(board, board_args)
            except Exception as exc:  # keep the failure in evidence
                records[board_id] = {
                    "idn": getattr(board, "idn", ""),
                    "port": getattr(board, "port", ""),
                    "error": f"<exception: {type(exc).__name__}: {exc}>",
                    "build": "",
                    "ring": {},
                }
                continue
            records[board_id] = {
                "idn": getattr(board, "idn", ""),
                "port": getattr(board, "port", ""),
                "build": build,
                "ring": ring,
                "error": error,
            }
    finally:
        close_persistent_connections()

    report = evaluate(records, args.expected_build)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "errors": report["errors"],
                      "out": str(args.out)}, ensure_ascii=False))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
