# T2 预约触发任务进度（归档 12）

Status: Draft
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_12.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-10-21

## 归档条目（第 12 批）

### T2-PROGRESS-20261018-045：交接记录——双组展开失败的最后一步已收敛到“第 2 行的模板行内容”

- **本轮读到的代码路径**（不再猜）：
  `trigger_action_template_expand()` → `trigger_action_template_valid()`（要求 `template_id && generation && span_slices && kind∈{SWITCH,GATEWAY}`）→ `trigger_position_instance_expand(descriptor, subdivisions, t->delta_q, &position)`。
  ⇒ 双行失败只可能来自两处：**第 2 行的 `generation`/`span_slices` 为 0**，或 **`trigger_position_instance_expand` 用该 `delta_q` 算不出实例**。
- **台架读数（模板页）**：`TRIGger:RESERVation:TEMPLate?` 在双组板返回 **23 字段**、单组板 **16 字段**，差值 7 ⇒ 每行一块（7 字段）。这与“双组板装了 2 行”一致，但字段顺序尚未逐项核对，因此**还没有直接证明第 2 行的 generation 为 0**。
- **下轮（最省事的两步）**：
  1. 按页 handler 顺序列出 7 字段含义，确认第 2 行的 `generation/span/kind` 是否为 0；
  2. 在 `expand` 的两个失败出口各加一个桶（`invalid_row` / `instance_expand_fail`），一次台架即可锁定；随后按结果修（补行内容 or 放宽 instance 边界）。
- **已确认可用的事实**：执行器侧“一次交整表 + 逐组回执”在真机上跑通（同一轮 6 个位置完整走完两组、`stage_reject` 全 0、`origin_late` 仅 3 次）；单组路径 169/169 零失败；T2 位置传输全程 `DISTRIBUTED`、`SEAT refused 0`。












### T2-PROGRESS-20261018-044：组周期假设也被否定——`expand_fail=544` 与周期无关，是结构性问题

- **实验**：同一条双组链路改用 `T2_SEQ_PERIOD_SLICES=2`（怀疑第二行 `delta` 超出网格细分范围）。
- **结果**：`SWITCH_A` 双组仍然 `executed=6`、`NO_PLAN=166`、`expand_fail=544`（**一字未变**）；反而把单组 `SWITCH_B` 打坏成 `executed=1 / BACKEND=168`。⇒ **周期不是原因**，且该参数不能随便动。
- **因此定性**：`trigger_action_template_expand()` 在“每位置 2 行模板”时**结构性失败**（与 delta/period 无关）。下一步只需读该函数对 2 行表的拒绝条件（span/argument/序号/role-kind 组合），在展开处加一个分桶即可定位到具体检查项。
- **本轮已完成并提交**（`0f4d285b`，P3 `build=20261017222242`）：求解路径计数（`no_descriptor/expand_fail/version_zero/published`）挂到 `WHY?` 页；T2 进展日志按 C14 轮转（`LEGACY_..._02.md`，live 176 KB，两份文档门禁全绿）。
- **剩余工作（下轮）**：① 读 `trigger_action_template_expand()` 的拒绝分支并加桶；② 修 2 行展开；③ 复跑期望 `executed→169 / NO_PLAN→0`。












### T2-PROGRESS-20261018-042：假设被台架否定一次——清槽位计划身份**没有**改善双组；`NO_PLAN` 的来源改为指向"描述符版本为 0"

- **试的修法**：窗口结束时清空该槽位的计划身份（`group/group_count/prediction_version/next_tick = 0`，理由：武装路径会清版本，FREEZE 把 version 0 读成 NO_PLAN）。
- **台架结果（同一 `T2_SEQ_GROUPS='1,2,1,1'`）**：双组板**完全没变** —— `executed=6`、`NO_PLAN=166`、`freeze_missing_local=166`；单组板仍 `168/169` 全过。⇒ **假设错误**，已如实记录。
- **排除掉的两条**：
  1. `t2_deadline_plan_current()` 已经要求 `previous_group == group`，所以"Core0 因为组号不同而跳过发布"不成立；
  2. 槽位遗留状态不是原因（清零无效）。
- **新的方向（代码依据）**：Core0 的 `plan_version = descriptor.prediction_version`（L1942）；若该位置的**描述符版本本身就是 0**，发布出去的计划版本也是 0，FREEZE 必然判 `NO_PLAN`。所以下一步该查的是：双行模板下，该位置的 PREVIEW/描述符是否根本没带版本（例如部署表里两行模板使 `trigger_action_template_expand` 只对第 1 行生成 instance，或 descriptor 的版本在展开时被清零）。
- **下轮**：在 Core0 的 group 0 分支上加两个计数（`expand_fail_count` / `descriptor_version_zero_count`），一次台架就能分辨"展开失败"还是"版本为 0"，再对症修。











