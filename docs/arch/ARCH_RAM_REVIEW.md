# ARCH RAM 余量评审与建议（面向 DPLL/VDC 调试）

Status: Draft
Domain: ARCH
Canonical: `docs/arch/ARCH_RAM_REVIEW.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/release/RELEASE_CHECKLIST.md`
Last updated: 2026-11-04

**目标**：为 DPLL/VDC 调试维持可用 SRAM 余量。DPLL/VDC 仍是主任务，RAM 只是它的使能条件；
release RAM 门禁不作为本阶段验收条件。

本文档记录当前余量状态、占用构成与消耗归因，并给出建议条目。**文中全部容量、余量与增量
均为指定构建产物的快照，非事实源**；判据以 `tools/project/ram_budget_check/ram_budget_check.py`
在**当前**构建上的实跑输出为准。建议条目由 `RAM-NN` 编号，供各域 owner 认领；未认领并
登记前不构成待办或契约。

**2026-11-04 复核范围变更**：本文档从"缩容前评审"扩为"当前复测 + 冗余实例复核"。
§1–§2.4 保留 2026-09/10 的历史快照以保留归因链（**不再代表当前值**）；当前值见 §2.5，
`.data`/`.time_critical` 口径缺口见 §2.6，冗余实例结论见 §6。历史值与当前值并列时不删除旧值。

后续进展：RefMem 静态缩容已完成本轮调试切片验收，实测收益和当前余量见
`docs/refmem/REFMEM_TASK_PROGRESS.md` 的 `REFMEM-TASK-20260916-001`。

## 1. 缩容前状态：已到链接硬底（2026-09 历史快照，非当前值）

**硬底不是门禁口径，而是构建事实**：crt0 的 `.heap` 占位段（2,048 B）必须完整落在 512 KiB
主区内，所以 `link_free_bytes` 低于 2,048 B 时链接直接失败。本阶段真正要维持的是
`link_free_bytes − 2,048 B`（下称**净余量**）不为负且留有余地。

| 时间 | 构建 | `link_free_bytes` | 净余量 | 备注 |
|---|---|---:|---:|---|
| 2026-09-14 22:24 | `out/build/dpll-node6-current` | 6,420 B | 4,372 B | 本轮评审起点 |
| 2026-09-15 10:11 | `out/build/DHRT100.elf.map` | 14,812 B | 12,764 B | arena 减半后的高点 |
| 2026-09-16 19:03 | `out/build/p3-known-topology-r1` | 2,192 B | 144 B | 完整构建 |
| 2026-09-16 19:32 | `out/build/dpll-fragment-spacing-r1` | 2,192 B | 144 B | 完整构建 |
| 2026-09-16 21:18 | `out/build/dpll-fifo-ingress-r1` | 1,548 B | **−500 B** | **链接失败** |

链接失败的证据链（`dpll-fifo-ingress-r1`）：

- 该目录只有 `DHRT100.elf.map`，无 `.elf` / `.bin`；同期的 `p3-known-topology-r1` 与
  `dpll-fragment-spacing-r1` 均有 A/B/Boot 全套产物。
- map 中 `.heap` 段起于 `0x2007F9F4`、长 2,048 B，止于 `0x200801F4`，越过主区顶端
  `0x20080000` 共 500 B；`__end__` 之后的自由空间仅 1,548 B，不足 2,048 B。
- 该目录 `.ninja_log` 中没有 `DHRT100.elf` 链接条目（对照构建有两条），对象文件停在
  21:18:31 而 map 生成于 21:18:36。

即：**当时净余量 144 B，最新切片已越底**；09-15 的 12,764 B 高点在约 33 小时内被消耗完毕。
2026-11-04 复测的当前值见 §2.5（净余量 1,252 B / 1,504 B），与上表不是同一时间点。

## 2. 占用构成

### 2.1 按子系统的存量与增量（2026-09 历史快照）

口径：链接器 map 中落在 `0x20000000`–`0x20082000` 的输入段按对象文件归属聚合。该口径适合
看**相对归属与增量**；其绝对合计与门禁口径的扫描范围不同（含/不含若干独立 section），
不可直接与 `link_free_bytes` 对账。

| 子系统 | 09-14 | 09-16 | 增量 |
|---|---:|---:|---:|
| `third_party/freertos` | 119,259 B | 119,259 B | 0 |
| `components/distributed_refmem` | 108,722 B | 110,802 B | +2,080 B |
| `components/vdc_dpll_manager` | 50,062 B | 57,607 B | **+7,545 B** |
| `components/tdma` | 48,560 B | 55,250 B | **+6,690 B** |
| `components/sync_io` | 44,877 B | 44,877 B | 0 |
| `components/calibration_manager` | 34,693 B | 34,701 B | +8 B |
| `components/event_bus` | 20,656 B | 20,656 B | 0 |
| `components/storage_manager` | 19,300 B | 19,300 B | 0 |
| `pico-sdk`、`middleware/scpi_port`、`ui_manager`、`application/src`、`ota_manager`、`sync_trigger`、`vdc_domain`、`portable_log_port` 等 | — | — | 0（`vdc_domain` +49 B） |
| **合计** | **509,086 B** | **526,098 B** | **+17,012 B** |

**增量高度集中**：`vdc_dpll_manager`、`tdma`、`distributed_refmem` 三者合计 +16,315 B，
占全部增量的 96%；其余子系统基本不变。即这一轮余量是被 DPLL/VDC 与 TDMA 自身的新增
常驻对象吃掉的，不是被闲置浪费占据。

### 2.2 主要常驻对象（2026-10-11 → 2026-11-04 对照）

快照口径：旧值取自 2026-10-11 构建；新值取 `out/build/bootstrap-on/DHRT100.elf.map`
（2026-11-04 14:12:16）。两列均**非事实源**，差异可能同时包含代码变更与对齐变化。

