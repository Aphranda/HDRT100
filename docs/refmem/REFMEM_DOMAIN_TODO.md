# Distributed RefMem 内部主域待办

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_TODO.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_TODO.md`, `docs/arch/HAOFV_MAINTENANCE_TODO.md`
Last updated: 2026-10-25

本文档维护 Distributed Vector Blackboard / RefMem Sync Domain 的当前可执行待办。这里不记录普通开发流水账，只记录会影响分布式共同事实、RefMemAO、A0-A7 通用逻辑插槽、节点装载、SlotClaim 协调、表镜像、slot owner、命令 ACK/NACK、部署门禁、连接质量和 RefMem Sync 的架构与实现事项。

## 当前优先插入任务：REFMEM 静态缩容

用户已授权将 [静态缩容评审](../check/submissions/REFMEM_CROSS_REVIEW_01.md) 转为执行待办，
目的是释放 TDMA/DPLL/VDC 调试所需 RAM；完成后返回
[VDC 接续入口](../vdc/VDC_DOMAIN_TODO.md#下次接续入口先释放-ram再返回特等席闭环)。
本节覆盖下述历史主线的本轮优先顺序，但不删除未完成的历史任务。

**当前状态：本轮 RAM 切片完成，四板快速 P3 调试流程及 RefMem 专项验证完成；运输与调度的严格质量失败保留到 VDC 主线。**
实际结果见 `REFMEM-TASK-20260916-001`，不能沿用最初交接时“尚未修改”的状态。当前代码事实源为
`distributed_refmem.h` 的容量/布局符号和 `refmem_vector_table.h` 的结构；评审目标
为保留所有区域 ID、owner、常驻 VDC/DPLL、现有 seqlock 与 node 临界区，缩减静态预留。
实测快照，非事实源：表由 64 KiB 缩至 18 KiB，A/B 链接均净释放 46 KiB；node 保持 8 个
逻辑槽、步长由 512 B 改为 128 B，布局版本由 1 升为 2。最终以真实链接 map 为准。
逻辑槽数本轮不跟随 TDMA 物理编译节点数变更。

| ID | 状态 | 执行项与完成判据 |
|---|---|---|
| `REFMEM-RAM-001` | DONE | 实际表引用、DataLink 区域引用、trigger/io 字节访问及 ARM sizeof 已审查；现有字段满足目标容量，区域 ID 和顺序保持。证据见 `REFMEM-TASK-20260916-001`，不把未来尚未实现的摘要当作本轮字段。 |
| `REFMEM-RAM-002` | DONE | 最小静态缩容及 ABI 断言已实现，目录继续使用真实 offsetof/sizeof；host/ARM/A/B Release、实际 RAM 收益与四板布局/初始化读回通过。保留整表 clear、常驻和已有同步机制；规范 `REFMEM-LAYOUT-01` 保持 pending，不将专项通过等同于全系统集成通过。 |
| `REFMEM-RAM-003` | DONE | host 布局版本、读回版本门禁及 TDMA ring CRC 已同步；真实 CLI→owner 生命周期测试证明旧布局正确 CRC 包拒绝、新包装载及 active/rollbackable 保持，四板版本与逻辑槽访问通过。六节点 CLI 须显式指定 `--tdma-node-count 6`；SD 整包工具仍默认八节点，另列后续能力，不宣称自动 rollback 或混合版本全网准入已通过。 |
| `REFMEM-RAM-004` | DONE | 固定范围内的当前源码 host、A/B Release/map、四板 QUICK_DIAGNOSTIC P3、指纹绑定及 STOP 后 RefMem 专项均完成。该模式允许保留质量失败后完成调试验收，不能额外要求 strict 全绿才关闭 RAM 切片。TDMA 启动超时、运输缺口及调度迟到继续由 `VDC-FAST-002` 承接，不宣称产品严格通过。目录几何由 host 验证，硬件仅读回目录有效/CRC 状态，未导出完整目录字节。 |

本轮验收范围按开始时的快速 P3 配置及 RefMem 缩容专项收敛；新增诊断发现不能自动成为
本轮退出条件。属于本次改动的兼容、容量、初始化或其他直接回归仍须处理；已知运输与
调度质量问题保留原件并进入对应主线任务。不得将“调试切片完成”和“产品严格验收通过”混用。

已定位的实现与验证入口：

- 生产：`components/distributed_refmem/inc/distributed_refmem.h`、`inc/refmem_vector_table.h`、
  `src/refmem_vector_table.c`、`src/refmem_application_model.c`、表 registry 的 owner 验证。
- 必须同步审查 `tools/refmem/refmem_table_image/refmem_table_image.py` 的 `LAYOUT_VERSION`
  和 `tests/python/test_refmem_pack_build.py`；仅改固件会使原工具生成的包与新固件不匹配。
- 现有测试：`tests/unit/test_node_capacity.c`、`tests/unit/test_refmem_table_registry.c`
  的产包/stage/activate/rollback 用例、`tests/python/test_refmem_vector_projection.py`
  和 `test_refmem_pack_build.py`；C registry 运行入口为
  `tools/tests/run_refmem_table_registry_tests.ps1`。本轮新增
  `tests/unit/test_refmem_vector_table.c` 与 `tests/python/test_refmem_layout.py`，
  覆盖真实几何及 CLI 产包到 owner 生命周期，不再沿用实施前“测试文件不存在”的判断。
- 缩容前基线及失败原件见 `VDC-PROGRESS-20260916-042`；新增缩容证据写入
  `out/HardwareAcceptance/20260916/refmem-static-shrink-r1/`，不能覆盖该基线。

布局规范由域 Architecture 的 `REFMEM-LAYOUT-01` 承接；RAM 实测及验收以 Task Progress 为准。
MPU、区域重分类和租约另列后续工作。
缩容不改变 DPLL/VDC 特等席的确定性运输路线，普通 RefMem 仍为后续负载。

## 当前架构基线

以下内容已经作为后续实现的基础，不再作为待办反复展开：

- RefMem 是 HAOFV 内部基础主域，不是对外 SCPI 主域，也不执行业务动作或硬实时边沿。
- A0-A7 是全环唯一的通用逻辑插槽，不等同于固定产品角色；一块物理板可以承载多个实例，但 active assignment 最多 8 个。
- B0-B4 是当前项目或默认 profile 的物理/实例标签，不是 RefMem slot；B 实例加载到哪个 A0-A7 slot 由 `DistributedNodeLoadTable`、`SlotClaimMap` 和 DeploymentGate 共同决定。
- 每个可参与系统的物理节点都必须具备 `REFMEM + VDC + TDMA` 基础能力；`VDC` 表示参与虚拟 DC 时间语义，`VDC_DPLL` 才表示运行 DPLL owner，具体 TDMA adapter 资源由 active foundation profile 与 DeploymentGate 校验。
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
| 4 | TDMA 可预测可靠性 | TDMA 不是“尽力而为发送”；每个 payload 必须有窗口命中、deadline、miss reason、retry/backoff、ACK/FENCE completion 和可查询 evidence。NodeLoad AUTO 必须证明单条 delta 不会因一次 `WINDOW_MISSED` 静默丢失。 |
| 5 | PIO 预约输出路径 | `ModelTurntableAO` 或等价模型必须走真实 PIO scheduled fire，验证“到点出边沿”的硬实时承诺。 |
| 6 | TDMA system node packaging | 将 `components/tdma` 从全局隐式基础件升级为可装载的 HAOFV system node / FB instance：NodeLoad staging 时声明 16-24 通讯环路、PIO、DMA、core1 service、short/long frame capacity 和 payload registry，并由 SlotClaim / RealtimeCapabilityContract / DeploymentGate 检测资源互斥。 |
| 7 | NodeLoad staging/activation | `CONFigure:MODEl:TURNtable:LOAD` 不再只改本地变量，而是形成 NodeLoadTable staging image；可查询、可验证、可拒绝、可激活、可回滚。 |
| 8 | Command / ACK / NACK 基础件 | SCPI 只 post command，owner take 后 ACK/NACK；启动、停止、配置激活和模型加载都能形成闭环状态。 |
| 9 | RefMemSlotContract | 每个 slot/字段有唯一 writer、权限、guard、snapshot 策略和 stale 规则；业务 AO 不能直接裸写共享内存。 |
| 10 | Quality / Evidence | CRC/drop/late/timeout/stale/claim conflict 等进入质量表和 evidence，可由维护接口和报告读取。 |
| 11 | 业务模型闭环 | 在以上基础上逐个接入 `ModelTurntableAO`、`ModelVnaAO`、`LinkSwitcherAO`、`PulseDistributorAO`、`VnaGatewayAO`。 |

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
- [x] P0: 风险评审 §3.4 子项 A：`refmem_pio_spi_adapter_send()` 不能作为假 TX 对外返回成功；旧 `SYSTem:REFMEM:SYNC:*?` frame builder 已收敛为只生成/回放诊断帧，真实发送必须走 TDMA/physical adapter owner 路径；无 TX 后端的 skeleton send 返回 `TX_UNBOUND`。
- [x] P0: 风险评审 §3.4 子项 B：两板真实 RefMem 同步验收。只通过 SCPI 操作 X 板加载两个节点，X 板经真实 TDMA/PIO transport 将 NodeLoad / table image / SlotClaim / quality evidence 同步到 Y 板，Y 板维护查询必须看到一致 snapshot；随后只通过 SCPI 操作 Y 板加载两个节点，同步回 X 板并由 X 板查询确认一致。PC 不得搬运 frame hex 或直接写对端。
- [x] P0: 风险评审 §3.4 子项 B1：定义 NodeLoad entry -> RefMem DELTA 的总线无关编解码基础件，并接入 `DistributedRefMemAO` staging apply；新增 `SYSTem:REFMEM:SYNC:TDMA:NODE:TX` 从本板 staging entry 生成真实 TDMA TX intent，接收端 `TDMA:FRAMe?` 可将 NodeLoad DELTA 交给 RefMemAO staging。
- [x] P0: 风险评审 §3.4 子项 B2：将 NodeLoad DELTA 发送/接收从维护命令触发升级为 RefMemAO/core1 TDMA 自运行窗口；新增 `SYSTem:REFMEM:SYNC:AUTO` / `AUTO?`，默认关闭，可由 SCPI 配置 enable、local slot、target mask、baud、deadline、uplink adapter 和 downlink adapter。两组 adapter 均按无 CS 的 3-wire SPI contract 表达 `duplex_mode,rx,sck,tx`，其中 uplink adapter 用于接收上一个板卡的数据，downlink adapter 用于下发数据到下一个板卡；开启后 RefMemAO service 自动提交 pending NodeLoad TX 或 RX window，core1 TDMA 只执行 PIO/DMA。
- [x] P0: 风险评审 §3.4 子项 B2.1：修正 AUTO RX 与 `LOAD:NODE` 的门禁冲突；`LOAD:SD` / `LOAD:BOARD` 继续要求 realtime idle，`LOAD:NODE` 可由 `DistributedRefMemAO` 判定并抢占自身 AUTO RX window，随后进入 NodeLoad pending TX 队列。SCPI 不直接 abort TDMA，不绕过 RefMemAO owner。
- [ ] P0: TDMA/AUTO NodeLoad 可靠性必须补齐 ACK/重发/fence completion。当前公共 TDMA 已能按窗口执行并记录 `WINDOW_MISSED`，但 AUTO NodeLoad 仍存在“单发 DELTA + 对端 RX window 必须命中”的缺陷；P0 验收要求每条 NodeLoad delta 都有 `origin_encoded -> transport_sent -> target_validated -> target_committed -> acked/fenced` completion 证据，missed window 必须触发有界 retry/backoff，不能静默丢失。
- [ ] P0: TDMA 必须作为可装载 system node 纳入 9 表模型，而不是隐式全局资源。首版可以固定每块板只有一个 active TDMA owner，但 `DistributedNodeLoadTable` / `DistributedFbInstanceTable` / `RealtimeCapabilityContract` 必须能表达 TDMA scheduler、PIO transport、core1 service、GPIO16-24 基础通讯环路、DMA channel、short/long frame payload registry 和方向模式；SlotClaim / DeploymentGate 必须能拒绝第二个 TDMA owner、拒绝业务模型复用 16-24 通讯线、拒绝与 RefMem/VDC 不一致的 payload 挂载。
- [x] P0: 风险评审 §3.4 子项 B3：COM5/COM6 HIL 验收双向两节点同步：X 加载两个节点同步到 Y，Y 加载两个节点同步回 X；验证 NodeLoad staging、TableRegistry CRC、SlotClaimMap、quality/evidence 查询一致。当前 B3 验收使用无 CS 的 3-wire SPI AUTO 配置，区分 uplink adapter（接收上一个板卡）和 downlink adapter（下发到下一个板卡），可配置 half/full duplex mode，不退回 PC frame hex bridge。已用 build `20260815165314` 在 COM5/COM6 完成双向两节点同步，HIL 记录位于 `build-rtos-multicore-smoke/refmem_node_load_auto_hil_COM5_COM6_20260815165314/`。
- [x] P0: 风险评审 §3.10 `OWNER_OK` 只能由 owner validation 结果置位；`refresh_active()` 的编译内置 active entry 已降级为 `ACTIVE_PRESENT|CRC_OK`，不再由 `present + CRC` 伪造 owner provenance。
- [x] P0: 风险评审 §3.11 `SYSTem:REFMEM:LOAD:NODE` / `LOAD:BOARD` staging 不能停在 metadata-only 死胡同；`LOAD:NODE` 已形成私有 NodeLoadTable draft，`LOAD:BOARD` 已形成私有 BoardCapabilityTable draft，单表 draft 已合并为完整 Node Model Candidate，并生成可 activation、可 rollback、可 runtime parse 的 9 表 inline RMTP package image。产品路径不允许单表独立激活或降级为仅诊断入口。
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
- [x] 将 RMTP table image 二进制生成逻辑收敛为共享基础件 `tools/refmem/refmem_table_image/refmem_table_image.py`，独立 package 工具和 SD System Pack staging 复用同一份 header、directory、表顺序、CRC 和默认表 payload。
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
- [x] 定义 `BoardCapabilityTable` 或等价 profile 表，把物理板能力、IO 约束和类 IP 核能力从 `GenericNodeTable` 中逐步拆出；所有板卡条目必须包含 `REFMEM + VDC + TDMA` baseline，`GenericNodeTable` 只保留 A0-A7 slot 基座和 claim policy。
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
- [x] 冻结正常运行禁止周期整表刷新：owner fact 变化只产生 dirty descriptor 和局部 delta；完整 snapshot 只用于节点加入、epoch/CRC 失配、stale 恢复或显式维护，并走 long-frame 分片。
- [ ] 实现通用 dirty queue：记录 slot/field/generation/dirty mask/target mask/priority/deadline，状态事实允许合并，command/event 必须使用有界队列。
- [ ] 实现 per-target generation completion：`transport_sent` 不清 dirty，只有 required target ACK/fence 后清对应 generation；旧 ACK 不得清除发送期间产生的新 dirty。
- [ ] 按 criticality 将 dirty delta 映射到 TDMA：realtime binding 已将 critical delta 净载荷限制为 224 B；仍需实现 dirty queue，并把 background delta 和 bootstrap/resync 接到 maintenance long frame、分片与续传。
- [x] 定义首版 PIO SPI transport adapter skeleton：adapter id、send/poll 占位、MTU、可选 RX timestamp 能力位、链路计数和错误上报。
- [ ] 定义 RefMem RMA Window 抽象：每个节点只暴露受控 slot mirror，不暴露任意地址。
- [ ] 定义 delta completion 语义：`origin_encoded`、`adapter_queued`、`transport_sent`、`target_received`、`target_validated`、`target_committed`、`visible_in_snapshot`。
- [ ] 定义原子远端更新白名单：仅允许 command flag、seq/heartbeat、quality counter、dirty bitmap 等小字段使用 atomic update。
- [ ] 定义 RMA-style fence：一批 delta 在 `SYNC_EPOCH` 或 `RUN_GATE_CHECK` 前必须完成校验和可见性切换。
- [ ] 将节点新鲜度纳入 `SYNC:CHECk`、`READ:SYNC:*?` 和 TRIG RUN 门禁。
- [ ] 明确 RefMem Sync 与 VDC 的边界：RefMem Sync 负责事实复制、completion、fence 和 quality；VDC 负责 offset/rate、DPLL、holdover 和 DC 时间发布。RefMem frame 只携带紧凑时间戳引用或必要采样点，不运行 DPLL。

## P4.5 - 最小系统板 PIO SPI Adapter Bring-up

目标：在不绑定 BISS-C 的前提下，用两块最小系统板和最简单 PIO SPI 风格 adapter 验证 RefMem Sync Protocol 的最小分布式闭环。当前端口、线序和已验证记录放在 `REFMEM_MIN_SYSTEM_PLAYBOOK.md`。

- [x] 阶段 0：固化线序与串口生命周期检查。注意当前最小系统 RefMem/TDMA 通讯环路使用 GPIO16-24 的 uplink/downlink adapter；`REALtime:IO:PROFile?`、输出 release 和 `two_board_io_validate.py` 只验证 GPIO4-7/21-24 业务/观测 overlay，不能作为 RefMem transport 默认 pin 推导来源。`refmem_node_load_auto_hil_validate.py` 默认使用 `min-system-gpio16-24` adapter profile；只有显式 `--adapter-profile io-preflight` 时才调用 IO overlay 线序预检，并把失败证据写入报告目录。
- [x] 阶段 1：新增 PIO SPI adapter skeleton，提供 adapter id、state、MTU、tx/rx counter、CRC/drop/timeout/last error snapshot。
- [x] 阶段 1：定义 PIO SPI adapter caps 与 `BoardCapabilityTable` / `RealtimeCapabilityContract` 的映射关系。
- [x] 阶段 2 前置：增加 PIO SPI adapter 单帧 RX staging / loopback 注入能力，先验证 HELLO frame 可被 adapter 接收、缓存、poll 和计数。
- [x] 阶段 2 前置：增加 `REFMEM_HELLO` payload/frame helper，将 board capability、adapter caps、layout/application/config CRC 组合为标准 HELLO frame。
- [x] 阶段 2 前置：增加 `SYSTem:REFMEM:SYNC:*` 维护入口，支持 `INITialize`、`HELLo?`、`EPOCh?`、`RX`、`PEER?`、`QUALity?`、`ADAPter?`；该入口只搬运协议帧和查询 sync snapshot，不写 active fact。
- [x] 阶段 2 前置：固化 `tools/refmem/refmem_sync_hil_validate/refmem_sync_hil_validate.py`，通过 SCPI 在两板之间搬运 HELLO/EPOCH hex frame，验证接收状态机、peer 和 quality。
- [x] 阶段 2：在两块最小系统板上通过 SCPI 搬运执行 `REFMEM_HELLO` 双向发送与接收，交换 layout version、application CRC、capability mask、adapter caps 和 max payload，并将结果写入 HIL 报告。
- [x] 阶段 2：在两块最小系统板上通过 SCPI 搬运执行 `REFMEM_EPOCH` 对齐，epoch/run/table seq 或 CRC bundle 不匹配时拒绝进入 delta active，并将结果写入 HIL 报告。
- [x] 阶段 2：将当前 SCPI 搬运通路切到真实 PIO SPI 物理 adapter service，保留相同 frame/peer/quality 语义。当前 `SYSTem:REFMEM:SYNC:SPI:*` 只触发帧级 TX/RX；RefMem frame 已在 COM5/COM6 的真实 PIO+DMA 物理链路上以 25 MHz 完成 `RAW/HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 双向闭环。
- [ ] 阶段 2 后续：将 PIO SPI physical adapter 从维护命令触发升级为 core1 realtime TDMA service，core0 只提交帧/窗口意图，PIO+DMA 在 TDMA window 内自行运行，core1 只处理 frame-ready/timeout 摘要。
  - [x] 建立 `refmem_realtime_tdma` service contract：core0 writer 只发布 intent mailbox，core1 writer 只发布 runtime/result snapshot，查询端通过 seqlock 合成状态，避免跨核共享字段双 writer。
  - [x] 将 `refmem_realtime_tdma` 从 RefMem 私有实现抽为 `components/tdma` 基础件；RefMem 侧仅保留兼容适配层，旧 `SYSTem:REFMEM:SYNC:TDMA:*` 查询/控制字段不变，实际调度、scheduled window、transport ops 和 payload binding 由 `tdma_service` 执行。
  - [x] 将 RefMem TDMA payload 注册收敛为标准 contract：`refmem_tdma_payload_register()` 集中注册 `REFMEM_DELTA` 和 `REFMEM_ACK_FENCE`，`refmem_realtime_tdma` 只调用该入口；公共 `tdma_service` submit 阶段拒绝未注册 payload。build `20260816103607` 已在 COM5/COM6 重跑 NodeLoad AUTO 双向两节点同步通过。
  - [ ] P0: 为 AUTO NodeLoad 增加 ACK/重发或 fence completion：当前窗口 missed 时可能丢失单发 DELTA，HIL 已观察到一次 B->A 只应用 1/2 帧后重跑通过；后续不能依赖偶然窗口命中作为可靠同步语义。验收时必须故障注入 `WINDOW_MISSED`、RX timeout 和 duplicate seq，确认未 ACK 的 delta 会在后续 TDMA window 有界重发，已 ACK 的 delta 不重复提交 staging。
  - [ ] P0: 将 TDMA 封装为 HAOFV system node / FB instance 后再接入 NodeLoad：该节点占用 GPIO16-24 基础通讯环路、PIO transport、DMA、core1 realtime service 和 TDMA scheduler；RefMem/VDC/OTA/config/log 等只能注册 payload，不得重复拥有 transport。节点装载时必须输出 resource claim、io claim、ip core claim、payload class whitelist 和 short/long frame capacity，供 SlotClaimMap 和 DeploymentGate 做互斥校验。
  - [x] 抽离后复测 COM5/COM6 NodeLoad AUTO 双向两节点同步：X 板只通过 SCPI 加载两个节点并经 TDMA/PIO 同步到 Y 板，随后 Y 板加载两个节点同步回 X 板；验收已验证 NodeLoad staging CRC、SlotClaimMap、AUTO applied count、last frame type/source 和错误队列。build `20260816095934` 记录位于 `build-rtos-multicore-smoke/refmem_node_load_auto_hil_COM5_COM6_20260816095934_tdma_component/`。
  - [x] 抽离后复测 COM5/COM6 NodeLoad AUTO 空闲维护：同步完成后继续观察两板 RX window 自运行，确认 `pending_count=0`、`last_error=0`、`submitted_rx_count` 和 TDMA `service_count/completed_seq` 继续增长；记录位于 `build-rtos-multicore-smoke/refmem_node_load_auto_hil_COM5_COM6_20260816095934_auto_maintenance/`。
  - [x] 将 TDMA service 接入 `DistributedRefMemAO` 初始化、core1 realtime loop 和维护查询 `SYSTem:REFMEM:SYNC:TDMA:STATus?`。
  - [x] 为 TDMA service 增加 physical ops 边界，并在 `DistributedRefMemAO` 中绑定 PIO+DMA physical adapter wrapper；core1 service 已可通过 intent 执行真实 `transmit/receive`。
  - [x] 增加维护入口 `SYSTem:REFMEM:SYNC:TDMA:TX/RX/FRAMe?/ABORt`，支持 post TDMA intent、读取 TDMA result frame 并进入 RefMem Sync decode。
  - [x] `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 增加 `--transport tdma`，复用现有 frame builder 和 pin remap，以 TDMA intent 路径跑 HELLO/EPOCH/DELTA/ACK/FENCE/QUALITY。
  - [x] 用 COM5/COM6 实板执行 `--transport tdma` HIL，确认不再依赖 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令。
  - [x] 在 TDMA HIL 通过后，将 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令降级为 legacy/diagnostic 或删除，仅保留 line/raw bring-up 必要入口。
  - [x] 增加 TDMA window timeout、DMA overrun、missed window 和 physical adapter error 到 `DistributedConnectionQualityTable` 的正式映射。
  - [x] 增加 `refmem_quality_evaluate_deployment_gate()`，把 runtime quality table 转换为 `DeploymentGate.QUALITY` 的 pass/reject/evidence 结果。
  - [x] 将 quality gate evaluator 接入 `system_manager` config RUN gate，使 TDMA timeout、late、drop 或 physical last_error 能拒绝 RUN 或 latch fault。
- [x] 固化 `tools/refmem/refmem_quality_gate_hil_validate/refmem_quality_gate_hil_validate.py`，脚本化 TDMA timeout -> config RUN gate reject -> config ACK/NACK reject -> OTA restore 闭环。
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
- [x] 增加 SlotClaim gate 正向验证入口：`tools/rtos/multicore_board_validate` 和 pytest HIL 查询 `SYSTem:REFMEM:CLAIM?` 与 `SYSTem:CONFigure:STAT?`，确认默认 profile gate ready 一致。
- [ ] 增加 SlotClaim 验证：重复 claim、缺失 UUID、stale、9-16 候选 overflow、超过 16 候选 rejected。
- [x] 增加 SlotClaim 纯 C 单元测试入口：`tools/tests/run_refmem_slot_claim_tests.ps1`，覆盖默认 assignment、重复 claim、UUID mismatch 和第 9 个候选 overflow；无 host C 编译器时退化为 ARM GCC 编译检查。
- [x] 增加两块最小系统板组网 baseline 验证工具骨架：`tools/refmem/refmem_network_validate/refmem_network_validate.py` 管理两个串口生命周期，确认 `REFMEM + VDC + TDMA` baseline、SlotClaim gate ready、默认 evidence 为空，并可比较 build id 与 SlotClaimMap CRC。
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

## P8 - 统一控制面（REFMEM-CTRL-01）

目标：把“任一板卡接口操作 RefMem、控制全部板卡”从愿景收敛为可验证契约；接入点无关、目标本地执行、显式掩码、单一协调者和聚合完成，不引入远程裸写或跨板阻塞查询。架构见 `REFMEM_DOMAIN_ARCHITECTURE.md` 的 `REFMEM-CTRL-01`。

- [ ] C0 命令槽改由 REFMEM 复制区承载（2026-10-23 修订，替代原 static 冻结结论）：任意板写入本板 RefMem 槽（process-image mailbox，带 seq/CRC）→ 全环同步 → 各板从镜像读命令/落 command slot/ACK 写回；观测直接读 REFMEM 快照。static `s_refmem_command_slot` 仅迁移前保留。验证用启用 flight engine / process-image 的环，`--no-t2` 纯直通环无从板写入口。
  - 进展（2026-10-23，build `20261023144347`）：node slot 128→256 B（8 槽 2 KB）、表 18,432→19,456 B，node slot 已加控制块（`refmem_command_snapshot_t` + seq/CRC + 计数器）；storage 文件写缓冲 16→12 KB 腾出 RAM，TDMA 未动。
  - 进展（2026-10-23，build `20261023152319`）：`refmem_node_control` 写/读/ACK/清除（seq+CRC）已落地；AO 镜像 command slot 到本板控制块，发送侧写入本板记录；`SYSTem:REFMEM:NODE:CONTrol?` 只读快照；host 用例通过，四板 HIL 观测到源 `POSTED`、required 目标 `ACKED`、其余 `TAKEN`。**未完成**：跨板复制该记录（C1 process-image 写路径）与从板写入口。
  - 进展（2026-10-24）：原子入口 `SYSTem:CTRL:EXECute/STATus?/CATalog?` + 命令目录 3 条试点 + 本地分发（LOAD_NODE ACKED、LOAD_BOARD 负路径）通过；`REFMEM:NODES?` 观测落地。**未完成**：params ref（N2.4）、请求/状态分离（N2.5）、跨板同步（N3）。
  - 进展（2026-10-24，build `20261023172516`）：参数池绑定 TLV、请求/状态分离（`update_status`/`apply_request`）、命令帧 `refmem_cmd_frame` build/apply、`EXECute` 帧 loopback（QUEUED→ACKED）；SCPI 全量目录 829 条（386 setter）。**未完成**：跨板发送/远端 apply/租约（N3.2）、REF 路径与 `LOAD_SD`、N4/N5.2。
  - 进展（2026-10-24，build `20261023180940`）：N3.2 分片传输（9 B 记录 / 0x55 解析 / 按 service 节流）落库；本地帧回环通过；**带环远端 HIL 未通过**（参考板 `QUEUED`、目标无记录）。下一步：补 0x55 每类型 RX 与分片 accepted/rejected/submit_fail 计数定位；剩余待办见 `out/refmem/REMAINING_TODO.md`。
  - 进展（2026-10-24，build `20261024022034`）：N3.2 远端投递打通——维护流 8 帧 TX 队列、分片 8 条/帧、动态 `distributed_refmem_local_slot()`、消费门禁修正；COM4 `NODE:CONTrol?` 出现远端请求（target=2/type=14），`SYSTem:REFMEM:CMDFRame?` ok=1；目标 NACK reason=13（RUN 中 config staging 被拒，fail-closed）。下一步：idle 窗口/`STOP→stage→START` 事务（N4）+ ACK 回传。
  - 进展（2026-10-24，N3.2b 回复回程）：目标终态写入本板 process-image ACK 席位（`TDMA_PROCESS_IMAGE_ACK_FLAG_COMMAND`，seq16 + reason），源板逐槽聚合到源 node 控制记录；`SYSTem:CTRL:STATus?` 可见 `ACKED`/`NACKED`，新增 `SYSTem:REFMEM:CMD:REPLy?` 计数。目录新增 `REFMEM_MAINTENANCE_SELF_CHECK` 作为 RUN 中可成功的探针。四板 HIL（P3 build `20261024035648`）：单目标 COM4 `ACKED`、源板 `ACKED`；多目标源板 `ack=0x0E/applied=3`；RUN `LOAD_NODE` 三目标 `NACKED reason=13`、源板 `nack=0x0E/reason=13`；证据 `out/refmem_cmd_reply/p3build_single_try1/`、`p3build_multi_try1/`、`p3build_nack_try1/`。P3 凭证已写（QUICK_DIAGNOSTIC，`config/hardware_acceptance/p3_acceptance_receipt.json`）。**未完成**：seq 仅低 16 位、回复不写其他 observer、协调者租约/非协调者转发（N3.3）、idle 窗口事务（N4）、分片命令启动窗口偶发丢帧（N3.2 P0）。

#### N3.2 传输可靠性细分（2026-10-24）

**失败签名**：196 B 的 `REFTMEM_CMD` 帧经 `refmem_cmd_fragment` 拆成 49 条 9 B 记录，再按 8 条/72 B 打包成 **7 个维护帧**；每个 72 B 帧在 22 B 链路段上占 8 个 chunk，共 **56 chunk**。带环 HIL 观测到参考板 `inject_ok=54`，但目标 `rx_seen=2`（只见到 2 个 chunk），即“源侧已注入、链路上只送达前 2 个”。这不是租约/ACK 逻辑问题，而是维护流的**注入窗口、序列去重和丢失恢复**问题。

**根因假设（N3.2.1 逐项验证，不先下结论）**：

| 假设 | 依据 | 验证手段 |
|---|---|---|
| H1 启动/重试帧复用 `transport_sequence`，从板 `maintenance_rx_sequence` 去重把后续 chunk 丢掉 | `take_maintenance_chunk` 只接受 `view.transport_sequence > maintenance_rx_sequence`；bootstrap 重试可能复用 sequence | 记录每个 chunk 的 emit/accept sequence，检查被去重的 chunk 数与 `inject_ok` 的差 |
| H2 启动窗口（config-control 帧数）短于 56 个 chunk 所需周期 | 维护注入在 `tx_resident_cycle_from` 每次返回周期注入一个 chunk；bootstrap 结束后窗口关闭 | 统计窗口内 config-control 帧数 vs 已注入 chunk 数 |
| H3 core0 提交与 core1 抽取的 8 帧队列缺 release/acquire，core1 看不到新队列项 | `tdma_maintenance_runtime` 的 head/tail/count 只有 `volatile`，无原子/屏障 | 注入前后读队列深度；加 fence 后 A/B |
| H4 不同维护帧的 chunk 交错/重入导致 RX 重组器拒绝新帧 | `tdma_maintenance_stream_rx_chunk` 在 header 不一致时直接 `return false`，不保留新帧 | 记录 RX 重组器拒绝时的 stream_id/index，检查是否新帧被旧帧阻塞 |

**细分任务**：

| ID | 任务 | 变更/边界 | 完成判据 | 依赖 |
|---|---|---|---|---|
| **N3.2.1** | 诊断与计数闭环 ✅（2026-10-24） | 只加只读计数/日志：队列深度、enqueue/reject、chunk emit/accept/dedup、重组 incomplete、每目标 chunk 数、丢失 chunk 掩码；HIL 输出逐 chunk 证据 | 对 1/2/7 帧命令能定位丢在“入队/注入/去重/重组/解析”哪一段；根因 H1–H4 有结论 | — |
| **N3.2.2** | core0/core1 队列交接 ✅（2026-10-24） | `tdma_maintenance_runtime` 的 head/tail/count 改 release/acquire 或等价屏障；满队列显式 `BUSY/REJECTED`，不静默丢 | host 队列用例 + 目标侧压力测试无丢失/重复；队列满可见 | 可与 1 并行 |
| **N3.2.3** | 注入调度与序列 ✅（2026-10-24） | 每个维护 chunk 必须搭载**新 transport sequence**（或维护帧不按 sequence 去重，改按 stream_id/generation/index）；每周期 chunk 配额；不抢占一等席 | 56 chunk 在 data phase 内有界完成；不从板去重计数为 0；位置/VDC 时延无退化 | 1 |
| **N3.2.4** | 逐帧可靠交付 ✅（2026-10-24，丢 1 chunk 已验） | 维护帧/流 ACK（可为 per-stream 位图）+ 有界重试/backoff；TX 保留未 ACK 帧；dedup 按 `stream_id+generation+index` | 故障注入（丢 1 chunk、重复、乱序、窗口 miss）后帧最终完成或带原因失败，不静默丢 | 2、3 |
| **N3.2.5** | 多批命令/环内序列 ✅（预排队两批，2026-10-24） | 一个环启动内支持 >1 批命令（队列深度/顺序/背压），或在中途可靠提交；支持 TAKE→CONFLICT→RELEASE→TAKE 序列 | 同一环运行内三条命令按序完成；租约两板 HIL 不需逐次复位 | 3、4 |
| **N3.2.6** | 命令帧尺寸/参数 ref ❌ 延后（紧凑帧回复回归，已回退） | 评估 `param_kind=ref`/可变参数/快照压缩，把常用命令从 7 维护帧降到 ≤2 帧；`LOAD_SD` 走 ref | `SELF_CHECK`、`LOAD_NODE` ≤2 帧；`LOAD_SD` 只传摘要/ref，不传 bulk | 可与 1 并行 |
| **N3.2.7** | HIL 矩阵与故障注入 ⏳ 部分（2026-10-24） | 四板 process-image 环：1/2/7 帧、单/多目标、NACK、重复、窗口 miss、RX timeout、ring stop→start、soak；逐目标证据 | 全部场景通过；N3.2 可关闭，进入 N3.3 租约 | 3、4、5 |
| **N3.2.8** | 契约/文档/观测入口 ✅（2026-10-24；N3.2.6 延后、N3.2.7 部分已记录） | 同步 `REFMEM-TRANSPORT-01`、`TDMA-PROCESSIMAGE-01`、P8 与 SCPI 计数；不新增 payload class/物理总线；预算仍用 `tdma_traffic_class_profile_t` | 文档门禁绿、登记锚点真实、无第二命令槽/ACK 事实 | 连续 |

**执行顺序**：N3.2.1 → N3.2.2（并行）→ N3.2.3 → N3.2.4 → N3.2.5 → N3.2.7；N3.2.6 可并行；N3.2.8 随每项连续更新。

**N3.2.1 进展（2026-10-24，build `20261024030428`）**：新增维护队列/inject/RX 只读计数与 `SYSTem:REFMEM:CMD:STATus?` 末尾 18 字段；HIL 覆盖 raw16/raw56/raw56_x2/exec196。1/2/7 帧应用层均成功（`t2_static_ok=1`、`refmem_parser_ok=1/2`、`frag_ok=7/frag_complete=1`）；exec196 连续 6 次 + 前序 1 次均为 `frag_complete=1`、源 `state=ACKED`，源侧 `inject_launched=inject_ok=54`、`dropped_unlaunched=0`、`new_sequence=54`、`repeated_sequence=0`、`queue_depth=0`。结论：H1–H4 在源侧均未出现，原 `rx_seen=2` 未复现；`rx_accepted/rx_dedup/rx_chunk_rejects` 含同一 mailbox 重复投递噪声，唯一 chunk 计数留 N3.2.3。证据 `out/refmem_transport_diag/n321_202610241248/`、`n321_exec196_202610241257/`。下一步 N3.2.2 队列 release/acquire 与 N3.2.3 唯一 chunk 计数/注入配额。

**N3.2.2 进展（2026-10-24）**：维护 TX 队列改为 SPSC head/tail + release/acquire；消费者在**帧完全发完后**才推进 head（此前在 `tx_begin` 时推进会让 producer 覆盖仍在发送的槽位）；满队列显式 `queue_full`/拒绝。新增 `tdma_spsc_index` host 用例；queue_full HIL：9 次提交 7 `QUEUED` + 2 拒绝、`queue_full=2`、enqueued/dequeued=7、depth=0。同轮 exec196 出现 COM3 `frag_ok=2/frag_complete=0`，而源侧 54/54 launched、dropped/repeated=0 → 归因 H4：RX 重组器被未完成的旧维护帧阻塞，后续新帧头全部被拒；移交 N3.2.3 的 RX 重启/唯一 chunk 计数。

**N3.2.3 进展（2026-10-24）**：`tdma_maintenance_stream_rx_chunk` 在 header 不一致时**放弃未完成旧帧并重启新帧**（此前永久 `return false`，一次丢 chunk 即 wedge 整个 run）；新增 `restart_count`/`duplicate_count` 与 runtime `rx_restarts`/`rx_duplicate_chunks`，接入 `CMD:STATus?`。host `test_tdma_maintenance_stream` 新增“旧帧未完成 + 新帧头重启/重复 chunk”用例并通过。注入侧保持每周期 1 chunk、每 chunk 新 transport sequence（N3.2.1 实测 54/54 new、0 repeated）；exec196 新 build 连续 6 次 3 目标 `frag_complete=1`。**限制**：重启只防 wedge，不补丢失的维护帧；整帧丢失仍需 N3.2.4 的 per-stream ACK/重试。证据 `out/refmem_transport_diag/n323_exec196_202610241355/`。

**N3.2.4 进展（2026-10-24，丢 1 chunk 已验）**：维护帧改为**帧级 stop-and-wait ACK**：TX 在整帧发完后保留槽位，目标在 process-image ACK 席位回 `stream_id:generation + result`（诊断字节 `0xA0|result`，与命令回复区分）；源端 `tx_ack_ok/tx_retries/tx_ack_fail/tx_awaiting_ack` 可见，50 ms 超时、最多 3 次、每次重试 generation+1。正常 exec196 2/2：`tx_ack_ok=7`、`tx_retries=0`、`frag_complete=1`；注入丢 1 chunk（`SYSTem:REFMEM:CMD:DROP 1`）2/2：`tx_dropped_chunks=1`、`tx_retries=1`、`tx_ack_ok=7`、三目标 `frag_complete=1`、`rx_restarts=1`，证明丢帧后能重传恢复。P3 build `20261024071934`（fingerprint `5a4c52…`）credential written；前三次 DPLL recorder 失败为台架间歇，最终全量重跑通过。证据 `out/refmem_transport_diag/n324_exec196_202610241433/`、`n324_drop1_202610241435/`。**N3.2.4 修正（per-target ACK，2026-10-24）**：soak 20 暴露单 ACK 缺陷——任一目标 ACK 即释放，COM4 漏帧时命令失败 3/20；改为按目标 ACK 掩码，只有 `required_mask` 全覆盖才释放，否则 50 ms 重传整帧。per-target soak 10/10 成功（1 次重传恢复），reply HIL multi 与 drop 1 通过。**未覆盖**：重复/乱序/窗口 miss 的完整矩阵（N3.2.7）、per-chunk 位图（当前整帧重传）。

**N3.2.5 进展（2026-10-24，预排队两批）**：维护 TX 队列 8→16 槽，可同时容纳两条 7 帧命令；HIL `multibatch` 在环启动前排入两条 `REFMEM_MAINTENANCE_SELF_CHECK`，单环按序执行：`submitted=2`、队列 14/14 排空、`inject_ok=108`、`tx_ack_ok=14`、三目标 `frag_ok=14`/`frag_complete=2`、无 full/retry。**限制**：真正的“环运行中再入队”需要与 trn03 并发的测试驱动，当前验证的是“一个环启动内执行 >1 批预排队命令”；mid-ring 并发留 N3.2.7。P3 build `20261024075123`（fingerprint `2f05da…`）credential written。证据 `out/refmem_transport_diag/n325_multibatch3_202610241544/`。

**N3.2.6 进展（2026-10-24，负结果/延后）**：尝试 headerless 56 B codec wire（SELF_CHECK 2 维护帧）：投递/重组成功（`frag_ok=2`、`frag_complete=1`、源 `tx_ack_ok=2`），但目标 `armed/published=0`、源 `CTRL:STATus?` 仍 `POSTED`；补 SELF_CHECK 参数后仍失败。全部改动已回退、未入库。结论：目标执行/reply 触发依赖 v1 快照/信封的某个字段，需目标侧 `cmd_frame_*` 计数才能定段；作为尺寸优化延后，不阻塞 N3.2.1–5 的可靠性主链。

**N3.2.7 进展（2026-10-24，部分）**：N3.2.5 build（P3 `20261024075123`）矩阵：1/2 帧 raw16/raw56 通过；7 帧 exec196 1/1；单目标 `0x02` 源 `target_mask=2`/`required=2`/`ack_flags=2`/`state=ACKED`（COM3/6 仅重组不执行）；`--drop 200` 无完成、源 `POSTED`、无假 ACK（fail-closed）；soak 5/5（2 s 环窗口）；0.5 s 窗口下 2/5 因 stop-and-wait 未排空 → 7 帧命令需 ≥2 s 环窗口。待补：重复/乱序/窗口 miss/RX timeout/ring stop→start/mid-ring/长期 soak。重复：raw56_x2 同一 wire 两次 → 三目标 `refmem_parser_ok=2`、`posted=1`、`acked=1`（第二次被 command try_post 去重）。证据 `out/refmem_transport_diag/n327_normal_202610241700/`、`n327_single_202610241700/`、`n327_soak5b_202610241707/`、`n327_dropfail_202610241705/`、`n327_dup_202610241730/`。per-target ACK 修正后：soak 10/10；soak 20/20（5 次重传恢复、1 次 restart，三目标全部完成）；drop 1 通过（`inject_ok=67`，三目标 `frag_complete=1`）；reply HIL multi `state=4`/`ack_flags=14`；证据 `out/refmem_transport_diag/n327_pertarget_soak10_202610241803/`、`n327_pertarget_drop1_202610241810/`、`out/refmem_cmd_reply/pertarget_multi_202610241810/`。

**N3.2.8 进展（2026-10-24）**：`REFTMEM-TRANSPORT-01` 登记维护流帧级 ACK（复用同一 process-image ACK 席位、诊断 magic `0xA0`、命令回复优先）；`TDMA_DOMAIN_ARCHITECTURE`/`REFMEM_DOMAIN_ARCHITECTURE` 补 stop-and-wait/重试/≥2 s 窗口/优先级；`SCPI_COMMANDS` 已记 `CMD:DROP`；N3.2.6 紧凑帧负结果列为已延后偏差。无新增 payload class、物理总线、第二命令槽。ACK 席位优先级修正后 reply HIL（multi）通过，exec196 normal/drop1 通过；P3 build `20261024090754`（fingerprint `b81745…`）credential written。

**每项完成后的 HAOFV 审查门**：①是否新增总线/payload class/第二命令槽/第二 ACK 事实；②是否占用 core1 硬实时窗口或拉长一等席周期；③单写者与 owner 边界是否保持；④故障是否 fail-closed 且有 reason/evidence；⑤布局/版本/契约是否同步；⑥host + 四板证据是否可复跑。任一项不满足不进入下一子项。

## P8.1 - N3.3 最小租约 / N4 idle 窗口（2026-10-24 启动）

| 里程碑 | 内容 | 验收 | 依赖 |
|---|---|---|---|
| **N3.3.1** ✅ | `refmem_lease` 状态机 + host 用例 | TAKE/CONFLICT/RELEASE/TAKE、stale epoch、expiry、not-owner | — |
| **N3.3.2** ✅（本地 smoke） | 目录 `REFTMEM_LEASE_TAKE/RELEASE` + `refmem_lease_table` + 节点控制 + `SYSTem:REFMEM:LEASE?` | 本地 TAKE→RELEASE→TAKE，`LEASE?` owner/epoch/state 正确 | N3.3.1 |
| **N3.3.3** ✅（2026-10-24） | 两板 HIL：A TAKE→B CONFLICT→A RELEASE→B TAKE，含 ring stop→start | 两板逐目标证据 | N3.3.2、N3.2.5 |
| **N3.3.4** ✅（2026-10-24） | 契约/文档/HAOFV 收口 | 门禁绿 | N3.3.3 |
| **N4.1** ✅（2026-10-24，测试钩子） | idle 窗口门控：RUN 中配置类命令显式 BUSY/REJECT | 不静默执行 | N3.3 |
| **N4.2** ✅（2026-10-24，本地事务 HIL） | STOP→stage→START 事务 + 回滚 | 两板 HIL | N4.1 |
| **N4.3/4.4** ✅（2026-10-24） | HIL + 契约/文档收口 | 门禁绿 | N4.2 |

**N3.3.2 本地 smoke（2026-10-24）**：COM4 本地 `CTRL:EXECute 5,2,2,1,1000` → `LEASE?=1,1,1,90406,1,"GRANTED"`、`CTRL:STATus? state=ACKED`；`EXECute 6,2,2,1` → `FREE`；`EXECute 5,2,2,2,1000` → `1,2,1,"GRANTED"`。CONFLICT/远端路径待 N3.3.3 两板环。

**N3.3.3 两板 HIL（2026-10-24）**：A=COM5、B=COM4；A 经环 TAKE（epoch1）→ B `LEASE?=owner 0,GRANTED`；B 本地 TAKE（epoch2）→ `CONFLICT`（state=5、reason=6 RESOURCE_BUSY），B 仍归 A；**停环后租约保持**（owner 0,GRANTED）；A 第二个环 RELEASE → B FREE；B 本地 TAKE（epoch2）→ `owner 1,GRANTED`、`CTRL:STATus? state=ACKED`。证据 `out/refmem_lease_hil/n333_202610242012/`。

**N4.1 进展（2026-10-24）**：配置类命令（BOARD/NODE/TABLE stage/activate）在 idle 窗口关闭时显式 `RUN_STATE_DENIED`（reason 7），不进入 dispatch；`SYSTem:REFMEM:IDLE:WINDow` 提供测试/事务钩子。HIL：默认开=1；关=0 后 LOAD_NODE → `state=5/reason=7`；开=1 后同命令 → `state=4/reason=0`。证据 `out/refmem_idle_hil/n41_202610242040/`。N4.2 将由 STOP→stage→START 事务管理开/关边界。

**N4.2 进展（2026-10-24）**：`CONFig:BEGIN/COMMit/ABORt/STATus?` 配置事务：BEGIN 开窗并标记事务；窗口内 `LOAD_NODE` ACK；COMMIT 关窗、active=0；随后同命令 `state=5/reason=7`；再次 BEGIN + 无效 stage（node 99）→ `reason=13`（门控已过，业务失败）；ABORt → active=0/window=1。prepared table views 失败丢弃；节点/板级 stage 由各自 dispatch 校验。证据 `out/refmem_config_txn_hil/n42b_202610242035/`。

**N4.3/N4.4 进展（2026-10-24）**：两板（COM4）与四板（COM4/3/6）远程事务 HIL：目标 BEGIN 后 A 经环 `LOAD_NODE` → `reason=13`（门控通过、业务失败）；目标 COMMIT 后同命令 → `reason=7`；BEGIN + 无效 stage → 13；ABORt → 窗口重开。远程配置 stage 当前返回业务 reason 13（远端 owner/permission 未实现），窗口门控 reason 7 可区分。证据 `out/refmem_remote_txn_hil/n43_two_b_202610242118/`、`n43_four_202610242120/`。`REFTMEM-IDLE-01` 已登记（pending）。
- [ ] 扩展 `refmem_command_request_t` / `refmem_command_snapshot_t`：`coordinator_slot`、`coordinator_boot_id`、`lease_epoch`、`catalog_crc32`、`fence_required`、`fence_state`、`control_reply`；旧字段语义不变，host 用例覆盖旧快照兼容。
- [ ] 定义 `control_owner_slot` 解析与唯一性门禁：profile 未声明时取最低已 claim 插槽；`DeploymentGate` 拒绝零个或多个控制 owner 的 RUN。
- [ ] 实现协调者租约：`CONTROL_LEASE_TAKE/RENEW/RELEASE`；仅 STOP/维护态迁移；RUN 中丢失 fail-closed 进入 `CONTROL_LOST`，禁止静默双主。
- [ ] 实现跨节点命令路由：Entry → Coordinator → Target 的 `REFMEM_CMD`/`REFMEM_ACK`；目标本地 owner TAKE、校验、执行、ACK/NACK；origin 通过 `control_reply` 读聚合结果。
- [ ] 实现 `required_mask` 部分成功、timeout、`STOP`/`FAULT_CLEAR` 抢占、重复/重传命中和 `ABORTED_BY_PREEMPT` 证据。
- [ ] 扩展 `refmem_command_reason_t` 与 SCPI reason 表：`UNSUPPORTED_COMMAND`、`CATALOG_MISMATCH`、`CONTROL_BUSY`、`CONTROL_OWNER_CONFLICT`、`TARGET_NOT_CLAIMED`、`ABORTED_LEASE_LOST`、`LATE_ACK`、`SKIPPED_STALE`。
- [ ] 新增 `SYSTem:COMMand:STATus?` / `SYSTem:COMMand:TARget?` 只读聚合视图；`SYSTem:COMMand:ACK?`/`NACK?` 仍读同一底层 command slot，不另建事实。
- [ ] 命令目录版本化：每个 `command_type` 声明 `allowed_states`、`required_capabilities`、`idempotent`、`transactional`、`default_timeout`、`payload_schema`；目录 CRC 进入 Version Bundle。
- [ ] Event Plane 静态装载：新增 `EVENT_CONFIGURE`/`EVENT_VALIDATE`/`EVENT_ACTIVATE` command type 与 `EventPackService` 静态入口；`DistributedRefMemAO` 保持唯一 command slot owner，Event Plane 不 take、不写 ACK、不建第二 epoch；ACK/NACK 写回同一 `AckCommandRegion`。`EVENT_INJECT`/`EVENT_ABORT` 不归 `EventPackService`；方案 B 才由 `EventRuntimeService` 承接。
- [ ] 方案 B R0：新增 `EVENT_INJECT`/`EVENT_ABORT` command type 与 `EventRuntimeService` handler；RefMem 保持唯一 command slot/ACK/lease；POSITION 直接拒绝（`NOT_MY_DOMAIN`）；先 host 契约，不接 POSITION/transport。
- [ ] 权限与运行态矩阵落地：入口权限与目标权限逻辑交集，无 force/override；`START`、`CAL`、`RESOURCE_JOB`、`LOAD:*` 等按矩阵拒绝。
- [ ] C1 两板 HIL：A 接入发起、B 本地执行、A 读到 ACK/fence；C4 多板 all-target `START`/`STOP` 逐目标证据一致；C5 与 `LOAD:SD`/activation 跨节点 FENCE 闭环。
  - 部分完成（2026-10-23，C1a）：`REFTMEM_CMD`（类型 0x52）从参考板 SCPI 入队 → TDMA 维护流 → 三目标 `rx_events/decoded/posted/acked=1`，发送方 `tx_ok=1`；三类载荷解析器与拒绝负测通过。证据见 `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_03.md`。非协调者 Entry 转发、C4 多板 `START`/`STOP`、C5 `LOAD:SD` FENCE 仍未做，C1 保持未勾选。
  - C1b 阻塞（2026-10-23）：从板 Entry 的维护流没有上行注入点——从板转发走 PIO 直通（`PHYSICAL_FLIGHT`/`PHYSICAL_PROCESS_IMAGE`），软件 TX 路径不参与；COM4 Entry 实验源端 `tx_ok=1` 但目标 `rx_events=0`、源端 `inject_attempts=0`。需 process-overlay 注入或专用 uplink mailbox，或把入口收敛到协调者；证据 `out/refmem_cmd_tdma/c1_entry_com4/`。

## P8.2 - N5 统一控制面闭环（2026-10-24 启动）

| 里程碑 | 内容 | 验收 | 依赖 |
|---|---|---|---|
| **N5.0** ⏳（N5.0.2a 完成） | 文件拆分（前置，不改行为）：按关注点拆 `distributed_refmem.c` / `scpi_system_snapshot_commands.c` 等；设行数预算 | 无单文件超预算；build/host/P3/HIL 烟测；行为不变 | 无 |
| **N5.1** ✅（审核完成：LEASE active；TRANSPORT/IDLE pending） | 契约交叉审核 `REFTMEM-LEASE-01`/`IDLE-01`/`TRANSPORT-01` | 交叉审核记录 + 转 active 或记录偏差 | 无（可并行） |
| **N5.2** | 远程配置 owner/permission + 环安全 stage：N5.2a 环运行期 app-model staging（PACKAGE_INVALID）；N5.2b 谁可以 stage 哪个节点；N5.2c 真实 STOP→stage→START | 远程 stage 真正 ACK（N5.2a+b）；未授权 `PERMISSION_DENIED`；两板/四板 HIL；全程 STOP 边界 | N5.0、N5.1 |
| **N5.3** | `control_owner_slot` 冻结（profile 元数据或 BoardCapabilityTable 择一） | 唯一协调者槽 + 只读视图 + 契约 | N5.2 |
| **N5.4** | 协调者租约/唯一性门禁（grant/renew/release/expire） | 非协调者写入/执行被拒绝；TAKE→CONFLICT→RELEASE→TAKE 两板 HIL | N5.3 |
| **N5.5** | 非协调者入口：任一维护接口同语义转发 | 从非协调者板发起同一命令；不新增总线 | N5.4 |
| **N5.6** | 跨节点 FENCE：activation gate 前 required visible/ACK/fence | timeout/reason evidence；两板/四板 HIL | N5.4、N5.5 |
| **N5.7** | 聚合广播/`control_reply` 独立只读副本 | 系统级 `COMMand:STATus?/TARget?`；不复制 node 记录 | N5.6 |
| **N5.8** | `REFTMEM-CTRL-01` 交叉审核转 active + 最终 HIL 矩阵 | 契约/文档/SCPI 同步；门禁绿 | N5.1–N5.7 |

**N5.2a.2 负结果（2026-10-25）**：仅换 profile 指针无效；table 9 的 owner/version 在 profile 数据本身，已被 ring 更新；需 init 时 profile 快照/owner 契约分离。

**N5.2a.1 诊断（2026-10-25）**：ring 后 `lint=2312=0x0908` → error 8 OWNER_VALIDATION、table 9 TDMA foundation profile；不是 registry/权限。N5.2a.2 用稳定 app-model profile 快照/owner 契约修 table 9。

**N5.2a 设计决策（2026-10-25）**：table descriptor 前后一致，失败在 app-model stage；选 **app-model staging 隔离**（N5.2a.1 诊断 → N5.2a.2 staging 基线 → N5.2a.3 改造 → N5.2a.4 HIL），不选 STOP 重置。

**N5.2a 第二次诊断（2026-10-25）**：环 start/stop 后 stage 仍 `staging_state=3/last_error=7`；根因是环 start 已改变 app-model table registry/package，STOP 不恢复；deferral 不是充分条件。下一步先设计 table package/registry 与 TDMA process image 的分离/重建，再写 deferral。

**N5.2 诊断（2026-10-24）**：reason 13 不是 owner/permission；环运行期本地/远程 stage 都失败（`staging_state=3 FAILED`、`last_error=7 PACKAGE_INVALID`、`lint_error=1 BAD_TABLE_VERSION`），环停止本地 stage ACK（`staging_state=2 VALIDATED`）。因此先做 N5.2a；**N5.2a 决策（2026-10-24）：延迟配置事务（STOP-only stage）**——环运行只收命令/排队，STOP 执行，START 后回 ACK；不选环安全 staging（process-image 与 raw-flight 均失败，冲突在环运行期表包/服务）。开放：命令超时跨 STOP/START、队列深度/顺序、多目标 ACK 聚合、COMMIT 与 START 先后。再加 N5.2b 权限；N5.2c 补真实 STOP→stage→START。证据 `out/refmem_remote_txn_hil/n52_diag/REPORT.md`。

#### N5.0 拆分清单（前置）

| 文件 | 总行数 | 拆分目标 |
|---|---:|---|
| `components/distributed_refmem/src/distributed_refmem.c` | 6,370 | core / command / config-txn / cmd-reply / tdma / vdc / diag + `distributed_refmem_internal.h`；单文件 ≤1,200 |
| `middleware/scpi_port/src/scpi_system_snapshot_commands.c` | 6,393 | refmem / tdma / vdc / system / 其它按域拆；单文件 ≤1,300 |
| `components/vdc_dpll_manager/src/vdc_dpll_manager.c` | 4,826 | DPLL 状态机 / 训练 / 观测 / SCPI 适配；单文件 ≤1,500 |
| `components/vdc_domain/src/vdc_domain.c` | 4,460 | 域模型 / 发布 / 观测；单文件 ≤1,500 |
| `components/calibration_manager/src/calibration_manager.c` | 4,138 | 会话 / 采集 / 结果 / 持久化；单文件 ≤1,500 |
| `components/tdma/src/tdma_pio_spi_ring_adapter.c` | 4,116 | PIO/DMA / 配置 / 训练 / 诊断；单文件 ≤1,500 |
| `components/storage_manager/src/storage_manager.c` | 3,566 | 文件 / 目录 / 写缓冲 / 诊断；单文件 ≤1,500 |
| `components/tdma/src/tdma_pio_spi_phys.c` | 3,076 | 物理层 / 诊断；单文件 ≤1,500 |
| `components/distributed_refmem/src/refmem_application_model.c` | 3,180 | 装载 / 校验 / 表；单文件 ≤1,500 |
| `components/sync_trigger/src/trigger_t2_runtime.c` | 2,843 | 会话 / 执行 / 观测；单文件 ≤1,500 |

- **N5.0.1** ✅（2026-10-25）：预算与关注点地图已写入 `DOCS_EXECUTION_CONSTRAINTS.md` 的 `EXE-CODE-01`；新文件 ≤800、拆后 ≤1,500、>2,000 需 TODO/豁免、>3,000 冻结新功能。
- **N5.0.2** `distributed_refmem.c` 先拆；**N5.2a 代码必须等它完成**，避免继续给 6.3k 行文件加功能。
  - **N5.0.2a** ✅（2026-10-24）：config-txn 拆出到 `refmem_config_txn.c`；`distributed_refmem.c` 6,370→6,315；P3 `20261024144807`；config txn HIL 14/14。下一刀 cmd-reply/command/tdma-vdc/diag。
  - **N5.0.2b** ✅（2026-10-24）：cmd-reply 拆出到 `distributed_refmem_cmd_reply.c` + `distributed_refmem_internal.h`；`distributed_refmem.c` 6,315→6,109；P3 `20261024151357`；exec196 HIL `[1,1,1]`。下一刀 command/tdma-vdc/diag。
  - **N5.0.2c** ✅（2026-10-24）：node control 拆出到 `distributed_refmem_node_control.c`；`distributed_refmem.c` 6,109→6,003；P3 `20261024154054`；config txn HIL 14/14。
  - **N5.0.2d** ✅（2026-10-25）：cmd-frame 拆出到 `distributed_refmem_cmd_frame.c`；`distributed_refmem.c` 6,003→5,946；P3 `20261024160525`；exec196 HIL `[1,1,1]`。
  - **N5.0.2e** ✅（2026-10-25）：dispatch/command 拆出到 `distributed_refmem_command.c`；`distributed_refmem.c` 5,946→5,763；P3 `20261024163157`；config txn HIL 14/14。
  - **N5.0.2f** ✅（2026-10-25，设计后成功）：VDC SCPI 拆出到 `scpi_vdc_snapshot_commands.c`；`scpi_system_snapshot_commands.c` 6,393→5,712；P3 `20261024170018`；VDC SCPI smoke 通过。
- **N5.0.2g** ⏳（2026-10-25，A.5 部分）：flight-sync 4 个只读 getter 拆出到 `distributed_refmem_diag.c`；`distributed_refmem.c` 5,690→5,568；P3 `20261025010050`；SCPI smoke 通过。
- **N5.0.2f-A（TDMA flight）** ⏳（A.2b 完成；A.3 第一次回退 2026-10-25）：getter 块跨 inc/`#if`，需按完整条件块切；下一刀 A.3 或 N5.0.5。
- **N5.0.3** ⏳：SCPI refmem 控制块已拆出到 `scpi_refmem_snapshot_commands.c`（5,712→4,627，P3 `20261025001128`）；剩余 sync/TDMA/system 块为 N5.0.3b。
- **N5.0.4** VDC/calibration/TDMA adapter 依次拆；每步单独提交。
- **N5.0.5** 预算门禁（可选 line-count 检查）+ 文档/HAOFV 收口。
- **每步约束**：纯移动/重命名 + 私有头；不改行为；host/build/P3/HIL 烟测；代码/文档分离提交。

