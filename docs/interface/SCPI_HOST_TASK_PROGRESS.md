# 接口域任务进度

Status: Active
Domain: INTERFACE
Canonical: `docs/interface/SCPI_HOST_TASK_PROGRESS.md`
Related: `docs/interface/SCPI_HOST_ARCHITECTURE.md`, `docs/interface/SCPI_HOST_TODO.md`, `docs/interface/SCPI_HAOFV_REVIEW.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-27

## 当前 checkpoint

### SCPI-PROGRESS-20261027-003：IF-012 Trigger 运行控制指令封装

- **范围**：不再移植 node-sequence 的 config/service store；只加 REFMEM 指令面，复用现有
  `sync_io_sequence` owner。新增 command_type 36–40、45；catalog 27 条。
- **行为**：
  - `TRIGger:STOP/PAUSe/CONTinue/ABORt` → `sync_io_sequence_stop/pause`；
  - `CONFigure:SWITch#` → `sync_io_sequence_set_switch`；
  - `TRIGger:STARt` → fail-closed `reason=7`，等待 T2 静态 plan 源/IF-013；
  - `TRIGger:MODE` 未加入，避免伪造产品模式。
- **硬件**（build `20261027045307`）：
  - `EXECute 37` STOP → ACK；`45,1,3` SWITCH → ACK；
  - `36,...` START → NACK `reason=7`；`38/39/40` → ACK。
- **凭证**：P3 `20261027045307`、scope `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、trials=8；
  host acceptance 75 passed。
- **RAM**：未新增 sequence store；仅将 `REFTMEM_DOMAIN_CONFIG_MAX_HANDLERS` 16→32
  （21 个注册所需，约 100–200 B），构建无 overflow。
- **提交**：`f8efb333`。
- **下一步**：START 接 T2 静态 plan 表；MODE 语义；IF-013 配置封装。

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
