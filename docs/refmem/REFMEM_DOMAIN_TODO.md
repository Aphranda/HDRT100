# Distributed RefMem 内部主域待办

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_TODO.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/arch/HAOFV_MAINTENANCE_TODO.md`
Last updated: 2026-08-15

本文档维护 Distributed Vector Blackboard / RefMem Sync Domain 的当前可执行待办。这里不记录普通开发流水账，只记录会影响分布式共同事实、RefMemAO、A0-A7 通用逻辑插槽、节点装载、SlotClaim 协调、表镜像、slot owner、命令 ACK/NACK、部署门禁、连接质量和 RefMem Sync 的架构与实现事项。

## 当前架构基线

以下内容已经作为后续实现的基础，不再作为待办反复展开：

- RefMem 是 HAOFV 内部基础主域，不是对外 SCPI 主域，也不执行业务动作或硬实时边沿。
- A0-A7 是全环唯一的通用逻辑插槽，不等同于固定产品角色；一块物理板可以承载多个实例，但 active assignment 最多 8 个。
- B0-B4 是当前项目或默认 profile 的物理/实例标签，不是 RefMem slot；B 实例加载到哪个 A0-A7 slot 由 `DistributedNodeLoadTable`、`SlotClaimMap` 和 DeploymentGate 共同决定。
- 每个可参与系统的物理节点都必须具备 `REFMEM + VDC` 基础能力；`VDC` 表示参与虚拟 DC 时间语义，`VDC_DPLL` 才表示运行 DPLL owner。
- `DistributedGenericNodeTable` 描述通用逻辑插槽基座、基础能力上限、claim policy 和 fail policy；UUID / hw profile 字段只作为兼容或默认 profile 提示，不表达固定物理身份。
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

当前阶段先补真实验证地基，再推进 RefMem 基础件；不能继续只堆静态表模型。业务节点只作为验证载体，不能绕过 RefMem 的 staging、owner、slot contract、command 和 quality 闭环。

| 顺序 | 主线 | 完成判据 |
|---:|---|---|
| 1 | Host C 断言门禁 | 18 个 C 单元测试必须由 host gcc 编译并执行断言；compile-only 只能作为无 host gcc 时的降级信息，不算完整验证通过。 |
| 2 | S0 flash/core1 park-lockout | Flash erase/program 前必须完成 core1 park/lockout/RAM-resident 入口握手，并加入故障注入；该门禁优先于继续扩大 RefMem 表模型。 |
| 3 | 真实最小 transport | 至少两块板通过一条真实物理链路运行 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY`，不再只依赖 PC hex bridge 或本地 stub。 |
| 4 | PIO 预约输出路径 | `ModelTurntableAO` 或等价模型必须走真实 PIO scheduled fire，验证“到点出边沿”的硬实时承诺。 |
| 5 | NodeLoad staging/activation | `CONFigure:MODEl:TURNtable:LOAD` 不再只改本地变量，而是形成 NodeLoadTable staging image；可查询、可验证、可拒绝、可激活、可回滚。 |
| 6 | Command / ACK / NACK 基础件 | SCPI 只 post command，owner take 后 ACK/NACK；启动、停止、配置激活和模型加载都能形成闭环状态。 |
| 7 | RefMemSlotContract | 每个 slot/字段有唯一 writer、权限、guard、snapshot 策略和 stale 规则；业务 AO 不能直接裸写共享内存。 |
| 8 | Quality / Evidence | CRC/drop/late/timeout/stale/claim conflict 等进入质量表和 evidence，可由维护接口和报告读取。 |
| 9 | 业务模型闭环 | 在以上基础上逐个接入 `ModelTurntableAO`、`ModelVnaAO`、`LinkSwitcherAO`、`PulseDistributorAO`、`VnaGatewayAO`。 |

近期不做：

- 不把 `TriggerMasterAO`、`TriggerAO`、`LinkSwitcherAO`、`ModelTurntableAO` 等功能实例重新写死到固定 A slot。
- 不让 SCPI 直接操作硬件动作；SCPI 只表达配置、动作意图和读取 snapshot。
- 不把 PIO SPI adapter 当成最终通讯绑定；它只是最小两板验证载体。
- 不在 RUN 中热改 active profile；所有表修改先进入 staging。

## 2026-08-15 架构偏离审查待办

本节记录本轮代码审查发现的 HAOFV 偏离点，必须逐步消除，不能继续作为“已知临时实现”隐含存在。

