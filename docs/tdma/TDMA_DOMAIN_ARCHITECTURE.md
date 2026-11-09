# TDMA 基础件主域架构

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Related: `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`
Last updated: 2026-11-09

## 文档接口与范围

TDMA 是 HAOFV 的确定性通信基础件，拥有环路、运输配置、固定过程映像、PIO/DMA 交接及运输证据。VDC/DPLL、RefMem、Trigger 通过受控接口使用运输；时间控制、事实提交和业务执行分别由这些域拥有。

本文面向总体架构评审，重点说明实时预算与飞行处理，按当前代码解释数据流、owner、状态与完成边界。已有实现、有限配置实测和正式产品资格分别评价；任务状态与实测数值不在架构中重复维护。

| 文件 | 职责 |
|---|---|
| 本文 | 模块、主数据流、不变量及已登记契约入口。 |
| [运行接口细则](TDMA_RUNTIME_CONSTRAINTS.md) | wire、异步准备、快速通道、生命周期及证据判据。 |
| [TODO](TDMA_DOMAIN_TODO.md) | 未完成/已完成任务、稳定 ID、依赖和退出门禁。 |
| [Task Progress](TDMA_TASK_PROGRESS.md) | 实测、失败、回退、源码指纹和历史归档索引。 |
| [重构前快照](../legacy/tdma/LEGACY_TDMA_DOMAIN_ARCHITECTURE.md) | 历史方案及原始叙述，仅用于追溯。 |

## 当前实现与完成边界

| 能力 | 当前代码路径 | 仍需独立证明 |
|---|---|---|
| 公共 runtime owner | `tdma_runtime_owner` 聚合 service、ring、adapter 与物理资源；VDC/RefMem 共用。 | 全部共享快照的一致性；登记中的 `HAOFV-879` 偏差未由文档整理关闭。 |
| 固定过程映像与飞行转发 | flight engine/map/FIFO、PIO process follower、局部 overlay 已接线。 | 同圈多节点更新、最坏 DMA 竞争、完整 phase WCET 和产品连续性。 |
| 自主 origin | 独立 PIO/DMA plan、双 bank、返回记录及异步准备已实现；需要显式诊断授权。 | 完整无 service 窗口、故障恢复、资源/时序及 resident 契约资格。 |
| 同步特等席 | typed 同步邮箱、origin 优先发布、RX IRQ 固定提取和 Core1 直接交接已支撑四板本地 DPLL。 | 每圈送达、覆盖期限、极限负载隔离和正式同步质量。 |
| 节点与周期配置 | 编译容量、STOP 后拓扑选择邮箱、ARM 冻结；Core1 整表周期请求/确认已接线。 | 扩容/混合容量和长周期实际运行；配置读回不等于运行验收。 |
| 业务可靠性与集成 | 普通 RefMem、typed 兼容运输及基础恢复设施存在。 | 正式 ACK/fence、T2 多板预约与 completion，不能由单板 Trigger 流程替代。 |

当前验收以所选四板 profile 为范围；外部示波器和内部探针提供互补证据。NO5 属于可选历史夹具，不是四板 TDMA 或 DPLL 主线的必需节点。具体接线、速率、运行时长和精度见进展记录。

## 模块与 HAOFV 层级

```text
Core0 管理面：SCPI / 部署 / 域 owner
  -> STOP 配置与 intent、普通负载 shadow、后台准备
  -> tdma_runtime_owner / tdma_service
Core1 确定性面：静态表与有界 IRQ 窗口
  -> ring runtime / adapter / flight engine
  -> PIO TX、PIO RX、DMA / 固定映像与局部 overlay
  -> 运输快照、普通 RX 副本、同步特等席记录
消费域：VDC/DPLL 本地控制，RefMem 事实提交，Trigger 业务执行
```

| 模块 | 唯一职责与主要代码落点 |
|---|---|
| 管理与调度 | `tdma_runtime_owner.c`、`tdma_service.c`：intent、配置及公共服务；`tdma_ring_runtime.c`：UP/DOWN、应用确认和环路证据。 |
| 资源与准入 | `tdma_profile.c`、`tdma_payload_registry.c`、`tdma_traffic_scheduler.c`：profile、白名单、预算、维护队列及 recovery。 |
| 过程映像 | `components/tdma/inc/tdma_process_image_layout.h`、`tdma_process_image_map.c`、`tdma_flight_engine.c`、`tdma_flight_fifo.c`：固定布局、owner 授权、版本与双向交接。 |
| PIO SPI adapter | `tdma_pio_spi_ring_adapter.c` 及其 `.inc`：运输编解码、身份、序列、接收健康和物理回调；不解释业务。 |
| 物理与 persona | `tdma_pio_spi_phys.c`、`tdma_pio_spi_phys_programs.c`、`tdma_pio_spi_persona_fsm.c`：SM/FIFO/DMA 生命周期和训练/普通/飞行 persona。 |
| 自主 origin | `tdma_origin_plan.c`、`tdma_origin_build_job.c`、`tdma_origin_exchange.c`、`tdma_runtime_origin.c`：硬件图、异步构造、发布选择、授权及退休。 |
| 普通后台准备 | `tdma_rx_scan.c`、`tdma_rx_prepare.c`、`tdma_overlay_prepare.c`：Core0 固定工位；Core1 复验并采用。 |
| 同步快速入口 | `tdma_priority_rx.c`、`tdma_pio_spi_phys_priority.inc`、`tdma_priority_tx.h`：固定记录、限额 IRQ、provider/sink；时间语义归 VDC。 |
| 事件与时序证据 | `tdma_event_observer.c`、`tdma_event_history.c`、`tdma_origin_reference.c`、`tdma_service_timing.c`：原始事件、回绕/身份关联和分项耗时。 |

未带目录的 TDMA 文件位于 `components/tdma/inc/` 或 `src/`。`TdmaSchedulerAO/TdmaRuntimeFB` 是 HAOFV 职责名称，不意味着存在同名独立任务。Core1 不通过 RTOS 任务调度来完成逐圈装卸。PIO SPI 为当前实用承载，BISS-C/UART/RS485 仍为 adapter 扩展方向。

## Owner 与资源不变量

| 对象 | writer / 执行 owner | 消费与限制 |
|---|---|---|
| profile、拓扑、周期请求 | Core0 管理入口 | Core1 认领并发布应用确认；RUN 内不热改 active 布局。 |
| 普通业务 shadow、准备结果 | Core0 对应域与固定准备工位 | Core1 有界复验，不等待准备完成，不读半写版本。 |
| active TX、DMA selection、运行状态 | Core1 TDMA owner；PIO/DMA 执行已授权计划 | Core0 不覆盖仍被硬件引用的缓冲。 |
| typed 同步内容 | Core1 VDC 编码；TDMA provider/发布边界运输 | 不经 RefMem 分片、普通 RX 解析或 Core0 队列；DCO 写入仍归 VDC。 |
| RX 同步记录 | Core1 非重入 IRQ / 停源后的生命周期 owner | 固定复制、校验及有界 sink，之后在允许的控制边界匹配采用。 |
| snapshot / 计数 | 各对象唯一 writer | sequence、原子操作或 DMB 保证交接；失败读取不得复用旧成功。 |
| 原始记录保存 | Core0 Storage owner | Core1 只追加有界内存；SCPI 触发及 STOP 后读取，SD/Flash 不进入实时路径。 |

TX、RX 和 SYNC_IO 的 PIO 分区由 `board_config.h`、`tdma_state_machine_resources.h` 与 `ARCH-PIOPARTITION-01` 决定。指令存储由同一 PIO 内的 SM 共享；空闲 SM 不代表可再装一份程序。personas、DMA、GPIO 和共享 scratch 的互斥在资源准入与 STOP 退休边界处理。

Flash 写入仍为 Core0 FlashTransaction 维护操作，并遵守 Core1 park/lockout；本域整理不改变 OTA。Core1 禁止 FatFs、SCPI、USB、长日志、动态分配及等待 Core0 的阻塞操作。

## 运行数据流

### 普通过程映像与异步候车平台

Core0 提前编码 ordinary mailbox、准备 RX decode 与 overlay 描述符；Core1 在固定边界认领完整版本并复验 epoch、map、长度及物理可用性；PIO/DMA 执行转发和已授权替换。未就绪时复用有效版本或保留拒绝，不能等待准备任务。普通 RX 副本由 Core0 后续消费，副本丢弃不等于线上转发停止。

准备完成、owner 接受、DMA 选择、物理送出、对端接收和业务应用是不同事实。各阶段保持自己的版本及确认；软件发布计数不能冒充物理发送或 RefMem commit。

### DPLL/VDC/SYNC 特等席快速通道

```text
NO1 已提交时间模型 + 对应事件
  -> Core1 VDC 编码 typed 同步 mailbox
  -> TDMA origin provider / 双缓冲发布
  -> 固定 SHORT 飞行运输
  -> 从板 RX IRQ 固定提取 header + 参考 mailbox
  -> 身份/CRC/覆盖复验、固定记录发布和有界 sink
  -> Core1 VDC 同事件匹配 + 本地 delay + 本地 DCO 控制
  -> SYNC_IO PIO/DMA 执行未来输出
```

typed 同步采用 `TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS`，内容由 `VDC-PRIORITY-01` 定义。它与普通 mailbox body 是不同 typed 解释，不能把整段 body 同时解释为普通 VDC、RefMem、ACK 和控制字段。它保留 transport 帧型及已冻结长度，不是独立同步帧。

