# 契约激活波次计划

Status: Active
Domain: Documentation Governance / Contract Review
Canonical: `docs/check/DOCS_CONTRACT_ACTIVATION_WAVE_PLAN.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/check/DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md`, `docs/check/submissions/README.md`, `config/hardware_acceptance/p3_acceptance_receipt.json`, `docs/console/CONSOLE_WORKFLOW_ARCHITECTURE.md`
Last updated: 2026-11-06

## 文档接口与范围

本文把 45 条 `pending` 契约拆成可执行的激活波次，回答四个问题：

1. 哪些可以**不用台架**先关闭；
2. 哪些必须台架，台架证据缺什么；
3. 先做哪一批，后做哪一批；
4. 什么条件下才允许把 `pending` 改成 `active`。

本文是**计划**，不是契约登记表；状态仍以 `DOCS_REGISTRY.md` 为准，证据仍回到各域 Task Progress。

## 1. 基线

| 项 | 2026-11-06 快照 |
|---|---|
| 契约总数 | 60 |
| active / pending / superseded | 13 / 45 / 2 |
| pending 粗分 | 11 host-only；13 bench-only；21 host+bench |
| 顶层条款 | 10 条全部有落点；无孤儿 |
| 已知偏差 | 4 条 T2 契约仍指向 Draft `clause_loc`（C7/G7 债务至 2026-11-18） |
| 现有 P3 | `QUICK_DIAGNOSTIC`，`PASS_WITH_WARNINGS`，strict gates true，build `20261106082718` |

> “host-only / bench-only / mixed”是按 `check` 字段关键词粗分，激活前必须人工复核。

## 2. 选择标准

候选优先级：

1. 发布风险：是否阻断主链路或发布门禁；
2. 跨域耦合：是否涉及多域 owner/接口；
3. 证据可用性：是否已有同源码指纹、同 scope 的证据；
4. 台架成本：host-only 优先，台架项按窗口合并；
5. C11 状态：是否已有独立复核记录。

## 3. Wave 0：host-only 先行（不占台架）

目标：先关闭 11 条不依赖新台架证据的契约，降低 pending 基数，并验证激活流程。

| contract_id | 域 | 证据/动作 | C11 | 目标 |
|---|---|---|---|---|
| VDC-PATHMATRIX-01 | vdc | 跑 C/host matrix completeness / lookup 用例 | 需独立复核 | 2026-11-20 |
| TDMA-OPMODE-01 | tdma | SCPI/状态机/单测比对 | 需独立复核 | 2026-11-20 |
| ARCH-FLASHMAP-01 | arch | 生成表/链接/map/factory 工具比对 | 需独立复核 | 2026-11-20 |
| DOCS-FLASH-01 | docs | 文档边界/门禁/索引审查 | 文档复核 | 2026-11-20 |
| DOCS-TRIPLETFORMAT-01 | docs | 三件套结构审查 + 自动化检查 | 文档复核 | 2026-11-20 |
| TDMA-FLIGHTCLAIM-01 | tdma | B0 前置门禁证据审查 | 需独立复核 | 2026-11-20 |
| DOCS-PROGRESSROTATE-01 | docs | C14 阈值/截断/索引闭包正反用例 | 文档复核 | 2026-11-20 |
| DOCS-PROGRESSORDER-01 | docs | C15 日期级倒序/归档顺序正反用例 | 文档复核 | 2026-11-20 |
| ARCH-TRIGGERSLICE-01 | trigger | 符号/常量/尺寸静态断言；复用 `TRIGGER_CROSS_REVIEW_01` | 已有复核，确认是否仍有效 | 2026-11-27 |
| ARCH-TRIGGERMAILBOX-01 | trigger | 符号/常量核对；复用 `TRIGGER_CROSS_REVIEW_02` | 已有复核，确认是否仍有效 | 2026-11-27 |
| INTERFACE-T2ACTION-01 | trigger | 页尾字段/顺序 + `t2_workflow.py` 尾 N 项切片回归 + 宿主用例 | 需独立复核 | 2026-11-27 |

Wave 0 退出判据：11 条全部转为 `active`，或有明确 `superseded/blocked` 结论并登记原因。

**2026-11-06 复核结果**：`DOCS-PROGRESSROTATE-01`、`DOCS-PROGRESSORDER-01` 经两次独立只读 C11 复核，均为 `PASS_WITH_NOTE`；因 R4 反向闭包、C15 同向语义和边界测试未覆盖，**保持 pending，不计入 Wave 0 关闭**。缺口与后续任务见 `docs/check/submissions/DOCS_PROGRESS_CROSS_REVIEW_01.md`。

## 4. Wave 1：高风险 runtime 契约（台架）

目标：先处理发布风险最高、跨域耦合最强的 10 条；能复用既有证据的，不重复上板。

