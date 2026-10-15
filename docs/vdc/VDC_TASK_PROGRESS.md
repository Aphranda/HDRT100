# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-10-15

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
### VDC-PROGRESS-20261015-058：**自服务交接**——T2 Step 1 现状、四处读数更正、整批切换的下一步（新窗口/新代理无需人类复述）

- TODO task ID：`T2-DEPLOY-003`（主）、`T2-IDENT-002`、`T2-TURNTABLE-002`。值为 2026-10-15 实验快照，非产品事实源。
- 目的：把交接固化在仓库里。**任何新上下文窗口/新代理读完本条 + `docs/trigger/t2/TRIGGER_T2_TODO.md` 的 `T2-DEPLOY-003` 行即可开工**，不需要用户再解释背景。
- 已提交地基（10 个，勿重做）：`60bcd6a1`（`trigger_position_window.h` 位置实例窗口抽象 + 6 组宿主用例 + harness + 工具累计读数）、`67705adb`/`61a26fb1`（三处口径收敛 → `TRIGGER_POSITION_INSTANCE_MAX`）、`5f1d26b3`（源侧语义 `open_mask`/`ack_complete`/`find_target`/`expire` + 第 7 组）、`cc69569d`（迭代纪律：先烧录+T2 验证功能，批次收口才付一次 P3）、`b87a2cc3`（批次边界：只切一侧必红）、`6b9fd53e`/`d090ceb2`/`49bbbe97`（既有红 `freeze_missing` 定位与追因）、`87e101ad`（下述四条更正入库）。
- 工作区**未提交、已宿主验证**（可接续或回退）：`trigger_action_template_table.h`（容量 8→16）、`trigger_position_window.h`（`instance_at`）、`trigger_t2_runtime.h/.c`（从板两处接入 + 粘滞回执 + 窗口累计访问器）、`scpi_trigger_t2_runtime_commands.inc`（`T2:ACTion?` 尾追加 5 字段）、`tests/unit|python/test_trigger_position_window.*`（8 组用例 + 线上绑定）、`tools/sync_trigger/t2_workflow.py`（step 17 分层 + 真计数读数）、`tools/sync_trigger/t2_iterate.py`。宿主点名集合：**40 passed / 1 failed**（唯一失败＝既有红 `freeze_missing`）。
- **四处读数更正（本会话踩过四次，务必先读）**：
  1. 窗口累计 5 字段（entered/announced/frozen/executed/abandoned）在 **`T2:ACTion?`** 页尾（`middleware/scpi_port/src/scpi_trigger_t2_runtime_commands.inc:365-371`），**不在 `T2?`**；在 `T2?` 取尾字段会得到无关值（曾据此误判"从板窗口全 0"）。
  2. 从板真正写 `frozen_mask` 的是 **`stage_pending_freeze()`（`trigger_t2_runtime.c:355`）**；`accept_remote_position()`（:466）在从板**未被执行**。
  3. `armed/completed/failed`（`T2:ACTion?`）与 `rt frozen`/`act armed`（工具 `WIN` 行）都是 **3 位掩码**（`7` = 0b111 = 3 槽），不是计数；判"消费了几个位置"只能看更正 1 的累计数。
  4. **对照实验（日志对照，无需上机）**：在 `stage_pending_freeze()` 加窗口标记前后，slot1 `armed 7 / completed 7` 完全相同（`out/tdma_t2/steps-20261015-132600.log` vs `steps-20261015-143456.log`）⇒ 单点接入对 3 槽可观测行为**无影响**，上限在**位掩码与 3 槽数组本身**（`frozen_mask`/`published_mask`/`received_mask`/`s_pending_mask` 3 位、`s_deadline[3]`、`index = position_id-1` 受 `POSITION_COUNT` 约束）⇒ **Step 1 必须整批切换**，单点接入只是必要不充分。
- **两条既有问题（各自追因，勿混入 Step 1 判据）**：① 短扫描下从板存在既有动作失败（`failed` 非零、`backend 0 = 45`），两份日志都有且与本次改动无关；② 既有红 `test_trigger_t2_runtime[freeze_missing]`（无预览路径挂死；固件侧 `stage_pending_freeze()` 已证明不阻塞，故归 harness 侧）。
- **下一步（照做，勿再问背景）**：① 整批切换——`trigger_t2_runtime.c` 两侧掩码/数组（`published_mask`/`freeze_complete_mask`/`freeze_abandoned_mask` 与 `frozen_mask`/`received_mask`/`s_pending_mask`、`s_deadline[]`/`freeze_version[]`/`freeze_local_version[]`/`freeze_local_age[]`）→ `trigger_position_window_t`，并按实例而非槽号寻址、槽位可回收；**同批**更新 `tests/python/test_trigger_t2_runtime.py` 的 16 处旧三槽语义期望（第 26 轮实测：只切一侧必红）；② 驱动判据改为 `win_totals[2]/[3] > 3`（真计数），并把 `failed`/`backend` 非零单列报出；③ 验证 `T2_SCAN_OVERRIDE_TERMINAL_MDEG=8000 python tools/sync_trigger/t2_iterate.py`（短扫描）→ 去掉 override 全扫描；④ 收口：一次 `python tools/hardware_acceptance/hardware_acceptance_workflow.py --profile full`（P3 凭证，硬约束 7）+ 契约登记（`T2-DEPLOY-003`/`T2-IDENT-002`/`T2-TURNTABLE-002`）+ C11 交叉复核 + 文档与代码**分开提交**。
- 下一 gate：`T2-DEPLOY-003`（整批切换完成并宿主点名集合全绿后，才进入台架短扫描）。
- **2026-10-15 追加（诊断计数的实现教训）**：给 `trigger_position_window_t` 加 "被拒绝的 mark/alloc" 计数时，**不能**按 `alloc(` 到 `mark(` 的区间整体替换 `return false;` ——该区间含多个 `bool` 辅助函数，替换会插到没有 `w` 的作用域 ⇒ 宿主立即转红（`1 failed, 26 errors`）。正确做法：先把每个函数体按各自结束花括号切出，只在 `alloc()`/`mark()` 体内计数；本次已回退该插入（树回到 `26 passed / 1 failed`＝仅既有红），字段声明保留但未使用，页面与驱动的 5 字段契约也已恢复一致（并在驱动里注释了"改字段数须同批改页面"）。⇒ 下一步第一件事仍是暴露"实例停在哪个状态 / mark 为何被拒"，但按上述作用域方式实现。
- **2026-10-15 追加（本窗口实测）**：驱动判据已由"位掩码"改为**真计数**（`win_totals[2]/[3]`，来自 `T2:ACTion?` 尾 5 字段），并**在台架上验证生效**：`steps-20261015-145141.log` 的结论为 `slot 1/2/3: window frozen 0 executed 0 FAIL (not beyond the legacy 3)`、`note: role 1/2 action failed mask = 1（既有条目，不计入 Step 1 判据）`、`ACTION execution 2/4`、`failed steps: none` ✓（此前的"criteria met"是掩码误判，已消除 ✓）。
- **该实测同时给出下一步的关键读数**：从板窗口 `frozen=0 / executed=0` 而 `entered` 曾达 3~4 ⇒ `alloc` 生效但 `mark(ANNOUNCED/FROZEN)` **没有生效**（否则 `frozen` ≥ 1）。下一步诊断（二选一，均为小改动）：① 在 `trigger_position_window_t` 内增计"被拒绝的 mark"计数并暴露；② 在页面上暴露每条记录的 `state`（`record[i].state`）以看出实例停在哪个状态。确认停因后再决定注入点（很可能是 `accept_remote_position()` 与 `stage_pending_freeze()` 两个 alloc 的实例身份/状态机冲突）。

### VDC-PROGRESS-20261015-057：Step 1 根因**定案且不需要 wire 契约选择**——8 位回执是"待确认窗口"，三槽限制在运行时自己的簿记

- TODO task ID：`T2-DEPLOY-003`、`T2-IDENT-002`、`T2-TURNTABLE-002`。值为 2026-10-15 实验快照，非产品事实源。
- **更正上一轮的判断**：上一轮我把"从板沿 361 位置消费"列为需要 A/B 契约选择（run id 当回执代 vs 回执改绝对身份），**这是错的**。读源侧用法后定案：
  - `components/distributed_refmem/src/distributed_refmem_position_ack.inc:68-92`：回执 = 4 位 run id（opcode）+ **8 位冻结掩码**；
  - `components/sync_trigger/src/trigger_t2_runtime.c:186-216`（`resend_one_freeze()`）：`open = published_mask & ~(freeze_complete_mask|freeze_abandoned_mask)`，`freeze_complete_mask |= open & position_freeze_acked(run_id, peers)` ⇒ 源**只把回执与"发布但未完成"的集合按位与**，落在集合之外的陈旧位被忽略 ✓。
  ⇒ 8 位掩码是**当前待确认窗口**，不是全量历史；一个回执代足以表达 ≤8 个待确认位置 ✓，**不需要改 run id 语义、不需要改回执格式**（wire 不变）。
- **真正的三槽限制在运行时自己的位置簿记**（这才是 Step 1 的落点）：`published_mask` / `freeze_complete_mask` / `freeze_abandoned_mask`（源侧）与 `frozen_mask` / `received_mask` / `s_pending_mask` / `s_deadline[3]`（从板）全是 **3 位**；叠加 `accept_remote_position()` 的 `position_id > TRIGGER_T2_RUNTIME_POSITION_COUNT ⇒ reject` 与"冻结位置永久占用"（`trigger_t2_runtime.c:415-443`）、`TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY = 1`（`:747-766`）⇒ 从板一生只接受 3 个实例、源侧也只簿记 3 个位置，与四板实测（从板累计 `events 2/2/0`、`lane drop` 单向增长）逐项吻合。
- **Step 1 修正后的范围（无需 A/B）**：把上述位置簿记从 **3 位窗口扩到 8 位窗口并使之可回收**——
  1. `accept_remote_position()` 接受 `position_id ∈ [1,15]`（对齐线上域），按实例→槽映射，已完成/放弃的槽可回收；新增 15 位"已执行实例"集合杜绝重复 FREEZE 造成的重复执行；
  2. 源侧 `published_mask`/`freeze_complete_mask`/`freeze_abandoned_mask` 与从板 `frozen_mask`/`received_mask` 同步扩到 8 位窗口（**在途预测窗口仍保持 1~2/3**，不回退到 361 行、不装长表）；
  3. 同批并入 Step 3 的 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY 1→16`、`TRIGGER_ACTION_TEMPLATE_CAPACITY 8→16`。
- **判据**：先用短扫描（工具已支持 `T2_SCAN_OVERRIDE_TERMINAL_MDEG`，≤8 位置，落在回执能力内）跑一轮，从板累计 `events`/冻结须覆盖**全部**位置、`lane drop` 不再单向增长、`ACTION execution` 覆盖全部目标板；随后全扫描判据＝从板累计冻结数与源 `announced` 同阶增长。固件改动 ⇒ 契约登记 + C11 + 一次全项 P3（含仍待提交的工具读数改动）。
- 备注：本轮尝试在自动轮次用 `update_goal edit` 更新目标被策略拒绝（需直接人类回合），故这条更正先落在本日志；目标下次人类回合再同步。

### VDC-PROGRESS-20261015-056：Step 0 定因——**窗口在源侧按设计滚动（我上轮读错了板）**；真缺口是"从板只冻结 3 个位置，而源已公告 283 个"

- TODO task ID：`T2-TURNTABLE-002`（窗口推进）、`T2-DEPLOY-003`（从板消费）、`T2-IDENT-002`（位置域口径）。值为 2026-10-15 实验快照，非产品事实源。
- **更正**：上一轮我据 `T2:SCAN?` 的 `window_entered 2 / announced 0` 断言"窗口不滚动"，**读错了板** —— 工具那次读的是**从板**的页，而滚动发生在**源板**。本轮改为每 tick 对四板同时读，结论反转。
- **实测（`out/tdma_t2/steps-20261015-102327.log`，每 2 s × 5 tick）**：

| 板 | entered | announced | pred_err | 判读 |
|---|---|---|---|---|
| slot 0 COUNTER（源） | 9→78→147→216→**285** | 7→76→145→214→**283** | **0** | ✓ 随 count 单调滚动，恒为 `entered−2`（1~2 个在途未公告，正是设计口径） |
| slot 1 SWITCH_A | 2（五个 tick 不动） | 0 | 0 | 从板不预测；只 `frozen 3` |
| slot 2 GATEWAY | 2 | 0 | 0 | 同上，`frozen 3`，本轮 `action done 3` ✓ |
| slot 3 SWITCH_B | 2 | 0 | 0 | 同上，`frozen 3`，本轮 `action done 3` ✓ |

  ⇒ **既不满足 ①（advance 未被调用）也不满足 ②（BACKWARD 锁死）**：源侧 `predictor.last_error == 0` 且 entered 随 count 增长 ✓ ⇒ `window_advance()` 运行期正常调用、无后退锁死。
- **真正的缺口**：源已公告 **283** 个位置，而从板只 `frozen 3`（三个 tick 值不变）⇒ **从板没有沿扫描消费被公告的位置序列**，仍是早期三槽结构（与报告 F3/F5 "消费者仍按早期三槽结构取用位置"、`T2-IDENT-002` 记录的 `position_id ≤ 3` 字面量一致）。分发侧同轮仍严重背压：`lane drop 46476 / 45684 / …`、`action done 3`。
- **口径副作用**：源侧 `mismatch 1199` 恒定，是 361 扫描与 legacy 3 槽位掩码的口径差（工具已在 `positions==3` 前提下才判该值），不是失败。
- **Step 1 重新界定**：不再是"让窗口滚动"（已成立），而是**从板侧消费**——为什么源公告 283 而从板只冻结 3：查冻结 latch 的 `position_id` 域（`T2-IDENT-002` 的三处口径）与运行时消费路径，判据 = 一轮内从板冻结/动作覆盖与源公告同阶。
- **本轮另见**：T2 流程 step 10（模板/动作安装）本轮偶发 FAIL（同轮 `--full` 收尾；此前多轮 OK）；`tests/python/test_trigger_t2_runtime.py::test_t2_runtime[freeze_missing]` 为**既有红**（本轮工作区改动仅工具/文档/产物，未动固件与测试），应单独登记排查。

### VDC-PROGRESS-20261015-055：角色动作序列实测——**解码片到达的角色 3/3 完成动作**；缺口是"每轮总有一块目标板收不到片"

- TODO task ID：`T2-DEPLOY-003`（多板槽位业务闭环）、`T2-TRANSPORT-002`。值为 2026-10-15 实验快照，非产品事实源。
- **角色→动作**：部署把四板定为 `COUNTER(slot0) / SWITCH_A(slot1) / GATEWAY(slot2) / SWITCH_B(slot3)`；动作表按**本板角色**筛选（`trigger_action_service.c:109` 只把 `row.role == binding.local_role` 的行进 `local_row_mask`），T2 运行时按本板模板种类在计划 deadline 上布置 **SWITCH 或 GATEWAY 截止**（`trigger_t2_runtime.c:670-710`）。⇒ "不同角色执行不同动作序列"在结构上已成立；本次给出**实测判据页** `TRIGger:RESERVation:T2:ACTion?`（`scpi_trigger_t2_runtime_commands.inc: scpi_cmd_trigger_t2_action_q`，含 armed/completed/failed/uncertain 与每位置 sequence/deadline/backend_state/origin_width）。
- **实测（两轮 `--full`，均 `failed steps: none`）**：
  - `out/tdma_t2/steps-20261015-091127.log`：SWITCH_A `armed 3 / completed 3 / failed 0 / uncertain 0 / events 2` ✓、GATEWAY 同为 `3/3/0/0` ✓、SWITCH_B `0/0/0/0` ✗、COUNTER 不动作 ✓；汇总 `ACTION execution: 2 of 4`。
  - `out/tdma_t2/steps-20261015-092034.log`：SWITCH_A ✓、**SWITCH_B ✓**、**GATEWAY ✗**；同样 `2 of 4`。
  - 配套 `RUNTIME` 行（T2? 页）：执行成功的板 `received 7 / frozen 3`；未执行的板 `received 0 / frozen 0 / preview_saved 0 / prep_reason 1`（其车道上仍接受了 2443 片）。
- **机制**：位置片经一等席车道送达每块目标板，但**每板只有一个单深度位置邮箱**：邮箱未腾空时新片被拒（`lane_reject` 14k–69k），被拒片进入有界 hold 环，环满即丢（`hold_drop` 26k–69k）。若某板在源密集发布时没抢到"新鲜片"，此后看到的几乎全是同一片的重发（`lane_dup ≈ lane_accept`）⇒ `received_mask` 恒 0 ⇒ 该板无法布置动作。两轮各恰好一块板饿死，且**饿死者在轮换**（运行 A 是 SWITCH_B、运行 B 是 GATEWAY）⇒ 这是**竞争型分发 + 有界缓冲**的固有结果，不是某一角色的功能缺陷。
- **结论**：解码位置片 → 按角色执行动作序列这一环**已验证可用**（收到片的角色 3/3 完成，`failed/uncertain` 全 0）；要让**每个**目标角色每轮都执行，必须改分发语义（每目标独立副本/独立邮箱、加深 hold 环、或按目标单播）——属 wire/运输契约层决策。
- **工具新增（`tools/sync_trigger/t2_workflow.py` step 20）**：`ACTION slot …`（含每位置明细）、`ACTION execution: N of 4`、`RUNTIME slot …`（received/frozen/prepare/preview_saved/prep_reason/desc_reject/trans_reject/last_error），与既有 `POSITION slot …`/`POSITION distribution …` 一起使"谁执行了什么、谁没收到"成为可见事实。

### VDC-PROGRESS-20261015-054：**位置分发实测成立**——T2 经一等席车道分发到三从；并更正上一轮基于 `rx_complete_count` 的错误结论

- TODO task ID：`T2-TRANSPORT-002`/`T2-IDENT-001`（位置运输证据）、`VDC-ISO-001`。值为 2026-10-15 实验快照，非产品事实源。
- **先更正**：上一轮我据 `SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 的 `rx_complete_count = 0` 判断"没有任何从板完成位置"，**是错的**。该计数器只属于**经典 mailbox** 路径；当一等席/优先级车道拥有位置运输时（`mailbox[3] == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS`），经典解析被**设计性跳过**（`components/distributed_refmem/src/distributed_refmem.c:1893-1896` + `s_position_priority_owned`），因此 `rx_complete_count` 在完全正常的分发下恒为 0。判定位置分发的正确证据是**车道路径**计数：`lane_accept_count` 只在"解码成功且已提交到本地位置邮箱/冻结锁存"时自增（`distributed_refmem_position_ack.inc:287`）。
- **实测（`out/tdma_t2/steps-20261015-083111.log`，`--full` 22 步、`failed steps: none`）**：
  - 源 slot 0：`tx_publish 22935`、`tx_seq 24659`、`tx_phase 5`（持续发布切片）；
  - 从 slot 1/2/3：`lane_accept 22860 / 11748 / 25481`、`lane_dup 24841 / 12033 / 27596`、`lane_reject 3257 / 30768 / 4451`、`owned 1`、`hold 2623 / 1614 / 3888`、`drain 2158 / 1329 / 3184`、`hold_delivered 461 / 281 / 704`、`drain_fail 1 / 0 / 0`；
  - 工具新增判定行：`POSITION distribution: source tx_publish 22935; follower lane_accept [(1,22860),(2,11748),(3,25481)] -> DISTRIBUTED`。
  ⇒ **T2 可以分发位置**：三从各自解码并提交了上万条切片，背压（单深度邮箱拒绝→有界 hold 队列→drain 重投）按设计工作；`lane_dup` 为导线重发计数。
