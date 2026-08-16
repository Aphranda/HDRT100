# VDC 内部主域架构

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
Related: `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-08-16

本文档定义 Distributed Hard Real-Time Trigger System 在 HAOFV 下的 Virtual Distributed Clock / VDC 内部主域。VDC Domain 不是对外 SCPI 主域，也不是 `SYNC_IO` 的一个普通算法函数，而是整个分布式硬实时系统的核心基础件，负责让多节点形成同一条可验证、可门禁、可报告的共同时间轴。

## 主域定位

VDC Domain 的正式定位：

```text
Virtual Distributed Clock Domain
```

工程内部简称：

```text
VDC Domain
```

它回答的问题是：

```text
分布式系统中，所有节点使用哪一条共同时间轴来预测、触发、测量和报告。
```

它不回答：

```text
系统共同事实表中有哪些 slot。
产品测试序列下一步做什么。
某条链路 delay 如何测量。
某个 PIO 边沿如何立即输出。
```

## 职责边界

VDC Domain 负责：

- 建立和维护 `local_tick -> vdc_time` 映射。
- 管理 `epoch_id`、`run_id`、wrap tracker 和时间回绕扩展。
- 管理 timestamp dictionary 和 compact timestamp 展开规则。
- 管理 SYNC DPLL 的 `offset/rate/lock/holdover/relock`。
- 消费 Calibration Domain 发布的 active link delay。
- 发布 VDC 时间事实、质量、版本和 evidence。
- 给 Trigger / Loop / Measure / Report 提供共同时间快照。
- 给 RefMem Domain 提供 VDC snapshot、quality、stale、CRC 和版本字段。
- 给 RUN gate 提供 VDC lock、quality、holdover age 和 fault 判据。

VDC Domain 不负责：

- 不执行业务触发序列。
- 不直接切换链路控制节点或其他业务硬件；当前项目实例中的 SP8T/SP2T 只属于链路控制节点的具体资源映射。
- 不测量校准链路 delay；链路 delay 由 Calibration Domain owner 产生。
- 不维护 RefMem slot 同步协议；RefMem Domain 只保存 VDC 快照。
- 不传输 OTA payload、日志全文、波形或大 trace。
- 不建立裸顶级 `VDC:*` 或 `DPLL:*` SCPI 域。

## HAOFV 层级

```text
SCPI / UI / System Pack
        ↓
SYNC action / SystemAO / ConfigGate
        ↓
VDC Domain
  VdcSyncAO / SyncDpllFB / HoldoverFB / RelockFB
        ↓
VdcVector / VdcQualityTable / TimestampRing
        ↓
timestamp service / RJ45_SYNC_RING / PIO capture
        ↓
local_tick / compact timestamp
```

和其他内部主域的关系：

```text
Calibration Domain -> active link delay -> VDC Domain
Realtime Service   -> compact timestamp -> VDC Domain
VDC Domain         -> vdc_time/quality -> RefMem Domain
RefMem Domain      -> shared snapshot/gate -> Trigger/Report/System
Trigger Domain     -> VDC snapshot -> FIRE_LOAD/local_fire
Measure Domain     -> T2/READY timestamp -> VDC quality/report
```

## 外部参考机制

VDC Domain 的参考对象聚焦在“共同时间”和“同步质量”，和 RefMem 的表驱动/RMA/ACK-NACK 参考分开维护。外部项目只提供工程机制，不改变 DTC100 自定义 VDC 协议和 HAOFV owner 边界。

| 参考对象 | 可借鉴机制 | VDC 落地方式 | 不采用内容 |
|---|---|---|---|
| LinuxPTP / Chrony | offset、frequency/rate、RMS offset、jitter、skew、slew、servo reset、holdover。 | `VdcDpllState` 保存 offset/rate/phase/frequency error；`VdcQualityTable` 保存 jitter/RMS/peak/holdover age/servo reset count；RUN gate 使用同一质量事实。 | 不引入 NTP/PTP 协议栈，不调整系统 wall clock，不让上位机直接调节主环路。 |
| SOEM / EtherCAT DC | reference clock、传播 delay 测量、initial sync、周期性 drift compensation、同步输出/输入 timestamp。 | A0 可作为首版 reference node；Calibration 提供 link delay；SYNC DPLL 形成 VDC；Trigger 预测分发使用 VDC snapshot；T2/READY timestamp 回写质量和证据。 | 不采用 EtherCAT 协议、ESC 寄存器模型、硬件 DC 单元或完整 SOEM 主站。 |
| IEC 61499 | 静态 AO/FB、事件输入输出、数据输入输出、部署一致性。 | 约束 `VdcSyncAO / SyncDpllFB / HoldoverFB / RelockFB / VdcQualityGateFB` 的静态事件和数据边界。 | 不做动态 FB 部署，不引入 IEC 工具链，不把 VDC 变成通用分布式运行时。 |

工程规则：

- LinuxPTP/Chrony 类字段用于描述 DPLL 质量，不直接等同于网络协议字段。
- EtherCAT DC 类机制用于描述 reference、delay、initial sync 和 drift compensation 思想，不采用 EtherCAT 协议。
- VDC 的 offset/rate 是唯一共同时间事实；RefMem 只保存 snapshot。
- HOLDOVER 的 age、drift bound 和 relock result 必须进入报告证据。
- Trigger 预测分发只能消费 `LOCKED/HOLDOVER` 且质量门限通过的 VDC snapshot。

### VDC 框架补足

结合 LinuxPTP / Chrony / EtherCAT DC 的机制，VDC Domain 需要补齐五个内部框架。

| 框架 | 作用 | 参考机制 |
|---|---|---|
| `VdcReferenceClockTable` | 定义 reference node、candidate、priority、当前 source 和切换原因。 | PTP reference clock / BMCA 思想，首版可固定 A0。 |
| `VdcServoProfile` | 定义 DPLL/servo 参数、step/slew、sanity limit 和 reset 策略。 | LinuxPTP `pi/linreg` servo、step threshold、sanity frequency limit。 |
| `VdcErrorBudget` | 定义 offset、RMS、peak、jitter、skew、delay、dispersion、holdover drift bound。 | Chrony tracking/sourcestats、PTP summary statistics。 |
| `VdcDcSyncPipeline` | 定义 delay 校准、initial sync、drift compensation、sync output/input timestamp 闭环。 | SOEM / EtherCAT DC `configdc`、sync0 cycle/shift、propagation delay 思想。 |
| `VdcHoldoverModel` | 定义进入、维持、失效和 relock 的 aging 规则。 | Chrony root dispersion / max clock error、PTP servo reset 思想。 |

### TDMA + DPLL 融合控制模型

VDC 不是在 TDMA 和 DPLL 之间二选一。产品化架构应采用“TDMA 提供确定性观测骨架，DPLL 形成共同时间估计”的融合模型：

```text
TDMA deterministic observation window
  -> PIO/DMA timestamp capture
  -> VdcSyncAO sample validation
  -> SyncDpllFB offset/rate estimator
  -> VdcClockModel / VdcVector snapshot
  -> core1 realtime phase pull / FIRE_LOAD timing
