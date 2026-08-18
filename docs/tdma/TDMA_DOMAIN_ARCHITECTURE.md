# TDMA 基础件主域架构

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Related: `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`
Last updated: 2026-08-17

本文档定义 TDMA 在 HAOFV 下的基础件主域。TDMA 是分布式硬实时系统的确定性通讯骨架，负责在 core1/PIO/DMA 侧按窗口执行上行、下行、payload、timestamp 和 completion；VDC、RefMem、OTA、诊断等域只挂载 payload 或消费 evidence，不能拥有 TDMA 物理环路。

## 主域定位

TDMA Domain 的正式定位：

```text
Time Division Multiple Access Foundation Domain
```

它回答的问题是：

```text
多节点之间什么时候、沿哪条方向、用哪个 adapter、发送或接收哪类 frame，并如何证明窗口命中和完成。
```

它不回答：

```text
共同时间 offset/rate 如何计算。
共同事实 slot 如何提交。
业务触发状态机下一步做什么。
某块调试板 GPIO 如何临时接线。
```

## HAOFV 层级

TDMA 是 HAOFV 中的基础 service / system node，不是 VDC 子模块。推荐层级如下：

```text
SCPI / UI / System Pack
  -> Domain AO / FB owner
  -> Domain Vector / CommandSlot
  -> VDC / RefMem / Trigger / OTA payload contract
  -> TDMA Domain
       TdmaSchedulerAO
       TdmaRuntimeFB
       TdmaPayloadRegistry
       TdmaRingRuntime
       TdmaQualityVector
  -> TransportAdapter
       PIO_SPI / BISS-C / UART / RS485 / future adapter
  -> REALtime / PIO / DMA / IRQ
```

HAOFV 约束：

| 约束 | 规则 |
|---|---|
| AO/FB owner | TDMA runtime 由 `TdmaSchedulerAO / TdmaRuntimeFB` 或等价基础 service 拥有；业务域只能提交 intent 或注册 payload。 |
| Vector writer | TDMA quality、runtime snapshot、ring seq、miss/late/timeout 只能由 TDMA owner 写入。 |
| Payload registry | VDC、RefMem、OTA、诊断只注册 payload class，不直接拥有 transport。 |
| Resource claim | PIO/SM/DMA、上行/下行组、adapter、GPIO 资源必须进入 RealtimeCapabilityContract / DeploymentGate。 |
| Non-blocking FB | TDMA FB action 只提交/推进窗口状态，不在 core0 阻塞等待物理传输。 |
| Hard realtime side path | PIO/DMA/IRQ 只执行 frame boundary、edge、capture 和最小 evidence 回写。 |

## 职责边界

TDMA Domain 负责：

- 管理 active TDMA schedule、window class、guard、deadline、slot 和 profile CRC。
- 管理上行/下行 runtime，尤其是 `TDMA_UP_LEG` 与 `TDMA_DOWN_LEG` 同时运行的 ring。
- 管理 payload registry、frame class、MTU、short/long frame capacity 和 payload admission。
- 调用 transport adapter 执行 TX/RX，并收集 `FRAME_READY/TIMEOUT/WINDOW_MISSED/OVERRUN`。
- 发布 runtime snapshot：intent/completed seq、arm/start/done timestamp、miss/late、timestamp source/resolution/flags、ring runtime 和 last error。
- 给 VDC 提供 observation window 的硬件 timestamp evidence。
- 给 RefMem 提供 data window 的 delta/ACK/fence/quality completion evidence。

TDMA Domain 不负责：

- 不计算 VDC DPLL。
- 不写 VDC offset/rate/lock/DCO。
- 不提交 RefMem active fact。
- 不执行 Trigger 产品业务动作。
- 不直接解析 CAL/SYNC/MEAS/TRIG/OTA 的业务参数。
- 不把 host 交替 self-test 或单向 leg 成功报告为闭环证据。

## 上行/下行环路模型

