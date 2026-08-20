#!/usr/bin/env python3
"""Probe reset evidence and OTA state on boards identified by ``*IDN?``."""

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


QUERIES = (
    "SYSTem:FW:BUILD?",
    "SYSTem:WATCHdog:LOG?",
    "SYSTem:WATCHdog:STATus?",
    "SYSTem:CORE?",
    "SYSTem:TDMA:RING:LOG?",
    "SYSTem:TDMA:FLIGHT:PROCess?",
    "SYSTem:TDMA:FLIGHT:FIFO?",
    "SYSTem:REFMEM:SYNC:FLIGHT?",
    "SYSTem:OTA:STATus?",
    "SYSTem:OTA:SLOT?",
    "SYSTem:OTA:RESult?",
)

BRIEF_QUERIES = (
    "SYSTem:FW:BUILD?",
    "SYSTem:WATCHdog:LOG?",
    "SYSTem:WATCHdog:STATus?",
    "SYSTem:CORE?",
    "SYSTem:OTA:SLOT?",
)


@dataclass(frozen=True)
class Board:
    port: str
    address: str
    idn: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--board-id", action="append", required=True,
        help="exact *IDN? third-field address; repeat for each board")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--attempts", type=int, default=10)
    parser.add_argument("--retry-delay", type=float, default=0.5)
    parser.add_argument("--brief", action="store_true",
                        help="query only build, watchdog, core, and OTA slot")
    parser.add_argument("--query", action="append",
                        help="override the default query set; may be repeated")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def command(ser: serial.Serial, text: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, text, timeout_s, require_match=True)


def discover(wanted: set[str], args: argparse.Namespace) -> dict[str, Board]:
    found: dict[str, Board] = {}
    for port in list_ports.comports():
        try:
            with serial.Serial(port.device, args.baud, timeout=0.1,
                               write_timeout=args.timeout) as ser:
                time.sleep(args.settle)
                identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
                if identity.address in wanted:
                    found[identity.address] = Board(
                        port=port.device, address=identity.address, idn=identity.idn)
        except (OSError, serial.SerialException, ValueError):
            continue
    return found


def probe(board: Board, args: argparse.Namespace) -> dict[str, str]:
    with serial.Serial(board.port, args.baud, timeout=0.1,
                       write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
        if identity.address != board.address:
            raise RuntimeError(
                f"{board.port}: identity changed to {identity.address}, "
                f"expected {board.address}")
        queries = tuple(args.query) if args.query else (
            BRIEF_QUERIES if args.brief else QUERIES)
        return {query: command(ser, query, args.timeout) for query in queries}


def main() -> int:
    args = parse_args()
    wanted = set(args.board_id)
    if len(wanted) != len(args.board_id):
        raise SystemExit("board-id values must be unique")
    if args.attempts <= 0:
        raise SystemExit("attempts must be positive")

    boards: dict[str, Board] = {}
    samples: dict[str, dict[str, str]] = {}
    errors: dict[str, str] = {}
    for attempt in range(1, args.attempts + 1):
        boards.update(discover(wanted - set(boards), args))
        for address in sorted(wanted - set(samples)):
            board = boards.get(address)
            if board is None:
                errors[address] = "not discovered"
                continue
            try:
                samples[address] = probe(board, args)
                errors.pop(address, None)
            except (OSError, serial.SerialException, ValueError, RuntimeError) as exc:
                errors[address] = f"attempt {attempt}: {exc}"
                boards.pop(address, None)
        if set(samples) == wanted:
            break
        time.sleep(args.retry_delay)

    result = {
        "passed": set(samples) == wanted,
        "boards": {address: asdict(board) for address, board in boards.items()},
        "samples": samples,
        "errors": errors,
    }
    out_dir = args.out_dir or (
        ROOT / "build-validation" /
        f"watchdog_reset_probe_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    print(f"out_dir={out_dir}")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
