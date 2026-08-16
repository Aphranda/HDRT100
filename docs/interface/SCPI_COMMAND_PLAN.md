# Distributed Hard Real-Time Trigger System SCPI 架构基线

Status: Active
Domain: SCPI
Canonical: `docs/interface/SCPI_COMMAND_PLAN.md`
Related: `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/interface/SCPI_COMMANDS.md`, `docs/interface/SCPI_TASK_PROGRESS.md`
Last updated: 2026-08-16

本文档记录 Distributed Hard Real-Time Trigger System 当前 SCPI 固件架构、命令域边界、owner 归属和后续收敛待办。`DTC100` 暂作为当前设备型号和 `*IDN?` 字段保留。它不是历史流水账，也不是上位机最终指令手册：

- 正式产品指令表以 `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md` 为准。
- 固件基础命令说明以 `docs/interface/SCPI_COMMANDS.md` 为准。
- RTOS、反射内存、owner task 和双核职责以 `docs/arch/RTOS_HAOFV_ARCHITECTURE.md` 为准。
- SCPI 拆分、验证、板端闭环记录以 `docs/interface/SCPI_TASK_PROGRESS.md` 为准。

## 1. 当前结论

当前 SCPI 已经从早期 `scpi_port.c` 大文件和 legacy trigger 入口，收敛为“主命令线 + 分域命令模块 + owner/snapshot 闭环”的结构。

当前主域为：

```text
*
SYSTem
CONFigure
TRIGger
CALibration
SYNC
READ
MEASure
MMEMory
COMMunication
REALtime
```

核心边界：

- `TRIGger:*` 只保留产品运行控制：模式、启动、停止、暂停、继续、终止。
- `CONFigure:*` 负责业务配置、校准配置和同步配置，不直接执行 RUN。
- `CALibration:*` 和 `SYNC:*` 是动作域，分别负责校准事务和同步事务。
- `READ:*?` 是产品视角的状态、结果和摘要读取。
- `SYSTem:*` 是系统、维护、诊断、权限、反射内存、日志、OTA、SD 和报告证据域。
- `COMMunication:*` 是通信维护主域，当前包含 BiSS-C 和 UART，后续 RS485/UART 扩展继续放在这里。
- `REALtime:*` 是底层实时维护和 validation 主域，不进入现场测试上位机主流程。
- `MEASure:*?` 是测量原语/服务视图，供 CAL/SYNC/诊断复用，不替代业务 RUN 结果。
- `MMEMory:*` 只表达文件系统式访问；SD 卡驱动、job、manifest 仍归 `SYSTem:SD/STORage:*` 管理。

## 2. 主命令线

系统对外只保留一条 SCPI 主命令线：

```text
USBTMC / USB488 / validation CDC
  -> USB device task
  -> task_scpi
  -> libscpi command table
  -> scpi_<domain>_commands.c
  -> command/config slot or owner event (through owner API)
  -> reflected memory / owner snapshot
  -> domain state machine internal loop
```

硬约束：

- SCPI 指令表是对外通讯接口和事务入口，不是硬件寄存器或 GPIO/PIO 操作接口。
- SCPI callback 只能解析参数、做权限/状态/资源门禁，并通过 owner API 写入 command/config slot、投递 owner event 或读取反射内存/snapshot。
- SCPI callback 不得直接操作 GPIO、PIO、DMA、ADC、UART、RS485、BiSS、SD、flash 或任何现场硬件。
- 硬件动作只能由对应域 owner 和子功能状态机在内部循环中消费反射内存、命令槽或事件后执行。
- 反射内存不是任意共享变量区：SCPI 只允许写 command/config slot；state、summary、ACK/NACK、result、health 和 evidence slot 必须由对应 owner 写入。
- 写命令返回 `1/OK` 只表示接口层 accepted；动作完成、拒绝原因和质量证据必须通过 ACK/NACK、`READ:*?` 或 `SYSTem:*?` 回读。
- 反射内存保存共同事实、配置快照、命令意图、ACK/NACK、状态摘要和质量证据；不承载精确触发边沿，也不传 OTA payload、日志全文、波形或 SD 文件内容。

固件分层要求：

