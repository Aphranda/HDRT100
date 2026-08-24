from __future__ import annotations

from pathlib import Path

from tools.doc_regression_check import (
    Result,
    check_constants,
    check_freshness,
    check_orphan_clauses,
    check_registry,
    check_skill_sync,
)

VALID_ROW = (
    "| contract_id | domain | contract | ver | clause_loc | code_anchor | check | registered | status |\n"
    "|---|---|---|---|---|---|---|---|---|\n"
    "| TDMA-REASON-01 | tdma | ring reason code | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md "
    "| tdma_ring_runtime.h | enum 比对 | 2026-08-19 | active |\n"
)


def _make_registry_tree(tmp_path: Path) -> None:
    (tmp_path / "docs" / "check").mkdir(parents=True)
    (tmp_path / "docs" / "tdma").mkdir(parents=True)
    (tmp_path / "components" / "tdma" / "inc").mkdir(parents=True)
    (tmp_path / "docs" / "tdma" / "TDMA_DOMAIN_ARCHITECTURE.md").write_text(
        "Status: Active\nLast updated: 2026-08-19\n", encoding="utf-8"
    )
    (tmp_path / "components" / "tdma" / "inc" / "tdma_ring_runtime.h").write_text(
        "", encoding="utf-8"
    )


def test_registry_accepts_valid_contract_rows(tmp_path: Path) -> None:
    _make_registry_tree(tmp_path)
    (tmp_path / "docs" / "check" / "DOCS_REGISTRY.md").write_text(
        VALID_ROW, encoding="utf-8"
    )
    result = Result(failures=[], warnings=[])
    check_registry(tmp_path, result)
    assert result.failures == []


def test_registry_rejects_duplicate_ids(tmp_path: Path) -> None:
    _make_registry_tree(tmp_path)
    dup = (
        "| contract_id | domain | contract | ver | clause_loc | code_anchor | check | registered | status |\n"
        "|---|---|---|---|---|---|---|---|---|\n"
        "| TDMA-REASON-01 | tdma | a | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_ring_runtime.h | x | 2026-08-19 | active |\n"
        "| TDMA-REASON-01 | tdma | b | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_ring_runtime.h | x | 2026-08-19 | active |\n"
    )
    (tmp_path / "docs" / "check" / "DOCS_REGISTRY.md").write_text(dup, encoding="utf-8")
    result = Result(failures=[], warnings=[])
    check_registry(tmp_path, result)
    assert any("duplicate" in f for f in result.failures)


def test_registry_rejects_missing_code_anchor(tmp_path: Path) -> None:
    (tmp_path / "docs" / "check").mkdir(parents=True)
    (tmp_path / "docs" / "tdma").mkdir(parents=True)
    (tmp_path / "docs" / "tdma" / "TDMA_DOMAIN_ARCHITECTURE.md").write_text(
        "", encoding="utf-8"
    )
    row = VALID_ROW.replace("tdma_ring_runtime.h", "no_such_file.h")
    (tmp_path / "docs" / "check" / "DOCS_REGISTRY.md").write_text(row, encoding="utf-8")
    result = Result(failures=[], warnings=[])
    check_registry(tmp_path, result)
    assert any("code_anchor" in f for f in result.failures)


def test_registry_warns_nonconforming_row_id(tmp_path: Path) -> None:
    """P3-8: a row that looks like a contract row but does not match ROW_RE
    (e.g. double-segment id TDMA-FLIGHT-BITMAP-01) must be WARNed, not
    silently skipped from counting/validation."""
    _make_registry_tree(tmp_path)
    row = VALID_ROW.replace("TDMA-REASON-01", "TDMA-FLIGHT-BITMAP-01")
    (tmp_path / "docs" / "check" / "DOCS_REGISTRY.md").write_text(row, encoding="utf-8")
    result = Result(failures=[], warnings=[])
    check_registry(tmp_path, result)
    assert any("does not match ROW_RE" in w for w in result.warnings)


def test_freshness_rejects_stale_top_doc(tmp_path: Path) -> None:
    (tmp_path / "docs" / "arch").mkdir(parents=True)
    (tmp_path / "docs" / "tdma").mkdir(parents=True)
    (tmp_path / "docs" / "arch" / "HAOFV_ARCHITECTURE.md").write_text(
        "Last updated: 2026-08-01\n", encoding="utf-8"
    )
    (tmp_path / "docs" / "tdma" / "TDMA_DOMAIN_ARCHITECTURE.md").write_text(
        "Last updated: 2026-08-19\n", encoding="utf-8"
    )
    result = Result(failures=[], warnings=[])
    check_freshness(tmp_path, result, set())
    assert any("freshness" in f for f in result.failures)


