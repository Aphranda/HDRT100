# TRIGGER T2 任务进展归档 07

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_07.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-19

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20261014-003` .. `T2-PROGRESS-20261013-002`（按新鲜度倒排，最新在前），共 4 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。

### T2-PROGRESS-20261014-003：GATE-001 锁态实测——台架已由主机侧保证 LOCKED，固件侧仍无门禁

- TODO task ID：`T2-GATE-001`。
- **实测（主控直达探针，复用本仓连接层；四板，静止态）**：`SYSTem:SYNC:VDC:LOCK:READiness?`（24 字段）读数——NO.1 `0010071E65B5CB38`：`input_ready=1, locked=1, reason=0, dpll.state=5(LOCKED), health=2, accepted=529, rejected=0`；NO.2 / NO.3 / NO.4：`input_ready=0, locked=0, reason=2, dpll.state=1, health=1, accepted=0`。即**静止时只有原点 LOCKED**，三块从板未就绪。
- **成因（工具 + 代码直读）**：VDC 只在 sync-IO observer 启用后接受输入；`t2_workflow.py` 步骤 16 对每板发 `SYSTem:SYNC:VDC:OBServer:TDMA`，步骤 17 再以 `LOCK:READiness?` 第 1 字段逐板判定 `locked == 1`（5 轮 × 3 s，全板达成才 break），未达成则打印 “dynamic T2 start withheld” 并 `return 3` **中止本轮**；只有全板 locked 之后才置 `LOAD:MASK` 位 6 启动 T2 相位。⇒ 台架上「已 LOCKED」这一前置**由主机侧工作流强制**，且实测可达（否则 T2 轮不会通过）。
- **GATE-001 的缺口因此定位得更准**：K-3 的前置**主机侧已强制、固件侧无检查**——生成侧 `prepare_deadlines_core0()` 与装填侧 `service_deadlines()` 都不看锁态/发布（评审 A-02 成立）。任何不经该工作流的主机路径、以及运行中锁态退化，都没有固件侧拒绝。
- **对上一轮取舍问题的回答**：既然工作流已保证四板 `locked==1` 才启动 T2 输出，固件侧落 **fail-closed** 门禁**不会改变台架行为**，只会拒绝台架不会进入的状态。故「严格门禁会让台架失去 T2 输出」的顾虑**不成立**，无需为此引入额外开关。
- **发布面的可读入口（实测存在）**：`SYSTem:REFMEM:VDC:VECtor?`、`SYSTem:REFMEM:DPLL:VECtor?`；仓库已有同时采集 readiness 与两个向量的工具 `tools/vdc/dpll_vdc_monitor/dpll_vdc_monitor.py`。
- **实现前待定（未核验）**：T2 固件应消费的**访问器**尚未确定——`REFMEM_VECTOR_FLAG_LOCKED` 今天只有定义、置位、测试与文档引用，**无固件消费方**，需先确认从 `components/sync_trigger` 读取该发布的合法入口（或经 `vdc_dpll_manager_get_snapshot()` 读同等字段而**不自行判定锁态**），再动代码。
- **未核验**：本轮未改任何源码，未运行 pytest / 构建 / 四板 P3；以上读数取自静止态，运行中锁态随 `t2_workflow` 步骤 16/17 变化（其日志已按轮打印 `lock t+Ns … LOCKED …`，可作为运行中佐证）。







### T2-PROGRESS-20261014-002：更正——「已 LOCKED 并发布」的输入确实存在，T2 未消费

- TODO task ID：`T2-GATE-001`。
- **触发**：用户指正两点——「LOCK 是可以用指令查到」「应该有进行发布」。主控据实复核后确认指正成立；本条**覆盖**同日 `T2-PROGRESS-20261014-001` 中「VDC 侧不存在可被消费的『已 LOCKED 并发布』符号」的结论（001 条原文保留，不改写）。
- **证据（主控直读）**：
  1. `components/distributed_refmem/inc/refmem_vdc_vector.h`：`REFMEM_VECTOR_FLAG_LOCKED = (1u << 5u)`。
  2. `components/distributed_refmem/src/distributed_refmem.c` 的 `distributed_refmem_vector_flags()`：在一份 guarded 快照上按**严格合取**置位——非 provisional、`dpll.debug_continue_enabled == 0`、`dpll.state == VDC_DOMAIN_LOCK_LOCKED`、`quality.valid == 1`、`quality.lock_state == VDC_DOMAIN_LOCK_LOCKED`、`quality.health_state == VDC_DOMAIN_HEALTH_HEALTHY`、`lock_quality_tier >= VDC_DOMAIN_LOCK_QUALITY_FINE_100NS`、`last_sample_time_ns != 0`、`freshness_limit_us != 0`、`last_sample_age_us <= freshness_limit_us`、`gate.passed != 0`；注释自述「No new observation, publication or formal promotion is performed by the vector writer」。该位写入两向量轮转的 VDC/DPLL RefMem 向量。
  3. 主机可见面：`middleware/scpi_port/src/scpi_sync_commands.c` 的 `scpi_cmd_sync_vdc_lock_readiness_q`（`SYSTem:SYNC:VDC:LOCK:READiness?`）回 `input_ready`、`locked = has_snapshot && dpll.state == LOCKED && input_ready`、reason、`dpll.state`、健康与接受/拒绝计数、时间戳资格、字典 CRC 等。
  4. 该标志已被测试：`tests/python/test_vdc_publication_generation.py` 断言发布向量 payload 的该位与期望一致（另有 `tests/unit/test_refmem_vector_projection.c`、`tests/python/test_vdc_idle_maintenance.py`）。
  5. 文档已记该投影语义：`docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`（该位是同一快照中健康锁定状态的投影，**不代表**完整 formal promotion）。
- **GATE-001 的真实缺口据此改写**：不是「无输入」，而是 **T2 未消费已发布的锁定标志**——全仓检索 `REFMEM_VECTOR_FLAG_LOCKED` 只有定义、置位、测试与文档引用，**尚无固件消费方**；T2 侧实际使用的仍是 `vdc_dpll_manager_get_committed_model()` 加弱判据 `model->dco.lock_state > VDC_DOMAIN_LOCK_FAULT`（把 OFF…RELOCKING 全当有效，评审 A-02 判据成立）。
- **先前结论的错误来源（据实记录）**：001 条依赖子代理结论，而该结论有两处偏差——① 把符号名写成 `REFRESH_VECTOR_FLAG_LOCKED`（代码中无此拼写，正确为 `REFMEM_VECTOR_FLAG_LOCKED`）；② 由「T2 不消费该标志」推成「不存在可消费符号」。主控未先直读 `distributed_refmem.c` 的置位点即写入文档，属**核验不足**，已按本条更正。
- **未核验**：本轮仍未运行 pytest / 构建 / 四板 P3；该标志在从板上的实际可读路径（T2 从 RefMem 区域读取的具体入口，以及是否经 `distributed_refmem` 暴露查询）与「该投影是否等同 `VDC-RUN-001` 正式判据」均待定。







### T2-PROGRESS-20261014-001：三个卡点的跨域前置核实与归口（GATE-001 / EXEC-002 / TRANSPORT-002+IDENT-001）

- TODO task ID：`T2-GATE-001`、`T2-EXEC-002`、`T2-TRANSPORT-002`、`T2-IDENT-001`。
- **取证方式**：三个**独立只读**侦察子代理分项核实（GATE-001 / EXEC-002 / 一等席与身份），均为代码 + 文档只读交叉核验，未创建/修改/删除文件、未运行构建与硬件工具；结论由主控按「实测代码 / 登记表事实 / 文档声明」三类分别复核，并**驳回了一条与主控直读冲突的子代理断言**（见末条）。
- **GATE-001：缺陷成立，且 `BLOCKED` 是实质性的**。K-3（`TRIGGER_T2_ARCHITECTURE.md`）要求「VDC 进入 LOCKED 并发布之后 T2 才有输出」，而生成侧 `prepare_deadlines_core0()` 只守 local template、会话/token/`dco.valid`/period，装填侧 `service_deadlines()` 只守 `cursor_valid` 与身份/期限，**两侧都没有锁态或发布检查**；sync_trigger 内唯一相关判据是 `model->dco.lock_state > VDC_DOMAIN_LOCK_FAULT`（`trigger_time_cursor.c`、`trigger_time_projection.c`），该「枚举最大值」判据把 OFF…RELOCKING 全部当作有效。**主机实测反证**：`tests/python/test_trigger_t2_runtime.py` 的 fake 模型未置 `lock_state`（= OFF）却断言装填成功。VDC 侧**无**可被消费的「已 LOCKED 并发布」符号（`VDC-LOCK-001`/`VDC-RUN-001` 在 `docs/vdc/VDC_DOMAIN_TODO.md` 均 PENDING、登记表零命中、无实现；`published_update_seq`、`REFRESH_VECTOR_FLAG_LOCKED` 等按 VDC 域架构自述**不代表**完整 formal promotion）。⇒「消费 VDC 提供的发布状态」这一半**当前不可实现**；T2 侧可动部分为按 K-3 落 fail-closed 拒绝（非 LOCKED/未发布一律不生成不装填）+ 非发布锁态主机负测。
- **EXEC-002：依赖长期无受理方**。现状确为单槽——`sync_io_switch.c` 以 `SYNC_IO_SWITCH_PENDING` 显式拒绝，`sync_io_gateway.c` 经 `prepare_realtime()` 的 pending 标志间接拒绝（失败路径另清 OUTPUT/READY FIFO），两侧各只有一份 `s_deadline`，头文件自述 “One transaction at a time, Core1 only.”。SYNC_IO 域此前**无任何对应任务项**（该域无 REVIEW 文件，找不到任何受理记录），已在该域登记 `SYNC-SCHED-001`（含退出门禁）。
- **TRANSPORT-002 / IDENT-001：登记落点缺失，且原表述含一处误报**。`EXE-SEAT-01` **有定义**（`docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 的「特等席增设须用户审核」节），但该文档 `Status: Draft`、**不可作 clause_loc**、自述不新增 wire 契约，且在登记表中**无行** ⇒ 原「按 `EXE-SEAT-01` 登记（C11）」仍无落点。真实落点为 TDMA 域 2026-10-14 建立的 `TDMA-SEAT-01`（一等席容器与预算条款登记）。布局事实（10 B 子区 = 24 B body 的相对偏移 6，绝对偏移 14..23；`_Static_assert(TRIGGER_POSITION_SLICE_WIRE_SIZE == TDMA_PROCESS_IMAGE_REFMEM_SIZE, …)`）经代码复核成立。
- **评审引用行号已漂移（据实记录，未改评审原件）**：主控直读确认 A-02 所引 `trigger_t2_runtime.c:700-702` 现为 SyncIO 回执读取、`trigger_time_cursor.c:201` 现为溢出保护，对应的检查现位于别处；子代理另报 A-16 所引 `sync_io_switch.c:332-338` 处实为服务函数（拒绝条件在同文件更后处）、`trigger_t2_runtime.c:395-416` 现为 mailbox 读取。
- **被驳回的子代理断言**：EXEC-002 侦察称 `prepare_deadlines_core0()` 「在现文件中已不存在（更名为 `service_deadlines()`）」。主控直读**证伪**——两者同时存在且职责不同：`service_deadlines()` 是装填路径（Core1 侧准入与提交），`prepare_deadlines_core0()` 是 Core0 侧的 deadline 解算/发布路径。故该断言未采信；登记的 `SYNC-SCHED-001` 文本不含此说法。
- **未核验**：本轮为静态只读核验，**未运行 pytest / 构建 / 四板 P3**；上述「可测 / 可接」仅为读码判断，不代表已通过。







