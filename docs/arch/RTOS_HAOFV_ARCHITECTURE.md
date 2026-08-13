# 基于 HAOFV 的 RTOS 架构

Status: Active
Domain: RTOS
Canonical: `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`, `docs/interface/DTC100_SCPI_COMMAND_PLANNING.md`
Last updated: 2026-08-13

本文档是 DTC100 / RP2350_TRIG 在 HAOFV 下的 RTOS + 双核 AMP 架构入口。
RTOS 只提供调度器、任务、队列、超时和同步原语，不替代 HAOFV 中的
Active Object、Function Block、Vector Blackboard、Resource Arbiter 和 Hardware Service
边界。

旧的 `RTOS_PORTING_PLAN.md`、`MULTICORE_PARTITION_PLAN.md`、`RTOS_DISTRIBUTED_TRIGGER_0614_SUMMARY.md`
和 `RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` 已整合到本文、`RTOS_HAOFV_TODO.md` 和
`RTOS_HAOFV_TASK_PROGRESS.md`。0614 / 0804 原始报告继续保留在 `docs/reports/distributed-trigger/`
作为历史设计输入。

## 架构原则

- HAOFV 是主线，RTOS 是运行容器。
- `core0` 是控制核，运行 FreeRTOS 管理任务。
- `core1` 是实时核，运行受限实时循环和 TriggerAO/TriggerFB 快路径。
- PIO/DMA/IRQ 负责硬实时边沿、短窗口倒计时、捕获和时间戳。
- SCPI/UI/System Pack 只能表达意图、配置和查询，不能直接操作硬件。
- 所有跨域动作必须通过 owner API、事件队列、命令槽或反射内存完成。
- 所有事实、摘要、版本、CRC、ACK/NACK 和健康状态必须由唯一 owner 写入。
- 反射内存回答“系统共同认知是什么”，不承载 OTA payload、日志全文、波形或实时边沿。

## 设计输入

当前 RTOS 架构承接以下输入：

| 输入 | 进入本文的结论 |
|---|---|
| HAOFV 顶层架构 | RTOS task 是 AO 容器；Vector 是事实源；外部入口只投递意图。 |
| 0614 分布式触发方案 | 虚拟 DC、预约触发、`T2_i` 回读、反馈校准和时间补偿必须分层。 |
| 0804 RP 分布式触发报告 | 四板 A0/A1/A2/A3、RJ45_SYNC_RING、`FIRE_LOAD`、T2、RUN 后报告闭环。 |
| PinProbe A1 RAM 反射内存经验 | 命令是意图，镜像是事实，查询读快照，命令槽原子 Take/Clear。 |
| 当前最小系统板代码 | 先用 RTOS + 双核 smoke 建立任务壳、水位、core1 heartbeat 和本地 64 KB 表。 |

## 总体模型

```text
Host / VNA / A3 USBTMC
    ↓
task_usb_device
    ↓
task_scpi
    ↓
command/config slot or owner event
    ↓
SystemAO / LoopEngineAO / CalibrationAO / VdcSyncAO / StorageAO
    ↓
DistributedVectorTable snapshot + ACK/NACK
    ↓
core1_realtime / TriggerAO / TriggerFB
    ↓
PIO / DMA / IRQ
```

硬实时旁路：

```text
PIO local_fire
DMA / IRQ capture
timestamp sample
trigger_status_ring
```

USB、SCPI、SD、OTA、LCD 和日志抖动不能进入真实触发边沿。

## 核心分区

| 区域 | owner | 内容 | 访问规则 |
|---|---|---|---|
| core0 control region | core0 | FreeRTOS task、USB/SCPI、OTA、Storage、UI、诊断、配置门禁 | core0 可读写；core1 禁止直接调用 |
| core1 realtime region | core1 | `core1_realtime`、TriggerAO/TriggerFB、PIO 装载、T2/READY 捕获、快速 fault | core1 独占写；core0 只能通过命令队列写意图 |
| shared vector region | core0 + core1 | `trigger_command_queue`、`trigger_status_ring`、DistributedVectorTable、heartbeat、fault evidence | 固定 layout、owner、sequence、CRC/seqlock |

