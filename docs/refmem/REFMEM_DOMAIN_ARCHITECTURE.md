# Distributed RefMem 内部主域架构

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`
Last updated: 2026-11-09

本文档定义 Distributed Hard Real-Time Trigger System 在 HAOFV 下的 Distributed Vector Blackboard / RefMem Sync 内部主域。RefMem Domain 不是对外 SCPI 主域，也不是产品业务动作域，而是分布式系统的内部基础主域，负责把多节点共同事实、静态分布式应用模型、命令意图、ACK/NACK、版本、质量和证据组织成可验证的数据面，并以统一控制面把系统级命令意图路由到显式目标插槽、聚合 ACK/NACK/fence，使接入点不再决定系统操作语义。

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
如何通过任一接入点，把系统级命令意图安全地路由到目标节点并确认完成。
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
- 维护统一控制面：把任一接入点的系统级命令意图路由到显式目标插槽，聚合 ACK/NACK/fence 和本地系统视图。
- 管理 slot seq / CRC / stale / version / dirty。
- 承接跨节点 `REFMEM_DELTA` / `REFMEM_EPOCH` 同步。
- 给 SCPI/UI/System Pack 提供 snapshot。
- 给 ConfigGate / RUN gate 提供部署一致性判断。
- 给 Diagnostics / Report 提供 evidence。

RefMem Domain 不负责：

- 不执行业务动作。
- 不代替目标节点执行动作，不提供远程裸内存写、跨板阻塞查询或绕过目标本地门禁的强制位。
- 不驱动硬实时边沿。
- 不解析产品命令语义。
- 不传输 OTA payload、日志全文、波形、SD 文件内容或硬实时边沿。
- 不引入完整 IEC 61499 分布式运行时。
- 不支持运行时动态部署 FB、跨节点 FB 直接调用或动态事件路由。
- 不计算 VDC offset/rate，也不执行 DPLL；VDC 共同时间由 VDC Domain owner 发布，RefMem 只保存其 snapshot、版本、质量和 evidence。

### 本地异步诊断镜像

VDC 保留区中的 typed health 诊断扩展由 `distributed_refmem_priority_health.c` 实现，Core0 RefMem task 是唯一写者；偏移、schema 和大小以 `REFMEM_VDC_PRIORITY_*` 与 `refmem_vdc_priority_region_t` 为准。旧 Core1 模型 payload、guard、校验和总表布局保持不变。扩展有独立 guard，仅本地异步镜像，不增加 TDMA 席位或 Core1 工作；age/FRESH 代表采样时状态，不能实时授权。源读取失败保留上一采样；读者须检验 schema、writer、身份及校验，不与独立模型快照假定同代。

### Flash 持久化边界

RefMem 持久化的是静态部署 package，不是运行中的共同事实。`.rmtp`、ApplicationMap、
BoardCapability、NodeLoad、FB/Event/DataLink 和 active/previous package ref 可由 System Pack
blob 保存；每次启动必须先进入 staging，并由 `DistributedRefMemAO` 完成 CRC、schema、owner、
resource claim 和 DeploymentGate 校验后激活。

64 KB live vector、dirty、slot/field sequence、command、ACK/NACK、heartbeat、stale、peer
online、transport in-flight、epoch 和 RUN completion 不得从 Flash 恢复为有效事实。上电或
package 激活建立新 epoch，所有 peer mirror 在重新同步前保持 stale。Flash/Storage owner
只交付 immutable package，不直接写 RefMem active image。分区和事务规则见
`docs/arch/HAOFV_FLASH_ARCHITECTURE.md`。

## 分布式验证硬约束

RefMem Domain 的核心价值不是单板上的 64 KB 表结构，而是多板对共同事实的同步认知。因此实现顺序必须保持一条最小垂直闭环：

```text
local vector fact
  -> command / dirty / delta publish
  -> realtime transport encode
  -> peer receive / validate / commit
  -> peer snapshot visible
  -> ACK/NACK / quality / stale evidence
```

其中 BISS-C、RJ45_SYNC_RING、UART、RS485 或后续其他链路都只是不同的 transport adapter / realtime capability 载体。RefMem 本身不绑定某一种通讯协议，也不把 BISS-C 当作必要前置；它只定义共同事实、delta/epoch、command/ACK/NACK、stale、CRC、quality 和 evidence 的语义。后续 RefMem 代码推进不能长期停留在本地单元测试；必须尽早选择一个最小 transport adapter 验证 `REFMEM_DELTA`、`REFMEM_EPOCH`、ACK/NACK 和 quality 计数。当前最小系统板的端口、GPIO、线序和脚本记录放在 `REFMEM_MIN_SYSTEM_PLAYBOOK.md`。

首版可接受的最小分布式闭环为：

- 任一板发布一个小型 RefMem fact 或 command sequence。
- core1 realtime 或 PIO 传输层把 compact delta 发到对端。
- 对端校验 seq、CRC、epoch 和 source slot 后写入 mirror / snapshot。
- 发起端能通过 ACK/NACK、stale、drop、CRC error 和 visible counter 判断闭环是否完成。
- 该链路不要求一开始就是完整 BISS-C 协议，也不要求 RefMem 与 BISS-C 绑定；只要求 transport adapter 的边界能承接后续 BISS-C codec、SYNC capture/fire、VDC timestamp、UART/RS485 或其他物理链路实现。

### 事件驱动局部同步

64 KB `DistributedVectorTable` 是本地共同事实空间，不是每周期广播的数据包。
正常运行严禁重复刷新整表，跨板同步固定采用事件驱动的局部 delta：

```text
owner 提交 local fact
  -> slot_seq / field_seq 递增
  -> OR dirty_mask，并记录 target pending mask
  -> RefMem publisher 只把 delta descriptor 入队
  -> TDMA short-frame window 编码 critical delta
  -> target validate / commit / snapshot visible
  -> ACK 或 FENCE 完成
  -> 仅清除已由 required target 确认、且 generation 未再次变化的 dirty 位
```

约束：

- dirty 队列保存 `slot_id/field_id/generation/dirty_mask/target_mask/priority/deadline` 等描述符，不复制 64 KB 表。
- 同一 slot 的状态事实可在尚未编码前合并：保留最新 generation，合并 dirty mask；command/event 不允许用“只保留最后一次”方式合并，必须进入有界命令队列。
- `adapter_queued` 或 `transport_sent` 不能清 dirty。只有 required target 已 `visible_in_snapshot` 并返回 ACK，或对应 fence passed，才允许推进 `acked_generation`。
- delta in-flight 期间 owner 再次更新同一字段时，新 generation 必须继续保持 dirty，旧 ACK 不能误清新事实。
- 自动同步阶段只发送 critical dirty delta、ACK/fence、heartbeat 和 VDC short frame；未变化字段不消耗 TDMA 带宽。
- 首次节点加入、epoch/layout/CRC 不一致、stale 恢复或显式维护请求时，才允许生成 full snapshot/resync；该过程必须分片、走 `LONG` maintenance window，并可暂停/续传，不能退化为周期整表刷新。
- 大型静态表通过 System Pack staging/activate 和 CRC bundle 管理；运行期 RefMem 只传播必要 descriptor、版本、CRC 和局部事实。
- 在 flight process image 中，RefMem publisher 只写 `TdmaProcessImageMap` 分配给本地 slot 的 inactive shadow segment；它不直接操作 PIO、DMA 或 active TDMA buffer。core1 cycle swap 后，transport adapter 才消费该稳定 segment。

### 八槽 compact mailbox 消费规则

RefMem 在 TDMA SHORT 飞行帧中使用统一的 8 × 32 B process image。每个物理节点只写
自己拥有的 32 B slot，但可以通过 slot 头部的 `target_mask` 读取任意其他 active slot，
因此单 writer 不会隔离板间通信。每个 slot 的前 8 B 是 TDMA/core1 可快速判断的
`magic/version/source_slot/target_mask/seq16` metadata，后 24 B 才由 core0 RefMem owner
解释为 compact delta、ACK/fence、VDC 摘要或控制内容。

core1 只校验固定头、按 source owner 和本机 target bit 生成 RX segment bitmap，并完成
seq16 去重；去重状态只在 RX FIFO 成功接收完整副本后提交，FIFO 满时允许同 seq16
重试。它不调用 `refmem_sync_receive_frame()`，不解释字段、dirty mask 或业务值。
core0 从 `TDMA_RX_FRAME_FIFO` 取得完整 payload 后，严格按 `segment_mask` 扫描命中的
32 B slot；空 mask 表示本周期没有需解析的新事实，禁止退化为全 8-slot 扫描。

2/3/4/8 板只改变 active node mask，wire payload、slot offset 和 parser 不变。TX/RX
跨核交接通过 descriptor 的 `slot_index/generation/sequence` 与 buffer 中同名版本相互
校验，不依赖两个 FIFO 同时推进；RefMem generation/dirty/ACK 语义仍由 core0 owner
维护，不能用 transport 的 seq16 代替完整 seq32 或可见性确认。

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
TransportAdapter / local shared memory
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
- RefMem 统一控制面以 REFMEM 复制区为唯一命令事实契约：任意板写入本板 RefMem 槽后全环同步，接入点无关、显式 `target_mask`/`required_mask`、目标本地 owner 执行、ACK/NACK/fence 聚合；观测面直接读 REFMEM 快照。C0 方向（2026-10-23 修订）：命令槽由 REFMEM 复制区承载，static `s_refmem_command_slot` 仅为迁移前本地载体；详见本章“当前实现状态”。契约见 `REFMEM-CTRL-01`。
- A0-A7 是全环唯一的通用逻辑插槽，代表可装载 slot substrate，不代表固定产品角色；具体角色由 `DistributedNodeLoadTable` 绑定到 AO/FB instance。
- 一块物理板可以加载多个逻辑实例，例如调试阶段同时承载模拟网分和模拟转台；active assignment 仍最多 8 个，candidate proposal 上限为 16。
- `DistributedGenericNodeTable` 只描述通用逻辑插槽基座、硬件身份、capability、claim policy 和 fail policy；不得把业务 role/persona 反向塞回 GenericNode。
- `DistributedNodeLoadTable` 是 profile 到 A0-A7 的实例装载表；同一插槽允许多条 enabled load，但必须通过 DeploymentGate 的资源、IO、owner、writer、事件和数据连接检查。
- NodeLoad 装载实例时必须同时装载该实例的实时能力契约；也就是 role/persona、`resource_claim`、`io_claim`、`ip_core_claim`、事件连接和数据连接必须作为同一个 active profile 的部署事实被验证。
- 节点装载不仅装载 RefMem 表项，也必须装载对应 core1 realtime / PIO / DMA / codec 类 IP 核能力。比如链路控制节点装载后，应能接收同步脉冲、执行链路序列并发布 Trigger/Io snapshot；BISS-C 能力装载后，应对应 PIO 编码/解码、DMA ring 和状态镜像。
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

因此，`NodeRegion[8]` 只描述八个通用插槽的新鲜度、心跳、装载摘要和故障摘要；具体插槽承载真实板卡、脉冲分发、链路切换、仪表控制、网关、模型网分或模拟转台，由静态分布式应用模型决定。

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

#### Table Image Activation Contract

RefMem table image activation 是 `DistributedRefMemAO` 的内部表驱动事务，不是 SCPI 直接改 active 表，也不是 StorageAO 写文件动作。StorageAO 只保证文件对象已经按路径、长度和 CRC 进入后端；`LOAD:SD` 只把 `.rmtp` 解析结果写入 staging；真正改变系统共同事实的动作必须由 activation contract 完成。

三类镜像必须分离：

| 镜像 | 角色 | 数据位置 | 可见性 |
|---|---|---|---|
| `active_image` | 当前 RUN gate 和业务 AO/FB 使用的稳定表集合。 | RefMemAO 管理的 active descriptor / table buffer。 | 对业务 snapshot 可见。 |
| `staging_image` | SCPI、SD 或类似 OTA 加载后的候选表集合。 | RefMemAO 私有 staging buffer 或外部 table image descriptor。 | 只对维护查询和 validator 可见。 |
| `rollbackable_image` | 最近一次被替换的旧 active 表集合。 | RefMemAO 保留的 rollback descriptor / table buffer。 | 只对回滚和诊断 evidence 可见。 |

当前实现中，`.rmtp` package image 字节保存在 `RefMemTableRegistry` 私有 active/staging/rollbackable buffer 中，向量表和 `SYSTem:REFMEM:TABle?` 只暴露 descriptor、CRC、state、seq 和 evidence 摘要。metadata-only staging（例如单表 CRC staging、`LOAD:NODE` 首版 staging 或失败 staging）必须清空 staging payload；只有 `stage_package_image()` 成功复制了完整 package 字节后，activation 才能使用该 staging image，避免旧 package buffer 被误激活。

stable table view 通过 `refmem_table_registry_access_table(role, table_id, view)` 借出，只返回 const payload 指针、表 CRC、offset、size、package CRC 和 table seq；调用者必须在同一 owner 事务完成后调用 `refmem_table_registry_release_table(view)`。只要 active/staging/rollbackable 任一 image 存在未释放 view，activation 必须返回 `IMAGE_BUSY` 并保持旧 active 不变。维护查询 `SYSTem:REFMEM:TABle:VIEW?` 只借出后立即 release，并只返回摘要字段，不导出完整 table payload。业务结构化读取由 `refmem_application_model_apply_active_table_views()` 在 activation 成功后完成：ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality 被解析到 application model 私有 runtime snapshot，外部 getter 读取该 snapshot，不直接访问 RMTP bytes。

activation 状态必须比 load mode 更细：

```text
LOAD_TO_STAGING
  -> CRC_CHECK
  -> LINT_CHECK
  -> OWNER_VALIDATE
  -> ACTIVATE_PENDING
  -> ACTIVATING
  -> ACTIVE
  -> ROLLBACKABLE / FAILED
```

activation gate 至少检查：

| Gate | 检查内容 | 失败处理 |
|---|---|---|
| RefMem mode | 必须处于 RefMem load `IDLE` 或明确的 `ACTIVATE_PENDING`，不得已有并发表事务。 | 拒绝 activation，记录 busy evidence。 |
| 产品实时态 | realtime core、trigger、sequence、pulse counter 和 link control 必须处于 idle/park，可接受 flash lockout 或 RAM-resident 入口策略。 | 拒绝 activation，保留 staging。 |
| CRC bundle | package CRC、每表 CRC、active/staging CRC bundle 和 table seq 必须一致。 | staging 标记 `FAILED`。 |
| owner validation | 每张表由 owner callback 检查字段范围、writer 唯一性、资源冲突、IO/IP 核能力和生命周期。 | staging 标记 `FAILED`，写入 owner reason。 |
| SlotClaimMap | A0-A7 active assignment 不得重复、超出 8 个 active slot，required slot 不得缺失；同一板最多 16 个 candidate。 | 拒绝 activation，生成 conflict/overflow/missing evidence。 |
| DeploymentGate | EventLink、DataLink、RealtimeCapabilityContract、quality 和 required node 必须满足 RUN 前门禁。 | 可留在 staged validated，但不得进入 active。 |
| Command ACK | 跨节点 activation 需要 command slot 全环 ACK/NACK 或 fence 完成。 | required 节点 NACK/timeout 时回滚。 |

切换规则：

- active 切换只能更新 RefMemAO 管理的 table image descriptor、`table_seq`、active CRC bundle 和 snapshot 可见版本；不得在 RUN 中就地改业务正在读取的数据结构。
- 向量表不承载完整 table image 数据，只承载 active/staging/rollbackable 的状态、CRC、path hash、version、seq、quality 和 evidence 摘要。
- AO/FB 或维护命令读取 active/staging/rollbackable table payload 时必须使用 TableRegistry access/release；未释放 reader view 会阻止 activation，避免表指针悬挂。
- owner validation callback 由 AO/FB owner 实现，RefMemAO 调度并汇总结果；`RefMemSlotContract` 是 RefMemAO 内部派生视图，不作为业务层独立 API。
- activation 失败必须保持旧 `active_image` 不变；若已进入 `ACTIVATING` 后失败，必须用 `rollbackable_image` 恢复 active descriptor，并记录失败阶段、table id、owner id、reason 和 evidence。
- activation 成功后旧 active 进入 `rollbackable_image`，直到下一次成功 activation 或显式清除；上位机可通过 `SYSTem:REFMEM:TABle?` 和 load status 查询证据。

#### RefMemSyncProtocol

`RefMemSyncProtocol` 是 RefMem 的总线无关同步约束层。架构上它只规定共同事实必须通过 delta、epoch、command、ACK/NACK、fence 和 quality 形成闭环；具体帧格式、首版 PIO SPI adapter、后续 BISS-C/RJ45/UART/RS485 adapter 迁移策略放在 `REFMEM_SYNC_ARCHITECTURE.md` 中维护。

```text
DistributedRefMemAO
  -> RefMemSyncProtocol
  -> TransportAdapter
  -> PhysicalBus
