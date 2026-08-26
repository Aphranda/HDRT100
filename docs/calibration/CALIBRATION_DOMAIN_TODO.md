# 校准域待办

Status: Active
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`
Related: `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`, `docs/calibration/CALIBRATION_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-08-27

本文档把 [`CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`](CALIBRATION_TDMA_CLK_TRAINING_PLAN.md) 和
[`CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`](CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md)
拆成可执行的校准域任务。校准域拥有物理测量、residence、endpoint bias、path-delay、
统计质量、generation/freshness 和 active/staging 接受门禁；TDMA 只拥有训练 persona、
PIO/SM/DMA/core1 资源、窗口编排和 raw evidence transport；VDC/DPLL 只消费 accepted
calibration 并建立 `local_tick_raw <-> vdc_time` 映射。

状态标记：[x] 已完成，`[~]` 进行中，[ ] 待办，`[!]` 阻塞。本文档是任务分解，
不是新的跨域冻结契约；候选 wire layout、阈值和 SCPI 拼写仍以训练方案中的 candidate
状态为准，冻结时遵循文档登记流程。

## 一、当前总览

| 阶段 | 目标 | 当前状态 | 交付物 |
|---|---|---|---|
| P0T | 线序、邻接矩阵和环路顺序校准 | `[~]` | accepted topology snapshot、node map 和 freshness |
| P0 | 硬件 latch、证据 transport 和 owner 边界 | `[~]` | 可关联的 `t1..t4` hardware-latched evidence |
| P1 | CLK RTT 粗捕获收尾 | `[~]` | diagnostic bracket、过渡抖动和拒绝原因 |
| P2 | 编码 marker、过采样和相关测距 | `[~]` | accepted/rejected coded RTT snapshot |
| P3 | 双向同时对比、residence 和 per-link delay | `[~]` | generic forward/return/sync 映射已接入；diagnostic per-link evidence 已形成；active/staging 待 bias/freshness gate |
| P4 | VDC/DPLL 接入与长时间验证 | [ ] | calibration-to-VDC gate evidence |

当前不能把第一阶段的 CLK RTT bracket、软件 timer 或 diagnostic latch 直接用于 VDC/DPLL。
正式校准必须同时满足硬件 latch、质量门禁、重复统计、拓扑/profile freshness 和恢复流程。

## 训练子域三阶段待办：Marker/Data/TDMA

训练子域按三个新阶段推进。旧校准 `P1/P2/P3` 只提供粗窗口、marker 基线和
`path_delay` candidate，不替代下面三个阶段的实际收发门禁。

| 阶段 | 阶段目标 | 主要输入 | 阶段输出 | 进入下一阶段 |
|---|---|---|---|---|
| `TRN-01` | 环路 marker 捕获与 core1/PIO cut-through | accepted topology、P2 codebook、epoch/sequence | 每跳 capture/forward tick、residence、整圈 marker RTT | 四节点同 epoch 捕获、顺序正确、每跳延迟有界、无 PIO/DMA fault |
| `TRN-02` | marker 锚定 DATA 码元时隙 | TRN-01 local origin、P3 `path_delay` candidate、PIO sample period | per-link `data_offset`、window、guard、skew、correlation/margin | 单跳和四条 directed link 重复通过，generation/profile/residence 一致 |
| `TRN-03` | TDMA 短帧/FIFO 闭环接入 | TRN-02 DATA windows、独立 SCK offset matrix、PIO instruction-cycle profile、loop-delay/residence、topology/profile CRC | staging/ARM、link/forward budget、短帧 TX/RX FIFO、sequence/CRC、active candidate | MARK、SCK、DATA 均 accepted，四板 up/down 和 FIFO 同时增长，且周期预算可重放；失败统一 STOPPED |

### TRN-01：环路 marker 捕获与切通

