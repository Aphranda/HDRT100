# TDMA 基础件主域待办

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_TODO.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-10-14

## 文档接口与状态规则

Architecture 维护 owner 与稳定语义，运行细则维护代码接口与预算读法，本文维护任务状态，Task Progress 保存原件、失败和验收。旧清单见[历史快照](../legacy/tdma/LEGACY_TDMA_DOMAIN_TODO.md)，原 task ID 保留。

状态限 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`。已接线或局部实测可计进行中，完整退出门禁满足才为 DONE；父任务不因子切片通过而关闭。代码与实测更新状态，文档重排不改变契约登记。

## 已有基线

| 能力 | 已有范围 | 证据入口 |
|---|---|---|
| 四板基本运输 | 固定 SHORT、process follower、局部 overlay、普通/自主路径及固定范围 quick P3。 | TDMA 进展；最新同源码凭证见 `VDC-PROGRESS-20260919-024`。 |
| 异步候车平台 | 普通 TX/RX/overlay 及 origin 构图有固定 Core0 工位，Core1 保留采用/退休/取消。 | `TDMA-PROGRESS-20260914-011/012`；012 候选已撤回，不计生产收益。 |
| 节点与周期 | 编译容量、STOP 后选择邮箱、ARM 冻结及整表周期请求/确认已实现。 | `TDMA-PROGRESS-20260913-062/063`、`20260914-013`。 |
| 同步快速通道 | typed 固定邮箱、Core1 TX/provider、RX IRQ 保全及 VDC 本地 MATCH/FOLLOW 已实际工作。 | VDC 的 `VDC-FAST-*`、`VDC-LOCAL-*` 及进展。 |
| 观测与时间 | observer 回绕修复、统一 TIMER1、内部探针和可选示波器长窗已有证据。 | `TDMA-PROGRESS-20260917-001`；VDC 进展 `20260918-040/041`、`20260919-018/020`。 |

当前以四板验证，NO5 为可选扩展。quick P3 的 PASS_WITH_WARNINGS 与原始严格失败字段并存，不能写成完整 WCET、零缺帧或 resident 全窗口通过。DPLL 已有真实闭环和物理输出；正式质量、恢复及 VDC 发布仍由 VDC 待办管理。

## 当前主线

优先收敛 **TDMA Foundation 的席位预算、双向准入与分段流式调度**，保留已工作的特等席。TDMA 的下一级抽象是 seat；T2、OTA、CONFIG、DIAGNOSTIC 都是挂载在 seat 上的 load instance。预算以当前 `PROJECT_CORE1_*`、`app_realtime_profile_*`、`tdma_profile_set_traffic()` 默认配置和实际 profile generation 为准；历史周期门限不作为现行统一判据。

新增或扩容特等席须先提交席位、字节、频率、Core1/IRQ 最坏耗时及 SYNC 余量清单，由用户审核后实施，见 `EXE-SEAT-01`。健康镜像与统计走 Core0 异步诊断，不占特等席或新增 Core1 发布配额。

| 顺序 | 工作 | 可评审的退出证据 |
|---|---|---|
| 1 | 完整 TDMA phase 与 IRQ 预算核对 | 同源码/同配置的前台、IRQ、关窗、skip/late、RUN/RESET/OTHER 和物理进展分别对账。 |
| 2 | 异步准备与自主运行 | no-update 复用、selection 退休、迟到更新、STOP 取消及主从 blackout；同圈 overlay 不破坏节拍。 |
| 3 | 特等席期限与负载隔离 | 事件生成到实际送达/采用的身份、最长间隔、覆盖/缺口及饱和负测；不依赖普通解析。 |
| 4 | 产品化扩展 | 资源/WCET、全拓扑、长档运行、recovery/fence 和多板 T2 分别验收。 |

### Foundation 席位预算与 load instance 顺序

```text
统一周期口径
  -> 特等席 TX/RX 与 close guard
  -> 一等席准入
  -> 一等席 load instance（当前为 T2 position_q）
  -> OTA/CONFIG/诊断分段 load instance
  -> RX FIFO/CRC/重组/Core0 消费
  -> ACK/诊断反向预算
  -> 周期 guard 与恢复预算