```

协议层的架构级帧类别如下，详细字段、CRC 布局和 payload 定义由 `REFMEM_SYNC_ARCHITECTURE.md` 维护：

| 帧类别 | 架构作用 | 约束 |
|---|---|---|
| `HELLO` | 节点发现、能力摘要、adapter 能力和 active CRC 交换。 | 只建立同步上下文，不直接 claim slot。 |
| `EPOCH` | 对齐 RefMem epoch、run、table seq 和 CRC bundle。 | epoch 不一致时不得提交 active fact。 |
| `DELTA` | 发布小型共同事实变化。 | 只能引用 `RefMemSlotContract` 白名单字段。 |
| `COMMAND` | 同步命令槽意图。 | 写命令 accepted 不等于动作完成。 |
| `ACK_NACK` | 回写 take、busy、ACK、NACK、timeout 和 reason。 | 结果必须能映射到 evidence。 |
| `FENCE` | 在 RUN gate、SYNC epoch 或配置激活前收束可见性。 | required 节点必须到达 snapshot visible。 |
| `QUALITY` | 发布 stale、CRC、drop、late、timeout 和统计质量。 | 质量事实进入 DeploymentGate 和 Report。 |

Transport adapter 的架构级约束如下：

| Adapter 类别 | 首版定位 | 架构边界 |
|---|---|---|
| PIO SPI | 首版两板 HIL 的最小同步承载。 | 用于快速验证协议闭环，不作为最终唯一通讯方案。 |
| BISS-C | 后续复杂实时 codec 承载。 | 只作为 adapter / 类 IP 核能力，不改变 RefMem 协议语义。 |
| RJ45_SYNC_RING | 后续环路同步承载。 | 只承载协议帧和可选 timestamp，不直接暴露远端内存。 |
| UART / RS485 | 维护、低速或扩展节点承载。 | 可参与 RefMem Sync，但必须服从同一 CRC、seq、fence 和 quality 规则。 |
| USB debug | 调试和验证承载。 | 不作为产品 RUN 硬实时路径依据。 |

Transport adapter 在架构中不是“驱动细节”，而是 RefMem 跨平台、跨总线能力的隔离边界。它必须暴露统一能力模型，让 DeploymentGate 能判断当前链路是否足以承载某个 active profile：

| Adapter 能力 | 架构含义 | Gate/Quality 用途 |
|---|---|---|
| `adapter_id` | 当前承载实现，例如 PIO SPI、BISS-C、UART 或 RS485。 | 报告和 profile 兼容检查。 |
| `capability_mask` | 是否支持 timestamp、双向、ACK、CRC offload、重传、半双工方向控制等。 | 判断能否承载 `DELTA`、`COMMAND`、`FENCE` 或 VDC timestamp 输入。 |
| `mtu/payload_limit` | 单帧可承载的 payload 上限。 | 决定 delta compact 策略和是否拒绝大字段同步。 |
| `latency_class` | 预期延迟等级，不等同于实时保证。 | RUN gate、timeout 默认值和 quality 门限输入。 |
| `timestamp_source` | 无 timestamp、接收 tick、PIO capture、外部同步 timestamp。 | 给 VDC/quality 提供样本来源标识。 |
| `error_counter` | CRC、drop、overrun、timeout、direction conflict。 | 进入 `DistributedConnectionQualityTable`。 |
| `active_path_role` | active、standby、debug、report-only。 | 多 adapter 共存时防止同一 slot mirror 多 writer。 |

adapter 的替换原则：

- 协议帧语义不随 adapter 改变；迁移总线不能改 `DELTA/EPOCH/COMMAND/FENCE/QUALITY` 的含义。
- adapter 可以优化编码、时钟、方向控制和 timestamp 获取，但不能绕过 `RefMemSlotContract`。
- adapter 的链路错误必须转成统一 quality/evidence，而不是返回物理层私有错误给业务域。
- 多 adapter 并存时，只允许一个 active sync path 写同一 mirror；其他路径只能作为 standby、diagnostic 或 report-only。
- PIO SPI 首版 adapter 的价值是降低 bring-up 复杂度，先验证协议和共同事实闭环；BISS-C 价值是后续承载更复杂的实时 codec 和同步链路，但它不能反向污染 RefMem 协议层。

同步协议可借鉴的参考机制如下：

| 参考来源 | 借鉴内容 | RefMem Sync 落点 |
|---|---|---|
| OpenSHMEM / MPI RMA | origin/target completion、atomic、fence、quiet。 | delta completion、RMA window 白名单和 fence 可见性门禁。 |
| NASA cFS Table Services | active/staging 表、CRC、owner validation。 | epoch/CRC bundle、table image 激活前一致性检查。 |
| IEC 61499 | event/data connection 和部署一致性。 | command/event/data link 与 DeploymentGate。 |
| LinuxPTP / Chrony / EtherCAT DC | timestamp、delay、lock quality 的质量表达。 | 仅作为 VDC/quality 输入参考，VDC 算法仍由 VDC Domain owner 管理。 |

架构上把 RefMem Sync 拆成 protocol 和 adapter，是为了保证四个不变量：

| 不变量 | 含义 | 破坏后果 |
|---|---|---|
| 共同事实不变量 | 所有节点看到的 active fact 必须来自同一套 epoch、run、layout、CRC 和 owner 规则。 | 同一条测试序列在不同板上含义不同，RUN gate 无法可信。 |
| 写入权限不变量 | 任何跨节点写入都必须经过 slot/field contract、owner validation 和 snapshot policy。 | 通讯中断、重发或误码可能直接污染 active 表。 |
| 完成语义不变量 | 发送成功、接收成功、校验成功、提交成功和对外可见必须分层表达。 | 上位机或业务域会把“发出去了”误判成“系统已经一致”。 |
| 质量证据不变量 | stale、drop、CRC、late、timeout、NACK 必须进入同一质量和 evidence 模型。 | 故障只表现为动作不一致，无法定位是链路、节点、epoch 还是权限问题。 |

因此，RefMem Sync 的核心不是“用哪种线通信”，而是“共同事实什么时候被允许成为共同事实”。PIO SPI、BISS-C、RJ45、UART 或 RS485 都只能改变帧如何到达，不能改变 fact 如何被接收、校验、提交和门禁。

在 HAOFV 中，RefMem Sync 处于 AO/FB 之间的事实层，而不是事件执行层：

```text
Domain AO/FB publishes intent or fact
  -> DistributedRefMemAO validates owner / contract / epoch
  -> RefMemSyncProtocol publishes delta / command / quality
  -> peer DistributedRefMemAO commits snapshot
  -> peer Domain AO/FB consumes snapshot or event
