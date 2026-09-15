# 核验提交单：TDMA 对 DPLL 的支撑能力 → HAOFV / TDMA / VDC 域

Status: Active
Domain: TDMA
Canonical: `docs/check/submissions/TDMA_CROSS_REVIEW_06.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-09-14

> 范围：以**实板 RUN 态快照**核对 TDMA 当前是否具备支撑 VDC/DPLL 的准入条件，并给出优先任务。
> 依据 `out/HardwareAcceptance/20260914/dpll-latch-direct-seed/handoff-r2/summary.json`
> 与 `.../handoff-r2-stop.json`，以及 `tdma_runtime_owner.c`、`tdma_ring_runtime.c`、
> `tdma_pio_spi_ring_adapter.c`、`tools/calibration_ring_validate/trn03_closed_loop.py` 的代码锚点。
> 本提交单只做能力判定与优先级排序，**不改变任何契约状态**，也不替代 P3 硬件验收凭证。

## 1. Review 状态字段

| 字段 | 值 |
|---|---|
| review_scope | TDMA 是否具备支撑 VDC/DPLL 的准入条件 |
| review_basis | 四板 RUN 态运行时/物理/engine 快照 + 代码锚点（**有限窗口快照，非事实源**） |
| review_verdict | `PARTIAL_SUPPORT`：**时间戳质量层已具备，同圈关联与准入层未闭合** |
| review_blocking_layer | **关联层**（origin 反馈关联链）——已从历史上的"物理层"前移 |
| review_c11 | `PENDING`（作者不得自审自批，待独立复核） |
| review_impact_on_contracts | 无状态变更；给出 P1–P5 优先任务 |
| review_date | 2026-09-14 |

## 2. DPLL 准入要求 vs 实测（逐条对照）

| DPLL 需要什么 | 实测 | 判定 |
|---|---|---|
| 硬件 latch，非软件读时刻 | `clock_latch_resolution_ns = 8`；`clock_latch_count` 318 / 6896 持续增长；`clock_latch_miss_count` 0 / 4 | ✅ |
| 分辨率 ≤ 100 ns（`VDC-DPLL-01`） | 8 ns | ✅ |
| flags 含 `HARDWARE_LATCHED` 且不含 `DIAGNOSTIC_ONLY` | `ring_timestamp_flags = 2`（`DIAGNOSTIC_ONLY`=1 已清） | ✅ |
| reference TX 与 feedback RX 均有 latch | origin 两者均非零 | ✅ |
| **同圈关联**：`feedback_rx ≥ reference_tx` 且 identity 一致 | origin `reference_tx` 比 `feedback_rx` **晚 4,540,504 ns（≈3 个周期）**；`up_tx_frame_crc32 ≠ down_rx_frame_crc32` | ❌ |
| `simultaneous_feedback_loop_evidence = 1` | `closed_loop_passed = false`、`realtime_gate_passed = false` | ❌ |
| 周期采样稳定（`periodic_interval_gate`） | **仅 origin 失败**：25/25 区间 failed；三块 follower 25/25 passed | ❌ |
| DPLL phase 自身 WCET | `max_runtime_cycles` = 40396 / 34937 / 34599 / 37373，**全部 > wcet 34000** | ❌ |
| 生产级准入 | `phases.dpll.diagnostic_only = true`、`dpll_provisional = true`、`dpll_debug_admission = true` | ❌ |

## 3. 已成立的三项（本轮最重要的正向结论）

1. **物理链路零误码**：四板 `rx_bad_frame_count = 0`、`rx_transport_bad_frame_count = 0`、
   `observed_frame_error_rate = 0.0`（帧数 2060 / 2032 / 2043 / 2051）。⇒ 传输层不再可疑。
2. **硬件 latch 成立**：8 ns 量化、计数持续增长、丢失可忽略，且 flags 由
   `tdma_runtime_owner.c:177-197` 从 `flight_clock_latch_armed` 与分辨率推导，**非硬编码**。
   这曾是长期阻塞项，现已闭合。
3. **启动屏障通过**：`stable_samples_observed = 3`（required 3）。启动期的
   `receive_segment_bitmap_incomplete` / `receive_wkc_incomplete` 属瞬态，不是稳态问题。

## 4. 主阻塞：origin 反馈关联链

`soak_validation.passed = false`，唯一错误为
`0010071E65B5CB38:periodic_interval_gate_failed`；三块 follower 全部 `passed = true`。

该门禁判据（`trn03_closed_loop.py:1637-1691`）：任一采样区间 `validate_node` 报错即失败。
origin 的 25 个区间全部失败，持续增长的是：

```text
receive_rejected_count_grew
rx_bitmap_incomplete_count_grew
receive_segment_bitmap_incomplete
receive_wkc_incomplete
```

同一链路上的两个下游症状：

```text
origin.up_tx_frame_crc32      = 3100952021
origin.down_rx_frame_crc32    = 601213647      → 同圈 identity 不一致
origin.reference_tx  − feedback_rx = +4,540,504 ns ≈ 3 × 1.5 ms 周期 → 配对顺序倒置
        → ring_last_error = 5 = TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING
