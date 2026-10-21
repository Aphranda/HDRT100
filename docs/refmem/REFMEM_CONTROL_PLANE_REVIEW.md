# RefMem 统一控制面与触发执行方案评审

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_CONTROL_PLANE_REVIEW.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

> 本文是评审稿（2026-10-21 评审通过），不是 canonical 契约。冻结条款以 `REFMEM_DOMAIN_ARCHITECTURE.md` 的 `REFMEM-CTRL-01` 为准；本文只汇总方案、现状证据、分阶段计划、审核结论和 MRF 子域升级评估。

## 1. 评审目的与结论摘要

目标：把“通过任意一个板卡接口操作 RefMem、控制所有板卡”收敛为可落地、可验证的方案，并回答同步之后“各板如何执行动作、如何触发执行”。

结论摘要：

- 分三层建设：**事实面**（同步已可用）、**控制面**（本地命令面已有，跨节点缺）、**执行面**（统一 dispatcher 基本没有）。
- 同步层保持简单；复杂度放在**动作契约、事件分发、跨节点完成证据**，不要反过来复杂化 delta/epoch。
- “任意板卡”应定义为**接入点/事件注入点任意**，不是“任意板都能当位置预测源”。位置源仍由唯一 `trigger_source` 承担。
- 触发执行以 **MRF Event System 为主参考**：EVG 广播“event code + timestamp”，每板 EVR 在本地时间到达时执行；T2 的 position 只是其中 class 0。
- 需要新增四个对象：`ControlCoordinator`（租约）、`EventGenerator`/`EventReceiver`（事件面）、`ActionDispatcher`/`ActionRegistry`（每板执行）、`TriggerSession`（会话生命周期）。
- 参考系分层：EtherCAT 只覆盖同步/运输；MRF 覆盖事件广播+定时执行；OpenSHMEM/lease/statechart 覆盖内存语义与协调。

## 2. 现状判断

| 层 | 现在有什么 | 缺什么 |
|---|---|---|
| 事实同步 | `REFMEM_DELTA`/`REFMEM_EPOCH`、TDMA 8×32 B mailbox、T2 `position_q` + FREEZE、`T2:ACTion?` 证据 | 无（保持现状） |
| 控制命令 | 本地 `s_refmem_command_slot`、`LOAD:*`/`ACTIVATE`、`CONFIG_ACTIVATE`、`SYSTem:COMMand:ACK?` | 跨节点路由、协调者租约、聚合视图、跨节点 FENCE |
| 动作执行 | 各域直接 SCPI/owner；`FbInstanceTable`/`EventLinkTable` 只做模型校验 | `ActionRegistry` + 每板 `ActionDispatcher` |
| 触发执行 | T2 已是 MRF 的 `POSITION` class 子集：origin 广播 position，target 本地解算 deadline，Core1 驱动 PIO；PREVIEW/FREEZE/回执齐全 | 通用 event code、EVG 通用化、任意板 software event 注入、event log、delay compensation |
| 命令槽物理位置 | live slot 是独立 static；`ack_command`（1024 B）只在 directory 注册 | 入区或明确 static 为规范位置（E0 决策） |
| 触发入口 | `T2:MOUNT/UNMOUNT` 为逐板本地命令；`TRIGger:STARt/STOP` 仍是占位返回 1 | `TriggerSession` 系统级命令 |

关键证据：

- 命令槽：`components/distributed_refmem/src/distributed_refmem.c` 的 `s_refmem_command_slot`，`LOAD:NODE` 路径 post 后立即本地 take。
- 动作模型：`refmem_application_model_get_event_link_table()` 无运行时调用者；`FbInstanceTable` 只用于 claim 推导、校验和查询。
- T2：`docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md` 的“T2 只负责分发时间片、动作侧自行展开”；`trigger_position_slice.h` 当前 16 B（含 next position cache）。
- 触发桩：`middleware/scpi_port/src/scpi_trigger_commands.c` 的 `TRIGger:STARt/STOP/PAUSe/CONTinue/ABORt` 返回 `1`，不执行动作。

## 3. 方案总览

```text
任意板 SCPI / UI / System Pack
  -> RefMem 控制面：command slot + TriggerSession + ControlCoordinator lease
  -> Event Plane（MRF 对齐）：EVG 广播 event code + timestamp
  -> 各板 EVR：校验 epoch/seq/CRC -> 到点 ActionDispatcher
       -> event_code 查 ActionRegistry
       -> AO/FB 或 core1 realtime owner 执行
  -> 结果/证据写回 RefMem + ACK/NACK + fence
  -> 聚合视图可见（本地快照，不做跨板阻塞读）
```

分层职责：