```

这个方向保证了 AO/FB 仍然是行为 owner，RefMem 只是共同事实 owner。链路控制节点、脉冲分发节点、仪表控制节点或 BISS-C codec 节点都不能因为自己掌握某个 adapter，就绕过 RefMemAO 直接改其他节点的 active fact。

RefMem Sync 和 VDC 的关系也必须保持分层：

| 层 | 负责内容 | 不负责内容 |
|---|---|---|
| TransportAdapter | 提供帧收发、链路计数和可选硬件 timestamp。 | 不计算 offset/rate，不决定共同时间。 |
| RefMemSyncProtocol | 使用 epoch、seq、CRC、fence、quality 组织事实同步。 | 不执行 DPLL，不修正链路 delay。 |
| VDC Domain | 根据 timestamp、校准 delay 和 DPLL 形成共同时间。 | 不定义 RefMem slot 同步帧。 |
| DeploymentGate | 聚合 RefMem 一致性和 VDC 质量，决定能否 RUN。 | 不替代具体 AO/FB 的业务状态机。 |

这样后续两板最小验证可以先用 PIO SPI adapter，产品化版本可以迁移到 BISS-C 或其他总线；但上位机看到的 RefMem 状态、ACK/NACK、fence、quality 和 evidence 解释保持一致。

架构约束：

- RefMem 不绑定具体通讯总线；BISS-C、PIO SPI、RJ45、UART、RS485 都只能作为 transport adapter。
- adapter 可以变化，RefMem 的共同事实语义、owner、CRC、epoch、stale、fence 和 evidence 规则不能变化。
- adapter 不得直接修改 active fact，只能把完整协议帧交给 `DistributedRefMemAO` 校验和提交。
- 协议帧不得承载 OTA、Storage、Report 或波形大 payload；RefMem 只同步小型共同事实、命令、质量和摘要。
- VDC timestamp 可以由 adapter 提供输入样本，但共同时间算法仍由 VDC Domain owner 负责。

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
  -> adapter_queued
  -> transport_sent
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
| Data connection | 静态 `DistributedDataLinkTable`，把状态、参数、质量、时间戳、T2 和统计量映射到固定 region 字段。 | 任意远程变量读写。 |
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
| `node_id` | A0-A7 通用插槽号。 | 只允许 0-7；字段名沿用 node_id 是为了匹配同步协议和 NodeRegion[8]。 |
| `node_uuid` | 兼容字段或默认 profile 提示。 | 不得用于把物理板 UUID 绑定到固定 A slot；物理身份以 BoardCapability 为准。 |
| `capability_mask` | 通用 slot 基础能力或 profile 上限。 | 每个可参与系统的物理节点必须具备 board、RefMem、VDC、TDMA 基础能力；具体可选能力来自 BoardCapability 和 RealtimeCapabilityContract。 |
| `claim_policy` | 逻辑插槽 claim 策略。 | `STRICT_UUID`、`ALLOW_SAME_BOARD_MULTI_SLOT`、`SPARE_DYNAMIC`、`DISABLED`。 |
| `claim_priority` | 冲突仲裁优先级。 | 只用于诊断和预案选择；不得静默抢占已 active 的必需插槽。 |
| `default_persona_mask` | 节点默认人格能力。 | 只作为装载约束输入，不等于 active role。 |
| `hw_profile_crc` | 兼容字段或默认 profile 提示。 | 不得作为 A slot 硬绑定依据；硬件约束摘要以 BoardCapability / System Pack 为准。 |
| `online_required` | 节点是否为当前 profile 必需。 | 必需节点 stale 或 missing 时禁止 RUN。 |
| `fail_policy` | 节点失效策略。 | `STOP`、`HOLDOVER`、`DEGRADE`、`REPORT_ONLY`。 |

规则：

- A0-A7 是唯一固定插槽空间。
- GenericNode 只描述通用插槽基座、claim policy、required/fail policy 和基础约束，不直接声明物理板绑定或业务 role/persona 实例。
- GenericNode 的能力不能从当前装载实例反推；它必须来自 board profile、硬件约束或 System Pack 中的硬件 profile。
- `REFMEM + VDC + TDMA` 是每个物理节点参与分布式系统的最小基础能力。RefMem 提供共同事实，VDC 提供共同时间，TDMA 提供确定性通讯与反馈环路；任一基础能力缺失都不能进入 distributed RUN。
- `VDC` 基础能力表示节点能接收、校验、使用并发布虚拟 DC 相关 snapshot；`VDC_DPLL` 类 IP 核表示某个实例负责运行 DPLL/虚拟环路 owner，两者不能混为一谈。
- `capability_mask` 是装载上限，不是当前业务角色。`gateway`、`model_vna`、`model_turntable`、`pulse_distributor` 等 role 只允许出现在 NodeLoadTable。
- 脉冲分发、链路切换、仪表控制、gateway、model_vna、model_turntable、test_agent 等都由 `DistributedNodeLoadTable` 装载，不扩展固定节点数量。

### BoardCapabilityTable

`BoardCapabilityTable` 描述当前 profile 中可见物理板、模型板或调试节点的基础能力。它回答“B0-B7 这块物理或模型节点能做什么”，不回答“它当前占用哪个 A0-A7 slot”，也不直接表达业务装载关系。当前产品 wire contract 固定 8 条 board capability 记录；后续单板 9 到 16 个候选节点的反向验证属于 `SlotClaimProposal` / `SlotClaimEvidence`，不得扩展为第 9 个 BoardCapability active slot。

| 字段 | 含义 | 约束 |
|---|---|---|
| `board_id` | B0-Bx 物理/实例标签。 | 只作为 profile 内部标签，不能当作 RefMem slot id。 |
| `board_uuid` | 物理板或模型节点身份。 | 用于 claim 稳定身份、冲突诊断和质量追踪，不绑定固定 A slot。 |
| `capability_mask` | 板级基础能力上限。 | 必须包含 `BOARD + REFMEM + VDC + TDMA`；可选 PIO、DMA、USB、SD、core1_rt、link_control、BISS-C 等。 |
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
- 当前 BoardCapabilityTable 与 A0-A7 active slot 容量保持 8 条固定 wire payload；16 个候选上限只用于 SlotClaim proposal 和 overflow evidence，不参与 BoardCapability table CRC。
- `B2.LinkSwitcherAO` 默认可以装载到 A2，但只要 capability、IO 约束、类 IP 核、事件和数据连接通过，也可以装载到其他 A0-A7 slot。
- 没有 `REFMEM + VDC + TDMA` baseline 的物理节点不得进入 distributed RUN；缺少 `VDC_DPLL` 只影响 DPLL owner 候选，缺少具体 adapter 资源则由 TDMA DeploymentGate 拒绝该 active ring profile。
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
| `board_uuid` | 当前 claim 的物理或模型节点身份。 |
| `loaded_instance_mask` | NodeLoadTable 中加载到该 slot 的实例摘要。 |
| `last_claim_seq` | 最近 claim 序号。 |
| `evidence_index` | 冲突、缺失 UUID、overflow、stale 或 owner validation 证据。 |

`SlotClaimMap` 只记录 resolved active assignment；未分配候选进入 `SlotClaimEvidence` 或质量表，不得伪装成 `NodeRegion[8]` 之外的新 active slot。首版代码已落地 `refmem_slot_claim.h/.c`，从当前 active default profile 派生本地 `SlotClaimMap`，并可通过 `SYSTem:REFMEM:CLAIM? [slot_id]` 查询 map 和指定 slot assignment。当前尚未接 RJ45 `CLAIM_*` 协调消息，`claim_epoch=1` 表示本地派生 epoch。

#### SlotClaim 自组网协调

纠错不只是报错，也需要支持板卡自组网协调。协调机制仍必须受静态模型和 HAOFV owner 约束：节点可以提出候选 claim 和释放意图，但最终 active claim 只能由 `DistributedRefMemAO` 在同一 `claim_epoch` 下提交。通用插槽优先尝试协调分配；只有没有满足约束的空闲插槽、实例装载超过 profile 上限、required slot 缺失或 owner validation 拒绝时，才进入 `OVERFLOW`、`MISMATCH` 或 `CLAIM_FAULT`。

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
| `CLAIM_CONFLICT` | RefMemAO -> ring | 广播重复 claim、缺失稳定 UUID、stale、slot overflow 或 owner validation 拒绝证据。 |
| `CLAIM_RELEASE` | board -> ring | 声明释放某 slot claim，带 seq/epoch 防止旧释放误伤。 |
| `CLAIM_RESOLVE` | RefMemAO -> ring | 发布协调结果，包括 active owner、disabled claim 和 evidence。 |
| `CLAIM_COMMIT` | RefMemAO -> all | 进入新 `claim_epoch`，同步 active SlotClaimMap CRC。 |

协调规则：

- 同一 `physical_board_uuid` 可以 claim 多个不同 `slot_id`，前提是每个 slot 的 `claim_policy`、`capability_mask`、resource/IO claim 和 NodeLoadTable 共存检查都通过。
- 同一 `slot_id` 若被多个不同 `physical_board_uuid` claim，状态先进入 `CLAIM_CONFLICT`，随后进入 `RESOLVE_PLAN`。若存在满足 capability/resource/IO/owner 约束的空闲通用插槽，可以生成迁移计划；若无可用插槽或实例数量超过 `NodeLoadTable`/profile 上限，进入 `OVERFLOW/CLAIM_FAULT`。
- 同一 `physical_board_uuid` 最多可以提出 16 个候选节点实例，但 `DistributedRefMemAO` 在同一 active profile / epoch 中最多只能提交 8 个 active slot assignment。第 9 到第 16 个未分配候选只能作为 `OVERFLOW` evidence，不得生成隐式 slot；超过 16 个候选必须作为格式错误拒绝。
- `STRICT_UUID` 只要求候选具备稳定 `board_uuid`，便于 claim epoch、release/commit 和质量追踪；它不表示固定 A slot。
- `SPARE_DYNAMIC` 只允许用于非 required optional slot，并且必须在 ConfigGate 激活后形成新的 active CRC bundle，不能在 RUN 中热抢占。
- `claim_priority` 用于在多个候选物理板或多个可用插槽之间选择协调计划；协调目标是全环 A0-A7 地址唯一、required slot 满足、资源/IO/owner 不冲突。`STRICT_UUID` 只要求候选具备稳定物理 UUID，不表示某个 UUID 必须绑定某个 A slot。
- B0-B4 只表示当前项目或默认 profile 中的物理/实例标签，不是 slot id。`B2.LinkSwitcherAO` 可在默认 profile 中加载到 slot A2，也可以在后续 profile 或自组网协调中加载到任何满足 capability、IO 约束和类 IP 核要求的 A0-A7 slot。
- 对 `ALLOW_SAME_BOARD_MULTI_SLOT`、`SPARE_DYNAMIC` 或 `REPORT_ONLY` slot，`DistributedRefMemAO` 可根据 capability、claim_priority、physical uuid、link quality、load_order 和空闲插槽集合生成自动协调结果，但必须提升 `claim_epoch`、更新 `SlotClaimMap CRC`，并广播 `CLAIM_COMMIT`。
- 冲突恢复可以通过重新加载 System Pack / profile、清除错误板卡 claim、执行受权限保护的维护命令，或对非 required dynamic slot 执行自组网协调完成；不得由节点本地自行改 `node_id` 并直接进入 active。
- `DeploymentGate.node_check` 必须检查 `SlotClaimMap`：required 实例必须有一个 resolved active slot，spare slot 可 `UNCLAIMED`，任何未解决的 `CLAIM_CONFLICT/MISMATCH/STALE/OVERFLOW/CLAIM_FAULT` required 实例都拒绝 RUN。首版代码已把本地派生 claim gate 接入 RefMem model validation 和 `system_manager` config RUN gate；RJ45 协调后的跨板 claim gate 后续接入。
- RJ45_SYNC_RING 同步 `SlotClaim` 摘要时，必须带 `claim_epoch` 和 CRC，旧 epoch claim 不得覆盖 active claim。
- 后续两块最小系统板组网验证以 B0/B1 两个物理板为最小闭环：每块板都必须先具备 `REFMEM + VDC + TDMA` baseline，再通过 `CLAIM_HELLO/PROPOSE/RESOLVE/COMMIT` 形成同一份 `SlotClaimMap CRC`。验证重点不是业务触发序列，而是确认重复 slot claim、缺失 UUID、stale、overflow、owner validation 拒绝和 required missing 都能被 RefMem gate 拒绝或进入可诊断协调路径。

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
| `state_region_ref` | 状态事实位置。 | 指向 Vector 固定 region 字段。 |
| `health_region_ref` | 健康事实位置。 | 指向质量或故障 region 字段。 |
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
| `evidence_region_ref` | 证据 region。 | timeout、NACK、late 需要可回溯。 |

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
| `region_path` | Vector 字段路径。 | 例如 `LoopRegion.active_sequence_crc`。 |
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
| `crc_region_ref` | CRC 范围。 | 字段、region、directory 或 package。 |
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
| 发布策略 | `snapshot_policy`、`sync_policy`、`crc_region_ref` | 定义直读、seqlock、双缓冲、是否跨节点 delta 同步和 CRC 范围。 |
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
| `layout_check` | `layout_version`、region offset、region size、directory CRC。 | 拒绝 RUN。 |
| `node_check` | 必需 A0-A7 插槽 online、SlotClaimMap resolved assignment、node_uuid、hardware profile、capability 和装载实例匹配。 | 拒绝 RUN 或按 fail_policy 降级。 |
| `instance_check` | required AO/FB instance 存在、版本兼容、enable 条件满足。 | 拒绝 RUN。 |
| `resource_check` | Flash、SD、USB、PIO、DMA、core1、RJ45 等资源无冲突。 | 拒绝冲突实例组合。 |
| `io_check` | SMA/RJ45/link-control resources/BiSS-C/UART/RS485 等 IO claim 无冲突；当前项目实例可映射为 SP8T/SP2T。 | 拒绝 RUN 或拒绝实例启用。 |
| `writer_check` | 每个 region 字段只有唯一 writer。 | 拒绝 RUN。 |
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
reject_region_ref
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
| `scope` | 质量范围。 | `NODE`、`RJ45_LINK`、`SLOT`、`EVENT_LINK`、`DATA_LINK`、`TRANSPORT_ADAPTER`。 |
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
| `evidence_index` | 证据索引。 | 指向 FaultEvidenceRegion 或 SD 日志。 |

运行态质量表由 `RefMemAO`/`DistributedRefMemAO` 从各 owner 的只读 snapshot 派生，不热写 active static `DistributedConnectionQualityTable`。本地物理链路至少包含两个 `TRANSPORT_ADAPTER` 派生 entry：

| Runtime entry | 来源 | 映射规则 |
|---|---|---|
| Local adapter | PIO SPI adapter snapshot + RefMem Sync quality counter | CRC、stale、drop、timeout、last_error 和 latency class 反映线级 bring-up/诊断质量。 |
| TDMA service | core1 realtime TDMA snapshot | `timeout_count` 表示 TDMA window timeout；`late_count` 表示 intent 被拒绝或错过窗口；`drop_count` 表示 DMA overrun/physical execution error；`last_error` 保留 physical adapter 最近错误；`seq_expected/seq_last` 反映 intent/completed 序列。 |

SCPI 只能通过 `SYSTem:REFMEM:QUALity? [index]` 读取该派生视图，不能直接修改 quality entry。TDMA frame payload 不进入向量表；向量表和 quality table 只承载 seq、CRC、error、timeout 和 evidence 摘要。

`DeploymentGate.QUALITY` 不能直接遍历底层 adapter 或 core1 mailbox；它必须调用 `refmem_quality_evaluate_deployment_gate()` 消费 runtime quality table。该 evaluator 只输出 gate 状态、reject reason、node/slot 和 evidence index，不改变 active fact。`SystemManager` 通过 `DistributedRefMemAO` 的 quality gate 结果更新 config RUN gate；它不拥有 TDMA 或 adapter 计数。

## 核心数据面

### REFMEM-LAYOUT-01：静态向量布局与部署版本匹配

当前容量和版本由 `distributed_refmem.h` 的 `DISTRIBUTED_REFMEM_TABLE_SIZE`、
`DISTRIBUTED_REFMEM_LAYOUT_VERSION` 定义；当前 offset 与成员尺寸由
`refmem_vector_table_t` 定义，目录使用真实 `offsetof`/`sizeof` 生成。
本契约登记为 pending，实现和硬件证据由 `REFMEM-RAM-001` 至 `REFMEM-RAM-004` 承接。

- 保留 `refmem_vector_region_id_t` 的 ID 和顺序、现有字段语义及唯一 writer；
  仅调整静态预留容量，不改变有效字段语义。整个表仍静态常驻、整表清零，VDC/DPLL 的 payload、
  对齐和 seqlock 不变，不引入 lease、动态分配、MPU 或 RefMem 分片前置。
- 总尺寸必须等于 `DISTRIBUTED_REFMEM_TABLE_SIZE`，目录连续覆盖全表；各区尺寸
  使用对应 `DISTRIBUTED_REFMEM_*_SIZE`。节点区为
  `DISTRIBUTED_REFMEM_NODE_COUNT * DISTRIBUTED_REFMEM_NODE_SLOT_SIZE`；逻辑槽数
  与步长分别受控，不随 TDMA 物理节点配置隐式改变。
- 任一 region offset、size、顺序或 node stride 改变均须提升布局版本；静态部署包
  ApplicationMap 必须匹配当前布局，由 `refmem_application_contract_validate_application_map()`
  在 owner validation 拒绝旧版本。CRC 正确不能绕过版本核验，host 产包器同步版本。
- 旧包拒绝不能覆盖 active 或 rollback image；staging 保持既有失败/清理语义，
  不承诺保留失败候选。本轮不提供自动旧包转换，也不把本地包门禁外推为已证明
  混合旧固件节点的全网准入。RMTP 格式、VDC/DPLL payload 和 TDMA wire 独立版本不随之改变。
- 普通字节数组的分配不等于对应业务事实已实现；新增字段必须核对实际区域边界。
  TLV 的预留不新增 writer、写权限或协商机制。SCPI 状态字段顺序保持，布局与容量
  字段报告当前符号值，host 验收不得继续假定历史版本。

### 历史首版布局快照（非当前事实源）

以下保留首版布局用于旧镜像识别，不用于当前代码寻址或 RAM 核算；当前值以上述
类型、符号和实际链接 map 为准：

Vector directory 的 16 项是固定内存定义区（region），不是 A0-A7 可实例化节点槽位，也不是 SlotClaim 的候选槽。节点实例化只能通过 A0-A7 逻辑 slot / NodeLoad / SlotClaimMap 进入系统；Vector region 只提供事实存放位置、owner 边界和 CRC/seq/stale 保护。

| Region | Offset | Size | 内容 | 写入者 |
|---|---:|---:|---|---|
| Header/Directory | `0x0000` | 1 KB | magic、layout、region offset、table_seq、epoch、crc32 | RefMem Domain |
| SystemRegion | `0x0400` | 1 KB | system_mode、role_map_version、run_id、fault_latch、release gate | SystemAO |
| Role/ConfigRegion | `0x0800` | 2 KB | NodeRoleMap、hw_profile、persona、feature mask | SystemAO / config loader |
| VdcRegion | `0x1000` | 2 KB | sync_id、offset、rate、lock_state、holdover、relock、`e_vdc` | VdcSyncAO |
| LoopRegion | `0x1800` | 4 KB | trigger param、angle sweep/breakpoint、active sequence、scan_index | LoopEngineAO |
| DpllRegion | `0x2800` | 2 KB | compare 捕获、角度预测、`T_fire_base`、`e_pll` | AngleDpll owner |
| NodeRegion[8] | `0x3000` | 4 KB | A0-A7 通用插槽的 node_id、装载摘要、heartbeat、local_state、error_code、stale_count | 各节点 owner |
| TriggerRegion[8] | `0x4000` | 8 KB | armed、last_fire_seq、late_count、t2_count、ready_timeout | 各节点 core1 摘要 |
| IoRegion[8] | `0x6000` | 8 KB | SMA/RJ45/BiSS IO 镜像、边沿计数、健康状态 | 各节点 IO owner |
| CalibrationRegion | `0x8000` | 8 KB | link table、delay table、staging/active/version/quality | CalibrationAO |
| StatisticsRegion | `0xA000` | 8 KB | `e_vdc/e_act/e_pll`、CRC/seq/late 分布、p99/p999 | Statistics / Measure owner |
| AckCommandRegion | `0xC000` | 4 KB | command_seq、ack/nack/busy/timeout 位图、原子命令槽 | 命令 owner + 节点 ACK |
| FaultEvidenceRegion | `0xD000` | 6 KB | fault_code、source_node、epoch、run_id、关键证据 | SystemAO / DiagnosticsAO |
| GatewayRegion | `0xE800` | 2 KB | A3/VNA/host 状态、采集状态 | GatewayAO |
| OtaStorageUiRegion | `0xF000` | 2 KB | OTA、Storage、UI 摘要 | 对应 task owner |
| TlvExtension | `0xF800` | 2 KB | versioned TLV、未来扩展 | owner by type |

历史快照的表尾为 `0x10000`、总大小为 64 KB；新旧布局不得按相同偏移解释。
当前版本拒绝及兼容边界以 `REFMEM-LAYOUT-01` 为准。

### Header 与 Directory 契约

Header/Directory 是 RefMem 的自描述入口。它必须至少提供：

| 字段 | 作用 | 规则 |
|---|---|---|
| `magic/end_magic` | 表识别和越界破坏检测。 | 初始化和 snapshot 时都必须校验。 |
| `layout_version` | 静态表布局版本。 | 匹配 `DISTRIBUTED_REFMEM_LAYOUT_VERSION`；布局变化必须递增。 |
| `table_size` | 总表大小。 | 匹配 `DISTRIBUTED_REFMEM_TABLE_SIZE`。 |
| `table_seq` | 全表事实序号。 | 任意 region active fact 更新后递增。 |
| `epoch_id` | 系统事实纪元。 | 复位、System Pack 切换、RUN 批次切换或重大恢复后递增。 |
| `run_id` | 当前运行批次。 | 把配置、同步、T2、故障和报告绑定到同一批次。 |
| `region_count` | directory 项数量。 | 匹配 `REFMEM_VECTOR_REGION_COUNT`。 |
| `region_directory[]` | region id、offset、size、owner、flags、crc。 | RUN 前必须校验 offset/size 不重叠且覆盖当前表；扩展字段属于下述产品化建议。 |
| `directory_crc32` | directory 自身 CRC。 | 防止 region map 半更新。 |
| `header_crc32` | header CRC。 | 不包含 `header_crc32` 字段自身。 |
| `compat_min_version` | 最低兼容 layout。 | 节点低于该版本时拒绝加入 RUN。 |

Directory 项建议结构：

```text
region_id
offset
size
owner_domain
owner_node_mask
writer_instance
flags
region_crc32
region_seq
stale_window_us
```

### Region Guard 契约

每个 region 的首部应预留统一 guard。P0 代码只有 header/node 的轻量 guard 语义，产品化版本需要推广到全部 region。

```text
region_seq
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

- `region_seq` 在本 region 完整发布后递增。
- `crc32` 覆盖 guard 后的有效 payload，不能覆盖自身。
- `stale_state` 使用 `OK/STALE/MISSING/INVALID/FAULT`。
- `owner_domain` 和 `writer_instance` 必须能在 `DistributedDataLinkTable` 中找到唯一来源。
- 非 owner 不能直接写 region；跨域写入必须走 command/config staging。

### Owner 与写权限

RefMem 的写权限按 region、字段和节点共同约束：

