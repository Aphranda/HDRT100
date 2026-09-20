# T2 预约与分布式时钟分发架构

Status: Draft
Domain: ARCH / T2 Reservation
Canonical: `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/trigger/TRIGGER_FOUR_BOARD_DISTRIBUTED_PLAN.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-08-20

本文档是 HAOFV 下 T2 预约主线的跨域架构入口，描述多节点共同时间如何建立、未来动作如何预约分发、各节点如何在本地硬实时执行，以及实际 T2 如何被硬件锁存并回传。它不建立新的运行 owner，也不替代 Trigger、VDC、TDMA、RefMem 或 `sync_io` 的主域文档；精确 wire layout、窗口容量和时序阈值在实现和交叉审核完成前保持待冻结。

## 设计结论

T2 预约不是“收到网络帧立即触发”，而是以下闭环：

```text
raw local clock + hardware edge latch
  -> TDMA clock-training observation
  -> VDC DPLL offset/rate mapping
  -> Trigger future reservation in VDC time
  -> TDMA flight distribution + READY/fence
  -> VDC target-to-local inverse mapping
  -> sync_io / PIO executes local deadline
  -> hardware latches actual edge as raw local tick
  -> VDC maps completion back to common time
  -> RefMem/TDMA publishes completion evidence
```

关键规则：

- 原始 `local_tick` 始终是自由运行、单调递增的硬件事实，DPLL 不回写、不暂停、不跳变该计数器。
- 硬件 timestamp latch 直接锁存原始 `local_tick`，因此 VDC 尚未锁定时也能形成 DPLL 输入证据；这消除了“必须先有 VDC 才能产生 VDC 所需 timestamp”的循环依赖。
- VDC 是 `local_tick <-> vdc_time` 映射和质量的唯一 owner。所谓 `system_tick` 只是 VDC 映射的只读封装，不是第二个可写时钟。
- Trigger 是预约业务语义和状态机的唯一 owner；在现有 `TriggerAO` 内增加 `TriggerReservationFB`，不建立第二个 Trigger owner。
- TDMA 只负责确定性运输、飞行处理、窗口、READY/fence 和 completion channel，不解析预约业务含义。
- `sync_io`、PIO、DMA、IRQ 负责本地 deadline 执行和实际边沿的原始硬件锁存；CPU 软件读取时间不能冒充实际 T2。
- 首版数据结构必须从节点表和 mask 派生，支持 A0-A7，不能把四节点数量写死进算法或 wire 语义。

## 时间术语

| 名称 | 含义 | owner |
|---|---|---|
| `local_tick_raw` | 本节点自由运行硬件 tick；所有硬件 latch 的原始证据。 | Hardware Service / `sync_io` |
| `VdcMapSnapshot` | 锚点、offset、rate、slew、quality、epoch 和 generation 的稳定快照。 | VDC |
| `system_tick` | `local_tick_raw` 经当前 VDC map 投影得到的单调共同时间视图；只读派生值。 | VDC API |
| `T_fire_target_vdc` | Trigger 预约的未来共同时间目标。 | Trigger |
| `T_fire_deadline_local` | 用冻结 map snapshot 反算出的本地 PIO deadline。 | VDC 计算，`sync_io` 消费 |
| `T2_actual_local` | 输出或设备完成边沿的原始硬件 latch。 | `sync_io` / PIO / DMA |
| `T2_actual_vdc` | 使用指定 map generation 将 `T2_actual_local` 映射后的共同时间事实。 | VDC |
| `T2_error_ns` | `T2_actual_vdc - T_fire_target_vdc`，或按业务定义减去已登记补偿。 | Trigger / Measure 消费 |

不得把 `T_fire_target_vdc` 命名为 `T2_actual`，也不得用软件处理完成时刻填充 `T2_actual_local`。

## 时钟模型

### 原始时间与 VDC

每个节点先拥有独立原始时间：

```text
local_tick_raw = free_running_hardware_counter()
```

VDC DPLL 根据同步边沿的原始 latch 持续更新仿射映射：

```text
vdc_time = vdc_anchor
         + (local_tick_raw - local_anchor) * corrected_rate
         + phase_slew
