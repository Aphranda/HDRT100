#!/usr/bin/env python3
"""Stage, clock-train, and start a 2..8-board TDMA SPI ring by *IDN? address."""

from __future__ import annotations

import argparse
import atexit
import csv
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
from tdma_field_parse import FIELDS as TDMA_FIELDS, PHYS_FIELDS  # noqa: E402
from calibration_ring_validate.calibration_timeout_config import (  # noqa: E402
    DEFAULT_ACTION_TIMEOUT_S,
)


@dataclass(frozen=True)
class Board:
    port: str
    address: str
    idn: str
    build: str


class BoardConnection:
    """Persistent SCPI connection for one board during a validation run."""

    def __init__(self, board: Board, args: argparse.Namespace) -> None:
        self.board = board
        self.args = args
        self.ser: serial.Serial | None = None
        self.identity_verified = False

    def open(self) -> "BoardConnection":
        if self.ser is not None:
            return self
        self.ser = serial.Serial(
            self.board.port, self.args.baud, timeout=0.1,
            write_timeout=self.args.timeout)
        time.sleep(self.args.settle)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()
        identity = parse_idn_response(
            command(self.ser, "*IDN?", self.args.timeout))
        if identity.address != self.board.address:
            self.close()
            raise RuntimeError(
                f"{self.board.port}: identity changed to {identity.address}, "
                f"expected {self.board.address}")
        self.identity_verified = True
        return self

    def close(self) -> None:
        if self.ser is None:
            return
        try:
            self.ser.flush()
        finally:
            self.ser.close()
            self.ser = None
            self.identity_verified = False

    def command(self, text: str) -> str:
        self.open()
        assert self.ser is not None
        return _board_command_on_serial(self.board, text, self.args, self.ser)


_PERSISTENT_CONNECTIONS: dict[str, BoardConnection] = {}


def close_persistent_connections() -> None:
    for connection in list(_PERSISTENT_CONNECTIONS.values()):
        connection.close()
    _PERSISTENT_CONNECTIONS.clear()


atexit.register(close_persistent_connections)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append",
                        help=("exact *IDN? address in physical ring order; "
                              "repeat 2..8 times, first board is reference slot0"))
    parser.add_argument("--reference-id",
                        help="legacy two-board reference address")
    parser.add_argument("--forward-id",
                        help="legacy two-board forward address")
    parser.add_argument("--expected-build")
    parser.add_argument("--level", type=int, default=7,
                        help=("TDMA operating level applied to every board while "
                              "stopped; default level 7 is 10 MHz / 1 ms"))
    parser.add_argument("--cycles", type=int, default=4096)
    parser.add_argument("--train-chunk-cycles", type=int, default=0,
                        help=("split the requested training total into bounded "
                              "chunks; 0 sends one command"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--action-timeout", type=float, default=DEFAULT_ACTION_TIMEOUT_S,
                        help="bounded wait for action acknowledgements")
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--start-wait", type=float, default=2.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--short-open", action="store_true",
                        help="open/close CDC for every command (diagnostic fallback)")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def resolve_board_ids(args: argparse.Namespace) -> list[str]:
    if args.board_id:
        if args.reference_id or args.forward_id:
            raise ValueError(
                "use either repeated --board-id or legacy two-board IDs")
        board_ids = list(args.board_id)
    else:
        if not args.reference_id or not args.forward_id:
            raise ValueError("provide 2..8 --board-id values")
        board_ids = [args.reference_id, args.forward_id]
    if len(board_ids) < 2 or len(board_ids) > 8:
        raise ValueError("board count must be in [2, 8]")
    if len(set(board_ids)) != len(board_ids):
        raise ValueError("board IDs must be unique")
    return board_ids


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
            if identity.address not in set(args.board_ids):
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


def order_boards_by_board_no(
        boards: dict[str, Board], board_ids: list[str],
        args: argparse.Namespace) -> list[Board]:
    """Load the Node order frozen by Calibration step 1."""
    numbered: list[tuple[int, Board]] = []
    for address in board_ids:
        board = boards[address]
        raw = board_command(board, "SYSTem:BOARD:NO?", args)
        try:
            board_no = int(raw.strip().strip('"'), 0)
        except ValueError as exc:
            raise RuntimeError(
                f"{board.address}: invalid BOARD:NO response {raw!r}") from exc
        numbered.append((board_no, board))
    expected = list(range(1, len(board_ids) + 1))
    observed = sorted(board_no for board_no, _ in numbered)
    if observed != expected:
        raise RuntimeError(
            "Calibration step 1 is incomplete: BOARD:NO values must be "
            f"unique and contiguous from 1 through {len(board_ids)}; "
            f"observed={observed}")
    return [board for _, board in sorted(numbered, key=lambda item: item[0])]


