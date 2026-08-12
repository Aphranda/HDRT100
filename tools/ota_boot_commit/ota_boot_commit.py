#!/usr/bin/env python3
"""Boot a pending OTA image, reconnect after USB reset, and commit it."""

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


ROOT = Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--reopen-timeout", type=float, default=30.0)
    parser.add_argument("--boot-wait", type=float, default=3.0)
    parser.add_argument("--skip-boot", action="store_true", help="only query and commit an already booted pending image")
    parser.add_argument("--expected-build", help="expected SYSTem:FW:BUILD? text without quotes")
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    return parser.parse_args()


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


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def command(ser: serial.Serial, text: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = read_serial_line(ser, deadline)
        if line is None or is_log_line(line):
            continue
        return line
    return "<timeout>"


def open_port(port: str, baud: int, reopen_timeout: float, settle: float, timeout_s: float) -> serial.Serial:
    deadline = time.monotonic() + reopen_timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            ser = serial.Serial(port, baud, timeout=0.1, write_timeout=timeout_s)
            time.sleep(settle)
            ser.reset_input_buffer()
            return ser
        except Exception as exc:  # pyserial raises OSError or SerialException depending on reset phase.
            last_error = exc
            time.sleep(0.5)
    raise SystemExit(f"failed to reopen {port}: {last_error}")


def run(args: argparse.Namespace) -> int:
    records: list[dict[str, str]] = []

    if not args.skip_boot:
        try:
            with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as ser:
                time.sleep(args.settle)
                response = command(ser, "SYSTem:OTA:BOOT", args.timeout)
                records.append({"command": "SYSTem:OTA:BOOT", "response": response})
                print(f"SYSTem:OTA:BOOT => {response}")
        except (OSError, serial.SerialException) as exc:
            response = f"<serial-reset:{exc}>"
            records.append({"command": "SYSTem:OTA:BOOT", "response": response})
            print(f"SYSTem:OTA:BOOT => {response}")
        time.sleep(args.boot_wait)

    with open_port(args.port, args.baud, args.reopen_timeout, args.settle, args.timeout) as ser:
        for text in (
            "SYSTem:FW:BUILD?",
            "SYSTem:OTA:SLOT?",
            "SYSTem:OTA:COMMit",
            "SYSTem:OTA:SLOT?",
            "SYSTem:ERRor?",
        ):
            response = command(ser, text, args.timeout)
            records.append({"command": text, "response": response})
            print(f"{text} => {response}")

    responses = {record["command"]: record["response"] for record in records}
    build = responses.get("SYSTem:FW:BUILD?", "")
    final_slot = records[-2]["response"] if len(records) >= 2 else ""
    final_error = responses.get("SYSTem:ERRor?", "")
    failures: list[str] = []
    if args.expected_build and build.strip('"') != args.expected_build:
        failures.append(f"build {build!r} != {args.expected_build!r}")
    if "0," not in final_slot:
        failures.append(f"final slot does not look committed: {final_slot!r}")
    if final_error != '0,"No error"':
        failures.append(f"final error is {final_error!r}")

    out_dir = args.out_dir or (ROOT / "build" / f"ota_boot_commit_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    summary = {
        "passed": not failures,
        "failed": len(failures),
        "failures": failures,
        "port": args.port,
        "expected_build": args.expected_build,
        "records": records,
        "out_dir": str(out_dir),
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out_dir / "summary.txt").write_text(
        "\n".join((f"passed={summary['passed']}", f"failed={len(failures)}", f"out_dir={out_dir}")) + "\n",
        encoding="utf-8",
    )
    print(f"summary: passed={summary['passed']} failed={len(failures)} out_dir={out_dir}")
    return 0 if not failures else 1


def main() -> int:
    return run(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
