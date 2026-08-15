# VDC 内部主域待办

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_TODO.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`
Last updated: 2026-08-16

本文档维护 Virtual Distributed Clock / VDC Domain 的独立待办。这里不记录普通开发流水账，只记录会影响共同时间、timestamp、offset/rate、DPLL、HOLDOVER/RELOCK、VDC quality、RefMem 映射和 RUN gate 的架构与实现事项。

## 参考项目收敛原则

VDC Domain 可以借鉴成熟时间同步项目和工业 DC 思想，但不直接引入对应协议栈。借鉴关系必须落到 `offset/rate`、jitter、servo reset、HOLDOVER、reference clock、链路 delay、initial sync、drift compensation、timestamp 和验证项上。表驱动、RMA、ACK/NACK 和分布式共同事实参考由 `docs/refmem/REFMEM_DOMAIN_TODO.md` 维护。

| 参考项目 | 可借鉴机制 | VDC 落地边界 |
|---|---|---|
| LinuxPTP / Chrony | offset、rate/frequency、RMS offset、jitter、skew、slew、servo reset、HOLDOVER 质量模型。 | 用于 `VdcDpllState`、`VdcQualityTable`、lock gate、holdover age、rate limit、servo reset 和报告字段；不引入 NTP/PTP 协议栈，不调整系统 wall clock。 |
| SOEM / EtherCAT DC | reference clock、传播延时测量、initial sync、周期性 drift compensation、同步输出/输入 timestamp。 | 用于 DTC100 自定义 VDC/DC 思想：校准链路 delay，DPLL 形成 DC，预测分发使用共同时间，T2/READY 回读质量；不采用 EtherCAT 协议、ESC 寄存器或硬件 DC 单元依赖。 |
| IEC 61499 | 固定 AO/FB owner、event/data boundary、deployment consistency。 | 用于约束 `VdcSyncAO / SyncDpllFB / HoldoverFB / RelockFB` 的静态 owner 和事件边界；不做动态 FB 部署。 |

## 2026-08-16 HAOFV 对齐评审

总体结论：当前 VDC 主域方向正确，已经避免把 VDC 做成裸 SCPI 域或 RefMem 子函数；`VdcTdmaScheduleProfile`、window class、frame envelope、diagnostic timestamp gate 和 TDMA window planner 都符合“VDC 拥有共同时间，RefMem 只同步共同事实，core1/PIO 只执行硬实时动作”的 HAOFV 主线。但当前实现仍处于主域契约和诊断闭环阶段，尚未形成完整 `VdcSyncAO / SyncDpllFB / VdcVector / VdcQualityGateFB` 运行结构。以下风险必须作为后续 VDC 主线优先级，而不是继续扩大旁路脚本或只读查询。

### 符合 HAOFV 的部分

- [x] VDC 已被定义为 HAOFV 内部基础主域，和 Distributed RefMem 并列；没有新增裸 `VDC:*` / `DPLL:*` 顶级 SCPI 域。
- [x] RefMem -> VDC 的桥接保持单向证据语义：RefMem 只提供 frame/payload/CRC/timestamp evidence，不计算 offset/rate，不发布 VDC lock。
- [x] `time_us_64()*1000` 诊断时间戳已明确标记为 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY`，不能进入 100 ns DPLL lock gate。
- [x] TDMA window class 已区分 `VDC_OBSERVATION_WINDOW`、`REFMEM_DATA_WINDOW` 和 `IDLE_BEACON`，普通 RefMem 数据不能进入 observation window。
- [x] `vdc_domain_plan_tdma_window()` 已把窗口相位、guard、wait、late 和 missed 计算收敛到 VDC owner 侧，SCPI 只读查询不提交 intent。
- [x] `VdcDcoControl` 已有结构契约，后续 core1/PIO 可以只读稳定 snapshot，不需要直接访问 DPLL 内部状态。

### 必须纠偏的待办