| 写入范围 | 允许 writer | 禁止事项 |
|---|---|---|
| Header/Directory | RefMem Domain | 业务域直接改 layout、offset、region_count。 |
| SystemRegion | SystemAO / ConfigGate | SCPI callback 直接写 active state。 |
| Role/ConfigRegion | SystemAO / config loader | 运行中无 gate 改 role/persona。 |
| VdcRegion | VdcSyncAO / SyncDpllFB | Angle DPLL 写 VDC offset/rate。 |
| LoopRegion | LoopEngineAO | TriggerAO 或 SCPI 直接改 active sequence。 |
| DpllRegion | AngleDpll owner | SYNC DPLL 写 `T_fire_base`。 |
| NodeRegion[n] | 节点 n owner / RefMem Sync 接收镜像 | 节点 A 写节点 B 的本地 owner 字段。 |
| TriggerRegion[n] | 节点 n core1 realtime 摘要 / core0 合并 owner | SCPI 临时读取时触发现场 IO。 |
| IoRegion[n] | 节点 n IO owner | 业务域绕过 IO owner 直接改 IO 镜像。 |
| CalibrationRegion | CalibrationAO | SYNC 或 TRIG 私自修正 delay table。 |
| StatisticsRegion | MEASure / Statistics owner | 业务配置写统计结果。 |
| AckCommandRegion | command owner + target node ACK writer | 在执行动作临界区内做耗时工作。 |
| FaultEvidenceRegion | SystemAO / DiagnosticsAO | 覆盖未落盘 evidence。 |
| GatewayRegion | GatewayAO | 普通节点假冒上位机网关状态。 |
| OtaStorageUiRegion | OTA / Storage / UI owner | core1 实时侧直接落盘或改 UI 状态。 |
| TlvExtension | owner by TLV type | 未注册 TLV type 写入。 |

### Command / ACK / NACK 契约

`AckCommandRegion` 是跨域、跨核和跨节点命令闭环的数据面。它不执行业务动作，只保存命令意图、目标节点、payload 引用、ACK/NACK/busy/timeout 位图和可回溯证据。

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

`AckCommandRegion` 首版字段建议：

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
| `evidence_index` | 证据索引。 | 指向 FaultEvidenceRegion 或 SD 日志。 |
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
| `NODE_LOAD_STAGE` | 提交 NodeLoad 候选到 RefMem staging。 | DistributedRefMemAO / 对应 AO/FB owner |
| `BOARD_CAPABILITY_STAGE` | 提交 BoardCapability 候选到 RefMem staging。 | DistributedRefMemAO |
| `TABLE_PACKAGE_STAGE` | 提交 `.rmtp` table package 校验摘要到 RefMem staging。 | DistributedRefMemAO |
| `EVENT_CONFIGURE` | 装载 EventPack 到 staging（只 stage，不激活）。 | EventPackService |
| `EVENT_VALIDATE` | 校验 staged EventPack（schema/CRC/map/alias/target policy/EVG slot）。 | EventPackService |
| `EVENT_ACTIVATE` | 激活已通过 validate 的 staged EventPack。 | EventPackService |
| `EVENT_INJECT` | 运行时注入控制事件（方案 B，R0 后启用）。 | EventRuntimeService（handler） |
| `EVENT_ABORT` | 运行时中止事件/会话（方案 B，R0 后启用）。 | EventRuntimeService（handler） |
| `TRIGGER_SESSION_CONFIGURE` | 配置系统级触发会话（source/target/grid/template CRC）。 | TriggerSession owner / T2 |
| `TRIGGER_SESSION_ARM` | target 本地 arm、source 准备游标/阈值。 | TriggerSession owner / T2 |
| `TRIGGER_SESSION_START` | source 启动 T2 发布，targets 进入 RUNNING。 | TriggerSession owner / T2 |
| `TRIGGER_SESSION_STOP` | 停止新位置并排空在途。 | TriggerSession owner / T2 |
| `TRIGGER_SESSION_ABORT` | 中止 session，保留已执行证据。 | TriggerSession owner / T2 |

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

`SYSTem:COMMand:ACK?` / `SYSTem:COMMand:NACK?` 是通用 command slot 维护视图，直接读取同一底层 `AckCommandRegion` snapshot 和 reason 表。`SYSTem:CONFigure:ACK?` / `SYSTem:CONFigure:NACK?` 只保留为配置门禁兼容视图，并且只映射 `CONFIG_ACTIVATE` command，不得把 `NODE_LOAD_STAGE`、`START`、`STOP` 等非配置激活命令混入配置 ACK 事实。

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

RefMem Header/SystemRegion 必须携带下面的版本束，用于 RUN gate 和报告闭环：

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

## 统一控制面（REFMEM-CTRL-01）

本节把“通过任意一个板卡接口操作 RefMem、实现所有板卡的操作”细化为可验证契约。结论先给出：RefMem 统一控制面提供的是**接入点无关的系统级命令面 + 本地快照观测面**；控制面客户端接入的是 RefMem，而不是某块板或某条独立控制总线。它不提供跨板裸写、同步远程读或绕过目标本地门禁的强制通道。

### 挂接关系：控制面接入 RefMem

统一控制面不是 RefMem 之外的第二套总线或独立服务。控制面客户端（SCPI/UI/System Pack/上位机工具）通过 RefMem 的三个既有面挂接：

| 挂接面 | 客户端动作 | RefMem 侧承载 | 不得做的事 |
|---|---|---|---|
| 命令面 | 提交 `SYSTem:*` intent | `AckCommandRegion` command slot + 命令目录 | 直接调用目标 owner、直接写目标事实 |
| 观测面 | 读取状态、聚合结果和证据 | 本地 snapshot、`control_reply`、`SYSTem:REFMEM:*` 视图 | 同步跨板查询、驱动现场 IO |
| 目录面 | 查询命令、能力和版本 | 命令目录、Version Bundle、`SYSTem:REFMEM:STATus?` | 用目录查询替代命令执行 |

挂接规则：

- 客户端只认 RefMem 的 `SYSTem:*` 契约，不认某块板的物理身份；接入哪块板只改变 `origin` 和本地权限。
- RefMem 是控制面的承载者（bearer）与路由者（router），不是执行者；执行仍归目标 owner。
- 控制面客户端不得绕过 RefMem 直接访问 TDMA/PIO/目标内存；RefMem 也不得绕过 owner 直接执行。
- 控制面接入不改变数据面：事实仍走 `REFMEM_DELTA`，命令走 `REFMEM_CMD`/`REFMEM_ACK`；两者共享 epoch、owner、CRC 和 quality，但不共享队列语义。
- 不新增第二套命令槽或 ACK 事实；所有控制面视图最终都回到同一个命令事实源（现行 `AckCommandRegion` 契约；当前 live slot 见“当前实现状态”）。

### 事件面挂接（Event Plane 静态子域）

RefMem 命令槽是命令事实的唯一 owner；Event Plane 是 RefMem 下的**静态基线子域**，方案 B 再增加 `CONTROL_EVENT` 运行时 handler。它不持有第二个命令槽、ACK 区域或 epoch。子域文档位于 `docs/refmem/event/`，canonical 为 `REFMEM_EVENT_ARCHITECTURE.md`。

```text
静态基线：
SYSTem:EVENT:CONFigure -> EVENT_CONFIGURE -> staged
SYSTem:EVENT:VALidate  -> EVENT_VALIDATE  -> validated/failed
SYSTem:EVENT:ACTivate  -> EVENT_ACTIVATE  -> active/rejected
  -> DistributedRefMemAO 唯一 take/ACK
  -> EventPackService 静态校验/激活
  -> EventVector 静态更新
  -> ACK/NACK 写回同一 AckCommandRegion

方案 B R0+：
SYSTem:EVENT:INJect/ABORt -> EVENT_INJECT/ABORT
  -> DistributedRefMemAO 唯一 take/ACK
  -> EventRuntimeService handler（CONTROL_EVENT only）
  -> EventStatus/EventLog
  -> result 回 RefMem
  -> POSITION 直接 NOT_MY_DOMAIN
```

- 静态阶段只启用 `EVENT_CONFIGURE`/`EVENT_VALIDATE`/`EVENT_ACTIVATE`；方案 B 的 R0 后才启用 `EVENT_INJECT`/`EVENT_ABORT`，且 command slot/ACK/lease 仍归 RefMem，`EventRuntimeService` 只做 CONTROL_EVENT handler；POSITION 永不归 Event Plane。
- 三步各自 ACK：`CONFIGURE`=staged、`VALIDATE`=validated/failed、`ACTIVATE`=active/rejected；Event Plane 只返回结果，ACK/NACK 由 `DistributedRefMemAO` 写回同一 `AckCommandRegion`。`epoch/run_id` 使用 RefMem 的值，不另建 epoch。
- `EventVector` 只存静态 pack/catalog/map/alias/EVG 席位；EventStatus/EventLog 是运行时状态，由 EventRuntimeService 写；TriggerRun 归 TRIGGER。
- 静态阶段运行期不归本域；方案 B 的 CONTROL_EVENT handler 只 post 给 owner，不执行、不接 POSITION。
- 首版 `EventPackService`/`EventRuntimeService` 运行在 `task_refmem_sync`，不新增任务；若独立 AO，需单独预算和顶层注册。

### REFMEM-TRANSPORT-01：REFMEM_CMD 运输

`REFMEM_CMD` 是单端口控制面的跨节点命令帧，不新增物理总线，也不新增第二命令槽。它走 **TDMA 二等席**（RefMem 数据/ACK/fence 所在的普通路径），不再使用 config-control，也不使用 raw SPI：

- `payload_subtype = REFMEM_CMD`；命令 envelope 与 `AckCommandRegion` 字段一致（command_seq/type/class/origin/target/required/payload/crc）。
- wire 尺寸/偏移/CRC 以 `refmem_cmd_codec.h` 的 `REFMEM_CMD_WIRE_SIZE`/`REFMEM_CMD_CRC_OFFSET` 为准；帧不内联 payload 数组。
- `message_class` 属于 TDMA 二等席的 RefMem 类；接收端按 subtype 分派，`REFMEM_CMD` 是唯一 consumer；未知 subtype 拒绝。
- 四板分发：`target_mask`/`required_mask` 使用 A0-A3 的显式位；`ring_node_count >= 4`；不得用隐式广播或魔法零值。
- active profile 必须包含二等席/RefMem 类；缺失时 `REFMEM_CMD` 返回 `TRANSPORT_UNAVAILABLE`，不静默降级。
- 席位预算：`SYSTem:TDMA:SEAT:ROTation <p>,<f>,<s>` 的 `s` 为二等席让位段；四板分发要求 `s>0`，且按 TDMA 实测规则 `f>=3`（避免 `7,2,1`）；不得拉长周期到影响一等席位置路径。
- 预算以 `tdma_traffic_class_profile_t` 的 `reserved_bytes_per_cycle`/`max_frames_per_cycle`/`queue_depth`/`deadline_ns` 为准；命令载荷走 `payload_kind`/`payload_ref`/`payload_size`/`payload_crc32`，大载荷用 staging/TLV ref。
- 优先级：TDMA 二等席；不占用 core1 硬实时窗口，不在 ISR 执行；二等席是普通路径预算，不承诺硬实时。
- ACK/fence 复用 `TDMA_PAYLOAD_CLASS_REFMEM_ACK_FENCE`；命令帧与 ACK 不共享队列语义。
- 不允许为 command 另开 payload class 或物理总线；只有二等席预算不足时，才另立契约和 TDMA 联审。
- 单端口入口只决定 `origin`；命令必须经 `DistributedRefMemAO` 唯一 command slot，再编码为 `REFMEM_CMD`。

### REFMEM-SESSION-01：系统级触发会话

单端口启动触发不能逐板 `T2:MOUNT`；用 RefMem 命令面的 session 统一：

| command_type | 语义 |
|---|---|
| `TRIGGER_SESSION_CONFIGURE` | `session_id/run_id/source_slot/target_mask/required_mask/grid/template CRC` 写入 staging |
| `TRIGGER_SESSION_ARM` | target 本地 arm；source 准备游标/阈值 |
| `TRIGGER_SESSION_START` | source 启动 T2 发布；targets 进入 RUNNING |
| `TRIGGER_SESSION_STOP` | 停止新位置，排空在途 |
| `TRIGGER_SESSION_ABORT` | 中止 session，保留已执行证据 |

状态机：

```text
IDLE -> CONFIGURED -> ARMING -> ARMED -> RUNNING -> STOPPING -> IDLE
                         \-> CONFIGURED（arm 失败，source 未启动）
RUNNING -> DEGRADED -> ABORTED / STOPPING
```

- `session_id` 由 coordinator 分配；`source_slot` 必须唯一且具备 COUNTER/VDC 能力（缺失由 DeploymentGate 拒绝）；`target_mask` 来自 active `SlotClaimMap`。
- `ControlCoordinator` 是 session 状态的唯一 writer；T2/target 只上报 `armed/running/stopped`，不写 session 状态；`origin` 只读聚合。
- 单板 `T2:MOUNT/UNMOUNT` 不创建系统 session；只有 `REFMEM-SESSION-01` 才写 coordinator 的 session 状态。
- `ARMING` 聚合 required targets 的 arm 结果和 source 准备结果：required 未全 arm 或 source 准备失败，不得进入 `ARMED`，也不得 `START`；失败时已 arm 的 target 必须 `DISARM`，source 不启动，session 回 `CONFIGURED` 或 `FAULT`。
- `START` 只允许从 `ARMED` 发起；若 source 已启动后 required target 失败，进入 `DEGRADED`，必须显式 `STOP/ABORT`，不静默 partial。
- `STOPPING` 必须有超时：`STOP_TIMEOUT -> DEGRADED/FAULT`，不得静默回 `IDLE`；排空证据写 EventLog。
- `START` 的 ACK 表示“source 已启动 + required targets 已 arm”，不表示位置已执行；执行证据仍看 T2 `published/frozen/executed`。
- `T2:MOUNT` 兼容模式 = `CONFIGURE + ARM + START` 一次性（仅旧 SCPI）；已 RUNNING 时幂等返回当前 session，不重复启动。新应用使用分步 session 命令；`T2:UNMOUNT` 映射 `STOP`。
- POSITION 的实时接收/执行仍归 TRIGGER；Event Plane 只提供静态映射，不接 session 实时状态。
- 契约状态：pending，代码锚点 `trigger_t2_runtime.h`；需 host + 两板 T2 证据。

### REFMEM-ACTION-01：动作分发

RefMem 命令/事件要落到 AO/FB，需要统一的动作分发契约：

- `ActionDispatcher` 是每板 `DistributedRefMemAO` 的 sub-service，运行在 `task_refmem_sync`；不是独立 AO/FB，不新增任务；只写 evidence，不写业务事实。
- `ActionRegistry`：每板静态表，`action_id → {owner_id, handler, required, version, crc}`；STOP 期装载，RUN 只读；`action_registry_crc` 进 Version Bundle。
- `CommandHandlerRegistry`：`command_type → handler`，覆盖 `START/STOP/CAL/LOAD` 等 generic command；与 `action_id` 表分开，避免只有事件路径。
- `ActionDispatcher`：校验 run-state/permission/deployment/freshness，然后 `post` 到 owner 的有界 mailbox；返回 `ACCEPTED/BUSY/REJECTED`，不直调 handler、不操作 PIO。
- mailbox 有界且带优先级；满时 `BUSY`/`REJECTED`，不阻塞、不丢 required；required 满时 `REJECTED(QUEUE_FULL)`，可选事件可进 evidence。
- required `action_id`/`command_type` 缺 handler 时，`DeploymentGate` 拒绝 RUN；optional 缺失进入 evidence；运行期收到未注册 `command_type` 直接 `REJECTED(UNHANDLED)`。
- handler 必须非阻塞；耗时动作返回 BUSY，由 owner service tick 推进。
- `action_id` 来自 Event Plane 的静态 EventMapTable；`command_type` 来自 RefMem 命令目录；Event Plane 只提供映射，不执行。
- 契约状态：pending，代码锚点 `distributed_refmem.h`；需 host 分发用例 + 单板 HIL。
- **实现状态（2026-11-05）：已被统一控制面取代。** 命令分发已由 `refmem_domain_config`
  （`command_type → handler`，32 项上限、词数校验、fail-closed）实现，并由 `refmem_command_catalog`
  与 `app_refmem_domain_config` 注册；`refmem_action_registry` 是第二套 `id → handler` 表，
  生产路径**零调用**（链接器 GC 丢弃其段），其 `action_id`（Event Plane）路径也未实现。
  结论：不再作为待接线路由，代码留档、不接入 RUN；将来若确需异步分发，应基于
  `refmem_domain_config` 扩展，而不是复活第二张表。

