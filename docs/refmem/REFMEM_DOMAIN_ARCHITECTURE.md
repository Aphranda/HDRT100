# Distributed RefMem 内部主域架构

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/interface/SCPI_COMMAND_PLAN.md`
Last updated: 2026-08-13

本文档定义 Distributed Hard Real-Time Trigger System 在 HAOFV 下的 Distributed Vector Blackboard / RefMem Sync 内部主域。RefMem Domain 不是对外 SCPI 主域，也不是产品业务动作域，而是分布式系统的内部基础主域，负责把多节点共同事实、静态分布式应用模型、命令意图、ACK/NACK、版本、质量和证据组织成可验证的数据面。

## 主域定位

RefMem Domain 的正式定位：

```text
Distributed Vector Blackboard / RefMem Sync Domain
```

工程内部简称：

```text
RefMem Domain
```

它回答的问题是：

```text
分布式系统中，所有节点对当前系统事实的共同认知是什么。
```

它不回答：

```text
产品测试流程下一步做什么。
硬实时边沿什么时候输出。
某个 SCPI 指令的业务语义如何解析。
```

## 职责边界

RefMem Domain 负责：

- 维护 64 KB `DistributedVectorTable`。
- 维护静态分布式应用模型：
  - `DistributedApplicationMap`
  - `DistributedFbInstanceTable`
  - `DistributedEventLinkTable`
  - `DistributedDataLinkTable`
  - `DistributedDeploymentGate`
  - `DistributedConnectionQualityTable`
- 管理 slot owner / writer / reader。
- 管理 command slot / ACK / NACK / busy / timeout。
- 管理 slot seq / CRC / stale / version / dirty。
- 承接跨节点 `REFMEM_DELTA` / `REFMEM_EPOCH` 同步。
- 给 SCPI/UI/System Pack 提供 snapshot。
- 给 ConfigGate / RUN gate 提供部署一致性判断。
- 给 Diagnostics / Report 提供 evidence。

RefMem Domain 不负责：

- 不执行业务动作。
- 不驱动硬实时边沿。
- 不解析产品命令语义。
- 不传输 OTA payload、日志全文、波形、SD 文件内容或硬实时边沿。
- 不引入完整 IEC 61499 分布式运行时。
- 不支持运行时动态部署 FB、跨节点 FB 直接调用或动态事件路由。
- 不计算 VDC offset/rate，也不执行 DPLL；VDC 共同时间由 VDC Domain owner 发布，RefMem 只保存其 snapshot、版本、质量和 evidence。

## HAOFV 层级

RefMem Domain 位于 HAOFV 的内部基础架构层：

```text
SCPI / UI / System Pack
        ↓
SystemAO / ConfigGate / Domain AO
        ↓
Distributed RefMem Domain
        ↓
Domain Vector / Slot / RefMem Sync
        ↓
RJ45_SYNC_RING / local shared memory
```

和业务域的关系：

```text
TRIGger / LoopEngine / CALibration / SYNC / MEASure
        ↓ publish/read facts
Distributed RefMem Domain
        ↓ sync small delta
Other nodes
```

## 当前 Canonical Model

本节是当前实现和后续待办的收敛点。若下文历史推导与本节冲突，以本节为准。

- `DistributedRefMemAO` 是 RefMem Domain 的唯一运行 owner；SCPI、UI、System Pack 和业务 AO/FB 只提交意图或读取 snapshot，不直接改 active fact。
- A0-A7 是全环唯一的通用逻辑插槽，代表可装载 slot substrate，不代表固定产品角色；具体角色由 `DistributedNodeLoadTable` 绑定到 AO/FB instance。
- 一块物理板可以加载多个逻辑实例，例如调试阶段同时承载模拟网分和模拟转台；active assignment 仍最多 8 个，candidate proposal 上限为 16。
- `DistributedGenericNodeTable` 只描述通用逻辑插槽基座、硬件身份、capability、claim policy 和 fail policy；不得把业务 role/persona 反向塞回 GenericNode。
- `DistributedNodeLoadTable` 是 profile 到 A0-A7 的实例装载表；同一插槽允许多条 enabled load，但必须通过 DeploymentGate 的资源、IO、owner、writer、事件和数据连接检查。
- NodeLoad 装载实例时必须同时装载该实例的实时能力契约；也就是 role/persona、`resource_claim`、`io_claim`、`ip_core_claim`、事件连接和数据连接必须作为同一个 active profile 的部署事实被验证。
- `SlotClaimMap` 是运行期协调结果，不是新的 slot 空间；未分配 candidate 只能进入 evidence，不能生成 A8 或隐式节点。
- `RefMemSlotContract` 是 `DistributedRefMemAO` 从 DataLink、Directory、SlotGuard、Gate 和 Quality 派生出的字段级只读契约，不是 AO/FB 对外业务 API。
- RefMem load mode 是 RefMem 自身的表镜像状态机；`LOAD:SD`、`LOAD:NODE`、后续 `LOAD:BEGIN/DATA/END/ABORT` 只允许写 staging image，active image 必须经 CRC、lint、owner validation 和 activation。
- 运行期不做热替换 active image；RUN 前 gate 可以激活已验证 image，RUN 中只能发布事实、命令、ACK/NACK、quality 和 evidence。

## 静态分布式模型

RefMem Domain 吸收 IEC 61499-style 分布式运行时的优点，但保留静态、可验证、产品化的实现方式。

### 节点模型硬规则

RefMem 的底座只固定 **A0-A7 八个通用插槽**。A0-A7 是 slot substrate 和同步协议中的通用 `node_id`，更准确地说是 8 个可装载插槽，不代表永久固定的产品角色。

模型节点、脉冲分发节点、链路切换节点、仪表控制节点、模拟网分节点、模拟转台节点、网关节点都不是额外的固定节点类型，也不是独立于 A0-A7 之外的表空间。它们是加载到 A0-A7 某个通用插槽上的 role、persona 或 AO/FB instance：

```text
A0-A7 generic slot substrate
  + NodeRoleMap
  + persona / feature_mask
  + DistributedFbInstanceTable
  -> board / pulse_distributor / link_switcher / instrument_controller
     / gateway / model_vna / model_turntable / model_dut / test_agent
```

在不冲突的情况下，同一个 A0-A7 通用插槽可以同时载入多个逻辑实例。例如一个插槽可以同时承载 `board` + `gateway`，或 `model_vna` + `model_turntable`，也可以在 IO 与时序资源允许时同时承载 `pulse_distributor` + `link_switcher`。是否允许并存由 `DistributedDeploymentGate` 判定，至少检查资源、IO、时序、owner、slot writer、事件连接和数据连接是否冲突。

因此，`NodeSlot[8]` 只描述八个通用插槽的新鲜度、心跳、装载摘要和故障摘要；具体插槽承载真实板卡、脉冲分发、链路切换、仪表控制、网关、模型网分或模拟转台，由静态分布式应用模型决定。

### 虚拟反射内存参考机制

RefMem Domain 的外部参考只聚焦“共同事实”和“受控远端内存语义”，不承担 VDC/DPLL 的共同时间算法。参考项目提供机制，不改变 HAOFV 的静态 owner、snapshot 和 RUN gate 约束。

| 参考对象 | 可借鉴机制 | RefMem 落地方式 | 不采用内容 |
|---|---|---|---|
| NASA cFS Table Services | table registry、active/inactive image、load/dump、data integrity、owner validation、application get/release access。 | 建立 `RefMemTableRegistry`、staging/active 双镜像、CRC + owner validation、dump/load 镜像、validator id、validation pending/result。 | 不引入 cFS software bus、flight app 生命周期、ground command 格式。 |
| OpenSHMEM / MPI RMA | remote memory window、put/get/accumulate、atomic memory operation、origin/target completion、fence/quiet、lock/unlock。 | 建立受控 `RefMemRmaWindow`，只允许 slot delta、command flag、dirty bitmap、heartbeat/seq、quality counter 等小字段同步；定义 `origin_encoded -> target_received -> target_validated -> target_committed`。 | 不暴露裸地址，不允许任意远端写变量，不做通用 PGAS。 |
| IEC 61499 / Eclipse 4diac | FB instance、event connection、data connection、device/resource mapping、deployment consistency。 | 建立静态 `DistributedApplicationMap`、`DistributedFbInstanceTable`、`DistributedEventLinkTable`、`DistributedDataLinkTable` 和 DeploymentGate linter。 | 不做动态 FB 部署，不引入 IEC 工具链，不执行跨节点 FB 直接调用。 |

#### RefMemTableRegistry

`RefMemTableRegistry` 是 cFS-style 表机制在本项目中的落地点。每张表必须能被列出、校验、激活和导出。

| 字段 | 含义 |
|---|---|
| `table_id` | 表编号，例如 Vector layout、ApplicationMap、FB instance、EventLink、DataLink、VDC snapshot map。 |
| `owner_domain` | 表 owner，只有 owner 可执行逻辑 validation。 |
| `offset/size` | 在 64 KB 表或外部 package 中的位置。 |
| `layout_version` | 表结构版本。 |
| `active_crc32` | 当前 active image CRC。 |
| `staging_crc32` | 当前 staging image CRC。 |
| `validation_state` | `EMPTY/STAGED/CRC_OK/OWNER_OK/FAILED/ACTIVE/ROLLBACKABLE`。 |
| `validator_id` | owner validation 函数或策略编号。 |
| `last_result` | 最近验证/激活/回滚结果。 |
| `last_evidence_index` | 失败证据索引。 |

表生命周期：

```text
LOAD_TO_STAGING
  -> CRC_CHECK
  -> OWNER_VALIDATE
  -> ACTIVATE
  -> ACTIVE
  -> ROLLBACKABLE / FAILED
```

规则：

- CRC 只证明字节完整性，不能替代 owner validation。
- load 只能进入 staging，不得直接覆盖 active。
- dump 只能导出稳定 snapshot 或 active image，不导出半更新数据。
- owner 使用表指针后必须 release，避免阻塞 staging/active 切换。
- validation 失败必须给出 reason 和 evidence，而不是只返回 false。

#### RefMemRmaWindow

`RefMemRmaWindow` 是 OpenSHMEM / MPI RMA 思想在本项目中的受控子集。它不是远端裸内存访问，而是固定 slot mirror 的同步窗口。

| 概念 | RefMem 子集 |
|---|---|
| RMA window | A0-A7 每个节点暴露自己的 slot mirror 和少量共享 command/quality 字段。 |
| put/get | 表达为 `REFMEM_DELTA` 和 snapshot read，不提供任意地址 put/get。 |
| accumulate | 只允许白名单计数器，例如 stale/drop/late/CRC counter。 |
| atomic | 只允许 command flag、ack/nack bit、dirty bitmap、heartbeat seq 等小字段。 |
| fence | 同一 target 的 delta 顺序可见性门禁。 |
| quiet / flush | 多 target 或 RUN gate 前的全部 delta 完成门禁。 |
| lock/unlock | 映射为 command slot take/clear 或 staging activation critical section。 |

completion 语义：

```text
origin_encoded
  -> ring_sent
  -> target_received
  -> target_crc_ok
  -> target_owner_validated
  -> target_committed
  -> visible_in_snapshot