```

三层控制环的职责如下：

| 控制环 | 执行者 | 时间尺度 | 主要输入 | 主要输出 | 不允许做的事 |
|---|---|---:|---|---|---|
| TDMA 硬实时环 | PIO / DMA / core1 realtime | 1e3ns 级窗口，ns timestamp | active TDMA schedule、reference edge、local tick | capture timestamp、sync frame、FIRE_LOAD edge | 计算 DPLL、访问 SCPI/SD/USB、修改 offset/rate |
| DPLL 锁相环 | `VdcSyncAO / SyncDpllFB` | 1e6ns 级 service tick | validated timestamp sample、active calibration delay、profile | offset/rate、phase error、lock state、quality | 直接驱动 PIO 输出、绕过 RefMem/Vector 写其他域事实 |
| 低频驯服环 | `HoldoverFB / VdcQualityGateFB` | s 级窗口 | rate history、temperature/aging evidence、holdover age | drift bound、dispersion、servo profile evidence、persistent compensation candidate | 在 RUN 热写 flash、改变实时 tick source、直接修正 local_tick |

该模型的关键点是：TDMA 只保证 DPLL 观测样本的确定性和低干扰，不等于共同时间已经锁定；DPLL 只修改 VDC 的 `offset/rate/quality`，不直接发硬实时边沿；core1/PIO 只消费稳定 VDC snapshot 进行相位牵引或预测输出。

#### TDMA Observation Window

TDMA schedule 必须给 VDC 保留固定同步观测窗口。该窗口用于发送/接收 reference sync frame、采集硬件 timestamp 和形成 DPLL sample，不能被普通数据帧、维护帧或 report payload 抢占。

| 字段 | 含义 |
|---|---|
| `tdma_epoch` | 当前 TDMA 周期或 schedule epoch。 |
| `tdma_period_ns` | 标称 TDMA 周期，例如 `1000000 ns`；实际数值来自 active profile。 |
| `sync_window_offset_ns` | 同步窗口相对周期起点的偏移。 |
| `sync_window_width_ns` | 同步窗口宽度。 |
| `guard_before_ns / guard_after_ns` | 同步窗口前后保护时间。 |
| `reference_slot_id` | 当前 reference node / slot，首版可固定 A0。 |
| `schedule_crc32` | TDMA schedule profile 摘要。 |
| `schedule_version` | schedule layout/version，用于跨节点一致性。 |

规则：

- 同步窗口只承载 reference edge、timestamp capture 和必要的同步帧摘要。
- 普通 RefMem delta、维护数据、日志、SD/OTA payload 不能进入同步观测窗口。
- TDMA schedule 更新只能走 VDC/System Pack staging 和 activation，不得在 RUN 中热改窗口位置。
- `schedule_crc32` 必须进入 VDC profile CRC 和 RefMem version bundle；不一致时拒绝 LOCK。

#### Two-board TDMA Hardware Baseline

当前 COM5/COM6 两块最小系统板已经形成真实物理环路。该环路使用无 CS 的 3-wire PIO SPI adapter，在 25 MHz 下通过 core1 realtime TDMA service 承载 RefMem `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 和 NodeLoad AUTO 同步。该结果说明：

- 已具备 TDMA 承载 DPLL 的硬件基础：两板之间可以在受控窗口内完成真实 TX/RX，而不是依赖 PC 搬运 frame hex。
- 当前已验证的是 RefMem data TDMA window 和 NodeLoad/quality 同步，不等价于 VDC DPLL 已锁定。
- 当前链路已有 schedule、slot、direction、deadline、timeout、CRC 和 quality evidence 的雏形，但还缺少 DPLL 可用的硬件 timestamp evidence。
- 后续 DPLL 只能消费 VDC observation window 产生的 timestamp sample，不能直接把 RefMem frame 成功、host 侧耗时或 `time_us_64()*1000` 诊断时间当作 100 ns 级同步证据。

