# RefMem Event Plane 子域架构（静态域）

Status: Draft
Domain: REFMEM
Canonical: `docs/refmem/event/REFMEM_EVENT_ARCHITECTURE.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

> 本版按“Event Plane 目前只负责静态域，不碰实时域”重写。本子域隶属 Distributed RefMem 主域，目录 `docs/refmem/event/`；父域 canonical 为 `REFMEM_DOMAIN_ARCHITECTURE.md`。静态事件模型归本域；运行时分发、运输、接收、执行、deadline、`TriggerRun` 和 ACK 归 RefMem/TRIGGER，不在本域。

## 1. 主域定位

Event Plane 是 HAOFV 的**静态事件模型子域**，不是运行时事件总线，也不是实时执行域：

```text
SCPI / UI / System Pack
        ↓ intent
Distributed RefMem 控制面（command slot / ACK）
        ↓ runtime
TRIGGER（实时域：T2 预测、TriggerRun、core1/PIO）

Event Plane（静态域）
  -> EventCatalog / EventMapTable / AliasRegistry / EventPack / EventVector
  -> 供 RefMem / TRIGGER / Interface 读取
```

它回答：

```text
有哪些事件码、类型、flags、payload schema
每个 code 映射到哪个 action_id / owner
外部命令的别名是什么
部署包版本/CRC/generation 是什么
EVG/source 的静态席位和 target policy 是什么
```

它不回答：

```text
事件什么时候发送、怎么运输、谁接收
deadline 怎么算、core1 怎么装填、PIO 何时出边沿
命令 ACK 谁写、TriggerRun 什么状态
```

这些分别归 RefMem（命令/ACK）和 TRIGGER（实时执行）。Event Plane 只提供静态输入。

## 2. HAOFV 符合性

| HAOFV 原则/硬约束 | Event Plane 落点 | 判定 |
|---|---|---|
| **表负责规则** | EventCatalog / EventMapTable / AliasRegistry / EventPack 静态表 | 符合（设计） |
| **Vector 负责事实** | EventVector 只存静态 pack/catalog/map/alias/EVG 席位 | 符合（设计） |
| **事件负责意图** | 运行期 intent 由 RefMem/TRIGGER 处理；Event Plane 只提供静态目录 | 符合（边界） |
| **事件载荷所有权** | 不适用：Event Plane 不承载运行时 payload | 不适用 |
| **Active Object 负责流程** | 不适用 AO/FB；`EventPackService` 是静态 owner，运行在 `task_refmem_sync` STOP 上下文 | 符合（设计） |
| **Function Block 负责逻辑** | 不适用：EVG/EVR/dispatch 归 RefMem/TRIGGER | 不适用 |
| **Resource Arbiter 负责互锁** | 只参与 EventPack activation；运行时仲裁归 owner | 符合（设计） |
| **Service Layer 负责硬件** | 不适用：无硬件路径 | 不适用 |
| **写权限规则** | `EventPackService` 是 EventVector 唯一 writer；运行态不写 | 符合（设计） |
| **分布式共同事实** | 静态模型供 RefMem/TRIGGER；运行时事实不归本域 | 符合（边界） |
| **分布式共同时间** | 不适用：不计算/不分发时间 | 不适用 |
| **分布式确定性通讯** | 不适用：不定义 TDMA class/transport | 不适用 |
| **静态部署** | EventPack staging/validate/activation/rollback；路径/previous 见 §4.1 | 符合（设计） |
| **FB 非阻塞** | 不适用：无运行时 FB | 不适用 |
| **顶层安全硬约束** | 无 Flash/PIO/实时路径；只读静态表 | 符合（设计） |
| **运行时事件处理（方案 B）** | `EventRuntimeService` 是 `DistributedRefMemAO` sub-service；非阻塞 handler；不接 POSITION | 设计符合（待实现） |
| **运行时载荷（方案 B）** | `CONTROL_EVENT` 只带 inline 小载荷；无 bulk | 设计符合（待实现） |
| **运行时写权限（方案 B）** | EventRuntimeService 写 EventStatus/EventLog；ACK 归 RefMem | 设计符合（待实现） |
| **运行时运输（方案 B）** | CONTROL_EVENT 用 TDMA class；R3 联审 | 部分（待联审） |

结论：**静态域职责符合 HAOFV**；运行时分发/实时执行不在静态基线，不能拿本域文档宣称“事件已分发/已执行”。本表前段判定静态基线；后四行方案 B 运行时为“部分（设计）”，R6 单独评审，不能沿用静态“不适用”。

## 3. 职责边界

Event Plane 负责：

- `EventCatalog`：event code、类型、flags、payload schema、默认 target policy 的静态定义。
- `EventMapTable`：`event_code → action_id/owner` 的静态映射。
- `AliasRegistry`：外部命令 → canonical code、默认 target、response policy。
- `EventPack`：上述静态表的版本化部署包、CRC、generation、activation/rollback。
- `EventVector`：静态事实（pack/catalog/map/alias/EVG 席位）。
- 静态校验：code/alias/map/target policy/EVG 席位一致性。

Event Plane 不负责：

- 事件发送、运输、接收、排队、去重、late/expired。
- `EVENT_INJECT`/`EVENT_ABORT` 的运行时执行。
- TDMA class、buffer、seat、优先级、payload budget。
- POSITION/T2 预测、deadline、`TriggerRun`、core1/PIO 边沿。
- 命令 ACK/NACK、command_seq、event_id 运行时分配。
- EventStatus/EventLog 的运行时写者（如需要，由 RefMem/TRIGGER 提供）。
- 业务动作执行、硬件访问、Flash/OTA/Storage 事务。

## 4. 静态构件

### 4.0 EventPackService（静态 owner）

`EventPackService` 是 Event Plane 的唯一静态 owner：

- 运行在 core0 `task_refmem_sync` 的 STOP 期 maintenance 上下文；**不是 AO/FB**，不新增任务、不新增实时路径。
- 负责 EventPack stage/validate/activate/rollback、EventVector 写、静态查询（`STATus?`/`MAPPing?`）。
- 只写 EventVector/EventPack；不写 command slot/ACK（由 `DistributedRefMemAO` 写）、不写 TriggerRun、不执行事件。
- 入口：`EVENT_CONFIGURE` / `EVENT_VALIDATE` / `EVENT_ACTIVATE`（经 RefMem command slot）。
- 若未来升级为独立任务，必须补 task/stack/priority 预算、ResourceArbiter 准入和顶层注册。

### 4.1 EventPack

EventPack 是 Event Plane 的唯一部署产物，header 至少包含：

```text
magic / version / generation /
catalog_count / map_count / alias_count /
crc32
```

- 默认 static const pack（`event_pack_default`）；产品可用 `/refmem/event/event_pack.bin` 覆盖，由 StorageAO 读写。
- manifest 可选 `event_pack` 行（schema/version/CRC）；缺失时使用默认 static pack，不阻塞启动。
- previous pack 保存为 `/refmem/event/event_pack.prev.bin`；activation 前把当前 active 复制到 previous，再原子替换 active。
- staging buffer 为 `EventPackService` 私有；validate 通过后才允许 activation。
- rollback 只切换 active/previous descriptor 与 bytes，不改 catalog/map 的运行期语义；失败保持旧 active。
- 不放进 RefMem 64 KB 表；不承载运行时 payload。
- 只在 STOP 期 stage/validate/activate/rollback；RUN 中只读。

### 4.2 EventCatalog

| 字段 | 含义 |
|---|---|
| `event_code` | 控制事件码，首版候选 8 bit，由 `EVENT-CODE-01` 冻结 |
| `event_class` | 静态分类（TRIGGER/SYNC/FAULT/CAL/INJECT），不表示运行时运输 class |
| `flags` | 静态能力位（是否允许 immediate、是否需要 required 等） |
| `payload_schema` | 小型 inline 参数 schema |
| `default_target_policy` | 默认目标策略，供 RefMem/TRIGGER 解析 |

`POSITION` 不使用本表；它是 TRIGGER 的实时位置事件，不在 Event Plane 的静态 code 空间。

### 4.3 EventMapTable

| 字段 | 含义 |
|---|---|
| `event_code` | catalog 中的 code |
| `action_id` | 目标 action 的静态 id |
| `owner_id` | action owner（AO/FB/域）静态 id |
| `required` | 是否 required |
| `target_policy` | 默认 target 解析策略 |
| `generation/crc` | 映射版本 |

Event Plane 只提供映射；运行期由 RefMem/TRIGGER 查表并执行。缺映射时，运行期 owner 必须拒绝，不能由 Event Plane 兜底执行。

### 4.4 AliasRegistry

| 字段 | 含义 |
|---|---|
| `alias_command` | 外部 SCPI 命令名 |
| `canonical_code` | 映射到的 event code |
| `default_target_policy` | 别名默认目标 |
| `response_policy` | legacy `1` / canonical `event_id,state` |
| `owner` | 运行期承接的 owner（RefMem/TRIGGER） |
| `deprecation` | 弃用状态 |

AliasRegistry 是别名的单一事实源；Interface 只生成/消费 SCPI 宏，不另写语义。

### 4.5 EventVector（静态）

| 字段 | writer | 值域 | 生命周期 | snapshot |
|---|---|---|---|---|
| `pack_version` | EventPackService | u32 | STOP | 是 |
| `catalog_generation` | EventPackService | u32 | STOP | 是 |
| `map_crc` | EventPackService | u32 | STOP | 是 |
| `alias_crc` | EventPackService | u32 | STOP | 是 |
| `event_code_count` | EventPackService | u32 | STOP | 是 |
| `evg_slot` | EventPackService（来自 deployment/claim） | A0-A7 或 `INVALID` | STOP/epoch | 是 |
| `target_policy` | EventPackService | mask policy 枚举 | STOP | 是 |

`EventPackService` 是 EventVector 的唯一 writer。存放位置：`components/event_plane/src/event_vector.c` 的本地 static `s_event_vector`，使用 seqlock/atomic、CRC/generation；读 API 为 `event_plane_get_static_snapshot()`。不进 RefMem 64 KB 表。

EventVector 不存 `session_state`、queue、last_error、counters、log。这些运行态如需要，由 RefMem/TRIGGER 的 status/log 提供，不归 Event Plane。

### 4.6 EventRuntimeService（方案 B，未实施）

`EventRuntimeService` 是方案 B 的运行时 handler，和 `EventPackService` 分开：

- **归属**：`DistributedRefMemAO` 的 sub-service，不单独成 AO；service tick 由 RefMem AO 调度，生命周期随 RefMem AO；R5 前不新增任务。
- 运行在 core0 `task_refmem_sync`；不是独立 AO/FB。
- 只处理 `CONTROL_EVENT`（`EVENT_INJECT`/`EVENT_ABORT`）；**永不处理 POSITION**。
- 不 take command slot、不写 ACK、不建 lease/epoch；`command_seq` 归 RefMem。
- 分配 `event_id`（EVG 队列）并写 EventStatus/EventLog；结果回 RefMem，ACK 由 `DistributedRefMemAO` 写。
- 执行只 post 给目标 owner/ActionDispatcher；不直调 handler、不操作 PIO。

运行态/权限矩阵：

| command | STOP | ARMED/RUN | 维护/FAULT | 约束 |
|---|---|---|---|---|
| `EVENT_INJECT` | 允许 | 按 code/permission | 维护类允许 | 必须过 EventMap；POSITION → `NOT_MY_DOMAIN` |
| `EVENT_ABORT` | 允许 | 允许 | 允许 | 只中止 EventRuntimeService 的 CONTROL_EVENT/session |
| `EVENT_CONFIGURE/VALIDATE/ACTIVATE` | 允许 | 拒绝 | 仅维护 | 归 `EventPackService` |

Handler 契约：

| 项 | 规则 |
|---|---|
| 入参 | command snapshot（type/code/target_mask/required/payload/command_seq） |
| 出参 | `ACCEPTED/BUSY/REJECTED` + reason + `event_id`（若分配） |
| 非阻塞 | 不做 TDMA 发送、不等待、不写 Flash |
| 重复 | 同 `command_seq` 返既有 result，不重复入队 |
| POSITION | 直接 `REJECTED(NOT_MY_DOMAIN)`；不接收、不转交 |
| ACK | 只回 result；ACK/NACK 由 `DistributedRefMemAO` 写 |

### 4.7 EventStatus / EventLog（运行时，方案 B，未实施）

`EventStatus` 是控制运行态，**不是 Vector**：

| 字段 | writer | 值域 | snapshot |
|---|---|---|---|
| `session_state` | EventRuntimeService | `IDLE/CONFIGURED/ACTIVE/ABORTED/FAULT` | 是 |
| `pending_count` | EventRuntimeService | 0..有界容量 | 是 |
| `last_command_seq` | EventRuntimeService | RefMem command_seq 副本 | 是 |
| `last_event_id` | EventRuntimeService | EVG event_id | 是 |
| `last_error` | EventRuntimeService | `event_reason_t` | 是 |
| `submitted/acked/rejected` | EventRuntimeService | u32 计数 | 是 |
| `queue_depth` | EventRuntimeService | 有界 | 是 |

`EventLog` 是有界环：`code/seq/event_id/command_seq/target/execute_at/actual_at/result/reason`；容量 `EVENT_LOG_CAPACITY`，溢出计数；需要持久证据时 STOP 后由 StorageAO 导出。`EventStatus`/`EventLog` 不放进 EventVector、不放进 64 KB 表。

- `session_state` 是 **控制会话**（EventSession：configured/active/aborted），不是 TRIGGER 的 `TriggerRun` armed/running/fired；两者不互相复制。
- EventStatus/EventLog 使用 seqlock/atomic；读 API 为 `event_plane_get_runtime_snapshot()` / `event_plane_get_log_entry()`；只读本地，不做跨板阻塞查询。

### 4.8 R0 运行时契约（方案 B，设计）

#### event_id

- `event_id` 32 bit，`0` 保留为无效；`(epoch, event_id)` 在 epoch 内唯一，分配后不复用。
- 分配者：`EventRuntimeService` 的 EVG 队列；`command_seq` 仍由 RefMem 分配。
- 回绕按 `int32_t diff = (int32_t)(id1 - id2)`；耗尽时 `REJECTED(EVENT_ID_EXHAUSTED)`，不回绕复用。
- 一个 `EVENT_INJECT` 在 E0 只产生 1 个 event；`EVENT_CONFIGURE/VALIDATE/ACTIVATE` 产生 0 个。
- `EventStatus.last_event_id` 和 `EventLog` 的 `event_id/command_seq` 双键是唯一映射。

#### EVENT_ABORT 语义

```text
ACCEPTED -> ABORT_REQUESTED -> ABORTED / ABORT_TIMEOUT
```

- target 可以是 `event_id` 或 session_id；未知 target → `REJECTED(UNKNOWN_EVENT)`。
- 幂等：重复 ABORT 返回既有终态，不重复下发。
- 只停止新的 CONTROL_EVENT dispatch；在途事件交 owner 处理，owner 可在自己的状态机中止；不承诺原子回滚。
- ABORT 不写 TriggerRun、不碰 POSITION；证据写 EventLog。

#### target / required mask

| 来源 | 规则 |
|---|---|
| 显式命令 | `target_mask` 必须来自 active `SlotClaimMap`；空 mask 拒绝 |
| EventMap `target_policy` | `EXPLICIT` / `ACTIVE_CLAIMED` / `ACTIVE_TRIGGER_TARGETS` / `SOURCE_ONLY` |
| `required_mask` | 缺省等于 `target_mask`；必须 `required ⊆ target`；required 目标必须 claimed 且 fresh |
| POSITION | 任何 target/code 指向 POSITION → `REJECTED(NOT_MY_DOMAIN)` |

解析结果随事件记录；发令后新 claim 的节点不追溯加入。

#### EventLog 容量与导出

- 容量符号 `EVENT_LOG_CAPACITY`；数值由 R2 的 RAM 预算决定，本文不写死。
- 溢出计 `log_overflow`，保留最近记录；required 事件溢出必须报 `REJECTED(LOG_FULL)`，不静默丢。
- 导出：STOP 后由 StorageAO 写 `/refmem/event/event_log.bin`，header（version/generation/record_count/crc32）+ 定长记录；Event Plane 不直接写 Flash。

#### 预算符号

- `EVENT_RUNTIME_WCET_CYCLES`、`EVENT_RUNTIME_STACK_BYTES`、`EVENT_LOG_RAM_BYTES` 是 R0 的预算符号；数值必须由 host 测量 + 板端 R5 证据给出，本文不猜测。
- 未测量前不得接固件；R0 只接受 host 上界和符号定义。

## 5. 静态模型规则

### 5.1 事件码命名空间

- 控制事件码由 `EVENT-CODE-01` 冻结；首版候选 8 bit，未定义 code 不得进入 catalog。
- `POSITION` 不映射进控制 code 空间；它是 TRIGGER 的独立实时事件。
- 别名只映射到 catalog 中存在的 code；不得引用未定义 code。

### 5.2 映射规则

- 每个 required `event_code` 必须有唯一 `action_id/owner`；重复映射拒绝。
- owner 必须存在于 active deployment；不存在则 pack 校验失败。
- 映射只描述“运行期应找谁”，不描述“怎么执行”。

### 5.3 EVG/source 静态席位

- `evg_slot` 由 deployment/claim 表达；Event Plane 只记录静态分配，不实现运行时 EVG。
- `target_policy` 只定义默认解析策略；具体 `target_mask` 由运行期从 `SlotClaimMap` 解析。

### 5.4 版本与 CRC

- EventPack、Catalog、Map、Alias 各自有 CRC；pack 汇总 CRC 进入 EventVector。
- generation 只在成功 activation 后递增；RUN 中不变。
- 旧 generation 的 pack 不得覆盖 active；rollback 只能回到 previous pack。

## 6. 接口面

Event Plane 只暴露静态配置/查询面：

| 命令 | 作用 |
|---|---|
| `SYSTem:EVENT:CONFigure` | 提交 EventPack staging（`STAGING_REF`），只 stage，不激活 |
| `SYSTem:EVENT:VALidate` | 校验 staged pack（schema/CRC/map/alias/target policy/EVG slot） |
| `SYSTem:EVENT:ACTivate` | 激活已通过 validate 的 staged pack |
| `SYSTem:EVENT:STATus?` | 读 staged/active pack version/CRC/generation、last validation |
| `SYSTem:EVENT:MAPPing?` | 读 `event_code → action_id/owner` |

不提供 `SYSTem:EVENT:INJect`/`ABORt`：运行时事件注入/中止归 RefMem/TRIGGER，Event Plane 不执行。

命令槽：`EVENT_CONFIGURE` / `EVENT_VALIDATE` / `EVENT_ACTIVATE` 通过同一 RefMem command slot 提交；`DistributedRefMemAO` 是唯一 take/ACK owner。完成点分别为：

- `EVENT_CONFIGURE` ACK = staged/accepted（不是 active）。
- `EVENT_VALIDATE` ACK = validated/failed（失败带 reason）。
- `EVENT_ACTIVATE` ACK = active/rejected（失败保持旧 active）。

Event Plane 只返回静态校验结果，不写 ACK。

别名：AliasRegistry 只提供静态映射；运行期别名命令仍由 RefMem/TRIGGER 承接，Event Plane 不执行别名动作。

## 7. 装载、激活与回滚

```text
EVENT_CONFIGURE (STOP only)
  -> stage EventPack
  -> staged

