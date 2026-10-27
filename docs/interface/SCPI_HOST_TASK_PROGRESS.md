# 接口域任务进度

Status: Active
Domain: INTERFACE
Canonical: `docs/interface/SCPI_HOST_TASK_PROGRESS.md`
Related: `docs/interface/SCPI_HOST_ARCHITECTURE.md`, `docs/interface/SCPI_HOST_TODO.md`, `docs/interface/SCPI_HAOFV_REVIEW.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-27

## 当前 checkpoint

### SCPI-PROGRESS-20261027-012：IF-013 七条配置命令封装

- **范围**（新表）：`TRIGger:MODE` 35、`CONFigure:TRIGger` 41、
  `CONFigure:ANGLe:SWEEP` 42、`PULSe` 43、`BREAkpoint` 44、`SWITch#` 45、
  `BREAkpoint:CLEar` 46、`SEQuence:ACTive` 47；catalog 34 条。
- **行为**：7 条配置命令当前统一 NACK `reason=13`（后端未接，fail-closed）；
  `SWITch#` 仍走 `sync_io_sequence_set_switch` 并 ACK。
- **硬件**（build `20261027063907`）：
  - `35/41/42/43/44/46/47` → NACK 13；
  - `45,1,1,1,3` → ACK。
- **凭证**：P3 `20261027063907`、scope `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、trials=8；
  host acceptance 75 passed。
- **证据**：`out/n7/local_hil/if013_surface.txt`。
- **提交**：`d427fc07`。
- **下一步**：按新表接后端：`CONFigure:TRIGger` 状态表、ANGLE 配置、plan active；
  优先确定哪些 owner API 已存在，避免新增 owner。

### SCPI-PROGRESS-20261027-011：按 2026-09-01 node-sequence 指令表重冻结

- **新表来源**：`docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md` §3–§4。
- **第一批 13 条**：Trigger 6（MODE/STARt/STOP/PAUSe/CONTinue/ABORt）+
  `CONFigure:TRIGger`、`ANGLe:SWEEP/PULSe/BREAkpoint/BREAkpoint:CLEAr`、`SWITch#`、
  `SEQuence:ACTive`。
- **变化**：
  - `TRIGger:STARt [plan_id]` 回归 plan ID（0/4 words），不再用 T2 action row index；
  - `ANGLe:PULSe/BREAkpoint` 纳入，旧“unsupported”结论作废；
  - `ANGLe:INPut/SPEEd`、`REPeat/NEXT/INJect/SOURce/IO/OUTPut/CODE/NODE:ROLE/LINK` 从新表删除；
  - IF-014/IF-015/IF-020 标 SUPERSEDED。
- **固件**（build `20261027062436`）：
  - `36,1,1,0,0,0,0` START → NACK `reason=7`（plan backend 未接，fail-closed）；
  - `37,1,1` STOP → ACK。
- **证据**：`out/n7/local_hil/trigger_start_planid.txt`；P3 `20261027062436`。
- **提交**：`897f882f`。
- **下一步**：IF-013 的 7 条配置命令按新表封装；plan 后端继续复用 T2 静态表，不新增 store。

### SCPI-PROGRESS-20261027-010：target STOP 持续 PENDING

- **复测**：`EXECute 37,2,2` 连续两次 → `QUEUED`，2 s 后仍 `state=1 PENDING`；
  START 同一目标可 `state=4 ACKED`，说明 target 分发/ACK 基础路径存在，STOP 单独卡住。
- **目标板本地控制面**：`COM4 SYSTem:CTRL:EXECute 37,1,1` / `CTRL:STATus?` 均 timeout，
  说明 `CTRL:*` 是 coordinator-only，不能从目标板直接判定。
- **假设**：target STOP handler 的 `trigger_action_execution_stop()` 在 ring running/armed
  状态下阻塞或未返回，导致命令槽不写 ACK；需目标板 ACK 轨迹或把 T2 stop 移到 core1 intent。
- **证据**：`out/n7/local_hil/trigger_start_target2_mask2.txt`。
- **下一步**：给 target STOP 增加 ACK 轨迹/超时快照，或改为 core1 intent；未闭环前 IF-012 不标完成。

### SCPI-PROGRESS-20261027-009：START target NO2 正向 ACK