### REFMEM-LOCAL-01：单板控制保留

RefMem 统一面是**增量层**，不替换单板控制：

- 单板 SCPI、本地 command slot、本地 owner、本地 T2 `MOUNT/UNMOUNT` 继续可用；不要求 coordinator、lease、`REFMEM_CMD` 或 transport。
- `target_mask` 只含本地时走本地 execute，不编码 `REFMEM_CMD`；含远端时才走统一面；mask 解析失败直接拒绝，不回退本地。
- 统一面不可用（无 coordinator/transport/profile）时，本地命令仍可执行；**全目标/远端命令必须拒绝 `UNIFIED_UNAVAILABLE`，不得静默缩成本地单板执行**。
- 两条路径共享同一套本地门禁（permission/run-state/deployment/slot single-writer），统一面不得降低本地门禁。
- `REFMEM-SESSION-01` 是可选系统级会话；单板 `T2:MOUNT/UNMOUNT` 保留，只有需要 source+targets 时才用 session。
- Event Plane 静态包/运行时 handler 不成为单板控制的前置；EventPack 未激活时，本地基础命令仍可用，事件类命令按自身门禁拒绝。
- 契约状态：pending，代码锚点 `refmem_command.h`；需 host 本地路径 + 两板“本地仍可用”证据。

### 目标与不变量

| ID | 不变量 | 含义 |
|---|---|---|
| CP1 | 接入点无关 | 同一固件族的每个节点都暴露同一套 `SYSTem:*` 维护接口和同一份 `command_type` 目录；接入点只决定 `origin`、本地权限和回程路径，不改变命令含义。 |
| CP2 | 执行必在目标 | 命令只能由目标插槽的本地 owner 在其 service 上下文执行；发起节点不得写目标 active fact，不得直接驱动目标 IO、Flash 或实时边沿。 |
| CP3 | 既有门禁不降级 | 控制面不解锁 slot 单 writer、owner validation、staging/activation、RunState、ResourceArbiter、Flash lockout 或权限表。 |
| CP4 | 读只读本地快照 | 系统视图由本地 mirror 逐槽拼装，每行带 `epoch/seq/age/stale`；禁止同步跨板读，禁止把 stale mirror 当 current fact。 |
| CP5 | 目标显式 | `target_mask`/`required_mask` 必须由 active `SlotClaimMap` 显式解析并随命令记录；“全部板卡”是发令时解析出的掩码，不是隐式广播或魔法零值。 |
| CP6 | 单一协调者 | 跨节点/多目标系统命令在同一 epoch 内只有一个 `ControlCoordinator` 拥有命令发号、超时判定和完成聚合权；其他接入点只转发，不并行发号。单板本地维护命令（target 仅本地）走本地 command slot，不要求 coordinator，但不生成跨节点命令事实。 |
| CP7 | 至多一次有效执行 | 同一 `(coordinator_boot_id, command_seq, target)` 只执行一次；重传只返回既有终态，不重复动作。 |
| CP8 | 接受不等于完成 | SCPI 的 `OK`/`1` 只表示 envelope 被接受；完成状态只能来自 ACK/NACK/fence 聚合快照。 |
| CP9 | 单板控制保留 | 统一面是增量层，不替换本地 SCPI/slot/owner/T2；统一面不可用时本地仍可用，远端命令必须拒绝，不得静默缩成单板执行。 |

### REFMEM-READ-01：读 key 与 OBS 聚合读

读面是 CP4 的落地契约：**只读本地镜像、逐槽拼装、不做同步跨板读**。首版不做同步 `CTRL:READ?`
（D0 决策 2026-11-09 选 A）；若将来确需同步读，先修订 CP4 再立项，不得在客户端或页面层绕过。

**发布面（先于读面）。** 每 key 只有一个 writer，在 **core0 service 上下文**（或该 owner 所在核心的
发布点）写入本板镜像；core1/ISR 侧的状态必须经 core0 drain 发布，禁止直写。写入通道：
首轮全量（每 key 一次，走维护流），之后只发 **dirty key 增量**；单次增量载荷 ≤
`REFMEM_CMD` wire 上限，禁止整表刷新。镜像条目包含 `key_id/seq/epoch/last_update_ms/<schema>`；
`seq` 由该 key 的发布计数器单调递增，`epoch` 取该 key 关联的 plan/command generation（无则 0）。

**read key 表（v1，8 个）。** freshness 直接复用 `RefMemSlotContract` 的 `stale_window_us`
（`refmem_application_model.h`），不新增第二套 freshness 参数。

| key_id | name | 唯一 writer | 行 schema（固定字长） |
|---|---|---|---|
| 1 | `switch1` | sync_io sequence owner（core1 → core0 drain） | `value, desired` |
| 2 | `switch2` | sync_io sequence owner（同上） | `value, desired` |
| 3 | `trigger_state` | trigger AO | `state, run_state, epoch` |
| 4 | `guide_plan` | guide executor | `action, gen, status, trials_done, passed, failed, abort_site` |
| 5 | `boot_plan` | boot service | `action, fail_step, boot_done, half_ring, profile_source` |
| 6 | `ring_status` | tdma runtime owner | `enabled, up, down, adapter, nodes, local_slot` |
| 7 | `cal_status` | calibration manager | `persisted_valid, generation, reject_reason` |
| 8 | `node_health` | refmem node table（`NODES?` 的只读投影） | `state, heartbeat, stale_count, fault_code` |

（v1 不含 `fault_last`：diagnostics 只有 `s_fault_latched` 与文本日志，`SYST:FAULT:LAST?` 是存储
report，二者都不是可发布的故障码来源；补 writer 后再增 key。）

表以 `config/refmem/read_catalog.yaml` 为唯一源，由 `tools/refmem/refmem_read_catalog_gen.py` 生成
`refmem_read_catalog.h` 与 `refmem_read_catalog_table.c`；host 用例
`tests/unit/test_refmem_read_catalog.c`（`tools/tests/run_refmem_read_catalog_tests.ps1`）。

**存储预算（pilot 与全量分开）。** 单板 node slot 288 B、现用 232 B、尾部仅 56 B（`refmem_vector_table.h`），
**pilot 不放 node slot 尾部**。pilot 用一块独立 region（约 1 KiB，候选为当前零消费的 statistics 区，
region 语义需单独登记），只放 ≤4 个高频 key（建议 `switch1/switch2/guide_plan/ring_status`），
4 key × 8 槽 ≈ 864–928 B。全量 8 key 需要新 region + `layout_version` 递增（`REFTMEM-LAYOUT-01`），
不与 pilot 同批改。

**查询形态。** `SYSTem:REFMEM:OBS? <key>,<offset>,<count>`：
- `<key>` 为 key_id 数字（1..8）；
- `offset >= DISTRIBUTED_REFMEM_NODE_COUNT` → SCPI 错误（与 `NODES?` 一致，不返回空行）；
- `count` 裁剪到 `node_count - offset`，上限 `DISTRIBUTED_REFMEM_NODE_COUNT`；
- 每行固定 `slot,valid,epoch,seq,age_ms,stale,<schema 字...>`。

**行语义。** `valid=0` 行表示该槽从未发布：`age_ms = 0xFFFFFFFF`、`stale = 1`、其余值字段为 0；
`valid=1` 行 `stale = (age_ms > stale_window_us/1000)`。`age_ms` 是**读者本地视角**
（读者 tick − 写者 `last_update_ms`），跨板存在时基偏差，`stale` 判定只用同一读者的时基。

**读面与命令面的边界。** OBS 是**只读投影**，不携带 `target_mask/required_mask`，不产生命令事实，
也不返回 NACK；"required 缺位 → NACK" 只属于命令面语义（`REFMEM_COMMAND_REASON_NODE_STALE` 等），
读面不得把该语义借来当查询失败。stale 行只能用于诊断，不得当作 current fact 参与校准/判据。

**单写者与撕裂。** 每 key 仅一个 writer；读侧用 `seq` 校验（`seq` 变化即重读一次）；跨 key 不保证
同一时刻快照，行级 `epoch/seq` 是唯一一致性口径。

**契约状态**：pending；代码锚点 `components/distributed_refmem/inc/refmem_command.h`；需 host 表
用例 + 单板 smoke + 四板 OBS HIL 后转 active。


### 角色与协调者

| 角色 | 职责 | 禁止 |
|---|---|---|
| Control Entry | 任一节点的维护接口；解析 SCPI、做形状校验、生成 intent、记录 `origin` | 直接改目标事实；绕过协调者并行发号 |
| Control Coordinator | 唯一发号者；分配 `command_seq`；解析目标掩码；维护在途命令、超时和聚合状态 | 代替目标执行；覆盖目标 NACK |
| Target Executor | `target_mask` 命中的节点；本地 owner TAKE、校验、执行、ACK/NACK | 执行未过本地门禁的命令；执行非本 slot 命令 |
| Observer | SCPI/UI/Report；读本地聚合快照与逐目标行 | 触发同步远程查询；把读取变成控制命令 |

`control_owner_slot` 由 active profile 的部署参数声明，是逻辑角色而不是新增 A 插槽；首版可从 `DistributedApplicationMap` profile 元数据或 `BoardCapabilityTable` capability 派生，二者择一在 C0 冻结。profile 未声明时取最低已 claim 插槽。`DeploymentGate` 必须拒绝零个或多个 `control_owner_slot` 的 RUN。协调者身份用 `(slot, boot_id, lease_epoch)` 标识。

协调者迁移只允许发生在 STOP 或显式维护态；迁移后 `lease_epoch` 递增，旧协调者的未完成命令一律记 `ABORTED_LEASE_LOST`。RUN 中协调者丢失时控制面 **fail-closed**：不再接受新的系统级命令，保留已提交事实和已执行结果，进入可诊断的 `CONTROL_LOST`，等待 STOP 后重新选举；不得在 RUN 中静默产生第二个协调者。此状态下系统级 `STOP` 也不可用，必须由本板 STOP、维护路径或硬件保护逐板执行。本板安全动作（本板 STOP、FAULT 锁存、硬件保护）不经控制面，始终可用。

### 命令信封

复用 `AckCommandRegion` 与 `refmem_command_request_t` 既有字段；新增字段进入新的 schema 版本，旧节点按目录/版本拒绝而不是按旧布局误读。既有字段语义明确如下：

| 字段 | 统一控制面语义 | 约束 |
|---|---|---|
| `command_seq` | 协调者单调命令序号 | 只由协调者分配；回绕按符号差值处理 |
| `source_node` / `source_instance` | 发起接入点，也是聚合回程目标 | 回程只投递到 origin 的命令镜像 |
| `target_mask` | 解析后的目标插槽位图 | 必须是 active claim 子集；空掩码拒绝 |
| `required_mask` | 必须 ACK 的子集 | 缺省等于 `target_mask`；report-only 目标不入 required |
| `command_type` / `command_class` | 命令目录项与权限分类 | 未知 type 或目录版本不符 → `NACK_UNSUPPORTED_COMMAND` / `NACK_CATALOG_MISMATCH` |
| `payload_kind/ref/size/crc32` | 受控载荷引用 | 只允许 `INLINE_SMALL`、`SLOT_REF`、`TLV_REF`、`STAGING_REF`；不得携带 bulk |
| `issue_epoch` / `run_id` | 运行代际 | 与目标 active 不一致 → `NACK_EPOCH_MISMATCH` |
| `timeout_us` | 协调者超时预算 | RUN 类命令不得为 0；目标不得自报 timeout |

需要扩展的字段（随 schema 版本发布）：

| 字段 | 用途 |
|---|---|
| `coordinator_slot` / `coordinator_boot_id` / `lease_epoch` | 标识唯一发号者；旧 lease 的命令拒绝执行 |
| `catalog_crc32` | 命令目录摘要；进入 Version Bundle，跨节点目录不一致时拒绝 |
| `fence_required` / `fence_state` | 声明是否需要“执行结果对后续读取可见”的 fence |
| `control_reply` | 每个节点保留最近一次聚合结果的只读副本；origin 与 Observer 读取它，不访问协调者私有状态 |

命令目录必须为每个 `command_type` 声明 `allowed_states`、`required_capabilities`、`idempotent`、`transactional`、`default_timeout` 和 `payload_schema`；目录 CRC 进入 Version Bundle。未知命令不得按“尽力而为”执行。

### 路由、执行与完成

```text
Entry accepted
  -> entry != coordinator ? forward intent to coordinator : continue
  -> coordinator resolves target_mask / required_mask from active SlotClaimMap
  -> coordinator allocates command_seq, POSTED
  -> REFMEM_CMD to each target (short frame, bounded retry)
  -> target owner TAKE: epoch/lease/permission/catalog/CRC/run-state checks
  -> TAKEN -> EXECUTING/BUSY -> ACK / NACK + reason
  -> REFMEM_ACK back to coordinator
  -> coordinator aggregates required_mask, writes control_reply
  -> aggregate 小帧广播到 active slots 的 control_reply（origin 与 Observer 同源可读）
  -> VISIBLE (snapshot) -> CLEAR_PENDING -> IDLE
```

规则：

- 目标 owner 的 TAKE 只做原子占用与校验，耗时动作转入其 service tick 并置 `busy_flags`；不得在 TAKE/CLEAR 临界区执行耗时动作。
- 目标可以拒绝任何命令；协调者不得用重试覆盖 NACK，只能升级证据或改发新命令。
- 单条命令的 `target_mask` 解析结果必须随命令记录；发令后新 claim 的节点不追溯加入。
- 在途期间同一接入点的新 intent：普通类返回 `CONTROL_BUSY`；只有 `STOP`、`FAULT_CLEAR` 允许抢占，在途命令记 `ABORTED_BY_PREEMPT`。目标在下一个 service 边界停止后续步骤，已发生的物理动作保留 evidence；不承诺原子回滚。
- 传输重试只允许同 `command_seq` 同 `payload_crc32`；目标命中重传缓存时直接返回既有终态，不重复执行。

命令状态机在既有 `IDLE/POSTED/TAKEN/BUSY/ACKED/NACKED/TIMED_OUT` 上扩展：

```text
IDLE
POSTED
TAKEN
EXECUTING / BUSY
ACKED / NACKED / TIMED_OUT / FAULTED / ABORTED / UNKNOWN
VISIBLE
CLEAR_PENDING
IDLE
```

- `VISIBLE` 表示聚合结果和 fence 已进入可读快照；RUN gate 只消费 `VISIBLE` 之后的事实。
- `UNKNOWN` 只用于协调者丢失且无法证明目标是否执行的非幂等命令；必须进入 evidence 和人工/维护复核，不得自动重发。
- `ABORTED` 表示被抢占或租约丢失；不得改写为 `NACKED` 以掩盖已发生的部分执行。

### 完成、fence 与超时

- 完成判据：`required_mask ⊆ ack_flags` 且不存在 required `nack/timeout`；否则失败。
- `fence_state` 依次为 `PENDING → ACKED → VISIBLE → PASSED`；只有 `PASSED` 才允许后续 RUN gate/activation 消费。fence 超时按失败处理，不把“已发送”当作“已可见”。
- 超时只由协调者判定；超时后到达的 ACK 记 `LATE_ACK` 证据，不改写终态。
- 部分成功必须逐目标报告，不得折叠成总体 `OK`；required 与 report-only 分开统计。

### 读模型

