# 校准域待办

Status: Active
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`
Related: `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`, `docs/calibration/CALIBRATION_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-08-30

本文档把 [`CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`](CALIBRATION_TDMA_CLK_TRAINING_PLAN.md) 和
[`CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`](CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md)
拆成可执行的校准域任务。校准域拥有物理测量、residence、endpoint bias、path-delay、
统计质量、generation/freshness 和 active/staging 接受门禁；TDMA 只拥有训练 persona、
PIO/SM/DMA/core1 资源、窗口编排和 raw evidence transport；VDC/DPLL 只消费 accepted
calibration 并建立 `local_tick_raw <-> vdc_time` 映射。

### 文档接口

- 架构语义、物理模型和候选契约：只看两份训练方案与 `docs/check/DOCS_REGISTRY.md`。
- 里程碑、任务状态、进入条件和退出门禁：只看本文。
- 提交、构建、OTA、HIL、SD 原始波形、SVG、失败与回退证据：只看
  [`CALIBRATION_TASK_PROGRESS.md`](CALIBRATION_TASK_PROGRESS.md)；本文不复制单次实验快照。
- 本文不是新的跨域冻结契约；wire layout、阈值或 SCPI 拼写冻结时，必须遵循文档登记流程。

## 一、工作板规则与当前状态

### 1.1 状态规则

| 状态 | 含义 | 使用规则 |
|---|---|---|
| `DONE` | 已完成 | 实现、测试、实板闭环、失败恢复和证据索引均满足本项退出门禁 |
| `IN PROGRESS` | 进行中 | 已有可复核产物，但仍缺表中列出的一个或多个退出门禁 |
| `PENDING` | 待办 | 尚未开始，或只有方案而没有可复核产物 |
| `BLOCKED` | 阻塞 | 当前条件阻止继续；必须在“当前阻塞项”中写明阻塞条件和解除条件 |

“编译通过”“SCPI 有响应”或单次启动成功都不能单独作为 `DONE` 证据。状态变化同步更新
本文；单次 build、HIL 数值和证据目录只追加到任务进度文件。

### 1.2 里程碑总览

| 阶段 | 目标 | 状态 | 进入下一阶段的门禁 |
|---|---|---|---|
| P0T | 线序、邻接矩阵和环路顺序校准 | `IN PROGRESS` | accepted topology snapshot、node map、generation 和 freshness 可重放 |
| P0 | 硬件 latch、证据 transport 和 owner 边界 | `IN PROGRESS` | `t1..t4` evidence 可关联，active/staging/rollback owner 完整 |
| P1 | CLK RTT 粗捕获收尾 | `IN PROGRESS` | diagnostic bracket、拒绝分类和 P2 有界搜索输入完整 |
| P2 | 编码 marker、过采样和相关测距 | `IN PROGRESS` | coded RTT 重复性、跨 Node 一致性和故障矩阵通过 |
| P3 | 双向同时对比、residence 和 per-link delay | `BLOCKED` | 有效 endpoint bias、fresh path candidate、完整 link 集和 freshness gate 通过 |
| P4 | VDC/DPLL 接入与长时间验证 | `IN PROGRESS` | VDC 只消费 accepted active calibration，holdover/relock 和长稳通过 |

当前不能把第一阶段的 CLK RTT bracket、软件 timer 或 diagnostic latch 直接用于 VDC/DPLL。
正式校准必须同时满足硬件 latch、质量门禁、重复统计、拓扑/profile freshness 和恢复流程。

TDMA 基础负载前置已完成：固定短帧优先承载 VDC/DPLL 最小字段、critical RefMem、ACK/fence/
quality 和最小控制 token；静态余量不足时不加载校准、模型或触发测量等次优先内容。校准训练
仍只能消费 TDMA 发布的 transport/window/raw evidence，不得因基础负载通过而绕过硬件 latch、
generation 或 active calibration gate。对应构建、OTA 和 schedule 证据见
`docs/tdma/TDMA_TASK_PROGRESS.md` 的 `TDMA-PROGRESS-20260828-002`。

### 1.3 当前主线

```text
产品发布主线：
P0T topology + P0 evidence owner
  -> P1 coarse bracket -> P2 coded RTT -> P3 path/bias candidate
       -> TRN-01 MARK -> TRN-02 DATA/SCK -> TRN-03A/B short frame
            -> TRN-03C active candidate -> TRN-03D fault/soak -> P4-REL VDC/DPLL

验证阶段快车道：
TRN-03B short frame + frozen phase matrix + raw capture/replay
  -> P4-DBG 算法与接口调试
       -> P4-LIVE 真实 hardware-latched observation 调试
            -> 汇入 P4-REL 产品门禁
```

两条路径不改变最终产品门禁。`P4-DBG` 可以使用明确标为 `DIAGNOSTIC_ONLY` 的录制或实时样本，
用于快速开发状态机、sample admission、offset/rate 更新、参数扫描、曲线和证据工具；它不得发布
正式 `LOCKED/HEALTHY/RUN`。`P4-LIVE` 必须使用真实 PIO 边沿 latch，但在 endpoint bias、完整
active calibration 和 TRN-03D 产品安全闭环完成前仍只形成 provisional 调试结论。只有 `P4-REL`
需要等待产品发布主线的全部门禁。

### 1.4 训练子域的校准前置门禁

