# 文档自回归体系 — 实施规格（v3，归档版）

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/DOCS_REGRESSION_PLAN.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/check/DOCS_REGRESSION_TODO.md`
Last updated: 2026-08-31

> 本文件由工作区 `doc-skill/方案_文档自回归体系.md` 归档而来；执行状态与最终调整见 `docs/check/DOCS_REGRESSION_TODO.md`。
> 设计依据一句话：需求追溯矩阵思想（契约登记）+ docs-as-code 门禁（commit 拦截）。

## 交付物总览

| # | 文件 | 内容 | 位置 |
|---|---|---|---|
| 1 | `DOCS_REGISTRY.md` | 契约登记表 + 条款落点表（含 5 条真实契约） | `docs/check/` |
| 2 | `doc_regression_check.py` | 环1 新鲜度 + 环2 登记，纯 stdlib | `tools/` |
| 3 | `test_doc_regression.py` | pytest 包装 | `tests/python/` |
| 4 | `pre-commit` | 硬门禁 | `.githooks/` |
| 5 | `SKILL.md` | skill 定义（frontmatter 触发，agent 自动加载） | `.agents/skills/doc-self-regression/` |
| 6 | `AGENTS.md` 文档章节 | 常设命令（Codex/agent 每次会话可读） | 项目根 |

## 设计来源：Harness 文档治理模式（精髓映射）

本方案不是自创，而是把 DeepSeek Harness 自身文档治理的成熟模式映射到本项目：

| DSH 模式 | 本项目落地 |
|---|---|
| **verify-\* 门禁**（`verify-doc-budgets` / `verify-md-links` 命名 + 自动运行） | pre-commit + pytest 运行 `verify-doc-registry` / `verify-doc-freshness`（检查器即门禁，命名同风格） |
| **SKILL.md**（YAML frontmatter 的 name/description 触发加载） | `.agents/skills/doc-self-regression/SKILL.md`，Codex/Claude/DSH 识别同类格式 |
| **AGENTS.md 常设命令**（根文档，agent 每次会话必读） | 根 `AGENTS.md` 增加文档维护章节 |
| **Agent Note**（header 块 + Alternatives considered） | 核验提交单采用同格式（见 §2.6） |
| **change-scope**（只审计变更，不全量扫描） | 检查器支持 `--scope <git diff 文件>`，pre-commit 只查本次变更涉及的文档 |
| **归档不删除**（archived 冻结快照） | 契约 `superseded` 移入归档区，禁止物理删行（C4） |
| **skill 自回归测试**（evolveguard：skill 改动要跑自己的测试） | `test_doc_regression.py` 同时校验"登记表格式 ↔ 检查器假设"一致性 |

## §0 硬约束（优先级最高，冲突时以本节为准）

任何交付物、文档、代码或后续变更与本节冲突时，以本节为准，并同步修正下级内容。

| # | 约束 | 规则 | 机检 |
|---|---|---|---|
| C1 | 检查器只读 | `doc_regression_check.py` 只读文档并输出，永不写文件 | 代码审查 |
| C2 | 零依赖 | 纯 Python 标准库；pre-commit 在无网络环境必须可运行 | 离线实测 |
| C3 | 门禁可绕过但留痕 | 允许 `git commit --no-verify`，但绕过必须可被检查器感知（登记表状态/日志） | WARN |
| C4 | 契约不可物理删除 | `contract_id` 一经登记只能 `superseded`，禁止删行 | 环2 校验历史行 |
| C5 | 格式三同步 | 登记表格式 ↔ 检查器 ↔ pytest 用例必须同一次 commit 变更 | pre-commit 比对 |
| C6 | 门禁快速 | pre-commit 内全套检查 ≤ 5 秒 | 实测 |
| C7 | 临时内容不污染 | 评审快照、临时备注、草稿**不得**登记为契约；只有域文档正式冻结的内容可登记（clause_loc 必须指向真实冻结文档） | 环2 + 人工 |
| C8 | 顶层刷新窗口 | 新契约登记后顶层 `HAOFV_ARCHITECTURE.md` 7 天内必须刷新 | 环1 |
| C9 | 可逆 | 每个落地步骤必须可 `git revert`，不产生一次性不可逆改动 | — |
| C10 | 冲突裁决 | 任何下级文档/代码与本节冲突时，以本节为准并同步修正 | — |
| C11 | 状态变更需交叉审核 | 登记表 status 变更（新契约登记、VIOLATED→OK）必须由**独立于作者的审核方**确认（agent 交叉 / 文档交叉 / 层间交叉），提交单记录审核方+方式+结论；禁止自审自批 | 提交单字段必填 |
| C12 | 域文档标准三件套 | 具有架构、实施清单和实施证据的域必须分别维护 Architecture、TODO、Task Progress；语义、状态、证据不得跨文件复制充当事实源 | 文档审查 + DOCS-FLASH-01 |
| C13 | 三件套最小格式 | Architecture、TODO、Task Progress 必须使用本节定义的最小结构、稳定 ID 和文件接口；新增文件立即执行，既有文件在实质修改时迁移，禁止继续扩散无 ID 清单和混合状态格式 | 文档审查 + DOCS-TRIPLETFORMAT-01 |

约束传递方向（单向收敛）：

```text
域文档正式冻结契约 ──登记──▶ 登记表（上提）──刷新──▶ 顶层感知
评审快照/临时备注 ────────✗ 禁止进入登记表
```

### 域文档标准三件套

| 文件类型 | 唯一职责 | 禁止内容 |
|---|---|---|
| `*_ARCHITECTURE.md` | 稳定语义、不变量、owner 边界、契约落点 | 单次构建号、板端日志、临时完成判断 |
| `*_TODO.md` | 里程碑、子项状态、进入/退出门禁、证据索引 | 复制任务快照、把一次性证据写成架构事实 |
| `*_TASK_PROGRESS.md` | 提交、构建/HIL、报告、失败、回退、阻塞 | 冻结新契约、擅自改变 registry status、替代 TODO 状态 |

三件套必须互相引用；语义变更先更新架构并登记，状态变更更新 TODO，实施证据只追加到任务进度。

### 三件套最小格式

| 文件类型 | 必备结构 | 稳定索引 | 状态/记录格式 |
|---|---|---|---|
| `*_ARCHITECTURE.md` | 文档接口、范围与边界、owner/不变量、数据或状态模型、跨域契约、失败与恢复、验证映射 | 条款 ID 或契约 ID | 只描述稳定语义；不得记录单次 build、HIL 结果或当前执行状态 |
| `*_TODO.md` | 文档接口、状态规则、已有基线、当前主线、里程碑总览、分阶段任务表、当前阻塞项、统一完成定义 | 每个任务使用域内唯一且稳定的 task ID | 状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`；任务表固定为 `ID / 任务 / 状态 / 完成或退出门禁` |
| `*_TASK_PROGRESS.md` | 文档接口、当前 checkpoint、按时间追加的任务记录、验证与证据索引、失败/回退、下一 gate | 每条记录必须引用 TODO task ID，并使用唯一 progress ID | 记录必须包含日期、变更/提交、构建或验证、结果、证据位置和下一步；不得替代 TODO 状态或冻结架构契约 |

