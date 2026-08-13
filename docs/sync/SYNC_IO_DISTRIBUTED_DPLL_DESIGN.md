# RP2350 分布式 DPLL 同步触发实施方案

Status: Active
Domain: TRIGGER
Canonical: `docs/sync/SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md`
Related: `docs/sync/SYNC_IO_RESOURCE_PLAN.md`, `docs/trigger/TRIGGER_SYNC_TODO.md`, `docs/arch/MULTICORE_PARTITION_PLAN.md`
Last updated: 2026-07-07

本文档把 `docs/arch/RTOS_DISTRIBUTED_TRIGGER_0614_SUMMARY.md` 中的 DPLL、未来预约触发和反馈校准思想，收敛为当前 RP2350_TRIG 工程可以分阶段落地的多板原型方案。

目标不是传输大数据，而是在多块 RP2350 板之间建立可校准的虚拟 DC 时间轴，并让每块板在本地以 PIO 输出确定性触发边沿。

## 设计结论

- 多板之间不依赖通信包到达时刻直接触发。
- 主节点计算未来触发时刻 `T_fire_base`，各节点本地换算为自己的 PIO 倒计时后到点输出。
- AUX 环路用于时间同步、链路延迟标定、`T2_i` 回读和 DPLL 反馈，不作为正常触发动作的串行等待链。
- RS-485/RS-422 芯片只作为高速差分物理层使用；每段点对点、固定方向、常使能，不做共享总线仲裁。
- PIO 只做硬实时边沿层；DPLL、异常样本剔除、权重平均和补偿表更新放在 CPU 或上位机。

## 与原技术报告的映射

| 原报告概念 | Zynq/EtherCAT 方案 | RP2350 原型映射 |
|---|---|---|
| DC 时间轴 | EtherCAT DC + FPGA 1 ns counter | 每板本地 tick + 软件 `offset/rate` 虚拟 DC |
| `T_fire_base` | 主站 DPLL 生成未来 DC 绝对时间 | A 板或上位机 DPLL 生成未来虚拟 DC tick |
| `T_fire_i` | `T_fire_base + Δt_i` | 每板按本地补偿表换算为 PIO 倒计时 |
| PDO 预约队列 | EtherCAT PDO 周期下发 | USB/Ethernet/RS-485 帧下发，必须提前到达 |
| FPGA 本地比较 | PL 内 DC compare | PIO 短窗口倒计时/比较输出 |
| TDC/T2_i 回读 | FPGA TDC 捕获完成脉冲 | PIO 捕获相对 tick，CPU 扩展为虚拟 DC 时间戳 |
| `e_i/e_c` | 多节点反馈残差分解 | 主节点收集时间戳后计算残差并更新 DPLL |

RP2350 原型不承诺原报告中的亚 ns TDC 或 `e_j RMS 200 ps`。当前工程已验证 `clk_sys=250 MHz`，PIO tick 为 4 ns，`SEQ_STEP` 输入到输出约 12 ns，适合作为百 ns 级分布式触发原型。

## A0-A3 业务拓扑

当前 RP1200波导天线测试原型按四类板卡角色组织：

```text
A0: 转台板卡 / Master / DPLL / ring origin
    接收转台 TTL 位置脉冲，计数并生成位置触发事件。

A1: DUT 板卡
    接收位置触发事件，按当前链路枚举状态切换 DUT 侧 SP8T 开关。

A2: 馈源板卡
    接收位置触发事件，按当前链路枚举状态切换馈源极化。

A3: 网分板卡
    等待本轮链路枚举切换完成后触发网分测试，捕获网分 REDY/READY 后结束本轮。
```

AUX 环路按固定方向连接：

```text
A0 CAL_OUT -> A1 CAL_IN
A1 CAL_OUT -> A2 CAL_IN
A2 CAL_OUT -> A3 CAL_IN
A3 CAL_OUT -> A0 CAL_IN
```

环路用于同步、状态传递和反馈回读；正常业务触发仍由各板本地 PIO 到点输出。推荐 AUX 引脚固定如下：