| 前置 | 当前状态 | 对 TRN-01/02/03 的约束 |
|---|---|---|
| accepted physical order | `IN PROGRESS`：host 四板环序和 NO 映射可重放，板内 topology transaction/freshness 仍待闭环 | 训练只能按 `*IDN?` 唯一地址和 accepted node order 执行 |
| P3 path candidate | `IN PROGRESS`：逐 link diagnostic `t1..t4` 和 path-sum 已有，endpoint bias 未有效 | 可用于 MARK/SCK/DATA 有界搜索，不得变成 active delay |
| phase matrix | `DONE`：MARK/SCK/DATA 全量 Node offset matrix 已经进入四板 TDMA 短帧闭环 | 仍必须连同 per-link base、generation 和 residual 重放，不能只加逻辑 offset |
| active lifecycle | `IN PROGRESS`：host/manager candidate、active、rollback 及 generation/link completeness 门禁已实现 | 受控 evidence 导入、真实 HIL、持久化与 VDC 消费前，所有训练仍为 staging/diagnostic evidence |

### 1.5 当前执行序

1. 切换为每板本地三线 reference loopback 接线，完成 endpoint bias generation。
2. 使用有效 bias 重跑 fresh P3 完整 link 集，并通过受控 evidence 导入形成 candidate。
3. 完成 TRN-03C 的实板 activate/rollback/persistence/VDC consumer 闭环。
4. 按 `TRN-03D-PHY -> RXGATE -> RETRY -> HEALTH -> FAULT -> SOAK` 完成原始误帧压降、
   确定性接收恢复、故障注入与长稳；原始链路未过门限前不得用重发结论替代物理收敛。
5. 与上述工作并行启动 `P4-DBG`；优先打通 trace replay、DPLL sample admission、offset/rate/lock
   曲线和参数扫描。`P4-LIVE` 只等待真实边沿 latch，不等待完整重试、watchdog、持久化和发布门禁。

## 二、训练子域：MARK / DATA / TDMA

训练子域按三个新阶段推进。旧校准 `P1/P2/P3` 只提供粗窗口、marker 基线和
`path_delay` candidate，不替代下面三个阶段的实际收发门禁。

| 阶段 | 阶段目标 | 主要输入 | 阶段输出 | 进入下一阶段 |
|---|---|---|---|---|
| `TRN-01` | 环路 marker 捕获与 core1/PIO cut-through | accepted topology、P2 codebook、epoch/sequence | 每跳 capture/forward tick、residence、整圈 marker RTT | 四节点同 epoch 捕获、顺序正确、每跳延迟有界、无 PIO/DMA fault |
| `TRN-02` | marker 锚定 DATA 码元时隙 | TRN-01 local origin、P3 `path_delay` candidate、PIO sample period | per-link `data_offset`、window、guard、skew、correlation/margin | 单跳和四条 directed link 重复通过，generation/profile/residence 一致 |
| `TRN-03` | TDMA 短帧/FIFO 闭环接入 | TRN-02 DATA windows、独立 SCK offset matrix、PIO instruction-cycle profile、loop-delay/residence、topology/profile CRC | staging/ARM、link/forward budget、短帧 TX/RX FIFO、sequence/CRC、active candidate | MARK、SCK、DATA 均 accepted，四板 up/down 和 FIFO 同时增长，且周期预算可重放；失败统一 STOPPED |

### 2.1 TRN-01：环路 MARK 捕获与切通

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| TRN-01A | 冻结 marker trial 的 epoch、sequence、marker_id、CRC、polarity、capture/forward tick 和 raw evidence 字段 | `DONE` | C snapshot、板端查询、host parser、字段数测试和 SD raw capture 已完成端到端一致性验证 |
| TRN-01B | 在 TDMA core1 owner 增加独立 marker PIO persona，支持 marker line 选择、固定 cut-through 和 DMA capture | `DONE` | PIO catalog/resource gate 和四板 HIL 通过；训练前显式 STOP，未与 cyclic TDMA 并发 |
| TRN-01C | 实现 Calibration intent 到 core1 的 bounded mailbox/prepare-ack，SCPI 不直接触碰 PIO/SM/DMA | `DONE` | ARM/INJECT 两阶段 mailbox、guarded snapshot、超时/拒绝矩阵、persona recovery 和 core1 owner 边界已覆盖 |
| TRN-01D | 完成 `NO.1 -> NO.2 -> NO.3 -> NO.4 -> NO.1` 环路 marker HIL | `DONE` | 零 offset 基线及一拍复核均为四节点 accepted；同 epoch/CRC、返回 marker、DMA/PIO fault 门禁通过，证据见任务记录 |

### 2.2 TRN-02：MARK 锚定 DATA 码元时隙

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| TRN-02A | 实现 DATA codeword 相关、极性、CRC、epoch/sequence、best/second peak 和 margin 判断 | `DONE` | DATA evaluator、marker-to-DATA PIO capture、板端 guarded snapshot、SCPI、SD raw capture 和正反用例回归已完成 |
| TRN-02B | 单跳 `NO.1 -> NO.2` 使用 P3 candidate 扫描 `marker -> DATA` 相对 offset，先粗后细收敛 | `DONE` | 固定阶梯各 profile 的四条 directed link 均完成多次 repeat；每档 DATA 与 forward residence 使用同一 identity，固化 profile gate 全绿 |
| TRN-02C | 将单跳结果形成 diagnostic training window，绑定 topology/profile/calibration generation 和 CRC | `DONE` | snapshot/SD capture 可读，topology/profile/schedule/calibration generation 已绑定，并保持 `DIAGNOSTIC_ONLY` |
| TRN-02D | 沿 accepted topology 完成四条 directed link 的 window 训练和固定 operating-profile 阶梯验证 | `DONE` | 固定阶梯全部完成完整 link 集、多次 repeat、跨度、identity、residence 和 fault-counter 门禁；证据见任务记录 |

