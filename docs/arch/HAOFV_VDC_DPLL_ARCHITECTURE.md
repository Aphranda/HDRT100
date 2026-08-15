# HAOFV VDC 与 DPLL 核心基础架构

Status: Draft
Domain: HAOFV
Canonical: `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/interface/SCPI_COMMAND_PLAN.md`
Last updated: 2026-08-13

本文档定义 VDC 与 DPLL 在 HAOFV 下的核心基础架构。HAOFV 是架构，基础件是对架构的实现；因此本文不把“架构”和“基础件”拆成两个孤立部分，而是按 HAOFV 的运行链条融合描述：每一条链都从 AO/FB/Vector 的 owner 开始，向下落到 Service、PIO/IRQ、compact timestamp、loop filter、时钟约束和动态性能指标。

VDC/DPLL 不是裸顶级产品命令域，也不是某个 PIO 模式。它是支撑多节点共同时间事实、同步门禁、预测分发、T2 证据和分布式 RUN 的基础设施。

## 0. 核心口径

- HAOFV 是主线：AO 管入口和生命周期，FB 管状态迁移和环路逻辑，Vector 管事实和唯一 writer，Service/PIO 管受限底层执行。
- 基础件只实现 HAOFV 节点，不自行形成业务状态、对外指令或运行主线。
- VDC 是共同时间坐标系，DPLL 是形成和维护 VDC 稳态的 FB 逻辑。
- Timestamp sample 是 DPLL 的原始观测事实；VDC 是 timestamp、校准 delay、DPLL 滤波和质量门限共同形成的结果。
- `SYNC DPLL` 与 `Angle DPLL` 是两条不同 HAOFV 链：前者生成 VDC offset/rate/lock，后者生成扫描预测 `T_fire_base`。
- 高实时链路只传 compact timestamp；完整语义由接收端依据 timestamp dictionary、epoch、wrap tracker 和 active profile 展开。

本文的阅读方式是按链路阅读，而不是按“架构一章、基础件一章”拆开阅读：

| 链路 | 从 HAOFV 到基础件 |
|---|---|
| Timestamp 采集链 | timestamp service / ValidationFB / TimestampRing -> compact timestamp / local tick / dictionary |
| SYNC DPLL 锁定链 | VdcSyncAO / SyncDpllFB / VdcVector -> timestamp sample / loop filter / offset-rate |
| HOLDOVER/RELOCK 链 | HoldoverFB / RelockFB / VdcVector -> freshness timer / age gate / quality window |
| 校准链 | CalibrationAO / CalibrationFB / CalibrationVector -> timestamp pair / delay model |
| Trigger 预测分发链 | TriggerAO / TriggerFB / core1 realtime -> VDC snapshot / local_fire / T2 timestamp |

## 1. 总装配链

VDC/DPLL 的整体装配按 HAOFV 走，而不是按算法模块自由调用。

```text
SCPI / UI / System Pack
        |
        v
Active Object Layer
  VdcSyncAO / CalibrationAO / TriggerAO / MeasureAO
        |
        v
Function Block Layer
  TimestampValidationFB / SyncDpllFB / HoldoverFB / RelockFB
  CalibrationFB / AngleDpllFB / TriggerFB
        |
        v
Vector Blackboard Layer
  VdcVector / CalibrationVector / TriggerVector
  TimestampRing / T2Summary / DistributedVectorTable
        |
        v
Hardware Service Layer
  timestamp service / sync_io / RJ45-BiSS frame service
        |
        v
Hard Realtime Side Path
  local_tick / compact timestamp / PIO capture / DMA / IRQ
```

