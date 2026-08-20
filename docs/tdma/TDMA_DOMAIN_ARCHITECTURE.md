# TDMA 基础件主域架构

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Related: `docs/tdma/TDMA_CLK_TRAINING_PLAN.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`
Last updated: 2026-08-20

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

这里的 ESC 是职责类比，不表示 RP2350 实现或兼容 EtherCAT Slave Controller：

```text
EtherCAT ESC processing-on-the-fly
            |
            v
RP2350 core1 + PIO/DMA deterministic forwarding engine

EtherCAT master/application stack
            |
            v
RP2350 core0 RTOS/domain protocol and application plane
```

core1 不能调用 VDC、RefMem、Trigger 或其他业务解码器，也不能等待 core0 对当前
飞行帧作出决定。core1 只执行由 active `TdmaProcessImageMap` 预先冻结的机械操作：

- 识别 frame boundary，维护固定长度 cyclic frame 的 byte index。
- 普通 byte 原样旁路到下行。
- 到达本节点 input slice 时复制原始 byte，供 core0 在帧后解析。
- 到达本节点 output slice 时写入 core0 在上一周期前已发布的 active TX image。
- 按固定偏移更新 hop / working counter（WKC）和流式 transport CRC。
- 采集 RX/TX edge timestamp、FIFO 水位、overrun 和 deadline evidence。

上述操作是固定位置匹配，不是业务解析。core1 不根据 payload 内容选择代码路径；
slot、offset、length、frame length 和 CRC policy 都来自已通过 DeploymentGate 的
active wire plan，并在 RUN 中保持不变。

#### 八槽短帧与 RX 位图快路径

首版多板 cyclic process image 固定使用 `tdma_flight_engine.h` 中的 wire 常量：短帧
process-image payload 为 256 B，分成 8 个 32 B slot。A0-A7 各自是唯一 writer，
任意 active 节点可以读取其他 slot；2/3/4 板与 8 板使用同一 wire plan，只改变
`active_mask`，不改变 offset 或重新协商帧格式。transport 允许的 260 B SHORT payload
中剩余 4 B 暂不分配给业务 slot。

```text
256 B cyclic payload = slot[0] ... slot[7]
slot[n] = 8 B fast header + 24 B opaque domain payload
```

每个 slot 的 8 B 快速头冻结如下，具体数值必须引用 `TDMA_FLIGHT_MAILBOX_*` 常量：

| byte | 字段 | core1 行为 |
|---:|---|---|
| 0..1 | `magic16` | 只判断是否为 compact mailbox。 |
| 2 | `version` | 只接受当前 wire version。 |
| 3 | `message_class` | 不解析；交给 core0/RefMem。 |
| 4 | `source_slot` | 必须等于 `TdmaProcessImageMap.owner_slot_id`。 |
| 5 | `target_mask` | 只检查本机 bit，形成 fan-out 读取条件。 |
| 6..7 | `seq16` | 对每个 segment 去重；变化时置 RX 位图。 |
| 8..31 | domain payload | core1 完全不解释。 |

这 8 B 是 transport mailbox metadata，不是业务 payload decode。core1 在完整帧透传和
本机 32 B slot 替换之外，只扫描 remote slot 的固定 8 B 头，输出
`input_segment_mask`；mask 为 0 时不得向 RX FIFO 发布空 descriptor，也不得要求 core0
回退为全帧扫描。core0 从 RX FIFO 取得完整帧副本后，只解析位图命中的 slot。由此把
RTOS 调度抖动移出快速过滤路径，同时保持完整帧可供 core0 做 CRC、RefMem commit 和
诊断。

RX 去重状态采用 classify/commit 两阶段：classify 只产生候选 mask，不更新已见 seq；
只有完整帧副本成功发布到 RX FIFO 后才提交该 slot 的 seq16。FIFO 满、buffer pool
耗尽或 descriptor 发布失败时不得提前记住 seq，否则同一 mailbox 的后续重传会被错误
过滤。core0 仍使用完整 seq32 和 RefMem 语义做最终重复、stale 与可见性判断。

读写关系固定为“单写、多方读”：本节点只替换自己的 slot，读取则由其他 slot 的
`target_mask` fan-out。节点间通信通过目标位图完成，不允许把“独立写 slot”误解为
只能读取本机 slot。

#### core0/core1 双 FIFO 与所有权

逻辑上，core0 与 core1 之间有两个方向相反的 SPSC FIFO。它们不是 PIO 的四字
hardware FIFO，也不应逐 byte 跨核握手；推荐实现为“固定缓冲池 + descriptor ring”：

```text
                                  core0 RTOS/domain plane
                         +-----------------------------------+
                         | build next output / parse input   |
                         +-----------------------------------+
                             |                         ^
        TX image FIFO        |                         | RX frame/slice FIFO
        core0 -> core1       v                         | core1 -> core0
                    +----------------+       +----------------+
                    | TX descriptor  |       | RX descriptor  |
                    | + double buffer|       | + buffer pool  |
                    +----------------+       +----------------+
                             |                         ^
                             v                         |
upstream PIO/DMA -> elastic FIFO -> core1 flight engine -> downstream PIO/DMA
                                      |
                                      +-- copy local input slice to RX buffer
                                      +-- replace local output slice from TX image
                                      +-- pass all other bytes unchanged
```

两个跨核 FIFO 的契约冻结为：

| FIFO | producer -> consumer | 内容 | 硬实时规则 |
|---|---|---|---|
| `TDMA_TX_IMAGE_FIFO` | core0 -> core1 | 下一周期 process-image 输出版本的 descriptor；实际数据位于双缓冲或固定池。 | core1 在 frame boundary 原子锁定一个完整版本；无新版本时继续使用上一版本，绝不等待 core0。 |
| `TDMA_RX_FRAME_FIFO` | core1 -> core0 | 已收帧或本节点 input slice 的 descriptor、长度、sequence、timestamp、quality；实际数据位于固定池。 | FIFO 满时只丢弃给 core0 的解析副本并增加 drop/quality counter，不能停止飞行转发。 |

