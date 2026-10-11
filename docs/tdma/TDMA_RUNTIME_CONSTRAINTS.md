# TDMA 运行接口细则

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/vdc/VDC_RUNTIME_CONSTRAINTS.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-11

## 文档接口

本文补充[架构](TDMA_DOMAIN_ARCHITECTURE.md)的实现接口与约束，不新建契约、不改变登记状态。任务和验证结论分别由 TODO、Task Progress 维护。旧方案全文保存在[历史快照](../legacy/tdma/LEGACY_TDMA_DOMAIN_ARCHITECTURE.md)。

## Wire、布局与完整性

| 项目 | 事实源 | 约束 |
|---|---|---|
| transport header 与上限 | `tdma_transport_frame.h` | SHORT/LONG 编码及长度门禁一致；不能直接序列化 C struct。 |
| runtime payload | `tdma_flight_payload_size(nodes)` | `nodes * TDMA_FLIGHT_SHORT_SLOT_SIZE + TDMA_FLIGHT_DPLL_OBSERVATION_SIZE`；节点数受编译容量约束。 |
| fast header | `TDMA_FLIGHT_MAILBOX_*` | magic/version、source、target、seq16；Core1 位图扫描不是域内提交。 |
| 普通 body | `TDMA_PROCESS_IMAGE_VDC_* / REFMEM_* / ACK_* / CONTROL_*` | mandatory-first；optional 仅占已声明静态余量。 |
| typed 同步 body | `TDMA_PROCESS_IMAGE_PRIORITY_SYNC_*` | binding generation、event sequence、output-ns 下界、不确定度宽度、flags；完整 body 专用。 |
| 全局 trailer | `TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_*` | 位于有效 payload 尾部；兼容 lag-1 cycle-phase 编码，valid 与 tick mask 分开。 |

普通 class 白名单由 `tdma_process_image_transport_class_valid()` 决定，typed 同步由 `tdma_process_image_typed_sync_class_valid()` 单独识别。旧绝对时间命令 class 不因枚举存在而启用；body 的 class 必须先匹配，再调用对应 codec。

`tdma_transport_packet_crc32()` 对 FLIGHT_MUTABLE 只覆盖 header，identity CRC 保持 origin/sequence/schedule/ring 等不可变身份。mailbox CRC 由 `tdma_process_image_crc16_ccitt()` 校验；不得把 header CRC 解释为对全部可变 payload 或 trailer 的保护。当前 mutable header 不具备历史 V2 尾 CRC/WKC 候选的完整语义。

RefMem critical 内帧受 `REFMEM-260B-01` 和 transport payload 上限约束，普通 Node body 容量另算。大块事实、全量恢复及日志走明确定义的分片/维护路径，不通过扩展实时短帧解决。

## 普通异步准备与版本交接

`tdma_runtime_owner_core0_prepare_service()` 推进 lifecycle、origin 构图、RX scan、RX prepare 和 overlay 工位。Core0 不直接安装活动 DMA；Core1 仅消费完整结果并在采用前复验。

| 阶段 | 必须核对 | 完成事实 |
|---|---|---|
| 准备请求 | epoch/config、map generation、有效长度、owner 授权及输入版本。 | 只表示占用一个固定工位。 |
| 结果发布 | 完整内容先写，随后 release 发布版本。 | prepared/READY，不代表 sent。 |
| 采用 | acquire、原请求身份、资源、当前绑定及取消状态。 | selection/安装请求，不代表业务应用。 |
| DMA 退休 | 所选槽已不再被硬件引用。 | 才允许 producer 重用；旧 descriptor 不得配新 buffer。 |
| RX 普通副本 | 收帧身份、完整性、位图、覆盖复验和 FIFO 发布。 | 只供后台解析；成功交接后才推进对应新鲜度。 |
| STOP | 取消 ACK、配置确认、硬件停机与池退休分别完成。 | 命令应答不能代替这些事实。 |

TX 无更新时复用先前有效版本，不等待 Core0。普通 RX FIFO 满时允许丢弃解析副本并计数，物理转发不因该副本停止；命令、事件和可靠 delta 仍按各域重试/背压处理。固定最新值池不能承担无限事件日志。

## 同步特等席接口

### TX：已编码记录与物理发布

`tdma_runtime_owner_set_priority_tx_provider()` 在停态注册 VDC provider。`tdma_pio_spi_ring_origin.inc` 的自主服务在 provider 返回 typed offer 后走物理双缓冲发布；普通 mailbox 准备仍有独立路径。

`TDMA_PRIORITY_TX_READY` 只表示 offer 就绪。原 `EMPTY` 保留座位的行为已取消；TDMA-SEAT-01（已实现，见[架构](TDMA_DOMAIN_ARCHITECTURE.md)）规定 `EMPTY` 不占座，并与一等按发布机会轮换。发布可能因 DMA selection 未退休或版本变化延后；提供者确认、物理发布、实际送达和 ACK 分开记录。已编码事件保持原生成时模型和身份，不按后来的 DCO 模型重算历史时间戳。