```

发送预算、接收预算和业务应用预算必须分别闭合。不能因为 TX 发送快或主机观察到等待时间，就推断 RX 有余量；必须测量 RX FIFO 最坏积压、Core0 消费峰值、重组耗时和反向流量。

首帧唯一身份、首次发车及首窗口诊断继续作为 TDMA 精细优化，不是 DPLL 使用后续稳态有效样本的前置。只有具体 TDMA 缺陷已破坏有效输入时，才把对应修复调回当前切片；不重开全部历史校准与启动证明。

## 未完成

### 里程碑总览

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-M6 | resident process image flight | IN PROGRESS | 自主全窗口、同圈装卸、无更新透传、受控退出及完整预算；不是只有周期可配置。 |
| TDMA-M3 | DPLL/VDC 最小负载 | IN PROGRESS | typed 运输和本地控制已有，补齐期限、正式 evidence 与 VDC 一致发布。 |
| TDMA-M4 | completion/reliability | PENDING | ACK/fence/retry/fail-closed 与长期错误率完整验证。 |
| TDMA-M5 | T2 reservation 与控制 | PENDING | 当前四板预约、READY/fence/completion 集成；其他节点规模另验。 |

### 实时预算、飞行与异步准备

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-FLIGHT-002 | 真飞行处理总目标 | IN PROGRESS | F1–F5、owner/resource/snapshot、完整 CPU/RAM 和物理周期映射；契约激活须 C11。 |
| TDMA-FLIGHT-001 | 常驻映像与同圈多 Node overlay | IN PROGRESS | 一次初始化、持续 cycle、授权装卸、无更新透传和受控退出。 |
| TDMA-FLIGHT-002B | 自主发车与硬件进展 | IN PROGRESS | 已有 origin 有限 blackout；补主从全窗口、物理节拍、切换/故障及最坏预算。 |
| TDMA-FLIGHT-002B1 | 自激发车入口契约收敛 | PENDING | **本任务分两层读：自激语义层只属参考节点；环前提层（profile/调度与每槽工作量）是工程约束，不是发射语义的一部分。** **(i) 自激语义层**：发射权只属**参考节点**（`tdma_pio_spi_ring_adapter_resident_process_image()`：`role == REFERENCE` + `FORWARDING_PHYSICAL_PROCESS_IMAGE` + flight engine active；许可侧 `calibration_origin_timing.inc` 要求 `local_slot_id == reference_slot_id`；本轮台架只有 NO1 受理 `TRIAL:READY`），raw-flight 不可达。**主机只做许可、授权/释放、只读回读与回收**，不参与逐拍发射：发射由**环返回**驱动——许可 → `RELease` → AUTONOMOUS 后，每周期 `rx_once → observe(boundary) → publish`（`tdma_pio_spi_ring_origin.inc` 的 `tdma_pio_spi_ring_origin_service`）。当前真实闸门是 **on-change**：`tdma_pio_spi_ring_adapter.c:1893-1921` 只对 `TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE` 且 `receive_health.configured != 0` 的帧把 `tdma_flight_engine_rx_unload()` 的 `new_segment_mask` 存入 `last_rx_new_segment_mask`，`:2286-2300` 只在该掩码非零时发布进 flight FIFO；发射侧再按 `(generation, sequence)` 去重并要求 `flight_engine.map_generation == job->layout.map_generation`。实测表现为按变更（≈每环回一次）而非按节奏。固件已加**opt-in** 开关 `tdma_ring_runtime_config_t.origin_cadence_enabled`（`tdma_ring_runtime.h`；`tdma_pio_spi_ring_adapter.c:2290（该处放行经独立复核判定为**死代码**并已回退；on-cadence 的实际放行实现尚待落到参考节点/origin 服务路径，且发布须用 present/output 掩码而非 new 掩码）` 非零且本节点为参考节点时，无新段也放行发布），默认 0 保持历史行为。on-change ↔ on-cadence 是**闸门策略**选择，不改变"发射由环返回驱动、由参考节点独占"这一语义。 **(ii) 环前提层**：全环节点共享同一 profile 与 schedule，**每槽工作量必须装进槽长**，否则环起不来（`TDMA-EMISSIONCLOCK-01` 要求低于可达下界的 profile fail closed）。最小发射间隔因此由环物理（槽长 × 节点数 + 每槽工作量）界定，属工程约束/可调参数，不写进发射语义层。本轮证据（快照，非事实源）：新增档位 20={10 MHz,375000 ns,4096}、21={10 MHz,500000 ns,4096}（`tdma_operating_profile.c`）；板端接受 `SYSTem:TDMA:OPMode:STAGe 20` 并返回 `20,10000000,375000,4096,0,111157511`（与工具 `profile_crc32(20)` 一致），但 `tools/tdma_ring_monitor/tdma_start_ring.py --level 20` 报 `ARM timeout`、`ring_adapter_started=0`、`tx_count=rx_count=0`（失败点 follower slot 1），`SYSTem:ERR?` 干净无拒绝码——故 375 µs 档在当前帧长/波特率下**每槽工作量装不进槽**，缩短环回不是本问题的解法。换档位不是线侧重校准：阶段记录 `calibration_crc32` 由**板端对结构体计算**（`tdma_origin_calibration_crc.c`、`calibration_origin_timing.inc`、`tdma_runtime_origin.inc`），必须由板端重新生成阶段凭据。入口按五层定序并写入架构：许可 `CALibration:ORIGin:TRIAL[:READY]`（要求 `DIAGNOSTIC_CONTINUE` **置位**，且只在参考节点受理）→ 授权（要求 `burst_limit == 0`，用 `SYSTem:TDMA:RING:BURSt 0`；许可后不得再装载/应用配置，`config_seq` 必须保持不变）→ 释放 `CALibration:ORIGin:RELease <trial_epoch>,<config_seq>` → 物理准入 `tdma_pio_spi_phys_origin_begin()` → 环边界硬件事件。构建与准备阶段机由**环周期服务**驱动（`tdma_pio_spi_ring_origin.inc` 的 `phys_origin.poll` → `tdma_runtime_origin.inc` 的 `tdma_runtime_owner_origin_poll()`），**不是主机轮询**；主机在自激语义层只做许可、授权/释放、只读回读与 `REVOkE` 回收。环启动前必须先完成生产软复位（否则 recorder 状态阻塞 ARM）。判定一律用只读回读（`READ:CALibration:ORIGin:HANDoff?` 判准备/READY，`:RUNTIme?` 判 `active` 与 `observed_return_count`），变更命令缺失终止符不得当作拒绝。出口强制收敛：`CALibration:ORIGin:REVOkE` 后校准阶段与 ring 配置不得残留。实测补充：`TRIAL:READY` 为 DEFER_RELEASE，若释放缺失、被拒或发生在到期之后，准备会**永久停在 PREPARED**（`comm_fsm_state=6`），同 epoch 重发被判重复；释放必须在 READY 之后、到期之前，且 `config_seq` 不得变化。所有 origin 诊断页（handoff/release/build probe）**要求 STOPPED 环**，运行期没有 READY 只读回读，只能用 `:RUNTIme?` 的 `comm_fsm_state`（5=AUTONOMOUS）判定。`elapsed_ticks` 与 `invalid_count` 在 250 MHz 下 17.18 s 后饱和（记录量为 u32），不得解读为被拒观测。`RECord?` 不含 `guard_count/guard_floor_ticks`（属试用期 cadence）。SCPI 页 tag 占首位，字段 N 位于 `values[N+1]`，解析不得用裸索引。 **(ii-b) 环前提层的接线约束：每板发射开关不得进 `tdma_ring_profile_t.flags`。** 该 `flags` 被 `tdma_ring_profile_crc32()`（`tdma_profile.c:94`）哈希进 `config.ring_profile_crc32`，随后**序列化进 wire 帧**（`tdma_transport_frame.c:177,290`）并在 RX 侧**校验**（`tdma_pio_spi_ring_adapter.c:1764`、`tdma_pio_spi_ring_origin.inc:30`）；若把每板位放进 `flags`，参考节点与转发节点会算出不同的 ring profile CRC，直接导致帧被拒。因此发射 cadence 开关必须是**每板 staged runtime 字段**（`tdma_ring_runtime_config_t.origin_cadence_enabled`），经与既有 `tdma_service_set_loop_delay_ns()` / `tdma_service_set_ring_diagnostic_mode()` 同族的 **STOP-only setter** 写入（`tdma_service.c:647,667`），而环配置字面量 `tdma_service.c:533-552` 有意不列该字段 ⇒ 默认 0、保持历史行为。主机面沿用**既有命令的可选第二参数**（`SYSTem:TDMA:RING:DIAGnostic <diag_continue>[,<origin_cadence>]`，SCPI 入口 `scpi_system_snapshot_commands.c:3811-3821`），保持响应与 `RING:DIAGnostic?` 元数不变以免影响既有主机；退路是更窄的 `SYSTem:TDMA:RING:ORIGin:CADEence <0|1>` 一对。**该主机面仍在实现中，命令拼写属快照，冻结契约文本前须复核。** |
| TDMA-FLIGHT-002C | 双缓冲与无更新稳态 | PENDING | 原子选择、旧槽退休与复用波形一致；无更新不靠 Core1 改写线行为。 |
| TDMA-FLIGHT-002D | 非阻塞 overlay 与同圈装卸 | PENDING | 就绪采用、迟到顺延、逐 hop owner/CRC 与节拍不变的独立证据。 |
| TDMA-FLIGHT-002E | byte/cycle 飞行声明与契约收敛 | PENDING | F1–F5 完成后独立 C11、registry 及顶层刷新；现有 pending 不自动提升。 |
| TDMA-FLIGHT-002F | TX/RX 异步候车平台 | IN PROGRESS | 已有后台准备；补完整预算、峰值归因、拥塞、迟到复用、selection 退休与 STOP 取消。 |
| TDMA-FLIGHT-002J | 席位双向预算 | IN PROGRESS | 绑定同一 profile generation，分别测 TX 准入、RX FIFO 峰值、Core0 消费、CRC/重组、ACK/诊断反向流和 guard；发送快但接收满必须进入背压证据。 |
| TDMA-FLIGHT-002K | 席位准入状态机 | IN PROGRESS | 软件模型按同代身份、完整预算及限时授权推进，load 选择与后端确认提交分开；物理 selection/commit 接线、VDC 新事件即时撤销及跨板无扰动专项仍须完成。 |
| TDMA-FLIGHT-002K1 | 席位准入软件模型 | IN PROGRESS | `tdma_seat_admission` 的身份、预算各维度、一次性消费、STOP/换代撤销、别名输入、晚交接与整数边界验证；同源码构建/P3 和独立复核完成后关闭，不等于物理窗口开放。 |
| TDMA-FLIGHT-002K2 | 物理边界与席位准入接线 | PENDING | 物理 selection、已提交发送及独立窗口时间证据先闭合，再绑定 owner 同代实测 TX/RX/CPU/反向预算；缺预算保持关闭；T2 经一等席验证，不能覆盖或暂停 VDC。 |
| TDMA-FLIGHT-002L | load instance 分段调度 | PENDING | OTA/CONFIG/诊断按 segment、每周期帧数、队列深度、deadline 和 overflow policy 运行；TX/RX/重组/反向流均有快照。 |
| TDMA-SEAT-01 | 一等席**容器与预算条款**的登记（T2 域一等席布局联审的条件 C1 要求） | PENDING | 现状：一等席容器/预算由 `TDMA-T2-001`、`TDMA-T2-002` 承担，**无专任任务行**；其载体章节在 `TDMA_DOMAIN_ARCHITECTURE.md` 中自述「本节不登记为契约」，且 `docs/check/DOCS_REGISTRY.md` 中 `SEAT`/`EXE-` 前缀**零命中**（2026-10-14 核实）⇒ T2 侧 `T2-IDENT-001`、`T2-TRANSPORT-002` 的「一等席布局/预算登记（C11）」**无落点**。**退出门禁**：以一个 `Status: Active` 文档为 clause_loc 登记一等席容器与预算条款（席位、字节、频率、Core1/IRQ 最坏耗时、SYNC 余量清单，即 `EXE-SEAT-01` 要求的预算清单节），staged 指纹与 C11 独立审核通过（核验方不得为起草方）；`EXE-SEAT-01` 的用户审核结论另记，不并入本条。 |
| TDMA-SEAT-02 | 席位比例与 seat 计数 SCPI 页 | IN PROGRESS | 比例已是 STOP-only 可配置（`SYSTem:TDMA:SEAT:ROTation`，默认 1:1，每个 share 取 1..`TDMA_SEAT_ROTATION_SHARE_MAX` 且 `M+N <= TDMA_SEAT_ROTATION_CYCLE_MAX`，越界/零值/运行中拒绝）；origin 席位与一等席服务事实已接只读页（`SYSTem:TDMA:SEAT:COUNters?`，8 字段，含 stage 拒绝与交付前损失），主机负测覆盖校验、拒绝、快照读回与等待发布。剩余：从板一等 accept/duplicate/reject 已在 `SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 尾部，需在台架探针中按名提取（已加入 `t2_bench_probe.py`，待一次硬件运行验证）；比例属席位预算，须经 `EXE-SEAT-01` 审核并在 `DOCS_REGISTRY.md` 登记（C11）。 |
| TDMA-SEAT-03 | 一等席服务等级对齐特等席 | IN PROGRESS | 目标：一等席在准入、失败记录、完整性与期限上与特等席同级。**已完成**（逐条见进展日志 `TDMA-PROGRESS-20261011-004/005/006/007/008`）：服务事实（stage 被拒、交付前退休、等待值先发布再退休）；完整性核对（记录级 header/identity/transport/mailbox CRC 由优先级通道在投递前统一校验）；有界接收**队列**（被拒 PREVIEW 每源 FIFO 深度 4、只追加 SPSC、满则丢弃并计数，由 T2 后续时间片按到达顺序补投，计数 `lane_hold/drain/drain_fail/hold_expired/hold_dropped`）；TX 侧控制字节仲裁 + **诊断准入**（先诊断后一等确认；有一等事实要发布时控制字节事先不授予诊断并计入 `lane_diag_refused_count`，未预留时的覆盖计入 `lane_ack_override_count`）；lane 拒绝按 class 归因（`first_class/other/unknown_class_reject_count` 三者之和恒等于 `reject_count`）；两条声明预算 + 越界计数（origin `TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES` → `seat_first_class_wait_violation_count`；从板 `DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS` → `lane_hold_expired_count`）；**稳定性判定页** `SYSTem:TDMA:SEAT:HEALth?`（`ok,reason_mask` + 20 字段原始证据，只由退化事实置位，见 [运行约束](TDMA_RUNTIME_CONSTRAINTS.md)）；**loop plan 席位饥饿显式化**（`tdma_loop_plan_t.dropped_mask` + `tdma_loop_plan_seat_service()` 纯判定；origin 计入 `seat_starved_count` / `seat_first_class_starved_count`，判定页 bit6）；**注意门控当前默认关闭**（`TDMA_LOOP_SCHEDULE_ENABLED == 0`，席位窗口调度未启用），构建守卫保证其可编译。**裁定（2026-10-11，owner）**：快速通道只有一条、特等与一等**分时复用**（**M:N 轮换即席位调度**）；不存在"独立 TX 通道"目标，A″/B 作废，loop-plan 窗口门控保持关闭；共用通道下的控制字节准入/覆盖保护为长期机制（`TDMA-PROGRESS-20261011-034`）。**剩余**：真机 P3 凭证与四板席位页证据（条目 15）。 |
| TDMA-FLIGHT-003 | 发车节拍下界与准入 | PENDING | 依据物理计划与 Calibration 输入给出可达下界；过快配置拒绝及只读证据齐全。 |
| TDMA-DET-004 | prepare/preload/launch/wire/feedback 分解 | PENDING | 普通与自主路径分别定义子阶段、硬件边界与拍级合同。 |
| TDMA-DET-005 | topology/baud/tail 动态容量门禁 | PENDING | 激活前完整 wire 与资源预算复核，超预算 fail closed。 |
| TDMA-FLIGHT-006 | byte 边界时序与准入 | IN PROGRESS | 现有模型补最坏 DMA 仲裁/断粮、环境与严格校准。 |
| TDMA-FLIGHT-007 | 非字节对齐 overlay owner 保护 | IN PROGRESS | 现有逐 hop 基线补 generation handoff、自主同圈与供给压力。 |
| TDMA-FLIGHT-002I | 编译容量与运行时布局 | IN PROGRESS | 两层配置已实现；补全拓扑/混合容量、资源和完整预算；减在线节点不等于释放静态 RAM。 |
| TDMA-HIL-001 | 四板时序与物理基线 | IN PROGRESS | 已有 P3/波形；补当前 profile 完整 WCET、频率/占空比、原始证据和零错误全窗口。 |
| TDMA-HIL-002 | 逐 phase 加载无回归 | PENDING | 分别启用 VDC/DPLL/RefMem/control，保留 TDMA deadline/error、IRQ 及最坏重叠。 |

