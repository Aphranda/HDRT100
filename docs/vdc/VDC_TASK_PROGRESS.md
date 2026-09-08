# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-08

本文只记录当前 VDC 迁移的实施 checkpoint 和证据闭环。任务状态以 `VDC_DOMAIN_TODO.md`
为唯一事实源，稳定语义以 `VDC_DOMAIN_ARCHITECTURE.md` 为准。重构前的长历史记录已移入
`docs/legacy/vdc/`，不再混入当前 checkpoint。

## 文档接口

| 字段 | 规则 |
|---|---|
| `Progress ID` | 每条记录唯一，格式为 `VDC-PROGRESS-YYYYMMDD-NNN`。 |
| `TODO task ID` | 必须引用 `VDC_DOMAIN_TODO.md` 中的稳定 Task ID。 |
| 状态 | 使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`；不在本文擅自改变 TODO 状态。 |
| 证据 | 命令、build/HIL、失败、回退和 `out/` 路径只记录已发生事实。 |
| 下一 gate | 必须指向一个 TODO Task ID 或明确外部阻塞。 |

## 当前 checkpoint

当前迁移顺序：

```text
VDC-TDMA-001
  -> VDC-CAL-001
  -> VDC-EVID-001
  -> VDC-SERVO-001 / VDC-SERVO-002
  -> VDC-LOCK-001
  -> VDC-SNAPSHOT-001
  -> VDC-HOLD-001
  -> VDC-RUN-001
  -> VDC-VERIFY-001
```

当前允许推进的 gate 是 `VDC-TDMA-001`、`VDC-CAL-001` 和 `VDC-EVID-001`；
`VDC-SERVO-001/002` 在正式 evidence 未闭环前只允许 host/replay 验证，不得用于发布板端目标锁。

## 进度记录

### VDC-PROGRESS-20260908-001 — debug admission continuation and five-board diagnostic P3

- TODO task ID：`VDC-EVID-001`、`VDC-VERIFY-001`、`VDC-OBS-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `d126fa2` 增加 debug-only admission continuation。recoverable evidence
  gate 的 raw code/slot/evidence sequence 由 VDC snapshot 保留，但该样本不进入 PI/DCO，
  不改变 accepted/rejected sample count；结构性 identity/schedule/window contract 错误仍
  严格拒绝。SCPI `DPLL:OVERRide` 使用 Core0 单槽 mailbox，TRN-03 只在同时指定
  `--diagnostic-continue` 和 `--dpll-provisional` 时启用，并记录 requested/applied
  generation。debug 状态下 RefMem 不发布 formal locked flag。
- 软件验证：VDC domain host C、RefMem VDC vector host C，以及 DPLL/NO5/SyncIO/TRN-03/P3
  Python 回归均通过；release build 和 staged hardware-acceptance fingerprint gate 均通过。
- 构建与 P3：五板 OTA 和默认 quick P0--P3/TRN-03 的 current-source diagnostic receipt
  位于 `out/HardwareAcceptance/20260908/vdc-debug-admission-p3-20260908/`。四板 TRN-03
  realtime/closed-loop、TDMA preflight 和 process-image soak 通过；每块 ring Node 的
  debug admission 都读回 `ACTIVE`，requested/applied generation 一致，TDMA receive 与
  transport reject 增量为零。NO1--NO4 internal DPLL SD capture 已保留。
- 失败与边界：该 receipt 的 strict gates 仍未闭合。NO5 raw waveform 有 SD segment drop，
  未产生完整 phase round；quick flow 也超过既有时间预算。两项原始原因保留在
  `diagnostic.json`、NO5 waveform segment 和 SVG 中，不能用于宣称收敛或
  `FORMAL_LOCKED`，也不应归因于 DPLL admission 或作为屏蔽 TDMA 节点的理由。
