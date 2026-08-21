# 校准域 TDMA SPI CLK 分级训练方案

Status: Active
Domain: CALIBRATION / TDMA Clock Training
Canonical: `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`
Related: `docs/calibration/README.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-08-21

本文档是校准域维护的多板 TDMA SPI CLK 训练事实源。校准域拥有 CLK/DATA/SYNC
物理测量、双向时间传递、residence、endpoint bias、path-delay candidate、统计质量、
generation/freshness 以及 EtherCAT DC 风格的训练状态和接受门禁；TDMA 只提供训练传输
persona、PIO/SM/DMA/core1 资源和窗口编排。第一阶段总结已经实现并完成四板 HIL 的
CLK 往返粗捕获；第二阶段规划使用编码 marker、PIO 过采样和板内相关匹配，把粗区间继续
缩小。本文档中的第二阶段 wire 图样、阈值和新增 SCPI 拼写仍是 candidate，必须在实现、
单元测试和 HIL 形成后再按文档登记流程冻结。第三阶段已形成板间 PIO/DMA 诊断 persona
和四板逐链路 HIL evidence，但 endpoint bias/freshness 未完成，仍不得进入 active calibration。

## 结论先行

- 第一阶段已经证明四板 CLK 可以逐节点透明转发并返回训练主节点，整圈 CLK RTT 位于
  约 `400~500 ns` 的量级。
- 第一阶段 overlap 判定只能提供 acquisition bracket；当前 timestamp flags 仍为
  `TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY`，不能提交给 VDC/DPLL。
- 第二阶段不再靠继续提高 SPI 频率缩窗。发送已知编码序列，主节点在约 `50 ns` 粗窗口内
  识别相关峰对应的 chip 索引，再用 `CLK_SYS` 过采样确定 chip 内相位。
- 训练执行必须驻留板内。SCPI 只提交 intent，core1 TDMA owner 推进状态机，PIO/DMA 完成
  边沿级动作；PC 工具只做按唯一板卡地址的维护态编排、采集和评分。
- 上电不得自动注入训练时钟。训练由显式指令触发，失败后统一回到 STOPPED；恢复普通
  DATA/CS PIO persona 后，仍需显式 START 才进入周期 TDMA。

## 校准阶段归属

线序/邻接测量、环路顺序判定、第一阶段粗 RTT bracket、码元边界解释、四主轮换结果、
质量分类和后续缩窗输入均由校准域维护。本文件是这些内容的唯一事实源；
[`CALIBRATION_DOMAIN_TODO.md`](CALIBRATION_DOMAIN_TODO.md) 维护未完成项，
[`CALIBRATION_TASK_PROGRESS.md`](CALIBRATION_TASK_PROGRESS.md) 保存带 build、拓扑和证据目录
的诊断快照。

TDMA 域只保留第一阶段所需的 transport/persona 契约：PIO/SM/DMA resource claim、CLK
burst/forward/capture 原语、core1 command slot、STOP/ARM/TRAIN/restore 时序和 raw evidence
发布。底层 transport 实现仍以 `tdma_*` 命名不代表 TDMA 拥有测量结论；TDMA 不解释
`N_low/N_high`，也不生成 path-delay、bias、residence 或 VDC/DPLL 可接受结果。

校准域拥有的 host 工具统一放在 `tools/calibration_ring_validate/` 并使用 `calibration_*`
命名。TDMA 传输辅助函数仍可由这些工具导入，但不得反向取得 topology、NO 映射或测量
结果的所有权。

## 第零阶段：线序与环路顺序测量

这里的“线序”指产品 TDMA 环中有方向的物理邻接关系，即某块板的 TX 实际进入哪块板的
RX；“环路顺序”是在邻接矩阵上形成的单一闭环顺序。单独 GPIO pin remap、极性和连接器
pinout 仍由 Hardware/SYNC_IO profile 提供 raw fact，但它们的测量接受结果作为 topology
evidence 进入校准域。

校准流程固定为：

1. 枚举当前端点并用 `*IDN?` 唯一地址建立 active board set；COM 号不进入结果主键。
2. 全部节点进入 STOPPED。按唯一地址对每个有向候选板对执行隔离 probe，一次只允许一个
   driver 和一个 receiver 进入测试 persona。
3. TDMA transport 返回 RX frame/word/edge counter、bad-header 和 timeout 等 raw evidence；
   校准域根据显式 profile 阈值生成 directed adjacency matrix。
4. 只有每个 active 节点入度和出度均为一、且从 anchor 出发恰好遍历全部 active 节点并
   回到 anchor 时，才接受为一个 closed ring。开链、分叉、重复节点、自环或多闭环均拒绝。
5. 接受后将顺序旋转到指定 `anchor_id`，生成 `ring_order` 和 `slot_map`。NO.1–NO.8 只是
   accepted topology 的操作员显示编号，不是板卡身份。
6. 只有 accepted topology 才允许通过 `SYSTem:BOARD:NO` 提交 NO 映射；写后读回，要求时
   重启再读回。TDMA START 不再隐式改写 NO。
7. 任一板卡集合、邻接、方向、profile 或 wiring 变化都使 topology freshness 失效，并使
   依赖它的 CLK RTT、path-delay 和 VDC/DPLL calibration generation 进入 stale/retrain。

当前维护入口是
`tools/calibration_ring_validate/calibration_ring_topology.py`。它输出
`measurement_domain=calibration`、pair evidence、`adjacency`、`ring_order`、`slot_map`、
NO 写入读回和可选重启读回；SCPI `SYSTem:TDMA:*` 仅作为隔离 probe 的 transport。

## 身份、拓扑与本次基线

板卡身份只使用 `*IDN?` 返回的唯一地址。COM 号是可变化的传输端点，不进入 topology、
calibration key 或报告主键。

| 逻辑位置 | 唯一板卡地址 |
|---|---|
| NO.1 | `0010071E65B5CB38` |
| NO.2 | `FB276192BEF9CCE1` |
| NO.3 | `2BD5090FE009FA2A` |
| NO.4 | `A1E549202D18ED6A` |

当前第一阶段诊断快照来自 build `20260821021250`，物理顺序为
`NO.1 -> NO.2 -> NO.3 -> NO.4 -> NO.1`。结果只对该拓扑、接线、收发器、profile 和
build 有效；其中任一项变化都要求重新训练。

第一阶段默认扫频使用 `tdma_operating_profile.c` 中 level 7、8、9 对应的
`10 -> 25 -> 30 MHz` 阶梯：10 MHz 是 acquisition/降级基线，25 MHz 是中间交叉检查，
30 MHz 是当前粗捕获高速档。更高档位不进入第一阶段默认定义，只能显式作为实验 profile
运行；低于 10 MHz 的兼容档也不作为后续训练的默认起点。

## HAOFV 执行边界

```text
SCPI/core0
  -> 有界原子 command slot
  -> TdmaSchedulerAO / core1 transport owner
  -> TDMA adapter
  -> PIO TX / PIO forwarding / PIO RX + DMA
  -> guarded/seqlock 训练 snapshot
  -> CalibrationAO 测量/统计/接受门禁
  -> SCPI status / host tool 只读采集