### 2.3 统一相位训练路径

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| PHASE-TRN-BASE | MARK、SCK、DATA 共用 `link_base_delay = measured_link_delay / 2` 与 `base_samples + node_offset`，codebook half-chip 仅用于波形编码 | `DONE` | C 共用原语、host 共用计划 schema 和 MARK/SCK/DATA 专项回归通过；范围和容量引用 `CALIBRATION_TRAINING_PHASE_*` |
| PHASE-TRN-MATRIX | 三种信号共用零 offset 基线、SD raw capture、离线相关/SVG、全量 Node 笛卡尔矩阵、动态加载和 residual repeat gate | `DONE` | MARK/SCK/DATA 全量矩阵、独立 SCK 四板 HIL 和 DATA 矩阵已接入同 generation TRN-03B 闭环；失败 trial 与 SD/SVG 原始证据仍必须保留 |
| SCK-TRN-01 | 使用 SCK 自身 PIO 启动、已知 burst 和 raw capture 完成独立环路捕获，不引用 MARK offset 计算相位 | `DONE` | request/snapshot/SCPI/PIO/host 已移除 MARK phase 输入，SCK 使用自身 origin、per-link base 和 Node offset |
| SCK-TRN-02 | 沿 accepted topology 对每个 destination Node 执行独立 STOP/ARM/inject repeat，生成全量 SCK offset matrix | `DONE` | 四板零 offset 基线与推荐矩阵动态加载均完成独立 repeat；raw capture、逐 Node SVG、直方图、众数/中位数和 residual 门禁见任务记录 |
| SCK-TRN-GATE | 在 MARK 与 SCK 分别 accepted 后验证 `mark_sck_skew` | `PENDING` | skew 只进入产品 guard/window 验收，不回写任一物理 offset；generation/profile/topology/stale 与 rollback 门禁通过 |

### 2.4 TRN-03：TDMA 短帧/FIFO 闭环接入

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| TRN-03A | 增加 TDMA per-link staging 和 ARM gate，绑定实际 PIO persona 的周期预算以及 MARK/SCK/DATA 统一相位字段 | `DONE` | 完整矩阵写后读回、四板 ARM 以及缺 link、diagnostic-only、矩阵/预算过期拒绝与 STOPPED 回退均已复验；证据索引见 `CAL-TASK-20260826-010` |
| TRN-03B | 按 ring role 装载产品 flight persona 后启动 TDMA 短帧；先过 `raw-flight`，再过 `process-image` | `IN PROGRESS` | 既有四板短帧/FIFO 证据保留；诊断捕获新增 schedule-WCET 门禁后，最新复测仍发现捕获拍可能触发 calibration quarantine，且停止后的 config-seq 应用同步需先闭环；需重新完成四板 raw-flight/process-image、SD raw capture、逐 Node SVG 与 schedule 无扰证据后才能 DONE |
| TRN-03C | 汇总 per-link path-delay、residence、loop-delay、PIO 周期预算和 residual，形成 active candidate gate | `IN PROGRESS` | host candidate/lifecycle、manager candidate/active/rollback owner、CRC、calibration generation 和完整 link 集合门禁已完成；缺 endpoint bias、fresh P3、受控导入、四板激活/回滚/持久化/VDC HIL |
| TRN-03D | 故障注入、原始链路误帧压降、确定性接收恢复与长稳 | `IN PROGRESS` | DATA/MARK、DMA overrun 和 PIO stall 已有闭环；以下 TRN-03D 子项、Node dropout、stale identity、active generation 保护和长稳全部通过后退出；证据索引见 `CAL-TASK-20260827-011`、`CAL-TASK-20260827-014` |
| TRN-03D-PHY | 逐 Node/link 建立原始误帧基线并优先降低物理/采样层错误率 | `IN PROGRESS` | 固化周期采样；比较相邻 DATA offset、首帧启动、CS/SCK/DATA 相位和错误 bit 位置；每个候选均保存最差 Node 的 frame/bit error、SD 原始波形和 SVG，不能用重试掩盖未收敛链路 |
| TRN-03D-RXGATE | 建立 EtherCAT 风格的确定性逐帧接收门禁 | `PENDING` | CRC、sequence、schedule/profile/calibration generation、Node bitmap/WKC 和 deadline 全部通过才发布 process-image；坏帧绝不覆盖上一 accepted image，并显式发布 stale/quality reason |
| TRN-03D-RETRY | 建立调度预算约束下的恢复与重发策略 | `PENDING` | cyclic 全状态帧默认由下一周期自然刷新；只有剩余预算容纳完整 frame、guard 和 deadline 时才允许有界显式重发；重发次数、原 sequence/新 attempt identity 和失败原因可追溯 |
| TRN-03D-HEALTH | 建立可配置滑动窗口、连续错误和状态降级策略 | `PENDING` | 阈值随 operating profile/candidate staging 并受 generation/CRC 保护；状态按 valid/stale/degraded/stopped fail closed，恢复必须重新满足 ARM/identity gate，旧 active generation 不变 |
| TRN-03D-SOAK | 以最差 Node/link 完成长稳验收，不使用环路平均值稀释错误 | `IN PROGRESS` | 周期快照覆盖瞬时 down/error、恢复次数、counter 单调性、原始 FER/BER、WKC/bitmap、重发和 stale 消费；任一坏帧被消费、identity 错误、未解释 down/recovery 或错误率越 profile 门限即失败 |

