# 融合型系统向量黑板与功能块架构方案

Status: Active
Domain: HAOFV
Canonical: `docs/arch/HAOFV_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`, `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`
Last updated: 2026-09-02
Version: 4

本文档定义 Distributed Hard Real-Time Trigger System 后续产品化演进采用的顶层软件架构。HAOFV 不直接冻结某一块 PCB 的引脚、电源和器件选型，而是定义系统组件之间的 owner、层次、约束传递、状态事实和执行边界。具体板级约束由 `docs/hardware/` 下的调试最小系统板约束、产品板约束和网表评审承接。

> **实施指南**：[HAOFV_IMPLEMENTATION_PLAYBOOK.md](HAOFV_IMPLEMENTATION_PLAYBOOK.md) 提供 ECC 表实现示例、Flash 异步 Job 代码和历史 GPIO 迁移样例；具体硬件约束仍以 `docs/hardware/` 和 `docs/sync/SYNC_IO_ARCHITECTURE.md` 为准。

## 架构名称

推荐正式名称：

```text
Hybrid Active Object Function Block Vector Architecture
```

中文名称：

```text
融合型主动对象功能块向量架构
```

工程内部可简称：

```text
HAOFV Architecture
```

该架构不是单一表驱动，也不是单一事件队列，也不是完整 IEC 61499 运行时，而是组合以下设计：

- `Active Object Layer`：每个功能域独立运行，拥有事件队列、生命周期、执行预算和对外 API。
- `IEC 61499-inspired Function Block Layer`：采用固定功能块、静态事件连接、数据输入输出和 ECC 状态机。
- `System Vector Blackboard`：系统总表，保存全局状态摘要、资源占用、错误摘要。
- `Domain Vector Tables`：各功能域独立向量表，例如 OTA、Trigger、Storage、UI。
- `Distributed Vector Blackboard / RefMem Sync Domain`：分布式系统共同事实内部主域，维护 64 KB 反射内存向量表、静态分布式应用模型、slot owner、命令槽、ACK/NACK、stale、CRC 和 sequence。
- `Virtual Distributed Clock / VDC Domain`：分布式系统共同时间内部主域，维护 `local_tick -> vdc_time` 映射、SYNC DPLL、HOLDOVER/RELOCK、timestamp dictionary、时间质量和预测分发时间基准。
- `Calibration Domain`：分布式链路测量和校准事实内部主域，维护有向线序/邻接矩阵、环路顺序、logical slot map、CLK/DATA/SYNC 原始 edge evidence、双向时间传递、residence、endpoint bias、path-delay、active/staging generation 和接受门禁。
- `TDMA Foundation Domain`：分布式系统确定性通讯骨架内部主域，维护上行/下行 TDMA、window、guard、payload registry、adapter、ring runtime、completion evidence 和质量摘要。
- `Table-Driven State Machines`：状态转移、命令解析、资源冲突、错误码使用表驱动。
- `Resource Arbiter`：统一管理 Flash、SPI、PIO、DMA、USB、LCD、SD 等资源互锁。
- `Flash Persistence Domain`：用唯一 FlashMap、FlashTransactionAO、BootControlStore、NVS、
  BlobStore 和 FCB 承接跨重启事实；业务域不能直接 erase/program 或持有裸分区地址。
- `RTE-like Service Layer`：上层不直接碰硬件，通过驱动和服务层访问外设。
- `Bootloader/OTA Safety Chain`：App 接收和校验，Bootloader 启动选择、搬运、回滚。
- `VDC/DPLL Core Infrastructure`：在 HAOFV 下形成共同时间事实、同步锁定、HOLDOVER/RELOCK、预测分发和 T2 质量闭环。
- `T2 Reservation Architecture`：定义跨域预约、共同时间目标、READY/fence、本地 deadline、实际 T2 latch 和 completion evidence 的装配顺序，不新增运行 owner。

完整分层如下：

```text
SCPI / UI / SD / Bootloader Result
        ↓
Active Object Layer
        ↓
IEC 61499-style Function Block Layer
        ↓
Time-Synchronized Vector Blackboard Layer
        ↓
Virtual Distributed Clock / VDC Domain
         ↓
Calibration Domain
         ↓
Distributed Vector Blackboard / RefMem Sync Domain
        ↓
TDMA Foundation Domain
        ↓
Hardware Service Layer

Hard Real-Time Side Path:
PIO / DMA / IRQ
```

一句话总结：

```text
Active Object 管运行，
IEC 61499 风格功能块管逻辑，
Vector Blackboard 管本节点事实，
Distributed RefMem 管多节点共同事实，
Calibration Domain 管链路测量和校准事实，
VDC Domain 管多节点共同时间，
TDMA Foundation 管上/下行确定性通讯骨架，
T2 主线管预约装配，PIO/DMA 管硬实时。
```

## 约束逻辑

HAOFV 的顶层职责不是列出具体 GPIO，而是把系统约束变成可追踪、可验证、可下放的规则链。板级资源、调试板差异和产品板差异由硬件域维护：

| 约束层 | owner | 输出到下层的内容 |
|---|---|---|
| 产品能力约束 | 产品架构 / 系统设计 | 必须支持的主域、运行模式、同步精度、报告闭环、OTA/SD/诊断能力。 |
| HAOFV 组件约束 | 本文档 | AO/FB/Vector/Distributed RefMem/Resource Arbiter/Service 的 owner、状态事实和跨域交互规则。 |
| 业务域约束 | `trigger/`、`sync/`、`calibration/`、`interface/` 等 | 触发序列、DPLL、校准、SCPI 契约、反射内存字段和 ACK/NACK。 |
| 板级硬件约束 | `hardware/` | 调试最小系统板和产品板的 pin map、隔离边界、电源域、连接器和网表事实。 |
| 固件实现约束 | `boards/`、`components/`、`middleware/` | board profile、驱动服务、RTOS task、PIO/DMA/IRQ owner 和构建配置。 |

约束传递方向必须单向收敛：

```text
产品需求
  -> HAOFV owner / layer / vector rule
  -> 功能域设计
  -> board profile 与硬件约束
  -> 固件实现和验证
```

反向允许提出变更请求，但不能让某一块调试板的临时接线直接污染顶层架构。调试最小系统板只证明架构闭环和软件路径，产品板约束才冻结量产 pin map、隔离、电源和连接器策略。

### 组件约束