### RX：固定提取与有界交接

`tdma_pio_spi_phys_priority.inc` 在准入的 Core1 IRQ 窗口消费 RX PIO 边界。固定复制 outer header、transport header 和 reference mailbox；复制前后观察 DMA 进度，检查覆盖、包长、身份与 CRC，然后发布固定记录并调用有界 sink。

记录表由 `TDMA_PRIORITY_RX_CAPACITY` 限定，按序号索引且复验完整身份；不是无限 FIFO。IRQ pending、重复、覆盖、缺口、epoch 重建和 rejected 计数必须保留。STOP 先停 IRQ 来源并退休绑定，不能让旧槽或迟到回调进入新会话。

VDC sink 可执行固定 typed 解码与入口保全，PI/FOLLOW 的最终采用在 VDC Core1 控制边界完成。普通 RefMem 分片、RTOS 消费和后台解析不是此路径的前置。

### 静态窗口与时基

`app_realtime_profile_priority()` 和 `PROJECT_CORE1_PRIORITY_RX_*` 定义各 phase 的 IRQ 配额、最大成本和关闭预留。前台开窗前检查剩余时间，关窗保留完整 IRQ 收尾成本；GUARD 不用作额外负载预算。前台墙钟包含 IRQ，不能把 ISR 另列后从总耗时扣掉。

物理 cadence 不满足入口能力时拒绝该同步 lane 并记录 `CADENCE`，普通运输按自己的状态继续。快速通道的存在不保证任意线速、任意后台停顿或全部 profile 都能逐圈保全。

事件时间由 `tdma_event_observer.c` / `tdma_event_history.c` / `tdma_origin_reference.c` 提供。TIMER1 raw、PIO counter、enable 包围区间、所载 reference 时间及 IRQ 到达时间不是同一量。缺边沿、回绕多解、旧 epoch、身份不符或覆盖须拒绝；高分辨率原始计数不自动提升为 formal evidence。

## Origin 模式与 resident 判据

| 路径 | 已有机制 | 判定限制 |
|---|---|---|
| 普通周期路径 | adapter 按 active profile 服务，保留 bootstrap 与兼容生命周期。 | 软件 TX 计数/轮询 launch 不能用于证明自主硬件发车。 |
| 显式自主路径 | `tdma_origin.pio` 的 control/guard 节拍和 DMA descriptor 图持续供给；Core0 提前构图，Core1 安装/发布/观测。 | 有效 bootstrap return、诊断授权、能力和资源复验仍需满足。 |
| 返回与更新 | origin 图保留返回身份/记录，双 bank 延续已接受映像并选择准备好的本地更新。 | 缺/坏 return 的错误事实不能被下一圈覆盖；不能宣称每一循环都无损。 |

旧架构把“回环事件触发下一发车”与“回环完全不作发射门控”混写。当前机制按 PIO control/guard 和 DMA 图描述；该描述不替代 `TDMA-EMISSIONCLOCK-01` 已登记的环边界硬件事件驱动要求。固定 guard 候选与该要求的符合性仍待复核。F1–F5 保留为独立验收条件，任何节拍语义修订另做 C11，不在文档重排中激活 `TDMA-RESIDENT-01`。

| 判据 | 所需证据 |
|---|---|
| F1：发车不依赖 Core1 | 主从 service blackout 内仍有有效硬件进展，不能用窗口外结果补齐。 |
| F2：环边界硬件事件驱动 | 按 `TDMA-EMISSIONCLOCK-01` 证明环边界事件与实际发车的因果关系、延迟及节拍下界；仅有固定 guard 周期证据不足以关闭此项。 |
| F3：抖动有界 | 实际边沿的最坏间隔与量化界，不只看平均值或软件记录速度。 |
| F4：无更新稳态 | 不需要 Core1 不断改写线行为；旧有效映像连续复用。 |
| F5：更新非阻塞 | 就绪更新被采用，迟到更新顺延，物理节拍不因装卸变化。 |

byte-level RX/TX 重叠、有限 origin blackout、全部节点 autonomous 全窗口、同圈多 Node overlay 是不同验证层。恢复 ordinary 成功不抹除 autonomous 切换时的 missing 或故障。

## 实时预算读法

唯一事实源为 `config/project_config.h`、`application/inc/app_realtime_profile.h`、`application/src/app_realtime_profile.c` 和 `application/src/app.c::app_realtime_run_phase()`。

```text
phase_width = end_cycle - start_cycle
irq_quota = 1 + ceil(phase_width / MIN_PHYSICAL_CYCLES)   [可中断相位]
irq_reservation = irq_quota * IRQ_CYCLES
foreground_budget = phase_wcet - irq_reservation
close_cycle = phase_end - CLOSE_CYCLES
```

MODEL、TRIGGER_MEASURE、GUARD 无此 IRQ 配额。长档增加 TDMA phase WCET 与区间，但 IRQ 配额随窗口宽度重新计算，前台不会获得全部新增拍数；其余 phase 平移且保持原宽度，GUARD 不借给执行负载。

