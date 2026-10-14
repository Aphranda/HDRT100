# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-10-14

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

### VDC-PROGRESS-20261014-005：门判据量化 + 两条路径同样失败（更正前四条的方向性表述）

- **A/B 实验作废（自查）**：`trn03_closed_loop.py` 直调两次（只差 `--diagnostic-continue`/`--startup-poll-interval-s`）**都在更早处中止**：`RuntimeError: 0010071E65B5CB38: recorder cancel did not complete: {'state': 5, ... 'written': 14, 'bytes': 10116}`——NO.1 残留上一轮冻结未保存的 recorder。该次 `dpll_schedule_gate.nodes` 为空、`passed` 只是初值 `False` ⇒ **门根本没被评估**，故「旗标假设被推翻」不成立，一并撤回。正确流程须**先复位（DPLL60 step 2）再跑 step 3**。
- **门的精确判据**（`trn03_closed_loop.validate_realtime_phase`）：每相位必须满足 `run_count` 增长、`overrun_count` 增量为 0、`deadline_miss_count` 增量为 0、该相位位不被 quarantine、`max_runtime_cycles <= wcet_cycles`；**dpll 相位 `diagnostic_only=True`，其失败只进 `feedback_errors` 不致败**（vdc/refmem 为致命）。
- **正确复位后的失败量化**（`dpll60-103901`，四板）：

| 板 | vdc | dpll | refmem |
|---|---|---|---|
| NO.1 | miss **519**、maxrt 11863/47000 | overrun 2451、miss 2285、maxrt **101147/39000** | 0 ✓ |
| NO.2 | miss **1203** | overrun 1425、miss 1864、maxrt **108867/39000** | miss **319** |
| NO.3 | miss **1252** | overrun 1331、miss 1918、maxrt 104604/39000 | miss **327** |
| NO.4 | miss **1258** | overrun 1341、miss 1760、maxrt 106521/39000 | miss **280** |

  ⇒ 致命项只有 **VDC（四板）与 REFMEM（三从板）的丢期限**；**DPLL 相位超预算 2.6×**（10 万 vs 3.9 万 cycles、上千次 overrun）是可疑起因，但按判据本身不致败。
- **关键更正**：**P3 的 step 24 自己的 summary 同样是 `passed=false`**（`hawf-100344/tdma-process-image/summary.json`）：NO.1 `vdc miss=202`、NO.2/3/4 `vdc miss≈493–577` 且 `refmem miss≈156–178`、DPLL `maxrt 95k–107k / wcet 39000`。⇒ **该门在 P3 与 DPLL60 两条路径上都在失败**；差别只在**谁把它当门**：P3 的 step 24 不据此判败（报 OK），DPLL60 的 step 3 据此判败（warn）⇒ 后续采集失去准备好的成品而 exit 2。故 `VDC-PROGRESS-20261014-002/003` 中「P3 通过 / DPLL60 失败」的方向性表述应读作**门的采用策略差异**，而非门本身只在一边失败。
- **未核验**：DPLL 相位为何超预算 2.6×（真实回归 or 该工况下的既有状态）未定位；未改动任何工具或固件。

### VDC-PROGRESS-20261014-004：否证「复位后未稳」假设；嫌疑转向 step 3 强制的 `--diagnostic-continue`

- **实验**：`DPLL60_workflow.py --steps '0-2'`（含四板软件复位，OK）→ 等 **150 s** → `--step 3` 单独跑。
- **结果（否证）**：step 3 **仍然 WARN 79.6 s**，`trn03 exit 1`、`realtime_gate=False closed_loop=False`，与复位后立即跑的结果一致。⇒ **`VDC-PROGRESS-20261014-003` 中「复位后未稳的窗口效应」假设不成立**，该门在此序列下是**系统性**失败（原文保留，以本条更正）。
- **新首要嫌疑**：DPLL60 的 step 3 **强制** `--diagnostic-continue`，而失败 summary 同步出现 `dpll_debug_admission=true`；P3 的 step 24 在**非诊断模式**下用同一工具跑同一 stage 却为 OK（本轮与上一轮的 P3 运行均是）。其余差异：DPLL60 未传 `--startup-poll-interval-s`（落到默认 0.1）、强制 `--cancel-unsaved-record`。
- **下一步（A/B，未执行）**：直接用 `trn03_closed_loop.py` 以 **P3 的旗标组合**（不带 `--diagnostic-continue`、带 `--startup-poll-interval-s`）跑同一 stage，与带 `--diagnostic-continue` 的一次对照；两者只在旗标上不同，可直接判定是否由诊断准入引起。
- **未核验**：未执行该 A/B；未改动任何工具；`dpll_max_runtime_exceeded_wcet` 的语义（计数器增长 vs 真实越界）仍需该窗口的相位证据判定。

### VDC-PROGRESS-20261014-003：DPLL60 工作流失败的根因是 `dpll_schedule_gate`（复位后未稳）

- **接续**：`VDC-PROGRESS-20261014-002` 把差异指向台架准备序列；本条把失败定位到**具体门**。
- **失败现场**（`out/HardwareAcceptance/20261014/dpll60-102105/tdma-process-image/summary.json`）：`passed=false`、`error=""`、`closed_loop_passed=false`、`realtime_gate_passed=false`、`diagnostic_passed=true`，其中
  `dpll_schedule_gate = {required: true, passed: false}`，节点 **NO.1**：
  `errors: ["vdc_deadline_miss_count_grew"]`，
  `dpll_feedback: ["dpll_overrun_count_grew", "dpll_deadline_miss_count_grew", "dpll_max_runtime_exceeded_wcet"]`。
  ⇒ **参考板的 VDC/DPLL 实时路径在采集窗口内丢期限计数与超限计数增长，并超过 WCET**。其余部分正常：`soak_validation.passed=true`（NO.1 `rx_good_frame_count=738`、`rx_bad_frame_count=0`）、`startup_barrier.passed=true`。
- **P3 为何通过**：P3 的 step 24 用**同一个工具**跑 `--stage process-image --dpll-provisional` 且为 **OK**（本轮与上一轮的 P3 运行均是），故门本身未变 ⇒ 差异在**板子当时的状态**。
- **两处调用差异（据实记录）**：DPLL60 的 step 3 **强制** `--diagnostic-continue` 与 `--cancel-unsaved-record`，且**未传** `--startup-poll-interval-s`（P3 传 `timing["status_poll_interval_s"]`，实测落到工具默认 `startup_poll_interval_s=0.1`）；更关键的是其 **step 2 对四板做了软件复位**，而 P3 跑到等价步骤前已用标定/训练（step 10–23）把 DPLL 跑稳十余分钟。
- **结论（待验证的假设）**：门在**复位后未稳**的窗口内必然看到计数增长，因此在 DPLL60 工作流「复位 → 立刻 process-image」的序列下失败；P3 因序列更长而通过。
- **未核验**：未验证「等待 DPLL 稳定后再跑 step 3」能否使其通过；未修改任何工具或固件；`dpll_max_runtime_exceeded_wcet` 是计数器增长还是真实 WCET 越界，需看该窗口的相位证据才能定。

### VDC-PROGRESS-20261014-002：同一采集工具两次结果不一致——P3 内部健康通过、DPLL60 工作流失败

- **用户提问**：既然 P3 包含 DPLL60，为什么 P3 能通过？是不是有工作流在占用串口？
- **P3 侧证据**（`out/HardwareAcceptance/20261014/hawf-100344` 10:13 与 `hawf-085542` 09:04）：两者的 `dpll-short-summary.json` 均为 `passed=True / evidence_status=PASS / scope_enabled=False / external_status=SKIPPED / errors=[]`，子进程自报 **`internal_health_passed: true`**。⇒ **无示波器时 P3 走的就是内部探针，且四板内部健康确实全过**；P3 确实包含 DPLL60（step 28 写 `HAOFV_P3_JOINT_PROVISIONAL_V1` 临时凭证 → step 29 联合采集 → step 30 摘要门限）。
- **DPLL60 工作流侧证据**（`dpll60-102105`）：step 6 联合采集失败（两次尝试），子进程 `internal_health_passed: false`。
- **已排除的差异源**：两次绑定**同一 build**（`20261014020356`，板子自报同值）、同一 `receipt_build`、同一 `tdma-stopped-handoff.json`（均 `passed: true`）、且都为 internal-only ⇒ **判据与凭证绑定都不是差异源**。
- **剩余差异（指向台架准备序列）**：P3 在采集前已跑完标定/训练（step 10–23）与 TDMA 段（24–27）；而 DPLL60 工作流在 step 2 做**四板复位与数据复位**、step 3（TDMA 停机态基线证据）为 warn。同样的采集面对不同准备状态给出不同结果，NO.2 表现为发布为空（`node_count=0`、`schedule_crc32=0`、epoch 全 0）且 3669/3669 全拒（见 `VDC-PROGRESS-20261014-001`）。
- **串口占用核实**：当前**无** python／台架工作流在运行（本会话全部后台作业为 completed/killed），仅两个空闲 powershell 进程且非台架工作流；上一条探针是被中断，不是端口冲突。
- **未核验**：未定位「准备序列中哪一步使 NO.2 起不来」的代码级根因；未复跑 DPLL60 验证该假设。

### VDC-PROGRESS-20261014-001：DPLL60 联合采集的发布/消费证据（四板内部解码）

- **触发**：用户要求跑一轮 DPLL60 工作流，以其为 VDC 发布证据，判断**锁定状态的发布**存在哪些问题。
- **运行**：`tools/vdc_priority_trace/DPLL60_workflow.py --full` → **step 6 联合采集失败**（两次尝试均 `Capture exited 2`），step 3/4 warn、其余 OK；运行目录 `out/HardwareAcceptance/20261014/dpll60-102105`。子进程结论：`internal_health_passed=false`、`physical_continuous_lock_qualified=false`、`exact_event_correlation_qualified=false`。
- **同一 build（`20261014020356`）上的四板发布状态**（`capture/NO*/decoded.json`，实质那次尝试，97 s）：

| 板 | schema | reason | mode | match | success | rejected | node_count | schedule_crc32 | arm/obs/rx epoch |
|---|---|---|---|---|---|---|---|---|---|
| NO.1 参考 | **10** | 8 | 0 | 27164 | 4198 | 0 | 4 | 2993488091 | 0/0/0 |
| NO.2 | **9** | **1** | 4 | **0** | **0** | **3669（全拒）** | **0** | **0** | 0/0/0 |
| NO.3 | 9 | 8 | 4 | 7864 | 1050 | 913 | 4 | 2993488091 | 2/3/4 |
| NO.4 | 9 | 8 | 4 | 7938 | 1068 | 1253 | 4 | 2993488091 | 2/3/4 |

- **发现（发布面）**：① **schema 按角色分裂**——参考发布 V10、三从板 V9，**同一构建**；V10 独有字段在从板缺失（`initial_wait_ticks=None`）。② **NO.2 的发布是空的**：`node_count=0`、`schedule_crc32=0`、epochs 全 0 ⇒ 它没有把参考发布的环身份落进自己的发布状态。③ 环运行中独立实测（`SYSTem:SYNC:VDC:LOCK:READiness?` 与 `SYSTem:REFMEM:VDC:VECtor?`）：原点 `input_ready 1 / locked 1 / dpll.state 5`，但**四板发布向量 `flags` 恒为 `0x55`**（`VALID | SCHEDULE_VALID | HARDWARE_EVIDENCE | PROVISIONAL`，**LOCKED 位恒 0**）；原点 `health=2`（**DEGRADED**）、`tier=3`、`gate=1` ⇒ 发布位要求 HEALTHY 且非 provisional，在本台架**结构上不可达**。
- **发现（消费面）**：④ NO.2 **3669/3669 全拒**却仍在 `state=3` 发布 ⇒ **消费失败不体现在发布状态里**，下游无法从发布面判断该板不可用。⑤ NO.3/NO.4 拒绝率近半（913/1253 对 1050/1068），能匹配但**不稳定**。⑥ 四板均 `complete_window_proven=false`、`physical_lock_qualified=false`。
- **工具侧两处据实记录**：① 工作流以**最后一次失败尝试**（attempt2，10.86 s、`main_completed=false`）的探针做摘要，而该探针四板缺 `final_phase_raw` ⇒ 摘要报出**误导性**的「phase status contains a non-integer field」（三从板各一条）；实质证据在 attempt1。② `physical_continuous_lock_qualified` 与 `exact_event_correlation_qualified` 在 `vdc_priority_joint_capture.joint_verdict` 中**写死 False**，属声明限制而非本轮结果。
- **与 2026-10-14 已交付判定入口的关系**：`vdc_dpll_manager_lock_verdict()`（提交 `f463ab92`）已把 `locked`/`provisional`/`published_qualified` 分离；本条发现解释了「已锁定却不可消费」的两个来源——(a) 角色间 schema 与环身份发布不统一，(b) 缺少在 provisional／DEGRADED 下仍能表达的**运行态锁定**发布面。
- **未核验**：本轮未改任何源码；未复跑 DPLL60，也未就失败做代码级根因定位（结论限于发布/消费面的证据分类）。

### VDC-PROGRESS-20260922-047：用户确认模型监控批次交付范围

- TODO task ID：`VDC-OBS-008` DONE，实际 GPIO 内部观测另列 `VDC-OBS-009` PENDING。用户明确选择“先交付现有模型监控改善，GPIO 观测另开后续任务”；本批到此停止扩展，不再把新 pad 观测的 PIO/SRAM 并发开发作为本批前置。父级长期/正式物理精度任务保持原状态。
- 交付事实：原始本地 MATCH 区间从 52 ns 到 40 ns、总区间从 172 ns 到 160 ns；输出起点区间及同配置 STOP/ARM 复采、QPC/保守 RAW 关联、正负 WARN/ERROR 修复均有独立复核和原件。以上为本批快照，详细实现、失败与边界分别见 041–044，不把模型区间宽度当作 GPIO 绝对误差。
- 当前同指纹四板 scope-on P3 为 `p3-anchor-shared-high-r1/`，build `20260922002449`，源码指纹 `36113f1f02eaba45060132d5288ff01c975bf28a46127669ae82e5149b3c4dea`；外部最大绝对相差 48.367 ns，quick 为 PASS_WITH_WARNINGS。工作树/暂存源码指纹一致，收据摘要/原生 trace/分页/外部 RAW 复算通过；最后一次硬件试验 044 已恢复四板 RAM 配置，未留下清理错误。
- 交付审计：`out/precision-budget-20260922/delivery_audit.py` / `delivery-audit.json` 逐项核对当前凭证、区间变化、定向测试 XML 和最后恢复事实。实现与收据留在暂存区、文档独立保留，未提交或推送；未删除历史失败或原型。本批交付不要求完成 GPIO 原型部署、forward-CS/bias 正式校准、逐事件数值资格或长期物理稳定性；这些缺口继续在对应后续任务中保留。

### VDC-PROGRESS-20260922-046：有限 pad 间隔的超窗拒绝与声明边界

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001` IN PROGRESS；仅 out 离线原型，无固件/工具/测试、资源或设备变更，沿用 043 的当前源码 P3。本条数值为合成模型快照，不授予物理测量资格。
- `pad_phase_bounded.pio` 保持十条只读指令，由未来 STOP owner 预装 OSR 最大轮询数与 Y 记录数，每个参考边沿重置 X，输出原始剩余 X。窗口用尽的 `UINT32_MAX` 单独记 TIMEOUT；零轮询结果记重叠/过近，不伪装精确零相差；合法正值只称 LOCAL_NEXT_EDGE，不称远端帧或计划输出的同事件配对。
- 真实 pioasm opcode 仿真覆盖十二个边界位置（含最后允许采样与首次超时）、三个 +200 ns 增量、首个输出缺失产生 TIMEOUT、持续缺输出产生指定数量超时记录后停止，以及缺参考无记录。此前无界原型及跨周期误配证据原位保留，最终结果与源码 SHA 见 `out/precision-budget-20260922/pad-phase-bounded-feasibility.json`；规则与剩余问题见 `pad-pair-admission.txt`。
- 上述改动仅解决有界等待和超窗结果表达。永久高 OUT/缺参考仍需 owner deadline/STOP；FIFO 阻塞使覆盖失效；窗口内无关边沿无法仅凭计数识别。后续接入必须携带真实参考身份、校准和生命周期证据，否则只能发布本地诊断，不能把目标缩成“计数器可运行”就关闭四板精度任务。下一 gate 是确定身份获取及资源/owner 接入的最小切片，不继续无限扩展离线验证器。

### VDC-PROGRESS-20260922-045：实际 pad 观测的资源与有限计数可行性

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001` IN PROGRESS；本轮只读生产代码并在 out 做离线原型，不改固件、工具、测试或资源分配，不运行硬件。源绑定 P3 仍沿用 043，以下容量/时序为当前代码与合成模型快照，不授予物理精度。
- 编译生产 persona 表和真实 board 配置的 host 检查确认 RUN/analyzer 冲突为 workspace（mask 512），SM、DMA、GPIO writer 无冲突；原独占 arena 在 RUN token 持有时拒绝第二 owner，释放后接受。还存在 RUN_RESERVED blanket gate、独立 analyzer manager 与共享 PIO0 runtime 的仲裁差异、runtime 不转发 analyzer start/stop 等边界；不能只拆 SRAM 或删拒绝条件就启用并发。原件为 `out/precision-budget-20260922/pad_resources.c`、`pad-resources.json`。
- 普通 RAW/EDGE 的记录时间在 Core1 drain 时取得，EDGE 默认采样周期与 snapshot 的单位/分辨率声明不能当作硬件 pad 时间；有限 burst 则使用采样索引，但固定观察 TDMA 六线，不含 OUT1。若扩为 GPIO16..29 十四位采样，现有整块容量在名义八纳秒步长下仅约 65.536 µs；已有计划/MATCH 相对位置约 142..560 µs（不同事件，仅为窗口可行性估计），不能假设两个目标边沿总能同窗。停止这条简单扩掩码路线的实施投入。
- 备选 `pad_phase_candidate.pio` 为十条只读指令的有限单向边沿计数，实际 pioasm 生成 opcode 后由 `check_pad_phase.py` 执行。十二个理想正向间隔、三个 +200 ns 增量、有限停止、持续缺边无结果、重叠零值歧义及八字 FIFO 阻塞检查通过；结果为 `pad-phase-feasibility.json`，显式 `hardware_tested=false`、`firmware_integrated=false`。独立审查发现初稿 GPIO28 是 RX CLK 而非帧 SYNC，已改 GPIO27；同时补证漏脉冲会形成跨周期非零误配，计数非零不证明事件身份。
- 下一 gate：先定义本地参考与 OUT1 的周期/身份及合法窗口，再评估复用 analyzer 既有 SM/DMA、独立记录存储和 service 的有限计数后端；不新增逐边沿 Core1 工作或 wire 席位。候选指令数超过现有 analyzer 描述，静态总和能容纳不等于实际装载、DMA 总线和 WCET 已合格；若需要新增席位或提高服务预算，回到 EXE-SEAT-01 审核。完整决策见 `pad-observer-decision.txt`。本地两 pad 间隔不直接等于四板 GPIO 相差，参考传播校准与物理输入偏差仍须独立闭合。

