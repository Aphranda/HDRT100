# 核验提交单：RefMem 单端口统一控制面三缺口审查 R1

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_01.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

## 1. 审查范围

对象：`REFMEM-TRANSPORT-01`、`REFMEM-SESSION-01`、`REFMEM-ACTION-01` 三条新增 canonical 条款及 pending 登记。

目的：只审架构/契约，不审代码；确认三条缺口是否已闭合到“可实现、可验收”的条款级。

## 2. 结论

**`PASS_WITH_CHANGES`**：三条 canonical 已落位，但 4 个条款级问题会直接影响实现，必须先补：

- `REFMEM_CMD` 复用 config-control，但 config-control 不在 TDMA foundation required mask；
- TriggerSession 的 partial arm / START 失败没有 all-or-nothing 规则；
- `T2:MOUNT` 兼容映射 `ARM+START` 与现网语义可能冲突；
- ActionDispatcher 只覆盖 `action_id`，generic command 没有 handler 路径，且缺有界队列。

## 3. 阻塞项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `V-B1` | TDMA 架构 REFMEM-TRANSPORT-01 | `TDMA_PAYLOAD_CLASS_CONFIG_CONTROL` 不在 `TDMA_PAYLOAD_FOUNDATION_REQUIRED_MASK`；profile 可能未启用 | 条款写明：active profile 必须包含 config-control；缺失时 `REFMEM_CMD` 返回 `TRANSPORT_UNAVAILABLE`，不静默降级 |
| `V-B2` | RefMem 架构 REFMEM-SESSION-01 | `START` 只写“source 启动 + required targets arm”；若 target arm 部分失败，source 已启动，没有回滚/降级规则 | 定义 `ARMING` 聚合状态：required 未全 arm 不得 START；START 后失败进入 `DEGRADED/ABORT`，不静默 partial |
| `V-B3` | RefMem 架构 REFMEM-SESSION-01 | `T2:MOUNT` 映射为 `ARM+START`，但现网 MOUNT 可能已启动发布；新 session 分两步，兼容模式可能重复启动 | 定义兼容模式：`MOUNT` = `CONFIGURE+ARM+START` 一次性，仅旧 SCPI；新 session 用分步命令 |
| `V-B4` | RefMem 架构 REFMEM-ACTION-01 | `ActionDispatcher` 只描述 `action_id→handler`；`START/STOP/LOAD` 等 generic command 没有 handler 路径；缺有界队列 | 增加 `command_type→handler` 表；post 使用有界 mailbox/优先级；满时 BUSY/REJECTED，不阻塞 |

## 4. 重大项

| ID | 问题 | 要求 |
|---|---|---|
| `V-M1` | `REFMEM_CMD_MAX_PAYLOAD` 无来源 | 与 `refmem_command_request_t` 对齐，给符号或结构体上限，不手写 |
| `V-M2` | session 的 `source_slot` 能力要求未写 | 明确需 COUNTER/VDC 能力；缺失由 DeploymentGate 拒绝 |
| `V-M3` | ActionRegistry 版本/CRC 未进 Version Bundle | R0 条款补 `action_registry_crc` |
| `V-M4` | 三条契约 pending 但无独立复核记录 | 本审查作为第一次；C11 仍需另一 agent |
| `V-M5` | 统一面可能替换/门控单板控制 | 增加 `REFMEM-LOCAL-01`：单板 SCPI/slot/owner/T2 保留；统一面不可用不得静默缩成单板 |

## 5. HAOFV 判定

| 原则 | 判定 |
|---|---|
| 命令面唯一 | 符合（条款明确 RefMem slot/ACK/lease） |
| 目标本地执行 | 符合（ActionDispatcher post owner） |
| 非阻塞 | 部分（V-B4 队列缺失） |
| 静态部署 | 部分（V-M3 registry CRC） |
| 实时边界 | 符合（POSITION 归 TRIGGER） |
| 运输确定性 | 部分（V-B1 profile mask） |

结论：**方向符合；V-B1–V-B4 闭合前不能进入实现。**

## 6. 建议修正顺序

1. V-B1：config-control profile mask / `TRANSPORT_UNAVAILABLE`。
2. V-B2：ARMING 聚合/all-or-nothing。
3. V-B3：T2 MOUNT 兼容模式。
4. V-B4：command handler + 有界 mailbox。
5. V-M1–V-M4。

## 7. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复评 R1（同一 agent 的 fresh pass，非第三方） |
| 审核方式 | 条款/边界对照，不改代码 |
| 审核结论 | `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-21 |
| 备注 | 不构成 C11 最终签署 |

## 8. 修正记录（2026-10-21，复审后）

- `V-B1` 已改：active profile 必须含 config-control；缺失返回 `TRANSPORT_UNAVAILABLE`；不改 foundation required mask。
- `V-B2` 已改：状态机增加 `ARMING`；required 未全 arm 不得 START；START 后失败进入 `DEGRADED`，必须显式 STOP/ABORT。
- `V-B3` 已改：`T2:MOUNT` 仅兼容模式一次性 `CONFIGURE+ARM+START`，RUNNING 幂等；新应用分步。
- `V-B4` 已改：增加 `CommandHandlerRegistry`（command_type→handler）；有界 mailbox、优先级、满时 BUSY/REJECTED。
- `V-M1` 已改：`REFMEM_CMD_MAX_PAYLOAD` 绑定 `sizeof(refmem_command_request_t.payload)` 或等价上限。
- `V-M2` 已改：`source_slot` 必须唯一且具备 COUNTER/VDC 能力，缺失 DeploymentGate 拒绝。
- `V-M3` 已改：`action_registry_crc` 进 Version Bundle。
- `V-M4` 仍开放：三条契约 pending，C11 需另一 agent；本审查是 fresh pass，不是签署。
- `V-M5` 已改：新增 `REFMEM-LOCAL-01`（单板控制保留）；统一面增量，不替换本地 SCPI/slot/owner/T2；统一面不可用时远端/全目标拒绝，不静默缩成单板。
- 判定：条款级阻塞已闭合；仍为 `PASS_WITH_CHANGES`，等独立复核/契约评审。
