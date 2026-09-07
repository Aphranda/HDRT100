# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-07

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