- `SYSTem:COMMand:ACK?` / `NACK?` 保持现有语义，读取本地 `AckCommandRegion` 快照。
- 统一控制面新增聚合视图（拟名 `SYSTem:COMMand:STATus?`）：`version,command_seq,state,type,class,origin,coordinator,lease_epoch,target_mask,required_mask,acked_mask,nacked_mask,busy_mask,timeout_mask,fence_state,age_ms,last_reason,last_reason_node,evidence_index,clear_seq,last_completed_seq`。
- 逐目标视图（拟名 `SYSTem:COMMand:TARget? <slot>`）：`slot,state,ack,nack,busy,timeout,reason,age_ms,evidence_index`。
- 可用性：协调者与 origin 读聚合行；目标读自身逐目标行；未参与节点返回 `NOT_APPLICABLE`。聚合行经小帧广播，任何接入点都能读到同一份系统级结果；逐目标明细不全网复制，避免把控制面变成状态广播总线。
- 系统级观测仍走 `SYSTem:REFMEM:STATus?`、`NODE?`、`TABle?`、`QUALity?`；每行必须带 `seq/epoch/age/stale`，UI/Report 不得把多板行拼成“同一瞬间”。
- 所有视图只读快照，不触发跨板阻塞查询；读取命令本身也不得产生新的控制命令。

### 授权与运行态

- 接入点本地权限决定能否提交；目标本地权限决定能否执行；两者都通过才执行（逻辑交集）。权限不随命令 payload 传递，也不存在 force/override 位。
- 命令类与运行态矩阵（首版）：

| 命令类 | STOP | RUN | 维护 / FAULT | 约束 |
|---|---|---|---|---|
| `CONFIG_STAGE` / `CONFIG_ACTIVATE` | 允许 | 拒绝 | 仅维护 | ConfigGate、staging 校验 |
| `START` / `STOP` | 允许 | `STOP` 允许，`START` 拒绝 | 允许 `STOP` | `START` 需 DeploymentGate |
| `ARM` / `FIRE_LOAD` | 允许 | 按 Trigger 状态机 | 拒绝 | 不改变实时边沿 owner |
| `CAL_*` | 允许 | 拒绝 | 仅维护 | realtime idle |
| `SYNC_*` | 允许 | 按 VDC 门禁 | 允许 STOP 类 | 不替代 VDC owner |
| `NODE_LOAD_*` / `BOARD_*` / `TABLE_*` | 允许 | 拒绝 | 仅维护 | RefMem load mode + staging |
| `RESOURCE_JOB` | 允许 | 拒绝 | 仅维护 | Flash lockout / ResourceArbiter |
| `FAULT_CLEAR` | 允许 | 允许 | 允许 | 不覆盖 FATAL 锁存 |
| `CONTROL_LEASE_*` | 允许 | 拒绝 | 允许 | 仅协调者候选 |

- 命令目录、权限表、RunState、ResourceArbiter、Flash lockout、owner validation 任一拒绝即终态；控制面不得重试到通过。
- 不允许通过 payload 表达“写任意地址/任意长度”；payload 只能按目录 schema 解释。

**首版配置授权（N5.2b，2026-10-26 冻结）**：`control_owner_slot` 首版取 `SlotClaimMap` 中**最低已 claim 插槽**
（profile 未显式声明时的 fallback，见「角色与协调者」），是派生值而非第二份状态。配置类命令
（BOARD_CAPABILITY_STAGE / NODE_LOAD_STAGE / TABLE_PACKAGE_STAGE / TABLE_PACKAGE_ACTIVATE）在目标侧
要求 `source_node == 本地 slot`（本板维护，CP9 保留）或 `source_node == control_owner_slot`；其他远程
origin 一律 `PERMISSION_DENIED`(12)，且**授权判定先于 idle 窗口判定**（不因窗口开/关在 7/12 之间摆动）。
无 claimed 插槽时本地配置仍可用、远程配置 12；`DeploymentGate` 的 `GATE_CONFIG`(8) 拒绝无法派生协调者
的 RUN。显式 `control_owner_slot` 字段（方案 B）属 N5.3，落地时替换本 fallback，不与 fallback 并行存在。
**实现状态（2026-10-26，build `20261026134613`）**：目标侧授权检查已落地；host 直驱 dispatch 覆盖
六种情景（含非协调者 12、无 owner fail-closed），四板 HIL 已验「协调者 ACK / 本地 ACK / 关窗 7」；
非协调者从板当前无维护流上行注入点，硬件负向随 N5.5 补。

### 边界

- 不承载 OTA、日志、波形、SD 文件或大表 bytes；只传 path hash、package CRC、descriptor 和摘要。
- 不在 core1 ISR 或 TDMA 硬实时窗口内执行命令；命令帧只使用短帧保留容量，执行发生在 core0 owner service。
- 不替代本地安全联锁、Fault 锁存、看门狗和 Flash 写保护。
- 不提供跨板同步读、远程裸内存、远端 FB 调用或运行时动态部署。
- “控制所有板卡”指一条命令的显式掩码覆盖所有 active slot 并由各目标本地执行，不是一块板代替其他板写事实。

### 失败与恢复

| 场景 | 处理 | 证据 |
|---|---|---|
| 目标 stale / 心跳丢失 | required → 失败；report-only → `SKIPPED_STALE` | `NODE_STALE` + quality |
| epoch / lease 不一致 | 拒绝，不执行 | `EPOCH_MISMATCH` / `CONTROL_OWNER_CONFLICT` |
| 重复或重传 | 返回既有终态 | `DUP_SEQ` 命中计数 |
| 目标 busy 超时 | 协调者置 timeout | `TIMEOUT` + busy 快照 |
| 协调者丢失 | 新 epoch 仅在 STOP/维护态建立；在途命令 `ABORTED_LEASE_LOST`；非幂等命令 `UNKNOWN` | lease 证据 + 逐目标状态 |
| 协调者分裂 | DeploymentGate 拒绝 RUN；命令拒绝发号 | `CONTROL_OWNER_CONFLICT` |
| 目标执行后 ACK 丢失 | 只读状态核对，不重发执行；非幂等类 `UNKNOWN` | 目标 snapshot + dedup cache |
| 目录/版本不一致 | 拒绝，不解释 payload | `CATALOG_MISMATCH` / `UNSUPPORTED_COMMAND` |

`refmem_command_reason_t` 的九项扩展（`UNSUPPORTED_COMMAND`、`CATALOG_MISMATCH`、`CONTROL_BUSY`、`CONTROL_OWNER_CONFLICT`、`TARGET_NOT_CLAIMED`、`ABORTED_BY_PREEMPT`、`ABORTED_LEASE_LOST`、`LATE_ACK`、`SKIPPED_STALE`）属 **C3/C6 未实现部分（v1 显式非目标）**：当前枚举未包含这些值；SCPI 侧 `UNSUPPORTED_COMMAND`/`NOT_CONTROL_OWNER`/`REJECTED` 是文本执行错误，不进入 reason 表。恢复该承诺需枚举、reason 表与负测同批落地。

### 当前实现状态

当前单卡控制面属于 RefMem 域：`DistributedRefMemAO` 拥有命令槽、SCPI intent API 和 owner 执行路径；但命令槽尚未进入 64 KB 表的 `AckCommandRegion`，`ack_command` 区只是预留并注册到 directory。

| 能力 | 状态 |
|---|---|
| 本地 command slot、ACK/NACK/busy/timeout、`SYSTem:COMMand:ACK?` | 已实现 |
| 命令槽物理放置 | **C0 方向（2026-10-23 修订）**：命令槽必须由 REFMEM 复制区承载，任意板写入 → 全环同步；`distributed_refmem.c` 的 static `s_refmem_command_slot` 仅为当前本地载体，不再是规范物理位置；观测面直接读 REFMEM 快照 |
| Event Plane 静态装载（`EVENT_CONFIGURE`） | 未实现；Event Plane 是静态子域，不持有第二命令槽/ACK/epoch，不做运行时注入 |
| 显式 `target_mask` / `required_mask` 字段与本地 take 校验 | 已实现 |
| 跨节点命令路由与 ACK 回程 | `REFTMEM_CMD` 分段经 TDMA 维护流投递到目标已实现（0x52/0x55）；目标执行后把终态写入自己的 **process-image ACK 席位**（`ACK_FLAG_COMMAND` + seq16 + reason），源板从本板 REFMEM 镜像逐槽聚合，更新源 node 控制记录的 `state/ack_flags/nack_flags/last_reason`；`SYSTem:CTRL:STATus?` 可见。**远程配置 stage 在目标窗口开启 + 环运行时已 ACK**（N5.2a.3/N5.2a.4 四板 HIL）；非协调者 Entry 转发、租约仍属 C1/C3 |
| 协调者租约、`control_owner_slot` 唯一性门禁 | 未实现 |
| 聚合视图 / 逐目标视图 / `control_reply` | 逐目标回复聚合已实现（源 node 控制记录 + `SYSTem:REFMEM:CMD:REPLy?` 计数）；系统级 `SYSTem:COMMand:STATus?/TARget?` 只读聚合已实现（从本地 node 记录派生，无第二事实）；广播/`control_reply` 独立副本仍未实现 |
| 跨节点 FENCE（activation gate 的全环 ACK） | 未实现 |
| 命令目录 CRC 进入 Version Bundle | 未实现 |

**app-model 打包基线（2026-10-26，N5.2a.3）**：部署包是**设计期**事实，不携带本板运行期 seat。
`s_tdma_foundation_profile`（或 `s_active_tdma_foundation_profile`）继续唯一承担
`SYSTem:TDMA:RING:TOPology` 写入的运行期 seat（`ring.local_index/reference_index/node_count`），
供 ring apply 与 `TDMA:RING:*` 观测；打包（`refmem_model_build_inline_package_image`）与
`tdma_foundation_profile_table_crc32` 改用设计期基线 `s_design_tdma_foundation_profile`
（`refmem_application_model_init` 与 package 激活时刷新）。原因：`node_load` 的 TDMA owner 是设计期
节点，若把 seat 打进包，非 0 槽板会在 table 9 owner 校验失败（`0x0908`）。STOP→stage→ACTIVATE 之后
由 START/`TDMA:RING:TOPology` 重新 seat，不改变 `set_tdma_ring_topology` 与 ring 应用路径。

**C0 方向修订（2026-10-23）**：命令槽不再冻结在本地 static，改为由 **REFMEM 复制区**
承载：任意板写入本板 RefMem 槽（process-image mailbox 格式，带 seq/CRC），环同步到所有板；
各板从本地镜像读命令、落本地 command slot、执行并把 ACK/NACK 写回同一复制区。观测面直接读
REFMEM 快照（`SYSTem:REFMEM:*`），不新增独立观测面。static `s_refmem_command_slot` 仅作为
迁移前的本地载体保留。`--no-t2` 的 `PHYSICAL_FLIGHT` 纯直通环没有从板写入口，验证必须用启用
flight engine / process-image 的环。

**C0 实现进展（2026-10-23，build `20261023144347`）**：node slot 128→256 B（8 槽 2 KB），表
18,432→19,456 B；node slot 增加控制块（`refmem_command_snapshot_t` 108 B + seq/CRC +
rx/tx/reject/last_error），保留原有状态字段。为容纳 +1 KB，`storage_manager` 文件写缓冲
16→12 KB（3×4 KB），TDMA 帧/槽未动，RAM 余量约 3.3 KB。注：正文中的“64 KB 表”指契约上限/
历史快照布局；当前实现为 19,456 B 压缩布局。

**统一执行入口（2026-10-24）**：`SYSTem:CTRL:EXECute <command_id|name>,<target_mask>,
<required_mask>[,params]` 查静态命令目录（`config/refmem/command_catalog.yaml` → C 表），写本板
node 控制记录；本地 AO 按目录分发到 owner handler，`STATus?`/`CATalog?` 观测，`SYSTem:REFMEM:NODES?`
聚合节点状态（含 `age_ms`/`stale`）。远端 target 经 `REFMEM_CMD` 分段投递。

**命令回复回程（2026-10-24）**：目标执行命令后不再只写本板 node 记录——它把终态写进自己的
**process-image mailbox ACK 席位**（`TDMA_PROCESS_IMAGE_ACK_SEQ16_OFFSET/FLAGS`），用
`TDMA_PROCESS_IMAGE_ACK_FLAG_COMMAND` 与 delta ACK 区分，`seq16 = command_seq & 0xFFFF`，
`reason` 放在可选诊断字节；ACK 席位属于既有 `REFMEM_ACK_FENCE` 路径，不新增 payload class、
第二命令槽或物理总线。目标连续发布 4 个普通镜像（VDC/T2/位置帧优先，回复不抢占一等席），
源板在 `distributed_refmem_cmd_reply_service()` 中按来源槽保存最新回复，并把
`required_mask` 的 ACK/NACK 聚合写回源 node 控制记录；`SYSTem:CTRL:STATus?`、
`SYSTem:REFMEM:NODE:CONTrol?` 直接可见，`SYSTem:REFMEM:CMD:REPLy?` 暴露
rx/applied/stale/duplicate/armed/published 计数。多目标命令下，目标 node 记录保留命令的全局
`required_mask`，因此单目标执行完成后记录保持 `TAKEN` 并置自身 `ack_flags` 位；完成态只在源板
聚合记录中产生（单目标场景自身位即 required，目标记录直接 `ACKED`）。`REFTMEM_MAINTENANCE_SELF_CHECK`（`command_type=MAINTENANCE`）作为 RUN 中可成功执行的控制面探针；
配置类命令由本地 idle 窗口门控：窗口关闭（COMMIT 后或 RUN 边界）返回 `RUN_STATE_DENIED`（7），
窗口开启（STOP/维护）时执行 app-model stage 并 ACK——该结论在 2026-10-26 的 N5.2a.3/N5.2a.4
修复后对**远程目标同样成立**（四板 HIL：目标 slot 1 收到源板命令后 `staging_state=2`、聚合 `ACKED`；
随后 COMMIT 关窗，同命令回 `reason=7`）。NACK 同样回传到源板。限制：seq 只有低 16 位过线；
回复不复制到其它 observer 的 node 记录；聚合广播/协调者租约仍未实现。

**维护流可靠交付（2026-10-24，N3.2.1–N3.2.5）**：7 帧命令改为帧级 **stop-and-wait**：TX 整帧发完后保留槽位，每个目标在 process-image ACK 席位各回一份 `stream_id:generation + result`（诊断字节 `0xA0|result`，`seq16=stream_id:generation`）；源端按目标累积 ACK 掩码，`required_mask` 全覆盖才释放，单目标漏帧触发整帧重传；50 ms 超时、最多 3 次、每次 generation+1，接收端按新 generation 重启重组。**命令回复优先占用 ACK 席位，维护 ACK 仅在无命令回复时发布**，不延迟命令终态。重试耗尽计数并释放槽位，不静默丢帧；7 帧命令的 stop-and-wait 需 **≥2 s 环窗口**（0.5 s 窗口会留下未发完帧，属有界未完成而非丢帧）。N3.2.6 的 2 帧紧凑帧实验投递成功但目标回复未 arm，已回退并记为延后优化；不作为本路径的完成条件。

## REFTMEM-LEASE-01 最小租约（N3.3）

- 记录：每个目标节点一个 `refmem_lease_record_t`（`owner_slot`/`epoch`/`lease_seq`/`expiry_ms`/`state`），状态 `FREE/GRANTED/EXPIRED`。
- 命令：目录命令 `REFTMEM_LEASE_TAKE`（`command_type=LEASE_TAKE`，参数 `epoch,ttl_ms`）与 `REFTMEM_LEASE_RELEASE`（`LEASE_RELEASE`，参数 `epoch`），经既有 `CTRL:EXECute`/命令槽/ACK 回程执行；不新增第二命令槽、第二 ACK 事实或 payload class。
- 语义：同一 owner+epoch 重复 TAKE 只续期；其他 owner 得到 `CONFLICT`（reason `RESOURCE_BUSY`）；`epoch` 小于记录 `STALE_EPOCH`（`EPOCH_MISMATCH`）；过期后旧 owner RELEASE 得 `EXPIRED`（`TIMEOUT`），新 epoch 可接管；非 owner RELEASE 得 `NOT_OWNER`（`PERMISSION_DENIED`）。
- 观测：`SYSTem:REFMEM:LEASE?` 返回 `owner,epoch,state,expiry_ms,lease_seq,state_name`；本地只读，不触发跨板查询。
- 边界：租约是目标节点 RAM 记录，环 stop→start 保持、板复位清零；跨板复制/协调者选举、租约续期心跳尚未实现。

