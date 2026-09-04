# SYNC_IO / Realtime IO 架构

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/SYNC_IO_ARCHITECTURE.md`
Related: `docs/sync/SYNC_IO_TODO.md`, `docs/sync/SYNC_IO_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`
Last updated: 2026-09-04

本文档定义本机 realtime IO capability、PIO persona、逻辑分析仪、SMA 维护能力和
PIO/DMA/IRQ 执行资源之间的稳定边界。它不定义产品 Trigger 状态机、TDMA 协议、
Calibration 算法、VDC/DPLL 伺服或 SD 文件格式。

## 文档接口

| 文件 | 唯一职责 |
|---|---|
| `SYNC_IO_ARCHITECTURE.md` | 稳定语义、owner、不变量、persona 模型、跨域边界和验证映射。 |
| `SYNC_IO_TODO.md` | 当前里程碑、稳定 task ID、状态和退出门禁。 |
| `SYNC_IO_TASK_PROGRESS.md` | 提交、构建、HIL、失败、回退和证据路径。 |

Architecture 不记录单次 build 或串口结果；TODO 不复制实施日志；Task Progress 不改变
本文件的契约。旧 SYNC_IO 规划文件仅作为历史输入，不再是当前事实源。

## 范围与边界

SYNC_IO 是 HAOFV 的 Hardware Service / Realtime IO 基础域，回答：

```text
本机 IO capability 如何被声明、仲裁、装载、执行、观测和安全释放。
```

调用链固定为：

```text
SCPI / UI / System Pack intent
  -> Trigger / Sync / Calibration AO or owner FB
  -> realtime capability request
  -> SYNC_IO persona owner
  -> PIO / DMA / IRQ / GPIO hardware execution
  -> bounded runtime snapshot / capture evidence
