# DTC100 SCPI 指令规划方案

Status: Draft
Domain: SCPI
Canonical: `docs/DTC100_SCPI_COMMAND_PLANNING.md`
Related: `docs/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`, `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`, `docs/SCPI_COMMANDS.md`
Last updated: 2026-08-12

## 1. 目标

本文用于重新规划 DTC100 的 SCPI 指令架构，先形成可评审的产品化分层方案，再同步修改正式指令表、HTML、固件命令表和验证工具。

本次规划的核心目标：

- 借鉴网分、信号源、示波器等仪器仪表的 SCPI 架构，按仪器功能和操作模型组织指令。
- 避免按内部 RTOS task、临时变量、算法名直接暴露产品指令。
- 明确 `SYNC` 域如何挂载到 DTC100 主控制面。
- 明确 `VDC` 和 `DPLL` 的层级关系，避免二者在指令树中平级混用。
- 为后续拆分 `scpi_port.c`、规划反射内存和上位机开发提供稳定接口边界。

## 2. 仪器式 SCPI 参考原则

典型仪器 SCPI 会按“仪器功能块 + 操作阶段”划分，而不是按固件线程或内部模块划分：

| 仪器式域 | 常见含义 | DTC100 对应 |
|---|---|---|
| `*` | IEEE 488.2 通用指令 | 识别、自检、状态字节、错误清理 |
| `SYSTem` | 系统、错误、维护、资源和诊断 | 版本、日志、RTOS、水位、资源、故障、维护调试 |
| `CONFigure` | 建立一次运行或测量所需配置 | 触发参数、角度、序列、开关、校准绑定、同步配置 |
| `TRIGger` | 触发模型和运行控制 | START/STOP/PAUSE/CONTINUE、运行模式 |
| `READ` / `FETCh` | 读取结果、状态和报告数据 | 运行状态、同步状态、T2、统计、报告 |
| `MMEMory` | 文件系统和数据块 | SD、日志、trace、snapshot、导出 |
| `STATus` | 标准状态寄存器/状态模型 | 后续如实现 SCPI status register，可独立规划 |

DTC100 不需要机械照搬 `SENSe/SOURce/INITiate/FETCh`，但应吸收其架构思想：

- 配置和执行分离。
- accepted 和完成态分离。
- 产品主视图和维护诊断分离。
- 查询读取快照，不临时跨线程抓内部状态。
- 写命令跨核、跨节点、持久化时，必须有 ACK/NACK 或 `READ/SYSTem` 闭环。

## 3. DTC100 主命令线

DTC100 的 SCPI 主线只有一条：

```text
USBTMC / USB488
  -> task_usb_device
  -> task_scpi
  -> libscpi command table
  -> domain command macros
  -> owner task / event queue / snapshot slot
```

任何业务域都不另开通信主线。`SYNC`、`CALibration`、`TRIGger`、`CONFigure` 都只是主命令表上的域挂载。

固件结构应收敛为：

```text
scpi_port.c
  - libscpi context
  - input/output/error queue
  - common command table assembly
  - no business implementation

scpi_<domain>_commands.c/.h
  - command callbacks for one product domain
  - parse parameters
  - return accepted or read snapshot
  - post event to owner task when needed

owner task / component
  - owns facts
  - writes vector table / snapshot
  - produces ACK/NACK
```

## 4. 指令域规划