TX/RX FIFO 不要求在同一 RTOS 时刻同步，也不能靠阻塞握手对齐。每个 ring descriptor
都携带 `slot_index + generation + sequence`，consumer 只接受 descriptor 与目标 buffer
slot 三者一致的版本；业务选择再由 `segment_mask` 完成。TX 在 cycle boundary 选择
最新完整 generation，RX 保留该帧 sequence 和位图，因此 core0 即使稍后运行也不会把
旧 descriptor 配到新 buffer。descriptor 异常只能丢弃并计数，不能让 core1 等待修复。

跨核 descriptor ring 使用 single-producer/single-consumer 语义：producer 填完 buffer
后以 release 发布 head，consumer 以 acquire 读取；禁止在 core1 快速路径使用 mutex、
动态内存、RTOS 阻塞队列或等待 core0 acknowledgement。buffer ownership 至少包含：

```text
TX: CORE0_INACTIVE -> CORE0_READY -> CORE1_ACTIVE -> CORE0_INACTIVE
RX: FREE -> CORE1_FILL -> CORE0_PARSE -> FREE
```

`TDMA_TX_IMAGE_FIFO` 的名称表示数据流向，不表示 core1 在 byte 到达时向 core0 逐 byte
请求数据。core0 必须提前构造完整 inactive image，再原子发布 generation；core1 在一帧
开始时锁定 active generation，保证同一帧不会混用两个版本。core0 解析周期 N 的 RX
副本并准备新数据，最早影响周期 N+1：

```text
cycle N wire       : core1 forwards, extracts input, inserts active TX generation G
after cycle N      : core0 parses RX descriptor and builds inactive generation G+1
cycle N+1 boundary : core1 atomically selects G+1 if ready; otherwise reuses G
```

#### core1 飞行替换算法

目标 fast path 可表达为以下固定步骤，伪代码中的 map 已在 ARM 前展开，不进行运行期
payload class 查询：

```c
on_cyclic_frame_start() {
    tx_view = tx_image_acquire_or_reuse();
    rx_view = rx_pool_try_acquire();
    byte_index = 0;
    crc = crc_init();
}

on_upstream_byte(uint8_t input) {
    uint8_t output = input;

    if (fixed_map_input_contains(byte_index) && rx_view != NULL) {
        rx_view->data[fixed_map_input_index(byte_index)] = input;
    }
    if (fixed_map_output_contains(byte_index)) {
        output = tx_view->data[fixed_map_output_index(byte_index)];
    }
    if (byte_index == fixed_map_hop_offset) {
        output = input + 1u;
    }
    if (byte_index == fixed_map_wkc_offset) {
        output = input + local_slice_exchange_succeeded;
    }

    downstream_put(output);
    crc = crc_update(crc, output);
    byte_index++;
}

on_cyclic_frame_end() {
    rx_descriptor_publish_nonblocking(rx_view);
}
```

实现可以按 byte、32-bit word 或固定 block 流水，不要求 CPU 为每个 byte 进入 IRQ。
PIO/DMA 应承担搬运，core1 只处理包含本节点 slice、hop/WKC 或 CRC 的固定 block。上行
与下行由不同板载时钟驱动时，二者之间必须保留有界 elastic FIFO；FIFO 深度覆盖晶振
频差、PIO/DMA arbitration 和最坏 core1 响应抖动，不能假设两个时钟长期同相。

#### CRC、WKC 与错误语义

当前 V1 `TdmaTransportFrame` 的 `transport CRC` 位于 byte 28，而 mutable payload 从
byte 32 开始。节点若在 CRC 已经发出后修改后续 payload，就无法在不缓存剩余帧的
前提下写回正确 CRC。因此 V1 可以用于完整帧 store-and-forward、固定 block cut-through
验证和只修改已延迟覆盖范围内的字段，但不能作为通用的零等待飞行替换最终格式。

产品飞行帧 V2 应将 mutable integrity 字段放到帧尾：

```text
+----------------+----------------------+----------+-----+---------------+
| immutable head | cyclic process image | hop/path | WKC | transport CRC |
+----------------+----------------------+----------+-----+---------------+
         pass / fixed-offset replace --------------------> trailing write
```

- immutable identity CRC 只覆盖 origin、sequence、schedule、ring plan、length 和 immutable flags。
- transport CRC 覆盖节点实际发出的完整字节流，由 core1/PIO 边转发边累计并在帧尾写入。
- 每个 owner segment 保留 generation/segment CRC，供 core0 事后判断本地业务数据是否可提交。
- WKC 仅在本节点成功完成约定 slice exchange 时增加；origin 用期望 WKC 判断所有节点是否工作。
- 输入 CRC 在帧尾才可验证，因此飞行转发是推测性 forwarding：错误帧可能已经离开节点，节点必须增加 error counter、使本地 RX descriptor 无效，并由 origin 的 CRC/WKC/sequence quality 拒绝该周期。
- identity、sequence 和 ring CRC 仍用于 reference TX/feedback RX 闭环相关；mutable payload 和 WKC 不得进入 immutable identity 比较。

#### 过载与故障策略

| 条件 | 行为 |
|---|---|
| core0 没有发布新 TX generation | core1 继续使用上一版，增加 `tx_image_stale_count`；不得阻塞。 |
| core0 消费 RX 过慢 / RX descriptor ring 满 | 丢弃解析副本，增加 `rx_mirror_drop_count`；wire forwarding 继续。 |
| RX buffer pool 耗尽 | 不复制本地 input slice，本周期本地 WKC 不增加或 quality 标记无效；wire forwarding 继续。 |
| core1 elastic FIFO 接近满/空 | 发布 high-water/underflow evidence；超限属于实时 adapter fault，不能静默报告 ring healthy。 |
| downstream PIO/DMA 无法按 deadline 接收 | 中止或标记当前帧并增加 hard realtime fault；不得等待 core0恢复。 |
| 输入尾部 CRC 错误 | 已飞行的下行帧不能撤回；本地副本无效并增加 CRC fault，origin 最终拒绝该周期。 |

管理面 STOP/ARM/TRAIN/START、role、process-image map 和 buffer pool 配置由 core0
提交，但只在 STOP/ARM 边界生效。START 后 core1 是 wire fast path 唯一 owner；SCPI、
UI、LOG 和 core0 domain task 只能读取 snapshot 或通过 FIFO 发布下一周期数据。

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

