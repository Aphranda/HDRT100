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

## Adapter 边界

TransportAdapter 是可替换物理承载，不改变 TDMA 语义。

| Adapter | 阶段 | TDMA 视角 |
|---|---|---|
| PIO SPI | 最小系统两板 bring-up。 | 快速验证 window、payload、CRC、completion 和 quality。 |
| BISS-C | 后续通讯基础件。 | 类 IP 核，提供编码/解码、timestamp 和错误摘要。 |
| UART / RS485 | 低速维护或扩展节点。 | 可承载低频 payload，但必须暴露 MTU、latency、timeout 和 quality。 |
| Future bus | 后续扩展。 | 只要满足 frame boundary、timestamp 和 completion contract，即可挂载。 |

Adapter 不得直接写 VDC、RefMem 或 Trigger active fact。它只能返回 TX/RX 执行结果、frame、timestamp metadata 和错误计数。

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
  inc/tdma_service.h
  inc/tdma_ring_runtime.h        # 后续可拆
  inc/tdma_payload_registry.h    # 后续可拆
  src/tdma_service.c
  src/tdma_ring_runtime.c        # 后续可拆
  src/tdma_payload_registry.c    # 后续可拆
```

过渡规则：

- 首版可以继续保留 `tdma_service.h/.c` 单体，但公开 snapshot 必须表达 ring runtime 和 payload registry 边界。
- RefMem 侧 `refmem_realtime_tdma` 只保留兼容 adapter，不再拥有调度器。
- VDC 侧 `SYSTem:SYNC:VDC:TDMA:*` 只能作为 VDC maintenance projection，不能表示 VDC 拥有 TDMA。
- 后续新增 TDMA maintenance command 时，应挂载在系统维护命名空间，例如 `SYSTem:TDMA:*`，并保持对外产品业务命令不直接操作 TDMA。

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

