# RTOS 分布式触发任务划分与待办

Status: Draft
Domain: RTOS
Canonical: `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`
Related: `docs/RTOS_PORTING_PLAN.md`, `docs/MULTICORE_PARTITION_PLAN.md`, `docs/TRIGGER_SYNC_TODO.md`, `docs/相控阵测试系统RP分布式触发方案技术报告0804.md`, `docs/RTOS_DISTRIBUTED_TRIGGER_0804_REPORT.html`, `docs/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`, `docs/LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md`, `docs/RP2350B_FOUR_BOARD_DISTRIBUTED_TRIGGER_SCHEME.md`
Last updated: 2026-08-12

本文档把《RP1200波导天线 RP 分布式触发方案技术报告 0804》落成 RP2350_TRIG
产品化 RTOS + 双核 AMP 任务划分。目标不是把现有函数平均分到多个任务，而是把
四板分布式触发系统中的控制面、实时控制面、数据面和诊断面固定成可实现边界。

## 设计输入

完整原始资料已经从外部 `DOC/` 目录补入仓库，克隆后不再依赖 OneDrive 本地路径：

- `docs/RTOS_DISTRIBUTED_TRIGGER_0804_REPORT.html`：0804 RP 分布式触发完整报告。
- `docs/RTOS_DISTRIBUTED_TRIGGER_0614_REPORT.html`：0614 分布式触发完整报告。
- `docs/LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md`：PinProbe A1 RAM 反射内存与多机协同历史方案。
- `docs/LEGACY_PINPROBEA1_OTA_CAN_DISTRIBUTION.md`：PinProbe A1 OTA 与 CAN 多机分发历史方案。

0804 报告给出的系统边界如下：

- 四块统一 RP2350B 节点板通过角色配置成为 A0/A1/A2/A3。
- A0 是扫描/转台/DPLL/ring origin，下位机生成 `T_fire_base`。
- A1 是 DUT/链路/SP8T 近端触发节点。
- A2 是馈源/极化/开关近端触发节点。
- A3 是上位机网关/VNA/网分近端节点，承担 USB/USBTMC/SCPI 接入。
- A3 是唯一外部 COM 入口；A0/A1/A2/A4 作为内部 BiSSC 组网节点，不再按“每块板一个 COM”建模。
- `RJ45_SYNC_RING` 传递 SYNC、FIRE_LOAD、DONE、MEAS_DONE、FAULT 和状态，不串行传递业务触发边沿。
- 各板提前装载本地预约，PIO 到点输出 SMA 边沿。
- PIO/DMA/IRQ 负责硬实时边沿、短窗口倒计时、捕获和时间戳；RTOS task 不直接产生精确边沿。
- RUN 态由四板硬件内部自循环；上位机只做配置、启动、停止、状态监控、数据读取和升级。

PinProbe A1 报告给出的可复用软件边界如下：

- RamVector 是唯一共享数据面，SCPI、状态机和执行层围绕同一份结构化表协作。
- 命令是意图，IO/硬件镜像是事实，状态是基于事实推导出的结论。
- 状态查询读取快照，不临时触发现场 IO 读取。
- 命令槽需要原子 Take/Clear，执行动作保持在临界区外。
- IO/状态镜像需要整帧快照，避免字段半新半旧。
- 诊断 AppLog 与实时 RamVector 分离，实时表回答“现在是什么”，日志回答“为什么变成这样”。

## 总体模型

产品化运行模型为 core0 FreeRTOS 控制核 + core1 实时核：

```text
Host / VNA / A3 USB
    ↓
task_usb_device
    ↓
task_scpi ───────┐
                 ↓
task_gateway_a3 -> task_loop_engine -> trigger_command_queue -> core1_realtime
                         ↑                    ↓
task_dpll ---------------┘              trigger_status_ring
task_calibration --------┐
task_vdc_sync -----------┴--------------┘
task_refmem_sync <----------------------┘
                         ↓
task_storage / task_ui / task_ota / task_diag
```

核心规则：

- `task_usb_device` 只服务 USB 设备栈，不解析业务。
- `task_scpi` 只解析命令、投递事件、查询快照，不推进扫描循环。
- `task_loop_engine` 承载 A0 下位机编排，不依赖上位机逐点调度。
- `task_refmem_sync` 维护同一张分布式向量表，不让真实板卡和模型节点状态各自漂移。
- `core1_realtime` 承载 TriggerAO、PIO 装载、运行态采样、T2/READY 捕获和 late 判定。
- `task_vdc_sync` 中的 SYNC DPLL 只负责把本地 tick 映射到稳态虚拟 DC；`task_dpll` 只负责转台角度预测和 `T_fire_base`。
- FatFs、USB 文本输出、SCPI 解析、OTA flash job、LCD 刷新不得进入 core1。

## 双核区域隔离

core0 和 core1 必须按区域隔离，而不是只按函数调用约定隔离。当前 linker 已经把
core1 stack 放在 `SCRATCH_X`、core0 stack 放在 `SCRATCH_Y`，但 `.text/.rodata/.data`
仍是同一 App 镜像共享区域。产品化目标需要进一步拆成三类区域：

| 区域 | owner | 内容 | 访问规则 |
|---|---|---|---|
| core0 control region | core0 | FreeRTOS task、USB/SCPI、OTA、Storage、UI、诊断、配置门禁 | core0 可读写；core1 禁止直接调用 |
| core1 realtime region | core1 | `core1_realtime`、TriggerAO/TriggerFB 快路径、PIO 装载、T2/READY 捕获、core1 私有状态 | core1 独占写；core0 只能通过 command queue 写意图 |
| shared vector region | core0 + core1 | `trigger_command_queue`、`trigger_status_ring`、DistributedVectorTable 摘要、heartbeat、fault evidence | 固定 layout、owner 字段、sequence/CRC；禁止临时读硬件 |

实现约束：

- core1 的入口、flash lockout poll、实时循环和高频 Trigger 快路径应逐步收敛到 RAM-resident section，避免 core0 flash erase/program 期间继续从 XIP flash 取指。
- core0 和 core1 应各自拥有独立的 VTOR/vector table；core1 的独立 RAM vector table/VTOR 是隔离入口和落实 flash lockout/park 的前提之一，而共享 RAM 一致性建议直接把 owner/sequence/CRC/seqlock 以及 RAM-resident section / flash lockout / park / entry table owner 做成表头或 slot 元素。
- core1 私有状态不得放在普通全局大池中；需要显式 section 或独立结构体，查询只能读取 core0 合并后的 snapshot。
- core0 需要写 flash、FatFs 或 USB 时，必须通过资源仲裁进入维护态；任何 flash erase/program 前都必须让 core1 park/lockout。
- 共享区只传小载荷、版本号和摘要，不传 OTA payload、日志文本、波形或 SD 文件内容。

## 自上而下 SCPI 适配

SCPI 指令表是上位机可见的产品 API，RTOS 任务划分必须先满足指令闭环，再落到底层驱动。
写指令返回 `1/OK` 只表示 accepted；跨核、跨节点或持久化动作必须通过 `READ:*?` 或
`SYSTem:*?` 快照确认完成态。业务配置走 `CONFigure:*`，业务读取走 `READ:*?`，
系统、资源、日志和故障走 `SYSTem:*`，运行控制走 `TRIGger:*`，校准和同步分别走
`CALibration:*` / `SYNC:*`。

### 指令域 owner

| SCPI 域 | 入口任务 | 执行 owner | 快照/闭环 | 说明 |
|---|---|---|---|---|
| IEEE 488.2 / `SYSTem:VERSion?` / `SYSTem:ERRor?` | `task_scpi` | `task_system` | `SystemSlot`、错误队列 | 基础识别、自检、错误队列和版本信息不进入实时路径 |
| `SYSTem:RTOS:STATus?` / `SYSTem:CORE?` | `task_scpi` | `task_system` | RTOS 水位、core heartbeat | 用于验收任务划分和双核活性 |
| `SYSTem:REFM:*` / `SYSTem:CONFigure:*` | `task_scpi` | `task_refmem_sync` / `task_system` | `DistributedVectorTable`、ACK/NACK | 读取表头、节点、配置门禁、角色、CRC 和拒绝原因 |
| `CONFigure:TRIGger` / `CONFigure:ANGLe:*` | `task_scpi` | `task_loop_engine` | `LoopSlot`、`Role/ConfigSlot` | 形成点内状态表、扫描角度集合和角度脉冲输入参数 |
| `CONFigure:SEQuence:*` / `READ:SEQuence:*?` | `task_scpi` | `task_loop_engine` | `LoopSlot`、配置 CRC、ACK/NACK | 采用自动展开状态表 + `state_id` 顺序引用；active sequence 在 START 前冻结 |
| `CONFigure:SWITch#` / `READ:SWITch#?` | `task_scpi` | `task_loop_engine` / `core1_realtime` | `IoSlot`、`TriggerSlot` | 独立切换只在序列引擎未占用时允许；RUN 中按策略 busy/deny |
| `TRIGger:MODE/STARt/STOP/PAUSe/CONTinue` | `task_scpi` | `task_system` + `task_loop_engine` | `SystemSlot`、`LoopSlot`、ACK/NACK | 模式、状态策略和资源仲裁由 core0 决定；core1 只消费已验证实时命令 |
| `READ:TRIGger:*?` / `READ:ANGLe:*?` | `task_scpi` | `task_loop_engine` | `LoopSlot`、`TriggerSlot` | 查询运行态、触发参数、角度游标和断点；只读快照，不临时读 IO |
| `CONFigure:CALibration:*` / `CALibration:*` | `task_scpi` | `task_calibration` | `CalibrationSlot`、ACK/NACK、storage | 维护 link/delay/staging/active；短事务测固定链路 delay，失败不覆盖旧数据 |
| `READ:CALibration:*?` | `task_scpi` | `task_calibration` | `CalibrationSlot`、结果页 | 读取 link table、delay table、结果、版本和质量 |
| `CONFigure:SYNC:*` / `SYNC:*` | `task_scpi` | `task_vdc_sync` | `VdcSlot`、`StatisticsSlot`、ACK/NACK | 绑定校准表、配置 ring/VDC DPLL/gate，执行 check/start/stop/relock/holdover |
| `READ:SYNC:*?` | `task_scpi` | `task_vdc_sync` | `VdcSlot`、`NodeSlot`、`StatisticsSlot` | 读取 LOCK/HOLDOVER、e_vdc、节点新鲜度、质量和版本 |
| `SYSTem:LOG:*` / `SYSTem:TRACe:*` / `SYSTem:SNAPshot:*` / `SYSTem:T2:DATA?` | `task_scpi` | `task_storage` | storage job、分页 block | RUN 后报告数据按页读取；不阻塞 core1 或 RUN 态边沿 |
| `SYSTem:OTA:*` / `SYSTem:USB:*` / `SYSTem:SD:*` | `task_scpi` | `task_ota` / `task_storage` / `task_system` | 资源仲裁、保护快照 | 涉及 flash/storage 的动作必须经过资源仲裁和 core1 park/lockout |

### 上位机事务闭环

测试上位机的主路径按“配置 -> 检查 -> 启动 -> 只读监控 -> 复盘”设计：