#### T2 预约 process-image 分发

完整跨域流水线见 `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`。TDMA 为 T2 预约提供确定性 channel，但预约语义仍由 Trigger owner 解释。建议在 active `TdmaProcessImageMap` 中登记四类固定 segment，精确字节布局待 System Pack 和交叉审核冻结：

| segment 语义 | writer | TDMA 操作 | completion 条件 |
|---|---|---|---|
| reservation command | origin Trigger | 按固定 offset 飞行分发 opaque bytes。 | 同 generation 完整绕环且 transport quality 有效。 |
| READY/NACK | each target Trigger | 仅替换 owner slot 获授权 slice，聚合 target mask。 | origin 看到所有目标 READY 或明确 NACK/timeout。 |
| fence | origin Trigger/RefMem publisher | 广播同 reservation generation 的 commit/fence 事实。 | 所有目标看到匹配 fence，才允许本地 ARM。 |
| completion | each target Trigger/Measure | 回填 actual latch、mapped time、result 和 quality 摘要。 | origin 收齐目标 completion mask 或超时结案。 |

TDMA 必须提供以下 transport evidence，但不得据此改变 Trigger 状态机：

- reservation/segment generation、ring sequence、schedule CRC、segment CRC 和 owner slot。
- encoded、queued、window-open、sent、received、validated、returned、fenced/completed 的有界 token。
- prepare lead time、window wait、forward latency、deadline miss、late、retry、NACK 和 timeout 计数。
- READY/fence/completion mask 的 transport 镜像；业务是否满足由 Trigger 读取后判断。

预约分发阶段为：

```text
PREPARE segment admitted
  -> flight distribution
  -> target READY/NACK slices
  -> same-generation feedback reaches origin
  -> fence segment distributed
  -> local execution remains outside TDMA
  -> completion slices return to origin
```

lead time 必须由 active schedule、node count、adapter pipeline、最坏环回窗口、arm guard 和本地装载预算计算，不能在 Trigger 或 TDMA 中写死。窗口不足时 TDMA 返回明确 late/window-missed evidence，不为赶上目标而跳过 READY/fence。

clock-training frame 与 reservation segment 可共享 cyclic process image 和硬件 RX/TX latch 基础，但职责不同：训练 timestamp 提供给 VDC DPLL；预约段只消费 VDC 生成的目标时间。飞行转发在 VDC 未锁定时仍可运行诊断/训练流量，Trigger 是否允许 ARM 由 VDC quality gate 决定。

#### 当前实现与迁移阶段

本节描述的是分阶段实现，不能把固定块替换等同于完整 ESC cut-through。截止
2026-08-20，当前实现已经具备 `TDMA_TX_IMAGE_FIFO`、`TDMA_RX_FRAME_FIFO`、固定
buffer pool、descriptor 的 generation/sequence 一致性校验、8 × 32 B process-image
map、本机 slot 替换，以及 core1 固定 8 B mailbox 头扫描和 RX segment bitmap。core0
只解析 bitmap 命中的 slot，RX FIFO 满或 descriptor 损坏不会阻塞 wire path。

当前 PIO SPI adapter 仍由连续 RX DMA 捕获完整帧，ring adapter 在 core1 service 中完成
transport decode、固定块替换、`advance_hop()` 和 CRC 重算，再把完整帧压入 TX PIO。
因此它属于有界 store-and-forward/固定块飞行处理验证，还没有 elastic byte-level
cut-through、RX/TX DMA 重叠、WKC 和尾部 CRC V2，不能宣称具备最终 ESC 时延特性。

迁移顺序冻结为：

1. 已完成：把完整帧 forward 接入 core1 resident ring service，保持 V1 wire format；后续仍需把软件 service 抖动量化为 RX-complete deadline evidence。
2. 已完成：引入 `TDMA_TX_IMAGE_FIFO`、`TDMA_RX_FRAME_FIFO`、固定 buffer pool、跨核 ownership 和 descriptor version 单测；core1 无需等待 core0。
3. 已完成首版：active `TdmaProcessImageMap` 固定 block 替换，core1 只读 8 B 快速头并发布 RX bitmap；仍需补齐硬件 forward latency、FIFO waterline 和 sequence-gap HIL 门禁。
4. 定义并门禁 V2 cyclic frame：尾部 transport CRC、WKC 和 immutable identity；V1/V2 不得在同一个 active ring 混跑。
5. 实现 PIO/DMA RX/TX 重叠和 elastic FIFO，完成真正 cut-through；以示波器和 HIL 证明固定 per-hop delay、无 underflow/overrun、core0 拥塞不影响 wire。
6. 接入 RX/TX 真实硬件 timestamp latch 和 VDC clock-training evidence；DPLL 不得成为飞行转发的前置依赖，但按 VDC 绝对时间 ARM 的 T2 预约必须通过 VDC quality gate。
7. 接入 T2 reservation/READY-NACK/fence/completion segments，先闭合 store-and-forward 语义，再用 cut-through HIL 证明 lead time 和 per-hop 上界。

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
  -> TDMA profile + operating profile 与当前 VDC ring/schedule/cycle 交叉门禁
  -> TableRegistry active/rollbackable 切换
  -> application model commit
  -> TDMA owner 配置公共 runtime
  -> maintenance snapshot 发布 profile/ring evidence