| 组件 | 顶层约束 | 由谁细化 |
|---|---|---|
| SCPI / UI / System Pack | 只能表达意图、配置和查询；不能直接驱动硬实时边沿。 | `interface/`、`storage/` |
| Active Object | 拥有事件队列、生命周期和执行预算；外部入口只能投递事件。 | 各功能域设计 |
| Function Block | 执行 ECC 状态迁移、资源规则和错误归因；Action 必须立即返回，耗时动作只能返回 busy 并由后续 tick 推进。 | `trigger/`、`ota/`、`storage/`、`sync/` |
| Vector Blackboard | 保存事实、摘要、命令槽和版本；字段必须有唯一 writer、值域、生命周期和快照规则。 | `refmem/`、各 Domain Vector |
| Distributed RefMem | HAOFV 内部基础主域；跨节点动作只能通过反射内存向量表、静态分布式应用模型、命令槽、ACK/NACK、stale、CRC、sequence 和同步帧表达。 | `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`、`docs/arch/RTOS_HAOFV_ARCHITECTURE.md`、`components/distributed_refmem/` |
| VDC Domain | HAOFV 内部基础主域；形成多节点共同时间事实，维护 local tick 到 VDC 时间映射、SYNC DPLL、HOLDOVER/RELOCK、timestamp dictionary 和时间质量门禁。 | `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`、`docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`、`components/vdc_domain/` |
| Calibration Domain | HAOFV 内部基础主域；测量有向线序、邻接矩阵、环路顺序和 CLK/DATA/SYNC 链路 delay，维护 accepted slot map、双向时间传递、residence、endpoint bias、path-delay active/staging、generation/freshness 和校准接受门禁。 | `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`、`components/calibration_manager/` |
| TDMA Foundation | HAOFV 内部基础主域；形成上行/下行确定性通讯骨架，维护 window、guard、payload registry、adapter、ring runtime、completion evidence 和质量摘要；VDC/RefMem 只能挂载 payload 或消费 evidence。DPLL residual 只通过维护态固定 SRAM capture 进入 Core0/StorageAO/SD 离线证据链，不进入 TDMA realtime path。 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`、`components/tdma/`、`components/vdc_dpll_manager/` |
| Resource Arbiter | 管理 Flash、SD、USB、PIO、DMA、LCD、隔离链路等互斥资源；Flash/XIP 双核安全是最高优先级硬约束。 | `arch/RTOS_HAOFV_ARCHITECTURE.md` |
| Flash Persistence Domain | App 唯一 writer 是 core0 `FlashTransactionAO`；Bootloader 使用无 RTOS 的 `BootFlashService`；各业务域只提交 versioned intent 并读取 durable completion。 | `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`、`drivers/mcu/flash/`、`components/ota_manager/` |
| Hardware Service | 封装 SDK/驱动细节；上层不直接调用板级 API。 | `components/`、`drivers/` |
| PIO/DMA/IRQ | 只执行硬实时动作和最小事实回写；对外维护入口归 `REALtime`，产品业务动作入口仍归 `TRIGger`。 | `sync/`、`trigger/`、board profile |

### 板级约束入口

| 板级约束 | 用途 |
|---|---|
| `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` | 当前调试最小系统板/DEMO 板约束，允许为了验证 RTOS、双核、SCPI 和基础 IO 做临时取舍。 |
| `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md` | 后续产品板约束入口，冻结隔离、电源、连接器、网表和产品 pin map。 |
| `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md` | 当前 RP2350B QFN-80 产品板 GPIO、隔离域和模拟/PIO 约束明细。 |

顶层架构引用这些文件，但不复制具体 pin map。任何 `GPIOxx`、连接器、ESD、电源域、隔离边界和装配 DNP/0R 策略应在硬件域维护。

### 顶层安全硬约束

以下约束优先级高于普通功能设计、调试便利性和局部实现习惯。任何代码或子文档与本节冲突时，以本节为准，并同步修正下级文档。

| 约束 | 规则 |
|---|---|
| 双核 Flash/XIP 安全 | 任何 Flash erase/program 只能由 core0 发起；进入 Flash 临界区前必须申请 Flash bus 资源锁，并等待 core1 park/lockout ACK。 |
| core1 实时 owner | RTOS + 双核 AMP 主线下，TriggerAO/TriggerFB 运行在 core1；core0 只能投递事件、写命令槽或读取快照。 |
| 跨核共享事实 | core0/core1 共享字段必须有唯一 writer，快照必须使用 seqlock、双缓冲或等价 sequence/version 机制，并使用 `__atomic` 或 DMB 屏障。 |
| 分布式共同事实 | 不引入完整 IEC 61499 分布式运行时；多节点状态、命令、ACK/NACK、版本、质量和证据统一由 Distributed RefMem / RefMem Sync 内部主域承接。 |
| 分布式共同时间 | VDC Domain 是共同时间唯一 owner；SYNC DPLL 维护 VDC offset/rate，Angle DPLL 只生成扫描预测时间，不能写 VDC offset/rate。 |
| 分布式确定性通讯 | TDMA Foundation 是上行/下行 TDMA runtime、payload registry、adapter 和环路 completion evidence 的唯一 owner；`clk_sys` 拍数是 Core1 schedule 唯一时间事实源；产品 SHORT process image 按 mandatory-first 静态装配 Node mailbox 与固定 DPLL observation trailer，并作为启动时一次注入、运行中持续循环的 resident process image；每个 Node 只在自己的固定 segment 执行 UNLOAD/LOAD，无更新时透传，物理 frame completion 回到下一 cycle，只有 STOP、复位、故障或重新配置才退出。DPLL 启用不得改变帧型、长度、序列、PIO 节拍，也不得临时使用余量、guard 或第二帧。错误恢复由 Core0 准备、Core1 在固定窗口装载、PIO 发送，双 recovery buffer 交替且每周期最多一帧，使用独立静态预算并复用原 Node segment offset。VDC、RefMem、Trigger、OTA 只能通过注册 payload、提交 intent 或读取 snapshot 使用它。 |
| Vector 字段契约 | 每个 Vector 字段或字段块必须定义 writer、value domain、lifecycle、snapshot-needed；不得把 Vector 当作全局变量自由读写。 |
| 时间回绕 | `uint32_t timestamp_ms` 只能用于短时间差；时间差必须使用回绕安全写法 `int32_t diff = (int32_t)(t1 - t0)`，长时间事实需要 epoch 扩展。 |
| Metadata failsafe | Bootloader 必须定义 metadata 双副本无效的强制恢复路径，禁止继续启动未知镜像。 |
| FB 非阻塞 | FB action 必须立即返回；耗时动作返回 `FB_RESULT_BUSY` 且 `next_state=self`，由 AO service 后续 tick 分步推进。 |

## 已冻结域契约登记（2026-08-21）

以下条目只提供顶层可见性；契约内容和版本以 `docs/check/DOCS_REGISTRY.md` 及对应域 canonical 文档为事实源。

| contract_id | 契约 | 域文档位置 | 顶层相关性 |
|---|---|---|---|
| `TDMA-REASON-01` | ring reason code 冻结 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` 的“Ring reason code”章节 | 错误码空间 TDMA 段。 |
| `TDMA-SEQLOCK-01` | runtime snapshot 必须使用 seqlock | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` 的“core0/core1 双 FIFO 与所有权”章节 | 跨核共享事实的实现要求。 |
| `TDMA-HOP-01` | `hop_limit` 归属 ring profile | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` 的“Transport Envelope 与长短帧”章节 | 分布式确定性通讯约束。 |
| `REFMEM-260B-01` | critical delta 容量合同 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` 的“Transport Envelope 与长短帧”章节 | RefMem 实时短帧容量约束。 |
| `VDC-DPLL-01` | DPLL 准入要求 `timestamp_resolution_ns <= 100` 且来自硬实时 latch | `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md:301-308` | 分布式共同时间证据门禁。 |

登记表中的扩展契约仍为 `pending`，顶层只显示其可见性，不把它们当作已冻结
硬约束：

| contract_id | 契约 | 域文档位置 | 状态 |
|---|---|---|---|
| `TDMA-FLIGHTBITMAP-01` | SHORT process image 固定 mailbox 与 core1 RX 位图 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` 的“固定 Node image、DPLL trailer 与 RX 位图快路径”章节 | pending |
| `TDMA-PROCESSIMAGE-01` | 固定 SHORT process image 静态装配 Node mailbox 与 DPLL observation trailer，DPLL 不得替换 wire frame | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` | pending |
| `TDMA-RESIDENT-01` | process image 启动时一次注入并持续循环；单轮多 Node 局部 UNLOAD/LOAD，无更新透传，frame completion 不终止 resident loop | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md:TDMA-RESIDENT-01` | pending |
| `TDMA-RECOVERY-01` | 双 recovery buffer 原 Node 位置重传、Core0/Core1/PIO owner 边界与独立静态预算 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` | pending |
| `TDMA-OPMODE-01` | SPI 速率与 TDMA 周期按离散 operating profile 成对切换，STOP 后生效 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` 的“SPI 速率与 TDMA 周期 operating profile”章节 | pending |
| `ARCH-FLASHMAP-01` | FlashMap v2 是 Boot/linker/App/factory/tool 的唯一分区词汇 | `docs/arch/HAOFV_FLASH_ARCHITECTURE.md` | pending |
| `ARCH-FLASHOWNER-01` | App erase/program 仅 core0 FlashTransactionAO；Boot 使用最小 BootFlashService | `docs/arch/HAOFV_FLASH_ARCHITECTURE.md` | pending |
| `ARCH-BOOTCTRL-01` | BCB 双 lane append/commit 与 Direct A/B test-confirm-revert | `docs/arch/HAOFV_FLASH_ARCHITECTURE.md` | pending |
| `ARCH-OTASTREAM-01` | USB/SD/UART/TDMA 共用 OtaStreamSession，ACK 只确认 durable offset | `docs/arch/HAOFV_FLASH_ARCHITECTURE.md` | pending |
| `REFMEM-PERSIST-01` | RefMem 只持久化部署 package/ref，上电建立新 epoch | `docs/arch/HAOFV_FLASH_ARCHITECTURE.md` | pending |
| `VDC-PERSIST-01` | VDC 只持久化低频 profile，上电从 OFF/CHECKING 重新锁相 | `docs/arch/HAOFV_FLASH_ARCHITECTURE.md` | pending |
| `ARCH-PIOCAT-01` | 动态 PIO 只装载签名 App catalog 中的 program，System Pack 只选择 ID | `docs/arch/HAOFV_FLASH_ARCHITECTURE.md` | pending |
| `DOCS-FLASH-01` | Flash 域架构、TODO、任务进度三类文档的事实边界与变更接口 | `docs/arch/HAOFV_FLASH_ARCHITECTURE.md` | pending |
| `DOCS-TRIPLETFORMAT-01` | 域文档 Architecture、TODO、Task Progress 三件套的最小格式、稳定 ID、状态词汇和文件接口 | `docs/check/DOCS_REGRESSION_PLAN.md` | pending |
| `VDC-PATHMATRIX-01` | Calibration load 生成完整 source/reference observation path matrix；DPLL 运行态只做矩阵索引，禁止沿环推断 | `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md` | pending |

## 分层职责

### Active Object Layer

Active Object 是功能域的运行容器。它负责：

- 事件队列。
- 对外 API。
- 执行预算。
- 生命周期。
- 与 `system_manager` 的资源申请和模式检查。
- 调用内部 Function Block。

建议 Active Object：

```text
OtaAO
TriggerAO
CalibrationAO
VdcSyncAO
TdmaSchedulerAO
DistributedRefMemAO
LoopEngineAO
StorageAO
UiAO
DiagnosticsAO
```

Active Object 不直接暴露内部状态，不允许外部模块直接修改 DomainVector 的状态字段。

### IEC 61499-style Function Block Layer

本项目只采用轻量 IEC 61499 子集，不实现完整 IEC 61499 运行时。这里的 Function Block 只负责本节点或本功能域内部的 FB + ECC 状态机逻辑，不承担跨节点动态部署和跨节点事件网络。

采用内容：

- 固定功能块实例。
- 本节点/本功能域内的静态事件连接。
- Event Input / Event Output。
- Data Input / Data Output。
- ECC 状态机。
- 表驱动状态转移。

不采用内容：

- 动态部署功能块。
- XML/FBT 解析。
- 完整分布式运行时。
- 跨节点 FB 直接调用。
- 跨节点动态事件路由。
- 工程工具链绑定。
- 在硬实时 PIO 路径中执行功能块调度。

Distributed Hard Real-Time Trigger System 的分布式系统能力不由 IEC 61499 分布式运行时实现，而由 Distributed Vector Blackboard / RefMem Sync Layer 实现。换句话说，FB 不跨节点直接调用 FB；跨节点交互只能通过反射内存向量表、命令槽、ACK/NACK、同步帧和共同时间事实完成。

建议功能块：

```text
TriggerFB
OtaFB
StorageFB
UiFB
SafetyFB
DiagnosticsFB
```

#### Function Block 非阻塞规则

Function Block 的 action 只允许做以下事情：

- 校验事件和数据。
- 修改本 FB 私有短状态。
- 写本域 Vector 的 staging/summary 字段。
- 投递异步 job 或设置 busy 状态。
- 立即返回 `FB_RESULT_OK`、`FB_RESULT_BUSY`、`FB_RESULT_ERROR` 或 `FB_RESULT_IGNORED`。

Function Block 禁止在 action 中等待 Flash、SD、USB、锁、队列或长事务完成。耗时事务必须采用：

```text
event -> action 启动 job -> FB_RESULT_BUSY + next_state=self
next tick -> service 查询 job -> BUSY/DONE/FAILED -> 状态迁移
```

该规则适用于 OtaFB、TriggerFB、StorageFB、CalibrationFB、SyncDpllFB 和后续所有业务 FB。

### Time-Synchronized Vector Blackboard Layer

Vector 是数据表，但"时间"由调度器保证。也就是说，Vector 按固定调度阶段更新、快照和提交。

建议阶段：

```text
1. Snapshot Input
2. Dispatch Events
3. Execute Function Blocks
4. Arbitrate Resources
5. Commit Outputs
6. Publish Diagnostics
```

### Distributed Vector Blackboard / RefMem Sync Layer

Distributed RefMem 是 HAOFV 在分布式 RTOS 系统中的核心体现。它不是 IEC 61499 分布式运行时，也不是任意共享变量区，而是多节点共同事实、命令意图、ACK/NACK、版本、质量和证据的静态数据面。

Distributed RefMem 需要吸收 IEC 61499 分布式运行时的优点，但保留静态、可验证、产品化的实现方式。也就是说，它不支持动态部署 FB，却要有“静态分布式应用模型”的概念：系统中有哪些节点、每个节点部署哪些 AO/FB 实例、哪些事件连接跨节点、哪些数据连接进入哪个 slot、哪个 owner 写入、谁消费、如何确认和如何诊断。

从 IEC 61499-style 分布式思想中吸收的内容：

