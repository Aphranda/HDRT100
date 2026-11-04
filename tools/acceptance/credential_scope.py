#!/usr/bin/env python3
"""Classify a staged change set into the acceptance credential it needs.

Three credential classes exist, each with its own evidence and fingerprint
scope so a test-only or docs-only change does not invalidate (or demand) a
hardware acceptance run:

``firmware``  production code, build files or the acceptance flow itself
              -> P3 hardware acceptance receipt (whole firmware fingerprint)
``host``      tests and the host-side gate wiring
              -> host acceptance receipt (tests/tools fingerprint)
``docs``      documentation corpus
              -> ``docs_check`` + ``doc_regression`` (no receipt)

The classifier is the single source of truth used by ``.githooks/pre-commit``
and by the receipt checkers, so the routing cannot drift from the policy text
in ``AGENTS.md`` §2.7.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

FIRMWARE_ROOTS = (
    "application/", "boards/", "bootloader/", "cmake/", "components/",
    "drivers/", "linker/", "middleware/", "osal/", "platform/", "third_party/",
    "config/",
)
FIRMWARE_FILES = ("CMakeLists.txt", "CMakePresets.json", "pico_sdk_import.cmake")

HOST_ROOTS = ("tests/", ".githooks/", "tools/acceptance/")
HOST_FILES = ("pytest.ini",)

DOC_ROOTS = ("docs/",)
DOC_FILES = ("AGENTS.md", "README.md")

# Receipts must never change the fingerprint they certify.
FINGERPRINT_EXCLUDES = {
    "config/hardware_acceptance/p3_acceptance_receipt.json",
    "config/hardware_acceptance/t2_acceptance_receipt.json",
    "config/hardware_acceptance/host_acceptance_receipt.json",
}


def _matches(path: str, roots: tuple[str, ...], files: tuple[str, ...]) -> bool:
    return path in files or any(path.startswith(root) for root in roots)


def classify(path: str) -> set[str]:
    """Credential classes a single repository path belongs to."""
    normalized = path.replace("\\", "/")
    while normalized.startswith("./"):
        normalized = normalized[2:]
    if normalized in FINGERPRINT_EXCLUDES:
        return set()
    classes: set[str] = set()
    if normalized.startswith("tools/") and not normalized.startswith("tools/acceptance/"):
        # User directive 2026-11-02: the hardware credential covers firmware
        # only. Python tooling is host-side, so it is routed to the host gate and
        # must not invalidate the P3 fingerprint.
        classes.add("host")
    if _matches(normalized, FIRMWARE_ROOTS, FIRMWARE_FILES):
        classes.add("firmware")
    if _matches(normalized, HOST_ROOTS, HOST_FILES):
        classes.add("host")
    if _matches(normalized, DOC_ROOTS, DOC_FILES) or normalized.endswith(".md"):
        classes.add("docs")
    return classes


def classify_many(paths) -> set[str]:
    classes: set[str] = set()
    for path in paths:
        classes |= classify(path)
    return classes


def _git(root: Path, *args: str) -> bytes:
    result = subprocess.run(
        ["git", *args], cwd=root, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            result.stderr.decode("utf-8", "replace").strip() or "git failed")
    return result.stdout


def staged_paths(root: Path = ROOT) -> list[str]:
    raw = _git(root, "diff", "--cached", "--name-only", "-z")
    return sorted(item.decode("utf-8", "surrogateescape")
                  for item in raw.split(b"\0") if item)


def scope_fingerprint(scope: str, root: Path = ROOT) -> tuple[str, int]:
    """Digest of the tracked files in one credential scope.

    Working-tree content wins over the index for modified/untracked files, so a
    receipt generated before ``git add`` still matches after staging.
    """
    if scope == "host":
        roots, files = HOST_ROOTS, HOST_FILES
    elif scope == "firmware":
        roots, files = FIRMWARE_ROOTS + ("tools/",), FIRMWARE_FILES
    else:
        raise ValueError(f"unknown credential scope: {scope}")
    entries: dict[str, tuple[str, str]] = {}
    raw = _git(root, "ls-files", "--stage", "-z")
    for entry in raw.split(b"\0"):
        if not entry or b"\t" not in entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        mode, oid, stage = metadata.split()
        if stage != b"0":
            continue
        path = raw_path.decode("utf-8", "surrogateescape").replace("\\", "/")
        if path in FINGERPRINT_EXCLUDES or not _matches(path, roots, files):
            continue
        entries[path] = (mode.decode("ascii"), oid.decode("ascii"))
    changed = _git(root, "ls-files", "--modified", "--deleted", "--others",
                   "--exclude-standard", "-z")
    for item in changed.split(b"\0"):
        if not item:
            continue
        path = item.decode("utf-8", "surrogateescape").replace("\\", "/")
        if path in FINGERPRINT_EXCLUDES or not _matches(path, roots, files):
            continue
        full = root / path
        if not full.is_file():
            entries.pop(path, None)
            continue
        oid = _git(root, "hash-object", f"--path={path}", path)
        mode = entries.get(path, ("100644", ""))[0]
        entries[path] = (mode, oid.decode("ascii").strip())
    digest = hashlib.sha256()
    for path in sorted(entries):
        mode, oid = entries[path]
        digest.update(f"{path}\0{mode}\0{oid}\n".encode("utf-8"))
    return digest.hexdigest(), len(entries)


def staged_scope_fingerprint(scope: str, root: Path = ROOT) -> tuple[str, int]:
    """Digest of the staged content only (what a commit would contain)."""
    roots, files = (HOST_ROOTS, HOST_FILES) if scope == "host" else (
        FIRMWARE_ROOTS + ("tools/",), FIRMWARE_FILES)
    digest = hashlib.sha256()
    count = 0
    raw = _git(root, "ls-files", "--stage", "-z")
    for entry in raw.split(b"\0"):
        if not entry or b"\t" not in entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        mode, oid, stage = metadata.split()
        if stage != b"0":
            continue
        path = raw_path.decode("utf-8", "surrogateescape").replace("\\", "/")
        if path in FINGERPRINT_EXCLUDES or not _matches(path, roots, files):
            continue
        digest.update(f"{path}\0{mode.decode('ascii')}\0{oid.decode('ascii')}\n"
                      .encode("utf-8"))
        count += 1
    return digest.hexdigest(), count


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staged", action="store_true",
                        help="classify the staged change set")
    parser.add_argument("--paths", nargs="*", default=None,
                        help="classify explicit paths instead of the index")
    parser.add_argument("--classes", action="store_true",
                        help="print the class names, space separated")
    parser.add_argument("--fingerprint", choices=("firmware", "host"),
                        help="print the scope fingerprint and file count")
    parser.add_argument("--staged-fingerprint", choices=("firmware", "host"),
                        help="print the staged scope fingerprint")
    args = parser.parse_args(argv)

    if args.fingerprint:
        digest, count = scope_fingerprint(args.fingerprint)
        print(f"{digest} {count}")
        return 0
    if args.staged_fingerprint:
        digest, count = staged_scope_fingerprint(args.staged_fingerprint)
        print(f"{digest} {count}")
        return 0
    paths = args.paths if args.paths is not None else (staged_paths() if args.staged else [])
    classes = classify_many(paths)
    if args.classes:
        print(" ".join(sorted(classes)))
    else:
        for name in sorted(classes):
            print(name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
