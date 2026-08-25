# 校准训练子域：Marker/Data 对齐与 TDMA 接入方案

Status: Active
Domain: CALIBRATION / TRAINING
Canonical: `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`
Related: `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/calibration/CALIBRATION_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`
Last updated: 2026-08-25

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
硬件检测 CS marker 首边沿
    -> 锁存本地 capture_origin，并立即由 TX PIO 转发给下一个节点
    -> DMA 继续捕获 marker/codeword，后处理校验 epoch/sequence/CRC
    -> 按 T_base + offset_link 建立 DATA search window
    -> 采集 DATA raw bits
    -> codeword correlation + polarity + CRC + sequence 校验
    -> 发布 accepted/rejected evidence
```

CS 的 cut-through 转发只允许使用 core1 持有的 PIO/SM/DMA 原语；转发路径不得等待
core0、USB、日志、完整 marker/codeword 解析或业务 FIFO。首边沿是转发资格，完整性校验是同一
trial 的后处理门禁；后处理失败必须记录链路级 reason，并停止当前训练 epoch，不能倒推首边沿
不存在，也不能把相关器在低质量波形上的 `polarity` 猜测直接解释成物理反相。

这里的 `N` 只调节接收 capture window，marker forward 的 PIO delay 固定引用
`TDMA_PIO_SPI_MARKER_FORWARD_DELAY_CYCLES`，不参与 offset 训练。capture 的动态加载关系为：

```text
link_i_base_delay = link_i_delay / 2
offset_link = measured_window_delay - link_i_base_delay
capture_delay_cycles = half_chip_samples(codebook) + offset_sample_count
```

因此 offset 为负表示把接收窗口向前挪，为正表示向后挪。候选初值来自当前有效 PIO 时钟周期
和每条 link 独立测得的粗窗口；不同线长必须分别计算 `link_i_base_delay`，不能复用全环平均值。
每次缩小搜索范围必须绑定 profile、topology、epoch 和 codebook。当前训练范围引用
`CALIBRATION_TRAINING_MARKER_MIN_OFFSET_SAMPLES` 和
`CALIBRATION_TRAINING_MARKER_MAX_OFFSET_SAMPLES`；最终 `capture_delay_cycles` 还必须满足
`CALIBRATION_TRAINING_MARKER_MAX_CAPTURE_DELAY_CYCLES`，所以范围是当前 profile 的搜索能力，
不是所有 codebook 都能无条件加载的范围。当前第三阶段约 `81 ns` 的观测值只能作为诊断输入
快照，不能写成训练算法的硬编码事实。

marker 与 DATA 走公共传播路径时，接收端使用相对间隔而非绝对延迟：

```text
T_sample(link)
    = marker_capture(link)
    + T_base(codebook/profile)
    + offset_link
```

当前 40 ns half-chip 是一个 codebook/profile 基准快照，不能作为所有 profile 的硬编码常量。
对当前 profile，训练的核心产物是每条 directed link 的 `offset_link`：它由已知 DATA codeword
在 4 ns raw sample 网格上的相关峰产生，先保存 `offset_sample_count`，再由
`offset_ns = offset_sample_count * sample_period_ns` 派生。边沿恰好落在采样周期附近时可能量化到
相邻 sample，因此质量门必须保留 best/second peak、margin、重复分布和 PWD，而不能把单次
±1 sample 变化提升为新的线路事实。

`offset_link` 是 marker 锚定后的相对接收窗口修正，不等于纯线缆传播延迟，也不等于把
ISO1452 的 driver/receiver delay 再加到 40 ns。每个节点使用自己的本地 marker capture 和本段
offset；四段 offset 是四个独立状态量，分别训练、分别绑定 source/destination board identity、
build、topology/profile/schedule/calibration generation 和节点当前 persona/state。任一节点状态、
PIO persona、时钟、收发器使能序列或链路条件改变，只使关联 directed link 的旧 offset stale，
随后由整圈完整性门决定是否要求其他链路一起重训；禁止先取四段平均值再回填每段。

若 marker 与 DATA 走不同收发器或不同物理对，则额外输出 `marker_data_skew_ns`；该 skew 必须单独统计和设门，不能被 path-delay 平均值掩盖。

## 2.1 Node/link/loop 命名与四维 offset 矩阵

物理拓扑只使用 `node`、`link` 和 `loop`：`node_i` 是第 `i` 个物理节点，`link_i` 是
`node_i -> node_(i+1 mod node_count)` 的有向物理链路，`node_i_loop` 是以 `node_i` 为
origin、经过全部 link 后返回该节点的完整环路。训练层不得使用 `slot`；跨到 RefMem/TDMA
已有插槽字段时，只允许在边界执行 `node_index <-> slot_id` 显式映射。

每条有向链路的延迟分解固定为：

```text
link_i_delay
    = node_i_driver_delay
    + link_i_path_delay
    + node_(i+1)_receiver_delay
    + link_i_capture_quantization