| contract_id | 域 | 已有证据 | 缺口 | 台架类型 |
|---|---|---|---|---|
| REFMEM-CTRL-01 | refmem | C11-01/03/04/05；N8.5 四板 `ack=14`；P3 QUICK strict=false | C3 租约、C6 fence；T2 在线 MOUNT 未覆盖 | 四板 HIL/P3 |
| REFMEM-TRANSPORT-01 | refmem | host 编解码；四板命令/ACK；P3 `PASS_WITH_WARNINGS` strict=false | seat rotation `f>=3` 条款与实现/预设不符 | 四板 |
| REFMEM-LAYOUT-01 | refmem | host/ABI/目录/节点边界 | Release map、旧包 owner 拒绝、四板 P3 | 四板 P3 |
| VDC-PUBLICATION-01 | vdc | host 发布回归、guard/回绕用例 | 同源码四板 P3、STOP 末态对照 | 四板 P3 |
| VDC-BOUNDARY-01 | vdc | host 多轮/至多一次/新模型窗口 | 实际 apply/ACK、频偏专项、四板 P3 | 四板 P3 |
| TDMA-FLIGHTBITMAP-01 | tdma | 容量矩阵、STOP/configure/ARM、单测 | 多板 HIL | 多板 HIL |
| TDMA-PROCESSIMAGE-01 | tdma | 编译断言、预算工具、帧型回归 | 多板 HIL | 多板 HIL |
| TDMA-RESIDENT-01 | tdma | 状态机/adapter/overlay | 短帧闭环、原始波形 HIL | 多板 HIL |
| ARCH-OTASTREAM-02 | ota | C11 有条件通过；2 板 N2；四板传输 | durable 递增 + 覆盖指纹 P3 | 四板 P3 |
| CALIBRATION-PHASE-01 | calibration | C/host/matrix；TRN-03 回归 | 四板 HIL | 四板 HIL |

Wave 1 退出判据：10 条全部转为 `active`，或有明确 `superseded/blocked` 结论；未关闭项保留 `pending`，不得写成完成。

## 5. Wave 2：剩余 24 条

- 先做证据盘点，不直接排台架；
- 按“同一构建/同一拓扑/同一板状态”合并窗口；
- 对长期不活跃的契约，评估 `superseded` 或降级；
- 不与 Wave 0/1 抢同一台架窗口。

## 6. 激活协议

每条契约必须按顺序完成：

1. 指定证据 owner；
2. 运行 host/离线/台架证据，绑定当前源码指纹；
3. 独立复核（C11）：作者之外的人/agent 只读取证；
4. 证据覆盖 `check` 字段的 scope；不覆盖的写清 `unverified_scope`；
5. 状态从 `pending` 改 `active`，更新登记表；
6. 如涉及顶层语义，按窗口刷新顶层文档；
7. 在对应域 Task Progress 记录证据与结论；
8. 更新索引/进度日志。

禁止：

- 没有证据就改 `active`；
- 作者自审自批；
- 用 QUICK P3 覆盖要求 FULL 的契约；
- 用旧 build/旧指纹的证据；
- 为了降低 pending 数量而批量改状态。

## 7. 台架窗口

| 窗口 | 覆盖 | 前置 | 目标 |
|---|---|---|---|
| W0 | Wave 0 host-only | 无 | 2026-11-20 前关闭 11 条 |
| W1 | RefMem + TDMA runtime | 同一 build、四板在线、STOP 边界 | 2026-12-15 前关闭 5–6 条 |
| W2 | VDC + OTA + Calibration | 同一 build、拓扑/校准就绪 | 2026-12-31 前关闭剩余高风险 |
| 发布 | G4 FULL P3 | 代码冻结 | 发布候选 |

原则：先用现有证据复核，缺口才新跑；一个窗口覆盖多条契约，不逐条上板。

## 8. 度量

| 指标 | 当前 | 目标 |
|---|---|---|
| active 契约比例 | 13/60 ≈ 22% | ≥35%（Wave 0 后） |
| Wave 0 关闭率 | 0/11 | 11/11 |
| Wave 1 关闭率 | 0/10 | 10/10 或有明确结论 |
| 证据复用率 | 未统计 | ≥50% |
| 台架窗口数 | 0 | Wave 1/2 各 1 个为主 |

## 9. 当前状态与下一步

- Wave 0：IN PROGRESS；2/11 已复核（`PASS_WITH_NOTE`，保持 pending），先补 R4/C15 缺口再激活。
- Wave 1：PENDING；先盘点既有 P3/HIL 是否可复用，再排窗口。
- Wave 2：PENDING；暂不排期。
- 当前不改任何契约状态；本计划只定义顺序和判据。

**下一步**：从 Wave 0 的 4 条 docs/静态契约和 3 条 host 契约开始，先拿一批可复核证据；同时把 Wave 1 的 10 条逐条标注“证据可复用/需新跑”。
