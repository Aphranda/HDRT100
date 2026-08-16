# SYNC_IO / Realtime IO 待办

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/SYNC_IO_TODO.md`
Related: `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/sync/SYNC_IO_TASK_PROGRESS.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TODO.md`
Last updated: 2026-08-16

本文档维护 `sync_io`、底层 realtime IO、PIO/DMA/IRQ mode driver、语义 IO 和板端验证的当前可执行待办。这里不记录普通流水账；完成闭环写入 `SYNC_IO_TASK_PROGRESS.md`。

## 当前优先级

| 顺序 | 主线 | 完成判据 |
|---:|---|---|
| 1 | PIO 预约输出路径 | `ModelTurntableAO` 不再通过 `time_us_64()` + debug GPIO 软件翻转出脉冲；由 `sync_io` realtime owner 装载 PIO/DMA 计划，到点由 PIO 输出边沿。 |
| 2 | 真实最小物理链路配合 RefMem | 至少两板通过真实物理 IO 运行 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY`，IO 侧提供 resource、runtime、quality 证据。 |
| 3 | ARM_IN / EXT_CLK_IN 运行逻辑 | AUX0/AUX1 从语义占位升级为可验证运行路径，接入 TriggerFB/Sync owner 和资源门禁。 |
| 4 | mode runtime self-test | SEQ_STEP、ENC_COUNT、BISS_TAP、模型脉冲输出均有板端 loopback 或外部回放验证。 |
| 5 | sync_io 组件化继续拆分 | `sync_io.c` 只保留 core 初始化和公共基础设施，脉冲、capture、clock、AUX、debug model 分模块维护。 |

## P0 - PIO 预约输出路径

目标：验证“到点出边沿”的硬实时承诺，避免模型转台继续依赖 core0 软件定时和 debug GPIO 写操作。

- [x] 定义模型脉冲输出受控 realtime primitive：输入为 bounded pulse plan，输出为 PIO/DMA 运行态 snapshot。
- [x] `ModelTurntableAO` 只生成扫描脉冲计划和状态事实，不直接翻 GPIO。
- [x] `sync_io` 作为 realtime IO owner 负责装载 PIO 程序、DMA/FIFO、输出引脚方向和安全释放。
- [ ] PIO 计划必须支持 `delay_us/ticks`、`pulse_width_us/ticks`、边沿极性、最大脉冲数、late/overflow 计数。（已完成 delay/pulse/edge/max；late/overflow 计数待 HIL 后补齐。）
- [ ] runtime snapshot 至少覆盖 `running`、`scheduled_count`、`completed_count`、`late_count`、`drop_count`、`last_error`、PIO/DMA 状态。（已完成 running/total/completed/fault/PIO/DMA/FIFO；late/drop 待补。）
- [ ] 停止/FAULT/RESET 必须恢复输出安全态并释放资源。（已完成 STOP/timeout FAULT；RESET 统一 release 待接入。）
- [ ] 增加纯 C 或 host 可执行测试覆盖计划生成：扫描起止、步长、加减速、脉宽、边沿、越界和 0 step 拒绝。
- [ ] 增加板端 HIL：两板或单板 loopback 捕获模型转台输出脉冲，验证数量、方向、安全释放和 runtime snapshot。

架构约束：

- 不把 `ModelTurntableAO` 写成固定 A slot 或固定 GPIO。
- 不绕过 TriggerFB/resource owner 直接抢 PIO/DMA。
- 不让 SCPI 直接驱动边沿；SCPI 只能配置、启动意图或查询 snapshot。
- 模型预约输出当前是 `sync_io` primitive，不进入产品 Trigger mode table；后续如开放为正式 mode，必须补 `sync_io_mode_ops_t.hw` 和资源仲裁表。

## P1 - 真实最小物理链路 IO 支撑

目标：配合 RefMem P4.5，把当前 PC hex bridge 换成真实最小链路时，IO 层有明确 owner、profile、状态和验证证据。