| 域 | 产品定位 | 执行 owner | 快照/闭环 | 说明 |
|---|---|---|---|---|
| `*` / SCPI core | 仪器通用控制 | `task_scpi` / libscpi | SCPI status/error queue | IEEE 488.2 基础能力 |
| `SYSTem` | 系统和维护 | `task_system`、`task_storage`、`task_ota` | SystemSlot、ResourceSlot、FaultSlot、日志 | 系统状态、权限、资源、故障、日志、OTA、SD |
| `CONFigure:TRIGger/ANGLe/SEQuence/SWITch` | 现场测试配置 | `task_loop_engine` | LoopSlot、IoSlot、ACK/NACK | 构造测试 recipe 和点内序列 |
| `TRIGger` | 测试运行控制 | `task_system` + `task_loop_engine` | SystemSlot、LoopSlot、TriggerSlot、ACK/NACK | START/STOP/PAUSE/CONTINUE |
| `READ:TRIGger/ANGLe/SEQuence/SWITch` | 测试运行读取 | `task_loop_engine` | LoopSlot、TriggerSlot、IoSlot | 产品上位机主视图 |
| `CONFigure:CALibration` / `CALibration` | 校准配置和动作 | `task_calibration` | CalibrationSlot、ACK/NACK、storage | 链路 delay、T2/DEVICE 校准、版本管理 |
| `READ:CALibration` | 校准读取 | `task_calibration` | CalibrationSlot | 校准状态、链路、参数、质量、版本 |
| `CONFigure:SYNC` / `SYNC` | 同步配置和动作 | `task_vdc_sync` | VdcSlot、NodeSlot、StatisticsSlot、ACK/NACK | 建立共同虚拟 DC 和同步门禁 |
| `READ:SYNC` | 同步读取 | `task_vdc_sync` | VdcSlot、NodeSlot、StatisticsSlot | 产品上位机主视图 |
| `READ:STATistics` | 跨域统计摘要 | `task_diag` / `task_storage` | StatisticsSlot、run summary | 报告统计，不挂在 SYNC 域内部 |
| `MMEMory` / storage | 文件和数据块 | `task_storage` | storage job | SD、trace、snapshot、报告分页 |
| `REALtime` | 底层实时维护和 validation | `core1_realtime` / TriggerAO / IO profile | TriggerVector、IoSlot、维护快照 | PCNT、ENC、SEQ_STEP、即时 IO、内部状态查询；不进入产品测试主流程 |

### 4.1 REALtime 主域定位

`REALtime:*` 是 DTC100 的底层实时维护主域，用于开发、产测、服务和调试上位机验证 core1
实时基础组件。它不属于现场测试上位机的产品运行主链路，也不替代 `CONFigure:*`、`TRIGger:*`
和 `READ:*?` 的业务接口。

`REALtime` 主域承载以下类型的能力：

- `REALtime:PCNT:*`：转台角度阈输入脉冲计数、方向、滤波、门控、比较和计数快照。
- `REALtime:ENC:*`：编码器计数触发配置、目标计数和实时观测。
- `REALtime:SEQ:*`：core1 点内序列基础组件验证，包括长度、宽度、索引和序列数据。
- `REALtime:IO:*`：即时输出、脉冲、marker、RJ45、采样和输出时钟验证。
- `REALtime:ARM/DISarm/DISAble/FAULT`：底层实时执行层门禁、停车和故障注入验证。
- `REALtime:STATus?`：实时核心维护快照，供调试工具确认底层状态，不作为产品报告字段。

命名原则：

- 产品 `TRIGger:*` 只表达一次业务 run 的启动、停止、暂停、继续和模式切换。
- `REALtime:*` 内部不再新增 `TRIGger` 子树；底层 pulse count、sequence step、IO fire 和
  core1 gate 都应放在具名实时子域下。
- 已存在的旧 `TRIGger:PCNT/ENC/SEQ`、裸 `PULSe/MARKer/RJ45/SAMPle/OUTPut` 和
  `STATus:TRIGger?` 仅作为 legacy validation alias 保留，验证脚本和新文档不再以它们作为主入口。
- 若后续需要暴露 BiSS-C 通信验证，应进入 `COMMunication:BISS:*` 或独立维护页，不挂到产品
  `TRIGger:*` 下。

## 5. 测试业务域规划

测试业务域是 DTC100 面向现场测试上位机的主流程，不应混入校准、同步调参、存储维护和内部调试。它的仪器式模型是：

```text
CONFigure business recipe
  -> SYNC/CAL gate check
  -> TRIGger run control
  -> READ business state
  -> SYSTem/READ report
```

### 5.1 业务域对象

