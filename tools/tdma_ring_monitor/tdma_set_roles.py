#!/usr/bin/env python3
"""Assign two-board TDMA reference/forward roles by *IDN? unique address."""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.board_identity import parse_idn_response  # noqa: E402
from scpi_common.scpi_serial import read_scpi_response  # noqa: E402


RING_LOCAL_SLOT = 57
RING_REFERENCE_SLOT = 58
RING_UP_RUNNING = 63
RING_DOWN_RUNNING = 64
TDMA_STATUS_FIELD_COUNT = 110


@dataclass(frozen=True)
class Board:
    port: str
    address: str
    idn: str
    build: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-id", required=True,
                        help="exact *IDN? third-field address for reference slot0")
    parser.add_argument("--forward-id", required=True,
                        help="exact *IDN? third-field address for forward slot1")
    parser.add_argument("--expected-build")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--verify-wait", type=float, default=1.0)
    parser.add_argument(
        "--resync-reference",
        action="store_true",
        help=(
            "temporarily move the reference board to slot1, leaving no "
            "reference transmitter, then move it back to slot0 so PIO RX "
            "is re-armed from an idle bus"
        ),
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def command(ser: serial.Serial, text: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, text, timeout_s, require_match=True)


def probe(port: str, args: argparse.Namespace) -> Board | None:
    try:
        with serial.Serial(port, args.baud, timeout=0.1,
                           write_timeout=args.timeout) as ser:
            time.sleep(args.settle)
            idn = command(ser, "*IDN?", args.timeout)
            identity = parse_idn_response(idn)
            if identity.address not in {args.reference_id, args.forward_id}:
                return None
            build = command(ser, "SYSTem:FW:BUILD?", args.timeout).strip('"')
            return Board(port, identity.address, identity.idn, build)
    except (OSError, serial.SerialException, ValueError):
        return None


def discover(args: argparse.Namespace) -> dict[str, Board]:
    found: dict[str, Board] = {}
    for item in list_ports.comports():
        board = probe(item.device, args)
        if board is not None:
            found[board.address] = board
    return found


def set_local_slot(board: Board, slot: int, args: argparse.Namespace) -> str:
    with serial.Serial(board.port, args.baud, timeout=0.1,
                       write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
        if identity.address != board.address:
            raise RuntimeError(
                f"{board.port}: identity changed to {identity.address}, expected {board.address}")
        return command(ser, f"SYSTem:TDMA:RING:LOCAL {slot}", args.timeout)


def read_status(board: Board, args: argparse.Namespace) -> dict[str, int]:
    with serial.Serial(board.port, args.baud, timeout=0.1,
                       write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
        if identity.address != board.address:
            raise RuntimeError(
                f"{board.port}: identity changed to {identity.address}, expected {board.address}")
        raw = command(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args.timeout)
    fields = [int(value.strip().strip('"'), 0) for value in raw.split(",")]
    if len(fields) != TDMA_STATUS_FIELD_COUNT:
        raise RuntimeError(f"{board.address}: TDMA status field count {len(fields)}")
    return {
        "local_slot": fields[RING_LOCAL_SLOT],
        "reference_slot": fields[RING_REFERENCE_SLOT],
        "up_running": fields[RING_UP_RUNNING],
        "down_running": fields[RING_DOWN_RUNNING],
    }


def main() -> int:
    args = parse_args()
    if args.reference_id == args.forward_id:
        raise SystemExit("reference-id and forward-id must differ")

    boards = discover(args)
    missing = {args.reference_id, args.forward_id} - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    for board in boards.values():
        if args.expected_build and board.build != args.expected_build:
            raise SystemExit(
                f"{board.address}: build {board.build} != {args.expected_build}")

    plan = {
        args.reference_id: {"port": boards[args.reference_id].port, "local_slot": 0},
        args.forward_id: {"port": boards[args.forward_id].port, "local_slot": 1},
    }
    if args.resync_reference:
        plan[args.reference_id]["transition"] = [1, 0]
    print(json.dumps({"plan": plan}, indent=2))
    if args.dry_run:
        return 0

    before_statuses = {
        address: read_status(board, args) for address, board in boards.items()
    }
    acknowledgements: dict[str, object] = {}
    if before_statuses[args.forward_id]["local_slot"] != 1:
        acknowledgements[args.forward_id] = set_local_slot(
            boards[args.forward_id], 1, args)
    else:
        acknowledgements[args.forward_id] = "already_slot1"

    if args.resync_reference:
        acknowledgements[args.reference_id] = [
            set_local_slot(boards[args.reference_id], 1, args),
            set_local_slot(boards[args.reference_id], 0, args),
        ]
    elif before_statuses[args.reference_id]["local_slot"] != 0:
        acknowledgements[args.reference_id] = set_local_slot(
            boards[args.reference_id], 0, args)
    else:
        acknowledgements[args.reference_id] = "already_slot0"
    time.sleep(args.verify_wait)
    statuses = {address: read_status(board, args) for address, board in boards.items()}
    passed = (
        statuses[args.reference_id]["local_slot"] == 0
        and statuses[args.reference_id]["reference_slot"] == 0
        and statuses[args.forward_id]["local_slot"] == 1
        and statuses[args.forward_id]["reference_slot"] == 0
    )
    result = {
        "passed": passed,
        "reference_id": args.reference_id,
        "forward_id": args.forward_id,
        "boards": {address: asdict(board) for address, board in boards.items()},
        "before_statuses": before_statuses,
        "acknowledgements": acknowledgements,
        "statuses": statuses,
    }
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke"
        / f"tdma_set_roles_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    print(f"out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
