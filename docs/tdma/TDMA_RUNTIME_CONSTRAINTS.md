# TDMA 运行接口细则

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/vdc/VDC_RUNTIME_CONSTRAINTS.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-09-19

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

`TDMA_PRIORITY_TX_READY` 只表示 offer 就绪。发布可能因 DMA selection 未退休或版本变化延后；提供者确认、物理发布、实际送达和 ACK 分开记录。已编码事件保持原生成时模型和身份，不按后来的 DCO 模型重算历史时间戳。

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

## Recovery、训练与业务扩展

Recovery 用 `TDMA_RECOVERY_*` 的独立缓冲、预算和每周期配额，Core0 准备、Core1 选择、PIO 执行；ACK 成功才退休，重试耗尽或无空槽明确背压/失败。它不增加物理节点、不扩展 process image，不承载原始波形或日志。端到端可靠性和多拓扑恢复仍按 TODO 验收。

Calibration 拥有线序、delay、residence、bias、matrix generation/freshness 及接受质量；TDMA 只提供训练 persona、窗口、资源、原始测量和失败原因。复用兼容且已确认的环序不要求重扫全部候选。正式校准门禁与可用诊断输入分别记录。

T2 的 reservation、READY/NACK、fence 与 completion 是拟受控运输的 opaque segment；目标时间和业务状态归 Trigger/VDC，正式 map、配额与多板生命周期尚需集成。另一分支的单板 sequence 测试不是本分支 TDMA 多板预约验收。

## 观测与验收

SCPI 负责配置、触发、停止及冻结后读取，不用查询续装实时链路。板端记录先进入有界 RAM，Core0/Storage 在允许的停态落盘；有限记录完成后保留运行和最终 STOP 的边界，不把 SAVE 成功当作采集全程无缺口。

`diagnostics_tdma_record_*` 记录的是若干 owner 快照的读取区间，sample slot 不等于跨板同一物理圈。RUN/RESET/OTHER、准备态和切换区间分别保留；局部耗时下降、历史预算或主机流程时间不能替代当前完整 WCET。

P3 按运行前冻结的 profile 与 `p3_alarm_policy.py` 分级，保留原始 `passed`、`closed_loop_passed`、`realtime_gate_passed` 和 diagnostic 结果。快速验收可为 PASS_WITH_WARNINGS，不因此宣布严格 TDMA 或锁相通过。代码改动绑定源码指纹、Release 和实际四板 P3；纯文档整理执行文档门禁，不重跑硬件。

验证覆盖映射：layout/codec、异步工位、selection 退休、STOP/ARM、priority IRQ/覆盖、origin plan/observer、profile/资源、四板基础运输及逐项专项。证据索引见 [Task Progress](TDMA_TASK_PROGRESS.md)，当前剩余条件见 [TODO](TDMA_DOMAIN_TODO.md)。