```

必须保持以下边界：

1. SCPI callback 不同步操作 PIO、DMA 或等待某个边沿，只校验参数并提交 intent。
2. core1 是 TDMA transport 状态和 PIO persona 的唯一 owner；CalibrationAO 拥有测量、统计、
   calibration generation/freshness 和接受门禁，core0 不参与实时转发和相关计算热路径。
3. follower 只透明再生 CLK 边沿，不解析 marker，不修改 marker。
4. master PIO 自主发送和采样；CPU service 只收割固定大小结果，不参与边沿排序。
5. snapshot 使用 guard/seqlock 发布；查询不能阻塞 core1。
6. host 工具不得通过高频 SCPI 轮询参与时间戳生成或维持 RX window。
7. 只有硬件 latch、质量门禁、重复统计全部通过后，才能清除 diagnostic-only 标志。

## 第一阶段：SPI CLK RTT 粗捕获

### 测量对象

第一阶段测量：

```text
spi_clk_round_trip_ns
  = 主节点发出 CLK marker 首边沿
  到同一 marker 绕环返回主节点 RX CLK 的时间
```

该值包含线缆、RS-422 收发器以及各 follower 的 CLK forwarding residence，但不包含
DATA、CS/frame-sync、完整帧 CRC、飞行替换、帧队列和 RTOS 解析时间。因此第一阶段不能
直接生成 TDMA 完整帧 feedback timeout。

### 当前板内实现

- `tdma_pio_spi_clk_forward`：follower 的 RX CLK -> TX CLK 逐边沿再生。
- `tdma_pio_spi_clk_burst`：master 自主发送指定 pulse count。
- `tdma_pio_spi_clk_capture`：master 捕获返回首边沿并设置 PIO IRQ。
- burst PIO 在完成点读取返回 IRQ，以硬件顺序判断返回发生在 TX done 之前还是之后。
- `tdma_pio_spi_phys_train_clock_service()` 只收割完成、超时和 snapshot，不参与边沿判定。
- `TdmaRingRuntime` 通过 command slot 接收训练请求；训练 snapshot 由
  `tdma_pio_spi_phys_get_clk_train_snapshot()` guarded 读取。

当前指令面：

```text
SYSTem:TDMA:RING:TRAIN <cycles>
SYSTem:TDMA:RING:TRAIN:STATus?
```

当前 `TRAIN <cycles>` 触发的是板内单个训练 trial，不是完整四主自动校准。完整第一阶段
仍由 `tools/calibration_ring_validate/calibration_clk_train.py` 编排，但所有实时动作和 overlap 判定已经
驻留板内。

### 完整训练流程

#### 1. 全环准备

1. 工具枚举串口，但只接受与调用参数中唯一板卡地址完全匹配的 `*IDN?` 结果。
2. 对全部 active 节点执行 `RING:STOP`。
3. 在 STOPPED 状态给全部节点 staging/apply 同一个 operating profile。
4. 写入相同 node count，并按物理顺序设置每个节点的 local slot 和当前 master slot。
5. 清除上一 epoch 的 RX/IRQ/FIFO/DMA 残留，生成新的 request sequence。

#### 2. 建立一个 master 的训练 persona

1. 先 ARM 所有 follower，再 ARM master，避免 master 注入时 follower 尚未转发。
2. follower 接受一个训练请求后进入 `FORWARDING/FORWARD_ARMED`，只做 CLK 透明转发。
3. master 保持 RX capture armed，然后接受指定 pulse count 的训练请求。
4. master 只注入一次 burst；返回 burst 不再从 master 转发，避免形成无限 CLK 环。

#### 3. 指数捕获

从 `pulse_count_start` 开始，每个 decision point 可重复 `R` 次。按硬件 overlap 结果分类：

| 分类 | 判据 | 含义 |
|---|---|---|
| `ALL_NON_OVERLAP` | `R` 次均在 TX done 后看到返回 | burst 持续时间仍短于 RTT |
| `MIXED` | 同一点既有 overlap 又有 non-overlap | 位于抖动/亚稳过渡带，不可作为单一阈值 |
| `ALL_OVERLAP` | `R` 次均在 TX done 前看到返回 | burst 持续时间已经长于 RTT |

若当前点是 `ALL_NON_OVERLAP`，pulse count 按 growth factor 增长；找到第一个
`ALL_OVERLAP` 后得到：

```text
D(N_low) <= spi_clk_round_trip_ns < D(N_high)
```

`MIXED` 必须单独记录，不能强制归入任一侧，也不能据此发布 DPLL eligible 结果。

#### 4. 逐级缩窗

1. 第一级使用指数搜索快速找到 RTT 所在数量级。
2. 第二级在 `N_low..N_high` 内二分；启用多次重复时，可逐点扫描过渡区。
3. 缩窗下限是一个有效训练 pulse/chip 周期，不是软件 timestamp 的 4 ns 名义分辨率。
4. 若返回 pulse 缺失、重复、超时或 marker 不完整，该点是 rejected sample，不得当作
   non-overlap。

#### 5. 四主轮换

依次让每个逻辑 slot 成为 master，其他节点保持 follower。每一轮都重新写 topology、重建
训练 persona，并保存唯一板卡地址、profile、baud、`N_low/N_high`、duration bracket、
mixed points、错误增量和 timestamp flags。

#### 6. 统一收尾

无论成功还是失败，工具最终都对全部节点执行 STOP。后续若进入正常 TDMA，必须重新
ARM，使 adapter 重建普通 DATA/CS PIO persona，然后由显式 START 启动。训练脚本本身不
自动 START。

### 第一阶段四板 HIL 诊断快照

下表是供电入口位于 NO.2 时的 bench 快照，不是通用硬件常量：

| SPI 档位 | NO.1 | NO.2 | NO.3 | NO.4 | 结论 |
|---|---:|---:|---:|---:|---|
| 10 MHz | `[400,500) ns` | `[400,500) ns` | `[400,500) ns` | `[400,500) ns` | 四板一致，完成数量级捕获 |
| 25 MHz | `[400,440) ns` | `[440,480) ns` | `[400,440) ns` | `[400,440) ns` | NO.2 落入相邻 40 ns 量化桶 |
| 30 MHz | `[400,434) ns` | `[434,467) ns` | `[434,467) ns` | `[400,434) ns` | 节点分布跨相邻约 33.3 ns 量化桶 |

将供电入口移到 NO.1、保持数据环序和 build 不变后的 A/B 快照为：

| SPI 档位 | NO.1 | NO.2 | NO.3 | NO.4 | 结论 |
|---|---:|---:|---:|---:|---|
| 10 MHz | `[400,500) ns` | `[400,500) ns` | `[400,500) ns` | `[400,500) ns` | 与前一供电拓扑一致 |
| 25 MHz | `[400,440) ns` | `[400,440) ns` | `[440,480) ns` | `[400,440) ns` | 相邻桶没有跟随供电入口移动 |
| 30 MHz | `[400,434) ns` | `[434,467) ns` | `[400,434) ns` | `[400,434) ns` | 相邻桶仍未跟随供电入口移动 |

两组快照均通过且没有 mixed point。25 MHz 的桶宽来自一个 40 ns 码元，30 MHz 的桶宽
约为 33.3 ns；当前整圈 RTT 靠近量化边界，小的 launch/capture 相位或门限变化会改变
`N_high`，不能把相邻桶差值解释为供电入口增加了同等物理传播延时。第一阶段只发布
`[D(N_low), D(N_high))` 和边界质量，第二阶段再用编码 marker/过采样缩窗。

对应 HIL 证据目录：

- `build-product-release/tdma_clk_train_four_10m`
- `build-product-release/tdma_clk_train_four_25m`
- `build-product-release/tdma_clk_train_four_30m`
- `build-product-release/tdma_clk_train_four_power_no1_10m`
- `build-product-release/tdma_clk_train_four_power_no1_25m`
- `build-product-release/tdma_clk_train_four_power_no1_30m`

所有结果仍为 diagnostic-only。第一阶段的完成标准是“能可靠找到 CLK RTT 粗区间并暴露
过渡抖动带”，不是“已经得到 path delay 或 DPLL 校准值”。

## 第二阶段：编码 marker + 相关测距

### 目标

第二阶段把第一阶段得到的约 `50 ns` 粗窗口作为有界相关搜索范围。返回 marker 是已知编码
序列；主节点识别相关峰对应的 chip 序号，再用过采样索引确定 chip 内位置：

```text
粗窗口        -> 限制可能的 lag 范围
相关峰 chip   -> 判断返回的是第几个码元位置
过采样 phase  -> 判断码元内第几个 4 ns sample
```

因此，即使 `50 ns` 内覆盖两个或多个候选 chip/亚码元，也不会再用“第一个看见的边沿”
猜测 RTT。完整 marker 的上下文使每个候选位置可区分，相关峰负责消除码元索引歧义。

候选计算形式：

```text
lag_sample = argmax corr(rx_samples, expected_marker)
spi_clk_round_trip_ns = capture_origin_ns + lag_sample * sample_period_ns
```

若实现显式两级索引，也可表示为：

```text
spi_clk_round_trip_ns
  = coarse_origin_ns
  + chip_index * chip_period_ns
  + phase_index * sample_period_ns