| 语义 | GPIO | 方向 | PIO 资源 | 说明 |
|---|---:|---|---|---|
| `CAL_IN` | 26 | 输入 | `pio2/sm0` | 校准/同步/反馈输入，复用产品 AUX0 |
| `EXT_CLK_IN` | 27 | 输入 | `pio2/sm1` | 后续外部参考输入预留，第一阶段不用 |
| `SYNC_CLK_OUT` | 28 | 输出 | `pio2/sm2` | 后续同步时钟输出预留，第一阶段不用 |
| `CAL_OUT` | 29 | 输出 | `pio2/sm3` | 校准/同步/反馈输出，复用产品 AUX3 |

电气连接建议：

```text
GPIO29 -> high-speed RS-485/RS-422 driver -> twisted pair
twisted pair -> high-speed RS-485/RS-422 receiver -> GPIO26
```

实现规则：

- 每段链路是点对点单向链路，不把多块板挂在同一对 A/B 线上。
- `DE` 固定使能，`/RE` 固定使能，不做半双工方向切换。
- 接收端按线缆阻抗做 100/120 ohm 端接。
- 先以 12.5 Mbps 或 20.833 Mbps 验证。20.833 Mbps 对应 250 MHz / 12，比强行 20.000 Mbps 更适合 PIO 整周期时序。

## 业务时序

一轮完整测试以 A0 收到转台 TTL 位置脉冲开始，以 A0 收到 A3 返回的网分 REDY/READY 结束。

```text
1. A0 POS_IN 收到转台 TTL 位置脉冲。
2. A0 PIO 捕获边沿并计数，生成 position_count 和 sequence_id。
3. A0 根据 DPLL/补偿表生成本轮 T_fire_base，也可在第一阶段直接使用位置脉冲到达时刻作为 T_fire_base。
4. A0 发送 POS_TRIG(seq, position_count, link_index, pol_index)。
5. A1 收到 POS_TRIG，切换 DUT SP8T，满足 settle_ticks 后追加 A1_DONE。
6. A2 收到 POS_TRIG/A1_DONE，切换馈源极化，满足 settle_ticks 后追加 A2_DONE。
7. A3 收到 A1_DONE + A2_DONE 后，输出 VNA_TRIG 触发网分测试。
8. A3 捕获网分 REDY/READY 输入，记录 T2_vna，并发送 MEAS_DONE(seq, T2_vna, status)。
9. A0 收到 MEAS_DONE 后，更新残差、统计和链路枚举状态，进入下一轮。
```

第一阶段不要求在环路上等待每块板“实时串行触发”下一块板。A1/A2/A3 的真实动作应提前装载或在收到 `POS_TRIG` 后用本地 PIO 定时输出；环路传递的 DONE/READY 用于闭环确认和推进下一轮。

建议状态机：

| 状态 | 所属板 | 入口 | 出口 |
|---|---|---|---|
| `WAIT_POS` | A0 | 等待转台 TTL 位置脉冲 | 捕获 POS 边沿，生成 `seq` |
| `DISTRIBUTE_POS` | A0 | 位置脉冲有效 | 发出 `POS_TRIG` 帧 |
| `DUT_SWITCH` | A1 | 收到 `POS_TRIG` | SP8T settle 完成，置 `A1_DONE` |
| `FEED_SWITCH` | A2 | 收到 `POS_TRIG` | 极化 settle 完成，置 `A2_DONE` |
| `VNA_TRIGGER` | A3 | `A1_DONE && A2_DONE` | 输出网分触发 |
| `WAIT_VNA_READY` | A3 | 已触发网分 | 捕获 `REDY/READY` |
| `ROUND_DONE` | A0 | 收到 `MEAS_DONE` | 更新 DPLL/补偿/枚举，回到 `WAIT_POS` |

若 A1/A2 任意一个超时，A3 不触发网分，返回 fault frame。A0 将该 `seq` 标记为无效样本，不进入 DPLL 环路滤波。

## 时间模型

每块板维护一个本地单调 tick：

```text
local_tick = clk_sys tick, 250 MHz 时 1 tick = 4 ns
```

CPU 维护虚拟 DC 映射：

```text
dc_tick = local_tick * rate_q32 + offset_tick
```

其中：

- `offset_tick`：本板本地 tick 到主节点虚拟 DC 的相位偏移。
- `rate_q32`：本板本地时钟相对主节点的频率修正。
- `Δt_i`：本节点固定/慢变链路补偿，包括驱动器、线缆、接收阈值、输出通道和设备动作补偿。

PIO 不直接维护完整 64-bit DC counter。可实施边界如下：

