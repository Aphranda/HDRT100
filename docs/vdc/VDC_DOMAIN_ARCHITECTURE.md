# VDC 内部主域架构

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
Related: `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-08

本文是 HAOFV Virtual Distributed Clock（VDC）内部基础主域的稳定架构事实源。
VDC 负责多节点共同时间、offset/rate 估计、质量 promotion 和时间快照发布；不拥有
TDMA transport、PIO 资源、Calibration 测量、RefMem 事实同步或 Trigger 业务预约。

## 文档接口

| 文件 | 唯一职责 |
|---|---|
| 本文 | 稳定语义、owner、不变量、状态模型、跨域接口、失败恢复和验证映射。 |
| `VDC_DOMAIN_TODO.md` | 稳定 Task ID、依赖顺序、状态和进入/退出门禁。 |
| `VDC_TASK_PROGRESS.md` | checkpoint、验证、构建/HIL、失败、回退和证据位置。 |
| `docs/legacy/vdc/` | 重构前历史快照，仅用于审计和回退，不是当前事实源。 |

本文不记录单次 build、板端计数、临时调参结论或某一轮 HIL 的完成判断。

## HAOFV 分层与 owner

```text
System Pack / SCPI intent
        -> VdcSyncAO: profile / dictionary / calibration binding
        -> TDMA Foundation: resident process image / timestamp facts
        -> VdcSyncAO: evidence admission
        -> SyncDpllFB: phase + frequency servo / DCO commit
        -> VdcQualityGateFB: quality / promotion / holdover
        -> VdcVector: guarded snapshot
        -> RefMem / Trigger / core1: read-only consumers