| 优先级 | 风险项 | 当前状态 | 纠偏目标 |
|---:|---|---|---|
| P0 | `VdcSyncAO / SyncDpllFB / VdcVector` 尚未实体化。 | `vdc_domain.c` 仍是单体 domain core，`vdc_dpll_manager` 仍是兼容 wrapper，`task_vdc_sync` 只是 1 ms service 壳。 | 建立 VDC AO/FB 内部边界：SCPI/System 只能投递 command/event，`VdcSyncAO` 拥有 schedule/profile/cal binding，`SyncDpllFB` 唯一写 offset/rate/lock，`VdcVector` 发布稳定 snapshot。 |
| P0 | TDMA plan 未被 core1/PIO 消费。 | `refmem_realtime_tdma` intent 已增加 VDC data window plan 字段，NodeLoad AUTO TX/RX 会从 VDC manager 获取 `REFMEM_DATA_WINDOW` 计划；core1 service 已能在 guard 前保持 pending、窗口错过时拒绝、进入 payload window 后执行。 | 继续把该路径接到真实 PIO timestamp latch，并把 `VDC_OBSERVATION_WINDOW` 的 SYNC sample 变成正式 DPLL 输入。 |
| P0 | 缺少硬件 timestamp latch。 | 当前板端 evidence 仍来自 `time_us_64()*1000` 或 `board_uptime_ms()*1e6`，只能做诊断。 | 增加 PIO/DMA/IRQ/core1 capture latch，正式 DPLL sample 必须 `timestamp_source=HARDWARE_TICK` 且 `timestamp_resolution_ns <= 100`。 |
| P0 | DPLL 算法未真正更新 clock model。 | `vdc_domain_submit_tdma_evidence()` 只做门禁、计数和简化状态推进，未根据 phase/frequency error 更新 `period_adjust_ppb` / `phase_offset_ns`。 | 将 PI/linreg 或等价 servo 收敛到 `SyncDpllFB`，实现 offset/rate、outlier、step/slew、sanity limit、reset 和 staged profile。 |
| P1 | VDC snapshot 尚未发布到 RefMem `VdcSlot`。 | RefMem application model 有 VDC/DPLL 区域字段，但 VDC 主域未按唯一 writer、guard、sequence/CRC 发布。 | 定义 `VdcSlot` 字段级 contract，VDC owner 写 snapshot/quality/fault/evidence，RefMem 只镜像和同步。 |
| P1 | RUN gate 未消费 VDC quality/error budget。 | RUN gate 已有 RefMem quality 约束，VDC lock/holdover/error budget 尚未进入统一门禁。 | `VdcQualityTable`、`VdcErrorBudget`、`VdcGateResult` 接入 SystemManager RUN gate 和 report evidence。 |
| P1 | `CONFigure:SYNC:VDC:DPLL`、`SYNC:*` 仍是 accepted stub 或固定回复。 | SCPI 入口未进入 VdcSyncAO command/event，也未写 staging profile。 | `CONFigure:SYNC:*` 写 VDC/System Pack staging，`SYNC:CHECk/STARt/STOP/RELock/HOLDover` 进入 VdcSyncAO event，查询只读 snapshot。 |
| P1 | `READ:SYNC:*?` 仍大量固定返回值。 | 产品读接口没有完全来自 VDC snapshot、quality、gate 和 RefMem freshness。 | 产品测试上位机读取路径必须覆盖 lock state、quality、active profile CRC、gate reject、holdover age、reference slot 和 evidence index。 |
| P2 | `VdcReferenceClockTable` / failover 尚未冻结。 | 默认 profile 固定 reference slot，板端 `PLAN?` 当前显示 local/reference 均为默认 0。 | 首版可固定 A0，但必须形成表结构、priority、candidate、current source、loss/failover reason 和 SlotClaim/NodeLoad 绑定。 |
| P2 | `VdcCalibrationBinding` 尚未接入。 | active link delay、cal CRC 变化不会驱动 VDC relock 或 invalidation。 | VDC 只消费 Calibration active result；cal CRC/delay 变化后旧 lock/check 失效，进入 CHECKING/RELOCKING 并记录 gate evidence。 |
| P2 | DCO snapshot 未形成跨核稳定消费路径。 | C 结构已存在，但 core1 还没有 seqlock/double-buffer 读取、半更新拒绝和 stale 策略。 | 增加 DCO shared snapshot guard，core1 只读稳定 DCO，late/半更新/stale 时拒绝 FIRE_LOAD。 |
| P2 | `IDLE_BEACON` 未落地。 | 无业务数据时没有持续 observation/freshness 样本。 | 在 TDMA cycle 中加入 idle beacon 或等价 sync frame，维持 DPLL sample freshness 和 holdover age。 |
| P3 | VDC 代码组件化不足。 | `components/vdc_domain` 只有 `vdc_domain.h/.c`。 | 拆分 `vdc_clock_model`、`vdc_dpll`、`vdc_quality`、`vdc_timestamp`，并保留 `vdc_domain` 作为聚合/owner API。 |