```

RUN gate 只能消费 `target_committed` 后的 snapshot。任何处于 encoded/sent/received 但未 committed 的 delta 都不能改变 active fact。

| 借鉴点 | RefMem Domain 落地形式 | 不采用的部分 |
|---|---|---|
| Application model | 静态 `DistributedApplicationMap`，描述应用/profile 元数据、目标插槽集合和加载实例的 CRC bundle。 | 运行时动态部署 application。 |
| FB instance model | 静态 `DistributedFbInstanceTable`，描述每个节点上的 AO/FB 实例、版本、role、enable 条件和共存冲突规则。 | 跨节点动态创建/销毁 FB。 |
| Event connection | 静态 `DistributedEventLinkTable`，把 START、STOP、FIRE_LOAD、DONE、FAULT、ACK/NACK 映射为 command slot、event queue 或 RJ45 frame。 | 跨节点直接事件调用和动态路由。 |
| Data connection | 静态 `DistributedDataLinkTable`，把状态、参数、质量、时间戳、T2 和统计量映射到固定 slot 字段。 | 任意远程变量读写。 |
| Deployment consistency | `DistributedDeploymentGate` 聚合 build id、hw profile、config CRC、calibration CRC、sync profile CRC、layout version 和实例共存冲突检查。 | 在线热替换部署。 |
| Diagnostics | `DistributedConnectionQualityTable` 记录 seq、CRC、stale、late、drop、timeout、last_error 和 evidence index。 | 依赖外部 IEC 工具链诊断。 |

### DistributedApplicationMap

`DistributedApplicationMap` 描述一套静态分布式应用的 profile、版本、目标插槽集合和表布局依赖。它是产品配置的一部分，不是运行时热部署脚本，也不是 A0-A7 节点目录。A0-A7 通用插槽由 GenericNodeTable 描述；应用实例装载由 `DistributedNodeLoadTable` 描述。

| 字段 | 含义 | 约束 |
|---|---|---|
| `application_id` | 分布式应用编号。 | 由上位机配置或 System Pack 生成。 |
| `application_version` | 应用模型版本。 | RUN 前必须和各节点 active config 一致。 |
| `profile_id` | 应用 profile 编号。 | 区分现场产品 profile、调试 profile、仿真 profile。 |
| `layout_version` | RefMem 表布局版本。 | 必须匹配 `DistributedVectorTable` header。 |
| `target_node_mask` | 本 profile 使用的 A0-A7 通用插槽集合。 | 只允许 0-7 位；字段名沿用 node 是为了兼容同步协议。 |
| `node_table_crc` | 通用插槽基座表摘要。 | RUN 前必须和 active image 一致。 |
| `node_load_crc` | 实例加载表摘要。 | RUN 前必须和 active image 一致。 |
| `fb_instance_crc` | AO/FB 实例定义表摘要。 | RUN 前必须和 active image 一致。 |
| `event_link_crc` | 事件连接表摘要。 | RUN 前必须和 active image 一致。 |
| `data_link_crc` | 数据连接表摘要。 | RUN 前必须和 active image 一致。 |

规则：

- `DistributedApplicationMap` 只定义应用 profile 的元数据和 CRC bundle，不直接描述每个节点上的实例范围。
- 不允许在 ApplicationMap 的 node 项中使用 `instance_first/count` 将节点绑定到连续实例区间。
- 同一个应用可以有多个 profile，例如现场四板 profile、B4 调试仿真 profile、HIL profile。
- RUN gate 以 `ApplicationMap + GenericNodeTable + NodeLoadTable + FbInstanceTable` 的组合为部署事实。

### DistributedGenericNodeTable

`DistributedGenericNodeTable` 描述 A0-A7 八个通用插槽基座。它回答“有哪些通用 slot substrate 可参与当前系统”，不回答“这些插槽加载了哪些业务实例”。

| 字段 | 含义 | 约束 |
|---|---|---|
| `node_id` | A0-A7 通用插槽号。 | 只允许 0-7；字段名沿用 node_id 是为了匹配同步协议和 NodeSlot[8]。 |
| `node_uuid` | 节点硬件身份。 | 用于防止 A0-A7 逻辑号错绑实体板。 |
| `capability_mask` | 节点硬件/基础能力上限。 | 每个可参与系统的物理节点必须具备 board、RefMem、VDC 基础能力；其余可选能力包括 Flash、SD、USB、PIO、DMA、RJ45、core1_rt、SMA、link_control、BISS-C、UART/RS485。 |
| `claim_policy` | 逻辑插槽 claim 策略。 | `STRICT_UUID`、`ALLOW_SAME_BOARD_MULTI_SLOT`、`SPARE_DYNAMIC`、`DISABLED`。 |
| `claim_priority` | 冲突仲裁优先级。 | 只用于诊断和预案选择；不得静默抢占已 active 的必需插槽。 |
| `default_persona_mask` | 节点默认人格能力。 | 只作为装载约束输入，不等于 active role。 |
| `hw_profile_crc` | 硬件约束摘要。 | 和当前板级约束、IO 能力一致。 |
| `online_required` | 节点是否为当前 profile 必需。 | 必需节点 stale 或 missing 时禁止 RUN。 |
| `fail_policy` | 节点失效策略。 | `STOP`、`HOLDOVER`、`DEGRADE`、`REPORT_ONLY`。 |

规则：

- A0-A7 是唯一固定插槽空间。
- GenericNode 只描述通用插槽基座、硬件身份和基础能力，不直接声明业务 role/persona 实例。
- GenericNode 的能力不能从当前装载实例反推；它必须来自 board profile、硬件约束或 System Pack 中的硬件 profile。
- `REFMEM + VDC` 是每个物理节点参与分布式系统的最小基础能力。没有 RefMem 能力的板卡不能发布/接收共同事实；没有 VDC 能力的板卡不能参与虚拟 DC 时间语义，因此不能进入 distributed RUN。
- `VDC` 基础能力表示节点能接收、校验、使用并发布虚拟 DC 相关 snapshot；`VDC_DPLL` 类 IP 核表示某个实例负责运行 DPLL/虚拟环路 owner，两者不能混为一谈。
- `capability_mask` 是装载上限，不是当前业务角色。`gateway`、`model_vna`、`model_turntable`、`pulse_distributor` 等 role 只允许出现在 NodeLoadTable。
- 脉冲分发、链路切换、仪表控制、gateway、model_vna、model_turntable、test_agent 等都由 `DistributedNodeLoadTable` 装载，不扩展固定节点数量。

### BoardCapabilityTable

`BoardCapabilityTable` 描述当前 profile 中可见物理板、模型板或调试节点的基础能力。它回答“B0-B4/Bx 这块物理或模型节点能做什么”，不回答“它当前占用哪个 A0-A7 slot”，也不直接表达业务装载关系。

| 字段 | 含义 | 约束 |
|---|---|---|
| `board_id` | B0-Bx 物理/实例标签。 | 只作为 profile 内部标签，不能当作 RefMem slot id。 |
| `board_uuid` | 物理板或模型节点身份。 | 用于 claim 和错绑诊断。 |
| `capability_mask` | 板级基础能力上限。 | 必须包含 `BOARD + REFMEM + VDC`；可选 PIO、DMA、USB、SD、core1_rt、link_control、BISS-C 等。 |
| `io_constraint_mask` | 板级 IO 约束。 | 表示该板可承载哪些 IO 类资源，例如 SMA、RJ45_SYNC、LINK_CONTROL、BISS-C、UART/RS485。 |
| `ip_core_mask` | 类 IP 核能力。 | 表示该板固件/PIO/实时资源能承载哪些类 IP 核，例如 pulse capture/fire、link sequence、BISS-C codec。 |
| `default_persona_mask` | 默认人格能力。 | 只作为默认 profile 提示，active role 仍由 NodeLoadTable 决定。 |
| `hw_profile_crc` | 硬件约束摘要。 | 来自硬件约束/System Pack。 |
| `active_default_slot` | 默认 profile 建议 slot。 | 仅用于默认映射，例如 B2 默认加载到 A2；后续可经 SlotClaim 协调迁移。 |
| `online_required` | 当前 profile 是否必需。 | 必需 board 缺失时拒绝 RUN 或进入 fault。 |

首版代码中 `BoardCapabilityTable` 已作为应用模型正式表参与 package CRC、TableRegistry 和 linter，并可通过 `SYSTem:REFMEM:BOARD?` 查询 active snapshot，也可通过 `SYSTem:REFMEM:TABle? 1` 查询表级 CRC、staging 和 validation 状态。System Pack 应优先加载 BoardCapabilityTable，再由 SlotClaimMap 把 B 实例映射到 A0-A7 slot：

```text
BoardCapabilityTable(B0-Bx)
  -> SlotClaimProposal(board_id, preferred_slot_id, capability, io, ip_core)
  -> SlotClaimMap(resolved active A0-A7 assignment)
  -> DistributedNodeLoadTable(slot_id -> AO/FB instance)
  -> DeploymentGate(resource/io/ip/event/data/quality check)
```

拆分后的规则：

- B0-Bx 的能力来自物理板、最小系统约束、产品硬件约束或 System Pack。
- A0-A7 的 GenericNode 只表达 slot substrate、claim policy 和 active assignment 结果，不应永久代表某个硬件功能。
- `B2.LinkSwitcherAO` 默认可以装载到 A2，但只要 capability、IO 约束、类 IP 核、事件和数据连接通过，也可以装载到其他 A0-A7 slot。
- 没有 `REFMEM + VDC` baseline 的物理节点不得进入 distributed RUN；缺少 `VDC_DPLL` 只影响 DPLL owner 候选，不影响普通 VDC 参与节点。
- 板卡能力必须支持通过 SD System Pack 和受控 SCPI staging 加载。固件内置表只作为 default/factory profile；SD/SCPI 加载的 BoardCapabilityTable 只能进入 staging image，必须经过 CRC、版本兼容、owner validation、IO 约束检查、类 IP 核能力检查和 DeploymentGate 后才能激活。
- SCPI 不得直接修改 active BoardCapabilityTable，也不得直接改 GenericNode 的 active slot 事实；SCPI 只能通过 `SYSTem:REFMEM:LOAD:BOARD` 提交 board capability load 意图，通过 `SYSTem:REFMEM:LOAD:BOARD:STATus?` 读取 staging 结果，通过 `SYSTem:REFMEM:BOARD?` 读取 active snapshot。

BoardCapabilityTable 加载路径：

```text
SD System Pack / SYSTem:REFMEM:LOAD:BOARD
  -> BoardCapabilityTable staging image
  -> CRC + schema/version check
  -> owner validation
  -> IO constraint + ip_core + baseline check
  -> SlotClaim proposal/regenerate
  -> DeploymentGate
  -> active profile activation
