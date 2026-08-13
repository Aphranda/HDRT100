# 基于 HAOFV 的 RTOS 架构

Status: Active
Domain: RTOS
Canonical: `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`, `docs/interface/SCPI_COMMAND_PLAN.md`
Last updated: 2026-08-13

本文档是 Distributed Hard Real-Time Trigger System 在 HAOFV 下的 RTOS + 双核 AMP 架构入口。
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
- VDC Domain 回答“系统共同时间是什么”，SYNC DPLL 是 VDC offset/rate owner，Angle DPLL 不能写 VDC 共同时间。

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

### PIO / VDC 参考数据通路

首版 VDC 与触发预测分发可以参考两个 PIO state machine 形成一收一发的硬实时最小链路。这里描述的是 RTOS/AMP 数据通路和 owner 边界，不冻结具体 PIO instance、GPIO、DMA channel 或最终布线路径；后续可根据实际资源、布线和 board profile 调整。

| 执行体 | 职责 | 输出到 |
|---|---|---|
| `PIO_SM0: SYNC_RX_CAPTURE` | 监测差分输入，捕获上升沿，将捕获事件写入 RX FIFO。 | DMA |
| DMA capture | 将 PIO RX FIFO 写入 RAM timestamp ring。 | `core1_realtime` |
| `core1_realtime` | 读取捕获时间戳，写 TriggerSlot 摘要，写 DPLL offset/rate 输入样本。 | TriggerSlot / VDC input ring |
| `task_vdc_sync` | 运行软件 DPLL，更新虚拟 DC offset/rate。 | VdcSlot |
| `task_loop_engine` | 根据 VDC snapshot 计算 `T_fire_base`，生成 `FIRE_LOAD`。 | `trigger_command_queue` |
| `core1_realtime` fire loader | 接收 `FIRE_LOAD`，装载 PIO 目标 tick。 | `PIO_SM1` |
| `PIO_SM1: SYNC_TX_FIRE` | 在指定 tick 输出差分边沿，经 ISO7740 到差分线和对端。 | 外部同步链路 |

数据流：

```text
SYNC_RX differential edge
  -> PIO_SM0 RX FIFO
  -> DMA timestamp ring
  -> core1_realtime
  -> TriggerSlot summary + VDC input sample
  -> task_vdc_sync
  -> VdcSlot offset/rate/lock/quality
  -> task_loop_engine
  -> FIRE_LOAD
  -> trigger_command_queue
  -> core1_realtime
  -> PIO_SM1 target tick
  -> ISO7740 -> differential line -> peer
```

边界：

- PIO 和 DMA 只处理硬实时最小事件，不执行 DPLL。
- core1 只做 timestamp ring 读取、TriggerSlot 摘要和 PIO 装载，不写 VDC offset/rate。
- `task_vdc_sync` 是 VDC offset/rate 唯一 writer。
- `task_loop_engine` 只消费 VDC snapshot 生成 `FIRE_LOAD`。
- late `FIRE_LOAD` 必须拒绝补发并进入 evidence。

## 核心分区

| 区域 | owner | 内容 | 访问规则 |
|---|---|---|---|
| core0 control region | core0 | FreeRTOS task、USB/SCPI、OTA、Storage、UI、诊断、配置门禁 | core0 可读写；core1 禁止直接调用 |
| core1 realtime region | core1 | `core1_realtime`、TriggerAO/TriggerFB、PIO 装载、T2/READY 捕获、快速 fault | core1 独占写；core0 只能通过命令队列写意图 |
| shared vector region | core0 + core1 | `trigger_command_queue`、`trigger_status_ring`、DistributedVectorTable、heartbeat、fault evidence | 固定 layout、owner、sequence、CRC/seqlock |

core1 禁止执行 FatFs、USB、SCPI、OTA flash job、LCD 刷新、阻塞日志格式化、动态内存申请和无界等待。

## Flash/XIP 双核保护框架

Flash erase/program 是 RTOS + 双核 AMP 的 S0 风险路径。框架目标不是让 Flash 写入更快，而是保证任何擦写都不会让 core1 在 XIP 总线不可用时取指、取常量或进入不可恢复状态。

### 参与组件

| 组件 | 角色 | 职责 |
|---|---|---|
| `FlashWriteOwner` | core0 | 唯一允许发起 erase/program 的 owner，可由 OtaAO、Boot metadata 写入、配置落盘通过受控 API 请求。 |
| `Resource Arbiter` | core0 | 提供 `FLASH_BUS` 互斥资源，统一记录 owner、冲突、timeout 和 fault escalation。 |
| `Core1LockoutGate` | core0/core1 shared | 提供 request、ack、state、timeout、last_result 和 sequence。 |
| `core1_realtime` | core1 | 在循环快路径轮询 lockout request，进入 RAM resident park 状态。 |
| `RuntimeProtectionTable` | shared/refmem | 暴露 lockout support、online、requested、acknowledged、park_state、last_result。 |
| `DiagnosticsAO` | core0 | 记录 timeout、unexpected XIP access suspect、overrun、restore failure 和 release 事件。 |

