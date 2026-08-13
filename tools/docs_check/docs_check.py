#!/usr/bin/env python3
"""Check docs metadata, index coverage, naming, and Markdown references."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REQUIRED_METADATA = ("Status:", "Domain:", "Canonical:", "Related:", "Last updated:")

ALLOWED_STATUS = {"Draft", "Active", "Frozen", "Deprecated"}

ALLOWED_PREFIXES = {
    "ARCH",
    "HAOFV",
    "SYNC_IO",
    "TRIGGER",
    "BISSC",
    "OTA",
    "SD",
    "LOG",
    "SCPI",
    "RTOS",
    "MULTICORE",
    "RELEASE",
    "DOCS",
    "CALIBRATION",
    "REFMEM",
    "COMMUNICATION",
    "MEASURE",
    "HARDWARE",
    "VALIDATION",
    "LEGACY",
    "VDC",
}

ALLOWED_SUFFIXES = {
    "ARCHITECTURE",
    "DESIGN",
    "PLAN",
    "TODO",
    "TASK_PROGRESS",
    "CHECKLIST",
    "PLAYBOOK",
    "COMPARISON",
    "EVALUATION",
    "COMMANDS",
    "ANALYSIS",
    "CONSTRAINTS",
}

LEGACY_NAME_ALLOWLIST = {
    "README.md",
    "TASK_PROGRESS.md",
    "RP2350B_QFN80_IO_CONSTRAINTS.md",
    "RP1200波导天线测试系统分布式触发方案SCPI指令表.md",
    "相控阵测试系统RP分布式触发方案技术报告0804.md",
}

DOC_REF_RE = re.compile(r"docs[\\/][^\s`'\"<>)\]]+?\.md")


@dataclass
class CheckResult:
    failures: list[str]
    warnings: list[str]

    def fail(self, message: str) -> None:
        self.failures.append(message)
        print(f"FAIL {message}")

    def warn(self, message: str) -> None:
        self.warnings.append(message)
        print(f"WARN {message}")

    def ok(self, message: str) -> None:
        print(f"OK   {message}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument(
        "--strict-names",
        action="store_true",
        help="Fail non-conforming filenames instead of warning about legacy names.",
    )
    return parser.parse_args()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def is_allowed_name(name: str) -> bool:
    if name in LEGACY_NAME_ALLOWLIST:
        return True
    if not name.endswith(".md"):
        return False

    stem = name[:-3]
    parts = stem.split("_")
    if len(parts) < 2:
        return False

    suffix = None
    if len(parts) >= 3 and "_".join(parts[-2:]) == "TASK_PROGRESS":
        suffix = "TASK_PROGRESS"
        prefix_parts = parts[:-2]
    else:
        suffix = parts[-1]
        prefix_parts = parts[:-1]

    if suffix not in ALLOWED_SUFFIXES:
        return False

    for width in range(min(3, len(prefix_parts)), 0, -1):
        prefix = "_".join(prefix_parts[:width])
        if prefix in ALLOWED_PREFIXES:
            return True
    return False


def check_metadata(path: Path, docs_dir: Path, text: str, result: CheckResult) -> None:
    lines = text.splitlines()
    if not lines or not lines[0].startswith("# "):
        result.fail(f"{path}: first line must be a level-1 Markdown title")
        return

    header_window = lines[1:8]
    for field in REQUIRED_METADATA:
        if not any(line.startswith(field) for line in header_window):
            result.fail(f"{path}: missing metadata field near top: {field}")

    status_line = next((line for line in header_window if line.startswith("Status:")), None)
    if status_line:
        status = status_line.split(":", 1)[1].strip()
        if status not in ALLOWED_STATUS:
            result.fail(f"{path}: invalid Status value: {status}")

    canonical_line = next((line for line in header_window if line.startswith("Canonical:")), None)
    if canonical_line:
        rel = path.relative_to(docs_dir).as_posix()
        expected = f"`docs/{rel}`"
        if expected not in canonical_line:
            result.fail(f"{path}: Canonical must include {expected}")


def check_index_coverage(docs_files: list[Path], docs_dir: Path, docs_index: Path, result: CheckResult) -> None:
    if not docs_index.exists():
        result.fail("docs/README.md is missing")
        return

    index_text = read_text(docs_index)
    for path in docs_files:
        rel = path.relative_to(docs_dir).as_posix()
        needle = rel if "/" in rel else path.name
        if needle not in index_text:
            result.fail(f"docs/README.md does not list {needle}")


def check_references(root: Path, scan_files: list[Path], result: CheckResult) -> None:
    for path in scan_files:
        text = read_text(path)
        for match in DOC_REF_RE.finditer(text):
            ref = match.group(0).replace("\\", "/")
            if "<" in ref or ">" in ref:
                continue
            target = root / ref
            if not target.exists():
                result.fail(f"{path}: broken docs reference: {match.group(0)}")


def check_conflict_markers(path: Path, text: str, result: CheckResult) -> None:
    for marker in ("<<<<<<<", "=======", ">>>>>>>"):
        if marker in text:
            result.fail(f"{path}: conflict marker found: {marker}")


def check_names(docs_files: list[Path], strict: bool, result: CheckResult) -> None:
    for path in docs_files:
        if is_allowed_name(path.name):
            continue
        message = f"{path}: filename does not match docs naming rules"
        if strict:
            result.fail(message)
        else:
            result.warn(message)


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    docs_dir = root / "docs"

    result = CheckResult(failures=[], warnings=[])

    if not docs_dir.exists():
        result.fail(f"docs directory not found: {docs_dir}")
        return 1

    docs_files = sorted(docs_dir.rglob("*.md"))
    for path in docs_files:
        text = read_text(path)
        check_conflict_markers(path, text, result)
        check_metadata(path, docs_dir, text, result)

    check_names(docs_files, args.strict_names, result)
    check_index_coverage(docs_files, docs_dir, docs_dir / "README.md", result)

    scan_files = docs_files
    root_readme = root / "README.md"
    if root_readme.exists():
        scan_files = scan_files + [root_readme]
    check_references(root, scan_files, result)

    if result.failures:
        print(f"SUMMARY FAIL failures={len(result.failures)} warnings={len(result.warnings)}")
        return 1

    result.ok(f"docs check passed: files={len(docs_files)} warnings={len(result.warnings)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