TDMA 的基础环路由两组同时运行的单向通道组成。上行和下行不是 VDC 内部实现，而是 TDMA foundation 的 runtime 能力。

两节点形式：

```text
TDMA_UP_LEG    : Board X UP   -> Board Y DOWN
TDMA_DOWN_LEG  : Board Y UP   -> Board X DOWN

closed-loop evidence = UP_LEG + DOWN_LEG 在同一固件运行周期内同时服务
```

N 节点形式：

```text
B0.UP -> B1.DOWN
B1.UP -> B2.DOWN
B2.UP -> B3.DOWN
...
Bn.UP -> B0.DOWN
```

规则：

- 每个物理节点都必须声明基础 TDMA 能力、基础 RefMem 能力和基础 VDC 消费能力。
- A0-A7 是逻辑 slot；物理板 B0-Bn 只是承载 slot 的板级实例，不得在 TDMA 架构中写死 B2 一定是某个业务节点。
- 一块物理板可以同时加载多个逻辑节点实例，但 TDMA ring 上的 active slot、payload class、resource claim 必须唯一、可诊断。
- 单向下发只能证明 leg 可用，不能证明闭环；host 交替 `X->Y` / `Y->X` 只能作为 bring-up 或故障定位。
- `simultaneous_feedback_loop_evidence` 只有在固件内部同时运行两条 leg，并且 RX/TX timestamp 相关性证明反馈回到 reference 后才能置位。

`TdmaRingRuntime` 不得再从 profile 中存在 `up_group_id/down_group_id` 直接推导
`up_running/down_running`。profile 只能证明两条 leg 已配置；运行状态必须由 active
adapter 每次 core1 service 返回。未绑定 adapter 时，runtime 必须保持两条 leg
停止并报告 `ADAPTER_MISSING`，不能用软件状态补成成功。

首版反馈相关条件冻结为：

```text
UP reference TX sequence == DOWN feedback RX sequence
UP reference identity CRC == DOWN feedback identity CRC
adapter schedule CRC      == active schedule CRC
reference_tx_timestamp    <= feedback_rx_timestamp
feedback round trip       <= feedback_timeout_ns
timestamp resolution      <= 100 ns
timestamp flags           = HARDWARE_LATCHED and not DIAGNOSTIC_ONLY
```

任一条件不满足时 `simultaneous_feedback_loop_evidence=0`。序号、帧或 schedule
不一致归 `EVIDENCE_MISSING`；时间戳来源、分辨率、顺序或超时不满足归
`TIMESTAMP_MISSING`。该证据只表达 TDMA 物理反馈环成立，VDC 仍需独立执行
DPLL sample gate、锁定质量和 HOLDOVER 判断。

## Window 与 Payload

TDMA window 是调度单位，payload 是业务载荷单位。

| Window class | owner | 可承载 payload | 消费域 |
|---|---|---|---|
| `VDC_OBSERVATION` | TDMA runtime | `VDC_SYNC_SAMPLE`、`IDLE_BEACON` | VDC 消费 timestamp evidence。 |
| `REFMEM_DATA` | TDMA runtime | `REFMEM_DELTA`、`REFMEM_ACK_FENCE`、`QUALITY` | RefMem 消费同步与 completion。 |
| `MAINTENANCE` | TDMA runtime | 维护帧、低频诊断摘要 | System/Diagnostics 只读或受控动作。 |
| `IDLE_BEACON` | TDMA runtime | 空闲同步/质量帧 | VDC freshness、RefMem quality。 |

规则：

- 普通 RefMem delta 不得抢占 VDC observation window。
- VDC DPLL 样本必须来自 TDMA observation window、硬实时 latch、`timestamp_resolution_ns <= 100` 且非 diagnostic-only。
- RefMem data frame 可以携带 timestamp/quality 摘要，但 payload 本身不参与 DPLL 计算。
- 无业务 payload 时仍需通过 idle beacon 或等价窗口维持 freshness。

### Transport Envelope 与长短帧