| 对象 | 产品含义 | 主要指令 | owner | 快照 |
|---|---|---|---|---|
| TriggerParam | 点内触发参数 | `CONFigure:TRIGger` / `READ:TRIGger:PARameter?` | `task_loop_engine` | LoopSlot / ConfigSlot |
| AngleSweep | 扫描角度集合和步长 | `CONFigure:ANGLe:SWEEP` / `READ:ANGLe:SWEEP?` | `task_loop_engine` | LoopSlot |
| AngleBreakpoint | 运动角度断点 | `CONFigure:ANGLe:BREAkpoint` / `READ:ANGLe:BREAkpoint?` | `task_loop_engine` | LoopSlot / storage |
| SequencePlan | 每个角度点内部状态序列 | `CONFigure:SEQuence:*` / `READ:SEQuence:*?` | `task_loop_engine` | SequenceSlot |
| SwitchAction | SP8T/SP2T 独立动作或序列动作 | `CONFigure:SWITch#` / `READ:SWITch#?` | `task_loop_engine` / core1 | IoSlot / ActionSlot |
| RunControl | START/STOP/PAUSE/CONTINUE | `TRIGger:*` / `READ:TRIGger:STATe?` | `task_system` + `task_loop_engine` | SystemSlot / LoopSlot |
| RunReport | RUN 后摘要证据 | `READ:RUN:*?` / `SYSTem:RUN:*?` | `task_storage` / `task_loop_engine` | RunSummary / trace |

### 5.2 业务域层级

业务配置全部走 `CONFigure`，运行控制全部走 `TRIGger`，状态和报告读取走 `READ` 或 `SYSTem:RUN`：

```scpi
CONFigure:TRIGger <channel_count>,<pol>,<freq_count>,<wave_count>
CONFigure:ANGLe:SWEEP <start_deg>,<stop_deg>,<step_deg>
CONFigure:ANGLe:BREAkpoint <breakpoint_id>,<angle_deg>[,<enable>]
CONFigure:SEQuence:MAP <state_id>,<switch1>,<switch2>,<pol>,<freq>,<wave>
CONFigure:SEQuence:ACTive <plan_id>,<state_id1>,<state_id2>,...
CONFigure:SWITch# <position>

TRIGger:MODE <IDLE|TRIG|CAL|SYNC|SIM>
TRIGger:STARt [plan_id]
TRIGger:STOP
TRIGger:PAUSe
TRIGger:CONTinue

READ:TRIGger:STATe?
READ:TRIGger:PARameter?
READ:ANGLe:POSition?
READ:ANGLe:PULSe?
READ:ANGLe:SWEEP?
READ:ANGLe:BREAkpoint?
READ:SEQuence:MAP?
READ:SEQuence:ACTive?
READ:SEQuence:STATe?
READ:SWITch#?
READ:RUN:SUMMary?
```

### 5.3 业务域规则

- `CONFigure:TRIGger` 只定义自动展开状态表所需维度，不直接启动测试。
- `CONFigure:SEQuence:MAP` 定义 `state_id -> switch1/switch2/pol/freq/wave` 的映射。
- `CONFigure:SEQuence:ACTive` 只引用已经存在的 state_id，形成点内执行顺序。
- `CONFigure:ANGLe:SWEEP` 定义扫描角度范围和步长；`CONFigure:ANGLe:BREAkpoint` 定义运动断点。
- 断点是运动角度断点，不保存点内序列位置；断点恢复时该角度点内部序列全部重测。
- `CONFigure:SWITch#` 是独立维护/调试切换；RUN 中若序列引擎占用开关资源，应 busy/deny。
- `TRIGger:STARt` 只消费 active config、active sequence、active calibration、active sync 和权限/资源门禁的冻结快照。
- RUN 中测试上位机主要读取网分数据，DTC100 只保留安全停止和只读证明入口。
- T2 明细是同步/校准质量证据和报告数据，不放在业务域；上位机通过 `SYSTem:T2:*?` 或后续报告/存储分页接口读取。

## 6. 校准域规划

校准域的目标是维护“固定链路 delay”和“设备动作补偿”的事实源，为 SYNC/VDC 和业务预测分发提供可信时间基准。校准不是持续运行主流程，也不直接生成测试序列。

### 6.1 校准对象