```

RP2350 当前 `BOARD_SYS_CLOCK_HZ=250 MHz`，单个 PIO SM 最快每条指令采一个输入样本，
因此原始 `sample_period_ns=4 ns`。编码能够让相邻 sample 的判别峰更尖、降低错位概率，
但不能把单路 GPIO/PIO 的硬件量化改成小于 4 ns。单次结果的时间分辨率仍发布为 4 ns；
多次统计得到的小于 4 ns 的均值变化只能命名为 statistical precision，不能伪装成更高
hardware resolution。未通过共同时间基准下的 PIO/DMA hardware latch HIL 前，结果不得
写入 active calibration。

### 最大分辨率边界

| 层次 | 能达到的能力 | 编码是否能改善 |
|---|---|---|
| 单路 PIO 原始采样 | `CLK_SYS` 每周期一个 sample，即 4 ns/bin | 不能；这是当前硬下限 |
| lag 唯一性 | 在粗窗口内唯一识别正确 4 ns bin | 能；由相关主峰和第二峰 margin 决定 |
| 抗毛刺/缺码 | 多个边沿共同投票，不依赖单个返回边沿 | 能；序列越长、转换越密集，处理增益越高 |
| 重复测量均值 | 自然抖动跨越相邻 bin 时，均值可小于 4 ns 变化 | 能改善统计精度，但不能提升单次硬分辨率 |
| 保证小于 4 ns | 需要多相采样、外部 TDC、提高硬件采样时钟或等价硬件 | 不能仅靠编码实现 |

当前最佳工程目标是“稳定选择正确的 4 ns bin”，并把 forwarding/synchronizer 产生的
抖动带收敛为可统计的相邻 bin 分布。若产品最终要求保证 1~2 ns 单次分辨率，应单独评估
外部 TDC、相移多路采样或更高采样时钟，不能继续增加码长来宣称实现。

### 编码候选与物理约束

`tools/calibration_ring_validate/calibration_clk_codebook_eval.py` 已把码本选择固化为可重复计算：生成
最大长度 Galois LFSR 序列，分别进行 NRZ、Manchester 和 differential-Manchester 展开，
在 4 ns raw-sample 域比较相邻 lag Hamming distance、粗窗内最小错误 lag distance、
电平游程和 marker 时长。当前 candidate 结果为：

| timing code | 编码 | 半码元 | timing field 时长 | 转换数 | 相邻 4 ns 理想 Hamming 差 |
|---|---|---:|---:|---:|---:|
| Barker-13 | Manchester | 20 ns | 0.52 us | 19 | 19 |
| m-sequence 31 | Manchester | 20 ns | 1.24 us | 46 | 47 |
| m-sequence 63 | Manchester | 20 ns | 2.52 us | 94 | 95 |
| m-sequence 127 | Manchester | 20 ns | 5.08 us | 190 | 191 |
| **m-sequence 255** | **Manchester** | **20 ns** | **10.20 us** | **382** | **383** |

同一码本的 Manchester 相邻 lag 判别力约为 NRZ 的三倍，并把最长无转换区限制为两个
半码元。码长从 127 增加到 255 不改变 4 ns 分辨率，但把理想相邻 lag margin 从 191
提高到 383；10.20 us timing field 对维护态训练仍足够短。因此第二阶段首选
`M255_MANCHESTER_20`，恶劣链路回退到 `M255_MANCHESTER_40`：

| codebook candidate | LFSR | seed | 半码元/逻辑位 | 最短/最长电平 | timing field |
|---|---|---:|---:|---:|---:|
| `M255_MANCHESTER_20` | width 8、Galois mask `0x8E` | `0x01` | 20/40 ns | 20/40 ns | 10.20 us |
| `M255_MANCHESTER_40` | width 8、Galois mask `0x8E` | `0x01` | 40/80 ns | 40/80 ns | 20.40 us |

LFSR candidate 的生成顺序为“输出 state LSB，state 右移；原 LSB 为 1 时 XOR mask”。该
bit order、mask、seed 和 wire 波形在固件实现/HIL 前仍不是冻结契约；评估工具是当前
candidate 的可重复事实源。

选择 Manchester 而不是 differential-Manchester，是因为当前物理线序已经固定极性，
普通 Manchester 的理想相邻 lag distance 多一个 sample。接收端仍同时计算反相信号的
score；若反相 score 更优，报告 `POLARITY_MISMATCH`，不得静默接受为有效校准。

最终图样还必须满足：

- follower 无需知道 codebook，仍逐边沿透明转发。
- epoch 不得通过循环移位 timing m-sequence 编码，否则“码相变化”会与“路径 delay 变化”
  混淆。epoch 使用独立 header，并由反码和 CRC 校验。
- 相关主峰与第二峰必须有明确 Hamming/correlation margin。
- 物理最窄高/低电平先保持不小于第一阶段 HIL 证明可稳定返回的宽度。
- 20 ns 半码元先作为性能 candidate；若脉冲缺失或 margin 不稳定，回退到 40 ns 半码元，
  同时保留 4 ns raw-sample 相关能力。
- marker 长度、capture window 和 DMA words 必须由命名常量/active profile 计算，不手写
  某一频率的固定值。

粗窗口不要求物理 chip 必须小于 `50 ns`：相关使用完整 marker，在粗窗口覆盖的所有候选
lag 上比较。20 ns 半码元下，50 ns 粗窗覆盖 2.5 个半码元和 12.5 个 raw sample；40 ns
回退档下仍可直接比较每个 4 ns lag。相关器绝不能先把 RX 解码成逻辑 bit 再做匹配，否则
会把时间量化重新放大到 40/80 ns；必须直接在 Manchester raw waveform 上相关。

### Candidate marker 格式

第二阶段 v0 marker 建议由以下字段组成，全部使用同一 Manchester 半码元宽度：

```text
QUIET_LOW
  -> SOF = Barker-13
  -> HEADER16(version2, codebook2, epoch8, master_slot3, polarity1)
  -> HEADER16_INV
  -> HEADER_CRC8
  -> TIMING = m-sequence 255
  -> EOF = inverted Barker-13
  -> QUIET_LOW