### 状态机

| 状态 | owner | 说明 | 允许转移 |
|---|---|---|---|
| `FLASH_IDLE` | core0 | 无 Flash 写请求，core1 正常运行实时循环。 | `REQUEST_LOCKOUT` |
| `REQUEST_LOCKOUT` | core0 | 已获得或正在申请 `FLASH_BUS`，向 core1 设置 lockout request。 | `WAIT_CORE1_ACK` / `DENY` |
| `WAIT_CORE1_ACK` | core0 | 等待 core1 ACK；该等待必须有固定 timeout。 | `PARKED_FOR_FLASH` / `FAULT_TIMEOUT` |
| `PARKED_FOR_FLASH` | core1 | core1 已进入 RAM resident park loop，不访问 XIP。 | `FLASH_CRITICAL` |
| `FLASH_CRITICAL` | core0 | core0 执行 erase/program；禁止 USB/SD/LCD/日志长路径进入 Flash resident 代码。 | `RELEASE_LOCKOUT` / `FAULT_WRITE` |
| `RELEASE_LOCKOUT` | core0 | 清除 request，等待 core1 退出 park，释放 `FLASH_BUS`。 | `FLASH_IDLE` / `FAULT_RELEASE` |
| `FAULT_TIMEOUT` | core0 | core1 未 ACK 或 release 超时；本次 Flash job 拒绝或失败。 | `FAULT` |

状态机约束：

- `FLASH_BUS` 资源锁必须先于 core1 lockout request。
- 未获得 `FLASH_BUS` 时不得发起 lockout request。
- 未收到 core1 ACK 时不得进入 erase/program。
- timeout 后必须清除 request，记录 NACK/fault，并返回失败。
- core1 park loop、lockout poll、状态写回和必要栈必须位于 RAM resident 或已验证不触发 XIP 的区域。

### 接口契约

| 接口 | 方向 | 语义 |
|---|---|---|
| `flash_write_request(owner, op, offset, length)` | AO -> FlashWriteOwner | 请求受控 Flash 写入，不直接调用底层 erase/program。 |
| `resource_acquire(FLASH_BUS, owner, timeout)` | FlashWriteOwner -> Resource Arbiter | 申请 Flash bus 互斥资源，失败返回 busy 或 fault。 |
| `core1_lockout_request(seq)` | core0 -> core1 | 请求 core1 进入 park，`seq` 用于防止旧 ACK 被误用。 |
| `core1_lockout_ack(seq, park_state)` | core1 -> core0 | core1 已进入 `PARKED_FOR_FLASH` 或拒绝。 |
| `runtime_protection_snapshot()` | SCPI/RefMem -> RuntimeProtectionTable | 查询 lockout 可观测状态，不触发现场动作。 |
| `flash_write_result(seq, result, elapsed_us)` | FlashWriteOwner -> Diagnostics/RefMem | 发布写入结果、耗时、失败原因和恢复状态。 |

### 可观测字段

RuntimeProtectionTable / `SYSTem:PROTection:STATus?` 至少需要覆盖：

| 字段 | 说明 |
|---|---|
| `flash_lockout_supported` | 当前构建和板级 profile 是否支持 core1 lockout。 |
| `flash_lockout_online` | core1 是否已经进入可响应 lockout 的实时循环。 |
| `flash_lockout_requested` | core0 当前是否请求 park。 |
| `flash_lockout_acknowledged` | core1 是否 ACK 当前 request sequence。 |
| `park_state` | `0=RUNNING, 1=WAIT_FOR_FLASH, 2=PARKED_FOR_FLASH, 3=FAULT_TIMEOUT, 4=RELEASING`。 |
| `last_lockout_seq` | 最近一次 lockout request 序号。 |
| `last_lockout_result` | `OK / BUSY / ACK_TIMEOUT / RELEASE_TIMEOUT / WRITE_FAILED`。 |
| `last_lockout_elapsed_us` | 最近一次 Flash 临界区或等待阶段耗时。 |

### 失败处理

