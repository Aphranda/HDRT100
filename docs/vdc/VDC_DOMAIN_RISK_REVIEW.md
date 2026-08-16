# VDC / DPLL 主域风险评审报告

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_RISK_REVIEW.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`
Last updated: 2026-08-17

本文档是 `components/vdc_domain`、`components/vdc_dpll_manager`、`components/sync_io` 代码与 `docs/vdc` 文档的一次风险评审记录，用于收敛共同时间（Virtual Distributed Clock / VDC）、DPLL 锁相与硬实时 capture/fire 层中的正确性缺陷、半接线路径、文档漂移和测试缺口。所有文件:行号以评审当日（2026-08-16）的工作树为准。

---

## 1. 评审范围与方法

- 范围：`components/vdc_domain`（8 文件，约 3.7k 行）、`components/vdc_dpll_manager`（2 文件，约 1.3k 行）、`components/sync_io`（13 文件，约 4.4k 行）+ `docs/vdc`（4 篇）+ `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`。
- 方法：四组并行代码审查（vdc_domain 核心 / dpll_manager+timestamp+payload / sync_io capture-fire / 文档与代码交叉核对），叠加 host 单测复跑。
- 基线：当前分支 `feature/rtos-multicore-haofv`，最近 5 提交均为 `vdc:*`（bring-up lock quality tiers、tdma windowed selftest、capture epoch 对齐、gpio overlay coarse lock、core1 DCO mirror）。
- 范围外：`components/tdma` 共享 TDMA 调度器本体（`tdma_service.c`）未逐行深审，本次只审 VDC 侧对它的挂载（`vdc_tdma_payload`、`vdc_domain_plan_tdma_window`）与消费；建议作为后续专项。

**一句话结论**：`vdc_domain` 核心是**真实现**——DPLL servo 数学、锁状态链、门禁拒绝、wrap tracker、timestamp dictionary 均有实单测覆盖，`submit_tdma_evidence` 确实更新 clock model，跨核共享用硬件 spin lock 无撕裂；`vdc_dpll_manager` 是干净 wrapper，没有第二套 servo、没有伪造 lock。真正的风险集中在三块：**（1）核心域两个真 bug**（DCO 更新序列非单调、lock 伪锁定）、**（2）sync_io 硬实时层的边界 bug 与半接线路径**、**（3）文档大范围漂移**。本次未发现 P0 级（即时崩溃 / 不可恢复数据损坏）缺陷，最高为 P1。

---

## 2. 风险总览

| 级别 | 数量 | 主题 |
|---|---|---|
| P1 高 | 3 | DCO 更新序列非单调、accepted 计数伪锁定、seq_width 踩 GPIO |
| P2 真 bug | 6 | uint32 溢出、DMA 丢字、ISR 破坏、边界误判 |
| P2 半接线 | 12 | wrap tracker 重置、残差测量、死 IRQ、无互斥、假成功 |
| 文档漂移 | 8 | 过期状态、假任务壳、枚举/SSCI 漏项、公式不一致 |
| 测试缺口 | 3 | manager/sync_io 零单测、死状态无覆盖 |

---

## 3. P1 高风险项

### 3.1 DCO 更新序列非单调——core1 DCO 消费特性失效

[vdc_domain.c:898](components/vdc_domain/src/vdc_domain.c#L898) 的 `vdc_domain_default_dco_control()` 每次把 `dco_update_seq` 重置为 `1u`，随后 [vdc_domain.c:727-730](components/vdc_domain/src/vdc_domain.c#L727-L730) 调它并做一次 `dco_update_seq++` → 恒为 `2`；lock 状态变化时 [vdc_domain.c:500](components/vdc_domain/src/vdc_domain.c#L500) 的 `sync_dco_lock_state` 再 `++` → `3`。序列在 `2↔3` 间振荡，从不单调增长。

后果：core1 DCO 消费者 [vdc_dpll_manager.c:981-991](components/vdc_dpll_manager/src/vdc_dpll_manager.c#L981-L991) 用 `snapshot.dco.dco_update_seq == last_dco_update_seq` 判 `unchanged_count`，由于序列恒为 `2`，**每次新快照都被判为「未变化」、`accepted_update_count` 永不增长**——正是提交 `bb82117`（mirror dco consumption on core1）要验证的消费路径。正确写法已在 `vdc_domain_publish_dco_control`（[vdc_domain.c:1588-1598](components/vdc_domain/src/vdc_domain.c#L1588-L1598)）示范：先存 seq、调 `default_dco_control`、再恢复并自增；`update_clock_from_evidence` 未照做。

处置状态（2026-08-17）：已修复。`vdc_domain_update_clock_from_evidence()` 在派生 DCO 前保存 `next_dco_seq`，调用 `vdc_domain_default_dco_control()` 后恢复该序号，避免默认初始化把序号重置为 `1`。`test_dco_control_contract()` 已覆盖 accepted evidence 更新后 DCO seq 必须单调前进。

### 3.2 `accepted_sample_count` 只增不重置——锁是单调计数伪锁定

[vdc_domain.c:1698](components/vdc_domain/src/vdc_domain.c#L1698) 对 `accepted_sample_count` 只 `++`，无任何重置路径；reject 路径 [vdc_domain.c:1661-1693](components/vdc_domain/src/vdc_domain.c#L1661-L1693) 只把 state 置 `CHECKING`，计数保留；状态链 [vdc_domain.c:1727-1739](components/vdc_domain/src/vdc_domain.c#L1727-L1739) 纯由累计计数驱动。

后果：单次瞬态 reject 把 LOCKED 打到 CHECKING，但下一个 accepted sample（计数仍 ≥ `lock_sample_count`）直接跳回 LOCKED——「连续 accepted sample」从不成立。与 TODO 自身目标矛盾（[VDC_DOMAIN_TODO.md:186](docs/vdc/VDC_DOMAIN_TODO.md#L186)「连续 accepted sample」、[:55](docs/vdc/VDC_DOMAIN_TODO.md#L55)「sample count 伪锁定」）。需要在 reject 时清零计数或维护连续 streak，而不是累计值。

处置状态（2026-08-17）：已修复为“连续 accepted streak”语义。gate reject 和 servo outlier 统一清零 `accepted_sample_count`、连续质量计数和频率估计基线；恢复样本可重新进入 `INITIAL_SYNC/FREQ_LOCK/PHASE_LOCK`，但不能一帧直接回到 `LOCKED`。`test_context_accepts_samples_until_locked()` 与 `test_dpll_rejects_servo_outlier()` 已覆盖该契约。

### 3.3 `seq_width` 接受 1..8，但输出组只有 4 脚、PIO 硬编码 `out pins, 4`

[sync_io_mode_seq_step.c:17](components/sync_io/src/sync_io_mode_seq_step.c#L17) 定义 `MAX_WIDTH = 8`，[:53-54](components/sync_io/src/sync_io_mode_seq_step.c#L53-L54) 校验接受 `1..8`；但输出组只有 `BOARD_SYNC_OUTPUT_PIN_COUNT = 4`（GPIO21-24），且 [seq_step.pio:18](components/sync_io/src/seq_step.pio#L18) 指令硬编码 `out pins, 4`。

后果：width 5..8 会让 `pio_sm_set_consecutive_pindirs(..., width, true)` 静默把 **GPIO25（LCD 背光）/ 26 / 27（AUX）/ 28（SYNC_CLK_OUT）** 重配成 PIO 输出并输出损坏序列；同时 `sm_config_set_out_shift` 用请求 width、`out pins` 固定 4，两者失配使 shift/auto-pull 脱同步。需要把 width 上限钳到 4（或让 PIO 程序按 width 参数化，二选一，当前两者不一致）。

处置状态（2026-08-17）：已修复。`SYNC_IO_SEQ_STEP_MODE_MAX_WIDTH` 改为板级 `BOARD_SYNC_OUTPUT_PIN_COUNT`，当前产品为 4；`sync_io_seq_step_arm()` 的 gate 参数校验前移到 DMA/IRQ/SM 操作之前，非法 gate 不再先停硬件资源。

---

## 4. P2 真 bug（边缘/边界触发）

| 项 | 位置 | 说明 |
|---|---|---|
| uint32 溢出 | [sync_io.c:973](components/sync_io/src/sync_io.c#L973) | `word_span_ns = sample_period_ns * 8u` 在 `sample_hz==1`（1e9×8=8e9）时溢出，corrupt 采集基准时间戳与窗口匹配。 |
| DMA 丢字 | [sync_io.c:249](components/sync_io/src/sync_io.c#L249) | DMA 在两次调用间恰好写满 8192 字时 `write_index == last_write_index`，`produced` delta 判 0，静默丢 8192 字。 |
| arm 先改后验 | [sync_io_mode_seq_step.c:126](components/sync_io/src/sync_io_mode_seq_step.c#L126) | `arm` 先关共享 DMA IRQ 与 `BOARD_SYNC_OUTPUT_SM`，再校验 `trigger_pin`；非法 gate 早退时 IRQ/SM 永久停掉。 |
| ISR 被踩 | [sync_io_mode_enc_count.c:233](components/sync_io/src/sync_io_mode_enc_count.c#L233) | `get_count` 用 `pio_sm_exec(mov isr,x)` 注入，会落在运行中程序的 `in pins,3` 与 `mov osr,isr` 之间，破坏当前解码样本。 |
| 完成计数饱和 | [sync_io_model_sched.c:202](components/sync_io/src/sync_io_model_sched.c#L202) | `completion_ns[i]` 经 `saturate_u64_to_u32`，>4.29s 的 schedule 把所有脉冲提前标「已完成」。 |
| 半截基线 | [vdc_sync_io_adapter.c:63-77](components/vdc_domain/src/vdc_sync_io_adapter.c#L63-L77) | ambiguous 边沿提前 `break`，把中间样本而非末样本写进 `*last_sample_mask`；下一 word 相对错误基线产生伪边沿。 |

---

## 5. P2 半接线路径（报成功/有效但实际不生效或不可靠）

| 项 | 位置 | 说明 |
|---|---|---|
| wrap tracker 高 32 位被重置 | [vdc_domain.c:1615](components/vdc_domain/src/vdc_domain.c#L1615) → [vdc_timestamp.c:241-252](components/vdc_domain/src/vdc_timestamp.c#L241-L252) | 发布 timestamp dictionary 时 `vdc_wrap_tracker_init` `memset` 清零 `tick_hi64`；observer 播种（[vdc_dpll_manager.c:673-678](components/vdc_dpll_manager/src/vdc_dpll_manager.c#L673-L678)）的高位被抹掉。>4.29s uptime 后时间戳错 2³² ns。两路审查独立命中。 |
| 锁质量用残差非入相 | [vdc_domain.c:1701-1705](components/vdc_domain/src/vdc_domain.c#L1701-L1705) | lock 判据与 quality tier 用**校正后**残差（`phase_error + phase_offset`），非入相误差；90µs 阶跃一步校正后报 `FINE_100NS/LOCKED`，offset 统计系统性偏小。 |
| slew_limit 硬编码 | [vdc_domain.c:879](components/vdc_domain/src/vdc_domain.c#L879) | `clock.slew_limit_ppb = 50000` 永不同步 `servo.sanity_freq_limit_ppb`；DCO 校验 [:977](components/vdc_domain/src/vdc_domain.c#L977) 会拒绝收紧后的 profile。 |
| ENC_COUNT DMA IRQ 死 | [sync_io_mode_enc_count.c:147](components/sync_io/src/sync_io_mode_enc_count.c#L147) | DMA transfer count 设 `0xFFFFFFFF` 永不完成，IRQ 不触发；`fire_count`/`dma_restart_count`/overflow 检测全死。 |
| SEQ_STEP gate 绑死 +3 | [seq_step.pio:48](components/sync_io/src/seq_step.pio#L48) | gated 程序 `wait 1 pin, 3` 绑 `in_pin_base+3`，仅 `trigger_pin==4` 时 gate 正确；5/6/7 静默绑到 GPIO8/9/10。 |
| 共享 SM 无互斥 | [sync_io_mode_seq_step.c:155](components/sync_io/src/sync_io_mode_seq_step.c#L155) / [enc_count.c:117](components/sync_io/src/sync_io_mode_enc_count.c#L117) | SEQ_STEP 与 ENC_COUNT 共用 `BOARD_SYNC_OUTPUT_SM`，各自 arm 互不检查；唯一 guard 是 NDEBUG 下消失的 assert。 |
| fire 报成功 SM 已禁用 | [sync_io.c:1134/1279](components/sync_io/src/sync_io.c#L1134) | `fire_pulse_on_sm` 不检查目标 SM enabled；debug output-mask 禁用 SM 后仍返回 true，脉冲实际没出。 |
| DMA 通道未 claim | [sync_io.c:763](components/sync_io/src/sync_io.c#L763) 等 | ENC_COUNT(ch1)/SEQ_STEP(ch0)/model(ch2) 未 `dma_channel_claim`，其他子系统可能抢占同一通道。 |
| Z 脉冲 IRQ 无 handler | [enc_count.pio:46](components/sync_io/src/enc_count.pio#L46) | `irq 0` 每 Z 沿置位，但只注册了 DMA IRQ handler，转数计数无人服务。 |
| 死遥测 | [vdc_dpll_manager.c:425-427](components/vdc_dpll_manager/src/vdc_dpll_manager.c#L425-L427) | `next_base_time_l32_ns` 写入并发布，但 decode 从未读它；缺硬件 base 时回退逻辑未接线。 |
| RX self-test 超时忽略 lead | [vdc_dpll_manager.c:161-173](components/vdc_dpll_manager/src/vdc_dpll_manager.c#L161-L173) | RX 拆除超时 = `start_delay + period×count + 250ms`；1s schedule 下 ~250ms 就 disarm，而观测窗口最远可到 ~1s 后，自测永远采不到自己生成的脉冲串。 |
| wrap 边界严格 `>` | [vdc_timestamp.c:280-292](components/vdc_domain/src/vdc_timestamp.c#L280-L292) | 后退量恰为 `0x80000000` 被判为后退（非 wrap）；真前跳分支却递增 `backward_reject_count`（命名误导）。现实周期下无实际故障，仅建议文档化。 |

---

## 6. 文档与代码漂移

1. **TODO 状态过期自相矛盾**。[VDC_DOMAIN_TODO.md:101](docs/vdc/VDC_DOMAIN_TODO.md#L101)「DPLL 未真正更新 clock model」已过期——代码已实现（`vdc_domain_update_clock_from_evidence`），且同文档 P3[:173](docs/vdc/VDC_DOMAIN_TODO.md#L173) 已标 `[x]`。

2. **假任务壳**。[VDC_DOMAIN_ARCHITECTURE.md:604](docs/vdc/VDC_DOMAIN_ARCHITECTURE.md#L604) / [VDC_DOMAIN_TODO.md:97](docs/vdc/VDC_DOMAIN_TODO.md#L97) 声称 `task_vdc_sync`/`task_dpll` 状态壳已存在——全库 grep 无该符号，真实路径是 `vdc_dpll_manager_vdc_service()/dpll_service()/tdma_core1_service()`（`app.c`）。该命名还传播到 `RTOS_HAOFV_ARCHITECTURE.md`、`RTOS_HAOFV_TODO.md`、`HAOFV_MAINTENANCE_TODO.md`。

3. **lock_state 枚举漂移**。[VDC_DOMAIN_ARCHITECTURE.md:499](docs/vdc/VDC_DOMAIN_ARCHITECTURE.md#L499) 含 `LOCKING`；真实枚举（[vdc_domain.h:28-38](components/vdc_domain/inc/vdc_domain.h#L28-L38)）是 `OFF/CHECKING/INITIAL_SYNC/FREQ_LOCK/PHASE_LOCK/LOCKED/HOLDOVER/RELOCKING/FAULT`（无 `LOCKING`）。同文档状态机表（:535-545）是对的，仅此 token 过期。

4. **SCPI 命令树漏项**。[VDC_DOMAIN_ARCHITECTURE.md:569-585](docs/vdc/VDC_DOMAIN_ARCHITECTURE.md#L569-L585) 漏 `SYSTem:SYNC:VDC:DCO?`、`TDMA:STATus?`、`DPLL:OVERRide?`、`DPLL:COEFficient?`（均已注册在 `SCPI_SYNC_COMMANDS`）。

5. **目标代码形态漏文件**。[VDC_DOMAIN_ARCHITECTURE.md:606-618](docs/vdc/VDC_DOMAIN_ARCHITECTURE.md#L606-L618) 只列 `vdc_domain/vdc_clock_model/vdc_dpll/vdc_quality/vdc_timestamp`，漏实际存在的 `vdc_sync_io_adapter`、`vdc_tdma_payload`。

6. **时钟映射公式不一致**。[VDC_DOMAIN_ARCHITECTURE.md:516,522](docs/vdc/VDC_DOMAIN_ARCHITECTURE.md#L516) 写 `vdc_time64_ns = local_tick64 * tick_ns * rate_q32 + offset_ns - delay_ns`；实现 [vdc_domain.c:940-968](components/vdc_domain/src/vdc_domain.c#L940-L968) 把 `local_tick64` 当已是 ns、用 `period_adjust_ppb`（非 `rate_q32`）、且从不减 `delay_ns`（`VdcCalibrationBinding` 尚未存在）。

7. **HAOFV_VDC_DPLL_ARCHITECTURE.md 整体过期**。[HAOFV_VDC_DPLL_ARCHITECTURE.md:345-375](docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md#L345-L375) 为 08-13 Draft，其状态机（`FREE_RUN/LOCKING`）与 `vdc_vector_t` 结构体均与已实现的枚举 / `vdc_domain_snapshot_t` 不符，SCPI 段同样漏全部 `SYSTem:SYNC:VDC:*` 维护命令。

---

## 7. 测试状态

评审当日复跑 `tools/tests/run_vdc_domain_tests.ps1` 与 `run_refmem_vdc_bridge_tests.ps1`，**全部通过**。

覆盖良好的部分（`test_vdc_domain.c` / `test_refmem_vdc_bridge.c`）：DPLL servo 相位校正与 rate pull（`test_dpll_slews_phase_and_pulls_rate_after_lock`）、servo outlier 拒绝、大初始相位 acquisition 接纳、lock quality tier、wrap tracker、timestamp dictionary、TDMA payload 挂载、REFMEM→VDC envelope 桥接。

缺口：

1. **`vdc_dpll_manager.c`（41KB owner/集成层）零单测**——DCO 消费镜像、observer service、self-test 角色机、`configure_sync_io_observer*` 只靠 HIL 脚本（`vdc_gpio_lock_validate.py` 等）。这是 TASK-037「core1 消费稳定 DCO mirror」契约所在，风险最高。
2. **HOLDOVER/RELOCKING/FAULT 三态是死代码**——无路径可达、无测试；架构文档验证门禁明确要求「HOLDOVER aging」「RELOCK 成功/失败路径」，均未覆盖。
3. **`sync_io.c`（60KB）无专属 host 单测**——edge-compression / capture 时基重锚仅靠板端 HIL。

---

## 8. 处置建议与优先级

| 顺序 | 事项 | 理由 |
|---|---|---|
| 1 | 修 `dco_update_seq` 单调性（§3.1） | 已完成；后续板端需复查 `SYSTem:SYNC:VDC:DCO?` 中 `accepted_update_count` 是否随真实 evidence 增长。 |
| 2 | 修 `accepted_sample_count` 重置（§3.2） | 已完成；`accepted_sample_count` 当前按连续锁相获取 streak 使用。 |
| 3 | 钳 `seq_width` 到 4 或参数化 PIO（§3.3） | 已完成；当前绑定 `BOARD_SYNC_OUTPUT_PIN_COUNT=4`，未做可变宽 PIO 程序。 |
| 4 | 修 wrap tracker 高位重置（§5.1） | 两路独立命中，>4.29s 后时间戳整体错位，直接影响 DPLL 输入正确性。 |
| 5 | 清 sync_io 边界 bug（§4 + §5 剩余） | 溢出、丢字、ISR 破坏、SM 无互斥、假 fire——批量处理，多数改动小。 |
| 6 | 补 manager/sync_io 单测（§7.1/7.3） | 高风险集成层当前零 host 覆盖。 |
| 7 | 统一文档漂移（§6） | 先删不存在的 `task_vdc_sync`/`task_dpll` 壳描述与过期 TODO 状态，再对齐枚举/公式/SCPI 树。 |
| 8 | 专项审 `components/tdma` 本体（§1 范围外） | TDMA 是 VDC 稳定性的前置条件，`tdma_service.c` 尚未逐行审。 |
