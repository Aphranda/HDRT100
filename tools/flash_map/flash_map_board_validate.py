#!/usr/bin/env python3
"""Validate the read-only FlashMap permission view on a board over SCPI."""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import open_serial_port, read_scpi_response  # noqa: E402


PERMISSIONS = {"read": 1, "write": 2, "execute": 4}
CONTEXTS = {"boot": 0, "app": 1, "factory": 2}
THERMAL_CRITICAL_MASK = (1 << 1) | (1 << 3)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM8")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout-s", type=float, default=3.0)
    parser.add_argument("--settle-s", type=float, default=1.0)
    parser.add_argument("--expected-build")
    parser.add_argument("--map", type=Path,
                        default=ROOT / "config" / "flash_map_v2.json")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def number(value: int | str) -> int:
    return value if isinstance(value, int) else int(value, 0)


def query(ser: Any, command: str, timeout_s: float,
          transcript: list[dict[str, str]]) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    response = read_scpi_response(ser, command, timeout_s, require_match=False)
    transcript.append({"command": command, "response": response})
    return response


def csv_fields(response: str) -> list[str]:
    return next(csv.reader([response], skipinitialspace=True))


def csv_ints(response: str) -> list[int]:
    return [int(field.strip(), 0) for field in csv_fields(response)]


def mask(values: list[str]) -> int:
    return sum(PERMISSIONS[value] for value in values)


def expected_app_allowed(partition: dict[str, Any], partition_id: int,
                         operation: int, active_id: int,
                         scratch_lease: bool) -> bool:
    if mask(partition["permissions"]["app"]) & operation == 0:
        return False
    if partition["id"] in {"APP_A", "APP_B"}:
        if operation == PERMISSIONS["write"]:
            return partition_id != active_id
        if operation == PERMISSIONS["execute"]:
            return partition_id == active_id
    if partition["id"] == "SCRATCH" and operation == PERMISSIONS["write"]:
        return scratch_lease
    return True