```

#### 全局逻辑插槽 Claim

在一个 RefMem active profile / epoch 中，A0-A7 是全环唯一逻辑地址。物理板、当前项目实例或调试模型使用 B0-B4 这类 board/profile label 表达，避免和 A0-A7 slot 混淆。允许一块物理板同时 claim 多个不同逻辑插槽，例如调试阶段 B4 同时 claim A5 作为模型网分、A6 作为模拟转台；但同一个 active `slot_id` 不能同时被两块物理板提交为 active owner。由于插槽是通用基座，重复 claim 不必立即成为不可恢复错误：只要还有可用插槽且 capability、resource、IO、owner 和 NodeLoadTable 约束满足，`DistributedRefMemAO` 可以通过自组网协调把其中一个候选迁移到其他空闲插槽。真正的失败条件是插槽已满、节点实例化溢出、硬绑定 required slot 不匹配，或资源/IO/owner 冲突无法解。

主路径是预规划：

```text
System Pack / active profile
  -> GenericNodeTable: slot_id -> expected node_uuid / hw_profile / capability
  -> NodeLoadTable: slot_id -> instance_id list
  -> board local profile: physical_board_uuid -> claimed slot list
```

启动或 profile 激活时，每块板发布 `SlotClaimProposal` 摘要。这里必须区分“候选节点实例”和“已分配 active slot”：候选最多 16 个，可以超过 A0-A7 的容量，用于自组网协调、反向验证和溢出检测；active slot 仍最多只有 8 个。

```text
physical_board_uuid
claim_candidate_count
claim_candidate[]
claim_seq
claim_epoch
claim_crc32
```

每个 `claim_candidate` 至少包含：

```text
candidate_id
preferred_slot_id
role_mask
persona_mask
capability_mask
resource_claim
io_claim
ip_core_claim
claim_policy
claim_priority
```

例如反向验证时，一块板可以一次实例化并上报 9 到 16 个候选节点实例。`DistributedRefMemAO` 必须尝试把候选映射到 A0-A7 八个通用插槽；前 8 个只有在 capability、resource、IO、类 IP 核、owner、event/data link 都满足时才能变成 active assignment，第 9 到第 16 个未分配候选必须被标记为 `OVERFLOW`，并进入 DeploymentGate evidence。这个测试用于证明系统不会因为单板多实例而生成第 9 个隐式插槽，也不会覆盖已有 active slot。超过 16 个候选是 proposal 格式错误，应直接 `NACK_BAD_CLAIM_PROPOSAL` 或等价 reason。

`DistributedRefMemAO` 聚合所有可见 `SlotClaim`，形成 `SlotClaimMap` 诊断视图：

| 字段 | 含义 |
|---|---|
| `slot_id` | A0-A7 逻辑插槽。 |
| `owner_board_uuid` | 当前 claim 该 slot 的物理板。 |
| `claim_count` | 当前 epoch 中 claim 该 slot 的物理板数量。 |
| `claim_state` | `UNCLAIMED/CLAIMED/CLAIM_CONFLICT/RESOLVING/STALE/MISMATCH/OVERFLOW/DISABLED`。 |
| `expected_node_uuid` | GenericNodeTable 中预期硬件身份。 |
| `actual_node_uuid` | 当前 claim 的硬件身份。 |
| `loaded_instance_mask` | NodeLoadTable 中加载到该 slot 的实例摘要。 |
| `last_claim_seq` | 最近 claim 序号。 |
| `evidence_index` | 冲突或错绑证据。 |

`SlotClaimMap` 只记录 resolved active assignment；未分配候选进入 `SlotClaimEvidence` 或质量表，不得伪装成 `NodeSlot[8]` 之外的新 active slot。首版代码已落地 `refmem_slot_claim.h/.c`，从当前 active default profile 派生本地 `SlotClaimMap`，并可通过 `SYSTem:REFMEM:CLAIM? [slot_id]` 查询 map 和指定 slot assignment。当前尚未接 RJ45 `CLAIM_*` 协调消息，`claim_epoch=1` 表示本地派生 epoch。

#### SlotClaim 自组网协调

纠错不只是报错，也需要支持板卡自组网协调。协调机制仍必须受静态模型和 HAOFV owner 约束：节点可以提出候选 claim 和释放意图，但最终 active claim 只能由 `DistributedRefMemAO` 在同一 `claim_epoch` 下提交。通用插槽优先尝试协调分配；只有没有满足约束的空闲插槽、实例装载超过 profile 上限、或硬绑定 required slot 不匹配时，才进入 `OVERFLOW` 或 `CLAIM_FAULT`。

协调状态机：

```text
DISCOVER
  -> CLAIM_PROPOSE
  -> CLAIM_COLLECT
  -> CONFLICT_DETECTED
  -> RESOLVE_PLAN
  -> RESOLVE_COMMIT
  -> CLAIM_ACTIVE
  -> CLAIM_STALE / CLAIM_FAULT
```

协调消息建议：

| 消息 | 方向 | 含义 |
|---|---|---|
| `CLAIM_HELLO` | board -> ring | 发布 physical board uuid、基础能力和当前 active slot assignment 摘要。 |
| `CLAIM_PROPOSE` | board -> RefMemAO/ring | 提出候选节点实例列表、preferred slot、capability、resource/IO claim 和 claim CRC。 |
| `CLAIM_CONFLICT` | RefMemAO -> ring | 广播重复 claim、uuid mismatch、hw profile mismatch、stale 或 slot overflow。 |
| `CLAIM_RELEASE` | board -> ring | 声明释放某 slot claim，带 seq/epoch 防止旧释放误伤。 |
| `CLAIM_RESOLVE` | RefMemAO -> ring | 发布协调结果，包括 active owner、disabled claim 和 evidence。 |
| `CLAIM_COMMIT` | RefMemAO -> all | 进入新 `claim_epoch`，同步 active SlotClaimMap CRC。 |

协调规则：

- 同一 `physical_board_uuid` 可以 claim 多个不同 `slot_id`，前提是每个 slot 的 `claim_policy`、`capability_mask`、resource/IO claim 和 NodeLoadTable 共存检查都通过。
- 同一 `slot_id` 若被多个不同 `physical_board_uuid` claim，状态先进入 `CLAIM_CONFLICT`，随后进入 `RESOLVE_PLAN`。若存在满足 capability/resource/IO/owner 约束的空闲通用插槽，可以生成迁移计划；若无可用插槽或实例数量超过 `NodeLoadTable`/profile 上限，进入 `OVERFLOW/CLAIM_FAULT`。
- 同一 `physical_board_uuid` 最多可以提出 16 个候选节点实例，但 `DistributedRefMemAO` 在同一 active profile / epoch 中最多只能提交 8 个 active slot assignment。第 9 到第 16 个未分配候选只能作为 `OVERFLOW` evidence，不得生成隐式 slot；超过 16 个候选必须作为格式错误拒绝。
- `STRICT_UUID` slot 必须匹配 GenericNodeTable 的 `node_uuid` 和 `hw_profile_crc`；不匹配进入 `MISMATCH`。
- `SPARE_DYNAMIC` 只允许用于 A5-A7 这类非 required spare slot，并且必须在 ConfigGate 激活后形成新的 active CRC bundle，不能在 RUN 中热抢占。
- `claim_priority` 用于在多个候选物理板或多个可用插槽之间选择协调计划；在 required hard binding 不冲突时可以自动协调通用插槽分配，但不得静默覆盖 `STRICT_UUID` 的硬绑定 required slot。
- B0-B4 只表示当前项目或默认 profile 中的物理/实例标签，不是 slot id。`B2.LinkSwitcherAO` 可在默认 profile 中加载到 slot A2，也可以在后续 profile 或自组网协调中加载到任何满足 capability、IO 约束和类 IP 核要求的 A0-A7 slot。
- 对 `ALLOW_SAME_BOARD_MULTI_SLOT`、`SPARE_DYNAMIC` 或 `REPORT_ONLY` slot，`DistributedRefMemAO` 可根据 capability、claim_priority、physical uuid、link quality、load_order 和空闲插槽集合生成自动协调结果，但必须提升 `claim_epoch`、更新 `SlotClaimMap CRC`，并广播 `CLAIM_COMMIT`。
- 冲突恢复可以通过重新加载 System Pack / profile、清除错误板卡 claim、执行受权限保护的维护命令，或对非 required dynamic slot 执行自组网协调完成；不得由节点本地自行改 `node_id` 并直接进入 active。
- `DeploymentGate.node_check` 必须检查 `SlotClaimMap`：required 实例必须有一个 resolved active slot，spare slot 可 `UNCLAIMED`，任何未解决的 `CLAIM_CONFLICT/MISMATCH/STALE/OVERFLOW/CLAIM_FAULT` required 实例都拒绝 RUN。首版代码已把本地派生 claim gate 接入 RefMem model validation 和 `system_manager` config RUN gate；RJ45 协调后的跨板 claim gate 后续接入。
- RJ45_SYNC_RING 同步 `SlotClaim` 摘要时，必须带 `claim_epoch` 和 CRC，旧 epoch claim 不得覆盖 active claim。

### DistributedNodeLoadTable

`DistributedNodeLoadTable` 是应用 profile 到通用插槽的实例装载表。它回答“哪个实例被加载到哪个 A0-A7 通用插槽上”，支持同一插槽同时加载多个不冲突实例。

| 字段 | 含义 | 约束 |
|---|---|---|
| `load_id` | 装载记录编号。 | 在当前表内唯一。 |
| `application_id` | 所属应用编号。 | 必须匹配 active ApplicationMap。 |
| `profile_id` | 所属 profile 编号。 | 必须匹配 active ApplicationMap。 |
| `node_id` | 目标 A0-A7 通用插槽。 | 必须存在于 GenericNodeTable。 |
| `instance_id` | 被装载的 AO/FB 实例。 | 必须存在于 DistributedFbInstanceTable。 |
| `role_mask` | 本次装载赋予的角色集合。 | 例如 board、pulse_distributor、gateway、model_vna、model_turntable。 |
| `persona_mask` | 本次装载启用的人格集合。 | 一个节点可由多条 load 记录组合出多 persona。 |
| `enabled` | 是否启用本条装载。 | 禁用时不参与 RUN，但保留诊断和报告。 |
| `required` | 是否为当前 profile 必需。 | 必需实例缺失或 stale 时拒绝 RUN。 |
| `fail_policy` | 本实例失效策略。 | `STOP`、`HOLDOVER`、`DEGRADE`、`REPORT_ONLY`。 |
| `load_order` | 同节点多实例初始化顺序。 | 用于 gateway + instrument 或 model_vna + model_turntable 等组合。 |

规则：

- 同一 `node_id` 插槽可以出现多条 enabled load 记录。
- 同一 `instance_id` 在同一 active profile 中首版只允许一条 enabled load 记录；后续若支持 replicated instance，必须显式增加 instance replica id。
- 每条 enabled load 必须通过 capability gate：实例的 resource/IO claim 必须被目标 GenericNode 的 `capability_mask` 覆盖。
- 每条 enabled load 必须同时通过 realtime capability gate：实例的 `ip_core_claim` 必须能映射到目标 GenericNode 的基础能力和 IO 约束，例如链路序列控制需要 PIO、DMA、core1_rt、link_control；BISS-C 编解码需要 PIO、DMA、core1_rt、BISS-C IO。
- 一块板同时模拟转台和网分时，应表现为同一通用插槽上两条 load 记录，例如 `B4.ModelVnaAO` 和 `B4.ModelTurntableAO` 被加载到某个可用 A0-A7 slot。
- 一个插槽同时装载多个实例时，必须通过 `DistributedDeploymentGate` 检查资源、IO、时序、owner、slot writer、事件连接和数据连接冲突。
- 逻辑实例可以禁用，但禁用实例仍应保留版本、原因和最后一次健康状态，便于报告闭环。

#### RealtimeCapabilityContract

`RealtimeCapabilityContract` 是从 `DistributedNodeLoadTable + DistributedFbInstanceTable + EventLink + DataLink` 派生出的装载契约。它不代表 RefMem 执行硬实时动作，而是说明某个实例被加载后，系统必须同步验证和发布哪些实时能力事实。

| 字段 | 含义 | 约束 |
|---|---|---|
| `realtime_owner` | 实时执行 owner。 | `CORE0`、`CORE1` 或 `SHARED`；硬实时边沿、脉冲捕获、PIO 编解码默认要求 `CORE1` 或等价实时资源。 |
| `resource_claim` | 执行资源。 | PIO、DMA、IRQ、timer、core1 时间片、RJ45 等；由 FB instance 声明，由 DeploymentGate 校验。 |
| `io_claim` | 外设与引脚约束。 | SMA/RJ45/link-control/BISS-C/UART-RS485 等；必须被硬件约束和 GenericNode capability 覆盖。 |
| `ip_core_claim` | 类 IP 核能力。 | PIO 程序、DMA 搬运、IRQ 捕获、core1 service 预算等组合能力，不等同于普通 GPIO。 |
| `event_links` | 事件入口/出口。 | `FIRE_LOAD`、`START`、`STOP`、`DONE`、`FAULT`、`ACK/NACK` 必须有静态连接或明确不需要。 |
| `data_links` | 事实入口/出口。 | timestamp、sequence state、link output state、quality、late/drop/error counter 必须有唯一 writer 和稳定 snapshot。 |
| `time_budget_us` | 实时服务预算。 | 超预算必须进入 Diagnostics evidence，并可触发 DeploymentGate 降级或拒绝 RUN。 |

首版类 IP 核映射：

| `ip_core_claim` | 含义 | 基础能力要求 |
|---|---|---|
| `PULSE_CAPTURE` | 捕获输入脉冲并生成时间戳。 | PIO + DMA + core1_rt，外加对应 SMA/RJ45 输入 IO。 |
| `PULSE_FIRE` | 在指定 tick 输出触发边沿。 | PIO + DMA + core1_rt，外加对应 SMA/RJ45 输出 IO。 |
| `LINK_SEQUENCE` | 按序列切换链路控制节点。 | PIO + DMA + core1_rt + link_control；当前项目实例可映射为 SP8T/SP2T。 |
| `BISS_C_CODEC` | BISS-C 编码/解码。 | PIO + DMA + core1_rt + BISS-C IO。 |
| `RJ45_SYNC_DELTA` | 同步环路上的小 delta/ACK/NACK 传递。 | RJ45_SYNC IO 和 RJ45 基础能力。 |
| `VDC_DPLL` | 虚拟 DC / DPLL 运行 owner。 | VDC 基础能力 + core1_rt 或等价受控实时预算，具体算法由 VDC Domain owner 执行。 |

链路控制节点的装载语义：

```text
NodeLoad(B2.LinkSwitcherAO -> slot A2 in default profile)
  -> role/persona: link_switcher
  -> resource_claim: CORE1_RT + PIO + DMA
  -> io_claim: LINK_CONTROL + RJ45_SYNC
  -> ip_core_claim: PULSE_CAPTURE + LINK_SEQUENCE
  -> event_links: FIRE_LOAD / DONE / FAULT / ACK/NACK
  -> data_links: pulse timestamp / link sequence state / link output state / quality