| 对象 | 产品含义 | 主要指令 | owner | 快照 |
|---|---|---|---|---|
| CalLink | 节点/端口之间的物理链路 | `CONFigure:CALibration:LINK:*` / `READ:CALibration:LINK?` | `task_calibration` | CalibrationSlot |
| CalParameter | 链路 delay、jitter、count、valid | `CONFigure:CALibration:PARameter` / `READ:CALibration:PARameter?` | `task_calibration` | CalibrationSlot |
| CalMeta | 版本追溯信息 | `CONFigure:CALibration:META` / `READ:CALibration:VERSion?` | `task_calibration` / storage | CalibrationSlot / storage |
| CalTransaction | 快速测一段 link delay | `CALibration:STARt` / `READ:CALibration:STATe?` | `task_calibration` | CalibrationSlot / result |
| CalProfile | staging/active/rollback | `CALibration:SAVE/LOAD/ACTivate/ROLLback` | `task_calibration` / storage | ACK / storage |
| CalHealth | 校准质量门禁 | `READ:CALibration:HEALth?` | `task_calibration` | CalibrationSlot / StatisticsSlot |

### 6.2 校准域层级

校准配置走 `CONFigure:CALibration:*`，校准动作走 `CALibration:*`，校准读取走 `READ:CALibration:*?`：

```scpi
CONFigure:CALibration:LINK:ADD <link_id>,<src_node>,<src_port>,<dst_node>,<dst_port>,<type>,<required>
CONFigure:CALibration:LINK:DELete <link_id>
CONFigure:CALibration:LINK:UPDate <link_id>,<field>,<value>
CONFigure:CALibration:PARameter <link_id>,<delay_ns>,<jitter_ns>,<count>,<valid>
CONFigure:CALibration:META <cal_id>,<operator>,<fixture_id>,<cable_id>,<temperature_c>,<note>

CALibration:STARt <src_node>,<src_port>,<dst_node>,<dst_port>
CALibration:STOP
CALibration:SAVE <cal_id>
CALibration:LOAD <cal_id>
CALibration:ACTivate <cal_id>
CALibration:ROLLback
CALibration:CLEAr

READ:CALibration:STATe?
READ:CALibration:LINK? [link_id|src_node,dst_node]
READ:CALibration:PARameter? [link_id]
READ:CALibration:HEALth? [domain]
READ:CALibration:VERSion?
```

### 6.3 校准域规则

- 校准表分 staging 和 active；`CONFigure:CALibration:*` 只写 staging。
- `CALibration:SAVE` 只保存 staging，不自动切换 active。
- `CALibration:ACTivate` 切换 active 后必须清除最近一次 `SYNC:CHECk` 结果，要求重新检查同步。
- `CALibration:STARt` 必须显式带输入和输出端，例如 `A0,OUT1,A1,IN1`，避免不知道测的是哪一段。
- NODE 链路用于 RJ45 触发/回传链路，SMA/DEVICE 链路用于外部仪表动作补偿；二者可以共享字段结构，但 domain 必须明确。
- 校准事务应短、快、可重复；失败不能覆盖旧 active 参数。
- 校准数据的增删改查必须记录版本、CRC、时间戳、操作者、夹具/线缆信息。
- SYNC 只接受 active 且方向匹配的 NODE link；DEVICE/T2 校准应在 VDC LOCKED 后进行。
- RUN 中禁止修改 active 校准表；必要时进入 MAINT/IDLE 后操作。

### 6.4 校准与 SYNC 的关系

```text
CALibration active NODE link
  -> SYNC:CHECk topology and delay gate
  -> SYNC:STARt VDC lock
  -> DEVICE/T2 calibration on locked VDC
  -> TRIGger:STARt prediction and fire load
```

校准域提供固定链路 delay 和质量证明；同步域基于 active 校准表建立 VDC；业务域基于 active VDC 和 T2/动作补偿执行预测分发。

## 7. SYNC 域挂载方案

`SYNC` 是主命令线上的一个业务域，挂载关系如下：

```text
SCPI command table
  -> SCPI_SYNC_COMMANDS
  -> scpi_sync_commands.c
  -> sync_control_queue / task_vdc_sync
  -> VdcSlot / NodeSlot / StatisticsSlot / ACK
```

### 7.1 SYNC 域四层