```text
scpi_port.c
  - libscpi context
  - input/output/error queue
  - stream write/flush/capture
  - reset/control/error bridge
  - command table assembly
  - common helpers

scpi_<domain>_commands.c/.h
  - one domain or one coherent subdomain
  - parse SCPI parameters
  - reject forbidden RUN-state writes
  - write command/config slot through owner API, read snapshot, or post owner event
  - no direct hardware operation

owner task / component
  - owns facts and state transitions
  - writes reflected memory/vector/snapshot
  - consumes command/config slots in its own loop
  - produces ACK/NACK, result, health and evidence data
```

`scpi_port.c` 不应重新承载业务实现。新增命令时先判断域归属，再在对应 `scpi_<domain>_commands.c/.h` 中注册。

## 3. 当前代码模块

`middleware/scpi_port/src/scpi_port.c` 当前聚合以下命令模块：

| 模块 | 主要域 | 当前定位 |
|---|---|---|
| `scpi_system_runtime_commands.c/.h` | `*TST?`, `SYSTem:FW/BOOT/LOG/CORE/RTOS:*` | 系统运行时、版本、自检、RTOS 水位 |
| `scpi_system_diagnostics_commands.c/.h` | `SYSTem:RUN/LOG/TRACe/SNAPshot/T2/FAULT:*`, `READ:RUN/STATistics:*` | 诊断、报告、T2、故障恢复闭环 |
| `scpi_system_snapshot_commands.c/.h` | `SYSTem:CONFigure/REFMEM/CORE/PROTection/MODE/RESource/FAULT:*` | 反射内存、配置门禁、系统表只读快照 |
| `scpi_system_access_commands.c/.h` | `SYSTem:SCPI:PERMission/ROLE:*` | SCPI 权限与角色 |
| `scpi_loop_engine_commands.c/.h` | `SYSTem:LOOP:STATus?` | loop engine 维护状态 |
| `scpi_config_commands.c/.h` | `CONFigure:TRIGger/ANGLe/SEQuence/SWITch`, `READ:*` | 现场测试业务配置和读取 |
| `scpi_calibration_commands.c/.h` | `CONFigure:CALibration:*`, `CALibration:*`, `READ:CALibration:*` | 校准 link、参数、版本、质量、事务 |
| `scpi_sync_commands.c/.h` | `CONFigure:SYNC:*`, `SYNC:*`, `READ:SYNC:*`, `SYSTem:SYNC:VDC:*` | 同步配置、同步动作、VDC/DPLL 维护 |
| `scpi_trigger_commands.c/.h` | `TRIGger:*`, `READ:TRIGger:STATe?` | 产品运行控制 |
| `scpi_realtime_component_commands.c/.h` | `REALtime:*` | 底层实时维护聚合入口 |
| `scpi_communication_biss_commands.c/.h` | `COMMunication:BISS:*` | BiSS-C 通信维护 |
| `scpi_communication_uart_commands.c/.h` | `COMMunication:SERial:UART#:*` | UART 通信维护骨架，后端待接入 |
| `scpi_ota_commands.c/.h` | `SYSTem:OTA:*` | OTA 包传输、状态、回滚和能力 |
| `scpi_usb_control.h` | `SYSTem:USB:*` | validation USB 模式切换，成品固定 USBTMC |
| `scpi_storage_commands.c/.h` | `SYSTem:SD/STORage/SNAPshot/TRACe/FAULT:*`, `MMEMory:*` | SD、storage job、snapshot、trace、MMEM 文件读取 |
| `scpi_measure_commands.c/.h` | `MEASure:*?` | 测量原语、链路 delay、T2 和报告视图 |

## 4. 域边界