### 后续执行顺序建议

1. P0 先做 `TDMA plan -> refmem_realtime_tdma intent -> core1 wait window`，让当前 25 MHz 两板链路真正受 VDC schedule 约束。
2. P0 紧接硬件 timestamp latch，把 observation window 样本从诊断时间戳升级为 DPLL eligible evidence。
3. P0/P1 实现 `SyncDpllFB` 的真实 offset/rate servo，并发布 `VdcClockModel` / `VdcDcoControl` 稳定 snapshot。
4. P1 接 `VdcSlot`、`VdcQualityTable`、`VdcErrorBudget` 到 RefMem 和 RUN gate。
5. P1/P2 再完善 SCPI staging、ReferenceClockTable、CalibrationBinding、Holdover/Relock 和 System Pack 持久化。

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
- [x] 冻结 `VdcTdmaScheduleProfile`，覆盖 TDMA 周期、同步窗口、guard、reference slot、schedule version 和 CRC。
- [x] 冻结 TDMA frame envelope：每一帧都必须先表达 schedule_epoch、slot_index、frame_seq、source/reference slot、timestamp evidence、schedule CRC、frame CRC 和 payload class；RefMem 数据只能作为 payload class 搭载。
- [x] 冻结 TDMA window class：`VDC_OBSERVATION_WINDOW` 是高优先级 DPLL 样本窗口，`REFMEM_DATA_WINDOW` 在同一 VDC/TDMA 骨架上搭载共同事实同步；两者必须分别有 slot/action、guard、CRC、quality 和禁止项。
- [x] 冻结 `VdcTDMATimestampEvidence`，字段单位统一为 ns，至少覆盖 expected_window_start、arm/start/done/apply timestamp、late_ns、jitter_ns、schedule_crc32、frame/sample CRC 和 `timestamp_resolution_ns`。
- [ ] 冻结 `VdcReferenceClockTable`，首版固定 A0，后续支持 priority / failover。
- [x] 冻结 `VdcDpllState` 字段、单位、writer、reader 和 snapshot 策略。
- [x] 冻结 `VdcDcoControl`，覆盖 DPLL 输出到 core1/PIO 的 `base_local_tick64`、`base_vdc_time64_ns`、`period_adjust_ppb/rate_q32`、`phase_offset_ns`、`slew_limit_ppb` 和 `dco_update_seq`。
- [x] 冻结 `VdcServoProfile`，覆盖 servo_type、kp/ki、update period、step threshold、sanity limit 和 reset policy。
- [x] 冻结 `VdcQualityTable` 字段、质量窗口、统计口径和 RUN gate 门限。
- [x] 冻结 `VdcErrorBudget`，覆盖 offset RMS/max、frequency skew、path delay、dispersion 和 root distance 等价字段。
- [ ] 冻结 `VdcTimestampDictionary` 表格式、CRC、版本兼容和 System Pack 导入策略。
- [ ] 冻结 `VdcWrapTracker` 的 tick/seq 扩展规则和回绕安全差分。
- [ ] 冻结 `VdcCalibrationBinding`，定义 active cal CRC、link key、delay_ns 和失效策略。
- [ ] 冻结 `VdcDcSyncPipeline`，定义 reference select、initial sync、drift compensation、LOCKED publish 和 T2 validation 阶段。
- [ ] 冻结 `VdcDisciplineModel`，定义 aging compensation、temperature compensation、wander、holdover drift bound、discipline window 和持久化 profile seq。
- [x] 冻结 `VdcGateResult`，定义 reject code、reject node、reject evidence 和 last pass tick。
- [x] 在 `VDC_DOMAIN_ARCHITECTURE.md` 融合 TDMA + DPLL 三环模型，明确 TDMA 只提供确定性观测窗口，DPLL 是 offset/rate 唯一 writer，低频驯服环只更新长期漂移和 HOLDOVER 误差预算。