- [x] P0: 风险评审 §3.1 跨核 seqlock/guard 写侧必须使用 release 语义；`refmem_realtime_tdma` 的 intent/result guard 和 `refmem_command` 的 command guard 已从普通 `++` 收敛为 `__atomic_add_fetch(..., __ATOMIC_RELEASE)`，读侧 acquire 与写侧 release 成对。
- [x] P0: 风险评审 §3.2 `BoardCapabilityTable` 当前产品 wire payload 固定 8 条；`CLAIM_CANDIDATE_MAX=16` 只保留给 SlotClaim candidate/overflow evidence，不能复用为 BoardCapability 表容量。
- [x] P0: 风险评审 §3.3 DeploymentGate/SlotClaimGate 评估结果必须真正 gate；application model validation、DistributedRefMemAO activation gate 和 SystemManager RUN gate 调用端已显式检查 `evaluate && ready`。
- [ ] P0: 风险评审 §3.10 `OWNER_OK` 只能由 owner validation 结果置位；`present + CRC` 只能进入 `CRC_OK`，不得伪装为 owner 语义通过。
- [ ] P0: 风险评审 §3.11 `SYSTem:REFMEM:LOAD:NODE` / `LOAD:BOARD` staging 不能停在 metadata-only 死胡同；必须形成可 activation、可 rollback、可 runtime parse 的真实 staging table image，或明确降级为不参与 activation 的诊断入口。
- [x] P0: `RefMemTableRegistry` activation 不能只切 descriptor/CRC；在真实 active/staging/rollbackable table image 切换实现前，activation API 必须显式拒绝 active 替换并返回 `IMAGE_NOT_LOADED`。
- [ ] P0: `.rmtp` `LOAD:SD` 不能只验证 package/header/table CRC 后复用当前 active lint；必须解析每张 staging 表并执行 owner validation、SlotClaimMap、RealtimeCapabilityContract、DeploymentGate 和 rollback 规则。
- [x] P0: `SYSTem:REFMEM:LOAD:NODE` 不能只校验单条 node/instance 范围后标记 validated；必须形成 staging NodeLoadTable image，并按候选表重新校验实例、资源、IO、事件/数据连接和 RUN gate。首版已形成私有 staging NodeLoadTable、计算整表 CRC 并只发布 table 3 registry staging entry；activation/rollback 继续留在 P0 后续项。
- [x] P1: `GenericNodeTable` linter 不得强制 `BoardCapabilityTable[i]` 与 `GenericNodeTable[i]` 的 slot、UUID、persona、hw profile 一一相等；GenericNode 只校验 A0-A7 通用 slot substrate，物理身份和能力由 BoardCapability/SlotClaim 约束。
- [x] P2: 旧 `refmem_realtime_contract_derive()` 仍通过 `active_default_slot` 查找 board，后续必须降级为 legacy/internal 或删除，生产路径只允许使用 SlotClaimMap resolved assignment。
- [x] P3: `SYSTem:REFMEM:LOAD:*` 首版全部接入 command slot；`LOAD:NODE`、`LOAD:BOARD` 和 `LOAD:SD` 均由 SCPI 解析意图或 StorageAO 结果摘要后交给 RefMemAO owner take 并 ACK/NACK。后续重点转向真实 active/staging/rollbackable table image。
- [ ] P5: `refmem_vector_table.h` 不应向普通模块公开可变 header/node pointer accessor；可变写入口应收敛到 `distributed_refmem.c` 或 RefMemAO owner API，对外只暴露 snapshot/validated publish helper。

## P0 - 表镜像与加载闭环

目标：先把当前 `LOAD:SD` / `LOAD:NODE` staging 骨架升级为真正可验证、可回滚的表镜像机制。没有这层，后续动态节点加载、SD 加载和类似 OTA 的 RefMem package 都会缺少统一落点。