| 借鉴点 | 在 HAOFV 中的落地形式 | 不采用的部分 |
|---|---|---|
| Application model | 静态 `DistributedApplicationMap`，描述应用/profile 元数据、目标插槽集合和模型 CRC bundle。 | 运行时动态部署 application。 |
| Generic node model | 静态 `DistributedGenericNodeTable`，描述 A0-A7 通用插槽基座、硬件身份和基础能力上限。 | 从当前应用装载反推节点能力，或把网分、转台、网关等固化成新节点类型。 |
| Node load model | 静态 `DistributedNodeLoadTable`，把 AO/FB instance 显式加载到 A0-A7 通用插槽。 | 用 `instance_first/count` 把节点绑定到连续实例范围。 |
| FB instance model | 静态 `DistributedFbInstanceTable`，描述可加载 AO/FB 实例、版本、role 和 enable 条件。 | 跨节点动态创建/销毁 FB。 |
| Event connection | 静态 `DistributedEventLinkTable`，把 START、STOP、FIRE_LOAD、DONE、FAULT、ACK/NACK 映射为 command slot、event queue 或 RJ45 frame。 | 跨节点直接事件调用和动态路由。 |
| Data connection | 静态 `DistributedDataLinkTable`，把状态、参数、质量、时间戳、T2 和统计量映射到固定 slot 字段。 | 任意远程变量读写。 |
| RefMem AO slot contract | `DistributedRefMemAO` 由 DataLink、Header/Directory、SlotGuard、DeploymentGate 和 QualityTable 派生字段级 `RefMemSlotContract`，用于校验事实提交、快照、delta 和订阅分发。 | 把 `RefMemSlotContract` 做成绕过 AO/FB 或 RefMemAO 的第二套业务 API，或由业务代码自行拼地址。 |
| Deployment consistency | build id、app map version、hw profile、config CRC、calibration CRC、sync profile CRC 进入 RefMem gate。 | 在线热替换部署。 |
| Execution control | 每个节点本地 AO/FB 执行 ECC；跨节点只传意图、事实和 ACK/NACK。 | 跨节点统一 FB scheduler。 |
| Diagnostics | 每个连接和 slot 有 seq、stale、CRC、late、timeout、drop、last_error 和 evidence。 | 依赖外部 IEC 工具链诊断。 |
| Management command | 使用受控 SCPI/System Pack 写 command/config slot，owner 原子 take/clear。 | 标准 IEC management runtime。 |

核心规则：

- 每个 slot 必须有唯一 writer。
- SCPI/UI/System Pack 只能写 command/config slot 或读取 snapshot。
- state、summary、health、result、quality 和 evidence slot 只能由对应 owner 写入。
- 跨节点同步只传播 slot delta、版本、CRC、stale、ACK/NACK 和小载荷，不传播 OTA payload、日志全文、波形或硬实时边沿。
- 查询必须读取本地快照；slot stale 时返回 stale，不临时跨板阻塞查询。
- VDC/DPLL 提供共同时间事实，RefMem 保存时间事实的版本、质量和证据。
- 所有跨节点事件连接和数据连接必须在静态表中声明，禁止在业务代码中临时拼接远程目标。
- 每个跨节点连接必须定义 source、destination、载荷格式、超时、ACK/NACK、stale 策略和诊断字段。
- RUN 前必须通过 Deployment Gate：节点角色、FB instance version、slot layout、配置 CRC、校准 CRC 和同步 profile 一致。

建议静态表：

| 表 | 内容 | owner |
|---|---|---|
| `DistributedApplicationMap` | 应用/profile 元数据、目标插槽集合和模型 CRC bundle。 | SystemAO / ConfigGate |
| `DistributedGenericNodeTable` | A0-A7 通用插槽、硬件身份、基础能力上限和失效策略。 | SystemAO / ConfigGate |
| `DistributedNodeLoadTable` | role/persona/instance 到通用插槽的装载关系，支持同插槽多实例。 | SystemAO / ConfigGate |
| `DistributedFbInstanceTable` | 可加载 AO/FB 实例、domain、版本、使能条件和健康状态。 | 各节点 SystemAO |
| `DistributedEventLinkTable` | 跨节点事件名、source、destination、传输通道、ACK/NACK、timeout。 | LoopEngineAO / SystemAO |
| `DistributedDataLinkTable` | slot 字段映射、writer、reader、单位、值域、生命周期、snapshot 策略。 | RefMem owner |
| `DistributedDeploymentGate` | build/hw/config/cal/sync/vector layout 一致性门禁。 | ConfigGate |
| `DistributedConnectionQualityTable` | seq、CRC、stale、late、drop、timeout、last_error 和 evidence 索引。 | DiagnosticsAO / RefMem |

这些表不是彼此孤立的配置清单，而是 `DistributedRefMemAO` 通用基础件的输入。对外行为入口仍属于 AO/FB owner、ConfigGate、CommandSlot owner 和 RefMem Sync owner；`RefMemSlotContract` 只规定 `DistributedRefMemAO` 接收和发布这些事实时必须满足的字段级地址、类型、值域、writer、version、timestamp、lifecycle、error、stale 和 guard 约束。业务代码不得绕过 AO/FB owner 或 RefMemAO 直接写裸 RefMem 字段。

分布式系统中，A0-A7 逻辑插槽在一个 active profile / epoch 内必须全环唯一。允许一块物理板同时承载多个不同逻辑插槽，例如一块调试板同时模拟网分和转台；但不允许两块物理板 claim 同一个逻辑插槽。`DistributedRefMemAO` 必须聚合各板 `SlotClaim` 摘要，发现 `CLAIM_CONFLICT/MISMATCH/STALE` 时通过 DeploymentGate 拒绝 required slot 进入 RUN。

典型跨节点连接示例：

| 逻辑连接 | IEC 61499-style 语义 | HAOFV 静态落地 |
|---|---|---|
| A3 START -> A0 LoopEngine | Event connection | `AckCommandSlot.command_seq` + gateway event + ACK/NACK |
| A0 FIRE_LOAD -> A1/A2/A3 core1 | Event + data connection | RJ45 frame + TriggerSlot sequence + late/fault evidence |
| A1 DONE/T2 -> A0 statistics | Event + data connection | TriggerStatus delta + T2 ring summary + StatisticsSlot |
| CAL link delay publish | Data connection | CalibrationSlot active version + delay table CRC |
| SYNC lock quality publish | Data connection | VdcSlot lock_state / e_vdc / stale / quality |

分布式交互主线：

```text
SCPI / UI / System Pack
  -> command/config slot
  -> owner AO/FB consume
  -> Domain Vector update
  -> Distributed RefMem slot publish
  -> RefMem Sync delta
  -> remote node local snapshot
  -> ACK/NACK / stale / evidence
```

因此，分布式系统在 HAOFV 中的表达是：

```text
静态 AO/FB 状态机
+ 分布式反射内存向量表
+ slot owner / command / ACK-NACK / stale / CRC / sequence
+ VDC/DPLL 共同时间事实
```

而不是：

```text
动态部署 FB
+ 跨节点 FB 网络运行时
+ 动态事件路由
+ 完整 IEC 61499 runtime
```

### Calibration Domain

Calibration Domain 是链路测量和校准事实的唯一 owner。它不拥有 TDMA transport，也不拥有
VDC offset/rate/lock；它把 PIO/DMA/IRQ 提供的原始边沿证据转换为带 topology、profile、
schedule、bias 和 generation 绑定的 active calibration。

底层训练和校准总装配为：

```text
CLK/DATA/SYNC physical edge
  -> PIO/DMA/IRQ hardware latch
  -> TDMA bounded training transport/evidence
  -> CalibrationAO / CalibrationFB
  -> CalibrationVector staging -> accepted active generation
  -> VdcSyncAO / SyncDpllFB consume accepted path-delay evidence
```

校准域负责：

- 通过 `*IDN?` 唯一地址和隔离 link probe 生成 directed adjacency，只有单一闭环覆盖全部
  active 节点时才发布 accepted ring order/slot map；NO 映射是该结果的显示/持久化投影。
- MARK/CS、SCK、DATA 统一使用 per-link `measured_link_delay / 2` 物理 base 与 per-destination
  Node offset；三者共用 PIO origin、raw capture、SD evidence、离线相关/SVG、零 offset 基线、
  全量矩阵、动态加载和 residual gate，信号 persona 不得另造校准算法。代码契约见
  `calibration_training_phase.h`。
- EtherCAT DC 风格训练状态、质量统计、freshness 和 accepted/rejected 门禁。
- CLK/DATA/SYNC 双向同时对比、residence、endpoint bias、path-sum、方向不对称性和
  per-link/cumulative delay 的发布边界。
- `t1/t2/t3/t4` 硬件边沿证据与同一 `SYNC` epoch/sequence 的关联；CPU 读取时刻只能是
  diagnostic evidence。
- active/staging calibration、CRC、generation 及 topology/profile 变化后的失效和重训。

TDMA 只负责隔离 topology probe 和训练 persona、`TRAIN_PREPARE/ACK/commit`、PIO/SM/DMA/
core1 资源、RX/window/timeout 和 failure propagation；TDMA START 不测量环序，也不隐式
改写 NO。VDC 只消费 accepted calibration，不把 calibration 测量写入自己的 offset/rate writer。

### TDMA Foundation Domain

TDMA Foundation 是 HAOFV 中的确定性通讯基础件。它不是 VDC 的子模块，也不是 RefMem 的私有同步线程，而是承接上行/下行 TDMA runtime、window/guard、payload registry、adapter、ring seq、miss/late/timeout、completion evidence 和质量摘要的内部主域。

TDMA 与 VDC / RefMem 的关系如下：

```text
VDC Domain
  -> registers VDC_SYNC_SAMPLE / IDLE_BEACON payload
  -> consumes TDMA observation timestamp evidence
  -> owns offset/rate/lock/DCO

RefMem Domain
  -> registers REFMEM_DELTA / ACK_FENCE payload
  -> consumes TDMA data completion evidence
  -> owns distributed fact commit / ACK / fence

TDMA Foundation
  -> owns UP/DOWN ring runtime
  -> owns adapter execution and payload admission
  -> publishes runtime quality/evidence
```

核心规则：

- TDMA 上行/下行环路是基础件，不归 VDC 私有。
- VDC 不能把 TDMA ring 配置就绪当作 DPLL 闭环证据；必须等待硬件 RX/TX timestamp 相关性证明。
- RefMem 不能绕过 payload registry 独占 transport；所有 delta、ACK、fence 都必须走 TDMA payload/window/completion 契约。
- Trigger/Loop/OTA/Diagnostics 可以读取 TDMA quality 或注册低频 payload，但不能绕过 TDMA owner 直接控制 PIO/SM/DMA。
- TDMA 作为 HAOFV system node 装载时必须声明 PIO/SM、DMA、core1 service、adapter、GPIO、UP/DOWN group、MTU、payload whitelist 和逐类 traffic budget，供 DeploymentGate 做资源互斥与流量准入。
- TDMA 吸收 TSN-style time-aware gate、guard band、shaping、backpressure 和逐流 policing；VDC/RefMem 使用硬预留流，配置/OTA/LOG 只能使用 maintenance 或剩余预算，不得产生优先级反转。
- TDMA 的物理速率与周期必须作为同一个离散 operating profile 由 TDMA owner 管理；SCPI 只能 staging，STOP 后 apply，下一次 ARM 才更新 PIO 和调度周期。两板 effective schedule CRC 不一致时必须拒绝帧，禁止运行态单板私自降级。

### T2 预约与分布式时钟分发总装配

T2 不是新的 owner，而是把 Calibration、VDC/DPLL、TDMA、Trigger、RefMem 和 sync_io
串成一条可验证流水线。跨域主线以
`docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` 为 canonical，顶层只保留 owner 和
事实方向：

```text
local_tick_raw + hardware edge latch
  -> Calibration accepted path-delay/bias
  -> VDC DPLL: local_tick_raw <-> vdc_time map
  -> TriggerReservationFB: T_fire_target_vdc
  -> TDMA flight distribution: opaque reservation + READY/NACK
  -> fence with reservation/map/calibration generation
  -> VDC inverse map: T_fire_deadline_local
  -> core1 + sync_io/PIO/DMA local deadline execution
  -> hardware T2_actual_local latch
  -> VDC forward map: T2_actual_vdc / T2_error
  -> RefMem + TDMA completion evidence
```