#### TRN-03D-PHY：先降低原始链路错误

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| TRN-03D-PHY-01 | 固化逐 Node、有向 link 的原始接收质量基线 | `IN PROGRESS` | 每个接收机会分别记录完整帧、transport CRC/header/length 错误、未收到、重复、late 和启动期/稳态；同时保留分子、分母、原始 FER、零错误统计上界和最差 Node/link，禁止只报四 Node 平均值 |
| TRN-03D-PHY-02 | 使用全量 offset 矩阵扩大最差 Node 的 DATA 稳定窗口 | `IN PROGRESS` | 固定 build、topology/profile/generation、线缆和 MARK/SCK，只改变一个候选 DATA offset；相邻候选使用同等且足够大的接收机会复测，矩阵写后读回一致，失败候选也保留证据 |
| TRN-03D-PHY-03 | 对错误帧做相位与 bit 位置归因 | `PENDING` | SD 只采集整个 loop 原始流量；离线工具按 Node 输出 reference/capture/best-delay SVG、bit/byte 错误直方图、首帧与稳态对照，并联合检查 CS/SCK/DATA 相位、SCK 频率/占空比、driver/link path/receiver 边界 |
| TRN-03D-PHY-04 | 冻结每个 operating profile 的物理接收门限 | `PENDING` | 选择最小原始 FER 且窗口两侧有余量的矩阵；门限、最小样本量和统计置信规则进入 candidate staging 并绑定 generation/CRC，不在 host 工具硬编码；冷启动、重复 OTA 启动和长稳复测均通过后才允许进入恢复策略验收 |

#### TRN-03D-RXGATE：EtherCAT 风格的逐帧验收与镜像保持

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| TRN-03D-RXGATE-01 | 在 TDMA owner 内形成单一 frame verdict | `PENDING` | transport CRC、frame length、sequence、schedule/profile CRC、calibration generation、Node bitmap/WKC 和 deadline 逐项给出 reason；任一项失败即 rejected，Calibration 和 host 不旁路消费 raw frame |
| TRN-03D-RXGATE-02 | 原子发布 accepted process-image | `PENDING` | 仅完整 verdict 通过后以 sequence/generation 一次提交新镜像；失败、截断或晚到帧绝不部分覆盖，reader 不可观察到同一帧的新旧 Node 数据混合 |
| TRN-03D-RXGATE-03 | 保留上一 accepted 镜像并显式标 stale | `PENDING` | rejected/missing 周期保持上一镜像的数据和原 accepted identity，同时单独发布 stale age、当前失败 reason 和连续失败次数；消费者可区分“旧但完整”与“新且有效”，不得把保持值冒充本周期新数据 |
| TRN-03D-RXGATE-04 | 校验完整 Node 集与身份 | `PENDING` | expected Node bitmap、实际 bitmap、WKC、topology generation 和 frame identity 一致才接受；Node dropout、重复 Node、错环序、旧 generation 或旧 sequence 均 fail closed |

#### TRN-03D-RETRY：周期刷新优先、显式重发有界

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| TRN-03D-RETRY-01 | 以下一 TDMA 周期的全状态帧作为默认恢复 | `PENDING` | 单次 rejected/missing 后不消费坏帧、不阻塞 core1 飞行路径；上一镜像保持 stale，下一周期新帧通过完整 RX gate 后自然恢复 VALID |
| TRN-03D-RETRY-02 | 对显式重发执行可证明的周期预算门禁 | `PENDING` | 仅当 profile 明确允许且剩余窗口容纳完整 frame、guard、flight budget 和 deadline 时发起；次数上限、backoff 和预算来自 staged profile，预算不足直接跳过而不挤占下一确定性周期 |
| TRN-03D-RETRY-03 | 绑定 original sequence 与 attempt identity | `PENDING` | 每次 attempt 可追溯到原 sequence，接收端只提交首个通过 RX gate 的结果；晚到、重复或跨 generation attempt 被拒绝，不允许重复应用同一 process-image |
| TRN-03D-RETRY-04 | 重发耗尽后保持确定性失败语义 | `PENDING` | exhausted/late/budget-rejected 分别计数并进入 HEALTH；不得无界循环、静默改 sequence 或复用未验收 payload，active calibration generation 不因传输恢复而改变 |

