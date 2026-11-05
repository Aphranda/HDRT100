"""Normative docs must not drift from the implementation they describe.

Every contradiction this file guards against was a real defect found by hand:

* a skill told agents to run ``tools/hardware_acceptance/p3_hardware_acceptance.py``,
  which had never existed;
* the documented step count (37) is a hard number that must track the decorator
  count in ``workflow_steps.py``;
* receipt schema literals quoted in docs must be schemas the checker accepts;
* a registry status typo would leave a contract invisible to the id/anchor gate.

The scan is limited to *normative* documents (agent orders, execution
constraints, the spec and the project skills). Historical records
(``DOCS_REGRESSION_REVIEW.md``, ``DOCS_REGRESSION_TODO.md``) legitimately cite
paths that no longer exist, so they are out of scope.
"""
from __future__ import annotations

import re
import subprocess
from pathlib import Path

import pytest

from tools.doc_regression_check import ID_RE, REGISTRY_REL, read_text, registry_rows

ROOT = Path(__file__).resolve().parents[2]

NORMATIVE_DOCS = [
    ROOT / "AGENTS.md",
    ROOT / "docs" / "check" / "DOCS_EXECUTION_CONSTRAINTS.md",
    ROOT / "docs" / "check" / "DOCS_REGRESSION_PLAN.md",
    ROOT / "docs" / "console" / "CONSOLE_WORKFLOW_ARCHITECTURE.md",
    ROOT / "tools" / "README.md",
    ROOT / "tools" / "tool_governance" / "README.md",
    *sorted((ROOT / ".agents" / "skills").glob("*/SKILL.md")),
]

TOOL_PATH_RE = re.compile(r"tools/[A-Za-z0-9_./-]+\.py")
STEP_COUNT_RE = re.compile(r"(\d+)\s*个步骤函数")
SCHEMA_RE = re.compile(r"HAOFV_[A-Z0-9_]*RECEIPT[A-Z0-9_]*_V\d+")
STATUSES = {"active", "pending", "superseded"}


def _docs_with_text() -> list[tuple[Path, str]]:
    docs = []
    for path in NORMATIVE_DOCS:
        if path.is_file():
            docs.append((path, read_text(path)))
    assert docs, "no normative documents found"
    return docs


def test_documented_tool_commands_exist() -> None:
    missing = []
    for path, text in _docs_with_text():
        for rel in sorted(set(TOOL_PATH_RE.findall(text))):
            if not (ROOT / rel).is_file():
                missing.append(f"{path.relative_to(ROOT).as_posix()}: {rel}")
    assert not missing, "documented tool paths do not exist: " + "; ".join(missing)


def test_documented_step_count_matches_implementation() -> None:
    source = read_text(ROOT / "tools/hardware_acceptance/workflow_steps.py")
    actual = len(re.findall(r"@acceptance_step\(", source))
    claimed = {
        int(match)
        for _, text in _docs_with_text()
        for match in STEP_COUNT_RE.findall(text)
    }
    assert claimed, "no step count is documented; the number must stay checkable"
    assert claimed == {actual}, (
        f"docs claim {sorted(claimed)} acceptance steps, implementation has {actual}")


def test_documented_receipt_schemas_exist() -> None:
    source = read_text(ROOT / "tools/hardware_acceptance/workflow_steps.py")
    known = set(re.findall(r'"(HAOFV_[A-Z0-9_]*RECEIPT[A-Z0-9_]*_V\d+)"', source))
    cited = {
        schema
        for _, text in _docs_with_text()
        for schema in SCHEMA_RE.findall(text)
    }
    assert cited <= known, f"docs cite unknown receipt schemas: {sorted(cited - known)}"


def test_registry_status_values_are_known() -> None:
    rows = registry_rows(read_text(ROOT / REGISTRY_REL))
    assert rows, "registry has no contract rows"
    bad = sorted({r[9] for r in rows if r[9] not in STATUSES})
    assert not bad, f"unknown registry status values: {bad}"


@pytest.mark.parametrize("contract_id", [
    "TDMA-SEAT-SHARE-01",
    "INTERFACE-T2ACTION-01",
    "DOCS-PROGRESSROTATE-01",
])
def test_contract_id_formats_are_accepted(contract_id: str) -> None:
    assert ID_RE.match(contract_id)


def test_out_is_local_evidence_only() -> None:
    """out/ must not become a sync location: agent drafts there stay local."""
    for name in ("out/agent-draft.md", "out/section-plan.md"):
        result = subprocess.run(
            ["git", "check-ignore", "-q", name], cwd=ROOT,
            capture_output=True, text=True)
        assert result.returncode == 0, (
            f"{name} is not ignored; out/ is a local evidence area and must not "
            "carry files that need cloud sync (see out/README.md §0)")
    for kept in ("out/README.md", "out/standards/OUT_DIRECTORY_STANDARD.md"):
        result = subprocess.run(
            ["git", "check-ignore", "-q", kept], cwd=ROOT,
            capture_output=True, text=True)
        assert result.returncode == 1, f"{kept} should stay tracked"