| 层级 | 指令前缀 | 作用 | 上位机定位 |
|---|---|---|---|
| 配置层 | `CONFigure:SYNC:*` | 写 staging 同步配置 | 测试上位机可在 RUN 前使用部分配置；SERVICE 可维护 |
| 动作层 | `SYNC:*` | check/start/stop/relock/holdover/save/load/activate/rollback | 动作 accepted，完成态靠 ACK/READ |
| 产品读取层 | `READ:SYNC:*?` | 读取同步状态、参数、健康度、节点、链路、质量和版本 | 测试上位机和报告系统主入口 |
| 维护诊断层 | `SYSTem:SYNC:*` | 内部服务水位、VDC/DPLL 调试、工程诊断 | SERVICE/DEBUG/FACTORY |

### 7.2 SYNC 不承担的职责

- 不直接产生 TRIG 边沿。
- 不直接推进测试序列。
- 不替代 `TRIGger:STARt/STOP`。
- 不承担全局统计报告入口。
- 不暴露 RTOS task 名称作为产品命令。
- 不在 RUN 中修改 active 时间基准。

## 8. VDC 与 DPLL 层级

建议将 `VDC` 定义为 SYNC 域的产品对象，将 `DPLL` 定义为实现 VDC 的内部控制算法。

层级关系：

```text
SYNC
  -> VDC               product object: virtual distributed clock
      -> DPLL          implementation/control loop for VDC offset/rate
```

含义：

- `VDC` 是上位机需要确认的产品事实：是否 LOCKED、是否 HOLDOVER、e_vdc 是否达标、节点是否新鲜。
- `DPLL` 是 VDC 的实现机制：根据同步帧时间戳、seq、CRC、链路 delay 和节点 age 估计 offset/rate。
- `DPLL` 不应与 `VDC` 平级暴露，也不应与扫描角度预测 DPLL 混用。
- 扫描/转台角度预测 DPLL 属于触发/扫描域，负责 `T_fire_base`，不是 `SYNC:VDC` 的 owner。

### 8.1 推荐命令层级

产品配置：

```scpi
CONFigure:SYNC:CALibration <cal_id>,<cal_crc>,<max_age_s>
CONFigure:SYNC:RING <origin>,<node_order>,<period_us>,<bitrate>,<timeout_ms>,<crc_limit>
CONFigure:SYNC:VDC:DPLL <lock_window_ns>,<lock_count>,<holdover_ms>,<relock_ms>,<profile>
CONFigure:SYNC:GATE <required_lock>,<max_age_ms>,<max_evdc_p99_ns>,<allow_holdover>
CONFigure:SYNC:LIMit <profile>[,<key=value>[,...]]
```

同步动作：

```scpi
SYNC:CHECk [ACTive|STAGing]
SYNC:STARt
SYNC:STOP
SYNC:RELock
SYNC:HOLDover 0|1
SYNC:SAVE <sync_id>[,scope]
SYNC:LOAD <sync_id>
SYNC:ACTivate <sync_id>
SYNC:ROLLback
```

产品读取：

```scpi
READ:SYNC:STATe?
READ:SYNC:PARameter?
READ:SYNC:HEALth?
READ:SYNC:NODE? [node]
READ:SYNC:LINK? [src_node,dst_node]
READ:SYNC:CHECk?
READ:SYNC:QUALity? [sync_id]
READ:SYNC:VERSion?
READ:SYNC:LIST?
READ:SYNC:ACTive?
```

维护诊断：

```scpi
SYSTem:SYNC:VDC:STATus?
SYSTem:SYNC:VDC:DPLL:STATus?
SYSTem:SYNC:VDC:DPLL:TUNE <bandwidth_hz>,<damping>,<max_slew_ppm>
SYSTem:SYNC:VDC:DPLL:COEFficient <kp_q31>,<ki_q31>,<max_slew_ppm>
SYSTem:SYNC:VDC:DPLL:OVERRide?
SYSTem:SYNC:VDC:DPLL:COEFficient?
SYSTem:SYNC:VDC:DPLL:DEFAult
```

### 8.2 不推荐命令

以下命令不建议作为新产品接口继续扩展：

```scpi
VDC:STAT?
DPLL:STAT?
STATus:VDC?
STATus:DPLL?
CONFigure:SYNC:DPLL
SYSTem:SYNC:DPLL:*
```

其中 `CONFigure:SYNC:DPLL` 和 `SYSTem:SYNC:DPLL:*` 的问题是少了 `VDC` 层，容易让上位机误以为 DPLL 是 SYNC 的产品对象，而不是 VDC 的实现环路。