所有 adapter 共用同一层 `TdmaTransportFrame`，物理层不解析 VDC、RefMem、
OTA、SD 或 LOG 的内部格式。首版 wire header 固定为 32 B、小端编码：

| 偏移 | 字段 | 宽度 | 语义 |
|---:|---|---:|---|
| 0 | magic / version / frame class | 4 B | 识别 TDMA transport 和 `SHORT/LONG`。 |
| 4 | packet size / header size / origin slot | 4 B | 固定边界、起点和总线截帧。 |
| 8 | transport sequence | 4 B | reference TX 与 feedback RX 的主相关序号。 |
| 12 | payload class / flags / hop count / hop limit | 4 B | 业务类型、反馈要求和环路转发约束。 |
| 16 | schedule CRC | 4 B | 绑定 active TDMA schedule。 |
| 20 | ring profile CRC | 4 B | 绑定 active ring topology。 |
| 24 | identity CRC | 4 B | 覆盖不随 hop 和飞行更新改变的路由身份字段，整圈保持不变。 |
| 28 | transport CRC | 4 B | 覆盖当前 hop 字段和完整 packet，每 hop 转发后重算。 |

长度与运行规则冻结为：

| 帧级 | packet 上限 | 净 payload 上限 | 使用阶段 | 典型 payload |
|---|---:|---:|---|---|
| `SHORT` | 292 B | 260 B | 自动同步、硬实时环路常驻。 | `VDC_SYNC_SAMPLE`、`IDLE_BEACON`、critical `REFMEM_DELTA/ACK_FENCE`、小型控制。 |
| `LONG` | 1024 B | 992 B | 宽松同步或显式 maintenance window。 | 配置块、`OTA_BULK`、`STORAGE_BULK`、批量 LOG/trace。 |

这里的“短帧包含 VDC 和 RefMem”表示同一自动同步 cycle 内可安排多个独立短帧，
而不是把两个域的内部结构强行合成一个共享 payload。VDC 和 RefMem 仍分别拥有
内部 payload schema、CRC 和 completion；TDMA 只拥有外层 transport、顺序和窗口。

对最终飞行模式，允许把多个域的固定小段放入同一个 `CYCLIC_PROCESS_IMAGE`
短帧，但各段仍由 `TdmaProcessImageMap` 明确 owner、offset、length、generation 和
segment CRC，不能让 VDC 直接写 RefMem 段或让 RefMem 直接写 VDC 段。

硬约束：

- `VDC_REALTIME`、`REFMEM_REALTIME` 只能进入 `SHORT` 队列。
- reliable bulk、LOG best effort 只能进入 `LONG` 队列；配置流可按数据量选择短帧或长帧。
- `LONG` 不得在严格自动同步阶段运行；只有 TDMA owner 打开 maintenance gate 且 active schedule 有足够 budget/guard 时才允许发送。
- 当前 VDC 内帧为 216 B，加 32 B transport header 后为 248 B，满足 292 B 短帧上限。
- 当前 RefMem 内帧理论最大为 292 B，不能直接再套短帧外层。PIO ring adapter 接入前必须把 critical delta 的 RefMem 内帧限制为 260 B，其中 RefMem 头 36 B、净 delta 最多 224 B；更大事实使用分片、background delta 或后续专用 bulk class。
- RefMem 不做周期整表刷新。TDMA short queue 只接收由 dirty fact 触发、已经局部编码的 critical delta；首次加入或失步恢复的 full snapshot 只能走 maintenance long-frame 分片。
- `hop_limit` 防止错误拓扑无限转发；origin 收到 `hop_count > 0` 且 identity 匹配的返回帧后停止转发，并形成 feedback candidate。

### EtherCAT-style 飞行处理

自动同步短帧参考 EtherCAT processing-on-the-fly 思想，但不复刻 EtherCAT 协议。
节点不等待完整短帧落 RAM 后再重新发送，而是在固定 byte offset 到达时读取或替换
自己拥有的 process-image segment，其余字节保持流水转发。长帧仍可采用有界
store-and-forward/fragment 方式，因为它只在 maintenance gate 内运行。