- `FLASH_BUS` busy：Flash job 返回 busy，不改变 core1 状态。
- core1 未 online：维护模式可按构建策略拒绝 Flash job；产品 RUN/OTA 路径必须拒绝。
- ACK timeout：进入 FAULT 或返回不可重试 NACK，禁止 erase/program。
- release timeout：记录 F2 系统故障，禁止后续 Flash job，保留 SCPI 只读诊断入口。
- 写入失败：释放 lockout 后进入 OTA/metadata 对应失败状态，并保存 evidence。

### 验证门禁

最小验证必须覆盖：

1. 空闲查询：`SYSTem:PROTection:STATus?` 显示 lockout supported/online。
2. OTA 或 metadata 写入前后：`requested/acknowledged/park_state` 按状态机变化。
3. 故障注入：模拟 core1 不 ACK 时 Flash job 不执行，错误进入 fault/NACK。
4. 并发压力：USBTMC 查询、UI 刷新、SD 空闲任务存在时，Flash 写入仍能 park/release core1。
5. 长稳：24h 内 core1 heartbeat 不停，lockout 统计无异常增长。

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
| `task_vdc_sync` | core0 | 4 | 2048 words | 当前任务壳承载 `VdcSyncAO / SyncDpllFB / VdcVector`；维护虚拟 DC offset/rate、LOCK/HOLDOVER/RELOCK、`e_vdc` |
| `task_calibration` | core0 | 3 | 2048 words | CAL link/delay 表、短事务测量、staging/active/version/quality |
| `task_refmem_sync` | core0 | 4 | 2048 words | 当前任务壳承载 `DistributedRefMemAO / RefMemSyncFB`；维护 64 KB DistributedVectorTable、静态分布式模型、slot delta、节点心跳、stale 判定 |
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
| `task_refmem_sync` / RefMem Domain | on | on | on | on | 所有节点维护同一张 DistributedVectorTable 和静态分布式模型 |
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

## RefMem 内部主域

DistributedVectorTable 按 64 KB 产品化完整布局实现，P0 只启用核心字段，其余区域保留并纳入
版本和 CRC 管理。

在 RTOS + 分布式系统中，RefMem 是内部主域，不仅是 slot 数据表，还承接 HAOFV 的静态分布式应用模型。它吸收 IEC 61499 分布式运行时的 application / FB instance / event connection / data connection / deployment consistency / diagnostics 思想，但不支持动态部署和跨节点 FB 直接调用。

RefMem 固定提供 A0-A7 八个通用插槽。这里的 `node_id` 更准确地说是 slot id；脉冲分发、链路切换、仪表控制、模型网分、模拟转台、网关、测试代理等不是额外固定节点，而是加载到 A0-A7 通用插槽上的 role、persona 或 AO/FB instance。在资源、IO、时序、owner 和 slot writer 不冲突时，同一通用插槽可以同时载入多个逻辑实例。

实施上必须拆成两类表：

| 表类型 | 作用 | 同步频率 |
|---|---|---|
| 静态分布式模型表 | 描述节点、FB 实例、事件连接、数据连接、部署门禁和连接质量字段定义。 | 配置加载、版本变化或 RUN 前校验时同步。 |
| 运行事实 slot 表 | 保存 heartbeat、state、command、ACK/NACK、T2、VDC、统计、故障证据。 | 按 slot delta 高频或中频同步。 |

静态分布式模型表建议：

| 表 | 内容 | RUN 门禁作用 |
|---|---|---|
| `DistributedApplicationMap` | 应用/profile 元数据、目标插槽集合和静态模型 CRC bundle。 | 确认 active profile、layout 和静态模型版本一致。 |
| `DistributedGenericNodeTable` | A0-A7 通用插槽基座、硬件身份、能力和失效策略。 | 确认通用插槽存在、online、hw profile 和基础能力匹配。 |
| `DistributedNodeLoadTable` | 将 AO/FB instance 显式加载到 A0-A7 通用插槽，支持同一插槽多实例。 | 确认 role/persona/instance 装载完整且共存关系可验证。 |
| `DistributedFbInstanceTable` | 可加载 AO/FB instance、domain、版本、enable 条件、健康状态和共存冲突规则。 | 确认每个 required instance 存在、版本兼容且无资源冲突。 |
| `DistributedEventLinkTable` | START/STOP/FIRE_LOAD/DONE/FAULT/ACK/NACK 的 source、destination、通道、timeout。 | 确认跨节点事件路径完整且 ACK 策略明确。 |
| `DistributedDataLinkTable` | slot 字段 writer/reader、单位、值域、生命周期、snapshot 策略。 | 确认没有多 writer、未声明 reader 或不一致单位。 |
| `DistributedDeploymentGate` | build id、hw profile、config CRC、calibration CRC、sync profile CRC、layout version 和实例共存冲突检查。 | RUN 前一票否决不一致部署或冲突实例组合。 |
| `DistributedConnectionQualityTable` | seq、CRC、stale、late、drop、timeout、last_error、evidence index。 | RUN 中诊断连接质量和报告闭环。 |