EVENT_VALIDATE (STOP only)
  -> validate schema/CRC/map/alias/target policy/EVG slot
  -> validated / failed (旧 active 不变)

EVENT_ACTIVATE (STOP only)
  -> previous = active
  -> active = staged
  -> EventVector update
  -> active
```

- 三步各自 ACK；`CONFIGURE` 的 ACK 只表示 staged，不代表 validated/active。
- 失败保持旧 active；不得半激活。
- RUN 中只读 active；不热加载。
- rollback 只切换 active/previous descriptor 与 bytes，不执行事件。

## 8. 写权限与一致性

- `EventPackService` 是 EventVector 唯一 writer；SCPI/别名只读 snapshot。
- EventPack/Catalog/Map/Alias 只在 STOP 期 stage/validate/activate；RUN 中只读。
- 静态表读取必须带 version/generation/CRC；不一致时拒绝使用。
- Event Plane 不写 RefMem command slot、不写 ACK、不写 TriggerRun。

## 9. 域关系

| 域 | 关系 |
|---|---|
| RefMem | 命令槽/ACK 唯一 owner；读取 EventPack 做运行期 intent 分发；Event Plane 不复制 command slot |
| TRIGGER | 实时域 owner：POSITION、T2 预测、`TriggerRun`、core1/PIO；读取 EventMap 找 action owner；Event Plane 不碰实时状态 |
| TDMA | 运输 owner；Event Plane 不定义 class/buffer/budget |
| VDC | 时间 owner；Event Plane 不做时间 |
| SCPI/Interface | 只提供 `SYSTem:EVENT:CONFigure/VALidate/ACTivate/STATus?/MAPPing?` 与 AliasRegistry 宏 |
| AO/FB | 运行期 action owner；Event Plane 只给静态 `action_id/owner_id` |

## 10. 当前实现状态

| 能力 | 状态 |
|---|---|
| T2 `POSITION` 实时事件 | 已实现（TRIGGER 内，不属本域） |
| EventPack/Catalog/Map/Alias 静态表 | 未实现 |
| EventVector 静态字段 | 未实现 |
| `SYSTem:EVENT:CONFigure/VALidate/ACTivate/STATus?/MAPPing?` | 未实现 |
| AliasRegistry 与 44 条别名的静态映射 | 未实现 |
| 运行时事件注入/分发/执行 | 不在本域；归 RefMem/TRIGGER |

## 11. 契约、代码形态与偏差

拟登记契约（E0 冻结后）：

- `EVENT-PACK-01`：EventPack header、存储、staging/activation/rollback。
- `EVENT-CODE-01`：控制事件 code 命名空间与保留段。
- `EVENT-MAPPING-01`：EventCatalog/EventMapTable/AliasRegistry schema、CRC、门禁。
- `EVENT-VECTOR-01`：EventVector 静态字段 writer/值域/lifecycle/snapshot。
- `EVENT-RUNTIME-01`：EventRuntimeService handler、EventStatus/EventLog、event_id/command_seq 映射（方案 B）。
- `EVENT-TRANSPORT-01`：CONTROL_EVENT 的 TDMA class/budget/seat（方案 B，与 TDMA 联审）。

拟建代码形态：

```text
components/event_plane/
  inc/event_plane.h          # 静态 API
  inc/event_vector.h         # 静态向量字段契约
  src/event_service.c        # EventPackService（静态 owner）
  src/event_pack.c           # EventPack 解析/校验/激活
  src/event_map.c            # Catalog/Map/Alias
  src/event_vector.c         # s_event_vector + seqlock/snapshot
  src/event_validate.c       # 静态校验
  src/event_runtime.c        # EventRuntimeService（方案 B）
  src/event_status.c         # EventStatus/EventLog（方案 B）
