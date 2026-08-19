#!/usr/bin/env python3
"""Stage, clock-train, and start a two-board TDMA SPI ring by *IDN? address."""

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
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from scpi_common.board_identity import parse_idn_response  # noqa: E402
from scpi_common.scpi_serial import read_scpi_response  # noqa: E402
from tdma_field_parse import FIELDS as TDMA_FIELDS  # noqa: E402


@dataclass(frozen=True)
class Board:
    port: str
    address: str
    idn: str
    build: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-id", required=True)
    parser.add_argument("--forward-id", required=True)
    parser.add_argument("--expected-build")
    parser.add_argument("--cycles", type=int, default=4096)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--start-wait", type=float, default=2.0)
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


def board_command(board: Board, text: str, args: argparse.Namespace) -> str:
    with serial.Serial(board.port, args.baud, timeout=0.1,
                       write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
        if identity.address != board.address:
            raise RuntimeError(
                f"{board.port}: identity changed to {identity.address}, "
                f"expected {board.address}")
        response = command(ser, text, args.timeout)
        # Most action commands return a bare OK. The shared reader strips
        # that acknowledgement to protect query parsing, so represent the
        # resulting empty response explicitly; state is verified below.
        action = text.strip().split(maxsplit=1)[0].upper()
        ack_only_actions = {
            "SYSTEM:TDMA:RING:STOP", "SYST:TDMA:RING:STOP",
            "SYSTEM:TDMA:RING:ARM", "SYST:TDMA:RING:ARM",
            "SYSTEM:TDMA:RING:START", "SYST:TDMA:RING:START",
        }
        if response == "<timeout>" and action in ack_only_actions:
            return "OK(no payload; verified by state readback)"
        return response


def status(board: Board, args: argparse.Namespace) -> dict[str, int]:
    raw = board_command(board, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args)
    values = [int(value.strip().strip('"'), 0) for value in raw.split(",")]
    if len(values) != len(TDMA_FIELDS):
        raise RuntimeError(
            f"{board.address}: TDMA field count {len(values)}, "
            f"expected {len(TDMA_FIELDS)}")
    keys = ("ring_enabled", "ring_local_slot_id", "ring_reference_slot_id",
            "ring_adapter_started", "ring_up_running", "ring_down_running",
            "ring_adapter_tx_count", "ring_adapter_rx_count",
            "ring_adapter_rx_bad_count")
    return {key: values[TDMA_FIELDS.index(key)] for key in keys}


def wait_started(board: Board, args: argparse.Namespace) -> dict[str, int]:
    deadline = time.monotonic() + args.arm_wait
    last: dict[str, int] = {}
    while time.monotonic() < deadline:
        last = status(board, args)
        if last["ring_enabled"] == 1 and last["ring_adapter_started"] == 1:
            return last
        time.sleep(0.05)
    raise RuntimeError(f"{board.address}: ARM timeout, last={last}")


def train(board: Board, args: argparse.Namespace) -> str:
    expected = str(args.cycles)
    for _ in range(3):
        response = board_command(
            board, f"SYSTem:TDMA:RING:TRAIN {args.cycles}", args)
        if response.strip().strip('"') == expected:
            return response
    raise RuntimeError(
        f"{board.address}: TRAIN did not acknowledge {args.cycles} cycles")


def main() -> int:
    args = parse_args()
    if args.reference_id == args.forward_id:
        raise SystemExit("reference-id and forward-id must differ")
    if args.cycles <= 0 or args.cycles > 65536 or args.cycles % 8:
        raise SystemExit("cycles must be an 8-cycle multiple in [8, 65536]")

    boards = discover(args)
    missing = {args.reference_id, args.forward_id} - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    if args.expected_build:
        for board in boards.values():
            if board.build != args.expected_build:
                raise SystemExit(
                    f"{board.address}: build {board.build} != {args.expected_build}")

    reference = boards[args.reference_id]
    forward = boards[args.forward_id]
    result: dict[str, object] = {
        "reference_id": args.reference_id,
        "forward_id": args.forward_id,
        "cycles": args.cycles,
        "boards": {address: asdict(board) for address, board in boards.items()},
        "sequence": [
            "STOP both", "LOCAL reference=0 forward=1", "ARM both",
            f"TRAIN forward={args.cycles}", f"TRAIN reference={args.cycles}",
            "START forward", "START reference",
        ],
    }
    print(json.dumps(result, indent=2))
    if args.dry_run:
        return 0

    acknowledgements: list[dict[str, str]] = []
    for board in (reference, forward):
        acknowledgements.append({board.address: board_command(
            board, "SYSTem:TDMA:RING:STOP", args)})
    acknowledgements.append({reference.address: board_command(
        reference, "SYSTem:TDMA:RING:LOCAL 0", args)})
    acknowledgements.append({forward.address: board_command(
        forward, "SYSTem:TDMA:RING:LOCAL 1", args)})

    for board in (forward, reference):
        acknowledgements.append({board.address: board_command(
            board, "SYSTem:TDMA:RING:ARM", args)})
    armed = {board.address: wait_started(board, args)
             for board in (forward, reference)}

    for board in (forward, reference):
        acknowledgements.append({board.address: train(board, args)})
    for board in (forward, reference):
        acknowledgements.append({board.address: board_command(
            board, "SYSTem:TDMA:RING:START", args)})

    time.sleep(args.start_wait)
    final = {board.address: status(board, args)
             for board in (reference, forward)}
    passed = (
        final[reference.address]["ring_local_slot_id"] == 0
        and final[forward.address]["ring_local_slot_id"] == 1
        and all(item["ring_adapter_started"] == 1 for item in final.values())
        and all(item["ring_up_running"] == 1 for item in final.values())
    )
    result.update({"passed": passed, "acknowledgements": acknowledgements,
                   "armed": armed, "final": final})
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke" /
        f"tdma_start_ring_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    print(f"out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