- **RefMem 控制面**：承载命令/事件事实、协调者租约、聚合和证据；不执行动作。
- **Event Plane**：EVG 发号/时间戳/广播，EVR 校验/排队/到点执行；不解释业务状态。
- **ActionDispatcher**：每板一个，决定“本地该执行什么、是否允许执行”，调用注册的 handler。
- **Owner**：AO/FB 或 realtime service，真正执行 PIO/GPIO/算法，并发布结果。

## 4. 核心设计

### 4.1 控制面挂接

- 命令面、观测面、目录面三个挂接面；不新增第二套控制总线、命令槽或 ACK 事实。
- `ControlCoordinator` 由 `control_owner_slot` 唯一确定；任一入口只做转发，只有协调者发号、判超时、做聚合。
- 协调者迁移只在 STOP/维护态；RUN 中丢失则 fail-closed 进入 `CONTROL_LOST`，本板 STOP/安全动作仍可用。
- 读只读本地快照；聚合小帧广播，逐目标明细留在协调者与目标本地。

### 4.2 动作执行

`ActionContract`（每类动作一行，静态装载）：

| 字段 | 含义 |
|---|---|
| `action_id` | 动作目录项，版本化 |
| `instance_id` | 绑定 `FbInstanceTable` |
| `trigger` | command / event / state / periodic / safety |
| `precondition` | run state、permission、deployment、输入新鲜度 |
| `context` | core0 service / core1 realtime |
| `handler` | `ActionRegistry` 注册的 owner 回调 |
| `output` | result slot / `DataLinkTable` 字段 |
| `completion` | ACK + `VISIBLE` + fence |
| `timeout` | 截止期与重试上限 |

`ActionDispatcher` 每板一个，在 service tick 中按 ECA（Event-Condition-Action）执行：读本地快照 → 求值条件 → 调 handler。缺 handler 的 required instance 必须让 `DeploymentGate` 拒绝，不能静默跳过。

### 4.3 触发执行：MRF 对齐的 Event Plane

触发执行以 MRF Event System 为主参考，而不是把 T2 的 position 当唯一事件。MRF 的模型是：**EVG 广播“event code + timestamp”，各 EVR 在本地时间到达时执行**。T2 的 position 只是其中一类事件（class 0），不是全部。

| MRF 概念 | RefMem 落点 | 说明 |
|---|---|---|
| Event Generator（EVG） | `event_generator_slot`（唯一，可 standby） | 盖时间戳、分配 seq、排序、广播；不执行动作 |
| Event Receiver（EVR） | 每板 `EventReceiver` | 校验 epoch/seq/CRC；按 code 查表；到点执行 |
| Event code | `event_code`（8 bit 命名空间） | POSITION / START / STOP / ABORT / CAL / INJECT |
| Timestamp | `execute_at_vdc_ns` | 绝对时间；position 用 `target_ms_index + fine` |
| Event frame | `{code, flags, timestamp, seq, target_mask, payload}` | 一帧一个事件；CRC 由 TDMA 承担 |
| Event mapping | `event_code → action handler` 表 | STOP 期装载；缺映射拒绝 |
| Delay compensation | per-EVR `fine_delay_ns` | 输出级补偿，不在 EVG 端逐板修正 |
| Event log | EVR 环形日志 + 聚合计数 | 逐事件证据，不只聚合 |
| Redundant EVG | standby + lease epoch | 只在 STOP/维护态切换 |

事件类型：

- `TIMESTAMPED`：到 `execute_at_vdc_ns` 执行，MRF 的核心语义。
- `IMMEDIATE`：收到即执行，仅安全/维护类。
- `SOFTWARE`：任意板注入，EVG 统一排序后广播。
- `POSITION`：T2 现有 16 B slice，作为 class 0 原样保留。

`TriggerSession` 用 goal/feedback/result/cancel 描述一次触发的生命周期（configure → arm → running → done/abort），但真正的执行语义由 Event Plane 承担；session 只定义范围、目标和证据，不替 EVG 发号，也不替 EVR 执行。

任意板语义：

- 任意板都可以是 EVR，也可以提交 `SOFTWARE` 事件；
- 只有 EVG 盖时间戳、发号、广播；EVG 是角色，不是固定物理板；
- `POSITION` 事件的生产者仍是能看到 COUNTER 的 `trigger_source`，不能任意；
- 因此“任意板触发”= **任意板提交 software event**，不是任意板当 position 预测器。

触发路径：

```text
任意板 TRIGGER_INJECT(code, target_mask, execute_at_vdc_ns)
  -> ControlCoordinator 校验/排序
  -> EVG 盖时间戳、分配 seq、广播 EventFrame
  -> 各板 EVR 校验 epoch/seq/CRC
  -> 到点：ActionDispatcher 查 event_code -> handler
  -> 逐目标 ACK/结果；EVG 聚合 fence
```

T2 映射：

