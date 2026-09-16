# ARCH RAM 余量评审与建议（面向 DPLL/VDC 调试）

Status: Draft
Domain: ARCH
Canonical: `docs/arch/ARCH_RAM_REVIEW.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/release/RELEASE_CHECKLIST.md`
Last updated: 2026-09-17

**目标**：为 DPLL/VDC 调试维持可用 SRAM 余量。DPLL/VDC 仍是主任务，RAM 只是它的使能条件；
release RAM 门禁不作为本阶段验收条件。

本文档记录当前余量状态、占用构成与消耗归因，并给出建议条目。**文中全部容量、余量与增量
均为指定构建产物的快照，非事实源**；判据以 `tools/ram_budget_check/ram_budget_check.py`
在**当前**构建上的实跑输出为准。建议条目由 `RAM-NN` 编号，供各域 owner 认领；未认领并
登记前不构成待办或契约。

后续进展：RefMem 静态缩容已完成本轮调试切片验收，实测收益和当前余量见
`docs/refmem/REFMEM_TASK_PROGRESS.md` 的 `REFMEM-TASK-20260916-001`。
以下占用与链接失败均为缩容前评审快照，不代表提交后的当前资源状态；剩余建议尚未授权实施，OTA 优化继续暂缓。

## 1. 缩容前状态：已到链接硬底（快照，非事实源）

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

即：**当前净余量 144 B，最新切片已越底**；09-15 的 12,764 B 高点在约 33 小时内被消耗完毕。

## 2. 占用构成（快照，非事实源）

### 2.1 按子系统的存量与增量

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

### 2.2 主要常驻对象（当前构建，前 16）

| 字节 | 段 | 对象 |
|---:|---|---|
| 118,784 | `.bss` | `ucHeap`（`third_party/freertos/.../heap_4.c`） |
| 65,536 | `.bss` | `s_distributed_refmem_table`（`distributed_refmem.c`） |
| 33,600 | `.bss` | `s_waveform_buffers`（`vdc_dpll_manager.c`） |
| 21,200 | `.bss` | `s_control`（`sync_io_logic_analyzer.c`） |
| 16,384 | `.sync_io_dma_ring` | `sync_io_shared_workspace`（`sync_io.c`） |
| 16,384 | `.bss` | `s_write_buffer`（`storage_manager.c`） |
| 16,384 | `.bss` | `s_ota_fast_blocks`（`event_bus.c`） |
| 12,168 | `.bss` | `s_tdma_runtime_owner`（`tdma_runtime_owner.c`） |
| 8,192 | `.bss` | `s_staging_image_buffer`（`refmem_table_registry.c`） |
| 8,192 | `.bss` | `s_rollbackable_image_buffer`（`refmem_table_registry.c`） |
| 8,192 | `.bss` | `s_capture_payload_storage`（`calibration_manager.c`） |
| 8,192 | `.bss` | `s_active_image_buffer`（`refmem_table_registry.c`） |
| 8,120 | `.bss` | `s_tdma_pio_spi_workspace`（`tdma_pio_spi_phys.c`） |
| 7,600 | `.bss` | `s_dpll_capture_records`（`vdc_dpll_manager.c`） |
| 7,520 | `.bss` | `s_tdma_pio_spi_ring_adapter`（`tdma_runtime_owner.c`） |
| 6,296 | `.bss` | `s_sync_io`（`sync_io.c`） |

### 2.3 主要子系统内部构成（当前构建）

| 子系统 | 合计 | 主要构成 |
|---|---:|---|
| Distributed RefMem | 110,802 B | 向量表 65,536 + 三份事务镜像 24,576 + `s_pending_tables` 4,796 + `s_refmem_sync_context` 1,784 + `s_feedback_rx` 1,656（后两项在本次增量内） |
| VDC/DPLL | 57,607 B | 波形三缓冲 33,600 + DPLL capture 7,600 + 反馈三项 `s_feedback_match_cache`/`s_feedback_matches`/`s_feedback_rx` 共 8,512 + `s_vdc_domain` 2,712 + `scratch_y` 快照 1,384 + SRAM 常驻代码 1,140 |
| TDMA | 55,250 B | runtime owner 12,168 + pio_spi workspace 8,120 + ring adapter 7,520 + event 常驻代码 3,296 + traffic scheduler 3,152 + `s_tdma_pio_spi_phys` 2,840 + cal tx 2,048 |
| SYNC_IO | 44,877 B | 逻辑分析仪控制块 21,200 + arena 16,384 + `s_sync_io` 6,296 |
| Calibration | 34,701 B | capture payload 8,192 + 四个 2,048 B 捕获环（ring tx/rx、marker raw/bit）+ 训练存储 1,280 + 扫描载荷 1,136 |
| EventBus | 20,656 B | OTA 事件载荷池 16,384 + OTA 队列 4,256 |