### VDC-PROGRESS-20260922-044：输出延迟注入区分内部模型与物理观测

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS。本批不改固件、工具或测试；按 ROI 选择一次既有 RAM 参数实验，预计约九十秒、价值 4/5，实际 outer 92.063 s。以下数值为单次对照快照，不是校准精度或事实源。
- 使用现有 joint CLI 的 `--output-delays 0 -28 -88 84`，相对基线仅 NO4 从 -116 ns 改为 +84 ns，增量 +200 ns；DPLL PI、固件和校准保持。基线为 `p3-anchor-shared-high-r1/dpll-four-board-short/`，试验为 `out/HardwareAcceptance/20260922/internal-sensitivity-no4-plus200-r1/`；固定范围和一次尝试条件见该目录 `experiment-scope.json`。这是输出侧有意延迟，不是接线故障或正式校准，不写 Flash。
- 同 build `20260922002449`，NO4 的 MATCH path CRC 和 delay 均与基线相同；四板 RUN enable 区间均为 512 ns。NO4 外部相差中位数从 +5.888 ns 到 +199.699 ns，试验范围 +180.121..+210.054 ns，绝对最大值按现行 `PHASE_WARN_NS` 输出 WARN；采集成功不表示相差小于百纳秒。内部末次 MATCH 从 `[-81,+79]` 到 `[-76,+84]` ns，宽度均为 160 ns，稳态包络为 `[-111,+104]` ns，没有相应的输出侧整体平移。
- 原始分页/native trace 与 scope RAW 经既有 `summarize_joint`/`validate_summary` 离线复算，三从各十个稳态唯一关联、RUN 查询零。outer errors/cleanup_errors/restore_needed 为空，四板原始 RAM delay 均为零且最终读回恢复零。派生报告 `out/precision-budget-20260922/no4-delay-sensitivity-summary.json` 明确不是 P3 凭证；比较报告保留原件哈希，既有同指纹 P3 收据不替换为注入实验。
- 解释：MATCH 的输入是参考事件、本地接收事件的模型投影及路径标量，独立 output delay 只在后续 RUN 规划消费。实验验证两个观测域不同，不宣称 DPLL 控制错误，也不把两个独立启动的中位数差当作精确增益或同事件测量误差。模型残差通过不能替代实际 GPIO 输出相差通过。
- ROI 回收：停止为了让内部数值接近示波器而继续微调四十/一百二十纳秒区间；上游观测变精不能补足下游观测缺失。下一 gate 先核清既有 owner/资源能否提供实际 GPIO 观测及事件关联，再选择校准/测量核心改动；不得直接将 RUN 计划/enable snapshot 当作 pad 实测，也不自动增设实时席位。forward-CS/bias 的物理资格仍未完成，不填零偏差。

### VDC-PROGRESS-20260922-043：本地 TIMER1 共享高字锚点候选

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本批未提交。选择直接影响本地 MATCH 原始区间的最小改动，不新增观测、IRQ 屏蔽、PIO/DMA 资源或实时席位。硬件数值仍以最终原件为准，以下构建/测试数字为快照。
- 语义：`tdma_event_enable_anchor.h` 将 H1/H3/L1/enable/L2/H2/H4 的双重高字检查化简为 H1/L1/enable/L2/H2。在既有 TIMER1 单调且无 reset/改钟/debug stop 前提下，高字相等足以证明两次低字在同一高字周期；低字原端点不变，回绕/逆序/相等仍拒绝，四个 fence、前后时钟核对、失败不写输出及单次 enable 均保留。外围回绕拒绝窗口可能缩小，但不会准入撕裂端点；不宣称检测完整计数周期 ABA。
- 准入实验的停止条件是实际 L1→L2 机器码变短，而非源码更短。Release A/B 相比上一 build，采样区间内去掉 enable 掩码准备和栈写，所有屏障保留；主控检查反汇编后才进入一轮实测。构建为 `out/build/anchor-shared-high-r1/`，决策及反汇编为 `out/precision-budget-20260922/shared-high-decision.txt`、`shared-high-disassembly.txt`。
- 四读 harness 执行真实生产 header，覆盖三个读边界的普通及全 64 位顶端回绕、时钟失效、空/别名指针、完整原始端点、相等/逆序及随机区间。anchor/live/exact/MATCH/TIMER1 coordinate 定向回归 300 passed，原件 `out/pytest/anchor-shared-high-r2/results.xml`；独立源码/测试复核 PASS，未由 reviewer 运行硬件。
- 实测兑现：`out/HardwareAcceptance/20260922/p3-anchor-shared-high-r1/` 使用 build `20260922002449`、source fingerprint `36113f1f02eaba45060132d5288ff01c975bf28a46127669ae82e5149b3c4dea`，quick P3 为 PASS_WITH_WARNINGS（INFO/WARN/ERROR/FATAL 为 33/19/0/0）。三从末次 MATCH 的 local 原始区间均由先前十三 tick/52 ns 缩至十 tick/40 ns，remote 均为 120 ns，总 residual 区间从 172 ns 缩至 160 ns；没有扣常数或改写端点。外部最大绝对相差 48.367 ns、INFO，三从各十个稳态唯一关联，RUN 查询零；outer 耗时 86.031 s，errors/cleanup_errors/restore_needed 为空。
- 内部稳态 extrema 为 `[-120,+97]`、`[-112,+95]`、`[-172,+99]` ns，依然存在超百纳秒观测；单次残差区间缩窄不能消除模型随时间变化或路径偏差。逐板原始 MATCH、输出锚、外部窗及报告哈希见 `out/precision-budget-20260922/run-comparison.json`。
- ROI 退出：保留这个直接降低本地观测不确定区间的简化，本批停止硬件运行及同一路径微优化。下一 gate 聚焦占剩余区间主要部分的远端参考及物理路径资格；不把这轮算术区间改善冒充完整 GPIO 误差界、逐事件内外关联或长期复现性。

### VDC-PROGRESS-20260922-042：缩短输出起点软件区间的核心实验

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本批未提交，改动仅在 SYNC_IO 既有 RUN owner 内，不增加 PIO/DMA、IRQ 屏蔽、实时席位或板端观测查询。以下数字为实验快照。
- 最近边界定位：前两轮 build、PI、output timing 和 output delay 读回一致；外部偏移集中在 NO4，中位数从 -8.022 ns 到 +149.934 ns。NO4 RUN enable 包围区间从 960 ns 到 2004 ns，其中 before→observed 为 796/1336 ns，observed→after 为 160/664 ns；路径标量差异只有数纳秒。它们是相关证据，不把区间增宽直接换算成真实输出延迟。逐板对比及原件哈希见 `out/precision-budget-20260922/run-comparison.json`。
- 核心改动：`sync_io_run_output.c` 在最终 raw 采样前完成非触发 DMA 地址配置、FDEBUG 清理和 paired 高段预装；first-low 仍按最终 sample 重编码，然后触发剩余 DMA、enable SM。取消、时钟、溢出和 deadline 失败继续异步退休，FIFO/workspace 不提前释放；单脉冲不触发零长度 DMA，后续 refill 和完整 observed→PC→after 原始证据保持不变。
- 第一版只把 duration 局部计算写到 raw 采样前，实际 Release 仍把 `__aeabi_uldivmod` 下沉进计时区间，未部署。最终改为 Core0 STOP prepare 计算不可变 `s_duration_ticks`，随既有 PREPARED release/acquire 交接，Core1 只读取。A/B 实际反汇编均确认最终 before read 到 enable 之间无 duration 除法调用；反汇编及决策记录位于上述 out 目录，不用 C 源行序冒充执行时序。
- 最终 host 定向回归 334 passed，命令为 `python -X utf8 -m pytest tests/python/test_sync_io_run_output.py tests/python/test_sync_pulse_uniform.py tests/python/test_vdc_run_output.py -p no:cacheprovider --basetemp out/pytest/run-origin-preload-r4 --junitxml out/pytest/run-origin-preload-r4/results.xml`。覆盖 paired/uniform 预装耗时后重编码、预装取消/deadline 无触发、FIFO 前缀、单/多脉冲、读数反向/回绕、连续与有限租期。r1/r2 测试编写错误原件保留；独立代码及所有权复核 PASS，Release 构建通过。
- 同指纹 P3：`out/HardwareAcceptance/20260922/p3-run-origin-preload-r1/` 使用 build `20260921184756`，source fingerprint `b528ab86ad4671fc681792b2d0583fd714b532a41918f637bb707ad524aa857c`；quick P3 为 PASS_WITH_WARNINGS（INFO/WARN/ERROR/FATAL 为 33/18/0/0），`check-staged` 通过。四板输出起点区间为 516/536/520/516 ns，对比此前通常约 960 ns 确实收窄；外部最大相差 49.958 ns、INFO，三从各十个稳态唯一关联，RUN 查询零，三从末次 MATCH 宽度仍为 172 ns。内部稳态包络为 `[-210,+100]`、`[-199,+100]`、`[-118,+105]` ns，保留其超百纳秒现象，不用外部较小值覆盖。
- 按 ROI 保留已兑现的输出起点改动。同固件/校准/补偿的一次 STOP/ARM 联合复采已通过，`joint-repeat/report.json` 耗时 89.219 s，errors/cleanup_errors/restore_needed 均为空；未重复 P3 构建、OTA、拓扑或训练。四板输出起点区间均为 512 ns，外部原始相差绝对值最大 35.865 ns，三从各十个稳态唯一关联，原生 trace/分页与 RAW 离线复算通过，末次 MATCH 区间仍为 172 ns。两次改动后窗口均兑现区间缩短，不据两个有限采样窗宣布长期复现性或物理精度完成；复算原件见 `run-preload-repeat-absolute-summary.json`，对比见 `run-comparison.json`。
- 复采暴露独立 P3 host 缺陷：`phase_max_abs_ns` 原实现取带符号最大值，把此轮负向极值 -35.865 ns 摘要成 +8.219 ns，可能漏报负向超限。修正为全部值先取 abs 再求 max，保留旧摘要，不修改波形。八个正负边界用例覆盖 ±100/101/1000/1001 ns 和全负窗口，最终 P3 DPLL/alarm 定向回归 170 passed，独立复核 PASS。本次两种数值都为 INFO，但这是数值摘要修复，不用“告警没变”掩盖错误。
- host 告警修复的同指纹验收已完成：`out/HardwareAcceptance/20260922/p3-phase-absolute-r1/` 复用上述 package/OTA 并核对四板在线 build，source fingerprint `77aa8fe516bf0c218d580ad21c6777ae6523eb2e4bf27f087e1e75abbd1324fa`，quick P3 PASS_WITH_WARNINGS、`check-staged` 通过。外部最大绝对相差 34.335 ns、INFO；内部包络为 `[-124,+101]`、`[-130,+109]`、`[-123,+106]` ns，三从各十个稳态唯一关联，RUN 查询零。outer 耗时 86.515 s，清理与恢复无错误；没有重复构建或 OTA。
- 下一 gate：停止本批硬件运行，保留已兑现输出起点优化及对称告警修复；内部 TX/MATCH residual 区间尚未缩小，物理路径不确定度及逐事件数值资格仍未闭合。继续原始参考/本地观测的误差预算与校准方案，不改阈值、不拟合输出补偿来冒充监控精度。

### VDC-PROGRESS-20260922-041：内部误差预算定位与保守窗口关联修复

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本批未提交，不改固件、Core1 探针、实时席位或物理精度契约。以下数值为实验快照。
- 离线分解 `p3-nonintrusive-correlation-v3-r3` 与 `p3-one-second-correlation-v4-r4`：三个 follower 的末次 MATCH 宽度均为 172 ns，其中远端编码区间 120 ns、本地投影 52 ns；本地原始十三个 250 MHz tick 同为 52 ns，没有几十纳秒的坐标舍入膨胀。r4 稳态 bin 最大宽度分布为 176..208 ns。路径 delay 只是暂定 reverse-DATA 转置标量，平移区间而不传播独立物理路径不确定度；后者未知，不是零。原件哈希与可复算脚本见 `out/precision-budget-20260922/width-budget.json`。
- 按 ROI 回收一秒 summary/GUARD 实验，保留 QPC 时钟证据及十秒窗口。独立审核确认现有 DMA/CPU enable anchor 已把高字读取移出低字包围区间，没有可安全直接删掉的读数；未做扣固定延迟、以中点替代端点或扩大 DMA/PIO 资源的伪收窄。下一核心候选是 NO1 原始 DMA enable 区间，但仍须先证明描述符与硬件排序方案，不以源码猜测承诺收益。
- 当前 build `20260921180908` 的四板 OTA、校准与 TDMA 证据位于 `out/HardwareAcceptance/20260922/p3-ten-second-qpc-v4-r1/`。DPLL 首次因自定义收据路径未传入 manifest 而在采样前失败，后从交接点单独恢复六十秒 scope-on，不重新递归 P0T。`dpll-recovery/report.json` 通过，清理/恢复错误为空。
- 独立审核进一步发现旧 scope 关联用写入返回时刻作触发下界，遗漏写入期间触发及 RAW 负时间段。仅在 host 离线复算改用命令 start 加 RAW 最早负偏移，终点取其后首次 STOP 回复结束；原始命令、旧采样字段和 RAW 全保留。修复后 162 项定向测试通过，独立复核 PASS。真实 RAW 复算外部最大相差 41.899 ns、三从各十个稳态唯一 bin、零歧义、RUN 查询零；内部包络仍分别为 `[-217,+102]`、`[-121,+101]`、`[-215,+108]` ns。外部 INFO 与内部超百纳秒现象分别保留，不相互覆盖。
- `corrected-scope-summary.json` 明确是旧采集的离线再分析、不是当前源码 P3 收据。随后 `p3-ten-second-qpc-v4-r2/` 通过复用同固件 package/OTA 的 `resume` 完成当前 host 源码 P3，source fingerprint 为 `f5b2d5cb15880a00c216aaafb8117cd446d19664edaf03e555ee9bad93663100`，结果 PASS_WITH_WARNINGS，`check-staged` 通过。三从仍各十个稳态唯一关联，RUN 查询零；外部最大相差升至 172.231 ns，十二窗均超百纳秒并告 WARN，内部包络为 `[-149,+107]`、`[-149,+104]`、`[-142,+104]` ns。两轮差异保留，不用较好轮次覆盖当前偏差。
- `exact_event_correlation_qualified`、`numeric_error_qualified` 均为 false；本批证明关联下界更可信，未证明 GPIO 物理精度或跨次复现性达标。下一 gate：停止重复硬件回归，先比较 r1 定向恢复与 r2 的配置、原始 enable 区间和路径/输出读回，再选择最小核心精度实验；无需因 WARN 向 P0T 递归。

### VDC-PROGRESS-20260921-040：双 STOP 仿射锚建立无扰 bin 关联

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本轮只改 host 证据关联和 P0T host 判定，不改固件、VDC owner、Core1 工作或实时席位。实现提交 `a7eb455c`，短闭环 schema 为 `HAOFV_P3_DPLL_SHORT_V3`。
- internal-seal 在四板全局 START 前、最终全局 STOP 后各取一次既有 TIMER1 bridge，原始 action 证明所有 pre 锚在最早 START 前结束、所有 post 锚在最晚 STOP 后开始；RUN 内板端查询为零。前后 clock identity、原生 trace tick、scope trigger host timestamp 和哈希原件必须一致，离线用全部仿射角点保守投影，防止中心点关联掩盖边界歧义。
- 结果只把外部窗口关联为唯一十秒 bin、歧义、capture 外或未映射；它不是逐事件身份，也没有证明内部 residual 与 GPIO 相差的数值误差。`exact_event_correlation_qualified=false`、`numeric_error_qualified=false` 保持显式；quick debug 中关联证据不足报告 false/WARN，显式 strict DPLL 才升级，符合新增诊断不得拒绝基础调试验收的执行约束。
- P0T 同轮确认只检验唯一物理线序。未校准前帧可能偶然对齐也可能完全不对齐，所以 frame/magic 只作诊断；r3 十二个有向组合的 `rx_frames` 全为零（快照），但 `rx_words` 活动仍唯一给出 NO1→NO2→NO3→NO4→NO1 并 PASS。r1 首链已由 `rx_words_delta=13696`（快照）正确识别，实际失败来自 STOP 返回兼容 ACK 后 helper 未接受；现在两个允许 ACK 都必须再由新鲜 STOP 状态读回确认。
- 软件最终依赖闭包 655 passed / 1 skipped（快照），唯一跳过为 Windows symlink 权限；Python 编译、工具治理、whitespace、`check-staged` 与 pre-commit 通过。r1 失败、r2 早期 staged 中间轮均保留，不作为最终凭证。
- 最终 r3 原件为 `out/HardwareAcceptance/20260921/p3-nonintrusive-correlation-v3-r3/`，build `20260921141824`，四板 scope-on P3 为 `PASS_WITH_WARNINGS`（快照：INFO 33 / WARN 19 / ERROR 0 / FATAL 0）。外部最大绝对相差 39.165 ns；三个 follower 各十一窗在 capture、十个稳态窗唯一关联，due=60 的一窗在 capture 后，歧义为零。最坏仿射角点速率偏差约 120.890/249.935/112.500 ppm；内部稳态 residual 为 `[-114,+100]`、`[-113,+102]`、`[-119,+105]` ns，最大 119 ns，均为本轮快照。内部按实现阈值告 WARN；既有 TDMA/时长反馈保留，DPLL 关联无 evidence error。
- 下一 gate：继续 `VDC-PRECISION-001`，缩小 host/TIMER1 仿射包围不确定区间或引入同事件标识，使 `numeric_error_qualified` 有可审计的关闭路径；同时继续 `VDC-OBS-007` 的质量发布和恢复对账。当前结果不授予 formal lock、连续 GPIO 精度或产品 RUN。

