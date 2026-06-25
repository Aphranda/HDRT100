# 同步触发系统待办事项

本文档用于跟踪同步触发系统从当前 PIO IO 驱动，完善到工业产品级触发子系统所需的剩余工作。

## 评审补充待办（2026-06-25）

### P0 - 功能阻塞问题

- [x] 修复 `ENC_COUNT` 单次触发后卡死。 (2026-06-25)
  修复：新增 DMA ch1 (`&s_enc.target` → PIO TX FIFO, DREQ 节拍), `transfer_count=0xFFFFFFFF`,
  IRQ handler 在耗尽时自动重启。DMA 在 `disarm` 时 abort。
  
- [x] 修复 `SEQ_STEP` DMA 连续环回不成立的问题。 (2026-06-25 修复，2026-06-26 重构)
  原方案：ring buffer + ISR 重启。RP2350 DMA ring buffer 对 struct 成员地址对齐不兼容，
  长 gate 下回绕后 DMA 读到错误地址导致死锁。
  最终方案：去掉 ring buffer，ISR 中手动重置 `read_addr` 到 `seq_table_addr`，
  再写 `al1_transfer_count_trig = seq_length`。经验证 10 MHz 连续 10 轮零失败。

- [x] 修复 `gate_enabled` 下 `SEQ_STEP` 触发源选择失效。 (2026-06-25)
  修复：`seq_step_program_init_common()` 在 gate 模式下计算触发源在 GPIO16-19 组内的偏移量，
  将 `seq_step_gated` 的 wait 指令 pin index 打到正确位置。`sync_io_seq_step_arm()` 增加校验：
  gate 模式要求 `trigger_pin` ∈ [16,19]，否则返回 false。

### P1 - 行为与接口一致性问题

- [ ] 让 `ENC_COUNT` 引脚配置真正生效。
  现状：`enc_a_pin`、`enc_b_pin`、`enc_z_pin` 和 SCPI 接口表面可配，但 ARM 仍固定使用 `GPIO16..GPIO19`。
  处理方向：要么实现真实可配置引脚，要么在实现到位前收窄公开 API 和文档承诺。

- [x] 修复 `PCNT_CLEAR` 统计累计逻辑。 (2026-06-25 已修复)
  修复：`enc_total += enc_count` 现在在 `enc_count = 0` 之前执行，确保清零前的值被累计。

- [x] 对齐文档与实现。 (2026-06-26 已更新)
  本文档已更新，反映最新实现状态（ISR 手动 read_addr 复位、64-bit 计数、无锁原子快照等）。

## 当前基线

- [x] 为同步触发系统预留全部 RP2350 PIO 状态机资源。
- [x] 实现 `GPIO16..GPIO19` 的 4bit PIO 输入采样。
- [x] 实现 `GPIO20` 主触发脉冲输出。
- [x] 实现 `GPIO21` 第二路脉冲输出。
- [x] 实现 `GPIO22` 同步时钟输出。
- [x] 实现 `GPIO23` Marker 脉冲输出。
- [x] 实现 `GPIO26/GPIO27/GPIO28/GPIO29` 四路 `pio2` 辅助 IO。
- [x] 状态灯不再占用 PIO 资源。

## P0 - 核心触发行为

- [x] 新增 `components/sync_trigger/`，作为 `components/sync_io/` 上层的触发业务控制组件。 (2026-06-25)
  实现：`trigger_fb.c`（TriggerFB ECC 表驱动，58 条规则覆盖 6 状态×20 事件），
  `sync_trigger.c`（TriggerAO 事件队列），`trigger_vector.h`（域向量黑板），
  `trigger_measure.c`（门控自检，64-bit 无锁原子计数）。

- [x] 定义触发状态：`IDLE`、`SEQ_CONFIGURED`、`SEQ_ARMED`、`ENC_CONFIGURED`、`ENC_ARMED`、`FAULT`。 (2026-06-25)
  比原计划多一个中间状态（SEQ_CONFIGURED），ARM 前可修改配置。

- [x] 定义触发配置结构体：模式、边沿、极性、延时、脉宽、重复次数、burst 间隔。 (2026-06-25)
  实现为 `trigger_vector_t`（176 字节），含 seq_table[256]、seq_length、seq_width、
  trigger_source_pin、edge、gate_enabled、safe_state、ENC/PCNT 全套配置、
  即时脉冲参数、sync_io 状态快照。

- [x] 实现软件 ARM/DISARM 接口。 (2026-06-25)
  通过 SCPI `TRIG:ARM` / `TRIG:DIS`，经由事件投递 → TriggerFB ECC 表驱动状态转移。

- [x] 实现输入上升沿、下降沿触发判定。 (2026-06-25)
  通过 `seq_step.pio` 的 wait 指令极性交换实现，PIO 150 MHz 采样。

