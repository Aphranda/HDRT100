# 实时负载挂载约束与标准流程

Status: Draft
Domain: ARCH
Canonical: `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/ARCH_ACTIVATION_LOAD_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `application/inc/app_realtime_schedule.h`
Last updated: 2026-10-13

本文是跨域实时负载的挂载实施规范草案。它整理当前代码中的 phase、owner、TDMA
服务等级和 T2 接线边界，供后续组件提交挂载申请时使用。本文不新增运行 owner，
不改变已经登记的 wire layout 或 VDC 契约；冻结前仍需独立复核，并按文档自回归
流程登记相应契约。

## 1. 两条轴必须分开

实时系统有两种不同的分类，不能互相替代：

| 维度 | 回答的问题 | 当前事实源 |
|---|---|---|
| phase | Core1 在哪个固定时间窗口执行服务？ | `APP_REALTIME_PHASE_TABLE`、`PROJECT_CORE1_PHASE_*`、`app_realtime_run_phase()` |
| seat | 载荷在 TDMA/共享运输资源上得到什么服务等级？ | `TDMA_DOMAIN_ARCHITECTURE.md` 的四级负载表、各 payload class 和 owner API |

phase 是 CPU 调度边界，seat 是服务目标和拥塞策略。一个功能只能绑定一个
Core1 执行 phase，但它可以同时拥有一个实时执行 phase 和一个独立的 wire
transport seat。例如 T2 在 `SYNC_TRIGGER` phase 执行预测与本地动作；T2
position descriptor 在 TDMA 中使用独立的 T2 message class；普通帧的 VDC 状态
保持原偏移，T2 片段占用较低优先级的 RefMem 区域。不能把 T2 的 descriptor 代码塞入 VDC phase，
也不能用 T2 传输替换 VDC 同步发布。

## 2. 功能域、唯一 owner 与 phase 挂载

| 功能域 | 运行 owner | 当前 phase / 入口 | 允许的工作 | 明确禁止 |
|---|---|---|---|---|
| TDMA Foundation | `tdma_runtime_owner` / physical owner | `TDMA`；`tdma_component_core1_service()` 与唯一 `tdma_runtime_owner_service_observer()` | PIO/SM/DMA、环服务、RX observer、ring clock snapshot、有限 RefMem 发布交接 | VDC servo、DPLL PI、T2 业务解算、SCPI/SD 等待 |
| VDC | `vdc_dpll_manager` 的 VDC service | `VDC`；`vdc_sync_ao_service()` | 消费 TDMA clock snapshot，发布共同时间状态和 `sync_seq` | 调用 TDMA observer、解释业务动作、写 T2 descriptor、直接操作动作 PIO |
| DPLL | `vdc_dpll_manager` 的 DPLL service | `DPLL`；`sync_dpll_fb_service()` | 消费 VDC evidence，运行 DPLL/model feedback 和质量老化 | 再次运行 TDMA observer、发布 T2 位置、借用 VDC 或 GUARD 预算 |
| SYNC capture | `vdc_dpll_manager` / `sync_io` capture owner | `SYNC_CAPTURE` | 受控的同步输入捕获和快照 | 改写 VDC/DPLL owner 状态、执行动作模板 |
| RefMem | `distributed_refmem` | `REFMEM`；`distributed_refmem_realtime_run_once()` | 固定向量、命令槽、ACK/fence 和低频镜像交接 | 拥有 TDMA PIO、改变 VDC sync cadence、执行 T2 动作 |
| T2 reservation | `sync_trigger` / `trigger_t2_runtime` | `SYNC_TRIGGER`；`sync_trigger_service()` | 只读 committed DCO 与本地 TIMER1 推进 cursor，交接异步预测结果、stage descriptor、解析本地时间片并执行模板 | 写 VDC/DPLL 状态、在目标时刻等待主站命令、运行中热加载 PIO/IO route |
| Model / turntable | `model_turntable` | `MODEL` | 转台模型和低频预测输入准备 | 直接发布 VDC、占用 TDMA fast path |
| Diagnostics / measure | 各自 Core0 owner | `TRIGGER_MEASURE` 或 Core0 维护路径 | 读取快照、记录证据、导出波形和报告 | 进入 Core1 特等席、阻塞实时 phase、把诊断计数当正式 sync_seq |

`SYNC_TRIGGER` 是 T2 的预留挂载点。T2 的 VDC 输入是只读事实：从板接收同步
信息并更新本地模型，T2 复用 committed DCO 与 TIMER1 派生本地 coarse/fine cursor。
接收观察的 `vdc_status.sync_seq` 不代表每个本地毫秒格；其未更新时本地时间仍推进。
游标、时间绑定与动作执行不增加 TDMA 载荷。T2 不向 VDC 反向写入同步状态，
也不通过增加 VDC service 工作来实现位置预测。

## 3. 四级 seat 与负载归属

当前四级服务采用“特等、一等、二等、无座”四类。无座是第四类服务，不能
把它误写成第三等实时席位。

| seat | 典型内容 | owner / 运输方式 | 准入规则 | 拥塞处理 |
|---|---|---|---|---|
| 特等 | DPLL/VDC 关键同步记录、确定性 SYNC 时间需求 | VDC/DPLL owner + TDMA priority typed mailbox + SYNC_IO PIO/DMA | 固定 codec、固定资源、独立 RX/TX 保全；新增或扩容必须先完成席位审核 | 缺口、旧代、覆盖、CRC 或 identity 失败必须显式记录；不能静默降级为普通镜像 |
| 一等 | 其余 VDC 状态、跟随数据、T2 position descriptor | 各自 domain owner；T2 使用 `TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS`（0x15）经 origin 快速路径的一等邮箱（`tdma_first_class.h`）运输，与特等按发布机会轮换（当前轮换比例为编译期常量 `TDMA_SEAT_ROTATION_*_SHARE`，取值 1:1） | 有独立 generation、CRC、有效同步区间和 freshness；不得影响特等 cadence | typed SYNC/专用 VDC 命令优先；特等无新内容时一等使用该机会、`EMPTY` 不占座；一等记录不再改写已排队 VDC image，也不再借用二等 RefMem 区域 |
| 二等 | RefMem delta、ACK/fence、可靠控制事实 | RefMem owner + 普通 process image / dirty queue | 固定段、dirty 标记、有界配额和背压；运输成功不自动提交业务事实 | 延后、重试或背压；不能借特等窗口或改写 active VDC 状态 |
| 无座 | 普通控制、日志、维护、UI、SD、离线分析 | Core0 AO、维护 phase 或 STOP 后路径 | 不得要求 Core1 实时席位，不得持有 PIO/SM/DMA 实时资源 | 丢弃、延后或在维护态执行；不能阻塞前三级 |

T2 的位置描述符属于一等运输负载，但 T2 的计算、解算和本地动作执行属于
`SYNC_TRIGGER` phase。两者都不能改变 VDC 的特等同步发布路径。T2 位置预测
提前生成，准确性优先于即时性；发送失败保留 reject/age/版本证据，不能补发到
已经执行的时间片。

wire seat 和 CPU phase 必须分别核算。增加一个 T2 descriptor 字段不能从
typed VDC body 的未使用字节中推断出可用空间，也不能把普通可选字段重复记作
特等余量。容量、频率、最坏 Core1/IRQ 时间和资源 claim 均必须引用代码符号
并在目标配置上重新测量。

## 4. 标准挂载流程

任何新组件或已有组件的新负载都按以下顺序挂载：

```text
分类 -> 指定 owner/writer -> 选择唯一 phase -> 选择 seat
  -> 静态资源与预算检查 -> staging/CRC/generation
  -> gate -> STOP/ARM 激活 -> RUN 只消费已激活快照
  -> 负测与证据 -> 代码/文档交叉复核