### VDC-PROGRESS-20260921-039：外部 HDO 对比下的内部稳态包络复验

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本轮由 tools 离线 decoder/analyzer 分类迁移触发同指纹四板复验，不改固件、VDC owner、Core1 工作或特等席。当前目标收紧为外部示波器对比前提，scope-off 结果不能单独关闭内部精度 gate。
- 实现提交 `fc609c64` 将 observation binary decoder 与 residual analyzer 迁入 VDC 标准目录，活跃 capture/monitor import 和 joint provenance 全部切换到 canonical；旧入口兼容、原件归档。decoder 的 CRC/schema 拒绝和 analyzer 的 metadata/坐标域/decimation/lock 声明边界保持不变。
- 软件与复核：最终聚焦依赖闭包 242 passed / 1 skipped，唯一跳过是 Windows symlink 权限；完整回归按用户反馈在无失败至 24% 时停止，不宣称本批全量通过。独立复核先指出外部 cwd 未固化一项 P2，补新旧成功/失败 CLI 的仓库外运行后最终 PASS；治理器 351 owned/visible files 闭合（快照）。
- scope-on P3：`out/HardwareAcceptance/20260921/tools-dpll-offline-evidence-r1/`，build `20260921115305`，HDO serial `HDO4A244301137`，结果 `PASS_WITH_WARNINGS`（快照：INFO 33 / WARN 18 / ERROR 0 / FATAL 0）。外部十二窗、四十八个 RAW block 均绑定收据并离线复算；最大绝对 GPIO 相差 40.151 ns，全部窗在 ±50 ns 内，外部分级 INFO。
- 同轮内部对照：NO2/NO3/NO4 约五十秒稳态 residual 包络为 `[-117,+110]`、`[-114,+101]`、`[-115,+104]` ns，最大 117 ns；末态 endpoint 最大仅 97 ns。原生分页重组、完整六十秒 coverage、成功间隔、共同 session/generation 与末 bin/endpoint 均通过。内部 extrema 继续比 endpoint 更能发现窗口内超 ±100 ns 的偏离。
- 证据边界：外部 GPIO 稀疏窗与内部 owner residual 的量、时间原点和采样时刻不同，不能逐事件相减或把 117 ns 与 40.151 ns 的差声明为监控误差；本轮只证明两条链在同一四板会话各自完整、可复算，并给内部保守包络提供外部物理对照。WARN 来自既有 TDMA 质量反馈，DPLL 外部相差为 INFO，不提升 formal lock、连续物理锁相或产品 RUN。
- 下一 gate：继续 `VDC-PRECISION-001`，建立内部 bin 与外部窗口的显式时间关联后才能量化内部监控误差；同时推进 `VDC-OBS-007` 的质量发布/恢复对账。未具备同事件关联前，不用两个 envelope 的数值差调参或授予精度。

### VDC-PROGRESS-20260921-038：四板内部稳态包络替代最终端点分级

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本轮只改 P3 host 证据消费与复算，不改固件、VDC owner、Core1 负载或特等席。
- 异常：scope-off 的 V1 P3 只检查 STOP 后 `final_phase_raw` endpoint，不能覆盖前一稳态区间的 residual 极值。对 `out/HardwareAcceptance/20260921/anomaly-fix-r1/p3-r4/` 离线重算时，旧端点为 94 ns、INFO，原生稳态包络为 119 ns、WARN；三从范围分别为 `[-113,+100]`、`[-112,+102]`、`[-119,+109]` ns（快照，非事实源）。
- 修复提交 `14742cee`：`HAOFV_P3_DPLL_SHORT_V2` 从 NO2--NO4 的 `summary.ram.bin` 提取稳态 residual extrema。完整六十秒 capture 必须满足 schema/interval、terminal/freeze、覆盖、全部 bin 有成功和成功最大间隔；首个 `INTERNAL_SETTLING_BINS` 启动 bin 完整保留、显式排除于稳态精度，最终 endpoint 必须落在最后稳态 bin。分页记录按 canonical 顺序重组并与 native bytes、`decoded.json` 双重一致。
- 身份绑定：summary 与 receipt 逐项绑定 staged 源码指纹、firmware build、按序四板 UID；三从 trace 绑定共同 session/generation、source/target slot、local/reference slot 与 node count。V1 endpoint-only 证据 fail-closed，scope-on 外部 RAW 路径未放宽。
- 软件验证：P3 DPLL/alarm/acceptance 与 VDC summary/seal 聚焦回归 296 项通过；扩大复核集合 559 项通过，独立复核 PASS。Python 编译、工具治理/catalog 与 whitespace 检查通过。
- 四板实测：`out/HardwareAcceptance/20260921/p3-internal-trace-v2-r1/p3-r1/`，build `20260921101121`，scope absent、external=SKIPPED，结果 `PASS_WITH_WARNINGS`（快照：INFO 32 / WARN 22 / ERROR 0 / FATAL 0）。NO2/NO3/NO4 约五十秒稳态包络为 `[-126,+98]`、`[-137,+99]`、`[-172,+103]` ns，总最大绝对值 172 ns，按实现阈值正确输出 `DPLL_PHASE_OUTSIDE_100NS`；完整 capture、分页重组、endpoint/末 bin 及共同 session/generation 全部通过。
- 边界与下一 gate：内部 owner residual 包络不是 GPIO 物理精度、连续波形或逐事件相关证据；启动大残差仍可审计。继续 `VDC-OBS-007` 的质量发布/恢复对账和 `VDC-PRECISION-001` 的物理测量不确定度，不因本轮 WARN 提升 formal lock 或产品 RUN。

### VDC-PROGRESS-20260920-037：示波器通道时延读回与收敛窗分级判据

- TODO task ID：`VDC-PRECISION-001`、`VDC-OBS-007`、`VDC-VERIFY-001` IN PROGRESS；以下数字为本轮实测快照，非产品资格。
- 目的：`VDC-PRECISION-001` 要求核验"探头/通道时延及测量不确定度"，本轮把两件此前只靠人工解释的事变成可复核证据——示波器自身通道时延随每个稀疏窗口读回留档；启动首点的未收敛现象写成显式、可审计、可关闭的判据，而不是默认豁免或事后口径。
- 通道时延读回：`tools/vdc_priority_trace/vdc_scope_evidence.py` 在同一 650k RAW 前缀捕获内逐通道查询 `:CHANn:TCAL?`，`channels` 每行新增 `channel_delay_s`，报告新增 `channel_delays_s` 与相对参考通道（默认 CH1）的 `channel_delays_relative_s`；查询失败或不可解析记 `None`，不伪造 0。本轮台架四通道均返回 `0.000000` s，即该仪器未声明通道间偏斜，十二个窗口读数一致（仅 1 个不同取值），故相对相位无需仪器偏斜修正——该假设现为逐窗实测事实，而非默认前提。
- 收敛窗分级判据：`tools/vdc_priority_trace/vdc_priority_joint_capture.py` 把相位判据分两级。±100 ns 仍是 fail-fast 粗门限，对含首窗的全部窗口生效（行为未变）；±50 ns 为稳态资格门限，仅对超出 `--settle-s`（默认 5 s，0 关闭）所声明收敛窗之后的窗口生效。每个 60 s checkpoint 记录 `settle_s`、`settling_windows`、`steady_windows`、`steady_failures` 与 `settling_exemptions`；判定层新增 `external_steady_state_passed` 与 `external_settling_exemptions`。
- fail-closed：checkpoint 未携带稳态分类时该项不得视为通过（`external_steady_state_passed=false` 且整体 `passed=false`）；收敛窗内超出 ±100 ns 仍判失败；缺 `due_s` 的窗口不能借用收敛豁免；`--settle-s` 必须有限且不超过 `--seconds`，否则在接触硬件前拒绝。host 测试覆盖首窗豁免与后窗不豁免、粗门限仍作用于收敛窗、0 关闭豁免、未分类不得通过、豁免计数可见及参数校验；本轮定向套件 282 项通过。
- 实测（60 s，`joint-60s-r3`，88.0 s）：12 窗全过粗门限，`external_steady_state_passed=true`、`external_settling_exemptions=0`，checkpoint 60 判 `settling=[5]`、稳态 11 窗、无稳态失败；首窗 NO2/NO3/NO4 最大绝对相位 5.945/3.945/19.944 ns 均在 ±50 ns 内，其余 11 窗最大绝对相位分别 ≤5.252/≤12.000/≤9.399 ns。同口径前一轮 `joint-60s-r2` 首窗 NO4 为 +61.986 ns（越 ±50 未越 ±100）：在本判据下该点计入 `settling_exemptions` 并被显式列出，r3 未用到豁免。两轮都说明首窗越 ±50 属正常收敛，不是需要人工解释的异常。
- 内部对照（同轮，6 bin / 60 s）：NO1 success 24103、freq/phase applies 0/0、最大成功间隔 49.633 ms；NO2 11687、18/542、56.008 ms；NO3 11760、36/569、51.379 ms；NO4 11610、15/550、52.114 ms；10 s 后内部残差包络 NO2/NO3/NO4 为 −116…+100 / −123…+102 / −113…+104 ns。内部残差与 GPIO 边沿是不同量、不同时间原点，不作逐事件或逐 bin 配对；`physical_continuous_lock_qualified` 与 `exact_event_correlation_qualified` 恒为 false。
- 原件：`out/HardwareAcceptance/20260920/joint-60s-r3/`（`capture/input-probe.json`、`capture/sample-*/{scope.json,phase.json,ch*.block}`、`capture/NO*/`、`capture/analysis/`）；对比图 `out/HardwareAcceptance/20260920/joint-60s-r3-comparison.svg`（新增收敛窗阴影、±50 ns 稳态带与收敛窗标注）；前一轮 `joint-60s-r2/` 与其对比图保留。P3 凭证 `out/HardwareAcceptance/20260920/settle-policy-preflight-r1/`（`PASS_WITH_WARNINGS`，33 INFO / 20 WARN / 0 ERROR / 0 FATAL）。
- 交付：实现与同指纹 P3 凭证提交 `4616d1b9`；文档分离提交。
- 下一 gate：继续 `VDC-PRECISION-001` 的测量不确定度与"全部有效边沿/窗口"口径，并把进度 036 的未观测区间界（5.046 s / 0.038 ns）接入发布验收；`VDC-VERIFY-001` 仍缺 RUN、角色/配置矩阵与长稳，本切片不上调 formal quality，也不宣称连续物理锁相。

### VDC-PROGRESS-20260920-036：离线同步计量工具与未观测区间有界化

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；以下数字为本轮快照，非产品资格。
- 目的：把"未观测区间有界化"从定性说法变成可复核证据，并给发布验收提供不依赖台架的离线计量。新工具 `tools/vdc/vdc_sync_metrology/vdc_sync_metrology.py` 只读冻结联合捕获证据（`capture/input-probe.json`；不连板卡、不连示波器、不引用固件符号），计算采样相位序列、窗内重复性、重叠 Allan 偏差、最小二乘频偏（1σ/2σ）与 `(|offset|+2σ)×最大空档`；收据绑定输入摘要（sha256/字节数）并记录工具自身哈希。
- fail-closed：缺时间戳或相位序列、捕获不完整、overrun、`large_phase_ambiguity`、窗口数不足、无任何平均因子达到最小差分、节拍抖动 >5% 均拒绝出收据（`--allow-incomplete`/`--allow-nonuniform` 才继续并在收据标注）；`continuous_lock_claimed` 恒为 false，`measurement_noise_floor_qualified` 恒为 false。host 测试 13 项通过：频偏回算、界=乘积、白相位噪声 ADEV 定律与 τ 标度、差分不足记录、缺时间戳/相位歧义/不完整/非均匀拒绝、输入摘要随内容变化、永不宣称连续锁相，以及真实 600 s 证据回归。
- 实测（600 s / 120 窗，`dpll-delay-center-r2/plus20-batched-600s`）：三通道频偏 +0.000124 / −0.002599 / +0.003526 ppb，2σ 0.0040 / 0.0044 / 0.0041 ppb，与既有"≤0.004 ppb（2σ）"口径一致；重叠 ADEV 在 τ=5/10/20/40/80/160 s 为 1.30/0.59/0.34/0.14/0.08/0.04 ppb（NO2，NO3/NO4 同量级），呈 ∝1/τ 的白相位噪声行为；反推白相位 σ 3.755/3.922/3.746 ns，与文档"约 3.8 ns rms、来源未定位"独立一致；τ=320 s 差分不足 8，工具拒绝外推。
- 未观测区间（新口径）：单窗可证观测约 1 ms（示波器记录 2 ms 内的边沿跨度），最大连续未观测空档 **5.046 s**、占空比 0.02%、p95 5.031 s、窗间无重叠；界 =（|offset|+2σ）× 5.046 s = **0.038 ns**，远低于 class A/B 阈值。60 s 轮（035 切片 `quality-clock-failclosed-r1/joint-gap-60s-r1`）为 −0.326/−0.403/−0.412 ppb（2σ 0.354–0.408 ppb，窗口长度不足以定 ppb 级绝对值），界 4.11 ns。
- 文档口径修正：`ARCH_FUTURE_APPLICATION_PLAN.md` 与 `ARCH_SYNC_STANDARD_CORE_DESIGN.md` 原记"单次捕获中位约 3.36 s、最大空档约 1.8 s"把捕获**墙钟时间**当观测时间（墙钟主要是主机读取与搬运开销）；以可证观测计为 5.046 s，两份 Draft 已按新口径改写并保留旧口径说明，后者第 9 节 SC-T5 前置条件按"已具备可复核证据"标注，SC-X1/SC-X3 状态不变。
- P3 与失败保留：首轮 `metrology-tool-r1` 因默认 quick 配置的 `ota_board_ids` 仍含 NO5 序列（`config/hardware_acceptance/p3_bench.json` 第 3–8 行；同文件第 59 行 `dpll_observer_board_id`）而在 OTA 阶段失败（`missing_serial_numbers=839E1AE79EA20F31`），失败证据保留、旧收据未被覆盖；按现役台架（四板 + 示波器 + 内部观测）改用 `--tdma-only` 的 `metrology-tool-r2` 通过：PASS_WITH_WARNINGS（32 INFO / 21 WARN / 0 ERROR / 0 FATAL），build `20260919163402`，1327 文件，OTA 4 板。
- 严格门禁现状（本轮实测，两轮一致）：`strict_gates_passed=false`，`diagnostic_continue=true`，`outcome=PASS_WITH_WARNINGS`。r2 保留两条失败：`TRN-00 residence matrix`（gate did not pass）与 `four-Node TDMA closed loop`（returncode 1）；035 轮保留 `four-Node TDMA closed loop`（explicit startup barrier timed out）。r2 未复用既有拓扑（重跑 P0T 线序探测），故告警构成与 035 不同（新增 9 INFO `CANDIDATE_REJECTED`、2 WARN `QUALITY_GATE_FAILED`、1 WARN `EXECUTION_FEEDBACK`；035 的 `TDMA_STARTUP_QUALITY`/`TDMA_NODE_QUALITY` 两条本轮未出现）。两轮共同的既有严格预算告警仍在（`vdc_deadline_miss_count_grew`、`vdc_overrun_count_grew`、`vdc_max_runtime_exceeded_wcet`、`vdc_load_quarantined`），既有全表严格预算未过这一结论不变；**四板 TDMA 闭环与短时 DPLL 发布尚未进入必需验收项，本轮不据此声称其通过**。NO5 是否从 quick 配置清理待用户决定（属 `config/` 变更，须重跑 P3）。
- 原件：`out/HardwareAcceptance/20260919/dpll-delay-center-r2/plus20-batched-600s/metrology.json`、`out/HardwareAcceptance/20260919/quality-clock-failclosed-r1/joint-gap-60s-r1/metrology.json`；P3 见 `out/HardwareAcceptance/20260920/metrology-tool-r2/`（失败轮 `metrology-tool-r1/` 保留）。
- 交付：实现与同指纹 P3 凭证提交 `6d004b2a`（hook 实跑：docs 门禁 OK + P3 门禁 OK）；文档分离提交。
- 下一 gate：把四板 TDMA 闭环与短时 DPLL 发布纳入必需验收（当前仅诊断告警），再推进 `VDC-EVID-001` 正式合格输入与控制迁移；计量侧下一步是仪器噪声底标定（SC-X3）与把计量收据接入 `VDC-VERIFY-001` 发布验收。本切片只提供离线界，不上调 formal quality，不宣称连续物理锁相，也不替代 GPIO 精度。