硬约束：

- `local_tick_raw` 自由运行、单调递增且不被 DPLL 回写；`system_tick` 是 VDC map 的只读
  派生视图，不是通过固定 offset 永久修正的第二个计数器。
- 硬件 latch 不依赖 VDC 已经 `LOCKED`；VDC 使用 raw latch 和 accepted calibration
  形成 DPLL 样本，锁定后再把 raw tick 映射为共同时间。
- Trigger 只拥有预约业务语义；TDMA 只传输 opaque reservation、READY/fence/completion；
  RefMem 只保存共同事实；sync_io/PIO/DMA/IRQ 只执行 deadline 和锁存实际边沿。
- map generation、calibration generation、schedule CRC 或 target mask 在 arm guard 前变化，
  必须重新 PREPARE；arm guard 后不得通过软件临界路径静默改写已装载 deadline。
- VDC 未满足正式质量门禁时，新的预约必须 fail closed；HOLDOVER 是否可预约由 profile
  误差预算决定，不能由 TDMA ring running 状态替代。

### 基础件向上收敛表

顶层只收敛 owner、事实和门禁；算法参数、wire layout、PIO 指令和 profile 容量继续由下列
canonical 文档维护：

| 基础件 | 向上提供的事实 | 上层唯一消费者/决策者 | 顶层禁止的替代实现 |
|---|---|---|---|
| `sync_io` / PIO / DMA / IRQ | `local_tick_raw`、实际边沿 latch、source/resolution/flags、overrun evidence | Calibration、VDC、Trigger/Measure | CPU 读取 timer 或软件完成时刻冒充 edge latch。 |
| Calibration | accepted path-delay、residence、endpoint bias、quality、CRC、generation/freshness | VDC/DPLL 和 T2 gate | TDMA 直接计算 delay，或 VDC 私自测量并写 calibration。 |
| VDC / SYNC DPLL | `VdcMapSnapshot`、offset/rate、LOCKED/HOLDOVER/RELOCK、map generation | TriggerReservationFB、T2 completion mapping | Angle DPLL 或 Trigger 修改 VDC offset/rate。 |
| TDMA Foundation | operating profile、UP/DOWN runtime、payload admission、window/guard、READY/fence/completion quality | 各 domain AO/FB 通过 payload/intent 使用 | 业务域直接控制 PIO/SM/DMA 或把 ring running 当作 VDC lock。 |
| Distributed RefMem | slot owner、command、ACK/NACK、stale、CRC、sequence、completion fact | 各节点本地 AO/FB 和 Diagnostics | 跨节点裸地址写入、跨板查询阻塞实时路径。 |
| T2 / Trigger | `T_fire_target_vdc`、reservation generation、READY mask、fence、`T2_actual_local/vdc`、error | Trigger/Measure 业务状态机 | 以收到帧时刻立即触发，或用软件时刻伪造 T2。 |
| Watchdog / Diagnostics | reset cause、fault stage、owner heartbeat、last snapshot generation | System/DeploymentGate/Report | 看门狗只复位而不保留原因，或由 UI 日志作为唯一故障事实。 |

### Hardware Service Layer

硬件服务层封装 MCU 和外部器件：

```text
drv_flash
sync_io
drv_spi
drv_uart
lcd_st7789
sd_card
watchdog
```

### Hard Real-Time Side Path

同步触发和高速采样的硬实时路径不进入通用功能块调度：

```text
PIO
DMA
IRQ
```

功能块只负责配置、启停、状态摘要和故障处理，真正的边沿捕获、脉冲输出、同步时钟由 PIO/DMA/IRQ 执行。

## 核心原则

### 表负责规则

适合使用表驱动的内容：

- SCPI 命令表。
- IO 引脚功能表。
- PIO/DMA 资源分配表。
- OTA 状态转移表。
- Trigger 模式和动作表。
- UI 页面和菜单表。
- 资源冲突表。
- 错误码和诊断事件表。

### Vector 负责事实

Vector 只保存当前系统事实和摘要：

- 当前状态。
- 命令槽。
- IO 快照。
- 配置快照。
- 资源占用。
- 进度。
- 错误码。
- 健康状态。

Vector 不保存大块数据。OTA 固件块、SD 文件缓存、采样环形缓冲、日志字符串不进入 Vector，只在 Vector 中记录指针、长度、CRC、进度和状态摘要。

### 事件负责意图

SCPI、UI、SD 卡、Bootloader 结果等外部入口只投递事件，不直接修改功能域状态。

示例：

```text
SYST:OTA:BEGIN
  -> ota_manager_post_event(OTA_EVENT_BEGIN)
  -> ota_manager_service()
  -> OTA 内部状态机处理
```

### 事件载荷所有权与生命周期

`fb_event_t` 携带的载荷数据必须明确所有权，防止悬垂指针。架构采用分层策略：

| 载荷大小 | 策略 | 使用场景 |
|---|---|---|
| ≤ 32 B | 内联拷贝到事件结构体内部 | 小命令、配置变更、状态查询 |
| > 32 B（OTA 数据块） | `event_bus` 层做内联拷贝到环形缓冲区 | `OTA_EVENT_DATA_BLOCK` |
| > 32 B（静态/全局） | 发送者持有静态分配内存，事件消费前不释放 | 配置表、校准数据 |
| > 32 B（ISR 产生） | ISR 写入无锁环形缓冲，AO 周期消费 | 触发统计、DMA 完成通知 |

**关键约束**：
- 发送者不得在事件投递后立即释放 payload 内存（除非使用内联模式）
- 消费者在 `fb_execute_fn` 返回后不得再访问 payload
- ISR 上下文只能使用内联模式或无锁原子写入

### Active Object 负责流程

每个复杂功能域作为一个主动对象运行：

- `sync_trigger`：同步触发域。
- `ota_manager`：OTA 域。
- `storage_manager`：SD/文件域。
- `ui_manager`：LCD/按键 UI 域，内部包含 `status_ui` 状态界面渲染模块。
- `diagnostics`：诊断域。

每个域只允许自己修改自己的运行状态。

### 功能块负责逻辑

每个 Active Object 内部可以包含一个或多个 Function Block。Function Block 只处理事件、数据和状态迁移，不直接访问底层硬件。

示例：

```text
OtaAO
  -> OtaFB
  -> FlashJobFB
  -> MetadataFB

TriggerAO
  -> TriggerFB
  -> TriggerReservationFB
  -> SafetyFB
  -> PioControlFB

CalibrationAO
  -> CalibrationFB
  -> CalibrationQualityGateFB

VdcSyncAO
  -> TimestampValidationFB
  -> SyncDpllFB
  -> HoldoverFB / RelockFB

TdmaSchedulerAO
  -> TdmaRingRuntimeFB
  -> TdmaPayloadAdmissionFB
```

### Resource Arbiter 负责互锁

所有共享资源必须通过统一仲裁：

- Flash 擦写。
- LCD/SD 共享 SPI。
- USB CDC OTA 传输。
- PIO/DMA 资源占用。
- OTA 与同步触发实时路径冲突。

### Service Layer 负责硬件

上层组件不直接调用 Pico SDK 硬件 API。硬件访问路径为：

```text
component
  -> service/driver wrapper
  -> Pico SDK / hardware
```

## 推荐目录结构

```text
application/
  app.c                         # 主循环和调度入口

components/
  board_identity/               # 板卡唯一身份与拓扑键
  calibration_manager/          # 校准策略与校准事实
  diagnostics/                  # 诊断、错误码、运行日志
  distributed_config/           # 分布式配置与部署事实
  distributed_refmem/           # 分布式共同事实主域
  event_bus/                    # 轻量事件投递
  led_manager/                  # 产品 LED 策略
  loop_engine/                  # 周期执行与模型循环
  model_turntable/              # 转台业务模型
  ota_manager/                  # OTA Active Object
  product_config/               # 产品配置入口
  resource_arbiter/             # 资源声明与互斥仲裁
  storage_manager/              # SD 卡 / 文件管理
  sync_io/                      # 同步 IO 与硬实时采集/输出
  sync_trigger/                 # 同步触发 Active Object
  system_manager/               # 系统模式与总调度
  tdma/                         # 确定性通讯 foundation domain
  ui_manager/                   # LCD/按键 UI 调度
  vdc_domain/                   # 分布式共同时间主域
  vdc_dpll_manager/             # VDC/DPLL 调度适配

middleware/
  scpi_port/                    # SCPI 命令入口
  u8g2_port/                    # U8G2 显示适配
  usbtmc_scpi_port/              # USBTMC/SCPI 传输适配
  portable_log_port/            # 可移植日志适配
  portable_ota_port/            # portable_ota 产品适配器
  fatfs_port/                   # 后续 SD 文件系统适配

drivers/
  mcu/
    flash/
    i2c/
    spi/
    pio/
    dma/
    uart/
    watchdog/
  external/
    lcd/
    sd_card/

bootloader/
  inc/
  src/

third_party/
  scpi-parser/
  u8g2/
  fatfs/
  portable_ota/                 # 平台无关 OTA 核心库
  freertos/                     # FreeRTOS Kernel（可选）
```

`components/`、`drivers/` 和 `middleware/` 的目录名以上述仓库实际目录为准；本节只描述
架构归属，新增/删除目录必须同步更新对应域 README，不把构建产物目录当作组件事实。

## 功能块模型

### 基础接口

建议定义最小功能块接口：

```c
typedef enum {
    FB_RESULT_IGNORED = 0,
    FB_RESULT_HANDLED,
    FB_RESULT_BUSY,
    FB_RESULT_ERROR,
} fb_result_t;

typedef struct {
    uint32_t id;
    uint32_t type;
    uint32_t timestamp_ms;
    uint32_t payload_size;
    union {
        uint8_t  inline_data[32];   // ≤32B 内联拷贝
        const void *external_ptr;   // >32B 外部指针（需满足所有权约定）
    } payload;
} fb_event_t;

typedef struct {
    uint32_t instance_id;
    uint32_t state;
    uint32_t error_code;
} fb_context_t;

typedef fb_result_t (*fb_execute_fn)(fb_context_t *context, const fb_event_t *event);
```

### ECC 状态转移表

Function Block 的核心是表驱动的 ECC（Execution Control Chart）状态机。每条规则包含当前状态、触发事件、前置条件、执行动作和下一状态：

```c
typedef struct {
    uint32_t     current_state;
    uint32_t     event;
    bool         (*condition)(fb_context_t *ctx, const fb_event_t *evt);
    fb_result_t  (*action)(fb_context_t *ctx, const fb_event_t *evt);
    uint32_t     next_state;
} fb_ecc_entry_t;
```

ECC 执行引擎遍历规则表，找到首条匹配的状态+事件+条件组合，执行动作并转移状态。无匹配规则时事件被静默忽略（返回 `FB_RESULT_IGNORED`）。

当前已实现的规模：
- **OtaFB**：15 条 ECC 规则，覆盖 9 个状态 + 10 个事件
- **TriggerFB**：规则数量必须由代码中的 `TRIG_ECC_TABLE_COUNT` 或等价 `sizeof(s_ecc_table) / sizeof(s_ecc_table[0])` 导出，不允许文档手写固定数字作为事实源