```

整圈结果命名为 `node_i_loop_delay`。它是从 `node_i` 出发的全部 link delay、各中继节点
forward residence 和 guard 的组合，不得替代任何单条 `link_i_delay`。例如 `node0 -> node1`
的接收端组成应命名为 `node1_receiver_delay`；禁止把 receiver delay 归到 source node，或把
driver/receiver datasheet delay 在端到端实测值上重复相加。

当前训练 offset 是按 source node 装载到 PIO capture 相位的独立状态量；forward delay 保持
固定。矩阵必须保留
所有节点维度的笛卡尔积；当前代码事实源为
`tools/calibration_ring_validate/calibration_marker_train.py::build_offset_matrix()`。筛选参数只决定
本轮执行哪些矩阵行，不能从输出中删除未执行行或把固定为零的维度降掉。每行至少保存：

```text
offset_sample_counts_by_node[node]
offset_ns_by_node[node]
executed, accepted_nodes, reject_reason_by_node
normal/inverted distance, marker flags, raw capture reference
```

固定一个 origin 的单次多节点 trial，直接得到 successor 方向的若干 `link` capture 和该 origin
的一个 `loop` capture；它不是“全部 link 都已独立测量”。要完成全量 directed-link 判断，必须
轮换 reference/origin，使每条 `link_i` 至少一次成为可直接与本地期望 marker 对照的测量对象。

## 2.2 收发驱动器时序预算与去嵌入

当前产品板的训练 marker 经 ISO1452 全双工隔离收发器传输。训练必须按明确的测量端点拆分
一跳路径，不能把“线路延迟”和“板间端到端延迟”混为一项：

```text
t_link_observed
    = t_driver_propagation
    + t_line_propagation
    + t_receiver_propagation
    + t_capture_quantization
```

其中 `t_link_observed` 的端点是发送板的 RP2350 输出 GPIO 到下一板的 RP2350 输入 GPIO；
因此它已经包含发送端驱动器传播、线缆/连接器传播和接收端接收器传播。已经由 P3 或后续
marker 训练得到的端到端一跳观测值，不能再叠加驱动器或接收器数据手册传播时间。

PIO cut-through residence 的端点不同：它从本板接收 GPIO 的已观测边沿开始，到本板发送
GPIO 的输出边沿结束，只描述 PIO/SM 本地转发路径。接收器传播发生在 residence 起点之前，
驱动器传播发生在 residence 终点之后，因此两者不进入 `forward_residence`，而进入相邻的
`t_link_observed`。任何 evidence 都必须携带 `measurement_endpoint` 或等价枚举，禁止把这两类
时延直接相加后再次计入整圈预算。

当前产品板网表映射如下（网表快照，非器件或布线事实源；事实源为
`docs/hardware/Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` 和实际装配/线束核验）：

```text
GPIO26 / RJ45_FWD_OUT
  -> ISO1452 U12 D
  -> U12 Y/Z
  -> RJ2 TRIG_OUT_P/N
  -> cable
  -> RJ1 TRIG_IN_P/N
  -> U12 A/B
  -> U12 R
  -> GPIO27 / RJ45_FWD_IN