```

### 4.1 分类与 owner

提交挂载申请时必须先写清楚：

- 业务事实、输入快照、输出事实以及字段的唯一 writer；
- 是否需要共同时间、是否产生硬件边沿、是否需要跨板运输；
- 运行时入口、Core0/Core1 归属、PIO/SM/DMA/GPIO/IRQ 资源；
- 失败时是拒绝、延后、背压还是保持上一有效版本。

一个字段只有一个活动 writer。跨核或跨板交接使用 owner API、命令槽、双缓冲
或 seqlock；禁止通过另一个域的内部静态变量旁路 owner。

### 4.2 选择 phase 与 seat

先选 phase，再选 seat，并回答两个独立问题：

1. 该工作是否能在一个固定 phase 内有界完成？不能就拆成 Core0 准备和 Core1
   采用，或移到维护态；不得等待下一域的 phase。
2. 该载荷是否需要 wire deadline、freshness 或可靠确认？按四级 seat 选择，
   不得因为 phase 有空闲就自动提升 seat。

VDC 和 DPLL 始终保持各自 phase。TDMA observer 只能在 TDMA phase 运行一次；
T2 只能在 `SYNC_TRIGGER` phase 运行；`sync_trigger_service()` 只消费 VDC
快照，不能反向调用 VDC/DPLL service。

### 4.3 静态预算与资源准入

挂载前必须检查：

- `APP_REALTIME_PHASE_TABLE` 的 phase 区间、WCET、入口余量和关闭窗口；
- 当前 profile generation 下的 CPU、IRQ、wire、FIFO、DMA 和静态内存余量；
- PIO program、SM、DMA channel、FIFO、GPIO route 和共享 scratch 的 owner；
- 该负载在 TDMA 满载、observer 活动、VDC 到期和 STOP/ARM 边界下的最坏路径。

不能借用后续 phase、GUARD、特等席 optional 空间或另一域的 quarantine 余量。
预算不足时必须返回明确的 skip/reject/backpressure 结果并保留原始计数。

### 4.4 staging、gate 与激活

运行中只读 active 配置。新 payload、模板、IO route、generation 和 CRC 先写
staging，完成 schema、长度、owner、资源、版本、拓扑和身份检查后，再经过域
gate 在 STOP/ARM 边界原子激活。激活失败保持旧 active 并发布原因；不得在 RUN
中热加载 PIO、改变 GPIO route 或重写时间片动作表。

### 4.5 验证与证据

每个挂载项至少保留以下证据：

| 层次 | 必须证明 |
|---|---|
| host/source | phase 入口唯一、owner writer 唯一、seat class 与 codec 一致、没有跨域直接调用 |
| build | 目标 profile 可编译，静态断言和预算工具通过，源码指纹可追溯 |
| runtime | phase run/skip/overrun、generation、CRC、queue/FIFO、reject/backpressure 计数可读 |
| HIL | 满载、VDC 到期时同帧 T2 片段、STOP/ARM、旧代/错 CRC、资源冲突负测 |
| hardware | 需要硬件边沿时，用外部示波器验证各通道相位；VDC、T2、READY 分别出具证据 |

涉及固件、PIO、构建、工具或测试实现的改动，必须按仓库规则运行软件回归、
release build 和 P3 硬件验收；旧凭证或不同源码指纹不能替代本轮证据。

## 5. T2 的标准挂载路径

T2 按下列固定路径接入，不增加 VDC 负载：

```text
VDC 1 kHz 基准
  -> 既有本地 committed DCO / TIMER1 网格（只读）
  -> SYNC_TRIGGER / trigger_t2_runtime
  -> NO1 COUNTER 预测 position_id、grid index、prediction version
  -> distributed_refmem_stage_position_descriptor()
  -> TDMA flight owner 的一等 T2 position class（普通 VDC 状态同帧）
  -> 从板 descriptor CRC/代际/有效区间检查
  -> 本板 grid 解算与静态模板实例
  -> 本板 cursor 到片后执行 SWITCH/GATEWAY/其他动作
  -> READY 只校验上一级动作是否重叠