- **工具修正（`tools/sync_trigger/t2_workflow.py`）**：位置运输诊断改为按页面字段名逐一打印（tx_publish/tx_seq/tx_frag/tx_phase、lane_accept/dup/reject/owned/hold/drain/drain_fail/hold_drop/hold_delivered/decode_reject、classic rx_complete/rx_reject/rx_pending_mask/rx_last_failure），并新增**全板**的 `POSITION slot …` 明细与 `POSITION distribution … -> DISTRIBUTED/PARTIAL/NO DELIVERY` 判定；原先误把 `tx_position_id` 标成 `POS pub`、`rx_pending_mask` 标成 `rx` 的标签一并纠正。判据只在"源已发布而从板无一提交"这种明确情形下失败。
- **仍待观察（非本轮目标）**：NO.3 的 `lane_reject 30768` 远高于其 `lane_accept 11748`（单深度邮箱背压），`hold_dropped` 与 `decode_reject` 已加入读数以便后续量化丢弃率；若要提高分发余量，方向是背压/邮箱深度而不是观测者。

### VDC-PROGRESS-20261015-053：**T2 跑通**——四步齐绿（step 17–21），1:1 负载在位、三从锁定并匹配

- TODO task ID：`VDC-ISO-001`（T3 关闭）、`TDMA-ISO-002`（1:1 轮换保留）。值为 2026-10-15 实验快照，非产品事实源。
- **最后的闸门是判据本身**：`t2_workflow.py` 的 step 17 原实现要求**四块板**都报 `READ:SYNC:STATe? = "LOCKED"`，但参考板是**源**（无可锁对象，本台无物理参考），其自身锁阶梯恒为 OFF——实测**每一次 T2 运行**参考板都是 `1,1,0,536870913,0,0,"U"`（`locked 0 / dpll.state 0`），而已知良好的 DPLL60 验收判据建立在**内部轨迹**上（`p3_dpll_short.py:_assess_internal_trace`），从不依赖这一位。工具自身 docstring 也写明"参考与从板判法不同"。
- **修复**（工具侧，无固件改动）：参考板改按**是否在服务**判定（readiness `input_ready == 1`），从板仍要求 `"LOCKED"`；参考板实测值照旧打印并标注 `(source, judged by serving)`。
- **实机（`out/tdma_t2/steps-20261015-052737.log`，`--full` 全 22 步）**：`failed steps: none`
  - step 17 OK：三从 `SYNC:STATe LOCKED`、`locked 1`、`dpll.state 1`、`reason 0`；参考 `(source, judged by serving)`；
  - step 18/19/20/21 全部 OK；step 20 运行采样：T2 counter 11k→284k、`published_mask 3`、`announced 3`、`prep 256047`；**一等席 1:1 轮换在位**：`seat opp/pri/fc ['127228','108011','19217']`；`travel ... -> OK`。
  ⇒ 负载在位、基础件事件观测存活且从板锁定/匹配，T2 全流程跑通。
- **结论**：本轮目标（负载不得让基础件事件观测失效；观测者只对非前进/回绕判失效；前进跳号作有界证据；ordinal 严格把关；最终跑通 T2）**已在实机全部达成并留下证据**。

### VDC-PROGRESS-20261015-052：**T3 收口**——负载下三从全部成匹配；step 17 只剩参考侧锁声明（提交 bf8b94b1，P3 凭证）

- TODO task ID：`VDC-ISO-001`（T3 关闭：观测者语义 + 历史不被退休 + 实机匹配）、`TDMA-ISO-002`（1:1 轮换保留）。值为 2026-10-15 实验快照，非产品事实源。
- **修复（已提交 `bf8b94b1`，全项 P3 PASS、凭证 build `20261014211528`）**：
  1. `tdma_event_history.c`：严格递增规则对**无见证字**（`0x0`/`0xFFFFFFFF`，空闲/间隙读）豁免——按上一真实见证排序、且不成为后续排序的参考；重复/后退/跨 epoch 回绕仍拒绝。⇒ 事件历史不再因共享导线被 `BAD_SEQUENCE` 退休。
  2. `vdc_priority_match.inc`：精确查找返回 `EVICTED` 时改为与 **LIVE 保留观测**关联（`anchor_fallback` 计数；epoch/arm/tick/flags/timer1 括号证明全部保留）；`RETIRED` 仍按生命周期跳过。
  3. 判别读数（无停机门禁）：`LOCK:READINESS?` 追加 matcher 活体状态与其交给门面的 arm/epoch/tick；`EVENt:SNAPshot?` 追加采样点与门面前置条件（`history_active/epoch/count/reason`、`observer_epoch`、`armed/arm_valid/arm_epoch`）；T2 工具打印 `match_live`/`match_arm`/`facade`/`sample_point`。
- **实机（T2 轮 `out/tdma_t2/steps-20261015-050623.log`）**：三从**全部成匹配** `matched 9754 / 9965 / 9823`、`have_match 1`、`last_reason 0`、`anchor_fallback 4`；step 17 现在**只**卡参考板 `NOT locked on ['0010071E65B5CB38']`（参考 `input_ready 1 locked 0 reason 0`）。
- **判定**：目标中"负载不得让基础件事件观测失效；观测者只对非前进/回绕判失效；前进跳号作有界证据；事件流连续性由 ordinal 把关"**已在实机达成**（比要求更强：从板在负载下不仅观测存活，而且形成了匹配）。T2 剩余闸门转为**参考侧锁声明**（`VDC-HOLD-001`/V2 家族，另案）。
- **下一 gate**：参考板 `locked 0 / reason 0`（`dpll.state 0`）——按 V2（freshness→HOLDOVER）与 master 锁阶梯排查，使 step 17 四板齐锁。

### VDC-PROGRESS-20261015-051：T3 根因定案——**无见证（哨兵）记录打破历史的严格递增规则，使从板历史被退休**（NO.4 因此能匹配）

- TODO task ID：`VDC-ISO-001`（T3 根因定案）、`TDMA-ISO-002`。值为 2026-10-15 实验快照，非产品事实源。
- **活体判别链（本轮新增读数，全部在无停机门禁页上，工作区待提交）**：
  - `LOCK:READINESS?` 追加 matcher 活体状态（`calls/matched/rejected/superseded/history_miss/pending/busy/last_reason/first_reject_reason/history_result/anchor_fallback/have_match`）与 matcher 交给门面的 `arm_epoch/observer_epoch/tick_hz`；
  - `EVENt:SNAPshot?` 追加采样点（`prefix_bits`、首字）与**精确门面前置条件**（`history_active/history_epoch/history_count/history_reason/observer_epoch/armed/arm_valid/arm_epoch`）。
  - 关键更正：`history_result 4` 是 **RETIRED**（不是 EVICTED；枚举顺序为 OK/BUSY/PENDING/EVICTED/RETIRED/BAD_ARGUMENT/BAD_STATE）。
- **定案（T2 轮 `out/tdma_t2/steps-20261015-042805.log`）**：
  - 失配从板 NO.2/NO.3：`history_active 0`、`history_count 0`、**`history_reason 10 = BAD_SEQUENCE`**、`armed 1`、`arm_valid 1`、`arm_epoch 2`、`obs_epoch 1`、matcher 输入 `arm_epoch 2 / observer_epoch 1 / tick_hz 250 MHz` **全部一致** ⇒ 前置条件都成立，是**历史自己因序号被拒而退休**；
  - 健康从板 NO.4：`history_active 1`、`history_count 16`、`history_reason 0` ⇒ **`matched 10521`、`anchor_fallback 3~7`、`have_match 1`** —— 即**负载下从板匹配已在同一轮的 NO.4 上成立**，且锚点关联真的在产出匹配。
  - 机制：`0x0/0xFFFFFFFF` 无见证记录被按 -046/-048 的口径**保留**（记录照常成），但其 `sequence` 是哨兵值 ⇒ 在历史看来是"重复/后退" ⇒ `BAD_SEQUENCE` ⇒ `tdma_event_history_retire()` ⇒ 之后每次精确查找都返回 RETIRED ⇒ matcher 以 `SOURCE` 拒绝 ⇒ 永不匹配。没有哨兵的从板（NO.4）历史存活 ⇒ 正常匹配。
- **修复方向（已用两轮试验确认边界）**：把**记录身份与线序见证解耦**——记录用本 epoch 的事件编号（0,1,2,…，与 ordinal 同步递增），线序见证只作证据；这样历史永远严格递增 ⇒ 不退休 ⇒ 配合已实现的锚点关联即可在负载下匹配。**代价**：6 个宿主 harness 需按"record.sequence = 事件编号"改期望（上一轮试做时已定位到具体断言），因此留到下一轮成批完成并刷写验证。
  - 备选（代价更小但不丢事件）：哨兵记录**不进历史**（只作证据），历史只收有见证的记录——需确认 matcher 的 `event_sequence` 关联仍成立。
- **本轮已交付**：锚点关联（宿主 294 项绿）、历史严格递增规则（gap 允许、重复/后退拒绝，宿主绿）、全套活体判别读数；`306 passed`。目标保持 active。

### VDC-PROGRESS-20261015-050：锚点关联已实现（宿主全绿）；但实机证明**历史查找已不是当前阻塞**——matcher 在更早的 SOURCE 检查就退出

- TODO task ID：`VDC-ISO-001`（T3 剩余闸门再定位）、`TDMA-ISO-002`。值为 2026-10-15 实验快照，非产品事实源。
- **本轮实现（工作区，未提交）**：匹配器的**锚点关联**——当精确历史查找返回 `EVICTED`（typed 记录的编号落在保留窗口之外）时，不再跳过，而是与 **LIVE 保留观测**关联（epoch/arm/tick/flags/timer1 括号等全部证明照旧），并用新字段 `anchor_fallback` 计数；`MATCH:STATus?` 末尾追加该字段（既有索引不变）。宿主 `test_vdc_priority_match.py` 的 `evicted` 用例按新语义改写（并放宽投影 stub 的括号断言为"精确副本或 LIVE 锚二选一"），`294 passed` 全绿。
- **实机反证（刷写后 T2 轮 `out/tdma_t2/steps-20261015-025012.log`）**：三从 `MATCH:STATus?` 仍 `calls ≈12.5 万 / matched 0 / rejected = calls`，但**`history_miss = 0` 且 `anchor_fallback = 0`** ⇒ 精确查找的 EVICTED 分支（以及我的关联分支）**都没有在这轮被执行**；发布出的 `history_result 4` 是更早某次调用的残留。同时 `first_reject_reason = 9 (SOURCE)` ⇒ 当前阻塞是**更早的 SOURCE 检查**（rx 绑定/slot/target_mask/live-valid 一类），而不是历史查找口径。
- **下一 gate（明确）**：把 matcher 的**活体**拒因暴露到无停机门禁的页（`LOCK:READiness?` 末尾追加 `last_reason/first_reject_reason/anchor_fallback/history_result`），在同轮 T2 里指名是哪一个 SOURCE 前置检查在负载下失败；随后按该检查修复。观测者语义（目标前半）已闭环，不受影响。

### VDC-PROGRESS-20261015-049：采样点实读——观测者采到的是"另一套编号"，而事件空间三数彼此吻合

- TODO task ID：`VDC-ISO-001`（T3 剩余闸门的又一层定位）、`TDMA-ISO-002`。值为 2026-10-15 实验快照，非产品事实源。
- **新增活体读数（工作区，待随下一次 P3 提交）**：`SYSTem:TDMA:EVENt:SNAPshot?` 末尾追加 `prefix_bits`（观测者采样位点）、`fifo_sequence_first`（采到的原始序列字）、`fifo_tx_first`/`fifo_rx_first`/`fifo_rx_second`（配对字），T2 工具打印 `sample_point …`。
- **实测（T2 轮 `out/tdma_t2/steps-20261015-021025.log`，NO.2）**：
  - 事件空间三数彼此吻合：`joined 72833`、`typed_lane carrier 72485`、`source_event 72482`（相差 ≤350）；
  - 观测者见证/采到的字却完全是另一套：`last 0x72966 (=469350)`、`seq_first 0x52232448`、`rx_first 0x3617567520`（≈9.0e8 量级）；
  - 采样位点 `prefix_bits 120`（≈帧内第 15 字节结束，正是传输帧序号字段 `PACKET_HEADER(4)+8..11` 的位置）。
  ⇒ **采样位点是对的，值不是同一套编号**：观测者序列流采到的字来自与 typed 车道不同的传输/字段（负载与特等共线后，导线上并非所有帧都携带同一序号空间）。这正是"历史按线序见证索引 vs 匹配器按源事件序号查询"（`-047`）的物理来源。
- **候选修复（需择一，均为基础件身份口径）**：
  - **R1′（匹配侧，推荐）**：关联改按"时间括号"——在已保留窗口内选取与 live 锚括号一致的记录（epoch/arm/tick/括号校验不变），彻底摆脱计数器口径；配对错一帧会让残差出带、锁不上（安全失败），不会产生假锁。
  - **R2′（观测侧）**：把序列 SM 严格门控到与配对 SM 相同的帧（PIO/触发条件），使见证恒等于该帧的传输序号。
  - **R3′（参考侧）**：typed payload 携带承载帧的传输序号（wire/codec 语义变更，需登记+C11）。
- **状态**：目标前半（观测者语义/回绕契约）已完成并提交（`c286228f`、`f5dde452`）；"最终跑通 T2" 卡在上述身份口径，目标保持 active。

### VDC-PROGRESS-20261015-048：见证规则收窄到"非回绕"——保住 epoch 内回绕语义，同时容忍负载哨兵

- TODO task ID：`VDC-ISO-001`（T3 见证规则细化）、`TDMA-ISO-002`。值为 2026-10-15 实验快照，非产品事实源。
- **问题**：`-046` 的哨兵规则把 `0xFFFFFFFF` 一律当"无见证"，与基础件本来的语义冲突——`0xFFFFFFFF` 也是计数器回绕前的**合法值**，且基础件明确要求"生产 epoch 不得跨越序号回绕"（`tdma_event_history` 精确门面以 `BAD_STATE` 拒绝跨回绕的查询）。该冲突被 `tests/python/test_tdma_event_exact.py::test_exact_terminal_boundaries` 抓住（`accept_event(0,UINT32_MAX); retain(0)` 后要求 RETIRED，实际变成仍存活 ⇒ BAD_STATE）。
- **修复**：区分"回绕"与"哨兵"——只有当 `0x0/0xFFFFFFFF` **不是本 epoch 的正常 +1 步**时才算无见证（`wrap_step`/`ones_step` 判定）；真实回绕（`last == UINT32_MAX && seq == 0`）与重复、后退一并保持既有**可恢复拒绝**，并新增 `sequence_wrap_count` 计数。既保住基础件的回绕契约，又让负载下的空闲/间隙字不再退休观测者。
- **实机（刷写后 T2 轮 `out/tdma_t2/steps-20261015-010842.log`）**：三从 `state 1 (ACTIVE)`、`reason 0`、`joined` 72.05/73.39/74.85 千；证据 `forward 2/1/0 step_max 38 zero 64/28/0 ones 2/1/0`、**`wrap 0 repeat 0 backwards 0`** ⇒ 负载下仍只产生哨兵与偶发跳号，观测者存活；step 17 仍因下一层闸门（`-047` 的身份口径）失败。
- **测试修正**：`test_vdc_priority_tx_scpi.py` 的页字段计数改为 29，并断言新增的 `provider_calls/provider_offcore`；同时把 TX 页的两个新字段改到**mailbox hex 之后**输出，使 `event_time_lower` 等既有位置（24/25）不再移动——发布页的既有索引保持不变。
- **下一 gate**：仍为身份口径统一（`-047` 的 R1/R2 择一），随后 T2 step 17 复验。

### VDC-PROGRESS-20261015-047：见证失配的代码落点——两个独立采样器（线序 FIFO vs 每帧校验的 RX 车道）

- TODO task ID：`VDC-ISO-001`（T3 剩余闸门的代码落点）、`TDMA-ISO-002`。值为 2026-10-15 读码+实测快照，非产品事实源。
- **两个采样器（同一 phys TU，同一根线，负载下不再一致）**：
  1. **事件观测者**（`tdma_pio_spi_phys_event.inc`）：序列 SM（`TDMA_EVENT_SEQUENCE_SM = BOARD_TDMA_TX_DATA_CAPTURE_SM`）在 **rx CS/SCK** 上采样，采样点 `bits = (byte_shift + PACKET_HEADER_SIZE + TRANSPORT_FRAME_SEQUENCE_OFFSET + 4) * 8 + bit_shift`（`:1671-1672`），其中 shift 取自 `phys->flight_alignment_byte_shift/bit_shift`（无显式 tap 时）；该词进入**独立 FIFO**，由 `tdma_event_observer_feed()` 每成一条记录弹出一个（`pending[2]`）。实测负载下该词 ≈ **46.7 万**。
  2. **优先级 RX 车道**（`tdma_pio_spi_phys_priority.inc`，IRQ 内）：按 **CS 边界**算 candidate 并做**逐帧校验**（外层 magic + `packet_bytes`，`:193-195`），再按 `header[8..11]` 取传输帧序号（`:196-197`），发布给 VDC 的 `carrier_sequence`；其 shift 在首个边界由同一个 `flight_alignment_*` 复制（`:354-357`）。实测负载下该序号 ≈ **7.17 万**，与 `source_event`/`up_tx`/`joined` 同口径。
- **机制**：负载在位时导线不再"一次传输＝一个事件"（一等席的帧与事件帧共享总线）。观测者的序列 SM 采样**每一次传输**，而其 rx/tx 配对只覆盖**事件帧**；两路 FIFO 独立，于是弹给记录的那个词不再属于该记录所属的那一帧，观测者见证被抬高到约 6.5 倍。DPLL60（1:1）时两者重合，故历史上可用。
- **候选修复（择一，均属基础件身份口径）**：
  - **R1（推荐，观测侧）**：让事件观测者**不再用自采词作身份**——成记录时以**优先级 RX 车道已验证的传输帧序号**（同 TU 内可取：`s_tdma_priority_*` 或 `tdma_priority_rx_*`）盖章，自采词降级为证据（`wire_witness`）；历史与匹配器均不改，负载与空载下都精确。
  - **R2（参考侧）**：让 typed payload 的 `event_sequence` 携带**承载帧的传输序号**（而非 VDC 源事件序号），使匹配器的查找键与观测者见证同源。
  - 二者都需契约登记与按 C11 的交叉评审；R1 不动 wire/codec 语义、只动内部身份来源，风险更低。
- **当前 goal 状态**：目标前半（观测者只对非前进/回绕判失效、前进跳号作有界证据、ordinal 把关）已修复、实机验证并提交（`c286228f`）；"最终跑通 T2" 仍卡在上述身份口径统一，目标保持 active。

### VDC-PROGRESS-20261015-046：T3 基础件修复已提交（P3 凭证）；负载开/关对照锁定"见证↔帧身份"失配

- TODO task ID：`VDC-ISO-001`（T3 修复闭环 + 下一层根因）、`TDMA-ISO-002`（轮换不削减特等 cadence 的反证）。值为 2026-10-15 实验快照，非产品事实源。
- **已提交（`c286228f`，全项 P3 PASS、凭证 build `20261014162759`）**：观测者线序语义按用户口径收窄——前进跳号（`sequence_forward_count`/`step_max`）与 `0x00000000`/`0xFFFFFFFF` 哨兵字（`sequence_zero_count`/`ones_count`）只作有界证据，重复/后退仍为可恢复拒绝，**ordinal 检查保持致命**；证据经有界只读访问器暴露（不改已发布快照布局）；`EVENt:SNAPshot?` 追加 9 个证据字段；`LOCK:READiness?` 追加 typed 车道身份对；T2 工具新增 `typed_lane`/`ring_seq`/`event_live`/`live_evidence`/速率差值读数；单元测试按新语义改写。实机：三从 `state 1 (ACTIVE)`、`reason 0`、`joined` 72.1–74.9 千（修复前 88–108 即 INVALID）。
- **负载开/关对照（同固件、同一批读数）**：
  - load **off**（DPLL60 采集 `out/HardwareAcceptance/20261015/hawf-002747/…/capture/actions.jsonl`）：三从 `joined 62679 / last 62680 / source_event 62677 / carrier 62680`——**四者一致**，`matched 9148/8932/9321` ✓。
  - load **on**（T2 `out/tdma_t2/steps-20261015-001650.log`）：三从 `joined ≈72.1–74.9 千 / last ≈46.7 万 / source_event ≈7.17 万 / carrier ≈7.17 万`——**见证与帧/事件编号相差约 6.5 倍**，`matched 0`、`history_result 4 (EVICTED)`。
  - ⇒ 负载在位时导线上"一次传输 ≠ 一个事件"（一等席共享导线），而观测者见证取自逐传输计数的字段；匹配器却用源事件序号去查按见证索引、深度 16 的事件历史 ⇒ 恒 EVICTED。**这是 T2 跑通的剩余闸门**（编号口径统一），与观测者语义无关——后者的用户口径修复已闭环。
  - 反证 `TDMA-ISO-002`：负载下 typed 车道的 `carrier`/`source_event` 仍与 `joined` 1:1（7.17 万 vs 7.21 万），**特等 cadence 未被轮换削减**；被抬高的只是逐传输见证，不是事件节拍。