```text
CONFigure:TRIGger / CONFigure:ANGLe:* / CONFigure:SEQuence:*
  -> task_scpi accepted
  -> task_loop_engine staging
  -> READ:TRIGger:PARameter? / READ:SEQuence:ACTive? / SYSTem:CONFigure:ACK?

SYNC:CHECk
  -> task_vdc_sync check active CAL/SYNC、VDC LOCK、e_vdc、节点 freshness
  -> READ:SYNC:STATe? / READ:SYNC:QUALity?

TRIGger:STARt [plan_id]
  -> task_system 状态策略、权限、资源仲裁、FAULT latch 检查
  -> task_loop_engine 冻结 epoch/run_id/config_crc/sequence_crc/cal_crc/sync_crc
  -> angle pulse 外层 + active sequence 内层
  -> core1_realtime 只执行 FIRE_LOAD/local_fire/capture

RUN 中
  -> 测试上位机主要读取网分数据
  -> DTC 只允许 READ:*?、SYSTem:*?、TRIGger:STOP 等安全入口

RUN 后
  -> SYSTem:RUN:SUMMary? / SYSTem:RUN:LOG?
  -> SYSTem:TRACe:DATA? / SYSTem:SNAPshot:DATA? / SYSTem:T2:DATA?
  -> SYSTem:FAULT:LAST? / SYSTem:ERRor?
```

调试上位机可以访问 `SERVICE/DEBUG/FACTORY` 能力，但仍必须走同一条 core0 控制面：
`PermissionProfile -> SystemModeTable -> StatePolicyTable -> ResourceArbiter -> ACK/NACK -> READ/SYSTem snapshot`。
任何调试入口都不能直接修改 core1 已装载预约、PIO owner 或实时状态。

### 自下而上补指令规则

RTOS、驱动和 HIL 工具实现过程中可以反推新增 SCPI 指令，但必须按产品化接口集中收敛，
不能按内部 task、临时变量或调试 printf 随意外露。新增指令只有满足以下条件才进入指令表：

| 准入项 | 要求 |
|---|---|
| 产品语义 | 指令必须对应配置、执行、状态、质量、日志、资源或恢复动作之一，不能只是读取某个内部变量 |
| 单一 owner | 必须明确由哪个 task/vector 写入事实，非 owner 只能投递事件或读取快照 |
| 固定响应 | 必须定义固定 block 字段，至少包含版本/CRC/stale/flags 或明确说明为何不需要 |
| 状态策略 | 必须写清 `IDLE/CONFIG/LOCK/CAL/ARMED/RUN/HOLDOVER/FAULT` 下允许、排队、拒绝或只读 |
| ACK 闭环 | 写命令如果跨核、跨节点或持久化，返回值只表示 accepted，完成态必须能通过 ACK/状态查询闭环 |
| 权限边界 | 必须落入 `TEST/SERVICE/DEBUG/FACTORY` 权限层级，不能因为调试方便绕过现场测试边界 |
| 实时隔离 | 指令不能直接等待 READY/T2、不能直接改 PIO owner、不能影响已装载 `local_fire` |
| 报告价值 | RUN 相关新增查询应能进入 run summary、trace、snapshot、T2 或 fault evidence，避免只服务一次性联调 |

命名收敛规则：

- 业务配置仍放在 `CONFigure:*`。
- 业务状态和质量读取放在 `READ:*?`。
- 系统、资源、权限、队列、水位、日志和维护放在 `SYSTem:*`。
- 运行动作只放在 `TRIGger:*`，校准动作只放在 `CALibration:*`，同步动作只放在 `SYNC:*`。
- 不新增按 task 命名的产品指令，例如不使用 `TASK:VDC:*` 或 `CORE1:PIO:*` 这类入口。
- 开发兼容查询可以保留，但必须在文档中标为 validation/兼容字段，产品上位机优先使用完整命令。

### 候选集中入口

当前指令表已经覆盖主要业务闭环。自下而上实现时，若发现观测能力不足，优先从以下少数
集中入口扩展，避免零散增加私有调试命令：

| 候选指令 | owner | 用途 | 进入条件 |
|---|---|---|---|
| `SYSTem:COMMand:ACK? [command_seq]` | `task_system` / `task_refmem_sync` | 通用分布式命令完成态，覆盖 CONFIG、TRIG、CAL、SYNC、FAULT clear | 如果 `SYSTem:CONFigure:ACK?` 语义不足以表达非配置命令，就升级为通用 ACK 入口 |
| `SYSTem:COMMand:NACK? [reason_id]` | `task_system` | 统一拒绝原因表，服务上位机 UI 参数校验和状态提示 | 当 CAL/SYNC/TRIG 的拒绝原因开始重复定义时启用 |
| `SYSTem:QUEue:STATus? [queue]` | `task_system` | 读取 SCPI、gateway、calibration、sync、trigger、storage 队列深度、水位、drop、timeout | 当 RTOS 水位不足以定位 accepted 后无完成态的问题时启用 |
| `READ:TRIGger:LOAD?` | `task_loop_engine` / `core1_realtime` | 读取 `FIRE_LOAD` 装载窗口、队列深度、next_seq、late/drop 和本地装载健康度 | 当 `READ:TRIGger:STATe?` 过于粗略，无法证明实时预约链路健康时启用 |
| `READ:STATistics? [domain]` | `task_storage` / `task_diag` | 统一读取 `e_vdc/e_act/e_pll/late/CRC/seq/stale` 窗口统计摘要 | 当 run summary 和各域 quality 查询无法满足报告汇总时启用 |
| `SYSTem:SNAPshot:MARK` | `task_storage` | 手动标记一个配置/运行/故障快照，供 HIL 和现场复盘定位 | 仅 `SERVICE+`，且不得在 RUN 中触发阻塞写盘 |

这些候选不是立即实现清单。只有当底层验证证明现有 `READ:*?` / `SYSTem:*?` 不能闭合
“accepted -> 执行 -> 完成态 -> 报告证据”链路时，才把候选提升到 SCPI 指令表，并同步
补充响应 block、权限策略、状态门禁和 HIL 验收用例。

## 任务划分

初始栈按“先大后小”策略给出，后续用 `SYST:RTOS:STAT?` 水位收缩。

| 执行体 | 核 | 优先级 | 初始栈 | 周期/触发 | 职责 |
|---|---:|---:|---:|---|---|
| `core1_realtime` | core1 | 裸实时循环 | core1 独立栈 | 尽可能快轮询 | TriggerAO、`local_fire` 装载、capture/T2 采样、RJ45 帧首沿服务、late/CRC/fault 快速判定 |
| `task_system` | core0 | 4 | 2048 words | 1 ms | bringup、系统模式、角色加载、资源仲裁、故障闩锁、board service |
| `task_usb_device` | core0 | 4 | 1536 words | 1 ms 或更快 | TinyUSB/CDC/USBTMC 轮询，保持 USB 传输活性 |
| `task_scpi` | core0 | 3 | 3072 words | 事件/1 ms | SCPI 解析、命令 ACK、只投递事件或查询快照 |
| `task_gateway_a3` | core0 | 3 | 3072 words | 事件/5 ms | A3 网关、配置包接收、START/STOP 转发、VNA 状态桥接 |
| `task_loop_engine` | core0 | 3 | 3072 words | 事件/scan tick | A0 扫描编排、LoopEngine、LayerAction、ActionMap、滚动生成 FIRE_LOAD |
| `task_vdc_sync` | core0 | 4 | 2048 words | SYNC/1 ms | SYNC DPLL、虚拟 DC offset/rate、LOCK/HOLDOVER/RELOCK、`e_vdc` 统计 |
| `task_calibration` | core0 | 3 | 2048 words | 事件/短事务 | CAL link/delay 表、NODE/SMA 基础链路测量、DEVICE/T2 校准、staging/active/version/quality |
| `task_refmem_sync` | core0 | 4 | 2048 words | ring frame/1 ms | 模拟反射内存管理、分布式向量表合并、节点心跳、slot stale 判定 |
| `task_dpll` | core0 | 3 | 2048 words | Compare/T2/scan tick | A0 转台 Compare Out、角度预测 DPLL、`T_fire_base` 生成；不承担虚拟 DC 同步 |
| `task_storage` | core0 | 2 | 3072 words | job/10 ms | TF/FatFs、配置包、断点、T2 分布、late/CRC/seq 日志 |
| `task_ota` | core0 | 2 | 1536 words | job | OTA AO、metadata、flash job，受资源仲裁限制 |
| `task_ui` | core0 | 1-2 | 2048 words | 50-250 ms | LCD、按键、节点 ID、同步状态、ARM、计数和错误码 |
| `task_diag` | core0 | 1 | 1024-1536 words | 100-1000 ms | 低频诊断、log flush、统计快照；P0 可并入 `task_system` |

P0 bring-up 可以只实现以下任务：

```text
task_system
task_usb_device
task_scpi
task_refmem_sync
task_ota
task_storage
task_ui
core1_realtime
```

`task_gateway_a3`、`task_loop_engine`、`task_calibration`、`task_vdc_sync`、`task_dpll` 先用空壳和计数器占位，
待队列、Vector owner 和验证工具稳定后逐步接入业务。

## 角色启用矩阵

同一固件支持 A0/A1/A2/A3，角色由 `NodeRoleMap` 或产品配置决定。

| 执行体 | A0 | A1 | A2 | A3 | 说明 |
|---|---:|---:|---:|---:|---|
| `task_system` | on | on | on | on | 所有节点都有系统状态与安全策略 |
| `task_usb_device` | diag | diag | diag | on | A3 必开；其他节点可仅用于维护口 |
| `task_scpi` | diag | diag | diag | on | RUN 态只允许安全查询和 STOP/FAULT clear |
| `task_gateway_a3` | off | off | off | on | 上位机/VNA 近端网关 |
| `task_loop_engine` | on | off | off | proxy | A0 执行；A3 只转发或显示 |
| `task_calibration` | owner | local | local | coordinator | A0/A3 编排；各节点执行本地输入/输出链路测量和结果发布 |
| `task_vdc_sync` | origin | follower | follower | follower | A0 发布；A1/A2/A3 跟随 |
| `task_refmem_sync` | on | on | on | on | 所有节点维护同一张 DistributedVectorTable |
| `task_dpll` | on | off | off | off | 首版转台 Compare Out 在 A0 |
| `core1_realtime` | on | on | on | on | 所有节点都有本地 PIO 触发/捕获 |
| `task_storage` | on | on | on | on | 记录配置、校准和异常；A3 可承担汇总 |
| `task_ui` | on | on | on | on | 本地状态查看 |
| `task_ota` | on | on | on | on | 维护和升级 |

## 队列与数据流

### 控制队列