- [ ] 为最小两板 transport adapter 定义 IO profile：输入/输出 pin group、方向、PIO/SM/DMA/IRQ、速率、MTU 和半/全双工规则。
- [ ] 建立 `sync_io` transport primitive 或 mode driver，承接 RefMem Sync frame 发送/接收，不把物理层写进 RefMem 协议层。
- [ ] adapter runtime snapshot 覆盖 tx/rx、CRC/drop/timeout、direction conflict、rx_pending、last_error 和 timestamp source。
- [ ] RefMem HIL 脚本读取 IO runtime snapshot，和 RefMem `QUALITY` 计数互相印证。
- [ ] 两板 COM5/COM6 或当前可用端口完成真实线 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY`。
- [ ] 记录线序、端口、build id、profile CRC、adapter id 和失败证据到 `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md` 或对应报告目录。

## P2 - AUX 语义通道落地

目标：把 `ARM_IN`、`EXT_CLK_IN` 从旧低层宏/诊断采样升级为 HAOFV 语义通道。

- [ ] AUX0/GPIO26 实现 `ARM_IN` 资格/请求路径，进入 TriggerFB 或 Sync owner 门禁。
- [ ] AUX1/GPIO27 实现 `EXT_CLK_IN` 外部参考/采样时钟路径，进入 Sync/VDC 或 REALtime 维护面。
- [ ] `ARM_IN` / `EXT_CLK_IN` 的 snapshot 能区分 physical level、debounced/qualified state、stale、last_edge_tick 和 fault。
- [ ] 与 `SYNC_CLK_OUT`、BiSS persona、CAL_RING、transport adapter 建立资源互斥表。
- [ ] 增加 HIL：输入拉高/拉低、断线、冲突 mode arm、RUN 态禁止修改。

## P3 - Mode Runtime Self-Test

目标：每个已实现底层 mode 都能独立做板端闭环，不依赖“编译通过”判断实时能力。

- [ ] SEQ_STEP：输入边沿 -> 4bit 输出，验证序列索引、DMA rollover、gate 边沿、disarm 安全态。
- [ ] ENC_COUNT：A/B/Z 外部回放或 loopback，验证计数、目标触发、Z 复位、非法 pin 拒绝。
- [ ] BISS_TAP：无外部 BiSS 时保留软件 frame crossing 和 sample scan；外部准备完成后补真实链路。
- [ ] `SYNC_CLK_OUT`：AUX2 输出频率、资源互斥和停止安全态验证。
- [ ] 模型脉冲输出：验证计划装载、脉冲数、宽度、完成态和 abort。
- [ ] 将手写验证固化为 Python 脚本；非必要不手写临时串口流程。

## P4 - 组件化与代码边界

目标：继续降低 `sync_io.c` 单体复杂度，让 mode driver 和 core primitive 边界清晰。

- [ ] 新增或整理 `sync_io_core.c/.h`，保留初始化、trace、SM 状态和共享 IRQ helper。
- [ ] 将 capture primitive 从 `sync_io.c` 拆出。
- [ ] 将 pulse primitive 从 `sync_io.c` 拆出，并明确即时 pulse 与 scheduled pulse 的区别。
- [ ] 将 clock/AUX primitive 拆出，并保持 AUX resource owner 边界。
- [ ] 将 debug model GPIO 维护接口与模型 scheduled output 分离，避免调试 overlay 污染产品实时路径。
- [ ] 为每个 primitive 明确：owner、资源、snapshot、失败码、HIL 验证脚本。

## P5 - RefMem / VDC 集成

目标：IO 的运行事实进入共同事实和共同时间链路，但不反向污染 IO owner。

- [x] 明确 `sync_io_read_capture_words()` 是通用 4 路 raw IO observation primitive，可观测 TDMA PIO 线、转台输入、READY/GATE/ARM/AUX 等数字输入；业务语义由上层 adapter / AO/FB contract 解释。
- [ ] 定义 IoSlot 字段级 publish helper：level/count/runtime/error/quality 只由 IO owner 发布。
- [ ] 通过 RefMemSlotContract 校验 IO fact writer、值域、timestamp、version 和 stale。
- [ ] VDC 只消费 timestamp 样本或输出 fire time，不让 `sync_io` 计算 VDC offset/rate。
- [x] 将 SYNC_IO capture fact 到 VDC `VdcSyncIoAdapter` 的任务接线固化：`vdc_dpll_manager` 增加默认关闭的 raw capture observer，记录 raw word、event id、tick_l32、capture result 和 VDC gate result；多核路径通过 `sync_io_read_capture_latched()` 消费 core1 latch ring。
- [x] 增加 observer 的 SCPI 维护查询：`SYSTem:SYNC:VDC:OBServer?` 返回 raw/no-edge/ambiguous/submitted/accepted/rejected、last raw word、event id、tick_l32 和 gate reject。
- [x] 增加 observer 的 SCPI 维护配置：`SYSTem:SYNC:VDC:OBServer` 支持无参数/`0` 安全关闭，启用态必须显式给出 batch、event id、mask、tick base、sample period、window 和 frame CRC；该命令不启动 capture、不改变 DPLL lock。
- [x] 增加 observer 的板端 HIL 证据字段：`OBServer?` 追加 event/mask/window/frame CRC、schedule/dictionary CRC、dictionary profile CRC、edge index、timestamp source/resolution/flags、source/reference slot 和 payload class；真实硬件 timestamp latch 仍按 VDC P3 待办推进。
- [x] 增加 core1 capture latch ring phase 1：core1 从 PIO capture FIFO 搬运 raw word，附带 sample seq、base time、sample period、timestamp source/resolution/flags 后供 VDC manager 消费；当前标记为 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY`。
- [x] 增加 `REALtime:IO:SAMPle:LATCh?` 维护查询，暴露 capture running、采样率、raw FIFO drop、latched word、latch drop 和 timestamp source/resolution。
- [ ] 将 phase 1 软件微秒 latch 升级为 PIO/DMA/IRQ/core1 hardware tick latch，正式 DPLL 样本必须声明 `HARDWARE_TICK` 且 `timestamp_resolution_ns <= 100`。
- [ ] 将同一 raw observation primitive 用于转台输入/脉冲计数验证，形成独立于 VDC 的 AO/FB 解释路径。
- [ ] late `FIRE_LOAD` 必须由 realtime owner 拒绝并发布 evidence。
- [ ] IO quality 与 RefMem `DistributedConnectionQualityTable` 对齐：CRC/drop/late/timeout/stale 不重复造字段。

## P6 - 文档清理

- [x] 建立三分标准入口：`SYNC_IO_ARCHITECTURE.md`、`SYNC_IO_TODO.md`、`SYNC_IO_TASK_PROGRESS.md`。
- [x] 将旧架构评审 P2 项迁入本文：双核边界、mode 自检、AUX 语义通道和兼容层。
- [x] 将旧重构计划的有效内容迁入新架构：hardware profile、mode driver、resource owner、RJ45/marker 收口。
- [x] 将旧资源规划的有效内容迁入新架构：PIO block、SM、DMA、语义 IO 和资源互斥。
- [x] 将旧分布式 DPLL 方案的有效内容迁入新架构：CAL_RING、VDC 边界、预约触发和 late 规则。
- [x] 删除旧 active 文件并修正全仓引用：`SYNC_IO_RESOURCE_PLAN.md`、`SYNC_IO_REFACTOR_PLAN.md`、`SYNC_IO_ARCH_REVIEW_TODO.md`、`SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md`。
- [x] README 入口只指向三分标准文件。
