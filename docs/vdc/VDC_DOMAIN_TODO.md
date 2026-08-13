# VDC 内部主域待办

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_TODO.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`
Last updated: 2026-08-13

本文档维护 Virtual Distributed Clock / VDC Domain 的独立待办。这里不记录普通开发流水账，只记录会影响共同时间、timestamp、offset/rate、DPLL、HOLDOVER/RELOCK、VDC quality、RefMem 映射和 RUN gate 的架构与实现事项。

## 参考项目收敛原则

VDC Domain 可以借鉴成熟时间同步项目和工业 DC 思想，但不直接引入对应协议栈。借鉴关系必须落到 `offset/rate`、jitter、servo reset、HOLDOVER、reference clock、链路 delay、initial sync、drift compensation、timestamp 和验证项上。表驱动、RMA、ACK/NACK 和分布式共同事实参考由 `docs/refmem/REFMEM_DOMAIN_TODO.md` 维护。

| 参考项目 | 可借鉴机制 | VDC 落地边界 |
|---|---|---|
| LinuxPTP / Chrony | offset、rate/frequency、RMS offset、jitter、skew、slew、servo reset、HOLDOVER 质量模型。 | 用于 `VdcDpllState`、`VdcQualityTable`、lock gate、holdover age、rate limit、servo reset 和报告字段；不引入 NTP/PTP 协议栈，不调整系统 wall clock。 |
| SOEM / EtherCAT DC | reference clock、传播延时测量、initial sync、周期性 drift compensation、同步输出/输入 timestamp。 | 用于 DTC100 自定义 VDC/DC 思想：校准链路 delay，DPLL 形成 DC，预测分发使用共同时间，T2/READY 回读质量；不采用 EtherCAT 协议、ESC 寄存器或硬件 DC 单元依赖。 |
| IEC 61499 | 固定 AO/FB owner、event/data boundary、deployment consistency。 | 用于约束 `VdcSyncAO / SyncDpllFB / HoldoverFB / RelockFB` 的静态 owner 和事件边界；不做动态 FB 部署。 |

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
- [ ] 在 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 增加 EtherCAT DC-style 的 initial sync / drift compensation / holdover 检查链。
- [ ] 在 `SCPI_COMMAND_PLAN.md` 确认 `READ:SYNC:*?`、`SYSTem:SYNC:VDC:*` 暴露字段能够覆盖 offset/rate/quality/gate/evidence。

## P2 - VDC 数据契约冻结

- [ ] 冻结 `VdcClockModel` 字段、单位、writer、reader 和 snapshot 策略。
- [ ] 冻结 `VdcReferenceClockTable`，首版固定 A0，后续支持 priority / failover。
- [ ] 冻结 `VdcDpllState` 字段、单位、writer、reader 和 snapshot 策略。
- [ ] 冻结 `VdcServoProfile`，覆盖 servo_type、kp/ki、update period、step threshold、sanity limit 和 reset policy。
- [ ] 冻结 `VdcQualityTable` 字段、质量窗口、统计口径和 RUN gate 门限。
- [ ] 冻结 `VdcErrorBudget`，覆盖 offset RMS/max、frequency skew、path delay、dispersion 和 root distance 等价字段。
- [ ] 冻结 `VdcTimestampDictionary` 表格式、CRC、版本兼容和 System Pack 导入策略。
- [ ] 冻结 `VdcWrapTracker` 的 tick/seq 扩展规则和回绕安全差分。
- [ ] 冻结 `VdcCalibrationBinding`，定义 active cal CRC、link key、delay_ns 和失效策略。
- [ ] 冻结 `VdcDcSyncPipeline`，定义 reference select、initial sync、drift compensation、LOCKED publish 和 T2 validation 阶段。
- [ ] 冻结 `VdcGateResult`，定义 reject code、reject node、reject evidence 和 last pass tick。

## P3 - DPLL / Clock Model

- [ ] 实现 `local_tick -> vdc_time64_ns` 映射函数。
- [ ] 实现 SYNC DPLL offset/rate 更新。
- [ ] 实现 outlier gate、jitter window、phase error 和 frequency error 统计。
- [ ] 实现 servo reset 策略，区分 step reset、profile reset、calibration reset 和 fault reset。
- [ ] 实现 reference node 选择和 reference loss/failover 记录；首版可固定 A0。
- [ ] 实现 error budget 累积，HOLDOVER 中按 drift bound 增长 dispersion。
- [ ] 实现 `LOCKING -> LOCKED` 判据。
- [ ] 实现 `LOCKED -> HOLDOVER` 判据。
- [ ] 实现 `HOLDOVER -> RELOCKING -> LOCKED/FAULT` 判据。
- [ ] 增加虚拟环路滤波器调试接口，但默认不要求上位机调节。

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

## P5 - RefMem 映射

- [ ] 定义 VDC snapshot 到 RefMem `VdcSlot` 的字段映射。
- [ ] 定义 VDC quality 到 RefMem `StatisticsSlot` / quality slot 的字段映射。
- [ ] 定义 VDC fault/evidence 到 RefMem `FaultEvidenceSlot` 的索引规则。
- [ ] 定义 RefMem stale/node freshness 对 VDC lock gate 的输入规则。
- [ ] 定义 VDC `epoch_id/run_id` 和 RefMem `Version Bundle` 的一致性检查。

## P6 - 代码组件化

- [ ] 增加 `components/vdc_domain/CMakeLists.txt`。
- [ ] 新增 `components/vdc_domain/inc/vdc_domain.h` 和 `src/vdc_domain.c`。
- [ ] 新增 `vdc_clock_model.h/.c`。
- [ ] 新增 `vdc_dpll.h/.c`。
- [ ] 新增 `vdc_quality.h/.c`。
- [ ] 新增 `vdc_timestamp.h/.c`。
- [ ] 让旧 `components/vdc_dpll_manager/` 过渡为兼容 wrapper 或逐步拆空。
- [ ] 修改 `application/src/app_tasks.c`，让 `task_vdc_sync` 直接服务 VDC Domain owner。
- [ ] 修改 SCPI VDC/SYNC 查询，保持读取 snapshot，不直接访问内部状态。

## P7 - SCPI / System Pack 接入

- [ ] 确认 `CONFigure:SYNC:VDC:DPLL` 写入 VDC staging profile。
- [ ] 确认 `SYNC:CHECk/STARt/STOP/RELock/HOLDover` 进入 VdcSyncAO command/event。
- [ ] 确认 `READ:SYNC:STATe?` / `READ:SYNC:QUALity?` 字段覆盖 VDC gate 和 quality。
- [ ] 确认 `SYSTem:SYNC:VDC:*` 只作为维护调试接口。
- [ ] 定义 VDC profile、timestamp dictionary、quality limits 的 System Pack 存储格式。
- [ ] 定义 VDC 参数 save/load/activate/rollback 策略。

## P8 - 验证

- [ ] 文档检查 `python tools/docs_check/docs_check.py`。
- [ ] 构建验证 build-validation。
- [ ] 构建验证 build-rtos-multicore-smoke。
- [ ] 板端记录 `SYSTem:SYNC:VDC:STATus?`。
- [ ] 板端记录 `SYSTem:SYNC:VDC:DPLL:STATus?`。
- [ ] 板端记录 `READ:SYNC:STATe?` 和 `READ:SYNC:QUALity?`。
- [ ] 故障注入 offset step、rate drift、jitter spike、holdover aging、relock fail。
- [ ] 故障注入 calibration CRC mismatch、timestamp dictionary mismatch、node stale。
- [ ] 验证 VDC unlocked 时禁止 RUN / `FIRE_LOAD`。
- [ ] 验证 LOCKED 后 T2/READY timestamp 映射到 VDC 时间。
- [ ] 验证 `PIO_SM0 -> DMA -> core1_realtime -> task_vdc_sync -> VdcSlot` 捕获链路时间戳连续性。
- [ ] 验证 `task_loop_engine -> FIRE_LOAD -> PIO_SM1` 输出链路 late 拒绝和准时输出。
- [ ] 增加 PTP/Chrony-style VDC 质量测试：offset step、rate drift、jitter spike、servo reset、holdover aging。
- [ ] 增加 EtherCAT DC-style 同步测试：reference node 选择、传播 delay 校准、周期漂移补偿、同步输出预测误差。