- [ ] 实现高电平有效、低电平有效的电平触发模式。

- [ ] 将 `ARM_IN` 接入触发状态机。
  现状：GPIO17 已定义 `BOARD_SYNC_ARM_IN_PIN`，只做了 pull-down，未接入 TriggerFB。

- [x] 将 `GATE_IN` 接入触发条件判定。 (2026-06-25，基础版)
  已实现：`seq_step_gated` PIO 程序（4 指令），TriggerFB 中 `gate_enabled` 开关，
  SCPI `TRIG:GATE ON/OFF`。尚未实现门控拒绝次数统计和输出抑制逻辑。

- [x] 增加触发统计计数。 (2026-06-25)
  实现：trigger_count、output_count、missed_count、rollover_count (64-bit)、
  error_code (10 种)、enc_count、enc_rev_count、enc_total、fault_timestamp_ms (接口预留)。

## P1 - 时序和输出功能

- [x] 实现输入触发后的延时输出。 (2026-06-25)
  通过 `sync_io_fire_pulse_us()` / `sync_io_fire_pulse_out_us()` / `sync_io_fire_marker_us()`，
  PIO `sync_pulse` 程序（5 指令）实现硬件精确定时。

- [x] 实现可配置输出极性。 (2026-06-25)
  `TRIG_SAFE_ZERO` / `TRIG_SAFE_ONE` 安全输出态，SCPI `TRIG:SAFE 0/1`。

- [x] 实现 `TRIG_OUT`、`PULSE_OUT`、`MARKER_OUT` 的可配置脉宽。 (2026-06-25)
  SCPI: `TRIG:WIDT` / `PULS:WIDT` / `MARK:WIDT`，µs 精度。

- [ ] 实现 burst 输出：重复次数和脉冲间隔。

- [ ] 为关键内部事件增加同步 Marker 脉冲输出。

- [ ] 增加 `EXT_CLK_IN` 外部时钟输入模式。
  现状：GPIO18 已定义 `BOARD_SYNC_EXT_CLK_IN_PIN`，只做了 pull-down。

- [x] 增加 `SYNC_CLK_OUT` 输出时钟分频配置。 (2026-06-25)
  SCPI: `OUTP:CLOC:FREQ <Hz>`，PIO `sync_clock` 程序（2 指令）实现。

- [ ] 处理输出忙状态下多个触发源同时到来的冲突策略。

## P1 - 采样和 DMA

- [x] 增加 DMA 连续缓冲，用于持续输入采样。 (2026-06-25 实现，2026-06-26 重构)
  当前方案：ISR 手动 read_addr 复位 + al1_transfer_count_trig 重启。
  不使用 DMA ring buffer（RP2350 对齐不兼容）。seq_len 默认 256，64-bit 内部计数。
  经验证 10 MHz 稳定，ISR 频率 39 kHz，CPU 负载 <1%。

- [x] 增加采样启动/停止接口。 (2026-06-25)
  `sync_io_start_capture()` / `sync_io_stop_capture()`，SCPI `SAMP:STAT ON/OFF`。

- [x] 增加采样溢出检测和恢复机制。 (2026-06-25)
  `dropped_capture_words` 计数器，SCPI `STAT:SYNC?` 可查询。

- [ ] 为触发事件关联采样时间戳。

- [ ] 定义 4bit 输入采样数据的打包格式。

- [ ] 增加可选的触发前、触发后采样窗口。

## P2 - 健壮性和诊断

- [x] 为 `sync_trigger` 和 `sync_io` API 增加明确错误码。 (2026-06-25)
  10 种 error_code：1=非法 seq 参数、2=资源忙、3=PIO/DMA 配置失败、4=硬件运行中丢失、
  10=ENC target 为 0、11=非法编码器引脚。

- [x] 增加输出引脚硬件自检，预留可回环测试方案。 (2026-06-26)
  GPIO22 (SYNC_CLK_OUT) → GPIO16 (TRIG_IN) 回环已验证，`tools/trigger_meas/loopback_test.py`。

- [x] 增加忙状态卡死超时检测。 (2026-06-25，基础版)
  `fb_seq_armed_service()` 检测 `sync_io_seq_step_is_running()` 返回 false 时切换 FAULT。
  `fault_timestamp_ms` 字段已预留，实时时间戳逻辑尚未实现（标记 TODO）。

- [x] 增加故障锁存和清除故障接口。 (2026-06-25)
  `TRIG_EVENT_CLEAR_FAULT` → `fb_fault_clear()` → 状态回 IDLE。

- [x] 增加运行状态快照，供 UI 和通信协议读取。 (2026-06-25)
  `STAT:TRIG?`（9 字段）和 `STAT:SYNC?`（6 字段），`sync_trigger_get_vector()` 临界区保护。

