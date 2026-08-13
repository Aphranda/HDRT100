# Distributed RefMem 内部主域待办

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_TODO.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/arch/HAOFV_MAINTENANCE_TODO.md`
Last updated: 2026-08-13

本文档维护 Distributed Vector Blackboard / RefMem Sync Domain 的独立待办。这里不记录普通开发流水账，只记录会影响分布式共同事实、slot owner、命令 ACK/NACK、部署门禁、连接质量和 RefMem Sync 的架构与实现事项。

## P0 - 文档主域建立

- [x] 建立 `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`。
- [x] 建立 `docs/refmem/REFMEM_DOMAIN_TODO.md`。
- [x] 建立 `docs/refmem/REFMEM_TASK_PROGRESS.md`。
- [x] 更新 `docs/refmem/README.md`，加入三份标准文档入口。
- [x] 更新 `docs/arch/README.md`，在阅读顺序中加入 RefMem 主域文档。
- [x] 更新 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`，把 `refmem/` 从预留目录升级为内部主域目录。

## P1 - 架构文件同步

- [x] 修改 `docs/arch/HAOFV_ARCHITECTURE.md`，把 Distributed RefMem 从 layer 明确升格为 HAOFV 内部主域。
- [x] 修改 `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`，把 `task_refmem_sync` 改成 `DistributedRefMemAO / RefMemSyncFB` 的 RTOS owner 体现。
- [x] 修改 `docs/arch/RTOS_HAOFV_TODO.md`，将 P1 反射内存主数据面重构为 RefMem Domain 待办。
- [x] 修改 `docs/arch/HAOFV_MAINTENANCE_TODO.md`，增加“RefMem 内部主域建设”维护项。
- [x] 修改 `docs/interface/DTC100_SCPI_COMMAND_PLANNING.md`，说明 `SYSTem:REFMEM:*` 背后 owner 是 RefMem Domain。
- [x] 修改 `docs/interface/SCPI_COMMANDS.md`，同步 `SYSTem:REFMEM:*` 查询说明，避免上位机误认为有顶级 `REFMEM` SCPI 域。

## P2 - 静态分布式应用模型

- [x] 文档定义 `DistributedApplicationMap`，覆盖 A0-A7 八个通用节点，以及加载到节点上的 board/pulse_distributor/link_switcher/instrument_controller/gateway/model_vna/model_turntable/model_dut/test_agent 等 role/persona/instance；允许无冲突时同一通用节点同时载入多个实例。
- [x] 文档定义 `DistributedFbInstanceTable`，覆盖每节点 AO/FB instance、domain、版本、enable 条件、资源/IO claim、时间预算和健康状态。
- [x] 文档定义 `DistributedEventLinkTable`，覆盖 START/STOP/FIRE_LOAD/DONE/FAULT/ACK/NACK 的 source、destination、transport、timeout、ACK 策略和 evidence。
- [x] 文档定义 `DistributedDataLinkTable`，覆盖 slot 字段 writer/reader、类型、单位、值域、生命周期、snapshot 策略和 stale 窗口。
- [x] 文档定义 `DistributedDeploymentGate`，把 layout、node、instance、resource、IO、writer、event、data、config、cal/sync quality 纳入 RUN 门禁。
- [x] 文档定义 `DistributedConnectionQualityTable`，覆盖 seq、CRC、stale、late、drop、timeout、last_error、p99/p999 和 evidence index。
- [ ] 将上述六张静态模型表落到 `refmem_application_model.h/.c`。
- [ ] 定义静态模型表的 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略。
- [ ] 将 DeploymentGate 输出映射到 `SYSTem:REFMEM:STATus?` / 诊断 evidence / RUN gate。

## P3 - Vector Table 与 Slot 契约

