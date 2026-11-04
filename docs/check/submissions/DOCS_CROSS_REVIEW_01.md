# 核验提交单：文档治理 → 仓库根（Agent 约束收敛）

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/submissions/DOCS_CROSS_REVIEW_01.md`
Related: `AGENTS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-11-04

## 提交内容

本轮为「agent 约束体系收敛」的 P0 + M1：修凭证分流空档、把契约 id 从「白名单两段」改为全量校验、清理失效引用、把里程碑模板从 `AGENTS.md` §8 迁到 `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` §7，并加对账测试防复发。

| 父层条款 | 符合性 | 证据 |
|---|---|---|
| C1 检查器只读 | ✅ | `tools/doc_regression_check.py` 仍只读；唯一写路径仍是显式 `--skill-sync`（同步到 `.agents/skills/doc-self-regression/`） |
| C4 契约不可物理删除 | ✅ | 登记表 60 行全部保留；`TDMA-FLIGHT-BITMAP-01` 仍为 `superseded`，未删行 |
| C7 Draft 不可登记为契约 | ⚠️ 限时债务 | 4 条 T2 行指向 `Status: Draft` 的 `TRIGGER_T2_TODO.md`；不做静默放行，改为 `DRAFT_CLAUSE_DEBT`（2026-11-18 到期即 FAIL，只减不增） |
| C8 顶层 7 天刷新 | ✅ | `doc_regression` freshness OK |
| C11 状态变更需交叉审核 | ✅ | 本条记录；审核方为独立只读子 agent（见下） |
| C14/C15 进展日志 | ✅ | 新条目 `TOOLS-PROGRESS-20261104-009` 倒排写入，日志 20 个全部通过 |
| EXE-CHANGE-01 证据闭环 | ✅ | host 凭证 `1249 files / 74 passed`；`sh .githooks/pre-commit` PASS（docs+host，无固件改动故 P3 skip） |

## 偏差声明

- `tools/**` 与 `.agents/**/*.py` 的凭证归属由 firmware 改为 host：依据是 2026-11-02 用户指令「hardware credential covers firmware only」，代码注释已记录；`source_scope.is_acceptance_source` 是 firmware 成员的唯一定义。
- `DRAFT_CLAUSE_DEBT` 为新增限时豁免，属 C11 管辖：已由独立子 agent 审核，条款未自批；到期未收口将 FAIL，不允许新增条目。
- `AGENTS.md` §8 正文迁出属「搬移」而非删除：EXE §7 逐行承接，删除 83 行 / 新增 91 行，非逐字部分仅为标题改号（8.0–8.3 → 7.1–7.4），语义等价。

## Alternatives considered

- 方案A 直接把 4 条 T2 行的 `clause_loc` 改指 `TRIGGER_T2_ARCHITECTURE.md`（拒绝：架构文档没有这几条条款的原文，改指即造假锚点，违反「锚点必须真实」）。
- 方案B 让 `tools/` 继续走 firmware（拒绝：与 2026-11-02 用户指令冲突，且工具改动会白白作废 P3 凭证）。
- 方案C 把 `.agents/**` 整体并入 host（拒绝：`SKILL.md` 属文档语料，并入会让纯文本改动强制跑 74 条 host 用例；只把 `.py` 实现并入）。
- 方案D 保留 §8 四份副本只改措辞（拒绝：正是本轮要消除的漂移源；改为单一正文 + 三处指针）。

## 核验结论

- 结论: `ACCEPT_WITH_DEVIATION`
- 核验人: 主会话（作者）

## 交叉审核记录（C11，必填）

- 审核方: 独立只读子 agent `463723db-139e-4305-8e19-df2ab1fead91`（未参与实现，全程只读）
- 审核方式: agent 交叉（独立取证 + 交叉差异）
- 审核结论: `PASS_WITH_NOTE` — 初审判 `APPROVE-WITH-FIXES`，4 项必修（索引与实际不一致、`.agents/**/*.py` 无门禁、host receipt 读写不一致、缺 C11 记录）已修；复审结论见 `docs/tools/DOCS_TOOLS_TASK_PROGRESS.md` 的 `TOOLS-PROGRESS-20261104-010`
- 审核日期: 2026-11-04

### 审核方独立发现（已修）

1. `.agents/**/*.py` 分类为空集 → pre-commit 不跑任何门禁（与 `AGENTS.md` §2「文档格式范围」承诺矛盾）。修复：`_host_member()` 覆盖 `.agents/**/*.py`，并同步进 host 指纹。
2. `host_acceptance.check_staged()` 读工作区 receipt、比对 staged 指纹，可被「staged 旧 receipt + 工作区新 receipt」绕过。修复：新增 `_receipt_matches_index()`，receipt 未入索引即 FAIL。
3. 新增债务豁免缺 C11 记录。修复：本条提交单。

### 审核方遗留（minor，随 M2/M3 批处理）

- `docs/check/DOCS_REGRESSION_PLAN.md` 仍写旧 `ROW_RE` 规格；`PRODUCT_VALUE_EVALUATION` 仍写「三环门禁」；PLAN 仍写环 3「下一期」。
- `tools/README.md` 等规范文档仍有已归档的 `p3_hardware_acceptance.py` 命令；`test_constraint_sync.py` 扫描范围尚未覆盖 `tools/*.md`。
- `registry_rows()` 对「缺前导 `|` 的错位行」仍会跳过（列数不足但 id 合法的行已 FAIL）。
- `DPLL_SHORT_UNCOVERED_PREFIXES` 中 `tools/ota_send/`、`tools/ota_multi_update/` 在当前分流下永不命中，需在 M3 重新定义或删除。