- **修正**：`REFMEM_COMMAND_TYPE_TRIGGER_START` 注册参数由 4 改为 1（catalog 已是 1），
  此前 handler 被参数数校验提前拒绝。
- **target 语义**：`CTRL:EXECute <id>,<target_mask>,<required_mask>,...` 是**位掩码**；
  NO2 用 `2,2`，不是 `2,1`。`36,2,1,0` 的 required 不是 target 子集，属参数错误。
- **实测**（build `20261027055237`，四板 ring up）：
  - `EXECute 36,2,2,0` → QUEUED；
  - 等待后 `CTRL:STATus?` → **state=4 ACKED**，target=2，required=2，ack=1；
  - START 正向通过。
- **残余**：`EXECute 37,2,2` STOP → 5 s 后仍 state=1 PENDING，未拿到目标板 STOP ACK；
  需查目标板 STOP 路径/命令队列。
- **证据**：`out/n7/local_hil/trigger_start_target2_mask2.txt`、`out/n7/t2_ring_up2.log`。
- **提交**：`de591ad0`（firmware）、P3 `20261027055237`。

### SCPI-PROGRESS-20261027-008：远程 target 命令超时，IF-012 正向需先补转发

- **现场**：`t2_workflow --steps 1-20 --leave-running` 全 20 步 OK；`FLIGHT:MODE 1`；
  四板 ring 已启动。
- **实测**：`SYSTem:CTRL:EXECute 36,2,1,0`（target NO2）→ **timeout**；
  `CTRL:STATus?` 仍停在上一条本地命令 `397158`，说明命令没有进入目标板。
- **归因**：`CTRL:EXECute` 的 target>1 转发/RefMem 命令通道尚未实现；本地 target=1
  又因 NO1=COUNTER 返回 NOT_LOCAL。因此 IF-012 的 START 正向被**远程分发缺口**阻塞，
  不是 T2 action row 本身。
- **影响**：IF-012 本地命令面/STOP/ABORT/SWITCH 可用；START 正向必须等 IF-002/IF-015
  的 target 转发（或明确只支持本地执行板）。
- **证据**：`out/n7/local_hil/trigger_start_target2.txt`、`out/n7/t2_ring_up.log`。
- **下一步**：先补 `CTRL:EXECute` target>1 的分发（仍复用 RefMem 命令面，不新增 owner），
  再复测 NO2/NO3/NO4 START；否则保持 fail-closed。

### SCPI-PROGRESS-20261027-007：NOT_LOCAL 是 COUNTER 角色语义，不是映射缺陷

- **核对**：`tools/sync_trigger/t2_workflow.py::action_row(0)` 对 slot0/COUNTER 明确返回
  `role=2, kind=2`（GATEWAY pulse），注释说明 COUNTER 没有自己的 action kind，
  只携带一条可验证行，**不执行本地 action**。
- **结论**：协调者 NO1 是 COUNTER，`ACTion:EXECute:PREPare 0` 返回 42/NOT_LOCAL 是预期；
  `TRIGger:STARt` 必须 target 到 NO2 SWITCH_A / NO3 GATEWAY / NO4 SWITCH_B，而不是协调者本板。
- **影响**：IF-012 START 正向不能在本板验证；需要 process-image REFMEM ring 起来后按 target 执行。
- **证据**：`t2_workflow.py` 行 46–60；`ACTion:ROW? 0` role=2；`PREPare 0 → 42`。
- **下一步**：启动四板 process-image ring，`CTRL:EXECute 36,<node>,1,0` 指向 SWITCH/GATEWAY，
  验证 START ACK + STATe；否则保持 fail-closed。

### SCPI-PROGRESS-20261027-006：T2 action table 无本板行，START 仍 fail-closed

- **实测**：安装 T2 workflow steps 1–10 后，`TRIGger:REServation:ACTion:ROW? 0` 返回 role=2、
  `TRIGger:REServation:ACTion:EXECute:PREPare 0` → **42 = NOT_LOCAL**；
  `EXECute:NOW?` → 40 = MODEL。
- **结论**：当前 T2 action table 没有协调者本板可执行行；`TRIGger:STARt` 的
  `prepare/arm` 必然 fail-closed（reason 13）。这不是封装错误，而是缺少本板 action row。