以下为 **2026-09-19 代码配置推导快照，非事实源、非实测 WCET**；按 `BOARD_SYS_CLOCK_HZ` 对拍数换算。现场以当前 `profile_generation` 和整表读回为准，不能继续套用历史 380/500/850 us 门限。

| 整表周期 | TDMA 相位宽度 | service 总预算（含 IRQ） | IRQ 配额 / 预算 | 前台预算 |
|---|---:|---:|---:|---:|
| 1.5 ms | 666 us | 632 us | 2 / 20 us | 612 us |
| 5 ms | 4166 us | 4132 us | 6 / 60 us | 4072 us |
| 10 ms | 9166 us | 9132 us | 11 / 110 us | 9022 us |
| 15 ms | 14166 us | 14132 us | 16 / 160 us | 13972 us |

TDMA 相位宽度额外包含关窗预留和入口余量；上述默认配置中分别为 14 us 与 20 us（同一代码快照）。`PROJECT_CORE1_PRIORITY_RX_IRQ_CYCLES` 仍标为 candidate，必须以实际完整 ISR（含尾部）及相位墙钟验证，不能把配置预算当作已证明上界。

`app_realtime_run_phase()` 在进入时要求完整 service WCET 能放入本相位关闭点前的剩余区间；不满足则记录 skip，而不是借用后续相位。TDMA 被跳过时，可按独立候选预算依次尝试 RUN 输出规划或缓存交接；它们只占原 TDMA 区间，保留各自超时记录，不能算作完整 TDMA service 已执行。

### TDMA 相位体与环回往返是两个量

`phase_last_runtime_cycles` / `phase_max_runtime_cycles` 量的是**整个 TDMA 相位体**：`app_realtime_run_phase()` 在 `service()` 前取 `start_counter`、返回后取 `end_counter`，而该 `service()` 是 `app_realtime_tdma_phase()`，除 TDMA 传输外还执行 RX observer 收割、`vdc_run_output_service_core1()`、`sync_io_reference_service_core1()`、`sync_io_logic_analyzer_service_core1()` 以及诊断/看门狗记账。因此该数字既不是 TDMA 服务耗时，也不能直接当作任一域的 WCET。

环形**往返**不在这个数字里：TX 提交即返回、不等线上完成；帧尾忙等上限约 18 us（`tdma_pio_spi_phys_frame_tail_us()`）；DMA 停止与 TX put 的自旋分别以 `TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US`、`TDMA_PIO_SPI_TX_PUT_TIMEOUT_US` 为硬上限。按 DET-02，线上飞行时间与 CPU 工作可重叠，两者不得相加或相减，也不能用相位体墙钟反推环回时长。

归因必须读逐阶段会计，不能只引用合计数：`SYSTem:TDMA:PROFile?`、`:PEAK?`（单个完整相位，非跨相位取最大）、`:RUN?`、`:OTHer?`、`:RX?` 导出 `total_ticks`、`full_phase_ticks` 与各阶段 `elapsed_ticks`/`calls`。各阶段区间是包含关系（父项含子项），先减子项再与兄弟项相加；`total_ticks` 与未单独记账的项之差即为相位体内其余服务。四板 TDMA 阶段采集该分解作为只读证据，不新增门禁。

相位体至少分三类，量级不同，必须分类评价：**稳态周期相位**（收发处理走异步路径，只有准入、发布与记账）、**自治 origin 相位**（发射与接收处理在同一次调用内完成，`adapter` 及其 `origin_*`/`rx_*` 子项占主导）、**STOP/ARM/配置过渡相位**（`ring_runtime` 内的 stop/start adapter 路径，`adapter` 阶段不发生作用）。`phase_max_runtime_cycles` 是跨相位的粘性最大值，会把三类混成一个数；`peak`、`autonomous_peak`（`RUN?`）、`other_peak`（`OTHer?`）与 `last` 就是为分类评价提供的。不得用粘性最大值对标稳态 WCET，也不得把过渡或自治相位的耗时记作周期服务超预算。

| 观测量 | 用途 | 易混淆项 |
|---|---|---|
| wire / round trip / emission interval | 验证线速、固定 pipeline 与自主节拍。 | 不等于 Core1 表周期，也不等于 DPLL 有效更新间隔。 |
| 完整 phase/service 墙钟 | 判定 service overrun、进入迟到及 deadline miss。 | IRQ、检查和已有探针成本不能从验收总量中扣掉。 |
| IRQ body / tail / count | 解释总耗时并核对固定配额。 | 分项 body 不包含全部收尾，不能当完整 ISR WCET。 |
| RX prepare / accept / publish | 区分 Core0 准备与 Core1 校验/交接成本。 | 名为 RX_PARSE 的分项并非全部普通解析耗时。 |
| RUN / RESET / OTHER / 切换 | 定位角色、状态与峰值归属。 | 不能删去首拍、停态或切换高峰后声称完整 WCET 通过。 |

