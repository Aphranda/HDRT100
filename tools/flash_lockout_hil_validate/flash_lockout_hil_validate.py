#!/usr/bin/env python3
"""Validate flash/core1 lockout evidence around a real OTA flash write."""

from __future__ import annotations

import argparse
import json
import struct
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
FLASH_TRANSACTION_STATE_COMPLETE = 9
FLASH_TRANSACTION_REQUESTER_OTA_IMAGE = 1
FLASH_TRANSACTION_REQUESTER_OTA_METADATA = 2
FLASH_TRANSACTION_OPERATION_PROGRAM = 2
FLASH_TRANSACTION_COMPLETION_COMMITTED = 4
FLASH_TRANSACTION_RESULT_COMMITTED = 1
FLASH_TRANSACTION_ERROR_NONE = 0
FLASH_COMPAT_MAP_APP_A_ID = 1
FLASH_COMPAT_MAP_APP_B_ID = 2
FLASH_COMPAT_MAP_BOOT_CONTROL_ID = 3
OTA_METADATA_RECORD_SIZE = 256
PACKAGE_IMAGE_COUNT_OFFSET = 20
PACKAGE_IMAGE_TABLE_OFFSET = 192
PACKAGE_IMAGE_ENTRY_SIZE = 32
PACKAGE_BLOCK_SIZE = 512


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


def parse_flash_transaction(response: str) -> dict[str, int]:
    fields = parse_u32_csv(response)
    names = (
        "state", "job_id", "requester", "partition_id", "operation",
        "requested_bytes", "processed_bytes", "verified_bytes", "map_version",
        "provider_generation", "store_generation", "transaction_generation",
        "completion_level", "last_result", "last_error", "retry_count",
        "abort_pending", "lockout_request_seq", "lockout_ack_seq",
        "lockout_timeout_count", "erase_count_delta", "program_count_delta",
        "verify_failure_count", "temperature_flags", "policy_gate_reason",
        "started_timestamp_ms", "completed_timestamp_ms",
    )
    if len(fields) < len(names):
        raise ValueError(f"FlashTransaction Vector response is incomplete: {response!r}")
    return dict(zip(names, fields[:len(names)]))


def parse_flash_transaction_probe(log_path: Path) -> dict[str, int]:
    prefix = "flash_transaction_probe="
    for line in log_path.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return parse_flash_transaction(line[len(prefix):])
    raise ValueError(f"FlashTransaction probe missing from {log_path}")


def package_probe_block(package_path: Path, target_slot: int) -> int:
    header = package_path.read_bytes()[:512]
    if len(header) < 512:
        raise ValueError(f"OTA package header is incomplete: {package_path}")
    image_count = struct.unpack_from("<I", header, PACKAGE_IMAGE_COUNT_OFFSET)[0]
    for index in range(image_count):
        entry = PACKAGE_IMAGE_TABLE_OFFSET + index * PACKAGE_IMAGE_ENTRY_SIZE
        if entry + PACKAGE_IMAGE_ENTRY_SIZE > len(header):
            raise ValueError(f"OTA package image table is truncated: {package_path}")
        slot, image_offset = struct.unpack_from("<II", header, entry)
        if slot != target_slot:
            continue
        if image_offset % PACKAGE_BLOCK_SIZE != 0:
            raise ValueError(
                f"OTA package slot {target_slot} image offset is not block-aligned"
            )
        return image_offset // PACKAGE_BLOCK_SIZE + 1
    raise ValueError(f"OTA package has no image for slot {target_slot}")


