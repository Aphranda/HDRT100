#!/usr/bin/env python3
"""Run and gate the scoped sequence single-board hardware acceptance.

This is an alternative only for the exact sequence-risk source allowlist.  It
does not produce P3, multi-board, waveform, RF, independent edge-count, or
TDMA stability evidence.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.hardware_acceptance import p3_hardware_acceptance as p3
from tools.hardware_acceptance import sequence_tdma_cycle_validate as cycle
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError


RECEIPT_SCHEMA = "HAOFV_SEQUENCE_SINGLE_BOARD_RECEIPT_V1"
ACCEPTANCE_SCOPE = "SEQUENCE_SINGLE_BOARD_RJ45_FUNCTIONAL"
DEFAULT_RECEIPT = Path(
    "config/hardware_acceptance/sequence_single_board_receipt.json")
FINITE_REPEAT = 10
MINIMUM_EVENTS = 9

PRODUCTION_ALLOWLIST = frozenset({
    "components/distributed_refmem/inc/refmem_sequence_roles.h",
    "components/distributed_refmem/src/distributed_refmem.c",
    "components/sync_io/inc/sync_io_sequence.h",
    "components/sync_io/src/sync_io_sequence.c",
    "components/sync_io/src/sync_io_sequence.pio",
    "components/sync_trigger/inc/trigger_sequence_link.h",
    "components/sync_trigger/inc/trigger_sequence_link_protocol.h",
    "components/sync_trigger/inc/trigger_sequence_service.h",
    "components/sync_trigger/src/trigger_sequence_link.c",
    "components/sync_trigger/src/trigger_sequence_link_protocol.c",
    "components/sync_trigger/src/trigger_sequence_service.c",
    "components/tdma/inc/tdma_pio_spi_ring_adapter.h",
    "components/tdma/src/tdma_pio_spi_ring_adapter.c",
    "middleware/scpi_port/src/scpi_config_commands.c",
    "middleware/scpi_port/inc/scpi_sequence_commands.h",
    "middleware/scpi_port/src/scpi_sequence_commands.c",
    "middleware/scpi_port/src/scpi_sequence_node_commands.c",
})

SUPPORT_ALLOWLIST = frozenset({
    ".githooks/pre-commit",
    "tests/python/test_p3_hardware_acceptance.py",
    "tests/python/test_refmem_sequence_role_scpi.py",
    "tests/python/test_sequence_feedback_validate.py",
    "tests/python/test_sequence_repeat_validate.py",
    "tests/python/test_sequence_scpi_runtime.py",
    "tests/python/test_sequence_single_board_gate.py",
    "tests/python/test_sequence_tdma_cycle_validate.py",
    "tests/python/test_sequence_trigger_acceptance.py",
    "tests/python/test_sequence_trigger_debug_ui.py",
    "tests/python/test_sequence_trigger_debug_ui_layout.py",
    "tests/python/test_sync_io_sequence.py",
    "tests/python/test_trigger_sequence_link.py",
    "tests/unit/test_refmem_sequence_role_scpi.c",
    "tests/unit/test_refmem_sequence_roles.c",
    "tests/unit/test_sequence_scpi_config.c",
    "tests/unit/test_sequence_scpi_runtime.c",
    "tests/unit/test_sync_io_sequence_resources.c",
    "tests/unit/test_sync_io_sequence.c",
    "tests/unit/test_tdma_local_return.c",
    "tests/unit/test_trigger_sequence_link.c",
    "tests/unit/test_trigger_sequence_link_protocol.c",
    "tests/unit/test_trigger_sequence_service.c",
    "tools/hardware_acceptance/sequence_single_board_gate.py",
    "tools/hardware_acceptance/sequence_feedback_validate.py",
    "tools/hardware_acceptance/sequence_repeat_validate.py",
    "tools/hardware_acceptance/sequence_tdma_cycle_validate.py",
    "tools/hardware_acceptance/sequence_trigger_acceptance.py",
    "tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py",
})

SOURCE_ALLOWLIST = PRODUCTION_ALLOWLIST | SUPPORT_ALLOWLIST
SOURCE_EXCLUDES = {
    DEFAULT_RECEIPT.as_posix(),
    p3.DEFAULT_RECEIPT.as_posix(),
}


def _normalized(path: str | Path) -> str:
    return str(path).replace("\\", "/").removeprefix("./")


def is_single_board_source(path: str | Path) -> bool:
    normalized = _normalized(path)
    return normalized not in SOURCE_EXCLUDES and p3.is_acceptance_source(normalized)


def _tree_digest(rows: Iterable[tuple[str, str, str]]) -> tuple[str, int]:
    digest = hashlib.sha256()
    count = 0
    for path, mode, blob_id in sorted(rows):
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(mode.encode("ascii"))
        digest.update(b"\0")
        digest.update(blob_id.encode("ascii"))
        digest.update(b"\n")
        count += 1
    return digest.hexdigest(), count


def _index_entries(root: Path) -> list[tuple[str, str, str]]:
    raw = p3._run_git(root, "ls-files", "--stage", "-z")
    entries = []
    for entry in raw.split(b"\0"):
        if not entry or b"\t" not in entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        fields = metadata.split()
        if len(fields) != 3 or fields[2] != b"0":
            continue
        path = raw_path.decode("utf-8", errors="surrogateescape")
        if is_single_board_source(path):
            entries.append((_normalized(path), fields[0].decode("ascii"),
                            fields[1].decode("ascii")))
    return sorted(entries)


def staged_source_fingerprint(root: Path = ROOT) -> tuple[str, int]:
    return _tree_digest(_index_entries(root))


def working_source_fingerprint(root: Path = ROOT) -> tuple[str, int]:
    entries = {path: (mode, oid) for path, mode, oid in _index_entries(root)}
    raw = p3._run_git(root, "ls-files", "--modified", "--deleted", "--others",
                      "--exclude-standard", "-z")
    for raw_path in raw.split(b"\0"):
        if not raw_path:
            continue
        path = _normalized(raw_path.decode("utf-8", errors="surrogateescape"))
        if not is_single_board_source(path):
            continue
        full = root / path
        if not full.is_file():
            entries.pop(path, None)
            continue
        blob_id = p3._run_git(
            root, "hash-object", f"--path={path}", path).decode("ascii").strip()
        entries[path] = (entries.get(path, ("100644", ""))[0], blob_id)
    return _tree_digest(
        (path, mode, oid) for path, (mode, oid) in entries.items())


def changed_staged_sources(root: Path = ROOT) -> list[str]:
    raw = p3._run_git(root, "diff", "--cached", "--name-only", "-z")
    return sorted(
        _normalized(item.decode("utf-8", errors="surrogateescape"))
        for item in raw.split(b"\0")
        if item and is_single_board_source(
            item.decode("utf-8", errors="surrogateescape")))


def validate_scope(paths: Iterable[str]) -> list[str]:
    changed = sorted({_normalized(path) for path in paths})
    if not changed:
        raise AcceptanceError("single-board gate has no staged source change")
    outside = sorted(set(changed) - SOURCE_ALLOWLIST)
    if outside:
        raise AcceptanceError(
            "staged source is outside the sequence single-board allowlist: " +
            ", ".join(outside[:8]))
    return changed


def _load_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise AcceptanceError(f"invalid {label} {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise AcceptanceError(f"{label} {path} is not an object")
    return value


def _is_no_error(value: object) -> bool:
    return isinstance(value, str) and value.lstrip().startswith("0,")


def validate_cycle_report(report: dict[str, Any], *, profile: str,
                          serial_number: str, build_id: str,
                          tool_sha256: str) -> None:
    if (report.get("passed") is not True or
            report.get("functional_cycle_verified") is not True or
            report.get("failure") is not None or
            report.get("cleanup_failures") != []):
        raise AcceptanceError(f"{profile} cycle report is not PASS")
    if report.get("scope") != "single_board_rj45_dut_vna_functional_cycle":
        raise AcceptanceError(f"{profile} cycle report has invalid scope")
    for claim in ("tdma_stability_verified", "multi_board_verified",
                  "independent_input_count_verified", "waveform_verified",
                  "rf_path_verified", "p3_receipt"):
        if report.get(claim) is not False:
            raise AcceptanceError(f"{profile} report overclaims {claim}")
    settings = report.get("settings")
    if (not isinstance(settings, dict) or
            settings.get("serial_number") != serial_number or
            str(settings.get("build")) != str(build_id) or
            settings.get("gui_control") is not True or
            settings.get("scpi_next") is not True or
            settings.get("minimum_events") != MINIMUM_EVENTS):
        raise AcceptanceError(f"{profile} report identity or fixed profile mismatch")
    expected_repeat = FINITE_REPEAT if profile == "finite" else 0
    if (settings.get("repeat") != expected_repeat or
            settings.get("pause_resume") is not (profile == "pause_resume")):
        raise AcceptanceError(f"{profile} report run mode mismatch")
    if report.get("tool_sha256") != tool_sha256:
        raise AcceptanceError(f"{profile} report was produced by another validator")

    diagnostics = report.get("transport_before_cleanup", {})
    transport = diagnostics.get("READ:SEQ:LINK:TRANSPORT?", {})
    parsed = transport.get("parsed", {})
    if (not _is_no_error(transport.get("error_before")) or
            not _is_no_error(transport.get("error_after")) or
            parsed.get("snapshot_quality") not in (1, 2)):
        raise AcceptanceError(
            f"{profile} report lacks a coherent attributed transport snapshot")
    for command, record in diagnostics.items():
        if (not isinstance(record, dict) or
                not _is_no_error(record.get("error_before")) or
                not _is_no_error(record.get("error_after"))):
            raise AcceptanceError(
                f"{profile} cleanup diagnostic is not error-free: {command}")

    for name in ("stopped", "after_stop_quiet"):
        io = report.get(name, {}).get("io", {})
        if any(io.get(field) != 0 for field in ("outputs", "owned", "armed", "busy")):
            raise AcceptanceError(f"{profile} report did not release sequence IO")
    readbacks = report.get("ring_stop_readbacks")
    if not isinstance(readbacks, list) or not readbacks:
        raise AcceptanceError(f"{profile} report lacks TDMA STOP readback")
    last_readback = readbacks[-1]
    if (not isinstance(last_readback, dict) or
            cycle.ring.field(last_readback, cycle.ring.RING_ENABLED) != 0 or
            cycle.ring.field(last_readback, cycle.ring.RING_ADAPTER_STARTED) != 0):
        raise AcceptanceError(f"{profile} report did not stop the TDMA owner")

    samples = report.get("samples")
    if not isinstance(samples, list) or not samples:
        raise AcceptanceError(f"{profile} report has no runtime samples")
    active = [sample.get("link", {}) for sample in samples
              if sample.get("link", {}).get("phase") in (2, 3, 4, 5, 8)]
    if not active or any(row.get("exchange_id", 0) == 0 for row in active):
        raise AcceptanceError(f"{profile} report lacks nonzero exchange identity")
    software_next = report.get("scpi_next")
    if (not isinstance(software_next, list) or len(software_next) < MINIMUM_EVENTS or
            any(record.get("response") != "1" or
                not isinstance(record.get("identity"), list) or
                len(record["identity"]) != 4 or record["identity"][3] == 0
                for record in software_next) or
            len({tuple(record["identity"]) for record in software_next}) != len(software_next)):
        raise AcceptanceError(f"{profile} report lacks attributed SCPI NEXT events")

    if profile == "finite":
        if report.get("repeat_result") != f"{FINITE_REPEAT},{FINITE_REPEAT},1":
            raise AcceptanceError("finite report did not complete the fixed repeat profile")
    else:
        pause = report.get("pause_resume", {})
        if (pause.get("passed") is not True or
                pause.get("exchange_rotated") is not True):
            raise AcceptanceError("pause/resume report lacks exchange rotation proof")


def validate_ota_summary(summary: dict[str, Any], *, serial_number: str,
                         build_id: str, package: Path) -> None:
    if summary.get("passed") is not True:
        raise AcceptanceError("single-board OTA summary is not PASS")
    matches = []
    for result in summary.get("results", []):
        board = result.get("board", {}) if isinstance(result, dict) else {}
        if board.get("serial_number") == serial_number:
            matches.append(result)
    if len(matches) != 1:
        raise AcceptanceError("OTA summary does not select exactly one requested board")
    result = matches[0]
    if result.get("passed") is not True or result.get("forced_continue") is True:
        raise AcceptanceError("single-board OTA did not pass strictly")
    commit = result.get("commit", {})
    command = commit.get("command", []) if isinstance(commit, dict) else []
    expected = None
    if isinstance(command, list) and "--expected-build" in command:
        index = command.index("--expected-build")
        expected = command[index + 1] if index + 1 < len(command) else None
    if commit.get("passed") is not True or str(expected) != str(build_id):
        raise AcceptanceError("OTA commit is not bound to the requested build")
    send = result.get("send", {})
    send_command = send.get("command", []) if isinstance(send, dict) else []
    package_paths = []
    if isinstance(send_command, list):
        for value in send_command:
            if isinstance(value, str) and value.lower().endswith(".pkg"):
                package_paths.append(Path(value).resolve())
    if send.get("passed") is not True or package.resolve() not in package_paths:
        raise AcceptanceError("OTA send is not bound to the supplied firmware package")


def _relative_file(root: Path, path: Path, label: str) -> Path:
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(root.resolve())
    except ValueError as exc:
        raise AcceptanceError(f"{label} must be inside the repository") from exc
    if not resolved.is_file():
        raise AcceptanceError(f"{label} is unavailable: {relative.as_posix()}")
    return relative


def _evidence(root: Path, path: Path, label: str) -> dict[str, str]:
    relative = _relative_file(root, path, label)
    return {"path": relative.as_posix(), "sha256": p3.sha256_file(root / relative)}


def _validate_evidence(root: Path, item: object, label: str) -> Path:
    if not isinstance(item, dict):
        raise AcceptanceError(f"receipt missing {label}")
    relative, expected = item.get("path"), item.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise AcceptanceError(f"receipt has invalid {label}")
    path = _relative_file(root, root / relative, label)
    full = root / path
    if p3.sha256_file(full) != expected:
        raise AcceptanceError(f"{label} evidence is missing or changed")
    return full


def _cycle_args(args: argparse.Namespace, output: Path, *, pause: bool) -> list[str]:
    values = ["--serial-number", args.serial_number, "--build", args.build,
              "--out", str(output), "--gui-control", "--minimum-events",
              str(MINIMUM_EVENTS), "--repeat", "0" if pause else str(FINITE_REPEAT),
              "--scpi-next"]
    if args.port:
        values.extend(("--port", args.port))
    elif args.visa_resource:
        values.extend(("--visa-resource", args.visa_resource))
    if pause:
        values.append("--pause-resume")
    return values


def run_acceptance(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    changed = validate_scope(changed_staged_sources(root))
    staged_fingerprint = staged_source_fingerprint(root)
    if working_source_fingerprint(root) != staged_fingerprint:
        raise AcceptanceError(
            "working source differs from the staged source; stage or remove every source change")

    package = _relative_file(root, root / args.package, "firmware package")
    ota_path = _relative_file(root, root / args.ota_summary, "OTA summary")
    validate_ota_summary(_load_json(root / ota_path, "OTA summary"),
                         serial_number=args.serial_number, build_id=args.build,
                         package=root / package)

    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    output_dir = (root / args.out_dir) if args.out_dir else (
        root / "out" / "HardwareAcceptance" /
        datetime.now(timezone.utc).strftime("%Y%m%d") /
        f"sequence-single-board-{stamp}")
    output_dir.mkdir(parents=True, exist_ok=False)
    finite_path = output_dir / "finite.json"
    pause_path = output_dir / "pause-resume.json"
    if cycle.main(_cycle_args(args, finite_path, pause=False)) != 0:
        raise AcceptanceError(f"finite single-board profile failed: {finite_path}")
    if cycle.main(_cycle_args(args, pause_path, pause=True)) != 0:
        raise AcceptanceError(f"pause/resume single-board profile failed: {pause_path}")

    tool_sha256 = p3.sha256_file(root / "tools/hardware_acceptance/sequence_tdma_cycle_validate.py")
    finite = _load_json(finite_path, "finite report")
    pause = _load_json(pause_path, "pause/resume report")
    validate_cycle_report(finite, profile="finite", serial_number=args.serial_number,
                          build_id=args.build, tool_sha256=tool_sha256)
    validate_cycle_report(pause, profile="pause_resume", serial_number=args.serial_number,
                          build_id=args.build, tool_sha256=tool_sha256)
    if working_source_fingerprint(root) != staged_fingerprint:
        raise AcceptanceError("source changed while single-board acceptance was running")

    receipt = {
        "schema": RECEIPT_SCHEMA,
        "passed": True,
        "acceptance_scope": ACCEPTANCE_SCOPE,
        "limitations": ["no_p3", "no_multi_board", "no_waveform", "no_rf",
                        "no_independent_edge_count", "no_tdma_stability"],
        "created_at": datetime.now(timezone.utc).isoformat(),
        "serial_number": args.serial_number,
        "build_id": args.build,
        "source_tree_sha256": staged_fingerprint[0],
        "source_file_count": staged_fingerprint[1],
        "changed_sources": changed,
        "validator_sha256": tool_sha256,
        "firmware_package": _evidence(root, root / package, "firmware package"),
        "ota_summary": _evidence(root, root / ota_path, "OTA summary"),
        "finite_report": _evidence(root, finite_path, "finite report"),
        "pause_resume_report": _evidence(root, pause_path, "pause/resume report"),
    }
    receipt_path = root / args.receipt
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(json.dumps(receipt, indent=2, ensure_ascii=True) + "\n",
                            encoding="utf-8")
    print(f"PASS sequence single-board acceptance receipt={args.receipt}")


def check_staged(root: Path, receipt_path: Path) -> None:
    changed = validate_scope(changed_staged_sources(root))
    staged_fingerprint = staged_source_fingerprint(root)
    if working_source_fingerprint(root) != staged_fingerprint:
        raise AcceptanceError(
            "working source differs from the staged commit; rerun single-board acceptance")
    receipt = p3.read_index_json(root, receipt_path)
    if (receipt.get("schema") != RECEIPT_SCHEMA or
            receipt.get("passed") is not True or
            receipt.get("acceptance_scope") != ACCEPTANCE_SCOPE):
        raise AcceptanceError("staged sequence single-board receipt is not PASS")
    if (receipt.get("source_tree_sha256") != staged_fingerprint[0] or
            receipt.get("source_file_count") != staged_fingerprint[1] or
            receipt.get("changed_sources") != changed):
        raise AcceptanceError(
            "staged source has no matching sequence single-board acceptance")
    expected_limitations = {"no_p3", "no_multi_board", "no_waveform", "no_rf",
                            "no_independent_edge_count", "no_tdma_stability"}
    if set(receipt.get("limitations", [])) != expected_limitations:
        raise AcceptanceError("single-board receipt has invalid claim limitations")

    tool_path = root / "tools/hardware_acceptance/sequence_tdma_cycle_validate.py"
    tool_sha256 = p3.sha256_file(tool_path)
    if receipt.get("validator_sha256") != tool_sha256:
        raise AcceptanceError("single-board validator changed after the hardware run")
    package = _validate_evidence(root, receipt.get("firmware_package"), "firmware package")
    ota_path = _validate_evidence(root, receipt.get("ota_summary"), "OTA summary")
    finite_path = _validate_evidence(root, receipt.get("finite_report"), "finite report")
    pause_path = _validate_evidence(
        root, receipt.get("pause_resume_report"), "pause/resume report")
    serial_number, build_id = receipt.get("serial_number"), receipt.get("build_id")
    if not isinstance(serial_number, str) or not isinstance(build_id, str):
        raise AcceptanceError("single-board receipt has invalid board identity")
    validate_ota_summary(_load_json(ota_path, "OTA summary"),
                         serial_number=serial_number, build_id=build_id,
                         package=package)
    validate_cycle_report(_load_json(finite_path, "finite report"), profile="finite",
                          serial_number=serial_number, build_id=build_id,
                          tool_sha256=tool_sha256)
    validate_cycle_report(_load_json(pause_path, "pause/resume report"),
                          profile="pause_resume", serial_number=serial_number,
                          build_id=build_id, tool_sha256=tool_sha256)
    print(f"OK   sequence single-board acceptance: build={build_id} board={serial_number}")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    run = subparsers.add_parser("run", help="execute the fixed single-board profiles")
    run.add_argument("--root", type=Path, default=ROOT)
    run.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    run.add_argument("--serial-number", required=True)
    run.add_argument("--build", required=True)
    run.add_argument("--package", type=Path, required=True)
    run.add_argument("--ota-summary", type=Path, required=True)
    run.add_argument("--out-dir", type=Path)
    transport = run.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port")
    transport.add_argument("--visa-resource")

    check = subparsers.add_parser(
        "check-staged", help="validate the indexed receipt without hardware access")
    check.add_argument("--root", type=Path, default=ROOT)
    check.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    covers = subparsers.add_parser(
        "covers-staged", help="report whether every staged source is in the allowlist")
    covers.add_argument("--root", type=Path, default=ROOT)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "run":
            run_acceptance(args)
        elif args.command == "check-staged":
            check_staged(args.root.resolve(), args.receipt)
        else:
            changed = changed_staged_sources(args.root.resolve())
            validate_scope(changed)
            print("OK   staged source is covered by the sequence single-board allowlist")
    except AcceptanceError as exc:
        print(f"FAIL sequence single-board acceptance: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
