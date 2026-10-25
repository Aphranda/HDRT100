"""Credential-class routing: firmware (P3), host (pytest), docs (gates).

The 2026-10-25 policy split keeps a test-only change from demanding a bench
run, while firmware and acceptance-flow changes still need the P3 receipt.
"""
import json
import subprocess
from pathlib import Path

import pytest

from tools.acceptance import credential_scope, host_acceptance
from tools.hardware_acceptance import p3_hardware_acceptance


@pytest.mark.parametrize("path,expected", [
    ("components/vdc_dpll_manager/src/vdc_priority_follow.c", ["firmware"]),
    ("application/src/app.c", ["firmware"]),
    ("CMakeLists.txt", ["firmware"]),
    ("tools/hardware_acceptance/p3_hardware_acceptance.py", ["firmware"]),
    ("tools/vdc_priority_trace/vdc_priority_joint_capture.py", ["firmware"]),
    ("tools/acceptance/host_acceptance.py", ["host"]),
    ("tests/python/test_vdc_priority_phase.py", ["host"]),
    (".githooks/pre-commit", ["host"]),
    ("docs/vdc/VDC_TASK_PROGRESS.md", ["docs"]),
    ("AGENTS.md", ["docs"]),
    ("docs/validation/P3_FLOW.html", ["docs"]),
])
def test_single_path_classes(path, expected):
    assert sorted(credential_scope.classify(path)) == expected


def test_receipts_never_change_the_fingerprint_they_certify():
    assert credential_scope.classify(
        "config/hardware_acceptance/p3_acceptance_receipt.json") == set()
    assert credential_scope.classify(
        "config/hardware_acceptance/host_acceptance_receipt.json") == set()


def test_mixed_change_needs_every_class():
    classes = credential_scope.classify_many([
        "components/tdma/src/tdma_service.c",
        "tests/python/test_vdc_host_harness.py",
        "docs/vdc/VDC_TASK_PROGRESS.md",
    ])
    assert classes == {"firmware", "host", "docs"}


def _repo(tmp_path: Path) -> Path:
    repo = tmp_path / "repo"
    (repo / "tests/python").mkdir(parents=True)
    (repo / "components/tdma/src").mkdir(parents=True)
    (repo / "docs").mkdir()
    (repo / "tests/python/test_a.py").write_text("print(1)\n", encoding="utf-8")
    (repo / "components/tdma/src/tdma_service.c").write_text("int x;\n", encoding="utf-8")
    (repo / "docs/readme.md").write_text("# doc\n", encoding="utf-8")
    subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
    subprocess.run(["git", "add", "-A"], cwd=repo, check=True)
    return repo


def test_scope_fingerprints_are_separate_and_change_with_content(tmp_path):
    repo = _repo(tmp_path)
    host_before, host_count = credential_scope.scope_fingerprint("host", repo)
    firmware_before, firmware_count = credential_scope.scope_fingerprint("firmware", repo)
    assert host_count == 1 and firmware_count == 1
    assert host_before != firmware_before

    (repo / "docs/readme.md").write_text("# doc v2\n", encoding="utf-8")
    assert credential_scope.scope_fingerprint("host", repo)[0] == host_before
    assert credential_scope.scope_fingerprint("firmware", repo)[0] == firmware_before

    (repo / "tests/python/test_a.py").write_text("print(2)\n", encoding="utf-8")
    assert credential_scope.scope_fingerprint("host", repo)[0] != host_before
    assert credential_scope.scope_fingerprint("firmware", repo)[0] == firmware_before


def test_untracked_host_file_counts_like_a_staged_one(tmp_path):
    repo = _repo(tmp_path)
    before, count = credential_scope.scope_fingerprint("host", repo)
    (repo / "tests/python/test_b.py").write_text("print(3)\n", encoding="utf-8")
    after, after_count = credential_scope.scope_fingerprint("host", repo)
    assert after != before and after_count == count + 1


def test_summary_parser_reads_pytest_output():
    match = host_acceptance.SUMMARY.search("12 passed, 3 failed, 1 error, 2 skipped in 4.5s")
    assert match and [match.group(name) for name in
                      ("passed", "failed", "errors", "skipped")] == ["12", "3", "1", "2"]
    match = host_acceptance.SUMMARY.search("81 passed in 12.5s")
    assert match and match.group("passed") == "81" and match.group("failed") is None


def test_check_staged_rejects_missing_or_stale_receipt(tmp_path, monkeypatch):
    repo = _repo(tmp_path)
    receipt = tmp_path / "receipt.json"
    monkeypatch.setattr(credential_scope, "ROOT", repo)
    monkeypatch.setattr(host_acceptance, "ROOT", repo)
    monkeypatch.setattr(host_acceptance, "RECEIPT", receipt)

    with pytest.raises(host_acceptance.HostAcceptanceError, match="no host acceptance receipt"):
        host_acceptance.check_staged()

    receipt.write_text(json.dumps({
        "schema": host_acceptance.SCHEMA, "passed": True,
        "fingerprint": "0" * 64, "file_count": 1,
        "summary_line": "1 passed", "paths": ["tests/python/test_a.py"],
    }), encoding="utf-8")
    with pytest.raises(host_acceptance.HostAcceptanceError, match="does not match the staged"):
        host_acceptance.check_staged()

    digest, count = credential_scope.staged_scope_fingerprint("host", repo)
    receipt.write_text(json.dumps({
        "schema": host_acceptance.SCHEMA, "passed": True,
        "fingerprint": digest, "file_count": count,
        "summary_line": "1 passed", "paths": ["tests/python/test_a.py"],
    }), encoding="utf-8")
    host_acceptance.check_staged()  # no exception


def test_p3_fingerprint_ignores_host_credential_scope():
    """A test or hook change must not invalidate the hardware credential."""
    assert p3_hardware_acceptance.is_acceptance_source(
        "components/vdc_dpll_manager/src/vdc_priority_follow.c")
    assert p3_hardware_acceptance.is_acceptance_source(
        "tools/hardware_acceptance/p3_hardware_acceptance.py")
    assert not p3_hardware_acceptance.is_acceptance_source(
        "tests/python/test_vdc_priority_phase.py")
    assert not p3_hardware_acceptance.is_acceptance_source(".githooks/pre-commit")
    assert not p3_hardware_acceptance.is_acceptance_source(
        "tools/acceptance/host_acceptance.py")