| 通道 | 方向 | 载荷 | 约束 |
|---|---|---|---|
| `scpi_control_queue` | `task_scpi` -> `task_gateway_a3/task_loop_engine/task_system` | 配置、START、STOP、查询请求 | 不承载大文件，写命令只表示 accepted |
| `gateway_control_queue` | `task_gateway_a3` -> `task_loop_engine` | 上位机配置包索引、启动停止、VNA 状态 | A3 专用，不能逐点驱动 RUN |
| `loop_event_queue` | `task_loop_engine/task_dpll/task_vdc_sync` 内部 | scan tick、layer action、角度 DPLL 更新、VDC gate 更新 | A0 owner 消费 |
| `calibration_job_queue` | `task_scpi/task_system` -> `task_calibration` | link add/set/delete、CAL start、save/load/activate/check | 短事务优先；持久化动作转 storage/resource 仲裁 |
| `sync_control_queue` | `task_scpi/task_system` -> `task_vdc_sync` | SYNC check/start/stop/relock/holdover、DPLL profile | 不直接写实时边沿；完成态写 VdcSlot/ACK |
| `trigger_command_queue` | core0 -> core1 | ARM/DISARM、FIRE_LOAD、PIO 装载、捕获窗口 | 固定小载荷，非阻塞，满队列计 `late/drop` |
| `trigger_status_ring` | core1 -> core0 | T2、READY、late、CRC、seq、fault evidence | 无 FatFs/USB 调用，只写 RAM ring |
| `refmem_delta_queue` | `task_refmem_sync` <-> RJ45 ring | 节点 slot 版本、状态摘要、ACK 位图、故障摘要 | 只传播小 delta，不传大文件和波形 |
| `storage_job_queue` | core0 tasks -> `task_storage` | trace、snapshot、manifest、catalog/read、CAL/SYNC package | 可阻塞但必须有超时 |
| `diag_event_queue` | any -> `task_diag/system` | 轻量事件码和计数 | 大文本格式化在 core0 低优先级完成 |

### 典型 RUN 数据流

```text
A3 SCPI START
  -> task_scpi
  -> task_gateway_a3
  -> task_system / task_loop_engine 做配置、CAL、SYNC、序列门禁
  -> task_loop_engine(A0) 冻结 run_id / active sequence / active Δt_i
  -> task_dpll 生成角度预测 T_fire_base
  -> task_loop_engine 生成 T_fire_i / Δt_i / mask
  -> trigger_command_queue(FIRE_LOAD)
  -> core1_realtime 装载 PIO local_fire
  -> PIO 到点输出 SMA_OUT
  -> PIO capture_window 捕获 READY/T2
  -> trigger_status_ring
  -> task_loop_engine/task_storage/task_gateway_a3
```

通信抖动只影响是否 late；一旦 PIO 已装载，USB/SCPI/SD/UI 抖动不能进入边沿。

## 模拟反射内存

当前四板系统需要维护同一张分布式向量表。这里的“模拟反射内存”不是硬件共享 RAM，
而是用固定内存布局 + owner 写权限 + 版本号 + RJ45_SYNC_RING 小帧同步，实现各板对
系统状态的同构镜像。
表内节点维度按 8 个节点预留：A0/A1/A2/A3 是当前真实板卡，后续可挂接模型节点，
例如模拟网分、模拟转台、模拟 DUT 或产测代理。上层只看统一的 node status、ACK、fault
和 heartbeat 语义，不关心节点背后是真硬件还是仿真模型。

核心定位：

- 反射内存回答“系统当前共同认知是什么”。
- 反射内存不承载精确触发边沿，不能替代 `FIRE_LOAD` 到 PIO 的本地装载。
- 反射内存不传大文件、波形、OTA payload 或 SD 内容。
- 反射内存与诊断日志分离；日志解释历史，反射内存保存当前事实和摘要。

### DistributedVectorTable

DistributedVectorTable 从 P0 起就按产品化完整表布局实现，避免后续扩容破坏协议、
工具和日志格式。初始目标为 64 KB 固定表，其中 P0 只启用核心字段，其余区域保留并
纳入 CRC/版本管理。

| 区域 | 建议大小 | 内容 | 写入者 | 说明 |
|---|---:|---|---|---|
| Header/Directory | 1 KB | magic、layout_version、table_size、slot offset、table_seq、epoch、crc32 | `task_refmem_sync` | 整表一致性、版本识别和工具解析入口 |
| SystemSlot | 1 KB | system_mode、role_map_version、run_id、fault_latch、release gate | `task_system` | 全局模式和安全状态 |
| Role/ConfigSlot | 2 KB | NodeRoleMap、hw_profile、persona、feature mask | `task_system` / config loader | 真实板卡和模型节点角色/hardware profile |
| VdcSlot | 2 KB | sync_id、sync_crc、offset、rate、lock_state、holdover、relock、`e_vdc`、sync_seq | `task_vdc_sync` | SYNC DPLL 形成的虚拟 DC 共同时间轴 |
| LoopSlot | 4 KB | trigger param、angle sweep/pulse/breakpoint、active sequence、scan_index、next_fire_seq、LoopEngine 摘要 | `task_loop_engine` | A0 扫描编排和业务序列摘要 |
| DpllSlot | 2 KB | compare 捕获、角度预测、`T_fire_base`、`e_pll` 统计 | `task_dpll` | A0 转台角度预测 DPLL 状态 |
| NodeSlot[8] | 4 KB | node_id、node_type、role、heartbeat、local_state、error_code、stale_count | 各节点 owner | 8 个 512B slot；真实板卡和模型节点都占用 node slot |
| TriggerSlot[8] | 8 KB | armed、last_fire_seq、late_count、t2_count、ready_timeout、runtime counters | 各节点 core1/模型摘要由 core0 合并 | 8 个 1KB 摘要；模型节点可发布模拟触发事实 |
| IoSlot[8] | 8 KB | SMA/RJ45/BiSS 近端 IO 镜像、反序映射、边沿计数、健康状态 | 各节点 owner | 8 个 1KB 事实镜像，不临时读硬件 |
| CalibrationSlot | 8 KB | link table、delay table、NODE/SMA 基础链路 delay、DEVICE/T2 delay、staging/active/version/quality | `task_calibration` | 只放当前生效摘要，完整历史在 storage |
| StatisticsSlot | 8 KB | `e_vdc/e_act/e_pll` 窗口统计、CRC/seq/late 分布、p99/p999 | 各统计 owner | 查询和报告的快速摘要 |
| AckCommandSlot | 4 KB | command_seq、ack_flags、nack_flags、busy_flags、原子命令槽 | 命令 owner + 各节点 ack | 配置/START/STOP 确认 |
| FaultEvidenceSlot | 6 KB | fault_code、source_node、evidence_seq、first_ts、last_ts、关键证据 | `task_system/task_refmem_sync` | 故障归档入口 |
| GatewaySlot | 2 KB | A3/VNA/host 状态、数据采集状态、上位机连接摘要 | `task_gateway_a3` | A3 网关状态 |
| OtaStorageUiSlot | 2 KB | OTA、Storage、UI 摘要 | 对应 task owner | 维护和观测状态 |
| TlvExtension | 2 KB | versioned TLV、未来扩展 | owner by type | 不改变主 slot offset 的小扩展区 |

合计 64 KB。链接脚本或配置区应按 64 KB 预留；如果 RP2350 SRAM 压力过大，可以把
完整快照保存在 core0 SRAM，core1 只持有 TriggerSlot 相关轻量镜像和状态 ring。

完整表不等于整表高频同步。RJ45_SYNC_RING 上只同步变更 slot 的小 delta：

```text
local full table: 64 KB
ring frame:       REFMEM_DELTA(slot_id, slot_version, compact payload)
A3 query:         read local snapshot
storage:          save full snapshot or compressed delta log
```

PinProbe A1 的 RamVector 经验在这里保留三条原则：

- 命令是意图：写命令槽不代表动作完成。
- 镜像是事实：查询只读 DistributedVectorTable 快照。
- 状态是推导：SystemManager/LoopEngine 基于事实镜像和 ACK 推导状态。
- 表要紧凑：PinProbe A1 的 RamVector 只有 1024B，说明反射内存的关键不是大，
  而是固定 layout、清楚 owner、查询快照和日志分离。RP2350 这里保留 64 KB 是产品化
  协议预算，仍禁止承载日志、波形、OTA payload 或 SD 文件内容。

### Owner 写权限

| 字段 | 唯一写入者 |
|---|---|
| `NodeSlot[i]` | 节点 i 的 `task_refmem_sync` |
| `TriggerSlot[i]` | 节点 i 的 core1 状态摘要，经本节点 core0 合并 |
| `IoSlot[i]` | 节点 i 的 IO/capture owner |
| `VdcSlot` | A0 `task_vdc_sync` 为 origin，其他节点只镜像 |
| `LoopSlot` | A0 `task_loop_engine` |
| `CalibrationSlot` | `task_calibration` 按 link/delay owner 写入 |
| `DpllSlot` | A0 `task_dpll` |
| `AckSlot.bit[i]` | 节点 i |
| `FaultSlot` | 首个故障 owner 写入，`task_system` 负责锁存 |

任何节点不能直接覆盖其他节点 slot。跨节点写意图必须变成命令帧，由目标节点 owner
验证后写入自己的 slot 和 ACK。

### 同步方式

RJ45_SYNC_RING 上增加 `REFMEM_DELTA` 轻量帧：

```text
REFMEM_DELTA {
  frame_type
  table_seq
  source_node
  slot_id
  slot_version
  payload_len
  payload_crc
  payload
}
```

同步策略：

- 高频变化只发摘要和计数，不发全文。
- 每个 slot 有独立 `slot_version` 和 `source_node`。
- 接收端只在 `version` 更新且 CRC 正确时合并。
- 长时间未更新的 slot 标记为 `STALE`，不直接删除旧值。
- A0 可以周期性发 `REFMEM_EPOCH`，统一 run_id 和 table epoch。
- A3 查询时读本地镜像；如果镜像 stale，应返回 stale 标志，而不是临时跨板阻塞查询。

### 快照一致性

本地查询和任务读取必须使用整表或整 slot 快照：

```text
read begin_seq
copy slot/table
read end_seq
begin_seq == end_seq && even -> valid snapshot
otherwise retry or return BUSY
```

实现待选：

- P0：slot 级临界区 + copy snapshot。
- P1：sequence lock，写入时 seq 置奇数，提交后置偶数。
- P2：双缓冲 table snapshot，`task_refmem_sync` 统一 publish。

命令槽沿用 PinProbe A1 的原子 Take/Clear 原则：

```text
lock
  if slot has command:
    copy command to local
    clear slot / update command_seq
unlock
execute outside critical section
publish ack/nack
```

### 与实时路径的关系

反射内存不能进入 PIO 边沿生成路径：

```text
DistributedVectorTable -> 用于状态认知、配置摘要、ACK、故障和查询
FIRE_LOAD command      -> 用于未来预约触发装载
PIO local_fire         -> 用于真实边沿
```

RUN 态下，`task_loop_engine` 可以根据反射内存中的 ACK、stale、fault、late_count 决定是否
继续生成下一批预约，但不能等反射内存同步完成后再输出某个已经临近的边沿。

## 分布式系统门禁

四板分布式触发的产品化风险主要来自“各板认知不一致”。以下门禁必须先定义，再逐步实现。

### 虚拟 DC 建立顺序

虚拟 DC 不是 BiSS-C 线时钟直接产生，也不是 DPLL 替代本地晶振。指令表中的
`SYSTem:SYNC:VDC:DPLL:*` 是同步域 VDC 的维护/调试入口，用来把各节点本地 tick 映射到共同 DC；`task_dpll`
是扫描域的角度预测环路，用来生成 `T_fire_base`。两类 DPLL 的 owner、指标和门禁不得混用。

产品化顺序固定为：