### VDC-PROGRESS-20260919-035：正式质量年龄失效锁存与待提交样本取消

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-HOLD-001` IN PROGRESS；以下数字为本轮快照。原 `vdc_domain_age_quality()` 在读钟不可用（`now_ns == UINT64_MAX`）时直接返回，旧 HEALTHY/LOCKED 与旧年龄被保留；反向时间把年龄算成 0，等于旧证据自行续期。
- 实现（Domain）：未知或饱和年龄统一锁存为 `last_sample_age_us = UINT32_MAX`——读钟不可用、时间早于样本锚点、相对已存微秒年龄回退均进入该状态；健康判定在 LOCKED/HEALTHY 与候选分支之前强制 DEGRADED（`FAULT` 仍优先），最大 freshness 上限也不能放行。仅恢复读钟不续期旧证据，仍由原 accepted 样本提交或生命周期重置解除。
- 实现（Core1）：pending preparation 的本地 apply 时间纳入复验，首样本尚无质量锚点时增加一次条件读钟；工作视图与发布视图任一新检出“有限→未知”跃迁时互相传播失效并撤销 preparation，经既有 finalize 拒绝路径退休，已生效 servo/DCO 保持不回滚。传播为边沿触发，旧发布视图的锁存状态不会反复污染后续真实新样本。
- 软件证据：Domain 回归 122 项、publication 回归 51 项、idle/aging 35 项、文档门禁 38 项通过；本轮复核复跑两个改动模块 63 项通过。首轮 publication 1 失败/50 通过（`lock_recovery` 夹具未按 accepted 语义重置年龄）与 idle 三轮 6/5/1 失败均保留原件，最终 r2/r4 通过；覆盖读钟不可用、反向时间、微秒年龄回退、FAULT 优先、最大 freshness、无参考、HOLDOVER、未发布模型隔离、accepted/rejected 恢复、pending 首次完成与跨锚点回滚。负测为 host 集成证据，不代表实板老化或正式 promotion。
- P3：Release A/B/BOOT 与固定四板快速验收通过，`p3-r1` 为 PASS_WITH_WARNINGS（25 INFO / 20 WARN / 0 ERROR / 0 FATAL），build `20260919154526`，源码 SHA `4d786e0ad296688d954c59a3acfe02fea7ffb518f375b69a40d7e4444a62c818`，1325 文件，`check-staged` 指纹核验 OK。既有全表严格预算与完整 WCET 仍未过，不据此授予资格。
- 资源对照：相对 034，text +288 B、data/bss 不增；`vdc_domain_age_quality` 174→242 B、`vdc_dpll_manager_age_quality_core1` 268→444 B，RefMem 实时入口仍 1284 B。新增条件判断与一次条件读钟有实际成本，代码大小不等于 WCET；未新增字段、诊断发布入口、席位或调度项。
- 联合实测 `joint-gap-60s-r1` PASS（89.86 s）：内部健康与十二个外部四路窗口全过、48 份 RAW 哈希一致，全部已采边沿在 ±50 ns 内（本轮日志最大绝对相差约 47.88 ns）；`physical_continuous_lock_qualified` 与 `exact_event_correlation_qualified` 仍为 false，RUN 查询为零，四板最终 STOP/恢复、无清理错误。该轮为 RAM 时序范围，不含 Flash、重启与 P0；稀疏窗口不证明未采样区间精度。
- 独立复核 `review.txt`：初判 PASS_WITH_NOTE、最终 PASS；保留的口径限制为年龄回退检测仅到已存微秒分辨率（不宣称纳秒单调性）、自然饱和同样取消在飞完成、pending 拒绝断言须以 servo 之后的 DCO/计为基线。复核同时记录一次工作区事件：reviewer 误用历史“外部一致性”指令改过两份 20260919 HTML 报告并重生成 PDF，四份报告已按 HEAD 恢复、无 diff 残留，未改产品、测试与域文档。
- 原件根：`out/HardwareAcceptance/20260919/quality-clock-failclosed-r1/`（`p3-run.log`、`resource-audit.json`、`p3-phase-comparison.json`、`review.txt`、`joint-gap-60s-r1/`）；测试日志见 `out/quality-clock-*.log` 与 `out/idle-clock-failclosed-fixed-r*.log`。
- 交付：实现与同指纹 P3 凭证提交 `bd5e7a87`；文档分离提交。
- 下一 gate：继续 `VDC-SNAPSHOT-001` 的正式合格输入接入、调试相位控制到正式控制的迁移及逐事件保持/恢复证据；该切片只让时钟异常 fail-closed，不上调 formal quality，也不扩容特等席（须用户审核）。

### VDC-PROGRESS-20260919-034：健康锁定标志与质量一致投影

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-VERIFY-001` IN PROGRESS；以下数字为本轮快照。原 RefMem `LOCKED` 位仅看 Domain 控制状态、debug 和 provisional；quality 老化为 DEGRADED 后仍可能置位，与同一向量的质量字段矛盾。
- 实现只收紧 `distributed_refmem_vector_flags()`：复核既有同快照 quality 有效性、锁状态、HEALTHY/fine、样本时间、freshness/age 及 gate。两向量复用原轮转和 CRC，不增加发布次数、时钟读取、健康镜像、wire 或特等席；保留 DCO/证据序号及 VALID/STALE 原义。此位是健康控制状态投影，不是 formal promotion，也不提升 typed FRESH。
- 软件证据：独立测试先在旧代码复现 9 失败/4 通过；修复后 publication 28 项、idle/reference/health 115 项、两/六/八节点 projection 3 项、文档 38 项通过。健康、边界、超龄、gate/valid/tier/时间缺失及矛盾快照、恢复均覆盖。正例使用健康资格夹具，经真实 Domain aging、manager 发布和两向量路径验证；不是实板正式样本准入或锁定过程证明。
- 旧 golden 三项初测因预期 LOCKED 位撤销而失败，原件留存；夹具先检查当前 payload CRC/无 LOCKED，再仅归一历史 LOCKED 位对照原 CRC golden，文件本身未改，其余字节继续受原基线约束。独立最终复核 PASS，见 `review.txt`。
- Release A/B/BOOT 与固定四板 P3 通过：build `20260919151458`，源码 SHA `d574aa0f88dcbe45793ee3b964cc13fe22020ff4ad163edb99e5af0f30d7a13d`，1325 文件，staged 指纹匹配。P3 为 PASS_WITH_WARNINGS（25 INFO / 19 WARN / 0 ERROR / 0 FATAL）；既有全表严格预算仍未过。NO1 RefMem 本轮 deadline miss 增 1、overrun 增 0，最大 17035 cycles 对配置 WCET 29000；不据此推定与本改动的因果或授予 WCET，四板相位对照见 `p3-phase-comparison.json`。
- 资源对照：相对 033，text +72 B、data/bss 不增，flags 函数 120→186 B，RefMem 实时入口仍 1284 B；代码大小不等于执行时间，既有判断增加有界成本。见 `resource-audit.json`，未调整静态预算或增加席位。
- 联合首轮 `joint-gap-60s-r1` FAIL 保留：四板 GUARD、300 ms 新参考暂停/恢复、模型及 Core0 镜像通过，外部十二窗/48 RAW 哈希通过；NO4 峰值 52.145 ns，已采边沿仍在 ±100 ns 内。适配器读取两向量时，通用串口层去掉复合结果的 OK 前缀，导致 VDC 查询解析失败、DPLL 查询未执行。只在 out 适配器登记两条复合查询，再作同配置实测；未改固件或重释首轮结论。
- 同配置 `joint-gap-60s-r2` PASS：新参考暂停至恢复约 301.598 ms，三从各记录一次恢复；四板 GUARD、模型末态与 Core0 健康镜像一致。STOP 下原生 reader 校验两向量后查询成功，各板两表分别保持 flags 85/69/69/69，均无 LOCKED，未将 typed 诊断闭环上提资格。SCPI 是字段子集，不据此声称主机独立重算完整向量 CRC。
- 复测十二个外部窗口、48 RAW 哈希通过；NO2/NO3/NO4 最大绝对相差 53.824/34.117/38.596 ns，NO2 三个已采边沿超 ±50 ns，全部已采边沿在 ±100 ns 内。两轮 RUN 查询均零，四板最终 STOP、清除注入并恢复参数，无清理错误。稀疏波形不证明未采样区间或暂停期间的连续精度，末态镜像一致不证明逐事件及时性。
- 原件根：`out/HardwareAcceptance/20260919/vector-health-lock-r1/`；失败、资源、P3、评审、采集日志及两轮 `joint-summary.json` 均留存。健康投影修复切片 DONE，整体发布/锁相目标保持 IN PROGRESS。
- 交付：实现与同指纹 P3 凭证提交 `a1d4b336`；文档分离提交。
- 下一 gate：正式 aging 对失效读钟/反向时间的 fail-closed 修复；随后明确 typed 合格证据接入及调试相位控制到正式控制的迁移。当前 phase step 遇 LOCKED 会拒绝，不能只上提一个状态位。两者是发布主线后续切片，不阻塞现有诊断闭环；不得借此扩充未经用户审核的特等席。

### VDC-PROGRESS-20260919-033：GUARD 分离启动等待与后续参考缺口

- TODO task ID：`VDC-OBS-007`、`VDC-SNAPSHOT-001`、`VDC-HOLD-001`；诊断修复切片 DONE，父任务 IN PROGRESS。以下数字为验收快照，非配置事实源。
- 根因：原 `last_success` 以 START 初始化，首次成功前的等待进入最大成功间隔；032 首轮 NO3/NO4 因此超过诊断界。新 GUARD 原生段带 `SUMMARY_STEADY_SUCCESS_GAP`，首次成功后才累计成功缺口，启动等待单列；同段后续、跨段、尾段真实缺口继续约束。首次完整非空段无成功/未绑定仍失败，当前最大段跨度为十秒，不是无限启动豁免。首次成功不是物理锁定。
- 边界：复用既有 capture 状态与原生 flags；不新增记录字节、时钟读取、快照发布或特等席，不改 wire、PIO/DMA、OTA。Core1 原诊断点增加条件判断，不能宣称零成本或完整 WCET 合格。健康镜像仍只由 Core0 发布，后续特等席增设仍须 `EXE-SEAT-01` 用户审核。
- 软件：summary/decoder/capture 252 项、GUARD 62 项及文档 38 项通过。新增真实 recorder/GUARD 场景覆盖启动等待、后续/跨段/尾段缺口、整段无参考、再 ARM、时钟/绑定/服务错误与严格边界；13 份 host 原生记录独立解码通过。旧八份实板原件仍按 `start_inclusive` 解释，032 首轮原失败保留；同捕获混合标记拒绝。
- P3：Release A/B/BOOT 与固定四板快速验收通过，`p3-r1` 为 PASS_WITH_WARNINGS（25 INFO / 18 WARN / 0 ERROR / 0 FATAL）。build `20260919145017`，源码 SHA `1189838276cc621280ef97862dbe7cd7e2c18e53e8a130d18606570c7849dff0`，1325 文件；staged 指纹核验通过。复用已知环序，不扩大 P3 门禁。
- 受控启动实测：STOP 下配置既有 GAP 将 NO1 首次新参考延迟 1200 ms，运行不查询板卡。`joint-startup-60s-r1` PASS：三从首次成功等待 1.911/2.046/2.178 s，之后最大成功间隔 32.736/26.690/32.215 ms；四板 GUARD 正常完成，未因启动等待误退休。十二个外部窗口、48 RAW 哈希通过，三从最大绝对相差 35.605/41.745/76.060 ns；NO4 一样本超 ±50 ns，全部已采样本仍在 ±100 ns 内。
- 再 STOP/ARM 的 `joint-startup-120s-r1` PASS：三从首次成功等待 1.879/2.020/2.161 s，之后最大成功间隔 34.043/38.463/36.865 ms；六十秒和一百二十秒检查点均通过。二十四个外部窗口、96 RAW 哈希通过，三从最大绝对相差 34.072/25.986/25.959 ns，已采样本均在 ±50 ns 内。两轮模型末态一致、RUN 查询均为零，四板最终 STOP、清除注入并恢复配置，无清理错误。
- 原件：`out/HardwareAcceptance/20260919/guard-startup-gap-r1/`；两轮各有 `startup-audit.json`、`joint-summary.json`、原生记录及波形，实际示波器触发为 CHAN1。独立复核从八份 `read-pages.json` 重组并校验原生 CRC/解码，结论 PASS_WITH_NOTE，见 `startup-hardware-review.txt`。首次新参考抑制不是物理断链，成功间隔是 owner 操作间隔，不是逐帧运输期限；稀疏相差与内部汇总不授予连续精度或同事件关联。新轮通过不证明 032 首轮多板停机的共同事件因果。
- 交付：实现与同指纹 P3 凭证提交 `21ee638c`，文档分离提交。
- 下一 gate：继续 `VDC-SNAPSHOT-001`/`VDC-HOLD-001` 的正式 quality/valid/freshness 和逐事件保持/恢复证据；该修复消除启动误判，不据此升级 formal quality。普通诊断经 Core0，任何需增加特等席的方案先交用户审核。

### VDC-PROGRESS-20260919-032：Core0 异步健康镜像与特等席授权边界

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-HOLD-001`、`VDC-RECOVERY-001`；本切片 DONE，父任务 IN PROGRESS。
- 用户要求：VDC 健康诊断不得占特等席或新增 Core1 发布；增设/扩容特等席必须先用户审核。已写入 `EXE-SEAT-01` 和 TDMA/VDC 待办。草稿的 Core1 镜像调用已撤掉，未进入硬件版本；实际实现只由 Core0 RefMem task 在临界区外、OTA 保护后采样既有 guarded health。
- 代码边界：独立健康扩展使用 VDC 保留区，完整携带源 revision、身份、事件和年龄，Core0 单写并有独立 guard/checksum。旧 wire、Core1 实时函数/两向量轮转、PIO/IRQ/静态预算均未增加工作或配额。STOP 查询 `SYST:REFMEM:VDC:PRIOR?` 返回采样诊断；FRESH/age 可能滞后，不能授权控制或与 legacy model 假定同代。当前 typed 正文已完整分配，普通 class optional 不能重复算作其余量。
- 验证：相关基线 84 项、新镜像/reader/SCPI 边界 84 项、文档门禁测试 38 项通过。Release A/B/BOOT 与链接检查通过；相对上一 gap build 的 text +640 B、data/bss +0 B（构建快照，非事实源）。主控复核独立只读审查 `review.txt`；相同实时函数大小不等于绝对无共享总线影响。
- 固定四板 P3：`p3-r1` 为 PASS_WITH_WARNINGS（25 INFO / 19 WARN / 0 ERROR / 0 FATAL，快照），build `20260919140543`，源码 SHA `fdbd901e9ab339643728ea66aafb230da6d0e5d2a8cca4a25068ffcc83b19db2`，1325 文件。复用已知环序，未操作 NO5。新增告警为 NO1 RefMem deadline miss 增 1，overrun 增 0；RefMem max 17105 cycles、配置 WCET 29000 cycles，旧 DPLL 超时仍存在，不能宣称完整静态表通过。对照见 `p3-phase-comparison.json`。
- 联合实测失败保留：`joint-gap-60s-r1` 四板镜像和模型末态一致，但 NO2/NO3 trace 以 BINDING 冻结，GUARD 分别在 59742/59883 ms 记录提前冻结，后续本地 STOP；NO3/NO4 还记录 SUCCESS_GAP。60 s 窗口 CH2 无上升沿，整轮 FAIL。已有 11 个有效窗口不代替最后失败。末态 MATCH 原因是 SOURCE，trace 的 BINDING 是退休汇总代码，不直接证明身份字段变化；NO3/NO4 首 bin 首次成功等待约 1046.839/1168.597 ms，超过 GUARD 成功间隔界，区别于 22 s 的注入暂停。复测对应约 759.595/890.291 ms。具体源失效与各板自主停机先后仍待对账。
- 同配置有界复测：`joint-gap-60s-r2` PASS，三从各恢复一次；新 offer 暂停至恢复约 304.607 ms，抑制 99 次（快照）。四板 STOP 后源/镜像 18 词一致且校验通过、模型一致；12 个外部窗口、48 个 RAW 文件 hash 核验通过，NO2/NO3/NO4 最大绝对相差约 36.098/27.389/36.947 ns。两轮 RUN 查询均为 0，最终四板 STOP、配置恢复，无清理错误。
- 原件：`out/HardwareAcceptance/20260919/typed-health-core0-mirror-r1/`，含 `resource-audit.json`、`runs-audit.json`、P3、两轮联合原件及各自 `joint-summary.json`；测试日志为 `out/health-mirror-core0-base-tests.log` 和 `out/pytest/refmem-priority-health-core0-review-r2/`。
- 交付：代码与同指纹 P3 凭证提交 `f4992cbc`；文档分离提交。
- 下一 gate：先对照 GUARD 首次成功等待与稳态缺口的判定边界、源失效与自主停机链，再继续 `VDC-SNAPSHOT-001`/`VDC-HOLD-001` 正式质量发布。末态相等不证明逐事件镜像及时性，复测通过不抹去首轮失败，稀疏波形不授予连续物理锁定。

### VDC-PROGRESS-20260919-031：同会话参考停更恢复与清除计划后再 ARM

- TODO task ID：`VDC-RECOVERY-001`、`VDC-SNAPSHOT-001`、`VDC-OBS-007` IN PROGRESS。数字均为本轮快照，非事实源；实现提交 `b9a082ec`。
- `vdc_priority_tx_gap.inc` 增加 STOP-only 单代诊断计划及 `SYST:VDC:PRIOR:TX:GAP` 配置/读回。Core1 按既有 TIMER1 执行半开窗口，截止后只由新源事件恢复；暂停仍检查身份，STOP 取消，默认关闭、不写 Flash。EMPTY 保留旧 DMA 邮箱，验证新参考停更/重复，不是物理断链。
- 证据根 `out/HardwareAcceptance/20260919/typed-reference-gap-r1/`。发送/SCPI 原回归 253 项、新 GAP 回归 174 项、health 27 项、映射/原生记录/summary/GUARD 155 项通过；中途旧 host 夹具缺 `.inc` 搜索路径导致 17 个 setup error，已修复。独立复核无生产阻塞，结论保留逐事件与连续运输证据边界。
- Release A/B/BOOT 通过，build `20260919131536`；相对前轮 BSS 增 168 B、text 增 1400 B。首轮 P3 通过后修改夹具使 `joint-gap-60s-r1` 在访问板卡前因指纹不符拒绝，失败留存。最终 `p3-r2` 为 PASS_WITH_WARNINGS：25 INFO/18 WARN/0 ERROR/FATAL，源码 SHA `39c19df801e0dc913eb7abab61f34232160b290496c0f3d32047c7a689c23f48`，1323 文件；staged 门禁通过。未修改 OTA 实现、未重复线序扫描、未放宽 P3。
- `joint-gap-60s-r2` 通过。计划首次有效观测后 22000 ms 暂停 300 ms；NO1 实际 22001.041280–22302.723544 ms，抑制 140 次 offer，序号 22059→22362，整数 tick 边界复核见 `gap-audit.json`。NO2/3/4 同事件范围的汇总成功间隔 303.972/302.728/301.695 ms，各取消一次旧基线，后续事件持续推进；同 session/generation，末态各记录一次恢复，Core1/Core0 模型一致。
- 该轮十二窗/48 RAW 哈希通过，三从最大绝对相差 61.913/31.924/48.982 ns，满足采样 ±100 ns，但 NO2 不满足所有样本 ±50 ns。新鲜度累计计数无逐次转换时间，十秒桶提供对应序列缺口旁证；未据此资格化暂停期间每帧运输、连续 DCO 保持或物理边沿精度。
- `joint-no-replay-60s-r1` 通过：计划已清零，新 generation 下 GAP 全部计数为零、三从 recoveries 均零；十二窗/48 RAW 哈希通过，最大绝对相差 42.069/30.746/45.275 ns，采样均在 ±50 ns 内。该轮证明清除后再 ARM 不重放；未清除旧计划的跨代拒绝由 host 负测覆盖。两轮 RUN 查询为零，四板 STOP/参数恢复，无 cleanup error；时钟 ppm/ppb 控制值不等于相对输出频率误差。
- 文档 C14 最旧连续条目迁入 `LEGACY_VDC_TASK_PROGRESS_10.md`，正文逐字保留，审计 `out/doc-audit/vdc-gap-rotation-20260919/rotation-audit.json`；未改既有同 ID 历史记录。
- 下一 gate：RefMem 独立诊断投影与正式 quality/valid/freshness 一致发布，逐事件恢复与 DCO 保持实证、物理失联及单板复位。整体锁相/VDC 发布目标未完成；稀疏外部与内部汇总均不授予连续物理锁相或精确事件关联。

### VDC-PROGRESS-20260919-030：typed 来源新鲜度独立发布

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-RECOVERY-001` IN PROGRESS；数字为本轮快照。实现 `c3d1bb6e`：FOLLOW 最终准入后单次记录新事件，Core1 每拍维护独立 TIMER1 年龄，STOP-only `SYST:VDC:PRIOR:FOLL:HEAL?` 读取；不污染正式 quality/DCO。FRESH 不是锁相，恢复计数包含读钟恢复。
- 初轮相关 host 154+133 项通过，最终新鲜度 27 用例通过，独立复核无阻塞；缺失/重复/旧代不续期、过期保持模型、恢复先建基线及退休负测通过。Release A/B/BOOT 通过，BSS 增 148 B、代码增 1112 B、wrapper 栈帧增 32 B；不授予完整 WCET。
- 证据根 `out/HardwareAcceptance/20260919/typed-reference-health-r1/`，`p3-r1` 为 PASS_WITH_WARNINGS：25 INFO/19 WARN/0 ERROR/FATAL。build `20260919123817`，源码 SHA `c38b1639ccc2569e355432a872c60ea7fc9e2c2bb19a52d3602956a032efa4c0`，staged 门禁通过。
- `joint-60s-r1` 保留 FAIL：四板 GUARD/波形通过，但 NO4 模型读回超时、错误队列影响恢复判定；NO3 峰值 51.986 ns。停态复核 `stop-audit-1789822167451896300.json` 确认参数恢复、模型可读；前次核验脚本误用缩写的失败也保留。适配器只对 STOP 模型读取有限重试并留痕。
- 同固件 `joint-60s-r2` 通过：模型均首次读回一致，十二窗/48 RAW 哈希一致、RUN 查询为零；NO2/3/4 最大绝对相差 34.353/34.083/43.852 ns。三从 typed accepted 为 12333/12544/12308，退休原因均 STOP；超时计数包含依次 STOP 尾段，不能单独认定 RUN 断流。四板已 STOP、恢复，无 cleanup error。
- 下一 gate：健康 TDMA 下 typed 参考中断/恢复实板注入、RefMem 独立诊断投影与正式质量；连续物理精度及内外精确事件关联仍未授予。

