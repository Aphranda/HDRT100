# RP2350_TRIG 产品化系统架构总纲

Status: Draft
Domain: ARCH
Canonical: `docs/arch/ARCH_PRODUCT_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`, `docs/trigger/TRIGGER_SYNC_TODO.md`, `docs/sync/SYNC_IO_RESOURCE_PLAN.md`, `docs/ota/OTA_SYSTEM_DESIGN.md`, `docs/storage/SD_TODO.md`, `docs/storage/LOG_SYSTEM_TODO.md`, `docs/trigger/RP2350B_FOUR_BOARD_DISTRIBUTED_TRIGGER_SCHEME.md`
Last updated: 2026-08-10

本文档把 `docs/` 中已经沉淀的框架性内容综合为面向产品的系统架构入口。各专项文档仍是细节来源；本文只定义产品边界、运行模型、数据契约、跨域依赖和发布门禁。

## 产品定位

RP2350_TRIG 是面向相控阵/多设备同步测试的分布式触发控制节点。产品化目标不是单板脉冲工具，而是一套可由同一硬件配置成 A0/A1/A2/A3 角色的四板协同系统：

| 角色 | 产品职责 |
|---|---|
| A0 | 扫描/时间主控，运行 LoopEngine、DPLL、虚拟 DC，滚动生成未来触发预约。 |
| A1 | DUT/链路近端动作节点，执行本地预约触发、门控、READY/T2 捕获。 |
| A2 | 馈源/极化/开关近端动作节点，执行本地预约触发和动作反馈。 |
| A3 | 上位机/VNA/网分近端网关，承担配置入口、状态汇总、仪表近端触发和数据面桥接。 |

核心产品原则：

- 上位机只负责配置、启动、停止、状态读取、数据采样和报告，不逐点驱动 RUN。
- 通信包到达时刻不直接产生业务触发边沿；各板必须提前装载本地预约。
- PIO/DMA/IRQ 是硬实时边沿执行层；RTOS task、SCPI、USB、SD、日志都不得直接产生精确边沿。
- Vector/Blackboard 是事实和意图的契约层，不承载大文件、波形、OTA payload 或日志文本。
- 每个状态字段必须有 owner；跨域动作只能通过事件、命令槽、mailbox 或 snapshot API。

## 总体架构

产品架构采用 HAOFV：

```text
Host / UI / SCPI / System Pack / Boot Result
        |
        v
Active Object Layer
  SystemAO / TriggerAO / OtaAO / StorageAO / DiagnosticsAO / UiAO
        |
        v
Function Block Layer
  table-driven ECC, resource arbitration, state transition, safety rules
        |
        v
Vector Blackboard Layer
  SystemVector / TriggerVector / OtaVector / StorageVector
  DistributedVectorTable / command slots / status rings
        |
        v
Hardware Service Layer
  sync_io / drv_flash / sd_card / usb / lcd / watchdog
        |
        v
Hard Real-Time Side Path
  PIO / DMA / IRQ
```

分层含义：

| 层 | 负责 | 禁止 |
|---|---|---|
| Active Object | 事件队列、生命周期、预算、对外 API。 | 暴露内部状态指针或绕过 owner 写 Vector。 |
| Function Block | ECC 状态迁移、规则表、资源互锁、错误归因。 | 直接做硬实时边沿或长期阻塞。 |
| Vector Blackboard | 当前事实、配置快照、命令槽、进度、错误摘要。 | 保存大块数据、日志文本、波形、OTA 数据块。 |
| Hardware Service | 封装 MCU/外设访问。 | 被上层跨层直接调用或绕过资源仲裁。 |
| PIO/DMA/IRQ | 采样、倒计时、捕获、输出脉冲。 | 访问 SD/UI/USB/SCPI/日志或复杂动态协议。 |

## 双核运行模型

产品化固件采用 RTOS + 双核 AMP，而不是 SMP。

```text
core0 control plane
  FreeRTOS
  USB / SCPI / UI
  OTA / Flash / Storage
  ConfigGate / LoopEngine / DPLL
  task_refmem_sync / Diagnostics
        |
        | command queue / mailbox / vector snapshot
        v
shared contract region
  Trigger command slots
  Trigger status ring
  DistributedVectorTable summaries
  heartbeat / fault / resource snapshot
        ^
        | execution facts / T2 / READY / late / counters
        |
core1 realtime plane
  TriggerAO / TriggerFB fast path
  PIO/DMA arm/load/capture
  local_fire queue service
  flash lockout poll
```

