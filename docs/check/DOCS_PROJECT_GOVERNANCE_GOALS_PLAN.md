# 项目治理长期目标与里程碑

Status: Active
Domain: Documentation Governance / Project Review
Canonical: `docs/check/DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`, `docs/standards/STANDARDS_DOMAIN_ARCHITECTURE.md`, `docs/standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`, `tools/tool_registry.json`, `tools/tool_workflow_triage.json`, `docs/console/CONSOLE_WORKFLOW_ARCHITECTURE.md`
Last updated: 2026-11-06

## 文档接口与范围

本文把 2026-11-06 的反向评估结论固化为**长期目标与里程碑**，作为治理收束的规划基线。  
它不是产品契约，不替代 `DOCS_REGISTRY.md`、域 TODO 或验收凭证；目标状态以本文为准，执行证据回到各域 `*_TASK_PROGRESS.md`。

本文只回答四件事：

1. 长期目标是什么；
2. 每个目标分几个里程碑、何时算完成；
3. 哪些里程碑需要台架；
4. 如何复审、止损和调整。

## 1. 评估基线

| 维度 | 2026-11-06 基线 | 目标方向 |
|---|---|---|
| 综合成熟度 | 2.8 / 4（L2+，局部 L3） | ≥3.2（L3） |
| 契约 | 13 active / 45 pending / 2 superseded | active ≥35%，高风险 pending 清零 |
| 工具 | 112 族；95 `UNASSESSED`；103 依赖复核 `PENDING` | 关键工具 80% 完成评估/依赖复核 |
| 工作流 | 8 条 triaged；3 条迁移 pending；2 条缺 resume | 5 条核心流程符合 WorkflowSpec |
| 进度日志 | 6 份 >180 KB，2 份 195.3 KB | 最大日志 <150 KB |
| 证据 | host 77 passed；P3 `QUICK_DIAGNOSTIC` | 发布候选必须 FULL P3 |

基线只作规划起点，不作为冻结事实；每次里程碑复审重新采样。

## 2. 目标模型

四个长期目标，按依赖排序：

```text
G1 标准产品化（无台架）
        │
        ├── G3 外部采用（无台架）
        │
G2 项目治理收束（部分台架）
        │
        └── G4 发布准备（必须台架）
```

- **G1** 让通用规范成为可交付、可版本化、可反馈的标准产品。
- **G2** 把契约、工具、工作流和日志的治理债务收到可发布水平。
- **G3** 用外部项目验证标准的可裁剪性和可执行性。
- **G4** 在代码冻结后完成发布级验收，不把 QUICK 当 FULL。

## 3. G1：标准产品化

**目标**：把 `docs/standards/` 从“一篇标准 + 三件套”变成“外部团队可独立采用的标准产品”。

| 里程碑 | 内容 | 完成判据 | 台架 | 状态 |
|---|---|---|---|---|
| G1-M1 | 域三件套与主标准 v1.0 | 已建立；`docs_check` 通过 | 否 | DONE |
| G1-M2 | Adoption guide | 外部读者可按 90 天路径启动；含角色、产物、门禁、止损 | 否 | DONE |
| G1-M3 | Localization template | 领域映射、填空模板、示例可替换；结构工程示例完整 | 否 | DONE |
| G1-M4 | 版本/复审/归档 | 版本规则、季度复审、旧版归档、changelog 可用 | 否 | DONE |
| G1-M5 | 反馈与评估固化 | 外部反馈日志、反向评估记录、改进项回流 TODO | 否 | DONE |

**度量**：guide 完成度、模板填充时间（目标 <1 天）、外部试点数（目标 ≥1）、反馈闭环数（目标 ≥3）。

**当前状态**：G1-M1～M5 DONE；G1 完成。外部反馈条目由 G3 试点承接，M6 保持 IN PROGRESS，不冒充已验证。  
**关联**：`STANDARDS_DOMAIN_TODO.md` 的 `STANDARDS-M4`～`M6`。

## 4. G2：项目治理收束

**目标**：把“登记强、闭环弱”收到可发布水平；不追求所有契约都 active，只要求高风险项有证据、有 owner、有到期日。

### G2-M1 契约激活波次

计划见 [契约激活波次计划](DOCS_CONTRACT_ACTIVATION_WAVE_PLAN.md)，状态 `IN PROGRESS`。  
2026-11-06：Wave 0 的 `DOCS-PROGRESSROTATE-01`、`DOCS-PROGRESSORDER-01` 已两次独立 C11 复核，均 `PASS_WITH_NOTE`；因闭包/同向/边界缺口保持 pending，先修复再激活。

- 选 10 条最高风险 pending（优先 TDMA/VDC/RefMem）。
- 每条指定证据 owner、现有证据、缺口、台架需求、到期日。
- 完成判据：10 条全部转为 active 或有明确 `superseded/blocked` 结论；active 比例 ≥30%。

### G2-M2 工具生命周期