- 下一 gate：`VDC-OBS-001`。先完成 runtime producer-to-Core0 bounded batch 接口，再推进
  `VDC-OBS-002` 的分段流式 SD 写入，消除 NO5 长期观测的 storage backpressure/drop
  缺口；之后才重新评估 `VDC-EVID-001`、`VDC-SERVO-002` 和 `VDC-VERIFY-001`。

### VDC-PROGRESS-20260907-005 — T3 matrix identity and Windows progress publish recovery

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 结论：独立 T3 没有使用错误校准矩阵。验收内置和独立入口均调用
  `tools/calibration_ring_validate/trn03_closed_loop.py`；独立复现读取与验收相同的
  `trn03-matrix.json`，generation、topology/profile/schedule CRC、物理节点顺序和
  offset row 均一致。复位后独立 T3 的启动稳定门和四节点 process-image soak 通过，
  证明先前失败来自运行起点/首帧边界状态而非矩阵选择。
- 工具修复：`ProgressReporter` 不再固定复用 Windows 的 `progress.json.tmp`；每次发布
  使用唯一 pending 文件，目标被 IDE/扫描器短暂锁定时写入带序号 fallback 并继续实时
  gate。新增锁占用回归测试；`tests/python/test_trn03_closed_loop.py` 为 `108 passed`。
- 硬件证据：最终源码 build `20260907110305` 的 quick P3/五板 OTA 证据位于
  `out/HardwareAcceptance/20260907/vdc-t3-progress-fix-r2-20260907/`；首次 T3 因
  `2BD5090FE009FA2A` ARM transient (`arm_result=8`, `-200 Execution error`) 失败，原始
  证据保留。复位后使用同一 package 的 `resume` 证据位于
  `out/HardwareAcceptance/20260907/vdc-t3-progress-fix-r3-resume-20260907/`，T3
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`，progress
  文件完整发布，NO1–NO4 SD 样本数为 `8/13/13/10`。
- 边界：NO5 外部观测仍因 sequence skew `14`、SD dropped count `424` 未通过；本轮
  TRN-01 SCK 仍无 replay-safe row。两项均保留为严格失败/诊断反馈，不能提升为
  `FORMAL_LOCKED`，也不屏蔽 TDMA 节点。
- 下一 gate：解决 SCK replay-safe 矩阵和 NO5 外部观测/SD 连续性，再推进
  `VDC-EVID-001`；保持 provisional DPLL 只作调试反馈。

### VDC-PROGRESS-20260907-004 — quick full-flow acceptance and T3 comparison

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 验收范围：按默认 `QUICK_DIAGNOSTIC` 执行 P0–P3、T0–T3 TDMA process-image/FIFO
  短帧闭环和 DPLL；未使用 `--full`。五板 OTA、P3、T3 均保留在
  `out/HardwareAcceptance/20260907/vdc-full-acceptance-r1-20260907/`。
- 结果：build `20260907095401`；OTA 五板通过；内置 T3
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`，并保持四节点
  TDMA 运行。NO1–NO4 内部 SD 样本数为 `7/9/12/10`；曲线分析仍为诊断级
  `low_decimated`，不能提升为 `FORMAL_LOCKED`。NO5 外部观测因 ring sequence skew
  `54` 未通过，严格总验收保持失败事实；DPLL 反馈不隔离 TDMA 节点。
- T3 对照：验收编排器内置调用与独立入口均为
  `tools/calibration_ring_validate/trn03_closed_loop.py`、`process-image`、512 cycles、
  `--dpll-provisional`、clock evidence enabled、1 s/0.25 s soak。独立复现分别保留于
  `vdc-independent-t3-r1-20260907/`（persistent session）和
  `vdc-independent-t3-r3-short-open-20260907/`（`--short-open`）；两轮都在启动稳定门
  因 NO1 `rx_bad/transport_bad` 与 process reject 增长而超时。差异是验收前序 P0–P2/SMA
  与刚 OTA 的干净起点，以及串口时序环境，不是两套 T3 实现。