| HAOFV 节点 | 基础实现件 | 输入 | 输出 | 禁止 |
|---|---|---|---|---|
| `VdcSyncAO` | event queue、profile staging、resource gate | `SYNC:*` event、active config | VDC lifecycle、FB dispatch | 直接采边沿或绕过 FB 写 offset/rate |
| `TimestampValidationFB` | dictionary、wrap tracker、age/CRC/seq gate | compact timestamp | valid/invalid sample | 发布产品状态 |
| `SyncDpllFB` | loop filter、outlier gate、calibrated sample | SYNC timestamp | offset/rate/e_vdc | 消费 Angle DPLL 输出 |
| `VdcVector` | atomic commit、update_seq、CRC | FB result | VDC fact snapshot | 被 SCPI、PIO、storage 直接写 |
| `TriggerFB` | RUN gate、FIRE_LOAD builder | VDC snapshot、`T_fire_base` | local_fire request | 修改 VDC owner 字段 |
| `timestamp service` | local tick、compact decoder、ring merge | PIO/IRQ capture | timestamp sample | 发布业务语义 |
| PIO/DMA/IRQ | edge capture、minimal flags | physical edge | compact timestamp | 执行 DPLL 或访问 USB/SD/SCPI |

## 2. Timestamp 采集链

这条链把物理边沿变成可被 HAOFV 消费的时间事实。

```text
physical edge
  -> PIO/IRQ capture
  -> local_tick latch
  -> compact timestamp
  -> timestamp service
  -> dictionary decode + wrap extend
  -> TimestampValidationFB
  -> TimestampRing / owner FB
```

### 2.1 末端基础件

`local_tick` 是每块板的本地单调时间事实。

| 属性 | 要求 |
|---|---|
| 单调性 | 必须单调递增，允许固定宽度回绕，但不允许软件重置造成局部倒退。 |
| 来源 | `clk_sys`、PIO capture counter、硬件 timer 或统一 timestamp service。 |
| 粒度 | 当前 RP2350 原型可按 250 MHz 计，1 tick = 4 ns。 |
| owner | board/realtime service。 |
| 禁止 | 上位机、SCPI、Storage、Report 不能生成或修正 local tick。 |

默认线上 compact timestamp 为 8 字节：

```c
typedef struct {
    uint16_t seq_delta;
    uint8_t  source_event;
    uint8_t  flags;
    uint32_t tick_l32;
} ts8_compact_t;
```

| 字段 | 位宽 | 含义 |
|---|---:|---|
| `seq_delta` | 16 | 相对当前 epoch/run 的序号低 16 位或差分序号。 |
| `source_event` | 8 | 高 4 位为 `source_id`，低 4 位为 `event_id`。 |
| `flags` | 8 | valid、late、edge polarity、overflow、holdover_mapped、crc/seq 异常摘要。 |
| `tick_l32` | 32 | local tick 低 32 位；接收端用 wrap tracker 扩展为 64 位。 |

扩展形式：

```c
typedef struct {
    uint16_t seq_delta;
    uint8_t  source_event;
    uint8_t  flags;
    uint32_t tick_l32;
    uint16_t age_us;
    uint16_t crc16;
} ts12_compact_t;

typedef struct {
    uint16_t seq_delta;
    uint8_t  source_event;
    uint8_t  flags;
    uint32_t tick_l32;
    uint32_t tick_h_or_vdc_l32;
    uint16_t age_us;
    uint16_t crc16;
} ts16_compact_t;
```

| 形式 | 字节 | 使用场景 |
|---|---:|---|
| `ts8_compact_t` | 8 | 高频实时边沿，默认优先。 |
| `ts12_compact_t` | 12 | 需要 age 或 CRC 的同步帧、跨板维护帧。 |
| `ts16_compact_t` | 16 | 需要携带 tick 高位、短 VDC 映射或更长 trace 的低频证据帧。 |

### 2.2 字典展开

大容量语义定义不在线上传输，而由配置、System Pack 或固件 profile 预设。线上只传 `source_event` 索引。

```c
typedef struct {
    uint8_t  source_id;
    uint8_t  node_id;
    uint8_t  port_id;
    uint8_t  signal_role;
    uint8_t  event_id;
    uint8_t  edge_policy;
    uint16_t default_quality_mask;
} timestamp_dictionary_entry_t;
```