在该基线之上，VDC 需要把同一条总线的 TDMA cycle 划分成不同 window class。它们不是两套互不相关的 TDMA，而是同一个 active schedule 中的不同 slot：DPLL/VDC 维护是总线时序骨架的主约束，RefMem 数据同步是在该骨架上搭载 payload。

| 窗口 | 用途 | 可承载内容 | 禁止 |
|---|---|---|---|
| `VDC_OBSERVATION_WINDOW` | 周期维护 DPLL/VDC。 | reference edge、SYNC timestamp、compact timestamp、sample CRC。 | 普通 RefMem delta、SD/OTA payload、维护日志。 |
| `REFMEM_DATA_WINDOW` | 在 VDC TDMA 骨架上同步共同事实。 | `DELTA/ACK_NACK/FENCE/QUALITY`、NodeLoad、quality evidence。 | 计算 offset/rate，发布 VDC lock。 |

窗口是调度单位，帧是协议单位。每一帧首先都是 `TDMA/VDC frame envelope`，必须服务共同时间维护；业务数据只是该 envelope 上的 payload class。也就是说，RefMem frame 不应被理解为“只同步数据的普通 SPI 包”，而应被理解为“携带 RefMem payload 的 TDMA/VDC 帧”。

```text
TDMA/VDC frame envelope
  -> preamble / frame boundary
  -> schedule_epoch / slot_index / frame_seq
  -> reference_slot_id / source_slot_id
  -> compact timestamp or timestamp latch reference
  -> schedule_crc32 / frame_crc32
  -> payload_class
  -> payload bytes
```

| payload class | 作用 | DPLL 关系 |
|---|---|---|
| `SYNC_SAMPLE` | reference edge 或同步观测样本。 | 必须进入 timestamp validation；合格后进入 DPLL。 |
| `REFMEM_DELTA` | 共同事实变化同步。 | 帧边界和 RX/TX timestamp 仍可作为质量或 DPLL candidate；payload 不参与 DPLL 计算。 |
| `ACK_NACK/FENCE/QUALITY` | completion、fence 和质量证据。 | 用于 freshness/quality gate；不写 offset/rate。 |
| `IDLE_BEACON` | 无业务数据时维持时钟、slot 和 membership。 | 可持续提供 DPLL 样本和 holdover freshness。 |

因此，总线在同步数据过程中也必须维护 DPLL；总线在循环维护 DPLL 时可以顺带同步 RefMem。没有业务数据时仍应发 `IDLE_BEACON` 或等价同步帧，避免 DPLL 样本中断。

同一 TDMA cycle 的运行顺序应由 `VdcTdmaScheduleProfile` 冻结。首版可以固定为：

```text
cycle[k]
  -> VDC_OBSERVATION_WINDOW  高优先级 reference / sync sample
  -> REFMEM_DATA_WINDOW      搭载 RefMem delta / ACK / FENCE / QUALITY，同时记录帧级 timestamp
  -> IDLE_BEACON_OR_GUARD    无数据时维持 DPLL freshness，有数据时提供方向切换和故障恢复余量
```

`REFMEM_DATA_WINDOW` 可以复用现有 PIO SPI physical adapter 和 TDMA mailbox；所有帧都应尽量形成硬件 timestamp evidence。`VDC_OBSERVATION_WINDOW` 是最高质量、最高优先级的 DPLL 样本窗口，必须增加硬件 timestamp latch，形成 `expected_ns / observed_ns / delay_ns / phase_error_ns / quality_flags` 样本后再交给 `VdcSyncAO / SyncDpllFB`。RefMem 数据同步可以携带当前 VDC snapshot、quality 和 evidence 摘要，但不能反向拥有 DPLL 状态。

首版两板 DPLL bring-up 建议采用固定 reference：

```text
Board X: reference slot
  -> 在 VDC_OBSERVATION_WINDOW 输出 reference edge / sync frame

Board Y: follower slot
  -> 在同一窗口捕获 RX edge timestamp
  -> 校验 schedule_crc32、reference_slot_id、seq、CRC、window bound
  -> 形成 VdcDpllSample
  -> SyncDpllFB 更新 offset/rate/quality

反向或双向测量:
  -> 用于 delay 校准、对称性检查和 fault evidence
```

该过程必须保留 HAOFV owner 边界：

- TDMA/core1/PIO 只执行窗口和采样。
- Timestamp service 只展开 compact timestamp、扩展 tick 和写样本 ring。
- `VdcSyncAO / SyncDpllFB` 是 offset/rate/lock 的唯一 writer。
- RefMem 只镜像 VDC snapshot、quality、fault 和 evidence，不计算 DPLL。
- SCPI 只配置 staging profile、发起动作事务或读取 snapshot。

100 ns 目标的工程门禁如下：