```

设计理由：

- Barker-13 只负责快速找到 marker 边界，不承担最终 RTT 精度。
- `HEADER16 + HEADER16_INV + CRC8` 证明 epoch/master/codebook 一致，并拒绝残留 marker。
- 固定 `TIMING` 字段单独用于 lag correlation；epoch 变化不会移动 timing 码相。
- EOF 和 quiet guard 用于证明完整捕获，并隔离相邻 trial。
- quiet guard 由第一阶段 `coarse_high_ns + guard_margin_ns` 计算，不能固定成某个短延时。

20 ns 半码元时，321 个逻辑位展开为约 3210 个 raw sample；按 32 sample/word 打包约为
101 words。40 ns 回退档约为 201 words。capture window 还要增加 coarse RTT、前后 guard
和 DMA 对齐，最终 word count 必须由 checked arithmetic 计算并与
`TDMA_PIO_SPI_RX_RING_WORDS` 做容量门禁。

header 的 CRC 多项式、bit endianness 和 codebook ID 仍是 candidate。它们在 C 编码器、
Python golden vector、单元测试和板端 HIL 同时形成后再登记冻结。

### Raw-sample 相关算法

master 已知发送模板 `T[0..L-1]`，RX DMA 得到 `R[]`。第一阶段 bracket 转为有限 lag 集合
`K`，对每个候选执行 32-bit XOR + popcount：

```text
D(k) = popcount(R[k : k+L] XOR T)
k_best = argmin D(k)
D_best = D(k_best)
D_second = min(D(k)), k != k_best
margin = D_second - D_best
```

50 ns bracket 在 4 ns 网格上最多只需约 14 个 lag 位置；评估工具默认还可使用 ±52 ns
范围做 27 点保守搜索。m-sequence 255 Manchester 在无噪声模型中，相邻 4 ns 错位的
Hamming distance 为 383，理论最近邻界限为 191 个 sample flips。191 只是理想码本指标，
不能直接作为板端 accept threshold；真实阈值必须由四板 HIL 的 edge jitter、脉冲展宽和
串扰分布冻结。

板端接受条件至少同时包括：

1. `D_best/L` 小于 codebook/profile 的最大错误率。
2. `margin` 大于 HIL 冻结的最小主峰裕量。
3. 正常极性 score 优于反相 score；否则报告极性错误。
4. SOF/EOF 完整，HEADER/INV/CRC 正确，epoch/master/codebook 全部匹配。
5. capture 未截断，TX/RX DMA 均完成且无 overrun/stall。
6. 多次 trial 的 `k_best` 只落在允许的相邻 bin 集合，拒绝孤立远端峰。

最终计算：

```text
spi_clk_round_trip_ns
  = capture_origin_ns
  + (k_best - timing_field_tx_origin_sample) * sample_period_ns
  - calibrated_local_endpoint_bias_ns
