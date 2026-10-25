# TDMA 基础件主域任务进度

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_TASK_PROGRESS.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-10-26

本文档记录 TDMA foundation 的阶段性任务进度、验证结果和后续动作。待办事项放在 `TDMA_DOMAIN_TODO.md`。

## 文档接口

架构维护稳定边界，TODO 维护任务状态；本文件与归档保存实施证据，不替代契约登记。
历史记录中的预算、节点及门禁描述只代表当时快照，当前口径以最新条目和主域文档为准。

## 当前 checkpoint

### TDMA-PROGRESS-20261026-045：tdma_service.c 拆为 main + Core1 单元（N5.0.4a 打样）

- **范围**：`tdma_service.c` 2,283 → 1,202 行；新增 `tdma_service_core1.c` 1,078 行与
  `inc/tdma_service_internal.h`（46 行）。两者均满足 `EXE-CODE-01` 的"拆分后单元 ≤1,500"。
- **边界**：intent 提交（`has_pending`/`enqueue_scheduled`/`submit`）、调度与恢复
  （`dispatch_next_scheduled`/`complete_scheduled`/`stage_active_recovery`）、Core1 执行
  （`generic_core1_service`/`core1_service`）、CRC 与全量快照、结果帧读出。函数体逐字位移。
- **跨边界调用实测单向**：Core1 单元调用主单元 10 个 helper（now/elapsed/delta/split_u64/load/
  begin|end_intent_write/begin|end_result_write/publish_exec_timestamp），主单元对 Core1 单元
  0 调用；`submit` 仍是 Core1 单元内 `static`，未产生跨 TU 双实例。私有头只导出这 10 个原型，
  不定义任何状态。
- **归档而非删除**：拆分前全文以 100% 复制移入 `components/tdma/archive/tdma_service_split_v1/`
  （SHA256 `BFA4829D…F68F84C`），`ARCHIVE_NOTE.md` 记录边界、映射与"不得删除"规则。
- **等价性证据**：`tdma_service_*` 符号 77 → 86（+10 个 helper 转为外部符号，-1 个被内联的
  `tdma_service_submit`）；同名符号代码量合计 12,934 → 12,942 B（+8 B，来自 `submit_tx/rx`
  各自内联 `submit`）；无 section/`__not_in_flash`/`__scratch` 迁移；条件编译仍只有
  `PICO_ON_DEVICE` 一处。
- **验证**：host 同集 `test_tdma_geometry_config`/`test_tdma_start_admission` 40 passed、
  `test_tdma_foundation_snapshot` 等 81 passed、`test_tdma_service_nonblocking` 23 passed、
  `test_state_machine_resource_check` 50 passed；P3 quick
  `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC` = `PASS_WITH_WARNINGS`（INFO 33 / WARN 10 / ERROR 0 /
  FATAL 0，build `20261025163403`，1,536 文件），凭证 `config/hardware_acceptance/p3_acceptance_receipt.json`。
- **遗留（⚠️，非本拆分引入）**：`sync_dpll_fb` 之外，`test_tdma_service_timing`、
  `test_tdma_command_dma`、`test_tdma_diagnostic_burst`、`test_tdma_seat_rotation`、
  `test_tdma_seat_invariants`、`test_vdc_command_ingress` 共 7 项 host 失败仍红：前者缺
  `tdma_pio_spi_ring_origin.c`/`phys_geometry.c` 等新单元源清单，后者按 `distributed_refmem.c`
  找已迁入 `distributed_refmem_internal.h` 的 `distributed_refmem_tdma_flight_sync_t`。
  两者均由 `0bc4e013`/`dca0de1d` 的既有拆分造成，与 N5.0.4a 无关；复验判据 = 修源清单后重跑，
  归属里程碑 = N5.0.4b（TDMA adapter 拆分时一并修）。
- **交付**：`81b417d7`（代码+测试+凭证）、`aa24027c`（N5.0.4a 登记修正）。

### TDMA-PROGRESS-20261025-044：6 个活 .inc 片段全部拆为独立 .c 单元（refactor 收口）

- **范围**：geometry / rx_scan / priority / flight_io / origin / event 六片段（约 273KB）拆为
  `components/tdma/src/*.c` 独立翻译单元；`tdma_pio_spi_phys.c` 不再 include 任何 `.inc`
- **内部头成为唯一跨单元面**：状态 extern + 函数原型。本阶段新增 event 核心 13 函数/3 状态、
  rx_first 11 入口/2 状态、RX start-cut 10 入口/5 状态、flight_io 18 助手、origin 5 助手、
  priority 3 入口、geometry 13 入口、rx_scan 3 入口；`tdma_pio_spi_phys_alignment_fits`
  以 guarded `static inline` 在内部头与片段间共享
- **归档而非删除**：六个片段与 `marker.inc` 以 100% 重命名移入
  `components/tdma/archive/tdma_inc_split_v1/`，`ARCHIVE_NOTE.md` 记录边界、审计来源与
  `TDMA_PHYS_<UNIT>_HARNESS` 前导守卫（供自带类型/宏的合成 harness 编译完整生产体）
- **等价性证据**：每个状态实例数均为 1（无跨 TU 双实例）；导出符号集只增不减；条件编译只保留
  `PICO_ON_DEVICE` 守卫；`nm --print-size` 复核热路径地址/大小拆分前后一致，例如
  `tdma_rx_start_cut_monitor` = `0x2000f06c/0x234`、`tdma_rx_first_window_admit` =
  `0x100b9258/0x74`、`tdma_rx_start_cut_after` = `0x100ba084/0x158`
- **验证**：full P3 `out/HardwareAcceptance/20261025/hawf-224435`（build `20261025144447`，
  1532 文件，`PASS_WITH_WARNINGS`，step 30 OK，外部 26.1 ns）；归档提交复用
  `hawf-231504`（build `20261025151524`，1532 文件，外部 28.0 ns、稳态 117/104/151 ns）；
  host 工作流 `out/host_acceptance/20261025/hawf-host-233945`，files=526，
  426 passed + 1 xfailed
- **工具**：host 凭证改为 6 步工作流 `prepare → scope → select → execute → receipt → selfcheck`
  （`tools/acceptance/host_acceptance_workflow.py`，`--profile quick|full`、`--run` 续跑），
  与 P3 一样按运行目录 + staged 指纹绑定，任一步失败即中止且不写凭证
- **残余**：`tdma_pio_spi_phys.c` 仍有约 95KB 主单元（服务编排、PIO/DMA 底层与校准路径），
  不在本目标范围；trn03 的 `test_load_mask_releases_only_newly_enabled_loads` 与 `app.c`
  现状（quarantine 释放已含 foundation mask）不一致，已标 xfail 待 owner 决策；
  `.inc` 片段现仅存在于归档目录，审计用，不得删除

### TDMA-PROGRESS-20261012-043：T2 工作流改造为 22 步可步进流程；修正两处 SCPI 解析错误（推翻"FOLLow 阻塞链"）；台架标定阶段自愈，`--steps 0-6` 全绿

- **工作流改造**：`tools/sync_trigger/t2_enable.py` → **`tools/sync_trigger/t2_workflow.py`**（`tools/tool_registry.json` 与 `tools/hardware_acceptance/p3_hardware_acceptance.py` 同步；治理计数 `owned_files == visible_files == 577`）。一条代码路径、**22 步（0–21）**，支持 `--list`（步骤表）、`--step N`（单步）、`--steps 1-6`/`3,7,12,16-19`（任意集合，按序）、`--full`（连续，验收脚本走此路径）、`--interactive`（步间回车）。每步输出**时间戳、耗时、状态（OK/FAIL）与具名失败原因**，并打印本步最贵的 3 条命令；整场 stdout tee 到 `out/tdma_t2/steps-<stamp>.log`，步骤内细节不再只留在终端。
- **两处 SCPI 解析错误（旧阻塞链据此推错，已推翻）**：
  1. `SYSTem:TDMA:SCHEDule?` = **8 头部字段 + 每相位 11 字段**，相位序按 `app.c:1256-1303`（0 TDMA/1 VDC/2 DPLL/3 CALIBRATION/4 SYNC_CAPTURE/5 REFMEM/6 MODEL/**7 SYNC_TRIGGER**/8 TRIGGER_MEASURE）。旧代码用 stride 9 且取相位 6，读到的是别的相位的静态字段（"run 计数不变"是误读）。实测相位 7：`wcet 26800`、窗口 `335000..365500`（宽 30500 > WCET，**放得下**）、`runs 90097`、`start_miss 5963/1.26M` ⇒ **优先级/FOLLow 不是阻塞**；旧引用的 `18764` 是 SYNC_CAPTURE 的 `deadline_miss`，`298500/332000` 是 REFMEM 的起止。
  2. `TRIGger:REServation:T2?` 的 `segment_max_cycles` 在 **[42..44]**（旧代码读 `[47..49]` = `freeze_resend_count` 等，恒 0 → 假的 "SERVICE NEVER RAN"）；`announced_mask=[31]`、`core0_prepare_calls=[39]`、`core0_preview_saved=[40]`、`core0_prepare_last_reason=[41]`、三个位置的 `(target,threshold)` 在 **[32..37]**（实测 `(5000,4750)(10000,9500)(15000,14250)`）。停机实测 `segments 8057/0/0` ⇒ **服务在跑**。
- **真正机制**：Core0 对 `last_counter_count` 已越过 `threshold_count` 的位置**永不生成 preview**（`trigger_t2_runtime.c:919-924`），无 preview 则 `stage_one_freeze()` 直接返回（`:210-211`）⇒ 确定性不发布。通过轮 t+2s 计数 ≈10315（阈值 4750/9500/14250 ⇒ `published_mask 3`）、t+6s ≈16184（⇒ `7`）；失败轮 t+2s 已 66–69 万且 `published_mask 0`。
- **顺序与自愈（已落地）**：Step 11 只 `UNLoad/LOAD/CLEar`（不 `STARt`）；Step 14 转台/源**只配置不启动**；新增 Step 18 时基就绪判定（cursor `err 0/valid 1/epoch==本次会话` + 模型 token/session）；**Step 19 = 原点试运行 + 启动脉冲流**，带**阈值守门**（计数 ≥ 第一阈值即拒绝并给具名原因）；新增 **Step 0 标定阶段自检与回放**。
- **台架修复（Step 0 的实际价值）**：NO.1/NO.2 的 `READ:CALibration:TRAINing:STAGe?` 返回 **`EMPTY`**（几乎肯定是此前误用 `CALibration:TRAINing:STAGe:CLEar` 所致），导致 `Provisional?=0` 且 NO.2 环 `ARM` 报 `reason 9`。按 `BEGin`/`LINK`/`COMMit` 从 slot 2 回放并落 NVS（`COMMit -> "TRN03NVS",210,1,3539521614`），四板阶段一致 `TRN03STG,1,4,31,1,210,3,3816963506,1383759744,2993488091,15` ⇒ `Provisional? = 1,50,4,2`。
- **代价修复**：共享发送器对"普通写"本应用 `action_timeout`，但工具 namespace 未设 ⇒ 每条写等满 8 s 查询超时。补 `action_timeout=0.25`、对结果码冗余的写命令加 `fire()`（0.6 s）、`poll` 加墙钟预算。实测 Step 2 **195.3 s → 9.1 s**、Step 1 66.2→4.1 s、Step 4 30.4→1.4 s，**1–6 合计 326 s → 18.5 s**。
- **验证**：`python tools/sync_trigger/t2_workflow.py --steps 0-6` → **`failed steps: none`，退出码 0**；Step 0 OK 0.5s / 1 4.1s / 2 9.3s / 3 1.4s / 4 1.2s / 5 0.6s / 6 1.9s（四板 `stage OK`）。日志 `out/tdma_t2/steps-20261012-004300.log`。
- **尚未完成**：Step 7–21 的顺序改造已就位但**未整链验证**；T2 仍未跑通发布；未提交（AGENTS.md §7：工具改动提交前需一次 P3 凭证，当前树为脏树）。

### TDMA-PROGRESS-20261011-042：T2 **端到端跑通过一次**（四板 configured、发布 3 个位置），但**尚不可重复**（1 通过 / 4 失败）

- **修正声明（重要）**：本条最初只记录了那次通过，属于单次证据。此后相同判据、相同/近似源码的复跑**全部失败**，因此**不得**据此认为 T2 已稳定跑通。失败样本与产物路径：
  | 运行 | 产物 | 结果 |
  |---|---|---|
  | 20261011-190909 | `out/HardwareAcceptance/20261011/t2-20261011-190909/report.json` | **PASS** `passed=true, configured=4, publishing=3` |
  | 20261011-192606 | 同目录 `t2-20261011-192606/report.json` | FAIL `configured=4, publishing=0` |
  | 20261011-194304 | `t2-20261011-194304/report.json` | FAIL `configured=4, publishing=0` |
  | 20261011-200134 | `t2-20261011-200134/report.json` | FAIL `configured=4, publishing=0` |
  | 20261011-（背靠背第二次） | 同上目录 | FAIL `configured=4, publishing=0` |
- **失败特征（已定位到根因，2026-10-11 21:50 修正）**：失败轮的 `T2?` 分段周期恒为 **`segments 0/0/0`**，即 `trigger_t2_runtime_service()` 从未运行；游标在失败轮**同样健康**（`err 0 / valid 1 / sync_seq` 递增，走兜底路径）。**先前把 `LOAD:MASK` 当主因是错的**：运行中（预检）读回是 `mask 91`（bit6 已置位），之前的 "27" 是我自己的 teardown 在读取前写回 27 造成的**测量伪影**。
- **真正根因（逐级实测）**：
  1. `SYSTem:TDMA:SCHEDule?` 的阶段 6（SYNC_TRIGGER）run 计数**冻结**；其窗口 `18764..298500`（宽 279736）**小于**声明 WCET `332000` → 每周期被 `start_missed` 跳过（`app.c:930-931`）；只有"优先级源 active"给出 ingress 余量时才放得进去（`app.c:888/904-905`）。
  2. 停机态读 `SYSTem:VDC:PRIORity:*`：NO.1 `SYNC?` 已接受 ✓；从板 `MATCH?` 已接受 ✓，但 **`FOLLow? = 0`（被拒）**、`FOLLow:PHASe?` 被拒 → **优先级通道从未 armed** → 阶段 6 永不运行 → 无发布。
  3. `FOLLow 1` 的前置（`vdc_priority_follow.inc:60-71`）：需要活跃会话 + `priority_generation == priority_match_generation()` + **`s_priority_match_request_session == session`**，并要 `boundary_set_stopped()` 成功。唯一通过的那次紧跟在 `dpll60`（跑过整环、可能已对齐 MATCH 会话/boundary）之后 59 s；之后每次背靠背运行都在**尚无载波记录**时写 FOLLow → 被拒。
- **代价改进（已落地）**：预检改为**致命**（相位 6 全板冻结即跳过采样），且 `SystemExit` 改为"仍走 teardown" —— 单次失败尝试从 ~18 分钟降到 **~1 分钟**，并顺带打印停机态优先级读数、把台架留在干净状态。
- **下一步（判据先行）**：把 `FOLLow 1` / `FOLLow:PHASe 1` 放在**环已运行且 MATCH 会话一致**之后写入并**读回校验 `FOLLow? == 1`**；该判据通过前不看 `publishing`，避免再用最终计数当调试回路。

- **另一处已发现的自伤**：链首"等待拓扑提交"的判据要求序号**推进**（`!= before_seq`），在**重复运行同一拓扑**时序号不变 → 等待必然超时（最新失败轮出现 `WARNING: ring config not committed: ['25','25']`）。应只要求 `config_seq == applied_config_seq`。
- **仍成立的部分**：起链配方（会话/拓扑提交/PROBe+OPMode/清表/验证式激活/代递增装表/计数器 LOAD/T2 配置/优先级/Provisional/ARM→TRAIN→START/掩码 91/源启动/试运行）与 `out/tdma_t2/t2_calib_replay.json` 的复用路径；`dpll60` PASS（`dpll60-110200`）证明 DPLL 本体正常。
- **提交策略**：可重复性解决前不提交（凭证也因 19:43 的工具编辑失效）；届时在冻结树上跑一次 P3 付凭证。


- TODO task ID：`TDMA-SEAT-03`（条目 15 的台架路径打通）；改动：`tools/sync_trigger/t2_enable.py`、`tools/hardware_acceptance/p3_hardware_acceptance.py`。
- **结果（真机证据）**：`p3_hardware_acceptance.py t2` → `out/HardwareAcceptance/20261011/t2-20261011-190909/report.json`：`passed=true, boards_configured=4, boards_publishing=3`，退出码 0。运行中采样：`T2?` `segments 315/3/0`、`sync_seq` 递增、`published_mask=7`、`announced=4750`、`pulse` 递增；`POSition?` `tx_publish_count=3`；`T2:CURSor?` `err 0/valid 1/session 绑定`；计数器 `loaded 1` 且递增。
- **最后两块拼图（此前所有前序修复都在此之前完成）**：
  1. **`SYSTem:TDMA:LOAD:MASK 91`**：位 6 = `APP_REALTIME_LOAD_SYNC_TRIGGER`（`app.h:19`）。掩码 27 **不含**位 6 时，`app_realtime_run_once()` 根本不调用 `app_realtime_sync_trigger_phase()`，于是 `sync_trigger_service()/trigger_t2_runtime_service()/trigger_time_cursor_service()` 从未运行 —— 表现为 T2 `configured+active` 但 `segments 0/0/0`、游标恒 `NO_SYNC/session 0`、无任何发布。探针正是先设 27、环启动后设 91。
  2. **动作/模板"代"必须每轮递增**：`install_stopped()`（`trigger_action_service.c:100-104`）对 `generation <= last_generation` 只接受"同表同绑定"，而 `clear_stopped()` **刻意保留** `last_generation`；工具原先硬编码 `generation=1` → **首次能装、之后全部被拒**（`T2:CONFigure` 落回 `diag 93`）。改为使用每轮唯一的 `dep_gen` 后四板全部装上。
- **完整起链配方（本次定型，T2 不再依赖 P3）**：见 `out/tdma_t2/t2_calib_replay.json` 的 `bringup.order` 与 `do_not`；要点：停机并关 VDC 会话 → `RING:DIAGnostic 1`（须在拓扑提交后）/`GEOMetry 0`/`TOPology 4,slot,0` 并等提交 → `PROBe 1,1` + `OPMode:STAGe 7`+`APPLy` → 清表 → GRID 配置 → **验证式部署激活** → 装表（代递增）→ 计数器 `UNLoad/LOAD/CLEar/STARt` → T2 配置/启动 → 源与优先级 `SYNC/MATCH/FOLLow` → `ProVisional 1` → **ARM（从板先、参考最后）→ TRAIN → START → `LOAD:MASK 91`** → 源启动 → 原点试运行。**禁止** `CALibration:TRAINing:STAGe:CLEar`（会毁掉标定阶段）。
- **P3 使用策略（owner 裁决）**：物理链路与标定不变时，标定/训练参数以 JSON 复用，T2 迭代**不跑 P3**；仅在提交需要 staged 凭证时跑。本轮 `dpll60`（缩小范围）**PASS**（`dpll60-110200`），`run` 的 DPLL 联合采集阶段两次 `Capture exited 2` 失败（非 DPLL 本体问题）。
- **未闭合**：`t2` 的 teardown 尚未做成"完整停机"（环 STOP 确认/VDC 运行 STOP/会话清零/计数器 UNLoad），连续跑可能从脏状态开始；凭证在工具再次改动后失效，提交前需重跑一次。

### TDMA-PROGRESS-20261011-041：T2 已到"仅差同步样本"；判决字段索引纠正（含验收判据错误）

- TODO task ID：`TDMA-SEAT-03`；改动：`tools/sync_trigger/t2_enable.py`、`tools/hardware_acceptance/p3_hardware_acceptance.py`。
- **本轮新增解锁与纠正**：
  1. **计数器装载**：`TRIGger:REServation:COUNter:LOAD {generation},{crc}` 是必须的一步（我此前从未发送）→ 实测 `loaded 1 state 2 count` 递增。拒绝码解码：`COUNTER_OK=0, ARGUMENT=1, BUSY=2, NOT_IDLE=3, DEPLOYMENT=4, WRONG_ROLE=5, **CHANGED=6**, …, NOT_LOADED=8`；`counter_binding()`（`trigger_counter_service.c:43-61`）要求环**完全停止**、`ring.config_seq == binding.ring_config`、`staged.enabled`、slot/schedule_crc 一致 → 因此 **LOAD 必须紧贴"部署激活"之后**（放在 T2 配置之后会因序号推进而 `CHANGED`）；且每次先 `UNLoad`（上一轮残留的 loaded 会让 LOAD 返回 BUSY）。
  2. **字段索引纠正（重要）**：`T2?` 页（原始切分）—— `2 configured, 3 active, 4 origin, 8 last_sync_sequence, 9 published_mask, 10 received_mask, 13 event_count, 15 frozen_mask, 28 last_counter_count(u64), 30 cursor.pulse_index, 33 announced_mask, 44/45 core0_prepare_calls/preview_saved`，其后为 freeze/ack 诊断字段。此前误把 idx8 当计数器。`POSition?` 页：**idx5 = tx_publish_count**、idx12 = rx_complete_count。
  3. **验收判据本身有错**：`t2` 子命令原先读 `POSition?` 的 idx3（= `tx_active`，空闲恒 0）→ 已改为 idx5/idx12，"publishing 0" 不再可能是误判。
  4. **网格 epoch 与会话**：`GRID:CONFigure` 的 epoch 必须与 `VDC:FEEDback:SESSion` 同值（探针同用一个 `generation`）；已把 session 对齐为 `dep_gen`。
  5. **残留状态陷阱**：若上一轮把环留在 armed，则本轮的 `GRID:CONFigure`/部署配置会被拒 → 游标读到**旧网格 epoch**（实测 `epoch 1`）。**四板软件复位后**再跑，epoch 即等于本轮 dep_gen ✓。→ 工具的 **teardown 需要完整停机**（环 STOP 确认、VDC 运行 STOP、会话清零、计数器 UNLoad），否则下一轮从脏状态开始。
- **当前唯一剩余阻塞**：`TRIGger:REServation:T2:CURSor?` 运行中读数为 **`err 2 = TRIGGER_TIME_CURSOR_NO_SYNC`**、`cfg 1`、`valid 0`、`session 0`、`pulse 0` → 网格已配置，但**没有同步样本进入游标**，于是 `service_origin()` 永不运行（`core0_prepare_calls=0`、`last_counter_count=0`），位置自然不发布。已排除项：四板 `configured/active` ✓、原点试运行获准（`reason=2 ACCEPTED`）✓、计数器已装载且在计数 ✓、网格 epoch 正确 ✓、`VDC:FEEDback:MODel?` 有模型 ✓。
- **下一步**：追同步样本来源（`SYSTem:VDC:PRIORity:SYNC/MATCH/FOLLow` 与环运行状态的配合），使游标从 `NO_SYNC` 转为 `OK`（`pulse_index > 0`），随后 `core0_prepare` 才会产出位置描述符。

### TDMA-PROGRESS-20261011-040：T2 起链推进到"四板 err 0 + 原点试运行获准"，仅余供源/发布计数

- TODO task ID：`TDMA-SEAT-03`；改动：`tools/sync_trigger/t2_enable.py`（把探针 `t2_bench_probe.py` 已验证的完整起链顺序并入）。
- **本轮真机进展（逐级解锁，每级都有固件侧证据）**：
  1. **四板全部 `T2 configured 1 active 1`**（含原点）——动作行语法 + 清表/激活顺序修复生效（TDMA-PROGRESS-039）。
  2. **原点试运行获准**：`CALibration:ORIGin:TRIAL` 返回 grant（42/46/54），停机读 `READ:CALibration:ORIGin:DIAGnostic?` → `reason = 2 (ACCEPTED)`。**此前 reason = 9 `DIAGNOSTIC_REQUIRED`**。
     - 根因：`tdma_service_set_ring_diagnostic()` 要求 **`ring_staged_config.enabled != 0`** 才允许置位，所以 `SYSTem:TDMA:RING:DIAGnostic 1` 必须在**拓扑提交之后**发送（此前发在拓扑之前 → 静默失败 → `RING:DIAGnostic? = 0`）。
  3. **四板 `T2?` 的 `last_error` 全部为 0**（此前 4=COUNTER / 5=CURSOR）。
     - `err 5 = CURSOR` 的根因是缺 **`SYSTem:SYNC:VDC:DPLL:PROVisional 1`**（临时路径延时训练）；`err 4 = COUNTER` 的根因是缺 **计数器源**（`TRIGger:REServation:SOURce:RELease/CONFigure/STARt` + `COUNter:CLEar/STARt`）。
  4. **VDC 会话/运行的顺序**：`FEEDback:SESSion <id>` 只能在输出**停止**时设置（否则 `STOP_REQUIRED_OR_BUSY`），而 T2 游标需要输出运行在线 → 必须 **RUN:STOP → SESSion → RUN <params>** 并校验（工具原先顺序相反，导致会话与运行双双失败）。
  5. **环启动的固件前置**：`tdma_ring_runtime_set_data_enabled()` 要求 `enabled && adapter_started && train_command_seq == train_accepted_seq` → 必须 **ARM（从板先、参考最后）→ 等 armed → TRAIN → 再 START**（与 `calibration_clk_train.py` 一致）；工具原先是 `TOPOLOGY→ARM→START` 连发。
- **仍余一项**：位置发布计数仍为 0。停机测量显示计数器页 `loaded=0`、`io.count32=0`（计数未推进），T2 `counter_count`/`announced_mask`/`cursor_pulse` 均为 0 → **计数源未真正喂数**（转台模型/软件源到计数器这一段），以及"验收判据读的 `POSition?` 字段是否就是发布计数"需要复核（可能是读错字段）。
- **台架与凭证**：本轮先做了**四板软件复位**（`SYSTem:BOOT:RESet`，复用 `reset_acceptance_boards`），随后 `p3_hardware_acceptance.py run` **PASS_WITH_WARNINGS（0 ERROR/0 FATAL）**并写出新凭证；但**之后我又改了 `tools/sync_trigger/t2_enable.py`**，所以该凭证已再次失效，提交前需重跑。
- **未闭合**：计数器供数链路（模型/软件源 → 计数器）；发布计数判据复核；凭证重跑。

