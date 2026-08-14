#!/usr/bin/env python3
"""Validate flash/core1 lockout evidence around a real OTA flash write."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
LOCKOUT_RESULT_ACKED = 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM5")
    parser.add_argument("package", type=Path, help="unified OTA package path")
    parser.add_argument("--expected-build", required=True, help="expected build id after OTA boot")
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--begin-timeout", type=float, default=90.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    return parser.parse_args()


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def read_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            raw = ser.readline()
        except (OSError, serial.SerialException) as exc:
            return f"<serial-reset:{exc}>"
        if not raw:
            continue
        text = raw.decode("utf-8", errors="replace").strip()
        if is_log_line(text) or text in {'"OK"', "OK"}:
            continue
        return text
    return "<timeout>"


def query(port: str, command: str, timeout_s: float, settle_s: float) -> str:
    with serial.Serial(port, 115200, timeout=0.1, write_timeout=timeout_s) as ser:
        time.sleep(settle_s)
        ser.reset_input_buffer()
        ser.write((command + "\n").encode("ascii"))
        ser.flush()
        return read_line(ser, timeout_s)


def parse_u32_csv(response: str) -> list[int]:
    values: list[int] = []
    for part in response.split(","):
        try:
            values.append(int(part.strip(), 0))
        except ValueError:
            pass
    return values


def parse_protection(response: str) -> dict[str, int]:
    fields = parse_u32_csv(response)
    if len(fields) < 21:
        raise ValueError(f"runtime protection response has no S0 lockout evidence fields: {response!r}")
    return {
        "version": fields[0],
        "table_seq": fields[1],
        "ram_resident_required": fields[2],
        "supported": fields[3],
        "online": fields[4],
        "requested": fields[5],
        "acknowledged": fields[6],
        "park_state": fields[7],
        "last_result": fields[8],
        "last_elapsed_us": fields[9],
        "request_seq": fields[10],
        "ack_seq": fields[11],
        "release_seq": fields[12],
        "timeout_count": fields[13],
        "release_timeout_count": fields[14],
        "entry_table_owner": fields[15],
        "flags": fields[16],
        "guard_owner": fields[17],
        "guard_crc32": fields[18],
        "guard_stale": fields[19],
        "guard_flags": fields[20],
    }


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


def main() -> int:
    args = parse_args()
    package = args.package if args.package.is_absolute() else ROOT / args.package
    out_dir = (
        args.out_dir if args.out_dir is not None else
        ROOT / "build-rtos-multicore-smoke" / f"s0_flash_lockout_{args.port}_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    )
    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    records: dict[str, object] = {
        "port": args.port,
        "package": str(package),
        "expected_build": args.expected_build,
        "steps": [],
    }
    failures: list[str] = []

    before_response = query(args.port, "SYSTem:PROTection:STATus?", args.timeout, args.settle)
    before = parse_protection(before_response)
    records["before"] = before
    (out_dir / "before_protection.txt").write_text(before_response + "\n", encoding="utf-8")

    ota_ok, ota_log = run_step(
        "positive_ota",
        [
            sys.executable,
            "tools/ota_send/ota_send.py",
            args.port,
            str(package),
            "--begin-timeout",
            str(args.begin_timeout),
            "--timeout",
            str(args.timeout),
            "--expect-final-state",
            "READY_TO_REBOOT",
        ],
        out_dir,
        timeout_s=300.0,
    )
    records["steps"].append({"name": "positive_ota", "passed": ota_ok, "log": str(ota_log)})
    if not ota_ok:
        failures.append("positive OTA failed")

    after_write_response = query(args.port, "SYSTem:PROTection:STATus?", args.timeout, args.settle)
    after_write = parse_protection(after_write_response)
    records["after_write"] = after_write
    (out_dir / "after_write_protection.txt").write_text(after_write_response + "\n", encoding="utf-8")

    boot_ok, boot_log = run_step(
        "boot_commit",
        [
            sys.executable,
            "tools/ota_boot_commit/ota_boot_commit.py",
            args.port,
            "--expected-build",
            args.expected_build,
            "--out-dir",
            str(out_dir / "boot_commit"),
        ],
        out_dir,
        timeout_s=120.0,
    )
    records["steps"].append({"name": "boot_commit", "passed": boot_ok, "log": str(boot_log)})
    if not boot_ok:
        failures.append("boot/commit failed")

    final_build = query(args.port, "SYSTem:FW:BUILD?", args.timeout, args.settle)
    final_response = query(args.port, "SYSTem:PROTection:STATus?", args.timeout, args.settle)
    final = parse_protection(final_response)
    records["final_build"] = final_build
    records["final"] = final
    (out_dir / "final_protection.txt").write_text(final_response + "\n", encoding="utf-8")

    if final_build.strip('"') != args.expected_build:
        failures.append(f"final build {final_build!r} != {args.expected_build!r}")
    if after_write["request_seq"] <= before["request_seq"]:
        failures.append("lockout request_seq did not grow during OTA write")
    if after_write["ack_seq"] != after_write["request_seq"]:
        failures.append("lockout ack_seq does not match request_seq after OTA write")
    if after_write["release_seq"] < after_write["request_seq"]:
        failures.append("lockout release_seq did not catch up after OTA write")
    if after_write["timeout_count"] != before["timeout_count"]:
        failures.append("lockout timeout_count changed during positive OTA")
    if after_write["release_timeout_count"] != before["release_timeout_count"]:
        failures.append("lockout release_timeout_count changed during positive OTA")
    if after_write["last_result"] != LOCKOUT_RESULT_ACKED:
        failures.append(f"last_result {after_write['last_result']} != ACKED")
    if after_write["last_elapsed_us"] == 0:
        failures.append("last_elapsed_us was not recorded")
    if final["online"] != 1 or final["supported"] != 1:
        failures.append("final lockout gate is not supported/online")

    records["passed"] = not failures
    records["failures"] = failures
    (out_dir / "summary.json").write_text(json.dumps(records, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")
    (out_dir / "summary.txt").write_text(
        "\n".join(
            [
                f"passed={not failures}",
                f"out_dir={out_dir}",
                f"request_seq={before['request_seq']}->{after_write['request_seq']}",
                f"ack_seq={before['ack_seq']}->{after_write['ack_seq']}",
                f"release_seq={before['release_seq']}->{after_write['release_seq']}",
                f"last_result={after_write['last_result']}",
                f"last_elapsed_us={after_write['last_elapsed_us']}",
            ] + [f"failure={failure}" for failure in failures]
        ) + "\n",
        encoding="utf-8",
    )
    print((out_dir / "summary.txt").read_text(encoding="utf-8"), end="")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