### 特等席与普通载荷

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-FLIGHT-002G | 同步特等席逐圈交付 | IN PROGRESS | typed TX、独立 RX 保全与 Core1 消费已有；补每圈/最长间隔、覆盖期限、溢出/旧代/STOP 和时钟映射。 |
| TDMA-FLIGHT-002H | 四级服务与固定配额 | PENDING | 普通流饱和不破坏特等期限与一等 freshness，RefMem 可靠确认、无座有界限流。 |
| TDMA-PAYLOAD-002 | 同步记录与 compact 输出 | IN PROGRESS | 区分 typed body、普通 Node body 和兼容 trailer；完整运输身份、正式资格及共存通过。 |
| TDMA-PAYLOAD-003 | critical RefMem 与 ACK/fence | IN PROGRESS | 基础 delta/ACK 已运输；补业务 commit/fence 与失败恢复。 |
| TDMA-PAYLOAD-004 | 最小控制 token | IN PROGRESS | 已预留布局；补 owner、opcode、deadline 与 completion。 |
| TDMA-PAYLOAD-006 | 实时诊断压缩与隔离 | IN PROGRESS | 只保留基础计数/身份；原始波形、SD/SVG 和详细归因不侵入实时 payload。 |

### DPLL 交接与观测

算法、精度和正式 VDC 发布的唯一任务源为 [VDC TODO](../vdc/VDC_DOMAIN_TODO.md)；下列旧 ID 保留 TDMA 接口责任，不再维护第二套独立锁相路线。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-DPLL-001 | 硬件事件与运输关联 | IN PROGRESS | typed/原始事件和兼容 trailer 已接线；补 active path 身份、换代、回绕与失配 HIL。 |
| TDMA-DPLL-002 | 本地闭环与 VDC 发布交接 | IN PROGRESS | 四板真实采用已有；跟随 VDC 的持续性、formal quality 与发布任务，不强制 NO5。 |
| TDMA-DPLL-003 | 正式 active calibration matrix | PENDING | 有向 path/offset、generation/freshness 与拓扑完整一致；运行态不按环序猜测或累加 fallback。 |
| TDMA-DPLL-004 | timestamp spine 与校准 | IN PROGRESS | TIMER1/observer/history 已有；补 CS/SCK/DATA 完整误差界、独立 SCK 与负测。 |
| TDMA-DPLL-005 | eligible evidence 准入 | IN PROGRESS | typed CRC/身份/MATCH 已有；正式校准和质量门禁另验，坏样本拒绝并留缺口而非阻断后续有效样本。 |
| TDMA-DPLL-006 | Core1 servo 与 compact 发布 | IN PROGRESS | MATCH/FOLLOW/delay/DCO 已工作；补完整锁定/保持、质量发布与 WCET。 |
| TDMA-DPLL-007 | 故障、holdover、relock | PENDING | 单链路/旧代/无效输入/recovery 注入有明确行为，恢复后重新准入，TDMA 无回归。 |
| TDMA-DPLL-008 | 原生记录与离线分析 | IN PROGRESS | SRAM/STOP 导出/SVG 已有；补长期分段/保存恢复，SD 和 NO5 非每轮必需。 |
| TDMA-DPLL-009 | 启动 profile 与持久化 | IN PROGRESS | 已有多组 SCPI/Flash/重启证据；补完整启动 profile，默认 RAM 迁移与显式 STOR 分开。 |
| TDMA-HIL-003 | 四板内部/外部观测 | IN PROGRESS | 内部探针及可选示波器已有联合长窗；补正式精度、生命周期与运输期限资格。 |