| ID | 待办 | 状态 | 退出门禁 |
|---|---|---|---|
| TRN-01A | 冻结 marker trial 的 epoch、sequence、marker_id、CRC、polarity、capture/forward tick 和 raw evidence 字段 | [x] | C snapshot、板端查询、host parser、字段数测试和 SD raw capture 已完成端到端一致性验证 |
| TRN-01B | 在 TDMA core1 owner 增加独立 marker PIO persona，支持 marker line 选择、固定 cut-through 和 DMA capture | [x] | PIO catalog/resource gate 和四板 HIL 通过；训练前显式 STOP，未与 cyclic TDMA 并发 |
| TRN-01C | 实现 Calibration intent 到 core1 的 bounded mailbox/prepare-ack，SCPI 不直接触碰 PIO/SM/DMA | [x] | ARM/INJECT 两阶段 mailbox、guarded snapshot、超时/拒绝矩阵、persona recovery 和 core1 owner 边界已覆盖 |
| TRN-01D | 完成 `NO.1 -> NO.2 -> NO.3 -> NO.4 -> NO.1` 环路 marker HIL | [x] | 当前 build 的零 offset 基线及一拍复核均为四节点 accepted；同 epoch/CRC、返回 marker、DMA/PIO fault 门禁通过，证据见任务记录 |

### TRN-02：marker 锚定 DATA 码元时隙

| ID | 待办 | 状态 | 退出门禁 |
|---|---|---|---|
| TRN-02A | 实现 DATA codeword 相关、极性、CRC、epoch/sequence、best/second peak 和 margin 判断 | [x] | DATA evaluator、marker-to-DATA PIO capture、板端 guarded snapshot、SCPI、SD raw capture 和正反用例回归已完成 |
| TRN-02B | 单跳 `NO.1 -> NO.2` 使用 P3 candidate 扫描 `marker -> DATA` 相对 offset，先粗后细收敛 | [x] | 固定阶梯各 profile 的四条 directed link 均完成多次 repeat；每档 DATA 与 forward residence 使用同一 identity，固化 profile gate 全绿 |
| TRN-02C | 将单跳结果形成 diagnostic training window，绑定 topology/profile/calibration generation 和 CRC | [x] | snapshot/SD capture 可读，topology/profile/schedule/calibration generation 已绑定，并保持 `DIAGNOSTIC_ONLY` |
| TRN-02D | 沿 accepted topology 完成四条 directed link 的 window 训练和固定 operating-profile 阶梯验证 | [x] | 固定阶梯全部完成完整 link 集、多次 repeat、跨度、identity、residence 和 fault-counter 门禁；证据见任务记录 |

### 统一相位训练路径

| ID | 待办 | 状态 | 退出门禁 |
|---|---|---|---|
| PHASE-TRN-BASE | MARK、SCK、DATA 共用 `link_base_delay = measured_link_delay / 2` 与 `base_samples + node_offset`，codebook half-chip 仅用于波形编码 | `[x]` | C 共用原语、host 共用计划 schema 和 MARK/SCK/DATA 专项回归通过；范围和容量引用 `CALIBRATION_TRAINING_PHASE_*` |
| PHASE-TRN-MATRIX | 三种信号共用零 offset 基线、SD raw capture、离线相关/SVG、全量 Node 笛卡尔矩阵、动态加载和 residual repeat gate | `[~]` | 共用矩阵生成器和 DATA/SCK observed matrix 已接入；四板 SCK/DATA 新固件 HIL 尚待执行，失败 trial 必须保留 |
| SCK-TRN-01 | 使用 SCK 自身 PIO 启动、已知 burst 和 raw capture 完成独立环路捕获，不引用 MARK offset 计算相位 | `[x]` | request/snapshot/SCPI/PIO/host 已移除 MARK phase 输入，SCK 使用自身 origin、per-link base 和 Node offset |
| SCK-TRN-02 | 沿 accepted topology 对每个 destination node 执行独立 STOP/ARM/inject repeat，生成全量 SCK offset matrix | `[x]` | 四板零 offset 基线与推荐矩阵动态加载均完成独立 repeat；raw capture、逐 Node SVG、直方图、众数/中位数和 residual 门禁见任务记录 |
| SCK-TRN-GATE | 在 MARK 与 SCK 分别 accepted 后验证 `mark_sck_skew` | [ ] | skew 只进入产品 guard/window 验收，不回写任一物理 offset；generation/profile/topology/stale 与 rollback 门禁通过 |