| 对象 | 2026-10-11 | 2026-11-04 | 变化 |
|---|---:|---:|---:|
| `ucHeap`（`third_party/freertos/.../heap_4.c`） | 118,784 | **98,752** | −20,032 |
| `s_distributed_refmem_table`（`distributed_refmem.c`） | 65,536 | **19,712** | −45,824 |
| `s_ota_fast_blocks`（`event_bus.c`） | 16,384 | 16,384 | 0 |
| `s_control`（`sync_io_logic_analyzer.c`） | 21,200 | **14,176** | −7,024 |
| `s_write_buffer`（`storage_manager.c`） | 16,384 | **12,288** | −4,096 |
| `s_tdma_runtime_owner`（`tdma_runtime_owner.c`） | 12,168 | 12,256 | +88 |
| `s_waveform_buffers`（`vdc_dpll_manager.c`） | 28,056 | **8,400** | −19,656 |
| `s_tdma_pio_spi_workspace`（`tdma_pio_spi_phys.c`） | 8,120 | 8,120 | 0 |
| `s_tdma_pio_spi_ring_adapter`（`tdma_runtime_owner.c`） | 7,520 | 7,840 | +320 |
| `s_dpll_capture_records`（`vdc_dpll_manager.c`） | 7,600 | 7,600 | 0 |
| `s_sync_io`（`sync_io.c`） | 6,296 | 6,296 | 0 |
| `s_capture_payload_storage`（`calibration_manager.c`） | 8,192 | 8,192 | 0 |
| `s_active` / `s_staging` / `s_rollbackable_image_buffer`（`refmem_table_registry.c`） | 各 8,192 | 各 8,192 | 0 |

结论：**旧表的"当前构建"已经不是当前**。`ucHeap`、向量表、波形三缓冲、`s_control`、
`s_write_buffer` 五项合计缩了约 96.6 KB，这也是本文档旧口径下"余量紧张"与今天 1,252 B
净余量之间的主要差额来源。

### 2.3 子系统构成（2026-10-11 → 2026-11-04）

口径同上节（`.data` + `.bss` 输入段按对象路径归属）；2026-11-04 一次共归因 493,615 B，
与两段实际合计 495,772 B 差 2,157 B（段内对齐填充），故本表是**近似归因**，不与
`link_free_bytes` 对账。

| 子系统 | 2026-10-11 | 2026-11-04 |
|---|---:|---:|
| `third_party/freertos` | 119,259 | **99,227** |
| `components/distributed_refmem` | 110,802 | **74,683** |
| `components/tdma` | 55,250 | **56,566** |
| `components/vdc_dpll_manager` | 52,063 | **46,733** |
| `components/sync_io` | 44,877 | **45,073** |
| `components/calibration_manager` | 34,701 | 34,701 |
| `components/sync_trigger` | — | **30,966** |
| `components/event_bus` | 20,656 | 20,656 |
| `components/storage_manager` | 19,300 | **15,204** |
| `application/src` | — | 12,544 |
| `middleware/scpi_port` | — | 11,470 |

子系统内部构成（2026-10-11 描述）仍具解释力：RefMem = 向量表 + 三份事务镜像 24,576 +
`s_pending_tables` 4,796 + `s_refmem_sync_context` 1,792；VDC/DPLL = 波形三缓冲 + DPLL
capture 7,600 + 反馈三项共 8,512；TDMA = runtime owner 12,256 + ring adapter 7,840 +
`s_tdma_pio_spi_phys` 2,856。

2026-10-11 的构成明细（历史原文，保留以对照）：

| 子系统 | 合计 | 主要构成 |
|---|---:|---|
| Distributed RefMem | 110,802 B | 向量表 65,536 + 三份事务镜像 24,576 + `s_pending_tables` 4,796 + `s_refmem_sync_context` 1,784 + `s_feedback_rx` 1,656（后两项在本次增量内） |
| VDC/DPLL | 52,063 B | 波形三缓冲 28,056（2026-10-11 由 33,600 缩容，见下 §波形三缓冲行）+ DPLL capture 7,600 + 反馈三项 `s_feedback_match_cache`/`s_feedback_matches`/`s_feedback_rx` 共 8,512 + `s_vdc_domain` 2,712 + `scratch_y` 快照 1,384 + SRAM 常驻代码 1,140 |
| TDMA | 55,250 B | runtime owner 12,168 + pio_spi workspace 8,120 + ring adapter 7,520 + event 常驻代码 3,296 + traffic scheduler 3,152 + `s_tdma_pio_spi_phys` 2,840 + cal tx 2,048 |
| SYNC_IO | 44,877 B | 逻辑分析仪控制块 21,200 + arena 16,384 + `s_sync_io` 6,296 |
| Calibration | 34,701 B | capture payload 8,192 + 四个 2,048 B 捕获环（ring tx/rx、marker raw/bit）+ 训练存储 1,280 + 扫描载荷 1,136 |
| EventBus | 20,656 B | OTA 事件载荷池 16,384 + OTA 队列 4,256 |

### 2.4 影响可选项的机制事实

| 事实 | 影响 |
|---|---|
| arena 尺寸由硬件 DMA 回绕决定，必须是 2 的幂（现为 2^14 = 16 KiB） | 不能任意取波形三缓冲尺寸之类的中间值；下一档是 32 KiB，反向操作无收益 |
| 波形观测取数链路为 `PIO RX → capture DMA ring(=arena) → latch ring → 观测服务` | 波形采集期间 arena 必然在线，波形三缓冲不能与 arena 共用内存 |
| `__HeapLimit` 与 `__StackLimit` 取值相同，显式 `ASSERT` 恒真 | 但静态越界仍由链接器自身的区域边界检查兜住（09-16 实证）；真正无保护的是运行期 `sbrk` 可增长到 `SCRATCH_X`（core1 栈）所在区 |
| `.heap` 段长度是 **crt0 占位最低位**，不是运行期堆上限（`__heap_end = __HeapLimit`） | 净余量口径只能按"减 2,048 B"算；把 2,048 B 读成"堆只有 2 KB"是误读 |
| PIO RX 每个 `push` 只承载 1 字节（8×`in pins,1` 后 `push block`），DMA 通道宽度决定该字节在 RAM 里占几个字节 | 通道宽度是纯 RAM 开销，不改变线上字节流；详见 §3 的 RAM-12 |