### T2-PROGRESS-20261013-002：TDMA 域一等席布局联审完成，`T2-AUDIT-03` 结案（有条件接受）

- TODO task ID：`T2-AUDIT-03`；契约 `ARCH-TRIGGERSLICE-01`（status 保持 `pending`）。
- **联审**：核验方为**独立 TDMA 域侧 subagent**（未参与 T2 或 TDMA 任一侧起草），方式为代码 + 登记表 + 文档只读交叉核验；结论 **有条件接受（`PASS_WITH_NOTE`）**；提交单 `docs/check/submissions/TRIGGER_CROSS_REVIEW_03.md`。
- **被确认成立**：10 B 子区位于 slot 14..23（24 B core0 body 内），与 VDC 8..13／ACK 24..26／CONTROL 27..28／DIAG 29／CRC 30..31 **无重叠无空洞**；`_Static_assert(TRIGGER_POSITION_SLICE_WIRE_SIZE == TDMA_PROCESS_IMAGE_REFMEM_SIZE, ...)` **逐字存在**（`distributed_refmem.c:33-34`）；发布机会轮换、「特等 `EMPTY` 让位」「两级皆空不消耗机会」「一等记录等待中不被覆盖」均为**代码机制**而非意向；`SYNC_TRIGGER` 相位预算获**独立重算确认**（相位 7、WCET 26800、窗口 335000..365500、REFMEM 298500..332000、SCPI 8 头部 + 每相位 11 字段）。
- **T2 侧据实修正登记行 4 处**：① `REPMEM-LAYOUT-01` → **`REFMEM-LAYOUT-01`**（原为**悬空引用**，真实 id 见 registry:22）；② 「前 8 B 为 **core1** 元数据」→ mailbox **fast header，由 Core0 镜像构建写入**（`distributed_refmem.c:984-990`），Core1 仅上车前复验；③ 「offset 14」→ **slot 绝对偏移 14**（24 B body 内相对偏移 6）；④ 版本耦合由「尺寸随布局版本耦合」收紧为**符号级**，并写明 `TDMA_PROCESS_IMAGE_LAYOUT_VERSION` 自引入未递增、混版本保护实际靠 **class 白名单拒帧**（普通 transport 白名单不含 0x15）。
- **跨域未闭合条件（仅指针，不计为 T2 待办项）**：TDMA 侧 **C1**（`TDMA-SEAT-01` 一等席容器与预算条款缺失）、**C2**（`TDMA-PROCESSIMAGE-01` 待补 RefMem 子区按 class 复用与版本递增规则）、**C3**（`SHARE_MAX == WAIT_MAX == 8`：合规 8:1 按设计置位 `SEAT:HEALth?` bit3）、**C4**（TDMA 不声明时间期限）；arch 侧 **C5**（`ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md:57` 引用**已删除**符号 `TDMA_SEAT_ROTATION_*_SHARE` 且称「编译期常量」，违反硬约束 2；另有多处已作废模型描述）。
- **零命中（治理缺口）**：`TDMA-SEAT-01/02/03` 在 `docs/check/DOCS_REGISTRY.md` 中**无任何登记行**；`EXE-SEAT-01` **全库无定义、无登记、无结论**，却被 T2 与 TDMA 共 5 处当作前置引用 ⇒ `T2-IDENT-001` 的「按 `EXE-SEAT-01` 登记」**目前无落点**。**（更正 2026-10-14：`EXE-SEAT-01` 并非「全库无定义」——它在 `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 的「特等席增设须用户审核」节有定义；成立的是「无登记行、无用户审核结论」，且其宿主文档为 Draft、不可作 clause_loc。落点已改指 TDMA 域 `TDMA-SEAT-01`。见 `T2-PROGRESS-20261014-001`。）**
- **未核验**：本次为**静态只读核验**，不含编译、pytest 与四板 P3；所有实测数字（2.13 ms 快速路径间隔、四板送达/冻结等）均为**文档快照**，仅核其与编译期常量自洽。