展开后的样本供 FB 和 Vector owner 使用：

```c
typedef struct {
    uint32_t epoch;
    uint32_t sequence_id;
    uint8_t  source_node;
    uint8_t  source_port;
    uint8_t  event_type;
    uint8_t  quality_flags;
    uint64_t local_tick;
    uint64_t mapped_vdc_tick;
    uint32_t frame_crc;
    uint32_t receive_age_us;
} timestamp_sample_t;
```

展开流程：

```text
ts8/ts12/ts16
  -> lookup source_event in timestamp dictionary
  -> extend seq_delta by epoch/run context
  -> extend tick_l32 by local wrap tracker
  -> attach node/port/signal role
  -> compute receive_age_us if not carried
  -> produce timestamp_sample_t
```

质量规则：

- `local_tick` 是原始事实，必须保留。
- `mapped_vdc_tick` 只有在 VDC `LOCKED/HOLDOVER` 且映射有效时才可信。
- `sequence_id` 用于把 SYNC、FIRE、READY/T2 和报告证据归入同一轮。
- `quality_flags` 至少区分 valid、late、stale、crc_error、seq_error、outlier、holdover_mapped。
- 字典版本不匹配时，样本只能作为 invalid evidence 存档，不能进入 DPLL。

## 3. SYNC DPLL 锁定链

这条链把 SYNC timestamp 变成 VDC 共同时间轴。

```text
SYNC:STARt event
  -> VdcSyncAO
  -> TimestampValidationFB
  -> apply active calibration delay
  -> SyncDpllFB
  -> SyncQualityGateFB
  -> VdcSyncAO commit
  -> VdcVector offset/rate/lock
```

### 3.1 输入事实

| 输入 | 来源 | 用途 |
|---|---|---|
| SYNC timestamp sample | timestamp service | phase detector 输入 |
| NODE/RJ45 delay | active CalibrationVector | 修正链路传播 delay |
| dictionary/profile CRC | System Pack / active profile | 样本解释一致性 |
| freshness / CRC / seq | timestamp validation | 样本门禁和 fault 归因 |

### 3.2 VDC 映射

每个节点维护从 local tick 到 VDC 的映射：

```text
vdc_tick = local_tick * rate_q32 + offset_tick
```

- `offset_tick`：本地时间到 VDC 的相位偏移。
- `rate_q32`：本地时钟相对 VDC 的频率修正。
- `rate_q32 = 1.0` 表示无频率修正。

offset/rate 的唯一 writer 是 `VdcSyncAO` 对 `VdcVector` 的原子提交；core1 只能读取一致 snapshot。

### 3.3 环路逻辑

基础离散环路：

```text
e_phase[k] = corrected_sample[k] - predicted_vdc[k]
offset[k+1] = offset[k] + Kp * e_phase[k] + Ki * sum(e_phase)
rate[k+1] = rate[k] + Ki_rate * e_phase[k]
```

传递函数视角：

```text
phase_detector -> loop_filter(z) -> numerically controlled time mapping
F(z) = Kp + Ki / (1 - z^-1)
```

| 参数 | 作用 | 过小 | 过大 |
|---|---|---|---|
| `Kp` | 相位误差快速修正 | 收敛慢 | 抖动放大、易振荡 |
| `Ki` | 长期频偏修正 | rate 漂移残留 | 积分 windup、超调 |
| `max_step_ns` | 限制 offset 跳变 | 修正过慢 | 时间轴不连续 |
| `max_rate_ppm` | 限制频率修正 | holdover 前误差变大 | 跟踪噪声 |
| `outlier_window_ns` | 样本剔除 | 错误样本进入环路 | 好样本被误丢 |

## 4. HOLDOVER / RELOCK 链

这条链处理观测丢失后的时间轴保持和恢复。

```text
timestamp freshness timeout
  -> HoldoverFB
  -> VdcVector HOLDOVER
  -> quality downgrade
  -> Trigger gate blocks new FIRE_LOAD
  -> new valid sample window
  -> RelockFB
  -> VdcVector LOCKED or FAULT
```