def _board_command_on_serial(board: Board, text: str,
                            args: argparse.Namespace,
                            ser: serial.Serial) -> str:
    # Most action commands return a bare OK. The shared reader strips
    # that acknowledgement to protect query parsing, so represent the
    # resulting empty response explicitly; state is verified below.
    action = text.strip().split(maxsplit=1)[0].upper()
    ack_only_actions = {
        "SYSTEM:TDMA:RING:STOP", "SYST:TDMA:RING:STOP",
        "SYSTEM:TDMA:RING:TOPOLOGY", "SYST:TDMA:RING:TOPOLOGY",
        "SYSTEM:TDMA:RING:ARM", "SYST:TDMA:RING:ARM",
        "SYSTEM:TDMA:RING:START", "SYST:TDMA:RING:START",
        "CALIBRATION:P3:START", "CALIBRATION:P3:STOP",
        "CALIBRATION:MARKER:INJECT", "CALIBRATION:MARKER:STOP",
        "CALIBRATION:DATA:INJECT", "CALIBRATION:DATA:STOP",
        "CALIBRATION:SCK:INJECT", "CALIBRATION:SCK:STOP",
        "SYSTEM:BOOT:RESET", "SYST:BOOT:RESET",
    }
    response = command(
        ser, text,
        min(args.timeout, getattr(args, "action_timeout",
                                  DEFAULT_ACTION_TIMEOUT_S))
        if action in ack_only_actions else args.timeout)
    if response == "<timeout>" and action in ack_only_actions:
        return "OK(no payload; verified by state readback)"
    return response


