# 校准训练子域：Marker/Data 对齐与 TDMA 接入方案

Status: Active
Domain: CALIBRATION / TRAINING
Canonical: `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`
Related: `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/calibration/CALIBRATION_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-08-22

本文档把“先发送同步 marker，再按本地 PIO 周期发送编码 DATA，并在接收端按 marker 建立相对时间基准”的方案收敛为校准域下的独立训练子域。本文档是实施方案和待冻结候选接口，不把当前诊断值直接提升为 active calibration，也不允许训练子域绕过 TDMA core1 owner 直接操作 PIO、SM 或 DMA。

## 1. 目标与边界

训练子域解决“有波形但不能还原有效字节”的问题：

- 确认相邻节点的同步线和数据线都可捕获；
- 用 marker 建立接收端本地时间零点，消除公共传播延迟对绝对等待的依赖；
- 通过已知 codeword、相关峰、CRC 和 epoch/sequence 判断 DATA 是否被正确采样；
- CS marker 识别成功后由 core1 立即 cut-through 转发给下一个节点，不等待完整 TDMA 帧和 core0 解析；
- 输出每条相邻链路的相对偏移、有效窗口、置信度和拒绝原因；
- 训练结果通过 bounded bridge 提供给 TDMA，作为运行时接收窗口的输入。

训练子域不解析完整业务帧，不替代 Calibration active path-delay table，不允许 core0、USB、SCPI 或日志任务进入边沿采样和相关计算热路径。

## 2. 物理与时序模型

训练以一条有向相邻链路为单位。发送节点在同一 PIO epoch 内发出：

```text
SYNC/CS marker -> N 个 PIO instruction cycles -> DATA codeword -> CRC/EOM
```

接收节点执行：

```text
捕获 CS marker
    -> 锁存本地 capture_origin
    -> 校验 marker epoch/sequence/CRC
    -> 立即将原始 CS marker 通过 TX PIO 转发给下一个节点
    -> 按 N 和本地 PIO 时钟建立 DATA search window
    -> 采集 DATA raw bits
    -> codeword correlation + polarity + CRC + sequence 校验
    -> 发布 accepted/rejected evidence
