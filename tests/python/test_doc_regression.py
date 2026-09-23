from __future__ import annotations

from pathlib import Path

from tools.doc_regression_check import (
    PROGRESS_MAX_BYTES,
    Result,
    check_constants,
    check_freshness,
    check_orphan_clauses,
    check_progress_rotation,
    check_registry,
    check_skill_sync,
)
import tools.doc_regression_check as regress

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


def test_registry_validates_calibration_contract_rows(tmp_path: Path) -> None:
    _make_registry_tree(tmp_path)
    row = VALID_ROW.replace("TDMA-REASON-01", "CALIBRATION-PHASE-01")
    (tmp_path / "docs" / "check" / "DOCS_REGISTRY.md").write_text(
        row, encoding="utf-8")
    result = Result(failures=[], warnings=[])
    check_registry(tmp_path, result)
    assert result.failures == []
    assert not any("CALIBRATION-PHASE-01" in warning
                   for warning in result.warnings)


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


# ---- loop 5: progress-log rotation (C14) --------------------------------

LOG_REL = "docs/sync/SYNC_IO_TASK_PROGRESS.md"


def _log(entries: list[str], last_updated: str = "2026-09-17",
         tail: str = "") -> str:
    body = "".join(f"### {e} - title\n\n- body\n\n" for e in entries)
    return (
        "# Sync task progress\n\n"
        "Status: Active\nDomain: sync_io\n"
        f"Canonical: `{LOG_REL}`\nRelated: `docs/sync/X.md`\n"
        f"Last updated: {last_updated}\n\n"
        "## 当前 checkpoint\n\n" + body + tail
    )


def _put(tmp_path: Path, rel: str, text: str) -> None:
    path = tmp_path / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _index_row(archive: str, first: str, last: str, count: int) -> str:
    return (
        "\n## 归档索引\n\n| 文件 | ID 区间 | 条目数 | 归档日期 |\n|---|---|---|---|\n"
        f"| `{archive}` | {first}..{last} | {count} | 2026-09-17 |\n"
    )


def _run_progress(tmp_path: Path) -> Result:
    result = Result(failures=[], warnings=[])
    check_progress_rotation(tmp_path, result)
    return result


def test_progress_accepts_bounded_log(tmp_path: Path) -> None:
    _put(tmp_path, LOG_REL, _log(["SYNC-PROGRESS-20260917-001",
                                  "SYNC-PROGRESS-20260916-002"]))
    assert _run_progress(tmp_path).failures == []


def test_progress_accepts_rotated_log_with_closed_index(tmp_path: Path) -> None:
    archive = "docs/legacy/sync/LEGACY_SYNC_IO_TASK_PROGRESS_01.md"
    _put(tmp_path, archive, "# Archive\n\nStatus: Frozen\nLast updated: 2026-09-17\n\n"
                            "### SYNC-PROGRESS-20260916-002 - old\n\n- body\n")
    _put(tmp_path, LOG_REL, _log(
        ["SYNC-PROGRESS-20260917-001"],
        tail=_index_row(archive, "SYNC-PROGRESS-20260916-002",
                        "SYNC-PROGRESS-20260916-002", 1)))
    assert _run_progress(tmp_path).failures == []


def test_progress_rejects_oversize_log(tmp_path: Path) -> None:
    _put(tmp_path, LOG_REL, _log(
        ["SYNC-PROGRESS-20260917-001"],
        tail="### SYNC-PROGRESS-20260917-002 - big\n\n"
             + ("x" * (PROGRESS_MAX_BYTES + 16)) + "\n"))
    assert any("C14 R1" in f for f in _run_progress(tmp_path).failures)


def test_progress_warns_for_registered_debt(tmp_path: Path) -> None:
    _put(tmp_path, "docs/tdma/TDMA_TASK_PROGRESS.md", _log(
        ["TDMA-PROGRESS-20260917-001"],
        tail="### TDMA-PROGRESS-20260917-002 - big\n\n"
             + ("x" * (PROGRESS_MAX_BYTES + 16)) + "\n"))
    result = _run_progress(tmp_path)
    assert result.failures == []
    assert any("rotation debt" in w for w in result.warnings)


def test_progress_rejects_expired_rotation_debt(tmp_path: Path,
                                                monkeypatch) -> None:
    monkeypatch.setitem(regress.PROGRESS_ROTATION_DEBT,
                        "docs/tdma/TDMA_TASK_PROGRESS.md", "2020-01-01")
    _put(tmp_path, "docs/tdma/TDMA_TASK_PROGRESS.md", _log(
        ["TDMA-PROGRESS-20260917-001"],
        tail="### TDMA-PROGRESS-20260917-002 - big\n\n"
             + ("x" * (PROGRESS_MAX_BYTES + 16)) + "\n"))
    assert any("past its registered rotation deadline" in f
               for f in _run_progress(tmp_path).failures)