目标数据路径：

```text
Domain AO / FB local fact commit
  -> DistributedRefMemAO marks dirty descriptor
  -> RefMemPublishFB encodes compact local segment
  -> write inactive TdmaProcessImage shadow buffer
  -> core1 swaps shadow/active at TDMA cycle boundary
  -> PIO RX/TX + DMA forwards SHORT frame
  -> local owned offset: read input segment / insert prepared output segment
  -> advance hop + update transport CRC
  -> origin receives feedback identity and process image
```

每个节点如何把本节点数据装入 TDMA，由 `TdmaProcessImageMap` 决定：

| 字段 | 作用 |
|---|---|
| `segment_id` | 固定 process-image 段编号。 |
| `owner_slot_id` | 唯一写 owner；一块物理板可承载多个逻辑 slot。 |
| `payload_class` | VDC compact sample、critical RefMem delta、ACK/quality 等段语义。 |
| `byte_offset / byte_length` | 在 260 B short payload 中的固定位置和容量。 |
| `generation / dirty_mask` | 本周期是否有新事实及其版本。 |
| `target_mask` | 哪些节点需要消费或 ACK。 |
| `segment_crc / policy` | 段内完整性、合并、重试和 deadline 策略。 |

约束：

- `TdmaProcessImageMap` 来自 active System Pack / DeploymentGate，不能由节点在 RUN 中自行抢占 offset。
- core0/domain task 只写 inactive shadow；PIO/DMA 只读 active buffer。cycle boundary 由 core1 唯一 owner 原子切换，避免半更新段上总线。
- 无 dirty 时段头发布 `NO_UPDATE` 或等价 generation 状态，对端不得重复提交旧值。
- 状态事实可合并为最新 generation；command/event 使用独立有界队列，不塞进可覆盖的状态段。
- `FLIGHT_MUTABLE` 只允许 `SHORT`。identity CRC 不覆盖可变 payload；每个 segment 自带 owner CRC/version，transport CRC 覆盖当前 hop 的完整 packet。
- origin TX 与 feedback RX 的闭环相关使用 immutable identity CRC、sequence、schedule CRC 和 ring CRC，不能比较飞行前后的 mutable payload CRC。
- 当前 216 B VDC 诊断内帧可作为 bring-up 的独立短帧，但不是最终 process-image 形态；产品飞行帧应使用 compact VDC sample，把余量留给 critical RefMem delta 和 ACK/quality。
- RP2350 首版可以先实现有界 byte/block cut-through；只有 PIO/DMA 实测证明 RX/TX 重叠和固定 pipeline delay 后，才宣称飞行模式成立。

## Adapter 边界

TransportAdapter 是可替换物理承载，不改变 TDMA 语义。

| Adapter | 阶段 | TDMA 视角 |
|---|---|---|
| PIO SPI | 最小系统两板 bring-up。 | 快速验证 window、payload、CRC、completion 和 quality。 |
| BISS-C | 后续通讯基础件。 | 类 IP 核，提供编码/解码、timestamp 和错误摘要。 |
| UART / RS485 | 低速维护或扩展节点。 | 可承载低频 payload，但必须暴露 MTU、latency、timeout 和 quality。 |
| Future bus | 后续扩展。 | 只要满足 frame boundary、timestamp 和 completion contract，即可挂载。 |

Adapter 不得直接写 VDC、RefMem 或 Trigger active fact。它只能返回 TX/RX 执行结果、frame、timestamp metadata 和错误计数。

## TSN-style 资源治理与流控

TDMA Foundation 吸收 TSN 的确定性资源治理思想，但不绑定 IEEE 802.1 协议、以太网帧格式或交换机实现。系统复用的是 traffic class、准入控制、time-aware gate、guard band、整形、背压、逐流质量和可选冗余消重；物理传输仍由 PIO SPI、BISS-C、UART、RS485 或后续 adapter 承载。