### TRN-03：TDMA 短帧/FIFO 闭环接入

| ID | 待办 | 状态 | 退出门禁 |
|---|---|---|---|
| TRN-03A | 增加 TDMA per-link staging 和 ARM gate，绑定实际 PIO persona 的周期预算以及 MARK/SCK/DATA 统一相位字段 | `[x]` | 完整矩阵写后读回、四板 ARM 以及缺 link、diagnostic-only、矩阵/预算过期拒绝与 STOPPED 回退均已复验；证据索引见 `CAL-TASK-20260826-010` |
| TRN-03B | 按 ring role 装载产品 flight persona 后启动 TDMA 短帧；先过 `raw-flight`，再过 `process-image` | `[x]` | 四板 raw-flight 与 process-image 均通过；固定 segment replacement、bitmap/WKC、尾部 CRC、TX/RX FIFO、map apply、SD raw capture 和逐 node SVG 形成同 generation 闭环；证据索引见 `CAL-TASK-20260826-010` |
| TRN-03C | 汇总 per-link path-delay、residence、loop-delay、PIO 周期预算和 residual，形成 active candidate gate | [ ] | bias、hardware latch、freshness、CRC、周期重放、重复性和 rollback 全部通过 |
| TRN-03D | 故障注入与长稳：marker timeout、低 margin、CRC/epoch 错、DMA overrun、PIO stall、掉线；固化工具和 SD/Flash 输入格式 | `[~]` | DATA 线端旧 epoch/header CRC/正向控制及 MARK 无下降沿 timeout 均已完成 release build、四板 OTA 和 HIL；有边沿的 DATA 故障保留 SD raw/SVG replay，无边沿 timeout 保留状态/counter/STOP/active 证据；其余物理故障、完整 active generation 保护和长稳仍待完成，证据索引见 `CAL-TASK-20260827-011` |

实施顺序固定为：

```text
TRN-01A..D
  -> TRN-02A..D
  -> TRN-03A..D
```

在 `TRN-01D` 通过前不得开始 DATA 时隙收敛；在 `TRN-02D` 通过前不得 ARM 四板 TDMA；
在 `TRN-03C` 通过前不得向 VDC/DPLL 发布 active calibration。

## 二、P0T 线序与环路顺序校准

- [x] 将有向线序/邻接矩阵、单闭环判定、anchor 旋转和 node map 的 owner 迁入校准域；
  TDMA 只提供隔离 probe persona、RX/TX counters 和 raw physical evidence。
- [x] 将 host 工具迁移为
  `tools/calibration_ring_validate/calibration_ring_topology.py`，并把第一阶段及码本工具统一
  改为 `calibration_*` 命名。
- [x] 按 `*IDN?` 唯一地址生成 directed adjacency；只有全部 active 节点形成一个闭环才
  允许生成 `ring_order/node_map`。COM 号不得进入 topology key。
- [x] NO 提交从 TDMA START 中移除；只有 accepted topology 可以写入 NO，且支持写后读回
  和重启持久化复核。
- [ ] 实现板内 `CalibrationTopologySnapshot`：active board set、predecessor/successor、
  direction、node map、topology CRC、generation、freshness、accepted/rejected reason 和
  raw evidence index。
- [ ] 固化重复 probe 和质量门禁：frame/word/edge evidence、timeout、bad header、误触发、
  跨轮一致性和 profile identity；阈值冻结前继续由显式参数/profile 提供。
- [ ] 增加开链、分叉、多闭环、反向接线、重复/缺失板卡、probe 串扰和中途掉线故障注入；
  覆盖直到 `TDMA_RING_NODE_MAX` 的拓扑容量。
- [ ] topology generation 或 wiring/profile freshness 改变时，使 P1/P2/P3 calibration staging
  失效；旧 active generation 只能按明确 holdover policy 使用，不得静默继承。

## 三、P0 基础件与跨域边界