```

| 层级 | owner | 允许 | 禁止 |
|---|---|---|---|
| 产品动作域 | TriggerAO / SyncAO / CalibrationAO | 表达启动、停止、配置和业务门禁。 | 直接改 PIO、DMA、IRQ 或 GPIO。 |
| SYNC_IO capability 域 | SYNC_IO realtime owner | 校验 capability、仲裁 persona、装载硬件、发布 snapshot。 | 解释 TDMA payload、运行 DPLL、写 SD 或改变产品 RUN 状态。 |
| persona driver | 获准的 persona | 有界 arm/disarm/service，使用已声明资源。 | 重复申请 owner、临时借用未声明 FIFO/DMA、改变其他 persona 引脚。 |
| PIO/DMA/IRQ | hardware execution | 捕获、时间戳、倒计时、输出边沿和最小事实搬运。 | 业务状态机、FatFs、USB、日志格式化和动态内存。 |
| Core0 maintenance | Core0 AO/tool | 提交意图、读取 snapshot、停止后落盘和离线分析。 | 进入 Core1 拍级路径或抢读业务 FIFO。 |

## SYNC-OWNER-001：唯一 Owner 与不变量

- `SYNC_IO realtime owner` 是 SYNC_IO/SMA PIO 的唯一运行时 owner。Trigger、Calibration、
  VDC、TDMA 验收和工具都是 capability 请求方，不直接持有 PIO block。
- 每个 persona 必须声明 PIO block、SM、instruction words、GPIO read/write mask、FIFO
  direction、DMA/DREQ、IRQ、共享 SRAM 和停止安全态。
- PIO block 只表达资源归属，不表达统一输入或输出方向；方向属于 persona 内的具体 SM。
- 同一 RX FIFO 只能有一个业务消费者。观测、诊断和 SD 路径不得成为第二个消费者。
- Core1 是硬实时唯一 writer；Core0 只提交命令和读取有界 snapshot，不在查询时触发硬件动作。
- 质量异常和数据丢失必须留下计数与原始证据；资源冲突、非法 GPIO 驱动和 FIFO 双消费者
  属于结构错误，必须阻止该 persona ARM，不能用调试门禁绕过。

## 能力、Mode 与 Persona

三个概念不得混用：

| 概念 | 含义 | 示例 |
|---|---|---|
| capability | 上层可请求的本机能力，不绑定固定 SM。 | 输入捕获、预约输出、逻辑分析、SMA 测量。 |
| mode | 产品或维护配置的稳定操作模式。 | `SEQ_STEP`、`ENC_COUNT`、`BISS_TAP`。 |
| persona | 某个运行 epoch 内装入硬件的资源与程序组合。 | capture、wave output、logic analyzer、SMA calibration。 |

目标 persona 目录如下：

| Persona | GPIO 行为 | 数据路径 | 典型请求方 |
|---|---|---|---|
| `INPUT_CAPTURE` | 只读指定语义输入。 | PIO RX FIFO -> DMA/ring -> snapshot。 | Trigger、维护、自检。 |
| `WAVE_OUTPUT` | 驱动已获准的语义输出。 | plan/command -> TX FIFO/DMA -> PIO output。 | Trigger、模型动作。 |
| `SCHEDULED_TRIGGER` | 按硬件时间基输出预约边沿。 | bounded plan -> DMA -> PIO deadline。 | Trigger、DPLL 外部证据。 |
| `LOGIC_ANALYZER` | 只读被观察 GPIO pad，不接管引脚。 | PIO RX FIFO -> capture DMA -> SRAM ring。 | 硬件验收、TDMA/DPLL 调试。 |
| `SMA_MAINTENANCE` | 按角色驱动或采样 SMA。 | persona-private FIFO/DMA。 | 线序、频率、环回测试。 |
| `SMA_CALIBRATION` | 按校准阶段驱动、预约或捕获。 | persona-private FIFO/DMA -> raw evidence。 | Calibration owner。 |

`WAVE_OUTPUT` 是输出 persona；`LOGIC_ANALYZER` 是只读观测 persona。不得再用不带限定词的
“wave persona”同时表示两者。

## ARCH-PIOPARTITION-01：PIO 资源分区

稳定分区以 `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md` 的
`ARCH-PIOPARTITION-01` 和 board profile 符号为事实源：

| 资源域 | 稳定职责 | 唯一 owner |
|---|---|---|
| Realtime Observation / SYNC_IO/SMA PIO | 语义 IO、SMA、预约触发、校准捕获和逻辑分析仪 persona。 | SYNC_IO realtime owner。 |
| TDMA TX PIO | combined CLK+SYNC control、返回 DATA capture 和 TDMA evidence。 | TDMA Foundation/Core1 owner。 |
| TDMA RX PIO | DATA output/flight/overlay 和 follower evidence。 | TDMA Foundation/Core1 owner。 |

PIO 实例由 `BOARD_TDMA_SMA_PIO_BLOCK_ID`、`BOARD_TDMA_TX_PIO_BLOCK_ID` 和
`BOARD_TDMA_RX_PIO_BLOCK_ID` 决定，不在本文复制编号。SYNC_IO persona 不得因为需要观测
TDMA 而申请 TDMA TX/RX PIO block；它只在自己的 PIO 上读取 pad-visible GPIO。

该分区不承诺 PIO0 内所有能力同时常驻。persona manager 必须按实际 SM、instruction RAM、
DMA/DREQ、IRQ、GPIO 和共享 SRAM 建立兼容矩阵，只有全部资源不相交的组合才允许并发。

## Persona 生命周期与兼容矩阵

所有 persona 使用同一生命周期：

```text
STOPPED
  -> validate descriptor and profile identity
  -> claim PIO/SM/DMA/GPIO/IRQ/DREQ/workspace
  -> load program and clear private FIFO
  -> ARMED -> RUNNING
  -> bounded stop/drain
  -> restore safe state and release
  -> STOPPED
