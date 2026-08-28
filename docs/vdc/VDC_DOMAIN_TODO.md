# VDC 内部主域待办

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_TODO.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`
Last updated: 2026-08-28

本文档维护 Virtual Distributed Clock / VDC Domain 的独立待办。这里不记录普通开发流水账，只记录会影响共同时间、timestamp、offset/rate、DPLL、HOLDOVER/RELOCK、VDC quality、RefMem 映射和 RUN gate 的架构与实现事项。

2026-08-28 阶段性事实：四节点 TDMA Foundation 已能在固件内同时运行上行/下行并形成参考反馈，NO5 只读监视器、PIO/DMA completion-latch 回收和 VDC/DPLL vector 发布路径已接通；本次五板 OTA 后实测 TDMA `up/down` 稳定且 `rx_bad=0`，但因 active Calibration `PATH_DELAY` 未加载、timestamp eligibility 尚未成立，NO1..NO4 DPLL 仍为 `CHECKING`，不得标记为 `LOCKED` 或 `HEALTHY`。证据与代码事实见 `docs/tdma/TDMA_TASK_PROGRESS.md` 的 `TDMA-PROGRESS-20260828-003`。

## 阶段性长期任务发布：DPLL-LONG-001

VDC 作为 DPLL 闭环的算法与事实发布 owner，按以下依赖顺序推进：

`TDMA 基线 → active calibration matrix → CS/SCK/DATA hardware timestamp → eligible parser/gate → 最小 SyncDpllFB → VdcVector 发布 → NO5 观测验收 → HOLDOVER/RELOCK/故障注入/长稳`。

当前状态为 `ACTIVE / P0`：静态 build、RAM gate、host pytest 已通过；板端水位、四节点长期 TDMA 基线和 active matrix 尚待实测。未完成前，VDC 必须保持 `CHECKING`，不能通过 sample count、默认零延迟表或软件时间戳伪造 `LOCKED`。

阶段性不变量：

- `VdcSyncAO` 只管理 schedule/profile/calibration binding，`SyncDpllFB` 唯一写 offset/rate/lock/DCO，`VdcVector` 只发布稳定快照。
- TDMA/Calibration 是前置域：VDC 不拥有 TDMA scheduler，不按物理环序推算 delay，不绕过 active topology/path-delay/offset matrix。
- 只有同一 TDMA ring sequence 的 hardware-latch evidence 才能进入 DPLL；invalid、duplicate、out-of-order、stale 或 matrix mismatch 只更新 quality/reject 计数。
- NO5 只读观测指定间隔和同时触发，用于验收，不参与 NO1–NO4 的控制环；SD/SVG 仅在 Core0/维护侧离线分析。
- 任何 servo、VDC 或诊断负载必须先通过 TDMA 拍级预算和 DeploymentGate；闭环不得改变 SHORT wire、phase 或 recovery 静态预算。

## 参考项目收敛原则

VDC Domain 可以借鉴成熟时间同步项目和工业 DC 思想，但不直接引入对应协议栈。借鉴关系必须落到 `offset/rate`、jitter、servo reset、HOLDOVER、reference clock、链路 delay、initial sync、drift compensation、timestamp 和验证项上。表驱动、RMA、ACK/NACK 和分布式共同事实参考由 `docs/refmem/REFMEM_DOMAIN_TODO.md` 维护。

| 参考项目 | 可借鉴机制 | VDC 落地边界 |
|---|---|---|
| LinuxPTP / Chrony | offset、rate/frequency、RMS offset、jitter、skew、slew、servo reset、HOLDOVER 质量模型。 | 用于 `VdcDpllState`、`VdcQualityTable`、lock gate、holdover age、rate limit、servo reset 和报告字段；不引入 NTP/PTP 协议栈，不调整系统 wall clock。 |
| SOEM / EtherCAT DC | reference clock、传播延时测量、initial sync、周期性 drift compensation、同步输出/输入 timestamp。 | 用于 DHRT100 自定义 VDC/DC 思想：校准链路 delay，DPLL 形成 DC，预测分发使用共同时间，T2/READY 回读质量；不采用 EtherCAT 协议、ESC 寄存器或硬件 DC 单元依赖。 |
| IEC 61499 | 固定 AO/FB owner、event/data boundary、deployment consistency。 | 用于约束 `VdcSyncAO / SyncDpllFB / HoldoverFB / RelockFB` 的静态 owner 和事件边界；不做动态 FB 部署。 |

## 2026-08-16 HAOFV 对齐评审

总体结论：当前 VDC 主域方向正确，已经避免把 VDC 做成裸 SCPI 域或 RefMem 子函数；`VdcTdmaScheduleProfile`、window class、frame envelope、diagnostic timestamp gate 和 TDMA window planner 都符合“VDC 拥有共同时间，RefMem 只同步共同事实，core1/PIO 只执行硬实时动作”的 HAOFV 主线。但当前实现仍处于主域契约和诊断闭环阶段，尚未形成完整 `VdcSyncAO / SyncDpllFB / VdcVector / VdcQualityGateFB` 运行结构。以下风险必须作为后续 VDC 主线优先级，而不是继续扩大旁路脚本或只读查询。

## DPLL 锁定 / VDC 同步最快闭环优先级

当前 TDMA 基础件已经具备形成 VDC 的最小骨架：active `VdcTdmaScheduleProfile` 定义 observation/data/idle window，RefMem data frame 可以作为 payload class 搭载在 TDMA frame envelope 中，SYNC_IO/core1 latch 可以产出带 timestamp metadata 的本地 IO fact，VDC observer adapter 可以把 fact 展开为 compact observation，再由 timestamp dictionary、wrap tracker 和 DPLL gate 统一裁决。后续优先级必须围绕这条最小实例推进，不能绕过 HAOFV 直接由 SCPI 或脚本伪造 offset/rate/lock。

最小实例定义：

```text
VdcSyncAO
  owns active schedule/profile/dictionary
  reads SyncIO capture facts through observer adapter

SyncDpllFB
  only accepts DPLL_ELIGIBLE timestamp evidence from VDC_OBSERVATION_WINDOW
  only writer of offset/rate/lock/DCO snapshot

VdcVector
  publishes readiness, quality, gate, clock model and DCO snapshot

SCPI maintenance
  configures observer bring-up knobs and reads VdcVector
  never writes lock state, offset, rate or accepted sample counters
