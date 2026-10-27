# 接口域待办

Status: Active
Domain: INTERFACE
Canonical: `docs/interface/SCPI_HOST_TODO.md`
Related: `docs/interface/SCPI_HOST_ARCHITECTURE.md`, `docs/interface/SCPI_HOST_TASK_PROGRESS.md`, `docs/interface/SCPI_HAOFV_REVIEW.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-27

## 任务表

`IF-005..IF-010` 来自 2026-10-20 的 HAOFV 符合性评审（见 `docs/interface/SCPI_HAOFV_REVIEW.md`）；
它们不改变现有命令行为，冻结前仍为 PENDING。

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| IF-001 | 冻结“单串口 + 其余板卡经 RefMem 分布式控制”的**会话与寻址**语义 | PENDING | 用户 2026-10-13 指定拓扑 |
| IF-002 | 定义**命令转发与应答**规则（参考/forward 节点） | PENDING | 与 T2-AUDIT-08 同源；登记前需 C11 |
| IF-003 | 明确**隔离与权限**：哪些命令允许跨板、哪些仅本板 | PENDING | 基于实测的 4 条节点作用域命令 |
| IF-004 | 冻结**错误与故障的可观测性口径**：主机在「命令被拒绝」「传输超时」「域级故障已 latch」三种情形下分别能观测到什么、又分别由什么清除 | PENDING | 归口自 T2 域的跨域口径（T2-AUDIT-03 结论表 C1 等）；实测缺口：拒绝不应答 ⇒ 主机只能等到超时；登记前需 C11 |
| IF-005 | 移除 SCPI 回调内的 Storage job 等待与 `storage_manager_service()` 直调（H1） | PENDING | 证据与判据见 `SCPI_HAOFV_REVIEW.md` §3.1；改为提交 job + `...:JOB?`/`READ:*?` 回读 |
| IF-006 | `SYSTem:TDMA:RING:TOPology` 去掉 50×20 ms 回调内重试（H2） | PENDING | 改为 accepted/BUSY + request sequence + `RING:STATus?` 回读；与 IF-004 的拒绝可观测性同源 |
| IF-007 | 维护/查询命令（SMA/OUTPut/MODel/UART TX TEST）**强制**收敛为单槽 intent + owner 执行（H3，无例外） | PENDING | 维护强操作同样受 HAOFV 约束；需资源 claim、有界耗时、owner 与 RUN 态门禁；参照 `ARCH-IOANALYZER-02`；登记前需 C11 |
| IF-008 | 清理 Flash validation 回调擦写、未注册 raw SPI handler、`FAULT:CLEar` 空实现（M1–M3） | PENDING | Flash 移入 owner 异步 job；raw SPI 删除或改走 TDMA intent；`FAULT:CLEar` 接 owner 事件或删除 |
| IF-009 | 再生成 §10 指令树并修正顶层 `INTERFACE-*` 摘要/归属（M5–M6、L2） | PENDING | 由命令表宏装配生成或校验；顶层 12 项 vs 登记表 v4 22 项；契约文本变更走 C11 |
| IF-010 | 分配接口域错误码区间并冻结拒绝/清除语义（L1） | PENDING | 现 `2401/2402` 在冻结区间外；错误名不入队列；与 IF-004 合并冻结 |

> 来源（2026-10-13，用户裁定）：“后续只有一个串口连接到 PC，其他板卡都用 REFMEM 进行分布式控制”。IF-002 曾以 `T2-AUDIT-08` 形式误归于 T2 待办，现已移回本域；T2 待办仅保留指针。

## REFMEM 应用指令封装（node-sequence）

来源：`origin/feature/node-sequence-reservation-trigger` 已实现的 24 条 set/action 应用指令。
原则：**不新增 owner**；REFMEM 只做入口、target/required、ACK/NACK 与逐目标证据；实际动作仍由
`trigger_sequence_service`、`sync_io_sequence`、`trigger_sequence_link`、
`distributed_refmem_stage_sequence_role` 执行。查询类约 30 条保留 SCPI，不进入命令面。

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| IF-011 | 按 2026-09-01 指令表冻结 node-sequence 第一批 13 条 → REFMEM 映射、plan-ID/角度编码、查询排除、coordinator-only 与 STOP/run 门禁 | IN PROGRESS | 新表已变更：`ANGLe:PULSe/BREAkpoint` 纳入，`INPut/SPEEd/REPeat/NEXT/INJect/NODE:ROLE/LINK` 删除；映射见 `SCPI_COMMAND_PLAN.md` §14 |
| IF-012 | TRIGger 运行控制 6 条：`MODE/STARt/STOP/PAUSe/CONTinue/ABORt` + `READ:TRIGger:STATe?` 回读 | IN PROGRESS | START 必须回到 `[plan_id]` 语义（0/4 words），当前 1-word row-index 壳需回退；target NO2 START ACK，STOP 仍 PENDING；PAUSE/CONTINUE/MODE 未闭环 |
| IF-013 | Trigger/角度配置 7 条：`CONFigure:TRIGger`、`ANGLe:SWEEP/PULSe/BREAkpoint/BREAkpoint:CLEAr`、`SWITch#`、`SEQuence:ACTive` | IN PROGRESS | `CONFigure:TRIGger` 已接 config/service 并 ACK；`SEQuence:ACTive` 待 plan 写入；ANGLe 5 条仍 fail-closed；SWITch# ACK |
| IF-014 | 旧 Sequence 运行时 7 条：`REPeat/NEXT/INJect/SOURce/IO/OUTPut/CODE` | SUPERSEDED | 2026-09-01 新表已删除这些指令；不再作为本期范围 |
| IF-015 | 旧 Node/link 4 条：`NODE:LOAD/ACTIVATE`、`NODE:ROLE`、`LINK` | SUPERSEDED | 新表无 `NODE:ROLE/LINK`；`NODE:LOAD/ACTIVATE` 仅在 IF-016/节点表需要时复用 |
| IF-016 | `CONFigure:SEQuence` plan 表 + plan ID 编码；必要时参数区/table package | PENDING | **复用 T2 `trigger_action_table_t`/`trigger_action_execution_*`，不新增 sequence store**；plan_id + state_id 列表；>64 B 走参数区 |
| IF-017 | 查询/可观测性：约 30 条 READ 不封装；REFMEM ACK 后回读/聚合证据 | PENDING | 查询保留 SCPI；function 状态与逐目标 ACK |
| IF-018 | host 单测 + 四板 HIL + 负测（重复、越界、STOP/run 冲突、部分 ACK） | PENDING | 每批独立 P3；scope 覆盖 |
| IF-019 | 文档/命令树/registry 更新与收口 | PENDING | `SCPI_COMMANDS`/`SCPI_COMMAND_PLAN`/顶层；契约变更走 C11 |
| IF-020 | 旧 sequence store RAM 预算（`TRIGGER_SEQUENCE_STATE_MAX`/`PLAN_MAX`） | SUPERSEDED | 不再新增 sequence store，改用 T2 `trigger_action_table_t`；该阻塞项作废，保留历史证据 |