### 2.5 当前余量（2026-11-04 复测）

快照口径：`out/build/bootstrap-on/DHRT100.elf.map`（14:12:16）与
`out/build/bootstrap-m6/DHRT100.elf.map`（14:13:48）；`DHRT100_B` 与 `DHRT100` 的
`.bss`/`.data` 完全一致。两目录均早于当前工作树（见本节末警示），**这张表也是快照**。

| 构建 | `.bss` | `link_end` | `link_free_bytes` | `.heap` 占位 | **净余量** |
|---|---:|---|---:|---:|---:|
| `bootstrap-on` DHRT100 | 427,804 | `0x2007F31C` | 3,300 | 2,048 | **1,252 B** |
| `bootstrap-on` DHRT100_B | 427,804 | `0x2007F31C` | 3,300 | 2,048 | **1,252 B** |
| `bootstrap-m6` DHRT100 | 427,552 | `0x2007F220` | 3,552 | 2,048 | **1,504 B** |

与上一份评审快照（2026-11-04 13:39，净余量 372 B / 616 B）相比：ON +880 B、OFF +888 B。
差额来源可逐项归因，不是估算：

- `s_refmem_spi_rx_dma_words` 由 `uint32_t[296]` 降为 `uint8_t[296]`，1,184 B → 296 B，
  **−888 B**，落在 `385e888e`（`refmem_spi_physical_adapter.c` 两处 DMA 通道宽度同步改为
  `DMA_SIZE_8`，三处 `& 0xFFu` 删除）。
- ON 与 OFF 的净余量差从 244 B 变为 252 B：当前 ON 侧多出 18 个 bootstrap 专用静态量
  合计 63 B（`refmem_bootstrap_relay.c` 等），其余为段内对齐；13:39 的 map 已被覆盖，
  无法把跨快照的 +8 B 残差逐符号归因，**不猜**。

**警示（本次评审的教训）**：这两份 map 生成于 14:12/14:13，而工作树中已有 22 个源文件
晚于它们（最新 2026-11-04 20:01，`distributed_refmem_cmd_reply.c` / `refmem_cmd_load.c`）。
任何"当前余量"结论在重建之前都只是快照；把快照当事实源是上一份评审失准的直接原因。

### 2.6 口径缺口：`.data` 与 `.time_critical`

此前各版只审 `.bss`，而 `.data` 是 67,968 B——比除 `ucHeap` 与向量表之外的任何单个 `.bss`
对象都大。拆开后（同一快照）：

| 组成 | 字节 | 说明 |
|---|---:|---|
| `.time_critical.*` 段 | **58,666**（171 段） | 常驻 SRAM 的**代码**，不是变量 |
| `.data.*` 变量 | 7,258 | 最大 `s_position_state` 3,224、`__malloc_av_` 1,032、`s_realtime_schedule` 476 |
| 段内对齐填充 | ≈2,044 | 由 67,968 减去以上两项得出 |

`.time_critical` 按对象文件聚合（前 10）：`trigger_t2_runtime.c` 8,746、`vdc_output.c` 5,412、
`vdc_dpll_manager.c` 4,156、`sync_io_switch.c` 4,044、`sync_io_run_output.c` 3,740、
`tdma_event_observer.c` 3,740、`app.c` 3,308、`sync_io_gateway.c` 2,544、
`distributed_refmem_position_ack.c` 2,220、`dcd_rp2040.c` 1,572。

单段最大：`run_output_service_owned` 4,780、`tdma_event_feed` 3,740、
`app_realtime_dispatch` 3,308、`sync_io_run_output_submit_count_core1` 2,820、
`service_deadlines` 2,472。

结论与判据：

- 这 57 KB 是**最大的一类未被审过的 RAM 池**，但它不是"冗余副本"，而是 core1 实时路径的
  刻意放置（XIP 争用下取指延迟不可接受）。审查它的问题不是"是否重复"，而是"逐函数是否
  仍必须常驻"。
- 回收判据：只有同时满足"无 core1 实时调用者"与"不在中断/关中断窗口内"的函数才可去
  `.time_critical`。按函数名线性扣减是错的——单段尺寸常含被内联的静态函数（例如
  `run_output_service_owned` 4,780 B 明显包含内联展开）。
- 本条不产生任何已授权的回收量，列为 `RAM-16` 待评估。

## 3. 已落地（本轮核实，从建议清单移除）

| 原编号 | 内容 | 核实依据 |
|---|---|---|
| RAM-01 | SYNC_IO capture DMA ring 由 8192 words 降到 4096 words | `SYNC_IO_SHARED_WORKSPACE_DMA_RING_BITS` 现为 14；`.sync_io_dma_ring` 段现为 16,384 B（原 32,768 B） |
| RAM-07 | arena 容量常量的编译期一致性保护 | `SYNC_IO_SHARED_WORKSPACE_BYTES` / `_WORDS` 已改为从 ring bits 派生；`components/sync_io/src/sync_io_persona_resources.c` 新增 ring 位宽、整字、pulse 容量三条断言 |
| RAM-08 | 观测路径纳入共享区资源租约 | `sync_io_workspace_claim` / `held_by` / `release` 已实现，并已由 capture、logic analyzer、analyzer burst、model pulse 四个使用方调用；`docs/sync/SYNC_IO_ARCHITECTURE.md` 已记录该仲裁方式 |
| RAM-12 | RefMem SPI RX 缓冲区由 1 字/字节收窄为 1 字节/字节 | `refmem_spi_physical_adapter.c` 现为 `uint8_t s_refmem_spi_rx_dma_words[296]`、两处 `DMA_SIZE_8`、无 `& 0xFFu`；map 实测 1,184 → 296 B（−888 B）。**证据缺口见下** |

**RAM-12 的证据缺口（必须补齐才算验收）**：