- T2 `PREVIEW` = `POSITION` event；`FREEZE` = position event 的提交控制。
- `T2:MOUNT/UNMOUNT` = `TriggerSession` 的 start/stop，不是 event。
- `T2:ACTion?` = EVR 执行日志/聚合视图。
- 16 B position slice 不改；`POSITION` 作为 class 0 复用现有编码。

边界：MRF 的 WKC/AL 状态机只作运输/通信证据；执行完成仍看 EVR 的 `executed` + ACK + fence。

### 4.4 调度与优先级

- 环上：TDMA 静态窗口保底，命令/事件使用保留窗口；`STOP`/`FAULT_CLEAR` 可抢占，其余单条在途。
- 节点内：ARINC 653 式时间分区 + 固定优先级（RM/DM）+ sporadic server 预算；**安全命令不用 EDF**。
- 状态与命令分离：状态 level-triggered（读快照收敛），命令 edge-triggered（幂等键 + at-most-once）。
- 不使用周期整表广播；全量 resync 只在入网、epoch/layout 不一致、stale 恢复时触发。

### 4.5 一致性与完成

- 单 writer + slot owner + epoch/generation + seqlock/双缓冲。
- 运输完成（WKC/frame consumed）与执行完成（owner ACK）分开记；两者不一致时归因到“未收到/未执行/未 ACK”。
- fence：`PENDING → ACKED → VISIBLE → PASSED`；RUN gate 只消费 `PASSED`。
- 不做跨板阻塞读；系统视图由本地 mirror 拼装，每行带 age/stale。

## 5. 参考模型

| 问题 | 借鉴 | 采用/不采用 |
|---|---|---|
| **事件广播/定时执行（主参考）** | **MRF Event System** | 借 EVG/EVR、event code、timestamp、event mapping、event log、delay compensation；WKC 只作运输证据 |
| 周期同步/过程数据 | EtherCAT PDO + DC | 只借同步与过程映像；**不借**控制面语义 |
| 反射内存语义 | OpenSHMEM/MPI RMA | 借 put/get/atomic/fence 子集；不做任意地址裸写 |
| 协调者唯一性 | Chubby/etcd lease | 借 lease/epoch；Raft 不进实时路径 |
| 触发会话 | ROS 2 Actions | 借 goal/feedback/result/cancel 作为 session 外壳 |
| 动作状态约束 | PLCopen / PackML | 借状态机；不引入完整 PLC 运行时 |
| 命令账本 | Redis Streams PEL | 借 outstanding/ACK/claim/timeout |
| 数据保护 | AUTOSAR E2E | 借 CRC/seq/timeout |
| 节点调度 | ARINC 653 + RM/DM | 借时间分区和固定优先级 |

## 6. 分阶段计划

| 阶段 | 交付 | 退出判据 |
|---|---|---|
| E0 事件契约 | `EventFrame` schema（code/flags/timestamp/seq/target_mask/payload）、event code 命名空间、16 B position slice 登记、命令槽物理放置 | 文档门禁 + 独立复核 |
| E1 EVR + 动作面 | 每板 `EventReceiver` + `event_code → handler` 表 + `ActionRegistry`/`ActionDispatcher`；本地 START/STOP | host 用例 + 单板 HIL |
| E2 EVG + 注入 | EVG 盖时间戳/发号/广播；任意板 `TRIGGER_INJECT`；两板 START/STOP/ABORT | 两板：B 执行 + A 读到 ACK/fence |
| E3 POSITION class | T2 position 作为 class 0 接入 Event Plane；`T2:MOUNT/UNMOUNT` 收敛为 session；短扫描 | executed = 位置数 |
| E4 全目标 | 四板 all-target；事件日志、per-EVR delay compensation、权限/运行态矩阵 | 四板逐目标一致、无隐式跳过 |
| E5 冗余/迁移 | standby EVG + STOP 态 `trigger_source` 迁移 | 双主拒绝 + 迁移/回退负测 |

## 7. 不做什么

- 不做远程裸内存写、跨板同步读、运行时动态 FB 部署。
- 不用 Raft/Paxos 给每条命令做共识；不把 Kafka/Redis 当实时总线。
- 不把 WKC/AL 状态机当业务执行证据；不把“帧到了”写成“动作完成”。
- 不为控制面新增第二条总线；不复制出第二套命令槽/ACK 事实。
- 不在 RUN 中热切 trigger source 或 coordinator。

## 8. 待审核决策点