core1 禁止执行 FatFs、USB、SCPI、OTA flash job、LCD 刷新、阻塞日志格式化、动态内存申请和无界等待。

## 任务模型

初始栈采用“先大后小”，后续通过 `SYSTem:RTOS:STATus?` 水位收缩。

| 执行体 | 核 | 优先级 | 初始栈 | 职责 |
|---|---:|---:|---:|---|
| `core1_realtime` | core1 | 裸实时循环 | 独立栈 | TriggerAO、`local_fire` 装载、capture/T2 采样、RJ45 帧首沿服务、late/CRC/fault 快判定 |
| `task_system` | core0 | 4 | 2048 words | bring-up、SystemAO、系统模式、资源仲裁、故障锁存、board service |
| `task_usb_device` | core0 | 4 | 1536 words | TinyUSB/CDC/USBTMC 轮询 |
| `task_scpi` | core0 | 3 | 3072 words | SCPI 解析、权限门禁、accepted 响应、只投递事件或查询快照 |
| `task_gateway_a3` | core0 | 3 | 3072 words | A3 网关、配置包接收、START/STOP 转发、VNA 状态桥接 |
| `task_loop_engine` | core0 | 3 | 3072 words | A0 扫描编排、角度/断点/序列展开、滚动生成 `FIRE_LOAD` |
| `task_vdc_sync` | core0 | 4 | 2048 words | SYNC DPLL、虚拟 DC offset/rate、LOCK/HOLDOVER/RELOCK、`e_vdc` |
| `task_calibration` | core0 | 3 | 2048 words | CAL link/delay 表、短事务测量、staging/active/version/quality |
| `task_refmem_sync` | core0 | 4 | 2048 words | 64 KB DistributedVectorTable、slot delta、节点心跳、stale 判定 |
| `task_dpll` | core0 | 3 | 2048 words | 角度预测 DPLL、Compare Out、`T_fire_base`；不参与 VDC offset/rate |
| `task_storage` | core0 | 2 | 3072 words | SD/FatFs、System Pack、trace、snapshot、T2、report job |
| `task_ota` | core0 | 2 | 1536 words | OtaAO、metadata、flash job，受资源仲裁和 core1 park 约束 |
| `task_ui` | core0 | 1-2 | 2048 words | LCD、按键、节点 ID、同步状态、计数和错误显示 |
| `task_diag` | core0 | 1 | 1024-1536 words | 低频诊断、log flush、统计快照；P0 可并入 `task_system` |

## 角色启用矩阵

同一固件支持 A0/A1/A2/A3，角色由 `NodeRoleMap` 或产品配置决定。

| 执行体 | A0 | A1 | A2 | A3 | 说明 |
|---|---:|---:|---:|---:|---|
| `task_system` | on | on | on | on | 所有节点都有系统状态与安全策略 |
| `task_usb_device` | diag | diag | diag | on | A3 必开；其他节点可作为维护口 |
| `task_scpi` | diag | diag | diag | on | RUN 态只允许安全查询和 STOP/FAULT clear |
| `task_gateway_a3` | off | off | off | on | 上位机/VNA 近端网关 |
| `task_loop_engine` | on | off | off | proxy | A0 执行；A3 转发或显示 |
| `task_calibration` | owner | local | local | coordinator | A0/A3 编排；各节点执行本地链路测量 |
| `task_vdc_sync` | origin | follower | follower | follower | A0 发布共同时间；其他节点跟随 |
| `task_refmem_sync` | on | on | on | on | 所有节点维护同一张表 |
| `task_dpll` | on | off | off | off | 首版转台 Compare Out 在 A0 |
| `core1_realtime` | on | on | on | on | 所有节点都有本地 PIO 触发/捕获 |
| `task_storage` / `task_ui` / `task_ota` | on | on | on | on | 维护、观测和升级 |

## 队列与数据流

