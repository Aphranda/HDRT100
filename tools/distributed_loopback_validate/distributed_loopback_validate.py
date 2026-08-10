#!/usr/bin/env python3
"""Validate the five-board distributed loopback bench over SCPI USB CDC.

Default topology:
- 5 physical RP2350_TRIG boards.
- One simulator board represents both turntable and VNA.
- Remaining boards are distributed trigger nodes, typically A0..A3.

The simulator may be named SIM or a normal node name such as A4:

  python tools/distributed_loopback_validate/distributed_loopback_validate.py \
      --board A0=COM5 --board A1=COM6 --board A2=COM7 --board A3=COM8 \
      --board SIM=COM9 --sim-role SIM --dry-run

  python tools/distributed_loopback_validate/distributed_loopback_validate.py \
      --board A0=COM5 --board A1=COM6 --board A2=COM7 --board A3=COM8 \
      --board A4=COM9 --sim-role A4

Current scope is bench preflight: topology validation plus per-board SCPI health
queries. Real RJ45 REFMEM_DELTA/FIRE_LOAD/T2 loop closure will be added after
the firmware protocol exists.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass, asdict
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TESTS = (
    "*IDN?",
    "SYST:FW:BUILD?",
    "SYST:CORE?",
    "SYST:CFG:STAT?",
    "SYST:CFG:ACK?",
    "SYST:MODE:TAB? 1",
    "SYST:RESource:TAB? 0",
    "SYST:FAULT:TAB? 0",
)
KNOWN_NODE_ROLES = {"A0", "A1", "A2", "A3", "A4", "SIM"}


@dataclass(frozen=True)
class BoardSpec:
    role: str
    port: str
    simulator: bool


def normalize_line(line: str) -> str:
    return line.strip()


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def trim_embedded_log(line: str) -> str:
    match = re.search(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+', line)
    if match is None:
        return line
    return line[:match.start()].strip()


def strip_leading_ack(line: str) -> str:
    if line.startswith('"OK[') or line.startswith("OK["):
        return ""
    if line.startswith('"OK"'):
        return line[4:].strip()
    if line.startswith('OK"'):
        return line[3:].strip()
    return line


def read_serial_line(ser: serial.Serial, deadline: float) -> str | None:
    raw = bytearray()
    while time.monotonic() < deadline:
        ch = ser.read(1)
        if not ch:
            continue
        raw.extend(ch)
        if ch == b"\n":
            break
    if not raw:
        return None
    return normalize_line(bytes(raw).decode("utf-8", errors="replace"))


def read_response(ser: serial.Serial, command: str, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    is_query = command.strip().endswith("?")

    while time.monotonic() < deadline:
        line = read_serial_line(ser, deadline)
        if line is None or is_log_line(line):
            continue

        if is_query:
            line = strip_leading_ack(trim_embedded_log(line))
            if line:
                return line
            continue

        if line.startswith('"OK"') or line.startswith('OK"') or line.startswith('"OK[') or line.startswith("OK["):
            return '"OK"'
        return line

    return "<timeout>"


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_response(ser, command, timeout_s)


def parse_csv_ints(response: str) -> list[int]:
    out: list[int] = []
    for part in response.split(","):
        part = part.strip().strip('"')
        try:
            out.append(int(part, 0))
        except ValueError:
            pass
    return out


def parse_board(value: str) -> tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("board must use ROLE=PORT syntax")
    role, port = value.split("=", 1)
    role = role.strip().upper()
    port = port.strip()
    if not role or not port:
        raise argparse.ArgumentTypeError("board role and port must be non-empty")
    if role not in KNOWN_NODE_ROLES:
        raise argparse.ArgumentTypeError(f"unsupported role {role!r}; expected one of {sorted(KNOWN_NODE_ROLES)}")
    return role, port


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", action="append", type=parse_board, required=True,
                        help="board mapping ROLE=PORT; repeat exactly five times")
    parser.add_argument("--sim-role", default="SIM",
                        help="role name of the board that simulates turntable and VNA")
    parser.add_argument("--expected-boards", type=int, default=5,
                        help="physical board count expected on the bench")
    parser.add_argument("--baud", type=int, default=115200,
                        help="ignored by USB CDC on most hosts")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="per-command timeout")
    parser.add_argument("--settle", type=float, default=1.5,
                        help="seconds to wait after opening each port")
    parser.add_argument("--out-dir", type=Path,
                        help="directory for JSON summary and transcript")
    parser.add_argument("--dry-run", action="store_true",
                        help="validate topology only; do not open serial ports")
    return parser.parse_args()


def build_specs(args: argparse.Namespace) -> list[BoardSpec]:
    sim_role = args.sim_role.strip().upper()
    role_to_port: dict[str, str] = {}
    port_to_role: dict[str, str] = {}

    if len(args.board) != args.expected_boards:
        raise SystemExit(f"expected {args.expected_boards} --board entries, got {len(args.board)}")

    for role, port in args.board:
        port_key = port.upper()
        if role in role_to_port:
            raise SystemExit(f"duplicate board role: {role}")
        if port_key in port_to_role:
            raise SystemExit(f"duplicate serial port: {port} used by {role} and {port_to_role[port_key]}")
        role_to_port[role] = port
        port_to_role[port_key] = role

    if sim_role not in role_to_port:
        raise SystemExit(f"--sim-role {sim_role} is not present in --board mappings")

    if args.expected_boards != 5:
        raise SystemExit("this script currently validates the five-board bench only")

    return [
        BoardSpec(role=role, port=port, simulator=(role == sim_role))
        for role, port in sorted(role_to_port.items())
    ]


def validate_topology(specs: list[BoardSpec]) -> list[str]:
    notes: list[str] = []
    sim_count = sum(1 for spec in specs if spec.simulator)
    if sim_count != 1:
        raise SystemExit(f"expected exactly one simulator board, got {sim_count}")

    simulator = next(spec for spec in specs if spec.simulator)
    notes.append(f"simulator={simulator.role} capabilities=turntable+vna")

    trigger_nodes = [spec.role for spec in specs if not spec.simulator]
    if len(trigger_nodes) != 4:
        raise SystemExit(f"expected four non-simulator trigger nodes, got {trigger_nodes}")
    notes.append(f"trigger_nodes={','.join(trigger_nodes)}")

    if "A0" not in {spec.role for spec in specs}:
        notes.append("warning=A0 missing; loop origin must be assigned before live RJ45 tests")
    return notes


def validate_board(spec: BoardSpec, args: argparse.Namespace) -> dict:
    records: list[dict[str, str]] = []
    passed = True
    with serial.Serial(spec.port, args.baud, timeout=0.2) as ser:
        time.sleep(args.settle)
        for command in DEFAULT_TESTS:
            response = query(ser, command, args.timeout)
            records.append({"command": command, "response": response})
            if response == "<timeout>":
                passed = False

    checks = {record["command"]: record["response"] for record in records}
    core = parse_csv_ints(checks.get("SYST:CORE?", ""))
    cfg = parse_csv_ints(checks.get("SYST:CFG:STAT?", ""))
    ack = parse_csv_ints(checks.get("SYST:CFG:ACK?", ""))
    mode = parse_csv_ints(checks.get("SYST:MODE:TAB? 1", ""))

    if len(core) < 5 or core[0] != 1:
        passed = False
    if len(cfg) < 24 or cfg[1] != 1:
        passed = False
    if len(ack) < 12 or ack[2] == 0 or ack[10] == 0:
        passed = False
    if len(mode) < 8 or mode[4] != 1 or mode[5] != 1:
        passed = False

    return {
        "role": spec.role,
        "port": spec.port,
        "simulator": spec.simulator,
        "passed": passed,
        "records": records,
    }


def write_outputs(out_dir: Path, summary: dict) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    summary_path = out_dir / "summary.json"
    transcript_path = out_dir / "scpi_log.txt"
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

    lines: list[str] = []
    for board in summary["boards"]:
        lines.append(f"[{board['role']} {board['port']}] {'PASS' if board['passed'] else 'FAIL'}")
        for record in board.get("records", []):
            lines.append(f"> {record['command']}")
            lines.append(f"< {record['response']}")
    transcript_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Summary: {summary_path}")
    print(f"Transcript: {transcript_path}")


def main() -> int:
    args = parse_args()
    specs = build_specs(args)
    notes = validate_topology(specs)

    if args.out_dir is not None:
        out_dir = args.out_dir.resolve()
    else:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_dir = ROOT / "build-distributed-loopback" / f"five_board_{ts}"

    print("Topology:")
    for spec in specs:
        marker = " simulator(turntable+vna)" if spec.simulator else ""
        print(f"  {spec.role}: {spec.port}{marker}")
    for note in notes:
        print(f"  {note}")

    if args.dry_run:
        summary = {
            "title": "Five-board distributed loopback preflight",
            "timestamp": datetime.now().isoformat(),
            "dry_run": True,
            "notes": notes,
            "boards": [asdict(spec) | {"passed": True, "records": []} for spec in specs],
            "overall": "PASS",
        }
        write_outputs(out_dir, summary)
        print("Result: topology PASS (dry-run)")
        return 0

    boards = [validate_board(spec, args) for spec in specs]
    passed = sum(1 for board in boards if board["passed"])
    total = len(boards)
    overall = "PASS" if passed == total else "FAIL"

    summary = {
        "title": "Five-board distributed loopback preflight",
        "timestamp": datetime.now().isoformat(),
        "dry_run": False,
        "notes": notes,
        "boards": boards,
        "passed": passed,
        "total": total,
        "overall": overall,
    }
    write_outputs(out_dir, summary)
    print(f"Result: {passed}/{total} boards passed")
    return 0 if overall == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
