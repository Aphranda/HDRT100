# HAOFV 顶层架构文档刷新任务单（给 Codex）

Status: Active
Domain: Documentation Governance
Canonical: `docs/temp/HAOFV_REFRESH_PLAN.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/check/DOCS_REGRESSION_TODO.md`
Last updated: 2026-08-19

> 本任务单由文档自回归体系（`docs/check/DOCS_REGRESSION_PLAN.md`）生成。
> 修改对象：`docs/arch/HAOFV_ARCHITECTURE.md`（顶层文档）。
> 背景：5 条跨域契约于 2026-08-19 登记（C8 硬约束），顶层须在 2026-08-26 前刷新；
> 同时修正评审发现的文档漂移（目录结构、手写数字、错误码空间缺口）。

## 零、前置阅读（执行前必须完成，不理解体系禁止动手）

Codex 在修改前必须先阅读并理解：

1. `AGENTS.md`（仓库根）—— 自回归文档体系常设命令（§0-§5）
2. `docs/check/DOCS_REGRESSION_PLAN.md` —— 体系规格（冻结契约、硬约束、门禁）
3. `docs/check/DOCS_REGISTRY.md` —— 契约登记表 + 条款落点表（本次修改的事实源）
4. `docs/check/DOCS_REGRESSION_REVIEW.md` —— 实施经验（常见坑，避免重蹈）

**理解验证**：能回答以下问题再动手——
- 为什么 5 字段元数据必须在标题后 7 行内？
- 为什么文档不能手写硬数字？
- 本次修改的 5 条契约在登记表中的 contract_id 是什么？
- 修改完成后跑哪些验证命令？FAIL 了怎么办？

**定向阅读清单**（动手前逐项完成，不要求读全部 docs）：
1. 任务 C：`docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md:583-599` —— 9 个 ring reason code 必须**逐字复制**，禁止凭记忆重写
2. 任务 D：`docs/vdc/VDC_DOMAIN_ARCHITECTURE.md:293` 附近 —— DPLL 准入语义
3. 任务 E：运行 `Get-ChildItem components -Directory | Select Name` 列出**实际**组件目录，禁止猜测

**任务理解摘要（动手前的强制输出）**：开始修改前，先输出以下摘要供用户确认，未经确认不得编辑文件：
```text
1. 5 条契约 contract_id 与来源：<列出>
2. 任务 C 的 9 个 reason code：<逐字列出>
3. 计划修改的章节清单：<A-E 逐条对应的章节/位置>
4. 本次不动的部分：F1/F2 决策项、其他文档
```

## 一、规则（修改时必须遵守）

1. **元数据**：`Last updated` 必须更新为修改日；5 字段（Status/Domain/Canonical/Related/Last updated）保持在标题后 7 行内
2. **数字单一来源**：不手写硬数字；引用代码符号或登记表契约；快照数字须标注"快照，非事实源"
3. **登记表是契约事实源**：本次修改只让顶层"可见"已登记的 5 条契约（引用域文档 canonical 位置），**不改变契约内容本身**
4. **增量修改**：不重构文档结构、不移动章节、不改其他文档（除非任务单明确列出）
5. **修改后验证**：跑 §三 的全部验证命令，必须全绿

## 二、执行流程（直接开始 + 节点自动核验）

> 执行方式：不要求动手前全量回顾，不需要用户逐点确认；按阶段推进，
> 每个节点自动核验（跑验证 + 输出变更摘要作为审计记录），核验通过自动继续。
> 最终交叉审核由独立方（另一 AI/人工，非用户逐点）执行。

### 节点 0（动手前，自动核验）
- 输出任务理解摘要（写入执行日志），随后直接开始
- 定向阅读：任务 C 的 `TDMA_DOMAIN_ARCHITECTURE.md:583-599`、任务 D 的 `VDC_DOMAIN_ARCHITECTURE.md:293`、任务 E 现场 `ls components/`

### 阶段 1：任务 A + B（头部版本化 + 190 规则修正）
- 执行 A、B；跑 `docs_check --strict-names` 确认无新 FAIL
- **→ 节点 1 自动核验**：验证通过则继续，不通过则修复后继续（摘要入日志）