- `[~]` 将校准测量 owner 从现有 `calibration_manager` 状态壳升级为
  `CalibrationAO / CalibrationFB / CalibrationVector`，保留 guarded snapshot 和
  active/staging/rollback 语义。
- [ ] 冻结 `CalibrationEvidence` 字段：板卡唯一地址、logical node、link key、
  `train_epoch`、`train_seq`、方向、topology/profile CRC、硬件时间源、分辨率、flags、
  DMA 状态、质量结果、generation 和 freshness。
- `[~]` 定义 `t1..t4` 的 latch source、resolution、overrun、window miss、epoch/sequence
  关联和拒绝原因；板间 P3 已发布 4 ns PIO/DMA 采样和同 epoch 边沿，完整 fault reason、
  topology/profile generation 仍待补齐。
- [x] 与 SYNC_IO/TDMA 对齐 `CLK/DATA/SYNC` 三线 PIO persona、DMA buffer owner、
  core1 service 入口和 raw evidence 发布方式；core0/USB/日志不得进入边沿热路径。
- [ ] 定义 endpoint bias reference loopback 的 profile、board identity、bias generation
  和失效策略；bias 未生成时只允许发布 observed value。
- [ ] 定义 active/staging calibration 的 CRC、generation、topology freshness、
  rollback 和 VDC/DPLL 消费门禁。

## 四、P1 第一阶段 CLK RTT 粗捕获

- [x] 将第一阶段测量流程、bracket 解释、四主结果和质量门禁从 TDMA 待办收敛到校准域；
  TDMA 仅保留 transport/persona/resource integration，禁止解释 RTT 或生成 delay/bias。
- [x] 完成 SPI CLK 透明转发、master burst/capture、PIO IRQ 和 guarded snapshot 的
  最小实现。
- [x] 完成四板 HIL 的数量级捕获，并记录各 profile、master、mixed point、错误增量和
  `DIAGNOSTIC_ONLY` 状态。结果详见任务记录和训练方案；该 HIL 结果是 build/topology/
  wiring/profile 快照，不是通用精度事实源。
- [x] 将第一阶段默认阶梯收敛为 operating profile level 7/8/9 对应的
  `10 -> 25 -> 30 MHz`；更高或更低兼容档仅允许显式实验，不进入默认训练定义。
- [x] 固化四主轮换的唯一板卡地址、logical node、STOP/ARM/STOP 收尾和恢复普通 persona
  检查；任一失败不得留下持续 CLK 环。
- [ ] 补齐第一阶段 rejected sample 分类：返回缺失、重复、超时、marker 不完整、
  overlap/mixed 和 topology/profile 变化。
- [ ] 将第一阶段 bracket 作为 P2 有界搜索输入，并禁止其单独生成运行态 feedback
  timeout、VDC delay 或 active per-link calibration。

## 五、P2 编码 marker 与相关测距

- [x] 完成候选 codebook 的离线评估；当前评估工具已能比较 LFSR、NRZ/Manchester/
  differential-Manchester 和 raw-sample lag margin。
- [x] 生成 C/Python golden vector，冻结 header、反码、CRC、bit order、codebook ID、
  epoch 字段和 wire waveform；冻结前不得登记为契约。
- `[~]` 用现有 CLK HIL 扫描 candidate 半码元档位；新 build `20260821062825` 上 32 ns
  四主单轮全部 accepted（lag span=1、无 DMA overrun/PIO stall），24 ns 在 NO.2--NO.4
  出现 `correlation_manchester` reject，因此 32 ns 暂为最短通过档；重复统计仍需完成，
  结果继续保持 `DIAGNOSTIC_ONLY`。
- `[~]` 实现 coded TX PIO、固定 FIFO 输入和 checked capture size；显式 quiet guard/profile
  尚待补齐。
- [x] 实现 master RX oversampling PIO + DMA；记录真实 `capture_origin`、TX/RX count、
  overrun、stall 和 buffer generation。
- `[~]` 实现 core1 有界 raw-sample 相关器，已输出 `peak`、`second_peak`、`margin`、
  Hamming distance、极性和 accepted/rejected reason；lag histogram/板内重复统计尚待完成。