## P3 - DPLL / Clock Model

- [x] 实现 `local_tick -> vdc_time64_ns` 映射函数。
- [ ] 实现 SYNC DPLL offset/rate 更新；首版已由 `vdc_domain_submit_tdma_evidence()` 在 accepted hardware sample 后更新 `VdcClockModel` / `VdcDcoControl` 的 phase offset 和 period adjust，并记录 frequency error / skew，后续仍需接 PI/linreg servo、outlier gate、reset policy 和真实硬件 timestamp latch。
- [x] 实现 TDMA observation window 输入门禁：只有来自 active schedule、正确 reference slot、正确 schedule CRC 和同步窗口内的 timestamp sample 才能进入 DPLL。
- [ ] 在 COM5/COM6 已验证 RefMem data TDMA 环路基础上，增加两板帧级 timestamp bring-up：每个 `REFMEM_DELTA/ACK/FENCE/QUALITY/IDLE_BEACON` 帧都产生 TDMA/VDC timestamp evidence；高优先级 `VDC_OBSERVATION_WINDOW` 形成正式 `VdcDpllSample`，再反向或双向测量 delay/evidence。
- [x] 增加 RefMem data frame 到 VDC TDMA frame envelope 的桥接基础件：`DELTA/ACK_NACK/FENCE/QUALITY` 可以映射到 `REFMEM_DATA_WINDOW` 并带诊断 timestamp evidence；该桥接不得计算 offset/rate，也不得把诊断 timestamp 标成 DPLL eligible。
- [x] 将当前 TDMA snapshot 的软件时间戳明确限定为诊断 evidence：若来源为 `time_us_64()*1000`，必须暴露 `timestamp_resolution_ns=1000`，不得进入 100 ns DPLL lock gate。
- [x] 将 RefMem TDMA TX/RX 从“收到 intent 后立即执行”升级为按 `VdcTdmaScheduleProfile` 的 `REFMEM_DATA_WINDOW` 执行：core1/PIO 必须等待窗口、记录 late/jitter，并在窗口外明确返回 gate evidence；首版已落地 `vdc_domain_plan_tdma_window()` 和 `SYSTem:SYNC:VDC:TDMA:PLAN?` 只读计划查询，`refmem_realtime_tdma` intent 已携带 VDC data window plan，core1 service 会在 guard 前等待、错过 payload window 时返回 `WINDOW_MISSED`。后续 HIL 需要确认 COM5/COM6 真实 25 MHz PIO 环路中的窗口 wait/late evidence。
- [ ] 增加硬实时 timestamp latch 路径：PIO/DMA/IRQ/core1 采集本地 tick，转换或映射为 ns 字段，并声明实际分辨率；DPLL 正式样本要求 `timestamp_resolution_ns <= 100`。
- [ ] 实现 DCO snapshot 生成：DPLL 输出通过 `VdcDcoControl` 提交给 core1，core1 只读稳定 snapshot 并执行 slew/phase pull；当前 VDC owner 已在 accepted sample 后派生 DCO snapshot，core1 稳定读取和 slew/phase pull 尚未接入。
- [ ] 实现 outlier gate、jitter window、phase error 和 frequency error 统计；首版已落地 VDC-owned `VdcQualityTable` / `VdcErrorBudget` 快照，能记录 accepted/rejected sample、offset RMS/max、jitter、timestamp freshness、resolution 和 gate reject，frequency error / outlier policy 后续接真实 servo。
- [ ] 实现 servo reset 策略，区分 step reset、profile reset、calibration reset 和 fault reset。
- [ ] 实现 reference node 选择和 reference loss/failover 记录；首版可固定 A0。
- [ ] 实现 error budget 累积，HOLDOVER 中按 drift bound 增长 dispersion。
- [ ] 实现 `INITIAL_SYNC -> FREQ_LOCK -> PHASE_LOCK -> LOCKED` 分阶段判据。
- [ ] 实现 `LOCKED -> HOLDOVER` 判据。
- [ ] 实现 `HOLDOVER -> RELOCKING -> LOCKED/FAULT` 判据。
- [ ] 增加虚拟环路滤波器调试接口，但默认不要求上位机调节。
- [ ] 增加低频驯服任务：按秒级窗口统计 wander、temperature/aging compensation candidate，RUN 中不得直接写 flash。

