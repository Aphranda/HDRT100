# 校准域待办

Status: Active
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`
Related: `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/calibration/CALIBRATION_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-08-20

本文档把 [`CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`](CALIBRATION_TDMA_CLK_TRAINING_PLAN.md)
拆成可执行的校准域任务。校准域拥有物理测量、residence、endpoint bias、path-delay、
统计质量、generation/freshness 和 active/staging 接受门禁；TDMA 只拥有训练 persona、
PIO/SM/DMA/core1 资源、窗口编排和 raw evidence transport；VDC/DPLL 只消费 accepted
calibration 并建立 `local_tick_raw <-> vdc_time` 映射。

状态标记：`[x]` 已完成，`[~]` 进行中，`[ ]` 待办，`[!]` 阻塞。本文档是任务分解，
不是新的跨域冻结契约；候选 wire layout、阈值和 SCPI 拼写仍以训练方案中的 candidate
状态为准，冻结时遵循文档登记流程。

## 一、当前总览

| 阶段 | 目标 | 当前状态 | 交付物 |
|---|---|---|---|
| P0 | 硬件 latch、证据 transport 和 owner 边界 | `[~]` | 可关联的 `t1..t4` hardware-latched evidence |
| P1 | CLK RTT 粗捕获收尾 | `[~]` | diagnostic bracket、过渡抖动和拒绝原因 |
| P2 | 编码 marker、过采样和相关测距 | `[~]` | accepted/rejected coded RTT snapshot |
| P3 | 双向同时对比、residence 和 per-link delay | `[ ]` | active/staging per-link calibration |
| P4 | VDC/DPLL 接入与长时间验证 | `[ ]` | calibration-to-VDC gate evidence |

当前不能把第一阶段的 CLK RTT bracket、软件 timer 或 diagnostic latch 直接用于 VDC/DPLL。
正式校准必须同时满足硬件 latch、质量门禁、重复统计、拓扑/profile freshness 和恢复流程。

## 二、P0 基础件与跨域边界

- `[~]` 将校准测量 owner 从现有 `calibration_manager` 状态壳升级为
  `CalibrationAO / CalibrationFB / CalibrationVector`，保留 guarded snapshot 和
  active/staging/rollback 语义。
- `[ ]` 冻结 `CalibrationEvidence` 字段：板卡唯一地址、logical slot、link key、
  `train_epoch`、`train_seq`、方向、topology/profile CRC、硬件时间源、分辨率、flags、
  DMA 状态、质量结果、generation 和 freshness。
- `[ ]` 定义 `t1..t4` 的 latch source、resolution、overrun、window miss、epoch/sequence
  关联和拒绝原因；CPU timer、DMA 完成时刻和帧解析时刻只能作为诊断证据。
- `[ ]` 与 SYNC_IO/TDMA 对齐 `CLK/DATA/SYNC` 三线 PIO persona、DMA buffer owner、
  core1 service 入口和 raw evidence 发布方式；core0/USB/日志不得进入边沿热路径。
- `[ ]` 定义 endpoint bias reference loopback 的 profile、board identity、bias generation
  和失效策略；bias 未生成时只允许发布 observed value。
- `[ ]` 定义 active/staging calibration 的 CRC、generation、topology freshness、
  rollback 和 VDC/DPLL 消费门禁。

## 三、P1 第一阶段 CLK RTT 粗捕获

- `[x]` 完成 SPI CLK 透明转发、master burst/capture、PIO IRQ 和 guarded snapshot 的
  最小实现。
- `[x]` 完成四板 HIL 的数量级捕获，并记录各 profile、master、mixed point、错误增量和
  `DIAGNOSTIC_ONLY` 状态。结果详见任务记录和训练方案；该 HIL 结果是 build/topology/
  wiring/profile 快照，不是通用精度事实源。
- `[ ]` 补齐第一阶段 rejected sample 分类：返回缺失、重复、超时、marker 不完整、
  overlap/mixed 和 topology/profile 变化。
- `[ ]` 将第一阶段 bracket 作为 P2 有界搜索输入，并禁止其单独生成运行态 feedback
  timeout、VDC delay 或 active per-link calibration。
- `[ ]` 固化四主轮换的唯一板卡地址、logical slot、STOP/ARM/STOP 收尾和恢复普通 persona
  检查；任一失败不得留下持续 CLK 环。

## 四、P2 编码 marker 与相关测距

- `[~]` 完成候选 codebook 的离线评估；当前评估工具已能比较 LFSR、NRZ/Manchester/
  differential-Manchester 和 raw-sample lag margin。
- `[ ]` 生成 C/Python golden vector，冻结 header、反码、CRC、bit order、codebook ID、
  epoch 字段和 wire waveform；冻结前不得登记为契约。
- `[ ]` 用现有 CLK HIL 扫描 candidate 半码元档位，确定 robust profile，并验证失败回退。
- `[ ]` 实现 coded TX PIO、固定 profile/FIFO 输入、quiet guard 和 checked capture size。
- `[ ]` 实现 master RX oversampling PIO + DMA；记录真实 `capture_origin`、TX/RX count、
  overrun、stall 和 buffer generation。