| 控制项 | HAOFV owner | 作用 |
|---|---|---|
| `sync_period_us` | `VdcSyncAO` | SYNC timestamp 采样周期，决定 DPLL 更新速率。 |
| `frame_guard_us` | timestamp/ring service | 帧间隔保护，防止环路自激或前后帧混叠。 |
| `capture_window_us` | core1 realtime | READY/T2 或 SYNC 边沿捕获窗口，超窗标记 late/missing。 |
| `sample_age_limit_us` | `TimestampValidationFB` | timestamp 从采集到被消费的最大允许年龄。 |
| `freshness_timeout_ms` | `HoldoverFB` | 节点失去更新多久后变成 stale/missing。 |
| `lock_window_ns` | `SyncQualityGateFB` | 连续样本残差进入窗口后允许 LOCKED。 |
| `holdover_enter_ms` | `HoldoverFB` | 观测丢失多久进入 HOLDOVER。 |
| `holdover_max_ms` | `HoldoverFB` | HOLDOVER 最长持续时间，超时进入 FAULT。 |
| `relock_settle_ms` | `RelockFB` | RELOCK 后重新稳定所需时间。 |

RELOCK 成功只恢复 VDC `LOCKED`，不自动恢复 TRIG RUN。

## 5. 校准链

校准链为 VDC/DPLL 提供 delay model，但它不等于同步锁定。

```text
CALibration:STARt
  -> CalibrationAO
  -> timestamp capture pair
  -> CalibrationFB delay calculation
  -> CalibrationVector staging
  -> CALibration:ACTivate
  -> active calibration CRC
  -> invalidate previous SYNC:CHECk
```

```text
corrected_sample = timestamp_sample - link_delay - device_delay
```

| delay 类型 | 来源 | 用途 |
|---|---|---|
| NODE/RJ45 基础链路 delay | `CALibration:STARt` 快速事务 | SYNC CHECK 和 DPLL 初始模型 |
| SMA/外部触发链路 delay | SMA 回路或外部触发测量 | 外部输入/输出补偿 |
| DEVICE/T2 动作补偿 | VDC LOCKED 后测得 | 预测分发中的 `Delta_t_i` |

active calibration 切换后，旧的 SYNC CHECK 结论失效，VDC 应进入 `CHECKING` 或要求 `SYNC:RELock`。

## 6. Trigger 预测分发链

这条链把 VDC 共同时间事实转成产品业务动作。

```text
VdcVector LOCKED snapshot
  -> TriggerAO RUN gate
  -> AngleDpllFB T_fire_base
  -> TriggerFB FIRE_LOAD
  -> local_fire_i = map(T_fire_base, offset/rate, Delta_t_i)
  -> core1 realtime
  -> PIO/DMA/IRQ execute edge
  -> READY/T2 compact timestamp
  -> MeasureAO / StorageAO evidence
```

| 节点 | 输入 | 输出 | 禁止 |
|---|---|---|---|
| `AngleDpllFB` | ANGLE/COMPARE timestamp、扫描配置 | `T_fire_base`、`e_pll` | 写 VDC offset/rate/lock |
| `TriggerFB` | VDC lock、active sequence、`T_fire_base` | FIRE_LOAD | 绕过 VDC gate |
| core1 realtime | local_fire request | deterministic edge、T2 timestamp | 执行 DPLL 或访问 SCPI/SD/USB |
| `MeasureAO` | T2/READY sample | `e_act`、报告证据 | 修改 VDC state |

## 7. 状态机和 Vector

建议 VDC/SYNC DPLL 使用以下状态：

