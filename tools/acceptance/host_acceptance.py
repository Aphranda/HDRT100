#!/usr/bin/env python3
"""Host acceptance credential for test-only changes.

The P3 credential certifies firmware behaviour on the bench; it is the wrong
(and expensive) credential for a change that only touches host tests. This tool
runs the host tests that a staged change touches and records a receipt bound to
the *host* fingerprint (``tests/``, ``.githooks/``, ``tools/acceptance/``).

    python tools/acceptance/host_acceptance.py run [--paths FILE ...]
    python tools/acceptance/host_acceptance.py check-staged

``run`` defaults to the test files in the staged change set, falling back to
the split-unit guards when only gate wiring changed. ``check-staged`` fails
unless the recorded receipt was generated from the staged host content and
reports PASS.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "acceptance"))

import credential_scope  # noqa: E402

RECEIPT = ROOT / "config/hardware_acceptance/host_acceptance_receipt.json"
SCHEMA = "HAOFV_HOST_ACCEPTANCE_RECEIPT_V1"
DEFAULT_PATHS = (
    "tests/python/test_vdc_host_harness.py",
    "tests/python/test_split_unit_hooks.py",
    "tests/python/test_owner_hook_link.py",
)
SUMMARY = re.compile(
    r"(?P<passed>\d+) passed(?:, (?P<failed>\d+) failed)?"
    r"(?:, (?P<errors>\d+) error)?(?:, (?P<skipped>\d+) skipped)?")


class HostAcceptanceError(RuntimeError):
    pass


def staged_test_paths() -> list[str]:
    paths = []
    for path in credential_scope.staged_paths(ROOT):
        if path.startswith("tests/python/") and path.endswith(".py"):
            if (ROOT / path).is_file():
                paths.append(path)
    return paths


def _unstaged(path: str) -> bool:
    """True when the tested file differs from what the index would commit."""
    result = subprocess.run(["git", "diff", "--quiet", "--", path], cwd=ROOT)
    return result.returncode != 0


def run(paths: list[str]) -> dict:
    """Single-call entry point; delegates to the step workflow for execution.

    The workflow (``host_acceptance_workflow.py``) owns the numbered steps and
    the fail-closed ordering. This wrapper keeps the historical one-shot
    invocation used by the hook available and writes the same receipt schema.
    """
    import host_acceptance_workflow as workflow  # local: workflow imports this module

    execution = workflow.execute(paths)
    # The receipt certifies the *staged* tests: that is exactly what a commit
    # carries and what check-staged recomputes.
    fingerprint, count = credential_scope.staged_scope_fingerprint("host", ROOT)
    working, _working_count = credential_scope.scope_fingerprint("host", ROOT)
    receipt = dict(execution)
    receipt.update({
        "schema": SCHEMA,
        "fingerprint": fingerprint,
        "file_count": count,
        "working_fingerprint": working,
        "started_at_utc": datetime.now(timezone.utc).isoformat(),
    })
    RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    RECEIPT.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt


def check_staged() -> None:
    if not RECEIPT.is_file():
        raise HostAcceptanceError(
            "no host acceptance receipt; run tools/acceptance/host_acceptance.py run")
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    if receipt.get("schema") != SCHEMA or receipt.get("passed") is not True:
        raise HostAcceptanceError("host acceptance receipt is not PASS")
    digest, count = credential_scope.staged_scope_fingerprint("host", ROOT)
    if receipt.get("fingerprint") != digest:
        raise HostAcceptanceError(
            "host acceptance receipt does not match the staged tests; rerun "
            "tools/acceptance/host_acceptance.py run after staging")
    print(f"OK   host acceptance: files={count} tests={receipt.get('summary_line')} "
          f"generated={receipt.get('started_at_utc')}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    run_parser = sub.add_parser("run", help="run host tests and write the receipt")
    run_parser.add_argument("--paths", nargs="*", default=None)
    sub.add_parser("check-staged", help="verify the staged host credential")
    args = parser.parse_args(argv)

    try:
        if args.command == "run":
            paths = args.paths or staged_test_paths() or list(DEFAULT_PATHS)
            receipt = run(paths)
            print(f"{'PASS' if receipt['passed'] else 'FAIL'} host acceptance "
                  f"files={receipt['file_count']} {receipt.get('summary_line')}")
            print(f"receipt: {RECEIPT.relative_to(ROOT).as_posix()}")
            return 0 if receipt["passed"] else 1
        check_staged()
        return 0
    except HostAcceptanceError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
