#!/usr/bin/env python3
"""Warn when a firmware source file is too large to remain maintainable.

The check is intentionally advisory: a file over the threshold reports a
warning and exits successfully.  This keeps the build deterministic while
making every large-file decision visible in the build log.
"""

from __future__ import annotations

import argparse
from pathlib import Path


DEFAULT_SUFFIXES = {
    ".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".S", ".s",
    ".inc",
}
DEFAULT_ROOTS = (
    "application", "boards", "bootloader", "components", "drivers",
    "middleware", "osal",
)
SKIP_DIRS = {"build", "out", ".git", "third_party"}


def iter_source_files(root: Path, roots: tuple[str, ...] = DEFAULT_ROOTS):
    """Yield project source files in stable order."""
    paths: list[Path] = []
    for relative_root in roots:
        source_root = root / relative_root
        if not source_root.is_dir():
            continue
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix not in DEFAULT_SUFFIXES:
                continue
            relative = path.relative_to(root)
            if any(part in SKIP_DIRS for part in relative.parts):
                continue
            paths.append(path)
    yield from sorted(set(paths), key=lambda item: item.as_posix())


def line_count(path: Path) -> int:
    """Count logical text lines while preserving UTF-8 source handling."""
    return len(path.read_text(encoding="utf-8").splitlines())


def find_large_sources(root: Path, max_lines: int) -> list[tuple[Path, int]]:
    """Return files strictly larger than ``max_lines``."""
    if max_lines < 1:
        raise ValueError("max_lines must be positive")
    large: list[tuple[Path, int]] = []
    for path in iter_source_files(root):
        try:
            lines = line_count(path)
        except UnicodeDecodeError as exc:
            raise ValueError(f"source is not valid UTF-8: {path}: {exc}") from exc
        if lines > max_lines:
            large.append((path, lines))
    return large


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--max-lines", type=int, default=1000)
    args = parser.parse_args()

    try:
        large = find_large_sources(args.root, args.max_lines)
    except (OSError, ValueError) as exc:
        print(f"source length check error: {exc}")
        return 2

    if not large:
        print(f"source length check passed: max_lines={args.max_lines}")
        return 0

    print(
        f"source length warning: {len(large)} file(s) exceed "
        f"{args.max_lines} lines; consider functional splitting:"
    )
    for path, lines in large:
        print(f"  WARNING {path.relative_to(args.root).as_posix()}: {lines} lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