```

激活约束：

- candidate profile 必须与当前 VDC ring 的 node count、local/reference、upstream/downstream、feedback、ring flags 和 topology CRC 一致。
- 产品 DPLL 模式下 active operating profile 的 `cycle_period_ns` 必须与 VDC schedule 周期一致。PIO-SPI bring-up 且 timestamp 仍为 `DIAGNOSTIC_ONLY` 时，允许 wire cycle 是 VDC 基础周期的整数倍，但不得把这种状态作为 DPLL 准入证据。TDMA runtime 使用 `tdma_operating_profile_schedule_crc32()` 将基础 schedule CRC 与 operating-profile CRC 合成 effective schedule CRC；两板档位不同会在接收门禁中按 schedule mismatch 拒绝，不能继续运行成隐式异步环。
- profile、VDC schedule 或 runtime capacity 任一不一致时，激活以 `RUNTIME_PROFILE` 原因拒绝，active runtime 不接受候选配置。
- 固件启动时内置 factory profile 也通过同一交叉门禁装入 runtime；它只是无 System Pack 时的受控默认值。
- `SYSTem:REFMEM:SYNC:TDMA:STATus?` 仅作为维护投影，在既有字段后追加 profile CRC、owner、adapter、whitelist、ring config/runtime 和 feedback evidence，不驱动窗口续期。

### SPI 速率与 TDMA 周期 operating profile

PIO-SPI bring-up adapter 不接受运行态任意改 baud 或 cycle。`tdma_operating_profile.h`
冻结 `TDMA_OPERATING_PROFILE_COUNT` 个离散组合，每项同时携带 `level`、`baud_hz`、
`cycle_period_ns`、`train_cycles`、`flags` 和 `profile_crc32`。档位表的唯一事实源是
`tdma_operating_profile.c::s_tdma_operating_profiles`；现场查询应使用
`SYSTem:TDMA:OPMode:CATalog?`，文档不得复制一份会漂移的硬编码表。

当前实现快照（2026-08-20，非事实源）：level 0–6 保留原有 10/25/30/35/40/45/50 MHz
与 2 ms wire 周期的兼容编号；level 7–13 使用相同频率梯度与 1 ms 周期；level 14–18
仅保留通过 292 B wire frame 80% 链路负载门禁的 30/35/40/45/50 MHz 与 100 us 周期。
10/25 MHz 在 100 us 下不进入 catalog。另增加 level 19 的 1 MHz/2 ms 保守 bring-up 档，
仅用于多板物理链路和 path-delay 探查，不作为吞吐档。`app_runtime.c` 当前由 1 ms core1 tick 驱动，因此
1 ms 与 100 us 仅作为可配置、可测量的 candidate 开放，不作为已满足调度实时性的
证据；10 us 仍未进入 catalog。
`TDMA_OPERATING_PROFILE_FLAG_HIL_VALIDATED` 只标记通过严格闭环门禁的安全档。
当前 HIL 快照（2026-08-20，非事实源）已完整测试 catalog：2 ms 下 10/25/30 MHz
严格短窗均为 100/A，35–50 MHz 不闭环；1 ms 下 10/25/30 MHz 能达到动态吞吐标准，
但存在 RX bad 或 DMA overrun，35–50 MHz 不闭环；100 us 的目标回环率为 5000/s，
当前 1 ms core1 service tick 只能达到约 1000/s，全部失败。恢复到 10 MHz/2 ms 后的
30 秒窗口为 100/A，坏帧、stall、timeout、overrun 均为 0。由于尚未完成长稳与独立
交叉审核，当前全部档位仍保持 candidate；未来自动策略不得把 candidate 当作降级落点。
在 active-node、绝对 deadline 和 RX ring 容量修正后的定向复测中，level 1、8、9 的
30 秒严格窗口均为 100/A；level 8 的 60 秒窗口也为 100/A，bad、stall、timeout、
overrun 均为 0。该结果只覆盖上述三个 level，不替代其余 catalog 的既有失败结论。

HIL 工具的回环吞吐标准随周期计算：`expected_loop_rate = 1e9 / (2 * cycle_period_ns)`，
即 2 ms/1 ms/100 us 分别要求 250/500/5000 frame/s，并要求实测 TX/RX 中较小值至少
达到目标的 90%。评分还同时扣除 adapter/physical bad frame、stall、TX timeout、DMA
ring overrun 和 TX/RX 不平衡；原始计数与评分版本必须随每档 JSON 一起归档。

SCPI 事务固定为：

```text
SYSTem:TDMA:OPMode:CATalog?       # 读取固件支持的完整离散组合
SYSTem:TDMA:OPMode?               # active + staged + 计数 + last_result
SYSTem:TDMA:OPMode:STAGe <level>  # 只改 staged，不碰线上 PIO
SYSTem:TDMA:RING:STOP             # 两板都先停止
SYSTem:TDMA:OPMode:APPLy          # STOP 状态才允许 active 切换
SYSTem:TDMA:RING:ARM
SYSTem:TDMA:RING:TRAIN <cycles>
SYSTem:TDMA:RING:START
```

`STAGe` 可以在 ring 运行时准备候选档位，但 `APPLy` 在 runtime enabled/ARMED/RUN
时必须返回错误。应用后，下一次 ARM 才把 active `baud_hz` 写入 PIO divider；reference
按 `cycle_period_ns * node_count` 的完整环回周期发帧，follower 仍逐帧转发。不得继续读取
编译常量或使用只适用于两板的 service-count 硬编码二分频。两板必须暂存并应用同一
level，再执行 ARM/TRAIN/START。

这里的 `node_count` 是当前物理环中实际活动节点数，不是 wire/process-image 的槽位容量。
产品 factory profile 使用 `TDMA_PROFILE_DEFAULT_ACTIVE_NODE_COUNT`；最大拓扑仍由
`TDMA_RING_NODE_MAX` 限制，SHORT 飞行处理布局仍固定为
`TDMA_FLIGHT_SHORT_SLOT_COUNT` 个槽。3/4/8 板部署必须通过显式 topology profile 改变
active node count，不能因为 wire 预留了 8 槽就把两板反馈周期放大为 8 个周期。
reference 的发射相位使用绝对 deadline 累加；RTOS service 晚到时只合并漏掉的 deadline，
每次 service 最多发送一帧，不允许用“本次实际发送时刻 + 周期”重新起算而累积 tick 抖动。
连续 RX DMA ring 的容量由 `TDMA_PIO_SPI_RX_RING_WORDS` 冻结，并至少容纳三个
`TDMA_PIO_SPI_RX_DMA_WORD_MAX`。这是飞行处理启用近 292 B SHORT process image 后的
相位余量；不能沿用只针对 32 B idle beacon 的 512-word 缓冲。

自动降级不能由单板因本地误码私自切档。后续自动策略必须由 reference 提议、所有
active 节点确认同一 profile CRC，并执行 STOP -> APPLY -> TRAIN -> START；本阶段只
开放可审计的手动 staging/apply 和 `tools/tdma_ring_monitor/tdma_frequency_sweep.py`
闭环扫频，避免形成两板不同速率的半连接状态。

### EtherCAT DC 风格的训练参数与门禁

这里借鉴 EtherCAT Distributed Clocks 在 bring-up 时使用的四类事实：端口链路状态、
硬件接收/发送时间、传播延时校准、同步窗口裕量。借鉴的是测量和验收方法，不引入
EtherCAT 协议、ESC 寄存器或主站状态机。本节是实现方案；训练状态、报告字段和计算
边界先作为 candidate，精确训练帧 wire layout 在代码和单元测试形成后再按文档登记流程
冻结。

第一阶段完整操作流程、四板 HIL 结果和第二阶段“编码 marker + 相关峰 + 4 ns 过采样”
实施计划独立维护在 `docs/tdma/TDMA_CLK_TRAINING_PLAN.md`。本节保留跨域架构边界，独立
文档作为训练 runbook 和分阶段验收入口。

#### 训练目标与边界

四板环 `NO.1 -> NO.2 -> NO.3 -> NO.4 -> NO.1` 的训练需要回答三个不同问题：

| 结果 | 测量对象 | 用途 |
|---|---|---|
| `spi_clk_round_trip_ns` | SPI TX CLK 的第一个训练边沿从主节点发出、逐节点再生转发并返回同一主节点 RX CLK 的时间。 | SPI CLK 基础训练；建立未知环路延迟的粗捕获窗口，验证 CLK 收发器、线缆和 PIO forwarding path。 |
| `frame_sof_round_trip_ns` | TRAIN frame 首边沿发出到返回帧首边沿。 | 区分 edge path 与帧级转发 pipeline。 |
| `frame_complete_round_trip_ns` | TRAIN frame 首边沿发出到完整返回帧接收完成。 | 直接生成该主节点的 RX 等待窗口和 feedback timeout。 |

第一阶段测得的就是 SPI CLK 环路转发延迟。它包含每段 CLK 线缆/收发器传播和每个节点
从 RX CLK 捕获到 TX CLK 再生的 forwarding residence，但不包含 DATA、CS/frame-sync、
完整帧接收、CRC、固定 offset
替换、转发排队和 RTOS service residence；TDMA 的 START/feedback timeout 必须依据
`frame_complete_round_trip_ns`，不能依据脉冲重叠阈值。

每个 active 节点轮流成为训练主节点，得到 `W[slot]`：

```text
W[slot] = frame_complete_rx_timestamp[slot] - frame_tx_start_timestamp[slot]
```

`W[slot]` 表示“该节点发出数据后，需要等待多久才能收到同一圈返回数据”。运行时不得
在 core0 阻塞等待 `W[slot]`；TDMA scheduler 使用它预约 RX window 和 completion deadline。
校准结果必须绑定唯一板卡地址、logical slot、物理线序/topology CRC、方向、operating
profile CRC、schedule CRC、baud、frame class/length 和 calibration generation。任一绑定
变化都使旧训练结果 stale，并触发重新训练。

#### 训练状态机

推荐由 TDMA owner 在维护态执行以下非阻塞状态机：

```text
STOPPED
  -> PREPARED          相同 topology/profile/schedule，清错误增量和 stale RX/DMA
  -> RX_ARMED          所有节点先开 RX capture，主节点尚未发送
  -> CLOCK_ACQUIRE     指数增加 SPI CLK 脉冲数，得到 CLK forwarding RTT 粗区间
  -> CLOCK_CODED       编码 CLK marker raw-sample 相关，定位 4 ns lag bin
  -> FRAME_MEASURE     短 TRAIN frame 绕环，记录 SOF/EOF 与节点 residence
  -> CALCULATE         计算每主节点 wait、guard、timeout 和质量
  -> VALID             四个主节点均通过，结果绑定到当前 generation
  -> START             启动普通 cyclic traffic