```

`D -> Y/Z` 是发送驱动器路径，`A/B -> R` 是接收器路径；`DE` 为高有效驱动使能，
`/RE` 为低有效接收使能。ISO1452 数据手册的全双工真值关系是：`D=H` 时 `Y=H/Z=L`，
`D=L` 时 `Y=L/Z=H`；`A-B` 为正时 `R=H`，为负时 `R=L`。因此 `Y -> P -> A`、
`Z -> N -> B` 的直通连接保持 `D -> R` 同相。P/N、Y/Z、A/B 或线束极性必须作为原始 evidence 检查，相关器检测到
反相时必须记录 `polarity` 并拒绝未声明的 active candidate，不能静默翻转后把它报告为正常链路。

当前网表把输入 P/N 分别连接到 U12 A/B、输出 P/N 分别连接到 U12 Y/Z，RJ1/RJ2 的
TRIG 对使用相同 pin 编号。配合 pin-to-pin 直通网线时，架构预期是每跳逻辑同相，不存在
“每经过一板固定反相”的设计语义。P2P 只约束连接拓扑，不能替代 pin-to-pin 连通和器件真值表
核验；但在线缆确认同 pin 直通后，训练中出现奇数跳反相、偶数跳同相时，应先归类为
“极性路径不一致”诊断：依次核对 ISO1452 `D -> Y/Z`、`A/B -> R` 真值、实际焊接/
连接器 pin map、PIO cut-through 输出电平和相关器 raw-bit 定义。在这些证据闭环前，不得增加
默认软件反相，也不得把交替极性发布为正常 active profile。

下表是当前 ISO1452 数据手册的诊断预算快照（快照，非事实源）。正式 active gate 必须绑定
实际 BOM 料号、datasheet revision、硬件版本、温度、供电、端接和线缆条件，并以
`docs/hardware/外围硬件手册/iso1452.pdf` 或更新后的受控器件资料复核：

| 路径参数 | 典型值快照 | 最大值快照 | 训练中的用途 |
|---|---:|---:|---|
| 驱动器 propagation delay | `19 ns` | `41 ns` | 端到端一跳延迟的发送端组成；只在没有实测端到端值时参与粗窗口预算 |
| 接收器 propagation delay | `36 ns` | `60 ns` | 端到端一跳延迟的接收端组成；只在没有实测端到端值时参与粗窗口预算 |
| 驱动器 pulse-width distortion | `1 ns` | `6 ns` | 侵蚀码元低/高脉宽和相关眼宽，不作为公共传播平移重复相加 |
| 接收器 pulse-width distortion | `2 ns` | `6 ns` | 与驱动器 PWD 合并形成最坏脉宽/眼宽预算 |
| 驱动器 enable delay | `32 ns` | `78 ns` | `DE` 打开后的 maintenance/ARM 建稳 guard，不进入稳态逐边沿 propagation |
| 驱动器 disable delay | `25 ns` | `46 ns` | persona teardown/三态切换 guard，不进入稳态逐边沿 propagation |
| 接收器 enable delay | `5 ns` | `20 ns` | `/RE` 打开后的 maintenance/ARM 建稳 guard，不进入稳态逐边沿 propagation |
| 接收器 disable delay | `9 ns` | `30 ns` | persona teardown/三态切换 guard，不进入稳态逐边沿 propagation |
| 驱动器差分上升/下降时间（5 V 总线侧测试条件） | `4.7 ns` | `6 ns` | 边沿带宽、码元眼宽和采样量化预算 |
| 接收器输出上升/下降时间 | `1 ns` | `4 ns` | GPIO 端边沿、码元眼宽和采样量化预算 |

这些参数必须按用途分开处理：

1. **传播延迟**整体平移波形。已有 `t_link_observed` 时，只保留为 datasheet plausibility 和
   温漂/批差 guard，不能再加到观测值，也不能从 codebook half-chip 中扣除。
2. **PWD**改变高、低脉宽并侵蚀眼宽。最坏预算可保守使用
   `driver_pwd_max_ns + receiver_pwd_max_ns`，但必须由实际相关峰、占空比和 margin 再验证。
3. **enable delay**只约束驱动器/接收器使能后的启动 guard。训练必须先建立输出 idle latch、
   方向和 RX capture，再等待使能建稳；teardown 时还要覆盖 disable delay。enable/disable delay
   都不是每个 bit 重复发生的延迟。
4. **纯线路去嵌入**只允许生成 diagnostic 派生量：

   ```text
   t_line_diagnostic
       = t_link_observed
       - t_driver_propagation_estimate
       - t_receiver_propagation_estimate
   ```

   典型值只能形成典型线路估计；最大值是安全上界，不能机械地从单次观测中相减并把负数当作
   线路事实。TDMA/Calibration 的运行补偿仍使用端到端 `t_link_observed`，不使用去嵌入后的
   `t_line_diagnostic` 替代真实一跳路径。

训练工具的物理预算 evidence 至少保存：

```text
observed_link_delay_ns
driver_propagation_typ_ns, driver_propagation_max_ns
receiver_propagation_typ_ns, receiver_propagation_max_ns
driver_pwd_max_ns, receiver_pwd_max_ns
driver_enable_max_ns, receiver_enable_max_ns
observed_link_delay_includes_driver_line_and_receiver
observed_link_delay_is_end_to_end_training_delay
do_not_add_component_propagation_to_observed_delay
component_propagation_deembedding_is_line_diagnostic_only
```

这些字段的当前代码落点是
`tools/calibration_ring_validate/calibration_marker_train.py::physical_timing_budget()`。它们属于
诊断 provenance，不得由 SCPI 写入 active path-delay table，也不能替代每轮的 raw capture、
相关峰、CRC、极性和 generation/freshness 证据。

## 2.3 专业术语与 EtherCAT 对照

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

## 2.4 历史实测 delay ledger

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
| `TRN-02` | Marker-Anchored DATA Window Training（marker 锚定 DATA 窗口训练） | TRN-01 marker origin、P3 `path_delay` diagnostic candidate、PIO sample period、DATA codeword | 每条 link 的 `data_offset`、`training_window`、`guard`、`marker_data_skew`、correlation/margin/CRC 证据 | 单跳先通过；四条 directed link 均在同一 generation/profile 下重复通过；窗口达到当前 PIO 分辨率或明确拒绝原因 |
| `TRN-03` | TDMA Short-Frame/FIFO Closed Loop（TDMA 短帧/FIFO 闭环接入） | TRN-02 per-link window、PIO instruction-cycle profile、loop-delay/residence 汇总、topology/profile/schedule CRC | TDMA per-link staging、ARM gate、link/forward budget、短帧 TX/RX FIFO 计数、sequence/CRC/feedback evidence、active candidate | 全部链路 accepted 且指令周期预算可重放后才能 ARM；四板 up/down、FIFO、sequence/CRC 同时增长；失败统一 STOPPED 并保持旧 active generation |

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

第二阶段在每个本地 marker origin 之后发送/捕获已知 DATA codeword。候选区间以当前
codebook/profile 的 `T_base` 为中心，由 P3 path-delay 粗预算、PIO pipeline 和 guard 形成有界
`offset_link` 搜索区间；实际接收位置通过 correlation、极性、CRC、sequence 和 margin 确认，
而不是把 `81 ns` 写成固定等待值，也不是把收发器数据手册延迟重复加到 `T_base`。

实施顺序固定为：

```text
NO.1 -> NO.2 单跳粗搜
    -> 单跳细搜/重复统计
    -> NO.2 -> NO.3、NO.3 -> NO.4、NO.4 -> NO.1
    -> 四条 directed link 的 window 一致性检查