| 项 | 要求 |
|---|---|
| 时间单位 | 所有 TDMA/DPLL timestamp 字段统一使用 `ns`。 |
| 分辨率声明 | snapshot 必须暴露 `timestamp_resolution_ns`，不能只靠字段名暗示精度。 |
| DPLL 准入 | `timestamp_resolution_ns <= 100` 且样本来自硬实时 latch，才允许进入正式 DPLL lock gate。 |
| 过渡实现 | `time_us_64() * 1000` 只能作为诊断时间戳，必须报告 `timestamp_resolution_ns=1000`，不得作为 100 ns evidence。 |
| 观测字段 | 至少包含 expected window start、arm/start/done/apply timestamp、late_ns、jitter_ns、schedule_crc32 和 frame/sample CRC。 |

#### DPLL Servo And DCO Contract

DPLL 的输出不是直接修改本地硬件 timer，而是更新 VDC clock model 的受控参数。core1 读取该 snapshot，把未来 `vdc_time` 映射到 `local_fire_tick`，通过小步 slew 或 phase pull 消化相位误差。

VDC clock model 需要同时表达标称周期和修正量：

| 字段 | 含义 |
|---|---|
| `base_epoch_id` / `run_id` | 时间上下文，防止旧样本进入新 RUN。 |
| `base_local_tick64` | 当前模型锚点的本地 tick。 |
| `base_vdc_time64_ns` | 当前模型锚点对应的 VDC 时间。 |
| `nominal_period_ns` | TDMA/VDC 标称周期。 |
| `period_adjust_ppb` 或 `rate_q32` | DPLL 输出的频率修正。 |
| `phase_offset_ns` | DPLL 输出的相位偏移。 |
| `slew_limit_ppb` | 每周期允许的最大平滑牵引量。 |
| `dco_update_seq` | DCO 参数提交序号。 |
| `tdma_schedule_crc32` | 使用中的 TDMA schedule 摘要。 |
| `servo_profile_crc32` | 使用中的 DPLL profile 摘要。 |

规则：

- `base_local_tick64` 是观测事实，不能被 DPLL 改写。
- `period_adjust_ppb/rate_q32` 和 `phase_offset_ns` 的唯一 writer 是 `SyncDpllFB` 的 commit。
- core1 读取 DCO snapshot 必须使用 seqlock、双缓冲或等价 guard，避免半新半旧参数。
- DPLL 可使用 step/slew 策略，但 RUN 中默认只允许 slew；超过 step/sanity limit 应进入 `RELOCKING` 或 `FAULT`。
- 参考实现中的固定地址、固定 1 ms、固定 ±50 ppm、固定 KP/KI/KD 都只能作为调试 profile 初值，不能写死为架构常量。

#### Low Frequency Discipline

低频驯服环用于长期稳定性和 HOLDOVER 误差预算，不直接参与每个同步周期的硬实时输出。

| 输出 | 用途 |
|---|---|
| `aging_compensation_ppb` | 老化补偿候选值，允许在维护窗口持久化。 |
| `temperature_compensation_ppb` | 温度补偿输入，来自传感器或 profile 表。 |
| `wander_ppb` | 长期频率漂移估计。 |
| `holdover_drift_bound_ns_s` | HOLDOVER 期间误差增长上界。 |
| `discipline_window_s` | 低频统计窗口。 |
| `persistent_profile_seq` | 持久化补偿 profile 序号。 |

规则：

- 低频补偿只能更新 staging compensation 或下一轮 servo profile，不得在 RUN 中直接写 flash。
- HOLDOVER 时冻结最后可信 offset/rate，并按 `holdover_drift_bound_ns_s` 增长 `dispersion_ns`。
- RELOCK 只能恢复 VDC `LOCKED`，不自动恢复 TRIG RUN。
- 长期指标如 Allan deviation、稳态 RMS、温漂补偿效果必须作为验证目标和报告字段，不作为未经实测的产品保证。

#### Fused State Machine

TDMA/DPLL 融合后，VDC 状态机需要区分“TDMA schedule 可用”和“VDC 已锁定”：

| 状态 | 触发条件 | TDMA 行为 | DPLL 行为 | 输出语义 |
|---|---|---|---|---|
| `OFF` | VDC 未启用 | 释放同步资源 | 停止 | 不发布有效共同时间。 |
| `CHECKING` | `SYNC:CHECk` 或 profile activation 后 | 检查 schedule/profile/cal CRC | 清空或准备 servo | 只能发布检查状态。 |
| `INITIAL_SYNC` | reference、cal、timestamp dictionary 通过 | 建立同步窗口并采样 | 允许 step 或粗 offset 初始化 | 粗同步，不允许正式 RUN。 |
| `FREQ_LOCK` | initial sync 样本连续有效 | TDMA 正常发/收同步帧 | 频率快速拉入，积分可限幅或关闭 | 频率趋同，但相位质量未达 RUN 门限。 |
| `PHASE_LOCK` | frequency error 进入门限 | TDMA 正常 | PI/PID 或产品 servo 全量收敛 | 相位误差进入窗口，准备 LOCKED。 |
| `LOCKED` | offset RMS、peak、freshness、CRC/seq 连续通过 | TDMA 正常 | 低抖动跟踪 | 可作为 FIRE_LOAD、T2/READY 和报告时间基准。 |
| `HOLDOVER` | reference 丢失或样本 stale | 可旁路或保留 schedule | 冻结可信 rate/offset，增长 dispersion | 只在 drift bound 内有限可用。 |
| `RELOCKING` | 新有效样本恢复 | TDMA 正常 | 带 outlier gate 重锁 | 不自动恢复 TRIG RUN。 |
| `FAULT` | cal/profile/CRC/seq/freshness/sanity 失败 | 释放或降级 | reset/fault evidence | 禁止 RUN 和新 FIRE_LOAD。 |