## 9. 配置、动作、读取的闭环模型

所有复杂写命令必须区分 accepted 和完成态：

```text
CONFigure:SYNC:* / SYNC:*
  -> SCPI returns 1 or OK
  -> owner task consumes event
  -> owner writes slot and ACK/NACK
  -> host reads READ:SYNC:*? or SYSTem:COMMand:ACK?
```

示例：

```scpi
CONFigure:SYNC:CALibration FIELD_20260811,3A91C027,86400
CONFigure:SYNC:RING A0,A0>A1>A2>A3>A0,1000,12500000,20,0
CONFigure:SYNC:VDC:DPLL 300,100,200,1000,LOW_JITTER
CONFigure:SYNC:GATE 1,50,100,0
SYNC:CHECk STAGing
SYNC:SAVE FIELD_SYNC_20260811
SYNC:ACTivate FIELD_SYNC_20260811
SYNC:CHECk ACTive
SYNC:STARt
READ:SYNC:STATe?
READ:SYNC:QUALity?
```

## 10. 与反射内存的关系

SCPI 指令不直接读写零散全局变量，统一通过反射内存或 owner snapshot 闭环。

建议对应：

| 指令视图 | 反射内存/快照 |
|---|---|
| `READ:SYNC:STATe?` | `VdcSlot` + `SystemSlot` |
| `READ:SYNC:PARameter?` | `SyncConfigSlot` / `ConfigSlot` |
| `READ:SYNC:HEALth?` | `StatisticsSlot` + `VdcSlot` |
| `READ:SYNC:NODE?` | `NodeSlot` |
| `READ:SYNC:LINK?` | `CalibrationSlot` + sync topology view |
| `READ:SYNC:CHECk?` | `SyncCheckSlot` / ACK reason |
| `SYSTem:SYNC:VDC:STATus?` | `task_vdc_sync` service snapshot |
| `SYSTem:SYNC:VDC:DPLL:*?` | VDC DPLL diagnostic snapshot |

## 11. 权限与运行态策略

| 指令层级 | TEST | SERVICE | DEBUG | FACTORY |
|---|---:|---:|---:|---:|
| `READ:SYNC:*?` | 允许 | 允许 | 允许 | 允许 |
| `SYNC:CHECk` | RUN 前允许 | 允许 | 允许 | 允许 |
| `SYNC:STARt/STOP/RELock/HOLDover` | 按状态策略 | 允许 | 允许 | 允许 |
| `CONFigure:SYNC:*` | RUN 前有限允许 | 允许 | 允许 | 允许 |
| `SYNC:SAVE/LOAD/ACTivate/ROLLback` | 禁止 | 允许 | 允许 | 允许 |
| `SYSTem:SYNC:VDC:STATus?` | 只读可选 | 允许 | 允许 | 允许 |
| `SYSTem:SYNC:VDC:DPLL:*` | 禁止 | 查询允许 | 允许 | 允许 |

RUN 中原则：

- 允许 `READ:SYNC:*?`。
- 允许安全停止相关动作。
- 禁止修改 active sync、active calibration、DPLL 调试覆盖和质量门限。
- HOLDOVER/RELOCK 不自动恢复 TRIG RUN，必须重新经过 ARM/START 门禁。

## 12. 与当前代码的收敛步骤

建议按以下顺序执行，每一步都要 build、烧录、串口快测、full smoke，并更新文档：

1. 冻结本文作为规划稿。
2. 修改正式 SCPI 指令表 Markdown，使 SYNC/VDC/DPLL 层级与本文一致。
3. 同步 HTML 和 PDF。
4. 固件命令表移除裸顶层 `VDC:*`、`DPLL:*`、`STATus:VDC/DPLL?` 的新验证依赖。
5. 固件将底层实时验证入口迁移到 `REALtime:*` 维护域；旧 `TRIGger:PCNT/ENC/SEQ`、裸 `PULSe/MARKer/RJ45/SAMPle/OUTPut`、`STATus:TRIGger?` 只作为 legacy validation alias，必须标注 deprecated。
6. `READ:STATistics?` 从 SYNC 模块迁到 report/statistics 模块。
7. `scpi_sync_commands.c/.h` 只保留 SYNC 域和 `SYSTem:SYNC:VDC:*` 维护入口。
8. `task_vdc_sync` 后续通过 sync_control_queue 消费动作，不由 SCPI callback 直接改状态。
9. 将 `READ:SYNC:*?` 的响应来源迁到 `VdcSlot/NodeSlot/StatisticsSlot`。
10. 增加 `SYSTem:COMMand:ACK?` 或复用现有 ACK 查询，统一完成态闭环。
11. 产品 `TRIGger:*` 只保留运行控制语义；低层 `ARM/DISarm/FAULT` 验证路径迁入 `REALtime:*` 或维护权限 alias。

