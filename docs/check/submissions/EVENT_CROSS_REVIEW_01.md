# 核验提交单：RefMem Event Plane 子域 → RefMem/HAOFV

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/EVENT_CROSS_REVIEW_01.md`
Related: `docs/refmem/event/REFMEM_EVENT_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

## 1. 审查范围

对象：`REFMEM_EVENT_ARCHITECTURE.md` 第二版（审查修正后）与 `REFMEM_DOMAIN_ARCHITECTURE.md` 的“事件面挂接”微调。

目的：确认 B1（命令槽所有权）、B2（运输类）、B3（跨核 post）是否闭合，M1–M7 是否仍有缺口，以及修正是否引入新问题。

## 2. 结论

**`PASS_WITH_CHANGES`**：B1–B3 在架构层已闭合；E0 前仍有 5 项阻塞、7 项重大缺口。当前不能进入契约冻结或实现，不能判定“符合 HAOFV”。

## 3. 阻塞项（E0 前必须闭合）

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `R-B1` | Event Plane §4.1/§9；RefMem “事件面挂接” | `event_plane_intent_handler` 的调用上下文未定义：在 SCPI/command take 上下文执行，还是 RefMem service tick？若在 take 上下文，可能阻塞命令槽；ACK 写者仍含糊（EVR “回 ACK” vs “不写 ACK 位”） | 明确 handler 在 core0 service tick 执行、非阻塞；ACK 只由 `DistributedRefMemAO` 写；Event Plane 只返回 result/reason |
| `R-B2` | Event Plane §10；RefMem command 契约 | `event_id` 与 `command_seq` 的关系未定义；RefMem 单条在途命令下，多个别名/事件如何关联 | 定义 `event_id` 分配者（EVG/RefMem）、与 `command_seq` 的映射、单条在途时的排队/拒绝规则；alias legacy `1` 如何回查 |
| `R-B3` | Event Plane §4.1/§8 | 首版 `EventPlaneService` 挂到 `task_refmem_sync`，但没有 WCET/栈/RAM 影响分析；该任务已承载 RefMem/T2 工作 | 补预算：新增 service 的最坏耗时、栈增量、对 REFMEM/T2 相位的影响；超预算则拆任务并走 ResourceArbiter |
| `R-B4` | Event Plane §4.5/§14 | `EventPack` 的存储/装载格式未定义：static const、System Pack blob 还是 RMTP 扩展？`EVENT_CONFIGURE` 的 `payload_kind` 未定 | 冻结 pack 格式、version/CRC、staging/activation/rollback 路径；与 RefMem table registry 边界写清 |
| `R-B5` | Event Plane §5.4/§14 | `CONTROL_EVENT` 新 TDMA class 只有名字，没有 class ID、payload 上限、缓冲/buffer 归属、与 POSITION 的优先级 | 与 TDMA 联审并登记 `EVENT-TRANSPORT-01`；明确 class/buffer/预算；不得复用 POSITION slice |

## 4. 重大项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `R-M1` | Event Plane §4.3/§7 | `EventVector.state` 与 `TriggerSession` 状态机重复，可能出现两套 state | 明确二选一：EventVector 只存 session 投影，或 TriggerSession 作为唯一 state、Vector 只读镜像 |
| `R-M2` | Event Plane §5.1 | `event_owner` capability/role 没有具体表/位/字段；SlotClaim/DeploymentGate 如何判定 0/多 owner 未定义 | 在 `BoardCapabilityTable`/`SlotClaim` 中给出具体表达和 gate 条件 |
| `R-M3` | Event Plane §4.5/§10 | `AliasRegistry` owner 写成 “Interface + EventPlaneService” 双 owner；SCPI 宏生成/门禁比对未实现 | 单一 owner（建议 EventPlaneService），Interface 只消费；定义生成/校验命令 |
| `R-M4` | Event Plane §4.2/§6 | `EventDispatchFB` 改为 post 正确，但 post 队列/背压/失败语义/core1 mailbox 映射未定义 | 定义 post API、队列深度、满时拒绝、core1 mailbox 选择；不得静默丢 required |
| `R-M5` | Event Plane §5.1/§5.8 | EVG standby 的 `lease_epoch` 与 RefMem 的 coordinator lease 关系未定义，可能两套 epoch | 复用 RefMem `lease_epoch` 或明确独立域及换算；禁止双 epoch 无映射 |
| `R-M6` | Event Plane §4.3；HAOFV 数据模型 | `EventVector` 未进入顶层数据模型/索引；EventLog 的持久化边界只写“StorageAO 导出” | 在 Event Plane 域内登记 Vector 字段契约；明确日志持久化仅在 STOP/证据导出，不进入运行路径 |
| `R-M7` | Event Plane §5.4 | `EVENT_CLASS_PAYLOAD_MAX` 只有符号，没有值/来源；`IMMEDIATE`/`TIMESTAMPED` 在 TDMA 窗口的优先级未定义 | 由 `EVENT-TRANSPORT-01` 给出预算和优先级；与 TDMA 的 seat/窗口预算对齐 |