### VDC-PROGRESS-20260919-029：已验实现按指纹收敛提交

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-OBS-007`、`VDC-FREQ-001` IN PROGRESS。实现提交 `7a1f2434` 包含启动预检、异步留证、age/ready、外参模型发布和超时恢复；staged P3 门禁核对通过，build/源码 SHA 沿用 028，未合入文档。
- 下一切片：typed FOLLOW 当前有 AGE 取消和新样本重新建基线，但通用 quality 是正式证据口径，不能用 MATCH 成功填充。独立复核建议单独发布 typed 事件新鲜度，使用 TIMER1 事件年龄和已有 FOLLOW 期限；不续期重复事件，不授予 formal，随后对账 Core0/RefMem 与参考失联恢复。

### VDC-PROGRESS-20260919-028：外参超时重试与补偿 HOLD 恢复切片

- TODO task ID：`VDC-FREQ-001`、`VDC-SNAPSHOT-001` IN PROGRESS；以下数字为本轮快照。采集 TIMEOUT 异步排空并同 lease/generation 重试，完整新窗口才清除失效；DMA/时钟/结构故障仍终止。补偿无有效窗口进入 HOLD，冻结基线，恢复仅用正常单窗口斜率；不代表正式 Domain HOLDOVER。
- 初版完整超时窗口先被数学判坏，新增回归复现 1 失败/1 通过；改为结构检查→deadline→求值后，最终后端 44 项通过，独立复核无阻塞。此前管理/配置/发布相关 157 项及真实 Domain 集成 13 场景通过。
- 证据根 `out/HardwareAcceptance/20260919/vdc-reference-recovery-r1/`；最终 `p3-r2` 为 PASS_WITH_WARNINGS，23 INFO/22 WARN/0 ERROR/FATAL，Release A/B/BOOT 通过。build `20260919120914`，源码 SHA `ad0d95657fb57dac7259e4014594d0b80f31a71fca4e2686533a8a5a76facf83`；保留修正前 `p3-r1`，不混用凭证或授予严格 WCET。
- `timeout-stop-1789820052283056800.json`：STOP 下故意配置无法按时完成的窗口，三次读回同代 TIMEOUT/invalid 且资源保持；取消释放后恢复正常配置，重新使能得到 3 个有效窗口。配置已恢复、无 Flash 写入。此项证明实板持续 TIMEOUT 状态与显式重启，未测重试次数，不证明物理断接后同会话恢复。
- `joint-60s-r1/joint-summary.json`：内部健康、外部十二窗、四板末态模型一致性通过；48 份 RAW 哈希一致，RUN 查询为零，读取 1.813–2.141 s。相对 NO1 范围：NO2 [0.201,35.910]、NO3 [-10.940,28.000]、NO4 [3.318,38.000] ns；全部已采边沿在 ±50 ns 内。四板 STOP/释放/恢复正常。
- 下一 gate：typed 参考失联、正式 quality 与物理参考同会话恢复。内部 `complete_window_proven` 仍 false，同事件精确对应及未采区间物理精度不作通过声明；实现尚未提交。

### VDC-PROGRESS-20260919-027：外参 clock/DCO 采用与管理发布一致

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-FREQ-001` IN PROGRESS；数字为本轮快照，非产品资格。非零补偿成功后按原 guard 只发布 clock/DCO；DPLL/quality/capture 仍按原 evidence 完成边界推进，PI 和调频参数未改。
- 修复前集成测试 3 失败/5 通过，直接复现模型未发布；修复后相关 75 项通过，最终九场景集成回归通过。真实 Domain 覆盖 prepare→reference→servo、servo→reference→finalize、Core0/RefMem 去重及拒绝/取消/零步进；独立只读复核无阻塞项。Release A/B/BOOT 链接通过，新增 XIP 100 B，静态 RAM 边界及函数栈帧未变；不据此授予 WCET。
- 证据根 `out/HardwareAcceptance/20260919/vdc-reference-publication-r1/`；`p3-r1` 为 PASS_WITH_WARNINGS，25 INFO/18 WARN/0 ERROR/FATAL。build `20260919114207`，源码 SHA `6689805c9f68a1133a26515e8d158b00714a16a028767b8f5168cc6b91ecba0a`。沿用已测线序，PowerShell/Python 原生入口运行，未重扫 P0。
- `joint-60s-r1` 内部健康与外部窗口通过；十二个四路窗口、48 份 RAW 哈希一致，读取 1.812–2.125 s，无漏采/超时，RUN 板端查询为零。相对 NO1 范围：NO2 [-12.000,33.851]、NO3 [-1.827,28.197]、NO4 [4.000,38.061] ns。局部 planned 输出路径预算通过，不替代完整静态表或未采区间精度。
- STOP 后四板 Core1 保留模型与 Core0 DCO 的序号、频率、锚点及 CRC 全部一致；NO1 补偿 accepted/applied 均 60，基线 4412 ppb、DCO seq 104。ppb 为控制量，非绝对频率精度。Core0 历史 invalid 计数非零，本轮只证明末态有效且一致；RefMem 的逐字段一致由 host 集成验证。四板已 STOP、释放并恢复，无 cleanup error；实现未提交。
- 下一 gate：缺参考时保持/降级、重新输入后的恢复及旧会话退休；正式 quality、内外精确事件关联和连续物理输出资格保持未完成。

### VDC-PROGRESS-20260919-026：空转质量老化与 ready 发布切片验收

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-OBS-007` IN PROGRESS；以下数字为本轮快照，非产品资格，实现尚未提交。
- Core1 wrapper 在 step 提前返回前分别维护工作 Domain 和已发布视图的 age/health/holdover age；不推进 service、证据或模型序号，不进入带 capture 副作用的完整 publisher。ready 变化同步派生 quality 和已有 runtime 发布。保留 prepare/servo/finalize/service 边界，无参考及读钟失败不伪造样本。
- 相关 host 回归 58+63 项、Domain C tests 通过；旧 manager 负对照 9 失败/9 通过，复现 age/ready 缺陷。Release A/B/BOOT 及链接契约通过。证据根 `out/HardwareAcceptance/20260919/vdc-idle-publication-r1/`；`p3-r1` 为 PASS_WITH_WARNINGS，23 INFO/24 WARN/0 ERROR/FATAL。build `20260919112440`，源码 SHA `46d18204f99544c4e00cd4090a84c8cb401a65624d74c8b48f0378b281254585`；不授予严格 WCET 资格。
- `joint-60s-r1` 内部健康及外部窗口通过，十二个四路窗口、48 份 RAW 哈希一致，RUN 板端查询为零；相对 NO1 范围：NO2 [-8.742,40.365]、NO3 [-14.223,28.061]、NO4 [-3.340,36.057] ns。固定 delay 和外参参数同 025；读取 2.000–2.266 s，后台写盘最大 7.265 s，未漏采/超时，最终 flush 完成。全程含启动，不据此宣称稳态退化或未采区间精度。
- `idle-observation-1789817581103122500.json`：四板 STOP/配置 ACK、UID/build 和错误队列核验通过；NO1 age 从 116273504 增至 117298009 µs，accepted 保持 39，health 为 DEGRADED。NO2–NO4 为 CHECKING/无正式参考样本，age=0 不作为老化实测证明；本地 typed DCO 跟踪与正式质量样本分开。全部采集正常 STOP/释放/恢复。
- 下一 gate：独立修复参考补偿采用后的 runtime DCO 可见性，再推进失联/恢复、正式质量及 RefMem 一致发布；内部 `complete_window_proven` 和内外同事件精确关联仍未闭合。

### VDC-PROGRESS-20260919-025：启动预检、异步保存与三会话联合验证

- TODO task ID：`VDC-OBS-007`、`VDC-OUTPUT-001`、`VDC-PRECISION-001`、`VDC-SNAPSHOT-001` IN PROGRESS。以下数字为实测快照，非产品资格；工具/测试改动仍在工作区，本次文档提交不包含这些实现。
- 证据根 `out/HardwareAcceptance/20260919/probe-scope-correlation-r1/`。启动预检核对 STOP/config ACK、IDN/编号/UID、build、凭证矩阵及逐 link 内容、process-image 模式和错误队列。先前 NO1 重启丢失训练 RAM，重装矩阵又恢复 raw 模式，均已定位并恢复；新预检在 ARM 前拒绝此类配置。不把此项视为单板独立复位恢复已完成：此前大相位差导致 RUN 输出 STARVED，以及一次 ARM 前 trace status 超时，仍保留原始失败。
- 示波器 RAW/报告采用主机后台有界写队列，VISA 单线程；内存分析、不可变副本、最终 flush 和写失败保持分别验证。73 项相关测试及独立只读复审通过；`p3-async-evidence-r2/` 为 PASS_WITH_WARNINGS，25 INFO/19 WARN/0 ERROR/FATAL。源码 SHA `05f2725f56e905f722019337976b9331ff587a4b2aa17bbc40d0ffa66d0e45b2`，build `20260919101812`；固定 quick P3 不授予严格 TDMA 预算或 DPLL 精度。
- 同指纹、相同输出 delay `[0,-8,-68,-116] ns`、外参补偿 `100/4/10000`，零模型初值分别执行 `run-600s-async-r1`、`restart-120s-r1`、`restart-120s-r2`。三轮独立会话的内部健康与外部窗口均通过，共 168 个新鲜四路窗口、672 份 RAW 哈希复核一致；RUN 板端查询为零，STOP/释放/参数恢复正常。这是 STOP/ARM 重复启动，不是 MCU 复位或断电重启验证。
- 全部已采边沿相对 NO1 均在 ±50 ns 内；各轮 60 秒后样本合并范围：NO2 [-13.959,9.483]、NO3 [-15.940,16.159]、NO4 [-9.917,12.125] ns。比较保留原始中心，见 `restart-comparison.json/.svg`；十分钟单次采集 1.813–2.172 s，第 475 秒后台写盘耗时 1.031 s 未导致漏采或超时。
- 内部 GUARD/目标封存通过不等于原生解码器的 `complete_window_proven`：后者仍 false；内部残差、命令 ppb、GPIO 相差及绝对频率准确度分开解释。示波器用 CHAN1 触发，OUT4→EXT 接线仍存在但当前 RUN 仅输出 OUT1。稀疏窗口不证明空档或同 ordinal/同 bin 精确对应，不能由本轮直接完成内部自校准。
- 下一 gate：`VDC-SNAPSHOT-001` 的无新参考 age 与 ready 一致发布。只读复核确认 setter 还需更新派生 quality；age 维护不能伪造 service/evidence 计数，也不能通过完整 publisher 提前公开尚未 finalize 的四拍证据。参考补偿后 runtime DCO 可见性另列核查，后续仍需坏帧/失联恢复及正式 VDC 发布验收。

### VDC-PROGRESS-20260919-024：外参补偿参数外置及两分钟斜率对比

- TODO task ID：`VDC-TUNE-002`、`VDC-FREQ-001`、`VDC-DRIFT-001` IN PROGRESS。以下测量与版本数字为本轮快照，非产品事实源。
- `SYST:VDC:REF:DISC:CONF/CONF?/ACT?/DEFA/REC/STOR` 已接线：STOP 且补偿关闭时配置斜率、滤波分母、测量准入限幅；ARM 锁存代际/CRC，Core1 确定性采用。journal v7 保存参数，v1–v6 仅 RAM 补默认，上电不自动使能。默认仍为 `PRODUCT_CONFIG_VDC_REFERENCE_DISCIPLINE_DEFAULT_*`，未改 OTA。
- Release A/B 与 Flash 链接契约通过，独立综合审查相关回归 155 项、补充输出配置及持久化回归 66 项通过。四板 `out/HardwareAcceptance/20260919/p3-reference-profile-r2/` 为 PASS_WITH_WARNINGS（25 INFO、18 WARN、0 ERROR/FATAL）；源码 SHA `d31246fd8b07f02a9e68c8680116e751420daf37ac4de01a07ac013ccd55e3f2`。TDMA diagnostic 为 true，原始 passed/closed_loop/realtime 为 false，严格调度告警保留。
- 专项根目录 `out/HardwareAcceptance/20260919/reference-discipline/`。`profile-120s-100-r2` 与 `profile-120s-50-r1` 均通过；同固件、PI、训练输入、delay 和示波器设置，四板零模型初值，每组每五秒采样共 24 窗，RUN 板端查询为零，正常 STOP/参数恢复。`profile-comparison-r1/comparison.json` 核验 comparable=true，配套 SVG/PNG 保留原始边沿，不平移曲线。
- 第二分钟 NO2/NO3/NO4 峰峰值：100 ppb/s 为 12.06/13.51/19.86 ns，50 ppb/s 为 30.01/19.10/35.94 ns。50 降低全段峰值，但末态滤波目标 4957 ppb、已采用基线 4850 ppb，仍在限速追赶；原生分段 DCO 持续推进，不能判为最终稳态或优于 100。保持默认 100，尚未证明波动来自参考源或 NO1 PI 振荡。
- `profile-120s-100-r1` 第 100 秒 RAW 导出耗时 5.407 秒，保留 FAIL。后续 bench adapter `reduced_scope.py` 保持 1M 点/2 ns 采集，只导出同次 STOP 的连续前 650000 点，检查四路共同时间轴及每路两个真实上升沿；两轮导出约两秒，无超时。不改正式工具或放宽门限，稀疏窗口不证明空档精度。
- Flash 实板 `profile-flash-r1.json` 已证明非默认保存/召回/重启保留，但恢复原 RAM 参数遇 STOP 准入拒绝，保留失败。`profile-flash-r2.json` 记录错误读回及有界 RAM 重试，Flash 写入不重试；非默认保存/重启、上电关闭、原 Flash 100/4/10000 恢复并再次重启读回全部通过，cleanup_errors 为空。四板 STOP，NO1 参考及补偿关闭；NO1 重启后训练 RAM 需在下次试验按已测矩阵重装。
- 下一 gate：核对 HDO4404 参考源规格及测量误差，继续参考失联保持/恢复与长窗验收。外参补偿后的十分钟仍未通过，不以本轮短窗替代。

### VDC-PROGRESS-20260919-023：外参有无对照，区分启动暂态与稳态波动

- TODO task ID：`VDC-FREQ-001`、`VDC-DRIFT-001` IN PROGRESS。以下数字均为实验快照，非产品事实源。
- 证据根目录：`out/HardwareAcceptance/20260919/reference-discipline/`。早期 `ab-60s-a1/b1` 均通过，但 A 仅关闭补偿更新、保留此前基线，不能解释为相同零初值的有无外参对照。
- `ab-120s-a1/b1` 两组各两分钟通过，均在 STOP 下使用现有 provisional 激活重建四板模型并确认积分/频率为零，复用已测线序。相同 PI、输出 delay、示波器配置，每五秒一次同触发四路 RAW，RUN 板端查询为零；两组均正常 STOP 和恢复。
- `ab-120s-comparison-r2/comparison.json` 与 `phase-ab.svg`：第二分钟 NO2/NO3/NO4 峰峰值，无外参为 12.55/16.09/14.05 ns，有外参为 10.26/9.80/18.00 ns。有外参全段最大绝对偏差约 42.79 ns，第二分钟全部已采边沿在约 ±12 ns；支持调频捕获暂态假设，不足以证明振荡原因或未采空档精度。重建导致路径 CRC 更新，完整训练输入与实际 delay 已分别比对。
- 外参补偿后的十分钟尚未通过：`joint-600s-r1` 在 240 秒检查点因导出超时停止，`r4` 在 360 秒因第 315/320 秒导出超时停止；波形门限未超。`r2` 为 NO3 START 应答超时，`r3` 因未释放 trace 拒绝启动；原始 trace 已导出后释放，见 `no3-trace-recovery/`，失败原件保留。
- 下一 gate：通过外置参数接口对比调频斜率，保持 PI 不变；之后再验证参考失联保持/恢复。外参 IIR 与 MASTER PI 残差分离，不能仅凭 NO4 偏差较大认定 NO1 PI 抵消外参。

### VDC-PROGRESS-20260919-022：外部参考慢速补偿实际采用，四板短窗通过

- TODO task ID：`VDC-FREQ-001` IN PROGRESS。已推送前一切片代码 `0f42a6a0` 和文档 `30e073e0`；本条为后续补偿切片，未推送。
- `DISCipline` 独立、易失、STOP 启用；Core1 消费硬件窗口，绑定主板 origin/session/配置/时钟身份，滤波限速后提交绝对基线。Domain 合成基线与 PI 残差，保持同刻时间连续；取消冻结已应用基线。SCPI 顺序及字段见 TODO。
- host 初轮 196 项通过；修正主板身份后相关 113 项通过；文档回归 38 项通过。Release A/B 与链接契约通过；四板 `p3-reference-discipline-r2` 为 PASS_WITH_WARNINGS，25 INFO/18 WARN/0 ERROR/FATAL（本轮快照）。TDMA diagnostic 通过，原始 passed/closed_loop/realtime 均 false，未宣称严格调度通过。
- 失败原件：`out/HardwareAcceptance/20260919/reference-discipline/joint-60s-r1/`。错误使用 follower RX observer 准入导致 NO1 零采用；修正为现有 priority TX 的自主 origin 记录来源，重新构建/P3 后复测，未放宽相位门限。
- 通过原件：同目录 `joint-60s-r2/capture/input-probe.json`、四板原生记录和 `main-review.json`。参考 67 窗，采用 60 次，末态测量 +4496、滤波/基线 +4474 ppb；NO1 原生分段 DCO 范围由 +1391 推进至 +5847 ppb。四板 GUARD、十二个新鲜示波器窗口全通过，RUN 查询为零。
- 实测快照：NO2/NO3/NO4 相对 NO1 分别为 [7.30,43.97]/[-4.20,45.99]/[1.60,65.91] ns。采样空档、DMA 仲裁误差和外部仪器基准精度未定界，不宣称连续物理精度或绝对 ppb 资格。
- 四板 STOP，参考/补偿关闭，配置恢复且无 cleanup error，未改 Flash。下一 gate：失参考保持与恢复、同启动补偿前后频率对照及较长联合窗口；滤波/步进仍为编译策略。

### VDC-PROGRESS-20260919-021：外部参考 MONITOR 可配置，四板共存通过

- TODO task ID：`VDC-REFERENCE-002` DONE；`VDC-FREQ-001` 的基准补偿未实施。
- 实现：`SYST:VDC:REF:CONF/ENAB/STAT?` 和 `DEFA/REC/STOR`；STOP 配置端口、标称 Hz、边沿、窗口和超时。产品配置 v6 只保存参数，上电不自启；兼容 v1–v5 CRC/字段。SYNC_IO 独占参考 SM，DMA9/10 读取 TIMER1；Core1 必经 TDMA 相位推进一次有界状态，Core0 只在退休 ACK 后释放。RS485、RUN 输出及 TDMA 原 owner 保留。
- 软件验证（本轮快照）：相关解析/配置/Flash/timing/静态调度 130 项、SYNC 后端及既有资源回归 108 项、路由/观测回归 23 项、文档回归 38 项通过。Release A/B 构建及链接契约通过；P3 `p3-reference-monitor-r4` 为 PASS_WITH_WARNINGS（25 INFO / 18 WARN / 0 ERROR / 0 FATAL），未要求 NO5。TDMA `diagnostic_passed=true`，`passed/realtime_gate_passed/closed_loop_passed=false`；严格调度告警保留，与此前 `p3-vdc-publication-r2` / `p3-internal-seal-r1` 判级一致，不能宣称严格 TDMA 门禁通过。
- 专项证据：`out/HardwareAcceptance/20260919/reference-monitor/standalone-r2/report.json`。IN4=GPIO20，上升沿序号 3→5、下降沿序号 2；停用释放、IN3 无信号超时、非默认窗口 Flash 保存/召回通过。读数约 +3680～+3692 ppb 为“输入相对本地标称时钟”，不代表独立绝对精度。
- 同轮共存：`reference-monitor/joint-60s-r1/capture/input-probe.json`，参考完成 67 个窗口，末态 +4380 ppb；四板 GUARD 全通过，RUN 查询为零，示波器每五秒一窗共十二窗通过。相对 NO1 边沿范围：NO2 −10.07～+25.88 ns、NO3 −11.58～+4.10 ns、NO4 −7.94～+59.59 ns（实测快照）；稀疏窗口不覆盖空档，不宣称完全连续物理精度。
- 失败与修复：初版 DMA0/1 与 RS485 冲突，改为9/10；独立审查指出 sticky DMA error，取得通道后停态 W1C 清理并回归。`standalone-r1` 中 PREPARED/取消不推进，是服务误挂可隔离 SYNC 相位，改至必经相位后通过。失败原件保留，不能用早期 P3 替代 r4。
- 重启验证：`reference-monitor/boot-check-r3.json` 证明非默认参数恢复且 enabled/resource/state 均 idle。前两次测试在重枚举后立即恢复 RAM 配置遇执行拒绝，原件 `boot-check.json`、`boot-check-r2.json` 保留；r3 留存拒绝并有界重试 RAM 配置后恢复默认 Flash。USB 可查询不等于 STOP 写入准入已稳定，Flash 写入未重试。
- 收尾：四板 STOP；NO1 参考关闭，参数恢复 IN4/10 MHz/上升沿/1000 ms/2500 ms。DMA 仲裁误差仍标记未定界，未改变任何 DCO；下一切片为参考频偏稳定性/误差预算，再决定慢速基准补偿。

### VDC-PROGRESS-20260919-020：显式 delay 与批量示波器留证，联合长窗通过

- TODO task ID：`VDC-OBS-007`、`VDC-DRIFT-001`、`VDC-OUTPUT-001` IN PROGRESS。
  工具切片 `cd8fd17a` 增加 `--output-delays` 四个 int32 参数，经既有 ResumeTrial
  STOP/快照/应用/读回/恢复路径处理；默认保留旧基线，本次显式选择
  [0,-8,-68,-116] ns。示波器采样内的逐查询 JSON 重写合并为正常/异常退出时
  保存；RAW 块仍即时落盘。保留新 WAIT→STOP、四路同窗 RAW、哈希、分钟门禁，
  不放宽采集超时或相位判据。进程强杀可能丢失当前窗尚未保存的 JSON，不能用
  不完整原件签发通过。本条全部测量数字为当日快照，非事实源。
- 联合与采集 helper 测试 118 项通过；独立只读 reviewer
  `/root/joint_capture_review` 另跑 48 项通过、无阻断项，未连接硬件。
  复核及测试原件见 `out/HardwareAcceptance/20260919/p3-joint-batch-r1/`。
  同源码构建及固定四板 P3 PASS_WITH_WARNINGS，25 INFO / 18 WARN / 0 ERROR/FATAL；
  source SHA `a41895ce7ada7757b2fe308c9f508860d771cfbefe6b36f5aca7a8a2bd000fe8`，
  package SHA `15ba684592fee95a3cf01ea96e889a9b9a2c04b383cfbb7d6963a7357a7ec969`。
  固件未改；本次不以锁相质量扩充基础 P3。
- 证据根 `out/HardwareAcceptance/20260919/dpll-delay-center-r2/`。
  `plus20-batched-60s/` 十二窗通过；NO2/NO3/NO4 范围分别
  [-9.924,16.531]/[-16.014,5.986]/[-4.491,60.118] ns。NO4 的最大值在首个
  第五秒窗口，之后收敛，不能将该轮写成全窗 ±50 ns。
  `plus20-batched-600s/` 连续运行十分钟，120 个新鲜外部窗口、十个分钟门禁及
  四板内部 GUARD 全部通过；三从范围分别 [-28.127,-3.705]/[-29.588,-4.961]/
  [-21.783,2.188] ns，全部在 ±50 ns 内。该轮跨启动中心仍有变化，不追加补偿。
- `comparison.json` 保留旧轮 FAIL 与新轮 PASS：旧十分钟最长采集 7.328 s，
  第 550/555 秒超时；新十分钟最长 3.531 s，无漏采/超时，采集耗时中位 3.359 s。
  这证明本轮调度通过，不宣称主机偶发停顿已永久消除。主控从原始 SRAM/RAW
  重新解码并复算，见两份 `*-main-review.json`；长窗图为
  `plus20-batched-600s-comparison.svg`。两轮运行板端查询均为零，原生封存、
  计时资格、STOP/RELEASE、参数恢复全部通过，无 cleanup error，未写 Flash。
- 下一 gate：沿用显式新 delay 做跨启动与失联恢复；推进 VDC 无新 evidence 时
  的质量老化和 idle ready 发布缺口。稀疏 GPIO 窗口通过不等于未采样区间精度、
  精确事件关联、全程物理锁定或 VDC 一致发布完成。内置 RRDELay 尚不替代 RAW。

### VDC-PROGRESS-20260919-019：NO2/NO3 输出 delay 各增加 20 ns

- TODO task ID：`VDC-OUTPUT-001`、`VDC-PRECISION-001` IN PROGRESS。用户观察到
  CH2/CH3 稳定提前，授权在本轮 delay 基础上各加 20 ns。试验配置由
  [0,-28,-88,-116] 改为 [0,-8,-68,-116] ns，沿用 24000/32000/16000 µs 时间轴，
  参数为当日快照；只经既有 STOP-only SCPI 应用/读回，不改固件及 Flash。
  证据根 `out/HardwareAcceptance/20260919/dpll-delay-center-r1/`，固定方案见
  `plan.json`，入口 `capture.py` 默认使用新候选，`--baseline` 显式选择旧配置。
- `plus20-60s/` 与 `plus20-repeat-60s/` 两次独立启动均联合通过；每次十二个
  四路 RAW 窗口、四板 GUARD、计时完整性、CRC、目标封存、STOP/RELEASE 与
  原配置恢复完整，板端运行查询零。主控从原始字节重放，见两份 `*-main-review.json`。
  第一轮 NO2/NO3 中位 -7.789/-6.955 ns，范围 [-13.887,0.016]/[-14.014,5.268] ns；
  第二轮中位 +3.010/-0.474 ns，范围 [-2.064,7.946]/[-6.092,7.551] ns。
  NO4 参数保持，其两轮范围分别 [-12.231,11.203]/[-1.972,11.959] ns。
  对照旧十分钟 NO2/NO3 中位 -21.977/-25.303 ns，支持中心改善，但两次启动
  本身存在约十纳秒变化，不能宣称精确平移或冷启动重复性完成。
- 同一末态波形中，内置 RRDELay 在 CH2/CH4 返回 `9.900E+37` 无效值，CH3 为
  -4 ns，RAW 仍可正常提取边沿。`builtin-repeat.json`、`builtin-recovery.json`
  保留读数，最终示波器 STOP、error queue 零；中间重设测量后的 query timeout
  保留为工具失败，不等于 DPLL 故障。无效读数不能作为零误差或外部通过。
- 下一 gate：后续试验优先采用新候选 delay，继续改进内置延迟巡检与 RAW 回查的
  有界采样编排，再跑严格长窗。原通用联合入口仍使用旧基线；本次候选请用本证据
  入口，待下一工具切片显式参数化，避免误用。两次短窗不替代全程物理锁定、
  失联恢复或 VDC 一致发布；板卡已 STOP 并恢复本次试验前配置，未固化候选。

### VDC-PROGRESS-20260919-018：同轮十分钟外部窗口与内部健康复核

- TODO task ID：`VDC-OBS-007`、`VDC-DRIFT-001` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/internal-joint-r1/`，下列数字为当日快照，非事实源。
  沿用已提交联合工具及最终 P3 指纹，未改固件/工具。`scope-on-chan1-600s/`
  连续运行，四板内部十个分钟检查点全部通过，原生目标封存及规划计时资格通过；
  最终输出 CANCELLED，板端运行查询零，STOP/RELEASE 与配置恢复完整。
