# TDMA 飞行处理与 PIO 时序冗余评审

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_TIMING_REVIEW.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`
Last updated: 2026-10-09

## 文档接口与范围

本文是 2026-10-09 对 TDMA **飞行处理（Core1 热路径）与 PIO 层**的一次只读评审，目的是找出时序上的冗余与预算水分。它是评审快照，不登记为契约（AGENTS.md 硬约束 6），也不改变 [TODO](TDMA_DOMAIN_TODO.md) 状态；采纳的项须转为 TODO 任务，实测结果写入 [Task Progress](TDMA_TASK_PROGRESS.md)。

- 基线：分支 `wip/tdma-real-flight-processing`，HEAD `ef64b5bd` 加评审当日未提交工作区。文件:行号以该工作树为准，代码变动后可能漂移。
- 范围：`components/tdma/` 的 `.pio` 程序、`tdma_pio_spi_phys*`、`tdma_flight_*`、`tdma_pio_spi_ring_origin.inc`、adapter 飞行与 RX 接受路径，`config/project_config.h` 与 `application/src/app.c` 中的 Core1 相位表和调度等待，以及 origin cadence/guard。Core0 准备工位只在迁移边界处涉及。
- 方法：读码、静态估算，以及读取已有构建 map 与 `out/HardwareAcceptance/20261008` 验收原件；**本次未构建、未上板、未新增实测**。耗时按 `BOARD_SYS_CLOCK_HZ` 折算；文中所有数值均为**估算快照，非事实源**，采纳前须用同源码 P3/`PROFile?` 分段计时复核。
- 核实等级：`已核实` = 评审主控复读代码确认；`读码` = 子评审读码给出、主控未逐条复读；`推测` = 推断，无代码或实测闭合。

## 复核与修改状态（2026-10-09 复核）

本文是评审稿；下表跟踪"结论是否被复核"与"是否已按结论修改代码/配置"。**复核**取值：`待定`（尚未逐条复核）／`正确`（复核后结论成立）／`需更正`（复核后结论需改写）／`部分正确`。**修改**取值：`未修改`／`已修改`（须附提交或证据入口）／`不适用`（纯观察项）。

| ID | 复核 | 修改 | 备注 |
|---|---|---|---|
| R-01 | **正确** | 未修改 | 逐项核对 `tdma_event_observer.h` 的 `tdma_event_reason_t`：`ORDINAL=8`、**`SEQUENCE=9`**；`TDMA_GEOMETRY_OBSERVER_FAULT` 确在 `tdma_frozen_geometry.h`（另一枚举）。⇒ 010 条目的根因结论待更正。 |
| R-02 | **正确** | 未修改 | `tdma_seat_admission.h:196-197` 默认值确为 **1**。待决：恢复为 0，或按 `EXE-SEAT-01` 先交预算清单。 |
| T-01 | 待定 | 未修改 | 需 PIO 准入公式代入 `BOARD_SYS_CLOCK_HZ` 复核各档。 |
| T-02 | 待定 | 未修改 | 按 `config/project_config.h` 配额符号算术复核。 |
| T-03 | 待定 | 未修改 | 需 `TDMA_SERVICE_TIMING_ENABLED=0` 对比构建量化。 |
| T-04 | 待定 | 未修改 | 复核两处调用点与既有测试覆盖缺口。 |
| T-05 | 待定 | 未修改 | 需 HIL 扫描验证末字节 CRC 后再改 guard。 |
| T-06 | 待定 | 未修改 | 建议改为只比 map 代号。 |
| T-07 | 待定 | 未修改 | 先去重、CRC 只保留一处（至少留一处 Core1 执行）。 |
| T-08 | **正确** | 未修改 | `tdma_seat_mount_store.c`：`s_mount` 为普通 static，`get()` 第 153 行、`save()` 第 216 行均无 seqlock/原子保护 ⇒ 跨核撕裂窗口成立；修法：seqlock 或 ARM 边界冻结 Core1 私有副本。 |
| T-09 | 待定 | 未修改 | 读码项，需先补分段计时。 |
| T-10 | 待定 | 未修改 | 恢复路径实测峰值远高于指令量估算，需分段计时定位。 |
| T-11 | 待定 | 未修改 | 含一处死拷贝（`accepted_payload` 无调用者），可优先处理。 |
| T-12 | 待定 | 未修改 | 建议直接用拍计数接口，去掉 µs→拍换算。 |
| G-01 / G-01a | 待定 | 未修改 | G-01a（`cycle_period_ns` 两条路径语义不一致）可读码复核，且与 R-01 一并排查。 |
| S-01 | 待定 | 未修改 | 相位表与三处等待可按符号算术复核。 |
| B-01 / B-01a | 待定 | 未修改 | B-01a 口径问题可在 `tools/tdma_ring_monitor/tdma_service_timing.py` 复核。 |
| B-01b | 待定 | 未修改 | 可用 `out/build/dpll60-latest/DHRT100.elf.map` 复核 XIP 地址，是本轮最大候选。 |
| B-01c | 待定 | 未修改 | 含死拷贝一项，可与 T-11 合并处理。 |

复核进度口径：上表"正确"项为本次已逐条读码核对；其余为评审原稿结论，**未复核前不得作为收益或根因证据**（与本文开头"估算快照，非事实源"一致）。修订本表即修订 `Last updated`。

## 一、优先处理（非时序水分，但影响当前结论）

### R-01：DPLL60 短槽失败的根因读数被误映射（已核实）

`TDMA-PROGRESS-20261009-010` 把 `SYSTem:TDMA:EVENT:Recovery?` 的 `last_failure_reason=9` 解读为 `TDMA_GEOMETRY_OBSERVER_FAULT`。该字段由 `tdma_event_recovery_record_failure()` 写入 `r->last_failure_reason = o->reason`（`tdma_pio_spi_phys_event.inc:1510`），类型是 `tdma_event_reason_t`（`tdma_event_observer.h`），其中 9 对应 `TDMA_EVENT_SEQUENCE`。

- 恢复只在 `o->reason == TDMA_EVENT_SEQUENCE` 时置 `RESET_PENDING`（`tdma_pio_spi_phys_event.inc:1520`）。
- `TDMA_GEOMETRY_OBSERVER_FAULT`（start-cut `RETIRED`，`tdma_pio_spi_phys_event.inc:1899-1901`）属于另一个枚举 `tdma_frozen_geometry.h`，不经该字段上报。
- SEQUENCE 失败的条件是观察者要求 wire sequence 严格逐帧加一（`tdma_event_observer.c:282-285`）。

结论：短槽下反复恢复的直接原因是**观察者帧序号跳号**，start-cut 退役不是已证实根因。需要更正 010 条目并重新评估修复方向。

相关疑点（推测）：`tdma_rx_sequence.c:38-40` 判定 DMA 计数“丢失”时按每拍可写一字假设上界，相对 10 MHz 线速约保守两个数量级，可能把正常推进误判为 epoch 变化。建议在 ARM 时按 baud 推导该上界后复核。

### R-02：窗口门默认值在工作区被打开（已核实）

未提交的 `components/tdma/inc/tdma_seat_admission.h:197` 把 `TDMA_LOOP_SCHEDULE_ENABLED` 默认由 0 改为 1，与 `TDMA-PROGRESS-20261009-011`“同档 A/B 开启略差、保持默认关闭”的结论相反。若为调试改动，提交前应恢复为 0；若有意开启，须先按 `EXE-SEAT-01` 提交预算清单并审核。

## 二、时序冗余与水分（按估计收益排序）

| ID | 问题 | 位置 | 估算（快照，非事实源） | 等级 |
|---|---|---|---|---|
| T-01 | PIO 跟随者字节尾重整周期决定速率上限 | `tdma_pio_spi.pio:132-136`、`tdma_pio_spi_phys.c:2367-2386` | 25 MHz 档线上时间约为 10 MHz 档的 40% | 已核实 |
| T-02 | 优先 RX IRQ 静态预留按 7 相位固定分配 | `config/project_config.h:53-67` | 约占 1.5 ms 表周期的 16% | 已核实（公式） |
| T-03 | 服务计时探针在设备构建默认常开 | `tdma_service_timing.h` | 每相位数 µs 至数十 µs，口径待测 | 已核实（开关） |
| T-04 | 事件观察者每个 TDMA 相位服务两次 | `application/src/app.c:1132`、`tdma_pio_spi_phys_flight_io.inc:142` | 每相位约 6–12 µs | 已核实（调用链） |
| T-05 | 帧尾 tail bytes 三层余量叠加 | `tdma_pio_spi_phys.c:561-575` | 10 MHz 下每帧约 2 µs 线上时间 | 已核实 |
| T-06 | process image map 每帧整表复制 | `tdma_flight_engine.c:236` | 每帧约 2 µs | 已核实 |
| T-07 | origin 发布对同一 mailbox 重复 CRC，且先 CRC 后去重 | `tdma_pio_spi_ring_origin.inc:408-413`、`tdma_pio_spi_phys_origin.inc:797` | 每周期 1–2 µs | 已核实 |
| T-08 | 窗口门每次发布重算常量计划、复制挂载表 | `tdma_pio_spi_ring_origin.inc:362-381, 475-480` | 每次发布 1–2 µs，最多两次 | 已核实 |
| T-09 | origin 每帧重建 TX 缓冲并重配 DMA | `tdma_pio_spi_phys_flight_io.inc:50-73` | 每帧 8–12 µs | 读码 |
| T-10 | 观察者恢复每次整套重建 SM/DMA | `tdma_pio_spi_phys_event.inc:1662-1707, 1955-1965` | 指令量 12–20 µs；实测峰值远大于此，构成未明 | 读码 |
| T-11 | RX 帧多余整帧复制 | `tdma_pio_spi_ring_adapter.c:2311`、`tdma_pio_spi_phys_flight_io.inc:377` | 每帧约 1 µs | 读码 |
| T-12 | 观察者用 µs 时间基再换算为拍 | `tdma_pio_spi_phys_event.inc:1457-1466, 2059-2084` | 每次 0.5–0.8k 拍，分辨率损失 | 读码 |

### T-01：PIO 跟随者字节尾重整周期

准入公式要求 `max(D + TDMA_PIO_SPI_PROCESS_DATA_DECODE_CYCLES, ⌈P/2⌉) + TDMA_PIO_SPI_PROCESS_BYTE_REARM_CYCLES ≤ P`（P 为每 bit 的 sys 拍数）；raw 跟随者使用 `TDMA_PIO_SPI_RAW_BYTE_REARM_CYCLES`。按当前 `BOARD_SYS_CLOCK_HZ`，`s_tdma_operating_profiles` 中高于 10 MHz 的档位在 process 跟随者上不开 `DIAGNOSTIC_CONTINUE` 均无法通过准入，即这些档位目前只在表中存在。

第八 bit 下降沿后的尾部为 `mov x,isr; push noblock; mov isr,x; in null,24`，用于推送时保留 ISR。可先把部分指令前移到下降沿之前以省 1–2 拍，但要达到 25 MHz 需要重新设计 ISR 保留方式（如 RP2350 `mov rxfifo[]`/FJOIN_RX_PUT）。改动影响 token/字节对齐，需重新训练并跑四板 P3。这是收益最大、改动也最大的一项。

相关项（读码）：

- origin 时钟固定 `TDMA_ORIGIN_CONTROL_CYCLES_PER_BIT` 拍/bit 且使用小数 clkdiv（`tdma_pio_spi_clkdiv_for_baud`），带来边沿抖动并限制最高档；可改为 clkdiv=1 加按 baud 修补延时。在 T-01 解决前不单独提升吞吐。
- cadence guard `jmp y-- guard [31]`（`tdma_origin.pio:19`）的量化粒度随 clkdiv 放大，与上项一并处理。
- `flight_origin_data_tx`（`tdma_pio_spi.pio:963-970`）中 OUT 之后的延时修补的注释提到“before IN”，但程序中没有 IN，疑为复制残留；需确认是否作为保持时间 guard，再改为显式的 OUT 前延时。
- `fd_master/fd_slave/directional_*` 已汇编但 `tdma_pio_spi_phys_programs.c` 中无加载点，仅占 flash，无运行时代价。

### T-02：优先 RX IRQ 静态预留

`PROJECT_CORE1_PRIORITY_RX_RESERVATION_CYCLES = 7 × (BASE_QUOTA × IRQ_CYCLES + CLOSE_CYCLES)`，按 1.5 ms 表周期的帧边界数看，配额远多于物理可能的 IRQ 次数。子评审称环周期低于 `PRIORITY_RX_MIN_PHYSICAL_CYCLES` 时该通道以 CADENCE 被拒（`tdma_pio_spi_phys_priority.inc:277-284`），此时预留整块空转（读码，未复核）。

同时单 IRQ 预算 `PROJECT_CORE1_PRIORITY_RX_IRQ_CYCLES` 低于历史实测的 ISR body 峰值（见 `LEGACY_VDC_TASK_PROGRESS_02`），即**总量偏松、单次偏紧**。建议按 `⌈cycle / min_physical⌉ + 1` 分配全局配额、单次预算按实测设定，通道被拒时安装无预留相位表。涉及 `project_config.h`/`app_realtime_profile` 静态断言，需评审。

### T-03：服务计时探针常开

`TDMA_SERVICE_TIMING_ENABLED` 在 `PICO_ON_DEVICE` 下默认 1。phys 层与 adapter 层探针点合计上百个，每点含一次 64 位 TIMER1 读和一次累加，且有嵌套计时及同一 start 重复 record（如 `tdma_pio_spi_phys_rx_scan.inc:151-152`）。

探针是 WCET 分类证据来源（`TDMA_RUNTIME_CONSTRAINTS.md`），不应直接删除。建议先出一个 `ENABLED=0` 构建对比 `PROFile?`，量出探针自身开销，再决定保留粒度；重复 record 可先合并。

### T-04：事件观察者每相位两次

`app_realtime_tdma_phase()` 先调 `tdma_component_core1_service()` → `tdma_runtime_owner_service_phys_tx()` → `tdma_pio_spi_phys_service_tx()`，其中第一行即 `tdma_pio_spi_phys_event_service()`；返回后 `app.c:1132` 又经 `tdma_runtime_owner_service_observer()` 调一次。两处注释都声称“每相位一次”。`tests/python/test_no5_tdma_isolation.py` 只统计 `tdma_runtime_owner_service_observer();` 的出现次数，覆盖不到 `service_tx` 内的调用。

副作用：观察者恢复状态机（“one reset transition per existing owner phase”）每相位推进两步。建议保留一个调用点（blackout 分支的单独收割保留），并补测试。

### T-05：帧尾 tail bytes 余量叠加

`tdma_pio_spi_phys_flight_tail_bytes()` 依次做 loop_delay ns→bit 向上取整、bit→byte 向上取整，再加固定 2 个 guard 字节。注释理由是返回 CS/SCK 相位与末字节 DATA 传播，而这两项受相位延时上限约束，远小于 2 字节。未找到实测依据。建议改为 bit 粒度计算、只取整一次，guard 降至 1 字节，并用 HIL 扫描验证末字节 CRC。

### T-06：map 整表复制

`tdma_flight_engine_read_map()` 在 seqlock 内执行 `*map = engine->map`，有 6 个调用点；单帧路径上 `inspect_input` 与 `commit_input` 至少各复制一次，参考端 `tx_load_impl` 还有一次。map 在激活后不可变（`configure` 在 active 时拒绝），架构要求的是复验 map **代**而非整表。建议在 `activate` 时预算远端段列表，热路径只做 seqlock 首尾序号与 generation 比较。

### T-07：mailbox CRC 重复

- 同步路径：`tdma_pio_spi_ring_origin_mailbox_valid()` 验一次，`tdma_pio_spi_phys_origin_publish()` 内 `tdma_pio_spi_phys_origin_mailbox_valid()` 再验一次。
- priority 路径：先 CRC（`ring_origin.inc:408`），再与 `origin_shadow` memcmp 去重（`:410-413`）；provider 常返回重复缓存内容，CRC 白算。

建议先 memcmp，相同即返回；CRC 只在一层保留（`commit_tx` 已信任调用方）。保留至少一处由 Core1 执行的 mailbox CRC。

### T-08：窗口门每次发布重算（含一项正确性问题）

`TDMA_LOOP_SCHEDULE_ENABLED` 下，每次发布都调用 `tdma_loop_plan_default_request()` 与 `tdma_loop_plan()`，输入为常量；`tdma_loop_plan_slot_for_index()` 含 64 位取模；`tdma_pio_spi_ring_origin_load_seat()` 每次整表复制挂载表再线性查找；priority 被拦后同步路径对同一 sequence 再算一次。相关函数不在 RAM。建议 ARM 时算好计划并预建槽类查找表，挂载映射在 ARM 时解析为每个 class 的结果。

**正确性**：`tdma_seat_mount_store_save()` 由 Core0 直接写 `s_mount = *table`，Core1 经 `tdma_seat_mount_store_get()` 无保护读取，可能读到撕裂表。须用 seqlock 或在 STOP/ARM 边界冻结一份 Core1 私有副本。

### T-09 至 T-12（读码）

- **T-09**：`FLIGHT_ORIGIN` 每帧逐字重写 TX 扩展缓冲（帧头、尾填充不变）并调用 `dma_channel_get_default_config`/`dma_channel_configure`，配置 ARM 后已固定。建议 ARM 时预算 ctrl，帧头尾常驻，每帧只写 payload 后重触发。
- **T-10**：恢复每次 claim/unclaim DMA 通道、`pio_sm_init` 与多次 `pio_sm_exec`，ARM 后真正变化的只有种子、FIFO 与 DMA 计数。实测 `service_max_us` 峰值（`TDMA-PROGRESS-20261009-010`）远高于指令量估算，多出部分推测来自 DMA 停止轮询（`tdma_pio_spi_phys.c:656-677`）或其他 IRQ。先给恢复路径加分段计时再优化。
- **T-11**：自主 origin 模式下 `last_rx_packet` 无消费者；`job->packet` 中转可由 `rx_ring_copy` 直接写入省去。`accepted_payload` 与 FIFO RX 槽同一份数据供两个 Core0 消费者，合并属契约变更。
- **T-12**：观察者区间由 `time_us_64` 得到再经 64 位除法换算为拍，TIMER1 本身按拍计数；建议直接用 `vdc_timestamp_clock_read_ticks64`。

### 小额重复（读码，单项通常 <1 µs）

- schedule/profile CRC 在 Core0 prepare、`process_rx_impl`、`origin_header_valid`、receive_health 共比较 4 次。
- READY 分支在 `accept_tx` 前重复检查 active/slot/payload_size/map_generation。
- `commit_input` 对每段重跑 `fast_mailbox_header`，inspect 已在同批字节上做过。
- inspect/commit/`tx_load_impl`/`expected_input_mask` 按 `TDMA_PROCESS_IMAGE_SEGMENT_COUNT` 循环而非实际段数；`expected_owner_mask` 每帧遍历拓扑，结果 ARM 后固定。
- Core1 单写者的 `rx_bitmap_*` 与 FIFO 计数使用原子读改写/RELEASE；可本地累加、每帧提交一次。
- `tx_load_impl`/`build_tx` 逐字节置 bitmap，段 32 字节对齐，可整字写。
- priority ISR 写多次 watchdog 面包屑，并调用 flash 中的 SDK `irq_set_enabled()`；identity 与 transport header 两次 CRC32 覆盖范围高度重叠（协议语义，改前须评审）。
- 多个 Core1 热路径函数未放入 RAM（`process_rx_impl`、`tdma_flight_fifo.c`、`tdma_transport_frame_decode`、`seat_admission`、`seat_mount_store`、origin publish 等），XIP 代价待查 map 文件。
- 非 resident 的 `tx_beacon` 每帧逐位 LFSR 生成常量对齐码字（`tdma_flight_engine.c:37`），可做成常量表；参考端 resident 周期对同一 packet 解码三次并多次整 payload 往返复制。两者均为过渡态。

### 仅维护路径（NORMAL persona，flight 不经过）

- `tdma_pio_spi_phys_frame_tail_us()` 固定按 10 个 word 加 10 µs 计算，调用处 `tdma_pio_spi_phys_flight_io.inc:288` 为 `busy_wait_us_32`；在途上限为 TX FIFO 加 OSR，且仓库已有 `tdma_pio_spi_phys_txstall_mask()`，可改为轮询 TXSTALL。
- `TDMA_PIO_SPI_TX_PUT_TIMEOUT_US` 按字计，整帧最坏为字数倍；建议改为按帧单一截止。
- `TDMA_PIO_SPI_RX_CSN_IDLE_WAIT_US` 已定义无引用。
- origin TX 截止为线时加固定 1 ms（`tdma_pio_spi_phys_flight_io.inc:120-124`），短于 1 ms 的周期下卡住的发射要跨多个周期才恢复；建议取 `min(1 ms, 周期)`。

## 三、结构性空闲与预算缺口（补充评审）

第二节各项合计只有数十 µs。本节审查三块更大的时间：线上固定 guard、Core1 时间触发调度的空转，以及 TDMA 相位实测峰值超出服务预算的缺口。结论是三者**不能各自独立优化**：线上空闲不是瓶颈，Core1 才是；而 Core1 超时的主因推测是 XIP 取指，不是计算量。

### G-01：固定 cadence guard 线上空闲

**推导（已核实，读码）**：

- 物理字节 = `TDMA_PIO_SPI_PACKET_HEADER_SIZE` + `TDMA_TRANSPORT_FRAME_HEADER_SIZE` + `tdma_flight_payload_size(N)` + `tdma_pio_spi_phys_flight_tail_bytes()`（`tdma_pio_spi_phys.c:2405-2410`）。
- `tdma_origin_cadence_calculate()`（`tdma_origin_plan.c:641-663`）：`fixed_cycles = bytes × 8 × TDMA_ORIGIN_CONTROL_CYCLES_PER_BIT + TDMA_ORIGIN_CONTROL_CYCLE_OVERHEAD`，guard = `⌈(required − fixed) / TDMA_ORIGIN_CONTROL_GUARD_CYCLES⌉`；PIO 侧为 `jmp y-- guard [31]`。
- 送入的周期就是 `config->cycle_period_ns`（`tdma_pio_spi_phys_origin.inc:517-519`）。

四节点、10 MHz 下的估算（快照，非事实源）：

| 档位 | 周期 | 线上时间 | guard | guard 占比 |
|---|---|---|---|---|
| 7 | 1 ms | ≈139 µs | ≈861 µs | ≈86% |
| 22 | 750 µs | ≈139 µs | ≈611 µs | ≈81% |
| 21 | 500 µs | ≈139 µs | ≈361 µs | ≈72% |
| 20 | 375 µs | ≈139 µs | ≈236 µs | ≈63% |

**发现 G-01a：两条路径对 `cycle_period_ns` 的解释不一致（已核实）。**

- 档位表注释（`tdma_operating_profile.c:44-47`）称 level 20/21 是“每节点槽长”，四节点一圈对齐 1.5 ms 表或 2 ms，“reference emits once per revolution”。
- 普通周期路径确实按一圈计：`feedback_timeout_ns = cycle_period_ns × node_count`（`tdma_service.c:527-529`）。
- 自主 origin 路径却把 `cycle_period_ns` 直接作为**每帧发车周期**（见上）。level 20 在自主模式下每 375 µs 发一帧，而不是每 1.5 ms 一帧。

这会影响 VDC/DPLL 按 `schedule.period_ns` 做的同余准入，可能与短槽档 DPLL60 失败（R-01）相关，需与 R-01 一并排查。

**guard 实际保护的内容**：

| 对象 | 判断 |
|---|---|
| 返回 CS / 环路延迟 | 已由 tail 字节在线上时间内覆盖，guard 内需求为 0（已核实）。 |
| DMA 边界图（取回 RTT、搬 bank、identity/route、mailbox CRC、写 record、重装 SM/DMA、发车字） | 推测需几十 µs；目前没有对应的代码符号或实测。 |
| 迟到供给 | 准入只校验 `guard_floor_ticks > rearm_budget_ticks`，该预算由主机工具传入（如 `tools/.../trn03_closed_loop.py`），不是代码符号（已核实）。 |
| Core1 发布 / RX 解析 | 双 bank 加 `local_shadow` 异步选择，Core1 不在发车关键路径上（已核实）。 |
| 与 Core1 1.5 ms 表对齐 | 只是名义上的整数倍；cadence 计算不读 Core1 表，两者持续相对漂移。guard 粒度还会让实际周期略长于请求值（375 µs 档约千分之一量级）。 |

**是否是水分**：线上确实是真空闲（CS 高、SCK 静止）。但缩短 guard **不会增加有效吞吐**：Core1 稳态一个相位就超出预算（见 B-01），实测 `published/boundary` 显示短档下多数帧只是透传旧映像。缩短 guard 只会增加重复帧，并让 record 环更快被覆盖。真正需要的 guard 推测只有几十 µs，但它在系统层面不是瓶颈。

**与 `TDMA-EMISSIONCLOCK-01` 的符合性**（登记状态 pending）：

| 要求 | 普通路径 | 自主路径 |
|---|---|---|
| Core1 不作发车门控 | 不符合（由 Core1 截止时间发车） | 符合 |
| 环边界硬件事件驱动 | — | 部分符合：触发点是本端 TX 结束，之后用固定计时器；返回事件只被记录，与发车无因果关系 |
| 可达下界以代码符号声明、低于下界 fail closed | — | 部分符合：只拒绝“周期 ≤ 线上时间”和“guard ≤ 主机预算”，缺 DMA 图 WCET 与 follower 帧间下界的代码符号 |

事件驱动发车的设想（推测）：去掉计时 guard，control SM 在 `pull block` 等待由边界图写入的发车字，最小间隔为 `线上时间 + T_graph_WCET + T_follower_rearm`。风险包括：DMA/总线争用带来抖动；失去迟到 fault；VDC 同余准入要随之改变；record 环在短间隔下会被更快覆盖。

**建议**：

1. 无需改契约：
   - 不为吞吐去压缩 guard，按“VDC 周期等于发车周期”选档。
   - 修正 level 20–22 的注释，或对自主模式下这几档 fail closed。
   - 用 record `raw_time` 实测边界图耗时，把下界改成代码符号（如 `TDMA_ORIGIN_GRAPH_WCET_TICKS`）参与准入；这是在实现既有要求，状态变更仍走 C11。
   - guard 末尾加细粒度余数延时，消除周期量化误差（PIO 改动，需 P3）。
2. 需 C11：改为事件驱动或最小间隔发车（涉及 `TDMA-EMISSIONCLOCK-01`、`TDMA-RESIDENT-01`、DET-02 与 VDC 同余准入）；统一 `cycle_period_ns` 的语义（会影响 profile/schedule CRC）。

### S-01：时间触发调度空转

**相位表（已核实，读码）**：1.5 ms 表（`PROJECT_CORE1_PROFILE_1500US_CYCLES`）依次为 TDMA、VDC、DPLL、CALIBRATION、SYNC_CAPTURE、REFMEM、MODEL、SYNC_TRIGGER、TRIGGER_MEASURE、GUARD，宽度合计等于表周期（`config/project_config.h:41-155`，断言见 `application/src/app_runtime.c`、`app_realtime_profile.c`）。TDMA 相位前台 WCET 为 `PROJECT_CORE1_TDMA_BACKGROUND_WCET_CYCLES + PROJECT_CORE1_PRIORITY_RX_PHASE_CYCLES`，即进展记录中的“632 µs 服务预算”；相位宽度另含关窗预留与入口余量。长档（5/10/15 ms）只加宽 TDMA。

前台可执行预算约占表周期的 79%。其余约 21% 由 IRQ 预留、关窗预留、相位余量和 GUARD 组成，前台永远用不上（按符号算术，快照，非事实源）。

**三处等待（已核实）**：

1. 每个相位起点 `app_realtime_wait_until()` 自旋（`application/src/app.c:767-773, 924`），此时优先 RX IRQ 已关。
2. 相位 eligible（优先 RX 通道激活）时等待到 `close_cycle`（`app.c:1012-1024`）。窗口保持打开，配额内的优先 RX ISR 在自旋中被服务，这是自旋期间唯一的有用工作。
3. 整表发车 `busy_wait_until`（`app_runtime.c:192-197`），注释说明不用 SDK 的 WFE 路径，以免推迟发车。

**这些等待是否必需**：

- close 等待的作用是计量归属。即使取消，这段时间也会被下一相位的起点等待吃掉，单独取消回收不到时间（已核实）。
- 真正必需的只有两点：整表节拍有界；TDMA 先于 VDC/DPLL。相位起点固定偏移主要服务于预算隔离（推测）。理由是 DPLL 用硬件 latch，SYNC_IO 输出靠 PIO 加提前提交，`TDMA-EMISSIONCLOCK-01` 禁止 Core1 门控发车。

**空转规模（推测，快照）**：分布呈双峰。

- **环停或安静时**：TDMA 相位体实测约百 µs，对比约 666 µs 的相位宽度，一表内一半以上 Core1 时间在空转。
- **环运行稳态时**：TDMA 相位体超出自身窗口（B-01），后续相位迟到或被跳过，主要矛盾是**超时**而非空转。
- 其余相位中，只有 DPLL、REFMEM 有实测，且 DPLL 余量很小；VDC、CALIBRATION、SYNC_CAPTURE、SYNC_TRIGGER 没有 WCET 实测。

**边界保护冗余（推测）**：同一个 eligible 相位的结束边界同时受四层保护：WCET 内的 IRQ 配额、关窗预留里的一个整 ISR（`PROJECT_CORE1_PRIORITY_RX_IRQ_CYCLES`）加 `CLOSE_MARGIN`、相位入口余量、表末 GUARD。关窗预留中的那个 ISR 必然属于配额之一：它要么在服务期间触发，已计入 WCET；要么在等待期间触发，而等待期本来就是空转。所以同一个 ISR 在墙钟上被预留了两次，每表约 7 × `PROJECT_CORE1_PRIORITY_RX_IRQ_CYCLES`。入口余量与 `CLOSE_MARGIN` 也都在防控制抖动，有重叠。这与 T-02“总量偏松、单次偏紧”是同一问题的两面。

**回收方案（按收益与风险排序）**：

| # | 方案 | 性质 |
|---|---|---|
| 1 | T-02：IRQ 配额按全局帧数分配，通道被拒时装无预留表 | 配置/断言，需评审 |
| 2 | 去掉关窗预留中重复的 ISR | 配置，需评审；风险 `close_missed` |
| 3 | 先实测 VDC/CALIBRATION/SYNC_CAPTURE/SYNC_TRIGGER 的 WCET，再按实测加余量缩小，让给 TDMA | 配置；需先补证据 |
| 4 | 拆分 TDMA 相位体，把 parse/handoff/origin_publish 移入后段或空闲段 | 契约变更（DET-03、相位顺序）；VDC/DPLL 要么拿同表快照，要么接受滞后一表 |
| 5 | 同表内把余量捐给尽力而为的后台片段（执行前复验片段 WCET ≤ 距 close 剩余） | 违反“空闲不借给其他相位”（`project_config.h:37`）与 DET-03，属模型级契约变更 |
| 6 | WFE 低功耗等待 | 吞吐零收益；需另配唤醒源和抖动证据 |

`app_realtime_wait_until()` 没有 RAM 属性，若未内联，自旋会在 XIP 上执行（推测，影响很小）。

### B-01：TDMA 相位实测峰值超出服务预算

稳态峰值（`TDMA-PROGRESS-20261008-002`，约 1155–1210 µs）约为服务预算的 1.8–1.9 倍。下列数字均出自 `out/HardwareAcceptance/20261008/dpll60-*/tdma-process-image/summary.json` 的 `service_timing.steady`，为快照，非事实源。

**稳态相位分两种，交替出现**：

- **接受相位**：`rx_parse` 约 300–370 µs，其中 inspect/health/evidence/fifo_publish/commit/complete 有子探针，另有约 90–160 µs **不在任何子探针下**。
- **捕获相位**：`rx_capture` 约 150–220 µs，`rx_request` 约 60–135 µs。

**B-01a：测量口径重复计数（已核实）**。`tools/tdma_ring_monitor/tdma_service_timing.py:64-68` 把 `rx_handoff`、`rx_request`、`rx_local_tx_edge`、`rx_request_publish` 并列为 RX 根。但 `rx_request` 记录于 `tdma_pio_spi_ring_adapter_rx_once_impl()` 内（`tdma_pio_spi_ring_adapter.c:2545-2555`），而后者被 `RX_HANDOFF` 包住（`:2588-2593`）；`rx_local_tx_edge`、`rx_request_publish` 又嵌在 `rx_request` 内（`tdma_pio_spi_ring_rx_prepare.inc:105-120`）。捕获相位的 RX 因此被多算，出现过 attributed 大于 top-level 的逻辑矛盾。进展记录中“follower RX 约 749–839 µs”需要重算，修正后的缺口会小于原口径。

**B-01b：Core1 RX 热路径在 XIP flash 上执行（已核实位置；影响为推测）**。

- `out/build/dpll60-latest/DHRT100.elf.map` 显示，`tdma_pio_spi_ring_adapter_process_rx_impl` 位于 0x100b9ec0（flash，约 4.4 KB），newlib `memcpy` 位于 0x10119c5c（flash，未 wrap 到 RAM）。
- 子评审报告 `rx_once`/`rx_accept`、`tdma_receive_health_evaluate`、`tdma_flight_fifo_core1_publish_rx`、`correlate_dpll_observation`、`tdma_pio_spi_phys_rx_event_query`、`tdma_event_history_lookup` 等也在 flash（读码）。即便调用方放在 RAM，结构体或 payload 拷贝仍要回到 flash 取指。
- 旁证（推测）：
  - `rx_request_publish` 只有十余条指令加一次原子写，实测约 10 µs。
  - `rx_health`、`rx_evidence` 的实测分别比指令量估算大一个数量级。
  - 放在 SRAM 的 `tdma_pio_spi_phys_rx_ring_copy` 实测与理论值相当。
- 推算每次 XIP miss 约在亚 µs 量级，再叠加 Core0 争用，整个 TDMA 相位受影响约数百 µs，**数量级上能解释缺口**。这也解释了此前多轮微优化“未证明完整 phase 改善”的现象。

**B-01c：RX 路径中可删的计算（读码，合计约 40–90 µs，单独解释不了缺口）**：

| 项 | 位置 | 判断 |
|---|---|---|
| RX_PARSE 内未插桩的诊断查询 | `tdma_pio_spi_ring_rx_prepare.inc:20-31` 调 `phys_rx_event_query`：16 项 history 线性查找，必要时整份 snapshot 复制 | 注释写明纯诊断、不影响准入；可移到 Core0 或相位外，先补探针 |
| RX_EVIDENCE | `tdma_pio_spi_ring_adapter.c:2030-2235`，`correlate_dpll_observation` 含 64 位取模（flash 中的 libgcc） | 计算量小，主要受 B-01b 拖累；相位换算可交 Core0，TX latch 配对须留 Core1 |
| `accepted_payload` 整段复制 | `tdma_receive_health.c:253` | 唯一读取方 `tdma_pio_spi_ring_adapter_read_accepted_image()` 在 components/middleware/application/tests 中无调用者（已核实），是死拷贝 |
| FIFO RX 槽复制 | `tdma_flight_fifo.c:413` | 交给 Core0 的必要副本；零拷贝需让 job 生命周期跨相位，风险中 |

**已排除（读码）**：接受路径上 Core1 不算 transport CRC、不重复 decode，decode 只在 Core0 的 `tdma_rx_prepare.c` 做一次；每节点循环有界；`%8`/`%4` 编译为移位。

**Core0 迁移边界**：`event_query`、health 拒绝原因预判、每段 present/seq16 预提取（可替代 Core1 整表 map 复制）、DPLL 相位换算可以移到 Core0（推测）。必须留在 Core1 的是：`rx_accept` 的 epoch/map/config/年龄复验、序号单调与 health 提交、FIFO 发布成功后的新鲜度提交、TX latch 排空与配对、`comm_fsm` 与 resident 完成状态、origin 配对接受（依据架构“普通过程映像”一节与 `tdma_service_timing.h`）。

**定位缺口所需的测量**：

1. 每相位首尾读 `XIP_CTRL` 的 CTR_ACC/CTR_HIT，得到每相位 miss 数（计数器两核共享）。
2. 运行期读 QMI M0 的 `timing`/`rcmd`，确认是否 quad 模式及 clkdiv。
3. A/B：把 `rx_once`、`rx_accept`、`process_rx_impl`、health、fifo_publish、correlate、`rx_request` 和一份 RAM 版 memcpy 放入 SRAM（约 9 KB，须先过 RAM 门禁）；或在 TDMA 相位期间让 Core0 在 RAM 中自旋，对比 `steady_peak`。
4. 给 `event_query` 与 `process_rx_impl` 开头补探针，并先修正 B-01a 的解析口径。

### 其他受约束项

| 项 | 为何不能直接裁剪 |
|---|---|
| 每帧 Core1 复验 epoch/map/长度 | 架构要求（Core0 准备、Core1 复验）；本文只建议去掉整表复制与重复 CRC，保留代号与长度比较。 |
| 输入同步旁路未启用 | ISO1452 隔离输入为异步信号，旁路有亚稳态风险，且需重做全部标定，不建议优先。 |
| RX 跟随者 CS latch 与 event counter 重复捕获同一边沿 | 去掉 TX/RX latch 每帧重武装可省 SM 与 Core1 时间，但会丢失独立的实际发射沿证据；须先确认是否被验收引用。 |

## 四、建议实施顺序

1. 更正 R-01 的进展记录，改按序号跳号方向排查短槽失败，并核对 G-01a 的周期语义分歧；恢复 R-02 默认值。
2. 修正测量口径（B-01a）后，先做 XIP 定位测量与 SRAM A/B（B-01b）。这是解释 TDMA 相位缺口的最大候选，结论会决定后续微优化是否值得。
3. 低风险切片：T-04（单次观察者服务并补测试）、T-06（map 只比代号）、T-07（先去重、CRC 只留一处）、T-08（ARM 时预算计划；挂载表加并发保护）、B-01c 死拷贝与诊断查询移出。
4. 测量切片：`TDMA_SERVICE_TIMING_ENABLED=0` 对比构建（T-03），恢复路径分段计时（T-10），非 TDMA 相位 WCET 实测（S-01 方案 3）。
5. 需评审的配置/契约切片：T-02 IRQ 预留按帧数分配、去掉关窗重复 ISR（S-01），T-05 帧尾 guard 下调并 HIL 扫描，level 20–22 注释或 fail closed（G-01）。
6. 需 C11 的模型级改动：事件驱动发车与 `cycle_period_ns` 语义统一（G-01），TDMA 相位拆分或余量捐赠（S-01）。
7. 大改：T-01 PIO 跟随者字节尾重设计以打通更高档位，需重新训练和四板 P3。不应先于 2：Core1 超时未解决前，提高线速不增加有效吞吐。

每个切片按 AGENTS.md 第 7 项在提交前执行 P3 硬件验收，并以同源码、同 profile 的实测更新 Task Progress；本文估算不作为收益证据。