- **下一 gate**：统一事件身份口径——让事件历史的索引等于"承载该事件那一帧"的序号（从 RX/transport 侧取得，或让 typed payload 携带帧序号），使负载在位时匹配器仍能精确关联；随后进入刷写→T2 验证循环直至 step 17 转绿。

### VDC-PROGRESS-20261015-045：T2 失配根因指名——历史按"线序见证"索引，而 typed 车道身份是"源事件序号"（负载下相差约 6.5 倍）

- TODO task ID：`VDC-ISO-001`（T3 第二层根因）、`TDMA-ISO-002`（轮换共线）。值为 2026-10-15 实验快照，非产品事实源。
- **新增判别读数（工作区，随下一次 P3 提交）**：`LOCK:READiness?` 末尾追加 typed 车道身份对——`carrier_sequence`（承载该记录那一帧的线序）、payload 的 `source_event`（源事件序号）、`binding_generation` 与 `carriers/unique/dup` 计数；T2 工具打印 `typed_lane …`。
- **实测（刷写后 T2 轮 `out/tdma_t2/steps-20261015-000617.log`，三从同形）**：失配从板 NO.2
  - `typed_lane carrier 72069 / source_event 72064`（两者一致，都是**事件口径**的小计数）；
  - `event_live joined 72302 … last 0x72416 (=467990) wire forward 2 zero 70 ones 1`（观测者见证是**每传输口径**的大计数，起始约 39.6 万，约为事件数的 6.5 倍）；
  - 匹配器 `MATCH:STATus? -> calls 116568 / matched 0 / superseded 78436 / history_result 4 (EVICTED)`。
  ⇒ 匹配器拿 payload 的 `source_event`（≈7.2 万）去查一张**按线序见证（≈46.8 万）索引**、深度仅 `TDMA_EVENT_HISTORY_CAPACITY = 16` 的历史 ⇒ `age = newest - source` 永远远超 16 ⇒ 恒 `EVICTED`，永不匹配、永不锁相。DPLL60（负载不在位）之所以能匹配，是因为那时导线上"一次传输＝一个事件"，两套编号重合。
- **结论**：基础件的**事件历史必须按 typed 车道使用的事件身份索引**（源事件序号 / 承载帧的事件编号），而不是按"每传输"的线序见证；或让观测者直接采集事件身份字段。线序见证仍按用户口径只作有界证据（前进跳号/哨兵计数，不判失效）。这是 T2 跑通的下一道闸门。
- **本轮已完成目标的一半**：观测者语义＝"只对非前进/回绕判失效，前进跳号与哨兵字作有界证据"（已实机验证：三从 `state 1 (ACTIVE)`、`reason 0`、`joined` 7.2–7.5 万持续增长）。
- **流程口径**：功能未全通过前不跑 P3；迭代期只刷写（`--steps 0-7`）。故本轮代码仍在工作区，文档单独提交。

### VDC-PROGRESS-20261014-044：T3 基础件侧已修——负载不再让从板事件观测失效（实机验证），失败面下移到匹配器历史查找

- TODO task ID：`VDC-ISO-001`（T3 基础件修复 + 实机验证）、`TDMA-ISO-002`（轮换口径保留）、`TDMA-ISO-001`。值为 2026-10-14 实验快照，非产品事实源。
- **修复（工作区已实现，待功能全通过后随 P3 凭证提交）**：`tdma_event_observer.c` 的线序判据不再把"前进跳号"和"哨兵字"当失效——
  - 前进步（`step > 1`）＝**有界证据**（`sequence_forward_count/step_max`），不再 `invalidate`：线序是**整条线上所有传输**的见证，而 TDMA-SEAT-01 的 1:1 轮换（用户裁定保留）本来就让一等席与特等席共享这条线；事件流自身的连续性仍由 ordinal 检查严格保证（保持致命）。
  - `0x00000000` 与 `0xFFFFFFFF` 两个哨兵字＝**无见证**（`sequence_zero_count/ones_count`）：记录照常成，上一见证保持不变，不推进 `last_sequence`。
  - 重复（standstill）与后退仍是既有**可恢复**拒绝（`TDMA_EVENT_SEQUENCE`），回绕分支因哨兵已归类而失效删除。
  - 证据经有界只读访问器暴露（**不改任何已发布快照布局**，避免影响 P3 的 RAM 解码），`SYSTem:TDMA:EVENt:SNAPshot?` 末尾追加 7 个字段；T2 工具打印 `wire forward/step_max/repeat/wrap/backwards/zero/ones/refused/last`。
- **实机验证（先刷写、不做验收；T2 轮 `out/tdma_t2/steps-20261014-234552.log`）**：三从 **`state 1 (ACTIVE)`、`reason 0`**、`joined` 71.8k/73.0k/74.2k（持续增长，此前只有 88–108 就失效）、`live_evidence flags 0x07 valid 1`；同轮证据显示机制确如设计：`forward 2/1/0`、`step_max 40`、`zero 69/38/0`、`ones 2/1/0` ⇒ **负载共享导线只产生哨兵与偶发跳号，不再退休基础件观测**。NO.4 仍进 LOCKED。
- **失败面下移**：step 17 仍 FAIL，但原因换了层。从板 `MATCH:STATus?` 为 `calls 116568 / matched 0 / superseded 78436 / rejected 116568 / history_result 4 (EVICTED)`，即匹配器的**精确历史查找**在负载下返回"已被逐出"——typed 记录到达时，其事件引用已不在从板事件历史里；NO.1（参考）仍 `locked 0`。⇒ 下一层 gate：事件历史深度/写入节拍与匹配器服务节拍在负载下的关系（`tdma_event_history` 深度与 `tdma_event_observer_join` 速率），以及 NO.1 的锁声明（与 `VDC-HOLD-001` 合并看）。
- **流程口径（用户 2026-10-14）**：功能未验证通过前不跑 P3；迭代期只刷写（`hardware_acceptance_workflow.py --steps 0-7`），P3 全项留到功能成立后跑一次取凭证再提交。

### VDC-PROGRESS-20261014-043：用户口径（1:1 轮换是 T2 能发布的前提）＋从板失配根因落到代码行（事件观测 SEQUENCE 失效）

- TODO task ID：`TDMA-ISO-002`（用户裁定）、`VDC-ISO-001`/`T3`（根因落点）、`TDMA-ISO-001`。值为 2026-10-14 实验快照，非产品事实源。
- **用户裁定（2026-10-14）**：特等席与一等席按发布机会 **1:1 轮换保留**——该轮换正是 T2 能发布的前提（特等几乎总有新内容，若特等不让座，一等拿不到任何机会）。据此 `TDMA-ISO-002` 的退出门禁改为**实测等价证明**（负载开/关下 typed SYNC 每圈上车率与最长间隔一致），并删除与之冲突的"规则改为特等有新内容时不让座"候选出口；`ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` 一等行改正过期表述（轮换比例不是"编译期常量 `TDMA_SEAT_ROTATION_*_SHARE`"，而是 STOP-only 每板设置 `SYSTem:TDMA:SEAT:ROTation`，默认 1:1）。
- **从板失配根因落到代码行**：新增 T2 侧活体读 `SYSTem:TDMA:EVENt:SNAPshot?`（无停机门禁），实机（`out/tdma_t2/steps-20261014-220218.log`）读得失配从板 NO.2：**`state 2 (INVALID)`、`reason 9 (SEQUENCE)`、`fault_bits 0x00`、`joined/published 91`、`service_count 542`、`gap_max_us 2712`、`max_us 351`**；NO.3 该页全零（观测者未 arm/未 join）；同轮 `live_evidence flags 0x15/0x3d`（`INVALID` 置位、`ACTIVE` 被清）。
  - 代码落点：`components/tdma/src/tdma_event_observer.c:282-286`——观测者要求**线序号严格 +1**：
    `if (joined != 0u && (last_sequence == UINT32_MAX || sequence != last_sequence + 1u)) return invalidate(observer, TDMA_EVENT_SEQUENCE);`
    即参考侧帧序不连续（跳号或重复）即判定事件观测失效。失效经 `tdma_event_live_retire(TDMA_EVENT_LIVE_INVALID)`（`tdma_pio_spi_phys_event.inc:2114-2123`）清掉 live `ACTIVE`，于是从板匹配器 `priority_match_live_valid()` 恒假、以 `SOURCE` 拒绝、永不锁相。
  - 与负载的因果：负载在位时参考侧发布由 origin 快路径按发布机会轮流承担（priority 与 first-class 交替，`seat_opportunity == priority + first_class`），被观测的帧序因此不再逐拍连续——**负载改变了基础件的观测输入**，正是挂载约束 §8 第 3 条禁止的形态。
- **下一 gate**：同固件下做**负载开/关对照**（同一组两页：`SYSTem:TDMA:SEAT:COUNters?` 与 `SYSTem:TDMA:EVENt:SNAPshot?`，DPLL60 vs T2），确认帧序连续性差异；随后按"基础件不受负载束缚"修复（观测者应只按自身事件帧类判序，或参考侧保持事件帧序连续）。本轮另有一次更差的台架轮（四板均未锁、step 10 亦失败），已记录为台架状态劣化，不作为结论依据。

### VDC-PROGRESS-20261014-042：T3 根因指名——从板事件观测在 T2 下被判 INVALID 并清 ACTIVE（发布侧健康）

- TODO task ID：`VDC-ISO-001`（根因指名）、`TDMA-ISO-003`、`TDMA-ISO-001`。值为 2026-10-14 实验快照，非产品事实源。
- **判别证据（提交 `2f235aaf`）**：`vdc_priority_lock_word_t` 末尾追加 `live_flags/live_valid/live_arm_epoch_lo/live_observer_epoch/live_tick_hz/live_bracket_ok/live_samples`，由匹配器在判定 `priority_match_live_valid()` **之前**逐拍记录并随锁字发布；`LOCK:READINESS?` 末尾输出同字段（该页无停机门禁，可运行中读），T2 工具 step 17 打印 `live_evidence`。
- **实机（全项 P3 PASS、凭证 build `20261014133640`；T2 轮 `out/tdma_t2/steps-20261014-214631.log`）**：
  - 正常从板（NO.4，`LOCKED`）：`live_evidence flags 0x07`（`RETAINED|ACTIVE|ANCHOR_VALID`）`valid 1`，`arm_epoch_lo 18 observer_epoch 4 tick_hz 250000000 bracket_ok 1`。
  - 失配从板（NO.2/NO.3，`reason 8`）：**`flags 0x15`＝`RETAINED|ANCHOR_VALID|INVALID`、`valid 0`**（`samples` 约 2.4–2.5 万；`bracket_ok 1`、锚点字段均正常）。
  - 参考板（NO.1）：`flags 0x00 samples 0`（不跑从板匹配器，符合预期）；同轮 `seat_live` 显示参考 priority 席持续上车（`opportunities == priority == 37189`，`starved 0/0`、`fc_deferred 0`）。
- **结论**：T2 下基础件的**发布侧健康**；缺口是**从板事件观测的 live 状态被置 `TDMA_EVENT_LIVE_INVALID` 且清掉 `ACTIVE`**，于是 `priority_match_live_valid()` 恒假 → 匹配器以 `SOURCE` 拒绝 → 从板永不锁相。这与 `-032` 首轮"NO.2/NO.3 匹配为零、首拒 SOURCE"完全一致，也解释了为什么 M2（采集相位不再被隔离）修好后仍未成匹配：采集相位在跑，但 live 状态被判无效。
- **下一 gate**：定位 `components/tdma` 事件观测在负载下置 `INVALID`/清 `ACTIVE` 的条件（时钟丢失、FIFO/DMA 异常、观察者世代失配等），同固件下做 T2 与 DPLL60 对照，再按"基础件不受负载束缚"修复。

### VDC-PROGRESS-20261014-041：更正 —— 参考侧发布在 T2 下是健康的；T3 的真实缺口在从板观测侧

- TODO task ID：`VDC-ISO-001`（T3 结论更正）、`TDMA-ISO-001/003`。值为 2026-10-14 实验快照，非产品事实源。
- **更正 `-040`**：该条据 `PRIORity:TX?` 的 `provider_calls = 0` 推断"T2 下参考侧 provider 从未被进入"。本轮改用**无停机门禁的** `SYSTem:TDMA:SEAT:COUNters?`（TDMA-SEAT-01 计数，可在环运行时读）并在**同一轮 T2 采样窗内取两次读数做差**（累积计数不能单点判读），结果（`out/tdma_t2/steps-20261014-205817.log`）：
  - 参考板 slot 0 在约 8 s 采样窗内：`seat_opportunity_count +131839`、**`seat_priority_count +118564`**、`seat_first_class_count +13275`。
  - ⇒ **参考的 priority 席（VDC typed SYNC）在 T2 下持续上车**，负载的一等席同时也在上车；`published_generation`/`seat_*` 均为累积计数，单点读会被"整轮未被服务"的假象误导。
  - `PRIORity:TX?` 的 `provider_calls = 0` 属**停机后读数的假象**（TX work 快照在停机读数之前被清零；该页本身要求 STOP，无法在运行中读）。⇒ `-040` 的结论作废，TX 页不能用于"运行中是否被服务"的判断；应改用 SEAT 计数或 VDC 侧无门禁页。
- **T3 的真实缺口（据此更正）**：发布侧健康，缺的是**从板观测侧**——从板 `PRIORity:RX?` 的 `typed_accept_count` 大量增长且 `typed_reject_count` 为 0（记录到达并被接受），但匹配器以 `SOURCE` 拒绝，即 `priority_match_live_valid()` 的 `TDMA_EVENT_LIVE_RETAINED|ACTIVE|ANCHOR_VALID` 事件 live 快照在 T2 下不完整（`vdc_priority_match.inc:275-277`）。这与 `-032` 首轮证据（首拒 `SOURCE`）一致，也与 M2 修复后 SYNC_CAPTURE 已恢复运行并不矛盾：采集相位在跑，但 live 快照的**标志/锚点**仍不满足。
- **下一 gate**：把事件 live 快照的三个标志与锚点条件逐项打点（同 SEAT 计数那样可运行中读取），同固件下做 T2 与 DPLL60 对照，指名是哪一个标志/锚点在负载下不成立；随后按基础件不受负载束缚修复。
- **本轮工具改动**：T2 侧新增 `seat_live` 读数与 `rate` 差值（采样窗首末两次），以及 origin 活体读数；这些改动待下一次成功的全项 P3 后再提交（本轮两次 P3 均在物理环节失败，见下）。
- **本轮 P3 记录**：`hawf-210711` step 29 采集失败（`internal_health_passed false` + `external_status FAIL`，属台架/物理窗口问题），其 `--run` 续跑又在 step 8 P0T 拓扑失败（前一次失败采集留下的板状态）⇒ 本轮未取得新凭证，工具改动保持未提交。

### VDC-PROGRESS-20261014-040：活体判别——T2 下 VDC 发布 provider 一次都没被进入（provider_calls = 0）

- TODO task ID：`VDC-ISO-001`（T3 判别工具与结论）、`TDMA-ISO-001`、`TDMA-ISO-003`。值为 2026-10-14 实验快照，非产品事实源。
- **新增判别工具（提交 `ecc1e4f0`）**：`vdc_priority_tx_snapshot_t` 末尾追加 `provider_calls`/`provider_offcore`，在 `vdc_priority_tx_core1()` 的**所有 guard 之前**计数（`calls` 无法区分"快路径从未问过"与"被问到但不在 Core1、只能答 EMPTY"）；`SYSTem:VDC:PRIORity:TX?` 末尾追加两字段。T2 工具侧新增 **origin 活体读回**（step 17 诊断内、环仍在运行时读 `READ:CALibration:ORIGin:RUNTIme?`）——停止处理器里的同页读数在 origin 刚退休后必然 active 0，无法判别分支。
- **实机结论（全项 P3 PASS、凭证 build `20261014124006`；T2 轮 `out/tdma_t2/steps-20261014-204919.log`）**：主机（参考板）活体 `origin_live active 1 comm_fsm 5/0 observed 16621 unobserved 52894 published_gen 34103/0 matched_gen 0`，而 `PRIORity:TX?` 的 **`provider_calls = 0`、`provider_offcore = 0`**。⇒ 参考的 origin 快路径确实在跑（active 1、逐周期计数增长），但 **VDC 的 priority provider 一次都没被进入**——既不是"从未询问"，也不是"在 Core1 之外被询问"。结合 `published_owner_generation = 0`（只有 `tdma_pio_spi_ring_origin_board()` 的零 tx 视图提交会留下这个形状），T2 期间的发布走的是 overlay/async 或 FIFO 分支，typed SYNC 没有任何发布机会，从板因此无记录可匹配。
- **下一 gate**：在 `tdma_pio_spi_ring_origin_publish()` 内加分支计数（provider 指针是否为空 / async 分支 / FIFO 分支 / publish 是否被调用），把这一层钉死后再谈修复（让参考的 priority 发布机会不依赖该分支选择）。

### VDC-PROGRESS-20261014-039：T3 定位到参考侧"origin 快路径未接管"——priority provider 只在 origin.active 时才被调用

- TODO task ID：`VDC-ISO-001`（T3 根因定位）、`TDMA-ISO-001`、`TDMA-ISO-003`。数值与代码位置均为 2026-10-14 快照，非产品事实源。
- **代码路径（读码，未改源码）**：typed SYNC 的发布机会来自 TDMA 快路径——`tdma_pio_spi_ring_origin_service()`（`components/tdma/src/tdma_pio_spi_ring_origin.inc:604-627`）依次做 RX → observe → `tdma_pio_spi_ring_origin_publish()`，而 `publish()` 第一件事就是调用 `adapter->priority_tx_provider(&config, mailbox)`（同文件 `:548-554`）。provider 由 `vdc_dpll_manager_init()` 一次性注册为 `vdc_priority_tx_core1`（`components/vdc_dpll_manager/src/vdc_dpll_manager.c:1871`），注册走停机态门禁（`tdma_runtime_owner.c:574-586`），成立后不会被清除。⇒ provider 为 NULL 可以排除。
- **关键分叉**：`tdma_pio_spi_ring_adapter_service_impl()`（`components/tdma/src/tdma_pio_spi_ring_adapter.c`）只在这一个条件下调用 origin 快路径：
  `if (adapter->origin.active != 0u) { ... tx_ok = tdma_pio_spi_ring_origin_service(adapter, &rx_ok); }`
  `else if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE) { ...tdma_pio_spi_ring_adapter_resident_process_image(adapter)... }`
  即参考板在 `origin.active == 0` 时走 **resident process-image** 分支，**根本不调用 priority provider** —— 与实测 `SYSTem:VDC:PRIORity:TX?` 的 `calls` 整轮冻结完全一致。