### 2.4 影响可选项的机制事实

| 事实 | 影响 |
|---|---|
| arena 尺寸由硬件 DMA 回绕决定，必须是 2 的幂（现为 2^14 = 16 KiB） | 不能任意取 33,600 B 之类的中间值；下一档是 32 KiB，反向操作无收益 |
| 波形观测取数链路为 `PIO RX → capture DMA ring(=arena) → latch ring → 观测服务` | 波形采集期间 arena 必然在线，波形三缓冲不能与 arena 共用内存 |
| `__HeapLimit` 与 `__StackLimit` 取值相同，显式 `ASSERT` 恒真 | 但静态越界仍由链接器自身的区域边界检查兜住（09-16 实证）；真正无保护的是运行期 `sbrk` 可增长到 `SCRATCH_X`（core1 栈）所在区 |

## 3. 已落地（本轮核实，从建议清单移除）

| 原编号 | 内容 | 核实依据 |
|---|---|---|
| RAM-01 | SYNC_IO capture DMA ring 由 8192 words 降到 4096 words | `SYNC_IO_SHARED_WORKSPACE_DMA_RING_BITS` 现为 14；`.sync_io_dma_ring` 段现为 16,384 B（原 32,768 B） |
| RAM-07 | arena 容量常量的编译期一致性保护 | `SYNC_IO_SHARED_WORKSPACE_BYTES` / `_WORDS` 已改为从 ring bits 派生；`components/sync_io/src/sync_io_persona_resources.c` 新增 ring 位宽、整字、pulse 容量三条断言 |
| RAM-08 | 观测路径纳入共享区资源租约 | `sync_io_workspace_claim` / `held_by` / `release` 已实现，并已由 capture、logic analyzer、analyzer burst、model pulse 四个使用方调用；`docs/sync/SYNC_IO_ARCHITECTURE.md` 已记录该仲裁方式 |

## 4. 剩余建议

按"能否立刻换取可测量余量"排序。前两项是当前唯一量级明确的来源。

| 编号 | 建议 | 增净余量 | 依据锚点 | 前置条件 |
|---|---|---:|---|---|
| RAM-03 | EventBus OTA 事件载荷池槽位由 4 降到 2 | 8,192 B | `EVENT_BUS_OTA_FAST_BLOCK_DEPTH`（`components/event_bus/inc/event_bus.h`）、`OTA_EVENT_MAX_DATA_SIZE` | 需给出 OTA 事件的实测并发深度；该数组只服务于载荷超过内联容量的事件 |
| RAM-05 | 评估 CalibrationManager 捕获载荷缓冲及四个捕获环容量 | 待定（现 8,192 B + 4 × 2,048 B） | `calibration_capture_payload_storage_t` 等（`components/calibration_manager/src/calibration_manager.c`） | 需校准域确认并发捕获路数 |
| RAM-02 | 复核 RefMem 三份事务镜像的单份容量 | 待定（现三份合计 24,576 B） | `REQMEM_TABLE_IMAGE_BUFFER_SIZE`（`components/distributed_refmem/inc/refmem_table_registry.h`） | 确认该上限是否被 System Pack 的真实上限绑定 |
| RAM-04 | 复核 RefMem 64 KiB 表几何的必要性 | 待定 | `DISTRIBUTED_REFMEM_TABLE_SIZE` 及 `refmem_vector_table.c` 的尺寸断言、`refmem_vector_table_validate_directory()` 的连续覆盖约束 | 属跨域契约变更，须先在 `docs/check/DOCS_REGISTRY.md` 登记并完成跨域核验 |
| RAM-11 | 为 DPLL/VDC 常驻对象设一条预算线 | — | §2.1 的增量归因（三个子系统占全部增量 96%） | 需 VDC 域认可口径；本条不回收 RAM，用于防止余量被逐切片蚕食 |

