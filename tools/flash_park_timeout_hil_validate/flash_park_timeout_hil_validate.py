#!/usr/bin/env python3
"""Validate that an injected core1 park timeout performs no raw Flash write."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.flash_lockout_hil_validate.flash_lockout_hil_validate import (
    parse_flash_transaction,
    parse_protection,
)


LOCKOUT_FAULT_CORE1_NO_ACK = 1
LOCKOUT_RESULT_FAULT_INJECTED = 4
FLASH_TRANSACTION_STATE_FAILED = 10
FLASH_TRANSACTION_REQUESTER_OTA_IMAGE = 1
FLASH_TRANSACTION_OPERATION_ERASE = 1
FLASH_TRANSACTION_ERROR_PARK = 18


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM8")
    parser.add_argument("package", type=Path, help="unified OTA package path")
    parser.add_argument("--expected-build", required=True)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out-dir", type=Path, required=True)
    return parser.parse_args()


def read_response(ser: serial.Serial, command: str, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    is_query = "?" in command.split(maxsplit=1)[0]
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        maybe_log = line[1:] if line.startswith('"') else line
        if not line or maybe_log.startswith("[") or maybe_log.startswith("log:"):
            continue
        if is_query and line in {'"OK"', "OK"}:
            continue
        return line
    return "<timeout>"


def exchange(port: str, command: str, timeout_s: float) -> str:
    with serial.Serial(port, 115200, timeout=0.1, write_timeout=timeout_s) as ser:
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.write((command + "\n").encode("ascii"))
        ser.flush()
        return read_response(ser, command, timeout_s)


def core1_heartbeat(response: str) -> int:
    try:
        fields = [int(field.strip(), 0) for field in response.split(",")]
    except ValueError as exc:
        raise ValueError(f"invalid core1 status: {response!r}") from exc
    if len(fields) < 3 or fields[0] != 1:
        raise ValueError(f"invalid core1 status: {response!r}")
    return fields[2]


def main() -> int:
    args = parse_args()
    package = args.package if args.package.is_absolute() else ROOT / args.package
    out_dir = args.out_dir if args.out_dir.is_absolute() else ROOT / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []
    records: dict[str, object] = {
        "port": args.port,
        "package": str(package),
        "expected_build": args.expected_build,
    }

    build = exchange(args.port, "SYST:FW:BUILD?", args.timeout).strip('"')
    before = parse_protection(
        exchange(args.port, "SYST:PROT:STAT?", args.timeout)
    )
    records["build"] = build
    records["before"] = before
    if build != args.expected_build:
        failures.append(f"build {build!r} != {args.expected_build!r}")

    set_response = exchange(
        args.port,
        f"SYST:OTA:INJ:LOCK {LOCKOUT_FAULT_CORE1_NO_ACK}",
        args.timeout,
    )
    injected = exchange(args.port, "SYST:OTA:INJ:LOCK?", args.timeout)
    records["set_response"] = set_response
    records["injected_flags"] = injected
    if "OK" not in set_response or injected != str(LOCKOUT_FAULT_CORE1_NO_ACK):
        failures.append("lockout fault injection command was not accepted")

    log_path = out_dir / "injected_ota.log"
    command = [
        sys.executable,
        str(ROOT / "tools" / "ota_send" / "ota_send.py"),
        args.port,
        str(package),
        "--timeout", str(args.timeout),
        "--expect-final-state", "FAILED",
        "--expect-error", "FLASH_ERASE",
    ]
    result = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=180.0,
    )
    log_path.write_text(result.stdout + f"\nexit_code={result.returncode}\n", encoding="utf-8")
    if result.returncode != 0:
        failures.append("injected OTA did not reach expected FLASH_ERASE failure")

    transaction = parse_flash_transaction(
        exchange(args.port, "SYST:DIAG:FLASH:TRAN?", args.timeout)
    )
    after = parse_protection(
        exchange(args.port, "SYST:PROT:STAT?", args.timeout)
    )
    records["transaction"] = transaction
    records["after"] = after
    expected = {
        "state": FLASH_TRANSACTION_STATE_FAILED,
        "requester": FLASH_TRANSACTION_REQUESTER_OTA_IMAGE,
        "operation": FLASH_TRANSACTION_OPERATION_ERASE,
        "processed_bytes": 0,
        "verified_bytes": 0,
        "last_error": FLASH_TRANSACTION_ERROR_PARK,
        "erase_count_delta": 0,
        "program_count_delta": 0,
    }
    for field, value in expected.items():
        if transaction[field] != value:
            failures.append(f"FlashTransaction {field} {transaction[field]} != {value}")
    if after["timeout_count"] != before["timeout_count"] + 1:
        failures.append("lockout timeout_count did not increment exactly once")
    if after["last_result"] != LOCKOUT_RESULT_FAULT_INJECTED:
        failures.append("lockout did not report injected no-ACK result")

    clear_response = exchange(args.port, "SYST:OTA:INJ:CLEAR", args.timeout)
    cleared = exchange(args.port, "SYST:OTA:INJ:LOCK?", args.timeout)
    heartbeat_before = core1_heartbeat(exchange(args.port, "SYST:CORE?", args.timeout))
    time.sleep(0.5)
    heartbeat_after = core1_heartbeat(exchange(args.port, "SYST:CORE?", args.timeout))
    records["clear_response"] = clear_response
    records["cleared_flags"] = cleared
    records["heartbeat"] = [heartbeat_before, heartbeat_after]
    if "OK" not in clear_response or cleared != "0":
        failures.append("lockout fault injection did not clear")
    if heartbeat_after <= heartbeat_before:
        failures.append("core1 heartbeat did not recover after clearing injection")

    records["passed"] = not failures
    records["failures"] = failures
    (out_dir / "summary.json").write_text(
        json.dumps(records, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    (out_dir / "summary.txt").write_text(
        "\n".join([
            f"passed={not failures}",
            f"build={build}",
            f"lockout_timeout_count={before['timeout_count']}->{after['timeout_count']}",
            f"transaction_error={transaction['last_error']}",
            f"raw_erase_delta={transaction['erase_count_delta']}",
            f"raw_program_delta={transaction['program_count_delta']}",
            f"core1_heartbeat={heartbeat_before}->{heartbeat_after}",
            *[f"failure={failure}" for failure in failures],
        ]) + "\n",
        encoding="utf-8",
    )
    print((out_dir / "summary.txt").read_text(encoding="utf-8"), end="")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