任一阶段失败 -> RELOCKING -> STOPPED/PREPARED
```

`ARM` 只表示 PIO/DMA/RX window 已准备；`TRAIN` 只有在完整输出 accepted sample 和
calibration result 后才表示训练完成。单次 SCPI 成功回执、发送了指定数量的空时钟或
`ring_adapter_started=1` 都不能把状态提升为 `VALID`。

#### 第一阶段：SPI CLK 转发基础训练

所有节点先同时 ARM SPI RX CLK capture/forwarding。当前训练主节点在 TX CLK 发送一段
脉冲，其他节点在 PIO/硬实时路径把 RX CLK 逐边沿再生到本节点 TX CLK，返回边沿由
主节点独立 RX CLK SM/DMA 捕获；主节点只产生初始 burst，收到返回 burst 后不得再次
转发，避免训练时钟在环上无限循环。该阶段不要求 DATA/CS 形成合法 SPI frame，输出
事实明确命名为 `spi_clk_*`，不能泛化成已经完成 SPI DATA 或 TDMA frame 训练。

脉冲数量不使用固定的 `10 -> 100 -> 1000` 大步长，而由配置给出 `pulse_count_start`，
随后按 `pulse_growth_factor` 指数增加，直至检测到“返回首边沿发生在本次 TX burst 完成
之前”或达到 `pulse_count_limit`。设实际硬件时间戳得到的 burst 持续时间为 `D(N)`：

```text
最后一个未重叠 burst: D(N_low)  <= spi_clk_round_trip
第一个发生重叠 burst: spi_clk_round_trip < D(N_high)
```

这只建立 acquisition bracket。达到重叠后应使用同一主节点本地硬件 tick 直接计算：

```text
spi_clk_round_trip_ns = returned_clk_marker_rx_edge - clk_marker_tx_edge
```

最终精度由 edge latch 分辨率决定，不由脉冲数量步长决定。若尚未具备硬件 latch，可在
`N_low..N_high` 间二分，把 diagnostic 粗区间收敛到一个训练时钟周期，但不得将该结果
标记为 DPLL eligible。

连续等间隔 CLK 容易把串扰、反射或上一 epoch 残留误认为返回。基础训练 marker 只编码
在 CLK 上，使用不可歧义的“脉冲组 + gap”图样，并由 host/控制面关联 `train_epoch`；
第二阶段再加入 CLK-only header/反码/CRC 和 timing code，DATA/CS TRAIN frame 留到第三
阶段。节点必须完整再生 CLK marker，主节点必须同时校验图样、返回脉冲数和超时。

当前第一阶段基础路径已由 `tdma_pio_spi_clk_forward`、`tdma_pio_spi_clk_burst` 和
`tdma_pio_spi_clk_capture` 实现：follower 的 PIO SM 独立于 DATA/CS gate 做 RX CLK -> TX CLK
逐边沿再生；master 的 burst SM 自主输出指定 pulse count，capture SM 用 PIO IRQ 记录返回
首边沿。burst SM 在完成点检查返回 IRQ，从硬件顺序区分 overlap/non-overlap；core1 上的
`tdma_pio_spi_phys_train_clock_service()` 只收割结果和执行超时，不参与边沿转发。SCPI
`SYSTem:TDMA:RING:TRAIN` 只写入 `TdmaRingRuntime` command slot，禁止同步直达 PIO；
`SYSTem:TDMA:RING:TRAIN:STATus?` 只读取 seqlock snapshot。训练替换 PIO persona 后，普通
START 必须先 stop/re-arm adapter，恢复 DATA/CS PIO，不能从训练 persona 直接进入 cyclic
service。

2026-08-20 build `20260820133035` 四板 HIL 快照（非规范事实源）使用唯一板卡地址绑定的物理顺序和
`tools/tdma_ring_monitor/tdma_clk_train.py` 轮换四个 master。最低档只得到
`spi_clk_round_trip_ns < 1 us`；在 10 MHz 下，四个 master 均得到同一 diagnostic bracket：
`400 ns <= spi_clk_round_trip_ns < 500 ns`。snapshot 的 timestamp flags 仍为
`TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY`，所以该区间不能进入 DPLL。25 MHz 的单个
40 ns 孤立脉冲未返回，表明下一步必须完成 P0.5-9c 的“脉冲组 + gap”marker、返回计数和
epoch 校验；不能把缺失的窄脉冲当作 non-overlap 样本，也不能据此继续二分。

#### 第二阶段：编码 CLK marker 精测

第一阶段只把 `spi_clk_round_trip_ns` 收敛到约一个有效 pulse 周期。第二阶段继续保持
CLK-only 和 follower 透明逐边沿转发，不引入 DATA/CS；master 发送已知编码 waveform，
RX PIO 在 `CLK_SYS` raw-sample 域捕获返回 waveform，core1 在第一阶段 bracket 内执行有界
相关。该阶段的目标是稳定定位正确的硬件 sample bin，不是解析业务 payload。

##### 分辨率与码本选择

产品当前 `BOARD_SYS_CLOCK_HZ=250 MHz`。单个 PIO SM 使用一条 `in pins,1` 时每个
`CLK_SYS` 周期取得一个 sample，因此 hardware resolution 为 4 ns。编码能够增加正确 lag
与相邻 lag 的 score margin、抵抗毛刺和缺码，但不能把单路 PIO 的单次硬分辨率变成 1 ns。
小于 4 ns 的重复均值必须标为 statistical precision，并同时保留
`hardware_resolution_ns=4`。

码本评估由 `tools/tdma_ring_monitor/tdma_clk_codebook_eval.py` 生成最大长度 Galois LFSR
序列并比较 NRZ、Manchester 和 differential-Manchester 在 raw-sample lag 下的 Hamming
margin。当前 candidate 选择如下；它是设计输入，尚未冻结为 wire contract：

| codebook candidate | timing code | 半码元/逻辑位 | timing field | 用途 |
|---|---|---:|---:|---|
| `M255_MANCHESTER_20` | width-8 m-sequence、candidate mask `0x8E`、seed `0x01` | 20/40 ns | 10.20 us | 默认性能档；最短电平 20 ns，4 ns raw correlation。 |
| `M255_MANCHESTER_40` | 同一码本 | 40/80 ns | 20.40 us | 恶劣链路回退档；不改变 4 ns sample resolution。 |

工具的无噪声模型中，M255 Manchester 有 382 个 waveform transition，相邻 4 ns lag 的
理想 Hamming distance 为 383；同长度 NRZ 为 127。码长提高的是处理增益和错位拒绝裕量，
不是硬件采样率。20 ns 半码元必须先通过四板 HIL；若脉冲缺失、展宽或 margin 不稳定，
全环回退 40 ns 半码元，不允许单节点选择不同 codebook。

##### CLK-only marker

candidate marker 使用以下逻辑字段，全部展开为同一 Manchester waveform：

```text
QUIET_LOW
  -> SOF Barker-13
  -> HEADER16(version2, codebook2, epoch8, master_slot3, polarity1)
  -> HEADER16_INV
  -> HEADER_CRC8
  -> TIMING m-sequence-255
  -> EOF inverted-Barker-13
  -> QUIET_LOW
