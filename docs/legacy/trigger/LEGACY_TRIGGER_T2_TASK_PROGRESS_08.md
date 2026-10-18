# TRIGGER T2 任务进展归档 08

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_08.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-19

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20261015-003` .. `T2-PROGRESS-20261014-004`（按新鲜度倒排，最新在前），共 6 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。

### T2-PROGRESS-20261015-003：R3（忙槽暂存后继）+ R6（丢失计数）落地，含「禁用即红」反证；NO.4 用 picotool 复位恢复；361 轮重启

- TODO task ID：`T2-DEPLOY-003`（R3/R6 与台架重启）。
- **R3 实现（`components/sync_trigger/src/trigger_t2_runtime.c`）**：新增每槽一深的「下一占用者」暂存 `s_stash[]`/`s_stash_valid`；忙槽（已冻结且未完成）收到**更高版本且目标不同**的 PREVIEW 时入暂存（同目标重解不入，避免同一位置二次动作）；`promote_stashed()` 在**读传输之前**把已完成的槽交给后继，于是随后到达的 FREEZE 重发能冻结后继，而不是被「已完成未回收」分支整条丢掉（这正是台架上位置间歇缺失的机制）。顺带把 PREVIEW 的绑定/校验/窗口/槽位落地抽成 `admit_preview()`，传输路径与暂存接管共用同一套规则。
- **R6 实现**：新增 `note_position_lost()`，计两处——未冻结的已收占位者被不同目标覆盖、已暂存后继被另一目标替换；同目标重解不计。只出计数（`freeze_abandoned_count`，已在页上），不动 `freeze_abandoned_mask`（保持主机侧语义），**无页/ABI 改动**。
- **宿主证据（`tests/python/test_trigger_t2_runtime.py`）**：新增 `follower_scan_successor`（后继只在飞、之后**只有 FREEZE 重发**，修复后仍冻结并执行、窗口 `executed==2`）与 `follower_overwrite_abandon`；`follower_scan_busy` 改为断言「后继被暂存、完成即接管、无需重发」。定向集合 **33 passed**。**反证已做**：把 `stash_successor()` 临时置为立即返回后，`follower_scan_successor` 转红（`descriptors[0].prediction_version==5u` 处失败），随即还原并复跑 33 passed。
- **台架恢复（用户批准重启）**：23:19 的重启轮在 step 4 失败（`missing_serial_numbers=A1E549202D18ED6A`）；复核证实 **NO.4 不在 USB**（`board-presence-probe.py` 3/4 板、OTA summary 只有 COM3/4/5）。picotool 能看到 4 个 RP2350（bus1 addr 5/6/7/8）但均非 BOOTSEL；用仓内 `tools/ota/picotool_reboot/picotool_reboot.py --serial-number A1E549202D18ED6A`（`picotool reboot --ser … -f`，**不进 BOOTSEL、不动 Flash**）后设备重枚举，四板全部应答 `*IDN?`（NO.1/2/3/4）。⇒ 该卡死可用 picotool 应用复位恢复，无需物理复位（与本仓旧记录中 COM6 需物理复位的情形不同）。
- **本轮重启命令**：`T2_SCAN_OVERRIDE_TERMINAL_MDEG=360000 T2_SCAN_OVERRIDE_STEP_MDEG=1000 python tools/sync_trigger/t2_iterate.py`（重烧含 R3/R6 的四板 → 361 位置扫描 → 判据）；读数出来后按逐板窗口累计（entered/announced/frozen/executed/abandoned）与 `freeze_version[]` 判定 R1–R6 与 B5。
- **两条宿主红（与本轮改动无关，已核）**：① `test_t2_configure_signature` 钉的 `t2_workflow.py` 调用点行为 `(214,520,972,1737)`，实际 `(222,528,982,1777)`——未提交的工具批次移动了行号，需随批次更新该 pin；② `test_trigger_grid_preview[configure_race]` 现为**稳定红**（3/3），其 `owner` harness 只编译 `trigger_grid_preview.c`/`vdc_domain.c`/`vdc_timestamp.c`/`tdma_profile.c`/`trigger_time_cursor.c`/`trigger_time_projection.c`，**不含 `trigger_t2_runtime.c`**，故与本批无关。
- **未核验**：R3/R6 只过了宿主用例与反证；台架判定待本轮 361 读数。RAM 增量（3 × `trigger_position_descriptor_t`）尚未从 map 量。








### T2-PROGRESS-20261015-002：B7 口径纠正（车道广播同一批预测时间片；依据冻结时间片按规划距离动作）+ 代码审查项工作区状态复核 + 上轮台架中断取证

- TODO task ID：`T2-DEPLOY-003`（B7 与代码审查项 R1–R7）。
- **口径纠正（2026-10-15 用户澄清，取代本文与 TODO 中「分发公平性」的表述）**：主机**只发位置时间片与 FREEZE**；从机本地解算后**回「已冻结」**，再**以冻结的那一片为基准**展开本板动作序列——部署期只取本角色那一行、按 `delta_q`（本板规划距离推出的延时）算本板动作时刻，到点动作。⇒ 车道对四板**广播同一批预测时间片**（一次飞行内各板收到的是同一批片），**不存在「按板分发」「公平性」或「逐板到位率」这类目标**；跨板 `lane_accept` 与 `ACTION execution N/4` 都不作判据，`lane_accept` 的离散只作接收侧（单深度邮箱 + 有界 hold 环）争用/背压的证据。每块板的动作依据就是**冻结的时间片**：收到即按**规划距离**推出的延时动作（动作时刻 = 冻结时间片 + 本板规划距离延时，**不再等第二次下发**）。代码与该口径一致：FREEZE 帧只带身份（`trigger_t2_runtime.c:368-375`）、本板角色行筛选（`:962-966`）、`trigger_position_instance_expand()` 的 `action_q = target_q + delta_q`、回执 `report_frozen()`（`:545-549`）；展开输入是 FREEZE 回绑的那片 PREVIEW（`:632-640`、`:1325`）⇒ R1 的目标校验是「冻结基准绑定正确」的判据，不只是丢包计数。
- **代码审查项工作区复核（只读，未改固件）**：`git status` 显示 `trigger_t2_runtime.c` 等 6 个源码文件有未提交改动，其中 **R1**（不再用 `descriptor_window_sync` 固定容差，改判「离上一个占用者更近即丢」，`:577-597`）、**R2**（`report_frozen()` 上报 `frozen & ~action_completed`，`:541-549`；早退分支区分「已完成未回收」`:564-573`）、**R4**（按槽 `s_announced_ordinal` 原子量比较，`:1400`、`:1498-1499`）**已有实现**；**R3**（忙槽丢下一位置 PREVIEW，`:564-567`）与 **R6**（覆盖未冻结占用者不计放弃）**仍未实现**；**R5** 从板侧不再静默（`:623-628`、`:517-518`）而 Core0 侧仍未加版本下限。R1/R2 的读数**尚未**由台架复验；TODO 的状态列已按此更正。
- **上轮台架中断取证**：`out/HardwareAcceptance/20261015/hawf-221039` 停在 step 4（刷写）——`COM5` 的 `ota_send.stdout.txt` 尾为 `progress=852480,2081144`、最后写入 22:15:27，此后无写入；`Get-Process` 无 python 进程 ⇒ 该轮**已中断**，不是"仍在跑"。其 `workflow-state.json`：`build_id=20261015141052`（UTC）、`fingerprint_before=31148c84c8d53ba8c98d3ad453708c40388618334e65dc7dacd16ffa5448d9e8`、`source_file_count=1808`，与复核时 `working_source_fingerprint()` **逐字相同** ⇒ 该包由当前工作区构建、**含 R1/R2/R4 改动**；`docs/**` 不在验收源码指纹（`SOURCE_ROOTS`）内，文档更正不改变指纹，可 `--run` 续跑。
- **未核验**：本轮未运行构建、OTA、P3 或宿主测试；以上均为只读取证（`git status`/`git diff`/工作区源码/该运行目录）。








### T2-PROGRESS-20261015-001：自 TODO `T2-DEPLOY-003` 行下沉的 2026-10-15 长记录（原文逐字保留）

- TODO task ID：`T2-DEPLOY-003`。本条仅搬迁原表格单元内的过程性记录（C12），未改写事实；其后有更新的口径以 TODO「现状汇总与待办」为准（例：其中“扩到 8 位窗口 / 备用方案”“LOCAL_TEMPLATE_CAPACITY 1→16、TEMPLATE_CAPACITY 8→16”均已被后续实测与裁决取代——后者因 DPLL 相位回归保持 8）。
- 原文：

**2026-10-15 角色动作实测**：动作表按本板角色筛选（`trigger_action_service.c:109`），运行时按本板模板种类布置 SWITCH/GATEWAY 截止（`trigger_t2_runtime.c:670-710`）；判据页 `TRIGger:RESERVation:T2:ACTion?`。两轮 `--full`（`out/tdma_t2/steps-20261015-091127.log`、`steps-20261015-092034.log`）中各正好两板 `armed 3 / completed 3 / failed 0 / uncertain 0`，未执行板 `received 0 / frozen 0`，且**饿死者在两轮间轮换**（SWITCH_B ↔ GATEWAY）⇒ 角色差异执行可用，缺口是**竞争型单深度邮箱 + 有界 hold 环**（`lane_reject` 14k–69k、`hold_drop` 26k–69k）导致每轮总有一块目标板拿不到新鲜片。剩余：改分发语义使**每个目标角色**每轮都拿到片（每目标独立副本/邮箱、加深 hold 环或按目标单播），并让 `ACTION execution: N of 4` 成为判据。**2026-10-15 Step 1 根因与实施清单（不改 wire）**：源侧只把冻结回执与"发布但未完成"集合按位与（`trigger_t2_runtime.c:186-216`），8 位回执是**当前待确认窗口**而非全量历史 ⇒ 无需改 run id 语义或回执格式；三槽限制来自运行时自己的簿记。实施顺序：① 新增 `TRIGGER_T2_RUNTIME_POSITION_WINDOW = 8`（与 `TRIGGER_T2_RUNTIME_POSITION_COUNT = 3` 的在途预测窗口分离）；② 源侧 `published_mask`/`freeze_complete_mask`/`freeze_abandoned_mask` 与从板 `frozen_mask`/`received_mask`/`s_pending_mask`、`s_deadline[]`/`freeze_version[]`/`freeze_local_version[]`/`freeze_local_age[]` 按新窗口重标；③ `accept_remote_position()` 接受 `position_id ∈ [1,15]`，加 `s_slot_position_id[WINDOW]` 实例→槽映射与 15 位 `s_executed_instances` 防重复执行，槽在动作完成/放弃后回收；④ 同批 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY 1→16`、`TRIGGER_ACTION_TEMPLATE_CAPACITY 8→16`；⑤ 工具短扫描覆盖（`T2_SCAN_OVERRIDE_TERMINAL_MDEG` ≤8 位置）验证从板累计 `events`/冻结覆盖全部位置、`lane drop` 不再单向增长、`ACTION execution` 覆盖全部目标板，再上全扫描。**2026-10-15 迭代纪律（用户裁决，取代逐改动付 P3 的做法）**：① 不再为每个小改动单独跑 P3——批次做大再做；② **P3 只覆盖 DPLL/联合采集，不能证明 T2 的改动正确** ⇒ T2 的功能判据必须由台架 T2 运行给出：先 `python tools/hardware_acceptance/hardware_acceptance_workflow.py --steps 0-7`（构建 + OTA 烧录，~3.5 min）把改动烧进四板，再 `python tools/sync_trigger/t2_workflow.py --full`（~8 min）验证 T2 行为；③ **功能验证通过后**，再对整批跑一次全项 P3（`--profile full`）拿凭证并提交；④ 期间允许工作区保留已宿主验证但未提交的改动（不再为提交而提交）；⑤ P3 的 step 29（DPLL60 联合采集）偶发失败时重试即可（第三次通常通过）。原句"固件改动 ⇒ 契约登记 + C11 + 一次全项 P3"仍成立，只是"一次"指**批次收口处**。**2026-10-15 实证边界（批次必须包含 harness 期望）**：把从板位置消费局部切到 `trigger_position_window_t`（实例键滚动 + 回执由窗口派生 + 槽位越界守卫）可以编译通过，但会让 `tests/python/test_trigger_t2_runtime.py` 的 **16 个用例转红**——它们钉住的是旧三槽语义（每板一行/一个动作、冻结位粘滞等）⇒ 运行时切换**必须与这 16 处期望、页/契约同批完成**，否则不构成可验证状态。该局部改动已按回收规则回退，树中不留未验证改动；回退后宿主为 `26 passed, 1 failed`（唯一失败是既有红 `freeze_missing`）。批次清单因此再确认：① 运行时两侧切换；② 同批更新 16 处运行时用例期望；③ 尾追加页 `T2:WINDow?` + 页契约；④ 模板容量 1→16/8→16；⑤ 修既有红 `freeze_missing`。验证用 `T2_SCAN_OVERRIDE_TERMINAL_MDEG=8000 python tools/sync_trigger/t2_iterate.py`（自动判据）。**2026-10-15 批次项⑤现状（既有红定位）**：`test_trigger_t2_runtime[freeze_missing]` 的失败断言在 `tests/python/test_trigger_t2_runtime.py:343`：`snapshot().freeze_missing_local_count==1 && !inverse_calls && !schedule_calls`——语义是"FREEZE 到达但本板没有已保存的预览时，必须把一个位置记入 `freeze_missing_local_count`，且**不得**调用 inverse/schedule 后端"。该用例在本会话所有改动**之前**即为红（已用 `git stash` 反证），修复需沿 `stage_pending_freeze()` / `preview_copy()` 路径追因（与 `T2-FREEZE-002` 的"无预览不中止整轮"语义相关），本次余量内未完成。注意：切换到 `trigger_position_window_t` 后该计数仍须保留（它是丢失证据，不是旧槽位语义）。**2026-10-15 追加观察（项⑤根因更清晰）**：该用例在不同轮次表现不同——先是断言不等（`freeze_missing_local_count != 1`），后一次是**子进程 5 s 超时挂死**（`subprocess.TimeoutExpired: out/pytest/runs/t2-runtime0/runtime.exe freeze_missing`）。⇒ 除计数语义外，"FREEZE 到达但本板无预览"这条路径很可能**阻塞**（等待/自旋），追因时优先看 `stage_pending_freeze()` 的 `preview_copy()` 失败分支与 `s_pending_mask` 的保持/重发循环（`trigger_t2_runtime.c:301-357`）是否会在无预览时反复进入而不推进。同批次内顺手把该用例从"挂死"变成"有界失败"更符合 `T2-FREEZE-002` 的"不中止整轮"口径。另：模板表容量已按本批扩容 `TRIGGER_ACTION_TEMPLATE_CAPACITY 8→16`（`trigger_action_template_table.h:10-13`，宿主点名集合 `188 passed / 1 failed`＝仅上述既有红，未提交、待批次 P3）。固件改动 ⇒ 契约登记 + C11 + 一次全项 P3。**页/ABI 约束（实施前必读）**：`TRIGGER_T2_RUNTIME_POSITION_COUNT` 同时定尺寸于快照结构（`trigger_t2_runtime_snapshot_t` 的 `freeze_version[]`/`freeze_local_version[]`/`freeze_local_age[]`/`action_*[]`）**与** SCPI 页的逐位置发射（`T2?` 第 19..27 字段、`T2:ACTion?` 第 5..19 字段）⇒ 3→8 会**移动既有页索引**，违反追加字段纪律。实施必须：既有 3 项区块保持原位不动，新增的 5 项**追加到各自页尾**（或另开一张独立诊断页），并在登记表补一条页契约变更；宿主 harness（`test_trigger_t2_runtime.py`、`test_t2_page_field_order.py` 等）随页形状同步改。**2026-10-15 收窄结论（优先方案，避开页/ABI）**：源侧每个位置本来就是"进窗→公告→冻结→滚出"顺序发布（实测 `announced 9→283`），其 `published_mask` 只表达**当前在途的 1~2 个位置**、`open` 集合同样很小 ⇒ 回执只需要覆盖当前在途那几个位，**窗口宽度不必扩到 8，也不必改任何页/快照**。因此 Step 1 可收窄为**纯从板侧**改动：① `accept_remote_position()` 不再把 `position_id` 当槽号（`position_id-1`）而按**实例**映射到 3 个滚动槽，接受域放宽到线上 `[1,15]`，新实例占用某槽时**清除该槽的冻结位**（回执位语义＝"当前在途实例是否已冻结"）；② 新增 15 位 `s_executed_instances` 防止同一实例的重复 FREEZE 再次布防；③ 槽在动作完成/放弃后回收，使从板沿扫描持续消费；④ 同批并入模板容量 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY 1→16` 与 `TRIGGER_ACTION_TEMPLATE_CAPACITY 8→16`（Step 3 前置）。判据不变：短扫描（`T2_SCAN_OVERRIDE_TERMINAL_MDEG`）一轮内从板累计 `events`/冻结覆盖全部位置、`lane drop` 不再单向增长、`ACTION execution` 覆盖全部目标板。上文的"扩到 8 位窗口"降级为**备用方案**（仅当需要同时在途 >3 个位置时再启用，届时按页尾追加处理）。 **2026-10-15 项⑤追因结论（已读固件侧）**：`stage_pending_freeze()`（`trigger_t2_runtime.c:313-356`）在无预览时只做三件事——把该槽位记入 `freeze_no_preview_mask`、`freeze_no_preview_count++`（每槽一次）、`continue`，**不返回错误、不自旋、不阻塞**；`s_pending_mask` 的位按 `T2-FREEZE-002` 刻意保留（等迟到预览或窗口到期放弃）。⇒ 固件侧不存在"无预览导致挂死"的循环，那个 5 s 超时更可能来自 **harness/driver 侧**（`runtime.exe freeze_missing` 的驱动在等一个它假定会发生的推进，或桩函数未返回）。故项⑤**降级为非关键**（只影响一条既有红诊断用例，不影响 Step 1 功能判据）：先做运行时切换与页/容量主线，之后再单独追这条 harness 稳定性问题。 **2026-10-15 本批进行中状态（工作区，未提交、刻意保留）**：从板侧已接窗口抽象（`trigger_position_window_alloc` 实例键分配 + `find` 得槽位 + 越界守卫 + 冻结时 `frozen_mask = ack_mask(window)` + 完成/失败 `EXECUTED/ABANDONED` 与 `release` 回收；描述符身份取 `count_epoch`，因该 legacy 描述符无 `target_ms`）。同一改动使 `tests/python/test_trigger_t2_runtime.py` 出现 **16 failed / 10 passed**，需**同批**处理的用例已逐条枚举：`receiver, fallback, freeze_stops_solve, gateway, independent_positions, reject_no_retry, stop, restart, identity, wrong_receipt, reject_uncertain, conflict, different_local_seq, all_events_retained, version_limit`（外加既有红 `freeze_missing`）。**分类要求（不可一律改期望）**：`identity`/`wrong_receipt`/`reject_uncertain`/`conflict`/`independent_positions` 等是拒绝与独立性用例，它们转红更可能因为"槽位现在可回收 + 源侧仍为 3 位簿记"，因此必须**与源侧切换一起**完成：源侧 `published_mask`/`freeze_complete_mask`/`freeze_abandoned_mask` → 窗口的 `open_mask`/`ack_complete`/`expire`，并改 `resend_one_freeze()`（`trigger_t2_runtime.c:186-216`）与 `stage_one_freeze()`（`:284-356`，其中 `published_mask |= …` 在 `:350`）的簿记来源。做完后再逐条判定"改期望"还是"补实现"。 **2026-10-15 页面与读数归属（四次误读后定案，务必先读）**：① 位置窗口累计 5 字段（entered/announced/frozen/executed/abandoned）追加在 **`TRIGger:RESERVation:T2:ACTion?`** 页末尾（`scpi_trigger_t2_runtime_commands.inc:365-371`，紧随其变长事件块之后），**不在 `T2?`** —— 在 `T2?` 上取尾 5 字段得到的是无关字段（曾据此得出"从板窗口全 0 / 7 是个数"等错误结论）。② 从板真正写 `frozen_mask` 的是 **`stage_pending_freeze()`（`trigger_t2_runtime.c:355`）**；`accept_remote_position()`（同文件 :466）在从板上**未被执行** ⇒ 单点接入该函数无效。③ 位掩码不是计数：`armed/completed/failed` 均为 3 位掩码（`7` = 0b111 = 3 个槽），`rt frozen`/`act armed` 同理 ⇒ 判"消费了几个位置"只能看 ① 的累计数。④ **对照实验结论（日志对照，无需再上机）**：在 `stage_pending_freeze()` 加窗口标记前后，slot1 `armed 7 / completed 7` 完全相同（`steps-20261015-132600.log` vs `steps-20261015-143456.log`）⇒ 单点接入对 3 槽可观测行为**没有影响**，上限在**位掩码与 3 槽数组本身** (`frozen_mask`/`published_mask`/`received_mask`/`s_pending_mask` 3 位、`s_deadline[3]`、`index = position_id-1` 受 POSITION_COUNT 约束) ⇒ **Step 1 必须按本行开头定义整批切换**（并同批改 `test_trigger_t2_runtime.py` 的 16 处旧语义期望），单点接入只是必要不充分。⑤ 另立条目、勿混入 Step 1 判据：短扫描下从板存在**既有的动作失败**（`failed` 非零、`backend 0 = 45`），在两份日志中都出现且与本次改动无关；它与既有红 `freeze_missing`（无预览路径挂死）应各自追因。 **开工先读（自服务交接）**：`docs/vdc/VDC_TASK_PROGRESS.md` 顶部条目 `VDC-PROGRESS-20261015-058` —— 现状 / 工作区未提交清单 / 四处读数更正 / 下一步照做清单（整批切换 → 驱动判据改 win_totals → 短扫描与全扫描 → 一次 P3 → 契约登记 + C11 + 文档分开提交）。








### T2-PROGRESS-20261014-006：用户裁定——调试阶段锁态只记录不阻断，K-3 据此改写

- TODO task ID：`T2-GATE-001`。
- **裁定（2026-10-14，用户原话）**：「调试阶段，目标是完善 T2 功能，而不是因为各种细节协议导致主线无法进行。**直接修改 K-3**。**T2 接受任何可达的状态，只是记录，不能拒绝或者阻断**。能跑通，能成功，就是调试阶段的目标。后续再细化规则。」
- **K-3 改写**（`TRIGGER_T2_ARCHITECTURE.md`）：由「VDC 进入 LOCKED 并发布之后 T2 才有输出；未发布不得生成或装填 deadline」改为「**调试阶段 T2 接受任何可达锁态，只记录、不阻断**；正式输出门禁（含非发布态拒绝）延后为独立任务，不阻塞主线」。同文档两处复述同步改写：TDMA RUN 中一节，以及跨域边界表「共同时间」行（后者并指明 T2 消费 `vdc_dpll_manager_lock_verdict()`、记录 `locked`/`provisional`/`published_qualified`/`reason`，仅作证据）。
- **TODO 同步**：总纲第 7 条改为「锁态只记录不阻断」；`T2-GATE-001` 状态 `BLOCKED` → `PENDING`、标题改为「VDC 锁态**记录**（正式输出门禁延后）」，并按 C12 把 1600+ 字的取证记录下沉到本条与 003/004/005，表内只留当前范围与指针。
- **未改写的历史原件**：`TRIGGER_T2_REVIEW.md` 的 K-3/A-02 段落与既有进展条目保留原状（评审与证据属历史记录）。**A-02 所描述的「未锁定即输出」在本裁定下不再是缺陷**，而是调试阶段的既定行为；其「应修复方向改为只放行已发布 LOCKED」的结论随之作废。
- **由此解除的阻塞**：原「VDC 需把「已达标并发布」的判定暴露给固件消费者」已由 `vdc_dpll_manager_lock_verdict()` 交付（提交 `f463ab92`，含 host 测试与 P3 凭证）；门禁**严格度取舍**（published_qualified 还是 locked）因本裁定**不再需要**——记录即可，无需在两者间选边。
- **剩余（本项 PENDING 的内容）**：T2 侧消费该判定并把 `locked`/`provisional`/`published_qualified`/`reason` 记入快照与证据，**不参与任何准入判断**。属固件改动，按硬约束 7 提交前需 P3 凭证。
- **未核验**：本条为文档裁定落地，未运行构建或硬件；T2 侧记录代码尚未实现。








### T2-PROGRESS-20261014-005：VDC 侧锁定判定已成固件单一入口（`T2-GATE-001` 的输入面就绪）

- TODO task ID：`T2-GATE-001`。
- **改动（VDC 域，3 个文件 + 1 个新测试）**：新增 `vdc_dpll_manager_lock_verdict()`（`components/vdc_dpll_manager/inc/vdc_dpll_manager.h`），一次读出 `input_ready` / `locked` / `provisional` / `published_qualified` / `reason` 与诊断计数；把原先**只存在于 SCPI 层**的 reason 阶梯与判断移入管理器实现；时间戳资格谓词导出为 `vdc_dpll_manager_timestamp_dpll_eligible()`；`SYSTem:SYNC:VDC:LOCK:READiness?` 改为薄委托，**24 字段线格式与 reason 编号逐字不变**。
- **行为等价依据**：阶梯在「输入路径全就绪但 DPLL 未锁」时回 `NOT_LOCKED`，故 `input_ready == (reason ∈ {READY, NOT_LOCKED})`、`locked == (reason == READY)`——原 SCPI 处 `input_ready` 与 `locked` 的两个布尔由此可证等价。
- **发布等价**：`published_qualified` 与 RefMem 写入方投影 `REFMEM_VECTOR_FLAG_LOCKED` 的合取一致；`provisional` 显式取自 `VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY`。**provisional 会话可 `locked` 而永不 `published_qualified`**——这与实测（台架四板 `flags` 恒为 `0x55`、bit5 恒 0）完全一致，属设计而非缺陷。
- **验证**：交叉编译全绿（`-Wall -Wextra -Werror`；期间一处 `-Wmaybe-uninitialized` 由显式清零修掉）；新增主机测试 `tests/python/test_vdc_lock_verdict.py` 从**真实源码抽函数体**驱动，覆盖全部 7 个 reason 档位、readiness 与发布的分野、provisional、以及 fine/health/freshness/gate/debug-continuation 各自单独扣留发布。测试过程**发现并固化了一条阶梯顺序**：`accepted_sample_count == 0` 的档位**先于** gate 档位（原期望写反，已按代码事实改正为 C6/C6b 两例）。定向集 35 passed（含 `test_vdc_publication_generation` / `test_refmem_vector_projection` / `test_dpll_flash_default`）；host 单测组 `run_vdc_domain_tests` ✓、`run_refmem_vdc_bridge_tests` ✓、`run_vdc_time_mapping_tests` ✓。
- **既有断裂（非本次引入）**：`tools/tests/run_refmem_vdc_vector_tests.ps1` 编译失败——其 include 列表缺 `components/sync_trigger/inc`，而未被本次改动触及的 `distributed_refmem.h` 会引入 `trigger_position_descriptor.h`；该组编译输入只有 `tests/unit/test_refmem_vector_table.c` 与 `refmem_vector_table.c`。据实记录，未修。
- **仍待（不在本轮）**：门禁**严格度**取舍（取 `published_qualified` 则台架不出输出、须走显式诊断开关；取 `locked` 则须把 provisional 轮标为诊断结果）——待用户裁定后 T2 才落码；P3 硬件验收凭证与提交（本轮已发起 full 验收）。
- **未核验**：主机测试与交叉编译不等于硬件行为；烧写后需比对 24 字段页面与改前基线（改前：NO.1 `input_ready 1 / locked 1 / reason 0 / dpll.state 5 / health 2 / accepted 529`；三从板 `input_ready 0 / locked 0 / reason 2 / dpll.state 1 / accepted 0`）。








### T2-PROGRESS-20261014-004：GATE-001 候选输入实测——发布锁定位在台架不可达，readiness 谓词无固件入口

- TODO task ID：`T2-GATE-001`。本条**更正** `T2-PROGRESS-20261014-003` 中「固件侧落 fail-closed 不改变台架行为」的表述（003 原文保留，不改写）：该结论只在门禁输入取 readiness 判据时成立；取**发布位**时台架将失去 T2 输出。
- **实测 A（只启用 observer，四板，静止态，24 s）**：对每板发 `SYSTem:SYNC:VDC:OBServer:TDMA 1,0,100,0` 后，原点保持 `locked=1 / accepted=529`，**三块从板持续 `locked=0 / accepted=0`**——即从板的锁定**不能**只靠启用 observer 达成，需要环运行中收到 VDC 同步样本（与 `t2_workflow` 步骤 16→17 的实际顺序一致）。
- **实测 B（同一窗口读 `SYSTem:REFMEM:VDC:VECtor?`）**：四板 `flags` 恒为 **`0x55`** = `VALID | SCHEDULE_VALID | HARDWARE_EVIDENCE | PROVISIONAL`；**`REFMEM_VECTOR_FLAG_LOCKED`（bit 5）始终为 0**，**包括自报 `locked=1`、`accepted=529` 的原点**；`PROVISIONAL`（bit 6）为 1。
- **成因（代码直读）**：`distributed_refmem_vector_flags()` 置 LOCKED 位的前置包含「**非 provisional**」与 `quality.lock_state == LOCKED`、HEALTHY、fine tier、freshness 未超界、gate 通过等严格合取；而**台架故意开 provisional**——`t2_workflow.py` 步骤 6 即发 `SYSTem:SYNC:VDC:DPLL:ProVisional 1`。故该发布位在台架上**结构上不可达**。
- **访问器现状（代码检索）**：发布面有固件入口 `distributed_refmem_get_vdc_vector_snapshot()`（`distributed_refmem.h`），且 `components/sync_trigger` **已依赖** `distributed_refmem`（`trigger_t2_runtime.c` 即在调 `distributed_refmem_get_position_descriptor()`）；但 **VDC 域没有任何 readiness/locked 谓词的固件访问器**——该谓词只存在于 SCPI 层（`middleware/scpi_port/src/scpi_sync_commands.c` 的 `scpi_sync_vdc_lock_readiness_reason()` 与处理器内联计算）。
- **对 GATE-001 的净结论**：三个候选输入互不等价——① readiness `locked`：台架可达，但**无固件入口**，T2 自行重算即违反 K-3「不自行判定锁态」；② 发布位 `REFMEM_VECTOR_FLAG_LOCKED`：**合 K-3 且有固件入口**，但台架因 provisional 不可达，取它则台架失去 T2 输出（除非改用非 provisional 验收，或按本项备注的显式诊断开关放行）；③ 本板 `dpll.state == LOCKED`：有入口但属自行判定。⇒ 本项**仍阻塞于 VDC 域**，且阻塞内容现可精确表述：**VDC 需把「已达标并发布」的判定（含 provisional 语义）暴露给固件消费者，而不只暴露给 SCPI**。
- **未核验**：未运行 pytest / 构建 / 四板 P3；实测 A/B 为静止态、环停止时的读数（未见运行中向量可见性）；未验证从板侧收到的发布向量是否与原点一致。