**N5.1 审核后 carry-over（未闭环，保持 pending）**：
- **N5.1a** `REFMEM-TRANSPORT-01`：seat rotation `f>=3` 条款与实现/预设不符；需 TDMA 域决定修条款或改实现并补显式断言；维护 ACK 的 clause_loc 跨 TDMA/REFMEM 两个 anchor；P3 报告改为 `PASS_WITH_WARNINGS/strict=false` 的诚实表述。
- **N5.1b** `REFTMEM-IDLE-01`：补 host 用例；把 legacy `SYSTem:REFMEM:LOAD:NODE/BOARD/ACTIVATE` 纳入 idle 窗口门控或缩小条款；补真实 STOP→stage→START 证据；COMMIT 失败需可区分（不能丢弃后仍返回成功）。
- **N5.1c** 数据修正：soak20 `tx_retries=5`（原写 4）；P3 为 `PASS_WITH_WARNINGS`、`strict_gates_passed=false`，不写成 strict passed。
**可选 backlog（不阻塞 N5）**：
- **N3.2-O1** 紧凑帧（2 帧）诊断/回退：先补目标侧 `cmd_frame_*` 计数，决定修复或长期延后。
- **N3.2-O2** 可靠性矩阵补测：乱序、窗口 miss、RX timeout、mid-ring、50× soak；以 per-target ACK 版本为基线。