#### VdcServoProfile

`VdcServoProfile` 描述 DPLL 如何从 timestamp sample 形成 offset/rate。它不是上位机日常调参表，而是维护和调试接口可观测的 active profile。

| 字段 | 含义 |
|---|---|
| `servo_type` | `PI`、`LINREG` 或产品自定义类型。 |
| `kp_q16` / `ki_q16` | PI 环路参数。 |
| `update_period_1e3ns` | DPLL 更新周期。 |
| `first_step_threshold_ns` | 初始大偏差是否允许 step。 |
| `step_threshold_ns` | 运行中超过该偏差时是否 step 或拒绝。 |
| `sanity_freq_limit_ppb` | 频率修正 sanity limit，超限触发 reset/fault。 |
| `offset_lock_threshold_ns` | LOCKED 判据的 offset 阈值。 |
| `lock_sample_count` | 连续满足阈值的样本数。 |
| `outlier_threshold_ns` | 样本剔除阈值。 |
| `reset_policy` | `PROFILE_CHANGE/CAL_CHANGE/FREQ_LIMIT/STEP_LIMIT/FAULT` 的 reset 策略。 |

#### VdcErrorBudget

`VdcErrorBudget` 给 RUN gate 和报告使用，不直接驱动硬件。

| 字段 | 含义 |
|---|---|
| `last_offset_ns` | 最近一次 offset 估计。 |
| `rms_offset_ns` | 统计窗口内 RMS offset。 |
| `max_abs_offset_ns` | 统计窗口内最大绝对 offset。 |
| `freq_offset_ppb` | 当前频率修正。 |
| `freq_skew_ppb` | 频率估计误差边界。 |
| `path_delay_ns` | active link delay 或当前同步路径 delay。 |
| `delay_stddev_ns` | delay 统计波动。 |
| `dispersion_ns` | HOLDOVER 或未更新期间累积不确定度。 |
| `root_distance_ns` | `path_delay/2 + dispersion + remaining_correction` 的本项目等价误差上界。 |

#### VdcDcSyncPipeline

VDC 的 DC 建立流程必须可拆分、可检查、可重放：

```text
select reference node
  -> load active calibration delay
  -> check timestamp dictionary/profile CRC
  -> initial sync sample window
  -> estimate offset/rate
  -> drift compensation loop
  -> LOCKED quality gate
  -> publish VDC snapshot to RefMem
  -> Trigger consumes snapshot for FIRE_LOAD
  -> T2/READY timestamp validates action timing
```

规则：

- reference node 首版可以固定 A0，后续再支持 priority / failover。
- propagation delay 只来自 Calibration active 表。
- initial sync 结果必须带 profile CRC、cal CRC 和 timestamp dictionary CRC。
- drift compensation 必须持续运行，不能只在启动时校一次。
- sync output/input timestamp 必须进入 evidence，用于报告 `e_vdc/e_act/e_pll`。

### 首版 PIO/VDC 参考装配链

当前 RP2350 首版可以参考下面路径，把 VDC 的观测和预测分发落到两个 PIO state machine、DMA、core1 realtime 和 core0 VDC task 上。该链路用于说明 VDC Domain 和 REALtime / Trigger / Loop 的接口边界，不冻结具体 PIO instance、GPIO、DMA channel 或最终布线路径；后续可根据实际资源、布线和板级 profile 调整。

```text
PIO_SM0: SYNC_RX_CAPTURE
  -> monitor differential input
  -> capture rising edge
  -> write capture event to RX FIFO
  -> DMA writes FIFO to RAM timestamp ring
  -> core1_realtime reads capture timestamp
  -> core1_realtime writes TriggerSlot summary
  -> core1_realtime writes DPLL input sample / timestamp ring
  -> task_vdc_sync consumes sample
  -> SyncDpllFB updates offset/rate
  -> VdcVector publishes VdcSlot snapshot

task_loop_engine
  -> reads VDC snapshot
  -> computes T_fire_base / local_fire tick
  -> emits FIRE_LOAD
  -> trigger_command_queue

PIO_SM1: SYNC_TX_FIRE
  -> receives FIRE_LOAD through core1_realtime
  -> outputs differential edge at target tick
  -> ISO7740 -> differential line -> peer node
```

| 环节 | owner | 输入 | 输出 | 禁止 |
|---|---|---|---|---|
| `PIO_SM0: SYNC_RX_CAPTURE` | REALtime / PIO service | 差分输入边沿 | RX FIFO capture event | 执行 DPLL、访问 RefMem、访问 USB/SD |
| DMA capture | DMA owner | PIO RX FIFO | RAM timestamp ring | 动态分配、阻塞等待 |
| `core1_realtime` capture reader | core1 realtime | RAM timestamp ring | TriggerSlot 摘要、DPLL input sample | 写 VDC offset/rate、格式化日志 |
| `task_vdc_sync` | VdcSyncAO / SyncDpllFB | DPLL input sample、active cal delay | offset/rate、lock、quality | 直接驱动 PIO 输出 |
| `VdcSlot` | VdcVector / RefMem mirror | VDC snapshot | RefMem 共同事实 | 被 Trigger/Loop 直接写 |
| `task_loop_engine` | LoopEngineAO | VDC snapshot、sequence、angle | `T_fire_base`、`FIRE_LOAD` | 写 VDC offset/rate |
| `core1_realtime` fire loader | core1 realtime | `FIRE_LOAD` | PIO target tick | 阻塞等待 core0 |
| `PIO_SM1: SYNC_TX_FIRE` | REALtime / PIO service | target tick、polarity、width | 差分输出边沿 | 计算 VDC、读取 SCPI |