def test_progress_rejects_stale_last_updated(tmp_path: Path) -> None:
    _put(tmp_path, LOG_REL, _log(["SYNC-PROGRESS-20260917-001"],
                                 last_updated="2026-09-01"))
    assert any("C14 R5" in f for f in _run_progress(tmp_path).failures)


def test_progress_rejects_archived_entry_outside_indexed_range(
        tmp_path: Path) -> None:
    archive = "docs/legacy/sync/LEGACY_SYNC_IO_TASK_PROGRESS_01.md"
    _put(tmp_path, archive, "# Archive\n\nStatus: Frozen\nLast updated: 2026-09-17\n\n"
                            "### SYNC-PROGRESS-20260915-009 - old\n\n- body\n")
    _put(tmp_path, LOG_REL, _log(
        ["SYNC-PROGRESS-20260917-001"],
        tail=_index_row(archive, "SYNC-PROGRESS-20260916-001",
                        "SYNC-PROGRESS-20260916-002", 1)))
    assert any("outside the indexed range" in f
               for f in _run_progress(tmp_path).failures)


def test_progress_rejects_declared_count_mismatch(tmp_path: Path) -> None:
    archive = "docs/legacy/sync/LEGACY_SYNC_IO_TASK_PROGRESS_01.md"
    _put(tmp_path, archive, "# Archive\n\nStatus: Frozen\nLast updated: 2026-09-17\n\n"
                            "### SYNC-PROGRESS-20260916-002 - old\n\n- body\n")
    _put(tmp_path, LOG_REL, _log(
        ["SYNC-PROGRESS-20260917-001"],
        tail=_index_row(archive, "SYNC-PROGRESS-20260916-002",
                        "SYNC-PROGRESS-20260916-002", 7)))
    assert any("declares 7 entries" in f
               for f in _run_progress(tmp_path).failures)


def test_progress_rejects_id_in_both_log_and_archive(tmp_path: Path) -> None:
    archive = "docs/legacy/sync/LEGACY_SYNC_IO_TASK_PROGRESS_01.md"
    _put(tmp_path, archive, "# Archive\n\nStatus: Frozen\nLast updated: 2026-09-17\n\n"
                            "### SYNC-PROGRESS-20260917-001 - dup\n\n- body\n")
    _put(tmp_path, LOG_REL, _log(
        ["SYNC-PROGRESS-20260917-001"],
        tail=_index_row(archive, "SYNC-PROGRESS-20260916-001",
                        "SYNC-PROGRESS-20260917-001", 1)))
    assert any("lives in both" in f for f in _run_progress(tmp_path).failures)


def test_progress_rejects_archive_newer_than_retained(tmp_path: Path) -> None:
    archive = "docs/legacy/sync/LEGACY_SYNC_IO_TASK_PROGRESS_01.md"
    _put(tmp_path, archive, "# Archive\n\nStatus: Frozen\nLast updated: 2026-09-17\n\n"
                            "### SYNC-PROGRESS-20260918-001 - newer\n\n- body\n")
    _put(tmp_path, LOG_REL, _log(
        ["SYNC-PROGRESS-20260917-001"],
        tail=_index_row(archive, "SYNC-PROGRESS-20260917-001",
                        "SYNC-PROGRESS-20260918-001", 1)))
    assert any("must hold the oldest entries" in f
               for f in _run_progress(tmp_path).failures)


def test_progress_rejects_archive_outside_legacy_dir(tmp_path: Path) -> None:
    archive = "docs/sync/LEGACY_SYNC_IO_TASK_PROGRESS_01.md"
    _put(tmp_path, archive, "# Archive\n\nStatus: Frozen\nLast updated: 2026-09-17\n\n"
                            "### SYNC-PROGRESS-20260916-002 - old\n\n- body\n")
    _put(tmp_path, LOG_REL, _log(
        ["SYNC-PROGRESS-20260917-001"],
        tail=_index_row(archive, "SYNC-PROGRESS-20260916-002",
                        "SYNC-PROGRESS-20260916-002", 1)))
    assert any("C14 R3" in f for f in _run_progress(tmp_path).failures)


def test_progress_rejects_rotated_log_without_checkpoint(tmp_path: Path) -> None:
    archive = "docs/legacy/sync/LEGACY_SYNC_IO_TASK_PROGRESS_01.md"
    _put(tmp_path, archive, "# Archive\n\nStatus: Frozen\nLast updated: 2026-09-17\n\n"
                            "### SYNC-PROGRESS-20260916-002 - old\n\n- body\n")
    text = _log(["SYNC-PROGRESS-20260917-001"],
                tail=_index_row(archive, "SYNC-PROGRESS-20260916-002",
                                "SYNC-PROGRESS-20260916-002", 1))
    _put(tmp_path, LOG_REL, text.replace("## 当前 checkpoint", "## 说明"))
    assert any("no '## 当前 checkpoint'" in f
               for f in _run_progress(tmp_path).failures)