- 现有守卫是 `tests/python/test_refmem_spi_rx_dma_width.py`，它是**源码形态断言**
  （检查 `uint8_t`、`DMA_SIZE_8` 出现两次、无 `DMA_SIZE_32/16`），不验证字节流等价。
  该测试的 docstring 正好写出了破坏模式："只改一处会让 32 位 DMA 把每项写 4 字节进 1 字节
  通道，踩坏相邻 `.bss`"。
- 等价性成立的前提是：对 `rxf[sm]` 的 8 位读**每次弹出一个 32 位 FIFO 项并返回其低 8 位**。
  若实际语义是"每 4 次 8 位读弹一次"或"不允许非 32 位访问"，字节流会错位或重复。
  本稿未取得 RP2350 手册条文或板级实测来确认这一点，故标记为**未验证的不变量**。
- 该改动混在 `385e888e`（提交信息主题是验收/工具修复）里，提交信息未提及；现有 P3 凭证
  是 `QUICK_DIAGNOSTIC / DPLL60_JOINT`，凭证键中无 refmem 项，`unverified_scope` 也未覆盖
  RefMem SPI 路径——因此**不能用该凭证证明此改动**。
- 补证方法（任选其一并留档）：host 侧把一次捕获的 296 个 FIFO 字分别按"取低字节"与
  "8 位 DMA"语义展开后逐字节比对；或两板 HIL 跑 `SYSTem:REFMEM:BOOTstrap:STATus?` 的
  tx/rx/invalid 计数与 `SYSTem:REFMEM:SYNC:RAW:RX?` 原始序列，前后一致。

## 4. 剩余建议

按"能否立刻换取可测量余量"排序。**除注明"实测 / DWARF"者外，收益均为算术推算，不是实测**；
落地前需重建并复测。

**收益优先级（2026-11-04 复核）**：RAM-02（潜在 12,288，需 System Pack 上限证据）>
RAM-03（4,096/档，改动已在工作树）> RAM-16（58,666 池，回收比例未知）>
RAM-13（2,252，产品决策）> RAM-15（1,110，需设计）> RAM-05（待校准域）> RAM-14（392，附属）。

| 编号 | 建议 | 增净余量 | 依据锚点 | 前置条件 |
|---|---|---:|---|---|
| RAM-03 | EventBus OTA 事件载荷池槽位减一档（3→2） | **4,096 B/档** | `EVENT_BUS_OTA_FAST_BLOCK_DEPTH`（`components/event_bus/inc/event_bus.h`）、`OTA_EVENT_MAX_DATA_SIZE`（=4,096，由 `PROJECT_OTA_MAX_DATA_BLOCK_SIZE` 决定） | 需给出 OTA 事件的实测并发深度；该数组只服务于载荷超过内联容量的事件。**注：4→3 已由并发编辑者在工作树完成（未提交），map 里的 16,384 B 仍是 depth=4 的旧值** |
| RAM-05 | 评估 CalibrationManager 捕获载荷缓冲及四个捕获环容量 | 待定（现 8,192 B + 4 × 2,048 B） | `calibration_capture_payload_storage_t` 等（`components/calibration_manager/src/calibration_manager.c`） | 需校准域确认并发捕获路数 |
| RAM-13 | SCPI 诊断桥 `s_refmem_sync` 编译开关化（不是合并） | **2,252 B** + flash | `middleware/scpi_port/src/scpi_system_snapshot_commands.c` 的 `s_refmem_sync`；15 条 `SYSTem:REFMEM:SYNC:*` 诊断命令 | **产品负责人**决定是否保留该诊断树；须同步改 `docs/interface/SCPI_COMMANDS.md` 与 `tools/refmem/refmem_sync_hil_validate/` |
| RAM-14 | 删除 SCPI 诊断桥里的 SPI 适配器中转（`inject_rx_frame` → `poll` 往返） | **392 B** | `refmem_pio_spi_adapter_t` 实测 `sizeof` = 392（DWARF）；`refmem_pio_spi_adapter_send()` 恒返回 `false` + `TX_UNBOUND` | 需先决定 `SYSTem:REFMEM:SYNC:ADAPter?` 的去向；与 RAM-13 互斥（同一对象） |
| RAM-15 | 常驻 `s_refmem_sync_context` 换用 `refmem_sync_delta_context_t`（680 B） | 最多 **1,112 B** | `s_refmem_sync_context` 1,792 B；`refmem_sync_delta_context_t` 680 B | **不是类型替换**：`distributed_refmem_link.c` 把非 VDC 的帧（含 COMMAND）都送进通用接收器，而 compact 型没有 command handler；需先给 compact 型补 hook（+8~12 B）并做 host 双路径测试 |
| RAM-02 | 复核 RefMem 三份事务镜像的单份容量 | 待定（现三份合计 24,576 B） | `REQMEM_TABLE_IMAGE_BUFFER_SIZE`（`components/distributed_refmem/inc/refmem_table_registry.h`） | 确认该上限是否被 System Pack 的真实上限绑定 |
| RAM-04 | 复核 RefMem 表几何的必要性 | 待定（**前提已变**：`DISTRIBUTED_REFMEM_TABLE_SIZE` 现为 19,712，不再是 65,536） | `DISTRIBUTED_REFMEM_TABLE_SIZE` 及 `refmem_vector_table.c` 的尺寸断言、`refmem_vector_table_validate_directory()` 的连续覆盖约束 | 属跨域契约变更，须先在 `docs/check/DOCS_REGISTRY.md` 登记并完成跨域核验；旧口径的 65,536 B 已不成立，本条应重定范围或关闭 |
| RAM-16 | 评估 `.time_critical` 常驻代码池（58,666 B / 171 段） | 待评估 | §2.6 | 需逐函数的 core1 调用图证据；不得按函数名线性扣减 |
| RAM-11 | 为 DPLL/VDC 常驻对象设一条预算线 | — | §2.1 的增量归因（三个子系统占全部增量 96%） | 需 VDC 域认可口径；本条不回收 RAM，用于防止余量被逐切片蚕食 |

**诊断树的性价比结论（2026-11-04 追加）**：三种处理方式**收益相同、代价不同**——