| 通道 | 方向 | 载荷 | 约束 |
|---|---|---|---|
| `scpi_control_queue` | `task_scpi` -> owner task | 配置、START、STOP、查询请求 | 不承载大文件；写命令只表示 accepted |
| `gateway_control_queue` | `task_gateway_a3` -> `task_loop_engine` | 上位机配置包索引、启动停止、VNA 状态 | A3 专用，不能逐点驱动 RUN |
| `loop_event_queue` | Loop/SYNC/DPLL 内部 | scan tick、layer action、角度 DPLL 更新、VDC gate 更新 | A0 owner 消费 |
| `calibration_job_queue` | SCPI/System -> Calibration | link CRUD、CAL start、save/load/activate/check | 短事务优先；持久化转 storage |
| `sync_control_queue` | SCPI/System -> VDC SYNC | check/start/stop/relock/holdover、DPLL profile | 不直接写实时边沿 |
| `trigger_command_queue` | core0 -> core1 | ARM/DISARM、`FIRE_LOAD`、PIO 装载、捕获窗口 | 固定小载荷，非阻塞，满队列计 drop/late |
| `trigger_status_ring` | core1 -> core0 | T2、READY、late、CRC、seq、fault evidence | 无 FatFs/USB 调用，只写 RAM ring |
| `refmem_delta_queue` | refmem <-> RJ45 ring | slot 版本、状态摘要、ACK 位图、故障摘要 | 只传播小 delta |
| `storage_job_queue` | core0 tasks -> Storage | trace、snapshot、manifest、catalog/read、CAL/SYNC package | 可阻塞但必须有超时 |

典型 RUN 数据流：

```text
A3 SCPI START
  -> task_scpi
  -> task_gateway_a3
  -> task_system / task_loop_engine 做配置、CAL、SYNC、序列门禁
  -> task_loop_engine(A0) 冻结 run_id / active sequence / active delay
  -> task_dpll 生成 T_fire_base
  -> task_loop_engine 生成 T_fire_i / delta / mask
  -> trigger_command_queue(FIRE_LOAD)
  -> core1_realtime 装载 PIO local_fire
  -> PIO 到点输出 SMA_OUT
  -> PIO capture_window 捕获 READY/T2
  -> trigger_status_ring
  -> task_loop_engine / task_storage / task_gateway_a3
```

## 反射内存

DistributedVectorTable 按 64 KB 产品化完整布局实现，P0 只启用核心字段，其余区域保留并纳入
版本和 CRC 管理。

| 区域 | 建议大小 | 内容 | 写入者 |
|---|---:|---|---|
| Header/Directory | 1 KB | magic、layout、slot offset、table_seq、epoch、crc32 | `task_refmem_sync` |
| SystemSlot | 1 KB | system_mode、role_map_version、run_id、fault_latch、release gate | `task_system` |
| Role/ConfigSlot | 2 KB | NodeRoleMap、hw_profile、persona、feature mask | `task_system` / config loader |
| VdcSlot | 2 KB | sync_id、offset、rate、lock_state、holdover、relock、`e_vdc` | `task_vdc_sync` |
| LoopSlot | 4 KB | trigger param、angle sweep/breakpoint、active sequence、scan_index | `task_loop_engine` |
| DpllSlot | 2 KB | compare 捕获、角度预测、`T_fire_base`、`e_pll` | `task_dpll` |
| NodeSlot[8] | 4 KB | node_id、role、heartbeat、local_state、error_code、stale_count | 各节点 owner |
| TriggerSlot[8] | 8 KB | armed、last_fire_seq、late_count、t2_count、ready_timeout | 各节点 core1 摘要 |
| IoSlot[8] | 8 KB | SMA/RJ45/BiSS IO 镜像、边沿计数、健康状态 | 各节点 IO owner |
| CalibrationSlot | 8 KB | link table、delay table、staging/active/version/quality | `task_calibration` |
| StatisticsSlot | 8 KB | `e_vdc/e_act/e_pll`、CRC/seq/late 分布、p99/p999 | 各统计 owner |
| AckCommandSlot | 4 KB | command_seq、ack/nack/busy/timeout 位图、原子命令槽 | 命令 owner + 节点 ack |
| FaultEvidenceSlot | 6 KB | fault_code、source_node、epoch、run_id、关键证据 | `task_system` |
| GatewaySlot | 2 KB | A3/VNA/host 状态、采集状态 | `task_gateway_a3` |
| OtaStorageUiSlot | 2 KB | OTA、Storage、UI 摘要 | 对应 task owner |
| TlvExtension | 2 KB | versioned TLV、未来扩展 | owner by type |

