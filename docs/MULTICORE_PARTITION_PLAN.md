# RP2350 双核分区方案

本文档定义 RP2350_TRIG 的双核演进边界。目标是把实时性要求高的触发域和
管理/观测/存储域隔离，而不是简单把现有函数平均分到两个核心。

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
- core0 完成 `stdio_init_all()`、`board_init()`、`app_init()` 后启动 core1。
- core1 等待 app ready，然后只循环执行 `app_trigger_service()`。
- core0 循环执行 `board_service()`、SCPI、OTA、Storage、UI、Diagnostics/LOG。
- FreeRTOS 路径暂不启用 multicore；SMP 需要单独验证。

## 跨核共享规则

- 共享队列、LOG ring、事件总线和 Vector snapshot 必须走 OSAL critical。
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