### TDMA-PROGRESS-20261011-039：T2 起链打通（四板全部 configured/active，含原点）——6 处顺序/语法缺陷

- TODO task ID：`TDMA-SEAT-03`；改动：`tools/sync_trigger/t2_enable.py`（起链顺序与行语法）、`tools/hardware_acceptance/p3_hardware_acceptance.py`（`t2` 真断言）。
- **成果（真机证据）**：修复后 `t2_enable.py --full` 使**四块板全部 `T2 configured 1 active 1`**（此前原点永远 `configured 0`，诊断码 93）。
- **逐条根因（都由固件自带诊断码/状态页证实）**：
  1. 停机块缺 `T2:STOP`：残留 active 的 T2 使所有停止态配置命令**无应答**（被匹配器显示为超时）。
  2. **VDC 反馈会话未关**：`vdc_dpll_manager_set_tdma_ring_topology()` 明确拒绝"会话活跃"时的拓扑设置（`if (vdc_dpll_manager_feedback_session()) return false;`）→ 先 `SYSTem:VDC:FEEDback:SESSion 0` 并校验归零。
  3. 拓扑**提交是异步的**：`RING:TOPology`（停态）后必须等 `RING:STATus?` 的 `config_seq == applied_config_seq` 且已推进（校准流程的 `_wait_ring_state(after_config_seq=…)` 同义）。
  4. `bind_local()` 在**激活时把 `ring.config_seq` 快照进部署绑定**，而**清表/配置类命令都会推进该序号** → 清表必须放在**激活之前**，否则 `current_binding()` 判据 44 失败 → `ACTion:INSTall` 返回 **35 `TRIGGER_ACTION_SERVICE_CHANGED`**。
  5. 部署激活后**自校验** `binding.ring_config == 当前 config_seq`，不一致就重新等待并重试（本次实测 92/92、97/97、101/101 全部一致）。
  6. **动作行必须符合固件语法**（`trigger_action_table.h:139-149`）：`kind` 只有 `SWITCH_CODE(1)`（限 SWITCH_A/SWITCH_B 角色）与 `GATEWAY_PULSE(2)`（限 GATEWAY 角色）；**COUNTER 角色（NO1 原点）没有自己的动作种类**，改为携带一条合法 GATEWAY 行。这是原点能过 `T2:CONFigure` 的关键（此前 `kind=0` → `TRIGGER_ACTION_TABLE_ROLE`）。
- **仍未通（位置发布）**：源/时基侧。`RING:START`（数据使能）要求 `enabled && adapter_started && train_command_seq == train_accepted_seq`（`tdma_ring_runtime_set_data_enabled`），即必须先用 **`RING:TRAin {cycles}`**；且计数器源需要 `TRIGger:REServation:SOURce:RELease/CONFigure/STARt`、快速通道需要 `SYSTem:VDC:PRIORity:SYNC/MATCH/FOLLow`、转台需要 `CALibration:ORIGin:TRIAL`（需环在跑）。**已验证配方在 `tools/sync_trigger/t2_bench_probe.py`**（早前成功发布位置用的就是它）。
- **台架遗留状态**：四板环已停；三块板 VDC 会话已清零；**NO.1 的 VDC 输出仍处于运行态（`RUN?` 状态 7，其余板为 6）**——来自我手工探测时的 `VDC:OUTPut:RUN`，会让 `FEEDback:SESSion <id>` 因 `STOP_REQUIRED_OR_BUSY` 被拒；需先彻底停掉（或按 `run`/`dpll60` 的做法先做四板软件复位）。
- **未闭合**：把探针里已验证的"源+优先级同步+TRAin+START"序列并入 `t2_enable.py`，使 `p3_hardware_acceptance.py t2` 判据 `configured==4 && publishing>0` 通过；P3 凭证因本轮工具改动失效，提交前需重跑。

### TDMA-PROGRESS-20261011-038：T2 真机链路排查——**原"PASS"是假通过**；4 处工具缺陷已修，剩余 1 处台架协议阻塞

- TODO task ID：`TDMA-SEAT-03` / T2 冻结闭环真机验证；改动：`tools/sync_trigger/t2_enable.py`（补 `T2:STOP`+停机校验、原点也装动作表、清表后安装、打印安装应答、按 `--nodes` 推导同伴掩码）、`tools/hardware_acceptance/p3_hardware_acceptance.py`（`t2` 子命令改为**真断言**：`configured==4 && publishing>0`，否则退出 1）。
- **结论先行**：`p3_hardware_acceptance.py t2` 此前**从不检验判据**，在"原点未配置、四板位置发布数全 0"的情况下也退出 0 —— 那是**假 PASS**。现在它会明确报 `boards configured 3/4, publishing 0, passed=False` 并退出 1。
- **已修复的 4 处工具缺陷**（每处都有台架证据）：
  1. `run_full` 初始停机块**没有 `T2:STOP`**：上一跑残留的 active T2 会让所有停止态配置命令被拒（表现为"无应答"）→ 已补 `T2:STOP` 并轮询 `T2?` 校验已停（否则打印 WARNING）。
  2. **原点从不安装动作表**（`if slot:` 跳过 slot 0），而 `T2:CONFigure` 在**每块板**都要求 `templates.installed/resources_loaded/binding_current` → 原点被拒（诊断码 **93**）→ 已改为**所有板**都安装动作表（原点不执行自身位置，但同样要过这道门）。
  3. 安装命令**报错被应答匹配器当成"无应答"**（显示 `<timeout>`），失败原因被吞掉 → 已打印 `ACTion/TEMPlate` 的安装/激活应答与安装后的状态页。
  4. 安装**变更式**动作表会被按"代"拒绝（`install_stopped` 的 generation 规则）→ 已先 `TEMPLATE:RELease` + `ACTion:CLEar` + `TEMPLate:CLEar` 并校验服务为空。
- **剩余阻塞（台架协议，非固件缺陷）**：`ACTion:INSTall` 返回 **35 = `TRIGGER_ACTION_SERVICE_CHANGED`**。查 `trigger_action_service.c:92-99`：该码在 **`current_binding()` 失败**时设置；而 `current_binding()`（`:58-77`）要求**本板已 staged 的环配置与 active 部署的 `ring_config` 一致、`schedule_crc`/`local_slot`/uid 一致、且环处于停止态**（其自带诊断码：41 环在跑、42 `config_seq != applied`、43 staged 未使能、**44 `ring_config` 不一致**、45 uid、46 local_slot、47 schedule_crc、48 crc）。`t2_enable.py` 每跑都**重新 stage 一份新部署 gen/crc，却不把环配置同步过去**，因此绑定不 current → 动作/模板链无法安装 → 原点无法 `T2:CONFigure` → 四板位置发布数始终为 0。
- **台架当前状态（我留下的）**：四板动作/模板表**已被清空**（原先是旧会话留下的表），diag 分别为 135/135/114/135；需要一条**能成功安装**的链路才能恢复。
- **已验证的固件侧修复（尚未烧录到板子）**：FREEZE 完成只等同伴（`peers = target_mask & ~(1u<<source_slot)`）+ `freeze_ack_own_in_mask` 标志 + `freeze_ack_stale/incomplete` 判据（`TDMA-PROGRESS-20261011-037`）；主机 `t2_enable.py` 的同伴掩码推导同样在此次改动内。**要真机验证必须重新构建烧录**（`dpll60` 或 `run`）。
- **未闭合**：环配置 ↔ 部署绑定的台架同步（阻塞 T2 起链）；FREEZE 修复的真机验证；P3 凭证因本轮工具改动而失效，提交前需重跑。

### TDMA-PROGRESS-20261011-037：**FREEZE 多次重发已定位并修复**（等待集合误含位置来源槽位自身）

- TODO task ID：`TDMA-SEAT-03` / T2 冻结闭环；改动：`trigger_t2_runtime.c`（只等同伴 + 配置期计数 149）、`trigger_t2_runtime.h`（`freeze_ack_own_in_mask`）、`scpi_trigger_t2_runtime_commands.inc`（页末位追加）、`tools/sync_trigger/t2_enable.py`（按 `--nodes` 推导同伴掩码，原先下发 `0x0F` 含槽 0）、`tools/sync_trigger/t2_bench_probe.py`、`tests/python/test_trigger_t2_freeze_abandon.py`（新增 peer-only 用例）、文档。
- **根因（机制性，不是链路丢失）**：冻结完成判定 `distributed_refmem_position_freeze_acked()` 是**全有或全无**——掩码内**任一**槽位上报的 `opcode != T2_FROZEN | (run_id & 0x0F)` 就整体返回 0；而确认槽位**只能由"收到的镜像"填充**。因此当 `target_mask` 含**位置来源槽位自身**（`source_slot`）时，该位**永远不可能被确认** → 每个位置始终保持"未完成" → 原点按 `T2_FREEZE_RESEND_SLICES`（4 片）**一直重发到位置窗口关闭**（此前只留下一个巨大的 `freeze_resend_count`，然后放弃/或继续下一轮）。
- **配置侧证据**：主机两个工具下发不一致——`t2_bench_probe.py` 发 `…,0,14,…`（`0x0E`，不含槽 0）✅；`t2_enable.py` 发 `…,0,15,…`（`0x0F`，**含槽 0**）❌。用后者启用 T2 即命中该缺陷。
- **修复**：原点**只等同伴** —— `peers = target_mask & ~(1u << source_slot)`；自身位被忽略并置 `freeze_ack_own_in_mask`（`T2?` 末位公开，作为"这类重发风暴"的识别标志）；**无同伴**（单板台架）按构造视为完成，不产生任何重发；配置期若发现掩码含自身槽位，记 `tdma_seat_mount_store_note_reject(149u)`（不静默、也不 fail-closed 破坏台架）。`t2_enable.py` 改为按 `--nodes` 推导同伴掩码（4 板 → `0x0E`，2 板 → `0x02`，单板 → 0 并打印说明）。
- **测试（证明"一次飞行即完成"）**：抽取式 harness 新增三种情形——① 掩码含自身槽位：断言**只以同伴掩码 0x4 询问**、`freeze_ack_own_in_mask=1`、位置**当轮完成**且 `freeze_resend_count == 0`；② 无同伴：断言不问确认、直接完成、无重发、pending 掩码归零；③ 常规 `0x0E`：断言行为不变（`own=0`、询问掩码仍为 `0x0E`）。相关用例 **55 passed**；设备语法守卫 8/8；设备构建两槽链接 + `flash_link_contract=OK`。
- **随之而来的必办项**：本次改动了 `components/tools/tests` → 现有 P3 凭证**已失效**，需在提交前重跑 P3；并在台架上用新字段确认重发数回落到"每位置 ≤1 次"。
- **未闭合**：P3 重跑与真机确认（建议 `dpll60` + `t2` + 运行中只读采集）。

### TDMA-PROGRESS-20261011-036：DPLL60 真机验收 **PASS**（凭证与当前工作区指纹一致）

- TODO task ID：`TDMA-SEAT-03`（条目 15 的真机路径打通第一步）；改动：仅文档（`docs/**` 不计入 P3 源码指纹）。
- **结果**：`python tools/hardware_acceptance/p3_hardware_acceptance.py dpll60` → **PASS**（退出码 0）：
  - `out/HardwareAcceptance/20261011/dpll60-014621/`（含 `build.log`、`ota-four-board`、`tdma.log`、`tdma-stopped-handoff.json`、`dpll-scope-discovery.json`、`dpll-short-summary.json`、`acceptance.json`、`provisional-receipt.json`）；
  - `config/hardware_acceptance/p3_acceptance_receipt.json`：schema `HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_DPLL60_V1`、`passed=true`、`source_tree_sha256=2094b9da…`、`source_file_count=1786` —— **与当前工作区指纹逐字一致**；四板 UID 齐全（`0010071E65B5CB38`/`FB276192BEF9CCE1`/`2BD5090FE009FA2A`/`A1E54920…`）；`calibration_generation=1791682738`。
  - `build.log` 显示本次**真实构建当前源码**（`cmake --build out/build/dpll60-latest`，142 目标）；`build_id=20261010050916` 是固件侧构建戳（非本机墙钟），凭证以指纹绑定为准。
- **上一轮 `run` 失败的真因（已闭环）**：四板 DPLL 阶段报 `provisional receipt source fingerprint differs`，根因是**我在 P3 运行期间继续编辑工作区**——`working_source_fingerprint()` 取**工作区**中 acceptance 源文件的哈希，编辑即失效。**规则（本轮确立）：P3 运行期间冻结工作区**；`docs/**` 不参与指纹，可在运行前后自由编辑。
- **本轮同时交付（代码，已在冻结前完成并验证）**：FREEZE 重发风暴的**可判别证据**——`distributed_refmem_position_ack_pending()` 输出 `freeze_ack_stale_mask`（报的 opcode 不是本 run：run id 不匹配或从未上报）与 `freeze_ack_incomplete_mask`（无人冻结的位置位），`T2?` 末位公开。判据：重发多且 `stale != 0` → 确认链（反向）问题；`stale == 0` 且 `incomplete != 0` → 正向送达问题；两者皆 0 仍重发 → 反向确认延迟（节奏 `T2_FREEZE_RESEND_SLICES` 短于确认回路）。
- **本跑覆盖与未覆盖**：DPLL60 驱动的是**特等席/DPLL 路径 + 四板 TDMA 环**（含 `tdma.log` 与停止交接证据），并绑定当前源码；**一等席（T2 position）仍需要自己的 `p3_hardware_acceptance.py t2` 路径**，或一次"运行中只读采集"（`SEAT:HEALth?` 要求通道已启动）。
- **验证**：`dpll60` PASS；`test_t2_freeze_completion`/`test_trigger_t2_freeze_abandon`/`test_trigger_t2_runtime`/`test_seat_acceptance_tool`/`test_tdma_seat_invariants` **58 passed**；设备语法守卫 8/8；设备构建两槽链接 + `flash_link_contract=OK`；文档门禁 OK。

### TDMA-PROGRESS-20261011-035：FREEZE 重发风暴可诊断（谁在对不上 / 谁还没冻结）+ DPLL60 失败归因

- TODO task ID：`TDMA-SEAT-03`；改动：`distributed_refmem_position_ack.inc`（新增 `distributed_refmem_position_ack_pending()`）、`distributed_refmem.h`、`trigger_t2_runtime.h/.c`（两个快照字段 + 在 `resend_one_freeze` 内刷新）、`scpi_trigger_t2_runtime_commands.inc`（`T2?` 末位追加）、`tools/sync_trigger/t2_bench_probe.py`、`test_t2_freeze_completion.py`、`test_trigger_t2_freeze_abandon.py`、`test_trigger_t2_runtime.py`（补桩）。
- **背景（owner 提问）**：FREEZE 之后从板经一次飞行本应回状态，异常时一次重发应足够；实测重发次数远超该量级。核对确认链后确认**结构性原因**：`distributed_refmem_position_freeze_acked()` 是**全有或全无**——`opcode = T2_FROZEN | (run_id & 0x0F)`，**任一目标板报的 opcode 不是本 run**（绑定别的 run、或从未上报）就 `return 0`，于是**所有位置都变"未确认"**，原点只能一直重发；再叠加"单深度描述符邮箱 → 每轮只发一个位置 + 4 片节奏 + 反向确认延迟"，每位置约需数轮。
- **本轮交付（可判别证据）**：新增 `distributed_refmem_position_ack_pending()`，按目标槽位输出两组事实——`stale_mask`（**报的 opcode 不是本 run**：run id 不匹配或从未上报）与 `incomplete_mask`（**没人冻结的位置位**，仅在"所有目标都在本 run 上说话"时有定义）。二者写入 T2 快照并在 `TRIGger:RESERVation:T2?` 末位追加（`freeze_ack_stale_mask`/`freeze_ack_incomplete_mask`）。**判据**：若重发多而 `stale_mask != 0` → 问题在**确认链的 run id/上报**（反向）；若 `stale_mask == 0` 而 `incomplete_mask != 0` → 问题在**正向送达**（从板没收到/版本对不上）；二者皆 0 却仍在重发 → 只可能是**反向确认延迟**（节奏 4 片短于确认回路），调 `T2_FREEZE_RESEND_SLICES` 即可。
- **测试**：refmem 侧断言重置后的三种状态（无人上报 → 全 stale、位置缺失集未知；某板报本 run 且冻结位 0x5 → stale=另一槽、incomplete=0xFA；另一槽报**别的 run** → 保持 stale 且完成判定为 0）；T2 侧抽取 harness 断言两个掩码被刷新进快照、空闲时归零。
- **DPLL60 失败归因（重要，是我的操作错误）**：P3 `run` 在"四板 DPLL 60 s"阶段报 `provisional receipt source fingerprint differs`。根因：`config/hardware_acceptance/p3_acceptance_receipt.json`（schema `HAOFV_P3_JOINT_PROVISIONAL_V1`）是本次 P3 在 DPLL **之前**写下的，而**我在 P3 运行期间继续编辑工作区**，`working_source_fingerprint()` 取的是**工作区**中 acceptance 源文件的哈希（当前 `856dd93e…` vs 凭证 `c3d38317…`，同为 1786 文件）→ DPLL 阶段校验失败。**教训（写入本条目，避免重犯）：P3 运行期间必须冻结工作区，任何源码/工具编辑都会使凭证失效。** 处置：先把所有待改代码改完并验证 → 再跑 `p3_hardware_acceptance.py dpll60`（owner 指示直接跑 DPLL60）。
- **验证**：`test_t2_freeze_completion`/`test_trigger_t2_freeze_abandon`/`test_trigger_t2_runtime`/`test_seat_acceptance_tool`/`test_tdma_seat_invariants` **58 passed**；设备语法守卫 8/8 PASS；设备构建两槽链接 + `flash_link_contract=OK`。
- **未闭合**：DPLL60 重跑结果；P3 `run` 全量凭证；正/反向与节奏三项判据需真机采集判定。

### TDMA-PROGRESS-20261011-034：owner 两条定案（通道唯一分时复用 / M:N 即席位调度）+ P3 已授权开跑

- TODO task ID：`TDMA-SEAT-03`（关闭 A' 待决项）；改动：`TDMA_SEAT_TX_CHANNEL_REVIEW.md`（作废横幅）、`TDMA_SEAT_ACCEPTANCE_REVIEW.md`（条目 12/14/19、边界说明、待决 1/2）、`TDMA_RUNTIME_CONSTRAINTS.md`（门控段 + 准入段）、`TDMA_DOMAIN_TODO.md`、`docs/README.md`、`docs/interface/SCPI_COMMANDS.md`、`distributed_refmem_position_ack.inc`（注释）。
- **定案一（通道唯一、两席分时复用）**：快速通道**只有一条**，特等席与一等席**分时复用**；**特等席同样没有独立 TX**。因此 A（一等专属通道）**不再是目标**，A″/B 方案作废，`TDMA_SEAT_TX_CHANNEL_REVIEW.md` 降级为决策过程记录；共用通道下**交付关键字节保护**（`distributed_refmem_position_ack_control_available()` 准入 + `lane_diag_refused_count`/`lane_ack_override_count`）是**长期机制**而非过渡——代码注释同步订正（两处 "A' transition"）。
- **定案二（M:N 即席位调度）**：**M:N 轮换（`TDMA-SEAT-01`）就是席位调度**；`TDMA_LOOP_SCHEDULE_ENABLED` 的 loop-plan 窗口门控**不在设计内**，保持 **0**，其代码与饥饿计数保留为**休眠代码 + 构建守卫/行为用例**，不再列为"待启用"。验收矩阵条目 12 相应改写为"非设计机制"，条目 14 改写为"**共用快速通道的分时复用**（不存在通道独立要求）"，条目 19 明确为"**M:N 席位调度本身**"；待决事项 1/2 关闭。
- **P3 已开跑（owner 授权）**：`p3_hardware_acceptance.py run`（默认 quick：构建 → OTA 四板 → P0–P3/TRN-03 → TDMA → 四板 DPLL 60 s）。四块板在位（COM3–COM6）。已观察到 **构建完成**（`out/build/p3-acceptance-20261011-093147`，即含本工作区的波形缩容 334）与 **OTA 四板 build=20261011013200**。
- **真机席位证据的路径（重要）**：本次 `run` 驱动的是**特等席路径 + 通用环流量**；**一等席有独立 P3 路径 `p3_hardware_acceptance.py t2`**。故条目 15 需要 `run` + `t2` 两跑，再由只读判定工具 `seat_acceptance.py` 采 `SEAT:HEALth?`/`COUNters?`/`ROTation?` 得 `ok=1, reason_mask=0`。
- **RAM**：owner 明确"buffer 可以继续缩小"。当前无需更多 RAM（两槽链接、余 2108 B），故**暂不再缩**；若后续改动需要余量，按此授权直接动波形缓冲，不再回问。
- **未闭合**：P3 `run`/`t2` 结果与凭证；波形段最终容量是否回升（产品侧）。

### TDMA-PROGRESS-20261011-033：G5 关闭——冻结放弃分支改用抽取式 harness 驱动（含负测）

- TODO task ID：`TDMA-SEAT-03`；改动：新增 `tests/python/test_trigger_t2_freeze_abandon.py`、文档（矩阵 G5 行、T2 架构说明）。
- **背景**：`-024` 新增的 `freeze_abandoned_count` 此前**没有任何测试产生过它**（G5）。三轮整机驱动均失败，原因已查明并记录：① 原点持续重解算会不断延长窗口，放弃分支永不进入；② 冻结采样流只推进时间会让运行时先报无关服务错误，FREEZE 根本发不出。
- **本轮解法（换工具，不硬撞）**：该分支本身是一段**自持**的小判定（只看 `s_state`/`s_config`/两个节奏 static + 两个 extern）。按仓库既有的**抽取式 harness** 手法，把 `resend_one_freeze()` 本体抽出来，用同名自持 statics 与两个 extern 桩（冻结确认、暂存描述符）直接驱动，覆盖六种情形：
  1. 无承诺 → 无动作、无计数；
  2. 窗口已过且未被确认 → **掩码置位 + 计数 +1 且不再暂存**（即 G5 的正面证据）；
  3. 计数**饱和**于 `UINT32_MAX` 不回绕；
  4. 仍在窗口内 → **重发一次**（阶段/位置/版本、`freeze_resend_count`、节奏游标与 `freeze_last_slice` 均断言）；
  5. **节奏门**：距上次重发不足 `T2_FREEZE_RESEND_SLICES` 时不再暂存；
  6. 已确认 → 置 `freeze_complete_mask` 且**不**计放弃；暂存被拒 → 返回 false 且只记传输事实与 `last_error`。
  宏值 `T2_FREEZE_RESEND_SLICES` 由**源码正则提取**后注入 harness，避免副本漂移。
- **负测（证明断言有效）**：临时移除 `++s_state.freeze_abandoned_count` 后该用例**立即失败**（`freeze_abandoned_count == 1` 不成立），随后已恢复源码并复验（`freeze_abandoned_count != UINT32_MAX` 位于 `trigger_t2_runtime.c:156`）。
- **验证**：新用例 + T2 运行时用例 **27 passed**；设备语法守卫 8/8；文档门禁 OK。
- **消费侧现状**：G1/G2/G4/G5/G6/G7/G8 全部**关闭或已归因**，G3 属 T2 域自身待办（`descriptor_window_sync` 未运行时可配）。**无剩余自主可做的消费侧项**。
- **仍未闭合（席位本体）**：A'（独立 TX 通道，验收条目 14）待评审；台架/P3（条目 15）待授权；波形段容量与"迟到是否要判定位"待确认。

### TDMA-PROGRESS-20261011-032：承诺本身可读（G8 关闭）+ 自查确认控制字节仲裁已有字节级断言

- TODO task ID：`TDMA-SEAT-03`；改动：`scpi_trigger_t2_runtime_commands.inc`（`T2?` 逐位置追加承诺）、`tools/sync_trigger/t2_bench_probe.py`、`tests/python/test_seat_acceptance_tool.py`（末位追加绑定扩充）、文档。
- **G8 关闭**：上一轮让"迟到量级"可读，本轮把**承诺本身**也公开——`T2?` 末尾逐位置追加 `descriptors[i].target_ms_index`（低字，消费侧正是按低字准入）与 `descriptors[i].expiry_sync`（3 个位置共 6 字段）。**没有承诺作参照，迟到计数不构成证据**；这与条目 20（席位声明预算可读）同一性质。末位追加的位置契约由 `test_t2_page_tail_is_append_only` 绑定。
- **自查：本轮先审"我自己新增的每条事实是否都有人驱动"**，结论是仅 `freeze_abandoned_count`（G5）没有；其余都有生产者测试。顺带确认一条**安全关键**性质已有**字节级**断言（此前只从计数层面看过）：`test_t2_freeze_completion.py:192-231` 断言共享控制字节的**最终内容**必须是一等冻结事实（`0xE0 | run 低半字节` 与冻结掩码），覆盖两条路径——诊断先写后被覆盖（`lane_ack_override_count`）与准入事先拒绝诊断（`lane_diag_refused_count`）。即 A' 替代机制的"诊断不得污染交付关键字节"是被真字节证明的，而不只是计数一致。
- **验证**：设备语法守卫 **8/8 PASS**；设备构建两槽链接 + `flash_link_contract=OK`；`test_seat_acceptance_tool.py` + T2 运行时用例 **53 passed**；文档门禁 OK。
- **仍未闭合**：G5（冻结放弃路径三轮未攻克，`freeze_abandoned_count` 生产者未被测试驱动）；A'（独立 TX 通道）待评审；台架/P3 未执行；波形段容量与"迟到是否要判定位"待确认。