#### TRN-03D-HEALTH：接收质量状态机与看门狗

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| TRN-03D-HEALTH-01 | 定义受 profile 管理的接收质量配置 | `PENDING` | 滑动窗口长度、原始 FER/丢帧/late 门限、连续失败门限、stale age、显式重发预算和恢复滞回随 candidate staging，配置完整性受 generation/CRC 保护 |
| TRN-03D-HEALTH-02 | 实现 `VALID -> STALE -> DEGRADED -> STOPPED` fail-closed 状态机 | `PENDING` | 单次错误进入 STALE；窗口或连续错误越限进入 DEGRADED；identity/WKC、超龄或持续越限进入 STOPPED；恢复转换和 reason 全部可单测，不因一次好帧无滞回抖回 VALID |
| TRN-03D-HEALTH-03 | 发布逐 Node/link 质量快照 | `PENDING` | good/bad/missing/duplicate/late、WKC/bitmap、retry、stale age、状态转换、最后错误和最差 Node/link 均为单调或 generation 有界事实；host 只汇总，不重算板端 verdict |
| TRN-03D-HEALTH-04 | 将 STOPPED 与 watchdog、ARM 恢复闭环 | `PENDING` | STOPPED 停止发布新 process-image 并保留故障证据；重启必须重新通过 topology/profile/calibration identity、矩阵 readback 和 ARM gate，watchdog reset 后可读取 reset reason，不能静默沿用失配 staging |

#### TRN-03D-FAULT / SOAK：故障矩阵与最终长稳

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| TRN-03D-FAULT-01 | 注入 transport bit flip、截断、坏 CRC、重复 sequence 和 late frame | `PENDING` | 每类故障命中唯一 RX reason；坏帧零消费，上一镜像 stale，下一周期刷新和可选有界重发均按 profile 执行 |
| TRN-03D-FAULT-02 | 注入 Node dropout、恢复和 bitmap/WKC 不完整 | `PENDING` | 任一缺失 Node 均不能被其他 Node 的正常数据掩盖；状态按门限降级/STOP，Node 回归后重新经过 identity/ARM gate，环序仍按 accepted NO 映射 |
| TRN-03D-FAULT-03 | 注入旧 topology/profile/calibration generation 和矩阵失配 | `PENDING` | stale identity 在消费前被拒，active calibration generation 保持不变，staging 不被隐式激活；恢复后读回的全量 MARK/SCK/DATA offset 矩阵与 candidate 一致 |
| TRN-03D-FAULT-04 | 注入重发预算不足、重发耗尽、DMA overrun、PIO stall 和 watchdog reset | `PENDING` | 每类故障有确定的 stale/degraded/stopped 转换、counter、SD/SCPI 证据和 persona 恢复；不存在无界重试、死循环或未解释的 down/recovery |
| TRN-03D-SOAK-01 | 在候选冻结前完成最差 Node/link 的原始误帧长稳 | `IN PROGRESS` | 使用 PHY-04 规定的样本量和统计规则报告 raw FER、丢帧率及置信上界；物理门限独立判定，关闭显式重发也必须通过，禁止用 accepted FER 代替 raw FER |
| TRN-03D-SOAK-02 | 在候选冻结后完成接收恢复长稳 | `PENDING` | 开启 RXGATE/HEALTH 和 profile 允许的恢复策略，周期检查坏帧零消费、stale age、状态转换、恢复、deadline、WKC/bitmap、counter 单调性和 active generation；最差 Node/link 任一门禁失败即整轮失败 |
| TRN-03D-SOAK-03 | 固化可复跑证据与发布判定 | `PENDING` | release build、四板异步 OTA、完整矩阵 readback、HIL timeline、SD raw capture、逐 Node SVG、JUnit 和 summary 均进入 `out/`；冷启动复跑一致且全部故障恢复后才允许 TRN-03D 标记 DONE |

EtherCAT 借鉴边界：本阶段借鉴逐帧 CRC/sequence/identity 验收、WKC/完整从站集合检查、坏帧不消费、
上一 process-image 保持、周期性新帧自然刷新和 watchdog/fail-closed。显式重发不是这里声称的
EtherCAT 原生 cyclic 恢复机制，而是本产品仅在 TDMA 预算可证明时允许的扩展；它不得参与
`TRN-03D-PHY` 的原始误帧达标判定。

实施顺序固定为：

```text
TRN-01A..D
  -> TRN-02A..D
  -> TRN-03A..D
```

在 `TRN-01D` 通过前不得开始 DATA 时隙收敛；在 `TRN-02D` 通过前不得 ARM 四板 TDMA；
在 `TRN-03C` 通过前不得向 VDC/DPLL 发布 active calibration。

## 三、P0T 线序与环路顺序校准

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| P0T-01 | 将有向线序、邻接矩阵、单闭环、anchor 旋转和 node map owner 迁入校准域 | `DONE` | TDMA 只提供隔离 probe persona、counter 和 raw physical evidence |
| P0T-02 | 固化 `calibration_ring_topology.py` 并统一 `calibration_*` host 工具命名 | `DONE` | 入口、schema 和回归使用统一校准命名 |
| P0T-03 | 按 `*IDN?` 唯一地址生成 directed adjacency | `DONE` | 仅 accepted 单闭环生成 `ring_order/node_map`；COM 号不进入 topology key |
| P0T-04 | 从 TDMA START 移除 NO 提交 | `DONE` | 只有 accepted topology 可写 NO，支持写后读回和重启复核 |
| P0T-05 | 实现板内 `CalibrationTopologySnapshot` | `PENDING` | active board set、相邻关系、方向、node map、CRC、generation、freshness、reason 和 raw index 完整 |
| P0T-06 | 固化重复 probe 和质量门禁 | `PENDING` | frame/word/edge、timeout、bad header、误触发、跨轮一致性和 profile identity 可判定 |
| P0T-07 | 完成拓扑故障注入 | `PENDING` | 开链、分叉、多闭环、反向、重复/缺失 Node、串扰和中途掉线覆盖至 `TDMA_RING_NODE_MAX` |
| P0T-08 | 将 topology/wiring/profile freshness 接入 staging 失效逻辑 | `PENDING` | 旧 active generation 仅按明确 holdover policy 使用，不静默继承 |