def parse_active_slot(response: str) -> int:
    fields = parse_u32_csv(response)
    if len(fields) < 1 or fields[0] not in (1, 2):
        raise ValueError(f"OTA slot response has invalid active slot: {response!r}")
    return fields[0]


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

    slot_response = query(args.port, "SYSTem:OTA:SLOT?", args.timeout, args.settle)
    active_slot = parse_active_slot(slot_response)
    expected_partition = (
        FLASH_COMPAT_MAP_APP_B_ID
        if active_slot == FLASH_COMPAT_MAP_APP_A_ID
        else FLASH_COMPAT_MAP_APP_A_ID
    )
    records["active_slot_before"] = active_slot
    records["expected_transaction_partition"] = expected_partition
    probe_block = package_probe_block(package, expected_partition)
    records["image_probe_block"] = probe_block
    (out_dir / "before_slot.txt").write_text(slot_response + "\n", encoding="utf-8")

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
            "--flash-transaction-probe-after-blocks",
            str(probe_block),
        ],
        out_dir,
        timeout_s=300.0,
    )
    records["steps"].append({"name": "positive_ota", "passed": ota_ok, "log": str(ota_log)})
    if not ota_ok:
        failures.append("positive OTA failed")

    try:
        image_transaction = parse_flash_transaction_probe(ota_log)
        records["image_flash_transaction"] = image_transaction
    except ValueError as exc:
        image_transaction = {}
        failures.append(str(exc))

    after_write_response = query(args.port, "SYSTem:PROTection:STATus?", args.timeout, args.settle)
    after_write = parse_protection(after_write_response)
    records["after_write"] = after_write
    (out_dir / "after_write_protection.txt").write_text(after_write_response + "\n", encoding="utf-8")

    transaction_response = query(
        args.port, "SYSTem:DIAGnostic:FLASh:TRANsaction?", args.timeout, args.settle
    )
    transaction = parse_flash_transaction(transaction_response)
    records["flash_transaction"] = transaction
    (out_dir / "after_write_flash_transaction.txt").write_text(
        transaction_response + "\n", encoding="utf-8"
    )

    expected_image_transaction = {
        "state": FLASH_TRANSACTION_STATE_COMPLETE,
        "requester": FLASH_TRANSACTION_REQUESTER_OTA_IMAGE,
        "partition_id": expected_partition,
        "operation": FLASH_TRANSACTION_OPERATION_PROGRAM,
        "map_version": 1,
        "completion_level": FLASH_TRANSACTION_COMPLETION_COMMITTED,
        "last_result": FLASH_TRANSACTION_RESULT_COMMITTED,
        "last_error": FLASH_TRANSACTION_ERROR_NONE,
        "abort_pending": 0,
        "erase_count_delta": 0,
        "program_count_delta": 1,
        "verify_failure_count": 0,
    }
    for field, expected in expected_image_transaction.items():
        if image_transaction.get(field) != expected:
            failures.append(
                f"image FlashTransaction {field} "
                f"{image_transaction.get(field)} != {expected}"
            )
    if image_transaction.get("requested_bytes") != 512:
        failures.append("image FlashTransaction did not own a 512-byte payload")
    if image_transaction.get("processed_bytes") != image_transaction.get("requested_bytes"):
        failures.append("image FlashTransaction processed bytes do not match request")
    if image_transaction.get("verified_bytes") != image_transaction.get("requested_bytes"):
        failures.append("image FlashTransaction verified bytes do not match request")
    if image_transaction.get("provider_generation", 0) == 0:
        failures.append("image FlashTransaction provider_generation is zero")
    if image_transaction.get("transaction_generation", 0) == 0:
        failures.append("image FlashTransaction transaction_generation is zero")
    if image_transaction.get("lockout_request_seq") != image_transaction.get("lockout_ack_seq"):
        failures.append("image FlashTransaction lockout request/ack sequences differ")

    expected_metadata_transaction = {
        "state": FLASH_TRANSACTION_STATE_COMPLETE,
        "requester": FLASH_TRANSACTION_REQUESTER_OTA_METADATA,
        "partition_id": FLASH_COMPAT_MAP_BOOT_CONTROL_ID,
        "operation": FLASH_TRANSACTION_OPERATION_PROGRAM,
        "requested_bytes": OTA_METADATA_RECORD_SIZE,
        "processed_bytes": OTA_METADATA_RECORD_SIZE,
        "verified_bytes": OTA_METADATA_RECORD_SIZE,
        "map_version": 1,
        "completion_level": FLASH_TRANSACTION_COMPLETION_COMMITTED,
        "last_result": FLASH_TRANSACTION_RESULT_COMMITTED,
        "last_error": FLASH_TRANSACTION_ERROR_NONE,
        "abort_pending": 0,
        "erase_count_delta": 0,
        "program_count_delta": 1,
        "verify_failure_count": 0,
    }
    for field, expected in expected_metadata_transaction.items():
        if transaction[field] != expected:
            failures.append(
                f"metadata FlashTransaction {field} {transaction[field]} != {expected}"
            )
    if transaction["provider_generation"] == 0:
        failures.append("metadata FlashTransaction provider_generation is zero")
    if transaction["transaction_generation"] == 0:
        failures.append("metadata FlashTransaction transaction_generation is zero")
    if transaction["lockout_request_seq"] != transaction["lockout_ack_seq"]:
        failures.append("metadata FlashTransaction lockout request/ack sequences differ")
    if transaction["lockout_timeout_count"] != after_write["timeout_count"]:
        failures.append("metadata FlashTransaction lockout timeout snapshot is inconsistent")
    if transaction["completed_timestamp_ms"] < transaction["started_timestamp_ms"]:
        failures.append("metadata FlashTransaction completion timestamp precedes start")

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
                f"image_transaction_partition={image_transaction.get('partition_id')}",
                f"image_transaction_generation={image_transaction.get('transaction_generation')}",
                f"image_transaction_bytes={image_transaction.get('requested_bytes')}",
                f"metadata_transaction_partition={transaction['partition_id']}",
                f"metadata_transaction_generation={transaction['transaction_generation']}",
                f"metadata_transaction_bytes={transaction['requested_bytes']}",
            ] + [f"failure={failure}" for failure in failures]
        ) + "\n",
        encoding="utf-8",
    )
    print((out_dir / "summary.txt").read_text(encoding="utf-8"), end="")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