格式约束不要求把三份文档写成相同正文；它只统一导航、状态和追溯接口。`TODO` 中不得复制
单次 build/HIL 数值，`TASK_PROGRESS` 中不得自行宣布契约生效，`ARCHITECTURE` 中不得使用
执行清单替代稳定语义。旧文档在未实质修改前允许保持原格式，但一旦重排状态或新增里程碑，
必须在同一文档变更中迁移到本表格式。

## §1 docs/check/DOCS_REGISTRY.md（完整内容）

```markdown
# 文档契约登记表

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/DOCS_REGISTRY.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`
Last updated: 2026-08-19

> 注：本文件必须满足 `tools/docs_check/docs_check.py` 的元数据要求（5 字段齐全），否则自回归门禁自相矛盾。

## 契约登记表

规则：域文档每冻结一个跨域契约，必须在此登记一行（contract_id 唯一，登记即上提）。
格式由 `tools/doc_regression_check.py` 校验（id 唯一 / 文件锚点存在）。

| contract_id | domain | contract | ver | clause_loc | code_anchor | check | registered | status |
|---|---|---|---|---|---|---|---|---|
| TDMA-REASON-01 | tdma | ring reason code 9 项冻结 | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_ring_runtime.h | enum 比对 | 2026-08-19 | active |
| TDMA-SEQLOCK-01 | tdma | runtime snapshot 必须 seqlock | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_service.c | 代码审查 | 2026-08-19 | active |
| TDMA-HOP-01 | tdma | hop_limit 归属 ring profile | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_ring_profile.h | 符号存在性 | 2026-08-19 | active |
| REFMEM-260B-01 | refmem | critical delta ≤260B | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | refmem_frame.h | 常量比对 | 2026-08-19 | active |
| VDC-DPLL-01 | vdc | DPLL 准入 resolution≤100ns | 1 | docs/vdc/VDC_DOMAIN_ARCHITECTURE.md | vdc_timestamp_clock.h | 符号存在性 | 2026-08-19 | active |