- [x] 定义静态模型表的 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略，覆盖 ApplicationMap、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 QualityTable。
- [x] 实现 `RefMemTableRegistry` 首版，记录 table id、owner、layout version、active CRC、staging CRC、validation state、validator id、last result 和 evidence；首版反映已编译 active 表和当前 staging snapshot。
- [x] 增加 TableRegistry 可观测生命周期字段：`EMPTY/STAGED/CRC_OK/OWNER_OK/ACTIVE/ROLLBACKABLE/FAILED`。
- [x] 实现 registry 级真实 active/staging/rollbackable table image 切换：`.rmtp` package bytes 进入私有 staging buffer，activation gate 通过后旧 active descriptor/buffer 进入 rollbackable，staging descriptor/buffer 切为 active；metadata-only staging 或失败 staging 必须清空 payload，继续以 `IMAGE_NOT_LOADED` 阻断伪切换。
- [x] 定义并落地 table image descriptor：active/staging/rollbackable 只由 descriptor、seq、CRC bundle、state 和 evidence 对外可见，完整表数据不得写入 RefMem 向量表。
- [x] 定义并落地 stable table view access/release：`refmem_table_registry_access_table()` 只借出 const payload view、CRC、offset、size 和 seq，`release_table()` 释放 reader guard；未 release 的 view 会让 activation 返回 `IMAGE_BUSY`。
- [ ] 增加完整 activation gate：RefMem load mode、产品实时 idle/park、flash lockout/RAM-resident 入口、CRC bundle、owner validation、SlotClaimMap、DeploymentGate 和 command ACK 必须全部通过后才能切 active；当前单板 gate 已接入 RefMem idle、realtime idle、runtime protection、CRC/owner、SlotClaim、quality/DeploymentGate、local command take 和 staging stable table view 预解析，仍需接跨节点 ACK/FENCE。
- [ ] 实现 table dump/load 镜像规则：dump 只导出稳定 snapshot，load 只能进入 staging，不得直接覆盖 active。
- [x] 将 active package view 解析为业务结构表 snapshot：ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality 的外部 getter 已在 activation 成功后切到 activated stable view；内部默认表 lint 抽象和真实 owner callback 调度继续留在后续项。
- [x] 增加 owner validation contract 首版入口：`refmem_table_registry_validate_staging()` 只校验当前 staging snapshot 的 CRC/lint/error 结果，不执行 active 替换。
- [ ] 实现真实 owner validation callback 调度；CRC 通过后仍必须由表 owner 检查字段范围、逻辑一致性、资源冲突和运行门禁。
- [ ] owner validation callback 结果必须写入 TableRegistry：table id、owner id、validator id、result、reason、evidence index 和失败阶段。
- [x] 将 `SYSTem:REFMEM:LOAD:SD` 从 manifest 占位升级为 `.rmtp` table image parser 首版，校验 header、table directory、payload CRC、package CRC 和每表 CRC；当前只写 staging，active 替换必须通过 `SYSTem:REFMEM:LOAD:ACTivate` intent。
- [x] 将 `.rmtp` 中的 `ApplicationMap`、`BoardCapabilityTable`、`GenericNodeTable` 和 `NodeLoadTable` 从 64 字节占位 payload 升级为固定 u32 表镜像；`RefMemTableRegistry` 在 package CRC 通过后解析这四张表并调用 application contract 做 owner validation。
- [x] 将 RMTP table image 二进制生成逻辑收敛为共享基础件 `tools/refmem_table_image/refmem_table_image.py`，独立 package 工具和 SD System Pack staging 复用同一份 header、directory、表顺序、CRC 和默认表 payload。
- [x] 将 `.rmtp` package validation summary 写入 `RefMemTableRegistry` per-table staging entry：descriptor 保留 package CRC，entry 使用 table directory CRC；全 9 张 canonical 表 owner validation 通过后 staging descriptor 进入 `OWNER_OK`，但真实 active image buffer 未落地前仍不得激活。
- [x] 将 `.rmtp` 其余表从占位 payload 升级为真实表镜像，并接入各自 owner validation：FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality。
- [x] 将 `sd_fs_build.py` 集成 RefMem table image 生成，默认输出 `/refmem/app_model.rmtp`、`/refmem/app_model.idx`、`/refmem/app_model.json`，并在根 `/manifest.idx` 中作为 `required=...,type=refmem_table_image` 引用。
- [x] 将 `SYSTem:REFMEM:LOAD:NODE` 从单条候选 snapshot 升级为 staging NodeLoadTable image，支持多条候选、CRC 和 owner validation；回滚仍随真实 active/staging/rollbackable image buffer 统一实现。
- [x] 将 `CONFigure:MODEl:TURNtable:LOAD <slot_id>,<output_index>` 的首版入口接入 RefMem command slot 和 NodeLoad staging snapshot：SCPI 不再直接调用 `model_turntable_load()`，而是由 `DistributedRefMemAO` post `NODE_LOAD_STAGE`、写 staging、调用 registered AO/FB owner、再 ACK/NACK。
- [ ] 将 `CONFigure:MODEl:TURNtable:LOAD <slot_id>,<output_index>` 升级为真实 NodeLoadTable staging image：生成或更新 `Template.ModelTurntableAO` 的候选装载记录，并记录 staging seq、slot、resource/io/ip claim、SlotClaimMap、RealtimeCapabilityContract、DeploymentGate 和拒绝原因。
- [ ] 增加 NodeLoad staging activation 命令或复用现有 config activation：activation 前必须完成 table CRC、instance range、SlotClaimMap、RealtimeCapabilityContract、DeploymentGate 和 command ACK 检查。
- [ ] 增加 NodeLoad rollback/abort 语义：未激活 staging 可 abort；激活失败必须保留旧 active profile，并在 TableRegistry 中记录失败 evidence。
- [x] 增加类似 OTA 的通用 StorageSCPI 文件分块加载：`SYSTem:STORage:FILE:WRITe:BEGIN/DATA/END/ABORt/STATus?`，可写入 `/refmem/app_model.rmtp`；写入后仍需 `SYSTem:REFMEM:LOAD:SD` 进入 RefMem staging。
- [x] 为 StorageAO 文件/目录补齐 CRUD 维护入口：`FILE:INFO?/READ?/DELete/REName`、`DIRectory:CREate/DELete/REName/CATalog?`；SCPI 不直接调用 FatFs，RefMem 向量表不承载文件数据。
- [x] 修复 StorageSCPI 写入 `/refmem/app_model.rmtp` 时反复撞 `RUNNING` 的结构性原因：StorageAO 显式 job 优先于 boot snapshot，Storage task 优先级高于 UI，UI 在 Storage job active 时让路；确认问题不是 RefMem PIO-SPI 与 SD 共用总线，而是 SD/LCD 共用 SPI 和服务优先级叠加。
- [x] 修复 FatFs 原子替换阶段 `RENAME_FAILED` 的兜底路径：text/binary 写入收敛为公共 bytes helper，临时文件 rename 失败后直写目标文件并清理 tmp，避免 `.rmtp` CRC 已匹配但替换失败导致 job 卡死或失败。
- [x] 增加 `SYSTem:REFMEM:TABle? [table_id]` 维护查询，观察 registry、active/staging CRC、validation state 和 evidence；保持在 `SYSTem:REFMEM:*` 命名空间内。
- [x] 增加 `SYSTem:REFMEM:LOAD:ACTivate` 和 `SYSTem:REFMEM:TABle:IMAGe?` 维护入口：SCPI 只提交 activation intent 或读取 descriptor，`DistributedRefMemAO` 通过 `TABLE_PACKAGE_ACTIVATE` command slot 执行 registry 级 activation。
- [x] 增加 `SYSTem:REFMEM:TABle:VIEW? [role],[table_id]` 维护查询：通过 access/release 读取 table view 摘要，验证 RMTP directory 中 active/staging/rollbackable payload 可稳定定位，仍不导出完整 table 数据。
- [ ] 重新 OTA COM5/COM6 并执行 `refmem_pack_write` + `refmem_scpi_load_validate`，确认 9 表 `.rmtp` 写入、读取、`LOAD:SD` staging 和 registry image lifecycle 在最小系统板上闭环通过。

