# 核验提交单：RefMem Event Plane 子域 HAOFV 符合性复审 R6

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/EVENT_CROSS_REVIEW_06.md`
Related: `docs/refmem/event/REFMEM_EVENT_ARCHITECTURE.md`, `docs/refmem/event/REFMEM_EVENT_TODO.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

## 1. 审查范围

对象：R5 修正后的 `REFMEM_EVENT_ARCHITECTURE.md`/`REFMEM_EVENT_TODO.md`，以及 `REFMEM_DOMAIN_ARCHITECTURE.md` 的运行时命令类型/事件面。

目的：确认静态基线是否仍符合 HAOFV，方案 B 运行时是否已具备 HAOFV 的 owner/写权限/非阻塞/预算/边界条件。

## 2. 结论

- **静态基线：`PASS`（设计层）**。
- **运行时方向：`PASS_WITH_CHANGES`**；R5 的 T-B1–T-B5 已在文档层修正，但仍未实现/未联审，不能判定“符合 HAOFV”。

## 3. R5 项复核

| R5 项 | 复核 | 证据 |
|---|---|---|
| `T-B1` 运行时 owner | ✅ 已命名 `EventRuntimeService`，并明确是 `DistributedRefMemAO` sub-service | 架构 §4.6 |
| `T-B2` EventStatus/EventLog 归属 | ✅ 由 EventRuntimeService 写；EventVector 仍静态；TriggerRun 归 TRIGGER | 架构 §4.7 |
| `T-B3` R0 顺序 | ✅ R0 先 owner/状态/预算/POSITION 门禁/命令契约；R1 只 host | TODO R0–R6 |
| `T-B4` handler 契约 | ✅ ACCEPTED/BUSY/REJECTED、重复 command_seq、ACK 归 RefMem、POSITION `NOT_MY_DOMAIN` | 架构 §4.6 |
| `T-B5` 运行时命令/契约 | ✅ RefMem 命令类型加 `EVENT_INJECT/ABORT`；新增 `EVENT-RUNTIME-01`/`EVENT-TRANSPORT-01` | RefMem 架构；子域 §11 |

## 4. 仍开放项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `U-M1` | 架构 §2 | 符合性表已加运行时四行，但仍标“部分（设计）”；没有 R6 的独立结论 | R6 结论按“部分”保留，等 R1–R3 实现后再升级 |
| `U-M2` | 架构 §4.7 | EventStatus/EventLog 有字段/容量，但 `EVENT_LOG_CAPACITY` 没有数值或预算来源 | R0 给出符号/预算；R2 落地 |
| `U-M3` | 架构 §4.6 | `EventRuntimeService` 的 `event_id` 分配策略只写“EVG 队列”，未定义回绕/唯一性 | R0 定义 `(epoch,event_id)` 唯一与回绕 |
| `U-M4` | 架构 §4.6 | `EVENT_ABORT` 只写“中止 CONTROL_EVENT/session”，没有状态机/幂等语义 | R1 定义 ABORT 状态与重复语义 |
| `U-M5` | 架构 §4.6 | `EVENT_INJECT` 的 required/target mask 来源只写 EventMap；未写 `SlotClaimMap` 解析 | R0 定义 target 解析与 required 缺省 |
| `U-M6` | 架构 §11 | `EVENT-RUNTIME-01`/`EVENT-TRANSPORT-01` 未登记；`EVENT-INJECT/ABORT` 未进 `refmem_command.h` | R6 登记/联审 |
| `U-M7` | TODO R5 | task/WCET/栈/RAM 仍是“待评估” | R0 估算、R5 证据；未完成前不接板端 |
| `U-M8` | 架构 §4.7 | EventLog 持久化只写 “StorageAO 导出”，未定义导出触发/格式 | R2 定义 |

## 5. HAOFV 判定

| 原则 | 静态基线 | 运行时方案 B |
|---|---|---|
| 表负责规则 | 符合（设计） | 部分：EventPack/Map 已设计 |
| Vector 负责事实 | 符合（设计） | 部分：EventStatus/EventLog 非 Vector，字段已列 |
| 事件负责意图 | 符合（边界） | 部分：handler 非阻塞契约已列 |
| AO/流程 | 符合（EventPackService 静态） | 部分：EventRuntimeService 是 RefMem sub-service，未实现 |
| 写权限 | 符合（设计） | 部分：EventStatus/Log writer 已定，ACK 归 RefMem |
| 静态部署 | 符合（设计） | 部分：EventPack 路径/rollback 已设计 |
| 实时域 | 符合（POSITION 归 TRIGGER） | 部分：POSITION 硬门禁已写 |
| 运输 | 不适用 | 部分：CONTROL_EVENT TDMA class 待 R3 |

结论：**静态基线符合；运行时方向符合但未闭环**，不能标记“符合 HAOFV”。

## 6. 建议下一步

1. R0：`EventRuntimeService`/`EventStatus`/`EventLog` 字段、预算、`event_id` 回绕、target/required 解析、POSITION 门禁。
2. R1：handler host 闭环 + ABORT 状态机。
3. R2：EventStatus/EventLog + 导出格式。
4. R3：CONTROL_EVENT TDMA class。
5. R6：`EVENT-RUNTIME-01`/`EVENT-TRANSPORT-01` 登记 + C11。

## 7. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复评 R6（同一 agent 的 fresh pass，非第三方） |
| 审核方式 | R5 修正对照 + HAOFV/RefMem/TRIGGER 边界 |
| 审核结论 | 静态 `PASS`；运行时 `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-21 |
| 备注 | 不构成 C11 最终签署；R0 前需另一 agent/人工复核 |

## 8. 修正记录（2026-10-21，复审后）

- `U-M1`：方案 B 四行从“部分（设计）”改为“设计符合（待实现）”；运输仍“部分（待联审）”。
- `U-M2`：`EVENT_LOG_CAPACITY` 改为 R2 RAM 预算决定，导出格式/路径已定（`/refmem/event/event_log.bin`）。
- `U-M3`：`event_id` 32 bit、`(epoch,event_id)` 唯一、0 无效、耗尽拒绝、回绕按 `int32_t diff`。
- `U-M4`：`EVENT_ABORT` 状态机 `ACCEPTED → ABORT_REQUESTED → ABORTED/ABORT_TIMEOUT`，幂等、未知 target 拒绝。
- `U-M5`：target/required 解析规则（显式/EventMap policy/`SlotClaimMap`、required⊆target、POSITION 拒绝）。
- `U-M6`：`EVENT-RUNTIME-01`/`EVENT-TRANSPORT-01` 仍在契约列表，未登记；transport 待 TDMA 联审。
- `U-M7`：预算符号 `EVENT_RUNTIME_WCET_CYCLES`/`EVENT_RUNTIME_STACK_BYTES`/`EVENT_LOG_RAM_BYTES` 已定，数值待测。
- `U-M8`：EventLog 导出 header/记录/路径已定。
- 结论：设计层阻塞项已闭合；剩余为实现、契约登记、TDMA 联审和 C11。
