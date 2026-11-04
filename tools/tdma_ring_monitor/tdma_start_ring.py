#!/usr/bin/env python3
"""Stage, clock-train, and start a 2..8-board TDMA SPI ring by *IDN? address."""

from __future__ import annotations

import argparse
import atexit
import csv
import json
import os
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
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tools.common.scpi.board_identity import parse_idn_response  # noqa: E402
from tools.common.scpi.scpi_serial import (  # noqa: E402
    COMPOSITE_ACK_HEADERS,
    SERIAL_LIFECYCLE_PHASE,
    TDMA_CONTROL_RESULT_FIELDS,
    SerialSession,
    is_scpi_query,
    read_scpi_response,
    serial_lifecycle_mode,
)
from tdma_field_parse import FIELDS as TDMA_FIELDS  # noqa: E402


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
        self.session: SerialSession | None = None

    def open(self) -> "BoardConnection":
        if self.session is not None:
            return self
        self.session = SerialSession(
            self.board.port, self.args.baud, self.args.timeout,
            self.args.settle, read_timeout_s=serial_read_timeout(self.args))
        self.session.open()
        identity = parse_idn_response(self.session.execute("*IDN?"))
        if identity.address != self.board.address:
            self.close()
            raise RuntimeError(
                f"{self.board.port}: identity changed to {identity.address}, "
                f"expected {self.board.address}")
        return self

    def close(self) -> None:
        if self.session is None:
            return
        self.session.close()
        self.session = None

    def command(self, text: str) -> str:
        self.open()
        assert self.session is not None and self.session.ser is not None
        # Drop any residue from the previous exchange before writing. A late
        # error-queue reply (`0,"No error"`) otherwise merges with this
        # command's own header echo, and the acceptance staging path then reads
        # the joined text as a rejected command instead of a valid reply. The
        # short-open path already flushes for the same reason.
        try:
            self.session.ser.reset_input_buffer()
        except Exception:  # noqa: BLE001 - a flush must never mask the command
            pass
        response = _board_command_on_serial(
            self.board, text, self.args, self.session.ser)
        # A single SCPI exchange can be answered in more than one message, and
        # the remainder may arrive after the terminator was consumed. Left in
        # the buffer it prefixes the next reply (observed as an error-queue
        # answer read as `2,10,"No error"`, which fails the "first field is 0"
        # check). Quiesce the line before returning so each command owns a
        # clean input buffer.
        self._quiesce()
        # A software reset invalidates the CDC session.  Release it here so
        # the next command performs a fresh discovery/open handshake.
        action = text.strip().split(maxsplit=1)[0].upper()
        if action in {"SYSTEM:BOOT:RESET", "SYST:BOOT:RESET"}:
            self.close()
        return response

    def _quiesce(self) -> None:
        """Discard bytes that arrive after a response's own terminator.

        A late remainder can land after the terminator was consumed and even
        after a first empty check, so this exits only after a quiet window: any
        byte seen resets the window. A transport without ``in_waiting`` (host
        fakes) pays nothing.
        """
        assert self.session is not None and self.session.ser is not None
        ser = self.session.ser
        if not hasattr(ser, "in_waiting"):
            return
        quiet_s = 0.02
        deadline = time.monotonic() + quiet_s
        while True:
            if time.monotonic() >= deadline:
                return
            try:
                waiting = int(ser.in_waiting)
            except Exception:  # noqa: BLE001 - transport without a depth read
                return
            if waiting <= 0:
                time.sleep(0.005)
                continue
            try:
                ser.read(waiting)
            except Exception:  # noqa: BLE001 - never mask the command result
                return
            deadline = time.monotonic() + quiet_s


_PERSISTENT_CONNECTIONS: dict[str, BoardConnection] = {}


def close_persistent_connections() -> None:
    for connection in list(_PERSISTENT_CONNECTIONS.values()):
        connection.close()
    _PERSISTENT_CONNECTIONS.clear()


atexit.register(close_persistent_connections)


def persistent_sessions_enabled(args: argparse.Namespace) -> bool:
    """Return whether this command owns a reusable CDC session.

    The acceptance orchestrator runs each phase in its own process and sets
    the environment contract below. This lets older calibration tools which
    do not expose a ``--keep-open`` flag still reuse one session per board,
    while ``--short-open`` remains an explicit diagnostic override.
    """
    if bool(getattr(args, "short_open", False)):
        return False
    return bool(getattr(args, "keep_open", False)) or \
        serial_lifecycle_mode() == SERIAL_LIFECYCLE_PHASE