### TDMA-PROGRESS-20261011-031：迟到量级可读（G8 部分关闭）+ 修正 `origin_no_ack` 误配为 follower + G5 第三次尝试仍失败

- TODO task ID：`TDMA-SEAT-03`；改动：`trigger_t2_runtime.h/.c`（`late_last_current_ms`/`late_last_expiry_sync`）、`scpi_trigger_t2_runtime_commands.inc`（`T2?` 再追加两字段）、`tools/sync_trigger/t2_bench_probe.py`、`tests/python/test_trigger_t2_runtime.py`、文档。
- **交付（G8 部分关闭）**：迟到此前的只有计数，没有**量级**。现在到达路径记录最近一次迟到的两个端点——消费侧粗序号 `late_last_current_ms` 与被错过的承诺窗口 `late_last_expiry_sync`（二者之差即迟到量），并在 `TRIGger:RESERVation:T2?` **末位**追加。逐位置的承诺窗口仍未公开（矩阵已记明）。
- **顺带修正一处真实缺陷（测试自身）**：`origin_no_ack` 用例从一开始就被 `config(...)` 的 case 名列表漏掉，实际以 **follower** 身份运行——所以该用例此前既不产生 FREEZE，也不曾覆盖它声称覆盖的路径。本轮把它加入 origin 列表后，用例才真正以原点身份跑 400 片（`freeze_complete_mask == 0` + 计数/掩码一致），强度高于此前。
- **G5 第三次尝试（仍失败，如实记录）**：为驱动"冻结放弃"分支做了两种尝试——① 冻结采样流只推进时间：`trigger_t2_runtime_service()` 报服务错误（首个错误码 5），FREEZE 根本没发出；② 让原点保持重解算：窗口被持续延长，分支永不进入。故 `freeze_abandoned_count` 的**生产者路径仍未被测试驱动**（其存在性由代码审查与计数/掩码一致性间接支持）。矩阵 G5 行已更新为"三轮未攻克"。
- **验证**：T2 运行时用例 **26 passed**（含迟到量级断言：`late_last_expiry_sync != 0` 且 `late_last_current_ms > late_last_expiry_sync`）；设备语法守卫 **8/8 PASS**；设备构建两槽链接 + `flash_link_contract=OK`；文档门禁 OK。
- **未闭合（消费侧）**：G5（放弃路径未驱动）、G8 的逐位置承诺窗口。
- **未闭合（席位本体）**：A'（独立 TX 通道）待评审；台架/P3 未执行；波形段容量待确认。

### TDMA-PROGRESS-20261011-030：迟到到达独立归因（消费侧 G1 关闭）+ 明确 G2 已由既有事实覆盖

- TODO task ID：`TDMA-SEAT-03`；改动：`trigger_t2_runtime.c`（到达准入拆分出迟到归因）、`trigger_t2_runtime.h`（`late_arrival_count`）、`scpi_trigger_t2_runtime_commands.inc`（`T2?` 末位追加）、`tools/sync_trigger/t2_bench_probe.py`、`tests/python/test_trigger_t2_runtime.py`（新增 `late_arrival` 用例 + 扩 `expired` 用例）、文档。
- **先纠正一条审计结论**：上轮审计把 G2 描述为"`s_pending_mask` 从不扫描窗口关闭未服务"。核对源码后，`s_pending_mask` 是**待发送 FREEZE 队列**，不是"期待到达集合"——消费侧"从未到达"实际由 `freeze_missing_local_count`（被要求冻结一个自己从未拥有的位置）覆盖，且已在 `-029` 并入判定页 bit8。**因此 G2 不是缺口**，本轮不再重复实现，避免制造第二个语义重叠的计数。
- **本轮实现 G1（真实缺口）**：迟到/过期到达原先与 10 种无关原因共用 `descriptor_reject_count`，无法回答"一等席有多少次**迟到但送达**"。现在把到达准入按原因拆开：
  - 身份/网格/部署类不符 → 仍只计 `descriptor_reject_count`；
  - **记录合法、寻址正确，但抵达时承诺窗口已关** → 额外计 `late_arrival_count`（聚合计数**不变**，既有断言与语义不破坏）；
  - **目标溢出（`expiry_sync == 0`，从未承诺窗口）明确不计入迟到**——"无窗口"与"错过窗口"必须分开（`expired` 用例新增断言把这条钉住）。
  - `valid_from_sync` 在消费侧被强制为 0，故窗口的另一侧（过早）在构造上不可达，已在代码注释中说明。
- **测试**：新增 `late_arrival` 用例（推进游标越窗后投递**新版本**记录：断言 `TRIGGER_T2_RUNTIME_DESCRIPTOR`、`late_arrival_count == 1`、聚合计数 +1、且**迟到记录没有覆盖已接收状态**）；`expired` 用例补"无窗口不计迟到"。T2 运行时用例 **26 passed**。
  - 过程记录：首版用例在派发前已被 harness 投递过**同版本**记录，命中"同版本直接接受"短路，导致看似"迟到被接受"；改用新版本后定位并修正，非实现缺陷。
- **验证**：设备语法守卫 **7/7 PASS**；设备构建两槽链接 + `flash_link_contract=OK`；T2 用例 26 passed；文档门禁 OK。
- **仍未闭合（消费侧）**：G5（冻结放弃路径仍未被 harness 驱动）、G8（承诺期限字段 `target_ms_index`/`expiry_sync` 不可读，故迟到的**量级**不可见）。另新增一个**语义待决**：迟到是否需要自己的判定位（bit9），见[验收矩阵](TDMA_SEAT_ACCEPTANCE_REVIEW.md)待决事项 5。
- **未闭合（席位本体）**：A'（独立 TX 通道）待评审；台架/P3 未执行；波形段容量待确认。

### TDMA-PROGRESS-20261011-029：消灭判定页"假健康"——消费侧端到端丢失进入 bit8

- TODO task ID：`TDMA-SEAT-03`（验收条目 21，关闭上轮缺口 G6）；改动：`tdma_pio_spi_ring_adapter.h`（新增 reason 位 8 + facts 字段 + `MASK_ALL` 扩为 9 位）、`scpi_system_snapshot_commands.c`（判定页读 T2 快照并把两个消费侧丢失事实折叠为 bit8，页尾追加两个原始计数）、`tools/sync_trigger/seat_acceptance.py`（位 8 + 证据改为**多计数**模型 + 期望字段 24）、`tests/python/test_seat_acceptance_tool.py`、`tests/unit/test_tdma_pio_spi_ring_adapter.c`、文档。
- **问题（上轮独立审计确认）**：`SEAT:HEALth?` 的前 8 个位只证明"席位对**看到过的**记录做了什么"；一等位置片**从未到达**时消费侧没有任何事实，判定页仍会报 `ok=1`——**假健康**。
- **修法**：新增 `TDMA_SEAT_HEALTH_REASON_FIRST_CLASS_MISSED (1u << 8)`，由两个**端到端**事实合成（饱和相加）：
  1. `freeze_abandoned_count`——原点在位置窗口关闭后放弃完成该位置的 FREEZE（上一轮新增的计数）；
  2. `freeze_missing_local_count`——从板被要求冻结一个自己从未拥有的位置（既有计数）。
  判定页在**任一**事实非零时置位；两个原始计数按**末位追加**方式公开（读者下标不变，页 22 → 24 字段）。**读不到 T2 快照时返回错误而不是报健康**——沿用"宁无数据，不给看似健康的 0"。
- **模型细化**：一位对应两个原始事实，验收工具原先"一位一计数"的接线检查改为**多计数**（任一非零即满足），并新增位 8 的自检/参数化用例；字段数仍由**固件源码反推**并与工具常量比对（本轮自动校验到 24）。
- **验证**：适配器 host 单测（含 bit8 单置位与全清断言）通过；`test_seat_acceptance_tool.py` **26 passed**；设备语法守卫 **7/7 PASS**；设备构建两槽链接 + `flash_link_contract=OK`；`docs_check`（178 文件）/`doc_regression` OK。
- **仍缺（消费侧）**：G1（迟到/过期与 11 种拒绝共用计数）、G2（从未到达的**独立**位置级计数）、G5（放弃路径仍未在 harness 中驱动）、G8（承诺期限字段不可读）。这些是 T2 域后续项，已列在[验收矩阵](TDMA_SEAT_ACCEPTANCE_REVIEW.md)「一之二」节。
- **未闭合**：A'（独立 TX 通道）待评审；台架/P3 未执行；波形段容量待确认。

### TDMA-PROGRESS-20261011-028：与特等席的机制对账（两份独立取证审计）+ 采集自解释 + 消费侧缺口清单

- TODO task ID：`TDMA-SEAT-03`；改动：`scpi_system_snapshot_commands.c`（`SEAT:ROTation?` 扩为 11 字段：配置 + 生效比例 + **声明上限** + 门控开关）、`tools/sync_trigger/seat_acceptance.py`（读该页并做"饥饿必须有窗口门控"一致性检查）、`tests/python/test_seat_acceptance_tool.py`、`trigger_t2_runtime.h/.c`（新增 `freeze_abandoned_count`）、`scpi_trigger_t2_runtime_commands.inc`（`T2?` 末位追加）、`tools/sync_trigger/t2_bench_probe.py`、`tests/python/test_trigger_t2_runtime.py`、`tdma_priority_tx.h`（陈旧契约注释）、`TDMA_SEAT_ACCEPTANCE_REVIEW.md`（字段数漂移）、文档。
- **做法**：用两份**独立取证审计**回答"一等席是否达到特等席水平"——一份清点特等席的稳定性机制（TX 快路径、双缓冲发布原子性、4 槽接收环/seqlock/epoch、IRQ 准入与看门狗面包屑、SD 级预算、消费侧 VDC 证据链），一份审计**消费侧**（T2 运行时）对一等席丢失的可观测性。
- **对账结论（一等席侧）**：准入/仲裁、完整性核对、有界接收保全、失败归因、两条声明预算 + 越界计数、判定页均**与特等席同级**；特等席更强的是（a）专属 typed body 与独立物理路径（= A'，仍未决）、（b）**多页诊断面**（`PRIORity?`/`:BUDget?`/`:TIMing?`/`:RECord?`/`:CONSumed?` + VDC 侧 `TX?`/`RX?`/`TRACE*`），（c）看门狗阶段面包屑（13 段）与 4 段时序自证。**本轮补上 (b) 中最关键的一条**：一等席此前只有计数、没有声明上限，采集无法自解释——现 `SEAT:ROTation?` 给出 M:N 生效值与上限、等待预算、队列深度/重试预算，以及 **`loop_schedule_enabled`**（否则"饥饿为 0"会被误读为已验证，实际只是门控未编译进来）。
- **消费侧缺口（重要，未关闭）**：一等席记录"**从未到达**"在消费侧**没有任何可观测事实**——迟到/过期到达被并入 `descriptor_reject_count`（与 11 种无关拒绝共用），从未到达则完全无痕；`SEAT:HEALth?` 的 8 个 reason 位**没有**任何一位由 T2 侧事实驱动，因此一次丢失的一等交付可以仍报 `ok=1`（**假健康**）。本轮先补最小可计数事实：冻结完成"放弃"此前只有按位置位掩码（下一轮 run 会清掉）、无法回答"发生过多少次"，现新增饱和计数 `freeze_abandoned_count` 并在 `TRIGger:REServation:T2?` **末位**追加。
- **诚实记录**：我试图在 T2 harness 里**驱动**放弃路径（默认窗口 + 400 片不 ack），未能进入该分支——原点的持续解算会刷新窗口；故该用例改为断言**计数与掩码一致**（`popcount(mask) == count`，每次 service 后校验；已确认场景再断言计数为 0），并把"放弃路径仍未被驱动"记为**已知缺口**，不假装覆盖。
- **顺带修正的两处漂移**：① `TDMA_SEAT_ACCEPTANCE_REVIEW.md` 里旧字段数 `22/12/30` → `22/12/11/31`（我自己上一轮漏改）；② `tdma_priority_tx.h` 注释声称 `EMPTY` "reserves the slot"，而实现、旋转用例与运行约束文档都断言 `EMPTY` **不保留**（陈旧契约注释，已订正并注明）。
- **提交他域的发现（仅报告，未改）**：`VDC_PRIORITY_TX_REJECT_MAPPING_CONTRADICTION` / `..._MAPPING_EXHAUSTED` 两个已声明拒绝原因在 `components/vdc_dpll_manager/src/**` 无生产者；特等席侧没有专属 `ok,reason_mask` 判定页；P3 本身不校验席位页（席位页由只读判定工具负责）。
- **未闭合**：A'（独立 TX 通道）待评审；台架/P3 未执行；消费侧"从未到达"的计数与判定页 bit（G2/G6）仍需实现；波形段容量待确认。

### TDMA-PROGRESS-20261011-027：随机化不变量测试（发现并补上一条未计数的队列出口）

- TODO task ID：`TDMA-SEAT-03`（验收条目 18/19）；改动：`distributed_refmem_position_ack.inc`（新增 `s_position_priority_hold_delivered_count`）、`distributed_refmem.h`（快照字段 `lane_hold_delivered_count`）、`distributed_refmem.c`（快照填充）、`scpi_system_snapshot_commands.c`（`POSition?` 追加为**末位**字段，既有下标不变）、`tools/sync_trigger/seat_acceptance.py`（期望字段数 30→31）、`tests/python/test_tdma_seat_invariants.py`（新增）、`tests/python/test_t2_freeze_completion.py`、文档。
- **动机**：此前席位用例都是**手挑场景**。本轮把"稳定性"写成**随机化不变量**：机会守恒、M:N 公平性、等待预算上界、队列出口记账闭合——由不变量而非例子来判定。
- **发现（真问题）**：`distributed_refmem_position_priority_drain()` 里有**一条不计数的出队路径**——队列副本已由线上重复帧送达时只推进 `head` 不计数。记录本身没丢（已由重复帧送达），但**队列账面无法对账**：`lane_hold_count` 减已知出口会长期高估在队条目。修复：新增 `lane_hold_delivered_count`，并在 `POSition?` **末位**追加该字段（逐位读取者下标不变），恒等式变为
  `lane_hold_count == lane_drain_count + lane_hold_expired_count + lane_hold_delivered_count + Σ(tail − head)`。
- **同轮被我自己的模型纠正四处**（记录以示诚实，均为我的理解错误而非实现缺陷）：① `*handled` 是**路由归属**而不是"消耗了一次机会"；② `drain()` 返回值只表示**是否补投成功**，退休已送达副本会推进队列却返回 false；③ `drain()` 返回 false **不蕴含** `drain_fail_count` 增加（空队列/消费者忙都可能）；④ 随机驱动漏了新计数器的**按种子复位**（导致跨种子串账）。
- **测试覆盖**：`tests/python/test_tdma_seat_invariants.py`——A 段 6 组 M:N × 32 种子 × 400 次机会（守恒、公平性偏差 ≤ 1 槽位、预算上界、违例计数与预算跨越次数一致）；A2 段 32 种子 × 300 次混合流量（路由归属恒真、机会守恒）；B 段 16 种子故障恢复**存活性**（发布侧拒绝期间记录被延迟但绝不丢失，故障解除后下一次机会即登板）；FIFO 段 24 种子 × 400 次随机投递（逐次投递后与序列结束时校验恒等式、丢满仅发生在队列已满、按类拒绝互不混淆）；确定性由两次构建的**摘要比对**保证。
- **验证**：`test_tdma_seat_invariants.py` 3 passed；席位验收集 **88 passed**；设备语法守卫 7/7 PASS；设备构建两槽链接 + `flash_link_contract=OK`；`docs_check`（178 文件）/`doc_regression` OK。
- **未闭合**：A'（独立 TX 通道）待评审；台架/P3 未执行；波形段容量待确认。

### TDMA-PROGRESS-20261011-026：秒级设备语法守卫（把上轮的教训固化）+ 全量套件复跑

- TODO task ID：`TDMA-SEAT-03`；改动：`tools/sync_trigger/seat_acceptance.py`（新增 `--device-syntax` 模式）、`tests/python/test_seat_acceptance_tool.py`（+6 用例）、`vdc_dpll_manager.h`（容量历史注释订正）、`docs/arch/ARCH_RAM_REVIEW.md`（陈旧示例改为符号化表述）、`docs/tdma/TDMA_SEAT_ACCEPTANCE_REVIEW.md`。
- **动机（来自 `-025` 的教训）**：上轮两处缺陷与一次 SRAM 溢出**只有目标构建能发现**，而完整构建要数分钟。本轮把"按目标参数语法编译席位相关 TU"做成**秒级守卫**，命令为 `python tools/sync_trigger/seat_acceptance.py --device-syntax`。
- **实现要点**：① 从 `out/build/*/compile_commands.json`（取最新，可 `--build-dir` 指定）读取**真实编译参数**，对 7 个席位相关翻译单元做 `-fsyntax-only`；② `syntax_command()` 为纯函数，处理 CMake 命令串的两个坑——`-o/-MF/-MT/-MQ` 及其取值要剥离，Windows 路径不能被 POSIX 转义吃掉（`posix=False`），而宏值里的 `\"` 要还原成 `"`（shell 本会替我们做），整段带引号的路径要脱引号；③ 无编译数据库时**明确报错并提示先跑规范构建**，不静默通过；④ 编译器缺失/工作目录异常按 FAIL 记录而不是崩溃。
- **负测（证明守卫真的有用）**：临时移除 `tdma_seat_admission.h` 的 `#include <stddef.h>` 后，守卫**秒级报出与上轮设备构建完全相同的两个 TU**（`tdma_pio_spi_ring_adapter.c`、`tdma_seat_admission.c`），并带上编译器自己的提示 `'NULL' is defined in header '<stddef.h>'; this is probably fixable by adding '#include <stddef.h>'`；恢复后 7 个 TU 全 PASS。首次负测因文件为 CRLF 而**未真正生效**，已改用行过滤重做（记录以防重蹈）。
- **其他对账**：`vdc_dpll_manager.h` 的容量注释由陈旧的"three 360-record buffers"改为完整历史（400→360→352→334，含本轮缩容的理由与"下次应优先动席位侧 `HOLD_DEPTH`"的约束）；`ARCH_RAM_REVIEW.md` 中把"不能任意取 33,600 B 之类的中间值"改为符号化表述，避免引用已不存在的数值。
- **验证**：守卫实跑 7/7 PASS；`test_seat_acceptance_tool.py` **22 passed**；**全量主机套件（21 分 46 秒）`1 failed, 8088 passed, 1 skipped`**——与上上轮基线一致（唯一失败仍是需 TOOLS owner 处理的治理项），通过数由 8070 增至 8088（新增工具与守卫用例）。
- **未闭合**：A'（独立 TX 通道）待评审；台架/P3 未执行；波形段容量与"是否接受用席位 `HOLD_DEPTH` 换回记录数"待评审确认。

### TDMA-PROGRESS-20261011-025：设备构建暴露两处主机看不到的缺陷 + SRAM 溢出（按目标授权缩波形 buffer）+ scratch 预算恢复

- TODO task ID：`TDMA-SEAT-03`；改动：`tdma_seat_admission.h`（补 `<stddef.h>`）、`scpi_system_snapshot_commands.c`（按 class 计数改从 lane 快照读）、`tdma_priority_rx.h/.c`（拒绝归因移出 scratch）、`tdma_pio_spi_phys_priority.inc`、`vdc_dpll_manager.h`（波形段 352→334）、`tests/python/test_tdma_priority_rx.py`、文档。
- **本轮做法**：首次用**项目自身的发布构建**（`tools/cmake_build_auto/cmake_build_auto.py`，preset `pico2-release`，与 P3 同一路径）验证工作区。结果证明此前"主机测试全绿即视为无回归"的口径**不充分**：设备构建抓出两处主机看不到的编译缺陷与一次 SRAM 溢出。
- **缺陷 1（编译）**：我在 `tdma_seat_admission.h` 新增的内联判定函数使用 `NULL`，而该头只包含 `stdbool/stdint`；主机翻译单元恰好先经其它头引入了 `NULL`，设备侧报 `'NULL' undeclared`。修复：头文件补 `#include <stddef.h>`。
- **缺陷 2（编译）**：`SYSTem:TDMA:SEAT:HEALth?` 处理器读取 `lane.lane_reject_*_class_count`，而**这些字段不在位置运输快照里**（它们属 `tdma_priority_rx_snapshot_t`）。主机侧无任何用例真正编译该处理器，故一直未暴露。修复：按 class 拒绝计数改由 `tdma_runtime_owner_get_priority_rx_snapshot()` 读取，lane 快照不可用时返回错误（宁无数据，不报"看起来健康的 0"）。
- **缺陷 3（链接）**：主 SRAM 溢出 **1436 B**（`region RAM overflowed`）；HEAD 基线同一构建可链接且余 **2404 B**。逐符号归因（`nm` 差分 HEAD↔工作区）显示我的静态新增共 **800 B**（席位 FIFO 640 + 头尾指针 64 + lane 状态 32 + 适配器 24 + runtime owner 8 + 计数 32）。**按目标授权（"如果RAM不够，可以缩小波形buffer"）缩波形缓冲**：`VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS 352 → 334`（28 B/记录 × 3 缓冲 = 释放 **1512 B**；该缓冲历史上已为 RAM 缩过一次 400→360，注释一并订正）。**席位能力不退让**（FIFO 深度保持 4）。
- **缺陷 4（链接，scratch 区）**：主 RAM 修好后 `SCRATCH_X` 仍溢出 **8 B**。实测该区数据 **1000 B + Core1 3 KiB 栈 = 4072/4096**（项目文档记录的余量正是 24 B），而我在 `tdma_priority_rx_t` 加的 4 个归因字段因 lane 保存**两份状态**（live + published）恰好占 **32 B**。Core1 栈是历史上依据实测扩到 3 KiB 的、不可缩；故把**拒绝归因移出 scratch**：新增内部状态类型（lane 使用）与扁平公开快照（读者字段名不变），归因改为普通 RAM 的 `tdma_priority_rx_attribution()`，由 `tdma_priority_rx_snapshot()` 逐字段合并（显式避免依赖两种类型间 padding 一致）。**SCRATCH_X 恢复 1000 B / 24 B 余量，SCRATCH_Y 保持 1992 B。**
- **修复后实测**：`DHRT100.elf` / `DHRT100_B.elf` 均链接成功，`flash_link_contract=OK`（app×2 + boot）；`link_free_bytes=2108`、`bss_bytes=447428`、`.scratch_x`=0x3e8、`.scratch_y`=0x7c8。
- **宿主验证**：lane/席位/位置/诊断/运输/board-record/服务时序 **125 passed**；`test_vdc_dpll_replay` + `test_dpll_observation_capture` **106 passed**；三套 host 单测全过。
- **流程教训（已写入验收矩阵）**：凡改动固件（含头文件），**设备构建属于验证范围**，不能只跑主机套件；`compile_commands.json` 已生成，后续可据此对改动文件做快速的 ARM 侧语法守卫。
- **未闭合**：A'（独立 TX 通道）仍待评审；台架/P3 仍未执行；波形段 334 记录属产品可见参数，需评审确认（若后续还需 RAM 余量，此处仍是既定 donor）。

### TDMA-PROGRESS-20261011-024：一等席验收工具（只读、可离线复算）+ 真机剧本

- TODO task ID：`TDMA-SEAT-03`（验收条目 15）；改动：新增 `tools/sync_trigger/seat_acceptance.py`、`tests/python/test_seat_acceptance_tool.py`、登记 `tools/tool_registry.json`（+ catalog 重生成）、`docs/tdma/TDMA_SEAT_ACCEPTANCE_REVIEW.md`（真机剧本）。
- **动机**：剩余验收条目 15（真机证据）之前只有"读 `SEAT:HEALth?` 看 ok"的口头口径，既不可复现也容易**误触设备**（本目标早期就发生过一次探针误触发台架会话）。本轮把真机验收做成**只读、可复算、可审核**的工具与剧本。
- **工具设计要点**：① **只发查询**（`SEAT:HEALth?`、`SEAT:COUNters?`、`REFMEM:SYNC:FLIGHT:POSition?`、`TDMA:SCHEDule?`），从不发 STOP/START/配置；② 三种模式 `--port` / `--from-json` / `--self-test` **互斥且必填**，无"参数写错就连端口"的退化路径（测试断言 `main([])` 抛 `SystemExit`）；③ 判定为**纯函数** `evaluate()`，可对保存的采集离线复算；④ 页长不符时**拒绝解释**（`reason_mask=None`）而不是按错位字段猜；⑤ 每个置位的 reason 位都必须在本页找到对应计数，反之亦然（判定位与证据不允许互相矛盾）。
- **自检与守卫**：`--self-test` 覆盖健康板 + 8 个 reason 位各自置位 + 计数无位/位无计数 + 页长不符 + 饥饿证据，共 12 类；`tests/python/test_seat_acceptance_tool.py` **16 passed**，其中包含**从固件 SCPI 处理器源码反推字段数**与工具常量比对（22/12/30）——页长变更不会静默错位。
- **登记**：新增 `TOOL-TDMA-SEAT-ACCEPTANCE`（`DEVICE_READ`），catalog 重生成；治理检查 `owned_files == visible_files == 577`、`catalog_errors == []`。
- **真机剧本**：写入 [验收矩阵](TDMA_SEAT_ACCEPTANCE_REVIEW.md) 第四节——步骤 1 走既有 P3 流程构建烧录（不另立一套），步骤 2 只读采集并本机判定（证据落 `out/HardwareAcceptance/<date>/seat-acceptance-<time>/`），步骤 3 用 `--from-json` 交给评审者离线复算；并明确本工具**不**验证条目 14（通道独立），也不替代 P3。
- **验证**：`pytest tests/python/test_seat_acceptance_tool.py` **16 passed**；`--self-test` 退出码 0 且 `passed=true`；`docs_check`/`doc_regression` 通过。
- **未闭合**：待授权执行步骤 1/2；条目 14（A'）仍待评审。

### TDMA-PROGRESS-20261011-023：席位验收矩阵 + 门控行为主机验证（休眠路径不再只靠"能编译"）