| 域 | 产品含义 | owner/事实源 | 闭环入口 | 关键规则 |
|---|---|---|---|---|
| `*` | IEEE 488.2 通用控制 | libscpi / `task_scpi` | status/error queue | 仅放标准通用命令 |
| `SYSTem` | 系统、维护、诊断、证据 | system/storage/OTA/config/refmem owner | `SYSTem:*?`, error queue, ACK/NACK | 不表达现场测试动作 |
| `CONFigure` | 建立配置快照 | loop/cal/sync owner | `SYSTem:CONFigure:ACK?`, 对应 `READ:*?` | 只写配置/命令槽，不启动运行，不直接碰硬件 |
| `TRIGger` | 产品运行控制 | system + loop owner | `READ:TRIGger:STATe?`, `READ:RUN:SUMMary?` | 只写运行意图；START/STOP 由 owner 状态机执行 |
| `CALibration` | 校准动作 | calibration owner | `READ:CALibration:*?` | `CALibration:STARt` 明确一段链路；测量由校准状态机执行 |
| `SYNC` | 同步动作 | VDC/DPLL/sync owner | `READ:SYNC:*?` | 基于 active calibration 建立 DC 时钟同步；DPLL 由同步状态机维护 |
| `READ` | 产品视图读取 | 各 owner snapshot | `READ:*?` | 查询快照，不临时跨线程抓内部变量 |
| `MEASure` | 测量原语 | measurement service / diag owner | `MEASure:*?` | 可被 CAL/SYNC/诊断复用，后端逐步接入 |
| `MMEMory` | 文件系统式访问 | storage owner | `MMEMory:*?` | 用于目录、info、文件读；破坏性 SD 操作仍在 `SYSTem:SD:*` |
| `COMMunication` | 外部/板间通信维护 | comm owner | `COMMunication:*?` | BiSS/UART/RS485 统一归入此域；USB 不放入此域 |
| `REALtime` | 底层实时 validation | core1 realtime / sync_trigger owner | `REALtime:STATus?` | 不作为产品业务主流程 API |

## 5. 产品业务链路

现场测试上位机主流程按以下顺序闭环：

```text
CONFigure:TRIGger
CONFigure:ANGLe:SWEEp
CONFigure:ANGLe:BREAkpoint
CONFigure:SEQuence
CONFigure:SEQuence:ACTive
  -> READ:TRIGger:PARameter?
  -> READ:ANGLe:SWEEp?
  -> READ:SEQuence:MAP?
  -> READ:SEQuence:CHECk?
  -> READ:SEQuence:ACTive?
  -> SYSTem:CONFigure:ACK? / NACK?
  -> TRIGger:MODE 1
  -> TRIGger:STARt
  -> READ:TRIGger:STATe?
  -> READ:RUN:SUMMary? / SYSTem:RUN:SUMMary?
```

业务规则：

- `CONFigure:TRIGger <channel_count>,<pol>,<freq_count>,<wave_count>` 定义自动展开状态表的维度；参数顺序按“通道 -> 极化 -> 频点 -> 波位”。
- `pol=0/1/2` 分别表示 H/V/BOTH，`BOTH` 展开成 H/V 两态。
- `CONFigure:SEQuence` 上传状态引用顺序；`state_id -> switch1/switch2/pol/freq/wave` 必须可通过 `READ:SEQuence:MAP?` 回读。
- `CONFigure:SEQuence:ACTive` 负责选择并冻结 active sequence；`TRIGger:STARt [plan_id]` 只是受限便捷事务，不能绕过 active sequence 校验和门禁。
- `CONFigure:ANGLe:SWEEp` 定义扫描角度；`CONFigure:ANGLe:BREAkpoint` 定义运动角度断点。
- 断点只作用于扫描角度，不保存点内序列位置；断点恢复时，该角度点内部序列完整重测。
- `CONFigure:SWITch#` 是独立维护/调试切换。RUN 中若序列引擎占用开关资源，必须 busy/deny。
- `TRIGger:MODE 0..4 = IDLE/TRIG/CAL/SYNC/SIM`；`TEST` 产品上位机默认只使用 `0=IDLE` 和 `1=TRIG`。

## 6. 校准和同步

校准和同步是两条相邻但不同的业务支撑链路：

```text
CALibration active link/parameter table
  -> SYNC:CHECk topology and delay gate
  -> SYNC:STARt VDC/DPLL lock
  -> MEASure/T2/DEVICE quality evidence
  -> TRIGger prediction and fire load
```

### 6.1 校准域

校准域维护固定链路 delay 和设备动作补偿，支持 link 和参数的增删改查。

