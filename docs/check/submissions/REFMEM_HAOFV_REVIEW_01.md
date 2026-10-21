# 核验提交单：RefMem 统一控制面 HAOFV 偏差审查 R1

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_HAOFV_REVIEW_01.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

## 1. 审查范围

对象：`REFMEM-TRANSPORT-01`、`REFMEM-SESSION-01`、`REFMEM-ACTION-01`、`REFMEM-LOCAL-01` 及 CP6/CP9。

目的：只审文档/契约是否符合 HAOFV；不改代码。

## 2. 结论

**`PASS_WITH_CHANGES`**：四条边界总体符合，但有 6 个 HAOFV 偏差需要修：

- 预算引用未定义符号；
- config-control 内 subtype 没有分派规则；
- session 状态没有唯一 writer；
- ActionDispatcher 的 AO/服务归属未写；
- 本地-only 绕过 coordinator 与 CP6 未协调；
- config-control 被误读为硬实时。

## 3. 偏差项

| ID | 位置 | 偏差 | 要求 |
|---|---|---|---|
| `H-B1` | REFMEM-TRANSPORT-01 | `TDMA_CONFIG_CONTROL_RESERVED_BYTES`/`MAX_FRAMES`/`REFMEM_CMD_MAX_PAYLOAD` 不是现有符号 | 改用 `tdma_traffic_class_profile_t` 字段；payload 上限绑定结构体成员 |
| `H-B2` | TDMA REFMEM-TRANSPORT-01 | config-control 的 subtype 没有分派/唯一 consumer 规则，可能碰撞 | `message_class=CONFIG_CONTROL` + subtype 分派；未知 subtype 拒绝 |
| `H-B3` | REFMEM-SESSION-01 | session 状态没有唯一 writer；T2 可能写 session | `ControlCoordinator` 唯一 writer；T2 只上报 target 状态 |
| `H-B4` | REFMEM-ACTION-01 | ActionDispatcher 未写 HAOFV 归属，易被当成新 AO/FB | 明确为 `DistributedRefMemAO` sub-service，`task_refmem_sync`；不新增任务 |
| `H-B5` | CP6/REFMEM-LOCAL-01 | 本地-only 不要求 coordinator，与 CP6“单一协调者”表面冲突 | CP6 限定为跨节点/多目标；单板本地维护不生成跨节点事实 |
| `H-B6` | REFMEM-TRANSPORT-01 | config-control 被当成硬实时运输 | 明确控制面预算，不承诺硬实时；timeout/fence 不替代 TDMA 调度 |

## 4. HAOFV 判定

| 原则 | 判定 |
|---|---|
| 表负责规则 | 符合（ActionRegistry/Registry 静态表） |
| Vector 负责事实 | 符合（不新增运行态 Vector） |
| 事件负责意图 | 符合（session/command 是控制意图，不冒充事实） |
| AO 负责流程 | 部分（H-B3/H-B4） |
| FB 非阻塞 | 部分（H-B4） |
| 单一 writer | 部分（H-B3） |
| 静态部署 | 符合（registry CRC/Version Bundle） |
| 实时边界 | 部分（H-B5/H-B6） |

## 5. 修正记录（2026-10-21，复审后）

- `H-B1` 已改：用 `tdma_traffic_class_profile_t` 四个字段；payload 绑定 `sizeof(refmem_command_request_t.payload)`。
- `H-B2` 已改：增加 `message_class=CONFIG_CONTROL` + subtype 分派；`REFMEM_CMD` 唯一 consumer，未知 subtype 拒绝。
- `H-B3` 已改：`ControlCoordinator` 是 session 状态唯一 writer；T2 只上报；单板 MOUNT 不建系统 session。
- `H-B4` 已改：ActionDispatcher 明确为 `DistributedRefMemAO` sub-service，`task_refmem_sync`，不新增任务。
- `H-B5` 已改：CP6 限定跨节点/多目标；本地-only 不生成跨节点事实。
- `H-B6` 已改：config-control 明确为控制面预算，不承诺硬实时。
- 判定：偏差已修；仍为 `PASS_WITH_CHANGES`，C11 需另一 agent。

## 6. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | fresh pass R1（同一 agent，非第三方） |
| 审核方式 | 条款/HAOFV 对照，不改代码 |
| 审核结论 | `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-21 |
| 备注 | 不构成 C11 最终签署 |
