# 核验提交单：RefMem 统一控制面四条款复审 R2

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_02.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

## 1. 审查范围

对象：`REFMEM-TRANSPORT-01`、`REFMEM-SESSION-01`、`REFMEM-ACTION-01`、`REFMEM-LOCAL-01` 四条款与 pending 登记。

目的：只审架构/契约，不审代码；确认单板保留 + 统一面增量的边界是否闭合。

## 2. 结论

**`PASS_WITH_CHANGES`**：四条款已落位，但 4 个运行语义还需补：

- ARMING 失败后已 arm 的 target 未要求 DISARM；
- STOPPING 无超时/失败状态；
- 运行期收到未注册 command_type 没有拒绝规则；
- target_mask 解析失败可能回退本地。

## 3. 阻塞项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `V2-B1` | REFMEM-SESSION-01 | ARMING 失败只写回 CONFIGURED，已 arm 的 target 可能残留 | 失败时已 arm target 必须 DISARM；source 不启动；回 CONFIGURED/FAULT |
| `V2-B2` | REFMEM-SESSION-01 | STOPPING 无超时，可能卡死或静默 IDLE | `STOP_TIMEOUT -> DEGRADED/FAULT`；排空证据写 EventLog |
| `V2-B3` | REFMEM-ACTION-01 | 运行期未注册 command_type 没有拒绝规则 | 直接 `REJECTED(UNHANDLED)`；required 在 DeploymentGate 拒绝 |
| `V2-B4` | REFMEM-LOCAL-01 | target_mask 解析失败可能被当作本地执行 | 解析失败直接拒绝，不回退本地 |

## 4. 重大项

| ID | 问题 | 要求 |
|---|---|---|
| `V2-M1` | `REFMEM-LOCAL-01` 未写“本地-only 不需要 coordinator/lease” | 明确本地路径不要求 coordinator/lease，但仍过 permission/run-state |
| `V2-M2` | `REFMEM-SESSION-01` 未写 source arm 失败 | source 准备失败同样按 ARMING 失败处理 |
| `V2-M3` | `REFMEM-ACTION-01` 未写 mailbox 满时 required 的拒绝 | required 满时 `REJECTED(QUEUE_FULL)`，不静默丢 |
| `V2-M4` | 四条契约仍 pending，C11 未签 | 保持 pending；独立第三方复核后再升级 |

## 5. HAOFV 判定

| 原则 | 判定 |
|---|---|
| 命令面唯一 | 符合 |
| 目标本地执行 | 符合 |
| 非阻塞 | 部分（V2-B3/V2-M3） |
| 单板保留 | 部分（V2-B4/V2-M1） |
| POSITION/实时边界 | 符合 |
| 运输确定性 | 符合（config-control profile 要求已写） |

结论：方向符合；V2-B1–V2-B4 闭合前不进入实现。

## 6. 修正记录（2026-10-21，复审后）

- `V2-B1` 已改：ARMING 失败要求已 arm target `DISARM`，source 不启动，回 `CONFIGURED/FAULT`。
- `V2-B2` 已改：`STOP_TIMEOUT -> DEGRADED/FAULT`，不静默回 IDLE。
- `V2-B3` 已改：运行期未注册 `command_type` 直接 `REJECTED(UNHANDLED)`。
- `V2-B4` 已改：`target_mask` 解析失败直接拒绝，不回退本地。
- `V2-M1`–`V2-M3` 仍建议在 M0 条款冻结时显式写入；`V2-M4` 保持 pending。
- 判定：条款级阻塞已闭合；仍为 `PASS_WITH_CHANGES`，C11 需另一 agent。

## 7. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | fresh pass R2（同一 agent，非第三方） |
| 审核方式 | 条款/边界对照，不改代码 |
| 审核结论 | `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-21 |
| 备注 | 不构成 C11 最终签署 |
