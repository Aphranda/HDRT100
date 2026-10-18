# TRIGGER T2 任务进展归档 09

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_09.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-19

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20261016-007` .. `T2-PROGRESS-20261015-004`（按新鲜度倒排，最新在前），共 10 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。

### T2-PROGRESS-20261016-007：引线扫到 10% 后残差只剩 1–2 个 `LATE`，瓶颈转为**接收侧的排空速率**；已改为每遍有界批量排空 + cursor 失效也排空

- TODO task ID：`T2-DEPLOY-003`。日志 `out/tdma_t2/steps-20261016-013950.log`（`T2_SCAN_OVERRIDE_THRESHOLD_PERMILLE=900`，10% 引线，`--no-flash`）。
- **引线从 20% 收到 10% 的结果**：`failed steps: none`；**SWITCH_A `361/361/361/359`（`late 2`）**、**SWITCH_B `361/361/361/360`（`late 1`）**——`LATE` 由 5% 引线时的 38/39/36 收到 **1–2**，说明引线预算判断成立；但 **GATEWAY 再次 `0/0/0/0`**，且其 `lane_accept 66312` 只有另两块（134717/134513）的**一半**，`lane_reject 241299`、`hold_drop 214643`。
- **定位**：三块板收到的是**同一广播**，接受量却差一倍 ⇒ 不是分发差异，而是**该板的消费者排空得慢**。位点：`trigger_t2_runtime_service()` 在 cursor 无效时**先返回、后不排空**——`accept_remote_position()` 只在那之后调用，于是 cursor 每次瞬态失效（非 sticky、本板更频繁）期间该板的 lane 完全不被排空，有界 hold 环填满并开始丢**新鲜**记录；再加上**每遍只排空 1 条**（1 条/ms ≤ 源端 ~1.1 条/ms 的到达率），单深度邮箱长期占满。
- **修复（两处，同一函数）**：新增 `drain_lane(current_ms)`——`deadline_receive()` + **每遍最多 `TRIGGER_T2_RUNTIME_RECEIVE_DRAIN_MAX`(=4) 条** `accept_remote_position()`；正常路径用它；**cursor 无效路径也调用**（`drain_lane_without_cursor()`，用最后一次已知的粗序号作准入时间戳），使瞬态失效只**推迟准入/布防**、不再**停掉运输**。**门控**：只在"本轮已建立过时基"（`last_sync_sequence` 非零）时排空——从未绑定过的板不排空（既有 `zero_pulse_index` 用例钉住的不变量：未绑定时不得接纳记录）。
- **宿主证据**：新增 `burst_drain`（3 条排队记录必须在**一遍内**全部消费）与 `blip_drain`（cursor 失效时排队记录仍被消费、admission 仍等 cursor）；**反证已做**：把 `TRIGGER_T2_RUNTIME_RECEIVE_DRAIN_MAX` 临时置 1 ⇒ `burst_drain` 立即红（`remote_queue_head==remote_queue_len` 断言失败）。T2 运行时/页序/位置飞行 **38 passed**。
- **真板结果（`out/tdma_t2/steps-20261016-015655.log`，重烧四板含本修复，10% 引线）**：**三块从板首次全部消费且负载均衡**——`SWITCH_A 361/361/361/355/6`（`late 6`）、`GATEWAY 361/361/361/355/6`（`late 6`）、`SWITCH_B 361/361/361/354/7`（`late 6`、`backend 1`）；`lane_accept` 三块几乎相同（**49507 / 48194 / 49497**，此前是 134k/66k/134k 的失衡），**`hold_drop` 由 176k–214k 降到 3.3k–4.4k（约 40×）**，`tx_publish 61963`。⇒ "一块板被接收侧压垮"的机制**已消除**；残余只剩每板 6–7 个 `LATE`（执行覆盖 354–355/361 ≈ 98.1–98.3%）。
- **同轮暴露的两处工具脆弱性（已修，`tools/sync_trigger/t2_iterate.py`）**：采样末段两块板的页读超时（`win ?/?...`）时，判定器在 `got.group(6)` 上抛 `AttributeError` 直接崩掉（把"读数不可用"变成"判定器异常"）；现改为按槽记录 totals/rejects，缺读数时打印 `?` 而不崩。
- **未核验**：把最后 6–7 个 `LATE` 收掉（下一轮：13% 引线 `threshold=870` 复跑，`pwsh-179`）；`DRAIN_MAX=4` 的相位预算与更优取值；`backend 1` 的个案归因。









### T2-PROGRESS-20261016-006：20% 引线复跑——步骤全绿、两块 361/361/361/361，第三块被**接收侧拥塞**打停（拒绝非 T2 逻辑）；引线的收益与代价已量化

- TODO task ID：`T2-DEPLOY-003`。日志 `out/tdma_t2/steps-20261016-012740.log`（同参数复跑 `--no-flash`，`T2_SCAN_OVERRIDE_THRESHOLD_PERMILLE=800`）。
- **结果**：`failed steps: none`；`slot 1 SWITCH_A 361/361/361/361`、`slot 3 SWITCH_B 361/361/361/361`（两块复现零丢失，五类 fail 全 0）；**`slot 2 GATEWAY 0/0/0/0`**、`lane_accept 65950`、**`hold_drop 176043`**、`lane_dup 107063`、`FREEZE latched 1382 / taken 1379 / kept 387`、`rx_pending_mask 1`、`desc_reject 0`、`last_error 5`(CURSOR)；`STICKY CURSOR FAULT` **0**。
- **判读**：GATEWAY 并非被 T2 逻辑判失败（五类 fail 全 0、无锁存、无 desc_reject），而是**它的接收路径被压垮**：20% 引线让源端每个位置的解算窗口更长，`tx_publish` 由 5% 引线时的 ~50k 涨到 **155k**（约 3×），三从板的 `lane_dup` 达 107k–179k、`hold_drop` 28k–176k；GATEWAY 那一块因此极少拿到"新鲜片"，其槽位 floor 早已被推进，收到的 1379 个 FREEZE 全被判为过期占用者的残留（早退、不计拒绝），窗口始终为 0；同时其 cursor 频繁瞬态失效（`last_error 5`、无锁存）⇒ 服务多数时间在推迟。
- **收益/代价（本轮结论）**：**引线必须覆盖从板流水线时延**（5% 引线时残差 100% 是 `LATE`；20% 时两块板零丢失），**但引线越长、PREVIEW 流量越大**，触及**每板有界接收队列**（深度 4）的容量上限——一块板一旦被压垮，本轮它就完全不消费。⇒ 最终一步落在**车道流量/每板接收容量**（TDMA 域：PREVIEW 去重/按位置只送最新、或加大有界队列），不在 T2 位置语义。
- **两轮 20% 引线对比**：第一轮 `steps-20261016-011618`（三块全 361/361/361/361，但 step 8 在 slot 0 被瞬时拒绝）；第二轮（本条目）步骤全绿但一块被拥塞打停。⇒ **能力已证（存在三块同时零丢失的一轮），但尚不可重复**，且可重复性取决于引线/流量平衡。
- **未核验**：能同时满足"引线够长"与"流量不过载"的参数点（候选：引线 850–900、源端 PREVIEW 去重、`DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH` 加深）；B5 版本余量仍未取终值。









### T2-PROGRESS-20261016-005：**361 全程零丢失达成**——三从板各 `361/361/361/361`（executed = entered），零 abandoned、五类 fail 全 0；根因是**宣告引线（lead）预算**

- TODO task ID：`T2-DEPLOY-003`。承接 `-004` 的原因拆分：残差 100% 是 `LATE`（`late/expired/no_plan/conflict/backend = 38/0/0/0/0`、`39/0/0/0/0`、`36/0/0/0/0`），即**arm 时该位置的绝对 deadline 已经过去**——不是 T2 逻辑丢位置，而是**流水线时延超过了宣告引线**。
- **引线的量级**：`T2:CONFigure` 第 7 参数 `threshold_per_mille` 决定引线（`lead = (1000 − threshold)‰` 的位置区间）。原配置 `950` ⇒ 仅 **5%** 引线；台架扫描约 2.4 位置/s ⇒ 引线约 **20 ms**，而从板需要"车道送达 + Core0 解算 + FREEZE + arm"全程走完，约 10% 的位置来不及。
- **把引线做成可调（工具）**：新增 `T2_SCAN_OVERRIDE_THRESHOLD_PERMILLE`（默认 950，不改既有行为）。**以 800（20% 引线）重跑 361 全程轮**（`out/tdma_t2/steps-20261016-011618.log`，固件未变、`--no-flash`）：
  `slot 1 SWITCH_A 361/361/361/361/0`、`slot 2 GATEWAY 361/361/361/361/0`、`slot 3 SWITCH_B 361/361/361/361/0`、源端 `361/361/361/358/0`；
  **五类 fail 全 0**（`0/0/0/0/0` ×3）、`ACTION execution 3 of 4`。⇒ **"361 个位置正常发布 + 各从机接收、解码并执行序列动作"在台架上首次全量成立且无丢失**（执行覆盖 361/361，而不是此前的 331–338）。
- **该轮的一个非 T2 红**：step 8（网格）在 **slot 0** 被拒（`grid readback -> result 33 configured ?`，另三块正常配置，四板 epoch 仍一致），属既存的 BUSY/NOT_IDLE 瞬时拒绝；已用同参数复跑确认（`pwsh-168`），读数待记。
- **结论（也是给部署的约束）**：T2 位置运输的**引线必须覆盖从板流水线时延**；`threshold_per_mille=950`（5%）在 2.4 位置/s 下不足，`800`（20%）在本台架工况下零丢失。引线是**扫描/部署配置**，不是 T2 语义的一部分：五类 fail 计数就是现场判定引线是否够用的量（`LATE` 非零 ⇒ 加引线或降扫描速率）。
- **未核验**：复跑一致性（进行中）；`threshold_per_mille` 的**下界与位置内 16 组序列跨度的关系**（引线过长会挤压窗口 lead？）；页尾新增 5 字段的页契约登记（批次收口）。









### T2-PROGRESS-20261016-004：残差归因与两处「cursor 瞬态即毁掉在飞位置」的过激反应（已修）；action_fail 原因拆分入页

- TODO task ID：`T2-DEPLOY-003`。承接 `-003`：三从板 `361/361/361` 但执行 331/337/338、每板 `abandoned 23–30`。
- **时间线证据（`out/tdma_t2/steps-20261016-004507.log`，逐 tick 复原）**：源端**全程无损**（`entered==announced==frozen`，`abandoned 0`，`executed = entered − 3` 即在飞 3 槽）；三从板的缺口**从 t+2s 起持续累积**，且在 **t+36→38s 同一下标上三块板同时 +2…+4 abandoned** ⇒ 与"稳态拥塞"不符，是**瞬态事件的成批效应**。
- **代码位点（两处过激反应，都在 `service_deadlines()`）**：
  1. 已 arm 的位置：`else if (terminal || !cursor_valid || plan.session/ring != cursor…)` → `cancel_backend()` + `action_fail()`。**裸 `!cursor_valid` 会把已经交给后端的绝对 deadline 取消并判失败**；而同一个 cursor 失效时，`s_state.cursor` 还是旧快照，用它比 session/ring 本身也没有意义。
  2. 未 arm 的冻结位置：`if (!cursor_valid) { action_failed_mask |= frozen & ~completed; return; }` → **每次 cursor 瞬态失效把该板所有在飞位置一次判失败**（成批 2–4 个，与观测的突发一致）。
- **修复**：cursor 失效时**只推迟、不判失败**——已 arm 的交给后端（只在 `terminal` 或**cursor 有效时**的 session/ring 真变化才取消）；未 arm 的保持冻结，等 cursor 恢复后由既有选择环按"可证迟到"判失败。理由：deadline 是绝对本地 tick，后端独立执行；失败应发生在**可证**之时，而不是每次时间基抖动时。
- **可归因化（本仓"失败必须按原因可见"惯例）**：新增 `TRIGGER_T2_ACTION_FAIL_{LATE,EXPIRED,NO_PLAN,CONFLICT,BACKEND}` 五个饱和计数（`action_fail(index, reason)`），**追加在 `T2:ACTion?` 页尾**（既有页索引不动），工具 WIN 行同步打印 `fail late/expired/no_plan/conflict/backend`；`t2_workflow.py` 的页尾切片由 `[-7:]` 改为 `[-12:]`（其余读者 `t2_bench_probe.py`/`t2_iterate.py` 只读头部或不读该页尾，已核）。
- **宿主证据**：`test_trigger_t2_runtime.py::identity` 由"cursor 失效必须取消并判失败"改写为"**失效不取消、不判失败、位置照常完成**"（并断言 `BACKEND` 计数为 0）；T2 运行时/页序/位置飞行/cursor 四套 **75 passed**。
- **未核验**：真板效果（已重烧并跑 361 全程轮 `pwsh-162`，读数待记）；页尾新增 5 字段的页契约登记（批次收口时随 C11 一起办）。









### T2-PROGRESS-20261016-003：**361 全程四板达成**——三从板各 `361/361/361` 进窗·宣告·冻结，执行 331/337/338；零 cursor 锁存；残差 23–30 位置/板（已计 abandoned）

- TODO task ID：`T2-DEPLOY-003`。日志 `out/tdma_t2/steps-20261016-004507.log`（`t2_iterate` 含重烧四板，build `20261015164031`，`T2_SAMPLE_TICKS=110`，`T2_SCAN_OVERRIDE_TERMINAL_MDEG=360000`）；退出码 **0**、`ACTION execution 3 of 4`、`VERDICT: Step-1 criteria met`。
- **逐板窗口（entered/announced/frozen/executed/abandoned）**：源端 `361/361/361/358/0`；SWITCH_A `361/361/361/331/30`；GATEWAY `361/361/361/337/24`；SWITCH_B `361/361/361/338/23`。⇒ 「361 发布」与「各从机接收/解码/冻结全部 361」**成立**；「执行」达 **331–338/361（92–94%）**，其余为**已计数的 abandoned**，无静默丢失。
- **通道侧**：三从 `lane_accept 32.2k/30.9k/31.9k`、`decode_reject 0`、`lane_dup 44.0k/42.5k/43.7k`、`lane_reject 19.9k/22.6k/20.2k`、`hold_drop 7.4k/10.2k/7.9k`、`hold drain 10.2k/10.3k/10.1k`；`FREEZE latched=taken 688/688、674/674、677/677`（**无未取走残留**，`rx_pending_mask 0`）。签名与修复前完全相反：修复前 `taken 0/22`、`rx_pending_mask 1`、窗口恒 0。
- **零 cursor 锁存**：本轮 `STICKY CURSOR FAULT` 计数 **0**（前一版同流程为 3 次、每次都对应一块板停摆），从板 `last_error` 由 `5`(CURSOR) 变为 **0**。⇒ `T2-PROGRESS-20261016-001/002` 的两处换代重基（BACKWARD + OVERFLOW）在真板上**成立**。
- **残差（下一步的量）**：每板 `abandoned 23–30`、`desc_reject 6.7k–7.3k`、`freeze_mismatch 42–52`。`desc_reject` 与 `abandoned` 的关系（哪些是重复/残留、哪些真的导致位置失败）尚未逐条归因；`prep_reason 1`／`last_error` 在**运转中**也会保持上一次的截面值，不能单独用作停摆判据（停摆判据应取窗口 `executed` 是否继续增长）。
- **目标状态**：源端发布与三从接收/解码/冻结已闭合；「各从机执行序列动作」已达 92–94%，**尚未零loss**。⇒ 目标保持 active，下一轮做残差归因（目标：abandoned → 0）与批次收口（一次 `--profile full` P3 + 契约登记/C11）。
- **未核验**：残差成因；同轮重复性（是否每轮都 361/361/361）；B5 版本 16 位余量（可从本轮 `freeze_version[]` 终值取，尚未取）。









### T2-PROGRESS-20261016-002：cursor 换代重基（BACKWARD **与** OVERFLOW 两处）——真板首次 `ACTION execution 3/4`、判据通过；残差为换代后的分段止步（21/91/280）

- TODO task ID：`T2-DEPLOY-003`。承接 `T2-PROGRESS-20261016-001`：修掉 BACKWARD 锁存后，同轮**判据首次通过**，并暴露出同类第二处（OVERFLOW）。
- **真板结果（`out/tdma_t2/steps-20261016-002514.log`，361 全程轮，`T2_SAMPLE_TICKS=110`，四板重烧含 BACKWARD 重基）**：`ACTION execution 3 of 4`、`failed steps: none`、**`VERDICT: Step-1 criteria met`**（`t2_iterate` 退出码 **0**，本目标开始以来第一次）。逐板窗口（entered/announced/frozen/executed/abandoned）：源端 `361/361/361/358/0`；**SWITCH_A `287/285/285/280/5`**、**GATEWAY `98/96/96/91/5`**、**SWITCH_B `23/21/21/21/0`**——**三块从板全部消费并执行**（此前每轮至少一块恒 0）。
- **同类第二处（OVERFLOW）**：三块从板随后分别锁存 `site OVERFLOW err 5`，时刻 t+8s(slot3)/t+40s(slot2)/t+120s(slot1)，**与各自窗口停止增长的时刻一一对应**。位点在 `cursor_project()` 的 `delta = coarse - (pulse_index-1)`：换代会让 `coarse` 合法地**低于**上一代的 `pulse_index`，uint64 相减下溢到 ~2^64，于是 `delta > UINT32_MAX - sample_seq` 成立并**致命锁存**。
- **修复**：两处判据都改为**以 committed model 代为单位**——`rebased = s_binding.bound && model->token != next->model_token`；换代那一 poll ①不做回退比较、②`delta` 取 1（不跨代算间隙、不计漏拍），其余代内行为不变（同代回退/间隙仍照旧锁存与计数）。
- **宿主证据**：`test_trigger_time_cursor.py` 新增/改写两例——`phase_backstep`（换代步退：接受并重基，同代回退仍锁存）与 `overflow_rebase`（换代使 coarse 低于上一代：不再下溢锁存，代内 +2 coarse 仍计入 `missed_sync_count`）；**两例都做了反证**（分别把 `/rebased` 临时置 false 即转红）。cursor 套件 **39 passed**；触发/T2 相关宿主集 **950 passed**（唯一失败仍是既存红 `test_trigger_grid_preview[configure_race]`，`owner.c:129`，与本改无关）。
- **当前残差（非致命）**：三块从板仍**分段止步**且覆盖不均（280/91/21），`lane_reject≈hold_drop` 仍有 2.9k/11k/20.7k、`lane_dup` 9k–18k，`desc_reject` 1201/976/261 ⇒ 换代后的**接收侧争用/去重**是下一步的量；`prep_reason 1`、`last_error 5` 仍是止步后的截面读数（`last_error` 在服务停摆后不再更新，不能单凭它判断停摆原因）。
- **未核验**：OVERFLOW 重基在真板上的效果（已重烧并跑 361 全程轮 `pwsh-150`，读数待记）；**逐板全 361** 尚未达成。









### T2-PROGRESS-20261016-001：根因锁定——从板 cursor 把「模型换代」当成时间回退并**致命锁存**，该板 T2 消费整轮停摆；已改为换代重基（含反证），未锁存的板吃满 361

- TODO task ID：`T2-DEPLOY-003`。证据链：工具加逐板 sticky fault 读取 → 台架 361 全程轮 → 因果对照 → 固件修复 + 宿主反证。
- **工具（`tools/sync_trigger/t2_workflow.py` step 20）**：新增**逐板**读 `TRIGger:REServation:T2:CURSor?` 的 sticky fault（复用既有 `cursor_fault()`/site/diff 解码），此前只在 bring-up 检查点解码、且运行中只读 slot 0 ⇒ 从板的锁存一直不可见。日志 `out/tdma_t2/steps-20261016-000559.log` 立即给出：
  `t+2s slot 1 STICKY CURSOR FAULT: site BACKWARD err 10 at seq 16872 … model_token 11164`、
  `t+2s slot 3 … BACKWARD err 10 … model_token 11350`（slot 3 = SWITCH_B，slot 1 = SWITCH_A）。
- **因果对照（同一轮，无任何其他改动）**：锁存 BACKWARD 的两块板窗口**全 0**（`win 0/0/0/0`，`lane acc 3200/25241`、`hold_drop 266515/207099`、`rt recv 0`），**未锁存的 GATEWAY 吃满整轮 361**（`win 361/361/361/355/6`、`lane acc 35024`、`deliv 2336`）。⇒ 「从板不消费」与 BACKWARD 锁存一一对应，此前把它归给 `VDC:PRIORity:FOLLow?` 超时/反馈会话**不成立**（FOLLow 超时只是同轮并存的另一现象）。
- **机制（代码位点）**：`trigger_time_cursor.c` 的 `cursor_project()` 用 `now_raw < s_binding.last_raw || output_ns < next->event_vdc_ns` 判回退，且 `invalidate(..., BACKWARD, true)` **sticky**；而该比较**跨模型换代**进行——从板的 typed follow 路径在运行中重发 committed model（新 token），投影坐标随之重基，于是被判成回退。锁存后 `s_binding.fault` 使 cursor 直到**新 feedback session** 前恒无效（`trigger_time_cursor.c:299-303`），T2 服务整轮返回 `CURSOR(5)`，lane 队列只进不出。**注意**：`delta > 1` 的漏拍**不是**故障（只累加 `missed_sync_count`，`:210-214`），故 `start_miss/deadline_miss` 是症状不是根因。
- **修复（固件）**：新增 `rebased = s_binding.bound && model->token != next->model_token`，换代的那一 poll **不做回退比较**（由随后的投影把 `event_vdc_ns`/`last_raw` 重基为新代基准）；**同一代内**的回退仍照旧致命锁存。宿主用例 `test_trigger_time_cursor.py::phase_backstep` 相应改写为「换代接受并重基 + 同代回退仍锁存」；**反证已做**：把 `rebased` 临时置 false 后该用例转红（`SITE(fault())==NONE` 断言失败），还原后 cursor 套件 **38 passed**，触发/T2 相关宿主集 **949 passed**（唯一失败为既存红 `test_trigger_grid_preview[configure_race]`，同一断言同一行，与本改无关）。
- **待办**：已以 `t2_iterate`（含构建+OTA）烧入四板并跑 361 全程轮（`T2_SAMPLE_TICKS=110`），结果待下一轮记录；预期三块从板均不再出现 BACKWARD 锁存、逐板窗口接近 361。
- **未核验**：换代重基在真板上的效果（待上述读数）；BACKWARD 锁存的**粘性**本身未改（真实同代回退仍会让该板本轮停摆，属设计，需要 session 重置才恢复）。









### T2-PROGRESS-20261015-006：361 全程轮——**源端 361/361/361 发布冻结成立**；从板接收解码正常但 T2 消费中途停摆（cursor 失效），逐板止于 87/15/0

- TODO task ID：`T2-DEPLOY-003`。台架全程轮（`T2_SAMPLE_TICKS=110`，`T2_SCAN_OVERRIDE_TERMINAL_MDEG=360000`，`t2_iterate --no-flash`，日志 `out/tdma_t2/steps-20261015-235324.log`，23:53→00:04）。
- **目标前半段闭合（源端）**：`WIN t+204s slot 0 COUNTER … win 361/361/361/358/0/0/0 (entered/announced/frozen/executed/abandoned)`——361 个位置**全部进窗、全部宣告、全部冻结**，`abandoned 0`、`mark_rejected 0`、`alloc_rejected 0`。此前"<3 建模"的结论作废。
- **目标后半段未闭合（从板）**：同一轮逐板窗口累计——SWITCH_A `0/0/0/0`、GATEWAY `17/15/15/13/2`、SWITCH_B `89/87/87/76/11`。三块从板的 `lane_accept` 分别为 `2838 / 14587 / 24879`、`decode_reject 0`，`hold_drop` 达 `209319 / 177954 / 150111`，`rt recv 7`（掩码饱和）、`prep_reason 1`（`!active || !cursor.valid`）、失败板 `last_error 5`(CURSOR)。⇒ **接收与解码成立，缺的是消费**：T2 服务在扫描中途开始整轮返回 CURSOR，lane 队列于是只进不出（`hold_drop` 单向增长）。
- **判读要点（勿再误读）**：① 源端窗口的 `executed` 是**槽位复用簿记**（`origin_announce` 复用槽时把 FROZEN 记作 EXECUTED），**不是硬件动作证据**——从板才能真正证明执行；② 从板窗口是否继续增长，是"该板 T2 服务是否还在跑"的直接判据，比 `lane_accept` 更可靠（`lane_accept` 由 lane ISR 计数，**与服务是否运行无关**，服务停了它照样涨）。
- **当前唯一硬缺口**：从板在扫描中途失去 cursor ⇒ 服务停摆 ⇒ 该轮消费止步。同轮残余 8 s 超时只有 `SYSTem:VDC:PRIORity:FOLLow?` 与 `owner MATCH … follow`（三从板），配合 `trigger_time_cursor.c:285-355` 的 `feedback_session`/model-ring 绑定条件 ⇒ 指向**从板 VDC 反馈/follow 会话**（`VDC-ISO-001` 的基础件边界）。下一轮先按基础件路径查（不要在 T2 位置语义里找）。
- **未核验**：三块从板同轮同时吃满 361；`freeze_version[]` 的 16 位余量（B5）本轮仍未取到终值读数。









### T2-PROGRESS-20261015-005：采样器读停机态页面导致从板失稳（已修）——两块从板在 361 扫描下各自吃满全部已发布位置；剩一块每轮轮换饿死（VDC FOLLow 超时 → cursor 失效）

- TODO task ID：`T2-DEPLOY-003`。台架实测两轮对照 + 一处工具修复；固件本轮未改（沿用 23:34 的 R3/R6 包 `build 20261015152931`）。
- **工具缺陷（已修，`tools/sync_trigger/t2_workflow.py` step 20）**：采样循环每个 tick 都读 `SYSTem:VDC:PRIORity:MATCH:STATus?` 与 `SYSTem:VDC:PRIORity:RX?`，而这两页**只在停机态可读**（同文件 teardown 处的注释早已写明：`ring.enabled/adapter_started` 或 lane 活跃时拒绝）。后果是**每块从板每个 tick 阻塞满 8 s 命令超时、连续 5 个 tick**；同轮实测 `phase7 start_miss/deadline_miss` 从主控的 `10473/0` 恶化到从板的 `36867/17282`（后续 `63156/30369`），cursor 失去有效性，T2 服务整轮返回 `TRIGGER_T2_RUNTIME_CURSOR(5)`，从板 hold 队列因此**从不 drain**。修复：采样窗口只保留运行中可答的页面（seat 计数），priority 页面回到 teardown 读；`rate` 行相应只报 seat 增量。
- **两轮对照（同为 361 扫描、同固件）**：
  | 轮次 | 采样时机 | SWITCH_A | GATEWAY | SWITCH_B | 主控 window |
  |---|---|---|---|---|---|
  | 修复前 `steps-20261015-233422` | 23:34 | `lane_accept 301 / hold_drop 31100 / FREEZE taken 0 / win 0/0` | `388 / 35833 / taken 0 / win 0` | `1068 / 39317 / taken 22 / win 10 frozen 10 executed` | `announced 283 frozen 283 executed 280` |
  | 修复后 `steps-20261015-234455` | 23:44 | `lane_accept 2148 / hold 701 drain 571 / FREEZE latched 56 kept 348 taken 53 / win 26 frozen 26 executed` | `2185 / 786 drain 648 / taken 56 / win 27 frozen 26 executed` | `1113 / 268 drain 222 / taken 22 / win 0` | `entered 28 announced 26` |
  ⇒ 修复后**两块从板各自消费了主控在该窗口内发布的全部位置**（`desc_reject 310/327`、`freeze_mismatch 1/2`、`miss_local 0`），即 R1/R3/R6 链路在 cursor 稳定的板子上端到端成立（`FREEZE taken` 53/56，「暂存后继 → 完成后接管 → 用 FREEZE 重发冻结」这条新路径在真板上被走通）；**饿死在轮次间轮换**（本轮轮到 SWITCH_B）。
- **剩余阻塞（跨域，非 T2 侧）**：唯一失败板 `last_error 5`（CURSOR）、`prep_reason 1`、`FREEZE taken 22` 后停摆；同轮剩余 8 s 超时**只剩** `SYSTem:VDC:PRIORity:FOLLow?`（step 15/16 各两次）与 `owner MATCH generation … follow <timeout>`（三块从板）。cursor 服务要求 `vdc_dpll_manager_feedback_session()` 非零且与 ring/model 绑定一致（`trigger_time_cursor.c:285-355`），从板的 **VDC 反馈/follow 会话**没建立起来 ⇒ cursor 失效 ⇒ T2 服务整轮拒绝 ⇒ 该板 lane 队列堵死。这属 `VDC-ISO-001`/挂载约束的边界（负载在位时基础件退化），不是 T2 位置语义问题。
- **采样窗口（已修）**：采样器原为固定 5 tick × 2 s ≈ 10 s。修复 priority 页面后单 tick 回到 ~2 s，10 s 只覆盖 361 扫描的前 ~28 个位置（上表右列即是），会把"扫过的前 28 个位置"误报成整轮读数。现由 `T2_SAMPLE_TICKS`（默认 5）控制；**第三轮以 `T2_SAMPLE_TICKS=30`（≈60 s，覆盖 361 全程）重启**（`T2_SCAN_OVERRIDE_TERMINAL_MDEG=360000`，`t2_iterate --no-flash`），读数待补。
- **C14 轮转（已执行）**：本日志曾达 200,398 B；最旧 35 条（`T2-PROGRESS-20260926-016`..`T2-PROGRESS-20260922-001`）已按日期边界逐字迁入 `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_01.md`，本文件补 `归档索引` 并降到 **113,347 B**（`docs/README.md` 已加索引行；`docs_check`/`doc_regression` 全绿）。**更正一条自身误判**：此前记的"日志尾部是乱码、需先修编码"是**错的**——那是 PowerShell 5.1 按 ANSI 代码页显示 UTF-8 的观感，文件本身是正常 UTF-8（已用 UTF-8 读回核对）。
- **未核验**：361 全程的发布/消费覆盖；第三轮读数出来后按逐板窗口累计判定。









### T2-PROGRESS-20261015-004：FREEZE 退化考古（3ddeb258 稳定 → 87706fee 破坏）+ 主控独立核实：回执坐标错位只存在于 HEAD，工作区批次已移除

- TODO task ID：`T2-DEPLOY-003`（FREEZE 稳定性）。只读考古 + 抄读源码核实，均未上机。
- **考古结论**：最后已知 FREEZE 稳定闭环的版本是 **3ddeb258**（2026-10-10「一等席经优先级通道接收，FREEZE 闭环确认直至冻结完成」），证据 `T2-PROGRESS-20261010-024`：三从 `frozen=7`、ACT `armed=done=7`、`fail=0`，NO1 `freeze_complete_mask=7`/`abandoned=0`。**当时设计上只有 3 个位置**（`position_id > POSITION_COUNT` 即拒），故"稳定"不含长扫描。
- **HEAD 的破环点（主控核实，非推测）**：`87706fee`（2026-10-15，位置实例窗口驱动从板消费）把「冻结即终态」的早退换成 `trigger_position_window_find(...) >= 0 && trigger_position_window_ack_mask(&s_position_window) & bit`，而 `ack_mask` **按窗口记录下标（8 槽）**组装（`trigger_position_window.h:56-64`）、`bit` 却是**运行时槽下标（0..2）** ⇒ 同一回执字节被两种坐标系解释，既会漏确认（重发到窗口过期 → `abandoned`）也会误确认；同提交的 `report_frozen` 还上报**未排除已完成位**的全量 `frozen_mask`。两条均以 HEAD 源码核实（`git show 9cb93bcb:components/sync_trigger/src/trigger_t2_runtime.c`）。
- **工作区批次已移除这两条（本轮核实）**：运行时**已无** `trigger_position_window_ack_mask()` 调用点（只剩头文件与 `tests/unit/test_trigger_position_window.c`）；`report_frozen()` 上报 `frozen_mask & ~action_completed_mask`。且线上没有独立的"实例"坐标——`position_id` 就是「槽+1」、绝对身份是 `target_ms_index`，所以「统一回实例坐标」在工作区已等价成立；剩下的身份要求是 **R1 的 target 判定 + `s_slot_target` 校验**，两者工作区都已具备。
- **对 361 的含义**：HEAD 那类坐标混用不会再以原形复现；本轮 361 读数要回答的是 R1/R3/R6 与接收侧争用，而不是回执坐标。**未核验**：全部为只读核实，3ddeb258 的"稳定"只覆盖 3 位置。