下一轮预算收敛先绑定同源码、同 profile、同节点/槽位/采样模式，核对物理进展和有效负载，再分析完整 phase 峰值。普通路径已异步化不代表全部 Core1 接受成本已移走；优化必须同时保留 selection 退休、新版本准入、CRC/身份及 STOP 取消。

## 配置、资源与停态边界

`tdma_service_ring_stop()` 先关闭 scheduler 准入，再提交停态意图。Core1 stop gate 处理已选传输和物理停止；`tdma_service_core0_lifecycle_service()` 在配置应用确认后退休队列/恢复池，锁忙留待后续调用。ARM/profile 更新与 STOP 由管理发布保护串行化。

`tdma_service_configure_flight_map_checked()` 保留 invalid、busy、active 等拒绝点。`SYSTem:TDMA:RING:TOPology` 是节点布局入口，不另建运行时邮箱数量事实源。`distributed_refmem` 在 ARM 前构建 map，physical owner 核对有效长度及 active mask；RUN 长度固定，改变容量需 STOP、退休并重新 ARM。

| 配置 | 唯一事实源 | 验证范围 |
|---|---|---|
| 编译容量 | `config/project_node_capacity.h`、CMake target 定义 | 本地数组、映像上限及超容量拒绝；不同容量需要互操作验证。 |
| 在线拓扑 | Calibration / active ring profile | slot/节点身份、active mask、有效邮箱数和反馈关系。 |
| baud / wire 周期 | `tdma_operating_profile.c::s_tdma_operating_profiles` | STOP 后 APPLY，下一次 ARM 安装及 effective CRC。 |
| Core1 整表周期 | `app_realtime_profile_supported()`、`PROJECT_CORE1_PROFILE_*` | 请求/确认、整表边界安装、后续相位平移、ARM 冻结。 |
| persona 资源 | `tdma_state_machine_resources.h`、`tdma_pio_spi_phys_programs.c` | PIO 指令占用、SM/DMA/FIFO owner、共享 workspace 生命周期。 |

不能用运行时减少节点证明 RAM 释放；静态 RAM 以目标 map、栈水位及共享池占用核算。校准持久化格式、导入 CRC 和 wire 长度是不同兼容性维度，裁剪数组不等于旧数据包自动兼容。

## 席位准入软件模型

`tdma_seat_admission.h/.c` 是 TDMA owner 串行调用的纯模型，当前没有 provider、PIO、IRQ 或 generic scheduler 运行调用，不新增特等席工作，也不修改现有 mailbox/回环路径。结构不是跨核原子快照；后续接线必须由唯一 owner 维护并用已有 guard 发布副本。

- `begin` 使用跨 STOP/换代不复用的递增 epoch，关联 cycle 与 config/profile/binding generation、priority sequence；新轮验证失败也撤销旧授权，耗尽不能回绕。
- `priority_selected` 和 `priority_committed` 必须来自同一 token 的物理事实；`READY/EMPTY`、FIFO 发布、普通 scheduler dispatch、备用 shadow 发布都不满足这些前置。
- `grant` 检查 `TDMA_SEAT_BUDGET_ALL` 的各量纲预算，以及 `[start_ns, end_ns - close_guard_ns)` 窗口。预算由 owner 在同代预先准入并扣除高等级预留；FIFO 空位不能代替 RX wire/消费预算。
- `select_load` 在临交接前复验本轮预算和剩余 duration，消费一次本地授权；`commit_load` 仅记选中 load 的后端实际交接确认，不等于 SENT、RX 或 APPLIED。后端失败撤销 FAULT；已发生的晚交接保留 `load_committed` 事实，同时报窗口过期，不重新授予本轮额度。
- 时间来自同一个 owner 单调时基。commit 的时间为实际交接时间，不是延迟回调/查询时间；owner 在推进模型时钟前报告交接。`expire` 须在决策边界调用；存储状态不是自动推进的实时时钟。STOP、新特等事件、绑定变化和故障由 owner 显式撤销。

本模块验证的是软件状态语义；TDMA-SEAT-01（已实现）不再以其时间窗授权作为一等上车前置。真实发送接线、最坏资源预算和跨板共存属于 `TDMA-FLIGHT-002K2`，未经这些证据不能据 `FIRST_CLASS_WINDOW_GRANTED` 的软件用例宣布物理一等席可用。

## Recovery、训练与业务扩展

Recovery 用 `TDMA_RECOVERY_*` 的独立缓冲、预算和每周期配额，Core0 准备、Core1 选择、PIO 执行；ACK 成功才退休，重试耗尽或无空槽明确背压/失败。它不增加物理节点、不扩展 process image，不承载原始波形或日志。端到端可靠性和多拓扑恢复仍按 TODO 验收。

Calibration 拥有线序、delay、residence、bias、matrix generation/freshness 及接受质量；TDMA 只提供训练 persona、窗口、资源、原始测量和失败原因。复用兼容且已确认的环序不要求重扫全部候选。正式校准门禁与可用诊断输入分别记录。

