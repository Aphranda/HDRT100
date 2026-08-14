# Distributed RefMem 内部主域待办

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_TODO.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/arch/HAOFV_MAINTENANCE_TODO.md`
Last updated: 2026-08-14

本文档维护 Distributed Vector Blackboard / RefMem Sync Domain 的当前可执行待办。这里不记录普通开发流水账，只记录会影响分布式共同事实、RefMemAO、A0-A7 通用逻辑插槽、节点装载、SlotClaim 协调、表镜像、slot owner、命令 ACK/NACK、部署门禁、连接质量和 RefMem Sync 的架构与实现事项。

## 当前架构基线

以下内容已经作为后续实现的基础，不再作为待办反复展开：

- RefMem 是 HAOFV 内部基础主域，不是对外 SCPI 主域，也不执行业务动作或硬实时边沿。
- A0-A7 是全环唯一的通用逻辑插槽，不等同于固定产品角色；一块物理板可以承载多个实例，但 active assignment 最多 8 个。
- B0-B4 是当前项目或默认 profile 的物理/实例标签，不是 RefMem slot；B 实例加载到哪个 A0-A7 slot 由 `DistributedNodeLoadTable`、`SlotClaimMap` 和 DeploymentGate 共同决定。
- 每个可参与系统的物理节点都必须具备 `REFMEM + VDC` 基础能力；`VDC` 表示参与虚拟 DC 时间语义，`VDC_DPLL` 才表示运行 DPLL owner。
- `DistributedGenericNodeTable` 描述通用逻辑插槽基座、硬件身份、能力、claim policy 和 fail policy。
- `DistributedNodeLoadTable` 描述实例装载关系，允许同一 A0-A7 插槽加载多个无冲突 AO/FB 实例。
- `DistributedFbInstanceTable`、`DistributedEventLinkTable`、`DistributedDataLinkTable`、`DistributedDeploymentGate`、`DistributedConnectionQualityTable` 共同描述静态 AO/FB 图、事件/事实连接、RUN 门禁和质量证据；其中 `resource_claim`、`io_claim` 和 `ip_core_claim` 共同表达实时能力契约。
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

## 近期执行主线

当前阶段先完善 RefMem 基础件，不继续堆业务节点。业务节点只作为验证载体，不能绕过 RefMem 的 staging、owner、slot contract、command 和 quality 闭环。

| 顺序 | 主线 | 完成判据 |
|---:|---|---|
| 1 | NodeLoad staging/activation | `CONFigure:MODEl:TURNtable:LOAD` 不再只改本地变量，而是形成 NodeLoad staging snapshot；可查询、可验证、可拒绝、可激活、可回滚。 |
| 2 | Command / ACK / NACK 基础件 | SCPI 只 post command，owner take 后 ACK/NACK；启动、停止、配置激活和模型加载都能形成闭环状态。 |
| 3 | RefMemSlotContract | 每个 slot/字段有唯一 writer、权限、guard、snapshot 策略和 stale 规则；业务 AO 不能直接裸写共享内存。 |
| 4 | RefMem Sync 最小闭环 | 先用 PIO SPI adapter 完成 `HELLO -> EPOCH -> DELTA -> ACK_NACK -> FENCE -> QUALITY`，协议不绑定 BISS-C。 |
| 5 | Quality / Evidence | CRC/drop/late/timeout/stale/claim conflict 等进入质量表和 evidence，可由维护接口和报告读取。 |
| 6 | 业务模型闭环 | 在以上基础上逐个接入 `ModelTurntableAO`、`ModelVnaAO`、`LinkSwitcherAO`、`PulseDistributorAO`、`VnaGatewayAO`。 |

近期不做：

- 不把 `TriggerMasterAO`、`TriggerAO`、`LinkSwitcherAO`、`ModelTurntableAO` 等功能实例重新写死到固定 A slot。
- 不让 SCPI 直接操作硬件动作；SCPI 只表达配置、动作意图和读取 snapshot。
- 不把 PIO SPI adapter 当成最终通讯绑定；它只是最小两板验证载体。
- 不在 RUN 中热改 active profile；所有表修改先进入 staging。

## P0 - 表镜像与加载闭环

目标：先把当前 `LOAD:SD` / `LOAD:NODE` staging 骨架升级为真正可验证、可回滚的表镜像机制。没有这层，后续动态节点加载、SD 加载和类似 OTA 的 RefMem package 都会缺少统一落点。

- [x] 定义静态模型表的 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略，覆盖 ApplicationMap、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 QualityTable。
- [x] 实现 `RefMemTableRegistry` 首版，记录 table id、owner、layout version、active CRC、staging CRC、validation state、validator id、last result 和 evidence；首版反映已编译 active 表和当前 staging snapshot。
- [x] 增加 TableRegistry 可观测生命周期字段：`EMPTY/STAGED/CRC_OK/OWNER_OK/ACTIVE/ROLLBACKABLE/FAILED`。
- [ ] 实现真实 active/staging/rollbackable table image 切换：staging 通过验证后可进入 activation，旧 active 进入 rollbackable。
- [x] 定义并落地 table image descriptor：active/staging/rollbackable 只由 descriptor、seq、CRC bundle、state 和 evidence 对外可见，完整表数据不得写入 RefMem 向量表。
- [ ] 增加 activation gate：RefMem load mode、产品实时 idle/park、flash lockout/RAM-resident 入口、CRC bundle、owner validation、SlotClaimMap、DeploymentGate 和 command ACK 必须全部通过后才能切 active。
- [ ] 实现 table dump/load 镜像规则：dump 只导出稳定 snapshot，load 只能进入 staging，不得直接覆盖 active。
- [x] 增加 owner validation contract 首版入口：`refmem_table_registry_validate_staging()` 只校验当前 staging snapshot 的 CRC/lint/error 结果，不执行 active 替换。
- [ ] 实现真实 owner validation callback 调度；CRC 通过后仍必须由表 owner 检查字段范围、逻辑一致性、资源冲突和运行门禁。
- [ ] owner validation callback 结果必须写入 TableRegistry：table id、owner id、validator id、result、reason、evidence index 和失败阶段。
- [x] 将 `SYSTem:REFMEM:LOAD:SD` 从 manifest 占位升级为 `.rmtp` table image parser 首版，校验 header、table directory、payload CRC、package CRC 和每表 CRC；当前仍只写 staging snapshot，不替换 active。
- [x] 将 `sd_fs_build.py` 集成 RefMem table image 生成，默认输出 `/refmem/app_model.rmtp`、`/refmem/app_model.idx`、`/refmem/app_model.json`，并在根 `/manifest.idx` 中作为 `required=...,type=refmem_table_image` 引用。
- [ ] 将 `SYSTem:REFMEM:LOAD:NODE` 从单条候选 snapshot 升级为 staging NodeLoadTable image，支持多条候选、CRC、owner validation 和回滚。
- [ ] 将 `CONFigure:MODEl:TURNtable:LOAD <slot_id>,<output_index>` 映射为 NodeLoad staging 意图：生成或更新 `Template.ModelTurntableAO` 的候选装载记录，并记录 staging seq、slot、resource/io/ip claim 和拒绝原因。
- [ ] 增加 NodeLoad staging activation 命令或复用现有 config activation：activation 前必须完成 table CRC、instance range、SlotClaimMap、RealtimeCapabilityContract、DeploymentGate 和 command ACK 检查。
- [ ] 增加 NodeLoad rollback/abort 语义：未激活 staging 可 abort；激活失败必须保留旧 active profile，并在 TableRegistry 中记录失败 evidence。
- [x] 增加类似 OTA 的通用 StorageSCPI 文件分块加载：`SYSTem:STORage:FILE:WRITe:BEGIN/DATA/END/ABORt/STATus?`，可写入 `/refmem/app_model.rmtp`；写入后仍需 `SYSTem:REFMEM:LOAD:SD` 进入 RefMem staging。
- [x] 为 StorageAO 文件/目录补齐 CRUD 维护入口：`FILE:INFO?/READ?/DELete/REName`、`DIRectory:CREate/DELete/REName/CATalog?`；SCPI 不直接调用 FatFs，RefMem 向量表不承载文件数据。
- [x] 增加 `SYSTem:REFMEM:TABle? [table_id]` 维护查询，观察 registry、active/staging CRC、validation state 和 evidence；保持在 `SYSTem:REFMEM:*` 命名空间内。

## P1 - SlotClaimMap 与自组网协调

目标：解决分布式环路中多个物理板误用同一 A0-A7 逻辑插槽、同一板卡加载多个实例、候选超过 active 容量和动态协调失败的可诊断问题。

- [x] 在 `GenericNodeTable` 中落 `claim_policy`、`claim_priority`、`node_uuid_crc32`、`hw_profile_crc32`、`online_required` 和 `fail_policy` 字段。
- [x] 在静态模型 linter 中加入 `claim_policy` 基础合法性检查。
- [x] 定义 `SlotClaimProposal` / `SlotClaimMap` 首版数据结构，区分 candidate instance 和 resolved active assignment；同一 physical board 最多上报 16 个 candidate。
- [x] 在 `SlotClaimProposal` 中显式加入 board/profile label，例如 B0-B4、physical uuid、hardware profile CRC、capability mask、IO constraint 和类 IP 核能力摘要，避免把 A0-A7 slot 误当成固定物理板。
- [x] 定义 `BoardCapabilityTable` 或等价 profile 表，把物理板能力、IO 约束和类 IP 核能力从 `GenericNodeTable` 中逐步拆出；所有板卡条目必须包含 `REFMEM + VDC` baseline，`GenericNodeTable` 只保留 A0-A7 slot 基座和 claim policy。
- [ ] 将 `BoardCapabilityTable` 纳入 `.rmtp` / SD System Pack 的真实表镜像格式，支持 load/dump、CRC、schema version 和 rollbackable active image。
- [x] 增加受控 SCPI staging 入口 `SYSTem:REFMEM:LOAD:BOARD` / `SYSTem:REFMEM:LOAD:BOARD:STATus?`，用于加载或修改单条 board capability 候选；SCPI 不得直接修改 active BoardCapabilityTable 或 GenericNode active slot fact。
- [x] 增加 `SYSTem:REFMEM:BOARD?` 和 `SYSTem:REFMEM:TABle? 1`，读取 active board capability snapshot、CRC、table registry validation state、last result 和 evidence；staging 详细字段后续随真实 staging image 增加。
- [ ] 将 BoardCapabilityTable owner validation 接入 `LOAD:SD` 和后续 `LOAD:BEGIN/DATA/END`：baseline、IO constraint、ip_core、hw_profile_crc、default slot 范围必须通过后才能进入 validated staging。
- [x] 实现 `SlotClaimMap` 首版本地聚合，记录 A0-A7 active assignment、claim epoch、physical board uuid、loaded instance mask、claim state、reason 和 CRC；当前从 active default profile 派生，尚未接 RJ45 自组网消息。
- [x] 增加 `SlotClaimEvidence` 或等价诊断视图，记录第 9 到第 16 个未分配候选的 `OVERFLOW` evidence；首版同时记录 duplicate、disabled slot、uuid/hw profile mismatch，并通过 `SYSTem:REFMEM:CLAIM:EVIDence?` 查询。
- [ ] 实现重复 slot claim 检测、uuid mismatch、hardware profile mismatch、stale claim、required hard binding mismatch 和 claim CRC 检查；当前重复 claim、uuid mismatch 和 9-16 candidate overflow 已有纯 C 单元测试覆盖，stale、hard binding 和 claim CRC 仍待实现。
- [x] 实现自组网协调消息静态帧：`CLAIM_HELLO`、`CLAIM_PROPOSE`、`CLAIM_CONFLICT`、`CLAIM_RELEASE`、`CLAIM_RESOLVE`、`CLAIM_COMMIT` 已落地固定 frame header、payload CRC、header CRC 和纯 C单元测试；尚未接入 RJ45 发送/接收、epoch stale 检查和 SlotClaimMap commit。
- [x] 将 `SlotClaimMap` 首版接入 `DistributedDeploymentGate.node_check` 和 `system_manager` config RUN gate：本地 required slot 冲突、错绑、overflow 或缺失时拒绝 RUN；spare dynamic slot 可未 claim。
- [ ] 增加单板 16 候选节点反向验证：一块板可上报 9 到 16 个候选用于溢出验证，但 active assignment 不得生成第 9 个隐式插槽。
- [x] 将默认功能 AO 收敛为模板语义：`PulseCounterAO`、`TriggerMasterAO`、`TriggerAO`、`LinkSwitcherAO`、`InstrumentControllerAO`、`ModelVnaAO` 和 `ModelTurntableAO` 不作为默认固定 active 业务角色运行。
- [ ] 增加动态装载验证：同一个 `Template.LinkSwitcherAO` 候选可以装载到任意满足 PIO/DMA/core1_rt/link_control 和事件/数据连接约束的 A0-A7 slot；不得因默认标签强制绑定 slot A2。
- [ ] 将可加载实例的 SCPI 运行时状态升级为 RefMem NodeLoad staging/activation：LOAD 只写 staging，activation 通过 SlotClaimMap、RealtimeCapabilityContract 和 DeploymentGate 后才进入 active。
- [ ] 增加 `Template.ModelTurntableAO` 动态装载验证：同一固件可把模拟转台候选加载到任意满足 `MODEL_TURNTABLE_PULSE + PULSE_FIRE + PIO/DMA/core1_rt` 的 A slot，不依赖 slot 1。

## P2 - RealtimeCapabilityContract 与 RefMemSlotContract

目标：把节点装载时的实时能力、IO 约束、类 IP 核能力和字段读写规范收敛为 `DistributedRefMemAO` 内部可验证能力，而不是让业务 AO/FB 直接拼地址、写共享内存或绕过 owner。

- [x] 在静态 `refmem_fb_instance_entry_t` 中增加 `ip_core_claim`，首版覆盖 pulse capture/fire、link sequence、BISS-C codec、RJ45 sync delta 和 VDC/DPLL。
- [x] 在静态模型 linter 中把 `ip_core_claim` 映射为 capability gate，确保类 IP 核不会被当成普通 GPIO。
- [x] 将 `Template.LinkSwitcherAO` 声明为 `CORE1_RT + PIO + DMA + LINK_CONTROL`，并补齐 FIRE_LOAD、DONE、FAULT、link timestamp、link sequence state 等事件/数据连接。
- [x] 将 BISS-C 节点声明为 `BISS_C_CODEC` 类 IP 核，要求 PIO、DMA、core1_rt 和 BISS-C IO。
- [x] 定义 `RealtimeCapabilityContract` 首版派生规则：从 NodeLoad、FbInstance、GenericNode 和 BoardCapability 生成实例级资源/IO/类 IP 核能力契约；当前 SlotClaimMap 未落地，先用 `active_default_slot` 建立 B 节点到 A slot 的临时关联。
- [x] 新增 `refmem_realtime_contract.h/.c`，提供资源/IO/类 IP 核 claim 到 capability 的映射，以及 `refmem_realtime_contract_derive()` 首版。
- [x] 将 `RealtimeCapabilityContract` 首版升级为 SlotClaimMap resolved assignment 输入：从 NodeLoad、FbInstance、GenericNode 和 SlotClaimMap 生成实例级资源/IO/类 IP 核能力契约；EventLink/DataLink 和时间预算仍在后续 gate 中补齐。
- [ ] 为 `RealtimeCapabilityContract` 增加 time budget、IP core version、PIO program id、DMA channel policy、IRQ source 和 fallback policy 校验。
- [x] 在 DeploymentGate 中增加 realtime contract 首版检查：缺少 core1_rt、PIO/DMA、IO 约束或类 IP 核能力时拒绝 RUN；IRQ/timer、事件路径和数据 writer 仍在后续 gate 中补齐。
- [x] 在 DeploymentGate 中增加 baseline 首版检查：缺少 `REFMEM` 或 `VDC` 基础能力的物理节点不得进入 distributed RUN；缺少 `VDC_DPLL` 只影响 DPLL owner 候选，不影响普通 VDC 参与节点。
- [ ] 定义类 IP 核能力版本字段，至少覆盖 PIO 程序 id/version、DMA channel policy、IRQ source、timer source、core1 time budget 和 fallback policy。
- [ ] 增加链路控制节点 HIL/板端验证：加载 link-control 候选后确认 `FIRE_LOAD` 可投递到 core1 owner，脉冲捕获和链路序列状态可通过 RefMem snapshot 读取。
- [ ] 定义 `RefMemSlotContract` 派生规则：从 DataLinkTable、Header/Directory、SlotGuard、DeploymentGate 和 QualityTable 生成字段级只读 contract。
- [ ] 新增 `refmem_slot_contract.h/.c`，只提供 `derive_contract`、`validate_write`、`validate_snapshot`、`validate_subscription` 等 RefMemAO 内部 helper。
- [ ] 为 `ModelTurntableAO` 定义首个字段级 contract：loaded/enabled/running、slot_id、output_index、pulse_count、last_tick、motion_phase、error_code、quality_ref。
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
- [ ] 增加 command slot 最小单元测试：重复 post、空闲 take、非目标 take、ACK/NACK 位图、timeout 标记、clear seq 防误清。
- [ ] 将模型加载动作接入 command slot：`CONFigure:MODEl:*:LOAD` 接口层 accepted 后只 post `CONFIG_STAGE` 或 `NODE_LOAD_STAGE`，由 RefMem owner 完成 staging 并 ACK/NACK。
- [ ] 将现有 `system_manager` 配置 ACK 迁移或映射到 RefMem AckCommandSlot snapshot。
- [ ] 扩展 NACK reason 表，补齐 resource busy、RUN denied、payload CRC、epoch mismatch、dup seq、timeout、permission denied。
- [ ] 定义 completion 语义：`local_posted`、`target_taken`、`target_acked`、`all_required_acked`、`durable_committed`。
- [ ] 定义 memory order / fence 规则：payload 写入先于 command publish，ACK/NACK 写入先于 status publish。
- [ ] 评估是否新增 `SYSTem:COMMand:ACK? / NACK?`，并保持 `SYSTem:CONFigure:*` 为兼容配置视图。

## P4 - RefMem Sync Protocol 与 RMA Window

目标：先定义总线无关的 RefMem Sync Protocol，把共同事实同步收敛为受控 delta/epoch/command/fence/quality，而不是远端任意写内存。首版两板验证使用最简单的 PIO SPI 风格 transport adapter；后续可迁移到 BISS-C、RJ45_SYNC_RING、UART、RS485 或其他通讯总线，但不得改变 RefMem 协议语义。

- [x] 定义总线无关 `RefMemSyncProtocol` 固定帧头：magic、version、frame_type、source_slot、target_mask、epoch、run_id、seq、ack_seq、compact_time、CRC。
- [x] 定义 `REFMEM_HELLO`，用于节点发现、能力摘要、MTU、adapter id、layout/app/config CRC 交换。
- [x] 定义 `REFMEM_DELTA(slot_id, slot_version, compact payload)` 帧格式。
- [x] 定义 `REFMEM_EPOCH(epoch, run_id, table_seq)` 帧格式。
- [x] 定义 `REFMEM_COMMAND` / `REFMEM_ACK_NACK` / `REFMEM_FENCE` / `REFMEM_QUALITY` 帧格式。
- [x] 定义 slot delta CRC、seq、source_slot、target_mask、compact timestamp 和 VDC timestamp dictionary 的关系。
- [x] 定义 delta 合并、重放、丢帧、乱序、重复 seq、epoch mismatch 和 stale 策略，要求策略与具体通讯总线无关。
- [x] 定义首版 PIO SPI transport adapter skeleton：adapter id、send/poll 占位、MTU、可选 RX timestamp 能力位、链路计数和错误上报。
- [ ] 定义 RefMem RMA Window 抽象：每个节点只暴露受控 slot mirror，不暴露任意地址。
- [ ] 定义 delta completion 语义：`origin_encoded`、`adapter_queued`、`transport_sent`、`target_received`、`target_validated`、`target_committed`、`visible_in_snapshot`。
- [ ] 定义原子远端更新白名单：仅允许 command flag、seq/heartbeat、quality counter、dirty bitmap 等小字段使用 atomic update。
- [ ] 定义 RMA-style fence：一批 delta 在 `SYNC_EPOCH` 或 `RUN_GATE_CHECK` 前必须完成校验和可见性切换。
- [ ] 将节点新鲜度纳入 `SYNC:CHECk`、`READ:SYNC:*?` 和 TRIG RUN 门禁。
- [ ] 明确 RefMem Sync 与 VDC 的边界：RefMem Sync 负责事实复制、completion、fence 和 quality；VDC 负责 offset/rate、DPLL、holdover 和 DC 时间发布。RefMem frame 只携带紧凑时间戳引用或必要采样点，不运行 DPLL。

## P4.5 - 最小系统板 PIO SPI Adapter Bring-up

目标：在不绑定 BISS-C 的前提下，用两块最小系统板和最简单 PIO SPI 风格 adapter 验证 RefMem Sync Protocol 的最小分布式闭环。当前端口、线序和已验证记录放在 `REFMEM_MIN_SYSTEM_PLAYBOOK.md`。

- [ ] 阶段 0：固化线序与串口生命周期检查，确保 `REALtime:IO:PROFile?`、输出 release、线序检测和脚本退出清理稳定。
- [x] 阶段 1：新增 PIO SPI adapter skeleton，提供 adapter id、state、MTU、tx/rx counter、CRC/drop/timeout/last error snapshot。
- [x] 阶段 1：定义 PIO SPI adapter caps 与 `BoardCapabilityTable` / `RealtimeCapabilityContract` 的映射关系。
- [x] 阶段 2 前置：增加 PIO SPI adapter 单帧 RX staging / loopback 注入能力，先验证 HELLO frame 可被 adapter 接收、缓存、poll 和计数。
- [x] 阶段 2 前置：增加 `REFMEM_HELLO` payload/frame helper，将 board capability、adapter caps、layout/application/config CRC 组合为标准 HELLO frame。
- [x] 阶段 2 前置：增加 `SYSTem:REFMEM:SYNC:*` 维护入口，支持 `INITialize`、`HELLo?`、`EPOCh?`、`RX`、`PEER?`、`QUALity?`、`ADAPter?`；该入口只搬运协议帧和查询 sync snapshot，不写 active fact。
- [x] 阶段 2 前置：固化 `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`，通过 SCPI 在两板之间搬运 HELLO/EPOCH hex frame，验证接收状态机、peer 和 quality。
- [x] 阶段 2：在两块最小系统板上通过 SCPI 搬运执行 `REFMEM_HELLO` 双向发送与接收，交换 layout version、application CRC、capability mask、adapter caps 和 max payload，并将结果写入 HIL 报告。
- [x] 阶段 2：在两块最小系统板上通过 SCPI 搬运执行 `REFMEM_EPOCH` 对齐，epoch/run/table seq 或 CRC bundle 不匹配时拒绝进入 delta active，并将结果写入 HIL 报告。
- [ ] 阶段 2：将当前 SCPI 搬运通路切到真实 PIO SPI 物理 adapter service，保留相同 frame/peer/quality 语义。
- [x] 阶段 3：实现最小 `REFMEM_DELTA` test field，通过 SCPI 搬运从 A 板发布到 B 板 mirror、B 板发布到 A 板 mirror，并切换到 sync mirror snapshot visible。
- [x] 阶段 3：实现 `REFMEM_ACK_NACK` 回传，覆盖 ACK、payload CRC mismatch、duplicate seq 和 target mismatch；当前通过 SCPI bridge 基于最近一次 RX snapshot 生成 ACK/NACK frame，并由对端记录 ack snapshot。
- [x] 阶段 4：实现最小 `REFMEM_FENCE`，验证 required 节点 visible 后 fence passed，min seq 不满足且 deadline 为 0 时进入 timeout evidence snapshot；当前仍是 SCPI bridge 验证，不代表真实 RUN gate 已接入。
- [ ] 阶段 4：将 PIO SPI adapter 的 CRC/drop/late/timeout 计数映射到 `DistributedConnectionQualityTable`；当前已完成总线无关 `REFMEM_QUALITY` frame 和 remote quality snapshot，尚未写入 active QualityTable。
- [x] 增加两板 HIL 工具，顺序管理 COM3/COM4、用户指定串口或 USBTMC VISA 生命周期，不并行占用同一端口。
- [x] 增加 RefMem sync HELLO/EPOCH HIL 报告输出，记录命令 transcript、slot、epoch、run、peer 和 quality 结果。
- [ ] 扩展 RefMem sync HIL 报告输出，记录 build id、package CRC、SlotClaimMap CRC、adapter id、线序 remap、delta/fence/quality 结果；当前已记录 build id、SlotClaimMap CRC、adapter id、delta mirror、ACK/NACK、fence snapshot、QUALITY frame 和 local quality，package CRC 与线序 remap 待接入。

## P4.6 - 最小模型系统 GPIO4..7 Overlay

目标：在两块最小系统板上用 `GPIO4..7` 搭建一个可运行的业务模型闭环，验证“模拟转台 -> 脉冲分发/VDC -> 链路控制 -> VNA 网关 -> 虚拟网分 READY”的分布式事件流。该 overlay 不替代 RefMem Sync transport adapter，也不写入产品板 pin map；表中的 A1-A5 只是当前测试装载选择，不能成为默认固化绑定。

- [x] 记录当前 overlay 测试装载选择：X 板 `A1` 模拟转台、`A2` 模拟网分、`A3` 链路控制；Y 板向后挪为 `A4` 脉冲分发、`A5` VNA 网关。
- [x] 记录当前 GPIO4..7 方向：`GPIO4` X->Y 位置脉冲，`GPIO5` X->Y READY，`GPIO6` Y->X TRIG，`GPIO7` X->Y LINK_SWITCH。
- [x] 固化最小系统 UART 不启用约束：`GPIO4/5` 可作为 overlay PIO 线使用，默认 `PROJECT_ENABLE_UART_STDIO=OFF` 时不得初始化 UART1。
- [x] 新增 debug model board profile 或等价配置表，显式声明 GPIO4..7 overlay 与 UART1 互斥。
- [x] 增加首个可加载模型实例 `ModelTurntableAO`：默认未加载，必须通过 `CONFigure:MODEl:TURNtable:LOAD <slot_id>,<output_index>` 显式选择运行槽位和输出索引。
- [ ] 将 overlay 模型实例纳入 `DistributedNodeLoadTable` / System Pack staging，而不是写成固定默认表：turntable simulator、virtual VNA、link control、pulse distributor、VNA gateway 均应可加载到任意满足能力约束的 A0-A7 slot。
- [ ] 将 `ModelTurntableAO` 当前 debug 输出从 SIO 迁移到 PIO/DMA/core1 预约输出，至少提供 feature flag 或 fallback，避免产品路径依赖维护接口。
- [ ] 为 overlay 定义 `RealtimeCapabilityContract`：每个实例的 GPIO owner、PIO/IRQ/DMA/core1 需求、time budget 和 fallback policy。
- [x] 增加线序/方向安全脚本：运行前 release 双方 GPIO4..7，只逐根拉高输出 owner，确认对端输入和非 owner 不驱动。
- [x] 完成 COM3/COM4 双板 overlay 方向 HIL：build `20260814104920`，package CRC `0x2DF62B6E`，`GPIO4/5/7` X->Y、`GPIO6` Y->X 均验证通过。
- [ ] 增加最小业务 HIL：A1 输出位置脉冲，A4 捕获并更新时间事实，A3 按 VDC 预约输出 `LINK_SWITCH`，A5 输出 `VNA_TRIG` 并等待 A2 `VNA_READY`。
- [ ] 将 overlay 结果写入 RefMem snapshot / quality / evidence，供 `READ:*` 或维护接口读取。

## P5 - 代码组件化

目标：将当前 `distributed_refmem.c` 的兼容壳逐步拆成可维护的 RefMem Domain 组件。

- [ ] 新增 `components/distributed_refmem/README.md`，说明 RefMem Domain 组件边界、owner、SCPI 入口、测试入口和与 VDC/TRIG/CAL/SYSTEM 的关系。
- [ ] 增加 `components/distributed_refmem/CMakeLists.txt`。
- [ ] 新增 `components/distributed_refmem/inc/refmem_domain.h` 和 `src/refmem_domain.c`。
- [x] 新增 `refmem_vector_table.h/.c`。
- [x] 新增 `refmem_application_model.h/.c`。
- [x] 新增 `refmem_table_registry.h/.c`。
- [x] 新增 `refmem_slot_claim.h/.c`。
- [ ] 新增 `refmem_slot_contract.h/.c`。
- [x] 新增 `refmem_sync.h/.c`。
- [x] 新增 `refmem_sync_frame.h/.c`，落地总线无关帧头、HELLO/EPOCH/DELTA payload、encode/decode 和 CRC 校验。
- [x] 新增 `refmem_sync_hello.h/.c`，将 board capability、adapter caps 和版本 CRC 打包为标准 HELLO frame。
- [x] 新增 `refmem_pio_spi_adapter.h/.c`，落地首版 transport caps、send/poll skeleton 和 counters snapshot。
- [ ] 新增 `refmem_command.h/.c`。
- [ ] 新增 `refmem_quality.h/.c`。
- [ ] 让旧 `distributed_refmem.h/.c` 过渡为兼容 wrapper 或逐步拆空。
- [ ] 修改根 `CMakeLists.txt`，从直接列源文件过渡到组件化文件列表。

## P6 - 应用与 SCPI 接入

目标：保持外部接口清晰，SCPI/UI/System Pack 只发起意图和读取 snapshot，不直接拥有 RefMem 事实。

- [ ] 修改 `application/src/app_tasks.c`，把 `task_refmem_sync` 的职责描述收敛到 RefMem Domain owner。
- [ ] 修改 `application/src/app.c`，逐步去掉直接 `distributed_refmem_*` 调用，改成 RefMem Domain 初始化/service。
- [x] 修改 `middleware/scpi_port/src/scpi_system_snapshot_commands.c`，保持 `SYSTem:REFMEM:*` 读取 snapshot 或写 staging load 意图，不触发跨板查询或现场 IO，不直接覆盖 active。
- [x] 保持 `SYSTem:REFMEM:*` 为系统维护入口，不新增裸顶级 `REFMEM` SCPI 域；`SYSTem:REFMEM:SYNC:*` 只作为调试/维护面的协议帧搬运和状态查询入口。
- [ ] 在 `SCPI_COMMAND_PLAN.md` 确认 `SYSTem:REFMEM:*` 暴露字段能够覆盖 table registry、slot freshness、ACK/NACK、CRC、stale、quality gate 和 evidence。

## P7 - 验证

每个实现闭环至少执行文档检查和相关代码构建；涉及板端行为时继续使用 COM6 或当前可用端口烧录/查询。

- [x] 文档检查：`python tools/docs_check/docs_check.py`。
- [x] 构建验证：`cmake --build build-rtos-multicore-smoke`。
- [ ] 新增 RefMem 基础件 smoke 验证脚本：顺序查询 build、claim、table、NodeLoad staging、command ACK、slot contract summary，脚本必须打开一次串口并在退出时关闭。
- [ ] 板端记录 `SYSTem:REFMEM:STATus?`、`SYSTem:REFMEM:NODE?`、`SYSTem:REFMEM:LOAD:STATus?`。
- [ ] 板端验证 `SYSTem:REFMEM:LOAD:NODE` 合法候选 staged、非法 node/instance rejected。
- [ ] 板端验证 `SYSTem:REFMEM:LOAD:SD` 在无 SD、manifest 缺失、manifest OK 三种路径下返回固定 snapshot 且不改 active。
- [x] 板端验证 `SYSTem:STORage:FILE:WRITe:BEGIN/DATA/END` 上传 `/refmem/app_model.rmtp`，再用 `FILE:INFO?`、`FILE:READ?` 和 `SYSTem:REFMEM:LOAD:SD` 完成正向闭环。
- [x] 板端验证通用 Storage 文件管理闭环：目录 create/rename/catalog/delete，文件 write/info/read/rename/delete。
- [ ] 增加 table registry 验证：CRC 正确但 owner validation 失败时不得激活。
- [ ] 增加 table image activation 验证：staging validated 后 active CRC bundle、table seq、rollbackable CRC 和 `SYSTem:REFMEM:TABle?` 状态按预期变化。
- [ ] 增加 activation 失败回滚验证：owner validation 失败、SlotClaim 冲突、DeploymentGate 拒绝和 ACK timeout 都不得污染旧 active image。
- [x] 增加 TableRegistry 纯 C 单元测试入口：`tools/tests/run_refmem_table_registry_tests.ps1`，覆盖 active descriptor、staging activation、gate 失败保持 active 不变、rollbackable descriptor 和无有效 staging 拒绝。
- [x] 增加 SlotClaim gate 正向验证入口：`tools/multicore_board_validate` 和 pytest HIL 查询 `SYSTem:REFMEM:CLAIM?` 与 `SYSTem:CONFigure:STAT?`，确认默认 profile gate ready 一致。
- [ ] 增加 SlotClaim 验证：重复 claim、错绑、stale、9-16 候选 overflow、超过 16 候选 rejected。
- [x] 增加 SlotClaim 纯 C 单元测试入口：`tools/tests/run_refmem_slot_claim_tests.ps1`，覆盖默认 assignment、重复 claim、UUID mismatch 和第 9 个候选 overflow；无 host C 编译器时退化为 ARM GCC 编译检查。
- [x] 增加两块最小系统板组网 baseline 验证工具骨架：`tools/refmem_network_validate/refmem_network_validate.py` 管理两个串口生命周期，确认 `REFMEM + VDC` baseline、SlotClaim gate ready、默认 evidence 为空，并可比较 build id 与 SlotClaimMap CRC。
- [x] 增加两块最小系统板 `debug_min_two_board_link` PIO 预检：读取或记录 active profile 的输入/输出 base pin，确认双方按 profile 交叉连接，且 `GPIO12..15` 未被双板链路占用。
- [ ] 增加两块最小系统板 PIO SPI adapter HIL 验证：按 P4.5 完成 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 闭环；当前 SCPI bridge 已覆盖 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY`，真实 PIO SPI physical adapter 仍待接入。同一验证不依赖 BISS-C。
- [ ] 增加两块最小系统板组网 HIL 验证：确认 `CLAIM_HELLO/PROPOSE/CONFLICT/RESOLVE/COMMIT`、slot 冲突拒绝或协调、RefMem snapshot 一致性和串口生命周期管理。
- [ ] 增加 SlotContract 验证：非法 writer、越界字段、stale snapshot、seqlock 重读。
- [ ] 增加 RMA-style atomic 验证：重复 post、并发 take、payload CRC mismatch、fence 前读取不可见。
- [ ] 增加 `ModelTurntableAO` RefMem 化验证：LOAD staging、activation、start command、pulse snapshot、stop command、quality/evidence 全链路闭环。
- [x] 增加 RefMem Sync frame 纯 C 单元测试入口：`tools/tests/run_refmem_sync_frame_tests.ps1`，覆盖 HELLO encode/decode、payload CRC、header CRC、bad magic、bad type、bad source slot、oversize payload 和短帧。
- [x] 增加 RefMem Sync 接收状态机纯 C 单元测试入口：`tools/tests/run_refmem_sync_tests.ps1`，覆盖 HELLO/EPOCH 接收、target mismatch、epoch mismatch、duplicate/stale/drop 计数和 payload CRC 错误归因。
- [x] 增加 RealtimeCapabilityContract 纯 C 单元测试入口：`tools/tests/run_refmem_realtime_contract_tests.ps1`，覆盖 PIO SPI transport 到 resource/io/ip_core claim 的映射，以及缺少 DMA 或 adapter IP 时拒绝。
- [x] 增加 RefMem Sync HELLO 纯 C 单元测试入口：`tools/tests/run_refmem_sync_hello_tests.ps1`，覆盖 board capability + adapter caps 生成 HELLO payload、编码为 frame、adapter RX staging poll 和字段校验。
