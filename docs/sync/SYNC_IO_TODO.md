# SYNC_IO / Realtime IO 待办

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/SYNC_IO_TODO.md`
Related: `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/sync/SYNC_IO_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TODO.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_TODO.md`
Last updated: 2026-09-04

本文档只维护 SYNC_IO 域的里程碑、任务状态和退出门禁。稳定语义以
`SYNC_IO_ARCHITECTURE.md` 为准，提交、构建、OTA/HIL 和失败证据只写入
`SYNC_IO_TASK_PROGRESS.md`。

## 文档接口

- Architecture 变更先冻结 owner、persona 和跨域契约，再进入本文件排期。
- TODO task ID 一经进入进度证据不得改名或复用。
- Task Progress 可以证明任务门禁，但不能自行改变本文件状态。
- 已归档的旧 P0--P6 勾选清单只保留在 git 历史；未完成能力已映射到下表稳定 ID。

## 状态规则

状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`。代码或文档完成但验证门禁未完成时
不得标记为 `DONE`。质量异常在调试流程中记录并继续采集；资源冲突、GPIO 写权限错误、FIFO
双消费者和 persona identity 不一致属于结构错误，不得放宽。

## 已有基线

- `sync_io_hw_profile.h` 已提供语义 IO 与 active board profile 边界。
- `sync_io_mode_ops_t` 已提供 mode 资源元数据和 arm/disarm 接口。
- 通用输入 capture、预约脉冲、SMA observer、SEQ_STEP 和 ENC_COUNT 已有实现基础。
- 状态机域已冻结 Realtime Observation/SYNC_IO/SMA PIO 与 TDMA TX/RX PIO 的分区；
  `SM-RES-004` 已完成 TDMA RX DATA output/unload、clock evidence 类型化 endpoint 和资源
  生命周期闭环。后续 PIO0 descriptor 不得引用或复用这些 TDMA endpoint。
- 当前输出型 `BOARD_SYNC_PIO_WAVE` 仍是迁移兼容实现，逻辑分析仪尚未成为独立 persona。

## 当前主线

本域按以下依赖顺序推进，不并行绕过前置资源层：

```text
文档与跨域契约
  -> PIO0 persona descriptor / compatibility matrix
  -> PIO0 claim-load-arm-stop-release manager
  -> WAVE_OUTPUT 迁移
  -> LOGIC_ANALYZER 公共模型
  -> RAW_SAMPLE
  -> EDGE_TIMESTAMP
  -> TRIGGERED_CAPTURE
  -> Core0/SD/离线分析
  -> TDMA 快速闭环与长时间观测
```

逻辑分析仪实现不得抢占状态机迁移主线。任何状态机或 SYNC_IO 固件变更都先通过 host/build，
再运行统一快速硬件验收中的 TDMA 短帧闭环；NO1--NO4 的 TDMA 波形和 NO5 的外部 DPLL
波形继续按各自 owner 验证。

## 里程碑总览

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SYNC-M1 | SYNC_IO 文档、PIO 分区与逻辑分析仪契约重构 | DONE | 三件套格式完成；顶层和状态机文档一致；契约登记为 pending；文档门禁与 pre-commit 通过。 |
| SYNC-M2 | PIO0 persona 资源与生命周期收敛 | DONE | descriptor、兼容矩阵、原子 claim/release、失败回滚和无泄漏负测通过；四节点 OTA、TDMA 短帧和 SD 原始证据已收口。 |
| SYNC-M3 | 独立逻辑分析仪 persona | PENDING | RAW、EDGE、TRIGGERED 三模式及 snapshot、drop evidence、Core0 drain 均完成。 |
| SYNC-M4 | 既有输入/输出/mode 能力迁移与清理 | IN PROGRESS | 输出 persona 不再占 TDMA PIO；旧 handoff 删除；active profile 与其余 mode 清理仍待完成。 |
| SYNC-M5 | 硬件验收与长期观测 | PENDING | 只读性、短帧无扰动、SD 完整性、长时间边沿曲线和外部证据关联均通过。 |