区域隔离目标：

| 区域 | owner | 内容 |
|---|---|---|
| core0 control region | core0 | FreeRTOS task、USB/SCPI、OTA、Storage、UI、日志、配置门禁。 |
| core1 realtime region | core1 | 实时入口、Trigger 快路径、PIO/DMA 装载、core1 私有状态。 |
| shared vector region | core0 + core1 | 命令槽、状态 ring、DistributedVectorTable 摘要、heartbeat、fault evidence。 |

## 产品执行体

产品层面继续采用 AO/FB 划分，但每个执行体都必须有明确 owner 和输入输出边界。

### Active Object

| AO | owner | 输入 | 输出 |
|---|---|---|---|
| SystemAO | core0 | 模式事件、故障事件、资源请求 | 系统模式、资源锁、发布门禁摘要 |
| TriggerAO | core1 | ARM/FIRE_LOAD/STOP/FAULT、配置快照 | TriggerVector、PIO 装载、READY/T2/late 摘要 |
| OtaAO | core0 | OTA begin/apply/commit/rollback 事件 | OtaVector、metadata 更新、flash job |
| StorageAO | core0 | snapshot/trace/report/pack 任务 | StorageVector、SD 文件、catalog 结果 |
| DiagnosticsAO | core0 | fault/heartbeat/统计事件 | fault bundle、log tail、告警摘要 |
| UiAO | core0 | 按键、页面、告警确认 | 屏幕状态、告警反馈、操作事件 |

### Function Block

| FB | owner | 作用 |
|---|---|---|
| TriggerFB | core1 | ECC 状态转移、本地预约、capture/late/fault 判定。 |
| OtaFB | core0 | OTA 事务状态机、metadata/slot 一致性、回滚判定。 |
| StorageFB | core0 | pack checkout、snapshot/trace/report 分片作业。 |
| SafetyFB | core0 | 资源互锁、模式门禁、故障闩锁、恢复条件。 |
| DiagnosticsFB | core0 | 证据归档、统计摘要、错误归因。 |

执行规则：

- AO 负责事件驱动与生命周期，FB 负责规则和状态迁移。
- AO/FB 只能通过 Vector snapshot 交换事实，不能直接共享内部私有结构。
- core1 的 AO/FB 只保留实时相关子集，不能引入 SD、USB、SCPI、OTA payload 或阻塞日志。

### Core0/Core1 隔离向量表

双核隔离需要独立的 CPU 中断向量表。这里的“隔离向量表”不是 Domain Vector，也不是
DistributedVectorTable，而是 core0/core1 各自的 exception/IRQ 入口表和 IRQ owner
分配表。

```text
core0 VTOR -> core0_vector_table
  USB / SCPI service tick / Storage / OTA maintenance / UI / diagnostics IRQ

core1 VTOR -> core1_vector_table
  PIO trigger / DMA trigger / capture / local_fire / realtime timer IRQ
```

设计目标：

| 机制 | 产品作用 | 边界 |
|---|---|---|
| core0 vector table | 控制面和维护面 IRQ 入口。 | 不承载 PIO/DMA 触发快路径。 |
| core1 vector table | 实时触发 IRQ 入口，尽量放 RAM-resident section。 | 不承载 USB、SCPI、FatFs、OTA、LCD。 |
| IRQ owner map | 固定每个 IRQ 的唯一归属核和允许 API。 | 不能运行时随意迁移 owner。 |
| runtime protection fields | RAM-resident section / flash lockout / park / entry table owner | 作为表头或 slot 元素记录当前隔离态和切换态。 |
| cross-core doorbell | 只做唤醒和轻量通知。 | 业务 payload 仍走 command queue/status ring。 |

隔离向量表能把两类冲突的入口分开：

- 避免 core0 的 USB/SD/OTA/日志中断抢占 core1 实时路径。
- 避免 core1 的 PIO/DMA/capture 中断进入 core0 的 RTOS/SCPI/Storage 上下文。

它是下面两类问题生效的前提，但不是单独完成闭环的全部机制：

- 共享 RAM 结构的并发一致性，通常直接把 owner、sequence、CRC、seqlock、stale 等做成表头或表元素。
- core1 在 core0 flash erase/program 期间的安全运行，仍需要把 RAM-resident section、flash lockout/park 和明确的入口表归属写入同一张表的头部或 slot 元素。
- Flash、SD、USB、PIO、DMA 仍必须有唯一 owner 和模式门禁。