TriggerFB 规则数量以 `sizeof(s_ecc_table) / sizeof(s_ecc_table[0])` 或等价代码符号为准；历史规模数字仅为快照，非事实源，不能作为产品化规模判断。

TriggerFB / OtaFB / SyncDpllFB 的 ECC 表必须增加以下静态检查：

- 检查 `(state, event, condition_id)` 是否重复。
- 检查 `SET_*` 类配置事件是否走统一 default 规则或显式策略，避免为每个状态复制直接透传规则。
- 检查每个事件的覆盖策略：显式处理、默认处理、或显式标记为 ignored。
- 当规则数量、状态数量或事件数量超过当前基线阈值时，必须更新架构风险评估和对应域设计文档。

具体代码示例见实施指南。

### OTA 功能块

```text
OtaFB
  Event Inputs:
    BEGIN
    DATA_BLOCK
    END
    ABORT
    VERIFY
    COMMIT
    TICK

  Event Outputs:
    READY
    PROGRESS
    FAILED
    PENDING_REBOOT

  Data Inputs:
    SystemVector snapshot
    OtaVector command slot
    Flash job status

  Data Outputs:
    OtaVector summary
    Metadata update request
```

### Trigger 功能块

```text
TriggerFB
  Event Inputs:
    ARM
    DISARM
    FIRE
    CONFIG_SET
    SAMPLE_START
    SAMPLE_STOP

  Event Outputs:
    ARMED
    FIRED
    BUSY
    FAULT

  Data Inputs:
    TriggerVector command slot
    PIO/DMA status
    Safety status

  Data Outputs:
    TriggerVector summary
    PIO control request
```

### Safety 功能块

`SafetyFB` 负责把安全事件转换为最高优先级控制意图，例如禁止 OTA、停止采样、关闭输出、进入故障模式。

```text
SafetyFB
  Event Inputs:
    FAULT_DETECTED
    RESOURCE_CONFLICT
    WATCHDOG_WARNING
    TRIGGER_TIMEOUT

  Event Outputs:
    ENTER_FAULT
    STOP_OUTPUTS
    BLOCK_OTA
    DIAG_EVENT
```

## 数据模型

数据模型由 `SystemVector` 和各功能域 `DomainVector` 组成。功能块通过快照读取 Vector，通过对应 Active Object 的提交接口更新本域摘要，避免跨模块直接写状态字段。

### SystemVector

`SystemVector` 是系统总表，只放全局摘要和仲裁结果。

建议字段：

```c
typedef struct {
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t system_mode;
    uint32_t resource_locks;
    uint32_t fault_summary;
    uint32_t trigger_summary;
    uint32_t ota_summary;
    uint32_t storage_summary;
    uint32_t ui_summary;
    uint32_t diagnostics_summary;
} system_vector_t;
```

### TriggerVector

`TriggerVector` 描述同步触发域的事实和配置快照。

建议字段块：

```c
typedef struct {
    uint32_t state;
    uint32_t input_snapshot;
    uint32_t output_state;
    uint32_t pio_status;
    uint32_t dma_status;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    uint32_t last_trigger_result;
    uint32_t error_code;
} trigger_vector_t;
```

字段块契约：

| 字段块 | 唯一 writer | 生命周期 | 是否需要快照 |
|---|---|---|---|
| `state/runtime` | RTOS + 双核主线下由 core1 `TriggerAO/TriggerFB` 写 | RUN / MAINTENANCE 运行态 | 是 |
| `pio_dma_status` | `REALtime` / PIO / DMA owner 写 | ARM 到 DISARM，或底层维护动作周期 | 是 |
| `sequence_cfg` | core0 `LoopEngineAO` / `TriggerAO` 通过命令槽提交，core1 ACK 后生效 | plan upload -> active plan -> release | 是 |
| `biss_cfg` | `COMMunity` / BISS-C owner 或 Trigger 兼容层提交 | config enabled -> disabled | 是 |
| `timestamp/t2` | realtime capture / VDC timestamp service 写 | ring slot / sequence window | 是 |

新增 TriggerVector 字段时，代码和文档必须同时标注 writer、value domain、lifecycle、snapshot-needed。禁止把 TriggerVector 当作自由扩展的全局结构体；跨核读取必须使用 sequence、一致性快照或双缓冲。

### OtaVector

`OtaVector` 描述 OTA 域的状态、进度和错误。

建议字段：

```c
typedef struct {
    uint32_t state;
    uint32_t target_slot;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t crc32_expected;
    uint32_t crc32_running;
    uint32_t progress_permille;
    uint32_t boot_flags_summary;
    uint32_t error_code;
} ota_vector_t;
```

### StorageVector

建议字段：

```c
typedef struct {
    uint32_t sd_mounted;
    uint32_t current_file_id;
    uint32_t file_size;
    uint32_t read_offset;
    uint32_t error_code;
} storage_vector_t;
```

### UiVector

建议字段：

```c
typedef struct {
    uint32_t page_id;
    uint32_t selected_item;
    uint32_t key_event;
    uint32_t dirty_flag;
    uint32_t error_code;
} ui_vector_t;
```

### Vector 摘要字段编码规范

多个 Vector 使用单个 `uint32_t` 字段存储位掩码摘要。以 `fault_summary` 和 `trigger_summary` 为例：

```c
// fault_summary bits:
#define SYS_FAULT_TRIGGER_MASK   (1u << 0)   // 触发域故障
#define SYS_FAULT_OTA_MASK       (1u << 1)   // OTA 域故障
#define SYS_FAULT_FLASH_MASK     (1u << 2)   // Flash 操作故障
#define SYS_FAULT_WATCHDOG_MASK  (1u << 3)   // 看门狗警告
#define SYS_FAULT_RESOURCE_MASK  (1u << 4)   // 资源死锁

// trigger_summary bits:
#define TRIG_SUMMARY_ARMED       (1u << 0)
#define TRIG_SUMMARY_RUNNING     (1u << 1)
#define TRIG_SUMMARY_FAULT       (1u << 2)
#define TRIG_SUMMARY_SEQ_MODE    (1u << 3)
```

### 错误码空间划分

每个域使用独立错误码范围，避免跨域错误混淆：

| 域 | 错误码范围 | 示例 |
|---|---|---|
| 通用 | 0 | `NONE` |
| OTA | 1-99 | 见 `docs/ota/OTA_SYSTEM_DESIGN.md` 15 种 OTA 错误码 |
| Trigger | 100-199 | 1=非法参数, 2=资源忙, 3=PIO/DMA 配置失败, 10=ENC target=0, 11=非法编码器引脚 |
| Flash | 200-299 | 擦除/写入/读回校验失败 |
| Storage | 300-399 | SD 卡挂载/读写/文件系统错误 |
| UI | 400-499 | LCD 初始化/刷新失败 |
| System | 500-599 | 资源死锁、模式切换冲突 |
| TDMA | 600-699 | ring reason code 9 项：`NONE` / `BAD_CONFIG` / `EVIDENCE_MISSING` / `DIRECTION_CONFLICT` / `ADAPTER_MISSING` / `TIMESTAMP_MISSING` / `PAYLOAD_STARVATION` / `WINDOW_MISSED` / `RESOURCE_CONFLICT`；见 `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md:805-819`。 |

多故障同时发生时：`error_code` 记录最严重错误，`fault_summary` 位掩码保留所有故障位。

### 字段值域约定

| 字段 | 类型 | 值域 | 说明 |
|---|---|---|---|
| `progress_permille` | `uint32_t` | 0-1000 | 千分比进度 |
| `state` | `uint32_t` | 对应域状态枚举 | 不跨域混用 |
| `error_code` | `uint32_t` | 见错误码空间 | 0 = NONE |
| `sequence` | `uint32_t` | 单调递增 | metadata transaction sequence |
| `timestamp_ms` | `uint32_t` | 系统启动后的毫秒数 | 约 49 天回绕 |

时间差必须使用回绕安全写法：

```c
int32_t diff_ms = (int32_t)(t1_ms - t0_ms);
```

禁止直接使用 `t1_ms > t0_ms` 判断先后关系。`timestamp_ms` 只用于短窗口调度、预算和状态新鲜度判断；长时间运行事实必须扩展为 `epoch_seconds`、`run_id`、高位 epoch 或等价字段。VDC、DPLL 和 T2 质量评估不得把无符号回绕差值直接当作相位误差。

## 写权限规则

| 数据 | 允许写入者 |
|---|---|
| `TriggerVector.state` | 只能 `sync_trigger` 写 |
| `OtaVector.state` | 只能 `ota_manager` 写 |
| `SystemVector.system_mode` | 只能 `system_manager` 写 |
| `resource_locks` | 只能 `resource_arbiter` 写 |
| `command_slot` | SCPI/UI/Storage 可通过 API 写入 |
| `summary` | 对应功能域写，SystemVector 汇总读取 |
| 大块数据 | 不进入 Vector |

禁止跨模块直接写结构体字段。所有写入通过 API 完成，例如：

```c
bool ota_manager_post_event(const ota_event_t *event);
bool sync_trigger_post_event(const trigger_event_t *event);
void system_vector_get_snapshot(system_vector_t *snapshot);
```

### 跨核写权限矩阵

| 数据 / 通道 | writer | reader | 同步机制 |
|---|---|---|---|
| Trigger command slot | core0 System/SCPI/LoopEngine | core1 TriggerAO | command sequence + ACK/NACK |
| Trigger runtime state | core1 TriggerAO/TriggerFB | core0 SCPI/UI/Diagnostics/RefMem | seqlock 或双缓冲快照 |
| PIO/DMA capture summary | core1 realtime owner | core0 Measure/Diagnostics/Storage | ring buffer + sequence |
| RefMem node heartbeat | 节点 owner / core0 RefMem service | 本节点和外部节点 | slot sequence + stale 判断 |
| Flash lockout state | core0 Flash/Resource owner，core1 lockout poll | System/Diagnostics | atomic flag + ACK timeout |

单字段标志必须使用 `__atomic_load_n` / `__atomic_store_n` 或 DMB 屏障；多字段事实必须使用 seqlock、双缓冲或等价 sequence/version 机制。core1 不允许直接调用 SD、Flash、FatFs、SCPI、UI 渲染或长格式化日志路径。

## 调度模型

### 主循环驱动

当前裸机版本由主循环驱动：

```text
app_run_once()
  scpi_port_service()
  event_bus_service()

  system_manager_service()
    snapshot_inputs()
    dispatch_events()
    trigger_ao_service(high_priority_budget)
    ota_ao_service(low_priority_budget)
    storage_ao_service(low_priority_budget)
    ui_ao_service(low_priority_budget)
    diagnostics_ao_service()
    publish_system_vector()
```

关键要求：

- 同步触发实时路径继续交给 PIO/DMA/IRQ。
- OTA、UI、SD 卡属于管理域，必须限时运行。
- OTA 写 Flash 前必须进入维护允许状态。
- 查询类命令读快照，不触发现场硬件访问。

### 调度预算定义

`ao_service(budget)` 中的 budget 是在单次调用中允许消耗的最大时间，以**微秒（μs）**为单位。每个 AO 在 service 入口记录时间戳，在耗时操作后检查是否超出预算。