```text
CONFigure:CALibration:LINK:ADD/UPDate/DELete/CLEAr
READ:CALibration:LINK?

CALibration:STARt
CALibration:STOP
READ:CALibration:STATe?
READ:CALibration:RESult?

CONFigure:CALibration:PARameter:ADD/SET/DELete
READ:CALibration:PARameter?

CALibration:SAVE/LOAD/ACTivate/ROLLback/CLEAr
READ:CALibration:LIST?
READ:CALibration:ACTive?

CONFigure:CALibration:META
READ:CALibration:META?
CONFigure:CALibration:LIMit
READ:CALibration:HEALth?

SYSTem:CALibration:LIMit:OVERRide
SYSTem:CALibration:LIMit:OVERRide?
SYSTem:CALibration:LIMit:DEFAult
```

规则：

- `CONFigure:CALibration:*` 写 staging，`CALibration:ACTivate` 才切换 active。
- `CALibration:STARt` 必须显式带链路端点，例如 `A0,OUT1,A1,IN1`。
- NODE 链路走 RJ45 触发回传；SMA/DEVICE 链路用于外部仪表或设备动作补偿。
- 校准事务应短、快、可重复；失败不得覆盖旧 active 参数。
- active 校准版本切换后，必须要求重新执行 `SYNC:CHECk`。

### 6.2 同步域

同步域基于 active calibration 验证拓扑和 delay，建立 VDC/DPLL 的稳态 DC 时钟，并为预测分发提供门禁。

```text
CONFigure:SYNC:CALibration
CONFigure:SYNC:RING
CONFigure:SYNC:VDC:DPLL
CONFigure:SYNC:GATE
CONFigure:SYNC:LIMit

SYNC:CHECk
SYNC:STARt
SYNC:STOP
SYNC:RELock
SYNC:HOLDover

READ:SYNC:STATe?
READ:SYNC:PARameter?
READ:SYNC:HEALth?
READ:SYNC:NODE?
READ:SYNC:LINK?
READ:SYNC:CHECk?

SYNC:SAVE/LOAD/ACTivate/ROLLback
READ:SYNC:LIST?
READ:SYNC:ACTive?
READ:SYNC:QUALity?
READ:SYNC:VERSion?
```

VDC/DPLL 层级：

- `SYNC:*` 是产品同步动作域。
- `CONFigure:SYNC:VDC:DPLL` 是产品同步配置中的 DPLL 参数入口。
- `SYSTem:SYNC:VDC:*` 是维护和诊断入口。
- `VDC` 是虚拟 DC 时钟；`DPLL` 是实现 VDC 稳态同步的算法层，不作为顶级域。
- 不允许新增裸 `VDC:*` 或裸 `DPLL:*`。

当前维护入口：

```text
SYSTem:SYNC:VDC:STATus?
SYSTem:SYNC:VDC:DPLL:STATus?
SYSTem:SYNC:VDC:TDMA:PLAN?
SYSTem:SYNC:VDC:OBServer?
SYSTem:SYNC:VDC:DPLL:TUNE
SYSTem:SYNC:VDC:DPLL:COEFficient
SYSTem:SYNC:VDC:DPLL:OVERRide?
SYSTem:SYNC:VDC:DPLL:COEFficient?
SYSTem:SYNC:VDC:DPLL:DEFAult
```

`SYSTem:SYNC:VDC:TDMA:PLAN? [window_class],[now_ns_lo],[now_ns_hi]` 是只读维护入口，用于复现 active `VdcTdmaScheduleProfile` 对 observation/data/idle window 的计划结果。它返回当前或指定 `now_ns` 对应的窗口起止、guard、wait_ns、late_ns、inside/missed 标志、schedule CRC 和 gate；不得作为产品上位机控制入口。

`SYSTem:SYNC:VDC:OBServer?` 是只读维护入口，用于查询 VDC manager 从 `sync_io_read_capture_words()` 消费 raw capture word 的 observer 证据。它只返回 enabled、batch、raw/no-edge/ambiguous/submitted/accepted/rejected、last raw word、event id、tick_l32 和 gate reject 等状态；不得启动 capture、不得改变 observer 配置、不得作为 DPLL lock evidence 本身。

## 7. 通信、实时和测量

