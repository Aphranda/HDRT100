from __future__ import annotations

from pathlib import Path

from tools.docs_check import docs_check


def test_docs_check_accepts_current_domain_filename() -> None:
    assert docs_check.is_allowed_name("VDC_DOMAIN_ARCHITECTURE.md")
    assert docs_check.is_allowed_name("REFMEM_TASK_PROGRESS.md")
    assert docs_check.is_allowed_name("SCPI_COMMANDS.md")


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