## 5. HAOFV 符合性复评

| 原则 | 复评 |
|---|---|
| 表负责规则 | 部分：EventPack 存储/激活未定（R-B4） |
| Vector 负责事实 | 部分：state 与 session 重复（R-M1）；未进顶层模型（R-M6） |
| 事件负责意图 | 符合（设计） |
| 事件载荷所有权 | 部分：wire budget 未定（R-B5/R-M7） |
| AO 负责流程 | 部分：service/AO 身份已澄清，预算未分析（R-B3） |
| FB 负责逻辑 | 部分：post 队列/背压未定（R-M4） |
| Resource Arbiter | 部分：EVG 席位未具体化（R-M2） |
| Service Layer | 符合（设计） |
| 写权限 | 部分：ACK 写者/state 需定（R-B1/R-M1） |
| 分布式共同事实 | 部分：B1 已修正，event_id/command_seq 未定（R-B2） |
| 分布式共同时间 | 符合（设计） |
| TDMA 通讯 | 部分：class/buffer 未定（R-B5） |
| 静态部署 | 部分：EventPack 未定（R-B4） |
| FB 非阻塞 | 部分：post 契约未定（R-M4） |
| 顶层安全 | 部分：日志/RAM 预算未定（R-B3/R-M6） |

结论：**方向符合，不能判定“符合 HAOFV”**；先闭合 R-B1–R-B5。

## 6. RefMem 边界复评

- `DistributedRefMemAO` 作为唯一 command slot owner 的修正是正确的；Event Plane 不再 take slot。
- 仍需补：`EVENT_*` 命令类型表的 owner 列应写 “DistributedRefMemAO → EventPlaneService”，避免误读为 EventPlane 拥有 slot。
- 仍需补：Event Plane 的 ACK/result 如何回写同一 `AckCommandRegion`；`event_id` 与 `command_seq` 的映射（R-B1/R-B2）。
- `EventVector`/`EventLog` 不进 64 KB 表的边界正确；但需在 RefMem 侧的“事件面挂接”明确“事件事实 vs 状态事实”的读取路径。

## 7. 建议修正顺序

1. `R-B1` handler 上下文 + ACK 写者。
2. `R-B2` `event_id`/`command_seq` 关系。
3. `R-B3` `task_refmem_sync` 预算。
4. `R-B4` EventPack 格式/激活。
5. `R-B5` CONTROL_EVENT TDMA class/buffer。
6. 再处理 R-M1–R-M7。

## 8. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复评 pass（同一 agent 的 fresh review，非第三方） |
| 审核方式 | 文档交叉 + 代码/契约对照 |
| 审核结论 | `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-21 |
| 备注 | 本记录不构成 C11 最终签署；E0 前需另一 agent 或人工复核并签署 |

## 9. 修正记录（2026-10-21，复审前）

- `R-M1` 已按“TRIGGER 实时域 / EventVector 静态域”分层修正：`EventVector` 只存静态 pack/catalog/map/alias/EVG 席位；`EventStatus` 存控制运行态；`TriggerRun` 归 TRIGGER；`EventSession` 与 `TriggerRun` 分离。
- `R-B3` 范围收窄：`EventPlaneService` 只做静态/控制工作；实时 deadline/执行归 TRIGGER；仍需 core0 service 的 WCET/栈/RAM 预算。
- `R-B1` 已补规则：intent handler 在 core0 service tick 执行，ACK 只由 `DistributedRefMemAO` 写（架构 §6.1）。
- `R-B2` 已补规则：`event_id` 归 EVG、`command_seq` 归 RefMem，1:1 映射与 legacy `1` 回查（架构 §6.2）。
- `R-B4` 已补规则：EventPack header/存储/staging/activation/rollback（架构 §6.3）。
- `R-B5` 已补规则：CONTROL_EVENT class/预算/优先级与 buffer 归属（架构 §5.4.1）；`EVENT-TRANSPORT-01` 仍待 TDMA 联审。
- `R-B1`/`R-B2`/`R-B4`/`R-B5` 仍为“规则已补、未实现/未独立复评”；复评结论保持 `PASS_WITH_CHANGES`。