### 阶段 2：任务 C（错误码表补 TDMA 段）
- 执行 C；9 个 reason code **逐字复制**自 `TDMA_DOMAIN_ARCHITECTURE.md:583-599`，与登记表 TDMA-REASON-01 比对
- **→ 节点 2 自动核验**：同上（9 个 code 清单入日志）

### 阶段 3：任务 D（5 条契约可见性）
- 执行 D；contract_id 与 `DOCS_REGISTRY.md` 逐一比对，canonical 路径/行号准确
- **→ 节点 3 自动核验**：同上（5 条契约表入日志）

### 阶段 4：任务 E（目录结构对齐）
- 执行 E；组件名以现场 `ls components/` 为准
- **→ 节点 4 自动核验**：同上（目录结构 diff 入日志）

### 收尾（全部节点通过后）
- 跑 §三 全部验证（4 条命令全绿）
- 完成 §五 验收检查表 + §六 交叉审核记录
- **最终通读（用户执行）**：用户通读全部变更，对照 §五 验收检查表逐项核验 + 核对节点 0-4 的审计摘要与最终文件一致
- 通读通过 → 按 §四 提交约定 commit

### 任务理解摘要（节点 0 输出，供确认）
```text
1. 5 条契约 contract_id 与来源：<列出>
2. 任务 C 的 9 个 reason code：<逐字列出>
3. 计划修改的章节清单：<A-E 逐条对应的章节/位置>
4. 本次不动的部分：F1/F2 决策项、其他文档
```

### 待办明细（A-F，供各阶段引用）

### A. 文档头部版本化
- 文件: `docs/arch/HAOFV_ARCHITECTURE.md` 头部（第 1-7 行）
- 动作：在 `Last updated` 下方新增一行 `Version: 2`（当前无版本号，视为 v1）；更新 `Last updated: 2026-08-19`
- 依据：HAOFV 评审 D3（canonical 文档无语义版本，漂移不可检测）

### B. 修正"190 规则"手写数字
- 位置: `docs/arch/HAOFV_ARCHITECTURE.md:602-604`
- 动作：
  - 保留规则句"TriggerFB 规则数量必须由代码 `sizeof` 导出，不允许文档手写固定数字作为事实源"
  - 删除或改写"2026-08-13 的 TriggerFB 基线已经从早期的 58 条规则扩展到约 190 条规则"为：
    "规则数量以 `sizeof(s_ecc_table) / sizeof(s_ecc_table[0])` 为准；以下历史数字仅为快照，非事实源。"
- 依据：文档自己的规则（`:602`）被自己违反；HAOFV-RISK-TODO-20260813-003

### C. 错误码空间表补 TDMA 域段
- 位置: `docs/arch/HAOFV_ARCHITECTURE.md:819-827`（错误码空间划分表）
- 动作：表尾追加一行：
  `| TDMA | 600-699 | ring reason code 9 项（NONE/BAD_CONFIG/EVIDENCE_MISSING/DIRECTION_CONFLICT/ADAPTER_MISSING/TIMESTAMP_MISSING/PAYLOAD_STARVATION/WINDOW_MISSED/RESOURCE_CONFLICT），见 TDMA_DOMAIN_ARCHITECTURE.md:583-599 |`
- 依据：契约 TDMA-REASON-01（登记表）；错误码空间表当前缺 TDMA 段

### D. 顶层登记 5 条契约可见性
- 位置: 建议在 `docs/arch/HAOFV_ARCHITECTURE.md` 的"顶层安全硬约束"表（:136-147）之后，新增小节"### 已冻结域契约登记（2026-08-19）"
- 内容（5 条，均引用域文档 canonical，不改内容）：
  | contract_id | 契约 | 域文档位置 | 顶层相关性 |
  |---|---|---|---|
  | TDMA-REASON-01 | ring reason code 9 项冻结 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md:583-599` | 错误码空间 TDMA 段（见任务 C） |
  | TDMA-SEQLOCK-01 | runtime snapshot 必须 seqlock | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md:575,606` | 跨核共享事实（HAOFV-139）实现要求 |
  | TDMA-HOP-01 | hop_limit 归属 ring profile | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md:196,477` | 分布式确定性通讯（HAOFV-142） |
  | REFMEM-260B-01 | critical delta ≤260B | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md:194` | RefMem 短帧容量约束 |
  | VDC-DPLL-01 | DPLL 准入 resolution≤100ns | `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md:293` | 分布式共同时间（HAOFV-141）证据门禁 |
