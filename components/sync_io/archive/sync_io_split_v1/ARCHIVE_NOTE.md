# sync_io.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `components/sync_io/src/sync_io.c` 在 N5.0.4i 拆分前的完整副本，
用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 2,110 行 / 98 个函数 / 10 个文件级静态对象
- 拆分点：第 1,272 行（`sync_io_disarm_capture_timestamp_window` 之后、
  `sync_io_fire_pulse_ns` 之前，括号深度 0；全文件 18 个条件编译块均不跨越）

## 拆分边界（N5.0.4i）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/sync_io.c`（留） | capture 时基/启动/DMA pop/latch、claim/AUX、init、时钟、状态读取、共享 DMA IRQ 分发 | 1,181 |
| `src/sync_io_output.c`（新） | fire_pulse、clock start/stop、debug 输出掩码、SMA 频率 TX/RX、debug model GPIO、AUX 读写、status | 840 |
| `inc/sync_io_internal.h`（新） | 类型 + 10 个 `extern` 对象 + 15 个跨单元函数原型 + capture DMA 别名宏 | 130 |

**单实例不变**：`s_sync_io` 上下文与 9 个辅助对象只在主单元定义一次；
`sync_io_shared_workspace`（`.sync_io_dma_ring` 段）保持单一全局定义。
capture 启停、DMA 环与所有权令牌的原子对完整留在主单元；带 `#if` 的时钟/AUX/GPIO
条件块随所属函数整体迁移，未跨单元切开。

**配套工具**：`state_machine_resource_check.check()` 现在接受 `sync_core` 为单个路径
或路径元组（默认 `(sync_io.c, sync_io_output.c)`），守卫脚本按并集扫描。
