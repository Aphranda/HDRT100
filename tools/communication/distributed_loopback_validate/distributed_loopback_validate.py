#!/usr/bin/env python3
"""Validate the BiSS network bench through the single A3 USB CDC control port.

Topology model:
- A3 is the only external COM/USB CDC entry point.
- A0, A1, A2 and A4 are logical peer roles on the internal BiSS network.
- A4 is the bench-side simulator role that represents turntable + VNA.

The current firmware still exposes the four-node distributed config map from A0
through A3, so the script only queries the A3 control board over SCPI and keeps
the peer roles as topology metadata.

Example:

  python tools/distributed_loopback_validate/distributed_loopback_validate.py \
      --a3-port COM5 \
      --peer A0 --peer A1 --peer A2 --peer A4 \
      --sim-role A4 --dry-run

Current scope is bench preflight: topology validation plus A3 SCPI health
queries. Real internal BiSSC frame closure will be added after the firmware
protocol exists.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
CONTROL_ROLE = "A3"
PEER_ROLES = ("A0", "A1", "A2", "A4")
ROLE_QUERY_IDS = (0, 1, 2, 3)
DEFAULT_TESTS = (
    "*IDN?",
    "SYST:FW:BUILD?",
    "SYST:CORE?",
    "SYST:CONFigure:STAT?",
    "SYST:CONFigure:ACK?",
    "SYST:CONFigure:ROLE? 0",
    "SYST:CONFigure:ROLE? 1",
    "SYST:CONFigure:ROLE? 2",
    "SYST:CONFigure:ROLE? 3",
    "SYST:MODE:TABle? 1",
    "SYST:RESource:TABle? 0",
    "SYST:FAULT:TABle? 0",
)


@dataclass(frozen=True)
class NodeSpec:
    role: str
    transport: str
    port: str | None
    simulator: bool


def normalize_line(line: str) -> str:
    return line.strip()


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def trim_embedded_log(line: str) -> str:
    match = re.search(r"(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+", line)
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


def parse_peer(value: str) -> str:
    role = value.strip().upper()
    if role not in PEER_ROLES:
        raise argparse.ArgumentTypeError(
            f"unsupported peer role {role!r}; expected one of {PEER_ROLES}"
        )
    return role


def parse_sim_role(value: str) -> str:
    role = value.strip().upper()
    if role not in PEER_ROLES:
        raise argparse.ArgumentTypeError(
            f"unsupported simulator role {role!r}; expected one of {PEER_ROLES}"
        )
    return role


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--a3-port", required=True, help="USB CDC serial port for A3 control board")
    parser.add_argument("--peer", action="append", type=parse_peer, required=True,
                        help="internal BiSS peer role; repeat exactly four times")
    parser.add_argument("--sim-role", default="A4",
                        type=parse_sim_role,
                        help="peer role that simulates turntable and VNA")
    parser.add_argument("--baud", type=int, default=115200,
                        help="ignored by USB CDC on most hosts")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="per-command timeout")
    parser.add_argument("--settle", type=float, default=1.5,
                        help="seconds to wait after opening the port")
    parser.add_argument("--out-dir", type=Path,
                        help="directory for JSON summary and transcript")
    parser.add_argument("--dry-run", action="store_true",
                        help="validate topology only; do not open serial ports")
    return parser.parse_args()


def build_topology(args: argparse.Namespace) -> list[NodeSpec]:
    peer_roles = [role.upper() for role in args.peer]
    if len(peer_roles) != 4:
        raise SystemExit(f"expected exactly four --peer entries, got {len(peer_roles)}")
    if len(set(peer_roles)) != 4:
        raise SystemExit(f"duplicate peer role in {peer_roles}")
    if set(peer_roles) != set(PEER_ROLES):
        raise SystemExit(f"expected peer roles {PEER_ROLES}, got {tuple(peer_roles)}")
    if args.sim_role not in peer_roles:
        raise SystemExit(f"--sim-role {args.sim_role} is not present in peer roles {peer_roles}")

    topology = [NodeSpec(role=CONTROL_ROLE, port=args.a3_port, transport="usbcdc", simulator=False)]
    for role in PEER_ROLES:
        topology.append(NodeSpec(role=role, port=None, transport="biss", simulator=(role == args.sim_role)))
    return topology


def validate_topology(specs: list[NodeSpec]) -> list[str]:
    notes: list[str] = []
    control = next(spec for spec in specs if spec.role == CONTROL_ROLE)
    notes.append(f"control={control.role} transport={control.transport} port={control.port}")

    peers = [spec.role for spec in specs if spec.role != CONTROL_ROLE]
    notes.append(f"peer_roles={','.join(peers)} transport=biss")

    simulator = next(spec for spec in specs if spec.simulator)
    notes.append(f"simulator={simulator.role} capabilities=turntable+vna")
    notes.append("firmware_role_map=A0..A3; A4 is bench-side simulator role")
    return notes


def validate_control_board(spec: NodeSpec, args: argparse.Namespace) -> dict:
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
    cfg = parse_csv_ints(checks.get("SYST:CONFigure:STAT?", ""))
    ack = parse_csv_ints(checks.get("SYST:CONFigure:ACK?", ""))
    role_rows = {
        idx: parse_csv_ints(checks.get(f"SYST:CONFigure:ROLE? {idx}", ""))
        for idx in ROLE_QUERY_IDS
    }

    if len(core) < 5 or core[0] != 1:
        passed = False
    if len(cfg) < 24 or cfg[1] != 1 or cfg[12] != 0x0F:
        passed = False
    if len(ack) < 12 or ack[2] != 0x0F or ack[10] == 0 or ack[11] == 0:
        passed = False

    for idx, fields in role_rows.items():
        if len(fields) < 8:
            passed = False
            continue
        if idx == 3 and (fields[6] != 3 or fields[7] != 3):
            passed = False

    return {
        "role": spec.role,
        "port": spec.port,
        "transport": spec.transport,
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
    for board in summary["nodes"]:
        port = board.get("port") or ""
        lines.append(f"[{board['role']} {board.get('transport', '')} {port}] "
                     f"{'PASS' if board['passed'] else 'FAIL'}")
        for record in board.get("records", []):
            lines.append(f"> {record['command']}")
            lines.append(f"< {record['response']}")
    transcript_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Summary: {summary_path}")
    print(f"Transcript: {transcript_path}")


def main() -> int:
    args = parse_args()
    topology = build_topology(args)
    notes = validate_topology(topology)

    if args.out_dir is not None:
        out_dir = args.out_dir.resolve()
    else:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_dir = ROOT / "build-biss-network" / f"preflight_{ts}"

    print("Topology:")
    for spec in topology:
        marker = " simulator(turntable+vna)" if spec.simulator else ""
        port_text = f" {spec.port}" if spec.port else ""
        print(f"  {spec.role}:{port_text} transport={spec.transport}{marker}")
    for note in notes:
        print(f"  {note}")

    if args.dry_run:
        summary = {
            "title": "BiSS network bench preflight",
            "timestamp": datetime.now().isoformat(),
            "dry_run": True,
            "notes": notes,
            "nodes": [asdict(spec) | {"passed": True, "records": []} for spec in topology],
            "overall": "PASS",
        }
        write_outputs(out_dir, summary)
        print("Result: topology PASS (dry-run)")
        return 0

    control = next(spec for spec in topology if spec.role == CONTROL_ROLE)
    node_result = validate_control_board(control, args)

    passed = 1 if node_result["passed"] else 0
    total = 1
    overall = "PASS" if passed == total else "FAIL"

    summary = {
        "title": "BiSS network bench preflight",
        "timestamp": datetime.now().isoformat(),
        "dry_run": False,
        "notes": notes,
        "nodes": [node_result] + [asdict(spec) | {"passed": True, "records": []} for spec in topology if spec.role != CONTROL_ROLE],
        "passed": passed,
        "total": total,
        "overall": overall,
    }
    write_outputs(out_dir, summary)
    print(f"Result: {passed}/{total} control boards passed")
    return 0 if overall == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
