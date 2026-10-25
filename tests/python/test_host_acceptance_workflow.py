"""Host acceptance is a numbered, fail-closed workflow, not a one-shot call."""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "acceptance"))

import credential_scope  # noqa: E402
import host_acceptance  # noqa: E402
import host_acceptance_workflow as workflow  # noqa: E402


def test_steps_are_fixed_unique_and_ordered() -> None:
    names = [name for name, _ in workflow.STEPS]
    assert names == ["prepare", "scope", "select", "execute", "receipt", "selfcheck"]
    assert len(set(names)) == len(names)
    assert all(title for _, title in workflow.STEPS)
    # The credential may only be written after a green execution.
    assert names.index("execute") < names.index("receipt") < names.index("selfcheck")
    assert workflow.PROFILES == ("quick", "full")


def test_quick_paths_prefer_staged_then_defaults() -> None:
    paths = workflow.selected_paths("quick", None)
    assert paths, "quick profile must always select something"
    for path in paths:
        assert (ROOT / path).is_file(), path
    explicit = ["tests/python/test_split_unit_hooks.py"]
    assert workflow.selected_paths("quick", explicit) == explicit


def test_full_profile_collects_the_python_suite() -> None:
    paths = workflow.selected_paths("full", None)
    assert len(paths) > 20
    assert all(path.startswith("tests/python/test_") and path.endswith(".py")
               for path in paths)


def test_execute_reports_green_without_writing_a_receipt(tmp_path: Path) -> None:
    before = host_acceptance.RECEIPT.read_bytes() if host_acceptance.RECEIPT.is_file() else None
    result = workflow.execute(["tests/python/test_split_unit_hooks.py"])
    assert result["passed"] is True
    assert result["summary"]["passed"] > 0
    assert result["returncode"] == 0
    after = host_acceptance.RECEIPT.read_bytes() if host_acceptance.RECEIPT.is_file() else None
    assert after == before, "the execute step must not write the credential"


def test_execute_reports_failure_for_a_missing_path() -> None:
    result = workflow.execute(["tests/python/does_not_exist_acceptance.py"])
    assert result["passed"] is False
    assert result["returncode"] != 0


def test_resume_requires_matching_state(tmp_path: Path) -> None:
    with pytest.raises(workflow.WorkflowError):
        workflow.run_steps(tmp_path / "fresh", "quick", None, resume=True)


def test_scope_requires_staged_host_files(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setattr(credential_scope, "staged_scope_fingerprint",
                        lambda *args, **kwargs: ("", 0))
    with pytest.raises(workflow.WorkflowError):
        workflow.run_steps(tmp_path / "scope", "quick", None, resume=False)


def test_failed_execute_never_reaches_the_receipt(tmp_path: Path, monkeypatch) -> None:
    def fake_execute(paths):
        return {"passed": False, "summary_line": "1 failed", "summary": {"failed": 1},
                "returncode": 1, "unstaged_tested_files": [], "command": [],
                "paths": paths, "duration_s": 0.0, "tail": []}

    monkeypatch.setattr(workflow, "execute", fake_execute)
    monkeypatch.setattr(credential_scope, "staged_scope_fingerprint",
                        lambda *args, **kwargs: ("deadbeef", 3))
    run_dir = tmp_path / "failed"
    with pytest.raises(workflow.WorkflowError):
        workflow.run_steps(run_dir, "quick",
                           ["tests/python/test_split_unit_hooks.py"], resume=False)
    state = json.loads((run_dir / "workflow-state.json").read_text(encoding="utf-8"))
    assert state["steps"][-1]["name"] == "execute"
    assert state["steps"][-1]["status"] == "FAIL"
    assert "receipt" not in (run_dir / "workflow.log").read_text(encoding="utf-8").split(
        "=== Step execute")[-1]
