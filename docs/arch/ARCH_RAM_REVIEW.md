# ARCH RAM 余量建议（面向 DPLL/VDC 调试）

Status: Draft
Domain: ARCH
Canonical: `docs/arch/ARCH_RAM_REVIEW.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/release/RELEASE_CHECKLIST.md`
Last updated: 2026-09-14

**目标**：本篇不是独立的 RAM 收敛计划，只做**为 DPLL/VDC 调试腾出余量**所需的那部分优化。
DPLL/VDC 仍是主任务，RAM 只是它的使能条件。release RAM 门禁不作为本阶段验收条件。

本文档只保留建议条目，不归档分析过程与逐项取证。条目中的量级均为快照估算，不是事实源；
实际收益以改动后在**当前**构建上实跑 `tools/ram_budget_check/ram_budget_check.py` 的结果
为准。条目由 `RAM-NN` 编号，供各域 owner 认领；未认领并登记前不构成待办或契约。

## 0. 现状与依据（快照，非事实源）

两种口径并存：链接器门禁口径，以及 VDC 域进度记录在用的"预留堆后"口径。

| 口径 | node6 | node8 |
|---|---:|---:|
| `ram_budget_check.py` 的 `link_free_bytes` | 6,420 B | 2,660 B |
| 预留 2 KiB crt heap 后的主 RAM 余量（VDC 域口径） | 4,372 B | **612 B** |

约束侧是 node8。该口径下 612 B 意味着一次微小的静态增长就可能直接链接失败。

RAM 缺口已不止是记账问题，本域进度记录中有两次直接阻断：

| 记录 | 现象 | 当时的偿付方式 |
|---|---|---|
| `docs/vdc/VDC_TASK_PROGRESS.md` 的 `VDC-PROGRESS-20260914-011` | 容量 8 的 A 链接主 RAM **超出 2,428 B** | UI 由完整 `TriggerVector`（7,168 B）改为 52 B 紧凑副本（3,392 B），BSS 回收 3,776 B；修复后容量 6/8 预留堆后余量为 4,908 / 1,148 B |
| `docs/vdc/VDC_TASK_PROGRESS.md` 同文件另一处资源记录 | `.data` 增加 128 B 越过对齐边界，`.bss` 后移 4,096 B，**RAM 超出 3,484 B** | 把仅用于 phase entry/exit 的 context 记录移回 Flash；修复后余量 4,372 / 612 B |

即当前余量是靠**功能让步**换来的，而不是回收本来就闲置的空间。这决定了本阶段的目标：

| 项 | 值 |
|---|---|
| 本阶段目标 | 约束侧（node8）**预留堆后**余量不低于 16 KiB |
| 达成方式 | 仅 RAM-01 即可（node8 → 16,996 B） |
| 暂不追求 | debug 门禁量级（32 KiB）与 release 门禁（48 KiB） |

## A. 近期建议（为 DPLL/VDC 腾余量）

| 编号 | 建议 | 增余量 | 依据锚点 | 前置条件 |
|---|---|---:|---|---|
| RAM-01 | SYNC_IO capture DMA ring 深度由 8192 words 降到 4096 words | 16,384 B | `SYNC_IO_CAPTURE_DMA_RING_WORDS`、`SYNC_IO_CAPTURE_DMA_RING_BITS`、`SYNC_IO_CAPTURE_LATCH_SERVICE_MAX_WORDS`、`SYNC_IO_CAPTURE_LATCH_RING_SIZE`（`components/sync_io/src/sync_io.c`） | 先做 RAM-07；并证明两次 latch 服务之间的最大 DMA 积压不超过新环形容量，且逻辑分析仪的记录尺寸断言在新容量下仍成立。环形尺寸受 DMA 回绕约束只能取 2 的幂，4096 words 是下一档 |
| RAM-03 | EventBus OTA 事件载荷池槽位由 4 降到 2 | 8,192 B | `EVENT_BUS_OTA_FAST_BLOCK_DEPTH`（`components/event_bus/inc/event_bus.h`）、`OTA_EVENT_MAX_DATA_SIZE` | 纯余量项，与 DPLL/VDC 无域关联；需给出 OTA 事件的实测并发深度 |

改动后预期余量（快照，非事实源；VDC 域口径，算术推算非实测）：

| 组合 | node6 | node8 |
|---|---:|---:|
| 现状 | 4,372 B | 612 B |
| 仅 RAM-01 | 20,756 B | **16,996 B** |
| RAM-01 + RAM-03 | 28,948 B | 25,188 B |

RAM-01 单条即满足本阶段目标，可视为"为 DPLL/VDC 腾余量"的最小动作集。RAM-01 的收益面
恰好也在 VDC 观测取数链路所在的 SYNC_IO 分量上。

## B. DPLL/VDC 观测链路相关（不回收 RAM，影响调试可信度）

| 编号 | 建议 | 依据锚点 | 说明 |
|---|---|---|---|
| RAM-08 | 把波形观测路径纳入 SYNC_IO 资源租约 | `sync_io_persona_manager_claim()` 的调用点、`vdc_dpll_manager.c` 经 `sync_io_read_capture_latched()` 的取数路径 | NO5 波形观测是 VDC 的正式证据路径，但当前不持有 persona 租约，与逻辑分析仪之间无运行时仲裁；观测结果的可信度依赖这条链路不被并发抢占 |
| RAM-09 | 对齐 `docs/sync/SYNC_IO_ARCHITECTURE.md` 的 persona 兼容矩阵与 workspace 资源模型 | `SYNC_IO_PERSONA_WORKSPACE_*` 枚举、`sync_io_persona_compatible()` 的 `workspace_mask` 按位与判定 | 四个 persona 共用同一个 workspace 位，矩阵中的"条件并发"在当前模型下恒不成立；同属观测资源的准入判断依据 |