```

BISS-C 节点的装载语义：

```text
NodeLoad(ModelTurntableAO or encoder node)
  -> role/persona: model_turntable or encoder
  -> resource_claim: CORE1_RT + PIO + DMA
  -> io_claim: BISS_C
  -> ip_core_claim: BISS_C_CODEC
  -> data_links: encoder position / capture timestamp / quality
```

因此，RefMem load 的目标不是“把某个 node 名称写进去”，而是把可验证的实时能力事实装入 staging image。只有 capability、IO 约束、类 IP 核、事件/数据连接、owner 和质量门禁都通过后，active profile 才能允许 RUN。

### DistributedFbInstanceTable

`DistributedFbInstanceTable` 描述可被加载的 AO/FB 实例定义。这里的 FB 是 HAOFV 的本地功能块，不是跨节点动态调用对象；实例实际加载到哪个 A0-A7 通用插槽由 `DistributedNodeLoadTable` 决定。

| 字段 | 含义 | 约束 |
|---|---|---|
| `instance_id` | 全局实例编号。 | 在当前 `application_id` 内唯一。 |
| `default_node_id` | 建议或默认装载节点。 | 只作为默认 profile 的提示；active 节点以 NodeLoadTable 为准。 |
| `domain` | 所属主域。 | `SYSTEM`、`TRIG`、`CAL`、`SYNC`、`MEAS`、`REFMEM` 等。 |
| `ao_type/fb_type` | AO/FB 类型。 | 用于版本兼容和配置校验。 |
| `instance_name` | 实例名。 | 例如 `B0.TriggerAO`、`B1.PulseDistributorAO`、`B2.LinkSwitcherAO`、`B3.InstrumentControllerAO`；B 前缀是物理/实例标签，不是 RefMem slot。 |
| `version` | 实例实现版本。 | RUN 前检查兼容范围。 |
| `enable_condition` | 启用条件。 | 由 mode、persona、feature、配置 CRC 共同决定。 |
| `resource_claim` | 资源占用。 | Flash、SD、USB、PIO、DMA、LCD、RJ45、core1 时间片等。 |
| `io_claim` | IO 占用。 | SMA、RJ45、link-control resources、BiSS-C、UART/RS485 等；当前项目实例可映射为 SP8T/SP2T。 |
| `ip_core_claim` | 类 IP 核能力占用。 | 例如 PIO pulse capture/fire、link sequence、BISS-C codec、RJ45 sync delta、VDC/DPLL。 |
| `time_budget_us` | 单次 service 预算。 | 超预算进入 Diagnostics evidence。 |
| `state_slot_ref` | 状态事实位置。 | 指向 Vector slot 字段。 |
| `health_slot_ref` | 健康事实位置。 | 指向质量或故障 slot 字段。 |
| `event_in/out_range` | 事件连接范围。 | 指向 `DistributedEventLinkTable`。 |
| `data_in/out_range` | 数据连接范围。 | 指向 `DistributedDataLinkTable`。 |
| `conflict_class` | 共存冲突分类。 | 同类互斥或资源互斥时拒绝同时启用。 |
| `restart_policy` | 故障恢复策略。 | `NO_RESTART`、`LOCAL_RESTART`、`SYSTEM_FAULT`。 |

### DistributedEventLinkTable

`DistributedEventLinkTable` 把跨 AO/FB 的事件意图静态化。它只描述事件路径，不直接携带大 payload。

| 字段 | 含义 | 约束 |
|---|---|---|
| `event_link_id` | 事件连接编号。 | 全局唯一。 |
| `source_instance` | 源实例。 | 可为 SCPI/SystemAO/LoopEngineAO/TriggerAO 等。 |
| `source_event` | 源事件。 | `START`、`STOP`、`ARM`、`FIRE_LOAD`、`DONE`、`FAULT`、`ACK`、`NACK`。 |
| `target_node_mask` | 目标 slot/node_id 集合。 | 支持单播、多播或广播到 A0-A7；字段名沿用 node 是协议兼容名。 |
| `target_instance` | 目标实例。 | 多播时可用类型匹配。 |
| `target_event` | 目标事件。 | 目标 AO/FB 接收的事件名。 |
| `transport` | 传递方式。 | `LOCAL_QUEUE`、`CORE_IPC`、`COMMAND_SLOT`、`RJ45_SYNC_RING`。 |
| `timeout_us` | ACK 或完成超时。 | 为 0 表示无需 ACK。 |
| `ack_policy` | 应答策略。 | `NONE`、`ANY`、`ALL_REQUIRED`、`BITMAP`。 |
| `retry_policy` | 重试策略。 | 次数、退避、重复 sequence 处理。 |
| `safety_class` | 安全等级。 | 影响 timeout 后进入 busy、degrade 还是 fault。 |
| `evidence_ref` | 证据槽。 | timeout、NACK、late 需要可回溯。 |

首版必须覆盖的事件路径：

- 上位机配置提交到 ConfigGate。
- `START/STOP` 到 LoopEngineAO 与各节点 TriggerAO。
- `FIRE_LOAD` 从 LoopEngineAO 到 core1 realtime owner。
- T2/DONE/FAULT 从实时侧回到 LoopEngineAO、MEASure 和 Diagnostics。
- 分布式 ACK/NACK 从各节点回到命令 owner。

### DistributedDataLinkTable

`DistributedDataLinkTable` 把事实字段静态化，避免把 RefMem 当作随意读写的全局变量。

| 字段 | 含义 | 约束 |
|---|---|---|
| `data_link_id` | 数据连接编号。 | 全局唯一。 |
| `slot_path` | Vector 字段路径。 | 例如 `LoopSlot.active_sequence_crc`。 |
| `writer_instance` | 唯一写入者。 | 不允许多 writer。 |
| `reader_mask` | 读取者集合。 | SCPI/UI/Report 也必须声明为 reader。 |
| `type` | 数据类型。 | `u8/u16/u32/i32/fixed/ns/tick/enum/bitmask/crc`。 |
| `unit` | 单位。 | `ns`、`us`、`tick`、`Hz`、`count`、`none`。 |
| `scale` | 定点比例。 | 例如 ns、0.1 ns、ppm、Q16.16。 |
| `min/max` | 值域。 | 配置和运行时都按此校验。 |
| `lifecycle` | 生命周期。 | `STAGING`、`ACTIVE`、`RUN`、`TRANSIENT`、`EVIDENCE`。 |
| `snapshot_policy` | 快照策略。 | `DIRECT_ATOMIC`、`SEQLOCK`、`DOUBLE_BUFFER`。 |
| `update_period_us` | 期望更新周期。 | 用于 stale 判定。 |
| `stale_window_us` | stale 窗口。 | 超窗后 READ 返回 stale 标志。 |
| `crc_scope` | CRC 范围。 | 字段、slot、directory 或 package。 |
| `permission` | 访问权限。 | `READ_ONLY`、`COMMAND_WRITE`、`CONFIG_STAGE_WRITE`。 |

### 通用 RefMem 基础件模型

RefMem 要形成可复用基础件，不能把字段能力拆成一张孤立的新业务表。更稳妥的方式是把现有静态模型表、Vector layout 和运行质量表组合成一个统一 `RefMemSlotContract` 视图：

```text
DistributedApplicationMap
  + DistributedGenericNodeTable
  + DistributedNodeLoadTable
  + DistributedFbInstanceTable
  + DistributedEventLinkTable
  + DistributedDataLinkTable
  + DistributedDeploymentGate
  + DistributedConnectionQualityTable
  + Header/Directory/SlotGuard
  -> DistributedRefMemAO / RefMemSlotContract