- CPU 负责 64-bit 时间轴、预约队列和 DPLL 数学。
- PIO 负责短窗口相对计时、边沿捕获、固定延迟转发和到点输出。
- CPU 提前把 `T_fire_i` 换算成相对倒计时 `delta_ticks`，装载给 PIO。

## CAL_RING 帧与时间戳

校准帧的第一个有效边沿是时间基准；后续比特只用于携带序号和校验，不参与决定触发时刻。

第一阶段帧格式：

```text
edge0       : 时间戳边沿
payload32   : uint32_t sequence_id
crc8        : payload CRC
idle_gap    : 帧间隔，防止环路自激
```

只传一个 `int32` 时，建议定义为：

```c
typedef struct {
    uint32_t sequence_id;
} cal_ring_payload_t;
```

后续可扩展：

```c
typedef struct {
    uint8_t  frame_type;
    uint8_t  node_id;
    uint16_t flags;
    int32_t  value;
    uint32_t sequence_id;
    uint8_t  crc8;
} cal_ring_frame_t;
```

A0-A3 业务帧类型建议：

| 帧类型 | 发送方 | 接收/处理方 | 作用 |
|---|---|---|---|
| `SYNC` | A0 | A1/A2/A3 | 虚拟 DC 时间轴同步和 offset/rate 更新 |
| `POS_TRIG` | A0 | A1/A2/A3 | 位置触发事件，携带 `seq/position_count/link_index/pol_index/T_fire_base` |
| `A1_DONE` | A1 | A2/A3/A0 | DUT SP8T 已切换完成 |
| `A2_DONE` | A2 | A3/A0 | 馈源极化已切换完成 |
| `VNA_TRIG` | A3 本地事件 | A3 输出到网分 | 触发网分测试，不一定需要在环路上传输 |
| `MEAS_DONE` | A3 | A0 | 网分 `REDY/READY` 已捕获，携带 `T2_vna/status` |
| `FAULT` | 任意板 | A0 | 超时、CRC 错误、late、开关未完成或 READY 未返回 |

`POS_TRIG` 建议最小字段：

```c
typedef struct {
    uint32_t sequence_id;
    uint32_t position_count;
    uint16_t link_index;
    uint16_t pol_index;
    uint64_t t_fire_base;
    uint32_t flags;
} pos_trig_frame_t;
```

`A1_DONE/A2_DONE/MEAS_DONE` 至少携带同一个 `sequence_id`，确保 A0 能把各板反馈归入同一轮测试。

建议从简单 NRZ 固定 bit time 开始：

| 目标速率 | PIO cycles/bit @250 MHz | 说明 |
|---:|---:|---|
| 12.5 Mbps | 20 | 推荐首版，裕量大 |
| 16.667 Mbps | 15 | 可选 |
| 20.833 Mbps | 12 | 推荐高速档 |
| 25 Mbps | 10 | 对收发器和走线要求更高 |

## PIO 实时职责

### `cal_forward`

用于环路标定和同步帧转发：

```text
wait edge on CAL_IN
capture relative tick
optional fixed delay N ticks
emit CAL_OUT pulse / frame edge
push capture result to RX FIFO
```

预估：

- 输入边沿到输出边沿：约 12-40 ns，取决于 PIO 指令数和脉冲格式。
- 调节粒度：4 ns。
- 链路总延迟另加差分 driver/receiver 和线缆传播延迟。

### `cal_capture_window`

用于相对时间戳捕获。PIO 在 CPU arm 后从窗口计数值开始递减，捕获边沿时 push 剩余计数。实际分辨率取决于 PIO 检测循环的指令数，不一定等于 1 个 `clk_sys` tick：

```text
X = capture_window_units
loop:
  sample CAL_IN
  if edge: push X
  X--
```

CPU 根据 `capture_window_units - X` 和该 PIO 程序的 `loop_cycles` 扩展成：

```text
captured_delta_ticks = (capture_window_units - X) * loop_cycles
rx_dc_tick = arm_dc_tick + captured_delta_ticks + channel_calibration
```

该方式避免 PIO 读取系统 timer 的不可实现问题。窗口长度由 CPU 提前设置；32-bit 窗口按 250 MHz 和 1-cycle 等效单位时覆盖约 17 秒，若检测循环为 2-3 cycle，则覆盖更长但分辨率相应变粗。首版应在 PIO 源码中显式记录 `loop_cycles` 并用示波器标定。