> 第一批建议：IF-012 + IF-013（13 条命令，全部 inline），形成 Trigger 运行/配置闭环；随后 IF-014、IF-015；
> IF-016 最后处理 plan 表；IF-017 的查询不进入 REFMEM。

> **44 条口径（2026-10-27 核对）**：`docs/refmem/event/REFMEM_EVENT_TASK_PROGRESS.md`
> 的 `EVENT-TASK-20261021-003/004` 已定义 44 条 = 39 条
> `scpi_port_result_accepted` + 5 条 `TRIGger:STARt/STOP/PAUSe/CONTinue/ABORt`；
> Event Plane 的 canonical 是 `SYSTem:EVENT:*`，44 条作为别名最终走同一个
> `event_plane_submit`，**不得产生第二套语义/ACK**。
> IF-012 的 5 条 Trigger 指令与该 44 条口径重叠：当前 REFMEM handler 直连
> `sync_io_sequence` 只是临时后端，Event Plane E2 落地时必须收敛为同一 submit，
> 不能同时保留两条 ACK 路径。

## 决策输入（IF-002）

| 项 | 选项 | 说明 |
|---|---|---|
| 转发实现 | (a) SCPI 层透明转发 | 需定寻址/应答/超时/权限/幂等；偏离 SCPI 现有边界 |
|  | **(b) RefMem 控制意图 + delta（推荐）** | 保持命令本板作用域；由参考节点写控制意图，经既有 node-load sync 帧分发，各板本地 apply |

