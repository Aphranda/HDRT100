# Distributed RefMem 内部主域待办

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_TODO.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/arch/HAOFV_MAINTENANCE_TODO.md`
Last updated: 2026-08-13

本文档维护 Distributed Vector Blackboard / RefMem Sync Domain 的独立待办。这里不记录普通开发流水账，只记录会影响分布式共同事实、slot owner、命令 ACK/NACK、部署门禁、连接质量和 RefMem Sync 的架构与实现事项。

## 参考项目收敛原则

RefMem Domain 可以借鉴成熟开源/工业项目的机制，但不直接引入对应协议或完整运行时。借鉴关系必须落到固定表、owner、CRC、atomic、completion、fence、静态 AO/FB 图和验证项上。VDC/DPLL、offset/rate、HOLDOVER 和同步质量参考由 `docs/vdc/VDC_DOMAIN_TODO.md` 维护。

| 参考项目 | 可借鉴机制 | RefMem 落地边界 |
|---|---|---|
| NASA cFS Table Services | 表注册、active/inactive 表、CRC / data integrity、owner validation callback、load/dump 流程。 | 用于 `DistributedVectorTable`、静态模型表和 System Pack 的表镜像生命周期；不引入 cFS 总线或 flight app 运行时。 |
| OpenSHMEM / MPI RMA | RMA window、put/get/accumulate 思想、原子操作、origin/target completion、fence/order。 | 用于定义 RefMem slot mirror、delta 同步完成语义和 command slot atomic take/clear；不允许任意远程写裸变量。 |
| IEC 61499 | 静态 FB instance、事件连接、数据连接、部署一致性。 | 用于 `DistributedApplicationMap` / `DistributedFbInstanceTable` / EventLink / DataLink；不做运行时动态 FB 部署。 |

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
- [x] 修改 `docs/interface/SCPI_COMMAND_PLAN.md`，说明 `SYSTem:REFMEM:*` 背后 owner 是 RefMem Domain。
- [x] 修改 `docs/interface/SCPI_COMMANDS.md`，同步 `SYSTem:REFMEM:*` 查询说明，避免上位机误认为有顶级 `REFMEM` SCPI 域。

## P1.5 - 外部参考机制工程化收敛

- [x] 收敛 NASA cFS Table Services 借鉴项：表驱动、active/inactive、CRC、owner validation、load/dump、table registry。
- [x] 收敛 OpenSHMEM / MPI RMA 借鉴项：RMA window、origin/target completion、atomic、fence/order、禁止任意远程裸写。
- [x] 收敛 IEC 61499 借鉴项：静态 AO/FB instance、event/data link、deployment consistency。
- [x] 在 `REFMEM_DOMAIN_ARCHITECTURE.md` 增加虚拟反射内存参考机制矩阵，明确“借鉴机制 / 不采用内容 / 本项目落地点”。
- [x] 在 `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `RefMemTableRegistry`、staging/active 生命周期和 owner validation 框架。
- [x] 在 `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `RefMemRmaWindow`、completion、fence/quiet 和 atomic 白名单框架。
- [ ] 在 `SCPI_COMMAND_PLAN.md` 确认 `SYSTem:REFMEM:*` 暴露的字段能够覆盖 table registry、slot freshness、ACK/NACK、CRC、stale、quality gate 和 evidence。

## P2 - 静态分布式应用模型

- [x] 文档定义 `DistributedApplicationMap`，覆盖应用/profile 元数据、A0-A7 通用插槽集合，以及加载到插槽上的 board/pulse_distributor/link_switcher/instrument_controller/gateway/model_vna/model_turntable/model_dut/test_agent 等 role/persona/instance；允许无冲突时同一通用插槽同时载入多个实例。
- [x] 架构纠偏：将 A0-A7 通用插槽基座与应用实例装载拆开，禁止 `ApplicationMap.node[]` 使用 `instance_first/count` 把节点绑定到连续实例范围。
- [x] 增加 `DistributedNodeLoadTable`：由应用 profile 显式声明 `node_id -> instance_id` 加载关系，支持同一通用插槽同时加载模拟转台和模拟网分等多个实例。
- [x] 文档定义 `DistributedFbInstanceTable`，覆盖每节点 AO/FB instance、domain、版本、enable 条件、资源/IO claim、时间预算和健康状态。
- [x] 文档定义 `DistributedEventLinkTable`，覆盖 START/STOP/FIRE_LOAD/DONE/FAULT/ACK/NACK 的 source、destination、transport、timeout、ACK 策略和 evidence。
- [x] 文档定义 `DistributedDataLinkTable`，覆盖 slot 字段 writer/reader、类型、单位、值域、生命周期、snapshot 策略和 stale 窗口。
- [x] 文档定义 `DistributedDeploymentGate`，把 layout、node、instance、resource、IO、writer、event、data、config、cal/sync quality 纳入 RUN 门禁。
- [x] 文档定义 `DistributedConnectionQualityTable`，覆盖 seq、CRC、stale、late、drop、timeout、last_error、p99/p999 和 evidence index。
- [x] 将静态模型表落到 `refmem_application_model.h/.c`，首版包含 ApplicationMap、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality。
- [ ] 定义静态模型表的 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略。
- [ ] 将 DeploymentGate 输出映射到 `SYSTem:REFMEM:STATus?` / 诊断 evidence / RUN gate。
- [x] 增加静态模型 linter：检查 instance id、node id、role/persona、resource claim、IO claim、writer 唯一性和 event/data link 完整性。
- [x] 增加静态模型 package CRC：分别覆盖 ApplicationMap、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality。
- [ ] 增加 FB 图版本门禁：借鉴 IEC 61499 的部署一致性思想，RUN 前检查 AO/FB 类型、版本、enable 条件和连接表 CRC。

## P3 - Vector Table 与 Slot 契约

- [x] 文档冻结 `DistributedVectorTable` 64 KB layout、slot offset、slot size、layout version 规则。
- [x] 文档定义 Header/Directory 契约，包含 directory CRC、slot directory、layout 兼容和 slot map 校验。
- [x] 文档定义 Version Bundle，包含 epoch、run_id、config/calibration/loop/action/sync/sequence/permission/storage/build/hw profile 版本。
- [x] 文档定义 slot owner 写权限规则，禁止非 owner 直接写其他节点 slot 或 active fact。
- [x] 文档定义 slot 级 snapshot 契约，查询只读快照，不临时触发现场 IO。
- [x] 文档定义 `DIRECT_ATOMIC`、`SEQLOCK`、`DOUBLE_BUFFER`、`EVIDENCE_REF` 四类快照策略。
- [x] 文档定义共享字段必须使用 `__atomic`、DMB 屏障或等价机制；跨核快照必须带 sequence/version。
- [x] 文档定义时间差一律使用回绕安全写法：`int32_t diff = (int32_t)(t1 - t0)`。
- [x] 文档定义 `epoch_id + tick32` 和 `dc_time64_ns` 语义，并要求增加 `epoch_seconds` / `time_epoch` 等价字段。
- [x] 文档定义通用 RefMemAO 基础件模型：`DistributedRefMemAO` 由 Application/GenericNode/NodeLoad/FbInstance/EventLink/DataLink/Gate/Quality 加 Header/Directory/SlotGuard 组合生成 `RefMemSlotContract` 契约视图。
- [x] 文档定义 `DistributedRefMemAO` 内部字段级 `RefMemSlotContract` 能力：地址、类型、值域、唯一 writer、原子访问、版本、时间戳、生命周期、错误绑定、订阅、发布策略和权限。
- [x] 将 `distributed_refmem.h/.c` 拆出 `refmem_vector_table.h/.c`，并按文档冻结 offset/size/static assert。
- [x] 为 DistributedVectorTable 实现 directory CRC 和 slot directory 校验。
- [ ] 定义 `DistributedRefMemAO` 的 `RefMemSlotContract` 派生规则：从 DataLinkTable、Header/Directory、SlotGuard、DeploymentGate 和 QualityTable 生成字段级只读 contract，不作为新的业务配置入口。
- [ ] 新增 `refmem_slot_contract.h/.c`，只提供 `validate_write`、`validate_snapshot`、`validate_subscription`、`derive_contract` 等 `DistributedRefMemAO` 内部校验 helper，不提供绕过 AO/FB 或 RefMemAO 的业务读写 API。
- [ ] 在静态模型 linter 中检查 `RefMemSlotContract`：地址不越界、字段宽度匹配、类型和值域一致、writer 唯一、version/timestamp/error 引用有效。
- [ ] 为全部 slot 增加统一 guard 或等价兼容结构。
- [ ] 实现 slot owner 写权限检查，禁止非 owner 写其他节点 slot。
- [ ] 实现 seqlock 或双缓冲，避免字段半新半旧。
- [ ] 在代码中补齐 `epoch_id/run_id/epoch_seconds/dc_time64_ns` 等时间与运行上下文字段。
- [ ] 增加 RefMem Table Registry：记录 table id、owner、offset、size、version、active_crc、staging_crc、validation state 和 validator id。
- [ ] 增加 active/inactive table image 生命周期：`STAGED -> VALIDATED -> ACTIVE -> ROLLBACKABLE`。
- [ ] 增加 owner validation callback 契约：表 CRC 通过后仍必须由 owner 检查字段范围、逻辑一致性和资源冲突。
- [ ] 增加 table dump/load 镜像规则：dump 只导出稳定 snapshot，load 只能进入 staging，不得直接覆盖 active。
- [ ] 实现 `RefMemTableRegistry`，至少覆盖 table id、owner、version、active/staging CRC、validation state、validator id 和 evidence。
- [ ] 实现 staging/active/rollbackable 双镜像切换。
- [ ] 实现 owner validation callback 调度和 validation pending/result 查询。

## P4 - Command / ACK / NACK

- [x] 文档定义命令槽原子 Take/Clear 语义，执行动作保持在临界区外。
- [x] 文档定义 `command_seq`、`source_node`、`target_mask`、`required_mask`、`command_type`、`payload_ref`、`payload_crc32`、`issue_epoch/run_id`、`timeout_us`。
- [x] 文档定义 ACK/NACK/busy/timeout 位图、last NACK reason、last NACK node、reason table CRC 和 evidence index。
- [x] 文档定义 `SYSTem:CONFigure:ACK? / NACK?` 是 `AckCommandSlot` 的配置门禁视图；后续 `SYSTem:COMMand:*` 也必须读取同一底层事实。
- [x] 文档定义 command slot stale、重复 `command_seq`、CRC mismatch、epoch mismatch、timeout 和 clear_seq 策略。
- [ ] 将 AckCommandSlot 字段落到 `refmem_command.h/.c`。
- [ ] 将现有 `system_manager` 配置 ACK 迁移或映射到 RefMem AckCommandSlot snapshot。
- [ ] 扩展 NACK reason 表，补齐 resource busy、RUN denied、payload CRC、epoch mismatch、dup seq、timeout、permission denied。
- [ ] 评估是否新增 `SYSTem:COMMand:ACK? / NACK?`，并保持 `SYSTem:CONFigure:*` 为兼容配置视图。
- [ ] 定义 command slot atomic API：`try_post`、`try_take`、`ack`、`nack`、`clear` 必须具备 compare-exchange 或等价临界区语义。
- [ ] 定义 completion 语义：区分 `local_posted`、`target_taken`、`target_acked`、`all_required_acked`、`durable_committed`。
- [ ] 定义 memory order / fence 规则：payload 写入先于 command publish，ACK/NACK 写入先于 status publish。

## P5 - RefMem Sync Protocol

- [ ] 定义 `REFMEM_DELTA(slot_id, slot_version, compact payload)` 帧格式。
- [ ] 定义 `REFMEM_EPOCH(epoch, run_id, table_seq)` 帧格式。
- [ ] 定义 slot delta CRC、seq、source_node、target_node、timestamp。
- [ ] 定义 RJ45_SYNC_RING 上的 delta 合并、重放、丢帧和 stale 策略。
- [ ] 将节点新鲜度纳入 `SYNC:CHECk`、`READ:SYNC:*?` 和 TRIG RUN 门禁。
- [ ] 定义 RefMem RMA Window 抽象：每个节点只暴露受控 slot mirror，不暴露任意地址。
- [ ] 定义 delta completion 语义：`origin_encoded`、`ring_sent`、`target_received`、`target_validated`、`target_committed`。
- [ ] 定义原子远端更新白名单：仅允许 command flag、seq/heartbeat、quality counter、dirty bitmap 等小字段使用 atomic update。
- [ ] 定义 RMA-style fence：一批 delta 在 `SYNC_EPOCH` 或 `RUN_GATE_CHECK` 前必须完成校验和可见性切换。
- [ ] 定义 compact timestamp 与 delta frame 的关系：实时链路只传最小 timestamp，RefMem delta 传事实摘要和质量，不传大 payload。
- [ ] 实现 `RefMemRmaWindow` 子集，禁止裸地址远端写，只允许 slot delta 和白名单 atomic 字段。
- [ ] 实现 `target_received -> target_crc_ok -> target_owner_validated -> target_committed -> visible_in_snapshot` completion 状态。

## P6 - 代码组件化

- [ ] 增加 `components/distributed_refmem/CMakeLists.txt`。
- [ ] 新增 `components/distributed_refmem/inc/refmem_domain.h` 和 `src/refmem_domain.c`。
- [ ] 新增 `refmem_vector_table.h/.c`。
- [x] 新增 `refmem_application_model.h/.c`。
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
- [ ] 增加 cFS-style table 测试：CRC 正确但 owner validation 失败时不得激活。
- [ ] 增加 RMA-style atomic 测试：重复 post、并发 take、payload CRC mismatch、fence 前读取不可见。
