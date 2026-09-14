#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
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
    for directory, dirs, files in os.walk(root):
        # Prune before descending: filtering rglob results still visits out/.
        dirs[:] = [name for name in dirs
                   if name not in SKIP_DIRS and not name.startswith(".pytest")]
        for name in files:
            path = pathlib.Path(directory) / name
            if name in SKIP_DIRS or name.startswith(".pytest"):
                continue
            if path.suffix not in SCAN_SUFFIXES or not path.is_file():
                continue
            yield path.relative_to(root), path


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