- TODO task ID：`TDMA-SEAT-03`；改动：`tdma_pio_spi_ring_origin.inc`、`tests/python/test_tdma_seat_rotation.py`、新增 [TDMA_SEAT_ACCEPTANCE_REVIEW.md](TDMA_SEAT_ACCEPTANCE_REVIEW.md)（+ `docs/README.md` 索引行）、`docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`。
- **交付 1：验收矩阵**。新增非契约评审文档 `TDMA_SEAT_ACCEPTANCE_REVIEW.md`：把"一等席稳定性达到特等席水平"拆成 **16 条可核查条目**，逐条给出实现落点、证据（符号/命令/测试）与状态，并显式列出 4 项待决事项（席位窗口调度是否启用、A' 方案取舍、台架/P3 授权、TOOLS 治理项）与复核命令。文档声明"仅作索引、不定义语义"，避免与运行约束形成第二事实源（C12）。
- **交付 2：门控行为验证**。把 origin 的席位窗口门控映射抽成 `tdma_pio_spi_ring_origin_seat_gate(adapter, load_seat, boundary_sequence)`（返回"现在可发/推迟"，STARVED 时计数），并用既有抽取模式新增 harness `test_seat_window_gate_counts_starvation_and_defers` **实跑**该函数：未挂载=放行、PRIORITY 窗口=放行、FIRST_CLASS 非本边界=推迟且**不计数**、FIRST_CLASS 落入窗口（boundary 2 → 2 ms ∈ [1.5,3.0)）=放行、FIRST_CLASS 落入 guard 区（boundary 3）=推迟不计数、**LOAD 席（`TDMA_LOOP_PLAN_LOAD_ENABLED == 0`，从未被请求）任意边界=计饥饿且不放行**（`seat_starved_count` 1→2，`seat_first_class_starved_count` 保持 0）。
- **验证（主机，本次实测）**：`test_tdma_seat_rotation.py` **3 passed**（原用例 + 门控行为 + 构建守卫）；`test_tdma_seat_admission`/`_owner` 复跑通过；`docs_check`（178 文件）/`doc_regression` **OK**。
- **覆盖论证（替代再一次全量跑）**：本轮改动的头/实现只被 2 个翻译单元包含（`tdma_pio_spi_ring_adapter.c`、`tdma_seat_admission.c`），二者均被上述用例覆盖；门控默认关闭，运行时路径未变，上轮全量基线 `1 failed / 8070 passed` 仍成立。
- **未闭合**：验收矩阵条目 14（通道独立 A'）与 15（真机证据）仍待你决定/授权；条目 12 的运行时意义取决于是否启用 `TDMA_LOOP_SCHEDULE_ENABLED`。

### TDMA-PROGRESS-20261011-022：席位门控判定抽成纯函数 + 补齐"未请求席位"饥饿 + 构建守卫

