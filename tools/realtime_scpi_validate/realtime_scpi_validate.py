"""Validate realtime maintenance SCPI aliases over USB CDC.

The product validator intentionally ignores low-level realtime validation
commands. This tool covers the REALtime:* maintenance domain and a minimal
legacy alias subset so command-tree normalization stays visible.
"""

from __future__ import annotations

import argparse
import csv
import re
import time
from dataclasses import dataclass
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
REALTIME_HEADERS = (
    Path("middleware/scpi_port/inc/scpi_realtime_io_commands.h"),
    Path("middleware/scpi_port/inc/scpi_realtime_sequence_commands.h"),
    Path("middleware/scpi_port/inc/scpi_realtime_encoder_commands.h"),
    Path("middleware/scpi_port/inc/scpi_realtime_pcnt_commands.h"),
    Path("middleware/scpi_port/inc/scpi_realtime_status_commands.h"),
)


@dataclass(frozen=True)
class TestCase:
    command: str
    min_fields: int
    expect_response: bool


COMMAND_ARGS = {
    "REALtime:IO:OUTPut:WIDTh": "25",
    "REALtime:IO:OUTPut:IMMediate": "",
    "REALtime:IO:OUTPut:MASK": "0",
    "REALtime:IO:OUTPut:RELease": "",
    "REALtime:IO:PULSe:WIDTh": "40",
    "REALtime:IO:PULSe:IMMediate": "",
    "REALtime:IO:MARKer:WIDTh": "55",
    "REALtime:IO:MARKer:IMMediate": "",
    "REALtime:IO:RJ45:WIDTh": "65",
    "REALtime:IO:RJ45:IMMediate": "",
    "REALtime:IO:SAMPle:RATE": "2000000",
    "REALtime:IO:SAMPle:STATe": "1",
    "REALtime:IO:CLOCk:FREQuency": "123456",
    "REALtime:IO:CLOCk:STATe": "1",
    "REALtime:SOURce": "16",
    "REALtime:EDGE": "0",
    "REALtime:GATE": "1",
    "REALtime:SAFE": "0",
    "REALtime:SEQ:LENGth": "1",
    "REALtime:SEQ:WIDTh": "1",
    "REALtime:ENC:TARGet": "100",
    "REALtime:ENC:APIN": "16",
    "REALtime:PCNT:DECode": "0",
    "REALtime:PCNT:DIRection": "0",
    "REALtime:PCNT:FILTer": "0",
    "REALtime:PCNT:GATE": "1",
    "REALtime:PCNT:CMP": "100",
    "REALtime:PCNT:PRESet": "0",
    "REALtime:PCNT:CLEar": "",
}

MIN_FIELDS = {
    "REALtime:STATus?": 9,
    "REALtime:IO:SYNC?": 6,
    "REALtime:IO:PROFile?": 8,
    "REALtime:IO:INPut:LEVel?": 3,
    "REALtime:IO:OUTPut:MASK?": 3,
}

SKIP_PATTERNS = {
    # This query returns the loaded realtime vector table. On a fresh board the
    # vector sequence length can be zero, so no SCPI result field is emitted.
    "REALtime:SEQ:DATA?",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="USB CDC serial port, for example COM6")
    parser.add_argument("--baud", type=int, default=115200, help="ignored by USB CDC on most hosts")
    parser.add_argument("--timeout", type=float, default=2.0, help="per-command timeout")
    parser.add_argument("--settle", type=float, default=1.0, help="seconds to wait after opening port")
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root")
    parser.add_argument("--dry-run", action="store_true", help="print generated tests without opening serial")
    return parser.parse_args()


def command_from_pattern(pattern: str) -> str | None:
    if not pattern.startswith("REALtime:"):
        return None
    if pattern in SKIP_PATTERNS:
        return None
    if pattern.endswith("?"):
        return pattern
    arg = COMMAND_ARGS.get(pattern)
    if arg is None:
        return None
    return f"{pattern} {arg}".strip()


def build_tests(root: Path) -> list[TestCase]:
    tests: list[TestCase] = []
    for path in REALTIME_HEADERS:
        text = (root / path).read_text(encoding="utf-8")
        for pattern in re.findall(r'\{\.pattern = "([^"]+)", \.callback = [A-Za-z0-9_]+\}', text):
            command = command_from_pattern(pattern)
            if command is not None:
                tests.append(TestCase(
                    command,
                    MIN_FIELDS.get(pattern, 1),
                    pattern.endswith("?"),
                ))
    if not tests:
        raise RuntimeError("no REALtime:* commands found")
    return tests


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def read_line(ser: serial.Serial, deadline: float) -> str | None:
    raw = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(1)
        if not chunk:
            continue
        raw.extend(chunk)
        if chunk in (b"\n", b"\r"):
            if raw.strip():
                return raw.decode("utf-8", errors="replace").strip()
            raw.clear()
    return None


def send_command(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write(command.encode("ascii") + b"\n")
    ser.flush()
    deadline = time.monotonic() + timeout_s
    while True:
        line = read_line(ser, deadline)
        if line is None:
            raise TimeoutError(command)
        if is_log_line(line):
            continue
        return line


def send_write_command(ser: serial.Serial, command: str) -> str:
    ser.reset_input_buffer()
    ser.write(command.encode("ascii") + b"\n")
    ser.flush()
    deadline = time.monotonic() + 0.2
    while time.monotonic() < deadline:
        if ser.in_waiting:
            ser.read(ser.in_waiting)
            deadline = time.monotonic() + 0.02
            continue
        time.sleep(0.005)
    return "<sent>"


def main() -> int:
    args = parse_args()
    tests = build_tests(args.root)
    if args.dry_run:
        for test in tests:
            print(test.command)
        print(f"generated={len(tests)}")
        return 0
    if not args.port:
        raise SystemExit("port is required unless --dry-run is used")

    failures: list[str] = []
    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        for test in tests:
            try:
                if test.expect_response:
                    response = send_command(ser, test.command, args.timeout)
                    fields = parse_csv_response(response)
                    ok = len(fields) >= test.min_fields and fields[0] != ""
                else:
                    response = send_write_command(ser, test.command)
                    ok = True
            except Exception as exc:  # noqa: BLE001 - validation should report and continue
                response = f"<error: {exc}>"
                ok = False
            status = "PASS" if ok else "FAIL"
            print(f"{status} {test.command} => {response}")
            if not ok:
                failures.append(test.command)

    print(f"summary: passed={not failures} failed={len(failures)} generated={len(tests)}")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