| 方式 | 回收 | 代价 |
|---|---:|---|
| 保持现状 | 0 B | 无 |
| 直接 `#if` 裁掉 15 条命令 | 2,252 B | 产品诊断面消失；HIL 脚本与文档同步改 |
| 只读半边合并到常驻 context | 2,252 B（换 ~64 B 的 last_rx 快照 + 本地序列） | 额外引入跨任务读一致性、序列域隔离、12 条命令重写、双域签字 |

合并**不比裁掉多省一个字节**，只是"裁掉但保留可见性"。因此：余量不紧张时选保持现状；
产品要瘦身时选 `#if`；只有"必须留诊断面又要省这 2.2 KB"时才选合并。当前 1,252 B 净余量
（上一轮 RAM-12 落地后）不构成必须瘦身的理由。

## 5. 一致性修正

| 编号 | 建议 | 依据锚点 | 说明 |
|---|---|---|---|
| RAM-06 | 处理运行期堆增长与 `SCRATCH_X` 相邻的问题 | `linker/rp2350_app_slot_a.ld` 的 `__HeapLimit`、`__StackLimit` 与 `ASSERT(__StackLimit >= __HeapLimit, ...)`；`__StackOneTop` 所在区 | 显式断言因两个符号取值相同而恒真；静态越界由链接器区域检查兜住，无保护的是运行期 `sbrk` 可增长到 `SCRATCH_X` 起址（core1 栈），中间既无 guard 也无 MPU 隔离 |
| RAM-09 | 对齐 `docs/sync/SYNC_IO_ARCHITECTURE.md` 的 persona 兼容矩阵与 workspace 资源模型 | `SYNC_IO_PERSONA_WORKSPACE_*` 枚举、`sync_io_persona_compatible()` 的 `workspace_mask` 按位与判定 | 四个 persona 共用同一个 workspace 位，矩阵中的"条件并发"在该模型下恒不成立；共享区已有独立租约，但 persona 准入层尚未对齐 |
| RAM-10 | 为 core0 主栈补水位观测或边界检查 | `linker/rp2350_app_slot_a.ld` 的 `.stack_dummy` / `__StackBottom`、`config/freertos/FreeRTOSConfig.h` 的栈溢出检查开关 | 栈溢出检查只覆盖 FreeRTOS 任务栈，不覆盖 core0 主栈 |

## 6. 冗余实例复核（2026-11-04）

目的：回答"把两个 live 实例合并成一个能省多少"。结论先行：**严格意义上的安全合并，全仓
0 项**。唯一真正的重复实例对（SCPI 诊断 context vs 常驻 context）合并会污染 live 状态；
其余"同尺寸/同名/同类型"经 liveness 追踪都不是重复。

### 6.1 结论表

| 候选 | 实测尺寸 | 冗余证据 | 判定 |
|---|---:|---|---|
| `s_refmem_sync`（SCPI 诊断桥） | 2,252 B | 与常驻 `s_refmem_sync_context` 同类型不同实例、**同时活跃** | **不可合并**，只能开关化（RAM-13） |
| `s_refmem_sync_context`（常驻） | 1,792 B | 真实 node-load 接收器 | 保持；容量可议（RAM-15） |
| `s_refmem_spi_rx_dma_words` | 296 B（原 1,184） | 曾是 4× 容量冗余（1 字/字节） | 已落地（RAM-12） |
| 三个 `s_action` | 1,432 / 480 / 152 B | 三个不同 TU 的 `static`、三种不同类型 | **不可合并**（同名纯属巧合） |
| `s_capture_payload_storage` | 8,192 B | **已经是 union**（marker/data/sck/ring 共用） | 无合并对象 |
| 三份 8,192 事务镜像 | 24,576 B | commit 路径要求三份并存 | **不可合并** |
| 校准阶段捕获/工作区 | 最多可省 ~7,176 B | 互斥性证据不完整 | 暂不合并（见 6.6） |
| CRC32 多实现 | — | 同算法多实现 | 只省 flash，不增 RAM |

### 6.2 SCPI 诊断桥不能合并到常驻 context

两者是不同实例且同时活跃：

- `scpi_refmem_sync_context()` 返回 `&s_refmem_sync.context`（`scpi_system_snapshot_commands.c`
  的 accessor），不是常驻实例。
- 常驻 `s_refmem_sync_context` 的**全部**引用只有 7 处：`distributed_refmem.c` 定义、
  `distributed_refmem_plane.c` 的 init + `set_command_handler`、`distributed_refmem_status.c`
  的 init + `set_command_handler` + 一处 `.local_slot` 读取、`distributed_refmem_link.c` 的
  `receive_frame`。
- `distributed_refmem_status.c` 的注释是直接证据："SCPI and RefMem are separate Core0 tasks.
  Only the RefMem service may retire/write the dedicated command receiver."
- 路由事实（决定 RAM-15 为什么不能"直接换型"）：`distributed_refmem_link.c` 只取
  `payload_class = REFMEM_REALTIME_TDMA_PAYLOAD_REFMEM_DELTA` 的结果帧，取到后
  再按帧类型分流——VDC COMMAND 进 `s_vdc_command_context`，**其余帧（含 node-load COMMAND）
  进 `s_refmem_sync_context`**。通用接收器因此同时承担 DELTA 与 COMMAND 两条路径，而
  `refmem_sync_delta_*` 明确把非 DELTA 帧判为 `FRAME_INVALID`/`BAD_TYPE`。

合并后会被污染的 live 状态（`refmem_sync_context_t` 全部字段，因为 `refmem_sync_init()`
第一句就是整结构 `memset`，`refmem_sync.c`）：