```

TRN-02 的结果按 `T_base + offset_link` 表达为相对接收窗口，不是新的绝对 `path_delay`。
staging 必须同时保存 `base_half_chip_ns`、`offset_sample_count`、`sample_period_ns`、
`offset_ns` 和最终 window/guard，禁止只保存相加后的单个数字而丢失量化来源。如果 marker 和 DATA 的物理
路径不完全相同，`marker_data_skew_ns` 必须单独统计；不能用整圈 RTT 或平均 path-delay
掩盖该偏差。

### TRN-03：TDMA 短帧/FIFO 闭环接入

第三阶段才装载按 ring role 配置的产品 cyclic flight persona，把 TRN-02 的 per-link window 绑定到 TDMA adapter，
并通过显式 `STOP -> stage -> validate -> ARM -> START` 接入短帧和 TX/RX FIFO。ARM 前
必须冻结本次 profile 的 PIO 指令周期预算；core1
负责 PIO/SM/DMA、飞行转发和 FIFO 搬运；core0 只处理已完成帧和 guarded snapshot。

TRN-03 replay matrix 必须由固化工具
`tools/calibration_ring_validate/trn03_matrix.py` 从同一 operating profile 下成对的
TRN-02 DATA repeat matrix 和 TRN-01 residence matrix 生成。生成器必须先验证
calibration/topology/profile/schedule identity、完整 link 集、重复跨度、hardware-latched
DATA、forward residence 和 loop delay，再从证据派生每条 link 的周期预算；禁止人工填写
`forward_residence_cycles` 或用另一 generation/profile 的 residence 补齐矩阵。
`tools/calibration_ring_validate/trn02_profile_gate.py` 负责固定阶梯的成对证据门禁，
`tools/calibration_ring_validate/trn03_stage.py` 负责完整 matrix 写后读回、ARM 状态读回和
STOPPED 回退验证。

训练层输入、派生矩阵和报告只使用 `node/link/loop`。读取 TDMA/RefMem 既有插槽状态时，
工具只在边界做 `node_index <-> slot_id` 映射，对训练层输出重新暴露为 node 字段；不得把
`slot` 作为新的训练层索引或时序概念。

### TRN-03 的 TDMA 指令周期预算

第三阶段的时间基准不是 RTOS 任务执行时间，而是 PIO state machine 的实际指令周期。
预算必须从代码和 profile 事实源生成并随 staging 一起校验：

```text
pio_instruction_period_ns = derived from clkdiv and clock_get_hz(clk_sys)
tdma_bit_period_ns         = bit_cycles * pio_instruction_period_ns
marker_to_data_cycles  = marker_gap_cycles + local_pipeline_cycles
forward_residence_cycles = forward_rx_cycles + forward_tx_cycles
link_budget_cycles     = rx_arm_lead_cycles
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
rx_arm_lead_cycles, codeword_cycles, guard_cycles, link_budget_cycles
loop_delay_ns, loop_delay_tolerance_ns, loop_delay_cycles
cycle_period_ns, baud_hz, profile_crc32, schedule_crc32
```

任何 `clkdiv`、PIO persona、`baud_hz`、码本长度或 `cycle_period_ns` 变化都必须使旧
training window stale 并重新训练。`cycle_period_ns` 是 TDMA 调度周期，不能替代 PIO
instruction period；core1 的 RTOS service 只允许在已预留的 bounded budget 内搬运 FIFO，
不能进入 marker、采样边沿或 cut-through 的关键路径。

第三阶段门禁分为两个连续层级：

- `raw-flight`：只验证原始短帧已经脱离“完整 RX 后由 core1 再次发送”的热路径；reference
  使用 `TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN`，其余 node 使用
  `TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER`。本层要求 PIO/DMA 的 DATA、CS/SCK、
  sequence、CRC 和物理计数形成整圈证据，但不要求本节点 process-image segment 已被替换；
- `process-image`：在 `raw-flight` 通过后，进一步验证 active TX image、固定 segment 替换、
  RX mirror/FIFO、map apply、WKC/尾部完整性和 core0 拥塞隔离。透明 byte pipeline 不能作为
  本层通过证据。

两个层级共同要求：

- 四条 directed link 均有同一 topology/profile/calibration generation 的 accepted window；
- `loop_delay` 只用于整圈返回预算、反馈窗口和 timeout，不替代 per-link `data_offset`；
- 每条 link 的 `link_budget_cycles`、`rx_arm_lead_cycles`、`forward_residence_cycles` 和
  `loop_delay_cycles` 均能由当前 PIO persona 重放，不能依赖一次性的 core0/RTOS 调度时刻；
- `raw-flight` 的 sequence、CRC、物理 RX/TX 和 up/down 状态同时增长；`process-image` 再要求
  TX/RX FIFO、segment replacement 和 map apply 同时增长；
- 任一链路失败、窗口过期或 generation 不一致，ARM 必须拒绝并恢复 STOPPED；
- 只有通过重复性、freshness、hardware latch、bias 和 rollback 门禁，才可形成 active candidate。

TRN-03B 的失败分析必须复用板端 wire capture 闭环，而不能只依据运行计数器猜测。历史
`copy_normal_capture` API 名称为兼容接口，不代表运行 persona 必须是 NORMAL；它同时允许
NORMAL、`FLIGHT_ORIGIN` 和 `FLIGHT_FOLLOWER` 的可诊断 wire capture。TDMA core1 owner
保存最近的 RX SCK 上升沿采样流以及最近一个完整 TX 帧；容量和格式
分别引用 `TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES`、
`TDMA_PIO_SPI_NORMAL_CAPTURE_VERSION`。Calibration 通过 guarded intent 请求 core1 锁存，
随后由 core0 storage owner 写入 SD；host 再并行下载各 node 证据，按训练配置中的 marker/data
方向矩阵生成每节点 SVG。分析窗口由 host 参数提供；当前 HIL 使用的 `1 us` 仅是本轮证据
快照，不是协议常量。

捕获 intent 使用 sequence 区分重试并采用 latest-wins：新的诊断请求可以覆盖仍处于
`PENDING` 的旧请求，generation/epoch 继续阻止旧 snapshot 被保存。查询必须同时发布
`core1_service_count`、`intent_read_fail_count`、`last_seen_sequence`、
`copy_attempt_count`、`copy_fail_count` 和 `consumed_sequence`，从而区分 core1 未运行、
intent 未读到、物理快照复制失败和已经消费但保存身份不匹配。host 允许有界自动重试，
最终无论闭环或捕获是否通过都必须执行 STOP。该诊断闭环只解释 TRN-03B 失败，不改变
active calibration，也不允许 SD/SCPI 进入 PIO 边沿热路径。

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
| `ACCEPTED` | 重复性、CRC、generation/freshness 通过 | 发布 guarded evidence，准备切换到当前产品 cyclic persona | 明确提交 staging 或 STOP |
| `REJECTED` | 缺 marker、坏 CRC、低 margin、DMA/stall、epoch 不匹配 | 记录 reason，停止训练并恢复已配置 persona | 新请求或 STOP |

训练不能在 `DATA_RUNNING` 状态直接切入 TDMA cyclic service；必须先完成 persona teardown、
DMA abort/clear、PIO FIFO clear，并按 ring role 装载当前产品 cyclic persona，再由显式 `START`
开启 TDMA。当前 PIO-SPI 产品路径对应 `FLIGHT_ORIGIN/FLIGHT_FOLLOWER`，不得写死为 NORMAL。

## 6. Codeword 与证据格式

训练 codeword 由校准域生成，TDMA 只搬运 packed words：

```text
SOF | epoch | train_sequence | marker_id | payload_pattern | inverse/polarity | CRC | EOF
```

codeword 必须具有足够 transition density，正向和反向 pattern 可区分，header 带 epoch、sequence、codebook_id，CRC 覆盖 header 和 payload。capture buffer 使用 TDMA owner 的专用 buffer，core0 不读取正在写入的 buffer。

每次 trial 必须发布：

```text
board_unique_id, logical_node, predecessor/successor node
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
4. 每个失败点也是有效训练 evidence，必须保存搜索区间、best/second lag、normal/inverted distance、
   polarity、marker flags、reject reason 和节点状态。`SEARCH_RANGE/CAPTURE_TRUNCATED` 用于扩大或
   平移下一轮区间，`DISTANCE/MARGIN/MANCHESTER` 用于调整采样相位、guard 或码本，
   `POLARITY/CRC/HEADER` 用于转入物理路径或状态一致性诊断；禁止只保留 accepted trial。