```text
local oscillator -> local_tick
NODE/SMA base link calibration -> link_delay_ns / delay table
BiSS-C/RJ45 sync frame -> local timestamp observation
SYNC DPLL -> offset/rate estimate
LOCKED virtual DC -> DEVICE/T2 calibration -> Δt_i
Angle DPLL -> T_fire_base prediction
FIRE_LOAD / local_fire / T2 capture / e_act validation
```

也就是说，本地晶振和本地 tick 先存在；CALibration 先建立 NODE/SMA 固定链路 delay；
BiSS-C/RJ45 组网提供跨节点观测；SYNC DPLL 在观测稳定后收敛出共同虚拟时间轴。
`LOCKING` 前只能作为 provisional/free-run 估计，不允许作为正式触发运行和 DEVICE/T2
校准的时间基准。DEVICE/T2 校准必须在 DC `LOCKED` 后执行，形成预测分发使用的动作补偿
`Δt_i`；随后角度预测 DPLL 才根据转台 Compare/角度脉冲生成未来 `T_fire_base`。

### 指令门禁依赖

| 动作 | 必备前置条件 | 完成态查询 | RTOS owner |
|---|---|---|---|
| `CONFigure:CALibration:LINK:*` | `IDLE/CAL`，目标端口合法，资源未被 RUN 占用 | `READ:CALibration:LINK?`、ACK/NACK | `task_calibration` |
| `CALibration:STARt <type,src,dst>` | link 已登记，`TriggerState!=RUN`，测试输出安全可控 | `READ:CALibration:STATe?`、`READ:CALibration:RESult?` | `task_calibration` + `core1_realtime` |
| `CALibration:ACTivate/SAVE` | staging valid，资源仲裁通过；涉及 flash/storage 时 core1 park/lockout | `READ:CALibration:ACTive?`、`READ:CALibration:VERSion?` | `task_calibration` + `task_storage` |
| `CONFigure:SYNC:*` | active CAL 存在或 staging 绑定合法，ring node_order 匹配 NODE link | `READ:SYNC:PARameter?`、ACK/NACK | `task_vdc_sync` |
| `SYNC:CHECk/STARt` | active sync、active cal、节点 freshness、CRC/seq 门限合法 | `READ:SYNC:STATe?`、`READ:SYNC:QUALity?` | `task_vdc_sync` + `task_refmem_sync` |
| `CONFigure:TRIGger/ANGLe/SEQuence` | `IDLE/CONFIG/ARM` 安全边界；RUN 中只读或拒绝 | `READ:TRIGger:PARameter?`、`READ:SEQuence:ACTive?` | `task_loop_engine` |
| `TRIGger:STARt` | 配置 CRC、active sequence、active CAL/SYNC、SYNC LOCKED、FAULT clear | `READ:TRIGger:STATe?`、`SYSTem:CONFigure:ACK?` | `task_system` + `task_loop_engine` |
| `SYSTem:FAULT:CLEAr` | 输出已安全，故障证据已可读或已归档 | `SYSTem:FAULT:LAST?`、`SYSTem:CONFigure:STAT?` | `task_system` |

### 全局 Epoch 与版本

每次 START 必须生成新的全局上下文：

```text
epoch
run_id
config_version
calibration_version
loop_plan_version
action_map_version
```

所有 RUN 态帧、T2、故障、日志和反射内存快照都必须携带 `epoch/run_id` 或能回溯到该上下文。
不同 epoch 的数据不能混入同一轮统计。

### 配置一致性

进入 `ARMED/RUN` 前，A0 必须确认目标节点的关键 CRC 一致：

| 项目 | 检查字段 |
|---|---|
| 固件 | build_id、app_version、feature_mask |
| 硬件 | hw_profile、board_rev、role |
| 角色 | NodeRoleMap CRC |
| 扫描 | LoopPlan CRC、ActionMap CRC |
| 校准 | Calibration CRC、`Δt_i` version |
| 反射内存 | layout_version、table_size、slot directory CRC |
| 环路协议 | frame_version、supported_frame_mask |

任意目标节点不一致时，系统不能进入 `RUN`；应返回明确的 NACK reason。

### 时间与状态分层

实现和测试报告必须保持以下层级独立：

```text
VDC time axis          -> 共同时间认知
FIRE_LOAD command     -> 未来预约
PIO local_fire        -> 真实边沿
T2/READY capture      -> 事实回读
DistributedVectorTable -> 共同状态镜像
Trace/Log             -> 事后解释
```

反射内存、SCPI、日志和 SD 不能参与真实边沿生成。

### 节点新鲜度

节点状态必须区分以下状态，不能只用 OK/FAIL：

| 状态 | 含义 | RUN 策略 |
|---|---|---|
| `OK` | 新鲜、版本匹配、CRC 正确 | 可参与 RUN |
| `STALE` | 超过 stale window 未更新 | 停止生成后续预约 |
| `MISSING` | 启动后从未见过该节点 | 禁止 ARM |
| `INVALID` | CRC、版本或 role 不匹配 | 禁止 ARM/RUN |
| `FAULT` | 节点主动故障 | 进入 FAULT 或按策略 HOLDOVER |

建议首版保守：RUN 中任一目标节点 `STALE/INVALID/FAULT`，A0 停止后续预约，进入 `HOLDOVER`
或 `FAULT`，不自动补发已过期触发。

### 命令确认

配置、ARM、START、STOP、FAULT_CLEAR 等分布式命令必须具备确认机制：

```text
command_seq
target_mask
ack_flags
nack_flags
busy_flags
timeout_flags
nack_reason[node]
```

“命令已发送”和“系统已进入目标状态”是两件事。SCPI 写命令只能表示 accepted；状态完成必须通过
ACK/状态查询确认。

### 故障证据

每个分布式故障至少记录：

```text
source_node
fault_code
severity
epoch
run_id
fire_seq
local_time
vdc_time
evidence_seq
slot_version
```

故障等级统一为：

| 等级 | 语义 |
|---|---|
| `INFO` | 可观测事件 |
| `WARN` | 可继续但计数 |
| `HOLDOVER` | 同步短时失锁，停止后续预约，等待重锁或 STOP |
| `FAULT` | 故障锁存，要求人工/上位机清除 |
| `INTERLOCK` | 硬禁止，输出安全态 |

### 降级与重锁

必须提前定义：

- RJ45 ring CRC 连续错误阈值。
- VDC holdover 最大持续时间。
- `STALE` 节点是否允许自动恢复。
- `RELOCK` 后是否允许继续 RUN。
- `HOLDOVER` 是否需要重新 ARM。

首版策略建议：

```text
RUN 中失锁/STALE -> HOLDOVER
HOLDOVER 中不生成新的 FIRE_LOAD
已装载且未过期的 PIO 动作按安全策略执行或撤销
RELOCK 后不自动继续 RUN，等待 A0/上位机重新 ARM/START
```

### 可复盘性

每次 RUN 至少保存以下摘要：

```text
run_id / epoch
四板 build_id / hw_profile / role
NodeRoleMap CRC
LoopPlan CRC
ActionMap CRC
Calibration CRC
late_count
CRC/seq/stale counters
T2/e_act/e_vdc/e_pll statistics
fault evidence
last N REFMEM_DELTA
last N FIRE_LOAD
last N ACK/NACK
```

完整历史写入 storage；DistributedVectorTable 只保存当前摘要和短窗口证据。

### 安全默认态

所有状态必须满足：

- 上电和 bootloader 阶段 DE/RE 默认关闭。
- PIO owner 未锁定前禁止输出。
- RUN 前禁止外部触发使能。
- 看门狗复位后输出进入安全态。
- 通信丢失时停止后续预约。
- FAULT 后输出保持安全态，直到明确清除。

## Vector 与字段归属

| 数据/Vector | 唯一写入者 | 可读者 | 说明 |
|---|---|---|---|
| `SystemVector.system_mode` | `task_system` | all | BOOT/IDLE/CONFIG/LOCK/RUN/HOLDOVER/FAULT |
| `NodeRoleMap` | `task_system` / config loader | all | A0/A1/A2/A3、hw_profile、persona |
| `DistributedVectorTable` | `task_refmem_sync` 按 slot owner 合并 | all snapshot | 四板共享事实镜像，不承载大数据或硬实时边沿 |
| `LoopPlan` / `SequencePlan` | `task_loop_engine` | SCPI/UI/storage | trigger param、angle sweep/pulse/breakpoint、active sequence、ActionMap |
| `T_fire_base` | `task_dpll` | `task_loop_engine/core1` | A0 角度预测输出的预约基准 |
| `VdcVector.offset/rate/lock` | `task_vdc_sync` | all | SYNC DPLL 形成的虚拟 DC 时间轴事实源 |
| `CalibrationVector.link/delay` | `task_calibration` | all snapshot | NODE/SMA 基础链路 delay、DEVICE/T2 delay、staging/active 版本 |
| `TriggerVector` | `core1_realtime` / TriggerAO | all snapshot | PIO 状态、ARM、seq、T2、late、error |
| `StorageVector` | `task_storage` | all | SD 状态、job、trace/snapshot 结果 |
| `OtaVector` | `task_ota` | all | OTA 状态、目标槽、错误码 |
| `UiVector` | `task_ui` | all | 页面、按键、告警确认 |

任何任务如果不是 owner，只能通过事件请求改变状态。

## 系统状态

建议产品化 SystemManagerAO 使用以下状态：

| 状态 | 说明 | 允许动作 |
|---|---|---|
| `BOOT` | 上电、自检、角色加载 | 关闭外部驱动、禁止触发 |
| `IDLE` | 安全空闲 | 配置、查询、OTA、SD 操作 |
| `CONFIG` | 配置包加载/校验 | 修改 NodeRoleMap、LoopPlan、校准表 |
| `LOCK` | RJ45 ring 和 VDC 锁定 | SYNC、offset/rate 收敛、禁止正式输出 |
| `CAL` | SMA/RJ45/T2 校准 | 允许测试脉冲和回环，不进入正式样本 |
| `ARMED` | 已准备 RUN | 只允许安全查询、STOP、DISARM |
| `RUN` | 四板内部自循环 | 禁止改 pin map/角色/PIO owner；只允许 STOP 和状态查询 |
| `HOLDOVER` | 短时同步丢失 | 停止后续预约输出，保留日志等待重锁 |
| `FAULT` | 故障锁存 | 输出安全态，等待人工/上位机清除 |

## SCPI 规则

RUN 态允许的产品化入口必须以完整指令表为准：

- `*IDN?`
- `SYSTem:CORE?`
- `SYSTem:RTOS:STATus?`
- `SYSTem:ERRor?`
- `SYSTem:FAULT:LAST?`
- `SYSTem:RUN:LAST?`
- `SYSTem:RUN:SUMMary?`
- `SYSTem:LOG:STATus?`
- `SYSTem:TRACe:LAST?`
- `READ:TRIGger:STATe?`
- `READ:TRIGger:PARameter?`
- `READ:ANGLe:POSition?`
- `READ:ANGLe:PULSe?`
- `READ:SYNC:STATe?`
- `READ:SYNC:QUALity?`
- `TRIGger:STOP`

RUN 态禁止或推迟到下一轮：