- **`origin.active` 的接管条件**（同文件 `:3172-3195`）：`burst_limit == 0 ∧ resident ∧ resident_seeded ∧ resident_return_ready ∧ comm_fsm.state == CYCLE_BOUNDARY ∧ !reference_tx_completion_pending ∧ !pending_tx_evidence ∧ phys_origin.admit != NULL ∧ origin_seed_valid(adapter)`；满足后由 `phys_origin.admit()` 起步，`start_origin_impl()` 成功才置 `origin.active = 1`（`tdma_pio_spi_ring_origin.inc:183`）。
- **负载侧差异（本轮实测）**：T2 与 DPLL60 采集的环启动序列几乎相同（都是 `FLIGHT:MODE? = 2`、`RING:BURSt 0`、`RING:ARM/START`、`ORIGin:TRIAL`；对照 `hawf-201432` 采集 actions 与本轮 T2 日志），差别在 T2 还挂载了 `TRIGger:REServation:T2`（INSTall/ACTivate/CONFigure）与 `FLIGHT:FIFO:RESet`、`OPMode:STAGe 7` 等。⇒ 下一步要做的是**把这组接管条件逐项打点**（resident_seeded / resident_return_ready / comm_fsm / reference_tx_completion_pending / pending_tx_evidence / origin_seed_valid），在**同一固件**下跑一轮 T2 与一轮 DPLL60 采集做对照，确认是哪一项被负载改掉；随后按"基础件不受负载束缚"修复（让参考的 priority 发布机会不依赖 resident→origin 的接管，或让负载不再改它不拥有的模式）。
- **本轮无源码改动**（纯定位，故无新凭证需求）。

### VDC-PROGRESS-20261014-038：T1 工具侧落地、owner arm 实机成立；T3 收窄到"T2 下 priority provider 整轮未被调用"

- TODO task ID：`VDC-LOCKPUB-001①`（arm 实机成立）、`VDC-ISO-001`（T3 再收窄）、`TDMA-ISO-001`（T1 工具侧关闭）、`TDMA-ISO-003`。数值均为 2026-10-14 实验快照，非产品事实源。
- **T1 工具侧（提交 `7668ff0d`）**：T2 流程不再配置基础件——step 2 删除 `PRIORity:{FOLLow:PHASe,FOLLow,SYNC,MATCH} 0`；step 15 由写入改为**只读回 owner 已 arm 的车道**（世代须等于本次会话）；step 16 删除运行中重下发（本就被 reject 115 拒绝）并**把 `FOLLow?` 的运行中 8 s 超时如实标为"运行中不可读"**（旧代码标成 "n/a (reference)" 且不记失败，即 T4 的工具侧缺陷）。`SESSion` 保留（身份而非车道配置）。
- **M1 arm 实机成立**：主机不再写优先级后，四板由 owner arm 到本会话且无需任何主机写入——step 15/16 读回 `SYNC? = 1791980606`、三从 `MATCH? = 1791980606`，两步骤 **OK**（`out/tdma_t2/steps-20261014-202326.log`）。
- **M1 补强（实测驱动）**：`priority_tx_owner_restart_core1()` 增加"采纳请求世代"分支。上一轮实测显示 NO.1 的 `PRIORity:TX?` 快照停在**上一会话**（generation 1791979527、`calls` 冻结 31209、`retired 1`）：TX 车道只在 provider 自己的世代变更路径里复位，而该路径在本工况下没被调用。补强后同轮 `TX?` 的 generation 已变为本会话、NO.4 `LOCKED` 恢复。
- **T3（VDC-ISO-001）再收窄**：本轮 T2 的失败步仍是 **step 17**，且证据把根因钉到参考侧发布——`SYSTem:VDC:PRIORity:TX?` 显示 **`calls = 0`（整轮 provider 从未被调用）**，即参考的 typed SYNC 在本负载工况下没有发布机会；从板因此无记录可匹配（`MATCH:STATus?` 三从 `calls == rejected ≈ 62k/60k/0.35k`、live 字段 0、首拒 `SOURCE`、`last_reason 18`）。⇒ 下一步在 TDMA/负载路径里定位"T2 挂载后一等席/overlay 路径为何不再调用 priority provider"（`tdma_pio_spi_ring_origin_publish()` 的 provider 分支与 T2 席位路径的关系），这正是 MOUNTING 约束禁止的"负载改变基础件行为"。
- **验证**：全项 P3 PASS 并写凭证（build `20261014121441`，`PASS_WITH_WARNINGS`、INFO 33/WARN 11/ERROR 0/FATAL 0、step 29 联合采集 PASS）。

### VDC-PROGRESS-20261014-037：M1 落地——观测车道 arm 归 Core1 owner，主机驱动的会话一律让位（P3 全绿）

- TODO task ID：`VDC-LOCKPUB-001①`（实现落地，效果待 T1 工具侧落地后实测）、`VDC-ISO-001`（M1 关闭实现面）、`TDMA-ISO-001/003`。数值均为 2026-10-14 实验快照，非产品事实源。
- **改动（代码，提交 `672edf0b`）**：
  1. Core1 owner 自管 arm/重 arm：`sync_dpll_fb_service()` 每拍调用 owner 侧 `vdc_priority_owner_arm_core1()`；FOLLOWER 用 `MATCH`/`FOLLow`/`PHASe`，MASTER 用 `SYNC`。arm 只在无人 arm 时进行（世代＝会话，绝不另造身份）；**retire 后原地重启**（保持世代，避免破坏 `binding_generation == request`）；follow 车道仅在请求仍是 `TYPED_FOLLOW` 时重启，主机 `FOLLow 0`/`SYNC 0` 的显式解除不会被推翻。
  2. 主机兼容闸门：新增 `components/vdc_dpll_manager/src/vdc_priority_host_control.inc`；四个主机侧写入者（`priority_match_set_stopped`/`priority_tx_set_stopped`/`priority_phase_set_stopped`/`priority_follow_set_stopped`，均在 TDMA 停机态门禁之后）在 arming 写入（非零世代、`TYPED_FOLLOW`、启用相位）时置位；owner 据此把该会话判定为主机驱动并**完全不碰**（不覆盖世代、不复活被解除的车道、不复位主机正在测量的 matcher），并在会话开放后给主机 **32 拍**先手再自行 arm。零值写入是解除、不是控制，不置位。
