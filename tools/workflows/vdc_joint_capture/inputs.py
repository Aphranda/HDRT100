"""Load a pinned VDC joint-capture input set without executing its adapter."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath


SCHEMA = "VDC_JOINT_CAPTURE_INPUTS_V3"
SCENARIO = "internal_seal"
FILE_KEYS = ("receipt", "baseline", "tap_baseline")
BASELINE_COMMANDS = {
    "*IDN?",
    "SYSTEM:FW:BUILD?",
    "SYSTEM:TDMA:RING:STATUS?",
    "SYSTEM:TDMA:FLIGHT:PRIORITY:TIMING?",
    "SYSTEM:TDMA:FLIGHT:PRIORITY:BUDGET?",
    "SYSTEM:TDMA:FLIGHT:PRIORITY?",
}
MIGRATED_SOURCE_ROLES = (
    "internal_seal", "quiet_startup", "internal_summary",
    "external_monitor", "parallel_startup", "late_scope_adapter",
    "outer_trial", "outer_hil", "legacy_scope_reader",
)


@dataclass(frozen=True)
class WorkflowInputs:
    manifest: Path
    manifest_sha256: str
    scenario: str
    board_uids: tuple[str, ...]
    bench_adapter: Path
    runtime_entrypoint: Path
    migrated_sources: tuple[Path, ...]
    executable_closure: tuple[Path, ...]
    receipt: Path
    baseline: Path
    tap_baseline: Path
    adapter_evidence: dict
    files: dict[str, dict[str, str]]

    def evidence(self, root: Path) -> dict:
        return {
            "schema": SCHEMA,
            "scenario": self.scenario,
            "manifest": {
                "path": self.manifest.relative_to(root).as_posix(),
                "sha256": self.manifest_sha256,
            },
            "board_uids": list(self.board_uids),
            "adapter": self.adapter_evidence,
            "files": self.files,
        }


def _repository_file(root: Path, value: str, label: str) -> Path:
    relative = PurePosixPath(value)
    if (not value or "\\" in value or ":" in value or relative.is_absolute()
            or ".." in relative.parts or relative.as_posix() != value):
        raise ValueError(f"{label} path must be normalized and repository-relative")
    path = (root / Path(*relative.parts)).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f"{label} file is missing or outside the repository")
    return path


def _read_json(path: Path, label: str):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"{label} is not valid UTF-8 JSON: {exc}") from exc


def _pinned_file(root: Path, declaration, label: str) -> tuple[Path, dict[str, str]]:
    if not isinstance(declaration, dict) or set(declaration) != {"path", "sha256"}:
        raise ValueError(f"{label} declaration fields are invalid")
    path = _repository_file(root, declaration["path"], label)
    expected = declaration["sha256"]
    if (not isinstance(expected, str) or len(expected) != 64
            or any(char not in "0123456789abcdef" for char in expected)):
        raise ValueError(f"{label} sha256 must be lowercase hexadecimal")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != expected:
        raise ValueError(f"{label} sha256 mismatch")
    return path, {"path": declaration["path"], "sha256": digest}


def validate_current_receipt(inputs: WorkflowInputs, root: Path) -> dict:
    """Bind the manifest to the current tracked source before adapter import."""
    from tools.hardware_acceptance import p3_hardware_acceptance as gate

    receipt = _read_json(inputs.receipt, "receipt")
    if not isinstance(receipt, dict):
        raise ValueError("receipt must be a JSON object")
    if receipt.get("schema") != gate.QUICK_GRADED_RECEIPT_SCHEMA:
        raise ValueError("receipt schema is not the required quick graded schema")
    gate._validate_quick_diagnostic_receipt(root, receipt)
    fingerprint = gate.working_source_fingerprint(root)
    if fingerprint != (receipt.get("source_tree_sha256"), receipt.get("source_file_count")):
        raise ValueError("current source does not match the P3 receipt")
    if receipt.get("tdma_board_ids") != list(inputs.board_uids):
        raise ValueError("receipt board UID order differs from workflow input manifest")
    return receipt


def load_inputs(manifest: Path, root: Path, selected_adapter: Path) -> WorkflowInputs:
    """Validate all declared inputs before an adapter import or device access."""
    root = root.resolve()
    manifest = manifest.resolve()
    if not manifest.is_relative_to(root) or not manifest.is_file():
        raise ValueError("workflow input manifest is missing or outside the repository")
    raw = manifest.read_bytes()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"workflow input manifest is not valid UTF-8 JSON: {exc}") from exc
    if (not isinstance(data, dict)
            or set(data) != {"schema", "scenario", "board_uids", "adapter", "files"}):
        raise ValueError("workflow input manifest has unexpected top-level fields")
    if data["schema"] != SCHEMA or data["scenario"] != SCENARIO:
        raise ValueError("workflow input manifest schema or scenario mismatch")
    board_uids = data["board_uids"]
    if (not isinstance(board_uids, list) or not board_uids
            or any(not isinstance(uid, str) or not uid for uid in board_uids)
            or len(set(board_uids)) != len(board_uids)):
        raise ValueError("workflow board_uids must be unique nonempty strings")
    declarations = data["files"]
    if not isinstance(declarations, dict) or set(declarations) != set(FILE_KEYS):
        raise ValueError("workflow input file declarations are incomplete")

    adapter = data["adapter"]
    adapter_fields = {
        "compatibility_entrypoint", "migrated_sources",
        "runtime_entrypoint", "runtime_closure",
    }
    if not isinstance(adapter, dict) or set(adapter) != adapter_fields:
        raise ValueError("adapter declaration fields are invalid")
    migrated_paths = []
    migrated_evidence = []
    migrated = adapter["migrated_sources"]
    if not isinstance(migrated, dict) or set(migrated) != set(MIGRATED_SOURCE_ROLES):
        raise ValueError("migrated sources must declare the fixed internal-seal lineage")
    for role in MIGRATED_SOURCE_ROLES:
        path, item = _pinned_file(root, migrated[role], f"migrated source {role}")
        migrated_paths.append(path)
        migrated_evidence.append((role, item))
    if not migrated_paths or len(set(migrated_paths)) != len(migrated_paths):
        raise ValueError("migrated sources must be a nonempty unique list")
    compatibility_entrypoint = _repository_file(
        root, adapter["compatibility_entrypoint"], "compatibility entrypoint")
    if compatibility_entrypoint != migrated_paths[0]:
        raise ValueError("compatibility entrypoint must select migrated internal_seal")
    if compatibility_entrypoint != selected_adapter.resolve():
        raise ValueError("selected bench adapter differs from workflow input manifest")

    executable_paths = []
    executable_evidence = []
    for index, declaration in enumerate(adapter["runtime_closure"]):
        path, item = _pinned_file(root, declaration, f"runtime closure[{index}]")
        executable_paths.append(path)
        executable_evidence.append(item)
    if len(set(executable_paths)) != len(executable_paths):
        raise ValueError("runtime closure contains duplicate paths")
    runtime_entrypoint = _repository_file(
        root, adapter["runtime_entrypoint"], "runtime entrypoint")
    if runtime_entrypoint not in executable_paths:
        raise ValueError("runtime entrypoint is absent from runtime closure")
    if set(migrated_paths) & set(executable_paths):
        raise ValueError("migrated sources must not remain in runtime closure")

    paths = {}
    evidence = {}
    for key in FILE_KEYS:
        declaration = declarations[key]
        expected_fields = {"path", "validation"} if key == "receipt" else {"path", "sha256"}
        if not isinstance(declaration, dict) or set(declaration) != expected_fields:
            raise ValueError(f"{key} declaration fields are invalid")
        if key == "receipt":
            path = _repository_file(root, declaration["path"], key)
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if declaration["validation"] != "P3_SOURCE_FINGERPRINT":
                raise ValueError("receipt validation must bind the current source fingerprint")
            evidence[key] = {"path": declaration["path"], "sha256": digest,
                             "validation": declaration["validation"]}
        else:
            path, evidence[key] = _pinned_file(root, declaration, key)
        paths[key] = path

    receipt = _read_json(paths["receipt"], "receipt")
    if not isinstance(receipt, dict) or receipt.get("tdma_board_ids") != board_uids:
        raise ValueError("receipt board UID order differs from workflow input manifest")
    baseline = _read_json(paths["baseline"], "baseline")
    if (not isinstance(baseline, list) or not baseline
            or any(not isinstance(row, dict)
                   or not isinstance(row.get("uid"), str)
                   or not isinstance(row.get("command"), str)
                   or not isinstance(row.get("raw"), str) for row in baseline)):
        raise ValueError("baseline rows must contain string uid/command/raw fields")
    commands_by_uid = {
        uid: [row["command"].upper() for row in baseline if row["uid"] == uid]
        for uid in board_uids
    }
    if (set(row["uid"] for row in baseline) != set(board_uids)
            or any(len(commands) != len(set(commands))
                   or set(commands) != BASELINE_COMMANDS for commands in commands_by_uid.values())):
        raise ValueError("baseline does not cover the declared board UIDs")
    tap = _read_json(paths["tap_baseline"], "tap baseline")
    tap_boards = tap.get("boards") if isinstance(tap, dict) else None
    if (not isinstance(tap, dict) or tap.get("passed") is not True
            or not isinstance(tap_boards, dict) or list(tap_boards) != board_uids):
        raise ValueError("tap baseline is not a passing record for the declared board order")
    for index, uid in enumerate(board_uids):
        row = tap_boards[uid]
        request = row.get("tap_request") if isinstance(row, dict) else None
        if (not isinstance(request, list) or len(request) != 3
                or any(isinstance(value, bool) or not isinstance(value, int) for value in request)
                or (request != [0, 0, 0] if index == 0 else request[0] != 1)):
            raise ValueError("tap baseline has an invalid per-board tap_request")

    return WorkflowInputs(
        manifest=manifest,
        manifest_sha256=hashlib.sha256(raw).hexdigest(),
        scenario=data["scenario"],
        board_uids=tuple(board_uids),
        bench_adapter=compatibility_entrypoint,
        runtime_entrypoint=runtime_entrypoint,
        migrated_sources=tuple(migrated_paths),
        executable_closure=tuple(executable_paths),
        receipt=paths["receipt"],
        baseline=paths["baseline"],
        tap_baseline=paths["tap_baseline"],
        adapter_evidence={
            "compatibility_entrypoint": adapter["compatibility_entrypoint"],
            "migrated_sources": dict(migrated_evidence),
            "runtime_entrypoint": adapter["runtime_entrypoint"],
            "runtime_closure": executable_evidence,
        },
        files=evidence,
    )