### T2-PROGRESS-20261018-041：读到了区分两种成因的计数——双组是 **`freeze_missing_local_count=166`（根本没解出计划）**

- **读数**（`TRIGger:RESERVation:T2?` prefix，索引由 handler 顺序算出：13=frozen_mask、14=mismatch、15=fallback、16=missing_local）：
  ```
  FB276192 SWITCH_A 2组 : mismatch=0    fallback=1  missing_local=166   ← 与 NO_PLAN=166 一一对应
  2BD5090F GATEWAY  1组 : mismatch=160  fallback=1  missing_local=7     ← 169/169 全过
  A1E54920 SWITCH_B 1组 : mismatch=160  fallback=1  missing_local=5     ← 169/169 全过
  ```
- **结论（这次是数字说话）**：单组板大量 `mismatch` 但**照常执行**（fallback 路径可用），所以 `mismatch` 不是问题；双组板 `mismatch=0` 而 `missing_local=166` ⇒ **该位置的槽里根本没有可用计划（版本为 0）**，FREEZE 只能判 `NO_PLAN`。
- **代码指针**：
  - `trigger_t2_runtime.c:1242` 在武装后把 `s_deadline[selected].prediction_version = 0u`；
  - `trigger_t2_runtime.c:974` 在 FREEZE 时把 `version==0` 判为 `NO_PLAN`；
  ⇒ 双组时 Core0 的 solve 没有在下一个 FREEZE 之前把**该位置自己的**计划填进去（单组时靠旧版本走 fallback，所以看不出问题）。
- **下轮**：检查 Core0 solve 的入口守卫（`s_local_template_count` 与部署/模板行数的一致性、以及“solve 只对 `group==0` 生成计划”的假设），并加一个“每位置 Core0 发布计划次数”的计数；修好后复验 `missing_local → 0`、`executed → 169`。











### T2-PROGRESS-20261018-040：`NO_PLAN` 的两个来源已定位到代码行，下一步只需读两个现成计数

- **代码定位**（`components/sync_trigger/src/trigger_t2_runtime.c`）：
  1. **FREEZE 路径**（~L974）：`!plan->prediction_version || plan->prediction_version <= s_slot_floor[index]` ⇒ `action_fail(NO_PLAN)`，并 `freeze_missing_local_count++ / freeze_mismatch_count++`；
  2. **抢占/选择路径**（~L1228）：`!plan->prediction_version` ⇒ `NO_PLAN`；`plan->action_q <= cursor` ⇒ `LATE`；会话/网格不符 ⇒ `EXPIRED`。
- **为什么只怀疑第 1 条**：本轮 166 个失败位置里 `LATE=0 / EXPIRED=0 / CONFLICT=0 / BACKEND=0`，只有 `NO_PLAN=166`；而"每位置 2 组"会把同一位置的占位窗口拉长（占位地板 `s_slot_floor` 在执行中推进），于是下一个位置的计划版本可能刚好等于地板 ⇒ 被判"没有计划"。这条与"单组 169/169 全过、双组只有 6 个走完"完全吻合。
- **下轮先读现成计数**（无需改固件）：`TRIGger:REServation:T2?` 的 prefix 里已经有 `freeze_mismatch_count / freeze_fallback_count / freeze_missing_local_count`。
  - 若 `freeze_missing_local_count` 增长 ⇒ 本地根本没解出计划（模板 2 行时的 solve 问题）；
  - 若只有 `freeze_mismatch_count` 增长 ⇒ 就是"地板/版本"语义问题，修法是把地板比较改成"同一位置窗口内不推进"，或让 `s_slot_floor` 只在**位置收尾**时推进。
- **修好后复验目标**：`executed → 169`、`NO_PLAN → 0`、`origin_late_count` 只增不减但不影响执行。











### T2-PROGRESS-20261018-039：开关双组窗口**开始执行了**（executed 0→6），限制点前移到 `NO_PLAN`

- **拿到实测数字**（上一轮的仪器）：`span=497~544 拍`、`bound=128 拍`（`256 ns × 组数`）。即双组武装的真实代价是 **~2 µs**（4 次 `pio_sm_put` + `pio_enable_sm_mask_in_sync`），远大于单组预算 64 拍。
- **修法**（`4df45340`）：origin 跨度超限**不再致命**——它本来就是“这条边沿迟到多少”，已由 `origin_before/after_tick` 上报；组间间隔用的是相对 delay，不受影响。改成 `origin_late_count` 计数并继续。
- **台架前后对比（同一条 `T2_SEQ_GROUPS='1,2,1,1'` 链路）**：
  | 指标 | 修前 | 修后 |
  |---|---|---|
  | `executed`（SWITCH_A 双组） | 0 | **6** |
  | `stage_reject` 全部桶 | BUSY=160, ORIG_WIN=1 | **全 0** |
  | 失败原因 | BACKEND=161 | **NO_PLAN=166**（BACKEND=0） |
  | 单组从板（GATEWAY/SWITCH_B） | 161/161 | **169/169 零失败** |