- 下一 gate：保持快速验收默认不开 T0–T3 capture；先处理 NO5/启动稳定性和 NO1–NO4
  收敛数据，再推进 `VDC-EVID-001`/`VDC-VERIFY-001`，不得用 provisional 或诊断结果
  宣称正式锁相。

### VDC-PROGRESS-20260907-003 — DPLL feedback without node quarantine

- TODO task ID：`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 变更：DPLL 失锁、phase residual 超限和 DPLL phase 的 WCET/deadline 计数保留为
  调试反馈；只要 TDMA UP/DOWN、process-image、FIFO 和基础收发连续，Core1 不再因
  DPLL phase 超限新增 `quarantined_mask` 或屏蔽节点。验收报告将 DPLL feedback 与
  TDMA 节点健康分开记录，调参器继续使用失锁/residual/frequency/reject 反馈小步
  调整并回退，等待连续样本逐渐收敛。
- 软件验证：相关 TDMA/P3 Python 回归通过；固件构建和五板 quick P3 证据分别保留
  在对应 `out/HardwareAcceptance/20260907/` 目录。当前硬件诊断仍可能因内部捕获
  无样本、NO5 SD dropped count 或波形稳定窗口不足而不构成 formal lock。
- 下一 gate：在不隔离 TDMA 节点的前提下重新收集 NO1–NO4 `FILTer?`/SD residual
  曲线，确认调参后的连续样本确实收敛，再评估 `VDC-SERVO-002`。

### VDC-PROGRESS-20260907-002 — debug Type-II PI tuning path

- TODO task ID：`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 变更：补齐 `loop_filter_integrator_ppb` 和 anti-windup；保留 FLL
  `last_frequency_error_ppb` 与 PI 积分状态的可观测分离。新增 debug SCPI
  `DPLL:TUNE`/`COEFficient`/`FILTer?`/`DEFAult`，通过 Core0 mailbox、Core1 service
  boundary 和 requested/applied generation 生效；新增 `tools/dpll_servo_tune/`
  对 NO1–NO4 逐步试探、评分、接受/回退并写入 JSON 原始响应。
- 调试语义：异常可解析参数不因产品范围被拒绝；实时路径对中间值做饱和保护，
  参数变更清空旧 acquisition/integrator history，不能自动提升 formal lock。
- 软件验证：VDC domain host C tests passed；DPLL/SCPI/残差相关 Python tests
  passed；极端 profile、signed SCPI tuple 和 anti-windup 负测已覆盖。
- 构建与 P3：本切片修改了固件、SCPI 和验收工具，必须在当前最终源码指纹下重新
  build/OTA/P3；在新 receipt 产生前不得提交或宣称硬件闭环通过。
- 下一 gate：完成当前源码指纹下的五板 quick P3，并使用调参器收集 NO1–NO4
  `FILTer?`/vector/residual 曲线；仍以 `low_decimated`/振荡事实为诊断结果。

### VDC-PROGRESS-20260907-001 — quick capture policy and internal DPLL SD evidence