### 可靠性与后续集成

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-REL-001 | ACK/fence/retry 策略 | PENDING | 原始错误率、可靠交付、背压和明确失败分开验收。 |
| TDMA-REL-002 | 原 Node recovery 双 buffer | IN PROGRESS | 固定 offset/预算已有；补 ACK、retry exhaustion、背压和多拓扑故障 HIL。 |
| TDMA-T2-001 | T2 最小载荷预算 | IN PROGRESS | 固定 SHORT/body 与 phase 不超限；紧缩单帧（`TRIGGER_POSITION_SLICE_WIRE_SIZE`，恰填 RefMem 区）预算与快速路径发布节奏已有四板实测（`TDMA-PROGRESS-20261010-001/002`，快照）；剩余为 T2 身份/确认位宽定稿（`T2-IDENT-001`）后重新核定一等席布局与预算。 |
| TDMA-T2-002 | T2 一等席 load instance | IN PROGRESS | 一等席窗口只提交不可变单 `position_q`；不等待特等席回环，不覆盖 VDC image；送达/应用/冻结证据已闭合（`TDMA-PROGRESS-20261010-002`、`T2-PROGRESS-20261010-024`）；剩余为真实物理 selection 证据与身份/确认位宽定稿。 |
| TDMA-T2-003 | T2 冻结诊断 load instance | PENDING | 正常一帧、异常追加一帧诊断，纳入 RX/TX 反向容量和队列背压，不占 typed SYNC body；注意冻结完成本身当前经控制字节回报，不构成该诊断 class。 |
| TDMA-QUALITY-001 | 逐流质量与背压 | PENDING | per-class、超预算及质量发布闭合。 |
| TDMA-MAP-001 | 正式 System Pack map | PENDING | owner/generation、active/shadow 在明确 cycle 边界切换。 |
| TDMA-ADAPTER-001 | PIO SPI 两级 flight gate | PENDING | byte/cycle 分级证据、完整性、FIFO 和 map apply 全范围验证。 |
| TDMA-ADAPTER-002 | BISS-C/UART/RS485 adapter | PENDING | MTU、latency、timeout、资源与质量接口及真实承载验证。 |
| TDMA-BULK-001 | bulk 与 critical 配额 | PENDING | 显式维护窗口、准入/背压，不侵占 Core1/guard；OTA 实现不纳入本次整理。 |
| TDMA-REL-003 | 多环消重 | PENDING | 多路径方案冻结后验证 sequence/replication/duplicate elimination。 |
| TDMA-BOARD-001 | 产品 IO 与硬件资格 | PENDING | 对应产品 IO、单跳及多板完整 HIL。 |