- **结论**：开关“一次交整表 + 逐组回执”的机制在真机上**已经跑通**（有 6 个位置完整走完两组）；剩下的拦住 166 个位置的是**本地规划器拿不到可用计划**（`TRIGGER_T2_ACTION_FAIL_NO_PLAN`），属于“每位置 2 组时模板/部署与本地 solve 不匹配”的问题，与执行器无关。
- **下轮**：查 `NO_PLAN` —— 看 `trigger_t2_runtime` 的本地 solve 在 2 行模板下是否只解出 1 组（`s_local_template_count` 与 `group_count/group_tick[]` 的对应）、以及 FREEZE 到达时计划表是否已就绪；对症修完再复跑，期望 `executed → 169`、`NO_PLAN → 0`。











### T2-PROGRESS-20261018-038：origin 窗口的两轮修正都没解决——已把"实测跨度 vs 上限"变成读数

- **第一轮修法**（`c5480e2a` 之后）：把 `raw_tick(&now)` 移到计划构建之后、并用真实 tick 重算首组 delay（新增 `sync_io_sequence_plan_first_delay()`）⇒ 台架仍 `ORIG_WIN=1`、`executed=0`。
- **第二轮修法**：把 origin 上限按组数放大（`ORIGIN_MAX_NS × group_count`，单组行为不变）⇒ 台架**仍然** `ORIG_WIN=1`、`executed=0`、`BUSY=160`。
- **同轮对照**：两块单组从板 **161/161 零失败**（`executed=161`，所有桶为 0）⇒ 问题严格限定在"一次交整表"的开关路径，且只在第一组武装发生一次，之后被 `PENDING` 卡死。
- **拒绝桶拆分**（`NO_READY/BUSY/CLOCK/TICK/PLAN/ORIG_DELAY/ORIG_WIN`）证明：`ORIG_DELAY=0`、`ORIG_WIN=1` ⇒ 失败在"使能后回读 tick"的比较，而不是"截止太近"。
- **下一步（本轮已就位）**：新增 `s_origin_span_last` / `s_origin_bound_last` 并在窄页输出（`7a2ffbb7`）——下一次台架运行直接读到"实测 90 拍 vs 上限 64 拍"这类数字，再决定是放宽上限、减少推字次数，还是把 origin 检查改成"只统计成功"。
- **教训（写下来）**：`SYNC_IO_SWITCH_ORIGIN_MAX_NS=256`（64 拍）对单组（2 次推字）够用，对多组（2×组数 次推字 + in-sync 使能）明显偏紧；改之前必须拿到实测跨度，否则就是猜。











### T2-PROGRESS-20261018-037：开关双组窗口的真机根因已锁定——**首组武装的 ORIGIN 窗口失败**，随后整块位置永远 PENDING

- **仪器**（`a4da030e` + `c5480e2a`）：`stage_sequence` 的六个早退点各一个桶（`NO_READY/BUSY/CLOCK/TICK/PLAN/ORIGIN`），通过窄页 `T2:ACTion:WHY?` 输出（开关整页 `TRIGger:SWITch?` 太长会 timeout）。
- **台架读数**（`T2_SEQ_GROUPS='1,2,1,1'`，SWITCH_A 两组；同一轮里两块单组从板 **157/157 全过**）：
  ```
  FB276192 (SWITCH_A, 2 组) -> executed=0 group_count=2 reasons[BACKEND]=157
                               stage_reject: NO_READY=0 BUSY=156 CLOCK=0 TICK=0 PLAN=0 ORIGIN=1
  2BD5090F / A1E54920 (1 组) -> executed=157 reasons 全 0 stage_reject 全 0
  ```
- **结论（因果链完整）**：首次双组武装在 **ORIGIN 窗口**检查处失败（`after+1-now` 超出 `SYNC_IO_SWITCH_ORIGIN_MAX_NS`），但此时两个 SM 已经被使能、字也已入 FIFO ⇒ 开关从此停在 `PENDING`，后续 156 次武装全部 `BUSY`，`groups_executed` 永远是 0。**不是时序太紧、不是 LATE、不是 plan 拒绝。**
- **为什么双组才触发**：`stage_sequence` 把 `raw_tick(&now)` 放在**计划构建（含逐组 tick 换算）之前**，窗口内还包含 `prepare_group` 与 2×组数 次 `pio_sm_put`；组数从 1 变 2，测量的“origin 宽度”就跨过了上限。
- **下轮修法（已明确，改动很小）**：先把计划算完，再取 `now`，并且**只用真实 `now` 重算首组的 delay**（`base - now - FIRST_EDGE_CYCLES`），后续组的 delay 是“间隔−固定预算”、与 `now` 无关；这样 origin 窗口只覆盖“使能之前的那几次推字”，双组也不会超限。修完复跑同一条 2 组链路，期望 `ORIGIN=0 / BUSY=0 / executed>0`。
- **顺带确认**：单组路径在本轮再次 157/157 零失败（GATEWAY 与 SWITCH_B），说明问题只在“一次交整表”的开关路径上。