必须遵守：

- core0 只能通过 command queue/mailbox 投递触发意图，不能直接改 core1 私有状态。
- core1 只能写执行事实和最小状态，不执行 FatFs、USB、SCPI、OTA flash job、LCD、阻塞日志或动态内存。
- flash erase/program 前，core0 必须让 core1 park/lockout；core1 入口、lockout poll 和关键快路径应逐步迁移到 RAM-resident section。
- core0/core1 必须使用明确的 VTOR/vector table 和 IRQ owner map；core1 vector table 只承载 realtime IRQ。
- 独立 RAM vector table/VTOR 可作为中断入口隔离，但不能替代 flash lockout、资源 owner 和共享区协议。

## Vector 体系

本项目存在三类“向量表”，必须区分：

| 类型 | 范围 | 作用 |
|---|---|---|
| CPU interrupt vector table | 单核异常入口 | core0/core1 各自的异常/IRQ 入口隔离，不是业务数据面。 |
| Domain Vector | 单板单域 | `TriggerVector`、`OtaVector`、`StorageVector` 等，保存本域事实和摘要。 |
| DistributedVectorTable | 四板系统级 | 模拟反射内存，保存多节点共同认知、ACK、stale、fault 和摘要。 |

Vector 基本规则：

- 命令是意图，IO/触发镜像是事实，状态是推导结果。
- 查询只读 snapshot，不临时访问现场硬件。
- Vector 字段必须有唯一写入者；跨域更新通过 API、事件或合并任务。
- 大块数据不进入 Vector，只保存 id、hash、size、CRC、进度和错误摘要。
- 需要 sequence、CRC、owner、stale 标志或 seqlock 作为表元素，避免半新半旧。

## 表驱动边界

产品架构不是“全局一张大表”，而是把可枚举、可验证、可切换的规则收敛成表，把动态执行留给 AO/FB 和硬实时路径。

适合表驱动的产品对象：

| 对象 | 表 | 说明 |
|---|---|---|
| 系统模式 | `SystemModeTable` | BOOT/IDLE/CONFIG/ARMED/RUN/HOLDOVER/MAINTENANCE/OTA/FAULT 及其允许迁移。 |
| 核心隔离 | `CoreVectorOwnerTable` | core0/core1 VTOR、IRQ owner、RAM-resident 入口、park/lockout 状态。 |
| 资源仲裁 | `ResourceArbiterTable` | Flash/SD/USB/PIO/DMA/LCD 的唯一 owner、互斥规则、超时和恢复动作。 |
| 触发动作 | `TriggerActionTable` | 模式、动作、预装载、FIRE_LOAD、local_fire、capture、late/fault 规则。 |
| 分布式同步 | `DistributedVectorTable` / `RJ45_SYNC_RING` | 节点事实镜像、delta 同步、ACK/NACK、stale、fault。 |
| OTA 事务 | `OtaStateTable` | 选择、校验、搬运、commit、rollback、metadata 双副本。 |
| 存储作业 | `StorageJobTable` | pack checkout、snapshot、trace、report、catalog、cleanup。 |
| 诊断归因 | `FaultCodeTable` | 错误码、证据类型、锁存规则、清除条件。 |

不适合单纯表驱动的对象：

- PIO 边沿输出。
- DMA 搬运。
- IRQ 捕获。
- 极短倒计时和实时装载。
- 依赖物理时序的硬实时窗口。

这些路径可以读表决定参数，但不能把边沿执行本身做成高层规则表再层层解释。

### 产品状态机表

系统模式不是随意切换，必须由状态表裁剪：

| 当前态 | 允许事件 | 下一个态 | 备注 |
|---|---|---|---|
| BOOT | init ok | IDLE | 完成自检、载入默认快照。 |
| IDLE | CONFIG, OTA, ARM | CONFIG/OTA/ARMED | 取决于门禁和资源状态。 |
| CONFIG | SAVE, ARM, OTA, FAULT | IDLE/ARMED/OTA/FAULT | 只改配置，不进硬实时。 |
| ARMED | START, STOP, FAULT | RUN/IDLE/FAULT | 冻结快照。 |
| RUN | STOP, FAULT, HOLDOVER | IDLE/FAULT/HOLDOVER | 不接受逐点配置变更。 |
| HOLDOVER | RELOCK, STOP, FAULT | RUN/IDLE/FAULT | 同步异常时的保守态。 |
| MAINTENANCE | OTA, FLASH, SAVE, EXIT | IDLE/OTA/FAULT | 维护和恢复专用。 |
| OTA | APPLY, COMMIT, ROLLBACK, FAIL | IDLE/FAULT | 独立维护流程。 |
| FAULT | CLEAR, SAVE, RECOVER | IDLE/MAINTENANCE | 先保全证据，再恢复。 |