当前 T2 position descriptor 使用独立 `TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS`：运行期是紧缩单帧记录，经 origin 快速路径一等邮箱运输、与特等按发布机会轮换，从板由优先级通道第二接收端接收（`TDMA-PROGRESS-20261010-002`）；`TDMA_PROCESS_IMAGE_T2_FRAGMENT_*` 分片仅在旧固件/迁移回退时出现，不在运行期路径上，也不再借用二等 RefMem 区域。物理 flight 白名单独立准入，普通 RefMem delta parser 仍拒绝该 class。四板实际送达与从板冻结/动作已在台架闭合（`T2-PROGRESS-20261010-024`，实测快照），但真实物理 selection 证据、seat 计数/比例 SCPI 页和一等期限预算仍未闭合。另一分支的单板 sequence 测试不是本分支 TDMA 多板预约验收。

## 观测与验收

**TDMA-SEAT-01 比例与计数的 SCPI 面（2026-10-11 落地）。** 轮换比例是 STOP-only 的每板设置，不写入 ring profile/geometry，因此不改变 wire CRC：

| 命令 | 语义 |
|---|---|
| `SYSTem:TDMA:SEAT:ROTation <priority_share>,<first_class_share>` | 设置 origin 快速路径的 M:N 轮换比例。每个 share 取 1..`TDMA_SEAT_ROTATION_SHARE_MAX`，且 `M+N <= TDMA_SEAT_ROTATION_CYCLE_MAX`；越界、零值或环已使能/适配器已启动时拒绝，返回错误码 `TDMA_SEAT_ROTATION`。成功回 `OK,<M>,<N>`。 |
| `SYSTem:TDMA:SEAT:ROTation?` | **配置 + 声明上限（11 字段，2026-10-11 扩展）**：`priority_share,first_class_share`（配置值，未设置回报 0，生效值见后两字段）、`priority_share_effective,first_class_share_effective,cycle`（由 `tdma_seat_rotation_priority/first_class` 解析后的生效比例与周期）、`share_max,cycle_max`（`TDMA_SEAT_ROTATION_SHARE_MAX`/`_CYCLE_MAX`）、`first_class_wait_max_opportunities`（`TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES`）、`hold_depth,hold_max_attempts`（`DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH`/`_MAX_ATTEMPTS`）、`loop_schedule_enabled`（`TDMA_LOOP_SCHEDULE_ENABLED`）。**为什么把上限放进页里**：计数只有与声明预算对照才是证据；且 `seat_starved_count`/`seat_first_class_starved_count` 只能在席位窗口门控内增长，而该门控在 `loop_schedule_enabled == 0` 时被编译掉——采集必须自解释，否则"饥饿为 0"会被误读成"验证过"，实际只是门控未启用（特等席侧同类可见性见 `SYSTem:TDMA:FLIGHT:PRIORity:BUDget?`）。 |
| `SYSTem:TDMA:SEAT:COUNters?` | 读回 origin 席位与一等席服务事实：`seat_opportunity_count,seat_priority_count,seat_first_class_count,seat_priority_yield_count,seat_first_class_reject_count,seat_first_class_wait_max,first_class_stage_refused_count,first_class_retired_waiting_count,seat_first_class_wait_violation_count,seat_starved_count,seat_first_class_starved_count,seat_first_class_deferred_count`。计数为累计值，读取不改变链路。 |
| `SYSTem:TDMA:SEAT:HEALth?` | **一等席稳定性判定**（24 字段）：`ok,reason_mask,seat_opportunity_count,seat_priority_count,seat_first_class_count,seat_first_class_wait_max,seat_first_class_wait_violation_count,first_class_stage_refused_count,first_class_retired_waiting_count,seat_first_class_starved_count,seat_first_class_deferred_count,lane_reject_first_class_count,lane_reject_other_class_count,lane_reject_unknown_class_count,lane_hold_count,lane_drain_count,lane_drain_fail_count,lane_hold_expired_count,lane_hold_dropped_count,lane_ack_override_count,lane_diag_refused_count,lane_decode_reject_count,freeze_abandoned_count,freeze_missing_local_count`（末尾两字段为 2026-10-11 追加的消费侧丢失原始事实，读者下标不变）。`ok=1` 当且仅当 `reason_mask==0`；判定只由**退化**事实构成（见下表），设计内的背压（`first_class_stage_refused_count`）与推迟计数只作为上下文证据出现、不进入 `reason_mask`。任一来源读不到时**返回错误而不报健康**：宁无数据，不给"看似健康的 0"。 |

`reason_mask` 位定义（符号以 [tdma_pio_spi_ring_adapter.h](../../components/tdma/inc/tdma_pio_spi_ring_adapter.h) 的 `TDMA_SEAT_HEALTH_REASON_*` 为准）：