## P1 - SlotClaimMap 与自组网协调

目标：解决分布式环路中多个物理板误用同一 A0-A7 逻辑插槽、同一板卡加载多个实例、候选超过 active 容量和动态协调失败的可诊断问题。

- [x] 在 `GenericNodeTable` 中落 `claim_policy`、`claim_priority`、`node_uuid_crc32`、`hw_profile_crc32`、`online_required` 和 `fail_policy` 字段；其中 UUID / hw profile 只作为兼容或默认 profile 提示，不得作为 A slot 物理绑定依据。
- [x] 在静态模型 linter 中加入 `claim_policy` 基础合法性检查。
- [x] 定义 `SlotClaimProposal` / `SlotClaimMap` 首版数据结构，区分 candidate instance 和 resolved active assignment；同一 physical board 最多上报 16 个 candidate。
- [x] 在 `SlotClaimProposal` 中显式加入 board/profile label，例如 B0-B4、physical uuid、hardware profile CRC、capability mask、IO constraint 和类 IP 核能力摘要，避免把 A0-A7 slot 误当成固定物理板。
- [x] 定义 `BoardCapabilityTable` 或等价 profile 表，把物理板能力、IO 约束和类 IP 核能力从 `GenericNodeTable` 中逐步拆出；所有板卡条目必须包含 `REFMEM + VDC` baseline，`GenericNodeTable` 只保留 A0-A7 slot 基座和 claim policy。
- [x] 将 `BoardCapabilityTable` 纳入 `.rmtp` / SD System Pack 的真实表镜像格式，支持 CRC、schema version 和 `LOAD:SD` owner validation；load/dump 与 rollbackable active image 仍随 P0 active/staging/rollbackable 切换继续完善。
- [x] 增加受控 SCPI staging 入口 `SYSTem:REFMEM:LOAD:BOARD` / `SYSTem:REFMEM:LOAD:BOARD:STATus?`，用于加载或修改单条 board capability 候选；SCPI 不得直接修改 active BoardCapabilityTable 或 GenericNode active slot fact。
- [x] 增加 `SYSTem:REFMEM:BOARD?` 和 `SYSTem:REFMEM:TABle? 1`，读取 active board capability snapshot、CRC、table registry validation state、last result 和 evidence；staging 详细字段后续随真实 staging image 增加。
- [x] 将 BoardCapabilityTable owner validation 接入 `LOAD:SD` package parser：baseline、IO constraint、ip_core 和 default slot 范围必须通过后才能进入 validated staging；SCPI inline `LOAD:BOARD` 已有同等字段检查。
- [x] 实现 `SlotClaimMap` 首版本地聚合，记录 A0-A7 active assignment、claim epoch、physical board uuid、loaded instance mask、claim state、reason 和 CRC；当前从 active default profile 派生，尚未接 RJ45 自组网消息。
- [x] 增加 `SlotClaimEvidence` 或等价诊断视图，记录第 9 到第 16 个未分配候选的 `OVERFLOW` evidence；首版同时记录 duplicate、disabled slot、uuid/hw profile mismatch，并通过 `SYSTem:REFMEM:CLAIM:EVIDence?` 查询。
- [x] 实现重复 slot claim 检测、缺失稳定 UUID、stale claim、claim CRC 和 map CRC 检查；纯 C 单元测试覆盖 duplicate、缺失 UUID、任意 slot claim、stale、claim CRC、map CRC 和 9-16 candidate overflow。注意：UUID 和硬件 profile 是 BoardCapability 身份/能力事实，不得把 A0-A7 固化为物理板或功能角色。
- [x] 实现自组网协调消息静态帧：`CLAIM_HELLO`、`CLAIM_PROPOSE`、`CLAIM_CONFLICT`、`CLAIM_RELEASE`、`CLAIM_RESOLVE`、`CLAIM_COMMIT` 已落地固定 frame header、payload CRC、header CRC 和纯 C单元测试；尚未接入 RJ45 发送/接收、epoch stale 检查和 SlotClaimMap commit。
- [x] 将 `SlotClaimMap` 首版接入 `DistributedDeploymentGate.node_check` 和 `system_manager` config RUN gate：本地 required slot 冲突、缺失 UUID、overflow、stale、CRC 错误或 required missing 时拒绝 RUN；spare dynamic slot 可未 claim。
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
- [x] 定义 `RealtimeCapabilityContract` 首版派生规则：从 NodeLoad、FbInstance、GenericNode 和 SlotClaimMap resolved assignment 生成实例级资源/IO/类 IP 核能力契约；不允许通过 `active_default_slot` 反查 BoardCapability 作为生产路径。
- [x] 新增 `refmem_realtime_contract.h/.c`，提供资源/IO/类 IP 核 claim 到 capability 的映射，以及 `refmem_realtime_contract_derive_from_claim_map()` 生产入口。
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
- [x] 新增 `refmem_command.h/.c`，实现 `try_post`、`try_take`、`ack`、`nack`、`timeout`、`clear` 和 sync payload 映射。
- [x] 增加 command slot 最小单元测试：重复 post、空闲 take、非目标 take、ACK/NACK 位图、timeout 标记、clear seq 防误清。
- [x] 将 `SYSTem:REFMEM:LOAD:NODE` 首版接入 command slot：接口层解析参数后调用 RefMem intent 入口，RefMem 原子清理已完成 command、post `NODE_LOAD_STAGE`、写 NodeLoad staging snapshot 并 ACK/NACK。
- [x] 将 `CONFigure:MODEl:TURNtable:LOAD` 首版接入 command slot：接口层调用 RefMem intent 入口，RefMem post `NODE_LOAD_STAGE`，AO/FB owner 通过注册回调执行加载，完成后写 ACK/NACK；完整 `CONFigure:MODEl:*:LOAD` 家族仍需逐项接入。
- [x] 将 `SYSTem:REFMEM:LOAD:BOARD` 接入 command slot：SCPI 解析字段后只调用 RefMem intent，`DistributedRefMemAO` post `BOARD_CAPABILITY_STAGE`、本地 owner take、写 BoardCapability staging snapshot 并 ACK/NACK；`board_id` 仅作为 profile/candidate payload，不作为 A0-A7 target slot。
- [x] 将 `SYSTem:REFMEM:LOAD:SD` 接入 command slot：StorageAO/RefMemAO 保持职责分离，SD 文件读取和 manifest scan 仍归 StorageAO，`.rmtp` staging 和 TableRegistry package validation 由 `DistributedRefMemAO` 通过 `TABLE_PACKAGE_STAGE` command 完成；Storage 前置失败也必须返回 `REJECTED` 和 NACK completion。
- [ ] 将其余模型加载动作接入 command slot：`CONFigure:MODEl:*:LOAD` 接口层 accepted 后只 post `CONFIG_STAGE` 或 `NODE_LOAD_STAGE`，由 RefMem owner 完成 staging 并 ACK/NACK。
- [x] 将现有 `system_manager` 配置 ACK 迁移或映射到 RefMem AckCommandSlot snapshot。
- [x] 扩展 command NACK reason 查询表，覆盖 resource busy、RUN denied、payload CRC、epoch mismatch、dup seq、timeout、permission denied，并通过 `SYSTem:COMMand:NACK?` 暴露同一底层 reason id。
- [ ] 定义 completion 语义：`local_posted`、`target_taken`、`target_acked`、`all_required_acked`、`durable_committed`。
- [ ] 定义 memory order / fence 规则：payload 写入先于 command publish，ACK/NACK 写入先于 status publish。
- [x] 新增 `SYSTem:COMMand:ACK? / NACK?` 通用 command slot 维护视图，并保持 `SYSTem:CONFigure:*` 只作为配置门禁兼容视图。

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

