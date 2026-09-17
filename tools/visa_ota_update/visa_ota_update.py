#!/usr/bin/env python3
"""Strict single-board VISA send/boot/commit pipeline with durable evidence."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.hardware_acceptance.sequence_trigger_acceptance import open_visa_resource, visa_command
from tools.ota_boot_commit.ota_boot_commit import slot_is_committed
from tools.ota_multi_update.ota_multi_update import read_package_build_id
from tools.ota_packager import ota_packager as package_format
from tools.scpi_common.board_identity import parse_idn_response

SEND_TOOL = ROOT / "tools/visa_ota_send/visa_ota_send.py"
COMMIT_TOOL = ROOT / "tools/ota_boot_commit/ota_boot_commit.py"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def package_info(path, expected_build):
    data = path.read_bytes()
    require(path.suffix.lower() == ".pkg" and len(data) >= package_format.PACKAGE_HEADER_SIZE,
            "expected a complete unified .pkg")
    magic, version, header, size, reserved, count = struct.unpack_from("<6I", data)
    require((magic, version, header, size, count) ==
            (package_format.PACKAGE_MAGIC, package_format.PACKAGE_VERSION,
             package_format.PACKAGE_HEADER_SIZE, len(data), 2), "invalid package header")
    build = read_package_build_id(path)
    require(build and build == expected_build, f"package build {build!r} != {expected_build!r}")
    payload_hash = hashlib.sha256(data[header:]).digest()
    require(data[144:176] == payload_hash, "package payload SHA256 mismatch")
    images = []
    previous_end = header
    for index, wanted_slot in enumerate((package_format.SLOT_A, package_format.SLOT_B)):
        slot, offset, length, crc, run_offset, flags = struct.unpack_from("<6I", data, 192 + index * 32)
        require(slot == wanted_slot and length > 0 and offset >= previous_end and
                offset % package_format.PACKAGE_PAYLOAD_ALIGNMENT == 0 and offset + length <= size,
                "invalid or overlapping package image")
        require(package_format.crc32(data[offset:offset + length]) == crc, "package image CRC mismatch")
        images.append(dict(slot=slot, offset=offset, size=length, crc32=crc,
                           run_offset=run_offset, flags=flags))
        previous_end = offset + length
    require(previous_end == size, "unexpected bytes after final image")
    return dict(path=str(path), size=size, build_id=build, sha256=hashlib.sha256(data).hexdigest(),
                crc32=package_format.crc32(data), header_reserved=reserved,
                payload_sha256=payload_hash.hex(), images=images)


def preflight(args, result):
    result.update(passed=False, started_at=timestamp(), transcript=[])
    started = time.monotonic()
    try:
        # VISA resource serial plus live IDN prevents selecting a different
        # board when the sender independently opens the same USB endpoint.
        require(args.resource.upper().startswith("USB") and
                args.serial_number in args.resource.split("::"), "VISA resource UID mismatch")
        with open_visa_resource(args.resource, args.timeout) as instrument:
            replies = {}
            for command in ("*IDN?", "SYST:FW:BUILD?"):
                record = {"command": command, "started_at": timestamp()}
                result["transcript"].append(record)
                try:
                    record["response"] = visa_command(instrument, command, args.timeout)
                    replies[command] = record["response"]
                except Exception as exc:
                    record["exception"] = f"{type(exc).__name__}: {exc}"
                    raise
                finally:
                    record["ended_at"] = timestamp()
                if command == "*IDN?":
                    identity = parse_idn_response(replies[command])
                    require(identity.serial_number == args.serial_number, "live device UID mismatch")
            build = replies["SYST:FW:BUILD?"].strip()
            require(len(build) > 2 and build.startswith('"') and build.endswith('"') and
                    build.count('"') == 2, "invalid current build response")
            build = build[1:-1]
            if args.expected_current_build is not None:
                require(build == args.expected_current_build, "current build mismatch")
            result.update(idn=identity.idn, current_build=build, serial_number=identity.serial_number, passed=True)
    finally:
        result.update(ended_at=timestamp(), elapsed_s=time.monotonic() - started)


def run_child(command, out_dir, name, result, timeout):
    result.update(command=command, passed=False, returncode=None, started_at=timestamp())
    started = time.monotonic()
    stdout_path, stderr_path = out_dir / f"{name}.stdout.log", out_dir / f"{name}.stderr.log"
    env = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUNBUFFERED="1")
    try:
        with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
            completed = subprocess.run(command, cwd=ROOT, env=env, stdout=stdout, stderr=stderr,
                                       timeout=timeout, check=False)
            result["returncode"] = completed.returncode
    except (Exception, KeyboardInterrupt) as exc:
        result["exception"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        result.update(ended_at=timestamp(), elapsed_s=time.monotonic() - started)
        for label, path in (("stdout", stdout_path), ("stderr", stderr_path)):
            if path.exists():
                result[label] = path.read_bytes().decode("utf-8", errors="replace")
                result[label + "_artifact"] = dict(path=str(path), sha256=sha256(path))
    require(result["returncode"] == 0, f"{name} exited with {result['returncode']}")


def validate_send(result, args, package):
    lines = result["stdout"].splitlines()
    idns = [line.removeprefix("idn=") for line in lines if line.startswith("idn=")]
    require(len(idns) == 1 and parse_idn_response(idns[0]).serial_number == args.serial_number,
            "sender identity evidence mismatch")
    require("boot=requested" in lines and any(line.startswith("status=") and
            "READY_TO_REBOOT" in line for line in lines), "sender lacks ready/boot evidence")
    require(f"size={package['size']}" in lines and f"crc32=0x{package['crc32']:08X}" in lines,
            "sender package size/CRC evidence mismatch")


def validate_commit(summary, args):
    require(summary.get("passed") is True and summary.get("failed") == 0 and
            summary.get("failures") == [], "commit child summary is not clean PASS")
    require(summary.get("serial_number") == args.serial_number and
            summary.get("expected_build") == args.expected_build, "commit child identity/build mismatch")
    records = summary.get("records", [])
    replies = {item["command"]: item["response"] for item in records}
    require(replies.get("SYSTem:FW:BUILD?", "").strip('"') == args.expected_build,
            "commit build readback mismatch")
    require("SYSTem:OTA:COMMit" in replies and
            slot_is_committed(replies.get("SYSTem:OTA:SLOT?", "")) and
            replies.get("SYSTem:ERRor?") == '0,"No error"', "commit final state not confirmed")
    active, _, confirmed, _, _ = (int(field.strip(), 0)
                                  for field in replies["SYSTem:OTA:SLOT?"].split(","))
    require(active in (package_format.SLOT_A, package_format.SLOT_B) and
            confirmed in (package_format.SLOT_A, package_format.SLOT_B),
            "commit final active/confirmed slot is not A or B")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("resource")
    parser.add_argument("package", type=Path)
    parser.add_argument("--serial-number", required=True)
    parser.add_argument("--expected-build", required=True)
    parser.add_argument("--expected-current-build")
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--cdc-fallback-port", default="COM3")
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--step-timeout", type=float, default=900)
    parser.add_argument("--reopen-timeout", type=float, default=45)
    args = parser.parse_args(argv)
    if any(not math.isfinite(value) or value <= 0 for value in
           (args.timeout, args.step_timeout, args.reopen_timeout)):
        parser.error("timeouts must be finite and positive")
    if not args.serial_number or not args.expected_build:
        parser.error("UID and expected build must not be empty")
    args.package = args.package.resolve()
    args.out_dir = args.out_dir.resolve()
    return args


def main(argv=None):
    args = parse_args(argv)
    args.out_dir.mkdir(parents=True, exist_ok=False)
    result = dict(board=dict(serial_number=args.serial_number, resource=args.resource),
                  send={}, commit={}, passed=False, forced_continue=False)
    report = dict(passed=False, started_at=timestamp(), results=[result], failure=None,
                  preflight={}, settings={key: str(value) if isinstance(value, Path) else value
                                          for key, value in vars(args).items()})
    started = time.monotonic()
    try:
        report["tools"] = {str(path.relative_to(ROOT)): sha256(path) for path in
            (Path(__file__), SEND_TOOL, COMMIT_TOOL,
             ROOT / "tools/ota_packager/ota_packager.py", ROOT / "tools/ota_multi_update/ota_multi_update.py",
             ROOT / "tools/hardware_acceptance/sequence_trigger_acceptance.py",
             ROOT / "tools/scpi_common/board_identity.py", ROOT / "tools/scpi_common/scpi_serial.py")}
        report["package"] = package_info(args.package, args.expected_build)
        preflight(args, report["preflight"])
        result["board"].update(idn=report["preflight"]["idn"], build_id=report["preflight"]["current_build"])
        require(sha256(args.package) == report["package"]["sha256"], "package changed after preflight")
        command = [sys.executable, str(SEND_TOOL), args.resource, str(args.package), "--boot",
                   "--timeout-ms", str(max(1, int(args.timeout * 1000)))]
        run_child(command, args.out_dir, "send", result["send"], args.step_timeout)
        validate_send(result["send"], args, report["package"])
        require(sha256(args.package) == report["package"]["sha256"], "package changed during send")
        result["send"]["passed"] = True
        commit_dir = args.out_dir / "commit"
        command = [sys.executable, str(COMMIT_TOOL), args.cdc_fallback_port, "--skip-boot",
                   "--serial-number", args.serial_number, "--expected-build", args.expected_build,
                   "--timeout", str(args.timeout), "--reopen-timeout", str(args.reopen_timeout),
                   "--out-dir", str(commit_dir)]
        summary_path = commit_dir / "summary.json"
        try:
            run_child(command, args.out_dir, "commit", result["commit"], args.step_timeout)
        finally:
            if summary_path.is_file():
                result["commit"]["summary_artifact"] = dict(path=str(summary_path), sha256=sha256(summary_path))
        result["commit"]["summary"] = child = json.loads(summary_path.read_text(encoding="utf-8"))
        validate_commit(child, args)
        require(sha256(args.package) == report["package"]["sha256"], "package changed during commit")
        require(all(sha256(ROOT / path) == expected for path, expected in report["tools"].items()),
                "pipeline tool changed during execution")
        result["commit"]["passed"] = result["passed"] = report["passed"] = True
    except (Exception, KeyboardInterrupt, SystemExit) as exc:
        report["failure"] = f"{type(exc).__name__}: {exc}"
    finally:
        report.update(ended_at=timestamp(), elapsed_s=time.monotonic() - started)
        with (args.out_dir / "summary.json").open("x", encoding="utf-8") as output:
            json.dump(report, output, indent=2, ensure_ascii=False)
            output.write("\n")
    print(json.dumps(dict(passed=report["passed"], failure=report["failure"], out_dir=str(args.out_dir))))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