## 5. 一致性修正

| 编号 | 建议 | 依据锚点 | 说明 |
|---|---|---|---|
| RAM-06 | 处理运行期堆增长与 `SCRATCH_X` 相邻的问题 | `linker/rp2350_app_slot_a.ld` 的 `__HeapLimit`、`__StackLimit` 与 `ASSERT(__StackLimit >= __HeapLimit, ...)`；`__StackOneTop` 所在区 | 显式断言因两个符号取值相同而恒真；静态越界由链接器区域检查兜住，无保护的是运行期 `sbrk` 可增长到 `SCRATCH_X` 起址（core1 栈），中间既无 guard 也无 MPU 隔离 |
| RAM-09 | 对齐 `docs/sync/SYNC_IO_ARCHITECTURE.md` 的 persona 兼容矩阵与 workspace 资源模型 | `SYNC_IO_PERSONA_WORKSPACE_*` 枚举、`sync_io_persona_compatible()` 的 `workspace_mask` 按位与判定 | 四个 persona 共用同一个 workspace 位，矩阵中的"条件并发"在该模型下恒不成立；共享区已有独立租约，但 persona 准入层尚未对齐 |
| RAM-10 | 为 core0 主栈补水位观测或边界检查 | `linker/rp2350_app_slot_a.ld` 的 `.stack_dummy` / `__StackBottom`、`config/freertos/FreeRTOSConfig.h` 的栈溢出检查开关 | 栈溢出检查只覆盖 FreeRTOS 任务栈，不覆盖 core0 主栈 |

## 6. 已评估并排除（不建议作为回收项）

| 对象 | 结论 | 依据 |
|---|---|---|
| FreeRTOS 静态堆 `ucHeap` | 不以下调容量作为回收手段 | `docs/arch/RTOS_HAOFV_TODO.md` 的 P0-RAM 记录：更低 heap 候选曾导致启动风险，已明确不再只凭 map 数字下调 |
| 波形三缓冲 `s_waveform_buffers` | 记录数与份数均已到界，不可再缩 | 三份是流水线最小配置（在采／在等／在写）；每段记录数受 RAM 而非 SD 写事务限制，且已按 A/B 双镜像约束压缩过一次 |
| 波形三缓冲与 SYNC_IO arena 合并 | 不成立，勿再提案 | 见 §2.4：DMA 环形尺寸必须是 2 的幂；且波形采集期间 arena 必然在线 |
| StorageAO 写事务缓冲 `s_write_buffer` | 已是多域复用的单份场地，无重复副本 | `STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES` 直接取自 `STORAGE_MANAGER_FILE_WRITE_MAX_BYTES`，波形分段等写入路径按序复用同一份 |

## 7. 复核命令

```powershell
python tools/ram_budget_check/ram_budget_check.py out/build/dpll-fragment-spacing-r1/DHRT100.elf.map --profile release
python tools/ram_budget_check/ram_budget_check.py out/build/dpll-fragment-spacing-r1/DHRT100_B.elf.map --profile release
```

净余量 = 输出的 `link_free_bytes` 减去 2,048 B。该值接近 0 时，下一个 DPLL/VDC 切片就可能
直接链接失败，不再是门禁问题而是构建问题。

## 8. 边界声明

- 本文档的容量、增量与归因均为指定构建产物的快照，非事实源；§2.1 的聚合口径与门禁口径
  扫描范围不同，不可直接与 `link_free_bytes` 对账。
- 建议条目的量级是评审估算，不是已批准的变更；预期收益为算术推算，不是实测值。
- `RAM-02`～`RAM-05` 涉及跨域语义或对外可观测容量，落地前应按
  `docs/check/DOCS_REGISTRY.md` 的规则判断是否需要登记为契约并做跨域核验。
- release profile 门禁（48 KiB）与 debug 门禁量级（32 KiB）本次均不作为验收条件。
- 本篇不改变 DPLL/VDC 的主任务优先级：RAM 条目是使能条件，不替代
  `docs/vdc/VDC_DOMAIN_TODO.md` 与 `docs/vdc/VDC_TASK_PROGRESS.md` 的既有状态。