- [x] 定义启动、故障、看门狗复位时的安全输出默认状态。 (2026-06-25)
  默认 safe_state = TRIG_SAFE_ZERO，所有输入加 pull-down，输出复位到 SIO 低电平。

- [ ] 增加编译期检查，避免引脚冲突和 PIO 状态机冲突。

## P2 - UI 和配置

- [x] 将 U8G2 配置页绑定到真实的 `sync_trigger` 参数。 (2026-06-24)
  `sync_config_ui.c` 三栏运行时看板：SYSTEM / TRIGGER / OTA，
  展示初始化状态、采集/时钟运行态、脉宽、丢词计数等 Trigger 摘要。

- [ ] 增加可编辑字段：模式、边沿、延时、脉宽、burst 次数、时钟输出频率。

- [ ] 增加 UI 状态显示：空闲、已 ARM、已触发、忙、故障。
  当前看板只显示 INIT/IO/CAP/CLK 摘要，未显示完整触发状态机状态。

- [ ] 增加 UI 计数显示：触发次数、丢失次数、故障次数。
  当前看板只显示 DROP 计数。

- [ ] 引入非易失性存储后，增加配置保存和加载。

## P2 - 操作系统和运行时架构

- [x] 保持触发时序链路独立于 RTOS 任务调度。 (2026-06-25)
  PIO/DMA/IRQ 硬实时旁路不进入通用调度，TriggerFB 只负责配置和状态摘要。

- [x] 输入采样、触发判定、输出控制、同步时钟由 PIO/DMA/IRQ/裸机控制。 (2026-06-25)
  `seq_step.pio`（3 指令）、`sync_clock.pio`（2 指令）、`sync_pulse.pio`（5 指令）。

- [x] 在引入 RTOS 前，先把 OSAL 作为边界层。 (2026-06-24)
  `osal_critical_enter/exit()` 保护向量读取，裸机和 FreeRTOS 双路径。

- [x] 裸机触发链路功能完整，已通过时序测量。 (2026-06-26)
  10 MHz 连续 10 轮验证全过，PIO 延迟 ~20ns，精度 <1000 ppm。

- [ ] 标注文档中哪些 API 可在 IRQ 上下文、任务上下文、UI/控制上下文调用。

- [ ] 增加非实时任务调度预算表，避免 UI 或日志影响触发服务。

## P2 - 编码器协议兼容

- [ ] BiSS-B (≤10 MHz) — 单 PIO SM: CLK 输出 + DATA 输入采样 + START 检测, ~15 指令, 可行
- [ ] BiSS-C (100 MHz) — PIO 不可行 (仅 1.5 clk_sys 周期/bit), 需外部 FPGA/ASIC (iC-MU 等)
- [ ] EnDat 2.1 (≤2 MHz) — 双 PIO SM: CLK 生成 + 半双工 DATA + 命令组装, ~25-30 指令, 可行
- [ ] EnDat 2.2 (≤16 MHz) — 临界, 2.3 clk_sys 周期/bit @150MHz, 需实验验证
- [ ] 协议模式作为 HAOFV `TRIG_MODE_BISS` / `TRIG_MODE_ENDAT` 落地

## P3 - 验证

- [x] 增加使用信号源和示波器的台架测试计划。 (2026-06-26)
  回环测试（GPIO22→GPIO16）、10 MHz 外部信号源连续 10 轮验证通过。

- [x] 测量默认 `clk_sys` 下的输入到输出延迟。 (2026-06-26)
  PIO `seq_step` 3 指令 @ 150 MHz ≈ 20 ns 输入到输出延迟。

- [x] 测量不同代表性脉宽下的输出脉宽误差。 (2026-06-25)
  验证 `sync_pulse` PIO 程序输出精度（clk_sys/1 分辨率 ≈6.67 ns）。

- [x] 测量启用 DMA 后的最大稳定输入采样率。 (2026-06-26)
  已测 10 MHz 连续稳定。理论 PIO 极限 ~50 MHz，推荐实用上限 ~30 MHz。

- [ ] 测量最大稳定输出 burst 频率。

- [ ] 将已验证工作范围和理论 PIO 极限分开记录。

- [ ] 为纯软件触发状态机逻辑增加回归测试。

## 备注

当前 `components/sync_io/` 层应保持低层定位，只负责确定性的 IO 原语。触发策略、状态、统计、配置校验和 UI 绑定，应放在更高层的 `components/sync_trigger/` 组件中。

## 更新日志

- **2026-06-26**：全部条目逐项交叉评审代码，更新完成状态。新增 5 条已完成（核心触发行为×4、DMA×1、验证×3），2 条标注为部分完成。关键改进：seq_len 256、64-bit 计数、无锁原子快照、10 MHz 连续验证。
- **2026-06-25**：初版，3 个 P0 评审补充缺陷修复完成。