```

`calibrated_local_endpoint_bias_ns` 必须来自同一 PIO persona 的本地校准；若尚未校准，结果
只能发布 observed RTT，不得把 PIO output、GPIO synchronizer 和本机 RX pipeline 的固定
延迟冒充线缆传播延迟。

### 4 ns 以下的统计处理

单次 `k_best` 是整数 sample。每个 master 建议重复 128 个 epoch，发布 lag histogram、
mode、相邻 bin 比例、mean/stddev/p99 和 reject count。若自然抖动使结果在 `k` 与 `k+1`
之间分布，可以计算亚 sample 均值供诊断和 DPLL 噪声模型使用，但 snapshot 必须同时保留：

```text
hardware_resolution_ns = 4
integer_lag_sample
lag_histogram[k-1..k+1]
statistical_mean_ns
statistical_confidence / sample_count
```

禁止仅发布小数均值并把 `hardware_resolution_ns` 改成 1 ns。PIO fractional divider 的指令
仍在 `CLK_SYS` 边沿执行，不能产生可靠的 1/2/1/4-cycle 输入采样相位；仅调整 divider 或
增加码长都不能突破当前 4 ns 单次硬量化。

### 板内数据路径

```text
core1 选择 epoch/codebook
  -> coded TX PIO 发送固定 marker
  -> follower PIO 透明转发
  -> master RX PIO 以 CLK_SYS 采样 RX CLK
  -> DMA 写入固定大小 capture window
  -> core1 在有界候选窗口内做相关/Hamming 匹配
  -> guarded snapshot 发布 peak、margin、RTT 和质量