def test_freshness_accepts_fresh_top_doc(tmp_path: Path) -> None:
    (tmp_path / "docs" / "arch").mkdir(parents=True)
    (tmp_path / "docs" / "tdma").mkdir(parents=True)
    (tmp_path / "docs" / "arch" / "HAOFV_ARCHITECTURE.md").write_text(
        "Last updated: 2026-08-19\n", encoding="utf-8"
    )
    (tmp_path / "docs" / "tdma" / "TDMA_DOMAIN_ARCHITECTURE.md").write_text(
        "Last updated: 2026-08-19\n", encoding="utf-8"
    )
    result = Result(failures=[], warnings=[])
    check_freshness(tmp_path, result, set())
    assert result.failures == []


def test_malformed_date_does_not_crash(tmp_path: Path) -> None:
    (tmp_path / "docs" / "arch").mkdir(parents=True)
    (tmp_path / "docs" / "tdma").mkdir(parents=True)
    (tmp_path / "docs" / "arch" / "HAOFV_ARCHITECTURE.md").write_text(
        "Last updated: 2026-08-13\n", encoding="utf-8"
    )
    (tmp_path / "docs" / "tdma" / "BAD_DATE.md").write_text(
        "Last updated: 2026-13-99\n", encoding="utf-8"
    )
    result = Result(failures=[], warnings=[])
    check_freshness(tmp_path, result, set())  # must not raise
    assert result.failures == []


def test_orphan_detects_uncovered_constraint(tmp_path: Path) -> None:
    (tmp_path / "docs" / "arch").mkdir(parents=True)
    (tmp_path / "docs" / "check").mkdir(parents=True)
    (tmp_path / "docs" / "arch" / "HAOFV_ARCHITECTURE.md").write_text(
        "### 顶层安全硬约束\n\n| 约束 | 规则 |\n|---|---|\n"
        "| 未知约束A | rule |\n\n## 分层职责\n",
        encoding="utf-8",
    )
    (tmp_path / "docs" / "check" / "DOCS_REGISTRY.md").write_text(
        "## 条款落点表\n\n| clause_id | 顶层条款 | domain_loc | module | verify | status |\n"
        "|---|---|---|---|---|---|\n"
        "| HAOFV-137 | 双核 Flash/XIP 安全 | x | y | z | PENDING |\n",
        encoding="utf-8",
    )
    result = Result(failures=[], warnings=[])
    check_orphan_clauses(tmp_path, result)
    assert any("orphan" in f for f in result.failures)


def test_skill_sync_detects_script_drift(
    tmp_path: Path, monkeypatch
) -> None:
    monkeypatch.setenv("DOC_SKILL_HARNESS_DIR", str(tmp_path / "no-harness"))
    (tmp_path / "tools").mkdir()
    (tmp_path / ".agents" / "skills" / "doc-self-regression").mkdir(parents=True)
    (tmp_path / "tools" / "doc_regression_check.py").write_text(
        "LIVE", encoding="utf-8"
    )
    (tmp_path / ".agents" / "skills" / "doc-self-regression" / "doc_regression_check.py").write_text(
        "SNAPSHOT", encoding="utf-8"
    )
    result = Result(failures=[], warnings=[])
    check_skill_sync(tmp_path, result)
    assert any("skill-sync" in f for f in result.failures)


def test_constants_detects_mismatch(tmp_path: Path) -> None:
    (tmp_path / "docs" / "tdma").mkdir(parents=True)
    (tmp_path / "components" / "tdma" / "inc").mkdir(parents=True)
    (tmp_path / "docs" / "tdma" / "T.md").write_text(
        "```c\n#define TDMA_MAX 292\n```\n", encoding="utf-8"
    )
    (tmp_path / "components" / "tdma" / "inc" / "t.h").write_text(
        "#define TDMA_MAX 1024\n", encoding="utf-8"
    )
    result = Result(failures=[], warnings=[])
    check_constants(tmp_path, result, ["docs"])
    assert any("doc=292 code=1024" in f for f in result.failures)


def test_constants_skips_snapshot_marked(tmp_path: Path) -> None:
    (tmp_path / "docs" / "tdma").mkdir(parents=True)
    (tmp_path / "docs" / "tdma" / "T.md").write_text(
        "快照，非事实源:\n```c\n#define TDMA_MAX 292\n```\n", encoding="utf-8"
    )
    result = Result(failures=[], warnings=[])
    check_constants(tmp_path, result, ["docs"])
    assert result.failures == []