- 外部 120 个四路冻结窗口全部读回有效，NO2/NO3/NO4 相对 NO1 的范围分别约
  [-32.000,-13.942]/[-36.014,-14.159]/[-7.478,13.402] ns，均在 ±50 ns 内。
  但第 550 秒采集耗时 7.328 秒，影响第 555 秒节拍；第 600 秒检查按既定规则
  因两次 acquisition_overrun 失败，整轮仍为 FAIL，不能称严格十分钟验收通过。
  细查最长约 3.781 秒间隔位于触发阈值查询结束至 SING 发送之前，落在主机
  报告保存/调度区间；该窗触发准入至完成观测约 328 ms，不能归因为 GPIO 抖动。
  `scope-on-chan1-600s-main-review.json` 与 `scope-on-chan1-600s-comparison.svg`
  保存主控逐份 SRAM/RAW 重放和对照；采集失败不覆盖健康及物理窗口事实。
- 运行期间重新验证发布缺口，证据
  `../vdc-idle-maintenance-r2/current-head-r1/results.json`：真实 Domain、Core1
  dispatcher、runtime/committed publisher 和 RefMem 路径仍复现六个异常场景，
  四个对照正常。无参考时预期 age 200000 µs，Domain/runtime/RefMem 仍为零；
  ready=false 可令 committed DCO 为 OFF，而 runtime/Core0 仍保留旧 LOCKED。
  旧复现脚本缺少新 GUARD 的禁用态 stub 导致的编译失败保留于 r1；r2 仅补该
  非激活诊断 hook，未替换受测 Domain/发布函数，不把 stub 当实板时序证明。
- 下一 gate：先按用户意见验证 NO2/NO3 输出 delay 各加 20 ns；随后以新鲜触发的
  示波器内置延迟巡检、定期 RAW 复核并减少逐查询写盘，重做严格长窗。
  VDC 发布后继只修复有界质量老化/ready 发布，不在该切片发明 HOLDOVER、
  改写 clock/DCO 含义或在单次丢样本时取消输出。持续物理锁定及正式发布仍未完成。
- 按 C14 将最旧四条逐字迁至 `LEGACY_VDC_TASK_PROGRESS_08.md`，索引闭包与原文
  哈希验证通过，见 `progress-rotation-08.json`；未删除历史证据。

### VDC-PROGRESS-20260919-017：内部探针可选示波器同轮复核

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/internal-joint-r1/`，数字为当日快照，非产品事实源。
  新入口 `tools/vdc_priority_trace/vdc_priority_joint_capture.py` 显式选择已验证
  bench adapter，`--scope off/on` 切换纯内部或同轮外部观测。保留全部 GUARD、
  CRC/身份、STOP/RELEASE/恢复；板端运行查询零。外部每五秒一份新冻结 RAW、分钟
  检查失败后收尾，不复用旧波形，内部与外部判据独立、联合通过要求两者满足。
  工具/测试切片已提交 `dcdc7e72`，实际 pre-commit 匹配最终 P3 指纹通过。
- 独立作者 40 项模拟测试通过；加入可选触发源后主控联合相邻回归共 111 项通过。
  `test-independent-review.json` 保留初版测试范围/哈希，最终测试见 `trigger-tests.txt`。
  示波器 STOP 负验证观察到新 WAIT，无输出时拒绝旧记录。初版 EXT 实测
  `scope-on-60s/` 12 次全部触发超时，内部四板通过、联合 FAIL，恢复完整。源码
  `sync_io_run_output.c:RUN_PIN` 只驱动 OUT1，OUT4 接线存在但没有 RUN 脉冲；故
  `--scope-trigger CHAN1` 为当前默认，EXT 仍可显式选择，不新增固件 GPIO 所有权。
- 最终固定 `p3-internal-joint-r2/` PASS_WITH_WARNINGS：23 INFO、23 WARN、
  0 ERROR/FATAL，源码 `ef0be959889f992c86d42683485a89841ea32314c75060bf57bc48574934b1a4`；
  包 SHA 仍为 `15ba684592fee95a3cf01ea96e889a9b9a2c04b383cfbb7d6963a7357a7ec969`。
- `scope-on-chan1-60s/` 联合通过：内部四板目标封存、外部 12 个四路窗口有效，
  两沿/窗，实际 NO2/NO3/NO4 相对 NO1 分别约 [-35.811,-13.924]、[-34.132,-15.885]、
  [-8.027,47.959] ns；四板规划计时无缺口/超限，最大约 200.208..205.576 µs。
  主控从 SRAM/示波器 RAW 重放，CRC、触发源、新 WAIT、停止和恢复匹配，见
  `scope-on-chan1-60s-main-review.json`；图为 `scope-on-chan1-60s-comparison.svg`。
- `scope-off-60s/` 同版本关闭分支通过：外部 SKIPPED、窗口零，内部四板目标封存、
  零运行查询、STOP/RELEASE/恢复完整，见 `scope-off-60s-main-review.json`。
- 用户提示后验证 HDO 内置 `:MEAS:ITEM? RRDELay,CHAN1,CHAN2/3/4`，并添加三组显示。
  同一末次冻结波形读得 -28/-26/-8 ns，与 RAW 边沿相差约 2 ns 内；单项查询
  约 2.5..3.2 ms，见 `scope-built-in-delays.json`。旧 `RDEL` 名称被 -222 拒绝的
  原件保留。当前联合脚本仍下载 RAW，后续可用内置读数快速巡检并定期 RAW 复核，
  须保持新触发完成和无效读数判定，不能把冻结旧读数当持续测量。
- 下一 gate：两种配置已可用，继续重复长窗与 VDC 质量发布。
  同轮并不证明精确同事件/同汇总段配对，稀疏外部窗口不证明未采样区间；内部残差
  与物理边沿是不同量，不以共同通过宣称绝对零扰动或全程锁定。EXT 失败原件保留。

### VDC-PROGRESS-20260919-016：目标自动封存与无查询联合探针复测

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/internal-seal-r1/`；下列数字为当日快照，非产品
  事实源。显式 GUARD 目标完成时先封存末段，保持 RUNNING 直到最后时钟、计数器和
  覆盖检查结束；异常仍否决 PASS。成功使用独立 TARGET_COMPLETE 原因冻结记录，
  不停止输出或环路；主机最终统一 STOP。普通 SUMMary、原生布局与记录池保持。
  GUARD 独立版本升级，解码工具只承认覆盖，不单独授予 GUARD 或物理锁相通过。
  代码已提交 `b539118b`，实际 pre-commit 匹配本次 P3 指纹通过。
- 主控 235 项回归、Release A/B/boot 链接通过；独立测试作者最终 119 项专项通过，
  两个隔离变异负控均检出，见 `independent-tests.json`。适配器 227 个策略检查通过。
  独立 `source-review.json` 复核源码、目标汇编与适配器，同意 `VDC-PRIORITY-01`
  v25 保持 pending；明确封存后不再记录参考，输出尾段另审，不宣称零扰动。
  `ram-check.json` 确认 A/B 静态 RAM 间隙仍为 20052 B，未增加记录池。
- 固定 `p3-internal-seal-r1/` PASS_WITH_WARNINGS：25 INFO、18 WARN、0 ERROR/FATAL，
  沿用确认拓扑，源码 `e91aafe851022f9a61c923dc37e0e7e5340bba282212ee12317962c3c0d24380`，
  包 SHA `15ba684592fee95a3cf01ea96e889a9b9a2c04b383cfbb7d6963a7357a7ec969`。
- `positive-60s/` 整轮健康与独立规划计时资格通过，主控从 RAM 原始字节重放，
  见 `positive-60s-main-review.json`。四板覆盖 60.001..60.004 秒、全部目标完成
  封存；计时缺口和预算超限均零，规划路径最大约 196.828/198.380/202.496/203.136 µs。
  输出最终 CANCELLED，CRC、STOP/RELEASE、恢复及零运行查询成立。三从稳定汇总段
  内部残差分别为 [-116,102]/[-123,106]/[-122,104] ns，属于估计区间，非 GPIO 精度。
  独立 `p3-short-review.json` 核对源码与全部凭证引用、原生页 CRC、保存的解码、
  判据、恢复和输出退休，结论一致；离线图为 `positive-60s-internal-follow.svg`。
- `positive-600s/` 连续十分钟整轮健康与独立规划计时资格通过；四板覆盖
  600.001..600.004 秒、全部十个分钟通过位完整，原生各 60 段，运行查询零。
  四板计时缺口/超限均零，规划最大约 228.364/221.724/217.800/207.848 µs，
  输出最终 CANCELLED，无本轮 STARVED，CRC、STOP/RELEASE 和配置恢复完整。
  三从稳定内部残差为 [-231,110]/[-194,112]/[-219,113] ns，长窗区间扩大，不能
  用本次健康通过宣称物理 ±100 ns。主控重放与图分别为
  `positive-600s-main-review.json`、`positive-600s-internal-follow.svg`。
- 下一 gate：按用户补充增加可配置外部示波器联合复核；关闭时保持内部静默自检，
  开启时在同一次运行中稀疏外部采样，分别报告内部健康、物理观测及窗口对应关系。
  单次长窗不宣称偶发故障消失；旧 OUTPUT_READ、STARVED、BINDING、非法运行态
  trace STOP 和超限失败原件均保留。后续仍需重复长稳、失联恢复与 VDC 质量发布。