```

| 组合 | 默认关系 | 规则 |
|---|---|---|
| `LOGIC_ANALYZER` + TDMA flight | 可并发 | 逻辑分析仪只占 SYNC_IO PIO 和自己的 RX DMA，不改 TDMA GPIO function，不读 TDMA FIFO。 |
| `LOGIC_ANALYZER` + `WAVE_OUTPUT` | 条件并发 | 必须证明 SM、instruction、DMA、workspace 不冲突；输出 GPIO 仍由输出 persona 独占。 |
| `LOGIC_ANALYZER` + `SMA_CALIBRATION` | 默认互斥 | 校准可能需要多个 SM、DMA 和相同 SRAM workspace；未声明兼容前按整 persona 切换。 |
| `INPUT_CAPTURE` + `LOGIC_ANALYZER` | 合并或互斥 | 通用 capture 应作为逻辑分析仪 backend，不得初始化第二个采样器竞争同一资源。 |
| `WAVE_OUTPUT` + `SMA_CALIBRATION` | 默认互斥 | 任一相同输出 pin、SM 或 DMA 重叠都必须拒绝后申请者。 |

persona 切换必须在 quiesced boundary 完成。失败时恢复旧 persona 或保持 `STOPPED`，同时发布
holder、requester、resource mask、failure stage 和 generation；不得留下部分 claim。

## ARCH-IOANALYZER-01：独立逻辑分析仪 Persona

### 观测范围

`LOGIC_ANALYZER` 用于监测本机 pad-visible IO。SYNC_IO PIO 可以读取 TDMA TX/RX PIO
正在使用的 GPIO 电平，因此能对 TDMA control、DATA 和同步边沿做旁路观测，但它不能直接
读取其他 PIO block 的 SM PC、ISR、OSR、X/Y 或私有 FIFO 内容。

需要内部执行状态时，由对应 PIO owner 发布只读 runtime snapshot，再通过共同硬件时间基、
persona generation 和 capture sequence 与逻辑分析波形关联。逻辑分析仪不得通过弹出目标
RX FIFO、清 IRQ 或暂停目标 SM 来取得状态。

### 只读引脚契约

- 被观察 GPIO 不进入 analyzer 的 write mask。
- analyzer 不调用会把被观察 GPIO 切换到自身 PIO function 的初始化路径。
- analyzer 不修改被观察 GPIO 的 function、direction、pull、drive strength、slew rate 或输出值。
- PIO 程序只允许读引脚、等待条件、移位到 RX FIFO 和维护私有计数；不得对被观察引脚执行
  `set pins`、`out pins`、side-set 输出或方向切换。
- analyzer 的 stop、overflow、DMA fault 和查询都不得改变被测 persona 的运行状态。

### 采集模式

| 模式 | 用途 | 数据策略 |
|---|---|---|
| `RAW_SAMPLE` | 短窗口 bit 级时序和线序分析。 | 固定采样周期，按 source mask 打包；达到容量后停止或覆盖并计数。 |
| `EDGE_TIMESTAMP` | 长时间 TDMA/DPLL 趋势观测。 | 只记录电平变化、edge mask 和 hardware tick，优先用于持续落盘。 |
| `TRIGGERED_CAPTURE` | 故障前后窗口。 | level/edge/pattern 触发，保留 bounded pre-trigger 与 post-trigger 数据。 |

高采样率 `RAW_SAMPLE` 不得直接形成无界 SD 流。长期观测使用 `EDGE_TIMESTAMP`、窗口化采集
或显式抽取；SRAM ring 发生覆盖时必须记录 dropped records 和不连续区间。

### 数据与 Snapshot

每个 capture epoch 至少关联以下事实，字段的具体类型与容量由后续代码符号冻结：

- analyzer persona/profile generation、capture sequence 和 source mask；
- capture mode、trigger descriptor、base hardware tick 和 tick rate；
- sample period 或 edge timestamp、raw level/edge mask；
- produced/consumed/dropped/overrun 计数和 first fault；
- timestamp source、resolution、eligibility flags 和关联的 TDMA cycle/persona generation；
- 数据 CRC、结束 reason 和 capture 是否完整。

逻辑分析仪数据默认是 `DIAGNOSTIC_ONLY`。只有独立的硬件 edge latch、时间分辨率、source
identity 和完整性门禁都满足 VDC 契约时，上层才可将特定记录标记为 DPLL eligible；
逻辑分析仪自身不做该判定。

### 双核与存储路径

```text
Core0 intent/config
  -> Core1 validate and ARM
  -> SYNC_IO PIO RX FIFO
  -> dedicated capture DMA
  -> bounded SRAM active/shadow ring
  -> Core1 publish snapshot
  -> Core0 drain after boundary or from shadow
  -> StorageAO / SD raw file
  -> offline decoder / SVG / transfer-function analysis