## 四、P0 基础件与跨域边界

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| P0-01 | 将 `calibration_manager` 状态壳升级为 `CalibrationAO / CalibrationFB / CalibrationVector` | `IN PROGRESS` | guarded snapshot 与 active/staging/rollback owner 边界完整 |
| P0-02 | 冻结 `CalibrationEvidence` 字段 | `PENDING` | identity、link key、epoch/sequence、方向、CRC、硬件时间源、DMA、质量、generation 和 freshness 完整 |
| P0-03 | 定义 `t1..t4` latch source、resolution、fault 和关联规则 | `IN PROGRESS` | P3 已有 PIO/DMA 同 epoch 边沿；仍需完整 fault reason 和 topology/profile generation |
| P0-04 | 与 SYNC_IO/TDMA 对齐三线 PIO persona、DMA buffer owner 和 core1 service | `DONE` | core0/USB/日志不进入边沿热路径，raw evidence 发布边界明确 |
| P0-05 | 定义 endpoint bias reference loopback profile 和生命周期 | `PENDING` | board identity、bias generation、质量与失效策略完整；无 bias 时只发布 observed value |
| P0-06 | 定义 active/staging calibration 生命周期与 VDC 消费门禁 | `IN PROGRESS` | path lifecycle 已实现；仍需 evidence package 导入、持久化和实板 consumer HIL |

## 五、P1 CLK RTT 粗捕获

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| P1-01 | 将 CLK RTT 流程、bracket 和质量解释收敛到校准域 | `DONE` | TDMA 仅保留 transport/persona/resource integration |
| P1-02 | 完成 CLK 透明转发、master burst/capture、PIO IRQ 和 guarded snapshot | `DONE` | 最小物理捕获链路可复核 |
| P1-03 | 完成多 Node、多 profile 的 diagnostic 数量级捕获 | `DONE` | build/topology/wiring/profile 身份完整，结果不冒充通用精度事实 |
| P1-04 | 固化默认 operating-profile 阶梯 | `DONE` | 默认阶梯只引用 profile 定义；实验档不进入默认训练 |
| P1-05 | 固化 master 轮换、STOP/ARM/STOP 和 persona recovery | `DONE` | 以唯一地址为主键，任一失败不遗留持续 CLK loop |
| P1-06 | 补齐 rejected sample 分类 | `PENDING` | 返回缺失、重复、超时、marker 不完整、overlap/mixed 和 identity 变化可区分 |
| P1-07 | 将 bracket 作为 P2 有界搜索输入 | `PENDING` | bracket 不单独生成 feedback timeout、VDC delay 或 active per-link calibration |

## 六、P2 编码 marker 与相关测距

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| P2-01 | 完成候选 codebook 离线评估 | `DONE` | LFSR、NRZ/Manchester/differential-Manchester 和 raw lag margin 可比较 |
| P2-02 | 生成 C/Python golden vector | `DONE` | header、反码、CRC、bit order、codebook ID、epoch 和 wire waveform 一致 |
| P2-03 | 扫描 candidate half-chip profile | `IN PROGRESS` | 重复统计完成前保持 `DIAGNOSTIC_ONLY`，默认档由 profile 选择 |
| P2-04 | 实现 coded TX PIO、固定 FIFO 输入和 checked capture size | `IN PROGRESS` | 仍需显式 quiet guard/profile gate |
| P2-05 | 实现 master RX oversampling PIO + DMA | `DONE` | capture origin、TX/RX count、overrun、stall 和 buffer generation 可查询 |
| P2-06 | 实现 core1 有界 raw-sample 相关器 | `IN PROGRESS` | peak、second peak、margin、distance、polarity、reason、histogram 和 repeat 统计完整 |
| P2-07 | 将 `CLOCK_COARSE -> CLOCK_CODED` 接入非阻塞 owner 状态机 | `IN PROGRESS` | `TRAIN_PREPARE/ACK/commit` 与 reference 单指令全 loop 闭环 |
| P2-08 | 扩展 guarded snapshot identity | `DONE` | build/topology/profile/schedule CRC、baud、codebook、epoch、sample period 和 generation 绑定 |
| P2-09 | 完成按唯一地址编排的多 Node diagnostic HIL | `DONE` | JSON/CSV evidence、STOP/IDLE 收尾和 persona recovery 通过 |
| P2-10 | 增加 unit/HIL 故障注入 | `IN PROGRESS` | 错位、反相、缺失/重复、低 margin、DMA/capture、掉线、ACK/commit、identity 和 recovery 覆盖 |
| P2-11 | 完成重复性和跨 Node 一致性门禁 | `PENDING` | 仅硬件 latch、质量和重复统计通过后清除 `TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY` |
| P2-12 | 固化 host 重复统计工具 | `DONE` | 输出 reject 分类、lag histogram、统计量、margin 和 sequence 一致性；唯一地址为主键 |
| P2-13 | 固化普通 TDMA TX PIO 频率/占空比静态门禁 | `DONE` | 声明周期与 PIO 指令一致；电气 rise/fall 仍由实测 gate 验证 |
| P2-14 | 将 CLK burst/forward 纳入同一静态时序工具 | `DONE` | burst 周期/占空比与 forward 再生语义可回归；电气边沿不由静态检查替代 |