| 位 | 符号 | 含义 |
|---|---|---|
| 0 | `TDMA_SEAT_HEALTH_REASON_HOLD_DROPPED` | 被拒的一等记录因**有界队列已满**被丢弃（`lane_hold_dropped_count`） |
| 1 | `TDMA_SEAT_HEALTH_REASON_HOLD_EXPIRED` | 记录在**声明的重试预算**内无法补投而被退休（`lane_hold_expired_count`） |
| 2 | `TDMA_SEAT_HEALTH_REASON_ACK_OVERRIDE` | 共享控制字节被一等确认**覆盖**（`lane_ack_override_count`）——准入生效后此位应恒为 0，是"独立通道是否建立"的判据 |
| 3 | `TDMA_SEAT_HEALTH_REASON_WAIT_VIOLATION` | 一等记录等待**达到/超过**服务预算（`seat_first_class_wait_violation_count`） |
| 4 | `TDMA_SEAT_HEALTH_REASON_LANE_REJECT` | 优先级 lane 拒收了一等 class 记录（`lane_reject_first_class_count`） |
| 5 | `TDMA_SEAT_HEALTH_REASON_RETIRED_WAITING` | 已就绪但**未上车即被退休**的一等事实（`first_class_retired_waiting_count`） |
| 6 | `TDMA_SEAT_HEALTH_REASON_FIRST_CLASS_STARVED` | **loop plan 排不下**一等席窗口（`seat_first_class_starved_count`）——挂在一等席上的负载将在每个边界被推迟，绝不允许静默发生 |
| 7 | `TDMA_SEAT_HEALTH_REASON_DECODE_REJECT` | 帧已通过优先级通道校验（header/identity/mailbox CRC）但**紧缩切片无法解码**（`lane_decode_reject_count`）——不可恢复的一等事实丢失，因此与"可恢复背压"分开计数 |
| 8 | `TDMA_SEAT_HEALTH_REASON_FIRST_CLASS_MISSED` | **端到端丢失**：原点在位置窗口关闭后放弃完成该位置的 FREEZE（`freeze_abandoned_count`），或从板被要求冻结一个自己从未拥有的位置（`freeze_missing_local_count`）。前 8 位只证明"席位对**看到过的**记录做了什么"；没有这一位，**从未到达**的一等交付会留下 `ok=1`（假健康），见 `TDMA-PROGRESS-20261011-029` |

**sink 层两类拒绝的分离（2026-10-11 补）。** 从板一等 sink 原先只有一个拒绝计数，混合了两种语义完全不同的情况：① 解码失败（`trigger_position_slice_decode()` 为假）——帧合法但切片不可解，**记录丢失且不可恢复**；② 单深度 mailbox 仍被占用——**有界背压**，记录已进入每源 FIFO 等待后续时间片补投。现在 ② 仍是 `lane_reject_count`（与 `lane_hold/drain/drain_fail` 一起构成可恢复路径的证据），① 独立为 `lane_decode_reject_count` 并进入判定页 bit7。这样"一等席到底丢了多少"不再需要从混合计数里猜。

**loop plan 席位饥饿（2026-10-11 补，2026-10-11 扩展）。** `tdma_loop_plan()` 按优先级顺序（PRIORITY → FIRST_CLASS → LOAD）在环内铺开各席位窗口；当剩余空间排不下时，此前只表现为"该席位没有窗口"，调用方（origin 发布门控）会**无限推迟**挂在该席位上的负载且不留证据。现在：

- `tdma_loop_plan_t.dropped_mask` 显式报告**所有被丢弃的已请求席位**（`TDMA_LOOP_PLAN_DROPPED_*`，`tdma_loop_plan_seat_dropped()` 查询）；
- 门控判定抽成**纯函数** `tdma_loop_plan_seat_service(plan, seat, boundary_sequence, slot_period_ns)`，三种结果：`OK`（本边界属于该席位）/ `DEFER`（席位正确但不是本边界）/ `STARVED`（**计划根本无法服务该席位**）；
- `STARVED` 覆盖两种成因：为空间被丢弃（mask 有对应位）**以及从未被请求**——例如 `TDMA_LOOP_PLAN_LOAD_ENABLED == 0` 时 LOAD 席永远不会进计划，若有人把负载挂到 LOAD 席（挂载表允许并持久化该 seat），今天会**永久静默推迟**，现在计为饥饿；
- origin 在 `STARVED` 时计入 `seat_starved_count`（一等席子集另计 `seat_first_class_starved_count`）并进入 `SEAT:HEALth?` bit6。该映射被抽成 `tdma_pio_spi_ring_origin_seat_gate()`（每次发布尝试重建计划、返回"现在可发/推迟"），因此可被主机 harness 直接抽取执行。

**门控不是现行调度机制（2026-10-11 owner 定案）**：席位调度由 **M:N 轮换（`TDMA-SEAT-01`）**实现——快速通道只有一条，特等与一等分时复用；本节的 loop-plan 窗口门控**不在设计内**，`TDMA_LOOP_SCHEDULE_ENABLED` 保持 **0**。相关代码与饥饿计数保留为**休眠代码**并配构建守卫与行为用例，以免腐烂；不要再把它当作"待启用项"。

