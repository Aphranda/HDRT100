# RP2350 双核分区方案

Status: Active
Domain: MULTICORE
Canonical: `docs/MULTICORE_PARTITION_PLAN.md`
Related: `docs/RTOS_PORTING_PLAN.md`, `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`, `docs/SYNC_IO_REFACTOR_PLAN.md`, `docs/communication/BISSC_TAP_BRIDGE_DESIGN.md`
Last updated: 2026-08-10

本文档定义 RP2350_TRIG 的双核演进边界。目标是把实时性要求高的触发域和
管理/观测/存储域隔离，而不是简单把现有函数平均分到两个核心。

## 产品化目标

产品化四板分布式触发固件目标为 RTOS + 双核 AMP 模型：一个控制核、一个实时核。
RTOS 提供任务、同步原语、超时和观测边界；双核提供控制面与实时控制面的物理隔离。
PIO/DMA/IRQ 仍负责硬实时边沿，实时核只负责提前装载、状态推进、快速故障判定和时间戳闭环。

| 核心 | 产品化角色 | 职责 |
|---|---|---|
| control core | 控制核 | SCPI/USBTMC/CDC、A3 网关、SD/StorageAO、OTA、UI、日志落盘、故障归档、角色和程序包配置 |
| realtime core | 实时核 | TriggerAO、RJ45_SYNC_RING 服务、虚拟 DC、预约触发队列、PIO/DMA/IRQ 状态采样、READY/T2 捕获 |

产品化 release 不应只以裸机单核或裸机双核作为最终架构。当前裸机双核仍可用于 bring-up
和跨核问题定位；RTOS 双核闭环通过前，release/validation 可以保持单核保守路径。

## 目标分区

| 核心 | 域 | 职责 |
|---|---|---|
| core0 | Management Core | `board_service()`、SCPI、OTA、StorageAO、UI、Diagnostics/LOG flush、故障归档 |
| core1 | Realtime Core | `sync_trigger_service()`、触发状态机推进、PIO/DMA 状态采样、BiSS runtime sample |

硬实时边沿仍由 PIO/DMA/IRQ 负责。core1 只运行实时控制面和状态服务，不用普通
C 循环直接产生精确边沿。

## 当前 P0 策略

- 默认 `PROJECT_USE_MULTICORE=OFF`，保持 release/validation 单核路径不变。
- 裸机实验构建可打开 `PROJECT_USE_MULTICORE=ON`。
- RTOS + 双核 AMP smoke 构建可同时打开 `PROJECT_USE_FREERTOS=ON` 和
  `PROJECT_USE_MULTICORE=ON`；该路径不是 FreeRTOS SMP，而是 core0 运行 FreeRTOS
  管理任务，core1 运行受限实时循环。
- core0 完成 `stdio_init_all()`、`board_init()`、`app_init()` 后启动 core1。
- core1 等待 app ready，然后只循环执行 `app_realtime_run_once()`，其内部推进 `Trigger` 状态机并记录 core1 心跳。
- core0 循环执行 `board_service()`、SCPI、OTA、Storage、UI、Diagnostics/LOG。
- FreeRTOS SMP 仍不启用；后续如需 SMP 必须单独验证。

## 跨核共享规则

- 跨核业务命令必须通过 mailbox/event queue 投递，优先使用 Pico SDK `queue_t` 或后续 OSAL mailbox
  封装；`multicore_fifo`/doorbell 只允许作为唤醒信号，不承载复杂业务 payload。
- 共享队列、LOG ring、事件总线和 Vector snapshot 必须走 OSAL critical 或 SDK/OSAL 提供的
  multicore-safe primitive。
- 双核裸机模式下，OSAL critical 必须使用 RP2350 spin lock，而不是只关本核中断。
- SCPI/UI/OTA/Storage 不允许直接修改触发域内部状态，只能投递事件或读快照。
- core1 不允许执行 FatFs、SCPI、USB CDC 文本输出、OTA flash job 或阻塞式 SD 访问。
- core1 允许投递极小 trace/log 证据，但实际输出/落盘由 core0 服务。
- `SYST:CORE?` 用于确认双核 smoke：core1 enabled 为 true，且 core1 loop count 持续增长。

## 当前验证状态

- 2026-07-07 已按单核主线回退并完成闭环验证：烧录 `build-biss-integration\RP2350_TRIG_FACTORY.uf2`，build id `20260707081355`，`SYST:CORE?` 第一字段为 `0`，确认 core1 关闭。
- 单核 SEQ_STEP smoke 已通过：`TRIG:MODE 1 -> TRIG:ARM -> TRIG:DISA` 后回到 state id `0`，错误队列返回 `0,"No error"`。
- 单核 BiSS board smoke 已通过：`tools\biss_board_validate\biss_board_validate.py COM4 --out-dir build-biss-integration\biss_validation_singlecore`，结果为 `PASS`。
- 双核 smoke 暂停作为后续议题：此前 `SYST:CORE?` 初期可见 core1 启动，但后续 core1 loop count 停止增长，`TRIG:MODE 1` 后状态未按预期切换；在定位跨核事件队列/critical section 前，release 和板端验证继续使用单核。

## 后续加固

- 将 `sync_trigger` 事件队列的 enqueue/dequeue 全部纳入 OSAL critical。
- 将 `storage_manager_trace_event()` 审计为跨核安全，必要时改成跨核 ring buffer。
- 增加板端双核验证脚本：烧录 multicore smoke、查询 `*IDN?`、`SYST:CORE?`、`TRIG:ARM/DISarm`、`SYST:LOG:STAT?`、`SYST:TRACe:LAST?`。
- 增加双核 HIL smoke：启动、SCPI baseline、Trigger arm/disarm、LOG STAT、SD trace decode。
- 在双核闭环通过前，release preset 必须保持 `PROJECT_USE_MULTICORE=OFF`。