| TSN 可借鉴机制 | 本系统映射 | 明确边界 |
|---|---|---|
| 802.1Qbv time-aware shaping | TDMA window/gate、guard band、active schedule CRC。 | 不实现以太网 gate control list；由 `TdmaSchedulerAO` 驱动本地 adapter gate。 |
| 802.1Qci per-stream filtering/policing | payload whitelist、traffic budget、deadline、queue depth、drop/backpressure counter。 | 未通过 admission 的流不进入 core1 队列。 |
| 802.1Qav credit shaping | 配置、OTA、LOG 的 token/credit 或 deficit 预算。 | 不用于 VDC/RefMem 硬预留流，避免实时窗口受动态 credit 影响。 |
| 802.1Qbu frame preemption | maintenance/bulk 帧仅在 frame boundary 可让位。 | 首版不宣称 adapter 支持字节级或 bit 级抢占。 |
| 802.1CB FRER | 后续多环 sequence、replication、duplicate elimination。 | 单环阶段不伪造冗余 evidence。 |

首版固定五类流：

| Traffic class | Payload | 调度与资源规则 | 溢出策略 |
|---|---|---|---|
| `VDC_REALTIME` | `VDC_SYNC_SAMPLE`、`IDLE_BEACON` | 最高优先级；固定 observation/idle gate；严格预留；禁止 OTA、配置和 LOG 借用 guard band。 | 记录 fault/quality，不能静默丢弃后继续报告 LOCKED。 |
| `REFMEM_REALTIME` | `REFMEM_DELTA`、`REFMEM_ACK_FENCE` | 固定 data gate；预留周期字节数和帧数；可靠 completion；不得侵占 VDC gate。 | 有界重试并向 producer 背压，超限 NACK/fence fault。 |
| `CONFIG_CONTROL` | System Pack、配置 staging/activate 控制帧 | 可靠、整形、可被实时流让行；只在 maintenance 或剩余预算中运行。 | producer 背压；不得阻塞 core1。 |
| `RELIABLE_BULK` | OTA package chunk、SD read/write block | 批量、可靠、只使用长帧；默认无硬预留，只消耗显式 maintenance/bulk budget。 | 暂停 producer 并续传，不挤占实时窗口。 |
| `LOG_BEST_EFFORT` | LOG/trace 摘要 | 最低优先级、整形、可被实时流让行；只使用剩余预算。 | 丢最旧记录并增加 drop counter，不能阻塞实时链路。 |

调度优先级是冻结的三级结构，不允许由运行期动态优先级改写：

```text
VDC_REALTIME
  > REFMEM_REALTIME
    > CONFIG_CONTROL / OTA_BULK / LOG_BEST_EFFORT
```

- `VDC_REALTIME` 永远先于 RefMem 和维护流出队，承担共同时间 observation、idle freshness 和 DPLL timestamp spine。
- `REFMEM_REALTIME` 只在没有可执行 VDC 帧时出队，不能借用或延长 VDC guard/window。
- VDC 帧尚未到 guard 但已预约时，RefMem 只有能在该 guard 前完整结束才可启动；否则保持排队并让出 adapter。
- 配置、OTA、LOG 统一属于低优先级 maintenance traffic；三者内部可按可靠性和吞吐排序，但不能提升到 RefMem 之上。
- maintenance gate 默认关闭。只有 `TdmaSchedulerAO` 确认当前不在同步阶段，或 active schedule 进入显式 maintenance window 时才允许打开；SCPI、OTA producer、LOG producer 都无权自行开门。
- 低优先级帧不得抢占实时短帧，也不得在已知的下一实时 guard 前启动一个无法在 guard 前完成的传输。首版 adapter 只在 frame boundary 调度，不宣称字节级或 bit 级抢占。
- 同步阶段新到达的 VDC/RefMem 帧不能被 maintenance backlog 阻挡；如果 adapter 已经执行 maintenance frame，则说明 maintenance gate/窗口规划错误，必须计入 quality/fault，而不能把延迟归咎于实时流。