### 7.1 通信域

`COMMunication:*` 统一承载外部或板间通信维护能力：

```text
COMMunication:BISS:*
COMMunication:SERial:UART#:*
```

边界：

- BiSS-C 属于 `COMMunication:BISS:*`，不属于 `TRIGger:*`。
- UART 属于 `COMMunication:SERial:UART#:*`，当前为后端待接入骨架，查询中保留 `PENDING_BACKEND`。
- 后续 RS485 建议扩展为 `COMMunication:SERial:RS485#:*` 或在 `SERial` 下增加端口类型字段。
- USB 是 SCPI 传输与 validation 模式切换，保留在 `SYSTem:USB:*`，不迁入 `COMMunication`。

### 7.2 REALtime 维护域

`REALtime:*` 面向开发、产测、服务和底层验证：

```text
REALtime:IO:*
REALtime:SEQ:*
REALtime:ENC:*
REALtime:PCNT:*
REALtime:SOURce/EDGE/GATE/SAFE
REALtime:ARM/DISarm/DISAble/FAULT
REALtime:STATus?
```

规则：

- `SEQ_STEP` 是 A1 底层状态机基础件。
- `ENC` 是计数脉冲、角度和分发触发基础件。
- `PCNT` 是脉冲计数基础件。
- 这些能力后续可进入产品化流程，但当前不挂到产品 `TRIGger:*` 下。

### 7.3 MEASure 测量域

`MEASure:*?` 当前作为测量原语和服务视图：

```text
MEASure:FREQuency?
MEASure:PERiod?
MEASure:JITTer?
MEASure:PULSe:WIDTh?
MEASure:LINK:DELay?
MEASure:T2?
MEASure:REPort?
```

边界：

- `MEASure:LINK:DELay?` 可以成为 `CALibration:STARt` 的底层测量来源，但不直接管理校准表版本。
- `MEASure:T2?` 可以成为同步质量和报告证据来源，但 T2 明细分页仍归 `SYSTem:T2:*?`。
- `MEASure:REPort?` 是测量摘要视图，不替代 `SYSTem:RUN:*?` 和 storage report。

## 8. 系统、诊断、存储和反射内存

`SYSTem:*` 是维护和证据主域，不再拆出裸 `STATus:*`、裸 `REFMEM:*` 或裸 `REPORT:*`。

当前系统主线：

```text
SYSTem:FW:*
SYSTem:BOOT:*
SYSTem:LOG:*
SYSTem:CORE?
SYSTem:RTOS:STATus?
SYSTem:LOOP:STATus?
SYSTem:SCPI:*
SYSTem:CONFigure:*
SYSTem:REFMEM:*
SYSTem:CORE:VECTOR?
SYSTem:PROTection:STATus?
SYSTem:MODE:TABle?
SYSTem:RESource:TABle?
SYSTem:FAULT:TABle?
SYSTem:FAULT:CLEAr
SYSTem:RUN:*
SYSTem:TRACe:*
SYSTem:SNAPshot:*
SYSTem:T2:*
SYSTem:OTA:*
SYSTem:SD:*
SYSTem:STORage:*
SYSTem:USB:*
```

反射内存边界：

- RefMem 是 HAOFV 内部主域，owner 为 Distributed Vector Blackboard / RefMem Sync Domain。
- 对外维护入口固定为 `SYSTem:REFMEM:*`，不单独建立顶级 `REFMEM` SCPI 域。
- 当前表设计面向 A0-A7 八个通用节点；真实板卡、模型网分、模拟转台、网关和测试代理是加载到通用节点上的 role/persona/instance。
- 在资源、IO、时序、owner 和 slot writer 不冲突时，同一通用节点可以同时载入多个逻辑实例。
- 产品业务读取优先通过 `READ:*?` 和 `SYSTem:*?` 摘要，不直接要求上位机解析全部反射内存布局。
- SCPI 只读取 RefMem snapshot 或写 command/config slot；state、summary、ACK/NACK、result、health 和 evidence slot 仍由对应 owner 写入。
- `SYSTem:CONFigure:ACK?` / `SYSTem:CONFigure:NACK?` 是 RefMem `AckCommandSlot` 的配置门禁视图；如果后续增加通用 `SYSTem:COMMand:ACK?` / `SYSTem:COMMand:NACK?`，必须读取同一底层 command slot，不允许另建一套 ACK/NACK 事实。
- 写命令返回 `OK` 或 `1` 只表示接口层 accepted；动作完成、节点拒绝、busy、timeout 和证据必须通过 ACK/NACK、对应 `READ:*?` 或 `SYSTem:*?` 快照闭环。