### VDC-PROGRESS-20260919-015：活动输出释放预检与探针收尾顺序

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/output-observer-gate-r1/`；数字为当日快照，
  非产品事实源。Core0 周期 release 原先在检查后端状态前占用客户端门控，
  活动输出也产生竞争。现先只读预检已发布的 RETIRED/代际，符合才单次弱 CAS；
  锁内再次核对原请求及新后端快照，失败保留所有权请求，后续 service 独立重试。
  未改变 GUARD 读取失败、模型失效、STOP/释放或补给预算判据。
  代码切片已提交 `d66d0f81`，实际 pre-commit 匹配本次 P3 源码凭证通过。
- 主控 243 项测试通过，Release 双 slot 与 boot 链接检查通过。独立作者新增
  18 个真实生产入口场景，直接观察活动状态零次 CAS，四个隔离变异负控全部检出，
  见 `../output-probe-gate-r1/independent-tests.json`。独审 `source-review.json`
  确认目标单次弱 CAS 与锁内重验，未发现所有权阻断；不能由此宣称零干扰或
  所有 OUTPUT_READ 原因已排除。
- 固定 `p3-output-observer-gate-r1/` PASS_WITH_WARNINGS，25 INFO、19 WARN、
  0 ERROR/FATAL；沿用已确认拓扑。源码
  `b3067dff8f64011a5304dfaf41d915ebace57b0f1954a5a602ef5efd3ae52828`，
  包 SHA `87678d449b6e2859b2e233efd313f0f2c77c78473cc43ad164841e5c7634c0cd`。
- 首次 `positive-60s/` 四板分钟 GUARD 都通过，计时样本差均为零，已记录最大
  墙钟约 198.380/201.428/268.636/205.752 µs，预算超限零；但 NO2–NO4 最终以
  BINDING 冻结，整轮 FAIL。原编排在运行结束后先逐板停环路、再冻结 trace，
  因此需隔离收尾动作对终态的影响；不能直接豁免该原生终态或改写失败。
- `positive-60s-r2/` 曾尝试窗口结束先发 trace STOP 再停环路，八个纯内存顺序
  检查通过，但未覆盖固件准入边界。实板 NO1 返回 timeout，随后 error queue 为
  execution error；源码确认 trace STOP 与 ARM/RELEASE 同样走 STOP-only metadata
  gate。因此该编排不可用，不再重试。整轮 FAIL、父 restore 告警及原始脚本保留。
  最终四板 initial/final 配置完全一致、环路停稳，NO1 最终 error queue 已为空；
  这些恢复证据不能改写原告警为通过。
- 下一 gate：独立切片在 Core1 目标检查点通过时自动封存本次记录，用明确完成
  原因区别主机 STOP 与故障；普通 SUMMary 保持原行为，输出继续到统一 STOP。
  不开放运行态 trace 控制，不将 BINDING 改写为正常。再验证短窗和十分钟，要求
  完整计时样本、四板检查点及 CRC/STOP/RELEASE/恢复一致，随后重复长稳与物理精度。

### VDC-PROGRESS-20260919-014：原 TDMA 相位内独立规划补给与内部长检

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/output-replan-r1/`；本节数字均为当日快照，
  非产品事实源。针对上一切片复现的缓存失效后无规划机会，在完整 TDMA 不能
  准入时，优先以独立候选预算执行原输出规划主体，余量更小时才执行缓存交接。
  两入口互斥、只占原相位；保留模型失效、已准入前缀、STOP/身份取消与 DMA 退休。
- `VDC_RUN_OUTPUT_SCHEMA` 追加六个 `planned_*` 字段，分别观测调用、提交、
  推进规划的服务和 caller 墙钟。弱 CAS 只试一次，失败不阻塞。完整 TDMA 的
  skip/start-miss 与 phase_run_count 语义保持；实际输出服务按自己的预算留账。
  候选预算没有变成 WCET 证明，任意迟到下的补给机会也未保证。
  代码切片提交 `f14c58ab`，实际 pre-commit 硬件凭证核对通过；契约 v24
  独审接受 pending 范围，见 `c11-v24-review.json`，不授予长稳或产品发布。
- 主控回归 `tests-final.txt` 为 225 项通过；独立测试作者增加 dispatcher 边界、
  缓存失效负控与重建恢复、DMA busy、取消及统计用例，四个隔离变异均检出，见
  `../output-refill-r1/independent-tests.json`。Release 与双 slot/boot 链接检查通过。
  独审 `source-review.json` 未发现所有权/相位选择阻断，目标反汇编确认两新增
  入口单次弱 CAS。静态 RAM 增用 24 B、主区余 20052 B；wrapper 与共享规划体
  栈帧合计 888 B，不含更深调用，不是整条 Core1 峰值栈。
- 固定 `p3-output-replan-r1/` PASS_WITH_WARNINGS，25 INFO、19 WARN、0 ERROR/FATAL；
  复用已确认拓扑，未重扫。最终源码
  `092143a701b2341668a1918a076c6545d2b4a3e14ad7ff1e14f39e62f3e3c567`，
  包 SHA `a8219efad3189870277053a89b26afd0295be82fae24335fa60ad2f620ce0858`。
  build ID 沿用旧值，必须以指纹区分；P3 基础门禁未扩展为锁相门禁。
- `positive-60s/` 四板联合参考/输出检查通过，最终均 CANCELLED，未见 STARVED；
  运行查询为零，CRC/STOP/RELEASE/配置恢复完成。独立规划调用
  6447/3547/3746/3437 次、提交 387/271/269/246 次、推进规划
  466/456/460/396 次，已记录最大墙钟约 199.324/210.112/231.928/205.520 µs，
  未记录预算超限。NO1/NO3/NO4 调用与计时样本差为 1/3/1，NO2 为零，
  因此 `strict_replan_qualification` 为 FAIL，不能以健康 PASS 覆盖计时缺口。
- `positive-600s/` 整轮 FAIL 保留：四板均通过至 420 秒；NO3 在自身 480 秒检查
  返回 `OUTPUT_READ`，NO2 在自身约 479.894 秒以 BINDING 提前冻结并触发
  `EARLY_FREEZE`，NO1/NO4 在 540 秒检出缺参考。各板起点不同，以上本地经过
  时间不能直接排列共同时间线或证明唯一因果。最终输出均 CANCELLED，未见
  STARVED；四板自主停止接受/退休、原生 CRC、STOP/RELEASE/恢复完整，运行查询零。
  已记录规划最大墙钟约 209.308/209.064/212.212/210.140 µs、预算超限零，但样本
  缺口 9/15/16/9，严格计时亦 FAIL。不能将部分正常窗口写成十分钟长稳通过。
- 下一 gate：先核对 OUTPUT_READ 与客户端门控竞争、NO2 绑定冻结的发生路径，
  修复观察可用性及计时发布；失败不降级为 PASS，也不以旧快照填充当前观察。
  `next-measurement-plan.json` 保留门控与生命周期的待证假设。随后重复长稳并与
  外部稀疏波形交叉核验；内部残差/引擎状态不授予实际 GPIO ±100 ns 或产品发布。