- **实机验证（全项 P3，run `hawf-194312`，build `20261014114319`）**：`PASS_WITH_WARNINGS`、INFO 33/WARN 11/**ERROR 0/FATAL 0**，凭证写入；step 29 的 DPLL 60 s 联合采集（HDO 示波器、内部 health 与外部稳态判据）**PASS**，step 30-36 全绿。⇒ **主机驱动的采集会话行为与改动前一致**（对照：改动过程中 `hawf-190706` 因主机 SYNC 写 ACK 被 owner 抢先占位而 `<timeout>`、`hawf-191605` 因 `FOLLow?` 解除读回被 owner 重新 arm 而 mismatch，两处均已按上一条修正）。同轮主机 harness **728 项通过**（match/tx/phase/follow/trace/command-owner）。
- **仍未闭合的效果面**：owner arm 的**收益**要等 T2 不再自行配置基础件（T1 工具侧：移除 step 2 的优先级清零、step 15 的 `SYNC/MATCH/FOLLow/PHASe`、step 16 的运行中重下发与 `OBServer:TDMA`）之后才能在 T2 轮里被测到——那之前 T2 仍算"主机驱动的会话"，owner 依约让位。
- **下一 gate**：落地 T1 工具侧（T2 不配置基础件）并复跑 T2，验收"三从成匹配并按角色声明"；随后按序进入 V2（`VDC-HOLD-001`）。

### VDC-PROGRESS-20261014-036：M2 已修并验证——基础件相位不再被隔离；T2 step 17 仍缺 M1

- TODO task ID：`VDC-ISO-001`（M2 关闭、M1/M3 打开）、`TDMA-ISO-003`、`VDC-LOCKPUB-001①`。数值均为 2026-10-14 实验快照，非产品事实源。
- **改动（代码）**：隔离只对非基础件负载生效（`work->load_bit & ~APP_REALTIME_LOAD_FOUNDATION_MASK`，`application/src/app.c`）——基础件相位（VDC/DPLL/SYNC_CAPTURE/REFMEM/SYNC_TRIGGER）的超预算仍计入 `phase_overrun_count`/`deadline_miss`/`schedule_miss` 证据，但不再关停基础件的输入与发布通路；`app_realtime_set_load_mask()` 在任何掩码写入时一并释放遗留的基础件隔离，使已落进该状态的固件也能恢复。host 用例同步改写并纳入 `app_realtime_set_load_mask` 切片（`tests/python/test_app_realtime_priority.py`）。
- **实证（P3 凭证 `hawf-184456`，build `20261014104506`，`PASS_WITH_WARNINGS`、INFO 33/WARN 11/ERROR 0/FATAL 0；T2 轮 `out/tdma_t2/steps-20261014-185408.log`）**：
  - 四板 `quarantined_mask` 由恒定的 **8 → 0**（`quarantine 0` 见于 step 18 preflight 与 step 20 采样）。
  - SYNC_CAPTURE（phase 4）`run` 由 `-034` 时的 **14776 / 1486 / 8025 / 13211 升到 204591 / 189275 / 188587 / 181320**，即事件采集通路恢复；其超预算仍如实记账（`overrun` 4550 / 5734 / 5397 / 5496，`schedule_miss` 仍增长）。
  - 参考侧恢复健康：游标 `err 0 valid 1`、`seq` 递增、`bound token 243 vs model 243`，**step 18/19/20 全部通过**，本轮失败步回到单一 **[17]**（对照 `-035` 实验轮的 [17,18,19]）。
- **仍未通过的部分（指向 M1）**：T2 step 17 仍是 NO.1/NO.2/NO.3 未锁、NO.4 `LOCKED`。`PRIORity:MATCH:STATus?`：NO.2/NO.3 仍为 `have_match 0`、`calls == rejected ≈ 39.6k`、`last_reason 18 (MATCH_STOP)`、live 字段全 0、`first_reject_reason 9 (SOURCE)`；NO.4 `matched 17893`。⇒ 采集通路恢复并未让两块从板开始观测：观测车道的 **arm/重 arm 仍完全由主机在停机态写入**（M1），环 enable 后写入被 reject 115、retire 后不重 arm。
- **下一 gate**：实施 M1（`VDC-LOCKPUB-001①`：Core1 owner 按 session/role 自 arm，STOP retire 后自 reset 重绑，不改世代值以免破坏 `binding_generation == request` 校验；主机显式写入保持兼容），随后同轮 T2 验收"三从成匹配并各按角色声明"；M3（`LOAD:MASK` 经 0 后参考绑定不重建）单列。

### VDC-PROGRESS-20261014-035：T3 又两条基础件缺陷——SYNC_CAPTURE 声明预算不足导致自隔离；LOAD:MASK 0 转变破坏参考绑定

- TODO task ID：`VDC-ISO-001`、`TDMA-ISO-003`、`VDC-LOCKPUB-001①`。数值均为 2026-10-14 实验快照，非产品事实源。
- **背景（`-034` 之后的实验）**：怀疑"基础件观测相位被隔离"是 T3 成因。读码确认隔离语义：任一 optional load 只要 `overrun || own_deadline_missed` 就被置入 `s_realtime_load_quarantined_mask`（`application/src/app.c:1095-1099`，DPLL 反馈负载例外），而**只有该位出现"禁用→启用"转变时才解除**（`app.c:594-610`：`newly_enabled = enabled_mask & ~previous_mask`）。实机四板 `quarantined_mask` 恒为 **8 = SYNC_CAPTURE（基础件掩码 91 的一位）**，即基础件自己的事件采集相位被关掉，且 91→91 不是转变、永远不会自行恢复。对应现象：从板匹配器首拒 `SOURCE`（事件 live 快照 `RETAINED|ACTIVE|ANCHOR_VALID` 不全，`vdc_priority_match.inc:275-277`）→ `FOLLOW_DISABLED`。
- **实验一：显式解除隔离（`--release-quarantine`，先写 `LOAD:MASK 0` 再写 91）——未解除**。四板读回均为 `quarantined 8 -> 8`；同一轮 phase4(SNC_CAPTURE) `overrun` 计数为 2、`max_rt` 11381/12091/14589/13731 而声明 `wcet` **9000**（`config/project_config.h:106`：`4000 + PRIORITY_RX_PHASE_CYCLES`）⇒ **声明预算低于实测最坏值**，所以一旦解除就立刻再次超预算并立刻重新被隔离。即：该基础件相位的隔离不是偶发，而是"声明值不足"的必然结果（与 `-008` 记录的 DPLL 相位同类）。
- **实验二（附带发现）：`LOAD:MASK` 的 0 转变会破坏参考绑定**。本轮自环 START 起游标一直 `err 1`、`bound token ? vs model 695/748/800/844`（模型未绑定），step 17 四板只有 NO.4 `LOCKED`，step 18/19 因游标始终无效失败 ⇒ 本轮失败步为 [17,18,19]。而 18:22/18:33 两轮（step 2 只写 27→91，不经过 0）游标正常。⇒ **把 VDC/REFTEM 等基础相位短暂关掉后，参考的模型/token 绑定不会自行重建**；这是继 `-033` 的"交接污染"之后第二条"基础件状态不可自恢复"的证据。
- **T3 机制清单（本轮定稿，按可验证性排列）**：
  1. **M1 无 owner 侧 arm/自恢复**：观测车道只由主机在停机态写 `PRIORity:SYNC/MATCH/FOLLow`，环 enable 后被 reject 115 拒绝（`-034`），retire 后不自恢复。
  2. **M2 基础件采集相位自隔离 + 声明预算不足**：`quarantined_mask` 恒含 SYNC_CAPTURE；解除即复现超预算并重新隔离。
  3. **M3 加载掩码转变破坏参考绑定**：`LOAD:MASK` 经 0 后参考不再绑定（本轮）。
- **下一 gate**：把 M2 作为基础件隔离缺陷修（基础件掩码内的相位不得被隔离——超预算仍记 `schedule_miss` 证据，但不得关停基础件输入通路；SYNC_CAPTURE 的声明预算按其实测最坏值重新推导），与 M1（`VDC-LOCKPUB-001①` owner 自管 arm/自恢复）同批实施；M3 单列一条实验（在非 0 转变下复现并定位参考绑定重建路径）。

### VDC-PROGRESS-20261014-034：T3 机制落到匹配器 STOP 路径——两块从板只在前环窗口被服务过，负载期间观测车道无自恢复

- TODO task ID：`VDC-ISO-001`、`TDMA-ISO-003`、`VDC-LOCKPUB-001①`、`VDC-LOCKPUB-002`；T2 侧 step 16/17。数值均为 2026-10-14 实验快照，非产品事实源。
- **复现**：同固件（build `20261014093650`）再跑一轮 T2（`out/tdma_t2/steps-20261014-182824.log`，18:28:24–18:31:32），仍只 `failed steps: [17]`：NO.4 `SYNC:STATe LOCKED`，NO.1/NO.2/NO.3 UNLOCK（NO.1 `input_ready 1 reason 0` 但 `locked 0`；NO.2/NO.3 `reason 8`）。⇒ `-033` 的判读得到第二次独立复现。
- **本轮新增证据（环 STOP 后读 `SYSTem:VDC:PRIORity:MATCH:STATus?`，字段序见 `scpi_sync_commands.c:1429-1454`）**：
  | 板 | role | retired | have_match | calls | matched | rejected | last_reason | live 字段 |
  |---|---|---|---|---|---|---|---|---|
  | NO.1 `0010071E65B5CB38` | MASTER | — | — | — | — | — | — | `PRIORity:MATCH?`＝0（主机不设 MATCH，正确） |
  | NO.2 `FB276192BEF9CCE1` | FOLLOWER | 0 | 0 | 39832 | 0 | **39832** | **18（`MATCH_STOP`）** | 全 0（`source_slot`/`event_sequence`/`rx_epoch`/`model_token`…） |
  | NO.3 `2BD5090FE009FA2A` | FOLLOWER | 0 | 0 | 39919 | 0 | **39919** | **18** | 全 0 |
  | NO.4 `A1E549202D18ED6A` | FOLLOWER | 1 | 1 | 25308 | **17459** | 2978 | 18 | `source_slot 0`、`event_sequence 123439`、`rx_epoch 8`、`model_token 2842`、`ring_config_seq 80` |
  三从的传输面一致：`PRIORity:RX?` 的 `typed_accept_count` 122921/122921/121985、`typed_reject_count` 0、`binding_generation` 全为 `1791973704`、`event_sequence` 全为 `123439`。⇒ **只有 NO.4 形成了匹配**，NO.2/NO.3 的匹配服务从头到尾走的是 STOP 分支。
- **机制（本轮推定，依据代码＋计数）**：`vdc_priority_match_core1()` 在自己读到的环快照 `!enabled || !adapter_started` 时走 `priority_match_skip(VDC_PRIORITY_MATCH_STOP, …)`（`components/vdc_dpll_manager/src/vdc_priority_match.inc:247-248`），`rejected` 因此按次累加；NO.2/NO.3 的 `calls` 与 `rejected` 相等且约为 4 万（≈1 kHz × 40 s，正是 step 15 写下 MATCH 到环 START 之间的停机窗），说明它们**只在前环窗口被服务过，环起来之后计数不再增长**，而两块板都未 retired（`retired 0`）。另据工具自身记录：负载会在环运行中下发 `CALibration:ORIGin:TRIAL`，其后 `RING:STATus?` 读到 `en 0 / adapter 0 / config 154→155`（`tools/sync_trigger/t2_workflow.py:2004-2055`），即**参考环被负载瞬时拉停**。基础件侧只有"停机态写 MATCH"这一条 arm 路径，环 enable 后被 reject 115 拒绝（`tdma_service.c:1033-1034`），**没有 owner 侧自恢复**。⇒ 三条事实指向同一结论：观测车道的 arm 归属必须回到 VDC owner。
- **仍未闭合（下一步必须测）**：`calls` 为何在环起来后停止增长——是 owner 侧该板 DPLL 相位被跳过（相位准入，`app.c:928-932`）还是匹配器提前返回。计划在 step 17 的 diag 里补打印**全部 10 个相位的 skip/start_miss/deadline 计数**与 `first_reject_reason`，再据实判读。
- **工具改动（未提交，待与固件一起付 P3）**：`tools/sync_trigger/t2_workflow.py` 的 step 21 收尾读回扩展为 `PRIORity:SYNC?/MATCH?/MATCH:STATus?/FOLLow:STATus?/FOLLow:HEALth?/FEEDback:SESSion?`（本次证据即由它给出）；`FOLLow:STATus?` 在活跃车道 8 s 超时仍被标 "n/a" 且不记失败的缺陷尚未修。
- **下一 gate**：按 `VDC-LOCKPUB-001①` 实施 owner 自管 arm 与 STOP 后自恢复（含按 active profile 在停机边界 arm），同时移除 T2 流程对基础件的 `PRIORity:*`/`OBServer:TDMA` 下发（T1/T4），再以同一 T2 轮验收"三从成匹配并各自按角色声明"。

### VDC-PROGRESS-20261014-033：T3 收窄——T2 下失锁点在"接受之后、成匹配之前"；17:43 那轮无发布是跨流程交接假象

- TODO task ID：`VDC-ISO-001`（新证据）、`VDC-HOLD-001`（V2 再证）、`VDC-LOCKPUB-001`、`TDMA-ISO-001`、`TDMA-ISO-002`；T2 侧 step 17。数值均为 2026-10-14 实验快照，非产品事实源。
- **假象排除（重要）**：`-032` 记录的 17:43 T2 轮（`out/tdma_t2/steps-20261014-174358.log`）自环 START 起每一拍都是 `err 7 valid 0 seq 0 bound ring_cfg 0 vs ring 14/14 bound token 0 vs model 43`、`sync_seq 0`、`count 0`，看起来像"基础件不再发布"。但该轮在 DPLL60（17:40:42–17:43:57）结束后 **1 s** 启动。同一固件（build `20261014093650`）18:22:52 单独重跑（`out/tdma_t2/steps-20261014-182252.log`）：游标 `err 0 valid 1`、`seq` 递增至 8.6 万、`bound ring_cfg 75 vs ring 75/75`、`bound token 243 vs model 243`、`sync_seq` 11 万+。⇒ **该模式是跨流程交接污染，不是 V1 回归，也不是基础件停止发布**；V1 声明归属改写本身未破坏环发布。
- **重跑复现的真实偏离**（step 17 FAIL，`failed steps: [17]`）：四板 `role` 首字段 0/1/1/1 ⇒ NO.1 为 MASTER、其余为 FOLLOWER：
  - NO.1（MASTER）：`readiness input_ready 1 locked 0 reason 0`（READY）、`accepted 194`、`READ:SYNC:STATe?`=UNLOCK。MASTER 分支的 `reason` 仍走历史输入阶梯（不查新鲜度），`locked` 取 owner 声明 ⇒ **阶梯 READY 与声明 0 并存**，成因是样本窗口过期（与 V2、`-026` 一致：NO.1 的 DEGRADED 唯一成因＝freshness 过期）。
  - NO.4（FOLLOWER，slot 3）：`SYNC:STATe LOCKED`、`locked 1` ⇒ 角色感知声明在"确实在跟随"的从板上成立。
  - NO.2/NO.3（FOLLOWER，slot 1/2）：`reason 8`（`FOLLOW_DISABLED`，无观测）。与 `-032` 的 16:55 轮同向：**T2 下只有一块从板成匹配**。
- **本轮排除的两个候选（都不解释 slot 分化）**：
  1. **相位跳过率**：同固件 DPLL60（`out/HardwareAcceptance/20261014/dpll60-174042/`）SYNC_TRIGGER（phase 7）`start_miss/run` 为 3901/42279、5372/40499、5804/40068、5176/40694（9–15%），T2 轮为 114279/579498、111866/581872、112992/581165、117027/573936（19–20%），且 `deadline_miss` 由 1/545/494/606 涨到 9/21754/22394/33307；但四板跳过率一致而成匹配按 slot 分化（只有 slot 3 成）⇒ 相位跳过是基础件既有事实与负载放大，**不是从板间差异的成因**。静态表里 SYNC_TRIGGER 的入口余量只有 **200 cycles**（`config/project_config.h:143-147`：`END = START + WCET + CLOSE + 200`），即该相位只能容忍前序相位迟到 0.8 µs 以内。
  2. **入站（RX）**：STOP 后 `SYSTem:VDC:PRIORity:RX?` 三从 `typed_accept_count` 123857/122947 量级一致、`typed_reject_count 0`，即 typed 记录已被接受 ⇒ 失效点在**接受之后、成匹配之前**，不在运输。
- **代码级确认（T4）**：环运行后一切"停机态写入"经 `tdma_service_update_event_tap()` 以 reject **115** 拒绝（`components/tdma/src/tdma_service.c:1033-1034`），故 step 16 的运行中重下发 `SYNC/MATCH/FOLLow/FOLLow:PHASe` 一律不生效；`FOLLow:STATus?` 在活跃车道读回 `<timeout>`（8 s），而工具把它标成 "n/a (reference)" 且**不记失败**（`tools/sync_trigger/t2_workflow.py:2140-2153`），掩盖了未 armed 的事实——这是工具侧缺陷，须先修再谈门禁。
- **下一 gate**：在 T2 运行窗内取从板匹配侧证据（`SYSTem:VDC:PRIORity:MATCH:STATus?` 与 follow health），定位"接受之后未成匹配"的确切拒绝码；随后按已定式的同一条路——owner 自管 arm（`VDC-LOCKPUB-001①`）+ 基础件隔离缺陷（`VDC-ISO-001`）修复，再谈 T2 门禁（T5）。

### VDC-PROGRESS-20261014-032：从板以内部观测判锁相；锁定声明归 VDC owner；HAOFV 偏离审查

- TODO task ID：`VDC-LOCKPUB-001`、`VDC-LOCKPUB-002`（IN PROGRESS），新增 `VDC-LOCKPUB-003`、`VDC-ISO-001`，`VDC-HOLD-001` 追记；TDMA 侧 `TDMA-ISO-001..003`。本条数值均为 2026-10-14 实验快照，非产品事实源。
- **审查起点**：用户给出的四板状态中从板 reason=9（`FOLLOW_STALE`），而当时工作区源码不可能产生 9 ⇒ 板上不是工作区代码；`PROJECT_BUILD_ID` 只在 configure 时生成，连续多次 OTA 前后 build 号恒等，无法证明新镜像上板。
- **实现（源码）**：
  1. 从板锁定依据改为内部观测：匹配器每次新匹配由 `vdc_priority_lock_observe()` 归约残差区间中心与宽度，带滞回与过期窗口（`components/vdc_dpll_manager/inc/vdc_priority_match.h` 的 `VDC_PRIORITY_LOCK_*`，取值为待裁定默认）；retire/换 request 清零，Core1 每拍自行过期降级。
  2. HAOFV 审查 V1 后：锁定声明归 VDC owner——`vdc_dpll_manager_lock_declaration()` 由 Core1 DPLL 服务每拍末尾提交（仅变化时，同 guard、推进 revision）；RefMem 向量只投影 `vector_snapshot.lock`，恢复按 revision 去重；SCPI/verdict 只投影声明；删除 `vdc_dpll_manager_lock_evidence()`。
  3. 构建号：`cmake_build_auto.py --build-id`，`OTA_workflow.py` 每次构建传入新 id。
- **验证（host/build）**：VDC/RefMem/priority/DPLL/SCPI 相关测试全量通过（锁定声明改写前一轮全量 4455 passed；改写后锁定相关文件 430 passed，全量复跑见下条或本条补记）；ARM 构建与 flash link 检查通过。
- **验证（实机，构建 `20261014084516`，即 V1 改写之前的版本）**：
  - OTA：四板 build 号刷写前同为旧值、刷写后同为新值，标定 gen/CRC 不变（`out/HardwareAcceptance/20261014/ota-164516/`）。
  - DPLL60：`failed steps: none`；示波器三从相对 NO1 稳态约 ±25 ns；STOP 后读 MATCH：三从各约 8.4k 次匹配，末次残差中心 +7/−6/+1 ns、宽度 160 ns，均落入入锁带（`out/HardwareAcceptance/20261014/dpll60-164927/`）。DPLL60 运行窗内禁止查询，故未直接读到运行中四板 locked。
  - T2：step 17 失败——NO.4 `locked=1`；NO.2/NO.3 reason 8，STOP 后 MATCH 匹配为零、首个拒绝 SOURCE；NO.1 阶梯 READY 但样本准入停止、health 降级，故 `locked=0`（`out/tdma_t2/steps-20261014-165543.log`）。
- **验证（实机，V1 版镜像 build `20261014093650`，指纹 `77bb7cd0`）**：
  - OTA（17:36）：四板 `20261014084516 → 20261014093650`，标定 gen/CRC 不变（`out/HardwareAcceptance/20261014/ota-173650/`）。
  - DPLL60：`failed steps: none`；REMFEM 相位四板 `max_runtime` 18478/19498/20125/20305 ≤ 29000、`overrun 0`（对照 `-030` 的 29924–35031 超预算，`out/HardwareAcceptance/20261014/dpll60-174042/`）。运行窗内仍禁止查询，未读到运行中四板 `locked`。
  - P3 硬件验收：`hardware_acceptance_workflow.py --profile full --package out/build/default/DHRT100_UPDATE.pkg --ota-summary .../ota-173650/ota-four-board/summary.json`（复用既有产物与 OTA，不重建、不重刷）→ 凭证写入 `PASS_WITH_WARNINGS`（INFO 33 / WARN 10 / ERROR 0 / FATAL 0），`check-staged` OK（sources=1802、scope `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`），run `out/HardwareAcceptance/20261014/hawf-180805/`。
  - T2 复跑（`out/tdma_t2/steps-20261014-174358.log`）：失败步 **[17, 18, 19]**——四板 `locked` 全 0（NO.1 `input_ready 0 locked 0 (dpll.state 0, reason 5)`、三从 `reason 8` `FOLLOW_DISABLED`）；step 18 参考游标 24 s 未有效（`err 7 valid 0 seq 0`、`segments 6113/0/0`、`sync_seq 0`）⇒ 本轮不发布，step 19 随之失败。与上一条 T2 记录相比 NO.4 也由 1 变 0。
- **HAOFV 偏离审查（顶层 + 挂载约束 + T2 预约 + VDC/DPLL 架构）**：VDC 侧 V1 锁定声明不在 owner（已改）、V2 失步未进 HOLDOVER、V3 观测通道生命周期由负载流程掌握、V4 无固件级全局 LOCK；T2 侧 T1 一等席依赖 VDC SYNC 通道（`tdma_pio_spi_ring_origin.inc`）、T2 轮换可能削减特等 cadence（待测）、T3 T2 负载下基础件退化、T4 step 16 运行中写入被 STOP 门禁拒绝、T5 T2 的 VDC 门禁在主机侧。用户裁定：TDMA 与 DPLL/VDC 是基础件，T2 是负载，T2 不得影响 DPLL/VDC；VDC 按自身最优发布，T2 接收。
- **更正**：此前称"T2 切换从板采用通路"不准确——DPLL60 采集同样下发 `FOLLow`/`FOLLow:PHASe`；偏离实质是 VDC 工作模式由逐次主机脚本选择（V3）。
- **下一 gate**：`VDC-LOCKPUB-002` 剩余项＝T2 侧四板投影一致（V1 版镜像已在板并取得 P3 凭证，本轮 T2 四板 `locked=0`）；然后按逐层验收先查 `VDC-ISO-001`。

### VDC-PROGRESS-20261014-031：按用户口径收敛为**1 bit 发布**并验证；缺口＝向量发布没跟上跟随窗口

- **用户口径（本条为事实源）**：**LOCK 的状态发布用 1 bit 就够（0/1 unlock/locked）**；**粗锁定/精锁定/完全锁定只是主机的过程**，不进发布。DPLL60 **只报 warning，不阻塞运行**。
- **据此实现（`-029` 的简化，删除我先前过度设计）**：
  1. 发布语义＝**1 bit**；`vdc_dpll_lock_verdict_t` 删去 `grade`/区间/`adopted`，只留 `locked`＋`reason`＋`role`；`vdc_dpll_adopt_grade_t` 与 `TRACKING_OUT_OF_BAND` 一并删除。
  2. 角色决定证据：MASTER＝自身 PI 锁定 ∧ 样本窗口有效（`valid`∧`lock_state==LOCKED`∧样本时间≠0∧freshness 上限≠0∧未超龄）∧ gate 通过 ∧ debug 关；FOLLOWER＝**正在跟随**（follow `FRESH` ∧ 未超龄 ∧ 已成匹配）。
  3. **health / tier / provisional 不再压制该位**（tier 是主机内部过程；provisional 由自己的位表达）——这正是"发布的状态不对"的根因。
  4. 实时成本：向量写方改调**小证据访问器** `vdc_dpll_manager_lock_evidence()`（不再在 REFMEM 相位里做整份快照拷贝）。
  5. DPLL60：step 6/7/8 的采集问题降为 **warning**，凭证记录 `capture_exit`/`capture_blocking=false`，运行不再被阻塞。
- **验证（实机，可复现）**：
  - `DPLL60_workflow.py --full` → **`failed steps: none`**（step 6 WARN，采集 exit 1 但不再阻塞）✓
  - REFMEM 相位恢复：NO.1 `max_runtime` 由 29924 **回到 20028/29000**、丢期限 0；从板 29145–29401/29000 **贴 WCET 边**（改前亦有丢期限，需另记）。
  - 角色感知位：arm 运行期间连续采样 200 s，**NO.4 的 LOCKED 位=1 被观测到 92 次**；NO.1/NO.2/NO.3 为 0 次。
- **据此定位的缺口（重要）**：**T2 step 17 要求四板 `locked==1` 且该轮 `failed steps: none`** ⇒ 当时**四板的判定都成立**；但**向量位只在 NO.4 上被抓到**。⇒ **RefMem VDC 向量是"采样发布"，未跟上短时的跟随窗口**（主机位=0 另有依据：NO.1 最后准入样本已 66.8 s，按架构「控制状态因保持而仍为 LOCKED，不能掩盖质量过期」应为 0）。下一缺口＝查 `distributed_refmem_service()`/向量的发布触发与节奏。
- **待裁定**：主机的位是否也忽略新鲜度（当前＝**活的锁**，符合文档；若改为纯保留态则输入已死也报 locked）。

### VDC-PROGRESS-20261014-030：**验证失败**——`-029` 的实现把 REFMEM 相位撑破 WCET（实测抓到）

- **验证方式（目标要求）**：P3 跑完后（`hawf-134838` 通过、`credential: written`、新固件 build `20261014054847` 上台），执行 `DPLL60_workflow.py --full`。
- **结果：FAIL**。`dpll60-135749` step 6 联合采集两次尝试均失败：`child_capture: Capture exited 2` + `RAM restoration failures: ['FB276192BEF9CCE1: SCPI error queue not empty', 'A1E549202D18ED6A: SCPI error queue not empty']`；attempt2 `apply_ram_timing: Expected 3 fields: '<timeout>'`；子进程 `internal_health_passed: false`。
- **真因（门增量对比，改前 vs 改后，同台架同流程）**：

| 指标 | 改前 `dpll60-124722` | 改后 `dpll60-135749` |
|---|---|---|
| NO.1 REFMEM `max_runtime/wcet` | 18262 / 29000 | **29924 / 29000（超）** |
| NO.2 REFMEM | 26603 / 29000 | **35031 / 29000（超）** |
| NO.3 REFMEM | 18805 / 29000 | **32401 / 29000（超）** |
| NO.4 REFMEM | 22592 / 29000 | **29476 / 29000（超）** |
| VDC / DPLL 相位 | — | 基本不变（本次未改） |

  ⇒ **`-029` 的改动把 REFMEM 相位推过 WCET**：我在 `vdc_dpll_manager_get_vector_snapshot()` 内调用 `vdc_dpll_manager_lock_verdict()`，而后者内部会**整份拷贝 `vdc_domain_snapshot_t`** 并再拷贝 observer 结构；该调用位于 **REMFEM 实时相位** ⇒ 相位超预算、丢期限 ⇒ 采集退化。
- **重要教训（两件）**：① **P3 通过 ≠ 实时门通过**——同一固件 P3 的 step 29 采集 OK 98.6 s，因为它对 `dpll_schedule_gate` 容忍；**只有 DPLL60 + 门增量能暴露该回归**。② 实时相位内**不得整份拷贝快照**；跨相位复用判定必须走**纯函数投影**（只读它已持有的字段 + 少量原子字），不能靠调用一个自带大拷贝的入口。
- **修法（下一步）**：抽出**纯投影** `vdc_dpll_lock_evaluate(inputs, out)`（无快照拷贝、无发布），让 `vdc_dpll_manager_lock_verdict()`（用已有 snapshot）与 `get_vector_snapshot()`（用已拷贝的向量字段 + follow/match 两个原子读）**共用同一套规则**，从而既保持"单一定义"，又把 REFMEM 相位恢复到预算内；随后重跑 DPLL60 复验门增量回到改前水平。

### VDC-PROGRESS-20261014-029：实现角色感知的锁发布（推进 `VDC-LOCAL-003`/`VDC-LOCK-001` 的发布侧）

- **依据（文档，逐条）**：`VDC_DOMAIN_TODO.md:24` 目标「四板 DPLL 稳定锁相与 VDC 一致发布；**NO1 自主 PI**，NO2–NO4 接收/ACK/加本地有向 delay 后由各自 Core1 跟踪」；`:37` 里程碑 C 判据「**三从持续采用 DCO**」；`:40` 档位「粗 ≤10 µs / 精 ≤1 µs / **完全 ≤100 ns**」；`:42` 「**四板等级取最低已证明等级**；…**测量不确定度未闭合时不宣称正式锁定**」；`:87` 「**允许先修局部发布缺陷**；完整门禁未闭合时**不得提升 formal quality**」。
- **HAOFV 合规**：`VdcVector` 禁止被 SCPI/PIO/storage 直接写（`HAOFV_VDC_DPLL_ARCHITECTURE.md:67`）；matcher 只形成误差观测、**不发锁相声明**（`VDC_RUNTIME_CONSTRAINTS.md:446`）；一个字段一个 writer（`ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md:91`）。⇒ **由 owner 声明，消费方只读**。
- **已实现**：
  1. **owner 侧角色感知判定**（`vdc_dpll_manager_lock_verdict()`）：MASTER 走原 PI 阶梯（语义不变）；**FOLLOWER 用「持续采用」证据**——`vdc_priority_follow_health_t`（`FRESH` ∧ `age ≤ freshness_limit`）＋ `vdc_priority_match_snapshot_t`（`have_match` ∧ 残差区间）。
  2. **分级用区间上下界**（不是中心）：`extent = max(|lo|,|hi|)` ⇒ 100 ns→`FULL`、1 µs→`FINE`、10 µs→`COARSE`，超出→`NONE`。依 `:42` 语义：不确定度未闭合不得宣称正式档。
  3. **reason 只追加不重编号**：新增 `FOLLOW_DISABLED=8`、`FOLLOW_STALE=9`、`TRACKING_OUT_OF_BAND=10`；**24 字段线格式不变**。
  4. **修复一处 HAOFV 违背**：`SYSTem:SYNC:VDC:LOCK:READiness?` 原先**自己算** `input_ready/locked`，现改为**只读 owner 判定**；同时删除那份已死的本地 reason 委托（消除"各写一份"）。
  5. **判定随事实发布**：owner 的角色/采用/档位/区间进入 `vdc_dpll_manager_vector_snapshot_t`（`lock_role/lock_adopted/lock_grade/lock_interval_*`，RAM 结构、**非 wire**），供 RefMem 镜像与主机页**投影而非重算**。
- **验证（本轮）**：交叉编译通过（`-Werror`，package 2067752）；`tests/python/test_vdc_lock_verdict.py` 扩至含 **D1–D9**（从板各档位/`FOLLOW_*`/`have_match=0`、以及"主机不读从板证据"）全过；受影响的 3 个 harness 补桩后 **45 passed**；`-k "vdc or refmem"` **3955 passed / 0 error**。
- **未做（下一轮）**：③ 把 `grade`/区间暴露给消费面（新只读页或 Core0 保留区诊断子区）；④ 文档同步（架构 L16/L165 对齐、`VDC-LOCK-001`/`VDC-LOCAL-003` 进展、登记 + C11）；⑤ **P3** 与 **DPLL60 验证**（期望四板 `locked` 按角色成立、从板报出已证明档位）。

### VDC-PROGRESS-20261014-028：源头找到了——**答案在 `VDC_DOMAIN_TODO.md` 的目标/里程碑段**（我没读那一段）

- **用户追问**：为什么无法从架构中得出「单 PI 在主机、从板跟随即算锁相」。**答：那句话就在 VDC TODO 的目标段，我漏读了该文档的「当前主线与里程碑」章节**（此前只读了它的任务表行）。
- **`docs/vdc/VDC_DOMAIN_TODO.md:24`（主线目标，原文）**：
  > **目标：四板 DPLL 稳定锁相与 VDC 一致发布。** NO1 自主 PI 并发布闭环事件时间戳；NO2–NO4 接收、返回 ACK，加本地有向 delay 后由各自 Core1 跟踪。NO1 不计算逐从校正，接收也不依赖从板已经锁相。
  ⇒ **单 PI（NO1）+ 从板跟踪** 是明文目标；四板锁相是目标本身。
- **`VDC_DOMAIN_TODO.md:37`（里程碑 C 完成判据，原文）**：`VDC-LONGTERM-002-C` 相位与频差 ｜ **三从持续采用 DCO**，内部斜率与外部波形可对照…
  ⇒ **从板的锁相判据是「持续采用 DCO」，不是「达到 Domain `LOCKED`」**——这正是 typed 跟随通路在做的事（实测：匹配中心 −16…+0.5 ns、每段约 1300 次成功、DCO `update_seq` 127–152）。
- **`VDC_DOMAIN_TODO.md:40`（阶段目标快照）**：粗锁定 **≤10 µs**、精锁定 **≤1 µs**、**完全锁定 ≤100 ns**；相位优化争取 ±50 ns，频差 ±50 ppb。
  ⇒ **±100 ns ＝「完全锁定」档**；且与代码常量一一对应：`VDC_DOMAIN_LOCK_TIER_COARSE_NS=10000`（粗 10 µs）、`DEBUG_NS=1000`（精 1 µs）、`FINE_NS=100`（完全 100 ns），`lock_acceptance_threshold_ns=1000`（判 `LOCKED` 的准入档＝精锁定）。
- **方法论教训（据实）**：我读了架构文档（描述**当前实现**，含 L16「不使 follower 自动获得 LOCKED 资格」、L165「typed DCO 采用不自动推进该状态」这类**实现现状**表述），却没读域 TODO 的**目标与退出判据**。**「应该是什么状态」应以 TODO 的目标/里程碑为源，架构叙述只说明"现在实现成什么样"。**
- **由此确认要修的对象**：发布口径把「从板持续采用 DCO」显示为 `CHECKING`/未锁——与 L24 目标与 L37 判据不符，需引入**角色感知的锁**（MASTER 用 PI 阶梯；FOLLOWER 用跟随/持续采用证据）。

### VDC-PROGRESS-20261014-027：**用户裁定——单 PI 在主机，从板跟随即算锁相；错的只是发布**（更正 `-021/-022/-023/-025/-026` 的判读）

- **架构意图（用户口径，本条为事实源）**：环路系统中**只允许一个 PI 调节锁相，即主机**；**其他板子跟随锁相**。若从机也做 PI，系统复杂度上升、**无法收敛**。⇒ **当前状态就是锁相了**；**发布的状态不对**。
- **因此以下我此前的判读作废**：
  - 把从板 `dpll.state = CHECKING`（其自身 Domain PI 阶梯停在第一级）读成「从板未锁相」——**错**。从板**不应**通过自身 PI 收敛，其 `accepted_sample_count = 0` 是**设计使然**，不是缺陷。
  - 把 `accepted=0`/`ts_source=NONE` 当作「准入未 arm ⇒ 所以从板锁不上」——**错**（方向反了）：从板的正确锁相证据在**跟随通路**（typed FOLLOW/MATCH），不在其 Domain PI 准入计数。
  - `-025` 里「四板都没有达到可发布要求 / 从板连前置都没过」的**结论仍成立，但原因需重述**：不是"从板性能不够"，而是**发布口径把从板跟随锁相当成了未锁**。
- **已确认仍然成立的事实**：从板事件匹配中心 **−16…+0.5 ns**、每段约 1300 次成功、DCO `update_seq` 127–152、`FOLLow:STATus?/HEALth?` 有保留记录（session 1791953343）——即**从板确实在按设计跟随**。
- **要修的对象＝发布口径**（三处）：
  1. `SYSTem:SYNC:VDC:LOCK:READiness?` 的 `locked` 在 handler 内**本地判定**为 `snapshot.dpll.state == LOCKED && input_ready` ⇒ 从板恒为 0；
  2. `REFMEM_VECTOR_FLAG_LOCKED` 的 9 条合取要求 `dpll.state == LOCKED ∧ quality.lock_state == LOCKED ∧ health == HEALTHY ∧ …` ⇒ 从板恒不置位；
  3. 全站只有"主机 PI 锁"一种模型，**没有"角色感知"的锁定义**（架构 L165「typed DCO 采用不自动推进该状态」正是该口径的来源）。
- **修法方向**：在**单一定义**处（`vdc_dpll_manager_lock_verdict()`）引入**角色感知的锁**——MASTER 用 Domain PI 阶梯；FOLLOWER 用**跟随通路证据**（FOLLOW 已启用 + health FRESH + 年龄在 `VDC_PRIORITY_FOLLOW_MAX_AGE_MS` 内，必要时加匹配成功/残差门限）；并让 ①② 两处**消费同一判定**，再按契约登记（含 C11）。

### VDC-PROGRESS-20261014-026：NO.1 的 DEGRADED 唯一成因＝freshness 过期；±100 ns 的准确含义

- **NO.1 逐项实测（`READ:SYNC:QUALity?`，字段名已按 `scpi_sync_quality_q` 核对）**：

| 字段 | 值 | 要求 | 判定 |
|---|---|---|---|
| 域 `state` | **5 LOCKED** | == LOCKED | ✅ |
| `tier` | **3 = FINE_100NS** | ≥ FINE | ✅ |
| `reject_code` | **0 = GATE_PASS** | == GATE_PASS | ✅ |
| `rms_offset_ns` / `last_offset_ns` | **1 / 0** | ≤ `fine_thr` | ✅（余量 ~100×） |
| `max_abs_offset_ns` | 168 | — | 历史最大（含捕获暂态） |
| **`age_us`** | **779083258（=779 s）** | ≤ freshness 上限 | ❌ |

  ⇒ **`HEALTHY` 的其余合取项全部满足，唯一不满足的是 freshness（最后一次准入样本已过 779 s，因为此刻环是停的）** ⇒ `DEGRADED`。这属**设计行为**（架构：空转维护 age/health），**环在跑时本可 HEALTHY**。
- **`VDC_DOMAIN_GATE_PASS = 0u`（更正）**：readiness 第 12 字段 `quality.last_reject_code == 0` 表示**通过**，此前把「0」读成"gate 未通过"是**错的**（`vdc_domain.h:107`）。
- **「±100 ns」的确切含义（三层）**：
  1. **它是 FINE 档的验收门限**：`VDC_DOMAIN_LOCK_TIER_FINE_NS = 100`（`vdc_domain.h:18`），`tier=3` 即 `FINE_100NS`；②相位档判的就是 `|校正相位误差| ≤ 100 ns`。
  2. **它同时是当前观测的分辨率底线**：记录残差**区间跨度 190–216 ns ≈ ±100 ns**（`-024`）。故这些记录**能支持「满足 ±100 ns」，不能支持「优于 ±100 ns」**——区间中心（−16…+0.5 ns）的可信度受区间宽度限制。
  3. **它是控制/质量口径，不是 GPIO 物理精度**：架构明确「计数分辨率不等于物理同步精度」「内部模型不等于 GPIO」「稀疏波形不证明未采样区间」，四个判定（参考到达 / 控制采用 / 引脚精度 / 共同时间发布）须分别成立。
- **由此得出的关键结论**：**±100 ns 这条要求本身不是瓶颈**——NO.1 `rms=1 ns`、从板事件匹配中心 −16…+0.5 ns，均在门限内且余量很大。真正卡住 4/4 的是：① 从板**准入记账 = 0**（未 arm）；② NO.1 的 **freshness 过期**（环停）。两者都是**管路问题，不是精度问题**。

### VDC-PROGRESS-20261014-025：锁相的**定义与要求**（代码级，含门限常量）

- **定义（代码事实）**：Domain 的 `dpll.state` 取 `VDC_DOMAIN_LOCK_LOCKED = 5`，当且仅当**准入样本数足够、且「校正相位误差」在门限内连续保持够多拍**——即「本地时基已被扳到与参考一致并保持」。判据在 `components/vdc_domain/src/vdc_domain.c:4222-4247`。
- **要求（逐条，含常量）**：

| 层级 | 要求 | 常量/来源 |
|---|---|---|
| **前置** | 必须有**准入样本**（过 evidence gate），`accepted_sample_count` 才增长 | evidence gate |
| ① 样本档 | `accepted ≥ 1 / 2 / 3` 分别进入 `INITIAL_SYNC / FREQ_LOCK / PHASE_LOCK` | `INITIAL_LOCK_SAMPLES=1`、`FREQ_LOCK_SAMPLES=2`、`PHASE_LOCK_SAMPLES=3`（`vdc_domain.c:19-21`） |
| ② 相位档 | `\|校正相位误差\| ≤ lock_threshold_ns`，门限按质量档：**FINE 100 ns** / DEBUG 1000 ns / COARSE 10000 ns | `VDC_DOMAIN_LOCK_TIER_FINE_NS=100`、`_DEBUG_NS=1000`、`_COARSE_NS=10000`（`vdc_domain.h:18-20`）；默认 servo `offset/debug/coarse_lock_threshold_ns` 取同值（`vdc_domain.c:1620-1622`） |
| ③ 持续档 | `good_streak ≥ servo.lock_sample_count`（**默认 4**）；`good_streak` 在 `\|相位\| ≤ 门限` 时 +1、否则清 0 | `vdc_domain_default_servo(): lock_sample_count = 4u`（`vdc_domain.c:1624`） |
| ④ 保持/退出 | 已 `LOCKED` 而相位越界 ⇒ **`RELOCKING`**（不直接回落 `LOCKED`） | `vdc_domain.c:4231-4232, 4242-4244` |
| ⑤ 健康（更严） | `LOCKED` ∧ gate 通过 ∧ fine tier ∧ 有效且未超龄 freshness ⇒ `HEALTHY` | 架构文档 L167 |
| ⑥ 发布（最严，可对外声明） | 非 provisional ∧ debug continuation 关 ∧ Domain/quality 均 `LOCKED` ∧ quality valid ∧ `HEALTHY` ∧ tier ≥ FINE ∧ 样本时间与 freshness 非零 ∧ age ≤ limit ∧ gate 通过 ⇒ `REFRESH_VECTOR_FLAG_LOCKED` | `distributed_refmem.c:2498-2509`（9 条合取） |
- **重要区分（判据用的不是 trace 残差）**：② 判的是**准入样本算出的「校正相位误差」**，不是 `VDC_SUMMARY_TRACE` 记录里的事件残差区间（后者是匹配/观测口径，见 `-024`）。故**残差中心十几 ns 并不自动授予 `LOCKED`**——前置的准入记账若为 0，②③ 根本不会被评估。
- **对台架实测的解释**：NO.1 `accepted=950`、相位 0 ns、`good_streak=950 ≥ 4` ⇒ 通过 ①②③④ ⇒ `LOCKED`；三从板 `accepted=0` ⇒ **停在①之前**（`CHECKING`），②③ 未评估。

### VDC-PROGRESS-20261014-024：从板「没锁相却 ±100 ns」不矛盾——**±100 ns 是区间宽度，真实偏差是中心（−16…+0.5 ns）**

- **用户追问**：既然从板没有锁相，为什么偏差能到 ±100 ns？
- **实测（`dpll60-124722` 稳态 bin1–5）**：

| 板 | 残差区间 | **区间中心** | 跨度 | 同记录 `max_width_ns` |
|---|---|---|---|---|
| NO.2 | [−107,+93] … [−96,+91] | **−7.0 … −1.5 ns** | 187–200 | 172–176 |
| NO.3 | [−108,+91] … [−124,+92] | **−16.0 … −4.5 ns** | 193–216 | 172–196 |
| NO.4 | [−117,+91] … [−96,+97] | **−13.0 … +0.5 ns** | 190–208 | 172–176 |

- **结论一：±100 ns 是区间宽度（测量不确定度），不是偏差幅度。** `residual_min/max` 是区间上下界 `[L.lo − E.hi, L.hi − E.lo]`（架构文档 L121），其跨度 ≈ 时间戳区间宽度（`max_width_ns` 172–196 ns）。架构文档 L105 **明确警告过**：「时间戳记录完整区间，**不能用中点隐藏测量宽度**」——同理，**也不能把区间宽度读成误差幅度**。
- **结论二：真实事件时间一致性＝区间中心＝−16…+0.5 ns**，即从板的事件时间映射与主板一致到**十几 ns**。
- **结论三：从板其实在跟踪。** `SYSTem:SYNC:VDC:DPLL:STATus?` 的 `update_seq = 140/127/152` 说明其 DCO **确实在被更新**（自由振荡的 TCXO 在 60 s 内不可能保持十几 ns）——这是**typed 本地跟随**这条**独立控制路径**在工作。
- **结论四：「没锁相」指 Domain 锁态，由另一条路径记账。** 架构 L137/L165：FOLLOWER 的普通 local evidence 是 **diagnostic-only**、**typed DCO 采用不自动推进 lock 状态** ⇒ 其 `accepted_sample_count = 0` ⇒ 停在第一级 `CHECKING`（见 `-023` 的阶梯）。
- **因此三个数值各有其义，不可互换**：① 记录残差**区间**（跨度 ~190–216 ns＝不确定度，中心 ~0–16 ns＝真实一致性）；② `DPLL:FILTer?` 的 `last_phase_error_ns`（从板 ±0.2–0.4 ms＝Domain 质量投影，未准入故无意义）；③ `accepted_sample_count`（准入记账，从板 0）。

### VDC-PROGRESS-20261014-023：锁相阶段的**确切阶梯与可达性**（枚举 9、可达 7、2 个无可达赋值点）

- **枚举 9 个**（`components/vdc_domain/inc/vdc_domain.h:42-52`）：`0 OFF`、`1 CHECKING`、`2 INITIAL_SYNC`、`3 FREQ_LOCK`、`4 PHASE_LOCK`、`5 LOCKED`、`6 HOLDOVER`、`7 RELOCKING`、`8 FAULT`。
- **实际迁移判据**（`components/vdc_domain/src/vdc_domain.c:4222-4247`，按 `accepted_sample_count` 与相位/连续良好数分档；门限 `VDC_DOMAIN_INITIAL_LOCK_SAMPLES=1`、`FREQ_LOCK_SAMPLES=2`、`PHASE_LOCK_SAMPLES=3`）：

| 阶段 | 进入条件 |
|---|---|
| `OFF` | `ready==0` 或复位 |
| `CHECKING` | `accepted < 1`（即 0） |
| `INITIAL_SYNC` | `accepted < 2`（==1） |
| `FREQ_LOCK` | `accepted < 3`（==2） |
| `PHASE_LOCK` | `accepted ≥ 3` 但 `|相位| > lock_threshold_ns` **或** `good_streak < servo.lock_sample_count` |
| `LOCKED` | `accepted ≥ 3` ∧ `|相位| ≤ lock_threshold_ns` ∧ `good_streak ≥ lock_sample_count` |
| `RELOCKING` | 曾 `LOCKED` 而相位越界（或再捕获中未达 `LOCKED` 条件） |

- **可达性结论**：**采集阶梯 6 级 + `RELOCKING` = 7 个可达**；**`HOLDOVER(6)` 与 `FAULT(8)` 在 domain 源码中没有任何赋值点**（与架构文档「并非所有失效迁移都已接通」「缺参考自动 HOLDOVER…仍属 `VDC-HOLD-001` 后续门禁」一致）。
- **与台架实测对应**：NO.1 `accepted=950` ⇒ 满足 `LOCKED` 全部条件（相位 0 ns、`good_streak=950`）＝ **5 LOCKED**；三从板 `accepted=0` ⇒ 停在**第一级** `CHECKING`（`accepted < 1`）。即在准入开始之前，从板**不可能**进入任何更高阶段。
- **勿混淆的三个不同阶梯**：锁态 9（可达 7）：本文；健康 6（`UNKNOWN/CHECKING/DEGRADED/LOCK_CANDIDATE/HEALTHY/FAULT`）；质量档 4（`NONE/COARSE_10US/DEBUG_1US/FINE_100NS`）。

### VDC-PROGRESS-20261014-022：从板残差为何在 ±100 ns 内——**typed MATCH 观测通路在工作，准入通路没有**（更正 `-021` 的用词）

- **用户追问**：既然从板 `accepted=0`、`ts_source=NONE`、`state=CHECKING`，为什么记录的残差仍在 ±100 ns 内？
- **实测（`dpll60-124722` 每 10 s bin，稳态 bin1–5）**：

| 板 | 成功/段 | 拒绝/段 | 拒绝率 | 残差 |
|---|---|---|---|---|
| NO.2 | 1254–1337 | 32–37 | ≈2.5% | [−107,+94] ns |
| NO.3 | 1331–1353 | 27–35 | ≈2.3% | [−124,+96] ns |
| NO.4 | 1269–1375 | 28–36 | ≈2.4% | [−117,+97] ns |

- **结论**：该残差是 **typed MATCH 的逐事件区间残差**（本地事件坐标 vs 主板投影事件时间＋路径 delay），**每段有约 1300 次成功匹配、只拒约 30 次**。按架构与代码，**从板的本地 TDMA evidence 通路「仍为可观测性而运行」**（`sync_dpll_fb_step` 注释：FOLLOWER 的本地证据由 Domain 标为 diagnostic-only、**永不喂本地 PI/DCO**）⇒ 它**不依赖** `accepted_sample_count`，因此在准入为 0 时仍能给出亚 100 ns 的匹配残差。
- **两件事因此并不矛盾**：从板在「**匹配/观测**」层面**已经达标（<100 ns，成功率 ≈97.5%）**；在「**准入/控制**」层面**尚未接线**（`accepted=0` → `ring_input=false` → readiness 页退回空的 observer 时间戳 → `NONE`、`state=CHECKING`、不发布 LOCKED）。
- **更正 `-021` 的表述**：原文「三从板……根本没有在消费 / 没有样本源」**过强**；准确说法是——**从板在匹配层正常消费并跟踪（每段约 1300 次成功、残差 <100 ns），但没有任何样本被准入到 Domain**。
- **注意区分三个不同口径**：① 记录里的 `residual_*`＝typed MATCH 区间残差（<100 ns，本文）；② `SYSTem:SYNC:VDC:DPLL:FILTer?` 的 `last_phase_error_ns`＝Domain 质量的相位误差投影（从板为残留值 ±0.2–0.4 ms，未准入故无意义，见 `-017`）；③ Domain `accepted_sample_count`＝准入计数（从板 0）。

### VDC-PROGRESS-20261014-021：「三从板没有时间戳源」的架构级答案＝**准入通路未被 arm**，非固件缺陷

- **`timestamp_source` 是投影而非独立能力**（`scpi_sync_commands.c` readiness handler）：
  `ring_input = accepted_sample_count != 0`；`timestamp_source = ring_input ? 域质量.last_timestamp_source : observer.last_timestamp_source`。故 `NONE` 的直接含义是**两支来源都没有记录**，不是缺少某件硬件/固件能力。
- **从板在跟随，但没有任何样本被准入**：`SYSTem:SYNC:VDC:STATus?`（`ready, lock_state, service_count, first_ms, last_ms, sync_seq`）实测 NO.2/3/4 = `1,1,139/126/151,…`，`SYSTem:SYNC:VDC:DPLL:STATus?`（`ready, state, service_count, first_ms, last_ms, update_seq`）= `1,1,167134/168641/170145,…,**update_seq 140/127/152**` ⇒ 从板**确实在处理环事件并更新本地 DCO**；但 `accepted_sample_count = 0`。
- **架构依据**：`docs/vdc/VDC_DOMAIN_ARCHITECTURE.md` L107-141 明确从板路径为「主板发布事件时间戳 → TDMA 固定邮箱 → RX IRQ 保全/解码 → MATCH → FOLLOW → 本板 DCO」；同时 L137/L165 明确 **FOLLOWER 的普通 local evidence 不进入 MASTER PI**、**typed DCO 采用不自动推进 lock 状态**，且「**显式 typed 本地跟踪是独立控制路径**」。⇒ 从板要成为「正式输入」必须**显式 arm**。
- **arm 序列（工具侧事实）**：`tools/sync_trigger/t2_workflow.py:600-637` 发送
  `SYSTem:VDC:PRIORity:SYNC <gen>` → `SYSTem:VDC:PRIORity:MATCH <gen>` → `SYSTem:VDC:PRIORity:FOLLow 1`（→ 可选 `FOLLow:PHASe 1`），并在 `:2068` 发送 `SYSTem:SYNC:VDC:OBServer:TDMA 1,0,100,0`。
  **只有 T2 工作流发这些**；`DPLL60`/P3 流程只准备环与停机态交接，**从不 arm**，且其软件复位（DPLL60 step 2 / P3 step 7）会**清除**已 arm 状态。
- **实测印证该因果**：在 T2 工作流跑过之后，从板 reason 由 `2 OBSERVER_DISABLED` 推进到 `5 TIMESTAMP_NOT_ELIGIBLE`（observer 已 arm、字典 2 项）；DPLL60 复跑（含复位）之后，四板 observer/字典归零、`ring|sync_io=0` ⇒ reason 退回未 arm 态。
- **结论**：三从板「没有时间戳源」= **准入通路（typed FOLLow / observer）未被 arm 且准入计数为 0**；这是**配置/流程缺口**，不是固件缺陷，也不是架构遗漏。要让从板真正参与锁定，流程必须按 T2 工作流的序列对每块从板 arm。

### VDC-PROGRESS-20261014-020：DPLL60 复跑评估（`dpll60-124722`）——运行本身干净，问题均为存量

- **运行结果**：`failed steps: none`；step 6 采集 **passed on attempt 1（110.1 s）**；子进程 `{"internal_health_passed": true, ..., "passed": true, "external_status": "SKIPPED"}`；量级正常（`actions.jsonl` 118103 B、`input-probe.json` 296539 B）。warn 仍是已知三项：step 3（`realtime_gate=False closed_loop=False`）、step 4（无示波器→internal only）、step 9（工作流凭证非正式凭证）。
- **残差（与 `-019` 基线一致）**：NO.1 无有效残差段；NO.2/3/4 **稳态 bin1–5 = [−107,+94] / [−124,+96] / [−117,+97] ns**；bin0（捕获段）首成功 = 296322780 / 433676577 / 578645961 ticks ≈ **1.19 / 1.73 / 2.31 s**，落在历史基线内（NO2 0.86–1.28、NO3 1.12–1.88、NO4 1.36–2.45 s）。
- **参考板环路健康**：`state=5 LOCKED`、`phase=0 ns`、`consecutive_good=950`、`accepted=950`、`ts_eligible=1`（health 仍为 DEGRADED）。
- **仍存在的存量问题（非本次引入）**：① `dpll_schedule_gate` 不通过（DPLL 相位超预算 → VDC/REFMEM 丢期限），是「严格全绿」的唯一实质障碍，且相位预算不能单独放大；② 三从板**未进入锁定且未开始捕获**（`ts_eligible=0`、`accepted=0`、`state=1`，残差为残留值）——**与「锁相需要时间」不同，它们没有样本源**；③ 发布面四板 `flags=0x55` 完全一致，无法区分「参考已锁 950 样本」与「从板未开始」；④ 工具侧 `physical_continuous_lock_qualified`/`exact_event_correlation_qualified` 等硬编码 False；⑤ 稳态负侧 −107…−124 ns 略超 ±100 ns（历史 −94…−136 ns，非回归）。

### VDC-PROGRESS-20261014-019：历史 P3 记录的残差核对——**稳态在 ±100 ns 内，唯一例外是 bin 0 启动段**

- **核对目标（用户）**：之前的 P3 记录，偏差应都在 ±100 ns 以内。
- **数据源**：`out/HardwareAcceptance/**/hawf-*/**/capture/NO*/decoded.json` 的 `records[].residual_min_ns` / `residual_max_ns`（仅 `residual_extrema_valid=true` 有效）。共 **9 次 P3 运行、36 份产物**；`bin_index` 0=启动段、1–5=稳态段。
- **稳态段结论（支持用户预期）**：全部 9 次运行中，每个从板的 **bin 1–5 残差均在约 ±100 ns 内**（正侧典型 +88…+100 ns、负侧 −94…−127 ns，个别触到 −136 ns）。
- **唯一例外＝每个从板的 bin 0**：`hawf-114312` 实测 NO.2 `res=[−136, +673063] ns`、NO.3 `[−9141300, +91]`、NO.4 `[−4113324, +88]`，其 `first_success_offset_ticks` 分别为 **310667545 / 447067705 / 583166864**（≈1.24/1.79/2.33 s @250 MHz，按席位每板多约 0.54 s），`rejected` 616/886/1194；其余段 `first_success_offset_ticks` 仅 6–13 万。历史运行里该段 `residual_min` 达 −92…−350 ms，且随席位序号递增。
- **判读**：bin 0 的负向大偏离是**锁定前的启动/首次成功偏差**，不是跟踪误差（与 `SUMMARY_STEADY_SUCCESS_GAP`、以及架构文档「首次成功不表示锁定」一致）；其「≈1×/2×/3× 板号」的递增正是**按席位递增的首次成功延迟**。
- **补充事实**：参考板 NO.1 的段一律 `residual_extrema_valid=false`（无有效残差）⇒ 该残差口径只评估从板；参考板的偏差应看 `SYSTem:SYNC:VDC:DPLL:FILTer?`（见 `VDC-PROGRESS-20261014-017`，实测 −5 ns）。

### VDC-PROGRESS-20261014-018：P3 的 DPLL 段**不能脱离自有前置单独重跑**；内部监测状态全程不变

- **动机**：直接跑 P3 的 DPLL 段（step 27–30 / 24–30），看内部监测状态。
- **三次尝试与签名**：
  1. `--steps '27-30'`（沿用 `hawf-114312`）：step 29 **FAIL 35.4 s**，`Capture exited 2` + `restore_original_ram: RAM restoration failures: ['0010071E65B5CB38: SCPI error queue not empty', '2BD5090FE009FA2A: SCPI error queue not empty']`（正常为 99–100 s）。原因：step 29 依赖 step 24–26 于 11:59 制备的停机态交接，而此后 T2 工作流（把环留在运行）与 `DPLL60 --full` 已改变台架。
  2. `--steps '24-30'`（自洽切片）：step 24 OK 28.8 s，但 step 26 warn 与 step 29 **FAIL 1.0 s** —— **NO.1 recorder 卡在 `state 5`**（`epoch 1791950600, written 14/14, bytes 10068, reason 0`），`--cancel-unsaved-record` 无法清除，交接无法检查。
  3. 先跑 **DPLL60 step 0**（其设计目的正是「完成保存或取消记录 → 等空闲态 0/8/9」，四板均报 `recorder state 5` 后被清掉，OK 3.7 s）再跑 `--steps '24-30'`：step 24 OK 32.4 s、step 26 warn，step 29 仍 **FAIL 31.2 s**（`Capture exited 2` + `0010071E65B5CB38: SCPI error queue not empty`）。
- **内部监测状态（三次皆相同，实测两次）**：NO.1 `flags=0x55 / state=5 LOCKED / health=2 DEGRADED / 相位 −5 ns / accepted 207 / ts_eligible 1`；NO.2–4 `flags=0x55 / state=1 CHECKING / health=1 / 相位为残留值 / accepted 0 / ts_eligible 0`。⇒ **失败的 DPLL 采集不改变内部监测状态**（保留上次发布快照的投影，符合设计）。
- **结论（过程约束）**：**打包的 DPLL 采集依赖其自有前置步骤与 OTA 所建立的干净台架状态**；在其它工作流动过台架后，单独重跑 24–30 不支持。需要「跑通的 P3 DPLL」时应走 **整条 P3**（已验证通过）或 **DPLL60 工作流**（已验证通过，自带准备与 step 0）。
- **顺带验证了一个修复**：上述失败留下 `schema=HAOFV_P3_JOINT_PROVISIONAL_V1` 的临时凭证（已从备份恢复为 `..._QUICK_GRADED_V1` / build `20261014034324`）；而 **DPLL60 step 0 在临时凭证在位的情况下照常跑通**，证明 `VDC-PROGRESS-20261014-013` 的「期望 build 改读在线值」修复确实解除了该陷阱。

### VDC-PROGRESS-20261014-017：内部监测的 DPLL 偏差实测；从板偏差值是残留、不可读作跟踪误差

- **偏差暴露面**：`SYSTem:SYNC:VDC:DPLL:FILTer?`（8 字段：`dpll.state`、`update_seq`、**`last_phase_error_ns`**、**`last_frequency_error_ppb`**、`loop_filter_integrator_ppb`、`clock.period_adjust_ppb`、`quality.consecutive_good_samples`、`quality.rejected_sample_count`）；另有 `SYSTem:REFMEM:VDC:VECtor?` 与 `SYSTem:REFMEM:DPLL:VECtor?` 各带一个 `last_phase_error_ns` 投影，以及 `READ:SYNC:QUALity?`。
- **实测（环运行中）**：

| 板 | state | update_seq | **相位偏差 ns** | 频差 ppb | 积分器 ppb | 周期修正 ppb | 连续良好 | 拒绝 |
|---|---|---|---|---|---|---|---|---|
| NO.1 | **5 LOCKED** | 422 | **−5** | 0 | 2882 | −2882 | 51 | 0 |
| NO.2 | 1 CHECKING | 5 | −82014 | 0 | 0 | 0 | 0 | 0 |
| NO.3 | 1 CHECKING | 11 | −252072 | 0 | 0 | 0 | 0 | 0 |
| NO.4 | 1 CHECKING | 14 | 258146 | 0 | 0 | 0 | 0 | 0 |

- **结论**：**唯一有意义的闭环偏差是参考板 NO.1 = 相位 −5 ns、频差 0 ppb、积分器 2882 ppb、周期修正 −2882 ppb、连续良好 51、拒绝 0**。
- **误读警告（重要）**：三从板的 ±82 µs/±252 µs/±258 µs **不是跟踪误差**——同一页 `freq_err=0 / integrator=0 / period=0 / good=0` 且 `update_seq` 仅 5/11/14，说明环路几乎未运行（与 `accepted=0` 一致），这些是残留值。结合架构文档「`rms_offset_ns`/`jitter_rms_ns` 是历史平滑统计字段，不能当作严格统计 RMS 的精度证明」，从板的偏差数字**在未消费样本前不具精度含义**。
- **交叉印证**：`READ:SYNC:QUALity?` 对 NO.1 给出 `DEGRADED, −5, …, accepted 207, res_ns 8, source 2(HARDWARE_TICK)`；三从板为 `CHECKING, 0, …, accepted 0, res_ns 8, source 1`。

### VDC-PROGRESS-20261014-016：VDC 锁定状态发布面实测——**四板 flags 完全相同，无法区分两种截然不同的状态**

- **发布面有两处**：RefMem VDC 向量（`SYSTem:REFMEM:VDC:VECtor?` 的 `flags`，跨域消费面）与锁定裁决页（`SYSTem:SYNC:VDC:LOCK:READiness?` 24 字段）。
- **flags 位**（`components/distributed_refmem/inc/refmem_vdc_vector.h:21-27`）：`VALID(1<<0)`、`STALE`、`SCHEDULE_VALID(1<<2)`、`CALIBRATION_VALID`、`HARDWARE_EVIDENCE(1<<4)`、**`LOCKED(1<<5)`**、**`PROVISIONAL(1<<6)`**；`LOCKED` 的产生是 `distributed_refmem.c:2498-2509` 的 **9 条合取**（`!provisional` ∧ `debug_continue_enabled==0` ∧ `dpll.state==LOCKED` ∧ `quality.valid` ∧ `quality.lock_state==LOCKED` ∧ `quality.health_state==HEALTHY` ∧ `tier>=FINE_100NS` ∧ `last_sample_time_ns!=0` ∧ `freshness_limit_us!=0` ∧ `age<=limit` ∧ `gate.passed`）。
- **实测（环运行 + 观察器已启用，`t2_workflow --steps '1-17'` 之后）**：

| 板 | 向量 flags | 裁决页 input/locked/reason | dpll.state | health | accepted | gate |
|---|---|---|---|---|---|---|
| NO.1 | **0x55**（VALID,SCHEDULE_VALID,HARDWARE_EVIDENCE,PROVISIONAL） | 1/1/**0 READY** | **5 LOCKED** | 2 DEGRADED | 207 | 0 |
| NO.2 | 0x55（同上） | 0/0/**5 TIMESTAMP_NOT_ELIGIBLE** | 1 CHECKING | 1 CHECKING | 0 | 0 |
| NO.3 | 0x55 | 0/0/**5** | 1 | 1 | 0 | 0 |
| NO.4 | 0x55 | 0/0/**5** | 1 | 1 | 0 | 0 |

  reason 枚举（`vdc_dpll_manager.h:708-716`）：`0 READY`、`1 SNAPSHOT_UNAVAILABLE`、`2 OBSERVER_DISABLED`、`3 DICTIONARY_EMPTY`、`4 NO_ACCEPTED_SAMPLE`、`5 TIMESTAMP_NOT_ELIGIBLE`、`6 GATE_REJECTED`、`7 NOT_LOCKED`。
- **缺陷（发布面缺失可区分性）**：四板 `flags` **完全相同（0x55）**，却掩盖了两种截然不同的现实——参考板**已 LOCKED（state 5、accepted 207）但 provisional + DEGRADED**，从板**根本没在消费（state 1、accepted 0、时间戳不合格）**。跨域消费者只看 `flags` 时无法判断「谁锁了、谁卡在哪、卡在什么条件」。
- **从板阻塞已精确具名且随启用程度推进**：观察器未启用时为 `reason=2 OBSERVER_DISABLED`；按 `SYSTem:SYNC:VDC:OBServer:TDMA 1,0,100,0` 启用后变为 `reason=5 TIMESTAMP_NOT_ELIGIBLE`。
- **该不合格是当前模式的必然**：`vdc_dpll_manager_timestamp_dpll_eligible()`（`vdc_dpll_manager.c:3406-3416`）要求 `source==HARDWARE_TICK ∧ resolution∈(0,准入上限] ∧ 有 DPLL_ELIGIBLE 位 ∧ **无 DIAGNOSTIC_ONLY 位**`；当前所有流程均跑在 provisional/诊断模式（`SYSTem:SYNC:VDC:DPLL:ProVisional 1` / `--dpll-provisional`），从板消费的是**对端**时间戳且带 `DIAGNOSTIC_ONLY` ⇒ 按设计不合格；参考板用本地硬件 tick 故能锁。
- **下一步（完善发布，待定方向）**：① **可区分性**——让跨域面能表达「锁态 + 阻塞原因」（向量加位是契约扩展需登记；或改由裁决页/`vdc_dpll_manager_lock_verdict()` 承担，其已含 reason/state/health/accepted 且 T2 可读）；② **语义决定**——provisional 模式下从板**是否应该**锁定：应当则放宽时间戳准入并**单独标注 provisional 锁**，不应当则在发布里显式表达。
- **未核验**：未改动发布实现；未测非 provisional（关掉 `ProVisional`/诊断准入）时从板能否锁定与 LOCKED 位能否置位。

### VDC-PROGRESS-20261014-015：干净基线的锁相发布证据；「锁合格」类结论在工具侧是**硬编码占位**

- **基线来源**：台架恢复（OTA ABORt + 期望 build 改读在线值）后，`DPLL60_workflow.py --full` 全程 `failed steps: none`，**step 6 采集 `capture passed on attempt 1`**，子进程 `{"internal_health_passed": true, ..., "passed": true, "external_status": "SKIPPED"}`。运行目录 `out/HardwareAcceptance/20261014/dpll60-120211`。
- **「已备好」量级复核（与失败形态对照）**：`capture/actions.jsonl` **117668 B**（失败时 13260）、`capture/input-probe.json` **295993 B**（失败时 48886）、四板 `nodeN_board_record.bin` **10.0–11.2 KB**（失败时 NO.1 仅 2708）⇒ 代码文档所述的准备完成量级成立。
- **四板发布证据（干净基线）**：

| 板 | decode schema | state | reason | session/generation | complete_window_proven | physical_lock_qualified |
|---|---|---|---|---|---|---|
| NO.1 | V10 | 3 | 8 | 1791950631（共享） | False | False |
| NO.2 | V9 | 3 | 8 | 共享 | False | False |
| NO.3 | V9 | 3 | 8 | 共享 | False | False |
| NO.4 | V9 | 3 | 8 | 共享 | False | False |

  ⇒ 四板**共享同一 session/generation、状态一致**（较 `VDC-PROGRESS-20261014-001` 的污染轮次已改善）；仍存 **参考/从板 decode schema 分裂（V10 vs V9）**。
- **关键发现（发布面不完整）**：上表后两列**不是板上事实**：`tools/vdc_priority_trace/vdc_priority_trace.py:217,318,372,452` 一律 `complete_window_proven=False, physical_lock_qualified=False`（硬编码），`tools/vdc_priority_trace/vdc_priority_joint_capture.py:188` 亦硬编码 `physical_continuous_lock_qualified=False`。⇒ 当前证据链里**真正被判过的只有内部健康**（`internal_health_passed=true`），所有「物理锁合格 / 完整窗口已证」类结论都是占位常量，不能当作板上锁相结论引用。
- **下一步（完善发布）**：① 让这些占位改为由板上 trace 派生（或明确标注为占位，避免误读）；② 查参考/从板 `status['schema']` 分裂（10 vs 9）是角色相关设计还是缺陷（工具接受任意 `VDC_SUMMARY_TRACE_DECODE_V*`，故不阻断）；③ 继续固件侧锁态发布路径本身。
- **未核验**：未改动这些占位；未定位 schema 分裂来源。

### VDC-PROGRESS-20261014-014：P3 step 8 失败的真因＝**被中断的 OTA 让四板停在 RECEIVING**；用 `SYSTem:OTA:ABORt` 清除

- **现象**：`hawf-114312` 的 step 8（P0T 拓扑）FAIL，工具控制台只投影 `passed/anchor_id/ring_order/adjacency/pair_results`（**不含 `error`**），故先前看不到原因；真实错误在 P0T out-dir 的 `summary.json`：
  `RuntimeError: Calibration profile apply failed`，四板全同——发 `SYSTem:TDMA:RING:STOP` 得 `OK` 后等待环状态超时（`started=False topology=None`），`last={... ring_config_seq: 6, ring_applied_config_seq: 1 ...}` 即**存在未应用的挂起环配置**。
- **真因**：我 kill 掉的那个整跑当时正处在 `Step 4 [B] 刷写（--method ota ...）build=20261014035018` **OTA 中途**；被中断后四板一致停在
  `SYSTem:OTA:STATus? → "RECEIVING",1,"NONE",1`、`MODE? → "DIRECT_AB",1`、`STReam:STATus? → 5,0,0,0,0`。
  固件 `tdma_component_core1_service()` 首句即 `if (ota_ao_is_active()) { return; }` ⇒ **OTA 会话 active 时 TDMA 服务被整段跳过** ⇒ 环配置永不应用 ⇒ 与 `ring_config_seq=6 / applied=1` 完全一致。**与我的工具改动无关（P3 不调用 DPLL60 工作流），也不是台架硬件故障。**
- **解法（无需重烧）**：固件注册了 `SYSTem:OTA:ABORt`（`middleware/scpi_port/inc/scpi_ota_commands.h:69`，同族还有 `COMMit`/`BOOT`/`END`/`MODE?`/`STATus?`/`RESult?`/`JOURnal?`/`BCB:HEALth?`/`STReam:*`）。对四板发 `ABORt` → `STATus?` 变为 `"ABORTED",0,"ABORTED",5`、`RESult? = 5,"ABORTED","APPLIED",2,...` ⇒ 半截 OTA 丢弃。**随后 step 8 即 OK 31.9 s**（此前 6.8 s FAIL）。
- **续跑完成**：`--run hawf-114312 --steps '9-36'` 按序跑完并 **`credential: written`**（step 24 OK 36.2 s、step 29 DPLL 采集 OK 99.0 s；step 19/23/26/33 为 warn）。⇒ 「不重复已完成流程」的断点续跑路径成立。
- **教训（流程）**：**不得在 P3 的 OTA 步骤中途终止**；一旦发生，恢复手段是 `SYSTem:OTA:ABORt`（不是重烧），否则后续所有 TDMA/DPLL 步骤都会表现为「环配置不应用」。

### VDC-PROGRESS-20261014-013：按台架所有者口径落实——独立 DPLL60 以**板上在线 build** 为准

- **用户口径**：DPLL60 工作流**只用于验证当前板上固件的功能**，**不需要对照构建记录**（也不需要 OTA）。
- **已实施**（`tools/vdc_priority_trace/DPLL60_workflow.py`）：
  - 新增 `online_builds(ctx)`：经 `p3_hardware_acceptance.read_online_builds()` 读取四板**在线 build**，每轮缓存一次；
  - `expected_build(ctx)` 改为**以在线 build 为准**（返回 `"(from online boards)"`）；四板不一致时报 `online boards (mixed)` 并让消费者显式失败；
  - 原「凭证/OTA 记录」推断逻辑降级为 `record_build(ctx)`，**仅当串口发现失败、读不到在线 build 时**才使用（旧值不再能锁死健康台架）；
  - 连带更新 step 2 的失败文案。
- **验证**：语法与 `-k "dpll60 or dpll_workflow"` 12 例通过；新增硬件无关回归测试 `tests/python/test_dpll60_workflow_build_expectation.py`（3 例：在线优先且不查记录、混合 build 显式上报、读不到时才回退记录）全通过。实机冒烟：`--steps '2'` → `expected build: 20261014032934 (from online boards)` → `reset issued for 4 boards` → **OK 4.4 s、`failed steps: none`**。
- **替代的旧机制记录**：`VDC-PROGRESS-20261014-012` 描述的优先级（bench config ＞ 凭证（忽略 PROVISIONAL）＞ 最新 passed OTA 记录 ＞ 凭证）已不再是默认路径；该条保留为机制证据。
- **待办**：工具改动按硬约束 7 需 P3 凭证后方可提交（已暂存并启动 full 验收）。

### VDC-PROGRESS-20261014-012：独立 DPLL60 跑不了的机制＝**失败 P3 留下的临时凭证**使期望 build 退化

- **用户判断（已证实）**：与 OTA 无关；DPLL60 应当跑**当前板上最新固件**，不该依赖记录推断。
- **机制**（`tools/vdc_priority_trace/DPLL60_workflow.py:157-196` `expected_build()` 的真实优先级）：① bench config 的 `build_id` 优先；② 凭证存在时取其 `build_id`，**但 schema 为 `HAOFV_P3_JOINT_PROVISIONAL_V1` 的临时凭证被刻意忽略**（第 175 行）；③ 取 `out/**/ota-*/ota-workflow-summary.json` 中 `passed` 的最新一条，若其时间晚于凭证时间则用它；④ 否则退回凭证。
  ⇒ **一次失败 P3 会在 step 28 写下临时凭证后中止**（step 29 挂掉、step 35 未运行），该文件留在 `config/hardware_acceptance/p3_acceptance_receipt.json`；于是 `receipt_build=""`、`receipt_mtime=0`，**任何**旧 OTA 记录都"更新"⇒ 期望 build 变成旧值。
- **实测证据**：`hawf-111659` 失败后，独立 DPLL60 `--steps '2'` 报
  `Step 2 FAIL: online boards do not run expected build 20261013132342`，而四板实际为 `20261014031725`（失败 P3 自己构建并烧写的那个）⇒ 期望值来自旧 OTA 记录，与板上固件不符，step 2 直接拒绝、后续全部无从运行。
- **对照（为何 P3 能跑）**：P3 在 run 内部**自建自烧**，其期望 build 恒等于它刚烧的那个，不存在记录冲突 ⇒ **同一台台架上 P3 的 DPLL60 永远能跑，独立 DPLL60 会因为记录不一致而跑不了**。这不是台架硬件故障，也不需要 OTA。
- **恢复验证**：在**回退掉全部诊断固件改动**后重跑 P3 full（`hawf-112921`）**通过并写入凭证**（step 29 OK 104.7 s、`credential: written`；新凭证 schema `HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_TDMA_4NODE_QUICK_GRADED_V1`、`build_id=20261014032934`）⇒ 独立 DPLL60 `--steps '2'` 随即 `expected build: 20261014032934 (from receipt)` → **OK 4.9 s、`failed steps: none`**。
- **建议的工具侧修法（未实施）**：`expected_build()` 在凭证不可用（临时/缺失/不匹配）时**改为读取板上在线 build**（即"跑当前最新固件"），而不是退回到旧 OTA 记录；这样失败 P3 不会连带把独立 DPLL60 锁死。
- **另记（已排除）**：NO.1 的 recorder 曾卡在 `state 5`（`SYSTem:TDMA:RECord:STATus? → 5,1791948276,250000,14,0,0,1,2708,0`），`--cancel-unsaved-record` 清不掉、记录只剩 2708 字节（对照通过轮 9956 字节）；该形态在**加探针之前**（10:39）就已出现，故与本轮诊断固件无关；P3 重新烧写（重启）后该状态消失。

### VDC-PROGRESS-20261014-011：提交探针所需的 P3 未通过——卡在环启动交接，探针嫌疑未排除

- **背景**：为提交 `VDC-PROGRESS-20261014-007/010` 的诊断固件（20 段探针 + 只读命令 + harness 原语，共 5 个文件），按硬约束 7 付一次 P3 full（`hawf-111659`）。**结果 FAIL**：
  - `Step 26 warn: four-Node TDMA closed loop: RuntimeError: no complete board record available after failed startup`
  - `Step 29 FAIL: AcceptanceError: TDMA stopped handoff incomplete: the capture failed before a board handoff could be checked (stage=process-image, phase=TRN-03B): RuntimeError: no complete board record available after failed startup`
  - `credential: not eligible`（step 30 起未运行）⇒ **凭证未写入，固件不能提交**。
- **未能排除的嫌疑（据实记录）**：探针给 DPLL 相位每次调用追加约 **20 次 SysTick 读**（`vdc_dpll_probe_now()`），而该相位实测每次调用 51–56k、预算 39k（`-010`）。也就是说**探针给一个已超预算的相位加了少量开销**，可能加剧调度让位、使启动屏障/交接失败。**未做 A/B**（需回退探针 → 烧写 → 重跑 P3）故不能定论。
- **两侧证据（指向间歇而非确定性）**：仅 10 分钟前的 `DPLL60 --steps '0-2'` + `--step 3`（**同一份固件**）**成功产出板级记录**（summary 里 `dpll_schedule_gate` 已评估、无 `error`），而 P3 的 step 26/29 却报同一句启动失败；另 P3 在 **09:04 与 10:13 两次通过**（当时固件**不含**探针）。故当前只能判定为**间歇性启动/交接失败**，与探针的相关性未知。
- **已排除的一个常见原因**：P3 流程在 step 4 会 **OTA 刷写并重启**四板，故「跨轮残留冻结 recorder」这一已知成因不适用本轮的 step 26/29（该成因是 DPLL60 工作流才需要 `--cancel-unsaved-record` 的原因）。
- **下一步（未执行）**：① 先重跑一次 P3 看是否间歇（两次历史通过支持这一可能）；② 若再现，则做 A/B：回退探针 → 烧写 → 重跑 P3，以判定探针是否加剧启动失败；③ 两路都失败则转查环启动屏障本身（`startup_barrier` / 板级记录导出）。
- **未核验**：未重跑 P3；未做探针 A/B；未定位「no complete board record available after failed startup」的根因。

### VDC-PROGRESS-20261014-010：探针细分到步内——**没有单一热点**，相位每次调用确实超载

- **改动（诊断，未提交）**：探针由 15 段扩到 **20 段**（新增 5 段切 `sync_dpll_fb_step` 内部：`debug_continue` 检查、`role` 应用/挂起、`match + consume_cmd`、`tune + Domain 拍`、`ring evidence`），命令仍为 `SYSTem:SYNC:VDC:DPLL:PHASe:PROBe?`（现回 20 个 UInt32）。交叉编译全绿、相关 host 测试 64 passed；OTA 烧写后 `--steps '0-2'`（复位）→ `--step 3` 仍 **WARN / `realtime_gate=False`**（复现）。
- **读数（Core1 周期，最大保持）**：

| 段 | NO.1 | NO.2 | NO.3 | NO.4 |
|---|---|---|---|---|
| `sync_dpll_fb_step`（总额） | **55624** | 51007 | 51654 | 50912 |
| `..match+consume_cmd` | 24388 | 29713 | 28832 | 28963 |
| `..tune+domain_beat` | 26840 | 25830 | 26372 | 25187 |
| `..ring_evidence` | 36475 | 23466 | 22935 | 23980 |
| `vdc_priority_ingress`（独立子服务） | 16768 | 29444 | 30270 | 36669 |
| `age_quality` | 21570 | 20569 | 21276 | 20927 |
| `follow_health` | 14709 | 13601 | 13447 | 15265 |

- **结论（关键）**：**没有单一热点可修**。`sync_dpll_fb_step` 内部三段各自最坏 23–37k（三者是**不同调用**上的分段最大值，不是同一次调用之和），再加 `vdc_priority_ingress` 最坏 36.7k、`age_quality` ~21k、`follow_health` ~15k。相位声明预算 **39000**，而**单个子步骤就常达 20–37k** ⇒ 该相位**每次调用做的工作量本身超出预算**；因此：① 即便解决 GUARD 约束做再平衡（见 `VDC-PROGRESS-20261014-009`），单靠挪预算也不足以达标；② 降低任一子步骤也不足以把总额压回 39k。正解方向只能是**让该相位每次少做事**（把子步骤摊到多个周期/降低节拍），或改变相位划分本身。
- **未核验**：未改实时行为；未评估「子步骤摊到多周期」对 DPLL/VDC 闭环时序的影响；探针为最大值保持、不含分布。

### VDC-PROGRESS-20261014-009：①「直接调高 DPLL 预算」不可行——GUARD 是静态断言保护的 ingress 预留

- **相位链已逐一算清并与实测窗口自洽**（`config/project_config.h`）：`PRIORITY_RX_IRQ_CYCLES=2500`、`PRIORITY_RX_PHASE_CYCLES=2×IRQ=5000`、`PRIORITY_RX_CLOSE_CYCLES=3500`；各相位 end = start + wcet + CLOSE + 边距（1000，DPLL 另有 `DPLL_ENTRY_MARGIN_CYCLES=1000`，SYNC_TRIGGER 为 200）。据此算得 TDMA end **166500**、VDC end **218000**、DPLL end **261500**、CALIBRATION **285000**、SYNC_CAPTURE **298500**、REFMEM **332000**、MODEL **335000**、SYNC_TRIGGER **365500**、TRIGGER_MEASURE **367500**、GUARD **367500..375000（= 7500）**——与实测快照**逐项吻合**。
- **实测尝试（已回退）**：把 `PROJECT_CORE1_PHASE_DPLL_WCET_CYCLES` 基数由 34000 提到 40000（声明 39000→45000，把后续相位整体后移 6000）⇒ **编译期静态断言失败**：`app_realtime_profile.c:9-11` 要求 `PHASE_GUARD_END - PHASE_GUARD_START == 7500u`（"priority ingress cannot spend the existing GUARD reservation"），并连带 3 个 host 测试失败（`test_app_realtime_priority`、`test_app_realtime_profile`、`test_sync_io_workspace`）。**结论：GUARD 的 7500 周期是 priority ingress 的预留、不是空闲余量，任何相位预算都不能单独放大。** 已回退，构建恢复绿灯。
- **由此排除** `VDC-PROGRESS-20261014-008` 的选项 ① 「直接调预算」。仍可走的路径：
  1. **等额再平衡**（总量不变 ⇒ 链与断言不受影响）：把某相位多余预算转给 DPLL。天然捐赠者是 **VDC**——声明 42000（实测 47000）而本轮实测 `max_runtime_cycles` 仅 **11863**；但**缩小声明 WCET 是实时安全契约变更**，须先给出该相位最坏工况的证据，不能只凭单次台架读数。
  2. **降低 DPLL 相位的实际开销**（把重拍移出每次调用的路径，或降低其节拍）。
- **未核验**：未做再平衡（缺捐赠相位的最坏证据）；未改任何预算或行为；DPLL 峰值（约 100k，含 `sync_dpll_fb_step` 单段 46815–57480）仍远超任何可容纳的静态预算。

### VDC-PROGRESS-20261014-008：DPLL 相位预算是**声明值不足**，非软浮点、非单点回归

- **排除软浮点**：`components/vdc_domain/src/vdc_dpll_manager.c` 与 `vdc_domain.c` 内**没有任何 `double`/`float`**（链接脚本 wrap 的整套 `__aeabi_d*` 是项目标准清单，不是本路径的证据）。
- **排除「④ 拍是重活」的猜测**：`vdc_domain_service()` 本体极小（计数 + 锁态迁移 + `refresh_quality_age`），不可能承担 5 万周期。
- **预算算术（`config/project_config.h`）**：
  `PROJECT_CORE1_PHASE_VDC_WCET_CYCLES = 42000 + PRIORITY_RX_PHASE`、
  `PHASE_DPLL_WCET_CYCLES = 34000 + PRIORITY_RX_PHASE`、
  `PHASE_REFMEM_WCET_CYCLES = 24000 + PRIORITY_RX_PHASE`，
  其中 `PRIORITY_RX_PHASE_CYCLES = 2 × PRIORITY_RX_IRQ_CYCLES`、`PRIORITY_RX_IRQ_CYCLES = 2500`
  ⇒ 与实测相位 wcet **47000 / 39000 / 29000** 逐一对上。
- **结论（可执行）**：DPLL 相位**声明预算 39000**，而**实测常态（last）44714**（净额 39714 > 基数 34000，超 5714），**最大子段 `sync_dpll_fb_step` 单独即 46815–57480** ⇒ 该相位的声明 WCET **低于其实际最坏开销**，这是相位超预算、进而 VDC/REFMEM 丢期限与 `quarantined_mask` 粘性的直接原因；不是软浮点、也不是某个子服务的单点回归。
- **可改方向（未选）**：① 提高 `PROJECT_CORE1_PHASE_DPLL_WCET_CYCLES` 基数（需核 `PROJECT_CORE1_DPLL_ENTRY_MARGIN_CYCLES=1000` 与后续相位链在 375000 周期内是否吃得下）；② 降低该相位每次调用的工作量（重拍移出 0.5 ms 节拍）；③ 重新审视该相位的执行节拍与其声明窗口的关系。三项都必须重新付预算一致性证据，且属固件改动（P3）。
- **未核验**：未改任何预算或代码；未验证调整后 VDC/REFMEM 丢期限是否归零；探针为最大值保持，不含分布。

### VDC-PROGRESS-20261014-007：分段探针上线，DPLL 相位超预算归因到 `sync_dpll_fb_step` 单段

- **改动（诊断，未提交）**：按用户「断点调」指示，复用 2026-10-13 给 origin 路径加探针的同一手法，为 `sync_dpll_fb_service()` 的 **15 个子服务**各加一个分段最大周期保持（Core1 SysTick），并**新增只读命令** `SYSTem:SYNC:VDC:DPLL:PHASe:PROBe?`（15 个 UInt32，追加式新增，不改任何既有页面/线格式）。探针数组放**主 RAM**：放 `SCRATCH_Y` 会溢出 64 字节并与 Core0 栈重叠（scratch Y 已零余量）。host harness `test_vdc_idle_maintenance.py` 同步补探针原语（它抽取该函数体单独编译）。
- **验证**：交叉编译全绿（`-Wall -Wextra -Werror`）；相关 host 测试 **77 passed**（含此前因新符号而 error 的 idle-maintenance 25 例）。以 OTA 烧写四板后，按断点式 `--steps '0-2'`（复位）→ `--step 3` **复现** WARN 与 `realtime_gate=False`。
- **探针读数（四板，Core1 周期，最大值）**：

| 板 | `sync_dpll_fb_step` | `vdc_priority_ingress` | `age_quality` | `follow_health` | 其余 11 段合计 |
|---|---|---|---|---|---|
| NO.1 | **57480** | 13854 | 19663 | 15736 | ~35k |
| NO.2 | **51308** | 31300 | 22744 | 14720 | ~35k |
| NO.3 | **46815** | 31376 | 21310 | 15123 | ~60k |
| NO.4 | **51332** | 29260 | 20568 | 12827 | ~34k |

- **结论**：相位预算 **39000**，而 **`sync_dpll_fb_step` 单独一段即 46815–57480**（1.2–1.5 倍整预算）⇒ 相位超预算与随之而来的 VDC/REFMEM 丢期限，首要归因是这一步；次因为 `vdc_priority_ingress`（从板 ~30k）与 `age_quality`（~20k）。
- **该步的结构**（`vdc_dpll_manager.c:3155-3213`）：有界**四拍流水线**——① debug/role 应用（早退）；② `feedback_match_service` + `consume_follower_command`；③ `apply_pending_debug_servo_tune`（早退）；④ `s_vdc_domain_service_pending` → **`vdc_domain_service()` + `publish_runtime_snapshot_locked()`**；⑤ ring evidence（finalize/prepare/apply）。峰值来自**含 `vdc_domain_service()` 的第 ④ 拍**。
- **未核验**：未在第 ④ 拍内部再分段（需再加探针 + 烧写）；未判定该拍 47–57k 是真实回归还是该工况既有水平；探针为**最大值保持**，不反映分布。

### VDC-PROGRESS-20261014-006：DPLL 相位超预算已量化，落在 `sync_dpll_fb_service()` 的 15 个子服务上

- **接续** `VDC-PROGRESS-20261014-005`：致命项是 VDC（四板）与 REFMEM（三从板）丢期限，起因指向 DPLL 相位超预算。本条把该相位的内容与量级钉死。
- **相位窗口（NO.1，`capture_load_masks=91`、`cycle_cycles=375000`）**：vdc `166500..218000`（wcet 47000）、dpll `218000..261500`（wcet 39000）、refmem `298500..332000`（wcet 29000）。
- **DPLL 相位是常态超预算、非偶发**：`before.last_runtime_cycles=17450 / max=50147 / overrun=1106 / miss=82` → `after.last=44714 / max=101147 / overrun=3557 / miss=2367`。即**窗口末次运行 44714 已超 39000 预算 14%**，峰值 2.6×。
- **VDC 相位本身不超预算**（`max=11863 < 47000`、`overrun=0`），但 `skip_count/start_miss` 增加 1285、`deadline_miss` 增加 519 ⇒ 属**被前序相位挤掉服务**，与 DPLL 超预算的因果一致。调度器级 `schedule_miss_count 31449→36639`、`quarantined_mask=8`（bit3 粘性）。
- **该相位全部内容**（`application/src/app.c:1161-1166` → `sync_dpll_fb_service()`，`components/vdc_dpll_manager/src/vdc_dpll_manager.c:3237-3262`）为 **15 个子服务**：`priority_trace_service_core1`、`vdc_priority_match_core1`、`priority_trace_match_core1`、`priority_follow_prepare_core1`、`vdc_priority_ingress_core1`、反馈会话与快照发布、`vdc_dpll_manager_age_quality_core1`、`sync_dpll_fb_step`、`vdc_boundary_service_core1`、`priority_follow_apply_core1`、`priority_follow_health_service_core1`、`reference_discipline_service_core1`、`model_feedback_end_core1`、`priority_summary_service`、`priority_guard_service_core1`。
- **下一步的阻塞**：仓库**没有**可读的逐子服务耗时（`vdc_priority_trace.h` 只声明函数、无计时字段），故要定位是哪一个吃掉 39k 预算，必须先加**分段最大周期探针**（与 2026-10-13 给 origin 路径加 `origin_probe` 的同一手法：分段取最大值、追加在既有诊断页尾部、只读不改行为）。属固件改动，按硬约束 7 提交前需 P3 凭证。
- **未核验**：未加探针、未定位具体子服务；未判定 2.6× 是真实回归还是该工况下既有状态。

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


## 归档索引

| 文件 | ID 区间 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_13.md` | VDC-PROGRESS-20260919-029..VDC-PROGRESS-20260919-015 | 15 | 2026-10-14 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_12.md` | VDC-PROGRESS-20260919-014..VDC-PROGRESS-20260919-007 | 8 | 2026-10-14 |
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
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_14.md` | VDC-PROGRESS-20260919-034..VDC-PROGRESS-20260919-030 | 5 | 2026-10-15 |