- `[~]` 将 `CLOCK_COARSE -> CLOCK_CODED` 接入 TDMA owner 的非阻塞状态机；实现
  `TRAIN_PREPARE/ACK/commit`，先完成唯一地址工具编排，再完成 reference 单指令全环闭环。
- [x] 扩展 guarded snapshot，绑定 build/topology/profile/schedule CRC、baud、codebook、
  epoch、sample period 和 calibration generation。
- [x] 完成按 `*IDN?` 唯一地址编排的四主最小 HIL 闭环、JSON/CSV evidence、STOP/IDLE
  收尾和 persona 恢复；结果仍为单轮 diagnostic snapshot。
- `[~]` 增加 unit/HIL 故障注入：错位、反相、缺失/重复、低 margin、DMA overrun、capture
  truncation、掉线、ACK 缺失、commit miss、profile/topology 改变和 persona 恢复。
- [ ] 完成四主重复性和跨主一致性门禁；只有真实 PIO/DMA latch、质量和重复统计通过后，
  才清除对应 `TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY`。
- [x] 固化 host 重复统计工具：每主独立 STOP/ARM/coded/STOP，输出 reject 分类、lag
  histogram、min/max/mean/p99/stddev、margin 和 sequence 一致性；工具仍以 `*IDN?` 唯一
  地址为主键，COM 只作为临时端点。
- [x] 固化普通 TDMA TX PIO 频率/占空比静态门禁；修正 TX loop 从实际 7 cycles/bit
  收敛为声明的 6 cycles/bit（3 high + 3 low）。当前 250 MHz `clk_sys` 下，10/25/30 MHz
  的理论误差分别约为 `-0.031%/-0.078%/-0.125%`，占空比均为 `50%`；电气 rise/fall
  仍需示波器确认。
- [x] 将回环反射校准的 `tdma_pio_spi_clk_burst` 与
  `tdma_pio_spi_clk_forward` 纳入同一静态时序工具；burst 以 4-cycle period、2:2
  high/low 检查频率和占空比，forward 明确为上游 RX 边沿再生且本地 divider=1。报告为
  `build-product-release/tdma_pio_timing_check_reflection_20260821.json`；收发器和线缆
  rise/fall 仍须示波器确认。

## 六、P3 双向同时对比法

### P3 三线信号组

P3 按动态 PIO persona 分两次执行，不能在同一已装载 persona 内混用：

- `TDMA_PIO_SPI_P3_GROUP_CLK_DATA`：`forward=CLK`、`return=DATA`、`sync=CS`。
- `TDMA_PIO_SPI_P3_GROUP_CS_DATA`：`forward=CS`、`return=DATA`、`sync=CLK`。

同步线只打开捕获窗口，不进入 `t1/t2/t3/t4` 或 path-sum。两组均使用同一逻辑
四边沿掩码；每次请求结束后 core1 owner 停止 SM/DMA 并卸载该组 persona，下一组重新从
catalog 装载。SCPI 的第六参数为 `signal_group`，省略时兼容默认为 `CLK_DATA`。

当前推进策略：每条相邻 link 固定执行
`calibration_link_frequency_policy.REQUIRED_FREQUENCY_LADDER_MHZ` 完整验证阶梯。稳定档标为
`STABLE_REQUIRED`，必须同时满足 PIO burst/forward 的频率与 50% 占空比门禁、同 epoch 的
`t1..t4` hardware-latched 证据、residence/path-sum 质量和恢复门禁。
`LIMITED_RX_FREQUENCY_MHZ` 为 `LIMITED_RX` 有界诊断接收档，每次验证都必须实际执行，
即使低频稳定档失败也不得跳过；该档任一拒绝发布 `FALLBACK_25MHZ` 并按
`LIMITED_RX_FALLBACK_MHZ` 回退，但不单独使稳定档总判定失败。build
`20260821100236` 已完成四板逐段三档诊断 HIL；最新 DHRT100 build `20260822085100`
沿 accepted topology 完成四条链路、三档各 3 次（36/36 accepted）。30 MHz 仍按
`LIMITED_RX` 受限诊断档处理，当前保守稳定上限为 25 MHz。endpoint bias、topology/profile
freshness 和 active/staging gate 尚未完成，结果仍不能作为 active per-link calibration。