### Domain Vector

| Vector | owner | 产品用途 |
|---|---|---|
| SystemVector | `task_system` / SystemAO | 系统模式、资源锁、全局故障、发布门禁摘要。 |
| TriggerVector | core1 `TriggerAO/TriggerFB` | PIO 状态、ARM、模式、seq、T2、READY、late、错误码。 |
| OtaVector | `task_ota` / OtaAO | OTA 状态、slot、进度、错误和 boot handoff 摘要。 |
| StorageVector | `task_storage` / StorageAO | SD 状态、job、pack/ref、snapshot/trace/report 摘要。 |
| UiVector | `task_ui` | 页面、按键、告警确认和显示状态。 |

### DistributedVectorTable

DistributedVectorTable 是四板系统的本地同构事实镜像，目标固定 64 KB，P0 可只启用核心 slot。

```text
Header/Directory
SystemSlot
Role/ConfigSlot
VdcSlot
LoopSlot
DpllSlot
NodeSlot[8]
TriggerSlot[8]
IoSlot[8]
CalibrationSlot
StatisticsSlot
AckCommandSlot
FaultEvidenceSlot
GatewaySlot
OtaStorageUiSlot
TlvExtension
```

它回答“系统当前共同认知是什么”，不负责产生边沿。

```text
DistributedVectorTable -> 状态认知、配置摘要、ACK、stale、fault、查询
FIRE_LOAD command      -> 未来预约触发装载
PIO local_fire         -> 真实边沿
```

RJ45_SYNC_RING 只同步变更 slot 的小 delta，不同步整表大块数据。

## 四板运行数据流

RUN 典型路径：

```text
Host / PC
  -> A3 配置入口
  -> A0 LoopEngine / ConfigGate / DPLL
  -> 生成 T_fire_base
  -> 按 NodeRoleMap / ActionMap / Calibration 生成 T_fire_i
  -> RJ45_SYNC_RING 下发 FIRE_LOAD / 状态帧
  -> 各板 core1 装载 local_fire
  -> PIO 到点输出 SMA/RJ45 近端边沿
  -> PIO 捕获 READY/T2
  -> core1 status ring
  -> core0 合并 TriggerSlot / FaultEvidence
  -> A0/A3/Storage/Diagnostics 发布 snapshot、trace、report
```

RUN 态规则：

- 已装载的触发边沿不受 USB、SCPI、SD、UI 抖动影响。
- A1/A2/A3 不能等待前一块板 DONE 后再实时动作；DONE/READY/T2 只用于闭环确认、异常剔除和推进下一窗口。
- late 的预约不补发；记录 late/fault evidence，按策略 HOLDOVER 或 FAULT。
- 上位机断连默认不终止已启动扫描，除非产品互锁配置要求通信丢失停机。

## 时间与同步模型

每块板维护本地 tick，控制面维护虚拟 DC。顺序上，本地晶振和 `local_tick`
上电后先存在；BiSS-C/RJ45 同步帧提供跨节点边沿观测；DPLL 再根据本地时间戳、
seq、CRC、链路 delay 和节点 age 估计 `offset/rate`，收敛后发布可用于运行门禁的
虚拟 DC。DPLL 不直接产生物理时钟，也不替代各板晶振：

```text
local oscillator -> local_tick
BiSS-C/RJ45 sync edge -> timestamp by local_tick
DPLL estimate -> rate_q32 + offset_tick
dc_tick = local_tick * rate_q32 + offset_tick
T_fire_i = T_fire_base + delta_t_i
```

状态顺序固定为：

```text
FREE_RUN  -> 只有本地 tick，可做本机维护和安全输出
OBSERVED  -> 已收到有效同步帧，但 offset/rate 尚未稳定
LOCKING   -> DPLL 正在收敛，统计 e_vdc
LOCKED    -> 虚拟 DC 可作为 FIRE_LOAD/T2/e_act 的正式时间基准
HOLDOVER  -> 短时失去观测，按策略只允许已装载队列完成
```

职责分层：

