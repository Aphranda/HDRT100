#!/usr/bin/env python3
"""Boot a pending OTA image, reconnect after USB reset, and commit it."""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import read_scpi_response  # noqa: E402
from tools.scpi_common.board_identity import parse_idn_response  # noqa: E402


class ReconnectedInstrument:
    def __init__(self, kind: str, endpoint: str, handle: Any,
                 manager: Any = None) -> None:
        self.kind = kind
        self.endpoint = endpoint
        self.handle = handle
        self.manager = manager

    def execute(self, text: str, timeout_s: float) -> str:
        if self.kind == "serial":
            return command(
                self.handle, text, timeout_s, require_match=True)
        return self.handle.query(text).strip()

    def close(self) -> None:
        try:
            self.handle.close()
        finally:
            if self.manager is not None:
                self.manager.close()

    def __enter__(self) -> "ReconnectedInstrument":
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--reopen-timeout", type=float, default=30.0)
    parser.add_argument("--boot-wait", type=float, default=3.0)
    parser.add_argument(
        "--serial-number",
        help=("expected *IDN? serial number; enables reconnect when USB "
              "re-enumerates on a different COM port"),
    )
    parser.add_argument("--skip-boot", action="store_true", help="only query and commit an already booted pending image")
    parser.add_argument(
        "--no-commit",
        action="store_true",
        help="boot and reconnect without issuing OTA:COMM (for no-confirm rollback HIL)",
    )
    parser.add_argument("--expected-build", help="expected SYSTem:FW:BUILD? text without quotes")
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    return parser.parse_args()


def command(ser: serial.Serial,
            text: str,
            timeout_s: float,
            *,
            require_match: bool = False) -> str:
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, text, timeout_s, require_match=require_match)


def candidate_ports(preferred_port: str, discover: bool) -> list[str]:
    ports = [preferred_port]
    if discover:
        ports.extend(item.device for item in list_ports.comports())
    unique: list[str] = []
    seen: set[str] = set()
    for port in ports:
        key = port.casefold()
        if key not in seen:
            seen.add(key)
            unique.append(port)
    return unique


def try_open_matching_port(port: str,
                           serial_number: str | None,
                           baud: int,
                           settle: float,
                           timeout_s: float) -> tuple[serial.Serial | None,
                                                       str | None,
                                                       Exception | None]:
    last_error: Exception | None = None
    for candidate in candidate_ports(port, serial_number is not None):
        ser: serial.Serial | None = None
        try:
            ser = serial.Serial(
                candidate, baud, timeout=0.1, write_timeout=timeout_s)
            time.sleep(settle)
            response = command(ser, "*IDN?", timeout_s, require_match=True)
            identity = parse_idn_response(response)
            if (serial_number is None or
                    identity.serial_number == serial_number):
                return ser, candidate, None
            last_error = RuntimeError(
                f"{candidate} is serial {identity.serial_number}, "
                f"expected {serial_number}")
        except Exception as exc:  # Ports may disappear during USB reset.
            last_error = exc
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass
    return None, None, last_error


def try_open_matching_visa(serial_number: str | None,
                           timeout_s: float) -> tuple[ReconnectedInstrument | None,
                                                      Exception | None]:
    if serial_number is None:
        return None, None
    try:
        import pyvisa
    except ImportError as exc:
        return None, exc

    manager = None
    last_error: Exception | None = None
    try:
        manager = pyvisa.ResourceManager()
        resources = [item for item in manager.list_resources()
                     if item.upper().startswith("USB")]
        resources.sort(key=lambda item: serial_number not in item)
        for resource in resources:
            instrument = None
            try:
                instrument = manager.open_resource(resource)
                instrument.timeout = max(1, int(timeout_s * 1000))
                instrument.write_termination = "\n"
                identity = parse_idn_response(instrument.query("*IDN?"))
                if identity.serial_number == serial_number:
                    return ReconnectedInstrument(
                        "usbtmc", resource, instrument, manager), None
                last_error = RuntimeError(
                    f"{resource} is serial {identity.serial_number}, "
                    f"expected {serial_number}")
            except Exception as exc:
                last_error = exc
            if instrument is not None:
                try:
                    instrument.close()
                except Exception:
                    pass
    except Exception as exc:
        last_error = exc
    if manager is not None:
        try:
            manager.close()
        except Exception:
            pass
    return None, last_error


def open_matching_instrument(port: str,
                             serial_number: str | None,
                             baud: int,
                             reopen_timeout: float,
                             settle: float,
                             timeout_s: float) -> ReconnectedInstrument:
    deadline = time.monotonic() + reopen_timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        ser, candidate, serial_error = try_open_matching_port(
            port, serial_number, baud, settle, timeout_s)
        if ser is not None and candidate is not None:
            return ReconnectedInstrument("serial", candidate, ser)
        instrument, visa_error = try_open_matching_visa(
            serial_number, timeout_s)
        if instrument is not None:
            return instrument
        last_error = visa_error or serial_error or last_error
        time.sleep(0.5)
    target = f"board {serial_number}" if serial_number else port
    raise SystemExit(f"failed to reopen {target}: {last_error}")


