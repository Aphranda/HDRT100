# 核验提交单：RefMem Event Plane 子域运行时方向审查 R5

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/EVENT_CROSS_REVIEW_05.md`
Related: `docs/refmem/event/REFMEM_EVENT_ARCHITECTURE.md`, `docs/refmem/event/REFMEM_EVENT_TODO.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

## 1. 审查范围

对象：

- `REFMEM_EVENT_ARCHITECTURE.md` 静态基线；
- `REFMEM_EVENT_TODO.md` 新增的 R0–R6 运行时拆分计划；
- 与 RefMem 命令面、TRIGGER 实时域、HAOFV 的边界。

目的：按“RefMem 管命令/事实、Event Plane 管事件字典+CONTROL_EVENT handler、TRIGGER 管实时执行”的方向，判断静态基线是否仍符合 HAOFV，以及运行时拆分计划是否已经具备 HAOFV 落地条件。

## 2. 结论

**静态基线：`PASS`（设计层）**。

**运行时计划：`PASS_WITH_CHANGES`，当前还不能按 R0–R6 直接实施**。方向正确，但 5 项阻塞未定义：

- 运行时 owner/组件未命名；
- EventStatus/EventLog 归属与现有静态文档冲突；
- R0 排在 R1/R5 之前，handler 没有状态和预算；
- handler 的同步/异步/ACK 契约未定义；
- 运行时命令类型、权限、运输契约未进入 RefMem/HAOFV 文档。

## 3. 静态基线复核（HAOFV）

| HAOFV 原则 | 静态基线 | 判定 |
|---|---|---|
| 表负责规则 | EventPack/Catalog/Map/Alias | 符合（设计） |
| Vector 负责事实 | EventVector 静态、`s_event_vector`、EventPackService 唯一 writer | 符合（设计） |
| 事件负责意图 | 运行期归 RefMem/TRIGGER；静态只给目录 | 符合（边界） |
| 事件载荷所有权 | 静态不承载 payload | 不适用 |
| AO/FB | EventPackService 是静态 service，不是 AO/FB | 符合（设计，需在实现时声明） |
| 写权限 | EventVector 唯一 writer；ACK 归 RefMem | 符合（设计） |
| 静态部署 | EventPack 路径/previous/rollback 已定义 | 符合（设计） |
| 实时域 | POSITION/T2/TriggerRun 归 TRIGGER | 符合（设计） |

静态基线可以继续作为 Draft 保留；没有发现新的实时越界。

## 4. 运行时计划阻塞项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `T-B1` | TODO R0–R6；架构 §4.0 | 运行时 owner 未命名：`EventPackService` 是静态 owner，但 R0 说 Event Plane 注册 `EVENT_INJECT/ABORT` handler，没有说明谁执行、在哪个上下文 | 命名 `EventRuntimeService`（或等价）作为 CONTROL_EVENT handler；与 `EventPackService` 分开；明确 task/上下文 |
| `T-B2` | 架构 §3/§4.5；TODO R1 | 当前静态文档说 EventStatus/EventLog 由 RefMem/TRIGGER 提供；R1 又说由 Event Plane 运行时 service 写，归属冲突 | 先定 EventStatus/EventLog owner：Event Plane runtime 写，还是 RefMem/TRIGGER 写；只保留一个 writer |
| `T-B3` | TODO R0/R1/R5 | R0（handler）排在 R1（状态）和 R5（预算）之前；handler 需要状态和预算才能实现 | 重排：先定义 owner/状态/预算，再接 handler；至少 R0 只做 host 契约，不接固件 |
| `T-B4` | TODO R0；架构 §6 | handler 契约未定义：同步返回还是 BUSY？结果如何回 RefMem？重复 `command_seq` 如何处理？ | 定义 `event_runtime_handler(command, result)` 非阻塞契约；BUSY/ACCEPTED/REJECTED；ACK 由 `DistributedRefMemAO` 写 |
| `T-B5` | RefMem 架构命令类型表；TODO R0/R6 | `EVENT_INJECT`/`EVENT_ABORT` 未进入 RefMem command type 表；`EVENT-RUNTIME-01` 未进入契约矩阵；权限/run-state 未定义 | 增加运行时命令类型、权限矩阵、reason、`EVENT-RUNTIME-01`；与 `REFMEM-CTRL-01` 联审 |

