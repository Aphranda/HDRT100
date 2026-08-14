from __future__ import annotations

from pathlib import Path

from tools.docs_check import docs_check


def test_docs_check_accepts_current_domain_filename() -> None:
    assert docs_check.is_allowed_name("VDC_DOMAIN_ARCHITECTURE.md")
    assert docs_check.is_allowed_name("REFMEM_TASK_PROGRESS.md")
    assert docs_check.is_allowed_name("SCPI_COMMANDS.md")


def test_docs_check_rejects_unmanaged_filename() -> None:
    assert not docs_check.is_allowed_name("random_notes.md")
    assert not docs_check.is_allowed_name("VDC_DOMAIN_UNKNOWN.md")


def test_docs_check_detects_broken_markdown_reference(tmp_path: Path) -> None:
    root = tmp_path
    doc = root / "sample.md"
    doc.write_text("See `docs/missing/MISSING_DOC.md`.\n", encoding="utf-8")

    result = docs_check.CheckResult(failures=[], warnings=[])
    docs_check.check_references(root, [doc], result)

    assert result.failures == [
        f"{doc}: broken docs reference: docs/missing/MISSING_DOC.md",
    ]