- 修改 pin map。
- 修改 `NodeRoleMap`。
- 修改 `ActionMap`。
- 修改 `CONFigure:TRIGger`、`CONFigure:ANGLe:*`、`CONFigure:SEQuence:*` 或 active sequence。
- 写入 `CONFigure:CALibration:*`、`CALibration:*`、`CONFigure:SYNC:*`、`SYNC:*`。
- 修改 `SYSTem:SYNC:VDC:DPLL:*` 调试覆盖。
- 修改 PIO owner。
- 直接写 SD 大文件。
- OTA 开始或 flash erase/write。
- 对已过期 `T_fire` 补发触发。

开发验证指令也必须归入系统维护域：`SYSTem:LOOP:STATus?` 属于 LoopEngine 维护入口，
`SYSTem:SYNC:VDC:STATus?` 和 `SYSTem:SYNC:VDC:DPLL:STATus?` 属于同步域维护入口。
不再新增裸顶层 `VDC:*`、`DPLL:*` 或 `STATus:VDC/DPLL?`；产品上位机优先使用
`READ:*?` / `SYSTem:*?` 的完整命令和固定 block 字段。

## 禁止清单

core1 禁止：

- FatFs/SD 调用。
- USB CDC/USBTMC 输出。
- SCPI 解析。
- OTA flash job。
- 任何直接触发 `flash_range_erase()` / `flash_range_program()` 的写 flash 路径；这类操作必须先由 core1 持续执行 `drv_flash_core1_lockout_poll()` 进入 park 再由 core0 写入。
- LCD/SPI UI 刷新。
- `printf`/阻塞日志格式化。
- 无界等待、mutex 长持有、动态内存申请。

`task_scpi` 禁止：

- 直接调用 `sync_io_start()` / `sync_io_*_arm()`。
- 直接改写 `TriggerVector`、`VdcVector`、`CalibrationVector`、`LoopPlan` owner 字段。
- 等待设备 READY/T2。
- 在 RUN 态逐点推进扫描。

`task_storage` 禁止：

- 在 core1 上运行。
- 持有文件系统锁时调用 TriggerAO 或 OTA flash job。
- 阻塞 RUN 态实时路径。

## 分阶段待办

### P0 - 任务边界固化

- [x] 将当前 `task_io_frontend` 拆为 `task_usb_device` 和 `task_scpi`。
  2026-08-10: build `20260810104144` 已烧录验证，RTOS task list 显示 `usb_device` 和 `scpi`。
- [x] 将 `app_comm_service()` 拆为 `app_usb_device_service()` 和 `app_scpi_service()`。
  2026-08-10: 裸机路径保留 `app_comm_service()` wrapper，FreeRTOS 路径由两个任务直接调用。
- [x] 让 `SYST:RTOS:STAT?` 显示拆分后的任务水位。
  2026-08-10: `usb_device` used 32 words，`scpi` used 1166 words，heap min free 73584 bytes。
- [x] 保持当前 `TRIG:MODE 1 -> TRIG:ARM -> TRIG:DIS` 板端 smoke 通过。
  2026-08-10: `tools/multicore_board_validate` 5/5 PASS，结果归档 `build-rtos-multicore-smoke/validation_split_usb_scpi_step1`。
- [x] 建立 `task_loop_engine` 空壳，只计数和响应状态查询，不接业务。
  2026-08-10: 已增加 `task_loop_engine` RTOS 任务、`LOOP:STAT?` / `STAT:LOOP?` 只读查询，以及本地 service_count/first_service_ms/last_service_ms 快照。
- [x] 建立 `task_vdc_sync` 空壳，只维护 lock 状态和统计计数。
  2026-08-10: build `20260810124245` 已烧录验证，VDC 状态返回 ready、lock_state、service_count、first/last service ms 和 sync_seq，计数持续增长。后续规范入口为 `SYSTem:SYNC:VDC:STATus?`。
- [x] 建立 `task_dpll` 空壳，只维护 disabled/ready 状态。
  2026-08-10: build `20260810124902` 已烧录验证，DPLL 状态返回 ready、state、service_count、first/last service ms 和 update_seq，计数持续增长。后续规范入口为 `SYSTem:SYNC:VDC:DPLL:STATus?`。
- [x] 建立 `task_refmem_sync` 空壳，按 64 KB 完整布局维护本地 DistributedVectorTable header、node slot 和 heartbeat。
  2026-08-10: build `20260810110636` 已烧录验证，`SYST:RTOS:STAT?` 显示 `refmem_sync`，本地 heartbeat 持续增长；NodeSlot 预留 8 个节点。
- [x] 增加本地 DistributedVectorTable snapshot 查询，先不做跨板同步。
  2026-08-10: 已增加 `SYST:REFM:STAT?` / `SYST:REFM:NODE?`，返回 64 KB table、layout version、table_seq 和本节点 slot 快照。

### P1 - 反射内存与快照一致性

- [ ] 定义 `distributed_vector_table.h`，冻结 64 KB 完整表 layout、slot offset、slot size 和 layout version。
- [ ] 在链接脚本/配置中为 DistributedVectorTable 预留 64 KB 预算，避免后续扩容破坏协议。
- [ ] 增加 epoch、run_id、config_version、calibration_version、loop_plan_version 和 action_map_version 字段。
- [ ] 增加 sync_version、sequence_version、permission_version 和 storage_snapshot_version 字段，和 SCPI `version block` / `param block` 对齐。
- [ ] 增加 DistributedVectorTable directory CRC 和 slot directory 校验。
- [ ] 实现 slot owner 写权限检查，禁止非 owner 直接写其他节点 slot。
- [ ] 实现 slot 级 snapshot API，查询只能读快照，不临时触发现场 IO。
- [ ] 实现 sequence lock 或双缓冲，避免字段半新半旧。
- [ ] 实现命令槽原子 Take/Clear，执行动作保持在临界区外。
- [ ] 将 core1 trigger status ring 合并到本节点 TriggerSlot 摘要。
- [x] 定义 `CoreVectorOwnerTable`，统一记录 core0/core1 VTOR、IRQ owner、entry owner、park/lockout 状态和恢复原因码。
  2026-08-10: 已在 DistributedVectorTable header 中加入 core0/core1 VTOR owner、IRQ owner mask、entry table owner 和 guard 字段，并通过 `SYST:CORE:VECT?` 查询。
- [x] 定义 `RuntimeProtectionTable`，把 RAM-resident section、flash lockout/park、entry table owner、realtime IRQ owner 写入表头或 slot 元素。
  2026-08-10: 已在 DistributedVectorTable header 中加入 RAM-resident、flash lockout/park 和 entry owner 状态，并通过 `SYST:PROT:STAT?` 查询。
- [x] 定义 `SystemModeTable`、`ResourceArbiterTable` 和 `FaultCodeTable` 的只读查询接口，作为产品门禁和诊断入口。
  2026-08-10: 已新增 `SYST:MODE:TAB?`、`SYST:RESource:TAB?`、`SYST:FAULT:TAB?`，分别暴露系统模式、资源仲裁和产品故障码只读表。
- [ ] 为所有共享表项统一补齐 `table_seq / slot_seq / owner / crc / stale / flags` 字段，保证反射内存口径一致。
- [ ] 将 `table_seq / slot_seq / owner / crc / stale / flags` 统一纳入 CAL/SYNC/SEQUENCE 对外响应字段，至少覆盖 `link table`、`delay table`、`sync state block`、`sync node block`、`sequence active block`、`check block` 和 `quality block`。
- [x] 增加 `SYST:REFM:STAT?` / `SYST:REFM:NODE?` 诊断命令。
  2026-08-10: P0 本地快照命令已接入，后续 stale/CRC/slot owner 完成后继续扩展字段语义。
- [ ] 增加 `OK/STALE/MISSING/INVALID/FAULT` 节点新鲜度状态和 stale window 计数。
- [ ] 将节点新鲜度状态纳入 `SYNC:CHECk`、`READ:SYNC:STATe?`、`READ:SYNC:NODE?` 和 TRIG RUN 门禁，明确 `STALE/MISSING/INVALID/FAULT` 的拒绝原因。

### P2 - 跨核通信

- [ ] 抽象 `trigger_command_queue`，替代直接暴露 TriggerAO 内部队列。
- [ ] 抽象 `trigger_status_ring`，core1 只写轻量事件，core0 负责格式化和落盘。
- [ ] 增加跨核 doorbell 作为唤醒信号，业务 payload 仍走队列。
- [ ] 为 TriggerVector snapshot 增加 sequence/version，避免查询读到撕裂状态。
- [ ] 抽象 `core_ipc_contract`，统一定义 core0/core1 mailbox、doorbell、ack、timeout 和 reset 语义。
- [ ] 实现 core1 park/lockout 握手和超时升级流程，确保 flash erase/program 前能确认 core1 已退出 XIP 取指。
- [ ] 审计 `storage_manager_trace_event()`，禁止 core1 直接调用。
- [ ] 为 core1 增加 stack/heartbeat/last_event 诊断字段。
- [ ] 拆分 core0/core1/shared 三类内存区域，至少包括 core1 私有状态 section、共享 ring/vector section 和 linker map 检查。
- [ ] 将 core1 入口、flash lockout poll 和实时快路径迁移到 RAM-resident section，确保 flash erase/program 期间 core1 不从 XIP flash 取指。
- [ ] 评估 core1 独立 RAM vector table/VTOR；只承载必要 realtime IRQ，禁止 USB/SCPI/Storage/OTA IRQ 进入 core1。
- [ ] 增加 linker map 断言，校验 core1 关键入口、lockout poll、status ring 和私有状态都落在预期 section。

### P3 - A0/A3 控制面

- [x] 建立配置门禁骨架，公开 `SYST:CFG:STAT?` / `STAT:CFG?`，冻结 build_id/hw_profile/CRC/ACK 快照。
  2026-08-10: build `20260810124902` 已烧录验证，`SYST:CFG:STAT?` 返回 build id、ready、gate_state、epoch、run_id、版本号、ACK/NACK/busy/timeout 位和 CRC 快照，service_count 持续增长。
- [x] 定义 `NodeRoleMap` 存储和 SCPI 查询接口。
  2026-08-10: `components/distributed_config/` 提供静态 `NodeRoleMap`，`SYST:CFG:ROLE? [node_id]` 可查询节点角色、persona、feature mask 和 IO base。
- [x] 定义 `LoopPlan`、`LayerAction`、`ActionMap` 的内存结构。
  2026-08-10: 静态 `LoopPlan` / `LayerAction` / `ActionMap` 已落地，`SYST:CFG:LOOP? [layer_id]` 和 `SYST:CFG:ACT? [action_id]` 可查询当前快照。
- [x] 定义 RUN 前配置一致性门禁：build_id、hw_profile、NodeRoleMap CRC、LoopPlan CRC、ActionMap CRC、Calibration CRC。
  2026-08-10: `distributed_config_validate()` 已加入本地结构一致性检查；门禁通过时 `ack_flags=target_mask`、`nack_flags=0`，失败时 `gate_state=2`。
- [x] 定义分布式命令 ACK/NACK 协议骨架：command_seq、target_mask、ack_flags、nack_flags、busy_flags、timeout_flags、nack_reason。
  2026-08-10: `SYST:CFG:ACK?` 暴露本地 command ACK 快照，`SYST:CFG:NACK? [reason_id]` 暴露 NACK reason 表。当前为本地配置门禁和表定义，真实跨板 ACK delta 同步仍放在 P4。