### `local_fire`

用于本地预约触发：

```text
pull delta_ticks
count down delta_ticks
set trigger output high
hold pulse_width_ticks
set trigger output low
```

使用约束：

- CPU 必须提前装载，保证 `delta_ticks` 大于最小装载安全裕度。
- 若队列到达太晚，标记 `late`，禁止临界路径补救触发。
- 主触发输出仍使用现有 `GPIO20..GPIO23` 统一主口，AUX 环路不直接承担业务触发输出。

## DPLL 闭环

主节点或上位机运行 DPLL：

```text
输入:
  Compare Out / 编码器角度脉冲时间戳
  B/C/A 节点 T2_i 或 CAL_RING 反馈时间戳
  每节点补偿表 Δt_i

输出:
  T_fire_base[k]
  每节点 T_fire_i[k] = T_fire_base[k] + Δt_i
```

残差：

```text
e_i[k] = T_fb_i[k] - T_pred_i[k]
e_c[k] = weighted_mean(e_i[k] - b_i[k])
```

简化 PI 更新：

```text
phase_error = clamp(e_c[k], -E_LIMIT, E_LIMIT)
phase += Kp * phase_error
freq  += Ki * phase_error
```

工程规则：

- 缺失、乱序、CRC 错误、超窗、late 样本不进入环路滤波。
- `Δt_i` 只慢速更新，用于线缆、通道 skew 和温漂补偿。
- 正式采集期间只允许平滑微调，禁止突变参数。
- DPLL 角度时间残差 `e_pll` 按微秒级验收，不与 PIO 边沿 ns 级指标混用。

## 预计实时性

| 项目 | RP2350 原型预估 |
|---|---:|
| PIO tick | 4 ns @250 MHz |
| 本板输入到输出固定转发 | 12-40 ns + IO 延迟 |
| 本地触发输出粒度 | 4 ns |
| 差分链路线缆传播 | 约 5 ns/m |
| 高速 RS-485/RS-422 driver+receiver | 常见 6-30 ns，按选型实测 |
| A0-A3 环路总延迟 | 线缆和收发器相关，可标定 |
| A1/A2/A3 动作残差首版目标 | P99.9 100-300 ns |
| 虚拟 DC 同步首版目标 | P99 30-100 ns，实测冻结 |
| DPLL 角度时间残差 | 沿用报告口径，P99.9 30-50 us 级 |

如果最终要求跨板动作残差稳定小于 10 ns 或亚 ns TDC，则 RP2350 原型不够，应升级到 FPGA/TDC 或硬件时间戳以太网/EtherCAT DC 方案。

## 分阶段实施计划

### Phase 0 - 静态硬件验证

- 选定高速 RS-485/RS-422 收发器，记录 propagation delay、skew、rise/fall、支持速率。
- 用 `GPIO29 -> driver -> cable -> receiver -> GPIO26` 单段回环验证 12.5 Mbps 和 20.833 Mbps。
- 示波器测量端接、过冲、共模、电源噪声和温漂。

验收：

- 单段链路边沿无明显振铃误触发。
- 12.5 Mbps 帧 CRC 错误率为 0，连续 1e6 帧。
- 记录单段固定延迟和温漂趋势。

### Phase 1 - CAL_RING 脉冲环路

- 新增 `TRIG_MODE_CAL_RING` 或独立 `cal_ring` component。
- 使用 AUX0/CAL_IN 和 AUX3/CAL_OUT。
- 实现单脉冲 `sequence_id` 环路：A0 发起，A1/A2/A3 固定延迟转发，A0 收回。
- 每板记录 `seq/rx_tick/tx_tick/crc/status`。

验收：

- 四板连续 1e6 轮无自激、无丢帧。
- 环路总延迟稳定分布可测。
- 固定延迟重启后可复现。

### Phase 2 - 虚拟 DC 时间轴

- A0 周期发送 sync frame。
- A1/A2/A3 根据捕获边沿计算 `offset_tick` 和 `rate_q32`。
- 实现慢速 offset/rate DPLL。
- 提供查询快照：`dc_locked`、`offset_tick`、`rate_ppb`、`last_seq`、`late_count`。

验收：

- 1 kHz sync 下 A1/A2/A3 offset 收敛。
- 静态 1 小时内 offset 分布进入目标窗口。
- 人为断开/恢复链路后进入 HOLDOVER/RELOCK。