关键约束：

- `PIO_SM0` 只捕获边沿并输出最小事件。
- DMA 只搬运 FIFO 到 RAM ring。
- `core1_realtime` 可以整理 timestamp sample 和写 TriggerSlot 摘要，但不能更新 VDC offset/rate。
- `task_vdc_sync` 是 SYNC DPLL 的唯一 writer。
- `task_loop_engine` 消费 VDC snapshot 计算 `T_fire_base`，不参与 VDC 收敛。
- `PIO_SM1` 只在指定 tick 输出边沿，late 的 `FIRE_LOAD` 必须拒绝补发。
- 所有跨核共享 ring/slot 必须带 sequence、CRC 或等价 guard。
- 具体 PIO/SM/GPIO/DMA 资源属于 board profile 和 SYNC_IO 资源适配，不在 VDC 主域冻结。

## 内部数据模型

VDC Domain 首版冻结以下基础表。字段可以分阶段实现，但 owner、writer、reader 和生命周期必须先稳定。

| 表 | 作用 | 唯一 writer |
|---|---|---|
| `VdcClockModel` | 描述 `local_tick` 到 `vdc_time64_ns` 的映射。 | `VdcSyncAO / SyncDpllFB` |
| `VdcTdmaScheduleProfile` | 描述 TDMA 周期、同步窗口、guard、reference slot 和 schedule CRC。 | `VdcSyncAO / profile loader` |
| `VdcReferenceClockTable` | 描述 reference node、candidate、priority、source 和切换原因。 | `VdcSyncAO` |
| `VdcDpllState` | 保存 SYNC DPLL 状态、offset、rate、phase/frequency error。 | `SyncDpllFB` |
| `VdcDcoControl` | 保存 DPLL 输出到 core1/PIO 消费的 DCO snapshot、slew limit 和 update seq。 | `SyncDpllFB` |
| `VdcServoProfile` | 保存 DPLL/servo 参数、step/slew、sanity limit 和 reset 策略。 | `VdcSyncAO / profile loader` |
| `VdcQualityTable` | 保存 jitter、RMS、peak、stale、holdover age、lock quality。 | `VdcQualityGateFB` |
| `VdcErrorBudget` | 保存 offset/rate/delay/dispersion/root distance 等误差预算。 | `VdcQualityGateFB` |
| `VdcTimestampDictionary` | 把 compact timestamp 的 source/event 展开为节点、端口和信号语义。 | `VdcSyncAO / profile loader` |
| `VdcWrapTracker` | 扩展 `tick_l32` 和 `seq_delta`，形成 64 位时间和完整序号。 | `timestamp service / VdcSyncAO` |
| `VdcCalibrationBinding` | 绑定 active calibration CRC、link delay 和使用范围。 | `VdcSyncAO` 只读 CAL active 结果后发布绑定 |
| `VdcDcSyncPipeline` | 保存 reference、initial sync、drift compensation 和 locked gate 阶段结果。 | `VdcSyncAO` |
| `VdcHoldoverPolicy` | 定义 HOLDOVER 进入、保持、退出和 RELOCK 策略。 | `VdcSyncAO` |
| `VdcDisciplineModel` | 保存长期漂移、温度/老化补偿候选和持久化 profile seq。 | `HoldoverFB / VdcQualityGateFB` |
| `VdcGateResult` | 给 RUN gate 的 lock/quality/reject/evidence 输出。 | `VdcSyncAO` |

### Timestamp Dictionary 与 Wrap Tracker

`VdcTimestampDictionary` 是 compact timestamp 的语义展开表，不承载实时数据。它由 `VdcSyncAO / profile loader` 从 active profile 或 System Pack 加载，字段至少包含 `event_id`、`source_slot_id`、`reference_slot_id`、`source`、`resolution_ns`、`default_flags`、`port_id`、`signal_id` 和 `payload_class`。表必须带 `version`、`entry_count`、`profile_crc32` 和 `dictionary_crc32`；版本、CRC、entry 有效性、event id 唯一性不通过时，timestamp sample 不得进入 DPLL。

`VdcWrapTracker` 是 timestamp service 的局部扩展状态，只把 `tick_l32` 扩展成 `local_tick64`，不写 offset/rate，也不写 VDC lock。首版规则为：

- `tick_l32` 正向递增时直接拼接当前高 32 位。
- `tick_l32` 从接近 `0xFFFFFFFF` 回到低值且差值超过半量程时，判定为一次正向回绕并递增高 32 位。
- `tick_l32` 小幅倒退默认拒绝；只有调用方明确给出 `max_backward_ticks` 才允许有限乱序。
- `tick_l32` 从低值跳回接近 `0xFFFFFFFF` 判定为 stale/pre-wrap 样本并拒绝。