### VDC-PROGRESS-20260919-013：参考与输出引擎联合探针及关闭监督对照

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/output-starvation-r1/`。先闭合长检假通过缺口，
  输出补给修复保持后继独立切片，固定 quick P3 范围不扩大。代码已提交
  `b6aedfd8`，真实 pre-commit 核对最终源码与硬件凭证通过。
- 同一进度 012 源码关闭分钟 GUARD、保留普通 SUMMary 的十分钟对照：首轮
  `guard-off-600s/` 在 NO1 START 未取得显式 OK 后停止，未进入采样；原件保留。
  父清理曾报告 NO1 error queue 非空，最终配置读回与原值一致，不能将该轮写成通过。
  复测 `guard-off-600s-r2/` 完整采样、运行查询零、原生 CRC 复解一致、清理恢复
  全部完成。参考通过，但 NO4 在约 468.828 秒 STARVED，NO1–NO3 到主机 STOP。
  因此分钟监督不是饥饿的必要条件；不能由该对照量化探针开销或唯一确定根因。
  数字为当日快照，详细末态见 `guard-off-600s-r2-analysis.json`。
- 独审 `vdc-refill-review.json` 用生产 client/planner 复现：模型改变使缓存失效，
  连续 cached-only 服务不能重建，在 FIFO 尾部耗尽前若没有完整规划机会即断流。
  模型不变和恢复完整服务两组对照可继续补给。关闭 GUARD 的 NO4 末次失效距尾部
  约 8 ms，之后仍有三次服务，与该路径相符，但不称为完整硬件因果证明。
  既有快速预算已出现少量超限，不能把完整规划直接塞进同一个短预算或重放旧模型。
- 新版 GUARD 元数据沿用同一显式 ARM 命令，普通 SUMMary 不受影响。首次运行
  绑定当时输出 request，读取/身份失败锁存；检查点通过一次客户端弱 CAS 和后端
  已发布快照，核对身份、RUNNING/OK、服务/提交新鲜度与末序号推进。观察器不调用
  service/submit；失败仍按原本板 STOP/退休机制执行。最终 PASS 后到主机 STOP
  的尾段另审实际退休原因，不用旧参考 passed 字段代替联合健康结论。
- 主机测试最终 206 项通过，集成 trace/summary/owner 的独立集合 191 项通过，
  文档检查器测试 38 项通过，集合不相加。观察器测试覆盖释放后证据、错误代际、
  门控重入、失败不覆盖输出和无硬件副作用；三个隔离变异负控均被检测。首次读取
  失败/错误会话恢复后仍失败的用例已补。构建复核发现强 CAS 可生成重试，已改为
  弱 CAS 单次尝试，目标反汇编确认无 retry backedge。
- Release linker 快照静态 RAM 增用 44 B，主区余 20076 B；新观察器及其调用的
  后端快照嵌套栈帧合计 568 B，不是整条 Core1 峰值栈，也不是 WCET 证明，见
  `resource-r2.json`。本次不宣称零干扰或 GPIO 精度。P3 r1 因运行中有上述源码
  修正而拒绝签发凭证，原失败保留；须以最终指纹的 P3 和后继联合探针专项为准。
- 最终 `p3-output-guard-r2/` 为 PASS_WITH_WARNINGS，25 INFO、18 WARN、0 ERROR/FATAL；
  源码指纹 `ffb2397a110f041e2a1d5dfff610debb38b4b6f1598ad2d351180e8e732c78d8`，
  1293 文件。build ID 复用，按源码和 package 哈希辨认。凭证引用的 29 份原件
  逐一复算匹配，见 `receipt-r2-hash-check.json`。
- `positive-60s/` 四板新版 GUARD 均在首分钟 PASS、原因零、输出 RUNNING/OK、
  无自主 STOP；成功参考分别 35649/15339/15548/15322，运行查询零。主机统一
  STOP 后四路原生末态均为 CANCELLED，capture/request/session 身份、CRC、
  STOP/RELEASE 与父层 RAM 恢复通过；不是只看冻结 PASS。耗时 83.891 秒为试验
  快照，未用内部 residual 或已准入序号宣称 GPIO 精度。
- `expiry-120s/` 保留整轮 FAIL：STOP 配置 NO4 为有限输出，其引擎约二十秒
  EXPIRED，参考继续成功；第六十秒 GUARD 精确锁存 OUTPUT_STOPPED|OUTPUT_STALE，
  原因 EXPIRED，参考异常 flags 全零，并完成原 config 的本板 STOP/退休。
  NO2/NO3 分别在约 59.761/59.888 秒以 BINDING 提前冻结，GUARD 保留 EARLY_FREEZE；
  NO1 首分钟通过，第二分钟记录缺参考后退休。负例脚本原先只接受其他节点首分钟
  通过、第二分钟缺参考，因此整轮未通过，不能把 NO4 检出成功改写为全流程 PASS。
  主机约一百二十二秒才清理，运行查询零、原生 CRC、STOP/RELEASE 与恢复完成。
  该负例证明输出失效能独立于参考被检出，不代表健康锁相或四板同步停止。
- 独立复核 `hardware-review.json` 重算 P3 原件引用、两轮共八份原生 CRC 与
  生命周期；`c11-v23-review.json` 接受 v23 pending 范围，未提升产品发布或锁相。
  本次 C14 仅逐字迁出最旧进度 028 至 archive07，索引和 README 已闭合。
- `positive-600s/` 四板参考与输出引擎联合检查通过：十个检查点全部 PASS，mask
  1023，输出状态 RUNNING/OK；最终统一 STOP 后四路原因均为 CANCELLED，没有
  STARVED。运行查询零，四份原生 CRC 重解码通过，覆盖约 603.157–603.772 秒，
  每板 61 段，STOP/RELEASE 与恢复无遗留。详细复核见 `positive-600s-main-review.json`。
  NO2/NO3/NO4 相位实际提交 5501/5543/5604 次、频率提交 39/69/14 次；稳定完整
  段内部残差包络 [-234,108]/[-234,112]/[-203,110] ns，不是 GPIO 实测精度。
  输出快速服务预算超限计数 34/13/19/13 仍保留，不能用联合健康 PASS 声称 WCET
  已闭合。本轮无故障不覆盖旧偶发 STARVED；补给路径未修改、根因与重复性仍待
  后继切片。以上数字均为当日证据快照，非长期保证或正式锁相资格。

### VDC-PROGRESS-20260919-012：内部分钟监督、失败本板停止与通过保持环路

- TODO task ID：`VDC-OBS-007`，参考监督子切片完成，整项仍 IN PROGRESS。代码提交 `37ca2351`，匹配凭证
  和 pre-commit 已核对。新增可选 GUARD ARM 与独立
  元数据，复用原汇总 schema/池；Core1 按分钟检查内部参考/覆盖，Core0 持 TDMA
  control guard 核对原 config/capture/session/generation 后，仅对 FAIL 请求
  输出取消和环路退休。busy 重试；停止接受、精确退休证据分别保留。不授予产品
  隔离或 GPIO 精度，运行零主机采样，主机等待结束后才取回结果。
- 独审发现首次时钟失败未先绑定 config、零配置回卷被当作无效两项问题，均已
  修复；退休锁存不能借后继 config。完整首轮回归 234 项、后继 43 项通过；
  修正停止策略后的最终聚焦回归 52 项通过，数字为当日快照，集合不累加。
  r5 的零配置预期错误保留，修正测试为 matcher 拒绝后仍能 STOP，未放宽 matcher。
- 初版 `positive-60s/` 保留 FAIL：NO1/NO4 达标后自动停机，NO2/NO3 在约
  59.758/59.877 秒提前 BINDING 冻结。四板冻结前持续成功且异常 flags 全零；
  自动停止/退休、主机清理及参数恢复均完成。各板 ARM 起点不同，不能让先达标者
  断环；修正为 PASS 只锁存目标判定，最终由编排统一 STOP，失败仍本板自动止损。
  原件及原适配器 `capture-before-pass-stop.py` 保留，不能把首轮写成通过。
- Release 双槽与匹配源码 `p3-internal-guard-r2/` 完成，PASS_WITH_WARNINGS，
  INFO/WARN/ERROR/FATAL 为 25/18/0/0，复用已确认线序，不扩大 P3。复用构建目录的
  build ID 仍为 `20260918221036`，须按当前源码/package 指纹辨认，不能凭同号混用。
  原件引用逐项 hash 核对见 `receipt-r2-hash-check.json`；初版 P3 r1 也保留。
  主 RAM 静态增量 164 B，余量 20120 B，仅为 linker 快照，不代表栈峰值或 WCET。
- 实验适配器独审补齐缺参考负例不能接受任意异常、结论不得沿用外部示波器文字；
  最终四十五项纯内存测试通过。正常轮验证 PASS 不自动断环；负例要求原生 CRC、
  零成功、NO_SUCCESS/UNBOUND 与原因位一致、首分钟失败和实际退休，不冒称健康。
  证据根 `out/HardwareAcceptance/20260919/internal-guard-r1/`；独立源码与 C11
  审查见 `design-review.json`、`c11-review.json`，v22 保持 pending。
- 修正版 `positive-60s-r2/` 四板 PASS，目标达成后均未自行断环；完整原生 span
  超过目标，异常 flags 全零，运行查询零，主机统一 STOP/RELEASE 与恢复通过。
  独立重解码/分页 CRC、原始动作、退休和外层恢复核对见
  `positive-r2-hardware-review.json`；继承的 `flow_completed=false` 是旧详细采集
  未使用标志，本轮按真实步骤证据判断，不修改旧字段或以 passed 单字段替代核验。
- `negative-120s/` 不发 origin TRIAL，四板均于目标中首个六十秒检查点判 FAIL；
  NO_SUCCESS/UNBOUND、成功间隔及字段饱和与原生零成功一致，记录跨度约
  60.001–60.003 秒，guard 的接受/环路退休/输出退休均已确认。主机约一百二十二秒
  才发送清理命令，证明自主停止不依赖主机轮询；运行查询零，清理/恢复无遗留。
  此负例通过只证明故障被检出且退休，不代表 DPLL 健康。数字均为当日试验快照。
- 同一源码 `positive-600s/` 四板参考监督通过，十个分钟检查点全 PASS（mask 1023），
  四板 TDMA 保持至主机 STOP。成功数 364333/173362/176176/164227；原生 CRC
  重解码和保存 JSON 一致，运行查询零，清理/恢复无遗留。完整启动/尾段保留，
  不将稳定段替代整轮判定；旧 `positive-60s/` 失败仍独立保留。
  全部从板自第十秒起的完整段内部区间包络分别为 [-218,111]、[-207,107]、
  [-215,139] ns，比短轮有所扩大；这不是实际 GPIO 误差，也不授予全程百纳秒锁相。
  数字为当日试验快照；原始结果、离线表与 SVG 见 `positive-600s/offline-analysis/`。
- 独立复核同时发现 NO2/NO3/NO4 的 `run_output_raw.reason` 均为 STARVED，
  提前结束输出；NO1 为主机取消。当前 GUARD 只检查参考/模型，并未检查输出
  健康，因此原试验 `passed=true` 只在参考子范围有效，整机长时输出验收失败。
  实际退出字段见 `output-retirement-600s.json`；不能以最终 idle 掩盖饥饿，
  也不能用全局 service 最大间隔单独断言原因。原件保持，不改写为健康。
  独立核验 `600s-hardware-review.json`：相对 PIO anchor 约在 169.311/70.240/
  553.306 秒饥饿，最后约 25 ms 未成功补给，期间仍有 service。NO2 末态为
  SUBMIT_REJECTED/NOT_READY，NO3/NO4 为 DMA_NOT_READY；最大 service gap 均
  小于配置的 refill 窗口，不能直接归因 CPU 停服。早于本 guard 的
  `internal-generation-r1/capture-600s/` 四板末态均为正常取消，需同固件监督
  开/关 A/B 判别当前开销与补给状态机的关系，不能先断言与探针无关。
- 下一 gate：`VDC-OBS-007` 与 `VDC-OUTPUT-001` 先补输出健康分钟判定、定位
  PIO 补给/缓存/提交路径并修复饥饿，再推进 `VDC-SNAPSHOT-001`、
  `VDC-RECOVERY-001` 的质量老化、一致发布和恢复。实际 GPIO 精度仍独立验收。

### VDC-PROGRESS-20260919-011：坏启动 seed 不再提前消费 origin 授权

- TODO task ID：`VDC-OBS-007`、`VDC-RECOVERY-001`，保持 IN PROGRESS。提交
  `c7d15493`：自动交接在一次性 admit 前检查完整 seed、邮箱 CRC/类型/槽位/目标
  以及帧序列/身份；坏候选继续原 bootstrap，下一有效回包可启动。直接启动也复验
  全部邮箱。授权拒绝、begin/poll 后失败仍须 STOP；不回卷授权、不改物理拒绝码，
  自主稳态路径无新增扫描，启动扫描成本仍需持续关注。
- 旧代码负例复现同类 MAILBOX 拒绝；新真实适配器回归证明坏候选不消费授权、不调用
  begin/poll，后续真实 TX/RX 可恢复。相关 60 项通过；另发现旧几何夹具缺少
  priority STOP 桩，补齐未安装 IRQ 的边界后 56 项通过，旧失败日志保留。
- 最终指纹四板 `p3-origin-seed-r3/` PASS_WITH_WARNINGS，build `20260918221036`，
  INFO/WARN/ERROR/FATAL 为 23/22/0/0，29 个引用 hash 核对通过。未重扫线序，
  不把锁相质量纳入 P3 基础门禁；上述数量均为当日快照，非产品事实源。
- 新固件独立十秒及六十秒内部复采通过，运行查询零。六十秒有效成功数
  33416/13883/14102/14004，三从调频 14/17/24、相位更新 557/563/573；异常 flags
  全零，首成功最大间隔 0.487/0.624/0.748/0.851 秒。原生 CRC/解码、STOP/RELEASE
  与恢复均完成；不能据此声称 GPIO 精度或全部跨启动可靠性。
- 证据根 `out/HardwareAcceptance/20260919/origin-seed-r1/`，含独立代码审查、旧版
  负对照、软件测试、`receipt-hash-check.json` 和 `capture-check.json`。上一长窗
  失败原件不覆盖，也未确定当时具体坏邮箱字节。下一 gate：设备端分钟健康判定
  与合法停止，再扩连续长窗；内部观察不代替实际输出精度及 VDC 失联质量发布。

### VDC-PROGRESS-20260919-010：静默启动取消非必要模型查询

- TODO task ID：`VDC-OBS-007`，保持 IN PROGRESS。上一轮 START 前可选 MODEL
  管理查询 timeout 三秒，trace 已计时，导致首成功间隔超限。新实验适配器仅
  配置阶段 NO1 的该查询明确省略一次，`model_before_start=null`，记录未发送；
  不伪造旧值或有效响应，其他配置/身份/owner ACK 与 STOP 后采集均保持。
  固件未改，源码指纹仍绑定 build `20260918210835` 的原 P3；原失败完整保留。
- 五项独立无硬件边界核验通过，见 `adapter-review.json`。新 `capture-60s/`
  静默 60.015 秒严格通过，四板各七段，异常 flags 全零，首成功间隔
  0.493/0.645/0.768/0.928 秒（当日快照，非产品事实源），未修改一秒门限。
  运行查询零，STOP/RELEASE/参数恢复通过，独立 CRC 与生命周期核验见
  `60s-review.json`。后继 `capture-600s/` 已结束并 FAIL：NO1 在约 0.454 秒冻结，
  四板有效参考成功数均为零；从板空段与计数饱和完整保留。该轮不是锁相精度失败，
  而是 NO1 自主 origin 尚未完成启动。全板 STOP/RELEASE/参数恢复完成，错误原件不覆盖。
- 按真实 SCPI 字段拆解：handoff 的 12 是阶段总数，不是 builder 拒绝码。
  release 的 `physical_reject=1966081` 拆为 `PREPARE_STAGE` 与 MAILBOX stage；
  仅 MAILBOX 被调用一次，后继 BUILD_BEGIN/STEP 均未进入。前置配置检查有独立
  CONFIG 拒绝编码，当前保留的是邮箱批校验失败；尚无失败 slot/原始 seed，
  不能断言是 CRC、class 或 mask。代码前置交接仅验 header，后置物理准备才验邮箱，
  正在验证不消费授权、不碰硬件的坏 seed 延后准入方案；不得放宽 CRC 或伪造 seed。
  独立核对原始分页/CRC、拒绝码与清理结果见 `capture-600s/independent-failure-review.json`；
  从板 17.17986918 秒为字段饱和值，不能当成真实最大间隔。
- 原固件同配置 `repeat-10s/` 单独复采通过内部覆盖/成功间隔检查，运行查询零，
  四板有效成功数 2345/1714/1698/1698（当日快照，非产品事实源），STOP/恢复完整。
  复采不能消除上一轮失败，也不能证明跨启动可靠或实际 GPIO 精度。
- 证据根 `out/HardwareAcceptance/20260919/internal-startup-r1/`。分钟自动止损、
  实际 GPIO 精度和失联质量老化仍需独立验证，不能由内部正常跟随直接授予。
  下一 gate：先用短窗闭合启动退避/拒绝，再实现同会话设备端分钟判定与合法 STOP；
  现有静默脚本只能结束后回看，本轮确实等待了完整长窗，不能称在线止损已完成。

### VDC-PROGRESS-20260919-009：内部探针 TX 基线绑定当前代际

- TODO task ID：`VDC-OBS-007`，保持 IN PROGRESS。新 capture 首个 service 可能
  先于新 TX provider，旧代 rejected 被采作基线，新代 owner 清零时误标为
  COUNTER_RESET。修复基线只采用当前 sync generation；未取得当前代基线时
  首个当前代快照从 owner 零初始化累计，开窗已有当前代基线则仅累计后续增量。
  同代回退、首次读取失败、已采当前代后的异代均保留异常；未忽略首段或尾段。
- 原汇总/解码/静默编排组合回归 214 项通过；新增真实 TX owner 正负对照为
  旧版八个预期失败、六个通过，新版十四项通过，完整 summary 文件 52 项通过。
  覆盖旧/新代、已有当前基线、真实回退与读取失败；schema、布局、池大小和
  严格解码门限未改。数量为当日快照，非产品事实源；独立审核同意 v21 pending。
- 新源码 Release 与 `p3-internal-generation-r1/` 四板 quick P3 完成，build
  `20260918210835`，PASS_WITH_WARNINGS、ERROR/FATAL 零，保留 27 项 WARN 及
  strict false。独立核对源码指纹与 29 个引用 hash，见 `p3-independent-review.json`。
- `capture-60s/` 静默六十秒通过，四板各七段，coverage/成功覆盖/间隔判定均过，
  异常 flags 全零，运行查询零，STOP/RELEASE/参数恢复完成。NO1 原生拒绝合计
  25226，STOP 后 TX 末态 25227、last_reject=STOP；差值与冻结后 STOP 退休新增拒绝一致，
  不宣称两者全等，首次拒绝精确计数仍由真实 owner 边界回归证明。
- 证据根 `out/HardwareAcceptance/20260919/internal-generation-r1/`；同配置
  `capture-600s/` 单次静默 600.078 秒，四板各 61 段、覆盖完整、异常 flags 全零，
  成功跨度均约 600.573 秒；三从频率更新 38/72/27、相位更新 5183/5231/5216。
  运行查询零，稳定段内部残差端点为 −198..103/−200..106/−193..107 ns，不能
  当作 GPIO 边沿误差。严格整轮仍 FAIL：首成功间隔 3.475..3.853 秒超过原一秒
  判据；配置末尾 NO1 MODEL 查询 timeout 恰为三秒，紧接四板 START，trace 从 ARM
  阶段已计时。后续 bin 最大成功间隔 0.093..0.264 秒，无异常；首 bin 聚合极值
  不能反推其每个运行间隔，不扣掉启动时间来重写通过。详见
  `startup-boundary-analysis.json`，下一切片处理这条非必要启动诊断与阶段边界。
- 600 秒外层恢复报告保留 NO1 error queue 非空和 restore-needed；最终四板
  initial/final 参数一致、TDMA 均 STOP、错误队列均零，STOP/RELEASE 完成。
  保留原 FAIL，不把最终参数一致称外层严格通过。分钟检查点设备端有界告警仍
  待实施，候选在 `minute-checkpoint-design-review.json`；本地 STOP 不等于四板
  联停。发布后继审计在 `vdc-publication-next-audit.json`：无新 evidence 的
  age 服务及 idle ready 发布需验证，正式 freshness/HOLDOVER 另行小切片闭合。
- 本切片源码提交 `4594a2b9`，匹配源码 P3 凭证与 pre-commit 通过；文档单独提交。
  独立长窗复核见 `capture-600s-independent-review.json`，不提升整轮失败结论。
- C14 按最旧连续条目逐字轮转 026/025 至 archive 07，索引闭包与正文 hash
  核验见 `progress-rotation.json`。不删除旧失败证据。

### VDC-PROGRESS-20260919-008：探针外部对照与 VDC 发布漏更新修复

- TODO task ID：`VDC-OBS-007`、`VDC-SNAPSHOT-001`，保持 `IN PROGRESS`。本条数字
  为当日实验快照，非产品事实源。先复核六百秒内部区间：残差端点包络与同段最大
  宽度相关系数约 0.983/0.985/0.990，与最大服务间隔相关性较弱；分段极值不保证
  同事件，不能推导因果或 GPIO 误差。独立只读分析 `probe-ab-residual-analysis.json`
  建议优先检查时间戳/匹配区间宽度，未据此修改闭环增益。
- `probe-scope-ab-a1/b1/a2/` 使用相同 timing `24000,32000,16000 us`、输出 delay
  `0/-28/-88/-116 ns`，按 OFF→ON→OFF 三次独立热启动，每轮每五秒采样、六十秒
  检查点。三轮各十二组四路新鲜 RAW 窗口，每从二十四个实际上升沿；2 ns 网格、
  2.5 V 交点、相对 NO1 未独立居中，三轮全在 ±50 ns 内。ON 一轮三从范围为
  −29.84..−14.82、−31.96..−14.99、−12.00..9.93 ns，未观察到明显增大，但独立
  启动/温漂和稀疏窗口限制保留，不宣称零开销或全程精度。四板运行查询为零，
  无提前输出退休，STOP/恢复完成；ON 严格内部报告保留 NO1 首段 COUNTER_RESET。
  SHA/RAW 重解码对比和图在 `probe-scope-ab-analysis/`。
- 修复前 STOP 读回 `publication-before-fix/` 证实：NO2–NO4 FOLLOW 已有新调频与
  相位更新，Core0 DCO consumer 和 RefMem DPLL 向量仍为 DCO seq=1、0 ppb。
  原因是两消费者按 `dpll.update_seq` 去重，而本地 rate/phase 只推进 DCO 序号。
  本切片让完整快照带同次稳定偶数 guard 的 `publication_revision`；失败不确认，
  零值回绕可消费，每个 RefMem beat 仍至多一份向量。旧 wire、证据序号、clock
  模型和质量语义保留，不把更新成功当作正式 LOCKED 或失联 aging 完成。
- 组合回归 315 项通过；既有 `test_core1_overrun_quarantines_only_the_faulting_load`
  因旧文本期望失败，`application/src/app.c` 与该测试文件和 HEAD 完全一致，证据
  `vdc-publication-existing-test-failure.json`。新增真实 Domain/管理/RefMem 测试
  对旧 HEAD 的 rate/phase/quality 漏更新均可复现，新实现及布局 golden 最终
  十六项通过。独立方同意 `VDC-PUBLICATION-01 v1 pending`，原件
  `publication-independent-review.json`。
- 新源码 Release 与 `p3-vdc-publication-r2/` 完成，build `20260918204248`，
  PASS_WITH_WARNINGS、ERROR/FATAL 均零，严格质量失败保留；首次 r1 仅因复用的
  topology 非原始测量身份在硬件前拒绝，r2 使用原始已测矩阵，未重扫线序。
  `publication-after-fix-r1/` 静默十秒、运行查询零，四板模型/Core0 consumer/
  RefMem DPLL 的 DCO 序号、source、lock、phase、rate 五个公共字段一致。
  三从 DCO 序号 112/114/113、频差 2226/2538/4675 ppb 已实际发布；旧 DPLL
  证据序号仍为一，证实修复不靠篡改证据序号。对账见
  `publication-readback-comparison.json`，不把局部字段一致称完整发布验收。
- `publication-scope-r1/` 在 START 前 NO3 RUN 配置超时，恢复时原 error queue
  非空导致保留 restore-needed；最终读回四板均 STOP、参数与原值一致。原失败
  不覆盖。同参数有界复测 r2 连续运行至六十秒检查点；首个示波器采传耗时
  5.187 秒，十秒窗口遗漏，检查点正确 FAIL 并停止。其余十一个有效四路窗口
  每从二十二边沿，范围 −30.33..−13.12、−30.51..−16.04、−12.04..9.71 ns，
  均在 ±50 ns 内，未见已采窗口恶化；不能宣称完整覆盖或全程精度通过。
  NO1 首段 COUNTER_RESET 仍使内部严格判定 false，运行查询零，STOP/RELEASE/
  恢复完成。RAW 重解码及图见 `publication-scope-analysis/`。源码提交
  `dbc77b6d`；独立最终核对见 `publication-final-evidence-review.json`。
- 证据根 `out/HardwareAcceptance/20260919/`。下一 gate：内部启动/运行/停止
  分级与分钟检查点有界告警，完善无示波器自检；继续 quality/valid/freshness
  和失联恢复，完整 VDC 发布未完成，不能以本切片取消正式质量门禁。

### VDC-PROGRESS-20260919-007：内部汇总扩窗与连续证据推进

- TODO task ID：`VDC-OBS-006`、`VDC-OBS-007`，保持 `IN PROGRESS`。本条数值为
  2026-09-19 实验快照，非产品事实源。显式双参数 summary ARM 新增独立窗口 schema，
  旧单参数版本保持固定间隔；十秒段复用原有 76 个百字节槽位，未新增记录环。
  request 参数与 ACK 一致交接，STOP 冻结头保存实际跨度，服务缺口门限不放宽。
- 生产记录器及历史解码/工具组合测试 278 项通过，真实 libscpi 参数解析 106 项
  通过。覆盖六百秒模拟、计时低字回绕、旧新 schema、STOP 前/上/后边界、FULL
  不可变、RELEASE/ACK、无效参数不抢池不消费 ID、缺口和字段饱和。独立只读代码
  审核 `internal_probe_review` 未发现阻塞项；正式 WCET 和无扰 A/B 尚未证明。
- `p3-summary-window-r1/` 完成 Release、四板 OTA 与固定范围 quick P3，build
  `20260918195501`，`PASS_WITH_WARNINGS`，ERROR/FATAL 为零。沿用已确认线序，
  未扩展 P3 必验范围或修改 OTA。四板专项通过 `internal-summary-probe.py` 复用
  已验证配置和并行 START，运行期不查询板卡，全板 STOP 后才下载并释放记录。
- `internal-window-r1/` 五秒首轮通过，四板 schema/跨度/CRC 正确，三从有效匹配、
  调频和相位更新，STOP/RELEASE 与外层 RAM 参数恢复完成。
- `internal-window-r2/` 静默六十秒，四板首末成功跨度均超过六十秒；三从成功数
  12747/13012/13269，调频 17/36/13 次，相位 563/579/562 次。稳定部分内部残差
  区间分别 −111..104、−113..105、−120..105 ns。严格报告仍为 false：NO1 第一段
  COUNTER_RESET 保留；其他段无服务缺口、空输入、时钟异常、未绑定或字段饱和。
  四板 STOP/CRC/RELEASE/参数恢复均完成，未将 DCO 修正量写成实测晶振频差。
- `internal-window-r3/` 静默一百二十秒，各板保存十三段；三从首末成功跨度均约
  120.46 秒，成功数 25690/25876/26499，调频 13/28/31 次，相位 1073/1110/1118 次。
  稳定部分内部区间 −141..106、−135..106、−127..119 ns；仅 NO1 首段保留
  COUNTER_RESET。`vdc_priority_tx_core1()` 首次消费新 generation 时重置 TX 工作区，
  summary 可能先读到旧 generation 计数，是该边界现象的代码解释；不凭此抹除原件
  或将其升级为整轮严格通过。STOP、CRC、RELEASE、恢复成功，运行查询数为零。
- 扩窗代码及匹配 P3 receipt 已提交 `9d734c82`，staged 指纹核验通过；文档双检查、
  38 项文档测试、pre-commit 与逃生门审计通过。Windows PowerShell 运行原生工具，
  hook 使用 `D:/Aphranda/Git/bin/bash.exe`，避免当前 PATH 缺少 `sh` 的问题。
- A/B 两槽链接 map 对比上一包：记录池均为 7600 B、工作区均为 288 B；request
  从 32 B 变为 36 B，扩窗只增加该参数字段。原件为 `window-ram-comparison.json`。
  为满足 C14，将最旧连续两条 20260917-024/023 逐字轮转至归档 06，正文 hash
  核验一致，见 `window-progress-rotation.json`；双文档门禁复跑通过。
- `internal-window-r4/` 单次静默 600.078 秒，无重启拼接，运行查询 0；四板各
  61 段，三从首末成功跨度约 600.50 秒。三从成功数 127209/128981/132202，
  调频 31/55/27 次，相位 5426/5470/5437 次。除 NO1 首段 COUNTER_RESET 外，
  四板无 SERVICE_GAP、NO_SUCCESS、CLOCK_INVALID、UNBOUND 或饱和，严格报告
  仍为 false；三从内部覆盖/成功间隔判据通过。最大服务间隔三从约 43.89/40.65/
  45.48 ms，不能将秒级缺口门限通过写成逐周期实时验收。十秒后内部残差区间
  极值 −223..110、−216..142、−220..113 ns，较短窗有所扩大，须跟进宽度与
  控制变化并做外部同窗对照，不能将其当成 GPIO 超差或完全锁相证明。
  全板 STOP/CRC/RELEASE/外层参数恢复完成；逐分钟重叠段与图见该轮
  `capture/analysis/`。分钟检查为离线回看，本轮未提供运行中每分钟止损。
  负残差最小值出现在 NO2/NO3 第十八段、NO4 第二十三段；末尾部分段区间为
  −98..95、−99..96、−98..98 ns，不能仅凭全程极值断言持续发散。稳定段最大
  区间宽度约 300/300/312 ns；它们不保证与残差极值属于同事件，不能直接相减
  推导相位真值。定位原件为 `analysis/residual-extrema-evidence.json`。
  独立复核原件 `window-r4-independent-review.json` 确认原生/分页 CRC、保存解码、
  十个分钟窗口、零运行命令与 STOP/RELEASE/恢复一致；严格 false 与精度边界保留。
- 证据根为 `out/HardwareAcceptance/20260919/`，每轮 `capture/analysis/` 保存
  原始 flags 的离线汇总和 SVG。扩窗只证明内部跟随观测能力，不授予 GPIO 精度，
  也未证明主机指定起止点与本地捕获端点严格对齐。下一 gate：独立连续长窗、
  启停边界分级、外部同窗开销对照及 VDC 发布；不能拼接多轮为连续六百秒。

## 归档索引

规则：此处只登记已迁出的历史证据段，条目正文逐字保留、未编辑。轮转规则见 §0 C14 / 环5。

| 文件 | ID 区间 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_11.md` | VDC-PROGRESS-20260919-006..VDC-PROGRESS-20260918-034 | 25 | 2026-10-14 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_10.md` | VDC-PROGRESS-20260918-033..VDC-PROGRESS-20260918-007 | 27 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_09.md` | VDC-PROGRESS-20260918-006..VDC-PROGRESS-20260918-004 | 3 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_08.md` | VDC-PROGRESS-20260918-003..VDC-PROGRESS-20260917-031 | 4 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_07.md` | VDC-PROGRESS-20260917-030..VDC-PROGRESS-20260917-025 | 6 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_06.md` | VDC-PROGRESS-20260917-024..VDC-PROGRESS-20260917-023 | 2 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_05.md` | VDC-PROGRESS-20260917-022..VDC-PROGRESS-20260917-022 | 1 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_04.md` | VDC-PROGRESS-20260917-021..VDC-PROGRESS-20260917-012 | 9 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_03.md` | VDC-PROGRESS-20260917-011..VDC-PROGRESS-20260917-011 | 1 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_02.md` | VDC-PROGRESS-20260917-010..VDC-PROGRESS-20260917-001 | 10 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_01.md` | VDC-PROGRESS-20260916-043..VDC-PROGRESS-20260906-002 | 146 | 2026-09-17 |