**休眠门控的验证（重要）**：该门控位于 `#if TDMA_LOOP_SCHEDULE_ENABLED` 内，而该宏默认为 **0**（`tdma_seat_admission.h`），因此上述饥饿计数在真机上不会增长，上述饥饿计数目前不会增长（`seat_starved_count == 0` 是"未启用"的正常结果，不代表已验证过真机行为）。验证分三层，避免"默认关闭的代码烂掉"：

1. 纯判定：`test_tdma_seat_admission.py` 覆盖 `OK`/`DEFER`/`STARVED`（含"空间不足被丢弃"与"从未被请求（LOAD）"两种 STARVED 成因）；
2. 门控行为：`test_tdma_seat_rotation.py::test_seat_window_gate_counts_starvation_and_defers` 抽取 `tdma_pio_spi_ring_origin_seat_gate()` 实跑——未挂载=放行、PRIORITY 窗口=放行、FIRST_CLASS 非本边界=推迟且**不计数**、FIRST_CLASS 落入窗口=放行、LOAD 席任意边界=**计饥饿且不放行**；
3. 构建守卫：同名文件以 `-DTDMA_LOOP_SCHEDULE_ENABLED=1` 语法编译真实适配器翻译单元必须通过。

**一等席"推迟"也是等待机会（2026-10-11 补）。** 一等席记录被租用后，若硬件发布未完成（`phys_origin.publish()` 返回假），记录会留在席位队列等待下一次机会。此前这种**由发布侧造成的等待**既不推进等待预算也不计数：记录可以无界等待而判定页显示"健康"。现在每次推迟都计入 `seat_first_class_deferred_count`，并**推进同一条声明预算** `TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES`（达到即计一次 `seat_first_class_wait_violation_count`、进入 `SEAT:HEALth?` bit3），成功上车后等待归零、并把最大值写进 `seat_first_class_wait_max`。因此"被特等挤"与"被发布侧拖"两类等待共享同一条有界、可计数的服务口径。

一等席服务事实的语义（2026-10-11 补）：

- **两条声明预算 + 越界计数**（`TDMA-SEAT-03` B）：origin 侧 `TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES` 声明一条一等记录在特等持续上车时可以等待的发布机会数，等待**达到**该预算即把该次等待记一次 `seat_first_class_wait_violation_count`（每次等待只计一次，不是原始最大值）；从板侧 `DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS` 声明保存记录的最大补投次数，超过即退休并计入 `lane_hold_expired_count`。两者都把"无限等待/无限重试"变成**有界且可计数**的事实。

- `seat_first_class_wait_max`：等待中的一等记录**最终上车**时的最大等待机会数；记录在等待期间被 STOP/route 丢失时，其等待值先发布再退休（不再静默丢弃）。
- `first_class_stage_refused_count`：因单深度邮箱仍有等待记录而**被拒绝的 stage 尝试**——即一等席的有界背压事实，不是错误。
- `first_class_retired_waiting_count`：已 READY 但**未上车即被退休**（route 丢失/STOP）的记录数，即交付前损失。

一等记录的完整性由**优先级通道**在交给任何 sink 之前统一校验：`tdma_priority_rx_validate()` 逐帧检查传输帧 magic/version/class/length/identity CRC/transport CRC 以及 mailbox magic/version/class/reference slot/target 位/mailbox CRC16；只有通过校验的记录才会 `publish` 并投递给 VDC 或一等 sink。因此从板侧"不做记录级 CRC 复验"只表示不复验 T2 紧缩切片本身，不表示整条链路缺少完整性门禁。

**一等席接收保全（2026-10-11，`TDMA-SEAT-03` 缺口①/④-RX）。** 从板优先级通道接收端在单深度 mailbox 仍被占用而拒收一条 PREVIEW 时，不再把它丢掉，也不再只保留最新一条：按**每源有界 FIFO（深度 `DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH`，默认 4）**入队，由 T2 相位在**后续时间片按到达顺序**经常规 get 路径补投。队列是**只追加的 SPSC 环**（生产者＝捕获 IRQ，消费者＝T2 相位，同核），因此不会覆盖正在被读取的条目；**队列满时丢弃最新到达并显式计数**，而不是顶掉已有条目。补投成功即出队；同一条记录若先被线上重复帧成功投递，则队列中的副本标记为已消费（不再重复投递）。补投也有界：单条连续失败达到 `DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS` 即退休并计数，然后继续尝试下一条。FREEZE 仍走 latch，不参与该队列。计数在 `SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 尾部：`lane_hold_count`（入队）、`lane_drain_count`（补投成功）、`lane_drain_fail_count`（补投失败）、`lane_hold_expired_count`（超重试预算退休）、`lane_hold_dropped_count`（队列满丢弃）、`lane_hold_delivered_count`（同一记录已由线上重复帧送达而退休的队列副本，2026-10-11 补）。

**队列出口记账必须闭合（2026-10-11，随机化不变量测试发现）。** 出队路径原先只有两条被计数（补投成功、超预算退休），而"同一记录已由线上重复帧送达"这条路径只推进 `head` 而不计数：记录本身没丢，但**队列的账面无法对账**——`lane_hold_count` 减去已知出口会长期高估仍在排队的条目。现在入口与出口满足恒等式

```
lane_hold_count == lane_drain_count + lane_hold_expired_count
                  + lane_hold_delivered_count + 仍在队列的条目数