- 依据：登记表 `docs/check/DOCS_REGISTRY.md`（上提环的顶层可见化）

### E. 目录结构章节对齐
- 位置: `docs/arch/HAOFV_ARCHITECTURE.md:503-548`（推荐目录结构）
- 动作：将 `sync_trigger/`、`ota_manager/`、`storage_manager/` 改为实际组件名，并补充 `components/tdma/`、`components/refmem/`、`components/vdc_dpll_manager/` 等基础件；列表后注明"目录以仓库实际结构为准"
- 依据：HAOFV 评审 D2（目录结构章节与实现漂移）

### F. 需用户决策项（**不执行**，仅列明）
- F1 时间回绕落地：架构 `:847` 说"epoch_seconds、run_id、高位 epoch 或等价字段"三选一——需要用户定，Codex 不改
- F2 SD factory 恢复宿主：`:1178` 允许从 SD `/factory/` 恢复 vs `:1351` Bootloader 不集成 FatFs——需要用户定宿主，Codex 不改

## 三、修改后验证（必须全绿）

```powershell
# 1. 文档检查（元数据/索引/链接/命名）
python tools/docs_check/docs_check.py --strict-names
#   期望: OK, files=97（若新增文件则 +1，且须在 docs/README.md 加索引行）

# 2. 自回归检查（freshness 应归零——顶层 Last updated 已更新）
python tools/doc_regression_check.py
#   期望: OK freshness / OK registry / OK orphan / OK skill-sync

# 3. pytest
python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider
#   期望: 全部通过

# 4. pre-commit 门禁
sh .githooks/pre-commit
#   期望: doc gates OK
```

## 四、提交约定

- **合并提交（用户决定，2026-08-19）**：本次 HAOFV 刷新与文档自回归体系文件（docs/check/*、检查器、门禁、skill 等）**合并为一个 commit** 推送，不拆分
- 建议 commit message：
  `docs: 建立文档自回归体系 + HAOFV 顶层刷新（契约登记可见/错误码 TDMA 段/数字修正）`
- 合并提交由最终通读通过后执行（明早用户通读）

## 五、验收检查表（Codex 完成后，独立方逐项核验）

| # | 检查项 | 通过标准 | 核验结果 |
|---|---|---|---|
| A1 | 版本化 | 头部含 `Version: 2`，`Last updated` 为修改日 | ☐ |
| B1 | 190 规则修正 | 手写"约 190 条"已改为快照标注或删除；`sizeof` 规则句保留 | ☐ |
| C1 | 错误码 TDMA 段 | 错误码表含 `TDMA 600-699` 行，列出 9 个 reason code | ☐ |
| C2 | 与登记表一致 | TDMA-REASON-01 的 9 个 code 与域文档 `:583-599` 完全一致 | ☐ |
| D1 | 契约登记小节 | 含 5 条契约表，contract_id 与 `DOCS_REGISTRY.md` 一致 | ☐ |
| D2 | canonical 引用 | 5 条引用的域文档路径/行号准确 | ☐ |
| E1 | 目录结构对齐 | 含 `components/tdma/`、`distributed_refmem/`、`vdc_dpll_manager/` 等实际组件 | ☐ |
| F1 | 决策项未动 | F1/F2 未被执行（保持原样） | ☐ |
| V1 | 检查器全绿 | §三 的 4 条命令全部通过（freshness 不再报警） | ☐ |

## 六、交叉审核记录（C11，必填）

```markdown
## 交叉审核记录
- 审核方: <另一 agent / 人工 / 另一份文档>
- 审核方式: agent 交叉（独立评审）/ 文档交叉（与 TDMA/VDC 域文档对照）/ 层间交叉（代码↔登记表↔文档）
- 逐项核验: 见 §五 表格（A1-F1, V1）
- 审核结论: ACCEPTED / ACCEPT_WITH_DEVIATION / REJECTED
- 审核意见: <逐条 PASS/NOTE/FAIL 说明>
- 审核日期: <YYYY-MM-DD>
```

**放行条件**：§五 全部 ☑ 且 §三 验证全绿，交叉审核结论为 ACCEPTED 或 ACCEPT_WITH_DEVIATION（偏差需在意见中说明）后，才允许 commit。