**顺序建议**：N5.1（docs，零代码）→ N5.2（小改，直接解决远程 reason 13）→ N5.3（冻结协调者槽）→ N5.4（核心）→ N5.5 → N5.6 → N5.7 → N5.8。N5.1/N5.2 可并行。
**关键决策**：N5.3 需在“active profile 元数据”与“BoardCapabilityTable”之间冻结 `control_owner_slot` 派生来源；冻结后写入契约，不在代码里留双源。
## M0 - 单端口统一控制面架构补齐（本目标）

目标：**只通过文档与契约**补齐 `REFMEM-TRANSPORT-01`、`REFMEM-SESSION-01`、`REFMEM-ACTION-01` 三个 canonical 缺口，并冻结 `REFMEM-LOCAL-01`（单板控制保留）；不改代码、不做板端验证。架构见 `REFMEM_DOMAIN_ARCHITECTURE.md` 的四个同名小节。

- [x] M0.1 `REFMEM-TRANSPORT-01`：`REFMEM_CMD` 走 TDMA 二等席（RefMem 数据/ACK/fence 普通路径），四板 A0-A3 显式 target/required；冻结 envelope、`tdma_traffic_class_profile_t` 预算、seat rotation `s>0 & f>=3`、payload ref/size/crc、ACK/fence；TDMA 联审待做。
- [x] M0.2 `REFMEM-SESSION-01`：冻结 `CONFIGURE/ARM/START/STOP/ABORT`、状态机、source/target/required、`T2:MOUNT/UNMOUNT` 兼容映射、ACK/fence。
- [x] M0.3 `REFMEM-ACTION-01`：冻结 `ActionRegistry`/`ActionDispatcher`、`action_id→handler`、STOP 装载、post 非阻塞、缺 handler gate。
- [x] M0.4 `REFMEM-LOCAL-01`：单板 SCPI/slot/owner/T2 保留；本地 target 不走 `REFMEM_CMD`；统一面不可用时本地仍可用，远端/全目标拒绝而非静默缩成单板。
- [x] M0.5 契约登记：四条 pending 行、clause_loc/code_anchor 核对。
- [ ] M0.6 独立复核：另一 agent 复核条款/边界；不涉及代码/板端。当前 R1/R2/HAOFV R1 为同一 agent fresh pass，C11 未签。