```

core1 相关器只处理固定上限的采样窗口，不动态分配内存，不遍历无界历史。原始样本先写入
专用 DMA buffer；core0、SCPI、日志和 USB 不得访问正在写入的 buffer。完成后通过 generation
翻转只读 buffer 或发布压缩 evidence。

PIO2 不再让普通环路、粗训练、板内回环和编码训练程序同时常驻。`TdmaPioSpiPhys` 只在
core1 且两个 SM 与 TX/RX DMA 均停止时执行 `tdma_pio_spi_phys_select_program_persona()`：
用 `pio_remove_program()` 卸载上一组程序，再装载当前功能所需的最小集合；失败时尝试恢复
上一 persona。禁止 core0/SCPI 直接清空或改写 PIO instruction memory。

下表是 2026-08-21 本地生成头文件的构建快照，非容量事实源；实际占用必须由各
`*_program.length` 在构建产物中求和：

| persona | 动态程序集合 | generated instruction 快照 |
|---|---|---:|
| `NORMAL` | byte TX + byte RX | 9 |
| `CLOCK_COARSE` | CLK forward + burst + capture | 15 |
| `CAL_LOOPBACK` | loopback TX + capture | 8 |
| `CLOCK_CODED` | CLK forward + coded TX + oversample RX | 7 |

master 使用现有两个 SM 分别 TX/RX，follower 只启用 forwarding SM。coded TX 和 RX 都
使用固定 DMA window：通道来自 `TDMA_PROFILE_DEFAULT_TX_DMA_CHANNEL_ID` 和
`TDMA_PROFILE_DEFAULT_RX_DMA_CHANNEL_ID`，经 `TdmaFoundationProfile`、ring runtime 和
物理层逐级校验，不能在物理层另行挑选未声明 channel。

master 启动顺序必须是：生成固定 TX buffer 和 expected template、配置 RX DMA、配置 TX DMA
并预装 FIFO、清 IRQ/FIFO、最后用 `pio_enable_sm_mask_in_sync()` 同时启动 TX/RX SM。leading
quiet samples 吸收同步启动偏差；`capture_origin`、`timing_field_tx_origin_sample` 和 DMA
transfer count 都写入同一 guarded snapshot。

### 第二阶段四板最小闭环实测

`CALibration:CLOCk:CODEd:STARt/STOP` 和
`READ:CALibration:CLOCk:CODEd?` 已接入 Calibration guarded command slot。START 参数只提供
候选 codebook、coarse lag window 和相关质量门限；固件自行绑定 `board_identity_serial()`、
build、logical slot、TDMA staged topology/profile/schedule CRC、baud、epoch、sequence 和
calibration generation。`RING:STOP` 会清空 live runtime，所以 Calibration 读取的是 TDMA
service 保留的最后一次 accepted `ring_staged_config`；该只读快照不能 arm 或改写 TDMA。

build `20260821044448` 的四板 HIL 是 2026-08-21 bench 快照，非产品阈值事实源。板卡按
Calibration accepted order
`0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A`
轮流作为 master，工具为
`tools/calibration_ring_validate/calibration_clk_coded.py`，证据目录为
`build-product-release/calibration_clk_coded_p2_four_accepted_order`：

| NO | master unique address | best lag sample | best distance | margin | marker/DMA |
|---:|---|---:|---:|---:|---|
| 1 | `0010071E65B5CB38` | 100 | 23 | 442 | 全 marker flags；无 overrun/stall |
| 2 | `FB276192BEF9CCE1` | 100 | 152 | 184 | 全 marker flags；无 overrun/stall |
| 3 | `2BD5090FE009FA2A` | 101 | 0 | 485 | 全 marker flags；无 overrun/stall |
| 4 | `A1E549202D18ED6A` | 101 | 154 | 179 | 全 marker flags；无 overrun/stall |

上表是单轮、显式宽松 diagnostic gate 下的快照。raw sample period 来自
`BOARD_SYS_CLOCK_HZ`，因此本轮峰值对应约 `400~404 ns`；这只是 coded TX/RX 同步 origin 下
的整圈 RTT，不能解释为单 link delay。`M255_MANCHESTER_20` 在同一硬件上主峰可见但严格
Manchester 字段拒绝；`M255_MANCHESTER_40` 四主均通过，因此后者暂作为 robust HIL profile，
前者保留实验档。正式 distance/margin 门限必须由 P2-11 重复统计产生，当前结果继续保留
`CALIBRATION_CLK_CODED_FLAG_DIAGNOSTIC_ONLY`。

本轮 START/STOP 的 CDC action response 可能在 persona 切换时超时；host 只在 guarded
snapshot 的 sequence、codebook 和 lag window 与本次 request 完全一致时接受该动作，不能
把单纯 `<timeout>` 当成成功。该响应时序仍需在产品级 PREPARE/ACK/commit 实现中消除。

### 板内指令触发闭环

目标产品行为是“训练流程在板卡中，通过指令触发执行”，分两步落地：

1. **最小闭环**：保留 `calibration_clk_train.py` 按唯一地址向各板发准备/触发指令，但每个 trial、
   重复统计、相关匹配和 snapshot 全部在板内完成。host 不参与实时判定。
2. **产品闭环**：只向当前 reference 提交一次训练 intent。reference 在普通 TDMA persona 下
   发送带 epoch/topology/profile/commit sequence 的 TRAIN control，收齐 active-node ACK
   bitmap 后，在约定序号统一切换 training persona；四主轮换、计算和恢复均由板内协调器
   完成。

产品闭环时序：

```text
显式 SCPI intent
  -> core0 command slot
  -> core1 校验 STOPPED/maintenance gate
  -> TRAIN_PREPARE(epoch, topology/profile CRC, commit_seq)
  -> 收齐 active-node ACK bitmap
  -> 到 commit_seq 统一切换 PIO persona
  -> CLOCK_COARSE
  -> CLOCK_CODED
  -> 四主轮换与质量计算
  -> 发布 VALID 或 FAILED snapshot
  -> 全节点恢复普通 persona并停在 STOPPED