5. 以 accepted peak 为中心缩小下一轮区间；margin 下降、峰值混合、skew 超限或 CRC 失败时回退上一轮。
6. 达到当前 profile 的 PIO/DMA 分辨率后停止缩小，生成 `training_window_start/end`、`data_offset` 和 `guard`。
7. 只有 topology/profile/bias/generation/freshness/CRC 全部满足，才允许提交 active candidate；否则只保留 diagnostic staging。

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
     -> load configured cyclic flight persona -> explicit START
     -> raw-flight gate -> process-image gate
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

### 四板 raw-flight 门禁

- 四条 directed link 都有同一 topology/profile generation 下的 accepted evidence；
- 每个 node 按 ring role 装载正确 flight persona，follower 不产生第二次 software TX；
- 四板 TDMA 的 `up_running/down_running`、物理 RX/TX、反馈 sequence/CRC 同时增长；
- bit-shift scanner 可解释非字节对齐返回流，坏帧、timeout、stall 和 overrun 不增长；
- 任一链路训练失败都使整圈回到 STOPPED，不允许部分启动。

### 四板 process-image 门禁

- `raw-flight` 已在相同 topology/profile/calibration generation 下通过；
- 每个 node 在固定 offset 到达时只替换自己拥有的 segment，其余 byte 保持流水；
- active TX image generation、RX mirror/FIFO、segment bitmap、map apply、WKC 和尾部完整性证据一致；
- core0 延迟或 RX FIFO 拥塞不能停止 wire forwarding，失败仍统一回到 STOPPED。

