# VDC 内部主域架构

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
Related: `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-17

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

公共本地时钟的拍数读取与纳秒派生由 `vdc_timestamp_clock_read_ticks64()` 和
`vdc_timestamp_clock_ticks_to_ns()` 提供。初始化保存实际 tick frequency 与向上取整的
resolution；只有二者乘积精确等于一秒纳秒数时，resolution 才能作为整数换算因子，
其余频率仍按商余数计算向下取整结果。该实现保留原无符号结果回绕语义，不重新
初始化 timer、调整 epoch 或改变 `local_tick_raw`；Core1 schedule 的边界与 WCET
继续使用原始拍数。换算加速不能提升 latch 精度、evidence 资格或逐圈保全声明。

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

### 共同时间相位域

`common_effective_time_ns` 是由 TDMA sequence 和 origin TX phase 导出的共同逻辑时间，
它不能把另一块板的 raw RX hardware-latch 自动变成共同时间。每个 evidence 必须明确
`correlation_flags` 中的相位域：origin 与 local latch 同一硬件计数器，或 local latch 已由
冻结的、generation-bound mapping 映射到共同时间。二者都不成立时，local phase 只能是
`LOCAL_PHASE_RAW` 诊断事实。

- MASTER 不能把跨板 raw local phase 输入 PI/DCO；`VDC_DOMAIN_GATE_LOCAL_PHASE_UNALIGNED`
  必须拒绝该输入并保持重获取路径可见。
- FOLLOWER 使用完全相同的 timestamp、directed path 和 residual 公式，但只记录 raw
  observation、gate 和 provenance；它不改变 PI、DCO 或 local lock promotion。
- 报告必须分别给出 absolute bias、raw spread 与可信 jitter。只有 path/bias generation、
  direction、continuity 和 local-to-common phase mapping 全部有效时，jitter 才是可信输出
  jitter；否则该字段为空，不能由 raw spread 或 `LOCKED` 填充。
- `local_tx_edge` 只有在 sequence、identity 和 capture generation 与接收 frame 绑定，且
  已声明其到共同时间的 mapping 时，才能作为 output observation。未绑定边沿不得跨记录
  补边或与 NO5 外部边沿配对。

对于一条双向链路，T1/T2/T3/T4 可作为校准/诊断参考：

```text
forward = T2 - T1
reverse = T4 - T3
path_delay  ~= (forward + reverse) / 2
clock_offset ~= (forward - reverse) / 2
```

正式 ring path 不假设对称，方向 bias 和多 hop 结果只在 Calibration load 阶段写入
`VdcObservationPathMatrix`；SyncDpllFB 运行态只读索引，不沿物理环临时累加。

## DPLL 调度准入

`SyncDpllFB` 由完整 Core1 静态表中的 DPLL phase 推进，窗口与 WCET 分别引用
`PROJECT_CORE1_PHASE_DPLL_START_CYCLE`、`PROJECT_CORE1_PHASE_DPLL_END_CYCLE` 和
`PROJECT_CORE1_PHASE_DPLL_WCET_CYCLES`。固定入口余量由
`PROJECT_CORE1_DPLL_ENTRY_MARGIN_CYCLES` 声明，覆盖调度轮询与入口抖动；它不增加
service WCET，也不随 DPLL 启停临时申请。全部已编译周期目录保留该余量，后续相位
按完整表安装，GUARD 始终不执行负载。

相位窗口恰好等于 WCET 时，`app_realtime_run_phase()` 的完整 WCET 容纳检查会使
稍晚到达的调用整拍跳过。该检查继续保留；修复静态表必须同时验证 start miss、
实际执行、overrun、deadline miss 和 TDMA 连续性。相位被调用不代表新 evidence 已
准入，更不代表 FOLLOWER 已应用主机命令或达到正式锁定。当前切片记录见
`VDC-PROGRESS-20260914-001`，完整表边界见 TDMA 的 `TDMA-DET-01`。

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

信号输出的本地时间投影由 `vdc_domain_dco_local_to_output_ns()` 统一计算：消费同一
份不可变 `VdcDcoControl` 及与其本地锚同域的时间，包含锚、phase 和 rate。现有
phase-only 诊断脉冲 deadline 与输出域残差接口共用该投影，避免输出已经采用 `dco`
而反馈仍套用另一个 `clock`
模型。投影必须有界；模型/时间无效或最终结果越界时返回失败，不改写调用者结果。

`vdc_domain_dco_output_phase_residual_ns()` 比较本地实际 RX 事件处的 DCO 输出相位，
与指定参考同事件的输出相位及有向路径时延。调用者负责验证来源、事件对应、时间单位、
模型代际和新鲜度；raw reference counter phase 不能隐式当作 reference DCO phase。
残差正负号是归中后的本地模型相位减期望相位，不单独授予物理引脚提前/滞后结论。
这两个纯计算接口不写 PI、DCO、lock 或 quality，也不建立共同时间映射或授予正式准入。

## HOLDOVER、RELOCK 与失败恢复

- 单份时间样本的 CRC、来源、时间有效性或代际不匹配只拒绝该样本；在当前控制
  配置仍有效时，保持积分、有效时间锚和可信 DCO 输出，不因这一帧直接清锁或
  重新捕获。下一有效样本继续校正；诊断拒绝计数与有效样本历史分开，最后有效
  时间不被坏样本刷新，quality freshness 和正式准入继续独立判定。
- 短暂失去 evidence：冻结可信 offset/rate，按 `holdover_drift_bound_ns_s` 增长 dispersion。
- 当前 path delay stale、实际 source generation/控制时间基准变化、持续丢失超出
  保持边界或正式质量失效按恢复策略处理；不能把收到一份旧代际报文解释为当前
  时间基准已经变化，也不能仅按单帧拒绝计数触发重锁。
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

### VDC-BOUNDARY-01：本地服务边界频率增量命令

本条款独立于旧共同绝对时间命令；它定义内部 DCO 的受限频率应用，不授予 formal
timestamp、LOCKED、物理 GPIO 连续性或正式 RUN 资格。实现及硬件退出状态见 TODO，
登记为 pending 不表示已经完成实际应用。以下数字是本条款登记的 wire ABI；实现
比对入口为 `refmem_sync_vdc_feedback.h` 与 `tdma_process_image_layout.h`。

TDMA class 为 `0x13`，schema 为 `3`，flags 为 `REFMEM_VDC_BOUNDARY_COMMAND_FLAGS`
（单次 probe）或 `REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS`（显式 AUTO）；二者均仅
申请 rate delta，在下一次合格 Core1 service boundary 应用。完整记录为 64 B，使用现有 VDC 区的 16 个 4 B
片段；不改变 Node mailbox 长度、其他业务区或外层广播 target mask。小端编码如下：

| 字节偏移 | 长度 | 字段 |
|---|---|---|
| 0 | 4 | schema、source_slot、target_slot、flags，各一字节 |
| 4 | 4 | control_session |
| 8 | 4 | command_seq |
| 12 | 4 | schedule_crc32 |
| 16 | 4 | target_clock_epoch_id |
| 20 | 4 | target_clock_run_id |
| 24 | 8 | target_arm_epoch |
| 32 | 4 | target_observer_epoch |
| 36 | 4 | basis_measurement_sequence |
| 40 | 4 | expected_target_model_token |
| 44 | 4 | expected_applied_command_seq |
| 48 | 4 | signed_delta_rate_ppb，二进制补码 |
| 52 | 8 | basis_source_output_ns_lo |
| 60 | 4 | 前 60 B 的 IEEE reflected CRC32，polynomial `0xEDB88320`，初值及末异或 `0xFFFFFFFF` |

本地 decoded struct 与 wire 布局分离：两个 uint64 字段前置、十个四字节字段、
一个清零且不序列化的 reserved word、四个单字节头字段；大小保持 64 B。Core0
显式编码/解码，禁止把结构体 memcpy 当作 wire。旧反馈 schema 与旧绝对命令继续
使用自己的解码器，新类型不得借旧字段改变语义。

- Core0/RefMem 唯一拥有准备、CRC、分片/组装及稳定副本。组内数据不可改写，只有
  实际 FIFO 发布成功才推进片号；组完成或取消后先成功发布普通 mailbox。重复片
  不续时，冲突/乱序/超时取消当前部分组；空 RX 队列也执行有界到期处理。复用接收
  槽时显式校验记录类型，不能把命令作为反馈或普通 phase/rate 解读。
  完整命令在最终绑定快照暂不可得时保留在 Core0 原组装缓冲，后续 service 有界
  重验；它尚未获得 Core1 应用资格。保存原接收 admission 与真实完成时刻，后续
  mailbox 不得覆盖待交接字节；已知换代、STOP/重新准入或原组装期限到期则退休。
  延后提交不得刷新测量依据、完成时刻或命令有效期；Core1 仍执行全部身份及年龄校验。
  Core0 在获取 RX FIFO 租约前分别刷新普通 VDC 和反馈绑定；任一绑定暂不可得时，
  本拍不 acquire/release RX，避免未准入的中间分片被静默消费。已知 inactive 与
  unknown 分开处理：前者正常退休相关状态并允许普通接收；已知 STOP 不等待无关
  VDC 快照。到期处理仍运行，TX 发布保留原有准入规则。此机制复用有界 FIFO，
  同一 view 中的普通数据也会延后；持续暂忙可能耗尽既有容量，不承诺无损运输。
- 同一 offer 允许有界重复完整组，总组数上限由
  `REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS` 定义，包含首次发送。重复使用完全相同的
  命令字节、序号、测量依据和原有效期，每片重新验证当前 owner 与原模型年龄；
  不续 TTL、不生成新校正、不恢复诊断额度。组间必须成功发布普通 mailbox，
  FIFO 发布失败或快照争用不推进片号及组预算。取消、身份变化或过期在组间空档
  也退休该 offer；相同 ID 不得恢复或换字节，只有新的单调 ID 可获得新发送预算。
- Core1/SyncDpllFB 唯一拥有逐从控制状态、命令选择与实际应用。一个有界 service
  最多检查一个从板或应用一条本地命令；不进行 wire 解析、CRC、存储或等待。出站
  offer 有单调不复用身份，Core0 返回的 TX-done 只证明该 offer 的整个有限发送
  批次已完成 FIFO 发布；逐组发布数量由 Core0 运输计数记录。精确 ACK 提前到达时
  Core1 退休匹配 offer，保留原命令及已用额度，允许后继从板推进；此时不伪造
  TX-done，Core1 批次完成计数可为零。过期 TX-done 不能完成新 offer，更不能
  充当 DCO 应用 ACK。Core0 观察到退休后停止新增发布，已进入 FIFO 或正在完成
  单次发布的片段仍可能在途；从板重复检测和应用序号保证至多应用一次。
- 来源与目标必须属于当前准入拓扑且不同；从板仅接受指定主机。命令必须绑定当前
  session、schedule、目标自身 clock epoch/run、ARM、observer、DCO model token
  及 expected applied sequence；序号非零、严格递增、不静默回绕。各板本地 epoch
  不相互比较为公共代际。外层 Core1 guard 内复验上述身份及取消，随后调用
  `vdc_domain_apply_follower_rate_delta()`；成功才发布新模型与 applied sequence。
- 作为命令依据的观测必须精确关联测量序号、源生命周期、源模型及 NO1 同序参考。
  不能把历史 MATCH 的 active 当作新测量，不能用最新 RX 元数据修补旧 pair。
  主机选择和发布前须在当前同 reference token 的 NO1 对应观测坐标重新计算
  pair 最新参考 lower bound 的年龄；保留的 last_age_ticks 不提供当前新鲜度。
  probe 使用绝对输出坐标，AUTO 使用 raw-now 按当前 DCO rate 缩放的 RATE 坐标，
  两种坐标不能相减；从板命令年龄仍使用回送的绝对输出 lower bound。
  首期同模型端点必须分别同 token；跨 token 区间只有具备额外连续性证明才可用于
  自动频差控制。模型 guard 为奇数时，Core1 writer 仅使用自己的最后发布副本并
  比对当前 Domain，不能调用必然拒绝的公共 reader 或把旧 token 贴给已改变的 DCO。
- 有效期在目标当前同一 DCO 模型的输出坐标检查：按当前本地时间上界计算输出，
  与回送的源观测 lower bound 作差，拒绝未来或超龄依据；不以接收时刻刷新 TTL，
  不要求跨板绝对时间映射。Core1 接纳时还须确认当前模型尚未被本拍其他服务改变。
  TTL、单次 step 限额和 ACK 等待是命名配置，必须按实际周期/片间交付验证，不能
  将一次配置的实测能力外推到其他周期。
- 每从至多一个未决命令；到期进入 EXPIRED_UNRESOLVED 并保留完整身份，停止重发。
  后续精确 ACK 必须匹配 session、目标生命周期、command_seq，并来自更新的模型
  和测量；较大的无关应用序号不是 ACK。未决不能被默认为“未应用”而再叠加校正，
  其他从板仍可推进。下一次自动校正还需新的应用后观测窗口。
  单次诊断命令序号取精确匹配反馈的 applied sequence 加一；耗尽拒绝，不从一重新
  起算。迟到精确 ACK 仅登记 late-applied，不在同一 probe 内恢复已用额度。
- STOP 请求接受与 owner 已停止分开判定。确认 disabled、adapter 停止及配置已应用
  后才允许配置；运行中 STOP/角色/会话/配置换代立即撤销应用授权，跨核复制前后
  复验。重 ARM 的旧字节、旧 offer、迟到准备不能恢复授权；失败保持可信输出。
- 诊断单次校正与自动闭环分开启用。前者只能在 STOP 后显式配置，每个从板按新鲜
  观测绑定一次受限测试增量，用于证明应用/回传，不把区间跨零说成校正方向已确定。
  四板分别授权同一测试增量，从板应用还须与自己的授权值相同；非零 feedback
  session 本身不是应用许可。主机在 offer 时消耗该从板额度，重复反馈、ACK、到期
  和重 ARM 都不生成新命令或补充额度；同一 offer 内仅允许上述有界重复运输，
  退休后不重启。下一轮须四板重新显式授权新的 probe。probe 绑定配置时的
  session，变更 session 本身不续发或恢复额度。
  自动模式另行验证估计策略及频率收敛，不能由单次命令成功自动提升质量。

#### 显式 AUTO 与 RATE 观测

AUTO 只在 STOP 后由 `SYSTem:VDC:FEEDback:AUTO` 显式授权，并绑定当时的非零
feedback session；与单次 probe 互斥。AUTO 关闭、probe 关闭、session 清零及
运行态 STOP/owner 换代都撤权，重 ARM 不恢复旧授权。跨核 mode 读取争用表示
“本拍暂不可得”，Core0 暂停准备，不能把它解释成关闭、切域或退休现有窗口。

RATE 反馈采用独立的 `REFMEM_VDC_FEEDBACK_RATE_SCHEMA` 与
`REFMEM_VDC_FEEDBACK_RATE_FLAGS`，完整大小及分片配额复用已有反馈，旧 raw/MODEL
格式保持独立。下面偏移属于 `VDC-BOUNDARY-01` 的登记 ABI，代码以
`refmem_sync_vdc_feedback.h/.c` 为比对入口。

| 反馈字节偏移 | RATE 含义 |
|---|---|
| 0–31 | 独立 schema/flags、来源/目标及源自身 clock/ARM/observer/measurement/tick_hz 身份，布局与既有反馈头一致。 |
| 32–39 | 绝对投影输出 lower bound，仅作为从板命令年龄依据。 |
| 40–47 | observer 相对计数按当前 committed DCO rate 缩放的频率坐标 lower bound；算术 upper 为 lower 加一个 ns，不包含物理检测不确定度。 |
| 48–59 | model token、applied command sequence、control session。 |
| 60–63 | 原反馈 CRC32 规则。 |

Core0 调用 VDC 投影接口准备 RATE 坐标及配对；主端参考使用 raw TX 括号分别向外
取整，不删除逐发参考括号宽度。源相对锚点仅在同 ARM/observer、同源模型差分中
消除；时钟桥读取仍校验时钟比例、年龄与 raw 模型切点，不能收窄旧 MODEL 语义。
配对 `reserved` 必须为 `VDC_FEEDBACK_RATE_DOMAIN`。固定缓存与每从 baseline
持有至 `VDC_FEEDBACK_RATE_MIN_INTERVAL_NS`；未成熟返回 WAIT，不增加成功配对或
错误计数。最大窗口遵守 `VDC_FEEDBACK_MODEL_MAX_INTERVAL_NS`，源或参考模型换代
重新建立 baseline；窗口末端仍单独检查新鲜度。

Core1 逐从读取有界准备结果，以误差区间靠近零的端点决定负反馈方向：源更快时
减小 rate；采用四分之一增益（当前控制策略快照，非永久调参事实源），deadband
与单步限幅分别由 `VDC_BOUNDARY_AUTO_DEADBAND_PPB` 和
`VDC_BOUNDARY_AUTO_MAX_DELTA_PPB` 定义。区间跨零或进入 deadband 则 HOLD，不能把
deadband 当成测量分辨率。每个 peer 保存自身不可变增量，每次实际应用至多一次。
精确 ACK 对账后才允许下一命令，其完整测量窗口必须属于应用后的新源模型；
窗口允许在 ACK 运输过程中积累，不声称等待 ACK 才开始采样。未知应用结果保持
UNRESOLVED，不叠加命令；late ACK 可对账，仍须新模型窗口。模型变化或总 rate
范围拒绝后的完整自动恢复另验，不由本切片授予。

板端维护记录复用 `VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES` 缓冲；版本由
`VDC_DPLL_MANAGER_DPLL_CAPTURE_SCHEMA` 定义。AUTO 观测/offer/apply/ACK/HOLD
使用独立 kind，不伪造旧 DPLL update_seq 或相位残差。TRACE ARM 成功时冻结本次
AUTO-only/legacy 记录模式；mode 读取争用则不 ARM，已 ARM 不被重新配置。AUTO-only
抑制旧 DPLL 记录追加，但不改变实际 DPLL 更新或发布；运行中撤权不切换本次记录
模式，STOP 后下一 ARM 再选择。offer 的端点记录与命令记录
成对预留，保存两端模型身份；命令保存 decoded 语义字段，不能称为原始 TX/RX
wire，ACK 保存真实收到的反馈字节。Core1 不做 CRC 或存储，全部 STOP 后由 Core0
导出。缓冲达到容量即停止，dropped 为零不等于全窗口完整；验收必须核查容量、
多轮实际应用、精确 ACK、独立区间复算与曲线变化。仅 HOLD 或一次测试增量不能
通过自动收敛验收。

验证须覆盖类型/CRC/乱序/取消、丢片后重复恢复、重复应用拒绝、组间普通帧、发送
上限和 ACK 提前退休、精确测量关联、年龄与模型变化、outbox ABA、未决
到期和迟到 ACK、连续重基、资源/栈及当前源码四板 P3；逐从真实应用与反馈对账另有
专项原件。完整物理精度和长稳恢复保持后续门禁。

### VDC-REFERENCE-01：显式参考事件时间戳运输

参考运输由 STOP 后的 `SYSTem:VDC:FEEDback:REFerence` 显式选择，复用
`VDC_BOUNDARY_MODE_REFERENCE` 和非零 feedback session，与 PROBE/AUTO 互斥。
该模式承载参考事件及其接收确认，不改变既有 MASTER/FOLLOWER 的 DCO 写入权限；本地跟踪
控制另行接入。session 清零、代际不匹配或 STOP 使旧数据失去当前准入资格。

MASTER 的 Core0 从 TDMA owner 稳定快照取 origin TX 事件，以事件发生时已提交的
DCO 模型做 `vdc_dpll_manager_project_feedback_event()` 投影，保留完整上下界、
model token、源 clock epoch/run、origin acquisition epoch、事件 sequence 和 session。
源 ARM/observer 两字段均绑定 origin acquisition epoch，不能解释为从板 EVENT tap
的代际；不使用 RATE 相对坐标或 phase/rate 补偿量替代输出事件时间戳。该投影仍为
内部模型时间区间，不授予已校准 pad 时间、物理精度或主板持续 PI 更新资格。

运输复用 `REFMEM_VDC_FEEDBACK_MODEL_SCHEMA`、`REFMEM_VDC_FEEDBACK_RECORD_SIZE`
及既有固定 VDC 分片配额，不改变 mailbox 长度、PIO 程序或帧节拍。MASTER 根据
上次完整发布的目标轮询其他节点，每组冻结一个事件和明确目标；组间保留普通帧。
组内每次准备检查绑定及 origin epoch，FIFO 未发布不得推进分片；不同目标可收到
不同的新事件，不能假定这一版本是同帧广播或每周期全从交付。

FOLLOWER 只接收显式指定主机、目标为本地且 session 相符的完整 MODEL 记录。
Core0 完成 CRC、类型、身份和顺序校验，再以 guarded snapshot 发布；远端 clock
身份留在 sample 中，接收者本地配置身份留在外层绑定中。完整组在绑定读取暂忙时
留待下一服务拍，已知 STOP、admission 变化或超时则退休，不能跨新会话重新采用。
收到参考、接收 ACK 和 DCO 采用分别判定，不能把 received 当作 applied。

接收确认使用 `REFMEM_VDC_RECEIPT_ACK_SCHEMA` 的独立类型，与原参考共用固定分片
通道；仅在完整校验并保留指定主机参考后准备，不等待本地同序观测、delay 校准或
DCO 采用。ACK 将来源/目标反转，回显原 MODEL 的身份及投影内容并重算 CRC；
恢复原 MODEL 后必须可逐字节对账。回显的 `applied_command_seq` 仍描述主机原模型，
不代表从板采用。ACK 组内不可被新参考改写，交接暂忙有界保留，取消沿既有准入退休。

MASTER 复用每来源 RX 快照的显式 `reference_proof` union arm 保存对应从板最近
完整 FIFO 发布的原参考，仅最后分片成功发布才更新证明；原 `record` 保存返回
的 ACK。旧反馈控制读取入口不得将此 arm 当作 decoded sample。恢复 ACK 的原参考
与当前证明完全相同时才增加接收确认计数；重复或证明已被较新发布淘汰时分别
计数，不扩大为所有组必达。`SYSTem:REFMEM:SYNC:TDMA:VDC:FEEDback:PROof?`
提供带绑定的有界快照，STOP 后仅保留历史，不授予当前资格。最后发送证明、最后
ACK 和最后从板参考可能不同步，离线分析须分别报告精确匹配，不能推断相等。
ACK 缺失不阻塞 TDMA 发车或 NO1 本地 PI；该确认也不作为本地跟踪的运行时前置。

验证覆盖发布内容冻结、目标轮询、跨核暂忙、FIFO 拒绝、STOP/代际撤销、错误来源/
目标/session/CRC、完整组暂忙保留，以及旧命令控制互斥。接收确认另覆盖完整发布
证明、错内容同序、重复/迟到、proof arm 消费隔离与取消。当前源码的资源、P3 和
逐从接收/确认专项各自留证；两条 TX 历史不足以同时证明所有目标的最终记录逐字节一致，
未匹配记录须如实标记，不以模型一致替代原始发布证据。本条登记保持 pending。

### VDC-PRIORITY-01：Core1 预编码同步邮箱与运行绑定

本条约束特等席同步记录的格式、发送与接收交接边界；登记为 pending，不授予单圈期限、
本地 DCO 采用、共同时间有效性或物理锁相。旧 `VDC-REFERENCE-01` 分片路线保留
既有证据，新同步类不得进入普通 RefMem/VDC fragment parser。

布局事实源为 `tdma_process_image_layout.h` 的
`TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS` 和 `PRIORITY_SYNC_*` 偏移，
编解码事实源为 `vdc_priority_codec.h/c`。沿用固定邮箱 header 的 magic、version、
source slot、target bitmap 和源事件序号低位（完整载荷圈序号仍由 transport header
携带）；body 为小端完整 binding
generation、完整 event sequence、output-ns 下界、区间宽度及 flags。邮箱 CRC 覆盖
header 与 body 至 `TDMA_PROCESS_IMAGE_CRC_OFFSET`；codec 的 body CRC helper
不能替代外层邮箱校验。generation 非零，width 非零且 lower+width 不溢出，
flags 必须在 `VDC_PRIORITY_CODEC_KNOWN_FLAGS` 内；失败保持输出不变。

时间值是源事件在实际 committed DCO 模型中的纳秒算术区间，保留完整上下界和
取整不确定度。它不是裸 RATE 仿射坐标，也不是对 GPIO 边沿精度的保证；原始
origin bracket 未包含的同步器/检测偏差须在后续精度任务校准。源事件早于当前
模型 `valid_from_raw` 时跳过，不能用新模型重新解释旧事件。正常模型修订不强制
变更运行 generation；同一事件首次成功编码后冻结字节，模型已变时不重新投影。

`SYSTem:VDC:PRIORity:SYNC` 是 Core0 STOP-only 意图，非零值必须严格递增且已有
feedback session；零禁用。generation 不替代 feedback session、origin epoch
或 model token。Core1 首次绑定完整 ring config、source epoch/tick rate、session、
role generation、clock epoch/run；绑定改变、STOP、序号回绕或反序时退休，必须
安装新 generation 才能再次绑定。模型/快照暂忙和单次无效事件只返回 EMPTY。

VDC provider 只在 TDMA Core1 origin owner 的固定服务边界生成一次候选；
DISABLED 允许普通发送，EMPTY/READY 保留特等席所有权。TDMA 保留 PIO/DMA 所有权，
READY 记录在本地槽位/header/CRC/布局复验后交给既有 origin exchange 双缓冲。
旧普通 origin overlay 仅租用独立私有 SRAM，可取消而不等待 Core0 ACK；其私有
工位在 ACK 前不得复用，也不得覆盖同步记录。硬件 selection 退休旧 DMA bank
后才允许下一次提交；软件 offer、双缓冲提交、实际选中和线上送达分别留证。

从板独立 priority RX 在普通队列之前保全完整 header/mailbox。STOP 配置门禁注册
固定 `tdma_priority_rx_sink_t`；Core1 捕获 IRQ 在外层校验和成功发布后同步交给
`vdc_priority_rx_core1`，不等待 DPLL phase、Core0 或普通负载队列。承载帧完整序号回绕时
交接使用发布后的本地 RX epoch；STOP、DMA 坐标退休和 epoch 耗尽以空记录退休
入口。回调只借用记录指针，不操作 FIFO/PIO/DMA、不执行完整 PI 服务。其耗时
计入 IRQ body，预算仍须实测闭合，不能以移至中断代替时限证明。

直接入口解码固定 body 并复验事件低位；同一相邻 source/generation/完整 event
身份而时间区间不同视为冲突。`vdc_priority_rx_snapshot_t` 分别报告 carrier、
typed 成功/拒绝、相邻重复和不同事件；后两者不证明历史全局单调性。最后成功
记录与最新回调状态分别保留，无效 body 不刷新成功记录。原 DPLL phase 中
`vdc_priority_ingress` 的 latest-only 副本仅作对照诊断，不代表本入口接收数量。
解码成功仅授予记录保留。控制仍须核对预安装绑定、完整事件序号、事件年龄和
本地 delay，不能把载荷圈序号当成记录事件序号，不能把陈旧记录视作新参考。
SCPI 仅配置/触发；TX 与 RX 明细在 STOP 后读回，不在实时路径采样。

验证覆盖普通类型隔离、全邮箱 CRC、上下界溢出、代际/模型/STOP 生命周期、
Core0 工位不阻塞、DMA bank 退休与四板实际 typed 记录。新增调用链须复核目标
栈/内存并测量静态调度预算；单圈送达、事件年龄、DCO 生效分别验收。

## 验证映射

| 验证层 | 必须证明 |
|---|---|
| Host/replay | FLL acquisition、PI tracking、rate drift、phase step、anti-windup 和 promotion 负测。 |
| TDMA short-frame | resident lifecycle、sequence/CRC、UP/DOWN、resource/persona 无冲突、hardware latch。 |
| Calibration | directed delay/bias、matrix completeness、generation/freshness 和失效重锁。 |
| VDC HIL | `COARSE_LOCKED` 与 `FORMAL_LOCKED` 分离，formal gate 失败不进入 RUN。 |
| NO5 | 只读外环 phase/evidence；不得参与控制或替代 ring evidence。 |
| Failure injection | stale、missing frame、bad CRC、dictionary mismatch、path mismatch、holdover aging、relock fail。 |
