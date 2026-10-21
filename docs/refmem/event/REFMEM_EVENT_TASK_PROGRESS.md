# RefMem Event Plane 子域任务进度

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/event/REFMEM_EVENT_TASK_PROGRESS.md`
Related: `docs/refmem/event/REFMEM_EVENT_ARCHITECTURE.md`, `docs/refmem/event/REFMEM_EVENT_TODO.md`, `docs/refmem/REFMEM_CONTROL_PLANE_REVIEW.md`
Last updated: 2026-10-21

> 2026-10-21 迁移：本子域从 `docs/event/` 迁入 `docs/refmem/event/`，文件更名为 `REFMEM_EVENT_*`；历史条目按新路径阅读。进度 ID 保留 `EVENT-TASK-*`（稳定子域 ID），文件前缀 `REFMEM_EVENT_` 表示父域归属，不要求两者一致。

本文档记录 RefMem Event Plane 子域的阶段性任务进度、验证结果和后续动作。待办放在 `REFMEM_EVENT_TODO.md`，本文只记录已经发生的工作和可回溯结果。

### EVENT-TASK-20261021-019 — R0 运行语义与 HAOFV 设计终审 R7

- 状态：完成 R0 运行语义补充与 R7 设计终审；未修改固件/SCPI。
- 完成内容：
  - 架构 §4.8：`event_id` 回绕/唯一、`EVENT_ABORT` 状态机、target/required 解析、EventLog 容量/导出、预算符号。
  - 架构 §2 运行时四行改为“设计符合（待实现）”，transport 仍“部分（待联审）”。
  - TODO R0 增加必须冻结项（event_id/ABORT/target/log/budget）。
  - Review 06 §8 记录 U-M1–U-M8 修正；新增 Review 07 设计终审。
- 结论：**静态基线 + 方案 B 运行时设计均符合 HAOFV**（设计层）；实现、契约登记、TDMA 联审、C11 未做。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（226 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：R0 host 契约实现；不在本目标内。

### EVENT-TASK-20261021-018 — HAOFV 符合性复审 R6

- 状态：完成 R6 审查与文档修正；未修改固件/SCPI。
- 结论：静态基线 `PASS`；运行时 `PASS_WITH_CHANGES`。
- 已完成修正：
  - `EventRuntimeService` 明确为 `DistributedRefMemAO` sub-service，不是独立 AO。
  - 增加运行态/权限矩阵（INJECT/ABORT/CONFIGURE/VALIDATE/ACTIVATE）。
  - `EventStatus.session_state` 明确为控制会话，不等于 TriggerRun。
  - EventStatus/EventLog 增加 seqlock/读 API。
  - RefMem 父域事件面增加方案 B 运行时 flow（INJECT/ABORT → handler → result）。
  - 子域符合性表增加方案 B 四行“部分（设计）”。
- 仍开放：U-M1–U-M8（event_id 回绕、ABORT 状态机、target/required 解析、契约登记、预算、日志导出）。
- 文档：`docs/check/submissions/EVENT_CROSS_REVIEW_06.md`；索引已更新。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（225 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：R0 先定 event_id/target/ABORT/预算，再做 R1 host handler。

### EVENT-TASK-20261021-017 — 闭合 R5：运行时 owner/状态/预算/契约

- 状态：完成架构与 TODO 修正；未修改固件/SCPI。
- 闭合项：
  - `T-B1`：新增 `EventRuntimeService`（方案 B），与静态 `EventPackService` 分离。
  - `T-B2`：EventStatus/EventLog 由 `EventRuntimeService` 写；EventVector 仍静态；TriggerRun 仍 TRIGGER。
  - `T-B3`：R0 重排为 owner/状态/预算/POSITION 门禁/命令契约；R1 只做 host handler。
  - `T-B4`：handler 契约（ACCEPTED/BUSY/REJECTED、重复 command_seq、ACK 归 RefMem、POSITION `NOT_MY_DOMAIN`）。
  - `T-B5`：RefMem 命令类型加入 `EVENT_INJECT/ABORT`（方案 B）；新增 `EVENT-RUNTIME-01`/`EVENT-TRANSPORT-01`。
  - `T-M1`–`T-M8`：POSITION 硬门禁、transport 后续、EventStatus/Log 字段、预算前移、符合性表条件化、命名分离、契约矩阵。
- 文档：子域架构 §4.6/§4.7/§11、TODO R0–R6、RefMem 命令类型/TODO、Review 05 §9。
- 实现边界：EventRuntimeService/EventStatus/EventLog/运行时命令均未实现；静态基线不变。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（224 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：R0 host 契约（owner/状态/预算/权限/POSITION 门禁），不写 handler 固件。

### EVENT-TASK-20261021-016 — 运行时方向复审 R5

- 状态：完成 R5 审查；未修改固件/SCPI。
- 结论：静态基线 `PASS`（设计层）；运行时计划 `PASS_WITH_CHANGES`，不能直接按 R0–R6 实施。
- 主要发现：
  - `T-B1`：运行时 owner 未命名（`EventRuntimeService` 与 `EventPackService` 未分离）。
  - `T-B2`：EventStatus/EventLog 归属与静态文档冲突。
  - `T-B3`：R0 handler 排在状态/预算之前，顺序不对。
  - `T-B4`：handler 同步/BUSY/重复 command_seq/ACK 契约未定义。
  - `T-B5`：`EVENT_INJECT/ABORT` 未进 RefMem command type/权限/contract。
  - `T-M1`–`T-M8`：POSITION 硬隔离、CONTROL_EVENT 运输、EventStatus/EventLog 字段、预算前移、符合性表条件化、命名分离、契约矩阵。
- 建议重排：R0 owner/状态/预算/权限；R1 host handler；R2 状态/日志；R3 运输；R4 执行；R5 板端预算；R6 契约/C11。
- 文档：`docs/check/submissions/EVENT_CROSS_REVIEW_05.md`；索引已更新。
- 下一步：先按 T-B1/B2/B3 重排 TODO，再开 R0 host 契约。

### EVENT-TASK-20261021-015 — 运行时拆分计划（方案 B）

- 状态：完成拆分计划文档；未修改固件/SCPI，当前架构仍是静态基线。
- 目标：在 RefMem 命令面下逐步加入 Event Plane 的控制事件运行时，避免一次性重写。
- 完成内容：
  - `REFMEM_EVENT_TODO.md` 新增 R0–R6 拆分表：命令槽/ACK 所有权、静态 vs 运行时状态、POSITION 接收、CONTROL_EVENT 运输、执行、任务/预算、契约/评审。
  - 门禁：R0/R1 未通过不进入 POSITION/transport；R2 前不接 POSITION；任何阶段不建第二命令面。
  - `REFMEM_EVENT_ARCHITECTURE.md` 结论补“运行时是后续拆分，不是本版基线”。
- 边界：POSITION/T2 永远归 TRIGGER；command slot/ACK/lease 永远归 RefMem；Event Plane 只增加 CONTROL_EVENT 运行时 handler（方案 B）。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（223 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：先做 R0（RefMem `EVENT_INJECT/ABORT` handler 注册、Event Plane 不 take/不写 ACK），再做 R1。

### EVENT-TASK-20261021-014 — 闭合 R4：静态 owner、命令拆分、EventPack/EventVector

- 状态：完成架构修正；未修改固件/SCPI。
- 闭合项：
  - `S-B2`：新增 `EventPackService` 静态 owner（`task_refmem_sync` STOP 上下文），EventVector 唯一 writer；RefMem 侧同名。
  - `S-B3`：`SYSTem:EVENT:CONFigure`=stage、`VALidate`=校验、`ACTivate`=激活；三步各自 ACK；`STATus?`/`MAPPing?` 只读。
  - `S-M1`：EventPack 路径 `/refmem/event/event_pack.bin`、previous `/refmem/event/event_pack.prev.bin`、manifest 可选行、rollback 切换规则。
  - `S-M2`：EventVector 定为 `s_event_vector`（`event_vector.c`），seqlock + `event_plane_get_static_snapshot()`。
  - `S-M5`：静态部署判定改为“设计符合，路径/rollback 见 §4.1”。
  - `S-M8`：契约 `domain=refmem`，contract_id 保留 `EVENT-*` 子域前缀。
  - `S-M9`：进度 ID 保留 `EVENT-TASK-*`，文件前缀 `REFMEM_EVENT_` 表示父域归属。
- 文档：子域架构 §2/§4.0/§4.1/§4.5/§6/§7/§8/§11/§12；RefMem 架构命令类型/事件面；RefMem TODO P8；Review 04 §9。
- 实现边界：EventPackService/EventVector/EventPack/Map/Alias 代码不存在；契约未登记。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（223 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：按 E0 冻结四项契约，先做 EventPack parser/validator + `CONFigure/VALidate/ACTivate` 入口。

### EVENT-TASK-20261021-013 — 子域迁移复审 R4

- 状态：完成迁移与静态域复审 R4；未修改固件/SCPI。
- 结论：`PASS_WITH_CHANGES`。迁移/索引通过；`S-B1` 已闭合；`S-B2`/`S-B3` 仍未闭合。
- 主要发现：
  - `S-B2`：子域文档未命名静态 owner；`EventVector` writer 写成 “EventPack activation”，与 RefMem 侧 `EventPackService` 不一致。
  - `S-B3`：`SYSTem:EVENT:VALidate?` 查询名 vs 校验动作；`CONFigure` 仍同时表示 staging/activation。
  - `S-M1`/`S-M2`：EventPack 路径/manifest/rollback、EventVector 存放仍未定。
  - `S-M5`：静态部署仍标“符合（设计）”，与 S-M1 矛盾。
  - `S-M8`/`S-M9`：契约 domain 与进度 ID 前缀口径待统一。
- 文档：`docs/check/submissions/EVENT_CROSS_REVIEW_04.md`；索引已更新。
- 下一步：先修 `S-B2`（EventPackService/EventVector writer）与 `S-B3`（命令语义），再补 EventPack/EventVector 细节。

### EVENT-TASK-20261021-012 — 迁入 refmem/event 子域

- 状态：完成目录迁移与索引更新；未修改固件/SCPI。
- 变更：
  - 三件套迁入 `docs/refmem/event/`，并更名为 `REFMEM_EVENT_ARCHITECTURE.md`、`REFMEM_EVENT_TODO.md`、`REFMEM_EVENT_TASK_PROGRESS.md`；旧目录已删除。
  - 三件套 Domain 改为 REFMEM，标题改为 RefMem Event Plane 子域；父域 `REFMEM_DOMAIN_ARCHITECTURE.md` 的事件面挂接同步收敛为静态子域（只保留 `EVENT_CONFIGURE`）。
  - `docs/README.md`、`docs/refmem/README.md`、`docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`、三份 `EVENT_CROSS_REVIEW_*` 的路径/Related 同步更新。
- 边界：Event Plane 仍是静态域；运行期事件归 RefMem/TRIGGER。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（222 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：按 `REFMEM_EVENT_TODO.md` 冻结四项静态契约。

### EVENT-TASK-20261021-011 — 静态域架构复审 R3

- 状态：完成独立复评 R3；未修改固件/SCPI。
- 结论：`PASS_WITH_CHANGES`。Event Plane 文档已收敛到静态域；跨文档仍有 2 项阻塞。
- 主要发现：
  - `S-B1`：RefMem 架构/TODO 仍列 `EVENT_INJECT`/`EVENT_ABORT` 和 Event Plane 运行时 intent，与静态 only 冲突。
  - `S-B2`：Event Plane 静态 owner 未命名（`EventPlaneService` 移除后无替代）；谁解析/校验/激活 EventPack、写 EventVector 未定义。
  - `S-B3`：`SYSTem:EVENT:VALidate?` 是查询名但校验是动作；`CONFigure` 同时被写成 staging/activation，语义不清。
  - `S-M1`/`S-M2`：EventPack 路径/manifest/rollback、EventVector 存放仍未定。
- 文档：`docs/check/submissions/EVENT_CROSS_REVIEW_03.md`；索引已更新。
- 下一步：先清理 RefMem 的 `EVENT_INJECT/ABORT`，命名 `EventPackService`，拆 `CONFigure/VALidate/ACTivate`；再补 EventPack/EventVector 细节。

### EVENT-TASK-20261021-010 — Event Plane 收敛为纯静态域

- 状态：完成架构与 TODO 重写；未修改固件/SCPI 实现。
- 约束：Event Plane 目前只负责静态域，不碰实时域。
- 完成内容：
  - `REFMEM_EVENT_ARCHITECTURE.md` 重写为静态域：EventPack/EventCatalog/EventMapTable/AliasRegistry/EventVector；移除 EVG/EVR/transport/POSITION/EventSession/TriggerRun/EventStatus/EventLog 的运行时职责。
  - 运行时归 RefMem（命令/ACK）与 TRIGGER（T2/TriggerRun/core1）；Event Plane 只提供静态输入与读契约。
  - 接口收敛为 `SYSTem:EVENT:CONFigure/STATus?/MAPPing?/VALidate?`；不提供 `INJect/ABORt`。
  - `REFMEM_EVENT_TODO.md` 重写为 E0 静态契约、E1 静态实现、E2 44 条别名静态映射、E3 运行时读取契约；POSITION/T2/TDMA/EVG/EVR 标为 Out of Scope。
  - `EVENT_CROSS_REVIEW_02.md` §9 记录：B6/B7 关闭，B8/B9/B5 归运行时，B10（EventPack 路径/rollback/alias 生成）仍开放。
- 实现边界：静态表/EventPack/EventVector 代码不存在；T2 POSITION 仍是 TRIGGER 的实时实现，不属本域。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（221 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：冻结 `EVENT-PACK-01`/`EVENT-CODE-01`/`EVENT-MAPPING-01`/`EVENT-VECTOR-01`，先做 EventPack parser/validator 与 `SYSTem:EVENT:CONFigure`。

### EVENT-TASK-20261021-009 — 架构第三版复审 R2

- 状态：完成独立复评 R2；未修改固件/SCPI。
- 结论：`PASS_WITH_CHANGES`。R-B1/B2/B4 规则部分通过；R-B5 仍延期，R-B3 仍缺预算；新增 B6（POSITION 与 Event Plane EVR 接收路径重复）。
- 主要发现：
  - `R-B6`：POSITION 实时接收/执行归 TRIGGER；Event Plane EVR 不能再收 POSITION，只能通过只读 hook/记录观察。
  - `R-B7`：`EVENT_CONFIGURE` 缺 canonical SCPI 入口。
  - `R-B8`：`event_id`/`command_seq` 缺按 id 查询接口，legacy `1` 回查不可执行。
  - `R-B9`：`EventSession` 与 `EventStatus.session_state` 仍重复。
  - `R-B10`：EventPack 路径/manifest/rollback/alias 生成未定义。
- 文档：`docs/check/submissions/EVENT_CROSS_REVIEW_02.md`；索引已更新。
- 下一步：先修 `R-B6`（实时域边界），再补 `R-B7`–`R-B10`；`R-B3` 预算与 `R-B5` TDMA 联审保持开放。

### EVENT-TASK-20261021-008 — 架构补充：命令面、EventPack、传输与失败恢复

- 状态：完成架构补充；未修改固件/SCPI 实现。
- 目标：闭合复评 `R-B1`/`R-B2`/`R-B4`/`R-B5` 的“规则未定义”，并补齐 post/EVG 租约/失败恢复/契约矩阵。
- 完成内容：
  - Event Plane 架构新增 §5.4.1 传输预算/优先级；§6.1 intent handler 上下文与 ACK 写者；§6.2 event_id/command_seq；§6.3 EventPack；§6.4 post 队列/背压；§6.5 EVG 租约；§9.1 失败与恢复；§10.1 AliasRegistry 响应策略；§14.1 六项契约矩阵。
  - 复评记录 §9 补充：`R-B1/B2/B4/B5` 规则已补、仍未实现/未独立复评；`R-B3` 预算仍缺。
- 实现边界：所有规则是设计；`EventPlaneService`/EventPack/EventStatus/TriggerRun 代码不存在。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（220 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：补 `EventPlaneService` 的 core0 WCET/栈/RAM 预算（R-B3），再做 E0.1 契约冻结。

### EVENT-TASK-20261021-007 — TRIGGER 实时域 / EventVector 静态域分层

- 状态：完成静态/实时域分层修订；未修改固件/SCPI 实现。
- 决策：TRIGGER 负责实时域（T2 预测、deadline、`TriggerRun`、core1/PIO 边沿）；Event Plane 是静态/控制域；`EventVector` 只存静态事实，运行态放 `EventStatus`，实时态放 TRIGGER 的 `TriggerRun`。
- 完成内容：
  - `REFMEM_EVENT_ARCHITECTURE.md`：主域定位改为“静态/控制域 + TRIGGER 实时域”；`EventVector` 改为静态字段（pack/catalog/map/alias/EVG 席位）；新增 `EventStatus`（控制运行态）；`TriggerSession` 拆为 `EventSession`（Event Plane）与 `TriggerRun`（TRIGGER）；调度/写权限/域关系/偏差/结论同步。
  - `REFMEM_DOMAIN_ARCHITECTURE.md`：事件面挂接补“Event Plane 静态/控制、ACK 由 DistributedRefMemAO 写、实时归 TRIGGER、EventVector 静态”。
  - `REFMEM_EVENT_TODO.md`：E0.1/E1/E3 按静态/实时分层改写。
  - `EVENT_CROSS_REVIEW_01.md` §9 记录 `R-M1` 已闭合、`R-B3` 范围收窄；`R-B1/B2/B4/B5` 仍待闭合。
- 实现边界：所有 EventVector/EventStatus/TriggerRun/EventPlaneService 均为设计，代码不存在；T2 `TriggerRun` 是唯一已实现实时子集。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（220 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：先闭合 `R-B1`（handler 上下文/ACK 写者）与 `R-B2`（event_id/command_seq），再补 `R-B4`/`R-B5` 契约。

### EVENT-TASK-20261021-006 — 架构审查修正与 RefMem 微调

- 状态：完成 Event Plane 架构审查修正与 RefMem 架构微调；未修改固件/SCPI 实现。
- 目标：闭合审查的 B1（命令槽所有权）、B2（运输类）、B3（跨核 post），并把 HAOFV 符合性判定从“全部符合”降级为“符合/部分/偏差”。
- 完成内容：
  - Event Plane 架构：`DistributedRefMemAO` 为命令槽唯一 owner，Event Plane 通过 `event_plane_intent_handler` 接收 `EVENT_*`；`EventDispatchFB` 改为 post，不直调 handler；POSITION 与 CONTROL_EVENT 分 class；`EventVector` writer 重定（state=EventPlaneService，epoch=RefMem 只读副本）；新增 `event_owner` 席位、`EventPack` 装载/回滚、Log 容量预算、别名单一事实源。
  - HAOFV 符合性表改为“符合（设计）/部分/偏差”，结论改为“方向符合，不能判定符合”；§14 偏差更新为 12 项，新增 `EVENT-TRANSPORT-01`。
  - RefMem 架构微调：新增“事件面挂接（Event Plane）”小节，明确 Event Plane 不持有第二命令槽/ACK/epoch；命令类型表新增 `EVENT_CONFIGURE/EVENT_INJECT/EVENT_ABORT`；当前状态表新增 intent 分发未实现。
  - RefMem TODO P8 增加 Event Plane intent 分发任务。
- 实现边界：所有 Event Plane 代码/契约仍是设计；RefMem 仅改文档，未改 command slot 或 SCPI。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（219 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：E0.1 冻结 `EVENT-FRAME-01`/`EVENT-CODE-01`/`EVENT-MAPPING-01`/`EVENT-VECTOR-01`/`EVENT-TRANSPORT-01`，再落 E0.0 的 44 条盘点与 5 条 trigger 别名。

### EVENT-TASK-20261021-005 — 架构按 HAOFV 重写与符合性评估

- 状态：完成架构重写与 HAOFV 符合性评估；未修改固件/SCPI 实现。
- 目标：按 HAOFV 顶层原则重新生成 Event Plane 架构，并明确哪些是设计符合、哪些是未闭环偏差。
- 完成内容：
  - `REFMEM_EVENT_ARCHITECTURE.md` 重写：主域在 HAOFV 层级中的位置、HAOFV 符合性摘要表、EventPlaneAO/EventGeneratorFB/EventReceiverFB/EventDispatchFB、EventVector 字段契约、EventLogRegion、EventCatalog/EventMapTable/AliasRegistry、EventFrame 载荷所有权、调度/实时边界、写权限、canonical+alias 接口面、域关系、实现状态、HAOFV 偏差清单。
  - 结论：架构层面符合 HAOFV（表/Vector/事件/AO/FB/仲裁/服务均有落点）；实现未闭环，偏差 10 项写入 §14。
  - `REFMEM_EVENT_TODO.md` E0.1 增加 `EVENT-VECTOR-01` 和 HAOFV 符合性复核；E1 增加 EventVector/EventLog 落地。
- 实现边界：所有 `EventPlaneAO`/FB/EventVector/EventMap/AliasRegistry 均为设计，代码不存在；T2 POSITION 仍是唯一已实现子集。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（219 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：E0.1 冻结 `EVENT-FRAME-01`/`EVENT-CODE-01`/`EVENT-MAPPING-01`/`EVENT-VECTOR-01`，再做 E0.0 的 44 条盘点与 5 条 trigger 别名绑定。

### EVENT-TASK-20261021-004 — canonical `SYSTem:EVENT` + 外部别名

- 状态：完成接口方案修订；未修改固件/SCPI 实现。
- 决策：内层 canonical 统一为 `SYSTem:EVENT:*`，外部 44 条空指令作为应用层别名；别名只做名称/参数归一化，最终调用同一 `event_plane_submit`，不产生第二套语义/ACK。
- 完成内容：
  - `REFMEM_EVENT_ARCHITECTURE.md` 接口面改为“canonical + alias”：canonical 5 条命令、alias registry 规则、legacy response policy、非事件命令不强行别名。
  - `REFMEM_EVENT_TODO.md` E0.0 改为：盘点 44 条 → 冻结 canonical/alias → 先做 5 条 trigger 别名；新增 canonical/alias 映射表和默认 `target_mask` 规则。
- 实现边界：`components/event_plane`、`scpi_event_commands.*` 仍是拟建；5 条 trigger 仍是空实现；44 条盘点尚未落盘。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（219 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：`EVENT-E0.0-01` 盘点 44 条并确认 canonical/alias 分类；随后 `EVENT-E0.0-02` 冻结映射。

### EVENT-TASK-20261021-003 — 复用 44 条空指令修订接口接入

- 状态：完成 TODO 与架构接口面修订；未修改固件/SCPI 实现。
- 背景：接口域已有 44 条“接受但未执行”的命令（39 条 `scpi_port_result_accepted` + 5 条 `TRIGger:STARt/STOP/PAUSe/CONTinue/ABORt`），是应用层统一接口；原计划新增 `SYSTem:EVENT:*` 动作命名空间的做法不采纳。
- 完成内容：
  - `REFMEM_EVENT_TODO.md` 的 E0.0 改为“盘点 44 条 → 分类 → 绑定现有命令”，首批只做 5 条 trigger vertical slice；新增命令映射表，明确 `SYNC/FAULT/CAL/CONFigure/UART` 的分批处置。
  - `REFMEM_EVENT_ARCHITECTURE.md` 的接口面改为“绑定现有 44 条统一接口”，不新增平行 action 命名空间；观测复用 `SYSTem:COMMand:ACK?`/`SYSTem:REFMEM:*`/`T2:ACTion?`。
- 实现边界：`components/event_plane` 仍是拟建；44 条盘点尚未落到 `out/event/E0.0/`；5 条 trigger 仍是空实现。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（219 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：执行 `EVENT-E0.0-01` 盘点 44 条，冻结映射后做 `EVENT-E0.0-05` 的 5 条 trigger 绑定。

### EVENT-TASK-20261021-002 — TODO 细化与接口域接入第一步

- 状态：完成 TODO 细化与架构接口面补充；未修改固件/SCPI 实现。
- 目标：把“接口域如何接入 Event Plane”拆成可执行的 E0.0 任务，先 host 闭环，再上板。
- 完成内容：
  - `REFMEM_EVENT_TODO.md` 重写：新增执行原则、E0.0 接口域接入 8 项任务（北向 API、stub、SCPI 适配、命令表、schema、错误/权限、host 闭环、证据）、E0.0 命令草案、E0.1 契约、E1–E5 和验证矩阵。
  - `REFMEM_EVENT_ARCHITECTURE.md` 新增“接口面”：`SYSTem:EVENT:INJect/STATus?/LOG?/MAPPing?`，明确接口只调用 `event_plane_*`、不接触 EVG/EVR。
- 实现边界：`components/event_plane`、`scpi_event_commands.*` 均为拟建；本轮未创建代码，未执行板端验证。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（219 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、19 进展日志）。
- 下一步：按 `EVENT-E0.0-01` 冻结 `event_plane.h` 北向 API，再做 `EVENT-E0.0-02` stub 与 host 单测。

### EVENT-TASK-20261021-001 — Event Plane 独立子域建立

- 状态：完成目录与三件套建立；未修改固件、SCPI 或工具实现。
- 依据：`REFMEM_CONTROL_PLANE_REVIEW.md` 2026-10-21 评审通过，结论为以 MRF Event System 为主参考、T2 `POSITION` 作为 class 0。
- 完成内容：
  - 新建 `docs/refmem/event/`，建立 `REFMEM_EVENT_ARCHITECTURE.md`、`REFMEM_EVENT_TODO.md`、`REFMEM_EVENT_TASK_PROGRESS.md` 三件套。
  - 架构定义 Event Plane 主域定位、EVG/EVR、事件类型、事件帧、event code 命名空间、时间基准、mapping、日志、delay compensation、TriggerSession、调度边界、域关系和当前实现状态。
  - TODO 拆 E0–E5：契约与命名空间、EVR 与动作面、EVG 与注入、POSITION 接入、全目标与证据、冗余与迁移。
  - `tools/docs_check/docs_check.py` 的 `ALLOWED_PREFIXES` 增加 `EVENT`，使 `EVENT_*` 命名合规。
  - `docs/README.md` 增加 `event/` 域入口、canonical、进度路由和三个文件索引；`docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` 增加内部域与 canonical 归属。
- 实现边界：通用 EVG/EVR、事件帧、software event 注入、mapping、日志、delay compensation、冗余均未实现；当前只有 T2 `POSITION` 子集。本条目不授予“任意板触发全部动作”已产品化的结论。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 与 `python tools/doc_regression_check.py` 通过；本轮为纯文档/索引变更，未执行板端验证。
- 下一步：按 E0 冻结 `EventFrame`/`event_code`/mapping 契约，登记 `EVENT-FRAME-01`、`EVENT-CODE-01`、`EVENT-MAPPING-01`；随后做 E1 EVR 与 ActionRegistry。