- [x] 阶段 0：固化线序与串口生命周期检查，确保 `REALtime:IO:PROFile?`、输出 release、线序检测和脚本退出清理稳定；`refmem_sync_hil_validate.py --preflight-io` 会在协议交换前调用 `two_board_io_validate.py` 并把结果写入同一报告目录。
- [x] 阶段 1：新增 PIO SPI adapter skeleton，提供 adapter id、state、MTU、tx/rx counter、CRC/drop/timeout/last error snapshot。
- [x] 阶段 1：定义 PIO SPI adapter caps 与 `BoardCapabilityTable` / `RealtimeCapabilityContract` 的映射关系。
- [x] 阶段 2 前置：增加 PIO SPI adapter 单帧 RX staging / loopback 注入能力，先验证 HELLO frame 可被 adapter 接收、缓存、poll 和计数。
- [x] 阶段 2 前置：增加 `REFMEM_HELLO` payload/frame helper，将 board capability、adapter caps、layout/application/config CRC 组合为标准 HELLO frame。
- [x] 阶段 2 前置：增加 `SYSTem:REFMEM:SYNC:*` 维护入口，支持 `INITialize`、`HELLo?`、`EPOCh?`、`RX`、`PEER?`、`QUALity?`、`ADAPter?`；该入口只搬运协议帧和查询 sync snapshot，不写 active fact。
- [x] 阶段 2 前置：固化 `tools/refmem_sync_hil_validate/refmem_sync_hil_validate.py`，通过 SCPI 在两板之间搬运 HELLO/EPOCH hex frame，验证接收状态机、peer 和 quality。
- [x] 阶段 2：在两块最小系统板上通过 SCPI 搬运执行 `REFMEM_HELLO` 双向发送与接收，交换 layout version、application CRC、capability mask、adapter caps 和 max payload，并将结果写入 HIL 报告。
- [x] 阶段 2：在两块最小系统板上通过 SCPI 搬运执行 `REFMEM_EPOCH` 对齐，epoch/run/table seq 或 CRC bundle 不匹配时拒绝进入 delta active，并将结果写入 HIL 报告。
- [x] 阶段 2：将当前 SCPI 搬运通路切到真实 PIO SPI 物理 adapter service，保留相同 frame/peer/quality 语义。当前 `SYSTem:REFMEM:SYNC:SPI:*` 只触发帧级 TX/RX；RefMem frame 已在 COM5/COM6 的真实 PIO+DMA 物理链路上以 25 MHz 完成 `RAW/HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 双向闭环。
- [ ] 阶段 2 后续：将 PIO SPI physical adapter 从维护命令触发升级为 core1 realtime TDMA service，core0 只提交帧/窗口意图，PIO+DMA 在 TDMA window 内自行运行，core1 只处理 frame-ready/timeout 摘要。
  - [x] 建立 `refmem_realtime_tdma` service contract：core0 writer 只发布 intent mailbox，core1 writer 只发布 runtime/result snapshot，查询端通过 seqlock 合成状态，避免跨核共享字段双 writer。
  - [x] 将 TDMA service 接入 `DistributedRefMemAO` 初始化、core1 realtime loop 和维护查询 `SYSTem:REFMEM:SYNC:TDMA:STATus?`。
  - [x] 为 TDMA service 增加 physical ops 边界，并在 `DistributedRefMemAO` 中绑定 PIO+DMA physical adapter wrapper；core1 service 已可通过 intent 执行真实 `transmit/receive`。
  - [x] 增加维护入口 `SYSTem:REFMEM:SYNC:TDMA:TX/RX/FRAMe?/ABORt`，支持 post TDMA intent、读取 TDMA result frame 并进入 RefMem Sync decode。
  - [x] `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 增加 `--transport tdma`，复用现有 frame builder 和 pin remap，以 TDMA intent 路径跑 HELLO/EPOCH/DELTA/ACK/FENCE/QUALITY。
  - [x] 用 COM5/COM6 实板执行 `--transport tdma` HIL，确认不再依赖 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令。
  - [x] 在 TDMA HIL 通过后，将 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令降级为 legacy/diagnostic 或删除，仅保留 line/raw bring-up 必要入口。
  - [x] 增加 TDMA window timeout、DMA overrun、missed window 和 physical adapter error 到 `DistributedConnectionQualityTable` 的正式映射。
  - [x] 增加 `refmem_quality_evaluate_deployment_gate()`，把 runtime quality table 转换为 `DeploymentGate.QUALITY` 的 pass/reject/evidence 结果。
  - [x] 将 quality gate evaluator 接入 `system_manager` config RUN gate，使 TDMA timeout、late、drop 或 physical last_error 能拒绝 RUN 或 latch fault。
