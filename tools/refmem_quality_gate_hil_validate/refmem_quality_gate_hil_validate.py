#!/usr/bin/env python3
"""Validate RefMem quality gate rejection on a real board.

The negative path intentionally arms a short TDMA RX window and does not send a
matching TX frame. The expected result is a TDMA timeout, followed by
SYSTem:CONFigure:STAT? reporting ready=0 and gate_state=2. If --restore-package
is provided, the script restores the board with a normal OTA boot/commit before
exiting.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
import time
from contextlib import contextmanager
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterator

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]


@dataclass
class ValidationStep:
    name: str
    command: str
    response: str
    passed: bool
    reason: str


def read_serial_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = bytearray()
        while time.monotonic() < deadline:
            try:
                ch = ser.read(1)
            except (OSError, serial.SerialException) as exc:
                return f"<serial-reset:{exc}>"
            if not ch:
                continue
            raw.extend(ch)
            if ch == b"\n":
                break
        if not raw:
            continue
        line = bytes(raw).decode("utf-8", errors="replace").strip()
        maybe_log = line[1:] if line.startswith('"[') else line
        if not line or maybe_log.startswith("[") or maybe_log.startswith("log:"):
            continue
        if line in {'"OK"', "OK", 'OK"'} or line.startswith('"OK[') or line.startswith("OK["):
            return '"OK"'
        return re.sub(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+.*$', "", line).strip()
    return "<timeout>"


def parse_csv(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except Exception:
        return []


def parse_ints(response: str) -> list[int]:
    values: list[int] = []
    for part in parse_csv(response):
        try:
            values.append(int(part.strip().strip('"'), 0))
        except ValueError:
            pass
    return values


def parse_config_ints(response: str) -> list[int]:
    values: list[int] = []
    for part in parse_csv(response):
        try:
            values.append(int(part.strip().strip('"'), 0))
        except ValueError:
            pass
    if values and values[0] not in (0, 1):
        values = values[1:]
    return values


@contextmanager
def open_board(port: str, baud: int, timeout_s: float, settle_s: float) -> Iterator[serial.Serial]:
    ser = serial.Serial(port, baud, timeout=0.1, write_timeout=timeout_s)
    try:
        time.sleep(settle_s)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        yield ser
    finally:
        try:
            ser.flush()
        finally:
            ser.close()


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_serial_line(ser, timeout_s)


def config_ready(response: str) -> tuple[bool, str]:
    fields = parse_config_ints(response)
    if len(fields) < 3:
        return False, f"config response too short: {response}"
    if fields[0] != 1 or fields[1] != 1:
        return False, f"config gate not ready: ready={fields[0]} gate={fields[1]}"
    return True, "OK"


def config_rejected(response: str) -> tuple[bool, str]:
    fields = parse_config_ints(response)
    if len(fields) < 3:
        return False, f"config response too short: {response}"
    if fields[0] != 0 or fields[1] != 2:
        return False, f"config gate not rejected: ready={fields[0]} gate={fields[1]}"
    return True, "OK"


def config_ack_ready(response: str) -> tuple[bool, str]:
    fields = parse_ints(response)
    if len(fields) < 12:
        return False, f"config ACK response too short: {response}"
    target = fields[2]
    ack = fields[3]
    nack = fields[4]
    busy = fields[5]
    timeout = fields[6]
    reason = fields[7]
    if target == 0 or ack != target or nack != 0 or busy != 0 or timeout != 0 or reason != 0:
        return False, (
            "config ACK not ready: "
            f"target={target} ack={ack} nack={nack} busy={busy} timeout={timeout} reason={reason}"
        )
    return True, "OK"


def config_ack_rejected(response: str) -> tuple[bool, str]:
    fields = parse_ints(response)
    if len(fields) < 12:
        return False, f"config ACK response too short: {response}"
    target = fields[2]
    ack = fields[3]
    nack = fields[4]
    busy = fields[5]
    timeout = fields[6]
    reason = fields[7]
    if target == 0 or ack != 0 or nack != target or busy != 0 or timeout != 0 or reason == 0:
        return False, (
            "config ACK not rejected: "
            f"target={target} ack={ack} nack={nack} busy={busy} timeout={timeout} reason={reason}"
        )
    return True, "OK"


def tdma_timeout(response: str, before_timeout_count: int) -> tuple[bool, str]:
    fields = parse_ints(response)
    if len(fields) < 24:
        return False, f"TDMA status too short: {response}"
    state = fields[0]
    completed_seq = fields[5]
    intent_seq = fields[4]
    timeout_count = fields[19]
    last_result = fields[22]
    last_error = fields[23]
    if state != 5:
        return False, f"TDMA state {state} != ERROR"
    if completed_seq < intent_seq:
        return False, f"TDMA intent not completed: intent={intent_seq} completed={completed_seq}"
    if timeout_count <= before_timeout_count:
        return False, f"TDMA timeout did not increase: {before_timeout_count}->{timeout_count}"
    if last_result != 3 or last_error != 3:
        return False, f"TDMA last_result/error not timeout: result={last_result} error={last_error}"
    return True, "OK"


def wait_for_tdma_timeout(ser: serial.Serial,
                          timeout_s: float,
                          before_timeout_count: int) -> tuple[str, bool, str]:
    deadline = time.monotonic() + timeout_s
    last_response = "<not-polled>"
    last_reason = "<not-polled>"
    while time.monotonic() < deadline:
        last_response = query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", timeout_s)
        ok, reason = tdma_timeout(last_response, before_timeout_count)
        last_reason = reason
        if ok:
            return last_response, True, "OK"
        time.sleep(0.05)
    return last_response, False, last_reason


def run_step(name: str, command: list[str], out_dir: Path, timeout_s: float) -> tuple[bool, Path]:
    log_path = out_dir / "logs" / f"{name}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", newline="\n") as log:
        log.write(f"$ {' '.join(command)}\n")
        log.flush()
        process = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout_s,
        )
        log.write(process.stdout)
        log.write(f"\nexit_code={process.returncode}\n")
    return process.returncode == 0, log_path


def restore_board(port: str,
                  package: Path,
                  expected_build: str,
                  out_dir: Path,
                  timeout_s: float,
                  begin_timeout_s: float) -> list[str]:
    failures: list[str] = []
    ota_ok, ota_log = run_step(
        "restore_ota_send",
        [
            sys.executable,
            "tools/ota_send/ota_send.py",
            port,
            str(package),
            "--timeout",
            str(timeout_s),
            "--begin-timeout",
            str(begin_timeout_s),
            "--expect-final-state",
            "READY_TO_REBOOT",
        ],
        out_dir,
        timeout_s=300.0,
    )
    if not ota_ok:
        failures.append(f"restore OTA send failed, log={ota_log}")

    boot_ok, boot_log = run_step(
        "restore_boot_commit",
        [
            sys.executable,
            "tools/ota_boot_commit/ota_boot_commit.py",
            port,
            "--expected-build",
            expected_build,
            "--out-dir",
            str(out_dir / "restore_boot_commit"),
        ],
        out_dir,
        timeout_s=120.0,
    )
    if not boot_ok:
        failures.append(f"restore boot/commit failed, log={boot_log}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--deadline-1e3ns", type=int, default=1000)
    parser.add_argument("--tdma-timeout", type=float, default=3.0)
    parser.add_argument("--tdma-baud", type=int, default=25000000)
    parser.add_argument("--pins", default="16,17,18,23", help="rx,csn,sck,tx")
    parser.add_argument("--expected-build", help="expected build id after optional restore")
    parser.add_argument("--restore-package", type=Path, help="OTA package used to clear runtime timeout state")
    parser.add_argument("--begin-timeout", type=float, default=90.0)
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()

    pins = [int(part.strip(), 0) for part in args.pins.split(",")]
    if len(pins) != 4:
        raise SystemExit("--pins must be rx,csn,sck,tx")

    started = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or ROOT / "build-rtos-multicore-smoke" / f"refmem_quality_gate_{args.port}_{started}"
    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    steps: list[ValidationStep] = []
    failures: list[str] = []

    with open_board(args.port, args.baud, args.timeout, args.settle) as ser:
        build = query(ser, "SYSTem:FW:BUILD?", args.timeout).strip('"')

        before_config = query(ser, "SYSTem:CONFigure:STAT?", args.timeout)
        ok, reason = config_ready(before_config)
        steps.append(ValidationStep("before_config_ready", "SYSTem:CONFigure:STAT?", before_config, ok, reason))
        if not ok:
            failures.append(reason)

        before_ack = query(ser, "SYSTem:CONFigure:ACK?", args.timeout)
        before_ack_ok, before_ack_reason = config_ack_ready(before_ack)
        steps.append(ValidationStep("before_config_ack_ready",
                                    "SYSTem:CONFigure:ACK?",
                                    before_ack,
                                    before_ack_ok,
                                    before_ack_reason))
        if not before_ack_ok:
            failures.append(before_ack_reason)

        before_tdma = query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args.timeout)
        before_tdma_values = parse_ints(before_tdma)
        before_timeout_count = before_tdma_values[19] if len(before_tdma_values) > 19 else 0
        steps.append(ValidationStep("before_tdma_status",
                                    "SYSTem:REFMEM:SYNC:TDMA:STATus?",
                                    before_tdma,
                                    len(before_tdma_values) >= 24,
                                    "OK" if len(before_tdma_values) >= 24 else "TDMA status too short"))

        rx_command = (
            "SYSTem:REFMEM:SYNC:TDMA:RX "
            f"{args.deadline_1e3ns},{args.tdma_baud},{pins[0]},{pins[1]},{pins[2]},{pins[3]}"
        )
        rx_response = query(ser, rx_command, args.timeout)
        rx_fields = parse_csv(rx_response)
        rx_ok = bool(rx_fields and rx_fields[0].strip('"') == "ACCEPTED")
        steps.append(ValidationStep("arm_timeout_rx",
                                    rx_command,
                                    rx_response,
                                    rx_ok,
                                    "OK" if rx_ok else "TDMA RX was not accepted"))
        if not rx_ok:
            failures.append("TDMA RX timeout injection was not accepted")

        timeout_response, timeout_ok, timeout_reason = wait_for_tdma_timeout(
            ser,
            args.tdma_timeout,
            before_timeout_count,
        )
        steps.append(ValidationStep("tdma_timeout_observed",
                                    "SYSTem:REFMEM:SYNC:TDMA:STATus?",
                                    timeout_response,
                                    timeout_ok,
                                    timeout_reason))
        if not timeout_ok:
            failures.append(f"TDMA timeout was not observed: {timeout_reason}")

        after_config = query(ser, "SYSTem:CONFigure:STAT?", args.timeout)
        reject_ok, reject_reason = config_rejected(after_config)
        steps.append(ValidationStep("after_config_rejected",
                                    "SYSTem:CONFigure:STAT?",
                                    after_config,
                                    reject_ok,
                                    reject_reason))
        if not reject_ok:
            failures.append(reject_reason)

        after_ack = query(ser, "SYSTem:CONFigure:ACK?", args.timeout)
        reject_ack_ok, reject_ack_reason = config_ack_rejected(after_ack)
        steps.append(ValidationStep("after_config_ack_rejected",
                                    "SYSTem:CONFigure:ACK?",
                                    after_ack,
                                    reject_ack_ok,
                                    reject_ack_reason))
        if not reject_ack_ok:
            failures.append(reject_ack_reason)

    restore_failures: list[str] = []
    if args.restore_package is not None:
        if not args.expected_build:
            raise SystemExit("--expected-build is required with --restore-package")
        package = args.restore_package if args.restore_package.is_absolute() else ROOT / args.restore_package
        restore_failures = restore_board(args.port,
                                         package,
                                         args.expected_build,
                                         out_dir,
                                         args.timeout,
                                         args.begin_timeout)
        failures.extend(restore_failures)

        with open_board(args.port, args.baud, args.timeout, args.settle) as ser:
            final_build = query(ser, "SYSTem:FW:BUILD?", args.timeout).strip('"')
            final_config = query(ser, "SYSTem:CONFigure:STAT?", args.timeout)
            final_ok, final_reason = config_ready(final_config)
            if final_build != args.expected_build:
                final_ok = False
                final_reason = f"final build {final_build!r} != {args.expected_build!r}"
            steps.append(ValidationStep("restore_config_ready",
                                        "SYSTem:CONFigure:STAT?",
                                        final_config,
                                        final_ok,
                                        final_reason))
            if not final_ok:
                failures.append(final_reason)

            final_ack = query(ser, "SYSTem:CONFigure:ACK?", args.timeout)
            final_ack_ok, final_ack_reason = config_ack_ready(final_ack)
            steps.append(ValidationStep("restore_config_ack_ready",
                                        "SYSTem:CONFigure:ACK?",
                                        final_ack,
                                        final_ack_ok,
                                        final_ack_reason))
            if not final_ack_ok:
                failures.append(final_ack_reason)

    records = {
        "started": started,
        "port": args.port,
        "build": build,
        "deadline_1e3ns": args.deadline_1e3ns,
        "tdma_baud": args.tdma_baud,
        "pins": pins,
        "restore_package": str(args.restore_package) if args.restore_package else None,
        "steps": [asdict(step) for step in steps],
        "passed": not failures,
        "failures": failures,
    }
    (out_dir / "summary.json").write_text(json.dumps(records, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")
    lines = [
        f"passed={not failures}",
        f"port={args.port}",
        f"build={build}",
        f"out_dir={out_dir}",
    ] + [f"{step.name}: {'PASS' if step.passed else 'FAIL'} {step.reason}" for step in steps]
    lines.extend(f"failure={failure}" for failure in failures)
    (out_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
