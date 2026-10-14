# 接口域任务进度

Status: Active
Domain: INTERFACE
Canonical: `docs/interface/SCPI_HOST_TASK_PROGRESS.md`
Related: `docs/interface/SCPI_HOST_ARCHITECTURE.md`, `docs/interface/SCPI_HOST_TODO.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-14

## 当前 checkpoint

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