## 13. 当前建议结论

短期推荐冻结以下命名：

```text
CONFigure:SYNC:VDC:DPLL
SYSTem:SYNC:VDC:STATus?
SYSTem:SYNC:VDC:DPLL:STATus?
SYSTem:SYNC:VDC:DPLL:TUNE
SYSTem:SYNC:VDC:DPLL:COEFficient
SYSTem:SYNC:VDC:DPLL:OVERRide?
SYSTem:SYNC:VDC:DPLL:COEFficient?
SYSTem:SYNC:VDC:DPLL:DEFAult
```

短期推荐避免：

```text
VDC:*
DPLL:*
STATus:VDC?
STATus:DPLL?
SYSTem:SYNC:DPLL:*
CONFigure:SYNC:DPLL
```

这样可以让 DTC100 的 SCPI 看起来像一台分布式触发仪器，而不是一个 RTOS 内部调试 shell。

## 14. 建议指令树

以下指令树是规划稿的冻结候选，不代表所有命令都已实现。后续正式指令表、HTML、固件命令表和验证工具应以本树为目标逐步收敛。

```text
*
  *IDN?
  *RST
  *CLS
  *TST?
  *OPC / *OPC?
  *WAI
  *STB?
  *ESR?
  *ESE / *ESE?
  *SRE / *SRE?

SYSTem
  :VERSion?
  :ERRor?
  :ERRor:COUNt?
  :FW
    :VERSion?
    :BUILD?
  :BOOT
    :VERSion?
    :CAPability?
  :RTOS
    :STATus?
    :TASK?
    :HEAP?
  :CORE?
  :COMMand
    :ACK?
    :LAST?
    :NACK?
  :CONFigure
    :SEQuence
      :CHECk?
  :FAULT
    :LAST?
    :CLEAr
    :TABle?
  :LOG
    :LEVel / :LEVel?
    :STATus?
    :PAGE?
    :CLEAr
  :RUN
    :LAST?
    :SUMMary?
    :LOG?
  :LOOP
    :STATus?
  :T2
    :COUNt?
    :DATA?
    :CLOCK?
  :TRACe
    :DATA?
    :LAST?
  :SNAPshot
    :DATA?
    :WRITe
    :LAST?
  :REFMem
    :STATus?
    :NODE?
    :VECTor?
  :SYNC
    :VDC
      :STATus?
      :DPLL
        :STATus?
        :TUNE
        :COEFficient / :COEFficient?
        :OVERRide?
        :DEFAult
  :SCPI
    :RUN
      :ALLOW?
  :USB
    :MODE / :MODE?
  :SD
    :STATus?
    :INFO?
    :MANifest?
    :INITialize
  :STORage
    :STATus?
    :JOB?
    :JOB:INFO?
  :OTA
    :STATus?
    :PROGress?
    :BEGIN
    :DATA
    :END
    :ABORt
    :BOOT
    :COMMit
    :SLOT?
    :RESult?

CONFigure
  :TRIGger
  :ANGLe
    :SWEEP
    :BREAkpoint
      :ADD
      :DELete
      :CLEAr
  :SEQuence
    :MAP
    :MAP:DELete
    :MAP:CLEAr
    :ACTive
  :SWITch#
  :CALibration
    :LINK
      :ADD
      :DELete
      :UPDate
      :CLEAr
    :PARameter
    :META
  :SYNC
    :CALibration
    :RING
    :VDC
      :DPLL
    :GATE
    :LIMit

TRIGger
  :MODE / :MODE?
  :STARt
  :STOP
  :PAUSe
  :CONTinue
  :ABORt

CALibration
  :STARt
  :STOP
  :SAVE
  :LOAD
  :ACTivate
  :ROLLback
  :CLEAr

SYNC
  :CHECk
  :STARt
  :STOP
  :RELock
  :HOLDover
  :SAVE
  :LOAD
  :ACTivate
  :ROLLback

READ
  :TRIGger
    :STATe?
    :PARameter?
  :ANGLe
    :POSition?
    :PULSe?
    :SWEEP?
    :BREAkpoint?
  :SEQuence
    :MAP?
    :ACTive?
    :STATe?
  :SWITch#?
  :RUN
    :SUMMary?
    :PROGress?
  :CALibration
    :STATe?
    :LINK?
    :PARameter?
    :HEALth?
    :VERSion?
    :LIST?
    :ACTive?
  :SYNC
    :STATe?
    :PARameter?
    :HEALth?
    :NODE?
    :LINK?
    :CHECk?
    :QUALity?
    :VERSion?
    :LIST?
    :ACTive?
  :STATistics?

MMEMory
  :CATalog?
  :CATalog:PAGE?
  :INFO?
  :READ?
  :WRITe
  :DELete

REALtime
  :STATus?
  :PCNT
    :DECode / :DECode?
    :DIRection / :DIRection?
    :FILTer / :FILTer?
    :GATE / :GATE?
    :CMP / :CMP?
    :PRESet / :PRESet?
    :CLEar
    :TOTal?
    :FREQuency?
  :ENC
    :TARGet / :TARGet?
    :COUNt?
    :APIN / :APIN?
    :REVolution?
  :SEQ
    :LENGth / :LENGth?
    :WIDTh / :WIDTh?
    :INDex?
    :DATA / :DATA?
  :SOURce / :SOURce?
  :EDGE / :EDGE?
  :GATE / :GATE?
  :SAFE / :SAFE?
  :ARM
  :DISarm
  :DISAble
  :FAULT
  :IO
    :OUTPut
      :WIDTh / :WIDTh?
      :IMMediate
    :PULSe
      :WIDTh / :WIDTh?
      :IMMediate
    :MARKer
      :WIDTh / :WIDTh?
      :IMMediate
    :RJ45
      :WIDTh / :WIDTh?
      :IMMediate
      :PINs?
    :SAMPle
      :RATE / :RATE?
      :STATe / :STATe?
    :CLOCk
      :FREQuency / :FREQuency?
      :STATe / :STATe?
    :SYNC?
```

