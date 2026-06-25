# 同步触发系统待办事项

本文档用于跟踪同步触发系统从当前 PIO IO 驱动，完善到工业产品级触发子系统所需的剩余工作。

## 评审补充待办（2026-06-25）

### P0 - 功能阻塞问题

- [x] 修复 `ENC_COUNT` 单次触发后卡死。 (2026-06-25)
  修复：新增 DMA ch1 (`&s_enc.target` → PIO TX FIFO, DREQ 节拍), `transfer_count=0xFFFFFFFF`,
  IRQ handler 在耗尽时自动重启。DMA 在 `disarm` 时 abort。
  
- [x] 修复 `SEQ_STEP` DMA 连续环回不成立的问题。 (2026-06-25)
  修复：`sync_io_seq_step_dma_handler()` 现在向 `dma_hw->ch[].al2_transfer_count` 回写
  `seq_length`，使 ring-wrap 后的读地址继续被 DMA 使用。ring-buffer 读地址自动回绕，
  transfer_count 由 IRQ 重启。

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

- [ ] 对齐文档与实现。
  现状：文档中关于 DMA 回填、连续循环、可配置引脚、低 CPU 介入的描述，部分超前于当前代码实现。
  规则：未落地行为保持待办状态，不提前标记为已实现。

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

- [x] 新增 `components/sync_trigger/`，作为 `components/sync_io/` 上层的触发业务控制组件。
- [ ] 定义触发状态：`IDLE`、`ARMED`、`TRIGGERED`、`BUSY`、`FAULT`。
- [ ] 定义触发配置结构体：模式、边沿、极性、延时、脉宽、重复次数、burst 间隔。
- [ ] 实现软件 ARM/DISARM 接口。
- [ ] 实现输入上升沿、下降沿、双边沿触发判定。
- [ ] 实现高电平有效、低电平有效的电平触发模式。
- [ ] 将 `ARM_IN` 接入触发状态机。
- [ ] 将 `GATE_IN` 接入触发条件判定和输出抑制逻辑。
- [ ] 增加触发统计计数：ARM 次数、触发次数、丢失次数、门控拒绝次数、故障次数。

## P1 - 时序和输出功能

- [ ] 实现输入触发后的延时输出。
- [ ] 实现可配置输出极性。
- [ ] 实现 `TRIG_OUT`、`PULSE_OUT`、`MARKER_OUT` 的可配置脉宽。
- [ ] 实现 burst 输出：重复次数和脉冲间隔。
- [ ] 为关键内部事件增加同步 Marker 脉冲输出。
- [ ] 增加 `EXT_CLK_IN` 外部时钟输入模式。
- [ ] 增加 `SYNC_CLK_OUT` 输出时钟分频配置。
- [ ] 处理输出忙状态下多个触发源同时到来的冲突策略。

## P1 - 采样和 DMA

- [ ] 增加 DMA 环形缓冲，用于持续输入采样。
- [ ] 增加采样启动/停止接口，并明确缓冲区所有权规则。
- [ ] 增加采样溢出检测和恢复机制。
- [ ] 为触发事件关联采样时间戳。
- [ ] 定义 4bit 输入采样数据的打包格式。
- [ ] 增加可选的触发前、触发后采样窗口。

## P2 - 健壮性和诊断

- [ ] 为 `sync_trigger` 和 `sync_io` API 增加明确错误码。
- [ ] 增加输出引脚硬件自检，预留可回环测试方案。
- [ ] 增加忙状态卡死超时检测。
- [ ] 增加故障锁存和清除故障接口。
- [ ] 增加运行状态快照，供 UI 和通信协议读取。
- [ ] 定义启动、故障、看门狗复位时的安全输出默认状态。
- [ ] 增加编译期检查，避免引脚冲突和 PIO 状态机冲突。

## P2 - UI 和配置

- [ ] 将 U8G2 配置页绑定到真实的 `sync_trigger` 参数。
- [ ] 增加可编辑字段：模式、边沿、延时、脉宽、burst 次数、时钟输出频率。
- [ ] 增加 UI 状态显示：空闲、已 ARM、已触发、忙、故障。
- [ ] 增加 UI 计数显示：触发次数、丢失次数、故障次数。
- [ ] 引入非易失性存储后，增加配置保存和加载。

## P2 - 操作系统和运行时架构

- [ ] 保持触发时序链路独立于 RTOS 任务调度。
- [ ] 输入采样、触发判定、触发延时、脉冲输出、Marker 输出、同步时钟、高速 DMA 路径由 PIO/DMA/IRQ/裸机控制。
- [ ] 在引入 RTOS 前，先把 OSAL 作为边界层。
- [ ] 只有在裸机触发链路功能完整且完成时序测量后，再增加 `osal/port/freertos/`。
- [ ] 后续 RTOS 任务仅用于 UI、通信、参数管理、日志、存储、诊断和非实时自检流程。
- [ ] 标注文档中哪些 API 可在 IRQ 上下文、任务上下文、UI/控制上下文调用。
- [ ] 增加非实时任务调度预算表，避免 UI 或日志影响触发服务。

## P2 - 编码器协议兼容

- [ ] BiSS-B (≤10 MHz) — 单 PIO SM: CLK 输出 + DATA 输入采样 + START 检测, ~15 指令, 可行
- [ ] BiSS-C (100 MHz) — PIO 不可行 (仅 1.5 clk_sys 周期/bit), 需外部 FPGA/ASIC (iC-MU 等)
- [ ] EnDat 2.1 (≤2 MHz) — 双 PIO SM: CLK 生成 + 半双工 DATA + 命令组装, ~25-30 指令, 可行
- [ ] EnDat 2.2 (≤16 MHz) — 临界, 2.3 clk_sys 周期/bit @150MHz, 需实验验证
- [ ] 协议模式作为 HAOFV `TRIG_MODE_BISS` / `TRIG_MODE_ENDAT` 落地

## P3 - 验证

- [ ] 增加使用信号源和示波器的台架测试计划。
- [ ] 测量默认 `clk_sys` 下的输入到输出延迟。
- [ ] 测量不同代表性脉宽下的输出脉宽误差。
- [ ] 测量启用 DMA 后的最大稳定输入采样率。
- [ ] 测量最大稳定输出 burst 频率。
- [ ] 将已验证工作范围和理论 PIO 极限分开记录。
- [ ] 为纯软件触发状态机逻辑增加回归测试。

## 备注

当前 `components/sync_io/` 层应保持低层定位，只负责确定性的 IO 原语。触发策略、状态、统计、配置校验和 UI 绑定，应放在更高层的 `components/sync_trigger/` 组件中。
