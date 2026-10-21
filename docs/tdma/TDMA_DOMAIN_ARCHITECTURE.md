# TDMA 基础件主域架构

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Related: `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`
Last updated: 2026-10-21

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
| 自主 origin | `tdma_origin_plan.c`、`tdma_origin_build_job.c`、`tdma_origin_exchange.c`、`tdma_runtime_origin.inc`：硬件图、异步构造、发布选择、授权及退休。 |
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

`REFMEM_CMD` 是 RefMem 统一控制面的跨节点命令帧，首版**复用既有** `TDMA_PAYLOAD_CLASS_CONFIG_CONTROL`，不新增 payload class：

- `payload_subtype = REFMEM_CMD`；envelope 与 command slot 一致（`command_seq/type/class/origin/target/required/payload/crc`）。
- `message_class = TDMA_PAYLOAD_CLASS_CONFIG_CONTROL`；接收端按 subtype 分派，`REFMEM_CMD` 是唯一 consumer；未知 subtype 拒绝，其他 config-control consumer 不得占用同一 subtype。
- 预算以 `tdma_traffic_class_profile_t` 的 `reserved_bytes_per_cycle`/`max_frames_per_cycle`/`queue_depth`/`deadline_ns` 为准；payload 上限绑定 `sizeof(refmem_command_request_t.payload)`，不手写数字。
- 优先级：`TDMA_TRAFFIC_CONFIG_CONTROL`；不占 core1 硬实时窗口，不在 ISR 执行；config-control 是控制面预算，不承诺硬实时。
- ACK/fence 复用 `TDMA_PAYLOAD_CLASS_REFMEM_ACK_FENCE`；命令帧与 ACK 不共享队列语义。
- 只有 config-control 预算不足时，才评估专用 payload class；必须另立契约和 TDMA 联审，不能静默扩类。
- 契约状态：pending；代码锚点 `tdma_profile.h`；需 host 编码/解码 + 两板命令/ACK 证据。

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

**实现状态。** 一等邮箱 `tdma_first_class.h`；仲裁在 `tdma_pio_spi_ring_origin.inc` 的 `publish_priority`，轮换比例经 `tdma_pio_spi_ring_adapter_set_seat_rotation()` 在 STOP 下按板配置（默认 1:1，SCPI 入口 `SYSTem:TDMA:SEAT:ROTation`，计数页 `SYSTem:TDMA:SEAT:COUNters?`）；改写已排队 VDC image 的补丁路径已删除。从板侧一等记录由优先级通道第二接收端（`TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS`）承担，普通 flight 路径不再作为一等接收路径（`TDMA-PROGRESS-20261010-002`）。未完成：真实物理 selection 证据（`TDMA-FLIGHT-002K2`）、一等席服务等级与特等席对齐（`TDMA-SEAT-03`）、比例与一等扩容的 `EXE-SEAT-01` 审核和 C11 登记。

席位余量须分开核对 wire 与 CPU：当前 typed 同步 body 由 `TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_SIZE` 完整占用，不能再塞入健康状态或把普通 class 的 optional 空间重复记作 typed 可用余量。SYNC 的后续需求须列出独立资源及最坏耗时，未完成准入/实测的空间不算已承诺余量。新增或扩容特等席须先经用户审核（`EXE-SEAT-01`）。VDC 健康镜像、统计及查询是 Core0 异步诊断，不占特等邮箱，也不新增 Core1 发布工作。

### 三份额轮换与让位周期（2026-10-16 目标裁定，status `pending` 待 C11 交叉审核）

快速通道只有一条 TX 快路径，按发布机会在**特等席**（VDC typed SYNC）与**一等席**（T2 position descriptor，class `TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS`）之间轮换。**二等席的定义是"不走快速通道的部分"**——普通帧与 refmem 镜像/回执（含位置冻结回执）——它不参与轮换，因此**只能在快速通道用剩的时间里通行**。

**轮换周期语义**：`cycle = priority_share + first_class_share + second_class_share`（`tdma_seat_rotation_cycle()`，`tdma_pio_spi_ring_adapter.h`）。一个周期内前 `priority_share` 个机会归特等席、随后 `first_class_share` 个归一等席，**最后 `second_class_share` 个机会两席都不得上车（让位段）**，把该时段留给普通/refmem 流量。让位段内两席的租约**保留**（内容不丢），且**不计入** `seat_first_class_wait`（它不是"被另一席抢走机会"，而是周期为普通路径预留），因此也不会产生等待超限。`second_class_share == 0` 时 `cycle = p + f`，与三份额形式之前的行为**逐位等价**。

**让位证据（不新增计数）**：`seat_opportunity_count − seat_priority_count − seat_first_class_count = 让位机会数`；期望比例 `fc ≈ f/(p+f+s)`、`yield ≈ s/(p+f+s)`。

**配置**：`SYSTem:TDMA:SEAT:ROTation <p>,<f>[,<s>]`（STOP-only 每板；第三字段可选，缺省 0；越界或无法构成周期即拒绝）。**两种工况预设**（目标口径）：VDC **未启动**时特等席等于没在跑 ⇒ **`1,2,7`**；VDC **启动** ⇒ **`7,2,1`**。读回 `SYSTem:TDMA:SEAT:ROTation?` 为 12 字段：索引 4 = 整周期 `p+f+s`，索引 11 = 二等席份额，索引 0–10 含义不变（旧镜像 11 字段仍可正确读前 11 项）。

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

自主 origin 额外经过 preparation、READY、release 与运行授权复验，入口在 `tdma_runtime_origin.inc`。诊断授权的持续运行不等于产品 RUN 或契约已激活。

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

### TDMA-RECOVERY-01：有界恢复

Core0 准备、Core1 固定窗口选择、PIO/DMA 发送；`TDMA_RECOVERY_BUFFER_COUNT` 缓冲及 `TDMA_RECOVERY_MAX_FRAMES_PER_CYCLE`、独立预算限制重传，复用原 Node segment。ACK、retry、backpressure 和退休不得覆盖 IN_FLIGHT。设施存在不代表可靠性 HIL 已闭合。

### SPI 速率与 TDMA 周期 operating profile

`TDMA-OPMODE-01` 采用离散 baud/cycle 组合，事实源为 `s_tdma_operating_profiles`。STAGE 不更改线上状态，STOP 后 APPLY、下次 ARM 安装；effective schedule CRC 绑定 profile。不能运行中单板私自降频，candidate 档不自动成为已验收回退档。Core1 整表周期是另一项配置，两者均需通过一致性及资源门禁。

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