```

固定 TIMING 字段单独用于 delay correlation。epoch 必须位于独立 header，禁止通过循环移位
timing m-sequence 表达 epoch；否则码相变化会与 path delay 变化混淆。HEADER/INV/CRC、
SOF/EOF 和动态 quiet guard 用于拒绝上一 epoch 残留、反射、极性错误和截断 capture。
quiet guard 由第一阶段 `coarse_high_ns`、profile guard 和硬件恢复时间计算，不能硬编码为
某个只适合当前四板线缆的常量。

candidate 全 marker 在 20 ns 半码元下有 321 个逻辑位、约 3210 个 raw sample、约
101 个 32-bit waveform word；40 ns 回退档约 201 word。最终 TX/capture word count 必须
使用 checked arithmetic，从 codebook、header、coarse RTT、guard 和 DMA alignment 计算，
并受 `TDMA_PIO_SPI_RX_RING_WORDS` 与 foundation profile capacity 门禁。CRC polynomial、
bit order 和 codebook ID 必须等 C/Python golden vector 与 HIL 形成后再登记冻结。

##### Raw-sample correlation

相关器禁止先解 Manchester 逻辑位；否则时间分辨率会被重新量化到逻辑位周期。master 对
固定 timing template `T` 和 RX capture `R` 的有限 lag 集合执行：

```text
D(k) = popcount(R[k : k+L] XOR T)
k_best = argmin D(k)
D_second = min(D(k)), k != k_best
margin = D_second - D_best
```

约 50 ns bracket 在 4 ns 网格上最多约 14 个候选 lag，计算量有明确上限。core1 只能对
固定窗口执行 32-bit XOR/popcount，不动态分配、不搜索无界历史。接受样本必须同时满足：

- `D_best/L` 小于 active codebook/profile 的 HIL 冻结阈值；
- `margin` 大于冻结阈值，正常极性 score 优于反相 score；
- HEADER/INV/CRC、epoch、master、codebook、SOF/EOF 全部匹配；
- TX/RX DMA 完成，capture 未截断，无 overrun/stall；
- 重复 trial 的峰只落在允许的相邻 bin 集合，无孤立远端峰。

最终 observed RTT 语义为：

```text
spi_clk_round_trip_ns
  = capture_origin_ns
  + (k_best - timing_field_tx_origin_sample) * sample_period_ns
  - calibrated_local_endpoint_bias_ns