| 状态 | 含义 | 允许出口 |
|---|---|---|
| `OFF` | 未启用，同步资源释放。 | `CHECKING` |
| `FREE_RUN` | 本地 tick 可用，但没有共同时间保证。 | `CHECKING`, `OFF` |
| `CHECKING` | 检查 active calibration、ring、节点 freshness 和门限。 | `LOCKING`, `FAULT`, `OFF` |
| `LOCKING` | DPLL 收敛 offset/rate，样本可被剔除但不允许正式 RUN。 | `LOCKED`, `HOLDOVER`, `FAULT`, `OFF` |
| `LOCKED` | VDC 可作为正式预测分发和 DEVICE/T2 校准基准。 | `HOLDOVER`, `RELOCKING`, `FAULT`, `OFF` |
| `HOLDOVER` | 短时失去同步观测，继续使用冻结/降权参数。 | `RELOCKING`, `FAULT`, `OFF` |
| `RELOCKING` | 在不重启全系统的情况下重新收敛。 | `LOCKED`, `FAULT`, `OFF` |
| `FAULT` | 同步质量、拓扑、CRC、seq 或节点新鲜度失败。 | `CHECKING`, `OFF` |

`VdcVector` 是 VDC/DPLL 的唯一事实源。

```c
typedef struct {
    uint32_t epoch;
    uint32_t sync_id;
    uint32_t sync_crc;
    uint32_t state;
    int64_t  offset_tick;
    int64_t  rate_q32;
    int32_t  e_vdc_ns;
    uint32_t lock_quality_ppm;
    uint32_t holdover_age_ms;
    uint32_t node_valid_bitmap;
    uint32_t node_stale_bitmap;
    uint32_t crc_error_count;
    uint32_t seq_error_count;
    uint32_t update_seq;
    uint32_t active_cal_crc;
} vdc_vector_t;
```

写权限：

| 字段 | 唯一 writer | 读者 |
|---|---|---|
| `offset_tick/rate_q32/state` | `VdcSyncAO` commit | Trigger、Measure、Diagnostics、SCPI snapshot |
| `node_*_bitmap` | `VdcSyncAO` / refmem sync merge | System、SYNC gate、Trigger gate |
| `active_cal_crc` | `VdcSyncAO` 复制 active calibration 版本 | SYNC/CAL consistency check |
| `e_vdc` 和质量统计 | `VdcSyncAO` commit | READ/SYSTem/报告 |

## 8. 时钟约束和动态性能

时钟约束：

| 约束 | 要求 |
|---|---|
| tick source | 必须单调、稳定、可由 PIO/IRQ 采样。 |
| tick width | 线上可传 32 位低位，本地必须扩展为 64 位事实。 |
| wrap tracker | 每个 source_id 或 capture domain 必须维护 wrap 状态。 |
| read atomicity | core1/core0 读写 tick、offset/rate 必须避免撕裂。 |
| clock change | RUN、LOCKED、HOLDOVER 中禁止改变 `clk_sys` 或 timestamp 分频。 |

动态性能指标：

| 指标 | 含义 | 来源 |
|---|---|---|
| `lock_time_ms` | 从 `SYNC:STARt` 到 `LOCKED` 的时间。 | VdcSyncFB |
| `e_vdc_rms_ns` | VDC 同步残差 RMS。 | SYNC timestamp |
| `e_vdc_pk_ns` | VDC 同步残差峰值。 | SYNC timestamp |
| `jitter_ns` | 样本短期抖动。 | timestamp statistics |
| `wander_ppm` | 长期频率漂移估计。 | rate history |
| `outlier_ratio` | 被剔除样本比例。 | SyncDpllFB |
| `holdover_drift_ns` | HOLDOVER 期间误差增长。 | holdover model / T2 |
| `relock_time_ms` | RELOCK 回到 LOCKED 的时间。 | RelockFB |
| `late_count` | FIRE/T2 late 计数。 | core1 realtime |
| `seq_crc_error_count` | 序号和 CRC 错误。 | refmem/ring service |

VDC `LOCKED` 的判据应至少包含：

