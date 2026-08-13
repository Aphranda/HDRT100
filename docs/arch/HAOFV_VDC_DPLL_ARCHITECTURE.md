# HAOFV VDC 与 DPLL 核心基础架构

Status: Draft
Domain: HAOFV
Canonical: `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`, `docs/sync/SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md`, `docs/interface/DTC100_SCPI_COMMAND_PLANNING.md`
Last updated: 2026-08-13

本文档定义 VDC 与 DPLL 在 HAOFV 下的核心基础架构位置。VDC/DPLL 不是裸顶级产品命令域，也不是某个 PIO 模式；它是支撑多节点共同时间事实、同步门禁、预测分发、T2 证据和分布式 RUN 的基础设施。

## 定位

VDC 是虚拟 DC 时钟，是多块 DTC100 节点之间的共同时间认知。DPLL 是让 VDC 稳态收敛、HOLDOVER、RELOCK 和质量评估成立的算法层。

```text
Local tick
  -> calibration delay model
  -> SYNC DPLL offset/rate estimate
  -> VDC common time axis
  -> Trigger prediction and local fire
  -> T2 / READY feedback evidence
```

在 HAOFV 中，VDC/DPLL 属于核心基础架构，向上支撑 `SYNC`、`TRIGger`、`CALibration`、`MEASure`、`SYSTem:T2` 和反射内存，向下依赖 `sync_io`、RJ45/BiSS-C、PIO/DMA/IRQ 和板级时间戳能力。

## 名词边界

| 名词 | 定义 | owner |
|---|---|---|
| Local Tick | 单板本地单调计数，来自 `clk_sys` 或硬件计数源。 | board/realtime service |
| VDC | Virtual Distributed Clock，多节点共同时间轴。 | `task_vdc_sync` / `VdcSyncAO` |
| SYNC DPLL | 估计 VDC 的 offset/rate，使各节点本地 tick 映射到共同 DC。 | `task_vdc_sync` |
| Angle DPLL | 根据转台 Compare/角度脉冲预测未来 `T_fire_base`。 | `task_dpll` / LoopEngine |
| Loop Filter | DPLL 内部虚拟环路滤波器，用于稳定 offset/rate 或角度预测。 | 对应 DPLL owner |
| T2 | READY/动作完成的时间事实，用于报告、质量和补偿闭环。 | realtime capture -> storage/measure |
| e_vdc | VDC 同步残差。 | `task_vdc_sync` |
| e_pll | 角度预测残差。 | `task_dpll` |
| e_act | 动作执行残差，通常为 `T2_i - T_fire_base - Delta_t_i`。 | Trigger/Measure |

关键约束：

- `SYNC DPLL` 和 `Angle DPLL` 是两套不同环路，owner、输入、输出、质量指标和门禁不同。
- `SYNC DPLL` 输出共同时间轴 `VDC offset/rate/lock`。
- `Angle DPLL` 输出扫描预测时间 `T_fire_base`，不能参与 VDC offset/rate 收敛。
- VDC/DPLL 可以有维护和调试接口，但不建立裸 `VDC:*` 或裸 `DPLL:*` 产品顶级命令。

## HAOFV 分层

```text
SCPI / UI / System Pack
  CONFigure:SYNC:* / SYNC:* / READ:SYNC:*?
  SYSTem:SYNC:VDC:* maintenance
        |
        v
Active Object Layer
  VdcSyncAO
  CalibrationAO
  TriggerAO
  MeasureAO / DiagnosticsAO
        |
        v
Function Block Layer
  VdcSyncFB
  SyncDpllFB
  SyncQualityGateFB
  HoldoverFB
  RelockFB
        |
        v
Vector Blackboard Layer
  VdcVector
  CalibrationVector
  TriggerVector
  Statistics/T2 summary
  DistributedVectorTable
        |
        v
Hardware Service Layer
  sync_io
  RJ45/BiSS-C frame service
  timestamp service
        |
        v
Hard Realtime Side Path
  PIO / DMA / IRQ
```

分层规则：

- `CONFigure:SYNC:*` 只写 staging 配置，不直接改变已锁定的 VDC。
- `SYNC:*` 是同步动作域，负责 CHECK、START、STOP、RELOCK、HOLDOVER。
- `SYSTem:SYNC:VDC:*` 是维护/诊断入口，只暴露状态、调试参数和受权限保护的调节动作。
- `TRIGger:*` 只能消费已通过门禁的 VDC 事实，不能直接调 DPLL。
- `REALtime:*` 可以用于底层时序验证，但不能越过 `task_vdc_sync` 写 VdcVector。

## Owner 与 Vector

### VdcVector

`VdcVector` 是 VDC/DPLL 的唯一事实源。建议常驻在反射内存或系统向量表的同步槽中。

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
| `offset_tick/rate_q32/state` | `task_vdc_sync` | Trigger、Measure、Diagnostics、SCPI snapshot |
| `node_*_bitmap` | `task_vdc_sync` / refmem sync merge | System、SYNC gate、Trigger gate |
| `active_cal_crc` | `task_vdc_sync` 复制 active calibration 版本 | SYNC/CAL consistency check |
| `e_vdc` 和质量统计 | `task_vdc_sync` | READ/SYSTem/报告 |

禁止：

- SCPI handler 直接写 VdcVector。
- core1 realtime 直接写 `offset_tick/rate_q32`。
- `task_dpll` 直接写 VDC lock 状态。
- 反射内存远端节点覆盖本地 owner 字段；远端事实必须通过 merge 规则进入只读 peer slot。

## 状态机

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

状态门禁：

