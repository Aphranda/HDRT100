#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import re
import sys


USB_PATTERN = re.compile(r"SYST(?:em)?:USB:")
ALLOWED_FILES = {
    pathlib.Path("middleware/scpi_port/inc/scpi_usb_control.h"),
    pathlib.Path("middleware/scpi_port/src/scpi_usb_control.c"),
}
SCAN_SUFFIXES = {
    ".c",
    ".h",
    ".cpp",
    ".cxx",
    ".cc",
    ".S",
    ".s",
    ".md",
}
SKIP_DIRS = {"build", "out", ".git", "third_party", "docs"}


def iter_files(root: pathlib.Path):
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(root)
        if any(part in SKIP_DIRS or part.startswith(".pytest")
               for part in rel.parts):
            continue
        if path.suffix not in SCAN_SUFFIXES:
            continue
        yield rel, path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    args = parser.parse_args()

    violations = []
    for rel, path in iter_files(args.root):
        if rel in ALLOWED_FILES:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if USB_PATTERN.search(text):
            violations.append(str(rel).replace("\\", "/"))

    if violations:
        print("SYST:USB namespace check failed:")
        for item in violations:
            print(f"  {item}")
        return 1

    print("SYST:USB namespace check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