- 连续 N 个有效样本进入 `lock_window_ns`。
- `e_vdc_rms_ns` 小于 profile 门限。
- node freshness 全部满足 required node。
- CRC/seq error 在窗口内低于门限。
- active calibration CRC 与 active sync CRC 一致。

## 9. SCPI 边界

VDC/DPLL 不建立裸顶级命令。产品命令树保持：

```text
CONFigure:SYNC:VDC:DPLL
SYNC:CHECk / SYNC:STARt / SYNC:STOP / SYNC:RELock / SYNC:HOLDover
READ:SYNC:STATe?
READ:SYNC:QUALity?
SYSTem:SYNC:VDC:STATus?
SYSTem:SYNC:VDC:DPLL:STATus?
SYSTem:SYNC:VDC:DPLL:TUNE
SYSTem:SYNC:VDC:DPLL:COEFficient
SYSTem:SYNC:VDC:DPLL:DEFAult
```

规则：

- `CONFigure:SYNC:VDC:DPLL` 写同步配置 staging。
- `SYNC:*` 执行动作事务。
- `READ:SYNC:*?` 给现场上位机读取产品同步状态。
- `SYSTem:SYNC:VDC:*` 给维护工具读取和调试底层 VDC/DPLL。
- 禁止新增 `VDC:*`、`DPLL:*`、`STATus:VDC?`、`STATus:DPLL?`。

## 10. 与既有文档的关系

| 文档 | 关系 |
|---|---|
| `HAOFV_ARCHITECTURE.md` | 定义 HAOFV 顶层 owner、层次和 Trigger/Realtime/VDC 边界。 |
| `RTOS_HAOFV_ARCHITECTURE.md` / `RTOS_HAOFV_TODO.md` | 定义 `task_vdc_sync`、`task_dpll`、反射内存 slot 和实现阶段。 |
| `SYNC_IO_ARCHITECTURE.md` | 同步域落地方案，描述 IO owner、PIO/DMA/IRQ、CAL_RING、预约触发和多板原型边界。 |
| `SCPI_COMMAND_PLAN.md` | 对外 SCPI 命令树和 VDC/DPLL 指令边界。 |

## 11. 待办

- [ ] 补齐四板环路 VDC 建立链：A0/A1/A2/A3 角色、A0 ring origin、A1/A2/A3 follower 映射、A0->A1->A2->A3->A0 环路方向。
- [ ] 定义四板 VDC 建立流程：NodeRoleMap -> active calibration -> ring heartbeat/freshness -> `SYNC:CHECk` -> `SYNC:STARt` -> compact timestamp exchange -> per-node offset/rate -> global LOCK。
- [ ] 定义环路帧与 timestamp 关系：SYNC、CAL、REFMEM_DELTA、FIRE_LOAD、DONE、MEAS_DONE、FAULT 分别使用 `ts8/ts12/ts16` 的规则。
- [ ] 定义 `NodeVdcSlot[A0..A3]`：每节点 offset/rate/lock/e_vdc/freshness/seq/crc、local slot 与 peer slot writer 规则。
- [ ] 定义环路 delay 模型：A0->A1、A1->A2、A2->A3、A3->A0 分段 delay、round-trip delay、one-way delay 与 lumped delay 的边界。
- [ ] 定义四板 global LOCK/HOLDOVER/FAULT 判据：required node、freshness、crc/seq、e_vdc、active calibration CRC 和 active sync CRC 的一致性门禁。
- [ ] 将 compact timestamp、timestamp dictionary 和 expanded sample 冻结到反射内存规划文档。
- [ ] 将 `SYNC DPLL` 与 `Angle DPLL` 的状态、参数和质量指标拆成两个 owner 表。
- [ ] 将 `SYSTem:SYNC:VDC:DPLL:*` 调试接口映射到权限 profile。
- [ ] 为 VDC LOCK/HOLDOVER/RELOCK 建立闭环验证脚本和报告字段。
- [ ] 增加动态性能评估记录模板，覆盖 lock_time、e_vdc、jitter、holdover drift、relock_time 和 late_count。