## P4 - CAL / SYNC / MEAS / TRIG 边界

- [ ] VDC 消费 Calibration active link delay，不直接执行校准测量。
- [ ] active calibration CRC 变化后，旧 `SYNC:CHECk` 结果失效，VDC 进入 `CHECKING` 或要求 RELOCK。
- [ ] `SYNC:*` 动作统一转为 VdcSyncAO event，不直接写 offset/rate。
- [ ] MEASure/T2 timestamp 进入 VDC quality 和 report evidence，但不直接修改业务序列。
- [ ] Trigger/Loop/Angle DPLL 只能读取 VDC snapshot，不得写 VDC owner 字段。
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
- [ ] 新增 `vdc_timestamp.h/.c`。
- [x] 让旧 `components/vdc_dpll_manager/` 过渡为兼容 wrapper 或逐步拆空。
- [ ] 修改 `application/src/app_tasks.c`，让 `task_vdc_sync` 直接服务 VDC Domain owner。
- [ ] 修改 SCPI VDC/SYNC 查询，保持读取 snapshot，不直接访问内部状态。

## P7 - SCPI / System Pack 接入

- [ ] 确认 `CONFigure:SYNC:VDC:DPLL` 写入 VDC staging profile。
- [ ] 确认 `SYNC:CHECk/STARt/STOP/RELock/HOLDover` 进入 VdcSyncAO command/event。
- [ ] 确认 `READ:SYNC:STATe?` / `READ:SYNC:QUALity?` 字段覆盖 VDC gate 和 quality；当前 `READ:SYNC:QUALity?` 已从 VDC snapshot 输出 health、offset、jitter、freshness、sample counter、timestamp resolution 和 reject code，字段语义仍需同步到 SCPI 指令表。
- [ ] 确认 `SYSTem:SYNC:VDC:*` 只作为维护调试接口；当前新增 `SYSTem:SYNC:VDC:TDMA:PLAN?` 仅返回 active schedule 窗口计划，不提交 intent、不写 RefMem、不写 DPLL。
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
- [ ] 验证 TDMA observation window 门禁：窗口外、schedule CRC 不匹配、reference slot 不匹配的样本不得进入 DPLL。
- [ ] 验证两板 TDMA 硬件基础到 DPLL 输入链：COM5/COM6 在真实 PIO 25 MHz 环路中产出板端 timestamp evidence，报告 `timestamp_resolution_ns`、window late/jitter、sample CRC 和 phase error，不使用 host 侧耗时代替板端证据；窗口调度未落地前，HIL 只允许把 VDC gate 窗口拒绝记录为诊断状态，不得标记为 DPLL lock evidence。
- [x] 固化两板 RefMem TDMA -> VDC envelope 诊断查询脚本：`tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 在 `DELTA/ACK_NACK/FENCE/QUALITY` 交换后查询 `SYSTem:REFMEM:SYNC:TDMA:VDC?`，记录 bridge/gate/CRC/timestamp evidence。
- [ ] 验证 `INITIAL_SYNC -> FREQ_LOCK -> PHASE_LOCK -> LOCKED` 收敛状态链，并记录 lock_time、RMS/peak offset 和 outlier ratio。
- [ ] 验证 DCO slew/phase pull：core1 读取稳定 snapshot，late/半更新 snapshot 不得产生 FIRE_LOAD。
- [ ] 验证低频驯服环：wander、temperature/aging compensation candidate 和 HOLDOVER drift bound 只影响 error budget 或下一轮 profile。
- [ ] 验证 `PIO_SM0 -> DMA -> core1_realtime -> task_vdc_sync -> VdcSlot` 捕获链路时间戳连续性。
- [ ] 验证 `task_loop_engine -> FIRE_LOAD -> PIO_SM1` 输出链路 late 拒绝和准时输出。
- [ ] 增加 PTP/Chrony-style VDC 质量测试：offset step、rate drift、jitter spike、servo reset、holdover aging。
- [ ] 增加 EtherCAT DC-style 同步测试：reference node 选择、传播 delay 校准、周期漂移补偿、同步输出预测误差。