```

| Owner | 拥有的事实 | 明确不拥有 |
|---|---|---|
| STATE_MACHINE | PIO/SM/DMA/FIFO/GPIO/IRQ persona 生命周期、claim/release、quiesce 和资源故障。 | DPLL 算法、RefMem commit、业务 payload。 |
| TDMA Foundation | 固定 process image、UP/DOWN resident cycle、sequence/CRC、slot/window、PIO/DMA hardware latch 和 completion evidence。 | offset/rate/lock、Calibration 结果解释。 |
| Calibration | directed link delay、bias、generation、freshness 和 observation path matrix。 | 实时 DPLL servo。 |
| `VdcSyncAO` | active schedule/profile/dictionary/calibration binding、evidence admission、同步动作事件。 | 直接写 PIO、改变 TDMA wire、修改业务 Vector。 |
| `SyncDpllFB` | `phase_offset_ns`、`period_adjust_ppb`、lock state、DCO snapshot 和 promotion 输入。 | 修改 raw timer、直接驱动 PIO、写 RefMem。 |
| `VdcQualityGateFB` / `VdcVector` | residual/jitter/dispersion/freshness、coarse/formal lock 结果和 guarded snapshot。 | 计算 transport 或覆盖 DPLL 输出。 |
| RefMem | 镜像共同时间、质量、fault 和 evidence。 | 计算 offset/rate。 |
| Trigger/core1/PIO | 读取稳定快照、反算 local deadline、执行 FIRE_LOAD。 | 写 VDC offset/rate/lock。 |
| SCPI/NO5/host | 配置 staging、发起动作、读取快照和离线分析。 | 写 lock、offset、rate、accepted count。 |

## 可配置 DPLL 控制角色

环形物理拓扑不决定 DPLL 的控制角色。每个节点保留相同的 PI 实现和完整的本地
evidence/quality 可观测性；运行时由持久化控制 profile 选择该节点是 `MASTER` 还是
`FOLLOWER`。因此任意槽位均可配置为主机，也允许多个主机与多个从机并存，用于后续
角色组合的可重复实验。

| 角色 | 本地 PI 能力 | 运行时 DCO 输入 | 不变量 |
|---|---|---|---|
| `MASTER` | 启用。通过正式 local evidence 驱动既有 FLL/PI/DCO 路径。 | 本地 `SyncDpllFB` 输出。 | 只接受已通过 admission 的本地 evidence；仍受既有 calibration、quality 和 promotion gate 约束。 |
| `FOLLOWER` | 保留但旁路。不得用 local evidence 更新积分器、rate、phase 或 local lock acquisition。 | 显式配置的主机槽位发布的 VDC DCO command。 | 只接受 source slot 与 profile 完全一致、sequence 更新且有效的命令；缺失、陈旧、错误来源或非法命令时保持上一稳定输出，绝不自动回退本地 PI。 |

控制角色切换必须在 DPLL service boundary 原子生效。切换时清空旧 PI 积分与连续锁定
历史，递增 control generation，并将 requested/applied generation 与拒绝/旁路计数发布
到快照和 SCPI。`FOLLOWER` 应用主机 command 时只镜像受验证的 rate、phase、lock 和
quality；它不取得主机的 formal-lock 权限，最终 promotion 仍须在本地满足现有质量和
安全门禁。

TDMA 与 Calibration 的职责不变：训练只测量 directed delay/bias，TDMA 只运输现有
process-image mailbox。为支持多个主机，RefMem 接收端必须按 source slot 保留已验收的
VDC command snapshot；禁止以“最后一个收到的 mailbox”作为从机输入。角色 profile 的
默认策略、Flash 兼容迁移、SCPI staging 和显式存储属于控制面，不改变 wire layout。

该模式将多节点本地 PI 的耦合实验与单节点闭环/从机跟随实验明确分开。角色配置本身不
承诺任何相位精度；验收仍以本地 evidence、DCO snapshot 完整性、quality promotion 和
实测残差为准。

### 本地晶振驯服

本地晶振驯服是独立于信号相位控制的可选底层通道。它以受验证的共同时间/主机命令和
本地 frequency measurement 形成慢速、限幅的 oscillator trim，降低本振长期自由漂移，
但不得直接写 DDS phase accumulator、替换 `FOLLOWER` 的主机 DCO command，或把
`FOLLOWER` 变回 local PI。

- `MASTER` 可将本地 PI/FLL 的可信 frequency 输出作为晶振驯服输入；其 DCO phase/rate
  仍由既有本地闭环 owner 产生。
- `FOLLOWER` 的信号 DCO 始终采用配置主机的 command。本地晶振驯服只调整底层 oscillator
  actuator；每次 trim 必须保持 local-to-VDC clock model 的时间连续性，不能制造未经主机
  command 的相位跳变。
- peer command 缺失、陈旧、来源错误、quality 不足、trim actuator fault 或超出限幅时，
  晶振驯服必须冻结最近可信 trim 并记录原因；它不得借此启用 follower local PI，也不得
  抬高 lock/quality/formal promotion。
- trim capability、requested/applied trim generation、command/source identity、限幅与
  fault/stale counters是 VDC snapshot/SCPI 的独立可观测事实。没有可用 actuator 的板卡
  可保持 role/DCO 行为，但必须明确报告 discipline unavailable，不能伪报已驯服。

## 稳定不变量

- `local_tick_raw` 是硬件观测事实，DPLL 不改写它；VDC 只维护从 local tick 到共同 VDC time 的映射。
- 只有同一 TDMA cycle/ring sequence、schedule CRC、frame/sample CRC、active path matrix 和正式 timestamp gate 全部通过的 evidence 才能进入 SyncDpllFB。
- 运行态只对 observation path matrix 做 O(1) 索引；缺失、过期、generation mismatch 或 CRC mismatch 必须 fail-closed。
- `VDC_DOMAIN_LOCK_LOCKED` 是环路状态，不等于产品目标锁定；正式运行必须是 `FORMAL_LOCKED` 且 health 为 `VDC_DOMAIN_HEALTH_HEALTHY`。
- core1 读取 DCO/clock snapshot 必须使用 seqlock、双缓冲或等价 guard；半新半旧、stale、late 或 generation mismatch 不得生成 FIRE_LOAD。
- 诊断 replay、software timestamp、单向 leg、NO5 外环观测和 TDMA up/down 成功不能单独提升为正式锁定。
- `FOLLOWER` 的 local evidence 仅可用于诊断和 gate 可观测性，不能成为 local PI 的隐式
  fallback；未验证的 peer command 同样不能驱动 DCO。
- oscillator trim 与 DCO signal phase owner 分离；任何 trim 更新都必须保持 clock-model
  连续性，且不能单独提高 DPLL lock 或产品质量等级。

## 两层状态机

### 实时资源/通信状态机

STATE_MACHINE 域定义硬件生命周期，VDC 只消费其已发布的 cycle evidence：

```text
STOPPED -> STAGED -> ARMED -> RESIDENT_INIT -> RUNNING
RUNNING -> CYCLE_BOUNDARY -> LOCAL_UNLOAD -> LOCAL_LOAD -> FORWARD -> CYCLE_BOUNDARY
RUNNING -- STOP / RESET / FAULT / RECONFIGURE --> STOPPED or STAGED
```

`RESIDENT_INIT` 只执行一次；物理 frame completion 不能结束 `RUNNING`。VDC evidence
只允许来自 `RUNNING` 中有效的 `CYCLE_BOUNDARY`/latch descriptor；`STOPPED`、persona
切换、diagnostic capture 和 resource fault 期间的记录只能用于诊断。

### VDC 锁相状态机

| 状态 | 进入条件 | 主要动作 | 允许输出 |
|---|---|---|---|
| `OFF` | VDC 未启用或 owner 未 ready。 | 不消费 DPLL evidence。 | 无共同时间。 |
| `CHECKING` | profile、TDMA、Calibration、dictionary、source 和资源快照检查。 | 清理旧连续性和 promotion history。 | readiness/checking。 |
| `INITIAL_SYNC` | 第一批正式 observation evidence 通过。 | 建立 phase anchor，可做受限初始 step。 | tracking candidate。 |
| `FREQ_LOCK` | 多周期 phase slope 可用。 | FLL-assisted acquisition，受限频率拉入。 | `COARSE_LOCKED` 候选。 |
| `PHASE_LOCK` | frequency error 进入 profile 范围。 | Type-II PI phase/frequency tracking、anti-windup、slew。 | `TRACKING_CANDIDATE`。 |
| `LOCKED` | 当前 profile 的连续环路状态完成。 | 继续低抖动跟踪。 | 只有 promotion 后才可 formal。 |
| `HOLDOVER` | reference/evidence 暂时 stale 且仍在 drift budget 内。 | 冻结可信 rate/phase，增长 dispersion。 | 受限 holdover，不自动 RUN。 |
| `RELOCKING` | 新 evidence 恢复或 quality 失效。 | 清理连续 fine history，重新 acquisition/tracking。 | 禁止 formal RUN。 |
| `FAULT` | CRC、资源、generation、sanity 或 drift budget 不可恢复。 | 停止正式输出并保留 fault evidence。 | 禁止 RUN/FIRE_LOAD。 |

状态机状态与质量等级正交：`COARSE_10US`/`DEBUG_1US` 只能形成粗锁或调试候选，
`FINE_100NS` 仍需经过连续窗口、freshness、jitter/dispersion、formal timestamp、
active calibration 和非 provisional path 的 promotion。

## TDMA 确定性观测契约

产品 RUN 使用固定 `CYCLIC_PROCESS_IMAGE`，不为 DPLL 插入独立同步帧：

```text
cycle[k]
  -> fixed process image / Node mailbox
  -> reference TX and local RX/TX hardware latch
  -> fixed DPLL observation trailer
  -> cycle[k+1] parser and evidence association