| 层 | 职责 |
|---|---|
| CPU / DPLL | 64-bit 时间轴、offset/rate、预约队列、残差过滤、补偿表。 |
| PIO | 短窗口倒计时、边沿捕获、固定延迟转发、到点输出。 |
| Storage/Diagnostics | 保存校准、T2 分布、late/CRC/ready_timeout 证据。 |

RP2350 原型目标应按百 ns 级分布式动作一致性定义；若产品要求稳定 <10 ns 或亚 ns TDC，应升级到 FPGA/TDC/EtherCAT DC 等硬件时间戳方案。

## 配置与 System Pack

配置体系分三层：

| 层 | 内容 | 约束 |
|---|---|---|
| 固件默认 | build_id、hw_profile、默认 role/config | 只用于 blank/factory bring-up。 |
| RAM 快照 | NodeRoleMap、LoopPlan、ActionMap、Calibration | ARM/RUN 使用的权威运行副本。 |
| SD System Pack | profile、mission、cal、update、manifest/ref | 可追溯、可回滚、可审计。 |

System Pack 借鉴 Git 思想但不在固件里实现 Git：

- `pack` 是不可变配置集合。
- `ref` 指向 active/previous/factory/candidate。
- checkout 只允许 BOOT/IDLE/MAINTENANCE 或 ARM 前。
- checkout 成功后通过事件进入 Trigger/Config 快照。
- RUN 中不得切换 ref 或直接修改动作表。

每次 RUN 必须冻结并记录：

```text
epoch / run_id
build_id
hw_profile
NodeRoleMap CRC
LoopPlan CRC
ActionMap CRC
Calibration CRC
target_mask / ack_flags / nack_flags / busy_flags / timeout_flags
```

## 维护域

### OTA

OTA 是维护域，不是实时域。

```text
SCPI / UI / SD
  -> OtaAO
  -> OtaFB
     -> FlashJobFB / MetadataFB / ImageVerifyFB / BootHandoffFB
  -> OtaVector
  -> drv_flash / bootloader
```

产品规则：

- SCPI 只投递 OTA 事件或读取快照，不直接擦写 flash。
- OTA 只允许在 MAINTENANCE/OTA 或已明确停止实时路径时执行。
- metadata commit 是 flash 写操作，必须协调 core1、DMA、USB、SPI 和中断活动。
- Bootloader 只做启动选择、镜像校验、搬运/跳转/回滚；不集成 SD/FatFs/UI/SCPI。

### SD / StorageAO

SD 是 System Pack 与持久化证据介质，不进入硬实时路径。

数据分级：

| 等级 | 内容 | 存储 |
|---|---|---|
| L0 当前事实 | Vector / 反射内存 | RAM 摘要 |
| L1 最近现场 | RAM trace ring | RAM ring |
| L2 证据文件 | snapshot / trace / report | SD |
| L3 系统包 | pack / profile / mission / cal / update | SD |
| L4 工具产物 | manifest / ref / factory / logs | SD |

规则：

- ARM 后硬实时阶段不得等待 SD/FatFs。
- SCPI/UI/SD 文件输入只能投事件，不能直接修改 Trigger/Ota/Storage 域状态。
- 所有读写 job 分片执行；SCPI 回调内不得同步 mount、枚举大目录或读大文件。
- 证据文件必须用 `.tmp`、索引和完整性标志，避免掉电后误用半文件。

### LOG / Diagnostics

日志是观测面，不是控制面。

- PIO/DMA/IRQ 热路径不得 `printf`、`LOG_*`、FatFs、StorageAO job 或阻塞 trace。
- 高频事件只锁存计数和状态；详细证据在 DISARM/FAULT 后补齐。
- USB CDC 与 SCPI 共通道时，OTA binary block 和产测自动化必须压低周期文本日志。
- 产品化故障包应包含 snapshot + trace + report + log tail，并带 schema/version/index。

## IO 与协议扩展

主触发 IO 的产品 pinout 必须稳定：

| 物理组 | GPIO | 产品用途 |
|---|---|---|
| IN0..IN3 | 16..19 | 主触发、gate、编码器、RJ45 前向输入等模式本地高速输入。 |
| OUT0..OUT3 | 20..23 | 主触发、脉冲、SEQ_STEP、RJ45 前向输出等模式本地高速输出。 |
| AUX0..AUX3 | 26..29 | ARM_IN、EXT_CLK_IN、SYNC_CLK_OUT、协议辅助输出。 |

