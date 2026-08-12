#!/usr/bin/env python3
"""Validate product SCPI framework commands over USB CDC or USBTMC/VISA.

The test list is generated from the product-facing SCPI domain headers and the
expected fixed-response fields are generated from the matching SCPI command
sources. This keeps the board-side validation aligned with the firmware command
table as product commands move between modules.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
PRODUCT_HEADERS = (
    Path("middleware/scpi_port/inc/scpi_system_access_commands.h"),
    Path("middleware/scpi_port/inc/scpi_config_commands.h"),
    Path("middleware/scpi_port/inc/scpi_calibration_commands.h"),
    Path("middleware/scpi_port/inc/scpi_sync_commands.h"),
    Path("middleware/scpi_port/inc/scpi_trigger_commands.h"),
    Path("middleware/scpi_port/inc/scpi_system_diagnostics_commands.h"),
    Path("middleware/scpi_port/inc/scpi_measure_commands.h"),
)
PRODUCT_SOURCES = (
    Path("middleware/scpi_port/src/scpi_port.c"),
    Path("middleware/scpi_port/src/scpi_system_access_commands.c"),
    Path("middleware/scpi_port/src/scpi_config_commands.c"),
    Path("middleware/scpi_port/src/scpi_calibration_commands.c"),
    Path("middleware/scpi_port/src/scpi_sync_commands.c"),
    Path("middleware/scpi_port/src/scpi_trigger_commands.c"),
    Path("middleware/scpi_port/src/scpi_system_diagnostics_commands.c"),
    Path("middleware/scpi_port/src/scpi_measure_commands.c"),
)


@dataclass(frozen=True)
class ExpectedField:
    kind: str
    value: str = ""

    def matches(self, actual: str) -> bool:
        if self.kind == "exact":
            return actual == self.value
        if self.kind == "text":
            return True
        if self.kind == "nonempty":
            return actual != ""
        return False

    def display(self) -> str:
        if self.kind == "exact":
            return self.value
        return f"<{self.kind}>"


@dataclass(frozen=True)
class TestCase:
    command: str
    callback: str
    expected: tuple[ExpectedField, ...]
    source: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="USB CDC serial port, for example COM4")
    parser.add_argument("--visa-resource", help="USBTMC VISA resource, for example USB0::0xCAFE::0x4030::SERIAL::INSTR")
    parser.add_argument("--baud", type=int, default=115200, help="ignored by USB CDC on most hosts")
    parser.add_argument("--timeout", type=float, default=2.0, help="per-command response timeout")
    parser.add_argument("--settle", type=float, default=1.0, help="seconds to wait after opening the port")
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root")
    parser.add_argument("--channel", type=int, default=1, help="channel number used for SCPI # patterns")
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    parser.add_argument("--dry-run", action="store_true", help="only generate and print the test list")
    parser.add_argument("--list", action="store_true", help="print generated test cases")
    parser.add_argument("--fail-fast", action="store_true", help="stop on first failed command")
    parser.add_argument("--skip-mode", action="store_true", help="skip TRIGger:MODE compatibility checks")
    return parser.parse_args()


def strip_c_suffix(expr: str) -> str:
    return re.sub(r"[uUlL]+$", "", expr.strip())


def parse_integer(expr: str) -> str | None:
    text = strip_c_suffix(expr)
    try:
        return str(int(text, 0))
    except ValueError:
        return None


def expected_from_result(kind: str, expr: str, *, channel: int) -> ExpectedField:
    expr = expr.strip()
    if kind in ("UInt32", "Int32"):
        if expr == "numbers[0]":
            return ExpectedField("exact", str(channel))
        value = parse_integer(expr)
        return ExpectedField("exact", value) if value is not None else ExpectedField("nonempty")
    if kind == "Bool":
        if expr == "TRUE":
            return ExpectedField("exact", "1")
        if expr == "FALSE":
            return ExpectedField("exact", "0")
        return ExpectedField("nonempty")
    if kind == "Text":
        match = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', expr)
        if match:
            value = bytes(match.group(1), "utf-8").decode("unicode_escape")
            return ExpectedField("exact", value)
        return ExpectedField("text")
    return ExpectedField("nonempty")


def command_from_pattern(pattern: str, *, channel: int) -> str:
    return pattern.replace("#", str(channel))


def parse_product_commands(root: Path, *, channel: int) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    for path in PRODUCT_HEADERS:
        header = (root / path).read_text(encoding="utf-8")
        for match in re.finditer(r'\{\.pattern = "([^"]+)", \.callback = ([A-Za-z0-9_]+)\}', header):
            pattern, callback = match.groups()
            entries.append((command_from_pattern(pattern, channel=channel), callback))
    if not entries:
        joined = ", ".join(str(path) for path in PRODUCT_HEADERS)
        raise RuntimeError(f"no product SCPI command registrations found in {joined}")
    return entries


def parse_callback_responses(root: Path, *, channel: int) -> dict[str, tuple[ExpectedField, ...]]:
    source = "\n".join((root / path).read_text(encoding="utf-8") for path in PRODUCT_SOURCES)
    callbacks: dict[str, tuple[ExpectedField, ...]] = {}
    function_re = re.compile(
        r"(?:static\s+)?scpi_result_t\s+([A-Za-z0-9_]+)\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
        re.DOTALL,
    )
    result_re = re.compile(r"SCPI_Result(UInt32|Int32|Bool|Text)\s*\(\s*context\s*,\s*(.*?)\s*\);")
    for match in function_re.finditer(source):
        name = match.group(1)
        fields = [
            expected_from_result(kind, expr, channel=channel)
            for kind, expr in result_re.findall(match.group("body"))
        ]
        callbacks[name] = tuple(fields)
    if not callbacks:
        joined = ", ".join(str(path) for path in PRODUCT_SOURCES)
        raise RuntimeError(f"no product SCPI callbacks found in {joined}")
    return callbacks


def mode_tests() -> list[TestCase]:
    tests = [
        TestCase(f"TRIGger:MODE {mode}", "scpi_cmd_trigger_mode", (ExpectedField("exact", "1"),), "mode")
        for mode in range(5)
    ]
    tests.append(
        TestCase(
            "TRIGger:MODE?",
            "scpi_cmd_trigger_mode_q",
            (
                ExpectedField("exact", "SIM"),
                ExpectedField("exact", "4"),
                ExpectedField("exact", "IDLE"),
                ExpectedField("exact", "ALLOW"),
                ExpectedField("exact", "NONE"),
            ),
            "mode",
        )
    )
    return tests


def build_tests(root: Path, *, channel: int, include_mode: bool) -> list[TestCase]:
    callbacks = parse_callback_responses(root, channel=channel)
    tests: list[TestCase] = []
    for command, callback in parse_product_commands(root, channel=channel):
        expected = callbacks.get(callback)
        if expected is None:
            raise RuntimeError(f"callback {callback} has no parsed fixed response")
        if not expected:
            raise RuntimeError(f"callback {callback} has no SCPI_Result fields")
        tests.append(TestCase(command, callback, expected, "product"))
    if include_mode:
        tests.extend(mode_tests())
    return tests


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def trim_embedded_log(line: str) -> str:
    match = re.search(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+', line)
    return line[:match.start()].strip() if match else line


def strip_leading_ack(line: str) -> str:
    if line in ('"OK"', "OK"):
        return line
    if line.startswith('"OK[') or line.startswith("OK["):
        return ""
    if line.startswith('"OK"['):
        return line[4:].strip()
    if line.startswith('OK"['):
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
    return bytes(raw).decode("utf-8", errors="replace").strip()


def read_response(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = read_serial_line(ser, deadline)
        if line is None or is_log_line(line):
            continue
        line = strip_leading_ack(trim_embedded_log(line))
        if line:
            return line
    return "<timeout>"


def send_command(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_response(ser, timeout_s)


def open_visa_resource(resource: str, timeout_s: float):
    try:
        import pyvisa
    except ImportError as exc:
        raise SystemExit("pyvisa is required: python -m pip install pyvisa") from exc

    rm = pyvisa.ResourceManager()
    inst = rm.open_resource(resource)
    inst.timeout = int(timeout_s * 1000)
    inst.write_termination = "\n"
    inst.read_termination = "\n"
    return rm, inst


def visa_query(inst, command: str) -> str:
    return str(inst.query(command)).strip()


def validate_response(test: TestCase, response: str) -> tuple[bool, str]:
    if response == "<timeout>":
        return False, "timeout"
    if re.match(r'^-\d+\s*,\s*"', response):
        return False, "SCPI error response"
    fields = parse_csv_response(response)
    if len(fields) != len(test.expected):
        return False, f"field count {len(fields)} != expected {len(test.expected)}"
    for index, (actual, expected) in enumerate(zip(fields, test.expected)):
        if not expected.matches(actual):
            return False, f"field {index + 1}: {actual!r} != {expected.display()!r}"
    return True, "ok"


def write_outputs(out_dir: Path, records: list[dict[str, str]], summary: dict[str, object]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    transcript = out_dir / "product_scpi_transcript.txt"
    with transcript.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# product SCPI validation {datetime.now().isoformat(timespec='seconds')}\n")
        for record in records:
            handle.write(f"> {record['command']}\n")
            handle.write(f"< {record['response']}\n")
            handle.write(f"# {record['status']} {record['reason']}\n")
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")
    lines = [
        f"passed={summary['passed']}",
        f"total={summary['total']}",
        f"failed={summary['failed']}",
        f"out_dir={out_dir}",
    ]
    (out_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def print_tests(tests: Iterable[TestCase]) -> None:
    for test in tests:
        expected = ",".join(field.display() for field in test.expected)
        print(f"{test.command} -> {expected} [{test.callback}]")


def run(args: argparse.Namespace) -> int:
    tests = build_tests(args.root, channel=args.channel, include_mode=not args.skip_mode)
    if args.list or args.dry_run:
        print_tests(tests)
    if args.dry_run:
        print(f"generated={len(tests)}")
        return 0
    if args.port and args.visa_resource:
        raise SystemExit("use either a CDC port or --visa-resource, not both")
    if not args.port and not args.visa_resource:
        raise SystemExit("CDC port or --visa-resource is required unless --dry-run is used")

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or (args.root / "build" / f"product_scpi_validation_{stamp}")
    records: list[dict[str, str]] = []
    failures = 0

    close_handles = []
    transport = "visa" if args.visa_resource else "cdc"
    try:
        if args.visa_resource:
            rm, inst = open_visa_resource(args.visa_resource, args.timeout)
            close_handles = [inst, rm]

            def execute(command: str) -> str:
                return visa_query(inst, command)
        else:
            ser = serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout)
            close_handles = [ser]
            time.sleep(args.settle)
            ser.reset_input_buffer()

            def execute(command: str) -> str:
                return send_command(ser, command, args.timeout)

        try:
            execute("*CLS")
        except Exception:
            pass

        for test in tests:
            response = execute(test.command)
            ok, reason = validate_response(test, response)
            failures += 0 if ok else 1
            record = {
                "command": test.command,
                "callback": test.callback,
                "response": response,
                "status": "PASS" if ok else "FAIL",
                "reason": reason,
            }
            records.append(record)
            print(f"{record['status']} {test.command} => {response}")
            if args.fail_fast and not ok:
                break
            time.sleep(0.05)
    finally:
        try:
            if close_handles:
                execute("*CLS")
        except Exception:
            pass
        for handle in close_handles:
            try:
                handle.close()
            except Exception:
                pass

    summary = {
        "passed": failures == 0,
        "total": len(records),
        "failed": failures,
        "port": args.port,
        "visa_resource": args.visa_resource,
        "transport": transport,
        "out_dir": str(out_dir),
    }
    write_outputs(out_dir, records, summary)
    print(f"summary: passed={summary['passed']} failed={failures} out_dir={out_dir}")
    return 0 if failures == 0 else 1


def main() -> int:
    args = parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