| 等级 | 预算 | 适用 AO | 说明 |
|---|---|---|---|
| `HIGH_PRIORITY` | 500 μs | TriggerAO | 只做状态处理，不阻塞 |
| `NORMAL` | 1000 μs | SystemAO, IoFrontendAO | 一般管理操作 |
| `LOW_PRIORITY` | 2000 μs | OtaAO (维护模式), UiAO | Flash 分片、LCD 刷新 |
| `OTA_DEDICATED` | 5000 μs | OtaAO (OTA 专用模式) | 大块 Flash 操作，必须周期喂狗 |
| `BACKGROUND` | 500 μs | DiagnosticsAO | 低频诊断 |

budget 表示 RTOS/AO 单次连续运行时间片，不是硬实时边沿的绝对 deadline。AO 超预算时必须记录 `BUDGET_OVERRUN` 或 `WATCHDOG_WARNING` 诊断事件，保存当前 progress，然后主动 yield 或返回 busy。禁止通过阻塞等待、循环追赶或扩大临界区来“补回”已经错过的调度时间。

### Flash 异步操作模型

W25Q32 扇区擦除需 60-300ms，页编程需 0.7-3ms。架构要求所有 Flash 操作拆分为异步 job，每个 job 在单次 `ao_service()` 内只执行一个可控分片，立即返回：

```
AO 创建 job → flash_job_scheduler → 执行一个分片
  → FLASH_JOB_BUSY（还有后续分片）
  → FLASH_JOB_DONE（全部完成）
  → FLASH_JOB_FAILED（失败）
```

| Job 类型 | 单次分片 | 估算耗时 | 说明 |
|---|---|---|---|
| `ERASE_SLOT` | 1 个 4 KB sector | < 300ms | 单次预算内完成一个 sector |
| `PROGRAM_BLOCK` | 256 B (1 page) | < 3ms | W25Q32 页编程 |
| `READBACK_VERIFY` | 256 B - 1 KB | < 1ms | 纯读操作 |
| `VERIFY_IMAGE_CRC` | 1 KB - 4 KB | < 1ms | 软件 CRC32 |
| `WRITE_METADATA` | 1 个 metadata 副本 | < 400ms | 擦+写+读回，原子事务 |

**不可中止阶段**：Metadata 正在擦写时不能立即中止（会导致双副本全部损坏）。处理方式为设置 `abort_pending` 标志，当前写入事务完成后检查并进入 ABORTED 状态。

**XIP 安全约束**：RP2350 从 W25Q32 XIP 执行代码时，Flash erase/program 是双核系统最高风险临界区，必须满足以下硬规则：

- 所有 Flash erase/program 只能由 core0 发起。
- 进入 erase/program 前必须申请 `SYS_RESOURCE_FLASH_BUS` 或等价 Flash bus 资源锁。
- core0 必须向 core1 发出 park/lockout 请求，并等待 core1 ACK 进入 `WAIT_FOR_FLASH` / `PARKED_FOR_FLASH` 状态。
- core1 ACK 超时必须进入 FAULT 或拒绝 Flash job，禁止继续 erase/program。
- core1 park 状态下不得访问 XIP 代码、XIP 常量、Flash resident 跳转表或任何会触发 Flash bus 取指/取数的路径；必须运行 RAM resident 安全循环或硬件自治路径。
- Flash 临界区内禁止 USB CDC/USBTMC、SD、LCD 和长日志格式化路径访问 Flash resident 代码路径。
- 退出 Flash 临界区后，core0 必须释放资源锁、恢复 core1，并记录 Flash job 结果、耗时和 lockout 状态到诊断快照。

## 系统模式

建议定义：

```c
typedef enum {
    SYSTEM_MODE_BOOT = 0,
    SYSTEM_MODE_RUN,
    SYSTEM_MODE_MAINTENANCE,
    SYSTEM_MODE_OTA,
    SYSTEM_MODE_FAULT,
} system_mode_t;
```

模式约束：

| 模式 | 允许行为 |
|---|---|
| `BOOT` | 上电初始化、自检、配置加载 |
| `RUN` | 同步触发、采样、SCPI 查询和轻量配置 |
| `MAINTENANCE` | 停止实时任务后允许 SD、OTA、配置写入 |
| `OTA` | 允许 Flash 擦写和升级状态机运行 |
| `FAULT` | 停止危险输出，保留诊断和有限维护入口 |

## 错误恢复与故障处理

### 故障分级

| 级别 | 名称 | 行为 | 恢复方式 |
|---|---|---|---|
| **F0** | 可恢复错误 | 记录 error_code，继续运行 | 自动清除或 SCPI `*CLS` |
| **F1** | 域级故障 | 域进入 FAULT 状态，其他域正常运行 | 显式 CLEAR 事件 |
| **F2** | 系统级故障 | 进入 `SYSTEM_MODE_FAULT`，停止危险输出 | 需复位或维护模式恢复 |
| **F3** | 致命故障 | Watchdog 复位 | 硬件自动复位 |

### FAULT 模式行为

```
进入条件：
  1. SafetyFB 发出 ENTER_FAULT
  2. Resource Arbiter 检测到不可恢复的资源冲突
  3. Watchdog 警告累积超过阈值

FAULT 模式下：
  - 所有危险输出停止（Trigger DISARM）
  - PIO/DMA 停用
  - OTA 禁止（Flash 操作中止）
  - 保留 SCPI 和诊断接口

恢复方式：
  1. 硬件复位（最可靠）
  2. 通过 SCPI 清除故障（如故障原因已消除）
  3. Bootloader 启动后自动进入 RUN 模式
```

### 事件队列溢出处理

环形队列满时丢弃最旧事件，记录 `overflow_count`，确保系统不会因队列满而死锁。

### Watchdog 策略

| 层次 | 机制 | 超时 | 行为 |
|---|---|---|---|
| 硬件 WDT | RP2350 硬件看门狗 | 1-2 s | 系统复位 |
| Task WDT (RTOS) | 每个 AO task 周期性标记 alive | 500 ms | 记录超时 task，可选系统复位 |
| OTA WDT | Flash 操作分片间喂狗 | 每次分片后 | 防止 Flash 操作饥饿看门狗 |
| CPU 心跳 (Trigger) | 可选，周期性检查 ARM 态 | 1 s | 超时自动 DISARM |

## 资源仲裁

建议资源位：

```c
typedef enum {
    SYS_RESOURCE_FLASH = 1u << 0,
    SYS_RESOURCE_SPI0  = 1u << 1,
    SYS_RESOURCE_USB   = 1u << 2,
    SYS_RESOURCE_PIO0  = 1u << 3,
    SYS_RESOURCE_PIO1  = 1u << 4,
    SYS_RESOURCE_PIO2  = 1u << 5,
    SYS_RESOURCE_DMA   = 1u << 6,
    SYS_RESOURCE_LCD   = 1u << 7,
    SYS_RESOURCE_SD    = 1u << 8,
} system_resource_t;
```

典型互锁：

| 操作 | 需要资源 |
|---|---|
| SCPI 在线 OTA | `FLASH + USB` |
| SD 卡离线 OTA | `FLASH + SPI0 + SD` |
| LCD 刷新 | `SPI0 + LCD` |
| SD 文件读取 | `SPI0 + SD` |
| PIO 输入采样 | `PIO0 + DMA` |
| PIO 输出触发 | `PIO1` |
| AUX PIO | `PIO2` |

### SPI 总线共享仲裁

HAOFV 只要求共享总线通过 Resource Arbiter 串行化访问。当前最小系统的实现示例是 LCD 和 SD 共用 SPI0，具体 GPIO、CS 和板级连线以 `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` 或对应产品板约束为准。

| 操作 | 持有资源 | 冲突操作 | 处理 |
|---|---|---|---|
| LCD 刷新 | `SPI0 + LCD` | SD 卡读写 | SD 等待 LCD 释放 |
| SD 卡读写 | `SPI0 + SD` | LCD 刷新 | LCD 跳过本轮刷新，保持 dirty |
| OTA Flash 写 | `FLASH` | LCD 大块刷新 + SD 读写 | 二者均暂停 |
| OTA SD 离线 | `FLASH + SPI0 + SD` | LCD 刷新 | LCD 暂停 |

硬件约束：任意时刻只能有一个设备的 CS 有效。切换设备前必须等待上一个设备的最后一个 SCK 周期完成。

## OTA 设计

OTA 使用独立 `OtaVector`、`OtaAO` 和 `OtaFB`。

```text
SCPI/UI/SD
  -> ota_ao_post_event()
  -> OtaVector command slot
  -> OtaAO
  -> OtaFB ECC 状态转移表
  -> flash_job_scheduler
  -> drv_flash
  -> metadata / inactive slot
```

### portable_ota 库集成

项目已实现独立的 `third_party/portable_ota/` 库，作为 OTA 域的底层引擎被 `OtaAO/OtaFB` 调用，分层关系为：

```text
HAOFV 层:  OtaAO / OtaFB / OtaVector
             ↓ 调用
middleware: portable_ota_port/  (产品适配器，类型转换 + 布局断言)
             ↓ 调用
third_party: portable_ota/  (平台无关 OTA 核心)
             ├─ pota_core      — 状态机 + 公共 API
             ├─ pota_package   — manifest 解析
             ├─ pota_metadata  — 双副本 metadata 读写
             ├─ pota_crc32     — CRC32 计算
             └─ pota_compat    — 状态/错误/结果映射表
```

关键集成约束：
- `OtaFB` 不直接调用 `pota_*` 函数，通过 `middleware/portable_ota_port/` 适配
- 产品层 API 使用产品自有类型，不暴露 `pota_*` 类型
- `portable_ota_port` 使用 `_Static_assert` 保证布局兼容
- Bootloader 只链接 metadata/crc32/package 子集，不链接 session 接收路径

### OTA 状态

```text
IDLE
CHECK_PERMISSION
ERASE_SLOT
RECEIVING
VERIFYING
MARK_PENDING
READY_TO_REBOOT
FAILED
ABORTED
```

### OTA 事件

```text
BEGIN
DATA_BLOCK
END
ABORT
VERIFY
COMMIT
ROLLBACK
TICK
```

### SCPI OTA 命令

SCPI 命令只投递事件，不直接操作 Flash：

```text
SYST:OTA:STAT?
SYST:OTA:BEGIN <size>,<crc32>
SYST:OTA:PBEGIN <size>,<crc32>    # 统一 package 传输
SYST:OTA:DATA #<block>
SYST:OTA:END
SYST:OTA:ABOR
SYST:OTA:PROG?
SYST:OTA:BOOT
SYST:OTA:COMM
SYST:OTA:SLOT?
SYST:OTA:RES?
SYST:OTA:TXN?                     # copy transaction 状态
SYST:OTA:MODE?                    # COPY_TO_ACTIVE 或 DIRECT_AB
SYST:OTA:TARG?                    # 下一次 OTA 目标 slot
SYST:OTA:CAP?                     # Bootloader/OTA 能力位
```

禁止 SCPI 直接调用 Flash 擦写函数。

### Bootloader 启动策略

```text
如果存在 pending slot:
    如果 pending slot 校验通过且尝试次数未超限:
        启动 pending slot
    否则:
        回滚 confirmed slot
否则:
    启动 confirmed slot
```

Bootloader 支持两种模式：
- **COPY_TO_ACTIVE**（默认）：校验 Slot B，复制到 Slot A，从 Slot A 启动
- **DIRECT_AB**（已验证）：直接从 pending slot 启动，App 自检后 commit