全局 lag-1 observation trailer 仍有固定位置及兼容语义，但不是当前本地跟踪的唯一输入。运输圈序号、mailbox 更新序号、所载事件序号及运行 generation 分别校验；“本圈送达”不等于“该圈刚产生的事件”。SYNC 的硬件执行归 SYNC_IO，不新增业务域对 TDMA PIO 的控制权。

### 四级负载

| 等级 | 内容 | 服务规则 |
|---|---|---|
| 特等 | DPLL/VDC 关键同步记录及 SYNC 确定性时间需求 | 固定编码、固定资源与有界入口；缺口、旧代和覆盖明确记录。逐圈无损仍须专项验收。 |
| 一等 | T2 位置记录（0x15）等实时单片负载；其余 VDC 状态与跟随数据 | 与特等共用 origin 快速路径，按发布机会轮换优先级，见 TDMA-SEAT-01；座位内保留 VDC 核心字段与 ACK；命令不得靠状态覆盖语义重放。 |
| 二等 | RefMem 数据及 ACK/fence | dirty 编码、有界配额与背压；运输成功不自动提交事实。 |
| 无座 | 普通控制、日志与维护数据 | 预定低优先级或 maintenance 预算；不得扩帧、借 guard 或阻塞前述路径。 |

上述是服务目标及 owner 分层，不表示所有等级已完成统一调度、共存与饱和验收。

### REFMEM-TRANSPORT-01：REFMEM_CMD 载体（pending）

`REFMEM_CMD` 是 RefMem 统一控制面的跨节点命令帧，走 **TDMA 二等席**（RefMem 数据/ACK/fence 的普通路径），不新增 payload class，也不新增物理总线：

- `payload_subtype = REFMEM_CMD`；envelope 与 command slot 一致（`command_seq/type/class/origin/target/required/payload/crc`）。
- 二等席承载：命令帧与 RefMem 镜像/回执共用普通路径，不在特等/一等快速通道里抢机会；接收端按 subtype 分派，`REFMEM_CMD` 是唯一 consumer。
- 四板分发：`target_mask`/`required_mask` 使用 A0-A3 显式位；`ring_node_count >= 4`；不得隐式广播。
- 席位：`SYSTem:TDMA:SEAT:ROTation <p>,<f>,<s>` 的 `s` 为二等席让位段；四板命令分发要求 `s>0`，按实测规则 `f>=3`（避免 `7,2,1`），周期不得拉长到影响一等席位置路径。
- 预算以 `tdma_traffic_class_profile_t` 的 `reserved_bytes_per_cycle`/`max_frames_per_cycle`/`queue_depth`/`deadline_ns` 为准；命令载荷走 `payload_kind`/`payload_ref`/`payload_size`/`payload_crc32`，大载荷用 staging/TLV ref。
- 优先级：TDMA 二等席；不占 core1 硬实时窗口，不在 ISR 执行；二等席是普通路径预算，不承诺硬实时。
- ACK/fence 复用 `TDMA_PAYLOAD_CLASS_REFMEM_ACK_FENCE`；命令帧与 ACK 不共享队列语义。
- 只有二等席预算不足时，才评估专用席位/payload class；必须另立契约和 TDMA 联审，不能静默扩类。
- 契约状态：pending；代码锚点 `tdma_profile.h`；需 host 编码/解码 + 四板命令/ACK 证据。

### TDMA-SEAT-01（用户已审核，已实现）：快速路径按发布机会轮换的流式调度

> 状态：2026-10-10 语义经用户审核（`2109963d`），实现 `8bd547b2`，DPLL60 通过；实测见 `TDMA-PROGRESS-20261010-001`。本节不登记为契约。运行流程图见 [tdma_runtime_flow.svg](tdma_runtime_flow.svg)。

**目标。** 流式调度在一个环回周期内充分利用 origin 的每一次发射机会；特等先上先发，特等没有新内容时把机会让给一等，任何一次机会都不空过。

**列车与座位。** 线上每次发车都是同一种帧：固定 SHORT 帧、固定长度、固定 1 ms 节拍（`TDMA-PROCESSIMAGE-01`、`TDMA-EMISSIONCLOCK-01`），本节不改变帧型、长度和节拍。每块板在每帧只有一个座位，即本节点 mailbox 段（`TDMA_FLIGHT_SHORT_SLOT_SIZE`）。特等和一等都是**实时单片**：一条记录在一个座位里装完，不分片、不重组。

**发布机会。** origin 快速路径（`tdma_origin_exchange`，双 shadow bank）同一时刻只允许一份 pending；DMA 图在 1 ms 发车边界的 LOCAL 步骤选中它后才退休。Core1 TDMA 相位每整表周期尝试一次发布。一次**发布机会**定义为：Core1 TDMA 相位调用发布时，快速路径没有 pending（`tdma_origin_exchange_ready()` 为真）。机会由 origin 计数，从运行开始单调编号。

三个周期分开解释（四板台架，1 ms profile，四节点，实测快照，非事实源）：

| 量 | 实测 | 来源 |
|---|---|---|
| 线上发车间隔 | 0.9998 ms | `READ:CALibration:ORIGin:RUNTIme?` 的 `boundary_sequence` |
| 快速路径发布间隔 | 约 2.13 ms（每 4 ms 约 1.9 次） | 同一查询的 `published_generation`（`t2-bench-probe-164300`） |
| 普通 FIFO 路径 | 约 4 ms | Core0 RefMem 按 `cycle_period_ns × node_count` 节流，每个环回最多交一份（`TDMA-PROGRESS-20261008-006`） |
| Core1 整表周期 | 1.5 ms | `PROJECT_CORE1_PROFILE_1500US_CYCLES` |

**按机会编号轮换优先级。** 第 k 次发布机会按轮换比例 M:N 指定本次的**主等级**。比例是 STOP-only 的每板设置（`SYSTem:TDMA:SEAT:ROTation <M>,<N>`，默认 `TDMA_SEAT_ROTATION_DEFAULT_*_SHARE` = 1:1，即特等、一等交替），每个 share 取 1..`TDMA_SEAT_ROTATION_SHARE_MAX` 且 `M+N <= TDMA_SEAT_ROTATION_CYCLE_MAX`；未设置或读到 0 时回落到编译默认，绝不产生零周期。每次机会：

```text
主等级有新内容          -> 主等级上车
否则 另一等级有新内容   -> 另一等级上车（机会不空过）
否则                    -> 不发布，座位保持上一份已发布内容
```

两个等级都有内容时，每类至少每 M+N 次机会各上车 M、N 次；一等空闲时，特等使用全部机会。默认 1:1 下，特等与一等在一个约 4 ms 的环回内各得到约一次机会；把比例调向特等（例如 2:1、3:1）会按比例缩短特等的等待上限、拉长一等的等待上限，因此在改比例前须先按席位预算核定一等的期限与背压（见 [TODO](TDMA_DOMAIN_TODO.md) 的 `TDMA-SEAT-02`）。

**新内容的定义。**
- 特等：provider 返回 `TDMA_PRIORITY_TX_READY`，且事件序号不同于**特等自己上一次上车的记录**。比较对象是特等自己的上一次发布，而不是座位上一次发出的内容；否则一等上车后，特等会把同一旧事件当作新事件重发。`EMPTY` 表示特等本次没有新内容，**不再保留座位**。
- 一等：一等邮箱中有尚未上车的完整记录。

**一等邮箱。** Core0 RefMem 预先组好完整 32 B mailbox：message class 0x15、VDC 核心字段与 ACK 保留、RefMem 区装 10 B T2 紧缩记录（`TRIGGER_POSITION_SLICE_WIRE_SIZE`）、CRC 已算好。邮箱单深度、单写者，Core0 写完后原子发布；Core1 在上车前复验 class、CRC、source slot 与 target mask，然后经快速路径发布。被一等上车取走后邮箱才可写下一条；一等记录在邮箱中等待时不丢弃，新记录不得覆盖等待中的旧记录。

**让座的代价。** 一等占用某次机会时，这次机会没有特等事件；provider 只提供最新事件，被让出的旧事件不补发。从板减少的是 MATCH 样本，不直接降低 DCO 更新节拍：相位修正最短间隔 `VDC_PRIORITY_PHASE_MIN_INTERVAL_MS`，频率跟随按 `VDC_PRIORITY_FOLLOW_*_INTERVAL_NS` 分级评估，跟随新鲜度按事件原始时间计算（`VDC_PRIORITY_FOLLOW_MAX_AGE_MS`）。T2 每个位置只有少量 PREVIEW 与 1 条 FREEZE，一等占用稀疏；样本减少对锁定质量的影响由 DPLL60 量化。