```

CS 的 cut-through 转发只允许使用 core1 持有的 PIO/SM/DMA 原语；转发路径不得等待 core0、USB、日志、完整帧解析或业务 FIFO。转发失败必须记录链路级 reason，并停止当前训练 epoch，防止后续节点把缺失 marker 当作无数据。

`N` 是训练变量，不是固定产品常量。候选初值来自当前有效 PIO 时钟周期和已测 path-delay 的粗窗口；每次缩小搜索范围必须绑定 profile、topology、epoch 和 codebook。当前第三阶段约 `81 ns` 的观测值只能作为诊断输入快照，不能写成训练算法的硬编码事实。

marker 与 DATA 走公共传播路径时，接收端使用相对间隔而非绝对延迟：

```text
DATA_expected = marker_capture + N * PIO_CYCLE_NS + local_pipeline_bias
```

若 marker 与 DATA 走不同收发器或不同物理对，则额外输出 `marker_data_skew_ns`；该 skew 必须单独统计和设门，不能被 path-delay 平均值掩盖。

## 2.1 专业术语与 EtherCAT 对照

本方案建议采用以下术语，避免把不同层次都称为 `delay`：

| 本方案含义 | 推荐术语 | EtherCAT 中的相近概念 | 差异 |
|---|---|---|---|
| CS/同步边沿建立本地时间零点 | timing marker / synchronization marker | Distributed Clocks 的同步事件、帧到达时间参考 | EtherCAT 由 ESC 硬件端口时间戳和 DC 事件完成，不依赖软件等待 |
| 识别后立即把 marker 发往下游 | cut-through forwarding / on-the-fly forwarding | ESC 对帧进行 on-the-fly 处理并继续转发 | 本方案只先转发训练 marker；TDMA 业务帧仍按既有 adapter 规则处理 |
| 已知 DATA 比特序列 | link training sequence / training codeword | Ethernet 物理层 preamble、链路检测和帧校验序列 | EtherCAT 不用一个固定“marker 后 N 周期”的应用层格式解决所有延迟 |
| 每段 TX 到 RX 的传播时间 | per-link propagation delay / port-to-port delay | DC propagation delay、port delay compensation | 本方案测量结果进入 Calibration；EtherCAT 由 ESC/DC 测量并用于系统时间偏移 |
| marker 后 DATA 的相对采样位置 | relative receive window / symbol alignment offset | local receive event 到 Sync0 的相位关系 | 本方案需要 codeword correlation；EtherCAT 主要依靠 ESC 收发器和 DC 硬件时序 |
| 多节点统一时间 | distributed clock synchronization | EtherCAT Distributed Clocks (DC) | 本方案后续由 VDC/DPLL owner 提供，不由训练 persona 自己生成全局时间 |

因此本方案的完整名称可写为：

```text
Marker-Anchored Cut-Through Link Training
（同步标记锚定的切通式链路训练）
```

训练输出的 `data_offset` 更准确地称为 `relative receive-window offset`，而不是 `loop_delay`；每跳测量值称为 `per-link propagation delay`，整圈汇总值才称为 `loop delay`。

### EtherCAT 的关键启示

EtherCAT 解决类似问题依靠的是 ESC 硬件数据通路和 Distributed Clocks 的组合：

1. 从站 ESC 在帧经过时直接读写属于自己的 datagram 区域，并在硬件路径上继续转发，属于 on-the-fly/cut-through processing，不等待主站软件解析整帧。
2. ESC 端口对接收事件、端口延迟和系统时间进行硬件记录；主站据此计算各段 propagation delay 和校正后的 DC offset。
3. 同步输出（例如 Sync0/Sync1）由从站本地硬件按校正后的 distributed time 产生，应用任务不负责 bit-level 对齐。
4. Working Counter、帧校验和状态寄存器用于判定帧是否实际经过目标从站；它们和 DC 时间戳是独立门禁，不能用单一 delay 数字替代。

对本项目的直接映射是：

```text
CS marker         -> 本地硬件同步事件
立即转发 marker   -> core1/PIO cut-through forwarding
DATA codeword     -> bounded link training sequence
per-link 81 ns    -> calibration propagation-delay candidate
relative offset   -> per-link receive-window/symbol alignment
loop_delay        -> 四段 propagation + residence + guard 的整圈预算
CRC/sequence      -> frame validity / working-counter 类门禁
```

差异也必须保留：EtherCAT 的 ESC 和 Ethernet PHY 提供专用硬件转发、时间戳和双向端口模型；当前 SPI/PIO 产品链路没有这些现成机制，因此 marker 转发、DMA 捕获、相关匹配和故障状态必须由 TDMA core1 owner 明确实现。`active path_delay table` 仍是校准事实源，不能因为采用 marker training 就被省略。

## 2.2 历史实测 delay ledger

以下数值来自校准域已有 bench/HIL 记录，仅用于训练初始窗口、算法回归和结果交叉检查；它们不是未经 active gate 的产品常量。不同阶段的测量对象不同，禁止直接相加或互相替代。

| 阶段 | 测量对象 | 已记录结果 | 训练子域的用途 | 不能做的解释 |
|---|---|---|---|---|
| P1 CLK RTT 粗捕获 | 四板整圈 CLK 往返 bracket | 10 MHz 各主节点约 `[400,500) ns`；25 MHz 量化桶约 `[400,440) ns` / `[440,480) ns`；30 MHz 量化桶约 `[400,434) ns` / `[434,467) ns` | 给 marker/data 训练提供数量级粗窗口和 guard 起点 | 不能当单段 link delay，不能按节点数平均分摊 |
| P2 coded marker | 四板 coded RTT 的相关 lag | `sample_period_ns` 为 `4 ns` 的记录中，best lag 为 `100/101` 个 raw sample，即约 `400/404 ns`；四主最小闭环 trial accepted | 限制 `N` 和 DATA 相对窗口的搜索范围，提供 raw-sample 对齐基准 | 仍是整圈 coded RTT，不是单跳 `path_delay` |
| P3 双向同时对比 | 相邻板间逐链路 path-sum，在等长链路对称假设下折半 | 四板逐链路 observed delay estimate 约 `78..82 ns`；最新复测约 `80..82 ns`，链路均值约 `80..81.333 ns`，当前可取约 `81 ns` 作为 diagnostic candidate | 为每条 directed link 建立 per-link propagation-delay 粗预算和 marker 搜索初值 | 尚未通过 endpoint bias、active generation、freshness 和 VDC/DPLL gate，不能称 active table |
| P3 板内 reference loopback | 同板三线 endpoint/reference 观测 | 已记录 observed delay estimate 约 `50..60 ns`，residence 约 `960..980 ns`，raw path-sum 约 `100..120 ns` | 仅用于 endpoint bias/reference loopback 交叉检查 | 不能当 NO.1->NO.2 等板间传播延迟 |

训练实现必须在 evidence 中同时保存 `stage`、`measurement_object`、`unit`、`sample_period_ns`、`topology/profile/build` 和 `diagnostic_only` 标志。特别是 `81 ns` 只能写入 per-link diagnostic staging，不能覆盖 `VDC PATH:DELay?` 的 active table，也不能绕过 TDMA ARM 的 generation/CRC/freshness 校验。

### 三阶段数值如何进入当前训练

```text
P1 [400,500) ns 级整圈 bracket
    -> P2 400/404 ns coded RTT 和 raw-sample lag
    -> P3 每跳约 81 ns propagation candidate
    -> CS marker 建立本地零点
    -> DATA codeword 以相对 offset 进行匹配
    -> 训练窗口和 per-link staging
    -> TDMA ARM/START
