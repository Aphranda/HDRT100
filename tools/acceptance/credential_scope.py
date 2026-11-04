#!/usr/bin/env python3
"""Classify a staged change set into the acceptance credential it needs.

Three credential classes exist, each with its own evidence and fingerprint
scope so a test-only or docs-only change does not invalidate (or demand) a
hardware acceptance run:

``firmware``  production code and build files
              -> P3 hardware acceptance receipt (firmware fingerprint)
``host``      tests, hook wiring and every Python tool (incl. the gate itself)
              -> host acceptance receipt (tests/ + tools/ fingerprint)
``docs``      documentation corpus
              -> ``docs_check`` + ``doc_regression`` (no receipt)

The hardware credential covers firmware only (user directive 2026-11-02): a
tool, hook, test or doc change must never invalidate or demand a bench run.
Firmware membership is delegated to
``tools/hardware_acceptance/source_scope.is_acceptance_source`` so this
classifier and the P3 fingerprint cannot drift apart.

The classifier is the single source of truth used by ``.githooks/pre-commit``
and by the receipt checkers, so the routing cannot drift from the policy text
in ``AGENTS.md`` §2.7. Run ``--explain`` to print the current table.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Firmware membership is NOT defined here: it is delegated to
# tools/hardware_acceptance/source_scope.is_acceptance_source (one definition).

# Every Python tool is host-scope: a tool change must produce test evidence, and
# it must be bound to the host fingerprint so it cannot commit evidence-free.
# ``.agents/**/*.py`` are executable skill implementations (AGENTS.md §2
# 「文档格式范围」), so they carry the host credential too; SKILL.md stays docs.
HOST_ROOTS = ("tests/", ".githooks/", "tools/")
HOST_FILES = ("pytest.ini",)
HOST_AGENT_SCRIPT_ROOT = ".agents/"

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


def _host_member(path: str) -> bool:
    """Host credential membership; shared by classify() and the fingerprint.

    Markdown is documentation, never host evidence: a README under tools/ must
    not force a host run, and must not silently invalidate the host receipt.
    """
    if path.endswith(".md"):
        return False
    if _matches(path, HOST_ROOTS, HOST_FILES):
        return True
    return path.startswith(HOST_AGENT_SCRIPT_ROOT) and path.endswith(".py")


def classify(path: str) -> set[str]:
    """Credential classes a single repository path belongs to."""
    normalized = path.replace("\\", "/")
    while normalized.startswith("./"):
        normalized = normalized[2:]
    if normalized in FINGERPRINT_EXCLUDES:
        return set()
    if normalized.endswith(".md"):
        return {"docs"}
    classes: set[str] = set()
    # Hardware credential covers firmware only (2026-11-02 user directive): tests,
    # hooks, Python tools and skill scripts are host-scope.
    if _host_member(normalized):
        classes.add("host")
    if _acceptance_source(normalized):
        classes.add("firmware")
    if _matches(normalized, DOC_ROOTS, DOC_FILES):
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


_ACCEPTANCE_FILTER = None


def _acceptance_source(path: str) -> bool:
    """Firmware membership, delegated to the P3 source scope.

    Two definitions of "what is firmware" would let the classifier and the P3
    fingerprint drift apart, so this module never keeps its own copy.
    """
    global _ACCEPTANCE_FILTER
    if _ACCEPTANCE_FILTER is None:
        tools_dir = ROOT / "tools" / "hardware_acceptance"
        if str(tools_dir) not in sys.path:
            sys.path.insert(0, str(tools_dir))
        import source_scope  # noqa: PLC0415 - local import keeps this standalone

        _ACCEPTANCE_FILTER = source_scope.is_acceptance_source
    return _ACCEPTANCE_FILTER(path)


def _scope_filter(scope: str):
    if scope == "host":
        return _host_member
    if scope == "firmware":
        return _acceptance_source
    raise ValueError(f"unknown credential scope: {scope}")


def scope_fingerprint(scope: str, root: Path = ROOT) -> tuple[str, int]:
    """Digest of the tracked files in one credential scope.

    Working-tree content wins over the index for modified/untracked files, so a
    receipt generated before ``git add`` still matches after staging.
    """
    include = _scope_filter(scope)
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
        if path in FINGERPRINT_EXCLUDES or not include(path):
            continue
        entries[path] = (mode.decode("ascii"), oid.decode("ascii"))
    changed = _git(root, "ls-files", "--modified", "--deleted", "--others",
                   "--exclude-standard", "-z")
    for item in changed.split(b"\0"):
        if not item:
            continue
        path = item.decode("utf-8", "surrogateescape").replace("\\", "/")
        if path in FINGERPRINT_EXCLUDES or not include(path):
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
    include = _scope_filter(scope)
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
        if path in FINGERPRINT_EXCLUDES or not include(path):
            continue
        digest.update(f"{path}\0{mode.decode('ascii')}\0{oid.decode('ascii')}\n"
                      .encode("utf-8"))
        count += 1
    return digest.hexdigest(), count


def explain() -> str:
    """Current routing table; the docs point here instead of restating it."""
    host = ", ".join(HOST_ROOTS + HOST_FILES)
    return "\n".join([
        "credential classes",
        "  firmware : P3 receipt, fingerprint = tools/hardware_acceptance/"
        "source_scope.is_acceptance_source (firmware code + build files only)",
        "  host     : host receipt, fingerprint = " + host +
        " (non-.md) + .agents/**/*.py",
        "  docs     : docs_check + doc_regression, no receipt (.md always docs)",
        "  unclassified: .git, out/, artifacts/, build/, *.md under tools/ never"
        " enter a fingerprint",
        "  (receipts under config/hardware_acceptance/ never enter a fingerprint)",
    ])


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
    parser.add_argument("--explain", action="store_true",
                        help="print the current routing table (docs point here)")
    args = parser.parse_args(argv)

    if args.fingerprint:
        digest, count = scope_fingerprint(args.fingerprint)
        print(f"{digest} {count}")
        return 0
    if args.staged_fingerprint:
        digest, count = staged_scope_fingerprint(args.staged_fingerprint)
        print(f"{digest} {count}")
        return 0
    if args.explain:
        print(explain())
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