```

其中：

- `GenericNodeTable` 和 `NodeLoadTable` 回答“实例装载到哪个 A0-A7 通用插槽”。
- `FbInstanceTable`、`EventLinkTable` 和 `DataLinkTable` 回答“实例之间有哪些事件和事实连接”。
- `Header/Directory/SlotGuard` 回答“事实字段在 64 KB 表中的物理位置、发布序号、CRC 和 stale 状态”。
- `DeploymentGate` 和 `ConnectionQualityTable` 回答“当前组合是否允许 RUN、连接是否可信、失败证据在哪里”。
- `DistributedRefMemAO` 是 RefMem 基础件的运行 owner；`RefMemSlotContract` 是它内部使用的字段级契约视图，为各 AO/FB owner 的事实提交、快照、delta、owner validation 和 subscription 提供校验依据。

这样链路控制节点、DPLL 节点、Trigger 状态节点、UI 事件节点都不需要自带一套反射内存规则。它们只需要声明 AO/FB instance、事件连接、数据连接和字段契约；对应 AO/FB owner 通过自己的 owner API 向 `DistributedRefMemAO` 提交事实意图或读取 snapshot，`DistributedRefMemAO` 按 `RefMemSlotContract` 完成校验、发布和订阅分发。

#### 字段级 RefMemSlotContract

`DistributedDataLinkTable` 描述“谁写、谁读、字段是什么”。`RefMemSlotContract` 进一步把 DataLink、Directory 和 Guard 合成为字段级能力视图，描述“这个字段被 `DistributedRefMemAO` 接收、校验、发布、过期和订阅分发时必须满足什么条件”。它不是 A0-A7 的通用插槽，也不是 `NodeLoadTable` 的装载插槽，而是 `DistributedVectorTable` 内每一个可发布事实字段或字段块的能力契约。

字段级 slot 的最小能力如下：

| 能力 | 字段建议 | 作用 |
|---|---|---|
| 地址 | `field_slot_id`、`slot_id`、`offset`、`width` | 唯一定位一个反射内存事实；`slot_id` 对应 64 KB 大 slot，`field_slot_id` 对应 slot 内字段。 |
| 类型 | `type` | 声明 `FLAG/ENUM/COUNTER/STEP/ERROR/VERSION/TIMESTAMP/U32/I32/FIXED/CRC/BITMASK` 等类型。 |
| 值域 | `value_min/value_max`、`enum_table_id` | 写入前做边界检查，防止非法状态进入共同事实。 |
| 写权限 | `writer_domain`、`writer_instance`、`writer_owner` | 定义唯一 writer；`DistributedRefMemAO` 接收事实提交时必须以此校验。 |
| 原子访问 | `atomic_width`、`memory_order` | 定义 8/16/32-bit 原子读写、DMB 或等价跨核屏障要求。 |
| 版本 | `version_field_slot_id`、`version_policy` | 支持读前后比对、seqlock 或字段组一致性检查。 |
| 时间戳 | `timestamp_field_slot_id`、`timestamp_kind` | 记录更新时间，支撑 stale、timeout、报告排序和重采样判断。 |
| 生命周期 | `lifecycle`、`valid_state_mask`、`clear_policy` | 定义何时有效、何时清零、何时冻结和何时失效。 |
| 错误绑定 | `error_field_slot_id`、`error_scope` | 把字段错误归因到本字段、本节点、上游或系统级错误。 |
| 订阅 | `subscription_mask`、`event_link_id` | 字段变化时投递本地事件、更新 dirty bitmap 或触发 delta 发布。 |
| 发布策略 | `snapshot_policy`、`sync_policy`、`crc_scope` | 定义直读、seqlock、双缓冲、是否跨节点 delta 同步和 CRC 范围。 |
| 权限 | `permission`、`write_mode` | 区分只读事实、命令写、配置 staging 写和维护写。 |

建议首版不要把它作为独立配置源，而是在代码中生成或静态固化为派生只读视图：

```c
typedef enum {
    RM_FIELD_TYPE_FLAG,
    RM_FIELD_TYPE_ENUM,
    RM_FIELD_TYPE_COUNTER,
    RM_FIELD_TYPE_STEP,
    RM_FIELD_TYPE_ERROR,
    RM_FIELD_TYPE_VERSION,
    RM_FIELD_TYPE_TIMESTAMP,
    RM_FIELD_TYPE_U32,
    RM_FIELD_TYPE_I32,
    RM_FIELD_TYPE_FIXED,
    RM_FIELD_TYPE_CRC,
    RM_FIELD_TYPE_BITMASK,
} rm_field_type_t;

typedef struct {
    uint16_t field_slot_id;
    uint8_t  vector_slot_id;
    uint16_t offset;
    uint8_t  width;
    rm_field_type_t type;
    uint32_t value_min;
    uint32_t value_max;
    uint32_t writer_domain;
    uint32_t writer_instance;
    uint16_t version_field_slot_id;
    uint16_t timestamp_field_slot_id;
    uint16_t error_field_slot_id;
    uint8_t  lifecycle;
    uint8_t  snapshot_policy;
    uint8_t  sync_policy;
    uint8_t  permission;
    uint32_t subscription_mask;
} refmem_slot_contract_t;
```

`DistributedRefMemAO` 内部校验必须按 `RefMemSlotContract` 执行，调用者不能自行拼接地址和权限。示例 helper 只能作为 RefMemAO 内部实现，不作为对外业务入口：

```c
bool refmem_slot_contract_validate_write(uint16_t field_slot_id,
                                         uint32_t writer_instance,
                                         uint32_t value);

bool refmem_slot_contract_validate_snapshot(uint16_t field_slot_id,
                                            uint32_t reader_instance);

bool refmem_slot_contract_validate_subscription(uint16_t field_slot_id,
                                                uint32_t subscriber_instance);