- [x] 把 `SystemModeTable` 和 `ResourceArbiterTable` 接到 `task_system`，让模式切换、资源占用和恢复动作都能通过统一查询返回。
  2026-08-10: 当前先接入本地 `resource_arbiter` 快照和只读表查询；完整 `task_system` 模式切换 AO 仍留作后续演进。
- [ ] 增加 `task_gateway_a3`，接收上位机配置、START/STOP 和数据查询。
- [ ] 增加 `task_loop_engine` 的 A0 扫描状态机，并按 SCPI 指令表支持 `CONFigure:TRIGger` 自动展开状态表。
- [ ] 增加 `CONFigure:ANGLe:SWEEp`、`CONFigure:ANGLe:PULSe`、`READ:ANGLe:POSition?` 和 `CONFigure:ANGLe:BPOint` 的 LoopSlot/staging/active 字段。
- [ ] 增加 `CONFigure:SEQuence`、`READ:SEQuence:MAP?`、`READ:SEQuence:CHECK?`、`CONFigure:SEQuence:ACTive` 和 `READ:SEQuence:ACTive?` 的序列库、CRC、拒绝原因和 ACK 闭环。
- [ ] 增加 `CONFigure:SWITch#` / `READ:SWITch#?` 的独立切换路径，并实现 RUN 中序列引擎占用时返回 busy。
- [ ] 冻结测试/调试双上位机边界：最低 `TEST` 权限就是现场测试程序，必须覆盖 SCPI 指令表 P5-P7 现场测试业务页，包括 RUN 前装载测试 recipe、配置触发参数、扫描角度、角度脉冲、断点续测和 active sequence，执行 `SYNC:CHECk` 门禁，启动/暂停/继续/停止测试，RUN 中只从网分取数据并保留安全停止与只读状态，RUN 后读取 `SYSTem:RUN:*`、同步状态和故障摘要；`TEST` 的限制来自 IDLE/CONFIG/ARM/PAUSE/RUN 状态边界和 profile 开关，而不是把业务指令上提到 `DEBUG`。调试上位机按 `TEST < SERVICE < DEBUG < FACTORY` 四级单调继承权限开放不同调试功能，高级权限包含低级权限全部功能；`DEBUG+` 增加任意状态强控、外设联动、异常注入、状态机推进和越过常规现场流程的验证动作，用权限 profile + 状态策略表对任意状态下的查询、控制、排队和拒绝作出决策，但必须通过 core0 控制面、资源仲裁和 ACK 闭环，不能直接影响 core1 已装载边沿。
- [x] 增加 RUN 态 SCPI 策略表和禁止命令错误码。
  2026-08-10: `SYST:SCPI:RUN:ALLOW? [index]` 暴露 RUN 态策略表；命名保留 ALLOW，但语义为权限 profile 在 RUN 状态下的执行结果，关键触发/采样/时钟/BiSS/PCNT/Storage/OTA 写入口在 ARMED/RUN 态按表返回 `RUN_STATE_DENIED:2401` 或 `RESOURCE_BUSY:2402`。
- [ ] 增加断点保存和恢复策略。

### P4 - CAL/SYNC、RJ45_SYNC_RING、反射内存同步与虚拟 DC

- [x] 增加 `task_calibration` 空壳和 `calibration_job_queue`，先支持 link/delay 表的 staging、snapshot 和计数器。
  2026-08-12: build `20260812043516` 已烧录验证，RTOS task list 显示 `calibration`；`READ:CALibration:STATe?` service_count 持续增长，`READ:CALibration:LINK?` / `READ:CALibration:PARameter?` 返回 active CRC 和 ready guard。
- [ ] 实现 `CONFigure:CALibration:LINK:ADD/SET/DELete`、`READ:CALibration:LINK?` 和 link key 去重。
- [ ] 实现 `CALibration:STARt <type,src_node,src_port,dst_node,dst_port>` 短事务骨架，失败不覆盖旧 staging delay。
- [ ] 实现 `READ:CALibration:STATe?`、`READ:CALibration:RESult?`、`READ:CALibration:PARameter?`、`READ:CALibration:VERSion?` 和 `READ:CALibration:QUALity?` 固定字段。
- [ ] 实现 `CALibration:SAVE/LOAD/ACTivate/ROLLback` 的资源仲裁、ACK/NACK 和 storage package 版本管理。
- [ ] 实现 GPIO26/27 `ring_rx_tx` PIO 原型。
- [ ] 定义 SYNC/FIRE_LOAD/DONE/MEAS_DONE/FAULT 帧格式和 CRC。
- [ ] 定义 `REFMEM_DELTA` 和 `REFMEM_EPOCH` 帧格式。
- [ ] 实现 slot delta 合并、slot_version、stale_count 和 CRC 检查。
- [ ] 实现 A3 本地镜像查询，slot stale 时返回 stale 标志而不是阻塞跨板查询。
- [ ] 实现 ACK/NACK/busy_flags 位图同步。
- [ ] 将 CAL/SYNC 的 `SAVE/ACTivate/CHECk/STARt/STOP/RELock/HOLDover` 统一接入分布式 ACK 语义，SCPI 写命令只表示 accepted，完成态通过 `command_seq/target_mask/ack_flags/nack_flags/busy_flags/timeout_flags` 查询。
- [ ] 实现 `REFMEM_DELTA`、`FIRE_LOAD`、`DONE/MEAS_DONE` 全部携带 epoch/run_id 或可回溯上下文。
- [ ] 定义 RJ45 帧级 `table_seq / slot_seq / owner / stale / crc` 头字段，统一反射内存和跨板同步的可追溯性。
- [ ] 实现 `CONFigure:SYNC:CALibration/RING/VDC:DPLL/GATE/LIMit` 的 staging 配置、profile 展开、门限覆盖和拒绝原因。
- [ ] 实现 `SYNC:CHECk/STARt/STOP/RELock/HOLDover`，完成态通过 `READ:SYNC:STATe?`、`READ:SYNC:CHECk?`、`READ:SYNC:QUALity?` 查询。
- [ ] 实现 SYNC DPLL 的 VDC offset/rate 更新、LOCK/HOLDOVER/RELOCK 和虚拟环路滤波器调试接口。
- [ ] 实现 NODE/RJ45 link delay 引入 VDC 计算，统计 `e_vdc`、crc_count、seq_error、node freshness。
- [ ] 增加四板 1e6 帧 CRC/seq/latency 验证工具。
- [ ] 增加断线、CRC 错误、乱序、late 故障注入。

### P5 - 本地预约触发与 T2 闭环

- [ ] 实现 `FIRE_LOAD` 到 core1 `local_fire` 装载。
- [ ] 实现 `delta_ticks/mask/pulse_width/polarity` 小载荷。
- [ ] 实现 late 判断，late frame 禁止补救触发。
- [ ] 实现 GPIO20..23 反序输入捕获和通道映射。
- [ ] 实现 T2/READY 捕获扩展到 LOCKED 虚拟 DC 时间戳，未 LOCKED 样本只作为调试数据。
- [ ] 实现 DEVICE/T2 校准使用 active VDC 和 active link delay 计算动作补偿 `Δt_i`。
- [ ] 实现 `e_act=T2_i-T_fire_base-Δt_i` 统计。
- [ ] 增加 `SYSTem:T2:DATA?` 分页读取，输出 `seq,node,channel,t2_tick,status,error_code,temperature` 等固定字段。
- [ ] 把 TriggerFB 的 ECC 状态转移表冻结成产品版，至少覆盖 ARM、FIRE_LOAD、READY、T2、late、FAULT、DISARM。
- [ ] 把 `TriggerActionTable` 接到触发路径，明确定义每种 role / mode / action 的装载时序和禁止条件。
- [ ] 增加 SMA_OUTx -> SMA_INx 回环自动验证脚本。

### P6 - DPLL 与整机闭环

- [ ] A0 接入转台 Compare Out。
- [ ] 实现角度预测 DPLL 状态机，输出 `T_fire_base`，并明确它不参与虚拟 DC offset/rate 收敛。
- [ ] 区分 `e_vdc`、`e_pll` 和 `e_act`，统计口径不得混用。
- [ ] 定义 HOLDOVER/RELOCK 策略：失锁、STALE、CRC 连错、RELOCK 后是否重新 ARM。
- [ ] 固化保守 HOLDOVER/RELOCK 规则：RUN 中失锁或节点 stale 后停止新增 `FIRE_LOAD`；`SYNC:RELock` 只恢复 VDC 锁定，不自动恢复 RUN，必须重新通过 ARM/START 门禁。
- [ ] 定义 `INFO/WARN/HOLDOVER/FAULT/INTERLOCK` 故障等级和统一 fault evidence 字段。
- [ ] A3 接入 VNA READY/MEAS_DONE 状态桥接。
- [ ] 实现 START 后硬件自循环，主机不逐点推进。
- [ ] 实现 A0 角度脉冲外层循环：每收到一个转台目标角度脉冲推进 `angle_index`，完整执行一次 active sequence，并在扫描完成后锁存 RUN summary。
- [ ] 将 `FaultCodeTable` 和 `SafetyFB` 接到 DPLL/Trigger/OTA 三域，统一故障等级、锁存条件和恢复路径。
- [x] 定义 BiSS 组网 HIL 回环验证脚本入口。
  2026-08-11: 新增 `tools/distributed_loopback_validate/distributed_loopback_validate.py`，默认拓扑为 A3 单外部 COM + 内部 BiSSC 组网；A4 作为内部模拟板角色，脚本只打开 A3 串口并做 SCPI preflight，真实内部帧级闭环待固件协议落地后扩展。
- [ ] 完成转台/VNA HIL 长稳测试。

### P7 - 发布门禁

- [ ] 24h 四板长稳：core1 heartbeat 不停、heap/stack 水位稳定。
- [ ] 24h 四板 DistributedVectorTable：slot 不撕裂，stale/heartbeat/CRC 统计稳定。
- [ ] SD/OTA/UI/SCPI 并发压力下，late=0 或按规则进入 HOLDOVER/FAULT。
- [ ] 故障证据落盘：CRC、seq、late、READY timeout、watchdog reset。
- [ ] 每次 RUN 保存 epoch/run_id、四板 build/hw profile、配置 CRC、校准 CRC、T2/e_act/e_vdc/e_pll 统计和最后 N 条 REFMEM/FIRE/ACK 证据。
- [ ] RUN 后报告闭环覆盖 `SYSTem:RUN:SUMMary?`、`SYSTem:RUN:LOG?`、`SYSTem:TRACe:DATA?`、`SYSTem:SNAPshot:DATA?`、`SYSTem:T2:DATA?`、`SYSTem:FAULT:LAST?`。
- [ ] 验证上电、bootloader、看门狗、通信丢失和 FAULT 下的安全默认态。
- [ ] release preset 明确 RTOS + 双核产品化门禁；单核/裸机仅保留 bring-up 路径。
- [ ] README、SCPI 命令文档、HIL 工具和生产测试流程同步更新。
  2026-08-11: BiSS 组网 HIL preflight 工具已建立，后续还需补生产测试流程和真实闭环验收矩阵。