```

尚无 `calibrated_local_endpoint_bias_ns` 时只能发布 observed RTT，不得把 master 本地 PIO
output、GPIO synchronizer 和 RX pipeline 固定延迟伪装成线缆传播延迟。重复 128 epoch 可
发布 lag histogram、mode、相邻 bin 比例和 statistical mean；即使均值出现小于 4 ns 的
变化，`timestamp_resolution_ns` 仍保持 4 ns。

##### PIO/DMA 与 HAOFV ownership

当前 TDMA PIO2 普通/第一阶段程序约占 24/32 条 instruction；coded TX 使用一条
`out pins,1`，oversampling RX 使用一条 `in pins,1`，预计合计 26/32。master 复用现有两个
SM 分别 TX/RX；follower 只运行已有 CLK forwarding SM，不解析 codebook。

coded TX/RX 都由 DMA 驱动。TX DMA channel 必须进入 `TdmaFoundationProfile` resource
claim，不能在 phys 层私自选择未声明 channel；RX DMA buffer 和 TX waveform buffer 在训练
期间由 core1/adapter 独占，core0、SCPI、USB 和日志只能读取完成后的 guarded snapshot。
master 必须先生成有界 buffer、配置 RX/TX DMA并预装 TX FIFO、清 IRQ/FIFO，再用
`pio_enable_sm_mask_in_sync()` 同步启动两个 SM。snapshot 至少记录 capture origin、timing
field TX origin、DMA transfer count、peak/second/margin、polarity、epoch/codebook 和
hardware/statistical resolution。

板内完整训练由显式指令触发，不在上电时自动注入 CLK。最小闭环允许 host 按 `*IDN?`
唯一地址编排各板，但实时 capture/correlation 全在板内；产品闭环由 reference 在普通 TDMA
persona 下完成 TRAIN_PREPARE/ACK/commit sequence，收齐 active-node bitmap 后统一切换
training persona。训练结束统一恢复普通 persona并停在 STOPPED，后续 START 仍需显式触发。

第二阶段完整码本评估、marker 字段、相关算法和 HIL 计划见
`docs/tdma/TDMA_CLK_TRAINING_PLAN.md`。

#### 第三阶段：短 TRAIN frame

CLOCK_ACQUIRE 和 CLOCK_CODED 均通过后，主节点发送独立的短 TRAIN frame。该帧应保持固定小端编码、
独立 CRC 和有界长度，并携带或关联以下事实：

```text
train_epoch / train_seq / master_slot / origin identity
hop_count / topology CRC / operating profile CRC / schedule CRC
reference TX edge / TX done
每节点 RX edge / TX edge 或对应 evidence index
返回 reference 的 RX edge / RX complete
```

中间节点在同一个本地时钟域计算自身 residence，不需要节点间时钟已经同步：

```text
residence_ns[i] = node_tx_edge[i] - node_rx_edge[i]
```

主节点按同一 `train_seq` 计算：

```text
frame_sof_round_trip_ns = feedback_rx_edge - reference_tx_edge
frame_complete_round_trip_ns = feedback_rx_complete - reference_tx_edge
ring_non_residence_ns = frame_sof_round_trip_ns - sum(residence_ns[i])
```

adapter 必须根据实际 frame length 和 active baud 计算 `frame_wire_time_ns`，不能复制某个
固定帧长的手算值。store-and-forward 下首帧返回时间至少包含各跳 wire time 和节点
residence；cut-through 下首帧返回主要包含 edge/pipeline delay，而完整返回仍需再包含一帧
wire time。训练报告必须声明当前 adapter 是 `STORE_AND_FORWARD` 还是经 HIL 证明的
`CUT_THROUGH`，不能混用两套公式。

#### 四主节点轮换与可观测边界

训练协调器按物理线序依次选择每个 active slot 为主节点。身份仍通过 `*IDN?` 唯一地址
确认，COM 号只作为本次连接端口，不进入校准键。每轮其他节点保持 forward，完成后保存：

```text
master slot 0 -> W[0]
master slot 1 -> W[1]
...
master slot N-1 -> W[N-1]
```

各 `W[i]` 的差异可定位主节点 TX/RX endpoint skew 或某节点被排除/包含时的 residence
异常，并直接生成每节点等待窗口。但单向环中，所有完整 RTT 都经过同一组物理链路；仅靠
轮换主节点的整圈 RTT 不能唯一分离每根线缆的单向传播延迟。首版允许发布：

- 每个主节点的完整环回等待时间和 timeout；
- 每个节点在自身同一时钟域内测得的 residence；
- 整圈扣除 residence 后的 aggregate propagation/pipeline delay；
- 在已有粗同步和合法 timestamp evidence 后形成的 reference-to-slot cumulative delay。

独立 per-link 单向 delay 只有在增加反向测量、相邻链路隔离回环或等价的双端时间戳方程
后才能发布。证据不足时不得把 aggregate delay 平均分摊到各 link，也不得把默认零表
标记为有效 `PATH_DELAY`。

#### 等待窗口和 timeout 生成

每个主节点完成多次有效环回后，按 `frame_complete_round_trip_ns` 统计
`min/max/mean/stddev/p99`。建议由 profile 提供 acquisition 和 guard 策略，计算语义为：

```text
expected_return_ns = frame_complete_round_trip_mean_ns
rx_window_start_ns = expected_return_ns - guard_before_ns
rx_window_end_ns   = expected_return_ns + guard_after_ns
feedback_timeout_ns = max(frame_complete_round_trip_p99_ns + guard_after_ns,
                          frame_complete_round_trip_max_ns + timestamp_margin_ns)