- TODO task ID：`VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 变更：quick 验收默认关闭 T0–T3 SD raw capture；只有异常路径保留原始波形。新增 NO1–NO4 内部 DPLL `TRACE` ARM/STOP/SAVE、SD 下载和 residual 分析；NO5 继续作为外部只读 waveform observer。验收输出默认按 `out/HardwareAcceptance/YYYYMMDD/<run>/` 分区，显式 `--full` 才使用 full bench 配置。
- 软件验证：150 项 calibration/P3/OTA/state-machine Python 回归通过；VDC domain 与 resource arbiter host 单测通过。
- 构建与 P3：当前源码指纹下 quick P3/五板 OTA 完成，证据目录为 `out/HardwareAcceptance/20260907/vdc-internal-dpll-r2-20260907/`；NO1–NO4 各自产生可读 SD capture 与 residual SVG，NO5 waveform 另存于 `dpll-no5-observation/`。
- 结果：内部捕获链路通过，但样本量与 decimation 仍不足以宣称 formal convergence；分析报告标记 `low_decimated`，DPLL gate 失败事实保留在 diagnostic receipt。
- 下一 gate：继续定位 NO1–NO4 residual oscillation，并在增加稳定样本/完整证据后推进 `VDC-SERVO-001`。

### VDC-PROGRESS-20260906-004 — 三件标准文件基础重建

- TODO task ID：`VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001`、`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-LOCK-001`。
- 状态：DONE。
- 日期：2026-09-06。
- 变更：重建 VDC Architecture/TODO/Task Progress canonical 正文；历史版本复制到 `docs/legacy/vdc/`。
- 架构结果：明确 STATE_MACHINE、TDMA Foundation、Calibration、VdcSyncAO、SyncDpllFB、VdcVector、RefMem 和 Trigger 的 owner 边界；分离资源生命周期状态机与 VDC 锁相状态机。
- TODO 结果：建立从 resident cycle、active calibration、formal evidence、FLL 粗锁、Type-II PI、promotion、snapshot、HOLDOVER 到 RUN/HIL 的唯一迁移顺序。
- 验证：本记录完成后执行 docs_check、doc_regression、文档 pytest 和 Git Bash pre-commit。
- 证据：历史原文快照位于 `docs/legacy/vdc/`；工具中间快照位于 `out/doc-archive/vdc-20260906/`。
- 下一 gate：`VDC-TDMA-001`。

### VDC-PROGRESS-20260906-003 — 状态机域对齐检查

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`。
- 状态：DONE。
- 日期：2026-09-06。
- 变更：对照 `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`、`HAOFV_STATE_MACHINE_TODO.md` 和任务进度，确认 VDC 只消费 `RESIDENT_INIT -> RUNNING` 后的 cycle/latch evidence。
- 结论：`STOPPED/STAGED/ARMED/RESIDENT_INIT`、persona 切换、resource fault 和 diagnostic capture 不能产生 formal DPLL evidence；`RUNNING` 内的 `CYCLE_BOUNDARY -> LOCAL_UNLOAD -> LOCAL_LOAD -> FORWARD` 才是正式 TDMA observation 的来源。
- 阻塞：状态机任务进度中的 NO5 DPLL phase/SD writer 阻塞仍属于上游验收事实；不得用 TDMA short-frame 通过替代 VDC formal lock。
- 下一 gate：完成当前源码指纹下的 TDMA resident/hardware-latch evidence，再推进 `VDC-CAL-001` 和 `VDC-EVID-001`。

### VDC-PROGRESS-20260906-002 — TDMA 确定性同步方法与锁相模型

- TODO task ID：`VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001`、`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-LOCK-001`。
- 状态：DONE。
- 日期：2026-09-06。
- 结论：VDC 采用固定 process-image/trailer、同圈 T1/T2/T3/T4、Calibration path matrix、FLL-assisted acquisition、Type-II PI tracking 和 coarse/formal promotion。
- 参考：LinuxPTP、Chrony、NTPv4/RFC 5905、EtherCAT Distributed Clocks、IEEE 1588 hardware timestamp、White Rabbit 和 TSN/gPTP 方法边界已写入 Architecture。
- 下一 gate：先闭合 TDMA/Calibration/evidence，禁止以 `LOCKED` 或 replay passed 冒充 `FORMAL_LOCKED`。

## 验证与证据规则

每个 checkpoint 必须至少记录：

- 对应 TODO Task ID 和 owner；
- 当前源码/build/config identity；
- host/build/test 命令及结果；
- OTA/HIL/NO5/SD 原始证据路径；
- 失败原因、后继状态、回退点和下一 gate。

诊断 replay、host 单测、TDMA short-frame、NO5 外环观测和正式 VDC lock 是不同证据等级，
不得相互替代。formal promotion 失败时保留失败证据，不修改为成功状态。