- [ ] 冻结 `DistributedVectorTable` 64 KB layout、slot offset、slot size、layout version。
- [ ] 为 DistributedVectorTable 增加 directory CRC 和 slot directory 校验。
- [ ] 增加 epoch、run_id、config/calibration/loop/action/sync/sequence/permission/storage version。
- [ ] 实现 slot owner 写权限检查，禁止非 owner 写其他节点 slot。
- [ ] 实现 slot 级 snapshot API，查询只读快照，不临时触发现场 IO。
- [ ] 实现 seqlock 或双缓冲，避免字段半新半旧。
- [ ] 共享 slot 字段使用 `__atomic` 或 DMB 屏障；跨核快照必须带 sequence/version。
- [ ] 时间差一律使用回绕安全写法：`int32_t diff = (int32_t)(t1 - t0)`。
- [ ] 评估并定义 `epoch_seconds` / `time_epoch` 扩展字段，避免 49 天回绕破坏 VDC/DPLL/T2。

## P4 - Command / ACK / NACK

- [ ] 实现命令槽原子 Take/Clear，执行动作保持在临界区外。
- [ ] 定义 command_seq、source_node、target_node、command_type、payload_ref、timeout_ms。
- [ ] 定义 ACK/NACK/busy/timeout 位图和 per-node reason。
- [ ] 将 `SYSTem:CONFigure:ACK? / NACK?` 与 AckCommandSlot 对齐。
- [ ] 定义 command slot stale 和重复 command_seq 策略。

## P5 - RefMem Sync Protocol

- [ ] 定义 `REFMEM_DELTA(slot_id, slot_version, compact payload)` 帧格式。
- [ ] 定义 `REFMEM_EPOCH(epoch, run_id, table_seq)` 帧格式。
- [ ] 定义 slot delta CRC、seq、source_node、target_node、timestamp。
- [ ] 定义 RJ45_SYNC_RING 上的 delta 合并、重放、丢帧和 stale 策略。
- [ ] 将节点新鲜度纳入 `SYNC:CHECk`、`READ:SYNC:*?` 和 TRIG RUN 门禁。

## P6 - 代码组件化

- [ ] 增加 `components/distributed_refmem/CMakeLists.txt`。
- [ ] 新增 `components/distributed_refmem/inc/refmem_domain.h` 和 `src/refmem_domain.c`。
- [ ] 新增 `refmem_vector_table.h/.c`。
- [ ] 新增 `refmem_application_model.h/.c`。
- [ ] 新增 `refmem_sync.h/.c`。
- [ ] 新增 `refmem_command.h/.c`。
- [ ] 新增 `refmem_quality.h/.c`。
- [ ] 让旧 `distributed_refmem.h/.c` 过渡为兼容 wrapper 或逐步拆空。
- [ ] 修改根 `CMakeLists.txt`，从直接列源文件过渡到组件化文件列表。

## P7 - 应用与 SCPI 接入

- [ ] 修改 `application/src/app_tasks.c`，把 `task_refmem_sync` 的职责描述收敛到 RefMem Domain owner。
- [ ] 修改 `application/src/app.c`，逐步去掉直接 `distributed_refmem_*` 调用，改成 RefMem Domain 初始化/service。
- [ ] 修改 `middleware/scpi_port/src/scpi_system_snapshot_commands.c`，保持 SCPI 读取 snapshot，不触发现场动作。
- [ ] 保持 `SYSTem:REFMEM:*` 为系统维护入口，不新增裸顶级 `REFMEM` SCPI 域。

## P8 - 验证

- [ ] 文档检查 `python tools/docs_check/docs_check.py`。
- [ ] 构建验证 build-validation。
- [ ] 构建验证 build-rtos-multicore-smoke。
- [ ] 板端记录 `SYSTem:REFMEM:STATus?`。
- [ ] 板端记录 `SYSTem:REFMEM:NODE?`。
- [ ] 板端记录 `SYSTem:CORE?` core1 heartbeat。
- [ ] 板端记录 `SYSTem:PROTection:STATus?` runtime protection snapshot。
- [ ] 故障注入 stale、CRC error、timeout、NACK reason。