### active calibration 门禁

- endpoint bias/reference loopback 已生成有效 bias generation；
- 四时间戳 hardware latch、resolution、flags、重复性和 asymmetry gate 通过；
- topology/profile/schedule CRC 和 freshness 一致；
- table CRC 正确，且不得带 `DIAGNOSTIC_ONLY`；
- accepted table 写入 SD/Flash 前保留证据文件和可回滚 generation。

## 11. 回退与交付顺序

marker timeout、低 margin、CRC/sequence/epoch 错、DMA overrun、PIO stall 或掉线均必须停止本轮、
清 FIFO/DMA、恢复已配置的产品 cyclic persona，并保持 active generation 不变。交付顺序固定为：

```text
TRN-01-A 文档/字段冻结候选
TRN-01-B 环路 marker capture/cut-through
TRN-02-A NO.1->NO.2 DATA offset 粗搜与细搜
TRN-02-B 四条 directed link window 训练
TRN-03-A TDMA per-link staging/ARM gate
TRN-03-B1 四板 TDMA raw-flight 闭环
TRN-03-B2 四板 TDMA process-image/FIFO 闭环
TRN-03-C path-delay/loop-delay 汇总、active gate、长稳与持久化
```

任何阶段未通过都保持 `DIAGNOSTIC_ONLY`，不得直接进入 DPLL。