`VdcCompactObservationSample` 是 PIO/DMA/core1 capture fact 进入 VDC 的最小载荷。它只包含 `sample_seq`、`event_id`、`tick_l32`、expected window、CRC 和质量摘要；VDC owner 必须先用 active `VdcTimestampDictionary` 展开 event，再用 `VdcWrapTracker` 扩展 tick，最后生成 `VdcTDMATimestampEvidence` 并经过 observation window gate。禁止 realtime IO、RefMem、SCPI 或 storage 直接构造 DPLL accepted sample。

`VdcSyncIoAdapter` 是 VDC 侧的 raw capture word 适配层。它可以把 `sync_capture_4bit` 的 8 个 4-bit sample word 按 `sample_period_ns`、edge event id 和 observed mask 转换为 `VdcCompactObservationSample`；它不访问 `sync_io` 内部状态，不计算 DPLL，也不声明样本可锁相。样本是否具备 `HARDWARE_TICK / <=100 ns / DPLL_ELIGIBLE` 资格，只由 active `VdcTimestampDictionary` 和 VDC gate 判定。

### 核心字段

VDC snapshot 至少需要覆盖：

| 字段 | 含义 |
|---|---|
| `epoch_id` | 时间 epoch；用于 tick wrap、run 切换和报告排序。 |
| `run_id` | 当前运行批次。 |
| `local_tick64` | 本节点扩展后的本地单调 tick。 |
| `vdc_time64_ns` | 映射到共同时间轴的纳秒时间。 |
| `base_local_tick64` | clock model 当前锚点的本地 tick。 |
| `base_vdc_time64_ns` | clock model 当前锚点的 VDC 时间。 |
| `nominal_period_ns` | TDMA/VDC 标称周期。 |
| `tdma_epoch` | 当前 TDMA schedule epoch。 |
| `tdma_schedule_crc32` | 当前 TDMA schedule profile CRC。 |
| `offset_ns` | 本地时间到 VDC 的相位偏移。 |
| `rate_ppb` / `rate_q32` | 本地时钟相对 VDC 的频率修正。 |
| `period_adjust_ppb` | DPLL 输出的 DCO 周期牵引量。 |
| `slew_limit_ppb` | RUN/HOLDOVER 中允许的最大平滑牵引量。 |
| `dco_update_seq` | DCO snapshot 提交序号。 |
| `phase_error_ns` | 当前同步相位误差。 |
| `freq_error_ppb` | 当前频率误差估计。 |
| `jitter_rms_ns` | 同步残差 RMS。 |
| `jitter_pk_ns` | 同步残差峰值或窗口峰值。 |
| `holdover_age_1e3ns` | 进入 HOLDOVER 后的持续时间。 |
| `lock_state` | `OFF/CHECKING/LOCKING/LOCKED/HOLDOVER/RELOCKING/FAULT`。 |
| `lock_quality` | 门禁质量等级或位图。 |
| `active_cal_crc` | 当前用于修正 link delay 的校准表 CRC。 |
| `timestamp_dict_crc` | timestamp dictionary 版本。 |
| `sync_profile_crc` | 同步参数、DPLL 参数和 gate limit CRC。 |
| `aging_compensation_ppb` | 长期老化补偿候选或 active 值。 |
| `temperature_compensation_ppb` | 温度补偿输入。 |
| `holdover_drift_bound_ns_s` | HOLDOVER 误差增长上界。 |
| `servo_reset_count` | DPLL 重置次数。 |
| `last_fault_code` | 最近故障。 |
| `last_evidence_index` | 诊断证据索引。 |

## 共同时间映射

每个节点维护从本地 tick 到 VDC 的映射：

```text
vdc_time = local_tick * rate + offset - calibrated_link_delay
```

实现中可以使用定点形式：

```text
vdc_time64_ns = local_tick64 * tick_ns * rate_q32 + offset_ns - delay_ns
```

规则：

- `local_tick64` 是原始观测事实，不能被 DPLL 修正。
- `offset/rate` 只能由 SYNC DPLL owner 写入。
- `delay_ns` 只能来自 active calibration 绑定。
- `vdc_time64_ns` 只有在 `LOCKED/HOLDOVER` 且质量门限通过时才可作为正式 RUN 基准。
- Angle DPLL、Trigger、SCPI、Storage、Report 只能读取 VDC snapshot，不得写 offset/rate。

## 状态机

| 状态 | 含义 | 允许转移 |
|---|---|---|
| `OFF` | VDC 未启用。 | `CHECKING` |
| `CHECKING` | 检查节点、RefMem freshness、active calibration、timestamp dictionary、TDMA schedule 和 servo profile。 | `INITIAL_SYNC`, `FAULT`, `OFF` |
| `INITIAL_SYNC` | TDMA 同步窗口可用，采集初始 sample 并建立粗 offset。 | `FREQ_LOCK`, `FAULT`, `OFF` |
| `FREQ_LOCK` | DPLL 快速拉入频率，积分项可限幅或关闭。 | `PHASE_LOCK`, `HOLDOVER`, `FAULT`, `OFF` |
| `PHASE_LOCK` | DPLL 收敛相位，offset RMS/peak 逐步进入门限。 | `LOCKED`, `HOLDOVER`, `FAULT`, `OFF` |
| `LOCKED` | VDC 可作为正式预测分发和 T2/READY 时间基准。 | `HOLDOVER`, `RELOCKING`, `FAULT`, `OFF` |
| `HOLDOVER` | 丢失部分同步观测，使用最后 rate/offset 和 aging gate。 | `RELOCKING`, `FAULT`, `OFF` |
| `RELOCKING` | 使用当前 holdover 状态尝试重新锁定。 | `LOCKED`, `FAULT`, `OFF` |
| `FAULT` | 同步质量、CRC、seq、cal/profile 或 freshness 失败。 | `CHECKING`, `OFF` |