- **影响**：IF-012 的 START 正向仍待 IF-013/IF-016 安装本板表行；STOP/ABORT/SWITCH 仍 ACK。
- **证据**：`out/n7/local_hil/trigger_t2_start_positive.txt`、`out/n7/trigger_t2_stage.log`。
- **下一步**：先构造/安装本板 action row，再复测 START；PAUSE/CONTINUE/MODE 仍待 owner surface。

### SCPI-PROGRESS-20261027-005：IF-012 START 改走 T2 action table

- **决策**：复用 T2 已有 `trigger_action_table_t` / `trigger_action_execution_prepare/arm/stop`，
  不新增 sequence store、不新增静态表。
- **改动**：`TRIGger:STARt` 参数由 4-word plan ID 改为 1-word row index；catalog 同步；
  handler 调 `prepare(row)` + `arm()`；无表/越界/模型不满足时 `reason=13`。
- **硬件**（build `20261027051729`）：
  - `EXECute 37` STOP → ACK；`40` ABORT → ACK；
  - `EXECute 36,1,1,0` START → NACK `reason=13`（未安装 T2 action table，fail-closed）。
- **凭证**：P3 `20261027051729`、scope `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、trials=8；
  host acceptance 75 passed。
- **RAM**：无新增 sequence store/静态表；handler registry 仍为 32。
- **提交**：`88b87717`。
- **下一步**：安装/构造 T2 action table 后做 START 正向；PAUSE/CONTINUE 仍待 owner surface；
  MODE 语义；IF-013 配置封装。

### SCPI-PROGRESS-20261027-004：44 条口径核对与 Event Plane 重叠

- **发现**：`docs/refmem/event/REFMEM_EVENT_TASK_PROGRESS.md` 的
  `EVENT-TASK-20261021-003/004` 已定义 44 条 = 39 条 `scpi_port_result_accepted`
  + 5 条 `TRIGger:STARt/STOP/PAUSe/CONTinue/ABORt`。
- **Event Plane 决策**：canonical `SYSTem:EVENT:*`；44 条是应用层别名，最终调用同一个
  `event_plane_submit`，不产生第二套语义/ACK。
- **对 IF-012 的影响**：当前 REFMEM 5 条 Trigger handler 直连 `sync_io_sequence`，是临时后端；
  Event Plane E2 落地时必须收敛到同一 submit，不能保留两条 ACK 路径。已在
  `SCPI_HOST_TODO.md` 增加口径说明。
- **边界**：Event Plane 代码仍不存在；本轮不实现 `event_plane`，不改变 IF-012 的已经验证行为。
- **下一步**：在 IF-012 完成 core1 intent/T2 plan 前，先确认 Event Plane E2 的归属；避免重复实现。

### SCPI-PROGRESS-20261027-003：IF-012 Trigger 运行控制指令封装

- **范围**：不再移植 node-sequence 的 config/service store；只加 REFMEM 指令面，复用现有
  `sync_io_sequence` owner。新增 command_type 36–40、45；catalog 27 条。
- **行为**：
  - `TRIGger:STOP/ABORt` → `sync_io_sequence_stop`（core0 可用）；
  - `CONFigure:SWITch#` → `sync_io_sequence_set_switch`（core0 可用）；
  - `TRIGger:PAUSe/CONTinue` → `sync_io_sequence_pause`，但 owner 要求 core1，当前 core0 handler fail-closed；
  - `TRIGger:STARt` → fail-closed `reason=7`，等待 T2 静态 plan 源/IF-013；
  - `TRIGger:MODE` 未加入，避免伪造产品模式。
- **硬件**（build `20261027045307`）：
  - `EXECute 37` STOP → ACK；`45,1,3` SWITCH → ACK；`40` ABORT → ACK；
  - `36,...` START → NACK `reason=7`；
  - `38/39` PAUSE/CONTINUE → NACK `reason=13`（core1 owner 拒绝，未假 ACK）。