- [x] 固化 `tools/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`，脚本化 TDMA timeout -> config RUN gate reject -> config ACK/NACK reject -> OTA restore 闭环。
- [x] 阶段 3：实现最小 `REFMEM_DELTA` test field，通过 SCPI 搬运从 A 板发布到 B 板 mirror、B 板发布到 A 板 mirror，并切换到 sync mirror snapshot visible。
- [x] 阶段 3：实现 `REFMEM_ACK_NACK` 回传，覆盖 ACK、payload CRC mismatch、duplicate seq 和 target mismatch；当前通过 SCPI bridge 基于最近一次 RX snapshot 生成 ACK/NACK frame，并由对端记录 ack snapshot。
- [x] 阶段 4：实现最小 `REFMEM_FENCE`，验证 required 节点 visible 后 fence passed，min seq 不满足且 deadline 为 0 时进入 timeout evidence snapshot；当前仍是 SCPI bridge 验证，不代表真实 RUN gate 已接入。
- [x] 阶段 4：将 PIO SPI adapter 的 CRC/drop/late/timeout 计数映射到 `DistributedConnectionQualityTable` 派生运行态视图；当前通过 `refmem_quality.h/.c` 生成 `runtime quality snapshot`，不热写 active static QualityTable。
- [x] 增加两板 HIL 工具，顺序管理 COM3/COM4、用户指定串口或 USBTMC VISA 生命周期，不并行占用同一端口。
- [x] 增加 RefMem sync HELLO/EPOCH HIL 报告输出，记录命令 transcript、slot、epoch、run、peer 和 quality 结果。
- [x] 扩展 RefMem sync HIL 报告输出，记录 build id、package CRC、SlotClaimMap CRC、adapter id、线序 remap、delta/fence/quality 结果；当前报告已记录 package CRC、线序 remap、IO preflight、SlotClaimMap CRC、adapter id、delta mirror、ACK/NACK、fence snapshot、QUALITY frame 和 local quality。

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
- [x] 新增 `refmem_command.h/.c`。
- [x] 新增 `refmem_quality.h/.c`。
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
- [x] 板端验证 `SYSTem:REFMEM:LOAD:NODE` 合法候选 staged、非法 node/instance rejected；COM5 build `20260815114032` 已验证合法候选只更新 `NodeLoadTable` table 3 staging CRC 和 `staging_table_mask=0x08`。
- [x] 板端验证 `SYSTem:REFMEM:LOAD:BOARD` 合法候选 ACK、非法 board_id NACK，并确认 `SYSTem:COMMand:ACK?` 暴露 `BOARD_CAPABILITY_STAGE` completion。
- [x] 板端验证 `SYSTem:REFMEM:LOAD:SD` 在当前 SD package 未有效 staging 时返回固定 `REJECTED` snapshot，不改 active，并通过 `SYSTem:COMMand:ACK?` 暴露 `TABLE_PACKAGE_STAGE` NACK。
- [ ] 扩展 `SYSTem:REFMEM:LOAD:SD` 板端验证：覆盖无 SD、manifest 缺失、manifest OK 且 package valid 三种路径，并确认 valid package 的 TableRegistry per-table staging CRC。
  - [x] COM6 覆盖 manifest OK + 9 表 package valid 正向路径：通用 Storage SCPI 写入 `/refmem/app_model.rmtp`，HIL manifest gate OK，`LOAD:SD` 返回 `STAGED`。
  - [x] COM5 覆盖 512B bounded read 优化后的 manifest OK + 9 表 package valid 正向路径：`LOAD:SD` 返回 `STAGED`，错误队列为 0。
  - [ ] 覆盖无 SD 路径。
  - [ ] 覆盖 manifest 缺失路径。
  - [x] 增加 TableRegistry per-table staging CRC 查询脚本化断言，避免只检查 `STAGED` 字符串；COM5 build `20260815113037` 已验证全 9 张表 staging CRC 与 RMTP directory 一致。