## 决策输入（IF-004）

**归口说明**：本条是**跨域口径**，原散落在 T2 域（T2-AUDIT-03 结论表 C1 及同族观察）。按 C12 归口本域，
口径在此展开；T2 域保留其自身功能与其已冻结的实测结论，只留一行指针。

**三种现象，三种观测性（代码锚点）**：

| 现象 | 主机侧观测到的 | 由什么清除 |
|---|---|---|
| 命令被处理器拒绝 | **读不到任何应答**（`<timeout>`）；`scpi_port_push_exec_error(context, "<名称>")` 压入的名称**不进入队列消息**，`SYSTem:ERR?` 只回通用文本 | 队列条目可被 `*CLS` 清掉 |
| SCPI 错误队列有历史条目 | `SYSTem:ERR?` 逐条回；条目可很深（前面阶段每位置一条） | `*CLS`（`scpi_port.c` 的 `*CLS` 注册）；或逐条读空 |
| 域级故障已 latch | `fault` 保持；分级为 **F1 域级故障**时 `*CLS`/`*RST` **无效** | 仅**显式 CLEAR** 事件（分级表见 `docs/arch/HAOFV_ARCHITECTURE.md` 的 F0–F3） |

**对主机侧契约的含义**：

- 同一句 `*CLS` 对「错误队列」与「域级故障」的作用**不同**：前者清、后者不清。接口文档必须写明这条分界，
  否则主机无法据此判断「已经清干净」；
- 「处理器拒绝」与「传输超时」在主机侧**不可区分**——两者都表现为读不到应答，且每次拒绝都要付出一个完整的
  `serial_timeout_s`（键见 `config/hardware_acceptance/p3_bench.json`），批量命令下不可接受；
- 依赖 `SYSTem:ERR?` 归因的客户端会把拒绝读成超时（P3 验收 step 17 即因此整行作废，见本域 Task Progress）。

| 项 | 选项 | 说明 |
|---|---|---|
| 拒绝的可观测性 | (a) 拒绝时回一个定义好的否定应答（错误码 + 名称） | 主机无需依赖超时即可归因；改变现有 wire 行为，需评估既有工具 |
|  | (b) 把 `scpi_port_push_exec_error` 的名称带进队列消息 | 改动最小；仍要求主机读队列，且仍要等超时 |
|  | (c) 明确「拒绝静默」为**约定**，主机侧以 `*CLS` + `SYSTem:ERR?` 为准 | 零 wire 改动；需在架构文档写出，并让工具统一采用 |
| 清除语义的落点 | (a) 在接口架构文档写明 `*CLS` 与显式 CLEAR 的分界 | 主机据此设计「清干净」判据；需与 F0–F3 分级表对齐 |
|  | (b) 另立一条可机读的清除能力查询 | 主机不靠文档约定即可判定；新增命令面，需评估权限与幂等 |

**判据**：主机在**不依赖超时**的前提下，能区分「处理器拒绝」「传输超时」「域级故障未清」三种状态并拿到各自原因；
且接口文档明确 `*CLS` 与显式 CLEAR 的分界。

**流程**：若冻结为条款，按 `docs/check/DOCS_REGRESSION_PLAN.md` 登记 `docs/check/DOCS_REGISTRY.md` 并走 C11（禁自审自批）。

## 状态词汇

PENDING / IN PROGRESS / BLOCKED / DONE（与仓库统一状态词汇一致）。