```

### 正常 TDMA 环路闭环对接方案

基于当前状态，VDC/DPLL 已经具备 active schedule 视图、VDC sync/idle payload、DPLL gate、clock model、DCO snapshot、节点/leg 长监控和 GPIO overlay HIL 证据；但当前仍主要依赖维护脚本交替触发单向 leg，尚未形成固件内部同时运行的实时反馈环路。这里的架构边界需要明确：`TDMA_UP_LEG` 与 `TDMA_DOWN_LEG` 是 TDMA Foundation 的基础 runtime，不是 VDC 组件；VDC 的目标是消费 TDMA Foundation 产生的 observation timestamp evidence，再由 `SyncDpllFB` 更新 offset/rate/quality。TDMA 主域待办见 `docs/tdma/TDMA_DOMAIN_TODO.md`。

闭环主线按以下阶段推进：

| 阶段 | 目标 | 关键输出 | 验收口径 |
|---:|---|---|---|
| R0 | 从 TDMA Foundation 只读绑定 ring profile 和资源声明。 | TDMA profile 明确节点数、ring 顺序、UP/DOWN 组、slot、guard、period、payload class 和 profile CRC；VDC 只保存 observation 绑定和 CRC。 | 两板、三板、五板都能用同一结构表达，不写死 `COM5/COM6` 或固定物理角色。 |
| R1 | 消费 core1 同时运行的上行组和下行组。 | TDMA runtime 同时 arm TX/RX，输出 `up_running/down_running`、slot seq、miss/late/timeout、direction conflict 和 resource claim evidence。 | VDC 不拥有 runtime；空闲无业务时仍持续运行 `IDLE_BEACON`，host 不参与窗口续装。 |
| R2 | 消费环路 timestamp evidence。 | 每个 ring frame 至少记录 reference TX、每 hop RX/TX、feedback RX、frame CRC、schedule CRC、timestamp source/resolution/flags。 | DPLL accepted sample 必须来自同一圈 TDMA ring evidence，`HARDWARE_TICK / <=100 ns / DPLL_ELIGIBLE / !DIAGNOSTIC_ONLY`。 |
| R3 | 完成 delay measure 与 active path-delay 表。 | 借鉴 EtherCAT DC 的传播延时测量：reference 发起 delay-measure frame，沿 ring 写入本地 timestamp，回到 reference 后计算每 hop delay/jitter/freshness。 | `VdcPathDelayTable` 不再只是默认零延迟；cal/profile CRC 变化会触发 CHECKING/RELOCKING。 |
| R4 | VdcSyncAO 常驻锁相。 | VdcSyncAO 拥有 auto-run 配置，周期性产生 `VDC_SYNC_SAMPLE/IDLE_BEACON`，SyncDpllFB 从 ring evidence 更新 offset/rate/quality。 | 5 min 常驻运行无 host 续窗，quality/freshness 持续刷新，`WINDOW_BOUND/BAD_FRAME` 为 0 或可定位。 |
| R5 | 长监控升级为只读健康评估。 | `vdc_long_monitor` 不再触发闭环，只读取每节点、每 leg、整环 quality、DCO、delay、gate 和 RefMem mirror，并输出 SVG。 | 报告同时给出 node/leg 质量和整环闭环质量，明确区分 `leg_monitor_passed` 与 `loop_health_passed`。 |
| R6 | N 节点 ring 扩展。 | `B0.UP -> B1.DOWN -> ... -> Bn.UP -> B0.DOWN` 的 hop 表、slot 表、delay 表和 quality 聚合。 | 3 节点、5 节点只扩展表项和 schedule，不改变 VDC/DPLL 算法主线。 |

正常闭环中，单向下发只能作为 open-loop reference，不能作为 DPLL 闭环证据；交替 `X->Y` / `Y->X` 只能用于 leg bring-up、线序检查、delay 校准或方向性故障定位。只有 TDMA Foundation 的上行组和下行组在同一固件运行周期内同时工作，并且反馈 evidence 能实时被 VDC owner 消费，才允许 VDC 报告整环闭环输入成立。

| 优先级 | 闭环目标 | 验收证据 |
|---:|---|---|
| P0.0 | 建立 VDC lock readiness 只读入口，明确当前卡在 observer、dictionary、timestamp eligibility、accepted sample、gate 还是 DPLL state。 | `SYSTem:SYNC:VDC:LOCK:READiness?` 和 HIL 脚本能在 COM5/COM6 上报告 `input_ready=0,locked=0,reason=TIMESTAMP_NOT_ELIGIBLE`，且不改变 DPLL 状态。 |
| P0.1 | 把 observer 手工 expected window/base 配置收敛到 active TDMA observation window，并继续接真实 PIO edge latch。 | `SYSTem:SYNC:VDC:OBServer:TDMA` 已按 `VdcTdmaScheduleProfile` 配置 observer 最小实例，并由 manager arm `sync_io` timestamp window；当前低速诊断采样仍报告 `DIAGNOSTIC_ONLY` 并被 gate 拒绝，后续必须由固件内部在 observation window 内预约边沿和采样，不能依赖 PC 串口赶窗口。 |
| P0.2 | 让 `VDC_OBSERVATION_WINDOW` 样本通过 DPLL gate。 | 已将 TDMA observer self-test 收敛到公共 `components/tdma`：TX 侧提交 `VDC_SYNC_SAMPLE` short frame intent，RX 侧可同时周期性 arm observation window 并启动 capture；self-test 不再直接驱动主输出组。TX self-test intent 已开启 `scheduled_window_valid`，公共 TDMA service 使用 TDMA boot-time ns 规划/执行窗口，并通过 near-window arm-ahead 避免轮询碰巧命中 10 us 窗口；`sync_io` capture word timebase 已对齐同一 boot-time ns，避免 PIO/DMA capture latch epoch 与 TDMA schedule epoch 分裂。2026-08-17 已用 COM5/COM6 GPIO overlay accepted-only HIL 验证真实 capture word 可形成 `HARDWARE_TICK / 100 ns / DPLL_ELIGIBLE / !DIAGNOSTIC_ONLY`，并以 `last_gate_reject_code=0` 进入 VDC gate；完整 `HEALTHY/FINE_100NS` 锁相仍归 P0.5。 |
| P0.2a | 解决 COM5/COM6 GPIO overlay HIL 中 `accepted_sample_count=0` 的断点。 | 已纠偏。根因是 `sync_io_capture_dma_produced_words()` 在无限 DMA 模式下把 `transfer_produced == produced_seq == 0` 当成有效前进，导致 ring write-index delta 永远不执行，capture produced/latch/observer 全链路停在 0。修复为只有 `transfer_produced > produced_seq` 才采用 DMA transfer counter，否则继续用 ring write index 推导 produced words。验收：build `20260816175137` 上 COM5->COM6 `accepted=0->1 observer_accepted=0->1`，COM6->COM5 `accepted=0->4 observer_accepted=0->4`，两向均 `source=HARDWARE_TICK resolution_ns=100 flags=DPLL_ELIGIBLE gate=PASS`。 |
| P0.3 | TDMA 基础件形成时间可预期的稳定闭环。 | TDMA 每个 short/long frame window 必须有可计算 start/end/guard/deadline、core1 arm/start/done timestamp、miss/late reason、payload completion 和 retry/fence 策略；VDC self-test 已增加 scheduled window contract 单测和 COM5/COM6 HIL，确认未到 guard/window 时不会提前执行，进入窗口后能完成 `FRAME_READY`；公共 TDMA timestamp spine 已增加 DPLL eligible 前置门禁，只有 `VDC_OBSERVATION` window 内的 `HARDWARE_TICK / <=100 ns / !DIAGNOSTIC_ONLY` VDC sync/idle payload 才能保留 eligible，VDC parser 也要求 DPLL 证据来自 TDMA 执行镜像而不是 frame 内旧 flags；当前 2 ms arm-ahead 是 bring-up 驻留策略，后续需要替换为真正 core1 scheduler/timer 或 PIO/DMA deadline 驱动；RefMem AUTO NodeLoad 不能因一次 `WINDOW_MISSED` 静默丢失 delta，VDC DPLL sample 不能由不稳定 TDMA 维护。 |
| P0.4 | TDMA 作为 HAOFV system node 纳入 NodeLoad/SlotClaim。 | VDC 继续只注册 `VDC_SYNC_SAMPLE` / `IDLE_BEACON` payload，不拥有 16-24 物理通讯环路；TDMA 节点声明 scheduler、PIO transport、DMA、core1 service、short/long frame capacity 和 payload registry，DeploymentGate 检测与 RefMem、业务 overlay 或第二 TDMA owner 的资源冲突。 |
| P0.5 | 实现 `SyncDpllFB` 最小环路滤波器，使用 accepted sample 计算 phase error / frequency error，经 PI 或等价 servo、限幅和 reset policy 调节 VDC clock model 的 offset/rate，并发布 DCO snapshot。 | `INITIAL_SYNC -> FREQ_LOCK -> PHASE_LOCK -> LOCKED` 状态链必须由环路滤波器收敛驱动，而不是 sample count 伪锁定；当前已把 reject 策略拆成软/硬两类：`WINDOW_BOUND` 和 `SERVO_OUTLIER` 不进入 DPLL 更新，但保留 offset/rate/accepted history 并进入 `RELOCKING`，由后续有效样本拉回；CRC、epoch、source、payload、timestamp eligibility/resolution 等硬拒绝才重新捕获。lock state、quality tier 和 offset/error budget 已改为本帧更新前入相残差口径，DPLL 同帧 phase correction 不能冒充 fine lock；默认 bring-up 先按 1 us debug threshold 进入可观测锁定，acquisition 阶段允许更大的受限 phase slew 加快拉入，rate 估计必须跨多个 TDMA 周期并受 slew limit 限制，避免单个异常 delta 直接撞到 `+/-50000 ppb`；后续必须由连续 100 ns fine 样本把 `lock_quality_tier` 晋级到 `FINE_100NS`；`HEALTHY/RUN` 仍要求 100 ns fine tier；报告 lock_time、last/rms/max offset、freq_offset_ppb、period_adjust_ppb、outlier/reset 次数。 |
| P0.5a | 固化 VDC 节点/leg 长监控，并避免把 leg 自测误判为实时闭环。 | 已新增 `tools/vdc_long_monitor/vdc_long_monitor.py`，通过维护侧循环 arm RX/TX `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest` 观察每个节点或单向 TDMA leg；该工具只配置/读取 observer 和模型输出，不写 offset/rate/lock，符合 HAOFV 维护侧边界。2026-08-17 COM5/COM6 180 s 双向交替监控结果：11 个窗口、accepted delta 1496、7 个窗口最终 `gate=PASS`、4 个窗口最终 `VDC_DOMAIN_GATE_WINDOW_BOUND`，采样点 55/67 为 `HARDWARE_TICK/100 ns/DPLL_ELIGIBLE`、12/67 为窗口外或停机后的无效镜像；offset 出现 `553600 ns` 级跳变，说明当前 bring-up 能观察 leg 样本输入，但不能证明实时反馈环路。监控报告必须显式区分 `leg_monitor_passed` 与 `simultaneous_feedback_loop_evidence`。 |
| P0.5b | 将 VDC observation / idle beacon 从 host 周期重装升级为消费 TDMA Foundation 常驻环路。 | 当前空闲态 `READ:SYNC:QUALity?` 会归零，`LOCK:READiness?` 显示 observer disabled，说明 VDC 还不是常驻锁相；60 s 监控中仍有 `WINDOW_BOUND`。下一步由 TDMA Foundation 同时驱动上行组和下行组，周期性提供 `VDC_SYNC_SAMPLE/IDLE_BEACON` timestamp evidence；`VdcSyncAO` 只拥有同步动作、profile 绑定和 DPLL 输入门禁。SCPI 只能启停调试策略和读取镜像，不能承担实时窗口重装或反馈回传。验收：空闲无业务数据时仍持续刷新 quality/freshness，连续 5 min 无 `WINDOW_BOUND` 最终态，observer 不因 host 查询而 disarm，TDMA snapshot 的 `simultaneous_feedback_loop_evidence=true` 且 VDC evidence 可追溯。 |
| P0.5b-0 | 将 `TDMARingProfile` / ring schedule 字段迁出为 TDMA Foundation owner。 | 首版代码契约仍落入 `VdcTdmaScheduleProfile`，作为过渡 observation 视图：包含 ring version、simultaneous UP/DOWN flag、active node count、local/reference index、UP/DOWN group、upstream/downstream/feedback slot、ring profile CRC，并纳入 schedule CRC 和 host 单测。后续应迁入 TDMA profile / System Pack staging 与 DeploymentGate，VDC 只读绑定。 |
| P0.5b-1 | 将两节点反馈环路抽象扩展为 N 节点 TDMA ring。 | 首版已新增 `vdc_domain_plan_tdma_ring()`，可从 active `VdcTdmaScheduleProfile` 读取 local/reference/upstream/downstream/feedback、active node count、from-reference hops、to-feedback hops 和 profile CRC；单测覆盖 8 槽默认环与 5 节点 profile。后续还需把 hop 表、delay 表项、quality 聚合和 reference/failover 表接入 runtime，不允许把每个节点写成独立点对点特例。 |
| P0.5b-2 | 对接 TDMA Foundation 的 core1 ring runtime 同时收发模型。 | 当前 HIL 仍是 host 维护脚本交替 arm 单向 leg。下一步由 TDMA Foundation 管理 UP/DOWN 组的 TX/RX state、PIO/SM/DMA resource、ring slot seq、frame pending、late/miss/timeout 和 direction conflict；VDC 只消费 snapshot/evidence。验收：无业务 payload 时仍持续 IDLE_BEACON；SCPI 查询不会 disarm；runtime snapshot 暴露 `up_running/down_running/ring_seq/last_error`。 |
| P0.5b-3 | 定义 ring frame timestamp evidence。 | 当前 evidence 可记录局部 window 和 observer 状态，但缺少同一圈 ring 的 reference TX、每 hop RX/TX、feedback RX 组合证据。下一步冻结 `VdcRingTimestampEvidence`：包含 ring_seq、hop_id、source/ref slot、reference_time_ns、local_rx_tick、local_tx_tick、feedback_rx_tick、path_delay_id、frame/schedule CRC、timestamp source/resolution/flags。验收：DPLL accepted sample 必须能追溯到同一圈 ring evidence。 |
| P0.5b-4 | 实现 delay-measure frame 与 active path-delay 更新流程。 | 已有 `VdcPathDelayTable` active 表和只读查询，但 delay 仍是默认/静态。借鉴 EtherCAT DC 环路测时思想，reference 发起 delay-measure frame，沿途节点写入硬件 timestamp，回到 reference 后计算每 hop delay/jitter/freshness，并由 Calibration/VDC staging 激活。验收：任一 hop delay 失效、cal CRC 改变或 freshness 超限时，VDC 进入 CHECKING/RELOCKING，不沿用旧 lock。 |
| P0.5b-5 | 将 VDC long monitor 从触发工具升级为只读健康评估。 | 当前 `vdc_long_monitor` 已改为声明 `LEG_SELFTEST_MONITOR`，但仍可触发单向 self-test。后续真正环路运行后，该工具默认只读 `VdcVector`、TDMA ring runtime、PathDelayTable、DCO、quality 和 RefMem mirror；触发 self-test 只能作为显式 bring-up 模式。验收：报告输出 node/leg 质量与整环质量，`loop_health_passed` 只由固件内同时运行 ring evidence 决定。 |
| P0.5b-6 | 建立正常环路闭环 HIL 验收脚本。 | 在两板最小系统上先验证真实同时运行环路，再扩展到 3/5 节点规划。脚本必须先检查 ring profile、UP/DOWN runtime、timestamp source、path delay freshness 和 VDC lock readiness，再进入长时间采样。验收：5 min 常驻运行中 `up_running=1`、`down_running=1`、`simultaneous_feedback_loop_evidence=true`、`BAD_FRAME=0`、`WINDOW_BOUND` 不作为最终态、quality/freshness 连续刷新，并在 `docs/temp/vdc_long_monitor/` 输出 summary + curve SVG。 |
| P0.5c | 定位并纠偏 `BAD_FRAME=12`。 | 2026-08-17 build `20260816183723` 的 60 s COM5/COM6 监控中出现 `VDC_DOMAIN_GATE_BAD_FRAME`，该问题优先于 servo 调参。需记录 reference sync block、schedule CRC、frame CRC、payload CRC、window class、payload class、source/reference slot 和 TDMA execution mirror，确认是 frame build/parse、CRC、reference time 语义还是窗口镜像不一致。验收：VDC 自动运行 5 min 内 `BAD_FRAME` 计数为 0；若发生必须能从低频 log 定位到具体字段。 |
| P0.5d | 在 evidence 连续稳定后再调 DPLL servo。 | 当前硬件 timestamp 输入和 accepted path 已通，但 gate 仍有 `WINDOW_BOUND/BAD_FRAME`，`freq_offset_ppb` 会打到 `+/-50000` 限幅，`FINE_100NS` 不连续。禁止在帧/窗口稳定前用滤波器参数掩盖问题；servo 调参前必须先满足连续 observation/idle beacon、BAD_FRAME 为 0、gate pass 稳定。验收：先达到 1 us 调试锁连续保持，再逐步收紧到 100 ns fine tier；每次参数变化必须记录 lock_time、RMS/peak offset、outlier ratio 和 rate saturation。 |
| P0.5e | 增加 VDC 低频维护 log，支撑锁相异常定位。 | 需要周期记录 `LOCAL_TIME/base_local_tick64`、`phase_offset_ns`、`period_adjust_ppb`、`PATH_DELAY`、`LOCK_STATUS/quality`、gate/reject code、window_start/observed timestamp、DCO seq、frame/schedule CRC 和 source/reference slot。日志由维护主域低频写入 RAM/SD，不进入 hard realtime path，不由 SCPI 轮询参与锁相。验收：出现 `WINDOW_BOUND/BAD_FRAME/offset jump/rate saturation` 时，能从日志还原对应窗口、帧和 DCO snapshot。 |
| P0.6 | core1 只读稳定 DCO snapshot，形成 VDC 同步输出/预约触发基础。 | 已增加 core1 DCO consumer mirror 和 `SYSTem:SYNC:VDC:DCO?` 只读查询，能验证 DCO snapshot valid、update seq、lock_state、phase/rate、CRC 和 invalid/stale 计数；并修复 rejected sample 后 DCO lock_state 不随 DPLL 回落的问题。下一步 core1 需要用该稳定 DCO 装载 FIRE_LOAD 或同步输出。 |
| P1 | 将 VDC quality/error budget 纳入 RUN gate，并把 VDC snapshot 映射到 RefMem。 | VDC unlocked/质量不达标时 RUN/FIRE_LOAD 被拒绝；LOCKED 后 RefMem 只镜像 VDC 共同时间事实。 |
| P1 | 诊断运行策略收敛到 VdcSyncAO command/config。 | 调试阶段允许 SCPI 启停 VDC observer/self-test 诊断；产品阶段由配置决定是否随系统自动运行。无论启停来源如何，实时诊断循环必须在内部运行并发布 VdcVector/诊断镜像，SCPI 查询只读当时镜像，不能参与 lock/offset/rate 实时计算。 |

当前主线结论：先完成 P0.0-P0.3，再回到 RefMem 表模型扩展；否则表模型继续扩大会超过当前两板硬件闭环的验证能力。

DPLL 稳定性要求：

- `SyncDpllFB` 是 offset/rate/lock/DCO snapshot 的唯一 writer；SCPI 调试命令只能写 staging profile 或易失调试覆盖，不能直接写 clock model。
- 环路滤波器首版至少要覆盖 `phase_error_ns`、`frequency_error_ppb`、`kp/ki`、`slew_limit_ppb`、`step_threshold_ns`、`outlier_threshold_ns` 和 `reset_policy`。
- accepted sample 进入后必须先通过 outlier/sanity gate，再进入环路滤波器；超限样本只能更新 quality/gate evidence，不能污染 offset/rate。
- `VdcClockModel.period_adjust_ppb`、`phase_offset_ns`、`slew_limit_ppb` 和 `VdcDcoControl` 必须来自同一个稳定 snapshot seq，core1 只能读取完整 snapshot；当前 accepted evidence 派生 DCO 时已保存并恢复单调 `dco_update_seq`，`slew_limit_ppb` 跟随 active servo sanity limit，core1 DCO consumer mirror 能暴露 accepted/unchanged/invalid 计数和最后消费的 DCO seq。
- bring-up profile 默认把 LOCKED 接纳阈值设为 1 us；维护阶段必要时可临时放宽到 10 us，但 `VdcQualityTable.lock_quality_tier` 必须按连续稳定样本明确区分 `COARSE_10US / DEBUG_1US / FINE_100NS`；只有 `FINE_100NS` 才能进入正式 `HEALTHY/RUN` 质量门禁。

EtherCAT DC-style 对照缺口：

- [x] 已具备本地 DC clock model 雏形：`base_local_tick64 + phase_offset_ns + period_adjust_ppb` 可以表达 `LOCAL_TIME + OFFSET + DRIFT_CORR`。
- [x] 已具备 reference slot / observation window / `VDC_SYNC_SAMPLE` payload 的 TDMA 骨架。
- [x] 冻结 reference sync frame 的时间语义首版：VDC sync/idle TDMA short frame envelope 已显式携带 `reference_time_ns`、`next_frame_start_ns`、`reference_seq_id`、`reference_frame_id`、`reference_sync_slot_id`、schedule CRC 和 frame CRC；frame gate 要求 `reference_time_ns == window_start_ns`、`next_frame_start_ns == window_start_ns + period`，不能只依赖窗口计划推断 reference time。
- [ ] 冻结 delay-measure frame：reference 发起回环测量，沿途节点写入本地 hardware timestamp，reference 回环后计算每个 slot 的 `PATH_DELAY`，再作为 active calibration/VDC profile 下发。
- [x] 将 `PATH_DELAY` 从单帧 evidence 字段升级为 active 表首版：`VdcPathDelayTable` 包含 source/reference slot、方向、delay_ns、jitter/stddev、cal_crc、freshness、writer、update_seq 和 table CRC；默认 8 slot 零延迟表用于 bring-up，`SYSTem:SYNC:VDC:PATH:DELay?` 只读查询 active entry。
- [x] DPLL phase error 公式已在 compact observation/context 路径显式使用 active `PATH_DELAY`：`T_local_rx - (T_reference + PATH_DELAY)` 形成入相误差，再由现有 `corrected_phase_error` 加 `OFFSET`；无 active table 的旧展开 API 保持零延迟兼容语义。
- [ ] 补齐 PATH_DELAY 失效策略：cal CRC、freshness、reference/source slot 变化或 delay-measure 失败时必须让旧 lock 失效并进入 `CHECKING/RELOCKING`。
- [ ] core1 TDMA scheduler 必须消费 DCO snapshot 形成 `T_effective`，用共同时间预约 frame/slot 起点；当前只完成窗口计划和部分 data-window 执行，尚未由 DCO 反驱 TDMA 边界。
- [ ] 低频维护 log 需要周期记录 DC 五元组：`LOCAL_TIME`、`OFFSET`、`DRIFT_CORR/period_adjust_ppb`、`PATH_DELAY`、`LOCK_STATUS/quality`，用于后续锁相优化和异常调查。

TDMA 最小实例约束：

- `components/tdma` 是唯一 TDMA scheduler / PIO transport / timestamp spine 基础件；VDC 不建立私有 TDMA 实现，只注册/消费 `VDC_SYNC_SAMPLE`、`IDLE_BEACON` 等 payload，并读取 TDMA 产生的 scheduled window 与 timestamp evidence。
- TDMA timestamp spine 是 DPLL evidence 的第一道公共门禁：adapter 上报的 `DPLL_ELIGIBLE` 只有在 active scheduled `VDC_OBSERVATION` window、VDC sync/idle payload、`HARDWARE_TICK`、resolution `<=100 ns` 且非 diagnostic-only 时才允许保留；其他 window、RefMem/data payload、unwindowed frame 和 diagnostic timestamp 必须清除 eligible。
- TDMA 后续必须被封装为可装载 HAOFV system node / FB instance，通过 RefMem 9 表 staging 激活；VDC 只能依赖 active TDMA node 暴露的 schedule、window、timestamp spine 和 payload registry，不能直接 claim GPIO16-24 或 PIO/DMA 物理资源。
- GPIO16-24 是 TDMA 基础通讯环路，只承载 TDMA transport 和其 payload；GPIO4-7 是最小系统业务/观测 overlay，不能反向复用为 TDMA transport，也不能把 TDMA 物理线当作模型 IO 自测线。
- TDMA 稳定性是 DPLL/VDC 的前置条件：每一帧必须先维护共同时间骨架，再搭载 payload；`WINDOW_MISSED`、late、timeout、CRC/drop 必须进入 quality/evidence，并驱动 retry、fence 或 holdover，而不能被当成普通日志。
- `SYSTem:SYNC:VDC:OBServer:TDMA` 只允许从 VDC active schedule 读取 `VDC_OBSERVATION_WINDOW` 并配置 observer，不允许由 SCPI 直接提供或修改 DPLL lock evidence。
- `TDMA_WINDOW_BASE` 只能说明 compact observation 的 expected/base 时间来自 active TDMA 计划；`sync_io` timestamp window 使用 capture timer 同一时间基 arm，支持按 TDMA period 周期性匹配，只有整个 capture word 落在某个 observation window 内才可能去掉 `DIAGNOSTIC_ONLY`，并且 compact observation 必须使用命中的窗口起点作为 expected window。
- 只有 PIO/DMA/IRQ/core1 在 observation window 内形成真正 edge latch，且 timestamp metadata 为 `HARDWARE_TICK + DPLL_ELIGIBLE + !DIAGNOSTIC_ONLY`，VDC gate 才能接受样本；bring-up admission 可临时接纳 `resolution<=1000ns` 的 coarse/debug lock，但产品 `HEALTHY/RUN` fine gate 仍必须达到 `resolution<=100ns`。
- 两板最小闭环必须增加固件内部预约测试边沿：一块板输出 PIO/DMA reference pulse train，另一块板在周期性 TDMA observation window 内采样并提交 observation；PC/SCPI 只能配置和读取结果，不能承担 10us 窗口内的实时动作。
- 调试最小系统的锁相观测临时走 GPIO4-7 业务/观测 overlay：X 板用 `sync_io_model_pulse_schedule_arm()` 在 GPIO4 等 overlay 线上装载 PIO/DMA reference pulse train，Y 板用 `sync_io` capture 观测 GPIO4-7 并把 word-level timestamp fact 交给 VDC observer。GPIO16-24 继续只归 TDMA 通讯环路。最终产品不保留 GPIO4-7 锁相观测依赖，锁相稳定后必须迁移到 TDMA 通讯帧/时间戳证据。

### 符合 HAOFV 的部分

- [x] VDC 已被定义为 HAOFV 内部基础主域，和 Distributed RefMem 并列；没有新增裸 `VDC:*` / `DPLL:*` 顶级 SCPI 域。
- [x] RefMem -> VDC 的桥接保持单向证据语义：RefMem 只提供 frame/payload/CRC/timestamp evidence，不计算 offset/rate，不发布 VDC lock。
- [x] `time_us_64()*1000` 诊断时间戳已明确标记为 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY`，不能进入 100 ns DPLL lock gate。
- [x] TDMA window class 已区分 `VDC_OBSERVATION_WINDOW`、`REFMEM_DATA_WINDOW` 和 `IDLE_BEACON`，普通 RefMem 数据不能进入 observation window。
- [x] 已建立 `components/tdma` 基础件首版；RefMem 旧 `refmem_realtime_tdma` 已降为适配层，后续 VDC observation、RefMem delta/ack/fence 和 long frame payload 必须挂到同一 TDMA scheduler。
- [x] `vdc_domain_plan_tdma_window()` 已把窗口相位、guard、wait、late 和 missed 计算收敛到 VDC owner 侧，SCPI 只读查询不提交 intent。
- [x] VDC `VDC_SYNC_SAMPLE` / `IDLE_BEACON` 已通过 `vdc_tdma_payload_register()` 注册到 `components/tdma` 公共 payload registry；VDC 不拥有私有 TDMA scheduler，只编解码 payload、校验 schedule/window/CRC，并从 TDMA snapshot 派生 timestamp evidence。
- [x] `VdcDcoControl` 已有结构契约，后续 core1/PIO 可以只读稳定 snapshot，不需要直接访问 DPLL 内部状态。