- `[ ]` 实现 core1 有界 raw-sample 相关器，输出 `peak`、`second_peak`、`margin`、
  Hamming distance、极性、accepted/rejected reason 和 lag histogram。
- `[ ]` 将 `CLOCK_COARSE -> CLOCK_CODED` 接入 TDMA owner 的非阻塞状态机；实现
  `TRAIN_PREPARE/ACK/commit`，先完成唯一地址工具编排，再完成 reference 单指令全环闭环。
- `[ ]` 扩展 guarded snapshot，绑定 build/topology/profile/schedule CRC、baud、codebook、
  epoch、sample period 和 calibration generation。
- `[ ]` 增加 unit/HIL 故障注入：错位、反相、缺失/重复、低 margin、DMA overrun、capture
  truncation、掉线、ACK 缺失、commit miss、profile/topology 改变和 persona 恢复。
- `[ ]` 完成四主重复性和跨主一致性门禁；只有真实 PIO/DMA latch、质量和重复统计通过后，
  才清除对应 `TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY`。

## 五、P3 双向同时对比法

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

- `[ ]` P3-1：定义 `CLK/DATA/SYNC` 同 epoch marker、四边沿 capture origin 和方向字段。
- `[ ]` P3-2：实现 `t1..t4` 关联、residence 扣除、path-sum、clock-rate error bound、
  不确定度和短窗口频率偏差处理。
- `[ ]` P3-3：完成同一 PIO persona 的板内 endpoint bias/reference loopback，并发布 bias
  generation、质量和失效原因。
- `[ ]` P3-4：完成双板 HIL 和故障注入，覆盖缺边沿、乱序、重复、极性、SYNC/CRC 错、
  DMA overrun/stall、频率偏差和方向 asymmetry。
- `[ ]` P3-5：完成四板逐链路 HIL，比较 per-link path-sum cumulative 与整圈 edge RTT
  residual；为后续八节点扩展冻结 profile acceptance threshold。
- `[ ]` P3-6：仅当四时间戳均 hardware-latched、bias generation、重复统计、拓扑 freshness
  和恢复门禁通过时，生成 active per-link delay 并交给 VDC/DPLL。

## 六、P4 VDC/DPLL 集成门禁

- `[ ]` 校准域只向 VDC 发布 accepted calibration snapshot，不直接写 DPLL 状态或 VDC time。
- `[ ]` VDC 消费 path-delay、residence、bias、generation、quality、freshness 和 topology
  CRC，建立 `local_tick_raw <-> vdc_time` 映射；DPLL 的 LOCKED/HOLDOVER/RELOCK 仍由 VDC
  owner 决策。
- `[ ]` 验证未 hardware-latched、`DIAGNOSTIC_ONLY`、stale、CRC 错、generation 不一致、
  topology 变化和 rejected sample 都会阻止 calibration 进入 active/VDC。
- `[ ]` 验证重新训练后必须显式提交/激活新 generation，旧 active calibration 在新证据未
  接受前继续可回滚使用或明确失效，不发生半更新。
- `[ ]` 增加 VDC/DPLL 双板和四板 observation window 验证，记录 offset/rate、lock、
  holdover、relock、late、CRC/sequence 和 calibration generation。

## 七、验证、长稳与发布

- `[ ]` 双板 HIL：验证每条 link 的 `t1..t4`、residence、path-sum、bias 和拒绝原因。
- `[ ]` 四板 HIL：验证轮换 master、逐链路累加、整圈 residual、拓扑 freshness 和恢复。
- `[ ]` 八节点扩展前：重复 profile/拓扑门禁，确认不把四板 aggregate 平均分摊成 link delay。
- `[ ]` 长时间验证：记录硬件 latch 计数、accepted/rejected、margin、overrun/stall、
  freshness、generation、VDC/DPLL 状态和 watchdog/fault evidence；结果写入任务记录并引用
  具体 build、拓扑、线缆和证据目录。
- `[ ]` 发布前：校准 CRC、版本 bundle、active/staging/rollback、SCPI 查询、SD/OTA 持久化
  和报告字段全部一致；训练失败统一 STOP 并恢复普通 PIO persona。

## 八、阻塞项与完成定义

当前关键阻塞项：

- `[!]` 正式 `t1..t4` 硬件 edge latch 及其 source/resolution 证据尚未成为第三阶段可用接口。
- `[!]` 第二阶段 marker wire layout、CRC、阈值和产品级训练 SCPI 尚未冻结。
- `[!]` endpoint bias/reference loopback、双向 asymmetry 和 topology freshness 尚未形成
  active calibration gate。

校准域完成定义：P2 的编码 RTT 能输出可追溯 accepted/rejected evidence；P3 的每条相邻
link 都能在同一 epoch 形成四时间戳、residence、path-sum 和质量统计；P4 的 VDC/DPLL 只
接受满足硬件 latch、非 diagnostic-only、generation/freshness 和重复性门禁的 active
calibration；双板、四板和后续八节点扩展验证均有可回溯任务记录。