```

规则：

- `field_slot_id` 是反射内存字段级地址，A0-A7 的 `node_id` 是节点装载插槽地址，两者必须分层命名，禁止混用。
- `DistributedDataLinkTable` 可以引用 `field_slot_id`，用于声明 writer/reader 和数据连接；`RefMemSlotContract` 定义 `DistributedRefMemAO` 接收、验证和发布该字段时的条件。
- `RefMemSlotContract` 是 `DistributedRefMemAO` 的内部契约视图，不是新的业务建模入口，也不是绕过 AO/FB 的运行 API；配置源仍以现有静态模型表和 Vector layout 为准。
- owner、sequence/version、CRC/seqlock、RAM-resident/flash lockout 相关事实都可以作为字段级 slot 元素表达，但字段本身仍必须服从唯一 writer 和生命周期。
- 对 core1 realtime 快路径发布的字段，`snapshot_policy` 至少应为 `DIRECT_ATOMIC` 或 `SEQLOCK`；多字段一致快照必须使用 version/seqlock 或双缓冲。
- 订阅只允许投递轻量事件、dirty bitmap 或 delta publish 请求，不允许在写入临界区执行耗时动作。
- 运行门禁不得直接相信裸字段值，必须检查字段值域、writer、版本、时间戳、stale 和错误绑定。

### DistributedDeploymentGate

`DistributedDeploymentGate` 是 RUN 前的一票否决表。它聚合静态模型、版本、资源、IO、时间和校准/同步质量，判断当前系统是否允许进入触发运行。

| 检查项 | 内容 | 失败处理 |
|---|---|---|
| `layout_check` | `layout_version`、slot offset、slot size、directory CRC。 | 拒绝 RUN。 |
| `node_check` | 必需 A0-A7 插槽 online、SlotClaimMap resolved assignment、node_uuid、hardware profile、capability 和装载实例匹配。 | 拒绝 RUN 或按 fail_policy 降级。 |
| `instance_check` | required AO/FB instance 存在、版本兼容、enable 条件满足。 | 拒绝 RUN。 |
| `resource_check` | Flash、SD、USB、PIO、DMA、core1、RJ45 等资源无冲突。 | 拒绝冲突实例组合。 |
| `io_check` | SMA/RJ45/link-control resources/BiSS-C/UART/RS485 等 IO claim 无冲突；当前项目实例可映射为 SP8T/SP2T。 | 拒绝 RUN 或拒绝实例启用。 |
| `writer_check` | 每个 slot 字段只有唯一 writer。 | 拒绝 RUN。 |
| `event_check` | 必需事件连接完整，timeout 和 ACK 策略明确。 | 拒绝 RUN。 |
| `data_check` | 必需数据连接完整，单位、值域、生命周期一致。 | 拒绝 RUN。 |
| `config_check` | config CRC、sequence CRC、angle CRC、permission version 一致。 | 拒绝 RUN。 |
| `cal_sync_check` | calibration CRC、sync profile CRC、VDC/DPLL lock quality 满足门限。 | 拒绝 RUN 或进入校准/同步维护。 |
| `quality_check` | stale、CRC、seq、late、drop、timeout 在门限内。 | 拒绝 RUN 或 latch fault。 |

Gate 输出必须可查询和可追溯：

```text
gate_state
reject_code
reject_instance
reject_node
reject_slot
reject_evidence_index
active_crc_bundle
last_check_tick
last_pass_tick
```

### DistributedConnectionQualityTable

`DistributedConnectionQualityTable` 面向诊断、报告和运行门禁。它记录连接是否可信，不替代业务状态。

| 字段 | 含义 | 约束 |
|---|---|---|
| `quality_id` | 质量记录编号。 | 可按 node、link、slot 或 event link 建立。 |
| `scope` | 质量范围。 | `NODE`、`RJ45_LINK`、`SLOT`、`EVENT_LINK`、`DATA_LINK`。 |
| `source_node/target_node` | 源/目标节点。 | 本地项可 target=self。 |
| `seq_expected/seq_last` | sequence 检查。 | 检测丢帧和乱序。 |
| `crc_error_count` | CRC 错误计数。 | 进入 evidence。 |
| `stale_count` | stale 次数。 | 影响 READ 与 RUN 门禁。 |
| `late_count` | late 次数。 | 用于 T2、FIRE_LOAD、SYNC 质量。 |
| `drop_count` | 丢弃计数。 | 包括队列满、过期、重复包。 |
| `timeout_count` | 超时计数。 | 对应 event ACK 或 data stale。 |
| `last_error` | 最近错误。 | 错误码必须可枚举。 |
| `last_error_tick` | 最近错误时间。 | 使用回绕安全差值计算。 |
| `p99/p999` | 延迟或误差分布。 | 支撑产品化报告。 |
| `evidence_index` | 证据索引。 | 指向 FaultEvidenceSlot 或 SD 日志。 |

## 核心数据面

首版 64 KB 表保持 RTOS 架构中的完整布局：

| Slot | Offset | Size | 内容 | 写入者 |
|---|---:|---:|---|---|
| Header/Directory | `0x0000` | 1 KB | magic、layout、slot offset、table_seq、epoch、crc32 | RefMem Domain |
| SystemSlot | `0x0400` | 1 KB | system_mode、role_map_version、run_id、fault_latch、release gate | SystemAO |
| Role/ConfigSlot | `0x0800` | 2 KB | NodeRoleMap、hw_profile、persona、feature mask | SystemAO / config loader |
| VdcSlot | `0x1000` | 2 KB | sync_id、offset、rate、lock_state、holdover、relock、`e_vdc` | VdcSyncAO |
| LoopSlot | `0x1800` | 4 KB | trigger param、angle sweep/breakpoint、active sequence、scan_index | LoopEngineAO |
| DpllSlot | `0x2800` | 2 KB | compare 捕获、角度预测、`T_fire_base`、`e_pll` | AngleDpll owner |
| NodeSlot[8] | `0x3000` | 4 KB | A0-A7 通用插槽的 node_id、装载摘要、heartbeat、local_state、error_code、stale_count | 各节点 owner |
| TriggerSlot[8] | `0x4000` | 8 KB | armed、last_fire_seq、late_count、t2_count、ready_timeout | 各节点 core1 摘要 |
| IoSlot[8] | `0x6000` | 8 KB | SMA/RJ45/BiSS IO 镜像、边沿计数、健康状态 | 各节点 IO owner |
| CalibrationSlot | `0x8000` | 8 KB | link table、delay table、staging/active/version/quality | CalibrationAO |
| StatisticsSlot | `0xA000` | 8 KB | `e_vdc/e_act/e_pll`、CRC/seq/late 分布、p99/p999 | Statistics / Measure owner |
| AckCommandSlot | `0xC000` | 4 KB | command_seq、ack/nack/busy/timeout 位图、原子命令槽 | 命令 owner + 节点 ACK |
| FaultEvidenceSlot | `0xD000` | 6 KB | fault_code、source_node、epoch、run_id、关键证据 | SystemAO / DiagnosticsAO |
| GatewaySlot | `0xE800` | 2 KB | A3/VNA/host 状态、采集状态 | GatewayAO |
| OtaStorageUiSlot | `0xF000` | 2 KB | OTA、Storage、UI 摘要 | 对应 task owner |
| TlvExtension | `0xF800` | 2 KB | versioned TLV、未来扩展 | owner by type |

表尾固定为 `0x10000`，总大小固定 64 KB。任何 slot offset、slot size 或 slot 顺序变化，都必须提升 `layout_version`，并导致旧 System Pack / 旧节点镜像进入 `INVALID` 或兼容转换路径。

### Header 与 Directory 契约

Header/Directory 是 RefMem 的自描述入口。它必须至少提供：

| 字段 | 作用 | 规则 |
|---|---|---|
| `magic/end_magic` | 表识别和越界破坏检测。 | 初始化和 snapshot 时都必须校验。 |
| `layout_version` | 64 KB 表布局版本。 | 首版冻结为 v1；布局变化必须递增。 |
| `table_size` | 总表大小。 | 固定 65536。 |
| `table_seq` | 全表事实序号。 | 任意 slot active fact 更新后递增。 |
| `epoch_id` | 系统事实纪元。 | 复位、System Pack 切换、RUN 批次切换或重大恢复后递增。 |
| `run_id` | 当前运行批次。 | 把配置、同步、T2、故障和报告绑定到同一批次。 |
| `slot_count` | directory 项数量。 | 首版为 16。 |
| `slot_directory[]` | slot id、offset、size、owner、flags、crc。 | RUN 前必须校验 offset/size 不重叠且覆盖 64 KB。 |
| `directory_crc32` | directory 自身 CRC。 | 防止 slot map 半更新。 |
| `header_crc32` | header CRC。 | 不包含 `header_crc32` 字段自身。 |
| `compat_min_version` | 最低兼容 layout。 | 节点低于该版本时拒绝加入 RUN。 |

Directory 项建议结构：

```text
slot_id
offset
size
owner_domain
owner_node_mask
writer_instance
flags
slot_crc32
slot_seq
stale_window_us
```

### Slot Guard 契约

每个 slot 的首部应预留统一 guard。P0 代码只有 header/node 的轻量 guard 语义，产品化版本需要推广到全部 slot。

```text
slot_seq
owner_domain
owner_node_id
writer_instance
crc32
stale_state
flags
write_epoch
write_tick32
```

规则：

- `slot_seq` 在本 slot 完整发布后递增。
- `crc32` 覆盖 guard 后的有效 payload，不能覆盖自身。
- `stale_state` 使用 `OK/STALE/MISSING/INVALID/FAULT`。
- `owner_domain` 和 `writer_instance` 必须能在 `DistributedDataLinkTable` 中找到唯一来源。
- 非 owner 不能直接写 slot；跨域写入必须走 command/config staging。

### Owner 与写权限

RefMem 的写权限按 slot、字段和节点共同约束：

| 写入范围 | 允许 writer | 禁止事项 |
|---|---|---|
| Header/Directory | RefMem Domain | 业务域直接改 layout、offset、slot_count。 |
| SystemSlot | SystemAO / ConfigGate | SCPI callback 直接写 active state。 |
| Role/ConfigSlot | SystemAO / config loader | 运行中无 gate 改 role/persona。 |
| VdcSlot | VdcSyncAO / SyncDpllFB | Angle DPLL 写 VDC offset/rate。 |
| LoopSlot | LoopEngineAO | TriggerAO 或 SCPI 直接改 active sequence。 |
| DpllSlot | AngleDpll owner | SYNC DPLL 写 `T_fire_base`。 |
| NodeSlot[n] | 节点 n owner / RefMem Sync 接收镜像 | 节点 A 写节点 B 的本地 owner 字段。 |
| TriggerSlot[n] | 节点 n core1 realtime 摘要 / core0 合并 owner | SCPI 临时读取时触发现场 IO。 |
| IoSlot[n] | 节点 n IO owner | 业务域绕过 IO owner 直接改 IO 镜像。 |
| CalibrationSlot | CalibrationAO | SYNC 或 TRIG 私自修正 delay table。 |
| StatisticsSlot | MEASure / Statistics owner | 业务配置写统计结果。 |
| AckCommandSlot | command owner + target node ACK writer | 在执行动作临界区内做耗时工作。 |
| FaultEvidenceSlot | SystemAO / DiagnosticsAO | 覆盖未落盘 evidence。 |
| GatewaySlot | GatewayAO | 普通节点假冒上位机网关状态。 |
| OtaStorageUiSlot | OTA / Storage / UI owner | core1 实时侧直接落盘或改 UI 状态。 |
| TlvExtension | owner by TLV type | 未注册 TLV type 写入。 |

### Command / ACK / NACK 契约

`AckCommandSlot` 是跨域、跨核和跨节点命令闭环的数据面。它不执行业务动作，只保存命令意图、目标节点、payload 引用、ACK/NACK/busy/timeout 位图和可回溯证据。

写命令的基本语义：

```text
SCPI / UI / System Pack accepted
  -> command slot POSTED
  -> target owner TAKE
  -> target AO/FB 执行或分步 busy
  -> ACK / NACK / TIMEOUT / FAULT
  -> SCPI / UI / Report 读取快照闭环