### 启动精细优化与训练承接

保留原 P0–P7/P0.5 未完成范围；Calibration 已有可用训练输入不等于以下所有生命周期和故障门禁已完成。兼容线序复用、四板基础验收和 DPLL 稳态推进不等待这些项目全部关闭。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-REFINE-001 | 首次发车与首描述符 | PENDING | 实际 builder/原件对应，首失败不被后成功替换；不要求 DPLL 使用首帧。 |
| TDMA-REFINE-002 | capture/sequence/control 偏移 | PENDING | 分频、stall 与采样时差分解，以误差预算确定收益。 |
| TDMA-REFINE-003 | 首 RX 窗口身份 | PENDING | 不跨 CS 拼接的独立诊断，后续有效帧正常消费。 |
| TDMA-TRAIN-001 | RX 扫描诊断 | PENDING | candidate/idle/real magic 分类可追溯。 |
| TDMA-TRAIN-002 | 训练状态机与 coded persona | PENDING | owner、epoch/seq、资源及有界恢复。 |
| TDMA-TRAIN-003 | 校准投影与 TRAIN frame | PENDING | snapshot/freshness、ACK/commit 及原始拒绝点。 |
| TDMA-TRAIN-004 | 拓扑/窗口/evidence | PENDING | active topology/generation/freshness 接入 schedule。 |
| TDMA-TRAIN-005 | 编排、故障与恢复 | PENDING | STOP→APPLY→ARM→TRAIN→restore→STOP 全流程可复核。 |