- [ ] 给产品版发布门禁补一份固定测试矩阵：core0/core1 隔离、flash lockout、REFMEM delta、FIRE_LOAD/T2、OTA 事务、掉电恢复。
- [ ] 给 CAL/SYNC 持久化路径补 flash/storage 资源仲裁验证：`CALibration:SAVE`、`SYNC:SAVE` 和配置落盘前必须确认系统处于 `IDLE/MAINT`，且 core1 已完成 park/lockout 或后端不触发 flash erase/program。
- [ ] 给 `task_refmem_sync`、`task_calibration`、`task_vdc_sync`、`task_dpll`、`task_gateway_a3` 建立统一长稳回归用例和失败码映射。

## 当前进度与下一刀

近期 RTOS 规划按小步验证执行：

| 顺序 | 代码目标 | 板端验收 |
|---:|---|---|
| 1 | 固化当前 RTOS + core1 完整 TriggerFB 基线 | `SYST:CORE?` core1 计数增长；`TRIG:MODE 1 -> ARM -> DIS` 通过 |
| 2 | 拆 `task_io_frontend` 为 `task_usb_device` + `task_scpi` | USB/CDC/USBTMC 活性正常；`SYST:RTOS:STAT?` 显示两任务水位 |
| 3 | 建立 `task_refmem_sync` 空壳 | 本地 64 KB DistributedVectorTable header/node heartbeat 可查询 |
| 4 | 建立 `task_loop_engine` 空壳 | A0/A3 控制面入口存在，但不接真实扫描 |
| 5 | 建立 `task_calibration` 空壳 | 已完成；link/delay staging 和 `READ:CALibration:*?` 快照可查询 |
| 6 | 建立 `task_vdc_sync` 空壳 | SYNC DPLL 状态、lock 状态和计数器可查询 |
| 7 | 建立 `task_dpll` 空壳 | 角度预测 dpll 状态和计数器可查询 |
| 8 | 接入 epoch/config/sequence/cal/sync CRC/ACK 门禁 | CONFIG/ARM/START 有 accepted 与完成态区分 |
| 9 | 接入 RJ45 `REFMEM_DELTA` 和 `SYNC:CHECk` | 多板 slot delta、stale、ACK 位图、VDC LOCK 同步 |
| 10 | 接入 `FIRE_LOAD` / T2 闭环 | 分布式预约触发进入产品化路径 |

每一步必须执行：

```text
cmake build
flash UF2
board smoke
SYST:RTOS:STAT? 水位记录
LOOP:STAT? loop counter
SYSTem:SYNC:VDC:STATus? VDC counter
SYSTem:SYNC:VDC:DPLL:STATus? sync DPLL counter
READ:SYNC:STATe? / READ:CALibration:STATe? if enabled
SYST:CORE? core1 heartbeat
SYST:ERR? 错误队列确认
```

第一刀已经完成：

```text
task_io_frontend
  -> task_usb_device
  -> task_scpi
```

板端验证结果：

```text
build_id: 20260810104144
smoke:    identity/build_id/core_heartbeat/trigger_seq/error_queue 5/5 PASS
rtos:     usb_device used 32 words, scpi used 1166 words, heap min free 73584 bytes
```

第二刀已经完成：

```text
add task_refmem_sync skeleton
reserve local 64 KB DistributedVectorTable layout
publish local header/node heartbeat snapshot
```

板端验证结果：

```text
build_id: 20260810110636
smoke:    identity/build_id/core_heartbeat/trigger_seq/error_queue 5/5 PASS
refmem:   SYST:REFM:STAT? -> 65536,1,<table_seq>,0,8,<heartbeat>,<service_count>,0
refmem:   SYST:REFM:NODE? 7 -> 7,0,0,0,0,0,0,0,0
rtos:     refmem_sync used 32 words, heap min free 65288 bytes
```

第三刀已经完成：

```text
add task_dpll skeleton
publish disabled/ready state and counter snapshot
do not connect turntable Compare Out or DPLL convergence yet
```

第四刀已经完成：

```text
add task_vdc_sync skeleton
publish lock state and counter snapshot
do not connect real DC convergence yet
```

板端验证结果：

```text
build_id: 20260810132729
smoke:    identity/build_id/core_heartbeat/loop_status/vdc_status/dpll_status/config_gate_status/trigger_seq/error_queue/log_stat/trace_last 11/11 PASS
vdc:      SYSTem:SYNC:VDC:STATus? -> 1,0,<service_count>,<first_service_ms>,<last_service_ms>,<sync_seq>
dpll:     SYSTem:SYNC:VDC:DPLL:STATus? -> 1,0,<service_count>,<first_service_ms>,<last_service_ms>,<update_seq>
cfg:      SYST:CFG:STAT? -> "20260810132729",1,1,60141,3550,13605,1848,2369500348,1,1,1,1,1,15,0,0,0,0,1484595822,2475547252,577814202,2954853378,2581941186,400340093,1187728286
rtos:     task_count 11; vdc_sync/dpll/cfg_gate/ui still visible; heap min free 27968 bytes
```

`components/distributed_config/` 已落地静态 `NodeRoleMap` / `LoopPlan` / `ActionMap` /
`Calibration` 数据源和本地一致性检查，并通过 `SYST:CFG:ROLE?` / `SYST:CFG:LOOP?` /
`SYST:CFG:ACT?` / `SYST:CFG:CAL?` 暴露只读快照。下一步继续补 `CoreVectorOwnerTable`、
`RuntimeProtectionTable`、分布式 ACK/NACK 原因码和 RUN 态命令策略表，不接跨板 RJ45
同步和真实转台 DPLL 收敛。

第五刀已经完成：

```text
add CoreVectorOwnerTable snapshot
add RuntimeProtectionTable snapshot
publish VTOR owner / IRQ owner / flash lockout / park fields through DistributedVectorTable header
```

板端验证结果：

```text
build_id: 20260810151918
ota:      SYST:OTA:COMM -> "OK"; SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5
corevec:  SYST:CORE:VECT? -> 1,<table_seq>,2,0,1,15,3840,2,0,2,<guard_crc>,0,0
protect:  SYST:PROT:STAT? -> 1,<table_seq>,1,1,1,0,0,0,2,11,2,<guard_crc>,0,0
smoke:    identity/build_id/core_heartbeat/loop_status/vdc_status/dpll_status/config_gate_status/config_snapshot_queries/runtime_protection_tables/trigger_seq/error_queue/log_stat/trace_last 13/13 PASS
```

下一步继续补分布式 ACK/NACK reason、RUN 态 SCPI 策略表和 SystemMode/ResourceArbiter
查询表，不接跨板 RJ45 同步和真实转台 DPLL 收敛。

第六刀已经完成：

```text
add task_calibration skeleton
publish CAL status snapshot through READ:CALibration:STATe?/LINK?/PARameter?/HEALth?
keep calibration measurement and storage package inactive
```

板端验证结果：

```text
build_id: 20260812043516
single:   calibration_status 1/1 PASS
full:     RTOS + multicore smoke 16/16 PASS
rtos:     calibration task stack 2048 words, used 28 words
cal:      READ:CALibration:STATe? service_count 673265 -> 674940 -> 676614
cal:      active_crc 268435459, link_seq 676621, parameter_seq 676629
trigger:  TRIGger:MODE 1 -> TRIGger:STARt -> TRIGger:STOP product smoke PASS
errors:   SYST:ERR? -> 0,"No error"
archive:  build-rtos-multicore-smoke/validation_calibration_step1_full_retry
```

### SCPI 模块拆分规划

当前 `middleware/scpi_port/src/scpi_port.c` 同时承担传输端口、SCPI 命令表、系统维护命令、
历史触发命令、OTA/Storage、RTOS 状态查询和产品业务命令。这个结构在 bring-up 阶段方便，
但后续接入 `CalibrationSlot`、`VdcSlot`、`LoopSlot` 和 `AckCommandSlot` 时，会让指令 owner
和实现文件边界不一致。拆分原则是先做无行为变化的编译单元拆分，再逐步把写命令接到
对应 task/event/refmem slot。

目标文件边界：

| 文件 | owner 对齐 | 内容 |
|---|---|---|
| `scpi_port.c` | `task_scpi` / port | SCPI context、输入输出、error queue、公共系统命令表汇总、legacy 命令暂留 |
| `scpi_product_commands.c` | product common | 通用 accepted、RUN/log/page、权限、角色，以及尚未拆出的产品占位命令 |
| `scpi_config_commands.c` | `task_loop_engine` | `CONFigure:TRIGger`、`CONFigure:ANGLe:*`、`CONFigure:SEQuence:*`、`CONFigure:SWITch#`、对应 `READ:*?` |
| `scpi_calibration_commands.c` | `task_calibration` | `CONFigure:CALibration:*`、`CALibration:*`、`READ:CALibration:*?` |
| `scpi_sync_commands.c` | `task_vdc_sync` | `CONFigure:SYNC:*`、`SYNC:*`、`READ:SYNC:*?`、`SYSTem:SYNC:VDC:*` 维护查询 |
| `scpi_system_commands.c` | `task_system/storage/ota/refmem` | IEEE 488.2 以外的 `SYSTem:*` 产品系统、资源、日志、故障、存储、OTA、REFM 查询 |
| `scpi_trigger_commands.c` | `task_loop_engine` / `core1_realtime` | 产品化 `TRIGger:STARt/STOP/PAUSe/CONTinue` 与 `READ:TRIGger:*?` |
| `scpi_legacy_commands.c` | compatibility | 旧 `TRIGger:SEQ/BISS/PCNT`、裸机 bring-up 和历史验证入口，后续标记权限和废弃计划 |

拆分顺序：

| 顺序 | 动作 | 验收 |
|---:|---|---|
| 1 | 拆出 `scpi_calibration_commands.c/.h`，保留现有 CAL 响应字段和 accepted stub | 已完成；`READ:CALibration:*?`、`READ:SYNC:LINK?` 和 full smoke 通过 |
| 2 | 拆出 `scpi_sync_commands.c/.h`，保留现有 SYNC 响应字段和 accepted stub | 已完成；`READ:SYNC:*?`、`SYNC:CHECk` 和 full smoke 通过 |
| 3 | 拆出 `scpi_config_commands.c/.h`，把测试 recipe/角度/序列/SWITCH 查询从 product common 移出 | 已完成；`READ:TRIGger:PARameter?`、`READ:ANGLe:*?`、`READ:SEQuence:*?` 通过 |
| 4 | 拆出 `scpi_trigger_commands.c/.h`，让产品运行控制和历史触发命令分层 | 已完成；`TRIGger:MODE/STARt/STOP` 产品 smoke 通过 |
| 5 | 拆出 `scpi_system_commands.c/.h`，收敛 system/refmem/config/RTOS/storage/OTA 入口 | `SYSTem:*`、OTA、Storage、REFM 验证通过 |
| 6 | 建立 `scpi_legacy_commands.c/.h`，给旧验证命令集中权限和 RUN 态策略 | 旧 HIL 工具仍可用，产品指令表不依赖 legacy 入口 |
| 7 | 规划并实现 `CalibrationSlot`，把 CAL 读取从 app task snapshot 迁移为反射内存快照 | `READ:CALibration:*?` 字段不变，source 从 task snapshot 变为 slot snapshot |
| 8 | 接入 CAL link 增删改查 staging + ACK/NACK | `CONFigure:CALibration:LINK:*` accepted 后可通过 ACK 和 `READ:CALibration:LINK?` 闭环 |

每次拆分都必须执行 build、烧录、板端 smoke、水位记录和错误队列检查；验证通过后暂存代码
和文档改动，并提交形成可回退节点。