```

P1/P2 只限制搜索范围，P3 只提供每跳传播预算；真正决定 DATA 是否被正确识别的是 marker 捕获、cut-through 转发、相对窗口、codeword correlation 和 CRC/sequence 门禁。

## 3. 三阶段训练主流程

新训练子域按 `TRN-01 -> TRN-02 -> TRN-03` 顺序推进。这里的阶段编号与历史校准
`P1/P2/P3` 有意分开：P1/P2/P3 是已经获得的测量事实和诊断输入，TRN-01/02/03
是把这些输入变成可运行链路的训练过程。

| 新阶段 | 名称 | 主要输入 | 主要输出 | 进入下一阶段的门禁 |
|---|---|---|---|---|
| `TRN-01` | Ring Marker Capture & Cut-Through（环路 marker 捕获与切通） | accepted topology、P2 marker codebook、marker line 角色、epoch/sequence | 每个节点的 marker capture/forward tick、cut-through residence、整圈 marker RTT、缺失/重复/乱序原因 | 所有节点捕获同一 marker；顺序正确；每跳 forward residence 有界；reference 捕获返回 marker；PIO/DMA fault 为零 |
| `TRN-02` | Marker-Anchored DATA Slot Training（marker 锚定 DATA 码元时隙） | TRN-01 marker origin、P3 `path_delay` diagnostic candidate、PIO sample period、DATA codeword | 每条 link 的 `data_offset`、`training_window`、`guard`、`marker_data_skew`、correlation/margin/CRC 证据 | 单跳先通过；四条 directed link 均在同一 generation/profile 下重复通过；窗口达到当前 PIO 分辨率或明确拒绝原因 |
| `TRN-03` | TDMA Short-Frame/FIFO Closed Loop（TDMA 短帧/FIFO 闭环接入） | TRN-02 per-link window、PIO instruction-cycle profile、loop-delay/residence 汇总、topology/profile/schedule CRC | TDMA per-link staging、ARM gate、slot/forward budget、短帧 TX/RX FIFO 计数、sequence/CRC/feedback evidence、active candidate | 全部链路 accepted 且指令周期预算可重放后才能 ARM；四板 up/down、FIFO、sequence/CRC 同时增长；失败统一 STOPPED 并保持旧 active generation |

### TRN-01：环路 marker 捕获与切通

第一阶段不解析完整 DATA，也不试图生成运行态 RX window。reference 发出带
`train_epoch`、`train_sequence` 和最小完整性字段的 marker；每个节点在本地捕获后，
由 PIO/core1 以固定有界延迟将原始 marker 或已确认的最小 marker token 转发给 successor。
转发路径不得等待完整 codeword、core0、USB、日志或业务 FIFO。第一阶段验证的是：

```text
NO.1 marker TX
  -> NO.2 capture + cut-through
  -> NO.3 capture + cut-through
  -> NO.4 capture + cut-through
  -> NO.1 return capture