报告/日志/T2 边界：

- RUN 后摘要：`READ:RUN:SUMMary?` 和 `SYSTem:RUN:SUMMary?`。
- 诊断证据：`SYSTem:RUN:LOG?`、`SYSTem:LOG:PAGE?`、`SYSTem:TRACe:DATA?`、`SYSTem:SNAPshot:DATA?`。
- T2 明细：`SYSTem:T2:COUNt?`、`SYSTem:T2:DATA?`。
- 文件访问：`MMEMory:CATalog:PAGE?`、`MMEMory:CATalog?`、`MMEMory:INFO?`、`MMEMory:READ?`。

## 9. 命名规范

新增或修改 SCPI 命令必须遵守以下规则：

- 使用标准 SCPI 大小写：关键字前四位大写，其余小写，例如 `CONFigure`、`TRIGger`、`STATus`。
- 对外文档使用完整语义词，不使用无人能读懂的缩写，例如使用 `BREAkpoint`，不使用 `BP` 或 `BPOint`。
- 业务配置使用 `CONFigure:*`。
- 业务读取使用 `READ:*?`。
- 产品运行动作使用 `TRIGger:*`。
- 校准动作使用 `CALibration:*`。
- 同步动作使用 `SYNC:*`。
- 系统维护使用 `SYSTem:*`。
- 文件系统式访问使用 `MMEMory:*`。
- 通信维护使用 `COMMunication:*`。
- 底层实时 validation 使用 `REALtime:*`。

禁止重新引入：

```text
TRIGger:BISS:*
TRIGger:PCNT:*
TRIGger:ENC:*
TRIGger:SEQ:*
TRIGger:SOURce/EDGE/GATE/SAFE
STATus:TRIGger?
STATus:BISS?
READ:T2:*
SYSTem:CFG:*
SYSTem:REFM:*
VDC:*
DPLL:*
BPOint
FBITs / POFFset / PBITs / PMODulo
```

## 10. 当前实现指令树

以下指令树按当前固件注册表整理。详细参数和响应字段见正式指令表与 `docs/interface/SCPI_COMMANDS.md`。