第七刀已经完成：

```text
split SCPI calibration commands into scpi_calibration_commands.c/.h
keep CAL response fields and accepted stubs unchanged
keep READ:SYNC:LINK? reusing CAL link snapshot
```

板端验证结果：

```text
build_id: 20260812045404
quick:    *IDN?/SYST:FW:BUILD?/READ:CALibration:STATe?/READ:CALibration:LINK?/READ:SYNC:LINK?/SYST:ERR? PASS
full:     RTOS + multicore smoke 16/16 PASS
cal:      READ:CALibration:STATe? -> "DONE","SMA","A0:OUT1","A1:IN1",0,0,0,0,"NONE",1,0,24460,268435459
sync:     READ:SYNC:LINK? -> 24475,1,0,268435459,0,1,"SMA","A0","OUT1","A1","IN1","BIDIR",1,1,1
errors:   SYST:ERR? -> 0,"No error"
archive:  build-rtos-multicore-smoke/validation_scpi_cal_split_step1
```

第八刀已经完成：

```text
split SCPI sync commands into scpi_sync_commands.c/.h
keep SYNC response fields and accepted stubs unchanged
keep SYNC response fields and accepted stubs unchanged
READ:STATistics? has been moved to report/statistics domain to avoid overlapping with SYNC quality
```

板端验证结果：

```text
build_id: 20260812050219
quick:    READ:SYNC:STATe?/PARameter?/HEALth?/NODE?/LINK?/CHECk?/QUALity?/VERSion? PASS
command:  SYNC:CHECk -> "PASS","ACTIVE","FIELD_DEFAULT",268435459,"FIELD_SYNC_DEFAULT",536870914,"A0>A1>A2>A3>A0",1,"","","","OK","","","","NONE"
full:     RTOS + multicore smoke 16/16 PASS
errors:   SYST:ERR? -> 0,"No error"
archive:  build-rtos-multicore-smoke/validation_scpi_sync_split_step1
```

第九刀已经完成：

```text
split SCPI config commands into scpi_config_commands.c/.h
move CONFigure:TRIGger / CONFigure:ANGLe:* / CONFigure:SEQuence:* / CONFigure:SWITch#
move READ:TRIGger:PARameter? / READ:ANGLe:*? / READ:SEQuence:*? / READ:SWITch#?
keep READ:RUN:* and READ:T2:* temporarily in product report commands
```

板端验证结果：

```text
build_id: 20260812052827
quick:    READ:TRIGger:PARameter?/READ:ANGLe:*?/READ:SEQuence:*?/READ:SWITch1?/READ:RUN:SUMMary?/READ:T2:* PASS
full:     RTOS + multicore smoke 16/16 PASS
errors:   SYST:ERR? -> 0,"No error" after error queue follow-up
archive:  build-rtos-multicore-smoke/validation_scpi_config_split_step1
```

注意：当前 `CONFigure:*` 仍是 accepted stub，不消费参数。带参数调用如
`CONFigure:TRIGger 8,2,5,1` 会先返回 `1`，随后 libscpi 留下 `-108` 参数错误。
这不是本次拆分引入的新问题，但接入 `task_loop_engine` staging 时必须同时修正：
写命令要么完整解析并投递配置事件，要么明确拒绝并给出固定 NACK/reason，不能返回 accepted
后再遗留 SCPI parser 参数错误。

第十刀已经完成：

```text
split product trigger run-control commands into scpi_trigger_commands.c/.h
move TRIGger:STARt/STOP/PAUSe/CONTinue and READ:TRIGger:STATe?
keep legacy TRIGger:MODE / TRIGger:SEQ / BISS / PCNT in scpi_port.c for now
```

板端验证结果：

```text
build_id: 20260812053420
quick:    TRIGger:MODE 1 / TRIGger:MODE? / READ:TRIGger:STATe? / TRIGger:STARt / TRIGger:STOP PASS
full:     RTOS + multicore smoke 16/16 PASS
errors:   SYST:ERR? -> 0,"No error"
archive:  build-rtos-multicore-smoke/validation_scpi_trigger_split_step1
```

第十一刀已经完成：

```text
split report placeholder commands into scpi_report_commands.c/.h
move SYSTem:RUN:* / SYSTem:LOG:PAGE? / SYSTem:TRACe:DATA? / SYSTem:SNAPshot:DATA? / SYSTem:T2:DATA?
move READ:RUN:SUMMary? / READ:T2:COUNt? / READ:T2:DATA?
keep real Storage/OTA/MMEM commands in scpi_port.c
```

板端验证结果：

```text
build_id: 20260812055925
quick:    SYSTem:RUN:LAST?/SUMMary?/LOG?, SYSTem:LOG:PAGE?, TRACe/SNAPshot/T2 DATA, READ:RUN/T2 PASS
full:     RTOS + multicore smoke 16/16 PASS
errors:   SYST:ERR? -> 0,"No error"
archive:  build-rtos-multicore-smoke/validation_scpi_report_split_step1
```

第十二刀已经完成：

```text
split system snapshot commands into scpi_system_snapshot_commands.c/.h
move REFMEM status/node, core vector, runtime protection, config gate ACK/NACK,
role/loop/action/calibration snapshots, SCPI run policy, SystemMode/Resource/Fault tables
keep SYSTem:TRIGger:DBG? and SYSTem:RESource? in scpi_port.c for local debug/static helper dependency
```

板端验证结果：

```text
build_id: 20260812060813
quick:    SYSTem:REFM/REFMem, CORE:VECTor, PROTection, CONFigure/CFG, SCPI:RUN:ALLOW, MODE/RESource/FAULT tables PASS
full:     RTOS + multicore smoke 16/16 PASS
errors:   SYST:ERR? -> 0,"No error"
archive:  build-rtos-multicore-smoke/validation_scpi_system_snapshot_split_step1
```

第十三刀已经完成，并在后续复核中做了边界修正：

```text
split LOOP engine status commands into scpi_loop_engine_commands.c/.h
move LOOP:STATus?/LOOP:STAT?/STATus:LOOP? only
move SYSTem:SYNC:VDC:STATus? and SYSTem:SYNC:VDC:DPLL:STATus? into scpi_sync_commands.c/.h
remove broad scpi_service_status_commands module to avoid overlapping with sync domain
keep SYSTem:TRIGger:DBG? and SYSTem:RESource? in scpi_port.c for local debug/static helper dependency
```

板端验证结果：

```text
build_id: 20260812062425
quick:    *IDN?/SYST:FW:BUILD?/LOOP:STAT?/VDC:STAT?/DPLL:STAT?/STATus:* aliases PASS before namespace cleanup
full:     RTOS + multicore smoke 16/16 PASS
errors:   SYST:ERR? -> 0,"No error"
archive:  build-rtos-multicore-smoke/validation_scpi_service_status_split_step1
```

边界修正验证结果：

```text
target:   build-rtos-multicore-smoke
quick:    READ:SYNC:* + SYSTem:SYNC:VDC:* + LOOP aliases
full:     RTOS + multicore smoke
archive:  build-rtos-multicore-smoke/validation_scpi_sync_loop_boundary_fix_step1
```

剩余 `scpi_port.c` 拆分规划按业务域推进，不再按“查询/状态”横切：

```text
1. scpi_system_runtime_commands.c/.h
   Scope:
     *TST?, SYSTem:FW:*, SYSTem:BOOT:VERSion?/CAPability?,
     SYSTem:LOG:LEVel/LEVel?/STATus?, SYSTem:CORE?, SYSTem:RTOS:STATus?
   Dependency:
     diagnostics, osal, ota metadata, project_config/project_build_info
   Risk:
     low, mostly read-only; LOG:LEVel is a small runtime setting
   Validation:
     IDN/FW/BOOT/LOG/CORE/RTOS/error queue + full smoke

2. scpi_io_trigger_commands.c/.h
   Scope:
     TRIGger/PULSe/MARKer/RJ45 width and immediate commands,
     SAMPle:RATE/STATe, OUTPut:CLOCk:*
   Dependency:
     sync_trigger event queue, sync_io_hw_profile, run-state policy
   Risk:
     medium, these are hardware-facing but still sync_trigger event based
   Validation:
     read back width/rate/state/pins, no trigger hang, full smoke

3. scpi_legacy_trigger_commands.c/.h
   Scope:
     TRIGger:MODE, TRIGger:SEQ:*, TRIGger:ARM/DISarm/FAULT,
     STATus:TRIGger?, ENC_COUNT legacy commands
   Dependency:
     sync_trigger state machine, storage trace/fault evidence, debug stage variables
   Risk:
     high, this area caused previous TRIG:MODE/TRIG:ARM hang; split only after
     local debug state is converted from scpi_port static globals to a small context API
   Validation:
     MODE/SEQ/ARM/DISarm/FAULT/debug path, repeated SSCOM-equivalent queries, full smoke

4. scpi_biss_commands.c/.h
   Scope:
     TRIGger:BISS:* and STATus:BISS?
   Dependency:
     BISS protocol constants, trigger vector/event queue, run-state policy
   Risk:
     medium-high, many parameters; split first with no behavior change, parser cleanup later
   Validation:
     representative BISS config/readback/status/error queue + full smoke

5. scpi_pcnt_commands.c/.h
   Scope:
     TRIGger:PCNT:* counter decode/direction/filter/gate/cmp/preset/clear/readback
   Dependency:
     trigger vector/event queue, run-state policy
   Risk:
     medium, compact and separable from BISS
   Validation:
     PCNT readback/defaults + error queue + full smoke

6. scpi_ota_commands.c/.h
   Scope:
     SYSTem:OTA:* including package begin/data/end/abort/boot/commit/status/result
   Dependency:
     ota_ao, ota metadata, run-state policy, optional fault injection
   Risk:
     medium-high after split; update path and arbitrary block input remain high-value validation targets
   Validation:
     status/progress/slot/result/txn/mode/target/capability first; write path separately gated
   Status:
     done 2026-08-12: SYSTem:OTA:* moved from scpi_port.c to scpi_ota_commands.c/.h,
     with shared scpi_port_internal.h helpers for read_u32/result_ok/run-state rejection.
     Verified by build-rtos-multicore-smoke and release_check.

7. scpi_storage_commands.c/.h
   Scope:
     SYSTem:SD:*, SYSTem:STORage:*, SNAPshot/TRACE/FAULT last, MMEMory:*
   Dependency:
     storage_manager, FATFS, SD raw ops, resource/run-state policy, job wait helper
   Risk:
     high, file system and potentially destructive maintenance commands
   Validation:
     status/info/manifest/job/MMEM read-only first; raw clear/MKFS only with explicit confirm tests

8. scpi_measure_commands.c/.h
   Scope:
     MEASure:FREQuency?, MEASure:REPort?
   Dependency:
     trigger_measure
   Risk:
     low, read-only self-test data
   Validation:
     frequency/report query + full smoke
```

长期收敛目标：`scpi_port.c` 只保留 libscpi 上下文、输入输出、错误队列、reset/flush/control
以及命令表拼装；业务命令全部进入清晰的业务域模块。共享能力如 `read_u32`、`result_ok`、
`run_forbidden`、`trigger_post`、`storage_job_wait` 应拆成窄接口 helper，而不是让各模块反向依赖
`scpi_port.c` 的 static 函数。