def board_command(board: Board, text: str, args: argparse.Namespace) -> str:
    if getattr(args, "keep_open", False):
        connection = _PERSISTENT_CONNECTIONS.get(board.address)
        if connection is None:
            connection = BoardConnection(board, args)
            _PERSISTENT_CONNECTIONS[board.address] = connection
        return connection.command(text)
    with serial.Serial(board.port, args.baud, timeout=0.1,
                       write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
        if identity.address != board.address:
            raise RuntimeError(
                f"{board.port}: identity changed to {identity.address}, "
                f"expected {board.address}")
        return _board_command_on_serial(board, text, args, ser)


def persistent_serial(board: Board, args: argparse.Namespace) -> serial.Serial:
    """Return a validated persistent serial handle for multi-command tools.

    The identity is checked once when the connection is opened.  Callers must
    not close this handle; the process-wide connection registry owns it.
    """
    connection = _PERSISTENT_CONNECTIONS.get(board.address)
    if connection is None:
        connection = BoardConnection(board, args)
        _PERSISTENT_CONNECTIONS[board.address] = connection
    connection.open()
    assert connection.ser is not None
    return connection.ser


def status(board: Board, args: argparse.Namespace) -> dict[str, int]:
    raw = board_command(board, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args)
    if raw == "<timeout>":
        raise RuntimeError(f"{board.address}: TDMA status query timed out")
    try:
        values = [int(value.strip().strip('"'), 0) for value in raw.split(",")]
    except ValueError as exc:
        raise RuntimeError(
            f"{board.address}: invalid TDMA status response {raw!r}") from exc
    if len(values) != len(TDMA_FIELDS):
        raise RuntimeError(
            f"{board.address}: TDMA field count {len(values)}, "
            f"expected {len(TDMA_FIELDS)}")
    keys = ("ring_enabled", "ring_config_seq", "ring_node_count",
            "ring_local_slot_id",
            "ring_reference_slot_id",
            "ring_profile_crc32", "ring_schedule_crc32",
            "ring_adapter_started", "ring_up_running", "ring_down_running",
            "ring_adapter_tx_count", "ring_adapter_rx_count",
            "ring_adapter_rx_bad_count")
    return {key: values[TDMA_FIELDS.index(key)] for key in keys}


def snapshot(board: Board, args: argparse.Namespace) -> dict[str, dict[str, int]]:
    """Read the TDMA and physical counters through the shared connection."""
    tdma_raw = board_command(board, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args)
    phys_raw = board_command(board, "SYSTem:SYNC:VDC:TDMA:PHYS?", args)
    try:
        tdma_values = [int(value.strip().strip('"'), 0)
                       for value in tdma_raw.split(",")]
        phys_values = [int(value.strip().strip('"'), 0)
                       for value in phys_raw.split(",")]
    except ValueError as exc:
        raise RuntimeError(
            f"{board.address}: invalid counter snapshot: "
            f"tdma={tdma_raw!r} phys={phys_raw!r}") from exc
    return {
        "tdma": {name: tdma_values[index] if index < len(tdma_values) else -1
                 for index, name in enumerate(TDMA_FIELDS)},
        "phys": {name: phys_values[index] if index < len(phys_values) else -1
                 for index, name in enumerate(PHYS_FIELDS)},
    }


def wait_started(board: Board, args: argparse.Namespace) -> dict[str, int]:
    deadline = time.monotonic() + args.arm_wait
    last: dict[str, int] = {}
    last_error = ""
    while time.monotonic() < deadline:
        try:
            last = status(board, args)
        except (OSError, RuntimeError, serial.SerialException) as exc:
            last_error = str(exc)
            time.sleep(0.1)
            continue
        if last["ring_enabled"] == 1 and last["ring_adapter_started"] == 1:
            return last
        time.sleep(0.05)
    physical = "<unavailable>"
    try:
        physical = board_command(
            board, "SYSTem:SYNC:VDC:TDMA:PHYS?", args)
    except (OSError, RuntimeError, serial.SerialException) as exc:
        physical = f"<query-error: {exc}>"
    raise RuntimeError(
        f"{board.address}: ARM timeout, last={last}, last_error={last_error}, "
        f"physical={physical}")


def _train_on_serial(board: Board, args: argparse.Namespace,
                     ser: serial.Serial) -> dict[str, object]:
    chunk_cycles = args.train_chunk_cycles or args.cycles
    remaining = args.cycles
    responses: list[str] = []
    completed_cycles = 0
    while remaining > 0:
        current = min(chunk_cycles, remaining)
        before_raw = command(
            ser, "SYSTem:TDMA:RING:TRAIN:STATus?", args.timeout)
        before = next(csv.reader([before_raw]), [])
        if len(before) != 28 or before[0].strip().strip('"') != "CLKTRAIN":
            raise RuntimeError(
                f"{board.address}: invalid pre-TRAIN status {before_raw!r}")
        previous_request_seq = int(before[5].strip().strip('"'), 0)
        response = ""
        for _ in range(3):
            response = command(
                ser, f"SYSTem:TDMA:RING:TRAIN {current}", args.timeout)
            responses.append(response)
            if response.strip().strip('"') == str(current):
                break
            time.sleep(0.05)
        if response.strip().strip('"') != str(current):
            raise RuntimeError(
                f"{board.address}: TRAIN chunk {current} failed after "
                f"{completed_cycles}/{args.cycles} cycles, response={response!r}")
        deadline = time.monotonic() + args.timeout
        train_snapshot: list[str] = []
        while time.monotonic() < deadline:
            train_raw = command(
                ser, "SYSTem:TDMA:RING:TRAIN:STATus?", args.timeout)
            train_snapshot = next(csv.reader([train_raw]), [])
            if (len(train_snapshot) == 28 and
                    train_snapshot[0].strip().strip('"') == "CLKTRAIN"):
                state = int(train_snapshot[2].strip().strip('"'), 0)
                request_seq = int(
                    train_snapshot[5].strip().strip('"'), 0)
                if request_seq != previous_request_seq and state in (1, 3, 4):
                    if state == 4:
                        raise RuntimeError(
                            f"{board.address}: TRAIN {current} entered ERROR: "
                            f"{train_raw}")
                    break
            time.sleep(0.02)
        else:
            raise RuntimeError(
                f"{board.address}: TRAIN {current} owner completion timeout, "
                f"last={train_snapshot}")
        completed_cycles += current
        remaining -= current
    return {
        "requested_cycles": args.cycles,
        "chunk_cycles": chunk_cycles,
        "chunk_count": (args.cycles + chunk_cycles - 1) // chunk_cycles,
        "command_attempt_count": len(responses),
        "completed_cycles": completed_cycles,
        "last_response": responses[-1] if responses else "",
    }


def train(board: Board, args: argparse.Namespace) -> dict[str, object]:
    if getattr(args, "keep_open", False):
        return _train_on_serial(board, args, persistent_serial(board, args))
    with serial.Serial(board.port, args.baud, timeout=0.1,
                       write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
        if identity.address != board.address:
            raise RuntimeError(
                f"{board.port}: identity changed to {identity.address}, "
                f"expected {board.address}")
        return _train_on_serial(board, args, ser)


def main() -> int:
    args = parse_args()
    args.keep_open = not args.short_open
    try:
        board_ids = resolve_board_ids(args)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    args.board_ids = board_ids
    if args.cycles <= 0 or args.cycles > 65536 or args.cycles % 8:
        raise SystemExit("cycles must be an 8-cycle multiple in [8, 65536]")
    if (args.train_chunk_cycles < 0 or
            (args.train_chunk_cycles != 0 and
             (args.train_chunk_cycles > args.cycles or
              args.train_chunk_cycles % 8 != 0))):
        raise SystemExit(
            "train-chunk-cycles must be 0 or an 8-cycle multiple not greater "
            "than cycles")
    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    if args.expected_build:
        for board in boards.values():
            if board.build != args.expected_build:
                raise SystemExit(
                    f"{board.address}: build {board.build} != {args.expected_build}")

    ordered = [boards[address] for address in board_ids]
    reference = ordered[0]
    start_order = ordered[1:] + [reference]
    node_count = len(ordered)
    result: dict[str, object] = {
        "reference_id": reference.address,
        "board_ids": board_ids,
        "node_count": node_count,
        "cycles": args.cycles,
        "train_chunk_cycles": args.train_chunk_cycles or args.cycles,
        "boards": {address: asdict(board) for address, board in boards.items()},
        "sequence": [
            "STOP all",
            f"TOPOLOGY node_count={node_count} slots=0..{node_count - 1} reference=0",
            "ARM all", f"TRAIN all={args.cycles}",
            "START forward slots", "START reference",
        ],
    }
    print(json.dumps(result, indent=2))
    if args.dry_run:
        return 0

    acknowledgements: list[dict[str, str]] = []
    for board in ordered:
        acknowledgements.append({board.address: board_command(
            board, "SYSTem:TDMA:RING:STOP", args)})
    if args.level is not None:
        for board in ordered:
            acknowledgements.append({board.address: board_command(
                board, f"SYSTem:TDMA:OPMode:STAGe {args.level}", args)})
            acknowledgements.append({board.address: board_command(
                board, "SYSTem:TDMA:OPMode:APPLy", args)})
            active = board_command(board, "SYSTem:TDMA:OPMode?", args)
            active_values = [int(value.strip().strip('"'), 0)
                             for value in active.split(",")]
            if not active_values or active_values[0] != args.level:
                raise RuntimeError(
                    f"{board.address}: active operating level is {active}")
    for slot, board in enumerate(ordered):
        acknowledgements.append({board.address: board_command(
            board,
            f"SYSTem:TDMA:RING:TOPology {node_count},{slot},0",
            args)})

    for board in start_order:
        acknowledgements.append({board.address: board_command(
            board, "SYSTem:TDMA:RING:ARM", args)})
    armed = {board.address: wait_started(board, args)
             for board in start_order}

    for board in start_order:
        acknowledgements.append({board.address: train(board, args)})
    before_start = {board.address: status(board, args) for board in ordered}
    for board in start_order:
        acknowledgements.append({board.address: board_command(
            board, "SYSTem:TDMA:RING:START", args)})

    time.sleep(args.start_wait)
    final = {board.address: status(board, args)
             for board in ordered}
    counter_deltas = {
        board.address: {
            "tx": ((final[board.address]["ring_adapter_tx_count"] -
                    before_start[board.address]["ring_adapter_tx_count"])
                   & 0xFFFFFFFF),
            "rx": ((final[board.address]["ring_adapter_rx_count"] -
                    before_start[board.address]["ring_adapter_rx_count"])
                   & 0xFFFFFFFF),
        }
        for board in ordered
    }
    passed = (
        all(final[board.address]["ring_node_count"] == node_count
            for board in ordered)
        and all(final[board.address]["ring_local_slot_id"] == slot
                for slot, board in enumerate(ordered))
        and all(item["ring_reference_slot_id"] == 0 for item in final.values())
        and all(item["ring_adapter_started"] == 1 for item in final.values())
        and all(item["ring_up_running"] == 1 for item in final.values())
        and all(item["ring_down_running"] == 1 for item in final.values())
        and all(item["tx"] > 0 for item in counter_deltas.values())
        and all(item["rx"] > 0 for item in counter_deltas.values())
    )
    result.update({"passed": passed,
                   "slot_map": [{"no": slot + 1, "address": board.address}
                                for slot, board in enumerate(ordered)],
                   "acknowledgements": acknowledgements,
                   "armed": armed, "before_start": before_start,
                   "counter_deltas": counter_deltas, "final": final})
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