这些静态表最终由 `DistributedRefMemAO` 组合为内部 `RefMemSlotContract` 契约视图。RTOS 任务或业务 AO/FB 不直接读写裸 RefMem 字段；它们通过各自 owner API 投递事实或读取 snapshot，由 `DistributedRefMemAO` 按字段契约校验 writer、值域、version、timestamp、stale、CRC 和订阅分发。

启动和 profile 激活时，`task_refmem_sync` / `DistributedRefMemAO` 还必须聚合全环 `SlotClaim` 摘要。A0-A7 是 active profile / epoch 内的全局唯一逻辑插槽；同一物理板可按预规划 claim 多个不同插槽，但两个物理板 claim 同一插槽必须进入 `CLAIM_CONFLICT`，并由 DeploymentGate 拒绝相关 required slot 进入 RUN。

| 区域 | Offset | 大小 | 内容 | 写入者 |
|---|---:|---:|---|---|
| Header/Directory | `0x0000` | 1 KB | magic、layout、slot offset、table_seq、epoch、crc32 | `task_refmem_sync` |
| SystemSlot | `0x0400` | 1 KB | system_mode、role_map_version、run_id、fault_latch、release gate | `task_system` |
| Role/ConfigSlot | `0x0800` | 2 KB | NodeRoleMap、hw_profile、persona、feature mask | `task_system` / config loader |
| VdcSlot | `0x1000` | 2 KB | sync_id、offset、rate、lock_state、holdover、relock、`e_vdc` | `task_vdc_sync` |
| LoopSlot | `0x1800` | 4 KB | trigger param、angle sweep/breakpoint、active sequence、scan_index | `task_loop_engine` |
| DpllSlot | `0x2800` | 2 KB | compare 捕获、角度预测、`T_fire_base`、`e_pll` | `task_dpll` |
| NodeSlot[8] | `0x3000` | 4 KB | A0-A7 通用插槽的 node_id、装载摘要、heartbeat、local_state、error_code、stale_count | 各节点 owner |
| TriggerSlot[8] | `0x4000` | 8 KB | armed、last_fire_seq、late_count、t2_count、ready_timeout | 各节点 core1 摘要 |
| IoSlot[8] | `0x6000` | 8 KB | SMA/RJ45/BiSS IO 镜像、边沿计数、健康状态 | 各节点 IO owner |
| CalibrationSlot | `0x8000` | 8 KB | link table、delay table、staging/active/version/quality | `task_calibration` |
| StatisticsSlot | `0xA000` | 8 KB | `e_vdc/e_act/e_pll`、CRC/seq/late 分布、p99/p999 | 各统计 owner |
| AckCommandSlot | `0xC000` | 4 KB | command_seq、ack/nack/busy/timeout 位图、原子命令槽 | 命令 owner + 节点 ack |
| FaultEvidenceSlot | `0xD000` | 6 KB | fault_code、source_node、epoch、run_id、关键证据 | `task_system` |
| GatewaySlot | `0xE800` | 2 KB | A3/VNA/host 状态、采集状态 | `task_gateway_a3` |
| OtaStorageUiSlot | `0xF000` | 2 KB | OTA、Storage、UI 摘要 | 对应 task owner |
| TlvExtension | `0xF800` | 2 KB | versioned TLV、未来扩展 | owner by type |

表尾固定为 `0x10000`，总大小固定 64 KB。更完整的 Header/Directory、slot guard、owner、snapshot、Version Bundle 和时间回绕契约，以 `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md` 为准。

完整表不等于整表高频同步。RJ45_SYNC_RING 上只同步变更 slot 的小 delta：

```text
REFMEM_DELTA(slot_id, slot_version, compact payload)
REFMEM_EPOCH(epoch, run_id, table_seq)
```

本地查询必须读快照，不临时跨板阻塞查询；slot stale 时返回 stale 标志。

## VDC / DPLL / T2 链

VDC 是 HAOFV 内部基础主域，不只是 `SYNC_IO` 中的一个算法函数。详细主域契约以 `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md` 为准；本文只保留 RTOS task 和双核 AMP 落地视角。

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
- `PIO_SM0: SYNC_RX_CAPTURE` 和 `PIO_SM1: SYNC_TX_FIRE` 只是首版参考路径；具体 PIO instance、GPIO、DMA channel 和 ring 大小由后续 board profile / SYNC_IO 资源适配决定。

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
