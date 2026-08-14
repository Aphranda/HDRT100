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

## P0 - 表镜像与加载闭环

目标：先把当前 `LOAD:SD` / `LOAD:NODE` staging 骨架升级为真正可验证、可回滚的表镜像机制。没有这层，后续动态节点加载、SD 加载和类似 OTA 的 RefMem package 都会缺少统一落点。

- [x] 定义静态模型表的 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略，覆盖 ApplicationMap、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 QualityTable。
- [x] 实现 `RefMemTableRegistry` 首版，记录 table id、owner、layout version、active CRC、staging CRC、validation state、validator id、last result 和 evidence；首版反映已编译 active 表和当前 staging snapshot。
- [x] 增加 TableRegistry 可观测生命周期字段：`EMPTY/STAGED/CRC_OK/OWNER_OK/ACTIVE/ROLLBACKABLE/FAILED`。
- [ ] 实现真实 active/staging/rollbackable table image 切换：staging 通过验证后可进入 activation，旧 active 进入 rollbackable。
- [ ] 实现 table dump/load 镜像规则：dump 只导出稳定 snapshot，load 只能进入 staging，不得直接覆盖 active。
- [x] 增加 owner validation contract 首版入口：`refmem_table_registry_validate_staging()` 只校验当前 staging snapshot 的 CRC/lint/error 结果，不执行 active 替换。
- [ ] 实现真实 owner validation callback 调度；CRC 通过后仍必须由表 owner 检查字段范围、逻辑一致性、资源冲突和运行门禁。
- [x] 将 `SYSTem:REFMEM:LOAD:SD` 从 manifest 占位升级为 `.rmtp` table image parser 首版，校验 header、table directory、payload CRC、package CRC 和每表 CRC；当前仍只写 staging snapshot，不替换 active。
- [x] 将 `sd_fs_build.py` 集成 RefMem table image 生成，默认输出 `/refmem/app_model.rmtp`、`/refmem/app_model.idx`、`/refmem/app_model.json`，并在根 `/manifest.idx` 中作为 `required=...,type=refmem_table_image` 引用。
- [ ] 将 `SYSTem:REFMEM:LOAD:NODE` 从单条候选 snapshot 升级为 staging NodeLoadTable image，支持多条候选、CRC、owner validation 和回滚。
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
- [ ] 增加动态装载验证：同一个 `B2.LinkSwitcherAO` 候选可以装载到任意满足 PIO/DMA/core1_rt/link_control 和事件/数据连接约束的 A0-A7 slot；不得因名称中的 B2 默认标签强制绑定 slot A2。

## P2 - RealtimeCapabilityContract 与 RefMemSlotContract

目标：把节点装载时的实时能力、IO 约束、类 IP 核能力和字段读写规范收敛为 `DistributedRefMemAO` 内部可验证能力，而不是让业务 AO/FB 直接拼地址、写共享内存或绕过 owner。

- [x] 在静态 `refmem_fb_instance_entry_t` 中增加 `ip_core_claim`，首版覆盖 pulse capture/fire、link sequence、BISS-C codec、RJ45 sync delta 和 VDC/DPLL。
- [x] 在静态模型 linter 中把 `ip_core_claim` 映射为 capability gate，确保类 IP 核不会被当成普通 GPIO。
- [x] 将默认 profile 中的 `B2.LinkSwitcherAO` 声明为 `CORE1_RT + PIO + DMA + LINK_CONTROL`，并补齐 FIRE_LOAD、DONE、FAULT、link timestamp、link sequence state 等事件/数据连接。
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
- [x] 新增 `refmem_table_registry.h/.c`。
- [x] 新增 `refmem_slot_claim.h/.c`。
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
- [ ] 板端验证 `SYSTem:STORage:FILE:WRITe:BEGIN/DATA/END` 上传 `/refmem/app_model.rmtp`，再用 `FILE:INFO?`、`FILE:READ?` 和 `SYSTem:REFMEM:LOAD:SD` 完成正向闭环。
- [ ] 板端验证通用 Storage 文件管理闭环：目录 create/rename/catalog/delete，文件 write/info/read/rename/delete。
- [ ] 增加 table registry 验证：CRC 正确但 owner validation 失败时不得激活。
- [x] 增加 SlotClaim gate 正向验证入口：`tools/multicore_board_validate` 和 pytest HIL 查询 `SYSTem:REFMEM:CLAIM?` 与 `SYSTem:CONFigure:STAT?`，确认默认 profile gate ready 一致。
- [ ] 增加 SlotClaim 验证：重复 claim、错绑、stale、9-16 候选 overflow、超过 16 候选 rejected。
- [x] 增加 SlotClaim 纯 C 单元测试入口：`tools/tests/run_refmem_slot_claim_tests.ps1`，覆盖默认 assignment、重复 claim、UUID mismatch 和第 9 个候选 overflow；无 host C 编译器时退化为 ARM GCC 编译检查。
- [x] 增加两块最小系统板组网 baseline 验证工具骨架：`tools/refmem_network_validate/refmem_network_validate.py` 管理两个串口生命周期，确认 `REFMEM + VDC` baseline、SlotClaim gate ready、默认 evidence 为空，并可比较 build id 与 SlotClaimMap CRC。
- [x] 增加两块最小系统板 `debug_min_two_board_link` PIO 预检：读取或记录 active profile 的输入/输出 base pin，确认双方按 profile 交叉连接，且 `GPIO12..15` 未被双板链路占用。
- [ ] 增加两块最小系统板组网 HIL 验证：确认 `CLAIM_HELLO/PROPOSE/CONFLICT/RESOLVE/COMMIT`、slot 冲突拒绝或协调、RefMem snapshot 一致性和串口生命周期管理。
- [ ] 增加 SlotContract 验证：非法 writer、越界字段、stale snapshot、seqlock 重读。
- [ ] 增加 RMA-style atomic 验证：重复 post、并发 take、payload CRC mismatch、fence 前读取不可见。