### Metadata 双副本无效 Failsafe

Bootloader 读取 metadata 时必须把“双副本均无效”作为独立致命状态处理。如果两个 metadata copy 均 CRC 失败，或版本/transaction 序列不可恢复冲突，Bootloader 禁止启动未知镜像。

强制恢复路径：

1. 首选进入 USB MSD / BOOTSEL 可恢复状态。
2. 如产品板启用 SD factory package，则允许从 SD `/factory/` 恢复最小 factory image。
3. 恢复状态必须通过 LED 模式、boot result metadata 或 App 恢复后的 SCPI 查询暴露。

发布验证必须包含 metadata 双副本损坏注入用例，确认系统不会跳转到未确认镜像。

### 写保护策略

| 区域 | 保护级别 | 机制 |
|---|---|---|
| Bootloader | **绝对保护** | OTA code 硬编码拒绝擦除 Bootloader 区域（`ota_partition.h` 边界检查） |
| App Slot A | **条件保护** | 仅当 Slot A 不是 confirmed 时才允许擦写 |
| App Slot B | 正常擦写 | OTA 目标 slot |
| Metadata | **规则保护** | 双副本，绝不擦除最后一个有效副本 |
| Product Config | **预留保护** | 独立写入 API，与 OTA 流程分离 |

### Golden Image 策略

- 当前：Factory UF2 = Bootloader + Slot A App + 空白 metadata；板端通过 BOOTSEL + UF2 做终极恢复
- 规划：Scratch 区预留 640 KB 可存放最小 Golden Image，或通过 SD 卡 `/factory/` 提供恢复包

## Trigger / Realtime 分层

`TRIGger` 是产品业务的动作域，不应改名为 `REALtime`。它回答“产品测试流程要执行什么动作”，负责现场测试中的运行意图、模式切换、开始、停止、暂停、继续、终止、业务门禁和业务状态闭环。

`REALtime` 是底层实时能力域，不替代 `TRIGger`。它回答“底层实时资源如何执行和验证”，负责 PIO/DMA/IRQ、SEQ/ENC/PCNT、底层 IO、时序调试、产测维护和硬实时能力观测。

两者的关系是：

```text
SCPI / UI / System Pack
        |
        v
TRIGger product action domain
  - select mode
  - start / stop / pause / resume / abort
  - validate product gate
  - bind active sequence
  - publish business state
        |
        v
REALtime capability domain
  - configure low-level realtime engine
  - arm/disarm PIO/DMA/IRQ resources
  - expose validation and maintenance hooks
  - publish low-level timing status
        |
        v
PIO / DMA / IRQ
  - deterministic edge/capture execution
```

| 层级 | 对外域 / 内部对象 | 职责 |
|---|---|---|
| 产品业务动作域 | `TRIGger:*` / `TriggerAO` / `TriggerFB` / `TriggerVector` | 产品 RUN 意图、运行门禁、业务状态机、active sequence 绑定、状态摘要和故障归因。 |
| 底层实时维护域 | `REALtime:*` / `sync_io` / realtime component | PIO/DMA/IRQ 资源、SEQ/ENC/PCNT 低层模式、IO 维护、validation、产测和调试观测。 |
| 硬实时执行层 | PIO / DMA / IRQ | 边沿捕获、脉冲输出、倒计时、READY/T2 捕获和最小事实回写。 |

命名规则如下：

- `TRIGger` / `TriggerAO` / `TriggerFB` / `TriggerVector`：只用于产品业务动作域。
- `REALtime`：只用于底层实时维护、验证、产测和低层能力观测。
- `sync_io`、PIO、DMA、IRQ：只用于硬件服务和硬实时执行层。
- `SEQ_STEP`、`ENC_COUNT`、`PCNT` 等底层实时模式可作为 `TRIGger` 业务动作的执行基础，但不直接成为现场上位机主流程的产品动作域。
- 上位机现场测试流程应调用 `TRIGger:*` 表达业务动作；开发、产测、HIL 和底层调试工具才调用 `REALtime:*`。
- `TRIGger:*` 可以请求或锁定 `REALtime` 能力，但不能绕过 `REALtime` owner 直接改 PIO/DMA/IRQ。
- `REALtime:*` 可以暴露底层状态和维护动作，但不能绕过 `TRIGger` owner 直接改变产品 RUN 状态。

因此，HAOFV 中的 `Trigger` 命名用于产品业务动作域；只有当文档讨论底层实时维护接口、实时能力或硬实时执行资源时，才使用 `REALtime`。

同步触发使用独立 `TriggerVector`、`TriggerAO` 和 `TriggerFB`。底层确定性时序仍由 `sync_io`、PIO、DMA 和 IRQ 实现。

```text
SCPI/UI/Button
  -> trigger command/event
  -> TriggerAO
  -> TriggerFB ECC 状态表
  -> sync_io / PIO / DMA
```

### 语义通道接口

Trigger 域的应用层接口必须使用稳定语义通道，而不是把任意 GPIO 暴露给 SCPI/UI：

```text
Input semantics:   TRIG_IN / RJ45_TRIG_IN / ARM_IN / EXT_CLK_IN / GATE_IN
Output semantics:  TRIG_OUT / PULSE_OUT / RJ45_TRIG_OUT / SYNC_CLK_OUT
```

GPIO16..GPIO23 的实际映射属于 board profile 和 `sync_io` 的职责。`TriggerAO`、`TriggerFB`、SCPI 和 UI 只能表达语义意图，不能把产品功能设计成任意 GPIO 交叉开关。

### 当前 board profile 示例：主触发口与 AUX 功能口

下表描述当前固件/板级 profile 中的语义 IO 划分示例，用于说明 `TriggerAO` 只依赖语义通道。最终 GPIO、连接器、电气约束和隔离边界以 `docs/hardware/` 与 `docs/sync/SYNC_IO_ARCHITECTURE.md` 为准。

| 接口 | 角色 | 语义 |
|---|---|---|
| 主输入 IN0..IN3 / active input group | 模式本地高速输入 | `TRIG_IN`、`RJ45_TRIG_IN`、`GATE_IN`、编码器 A/B/Z（仅 IN0..IN2）、后续计数/采样输入 |
| 主输出 OUT0..OUT3 / active output group | 模式本地高速输出 | `TRIG_OUT`、`PULSE_OUT`、`RJ45_TRIG_OUT`、`SEQ_OUT[3:0]` |
| AUX0..AUX3 / GPIO26..29 | 跨模式框架功能 | `ARM_IN`、`EXT_CLK_IN`、`SYNC_CLK_OUT`、`AUX3_TX/BISS_DATA_OUT` |

### 模式资源约束

| 模式 | 应用层资源约束 |
|---|---|
| `SEQ_STEP` | OUT0..OUT3 被序列输出总线独占；独立主总线输出应返回 busy 或在 ARM 前关闭。`ARM_IN` 位于 AUX0。`SYNC_CLK_OUT` 位于 AUX2，不占用序列输出总线。历史 `MARK:*` 命令兼容到 OUT3/RJ45，armed 时应拒绝。 |
| `ENC_COUNT` | IN0/IN1/IN2 分别作为 A/B/Z；IN3 的硬件定义是 `RJ45_TRIG_IN`，不被 ENC 软件定义占用。`ARM_IN` 位于 AUX0，不再与 B 相冲突。 |
| `IDLE` | 语义输出可由即时命令使用；语义输入只做采样/诊断或配置预览。 |

### Board Profile 迁移约束

当前固件中仍可能保留调试最小系统的兼容宏。迁移规则是：临时 GPIO 宏只允许存在于 board profile 或兼容层，不能成为 HAOFV 顶层规则；硬件冻结后，语义 IO 到物理 IO 的映射必须由产品板约束、`sync_io` profile 和验证矩阵共同确认。历史兼容命令只能作为语义入口，不能重新定义独立硬件输出。

### 触发模式扩展表

| 模式 | 输入占用 | 输出占用 | PIO | CPU | 状态 |
|---|---|---|---|---|---|
| `SEQ_STEP` (mode=1) | IN0 + 可选 IN3 | OUT0-3 | pio1/sm0 + DMA | ARM 后为零 | ✅ |
| `ENC_COUNT` (mode=2) | IN0/IN1/IN2 | OUT0 | pio1/sm0 + DMA | ARM 后为零 | ✅ |
| `GATE_LEVEL` (mode=3) | IN0 + IN3 | OUT0 | pio0/sm2 + pio1/sm0 | ARM 后为零 | 规划 |
| `ARM_SINGLE` (mode=4) | AUX0 | OUT0 | pio2/sm0 + pio1/sm0 | 每次触发 IRQ | 规划 |
| `FREE_BURST` (mode=5) | IN0 | OUT0-1 | pio1/sm0/sm2 | ARM 后为零 | 规划 |

新增模式只需在静态模式表中追加一行：

```c
static const trig_mode_entry_t s_mode_table[] = {
    { TRIG_MODE_SEQ_STEP,  SYS_RESOURCE_PIO1 | SYS_RESOURCE_DMA,
      (1u<<TRIG_STATE_IDLE), TRIG_CFG_SEQ_TABLE },
    { TRIG_MODE_ENC_COUNT, SYS_RESOURCE_PIO1 | SYS_RESOURCE_DMA,
      (1u<<TRIG_STATE_IDLE), TRIG_CFG_ENC_TARGET },
    // 新模式的入口：追加一行即可
};
```

### Trigger 域拒绝 OTA 条件

```text
如果 capture_running == true
或 sync_clock_running == true
或 trigger_armed == true
则 SYST:OTA:BEGIN 返回 busy
```

## Bootloader 与 OTA 安全链

推荐第一阶段采用：

```text
App 接收 OTA 包
  -> 写 inactive/staging 区
  -> App 校验 raw bin CRC 和向量表
  -> 写 metadata pending
  -> Bootloader 校验向量表和镜像
  -> 启动或搬运 App
  -> 新 App 自检通过后 commit
```

Bootloader 负责：

- 读取 metadata 双副本。
- 选择启动 Slot。
- 校验 raw bin CRC 和 App 向量表。
- 设置 VTOR/MSP 并跳转 App，或搬运 staging 到固定运行区。
- 处理 pending、commit、rollback。

App 负责：

- 接收固件。
- 分块写入。
- 计算 CRC。
- 设置 pending。
- 自检通过后 commit。

Bootloader 不引入 FreeRTOS。启动链路越小越可靠，OTA apply/rollback 必须在最小依赖下运行。Bootloader 不集成 SD/FatFs/UI/SCPI/网络。

## 表驱动使用范围

| 模块 | 表 |
|---|---|
| SCPI | 命令表 |
| IO | 引脚功能表 |
| PIO | PIO 资源分配表 |
| OTA | 状态转移表、错误码表 |
| Trigger | 模式表、动作表、状态转移表 |
| Function Block | Event/Input/Output 映射表、ECC 状态表 |
| Resource Arbiter | 资源冲突表 |
| UI | 页面表、菜单项表 |
| Diagnostics | 错误码/事件码表 |

## 配置管理

### 配置层次