- TODO task ID：`TDMA-SEAT-03`；改动：`tdma_seat_admission.h`、`tdma_pio_spi_ring_origin.inc`、`tests/unit/test_tdma_seat_admission.c`、`tests/python/test_tdma_seat_rotation.py`、`docs/`。
- **本轮发现（两点，均由读码确认）**：① 门控代码位于 `#if TDMA_LOOP_SCHEDULE_ENABLED` 内，而该宏**默认为 0**（席 位窗口调度未启用）→ 我在 `-019` 加的饥饿计数**当前不编译、也没有任何测试覆盖**；② `-019` 的检查只看 `dropped_mask`，而该掩码只为**已请求**席位置位——若把负载挂到**故意未启用**的 LOAD 席（挂载表允许并持久化该 seat，`tdma_seat_mount_seat_valid()` 接受它），计划里永远没有 LOAD 窗口、掩码里也没有 LOAD 位 → 仍会**永久静默推迟**，正是 `-019` 想根除的形态。
- **修复**：把门控判定抽成**纯函数** `tdma_loop_plan_seat_service(plan, seat, boundary_sequence, slot_period_ns)`（三种结果 `OK`/`DEFER`/**`STARVED`**），并新增 `tdma_loop_plan_seat_present()`；`STARVED` 覆盖"为空间被丢弃"与"从未被请求"两种成因，origin 据此计入 `seat_starved_count` / `seat_first_class_starved_count`（判定页 bit6）。
- **构建守卫（针对"默认关闭的代码会烂掉"）**：`tests/python/test_tdma_seat_rotation.py` 新增 `test_seat_window_gate_still_builds_when_enabled`——以 `-DTDMA_LOOP_SCHEDULE_ENABLED=1` 对**真实适配器翻译单元**做语法编译，必须通过。
- **验证（主机，本次实测）**：席位准入单测新增门控判定用例——两窗口计划中 PRIORITY present、FIRST_CLASS present、LOAD **not present**；`seat_service(NONE)`=OK、`seat_service(LOAD)`=**STARVED**（未请求席位）、`seat_service(PRIORITY, seq 0)`=OK、`seat_service(FIRST_CLASS, seq 0/1)`=DEFER、`NULL` 计划=STARVED、空间不足计划中 FIRST_CLASS=STARVED。`test_tdma_seat_admission` 与 `test_tdma_seat_rotation`（含构建守卫）**全过**。
- **文档口径修正**：运行约束明确写出"门控当前默认关闭，`seat_starved_count == 0` 是未启用的正常结果、不代表真机已验证"，避免把休眠状态误读为已验证行为。
- **未闭合**：席位窗口调度是否启用（`TDMA_LOOP_SCHEDULE_ENABLED`）属策略决定，需与 A' 一并评审；台架与 P3 仍未做。

### TDMA-PROGRESS-20261011-021：从板 sink 拒绝拆分（不可恢复的解码失败独立可见）

- TODO task ID：`TDMA-SEAT-03`；改动：`distributed_refmem_position_ack.inc`、`distributed_refmem.h/.c`、`tdma_pio_spi_ring_adapter.h`、`scpi_system_snapshot_commands.c`、`tests/python/test_t2_freeze_completion.py`、`tests/unit/test_tdma_pio_spi_ring_adapter.c`、`t2_bench_probe.py`、`docs/`。
- **发现的缺口（读码确认）**：`SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 的 `lane_reject_count` 实际来自 sink 的 `s_position_priority_reject_count`，而该计数混合了两种语义完全不同的情况——①`trigger_position_slice_decode()` 为假（帧已通过通道校验，但紧缩切片不可解，**记录丢失且不可恢复**）；②单深度 mailbox 仍被占用（**有界背压**，记录已进入每源 FIFO 等待补投）。于是"一等席到底丢了多少"必须从混合计数里猜，且解码失败没有判定位。
- **修复**：拆分——②保持为 `lane_reject_count`（与 `lane_hold/drain/drain_fail` 共同构成可恢复路径证据），①新增 `s_position_priority_decode_reject_count` → 快照 `lane_decode_reject_count` → `POSition?` 追加为第 30 字段；判定页新增 reason 位 **bit7 `DECODE_REJECT`** 并把它纳入 22 字段证据；探针新增 `position_lane_decode_reject_delta` 与 `seat_health_decode_reject`。
- **验证（主机，本次实测）**：`test_t2_freeze_completion.py` 新增用例——把已构造记录的切片 tag 位清零（帧仍合法）后送入 sink：`s_position_priority_decode_reject_count` +1、**背压计数不变**、**入队计数不变**、**接受计数不变**、无描述符可取；适配器 host 单测判定断言扩到 **13 组**（含 bit7 独立置位）→ `tdma_pio_spi_ring_adapter tests passed`；席位验收集 **125 passed**，三套 host 单测全过。**因改动落在优先级 IRQ 的接收 sink 上，另跑一次串行全量套件（22 分 17 秒）：`1 failed, 8070 passed, 1 skipped`——与基线逐项一致，无回归**（唯一失败仍是需 TOOLS owner 决定的治理项）。字段数脚本核对 `POSition?`=30、`HEALth?`=22。
- **RAM**：+4 B（一个计数）。
- **未闭合**：A'（独立 TX 通道）仍待评审；台架与 P3 仍未做。

### TDMA-PROGRESS-20261011-020：一等席"推迟"纳入同一条有界服务口径（发布侧等待不再不可见）

- TODO task ID：`TDMA-SEAT-03`；改动：`tdma_pio_spi_ring_origin.inc`、`tdma_pio_spi_ring_adapter.h`、`scpi_system_snapshot_commands.c`、`tests/python/test_tdma_seat_rotation.py`、`t2_bench_probe.py`、`docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`。
- **发现的缺口（读码确认）**：一等席记录被 `tdma_first_class_take()` 租用后，若 `phys_origin.publish()` 未完成，`tdma_pio_spi_ring_origin_board()` 返回"推迟"、`tdma_first_class_finish(..., false)` 把记录放回队列（这是对的设计，不丢记录）；**但等待预算只在"特等席上了车"时才推进**（`if (first_pending) seat_first_class_wait++` 在 priority 分支内）。于是由**发布侧**（硬件忙/stall）造成的等待可以**无界**且**不可见**：`seat_first_class_wait` 恒为 0、`wait_max` 不更新、判定页显示"健康"。
- **修复**：推迟分支显式记账——`seat_first_class_deferred_count++` 并把同一次机会**推进声明预算** `TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES`（达到即计一次 `seat_first_class_wait_violation_count` → 判定页 bit3），上车后等待归零、`wait_max` 记录峰值。这样"被特等挤掉"与"被发布侧拖住"两类等待共享同一条**有界、可计数**的口径；`SEAT:COUNters?` 增至 **12** 字段、`SEAT:HEALth?` 增至 **21** 字段（新增字段均为上下文证据，不新增 reason 位，避免把正常的偶发推迟误报为降级）。
- **顺带审计（无改动，记录结论）**：① FREEZE latch 是"每 position_id 1..15 一位"的幂等锁存（`trigger_position_freeze_latch_put()` 对已锁存/有效 id 恒返回真），**不存在"满了丢 FREEZE"的状态**，其假返回只对应非法 id/version（线上由 1..15 的 nibble 与解码校验排除）；② 快路径轮换用 `tdma_first_class_pending()`（席位队列）判定，而 loop plan 窗口用**席位挂载表**判定，两者判据不同但职责不同（前者决定"谁带头"，后者决定"何时可发"），暂不作为缺陷，已记入评审待办。
- **验证（主机，本次实测）**：`test_tdma_seat_rotation.py` 通过——新增断言：单次推迟 → `seat_first_class_deferred_count==1` 且记录仍 pending；连续推迟满预算 → `seat_first_class_wait==预算`、`wait_violation_count` +1、`deferred_count==1+预算`；随后成功上车 → 等待归零且 `wait_max==预算`。席位验收集 **125 passed**、三套 host 单测全过。**因改动触及 origin 发布路径，另跑一次串行全量套件（21 分 38 秒）：`1 failed, 8070 passed, 1 skipped`——与基线逐项一致，无回归**（唯一失败仍是需 TOOLS owner 决定的治理项）。字段数脚本核对 `COUNters?`=12、`HEALth?`=21。
- **RAM**：origin +4 B（一个计数）。
- **未闭合**：A'（独立 TX 通道）仍待评审；台架与 P3 仍未做。

### TDMA-PROGRESS-20261011-019：loop plan 席位饥饿显式化（一等席不再可能静默永久推迟）

- TODO task ID：`TDMA-SEAT-03`；改动：`tdma_seat_admission.h/.c`、`tdma_pio_spi_ring_adapter.h`、`tdma_pio_spi_ring_origin.inc`、`scpi_system_snapshot_commands.c`、`tests/unit/test_tdma_seat_admission.c`、`tests/unit/test_tdma_pio_spi_ring_adapter.c`、`t2_bench_probe.py`、`docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`、`docs/tdma/TDMA_DOMAIN_TODO.md`。
- **发现的缺口（读码确认，非推测）**：`tdma_loop_plan()` 按 PRIORITY → FIRST_CLASS → LOAD 顺序铺窗口，剩余空间不足时只是 `break`；origin 发布路径用 `tdma_loop_plan_slot_for_index()` 比对"本负载所在席位"，不匹配就 `return true`（推迟重试）。因此当窗口排不下时，挂在一等席上的负载会在**每个边界**被推迟且**没有任何计数或错误**——`(void)tdma_loop_plan(...)` 连返回值都不看。默认配置（环 4 ms / 发射 1.5 ms / guard 30 µs）能容纳两个窗口，但 `start_offset_ns`（可配置）或更小的环周期会让一等席先被挤掉：典型地 `start_offset=1 ms` 时 priority 占 [1.0,2.5)，first-class 需要 2.5+1.5+0.03>4 ms → **一等席无窗口**。
- **修复**：① `tdma_loop_plan_t` 新增 `dropped_mask`（位 `TDMA_LOOP_PLAN_DROPPED_PRIORITY/FIRST_CLASS/LOAD`）与纯内联查询 `tdma_loop_plan_seat_dropped()`；planner 在 break 时把**所有**被丢弃的已请求席位记入掩码；② origin 发现"本负载席位被丢弃"时计入新的 `seat_starved_count` 与 `seat_first_class_starved_count`（`tdma_pio_spi_ring_origin_status_t`），仍按原语义推迟但不留静默；③ 判定页新增 reason 位 **bit6 `FIRST_CLASS_STARVED`** 并把 `seat_first_class_starved_count` 纳入 20 字段证据；`SEAT:COUNters?` 增加两个饥饿计数（11 字段）；④ 探针新增 `seat.seat_starved_delta` / `seat.first_class_starved_delta` 与 `seat_health_first_class_starved`。
- **验证（主机，本次实测）**：席位准入单测新增饥饿用例——`emission=1.34 ms` 三窗口排不下 → `dropped_mask==LOAD`；`start_offset=1 ms` → 只放下 priority、`dropped_mask==FIRST_CLASS` 且 `seat_dropped(FIRST_CLASS)` 为真；`start_offset=2.5 ms` → `count==0` 且掩码为 PRIORITY|FIRST_CLASS；`NULL`/`NONE` 查询为假；锚定环首时掩码恒为 0。适配器 host 单测判定断言扩到 **11 组**（含 bit6 独立置位）→ `tdma_pio_spi_ring_adapter tests passed`。席位相关用例 **132 passed**，三套 host 单测全过。**因本次触及 origin 发布路径，另跑一次串行全量套件（22 分 17 秒）：`1 failed, 8070 passed, 1 skipped`——与上一轮基线逐项一致，无回归**（唯一失败仍是需 TOOLS owner 决定的治理项）。字段数：`COUNters?`=11、`HEALth?`=20（脚本核对与文档一致）。
- **RAM**：origin +8 B（两个计数）；planner 结构 +4 B（掩码，非持久）。累计席位相关新增仍 <1.5 KB，未动用"缩波形 buffer"授权。
- **未闭合**：A'（独立 TX 通道）仍待评审；台架与 P3 仍未做。

### TDMA-PROGRESS-20261011-018：清既有失败 9 项（描述符 schema 漂移 6 项 + 迁移路径/契约口径 3 项）

- 背景：`-015` 之后全量套件剩 10 个既有失败。本轮把其中 **9 个**修掉，只留下一个需要 TOOLS owner 决定的治理项。
- **修复 ①（6 项，`test_trigger_position_instance` 2 + `test_trigger_action_template` 1 + `test_trigger_action_runtime` 3）**：`TRIGGER_POSITION_DESCRIPTOR_SCHEMA` 已升到 **2**，而这三处 harness 仍写死 `.schema=1` → `trigger_position_descriptor_valid()` 一律拒绝，`expand`/`stage` 断言随之失败。改为引用**符号** `TRIGGER_POSITION_DESCRIPTOR_SCHEMA`（并加注释），使 schema 再升版不会静默作废这些检查。**结果 9 passed。**
- **修复 ②（1 项，`test_vdc_dpll_replay`）**：测试把 runner 的兼容副本指向 `tools/vdc_dpll_replay/…_runner.c`，而 tools 迁移按登记表把该副本放在 `tools/vdc/vdc_dpll_replay/`（registry role=`resource`，与归档字节完全相同：`sha256 8640d710…`，9528 B）；最深旧目录只保留 `.py` 包装与 README。按登记表修正路径，保留"归档 == 兼容副本、规范化后 == 分类目录"三段字节断言。**结果 88 passed。**
- **修复 ③（1 项，`test_p3_hardware_acceptance`）**：P3 `run` 的口径**已变更**为"显式 `--config` 优先，仅在默认配置时才按 `--full` 选 p3_bench/quick"（工具内注释给出理由：原先会静默忽略调用方要求的配置）。测试仍写旧语义（字面量名为 `ignored.json`），按新契约重写并**加密**：默认配置 + full/quick 选对应 profile、显式配置在两种模式下都必须胜出、`resume` 仍用自定义配置。**注意：这是钉住工具既有行为，不修改 P3 工具本身**（提交门禁语义未变）。
- **修复 ④（1 项，`test_calibration_topology_start`）**：ARM 未知 ACK 的拒绝路径现在可能由"ACK 分类"或"后续新鲜状态确认"两处触发，失败文本相应有 `ARM acknowledgment unknown` / `unconfirmed ARM` / `invalid ARM status` 三种；测试只认第一种。放宽正则覆盖三者，**保留核心断言**（未知 ARM ACK 不得借用旧 status=1、且不得发出 `:START`）。**结果 140 passed（两个文件合计）。**
- **仍未修（1 项，需 TOOLS owner）**：`test_tool_governance.py::test_actual_registry_and_catalog_are_in_sync` 要求整份治理检查零 error，而现存 `unassessed legacy tool family`（103 族）、`dependency review pending` 与 `invalid allowed tool root file` 属**评估/所有权决定**，不是测试侧可改的口径（登记表 notes 明确"legacy inventory only，不代表支持资格"）。
- **验证**：上述四组文件定点复跑 **9 + 88 + 140 passed**；串行全量套件（22 分 16 秒）**`1 failed, 8070 passed, 1 skipped, 0 errors`**——对比本轮开始时的 `10 failed, 8061 passed, 0 errors`：9 项既有失败清零、通过数 +9，唯一剩余项即上文需 TOOLS owner 决定的治理项。
- **本目标累计（第 7→14 轮）**：全量套件由 `13 failed / 7607 passed / 451 errors` 收敛到 `1 failed / 8070 passed / 0 errors`。

### TDMA-PROGRESS-20261011-017：A' 决策所需硬事实——32 B 短槽逐字节占用表（否证方案 A）

- TODO task ID：`TDMA-SEAT-03` 缺口 A'；改动：[TDMA_SEAT_TX_CHANNEL_REVIEW.md](TDMA_SEAT_TX_CHANNEL_REVIEW.md)（附录 A + 推荐与问题集修正）。
- **做法**：不写代码、不改布局，只把布局常量（`tdma_process_image_layout.h`、`tdma_flight_engine.h`）逐符号导出成 **32 B 短槽占用表**，回应评审问题 1。表内每个区间都标注了唯一的偏移/长度符号，属"符号导出的快照"，非实测值，也不手写硬数字（AGENTS.md 硬约束 2）。
- **结论（推翻方案 A 的前提）**：偏移 0–31 **全部有主**，短槽没有空闲字节——方案 A 原文的"使用未用余量"不成立。独立化只剩三条路：**A″ 回收式专属**（显式把某字段所有权转给一等席并给出旧 owner 替代，候选是偏移 29 的 optional diagnostic，其读回 `last_optional_diagnostic` 需替代方案）、**B 专属帧**（不回收字节，代价是每圈多一帧 → 需重算 M:N 与 IRQ WCET）、**C 现状**（准入+仲裁，标注"未独立"）。
- **补充事实**：opcode **命名空间已分离**（一等 `0xE0|run_id`、诊断 `0xD1`/`0xD2`），接收侧不存在混淆；真正的共享只是同一对字节的**写权**。因此若长期停留在 C，可把"命名空间分离 + 诊断准入 + `lane_ack_override_count` 恒为 0"作为显式验收条款（已列为评审问题 6）。
- **验证**：`docs_check --strict-names` **OK**、`doc_regression` **OK**（文档门禁全绿）；表格内容逐项对照上述两个头文件符号。
- **席位验收证据块（主机，本轮集中复跑）**：席位/位置/诊断/运输验收集 **243 passed**（`test_tdma_seat_rotation`、`test_tdma_seat_admission`、`test_tdma_priority_rx`、`test_tdma_priority_rx_scpi`、`test_t2_freeze_completion`、`test_t2_position_flight`、`test_trigger_position_mailbox`、`test_distributed_action_probe`、`test_refmem_action_probe`、`test_vdc_feedback_transport`、`test_vdc_feedback_preacquire`、`test_vdc_flight_rx`、`test_vdc_command_ingress`、`test_tdma_board_record`、`test_flash_inventory`）；三套 host 单测 `tdma_pio_spi_ring_adapter`（含 9 组席位判定断言）、`tdma_service_scheduler`、`tdma_ring_runtime` **全部 passed**。
- **未闭合**：A'/A″/B/C 的取舍仍等你评审；台架与 P3 仍未做。

### TDMA-PROGRESS-20261011-016：一等席稳定性判定页 `SYSTem:TDMA:SEAT:HEALth?`

- TODO task ID：`TDMA-SEAT-03`；改动：`tdma_pio_spi_ring_adapter.h`、`scpi_system_snapshot_commands.h/.c`、`tests/unit/test_tdma_pio_spi_ring_adapter.c`、`t2_bench_probe.py`、`docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`、`docs/tdma/TDMA_DOMAIN_TODO.md`。
- **动机**：一等席的退化证据此前分散在三个页（`SEAT:COUNters?`、`FLIGHT:PRIORity?`、`REFMEM:SYNC:FLIGHT:POSition?`），一次台架运行后仍需人工比对才能回答"一等席是否达到特等席水平"。特等席的口径是可运维：一个读回即给结论与原因。
- **实现**：纯函数 `tdma_seat_health_reason()`（`tdma_pio_spi_ring_adapter.h` 内联，输入 `tdma_seat_health_facts_t`）把六个**必须为零**的退化事实折成 `reason_mask`；SCPI 页 `SYSTem:TDMA:SEAT:HEALth?` 输出 `ok,reason_mask` + 19 字段原始证据（含 origin 席位计数与从板 lane 计数）。设计内的有界背压（`first_class_stage_refused_count`）只作上下文，**不**进入 `reason_mask`，避免把正常背压误报为降级；`reason_mask` 的 bit2（`ACK_OVERRIDE`）同时是"独立 TX 通道是否建立"的判据（准入生效后应恒为 0）。
- **读出**：探针 `BUDGET_QUERIES` 新增该页，并把 `seat_health_ok` / `seat_health_reason_mask` 提到 delta 行顶层，使一次运行的结果可直接判读。
- **验证**：适配器 host 单测新增 9 组断言（健康=0、`NULL`→全位、六个位各自独立置位、组合位为并集）→ `tdma_pio_spi_ring_adapter tests passed`；编译含快照 SCPI 的三个用例 **47 passed**；探针语法校验通过；字段数脚本核对 **19**。
- **RAM**：无新增存储（纯函数 + 只读页）。
- **未闭合**：A' 本体（独立 TX 通道）仍待评审；台架复测与 P3 凭证仍未做。

### TDMA-PROGRESS-20261011-015：席位域既有失败三连修（admission / flash 清单 / board record）

- 背景：`-014` 之后主机套件只剩 13 个既有失败（全部在 HEAD 验证过）。其中**三个属 TDMA/席位域**——席位工作自己把自己的门禁测试打红了，按目标口径（席位工程健康度即"达到特等席水平"的一部分）本轮一并修好。
- **① `test_tdma_seat_admission`**：`tests/unit/test_tdma_seat_admission.c` 里 6 处 `tdma_loop_plan_request_t` 与 1 处 `tdma_loop_plan_t` 用**定位初始化器**，落后于结构体新增字段（`start_offset_ns`、`loop_period_ns`），触发 `-Werror=missing-field-initializers` → 全部改为**指定初始化器**（新增字段显式写 0 或原有 200000u 值，语义不变）；另把 Python 侧写死的 `"5 case groups passed"` 改为"正则取组数 ≥ 14"，避免 C 侧增加用例再次腐化。**结果：1 passed（C 侧 14 组全过）。**
- **② `test_flash_inventory`**：断言写死原始 flash 调用点为 10 个，而 `components/tdma/src/tdma_seat_mount_store.c`（`TdmaSeatMountStoreReadView`，TDMA-SEAT-01 引入）是**第 11 个且已登记在 `config/flash_raw_call_allowlist.json`**。改为断言"源码发现集合 == allowlist 声明集合"，并显式要求席位挂载存储仍在集合内。**结果：7 passed。**
- **③ `test_tdma_board_record`**：密封 board record 的 `fifo` 组是 live SCPI 页的**精确前缀**，而 live 页新增了三个类型 B 发送拒绝计数（`tx_reject_bad_payload_count/ring_full/no_slot`）；测试只给 `runtime` 组开了"显式命名追加块"白名单。按同一机制为 `fifo` 组补 `appended_fifo` 白名单（仍强制追加块必须**恰好**位于尾部、历史字段不得移动/改名/消失）。**结果：26 passed。**
- **验证**：三者合并复跑 **34 passed**；串行全量套件（22 分 07 秒）**`10 failed, 8061 passed, 1 skipped, 0 errors`**——对比本轮开始时的 `13 failed, 8058 passed, 1 skipped, 0 errors`：席位域三个既有失败清零、通过数 +3、error 仍为 0。
- **剩余 10 个既有失败（均非席位域，且已在 HEAD 上验证过）**：`test_trigger_action_runtime`(3)、`test_trigger_position_instance`(2)、`test_trigger_action_template`(1)、`test_calibration_topology_start`(1)、`test_p3_hardware_acceptance`(1)、`test_tool_governance`(1)、`test_vdc_dpll_replay`(1)。
- **说明**：以上均为测试脚手架/断言口径修正，不改生产语义；`tests/` 属 §7 范围，提交需 P3 凭证。

### TDMA-PROGRESS-20261011-014：根因④修复——主机套件既有 error 清零

- 背景：`-010` 给出 451 errors 基线，`-011` 修掉其中 441 个，剩余 10 个全在 `tests/python/test_sync_io_action.py`。本轮把它修完。
- **真凶（本轮用"打补丁的临时副本"实测定界，不靠猜）**：该用例派生自 `test_sync_io_switch.py` 的共享 mock，而 mock 把 PIO 程序长度**硬编码为 switch 程序的 6 字**（`pio_can_add_program`/`pio_add_program` 里的 `g->length==6`），派生 harness 里的改写锚点却是**陈旧的 `g->length==4`**——`str.replace` 静默匹配失败，于是 8 字的 action 程序永远 `pio_can_add_program()==false`，`sync_io_action_load()` 返回 false。探针输出：`LOAD d=1 id=11 dma=0 w=65536 s_mask=65536 c1=0 c3=0 cap=0`（`cap=0` 即 can_add 失败）。
- **同时存在的第二处腐化**：共享 mock 后来拥有了 `raw_lo()` 驱动的计时器模型（每次读时钟都会推进 8 拍），派生 harness 又定义了一套纯字段计时器 → 同名 `timer`/`clock_get_hz` 重复定义（硬错误）；且派生 harness 的断言（`enable_before_tick==origin`）要求**读时钟无副作用**，即纯字段模型才是它期望的语义。
- **修复**：① 锚点改为 mock 的真实文本 `g->length==6`→`g->length==8`，并加 `assert MOCK.count('g->length==6')>=2` 防止将来 mock 变更再次静默失效；② 派生 harness 删除继承的 `raw_lo` 计时器模型（保留 `advance()` 前置声明）与 `raw_reads/origin_cost/late_after`，保留自己的纯字段计时器；③ 为该变体 mock 增加 `#pragma GCC diagnostic ignored "-Wunused-function"`（共享 mock 里有本变体不调用的桩函数）。均为测试脚手架改动，不改生产语义。
- **结果**：`test_sync_io_action.py` **10 passed**（此前 10 errors）；与本目标相关的 13 个既有失败**不受影响**（仍是在 HEAD 上验证过的那些）。
- **说明**：`tests/python/` 属 §7 测试范围，提交需 P3 凭证。

### TDMA-PROGRESS-20261011-013：一等席 TX 侧诊断准入（评审草案方案 C 落地，过渡项）

- TODO task ID：`TDMA-SEAT-03`（缺口 A' 过渡）；改动：`distributed_refmem_position_ack.inc`、`distributed_action_probe.inc`、`distributed_refmem.h/.c`、`scpi_system_snapshot_commands.c`、`t2_bench_probe.py`、`tests/python/test_t2_freeze_completion.py`、`test_distributed_action_probe.py`、`test_vdc_feedback_transport.py`。
- **问题**：`lane_ack_override_count` 只能事后统计"诊断被覆盖"；诊断仍然**先占用**共享控制字节，属于"事后仲裁"而不是"准入"。特等席的口径是有独立资源，一等席至少在共享资源上应当**事先**获得优先权。
- **实现**：新增准入查询 `distributed_refmem_position_ack_control_available(mailbox_class)`——当本板有冻结事实要随本镜像发布（`s_position_local_frozen != 0`）且该 class 本来会承载该事实时，返回 false 并计入 `s_position_diag_refused_count`；`action_patch_mailbox(mailbox, bool control_allowed)` 相应改造：**载荷片段照旧写入**（不破坏诊断自身的 RefMem 广播语义与 peer 选择），但 `control_allowed` 为假时**不写** `CONTROL_OPCODE`/`CONTROL_SEQ8`。镜像构建顺序保持"先诊断、后一等确认"，因此未预留路径上的覆盖仍被计入 `lane_ack_override_count`。
- **读出**：`SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 追加 `lane_diag_refused_count`（29 字段）；探针新增 `position_lane_diag_refused_delta`（index 22）。文档（[RUNTIME_CONSTRAINTS](TDMA_RUNTIME_CONSTRAINTS.md)、[SCPI_COMMANDS](../interface/SCPI_COMMANDS.md)、[TODO](TDMA_DOMAIN_TODO.md)、[方案评审](TDMA_SEAT_TX_CHANNEL_REVIEW.md)）同步。
- **不变式（自证独立性的判据）**：准入生效后 `lane_ack_override_count` 应恒为 0——若仍增长，说明存在准入之外的共享写入，独立通道尚未建立。
- **验证（主机，本次实测）**：`test_t2_freeze_completion.py` 通过（新增：冻结时准入拒绝且计数 +1、诊断不占控制字节、随后的一等 patch **不产生新覆盖**、无冻结时/T2 position class 上准入恒放行且不计数）；`test_distributed_action_probe.py`、`test_refmem_action_probe.py`、`test_vdc_feedback_transport.py`、`test_vdc_feedback_preacquire.py`、`test_t2_position_flight.py`、`test_trigger_position_mailbox.py` 共 **159 passed**。
- **RAM**：新增 1 个计数（4 B）+ 1 个布尔参数。
- **未闭合**：A' 本体（方案 A 载荷专属段 / B 专属帧）仍待评审；台架复测与 P3 凭证仍未做。

### TDMA-PROGRESS-20261011-012：缺口 A' 方案评审草案 + 根因④诊断（`test_sync_io_action` 暂缓）

- **交付**：新增 [TDMA_SEAT_TX_CHANNEL_REVIEW.md](TDMA_SEAT_TX_CHANNEL_REVIEW.md)（非契约评审草案，已在 `docs/README.md` 加索引行）——针对 `TDMA-SEAT-03` 唯一剩余功能缺口 A'（一等席独立 TX 通道）给出三个候选方案与逐项影响面/验收口径：**A 载荷内专属段**（推荐，改动最小，需先给 mailbox 32 B 的逐字节占用表证明有余量）、**B 专属出站镜像帧**（物理隔离最彻底，但要重算 origin 发布机会 M:N 与优先级 lane 的 IRQ WCET）、**C 共享加固**（零 wire 变更的过渡做法，明确不作为关闭条件）。列出的待评审问题包括：余量表、线格式版本兼容/混版本策略、方案 B 的开销数字、异主写入检测计数命名（拟 `lane_alien_write_count`，须与 `lane_ack_override_count` 去重）、以及是否必须含四板台架证据。**该缺口的实施仍等待评审，不自行改布局。**
- **根因④诊断（`test_sync_io_action`，10 项，本轮暂缓并保持原状）**：该文件派生自 `test_sync_io_switch.py` 的共享 mock，而共享 mock 后来拥有了 `raw_lo()` 驱动的计时器模型（`timer`/`timer1_hw`/`timerawh`/`timerawl`/`clock_get_hz`/`TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS`），派生文件又**再定义一套同名计时器**，于是出现 `conflicting types for 'timer'` 等硬错误（这正是本目标两轮里 451 个既有 error 的一员，HEAD 同样失败）。本轮实测两条修复路线：
  1. 保留派生（纯字段）计时器、删除继承模型 → 编译通过，但首个用例 `sync_io_action_load(&c,&g)` 返回 false；
  2. 保留继承（raw_lo）模型、把派生 `advance` 改为写 `timer.raw_lo_dummy/raw_hi_dummy` → **同样**在该断言失败。
  两条路线失败点一致 ⇒ 该失败**与计时器模型选择无关**，属 mock 与生产 `sync_io_action_load()` 的 ACTION persona 接口失效（`sync_io_pio0_runtime_prepare_io(SYNC_IO_PERSONA_ID_ACTION,...)` 内 `hooks.load/arm` 未满足生产校验）。为避免把全量基线数字搅浑（10 errors 会变成 10 failed），**已回退该文件改动**，保留上述诊断供后续单独处理。

### TDMA-PROGRESS-20261011-011：清除席位插桩/T2 迁移遗留的主机脚手架腐化（约 640 项用例恢复）

- 背景：`TDMA-PROGRESS-20261011-010` 已修 3 处并给出全量基线（`13 failed / 7607 passed / 451 errors`），并定位 451 个 error 的根因。本轮把其中**与席位插桩/位置运输直接相关的部分全部修掉**——理由是：席位插桩自身把主机套件打断 400+ 项，本身就是"席位工程不达特等席水平"的证据。
- **修复 A：seat store 主机桩（根因①）**。新增 `tests/unit/host_stubs/tdma_seat_mount_store_stub.c`，提供 `tdma_seat_mount_store_note_reject()` 与 `tdma_seat_mount_store_status()`（席位插桩会无条件调用它们，但多数 harness 不链接 store 实现）。接入：
  - `test_vdc_run_output.compile_host()` **一处接入，同时修好 `test_vdc_run_timeline`**（后者复用前者）→ **184 + 60 passed**（原 151+58 errors）；
  - `test_trigger_action_service`（owner/parser/exec-parser 三处）→ **85 passed**（原 60 passed + 25 errors）；`test_trigger_action_execution` → **11 passed**；`test_trigger_deployment_service` → **74 passed**（并在 harness 内补 `trigger_action_execution_is_loaded()` 桩）。
- **修复 B：生产侧 SCPI 依赖声明**。`middleware/scpi_port/inc/scpi_port_internal.h` 增加 `#include "tdma_seat_mount_store.h"`——SCPI 命令实现调用该 store 的诊断入口，原先只靠 `scpi_system_snapshot_commands.c` 的 include 顺序（第 40 行）才可见，单编译某个 `.inc` 的 harness 因此得到 implicit declaration。声明放在 SCPI 内部头里，依赖关系不再依赖包含顺序。
- **修复 C：缺失的组件包含路径（根因②）**。`distributed_refmem.h` 会包含 `trigger_position_descriptor.h`（位于 `components/sync_trigger/inc`），但若干 fixture 的 include 列表没有 `sync_trigger`：给 `test_vdc_priority_follow_config`（**40 passed**）、`test_vdc_command_stop`（**15 passed**）、`test_node_capacity`（**17 passed**）、`test_vdc_command_ingress`（**11 passed**）补上。
- **修复 D：抽取式 harness 缺位置运输桩（根因③）**。这些 harness 抽取 `distributed_refmem.c` 的函数体，却不编译 `distributed_refmem_position_ack.inc`，于是新调用点（`distributed_refmem_position_ack_receive`、`distributed_refmem_position_priority_owned`、`distributed_refmem_tdma_parse_position_fragment`）无声明：按 `test_vdc_feedback_transport.py` 既有做法补三个 `static` 桩 → `test_vdc_command_ingress` **11 passed**、`test_vdc_flight_rx` **20 passed**。
- **累计恢复**：本目标两轮共修复 **13 个既有腐化文件、约 640 项用例**（93+23+8+184+60+11+74+40+15+17+11+20+85）。仍未修：`test_sync_io_action`（10 项，`action_mock.h` 与真实 pico 头冲突：`conflicting types for 'timer'`、`clock_get_hz` 重定义、`-Werror=unused-function`），属根因④（mock/头文件顺序），留待下一轮。
- **验证**：逐个文件复跑确认（见上）。**串行全量套件新基线：`13 failed, 8048 passed, 1 skipped, 10 errors`（21 分 59 秒）**，对比 `-010` 记录的 `13 failed, 7607 passed, 1 skipped, 451 errors`：**441 个 error 清零、441 项转为通过，失败集合不变（仍是那 13 个已在 HEAD 上验证过的既有失败）**。剩余 10 个 error 全部集中在 `test_sync_io_action`（根因④）。`tests/unit/host_stubs/tdma_seat_mount_store_stub.c` 与各 harness 属 §7 测试范围，提交需 P3 凭证。

### TDMA-PROGRESS-20261011-010：修复既有主机脚手架腐化（138 项用例恢复可运行）

- 背景：`TDMA-PROGRESS-20261011-004/005` 记录的两处既有失败（均由 `8bd547b2` 的席位插桩/位置静态量引入，非本轮功能改动导致），此前只能靠 stash 对照实验证明"HEAD 同样失败"。本轮把它们修好，使这些用例重新成为可信的验证手段。
- **修复 1：`tests/python/test_vdc_feedback_preacquire.py`（118 项 ERROR → 93 passed）**。该 harness 逐个抽取 `distributed_refmem.c` 的函数体，却没有注入 `distributed_refmem_position_ack.inc` 里的位置运输静态量，也没有 `distributed_refmem_position_cancel_service`。按 `test_t2_position_flight.py` 已有做法补注入：`s_position_tx_mailbox`、`s_position_rx_mailboxes[]`、`s_position_freeze_latches[]`、`s_position_cancel_requested` 与 cancel service 的桩，并显式包含 `trigger_position_mailbox.h`。
- **修复 2：`tests/python/test_tdma_service_nonblocking.py`（17 项链接错误 → 23 passed）**。该单测以 `PICO_ON_DEVICE` 路径包含 `tdma_service.c`（因此走设备分支、包含 seat store 头），但从未链接 seat store 实现。在测试 TU 内补 `tdma_seat_mount_store_note_reject()` 的桩定义（seat store 本体由自己的套件覆盖）。
- **验证**：`test_vdc_feedback_preacquire.py` **93 passed**、`test_tdma_service_nonblocking.py` **23 passed**；两处修复都不改变生产代码语义（纯测试脚手架）。
- **修复 3：`tests/python/test_t2_deploy_action_probe.py` 收集失败 → 8 passed**。全量套件此前因该文件的收集错误整体中断：它导入 `tools.t2_deploy_action_probe.t2_deploy_action_probe`，而 tools 迁移把实现搬到了 `tools/archive/deletable-candidates/t2_deploy_action_probe/`（该目录名含连字符，**根本无法导入**），迁移的引用审计也漏掉了这个测试导入。按仓库既有的 import-safe wrapper 约定补 `tools/t2_deploy_action_probe/t2_deploy_action_probe.py`（用 `importlib` 按路径加载归档实现并注册 `sys.modules`，否则 dataclass 解析 `__module__` 失败），并把该 wrapper 登记进 `tools/tool_registry.json`。
- **工具治理**：`tool_governance.py check` 现在报 `owned_files == visible_files == 576`（此前有 3 个未登记文件：新 wrapper、以及 T2 台架工具 `tools/sync_trigger/t2_bench_probe.py`、`t2_enable.py`）。顺带把后两者按 schema 登记为 `TOOL-T2-BENCH-PROBE`（`DEVICE_CONFIGURE`），并在 notes 中写明：`t2_bench_probe.py` 收到未知参数会**落到默认全量运行**，调用时必须显式给输出目录或模式（本目标早前正是这样误触发过一次台架会话）。`tools/README.md` 的 catalog 块已按 registry 重新生成（`catalog_errors == []`）。
- **既有失败（stash 对照确认，非本次引入）**：`test_tool_governance.py::test_actual_registry_and_catalog_are_in_sync` 在 HEAD 上同样失败——它要求整份治理检查零 error，而当前存在既有的 `unassessed legacy tool family` / `dependency review pending` / `invalid allowed tool root file` 条目；本次改动只减少了"未登记文件"这一类 error。
- **验证纪律**：本仓库的 `compile_grid`/`compile_executable` 使用 `out/pytest/runs`（`.gitignore` 忽略），**并发跑多个 pytest 进程会互相踩目录并报 `FileExistsError`**；判断全量套件状态必须**串行单次**运行（本轮清空该目录后复跑，确认 error 不是残留假象）。
- **全量套件基线（串行单次，19 分 43 秒）**：`13 failed, 7607 passed, 1 skipped, 451 errors`。**13 个 failed 逐条比对后确认在 HEAD 上同样失败**（stash 全量对照，清单完全一致）：`test_calibration_topology_start`(1)、`test_flash_inventory`(1)、`test_p3_hardware_acceptance`(1)、`test_tdma_board_record`(1)、`test_tdma_seat_admission`(1)、`test_tool_governance`(1)、`test_trigger_action_runtime`(3)、`test_trigger_action_template`(1)、`test_trigger_position_instance`(2)、`test_vdc_dpll_replay`(1)。
- **451 个 error 的三大根因（本轮抽样定位，尚未修）**：① 缺 seat store 符号（`tdma_seat_mount_store_note_reject`、`tdma_seat_mount_store_status`，另有 `trigger_action_execution_is_loaded` 未定义）——`test_vdc_run_output`、`test_trigger_action_service`、`test_vdc_run_timeline`、`test_trigger_deployment_service`、`test_trigger_action_execution` 等；② 头文件/包含路径漂移（`In file included from components/distributed_refmem/inc/refmem_application*`）——`test_vdc_priority_follow_config`、`test_vdc_command_stop`、`test_node_capacity`、`test_vdc_command_ingress`、`test_sync_io_action`；③ `test_vdc_flight_rx` 在 `distributed_refmem_*` 内的单点错误。三者都与本目标的功能改动无关，属 `8bd547b2`（席位插桩）与 tools 迁移之后的脚手架腐化。
- **说明**：`tests/unit/test_tdma_service_nonblocking.c`、harness、`tools/` 与 registry 属 §7 的工具/测试范围，提交时同样需要 P3 凭证。

### TDMA-PROGRESS-20261011-009：一等席有界接收队列（SEAT-03 缺口④-RX 完成）

- TODO task ID：`TDMA-SEAT-03`；改动：`distributed_refmem_position_ack.inc`、`distributed_refmem.h/.c`、`scpi_system_snapshot_commands.c`、`t2_bench_probe.py`、`tests/python/test_t2_freeze_completion.py`。
- **动机**：R2 的"每源一槽、只保最新"在同一窗口内出现多条被拒记录时会丢掉较早的那条；且它用 generation 防半写来对抗"生产者覆盖单槽"，结构上不如只追加队列稳。
- **实现**：改成**每源有界 FIFO**（`DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH` 默认 4）——只追加 SPSC 环，生产者＝捕获 IRQ、消费者＝T2 相位（同核）；补投按**到达顺序**逐片进行；**队列满丢最新并计入 `lane_hold_dropped_count`**（不覆盖活条目，因此不再需要 generation 防半写）；同一条记录若先由线上重复帧成功投递，队列副本**标记为已消费**（出队但不重复投递）；单条连续失败达 `DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS` 即退休并计入 `lane_hold_expired_count`，随后继续尝试下一条。
- **读出**：`SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 28 字段（新增 `lane_hold_dropped_count`）；探针新增 `position_lane_hold_dropped_delta`；文档字段表与语义同步。
- **验证（主机，本次实测）**：`test_t2_freeze_completion.py` 通过——新增：两条被拒记录按到达顺序补投（v11 先于 v12，不再是"最新优先"）、线上重复投递后队列副本不再重复投递、**满队列丢弃最新且只计一次**、单条超重试预算退休后继续处理下一条、STOP/`ack_reset` 清空队列（且用例末尾把占用的 mailbox 释放，保证后续 STOP 断言独立）。
- **RAM（宿主 gcc 度量口径）**：每源 `4 × (slice 12 B + target 4 B + attempts 4 B)` = 80 B × 8 源 = 640 B，加 head/tail（64 B）与 5 个计数 ≈ **724 B**。六轮累计席位相关新增 <1 KB，仍远小于授权范围，**未**缩波形 buffer。
- **未闭合**：SEAT-03 剩余 A'. 一等专属**独立 TX 通道**（当前仅做了控制字节仲裁，物理/载荷通道仍与特等共用，需 wire/预算核定与 `EXE-SEAT-01`）；台架复测与 P3 凭证仍未做。

### TDMA-PROGRESS-20261011-008：一等席两条声明预算与越界计数（SEAT-03 缺口 B）

- TODO task ID：`TDMA-SEAT-03`；改动：`tdma_pio_spi_ring_adapter.h`、`tdma_pio_spi_ring_origin.inc`、`distributed_refmem_position_ack.inc`、`distributed_refmem.h/.c`、`scpi_system_snapshot_commands.c`、`t2_bench_probe.py`、`tests/python/test_tdma_seat_rotation.py`、`tests/python/test_t2_freeze_completion.py`。
- **问题**：一等席原先没有声明预算——origin 侧等待只有"最大值"没有越界事实；从板侧 R2 引入的保存副本会**无限重试**（消费者长时间不取走就一直重投），既无上界也不可观测。
- **实现**：① origin 侧 `TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES`（发布机会数预算）——等待**达到**预算即记一次 `seat_first_class_wait_violation_count`（每次等待只记一次）；② 从板侧 `DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS`（补投次数预算，定义在被编译的 `.inc` 内并加 `#ifndef` 保护，使主机 harness 与固件共用同一值）——连续补投失败达预算即退休副本并计入 `lane_hold_expired_count`，成功或新记录到达时归零。
- **读出**：`SYSTem:TDMA:SEAT:COUNters?` 9 字段（新增等待越界）；`SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 27 字段（新增 `lane_hold_expired_count`）；探针新增 `seat.first_class_wait_violation_delta` 与 `position_lane_hold_expired_delta`。
- **验证（主机，本次实测）**：`test_tdma_seat_rotation.py` 通过（新增 M=8:N=1 场景：一等记录等满 8 次机会时越界计数 +1、第 9 次机会归一等、上车后等待归零）；`test_t2_freeze_completion.py` 通过（新增：连续补投失败达预算 → 只退休一次并计数、之后不再重投、被占用的 mailbox 取走后不再有残留）；lane/seat/freeze 相邻 62 项通过。
- **RAM**：origin +4 B（1 计数）；从板 +8×4 B（每源尝试数）+4 B（1 计数）= 40 B。四轮累计新增仍 <300 B，未触及"缩波形 buffer"手段。
- **未闭合**：SEAT-03 剩余 A（一等专属独立接收队列 / 独立 TX 通道，需 wire/预算核定与 `EXE-SEAT-01`）；台架复测与 P3 凭证仍未做。

### TDMA-PROGRESS-20261011-007：优先级通道拒绝按 class 归因（SEAT-03 缺口⑤）

- TODO task ID：`TDMA-SEAT-03`；改动：`tdma_priority_rx.h/.c`、`tdma_pio_spi_phys_priority.inc`、`scpi_sync_commands.c`、`tests/python/test_tdma_priority_rx.py`、`tests/python/test_tdma_priority_rx_scpi.py`。
- **问题**：lane 级拒绝原先只有一个 `reject_count` 与 `last_reject`，无法回答"一等席被拒了几条、链路整体被拒了几条"，即无法把一等席的稳定性与其它 class 的链路噪声分开。
- **实现**：新增 `tdma_priority_rx_reject_class(lane, reason, mailbox_class, class_known)`（旧 `tdma_priority_rx_reject()` 保留并等价于"class 不可信"）。快照新增 `first_class_reject_count`（class == `TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS`）、`other_class_reject_count`、`unknown_class_reject_count` 与 `last_reject_class`（不可归因记 `TDMA_PRIORITY_RX_CLASS_UNKNOWN`）；**三者之和恒等于 `reject_count`**。IRQ 捕获路径只在传输头已通过校验（MAILBOX/MAILBOX_CRC/SEQUENCE/EXHAUSTED/CADENCE）时才把 mailbox class 传为可信，更早的失败（DMA/GEOMETRY/INCOMPLETE/OVERWRITTEN/HEADER/HEADER_CRC）一律进 unknown 桶，避免把乱码算成一等损失；publish 路径（SEQUENCE/EXHAUSTED）携带已校验记录的 class。
- **读出**：`SYSTem:TDMA:FLIGHT:PRIORity?` 尾部追加四字段（该页由 23 字段变为 27 字段），SCPI 单测同步更新。
- **验证（主机，本次实测）**：`test_tdma_priority_rx.py` 通过（新增：publish 路径拒绝落入 other 桶且 `last_reject_class` 为该 class；无 class 的拒绝只增 unknown 桶并置 `CLASS_UNKNOWN`；T2 position class 拒绝单独计数；三桶之和等于 `reject_count`）；`test_tdma_priority_rx_scpi.py` 通过（字段数 27）；lane 相邻 python 用例 18 项通过。
- **RAM**：新增四个计数（16 B）+ 一个 API 参数，无缓冲。
- **未闭合**：SEAT-03 剩余 A（一等专属独立接收队列/独立 TX 通道，需 wire/预算核定）与 B（一等专属期限/背压预算）；台架复测与 P3 凭证仍未做。

### TDMA-PROGRESS-20261011-006：一等席 TX 侧控制字节仲裁（SEAT-03 缺口④的 TX 一半）

- TODO task ID：`TDMA-SEAT-03`；改动：`distributed_refmem.c`、`distributed_refmem_position_ack.inc`、`distributed_refmem.h`、`scpi_system_snapshot_commands.c`、`t2_bench_probe.py`、`tests/python/test_t2_freeze_completion.py`。
- **问题（读码，非推测）**：冻结完成状态搭在出站镜像的 `TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET`/`CONTROL_SEQ8_OFFSET` 上，而 `action_patch_mailbox()` 诊断写同样两个字节；原先镜像构建顺序是"先一等确认、后诊断"，诊断一旦激活就会**静默覆盖**一等的冻结完成事实（特等席有专用 typed body，不存在这种共享），origin 因此看不到该从板的冻结掩码，只能靠 FREEZE 重发/超时兜底。
- **修复**：镜像构建顺序改为**先 `action_patch_mailbox()`、后 `distributed_refmem_position_ack_patch()`**——交付关键的一等事实最后写入；`distributed_refmem_position_ack_patch()` 在覆盖前检查前一 opcode，若不是 `OPCODE_NONE` 也不是 T2 frozen 家族（即被诊断占用）就计入 `s_position_ack_override_count`，同一 opcode 重复写入不计。
- **读出**：`lane_ack_override_count` 追加到 `SYSTem:REFMEM:SYNC:FLIGHT:POSition?`（现 26 字段），探针按名提取为 `position_lane_ack_override_delta`。
- **验证（主机，本次实测）**：`test_t2_freeze_completion.py` 通过（新增：诊断占用 0xD1/SEQ8=7 时一等确认覆盖为 T2 frozen opcode 且覆盖计数 +1、重复写入不重复计数）；`test_distributed_action_probe.py`、`test_refmem_action_probe.py` 通过（诊断路径与重试语义未受影响）；refmem/T2 相邻 48 项通过。
- **RAM**：本次仅新增一个计数器（4 B）。累计席位相关新增仍 <200 B，无需动用波形 buffer。
- **未闭合**：SEAT-03 缺口④的剩余部分（一等专属**独立接收队列**与独立 TX 通道，需 wire/预算核定）、⑤（从板 lane 拒绝按 class 归因）、⑥（一等独立期限/背压预算）；台架复测与 P3 凭证仍未做。

### TDMA-PROGRESS-20261011-005：一等席有界接收保全（SEAT-03 缺口①第一步）

- TODO task ID：`TDMA-SEAT-03`；改动：`distributed_refmem_position_ack.inc`、`distributed_refmem.c`、`distributed_refmem.h`、`scpi_system_snapshot_commands.c`、`t2_bench_probe.py`、`tests/python/test_t2_freeze_completion.py`、`tests/python/test_t2_position_flight.py`。
- **动机（上轮实测症状）**：单深度接收 mailbox 被占用时，被拒的 PREVIEW 只能靠 origin 的线上重复重试；origin 一旦停止重复（例如该版本已上车/进入 FREEZE 阶段），该版本即永久丢失，从板只能用较旧本地解算冻结——上一批台架即出现 `freeze_mismatch_count = 3/2/0`。
- **实现**：拒收的 PREVIEW 现在保存到**每源一槽**（最新到达优先，按源 generation 写奇/偶防半写），由 T2 相位在下一个时间片经 `distributed_refmem_get_position_descriptor()` 经常规 mailbox 补投；补投成功清除副本，同一记录若先被线上重复帧成功入箱则匹配副本作废（不重复投递）。FREEZE 仍走 latch。新增计数 `lane_hold_count/lane_drain_count/lane_drain_fail_count`，随 `SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 尾部读出（该命令字段表已补全为 25 字段），并在 `t2_bench_probe.py` 中按名提取为 `position_lane_hold/drain/drain_fail` 增量。
- **RAM 实测（宿主 gcc 度量，快照非事实源）**：每源 `trigger_position_slice_t`(12 B) + generation(4 B) + target(4 B) = 20 B × 8 源 = **160 B**，加三个计数 ≈172 B；仍在"RAM 不够才缩波形 buffer"的授权范围内，**无需**动用波形 buffer。
- **验证（主机，本次实测）**：`test_t2_freeze_completion.py` 通过（新增：拒收即保存、单槽最新优先、mailbox 满时补投失败并保留、补投成功投递、线上重复入箱使副本作废）；`test_t2_position_flight.py`、`test_trigger_position_mailbox.py`、`test_tdma_runtime_mailboxes.py` 通过（其中飞行用例的 harness 增加了对补投入口的显式桩）。
- **发现（既有缺陷，非本次引入，均由 stash 对照实验在 HEAD 上确认）**：① `tests/python/test_vdc_feedback_preacquire.py` 的 118 项 parametrize 用例以 ERROR 失败——这些 harness 抽取 `distributed_refmem.c` 的单个函数时不再注入 `s_position_tx_mailbox/s_position_rx_mailboxes/s_position_freeze_latches` 与 `distributed_refmem_position_cancel_service`；② `tests/python/test_tdma_service_nonblocking.py` 的 17 项以链接错误失败——该 harness 以 `PICO_ON_DEVICE` 路径包含 seat store 头但未链接其实现（`tdma_seat_mount_store_note_reject` 未定义）。两者都与 `8bd547b2` 引入的席位插桩/位置静态量同源，属测试脚手架腐化，建议单列修复项。
- **未闭合**：SEAT-03 缺口④（一等专属 TX 保全与独立接收队列）、⑤（从板 lane 拒绝按 class 归因）、⑥（一等独立期限/背压预算）；台架复测与 P3 凭证仍未做。

### TDMA-PROGRESS-20261011-004：一等席服务事实落地 + 主机套件解阻（SEAT-03 第一步）

- TODO task ID：`TDMA-SEAT-02`、`TDMA-SEAT-03`。源码改动：`tdma_first_class.h`、`tdma_pio_spi_ring_origin.inc`、`tdma_pio_spi_ring_adapter.h/.c`、`scpi_system_snapshot_commands.c`、`tdma_service.c`、`tools/sync_trigger/t2_bench_probe.py`。
- **既有缺陷修复（先决条件）**：`tdma_service.c` 的 `tdma_seat_mount_store_note_reject` 只在 `PICO_ON_DEVICE` 下声明却在 17 处无条件调用，导致 `run_tdma_service_scheduler_tests.ps1` 主机编译失败（`8bd547b2` 引入，与已修复的 A-07 同类）。按 `trigger_t2_runtime.c` 的既有做法补主机侧空实现后，该套件恢复通过。
- **一等席服务事实（补 SEAT-03 缺口②⑤）**：`tdma_first_class_t` 新增 `stage_refused_count`（单深度邮箱有等待记录而拒绝新 stage——有界背压事实）与 `retired_waiting_count`（READY 未上车即被 route 丢失/STOP 退休——交付前损失）；`publish_priority` 在 route 丢失退休等待记录前先把它的等待值并入 `seat_first_class_wait_max`，消除"等待被静默丢弃"的观测缺口；新增两个计数经适配器快照读出，`SYSTem:TDMA:SEAT:COUNters?` 由 6 字段扩为 8 字段。
- **完整性核对（修正 SEAT-03 缺口③的原判）**：`tdma_priority_rx_validate()` 在投递给任何 sink 之前逐帧校验传输帧 magic/version/class/length/identity CRC/transport CRC 与 mailbox magic/version/class/reference slot/target 位/mailbox CRC16，只有通过者才 publish/投递。因此记录级完整性**已由通道提供**，"从板不复验"仅指不复验 T2 紧缩切片；该结论已写入 `TDMA_RUNTIME_CONSTRAINTS.md`。
- **观测面（补 SEAT-02 剩余项）**：`t2_bench_probe.py` 新增按名提取 `SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 尾部的 lane accept/duplicate/reject/owned，并把 `SYSTem:TDMA:SEAT:COUNters?` 加入采样查询，输出 `position_lane_*` 与 `seat{...}` 增量（含 stage 拒绝与交付前损失），供下一次台架运行直接给出"一等席还剩多少丢"。
- **验证（主机，本次实测）**：`run_tdma_pio_spi_ring_adapter_tests.ps1` 通过（新增 stage 拒绝/退休等待计数与快照读回断言）；`tests/python/test_tdma_seat_rotation.py` 通过（新增背压计数、route 丢失退休计数与"先发布等待再退休"断言）；`run_tdma_service_scheduler_tests.ps1` 由失败转为通过；`tests/python` 的 TDMA/SCPI 相邻 50 项通过。
- **台架状态事实（须记录）**：本轮误用 `t2_bench_probe.py --help` 触发了一次真实探针会话（脚本不处理 `--help`），产物 `out/HardwareAcceptance/20261011/t2-bench-probe-005754`（`passed=false`，错误为 STATUS 读回形状断言）。该运行对四板执行了 `TRIGger:REServation:GRID:CLEar` 与结尾 `SYSTem:TDMA:RING:STOP`（四板均 OK），即**清空了台架上原有的 GRID/T2 配置但把环停稳**；COM3–COM6 四板在线。重跑任何台架步骤前须先恢复现场配置。
- **RAM 实测（宿主 gcc 度量，快照非事实源）**：本轮与上一轮为席位能力新增的静态内存合计约 32 B/板——`tdma_first_class_t` 48 B（两个计数 +8 B）、适配器两个比例字段 +8 B、快照四个只读字段 +16 B。目标给出的"RAM 不够可缩小波形 buffer"本轮**无需动用**；后续若为一等席加有界接收深度，量级预计为每源约百字节（8 源 <1 KB），届时再与波形 buffer 一并核算。
- **未闭合**：SEAT-03 缺口①（无一等专属 RX/TX 保全与独立队列）、③'（从板 lane 拒绝原因未按 class 归因）、⑤'（一等无独立期限/背压预算）；固件与工具改动尚未取得 P3 硬件验收凭证。

### TDMA-PROGRESS-20261011-003：一等席 M:N 轮换比例的 SCPI 入口与 seat 计数页落地

- TODO task ID：`TDMA-SEAT-02`；关联新增 `TDMA-SEAT-03`。源码改动：`tdma_pio_spi_ring_adapter.h/.c`、`tdma_pio_spi_ring_origin.inc`、`tdma_runtime_owner.h/.c`、`scpi_system_snapshot_commands.h/.c`。
- **实现**：编译期常量改为带默认值的每板 STOP-only 设置——`TDMA_SEAT_ROTATION_DEFAULT_PRIORITY_SHARE`/`_FIRST_CLASS_SHARE`（1:1）、上界 `TDMA_SEAT_ROTATION_SHARE_MAX` 与 `TDMA_SEAT_ROTATION_CYCLE_MAX`，校验与回落 helpers 在头文件内联；适配器新增两个字段与 `tdma_pio_spi_ring_adapter_set_seat_rotation()`（`started` 非零即拒绝），仲裁按配置值计算 M:N，读到 0 回落默认以避免零周期；owner 侧 `tdma_runtime_owner_set_seat_rotation()` 与既有 flight-mode 同族，要求环未使能且适配器未启动。
- **SCPI**：`SYSTem:TDMA:SEAT:ROTation <M>,<N>`（成功回 `OK,<M>,<N>`，失败 `TDMA_SEAT_ROTATION`）、`SYSTem:TDMA:SEAT:ROTation?`、`SYSTem:TDMA:SEAT:COUNters?`（origin 六个席位计数）。比例不写入 ring profile/geometry，故不影响 wire CRC。
- **验证（主机）**：`run_tdma_pio_spi_ring_adapter_tests.ps1` 通过（新增默认值/2:1 读回、零值与越界拒绝、`started` 拒绝、拒绝后原值保持）；`tests/python/test_tdma_seat_rotation.py` 通过（新增 2:1 与 1:2 的领队序列、等待计数、零值回落 1:1、校验边界）；SCPI/TDMA 相邻 python 测试 50 项通过；`run_tdma_ring_runtime_tests`、`run_tdma_adapter_comm_fsm_tests`、`run_tdma_flight_overlay_tests` 通过。
- **发现（既有缺陷，非本次引入）**：`run_tdma_service_scheduler_tests.ps1` 在主机编译报 `tdma_service.c:1018` 的 `tdma_seat_mount_store_note_reject` 隐含声明——该调用由 `8bd547b2` 引入且未做主机条件编译，与已修复的 A-07 同类；`git show HEAD` 同样存在，需按 A-07 方式修（本域诊断插桩的落地范围见 `TDMA-SEAT-03` 与评审 A-07）。
- **未闭合**：从板一等 accept/duplicate/reject 并入可读面并在探针采集；`seat_first_class_wait_max` 不统计 STOP 退休记录；比例与一等扩容的 `EXE-SEAT-01` 审核 + C11 登记；**固件改动尚未取得 P3 硬件验收凭证**（本机无四板台架，pre-commit 会按 staged 源码指纹拦截）。
### TDMA-PROGRESS-20261010-002：一等席接收改走优先级通道，修复一等记录 seq16 冻结

- TODO task ID：`TDMA-FLIGHT-002K2`、`TDMA-T2-002`；提交 `3ddeb258`；用户确认直接实施，语义写入 TDMA-SEAT-01"路径分离"。
- **实测预算（`t2-bench-probe-201442`，21.6 s，快照）**：从板普通 RX 站捕获周期约 4 ms（`PROFile:RX?`，每次捕获均有 CLAMP 跳过），一等记录线上停留约一个发布机会（约 1.9 ms）；origin `next_seq32` 增量 0。优先级通道 38031 次 IRQ、序号缺口 108。
- **改动**：优先级 IRQ 同一记录交给第二接收端（一等，0x15）；一等上车推进 `next_seq32`；`SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 末尾新增通道 accept/duplicate/reject/owned。
- **结果**：T2 三从板全部动作完成（见 `T2-PROGRESS-20261010-024`）；DPLL60 `dpll60-135802` PASS。

### TDMA-PROGRESS-20261010-001：TDMA-SEAT-01 快速路径按发布机会轮换落地，T2 位置记录跨环送达

- TODO task ID：`TDMA-FLIGHT-002K2`、`TDMA-T2-002`；提交：语义 `2109963d`（用户已审核）、实现 `8bd547b2`。
- **根因（实测 + 读码）**：origin 启用特等 provider 时，READY 与 EMPTY 均视为占座（`tdma_pio_spi_ring_origin.inc`），普通 flight FIFO 永不被 Core1 消费，T2 记录停在 `FIFO_READY`。此前“每 4 ms 只有一次发射机会”是 Core0 RefMem 对普通路径的节流：发布间隔取 `cycle_period_ns × node_count`（`distributed_refmem.c` 的 `ring_interval_ms`），不是快速路径容量。
- **快速路径容量实测（快照，非事实源）**：`READ:CALibration:ORIGin:RUNTIme?` 的 `published_generation` 对比 `boundary_sequence`，四板 1 ms profile：发车 0.9998 ms，快速路径发布约 2.13 ms（实现后约 1.9 ms），即每 4 ms 约两次机会（`t2-bench-probe-164300`、`t2-bench-probe-183247`）。采样已固化到 `tools/sync_trigger/t2_bench_probe.py`（`origin_cadence_summary`）。
- **实现**：`tdma_first_class.h` 一等邮箱（单深度、单写者、CAS 发布，STOP 退休）；`publish_priority` 按机会 1:1 轮换主等级、主等级无新内容即让座、EMPTY 不占座、特等去重比较自身上次上车；删除 `tdma_flight_fifo_core0_patch_tx_ready` 与 `tdma_service_patch_flight_tx_ready`。新增计数 `seat_opportunity/priority/first_class/priority_yield/first_class_reject/first_class_wait_max`（位于 origin 状态，尚未接 SCPI）。
- **验证**：主机 `test_tdma_seat_rotation`（EMPTY 不占座、单独特等全占、1:1 交替、不重发旧事件、等待记录不被覆盖、延后重试、坏记录退休、STOP 退休）与相邻 459 项通过；FIFO C 单测通过；DPLL60 PASS（`dpll60-102638`），特等未退化。
- **四板 T2（单轮 12 s，快照）**：NO1 位置记录经快速路径上车；三从板 `rx_complete_count` 6/1/1，SYNC_TRIGGER 零隔离。
- **过程中暴露并修正**：`19bb91dd` 在从板接收路径加入 Core1 逐位 CRC32（21 字），从 XIP 执行使从板 SYNC_TRIGGER 最大 77643 拍（预算 26800）而被隔离；改为仅按 prediction version 去重，并把 `accept_remote_position`/`deadline_receive` 放入 SRAM，最大降至约 1.5–1.8 万拍。RAM 余量由调试波形探针瘦身释放（`VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS` 400→360，3360 B），链接余量 4464 B。
- **更正 `TDMA-PROGRESS-20261009-010` 的根因读数**：该条把 `SYSTem:TDMA:EVENT:Recovery?` 的 `last_failure_reason=9` 解读为 `TDMA_GEOMETRY_OBSERVER_FAULT`；该字段来自 `o->reason`（`tdma_pio_spi_phys_event.inc` 的 `tdma_event_recovery_record_failure`），类型为 `tdma_event_reason_t`，9 即 `TDMA_EVENT_SEQUENCE`，且恢复只在 SEQUENCE 时拉起。短槽反复恢复的直接原因是观察者帧序号跳号（`tdma_event_observer.c` 要求逐帧加一），start-cut 退役不是已证实根因。原条目保留不改写。
- 未决：从板收到位置但尚未冻结/执行动作；NO3/NO4 少量 RX 拒绝（FRAGMENT，推测为接收邮箱未及取走）；seat 计数接 SCPI 只读页。
### TDMA-PROGRESS-20261009-012：席位挂载表全链落地（专用持久化分区 + 运行时窗口判定）

**已实现并硬件验证**：席位作为"容量"、负载挂载于席位的语义以**挂载表**承载
（`components/tdma/inc/tdma_seat_mount_store.h`：`tdma_seat_mount_table_t`、
`tdma_seat_mount_crc()`、`_encode`/`_decode`/`_lookup`/`_seat_valid`），
数据模型与 CRC 已与主机参照实现交叉验证；发布路径只做"**席位 vs 席位**"比较
（窗口不匹配即让出视图重试），不再表达"负载 = 席位"。

- **持久化载体**：记录经 `flash_store_nvs_plan_append`/两段提交（正文 + 提交页）写入
  `FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_*` 指向的**专用 `SEAT_MOUNT` 分区**
  （`config/flash_map_v1_compat.json`、`config/flash_map_v2.json` 新增；
  `tools/flash_map/flash_map.py` 重新生成）。迁移原因：此前借用 `SCRATCH` 尾部时，
  记录会在平台/OTA 暂存活动后被擦除（四板实测 `loaded` 由 1 回落为 0）。
  迁移需同步 OTA 布局断言（`components/ota_manager/inc/ota_partition.h`：
  OTA 部署布局改为"止于席位挂载分区"）。
- **耐久性已验证**：四板写入后 `loaded=1`、表项正确；经历一次完整 picotool 烧录 + 验收运行后
  再次读回仍为 `loaded=1`（开机载入路径 `tdma_seat_mount_store_init()` 位于 TDMA owner 初始化链、
  ARM 之前）。
- **配置入口**：`TRIGger:REServation:SEAT:MOUNT <gen>,<crc>,<count>,…`、`:MOUNT?`、`:CLEar`
  （`middleware/scpi_port/src/scpi_trigger_seat_commands.inc`，含 CRC 校验与参数校验）。
- **配套修复（使能链阻塞项，均属平台闸门）**：
  ① `config/flash_raw_call_allowlist.json` 登记新的原始 flash 读取者；
  ② 构建源码登记（`CMakeLists.txt` 显式列源）；
  ③ flash 事务 AO 的**来源白名单**（`flash_transaction_ao.c`）需加入席位挂载来源；
  ④ 事务层按来源的"分区 × 范围 × 长度"规则（`flash_transaction_fb.c`）需为新来源补分支——
     缺 ③④ 时写入会以 `ERROR_POLICY`/`ERROR_PERMISSION` 静默失败。
- **可观测性**：为本链路与后续使能链建立了"诊断通道"（复用 `SEAT:MOUNT?` 的 last_reject，
  基线 + 首个触发者优先），使每一步拒绝原因可一次读出；相关临时插桩需在目标达成后清理或规范化。
- 备注：本次改动触及 flash 映射与事务层，提交前需按仓库约束跑 P3 硬件验收与文档门禁；
  进展条目与代码分离提交。

### TDMA-PROGRESS-20261009-011：分段流式调度**规划层**（已单测，未接物理路径）

**已实现（纯逻辑，主机单测 13 组全过；固件构建 exit 0）**：
`tdma_loop_plan()` 在环路内按 **特等席 PRIORITY → 一等席 FIRST_CLASS → 灵活/诊断 LOAD** 顺序排布发射窗口，
仅当剩余仍含完整窗口（含 close guard）时才追加 LOAD；`tdma_loop_plan_slot_for_time()` 提供
"某时刻允许哪一档发射"的判定；`tdma_loop_plan_default_request()` 给出生产默认值
（环路 4 ms、发射周期 1.5 ms、guard 占位 30 µs、**第三次关闭**）；`start_offset_ns` 支持窗口前移压缩。

**关键约束（实测契约）**：席位层 `tdma_seat_admission_begin()` 要求窗口 `close_guard_ns` 非零，
且窗口在其结束前 guard 即关闭 ⇒ **可用发射段 = [start, end − guard)**，声明 1.5 ms 时可用 1.47 ms。

**接线与实测（对应 TDMA-FLIGHT-002K2）**：已在 origin 特等席发布路径加入窗口门条件
（`tdma_pio_spi_ring_origin.inc`，编译期开关 `TDMA_LOOP_SCHEDULE_ENABLED`，**默认 0**），
并在 `ORIGin:RUNTIme?` 追加锁存计数 allow/block（静态累计 + 只读 getter，**不受 origin 结构 memset 影响**）。
同档 A/B（1 ms、同配置，实测快照）：开关 ON 时参考板 allow/block ≈ 15084/15611（≈49%/51%），
matched 合计 23352、23467（两次）；OFF 时 matched 合计 23977 ⇒ **ON 相对 OFF ≈ −2%**；
当前**无第二类发射者**（一等席/LOAD 未挂载）⇒ 判定**保持默认关闭（冻结，待消费者）**。
第三次发射的启用须先按 `EXE-SEAT-01` 提交预算清单（含 guard 实测值）并经用户审核。

### TDMA-PROGRESS-20261009-010：VDC/DPLL 周期感知修复（实机确认）与 DPLL60 剩余阻塞＝板端 trace 记录为 0

**已实现并经实机确认的修复（VDC/DPLL 运行在环的 slot period）**：
`vdc_domain_build_tdma_runtime_schedule` 采用 `binding->cycle_period_ns`；当周期不等于
`VDC_DOMAIN_DEFAULT_PERIOD_NS` 时**按同比例重排作者态窗口**（作者态为绝对 ns：refmem `[20 µs,820 µs]`、
idle beacon 起点 `900 µs`，见 `components/vdc_domain/inc/vdc_domain.h:13-29`）；采用
`binding->effective_schedule_crc32` 并用 `tdma_operating_profile_compose_schedule_crc32`
对**规范重建的 identity** 做交叉校验；校验改用 period 无关的 `vdc_domain_active_schedule_validate`，
同时 `vdc_domain_schedule_validate` 在 `operating_profile_crc32 != 0` 时委派给该 active 校验器
（该函数被激活、包络、紧凑观测、窗口/环计划等所有运行期消费者共用，故一处生效）。
**实机证据**（level 22/750 µs）：`SYSTem:SYNC:VDC:DCO?` 的周期为 `750000` 且 `tdma_schedule_crc32`
等于环侧 `SYSTem:TDMA:RING:CONFig?` 的 schedule CRC；`tdma-stopped-handoff.json passed=True`
（provisional 门通过）；`vdc_domain` 主机单测全绿。

**DPLL60 的剩余阻塞（本轮定位到唯一布尔项）**：`dpll-four-board-short/capture/input-probe.json` 中
`decoded.records` 长度 = **0**，而 `tools/workflows/vdc_joint_capture/internal_seal.py:148-175` 的
`native` 项要求 `bool(records) && tick_hz > 0` ⇒ `input-probe.passed=False` ⇒ `internal_health_passed=false`；
外部示波器在 60 s 内每个采样窗均 `missed`。同批数据中 `guard{schema:3,target_s:60,state:4}`、
`internal_assessment{crc_verified,schema_ok,interval_ok,terminal:True,freeze_reason:1}`、
`trace_arm_ack.status{sample_interval_ms:10000}`、`trace_stop_ack.status{state:3,command:2,reason:1}`、
`trace_release_ack.status{state:0,command:3,reason:6}` 均与工具期望一致 ⇒ **工具侧"仍按 1 ms 比对"的假设被排除**，
剩余解释是**板端未产出样本**（与全档位环健康症状一致：`fifo_tx_reject_grew`、从机
`tx_publish_reject_count` ≈96–98k、RX 观测率 33–44/s）。

**同批保留的其它改动**：`components/sync_trigger/src/trigger_grid_preview.c` 的
`grid_matches_vdc_period()` 由"必须等于 1 ms 默认值"放宽为非零互等；`SYSTem:TDMA:FLIGHT:FIFO?`
追加 TX 发布拒绝分因计数（`tx_reject_bad_payload_count`/`_ring_full_count`/`_no_slot_count`，页面 20 字段，
`tools/tdma_ring_monitor/flight_bitmap_validate.py` 同步）。

**DPLL60 短槽根因（量化，2026-10-09/10 实测对照）**：同一台架、同一流程下，
1 ms 档**全程 PASS**（`PASS_WITH_WARNINGS`，ERROR=0，含 four-board DPLL 60 s 采集），
而 750/500 µs 档失败。用**现成**的恢复页 `SYSTem:TDMA:EVENT:Recovery?`
（handler `middleware/scpi_port/src/scpi_tdma_commands.c:54`，字段序 pending, failure_count,
attempt_count, enable_count, deferral_count, cancel_count, cancel_reason, service_max_us,
last_failure_epoch, last_failure_reason）对照从机读数：
| 项 | 1 ms（通过）| 750 µs（失败）|
|---|---|---|
| attempt / enable / failure | **1 / 1 / 1** | **133 / 122 / 122** |
| deferral / cancel | 0 / 0 | 11 / 0 |
| service_max_us | **190** | **357** |
| `MATCH:STATus?` matched | **9340 / 9403** | **0** |
| `last_failure_reason` | 9 | 9 |
其中 9 = `TDMA_GEOMETRY_OBSERVER_FAULT`（`components/tdma/inc/tdma_frozen_geometry.h:25-35`），
其条件为 `s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_RETIRED`
（`components/tdma/src/tdma_pio_spi_phys_event.inc:1899-1901`）。
⇒ 结论：1 ms 下观察者**只武装一次并保持活跃**；750/500 µs 下被恢复状态机**反复重武装上百次**，
每次都迅速以 FAULT 结束，从未留下稳定 live ⇒ `priority_match_live_valid` 失败 ⇒
从机 match 全被 `SOURCE(9)` 拒 ⇒ DPLL 无相位 ⇒ 采集 60 s 全 missed。
`service_max_us` 190→357 µs（占 750 µs 周期的 48%、500 µs 周期的 71%）是量化依据：
观察者的武装/服务/退役生命周期按 1 ms 量级设计，无法在短周期内完成并保持。
**FAULT 的直接条件（2026-10-10，代码级）**：`TDMA_GEOMETRY_OBSERVER_FAULT`
（`components/tdma/src/tdma_pio_spi_phys_event.inc:1899-1901`，条件为
`s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_RETIRED`）之所以在短槽下反复出现，是因为
start-cut 以 **epoch 不匹配**被退役：同文件 `:1087-1091` 中
`s_tdma_pio_spi_rx_arm_epoch != cut->arm_epoch` ⇒ `TDMA_RX_START_CUT_ARM`、
`s_tdma_pio_spi_rx_sequence.observation_epoch != cut->observation_epoch_after` ⇒
`TDMA_RX_START_CUT_OBSERVATION_EPOCH`；另有 `:1080-1086` 的角色/persona 不符时以
`TDMA_RX_START_CUT_GEOMETRY` 退役（从机 persona=13 时通常不触发）。
⇒ 短槽下 arm/observation epoch 推进快于 cut 捕获值 ⇒ 每次检查都不匹配 ⇒ 退役 ⇒ 观察者 FAULT ⇒
恢复状态机重启（churn 133 次）⇒ 无稳定 live ⇒ match 全 SOURCE(9) 拒；1 ms 下 epoch 与 cut 同步
（attempt=1）⇒ 稳定 ⇒ PASS。修法 ① 的具体形态：让 observer 窗口以 **cut 捕获的 epoch** 为准
（或仅在 epoch 回退/非法跳变时退役），而不是任何不等即退役。

修法方向：① 让 live 跨多个环周期保持（start-cut 退役与周期解耦）；② 压缩/分片观察者服务耗时（WCET）；
③ 放宽恢复状态机在短周期下的重启条件。验证判据：attempt/enable 回到个位、`last_failure_reason ≠ 9`、
`MATCH matched > 0`、bin `success_count > 0`、采集出现触发。

**DPLL60 首因插桩结果与根因链（2026-10-09 晚，实测）**：为 `SYSTem:VDC:PRIORity:MATCH:STATus?`
追加"首个真实拒绝原因"（`vdc_priority_match.h` 增字段、`vdc_priority_match.inc` 在 `!bound` 时锁存、
页面 append-only 暴露；工具 `tools/workflows/vdc_joint_capture/match.py` 的 `TRAILER` 同步），
在 level 22 静止树的运行（`out/HardwareAcceptance/20261009/p3-014827`）读得：
**三块从机 `first_reject_reason = 9`（`VDC_PRIORITY_MATCH_SOURCE`），参考板为 0**。
据此逐跳定位（均为 `file:line` 实证）：
`vdc_priority_match.inc:220-221`（`!priority_match_live_valid()` ⇒ SOURCE）
→ `:108-113`（`required = TDMA_EVENT_LIVE_RETAINED|ACTIVE|ANCHOR_VALID`，且 arm_epoch/record.epoch/tick_hz 非零、
   `timer1_enable_after >= before`）
→ `tdma_pio_spi_phys_event.inc:505-522`（`tdma_event_live_retire()` 会清 ANCHOR_VALID 与 ACTIVE）
→ `:524-537`（monitor 触发 retire 的四条件：`rx_arm_valid` 假、`arm_epoch` 不匹配、`anchor.epoch != observer.epoch`、
   或时间戳时钟非"当前"）
→ `tdma_pio_spi_phys_event.inc:543-553`（`flags = sampled && after >= before ? ANCHOR_VALID : ANCHOR_UNAVAILABLE`，
   并可能被 `TDMA_EVENT_LIVE_CLOCK`/`INVALID` 覆盖）
→ `components/tdma/inc/tdma_event_enable_anchor.h:33-56`（`sampled=false` 的全部条件：clock_before/after、
   高位翻转、`upper <= lower`）
→ `components/vdc_domain/src/vdc_timestamp_clock.c:133-143`（时钟判据**无周期依赖**，已排除其为本因）。
**当前嫌疑**：`s_tdma_event_observer.state == TDMA_EVENT_ACTIVE` 与武装/观察者世代对齐
（下一处读取：`components/tdma/src/tdma_pio_spi_phys_event.inc` 内 observer 状态与 `rx_arm_epoch` 的置位点）。
**观察者不启动的具体门（2026-10-09 深夜，代码级 + 运行中实测）**：运行中几何采样（`input-probe.json`
的 `geometry` 字段，由本次新增的 per-board 运行中查询得到）显示，三块从机为
`persona=13`（= `TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER`，`components/tdma/inc/tdma_pio_spi_phys.h:224`）、
`training_samples=2`（= `TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES`），即
`tdma_pio_spi_phys_event_selected()` 与对齐两项门槛**均已满足**；但 `observer_epoch/state/reason/prefix`
**全为 0**（运行中亦为 0，且 reason=0 ⇒ 未走到 CS 失败分支）。追至
`tdma_event_tap_freeze()`（`components/tdma/src/tdma_pio_spi_phys_event.inc:225-241`）：
使能 tap 的有效性要求
`selected && **config->geometry_generation == 0u** && phys->baud_hz != 0 &&
prefix_bits + 32 <= physical_bytes * 8`（未使能 tap 则直接有效）。
⇒ 若冻结 tap 时几何世代已非 0，则 `tap_valid=false` ⇒ `s_tdma_event_waiting=false`
（观察者 ARM 路径 `:1613`）⇒ 观察者永不启动 ⇒ 无 live ⇒ `priority_match_live_valid` 失败 ⇒
**`first_reject_reason = 9 (SOURCE)`** ⇒ matched=0 ⇒ DPLL60 未完成。
修法方向：**A** 流程侧把"应用事件 tap"提前到几何世代为 0 时；**B** 固件侧放宽该门
（使能 tap 不应以几何世代为 0 为前提，或按已应用几何坐标校验）。
另记：观察者 ARM 路径已把环周期纳入（`s_tdma_event_period_ns = config->cycle_period_ns`，`:1610`），
故"观察者不懂短周期"这一可能已被排除。

**观察者/几何诊断页与"运行中采样"方案（2026-10-09 夜）**：`SYSTem:TDMA:RING:GEOMetry?`
（handler `middleware/scpi_port/src/scpi_system_snapshot_commands.c:3902`，共 36 字段，末四为
`observer_epoch/observer_state/observer_reason/observer_prefix_bits`）可读，但**停环后观察者字段清零**
（实测从机末四 = 0,0,0,0；身份项 schedule/profile CRC 正确，如 1283010877 / 2743669232）。
因此判定 `tdma_event_start()`（`components/tdma/src/tdma_pio_spi_phys_event.inc:1621-1631`）的失败 clause，
必须在**运行窗口内**取值：在 `tools/workflows/vdc_joint_capture/**` 或
`tools/vdc_priority_trace/vdc_priority_joint_capture.py` 的 preflight（ARM 之前）与 STOP 之后各查一次该页，
把 36 字段存入 per-board 行（新键 `geometry_pre`/`geometry_post`）；或在固件侧于 STOP 时锁存 observer 状态
并以 append-only 方式暴露。任一方式落地后，即可在 `waiting / tap_applied_valid / CS pad+GPIO（会写
observer_reason = TDMA_GEOMETRY_OBSERVER_CS） / 对齐` 四 clause 中指名。
（该工具在 acquisition 期间禁止查询，故必须"ARM 前 + STOP 后"两次采样或固件锁存。）

另注：运行期间**树必须静止**——该验收会校验收据源码指纹，运行中改动文件会导致
`provisional receipt source fingerprint differs`（`p3_dpll_short.py:879`）而失败。

**更正（2026-10-09，两个只读诊断代理的实证）**：
① 前文"板端 trace 记录为 0（`decoded.records=0`）⇒ `native` 项失败"**不成立**：`record_count=0` 取自
`trace_arm_ack.status`（ARM 时刻快照，早于任何 bin 提交），而各板 `capture/NO*/decoded.json` 为
`record_count=6`、`tick_hz=250000000`，且 `guard_checks[*].checks.native` 全为 true；
`input-probe.json` 中并无 `records` 键。真实失败的检查项是 `verdict` 与 `health`。
② 前文"域周期已随环（实机确认）"所依据的运行 **并不含周期修复**：以
`out/build/p3-acceptance-20261009-005437/DHRT100.elf.map` 的
`.text.vdc_domain_build_tdma_runtime_schedule` = 0x11c（284 B，旧版）与修复后
`p3-acceptance-20261009-011432` 的 0x234（564 B）对照为证（`vdc_domain.c` mtime 晚于该构建）。
③ 真实故障链（同一批工件）：环观测按**环周期**构造（`tdma_pio_spi_ring_adapter.c:236-244`），却在
`vdc_ring_observer.c:107-111` 按 **域 schedule.period_ns** 做同余准入 ⇒ 旧版固件（域固定 1 ms）下
全部观测被拒 ⇒ 从机 `SYSTem:VDC:PRIORity:MATCH:STATus?` 呈 `calls=28810, matched=0, rejected=calls`、
各 bin `NO_SUCCESS|UNBOUND` ⇒ `priority_summary_success` 从不递增 ⇒ `internal_health_passed=false`。
参考板的 26697 成功来自 TX encode（`PRIORITY:TX? encoded` 相同），不经 match。
④ `servo.update_period_us=1000`（`vdc_domain.c` 内 `vdc_domain_default_servo`）在两批运行中**都存在**
（硬件读回 `SYSTem:SYNC:VDC:DPLL:COEFficient?` 的 `pi` 字段），故其**不是**本故障原因；
其唯一影响是伺服时间常数与 `servo_profile_crc32`。
⑤ 仍存在的 1 ms 硬钉：`components/sync_trigger/src/trigger_grid_preview.c` 的另外两处（除已放宽的
`grid_matches_vdc_period()` 之外），以及 `vdc_run_output.inc` 的 RUN 周期约束。
⇒ 因此**最小修复仍是让 VDC 域采用环周期**（已写入的 `vdc_domain` 改动），
并把该固件**真正烧入后**重跑验收（本轮 `p3-005437` 之后的运行尚未在 DPLL 阶段取得有效证据；
`p3-acceptance-20261009-011432` 那次在执行到 TRN-00 MARK 时因台架瞬态失败）。

**DPLL60 根因（本轮定论，证据链已闭合）**：采集的 trace 属 **priority trace guard**
（`components/vdc_dpll_manager/src/vdc_priority_trace.inc`，ARM 入口
`middleware/scpi_port/src/scpi_sync_commands.c:959`）。其 ARM 在
`origin_invalid || interval_invalid` 时直接判 `REJECTED`（`guard.state=4` / `reason=1`=BINDING，`:256-262`），
而 `origin_invalid` 的判据是 `vdc_priority_tx_origin_trace_eligible_core1()`
（`components/vdc_dpll_manager/src/vdc_priority_tx.inc:44`）要求 ARM 携带的 generation/session 与
当前 `priority_sync_generation` / `feedback_session` 匹配（`session` 由**参考纪律**建立）。
实测两批对照：level 7 通过侧 `guard.state=3`/`stop.reason=8`/`freeze_reason=8`，
level 21/22 失败侧 `4`/`1`/`1`（其余字段与 arm 参数 `schema=10, interval_ms=10000` **完全相同**），
且 `freeze_reason=1` 等价于纪律 profile 以 `enabled=0` 施加（`vdc_domain.c:2032-2034`）。
**决定性证据**：`tools/hardware_acceptance/**`、`tools/workflows/vdc_joint_capture/**` 与
`config/tool_workflows/**` 中**均无** `SYST:VDC:REF:DISC`（参考纪律启用命令）的发送；固件侧
`vdc_dpll_manager_set_reference_discipline()` 也**只有** SCPI handler 一个调用者
（`middleware/scpi_port/src/scpi_reference_commands.c:88`，无自动启用路径；其前置要求参考后端已停，
见 `vdc_reference_config.inc:57-69`）。⇒ **验收流程从未启用参考纪律 ⇒ 无 feedback session ⇒
trace ARM 资格失败 ⇒ 无记录/无标记 ⇒ 示波器 60 s 全 missed ⇒ DPLL60 未完成**。
最小改法：在 DPLL 采集前按顺序补"停参考后端 → （可选）`REF:DISC:CONF` → `REF:DISC 1` → 复核读回"
（注意 `vdc_reference_config.inc:107-108` 的"请求待决时拒绝配置"互锁），落点
`tools/hardware_acceptance/p3_dpll_short.py` 的 preflight。

**待查（下一步）**：板端 trace 记录的产出条件与 `reason=1`/`reason=6` 的枚举含义；
`vdc_domain_default_servo` 的 `update_period_us = 1000u`（`components/vdc_domain/src/vdc_domain.c:1597`，
哈希进 `servo_profile_crc32`）是否必须随 slot period；以及激活后再做档位扫描会被
`components/distributed_refmem/src/distributed_refmem.c` 的计划周期一致性校验拒绝的潜在陷阱。


### TDMA-PROGRESS-20261009-009：DPLL60 在 500/750 µs 的阻塞根因（VDC 域周期固定 1 ms）与两次失败尝试

**实测根因（直接证据）**：环与 VDC 域的周期不一致——
`SYSTem:TDMA:RING:CONFig?` 的周期字段为 **750000**（level 22），而
`SYSTem:SYNC:VDC:TDMA:STATus?` 仍报 **1000**（µs，即 VDC 域自有的 1 ms 周期，
常量 `VDC_DOMAIN_DEFAULT_PERIOD_NS`，`components/vdc_domain/inc/vdc_domain.h:13`，于 `vdc_domain.c:1468` 写入）。
⇒ 500/750 µs 下 DPLL/VDC 标记与观测窗口无法对齐 ⇒ 示波器 60 s 内每个采样窗均 `missed`、
`internal_health_passed=false` ⇒ 采集 `Capture exited 2`。1 ms（level 7）下 `dpll60` 曾 PASS，正因周期恰好相等。

**已达成且保留的改动**：① provisional 门修复（去掉 `vdc_domain_build_tdma_runtime_schedule` 中
"域 CRC == 绑定 effective CRC"的等式，并保持域自有周期）——经实机验证：500/750 µs 可跑完 TDMA 阶段
（`tdma-stopped-handoff.json passed=True`）并进入 DPLL60 阶段；② `sync_trigger` 的
`grid_matches_vdc_period()` 由"必须等于 1 ms 默认值"放宽为"非零互等"；
③ TX 发布拒绝的**分因计数**（bad_payload/ring_full/no_slot，页面 `SYSTem:TDMA:FLIGHT:FIFO?` 追加至 20 字段，
工具 `flight_bitmap_validate.py` 同步扩展）。

**两次失败尝试（均已回滚，构建绿）**：
- A2：让域采用环周期（`updated.period_ns = binding->cycle_period_ns`）+ 采用环侧 CRC +
  改用 period 无关的 `vdc_domain_active_schedule_validate()`；自哈希校验 `:1781`
  （`schedule_crc32 == vdc_domain_schedule_crc32(profile)`，含 period）是其不可通过的根。
- A2′：在 A2 基础上按同比例重排作者态窗口——因为作者态窗口是**绝对 ns 且按 1 ms 排布**：
  refmem `[20 µs, 820 µs]`、idle beacon 起点 `900 µs`、guard 1 µs（`vdc_domain.h:14-29`）。
- 两次在实机上**同点回归**：`TDMA stopped handoff incomplete: board_records board set mismatch`
  （TDMA 阶段中止）⇒ 说明除窗口区间外还有未识别的前置条件（下游消费者或激活路径中的 CRC 自洽要求）。

**结论与交接**：让 VDC/DPLL 运行在非 1 ms 周期属**语义级改动**（域调度 + period-hashed CRC 定义 + 可能
ring 侧 CRC 口径），需先用**离线探针**（编译生产源码、直接调用生产 API，不占台架）指名"A2 路径第一个
return false 的 file:line"，再一次性实施；避免继续硬件盲试。


### TDMA-PROGRESS-20261008-008：短槽"起不来"的证据链被否证（起环工具时序缺陷）+ 配置解析缺陷

**结论先行**：此前关于"375/500 µs 槽起不来"的全部证据**无效**——它们出自
`tools/tdma_ring_monitor/tdma_start_ring.py`，而该工具在两处缺陷下**任何档位都起不来**。

- **缺陷一（时序，已修）**：工具原序列为 `… → ARM → wait_started(等 adapter_started) → TRAIN → START`，
  而适配器只有 `TRAIN + START` 之后才会启动 ⇒ 该等待**永不满足**，每次都以
  `ARM timeout / ring_adapter_started=0 / tx_count=rx_count=0` 结束。
  **对照实验**：在已知可用的 **level 7（1 ms、四板）** 上用同一工具，得到**逐字段相同**的失败
  ⇒ 该失败签名是工具性质，与槽长无关。
- **缺陷二（APPLy 首次被拒，已加重试）**：`OPMode:APPLy` 紧随 `STAGe` 发出时首轮常被拒
  （`apply_count=0, reject_count=1, last_result=1`），第二次即成功；工具已改为重试 4 次。
- 修复后序列可推进到 **TRAIN**，并在从机给出具体失败
  `TRAIN chunk 4096 failed after 0/4096 cycles, response='<timeout>'`；
  同条件在 **level 7 两块板**上**完全相同**，且在块大小 4096 与 64、读取超时 3 s 与 20 s、
  TRAIN 置于 ARM 前或后等组合下**均为超时无回包** ⇒ 与槽长无关。
  文档确证根因（`docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md:181-186`）：训练流程要求
  **先 ARM 所有 follower，再 ARM master**，follower 进入 `FORWARDING/FORWARD_ARMED` 且 master
  保持 RX capture armed 后才接受训练请求；而起环工具按 `start_order` 统一 ARM 并对**全部节点**
  发 TRAIN 指令，顺序与角色均不符 ⇒ 板端不回包。该工具因此只适合装载与 ARM 级别操作，
  训练须由专用编排器 `tools/calibration_ring_validate/calibration_clk_train.py` 承担。

**另一处缺陷（配置解析，已修）**：`tools/hardware_acceptance/p3_hardware_acceptance.py` 的
`acceptance_config_path()` 对 `run` 命令**忽略 `--config`**（`run` 恒取 `p3_bench.json`（`--full`）
或 `p3_bench_quick.json`）⇒ 以 `--config p3_bench_level22.json` 启动的链路**实际按 level 7 运行**
（产物名 `*-level7*`、`summary.json: level=7`）。已修为"显式 `--config` 优先，默认行为不变"，
并以模块级调用验证三种输入的解析结果。

**本轮新增档位**：`level 22 = {10 MHz, 750 000 ns, 4096}`（四文件同前例：档位表 + COUNT 22→23 +
`PROFILE_FACTS[22]`），并派生出 `trn03-matrix-level22.json`（`profile_crc32 = 2743669232`，
与板端 `STAGe 22` 回读一致）。配套验收配置：`p3_bench_level{21,22}.json`。

**仍未判定**：短槽（375/500/750 µs）究竟能否起环——需要**有效路径**（trn03 全链）在目标档位下取证；
该档位的校准阶段凭据必须由板端在装载时生成（派生矩阵无法通过 `CAL:TRAINing:STAGe:BEGin`）。


### TDMA-PROGRESS-20261008-007：on-cadence 开关实测生效（RX 侧计数器），但发射节拍仍由 origin/TX 侧决定

**更正 -006 的度量口径**：`READ:CALibration:ORIGin:RUNTIme?` 的 `published_generation` 属 **origin/TX** 路径
（由 `tdma_pio_spi_ring_origin.inc` 的 TX 映像 acquire 与 `phys_origin` 发布推进）；本轮改动的闸门位于
`tdma_pio_spi_ring_adapter.c` 的 **RX 镜像**发布点，其计数器是 flight FIFO 的 `rx_publish_count`
（`SYSTem:TDMA:FLIGHT:FIFO?`）。两者不相交，故 -006 用 `published_generation` 比较**看不见**被改的路径。

**用正确计数器复算（两臂均为既有工件，trn03 已解析）**：
- 基线（on-change，`out/HardwareAcceptance/20261008/origin-baseline-l7-01`）：参考节点
  `rx_publish_count` 872 → 1527，Δ655 / 19.75 s ≈ 33.2 /s，`rx_publish_drop_count` 0。
- on-cadence 臂（`out/HardwareAcceptance/20261008/origin-oncadence-l7-02`）：869 → 1739，Δ870 / 19.89 s ≈ 43.7 /s，
  `rx_publish_drop_count` 0。
⇒ **开关确定生效（+33%）**，即 D1 字段贯通与 site A 落点均为活路径。

**但两条限制仍然成立**：
- RX 发布速率（33–44 /s）比环边界（1.000 ms ⇒ 1000 /s）低约 30×，说明 RX 发布路径另受更强条件约束
  （候选：`tdma_flight_engine_rx_unload()` 的 `inspected`、`tdma_receive_health_evaluate()` 的接受条件、
  或收帧到达率；均待代码追溯）。
- 同一两臂中 origin 侧 `published_generation` 均约 4 ms（250 /s）且未变 ⇒ 发射节拍不由 RX 闸门决定，
  4:1 量化的杠杆在 **origin/TX 侧的"每环回一次发布机会"**。

**待办**：① 追溯 RX 发布的决定性限制条件；② 追溯 origin/TX 每环回机会的设置点，判断是否可让参考节点
每 1.5 ms 获得一次发布机会（表/调度层），或是否必须缩短环回（375 µs 槽在当前帧长/线速下已实测起不来）。


### TDMA-PROGRESS-20261008-006：on-cadence A/B 实测为负 + 独立复核纠正三项实现缺陷

**实测结论（同条件、两臂均达 AUTONOMOUS）**：开启 on-cadence 闸门**未改变**发射节拍。
- 基线（on-change，`out/HardwareAcceptance/20261008/origin-baseline-l7-01`）：`published_generation` 3.9996 ms、
  `boundary_sequence` 0.9998 ms、AUTONOMOUS 68/68、`rejected_observation_count` 0。
- on-cadence 臂（`out/HardwareAcceptance/20261008/origin-oncadence-l7-02`，工具发
  `SYSTem:TDMA:RING:DIAGnostic 1,1`）：`published_generation` **4.0014 ms**、`boundary_sequence` 1.0005 ms、
  AUTONOMOUS 71/71。
⇒ "RX 新段（on-change）闸门是发射节拍限制器"这一假设**未被实测支持**；`published/boundary` 比值恰为 4.000，
更符合"参考节点每环回只有一次发布机会"的量化。旁证：D 类实测每 1 s 区间 `input_bytes` 增量为 0 而
publish/acquire 仍以 ~217–220/s 推进；失败面在 TX 侧（`fifo_tx_reject_grew`，从机
`tx_publish_reject_count` 约 92% vs 参考约 20%，`tx_image_stale_count == tx_reuse_count`）。

**独立复核（只读）纠正了三项实现缺陷，均已修正并构建通过**：
- D1 字段未贯通：`origin_cadence_enabled` 在 `tdma_ring_runtime_configure`/`read_config` 往返中丢失，
  且 `service_config` 未初始化 ⇒ 已加运行期状态成员、两分支读写与零初始化。
- D2 可选参数判定错误：`scpi_port_read_u32()` 是**必选**变体，缺参时 libscpi 推入
  `SCPI_ERROR_MISSING_PARAMETER` ⇒ 既有单参数调用方会读非空错误队列；已改用
  `SCPI_ParamUInt32(..., FALSE)`。
- 落点错误：原先放宽的发布闸门位于**转发路径**（参考节点在该路径恒不可达）⇒ 判定为死代码并已回退；
  正确落点为参考侧收帧发布点（`process_rx_impl` 内的 FIFO 发布），以 **present/output 掩码**放行
  （new 掩码为空时不得发"空包"：`segment_mask == 0` 会被 refmem 消费者与 `rx_commit` 跳过），
  并以 `comm_fsm.state == AUTONOMOUS` 为守卫。

**待办（交接）**：① 复核方四问的答复（TRN-03 在 setter 之后是否重建/清零 staged 配置、ARM 是否经
`read_config`、site A 是否同一 `adapter->config` 实例）以判定负结果有效性；② 若判"证据不足"则新增只读回读
（新命令，不改既有元数）后重做 A/B；③ 若判"有效"则改追环回/槽的发布机会与 TX 侧压力；
④ `--flash picotool` 后 p3 报 `online board set mismatch`（四板实际均在线）导致该路径拿不到凭证，需在烧录后
重新发现在线集合；⑤ 收口需聚焦测试 + P3 凭证 + 代码/文档分离提交。


### TDMA-PROGRESS-20261008-005：自激发射节拍闸门（on-change→on-cadence）与六类协作方式

- TODO task ID：`TDMA-FLIGHT-002B1`、`TDMA-FLIGHT-002B`、`TDMA-EMISSIONCLOCK-01`；日期：2026-10-08。
- 本条是**独立记录**，不覆盖 `TDMA-PROGRESS-20261008-004`：004 负责闸门位置归因与"1.5 ms 需策略+调度两件事"，本条记录本轮新增的固件开关、档位与工具入口，并按两层重述语义边界。002B1 的 TODO 单元已按同一口径改写并自洽（见 `docs/tdma/TDMA_DOMAIN_TODO.md` 的 `TDMA-FLIGHT-002B1` 行）。
- **自激语义层（reference-only）**：
  - 发射权只属参考节点：`tdma_pio_spi_ring_adapter_resident_process_image()` 要求 `role == TDMA_PIO_SPI_RING_ROLE_REFERENCE` + `FORWARDING_PHYSICAL_PROCESS_IMAGE` + flight engine active；许可侧 `calibration_manager/src/calibration_origin_timing.inc:117-118` 要求 `local_slot_id == reference_slot_id`。本轮台架只有 NO1 受理 `TRIAL:READY`。
  - 主机只做四件事：许可（`CAL:ORIG:TRIAL[:READY]`）、授权/释放（`RELease`，授权要求 `burst_limit == 0` 且 `config_seq` 不变）、只读回读、`REVOkE` 回收。主机**不参与逐拍发射**。
  - 发射由**环返回**触发：许可 → `RELease` → AUTONOMOUS，之后每周期 `rx_once → observe(boundary) → publish`（`components/tdma/src/tdma_pio_spi_ring_origin.inc` 的 `tdma_pio_spi_ring_origin_service`）。
- **当前真实闸门 = on-change（快照，非事实源）**：
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c:1893-1921`：只对 `TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE` 且 `receive_health.configured != 0` 的帧调用 `tdma_flight_engine_rx_unload()`，把 `unloaded.new_segment_mask` 存入 `adapter->last_rx_new_segment_mask`。
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c:2286-2300`：仅当该掩码非零（或下述 cadence 开关放行）才 `tdma_flight_fifo_core1_publish_rx()` 发布进 flight FIFO。
  - 发射侧去重：`tdma_pio_spi_ring_origin.inc:234-282` 按 `(generation, sequence)` 去重，并要求 `flight_engine->map_generation == job->layout.map_generation`。
  - 实测后果（level 7，1 ms 槽、四节点）：`returned.sequence` 每 1.0001 ms 前进、`observed_return_count` 每 3.111 ms、`published_generation` 每 3.9996 ms——即发射按变更（≈每环回一次新段）而非按节奏。
- **新增 opt-in 开关（本轮固件切片）**：`tdma_ring_runtime_config_t.origin_cadence_enabled`（`components/tdma/inc/tdma_ring_runtime.h:162`）；`tdma_pio_spi_ring_adapter.c:2290（该处放行经独立复核判定为**死代码**并已回退；on-cadence 的实际放行实现尚待落到参考节点/origin 服务路径，且发布须用 present/output 掩码而非 new 掩码）` 在其非零且 `config.local_slot_id == config.reference_slot_id` 时无新段也放行发布。默认 0 = 保持历史 on-change 行为。on-change ↔ on-cadence 属**闸门策略**选择。
- **环前提层（工程约束，不是发射语义）**：全环节点共享同一 profile 与 schedule，**每槽工作量必须装进槽长**，否则环起不来；`TDMA-EMISSIONCLOCK-01` 要求低于可达下界的 profile fail closed。最小发射间隔由环物理（槽长 × 节点数 + 每槽工作量）界定，属可调参数，不写进发射语义。
- **档位 20/21 与 375 µs 档的反证（快照，非事实源）**：`tdma_operating_profile.c` 新增 20 = {10 MHz, 375000 ns, 4096}、21 = {10 MHz, 500000 ns, 4096}。板端接受 `SYSTem:TDMA:OPMode:STAGe 20` 并返回 `20,10000000,375000,4096,0,111157511`（与工具 `profile_crc32(20)` 一致），但 `tools/tdma_ring_monitor/tdma_start_ring.py --level 20` 报 `ARM timeout`、`ring_adapter_started=0`、`tx_count=rx_count=0`（失败点 follower slot 1），`SYSTem:ERR?` 为干净无错误、无显式拒绝码。结论：375 µs 档在当前帧长/波特率下每槽工作量装不进槽 ⇒ **缩短环回不是解法**，需表级调度/窗口算术另行设计。
- **工具与换档约束**：`tools/calibration_ring_validate/trn03_matrix.py` 新增 `--derive-from/--source-level`，用已测物理行给另一档位重打戳（拒绝同时改波特率）。阶段记录 `calibration_crc32` 由**板端对结构体计算**（`tdma_origin_calibration_crc.c`、`calibration_origin_timing.inc`、`tdma_runtime_origin.inc`），主机无法重打戳：**换档位必须由板端重新生成阶段凭据，不是线侧重校准**。
- **环前提层的接线约束（每板开关不得改 ring CRC）**：发射 cadence 开关**不能**放进 `tdma_ring_profile_t.flags`——该 `flags` 被 `tdma_ring_profile_crc32()`（`tdma_profile.c:94`）哈希进 `config.ring_profile_crc32`，随后序列化进 wire 帧（`tdma_transport_frame.c:177,290`）并在 RX 侧校验（`tdma_pio_spi_ring_adapter.c:1764`、`tdma_pio_spi_ring_origin.inc:30`）。若参考节点带一个每板位而转发节点不带，两边的 ring profile CRC 不再相等，帧会被直接拒绝。故本开关设计为**每板 staged runtime 字段** `tdma_service_ring_runtime_config_t.origin_cadence_enabled`，经与 `tdma_service_set_loop_delay_ns()` / `tdma_service_set_ring_diagnostic_mode()` 同族的 **STOP-only setter** 写入（`tdma_service.c:647,667`）；环配置字面量 `tdma_service.c:533-552` 有意省略该字段，因此默认 0、保持历史行为（这也是 `tdma_ring_runtime.h` 而非 profile 头承载该字段的原因）。
- **主机面（快照，实现中，冻结契约前须复核拼写）**：设计为**既有命令的可选第二参数** `SYSTem:TDMA:RING:DIAGnostic <diag_continue>[,<origin_cadence>]`，SCPI 入口 `middleware/scpi_port/src/scpi_system_snapshot_commands.c:3811-3821`（当前 `tdma_ring_diagnostic` 与 `tdma_ring_diagnostic_q` 仍为单参数/单响应元数），保持响应与 `RING:DIAGnostic?` 元数不变以免影响既有主机；退路是更窄的 `SYSTem:TDMA:RING:ORIGin:CADEence <0|1>` 一对。固件实现进行中。
- **Agent 协作方式（自本轮起）**：六类任务分工，每类一个常驻 agent，新工作用消息派发——**A 固件实现** / **B 主机工具与验收链** / **C 文档与契约**（本条作者）/ **D 台架实验** / **E 审查复核** / **F 构建烧录凭证**。类别边界用于派发与复核归属，不改变各任务的 C11 交叉审核要求。
- **术语消歧（防混淆，登记切片须沿用）**：本轮 cadence 指**闸门 cadence**（on-change ↔ on-cadence 的发布闸门策略），与既有**物理 cadence** 是两个概念：后者是 `tdma_origin_cadence_t` / `tdma_origin_cadence_calculate()`（`components/tdma/inc/tdma_origin_plan.h`，其头注释明确 "describes the physical cadence only"，字段为分频、guard 与每帧周期上下界，`tdma_origin_plan.c` 计算）。因此 `origin_cadence_enabled` 在契约文本中必须显式限定为闸门侧；若 A 在实现中改名以避开 `tdma_origin_cadence_t`，登记时同步记录该命名决定，两概念在任何条款里不得混用。
- 未决/下一 gate：`origin_cadence_enabled` 的实板 on-cadence 验证（含参考节点独占性与从机无扰）；可达下界与 fail-closed 门禁的代码化；002B1 仍为 PENDING，待语义层与环前提层分别取证后收敛。

### TDMA-PROGRESS-20261008-004：自激发车发射节拍的真实闸门与解耦结论

**结论**：发射节拍既不等于槽长、也不等于环回，而是**由过程映像"是否出现新段"决定**（on-change），
在静态台架上≈每环回一次，因此实测表现为 `published_generation` 250 Hz（3.9996 ms）。

- 闸门链（`components/tdma/src/tdma_pio_spi_ring_adapter.c`）：
  `:1893-1921` 仅对 `TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE` 且 `receive_health.configured != 0`
  的帧调用 `tdma_flight_engine_rx_unload()`，把 `unloaded.new_segment_mask` 写入
  `adapter->last_rx_new_segment_mask`；`:2286-2300` 的 `tdma_flight_fifo_core1_publish_rx()`
  准入即 `last_rx_new_segment_mask != 0`。
- 发射链（`tdma_pio_spi_ring_origin.inc:234-282`）：`core1_acquire_tx()` 按
  `(generation, sequence)` 去重 → `grant_tx()` → 异步 overlay 准备 → `commit_tx()` 后
  `published_generation` 递增；READY 分支还要求 `flight_engine.map_generation == job->layout.map`。
- 同轮实测三计数不同拍：`returned.sequence` 1.0001 ms（1 kHz）、`observed_return_count`
  3.111 ms（321 Hz）、`published_generation` 3.9996 ms（250 Hz）——与 on-change 闸门一致。
- 由此，"达到 1.5 ms 发射间隔"需要两件独立的事（均与线速/槽长无关）：
  ① **策略**：把发射由 on-change 改为 on-cadence（按节奏发当前映像），闸门点即
  `last_rx_new_segment_mask` 与 publish 的去重条件；② **调度**：为参考节点提供每 1.5 ms
  一个不与从机冲突的发射窗口（表级设计，需要重做 `schedule_crc32` 与调度器窗口算术）。
- 反证（同轮硬件）：375 µs 槽档（level 20）**profile 可用但环起不来**——
  板端接受 `SYSTem:TDMA:OPMode:STAGe 20` 返回 `20,10000000,375000,4096,0,111157511`
  （与工具派生 `profile_crc32(20)` 一致），但 `tdma_start_ring.py --level 20` 在
  7 s 与 45 s 两种等待下均报 `ARM timeout`，`ring_adapter_started=0`、`tx_count=rx_count=0`，
  失败点为从机 slot 1（`FB276192BEF9CCE1`）；`ERR?` 为 `0,"No error"`，即无显式拒绝码。
  因此**缩短环回不是本问题的解法**。
- 工具：`tools/calibration_ring_validate/trn03_matrix.py` 新增 `--derive-from/--source-level`
  重打戳入口（深遍历替换 `profile_level`/`cycle_period_ns`/`profile_crc32`，写入
  `derived_from{level,source,physical_measurements_reused:true}`）；派生产物
  `out/HardwareAcceptance/20261008/trn03-matrix-level20.json`（`config.profile_level=20`）。
  注意：阶段记录的 `calibration_crc32` **由板端对结构体计算**（`tdma_origin_calibration_crc.c`
  + `calibration_origin_timing.inc:152` + `tdma_runtime_origin.inc:168,195`），主机无法重打戳，
  故换档位必须由板端重新生成阶段凭据。


### TDMA-PROGRESS-20261008-003：自激发车（origin）入口五层链路与运行期可达性

- 契约：TDMA-FLIGHT-002B1（docs/tdma/TDMA_DOMAIN_TODO.md）当天修正为 **可达路径 = process-image**，
  依据 `tdma_pio_spi_ring_adapter.c:1187-1196`（role=REFERENCE + FORWARDING_PHYSICAL_PROCESS_IMAGE + flight engine active）。
- 入口五层与实测：
  1) 许可 `CAL:ORIG:TRIAL[:READY] <trial>,<rearm>,<polls>,<duration_ticks>`：仅**参考节点**受理，
     成功返回固件内部 epoch；要求 `DIAGNOSTIC_CONTINUE` 置位（`calibration_origin_timing.inc:115-116`）。
  2) 授权 `tdma_runtime_owner_origin_authorized()`（`tdma_runtime_origin.inc:145-163`）：要求 burst_limit==0
     （`SYSTem:TDMA:RING:BURSt 0`，读回 0 确认）、config_seq 与 ring_runtime 一致、epoch 等于当前 epoch 等 15 条。
  3) 释放 `CAL:ORIG:RELease <epoch>,<config_seq>`；许可后不得再装载/应用配置（否则 config_seq 变更即失效）。
  4) 准入 `tdma_runtime_owner_origin_begin`（`tdma_runtime_origin.inc:218-248`）：需回环帧匹配
     （transport_sequence==up_sequence、identity_crc32==up_tx_frame_crc32，`components/tdma/src/tdma_pio_spi_ring_origin.inc:117-120`），
     成功后写 release=PREPARING 并 `tdma_origin_handoff_begin`。实测 `origin.active=1`、`returned.sequence/identity` 已记录、
     `READ:…:HANDoff?` 由 UNAVAILABLE 变为可见且 `result=0`。
  5) 准备→AUTONOMOUS：准备阶段机由**环周期**驱动（`components/tdma/src/tdma_pio_spi_ring_origin.inc:164` → `components/tdma/src/tdma_runtime_origin.inc:281`，每周期最多 4 步），
     需 `RESIDENT_INSTALLED`（`tdma_adapter_comm_fsm.c:239-244`）才转 state 5；只有 state==5 时才进入
     `tdma_pio_spi_ring_origin_service()` 的 rx_once→observe(boundary)→publish（`components/tdma/src/tdma_pio_spi_ring_origin.inc:383-405`）。
     实测 `comm_fsm_state` 恒为 **6 (RESIDENT_PREPARING)**，从未到 5；`boundary.*`、`observed_return_count` 恒 0。
- 关键实测约束（快照，非事实源）：
  - `duration` 单位是 **ticks**（250 MHz）：2.5e9→约 10 s、1.5e10→60 s；`elapsed_ticks` 曾饱和 UINT32_MAX。
  - 60 s 窗口下 `active=1` 可持续约 34 s，但 `invalid_count` 达 56348（每周期被判无效，来源待审查确认）。
  - 环启动前必须**生产软复位**（否则 recorder state=5 拒绝 CANCEL，阶段中止在前置处）。
  - 阶段（诊断模式 + --leave-running）在 realtime gate 失败时可能以 STOP 收尾（`trn03_closed_loop.py:374-382`：
    handoff 记录为空则不允许 leave-running）⇒ 随后的许可必然被拒（实测 trial-page 回到 `0,2,0,…`、FSM=STOPPED）。
- 工具治理：自激发车会话编排已固化为 `tools/bench/tdma_origin_session.py`（stable 根，登记 TOOL-BENCH 的
  entrypoints 与 files 两处）；TRN03 保持纯环验证（复位不进入 TRN03）。
- 未决（已开三条只读审查 + 后续独立 cross-reviewer）：RESIDENT_PREPARING→AUTONOMOUS 不可达是固件缺陷、
  还是"许可/释放必须发生在环运行期"的操作条件缺口。

### TDMA-PROGRESS-20261008-002：稳态/过渡分列与常规 DPLL-60 验收凭证

- TODO task ID：`TDMA-FLIGHT-002J`、`TDMA-FLIGHT-002K`、`TDMA-HIL-001`；日期：2026-10-08。
- 仪表化（未提交）：`SYSTem:TDMA:PROFile:STEady?` 新增**稳态记录**——相位若调用 `ring_start/stop_adapter`、`ring_train`、`adapter_stop/disarm` 或任一 `arm_*` 即判为过渡，不进入稳态峰值；follower 的发送侧（`overlay_prepare`/`overlay_boundary`）纳入 TX 分组；解析器支持按整表周期派生 `cycle_wait_ticks = cycle − TX − RX`，`service_timing_snapshot()` 用板端 `schedule.cycle_cycles` 直接落进证据。
- 实测快照（build `20261008074530`，数值为本轮快照、非事实源）：**稳态 `steady_peak`** 四板 1154.8–1209.6 µs，对 850 µs 压缩后设计预算超 36–42%；TX/RX/周期等待分别为参考板 442.0/326.3/731.7 µs、follower TX 135.2–168.0、RX 749.1–839.1、等待 492.8–615.7 µs（`phase_wait` 仅 0.3 µs，优先通道未激活）。**ARM 过渡** `peak` 1227.3–1678.1 µs，其中 `ring_start_adapter` 1033–1361 µs（`arm_select_persona` 282–313、`arm_configure` 239–264 µs），每会话一次。
- 更正上一条目口径：先前记的稳态 95–111 µs 取自 `PROFile?` 的 `last`，那是 STOP 后的安静拍；环运行窗口内的稳态最坏相位就是一次完整收发服务拍，因此"超预算"不是 ARM 造成的假象。
- 常规硬件验收凭证：新增 `p3_hardware_acceptance.py dpll60`（收据 schema `HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_DPLL60_V1`、scope `DPLL60_JOINT`），复用已成功 package/OTA，经一次有限 TDMA 阶段应用冻结矩阵并产出四板 STOP 原始交接，再跑四板 DPLL 60 s。本轮复用路径 114.5 s（对照完整 quick P3 480.9 s）；`check-staged` 接受该收据；staged 改动命中 `DPLL_SHORT_UNCOVERED_PREFIXES`（校准/flash/存储/sync IO/OTA 工具/示波器路径）时门禁拒绝并要求完整验收，范围声明同步写入 `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 的 EXE-CHANGE-01。
- 顺带修复的既有测试缺陷（与本轮改动无关）：三处 harness 缺 `components/sync_trigger/inc` 包含路径（`distributed_refmem.h` 引入该头）、两处切片 harness 需要 timing 空实现前导、一处 ARM 尾部锚点因计时包装需放宽。

### TDMA-PROGRESS-20261008-001：席位准入模型提交与同源码四板 quick P3 复跑

- TODO task ID：`TDMA-FLIGHT-002K`、`TDMA-FLIGHT-002K1`、`TDMA-FLIGHT-002J`、`TDMA-HIL-001`；日期：2026-10-08。
- 提交：模型切片 `f1170dd9`（`CMakeLists.txt`、`tdma_seat_admission.h/.c`、host/单元测试及硬件收据），文档切片 `2bf03c50`（按代码/文档分离约定单独提交）；均推送至 `wip/tdma-real-flight-processing` 并与远端同步。
- 同源码硬件复跑（数值为本轮快照、非事实源，凭证以 `config/hardware_acceptance/p3_acceptance_receipt.json` 为准）：`.../p3-tdma-seat-admission-02/`（`build_id=20261008034144`、指纹 `6659300f…`，随 `f1170dd9` 提交）与加入逐阶段采集后的 `.../p3-tdma-timing-03/`（`build_id=20261008042320`、指纹 `4baf9777…`、1543 文件）均为四板 quick P3 `PASS_WITH_WARNINGS`、`ERROR=0/FATAL=0`；P0–P3、T0–T3 与 DPLL 阶段通过，TDMA 阶段 `PASS_WITH_WARNINGS`。最终凭据保留两项诊断失败：`four-Node TDMA closed loop` 与 `acceptance time budget`（整轮耗时超过 quick profile 的 450 s 错误门限；该 profile 余量本就紧张，逐阶段采集本身只增加约 20 次只读查询）。
- 与 9-28 轮的差异：`coded marker calibration level 7`、`TRN-01 SCK`、`TRN-03 SCK replay row selection` 三项本轮通过，收据内仅保留 `four-Node TDMA closed loop` 一项原始诊断失败。该差异只说明本轮训练、校准与运输流程完成，不能宣布严格 TDMA、锁相、零缺帧或 resident 全窗口通过，也不重判 9-28 轮原始失败。
- 相位体墙钟快照（口径为 `app.c` 的 `runtime_cycles = app_realtime_elapsed_cycles(start,end)`、`overrun = runtime_cycles > contract->wcet_cycles`，本轮快照、非事实源）：同轮内 TDMA 相位 `max_runtime_cycles` 四板分别达 343230 / 369169 / 339004 / 349448 拍（按 250 MHz 约 1372.9 / 1476.7 / 1356.0 / 1397.8 µs），而该相位 `wcet_cycles` 为 158000 拍；`overrun_count` 随样本单调增长（NO4 由 339 增至 1685），`schedule_miss_count` 占 `cycle_count` 的 30%–52%。该数字是 `app_realtime_tdma_phase()` **整个相位体**的合计（TDMA 传输、RX observer 收割、VDC run-output、SYNC_IO reference、逻辑分析仪、诊断记账），不是 TDMA 服务耗时；环形往返不在其中（TX 提交即返回、帧尾忙等约 18 us、DMA 停止与 TX put 自旋上限 64 / 500 us），因此不得把该数读成"TDMA 用掉 1.3 ms"。
- 相位体归因（同日追加；STOP 后只读读取四板 `SYSTem:TDMA:PROFile?` / `:PEAK?` / `:RUN?` / `:OTHer?` / `:RX?`；数值为本轮快照、非事实源）：**稳态周期相位**（`last`）约 95–111 µs，远低于该相位 632 µs 服务预算，主要由 `owner_service`（46–55 µs）与 `ring_runtime`（28–38 µs）构成；**过渡相位**（`peak` 与 `OTHer?` 同值）约 1194–1594 µs，`ring_runtime` 占 1107–1384 µs 而 `ring_publish` 仅 20–40 µs、`adapter` 未记账，属 STOP/ARM/配置的 stop-start adapter 路径，不是周期服务；同一固件的只读探针另捕获到**自治 origin 相位**（`RUN?`，`entry_state` 低位为 2）峰值约 1.26 ms，其中 `adapter` 约 784 µs（`origin_publish` 约 470 µs、`rx_handoff` 约 183 µs、`rx_parse` 约 167 µs），即发射与接收处理在同一次调用内完成。因此 `phase_max_runtime_cycles` 出现 1.2–1.7 ms 是粘性最大值混合三类相位的结果，不能与稳态 WCET 直接比较：**预算本身未见写错，需要改的是评价口径与分类预算**。自本轮起 `trn03_closed_loop.py` 在四板 TDMA 阶段一并采集 `last`/`peak`/`RUN`/`OTHer`/`RX` 五条记录作为只读证据，不新增门禁。
- RX/TX 事实（同轮末样本，逐节点区间）：RX 副本 `rx_publish_count` 647–699、`rx_acquire_count` 646–699，而 `rx_parse_count` 为 0/1/1/0、`rx_queued_count` 0–1；TX 侧 `tx_publish_reject_count` 825–1777 高于 `tx_publish_count` 935–1016，`tx_reuse_count` 0–4。这与 `TDMA-FLIGHT-002J` 要求的 RX 到达峰值、FIFO 高水位和 Core0 消费峰值一致：现有计数不足以证明接收侧闭合。
- 已推送但此前无独立条目的预算事实切片：`3d8045f6` 暴露方向性周期预算快照（`tx_reserved_bytes_per_cycle`、`tx_remaining_bytes_in_cycle`、`rx_budget_defined`）；`99a4f052` 暴露 RX FIFO 容量与可用槽位及发布/丢弃/获得/释放/排队/解析计数，并明确其为固定 FIFO 事实、不等同每周期 RX wire budget。`traffic_scheduler_rx_budget_defined` 仍由代码置 0。
- 失败原件：同日首轮因四板串口未枚举在 OTA 入口失败（`candidate_ports` 为空、`alarms.json` 为 `BLOCKED`），原件保留在 `out/HardwareAcceptance/20261008/p3-tdma-seat-admission-01/`，未生成或改写任何凭证。
- 下一 gate：`TDMA-FLIGHT-002K2` 物理 selection/提交边界、VDC 新事件即时撤销与跨板无扰动；随后按同代绑定实测 TX/RX/CPU/反向预算。父任务 `002K` 保留 IN PROGRESS，不因纯模型测试与 quick P3 关闭物理窗口接线。

### TDMA-PROGRESS-20260928-001：席位准入软件模型与物理接线边界

- TODO task ID：`TDMA-FLIGHT-002K`、`TDMA-FLIGHT-002K1`、`TDMA-FLIGHT-002K2`；日期：2026-09-28。
- 变更：新增 `tdma_seat_admission.h/.c` 并加入构建；同 epoch/cycle/config/profile/binding/priority 身份贯穿特等选择、提交、一等窗口与 load 选择/提交。TX/RX、保留槽、Core1/IRQ、Core0 消费及反向流量独立校验，一次性扣费；未接真实硬件窗口，不修改 VDC provider 或 PIO。
- 独立复核：`/root/tdma_seat_review` 只读审查发现成功 begin 的输入别名被清零，以及后端晚交接的剩余 duration 未复验；主控修复并补用例，复核确认无剩余阻断项。真实晚交接事实保留但撤窗，不能写成正常发送。
- 软件证据：`python -m pytest tests/python/test_tdma_seat_admission.py -q -p no:cacheprovider --basetemp out/pytest/tdma-seat-admission-r2-20260928` 通过；生产 C 的五组用例覆盖顺序/重复、各维度缺项与不足、旧身份/撤销、时间/整数边界和后端失败（本轮快照）。既有 scheduler/FIFO/service host runner 均通过，目录为 `out/pytest/tdma-seat-existing-*`。
- 构建与硬件：当前 P3 在 `out/HardwareAcceptance/20260928/p3-tdma-seat-admission-02/` 执行；结束后按原始分级、staged 指纹和 STOP 交接补齐结论。此前请求复用环序在入口被当前 scope 拒绝，未进入硬件；已使用正常 quick 入口，未修改门禁。
- 下一 gate：完成当前源码 quick P3、指纹提交推送及文档门禁；随后独立收敛物理 selection/commit 证据和同代实测预算。父任务保留 IN PROGRESS，不因纯模型测试关闭物理窗口接线。

### TDMA-PROGRESS-20260919-001：主域文档按当前实现收敛与历史轮转

- TODO task ID：`TDMA-FLIGHT-002`、`TDMA-FLIGHT-002G`、`TDMA-FLIGHT-002I`、`TDMA-HIL-001`。
- 日期：2026-09-19；本轮仅文档代码对照与日志轮转，无固件、工具实现或硬件运行变更。
- 对照：按 TDMA owner、异步准备、typed 特等席固定记录保全及 Core1 MATCH/FOLLOW 路线更新架构和任务；可配置整表周期、编译容量与 STOP 选节点邮箱布局按现有代码描述，不将配置目录等同长档循环验收。
- 重点：继续收敛自主飞行、逐圈装卸期限、完整静态预算、资源与受控退出；现有四板 DPLL 物理闭环不等待首帧精细优化或 NO5，正式 VDC 资格仍独立验收。
- 已有证据：引用 `VDC-PROGRESS-20260919-024` 及 `out/HardwareAcceptance/20260919/p3-reference-profile-r2/`，其源码指纹为 `d31246fd8b07f02a9e68c8680116e751420daf37ac4de01a07ac013ccd55e3f2`（原轮快照，非本轮新凭证）。该轮 PASS_WITH_WARNINGS，TDMA diagnostic 为 true，原始 passed/closed_loop/realtime 为 false；本次不改写其结论。
- 轮转：按日期倒排，保留同日原顺序；最旧连续日期段迁入归档，逐条正文及未编号历史段保留，索引列出范围和条数。轮转前后 ID、字节摘要及正文一致性见 `out/doc-audit/tdma-refresh-20260919/rotation-audit.json`。
- 验证与下一 gate：本轮 `docs_check --strict-names`、`doc_regression_check`、文档 pytest、强制文档 pre-commit、`git diff --check` 及轮转审计均通过；本条不代表新 P3、WCET 或锁相实测通过。继续依各任务退出条件推进，失败原件及历史状态不重判。