`tdma_foundation_profile_t` 是上述资源治理的 active contract，必须由 System Pack / DeploymentGate 激活并冻结：

- `owner_instance_id` 唯一标识 `TdmaSchedulerAO / TdmaRuntimeFB` owner。
- `ring` 冻结节点顺序、reference、UP/DOWN group 和 topology CRC。
- `resource` 冻结 adapter、PIO block、两组 SM、TX/RX DMA、core1 service、IO/IP claim、short/long frame capacity 和 payload whitelist。
- `resource` 同时冻结 `cycle_period_ns`、周期容量、guard band 和 queue RAM 总容量，使资源门禁不依赖运行期猜测。
- `traffic[]` 冻结逐类 payload mask、每周期预留字节、每周期最大帧数、队列深度、deadline、gate/shaping/preemption 标志和 overflow policy。
- 所有 payload 必须且只能归入一个 traffic class；未登记 payload 在 registry admission 阶段拒绝。

`TdmaFoundationProfile` 已作为 RMTP/System Pack 的第 10 张正式表：

```text
table id       : 9
table name     : TdmaFoundationProfile
wire format    : fixed u32 little-endian
row count      : 1
row words      : 71
table lifecycle: staging -> CRC -> owner/resource gate -> active -> rollbackable
table owner    : TDMA AO
```

不能直接序列化编译器 C struct。编码器和解码器必须逐字段处理，保证 RP2350、后续 MCU/SoC 和 host System Pack 工具得到相同布局与 CRC。

### 激活事务

第 10 张表不是只供诊断读取的配置副本。它和其余九张 RMTP 表共同参与以下事务：

```text
System Pack / SCPI staging
  -> 10-table CRC + owner/resource validation
  -> prepare candidate table views
  -> TDMA profile 与当前 VDC ring/schedule/cycle 交叉门禁
  -> TableRegistry active/rollbackable 切换
  -> application model commit
  -> TDMA owner 配置公共 runtime
  -> maintenance snapshot 发布 profile/ring evidence
```

激活约束：

- candidate profile 必须与当前 VDC ring 的 node count、local/reference、upstream/downstream、feedback、ring flags 和 topology CRC 一致。
- `cycle_period_ns` 必须与 VDC schedule 周期一致；TDMA runtime 记录同一 `schedule_crc32`，不能自行生成另一个时间表。
- profile、VDC schedule 或 runtime capacity 任一不一致时，激活以 `RUNTIME_PROFILE` 原因拒绝，active runtime 不接受候选配置。
- 固件启动时内置 factory profile 也通过同一交叉门禁装入 runtime；它只是无 System Pack 时的受控默认值。
- `SYSTem:REFMEM:SYNC:TDMA:STATus?` 仅作为维护投影，在既有字段后追加 profile CRC、owner、adapter、whitelist、ring config/runtime 和 feedback evidence，不驱动窗口续期。

资源与流控规则：

- VDC/RefMem 使用 time-aware gate 和 guard band；OTA/配置/LOG 不得通过动态优先级反转进入这些窗口。
- 公共 runtime 只能有一个 `TdmaSchedulerAO`；VDC、RefMem 和维护 producer 注册到同一 payload registry/traffic scheduler，core1 每轮只推进一次公共 service。
- `maintenance_gate_open` 是 TDMA owner 的内部事实，默认关闭；业务域和维护命令只能提交 intent，不能直接修改门状态。
- core1 只推进已准入队列和 gate，不等待 producer；背压通过 command/vector evidence 返回对应 AO/FB。
- System Pack 激活前必须检查所有 class 的总预算、adapter MTU、窗口容量、DMA/SM/IO claim 和 queue RAM 水位；超配直接由 DeploymentGate 拒绝。
- profile owner 必须唯一对应一个已加载的 `TdmaSchedulerAO`；owner 的 NodeLoad、SlotClaim、RealtimeCapabilityContract、IO/IP claim 和 adapter 资源必须一致。
- TDMA communication adapter IO 只能由 TDMA owner 占用；业务 AO/FB 通过 payload/intent 使用 TDMA，不得重复声明物理 ring IO。
- 可借鉴 TSN policing：逐流统计 late、deadline miss、drop、retry、backpressure 和 budget overrun，并写 `TdmaQualityVector`。
- 多路径或多环冗余后续可借鉴 FRER 的 sequence/duplicate elimination，但首版两板单环不引入无证据的冗余成功状态。
- RefMem 后续应按 region/slot criticality 拆分 critical delta 与 background delta；首版 `REFMEM_REALTIME` 先承载 delta/ACK/fence，运行测得水位后再细分，不能默认所有 64 KB 事实都占用硬预留带宽。