```text
*
  CLS
  ESE / ESE?
  ESR?
  IDN?
  OPC / OPC?
  RST
  SRE / SRE?
  STB?
  TST?
  WAI

SYSTem
  ERRor[:NEXT]?
  ERRor:COUNt?
  VERSion?
  FW:VERSion?
  FW:BUILD?
  BOOT:VERSion?
  BOOT:CAPability?
  BOOT:RESet                 ; fault injection build only
  LOG:LEVel / LEVel?
  LOG:STATus?
  LOG:PAGE?
  CORE?
  CORE:VECTOR?
  RTOS:STATus?
  LOOP:STATus?
  SCPI:PERMission / PERMission?
  SCPI:ROLE / ROLE?
  SCPI:RUN:ALLOW?
  CONFigure:STAT?
  CONFigure:ROLE?
  CONFigure:LOOP?
  CONFigure:ACT?
  CONFigure:CAL?
  CONFigure:ACK?
  CONFigure:NACK?
  REFMEM:STATus?
  REFMEM:NODE?
  PROTection:STATus?
  MODE:TABle?
  RESource?
  RESource:TABle?
  FAULT:TABle?
  FAULT:CLEAr
  FAULT:LAST?
  RUN:LAST?
  RUN:SUMMary?
  RUN:LOG?
  TRACe:DATA?
  TRACe:LAST?
  SNAPshot:DATA?
  SNAPshot:WRITe
  SNAPshot:LAST?
  T2:COUNt?
  T2:DATA?
  TRIGger:DBG?
  CALibration:LIMit:OVERRide / OVERRide?
  CALibration:LIMit:DEFAult
  SYNC:VDC:STATus?
  SYNC:VDC:DPLL:STATus?
  SYNC:VDC:TDMA:PLAN?
  SYNC:VDC:OBServer?
  SYNC:VDC:DPLL:TUNE
  SYNC:VDC:DPLL:COEFficient / COEFficient?
  SYNC:VDC:DPLL:OVERRide?
  SYNC:VDC:DPLL:DEFAult
  OTA:STATus?
  OTA:PROGress?
  OTA:BEGIN
  OTA:PBEGIN
  OTA:DATA
  OTA:END
  OTA:ABORt
  OTA:BOOT
  OTA:COMMit
  OTA:SLOT?
  OTA:RESult?
  OTA:TXN?
  OTA:TRANsaction?
  OTA:MODE / MODE?           ; write only in fault injection build
  OTA:TARGet?
  OTA:CAPability?
  OTA:INJect:*               ; fault injection build only
  SD:STATus?
  SD:INFO?
  SD:RAW:CLEar
  SD:RAW:READ?
  SD:MKFS
  SD:INITialize
  SD:MANifest?
  STORage:JOB:INFO
  STORage:JOB?
  STORage:STATus?
  USB:MODE / MODE?           ; runtime switch build only
  USB:BOOT                   ; runtime switch build only

CONFigure
  TRIGger
  ANGLe:SWEEp
  ANGLe:PULSe
  ANGLe:BREAkpoint
  ANGLe:BREAkpoint:CLEAr
  SEQuence
  SEQuence:ACTive
  SWITch#
  CALibration:LINK:ADD
  CALibration:LINK:UPDate
  CALibration:LINK:DELete
  CALibration:LINK:CLEAr
  CALibration:PARameter:ADD
  CALibration:PARameter:SET
  CALibration:PARameter:DELete
  CALibration:META
  CALibration:LIMit
  SYNC:CALibration
  SYNC:RING
  SYNC:VDC:DPLL
  SYNC:GATE
  SYNC:LIMit

TRIGger
  MODE / MODE?
  STARt
  STOP
  PAUSe
  CONTinue
  ABORt

CALibration
  STARt
  STOP
  SAVE
  LOAD
  ACTivate
  ROLLback
  CLEAr

SYNC
  CHECk
  STARt
  STOP
  RELock
  HOLDover
  SAVE
  LOAD
  ACTivate
  ROLLback

READ
  TRIGger:PARameter?
  TRIGger:STATe?
  ANGLe:SWEEp?
  ANGLe:PULSe?
  ANGLe:POSition?
  ANGLe:BREAkpoint?
  SEQuence?
  SEQuence:MAP?
  SEQuence:CHECk?
  SEQuence:ACTive?
  SWITch#?
  CALibration:LINK?
  CALibration:STATe?
  CALibration:RESult?
  CALibration:PARameter?
  CALibration:LIST?
  CALibration:ACTive?
  CALibration:META?
  CALibration:HEALth?
  SYNC:STATe?
  SYNC:PARameter?
  SYNC:HEALth?
  SYNC:NODE?
  SYNC:LINK?
  SYNC:CHECk?
  SYNC:LIST?
  SYNC:ACTive?
  SYNC:QUALity?
  SYNC:VERSion?
  RUN:SUMMary?
  STATistics?

MEASure
  FREQuency?
  PERiod?
  JITTer?
  PULSe:WIDTh?
  LINK:DELay?
  T2?
  REPort?

MMEMory
  CATalog:PAGE?
  CATalog?
  INFO?
  READ?

COMMunication
  BISS:CONFigure
  BISS:ROLE / ROLE?
  BISS:DEVice / DEVice?
  BISS:CLOCk / CLOCk?
  BISS:FRAMe:BITS / BITS?
  BISS:POSition:OFFSet / OFFSet?
  BISS:POSition:BITS / BITS?
  BISS:POSition:MODulo / MODulo?
  BISS:SAMPle:*
  BISS:TIMEout / TIMEout?
  BISS:ANCHor:*
  BISS:ERRor:BIT / BIT?
  BISS:WARNing:BIT / BIT?
  BISS:STATus:GATE / GATE?
  BISS:CRC:*
  BISS:LATency:OFFSet / OFFSet?
  BISS:TARGet / TARGet?
  BISS:PINs?
  BISS:PULSe
  BISS:FRAMe
  BISS:CRC:ERRor
  BISS:TIMEout:INJect
  BISS:STATus?
  SERial:UART#:BAUD / BAUD?
  SERial:UART#:FORMat / FORMat?
  SERial:UART#:STATe / STATe?
  SERial:UART#:STATus?
  SERial:UART#:TX:TEST / TEST?
  SERial:UART#:RX:COUNt?
  SERial:UART#:ERRor?

REALtime
  IO:OUTPut:WIDTh / WIDTh?
  IO:OUTPut:IMMediate
  IO:PULSe:WIDTh / WIDTh?
  IO:PULSe:IMMediate
  IO:MARKer:WIDTh / WIDTh?
  IO:MARKer:IMMediate
  IO:RJ45:WIDTh / WIDTh?
  IO:RJ45:IMMediate
  IO:RJ45:PINs?
  IO:SAMPle:RATE / RATE?
  IO:SAMPle:STATe / STATe?
  IO:CLOCk:FREQuency / FREQuency?
  IO:CLOCk:STATe / STATe?
  IO:SYNC?
  SOURce / SOURce?
  EDGE / EDGE?
  GATE / GATE?
  SAFE / SAFE?
  SEQ:LENGth / LENGth?
  SEQ:WIDTh / WIDTh?
  SEQ:INDex?
  SEQ:DATA / DATA?
  ENC:TARGet / TARGet?
  ENC:COUNt?
  ENC:APIN / APIN?
  ENC:REVolution?
  PCNT:DECode / DECode?
  PCNT:DIRection / DIRection?
  PCNT:FILTer / FILTer?
  PCNT:GATE / GATE?
  PCNT:CMP / CMP?
  PCNT:PRESet / PRESet?
  PCNT:CLEar
  PCNT:TOTal?
  PCNT:FREQuency?
  ARM
  DISarm
  DISAble
  FAULT
  STATus?
```

