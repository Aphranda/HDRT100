# RefMem Event Plane 子域待办（静态域）

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/event/REFMEM_EVENT_TODO.md`
Related: `docs/refmem/event/REFMEM_EVENT_ARCHITECTURE.md`, `docs/refmem/event/REFMEM_EVENT_TASK_PROGRESS.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

本文件维护 Event Plane 静态域的可执行待办。Event Plane 只做静态事件模型；运行时分发/执行不在本域。

## 执行原则

- **只做静态域**：EventPack/Catalog/Map/Alias/EventVector；不碰运输、接收、执行、deadline、TriggerRun、ACK。
- 运行时归 RefMem（命令/ACK）和 TRIGGER（实时执行）；本域只提供静态输入。
- 每一步先在 host 闭环，再上板。
- 每条任务有落点、判据、依赖；没有判据不算完成。

## 当前基线

- 架构已按“静态域 only”重写：`REFMEM_EVENT_ARCHITECTURE.md`。
- 现有实现只有 T2 `POSITION` 实时事件，归 TRIGGER，不属本域。
- 静态表/EventPack/EventVector 尚未实现；`SYSTem:EVENT:*` 静态命令尚未进 command table。

## E0 - 静态契约

- [ ] 冻结 `EVENT-PACK-01`：header（magic/version/generation/catalog_count/map_count/alias_count/crc32）、存储路径、manifest 集成、previous pack/rollback。
- [ ] 冻结 `EVENT-CODE-01`：控制事件 code 命名空间/保留段；POSITION 不进入该空间。
- [ ] 冻结 `EVENT-MAPPING-01`：EventCatalog/EventMapTable/AliasRegistry schema、CRC、activation 门禁。
- [ ] 冻结 `EVENT-VECTOR-01`：EventVector 静态字段 writer/值域/lifecycle/snapshot；确认不进 RefMem 64 KB 表。
- [ ] 登记四项契约（pending），`clause_loc` 指向本域架构，`code_anchor` 用真实文件。
- [ ] 明确 `SYSTem:EVENT:CONFigure/VALidate/ACTivate` 与 `EVENT_CONFIGURE/EVENT_VALIDATE/EVENT_ACTIVATE` 的 1:1 关系；三步各自 ACK，ACK 由 `DistributedRefMemAO` 写。

## E1 - 静态实现

- [ ] `EventPack` parser/validator：magic/version/generation/CRC、catalog/map/alias 计数一致性。
- [ ] `EventPackService` 静态 owner：`task_refmem_sync` STOP 上下文，负责 stage/validate/activate/rollback。
- [ ] `EventVector` 静态字段落地：EventPackService 唯一 writer、CRC/generation、`s_event_vector`/seqlock/snapshot。
- [ ] `EventCatalog`/`EventMapTable`/`AliasRegistry` 静态表落地；重复 code/映射/alias 拒绝。
- [ ] `SYSTem:EVENT:CONFigure`：`STAGING_REF` 只 stage；`VALidate` 只校验；`ACTivate` 只激活；失败保持旧 active。
- [ ] `SYSTem:EVENT:STATus?`/`MAPPing?`：只读静态快照；`VALidate` 是动作命令，不是查询。
- [ ] host 用例：坏 CRC、重复 code、缺 owner、alias 指向未定义 code、rollback。
- [ ] 单板 HIL：`CONFigure`/`VALidate`/`ACTivate` 逐步 ACK、`STATus?`/`MAPPing?` 读回。

## E2 - 44 条别名的静态映射

- [ ] 盘点 44 条“接受但未执行”的命令（39 条 `scpi_port_result_accepted` + 5 条 trigger；快照）。
- [ ] 为每条分类：static-alias / runtime-owner / keep-accepted / delete-merge。
- [ ] 建立 AliasRegistry 静态映射：alias → canonical code、default target policy、response policy、runtime owner。
- [ ] SCPI 别名宏由 registry 生成或门禁逐条比对；禁止两处手写漂移。
- [ ] 首批 5 条 trigger 只登记静态映射；运行期执行仍由 RefMem/TRIGGER 承接，不在本域实现。
- [ ] host 用例：alias 查找、未定义 code 拒绝、response policy 一致。

## E3 - 运行时读取契约（不实现运行时）

- [ ] 定义 RefMem/TRIGGER 如何读取 EventPack/EventMap 的稳定快照（version/generation/CRC、指针/拷贝、生命周期）。
- [ ] 定义 `EVENT_CONFIGURE`/`EVENT_VALIDATE`/`EVENT_ACTIVATE` 经 RefMem command slot 的 intent/ACK 边界：Event Plane 只返回静态结果，ACK 由 `DistributedRefMemAO` 写。
- [ ] 定义缺 mapping/旧 generation 时运行期 owner 的拒绝责任；Event Plane 不兜底执行。
- [ ] 文档化“本域不提供 INJect/ABORt/transport/EVR/TriggerRun”；这些归 RefMem/TRIGGER。

