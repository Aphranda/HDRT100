from __future__ import annotations

from pathlib import Path

from tools.docs_check import docs_check


def test_docs_check_accepts_current_domain_filename() -> None:
    assert docs_check.is_allowed_name("VDC_DOMAIN_ARCHITECTURE.md")
    assert docs_check.is_allowed_name("REFMEM_TASK_PROGRESS.md")
    assert docs_check.is_allowed_name("SCPI_COMMANDS.md")
    assert docs_check.is_allowed_name("STATE_MACHINE_DOMAIN_ARCHITECTURE.md")
    assert docs_check.is_allowed_name("STATE_MACHINE_DOMAIN_TODO.md")
    assert docs_check.is_allowed_name("STATE_MACHINE_TASK_PROGRESS.md")


def test_docs_check_accepts_added_allowlist_names() -> None:
    # C5 三同步：白名单新增（PRODUCT 前缀 / REGISTRY / REVIEW 后缀）必须有测试覆盖
    assert docs_check.is_allowed_name("PRODUCT_BOARD_MIGRATION_PLAN.md")
    assert docs_check.is_allowed_name("DOCS_REGISTRY.md")
    assert docs_check.is_allowed_name("REFMEM_DOMAIN_RISK_REVIEW.md")
    assert docs_check.is_allowed_name("TDMA_CODE_REVIEW.md")
    # 数字序号后缀容错（提交单归档）
    assert docs_check.is_allowed_name("TDMA_CROSS_REVIEW_01.md")


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


def test_docs_check_rejects_invalid_last_updated_date(tmp_path: Path) -> None:
    """P3-10: malformed/impossible Last updated dates must FAIL (G4)."""
    docs_dir = tmp_path / "docs"
    doc = docs_dir / "tdma" / "TDMA_BAD_DATE.md"
    doc.parent.mkdir(parents=True)
    text = (
        "# T\n"
        "\n"
        "Status: Active\n"
        "Domain: tdma\n"
        "Canonical: `docs/tdma/TDMA_BAD_DATE.md`\n"
        "Related: x\n"
        "Last updated: 2026-13-99\n"
    )
    doc.write_text(text, encoding="utf-8")
    result = docs_check.CheckResult(failures=[], warnings=[])
    docs_check.check_metadata(doc, docs_dir, text, result)
    assert any("Last updated" in f for f in result.failures)


def test_docs_check_reads_non_utf8_without_crash(tmp_path: Path) -> None:
    """P3-9: GBK/non-UTF-8 files must not crash docs_check (G4)."""
    docs_dir = tmp_path / "docs"
    doc = docs_dir / "tdma" / "TDMA_GBK.md"
    doc.parent.mkdir(parents=True)
    # GBK-encoded Chinese text (cannot be decoded as UTF-8)
    doc.write_bytes("Last updated: 2026-08-19\n中文内容".encode("gbk"))
    assert "Last updated" in docs_check.read_text(doc)