### 必须纠偏的待办

| 优先级 | 风险项 | 当前状态 | 纠偏目标 |
|---:|---|---|---|
| P0 | `VdcSyncAO / SyncDpllFB / VdcVector` 尚未完全实体化。 | 已新增 `vdc_sync_ao_service()`、`sync_dpll_fb_service()` 和 `tdma_component_core1_service()` 语义入口，core1 realtime loop 已改用新入口；旧 `vdc_dpll_manager_*_service()` 保留为兼容 wrapper。当前 `vdc_domain.c` 仍是单体 domain core，`vdc_dpll_manager` 仍承载集成层、observer/self-test 和 snapshot 发布。 | 继续建立 VDC AO/FB 内部边界：SCPI/System 只能投递 command/event，`VdcSyncAO` 拥有 schedule/profile/cal binding，`SyncDpllFB` 唯一写 offset/rate/lock，`VdcVector` 发布稳定 snapshot。 |
| P0 | TDMA plan 未被 core1/PIO 消费。 | `components/tdma` 已成为唯一 scheduler/transport/timestamp spine；RefMem 通过 `refmem_tdma_payload_register()` 注册 DELTA/ACK_FENCE，VDC 通过 `vdc_tdma_payload_register()` 注册 SYNC_SAMPLE/IDLE_BEACON；NodeLoad AUTO TX/RX 会从 VDC manager 获取 `REFMEM_DATA_WINDOW` 计划，core1 service 已能在 guard 前保持 pending、窗口错过时拒绝、进入 payload window 后执行。 | 继续把该路径接到真实 PIO timestamp latch，并把 `VDC_OBSERVATION_WINDOW` 的 SYNC sample 变成正式 DPLL 输入。 |
| P0 | TDMA payload completion 仍不够可靠。 | COM5/COM6 回归曾观察到 AUTO NodeLoad 首次 B->A 只应用 1/2 帧，TDMA snapshot 记录 `WINDOW_MISSED`；重跑通过说明链路可用，但当前仍缺少强 completion。 | TDMA/RefMem 必须补 ACK/重发/fence completion 和故障注入，保证 missed window 不会造成静默丢帧；该项优先于继续扩大 VDC DPLL 锁定算法。 |
| P0 | 缺少硬件 timestamp latch。 | `timer1/CLK_SYS` 已抽成共享 `VdcTimestampClock`，SyncIO 与 TDMA 共用同一硬件 tick；TDMA PIO-SPI ring 已能发布非零 `HARDWARE_TICK` 诊断 timestamp，250 MHz 下 `timestamp_resolution_ns=4`，但当前仍是 CPU 读取时刻，flags 保持 `DIAGNOSTIC_ONLY`。 | 增加 PIO/DMA/IRQ/core1 边沿 latch，正式 DPLL sample 必须 `timestamp_source=HARDWARE_TICK`、`timestamp_resolution_ns <= 100` 且非 diagnostic-only。 |
| P0 | DPLL servo 仍需从 domain 单体中拆成 `SyncDpllFB`。 | 当前 `vdc_domain_submit_tdma_evidence()` 已根据 phase/frequency error 更新 `period_adjust_ppb` / `phase_offset_ns`，并发布 DCO snapshot；但实现仍位于 `vdc_domain.c` 单体内部，尚未形成独立 `SyncDpllFB` action/state table 和 profile staging。 | 将现有 PI/servo、outlier、step/slew、sanity limit、reset 和 staged profile 收敛到 `SyncDpllFB` 组件边界，保持 offset/rate/lock/DCO 唯一 writer。 |
| P0 | VDC 还不是固件内部连续锁相。 | 当前依赖维护脚本重复 arm self-test；空闲态 quality 归零，observer disabled；长监控出现 `WINDOW_BOUND`。 | `VdcSyncAO` 持有 auto-run 配置，core1 TDMA scheduler 连续产生 observation/idle beacon；SCPI 只读镜像或启停调试策略，不参与实时续窗。 |
| P0 | VDC frame gate 偶发 `BAD_FRAME=12`。 | 60 s COM5/COM6 监控中出现 `BAD_FRAME`，说明 reference sync frame、CRC、payload/window class 或 TDMA execution mirror 仍有偶发不一致。 | 在调 servo 前先补字段级 evidence 和低频 log，确认并消除 BAD_FRAME；自动运行 5 min 内 BAD_FRAME 必须为 0。 |
| P0 | DPLL 调参前置条件未满足。 | 目前 `freq_offset_ppb` 曾可达 `+/-50000` 限幅，`FINE_100NS` 不连续，且 gate 仍有窗口/帧拒绝；代码已先加入 rate 多周期估计和 slew limit，用于抑制单样本频率跳变，但仍需两板长测复核。 | 先稳定 evidence，再调 servo；参数变化必须输出 lock_time、RMS/peak offset、outlier ratio、rate saturation，避免用滤波器掩盖帧/窗口问题。 |
| P0 | VDC 缺少足够低频维护 log。 | 目前问题定位依赖 SCPI 快照和 host CSV，不能完整回放 window/frame/DCO 关系。 | 维护主域周期记录 DC 五元组、gate、window、observed timestamp、DCO seq 和 CRC 字段，异常时可直接定位 `WINDOW_BOUND/BAD_FRAME/offset jump` 来源。 |
| P1 | VDC snapshot 尚未发布到 RefMem `VdcSlot`。 | RefMem application model 有 VDC/DPLL 区域字段，但 VDC 主域未按唯一 writer、guard、sequence/CRC 发布。 | 定义 `VdcSlot` 字段级 contract，VDC owner 写 snapshot/quality/fault/evidence，RefMem 只镜像和同步。 |
| P1 | RUN gate 未消费 VDC quality/error budget。 | RUN gate 已有 RefMem quality 约束，VDC lock/holdover/error budget 尚未进入统一门禁。 | `VdcQualityTable`、`VdcErrorBudget`、`VdcGateResult` 接入 SystemManager RUN gate 和 report evidence。 |
| P1 | `CONFigure:SYNC:VDC:DPLL`、`SYNC:*` 仍是 accepted stub 或固定回复。 | SCPI 入口未进入 VdcSyncAO command/event，也未写 staging profile。 | `CONFigure:SYNC:*` 写 VDC/System Pack staging，`SYNC:CHECk/STARt/STOP/RELock/HOLDover` 进入 VdcSyncAO event，查询只读 snapshot。 |
| P1 | `READ:SYNC:*?` 仍大量固定返回值。 | 产品读接口没有完全来自 VDC snapshot、quality、gate 和 RefMem freshness。 | 产品测试上位机读取路径必须覆盖 lock state、quality、active profile CRC、gate reject、holdover age、reference slot 和 evidence index。 |
| P2 | `VdcReferenceClockTable` / failover 尚未冻结。 | 默认 profile 固定 reference slot，板端 `PLAN?` 当前显示 local/reference 均为默认 0。 | 首版可固定 A0，但必须形成表结构、priority、candidate、current source、loss/failover reason 和 SlotClaim/NodeLoad 绑定。 |
| P2 | `VdcCalibrationBinding` 尚未接入。 | active link delay、cal CRC 变化不会驱动 VDC relock 或 invalidation。 | VDC 只消费 Calibration active result；cal CRC/delay 变化后旧 lock/check 失效，进入 CHECKING/RELOCKING 并记录 gate evidence。 |
| P2 | DCO snapshot 未形成跨核稳定消费路径。 | C 结构已存在，但 core1 还没有 seqlock/double-buffer 读取、半更新拒绝和 stale 策略。 | 增加 DCO shared snapshot guard，core1 只读稳定 DCO，late/半更新/stale 时拒绝 FIRE_LOAD。 |
| P2 | `IDLE_BEACON` 未落地。 | 无业务数据时没有持续 observation/freshness 样本。 | 在 TDMA cycle 中加入 idle beacon 或等价 sync frame，维持 DPLL sample freshness 和 holdover age。 |
| P3 | VDC 代码组件化不足。 | `vdc_timestamp` 已先行拆出，`clock_model`、`dpll`、`quality` 仍在 `vdc_domain` 单体内。 | 继续拆分 `vdc_clock_model`、`vdc_dpll`、`vdc_quality`，并保留 `vdc_domain` 作为聚合/owner API。 |