```

其中"仍在队列"按每源 `tail - head` 求和（`POSition?` 只给逐条计数与 `lane_owned`，不给逐源深度）。该恒等式在**每次投递之后**与每个随机序列结束时都成立，由 `tests/python/test_tdma_seat_invariants.py` 的 24 种子 × 400 次随机投递驱动，以及 `test_t2_freeze_completion.py` 的线上重复用例共同见证。注意 `distributed_refmem_position_priority_drain()` 的返回值只表示**是否补投成功**：退休一个已送达副本会推进队列但返回 false，因此该出口只能由计数而不是返回值证明。

**一等席 TX 侧的关键事实仲裁与准入（2026-10-11，`TDMA-SEAT-03` 缺口④；owner 定案：快速通道唯一、两席分时复用，故本机制是长期机制）。** 冻结完成状态搭在出站镜像的 `CONTROL_OPCODE`/`CONTROL_SEQ8` 两个控制字节上，而 action probe 诊断也会写同样两个字节。规则是**一等优先、诊断准入**：当本板有冻结事实要随本镜像发布时（`distributed_refmem_position_ack_control_available()` 为假），诊断**不得占用**这两个控制字节——它仍写自己的 RefMem 载荷片段，但控制字节被事先拒绝，拒绝次数计入 `lane_diag_refused_count`；镜像构建顺序固定为**先诊断、后一等确认**，因此即使在未预留的镜像里诊断先写了，交付关键的一等事实也会最后写入并覆盖它，每次覆盖计入 `lane_ack_override_count`（同一 opcode 重复写入不算覆盖）。两条计数合起来是完整事实：`lane_diag_refused_count` 表示准入生效，`lane_ack_override_count` 表示准入之外仍发生的覆盖——后者在独立通道建立后应恒为 0。特等席本就有专用 typed body，不存在这种共享。方案与剩余评审项见 [一等席独立 TX 通道方案评审](TDMA_SEAT_TX_CHANNEL_REVIEW.md)。

**优先级通道拒绝的按 class 归因（2026-10-11，`TDMA-SEAT-03` 缺口⑤）。** lane 级拒绝不再只有一个合计值：`SYSTem:TDMA:FLIGHT:PRIORity?` 尾部追加四个字段——`first_class_reject_count`（被拒记录的 mailbox class 等于 T2 position class）、`other_class_reject_count`（已验证但属其他 class）、`unknown_class_reject_count`（传输头未通过校验、class 字节不可信）、`last_reject_class`（原始 class 字节，不可归因时为 `TDMA_PRIORITY_RX_CLASS_UNKNOWN`）。三者之和恒等于 `reject_count`，所以"一等席丢了多少"与"链路整体丢了多少"可以分开核对；被判为 unknown 的记录**不会**被算成一等损失。

比例在 STOP 下配置、由 `ARM` 冻结；比例是**每板**设置：参考节点角色轮换后，由当时承担 origin 的板按本板保存的比例仲裁，因此多板实验必须把同一比例写到所有可能成为 origin 的板，并在实验记录中声明。一等席服务等级与特等席的差距另见 `TDMA-SEAT-03`。

SCPI 负责配置、触发、停止及冻结后读取，不用查询续装实时链路。板端记录先进入有界 RAM，Core0/Storage 在允许的停态落盘；有限记录完成后保留运行和最终 STOP 的边界，不把 SAVE 成功当作采集全程无缺口。

`diagnostics_tdma_record_*` 记录的是若干 owner 快照的读取区间，sample slot 不等于跨板同一物理圈。RUN/RESET/OTHER、准备态和切换区间分别保留；局部耗时下降、历史预算或主机流程时间不能替代当前完整 WCET。

P3 按运行前冻结的 profile 与 `p3_alarm_policy.py` 分级，保留原始 `passed`、`closed_loop_passed`、`realtime_gate_passed` 和 diagnostic 结果。快速验收可为 PASS_WITH_WARNINGS，不因此宣布严格 TDMA 或锁相通过。代码改动绑定源码指纹、Release 和实际四板 P3；纯文档整理执行文档门禁，不重跑硬件。

验证覆盖映射：layout/codec、异步工位、selection 退休、STOP/ARM、priority IRQ/覆盖、origin plan/observer、profile/资源、四板基础运输及逐项专项。证据索引见 [Task Progress](TDMA_TASK_PROGRESS.md)，当前剩余条件见 [TODO](TDMA_DOMAIN_TODO.md)。