| 字段 | 谁在用 | 后果 |
|---|---|---|
| `peer[8]` | node-load auto sync 对端序/可达性 | 对端"失联"，重同步/重传风暴 |
| `mirror[8]` | DELTA 镜像（slot/field/seq/value） | 已提交值被清零，后续 fence/ACK 判定错误 |
| `ack[8]` / `fence[8]` | 维护 ACK / fence 判定 | fence 证据丢失，`min_table_seq` 判定回退 |
| `remote_quality[8]` / `quality` | 质量计数与远端快照 | 计数清零，`QUALity?` 读回失真 |
| `command_handler` / `command_handler_context` | `distributed_refmem_sync_command_handler` | **置 NULL，node-load 命令不再被分发**（最严重） |
| `local_slot` / `active_epoch_id` / `active_run_id` | 会话身份 | 与 `s_node_load_auto_sync` 的 epoch/run 不一致，帧被拒 |

语义越权（比状态清零更难接受）：`SYSTem:REFMEM:SYNC:RX` 会把 PC 搬运来的 hex 注入诊断
context；若合并且已绑定 handler，外部 hex 可直接触发真实 refmem 命令执行，等价于新增一条
绕过 `AUTO`/TDMA owner 的命令注入路径。注入的 `ACK_NACK`/`FENCE`/`QUALITY` 帧还会被当成
线上真实回程记入 `ack[]/fence[]/remote_quality[]`，使 fence 被虚假满足。另有序列域冲突：
`s_refmem_sync.tx_seq32` 与 `s_node_load_auto_sync.next_seq32` 是两条独立序列。

**2252 B 的构成已实测闭合**（DWARF `sizeof`）：`refmem_sync_context_t` 1,792 +
`refmem_pio_spi_adapter_t` 392 + `tx_seq32` 4 + `refmem_sync_rx_snapshot_t` 60 +
`initialized` 4 = 2,252。因此"删掉适配器中转"的收益是精确的 392 B，不是估算上界。

诊断面共 15 条命令（`scpi_system_snapshot_commands.h` 中 `SYSTem:REFMEM:SYNC:` 段）：
`INITialize`、`HELLo?`、`EPOCh?`、`DELTa?`、`ACK?`、`FENCe?`、`QUALity:FRAMe?`、`RX`、
`MIRRor?`、`ACK:STATus?`、`FENCe:STATus?`、`QUALity:STATus?`、`PEER?`、`QUALity?`、
`ADAPter?`；`AUTO`/`AUTO?` 与 `SPI:*` 属常驻侧，开关化时须留在外面。

### 6.3 三个 `s_action`：同名不同物

| 符号 | 尺寸 | 定义 | 类型 | 域 |
|---|---:|---|---|---|
| `s_action` | 1,432 B | `distributed_action_probe.c` | 匿名 struct（probe engine + binding + queue[4] + assembly/last_transport + status） | DistributedRefMem AO（运行时 opt-in 诊断） |
| `s_action` | 480 B | `trigger_action_service.c` | `trigger_action_snapshot_t` | sync_trigger（T1 action 表） |
| `s_action` | 152 B | `sync_io_action.c` | `sync_io_action_snapshot_t` | sync_io（PIO 输出动作） |

三者均为 `static`，不同 TU 不产生符号冲突，reader/writer 集合互不相交。合并即类型双关与
状态互相踩踏。1,432 B 那份是运行时 opt-in 的诊断，若产品镜像不需要，属"开关化"而非合并。

### 6.4 三份 8,192 事务镜像不可合并

`s_active` / `s_staging` / `s_rollbackable_image_buffer` 是回滚语义的最小集合：
commit 先 `active → rollbackable`，再 `staging → active`（`refmem_table_registry.c`），
且激活前有"任一份被访问即拒绝"的 owner 守卫（`s_image_access_count[...]`）。合并任意两份
都会丢失"新 active + 旧 active"同时保留。可讨论的只有单份容量（RAM-02），不是合并。

### 6.5 `s_capture_payload_storage` 已经是 union

`calibration_manager.c` 的 `calibration_capture_payload_storage_t` 把 marker/data/sck/ring
四份 8,192 合成一份，注释写明"exports are serialized through the Core0 maintenance path…
never live concurrently"。**没有 4 × 8,192 可回收**；剩余问题只是容量本身（RAM-05）。

### 6.6 校准阶段捕获/工作区：合并"可能"，但互斥证据不完整

实测：`s_data_capture`/`s_sck_capture`/`s_clk_coded_capture` 各 1,024；三个 workspace 各
832；`s_data_observed_words`/`s_marker_tx_words` 各 804；`s_marker_raw_capture` 与
`s_marker_bit_capture` 各 2,048；`s_ring_capture_rx`/`s_ring_capture_tx` 各 2,048。

明确不能动：marker raw/bit 与 ring rx/tx 都是**同一调用同时传入**，必须并存。
可议的是跨阶段 union，上限 ≈ 3×1,024 + 3×832 + 2×804 = **7,176 B**。

互斥性证据（有利）：`tdma_pio_spi_phys_data_train_restored.c` 的 arm 路径一次性拒绝
data_train/marker/coded/p3/cal_loopback 全部活跃组合，说明 owner 层按"同一时刻只有一个
捕获者"设计。

互斥性证据（缺口，决定性）：`tdma_pio_spi_phys_coded.c` 的 `coded_start` 只检查 coded 与
marker 两类状态，**不检查 data_train / p3 / cal_loopback**，与 data_train 侧的检查不对称；
校准层 `s_data_active`/`s_sck_active`/`s_marker_active`/`s_clk_coded_active` 是四个独立标志，
不存在单一"只允许一个阶段"的闸门。**补齐 owner 仲裁对称性之前无法证明等价**，维持待定。

### 6.7 CRC32：只省 flash，且表不在主 RAM

已核实的两条：

- `tdma_transport_frame.c` 的 4 位（nibble）残差表 `tdma_transport_crc_residues[16]` 被显式
  放在 `__scratch_y("tdma_transport_crc_table")`，Kernel 亦标注常驻 SRAM；它占的是
  `scratch_y`，**不占 512 KiB 主区余量**。
- `ota_crc32_compute` / `ota_crc32_update`（`components/ota_manager/src/ota_crc32.c`）是对
  `portable_ota_port_crc32_*` 的薄包装，不是第三份内核。

