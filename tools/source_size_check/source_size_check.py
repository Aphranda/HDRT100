#!/usr/bin/env python3
"""Report source files that exceed the project single-file size budget."""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path


DEFAULT_SUFFIXES = (".c", ".h", ".cc", ".cpp", ".hpp", ".pio")
DEFAULT_EXCLUDED_DIRS = {".git", "out", "build", "build-rtos-multicore-smoke"}


@dataclass(frozen=True)
class OversizeFile:
    path: str
    lines: int
    limit: int
    split_candidates: tuple[str, ...]


def iter_code_files(root: Path):
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in DEFAULT_SUFFIXES:
            continue
        try:
            relative_parts = path.relative_to(root).parts
        except ValueError:
            continue
        if any(part in DEFAULT_EXCLUDED_DIRS for part in relative_parts):
            continue
        yield path


def suggest_split_candidates(path: Path) -> tuple[str, ...]:
    name = path.stem.lower()
    candidates: list[str] = []
    for token, label in (
        ("pio", "PIO/program loading"),
        ("cal", "calibration"),
        ("train", "training"),
        ("flight", "flight persona"),
        ("ring", "ring/runtime"),
        ("phys", "physical transport"),
        ("service", "service/orchestration"),
    ):
        if token in name:
            candidates.append(label)
    if not candidates:
        candidates.append("review by functional responsibility")
    return tuple(candidates)


def scan(root: Path, limit: int) -> list[OversizeFile]:
    result: list[OversizeFile] = []
    for path in iter_code_files(root):
        lines = sum(1 for _ in path.open("r", encoding="utf-8", errors="replace"))
        if lines > limit:
            result.append(OversizeFile(
                path=str(path.relative_to(root)).replace("\\", "/"),
                lines=lines,
                limit=limit,
                split_candidates=suggest_split_candidates(path),
            ))
    return sorted(result, key=lambda item: (-item.lines, item.path))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--max-lines", type=int, default=1000)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    if args.max_lines <= 0:
        raise SystemExit("--max-lines must be positive")
    oversized = scan(root, args.max_lines)
    report = {
        "root": str(root),
        "max_lines": args.max_lines,
        "warning_count": len(oversized),
        "warnings": [asdict(item) for item in oversized],
    }
    if args.output is not None:
        output = args.output if args.output.is_absolute() else root / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if oversized:
        print(f"WARNING source-size: {len(oversized)} file(s) exceed {args.max_lines} lines")
        for item in oversized:
            suggestions = ", ".join(item.split_candidates)
            print(f"WARNING source-size: {item.path}: {item.lines} lines; split candidates: {suggestions}")
    else:
        print(f"source-size: all code files are <= {args.max_lines} lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
