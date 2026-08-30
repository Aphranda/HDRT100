#!/usr/bin/env python3
"""Run and gate the mandatory five-board OTA/four-board P3 acceptance.

``run`` builds the current working source, updates all configured boards,
executes the complete P3 matrix, checks schedule isolation, and writes a
tracked receipt. ``check-staged`` is intentionally hardware-free: it rejects a
code commit unless the receipt in the Git index matches the complete staged
source fingerprint and the referenced local evidence remains intact.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG = Path("config/hardware_acceptance/p3_bench.json")
DEFAULT_RECEIPT = Path("config/hardware_acceptance/p3_acceptance_receipt.json")
RECEIPT_SCHEMA = "HAOFV_P3_HARDWARE_ACCEPTANCE_RECEIPT_V1"
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


class AcceptanceError(RuntimeError):
    """A mandatory acceptance condition was not met."""


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


def _validate_evidence(root: Path, record: dict[str, Any]) -> None:
    for name in ("firmware_package", "ota_summary", "p3_summary"):
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


def check_staged(root: Path, receipt_path: Path) -> None:
    changed = changed_staged_sources(root)
    if not changed:
        print("OK   P3 hardware acceptance: no staged code change")
        return
    dirty = unstaged_sources(root)
    if dirty:
        raise AcceptanceError(
            "unstaged code differs from the staged commit; rerun acceptance "
            "after resolving: " + ", ".join(dirty[:8]))
    receipt = read_index_json(root, receipt_path)
    if receipt.get("schema") != RECEIPT_SCHEMA or receipt.get("passed") is not True:
        raise AcceptanceError("staged P3 hardware acceptance receipt is not PASS")
    fingerprint, file_count = staged_source_fingerprint(root)
    if receipt.get("source_tree_sha256") != fingerprint:
        raise AcceptanceError(
            "staged code fingerprint has no matching P3 acceptance; run "
            "python tools/hardware_acceptance/p3_hardware_acceptance.py run")
    if receipt.get("source_file_count") != file_count:
        raise AcceptanceError("P3 acceptance source file count mismatch")
    _validate_evidence(root, receipt)
    print(
        f"OK   P3 hardware acceptance: sources={file_count} "
        f"build={receipt.get('build_id')} trials={receipt.get('trial_count')}")


def _run_step(command: list[str], root: Path, log_path: Path) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment["PYTHONIOENCODING"] = "utf-8"
    result = subprocess.run(
        command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=environment, check=False)
    log_path.write_bytes(result.stdout)
    if result.returncode != 0:
        tail = result.stdout.decode("utf-8", errors="replace").splitlines()[-20:]
        raise AcceptanceError(
            f"step failed ({' '.join(command)}):\n" + "\n".join(tail))


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


def read_schedules(board_ids: list[str]) -> dict[str, dict[str, int]]:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import board_command, discover  # type: ignore

    args = argparse.Namespace(
        board_ids=board_ids, baud=115200, timeout=5.0, settle=0.2,
        keep_open=False)
    boards = discover(args)
    missing = sorted(set(board_ids) - set(boards))
    if missing:
        raise AcceptanceError("schedule boards missing: " + ", ".join(missing))
    return {
        board_id: parse_schedule(board_command(
            boards[board_id], "SYSTem:TDMA:SCHEDule?", args))
        for board_id in board_ids
    }


def validate_ota(summary: dict[str, Any], expected_ids: list[str],
                 build_id: str) -> None:
    found = {board.get("serial_number") for board in summary.get("boards", [])}
    if (summary.get("passed") is not True or summary.get("dry_run") is True or
            summary.get("failed_count") != 0 or
            summary.get("board_count") != len(expected_ids) or
            summary.get("updated_count") != len(expected_ids) or
            found != set(expected_ids) or summary.get("expected_build") != build_id):
        raise AcceptanceError("five-board OTA summary did not meet acceptance")


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


def run_acceptance(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    config_path = root / args.config
    receipt_path = root / args.receipt
    config = json.loads(config_path.read_text(encoding="utf-8"))
    fingerprint_before, source_count = working_source_fingerprint(root)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = root / (args.out_dir or Path(f"out/hardware_acceptance/p3-{stamp}"))
    build_dir = root / (args.build_dir or Path(f"out/build/p3-acceptance-{stamp}"))
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"P3 acceptance: build -> {build_dir}", flush=True)
    _run_step([
        sys.executable, str(root / "tools/cmake_build_auto/cmake_build_auto.py"),
        "--root", str(root), "--build-dir", str(build_dir),
    ], root, out_dir / "build.log")
    package = build_dir / "DHRT100_UPDATE.pkg"
    if not package.is_file():
        raise AcceptanceError(f"firmware package missing: {package}")
    build_id = _read_package_build_id(package)

    ota_dir = out_dir / "ota-five-board"
    print(f"P3 acceptance: asynchronous OTA five boards build={build_id}", flush=True)
    ota_command = [
        sys.executable, str(root / "tools/ota_multi_update/ota_multi_update.py"),
        str(package), "--expected-board-count", str(len(config["ota_board_ids"])),
        "--expected-build", build_id, "--out-dir", str(ota_dir),
    ]
    for board_id in config["ota_board_ids"]:
        ota_command.extend(["--serial-number", board_id])
    _run_step(ota_command, root, out_dir / "ota.log")
    ota_summary_path = ota_dir / "summary.json"
    ota_summary = json.loads(ota_summary_path.read_text(encoding="utf-8"))
    validate_ota(ota_summary, config["ota_board_ids"], build_id)

    board_ids = config["p3_board_ids_in_physical_order"]
    schedule_before = read_schedules(board_ids)
    p3_dir = out_dir / "p3-four-board"
    print("P3 acceptance: four links x two groups x frequency ladder", flush=True)
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
    _run_step(p3_command, root, out_dir / "p3.log")
    p3_summary_path = p3_dir / "summary.json"
    p3_summary = json.loads(p3_summary_path.read_text(encoding="utf-8"))
    p3_metrics = validate_p3(p3_summary, config)
    schedule_after = read_schedules(board_ids)
    validate_schedule_isolation(
        schedule_before, schedule_after, int(config["calibration_load_mask"]))

    fingerprint_after, count_after = working_source_fingerprint(root)
    if (fingerprint_after, count_after) != (fingerprint_before, source_count):
        raise AcceptanceError("source changed while hardware acceptance was running")
    relative = lambda path: path.resolve().relative_to(root).as_posix()
    receipt = {
        "schema": RECEIPT_SCHEMA,
        "passed": True,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_tree_sha256": fingerprint_after,
        "source_file_count": source_count,
        "bench_config_path": args.config.as_posix(),
        "bench_config_sha256": sha256_file(config_path),
        "build_id": build_id,
        "firmware_package": {
            "path": relative(package), "sha256": sha256_file(package)},
        "ota_summary": {
            "path": relative(ota_summary_path),
            "sha256": sha256_file(ota_summary_path),
            "board_count": len(config["ota_board_ids"]),
        },
        "p3_summary": {
            "path": relative(p3_summary_path),
            "sha256": sha256_file(p3_summary_path),
        },
        **p3_metrics,
        "schedule_before": schedule_before,
        "schedule_after": schedule_after,
        "realtime_load_mask_unchanged": True,
        "calibration_quarantined": False,
    }
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(
        json.dumps(receipt, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (out_dir / "acceptance.json").write_text(
        json.dumps(receipt, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(
        f"PASS P3 hardware acceptance build={build_id} "
        f"trials={p3_metrics['trial_count']} receipt={receipt_path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    run = subparsers.add_parser("run", help="build, OTA, execute P3, write receipt")
    run.add_argument("--root", type=Path, default=ROOT)
    run.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    run.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    run.add_argument("--build-dir", type=Path)
    run.add_argument("--out-dir", type=Path)
    check = subparsers.add_parser(
        "check-staged", help="gate staged code against the indexed receipt")
    check.add_argument("--root", type=Path, default=ROOT)
    check.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "run":
            run_acceptance(args)
        else:
            check_staged(args.root.resolve(), args.receipt)
    except (AcceptanceError, OSError, KeyError, TypeError, ValueError,
            json.JSONDecodeError) as exc:
        print(f"FAIL P3 hardware acceptance: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