```

Core0 可查询进度和状态，但查询只读 snapshot。FatFs、SVG、解码和曲线拟合不得进入 Core1
实时路径。外部 NO5/SMA 捕获仍用于证明真实线缆和外部相位；本机 pad 观测只能证明本机可见
电平，不能替代外部物理链路证据。

## 语义 IO 与 Board Profile

应用层只能使用语义通道，物理映射以 `sync_io_hw_profile.h` 和 board profile 为事实源。

| 通道组 | 语义 | 规则 |
|---|---|---|
| main input group | `TRIG_IN`、`GATE_IN`、`ENC_A/B/Z` 等模式输入。 | 输入位序在 profile 边界归一化。 |
| main output group | `TRIG_OUT`、`PULSE_OUT`、mode output。 | 输出必须由已获准 persona 驱动并有停止安全态。 |
| TDMA wire group | TDMA CLK、SYNC 和 DATA。 | 归 TDMA owner；逻辑分析仪仅旁路读取。 |
| legacy AUX/BiSS aliases | `ARM_IN`、`EXT_CLK_IN`、`SYNC_CLK_OUT`、BiSS。 | 只有 active board profile 显式启用时才可 ARM。 |

当前产品 board profile 通过 `BOARD_SYNC_AUX_ENABLED` 和
`BOARD_SYNC_RJ45_TRIGGER_ENABLED` 禁用与 TDMA wire group 重叠的旧 AUX/RJ45 persona。
文档不得再把这些 alias 描述为产品板上永久可用的 PIO2 能力。

## Mode Driver 契约

已实现或保留的 mode 通过 `sync_io_mode_ops_t` 暴露 `id/name/resources/hw/validate/arm/
disarm/is_running`。`hw` 必须显式记录 PIO、SM、DMA 和 IRQ；board profile 未启用所需引脚时，
mode 即使有代码也不得 ARM。

- `SEQ_STEP`：外部边沿驱动 bounded sequence 输出。
- `ENC_COUNT`：A/B/Z 计数和目标触发。
- `BISS_TAP`：BiSS 调试/通信准备 persona，仅在兼容 profile 可用。
- reserved mode：`get_ops()` 返回 NULL，不得用占位 ID 绕过能力检查。

Mode 的产品 RUN 状态由 TriggerFB 等上层 owner 管理；SYNC_IO 只管理底层执行和资源。

## Snapshot 与查询

所有外部查询必须读取本地 snapshot，不得临时跨板查询或触发 IO：

| 数据 | 发布规则 |
|---|---|
| persona lifecycle | state、generation、owner、resource mask、last transition/error。 |
| PIO/DMA | enabled、FIFO level、transfer count、stall/overflow；不得消费业务 FIFO。 |
| logic analyzer | mode、source mask、capture progress、drop/overrun、trigger/end reason。 |
| output persona | scheduled/completed/late/drop、safe-state 和 DMA progress。 |
| 分布式事实 | 由 RefMem/VDC/TDMA owner 解释；SYNC_IO 不跨节点聚合。 |

跨核多字段 snapshot 必须使用唯一 writer 加 seqlock、双缓冲或等价版本协议。高频事实进入
bounded ring/counter，不在 IRQ 或 Core1 路径输出文本日志。

## 跨域契约

| 域 | 与 SYNC_IO 的边界 |
|---|---|
| STATE_MACHINE | 冻结 PIO 分区、persona descriptor 和 claim/release 生命周期。 |
| TDMA | 独占 TDMA TX/RX PIO 和业务 FIFO；可被逻辑分析仪从 GPIO pad 旁路观测。 |
| Calibration | 计算训练/校准参数并请求 SMA persona；不成为 PIO0 hardware owner。 |
| Trigger | 管产品动作和业务门禁；请求输出/输入 capability，不直接写硬件。 |
| VDC/DPLL | 消费合格 timestamp evidence、计算共同时间和伺服；不把普通 capture 冒充锁相证据。 |
| RefMem | 发布共同事实和质量；不承载实时边沿或大波形。 |
| Storage | Core0/StorageAO 持久化停止态或 shadow capture；不进入实时采集路径。 |

## 失败与恢复

- descriptor/profile 不一致、资源冲突、非法写引脚、instruction space 不足、FIFO 双消费者
  或 DMA/DREQ 不一致时，persona 保持 `STOPPED` 并发布结构错误。
- 调试阶段的信号质量、CRC、drop、timeout 和 phase 异常应记录后继续可执行流程；它们不能
  被提升为资源安全豁免，也不能隐藏原始数据。
- analyzer overflow 只终止或降级 analyzer capture，不得停止 TDMA、Trigger 或 DPLL 实时路径。
- 输出 persona fault/stop/reset 必须先恢复安全电平，再释放 GPIO 和硬件资源。
- Flash erase/program 前必须完成 Core1 park/lockout；恢复后 persona 必须重新验证 generation，
  不从未知 PC/FIFO 状态继续运行。

## 验证映射

| 契约 | 静态/Host 验证 | 硬件验证 |
|---|---|---|
| PIO 分区 | board/profile 符号、resource mask、owner 冲突负测。 | persona 切换、失败回滚和无泄漏恢复。 |
| analyzer 只读 | PIO 指令扫描、GPIO write mask、禁止 target FIFO consumer。 | TDMA 运行时启停 analyzer，确认 cycle/CRC 不因观测变化。 |
| capture 完整性 | ring wrap、drop/overrun、CRC、trigger window 单测。 | 短时 RAW 和长时 EDGE capture，验证不连续区间可定位。 |
| 双核边界 | snapshot 并发读、Core1 禁止 FatFs/日志/动态内存。 | 串口持续查询不扰动实时路径，Core0 SD drain 不造成实时回归。 |
| 外部证据 | source/eligibility/profile generation gate。 | 本机 pad capture 与 NO5/SMA 外部波形关联。 |

任何固件、PIO、工具或测试实现变更都必须按仓库规则运行统一硬件验收；文档重构本身只运行
文档自回归门禁，不冒充硬件验收结果。

## 当前迁移边界

- 当前 `BOARD_SYNC_PIO_FAST` 已承载通用 capture 和 SMA observer；它们是逻辑分析仪 backend
  的实现输入，还不是完整 `LOGIC_ANALYZER` persona。
- 当前 `sync_io_init()` 仍直接静态 claim capture SM，`sma_cable_delay` 也以自身 owner
  直接申请 PIO0 和动态 SM/DMA；两条路径尚未收敛到统一 SYNC_IO persona manager。
- `WAVE_OUTPUT` 与 `SCHEDULED_TRIGGER` 已由 PIO0 persona descriptor 和 lifecycle manager
  装载、仲裁并运行，使用 PIO0 专用 SM、DMA/workspace 和 safe-state 语义；TDMA flight
  不再通过兼容 handoff 接管或恢复 SYNC_IO 的旧 PIO1 wave claims。
- 当前通用 capture 只覆盖 active input group，Core1 drain timestamp 仍是诊断事实；后续需要
  可配置 source mask、硬件 edge timestamp、trigger window 和独立 capture ring。
- 当前产品 profile 禁用旧 AUX/RJ45 persona，GPIO 与 PIO2 归 TDMA；旧文档中的 PIO2 AUX、
  BiSS 和 `SYNC_CLK_OUT` 常驻描述已经失效。
- 迁移必须先建立 PIO0 persona manager 和兼容矩阵，再迁移输出 persona；不得把 PIO1 的静态
  SM 编号直接复制到 PIO0，也不得假设 capture、输出、校准和 analyzer 全部并发常驻。