## 七、P3 双向对比、bias 与 per-link delay

P3 的 signal group、方向、`t1..t4` 方程和 profile policy 以训练方案为唯一语义源。TODO 只跟踪
实现状态；同步线只关联同 epoch capture，不进入 path-sum，固件不得反转物理 TX/RX 方向。

### 7.1 板间 P3

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| P3-HIL-CODE-GATE | 将稳定 P3 物理回归固化为所有实现代码变更的提交前硬件验收 | `DONE` | 单一工具完成 release build、配置内全部 Node OTA、四条 directed link 的两组信号全频率重复矩阵、TDMA 隔离核验和源码指纹凭证；pre-commit 对无匹配凭证的 staged 实现 fail closed；正反测试、五板 OTA、四板 HIL 与提交推送全部通过 |

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| P3-01 | 定义三线同 epoch marker、四边沿 capture origin 和方向字段 | `DONE` | 字段与 P3 signal group 和 raw evidence 一致 |
| P3-02 | 实现 `t1..t4` 关联、residence 扣除、path-sum 和不确定度 | `DONE` | clock-rate error 和短窗口频率偏差纳入质量结果 |
| P3-03 | 完成同一 PIO persona 的 endpoint bias/reference loopback | `BLOCKED` | 当前 bench 缺每板本地三线 loopback；解除后必须产生有效 bias generation、质量与 reject reason |
| P3-04 | 完成完整 link 集的 profile HIL 与故障注入 | `IN PROGRESS` | 正向阶梯已有；仍需缺边沿、乱序、重复、极性、SYNC/CRC、DMA/stall、频偏和 asymmetry |
| P3-05 | 比较 per-link cumulative path-sum 与整圈 edge RTT residual | `IN PROGRESS` | required 与 limited profile 分离评分，并冻结面向 `TDMA_RING_NODE_MAX` 的 acceptance threshold |
| P3-06 | 完成 path snapshot active/staging gate | `IN PROGRESS` | CRC、generation/freshness、hardware latch、repeat、asymmetry 和 endpoint bias 全部满足后才 active |
| P3-07 | 完成 accepted calibration 持久化生命周期 | `IN PROGRESS` | SD evidence 不被读取为 active；仍需 Calibration NVS、candidate/active/previous ref 和重启负向验证 |

### 7.2 本地 reference loopback

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| P3-LB-01 | 复用产品板三线 loopback 进入维护 persona | `DONE` | `STOP -> LOCAL -> ARM -> TRAIN -> START` 可恢复，TX/RX 物理数据持续产生 |
| P3-LB-02 | 实现 `calibration_bidirectional` 纯 C 计算与门禁 | `DONE` | residence、raw path-sum、bias 扣除、delay estimate、坏顺序和缺证据正反回归通过 |
| P3-LB-03 | 将四边沿 hardware evidence 接入 core1 owner | `DONE` | PIO/DMA latch evidence 完整并保持 `DIAGNOSTIC_ONLY` |
| P3-LB-04 | 增加维护态 loopback SCPI 与只读 snapshot | `DONE` | host 不注入实时边沿；snapshot 发布 reference result、reason、resolution 和 flags |
| P3-LB-05 | 完成本地 loopback 重复公式与 persona recovery HIL | `DONE` | 四边沿/SYNC/公式、STOP 后 resident TDMA 恢复和物理 counter gate 通过 |
| P3-LB-06 | 在同一 PIO persona 下形成有效 endpoint bias generation | `PENDING` | bias identity、quality、freshness 和 failure reason 完整，板间 P3 才可提升为 active candidate |

## 八、P4 VDC/DPLL 集成门禁

### 8.1 验证阶段快车道

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| P4-DBG-01 | 建立 `DIAGNOSTIC_ONLY` 的 DPLL/VDC 调试模式 | `IN PROGRESS` | 可以消费有完整 sample identity、sequence、source、resolution、CRC 和 reject reason 的录制/实时样本；缺失或坏样本只能 reject，不能补零；输出始终带 debug/provisional 身份且不能进入正式 RUN gate |
| P4-DBG-02 | 固化 SD/TDMA 原始证据到 DPLL 的 replay 调参路径 | `PENDING` | 同一 evidence 可确定性重放；参数变化输出 accepted/rejected、lock time、offset RMS/peak、rate、outlier 和 saturation，host 只配置与记录，不参与实时计算 |
| P4-DBG-03 | 在现有四板短帧环路上联调 VDC owner、TDMA payload 和 DCO snapshot | `PENDING` | TDMA sequence/CRC/process-image 持续推进且无 DMA/PIO overrun 时可联调接口；原始 FER、missing 和 reject 单独记账，算法结果不得掩盖物理错误或升级 calibration 身份 |
| P4-LIVE-01 | 接入连续真实 PIO 边沿 latch observation | `PENDING` | timestamp 来自硬件 latch 而非 core1 drain/host 时间；source、resolution、flags、window、frame identity 和 path-delay 输入可回溯，允许开始真实 jitter/servo 调参，但结论保持 provisional |
| P4-LIVE-02 | 完成多 Node 真实输入的粗锁、频锁和相锁调试 | `PENDING` | 分别验证 initial sync、frequency pull、phase pull、outlier reject、holdover/relock；任何 sample identity、CRC、sequence 或 freshness 失败都不能更新 offset/rate |

