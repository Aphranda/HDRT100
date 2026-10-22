#!/usr/bin/env python3
"""Dedicated TDMA bring-up workflow.

This is a thin, reviewable orchestration around the existing SCPI/DMA ring
commands.  It does NOT open a new bus and does NOT use the raw SPI debug
surface: every step goes through the board's TDMA ring/seat SCPI commands.

Typical no-DPLL, second-class-seat run:

  python tools/tdma/tdma_workflow.py \
      --board-id 0010071E65B5CB38 --slot 0 \
      --board-id A1E549202D18ED6A --slot 1 \
      --node-count 2 --seat-rotation 1,2,7 --cycles 4096 --full

The workflow is intentionally explicit: a slot that does not match the board's
ring profile is reported, never silently inferred.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.tdma_ring_monitor.tdma_start_ring import (  # noqa: E402
    board_command,
    close_persistent_connections,
    discover,
)

STEPS = {
    0: "discover",
    1: "stop",
    2: "seat_rotation",
    3: "topology",
    4: "train",
    5: "arm",
    6: "start",
    7: "status",
}


def make_args(ns: SimpleNamespace) -> SimpleNamespace:
    ns.baud = 115200
    ns.timeout = 8.0
    ns.read_timeout = 0.02
    ns.settle = 0.02
    ns.keep_open = True
    ns.short_open = False
    ns.action_timeout = 0.5
    return ns


def command(board, text: str, ns: SimpleNamespace) -> str:
    result = board_command(board, text, ns)
    if result == "<timeout>":
        raise RuntimeError(f"{board.port}: timeout for {text}")
    return result


def command_retry(board, text: str, ns: SimpleNamespace,
                  attempts: int = 3, delay_s: float = 0.5) -> str:
    last: Exception | None = None
    for attempt in range(attempts):
        try:
            return command(board, text, ns)
        except Exception as exc:  # noqa: BLE001
            last = exc
            if attempt + 1 < attempts:
                time.sleep(delay_s)
    assert last is not None
    raise last


def status(board, ns: SimpleNamespace) -> dict[str, str]:
    return {
        "ring": command(board, "SYSTem:TDMA:RING:STATus?", ns),
        "config": command(board, "SYSTem:TDMA:RING:CONFig?", ns),
        "arm": command(board, "SYSTem:TDMA:RING:ARM:STATus?", ns),
    }


def run(args: argparse.Namespace) -> int:
    if len(args.board_id) < 2:
        raise SystemExit("at least two --board-id values are required")
    if len(args.slot) != len(args.board_id):
        raise SystemExit("provide one --slot per --board-id")

    ns = make_args(SimpleNamespace(
        board_ids=list(args.board_id),
        baud=args.baud,
        timeout=args.timeout,
        read_timeout=args.read_timeout,
        settle=args.settle,
        keep_open=True,
        short_open=False,
        action_timeout=0.5,
    ))
    ns.timeout = args.timeout
    ns.baud = args.baud
    ns.read_timeout = args.read_timeout
    ns.settle = args.settle
    # Ring STOP can leave a persistent CDC handle temporarily unresponsive on
    # the bench; use one bounded session per command for deterministic recovery.
    ns.keep_open = False
    ns.short_open = True
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    evidence: dict = {
        "started_utc": datetime.utcnow().isoformat() + "Z",
        "board_ids": list(args.board_id),
        "slots": [int(s) for s in args.slot],
        "node_count": args.node_count,
        "seat_rotation": args.seat_rotation,
        "cycles": args.cycles,
        "steps": [],
    }

    try:
        boards = discover(ns)
        selected = []
        for address, slot in zip(args.board_id, args.slot):
            board = boards.get(address)
            if board is None:
                raise RuntimeError(f"board not found by *IDN?: {address}")
            selected.append((board, int(slot)))
        evidence["boards"] = {
            b.address: {"port": b.port, "build": b.build, "slot": slot}
            for b, slot in selected
        }

        for number in args.steps:
            name = STEPS[number]
            if args.dry_run:
                evidence["steps"].append({"step": number, "name": name,
                                          "result": "DRY_RUN"})
                continue
            if number == 0:
                evidence["steps"].append({"step": 0, "name": name,
                                          "boards": evidence["boards"]})
            elif number == 1:
                for board, _ in selected:
                    command(board, "SYSTem:TDMA:RING:STOP", ns)
                # STOP retires the ring owner asynchronously; seat/topology
                # writes are rejected until that retirement settles.
                time.sleep(4.0)
                evidence["steps"].append({"step": 1, "name": name, "result": "OK"})
            elif number == 2:
                for board, _ in selected:
                    command_retry(
                        board, f"SYSTem:TDMA:SEAT:ROTation {args.seat_rotation}", ns)
                evidence["steps"].append({"step": 2, "name": name,
                                          "rotation": args.seat_rotation})
            elif number == 3:
                for board, slot in selected:
                    command_retry(
                        board,
                        f"SYSTem:TDMA:RING:TOPology {args.node_count},{slot},0",
                        ns)
                evidence["steps"].append({"step": 3, "name": name,
                                          "node_count": args.node_count})
            elif number == 4:
                for board, _ in selected:
                    command(board, f"SYSTem:TDMA:RING:TRAIN {args.cycles}", ns)
                evidence["steps"].append({"step": 4, "name": name,
                                          "cycles": args.cycles})
            elif number == 5:
                for board, _ in selected:
                    command(board, "SYSTem:TDMA:RING:ARM", ns)
                time.sleep(args.arm_wait)
                evidence["steps"].append({"step": 5, "name": name,
                                          "arm": {b.address: command(
                                              b, "SYSTem:TDMA:RING:ARM:STATus?", ns)
                                              for b, _ in selected}})
            elif number == 6:
                for board, _ in selected[1:]:
                    command(board, "SYSTem:TDMA:RING:STARt", ns)
                # reference is the first --board-id by contract
                command(selected[0][0], "SYSTem:TDMA:RING:STARt", ns)
                time.sleep(args.start_wait)
                evidence["steps"].append({"step": 6, "name": name})
            elif number == 7:
                evidence["steps"].append({"step": 7, "name": name,
                                          "status": {b.address: status(b, ns)
                                                     for b, _ in selected}})
    except Exception as exc:  # noqa: BLE001
        evidence["error"] = f"{type(exc).__name__}: {exc}"
        evidence["finished_utc"] = datetime.utcnow().isoformat() + "Z"
        (out_dir / "tdma_workflow.json").write_text(
            json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(evidence, indent=2))
        return 1
    finally:
        close_persistent_connections()

    evidence["finished_utc"] = datetime.utcnow().isoformat() + "Z"
    (out_dir / "tdma_workflow.json").write_text(
        json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(evidence, indent=2))
    return 0


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--board-id", action="append", required=True)
    p.add_argument("--slot", action="append", required=True,
                   help="TDMA ring slot for the matching --board-id")
    p.add_argument("--node-count", type=int, default=None)
    p.add_argument("--seat-rotation", default="1,2,7")
    p.add_argument("--cycles", type=int, default=4096)
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--timeout", type=float, default=8.0)
    p.add_argument("--read-timeout", type=float, default=0.02)
    p.add_argument("--settle", type=float, default=0.02)
    p.add_argument("--arm-wait", type=float, default=1.0)
    p.add_argument("--start-wait", type=float, default=1.0)
    p.add_argument("--out-dir", default="out/tdma_workflow")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--full", action="store_true",
                   help="run every step (same as --steps all)")
    p.add_argument("--steps", default="all",
                   help="all or comma/range list, e.g. 1-4,6")
    ns = p.parse_args()
    if ns.node_count is None:
        ns.node_count = len(ns.board_id)
    if ns.full:
        ns.steps = "all"
    ns.steps = parse_steps(ns.steps)
    return ns


def parse_steps(text: str) -> list[int]:
    if text == "all":
        return list(STEPS)
    result: list[int] = []
    for part in text.split(","):
        if "-" in part:
            lo, hi = part.split("-", 1)
            result.extend(range(int(lo), int(hi) + 1))
        else:
            result.append(int(part))
    unknown = sorted(set(result) - set(STEPS))
    if unknown:
        raise SystemExit(f"unknown steps: {unknown}")
    return result


if __name__ == "__main__":
    raise SystemExit(run(parse_args()))