- [x] 首轮优化 `SYSTem:REFMEM:LOAD:SD` 耗时：StorageAO 内部 bounded read 从 128B 提到 512B，4800B package 加载和状态查询在 COM5 上约 6.944 s。
- [ ] 支线优化 `SYSTem:REFMEM:LOAD:SD` 耗时：当前 512B bounded read 路径已经可用，bounded stream read job 或更大的 StorageAO 读事务转为支线，不阻塞 RefMem 主线；实现时仍保持 SCPI 只发起意图。
- [x] 复核无参 `SYSTem:REFMEM:LOAD:SD` 错误队列污染：本轮干净串行测试未复现，之前为 PC 侧引号错误/同 COM 并发访问导致的响应串扰。
- [x] 板端验证 `SYSTem:STORage:FILE:WRITe:BEGIN/DATA/END` 上传 `/refmem/app_model.rmtp`，再用 `FILE:INFO?`、`FILE:READ?` 和 `SYSTem:REFMEM:LOAD:SD` 完成正向闭环。
- [x] 板端验证通用 Storage 文件管理闭环：目录 create/rename/catalog/delete，文件 write/info/read/rename/delete。
- [ ] 增加 table registry 验证：CRC 正确但 owner validation 失败时不得激活。
- [x] 增加 table image activation 验证：COM5/COM6 build `20260815121205` 已通过 `LOAD:SD -> LOAD:ACTivate -> TABle:IMAGe? -> TABle? 0..8`，确认 active CRC bundle、table seq、staging 清零和 `ACTIVE` 状态按预期变化；首轮没有旧 active package bytes，因此 rollbackable descriptor 为空，后续需增加二次 activation 回滚验证。
- [ ] 增加 activation 失败回滚验证：owner validation 失败、SlotClaim 冲突、DeploymentGate 拒绝和 ACK timeout 都不得污染旧 active image。
- [x] 增加 TableRegistry 纯 C 单元测试入口：`tools/tests/run_refmem_table_registry_tests.ps1`，覆盖 active descriptor、staging activation、gate 失败保持 active 不变、rollbackable descriptor 和无有效 staging 拒绝。
- [x] 增加 SlotClaim gate 正向验证入口：`tools/multicore_board_validate` 和 pytest HIL 查询 `SYSTem:REFMEM:CLAIM?` 与 `SYSTem:CONFigure:STAT?`，确认默认 profile gate ready 一致。
- [ ] 增加 SlotClaim 验证：重复 claim、缺失 UUID、stale、9-16 候选 overflow、超过 16 候选 rejected。
- [x] 增加 SlotClaim 纯 C 单元测试入口：`tools/tests/run_refmem_slot_claim_tests.ps1`，覆盖默认 assignment、重复 claim、UUID mismatch 和第 9 个候选 overflow；无 host C 编译器时退化为 ARM GCC 编译检查。
- [x] 增加两块最小系统板组网 baseline 验证工具骨架：`tools/refmem_network_validate/refmem_network_validate.py` 管理两个串口生命周期，确认 `REFMEM + VDC` baseline、SlotClaim gate ready、默认 evidence 为空，并可比较 build id 与 SlotClaimMap CRC。
- [x] 增加两块最小系统板 `debug_min_two_board_link` PIO 预检：读取或记录 active profile 的输入/输出 base pin，确认双方按 profile 交叉连接，且 `GPIO12..15` 未被双板链路占用。
- [x] 增加两块最小系统板 PIO SPI adapter HIL 验证：按 P4.5 完成 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 闭环；build `20260815031915` 在 COM5/COM6 上通过 25 MHz PIO+DMA physical adapter HIL，同一验证不依赖 BISS-C。
- [x] 增加 core1 TDMA service HIL 验证：不通过 SCPI 在每帧前后阻塞等待，改为 core0 配置窗口、core1/realtime service 驱动 PIO+DMA 环路，报告 frame-ready、missed window、DMA overrun 和 quality evidence。
- [ ] 增加两块最小系统板组网 HIL 验证：确认 `CLAIM_HELLO/PROPOSE/CONFLICT/RESOLVE/COMMIT`、slot 冲突拒绝或协调、RefMem snapshot 一致性和串口生命周期管理。
- [ ] 增加 SlotContract 验证：非法 writer、越界字段、stale snapshot、seqlock 重读。
- [ ] 增加 RMA-style atomic 验证：重复 post、并发 take、payload CRC mismatch、fence 前读取不可见。
- [ ] 增加 `ModelTurntableAO` RefMem 化验证：LOAD staging、activation、start command、pulse snapshot、stop command、quality/evidence 全链路闭环。
- [x] 增加 RefMem Sync frame 纯 C 单元测试入口：`tools/tests/run_refmem_sync_frame_tests.ps1`，覆盖 HELLO encode/decode、payload CRC、header CRC、bad magic、bad type、bad source slot、oversize payload 和短帧。
- [x] 增加 RefMem Sync 接收状态机纯 C 单元测试入口：`tools/tests/run_refmem_sync_tests.ps1`，覆盖 HELLO/EPOCH 接收、target mismatch、epoch mismatch、duplicate/stale/drop 计数和 payload CRC 错误归因。
- [x] 增加 RealtimeCapabilityContract 纯 C 单元测试入口：`tools/tests/run_refmem_realtime_contract_tests.ps1`，覆盖 PIO SPI transport 到 resource/io/ip_core claim 的映射，以及缺少 DMA 或 adapter IP 时拒绝。
- [x] 增加 RefMem Sync HELLO 纯 C 单元测试入口：`tools/tests/run_refmem_sync_hello_tests.ps1`，覆盖 board capability + adapter caps 生成 HELLO payload、编码为 frame、adapter RX staging poll 和字段校验。
- [x] 增加 RefMem Quality 派生视图纯 C 单元测试入口：`tools/tests/run_refmem_quality_tests.ps1`，覆盖本地 adapter 计数、remote QUALITY snapshot、runtime table 拼装和越界查询。
- [x] 增加 RefMem Command Slot 纯 C 单元测试入口：`tools/tests/run_refmem_command_tests.ps1`，覆盖 post/take/ack/nack/timeout/clear 和 sync COMMAND/ACK_NACK payload 映射。
