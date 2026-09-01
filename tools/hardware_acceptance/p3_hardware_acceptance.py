#!/usr/bin/env python3
"""Run and gate the mandatory calibration-to-DPLL hardware acceptance.

``run`` is the firmware-change path: it builds the current working source,
updates all configured boards, then executes the complete acceptance.
``resume`` is the host-only/runtime-recovery path: it reuses an existing
package and OTA record, verifies the live build on every board, and continues
the same acceptance without building or updating firmware. ``check-staged``
is intentionally hardware-free: it rejects a code commit unless the receipt
in the Git index matches the complete staged source fingerprint and every
referenced local evidence digest remains intact.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG = Path("config/hardware_acceptance/p3_bench.json")
DEFAULT_RECEIPT = Path("config/hardware_acceptance/p3_acceptance_receipt.json")
RECEIPT_SCHEMA = "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_V4"
TDMA_RECEIPT_SCHEMA = "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_TDMA_4NODE_V2"
LIMITED_RECEIPT_SCHEMA = "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_10MHZ_LIMITED_V1"
SOURCE_ROOTS = {
    ".githooks", "application", "boards", "bootloader", "cmake",
    "components", "config", "drivers", "linker", "middleware", "osal",
    "platform", "tests", "tools",
    "third_party",
}
SOURCE_ROOT_FILES = {
    "CMakeLists.txt", "CMakePresets.json", "pico_sdk_import.cmake",
    "pytest.ini",
}
SOURCE_EXCLUDES = {DEFAULT_RECEIPT.as_posix()}
SCHEDULE_HEADER_FIELDS = 8
SCHEDULE_PHASE_FIELDS = 11
DEFAULT_ACCEPTANCE_TIMING = {
    "serial_timeout_s": 3.0,
    "serial_settle_s": 0.05,
    "action_timeout_s": 0.5,
    "p3_capture_timeout_s": 3.0,
    "phase_gap_s": 0.1,
    "status_poll_interval_s": 0.05,
    "serial_read_timeout_s": 0.02,
    "board_reset_timeout_s": 15.0,
    "board_reset_poll_interval_s": 0.5,
}


class AcceptanceError(RuntimeError):
    """A mandatory acceptance condition was not met."""


_TIMING_PATH: Path | None = None
_TIMING_EVENTS: list[dict[str, Any]] = []


def _start_timing_probe(path: Path) -> None:
    """Start the host-side acceptance probe; never runs on the realtime path."""
    global _TIMING_PATH, _TIMING_EVENTS
    _TIMING_PATH = path
    _TIMING_EVENTS = []
    _write_timing_probe()


def _write_timing_probe() -> None:
    if _TIMING_PATH is None:
        return
    _TIMING_PATH.parent.mkdir(parents=True, exist_ok=True)
    _TIMING_PATH.write_text(
        json.dumps({
            "schema": "HAOFV_HARDWARE_ACCEPTANCE_TIMING_PROBE_V1",
            "events": _TIMING_EVENTS,
        }, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")


def _record_timing_event(event: dict[str, Any]) -> None:
    _TIMING_EVENTS.append(event)
    _write_timing_probe()


def acceptance_timing(config: dict[str, Any]) -> dict[str, float]:
    """Resolve one explicit host I/O timing contract for every phase."""
    raw = config.get("hardware_acceptance_timing", {})
    if not isinstance(raw, dict):
        raise AcceptanceError("hardware_acceptance_timing must be an object")
    result: dict[str, float] = {}
    for name, default in DEFAULT_ACCEPTANCE_TIMING.items():
        value = raw.get(name, default)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise AcceptanceError(f"invalid acceptance timing {name}: {value!r}")
        value = float(value)
        if value <= 0.0:
            raise AcceptanceError(f"acceptance timing {name} must be > 0")
        result[name] = value
    # Preserve the existing tool-facing names while recording the contract in
    # terms of direction.  New configs may use the explicit names; legacy
    # configs continue to map serial_settle/phase_gap to them.
    for alias, legacy_name in (("input_settle_s", "serial_settle_s"),
                               ("output_handoff_s", "phase_gap_s")):
        value = raw.get(alias, result[legacy_name])
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise AcceptanceError(f"invalid acceptance timing {alias}: {value!r}")
        value = float(value)
        if value <= 0.0:
            raise AcceptanceError(f"acceptance timing {alias} must be > 0")
        result[alias] = value
        result[legacy_name] = value
    if result["serial_read_timeout_s"] > result["serial_timeout_s"]:
        raise AcceptanceError(
            "serial_read_timeout_s must not exceed serial_timeout_s")
    if result["action_timeout_s"] > result["serial_timeout_s"]:
        raise AcceptanceError("action_timeout_s must not exceed serial_timeout_s")
    return result


def add_serial_timing(command: list[str], timing: dict[str, float], *,
                      action: bool = False, capture: bool = False,
                      gap: bool = False) -> None:
    """Append the common deterministic serial timing arguments to a tool."""
    command.extend(["--timeout", str(timing["serial_timeout_s"]),
                    "--settle", str(timing["input_settle_s"])])
    if action:
        command.extend(["--action-timeout", str(timing["action_timeout_s"])])
    if capture:
        command.extend(["--capture-timeout",
                        str(timing["p3_capture_timeout_s"])])
    if gap:
        command.extend(["--gap", str(timing["output_handoff_s"])])


def calibration_probe_phase_cycles(config: dict[str, Any],
                                   level: int) -> int:
    """Return the stopped probe phase selected for one operating profile.

    The phase is expressed in 250 MHz system-clock cycles. It cannot be
    shared across the frequency ladder: the physical follower requires the
    phase plus its fixed SCK re-arm budget to fit inside one half-period.
    """
    by_level = config.get("calibration_probe_phase_cycles_by_level")
    if not isinstance(by_level, dict) or str(level) not in by_level:
        raise AcceptanceError(
            f"missing calibration probe phase for profile level {level}")
    phase = by_level[str(level)]
    if (not isinstance(phase, int) or isinstance(phase, bool) or
            not 1 <= phase <= 31):
        raise AcceptanceError(
            f"invalid calibration probe phase for profile level {level}: "
            f"{phase!r}")
    return phase


def calibration_coded_probe_phase_cycles(config: dict[str, Any],
                                          level: int) -> list[int]:
    """Return the ordered P2 phase candidates for one operating profile."""
    by_level = config.get("calibration_coded_probe_phase_cycles_by_level")
    if by_level is None:
        return [calibration_probe_phase_cycles(config, level)]
    if not isinstance(by_level, dict) or str(level) not in by_level:
        raise AcceptanceError(
            f"missing coded calibration probe phases for profile level {level}")
    phases = by_level[str(level)]
    if (not isinstance(phases, list) or not phases or
            any(not isinstance(phase, int) or isinstance(phase, bool) or
                not 1 <= phase <= 31 for phase in phases) or
            len(set(phases)) != len(phases)):
        raise AcceptanceError(
            f"invalid coded calibration probe phases for profile level "
            f"{level}: {phases!r}")
    return phases


def _run_git(root: Path, *args: str, input_bytes: bytes | None = None) -> bytes:
    result = subprocess.run(
        ["git", *args], cwd=root, input=input_bytes, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        raise AcceptanceError(
            f"git {' '.join(args)} failed: "
            f"{result.stderr.decode('utf-8', errors='replace').strip()}")
    return result.stdout


def is_acceptance_source(path: str) -> bool:
    normalized = path.replace("\\", "/")
    while normalized.startswith("./"):
        normalized = normalized[2:]
    if normalized in SOURCE_EXCLUDES:
        return False
    item = Path(normalized)
    if normalized in SOURCE_ROOT_FILES:
        return True
    if not item.parts or item.parts[0] not in SOURCE_ROOTS:
        return False
    return True


def _tree_digest(rows: Iterable[tuple[str, str, str]]) -> tuple[str, int]:
    digest = hashlib.sha256()
    count = 0
    for path, mode, blob_id in sorted(rows, key=lambda row: row[0]):
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(mode.encode("ascii"))
        digest.update(b"\0")
        digest.update(blob_id.encode("ascii"))
        digest.update(b"\n")
        count += 1
    return digest.hexdigest(), count


def working_source_fingerprint(root: Path = ROOT) -> tuple[str, int]:
    entries = {
        path: (mode, oid) for path, mode, oid in _index_entries(root)
    }
    raw = _run_git(
        root, "ls-files", "--modified", "--deleted", "--others",
        "--exclude-standard", "-z")
    changed = sorted({
        item.decode("utf-8", errors="surrogateescape")
        for item in raw.split(b"\0") if item
    })
    for path in changed:
        normalized = path.replace("\\", "/")
        if not is_acceptance_source(normalized):
            continue
        full = root / path
        if not full.is_file():
            entries.pop(normalized, None)
            continue
        blob_id = _run_git(
            root, "hash-object", f"--path={normalized}", normalized
        ).decode("ascii").strip()
        mode = entries.get(normalized, ("100644", ""))[0]
        entries[normalized] = (mode, blob_id)
    return _tree_digest(
        (path, mode, oid) for path, (mode, oid) in entries.items())


def _index_entries(root: Path) -> list[tuple[str, str, str]]:
    raw = _run_git(root, "ls-files", "--stage", "-z")
    entries = []
    for entry in raw.split(b"\0"):
        if not entry or b"\t" not in entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        fields = metadata.split()
        if len(fields) != 3 or fields[2] != b"0":
            continue
        path = raw_path.decode("utf-8", errors="surrogateescape")
        if is_acceptance_source(path):
            entries.append((path.replace("\\", "/"),
                            fields[0].decode("ascii"),
                            fields[1].decode("ascii")))
    return sorted(entries)


def staged_source_fingerprint(root: Path = ROOT) -> tuple[str, int]:
    entries = _index_entries(root)
    return _tree_digest(entries)


def changed_staged_sources(root: Path = ROOT) -> list[str]:
    raw = _run_git(root, "diff", "--cached", "--name-only", "-z")
    return sorted(
        path for path in (
            item.decode("utf-8", errors="surrogateescape")
            for item in raw.split(b"\0") if item)
        if is_acceptance_source(path))


def unstaged_sources(root: Path = ROOT) -> list[str]:
    raw = _run_git(root, "diff", "--name-only", "-z")
    return sorted(
        path for path in (
            item.decode("utf-8", errors="surrogateescape")
            for item in raw.split(b"\0") if item)
        if is_acceptance_source(path))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_index_json(root: Path, path: Path) -> dict[str, Any]:
    raw = _run_git(root, "show", f":{path.as_posix()}")
    try:
        value = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise AcceptanceError(f"invalid staged receipt {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise AcceptanceError(f"staged receipt {path} is not an object")
    return value


def _validate_evidence(root: Path, record: dict[str, Any], *,
                       include_dpll: bool = True) -> None:
    names = (
            "firmware_package", "ota_summary", "topology_summary",
            "coarse_calibration_summary", "coded_calibration_summary",
            "initialization_reset", "p3_summary", "trn00_summary",
            "trn01_summary", "trn02_summary",
            "trn03_matrix", "tdma_summary")
    if include_dpll:
        names += ("sma_observer_wiring", "dpll_summary")
    for name in names:
        evidence = record.get(name)
        if not isinstance(evidence, dict):
            raise AcceptanceError(f"receipt missing {name}")
        relative = evidence.get("path")
        expected = evidence.get("sha256")
        if not isinstance(relative, str) or not isinstance(expected, str):
            raise AcceptanceError(f"receipt has invalid {name} evidence")
        path = root / relative
        if not path.is_file():
            raise AcceptanceError(f"local {name} evidence missing: {relative}")
        if sha256_file(path) != expected:
            raise AcceptanceError(f"local {name} evidence digest changed: {relative}")
    timing = record.get("timing_probe")
    if timing is not None:
        if not isinstance(timing, dict):
            raise AcceptanceError("receipt has invalid timing_probe evidence")
        relative = timing.get("path")
        expected = timing.get("sha256")
        path = root / relative if isinstance(relative, str) else None
        if (path is None or not isinstance(expected, str) or
                not path.is_file() or sha256_file(path) != expected):
            raise AcceptanceError("local timing_probe evidence is missing or changed")


def _validate_limited_10mhz_evidence(root: Path,
                                     record: dict[str, Any]) -> None:
    """Validate the intentionally reduced bring-up receipt.

    This gate proves only OTA plus the 10 MHz P3 matrix.  It is deliberately
    not accepted as the full calibration-to-DPLL receipt.
    """
    for name in ("firmware_package", "ota_summary", "p3_summary"):
        evidence = record.get(name)
        if not isinstance(evidence, dict):
            raise AcceptanceError(f"limited receipt missing {name}")
        relative = evidence.get("path")
        expected = evidence.get("sha256")
        if not isinstance(relative, str) or not isinstance(expected, str):
            raise AcceptanceError(f"limited receipt has invalid {name} evidence")
        path = root / relative
        if not path.is_file() or sha256_file(path) != expected:
            raise AcceptanceError(f"limited {name} evidence is missing or changed: {relative}")
    p3 = json.loads((root / record["p3_summary"]["path"]).read_text(
        encoding="utf-8"))
    if (p3.get("passed") is not True or
            p3.get("frequency_ladder_mhz") != [10] or
            p3.get("frequency_policy", {}).get(
                "highest_stable_frequency_mhz") != 10 or
            any(trial.get("passed") is not True
                for trial in p3.get("trials", []))):
        raise AcceptanceError("limited receipt P3 evidence is not a passing 10 MHz matrix")
    ota = json.loads((root / record["ota_summary"]["path"]).read_text(
        encoding="utf-8"))
    validate_ota(ota, list(record["ota_board_ids"]), str(record["build_id"]))


def check_staged(root: Path, receipt_path: Path) -> None:
    changed = changed_staged_sources(root)
    if not changed:
        print("OK   Latency Cal hardware acceptance: no staged code change")
        return
    dirty = unstaged_sources(root)
    if dirty:
        raise AcceptanceError(
            "unstaged code differs from the staged commit; rerun acceptance "
            "after resolving: " + ", ".join(dirty[:8]))
    receipt = read_index_json(root, receipt_path)
    schema = receipt.get("schema")
    if schema not in (RECEIPT_SCHEMA, TDMA_RECEIPT_SCHEMA,
                      LIMITED_RECEIPT_SCHEMA) or \
            receipt.get("passed") is not True:
        raise AcceptanceError("staged hardware acceptance receipt is not PASS")
    fingerprint, file_count = staged_source_fingerprint(root)
    if receipt.get("source_tree_sha256") != fingerprint:
        raise AcceptanceError(
            "staged code fingerprint has no matching hardware acceptance; run "
            "python tools/hardware_acceptance/p3_hardware_acceptance.py run")
    if receipt.get("source_file_count") != file_count:
        raise AcceptanceError("P3 acceptance source file count mismatch")
    if schema == LIMITED_RECEIPT_SCHEMA:
        if receipt.get("acceptance_scope") != "10MHZ_LIMITED_P3":
            raise AcceptanceError("limited receipt has invalid acceptance scope")
        _validate_limited_10mhz_evidence(root, receipt)
    elif schema == TDMA_RECEIPT_SCHEMA:
        if receipt.get("acceptance_scope") != "FOUR_NODE_TDMA":
            raise AcceptanceError("TDMA-only receipt has invalid acceptance scope")
        if receipt.get("tdma_board_ids") != receipt.get("ota_board_ids"):
            raise AcceptanceError("TDMA-only receipt board sets differ")
        _validate_evidence(root, receipt, include_dpll=False)
    else:
        _validate_evidence(root, receipt)
    print(
        f"OK   hardware acceptance: sources={file_count} "
        f"build={receipt.get('build_id')} trials={receipt.get('trial_count')} "
        f"scope={receipt.get('acceptance_scope', 'FULL')}")


def write_limited_receipt(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    config_path = root / args.config
    package = (root / args.package).resolve()
    ota_summary_path = (root / args.ota_summary).resolve()
    p3_summary_path = (root / args.p3_summary).resolve()
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if config.get("frequency_ladder_mhz") != [10]:
        raise AcceptanceError("limited receipt requires config frequency ladder [10]")
    p3 = json.loads(p3_summary_path.read_text(encoding="utf-8"))
    if (p3.get("passed") is not True or p3.get("frequency_ladder_mhz") != [10] or
            p3.get("frequency_policy", {}).get(
                "highest_stable_frequency_mhz") != 10):
        raise AcceptanceError("P3 summary is not a passing 10 MHz result")
    build_id = _read_package_build_id(package)
    ota = json.loads(ota_summary_path.read_text(encoding="utf-8"))
    validate_ota(ota, list(config["ota_board_ids"]), build_id)
    fingerprint, file_count = staged_source_fingerprint(root)
    trials = p3.get("trials", [])
    delays = [float(row["delay_estimate_ns"]) for row in trials
              if "delay_estimate_ns" in row]
    receipt = {
        "schema": LIMITED_RECEIPT_SCHEMA,
        "acceptance_scope": "10MHZ_LIMITED_P3",
        "passed": True,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_tree_sha256": fingerprint,
        "source_file_count": file_count,
        "bench_config_path": args.config.as_posix(),
        "bench_config_sha256": sha256_file(config_path),
        "build_id": build_id,
        "firmware_package": evidence_entry(root, package),
        "ota_summary": evidence_entry(
            root, ota_summary_path, board_count=len(config["ota_board_ids"])),
        "p3_summary": evidence_entry(root, p3_summary_path),
        "ota_board_ids": list(config["ota_board_ids"]),
        "board_ids_in_physical_order": list(
            config["p3_board_ids_in_physical_order"]),
        "frequency_ladder_mhz": [10],
        "trial_count": len(trials),
        "delay_min_ns": min(delays) if delays else None,
        "delay_max_ns": max(delays) if delays else None,
        "highest_stable_frequency_mhz": 10,
        "hardware_acceptance_timing": acceptance_timing(config),
    }
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(
        json.dumps(receipt, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"PASS limited 10 MHz hardware acceptance receipt={args.receipt}")


def _run_step(command: list[str], root: Path, log_path: Path, *,
              allow_failure: bool = False) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment["PYTHONIOENCODING"] = "utf-8"
    # Several calibration actions intentionally time out before their delayed
    # acknowledgement reaches USB CDC.  A fresh session for the following
    # readback prevents that delayed response from poisoning a TDMA status
    # query.  Individual tools may still opt into --keep-open where the whole
    # transaction has one explicit serial owner.
    environment["HAOFV_ACCEPTANCE_PERSISTENT_SESSIONS"] = "0"
    environment.setdefault("HAOFV_SERIAL_READ_TIMEOUT_S", "0.02")
    started = datetime.now(timezone.utc)
    monotonic_start = time.perf_counter()
    result = subprocess.run(
        command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=environment, check=False)
    ended = datetime.now(timezone.utc)
    _record_timing_event({
        "action": Path(str(command[1])).stem if len(command) > 1 else str(command[0]),
        "argv": [str(value) for value in command],
        "started_at_utc": started.isoformat(),
        "ended_at_utc": ended.isoformat(),
        "duration_ms": round((time.perf_counter() - monotonic_start) * 1000.0, 3),
        "returncode": result.returncode,
        "status": "PASS" if result.returncode == 0 else "FAIL",
    })
    log_path.write_bytes(result.stdout)
    if result.returncode != 0 and not allow_failure:
        tail = result.stdout.decode("utf-8", errors="replace").splitlines()[-20:]
        raise AcceptanceError(
            f"step failed ({' '.join(command)}):\n" + "\n".join(tail))
    return int(result.returncode)


def _read_package_build_id(path: Path) -> str:
    data = path.read_bytes()[:512]
    if len(data) < 512 or int.from_bytes(data[0:4], "little") != 0x474B5054:
        raise AcceptanceError(f"invalid OTA package: {path}")
    return data[112:144].split(b"\0", 1)[0].split(b"\xff", 1)[0].decode(
        "ascii", errors="ignore").strip()


def parse_schedule(raw: str) -> dict[str, int]:
    try:
        values = [int(value.strip().strip('"'), 0)
                  for value in next(csv.reader([raw]), [])]
    except ValueError as exc:
        raise AcceptanceError(f"invalid TDMA schedule: {raw!r}") from exc
    if len(values) < SCHEDULE_HEADER_FIELDS:
        raise AcceptanceError("truncated TDMA schedule")
    phase_count = values[3]
    expected = SCHEDULE_HEADER_FIELDS + phase_count * SCHEDULE_PHASE_FIELDS
    if len(values) != expected:
        raise AcceptanceError(
            f"invalid TDMA schedule shape: {len(values)} != {expected}")
    return {
        "enabled_mask": values[4],
        "quarantined_mask": values[5],
        "cycle_count": values[6],
        "schedule_miss_count": values[7],
    }


def read_schedules(board_ids: list[str], timing: dict[str, float]) -> dict[str, dict[str, int]]:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import (  # type: ignore
        board_command, close_persistent_connections, discover)

    args = argparse.Namespace(
        board_ids=board_ids, baud=115200,
        timeout=timing["serial_timeout_s"], settle=timing["serial_settle_s"],
        keep_open=True)
    boards = discover(args)
    missing = sorted(set(board_ids) - set(boards))
    if missing:
        raise AcceptanceError("schedule boards missing: " + ", ".join(missing))
    try:
        return {
            board_id: parse_schedule(board_command(
                boards[board_id], "SYSTem:TDMA:SCHEDule?", args))
            for board_id in board_ids
        }
    finally:
        # Do not hold a CDC handle while the next acceptance phase opens it.
        close_persistent_connections()


def validate_ota(summary: dict[str, Any], expected_ids: list[str],
                 build_id: str) -> None:
    found = {board.get("serial_number") for board in summary.get("boards", [])}
    if (summary.get("passed") is not True or summary.get("dry_run") is True or
            summary.get("failed_count") != 0 or
            summary.get("board_count") != len(expected_ids) or
            summary.get("updated_count") != len(expected_ids) or
            found != set(expected_ids) or summary.get("expected_build") != build_id):
        raise AcceptanceError("five-board OTA summary did not meet acceptance")


def validate_online_builds(builds: dict[str, str], expected_ids: list[str],
                           build_id: str) -> None:
    """Require the exact bench board set to be running the expected package."""
    if set(builds) != set(expected_ids):
        missing = sorted(set(expected_ids) - set(builds))
        unexpected = sorted(set(builds) - set(expected_ids))
        raise AcceptanceError(
            f"online board set mismatch: missing={missing}, "
            f"unexpected={unexpected}")
    mismatched = {
        board_id: observed for board_id, observed in builds.items()
        if observed != build_id
    }
    if mismatched:
        raise AcceptanceError(
            f"online boards do not run expected build {build_id}: {mismatched}")


def read_online_builds(board_ids: list[str], timing: dict[str, float]) -> dict[str, str]:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import discover  # type: ignore

    probe_args = argparse.Namespace(
        board_ids=board_ids, baud=115200,
        timeout=timing["serial_timeout_s"], settle=timing["serial_settle_s"],
        keep_open=True)
    boards = discover(probe_args)
    return {board_id: board.build for board_id, board in boards.items()}


def reset_acceptance_boards(board_ids: list[str], build_id: str,
                            timing: dict[str, float]) -> dict[str, Any]:
    """Software-reset one exact UID set and verify clean re-enumeration."""
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import (  # type: ignore
        board_command, close_persistent_connections, discover)

    reset_args = argparse.Namespace(
        board_ids=board_ids,
        baud=115200,
        timeout=timing["serial_timeout_s"],
        action_timeout=timing["action_timeout_s"],
        settle=timing["input_settle_s"],
        read_timeout=timing["serial_read_timeout_s"],
        keep_open=False,
        short_open=True,
    )
    boards = discover(reset_args)
    before_builds = {
        board_id: board.build for board_id, board in boards.items()}
    validate_online_builds(before_builds, board_ids, build_id)

    started = time.monotonic()
    responses: dict[str, str] = {}
    ports_before: dict[str, str] = {}
    try:
        for board_id in board_ids:
            board = boards[board_id]
            ports_before[board_id] = board.port
            responses[board_id] = board_command(
                board, "SYSTem:BOOT:RESet", reset_args)
    except Exception as exc:
        raise AcceptanceError(
            f"acceptance initialization reset failed: {exc}") from exc
    finally:
        close_persistent_connections()

    deadline = time.monotonic() + timing["board_reset_timeout_s"]
    rebooted: dict[str, Any] = {}
    while time.monotonic() < deadline:
        time.sleep(timing["board_reset_poll_interval_s"])
        rebooted = discover(reset_args)
        if set(rebooted) == set(board_ids):
            break
    after_builds = {
        board_id: board.build for board_id, board in rebooted.items()}
    try:
        validate_online_builds(after_builds, board_ids, build_id)
    except AcceptanceError as exc:
        raise AcceptanceError(
            "boards did not re-enumerate cleanly after initialization reset: "
            f"{exc}") from exc
    return {
        "schema": "HAOFV_HARDWARE_ACCEPTANCE_INITIALIZATION_RESET_V1",
        "passed": True,
        "command": "SYSTem:BOOT:RESet",
        "board_ids": list(board_ids),
        "expected_build": build_id,
        "ports_before": ports_before,
        "ports_after": {
            board_id: rebooted[board_id].port for board_id in board_ids},
        "builds_before": before_builds,
        "builds_after": after_builds,
        "responses": responses,
        "elapsed_s": time.monotonic() - started,
    }


def validate_p3(summary: dict[str, Any], config: dict[str, Any]) -> dict[str, Any]:
    trials = summary.get("trials", [])
    expected_trials = (
        len(config["p3_board_ids_in_physical_order"]) *
        len(config["frequency_ladder_mhz"]) * 2 * int(config["repeats"]))
    policy = summary.get("frequency_policy", {})
    delays = [float(trial["delay_estimate_ns"]) for trial in trials
              if "delay_estimate_ns" in trial]
    if (summary.get("passed") is not True or len(trials) != expected_trials or
            any(trial.get("passed") is not True for trial in trials) or
            policy.get("stable_profiles_passed") is not True or
            policy.get("highest_stable_frequency_mhz") !=
            config["stable_frequency_mhz"] or len(delays) != expected_trials):
        raise AcceptanceError("four-board P3 matrix did not meet acceptance")
    minimum = float(config["minimum_link_delay_ns"])
    maximum = float(config["maximum_link_delay_ns"])
    if min(delays) < minimum or max(delays) > maximum:
        raise AcceptanceError(
            f"P3 delay outside configured bench range: {min(delays)}..{max(delays)}")
    for trial in trials:
        for endpoint in ("initiator", "responder"):
            snapshot = trial.get(endpoint, {})
            if snapshot.get("dma_overrun_count") != 0 or snapshot.get("pio_stall_count") != 0:
                raise AcceptanceError(f"P3 {endpoint} DMA/PIO fault observed")
    return {
        "trial_count": expected_trials,
        "delay_min_ns": min(delays),
        "delay_max_ns": max(delays),
        "highest_stable_frequency_mhz":
            policy["highest_stable_frequency_mhz"],
    }


def validate_pass_summary(summary: dict[str, Any], label: str) -> None:
    if summary.get("passed") is not True:
        detail = summary.get("error") or summary.get("gate_failures") or ""
        raise AcceptanceError(f"{label} did not meet acceptance: {detail}")


def build_diagnostic_feedback(
        tdma_summary: dict[str, Any],
        dpll_summary: dict[str, Any] | None,
        trn03_matrix: dict[str, Any]) -> dict[str, Any]:
    """Collect failed-gate measurements that can drive DPLL correction."""
    runtime_fields = (
        "ring_adapter_rx_count", "ring_adapter_rx_bad_count",
        "ring_adapter_rx_transport_bad_count",
        "ring_adapter_rx_schedule_bad_count",
        "ring_adapter_rx_profile_bad_count",
        "ring_adapter_last_bad_transport_result",
        "ring_adapter_last_bad_sequence",
        "ring_adapter_last_bad_header_diff_count",
        "ring_adapter_last_bad_header_first_diff_offset",
        "ring_adapter_last_bad_header_expected_byte",
        "ring_adapter_last_bad_header_observed_byte",
    )
    phase_fields = (
        "flight_marker_offset_sample_count",
        "flight_sck_offset_sample_count",
        "flight_data_offset_sample_count",
        "flight_marker_phase_delay_cycles",
        "flight_sck_phase_delay_cycles",
        "flight_data_phase_delay_cycles",
    )
    process_fields = (
        "receive_accepted_count", "receive_rejected_count",
        "receive_missing_count", "rx_bitmap_incomplete_count",
        "receive_last_transport_result", "receive_quality_flags",
    )
    node_feedback: dict[str, Any] = {}
    for address, node in tdma_summary.get("nodes", {}).items():
        runtime = node.get("runtime_after", {})
        physical = node.get("physical_after", {})
        process = node.get("flight_after", {}).get("process", {})
        node_feedback[address] = {
            "node_index": node.get("node_index"),
            "passed": bool(node.get("passed")),
            "errors": list(node.get("errors", [])),
            "runtime": {field: runtime.get(field) for field in runtime_fields},
            "physical_phase": {
                field: physical.get(field) for field in phase_fields},
            "process_image": {
                field: process.get(field) for field in process_fields},
            "crc_diagnostic": node.get("crc_diagnostic_after", {}),
        }
    dpll_boards = [] if dpll_summary is None else dpll_summary.get("boards", [])
    observer = next((row for row in dpll_boards
                     if row.get("role") == "observer"), {})
    return {
        "calibration_offsets": {
            "active_row_id": trn03_matrix.get(
                "offset_matrix", {}).get("active_row_id"),
            "active_row": tdma_summary.get("offset_row", {}),
            "sck_replay_selection": trn03_matrix.get(
                "derivation", {}).get("sck_replay_selection", {}),
        },
        "tdma": {
            "startup_barrier": tdma_summary.get("startup_barrier", {}),
            "worst_receive_quality": tdma_summary.get(
                "soak_validation", {}).get("worst_receive_quality", {}),
            "nodes": node_feedback,
            "dpll_schedule_gate": tdma_summary.get("dpll_schedule_gate", {}),
        },
        "dpll": {
            "ring_sequence_consistent": (
                None if dpll_summary is None else
                dpll_summary.get("ring_sequence_consistent")),
            "ring_sequence_skew": (
                None if dpll_summary is None else
                dpll_summary.get("ring_sequence_skew")),
            "observer_phase": observer,
            "boards": dpll_boards,
        },
    }


def validate_sma_observer_topology(
        summary: dict[str, Any], config: dict[str, Any]) -> list[dict[str, int]]:
    """Require the measured five-board SMA routes to match the bench wiring."""
    validate_pass_summary(summary, "five-board bidirectional SMA wiring")
    expected = config.get("sma_observer_routes")
    observed = summary.get("wire_order", {}).get("routes")
    route_fields = (
        "node_no", "node_output_channel", "validator_input_channel",
        "validator_output_channel", "node_input_channel",
    )
    if not isinstance(expected, list) or not isinstance(observed, list):
        raise AcceptanceError("SMA observer route contract is missing")

    def normalize(routes: list[Any], label: str) -> list[dict[str, int]]:
        normalized: list[dict[str, int]] = []
        for route in routes:
            if not isinstance(route, dict) or any(
                    isinstance(route.get(field), bool) or
                    not isinstance(route.get(field), int)
                    for field in route_fields):
                raise AcceptanceError(
                    f"invalid {label} SMA observer route: {route!r}")
            normalized.append({
                field: int(route[field]) for field in route_fields
            })
        return sorted(normalized, key=lambda route: route["node_no"])

    expected_routes = normalize(expected, "configured")
    observed_routes = normalize(observed, "measured")
    if observed_routes != expected_routes:
        raise AcceptanceError(
            f"measured SMA topology differs from bench contract: "
            f"{observed_routes} != {expected_routes}")

    identities = summary.get("identities", {})
    expected_ids = list(config.get("p3_board_ids_in_physical_order", []))
    expected_ids.append(str(config.get("dpll_observer_board_id", "")))
    measured_ids = [
        identities.get(str(board_no), identities.get(board_no, {})).get(
            "address")
        for board_no in range(1, 6)
    ] if isinstance(identities, dict) else []
    if measured_ids != expected_ids:
        raise AcceptanceError(
            f"SMA topology board identities differ from bench contract: "
            f"{measured_ids} != {expected_ids}")
    return observed_routes


def p3_link_delays(summary: dict[str, Any], config: dict[str, Any]) -> list[int]:
    """Reduce stable CLK_DATA P3 repeats to even per-link training delays."""
    board_ids = list(config["p3_board_ids_in_physical_order"])
    stable_hz = int(config["stable_frequency_mhz"]) * 1_000_000
    delays: list[int] = []
    for link_index, source in enumerate(board_ids):
        destination = board_ids[(link_index + 1) % len(board_ids)]
        values = [
            float(trial["delay_estimate_ns"])
            for trial in summary.get("trials", [])
            if trial.get("source") == source and
            trial.get("destination") == destination and
            int(trial.get("signal_group", -1)) == 0 and
            int(trial.get("frequency_hz", 0)) == stable_hz and
            trial.get("passed") is True
        ]
        if len(values) != int(config["repeats"]):
            raise AcceptanceError(
                f"link{link_index}: stable P3 delay repeat set is incomplete")
        selected = int(round(sum(values) / len(values)))
        # The shared calibration phase model requires an even end-to-end
        # value because link_base_delay is exactly link_delay / 2.
        selected += selected & 1
        delays.append(selected)
    return delays


def discover_board_ports(board_ids: list[str], timing: dict[str, float]) -> dict[str, str]:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import discover  # type: ignore

    args = argparse.Namespace(
        board_ids=board_ids, baud=115200,
        timeout=timing["serial_timeout_s"], settle=timing["serial_settle_s"],
        keep_open=True)
    boards = discover(args)
    missing = sorted(set(board_ids) - set(boards))
    if missing:
        raise AcceptanceError("DPLL boards missing: " + ", ".join(missing))
    return {board_id: boards[board_id].port for board_id in board_ids}


def evidence_entry(root: Path, path: Path, **fields: Any) -> dict[str, Any]:
    relative = path.resolve().relative_to(root).as_posix()
    return {"path": relative, "sha256": sha256_file(path), **fields}


def write_phase_summary(path: Path, phase: str,
                        summaries: list[Path]) -> dict[str, Any]:
    """Freeze a multi-profile/multi-part phase as one receipt artifact."""
    rows = []
    for summary_path in summaries:
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        validate_pass_summary(summary, f"{phase}:{summary_path.parent.name}")
        rows.append({
            "path": summary_path.resolve().relative_to(ROOT).as_posix(),
            "sha256": sha256_file(summary_path),
            "passed": True,
        })
    result = {
        "schema": "HAOFV_HARDWARE_ACCEPTANCE_PHASE_SUMMARY_V1",
        "phase": phase,
        "passed": bool(rows),
        "evidence": rows,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return result


def validate_schedule_isolation(
        before: dict[str, dict[str, int]], after: dict[str, dict[str, int]],
        calibration_mask: int) -> None:
    if set(before) != set(after):
        raise AcceptanceError("schedule board set changed during P3")
    for board_id in before:
        if before[board_id]["enabled_mask"] != after[board_id]["enabled_mask"]:
            raise AcceptanceError(f"{board_id}: P3 changed TDMA load mask")
        if after[board_id]["quarantined_mask"] & calibration_mask:
            raise AcceptanceError(f"{board_id}: calibration load quarantined by P3")


def validate_runtime_schedules(
        schedules: dict[str, dict[str, int]], calibration_mask: int) -> None:
    """Gate the final running schedule without comparing it to STOPPED P3."""
    enabled = {row["enabled_mask"] for row in schedules.values()}
    if len(enabled) != 1 or enabled == {0}:
        raise AcceptanceError("final TDMA enabled load mask is inconsistent")
    for board_id, row in schedules.items():
        if row["quarantined_mask"] & calibration_mask:
            raise AcceptanceError(
                f"{board_id}: calibration load quarantined after TDMA/DPLL")
        if row["schedule_miss_count"] != 0:
            raise AcceptanceError(
                f"{board_id}: final TDMA schedule miss count is nonzero")


def run_acceptance(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    config_path = root / args.config
    receipt_path = root / args.receipt
    config = json.loads(config_path.read_text(encoding="utf-8"))
    board_ids = list(config["p3_board_ids_in_physical_order"])
    tdma_only = bool(getattr(args, "tdma_only", False))
    diagnostic_continue = bool(getattr(args, "diagnostic_continue", False))
    diagnostic_failures: list[dict[str, Any]] = []
    ota_board_ids = board_ids if tdma_only else list(config["ota_board_ids"])
    if tdma_only and len(board_ids) != 4:
        raise AcceptanceError(
            "TDMA-only acceptance requires exactly four configured ring nodes")
    timing = acceptance_timing(config)
    # Imported calibration tools resolve the same read quantum through the
    # process environment, even when their legacy CLI has no read-timeout
    # option yet.
    os.environ["HAOFV_SERIAL_READ_TIMEOUT_S"] = str(
        timing["serial_read_timeout_s"])
    fingerprint_before, source_count = working_source_fingerprint(root)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = root / (args.out_dir or Path(f"out/hardware_acceptance/p3-{stamp}"))
    out_dir.mkdir(parents=True, exist_ok=True)
    timing_probe_path = out_dir / "timing.json"
    _start_timing_probe(timing_probe_path)

    if args.command == "resume":
        package = (root / args.package).resolve()
        ota_summary_path = (root / args.ota_summary).resolve()
        if not package.is_file():
            raise AcceptanceError(f"reused firmware package missing: {package}")
        if not ota_summary_path.is_file():
            raise AcceptanceError(
                f"reused OTA summary missing: {ota_summary_path}")
        build_id = _read_package_build_id(package)
        ota_summary = json.loads(
            ota_summary_path.read_text(encoding="utf-8"))
        validate_ota(ota_summary, ota_board_ids, build_id)
        online_builds = read_online_builds(ota_board_ids, timing)
        validate_online_builds(
            online_builds, ota_board_ids, build_id)
        reuse_record = {
            "schema": "HAOFV_HARDWARE_ACCEPTANCE_FIRMWARE_REUSE_V1",
            "firmware_changed": False,
            "build_skipped": True,
            "ota_skipped": True,
            "build_id": build_id,
            "package": package.resolve().relative_to(root).as_posix(),
            "ota_summary": ota_summary_path.resolve().relative_to(root).as_posix(),
            "online_builds": online_builds,
        }
        (out_dir / "firmware-reuse.json").write_text(
            json.dumps(reuse_record, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        print(
            f"Hardware acceptance: reuse live build={build_id}; "
            "build and OTA skipped", flush=True)
    else:
        build_dir = root / (
            args.build_dir or Path(f"out/build/p3-acceptance-{stamp}"))
        print(f"P3 acceptance: build -> {build_dir}", flush=True)
        _run_step([
            sys.executable,
            str(root / "tools/cmake_build_auto/cmake_build_auto.py"),
            "--root", str(root), "--build-dir", str(build_dir),
        ], root, out_dir / "build.log")
        package = build_dir / "DHRT100_UPDATE.pkg"
        if not package.is_file():
            raise AcceptanceError(f"firmware package missing: {package}")
        build_id = _read_package_build_id(package)

        ota_dir = out_dir / ("ota-four-board-tdma" if tdma_only
                             else "ota-five-board")
        print(
            f"P3 acceptance: asynchronous OTA {len(ota_board_ids)} boards "
            f"build={build_id}",
            flush=True)
        ota_command = [
            sys.executable,
            str(root / "tools/ota_multi_update/ota_multi_update.py"),
            str(package), "--expected-board-count",
            str(len(ota_board_ids)),
            "--expected-build", build_id, "--out-dir", str(ota_dir),
        ]
        for board_id in ota_board_ids:
            ota_command.extend(["--serial-number", board_id])
        _run_step(ota_command, root, out_dir / "ota.log")
        ota_summary_path = ota_dir / "summary.json"
        ota_summary = json.loads(
            ota_summary_path.read_text(encoding="utf-8"))
        validate_ota(ota_summary, ota_board_ids, build_id)

    print(
        f"Hardware acceptance: initialize {len(ota_board_ids)} boards by "
        "software reset", flush=True)
    reset_started = datetime.now(timezone.utc)
    reset_evidence = reset_acceptance_boards(
        ota_board_ids, build_id, timing)
    reset_path = out_dir / "initialization-reset.json"
    reset_path.write_text(
        json.dumps(reset_evidence, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    _record_timing_event({
        "action": "acceptance.initialization_reset",
        "started_at_utc": reset_started.isoformat(),
        "ended_at_utc": datetime.now(timezone.utc).isoformat(),
        "duration_ms": round(float(reset_evidence["elapsed_s"]) * 1000, 3),
        "returncode": 0,
        "status": "PASS",
    })

    common_boards: list[str] = []
    for board_id in board_ids:
        common_boards.extend(["--board-id", board_id])

    topology_dir = out_dir / "p0t-topology"
    print("Hardware acceptance: P0T line order and NO assignment", flush=True)
    topology_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_ring_topology.py"),
        *common_boards,
        "--anchor-id", str(config["topology_anchor_board_id"]),
        "--expected-build", build_id,
        "--level", str(config["topology_profile_level"]),
        "--cycles", str(config["topology_probe_cycles"]),
        "--pair-wait", str(config["topology_pair_wait_s"]),
        "--adjacency-only", "--out-dir", str(topology_dir),
    ]
    add_serial_timing(topology_command, timing, action=True, gap=True)
    _run_step(topology_command, root, out_dir / "topology.log")
    topology_summary_path = topology_dir / "summary.json"
    topology_summary = json.loads(
        topology_summary_path.read_text(encoding="utf-8"))
    validate_pass_summary(topology_summary, "P0T topology")
    if topology_summary.get("ring_order") != board_ids:
        raise AcceptanceError(
            "measured physical loop order differs from bench NO.1..NO.4")
    assignments = topology_summary.get("assignments", [])
    if ([row.get("address") for row in assignments] != board_ids or
            any(row.get("passed") is not True for row in assignments)):
        raise AcceptanceError("P0T NO assignment/readback did not pass")

    coarse_paths: list[Path] = []
    coded_paths: list[Path] = []
    for level in config["calibration_profile_levels"]:
        probe_phase_cycles = calibration_probe_phase_cycles(config, level)
        coded_probe_phase_cycles = calibration_coded_probe_phase_cycles(
            config, level)
        coarse_dir = out_dir / f"coarse-clk-level{level}"
        print(f"Hardware acceptance: coarse CLK calibration level={level}",
              flush=True)
        coarse_command = [
            sys.executable,
            str(root / "tools/calibration_ring_validate/calibration_clk_train.py"),
            *common_boards, "--expected-build", build_id,
            "--level", str(level),
            "--repeats", str(config["calibration_clk_repeats"]),
            "--probe-phase-cycles", str(probe_phase_cycles),
            "--binary-refine", "--out-dir", str(coarse_dir),
        ]
        add_serial_timing(coarse_command, timing, action=True, gap=True)
        _run_step(coarse_command, root, out_dir / f"coarse-clk-level{level}.log")
        coarse_paths.append(coarse_dir / "summary.json")

        coded_dir = out_dir / f"coded-marker-level{level}"
        print(f"Hardware acceptance: coded marker calibration level={level}",
              flush=True)
        coded_command = [
            sys.executable,
            str(root / "tools/calibration_ring_validate/calibration_clk_coded.py"),
            *common_boards, "--expected-build", build_id,
            "--level", str(level),
            "--codebook", str(config["calibration_coded_codebook"]),
            "--repeats", str(config["calibration_coded_repeats"]),
            "--out-dir", str(coded_dir),
        ]
        for phase_cycles in coded_probe_phase_cycles:
            coded_command.extend([
                "--probe-phase-cycles", str(phase_cycles)])
        add_serial_timing(coded_command, timing, action=True, gap=True)
        _run_step(coded_command, root, out_dir / f"coded-marker-level{level}.log")
        coded_paths.append(coded_dir / "summary.json")

    coarse_summary_path = out_dir / "coarse-calibration-summary.json"
    coded_summary_path = out_dir / "coded-calibration-summary.json"
    write_phase_summary(coarse_summary_path, "COARSE_CLK", coarse_paths)
    write_phase_summary(coded_summary_path, "CODED_MARKER", coded_paths)

    schedule_before = read_schedules(board_ids, timing)
    p3_dir = out_dir / "p3-four-board"
    print("Hardware acceptance: Latency Cal four links x two groups x frequency ladder",
          flush=True)
    p3_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_link_p3.py"),
        "--expected-build", build_id,
        "--signal-group", config["signal_group"],
        "--repeats", str(config["repeats"]),
        "--pulse-count", str(config["pulse_count"]),
        "--capture-words", str(config["capture_words"]),
        "--out-dir", str(p3_dir),
    ]
    for board_id in board_ids:
        p3_command.extend(["--board-id", board_id])
    for frequency in config["frequency_ladder_mhz"]:
        p3_command.extend(["--frequency-mhz", str(frequency)])
    # A single-frequency bench is an intentional bring-up gate.  The P3
    # tool keeps the full 10/25/30 MHz ladder as its default contract, so make
    # the reduced 10 MHz input explicit rather than silently weakening it.
    if config["frequency_ladder_mhz"] != [10, 25, 30]:
        p3_command.append("--diagnostic-frequency-only")
    add_serial_timing(p3_command, timing, action=True, capture=True, gap=True)
    _run_step(p3_command, root, out_dir / "p3.log")
    p3_summary_path = p3_dir / "summary.json"
    p3_summary = json.loads(p3_summary_path.read_text(encoding="utf-8"))
    p3_metrics = validate_p3(p3_summary, config)
    link_delays = p3_link_delays(p3_summary, config)
    schedule_after = read_schedules(board_ids, timing)
    validate_schedule_isolation(
        schedule_before, schedule_after, int(config["calibration_load_mask"]))

    generation = int(time.time())
    link_delay_args: list[str] = []
    for link_delay in link_delays:
        link_delay_args.extend(["--link-delay-ns", str(link_delay)])

    marker_dir = out_dir / "trn00-marker-accepted-row"
    marker_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_marker_train.py"),
        *common_boards, "--expected-build", build_id,
        "--level", str(config["training_profile_level"]),
        "--codebook", str(config["training_marker_codebook"]),
        "--offset-matrix", *link_delay_args,
        "--matrix-repeats", str(config["training_marker_repeats"]),
        "--matrix-epoch-start", "1",
        "--matrix-generation-start", str(generation),
        "--out-dir", str(marker_dir),
    ]
    for value in (-1, 0, 1):
        marker_command.extend(["--matrix-offset-value", str(value)])
    for node, value in enumerate(config["training_marker_offsets_by_node"]):
        marker_command.extend([
            "--matrix-filter-node-offset", f"{node}={value}"])
    add_serial_timing(marker_command, timing, action=True, gap=True)
    print("Hardware acceptance: TRN-00 accepted MARK offset row", flush=True)
    _run_step(marker_command, root, out_dir / "trn00-marker.log")
    marker_summary_path = marker_dir / "summary.json"
    marker_summary = json.loads(marker_summary_path.read_text(encoding="utf-8"))
    validate_pass_summary(marker_summary, "TRN-00 MARK offset row")

    # Residence, SCK and DATA form one portable training identity.  MARK row
    # repetitions above use separate generations only as fresh stability proof.
    training_generation = generation + int(config["training_marker_repeats"]) + 1
    residence_dir = out_dir / "trn00-residence"
    residence_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_marker_train.py"),
        *common_boards, "--expected-build", build_id,
        "--level", str(config["training_profile_level"]),
        "--codebook", str(config["training_marker_codebook"]),
        "--residence-matrix", "--epoch", "32",
        "--generation", str(training_generation), *link_delay_args,
        "--out-dir", str(residence_dir),
    ]
    for value in config["training_marker_offsets_by_node"]:
        residence_command.extend(["--node-offset-samples", str(value)])
    add_serial_timing(residence_command, timing, action=True, gap=True)
    print("Hardware acceptance: TRN-00 full residence matrix", flush=True)
    _run_step(residence_command, root, out_dir / "trn00-residence.log")
    residence_summary_path = residence_dir / "summary.json"
    residence_summary = json.loads(
        residence_summary_path.read_text(encoding="utf-8"))
    validate_pass_summary(residence_summary, "TRN-00 residence matrix")
    trn00_summary_path = out_dir / "trn00-summary.json"
    write_phase_summary(
        trn00_summary_path, "TRN-00_MARK_AND_RESIDENCE",
        [marker_summary_path, residence_summary_path])

    sck_dir = out_dir / "trn01-sck"
    sck_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_sck_train.py"),
        *common_boards, "--expected-build", build_id,
        "--level", str(config["training_profile_level"]),
        "--codebook", str(config["training_sck_codebook"]),
        "--all-links", "--repeats", str(config["training_sck_repeats"]),
        "--max-offset-span", str(config["training_max_offset_span"]),
        "--epoch", "48", "--generation", str(training_generation),
        "--reuse-ring-identity", *link_delay_args,
        "--out-dir", str(sck_dir),
    ]
    for value in config["training_sck_offsets_by_node"]:
        sck_command.extend(["--node-sck-offset-samples", str(value)])
    add_serial_timing(sck_command, timing, action=True, gap=True)
    print("Hardware acceptance: TRN-01 SCK offset matrix", flush=True)
    _run_step(sck_command, root, out_dir / "trn01-sck.log")
    sck_summary_path = sck_dir / "summary.json"
    sck_summary = json.loads(sck_summary_path.read_text(encoding="utf-8"))
    validate_pass_summary(sck_summary, "TRN-01 SCK training")
    trn01_summary_path = out_dir / "trn01-summary.json"
    write_phase_summary(
        trn01_summary_path, "TRN-01_SCK_OFFSET_MATRIX",
        [sck_summary_path])

    data_dir = out_dir / "trn02-data"
    data_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_data_train.py"),
        *common_boards, "--expected-build", build_id,
        "--level", str(config["training_profile_level"]),
        "--codebook", str(config["training_data_codebook"]),
        "--marker-direction", "forward", "--data-direction", "reverse",
        "--all-links", "--repeats", str(config["training_data_repeats"]),
        "--max-offset-span", str(config["training_max_offset_span"]),
        "--marker-to-data-samples",
        str(config["training_marker_to_data_samples"]),
        "--epoch", "96", "--generation", str(training_generation),
        "--reuse-ring-identity", *link_delay_args,
        "--out-dir", str(data_dir),
    ]
    for value in config["training_marker_offsets_by_node"]:
        data_command.extend(["--node-marker-offset-samples", str(value)])
    for value in config["training_data_offsets_by_node"]:
        data_command.extend(["--node-data-offset-samples", str(value)])
    add_serial_timing(data_command, timing, action=True, gap=True)
    print("Hardware acceptance: TRN-02 DATA repeat matrix", flush=True)
    _run_step(data_command, root, out_dir / "trn02-data.log")
    data_summary_path = data_dir / "summary.json"
    data_summary = json.loads(data_summary_path.read_text(encoding="utf-8"))
    validate_pass_summary(data_summary, "TRN-02 DATA training")

    trn03_matrix_path = out_dir / "trn03-matrix.json"
    print("Hardware acceptance: derive fresh TRN-03 replay matrix", flush=True)
    matrix_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/trn03_matrix.py"),
        "--level", str(config["training_profile_level"]),
        "--data", str(data_summary_path),
        "--residence", str(residence_summary_path),
        "--sck", str(sck_summary_path), "--out", str(trn03_matrix_path),
    ]
    if diagnostic_continue:
        matrix_command.append("--diagnostic-continue")
    _run_step(matrix_command, root, out_dir / "trn03-matrix.log")
    trn03_matrix = json.loads(trn03_matrix_path.read_text(encoding="utf-8"))
    if (trn03_matrix.get("node_ids_in_loop_order") != board_ids or
            int(trn03_matrix.get("calibration_generation", 0)) !=
            training_generation):
        raise AcceptanceError("fresh TRN-03 matrix identity is inconsistent")
    sck_replay_selection = trn03_matrix.get("derivation", {}).get(
        "sck_replay_selection", {})
    if not bool(sck_replay_selection.get("selected_row_replay_safe", True)):
        if not diagnostic_continue:
            raise AcceptanceError("TRN-03 selected SCK row is not replay-safe")
        diagnostic_failures.append({
            "phase": "TRN-03 SCK replay row selection",
            "returncode": 1,
            "error": "no measured SCK row satisfies the flight re-arm budget",
            "selected_offset_sample_counts_by_node": sck_replay_selection.get(
                "selected_offset_sample_counts_by_node"),
            "selected_min_follower_margin_samples": sck_replay_selection.get(
                "selected_min_follower_margin_samples"),
            "matrix": trn03_matrix_path.resolve().relative_to(root).as_posix(),
        })

    sma_wire_order_summary_path: Path | None = None
    if not tdma_only:
        sma_dir = out_dir / "sma-no5-full-txrx"
        sma_command = [
            sys.executable,
            str(root / "tools/sma_cable_delay_validate/sma_cable_symmetric_rtt.py"),
            "--output-dir", str(sma_dir),
            "--repeats", str(config["sma_observer_rtt_repeats"]),
            "--capture-words", str(config["sma_observer_capture_words"]),
            "--line-settle", str(timing["output_handoff_s"]),
        ]
        for board_id in config["ota_board_ids"]:
            sma_command.extend(["--board-id", str(board_id)])
        add_serial_timing(sma_command, timing)
        print("Hardware acceptance: five-board bidirectional SMA topology and RTT",
              flush=True)
        _run_step(sma_command, root, out_dir / "sma-no5-full-txrx.log")
        sma_wire_order_summary_path = sma_dir / "sma_cable_symmetric_rtt.json"
        sma_summary = json.loads(
            sma_wire_order_summary_path.read_text(encoding="utf-8"))
        validate_sma_observer_topology(sma_summary, config)

    tdma_dir = out_dir / "tdma-process-image"
    tdma_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/trn03_closed_loop.py"),
        *common_boards, "--config", str(trn03_matrix_path),
        "--expected-build", build_id,
        "--level", str(config["tdma_profile_level"]),
        "--stage", "process-image", "--dpll-provisional",
        "--clock-evidence", "enabled", "--leave-running",
        "--window-s", str(config["tdma_window_s"]),
        "--sample-interval-s", str(config["tdma_sample_interval_s"]),
        "--startup-timeout-s", str(config["tdma_startup_timeout_s"]),
        "--startup-stable-samples", str(config["tdma_startup_stable_samples"]),
        "--startup-poll-interval-s",
        str(timing["status_poll_interval_s"]),
        "--out-dir", str(tdma_dir),
    ]
    add_serial_timing(tdma_command, timing)
    if config["tdma_capture_waveforms"]:
        tdma_command.append("--capture-waveforms")
    if diagnostic_continue:
        tdma_command.append("--diagnostic-continue")
    print("Hardware acceptance: four-Node TDMA process-image/FIFO loop", flush=True)
    tdma_returncode = _run_step(
        tdma_command, root, out_dir / "tdma.log",
        allow_failure=diagnostic_continue)
    tdma_summary_path = tdma_dir / "summary.json"
    tdma_summary = json.loads(tdma_summary_path.read_text(encoding="utf-8"))
    if diagnostic_continue and tdma_returncode != 0:
        diagnostic_failures.append({
            "phase": "four-Node TDMA closed loop",
            "returncode": tdma_returncode,
            "error": str(tdma_summary.get("error", "")),
            "summary": tdma_summary_path.resolve().relative_to(root).as_posix(),
        })
    else:
        validate_pass_summary(tdma_summary, "four-Node TDMA closed loop")
    if tdma_summary.get("left_running") is not True:
        raise AcceptanceError("TDMA gate did not hand off a running loop")

    dpll_summary_path: Path | None = None
    if tdma_only:
        print(
            "Hardware acceptance: skip NO5 DPLL observation "
            "(TDMA four-node scope)", flush=True)
    else:
        all_board_ids = list(config["ota_board_ids"])
        ports = discover_board_ports(all_board_ids, timing)
        dpll_dir = out_dir / "dpll-no5-observation"
        dpll_command = [
            sys.executable,
            str(root / "tools/dpll_vdc_monitor/dpll_vdc_monitor.py"),
            "--observer-name", str(config["dpll_observer_name"]),
            "--duration-s", str(config["dpll_monitor_duration_s"]),
            "--poll-interval-s", str(config["dpll_monitor_poll_interval_s"]),
            "--expected-interval-ms", str(config["dpll_expected_interval_ms"]),
            "--interval-tolerance-ms", str(config["dpll_interval_tolerance_ms"]),
            "--sequence-skew-tolerance",
            str(config["dpll_sequence_skew_tolerance"]),
            "--phase-sample-period-ns",
            str(config["dpll_phase_sample_period_ns"]),
            "--phase-pulse-period-ns",
            str(config["dpll_phase_pulse_period_ns"]),
            "--phase-pulse-high-ns",
            str(config["dpll_phase_pulse_high_ns"]),
            "--phase-pulse-count",
            str(config["dpll_phase_pulse_count"]),
            "--phase-start-delay-ns",
            str(config["dpll_phase_start_delay_ns"]),
            "--phase-max-span-ns",
            str(config["dpll_phase_max_span_ns"]),
            "--phase-min-complete-rounds",
            str(config["dpll_phase_min_complete_rounds"]),
            "--fail-on-gate", "--out-dir", str(dpll_dir),
        ]
        add_serial_timing(dpll_command, timing)
        for index, board_id in enumerate(board_ids, 1):
            dpll_command.extend(["--board", f"NO{index}={ports[board_id]}"])
        observer_id = str(config["dpll_observer_board_id"])
        dpll_command.extend([
            "--board", f"{config['dpll_observer_name']}={ports[observer_id]}"])
        waveform_analysis_path = tdma_dir / "analysis" / "ring_capture_analysis.json"
        if config["tdma_capture_waveforms"] and waveform_analysis_path.is_file():
            dpll_command.extend([
                "--waveform-analysis", str(waveform_analysis_path)])
        elif config["tdma_capture_waveforms"]:
            # SD capture/analysis is an offline diagnostic attachment.  A
            # missing attachment must not block the independent NO5 DPLL
            # state/sequence observation after the realtime TDMA gate passed.
            print(
                "Hardware acceptance: SD waveform evidence unavailable; "
                "continue DPLL observation without attachment", flush=True)
        print("Hardware acceptance: DPLL/VDC observation on NO5", flush=True)
        dpll_returncode = _run_step(
            dpll_command, root, out_dir / "dpll.log",
            allow_failure=diagnostic_continue)
        dpll_summary_path = dpll_dir / "summary.json"
        if not dpll_summary_path.is_file():
            if not diagnostic_continue:
                raise AcceptanceError("DPLL monitor did not write summary.json")
            dpll_summary = {
                "schema": "HAOFV_FAILED_STEP_V1",
                "passed": False,
                "error": "DPLL monitor did not write summary.json",
                "returncode": dpll_returncode,
            }
            dpll_summary_path.parent.mkdir(parents=True, exist_ok=True)
            dpll_summary_path.write_text(
                json.dumps(dpll_summary, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8")
        else:
            dpll_summary = json.loads(
                dpll_summary_path.read_text(encoding="utf-8"))
        if diagnostic_continue and dpll_returncode != 0:
            diagnostic_failures.append({
                "phase": "DPLL/VDC NO5 observation",
                "returncode": dpll_returncode,
                "error": str(dpll_summary.get("error", "")),
                "summary": dpll_summary_path.resolve().relative_to(root).as_posix(),
            })
        else:
            validate_pass_summary(dpll_summary, "DPLL/VDC NO5 observation")

    final_schedules = read_schedules(board_ids, timing)
    try:
        validate_runtime_schedules(
            final_schedules, int(config["calibration_load_mask"]))
    except AcceptanceError as exc:
        if not diagnostic_continue:
            raise
        diagnostic_failures.append({
            "phase": "final TDMA realtime schedule",
            "returncode": 1,
            "error": str(exc),
        })

    fingerprint_after, count_after = working_source_fingerprint(root)
    if (fingerprint_after, count_after) != (fingerprint_before, source_count):
        raise AcceptanceError("source changed while hardware acceptance was running")
    if diagnostic_failures:
        diagnostic_result = {
            "schema": "HAOFV_HARDWARE_ACCEPTANCE_DIAGNOSTIC_V1",
            "passed": True,
            "flow_completed": True,
            "strict_gates_passed": False,
            "diagnostic_continue": True,
            "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            "build_id": build_id,
            "failures": diagnostic_failures,
            "tdma_summary": tdma_summary_path.resolve().relative_to(root).as_posix(),
            "dpll_summary": (
                dpll_summary_path.resolve().relative_to(root).as_posix()
                if dpll_summary_path is not None else None),
            "feedback_inputs": build_diagnostic_feedback(
                tdma_summary,
                dpll_summary if dpll_summary_path is not None else None,
                trn03_matrix),
        }
        diagnostic_path = out_dir / "diagnostic.json"
        diagnostic_path.write_text(
            json.dumps(diagnostic_result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        _record_timing_event({
            "action": "acceptance.complete",
            "started_at_utc": None,
            "ended_at_utc": datetime.now(timezone.utc).isoformat(),
            "duration_ms": None,
            "returncode": 0,
            "status": "PASS_WITH_DIAGNOSTICS",
        })
        print(
            "PASS hardware regression flow completed with diagnostic gates; "
            f"evidence={diagnostic_path}")
        return
    _record_timing_event({
        "action": "acceptance.complete",
        "started_at_utc": None,
        "ended_at_utc": datetime.now(timezone.utc).isoformat(),
        "duration_ms": None,
        "returncode": 0,
        "status": "PASS",
    })
    receipt = {
        "schema": (TDMA_RECEIPT_SCHEMA if tdma_only else RECEIPT_SCHEMA),
        "acceptance_scope": "FOUR_NODE_TDMA" if tdma_only else "FULL",
        "dpll_observation": "SKIPPED_TDMA_ONLY" if tdma_only else "REQUIRED",
        "passed": True,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_tree_sha256": fingerprint_after,
        "source_file_count": source_count,
        "bench_config_path": args.config.as_posix(),
        "bench_config_sha256": sha256_file(config_path),
        "build_id": build_id,
        "firmware_package": evidence_entry(root, package),
        "ota_summary": evidence_entry(
            root, ota_summary_path, board_count=len(ota_board_ids)),
        "topology_summary": evidence_entry(root, topology_summary_path),
        "coarse_calibration_summary": evidence_entry(root, coarse_summary_path),
        "coded_calibration_summary": evidence_entry(root, coded_summary_path),
        "initialization_reset": evidence_entry(root, reset_path),
        "p3_summary": evidence_entry(root, p3_summary_path),
        "trn00_summary": evidence_entry(root, trn00_summary_path),
        "trn01_summary": evidence_entry(root, trn01_summary_path),
        "trn02_summary": evidence_entry(root, data_summary_path),
        "trn03_matrix": evidence_entry(root, trn03_matrix_path),
        "tdma_summary": evidence_entry(root, tdma_summary_path),
        "ota_board_ids": ota_board_ids,
        "tdma_board_ids": board_ids,
        **p3_metrics,
        "link_delay_ns_by_link": link_delays,
        "link_base_delay_ns_by_link": [value // 2 for value in link_delays],
        "calibration_generation": training_generation,
        "schedule_before": schedule_before,
        "schedule_after": final_schedules,
        "realtime_load_mask_unchanged": True,
        "calibration_quarantined": False,
        "hardware_acceptance_timing": timing,
        "timing_probe": evidence_entry(root, timing_probe_path),
    }
    if dpll_summary_path is not None:
        receipt["dpll_summary"] = evidence_entry(root, dpll_summary_path)
    if sma_wire_order_summary_path is not None:
        receipt["sma_observer_wiring"] = evidence_entry(
            root, sma_wire_order_summary_path)
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(
        json.dumps(receipt, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (out_dir / "acceptance.json").write_text(
        json.dumps(receipt, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(
        f"PASS calibration-to-DPLL hardware acceptance build={build_id} "
        f"Latency_Cal_trials={p3_metrics['trial_count']} receipt={receipt_path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    run = subparsers.add_parser(
        "run", help="build, OTA, calibrate/train, P3, TDMA and DPLL")
    run.add_argument("--root", type=Path, default=ROOT)
    run.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    run.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    run.add_argument("--build-dir", type=Path)
    run.add_argument("--out-dir", type=Path)
    run.add_argument(
        "--tdma-only", action="store_true",
        help="run calibration and four-node TDMA acceptance without NO5/DPLL")
    run.add_argument(
        "--diagnostic-continue", action="store_true",
        help=("continue through TDMA/DPLL runtime gate failures, retain all "
              "evidence, and finish with a non-passing diagnostic result"))
    resume = subparsers.add_parser(
        "resume",
        help="reuse an existing package/OTA record; never build or OTA")
    resume.add_argument("--root", type=Path, default=ROOT)
    resume.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    resume.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    resume.add_argument("--package", required=True, type=Path,
                        help="existing DHRT100_UPDATE.pkg")
    resume.add_argument("--ota-summary", required=True, type=Path,
                        help="existing successful OTA summary for the selected scope")
    resume.add_argument("--out-dir", type=Path)
    resume.add_argument(
        "--tdma-only", action="store_true",
        help="resume four-node TDMA acceptance without NO5/DPLL")
    resume.add_argument(
        "--diagnostic-continue", action="store_true",
        help=("continue through TDMA/DPLL runtime gate failures, retain all "
              "evidence, and finish with a non-passing diagnostic result"))
    check = subparsers.add_parser(
        "check-staged", help="gate staged code against the indexed receipt")
    check.add_argument("--root", type=Path, default=ROOT)
    check.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    limited = subparsers.add_parser(
        "limited-receipt",
        help="write an auditable OTA + 10 MHz P3 bring-up receipt")
    limited.add_argument("--root", type=Path, default=ROOT)
    limited.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    limited.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    limited.add_argument("--package", required=True, type=Path)
    limited.add_argument("--ota-summary", required=True, type=Path)
    limited.add_argument("--p3-summary", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command in ("run", "resume"):
            run_acceptance(args)
        elif args.command == "check-staged":
            check_staged(args.root.resolve(), args.receipt)
        else:
            write_limited_receipt(args)
    except (AcceptanceError, OSError, KeyError, TypeError, ValueError,
            json.JSONDecodeError) as exc:
        print(f"FAIL Latency Cal hardware acceptance: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
