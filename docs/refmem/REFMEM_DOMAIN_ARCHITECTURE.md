# Distributed RefMem 内部主域架构

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/interface/DTC100_SCPI_COMMAND_PLANNING.md`
Last updated: 2026-08-13

本文档定义 DTC100 / RP2350_TRIG 在 HAOFV 下的 Distributed Vector Blackboard / RefMem Sync 内部主域。RefMem Domain 不是对外 SCPI 主域，也不是产品业务动作域，而是分布式系统的内部基础主域，负责把多节点共同事实、静态分布式应用模型、命令意图、ACK/NACK、版本、质量和证据组织成可验证的数据面。

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

## 静态分布式模型

RefMem Domain 吸收 IEC 61499-style 分布式运行时的优点，但保留静态、可验证、产品化的实现方式。

### 节点模型硬规则

RefMem 的底座只固定 **A0-A7 八个通用节点**。A0-A7 是 slot 和同步协议中的通用 node id，不代表永久固定的产品角色。

模型节点、脉冲分发节点、链路切换节点、仪表控制节点、模拟网分节点、模拟转台节点、网关节点都不是额外的固定节点类型，也不是独立于 A0-A7 之外的表空间。它们是加载到 A0-A7 某个通用节点上的 role、persona 或 AO/FB instance：

```text
A0-A7 generic node
  + NodeRoleMap
  + persona / feature_mask
  + DistributedFbInstanceTable
  -> board / pulse_distributor / link_switcher / instrument_controller
     / gateway / model_vna / model_turntable / model_dut / test_agent