**路径分离。**
- origin 启用特等 provider 时，特等与一等都只走快速路径；普通 flight FIFO 不参与 origin 的座位发布。改写已排队 VDC image 的补丁路径（`tdma_flight_fifo_core0_patch_tx_ready`）作废删除。
- 多包与长帧（流式 OTA、配置、可靠控制等）保留 `tdma_flight_fifo` 双槽 publish/acquire/release 与分片重组，使用独立的发车机会与预算，不占用本节的快速路径；其出线方式另行设计。
- 从板接收（2026-10-10 修订，用户确认直接实施，`3ddeb258`）：特等与一等都由优先级通道接收。特等 RX IRQ 每帧捕获 origin 槽并按传输序号登记，同一记录再交给两个接收端：VDC sink 只取 0x14；一等接收端（`tdma_runtime_owner_set_priority_first_class_rx_sink`）只取 0x15，交给 T2 位置邮箱/FREEZE 锁存。普通 flight 路径每次捕获只取最新一帧、约 4 ms 一次（`SYSTem:TDMA:PROFile:RX?` 实测，快照，非事实源），而一等记录只在线上停留约一个发布机会，因此不再作为一等接收路径；一等接收端首次接受记录后，Core0 普通路径停止解析 0x15。
- 一等记录在线上可能连续出现在多帧（下一次机会到来前座位保持上一份内容）：接收端把线上字节相同的重复帧视为同一记录；邮箱忙而未入箱的记录不记忆，下一份重复帧重试。一等记录各自占用独立的 mailbox seq16（`next_seq32` 随每次一等上车推进），避免普通路径按段序号去重时把新记录误判为重复。

**与既有模型的关系。**
- `tdma_loop_plan` 的环内时间窗与 `TDMA_LOOP_SCHEDULE_ENABLED` 不再作为座位仲裁；开关保持 0，时间窗按 1 ms 帧号映射与 2.13 ms 的实际机会不对齐。
- 席位挂载表（`tdma_seat_mount_store`）声明 message class 属于哪个等级；ARM 时解析为本次运行的固定映射，运行期发布路径不读挂载表。
- `tdma_seat_admission` 的时间窗授权（`FIRST_CLASS_WINDOW_GRANTED`）不再是一等上车前置；其中事实分级、STOP/换代撤销、晚交接保留的规则继续适用。
- 事实分级不变：上车（本次机会归属）、物理发布、对端接收、业务应用分别记录；上车不等于送达。

**EXE-SEAT-01 预算清单。**

| 项 | 特等 | 一等 |
|---|---|---|
| 字节 | 完整 typed body（`TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_SIZE`），不变 | 32 B 座位，其中 RefMem 区 10 B 为 T2 记录；wire 帧长不变 |
| 频率 | 一等空闲时全部机会；一等有内容时不少于 M/(M+N) | 不超过 N/(M+N)，仅在有记录时占用 |
| Core1 新增 | 每次机会一次事件序号比较 | 每次机会一次邮箱状态读取；上车时复验 CRC 并复制 32 B（最坏耗时按同源码实测计入 TDMA 相位） |
| IRQ | 不新增 | 不新增 |
| 对 SYNC 的影响 | 一等占用期间特等事件间隔最多从 1 次机会变为 (M+N)/M 次 | — |

**验收。**
1. 主机测试：特等有新事件且轮到特等时一等不上车；轮到一等且一等有记录时一等上车；轮到一等但一等为空时特等上车；一等上车后特等不重发旧事件；EMPTY 不占座；STOP 退休一等邮箱。
2. DPLL60 不退化；新增计数：每等级上车次数、特等让座次数、一等记录等待机会数。
3. 四板 T2：NO1 位置记录上车数、从板优先级通道 accept/duplicate/reject 计数、一等等待不超过 M+N 次机会。注意运行期一等记录由优先级通道接收，旧 `rx_complete_count` 在该路径上不再增长，不作为送达判据。

**实现状态。** 一等邮箱 `tdma_first_class.h`；仲裁在 `tdma_pio_spi_ring_origin.c` 的 `publish_priority`，轮换比例经 `tdma_pio_spi_ring_adapter_set_seat_rotation()` 在 STOP 下按板配置（默认 1:1，SCPI 入口 `SYSTem:TDMA:SEAT:ROTation`，计数页 `SYSTem:TDMA:SEAT:COUNters?`）；改写已排队 VDC image 的补丁路径已删除。从板侧一等记录由优先级通道第二接收端（`TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS`）承担，普通 flight 路径不再作为一等接收路径（`TDMA-PROGRESS-20261010-002`）。未完成：真实物理 selection 证据（`TDMA-FLIGHT-002K2`）、一等席服务等级与特等席对齐（`TDMA-SEAT-03`）、比例与一等扩容的 `EXE-SEAT-01` 审核和 C11 登记。

席位余量须分开核对 wire 与 CPU：当前 typed 同步 body 由 `TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_SIZE` 完整占用，不能再塞入健康状态或把普通 class 的 optional 空间重复记作 typed 可用余量。SYNC 的后续需求须列出独立资源及最坏耗时，未完成准入/实测的空间不算已承诺余量。新增或扩容特等席须先经用户审核（`EXE-SEAT-01`）。VDC 健康镜像、统计及查询是 Core0 异步诊断，不占特等邮箱，也不新增 Core1 发布工作。

### 三份额轮换与让位周期（2026-10-16 目标裁定，status `pending` 待 C11 交叉审核）

快速通道只有一条 TX 快路径，按发布机会在**特等席**（VDC typed SYNC）与**一等席**（T2 position descriptor，class `TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS`）之间轮换。**二等席的定义是"不走快速通道的部分"**——普通帧与 refmem 镜像/回执（含位置冻结回执）——它不参与轮换，因此**只能在快速通道用剩的时间里通行**。

**轮换周期语义**：`cycle = priority_share + first_class_share + second_class_share`（`tdma_seat_rotation_cycle()`，`tdma_pio_spi_ring_adapter.h`）。一个周期内前 `priority_share` 个机会归特等席、随后 `first_class_share` 个归一等席，**最后 `second_class_share` 个机会两席都不得上车（让位段）**，把该时段留给普通/refmem 流量。让位段内两席的租约**保留**（内容不丢），且**不计入** `seat_first_class_wait`（它不是"被另一席抢走机会"，而是周期为普通路径预留），因此也不会产生等待超限。`second_class_share == 0` 时 `cycle = p + f`，与三份额形式之前的行为**逐位等价**。

**让位证据（不新增计数）**：`seat_opportunity_count − seat_priority_count − seat_first_class_count = 让位机会数`；期望比例 `fc ≈ f/(p+f+s)`、`yield ≈ s/(p+f+s)`。

**配置**：`SYSTem:TDMA:SEAT:ROTation <p>,<f>[,<s>]`（STOP-only 每板；第三字段可选，缺省 0；越界或无法构成周期即拒绝）。**两种工况预设**（目标口径）：VDC **未启动**时特等席等于没在跑 ⇒ **`1,2,7`**；VDC **启动** ⇒ **`7,2,1`**。读回 `SYSTem:TDMA:SEAT:ROTation?` 为 12 字段：索引 4 = 整周期 `p+f+s`，索引 11 = 二等席份额，索引 0–10 含义不变（旧镜像 11 字段仍可正确读前 11 项）。

**在线入口（统一控制面增量）**：除上述 STOP-only SCPI 入口外，`REFMEM-CTRL-01` 的统一控制面提供 `tdma_runtime_owner_commit_seat_rotation3`：`p/f/s` 打包为单个 32-bit 原子字（4 bit 各段），`tdma_pio_spi_ring_origin` 每次发布机会只读一次并解包，切换在下一机会生效，**不需要停环**；SCPI `SEAT:ROTation` 仍保持 STOP-only。在线入口的验收不变量是切换前后 `up_sequence`/`down_rx_sequence` 持续单调、无帧间隔（见 N6.3）。

**与 `TDMA-SEAT-CLASS-001` 的关系**：本条款给二等席**固定时间**；另一条路线是把冻结回执/状态上行**提升到一等席**。目标已选择先做本条。**门禁**：两工况台架下"冻结往返"回落到环数周期量级、`freeze_resend`/位置降到个位数、特等席 cadence 无回归；`EXE-SEAT-01` 预算清单登记本条款的三份额与周期语义。

## 配置与生命周期

| 边界 | 必须保持的语义 |
|---|---|
| STOP 请求 | 先关闭新准入，发布停止 intent；请求接受不是硬件停止或缓冲退休证明。 |
| 停止确认 | Core1 完成已有选择/硬件退休并确认配置；Core0 后台取消、队列及恢复池退休单独完成。 |
| STAGE / APPLY | 校验资源、拓扑、CRC、容量及周期；active 只能在允许的停态更新。 |
| ARM | 固定 map、节点数、trailer offset、DMA 长度及 persona；准备与应用交接未完成时拒绝。 |
| TRAIN / START | 使用已确认 Calibration 输入及运行身份；首帧可无效，后续有效样本可推进 DPLL。 |
| RUN | 保持 active 身份、owner 和 wire 长度；迟到/坏样本显式拒绝，不伪造进度。 |
| STOP / 换代 | 撤销授权并退休旧请求/selection/记录，旧结果不能进入新会话。 |

自主 origin 额外经过 preparation、READY、release 与运行授权复验，入口在 `tdma_runtime_origin.c`。诊断授权的持续运行不等于产品 RUN 或契约已激活。

## 拍级确定性周期

### TDMA-DET-01：唯一时间单位

Core1 静态表以 `clk_sys` 拍数为事实源，见 `APP_REALTIME_PHASE_TABLE`、`PROJECT_CORE1_*` 和 `app_realtime_profile.c`。DPLL/VDC/SYNC 的直接时间坐标使用 TIMER1；TIMER0 保留 SDK、alarm 和系统超时语义。显示为 ns/us 不改变底层单位，节拍不等于物理精度保证。

### TDMA-DET-02：wire phase 与 CPU phase 分离

Core1 整表周期、operating-profile wire 周期、实际硬件发车/回环间隔和 DPLL 有效更新间隔分别建模。周期请求由 Core1 在完整表边界安装并确认；长档扩展 TDMA 预算并顺延后续相位，不能按旧预算重判历史样本。