## C. 一致性修正（避免 RAM 越界被误判为 DPLL 逻辑缺陷）

DPLL/VDC 调试依赖失败归因可信。当前主 RAM 溢出既不会被链接器拦下（见 RAM-06），也没有
区域隔离，失败形态是 core1 侧的难定位异常——容易被当成环路或时序问题排查。

| 编号 | 建议 | 依据锚点 | 说明 |
|---|---|---|---|
| RAM-06 | 修正链接器溢出断言与堆／scratch 边界 | `linker/rp2350_app_slot_a.ld` 中 `__StackLimit`、`__HeapLimit` 与 `ASSERT(__StackLimit >= __HeapLimit, ...)` | 两个符号取值相同，断言恒真；主区顶端紧邻 `SCRATCH_X`（core1 栈） |
| RAM-07 | 为 SYNC_IO arena 的容量常量补编译期一致性断言 | `SYNC_IO_SHARED_WORKSPACE_WORDS`（`components/sync_io/inc/sync_io_persona_resources.h`）、`SYNC_IO_CAPTURE_DMA_RING_WORDS`、`SYNC_IO_CAPTURE_DMA_RING_BITS`（`components/sync_io/src/sync_io.c`） | 三个独立宏之间无 `_Static_assert` 绑定；只改其一不会报警。**RAM-01 的前置动作** |
| RAM-10 | 为 core0 主栈补水位观测或边界检查 | `linker/rp2350_app_slot_a.ld` 的 `.stack_dummy` / `__StackBottom`、`config/freertos/FreeRTOSConfig.h` 的栈溢出检查开关 | 栈溢出检查只覆盖 FreeRTOS 任务栈，不覆盖 core0 主栈 |

## D. 暂缓（与 DPLL/VDC 无直接关系）

| 编号 | 建议 | 量级 | 依据锚点 |
|---|---|---:|---|
| RAM-02 | 复核 RefMem 三份事务镜像的单份容量 | 待定（现三份合计 24,576 B） | `REQMEM_TABLE_IMAGE_BUFFER_SIZE`（`components/distributed_refmem/inc/refmem_table_registry.h`） |
| RAM-04 | 复核 RefMem 64 KiB 表几何的必要性 | 待定 | `DISTRIBUTED_REFMEM_TABLE_SIZE` 及 `refmem_vector_table.c` 的尺寸断言、`refmem_vector_table_validate_directory()` 的连续覆盖约束 |
| RAM-05 | 评估 CalibrationManager 捕获载荷缓冲的容量 | 待定（现 8,192 B） | `calibration_capture_payload_storage_t`（`components/calibration_manager/src/calibration_manager.c`） |

`RAM-04` 属跨域契约变更，须先在 `docs/check/DOCS_REGISTRY.md` 登记并完成跨域核验；
其余两项需先取得可量化依据。三者均不服务于 DPLL/VDC 的近期调试需求。

## E. 已评估并排除（不建议作为回收项）

| 对象 | 结论 | 依据 |
|---|---|---|
| FreeRTOS 静态堆 `ucHeap` | 不以下调容量作为回收手段 | `docs/arch/RTOS_HAOFV_TODO.md` 的 P0-RAM 记录：更低 heap 候选曾导致启动风险，已明确不再只凭 map 数字下调 |
| 波形三缓冲 `s_waveform_buffers` | 记录数与份数均已到界，不可再缩 | 三份是流水线最小配置（在采／在等／在写）；每段记录数受 RAM 而非 SD 写事务限制，且已按 A/B 双镜像约束压缩过一次 |
| 波形三缓冲与 SYNC_IO arena 合并 | 不成立，勿再提案 | DMA 环形尺寸必须是 2 的幂；且波形采集期间 capture DMA ring 必然在线，两者不能共用内存 |
| StorageAO 写事务缓冲 `s_write_buffer` | 已是多域复用的单份场地，无重复副本 | `STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES` 直接取自 `STORAGE_MANAGER_FILE_WRITE_MAX_BYTES`，波形分段等写入路径按序复用同一份 |

## F. 复核命令

```powershell
python tools/ram_budget_check/ram_budget_check.py out/build/dpll-node6-current/DHRT100.elf.map --profile release
python tools/ram_budget_check/ram_budget_check.py out/build/dpll-node8-current/DHRT100.elf.map --profile release
```

VDC 域口径 = 上表输出中的 `link_free_bytes` 减去预留的 2 KiB crt heap；本阶段目标按该口径
判定 node8 是否达到 16 KiB。

## G. 边界声明

- 本条目的量级是评审估算，不是已批准的变更；预期余量为算术推算，不是实测值。
- `RAM-01`～`RAM-05` 涉及跨域语义或对外可观测容量，落地前应按
  `docs/check/DOCS_REGISTRY.md` 的规则判断是否需要登记为契约并做跨域核验。
- release profile 门禁（48 KiB）与 debug 门禁量级（32 KiB）本次均不作为验收条件，不得据此
  认为 RAM 问题已闭合。
- 本篇不改变 DPLL/VDC 的主任务优先级：RAM 条目是使能条件，不替代
  `docs/vdc/VDC_DOMAIN_TODO.md` 与 `docs/vdc/VDC_TASK_PROGRESS.md` 的既有状态。