## 已完成

以下保留原已验收范围；它们不覆盖新增 profile、最坏工况或当前未完成父任务。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-M1 | 拍级 schedule 与编译门禁 | DONE | 静态表、phase/WCET 结构和四板基线已建立；当前完整实测由 HIL-001 继续收敛。 |
| TDMA-M2 | mandatory-first SHORT | DONE | 原固定布局、publisher/parser、CRC、SCPI 与多板基线齐全；typed 共存另验。 |
| TDMA-DET-001 | 拍级表与唯一单位 | DONE | `APP_REALTIME_PHASE_TABLE`、邻接及周期闭合检查。 |
| TDMA-DET-002 | schedule/wire 编译门禁 | DONE | phase、WCET、SPI 与最大 SHORT wire 构建拒绝。 |
| TDMA-DET-003 | 拍级 runtime/SCPI | DONE | 既有 runtime/SCPI 多板证据归档；新周期运行不在原范围。 |
| TDMA-PAYLOAD-001 | mandatory body 固定布局 | DONE | layout、编译断言与预算工具一致。 |
| TDMA-PAYLOAD-005 | optional 静态准入 | DONE | 仅使用 mandatory 后的固定余量，无 runtime-free 字节。 |
| TDMA-FLIGHT-002A | byte-level RX/TX 重叠 | DONE | 当前夹具固定 pipeline 取证，见 `TDMA-PROGRESS-20260911-006`；PIO/profile 变更复验。 |
| TDMA-FLIGHT-004 | 四板回归归因与恢复 | DONE | `TDMA-PROGRESS-20260911-003`，不外推电气根因。 |
| TDMA-FLIGHT-005 | follower PIO 补丁下标 | DONE | public label 绑定、变异与多板验证，见 `TDMA-PROGRESS-20260911-004`。 |