def serial_read_timeout(args: argparse.Namespace) -> float:
    """Resolve the bounded serial read quantum used by SCPI response reads."""
    value = getattr(args, "read_timeout", None)
    if value is None:
        raw = os.environ.get("HAOFV_SERIAL_READ_TIMEOUT_S")
        value = float(raw) if raw else 0.02
    if value <= 0.0:
        raise ValueError("serial read timeout must be > 0")
    return min(float(value), float(getattr(args, "timeout", value)))


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
    parser.add_argument("--read-timeout", type=float, default=0.02,
                        help="bounded serial read quantum used by SCPI reads")
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--start-wait", type=float, default=2.0)
    parser.add_argument("--status-poll-interval", type=float, default=0.05,
                        help="fixed interval used by ARM/TRAIN state readback")
    parser.add_argument("--keep-open", action="store_true",
                        help="reuse one CDC session for this ring transaction")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--short-open", action="store_true",
                        help="open/close CDC for every command (diagnostic fallback)")
    parser.add_argument("--bringup", action="store_true",
                        help=("STOP-only no-calibration ARM authorization: set "
                              "RING:DIAGnostic 1,0,1 on every board before "
                              "topology/ARM"))
    parser.add_argument("--skip-train", action="store_true",
                        help=("skip the clock-training pass; required for the "
                              "10 kHz bring-up, which uses fixed phase 2..31"))
    parser.add_argument("--process-image", action="store_true",
                        help=("select the process-image forwarding mode "
                              "(SYST:TDMA:FLIGHT:MODE 1) before ARM"))
    parser.add_argument("--list", action="store_true",
                        help="print the numbered step table and exit")
    parser.add_argument("--step", type=int,
                        help="run exactly one numbered step")
    parser.add_argument("--steps",
                        help="step selection, e.g. '1-3' or '0,2,5-7' or 'all'")
    parser.add_argument("--full", action="store_true",
                        help="run the continuous chain (default when no step is selected)")
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
        with serial.Serial(port, args.baud,
                           timeout=serial_read_timeout(args),
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
    # Most action commands return a bare OK. The shared reader consumes that
    # ACK in the same transaction. START must preserve an unknown outcome:
    # quiet acquisition callers cannot perform a RUN state query to verify it.
    action = text.strip().split(maxsplit=1)[0].upper()
    ack_only_actions = {
        "SYSTEM:TDMA:RING:STOP", "SYST:TDMA:RING:STOP",
        "SYSTEM:TDMA:RING:ARM", "SYST:TDMA:RING:ARM",
        "SYSTEM:TDMA:RING:START", "SYST:TDMA:RING:START",
        "CALIBRATION:TOPOLOGY:PROBE",
        "CALIBRATION:TRAINING:STAGE:CLEAR",
        "CAL:TRAINING:STAGE:CLEAR",
        "CALIBRATION:LOOPBACK:STOP",
        "CALIBRATION:CLOCK:CODED:START",
        "CALIBRATION:CLOCK:CODED:STOP",
        "CALIBRATION:P3:START", "CALIBRATION:P3:STOP",
        "CALIBRATION:MARKER:ARM", "CALIBRATION:MARKER:INJECT",
        "CALIBRATION:MARKER:STOP",
        "CALIBRATION:DATA:ARM", "CALIBRATION:DATA:INJECT",
        "CALIBRATION:DATA:STOP",
        "CALIBRATION:SCK:ARM", "CALIBRATION:SCK:INJECT",
        "CALIBRATION:SCK:STOP",
        "SYSTEM:BOOT:RESET", "SYST:BOOT:RESET",
    }
    action_timeout = min(
        float(getattr(args, "action_timeout", args.timeout)),
        float(args.timeout),
    )
    result_write = action in COMPOSITE_ACK_HEADERS or action in TDMA_CONTROL_RESULT_FIELDS
    # The capture latch is a value-returning write, but it is not a timing-critical
    # ACK: forcing it onto the 0.2 s action timeout made trn03 report a spurious
    # "<timeout>" while a fresh connection returned generation/epoch immediately.
    result_write = result_write or action in {
        "CALIBRATION:RING:CAPTURE:LATCH", "CAL:RING:CAPTURE:LATCH",
        "CALIBRATION:RING:CAPTURE:SAVE", "CAL:RING:CAPTURE:SAVE"}
    ordinary_write = not is_scpi_query(text) and not result_write
    try:
        response = command(
            ser, text,
            action_timeout
            if not result_write and (action in ack_only_actions or ordinary_write)
            else args.timeout)
    except (OSError, serial.SerialException):
        if action not in {"SYSTEM:BOOT:RESET", "SYST:BOOT:RESET"}:
            raise
        # The watchdog reset is expected to invalidate USB before an ACK can
        # be read. Treat that transport loss as the successful reset handoff.
        return "OK(disconnected for software reset)"
    if response == "<timeout>" and action in ack_only_actions and not result_write:
        if action in {"SYSTEM:TDMA:RING:START", "SYST:TDMA:RING:START"}:
            # A firmware admission rejection also has no OK payload. Do not
            # manufacture success, retry START, or query a potentially live ring.
            # The caller must STOP and retain the failed lifetime.
            return response
        return "OK(no payload; verified by state readback)"
    return response


def board_command(board: Board, text: str, args: argparse.Namespace) -> str:
    # Persistent sessions are the deterministic default.  Existing tools can
    # opt into the slower diagnostic behavior with --short-open.
    keep_open = persistent_sessions_enabled(args)
    if keep_open:
        connection = _PERSISTENT_CONNECTIONS.get(board.address)
        if connection is None:
            connection = BoardConnection(board, args)
            _PERSISTENT_CONNECTIONS[board.address] = connection
        return connection.command(text)
    with serial.Serial(board.port, args.baud,
                       timeout=serial_read_timeout(args),
                       write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
        if identity.address != board.address:
            raise RuntimeError(
                f"{board.port}: identity changed to {identity.address}, "
                f"expected {board.address}")
        return _board_command_on_serial(board, text, args, ser)


def status(board: Board, args: argparse.Namespace) -> dict[str, int]:
    query = "SYSTem:REFMEM:SYNC:TDMA:STATus?"
    raw = board_command(board, query, args)
    if raw == "<timeout>" and persistent_sessions_enabled(args):
        # A stale persistent CDC read is a recoverable transport observation,
        # not a TDMA safety fault.  Bound recovery to one fresh short-open
        # query; callers still receive the timeout if the fresh session also
        # fails, preserving the original state-machine gate.
        close_persistent_connections()
        fallback_args = argparse.Namespace(**vars(args))
        fallback_args.keep_open = False
        fallback_args.short_open = True
        raw = board_command(board, query, fallback_args)
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


def wait_started(board: Board, args: argparse.Namespace) -> dict[str, int]:
    deadline = time.monotonic() + args.arm_wait
    last: dict[str, int] = {}
    last_error = ""
    while time.monotonic() < deadline:
        try:
            last = status(board, args)
        except (OSError, RuntimeError, serial.SerialException) as exc:
            last_error = str(exc)
            time.sleep(getattr(args, "status_poll_interval", 0.05))
            continue
        if last["ring_enabled"] == 1 and last["ring_adapter_started"] == 1:
            return last
        time.sleep(getattr(args, "status_poll_interval", 0.05))
    raise RuntimeError(
        f"{board.address}: ARM timeout, last={last}, last_error={last_error}")


def train(board: Board, args: argparse.Namespace) -> dict[str, object]:
    chunk_cycles = args.train_chunk_cycles or args.cycles
    remaining = args.cycles
    responses: list[str] = []
    completed_cycles = 0
    # Use the same persistent session as the surrounding ring transaction.
    # Re-opening here added a second USB settle/IDN handshake and could leave
    # two handles racing during the ARM -> TRAIN handoff.
    poll_interval = getattr(args, "status_poll_interval", 0.05)
    while remaining > 0:
        current = min(chunk_cycles, remaining)
        before_raw = board_command(
            board, "SYSTem:TDMA:RING:TRAIN:STATus?", args)
        before = next(csv.reader([before_raw]), [])
        if len(before) != 28 or before[0].strip().strip('"') != "CLKTRAIN":
            raise RuntimeError(
                f"{board.address}: invalid pre-TRAIN status {before_raw!r}")
        previous_request_seq = int(before[5].strip().strip('"'), 0)
        response = ""
        # A whole training chunk can take longer than the generic read timeout
        # (a 4096-cycle chunk returning "<timeout>" was observed at both 1 ms
        # and 750 us slots, so the timeout - not the slot - was the suspect).
        train_args = argparse.Namespace(**vars(args))
        train_args.timeout = max(
            float(getattr(args, "timeout", 2.0) or 2.0), 20.0)
        for _ in range(3):
            response = board_command(
                board, f"SYSTem:TDMA:RING:TRAIN {current}", train_args)
            responses.append(response)
            if response.strip().strip('"') == str(current):
                break
            time.sleep(poll_interval)
        if response.strip().strip('"') != str(current):
            raise RuntimeError(
                f"{board.address}: TRAIN chunk {current} failed after "
                f"{completed_cycles}/{args.cycles} cycles, response={response!r}")
        deadline = time.monotonic() + args.timeout
        train_snapshot: list[str] = []
        while time.monotonic() < deadline:
            train_raw = board_command(
                board, "SYSTem:TDMA:RING:TRAIN:STATus?", args)
            train_snapshot = next(csv.reader([train_raw]), [])
            if (len(train_snapshot) == 28 and
                    train_snapshot[0].strip().strip('"') == "CLKTRAIN"):
                state = int(train_snapshot[2].strip().strip('"'), 0)
                request_seq = int(train_snapshot[5].strip().strip('"'), 0)
                if request_seq != previous_request_seq and state in (1, 3, 4):
                    if state == 4:
                        raise RuntimeError(
                            f"{board.address}: TRAIN {current} entered ERROR: "
                            f"{train_raw}")
                    break
            time.sleep(poll_interval)
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


STEP_TITLES = {
    0: "discover and verify boards/build (--dry-run stops here)",
    1: "STOP all boards",
    2: "STAGE/APPLY operating level (--level)",
    3: "write TOPOLOGY (node_count / slot / reference)",
    4: "set RING:BRINGup (STOP-only; --bringup)",
    5: "clock training (skipped by --skip-train)",
    6: "ARM all boards (followers first, reference last)",
    7: "START all boards (forward slots first, reference last)",
    8: "wait for adapter_started, verify up/down/counters",
    9: "write summary.json and verdict",
}
STEP_FUNCTIONS = {}


def step(number):
    def register(function):
        STEP_FUNCTIONS[number] = function
        return function
    return register


class Context:
    def __init__(self, args):
        self.args = args
        self.acks = []
        self.records = []
        self.failures = []
        self.discovered = False
        self.dry_run = False
        self.passed = False
        self.result = {}
        self.before_start = {}
        self.final = {}
        self.boards = {}
        self.ordered = []
        self.start_order = []
        self.node_count = 0
        self.out_dir = args.out_dir or (
            ROOT / "build-rtos-multicore-smoke" /
            f"tdma_start_ring_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
        self.out_dir.mkdir(parents=True, exist_ok=True)

    def log(self, message):
        print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)

    def ack(self, board, response):
        self.acks.append({board.address: response})


def parse_selection(args):
    if args.step is not None:
        return [args.step]
    if args.steps:
        selected = []
        for part in args.steps.split(","):
            part = part.strip()
            if not part:
                continue
            if "-" in part:
                first, last = part.split("-", 1)
                selected.extend(range(int(first), int(last) + 1))
            else:
                selected.append(int(part))
        return sorted(set(selected))
    return list(range(10))


@step(0)
def step_discover(ctx):
    args = ctx.args
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
    if args.status_poll_interval <= 0.0:
        raise SystemExit("status-poll-interval must be > 0")
    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    if args.expected_build:
        for board in boards.values():
            if board.build != args.expected_build:
                raise SystemExit(
                    f"{board.address}: build {board.build} != {args.expected_build}")
    ctx.boards = boards
    ctx.ordered = [boards[address] for address in board_ids]
    ctx.start_order = ctx.ordered[1:] + [ctx.ordered[0]]
    ctx.node_count = len(ctx.ordered)
    ctx.result = {
        "reference_id": ctx.ordered[0].address,
        "board_ids": board_ids,
        "node_count": ctx.node_count,
        "cycles": args.cycles,
        "train_chunk_cycles": args.train_chunk_cycles or args.cycles,
        "boards": {address: asdict(board) for address, board in boards.items()},
        "sequence": [
            "STOP all",
            f"TOPOLOGY node_count={ctx.node_count} slots=0..{ctx.node_count - 1} reference=0",
            "TRAIN all=%d (while STOPPED)" % args.cycles,
            "ARM all", "START forward slots", "START reference",
            "wait for adapter_started (after START)",
        ],
    }
    ctx.log(json.dumps(ctx.result, indent=2))
    if args.dry_run:
        ctx.dry_run = True
    ctx.discovered = True
    return True


def ensure_discovered(ctx):
    if not ctx.discovered:
        step_discover(ctx)


@step(1)
def step_stop(ctx):
    ensure_discovered(ctx)
    for board in ctx.ordered:
        ctx.ack(board, board_command(board, "SYSTem:TDMA:RING:STOP", ctx.args))
    return True


@step(2)
def step_stage(ctx):
    ensure_discovered(ctx)
    if ctx.args.level is None:
        return True
    for board in ctx.ordered:
        ctx.ack(board, board_command(
            board, f"SYSTem:TDMA:OPMode:STAGe {ctx.args.level}", ctx.args))
        # The first APPLy right after STAGe is often rejected while the
        # previous configuration is still retiring, so retry before
        # concluding that the level cannot be activated.
        active = board_command(board, "SYSTem:TDMA:OPMode?", ctx.args)
        for _ in range(4):
            applied = board_command(
                board, "SYSTem:TDMA:OPMode:APPLy", ctx.args)
            active = board_command(board, "SYSTem:TDMA:OPMode?", ctx.args)
            active_values = [int(value.strip().strip('"'), 0)
                             for value in active.split(",")]
            if active_values and active_values[0] == ctx.args.level:
                break
            time.sleep(0.5)
        ctx.ack(board, applied)
        active_values = [int(value.strip().strip('"'), 0)
                         for value in active.split(",")]
        if not active_values or active_values[0] != ctx.args.level:
            raise RuntimeError(
                f"{board.address}: active operating level is {active}")
    return True


@step(3)
def step_topology(ctx):
    ensure_discovered(ctx)
    for slot, board in enumerate(ctx.ordered):
        ctx.ack(board, board_command(
            board,
            f"SYSTem:TDMA:RING:TOPology {ctx.node_count},{slot},0",
            ctx.args))
    if ctx.args.process_image:
        # 1 = process-image forwarding (adapter mode 2); 0 = physical flight.
        for board in ctx.ordered:
            ctx.ack(board, board_command(
                board, "SYSTem:TDMA:FLIGHT:MODE 1", ctx.args))
    return True


@step(4)
def step_bringup(ctx):
    ensure_discovered(ctx)
    if not ctx.args.bringup:
        return True
    for board in ctx.ordered:
        # TOPOLOGY rebuilds the staged config, so the bring-up bit must be
        # set after it and immediately before ARM. APPLY is asynchronous:
        # retry until the readback shows the bit.
        readback = "0"
        for _ in range(8):
            set_response = board_command(
                board, "SYSTem:TDMA:RING:BRINGup 1", ctx.args)
            readback = set_response
            try:
                enabled = int(set_response.strip().strip('"'), 0)
            except ValueError:
                enabled = 0
            if enabled == 1:
                break
            time.sleep(0.2)
            readback = board_command(
                board, "SYSTem:TDMA:RING:BRINGup?", ctx.args)
            try:
                enabled = int(readback.strip().strip('"'), 0)
            except ValueError:
                enabled = 0
            if enabled == 1:
                break
            time.sleep(0.5)
        ctx.ack(board, readback)
        if enabled != 1:
            raise RuntimeError(f"{board.address}: BRINGup readback {readback}")
    return True


@step(5)
def step_train(ctx):
    ensure_discovered(ctx)
    if ctx.args.skip_train:
        return True
    # Clock training drives a single in-board trial and the documented flow
    # trains while the ring is STOPPED, so this pass must precede ARM/START.
    for board in ctx.start_order:
        ctx.ack(board, train(board, ctx.args))
    return True


@step(6)
def step_arm(ctx):
    ensure_discovered(ctx)
    for board in ctx.start_order:
        ctx.ack(board, board_command(board, "SYSTem:TDMA:RING:ARM", ctx.args))
    return True


@step(7)
def step_start(ctx):
    ensure_discovered(ctx)
    ctx.before_start = {board.address: status(board, ctx.args)
                        for board in ctx.ordered}
    for board in ctx.start_order:
        ctx.ack(board, board_command(board, "SYSTem:TDMA:RING:START", ctx.args))
    return True


@step(8)
def step_wait(ctx):
    ensure_discovered(ctx)
    if not ctx.before_start:
        ctx.before_start = {board.address: status(board, ctx.args)
                            for board in ctx.ordered}
    # The adapter only starts once START has been issued, so the start barrier
    # has to be awaited here.
    armed = {board.address: wait_started(board, ctx.args)
             for board in ctx.start_order}
    time.sleep(ctx.args.start_wait)
    final = {board.address: status(board, ctx.args)
             for board in ctx.ordered}
    counter_deltas = {
        board.address: {
            "tx": ((final[board.address]["ring_adapter_tx_count"] -
                    ctx.before_start[board.address]["ring_adapter_tx_count"])
                   & 0xFFFFFFFF),
            "rx": ((final[board.address]["ring_adapter_rx_count"] -
                    ctx.before_start[board.address]["ring_adapter_rx_count"])
                   & 0xFFFFFFFF),
        }
        for board in ctx.ordered
    }
    passed = (
        all(final[board.address]["ring_node_count"] == ctx.node_count
            for board in ctx.ordered)
        and all(final[board.address]["ring_local_slot_id"] == slot
                for slot, board in enumerate(ctx.ordered))
        and all(item["ring_reference_slot_id"] == 0 for item in final.values())
        and all(item["ring_adapter_started"] == 1 for item in final.values())
        and all(item["ring_up_running"] == 1 for item in final.values())
        and all(item["ring_down_running"] == 1 for item in final.values())
        and all(item["tx"] > 0 for item in counter_deltas.values())
        and all(item["rx"] > 0 for item in counter_deltas.values())
    )
    ctx.final = final
    ctx.passed = passed
    ctx.result.update({
        "passed": passed,
        "slot_map": [{"no": slot + 1, "address": board.address}
                     for slot, board in enumerate(ctx.ordered)],
        "acknowledgements": ctx.acks,
        "armed": armed, "before_start": ctx.before_start,
        "counter_deltas": counter_deltas, "final": final,
    })
    return passed


def write_summary(ctx):
    ctx.result.update({
        "steps": ctx.records,
        "failures": ctx.failures,
        "passed": ctx.passed,
        "out_dir": str(ctx.out_dir),
    })
    path = ctx.out_dir / "summary.json"
    path.write_text(json.dumps(ctx.result, indent=2), encoding="utf-8")
    ctx.log(json.dumps(ctx.result, indent=2))
    ctx.log(f"out_dir={ctx.out_dir}")
    return path


@step(9)
def step_summary(ctx):
    # The summary is written once after the loop so it includes step 9's own
    # record and, on failure, the failing step's record as well.
    return ctx.passed


def main() -> int:
    args = parse_args()
    # The ring transaction owns all board I/O, so it can safely reuse the
    # sessions. Imported helpers must opt in instead.
    args.keep_open = not args.short_open
    if args.list:
        for number in sorted(STEP_TITLES):
            print(f"{number:2d}  {STEP_TITLES[number]}")
        return 0
    selection = parse_selection(args)
    unknown = [number for number in selection if number not in STEP_TITLES]
    if unknown:
        raise SystemExit(f"unknown step(s): {unknown}")
    ctx = Context(args)
    try:
        for number in selection:
            if ctx.dry_run and number != 0:
                break
            if number != 0:
                ensure_discovered(ctx)
            started = time.monotonic()
            ctx.log(f"=== Step {number} START  {STEP_TITLES[number]}")
            try:
                ok = bool(STEP_FUNCTIONS[number](ctx))
            except Exception as exc:                     # noqa: BLE001
                ok = False
                ctx.failures.append({"step": number, "error": str(exc)})
                ctx.log(f"  FAIL {type(exc).__name__}: {exc}")
            duration = time.monotonic() - started
            ctx.records.append({
                "step": number, "title": STEP_TITLES[number],
                "ok": ok, "duration_s": round(duration, 3),
            })
            ctx.log(f"=== Step {number} END    {'OK' if ok else 'FAIL'}  "
                    f"{duration:.1f}s")
            if not ok:
                break
    finally:
        write_summary(ctx)
    if ctx.dry_run:
        return 0
    if any(record["step"] == 8 for record in ctx.records):
        return 0 if ctx.passed else 1
    return 0 if all(record["ok"] for record in ctx.records) else 1


if __name__ == "__main__":
    raise SystemExit(main())