## 当前任务表

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SYNC-DOC-001 | 重构 SYNC_IO 三件套并冻结 `ARCH-IOANALYZER-01` | DONE | 架构、TODO、进度、顶层和 registry 同步；检查器、pytest、pre-commit 全绿。 |
| SYNC-RES-001 | 建立 PIO0 persona descriptor 与兼容矩阵 | DONE | 每个 persona 声明 SM、instruction、GPIO read/write、FIFO、DMA/DREQ、IRQ、workspace 和 safe state；静态冲突负测通过。 |
| SYNC-RES-002 | 建立 PIO0 persona lifecycle manager | DONE | validate/claim/load/arm/stop/release 原子化；失败保持 STOPPED 或恢复旧 persona，无部分 claim；TDMA 短帧闭环通过。 |
| SYNC-RES-003 | 将 `WAVE_OUTPUT` / `SCHEDULED_TRIGGER` 从 TDMA TX PIO 迁入 PIO0 owner | DONE | 输出与预约触发由 PIO0 persona manager 装载并运行；旧 PIO1 suspend/resume handoff 已删除；四节点 OTA、TDMA 短帧和 SD 原始证据通过。 |
| SYNC-LA-001 | 定义逻辑分析仪 config、capture record、runtime snapshot 和结束 reason | DONE | API 不暴露任意 GPIO 写；只读 pad mask 复用 persona descriptor；profile/source/persona generation、门禁原因、原始观测、调试有界继续和硬停类别可追溯。 |
| SYNC-LA-002 | 实现 `RAW_SAMPLE` 短窗口采集 | IN PROGRESS | bounded SRAM record ring、wrap/CRC、drop/overrun 和容量边界 host 验证已完成；真实 PIO/DMA arm、短窗口硬件证据仍待完成。 |
| SYNC-LA-003 | 实现 `EDGE_TIMESTAMP` 长时间采集 | PENDING | PIO hardware tick 记录 edge mask/level；空闲期无无界数据；wrap 和时间连续性可恢复。 |
| SYNC-LA-004 | 实现 `TRIGGERED_CAPTURE` 前后窗口 | PENDING | level/edge/pattern trigger、bounded pre/post window、timeout/end reason 和重复 ARM 测试通过。 |
| SYNC-LA-005 | 接入 Core1 snapshot、Core0 drain 和 StorageAO | PENDING | Core0 查询不触发动作；active/shadow 缓冲交接明确；SD 慢写只产生 analyzer drop evidence，不阻塞实时路径。 |
| SYNC-LA-006 | 建立离线 decoder、波形图和分析元数据 | PENDING | 原始文件 CRC、profile、timebase、drop interval 可验证；横坐标和不连续区间明确。 |
| SYNC-LA-007 | 逻辑分析仪只读与 TDMA 无扰动 HIL | PENDING | analyzer 启停前后 TDMA cycle/CRC/drop 无观测引入回归；目标 GPIO function、direction、pull 和 FIFO consumer 不变。 |
| SYNC-LA-008 | NO5/SMA 外部波形关联 | PENDING | 本机 pad capture 与 NO5 外部 capture 使用共同时间/sequence 元数据关联，并明确两类证据不能互相替代。 |

## 既有能力收尾

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SYNC-OUT-001 | 完善预约输出 runtime 和复位路径 | PENDING | late/drop/overflow、RESET release、host 计划测试和真实边沿 HIL 完整。 |
| SYNC-MODE-001 | 完成 SEQ_STEP/ENC_COUNT/BISS_TAP 板端 self-test | PENDING | 每个 active profile mode 有独立 loopback/回放、资源冲突和安全释放证据。 |
| SYNC-PROFILE-001 | 清理失效 AUX/RJ45/BiSS 运行描述与能力暴露 | PENDING | `BOARD_SYNC_AUX_ENABLED` 等 profile 开关贯穿 validate、查询和文档；禁用能力不可 ARM。 |
| SYNC-COMP-001 | 拆分 `sync_io.c` core、capture、output、analyzer 和 AUX compatibility | PENDING | 每个模块只有一个 owner 边界；公共 header 不再混合产品 mode 与诊断 persona 实现。 |
| SYNC-VDC-001 | 将诊断 latch 升级为可验证 hardware edge latch | PENDING | source/resolution/flags/persona identity 满足 VDC 契约前始终保持 `DIAGNOSTIC_ONLY`。 |

## 当前阻塞项

- PIO0 当前已有静态 capture 与 SMA observer claim；输出 persona 也按旧固定 SM 编号初始化。
  在状态机主线完成 `SM-RES-005` 且 `SYNC-RES-001/002` 建立 descriptor 与生命周期前，不能把
  `BOARD_SYNC_PIO_WAVE` 直接改成 PIO0。
- PIO0 的 SM 和 instruction RAM 容量不支持“所有 persona 默认常驻”的假设；并发组合必须由
  descriptor 计算，而不是在文档中静态承诺。
- 逻辑分析仪只能观察 pad-visible GPIO。PIO1/PIO2 内部 PC/FIFO/IRQ 状态仍需对应 owner 发布
  snapshot，不能通过 analyzer 直接访问。
- 长时间原始等间隔采样无法直接依赖 SD 吞吐；`EDGE_TIMESTAMP` 和 drop interval 是进入长期
  DPLL/TDMA 分析前置条件。

## 统一完成定义

任务只有同时满足 HAOFV owner 边界、静态资源契约、host/pytest、release build、统一快速
硬件验收、TDMA 短帧闭环、相应 SD 原始证据和文档门禁，才可标记为 `DONE`。纯文档任务不
触发无意义 OTA，但必须通过文档检查器、文档自回归测试、pre-commit 和 staged acceptance
检查。失败必须写入 Task Progress，并保持最近已验证 persona 可恢复。