完整表不等于整表高频同步。RJ45_SYNC_RING 上只同步变更 slot 的小 delta：

```text
REFMEM_DELTA(slot_id, slot_version, compact payload)
REFMEM_EPOCH(epoch, run_id, table_seq)
```

本地查询必须读快照，不临时跨板阻塞查询；slot stale 时返回 stale 标志。

## VDC / DPLL / T2 链

0614 和 0804 方案在 RTOS 下收敛为下面的链条：

```text
local oscillator -> local_tick
NODE/SMA base link calibration -> link_delay_ns / delay table
RJ45 sync frame -> local timestamp observation
SYNC DPLL -> VDC offset/rate estimate
LOCKED virtual DC -> DEVICE/T2 calibration -> action delay
Angle DPLL -> T_fire_base prediction
FIRE_LOAD / local_fire / T2 capture / e_act validation
```

约束：

- `task_vdc_sync` 的 SYNC DPLL 只维护共同时间事实。
- `task_dpll` 的 Angle DPLL 只维护角度预测和 `T_fire_base`。
- `T2` 是实际动作回读事实，进入 Measure/T2/Statistics，不放在业务配置域。
- DC 未 `LOCKED` 前的时间戳只能作为调试数据，不能作为正式 RUN 或 DEVICE/T2 校准基准。

## SCPI 边界

SCPI 是产品对外通讯接口，不是硬件操作接口。

```text
SCPI command
  -> task_scpi parse and gate
  -> command/config slot or owner event
  -> ACK/NACK or accepted
  -> domain owner state machine loop
  -> hardware service / PIO / storage / communication backend
  -> status/result/health/evidence slot
  -> READ:*? / SYSTem:*?
```

业务配置走 `CONFigure:*`，业务读取走 `READ:*?`，系统资源和维护走 `SYSTem:*`，
运行控制走 `TRIGger:*`，校准和同步动作分别走 `CALibration:*` / `SYNC:*`。
底层实时验证入口归 `REALtime:*`，不得继续挤入产品 `TRIGger` 主线。

## 系统状态

| 状态 | 说明 | 允许动作 |
|---|---|---|
| `BOOT` | 上电、自检、角色加载 | 关闭外部驱动，禁止触发 |
| `IDLE` | 安全空闲 | 配置、查询、OTA、SD 操作 |
| `CONFIG` | 配置包加载/校验 | 修改 NodeRoleMap、LoopPlan、校准表 |
| `LOCK` | RJ45 ring 和 VDC 锁定 | SYNC、offset/rate 收敛，禁止正式输出 |
| `CAL` | SMA/RJ45/T2 校准 | 允许测试脉冲和回环 |
| `ARMED` | 已准备 RUN | 只允许安全查询、STOP、DISARM |
| `RUN` | 四板内部自循环 | 禁止改配置；只允许 STOP 和状态查询 |
| `HOLDOVER` | 短时同步丢失 | 停止后续预约输出，等待重锁或 STOP |
| `FAULT` | 故障锁存 | 输出安全态，等待人工/上位机清除 |

RUN 态禁止修改 pin map、NodeRoleMap、ActionMap、`CONFigure:*`、`CALibration:*`、
`SYNC:*`、PIO owner、OTA/flash、SD 大文件和任何已过期 `T_fire` 补发。

## 发布门禁

- `SYSTem:CORE?` core1 heartbeat 持续增长。
- `SYSTem:RTOS:STATus?` heap/stack 水位稳定。
- `SYSTem:REFMEM:*` table_seq、layout、node heartbeat、slot stale 正常。
- CAL/SYNC/DPLL service_count、版本、CRC、quality/fault 可查询。
- `TRIGger:STARt/STOP` 通过产品路径，不直接驱动底层 realtime。
- RUN 后可读取 run summary、trace、snapshot、T2 和 fault evidence。
- OTA/Storage/UI/SCPI 并发压力不影响 core1 realtime。
- flash erase/program 前 core1 park/lockout 可确认。