## Runtime 拆分计划（方案 B，渐进）

目标：在 RefMem 命令面下增加 Event Plane 的**控制事件运行时**；POSITION/T2 仍归 TRIGGER。当前版本仍是静态基线，下表只是拆分顺序；未实施前不改变 Out of Scope。

原则：一次只拆一个冲突；每步先在 host/两板闭环；不建第二 command slot/ACK/lease；不接 POSITION。

| 阶段 | 冲突 | 拆分动作 | 退出判据 |
|---|---|---|---|
| `R0` | owner/状态/预算/命令契约 | 命名 `EventPackService`（静态）与 `EventRuntimeService`（运行时）；定 EventStatus/EventLog 唯一 writer；core0 task/预算估算；POSITION 硬隔离；`EVENT_INJECT/ABORT` command type、权限矩阵、reason（先 host 契约） | owner/字段/权限/预算/host 模型齐 |
| `R1` | handler 契约 | `event_runtime_handler` 非阻塞：`ACCEPTED/BUSY/REJECTED`、重复 `command_seq` 返既有结果、结果回 RefMem；host 模拟 command→handler→result→ACK | host 闭环，不接固件/POSITION/transport |
| `R2` | 静态 vs 运行时状态 | EventVector 仍静态；EventStatus/EventLog 由 `EventRuntimeService` 写；TriggerRun 仍 TRIGGER | 状态/日志 snapshot 可读 |
| `R3` | 运输 | `CONTROL_EVENT` 用 TDMA class；POSITION 不变；不新增总线 | class/budget/seat 联审通过 |
| `R4` | 执行 | EventDispatch 只 post 给 owner；ActionDispatcher | owner result + ACK |
| `R5` | 板端任务/预算 | task/stack/WCET/两板 HIL；独立 AO 才补新任务 | WCET/栈/RAM/两板证据 |
| `R6` | 契约/评审 | `EVENT-RUNTIME-01`/`EVENT-TRANSPORT-01` 登记；R5 review 复审 | 契约登记 + C11 |

门禁：

- `R0` 未通过前，不写 handler 代码；R0 必须先定 owner/状态/预算/POSITION 门禁。
- `R0` 必须冻结（见架构 §4.8）：`event_id` 回绕/唯一、`EVENT_ABORT` 状态机、target/required 解析、`EVENT_LOG_CAPACITY`/导出格式、`EVENT_RUNTIME_WCET_CYCLES`/`EVENT_RUNTIME_STACK_BYTES` 预算符号。
- `R1` 只做 host 模型，不接固件、不接 POSITION、不接 transport。
- `R2` 未通过前，不接 POSITION/transport；POSITION 永远不做 Event Plane 第二接收路径。
- 任何阶段不得把 Event Plane 变成第二命令面；RefMem 的 command slot/ACK/lease 不变。

## 不在本域（当前静态版本）

以下为当前静态版本的排除项；方案 B 按上表逐步拆分，拆分前仍不在本域。

- POSITION/T2 预测、deadline、`TriggerRun`、core1/PIO 边沿：归 TRIGGER。
- 事件运输（TDMA class/buffer/seat/优先级）：归 TDMA/TRIGGER。
- 运行时 `EVENT_INJECT`/`EVENT_ABORT`、event_id/command_seq 分配、ACK：当前归 RefMem；方案 B 只把事件运行时 handler 放回 Event Plane，command slot/ACK 仍归 RefMem。
- EventStatus/EventLog 运行时写者：当前由 RefMem/TRIGGER 提供；方案 B 的 R1 再评估归属。
- EVG/EVR/dispatch：当前不建代码；方案 B 的 R2–R4 逐步评估，POSITION 不进入。

## 验证矩阵

| 阶段 | host | 单板 HIL | 两板/四板 | 必需证据 |
|---|---|---|---|---|
| E0 | 契约文本审查 | 不需要 | 不需要 | 契约登记/独立复核 |
| E1 | 必须 | 必须 | 不需要 | pack CRC、activate、status/mapping 读回 |
| E2 | 必须 | 视需要 | 不需要 | alias registry 与 SCPI 宏一致 |
| E3 | 接口契约 | 不需要 | 不需要 | RefMem/TRIGGER 读取契约 |

## 依赖与边界

- 依赖 RefMem 的 command slot/ACK（`DistributedRefMemAO` 唯一 owner）。
- 依赖 RefMem 的 staging/activation 模式；EventPack 不进 64 KB 表。
- 依赖 Interface 的 SCPI 命令表；Event Plane 只提供静态 registry。
- 不依赖 TRIGGER 实时实现；只定义它读取 EventMap 的静态契约。
- 不在本域写 ACK、TriggerRun、TDMA class、POSITION、deadline。