物理方向约束：同一 BiSS 段为 A.CLK_TX `GPIO25` -> B.CLK_RX `GPIO28`，同时
B.DATA_TX `GPIO29` -> A.DATA_RX `GPIO24`，SYNC 使用 `GPIO26/27` 关联 epoch。ISO1452
固定方向与 P3 的 `t1/t2/t3/t4` 双向方程一致，固件不得反转该方向。

第三阶段使用相邻板卡反向的 `CLK` 和 `DATA`，`SYNC` 只负责把同一 epoch/sequence 的
边沿关联起来。对链路 `A -> B`，定义：

```text
t1 = A.CLK_TX
t2 = B.CLK_RX
t3 = B.DATA_TX
t4 = A.DATA_RX
residence_B = t3 - t2
path_sum_AB = (t4 - t1) - residence_B
```

等长差分线缆支持对称性假设，但不替代收发器、GPIO synchronizer、PIO pipeline、连接器
和方向 endpoint bias 校准。若 asymmetry 超过门禁，只发布 path-sum，不伪造两个单向 delay。

- [x] P3-1：定义 `CLK/DATA/SYNC` 同 epoch marker、四边沿 capture origin 和方向字段。
- [x] P3-2：实现 `t1..t4` 关联、residence 扣除、path-sum、clock-rate error bound、
  不确定度和短窗口频率偏差处理。
- [ ] P3-3：完成同一 PIO persona 的板内 endpoint bias/reference loopback，并发布 bias
  generation、质量和失效原因。
- `[~]` P3-4：四板相邻段的 10/25/30 MHz 最小 HIL 已完成（最新复测 36/36 accepted）；继续补故障注入，覆盖缺边沿、乱序、重复、
  极性、SYNC/CRC 错、
  DMA overrun/stall、频率偏差和方向 asymmetry。
- `[~]` P3-5：四板逐链路三档重复 HIL 已完成（四链路、每档 3/3）；验证工具已强制每轮包含
  `LIMITED_RX_FREQUENCY_MHZ`
  `LIMITED_RX`，并与 10/25 MHz 稳定档分离评分。继续比较 per-link path-sum cumulative
  与整圈 edge RTT residual；为后续八节点扩展冻结 profile acceptance threshold。
- `[~]` P3-6：已加入路径快照 active/staging CRC、generation/freshness、硬件锁存、重复统计和
  asymmetry 门禁，并提供 Calibration snapshot -> VDC path-delay bridge；最新四板硬件复测仍为
  diagnostic-only，必须完成 endpoint bias/reference generation 和 freshness 后才能发布 active
  per-link delay。
- `[~]` P3-7：`CALibration:SAVE` 已接入 accepted bias snapshot -> Storage manager -> SD
  `/cal/accepted_<unique-id>_g<generation>.json` 原子文件；该文件是后续 Calibration NVS 的
  输入证据，不能被读取为 active calibration。正式 Flash NVS、candidate/active/previous
  ref 和重启负向验证仍待 HAOFV Flash M2-03。

### P3 单板回环预研

- [x] 复用产品板 TX/RX 三线回环，确认单板维护态可以稳定进入 `STOP -> LOCAL -> ARM ->
  TRAIN -> START`，并持续产生 TX/RX 物理层数据。
- [x] 新增 `calibration_bidirectional` 纯 C 计算/门禁模块和 Vivado/GCC host 单测，验证
  同板参考样本的 residence、raw path-sum、endpoint bias 扣除、delay estimate、坏顺序和
  缺证据拒绝。
- [x] 将单板回环结果接入 TDMA owner 持有的 `BOARD_TDMA_SPI_PIO`、既有 TX/RX SM 和
  `TDMA_PIO_SPI_RX_DMA_CHANNEL` edge-latch evidence；
  四边沿由 core1 收割，保留 `DIAGNOSTIC_ONLY`。