## 当前阻塞项

| 验收目标 | 尚缺的关键证据 | 对现有主线的影响 |
|---|---|---|
| 严格实时与 resident 完整资格 | 当前完整 WCET、主从 blackout、同圈多节点更新、切换/拥塞/恢复、F1–F5。 | 不能宣布全配置真飞行完成；不否定已工作的四板运输。 |
| 特等席确定性交付 | 实际逐圈/最长更新间隔、覆盖上界、最坏 IRQ/前台与普通流隔离。 | 不能由固定 FIFO 或单次锁相直接推出无损期限。 |
| 正式 VDC/可靠业务发布 | calibration/quality、ACK/fence、正式 map 与 T2 集成。 | 按所属域任务推进，不重建 NO5 或首帧前置。 |
| 架构偏差关闭 | `TDMA-RESIDENT-01` C11 与 `HAOFV-879` 全写者/读者审核。 | 保留 registry 状态；局部 guard 实现不自动关闭条款。 |

## 交接状态：席位挂载与 T2 使能链（2026-10-09）

| ID | 任务 | 状态 | 完成定义 |
|---|---|---|---|
| **TDMA-HANDOFF-001** | **会话交接：席位挂载与 T2 使能链** | IN PROGRESS | ① 本节「当前唯一阻塞」可被下一手直接复现（`DEPloyment:STAGe` 被拒、`T2:CONFigure` 返回码 92）；② 配套原件齐全且相互一致（`TDMA-PROGRESS-20261009-012`、`T2-PROGRESS-20261009-021`、提交 `7ff68367` 与 `df09747c`、P3 凭证 `PASS_WITH_WARNINGS`）；③ 本轮为定位插入的临时台架诊断插桩已清理或规范化为正式诊断；④ 接手方在 Task Progress 记一条"接手确认"。 |

本节记录一次跨域收口的即时状态，便于接手；证据原件见 [Task Progress](TDMA_TASK_PROGRESS.md)
（`TDMA-PROGRESS-20261009-012`）与 [T2 进展](../trigger/t2/TRIGGER_T2_TASK_PROGRESS.md)
（`T2-PROGRESS-20261009-021`），代码与工具已随提交 `7ff68367` 入库。

**已实现并经硬件验证**