```

每个 observation evidence 至少包含：

- `cycle/ring sequence`、`schedule_crc32`、`frame_crc32`、`sample_crc32`；
- reference TX、local RX/TX、feedback RX 的 hardware timestamp；
- source/reference slot、window class、payload class、late/jitter；
- timestamp source/resolution/flags、dictionary/profile CRC；
- active path-delay entry、calibration generation、freshness 和 bias generation。

对于一条双向链路，T1/T2/T3/T4 可作为校准/诊断参考：

```text
forward = T2 - T1
reverse = T4 - T3
path_delay  ~= (forward + reverse) / 2
clock_offset ~= (forward - reverse) / 2
```

正式 ring path 不假设对称，方向 bias 和多 hop 结果只在 Calibration load 阶段写入
`VdcObservationPathMatrix`；SyncDpllFB 运行态只读索引，不沿物理环临时累加。

## DPLL 算法

### FLL-assisted acquisition

`INITIAL_SYNC/FREQ_LOCK` 使用跨多个 cycle 的 phase residual 斜率估计 frequency error：

```text
frequency_error ~= (phase_error[k] - phase_error[k-m])
                   / (T[k] - T[k-m])
```

估计必须经过窗口连续性、异常诊断、frequency sanity limit 和 slew limit；单个相邻
sample 不得直接把 DCO 推到极限。初始 phase 可受限 feed-forward/step，但不得改写 raw tick。

### Type-II PI tracking

`PHASE_LOCK` 维护 phase 和 frequency 两个状态。profile 的 `kp_q16/ki_q16`、update
period、step/slew/sanity limit 共同定义离散环路；`last_frequency_error_ppb` 是 FLL
斜率估计，`loop_filter_integrator_ppb` 是 Type-II PI 的累计频率校正，两者不能混成
一个瞬时 Ki 项。每个通过 admission 的 sample 执行：

```text
integrator[k+1] = integrator[k] + Ki * phase_residual[k] * update_period
rate[k]         = FLL[k] + integrator[k+1]
phase[k]        = phase[k] - Kp * phase_residual[k]
```

`integrator` 和最终 rate correction 都受 `sanity_freq_limit_ppb` 限制；当校正已经
饱和且下一步会继续向饱和方向积分时保持积分值，反向误差则允许 unwind。所有中间
乘法使用有界 64-bit 路径，因此 debug SCPI 可以保留负系数、零周期或极端无符号
限值作为实验输入；这不等于接受为产品参数，也不跳过 evidence/admission/final
promotion。质量统计使用 correction 前 input residual，避免同一帧 correction 后的数值
冒充 fine lock。

调试阶段通过 `SYSTem:SYNC:VDC:DPLL:TUNE` 或 `COEFficient` 写入
`kp_q16,ki_q16,update_period_us,step_threshold_ns,sanity_freq_limit_ppb`。SCPI 只写
Core0 单槽 mailbox；Core1 在 DPLL service boundary 交换完整 profile、递增
generation、清空旧 acquisition/integrator history，并以 `COEFficient?` 和
`FILTer?` 发布 active/requested/applied generation、profile CRC、FLL error、积分项、
rate correction 和 reject 计数。格式错误可拒绝，数值异常在 debug profile 不拒绝；
调参器必须按残差/频率/拒绝计数评分并回退，不得把 `LOCKED` 或较低 residual 自动
写成 `FORMAL_LOCKED`。

### Debug admission continuation

调试态可通过 `SYSTem:SYNC:VDC:DPLL:OVERRide 1` 将一条可恢复的 evidence gate
失败记录为 continuation，而不是把 TDMA 节点或 DPLL 调试流程硬停。SCPI 只写一个
不可覆盖的 Core0 intent；Core1 只在 DPLL service boundary 应用它。host 必须将 set
响应返回的 generation 与 `OVERRide?` 的 requested/applied generation 对账，只有
`ACTIVE` 且两者相等时才开始调试观测。

continuation 仅适用于 `PAYLOAD_NOT_DPLL_SAMPLE`、`TIMESTAMP_NOT_ELIGIBLE`、
`TIMESTAMP_RESOLUTION`、`WINDOW_BOUND` 和 `BAD_FRAME` 等可恢复 gate。原始 gate 的
code、slot 和 evidence sequence 保留在 debug snapshot；该样本不进入 PI/DCO，不增加
accepted 或 rejected sample count，且不改变 TDMA 的 UP/DOWN、process-image 或 FIFO
生命周期。bad argument/schedule、CRC/epoch、source/reference identity、window class 和
payload/window contract 等结构性错误仍严格拒绝。

debug continuation 不是产品 admission：它不能把 `LOCKED` 提升为 `FORMAL_LOCKED`，
并且 RefMem 不得发布 `REFMEM_VECTOR_FLAG_LOCKED`。因此调试读回中的 product-visible
reject 字段可保持 PASS 以支持有界采集和调参，但原始 debug gate 与 continuation count
必须随证据保存；产品 profile 禁用该路径并恢复严格拒绝。

DPLL 失锁、phase residual 超限或 DPLL phase 自身的 WCET/deadline 计数是调试反馈，
不是 TDMA 节点故障。只要 TDMA UP/DOWN、process-image、FIFO 和基础收发连续性仍然
正常，节点必须继续参与环路；DPLL 负载不得新增 `quarantined_mask`、停止 TDMA 或
屏蔽节点。调参器应读取这些失锁/时序反馈，按小步改变 PI 参数并等待新的连续样本，
让环路逐步收敛；只有 TDMA/硬件资源本身不可恢复时才进入节点级故障处理。

### Lock promotion

```text
FRAME_VALID
  -> TIMESTAMP_VALID
  -> COARSE_LOCKED
  -> TRACKING_CANDIDATE
  -> FORMAL_LOCKED