## 5. 运行时计划重大项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `T-M1` | TODO R2 | POSITION 隔离被放在 R2，但 R0/R1 期间可能已经接 handler；POSITION 必须从 R0 起就硬隔离 | 在 R0 门禁写死：Event Plane 只处理 CONTROL_EVENT，POSITION 永不进入 |
| `T-M2` | TODO R3 | `CONTROL_EVENT` TDMA class/budget 仍是后续项，但 R0/R1/R4 已假设可 transport | 先定义 class/budget/seat 占位或明确 host-only 阶段 |
| `T-M3` | TODO R1 | EventStatus 字段契约未定义；HAOFV Vector 字段契约要求 writer/value/lifecycle/snapshot | 给 EventStatus 字段表；EventVector 仍只静态 |
| `T-M4` | TODO R1 | EventLog 容量/RAM/持久化/溢出未定 | 定义 `EVENT_LOG_CAPACITY`、RAM 预算、StorageAO 导出 |
| `T-M5` | TODO R5 | task/预算排在 R5，晚于 handler 实现；HAOFV 要求先有预算 | 把预算前移到 R0 前或 R0 host-only 门禁 |
| `T-M6` | 架构 §2 | 符合性表仍是“静态 only”，运行时加入后会出现“不适用 AO/FB”与新 runtime service 冲突 | 增加“静态/运行时”两列或条件行，避免表过期 |
| `T-M7` | 架构 §4.0/§4.5 | `EventPackService` 与运行时 handler 命名未分离，容易把静态 owner 当 runtime owner | 固定命名：`EventPackService`（静态）/`EventRuntimeService`（运行时） |
| `T-M8` | 架构 §11 | 契约矩阵缺 `EVENT-RUNTIME-01`、权限/run-state 契约 | 纳入契约矩阵；与 `REFMEM-CTRL-01` 联审 |

## 6. 建议的重排

```text
R0 契约/owner：
  - EventPackService（静态）与 EventRuntimeService（运行时）命名
  - EventStatus/EventLog owner + 字段契约
  - task/预算（至少 core0 估算）
  - POSITION 硬隔离门禁
  - EVENT_INJECT/ABORT command type + 权限矩阵（可先 host）

R1 host handler：
  - event_runtime_handler 非阻塞契约
  - host 闭环：command -> handler -> result -> ACK 模拟
  - 不接固件、不接 POSITION、不接 transport

R2 状态/日志：
  - EventStatus/EventLog 落地
  - EventVector 仍静态

R3 CONTROL_EVENT 运输：
  - TDMA class/budget/seat；POSITION 不变

R4 执行：
  - post 给 owner/ActionDispatcher

R5 预算/板端：
  - task/stack/WCET；单板/两板 HIL

R6 契约/C11：
  - EVENT-RUNTIME-01/EVENT-TRANSPORT-01 登记
```

## 7. HAOFV 判定

- 静态基线：**符合（设计）**。
- 运行时计划：**方向符合，但当前不能判定“符合 HAOFV”**；T-B1–T-B5 闭合前，运行时还不具备 AO/task/写权限/命令契约。
- 关键红线不变：RefMem 唯一命令面、TRIGGER 唯一 POSITION/实时域、Event Plane 不做第二命令槽/第二 EVR。

## 8. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复评 R5（同一 agent 的 fresh pass，非第三方） |
| 审核方式 | 静态基线 + R0–R6 计划对照 HAOFV/RefMem/TRIGGER |
| 审核结论 | 静态 `PASS`；运行时 `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-21 |
| 备注 | 不构成 C11 最终签署；R0 前需另一 agent/人工复核 |

## 9. 修正记录（2026-10-21，复审前）

- `T-B1` 已改：新增 `EventRuntimeService`（§4.6），与静态 `EventPackService` 分离；运行在 `task_refmem_sync`，R5 前不新增任务。
- `T-B2` 已改：EventStatus/EventLog 明确由 `EventRuntimeService` 写（§4.7）；EventVector 仍静态；TriggerRun 仍 TRIGGER。
- `T-B3` 已改：R0 重排为 owner/状态/预算/POSITION 门禁/命令契约；R1 只做 host handler；不再先写 handler。
- `T-B4` 已改：handler 契约表（ACCEPTED/BUSY/REJECTED、重复 command_seq、ACK 由 RefMem 写、POSITION `NOT_MY_DOMAIN`）。
- `T-B5` 已改：RefMem 命令类型表加入 `EVENT_INJECT/ABORT`（方案 B，R0 后启用）；新增 `EVENT-RUNTIME-01`/`EVENT-TRANSPORT-01` 契约；权限矩阵留在 R0。
- `T-M1`–`T-M8` 已在 TODO/架构中对应：POSITION 硬门禁、transport 后续、EventStatus/Log 字段、预算前移、符合性表条件化、命名分离、契约矩阵。
- 判定调整：静态基线仍 `PASS`；运行时仍 `PASS_WITH_CHANGES`，等 R0 host 契约和独立复核。