| 层次 | 存储位置 | 内容 | 修改方式 |
|---|---|---|---|
| **编译期** | `config/project_config.h` | 版本号、循环周期、看门狗超时 | 代码修改 + 重新构建 |
| **板级** | `boards/rp2350_trig/inc/board_config.h` | 引脚映射、外设实例 | 代码修改 + 重新构建 |
| **产品参数** | W25Q32 Product Config 区 `0x350000` | 校准数据、序列号 | 专用 SCPI 命令或生产工具 |
| **运行时** | TriggerVector 配置快照 | 触发参数、序列表 | SCPI/UI 配置命令 |
| **OTA 策略** | OTA Metadata | slot 状态、版本策略、启动模式 | OTA 流程自动管理 |

Product Config 区（64 KB）当前预留，后续存放校准系数、序列号、硬件版本号和默认触发参数。

## 诊断数据流

### 数据分类

| 数据类别 | 存储位置 | 访问方式 |
|---|---|---|
| 当前错误码 | `SystemVector.error_code` + 各 DomainVector | SCPI `SYST:ERR?` / `STAT:*?` |
| 错误历史 | 诊断环形缓冲（RAM, ~128 B） | SCPI `SYST:ERR:COUN?` |
| 运行统计 | 各 DomainVector 计数字段 | SCPI `STAT:TRIG?` / `STAT:SYNC?` |
| OTA 审计 | metadata（Flash） | SCPI `SYST:OTA:RES?` / `SYST:OTA:TXN?` |
| 固件标识 | build id（Flash, 32 B） | SCPI `SYST:FW:BUILD?` |
| 故障日志 | Scratch 区（预留） | 后续实现 |

### DiagnosticsAO 接口

```c
void diagnostics_report_event(uint32_t domain, uint32_t error_code,
                              uint32_t timestamp_ms, const char *context);
void diagnostics_publish_heartbeat(void);
void diagnostics_get_summary(diagnostics_summary_t *summary);
```

系统心跳由 DiagnosticsAO 周期发布（1 Hz），包含运行时间、各域状态摘要、资源锁占用和故障锁存状态。RTOS 模式下各 AO task 周期性标记 alive，SystemAO 监控超时任务。

## FreeRTOS 集成约束

### 架构关系

```
HAOFV 是架构，
FreeRTOS 是调度器，
OSAL 是隔离层，
PIO/DMA/IRQ 是实时底座。
```

### 任务模型

| FreeRTOS Task | 承载对象 | 优先级 |
|---|---|---|
| `task_trigger` | `TriggerAO` | 5（最高，单核 RTOS 路径） |
| `task_system` | `SystemManagerAO` | 4 |
| `task_ota` | `OtaAO` | 3 |
| `task_io_frontend` | SCPI/UI 输入入口 | 3 |
| `task_ui` | `UiAO` | 2 |
| `task_storage` | `StorageAO` | 2 |
| `task_diag` | `DiagnosticsAO` | 1 |

产品化 RTOS + 双核 AMP 路径中，`TriggerAO` / `TriggerFB` 状态机不再由 core0
上的 FreeRTOS `task_trigger` 承载，而是由 core1 实时核的受限循环承载。core0 上的
SCPI/UI/Storage/OTA 只能投递 Trigger 事件或读取 TriggerVector 快照；core1 负责
消费事件队列、执行 TriggerFB ECC 状态迁移、刷新 PIO/DMA 运行态和捕获 READY/T2
相关状态。该路径仍遵守 HAOFV 分层：FreeRTOS 是控制核调度器，core1 是 TriggerAO
运行容器，PIO/DMA/IRQ 是硬实时边沿执行层。

基础件任务壳保持同一 owner 方向：core0 的 `task_calibration`、`task_vdc_sync`、
`task_dpll` 和 `task_refmem_sync` 只推进各域控制面、Vector/快照和异步 job；core1 的
`TdmaSchedulerAO`/realtime loop 独占 TDMA fast path、PIO/DMA persona、窗口和 deadline
服务。`SyncDpllFB` 仍是 VDC offset/rate/lock 唯一 writer，CalibrationAO 的测量结果通过
accepted snapshot 进入 VDC，不能由 task 或 SCPI 旁路写入。

### 必须遵守的约束

- 业务组件禁止直接包含 `FreeRTOS.h`，只能通过 `osal/` 接口
- Bootloader 永远裸机，不引入 FreeRTOS
- 所有阻塞等待必须有 timeout
- ISR 只投递事件或设置标志，不执行复杂业务
- 硬实时路径（PIO/DMA/IRQ）不进入 RTOS 任务调度
- RTOS + 双核 AMP 下，TriggerAO/TriggerFB 状态机归 core1；core0 不能直接改写触发域内部状态
- RTOS + 双核 AMP 下，core0 对 Trigger 域只能写命令槽、投递事件或读取快照，不能跨核直接写 runtime state
- Vector Blackboard 调度阶段和写权限规则在 RTOS 下保持不变；共享字段必须使用 atomic、DMB、seqlock、双缓冲或等价机制
- Flash 操作临界区必须使用 Flash bus 资源锁 + core1 park/lockout ACK；未获得 ACK 时禁止 erase/program

### 当前进度

RTOS/OSAL、双核 AMP 和分布式触发任务划分详见 `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`。
当前产品化主线已经从单核 `task_trigger` 过渡到 RTOS + 双核 AMP：core0 负责控制面，
core1 负责 TriggerAO/TriggerFB 实时 owner，SCPI 通过反射内存、命令槽、事件队列和
owner 状态机闭环。任务划分、栈/堆水位、VDC/DPLL、CAL/SYNC 和验证待办以
`docs/arch/RTOS_HAOFV_TODO.md` 为准。

## 测试策略

### 测试分层

| 层次 | 框架 | 范围 | 示例 |
|---|---|---|---|
| **单元测试** | ARM GCC compile/object-build gate | portable_ota 库 | `test_portable_ota_core.c` |
| **集成测试** | 板端 SCPI smoke | AO 状态转移 | `STAT:TRIG?` 查询验证 |
| **OTA 闭环测试** | `ota_board_validate.py` | 完整 OTA + 负向矩阵 | 8+ 负向注入用例 |
| **时序验证** | 示波器 + `loopback_test.py` | PIO 延迟、脉宽精度 | GPIO22→GPIO16 回环 |
| **发布门禁** | `release_check.py` | 构建产物、安全检查 | 14 项检查 |
| **长稳测试** | 24h 连续运行 | 内存泄漏、看门狗 | RTOS Step 9 |

### 资源仲裁死锁检测

编译期通过资源依赖分析检查潜在死锁环。运行时资源申请带超时：超时后报告 `RESOURCE_ACQUIRE_TIMEOUT` 诊断事件并返回失败，不无限阻塞。

### AO 单元测试隔离

AO 单元测试通过 mock OSAL 和驱动层实现隔离：注入事件 → 执行 `ao_service()` → 验证状态转移和域向量变化。测试模式下可不限预算。

## 当前基础件收敛顺序

顶层的实现顺序按“先形成可观测事实，再形成共同时间，最后开放预约执行”收敛；各项
详细完成状态以对应 domain TODO 和代码为准：

1. `sync_io`/PIO/DMA/IRQ 形成自由运行 `local_tick_raw`、真实 edge latch、source/
   resolution/flags 和 overrun evidence；CPU 只搬运 descriptor。
2. Calibration Domain 完成 `CLK/DATA/SYNC` 训练、双向时间传递、residence、endpoint bias、
   path-delay active/staging、generation/freshness 和 accepted/rejected gate。
3. TDMA Foundation 以唯一 `TdmaSchedulerAO` 运行 UP/DOWN ring、payload registry、operating
   profile、window/guard、READY/fence 和 completion evidence；host 只做维护态编排和只读监控。
4. VDC/DPLL 消费 accepted calibration 与硬件 latch，形成 `VdcMapSnapshot`、offset/rate、
   `LOCKED/HOLDOVER/RELOCK` 和 map generation；`SyncDpllFB` 是 offset/rate/lock 唯一 writer。
5. Distributed RefMem 通过固定 slot、command、ACK/NACK、stale、CRC 和 sequence 发布共同
   事实；不绕过 TDMA payload registry，也不承担 DPLL 计算。
6. TriggerReservationFB 消费 VDC map 生成 `T_fire_target_vdc`，经 TDMA opaque flight、READY/
   fence 和 generation gate 分发；arm guard 后由 core1/sync_io 装载本地 deadline。
7. PIO/DMA/IRQ 锁存 `T2_actual_local`，VDC 映射为 `T2_actual_vdc`，Trigger/Measure 形成
   `T2_error`，RefMem/TDMA 发布 completion evidence。
8. DeploymentGate 统一检查 node/profile/config/calibration/schedule/vector CRC、PIO/SM/DMA/
   IO/IP claims、payload capacity 和 owner 唯一性；通过后才允许 RUN。
9. Watchdog/Diagnostics 保存 reset cause、fault stage、heartbeat、last owner snapshot 和
   generation；长稳测试必须能把复位原因与对应域 evidence 关联。

## 风险与约束

| 风险 | 约束 |
|---|---|
| Vector 退化成全局变量 | 严格写权限，所有写入走 API |
| 表项回调隐藏阻塞 | 表只描述规则，耗时动作由 service 分步执行 |
| OTA 影响实时触发 | OTA 只能在维护/OTA 模式运行，Flash 操作限时调度 |
| LCD/SD 共享 SPI 冲突 | 所有 SPI 使用走 Resource Arbiter + CS 互斥 |
| SCPI 与日志共用 USB CDC | OTA 期间暂停周期日志或降低输出 |
| 完整 IEC 61499 过重 | 只采用固定功能块、静态事件连接和表驱动 ECC |
| 架构过度设计 | 依照“raw latch → calibration → VDC/DPLL → TDMA → T2”顺序逐层验收；未通过下层证据门禁时，上层只保留诊断能力 |
| Flash 擦写阻塞主循环 | Flash job 分步异步执行，XIP 临界区保护，操作前后喂狗 |
| 事件 payload 悬垂指针 | 内联拷贝 + 所有权约定 + ISR 只用无锁原子 |
| Bootloader 被意外覆盖 | 写保护 + 边界硬编码检查 |
| FreeRTOS 破坏硬实时 | PIO/DMA/IRQ 永远不进入 RTOS 任务调度 |
| GPIO 配置漂移 | 产品 pinout 冻结后移除开发板兼容宏 |

## 最终结论

当前项目推荐采用：

```text
整体：融合型主动对象功能块向量架构
上层：Active Object / Domain Service
中层：轻量 IEC 61499 Function Block 子集
底层：Time-Synchronized Vector Blackboard
局部：表驱动
硬件访问：RTE-like Service Layer
安全边界：Resource Arbiter
实时保证：PIO/DMA + 管理域限时调度
OTA 引擎：portable_ota 平台无关库
RTOS 隔离：OSAL 抽象层
共同时间：Calibration + VDC/DPLL + TDMA Foundation
预约执行：T2 Reservation + TriggerReservationFB + sync_io
```

这套方案比单纯表驱动更适合中型嵌入式系统，也比完整 AUTOSAR、完整 IEC 61499 运行时或重型 RTOS 架构更轻。它适合 RP2350_TRIG 后续扩展 OTA、SD 卡、LCD、SCPI、第三方库、Bootloader 和 RTOS。