## 跨域契约

| 来源域 | 给 VDC 的输入 | VDC 的处理 |
|---|---|---|
| Calibration | active link delay、calibration CRC、link key。 | 校验 CRC 和 link key，绑定到当前 VDC profile。 |
| Realtime | compact timestamp、edge flags、local tick。 | 展开、校验、wrap extend，形成 timestamp sample。 |
| RefMem | node freshness、epoch/run_id、deployment gate 输入。 | 作为 lock gate 和 stale 判据。 |
| SYNC | check/start/stop/relock/holdover 事务。 | 转为 VdcSyncAO event，不直接操作 offset/rate。 |
| Measure | T2/READY timestamp 和质量反馈。 | 进入质量统计和 evidence，不直接改变业务序列。 |

| 消费域 | 从 VDC 读取 | 使用限制 |
|---|---|---|
| Trigger | `vdc_time64_ns`、lock state、quality、active cal CRC。 | 只在 RUN gate 通过后生成 `FIRE_LOAD`。 |
| Loop / Angle DPLL | VDC snapshot、Compare timestamp。 | Angle DPLL 只生成 `T_fire_base`，不能写 VDC offset/rate。 |
| RefMem | VDC snapshot、quality、fault、evidence。 | 保存共同事实，不计算 DPLL。 |
| Report / Storage | VDC 版本、质量、T2 证据。 | 用于报告闭环和问题复现。 |
| SCPI / UI | snapshot 和状态摘要。 | 只能读快照或写配置/命令槽。 |

## SCPI 边界

VDC Domain 不建立裸顶级命令。产品命令树保持：

```text
CONFigure:SYNC:VDC:DPLL
SYNC:CHECk / SYNC:STARt / SYNC:STOP / SYNC:RELock / SYNC:HOLDover
READ:SYNC:STATe?
READ:SYNC:QUALity?
SYSTem:SYNC:VDC:STATus?
SYSTem:SYNC:VDC:DPLL:STATus?
SYSTem:SYNC:VDC:TDMA:PLAN?
SYSTem:SYNC:VDC:OBServer
SYSTem:SYNC:VDC:OBServer?
SYSTem:SYNC:VDC:DPLL:TUNE
SYSTem:SYNC:VDC:DPLL:COEFficient
SYSTem:SYNC:VDC:DPLL:DEFAult
```

规则：

- `CONFigure:*` 写 staging 配置。
- `SYNC:*` 发起同步动作事务。
- `READ:SYNC:*?` 给产品上位机读取同步状态。
- `SYSTem:SYNC:VDC:*` 给维护工具读取和调试底层 VDC/DPLL。
- `SYSTem:SYNC:VDC:TDMA:PLAN?` 只输出 active schedule 的窗口计划和 gate evidence，不提交 TDMA intent，也不改变 RefMem 或 DPLL 状态。
- `SYSTem:SYNC:VDC:OBServer` 只配置 VDC manager 的 SYNC_IO raw capture observer；无参数或 `0` 关闭 observer，启用态必须由维护工具显式给出 event id、tick base、sample period、window 和 frame CRC，不启动 capture、不伪造 lock evidence。
- `SYSTem:SYNC:VDC:OBServer?` 只读取 observer 证据计数和最近 gate 结果。
- 禁止新增 `VDC:*`、`DPLL:*`、`STATus:VDC?`、`STATus:DPLL?`。

## 目标代码形态

当前第一阶段代码已经有 `components/vdc_dpll_manager/` 和 `task_vdc_sync` / `task_dpll` 状态壳。产品化目标是把共同时间主域拆成独立组件：

```text
components/vdc_domain/
  inc/vdc_domain.h
  inc/vdc_clock_model.h
  inc/vdc_dpll.h
  inc/vdc_quality.h
  inc/vdc_timestamp.h
  src/vdc_domain.c
  src/vdc_clock_model.c
  src/vdc_dpll.c
  src/vdc_quality.c
  src/vdc_timestamp.c
```

过渡规则：

- `components/vdc_dpll_manager/` 可以先作为兼容 wrapper。
- `task_vdc_sync` 最终服务 `VdcSyncAO / SyncDpllFB / VdcVector`。
- `task_dpll` 最终服务 `AngleDpllFB / AnglePredictionVector`，不归 VDC offset/rate owner。
- SCPI 读取必须走 snapshot，不得直接访问内部状态字段。

## 验证门禁

VDC 主域最小验证必须覆盖：

- offset step 响应。
- rate drift 跟踪。
- jitter spike 剔除。
- HOLDOVER aging。
- RELOCK 成功和失败路径。
- calibration CRC mismatch 拒绝 LOCK。
- timestamp dictionary mismatch 拒绝样本进入 DPLL。
- node stale 禁止 RUN。
- VDC unlocked 禁止 `FIRE_LOAD`。
- LOCKED 后 T2/READY timestamp 能映射到 VDC 时间。