"全仓多实现"的准确清单本次未逐条展开（`distributed_config.c`、`calibration_bias.c` 等处的
`*_crc32` 多为域内哈希/快照摘要，不是同一条线格式）。结论不变：**在没有第二张落在 `.bss`
的静态表之前，统一 CRC 实现只减少 flash，对 1,252 B 主 RAM 余量零贡献**，且跨线格式风险高，
不列为本次条目。

### 6.8 其它被判"非冗余"的常驻对象

| 符号 | 尺寸 | 判定 |
|---|---:|---|
| `s_distributed_refmem_table` | 19,712 | 必需单实例（`distributed_refmem_vector_table()` 返回其地址） |
| `s_ota_fast_blocks` | 16,384 | 非重复，但为单点可裁剪（RAM-03） |
| `s_control` | 14,176 | 单 owner 控制块 |
| `s_write_buffer` | 12,288 | SD 写缓冲，多域按序复用同一份 |
| `s_tdma_runtime_owner` | 12,256 | 组合单实例 |
| `s_waveform_buffers` | 8,400 | 三缓冲，采集期在线；不得与 arena 共用（§2.4） |
| `s_feedback_match_cache` 4,120 + `s_feedback_matches` 2,736 | 6,856 | 互补结构，同一次匹配里都要读 |
| `s_position_state`（`.data`） | 3,224 | 单实例位置状态 |

## 7. 已评估并排除（不建议作为回收项）

| 对象 | 结论 | 依据 |
|---|---|---|
| FreeRTOS 静态堆 `ucHeap` | 不以下调容量作为回收手段 | `docs/arch/RTOS_HAOFV_TODO.md` 的 P0-RAM 记录：更低 heap 候选曾导致启动风险，已明确不再只凭 map 数字下调 |
| 波形三缓冲 `s_waveform_buffers` | **2026-10-11 已再缩一次，当前不可再缩** | 三份是流水线最小配置（在采／在等／在写）。每段记录数原为 352（历史 400→360→352）；TDMA-SEAT-03 的一等席保全使 app 链接溢出 SRAM 1436 B，按目标授权（"如果RAM不够，可以缩小波形buffer"）再缩到 **334**（−1512 B），随后 A/B 两槽重新链接通过。代价：单段记录数 −5.1%，长程连续性仍由 SD 分段与 `dropped_count` 记账保证。若还需余量，替代方案是缩小席位侧 `DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH`（当前 4，降到 2 可再省 320 B）而不是继续缩本缓冲；详见 `TDMA-PROGRESS-20261011-025` 与 `TDMA_SEAT_ACCEPTANCE_REVIEW.md` 待决事项 5 |
| 波形三缓冲与 SYNC_IO arena 合并 | 不成立，勿再提案 | 见 §2.4：DMA 环形尺寸必须是 2 的幂；且波形采集期间 arena 必然在线 |
| StorageAO 写事务缓冲 `s_write_buffer` | 已是多域复用的单份场地，无重复副本 | `STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES` 直接取自 `STORAGE_MANAGER_FILE_WRITE_MAX_BYTES`，波形分段等写入路径按序复用同一份 |
| SCPI 诊断 context → 常驻 context | **不可合并** | live 状态清零 + `command_handler` 被解除 + 诊断注入升级为真实命令 + 序列域冲突（§6.2） |
| 三个 `s_action` 合并 | 不可合并 | 不同 TU / 不同类型 / 生命周期不相交（§6.3） |
| 三份 8,192 事务镜像合并 | 不可合并 | 回滚语义要求三份并存（§6.4） |
| `s_capture_payload_storage` 再合并 | 无对象 | 已经是 union（§6.5） |
| marker raw/bit、ring rx/tx 合并 | 不可合并 | 每次调用成对同时使用（§6.6） |
| 校准跨阶段 union | 暂不合并 | owner 仲裁不对称 + 无单一阶段闸门，等价性无法证明（§6.6） |
| CRC32 统一 | 暂不合并 | 只省 flash，不增 RAM 余量；跨线格式风险（§6.7） |
| `s_feedback_match_cache` / `s_feedback_matches` | 互补，非重复 | 同路径同时读（§6.8） |

## 8. 复核命令

```powershell
# 当前构建（重建后请用最新目录，并确认 map 的 mtime 晚于全部源文件）
python tools/project/ram_budget_check/ram_budget_check.py out/build/bootstrap-on/DHRT100.elf.map --profile release
python tools/project/ram_budget_check/ram_budget_check.py out/build/bootstrap-on/DHRT100_B.elf.map --profile release
python tools/project/ram_budget_check/ram_budget_check.py out/build/bootstrap-m6/DHRT100.elf.map --profile release
```

净余量 = 输出的 `link_free_bytes` 减去 2,048 B。该值接近 0 时，下一个 DPLL/VDC 切片就可能
直接链接失败，不再是门禁问题而是构建问题。

RAM-12 的守卫测试（源码形态，非行为等价）：

```powershell
python -m pytest tests/python/test_refmem_spi_rx_dma_width.py -p no:cacheprovider
```

## 9. 执行计划（宿主侧可执行；不触碰台架）

### 9.1 约束：为什么有些步骤只能停在"改完但不提交"

- 提交路由由 `.githooks/pre-commit` → `tools/acceptance/credential_scope.py` 决定：`firmware` 类
  必须通过 `tools/hardware_acceptance/workflow_steps.py check-staged`（P3 凭证 = 台架证据）。
  **所以不动台架时，固件改动可以写、可以构建、可以跑 host 测试，但不能提交**；能直接提交的
  只有 docs 与 host 类。
- **不算台架的动作**（本机即可）：`cmake --build`、`ram_budget_check.py`、`pytest`、
  `tools/tests/run_refmem_all_host_tests.ps1`。
- **需要台架的动作**（本轮不做，只列判据）：任何固件提交的 P3 凭证、两板 HIL、RAM-12 的
  硬件语义实测、SOAK。

### 9.2 P0 基线：任何改动之前必须先做