```

TRN-01 的 `SYNC/CS` 是训练 persona 下的 timing marker；普通 TDMA persona 中它仍是
`frame-sync/CS`，两种语义不能在同一 PIO/SM/DMA epoch 混用。第一阶段通过后，marker
捕获不再是后续失败判定的主要变量，后续重点转向 marker 后的 DATA 相对时序。

### TRN-02：marker 锚定 DATA 码元时隙

第二阶段在每个本地 marker origin 之后发送/捕获已知 DATA codeword。候选 `N` 由 P3
path-delay 粗预算、PIO pipeline 和 guard 形成有界搜索区间；实际接收位置通过
correlation、极性、CRC、sequence 和 margin 确认，而不是把 `81 ns` 写成固定等待值。

实施顺序固定为：

```text
NO.1 -> NO.2 单跳粗搜
    -> 单跳细搜/重复统计
    -> NO.2 -> NO.3、NO.3 -> NO.4、NO.4 -> NO.1
    -> 四条 directed link 的 window 一致性检查
```

TRN-02 的结果是相对接收窗口，不是新的绝对 `path_delay`。如果 marker 和 DATA 的物理
路径不完全相同，`marker_data_skew_ns` 必须单独统计；不能用整圈 RTT 或平均 path-delay
掩盖该偏差。

### TRN-03：TDMA 短帧/FIFO 闭环接入

第三阶段才恢复 NORMAL persona，把 TRN-02 的 per-link window 绑定到 TDMA adapter，
并通过显式 `STOP -> stage -> validate -> ARM -> START` 接入短帧和 TX/RX FIFO。ARM 前
必须冻结本次 profile 的 PIO 指令周期预算；core1
负责 PIO/SM/DMA、飞行转发和 FIFO 搬运；core0 只处理已完成帧和 guarded snapshot。

### TRN-03 的 TDMA 指令周期预算

第三阶段的时间基准不是 RTOS 任务执行时间，而是 PIO state machine 的实际指令周期。
预算必须从代码和 profile 事实源生成并随 staging 一起校验：

```text
pio_instruction_period_ns = derived from clkdiv and clock_get_hz(clk_sys)
tdma_bit_period_ns         = bit_cycles * pio_instruction_period_ns
marker_to_data_cycles  = marker_gap_cycles + local_pipeline_cycles
forward_residence_cycles = forward_rx_cycles + forward_tx_cycles
slot_budget_cycles     = rx_arm_lead_cycles
                         + marker_to_data_cycles
                         + codeword_cycles
                         + forward_residence_cycles
                         + guard_cycles