验证阶段只保留会污染算法结论的硬门槛：样本身份可追溯、坏样本不进入 DPLL、真实调参使用
hardware latch、offset/rate 只有 VDC owner 写、调试输出不能进入产品 RUN。完整 RX health 状态机、
显式重发、watchdog、持久化、发布级阈值和全部故障矩阵安排在 `P4-REL` 前补齐，不阻塞
`P4-DBG`；其中 CRC/sequence/identity 的最小拒绝仍应尽早复用现有 TDMA gate。

### 8.2 产品发布门禁

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| P4-01 | 校准域只向 VDC 发布 accepted calibration snapshot | `IN PROGRESS` | manager publish gate 已接入；仍需真实 active HIL，校准域不直接写 DPLL/VDC time |
| P4-02 | VDC 消费 path-delay、residence、bias、identity、quality 和 freshness | `IN PROGRESS` | bridge 已有；仍需 consumer readback、拒绝矩阵和 owner 状态 HIL |
| P4-03 | 验证非法 calibration 一律不能 active/VDC | `PENDING` | 非 hardware latch、diagnostic、stale、CRC/generation/topology 错和 rejected sample 全部 fail closed |
| P4-04 | 验证 retraining、显式 activation 和 rollback | `PENDING` | 新 generation 不半更新；旧 active 仅按明确 rollback/invalid policy 使用 |
| P4-05 | 完成 VDC/DPLL observation window HIL | `PENDING` | 多 Node offset/rate、lock、holdover、relock、late、CRC/sequence 和 generation 可回溯 |
| P4-REL-01 | 将调试路径收敛到产品接收与恢复门禁 | `PENDING` | TRN-03D-PHY/RXGATE/RETRY/HEALTH/FAULT/SOAK、active calibration、watchdog、持久化和发布阈值全部通过；debug/provisional 结果不能被原地提升为正式 active/LOCKED，必须基于合格新观测重新验收 |

## 九、验证、故障与发布

| ID | 任务 | 状态 | 完成/退出门禁 |
|---|---|---|---|
| VAL-01 | 完成完整 directed link 集的 P3 HIL | `IN PROGRESS` | `t1..t4`、residence、path-sum、endpoint bias 和拒绝故障注入全部通过 |
| VAL-02 | 完成多 Node cumulative residual 验证 | `IN PROGRESS` | 不把 aggregate 平均分摊成 link delay；profile/fallback 评分按 policy 执行 |
| VAL-03 | 扩展到 `TDMA_RING_NODE_MAX` 前复核 profile/topology gate | `PENDING` | Node 数变化不破坏 identity、矩阵容量、freshness 和完整 link 集门禁 |
| VAL-04 | 完成长时间验证 | `IN PROGRESS` | 按 `TRN-03D-PHY/RXGATE/RETRY/HEALTH/SOAK` 覆盖最差 Node/link 原始错误率、坏帧不消费、stale/重发/降级/STOP、latch、DMA/stall、freshness、generation、VDC 和 watchdog/fault |
| VAL-05 | 完成发布门禁 | `PENDING` | CRC、version bundle、active/staging/rollback、SCPI、SD/OTA 持久化和报告字段一致；失败统一 STOP 并恢复 persona |

## 十、当前阻塞项与统一完成定义

### 10.1 当前阻塞项

| ID | 阻塞范围 | 阻塞条件 | 解除条件 |
|---|---|---|---|
| BLK-01 | P3-03 / P3-LB-06 | 当前四板环线不是每板本地三线 reference loopback，bias capture 缺有效 RX 边沿 | 每板完成本地 TX->RX 三线连接并重跑 endpoint-bias，形成 accepted bias generation |
| BLK-02 | TRN-03C / P4-REL | fresh P3 依赖 BLK-01，当前不能形成可激活的完整 candidate；不阻塞 P4-DBG，接入真实 latch 后也不阻塞 provisional P4-LIVE | 使用有效 bias 重跑完整 link 集，经受控导入通过 activate/rollback/persistence/VDC HIL |
| BLK-03 | P2 产品化 | marker wire layout、CRC、阈值和产品级训练 SCPI 尚未冻结 | 完成正反回归、跨域评审与契约登记 |
| BLK-04 | active calibration | endpoint bias、asymmetry 和 topology freshness 尚未共同进入 active gate | candidate 对全部 identity、quality、freshness 和完整 link 集 fail closed |

### 10.2 统一完成定义

校准域只有同时满足以下条件才可标记整体完成：

1. P2 能输出可追溯的 accepted/rejected coded RTT evidence，并通过重复性与故障门禁。
2. P3 的每条相邻 link 都在同一 epoch 形成 hardware-latched `t1..t4`、residence、path-sum、
   endpoint bias 和质量统计。
3. TRN-03 的短帧/FIFO、active candidate、激活/回滚、故障矩阵和长稳全部闭环。
4. VDC/DPLL 只接受满足 hardware latch、非 diagnostic、generation/freshness、完整 link 集和
   重复性门禁的 active calibration。
5. 当前拓扑和扩展到 `TDMA_RING_NODE_MAX` 的验证均有可回溯任务记录；失败统一 STOPPED 并
   恢复普通 PIO persona。