```

**根因假设**：origin 的"收到返回帧 → 解析段位图 → 关联 identity → 配对时间戳"这条链不成功。
三处症状（bitmap/reject、identity 不一致、时间戳倒置）**极可能同根因**，应作为**一项**调查，
不宜拆成三条并行线。

`ring_last_error = 5` 的判定细节值得注意：`tdma_ring_runtime.c:774-776` 会区分

```c
reason = hardware_timestamp_eligible ? EVIDENCE_MISSING : TIMESTAMP_MISSING;
```

而本轮报的是 `TIMESTAMP_MISSING`。结合 flags 已合格这一事实，说明**判定被引向了时间戳侧，
而真实缺口在关联侧**——这也是为什么该字段容易被误读。

## 5. 优先任务

### P1 ★ 修 origin 反馈接收的 bitmap / reject 持续增长

- **理由**：这是 `closed_loop_passed = false` 的唯一直接原因，也是 `periodic_interval_gate`
  的唯一失败项；没有持续闭环，DPLL 无法获得连续样本流。
- **锚点**：`tdma_receive_health.c` 的段位图/WKC 判定，`tdma_flight_engine.c` 的 bitmap 聚合。
- **对照**：follower 收到同一帧结构却全部通过，说明差异在 origin 侧的校验路径而非线格式。
- **完成判据**：origin 连续多区间 `rx_bitmap_incomplete_count` / `receive_rejected_count`
  零增长，`periodic_interval_gate` 通过。

### P2 ★ 修 origin 同圈 identity 关联

- **理由**：`simultaneous_feedback_loop_evidence` 置位的唯一前提，也是 P3 的基础。
- **注意**：`identity_crc32` 系列为 FNV-1a 指纹（非 CRC），一致性只证明"同一帧实例回到 origin"。
- **完成判据**：稳态下 identity 持续一致，而非"时对时错"。

> **P1 与 P2 建议合并为一项调查**（origin 反馈关联链），避免在同根因上并行投入。

### P3 修 reference TX / feedback RX 时间戳同圈配对

- **现状**：顺序倒置 4.54 ms；`tdma_ring_runtime.c:761-773` 要求
  `feedback_rx ≥ reference_tx` 且差值 ≤ `feedback_timeout_ns`。
- **根因假设**：TX 字段每圈无条件更新，RX 字段仅在关联成功时更新 ⇒ 两者持续拉大。
- **完成判据**：两者落在同一圈内，差值 ≈ 单圈往返（亚毫秒级），`ring_last_error` 回到 0。

### P4 修 DPLL phase 自身 WCET 超限（可与 P1–P3 并行）

- **现状**：四板 `max_runtime` 全部超 `wcet 34000`；origin 另有
  `dpll_overrun_count_grew` 与 `dpll_deadline_miss_count_grew`。
- **依据**：`EXE-SAFE-01` 将 DPLL 局部 WCET/deadline 超限归为调试反馈，不隔离节点；
  但它使 DPLL 无法进入生产准入。
- **完成判据**：四板 `max_runtime ≤ wcet`，`overrun_count` 零增长。

### P5 关闭 `diagnostic_only` 与临时许可（准入门，依赖 P1–P4）

- **现状**：`phases.dpll.diagnostic_only = true`、`dpll_provisional`、`dpll_debug_admission`。
- **完成判据**：DPLL phase 标为非 diagnostic；退出 Calibration 有限期调试许可。

### 支撑项（不直接阻塞，需并行）

| 项 | 理由 |
|---|---|
| `TDMA-FLIGHT-003` 发车节拍预算显式化 | `emission_period_ns = feedback_timeout_ns`，而 `feedback_timeout` 现由 TDMA 自行乘算，违反窗口量所有权条款；P1 修复后必须有门禁防止该值再次破坏周期性 |
| `TDMA-DPLL-003` active calibration path matrix 前置门禁 | 不是当前第一阻塞，但 P3 之后立即需要，用于计算 offset/rate |

## 6. 提交内容

| 父层条款 | 符合性 | 证据 |
|---|---|---|
| `VDC-DPLL-01` 准入分辨率 ≤ 100 ns | 符合 | `clock_latch_resolution_ns = 8` |
| HAOFV-142 TDMA 是确定性 transport 唯一 owner | 符合 | 本轮未发现越权 |
| HAOFV-879 多字段事实必须 seqlock | **VIOLATED**（既有偏差） | 见 `TDMA_CROSS_REVIEW_01.md`，本提交单不重复处理 |
| `TDMA-DPLL-004` 硬件 timestamp spine | **部分符合** | 单点 latch 已合格；同圈关联未闭合 |
| `TDMA-DPLL-005` eligible parser/quality gate | **未达成** | 无连续 eligible 样本流 |
| `TDMA-PAYLOAD-002` 固定 observation 与 VDC eligible 闭环 | **未达成** | bitmap 增长与同圈关联未闭合 |
| `TDMA-RESIDENT-01` 常驻循环不变量 | **偏差**（既有） | 见 `TDMA_CROSS_REVIEW_04.md` |
| 有限窗口证据不得外推为产品结论 | 符合 | 全文数值标注"有限窗口快照，非事实源"；单窗口 26 采样、约 6.25 s |

## 7. 偏差声明

- 本提交单判定为 `PARTIAL_SUPPORT`：**支持 DPLL 的物理前提（latch/分辨率/flags/链路）已成立**，
  但**同圈关联与生产准入未闭合**，因此不得据此宣称 DPLL 可支撑或可锁定。
- 本轮证据为**单次双窗口快照**（26 个采样、约 6.25 s），不足以证明长期稳定性；
  长稳与故障注入仍归 `TDMA-DPLL-007` 与 `TDMA-HIL-003`。
- 不覆盖：正式 RAM 门禁、完整 Core1 WCET 绝对达标、真正 TDMA service blackout 取证、
  绝对 VDC 时间映射与逐圈证据保全。

## 8. Alternatives considered

- 先做 DPLL phase WCET（P4）——**拒绝作为首选**：关联未闭合时没有合格样本流，
  预算修好也无法验证 DPLL 行为。
- 先退出诊断许可（P5）——**拒绝**：在 diagnostic-only 下保留反馈是当前唯一安全的调试通道。
- 把 bitmap、identity、时间戳拆成三条并行调查——**拒绝**：三者为同一关联链的上/下游症状，
  合并调查可避免重复定位同一根因。
- 按 P1+P2（合并）→ P3 → P4 → P5 推进，并并行支撑项——**接受**。

## 9. 核验结论

- 结论：`ACCEPT_WITH_DEVIATION`（能力判定 `PARTIAL_SUPPORT`；优先任务已建立）
- 核验人：TDMA 域本次能力审计（作者）
- 本提交单不改变任何契约或任务状态

## 10. 交叉审核记录（C11，必填）

- 审核方：实板 RUN 态原始快照（`out/HardwareAcceptance/20260914/dpll-latch-direct-seed/`）
  与 `tdma_runtime_owner.c` / `tdma_ring_runtime.c` / `tdma_pio_spi_ring_adapter.c` /
  `trn03_closed_loop.py` 代码锚点的层间交叉
- 审核方式：证据交叉（快照字段 ↔ 门禁判据）+ 层间交叉（TDMA ↔ VDC 准入条款）
- 审核结论：`PASS_WITH_NOTE`（数据与锚点一致；**作者不得自审自批，本项 C11 状态为
  `PENDING`，须由独立复核方确认后方可写回登记表状态**）
- 审核日期：2026-09-14