```

其中：

- VDC 同步信息及模型的发布只由原 owner 负责，T2 本地游标不反向驱动发布；
- DPLL phase 只消费 VDC evidence 并维护模型；
- T2 descriptor 不携带可写 DCO 命令；到期的普通帧继续发布 VDC 状态并推进
  `last_publish_ms`，T2 只替换该帧的二等 RefMem 区域；
- 位置描述符是预测索引（例如 ms index 与 fine-slice index），从板按本地
  VDC model/grid 解算，不接收主站在目标时刻下发的即时 deadline；
- 模板和 PIO persona 在启动或 STOP/ARM 时预装，RUN 时只执行已激活实例；
- READY 是上一动作的校验事实，不是当前动作的许可，也不能填补缺失的硬件 latch。

## 6. 越界判定与回滚

出现以下任一情况，挂载项不得继续进入 RUN：

- 在 VDC/DPLL phase 中调用 TDMA observer、T2 runtime 或动作 PIO；
- 在 T2 phase 中写 VDC/DPLL 状态、修改共同时间或等待 Core0/SCPI；
- 以 transport counter、observer raw sequence 或 READY 电平冒充 `sync_seq`、
  position_id 或实际动作 latch；
- 运行中申请/释放 PIO、SM、DMA、GPIO route，或重排已激活模板；
- overrun 后借用下一 phase、GUARD、其他 seat 或关闭 quarantine 来继续执行；
- 用旧 generation、旧 session、旧 descriptor 或旧示波器凭证完成新会话。

处理顺序是记录原始原因和状态快照，拒绝或延后该负载，保持既有 active 配置，
在 STOP 边界回滚 staging/selection，并为下一次挂载重新生成 source fingerprint。

## 7. 挂载申请模板

后续组件提交以下字段即可进入评审：

| 字段 | 内容 |
|---|---|
| `domain/owner` | 功能域、唯一 writer、Core0/Core1 owner |
| `phase` | 唯一 `APP_REALTIME_PHASE_*` 及入口函数 |
| `seat` | 特等 / 一等 / 二等 / 无座，理由和退让策略 |
| `payload` | message class、字段、版本、CRC、generation、freshness |
| `resources` | PIO/SM/DMA/FIFO/GPIO/IRQ/scratch 及互斥关系 |
| `budget` | 代码符号、profile、最坏 CPU/IRQ/wire 成本和剩余量 |
| `lifecycle` | staging、gate、STOP/ARM、RUN、STOP/rollback |
| `negative tests` | 满载、到期冲突、旧代、CRC、资源冲突和失败恢复 |
| `evidence` | host、build、runtime、HIL、示波器/P3 证据位置 |

## 8. 升格条件

本文从 Draft 升格为 Active 前，必须完成：

1. 独立复核确认四级 seat 与现有 TDMA/VDC/T2 文档没有冲突；
2. `app_realtime_schedule.h`、T2 position class 和 VDC phase 入口通过源码审计；
3. 至少一次四板验证证明 T2 position 发布不会改变 VDC 发布 cadence、`sync_seq`
   推进或外部示波器相位；
4. 将冻结条款登记到 `docs/check/DOCS_REGISTRY.md`，并刷新顶层架构引用。

## 9. T2 专属约束已独立

T2 相关的启动期 PIO 资源准入、越界判定、验证顺序与证据分级，已按域归属独立为
`docs/trigger/t2/TRIGGER_T2_RUNTIME_CONSTRAINTS.md`。本文仅保留跨域挂载规范（第 1–8 节）；两者冲突时，T2 专属约束以该文件为准。