### 后续执行顺序建议

1. 先由 TDMA Foundation 冻结 `TDMARingProfile`：节点数、ring 顺序、UP/DOWN 组、slot、guard、payload class、profile CRC 和资源声明。
2. 再由 TDMA Foundation 让 core1 TDMA runtime 同时运行 UP/DOWN 两组：先空闲 `IDLE_BEACON`，再挂 `VDC_SYNC_SAMPLE` 和 RefMem payload。
3. 由 TDMA Foundation 补齐 ring timestamp evidence：reference TX、每 hop RX/TX、feedback RX、schedule/frame CRC、timestamp source/resolution/flags；VDC 只消费合格样本。
4. 实现 delay-measure frame 和 active `VdcPathDelayTable` 更新/失效策略，避免 DPLL 用默认零延迟长期运行。
5. 让 `VdcSyncAO` 常驻锁相，`SyncDpllFB` 只消费 ring evidence，core1 只读 DCO snapshot 驱动后续 TDMA/FIRE_LOAD。
6. 把 `vdc_long_monitor` 切成默认只读健康评估；self-test 只保留为显式 bring-up 模式。
7. 两板正常环路闭环 5 min 通过后，再规划 3 节点、5 节点 ring 扩展；扩展只增加 hop/slot/delay/quality 表项，不改 VDC/DPLL 主算法。

