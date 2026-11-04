"""Workflow-form triage rules (2026-11-04 user directive).

A used script must be judged: WORKFLOW / STEP / STANDALONE / LIBRARY. The gate
must fail when a ``*_workflow.py`` entrypoint is not triaged WORKFLOW, when a
non-UNASSESSED family has no row, and when a WORKFLOW row lacks an evidence
root or resume description. UNASSESSED legacy families stay warnings.
"""
from __future__ import annotations

import json
from pathlib import Path

from tools.tool_governance import workflow_triage


def _tree(tmp_path: Path, tools: list[dict], entries: list[dict],
          files: dict[str, str] | None = None) -> Path:
    (tmp_path / "tools").mkdir(parents=True)
    (tmp_path / "tools" / "tool_registry.json").write_text(
        json.dumps({"schema_version": 1, "tools": tools}), encoding="utf-8")
    (tmp_path / "tools" / "tool_workflow_triage.json").write_text(
        json.dumps({"schema": "TOOLS_WORKFLOW_TRIAGE_V1", "entries": entries}),
        encoding="utf-8")
    for rel, text in (files or {}).items():
        path = tmp_path / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return tmp_path


def _tool(tool_id: str, lifecycle: str = "EXPERIMENTAL",
          entrypoints: list[str] | None = None) -> dict:
    return {"id": tool_id, "lifecycle": lifecycle,
            "operation": "OFFLINE", "entrypoints": entrypoints or []}


def test_workflow_row_passes_with_run_and_out(tmp_path: Path) -> None:
    root = _tree(
        tmp_path, [_tool("TOOL-A")],
        [{"tool": "TOOL-A", "form": "WORKFLOW",
          "entry": "tools/a/a_workflow.py", "evidence_root": "out/a/",
          "resume": "--run <dir>", "reason": "multi-stage"}],
        {"tools/a/a_workflow.py": 'p.add_argument("--run")\n'})
    errors, _warnings = workflow_triage.check(root)
    assert errors == []


def test_workflow_without_resume_or_out_fails(tmp_path: Path) -> None:
    root = _tree(
        tmp_path, [_tool("TOOL-A")],
        [{"tool": "TOOL-A", "form": "WORKFLOW",
          "entry": "tools/a/a.py", "evidence_root": "build/", "reason": "x"}],
        {"tools/a/a.py": "print(1)\n"})
    errors, _warnings = workflow_triage.check(root)
    assert any("evidence_root must start with out/" in e for e in errors)
    assert any("must declare how to resume" in e for e in errors)


def test_untriaged_workflow_entrypoint_fails(tmp_path: Path) -> None:
    root = _tree(
        tmp_path, [_tool("TOOL-A", entrypoints=["tools/a/a_workflow.py"])],
        [{"tool": "TOOL-A", "form": "STANDALONE", "reason": "x"}],
        {"tools/a/a_workflow.py": "print(1)\n"})
    errors, _warnings = workflow_triage.check(root)
    assert any("is a workflow entrypoint but TOOL-A is not triaged WORKFLOW" in e
               for e in errors)


def test_non_unassessed_family_without_row_fails(tmp_path: Path) -> None:
    root = _tree(tmp_path, [_tool("TOOL-A", lifecycle="SUPPORTED")], [])
    errors, warnings = workflow_triage.check(root)
    assert any("has no triage row" in e for e in errors)
    assert all("UNASSESSED family not judged" not in w for w in warnings)


def test_unassessed_family_is_debt_not_error(tmp_path: Path) -> None:
    root = _tree(tmp_path, [_tool("TOOL-LEGACY", lifecycle="UNASSESSED")], [])
    errors, warnings = workflow_triage.check(root)
    assert errors == []
    assert any("UNASSESSED family not judged yet" in w for w in warnings)


def test_step_must_point_at_a_workflow_and_library_has_no_entrypoint(
        tmp_path: Path) -> None:
    root = _tree(
        tmp_path,
        [_tool("TOOL-W"), _tool("TOOL-S"), _tool("TOOL-L", entrypoints=["tools/l/l.py"])],
        [
            {"tool": "TOOL-W", "form": "WORKFLOW", "entry": "tools/w/w_workflow.py",
             "evidence_root": "out/w/", "resume": "run", "reason": "x"},
            {"tool": "TOOL-S", "form": "STEP", "workflow": "TOOL-NOPE", "reason": "x"},
            {"tool": "TOOL-L", "form": "LIBRARY", "reason": "x"},
        ],
        {"tools/w/w_workflow.py": 'subparsers.add_parser("run")\n',
         "tools/l/l.py": "print(1)\n"})
    errors, _warnings = workflow_triage.check(root)
    assert any("STEP must name an earlier WORKFLOW row" in e for e in errors)
    assert any("LIBRARY must not declare entrypoints" in e for e in errors)


def test_unknown_tool_and_duplicate_rows_fail(tmp_path: Path) -> None:
    root = _tree(
        tmp_path, [_tool("TOOL-A")],
        [{"tool": "TOOL-A", "form": "STANDALONE", "reason": "x"},
         {"tool": "TOOL-A", "form": "STANDALONE", "reason": "x"},
         {"tool": "TOOL-GHOST", "form": "STANDALONE", "reason": "x"}])
    errors, _warnings = workflow_triage.check(root)
    assert any("duplicate triage row" in e for e in errors)
    assert any("not in tool_registry.json" in e for e in errors)


def test_migration_target_is_reported_as_warning(tmp_path: Path) -> None:
    root = _tree(
        tmp_path, [_tool("TOOL-A")],
        [{"tool": "TOOL-A", "form": "STANDALONE", "target_form": "WORKFLOW",
          "milestone": "M7 校准训练 workflow", "reason": "x"}])
    errors, warnings = workflow_triage.check(root)
    assert errors == []
    assert any("migration pending STANDALONE -> WORKFLOW (M7 校准训练 workflow)" in w
               for w in warnings)


def test_invalid_target_form_fails(tmp_path: Path) -> None:
    root = _tree(
        tmp_path, [_tool("TOOL-A")],
        [{"tool": "TOOL-A", "form": "STANDALONE", "target_form": "BOGUS",
          "reason": "x"}])
    errors, _warnings = workflow_triage.check(root)
    assert any("target_form 'BOGUS'" in e for e in errors)