### Phase 3 - 本地预约触发

- A0 接收转台 TTL 位置脉冲并生成 `T_fire_base`，给 A1/A2/A3 下发 `T_fire_i` 或相对 `delta_ticks`。
- 各板提前装载 PIO `local_fire`。
- A1 到点切换 DUT SP8T，A2 到点切换馈源极化，A3 在 A1/A2 完成后输出 `VNA_TRIG`。
- 输出回读到本板输入或示波器统一采集，形成 `T2_i`；A3 额外捕获网分 `REDY/READY` 作为 `T2_vna`。

验收：

- 通信抖动只影响 `late_count`，不改变已装载触发边沿。
- `late_count=0` 时，A1/A2/A3 动作残差进入 100-300 ns P99.9 首版目标。

### Phase 4 - DPLL 闭环

- 接入角度/Compare Out 时间戳。
- DPLL 生成滚动 `T_fire_base[k]`。
- 用 A1/A2/A3 的 `T2_i` 和 A3 的 `T2_vna` 反馈更新共模残差 `e_c` 和慢速 `Δt_i`。
- 增加异常样本剔除和失锁判据。

验收：

- `e_pll` 和 `e_act` 分层统计，不混用。
- `e_pll` 首版按 P99.9 50 us 目标。
- `e_act` 按 A1/A2/A3 输出和 A3 网分 READY 回读实际测量闭合。

## 软件接口建议

建议先不直接暴露复杂 DPLL 参数，先暴露校准和状态：

```text
CAL:RING:STAT ON|OFF
CAL:RING:SEQ?
CAL:RING:LAST?
CAL:RING:COUN?
CAL:DC:STAT?
CAL:DC:OFFS?
CAL:DC:RATE?
CAL:FIRE:LOAD <seq>,<dc_tick_low>,<dc_tick_high>,<delta_ticks>
CAL:FIRE:STAT?
```

后续 DPLL 参数单独放到开发命令：

```text
DPLL:STAT?
DPLL:KP <value>
DPLL:KI <value>
DPLL:HOLD
DPLL:RELOCK
```

## 与现有工程的冲突点

- `GPIO26..GPIO29` 当前作为固定两收两发 AUX 功能接口；历史 `ENC_COUNT`
  开发诊断输入组 `TRIG:ENC:APIN 26` 已因硬件冻结关闭。
- `pio2/sm0` 和 `pio2/sm3` 将被 CAL_RING 占用；`ARM_IN`、`AUX3_TX/BISS_DATA_OUT` 等 AUX persona 需要与 CAL_RING 做 owner/arbiter。
- `SYNC_CLK_OUT` 运行路径已迁移到 AUX2/GPIO28；后续 CAL_RING 模式必须通过 `PIO2 + AUX` 资源仲裁与该路径互斥。历史 `MARK:*` 命令兼容到 `GPIO23/RJ45_TRIG_OUT`，不迁移到 AUX。
- PIO 不能直接读取系统 timer。所有 ns 级相对时间戳必须用 PIO 自身窗口计数、采样索引或外部 TDC 实现，再由 CPU 扩展到虚拟 DC。

## 风险与边界

- RS-485 多点总线模式不适合该方案；必须按点对点单向差分链路布线。
- 高速收发器选型比 PIO 更关键，需关注 propagation skew 和温漂，而不仅是标称 data rate。
- 只靠环路无法唯一分解每段单向延迟；需要交换方向、外部示波器或额外参考测试来拆分 A->B、B->C、C->A。
- 没有共享硬件参考时钟时，虚拟 DC 依赖周期同步和频率估计。HOLDOVER 时间越长，漂移越大。
- 如果最终产品在强 EMI 环境运行，应按原报告建议优先考虑隔离、屏蔽、光口或 FPGA/EtherCAT DC 正式方案。

## 当前推荐下一步

1. 确认硬件是否允许 `GPIO29 -> 差分驱动`、`差分接收 -> GPIO26`。
2. 选定 20 Mbps 以上、低 skew 的 RS-485/RS-422 收发器。
3. 先做 Phase 0/1：单段回环和 A0-A3 CAL_RING 脉冲环路。
4. 实测环路延迟分布后，再决定 Phase 2 的 sync rate、DPLL 带宽和补偿表格式。