| 步 | 动作 | 命令 | 判据 |
|---|---|---|---|
| S0.1 | 树静默检查 | `Get-ChildItem components,application,middleware -Recurse -File -Include *.c,*.h,*.pio \| Sort-Object LastWriteTime -Descending \| Select-Object -First 1` | 最近 10 分钟无变更；有变更先与编辑者协调，否则基线无意义 |
| S0.2 | 重建 | `cmake --build out/build/bootstrap-on`；备选新目录 `cmake --preset pico2-release -B out/build/ram-baseline` | 构建成功；map 的 mtime 晚于全部源文件 |
| S0.3 | 记基线 | `python tools/project/ram_budget_check/ram_budget_check.py out/build/bootstrap-on/DHRT100.elf.map --profile release` | 记下 `bss_bytes` 与 `link_free_bytes`；与 427,804 / 3,300 的差必须能解释 |
| S0.4 | host 绿灯 | `.\tools\tests\run_refmem_all_host_tests.ps1` | 全过（2026-11-04 本机实跑：40 个套件全 PASS）；不绿就先修，再谈改动 |

### 9.3 P1 改动：host 可证、按顺序做

**S1 — RAM-14（392 B，最小且最独立）**

- 改动：`scpi_system_snapshot_commands.c` 的 RX 命令直接调 `refmem_sync_receive_frame()`，
  删掉 `inject_rx_frame` → `poll` 往返；`scpi_refmem_sync_state_t` 去掉 `adapter` 字段。
- 前提：产品先定 `SYSTem:REFMEM:SYNC:ADAPter?` 去留（保留则 adapter 不能删，只能删往返）。
- 验收：host 单测断言"直连 == 往返"（两条路径在同一 TU 内可同时调用）；构建后
  `bss_bytes` = 427,412（±4 对齐）；净余量 1,644 B。
- 回滚：patch 留档到 `out/patches/ram14-*.patch`，**不 reset、不 checkout**（AGENTS §2.10）。

**S2 — RAM-15（≤1,112 B，需设计，不是换型）**

- 改动：给 `refmem_sync_delta_context_t` 补 `command_handler`/`command_handler_context`
  （+8~12 B）并让 `refmem_sync_delta_receive_frame` 支持 COMMAND 分发，再把
  `distributed_refmem.c` 的 `s_refmem_sync_context` 换型。
- 验收：host 单测覆盖 DELTA 与 COMMAND 两条帧路径（handler 调用次数、payload 一致）；
  构建后 bss 减少 ~1,100 B（1,792 − 680 − 新增字段）。
- 风险：改变接收器语义属跨域，需 RefMem owner 与 SCPI owner 双签。

**S3 — RAM-13（2,252 B，产品决策）**

- 改动：照 `PROJECT_REFMEM_BOOTSTRAP_ENABLED` 的既有模式加 CMake option（`option()` +
  `target_compile_definitions`），把 `s_refmem_sync` 与 15 条诊断命令整体 `#if` 掉。
- 验收：OFF 构建 bss −2,252；ON 构建 bss 不变；host 侧断言 OFF 时命令表不含这 15 条。
- 注意：与 RAM-14 互斥（同一对象），二选一，不要叠加。

**S4 — RAM-03（只做证据，不改代码）**

- 动作：读 `event_bus.c` 的 OTA 事件生产/消费路径，给出"同时最多 N 个超大载荷事件"的结论；
  只有 N ≤ 2 才能提 depth 4→2。

**S5 — RAM-16（只做证据）**

- 动作：对 171 个 `.time_critical` 段按对象列调用者，分成"仅 core0 / 仅诊断 / 无调用者"三类；
  只统计，不改代码。

### 9.4 预期账（三种组合，算术叠加，必须实测复核）

| 组合 | `.bss` | 净余量 |
|---|---:|---:|
| 现状 | 427,804 | 1,252 B |
| A：保留诊断树（RAM-14 + RAM-15） | 426,312 | 2,744 B |
| B：裁剪诊断树（RAM-13 + RAM-15） | 424,452 | 4,604 B |
| A + RAM-03（若证据成立） | 418,120 | 10,936 B |

### 9.5 现在就能做的三件事

1. **S0.2 + S0.3**：重建并记下真实基线（约 5 分钟）。
2. **S0.4**：跑一遍 RefMem host 测试，确认起点是绿的。
3. **S1**：把 RAM-14 的 patch 做出来并跑 host 测试；**不提交**——`firmware` 类会被 P3 门禁拦下。

## 10. 边界声明

- 本文档的容量、增量与归因均为指定构建产物的快照，非事实源；§2.2/§2.3 的两列分别来自
  2026-10-11 与 2026-11-04 14:12:16 的构建，跨列比较含对齐差异。
- 2026-11-04 的两份 map 早于当前工作树（22 个源文件更新），§2.5 的 1,252 B / 1,504 B
  在重建前只是快照。
- 建议条目的量级是评审估算，不是已批准的变更；除已注明"实测/DWARF"者外，预期收益为
  算术推算，不是实测值。
- `RAM-02`～`RAM-05`、`RAM-13`～`RAM-16` 涉及跨域语义或对外可观测容量，落地前应按
  `docs/check/DOCS_REGISTRY.md` 的规则判断是否需要登记为契约并做跨域核验。
- release profile 门禁（48 KiB）与 debug 门禁量级（32 KiB）本次均不作为验收条件；当前
  两个构建都会 FAIL 该门禁，这是既有取舍，不是本文新增的结论。
- 仓库待提交条目已接近 AGENTS §2.11 的 40 项积压上限；认领 RAM-13～RAM-16 任一固件条目
  前需先清账，否则会触发冻结流程。
- §9 的执行计划是**宿主侧**步骤；其中固件类改动在取得 P3 凭证前不得提交，构成本轮的硬边界。
- 本篇不改变 DPLL/VDC 的主任务优先级：RAM 条目是使能条件，不替代
  `docs/vdc/VDC_DOMAIN_TODO.md` 与 `docs/vdc/VDC_TASK_PROGRESS.md` 的既有状态。