- [x] 增加维护态 `CALibration:LOOPback:*` SCPI 触发/只读 snapshot，发布
  reference-only loopback result、reject reason、latch resolution/flags；host 不传入实时边沿时间戳。
- [x] 完成 DHRT100 连续 10 epoch 的四边沿/SYNC/公式验证，并在维护 persona STOP 后恢复
  resident TDMA，确认 TX/RX 与物理错误计数门禁通过；结果仍为 diagnostic snapshot。
- [ ] 在同一 PIO persona 下完成 endpoint bias/reference loopback，才能把已完成的板间 P3
  diagnostic HIL 提升为 active candidate。

## 七、P4 VDC/DPLL 集成门禁

- [ ] 校准域只向 VDC 发布 accepted calibration snapshot，不直接写 DPLL 状态或 VDC time。
- [ ] VDC 消费 path-delay、residence、bias、generation、quality、freshness 和 topology
  CRC，建立 `local_tick_raw <-> vdc_time` 映射；DPLL 的 LOCKED/HOLDOVER/RELOCK 仍由 VDC
  owner 决策。
- [ ] 验证未 hardware-latched、`DIAGNOSTIC_ONLY`、stale、CRC 错、generation 不一致、
  topology 变化和 rejected sample 都会阻止 calibration 进入 active/VDC。
- [ ] 验证重新训练后必须显式提交/激活新 generation，旧 active calibration 在新证据未
  接受前继续可回滚使用或明确失效，不发生半更新。
- [ ] 增加 VDC/DPLL 双板和四板 observation window 验证，记录 offset/rate、lock、
  holdover、relock、late、CRC/sequence 和 calibration generation。

## 八、验证、长稳与发布

- `[~]` 双板/逐段 HIL：四条相邻 link 的 `t1..t4`、residence 和 path-sum 已通过；bias 和
  拒绝故障注入待完成。
- `[~]` 四板 HIL：最新 build `20260822085100` 的四条链路逐链路三档重复为 36/36 accepted；
  10/25 MHz 稳定档通过，30 MHz 按 `LIMITED_RX -> FALLBACK_25MHZ` 策略保留为受限诊断档。
  observed delay estimate 为 78..82 ns、单链路 jitter 为 0..2 ns，仍需 endpoint bias、
  逐链路累加、整圈 residual、拓扑 freshness 和长稳验证。
- [ ] 八节点扩展前：重复 profile/拓扑门禁，确认不把四板 aggregate 平均分摊成 link delay。
- [ ] 长时间验证：记录硬件 latch 计数、accepted/rejected、margin、overrun/stall、
  freshness、generation、VDC/DPLL 状态和 watchdog/fault evidence；结果写入任务记录并引用
  具体 build、拓扑、线缆和证据目录。
- [ ] 发布前：校准 CRC、版本 bundle、active/staging/rollback、SCPI 查询、SD/OTA 持久化
  和报告字段全部一致；训练失败统一 STOP 并恢复普通 PIO persona。

## 九、阻塞项与完成定义

当前关键阻塞项：

- `[!]` 统一相位代码路径已形成，但新固件尚未进行四板 MARK/SCK/DATA 零 offset 基线、矩阵
  动态加载与 residual repeat；HIL 通过前不能把 host 推荐矩阵提升为 active calibration。
- `[!]` P3 已有 4 ns PIO/DMA edge evidence 接口，但 endpoint bias、active/staging
  generation 和 topology/profile freshness 尚未补齐，不能进入正式 active calibration。
- `[!]` 第二阶段 marker wire layout、CRC、阈值和产品级训练 SCPI 尚未冻结。
- `[!]` endpoint bias/reference loopback、双向 asymmetry 和 topology freshness 尚未形成
  active calibration gate。

校准域完成定义：P2 的编码 RTT 能输出可追溯 accepted/rejected evidence；P3 的每条相邻
link 都能在同一 epoch 形成四时间戳、residence、path-sum 和质量统计；P4 的 VDC/DPLL 只
接受满足硬件 latch、非 diagnostic-only、generation/freshness 和重复性门禁的 active
calibration；双板、四板和后续八节点扩展验证均有可回溯任务记录。