协议扩展必须纳入 Trigger 域。例如 BiSS-C：

```text
SCPI / UI / SD profile
  -> TriggerAO
  -> TriggerFB ECC
  -> TriggerVector / BiSS profile
  -> sync_io / biss_node_io
  -> PIO / DMA / IRQ hard real-time path
```

慢速配置、自校准和诊断属于 `SLOW_CTRL_SYNC`；正式测试属于 `FAST_RT_TEST`。ARM 后正式路径不得经过 SCPI/UI/SD/普通任务。

## 系统模式

产品系统模式建议统一为：

```text
BOOT
IDLE
CONFIG
ARMED
RUN
HOLDOVER
MAINTENANCE
OTA
FAULT
```

关键约束：

- `CONFIG` 可修改角色、pack、LoopPlan、ActionMap、Calibration。
- `ARMED` 冻结运行快照，允许最后一致性检查。
- `RUN` 只接受白名单查询、STOP/FAULT clear 等安全命令，不接受逐点推进或配置变更。
- `MAINTENANCE/OTA` 可执行 flash、SD 大 job、升级、产测和恢复动作。
- `FAULT` 锁存安全默认态，优先保存证据，再等待人工或上位机恢复策略。

## 发布门禁

产品 release 不能只依赖裸机单板 smoke。最低门禁应覆盖：

| 领域 | 发布门禁 |
|---|---|
| 双核 | `SYST:CORE?` 两核 heartbeat 持续增长；flash 写入期间 core1 不死锁。 |
| Trigger | `ARM -> FIRE_LOAD -> local_fire -> READY/T2 -> DISARM` 闭环；late 不补发。 |
| 分布式 | 四板 slot owner、heartbeat、stale、ACK/NACK、CRC 和 fault evidence 稳定。 |
| 配置 | RUN 前 build/hw/profile/role/loop/action/cal CRC 一致性检查。 |
| OTA | online package、boot apply、commit、rollback/failure path、metadata 双副本。 |
| Storage | pack checkout、snapshot/trace/report、掉电半文件恢复、容量保护。 |
| 诊断 | fault bundle 可复盘；日志不干扰 SCPI/OTA/实时路径。 |
| 长稳 | SD/OTA/UI 并发压力下实时核无阻塞；24h heartbeat、heap/stack、水位稳定。 |

## 落地路线

推荐按以下顺序冻结产品架构：

1. 固化 RTOS + 双核 AMP：core0 控制面、core1 实时面、shared vector/ring。
2. 完成 TriggerVector snapshot、command queue、status ring 和 owner 写权限。
3. 冻结 DistributedVectorTable 64 KB layout、slot directory、CRC、stale 和 owner。
4. 完成 ConfigGate：NodeRoleMap、LoopPlan、ActionMap、Calibration、ACK/NACK 原因码。
5. 接入 RJ45_SYNC_RING delta，同步 NodeSlot/TriggerSlot/IoSlot/ACK/Fault。
6. 实现 FIRE_LOAD 到 core1 `local_fire`，闭环 READY/T2/late/fault evidence。
7. 接入 DPLL/虚拟 DC、校准和窗口化扫描程序。
8. 完成 SD System Pack、snapshot/trace/report 和 OTA 离线包。
9. 建立 HIL 发布矩阵和长稳证据包。

## 文档关系

| 主题 | 细节来源 |
|---|---|
| HAOFV 顶层分层 | `docs/arch/HAOFV_ARCHITECTURE.md` |
| RTOS + 双核 AMP + DistributedVectorTable | `docs/arch/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` |
| 触发域待办与当前基线 | `docs/trigger/TRIGGER_SYNC_TODO.md` |
| PIO/GPIO/AUX 资源约束 | `docs/sync/SYNC_IO_RESOURCE_PLAN.md` |
| 四板业务拓扑 | `docs/trigger/RP2350B_FOUR_BOARD_DISTRIBUTED_TRIGGER_SCHEME.md` |
| DPLL/虚拟 DC | `docs/sync/SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` |
| OTA/Bootloader | `docs/ota/OTA_SYSTEM_DESIGN.md` |
| SD/System Pack | `docs/storage/SD_TODO.md` |
| 日志与证据 | `docs/storage/LOG_SYSTEM_TODO.md` |
| BiSS-C 协议扩展 | `docs/communication/BISSC_TAP_BRIDGE_DESIGN.md` |
| PinProbe A1 历史 RamVector | `docs/LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md` |