IRQ 固定配额、关闭预留、前台 service 和输出规划共同计入实际相位墙钟。不能只报告局部 helper、扣除探针或用空队列成本替代完整 WCET。代码目录支持哪些周期，以 `app_realtime_profile_supported()` 为准；配置可读不证明对应长帧或长档运行通过。

预算阅读顺序是：**整表周期 → TDMA 相位范围 → IRQ/关窗预留 → 前台可执行预算 → 实测完整耗时与迟到**。硬件线上的飞行时间与 CPU 工作可重叠，不能简单相加，也不能用“整表周期减 wire 时间”作为装卸预算。当前表的逐项配置快照和超时判据见[实时预算细则](TDMA_RUNTIME_CONSTRAINTS.md#实时预算读法)。

### TDMA-DET-03：基础载荷优先的静态装配

布局和配额在构建/配置准入阶段确定，RUN 不根据临时余量扩展。普通 mandatory body、typed body 和全局 trailer 各按自己的 codec 校验；维护流不能夺取实时短帧、其他 owner 段或 guard。资源、wire 最长时间和 phase 邻接检查由 `app_runtime.c` 及 layout 静态断言执行。

### TDMA-DET-04：分层周期预算与席位准入

> TDMA-SEAT-01（已审核并实现）改为按发布机会轮换优先级。本节以环内时间窗划分席位、以 `FIRST_CLASS_WINDOW_GRANTED` 作为一等上车前置的表述由 TDMA-SEAT-01 取代；预算量纲、事实分级和撤销规则继续有效。

TDMA 的下一级抽象是 seat，而不是某个具体功能。具体功能以 load instance 挂载到 seat 上：

```text
TDMA Foundation
  -> seat 服务等级与周期预算
      -> load instance 准入、排队、分段和背压
          -> T2 / OTA / CONFIG / DIAGNOSTIC 等具体功能
```

TDMA Foundation 对每个运行周期同时预算 TX、RX、Core1/IRQ、Core0 消费、反向 ACK/诊断和 guard。发送速度快但 RX FIFO 或 Core0 消费跟不上时，业务仍然失败；因此每周期发送字节预留不能单独代表可交付能力。

周期日历按以下顺序解释，具体边界由当前 profile、`APP_REALTIME_PHASE_TABLE` 和硬件读回决定：

```text
特等席 VDC/DPLL/SYNC 提交
  -> 特等席硬件 selection/DMA 交接与 close guard
  -> 一等席准入判断
  -> 一等席 load instance（当前挂载 T2 position_q）
  -> OTA/配置/诊断分段流
  -> RX FIFO 消费、CRC、重组和业务交接
  -> ACK/诊断反向发送
  -> 周期 guard
```

`PROJECT_CORE1_PROFILE_1500US_CYCLES` 是当前 Core1 的候选整表周期；`TDMA_PROFILE_DEFAULT_CYCLE_PERIOD_NS` 是 TDMA Foundation 默认 wire profile 周期。两者不是同一个事实源，必须在 STOP/ARM 后通过 profile generation 和周期读回统一，不能将二者直接相减作为业务余量。

每个 traffic class 必须同时提供 TX 和 RX 预算：

```text
tx_admission <= tx_service_capacity
rx_arrival <= rx_fifo_capacity + rx_consume_capacity
ack_and_diagnostic_tx <= reverse_tx_capacity
core0_consume_peak <= fixed_core0_phase_budget
```

seat 上挂载的 load instance 至少包括 traffic class、seat、owner、deadline、TX/RX 每周期字节和帧上限、segment size、TX/RX queue depth、overflow policy、CRC/reassembly 成本以及停止时的退休规则。T2 的 `position_q` 是当前挂载在一等席上的单帧负载；OTA、CONFIG 和诊断是挂载在较低席位的有界分段负载。不同 load instance 可以共用 FIFO 基础设施，但不能共用完成语义。

上述分层可概括为一条准入无关性：挂在任一 seat 上的 load instance 能否准入，只取决于它自己的同代身份（epoch、cycle、config/profile/binding generation、priority sequence）、本席预算、时间窗与共享资源当前状态；物理环回、其他席位的发送完成或对端接收都不构成它的准入前置。各实例共用 FIFO/DMA/PIO 基础设施但不共用完成语义，任一实例的完成、迟到或失败都不推进、不回滚其他实例的状态。该段整理本文既有约束，不新增承诺，也未登记为契约；若要作为冻结条款，须先完成域间核验并按 C11 登记。

priority provider 返回 `READY` 或 `EMPTY` 只代表特等席 offer/保留，不能代表物理发送已完成。只有记录 `PRIORITY_SELECTED`、`PRIORITY_TX_COMMITTED` 后，TDMA 才能计算 `FIRST_CLASS_WINDOW_GRANTED`；VDC 新事件、绑定变化、故障或窗口不足时立即撤销一等席准入。任何准入都不能修改已被 Core1 或 DMA 引用的 VDC image。

预算完成事实必须分开记录：`queued`、`admitted`、`selected`、`tx_committed`、`rx_queued`、`rx_consumed`、`reassembled`、`applied`。软件发布计数、FIFO 入队和 priority offer 都不能冒充物理发送、对端接收或业务应用。

`tdma_seat_admission` 是 Foundation 内独立的 owner 串行软件模型：按同一 epoch、cycle、config/profile/binding generation 和 priority sequence 关联选择、提交、预算及窗口。预算量纲由 `tdma_seat_resource_t` 分开定义；全部显式定义后才可计算软件授权。load 选择消费本轮授权，后端确认提交另记；STOP、换代、新特等事件、故障或时间不足撤销授权。旧回调不能推进新轮状态，晚交接事实与时间窗失败同时保留。跨核读取仍须使用 owner 的 guarded handoff，不能直接共享此模型结构。

该模型当前未接入 provider、generic scheduler 或物理 origin，因此软件测试不代表一等席已经获得硬件发送窗口。真实接线由 `TDMA-FLIGHT-002K` 继续跟踪：补齐物理 selection/提交边界及同代实测预算后才能授予发送；不能用现有 `phys_origin.publish/commit_tx` 的备用映像交接冒充物理进展。

## 跨域契约入口

本次整理保留契约 ID、登记状态及实现要求，不以重写文档完成 C11 或新增产品承诺。状态唯一来源为 [登记表](../check/DOCS_REGISTRY.md)。

### TDMA-RESIDENT-01：常驻循环过程映像不变量

逻辑 process image 在启动时初始化，运行中持续循环；每个 Node 只对授权 segment 执行 UNLOAD/LOAD，无新 generation 时透传。物理 frame completion 是下一 cycle 的边界，不是 resident loop 的终止。transport cycle 与 segment generation 分开；`hop_limit` 约束单个 frame 的拓扑传播，不作为正常 resident 停止条件。

退出由 STOP、复位、故障或显式重配置控制。origin 需延续返回的有效映像，不能按 Node 各发独立一帧来冒充同圈更新。该条款仍为 pending，普通周期模式与显式自主模式均按自身证据评价；回环驱动和固定节拍候选不等同，当前实现与已登记发车要求的符合性及 F1–F5 闭合需独立复核，见[细则](TDMA_RUNTIME_CONSTRAINTS.md)。

### 发车、更新、声明与采集契约

| 契约 | 保留要求与当前边界 |
|---|---|
| `TDMA-EMISSIONCLOCK-01` | 发车节拍由环边界硬件事件驱动，Core1 不得作为发车门控；可达下界以代码符号声明，低于下界的 profile 必须 fail closed。当前 PIO control/guard 与 DMA 图的实现描述不替代该要求；固定 guard 候选的符合性仍待验证，改变要求须另做 C11。 |
| `TDMA-UPDATEINJECT-01` | overlay 非阻塞注入；未就绪透传上一版且不改变节拍。已有异步准备不等于同圈多节点更新及节拍不变性已验收。 |
| `TDMA-FLIGHTCLAIM-01` | byte-level 与 cycle-level 分开声明；cycle-level 必须绑定 RX/TX 重叠与固定 pipeline delay 实测证据，并满足其独立连续性条件。 |
| `TDMA-CAPTURE-01` | DPLL residual 经固定 SRAM capture，停止后由 Core0/StorageAO 写 SD 并离线解码；详细采集处理不得进入 TDMA 实时路径。其他观测接口不自动替代该专项证据。 |

上述契约均维持登记中的 pending 状态。

### core0/core1 双 FIFO 与所有权

`TDMA-SEQLOCK-01` 要求多字段 runtime snapshot 使用 seqlock 或等价版本化交接。TX/RX descriptor FIFO 与 PIO 硬件 FIFO 不混同；固定池按 producer/consumer 所有权、版本和 release/acquire 发布。Core1 不阻塞等 Core0；普通 RX 副本满队列可丢弃计数，不能套用于特等席而静默丢样本。`HAOFV-879` 的既有审查偏差继续挂账。

### Transport Envelope 与长短帧

`TDMA-HOP-01`：hop limit 来自 ring profile。`REFMEM-260B-01`：critical RefMem 内帧须受 `TDMA_TRANSPORT_SHORT_PAYLOAD_MAX` 约束，净载荷还需扣除 RefMem 头；该上限不是每个 Node mailbox 的可用空间。

transport header、SHORT/LONG 上限以 `tdma_transport_frame.h` 为准。当前 `FLIGHT_MUTABLE` 的 packet CRC 只保护 header，owner mailbox CRC 保护相应段；不能写成完整可变 payload 的 CRC。尾部 CRC/WKC 扩展是后续方案，不是已经部署的 wire 格式。

### 固定 Node image、DPLL trailer 与 RX 位图快路径

`TDMA-FLIGHTBITMAP-01`：`PROJECT_NODE_CAPACITY` 限制本地静态容量；STOP 后由已准入 topology 选择 N 个邮箱，ARM 冻结 map/trailer/DMA 长度，RUN 不变。降低运行节点数缩短有效 wire，不自动释放已编译 RAM。

`TDMA-PROCESSIMAGE-01`：Node mailbox 和全局 observation trailer 组成固定 SHORT image；同步开关不能改 transport 帧型、长度、序列或 PIO 节拍。实际 payload 由 `tdma_flight_payload_size()` 得出，trailer 位于有效 payload 尾部；最大容量宏不是当前运行长度。RX 位图只表示 presence/new/expected 等运输事实，不能代替业务 ACK。

Node mailbox 的 ACK 席位（偏移 24..26）由 16-bit seq 与 1 B flags 组成，flags 的 bit0/1/2 为 VALID/NACK/FENCE，bit3 为 `ACK_FLAG_COMMAND`：置位表示该席位携带 **REFMEM_CMD 的单目标终态回复**（`seq16 = command_seq & 0xFFFF`），此时可选诊断字节（偏移 29）解释为 reason；未置位时仍是 delta/fence ACK。该复用不新增 payload class、不改变帧长或节拍，回复的传输优先级低于一等席（位置/VDC/feedback），由 RefMem 在普通镜像中发布。

### TDMA-RECOVERY-01：有界恢复

Core0 准备、Core1 固定窗口选择、PIO/DMA 发送；`TDMA_RECOVERY_BUFFER_COUNT` 缓冲及 `TDMA_RECOVERY_MAX_FRAMES_PER_CYCLE`、独立预算限制重传，复用原 Node segment。ACK、retry、backpressure 和退休不得覆盖 IN_FLIGHT。设施存在不代表可靠性 HIL 已闭合。

### SPI 速率与 TDMA 周期 operating profile

`TDMA-OPMODE-01` 采用离散 baud/cycle 组合，事实源为 `s_tdma_operating_profiles`。STAGE 不更改线上状态，STOP 后 APPLY、下次 ARM 安装；effective schedule CRC 绑定 profile。不能运行中单板私自降频，candidate 档不自动成为已验收回退档。Core1 整表周期是另一项配置，两者均需通过一致性及资源门禁。

### 低速档与速率切换路径（10 kHz 起）

> 状态：可行性分析（2026-11-04）。本节不新增冻结契约，也不把任何档位写成已验收。除标注“快照”外，数值均由代码符号推导，非实测、非事实源；采纳为产品档前须按 `TDMA-OPMODE-01` 完成 C11 登记、预算清单与四板证据。

**目标。** 在高速环可用之前先跑一个**不依赖相位标定**的 4 节点引导环：验证线序与物理连通、运载心跳/健康、承载少量维护与 CAL/训练命令，再逐级升到 100 kHz / 1 MHz / 5 MHz / 10 MHz。低速档仍是 `TDMA-OPMODE-01` 的离散 operating profile，不是第二条物理链路，也不改 wire 帧型、帧长或 Core1 节拍语义。

**范围与裁定（2026-11-04 用户）。** 只保留 **4 节点**：镜像按 `PROJECT_NODE_CAPACITY=4` 构建，catalog 只生成 N=4 项（level 23–25），代码里不写死 slot；`baud`、`slot`、`guard_quanta`、`turnaround`、`loop_delay` 与超时都是可配置参数。节点数大于 4 由 `slot_min(N)` 准入直接拒绝，不保留 8 节点 catalog 或 8 节点台架。

**非目标。** 低速档不承担 DPLL 同步（圈样本率 = 1/(slot × 节点数)）、产品 RUN、OTA/大载荷吞吐，也不替代 P3 标定。**定位（2026-11-04 用户裁定）：10K 低速只承载 REFMEM；1M 及以上才用于启动 DPLL、发布 VDC；若要在低速率启动 DPLL，必须先放宽门限。** `tdma_pio_spi_phys_p3_start()` 只接受 10/25/30 MHz，低速档没有自己的 bit-level delay 测量，不能凭自身测量生成标定 stage（合成 stage 等同伪造标定）；因此“低速档承载 CAL 命令”只能兑现为**命令通道**：在进入 CAL 之前/之后下发训练、查询与健康命令，CAL 测量期间环仍处于 STOP、不承载命令。

**四个时基不能混用。**

| 量 | 事实源 | 10 kHz、4 节点推导值 | 换档影响 |
|---|---|---|---|
| 线上帧时 | `wire_bytes × 8 / baud`，`tdma_pio_spi_phys_wire_time_ns()` | 174 B → 139.2 ms | 与 baud 成反比 |
| 每节点 slot | operating profile 的 `cycle_period_ns` | 必须大于帧时（见下） | 随档位 |
| 环回周期 | `feedback_timeout_ns = slot × node_count`（`tdma_service.c`） | 600 ms（slot 150 ms） | 随档位 |
| Core1 整表周期 | `SYSTem:TDMA:PERiod`，白名单见 `app_realtime_profile_supported()` | 1.5 ms，可不改 | 不随档位 |
| 标定身份 | `profile_crc32` + `schedule_crc32`（`tdma_service_stage_calibration()`）；若允许 slot 覆盖还须并入 effective slot 与 N | 换档即变 | 换档必失配 |

10 kHz 的 174 B 帧按 1.5 ms 整表周期要跨约 93 个相位；Core1 只在相位边界提交/收割，线上时间由 PIO/DMA 与 guard 承担。因此“10 kHz 帧能跑”与“Core1 每 1.5 ms 要等这帧”是两件不同的事，后者不成立也不必要。

**参数模型（4 节点）。** 可配置量分四层，任何一层都不把 slot 写死。

| 层 | 参数 | 4 节点默认 | 约束与入口 |
|---|---|---|---|
| 冻结 | `clk_sys`、6 PIO cycles/bit、guard 32、PIO 程序、wire 帧型/长度、`physical_bytes` 上界、flash 标定 schema | — | 不得随档位改 |
| profile | `baud_hz`、`cycle_period_ns`（slot）、`flags` | 10 kHz / 150 ms | STOP 下 `OPMode:STAGe/APPLy`；catalog 给默认值 |
| ring | `node_count`（2..4）、local/reference slot、`loop_delay_ns`、`feedback_timeout_ns` | 4 | STOP 下 ring 配置；`feedback_timeout = slot × N` |
| 派生 | `physical_bytes`、`tail_bytes`、guard 量子、TX 完成余量、维护 ACK 超时、启动窗口 | 由 N/baud/loop_delay 计算 | 不得手写；与 profile/ring 同代 |

slot 不是常量，而是按节点数与帧长校验后的参数：

```text
physical_bytes(N) = 4 + 32 + 32N + 4 + tail(N)
tail(N)           = (N-1) + ceil(ceil(loop_delay_ns × baud / 1e9) / 8) + 2   ≤ 11
slot_min(N)       = wire(baud, physical_bytes(N)) + k × guard_quantum(baud) + turnaround_margin(N), k ≥ 1
slot × N          ≤ UINT32_MAX
```

10 kHz、`loop_delay_ns ≈ 1 µs` 时：N=4 → 173–174 B、slot ≥ 150 ms；`slot_min(4)` 约 139.7 ms。N>4 不生成 catalog 项，且 `slot_sufficient(..., N>4)` 对 150 ms 直接为假。

**可配置的边界。** `baud`/`slot`/`flags` 属 profile，`N`/`loop_delay` 属 ring，超时与窗口是派生量；四者必须同代绑定。若要让 `slot` 脱离 catalog 在线覆盖，必须把 effective `slot` 与 `N` 并入 `schedule_crc32`，否则换 slot 后旧 calibration stage 仍会通过 `profile_crc32 + schedule_crc32` 校验——这是 `TDMA-OPMODE-01` 的契约变更，须先 C11 登记。登记前只允许选 catalog 里已生成好的项，不允许任意写 slot。

**逐层可行性（按当前代码）。**

| 层 | 结论 | 证据与说明 |
|---|---|---|
| PIO 分频 | 成立 | `tdma_origin_cadence_calculate()` 用 `divider256 = clk_sys × 256 / (baud × 6)`；10 kHz 得 1,066,666（整数 4166、小数 170），低于 16-bit 上限；PIO 可达最低约 636 Hz。 |
| slot 与 guard | 成立（有下界） | 同一函数要求 `required_cycles > fixed_cycles`，即 slot 必须比帧时多出整数个 guard 量子；slot 等于帧时会被直接拒绝。10 kHz 量子为 533 µs。 |
| 帧长与 tail | 成立 | `tdma_pio_spi_phys_arm()` 组帧为 `4 + 32 + 每节点 32 + trailer 4 + tail`；tail 随 `loop_delay_ns × baud` 缩小，低速下不超过 `TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES`。 |
| 运行态 baud 准入 | **不成立** | `tdma_ring_runtime.c` 与 `tdma_pio_spi_ring_adapter_tx.c` 两处硬性要求 baud 落在 1 MHz 到 50 MHz，10 kHz 在 ARM 前即 `BAD_CONFIG`。 |
| 档位目录 | **不成立** | `s_tdma_operating_profiles` 仅 23 项，无 10 kHz / 100 kHz / 5 MHz；`TDMA_OPERATING_PROFILE_MIN/MAX_CYCLE_PERIOD_NS` 仍是 100 µs / 2 ms 语义。 |
| TX 完成期限 | **不成立** | `tdma_pio_spi_phys_flight_io.c` 固定 `wire_ns + 1 ms`；Core1 服务粒度 1.5 ms 时，139 ms 的长帧在完成点附近可能被判超时并触发 `origin_recover`。 |
| 参考发射调度 | 成立 | `tdma_pio_spi_ring_adapter_service.c` 以 `feedback_timeout_ns` 为发射周期，按绝对 deadline 累加并合并错过的拍；600 ms 周期无溢出。 |
| RX 增量扫描 | 成立 | `tdma_pio_spi_phys_rx_scan.c` 按生产字数扫描、按窗口复制，不依赖比特时长；1024 word 的 RX ring 在 10 kHz 下约容 6 帧。 |
| 免标定 ARM | **不成立（语义断点）** | 无 stage 时 `tdma_runtime_owner` 只有 topology-probe 相位；而 adapter 在 probe 模式下停用 flight engine，过程映像与维护邮箱不上线。 |
| 维护命令 ACK | **不成立** | `tdma_maintenance_runtime.c` 固定 50 ms、3 次；10 kHz 单圈 600 ms，ACK 未到即超时，重试只递增 generation，命令无法完成。 |
| CAL / P3 | **不成立** | P3 白名单 10/25/30 MHz；低速档没有 bit-level delay 测量，不能凭自身测量生成 `calibration_stage`，因此不能自行“修好”高速标定。 |
| DPLL / VDC | **定位排除** | 600 ms 圈 → 1.67 Hz 样本；`vdc_time_mapping` 只校验非零，不会自动拒绝，必须由 STOP 流程保证 VDC 不在低速档运行。 |
| 启动窗口 | 需改工具 | `TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES = 2`，ARM 前需要 2 圈对齐 = 1.2 s（4 节点）；工具窗口要按档位换算。 |
| 诊断捕获 | 语义失效 | `8 × 32 × 4 ns = 1.024 µs` 的 SCK 波形捕获窗口，在 100 µs 位宽下只采到电平，不能作为低速波形证据。 |

**关键算术（推导，非实测）。**

- 分频：`divider256 = clk_sys × 256 / (baud × 6)`。10 kHz 整数分频 4166、小数 170，量化误差约 1/4166 ≈ 0.024%，比 10 MHz 档（clkdiv 约 4.17）更细；低速档的分频抖动不是瓶颈。
- guard 量子：`32 × divider / clk_sys`，即 32 个 PIO 控制拍；10 kHz 为 533 µs。slot 必须取“帧时 + 整数个量子”，不能取帧时本身。
- ARM 相位准入：`period_cycles = clk_sys / baud`，10 kHz 为 25,000；相位表只允许 1..31，远小于半个 bit（12,500 cycles），低速下相位准入反而最宽松。
- 环回周期：`slot × 节点数`。它同时是参考发射周期、feedback 超时和维护 ACK 的物理上限，必须按“帧时 + 每跳重装余量”取，不能按帧时取。

**速率阶梯（推导值；帧长 40/174 B 为命令/四节点实际帧快照）。**

| baud | 位宽 | 40 B | 174 B | guard 量子 | 4 节点环回（按推荐档位） |
|---|---:|---:|---:|---:|---:|
| 10 kHz | 100 µs | 32.0 ms | 139.2 ms | 533 µs | 600 ms（slot 150 ms） |
| 100 kHz | 10 µs | 3.2 ms | 13.9 ms | 53.3 µs | 60 ms（slot 15 ms） |
| 1 MHz | 1 µs | 320 µs | 1.39 ms | 5.33 µs | 8 ms（level 19，slot 2 ms） |
| 5 MHz | 200 ns | 64 µs | 278 µs | 1.07 µs | 2 ms（slot 500 µs） |
| 10 MHz | 100 ns | 32 µs | 139 µs | 0.53 µs | 8 ms（level 0，slot 2 ms） |

档位与帧长不是一回事：1 MHz 的 2 ms slot 对 174 B 留 0.61 ms 余量；10 kHz 的 150 ms slot 对 174 B 留 10.8 ms（约 7.7%），已含每跳重装余量。5 MHz 的 500 µs slot 对 174 B 留 222 µs，应在 L0 实测后再定。

**档位生成（追加在 level 22 之后，保持 0..22 编号与既有 CRC 不变）。**

| level | baud | slot | 定位 |
|---|---|---|---|
| 23 | 10 kHz | 150 ms | 4 节点引导环（174 B）；首个台架切片 |
| 24 | 100 kHz | 15 ms | 4 节点中间档 |
| 25 | 5 MHz | 500 µs | 4 节点补齐档 |

1 MHz 与 10 MHz 复用既有 level 19 / level 0；不新增 8 节点项。`TDMA_OPERATING_PROFILE_COUNT` 为 26（23 + 3）。`TDMA_OPERATING_PROFILE_MIN/MAX_CYCLE_PERIOD_NS`、`tests/unit/test_tdma_operating_profile.c` 与 `OPMode:CATalog?` 工具解析必须同步；新增项只经 STOP 下的 `OPMode:STAGe/APPLy` 生效。`tdma_operating_profile_find_baud()` 只按 baud 返回首个匹配，选档须用 `find_pair(baud, slot)`。

**低速档会打破的既有假设。**

1. **线时 ≤ TDMA 相位 WCET 的编译期断言。** `app_runtime.c` 用编译期 `BOARD_TDMA_SPI_BAUD_HZ`（10 MHz）断言最大线时 ≤ `PROJECT_CORE1_TDMA_BACKGROUND_WCET_CYCLES`；10 kHz 的最坏帧线时约为该预算的 400 倍。flight 路径本身是“提交 + 有界轮询”的异步路径，参考侧也明确允许完成在后续服务拍才收割，但该前提被打破后必须用实测证明 TDMA 相位 WCET、迟到与 IRQ 预算均未变化，不能只靠“代码看起来不等待”。
2. **固定 1 ms 的 TX 完成余量**，见上表；低速长帧在完成点附近可能被判超时。
3. **固定 50 ms 的维护 ACK 超时**，见上表；600 ms 圈回时间大于超时，重试只会把 generation 推高。
4. **“无标定 = topology probe”**：产品过程映像的 ARM 目前必须带 calibration stage，或落在会停用 flight engine 的 probe 模式；低速引导环如果要承载命令，必须新增一条显式的免标定产品 ARM 语义。
5. **DPLL/VDC 的样本率假设**：1.5 ms 表与 600 ms 圈不是同一量纲；低速档必须把 VDC/DPLL 置于 STOP 或显式 SKIPPED，不能用“仍然收得到帧”推断锁相可用。
6. **启动/工具窗口**：2 圈对齐在 4 节点 10 kHz 是 1.2 s；任何“5 s 内 UP”的脚本都要按档位换算，不能用固定帧数。
7. **诊断捕获的语义**：4 ns 采样网格配 100 µs 位宽只看到电平；低速波形证据必须来自示波器或新的按位宽缩放的捕获，不能复用现有 SCK 捕获。

**结论。** 物理与时序在算术上**成立**（物理层与从板重装仍须 L0 台架确认）：分频、guard、帧长、RX 扫描、ARM 相位准入都在低速下更宽松，10 kHz 甚至比 10 MHz 有更大的相位与量化余量。当前软件**不可行**：四处硬门槛（baud 区间、档位目录、TX 期限、维护 ACK）加一处语义断点（免标定 ARM）会分别把 10 kHz 拒在 ARM、发射、命令和标定之外。因此定位只能是**4 节点引导/诊断档**：可用来验证线序、心跳和少量命令，不能作为产品运行档，也不能替代高速标定。

**执行路径（A 路：改现有基础件，不新写 PHY）。**

| 阶段 | 目标 | 改动 | 门禁 / 回退 |
|---|---|---|---|
| L0 物理前置 | 把“100 µs 位宽、139 ms CS 低电平”从待测变已知 | 不改产品代码；用台架 SPI 主设备或临时测试固件在 STOP 下静态发 40/174 B 帧，示波器测 SCK/CS/DATA 与驱动/隔离器 | 边沿单调、CS 正常回高、误码 0/1e4；不通过则停止，不改固件 |
| L1 目录与准入 | 让 10 kHz / 100 kHz / 5 MHz 成为 4 节点合法档位 | 追加 level 23–25；`COUNT` 同步；新增按 (baud, slot) 查档的助手；两处 baud 区间改为查表 + `slot ≥ slot_min(N)` 校验；host 覆盖 N=2..4 与 N>4 拒绝 | host 全绿；负测：目录外 baud、slot=帧时、N>4、旧档 CRC 不变；失败只回退新增项，不动 0..22 |
| L2 TX 期限 | 长帧不再误判完成超时 | `tdma_pio_spi_phys_flight_io.c` 的 `+1 ms` 改为 `wire_ns + max(2 × Core1 表周期, 1 ms)`（或用 `2 × cycle_period_ns` 作保守上界）；host 覆盖 139 ms 帧 + 1.5 ms 服务 | 1 MHz/10 MHz quick P3 不回归；10 kHz 正常完成时 `tx_timeout_count == 0`；真挂死仍在派生期限内恢复 |
| L3 免标定 ARM | 让过程映像与维护邮箱在无 stage 时上线 | **已批准 B（2026-11-04）**：`TDMA_RING_FLAG_BRINGUP_NO_CAL` + STOP-only `RING:BRINGup <0|1>`；无 stage ARM 固定相位 2..31、保持 flight engine，并按 slot 顺序填 bring-up topology（process image 需要），禁写 flash 标定、禁 DPLL 入口；A 合成 stage 不采用 | 未置 flag 仍拒绝；置 flag 后四板 10 kHz 有 UP/DOWN 与过程映像；不产生标定记录；失败回到 probe 模式 |
| L4 命令通道 | 低速下 CAL/维护命令可完成 | `tdma_maintenance_runtime.c` 的 ACK 超时改为按 `feedback_timeout_ns` 派生并设上限；保留 3 次重试与 generation 语义；超时/nack 分开计数 | 单帧命令 ≤2 圈完成（7 帧命令按 7×2 圈预算，10 kHz 约 8.4 s）、`tx_retries == 0`、`tx_fail == 0`；断链时 3 次后 fail 且可恢复；不改变高速档 50 ms 行为 |
| L5 阶梯与切换 | 4 节点 10k→100k→1M→5M→10M 可重复 | 每档 STOP → STAGE → APPLY →（高速段重新 stage calibration）→ ARM → START；低速段只 `clear_calibration_stage()`，不 commit；每档 60 s 观测 | 每档 `up_sequence` 单调、无 `WINDOW_MISSED`、无 `PAYLOAD_STARVATION`；切换前后 `config_seq/applied_config_seq` 对账；DPLL 只在 ≥1 MHz 评估 |
| L6 退出/回退 | 失败可回到产品档 | STOP → 默认 level 0 → 必要时重上电恢复训练记录 → 重新 stage/ARM；低速档不写 flash | 回退后 1 MHz/10 MHz 基线复跑通过；旧的低速计数不进入新产品会话 |

**验收矩阵。**

| 层 | 用例 | 通过判据 |
|---|---|---|
| host | 档位目录、分频与 guard、(baud, slot) 一致性、N=2..4 准入、N>4 拒绝、TX 期限派生、ACK 超时派生 | 新增/既有用例全绿；负测按预期拒绝；不改变既有档 CRC |
| 四板（4 节点，必验） | 10 kHz/150 ms 引导环、过程映像、命令/ACK、STOP/APPLY、无 stage ARM | UP/DOWN 单调；命令 ≤2 圈；无标定写入；STOP 后无残留 |
| 示波器 | SCK/CS/DATA 在 100 µs 位宽与长 CS 下的波形 | 边沿、电平、占空比符合驱动规格；CS 低电平期间无异常 |
| 负测 | 目录外 baud、slot=帧时、N>4、未授权免标定 ARM | 分别拒绝且 reason/计数正确，不静默降级 |

**禁止改。** `clk_sys`；PIO 程序与 `TDMA_ORIGIN_CONTROL_CYCLES_PER_BIT` / guard 常量；`physical_bytes` 上界；wire 帧型/长度；标定 flash 单记录 schema；Core1 相内阻塞；运行中热切换。不得把低速档设为 `PROJECT_TDMA_SPI_BAUD_HZ` 的编译期默认值：CMake 白名单只服务 10–50 MHz 的默认构建，低速默认值会让 `APP_REALTIME_WIRE_MAX_CYCLES` 断言直接失败。低速会话只 `clear_calibration_stage()`，**禁止 commit**，避免覆盖 flash 唯一标定记录。

**未决项（台架）。** 100 µs 位宽与 139 ms CS 低电平在 ISO1452/线缆上的眼图与共模；从板 RX→TX 重装（cut-through）延迟的实测上界，它决定 slot 余量而不是 guard 量子；600 ms 圈 + 1.5 ms Core1 服务下发射相位抖动与环内多帧重叠；`tdma_event_observer` 在 600 ms 周期下的 `join_timeout_cycles` 与 history 容量；OTA/BOOTSEL 刷写路径的 counter/签名门禁。以上均需实测，不能由 host 结果替代。

### 开机重建与单口启环（`TDMA-BOOTPROFILE-01`）

**唯一镜像。** 引导期的 10 kHz 环不在启动时由主机逐板下发，也不靠编译期台架开关：每板持久化一份
boot profile（v2 地图的 CALIBRATION_NVS 双扇区记录），上电后由 `app_guide_boot_service` 按板号定序
重建。P3 验收镜像与台架镜像是**同一构建**（都按 `pico2-v2-factory-candidate` 预设），差异只在
provisioned 状态，不在 image；不得为台架另立镜像。唯一的编译期前提是 v2 部署地图（`v2_candidate`），
这是分区布局而不是本功能的开关。

**产品默认档（裸板入环）。** 刚烧录、从未 provisioning（或记录被清除）的板子也必须能加入引导环，
否则现场换板永远收不到 provisioning 命令。因此镜像内置一份**产品默认 profile**：`profile_level`
与 `node_count` 取构建常量（`PROJECT_TDMA_GUIDE_LEVEL` / `PROJECT_TDMA_GUIDE_NODES`），`local_slot`
取板号-1，`flags` 固定含 `BRINGUP_NO_CAL`，`topology_crc32` 取固定产品标签 `DHRT100-GUIDE-4N` 的
CRC32，`generation=1`。默认档**不写 flash**，每次上电合成；一旦写入持久化 profile，**持久档优先**。
板号为 0（未写 `SYSTem:BOARD:NO`）时默认档不成立，开机保持 NONE——板号写入属于烧录流程的一部分。
`BOOT:PLAN?` 末尾追加第 20 字段 `profile_source`（0=无档、1=持久档、2=产品默认档），
`BOOT:PROFile?` 仍只反映持久档（默认档时 `loaded=0`）。默认档与持久档共用同一套 8 步引导序列。

**记录语义。** `tdma_boot_profile_t`：`version / profile_level / node_count / local_slot / flags /
topology_crc32 / generation` + CRC32（`tdma_boot_profile.h`，32 B 定长）。`BRINGUP_NO_CAL` 位允许无
标定 ARM（`TDMA_RING_FLAG_BRINGUP_NO_CAL`），本镜像开机没有 calibration stage，因此**写入口拒绝
缺该位的 profile**（否则每次上电都会重放一个必然失败的 ARM）。`topology_crc32` 只是主机对账身份：
`tdma_boot_plan_decide` 不比较它，开机裁决只看 profile 合法性与 slot/node 一致性。

**存储所有权。** boot profile 落在 v2 地图 CALIBRATION_NVS 的第三个子区（calibration store 与
seat mount store 之后各两个擦除扇区），仍由 `FLASH_TRANSACTION_REQUESTER_CALIBRATION` 写：flash 事务
策略把该 requester 的合法范围扩展为"calibration 子区**或** boot profile 子区"，两区由 flash map 的
静态断言保证互不重叠；单写者仍是 flash AO（core1 park → 写 → 重启环），SCPI 不得绕过它直接写 flash。

**写入口。** `SYSTem:TDMA:BOOT:PROFile <level>,<nodes>,<slot>,<flags>,<topology_crc32>`：
- **STOP-only 且 fail-closed**：`enabled` 或 `adapter_started` 为真**或环快照读不出**都拒绝。快照读不出
  与"环在跑"相关（core1 每周期发布，guard 重试可能耗尽），而 flash AO 会为写入短暂停环再重启，
  fail-open 会在测量窗口内静默改变环状态。
- 参数区间：`level != 0`、`2 <= nodes <= min(TDMA_BOOT_PROFILE_NODE_MAX, PROJECT_NODE_CAPACITY)`、
  `slot < nodes`、flags 是已定义位子集、`topology_crc32 != 0`。
- generation 由 `tdma_boot_profile_store_next_generation()` 决定（重扫两扇区取最新可读记录 + 1，空
  store 为 1，`0xFFFFFFFF` 直接拒绝），**严格大于**已存记录；`status.generation + 1` 不再使用（缓存
  可能落后于 flash，且损坏记录会让板子永久卡在旧代）。
- 成功回 `"BOOTPROF",generation,sequence,payload_crc32`。错误码分层：
  `..._ARGUMENT`（缺参）/`..._RING_RUNNING`/`..._RANGE`/`..._FLAG`/`..._UNSUPPORTED`（v1 地图）/
  `..._GENERATION`/`..._FLASH`/`..._VERIFY`/`..._RECORD`，主机可据此区分"不支持"与"写失败"。
- 写入口会先擦除**无法解析的扇区**（旧 schema/object、坏区）再追加，避免一条旧记录永久阻断
  provisioning；正常路径仍是 append + 代次选择 + 回读校验。

**清除入口。** `SYSTem:TDMA:BOOT:PROFile:CLEar`：同一 STOP-only 门，擦除两个扇区并把状态恢复为
`loaded=0, generation=0, reject_reason=EMPTY`，回 `"BOOTPROF","CLEARED"`。擦除必须**物理回读验证**：
两个扇区都要 `drv_flash_is_erased()` 为真（全 0xFF），且重扫不得有可解析记录；任一不满足就回
`..._VERIFY`，不得谎报 CLEARED——只查"有没有记录"会把可读的垃圾/半擦除扇区误判为空。flash 完成日志
按"请求指纹"去重，指纹包含 `store_generation`/`nonce`；write、不可解析扇区擦除、rotate 目标擦除与
clear **全部**把调用方的一次性 nonce（SCPI 层取 boot uptime）折入身份——否则同参数重写或重复 clear
会被当成 COMPLETED 重放而不做物理 IO（实板 2026-11-08 实测：`CLEARED` 后记录仍在、同参数重写报
`VERIFY`）。代价是崩溃一致性不再依赖日志去重，而依赖"重放幂等 + 物理 verify"：program 由驱动
memcmp 与事务 verify 兜底，erase 幂等。清除后同样需要重启才改变开机行为。

**读回。** `SYSTem:TDMA:BOOT:PROFile?` → `"BOOTPROF",supported,loaded,reject_reason,generation,
sequence,payload_crc32,level,nodes,slot,flags,topology_crc32`，随后追加 4 个扫描诊断字段
`scan_result[0], scan_result[1], scan_usable[0], scan_usable[1]`（共 16 字段；诊断只用于把
`RECORD` 拒绝归因到扇区扫描还是 profile 编码）。v1 兼容地图上 `supported=0`、写入/清除以
`..._UNSUPPORTED` 拒绝，不得静默降级。

**开机行为。** 写入后**需重启生效**：boot plan 在 `app_init` 时由 `tdma_boot_plan_decide` 冻结，重写
profile 不改变本代的 `BOOT:PLAN?`（因此 `PROFile?` 可能显示新代而 `PLAN?` 仍是旧代，直到重启；主机
不得用 `PLAN?` 判"写入是否生效"）。`tdma_boot_plan_decide`：slot 0 → `START_REF`，其余 → `LISTEN`；
序列为 TOPOLOGY→STOP→STAGE→APPLY→BRINGUP_NO_CAL→**SET_FLIGHT_MODE**→ARM→START/LISTEN。
`NODE_MISMATCH/SLOT_MISMATCH/REJECT` 分支在开机路径不可达（app 传入的 expected 正是 profile 自身），
只在被其它调用方复用时生效。半环由 20 s 完整性窗口判定：`down_running` 为 0 即 `half_ring=1` 并
STOP；不得停留半环。

**SET_FLIGHT_MODE 是命令面的前置。** 默认 `PHYSICAL_FLIGHT` beacon 只有在"没有飞行 TX 排队"时才携带
一个维护 chunk；而 RefMem 飞行同步每个环回都会填 FIFO（10 kHz/level 23 下每 600 ms 一次），所以
provisioning 或 GUIDE 命令会永远留在队列里（2026-11-07 实板：`q_depth=1, tx_ok=0`，60 s 不动）。
boot 序列在 STOP 之后设置 resident process image（`tdma_runtime_owner_set_flight_process_image_mode(true)`，
STOP-only），同一命令随即排空并四板生效（`ack_ok` 增长、`frag_done=1`、四板 `PLAN?` gen 同步）。
因此"开机自组环"必须带这一步，否则 `BOOT:PROFile` 虽然成功、环也 up，但单口 C/T 不可用。

**单口读回与重跑屏障。** `SYSTem:TDMA:BOOT:PLAN?` 现为 **20** 字段（0-based：`fail_step=3`、
`half_ring=16`、`boot_done=17`、`boot_next_step=18`、`profile_source=19`；原 17 字段后追加
`boot_done`、`boot_next_step`）。判据：四板 `boot_done=1`、`fail_step=0`，slot 0 `action=START_REF`、其余 `LISTEN`、
`half_ring=0`；`RING:STATus?` `enabled=1,node_count=4,up_running=1,down_running=1,adapter_started=1` 且
`total_up_tx/total_down_rx` 单调增长。**重跑验收**：boot 服务是 3–33 s 有界重试，ARM 失败还会从 STOP
重放；主机在它飞行期间发 `RING:STOP` 会被下一次 ARM/START 撤销。因此 P0T 之前的屏障是
`BOOT:PLAN?` `boot_done=1`（或固定等待 ≥33 s），不能假设复位后环立刻是停的。

**禁止。** 环运行时（或环快照不可得时）写/清 boot profile；缺 `BRINGUP_NO_CAL` 的 profile；把 v1 地图
的"不支持"当成功；用第二个端口或第二份镜像绕过单口目标。


### Ring reason code

`TDMA-REASON-01` 保留 `tdma_ring_runtime.h` 中的稳定语义：

| Reason | 含义 |
|---|---|
| `NONE` | 无 ring fault。 |
| `BAD_CONFIG` | 参数、slot、flag 或 CRC 不合法。 |
| `EVIDENCE_MISSING` | 缺少要求的环路证据。 |
| `DIRECTION_CONFLICT` | UP/DOWN 组缺失或方向冲突。 |
| `ADAPTER_MISSING` | 无可执行 adapter。 |
| `TIMESTAMP_MISSING` | 缺少要求的硬件时间证据。 |
| `PAYLOAD_STARVATION` | 必需负载/窗口供给不足。 |
| `WINDOW_MISSED` | 未命中要求的时间窗口。 |
| `RESOURCE_CONFLICT` | PIO/SM/DMA/IO 等资源冲突。 |

### 消费域接口

| 域 | 交接 | 不属于 TDMA 的责任 |
|---|---|---|
| Calibration | topology、矩阵身份、训练输入；TDMA 提供资源及原始运输证据。 | 测量 delay/bias/residence、决定校准质量。 |
| VDC/DPLL | typed provider/sink、原始事件与质量；兼容 trailer 独立保留。 | 参考时间编码、delay 补偿、PI/DCO、锁相与正式发布。 |
| RefMem | 普通 mailbox、片段、运输确认及错误。 | active fact、commit/fence 和业务可靠性。 |
| Trigger / T2 | 受控 opaque 预约、READY/fence/completion 载荷，布局与预算另验。 | 动作语义、目标时间、硬件执行与 T2_actual。 |
| SYNC_IO | 经资源和时间接口协调确定性 capture/output。 | IN/OUT 的状态机、输出计划与引脚精度。 |
| 管理与观测 | 配置 intent、只读 snapshot、冻结记录。 | 通过 SCPI 轮询维持实时运行。 |

维护流 TX 队列是 SPSC：core0 producer 以 release 发布 tail，core1 consumer 以 acquire 读取 tail 后才读槽位；槽位只在帧**完全发完**后 release head（不得在 `tx_begin` 时提前释放），队列满显式拒绝并计 `queue_full`，不静默覆盖在途帧。该不变量由 `tdma_spsc_index.h` 的 index helper 与 host 用例约束。

维护流 RX 重组器在 **header 不一致**时放弃未完成的旧帧并以新帧头重启，不永久拒绝后续帧；重复 chunk 只计 `duplicate_count`，不重复复制。单次 chunk 丢失不得 wedge 整个 run；整帧丢失的补偿属于上层 per-stream ACK/重试，不在重组器内伪造完成。

维护帧采用 stop-and-wait ACK：TX 在整帧发完后保留队列槽位，接收端经 process-image ACK 席位回 `stream_id:generation + result`（诊断字节 `0xA0|result`，与命令回复的 `ACK_FLAG_COMMAND` 区分）；源端 50 ms 超时（高速档基线；低速引导档按 `feedback_timeout_ns` 派生，见低速档一节）、最多 3 次、每次重试 generation+1，接收端按新 generation 重启重组。ACK 丢失/超时重传不得静默丢帧；重试耗尽计数并释放槽位。每个目标各回一份维护 ACK，源端按 required_mask 全覆盖才释放；命令回复优先占用 ACK 席位，维护 ACK 仅在无命令回复时发布，避免延迟命令终态；7 帧命令的 stop-and-wait 需 ≥2 s 环窗口预算。

维护 TX 队列为 16 槽（可用 15），可在一个环启动内预排两条 7 帧命令并按序执行；环运行中再入队需要并发测试驱动，当前 HIL 覆盖预排队多批。队列满时显式拒绝，不静默覆盖。

## 失败、恢复与验证映射

错帧、覆盖、重复、旧代或不完整记录拒绝并计数；DPLL 跳过坏样本，是否保持/失锁由 VDC 质量策略判定。DPLL 局部拒绝或相位超时不能自动隔离仍健康的 TDMA。STOP 意图、配置应用、物理停机和池退休需分别对账；故障不抹除先前失败记录。

| 验证层 | 能证明什么 | 不能替代什么 |
|---|---|---|
| host/C/静态资源测试 | 布局、算术、状态、owner 和拒绝分支。 | 真实 DMA 仲裁、物理时序与误差。 |
| 固定四板 quick P3 | 对应源码指纹下的基础校准输入、有效运输及清理。 | 严格零错误、完整 WCET、resident 全窗口和 DPLL 锁相专项。 |
| byte-level flight | RX/TX 重叠、局部替换和固定 pipeline 的实测范围。 | 自主 cycle-level/F1–F5 及最坏负载。 |
| 内部探针与外部波形 | 各自记录范围内的参考连续性、DCO 采用及实际边沿。 | 未采空档、绝对事件序号或尚未建立的误差上界。 |
| 产品资格 | 对应 profile 的严格时序、连续性、恢复、资源和跨域质量。 | 不能以某一项局部 PASS 自动提升。 |

固定范围和 INFO/WARN/ERROR/FATAL 判定沿用 `DOCS_EXECUTION_CONSTRAINTS.md` 与 `p3_alarm_policy.py`。数字、build、源码指纹和失败原件只进入进展及报告；详细接口判据见[运行细则](TDMA_RUNTIME_CONSTRAINTS.md)。