```

`offset` 只能解决相位差；分布式节点存在频偏时必须同时修正 `rate`。因此
`system_tick = local_tick + fixed_offset` 只能作为初始粗同步模型，不能作为长期 VDC。

### 正向与反向 API

VDC 对实时消费者提供两个有 generation 约束的纯计算接口：

```text
vdc_from_local(raw_tick, map_generation) -> vdc_time
local_from_vdc(target_vdc, stable_snapshot) -> local_deadline
```

规则：

- 正向映射用于同步观测、实际输出/输入 latch 和 completion evidence。
- 反向映射用于把未来 `T_fire_target_vdc` 转成本地 PIO deadline。
- `VdcMapSnapshot` 必须通过 seqlock、双缓冲或等价 guard 稳定读取。
- Trigger 在 arm guard 前冻结预约使用的 `map_generation`；跨越 generation 时必须重算并重新 PREPARE，不能静默沿用旧 deadline。
- RUN 中 VDC 只允许有界 slew。对已进入 arm guard 的预约，不允许时间映射 step 导致本地 deadline 跳变。
- `LOCKED` 可正常接受新预约；`HOLDOVER` 是否允许由预约 profile 的误差预算决定；其他状态 fail closed。

### RP2350 硬件 latch 能力

架构要求 `sync_io` 暴露 `HardwareTimestampLatch` realtime capability，而不是假设 PIO 指令能直接读取完整系统 timer。可接受实现必须把实际边沿和原始硬件时间在有界硬实时路径中关联，例如：

- PIO 在边沿处锁存自身相对计数，结合由硬件维护的 anchor/wrap evidence 展开为 `local_tick_raw`。
- PIO 边沿产生 DREQ，由 DMA 链在固定、已测量的 pipeline 中采集硬件 timer 寄存器和 edge descriptor。
- 后续外部 FPGA/TDC 或 transport MAC 直接提供带 source/resolution/flags 的 latch。

无论采用哪种实现，都必须发布 source、resolution、flags、pipeline bound、sequence 和 overrun evidence，并通过示波器/HIL 校准。当前 PIO-SPI 路径在完整帧处理处由 CPU 读取 timer 的值仍是 `DIAGNOSTIC_ONLY`；在真实 edge latch 完成前，不得用于 VDC 正式锁定、`T2_actual_local` 或 `simultaneous_feedback_loop_evidence`。

## 启动时钟训练

时钟训练必须早于正式预约，但不依赖已经锁定的 VDC：

1. 每节点启动自由运行 `local_tick_raw` 和 wrap extension，声明 tick source、分辨率与 epoch。
2. TDMA adapter 进入 `TRAIN`，reference 节点发送带 sequence、schedule identity 和 reference time 语义的训练帧。
3. 各节点在 RX/TX 物理边沿由 PIO/DMA/IRQ 锁存 `local_tick_raw`；CPU 只能搬运 latch descriptor，不能补写边沿时刻。
4. TDMA 将 reference TX、逐 hop RX/TX、feedback RX 及 CRC/quality 作为 observation evidence 交给 VDC。
5. VDC 结合 active path-delay calibration 估计 offset/rate，经历 initial sync、frequency lock 和 phase lock。
6. 达到质量门禁后 VDC 发布 `LOCKED` 的 `VdcMapSnapshot`；Trigger 才可把新预约推进到 PREPARED。
7. 运行中训练帧继续存在，DPLL 持续更新下一代 map；预约执行和时钟训练共享原始 latch 基础，但 owner 与数据语义分离。

TDMA 的飞行转发本身不依赖 DPLL 锁定；只有“按 VDC 绝对时间执行预约”需要合格 VDC map。

## HAOFV Owner 边界

| owner / 层 | 本主线职责 | 禁止 |
|---|---|---|
| `TriggerAO` + `TriggerReservationFB` | 创建预约、业务补偿、状态机、目标节点、late/cancel/fault 策略和最终业务结论。 | 写 VDC offset/rate，直接操作 TDMA ring 或 PIO 寄存器。 |
| VDC Domain | 生成稳定 map snapshot、正反向映射、LOCKED/HOLDOVER gate、map generation 和 completion 映射。 | 拥有 Trigger 业务状态，发送私有 transport。 |
| `TdmaSchedulerAO` | 为预约段准入窗口，执行 flight distribution，发布 transport completion、READY/fence 和质量证据。 | 解析触发模式、修改预约目标时间、写 Trigger active fact。 |
| Distributed RefMem | 保存 PREPARE/READY/ACK/NACK/fence/completion 共同事实与 freshness。 | 计算 deadline、DPLL 或代替 TDMA 转发。 |
| `sync_io` + PIO/DMA/IRQ | 装载本地 deadline、输出动作、捕获实际边沿并提交最小 latch descriptor。 | 决定扫描业务流程、使用软件时间伪造 latch。 |
| core0 control plane | 配置、System Pack、SCPI/UI、日志、诊断和预约命令投递。 | 参与当前周期 wire deadline 或直接改 core1 active state。 |
| core1 realtime plane | 推进 TDMA fast path、Trigger reservation ECC、稳定快照消费和 PIO/DMA 服务。 | FatFs、USB 文本、UI、OTA flash 或无界阻塞。 |

## 预约对象

以下是语义字段集合，不是已冻结 wire struct：

| 字段组 | 最小语义 |
|---|---|
| identity | reservation/run/epoch/sequence、origin slot、target mask。 |
| target | `T_fire_target_vdc`、通道/动作 id、极性、脉宽、业务补偿引用。 |
| clock binding | VDC quality tier、map generation、calibration CRC、schedule CRC。 |
| gate | earliest prepare、arm guard、late limit、completion timeout、HOLDOVER policy。 |
| evidence | READY/NACK mask、fence generation、actual latch、completion/error/quality。 |

精确字段宽度、字节序、segment offset 和 CRC 覆盖范围必须由 System Pack、TDMA process-image map 和交叉审核共同冻结；本文件不提前登记 wire 契约。

## 端到端流水线

### 预约准备与分发

1. Loop/Angle DPLL 或业务计划向 `TriggerAO` 提交未来动作意图。
2. `TriggerReservationFB` 检查 run/epoch、目标 mask、互锁、VDC quality、active calibration 和最小 lead time，生成 `T_fire_target_vdc`。
3. Trigger 把预约语义编码到自己拥有的 shadow segment，并向 TDMA 提交 payload intent；TDMA 不读取通道或动作含义。
4. TDMA 在已准入 realtime window 中飞行分发预约段。节点只在自己的固定 offset 提取输入、写入自己拥有的 READY/NACK 或 completion 段。
5. 各目标节点校验预约 identity、freshness、VDC generation、资源和 lead time，发布 PREPARED/READY；失败则发布明确 NACK reason。
6. origin 收齐目标 mask 的 READY，并确认同一 generation 已完整绕环后发布 fence。缺少 READY、CRC 错误、窗口错过或 generation 不一致都不得 ARM。

### 本地执行与完成

1. 在 arm guard 前，目标节点稳定读取 `VdcMapSnapshot`，调用 `local_from_vdc()` 生成 `T_fire_deadline_local`。
2. Trigger 通过 REALtime 能力接口把 deadline、通道、脉宽和 reservation identity 装载给 `sync_io`；PIO 在本地时间域执行倒计时。
3. 到点产生输出边沿，PIO/DMA/IRQ 同时锁存原始 `T2_actual_local`。设备 READY/T2 输入使用同一原始 latch 原则。
4. VDC 使用预约绑定的 map generation 调用 `vdc_from_local()`，得到 `T2_actual_vdc` 和 mapping quality。
5. Trigger/Measure 计算 `T2_error_ns`，将 completion、late、timeout、quality 和 evidence index 写入自己的 shadow segment。
6. TDMA 把 completion 段带回 origin；RefMem 保存分布式完成事实和 fence，Trigger 决定 COMPLETED、重试、跳过或 FAULT。

## 预约状态机

| 状态 | 语义 | 允许的主要迁移 |
|---|---|---|
| `DRAFT` | 业务意图尚未通过门禁。 | `PREPARED`, `REJECTED`, `CANCELLED` |
| `PREPARED` | target、clock binding 和资源检查完成。 | `DISTRIBUTING`, `REJECTED`, `CANCELLED` |
| `DISTRIBUTING` | 预约已进入 TDMA process image。 | `WAIT_FENCE`, `REJECTED`, `FAULT` |
| `WAIT_FENCE` | 等待所有目标 READY 和同 generation 环回。 | `ARMED`, `REJECTED`, `FAULT` |
| `ARMED` | 本地 deadline 已冻结并装载。 | `FIRED`, `CANCELLED`, `FAULT` |
| `FIRED` | 预约输出已执行并获得输出 latch。 | `WAIT_T2`, `COMPLETED`, `FAULT` |
| `WAIT_T2` | 等待设备 READY/T2 latch。 | `COMPLETED`, `FAULT` |
| `COMPLETED` | 目标 mask 的完成事实和质量已闭合。 | 终态 |
| `REJECTED` | late、stale、quality、CRC、resource 或 generation gate 拒绝。 | 终态或由上层创建新预约 |
| `CANCELLED` | 在允许取消窗口内撤销。 | 终态 |
| `FAULT` | 已 ARM 后出现不可安全恢复故障。 | 进入系统安全流程 |

取消到达 arm guard 后默认 fail closed：不得通过软件临界路径“尽量取消”已装载的边沿。需要硬件可取消能力时，必须作为独立 realtime capability 和验证项声明。

## TDMA Process Image 分段

T2 预约使用固定 offset、owner 明确的 process-image 段；建议的语义分组如下，实际布局待冻结：

| segment 语义 | writer | reader | TDMA 行为 |
|---|---|---|---|
| reservation command | origin Trigger | target Trigger | 作为 opaque bytes 飞行分发。 |
| READY/NACK | each target Trigger | origin Trigger | 仅允许 owner slot 写自己的固定 slice。 |
| fence | origin Trigger/RefMem fact publisher | all targets | 关联 reservation generation 和 ready mask。 |
| completion | each target Trigger/Measure | origin Trigger | 携带 actual local/VDC、quality 和 result 摘要。 |

V1 完整帧 store-and-forward 可用于语义闭环和单板/两板 bring-up，但不能宣称严格 EtherCAT-style cut-through。只有 RX/TX 重叠、固定 per-hop pipeline、WKC/尾部完整性和 HIL 证据成立后，才能把对应 adapter 标记为 flight capable。

## 故障与门禁

- VDC 未锁定或 HOLDOVER 超出预约 profile 预算：拒绝新预约；已 ARM 预约按冻结 policy 执行或进入安全故障，不临时改 deadline。
- map generation 在 arm guard 前变化：重新计算、重新 PREPARE、重新 fence；guard 后变化不得影响已冻结 deadline。
- TDMA late/window miss/CRC/sequence/fence timeout：不 ARM，发布可追溯 NACK。
- PIO TX FIFO 欠载、DMA overrun、硬件 latch 缺失：该次 actual evidence 无效，不能用 CPU 时间补齐。
- completion 超时：保留输出 latch 和已有 READY/T2 证据，由 Trigger policy 决定 retry、skip 或 FAULT。
- core0 拥塞、UI/LOG/SCPI 阻塞：不得停止 core1 flight 或已装载 PIO deadline；只允许丢弃低优先级镜像并增加质量计数。

## 验证顺序

1. 单板回环：raw latch 连续性、正反向映射 round trip、预约状态机、late 拒绝、PIO 输出 latch 和 T2 completion。
2. 两节点：TRAIN -> LOCKED -> PREPARE/READY/fence -> ARMED -> completion 全链路，host 只读监控。
3. 三节点：验证逐 hop generation、READY mask、fence 和 completion 聚合。
4. 五节点：验证 lead-time 水位、window budget 和故障注入。
5. 八节点：验证 A0-A7 profile 只扩表不改算法，确认 mask、process-image 容量和最坏环路时间。
6. 长时间验证：记录 lock/holdover、map generation、late/window miss、FIFO 水位、硬件 latch 缺失、T2 error 分布和 watchdog/reset evidence。

任何阶段都必须区分：软件模型测试、完整帧 store-and-forward HIL、真正 cut-through HIL，以及最终多节点时钟分发闭环；低等级证据不能替代高等级结论。