- 先做 Top-20 风险分级，不做 112 族全量。
- 所有 `*_workflow.py` 完成判型；设备类工具补 `operation` 与依赖复核。
- 完成判据：Top-20 完成 `lifecycle/operation/dependency_review`；关键工具 80% 达到 `SUPPORTED` 或明确 `DEPRECATED/ARCHIVED`。

### G2-M3 工作流统一

- 选 5 条核心流程：硬件验收、T2、OTA、REFMEM、TDMA。
- 每条补齐 state 文件、resume 校验、逐步 evidence、最终 receipt。
- 完成判据：5/5 符合 `WorkflowSpec`；`*_workflow.py` 判型无 error；resume 缺失清零。

### G2-M4 日志轮转

- 6 份 >180 KB 的 `*_TASK_PROGRESS.md` 在下次追加前轮转。
- 完成判据：最大日志 <150 KB；归档索引闭包；`doc_regression_check` 通过。

**度量**：active 契约比例、Top-20 工具评估率、WorkflowSpec 符合率、最大日志体积。

**当前状态**：PENDING。  
**台架**：G2-M1 部分需要（45 条 pending 中约 34 条涉及台架证据）；G2-M2 设备类工具需要；G2-M3 端到端需要；G2-M4 不需要。

## 5. G3：外部采用

**目标**：用结构专业同事的真实项目验证标准，而不是只做“文档交付”。

| 里程碑 | 内容 | 完成判据 | 台架 |
|---|---|---|---|
| G3-M1 | 结构专业试点 | 选一个真实项目，按 guide 跑一轮 | 否（外部环境） |
| G3-M2 | 本地化 | 把三件套、契约、工具、工作流映射到结构专业模板 | 否 |
| G3-M3 | 反馈闭环 | 收集 ≥3 个问题，至少 2 个回流标准/模板 | 否 |
| G3-M4 | 采用结论 | 给出“可复制/需修改/不适用”的结论和证据 | 否 |

**度量**：试点周期、模板填充时间、问题关闭率、第二次采用成本。

**当前状态**：PENDING。  
**依赖**：G1-M2/M3 完成。

## 6. G4：发布准备

**目标**：在代码冻结后完成发布级验收；不以 QUICK 凭证替代 FULL。

| 里程碑 | 内容 | 完成判据 | 台架 |
|---|---|---|---|
| G4-M1 | 发布范围冻结 | 发布契约清单、证据映射、残余风险清单 | 否 |
| G4-M2 | 代码冻结 + FULL P3 | 同源码指纹、四板 FULL、strict gates | **是** |
| G4-M3 | 发布门禁 | `check-staged` 通过；凭证/receipt 归档；发布 checklist 完成 | **是** |

**度量**：FULL P3 通过率、P0/P1 残余数、凭证覆盖率、发布回退次数。

**当前状态**：PENDING。  
**依赖**：G2 完成；代码冻结；台架窗口。

## 7. 台架需求总结

| 目标 | 台架需求 | 说明 |
|---|---|---|
| G1 标准产品化 | 无 | 纯文档/模板/评审 |
| G2 治理收束 | 部分 | 契约证据、设备工具、工作流端到端需要；日志/离线工具不需要 |
| G3 外部采用 | 无（本域） | 外部项目自己的环境另计 |
| G4 发布准备 | 必须 | FULL P3、四板验收、发布凭证 |

**原则**：先用现有证据复核，再排台架；按同构建/同拓扑合并窗口；不在治理文档阶段盲目上板。

## 8. 节奏与复审

| 频率 | 动作 | 产出 |
|---|---|---|
| 每周 30 分钟 | 看板：里程碑状态、阻塞、台架需求 | 周报/阻塞清单 |
| 每月 | 里程碑复审：完成/延期/改道 | 月度目标报告 |
| 每季度 | 重新评估成熟度、删减低价值目标 | 评估快照 + 下季目标 |

状态词汇：`PENDING` / `IN PROGRESS` / `DONE` / `BLOCKED`。  
`DONE` 必须有证据；延期必须写复验判据和闭环里程碑；`BLOCKED` 必须有外部条件和解除条件。

## 9. 止损与调整

出现以下情况，暂停扩张、先收缩：

- 门禁被普遍绕过，或 `--no-verify` 频率上升；
- 关键目标连续 2 个里程碑延期；
- 台架窗口被非关键目标占用；
- 外部采用反馈显示模板不可执行；
- 治理成本明显高于减少的返工。

调整规则：每次复审最多保留 4 个长期目标；新增目标必须说明替换哪个或为什么并行。

## 10. 当前下一步

1. **G2-M4**：日志轮转（不需要台架，且能立即降低 C14 风险的治理项）；
2. **G2-M1**：建立 10 条高风险 pending 契约的激活波次；
3. **G3-M1**：结构专业试点（为 M6 提供外部反馈）。

G1/G3 可以并行；G2 与 G4 串行；G4 在代码冻结后启动。
