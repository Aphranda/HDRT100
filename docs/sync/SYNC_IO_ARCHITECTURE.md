# SYNC_IO / Realtime IO 架构

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/SYNC_IO_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/sync/SYNC_IO_TODO.md`, `docs/sync/SYNC_IO_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`, `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`
Last updated: 2026-08-15

本文档定义 HAOFV 下 `sync_io`、底层实时 IO、PIO/DMA/IRQ mode driver 和产品同步动作之间的边界。它不是 VDC/DPLL 的 canonical，也不是产品 `TRIGger` 业务动作文档；它回答的问题是：

```text
本机底层 IO 能力如何被声明、仲裁、装载、执行、观测和验证。
```

## 架构定位

`SYNC_IO` 是 HAOFV 的 Hardware Service / Realtime IO 基础件。它位于产品动作域和硬实时执行层之间：

```text
SCPI / UI / System Pack
        ↓
TRIGger / SYNC / CAL product action domain
        ↓
TriggerAO / SyncAO / CalibrationAO / owner FB
        ↓
REALtime maintenance and capability domain
        ↓
sync_io Hardware Service
        ↓
mode drivers / resource map
        ↓
PIO / DMA / IRQ
```

一句话规则：

```text
产品域表达意图，AO/FB 管状态和资源，sync_io 装载底层能力，PIO/DMA/IRQ 输出硬实时边沿。
```

## HAOFV 边界

| 层级 | owner | 允许做什么 | 禁止做什么 |
|---|---|---|---|
| `TRIGger:*` | TriggerAO / TriggerFB | 产品 RUN、START/STOP、active sequence、业务门禁。 | 直接改 PIO/DMA/IRQ 或任意 GPIO。 |
| `SYNC:*` | VdcSyncAO / SyncDpllFB | 同步动作、锁定、HOLDOVER/RELOCK、同步质量。 | 直接承担 VDC canonical 或裸写共同时间事实。 |
| `REALtime:*` | realtime maintenance owner | 底层 IO、SEQ/ENC/PCNT/BISS、PIO/DMA/IRQ 的维护、验证和状态查询。 | 改变产品 RUN 业务状态。 |
| `sync_io` | SyncIO owner | 初始化 PIO 程序、装载 mode、管理 GPIO/PIO/DMA/IRQ 原语和 runtime snapshot。 | 解析产品 SCPI 业务语义。 |
| mode driver | mode owner / TriggerFB 持有资源 | validate/arm/disarm/is_running、低层 runtime 采样。 | 重复申请资源或绕过 TriggerFB owner。 |
| PIO/DMA/IRQ | hardware execution | 捕获、倒计时、输出边沿、最小事实回写。 | 执行业务状态机、DPLL、FatFs、USB、日志格式化。 |

## 文件标准

`docs/sync` 后续采用三分结构：

| 文件 | 作用 |
|---|---|
| `SYNC_IO_ARCHITECTURE.md` | 架构和边界，说明 owner、层级、资源、mode、snapshot、HAOFV 约束。 |
| `SYNC_IO_TODO.md` | 当前未完成事项，按优先级维护，不记录流水账。 |
| `SYNC_IO_TASK_PROGRESS.md` | 已完成闭环、验证命令、风险和下一步。 |

旧 `SYNC_IO_RESOURCE_PLAN.md`、`SYNC_IO_REFACTOR_PLAN.md`、`SYNC_IO_ARCH_REVIEW_TODO.md` 和 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 的有效内容已经并入本文和 `SYNC_IO_TODO.md`；后续不再作为 active 文档保留。

## 核心对象

| 对象 | 当前代码入口 | 架构职责 |
|---|---|---|
| hardware profile | `components/sync_io/inc/sync_io_hw_profile.h` | 固定语义 IO、合法 pin group、AUX 方向、编译期断言。 |
| sync_io core | `components/sync_io/src/sync_io.c` | PIO 程序加载、公共 pulse/capture/clock/AUX 原语、trace helper、共享 IRQ 分发。 |
| mode table | `components/sync_io/inc/sync_io_mode.h` | mode id、资源、PIO/SM/DMA/IRQ 元数据、validate/arm/disarm/is_running。 |
| SEQ_STEP mode | `sync_io_mode_seq_step.c` | 外部触发沿驱动序列输出，PIO/DMA 自主步进。 |
| ENC_COUNT mode | `sync_io_mode_enc_count.c` | 编码器 A/B/Z 计数，目标计数触发输出。 |
| BISS_TAP mode | `sync_io_mode_biss_tap.c` | BiSS-C 调试/通信准备阶段的 TAP、透传、采样。 |
| resource map | `components/sync_trigger/src/trigger_resource_map.c` | 从 mode table 派生 TriggerFB 的资源仲裁 mask。 |
| TriggerFB | `components/sync_trigger/src/trigger_fb.c` | ECC 状态、资源 owner、mode arm/disarm 编排和错误归因。 |

## 语义 IO

应用层、SCPI、UI 和 TriggerVector 只能使用语义 IO，不应直接暴露任意 GPIO。

| 语义通道 | 当前默认物理通道 | 方向 | 规则 |
|---|---|---|---|
| `TRIG_IN` | 主输入 IN0 | 输入 | 默认外部触发、计数或事件输入。 |
| `GATE_IN` / `RJ45_TRIG_IN` | 主输入 IN3 | 输入 | 硬件定义是 RJ45 兼容输入；gate 是 mode 解释。 |
| `ENC_A/B/Z` | 主输入 IN0/IN1/IN2 | 输入 | `ENC_COUNT` 固定三线输入，不占用 IN3。 |
| `TRIG_OUT` | 主输出 OUT0 | 输出 | 默认确定性动作输出。 |
| `PULSE_OUT` | 主输出 OUT1 | 输出 | 第二路脉冲或 burst 输出。 |
| `MODE_OUT2` | 主输出 OUT2 | 输出 | 模式本地输出或 `SEQ_STEP` bit2。 |
| `RJ45_TRIG_OUT` | 主输出 OUT3 | 输出 | RJ45 兼容输出；历史 `MARK:*` 只是兼容入口。 |
| `ARM_IN` | AUX0 | 输入 | 产品级 ARM 资格/请求，仍需接入运行逻辑。 |
| `EXT_CLK_IN` | AUX1 | 输入 | 外部参考或采样时钟，仍需接入运行逻辑。 |
| `SYNC_CLK_OUT` | AUX2 | 输出 | 框架同步时钟输出，已迁移到 AUX2。 |
| `AUX3_TX` / `BISS_DATA_OUT` | AUX3 | 输出 | 固定辅助输出或 BiSS persona 输出。 |

具体连接器、电气隔离和产品 pinout 以 `docs/hardware/` 为准；本文冻结固件侧语义 IO、PIO/DMA/IRQ owner 和 mode 资源边界。

## PIO / DMA 资源基线

RP2350 的 3 个 PIO block 都预留给同步触发和底层 realtime IO。状态 LED、LCD、USB、SD、日志和 UI 不得占用同步触发 PIO state machine。

| PIO block | 角色 | 规则 |
|---|---|---|
| `pio0` | 高速输入捕获、触发资格判定、RJ45 输入采样。 | 输入采样、timestamp/capture 和 gate/inhibit 相关逻辑优先放在此处。 |
| `pio1` | 确定性输出、序列输出、即时/预约脉冲。 | 输出时序与输入捕获隔离；mode 间必须经资源表互斥。 |
| `pio2` | AUX 功能、协议辅助、SYNC_CLK_OUT、BiSS persona、后续 CAL_RING。 | 跨模式框架功能和协议 persona 互斥，不扰动主触发口。 |

当前固件资源语义如下：

| 资源 | 当前 owner / mode | 约束 |
|---|---|---|
| `pio0/sm0` | capture primitive | 主输入组采样。 |
| `pio0/sm2` | RJ45 input / gate interpretation | 硬件语义为 `RJ45_TRIG_IN`，gate 只是 mode 解释。 |
| `pio1/sm0` | `SEQ_STEP` / `ENC_COUNT` / main output primitive | 当前共享 SM，运行互斥由 TriggerFB/resource owner 保证。 |
| `pio1/sm2` | `PULSE_OUT` primitive | 第二路 pulse 输出，不能与占用主输出总线的 mode 冲突。 |
| `pio1/sm3` | `RJ45_TRIG_OUT` primitive | 历史 `MARK:*` 只是兼容入口。 |
| `pio2/sm0..sm3` | AUX / BiSS / SYNC_CLK_OUT / future CAL_RING | AUX0/1 固定输入，AUX2/3 固定输出；persona 切换必须互斥。 |
| `DMA0` | `SEQ_STEP` | 与 `ENC_COUNT` 共享 `DMA_IRQ_0`，不可并发。 |
| `DMA1` | `ENC_COUNT` | 与 `SEQ_STEP` 共享 IRQ，ISR 入口已有互斥断言。 |

任何新增 mode 必须在 `sync_io_mode_ops_t.hw` 中显式声明 PIO block、SM、DMA channel 和 IRQ，不允许只靠代码注释表达资源占用。

## Mode 资源互斥

| mode / primitive | 输入占用 | 输出占用 | PIO/DMA | 当前状态 |
|---|---|---|---|---|
| `SEQ_STEP` | `TRIG_IN`，可选 `GATE_IN` | OUT0..OUT3 序列总线 | `pio1/sm0 + DMA0 + DMA_IRQ_0` | 已实现。 |
| `ENC_COUNT` | IN0/IN1/IN2 = A/B/Z | OUT0 目标触发 | `pio1/sm0 + DMA1 + DMA_IRQ_0` | 已实现。 |
| `BISS_TAP` | AUX0/AUX1 | AUX2/AUX3 | `pio2` 多 SM | 已实现调试/通信准备能力。 |
| `SYNC_CLK_OUT` | 无 | AUX2 | `pio2/sm2` | 已迁移到 AUX2。 |
| model scheduled pulse | 由 NodeLoad/RealtimeCapability 决定 | 调试 overlay 或产品 profile 输出 | 待定 PIO/DMA primitive | P0 待办。 |
| RefMem minimal transport | 由 adapter profile 决定 | 由 adapter profile 决定 | 待定 PIO/PIO-like transport | P1 待办。 |
| CAL_RING | AUX0 输入 | AUX3 输出 | `pio2/sm0/sm3` 候选 | 后续同步链路原型。 |

冲突处理：

- mode arm 前由 TriggerFB 或对应 owner 持有资源。
- 冲突返回稳定错误码和 snapshot，不静默覆盖已有 mode。
- armed 期间禁止修改影响 PIO/DMA/IRQ 的配置字段。
- fault/reset/stop 必须走统一 release helper，恢复输出安全态。

## Mode Driver 契约

每个 mode 必须通过表驱动入口暴露：

| 字段/函数 | 规则 |
|---|---|
| `id/name` | 稳定 mode 标识，未实现 mode 返回 NULL ops。 |
| `resources` | 粗粒度资源需求，供 TriggerFB/resource map 仲裁。 |
| `hw` | PIO block、SM、DMA channel、IRQ 元数据，必须显式记录共享关系。 |
| `validate(config)` | 只做字段范围、profile、方向、mode 能力检查。 |
| `arm(config)` | 在上层 owner 已持有资源后装载 PIO/DMA/IRQ。 |
| `disarm()` | 停止硬件路径，恢复安全态，释放 mode 私有状态。 |
| `is_running()` | 只读运行态，不阻塞、不触发现场动作。 |

资源 owner 边界：

- P0/P1 当前由 TriggerFB 统一 acquire/release。
- 裸 `sync_io_*_arm()` 不得重复 acquire，避免同 owner 自冲突。
- 后续如改为 `sync_io_mode_arm_with_owner()`，必须整体迁移 owner 边界，不能两层同时仲裁。

## Snapshot 与观测

所有外部查询必须读取本地 snapshot，不得临时跨板阻塞查询或临时驱动 IO。

| 数据 | Snapshot 规则 |
|---|---|
| mode running | `is_running()` + runtime counters。 |
| PIO/DMA 状态 | FIFO、transfer_count、restart/rollover、irq enabled。 |
| 资源占用 | Resource Arbiter snapshot。 |
| 低频生命周期 | trace event，记录 arm/disarm/fault/resource conflict。 |
| 高频边沿 | PIO/DMA/IRQ 计数器或 ring，不输出文本日志。 |
| 分布式事实 | 由 RefMem snapshot/quality/evidence 承接，不由 `sync_io` 直接跨节点查询。 |

## 双核与 Flash 安全

RTOS + 双核 AMP 下：

- core1 是实时 owner，负责 TriggerAO/TriggerFB 快路径、PIO 装载和快速 runtime 采样。
- core0 只能投递命令、提交配置意图或读取 snapshot。
- 共享字段必须使用唯一 writer、sequence/seqlock、atomic 或 DMB。
- Flash erase/program 前必须完成 core1 park/lockout；`sync_io` mode 不得在 lockout 期间从 XIP 路径执行关键实时入口。

## RefMem / VDC 边界

`sync_io` 不直接拥有分布式共同事实和共同时间：

| 基础主域 | 与 IO 的关系 |
|---|---|
| RefMem Domain | 记录 IO fact、quality、stale、ACK/NACK 和 evidence；不承载边沿或大文件。 |
| VDC Domain | 发布共同时间 offset/rate/lock/quality；IO 只提供 timestamp 样本或按 VDC 结果装载本地输出。 |
| Trigger Domain | 把产品动作意图转换为 mode 装载和 `FIRE_LOAD`。 |

`sync_io` 的输出只能是本机硬件执行和本地 runtime snapshot；跨节点同步必须经 RefMem Sync / VDC / CommandSlot。

## CAL_RING / 分布式同步链路边界

旧分布式 DPLL 文档中的有效结论收敛为以下边界：

- VDC 是共同时间 owner；SYNC_IO 只提供本地 timestamp 样本、转发边沿、预约输出和质量计数。
- 多板之间不依赖通信包到达时刻直接触发；上层应提前形成 `FIRE_LOAD` 或等价预约计划。
- PIO 负责短窗口相对计时、边沿捕获、固定延迟转发和到点输出，不直接维护完整 64-bit DC counter。
- CPU / VDC owner 负责 `local_tick -> vdc_time` 映射、offset/rate、HOLDOVER/RELOCK 和质量门禁。
- CAL_RING 或 RefMem transport 是 adapter / mode 能力，不改变 RefMem/VDC/Trigger 的 owner 边界。

推荐同步原型链路：

```text
AUX0/CAL_IN capture
  -> PIO timestamp or relative counter
  -> Sync/VDC sample
  -> RefMem / quality evidence
  -> optional AUX3/CAL_OUT forward or scheduled output