```

不能在切换到 CLK-only persona 后再依赖普通 DATA frame 协调节点，所以 PREPARE、ACK 和
commit sequence 必须在普通 TDMA persona 仍运行时完成。任一 active 节点未 ACK、拓扑 CRC
变化或 commit miss，都必须取消本 epoch，禁止部分节点进入训练。

现有 `SYSTem:TDMA:RING:TRAIN <cycles>` 保留第一阶段单 trial 诊断语义。产品级“一条指令
触发完整训练”的 SCPI 拼写、参数和状态字段在代码/测试完成前不在本文冻结；但它必须复用
上述 command-slot 和 snapshot 边界，不能新增同步直达 PIO 的旁路。

### 第二阶段实施待办

- [x] P2-1：冻结候选 codebook 生成器和离线自相关测试，覆盖主峰、第二峰、循环移位、反相、
  单 chip 缺失/重复和上一 epoch 残留。
  - 已完成：C/Python 共用 candidate 语义，包含 marker header/反码/CRC、Manchester waveform、
    golden vector、正反相、缺失/重复 sample、旧 epoch、截断和低 margin 单测；wire layout
    仍是 candidate，不因此登记为正式跨域契约。
- [~] P2-2：用现有 CLK pulse HIL 扫描 candidate 最窄高低电平，选择 robust chip；
  新 build 上 32 ns 四主单轮通过，24 ns 在三个 master 上出现
  `correlation_manchester` reject，20 ns 仍待四板齐备后筛选；32 ns 作为当前
  candidate robust 档，尚不能清除 `DIAGNOSTIC_ONLY`。
- [~] P2-3：coded TX PIO 和固定 FIFO window 已完成，不由 core1 逐边沿喂数；显式 quiet
  guard/profile 仍待补齐。
- [x] P2-4：实现 master RX 过采样 PIO + DMA 固定窗口；TX epoch 和 RX capture 使用同一
  `CLK_SYS` 时间基准，并记录真实硬件 capture origin。
- [~] P2-5：core1 有界相关状态机已输出 peak/second peak、margin、Hamming distance 和
  accepted/rejected reason；板内重复统计与 histogram 尚待完成。
- [~] P2-6：把 `CLOCK_COARSE -> CLOCK_CODED` 接入 TDMA owner 非阻塞状态机；训练中禁止
  core0/USB/日志影响 PIO/DMA buffer ownership。
- [x] P2-7：扩展 guarded snapshot，绑定唯一板卡地址、logical slot、topology/profile/
  schedule CRC、baud、codebook ID、epoch、sample period 和 calibration generation。
- [~] P2-8：实现 TRAIN_PREPARE/ACK/commit sequence；工具按唯一 ID 触发的最小四主闭环已
  完成，reference 单指令协调、ACK bitmap 和 commit sequence 尚待实现，
  再完成 reference 单指令协调全环的产品闭环。
- [x] P2-9：新增 `calibration_clk_coded.py` 使用固件返回的相关结果，只做批量触发、UTF-8
  JSON/CSV/summary 和评分；禁止 host 自己重算板端实时判定结果作为唯一事实源。
- [x] P2-9a：静态复核回环反射校准 persona：`tdma_pio_spi_clk_burst` 的分频、4-cycle
  周期和 2:2 high/low，以及 `tdma_pio_spi_clk_forward` 的上游边沿再生语义已纳入
  `tdma_pio_timing_check.py`；输出仅是数字时序门禁，不能替代示波器的电气验证。
- [~] P2-10：增加 unit/HIL 门禁：码元错位、反相、缺失/重复、低 margin、DMA overrun、
  capture truncation、master 掉线、ACK 缺失、commit miss、profile/topology 改变和恢复 persona。
- [ ] P2-11：四个 master 每点至少重复 100 次，统计 min/max/mean/p99/stddev、peak margin 和
  混合/拒绝比例；先在 10/25/30 MHz 验证，35 MHz 只保留实验档。
- [ ] P2-12：只有真实 PIO/DMA hardware latch、重复门禁和跨主一致性通过后，才允许清除
  `TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY`；随后再进入短 TRAIN frame 和完整 path-delay
  校准。

## 第二阶段验收门槛

第二阶段完成至少需要同时满足：

| 门禁 | 要求 |
|---|---|
| 身份 | 报告和 calibration key 只使用唯一板卡地址 + logical slot |
| 协调 | 所有 active 节点同一 epoch、topology/profile CRC、commit sequence |
| 实时边界 | TX/RX/forward/capture 在 PIO/DMA；core1 只做有界状态机和相关 |
| 码元定位 | 主峰唯一，主峰/第二峰 margin 达到冻结阈值，无 epoch 歧义 |
| 时间证据 | 同一硬件时间基准，sample period 和 latch flags 明确 |
| 重复性 | 四主多次重复无系统性 slot 偏移，mixed/reject 比例低于冻结阈值 |
| 故障恢复 | 任一失败统一 STOP，普通 persona 可重建，不形成无限 CLK 环 |
| DPLL 门禁 | 未满足 hardware-latched、非 diagnostic-only 前，VDC/DPLL 必须拒绝 |

## 第三阶段：对称双面双向测距

第三阶段不再把整圈 CLK RTT 平均分摊到各 link，而是利用每条相邻板卡链路上方向相反
的 `CLK` 与 `DATA` 物理路径，执行双向同时对比法（对称双面双向测距、双程时间传递）。
`SYNC` 是同一 `train_epoch/sequence` 的事务标记，负责把两条相反方向的边沿锁定在同一
次测量中；它不是额外的传播延迟假设，也不承担业务 VDC 语义。

### 产品环路的物理方向

同一 BiSS 段为 A.CLK_TX `GPIO25` -> B.CLK_RX `GPIO28`，同时 B.DATA_TX `GPIO29` ->
A.DATA_RX `GPIO24`；SYNC 使用 `GPIO26/27` 关联 epoch。ISO1452 固定方向已经与下述
“CLK 正向 + DATA 反向”方程一致，固件不得在 persona 切换时反转驱动器方向。

### 单链路时间戳方程

对链路 `A -> B`，约定 `CLK` 从 A 到 B，`DATA` 从 B 返回 A。原始 PIO 边沿 latch 记录：

```text
t1 = A.CLK_TX       # A 本地时钟：CLK 正向发送
t2 = B.CLK_RX       # B 本地时钟：CLK 正向接收
t3 = B.DATA_TX      # B 本地时钟：DATA 反向发送
t4 = A.DATA_RX      # A 本地时钟：DATA 反向接收
```

B 端的本地 residence 为：

```text
residence_B = t3 - t2
path_sum_AB = (t4 - t1) - residence_B
```

其中 `t4-t1` 和 `t3-t2` 分别只在单板 A、B 的本地时钟域内计算，因此不要求两个板卡
在此刻已经共享同一个绝对时间零点；短测量窗口内的频率偏差必须作为 `clock_rate_error`
和不确定度进入门禁。若正反向路径满足可接受的对称性：

```text
delay_A_to_B = path_sum_AB / 2
```

若正反向不完全对称，则只能发布：

```text
path_sum_AB = delay_A_to_B + delay_B_to_A
asymmetry_ns = delay_A_to_B - delay_B_to_A
```

不能仅凭一次双程结果伪造两个单向 delay。需要单向结果时，使用等长线缆、收发器/PIO
endpoint bias 校准、重复 epoch 统计和方向性校验共同形成 `asymmetry_bound`；超过 bound
时保留 aggregate/path-sum，拒绝生成 per-link active entry。

### PIO 原始测量与证据绑定

三根线的原始 PIO persona 必须同时 arm `CLK_RX`、`CLK_TX`、`DATA_TX`、`DATA_RX` 和
`SYNC` capture。一次 accepted sample 必须带有：

- `train_epoch`、`train_seq`、`source_board_id`、`destination_board_id`、logical slot 和
  direction；
- `t1/t2/t3/t4` 的硬件 latch index、各自 source/resolution/flags、DMA count 和 overrun；
- `residence_B`、`path_sum_AB`、`delay_mean/stddev/p99`、`asymmetry_bound`、endpoint bias
  generation、topology/profile CRC 和 calibration generation；
- `SYNC` 到四个边沿的关联状态，以及缺失、重复、乱序、极性、CRC、window miss 和 stale
  原因。

所有四个边沿必须来自同一 epoch 和同一 PIO persona。CPU 读取 timer、DMA 完成时刻、帧
解析时刻只能作为 diagnostic evidence，不能替代 `t1..t4` 的 edge latch。任何一个方向
缺失时，不得以单向值或默认零值补齐 path calibration。

### 等长差分链路和四板 HIL 的定位

单端转差分后使用严格等长网线，可以把物理传播差异压到较小范围，并使
`delay_A_to_B ~= delay_B_to_A` 成为可验证的工程假设；它不能消除收发器、GPIO
synchronizer、PIO pipeline、连接器和方向切换的 endpoint bias。因此必须先做板内同 persona
loopback/bias reference，再做板间双程测量。

四板环回不再是“为了弥补无法测量板间 delay”而存在，而是第三阶段的系统级验证：

1. 逐条相邻链路执行 `SYNC + CLK(A->B) + DATA(B->A)` 双程测量，生成 per-link path-sum
   和质量；
2. 轮换 master，比较四板 cumulative sum 与整圈 edge RTT，检查遗漏、重复或方向性异常；
3. 以四板 HIL 的残差、asymmetry 和 endpoint bias 作为 profile acceptance evidence。

如果单链路双程证据、bias generation、等长拓扑和重复性门禁均通过，四板 HIL 不再是单向
delay 可观测性的必要条件，但仍是 8 节点扩展前的系统级回归门禁。四板结果不能替代每条
link 的 `t1..t4` 证据，也不能把整圈 aggregate 平均分摊为 link delay。

### 第三阶段门禁和交付

- `SYNC` epoch/sequence、板卡身份、拓扑/profile CRC 和 PIO persona 全部一致；
- `t1..t4` 均为同一 trial 的 hardware-latched edge，分辨率满足正式校准门禁且不带
  `DIAGNOSTIC_ONLY`；
- B 端 residence、双程 path-sum、endpoint bias 和 clock-rate error 均有来源和 generation；
- 等长线缆的对称性假设通过重复统计和四板 residual 检验，不通过时只发布 path-sum；
- 缺边沿、重复边沿、SYNC/CRC/epoch 错、DMA overrun/stall、窗口超时或 freshness 失效时，
  sample rejected，active calibration 保持不变；
- 校准域发布 active per-link calibration，VDC 消费该结果形成 VDC/DPLL map；TDMA 只
  承载 raw evidence、训练窗口和 failure propagation。

### 第三阶段实施待办

- [x] P3-1：为相反方向 CLK/DATA/SYNC 定义同 epoch 的 PIO marker 和四边沿 capture origin。
- [x] P3-2：实现 `t1..t4` 证据关联、residence 扣除、path-sum 和 clock-rate error bound。
- [ ] P3-3：完成同一 PIO persona 的板内 endpoint bias/reference loopback 校准。
- [~] P3-4：双向计算 unit 和四条相邻段 HIL 已完成；继续补 fault injection，覆盖缺边沿、乱序、重复、
  极性、SYNC/CRC 错、DMA overrun/stall、频率偏差和方向 asymmetry。
- [~] P3-5：四板逐链路三档诊断 HIL 已完成；P3 验证入口现已固定
  `calibration_link_frequency_policy.REQUIRED_FREQUENCY_LADDER_MHZ`，稳定档为
  `STABLE_REQUIRED`，`LIMITED_RX_FREQUENCY_MHZ` 为每轮必测的 `LIMITED_RX`。继续完成
  cumulative/整圈 residual 对比，固化 8 节点扩展前的 profile acceptance threshold。
- [ ] P3-6：只有四时间戳 hardware latch、bias generation、重复统计和拓扑 freshness 全部
  通过后，才生成 active per-link delay，清除对应 diagnostic-only 标志并交给 VDC/DPLL。

P3 HIL bench 快照（非事实源）：build `20260821100236` 上，physical order
`0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A`
的四条相邻链路完成 12 个 link/frequency level、36 个独立 epoch，基础四边沿/CLK 门禁
全部 accepted；证据目录为 `build-product-release/calibration_p3_full_20260821`。observed
对称单程估计为 `80..82 ns`，DMA overrun/PIO stall 均为零。增加返回 DATA pulse-width
门禁后，30 MHz 四段加严复测为 39/40 accepted：NO.1→NO.2 出现一次 8 ns DATA_RX
高电平并被拒绝，证据目录为 `build-product-release/calibration_p3_30m_data_gate_20260821`。
因此当前保守稳定档上限来自该 bench 快照；`LIMITED_RX_FREQUENCY_MHZ` 为 `LIMITED_RX`
有界诊断接收档，每次 P3 验证仍必须执行，不能因低频失败而跳过。该档任一拒绝记录
`FALLBACK_25MHZ` 并按 `LIMITED_RX_FALLBACK_MHZ` 回退，且不放宽 CLK/DATA 频率、占空比
或脉宽门限，也不单独使 `STABLE_REQUIRED` 稳定档总判定失败。
所有结果继续带 `TDMA_PIO_SPI_P3_FLAG_DIAGNOSTIC_ONLY`。

完成编码 CLK RTT 后，下一阶段才是带 DATA/CS/CRC 的短 TRAIN frame、节点 residence 和
`frame_complete_round_trip_ns`。只有完整帧 RTT 才能生成运行态 RX window、guard 和
feedback timeout。