```

静态基线不建 EVG/EVR/dispatch/transport；方案 B 只建 CONTROL_EVENT runtime，不建 POSITION EVR。

HAOFV 偏差/待闭合：

1. **EventPack 实现未闭环**：路径/manifest/previous/rollback 规则已在 §4.1 给出，但 StorageAO 读写、原子替换、回滚尚未实现。
2. **静态表未落地**：Catalog/Map/Alias/EventVector 代码不存在。
3. **`SYSTem:EVENT:*` 未进 command table**：`CONFigure`/`VALidate`/`ACTivate` 只有概念，尚无 SCPI 入口。
4. **AliasRegistry 生成/校验**：44 条别名的静态映射、宏生成、门禁比对未实现。
5. **EVG 席位表达**：`evg_slot` 的 deployment/claim 字段未具体化。
6. **与 RefMem/TRIGGER 的读取契约**：运行期如何读 EventPack/Map（快照/指针/CRC）未定义。
7. **EventVector 实现未落地**：存放位置已定（`s_event_vector`），但代码/seqlock/读 API 不存在。
8. **契约 domain 口径**：`EVENT-*` 是子域前缀，登记时 `domain=refmem`；尚未登记。
9. **运行时未落地**：`EventRuntimeService`/EventStatus/EventLog、`EVENT_INJECT/ABORT`、CONTROL_EVENT transport 均为方案 B 设计；R0–R6 未开始。

## 12. 结论

- **Event Plane 只负责静态域**：EventPack/Catalog/Map/Alias/EventVector；不碰运输、接收、执行、deadline、TriggerRun、ACK。
- **静态域职责符合 HAOFV**：表/静态 Vector/部署门禁/写权限均有落点；没有实时路径。
- **运行时不归本域**：事件注入/分发/执行由 RefMem/TRIGGER 承接；本域文档不能宣称“事件已执行”。
- **运行时是后续拆分，不是本版基线**：方案 B 的控制事件运行时按 `REFMEM_EVENT_TODO.md` 的 R0–R6 渐进拆分；POSITION/T2 永远归 TRIGGER，命令槽/ACK/lease 永远归 RefMem。
- **E0 准入**：先冻结 `EVENT-PACK-01`/`EVENT-CODE-01`/`EVENT-MAPPING-01`/`EVENT-VECTOR-01`，完成 EventPack/Map/Alias 静态实现与 `SYSTem:EVENT:CONFigure/VALidate/ACTivate` 入口；运行时契约另立。
