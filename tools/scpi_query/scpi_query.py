#!/usr/bin/env python3
"""Send a small batch of SCPI commands to an RP2350_TRIG USB CDC port.

The tool opens the serial port once, filters diagnostic log lines that share the
CDC stream, prints one response per command, and closes the port before exit.
"""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM4")
    parser.add_argument("commands", nargs="*", help="SCPI commands to send")
    parser.add_argument("--baud", type=int, default=115200, help="ignored by USB CDC on most hosts")
    parser.add_argument("--timeout", type=float, default=2.0, help="per-command response timeout")
    parser.add_argument("--settle", type=float, default=0.5, help="seconds to wait after opening the port")
    parser.add_argument("--cmd-file", type=Path,
                        help="UTF-8 text file with one SCPI command per line; # starts a comment")
    parser.add_argument("--out", type=Path, help="write a UTF-8 transcript")
    parser.add_argument("--json", action="store_true", help="print JSON records instead of text lines")
    return parser.parse_args()


def load_commands(args: argparse.Namespace) -> list[str]:
    commands = list(args.commands)
    if args.cmd_file is not None:
        for raw in args.cmd_file.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if line and not line.startswith("#"):
                commands.append(line)
    if not commands:
        raise SystemExit("no SCPI commands provided")
    return commands


def normalize_line(line: str) -> str:
    return line.strip()


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def strip_leading_ack(line: str) -> str:
    if line.startswith('"OK"'):
        return line[4:].strip()
    if line.startswith('OK"'):
        return line[3:].strip()
    return line


def read_response(ser: serial.Serial, command: str, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    is_query = command.strip().endswith("?")

    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = normalize_line(raw.decode("utf-8", errors="replace"))
        if is_log_line(line):
            continue

        if is_query:
            line = strip_leading_ack(line)
            if not line:
                continue
            return line

        if line.startswith('"OK"') or line.startswith('OK"'):
            return '"OK"'
        return line

    return "<timeout>"


def send_command(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_response(ser, command, timeout_s)


def write_transcript(path: Path, records: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# scpi_query transcript {datetime.now().isoformat(timespec='seconds')}\n")
        for record in records:
            handle.write(f"> {record['command']}\n")
            handle.write(f"< {record['response']}\n")


def main() -> int:
    args = parse_args()
    commands = load_commands(args)
    records: list[dict[str, str]] = []

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        time.sleep(args.settle)
        for command in commands:
            response = send_command(ser, command, args.timeout)
            record = {"command": command, "response": response}
            records.append(record)
            if args.json:
                print(json.dumps(record, ensure_ascii=False))
            else:
                print(f"{command} => {response}")

    if args.out is not None:
        write_transcript(args.out, records)

    return 0 if all(record["response"] != "<timeout>" for record in records) else 1


if __name__ == "__main__":
    raise SystemExit(main())