### T2-PROGRESS-20261018-036：单组 GATEWAY 157/157 通过；**开关的双组窗口在台架上稳定失败**（executed=0、BACKEND）

- **隔离实验**（`T2_SEQ_GROUPS='1,2,1,1'`：只有 SWITCH_A 两组，其余单组；`T2_SEQ_PERIOD_SLICES=2000`）：
  ```
  FB276192 (SWITCH_A, 2 组) -> executed=0 group_count=2 packs 240/240/240 reason[4]=157   ← BACKEND
  2BD5090F (GATEWAY , 1 组) -> executed=157 group_count=1 packs 496/496/496 reasons 全 0 ← 全过
  A1E54920 (SWITCH_B, 1 组) -> executed=1  group_count=1 packs 496/240/240 reason[4]=156  ← BACKEND
  ```
- **两条可靠结论**：
  1. **单组路径在真机上没问题**：GATEWAY 1 组跑完 157 个位置、零失败（与之前 `(1,1,1,1)` 全绿一致）。
  2. **开关的双组窗口在真机上稳定失败**：`groups_executed=0`、157 个位置全部 `BACKEND` —— 也就是说 `sync_io_switch_realtime_stage_sequence()` 在真机上**返回 false**（或执行器立刻 CANCELLED/UNCERTAIN），host 用例的绿灯**不代表**真机可用。
- **注意一个混淆变量**：`T2_SEQ_PERIOD_SLICES=2000` 会同时改变网格/位置节奏，本轮连单组 SWITCH_B 也出现 156 失败 ⇒ 该参数不能这样跳变，必须小步扫（这也解释了为什么“把周期放大”不是解法）。
- **下轮（仪器化，不再猜）**：给开关的 `stage_sequence` 加**拒绝原因计数**（`stage_reject_reason` 桶：NO_LEASE/PENDING/DEADLINE_PASSED/TOO_CLOSE/BAD_DELTA/FIFO）并用窄页读出；这样下一次台架运行就能直接看到“为什么 false”，而不是从 `executed=0` 反推。











### T2-PROGRESS-20261018-035：双组窗口的失败原因已定位——**BACKEND（后端拒绝）**，不是 LATE

- **工具**：新增窄页 `TRIGger:RESERVation:T2:ACTion:WHY?`（`6375d919`），返回 `groups_executed, group_count, group_pack(0..2), action_fail_reason[5]`。原因很简单：`T2:ACTion?` 约 1500 字段，工具读窗口截断在 53 字段，**失败原因桶在截断之外**，此前根本看不到。
- **台架实测**（`T2_SEQ_GROUPS='1,2,2,2'`，22 步全 OK、`DISTRIBUTED`，三块从板读数一致）：
  ```
  FB276192 / 2BD5090F / A1E54920 -> 1,0, 0, 2, 240,240,240, 0,0,0,0,166
                                     └executed=0 └group_count=2  └packs┘ └reason[0..3]┘ └reason[4]=166
  ```
  `TRIGGER_T2_ACTION_FAIL_BACKEND = 4u` ⇒ **166 个位置因“后端拒绝/取消/不确定”失败**，而 `groups_executed = 0`、`group_count = 2`。
- **结论**：双组窗口根本没跑起来——不是“第二组太晚（LATE）”，而是**一次都没被执行器接受**。可能的直接原因（下轮按顺序验证）：
  1. 上一位置的窗口仍 `PENDING`（两组耗时 > 位置间隔），下一次 `stage_sequence` 被开关的 `state==PENDING` 拒绝 ⇒ 属“组周期/位置周期太短”；
  2. GATEWAY 侧 `stage_sequence` 要求 `!s_ready_active`：台架**没接网分**，READY 永不到达 ⇒ 第 2 个位置起一直被拒（这条与 `ready_collision_count` 的预期增长一致）。
- **下轮顺序**：① 先把 `T2_SEQ_PERIOD_SLICES` 与位置间隔放大到“两组动作能跑完”（例如组周期 ≥ 2×动作时间），复测 `groups_executed>0`；② 无网分时把 GATEWAY 的窗口预期改成“只验 TRIG 与踩踏计数”，或注入 READY；③ 再谈四板全链。