| ID | 决策 | 推荐 | 影响 |
|---|---|---|---|
| Q1 | “任意板卡”的语义 | 接入点/事件注入点任意；position 源唯一 | 决定是否允许任意板当 trigger source |
| Q2 | 命令槽物理放置 | 迁入 `ack_command` 区并升布局版本 | 影响 ABI、`REFMEM-LAYOUT-01`、跨核访问 |
| Q3 | ActionRegistry 载体 | 先静态注册表，稳定后再进 9 表 | 先避免 wire 变更，后续再统一 |
| Q4 | `EventFrame` 格式 | 新增事件 class；`POSITION` 作为 class 0 原样保留 | 影响 TDMA message class/带宽预算 |
| Q5 | EVG 角色 | 唯一 EVG + 可选 standby；position source 独立 | 决定“任意板触发”的范围 |
| Q6 | Coordinator / EVG / Source | 三者逻辑解耦，可同板 | 避免把控制面绑死在源板 |
| Q7 | 16 B position slice 登记 | 本周期补登记（当前登记仍写 10 B） | 影响契约一致性和后续 event 扩展 |
| Q8 | 首个里程碑 | E0/E1/E2：两板 software event（START/STOP）+ EVR 映射，position 后接 | 控制风险，先闭环事件面再扩 T2 |

## 9. 审核记录

| 项 | 内容 |
|---|---|
| 审核人 | 用户 |
| 审核日期 | 2026-10-21 |
| 结论 | ACCEPTED |
| 修改要求 | 以 MRF Event System 为主参考；MRF 子域升级另行评估（见 §10） |

## 10. MRF 子域升级评估

### 10.1 评估口径

“子域升级”指：新建 `docs/<domain>/` 目录 + 架构/TODO/进度三件套 + 独立 `contract_id` + 独立 owner + 索引/轮转/交叉审核。仅新增一个章节或一份评审稿不算子域。

### 10.2 现状

- MRF 目前是**参考模型**，不是产品域；实际实现分布在三处：RefMem（命令/事件承载）、Trigger/T2（POSITION 事件）、TDMA（一等席运输）。
- 只有 `POSITION` class 有实现；EVG/EVR 通用化、software event 注入、event log、delay compensation 尚未实现。
- 文档只有本评审稿、`REFMEM-CTRL-01` 的事件章节和 T2 文档中的映射，尚未形成三件套。

### 10.3 选项对比

| 选项 | 形态 | 优点 | 代价/风险 | 结论 |
|---|---|---|---|---|
| A 不升级 | 参考模型 + 跨域契约 | 不新增第四份事实源；与实现分布一致；治理成本最低 | 发现性稍差，需要索引指路 | **推荐（当前）** |
| B RefMem 子域 `event/` | `docs/refmem/event/` 三件套 | 与 bearer/控制面一致；事件面集中 | 与 T2 文档交叉；需要独立 owner；当前内容不足 | 观察，成熟后再评估 |
| C Trigger 子域 `mrf/` | `docs/trigger/mrf/` 三件套 | 触发语义集中 | 与 RefMem/TDMA 交叉；MRF 是 vendor 名 | 不推荐 |
| D 顶级 `mrf/` 域 | `docs/mrf/` 三件套 | 发现性最强 | 过度治理；无单一 owner；命名误导 | 不推荐 |

### 10.4 结论

**当前不升级为子域。** 理由：

1. **没有单一 owner**：事件语义跨 RefMem、Trigger、TDMA，任一新域都会与另外两个域争夺事实源。
2. **实现只覆盖 `POSITION`**：通用事件面尚未落地，现在建域会把“参考模型”固化成“产品域”。
3. **MRF 是参考名，不是产品名**：产品能力应叫 Event Plane；用 vendor 名做域会误导后续维护。
4. **治理成本高**：三件套、独立 contract、索引、轮转、交叉审核，当前内容量不足以支撑。

当前承载方式：**参考模型 + 跨域契约 + 现有域章节**。

- 事件面语义：`REFMEM-CTRL-01` 的 Event Plane 章节（扩展 `REFMEM-EVENT-01` 契约）。
- POSITION 映射：`TRIGGER_T2_ARCHITECTURE.md`。
- 运输：TDMA event class / 一等席。
- 设计依据：本评审稿，不登记为契约。

### 10.5 升级触发条件

满足任一条再重新评估：

- 通用 EVG/EVR/software event 已实现，并有明确 owner；
- `POSITION` 之外出现至少两个事件消费者（例如 CAL、SYNC、维护）；
- 事件面文档达到三件套规模（架构 + TODO + 进度/工具）；
- 需要独立 `contract_id` 管理 event frame/version/delay compensation；
- 需要独立轮转或交叉审核。

### 10.6 如果升级，建议形态

- 名称用 **Event Plane**，不用 MRF；MRF 只作为参考模型引用。
- 放在 `docs/refmem/event/` 或 `docs/trigger/event/`，取决于 owner；不新增顶级域。
- 三件套 + `EVENT-*` 契约；在 `docs/README.md` 和域 README 加索引；按 C14/C15 轮转。
