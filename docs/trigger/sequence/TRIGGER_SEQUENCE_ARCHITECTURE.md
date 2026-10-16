# SYNC_SEQUENCE / 位置内序列解算与执行

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`
Related: `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`, `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`
Last updated: 2026-10-16

本文件定义 **动作侧**在拿到"位置时间片"之后，如何自行解算并执行整条位置内序列。它承接触发域的分发口径：
`T2 只负责分发位置时间片；动作执行得到时间片之后，自行按照约定进行`（`docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`「动作解耦」）。

## 1. 输入：一个位置时间片 + 本板角色的约定

| 输入 | 内容 | 来源 |
|---|---|---|
| 时间片 | 位置 `q_P` 对应的**绝对 Timer1 tick**（`base_tick`）+ 位置身份（session / position id / prediction version） | 触发域 T2 分发（每位置一次） |
| 约定 | 本板角色的**组序列**：第 `g` 组 `{argument, delta_q}`，按序排列；`argument` = 通道码/极化电平/脉宽，`delta_q` = 相对 `q_P` 的细分偏移 | 停机态安装（`TEMPLate:INSTall`，行序即组序） |
| 策略 | 组间 settle 下限、READY 窗口、失败处置 | 本域配置 + 触发域 K-1/K-2 语义 |

**约定不可在运行时热加载**：位置开始前，该板角色的整条组序列已经就位（静态模板）。

## 2. 解算：由时间片展开每组绝对 deadline

第 `g` 组的绝对时刻**只由时间片与静态偏移决定**，不依赖任何一次 T2 服务往返，也不依赖上一组的完成观测：

```text
tick_g = base_tick + (delta_q_g − delta_q_0) → 时钟域换算 → 绝对 deadline_g
```

- 时间片是一次性的输入；**组间不做任何跨域交互**（板间亦无先后：四板并行、只对时间片负责）。
- 组间不由 READY 推进（K-1）。READY 只用于发现踩踏与记录，不用于推迟下一组。
- 一旦某组 deadline 被硬件接受即**不追改**（K-2）；后续组的解算使用当前 DCO 模型，模型不连续由 VDC 域保证。

## 3. 执行：深度 ≥2 的绝对 deadline 队列

**这是本域的核心能力，也是当前唯一的结构性缺口**（`SYNC-SCHED-001`）。

- 执行端同一时刻持有**不少于两组**已解算的绝对 deadline：第 `k` 组在跑时，第 `k+1` 组已经在硬件队列里等待，因此组间转换由 **tick 驱动**，而不是"软件观测到上一组完成再提交下一组"。
- 实测依据（触发域 2026-10-16 台架扫描）：只要布防依赖"观测到完成"，周期取 10/25/30/40/50/60 ms **全部**停在每位置 1 组（`T2:ACTion?` 组打包字 `done=0b1`、`next=1`、`in_flight=0xF`）；观测往返本身大于这些间隔，故该形状不可能支撑紧间隔。
- 现状（2026-10-14 核实）：`sync_io_switch.c` 以 `SYNC_IO_SWITCH_PENDING` 显式拒绝第二个 deadline，`sync_io_gateway.c` 经 `prepare_realtime()` 的 pending 标志间接拒绝；两侧各只有一份 `s_deadline`，头文件自述 “One transaction at a time, Core1 only.”。
- 队列语义：每组独立**序号**、提交/取消/**回执**；容量满、序号错配、deadline 已过都必须给出**终止性**失败（不 fire-now 补偿）。
- STOP/teardown：取消队列内全部未执行组，并记录硬件取消结果（含已经进入硬件、无法取消的部分的归属）。

## 4. 证据：逐组可读

- 逐组需可读：组序号、解算出的 deadline、实际边沿时刻、完成/取消/失败原因。
- 位置级汇总（触发域判据所需）：该位置已完成的**组数**、是否跑到末组、失败归因。
- 触发域页面的组进度字段（`T2:ACTion?` 尾 17 项中的 5 项）在动作侧具备该能力后应改为**读本域**的逐组进度；在此之前它報的是触发域的过渡实现。

## 5. 边界（不做什么）

- 不参与位置分发、冻结、位置窗滚动——那是触发域 T2 的交付（分发/冻结/接收/滚动四段）。
- 不做跨板握手：`SWITCH_A`、`SWITCH_B`、`GATEWAY` 各自从同一个时间片独立展开自己的组序列，板间无先后。
- 不把"上一组完成"当作下一组的前提（除非某组被显式判失败并按终止语义结束该位置）。

## 6. 与触发域的接口（待本域确认后登记契约）

- 触发域提供：每位置的 `base_tick` + 身份 + 本板角色的组序列表（`argument`/`delta_q`）。
- 本域提供：序列提交接口（时间片 + 组序列表 + settle/READY 策略）、逐组回执与失败归因、STOP 取消语义。
- 契约登记时机：本域实现落地、`code_anchor` 真实存在后再登记 `docs/check/DOCS_REGISTRY.md`（暂不登记，避免悬空锚点）。
