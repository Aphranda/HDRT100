# Distributed RefMem 内部主域待办

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_TODO.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/arch/HAOFV_MAINTENANCE_TODO.md`
Last updated: 2026-08-13

本文档维护 Distributed Vector Blackboard / RefMem Sync Domain 的当前可执行待办。这里不记录普通开发流水账，只记录会影响分布式共同事实、RefMemAO、A0-A7 通用逻辑插槽、节点装载、SlotClaim 协调、表镜像、slot owner、命令 ACK/NACK、部署门禁、连接质量和 RefMem Sync 的架构与实现事项。

## 当前架构基线

以下内容已经作为后续实现的基础，不再作为待办反复展开：

- RefMem 是 HAOFV 内部基础主域，不是对外 SCPI 主域，也不执行业务动作或硬实时边沿。
- A0-A7 是全环唯一的通用逻辑插槽，不等同于固定产品角色；一块物理板可以承载多个实例，但 active assignment 最多 8 个。
- `DistributedGenericNodeTable` 描述通用逻辑插槽基座、硬件身份、能力、claim policy 和 fail policy。
- `DistributedNodeLoadTable` 描述实例装载关系，允许同一 A0-A7 插槽加载多个无冲突 AO/FB 实例。
- `DistributedFbInstanceTable`、`DistributedEventLinkTable`、`DistributedDataLinkTable`、`DistributedDeploymentGate`、`DistributedConnectionQualityTable` 共同描述静态 AO/FB 图、事件/事实连接、RUN 门禁和质量证据。
- `RefMemSlotContract` 是 `DistributedRefMemAO` 内部派生的字段级契约视图，不是新的业务配置入口；AO/FB 通过自己的 owner API 提交事实或读取 snapshot。
- RefMem load mode 是 `DistributedRefMemAO` 自己的表镜像状态机，不是产品 `TRIGger:MODE`，也不是 ResourceArbiter mode。
- SCPI/SD/类似 OTA 的配置加载只进入 staging image；active image 必须经过 CRC、lint、owner validation、activation 和回滚策略。

## 参考项目收敛原则

RefMem Domain 可以借鉴成熟开源/工业项目的机制，但不直接引入对应协议或完整运行时。借鉴关系必须落到固定表、owner、CRC、atomic、completion、fence、静态 AO/FB 图和验证项上。VDC/DPLL、offset/rate、HOLDOVER 和同步质量参考由 `docs/vdc/VDC_DOMAIN_TODO.md` 维护。

| 参考项目 | 可借鉴机制 | RefMem 落地边界 |
|---|---|---|
| NASA cFS Table Services | 表注册、active/inactive 表、CRC / data integrity、owner validation callback、load/dump 流程。 | 用于 `DistributedVectorTable`、静态模型表和 System Pack 的表镜像生命周期；不引入 cFS 总线或 flight app 运行时。 |
| OpenSHMEM / MPI RMA | RMA window、put/get/accumulate 思想、原子操作、origin/target completion、fence/order。 | 用于定义 RefMem slot mirror、delta 同步完成语义和 command slot atomic take/clear；不允许任意远程写裸变量。 |
| IEC 61499 | 静态 FB instance、事件连接、数据连接、部署一致性。 | 用于 `DistributedApplicationMap` / `DistributedFbInstanceTable` / EventLink / DataLink；不做运行时动态 FB 部署。 |

## P0 - 表镜像与加载闭环

目标：先把当前 `LOAD:SD` / `LOAD:NODE` staging 骨架升级为真正可验证、可回滚的表镜像机制。没有这层，后续动态节点加载、SD 加载和类似 OTA 的 RefMem package 都会缺少统一落点。

- [ ] 定义静态模型表的 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略，覆盖 ApplicationMap、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 QualityTable。
- [ ] 实现 `RefMemTableRegistry`，记录 table id、owner、offset/size、layout version、active CRC、staging CRC、validation state、validator id、last result 和 evidence。
- [ ] 增加 table image 双镜像生命周期：`EMPTY -> STAGED -> CRC_OK -> OWNER_OK -> ACTIVE -> ROLLBACKABLE/FAILED`。
- [ ] 实现 table dump/load 镜像规则：dump 只导出稳定 snapshot，load 只能进入 staging，不得直接覆盖 active。
- [ ] 实现 owner validation callback 调度；CRC 通过后仍必须由表 owner 检查字段范围、逻辑一致性、资源冲突和运行门禁。
- [ ] 将 `SYSTem:REFMEM:LOAD:SD` 从 manifest 占位升级为真实 TLV/System Pack parser。
- [ ] 将 `SYSTem:REFMEM:LOAD:NODE` 从单条候选 snapshot 升级为 staging NodeLoadTable image，支持多条候选、CRC、owner validation 和回滚。
- [ ] 增加类似 OTA 的 SCPI package 分块加载：`SYSTem:REFMEM:LOAD:BEGIN/DATA/END/ABORT`，用于传输完整 RefMem application/node package 到 staging。
- [ ] 增加 `SYSTem:REFMEM:TABLE:*` 维护查询草案，至少能观察 registry、active/staging CRC、validation state 和 evidence；保持在 `SYSTem:REFMEM:*` 命名空间内。

## P1 - SlotClaimMap 与自组网协调

目标：解决分布式环路中多个物理板误用同一 A0-A7 逻辑插槽、同一板卡加载多个实例、候选超过 active 容量和动态协调失败的可诊断问题。

- [x] 在 `GenericNodeTable` 中落 `claim_policy`、`claim_priority`、`node_uuid_crc32`、`hw_profile_crc32`、`online_required` 和 `fail_policy` 字段。
- [x] 在静态模型 linter 中加入 `claim_policy` 基础合法性检查。
- [ ] 定义 `SlotClaimProposal` 数据结构，区分 candidate instance 和 resolved active assignment；同一 physical board 最多上报 16 个 candidate。
- [ ] 实现 `SlotClaimMap` 聚合，记录 A0-A7 active assignment、claim epoch、physical board uuid、loaded instance mask、claim state、reason 和 CRC。
- [ ] 增加 `SlotClaimEvidence` 或等价诊断视图，记录第 9 到第 16 个未分配候选的 `OVERFLOW` evidence；超过 16 个 candidate 必须拒绝。
- [ ] 实现重复 slot claim 检测、uuid mismatch、hardware profile mismatch、stale claim、required hard binding mismatch 和 claim CRC 检查。
- [ ] 实现自组网协调消息：`CLAIM_HELLO`、`CLAIM_PROPOSE`、`CLAIM_CONFLICT`、`CLAIM_RELEASE`、`CLAIM_RESOLVE`、`CLAIM_COMMIT`。
- [ ] 将 `SlotClaimMap` 接入 `DistributedDeploymentGate.node_check`：required slot 冲突、错绑或 stale 时拒绝 RUN；spare dynamic slot 可按协调结果进入新 epoch。
- [ ] 增加单板 16 候选节点反向验证：一块板可上报 9 到 16 个候选用于溢出验证，但 active assignment 不得生成第 9 个隐式插槽。

## P2 - RefMemSlotContract 与 AO/FB owner API

目标：把字段读写规范收敛为 `DistributedRefMemAO` 内部可验证能力，而不是让业务 AO/FB 直接拼地址、写共享内存或绕过 owner。

- [ ] 定义 `RefMemSlotContract` 派生规则：从 DataLinkTable、Header/Directory、SlotGuard、DeploymentGate 和 QualityTable 生成字段级只读 contract。
- [ ] 新增 `refmem_slot_contract.h/.c`，只提供 `derive_contract`、`validate_write`、`validate_snapshot`、`validate_subscription` 等 RefMemAO 内部 helper。
- [ ] 明确 AO/FB 对外入口命名：业务 owner 调用 `DistributedRefMemAO` 的 publish/snapshot/subscription API；`SlotContract` 不作为业务 API 暴露。
- [ ] 在 linter 中检查 contract：地址不越界、字段宽度匹配、类型和值域一致、writer 唯一、version/timestamp/error 引用有效。
- [ ] 为全部字段增加统一 guard 或等价兼容结构。
- [ ] 实现 slot owner 写权限检查，禁止非 owner 写其他节点 slot 或 active fact。
- [ ] 实现 seqlock 或双缓冲，避免多字段快照半新半旧。
- [ ] 在代码中补齐 `epoch_id/run_id/epoch_seconds/dc_time64_ns` 等时间与运行上下文字段。

## P3 - Command / ACK / NACK

目标：把配置、启动、停止、激活、资源作业等跨 owner 意图统一到 command slot，而不是让 SCPI 或业务层直接改共同事实。

- [x] 文档定义命令槽原子 Take/Clear 语义，执行动作保持在临界区外。
- [x] 文档定义 `command_seq`、`source_node`、`target_mask`、`required_mask`、`command_type`、`payload_ref`、`payload_crc32`、`issue_epoch/run_id`、`timeout_us`。
- [x] 文档定义 ACK/NACK/busy/timeout 位图、last NACK reason、last NACK node、reason table CRC 和 evidence index。
- [ ] 新增 `refmem_command.h/.c`，实现 `try_post`、`try_take`、`ack`、`nack`、`clear`。
- [ ] 将现有 `system_manager` 配置 ACK 迁移或映射到 RefMem AckCommandSlot snapshot。
- [ ] 扩展 NACK reason 表，补齐 resource busy、RUN denied、payload CRC、epoch mismatch、dup seq、timeout、permission denied。
- [ ] 定义 completion 语义：`local_posted`、`target_taken`、`target_acked`、`all_required_acked`、`durable_committed`。
- [ ] 定义 memory order / fence 规则：payload 写入先于 command publish，ACK/NACK 写入先于 status publish。
- [ ] 评估是否新增 `SYSTem:COMMand:ACK? / NACK?`，并保持 `SYSTem:CONFigure:*` 为兼容配置视图。

## P4 - RefMem Sync Protocol 与 RMA Window

目标：把 RJ45_SYNC_RING 上的共同事实同步定义为受控 delta/epoch，而不是远端任意写内存。

- [ ] 定义 `REFMEM_DELTA(slot_id, slot_version, compact payload)` 帧格式。
- [ ] 定义 `REFMEM_EPOCH(epoch, run_id, table_seq)` 帧格式。
- [ ] 定义 slot delta CRC、seq、source_node、target_node、timestamp。
- [ ] 定义 RJ45_SYNC_RING 上的 delta 合并、重放、丢帧和 stale 策略。
- [ ] 定义 RefMem RMA Window 抽象：每个节点只暴露受控 slot mirror，不暴露任意地址。
- [ ] 定义 delta completion 语义：`origin_encoded`、`ring_sent`、`target_received`、`target_validated`、`target_committed`、`visible_in_snapshot`。
- [ ] 定义原子远端更新白名单：仅允许 command flag、seq/heartbeat、quality counter、dirty bitmap 等小字段使用 atomic update。
- [ ] 定义 RMA-style fence：一批 delta 在 `SYNC_EPOCH` 或 `RUN_GATE_CHECK` 前必须完成校验和可见性切换。
- [ ] 将节点新鲜度纳入 `SYNC:CHECk`、`READ:SYNC:*?` 和 TRIG RUN 门禁。

## P5 - 代码组件化

目标：将当前 `distributed_refmem.c` 的兼容壳逐步拆成可维护的 RefMem Domain 组件。

- [ ] 增加 `components/distributed_refmem/CMakeLists.txt`。
- [ ] 新增 `components/distributed_refmem/inc/refmem_domain.h` 和 `src/refmem_domain.c`。
- [x] 新增 `refmem_vector_table.h/.c`。
- [x] 新增 `refmem_application_model.h/.c`。
- [ ] 新增 `refmem_table_registry.h/.c`。
- [ ] 新增 `refmem_slot_claim.h/.c`。
- [ ] 新增 `refmem_slot_contract.h/.c`。
- [ ] 新增 `refmem_sync.h/.c`。
- [ ] 新增 `refmem_command.h/.c`。
- [ ] 新增 `refmem_quality.h/.c`。
- [ ] 让旧 `distributed_refmem.h/.c` 过渡为兼容 wrapper 或逐步拆空。
- [ ] 修改根 `CMakeLists.txt`，从直接列源文件过渡到组件化文件列表。

## P6 - 应用与 SCPI 接入

目标：保持外部接口清晰，SCPI/UI/System Pack 只发起意图和读取 snapshot，不直接拥有 RefMem 事实。

- [ ] 修改 `application/src/app_tasks.c`，把 `task_refmem_sync` 的职责描述收敛到 RefMem Domain owner。
- [ ] 修改 `application/src/app.c`，逐步去掉直接 `distributed_refmem_*` 调用，改成 RefMem Domain 初始化/service。
- [x] 修改 `middleware/scpi_port/src/scpi_system_snapshot_commands.c`，保持 `SYSTem:REFMEM:*` 读取 snapshot 或写 staging load 意图，不触发跨板查询或现场 IO，不直接覆盖 active。
- [ ] 保持 `SYSTem:REFMEM:*` 为系统维护入口，不新增裸顶级 `REFMEM` SCPI 域。
- [ ] 在 `SCPI_COMMAND_PLAN.md` 确认 `SYSTem:REFMEM:*` 暴露字段能够覆盖 table registry、slot freshness、ACK/NACK、CRC、stale、quality gate 和 evidence。

## P7 - 验证

每个实现闭环至少执行文档检查和相关代码构建；涉及板端行为时继续使用 COM6 或当前可用端口烧录/查询。

- [ ] 文档检查：`python tools/docs_check/docs_check.py`。
- [ ] 构建验证：`cmake --build build-rtos-multicore-smoke`。
- [ ] 板端记录 `SYSTem:REFMEM:STATus?`、`SYSTem:REFMEM:NODE?`、`SYSTem:REFMEM:LOAD:STATus?`。
- [ ] 板端验证 `SYSTem:REFMEM:LOAD:NODE` 合法候选 staged、非法 node/instance rejected。
- [ ] 板端验证 `SYSTem:REFMEM:LOAD:SD` 在无 SD、manifest 缺失、manifest OK 三种路径下返回固定 snapshot 且不改 active。
- [ ] 增加 table registry 验证：CRC 正确但 owner validation 失败时不得激活。
- [ ] 增加 SlotClaim 验证：重复 claim、错绑、stale、9-16 候选 overflow、超过 16 候选 rejected。
- [ ] 增加 SlotContract 验证：非法 writer、越界字段、stale snapshot、seqlock 重读。
- [ ] 增加 RMA-style atomic 验证：重复 post、并发 take、payload CRC mismatch、fence 前读取不可见。