def slot_is_committed(response: str) -> bool:
    try:
        active, pending, confirmed, boot_attempts, _ = (
            int(field.strip(), 0) for field in response.split(","))
    except (TypeError, ValueError):
        return False
    return active == confirmed and pending == 0 and boot_attempts == 0


def build_matches_expected(response: str, expected_build: str | None) -> bool:
    return expected_build is None or response.strip('"') == expected_build


def run(args: argparse.Namespace) -> int:
    records: list[dict[str, str]] = []
    serial_number = args.serial_number

    if not args.skip_boot:
        try:
            with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as ser:
                time.sleep(args.settle)
                idn = command(ser, "*IDN?", args.timeout, require_match=True)
                detected_serial = parse_idn_response(idn).serial_number
                records.append({"command": "*IDN?", "response": idn})
                if serial_number is not None and detected_serial != serial_number:
                    raise SystemExit(
                        f"{args.port} is serial {detected_serial}, "
                        f"expected {serial_number}")
                serial_number = detected_serial
                response = command(ser, "SYSTem:OTA:BOOT", args.timeout)
                records.append({"command": "SYSTem:OTA:BOOT", "response": response})
                print(f"SYSTem:OTA:BOOT => {response}")
        except (OSError, serial.SerialException) as exc:
            response = f"<serial-reset:{exc}>"
            records.append({"command": "SYSTem:OTA:BOOT", "response": response})
            print(f"SYSTem:OTA:BOOT => {response}")
        time.sleep(args.boot_wait)

    instrument = open_matching_instrument(
        args.port, serial_number, args.baud, args.reopen_timeout,
        args.settle, args.timeout)
    print(
        f"reconnected_transport={instrument.kind} "
        f"endpoint={instrument.endpoint} serial={serial_number or 'unknown'}")
    with instrument:
        commands = [
            "SYSTem:FW:BUILD?",
            "SYSTem:OTA:SLOT?",
            "SYSTem:OTA:RES?",
            "SYSTem:OTA:TXN?",
            "SYSTem:OTA:JOUR?",
            "SYSTem:OTA:STAT?",
        ]
        build = ""
        for text in commands:
            response = instrument.execute(text, args.timeout)
            records.append({"command": text, "response": response})
            print(f"{text} => {response}")
            if text == "SYSTem:FW:BUILD?":
                build = response

        commit_allowed = build_matches_expected(build, args.expected_build)
        if not args.no_commit and commit_allowed:
            response = instrument.execute("SYSTem:OTA:COMMit", args.timeout)
            records.append({
                "command": "SYSTem:OTA:COMMit", "response": response})
            print(f"SYSTem:OTA:COMMit => {response}")
        elif not args.no_commit:
            print(
                "SYSTem:OTA:COMMit => <skipped: unexpected firmware build>")

        final_slot = instrument.execute("SYSTem:OTA:SLOT?", args.timeout)
        if not args.no_commit and commit_allowed:
            commit_deadline = time.monotonic() + args.timeout
            while (not slot_is_committed(final_slot) and
                   time.monotonic() < commit_deadline):
                time.sleep(0.1)
                final_slot = instrument.execute(
                    "SYSTem:OTA:SLOT?", args.timeout)
        records.append({
            "command": "SYSTem:OTA:SLOT?", "response": final_slot})
        print(f"SYSTem:OTA:SLOT? => {final_slot}")

        final_error = instrument.execute("SYSTem:ERRor?", args.timeout)
        records.append({
            "command": "SYSTem:ERRor?", "response": final_error})
        print(f"SYSTem:ERRor? => {final_error}")

    responses = {record["command"]: record["response"] for record in records}
    build = responses.get("SYSTem:FW:BUILD?", "")
    failures: list[str] = []
    if not build_matches_expected(build, args.expected_build):
        failures.append(f"build {build!r} != {args.expected_build!r}")
    if not args.no_commit and not slot_is_committed(final_slot):
        failures.append(f"final slot does not look committed: {final_slot!r}")
    if not args.no_commit and final_error != '0,"No error"':
        failures.append(f"final error is {final_error!r}")

    out_dir = args.out_dir or (ROOT / "build" / f"ota_boot_commit_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    summary = {
        "passed": not failures,
        "failed": len(failures),
        "failures": failures,
        "port": args.port,
        "reconnected_transport": instrument.kind,
        "reconnected_endpoint": instrument.endpoint,
        "serial_number": serial_number,
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