- `LOCKING` 期间不得执行正式 `TRIGger:STARt`。
- `LOCKED` 是正式 RUN、DEVICE/T2 校准和预测分发的必要条件。
- `HOLDOVER` 是否允许继续 RUN 由 profile 决定；默认只允许已装载 fire 完成，不允许新增 `FIRE_LOAD`。
- `RELOCKING` 成功不自动恢复 RUN，必须重新通过 `TRIGger` 业务门禁。

## 数据流

### 建立共同时间轴

```text
CONFigure:CALibration:LINK:* / CALibration:STARt
        |
        v
CALibration:ACTivate
        |
        v
CONFigure:SYNC:* staging
        |
        v
SYNC:CHECk
  - active calibration present
  - node/ring topology valid
  - link delay table complete
  - freshness/CRC/seq limits valid
        |
        v
SYNC:STARt
  - collect timestamp samples
  - reject outliers
  - update offset/rate
  - publish VdcVector
        |
        v
READ:SYNC:STATe? / READ:SYNC:QUALity?
```

### 支撑预测分发

```text
VDC LOCKED
        |
        v
DEVICE/T2 calibration -> Delta_t_i
        |
        v
Angle DPLL -> T_fire_base
        |
        v
TRIGger gate -> FIRE_LOAD
        |
        v
core1 realtime -> local_fire_i
        |
        v
PIO/DMA/IRQ execute edge
        |
        v
T2/READY capture -> e_act/e_vdc/e_pll statistics
```

## DPLL 环路滤波器

DPLL 需要虚拟环路滤波器，但它是固件内部稳定性机制，不是现场上位机的业务配置项。调试接口可以暴露参数，便于开发、产测和服务阶段调节。

建议参数：

| 参数 | 用途 | 典型约束 |
|---|---|---|
| `profile` | 选择保守/标准/快速收敛配置。 | 产品默认 `standard` |
| `kp` | 相位误差比例项。 | 调试权限 |
| `ki` | 频率/漂移积分项。 | 调试权限 |
| `max_step_ns` | 单次 offset 修正上限。 | 防止跳变 |
| `max_rate_ppm` | rate 修正上限。 | 防止过度跟踪噪声 |
| `outlier_ns` | 样本剔除阈值。 | 与链路 jitter 相关 |
| `lock_window_ns` | LOCK 判据窗口。 | 产品质量门限 |
| `holdover_ms` | 失去观测后的保持时间。 | 安全门限 |

调节规则：

- 默认产品参数由 profile 给出，上位机现场测试不需要调节。
- `SYSTem:SYNC:VDC:DPLL:*` 调试接口只允许 `SERVICE/DEBUG/FACTORY` 权限使用。
- 参数更新写 staging，必须通过 `SYNC:STOP` 或安全 `RELOCK` 流程应用。
- RUN 中禁止改变 DPLL 系数。
- 任意参数覆盖都必须写入日志和报告证据。

## 与校准域的关系

校准域给 VDC/DPLL 提供固定链路模型。校准很快，主要测算线缆、驱动、接收和设备动作 delay，但它不能直接宣称系统已同步。

| 校准内容 | 是否需要 VDC LOCKED | 用途 |
|---|---|---|
| NODE/RJ45 基础链路 delay | 否 | SYNC CHECK 和 DPLL 初始模型 |
| SMA/外部触发链路 delay | 否 | 外部触发路径补偿 |
| DEVICE/T2 动作补偿 | 是 | 预测分发中的 `Delta_t_i` |

active calibration 切换后：

- `VdcVector.active_cal_crc` 必须更新。
- 旧的 `SYNC:CHECk` 结论失效。
- VDC 若处于 `LOCKED`，应进入 `CHECKING` 或要求 `SYNC:RELock`。
- TRIGger RUN 门禁必须重新检查 active cal 与 active sync 的 CRC 一致性。

## 与 Trigger / Realtime 的关系

`TRIGger` 是产品业务动作域，消费 VDC 的锁定状态和时间事实。`REALtime` 是低层实时能力域，执行 VDC 派生出的本地时间动作。

| 方向 | 规则 |
|---|---|
| `TRIGger -> VDC` | RUN 前读取 VDC lock、epoch、quality 和 active cal CRC，决定是否允许 START。 |
| `VDC -> TRIGger` | 发布共同时间轴和质量门禁，不直接启动业务 RUN。 |
| `VDC -> REALtime` | 提供 local_fire 换算所需 offset/rate 和有效窗口。 |
| `REALtime -> VDC` | 提供时间戳样本、T2、CRC/seq/freshness 事实，不直接修改 offset/rate。 |

## SCPI 边界

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

## 与既有文档的关系

| 文档 | 关系 |
|---|---|
| `HAOFV_ARCHITECTURE.md` | 定义 HAOFV 顶层 owner、层次和 Trigger/Realtime/VDC 边界。 |
| `RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` | 定义 `task_vdc_sync`、`task_dpll`、反射内存 slot 和实现阶段。 |
| `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` | 同步域落地方案，描述环路、帧、PIO 和多板原型细节。 |
| `DTC100_SCPI_COMMAND_PLANNING.md` | 对外 SCPI 命令树和 VDC/DPLL 指令边界。 |

## 待办

- [ ] 将 `VdcVector` 字段冻结到反射内存规划文档。
- [ ] 将 `SYNC DPLL` 与 `Angle DPLL` 的状态、参数和质量指标拆成两个 owner 表。
- [ ] 将 `SYSTem:SYNC:VDC:DPLL:*` 调试接口映射到权限 profile。
- [ ] 在 `RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` 中补充本架构文档引用。
- [ ] 为 VDC LOCK/HOLDOVER/RELOCK 建立闭环验证脚本和报告字段。
