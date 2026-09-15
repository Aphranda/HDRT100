# 核验提交单：TDMA Core1 phase 预算归因 → HAOFV / TDMA 域

Status: Active
Domain: TDMA
Canonical: `docs/check/submissions/TDMA_CROSS_REVIEW_05.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-09-14

> 范围：`TDMA-FLIGHT-002B/002F` 的 Core1 TDMA phase 预算复核。依据为实测分项计时
> （`out/HardwareAcceptance/20260913/tdma-flight-mailbox-crc-byte/profile-r2/origin-recording.json`）、
> `components/tdma/inc/tdma_service_timing.h` 的 31 项计时枚举，以及 adapter 与 ring runtime
> 的代码锚点。本提交单只做归因与优化排序，**不改变任何契约状态**，也不替代 P3 硬件验收凭证。

## 1. Review 状态字段

| 字段 | 值 |
|---|---|
| review_scope | Core1 TDMA phase `full_phase_ticks` vs 500 µs 目标 |
| review_basis | 实板分项计时快照 + 代码锚点（**有限窗口证据，非事实源**） |
| review_verdict | 原判 `NOT_MET`（对当时 500 µs 目标）；**2026-09-14 判据修订后为 `MET`**，见 §12 |
| review_c11 | `PENDING`（本提交单作者不得自审自批，待独立复核） |
| review_impact_on_contracts | 无状态变更；仅建立优化队列 |
| review_date | 2026-09-13 |

## 2. 判据与实测

| 项 | 值 | 口径 |
|---|---|---|
| 预算目标 | 500 µs | `TDMA-PROGRESS-20260913-054`（用户确认；旧 380 µs 不再作长期硬门槛） |
| 时钟 | 250 MHz，1 tick = 4 ns | 记录内 `clock_hz` |
| 判据 | `full_phase_ticks ≤ 125000` | 由目标与时钟换算 |
| **实测** | **166821–173111 ticks = 667–692 µs** | 三块 follower 板 `peak` |
| **结论** | **未达**，缺口 **41821–48111 ticks = 167–192 µs** | — |

## 3. 实测分项（follower `peak`，单位 µs）

| 分项（计时枚举名） | A1E54920 | 2BD5090F | FB276192 | 备注 |
|---|---:|---:|---:|---|
| `full_phase_ticks` | 667.3 | 682.8 | 692.4 | 目标 500 |
| `owner_service` | 579.7 | 578.7 | 597.6 | 父项 |
| `ring_runtime` | 512.4 | 526.3 | 524.8 | 父项 |
| **`adapter`** | **413.8** | **473.5** | **422.7** | **占 62–68%** |
| ├ `rx_handoff` | 255.5 | 299.9 | 255.0 | 整个 `rx_once` |
| │ ├ `rx_capture` | 184.4 | 225.4 | 187.7 | 物理捕获，父项 |
| │ │ ├ `rx_acquire` | 87.2 | 95.4 | 81.4 | 父项 |
| │ │ │ ├ `rx_dma_observe` | 48.8 | 47.5 | 39.6 | **calls=2** |
| │ │ │ ├ `rx_locate` | 11.4 | 21.0 | 24.1 | |
| │ │ │ ├ `rx_ring_copy` | 17.0 | 17.1 | 11.0 | |
| │ │ │ └ `rx_header_check` | 0.4 | 0.4 | 0.4 | 已至极限 |
| │ │ ├ `rx_latch` | 38.9 | 55.8 | 34.5 | 板间差 1.6× |
| │ │ ├ `rx_clock` | 10.0 | 19.7 | 22.8 | |
| │ │ └ `rx_packet_copy` | 13.9 | 13.4 | 13.4 | |
| │ └ 未插桩区 | 71.1 | 74.5 | 67.3 | **`rx_handoff − rx_capture`** |
| ├ **`overlay_prepare`** | 74.6 | 66.2 | **86.1** | |
| ├ `adapter_prologue` | 17.9 | 22.6 | 25.1 | |
| └ `adapter_status` | 15.9 | 25.1 | 10.9 | |
| `ring_publish` | 28.9 | 23.5 | 35.7 | |
| `intent_dispatch` | 47.9 | 36.7 | 51.3 | 证据链未覆盖 |
| `phys_service` | 7.6 | 15.6 | 18.9 | |
| `training_gate` / `analyzer` / `accounting` / `refmem_publish` | 约 43 | 约 44 | 约 34 | 均已很小 |

## 4. 缺口归因

`adapter`（RX 捕获 + 解析 + overlay）单项即 **414–473 µs**，是整个 phase 的 **62–68%**。
其余全部子项合计不足 160 µs，且 `rx_header_check` 已降至 0.4 µs、`ring_publish` /
`training_gate` / `analyzer` / `accounting` / `refmem_publish` 均在 36 µs 以内并已多轮优化。

**因此：分项微优化已无空间；缺口等价于需要把 adapter 削减 35–40%，或把整个 phase 削减四分之一。**

## 5. 优化项（按 ROI 排序，均含代码锚点与实测收益）

| # | 优化项 | 代码锚点 | 实测收益 | 风险 |
|---|---|---|---|---|
| O1 | `reuse_overlay_tx` 的"无更新复用"早退**提到 `phys_grant_overlay` 之前** | `components/tdma/src/tdma_pio_spi_ring_adapter.c:2451-2456` | **66–86 µs**（稳态透传可归零） | 低；需保证 grant 的 DMA selection 副作用不丢 |
| O2 | `rx_dma_observe` 每 phase 两次调用分别打标签，定位贵的那一次 | `tdma_service_timing.h:35-37`；`rx_acquire` 子项 | 诊断先于优化（现值 40–49 µs / 2 次） | 极低 |
| O3 | 给 `rx_handoff − rx_capture` 未插桩区（67–75 µs）补插桩 | `tdma_pio_spi_ring_adapter.c:2259-2319`（`rx_once_impl`） | 使剩余缺口可见 | 极低 |
| O4 | `rx_latch` 快慢路径分离（板间差 1.6×，疑为超时/重试路径） | `rx_capture` 子项 | 待测；上界 21 µs | 低 |
| O5 | 核查 `intent_dispatch` 为何每 phase 36–51 µs | `tdma_service_timing.h:46` | 未知，可能可观 | 低 |
| O6 | RX 扫描改**增量游标**，替代每 phase 重扫 | `rx_acquire` / `rx_locate` 路径 | 量纲级（待 O3 插桩后评估） | 中 |
| O7 | 稳态（无更新）时 follower 的 phase 工作趋近于 0 | 架构 `TDMA_DOMAIN_ARCHITECTURE.md` 无更新透传条款 | 量纲级 | 中 |

O1 + O2 合计预期 **约 110–135 µs**，接近所需缺口的 **2/3**；其余缺口需要 O6/O7 的量纲级改动。

## 6. 方法论修正（对后续归因的要求）

1. **origin 与 follower 是两条不同 RX 路径**：`0010071E65B5CB38` 的 `peak` 中 `adapter`/`rx_capture`
   全为 0，其 RX 成本落在 `rx_parse`/`rx_evidence`/`rx_health`/`rx_fifo_publish`/`rx_complete`；
   `tdma_service_timing.h:29-30` 已注明该分工。**两条路径的数字不可横向比较。**
2. **`peak` 取各自最坏的一拍**，四个节点的 `peak` 不在同一拍上，因此只能按 role 分组统计
   （reference `peak` / follower `peak`），不得混算。
3. 归因表须以 `tdma_service_timing.h` 的 31 项枚举为固定骨架，缺项标 `N/A` 而非省略，
   否则会出现"分项下降、完整 phase 不改善"的重复投入（2026-09-13 的 28 个切片已出现该现象）。

## 7. 提交内容

| 父层条款 | 符合性 | 证据 |
|---|---|---|
| HAOFV-142 TDMA 是确定性 transport 唯一 owner | 符合 | ring runtime / adapter / payload registry 均由 TDMA owner 独占，本次复核未发现越权 |
| HAOFV-879 多字段事实必须 seqlock | **VIOLATED**（既有偏差） | adapter `get_snapshot` 裸读；偏差已在 `TDMA_CROSS_REVIEW_01.md` 登记，本提交单不重复处理 |
| `TDMA-FLIGHT-002F` 静态预算目标 | **未达** | `full_phase_ticks` 166821–173111 > 125000（500 µs） |
| `TDMA-RESIDENT-01` 常驻循环不变量 | **偏差**（既有） | 发车仍由 `resident_return_ready` + `CYCLE_BOUNDARY` 门控，见 `TDMA_CROSS_REVIEW_04.md` |
| 有限窗口证据不得外推为产品结论 | 符合 | 本提交单所有数值均标注"有限窗口快照，非事实源" |

## 8. 偏差声明

- `TDMA-FLIGHT-002F`：预算未达，缺口 167–192 µs。计划按 O1→O2→O3 顺序收敛，
  O6/O7 为量纲级后续；接受理由=优先消除已定位的最大单项并先补齐归因可见性，
  避免在未插桩区继续盲投。
- `HAOFV-879`：维持既有 `VIOLATED`，不因本提交单改变状态。
- 本提交单**不覆盖**正式 RAM 门禁、完整 Core1 WCET 的绝对达标、真正 TDMA service blackout 取证、
  绝对 VDC 时间映射与逐圈证据保全；这些仍按其原任务独立验收。

## 9. Alternatives considered

- 继续在 CRC 上投入（查表 CRC-32 等）——**拒绝**：`TDMA-PROGRESS-20260913-060` 已把每字节
  CRC 从 44 条指令降到 9 条且"未证明完整 phase 一致改善"，结论为 CRC 已非瓶颈；查表还需
  1 KB SRAM，与长期未通过的正式 RAM 门禁直接冲突。
- 直接缩减 payload 或帧型以换时间——**拒绝**：帧型、长度与 SHORT 静态布局为冻结契约，
  不得为预算放宽。
- 先做 O6/O7 量纲级改动——**推迟**：在 O3 补齐插桩、O1/O2 释放已知收益之前，
  无法判定量纲级改动的真实上界。
- 按上述 O1→O2→O3 顺序收敛并保留分项证据——**接受**。

## 10. 核验结论

- 结论：`ACCEPT_WITH_DEVIATION`（预算未达；已建立优化队列与归因可见性要求）
- 核验人：TDMA 域本次归因分析（作者）
- **review_verdict = `NOT_MET`**；本提交单不改变任何契约或任务状态

## 11. 交叉审核记录（C11，必填）

- 审核方：实板分项计时原始记录（`out/HardwareAcceptance/20260913/tdma-flight-mailbox-crc-byte/`）
  与 `tdma_service_timing.h` 枚举、`tdma_pio_spi_ring_adapter.c` 代码锚点的层间交叉
- 审核方式：证据交叉（原始 `origin-recording.json` ↔ 枚举骨架）+ 层间交叉（文档 ↔ 代码锚点）
- 审核结论：`PASS_WITH_NOTE`（数据与锚点一致；**作者不得自审自批，本项 C11 状态为 `PENDING`,
  须由独立复核方确认后方可写回登记表状态**）
- 审核日期：2026-09-13

## 12. 判据修订记录（2026-09-14）

> 本节为**追加修订**，不改写 §1–§11 的原始内容，以保留审计链（submissions 归档规则：
> 文件不删除、历史可追溯）。

### 12.1 判据漂移事实

本提交单 §2 原以 **500 µs** 为目标（源自 `TDMA-PROGRESS-20260913-054`）。其后源码把
预算改为：

| 符号 | 值 | 换算 |
|---|---|---|
| `PROJECT_CORE1_PROFILE_1500US_CYCLES` | 375000 | 1.5 ms |
| `PROJECT_CORE1_CYCLE_CYCLES` | `= PROJECT_CORE1_PROFILE_1500US_CYCLES` | 默认周期 1.5 ms |
| `PROJECT_CORE1_PHASE_TDMA_START_CYCLE` / `_END_CYCLE` | 0 / 217500 | 窗口 870 µs |
| `PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES` | 212500 | **WCET 850 µs** |

同时新增四档周期 profile：`PROFILE_1500US` / `PROFILE_5MS` / `PROFILE_10MS` / `PROFILE_15MS`。

### 12.2 修订后的判定

§3 实测 `full_phase_ticks` = 166821 – 173111（667 – 692 µs）**小于**新 WCET 212500（850 µs），
余量 39400 – 45680 ticks（**158 – 183 µs**）。故在修订判据下判定为 **`MET`**。

### 12.3 必须同时记录的性质变化（避免误读为"已优化"）

- **判定改变的原因是目标被抬高，而不是代码变快。** 实测分项数值未变；
  周期由 1 ms 放宽到 1.5 ms、phase WCET 由 380 → 500 → 850 µs **连续两次放宽**。
- **节拍语义随之变化**：周期 1.5 ms 意味着环帧率上限低于 1 ms 档，属功能性影响，
  不是纯预算调整。
- **§5 的优化队列 O1–O7 仍然有效**：余量仅 158 – 183 µs，且 §3 数据为 2026-09-13 有限窗口
  快照；`adapter` 仍占 phase 的 62 – 68%。预算放宽不改变"RX 路径是主开销"这一归因。
- 建议在 `TDMA_TASK_PROGRESS.md` 补一条明确的**预算放宽决策记录**，否则审计链在此处存在断点。

### 12.4 与 06 的关系

能力侧的后续结论见 `TDMA_CROSS_REVIEW_06.md`（DPLL 支撑能力与优先任务）。
本文的预算归因与 06 的关联层结论相互独立，不互相引用作为依据。