```

在不冲突的情况下，同一个 A0-A7 通用节点可以同时载入多个逻辑实例。例如一个节点可以同时承载 `board` + `gateway`，或 `model_vna` + `test_agent`，也可以在 IO 与时序资源允许时同时承载 `pulse_distributor` + `link_switcher`。是否允许并存由 `DistributedDeploymentGate` 判定，至少检查资源、IO、时序、owner、slot writer、事件连接和数据连接是否冲突。

因此，`NodeSlot[8]` 只描述八个通用节点的新鲜度、心跳、角色摘要和故障摘要；具体节点承载真实板卡、脉冲分发、链路切换、仪表控制、网关、模型网分或模拟转台，由静态分布式应用模型决定。

| 借鉴点 | RefMem Domain 落地形式 | 不采用的部分 |
|---|---|---|
| Application model | 静态 `DistributedApplicationMap`，描述 A0-A7 通用节点以及加载到节点上的 role、persona 和实例。 | 运行时动态部署 application。 |
| FB instance model | 静态 `DistributedFbInstanceTable`，描述每个节点上的 AO/FB 实例、版本、role、enable 条件和共存冲突规则。 | 跨节点动态创建/销毁 FB。 |
| Event connection | 静态 `DistributedEventLinkTable`，把 START、STOP、FIRE_LOAD、DONE、FAULT、ACK/NACK 映射为 command slot、event queue 或 RJ45 frame。 | 跨节点直接事件调用和动态路由。 |
| Data connection | 静态 `DistributedDataLinkTable`，把状态、参数、质量、时间戳、T2 和统计量映射到固定 slot 字段。 | 任意远程变量读写。 |
| Deployment consistency | `DistributedDeploymentGate` 聚合 build id、hw profile、config CRC、calibration CRC、sync profile CRC、layout version 和实例共存冲突检查。 | 在线热替换部署。 |
| Diagnostics | `DistributedConnectionQualityTable` 记录 seq、CRC、stale、late、drop、timeout、last_error 和 evidence index。 | 依赖外部 IEC 工具链诊断。 |

### DistributedApplicationMap

`DistributedApplicationMap` 描述一套静态分布式应用如何装载到 A0-A7 八个通用节点上。它是产品配置的一部分，不是运行时热部署脚本。

| 字段 | 含义 | 约束 |
|---|---|---|
| `application_id` | 分布式应用编号。 | 由上位机配置或 System Pack 生成。 |
| `application_version` | 应用模型版本。 | RUN 前必须和各节点 active config 一致。 |
| `layout_version` | RefMem 表布局版本。 | 必须匹配 `DistributedVectorTable` header。 |
| `node_id` | A0-A7 通用节点号。 | 只允许 0-7。 |
| `node_uuid` | 节点硬件身份。 | 用于防止 A0-A7 逻辑号错绑实体板。 |
| `role_mask` | 节点当前角色集合。 | 例如 `board`、`pulse_distributor`、`link_switcher`、`instrument_controller`、`gateway`、`model_vna`、`model_turntable`。 |
| `persona_mask` | 节点装载的人格/能力集合。 | 一个节点可同时装载多个不冲突 persona。 |
| `instance_first/count` | 本节点实例表范围。 | 指向 `DistributedFbInstanceTable`。 |
| `hw_profile_crc` | 硬件约束摘要。 | 和当前板级约束、IO 能力一致。 |
| `config_crc` | 业务配置摘要。 | 和 Loop/Trigger/Interface 配置一致。 |
| `required` | 节点是否为当前应用必需。 | 必需节点 stale 或 missing 时禁止 RUN。 |
| `fail_policy` | 节点失效策略。 | `STOP`、`HOLDOVER`、`DEGRADE`、`REPORT_ONLY`。 |

规则：

- A0-A7 是唯一固定节点空间。
- 脉冲分发、链路切换、仪表控制、gateway、model_vna、model_turntable、test_agent 等都是加载实例，不扩展固定节点数量。
- 一个节点同时装载多个实例时，必须通过 `DistributedDeploymentGate` 的冲突检查。
- 逻辑实例可以禁用，但禁用实例仍应保留版本、原因和最后一次健康状态，便于报告闭环。

### DistributedFbInstanceTable

`DistributedFbInstanceTable` 描述运行在各节点上的 AO/FB 实例。这里的 FB 是 HAOFV 的本地功能块，不是跨节点动态调用对象。

| 字段 | 含义 | 约束 |
|---|---|---|
| `instance_id` | 全局实例编号。 | 在当前 `application_id` 内唯一。 |
| `node_id` | 实例所在 A0-A7 节点。 | 必须存在于 `DistributedApplicationMap`。 |
| `domain` | 所属主域。 | `SYSTEM`、`TRIG`、`CAL`、`SYNC`、`MEAS`、`REFMEM` 等。 |
| `ao_type/fb_type` | AO/FB 类型。 | 用于版本兼容和配置校验。 |
| `instance_name` | 实例名。 | 例如 `A0.TriggerAO`、`A1.PulseDistributorAO`、`A2.LinkSwitcherAO`、`A3.InstrumentControllerAO`。 |
| `version` | 实例实现版本。 | RUN 前检查兼容范围。 |
| `enable_condition` | 启用条件。 | 由 mode、persona、feature、配置 CRC 共同决定。 |
| `resource_claim` | 资源占用。 | Flash、SD、USB、PIO、DMA、LCD、RJ45、core1 时间片等。 |
| `io_claim` | IO 占用。 | SMA、RJ45、SP8T、SP2T、BiSS-C、UART/RS485 等。 |
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
| `target_node_mask` | 目标节点集合。 | 支持单播、多播或广播到 A0-A7。 |
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

### DistributedDeploymentGate

`DistributedDeploymentGate` 是 RUN 前的一票否决表。它聚合静态模型、版本、资源、IO、时间和校准/同步质量，判断当前系统是否允许进入触发运行。

| 检查项 | 内容 | 失败处理 |
|---|---|---|
| `layout_check` | `layout_version`、slot offset、slot size、directory CRC。 | 拒绝 RUN。 |
| `node_check` | 必需 A0-A7 节点 online、node_uuid、role/persona 匹配。 | 拒绝 RUN 或按 fail_policy 降级。 |
| `instance_check` | required AO/FB instance 存在、版本兼容、enable 条件满足。 | 拒绝 RUN。 |
| `resource_check` | Flash、SD、USB、PIO、DMA、core1、RJ45 等资源无冲突。 | 拒绝冲突实例组合。 |
| `io_check` | SMA/RJ45/SP8T/SP2T/BiSS-C/UART/RS485 等 IO claim 无冲突。 | 拒绝 RUN 或拒绝实例启用。 |
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
| NodeSlot[8] | `0x3000` | 4 KB | A0-A7 通用节点的 node_id、role、persona、heartbeat、local_state、error_code、stale_count | 各节点 owner |
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
```

SCPI callback 只能读取 RefMem snapshot 或写 command/config slot，不能临时触发跨板查询，也不能直接修改 state、summary、result、health、quality 或 evidence slot。

## 当前实现现状

当前代码中 `components/distributed_refmem/` 仍是组件骨架：

- `distributed_refmem.h`
- `distributed_refmem.c`

它已经维护本地 64 KB `DistributedVectorTable`、header、node slot、core vector 和 runtime protection snapshot，但尚未形成完整 RefMem Domain owner，也未拆出 application model、event link、data link、deployment gate、connection quality、sync protocol 和 command ACK 子模块。

## 目标代码形态

后续建议收敛为：

```text
components/distributed_refmem/
  CMakeLists.txt
  inc/
    refmem_domain.h
    refmem_vector_table.h
    refmem_application_model.h
    refmem_sync.h
    refmem_command.h
    refmem_quality.h
  src/
    refmem_domain.c
    refmem_vector_table.c
    refmem_application_model.c
    refmem_sync.c
    refmem_command.c
    refmem_quality.c
```

旧 `distributed_refmem.h/.c` 可以在过渡期保留为兼容 wrapper，最终收敛到 `refmem_domain_*` 和 `refmem_vector_table_*`。