loop_delay_cycles      = ceil(loop_delay_ns / pio_instruction_period_ns)
```

`clkdiv` 必须来自实际装载的 PIO persona：普通 TDMA 使用
`tdma_pio_spi_clkdiv_for_baud()`，P3/训练 persona 使用
`tdma_pio_spi_p3_clkdiv_for_baud()` 和对应的
`tdma_pio_spi_p3_data_high_cycles()`。`clock_get_hz(clk_sys)` 的板级来源是
`BOARD_SYS_CLOCK_HZ`。`bit_cycles`、marker gap、forward residence 和 codeword cycles
必须从 PIO 程序/码本派生，不能由 SCPI 或 core0 临时注入。

每个 TRN-03 staging record 至少绑定：

```text
pio_program/persona_id, clkdiv, clk_sys_hz, pio_instruction_period_ns
bit_cycles, marker_to_data_cycles, forward_residence_cycles
rx_arm_lead_cycles, codeword_cycles, guard_cycles, slot_budget_cycles
loop_delay_ns, loop_delay_tolerance_ns, loop_delay_cycles
cycle_period_ns, baud_hz, profile_crc32, schedule_crc32
```

任何 `clkdiv`、PIO persona、`baud_hz`、码本长度或 `cycle_period_ns` 变化都必须使旧
training window stale 并重新训练。`cycle_period_ns` 是 TDMA 调度周期，不能替代 PIO
instruction period；core1 的 RTOS service 只允许在已预留的 bounded budget 内搬运 FIFO，
不能进入 marker、采样边沿或 cut-through 的关键路径。

第三阶段门禁包括：

- 四条 directed link 均有同一 topology/profile/calibration generation 的 accepted window；
- `loop_delay` 只用于整圈返回预算、反馈窗口和 timeout，不替代 per-link `data_offset`；
- 每个 slot 的 `slot_budget_cycles`、`rx_arm_lead_cycles`、`forward_residence_cycles` 和
  `loop_delay_cycles` 均能由当前 PIO persona 重放，不能依赖一次性的 core0/RTOS 调度时刻；
- 短帧的 TX/RX FIFO、sequence、CRC、up/down 状态同时增长；
- 任一链路失败、窗口过期或 generation 不一致，ARM 必须拒绝并恢复 STOPPED；
- 只有通过重复性、freshness、hardware latch、bias 和 rollback 门禁，才可形成 active candidate。

三阶段与历史 delay ledger 的关系固定为：

```text
P1 CLK RTT bracket       -> TRN-01/TRN-02 的数量级 guard
P2 coded marker RTT      -> TRN-01 的 marker codebook/捕获基线
P3 per-link ~81 ns       -> TRN-02 的候选 N 初值
TRN-01 marker origin     -> TRN-02 的相对 DATA 窗口
TRN-02 accepted window   -> TRN-03 的 TDMA ARM/FIFO 接入
```

## 4. HAOFV owner 分工

| 层 | 所有权 | 允许动作 | 禁止动作 |
|---|---|---|---|
| CalibrationAO/FB | 训练状态、codebook、搜索区间、质量门禁、generation/freshness | 发布 bounded intent，配对 raw evidence，生成 candidate | 直接写 PIO/SM/DMA，直接改 TDMA live 状态 |
| TDMA core1 owner | 训练 persona、PIO/SM/DMA、方向控制、marker/data 边沿、cut-through 转发和 DMA buffer | 按 intent 装载 persona、启动/停止、转发 marker、发布 guarded snapshot | 解释相关峰含义，决定 active calibration |
| TDMA adapter | 把训练结果转换为运行时 per-link window | 在 ARM 时绑定已接受 staging，向 RX 逻辑提供窗口 | 自己测量 path-delay 或绕过 generation/CRC |
| VDC/DPLL | 消费 accepted calibration | 读取 path-delay、quality、freshness、topology | 消费 `DIAGNOSTIC_ONLY` 或未完成训练结果 |
| SCPI/Host | 维护态编排和读取 | 提交 bounded command、读取 snapshot、保存证据 | 注入实时时间戳、轮询代替 core1 状态机 |

## 5. 训练状态机

| 状态 | 进入条件 | core1 动作 | 退出条件 |
|---|---|---|---|
| `IDLE` | ring disabled | 保持普通资源释放 | 收到合法训练 intent |
| `COARSE_READY` | accepted topology/profile，path-delay 粗窗口存在或明确为 diagnostic candidate | 装载 marker/data persona，清空 FIFO/DMA，锁定 epoch | 所有参与节点 ready |
| `MARKER_ARMED` | RX capture 已启动 | 捕获 marker 并锁存本地 origin | marker 命中或 timeout |
| `MARKER_FORWARD` | marker header 已通过最小完整性校验 | 在 bounded core1 路径立即发往 successor | TX 完成或 forward error |
| `DATA_RUNNING` | marker 已捕获且转发已提交 | 按候选 `N` 发/收 codeword，DMA 连续采样 | TX/RX DMA 完成或 stall |
| `CORRELATING` | raw capture 完整 | 计算 codeword、极性、CRC、margin、skew | accepted 或 rejected |
| `REFINING` | accepted 但窗口仍可缩小 | 更新下一轮 bounded search interval | 达到 profile resolution 或最大轮数 |
| `ACCEPTED` | 重复性、CRC、generation/freshness 通过 | 发布 guarded evidence，准备恢复 NORMAL persona | 明确提交 staging 或 STOP |
| `REJECTED` | 缺 marker、坏 CRC、低 margin、DMA/stall、epoch 不匹配 | 记录 reason，停止训练并恢复 persona | 新请求或 STOP |

训练不能在 `DATA_RUNNING` 状态直接切入 TDMA cyclic service；必须先完成 persona teardown、DMA abort/clear、PIO FIFO clear 和 NORMAL persona restore，再由显式 `START` 开启 TDMA。

## 6. Codeword 与证据格式

训练 codeword 由校准域生成，TDMA 只搬运 packed words：

```text
SOF | epoch | train_sequence | marker_id | payload_pattern | inverse/polarity | CRC | EOF
```

codeword 必须具有足够 transition density，正向和反向 pattern 可区分，header 带 epoch、sequence、codebook_id，CRC 覆盖 header 和 payload。capture buffer 使用 TDMA owner 的专用 buffer，core0 不读取正在写入的 buffer。

每次 trial 必须发布：

```text
board_unique_id, logical_slot, predecessor/successor slot
topology_generation, topology_crc32, profile_crc32, schedule_crc32
train_epoch, train_sequence, codebook_id
marker_capture_tick, marker_forward_tick
data_best_lag, second_lag, margin, marker_data_skew_ns
polarity, crc, sequence result, sample_period_ns, timestamp_flags
dma_overrun, pio_stall, timeout, accepted, reject_reason
```

raw evidence 必须保留；Calibration 只在质量门禁完成后发布 candidate/accepted summary，TDMA 不消费 raw buffer 的解释结果。

## 7. 搜索与收敛算法

1. 使用 P3 path-delay candidate 和 accepted topology 建立有界初始区间，并记录 `DIAGNOSTIC_ONLY` 来源。
2. 在区间内以 codeword 相关峰搜索 `N` 和采样相位，先保证 marker、DATA、CRC 三者同时通过。
3. 对通过点重复 trial，统计 best lag、second peak、margin、skew、DMA/stall 和频率/占空比。
4. 以 accepted peak 为中心缩小下一轮区间；margin 下降、峰值混合、skew 超限或 CRC 失败时回退上一轮。
5. 达到当前 profile 的 PIO/DMA 分辨率后停止缩小，生成 `training_window_start/end`、`data_offset` 和 `guard`。
6. 只有 topology/profile/bias/generation/freshness/CRC 全部满足，才允许提交 active candidate；否则只保留 diagnostic staging。

输出的 `data_offset` 是 marker 后的相对采样位置，不是把 `path_delay` 硬编码到接收器等待循环。TDMA 使用它建立本节点 RX 窗口，整圈 `loop_delay` 仍由各段 path-delay、residence 和 guard 汇总得到。

## 8. TDMA 接入顺序

四板必须按 accepted topology 的相邻链路分别执行：

```text
NO.1 -> NO.2
NO.2 -> NO.3
NO.3 -> NO.4
NO.4 -> NO.1
```

每条链路完成 `ACCEPTED` 后，Calibration 生成 per-link staging record。TDMA 接入顺序固定为：

```text
STOP -> topology/profile readback -> stage per-link training window
     -> validate generation/CRC/freshness -> ARM
     -> optional CLK coarse training -> marker/data evidence check
     -> restore NORMAL persona -> explicit START