证据：`REFMEM_DOMAIN_ARCHITECTURE.md` 四个同名小节；`DOCS_REGISTRY.md` 四条 pending 行；`REFMEM_UNIFIED_CONTROL_REVIEW_01/02.md`、`REFMEM_HAOFV_REVIEW_01.md`。

门禁：M0 只改文档/契约；不新增第二命令槽、不接 POSITION、不让 Event Plane 执行实时边沿。

## M1 - host 实现（当前分支）

目标：只做 host 可编译/可测试的 M1 骨架，不接板、不接 TDMA 实物、不加第二命令槽。

- [x] `REFMEM_CMD` codec：`refmem_cmd_codec.h/.c`，`REFMEM_CMD_WIRE_SIZE` canonical frame、LE、CRC、magic/version/reserved 校验；payload 只走 ref/size/crc。
- [x] `REFMEM_SYNC_FRAME_COMMAND` payload 转换：`refmem_command_request_to/from_sync_payload`，与既有 `refmem_sync_command_payload_t` 对接；host 用例覆盖 mask/seq/type 拒绝。
- [x] `TriggerSession` 状态机：`refmem_trigger_session.h/.c`，CONFIGURE/ARM/START/STOP/ABORT、ARMING、DISARM、DEGRADED、STOP timeout、T2 MOUNT 兼容。
- [x] `ActionRegistry/ActionDispatcher`：`refmem_action_registry.h/.c`，action_id/command_type 查找、有界队列、QUEUE_FULL、UNHANDLED、required handler gate。
- [x] 本地/统一面路由：`refmem_control_route.h/.c`，local-only 不走 `REFMEM_CMD`；统一面不可用拒绝远端/全目标。
- [x] host 测试：5 个新 runner + `run_refmem_all_host_tests.ps1`（设置 sync_trigger include 后跑全部 RefMem host tests）。
- [ ] 接入 CMake/固件主流程（M2 前不做；避免当前分支 WIP 冲突）。
- [ ] 板端/HIL（不在 host M1）。

## 后续实现（不在本目标）

- M2 两板 HIL；M3 两板 T2 session；M4 四板 all-target；M5 C11/产品证据。实现阶段另立任务。