- **凭证**：P3 `20261027045307`、scope `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、trials=8；
  host acceptance 75 passed。
- **RAM**：未新增 sequence store；仅将 `REFTMEM_DOMAIN_CONFIG_MAX_HANDLERS` 16→32
  （21 个注册所需，约 100–200 B），构建无 overflow。
- **提交**：`f8efb333`。
- **下一步**：START 接 T2 静态 plan 表；PAUSE/CONTINUE 走 core1 intent；MODE 语义；IF-013 配置封装。

### SCPI-PROGRESS-20261027-002：IF-012 基线移植尝试与 RAM 阻塞

- **范围**：在 `wip/node-seq-refmem` 上移植 `trigger_sequence_config.h/.c`、
  `trigger_sequence_service.h/.c`，并接入 `sync_trigger_init()` 与 core1 `trigger_sequence_service_service()`。
- **编译**：C 源文件编译通过；补齐 `sync_trigger_sequence_can_start()` 后无编译错误。
- **链接（阻塞）**：全量 `STATE_MAX=256/PLAN_MAX=4` → **RAM overflow 4460 B**；
  降为 `64/2` → overflow **204 B**；降为 `48/2` → overflow **28 B**。
  结论：**容量/内存预算未冻结前不能合入**；不得静默降容量。
- **WIP 保存**：`refs/wip/node-seq-port-wip`（stash `7aa48d4b`），已切回
  `wip/closeout-all-domains`；未提交固件代码，未跑 P3。
- **下一步**：IF-020 决定 `STATE_MAX/PLAN_MAX` 或释放 RAM；再重新应用 WIP、构建、P3。
- **门禁**：本轮只提交文档；`docs_check`/`doc_regression` 通过。

### SCPI-PROGRESS-20261027-001：IF-011 基线评估与 node-sequence 映射冻结

- **范围**：`origin/feature/node-sequence-reservation-trigger` 的 24 条 set/action → REFMEM 统一面。
- **基线实测**：当前线 1384 commits、分支 65 commits；`git merge-tree --write-tree` 得到 **112 个冲突**。
  结论：**不整分支 merge**，改为 feature-port 分支，按 `trigger_sequence_config/service` →
  `trigger_sequence_link` → `sync_io_sequence` → SCPI handlers → RefMem sequence roles 的顺序移植。
- **映射冻结**：见 `docs/interface/SCPI_COMMAND_PLAN.md` §14；第一批 13 条全部 inline
  （Trigger 运行 6 + Trigger/角度配置 6 + `SEQuence:ACTive`），后续 sequence runtime 7 条、
  node/link 4 条；`CONFigure:SEQuence` 走参数区/table package；约 30 条查询保留 SCPI。
- **原则**：不新增 owner；`ANGLe:PULSe/BREAKpoint` 明确不封装；plan ID 固定 4×u32；double 用
  IEEE-754 位模式；ACK 后必须回读。
- **状态**：IF-011 文档冻结完成；尚未改代码。下一步 IF-012 基线移植 + Trigger 运行控制。
- **门禁**：`docs_check --strict-names` 与 `doc_regression_check` 通过后提交。

### SCPI-PROGRESS-20261020-001：接口域 HAOFV 符合性评审与文档回写

- **范围**：`docs/interface/` 全部文档 + `middleware/scpi_port` 命令表/分域模块；按 HAOFV
  “SCPI 只表达意图、只写 command/config slot 或读 snapshot、完成态 ACK/NACK 闭环、FB/AO
  非阻塞”逐条对照。
- **结论（快照）**：主干方向符合；不符合项集中在维护/HIL 路径——回调内等待 Storage job 并
  直调 `storage_manager_service()`（上限约 10 s）、`RING:TOPology` 回调内 50×20 ms 重试、
  SMA/OUTPut/UART 维护命令直接操作硬件、Flash validation 回调内擦写、未注册 raw SPI
  handler 残留、`FAULT:CLEar` 空实现、拒绝无应答（IF-004 延续）。
- **取证**：逐条 `文件:行号` 与违反条款见 `docs/interface/SCPI_HAOFV_REVIEW.md`；本文不复制
  评审正文，只记录闭环状态。
- **回写**：`SCPI_COMMAND_PLAN.md` 增 §13 结论表与 §10 事实边界；`SCPI_COMMANDS.md` 增
  文档边界；`SCPI_HOST_ARCHITECTURE.md` 增评审指针；`SCPI_HOST_TODO.md` 新增
  `IF-005..IF-010`；`docs/README.md` 与本域 README 增索引；`docs/arch/HAOFV_MAINTENANCE_TODO.md`
  撤销“维护接口可以保留强操作”的旧口径。
- **契约状态**：本次不登记新契约、不改任何登记行状态；评审结论冻结前仍为 PENDING，冻结需
  `DOCS_REGISTRY.md` 登记 + C11 交叉审核。
- **强制口径（2026-10-20）**：维护强操作不设例外；`IF-007` 按“intent mailbox + owner 执行”
  强制收敛，不接受把现有直驱登记为例外。`HAOFV-MAINT-20260813-008` 的旧表述不再适用于
  接口域。
- **门禁**：`docs_check --strict-names` 与 `doc_regression_check` 在回写后重跑，结果见提交记录。

### SCPI-PROGRESS-20261014-001：命令拒绝在主机侧不可观测（P3 验收 step 17 现场）

- **现场**：P3 验收 step 17（TRN-00 MARK）准备阶段，`SYSTem:TDMA:RING:TOPology <node_count>,<node>,<ref>`
  在某板上读不到应答，随后 `SYSTem:ERR?` 回一条执行错误（读数快照，非事实源；原件见
  `out/HardwareAcceptance/20261014/hawf-003351/trn00-marker-accepted-row/row_000059/repeat_01_g1791909596/summary.json`
  的 `actions`）。
- **确定性复现**：把该命令参数改成非法值（越界 slot、越界 node_count、缺参、类型错），主机侧得到**同一形状**的
  读数——读不到应答 + 队列里一条错误（`-200` / `-109` / `-104`，快照）。据此判定：**处理器拒绝时不发任何应答**，
  故主机侧「处理器拒绝」与「传输超时」**不可区分**。
- **另一条缺口**：`scpi_port_push_exec_error(context, "<名称>")` 压入的**名称不进入队列消息**，队列只回通用文本，
  因此从队列也无法看出是哪个处理器拒绝的。
- **影响面**：任何等待应答的 SCPI 客户端都会把拒绝读成超时，并为每次拒绝付出一个完整的 `serial_timeout_s`
  （键见 `config/hardware_acceptance/p3_bench.json`）；按 `SYSTem:ERR?` 归因的工具会误判——本次 P3 step 17
  即因四板并行准备中任一板命中而整行作废。
- **本轮临时处置（落在接口域之外，仅遮症状）**：`middleware/scpi_port/src/scpi_system_snapshot_commands.c` 的
  `scpi_cmd_system_tdma_ring_topology` 改为在**同一条命令内**对设置动作做有界重试，使瞬时拒绝不再暴露给主机。
  提交 `4625bb4c`；之后连续 20 次 step 17 一次通过，P3 full 亦通过
  （`out/HardwareAcceptance/20261014/hawf-085542`）。**被拒的具体原因仍未具名**（该设置路径有 5 处失败返回），
  故本域提出 `IF-004`，不在接口域内先行改 wire 行为。
- **归位说明**：本条证据由 T2 域的 P3 现场产生，但缺陷本体是**面向主机的应答契约**，故按 C12 归接口域。
- **归口（2026-10-14，用户裁定）**：这类「错误／故障可观测性」口径**统一并入本域**，T2 域保持其自身功能
  与已冻结的实测结论、只留一行指针。据此 `IF-004` 的范围由「拒绝应答语义」扩展为「错误与故障可观测性口径」，
  把三种现象的观测与清除方式并列（含 `*CLS` 清错误队列但**不清** F1 域级故障这条分界）；T2 待办已加归口指针。

### 2026-10-13 建域与现状取证

- 用户指定拓扑：后续仅一路串口接 PC，其余板卡经 RefMem 分布式控制。
- 现状实测：SCPI 命令表位于 `middleware/scpi_port/src/scpi_port.c:321`；全仓仅 4 条命令带节点作用域
  （`scpi_cmd_refmem_node_q:218`、`scpi_cmd_refmem_load_node:531`、`scpi_cmd_config_role_q:5283`、`scpi_cmd_config_calibration_q:5349`）；
  未发现通用转发/代理构造。
- 既有可复用机制：`distributed_refmem_stage_node_load`、`distributed_refmem_build_node_load_sync_frame`
  （`components/distributed_refmem/inc/distributed_refmem.h:607`）、`apply_node_load_sync_payload`（`:617`）；
  角色词汇 “reference / forward node”（`refmem_application_model.h:611`）。
- 本域三件套建立（C12/C13）；语义/状态/证据分文件，互不复制。
