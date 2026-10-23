# 核验提交单：RefMem 统一控制面 C0/C1a 收口

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_03.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `tools/refmem/refmem_cmd_tdma_workflow/README.md`
Last updated: 2026-10-23

## 1. 审查范围

对象：`REFMEM-CTRL-01` 的 **C0**（命令槽物理放置冻结）与 **C1a**（`REFTMEM_CMD` 全目标投递/解析/ACK）。

证据固件：build `20261023121304`（与当前工作区源码一致；P3 凭证 `hawf-201249`）。以下数字均为
本轮 HIL 快照，非事实源。

不审：C1 的非协调者 Entry 转发、C2–C5、协调者租约、聚合视图、跨节点 FENCE。

## 2. 结论

**建议 `PASS_WITH_OPEN_ITEMS`**：C0 决策可冻结；C1a 的可观测路径（SCPI 入队 → TDMA
维护流 → 目标 owner 解析/执行/ACK）在当前四板上成立。C1 其余部分与 C2–C5 保持开放；
`REFMEM-CTRL-01` 等五项契约维持 pending，本单不升级登记状态。

## 3. C0 决策

| 项 | 内容 |
|---|---|
| 规范物理位置 | `components/distributed_refmem/src/distributed_refmem.c` 的 static `s_refmem_command_slot` |
| `ack_command` 预留区 | `refmem_vector_table_t.ack_command`（`DISTRIBUTED_REFMEM_ACK_CMD_SIZE`）仅目录注册，不承载 live slot |
| 是否迁入 | 不迁入；不同步目录/布局版本 |
| 理由 | owner、SCPI intent、slot 生命周期均绑定该 static；迁入需改目录与布局版本并重做单板语义，收益不足以覆盖风险 |
| 文档同步 | `REFMEM_DOMAIN_ARCHITECTURE.md` 的“当前实现状态”与 C0 段；`REFMEM_DOMAIN_TODO.md` P8 C0 已勾选 |

## 4. C1a 证据（build 20261023121304，`target_mask=0x0F`）

| 项 | 结果 |
|---|---|
| 工作流 | `out/refmem_cmd_tdma/closeout_c0c1/report.json`，7/7 步 PASS，`wire` 56 B |
| 源（COM5 / NO.1） | `submitted=1`、`served=1`、`tx_ok=1`、`rejected=0`；`parser_registered=3`、`parser_unknown=0` |
| 目标（COM4/COM3/COM6） | 每板 `rx_events=1`、`decoded=1`、`posted=1`、`acked=1` |
| 解析器套件 | `out/refmem_cmd_tdma/closeout_parser_suite/suite_report.json`，3/3 parser PASS：0x52 `refmem_parser_ok=1`、0x53 `t2_static_ok=1` 且 `t2_static_applied=1`、0x54 `t2_dynamic_ok=1`（三目标逐板） |
| 拒绝套件 | `out/refmem_cmd_tdma/closeout_reject_suite/reject_suite_report.json`，3/3 case PASS：0x52 坏 CRC → `refmem_parser_reject=1`、0x53 坏 magic → `t2_static_reject=1`、0x54 坏 reserved → `t2_dynamic_reject=1`；均无 `acked` |

工作流步骤：`reset_boards → discover_boards → set_second_class_seat → queue_refmem_cmd →
start_ring_without_t2 → read_statuses → verify_all_target_ack`，全部 PASS。

## 5. 未覆盖 / 开放项

| ID | 项 | 状态 |
|---|---|---|
| O1 | C1 非协调者 Entry → Coordinator → Target 转发与聚合回程 | 未做 |
| O2 | C4 多板 all-target `START`/`STOP` 逐目标证据 | 未做 |
| O3 | C5 `LOAD:SD`/activation 跨节点 FENCE | 未做 |
| O4 | C2 required/partial/timeout/preempt/重复幂等 | 未做 |
| O5 | C3 协调者租约、`control_owner_slot` 唯一性、双主拒绝 | 未做 |
| O6 | 聚合视图/`control_reply`、命令目录 CRC 进 Version Bundle | 未做 |

## 6. 边界与风险

- 只证明“参考板作为唯一入口”的 all-target 投递；未证明任意入口同语义。
- ACK 是目标本地 posted/acked 计数，不是跨节点聚合 `control_reply`。
- 拒绝套件只覆盖三类指定坏载荷，不覆盖全部 `refmem_command_reason_t`。
- 五项契约仍 pending；C11 独立复核后由主控决定是否更新登记表。

## 7. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 待独立复核（本单为提交材料，非自审结论） |
| 审核方式 | 证据/边界对照，不改代码 |
| 建议结论 | `PASS_WITH_OPEN_ITEMS` |