def main() -> int:
    args = parse_args()
    source = json.loads(args.map.resolve().read_text(encoding="utf-8"))
    partitions = source["partitions"]
    by_name = {partition["id"]: index
               for index, partition in enumerate(partitions)}
    active_id = by_name["APP_A"]
    out_dir = (args.out_dir or
               ROOT / "build" /
               f"flash_map_{args.port}_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    transcript: list[dict[str, str]] = []
    failures: list[str] = []
    map_snapshots: list[list[str]] = []
    access_checks = 0

    with open_serial_port(args.port, args.baud, args.timeout_s,
                          args.settle_s) as ser:
        idn = query(ser, "*IDN?", args.timeout_s, transcript)
        build = query(ser, "SYST:FW:BUILD?", args.timeout_s,
                      transcript).strip('"')
        sensors_response = query(ser, "SYST:DIAG:SENS?", args.timeout_s,
                                 transcript)
        core_before = query(ser, "SYST:CORE?", args.timeout_s, transcript)

        if idn == "<timeout>" or idn.count(",") < 3:
            failures.append(f"invalid identity response: {idn!r}")
        if args.expected_build is not None and build != args.expected_build:
            failures.append(
                f"build id {build!r} does not match {args.expected_build!r}")

        for partition_id, partition in enumerate(partitions):
            response = query(
                ser, f"SYST:DIAG:FLASH:MAP? {partition_id}",
                args.timeout_s, transcript)
            try:
                fields = csv_fields(response)
                actual = [
                    int(fields[0]), fields[1], int(fields[2]), int(fields[3]),
                    int(fields[4]), int(fields[5]), int(fields[6]),
                    int(fields[7]), int(fields[8]), int(fields[9]),
                    int(fields[10]),
                ]
                expected = [
                    source["map_version"], source["deployment_state"],
                    len(partitions), partition_id, number(partition["offset"]),
                    number(partition["size"]), number(partition["alignment"]),
                    mask(partition["permissions"]["boot"]),
                    mask(partition["permissions"]["app"]),
                    mask(partition["permissions"]["factory"]),
                    int(partition["executable"]),
                ]
                if actual != expected:
                    failures.append(
                        f"partition {partition['id']} snapshot {actual} != {expected}")
                map_snapshots.append(fields)
            except (ValueError, IndexError) as exc:
                failures.append(
                    f"partition {partition['id']} invalid snapshot {response!r}: {exc}")

        operations = tuple(PERMISSIONS.values())
        for partition_id, partition in enumerate(partitions):
            size = number(partition["size"])
            offset = number(partition["offset"])
            for context_name, context_id in CONTEXTS.items():
                base_mask = mask(partition["permissions"][context_name])
                for operation in operations:
                    expected_allowed = (base_mask & operation) != 0
                    if context_name == "app":
                        expected_allowed = expected_app_allowed(
                            partition, partition_id, operation, active_id, True)
                    for relative_offset in (0, size - 1):
                        command = (
                            "SYST:DIAG:FLASH:ACCESS? "
                            f"{partition_id},{context_id},{operation},"
                            f"{active_id},1,{relative_offset},1")
                        response = query(ser, command, args.timeout_s,
                                         transcript)
                        access_checks += 1
                        try:
                            values = csv_ints(response)
                            expected = [int(expected_allowed),
                                        offset + relative_offset, partition_id]
                            if values != expected:
                                failures.append(
                                    f"access {command!r}: {values} != {expected}")
                        except ValueError as exc:
                            failures.append(
                                f"access {command!r} invalid {response!r}: {exc}")

        extra_cases = [
            ("active_app_write", by_name["APP_A"], 1, 2, active_id, 1,
             0, 1, 0, number(partitions[by_name["APP_A"]]["offset"])),
            ("inactive_app_write", by_name["APP_B"], 1, 2, active_id, 1,
             0, 1, 1, number(partitions[by_name["APP_B"]]["offset"])),
            ("inactive_app_execute", by_name["APP_B"], 1, 4, active_id, 1,
             0, 1, 0, number(partitions[by_name["APP_B"]]["offset"])),
            ("scratch_without_lease", by_name["SCRATCH"], 1, 2, active_id, 0,
             0, 1, 0, number(partitions[by_name["SCRATCH"]]["offset"])),
            ("future_pool_read", by_name["FUTURE_POOL"], 1, 1, active_id, 1,
             0, 1, 0, number(partitions[by_name["FUTURE_POOL"]]["offset"])),
            ("cross_partition", by_name["APP_A"], 1, 1, active_id, 1,
             number(partitions[by_name["APP_A"]]["size"]) - 1, 2, 0, 0),
            ("zero_length", by_name["APP_A"], 1, 1, active_id, 1,
             0, 0, 0, 0),
            ("unknown_active_write", by_name["APP_B"], 1, 2, 0xFFFFFFFF, 1,
             0, 1, 0, number(partitions[by_name["APP_B"]]["offset"])),
        ]
        for (name, partition_id, context_id, operation, active_partition,
             lease, relative_offset, length, expected_allowed,
             expected_absolute) in extra_cases:
            command = (
                "SYST:DIAG:FLASH:ACCESS? "
                f"{partition_id},{context_id},{operation},{active_partition},"
                f"{lease},{relative_offset},{length}")
            response = query(ser, command, args.timeout_s, transcript)
            access_checks += 1
            try:
                values = csv_ints(response)
                expected = [expected_allowed, expected_absolute, partition_id]
                if values != expected:
                    failures.append(f"{name}: {values} != {expected}")
            except ValueError as exc:
                failures.append(f"{name} invalid {response!r}: {exc}")

        time.sleep(1.0)
        core_after = query(ser, "SYST:CORE?", args.timeout_s, transcript)
        error = query(ser, "SYST:ERR?", args.timeout_s, transcript)

    sensor_values: list[int] = []
    try:
        sensor_values = csv_ints(sensors_response)
        if len(sensor_values) < 22:
            failures.append(f"sensor snapshot has {len(sensor_values)} fields")
        elif sensor_values[2] & THERMAL_CRITICAL_MASK:
            failures.append(
                f"thermal critical flag set: 0x{sensor_values[2]:08X}")
    except ValueError as exc:
        failures.append(f"invalid sensor snapshot {sensors_response!r}: {exc}")

    try:
        before_values = csv_ints(core_before)
        after_values = csv_ints(core_after)
        if (len(before_values) < 3 or len(after_values) < 3 or
                before_values[0] != 1 or after_values[0] != 1 or
                after_values[2] <= before_values[2]):
            failures.append(
                f"core1 heartbeat did not grow: {core_before!r} -> {core_after!r}")
    except ValueError as exc:
        failures.append(f"invalid core snapshot: {exc}")
    if not error.startswith("0,") and "No error" not in error:
        failures.append(f"final SCPI error queue is not empty: {error!r}")

    summary = {
        "passed": not failures,
        "port": args.port,
        "identity": idn,
        "build": build,
        "map_version": source["map_version"],
        "deployment_state": source["deployment_state"],
        "partition_count": len(map_snapshots),
        "access_checks": access_checks,
        "sensors": {
            "raw": sensors_response,
            "flags": sensor_values[2] if len(sensor_values) > 2 else None,
            "board_mdeg_c": sensor_values[8] if len(sensor_values) > 8 else None,
            "rp2350_mdeg_c": sensor_values[11] if len(sensor_values) > 11 else None,
            "current_output_uv": sensor_values[13] if len(sensor_values) > 13 else None,
            "current_nominal_ma": sensor_values[14] if len(sensor_values) > 14 else None,
            "current_calibrated": sensor_values[15] if len(sensor_values) > 15 else None,
            "current_frontend_healthy": sensor_values[19] if len(sensor_values) > 19 else None,
        },
        "core_before": core_before,
        "core_after": core_after,
        "error_queue": error,
        "failures": failures,
    }
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (out_dir / "transcript.json").write_text(
        json.dumps(transcript, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")

    print(f"passed={summary['passed']}")
    print(f"build={build}")
    print(f"partitions={len(map_snapshots)}/{len(partitions)}")
    print(f"access_checks={access_checks}")
    if len(sensor_values) >= 20:
        print(f"board_temp_c={sensor_values[8] / 1000:.3f}")
        print(f"rp2350_temp_c={sensor_values[11] / 1000:.3f}")
        print(f"current_output_uv={sensor_values[13]}")
        print(f"current_nominal_ma={sensor_values[14]}")
        print(f"current_calibrated={sensor_values[15]}")
        print(f"current_frontend_healthy={sensor_values[19]}")
    print(f"out_dir={out_dir}")
    for failure in failures:
        print(f"failure={failure}")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