## REFTMEM-IDLE-01 idle 窗口与配置事务（N4）

- idle 窗口是配置类命令（BOARD/NODE/TABLE stage/activate）的唯一允许条件；窗口关闭时 dispatch 直接返回 `RUN_STATE_DENIED`（reason 7），不进入业务校验、不静默执行。
- `CONFig:BEGIN` 打开窗口并标记事务；`CONFig:COMMit` 提交 prepared table views（失败则丢弃）、关闭窗口、清除事务；`CONFig:ABORt` 丢弃 prepared views、重新打开窗口、清除事务；`CONFig:STATus?` 只读 `txn_active,idle_window`。
- 调用方负责外部 STOP/START 边界：STOP 后 BEGIN → stage → COMMIT → START；COMMIT 后窗口关闭，RUN 中配置命令被拒绝。
- 不新增命令槽/ACK 事实/payload class；窗口与事务是 REFMEM 控制面本地状态。节点/板级 stage 由各自 dispatch 校验，prepared table views 是唯一带 commit/discard 对的暂存对象。
- 远程配置命令同样受目标窗口门控：窗口内业务校验失败返回 reason 13，窗口外返回 reason 7；远端 owner/permission 校验尚未实现（当前远程 stage 返回 13，门控仍可区分）。

**C0 命令路径（2026-10-23，build `20261023152319`）**：`refmem_node_control` 提供控制块的
写/读/ACK/清除（seq+CRC 防撕裂）；AO 在 post/take/ack/nack/timeout/clear 后把本地 command slot
镜像到本板 node slot，`refmem_cmd_load_submit` 在发送侧也写入本板控制记录；新增
`SYSTem:REFMEM:NODE:CONTrol? [<node>]` 只读快照（21 字段）。四板 HIL：源 `POSTED`、required
目标 `ACKED`、其余目标 `TAKEN` 并记录各自 ACK 位。跨板复制该记录（C1 写路径）仍未实现。

### 分阶段落地与验收

| 阶段 | 内容 | 退出判据 |
|---|---|---|
| C0 | 信封字段、目录 CRC、聚合视图 schema 冻结；命令槽由 REFMEM 复制区承载（任意板可写、全环同步），本地单板语义不变 | host 用例 + process-image 环 HIL 回归 |
| C1 | 两板与转发：Entry A（协调者）→ Target B；再测 Entry B（非协调者）→ Coordinator A → Target C，覆盖转发与聚合回程（N5.5 冻结后，非协调者转发腿为显式非目标；只保留 Entry A→Target B 与本地入口） | B/C 本地状态变化 + A/B 读到 ACK/fence |
| C2 | required/partial/timeout/preempt/重复幂等 | 负测覆盖 NACK、timeout、重复、抢占 |
| C3 | 协调者租约、STOP/维护态迁移、双主拒绝 | 租约迁移与双主负测 |
| C4 | 多板 all-target `START`/`STOP` + 权限/运行态矩阵 | 逐目标证据一致，无隐式跳过 |
| C5 | 与 `LOAD:SD`/activation gate 的跨节点 FENCE 闭环 | 逐表 activation 证据与回滚保持 |

在 C4/C5 通过前，只能表述为“统一控制面契约与本地命令面已落地”，不得宣称“任意接口控制全部设备”已产品化。

### 与既有契约的关系

- 扩展 `Command / ACK / NACK 契约`，不改变 `AckCommandRegion` 既有字段语义；新增字段随 schema 版本发布。
- 保持 `RefMemRmaWindow` 边界：控制面仍不提供任意地址 put/get；事实同步仍走 `REFMEM_DELTA`。
- 保持 `Snapshot 与并发契约`：读只读本地快照，跨板阻塞查询仍禁止。
- 为 `RefMem Table Image Activation Contract` 补跨节点 FENCE/ACK；activation 的本地 gate 不减少。
- 登记 `REFMEM-CTRL-01`（pending）；实现和证据完成前，本条不授予产品 RUN 或“全设备可控”结论。

### REFMEM-CTRL-01 扩展：不停机四域配置与单命令功能封装

本节冻结“TDMA 不停机”前提下的四域配置与应用层单命令语义，作为 `REFMEM-CTRL-01` 的增量条款；不新增物理总线、payload class、第二命令槽或第二 ACK 事实。

**前提与边界**

- TDMA 是四板互通的数据面和控制承载，**配置期间不得停环**；任何要求 `ring.enabled == 0` 的路径都不能作为运行时配置入口。
- DPLL/VDC/T2 允许在各自域内 STOP/START；TDMA 控制通道继续运行，命令与 ACK 仍走 `REFTMEM_CMD`。域停窗口内执行的配置 ACK 表示 `APPLIED`，不是 `STAGED`。
- TDMA 自身只允许在线安全配置：seat rotation（`p/f/s` 打包为单个原子字，core1 每机会读取一致快照）与 load mask（现有原子 exchange）；ring topology/period 属部署期，运行时返回 `NOT_ONLINE_SAFE`，不做“停环换配置”。
- 入口仍固定为协调者板（最低 claimed slot）；非协调者远端命令入口侧返回 `NOT_CONTROL_OWNER`（不落 reason）、目标侧 reason 12，不排队死 intent。

**入口无关边界（N5.5，2026-10-29 按 HAOFV 冻结）**

统一控制面当前不提供“任一板接口发起远端命令”的能力：非协调者板只保留本地控制面（`REFMEM-LOCAL-01`/CP9），其远端 `REFTMEM_CMD` 在入口侧返回 `NOT_CONTROL_OWNER`（文本执行错误、不落 reason）；目标侧授权拒绝才落 reason 12；两条路径都不中继、不排队、不建 relay 状态；C1 的“Entry B → Coordinator A → Target C”转发腿为显式非目标。

理由：N5.5.0 spike 已证两条前置不成立——远端 node 命令块不在 REFMEM 同步范围（D2 ❌），硬件也没有伪造非 owner 发送者的注入点（D3 ❌）；可行路径只剩新增 intent payload/TLV 容量（触碰 payload/布局/256 B 上限）或产品级入口收敛。HAOFV 要求单一 writer、单一事实、最小机制，故取后者：不新增 payload class/wire/第二命令槽/第二 ACK，也不新增 relay 状态。

代价与重开判据：`REFMEM-CTRL-01` 的“接入点无关”条款由此**未满足**，契约保持 `pending`；若产品要求非协调者远端发起，按紧凑 intent delta 重做，且先补 TLV/参数池同步与 256 B 容量证据，再改本节与登记表，不直接翻条款。

### REFMEM-ENTRY-01 统一控制面入口语义（2026-10-29；C11-02 = PASS_WITH_CHANGES，待整改）

把原“接入点无关”拆成可判定的两条，避免把产品收敛误读成能力缺失：

1. **同目录、同语义、可本地控制（满足）**：每块板拥有相同的命令目录与 function 表（条目数以 `config/refmem/command_catalog.yaml` / `function_catalog.yaml` 为准）、相同 SCPI 语义与 `COMMand:STATus?/TARget?` 只读视图；本板维护与本地控制不受影响（`REFTMEM-LOCAL-01`/CP9）。
2. **跨节点发号单入口（产品级收敛）**：远端 `REFTMEM_CMD` 的唯一发号者是 `control_owner_slot`（`SlotClaimMap` 最低已 claim 插槽）；非协调者远端命令在**入口侧**返回 `NOT_CONTROL_OWNER`（SCPI 文本执行错误，不落 reason），**目标侧**授权拒绝落 reason 12（`PERMISSION_DENIED`）；两条路径都不中继、不排队、不建 relay 状态。原“Entry B → Coordinator A → Target C”腿为显式非目标。
3. **传输未就绪（环停）拒绝（2026-10-29）**：`target` 含远端且 TDMA 环未运行时，入口直接返回 `NOT_ONLINE_SAFE` 且**不产生命令记录**（否则记录停留 `POSTED`、单槽堵死）；该判定优先于 `NOT_CONTROL_OWNER`，并与部署期 `NOT_ONLINE_SAFE`（运行时配置命令拒绝）区分。

**单串口拓扑（2026-10-29 用户约束；T2 配置/MOUNT/聚合已机器捕获）**：最终应用只连接一个串口。该口落在协调者板上时，四域在线配置、`SYSTem:CTRL:FUNCtion:RUN`、`T2_MOUNT`、只读聚合均由这一个口完成，远端目标经 `REFTMEM_CMD` 投递并向源口回 ACK；不需要第二串口或逐板直连。若该口落在非协调者板，只保留本地控制（上面的第 2 条）。**证据**：`out/n8/single_port_capture.json`（port=COM5、build `20261029031043`、`30/31,14,14` 均 `ACKED/ack=14`、`COMMand:STATus?` 可见）；四域配置与 `FUNC_*` 的单口同批捕获未做，属产品约束（未验证）。

**重开判据**：产品要求“任一节点发起远端命令”时，按紧凑 intent delta 重做，先补 TLV/参数池同步与 256 B 容量证据，再改本节与 `REFMEM-ENTRY-01` 的登记行；在此之前不直接翻条款。

**证据**：N5.5 决策记录；单口机器捕获 `out/n8/single_port_capture.json`（COM5，`30/31,14,14` `ack=14`）；`SYSTem:COMMand:STATus?` 单板/四板读数。非协调者 12 为目标侧 host 证据；入口侧为 `NOT_CONTROL_OWNER` 文本错误，HIL 结构性不可达。

**原子域配置命令（15 条，`command_type` 见 `refmem_command.h`）**

| id | name | owner | 应用语义 |
|---:|---|---|---|
| 20 | `TDMA_RING_TOPOLOGY` | TDMA | 部署期；运行时 `NOT_ONLINE_SAFE` |
| 21 | `TDMA_SEAT_ROTATION` | TDMA | 在线原子切换 |
| 22 | `TDMA_PERIOD` | TDMA | 部署期；运行时 `NOT_ONLINE_SAFE` |
| 23 | `TDMA_LOAD_MASK` | TDMA | 在线原子 mask |
| 24 | `DPLL_ROLE` | VDC | request/generation 在线 |
| 25 | `DPLL_TUNE` | VDC | debug profile 在线 |
| 26 | `DPLL_OVERRIDE` | VDC | 在线 |
| 27 | `VDC_REFERENCE_CONFIG` | VDC | VDC 域停窗口内 `APPLIED` |
| 28 | `VDC_OUTPUT_DELAY` | VDC | VDC 域停窗口内 `APPLIED` |
| 29 | `VDC_OUTPUT_TIMING` | VDC | VDC 域停窗口内 `APPLIED` |
| 30 | `T2_CONFIGURE` | T2 | T2 域停窗口内 `APPLIED` |
| 31 | `T2_MOUNT` | T2 | 在线可用（N8.5）：TDMA 不停环下清位置 TX/pending 后启动 T2；四板 `ack=14` |
| 32 | `T2_UNMOUNT` | T2 | T2 域停 |
| 33 | `VDC_STOP` | VDC | VDC 域停（映射到现有 output/reference stop） |
| 34 | `VDC_START` | VDC | VDC 域启（映射到现有 output/reference start） |

命令仍走单一 `AckCommandRegion` 与 `REFTMEM_CMD`；`target_mask`/`required_mask` 显式，目标本地 owner 执行。`command_type` 是既有 u32 字段，新增类型不改变 wire 尺寸；`REFTMEM_CMD_MAX_PAYLOAD`、node slot 与向量表布局不变。

**T2 在线启动（N8.5，2026-10-29）**：统一面 `T2_MOUNT` 在 TDMA 不停环下可用——RefMem 在同一服务上下文清位置 TX/pending（单写者），随后调用 `trigger_t2_runtime_start`，生产者保持停发直到返回。四板 `CTRL:EXECute 31,14,14,1,0,0` 已验 `ACKED/ack=14` 且环连续。运行中配置仍走 cached STOP 快照；部署期 topology/period 仍不在运行时范围。

**应用功能层：一条指令控制一个功能**

```text
SYSTem:CTRL:FUNCtion:RUN <function_id|name>,<target_mask>[,<required_mask>[,<p0>,...]]
    -> QUEUED,<function_seq>
SYSTem:CTRL:FUNCtion:STATus? [<function_seq>]
SYSTem:CTRL:FUNCtion:CATalog? [<offset>[,<count>]]
```

- function 表是编译期 `const` 静态表：`function_id → {name, step_count, failure_policy, steps[]}`；step = `{command_id, target_rule, required_rule, param_bindings[], flags}`。
- 执行器在 `task_refmem_sync` 中有界、非阻塞、串行推进；每步走同一条原子 `CTRL:EXECute` 路径，一次只允许一个 in-flight 命令/目标；聚合 per-step ACK/NACK，最终 `COMPLETED/FAILED/PARTIAL`。
- 失败策略首版只支持 `ABORT_REMAINING` / `CONTINUE_REPORT`；`ROLLBACK` 明确不实现。
- 功能示例（以 `config/refmem/function_catalog.yaml` 为准）：`FUNC_TDMA_ONLINE`（21→23，2 步）、`FUNC_CLOCK_RECONFIG`（24→25，2 步）、`FUNC_TDMA_CLOCK_ONLINE`（21→23→24→25，4 步）、`FUNC_VDC_RECONFIG`（33→27→28→29，4 步）、`FUNC_VDC_OUTPUT_DELAY`（33→28，2 步）、`FUNC_T2_RECONFIG`（32→30，2 步；T2 启动走在线 `T2_MOUNT`）。
- **TDMA 连续性不变量**：任何 function 执行前后，TDMA `up_sequence`/`down_rx_sequence` 必须单调推进；HIL 证据必须包含跨功能前后的序号与帧计数，不允许用“功能 ACK”替代链路连续性。
- function 表不引入动态脚本、运行时注册、第二命令槽或第二 ACK 事实；单板本地 `T2:MOUNT/UNMOUNT` 等路径仍保留（`REFMEM-LOCAL-01`）。

**状态与证据**

- 契约状态：pending（作为 `REFMEM-CTRL-01` 的增量，不单独授予产品 RUN 结论）。
- 实现状态（2026-10-29）：`config/refmem/function_catalog.yaml` 共 6 个 function；执行器单 in-flight、串行 step、`ABORT_REMAINING`；`SYSTem:CTRL:FUNCtion:RUN/STATus?/CATalog?` 已接线；host 覆盖成功/失败/超时/单 in-flight。四板 HIL：`FUNC_TDMA_ONLINE` 2 步、`FUNC_TDMA_CLOCK_ONLINE` 4 步、`FUNC_VDC_RECONFIG` 4 步均 `COMPLETED/ack=14`；`FUNC_T2_RECONFIG,14,14` 四板正向 `COMPLETED/ack=14`（T2 handler 读 cached STOP 暂存 grid/deployment）；在线 `T2_MOUNT` 四板 `ack=14`（N8.5）。**证据等级**：P3 为 QUICK/DPLL scope、`strict_gates_passed=false`，不覆盖 T2 在线 MOUNT 与位置复位；该路径按 HIL 读数成立，契约验收需 full/等价专项。2026-10-29 全量尝试：`--profile full` 可走完 37 步（仍写 QUICK），`--full-bench` 卡在 TRN-03 SCK re-arm 预算（`REFTMEM-TASK-20261029-014`）。
- 代码锚点：`refmem_command.h`、`refmem_command_catalog.h`、`refmem_function.h`、`refmem_function_catalog.h`。
- 验收：host 覆盖目录/参数/入口/在线原子/域停序列/失败策略；四板 HIL 覆盖协调者单端口、逐目标 ACK、TDMA TX/RX 连续性、`NOT_ONLINE_SAFE` 负测。