def test_progress_ignores_archive_trees(tmp_path: Path) -> None:
    # A canonical-shaped name under legacy/ must stay out of the rotation gate.
    _put(tmp_path, "docs/legacy/sync/SYNC_IO_TASK_PROGRESS.md",
         _log(["SYNC-PROGRESS-20260917-001"],
              tail="### SYNC-PROGRESS-20260917-002 - big\n\n"
                   + ("x" * (PROGRESS_MAX_BYTES + 16)) + "\n"))
    assert _run_progress(tmp_path).failures == []


# ---- C15: newest first (freshness descending) ---------------------------

ORDER_REL = "docs/ota/OTA_TASK_PROGRESS.md"


def test_progress_accepts_newest_first_log(tmp_path: Path) -> None:
    _put(tmp_path, ORDER_REL, _log(["OTA-PROGRESS-20260917-001",
                                    "OTA-PROGRESS-20260916-002",
                                    "OTA-PROGRESS-20260910-003"]))
    assert _run_progress(tmp_path).failures == []


def test_progress_rejects_ascending_log(tmp_path: Path) -> None:
    _put(tmp_path, ORDER_REL, _log(["OTA-PROGRESS-20260910-001",
                                    "OTA-PROGRESS-20260917-002"]))
    assert any("C15" in f for f in _run_progress(tmp_path).failures)


def test_progress_allows_any_same_day_order(tmp_path: Path) -> None:
    # 12 of 14 existing logs use ascending same-day blocks; C15 is date-level only.
    _put(tmp_path, ORDER_REL, _log(["OTA-PROGRESS-20260917-001",
                                    "OTA-PROGRESS-20260917-002",
                                    "OTA-PROGRESS-20260917-003"]))
    assert _run_progress(tmp_path).failures == []


def test_progress_warns_for_registered_order_debt(tmp_path: Path) -> None:
    _put(tmp_path, "docs/sync/SYNC_IO_TASK_PROGRESS.md",
         _log(["SYNC-PROGRESS-20260910-001", "SYNC-PROGRESS-20260917-002"]))
    result = _run_progress(tmp_path)
    assert result.failures == []
    assert any("order inversion" in w for w in result.warnings)


def test_progress_rejects_expired_order_debt(tmp_path: Path,
                                             monkeypatch) -> None:
    monkeypatch.setitem(regress.PROGRESS_ORDER_DEBT, ORDER_REL, "2020-01-01")
    _put(tmp_path, ORDER_REL, _log(["OTA-PROGRESS-20260910-001",
                                    "OTA-PROGRESS-20260917-002"]))
    assert any("past its registered deadline" in f
               for f in _run_progress(tmp_path).failures)


def test_progress_rejects_archive_not_newest_first(tmp_path: Path) -> None:
    archive = "docs/legacy/ota/LEGACY_OTA_TASK_PROGRESS_01.md"
    _put(tmp_path, archive, "# Archive\n\nStatus: Frozen\nLast updated: 2026-09-17\n\n"
                            "### OTA-PROGRESS-20260910-001 - old\n\n- body\n\n"
                            "### OTA-PROGRESS-20260916-002 - new\n\n- body\n")
    _put(tmp_path, ORDER_REL, _log(
        ["OTA-PROGRESS-20260917-001"],
        tail=_index_row(archive, "OTA-PROGRESS-20260916-002",
                        "OTA-PROGRESS-20260910-001", 2)))
    assert any("newest-first" in f for f in _run_progress(tmp_path).failures)


def test_progress_accepts_descending_index_range(tmp_path: Path) -> None:
    # The range is written newest..oldest, so the checker must accept it.
    archive = "docs/legacy/ota/LEGACY_OTA_TASK_PROGRESS_01.md"
    _put(tmp_path, archive, "# Archive\n\nStatus: Frozen\nLast updated: 2026-09-17\n\n"
                            "### OTA-PROGRESS-20260916-002 - new\n\n- body\n\n"
                            "### OTA-PROGRESS-20260910-001 - old\n\n- body\n")
    _put(tmp_path, ORDER_REL, _log(
        ["OTA-PROGRESS-20260917-001"],
        tail=_index_row(archive, "OTA-PROGRESS-20260916-002",
                        "OTA-PROGRESS-20260910-001", 2)))
    assert _run_progress(tmp_path).failures == []