## P0 - 文档主域建立

- [x] 建立 `docs/vdc/README.md`。
- [x] 建立 `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`。
- [x] 建立 `docs/vdc/VDC_DOMAIN_TODO.md`。
- [x] 建立 `docs/vdc/VDC_TASK_PROGRESS.md`。
- [x] 更新 `docs/README.md`，加入 VDC 主域目录和三件套入口。
- [x] 更新 `docs/arch/README.md`，在阅读顺序中加入 VDC 主域。
- [x] 更新 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`，把 `vdc/` 从 sync 中拆出为内部主域目录。

## P1 - HAOFV 主域升级

- [x] 修改 `docs/arch/HAOFV_ARCHITECTURE.md`，把 VDC 与 RefMem 并列为 HAOFV 内部基础主域。
- [x] 修改 `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`，把 `task_vdc_sync` 描述为当前任务壳承载 `VdcSyncAO / SyncDpllFB / VdcVector`。
- [x] 修改 `docs/arch/RTOS_HAOFV_TODO.md`，将 VDC/DPLL 实施项收敛为 VDC Domain 待办。
- [x] 修改 `docs/arch/HAOFV_MAINTENANCE_TODO.md`，增加“VDC 内部主域建设”维护项。
- [x] 修改 `docs/sync/README.md`，明确 `sync/` 是 SYNC 动作和 IO 落地，不再作为 VDC canonical。
- [x] 修改 `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`，明确 RefMem 只保存 VDC snapshot，不计算 VDC。

## P1.5 - 外部时间同步参考机制工程化收敛

- [x] 收敛 LinuxPTP / Chrony 借鉴项：offset/rate/frequency、RMS offset、jitter、skew、slew、servo reset、HOLDOVER age。
- [x] 收敛 SOEM / EtherCAT DC 借鉴项：reference clock、传播 delay、initial sync、周期性 drift compensation、同步输出/输入 timestamp。
- [x] 收敛 IEC 61499 对 VDC owner/FB 边界的借鉴项：静态 AO/FB、event/data boundary、deployment consistency。
- [x] 在 `VDC_DOMAIN_ARCHITECTURE.md` 增加虚拟 DC 时钟参考机制矩阵，明确“借鉴机制 / 不采用内容 / 本项目落地点”。
- [ ] 在 `HAOFV_VDC_DPLL_ARCHITECTURE.md` 增加 VDC/DPLL 质量字段与 PTP/Chrony servo 字段映射。
- [ ] 在 `SYNC_IO_ARCHITECTURE.md` / VDC 文档之间补齐 EtherCAT DC-style 的 initial sync / drift compensation / holdover 检查链。
- [ ] 在 `SCPI_COMMAND_PLAN.md` 确认 `READ:SYNC:*?`、`SYSTem:SYNC:VDC:*` 暴露字段能够覆盖 offset/rate/quality/gate/evidence。

## P2 - VDC 数据契约冻结

- [x] 冻结 `VdcClockModel` 字段、单位、writer、reader 和 snapshot 策略。
- [x] 冻结过渡期 `VdcTdmaScheduleProfile`，覆盖 TDMA 周期、同步窗口、guard、reference slot、schedule version、`TDMARingProfile` 和 CRC；后续 ring owner 迁入 TDMA Foundation，VDC 只读绑定 observation profile。
- [x] 冻结 TDMA frame envelope：每一帧都必须先表达 schedule_epoch、slot_index、frame_seq、source/reference slot、timestamp evidence、schedule CRC、frame CRC 和 payload class；RefMem 数据只能作为 payload class 搭载。
- [x] 冻结 TDMA window class：`VDC_OBSERVATION_WINDOW` 是高优先级 DPLL 样本窗口，`REFMEM_DATA_WINDOW` 在同一 TDMA Foundation 骨架上搭载共同事实同步；两者必须分别有 slot/action、guard、CRC、quality 和禁止项。
- [x] 冻结 `VdcTDMATimestampEvidence`，字段单位统一为 ns，至少覆盖 expected_window_start、arm/start/done/apply timestamp、late_ns、jitter_ns、schedule_crc32、frame/sample CRC 和 `timestamp_resolution_ns`。
- [ ] 冻结 `VdcReferenceClockTable`，首版固定 A0，后续支持 priority / failover。
- [x] 冻结 `VdcDpllState` 字段、单位、writer、reader 和 snapshot 策略。
- [x] 冻结 `VdcDcoControl`，覆盖 DPLL 输出到 core1/PIO 的 `base_local_tick64`、`base_vdc_time64_ns`、`period_adjust_ppb/rate_q32`、`phase_offset_ns`、`slew_limit_ppb` 和 `dco_update_seq`。
- [x] 冻结 `VdcServoProfile`，覆盖 servo_type、kp/ki、update period、step threshold、sanity limit 和 reset policy。
- [x] 冻结 `VdcQualityTable` 字段、质量窗口、统计口径和 RUN gate 门限。
- [x] 冻结 `VdcErrorBudget`，覆盖 offset RMS/max、frequency skew、path delay、dispersion 和 root distance 等价字段。
- [x] 冻结 `VdcTimestampDictionary` 表格式、CRC 和版本兼容；System Pack 导入策略另列 P7。
- [x] 冻结 `VdcWrapTracker` 的 tick 扩展规则和回绕安全差分；完整 seq_delta 扩展随 capture ring 落地继续补齐。
- [x] P2-MATRIX-01 冻结 `VdcObservationPathMatrix`：Calibration snapshot 导入阶段由完整 directed link 集合生成 source/reference 多跳路径矩阵，矩阵完整性、generation/freshness 和 table CRC 一并门禁；DPLL 运行态只允许确定性矩阵索引，禁止查找失败后沿物理环累加或使用默认 delay。
- [ ] 冻结 `VdcCalibrationBinding`，定义 active cal CRC、link key、delay_ns 和失效策略。
- [ ] 冻结 `VdcDcSyncPipeline`，定义 reference select、initial sync、drift compensation、LOCKED publish 和 T2 validation 阶段。
- [ ] 冻结 `VdcDisciplineModel`，定义 aging compensation、temperature compensation、wander、holdover drift bound、discipline window 和持久化 profile seq。
- [x] 冻结 `VdcGateResult`，定义 reject code、reject node、reject evidence 和 last pass tick。
- [x] 在 `VDC_DOMAIN_ARCHITECTURE.md` 融合 TDMA + DPLL 三环模型，明确 TDMA 只提供确定性观测窗口，DPLL 是 offset/rate 唯一 writer，低频驯服环只更新长期漂移和 HOLDOVER 误差预算。

## P3 - DPLL / Clock Model

- [x] 实现 `local_tick -> vdc_time64_ns` 映射函数。
- [ ] 将现有正向映射收口为 `vdc_from_local(raw_tick, map_generation)`，并实现
  `local_from_vdc(target_vdc, stable_snapshot)` 反向映射；两者必须返回/校验 epoch、generation、
  quality 和 overflow/rounding 状态，增加定点 round-trip、wrap、负 offset 和 rate correction 单测。
- [ ] 发布稳定 `VdcMapSnapshot` 并接入 core1 消费。
  snapshot 至少覆盖 local/VDC anchor、rate、phase slew、quality、calibration/schedule CRC、epoch 和
  generation；读取必须使用 seqlock、双缓冲或等价 guard，禁止半新半旧参数产生 deadline。
- [x] 实现 SYNC DPLL offset/rate 更新首版：`vdc_domain_submit_tdma_evidence()` 在 accepted hardware sample 后通过 `kp_q16` 生成 phase correction、按 `step_threshold_ns` slew、按 sample period 估计 frequency error，并在达到 lock sample 后用 `ki_q16` 对持续 phase error 产生 rate pull；结果发布到 `VdcClockModel` / `VdcDcoControl`，并记录 frequency error / skew。
- [x] 实现 TDMA observation window 输入门禁：只有来自 active schedule、正确 reference slot、正确 schedule CRC 和同步窗口内的 timestamp sample 才能进入 DPLL。
- [ ] 在 COM5/COM6 已验证 RefMem data TDMA 环路基础上，增加两板帧级 timestamp bring-up：每个 `REFMEM_DELTA/ACK/FENCE/QUALITY/IDLE_BEACON` 帧都产生 TDMA/VDC timestamp evidence；高优先级 `VDC_OBSERVATION_WINDOW` 形成正式 `VdcDpllSample`，再反向或双向测量 delay/evidence。当前已具备 PIO raw capture word -> core1 latch ring -> manager observer -> compact observation -> dictionary -> wrap tracker -> evidence gate 的 VDC 入口，并可通过 `REALtime:IO:SAMPle:LATCh?` 查询 latch 状态、通过 `SYSTem:SYNC:VDC:OBServer` 安全关闭/显式配置 observer、通过 `SYSTem:SYNC:VDC:OBServer?` 读取 observer 状态、配置 CRC、dictionary CRC、edge index 和 dictionary 展开证据；observer 默认关闭，latch 时间基已升级为 `timer1/CLK_SYS` 的 `HARDWARE_TICK / <=100 ns / DIAGNOSTIC_ONLY`，默认 bring-up dictionary 和 open-anchor wrap 已接入，COM5/COM6 已验证 forced edge 会 `submitted=1,accepted=0,rejected=1,gate=TIMESTAMP_NOT_ELIGIBLE`；真实 PIO edge latch 和配置来源尚未接入。
- [x] 增加 RefMem data frame 到 VDC TDMA frame envelope 的桥接基础件：`DELTA/ACK_NACK/FENCE/QUALITY` 可以映射到 `REFMEM_DATA_WINDOW` 并带诊断 timestamp evidence；该桥接不得计算 offset/rate，也不得把诊断 timestamp 标成 DPLL eligible。
- [x] 增加 VDC sync/idle payload 到公共 TDMA 的挂载基础件：`VDC_SYNC_SAMPLE` 和 `IDLE_BEACON` 使用统一 TDMA short frame registry，payload parse 会把 TDMA snapshot 映射为 VDC evidence；当前软件时间戳仍被 gate 为 diagnostic-only，不能进入 DPLL lock。
- [x] 将当前 TDMA snapshot 的软件时间戳明确限定为诊断 evidence：若来源为 `time_us_64()*1000`，必须暴露 `timestamp_resolution_ns=1000`，不得进入 100 ns DPLL lock gate。
- [x] 将 RefMem TDMA TX/RX 从“收到 intent 后立即执行”升级为按 `VdcTdmaScheduleProfile` 的 `REFMEM_DATA_WINDOW` 执行：core1/PIO 必须等待窗口、记录 late/jitter，并在窗口外明确返回 gate evidence；首版已落地 `vdc_domain_plan_tdma_window()` 和 `SYSTem:SYNC:VDC:TDMA:PLAN?` 只读计划查询，`refmem_realtime_tdma` intent 已携带 VDC data window plan，core1 service 会在 guard 前等待、错过 payload window 时返回 `WINDOW_MISSED`。后续 HIL 需要确认 COM5/COM6 真实 25 MHz PIO 环路中的窗口 wait/late evidence。
- [ ] 增加真实硬实时 timestamp latch 路径：PIO/DMA/IRQ 在 RX/TX/输出/输入实际边沿锁存
  `local_tick_raw`，core1 只搬运 descriptor，随后由 VDC 映射为 ns；DPLL 正式样本继续服从已登记的
  hardware-latch 质量门禁。VDC 侧已有 `VdcSyncIoAdapter`、compact sample 和共享 clock；当前
  TDMA 时间仍来自 CPU 读取完整包时刻并标记 diagnostic-only，不能作为训练闭环或 T2 actual。
- [ ] 实现 DCO snapshot 生成：DPLL 输出通过 `VdcDcoControl` 提交给 core1，core1 只读稳定 snapshot 并执行 slew/phase pull；当前 VDC owner 已在 accepted sample 后派生 DCO snapshot，core1 稳定读取和 slew/phase pull 尚未接入。
- [x] 实现 outlier gate、jitter window、phase error 和 frequency error 统计首版：DPLL accepted path 更新 RMS/max/jitter/frequency，servo outlier 会以 `VDC_DOMAIN_GATE_SERVO_OUTLIER` 拒绝样本并写入 quality/evidence；当前 `WINDOW_BOUND/SERVO_OUTLIER` 属于软拒绝，不清空 clock model 和 accepted history，硬拒绝才重新捕获。
- [ ] 实现 servo reset 策略，区分 step reset、profile reset、calibration reset 和 fault reset。
- [ ] 实现 reference node 选择和 reference loss/failover 记录；首版可固定 A0。
- [ ] 实现 error budget 累积，HOLDOVER 中按 drift bound 增长 dispersion。
- [x] 实现 `INITIAL_SYNC -> FREQ_LOCK -> PHASE_LOCK -> LOCKED` 分阶段判据首版；状态链由连续 accepted sample、offset lock threshold 和 `lock_sample_count` 驱动，后续还需接 reference loss、quality aging 和 reset policy。
- [ ] 实现 `LOCKED -> HOLDOVER` 判据。
- [ ] 实现 `HOLDOVER -> RELOCKING -> LOCKED/FAULT` 判据。
- [ ] 增加虚拟环路滤波器调试接口，但默认不要求上位机调节。
- [ ] 增加低频驯服任务：按秒级窗口统计 wander、temperature/aging compensation candidate，RUN 中不得直接写 flash。

## P4 - CAL / SYNC / MEAS / TRIG 边界

- [x] P4-PATHMATRIX-01 VDC 消费 Calibration active link delay，不直接执行校准测量；校准导入时通过 `vdc_domain_load_observation_path_matrix()` 装载完整 observation path matrix，运行态缺失矩阵项即 fail-closed。
- [ ] active calibration CRC 变化后，旧 `SYNC:CHECk` 结果失效，VDC 进入 `CHECKING` 或要求 RELOCK。
- [ ] `SYNC:*` 动作统一转为 VdcSyncAO event，不直接写 offset/rate。
- [ ] MEASure/T2 timestamp 进入 VDC quality 和 report evidence，但不直接修改业务序列。
- [ ] Trigger/Loop/Angle DPLL 只能读取 VDC snapshot，不得写 VDC owner 字段。
- [ ] 实现 T2 预约 clock binding：Trigger 在 arm guard 前调用 `local_from_vdc()`，绑定
  `map_generation`、quality、calibration/schedule CRC；generation 变化时返回明确 stale/reprepare，
  `LOCKED` 正常准入，`HOLDOVER` 按预约 error budget 准入，其余状态 fail closed。
- [ ] 实现 T2 completion 映射：接收 `T2_actual_local` 原始 latch，按绑定 generation 生成
  `T2_actual_vdc` 和 mapping quality；未知/淘汰 generation、软件时间戳或缺失 latch 必须拒绝，
  不得由 VDC 代替 Trigger 计算业务 retry/skip。
- [ ] VDC unlocked 或 quality 不达标时，RUN gate 禁止 `FIRE_LOAD`。
- [ ] 冻结 `PIO_SM0: SYNC_RX_CAPTURE` 输入事件格式、FIFO word、DMA ring 和 timestamp sample 映射。
- [ ] 冻结 `PIO_SM1: SYNC_TX_FIRE` 的 `FIRE_LOAD` 小载荷、target tick、polarity、width 和 late 拒绝规则。
- [ ] 冻结 core1_realtime 对 capture ring、TriggerSlot、VDC input ring 的写入边界。
- [ ] 冻结 `task_loop_engine -> trigger_command_queue -> core1_realtime -> PIO_SM1` 的 FIRE_LOAD 装载路径。
- [ ] 冻结 TDMA 同步窗口内的禁止项：普通数据帧、维护帧、report payload、SD/OTA payload 不得占用 VDC observation window。

## P5 - RefMem 映射

- [ ] 定义 VDC snapshot 到 RefMem `VdcSlot` 的字段映射。
- [ ] 定义 VDC quality 到 RefMem `StatisticsSlot` / quality slot 的字段映射。
- [ ] 定义 VDC fault/evidence 到 RefMem `FaultEvidenceSlot` 的索引规则。
- [ ] 定义 RefMem stale/node freshness 对 VDC lock gate 的输入规则。
- [ ] 定义 VDC `epoch_id/run_id` 和 RefMem `Version Bundle` 的一致性检查。

## P6 - 代码组件化

- [x] 增加 `components/vdc_domain/CMakeLists.txt`。
- [x] 新增 `components/vdc_domain/inc/vdc_domain.h` 和 `src/vdc_domain.c`。
- [ ] 新增 `vdc_clock_model.h/.c`。
- [ ] 新增 `vdc_dpll.h/.c`。
- [ ] 新增 `vdc_quality.h/.c`。
- [x] 新增 `vdc_timestamp.h/.c`。
- [x] 让旧 `components/vdc_dpll_manager/` 过渡为兼容 wrapper 或逐步拆空。
- [x] 修改 core1 realtime loop，让 VDC 执行入口使用 `vdc_sync_ao_service()` / `sync_dpll_fb_service()` / `tdma_component_core1_service()` HAOFV 语义命名；旧 `vdc_dpll_manager_*_service()` 仅保留为兼容 wrapper。
- [ ] 修改 SCPI VDC/SYNC 查询，保持读取 snapshot，不直接访问内部状态。

## P7 - SCPI / System Pack 接入

- [ ] 确认 `CONFigure:SYNC:VDC:DPLL` 写入 VDC staging profile。
- [ ] 确认 `SYNC:CHECk/STARt/STOP/RELock/HOLDover` 进入 VdcSyncAO command/event。
- [ ] 确认 `READ:SYNC:STATe?` / `READ:SYNC:QUALity?` 字段覆盖 VDC gate 和 quality；当前 `READ:SYNC:QUALity?` 已从 VDC snapshot 输出 health、offset、jitter、freshness、sample counter、timestamp resolution 和 reject code，字段语义仍需同步到 SCPI 指令表。
- [ ] 确认 `SYSTem:SYNC:VDC:*` 只作为维护调试接口；当前 `SYSTem:SYNC:VDC:TDMA:PLAN?` 仅返回 active schedule 窗口计划，不提交 intent、不写 RefMem、不写 DPLL；`SYSTem:SYNC:VDC:OBServer` 只配置 manager observer，不启动 capture、不伪造 DPLL lock。
- [ ] 定义 VDC profile、timestamp dictionary、quality limits 的 System Pack 存储格式。
- [ ] 定义 VDC 参数 save/load/activate/rollback 策略。

## P8 - 验证

- [x] 文档检查 `python tools/docs_check/docs_check.py`。
- [ ] 构建验证 build-validation。
- [ ] 构建验证 build-rtos-multicore-smoke。
- [ ] 板端记录 `SYSTem:SYNC:VDC:STATus?`。
- [ ] 板端记录 `SYSTem:SYNC:VDC:DPLL:STATus?`。
- [ ] 板端记录 `READ:SYNC:STATe?` 和 `READ:SYNC:QUALity?`。
- [ ] 故障注入 offset step、rate drift、jitter spike、holdover aging、relock fail。
- [ ] 故障注入 calibration CRC mismatch、timestamp dictionary mismatch、node stale。
- [ ] 验证 VDC unlocked 时禁止 RUN / `FIRE_LOAD`。
- [ ] 验证 LOCKED 后 T2/READY timestamp 映射到 VDC 时间。
- [ ] 验证 T2 预约正反向闭环：同一 map generation 下
  `T_fire_target_vdc -> local deadline -> hardware latch -> T2_actual_vdc`，记录量化误差、
  `T2_error_ns`、generation 和 quality；在 map 更新、HOLDOVER、relock、wrap 和 late 注入下 fail closed。
- [ ] 验证 TDMA observation window 门禁：窗口外、schedule CRC 不匹配、reference slot 不匹配的样本不得进入 DPLL。
- [ ] 验证两板 TDMA 硬件基础到 DPLL 输入链：COM5/COM6 在真实 PIO 25 MHz 环路中产出板端 timestamp evidence，报告 `timestamp_resolution_ns`、window late/jitter、sample CRC 和 phase error，不使用 host 侧耗时代替板端证据；窗口调度未落地前，HIL 只允许把 VDC gate 窗口拒绝记录为诊断状态，不得标记为 DPLL lock evidence。
- [x] 固化两板 RefMem TDMA -> VDC envelope 诊断查询脚本：`tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 在 `DELTA/ACK_NACK/FENCE/QUALITY` 交换后查询 `SYSTem:REFMEM:SYNC:TDMA:VDC?`，记录 bridge/gate/CRC/timestamp evidence。
- [ ] 验证 `INITIAL_SYNC -> FREQ_LOCK -> PHASE_LOCK -> LOCKED` 收敛状态链，并记录 lock_time、RMS/peak offset 和 outlier ratio。
- [x] 固化 VDC 自动长监控脚本：`tools/vdc_long_monitor/vdc_long_monitor.py` 支持 COM5/COM6 双向交替、自重装 self-test 窗口、CSV/JSONL/summary 落盘和 accepted/gate/DCO/offset/rate 统计。
- [ ] 纠偏 VDC 长监控暴露的 `WINDOW_BOUND` 稳定性问题：当前 180 s 双向监控有 4/11 个窗口最终 gate 为 `VDC_DOMAIN_GATE_WINDOW_BOUND`，后续需要让固件内部连续 observation window 或 idle beacon 持续运行，SCPI 只读镜像，不再由 host 周期释放/重装造成最后 evidence 停在窗口外。
- [ ] 纠偏 VDC 长监控暴露的 offset/rate 大跳变：当前 180 s 记录 `max_abs_offset_ns_max=963800`、`last_offset_ns_max=553600`、`freq_offset_ppb` 到达 `+/-50000` 限幅，需结合低频 log 和 DCO snapshot 分析 phase unwrap、path delay、window base 与 servo slew 策略。
- [ ] 验证 DCO slew/phase pull：core1 读取稳定 snapshot，late/半更新 snapshot 不得产生 FIRE_LOAD。
- [ ] 验证低频驯服环：wander、temperature/aging compensation candidate 和 HOLDOVER drift bound 只影响 error budget 或下一轮 profile。
- [ ] 验证 `PIO_SM0 -> DMA -> core1_realtime -> task_vdc_sync -> VdcSlot` 捕获链路时间戳连续性。
- [ ] 验证 `task_loop_engine -> FIRE_LOAD -> PIO_SM1` 输出链路 late 拒绝和准时输出。
- [ ] 增加 PTP/Chrony-style VDC 质量测试：offset step、rate drift、jitter spike、servo reset、holdover aging。
- [ ] 增加 EtherCAT DC-style 同步测试：reference node 选择、传播 delay 校准、周期漂移补偿、同步输出预测误差。