```

SCPI 写命令返回 `OK` 或 `1` 只表示接口层 accepted，不表示动作已经完成。动作完成、拒绝原因、节点超时和质量证据必须通过 `SYSTem:CONFigure:ACK?`、后续 `SYSTem:COMMand:ACK?`、对应 `READ:*?` 或故障/报告接口回读。

`AckCommandSlot` 首版字段建议：

| 字段 | 含义 | 规则 |
|---|---|---|
| `slot_guard` | 统一 slot guard。 | 带 `slot_seq`、owner、crc、stale。 |
| `command_seq` | 命令序号。 | command owner 单调递增，0 保留为无效。 |
| `source_node` | 发起节点。 | 字段名沿用 node 是协议兼容名，语义上指 A0-A7 slot；对应实例可为 B3 gateway 或 B0 SystemAO。 |
| `source_instance` | 发起实例。 | 对应 `DistributedFbInstanceTable`。 |
| `target_mask` | 目标 A0-A7 slot/node_id 位图。 | 只允许指向静态模型中存在的通用插槽。 |
| `required_mask` | 必须 ACK 的目标位图。 | 可小于 target_mask，用于 report-only 节点。 |
| `command_type` | 命令类型。 | 使用枚举，不使用自由字符串。 |
| `command_class` | 权限和资源分类。 | 配合 RUN 态策略、ResourceArbiter 和权限表。 |
| `payload_kind` | payload 类型。 | `INLINE_SMALL`、`SLOT_REF`、`TLV_REF`、`STAGING_REF`。 |
| `payload_ref` | payload 引用。 | 指向 staging slot、TLV 或小 payload 区。 |
| `payload_size` | payload 长度。 | 大 payload 不进入 RefMem。 |
| `payload_crc32` | payload 摘要。 | target TAKE 前必须校验。 |
| `issue_epoch/run_id` | 发起上下文。 | 防止旧 RUN 命令污染新 RUN。 |
| `issue_tick32` | 发起时间。 | timeout 使用回绕安全差值。 |
| `timeout_us` | 命令超时。 | 0 表示无 timeout，但产品 RUN 命令不应为 0。 |
| `taken_flags` | 目标已 take 位图。 | 原子设置。 |
| `ack_flags` | 目标完成 ACK 位图。 | 只由目标 owner 设置。 |
| `nack_flags` | 目标拒绝位图。 | 只由目标 owner 设置。 |
| `busy_flags` | 目标忙位图。 | 表示已接收但需后续 tick 推进。 |
| `timeout_flags` | command owner 判定超时位图。 | 不由目标节点自己设置。 |
| `last_nack_reason` | 最近 NACK 原因。 | 枚举值，必须能查 reason 表。 |
| `last_nack_node` | 最近 NACK 节点。 | 0-7 或 `UINT32_MAX`。 |
| `reason_table_crc32` | reason 表摘要。 | 上位机可校验解释版本。 |
| `evidence_index` | 证据索引。 | 指向 FaultEvidenceSlot 或 SD 日志。 |
| `clear_seq` | 清除确认序号。 | 防止清错新命令。 |

命令类型首版建议：

| `command_type` | 用途 | 典型 owner |
|---|---|---|
| `CONFIG_STAGE` | 写入配置 staging。 | ConfigGate / 对应 Domain AO |
| `CONFIG_ACTIVATE` | staging 转 active。 | ConfigGate |
| `START` / `STOP` | 启停产品运行。 | LoopEngineAO / SystemAO |
| `ARM` | 装载触发准备。 | TriggerAO |
| `FIRE_LOAD` | 装载未来触发时间。 | core1 realtime owner |
| `CAL_START` | 启动指定链路校准。 | CalibrationAO |
| `CAL_SAVE_LOAD_ACTIVATE` | 校准数据持久化/加载/激活。 | CalibrationAO / StorageAO |
| `SYNC_START_STOP` | 开启或停止同步维护。 | VdcSyncAO |
| `SYNC_RELOCK_HOLDOVER` | DPLL/VDC 维护动作。 | VdcSyncAO |
| `FAULT_CLEAR` | 清除可恢复故障锁存。 | SystemAO |
| `RESOURCE_JOB` | Flash/SD/OTA 等资源任务。 | Resource owner |
| `MAINTENANCE` | 维护/产测动作。 | 对应维护 owner |

命令状态机：

```text
IDLE
POSTED
TAKEN
EXECUTING / BUSY
ACKED / NACKED / TIMED_OUT / FAULTED
CLEAR_PENDING
IDLE
```

规则：

- command owner 只能在 `IDLE` 或 `CLEAR_PENDING` 完成后发布新 `command_seq`。
- target owner TAKE 命令时只做原子占用、payload CRC、epoch/run_id 和权限检查。
- TAKE/CLEAR 的临界区只允许修改位图和状态，不允许执行耗时动作。
- 耗时动作必须转入 AO/FB service tick，期间设置 `busy_flags`。
- target 完成后设置 `ack_flags` 或 `nack_flags`，并清除自身 busy 位。
- command owner 根据 `required_mask` 判断命令完成：`required_mask == ack_flags` 为完成；任一 required NACK 或 timeout 为失败。
- `ack_flags`、`nack_flags`、`busy_flags` 和 `timeout_flags` 互斥；同一节点不能同时处于多个终态。

重复和 stale 策略：

| 场景 | 处理 |
|---|---|
| 相同 `command_seq + payload_crc32` 重复到达 | target 返回上一次 ACK/NACK，不重复执行动作。 |
| 相同 `command_seq` 但 payload CRC 不同 | 标记 `NACK_DUP_SEQ_CRC_MISMATCH`。 |
| 低于 last completed seq 的旧命令 | 标记 stale 或忽略，并记录 quality drop。 |
| `issue_epoch/run_id` 与 active 不一致 | 标记 `NACK_EPOCH_MISMATCH`。 |
| target 长时间 busy 超过 `timeout_us` | command owner 设置 timeout 位，必要时升级 fault。 |
| clear_seq 与当前 command_seq 不一致 | 拒绝 clear，防止清掉新命令。 |

Reason 表至少覆盖：

| reason | 含义 |
|---|---|
| `NONE` | 无拒绝。 |
| `CONFIG_CRC_MISMATCH` | 配置 CRC 不一致。 |
| `HW_PROFILE_MISMATCH` | 硬件 profile 不匹配。 |
| `NODE_STALE` | 节点 stale。 |
| `NODE_FAULT` | 节点故障。 |
| `FLASH_LOCKOUT_UNREADY` | Flash/core1 lockout 未就绪。 |
| `RESOURCE_BUSY` | 资源被占用。 |
| `RUN_STATE_DENIED` | 当前 RUN 态禁止该命令。 |
| `PAYLOAD_CRC_MISMATCH` | payload 校验失败。 |
| `EPOCH_MISMATCH` | epoch/run_id 不匹配。 |
| `DUP_SEQ_CRC_MISMATCH` | 重复序号但 payload 不同。 |
| `TIMEOUT` | 命令超时。 |
| `PERMISSION_DENIED` | 权限不足。 |

现有 `SYSTem:CONFigure:ACK?` / `SYSTem:CONFigure:NACK?` 是 `AckCommandSlot` 的配置门禁视图。后续如果增加 `SYSTem:COMMand:ACK?` / `SYSTem:COMMand:NACK?`，应读取同一底层 command slot，不另建一套 ACK/NACK 事实。

### Snapshot 与并发契约

所有对外读取都必须读取快照。SCPI/UI/Report 读取 RefMem 时，不允许临时跨板阻塞查询，也不允许临时驱动现场 IO。

首版快照策略：

| 数据类型 | 策略 | 适用场景 |
|---|---|---|
| 单个 32-bit 事实 | `DIRECT_ATOMIC` | heartbeat、flags、state、counter。 |
| 小型结构 | `SEQLOCK` | node snapshot、guard、status summary。 |
| 大型 payload | `DOUBLE_BUFFER` | sequence map、delay table、statistics block。 |
| SD/report 证据 | `EVIDENCE_REF` | RefMem 只保存索引和摘要。 |

跨核共享字段必须使用 `__atomic`、DMB 屏障或等价 RTOS/SDK 屏障。若字段来自 core1 realtime 快路径，core0 只能读取 ring 或 snapshot，不能在 core1 正执行 PIO/DMA 快路径时持有长临界区。

### Version Bundle

RefMem Header/SystemSlot 必须携带下面的版本束，用于 RUN gate 和报告闭环：

```text
layout_version
application_version
config_version / config_crc
calibration_version / calibration_crc
sync_profile_version / sync_profile_crc
loop_version / active_sequence_crc
action_version
permission_version
storage_pack_version
build_id
hw_profile_crc
```

任何会改变 RUN 语义的配置切换，都必须形成新的 `active_crc_bundle`，并记录到 DeploymentGate 和 FaultEvidence/Report。

### 时间字段与回绕

RefMem 中的时间字段分三类：

| 类型 | 用途 | 规则 |
|---|---|---|
| `tick32_ms/us` | 短窗口调度、stale、timeout。 | 差值必须写成 `int32_t diff = (int32_t)(t1 - t0)`。 |
| `epoch_id + tick32` | RUN 批次内事件、T2、故障 evidence。 | 查询和报告必须同时带 epoch/run_id。 |
| `dc_time64_ns` | VDC/DPLL 稳态共同时间、预测分发。 | 由 SYNC/VDC owner 发布，业务域只读。 |

`uint32_t` tick 不能直接表示长时间绝对时间。产品化版本必须增加 `epoch_seconds` 或等价 `time_epoch` 字段，避免 49 天回绕破坏 VDC/DPLL/T2、日志排序和 stale 计算。

## 对外接口边界

RefMem 不建立裸顶级 `REFMEM` SCPI 域。对外维护查询归 `SYSTem:REFMEM:*`：

```text
SYSTem:REFMEM:STATus?
SYSTem:REFMEM:NODE?
SYSTem:REFMEM:CLAIM?
SYSTem:REFMEM:LOAD:SD
SYSTem:REFMEM:LOAD:NODE
SYSTem:REFMEM:LOAD:STATus?
SYSTem:REFMEM:BOARD?
SYSTem:REFMEM:LOAD:BOARD
SYSTem:REFMEM:LOAD:BOARD:STATus?
SYSTem:REFMEM:TABle?
```

SCPI callback 只能读取 RefMem snapshot 或提交受控 owner 意图，不能临时触发跨板查询，也不能直接修改 state、summary、result、health、quality 或 evidence slot。RefMem 向量表不承载 `app_model.rmtp` 文件数据；它只保存 state、size、CRC、path hash、table registry、load snapshot 和 evidence 等事实摘要。完整文件数据属于 StorageAO 私有事务 buffer 或 SD/FatFs 后端对象。

### StorageAO File Management

`app_model.rmtp` 的写入不是 SCPI 直接写 FatFs，也不是 RefMem 向量表承载数据。它通过 StorageAO 的通用文件管理基础件完成；RefMem 只在 `LOAD:SD` 阶段读取并校验文件内容：

```text
SCPI command
  -> StorageAO file/directory command
  -> path whitelist + mode gate
  -> receive/update private transaction buffer
  -> CRC gate
  -> StorageAO FILE_WRITE job
  -> backend atomic replace
  -> FILE_INFO/FILE_READ/FILE_DELETE/FILE_RENAME query and evidence
  -> SYSTem:REFMEM:LOAD:SD validates package into RefMem staging
```

通用 Storage 文件/目录能力：

| 类别 | 命令 | 语义 |
|---|---|---|
| 文件写事务 | `SYSTem:STORage:FILE:WRITe:BEGIN/DATA/END/ABORt/STATus?` | 分块写入 StorageAO 私有 buffer，CRC 通过后由 `FILE_WRITE` job 原子替换后端文件。 |
| 文件查询 | `SYSTem:STORage:FILE:INFO?/READ?` | 通过 StorageAO job 查询文件/目录摘要或读取受限片段。 |
| 文件维护 | `SYSTem:STORage:FILE:DELete/REName` | 通过 StorageAO job 删除或移动/改名文件。 |
| 目录维护 | `SYSTem:STORage:DIRectory:CREate/DELete/REName/CATalog?` | 通过 StorageAO job 创建、删除、移动/改名和分页枚举目录。 |

接口规则：

- `BEGIN "<path>",<size>,<crc32>` 只建立 StorageAO 写事务，返回 `txn_id`，不写 SD。
- `DATA <offset>,"<hex>"` 只允许顺序 offset 分块，写入 StorageAO 私有 buffer；数据不进入 RefMem 向量表。
- `END` 先校验传输 CRC，再投递 StorageAO `FILE_WRITE` job，由 StorageAO owner 执行目录创建、临时文件写入、sync 和原子替换。
- `INFO?`、`READ?`、`DELete`、`REName` 和目录命令都通过 StorageAO job 完成；SCPI 层不直接调用 FatFs。
- 路径先经 StorageAO 规范化和白名单检查；调试上位机可再做产品级封装限制，但底层不绑定 `REFMEM_PACKAGE` 专用入口。
- `LOAD:SD` 仍是独立步骤：它读取 `/refmem/app_model.rmtp`，校验 `.rmtp` header、目录和 CRC，只写 RefMem staging snapshot，不直接替换 active image。
- 后续对接其他存储设备时，SCPI 和 RefMem 不变，只扩展 StorageAO backend contract。

### RefMem Load 状态机

RefMem 动态加载不是产品 `TRIGger:MODE`，也不是 `ResourceArbiter` 的 BOOT/RUN/OTA。它是 `DistributedRefMemAO` 自己的表镜像状态机：

| mode | 含义 | 允许入口 |
|---|---|---|
| `IDLE` | RefMem 没有 load/validate/activate 事务。 | 允许 `LOAD:SD`、`LOAD:NODE`、`LOAD:BOARD` 写 staging。 |
| `LOAD_TO_STAGING` | 正在接收 SD/System Pack 或 SCPI inline 节点候选。 | 禁止新的 load begin。 |
| `VALIDATING` | 正在做 CRC/lint/owner validation。 | 禁止新的 load begin。 |
| `ACTIVATING` | staging 正在切换 active。 | 禁止新的 load begin，后续必须接全环 ACK。 |
| `FAULT` | RefMem 加载或激活失败并锁存。 | 只允许维护清除或诊断查询。 |

staging image 独立于 active image：

```text
SCPI / SD
  -> RefMem mode IDLE gate
  -> LOAD_TO_STAGING
  -> VALIDATING
  -> staging_state = VALIDATED / FAILED
  -> mode 回到 IDLE
  -> 后续 ACTIVATE 才允许替换 active