## 11. 后续收敛待办

这些待办按架构优先级排列，具体执行记录追加到 `docs/interface/SCPI_TASK_PROGRESS.md`。

- [ ] UART 后端接入真实 driver/owner，替换 `PENDING_BACKEND` 响应，补 RX/TX/error 计数闭环。
- [ ] 规划 RS485 命令树，建议挂在 `COMMunication:SERial:RS485#:*`，并与 UART 共享 serial owner 抽象。
- [ ] 复核 `READ:RUN:SUMMary?` 与 `SYSTem:RUN:SUMMary?` 的保留策略：一个作为产品快捷读，一个作为系统报告读，响应字段必须一致。
- [ ] 完成 `CONFigure:SEQuence` 后端产品化：序列库、active CRC、拒绝原因、ACK/NACK 和 RUN 冻结快照。
- [ ] 完成 `SYSTem:T2:DATA?` 分页数据结构，固定字段、block 格式、CRC、seq/late/error 证据。
- [ ] 将 `MEASure:LINK:DELay?` 与校准 owner 打通，形成 `CALibration:STARt` 的真实测量来源。
- [ ] 将 `MEASure:T2?` 与同步质量/报告数据打通，明确与 `SYSTem:T2:*?` 的摘要/明细边界。
- [ ] 反射内存表字段与 SCPI 摘要字段继续对齐，避免上位机同时维护两套状态模型。
- [ ] MMEM 写入/删除如进入产品化，必须先定义权限、RUN 态拒绝、文件锁和 storage job ACK。
- [ ] 下一次板端烧录时覆盖 product SCPI、realtime SCPI、legacy 删除验证、UART 骨架查询和 RUN smoke。

## 12. 验证基线

本文档对应的当前离线验证基线：

```text
python tools/product_scpi_validate/product_scpi_validate.py --dry-run
  generated=125

python tools/realtime_scpi_validate/realtime_scpi_validate.py --dry-run
  generated=57
```

文档修改后至少执行：

```text
python tools/docs_check/docs_check.py
git diff --check
```