### 14.1 指令树收敛说明

- `CONFigure:*` 只负责写入 staging 配置，不直接开始测试、校准或同步。
- `TRIGger:*` 是测试运行控制树，负责业务 run 的 start/stop/pause/continue 和运行模式切换。
- `CALibration:*` 是校准动作树，负责快速测链路 delay、保存、加载、激活、回滚和清空。
- `SYNC:*` 是同步动作树，负责基于 active 校准表进行 check/start/stop/relock/holdover。
- `READ:*?` 是产品读取树，优先服务上位机主界面和报告闭环。
- `SYSTem:*` 是系统、维护、诊断和证据树；`SYSTem:T2:*?` 不属于业务域。
- `SYSTem:SYNC:VDC:*` 是维护诊断树，不是产品测试主流程树。
- `CONFigure:SYNC:VDC:DPLL` 是产品配置树，用于选择 VDC DPLL profile 和门限，不直接调试系数。
- `REALtime:*` 是底层实时维护和 validation 树，用于 PCNT、ENC、SEQ_STEP、即时 IO、TriggerVector 快照和低层 ARM/DISarm/FAULT 验证，不作为现场测试上位机主流程 API。
- 产品 `TRIGger:*` 不再承载底层 `SEQ_STEP/ENC/PCNT/PIO` 验证命令；这类能力必须从 `REALtime:*` 或 maintenance/legacy alias 进入。
- 裸 `VDC:*`、裸 `DPLL:*`、`STATus:VDC?`、`STATus:DPLL?` 不进入建议指令树。
- 旧的编码器、BiSS、PCNT、PIO 单项验证命令不进入产品主树；需要保留时应放入 `REALtime:*`、`COMMunication:BISS:*`、维护权限或独立 validation 文档。