| 能力 | 状态 | 说明与证据入口 |
|---|---|---|
| 席位挂载表全链 | DONE | SCPI 配置（含 CRC 校验）→ flash 持久化 → 开机载入 → 运行时"按负载所挂席位"判定发布窗口；负载与席位分离，只做席位对席位比较。 |
| 持久化载体与耐久 | DONE | 记录写入专用 `SEAT_MOUNT` 分区（`FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_*`），四板在经历烧录与完整运行后仍 `loaded=1`；此前借用暂存区会丢失。 |
| 平台闸门（4 处） | DONE | 原始 flash 调用登记、构建源登记、flash 事务 AO 来源白名单、事务层"来源×分区×范围×长度"规则；缺任一项写入以策略/权限错误静默失败。 |
| T2 使能链前置 | DONE（逐环验证） | 网格（`subdivisions >= 2`）→ 部署 STAGe/ACTivate（CRC 经设备接受）→ 动作表（**必须逐板按本板 `binding.local_role`**）→ 模板库（`TEMPLate:INSTall/ACTivate`，自带 CRC）→ 停机维护窗口 → `T2:CONFigure` 成功（三块执行板 `configured=1`）。 |
| DPLL60 步进化 | DONE | `tools/vdc_priority_trace/DPLL60_workflow.py`（步骤 0–9：初始化/前置检查/四板复位/TDMA 停机态交接证据/示波器发现/采集前置/60 s 联合采集/产物校验/摘要与门限/工作流收据），`--list/--step/--steps/--full/--interactive/--refresh-html`，数据驱动视图 `docs/trigger/t2/DPLL60_FLOW.html`。薄编排，采集与判据复用 `vdc_priority_joint_capture.py` 与 `p3_dpll_short`；工作流收据不是凭证，正式凭证仍由打包入口签发（代码提交 `b2c527ed`）。P3 全流程的同形工作流见 T2 `TRIGGER_T2_TODO.md` 后续项。 | 无 |
| T2 启动闸门 | DONE | `T2:STARt` 前置 `distributed_refmem_reset_position_stopped()` 要求飞行 FIFO 排空；解药为 `SYSTem:TDMA:FLIGHT:FIFO:RESet`（`SYSTem:TDMA:FLIGHT:FIFO?` 可读四计数）。 |
| 源侧（转台脉冲） | DONE | `CONFigure:MODEl:TURNtable:{LOAD,MOTion,TRIGger}` + `MODEl:TURNtable:STARt`；台架为每板 OUT4↔IN1 自环，**NO1 部署行必须允许 OUT4（掩码 0x8）**。 |
| VDC 输出运行 | DONE | 反馈会话号必须**单调递增**（`vdc_model_feedback.inc` 的 `s_model_feedback_last_session`，`UINT32_MAX` 为清除哨兵）；参数界 `period_ns∈[1ms,100ms]`、`high_ns∈[100ns, period/2]`、`duration_ms<=20000`；须在停机态准备。 |
| 台架工具 | DONE | `p3_hardware_acceptance.py t2`（调用 `t2_workflow.py --full`；通过要求四板 `T2 configured 1 active 1`、有发布，且 workflow 报告 `failed steps: none`——后者涵盖 SYNC_TRIGGER 隔离、游标粘性故障与参考板未发布全部位置）与 `tools/sync_trigger/t2_workflow.py`（22 步 0–21 的可步进工作流：`--list/--step N/--steps 1-6/--full/--interactive`；每步带时间戳、耗时、具名失败原因，整场 tee 到 `out/tdma_t2/steps-<stamp>.log`）。顺序：**标定阶段自检(0) → 停机态配置与挂载(1–13) → 转台/源只配置(14) → 优先级(15) → 起环(16) → 锁相+动态启动位6(17) → 时基就绪(18) → 试运行+启动脉冲流(19,阈值守门) → 采样(20) → 停机(21)**。 |

**当前唯一阻塞（下一手直接从这里接）**

| 阻塞项 | 现象与已排除项 | 下一步 |
|---|---|---|
| 部署 `DEPloyment:STAGe` 准入被拒 | `DEPloyment?` 在 STAGe/ACTivate 后仍全 0，随之 `T2:CONFigure` 返回诊断码 `92`（部署未激活）。已排除：VDC 反馈会话在位（清会话前后一致）、RefMem 暂存包（`LOAD:STATus?` 无待提交包、`LOAD:ACTivate` 后仍为 0）。 | 给 `stage_stopped()` / `deployment_idle()` 逐条插码（沿用现有"基线 + 首个触发者优先"通道），或先 `SYSTem:BOOT:RESet` 软复位四板再走链；随后复现 staged/active/configured/active 全 1。 |

**遗留与债务**

| 项 | 说明 |
|---|---|
| 临时台架诊断插桩 | 本会话为定位在 flash 事务、TDMA 服务、SCPI 命令、T2 运行时与 VDC 准备等处插入的诊断码（复用 `TRIGger:REServation:SEAT:MOUNT?` 的 last_reject 通道），已在提交信息中注明，需清理或规范化为正式诊断。 |
| 发布判据未达成 | `TRIGger:REServation:T2?` 的 `last_error` 仍为 `CURSOR(5)`、`event_count`/`published_mask` 为 0，`SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 计数为 0；即"位置帧发布"尚未观测到。 |
| 文档门禁既有告警 | REFMEM 进展日志轮转、若干进展日志日期序为已登记债务，与本次改动无关。 |

## 统一完成定义

每项先固定配置、目标与退出门禁，再核对 owner/状态/负测及相应原件。实现改动按 host → Release/资源 → 同源码四板 quick P3 → 所需专项 → 复核执行；P3 基础范围不随调试扩张。纯文档切片走文档门禁。

SD、外部示波器或其他夹具按专项需要选择；证据须完整可追溯，但不要求每个任务都重复全部夹具。quick P3、物理精度、resident 连续性与正式产品质量分别关闭。失败/回退保留原件，不用新预算、较晚成功窗口或报告改写旧失败。详细证据统一查 [Task Progress](TDMA_TASK_PROGRESS.md) 及其归档索引。