```

如果任一链路缺少 accepted window，ARM 必须拒绝，不能让 NO.1 单独发出 cyclic 数据后把后续节点置于无窗口状态。运行中 marker/data 训练只允许作为显式 maintenance epoch，不能与 cyclic TDMA 共用 PIO/SM/DMA。

## 9. 候选接口与代码落点

以下接口是实施候选，冻结前必须先有 host test、core1 test 和 HIL evidence：

| 目的 | 代码落点 |
|---|---|
| 训练请求/snapshot | `components/tdma/inc/tdma_pio_spi_phys.h`、`components/tdma/src/tdma_pio_spi_phys.c` |
| core1 intent owner | `components/tdma/inc/tdma_runtime_owner.h`、`components/tdma/src/tdma_runtime_owner.c` |
| ring staging/ARM gate | `components/tdma/inc/tdma_ring_runtime.h`、`components/tdma/src/tdma_ring_runtime.c`、`components/tdma/src/tdma_service.c` |
| adapter RX window | `components/tdma/inc/tdma_pio_spi_ring_adapter.h`、`components/tdma/src/tdma_pio_spi_ring_adapter.c` |
| SCPI maintenance command | `middleware/scpi_port/inc/scpi_calibration_commands.h`、`middleware/scpi_port/src/scpi_calibration_commands.c` |
| host validation | `tools/calibration_ring_validate/calibration_*.py` |
| evidence/test record | `docs/calibration/CALIBRATION_TASK_PROGRESS.md` |

当前 `CLOCK_CODED` 接口只描述 CLK coded raw transport，尚未表达 marker/data 相对窗口、CS cut-through、per-link 角色和 TDMA staging generation；应先扩展 snapshot/intent 语义，再复用底层 DMA 资源。

## 10. 验收门禁

### 单跳门禁

- marker 捕获率、DATA codeword 匹配率和 CRC 通过率分别记录；不能只看 `rx_dma_produced_words`；
- CS marker 识别后 successor 的 marker 转发延迟必须有 core1 evidence，且不得由 core0 参与；
- `rx_magic_fail_count`、DMA overrun、PIO stall、timeout 在稳定窗口内不增长；
- best lag 重复范围、margin、marker/data skew 和占空比满足 operating profile；
- NO.1 -> NO.2 先通过，再扩展到其他三条链路。

### 四板门禁

- 四条 directed link 都有同一 topology/profile generation 下的 accepted evidence；
- 每个节点都能在 marker 相对窗口内解析 DATA，并立即转发 CS marker；
- 四板 TDMA 的 `up_running/down_running`、adapter RX/TX、反馈 sequence/CRC 同时增长；
- 任一链路训练失败都使整圈回到 STOPPED，不允许部分启动。

### active calibration 门禁

- endpoint bias/reference loopback 已生成有效 bias generation；
- 四时间戳 hardware latch、resolution、flags、重复性和 asymmetry gate 通过；
- topology/profile/schedule CRC 和 freshness 一致；
- table CRC 正确，且不得带 `DIAGNOSTIC_ONLY`；
- accepted table 写入 SD/Flash 前保留证据文件和可回滚 generation。

## 11. 回退与交付顺序

marker timeout、低 margin、CRC/sequence/epoch 错、DMA overrun、PIO stall 或掉线均必须停止本轮、清 FIFO/DMA、恢复 NORMAL persona，并保持 active generation 不变。交付顺序固定为：

```text
TRN-01-A 文档/字段冻结候选
TRN-01-B 环路 marker capture/cut-through
TRN-02-A NO.1->NO.2 DATA offset 粗搜与细搜
TRN-02-B 四条 directed link window 训练
TRN-03-A TDMA per-link staging/ARM gate
TRN-03-B 四板 TDMA 短帧/FIFO 闭环
TRN-03-C path-delay/loop-delay 汇总、active gate、长稳与持久化
```

任何阶段未通过都保持 `DIAGNOSTIC_ONLY`，不得直接进入 DPLL。