```

`SYSTem:REFMEM:LOAD:SD [path]` 扫描 SD `/manifest.idx`，随后读取并校验 RefMem table image；默认路径为 `/refmem/app_model.rmtp`，可用可选 path 覆盖。当前 parser 已校验 `.rmtp` header、table directory、payload CRC、package CRC 和单表 CRC，但仍只写 staging load snapshot，不替换 active image。`SYSTem:REFMEM:LOAD:NODE <node_id>,<instance_id>,<role_mask>,<persona_mask>[,<enabled>,<required>,<load_order>]` 允许 SCPI 直接提交一条 NodeLoad 候选到 staging，用于调试和自组网协调前的节点实例化验证。`SYSTem:REFMEM:LOAD:BOARD <board_id>,<board_uuid_crc32>,<capability_mask>,<io_constraint_mask>,<ip_core_mask>,<default_persona_mask>,<hw_profile_crc32>,<active_default_slot>,<online_required>` 允许 SCPI 提交一条 BoardCapability 候选到 staging，用于调试和 SD System Pack 前置验证；它必须满足 `REFMEM+VDC` baseline 和字段范围约束。以上入口都不直接覆盖 active 表，也不修改 NodeSlot live fact。`SYSTem:REFMEM:LOAD:STATus?` 读取 NodeLoad/SD load snapshot：`version,load_seq,source,mode,staging_state,manifest_status,manifest_schema,manifest_required_count,manifest_missing_count,path_hash,active_package_crc32,staging_package_crc32,staging_lint_error_count,staging_first_lint_error,staging_node_id,staging_instance_id,staging_role_mask,staging_persona_mask,staging_enabled,staging_required,staging_load_order,last_error,manifest_build_id,path`。`SYSTem:REFMEM:LOAD:BOARD:STATus?` 读取 BoardCapability staging snapshot：`version,load_seq,mode,staging_state,active_crc32,staging_crc32,staging_lint_error_count,staging_first_lint_error,staging_board_id,staging_board_uuid_crc32,staging_capability_mask,staging_io_constraint_mask,staging_ip_core_mask,staging_default_persona_mask,staging_hw_profile_crc32,staging_active_default_slot,staging_online_required,last_error`。

写入 `/refmem/app_model.rmtp` 使用通用 Storage 文件接口，不再保留 `SYSTem:REFMEM:PACKage:*` 专用入口：

| 命令 | 返回 | 语义 |
|---|---|---|
| `SYSTem:STORage:FILE:WRITe:BEGIN "/refmem/app_model.rmtp",<size>,<crc32>` | `OK,txn_id` | 在 StorageAO 建立写事务。 |
| `SYSTem:STORage:FILE:WRITe:DATA <txn_id>,<offset>,"<hex>"` | `OK,txn_id,received_size` | 顺序写入事务 buffer。 |
| `SYSTem:STORage:FILE:WRITe:END <txn_id>` | `WRITTEN,<status fields>` | CRC 通过后提交 StorageAO 写 job，原子写入 `/refmem/app_model.rmtp`。 |
| `SYSTem:STORage:FILE:WRITe:ABORt [txn_id]` | `OK` | 中止当前事务。 |
| `SYSTem:STORage:FILE:WRITe:STATus?` | `active,txn_id,state,expected_size,received_size,expected_crc32,actual_crc32,path_hash,error,job_id,path` | 查询 StorageAO 写事务摘要。 |
| `SYSTem:STORage:FILE:INFO? "/refmem/app_model.rmtp"` | `OK/ERROR,job_id,job_state,kind,size,path_hash,error,path` | 查询文件信息。 |
| `SYSTem:STORage:FILE:READ? "/refmem/app_model.rmtp",[offset],[length]` | `OK/ERROR,job_id,offset,requested,returned,file_size,eof,path_hash,error,hex` | 分段读取文件内容。 |
| `SYSTem:STORage:FILE:DELete "/refmem/app_model.rmtp"` | `DELETED/ERROR,job_id,job_state,error,path` | 删除文件。 |

### RefMem Table Image 格式

SD 根 `/manifest.idx` 只作为 System Pack 总索引；它负责告诉固件“有哪些必需文件、产品/硬件/schema 是否匹配”。RefMem 自己的表镜像必须放在独立文件中，由 manifest required 行引用，避免把二进制表和 SD manifest 字符串解析混在一起。

首版文件路径建议：

```text
/refmem/app_model.rmtp
/refmem/app_model.idx
/refmem/app_model.json
```

其中 `.rmtp` 是固件后续解析的二进制 table image，`.idx` 是轻量索引，`.json` 是 PC 工具和人工审查用说明。`tools/refmem_pack_build/refmem_pack_build.py` 已固化最小占位格式，后续 `sd_fs_build.py` 可以把它复制进 SD System Pack，并在根 `/manifest.idx` 中加入：

```text
required=/refmem/app_model.rmtp,type=refmem_table_image,size=<bytes>,crc32=<crc32>
```

`.rmtp` header 首版固定 64 字节，小端序：

| Offset | 字段 | 含义 |
|---:|---|---|
| 0 | `magic[4] = "RMTP"` | RefMem Table Package。 |
| 4 | `format_version` | 当前为 1。 |
| 8 | `header_size` | 当前为 64。 |
| 12 | `total_size` | 完整 package 大小。 |
| 16 | `table_count` | 当前 9，包含 ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality。 |
| 20 | `table_dir_size` | table directory 字节数。 |
| 24 | `payload_crc32` | 所有表 payload 拼接 CRC。 |
| 28 | `package_crc32` | 完整 package CRC；计算时该字段先置 0。 |

table directory 每项 16 字节：

| 字段 | 含义 |
|---|---|
| `table_id` | 对应 ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality。 |
| `offset` | 表 payload 在 package 内偏移。 |
| `size` | 表 payload 字节数。 |
| `crc32` | 单表 payload CRC。 |

约束：

- `.rmtp` 只描述 RefMem static model table image，不承载 OTA payload、日志、波形或大证据。
- RefMem 向量表不承载 `.rmtp` 文件数据；最多记录路径、hash、CRC、load state 和 validation evidence。
- 固件 parser 必须先验证 magic、format_version、header_size、total_size、table_count、payload CRC、package CRC 和每表 CRC，再进入 owner validation。当前首版已完成这些格式与 CRC 校验。
- table image 只能写 staging，不能直接覆盖 active。
- owner validation 通过前，`RefMemTableRegistry` 只能显示 `STAGED/CRC_OK`，不能显示 `OWNER_OK`。
- owner validation 通过后仍不自动 RUN；必须等待显式 activation 和 RUN gate。
- 运行中不热替换 active image；需要在 RefMem mode `IDLE` 且实时触发 idle 的维护窗口执行 activation。

## 当前实现现状

当前代码中 `components/distributed_refmem/` 已经从单文件表骨架推进到首版 RefMem Domain 组件：

- `distributed_refmem.h/.c`：仍是当前对外兼容入口，维护本地 64 KB `DistributedVectorTable`、header、node slot、core vector、runtime protection snapshot 和 status flags。
- `refmem_vector_table.h/.c`：封装 64 KB table layout、slot directory、header CRC 和 directory 校验。
- `refmem_application_model.h/.c`：落地 ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality、静态 linter、package CRC 和 load staging snapshot。
- `refmem_slot_claim.h/.c`：首版派生 `SlotClaimMap`，从 GenericNode、BoardCapability、NodeLoad 和 FB instance 生成 A0-A7 resolved assignment、candidate/assigned/conflict/overflow 计数、loaded instance mask 和 CRC。
- `refmem_realtime_contract.h/.c`：首版派生 `RealtimeCapabilityContract`，从 NodeLoad、FB instance、GenericNode 和 SlotClaimMap resolved assignment 生成实例级资源/IO/类 IP 核能力契约；保留 default-slot fallback 仅用于过渡。
- `SYSTem:REFMEM:LOAD:SD`：已接入 Storage manifest 扫描和 `.rmtp` table image parser，可校验 header、directory、payload CRC、package CRC 和单表 CRC，当前仍只写 staging snapshot，不执行 active 切换。
- `SYSTem:REFMEM:LOAD:NODE`：已支持通过 SCPI inline 提交单条 NodeLoad 候选到 staging snapshot，尚未形成多条 staging NodeLoadTable image。
- `SYSTem:REFMEM:LOAD:STATus?`：已可查询 load sequence、source、RefMem load mode、staging state、manifest、active/staging CRC、lint/error 和当前候选。
- `SYSTem:REFMEM:LOAD:BOARD` / `SYSTem:REFMEM:LOAD:BOARD:STATus?`：已支持通过 SCPI inline 提交单条 BoardCapability 候选到 staging snapshot，校验 board 范围、`REFMEM+VDC` baseline 和默认 slot 范围；尚未形成多条 staging BoardCapabilityTable image。

尚未形成完整实现的部分：

- `RefMemTableRegistry`、active/staging/rollbackable 双镜像切换和 owner validation callback。
- `SlotClaimMap` RJ45 运行期聚合、自组网协调和 candidate overflow evidence；本地 SlotClaim gate 已接入 DeploymentGate/RUN gate。
- `RefMemSlotContract` 派生代码、字段级 owner 写权限、seqlock/双缓冲快照和 subscription 分发。
- `refmem_command.h/.c`、ACK/NACK 原子命令槽和 completion/fence 语义。
- RJ45_SYNC_RING 上的 `REFMEM_DELTA` / `REFMEM_EPOCH` 同步协议和受控 RMA window。

## 目标代码形态

后续建议收敛为：

```text
components/distributed_refmem/
  CMakeLists.txt
  inc/
    refmem_domain.h
    refmem_vector_table.h
    refmem_application_model.h
    refmem_table_registry.h
    refmem_slot_claim.h
    refmem_slot_contract.h
    refmem_sync.h
    refmem_command.h
    refmem_quality.h
  src/
    refmem_domain.c
    refmem_vector_table.c
    refmem_application_model.c
    refmem_table_registry.c
    refmem_slot_claim.c
    refmem_slot_contract.c
    refmem_sync.c
    refmem_command.c
    refmem_quality.c
```

旧 `distributed_refmem.h/.c` 可以在过渡期保留为兼容 wrapper，最终收敛到 `refmem_domain_*` 和 `refmem_vector_table_*`。