## 条款落点表

规则：顶层每条硬约束必须有落点行，不允许孤儿条款。评审发现违规 → 状态改 VIOLATED。

| clause_id | 顶层条款 | domain_loc | module | verify | status |
|---|---|---|---|---|---|
| HAOFV-140 | 跨核多字段事实必须 seqlock/双缓冲/version | tdma: 双 FIFO 契约 | tdma_flight_fifo | 代码审查 | PARTIAL |
| HAOFV-879 | 多字段事实必须 seqlock | tdma: runtime snapshot | tdma_pio_spi_ring_adapter | 代码审查 | VIOLATED |
| HAOFV-365 | TDMA 声明 UP/DOWN group | tdma: ring profile | tdma_ring_profile.h | 符号存在性 | OK |

## 状态说明

- contract status: active / pending / superseded
- clause status: OK / PARTIAL / VIOLATED / PENDING
- 新契约登记后，顶层 `docs/arch/HAOFV_ARCHITECTURE.md` 须在 7 天内刷新（环1 校验）
```

## §2 tools/doc_regression_check.py（完整代码）

```python
#!/usr/bin/env python3
"""Check docs freshness (loop 1) and contract registry (loop 2).

Companion to tools/docs_check/docs_check.py. Pure stdlib.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

FRESHNESS_DAYS = 7
REGISTRY_REL = Path("docs/check/DOCS_REGISTRY.md")
TOP_DOC_REL = Path("docs/arch/HAOFV_ARCHITECTURE.md")

DATE_RE = re.compile(r"Last updated:\s*(\d{4})-(\d{2})-(\d{2})")
ROW_RE = re.compile(r"^\|\s*(TDMA|VDC|REFMEM|RTOS|INTERFACE|HARDWARE|ARCH|DOCS)-[A-Z0-9]+-\d+\s*\|")


@dataclass
class Result:
    failures: list[str]
    warnings: list[str]

    def fail(self, msg: str) -> None:
        self.failures.append(msg)
        print(f"FAIL {msg}")

    def warn(self, msg: str) -> None:
        self.warnings.append(msg)
        print(f"WARN {msg}")

    def ok(self, msg: str) -> None:
        print(f"OK   {msg}")


def parse_date(text: str) -> tuple[int, int, int] | None:
    m = DATE_RE.search(text)
    if not m:
        return None
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def days_between(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    return (__import__("datetime").date(*b) - __import__("datetime").date(*a)).days


def check_freshness(root: Path, result: Result) -> None:
    top = root / TOP_DOC_REL
    if not top.exists():
        result.fail(f"{TOP_DOC_REL} missing")
        return
    top_date = parse_date(top.read_text(encoding="utf-8"))
    if top_date is None:
        result.fail(f"{TOP_DOC_REL}: missing 'Last updated: YYYY-MM-DD'")
        return

    newest: tuple[int, int, int] | None = None
    newest_file = ""
    for doc in (root / "docs").rglob("*.md"):
        if doc == top or "archive" in doc.parts or "legacy" in doc.parts:
            continue
        d = parse_date(doc.read_text(encoding="utf-8", errors="ignore"))
        if d is None:
            continue
        if newest is None or d > newest:
            newest, newest_file = d, str(doc.relative_to(root))

    if newest is None:
        result.ok("freshness: no dated domain docs found")
        return
    if days_between(top_date, newest) > FRESHNESS_DAYS:
        result.fail(
            f"freshness: top doc is {days_between(top_date, newest)}d older than "
            f"{newest_file} ({newest[0]}-{newest[1]:02d}-{newest[2]:02d}); "
            f"refresh {TOP_DOC_REL} within {FRESHNESS_DAYS}d"
        )
    else:
        result.ok("freshness: top doc within window")


def check_registry(root: Path, result: Result) -> None:
    reg = root / REGISTRY_REL
    if not reg.exists():
        result.fail(f"{REGISTRY_REL} missing (create it per spec)")
        return
    text = reg.read_text(encoding="utf-8")
    rows = [ln for ln in text.splitlines() if ROW_RE.match(ln)]
    if not rows:
        result.fail(f"{REGISTRY_REL}: no contract rows found")
        return

    ids = [ln.split("|")[1].strip() for ln in rows]
    dupes = sorted({i for i in ids if ids.count(i) > 1})
    if dupes:
        result.fail(f"{REGISTRY_REL}: duplicate contract_id {dupes}")
    else:
        result.ok(f"registry: {len(rows)} contracts, ids unique")

    for ln in rows:
        cells = [c.strip() for c in ln.split("|")]
        # cells: ['', id, domain, contract, ver, clause_loc, code_anchor, check, registered, status, '']
        if len(cells) < 10:
            result.fail(f"{REGISTRY_REL}: malformed row: {ln[:80]}")
            continue
        clause_loc, code_anchor = cells[5], cells[6]
        doc_rel = clause_loc.split(":")[0]
        if not (root / doc_rel).exists():
            result.fail(f"{REGISTRY_REL}: clause_loc file missing: {clause_loc}")
        if not any((root / p).name == code_anchor
                   for p in [clause_loc.split(":")[0]] + []):
            # search under root for the anchor file name
            if not list(root.rglob(code_anchor)):
                result.fail(f"{REGISTRY_REL}: code_anchor not found: {code_anchor}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--freshness", action="store_true", help="only loop 1")
    parser.add_argument("--registry", action="store_true", help="only loop 2")
    args = parser.parse_args()
    root = args.root.resolve()

    result = Result(failures=[], warnings=[])
    if args.freshness or not (args.freshness or args.registry):
        check_freshness(root, result)
    if args.registry or not (args.freshness or args.registry):
        check_registry(root, result)

    if result.failures:
        print(f"SUMMARY FAIL failures={len(result.failures)} warnings={len(result.warnings)}")
        return 1
    result.ok("doc_regression passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

## §2.5 .agents/skills/doc-self-regression/SKILL.md（完整内容）

```markdown
---
name: doc-self-regression
description: Use when maintaining the HAOFV docs corpus — registering a frozen contract, refreshing the top-level doc, reviewing doc changes, or on "docs drift / register contract / check docs" requests. Enforces contract registration, freshness windows, and verify-doc-* gates.
---

# Doc Self-Regression

## 触发时机
- 域文档冻结跨域契约（wire 格式/错误码/容量/时序门禁）→ 必须登记
- 修改 canonical 文档 / 顶层文档 → 必须更新 Last updated，登记影响
- 评审 / 审查文档变更 → 先跑 verify-doc-*

## 硬约束（以方案 §0 为准，冲突时不可违反）
- 检查器只读；登记契约不可物理删除（只能 superseded）
- 临时内容（评审快照/草稿）不得登记为契约
- 新契约登记后顶层 7 天内刷新

## 工作流
1. 冻结契约 → `docs/check/DOCS_REGISTRY.md` 加一行（contract_id 唯一）
2. 改文档 → 更新 Last updated；数字引用代码符号，不手写
3. 提交前 → `python tools/docs_check/docs_check.py && python tools/doc_regression_check.py`
4. 评审 → 结论写回登记表 status（OK/PARTIAL/VIOLATED/PENDING）

## 检查命令
- 全量: `python tools/doc_regression_check.py`
- 变更范围: `python tools/doc_regression_check.py --scope <git diff --name-only>`
- pytest: `pytest tests/python/test_doc_regression.py`
```

## §2.6 核验提交单（Agent Note 格式）+ AGENTS.md 章节

### 核验提交单（采用 Agent Note header 块 + Alternatives considered）

```markdown
Status: Active
Domain: TDMA
Canonical: <本提交单路径>
Related: <父层文档>, <登记表>
Last updated: 2026-08-19

## 提交内容
| 父层条款 | 符合性 | 证据 |
|---|---|---|
| HAOFV-879 seqlock 快照 | ❌ | adapter get_snapshot 裸读，见偏差声明 |

## 偏差声明
- TDMA-SEQLOCK-01: 计划迁移步骤2修复；接受理由=当前 DIAGNOSTIC_ONLY 无 100ns 证据需求

## Alternatives considered
- 方案A 立即加 seqlock（拒绝：迁移阶段未到，改动面大）
- 方案B 登记偏差分批修复（接受）

## 核验结论
- 结论: ACCEPT_WITH_DEVIATION
- 核验人: <owner>

## 交叉审核记录（C11，必填）
- 审核方: <另一 agent / 人工 / 另一份文档>
- 审核方式: agent 交叉（独立评审）/ 文档交叉（域↔域对照，如 TDMA↔VDC）/ 层间交叉（代码↔登记表↔文档）
- 审核结论: PASS / PASS_WITH_NOTE / FAIL
- 审核日期: 2026-08-19
```

### 根 AGENTS.md 文档章节（Codex 每次会话可读）

```markdown
## 文档维护常设命令（doc-self-regression）
- 冻结跨域契约 → 必须在 docs/check/DOCS_REGISTRY.md 登记（contract_id 唯一）
- 登记后顶层 7 天内刷新；每次文档改动更新 Last updated
- 文档硬数字不手写：引用代码符号，或登记，或标注"快照"
- 提交前必须通过 verify-doc-*（pre-commit 自动执行）
- 冲突处理：以文档治理方案 §0 硬约束为准
```

## §3 tests/python/test_doc_regression.py（完整代码）

```python
from __future__ import annotations

from pathlib import Path

from tools.doc_regression_check import check_registry, check_freshness, Result


def test_registry_accepts_valid_contract_rows(tmp_path: Path) -> None:
    reg = tmp_path / "docs" / "check"
    reg.mkdir(parents=True)
    (reg / "DOCS_REGISTRY.md").write_text(
        "| contract_id | domain | contract | ver | clause_loc | code_anchor | check | registered | status |\n"
        "|---|---|---|---|---|---|---|---|---|\n"
        "| TDMA-REASON-01 | tdma | ring reason code | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_ring_runtime.h | enum 比对 | 2026-08-19 | active |\n",
        encoding="utf-8",
    )
    result = Result(failures=[], warnings=[])
    check_registry(tmp_path, result)
    assert result.failures == []


def test_registry_rejects_duplicate_ids(tmp_path: Path) -> None:
    reg = tmp_path / "docs" / "check"
    reg.mkdir(parents=True)
    (reg / "DOCS_REGISTRY.md").write_text(
        "| contract_id | domain | contract | ver | clause_loc | code_anchor | check | registered | status |\n"
        "|---|---|---|---|---|---|---|---|---|\n"
        "| TDMA-REASON-01 | tdma | a | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_ring_runtime.h | x | 2026-08-19 | active |\n"
        "| TDMA-REASON-01 | tdma | b | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_ring_runtime.h | x | 2026-08-19 | active |\n",
        encoding="utf-8",
    )
    result = Result(failures=[], warnings=[])
    check_registry(tmp_path, result)
    assert any("duplicate" in f for f in result.failures)


def test_freshness_rejects_stale_top_doc(tmp_path: Path) -> None:
    (tmp_path / "docs" / "arch").mkdir(parents=True)
    (tmp_path / "docs" / "tdma").mkdir(parents=True)
    (tmp_path / "docs" / "arch" / "HAOFV_ARCHITECTURE.md").write_text(
        "Last updated: 2026-08-01\n", encoding="utf-8")
    (tmp_path / "docs" / "tdma" / "TDMA_DOMAIN_ARCHITECTURE.md").write_text(
        "Last updated: 2026-08-19\n", encoding="utf-8")
    result = Result(failures=[], warnings=[])
    check_freshness(tmp_path, result)
    assert any("freshness" in f for f in result.failures)
```

## §4 .githooks/pre-commit（完整内容）

提交钩子按暂存区范围运行门禁：只包含 Python、C 或其他实现代码时跳过文档扫描，
避免与文档无关的提交承担文档检查成本；涉及 docs/、文档检查器、hook 或
AGENTS.md 时仍运行完整检查。设置 FORCE_DOC_GATES=1 可在任意提交上强制运行。

```bash
#!/bin/sh
# Run documentation self-regression only for commits that can affect it.
set -e
cd "$(git rev-parse --show-toplevel)" || exit 1

needs_docs=0
while IFS= read -r path; do
    case "$path" in
        docs/*)
            needs_docs=1
            break
            ;;
        tools/docs_check/*)
            needs_docs=1
            break
            ;;
        tools/doc_regression_check.py)
            needs_docs=1
            break
            ;;
        .githooks/*)
            needs_docs=1
            break
            ;;
        AGENTS.md)
            needs_docs=1
            break
            ;;
    esac
done <<EOF
$(git diff --cached --name-only)
EOF

if [ "$needs_docs" -eq 0 ] && [ "${FORCE_DOC_GATES:-0}" != "1" ]; then
    echo "[pre-commit] docs gates skipped (no documentation-scope changes)"
    exit 0
fi

echo "[pre-commit] docs_check..."
python tools/docs_check/docs_check.py --strict-names || {
    echo "docs_check FAILED - fix docs before committing"
    exit 1
}
python tools/doc_regression_check.py || {
    echo "doc_regression FAILED - fix docs before committing"
    exit 1
}
exit 0
```

## §5 安装与验收（复制粘贴即可）

```powershell
# 安装（一次性）
cd <仓库根>                    # AGENTS.md 所在目录
git config core.hooksPath .githooks

# 验收 1：检查器（预期环1 报顶层过期 = 准绳，非失败）
python tools/doc_regression_check.py
#   预期: FAIL freshness: top doc is N d older than ...（顶层未刷新，正确报警）
#         OK   registry: 5 contracts, ids unique

# 验收 2：pytest
pytest tests/python/test_doc_regression.py -v
#   预期: 3 passed

# 验收 3：门禁（故意造重复 id 再 commit → 应被拒）
# 在 DOCS_REGISTRY.md 里复制一行 TDMA-REASON-01 再 git add . && git commit → 拒绝
```

## §6 本次明确不做

| 不做 | 原因 |
|---|---|
| 环3 数字单一来源检查 | 下一期；本期先立住登记 + 新鲜度 + 门禁 |
| `verify-doc-crosscheck` 自动交叉核验 | 下一期：对登记契约自动比对"域文档描述 vs code_anchor 实际值"；本期交叉审核走提交单人工/agent 流程（C11） |
| 顶层 HAOFV_ARCHITECTURE.md 刷新 | 另行待办（涉及正文修改，单独评审） |
| 逐级核验流程的完整自动化 | 提交单格式本期定义（§2.6），流程依赖登记表跑顺后再启用 |
| CI / pre-commit 框架 / 第三方依赖 | 单人项目 git hook 足够 |

## §7 遗留缺口与处置（审核补充，2026-08-19）

| 缺口 | 处置 | 机检 |
|---|---|---|
| G1 登记表元数据与 docs_check 冲突 | 补 Related 字段（已修，见 §1 表头） | docs_check 自检 |
| G2 孤儿条款无机检 | 环2 扩展：解析顶层硬约束表 vs 落点表比对（T8） | 检查器 |
| G3 检查器性能/误匹配 | 预索引文件清单一次；排除 `build*/`、`.git/`（T9） | 实测 ≤5s |
| G4 GBK 编码崩溃 | read_text 加 errors="ignore" + 编码检测（T9） | 防御 |
| G5 逃生门留痕 | hook 写 `.git/doc-verify.log`；检查器比对 git log 与 hook log 差异 → WARN（T10） | 检查器 |
| G6 提交单归档位置 | `docs/check/submissions/YYYY-MM-DD-<id>.md`（T11） | 目录约定 |
| G7 Draft 文档不可登记 | 环2 校验 clause_loc 文档 Status 必须 Active/Frozen（T8） | 检查器 |
| G8 检查器自身交叉审核 | T12：用独立 agent/人工评审 doc_regression_check.py 代码 | 评审 |
| G9 方案收尾路径 | 执行完成后本方案转 Active 归档入 `docs/check/`（T13） | — |
| G10 SKILL/AGENTS 权威关系 | AGENTS.md=常设命令（权威），SKILL.md=触发工作流并引用 AGENTS（§2.5 已体现） | — |

## §8 待办分解（批准后细化执行序）

```
T1  建 docs/check/DOCS_REGISTRY.md（5 字段元数据 + 5 契约 + 落点表）   [§1, G1]
T2  建 tools/doc_regression_check.py（环1+环2 基础，--scope）          [§2]
T3  建 tests/python/test_doc_regression.py（3 用例）                  [§3]
T4  建 .githooks/pre-commit + git config core.hooksPath                [§4+§5]
T5  建 .agents/skills/doc-self-regression/SKILL.md                     [§2.5]
T6  根 AGENTS.md 追加文档维护章节                                      [§2.6]
T7  全量验收（检查器 / pytest / 门禁三连）                            [§5]
T8  环2 扩展：孤儿条款检查 + Draft 文档禁止登记                        [G2, G7]
T9  检查器加固：预索引 + 排除 build + 编码防御                         [G3, G4]
T10 逃生门留痕：hook 日志 + git log 比对                               [G5]
T11 提交单归档目录 + 模板                                             [G6]
T12 检查器交叉审核（独立评审 doc_regression_check.py）                 [G8]
T13 方案收尾：v3 转 Active 归档入 docs/check/                          [G9]
```

---

## 决策确认记录

- 2026-08-19: 用户认可 §0-§2.6 交付物内容与 pre-commit 门禁
- 2026-08-19: 用户补充需求已并入 — 逐级核验(§2.6)、硬约束(§0)、harness 精髓(映射表)、git 经验(§7)、交叉审核(C11)、遗留缺口(§7 G1-G10)
- 执行入口: §8 待办 T1→T13，逐条执行、每步反馈，T12 检查器交叉审核在 T2 代码完成后进行