## 跨域契约

| 消费域 | 从 TDMA 读取 | 向 TDMA 提交 | 禁止 |
|---|---|---|---|
| VDC | observation timestamp、schedule CRC、ring quality、late/miss。 | `VDC_SYNC_SAMPLE` / `IDLE_BEACON` payload registration 和 observation window profile。 | 直接拥有 transport，写 ring runtime，伪造 closed-loop evidence。 |
| RefMem | data window completion、ACK/fence quality、adapter counters。 | `REFMEM_DELTA` / `REFMEM_ACK_FENCE` payload registration 和 pending delta intent。 | 把 TDMA 当作私有同步线程，绕过 payload registry。 |
| Trigger / Loop | VDC snapshot、必要时读取 TDMA quality。 | 通过 VDC/Realtime 提交 FIRE_LOAD 或 trigger intent。 | 直接占用 TDMA communication ring。 |
| System / DeploymentGate | resource claim、runtime health、payload registry、adapter caps。 | profile staging、enable/disable、resource arbitration。 | 在 RUN 中热改 active ring。 |
| Diagnostics / Report | TDMA snapshot、quality、evidence index、SVG/CSV 输入。 | 低频查询或显式 bring-up self-test。 | 通过 host 查询续装实时窗口。 |

## 目标代码形态

当前代码已有 `components/tdma/` 公共 service。产品化目标是把它从隐式组件升级为 HAOFV system node / foundation domain：

```text
components/tdma/
  inc/tdma_profile.h
  inc/tdma_service.h
  inc/tdma_payload_registry.h
  inc/tdma_process_image_map.h
  inc/tdma_ring_runtime.h
  inc/tdma_pio_spi_ring_adapter.h
  inc/tdma_pio_spi_phys.h
  inc/tdma_traffic_scheduler.h
  inc/tdma_runtime_owner.h
  inc/tdma_transport_frame.h
  src/tdma_profile.c
  src/tdma_service.c
  src/tdma_payload_registry.c
  src/tdma_process_image_map.c
  src/tdma_ring_runtime.c
  src/tdma_pio_spi_ring_adapter.c
  src/tdma_pio_spi_phys.c
  src/tdma_pio_spi.pio
  src/tdma_traffic_scheduler.c
  src/tdma_runtime_owner.c
  src/tdma_transport_frame.c
```

过渡规则：