```

`FORMAL_LOCKED` 的必要条件是 fine quality、连续稳定窗口、RMS/peak/jitter、frequency
error、freshness、active calibration、sequence/CRC、formal timestamp gate 和非
provisional path 全部通过。`LOCKED` 不足以替代这一 promotion。

## Clock/DCO snapshot

`VdcClockModel` 至少表达 epoch/run、local/VDC anchor、nominal period、phase offset、
period adjustment、slew limit、schedule/profile CRC 和 model generation。
`VdcDcoControl` 是面向 core1/PIO 的只读派生 snapshot，必须带 `dco_update_seq`、
source model sequence、lock state 和相同 CRC。

提交规则：

- `SyncDpllFB` 是唯一 writer；每次 commit 都推进 update sequence。
- core1 只接受完整且稳定的 snapshot；失败时保留上一稳定值并上报 stale/late。
- snapshot 不改变 TDMA wire timing；它只决定本地事件如何映射到共同时间。

## HOLDOVER、RELOCK 与失败恢复

- 短暂失去 evidence：冻结可信 offset/rate，按 `holdover_drift_bound_ns_s` 增长 dispersion。
- path delay stale、source generation 变化、连续 sequence 丢失或 formal gate 失败：进入 `RELOCKING` 或 `FAULT`。
- source 切换必须清空旧连续性和 promotion history，从 `CHECKING/INITIAL_SYNC` 重新开始。
- 不得用旧 build、旧 receipt、host 时间或单向 leg 结果恢复正式 RUN。

## 跨域接口

| 输入/输出 | owner | VDC 规则 |
|---|---|---|
| PIO/SM/DMA persona lifecycle | STATE_MACHINE | 只消费 `RUNNING` cycle evidence；资源 fault 直接阻断 formal promotion。 |
| TDMA resident image/trailer | TDMA Foundation | 固定 wire/phase；VDC 不插帧、不调度 transport。 |
| active delay/matrix | Calibration | 只在 load/activation 生成；运行态只索引。 |
| timestamp dictionary | VdcSyncAO | 校验 event/source/resolution/payload，不能抬高 diagnostic flag。 |
| VDC snapshot | VdcVector | guarded read-only；RefMem/Trigger/core1 不能反写。 |
| T2/READY/FIRE_LOAD | Trigger/Measure | 绑定 map generation 和 quality；formal gate 失败时 fail-closed。 |
| SCPI | System/maintenance | 只写 staging/command slot 或读取 snapshot。 |

## 验证映射

| 验证层 | 必须证明 |
|---|---|
| Host/replay | FLL acquisition、PI tracking、rate drift、phase step、anti-windup 和 promotion 负测。 |
| TDMA short-frame | resident lifecycle、sequence/CRC、UP/DOWN、resource/persona 无冲突、hardware latch。 |
| Calibration | directed delay/bias、matrix completeness、generation/freshness 和失效重锁。 |
| VDC HIL | `COARSE_LOCKED` 与 `FORMAL_LOCKED` 分离，formal gate 失败不进入 RUN。 |
| NO5 | 只读外环 phase/evidence；不得参与控制或替代 ring evidence。 |
| Failure injection | stale、missing frame、bad CRC、dictionary mismatch、path mismatch、holdover aging、relock fail。 |
