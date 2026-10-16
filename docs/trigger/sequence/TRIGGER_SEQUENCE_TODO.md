# SYNC_SEQUENCE / 位置内序列任务状态

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`, `docs/sync/SYNC_IO_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`
Last updated: 2026-10-16

本文件只维护**动作侧序列解算与执行**的任务状态与退出判据。语义以 `TRIGGER_SEQUENCE_ARCHITECTURE.md` 为准；提交、构建、OTA/HIL 与失败证据只写入 `TRIGGER_SEQUENCE_TASK_PROGRESS.md`。

## 文档接口

- Architecture 变更先冻结 owner、persona 与跨域契约，再进入本文件排期。
- Task ID 一经进入进度证据不得改名或复用。
- Task Progress 可以证明任务门禁，但不能自行改变本文件状态。

## 任务表

| Task ID | 名称 | 状态 | 判据 / 内容 |
|---|---|---|---|
| SYNC-SCHED-001 | 绝对 deadline 队列深度 ≥2 | PENDING | SWITCH/GATEWAY 执行端支持深度 ≥2 的绝对 deadline 队列；一组在跑时下一组已在硬件队列等待，组间转换由 tick 驱动而非完成观测。逐组独立序号、回执、取消结果；容量满/序号错配/deadline 已过均为终止性失败。**这是位置内序列可执行的前提**（触发域扫描证据：10–60 ms 各档均停在每位置 1 组）。 |
| TRIGGER-SEQ-API-001 | 序列提交接口 | PENDING | 输入 = 位置时间片（绝对 `base_tick` + 身份）+ 本板角色的组序列表（`{argument, delta_q}` 有序）+ settle/READY 策略；第 `g` 组绝对时刻 = `base_tick + (delta_q_g − delta_q_0)`。接口形状需本域确认后冻结，并在 `code_anchor` 真实存在时登记契约。 |
| TRIGGER-SEQ-EVIDENCE-001 | 逐组证据与位置级汇总 | PENDING | 逐组可读：组序号、解算 deadline、实际边沿、完成/取消/失败原因；位置级：已完成组数、是否到末组、失败归因。供触发域位置级判据与台架判读引用（届时 `T2:ACTion?` 的组进度字段改读本域）。 |
| TRIGGER-SEQ-TERM-001 | STOP / 终止语义 | PENDING | STOP 取消队列内全部未执行组并记录硬件取消结果；已经进入硬件、无法取消的部分需明确归属（不确定态不得静默丢弃）。 |
| TRIGGER-SEQ-HIL-001 | 台架验证 | PENDING | 同一固件、参数只经安装变更（不重烧）：扫组间隔找到"每组都完成"的最小间隔；逐组序号/回执/边沿可读；容量满与序号错配负测；STOP 取消负测。 |

## 状态定义

- `PENDING`：未开工。
- `IN PROGRESS`：已开工，进度证据在 `TRIGGER_SEQUENCE_TASK_PROGRESS.md`。
- `DONE`：判据全部满足且有台架证据；跨域接口已在登记表登记。
- `BLOCKED`：存在本域无法消除的前置（须写明前置 task ID 与证据）。

## 依赖与边界

- 上游（触发域，已完成侧）：每位置一个时间片的**分发/冻结/接收/滚动**，以及本板角色组序列的**可安装与可读**；触发域不再承担逐组配速（`T2-SEQ-001` 已转 MOVED TO ACTION SIDE）。
- 本域只对"拿到时间片之后如何解算与执行"负责；板间并行、无跨板握手。
- 与 `SYNC_IO_TODO.md` 的关系：`SYNC-SCHED-001` 在两处引用同一 task ID，语义以本文件为准，排期与进度证据写在本文件系列。