- `TdmaPayloadRegistry` 已从 `tdma_service.c` 拆出；`tdma_service` 保留聚合 API，并委托注册、whitelist、capacity 和 admission。
- `TdmaRingRuntime` 已从 `tdma_service.c` 拆出；`tdma_service` 保留聚合配置与 snapshot API，并委托 ring config、core1 service 和 seqlock snapshot。
- `TdmaPioSpiRingAdapter` 实现 `TdmaRingAdapterOps`，作为 PIO SPI bring-up transport 的 ring adapter；它只编解码 `TdmaTransportFrame`，维护 UP/DOWN sequence、identity CRC、idle beacon 计数和 timestamp 元数据，不接触 VDC/RefMem 内帧。
- `TdmaPioSpiPhys` 是 PIO/DMA 常驻物理层（当前最小系统为 frame-sync/CS + DATA + CLK 的三线单向腿：发送端闲置 RX/CS `GPIO21`、TX/DATA `GPIO23`、CLK `GPIO24` -> 对端闲置 TX/CS `GPIO16`、RX/DATA `GPIO18`、CLK `GPIO19`，downlink master TX + uplink slave RX 双 SM 同时 arm）；它由 ring adapter 的 start/stop 回调经 `set_phys_ctrl` 驱动 arm/disarm，`set_phys` 提供帧级收发钩子。CS 在这里表示点对点 `FRAME_SYNC`，不是多从机片选。
- **Adapter 模块化边界（HAOFV）**：`tdma_service` 维护 `adapter_type -> TdmaRingAdapterOps` 注册表（`tdma_service_register_adapter_impl()`），`tdma_service_configure_foundation_profile()` 按 active profile 的 `resource.adapter_type` 绑定对应实现；未注册类型解绑并报告 `ADAPTER_MISSING`。当前注册 `TDMA_ADAPTER_PIO_SPI`；后续 `TDMA_ADAPTER_BISS_C / UART / RS485` 以独立 adapter 模块注册即可切换，不改变 ring runtime 契约。
- RefMem 侧 `refmem_realtime_tdma` 只保留兼容 adapter，不再拥有调度器；其命令式 `refmem_spi_physical_adapter` 是历史 SCPI 驱动路径，TDMA 常驻环启用后由 ring owner 独占 pio0 SM2/SM3，业务维护路径不得再 arm 同一组 SM。
- VDC 侧 `SYSTem:SYNC:VDC:TDMA:*` 只能作为 VDC maintenance projection，不能表示 VDC 拥有 TDMA。
- 后续新增 TDMA maintenance command 时，应挂载在系统维护命名空间，例如 `SYSTem:TDMA:*`，并保持对外产品业务命令不直接操作 TDMA。

### Ring reason code

`TdmaRingRuntime` 冻结以下诊断原因，后续 adapter、scheduler 和 quality vector 只能映射这些稳定语义，不能各自发明错误编号：

| Reason | 含义 |
|---|---|
| `NONE` | 当前无 ring fault。 |
| `BAD_CONFIG` | 节点数、slot、flag 或 CRC 不合法。 |
| `EVIDENCE_MISSING` | runtime 已推进，但缺少闭环证据。 |
| `DIRECTION_CONFLICT` | UP/DOWN group 缺失、相同或方向冲突。 |
| `ADAPTER_MISSING` | active profile 没有可执行 transport adapter。 |
| `TIMESTAMP_MISSING` | 缺少符合约束的硬件 timestamp。 |
| `PAYLOAD_STARVATION` | 预留窗口没有获得要求的 payload/beacon。 |
| `WINDOW_MISSED` | runtime 未命中 active schedule window。 |
| `RESOURCE_CONFLICT` | PIO/SM/DMA/IO/IP claim 冲突。 |

当前首版直接产生 `BAD_CONFIG` 和 `DIRECTION_CONFLICT`；其余 reason 已冻结编号，待 adapter、time-aware scheduler 和 timestamp correlation 接入后按 owner 边界发布。

## 验证门禁

TDMA Domain 最小验证必须覆盖：

- payload registry admission/rejection。
- TX/RX intent seqlock 和跨核唯一 writer。
- window guard、late、miss、timeout、overrun。
- ring config 校验：节点数、slot、UP/DOWN group、profile CRC、schedule CRC。
- runtime snapshot：`up_running/down_running/ring_seq/last_error`。
- 禁止伪造 `simultaneous_feedback_loop_evidence`。
- RefMem delta 单发丢失后的 ACK/重发/fence completion。
- VDC observation window 的硬件 timestamp eligibility。
- 两板同时上/下行 HIL，host 只读监控。
- 后续 3 节点、5 节点只扩展 profile 表，不改算法主线。