```

首次训练使用独立 `acquisition_timeout_ns`，必须覆盖 active node count、实际 wire time、
store-and-forward residence 和 RTOS 最坏调度延迟；它不能复用稳态单周期 deadline。训练
完成后再收紧 steady-state RX window。若 timeout 或 guard 不能放入 active TDMA cycle，
该 operating profile 直接判为容量不成立，不能靠丢弃迟到反馈继续提速。

#### 训练报告

板端 snapshot、SCPI 只读投影和 host JSON 至少应能表达以下参数：

| 类别 | 参数 | 训练用途 |
|---|---|---|
| 拓扑/配置 | `reference_slot`、`active_node_count`、`local_slot`、上/下行邻居、`profile_crc32`、`schedule_crc32` | 证明所有板在同一拓扑、同一速率和同一周期上训练；任何 CRC 不一致都拒绝进入测量态。 |
| 训练控制 | `train_state`、`train_epoch/seq`、当前 master、pulse start/current/limit、chunk/gap、sample/accepted/rejected count | 区分本地空时钟已发送、CLK 已环回、frame 已环回和完整校准有效。 |
| 主节点时间戳 | `clk_tx_first/done`、`returned_clk_rx_first`、frame `tx_edge/done`、`feedback_rx_edge/complete` | 分别计算 SPI CLK forwarding RTT 和 frame SOF/complete RTT。 |
| 节点时间戳 | 每 slot 的 `clk_rx_edge/clk_tx_edge/clk_forward_residence` 及 frame `rx_edge/tx_edge/residence`、timestamp source/resolution/flags | 分离 CLK 再生延迟与帧转发延迟；`rx_extract` 只能作为软件排队诊断。 |
| 延时校准 | SPI CLK/frame RTT 的 `min/max/mean/p99/stddev/jitter`、aggregate/cumulative delay、freshness、cal CRC、update seq | 先证明 CLK 基础训练，再生成每主节点 RX window/timeout并受控发布 active calibration。 |
| 帧质量 | `rx_good`、`rx_bad`、`seq_gap`、`crc/magic_fail`、`rx_stall`、`tx_timeout`、`dma_overrun`、`window_miss` | 训练期间必须按增量计数，不能只读取累计总数或只看 `ring_up/down_running`。 |
| 调度裕量 | `frame_wire_time`、acquisition/feedback timeout、guard、RX window、arm/start/done、late、window margin | 确认完整返回和方向切换在 guard 内，为下一速率档提供可量化余量。 |
| 链路状态 | 每一跳 link up、RX 首帧时间、连续无帧时长、方向冲突/CSN 错误 | 区分物理断链、方向/线序错误和纯时序不满足；首跳没有 `rx_edge` 时不进入 path-delay 调参。 |

`tx_done`、`rx_complete` 和 `rx_extract` 可以定位 wire completion、DMA 和软件排队，但不得
替代 `rx_edge` 参与 DPLL phase sample。所有时间戳必须带 `timestamp_source`、
`timestamp_resolution_ns` 和 `timestamp_flags`；只有硬件锁存、分辨率不大于 100 ns、
且不带 `DIAGNOSTIC_ONLY` 的样本才能成为正式训练证据。

整体执行顺序为：

```text
STOP all nodes
  -> APPLY identical operating/topology profile
  -> clear counters and stale RX FIFO
  -> ARM all nodes
  -> for each master: CLOCK_ACQUIRE -> CLOCK_CODED -> FRAME_MEASURE
  -> calculate W[slot], guard and timeout
  -> publish valid calibration only after all active masters pass
  -> START cyclic traffic
```

训练通过条件：所有 active master 都收到匹配 epoch/seq/CRC 的返回 edge 和完整 TRAIN
frame；每个节点产生有效 residence；identity、topology、schedule/profile CRC 连续一致；
`rx_bad/seq_gap/magic_fail/stall/timeout/overrun/window_miss` 在 accepted 统计窗口内均为零；
完整 RTT 持续刷新；生成的 timeout/window 能放入 profile 容量。任一条件失败时状态为
`RELOCKING`，保留原始计数和失败原因，不得用默认 `delay_ns=0` 冒充已锁定。

最低速率训练成功后，reference 按 operating-profile catalog 逐级提出下一档，所有 active
节点共同 STOP/APPLY/ARM/TRAIN/START。每一档重新测量 wire time、RTT、jitter、guard 和
错误增量；失败时全环回退最后一个 `VALID` profile，不能让单节点自行提速或降速。

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
| Trigger / Loop | reservation/READY-NACK/fence/completion token、mask、window/late/quality；目标时间来自 VDC。 | 注册 opaque reservation segments 并提交 payload intent；Trigger 自己解释业务语义。 | 直接占用 ring、写 active image、要求 TDMA 解析动作或修改目标时间。 |
| System / DeploymentGate | resource claim、runtime health、payload registry、adapter caps。 | profile staging、enable/disable、resource arbitration。 | 在 RUN 中热改 active ring。 |
| Diagnostics / Report | TDMA snapshot、quality、evidence index、SVG/CSV 输入。 | 低频查询或显式 bring-up self-test。 | 通过 host 查询续装实时窗口。 |

## 目标代码形态

当前代码已有 `components/tdma/` 公共 service。产品化目标是把它从隐式组件升级为 HAOFV system node / foundation domain：

```text
components/tdma/
  inc/tdma_operating_profile.h
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
  src/tdma_operating_profile.c
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
- T2 reservation/READY-NACK/fence/completion segment 的 owner、generation、mask、lead-time 和 fail-closed 行为。
- 两板同时上/下行 HIL，host 只读监控。
- 后续 A0-A7 节点只扩展 profile 表和容量，不改算法主线。