```

预约触发链路：

```text
Trigger/Loop owner
  -> FIRE_LOAD(seq, target time / delta ticks, output semantic)
  -> realtime owner validate late/resource
  -> sync_io arm scheduled primitive
  -> PIO/DMA output edge
  -> status ring / RefMem quality / evidence
```

late `FIRE_LOAD` 必须拒绝并进入 evidence，禁止在临界路径补发。

## 当前实现基线

- 已建立 `sync_io_hw_profile.h`，主输入/输出组、AUX 方向、RJ45 和 `SYNC_CLK_OUT` 编译期断言已落地。
- 已建立 mode table，SEQ_STEP、ENC_COUNT、BISS_TAP 均有 mode driver 和 hw 元数据。
- `sync_io.c` 已从单体模式实现收敛为 core 基础设施；但仍偏长，公共原语还可继续拆分。
- `SYNC_CLK_OUT` 已迁移到 AUX2/GPIO28，并通过 `PIO2 + AUX` 资源互斥。
- `ARM_IN`、`EXT_CLK_IN` 仍是运行逻辑待接入项。
- `ModelTurntableAO` 仍存在 debug GPIO 软件定时输出路径，后续必须按 HAOFV 收敛为 “AO 生成计划，sync_io realtime owner 装载 PIO/DMA，PIO 到点出边沿”。

## 实施原则

- 不把调试板临时 GPIO 直接写入产品架构。
- 不让 SCPI/UI 直接调用 GPIO/PIO。
- 不让 mode driver 解析产品业务。
- 不让 Vector/RefMem 承载实时边沿或大 payload。
- 不用补丁式绕过 owner；新增能力必须进入 profile、mode table、resource map、snapshot 和验证脚本。
- 真实 transport 和真实 PIO 输出优先于继续堆静态表模型。