## 对外接口边界

RefMem 不建立裸顶级 `REFMEM` SCPI 域。对外维护查询归 `SYSTem:REFMEM:*`：

```text
SYSTem:REFMEM:STATus?
SYSTem:REFMEM:NODE?
SYSTem:REFMEM:CLAIM?
SYSTem:REFMEM:LOAD:SD
SYSTem:REFMEM:LOAD:NODE
SYSTem:REFMEM:LOAD:ACTivate
SYSTem:REFMEM:LOAD:STATus?
SYSTem:REFMEM:BOARD?
SYSTem:REFMEM:LOAD:BOARD
SYSTem:REFMEM:LOAD:BOARD:STATus?
SYSTem:REFMEM:TABle?
SYSTem:REFMEM:TABle:IMAGe?
SYSTem:REFMEM:TABle:VIEW?
```

统一控制面的聚合视图拟以 `SYSTem:COMMand:STATus?` 和 `SYSTem:COMMand:TARget?` 提供；实现前，`SYSTem:COMMand:ACK?` / `SYSTem:COMMand:NACK?` 仍是唯一命令事实读取面。它们同样只读本地快照，不改变本节接口边界。

`SYSTem:REFMEM:STATus?` 的前 8 个字段保持为 `table_size,layout_version,table_seq,local_node_id,node_count,local_heartbeat,service_count,flags`。从初始化可观测性固化后追加 `initialized,init_stage,init_error`，其中 `init_stage` 使用 `DISTRIBUTED_REFMEM_INIT_STAGE_*` 固定枚举，正常完成为 `READY=8`，`init_error=0` 表示无初始化错误；失败时 `init_error` 等于失败阶段码。

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

`SYSTem:REFMEM:LOAD:SD [path]` 扫描 SD `/manifest.idx`，随后读取并校验 RefMem table image；默认路径为 `/refmem/app_model.rmtp`，可用可选 path 覆盖。StorageAO/FB 负责 SD/FatFs、manifest scan 和文件读写；SCPI 不直接写 TableRegistry，`DistributedRefMemAO` 通过 `TABLE_PACKAGE_STAGE` command slot 消费 StorageAO 给出的 path hash、manifest 摘要、package CRC、package bytes 和 table CRC 摘要，随后写 RefMem staging 并 ACK/NACK。当前 parser 已校验 `.rmtp` header、table directory、payload CRC、package CRC 和单表 CRC，并把完整 package bytes 复制到 `RefMemTableRegistry` 私有 staging image buffer；activation gate 通过后，registry 会将旧 active descriptor/buffer 移入 rollbackable，再把 staging descriptor/buffer 切为 active。`RefMemTableRegistry` 的 staging descriptor 记录 package CRC，table entry 记录各自 table directory CRC；ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality 全 9 张 canonical 表均为固定 u32 wire payload，并通过 owner validation 后进入 OWNER_OK。activation 成功后，`DistributedRefMemAO` 触发 application model 从 active stable table view 解析 runtime snapshot，后续 SCPI/SystemManager/SlotClaim/RUN gate 的 getter 读取 runtime snapshot；真实 owner validation callback 调度仍需后续接入。

`SYSTem:REFMEM:LOAD:ACTivate` 是维护/调试入口，只提交 activation intent，不直接改 active 表。`DistributedRefMemAO` 通过 `TABLE_PACKAGE_ACTIVATE` command slot take 后组装 activation gate：RefMem load mode 必须 idle，realtime/trigger 必须 idle，runtime protection 必须具备 RAM-resident / entry owner / flash lockout online 证据，staging descriptor 必须 `CRC_OK + OWNER_OK`，SlotClaim 和 DeploymentGate 必须通过，当前本地 command take 视作 local ACK gate。registry 切换前必须先由 application model owner 对 staging stable table view 做预解析，预解析失败返回 `STAGING_VIEW_INVALID` 并保持旧 active 不变；gate 通过后才调用 `refmem_table_registry_activate_staging()`，成功后提交已预解析的 pending runtime snapshot 并更新 `active_package_crc32`。失败返回 `REJECTED` 并通过 `SYSTem:COMMand:ACK?` 暴露 ACK/NACK。该入口当前完成 registry 级 descriptor/buffer 切换、本地 runtime snapshot 两阶段切换和 staging view 预解析，后续仍需接跨节点 FENCE/ACK。

`SYSTem:REFMEM:TABle:VIEW? [role],[table_id]` 通过 TableRegistry access/release 读取指定 image 的单表只读 view 摘要，返回 `version,role,table_id,table_seq,package_crc32,table_crc32,image_offset,image_size,first_u32`。它用于证明 active/staging/rollbackable bytes 可以按 RMTP directory 稳定访问，不用于 dump 完整表；完整 dump/load 规则仍需后续按 P0 独立实现。

Inline SCPI load 的产品语义不是“单表加载”，而是“单表 draft 进入完整节点模型候选”。单表可以独立生成、独立校验和记录 CRC，但不能独立 activation；只要要进入 active runtime，必须合并成完整 9 表 RMTP package，再交给 TableRegistry 统一 staging、activation 和 rollback。

| 层级 | 产物 | 允许操作 | 禁止操作 |
|---|---|---|---|
| Table Draft | 单张候选表，例如 `NodeLoadTable draft`、`BoardCapabilityTable draft`。 | 单表字段校验、owner 初验、整表 CRC、失败 evidence。 | 直接写 active、单表独立 activation、把 draft 当 runtime fact。 |
| Node Model Candidate | 由当前 active 9 表叠加所有 draft 后形成的完整候选模型。 | 跨表校验、SlotClaimMap、RealtimeCapabilityContract、DeploymentGate、quality/evidence 合成。 | 缺表运行、只校验被修改表、绕过 ApplicationModel owner。 |
| RMTP Package Image | 完整 9 表 `.rmtp` bytes，含 header、directory、payload CRC、package CRC、per-table CRC。 | `refmem_table_registry_stage_package_image()`、staging stable view、activation gate、rollbackable 切换。 | 向量表承载完整数据、SCPI 直接写 registry active buffer。 |

| 入口 | Draft 输入 | 合并规则 | 最终进入 TableRegistry 的产物 |
|---|---|---|---|
| `SYSTem:REFMEM:LOAD:SD [path]` | SD 中完整 `.rmtp` package。 | 直接按 package owner validation 解析 9 表。 | 原始 `.rmtp` package image。 |
| `SYSTem:REFMEM:LOAD:NODE ...` | 一条 NodeLoad 候选，累积到私有 `NodeLoadTable draft`。 | 当前 active 9 表 + NodeLoad draft + 已存在 BoardCapability draft。 | 新生成的完整 9 表 inline RMTP package image。 |
| `SYSTem:REFMEM:LOAD:BOARD ...` | 一条 BoardCapability 候选，累积到系统级多板能力资源综合表 draft。 | 当前 active 9 表 + BoardCapability draft + 已存在 NodeLoad draft。 | 新生成的完整 9 表 inline RMTP package image。 |

`SYSTem:REFMEM:LOAD:NODE <node_id>,<instance_id>,<role_mask>,<persona_mask>[,<enabled>,<required>,<load_order>]` 允许 SCPI 提交一条 NodeLoad 候选到 staging，用于调试和自组网协调前的节点实例化验证；SCPI 只调用 RefMem intent API，`DistributedRefMemAO` 通过 `NODE_LOAD_STAGE` command slot take 后，由 ApplicationModel owner 更新私有 `DistributedNodeLoadTable draft`、执行 NodeLoadTable contract validation、计算 draft 整表 CRC，再把当前 active 9 表叠加所有 draft 合成为完整 Node Model Candidate。合成结果编码为 9 表 inline RMTP package image，交给 `RefMemTableRegistry` staging；该入口不直接覆盖 active `NodeLoadTable`，也不修改 NodeRegion live fact。多条 `LOAD:NODE` 可在同一 draft 上累积候选，每次成功后都会刷新完整 staging package image，后续 activation/rollback 只处理完整 package。

`SYSTem:REFMEM:LOAD:BOARD <board_id>,<board_uuid_crc32>,<capability_mask>,<io_constraint_mask>,<ip_core_mask>,<default_persona_mask>,<hw_profile_crc32>,<active_default_slot>,<online_required>` 允许 SCPI 提交一条 BoardCapability 候选到 staging，用于调试和 SD System Pack 前置验证；它必须满足 `REFMEM+VDC+TDMA` baseline 和字段范围约束。`BoardCapabilityTable` 是系统级多板能力资源综合表，`board_id` 是板卡/profile 候选标签，不是 A0-A7 执行 slot；该命令由本地 `DistributedRefMemAO` 通过 `BOARD_CAPABILITY_STAGE` command slot 执行，payload_ref 才携带 `board_id`。ApplicationModel owner 更新私有 `BoardCapabilityTable draft`、执行 BoardCapabilityTable contract validation、计算 draft 整表 CRC，并刷新完整 9 表 inline RMTP package staging。以上入口都不直接覆盖 active 表，也不修改 NodeRegion live fact。`SYSTem:REFMEM:LOAD:STATus?` 读取系统级 package load snapshot：`version,load_seq,source,mode,staging_state,manifest_status,manifest_schema,manifest_required_count,manifest_missing_count,path_hash,active_package_crc32,staging_package_crc32,staging_lint_error_count,staging_first_lint_error,staging_node_id,staging_instance_id,staging_role_mask,staging_persona_mask,staging_enabled,staging_required,staging_load_order,last_error,manifest_build_id,path`。`SYSTem:REFMEM:LOAD:BOARD:STATus?` 读取 BoardCapability draft snapshot：`version,load_seq,mode,staging_state,active_crc32,staging_crc32,staging_lint_error_count,staging_first_lint_error,staging_board_id,staging_board_uuid_crc32,staging_capability_mask,staging_io_constraint_mask,staging_ip_core_mask,staging_default_persona_mask,staging_hw_profile_crc32,staging_active_default_slot,staging_online_required,last_error`。

写入 `/refmem/app_model.rmtp` 使用通用 Storage 文件接口，不再保留 `SYSTem:REFMEM:PACKage:*` 专用入口：

Storage 文件写入属于 `StorageAO`，不是 `DistributedRefMemAO` 的职责。SCPI 只能建立写事务、提交分块、提交 commit job 和查询 snapshot；实际 SD/FatFs 操作必须在 StorageAO 服务上下文执行。RefMem 向量表不承载 `.rmtp` 文件数据，只记录 path hash、CRC、load state 和 validation evidence。当前最小系统板上 SD 与 LCD 共用板级 `BOARD_SPI_PORT`，因此 UI 必须在 Storage job `QUEUED/RUNNING` 时让路；RefMem 最小 transport 使用 PIO-SPI GPIO16-19，不与 SD 的硬件 SPI 共享总线。若写入过程中出现 `RUNNING`，应优先检查 StorageAO job 优先级、boot snapshot 是否插队、ResourceArbiter 的 `SPI0|SD/LCD` holder 和 FatFs replace 结果，而不是把问题归因到 RefMem PIO-SPI transport。

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

其中 `.rmtp` 是固件后续解析的二进制 table image，`.idx` 是轻量索引，`.json` 是 PC 工具和人工审查用说明。RMTP 二进制生成由 `tools/refmem/refmem_table_image/refmem_table_image.py` 统一维护；`tools/refmem/refmem_pack_build/refmem_pack_build.py` 和 `tools/storage/sd_fs_build/sd_fs_build.py` 只负责各自输出目录、manifest 和 idx。当前 table 0 `ApplicationMap`、table 1 `BoardCapability`、table 2 `GenericNode`、table 3 `NodeLoad`、table 4 `FbInstance`、table 5 `EventLink`、table 6 `DataLink`、table 7 `DeploymentGate` 与 table 8 `ConnectionQuality` 均已是固定 u32 真实表镜像；带名字或路径语义的表项在 `.rmtp` 中使用 `name_hash` / `region_path_hash`，人类可读文本留在 `.idx` / `.json` 等调试说明中。SD System Pack 在根 `/manifest.idx` 中加入：

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

- `distributed_refmem.h/.c`：当前对外兼容入口，按 `DISTRIBUTED_REFMEM_TABLE_SIZE` 维护本地 `DistributedVectorTable`、header、node slot、core vector、runtime protection snapshot 和 status flags。
- `refmem_vector_table.h/.c`：封装当前静态 table layout、slot directory、header CRC 和 directory 校验。
- `refmem_application_model.h/.c`：落地 ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality、静态 linter、package CRC 和 load staging snapshot。
- `refmem_slot_claim.h/.c`：首版派生 `SlotClaimMap`，从 GenericNode、BoardCapability、NodeLoad 和 FB instance 生成 A0-A7 resolved assignment、candidate/assigned/conflict/overflow 计数、loaded instance mask 和 CRC。
- `refmem_realtime_contract.h/.c`：首版派生 `RealtimeCapabilityContract`，从 NodeLoad、FB instance、GenericNode 和 SlotClaimMap resolved assignment 生成实例级资源/IO/类 IP 核能力契约；保留 default-slot fallback 仅用于过渡。
- `SYSTem:REFMEM:LOAD:SD`：已接入 Storage manifest 扫描和 `.rmtp` table image parser，可校验 header、directory、payload CRC、package CRC 和单表 CRC；SCPI 只提交 StorageAO 结果摘要和 package bytes，`DistributedRefMemAO` 通过 `TABLE_PACKAGE_STAGE` command slot 写 staging 并 ACK/NACK，registry 级 activation 已能切换 active/staging/rollbackable image descriptor 和私有 bytes buffer。
- `SYSTem:REFMEM:LOAD:NODE`：已支持通过 SCPI inline 提交 NodeLoad 候选到私有 staging `DistributedNodeLoadTable` image，按候选表重新执行 NodeLoadTable contract validation，并把 table 3 staging CRC 和 validation state 发布到 TableRegistry；尚未执行 active/rollbackable image 切换。
- `SYSTem:REFMEM:LOAD:ACTivate`：已接入 `TABLE_PACKAGE_ACTIVATE` command slot；`DistributedRefMemAO` owner take 后检查首版 activation gate，并执行 registry 级 image activation。COM5/COM6 已验证 `LOAD:SD -> LOAD:ACTivate -> SYSTem:REFMEM:TABle? 0..8` active CRC bundle 切换。
- `SYSTem:REFMEM:LOAD:STATus?`：已可查询 load sequence、source、RefMem load mode、staging state、manifest、active/staging CRC、lint/error 和当前候选。
- `SYSTem:REFMEM:LOAD:BOARD` / `SYSTem:REFMEM:LOAD:BOARD:STATus?`：已支持通过 SCPI inline 提交单条 BoardCapability 候选到 staging snapshot，校验 board 范围、`REFMEM+VDC+TDMA` baseline 和默认 slot 范围；SCPI 已收敛为 RefMem intent，`DistributedRefMemAO` 通过 `BOARD_CAPABILITY_STAGE` command slot 写 staging 并 ACK/NACK；尚未形成多条 staging BoardCapabilityTable image。
- `refmem_table_registry_activate_staging()`：已落地 registry 级 activation gate 和真实 package image bytes 切换；无 package bytes 的 metadata-only staging 仍返回 `IMAGE_NOT_LOADED` 并保留 staging descriptor，禁止用旧 staging buffer 伪装 active 替换。
- `refmem_table_registry_get_image_descriptor()` / `SYSTem:REFMEM:TABle:IMAGe?`：已可读取 active/staging/rollbackable descriptor，用于维护查询和 activation 验证脚本。
- `refmem_table_registry_access_table()` / `release_table()` / `SYSTem:REFMEM:TABle:VIEW?`：已可按 role/table id 从 RMTP directory 借出稳定只读 table payload view，并通过 reader guard 阻止未 release 时 activation。

尚未形成完整实现的部分：

- activation 后 active image 到业务结构表的解析、owner validation callback 调度和完整 dump/load 规则。
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
