# TRIGGER T2 任务进展归档 10

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_10.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-19

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20261016-016` .. `T2-PROGRESS-20261016-008`（按新鲜度倒排，最新在前），共 9 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。

### T2-PROGRESS-20261016-016：`late` 归因落到"宣告提前量不足"——提前量 100 脉冲（≈35 ms）时三从板 `late` 归零

- **背景**：361 位置全程轮里从板残差**全部是** `late`（22/26/22，`expired/no_plan/conflict/backend` 全 0），而 `late`＝"承诺窗口内但动作时刻已过"。按约定口径，FREEZE 的存在就是为了给足提前量，因此这指向**宣告提前量本身太薄**，而不是从板负载。
- **实测（同固件、只改停机态安装参数，不重烧）**：新增 `tools/sync_trigger/t2_declare_lead_sweep.py` 扫 `threshold`（提前量 = (1000−threshold)‰ × 位置间隔）：

  | 提前量 | SWITCH_A | GATEWAY | SWITCH_B | 冻结重发 |
  |---|---|---|---|---|
  | 50 脉冲 ≈ 19 ms | 页面读取失败 | **47 executed / 2 late** | **47 / 2** | 1.88×/位置 |
  | 100 脉冲 ≈ 35 ms | **49/0** | **49/0** | **49/0** | 3.98×/位置 |
  | 200 脉冲 | 该轮四板**全无投递**（无效样本，扫描器已标注 void） | — | — | 4.0×/位置 |
  | 400 脉冲 ≈ 155 ms | **49/0** | **49/0** | **49/0** | 5.81×/位置 |

  结论：**把宣告提前量从 5% 提到 10%（≈35 ms），`late` 即归零**；冻结重发比随提前量单调上升（越早冻结、等待越久、重发越多），与"提前冻结换取动作余量"一致。
- **配套固件改动（本批，一次批次）**：① `T2:CONFigure` 新增**可选第 13 字段**＝承诺窗口（`expiry = target + window`），缺省仍为内置 grace ⇒ 窗口成为停机态安装参数，**不需重烧**；② 冻结受理处记录**提前量证据**：`freeze_lead_min_slices`／`freeze_lead_last_slices`／`freeze_lead_expired_count`（粗片序号）；③ `T2:ACTion?` 页尾 17 → **22**（追加组进度 5 项 + 提前量 5 项，只增不改）；④ 工装按 `T2_ACTION_TAIL_FIELDS`（默认 22）切片，**尾长与板上镜像不符时告警**（本轮踩过：工具先改 22、板上仍是 17，`win` 整体错位读出 `abandoned 1000`／`late 49`，该轮数据作废）。宿主运行时 + K-2 用例 37 passed。
- **失败归属口径（本批固化）**：`late`＝窗内但动作时刻已过（提前量/调度问题）；`expired`＝已超出承诺窗口（窗口问题）。两者即"提前量不足"与"窗内被吃掉"的分界，不再需要推断。
- **待办**：以 361 位置全程（threshold 900）确认 `late` 归零并读提前量字段 → 把"最小可用提前量"与本批窗口值写入静态配置 → 本批（`components/`+`middleware/`）跑一次 P3 取证后提交。










### T2-PROGRESS-20261016-015：361 位置全程验收通过（分发/冻结/接收/滚动四段）——T2 交付达成；序列解算与执行移交 SYNC 域

- **运行**：`tools/sync_trigger/t2_workflow.py --full`，同一固件（默认每角色 1 行安装，**未重烧**），361 位置全程（`T2SCAN ... positions=361`，step 20 用时 558 s）。**结果：流程 exit 0、`failed steps: none`、`T2 workflow: OK`**。
- **四段证据（判据口径：位置时间片能否正常分发、冻结、接收和滚动）**：
  | 段 | 证据 |
  |---|---|
  | 分发 | 源板 `tx_publish 64720`；三从板 `lane_accept 47753 / 46446 / 48627`，判定 `-> DISTRIBUTED`；`decode_reject 0` |
  | 冻结 | 三从板窗口 `frozen` 列均达 **361**；`FREEZE latched/taken` 各 921/914/925、`kept` 15/26/18 |
  | 接收 | 三从板 `rt recv 7`（窗口 3 槽 + 在途）、`drain_fail` 26/41/20、`hold_delivered` 549/691/528；无 `decode_reject` |
  | 滚动 | 三从板窗口 `entered/announced/frozen` 均 **361**，`executed` 339（SWITCH_A）/ 335（GATEWAY）/ 339（SWITCH_B），`abandoned` 22/26/22；动作侧 `armed 7 / completed 7 / failed 0 / uncertain 0`，`ACTION execution: 3 of 4 boards completed an action`（COUNTER/origin 不执行自身时间片，符合设计） |
- **已知余项（非本轮判据）**：22–26 例 `late`（≈6%），归因为布防点相对游标过晚；`late` 计数的来源与处置属 T2 侧后续调优项，另 `sched` 行显示从板 `deadline_miss` 量级较大（重载），与本轮"四段达成"判据无冲突。
- **边界落地（本批）**：位置内多组序列的解算与执行**移交 SYNC 域**，新目录 `docs/trigger/sequence/`（三件标准文件 `TRIGGER_SEQUENCE_{ARCHITECTURE,TODO,TASK_PROGRESS}.md`；序列是 TRIGGER 域业务对象，执行端能力仍由 SYNC_IO 域承担），任务 `TRIGGER-SEQ-API-001`、`TRIGGER-SEQ-EVIDENCE-001`、`TRIGGER-SEQ-TERM-001`、`TRIGGER-SEQ-HIL-001`，跨域前置 `SYNC-SCHED-001`（深度 ≥2 绝对 deadline 队列，见 `docs/sync/SYNC_IO_TODO.md`）。`T2-SEQ-001` 在本域 TODO 已转 `MOVED TO ACTION SIDE`。
- **T2 侧待清理（不影响四段验收）**：过渡期的逐组配速（`t2_deadline_plan_t.next_tick`、组游标、`complete_action` 同遍推进）在 SYNC 域接手序列后移除；移除后 T2 只剩"每位置一个时间片"。










### T2-PROGRESS-20261016-014：T2 验收口径回归四段（分发/冻结/接收/滚动）；位置内序列归动作侧，T2 侧配速为过渡实现

- **验收口径（用户 2026-10-16）**：`如果位置时间片能正常分发，冻结，接收和滚动，T2 的工作完成了`。即 T2 的交付 = ① 分发（源板发布、从板车道接收）② 冻结（FREEZE 落位与本地解算）③ 接收（从板收到并解码时间片）④ **滚动**（位置窗随执行推进、槽位回收、继续接纳新位置）。**位置内多组序列不是 T2 的交付**。
- **边界（同口径）**：T2 只分发时间片；动作侧拿到时间片后自行按约定展开序列；四板并行、只对时间片负责（板间无逐组握手）。已写入 `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`「动作解耦」。
- **当前固件与四段的关系**：序列未开启时（默认每角色 1 行）组游标恒 0、`next_tick` 恒 0、Core0 的 `group>0` 分支永不进入，故 T2 行为与序列落地前**逐位等价**；因此四段验收可在当前固件上直接进行。已启动全程 361 位置扫描（同一固件、不重烧）。
- **过渡实现标注**：T2 侧的逐组配速（`t2_deadline_plan_t.next_tick` + 组游标 + `complete_action` 同遍推进）在结构体注释与架构文档中均标注为**过渡**，动作侧具备序列能力后移除；真板实测（周期 30/40/50/60 ms）已证明"观测到完成再布防"在任何间隔下都停在每位置 1 组，该路径不作为长期方案。
- **保留在 T2 侧（属分发职责）**：本角色组序列的安装与可读（＝交给动作侧的"约定"）、时间参数的可配置安装（`config/trigger/t2_sequence_profile.json`、`t2_workflow.py --seq-*`、`tools/sync_trigger/t2_sequence_sweep.py`，改周期/码**不重烧**）、位置级执行证据与失败归因。










### T2-PROGRESS-20261016-013：序列时间参数改为**可配置安装参数**（不重烧即可扫周期），并用扫描器定位到"晋升组未被布防"

- 需求（用户）："时间参数做成可配置的，不用每次烧录进行改动"。**结论：时间参数本来就住在停机态安装里**——每组的偏移是它自己那行模板的 `delta_q`、动作值是 `argument`，`TEMPLate:INSTall` 在停机态安装，因此改周期/改码只需**重跑安装与采集**。
- **交付**：
  1. `config/trigger/t2_sequence_profile.json`：角色组数、`group_period_slices`、`group_offset_slices`、逐组码（按 `node-sequence` 分支 `trigger_reservation_build_plan()` 的 `output_code = (i//2) | (level<<2)`）、网分脉宽，并含 `profiles.single`（每角色 1 行=序列落地前行为）与 `profiles.reservation_8`（每位置 8 组）。
  2. `t2_workflow.py` 新增 `--sequence-profile/--seq-groups/--seq-period-slices/--seq-offset-slices/--seq-codes-a/--seq-codes-b/--seq-arg-g`；优先级 **命令行 > 环境变量 > profile > 内置默认**（内置默认仍是每角色 1 行，序列命令真实开关与极化线，不静默开启）。
  3. **`tools/sync_trigger/t2_sequence_sweep.py`**：同一固件下逐档重跑安装+采集（**不 build、不 OTA、不烧录**），逐档汇总 `groups exec/count`、槽打包字（`next`/`in_flight`/`done`）、窗口行与五类失败计数。
- **扫描结果（同一固件，4 档周期：30/40/50/60 ms/组）**：三从板**完全一致**地停在"每位置 1 组"（`groups 3/8`、`next=1`、`in_flight=0xF`、`done=0b1`）。**与周期无关**这一点排除了"组间隔不够/太晚"的解释（若只是迟到，放大周期应当改善），指向**晋升后的第 1 组根本没有被布防**。
- **观测到的台架波动**：同日有一次扫描四板全无投递（`lane_accept 0`、`win 0/…`），即台架/起链存在偶发不投递；扫描器取各槽"最大完成组数"样本，单次 0 投递会显示为 0/8，判读时需与 `POSITION distribution` 一起看。
- **下一步（需要一次固件批次，二选一或叠加）**：① **逐组归因计数**（晋升组为何未布防：无 `next_tick`／被判 `late`／后端拒绝／无计划）——最省，能终止猜测；② SYNC_IO 域 **深度 ≥2 绝对 deadline 队列**（`SYNC-SCHED-001`）——紧间隔的结构性前置。










### T2-PROGRESS-20261016-012：序列动作控制真板验证——第一次跑出"组 0 完成即停"，根因是描述符镜像不可靠，已改为从上一组计划推导

- 关联：`T2-SEQ-001`（本角色 `G_r` 行 = 序列）、`INTERFACE-T2ACTION-01`（页尾 12 → 17 字段）。
- **真板证据（序列首次上板，2026-10-16 12:36 扫描，短扫描 9 位置）**：
  - 挂载与页面字段**正确**：`groups exec/count` 分别为 SWITCH_A `3/8`、GATEWAY `3/2`、SWITCH_B `3/2`、COUNTER `0/0`（origin 不执行），槽打包字初值 `240`=0xF0（`next=0`、`in_flight=0xF` 无在飞、`done=0`）。
  - **停点**：三从板每个位置只跑完**第 0 组**（打包字 `497`=0x1F1 → `done=0b1`、`next=1`、`in_flight=0xF`），`executed 0`、五类失败计数全 0。`in_flight` 在所有采样点都是"无"，说明第 1 组**从未布防**（若布防后卡住，它会持续显示在飞）。
  - **诊断缺口**：改前页面只有窗口累计与失败计数，"布防了但序列不推进"与"从未布防"不可区分——故本批把组进度**追加**到 `T2:ACTion?` 尾部（只增不改，既有索引不动）。
- **根因**：第 `g>0` 组的 Core0 备计划走了 `snapshot->descriptors[index]` 镜像，而该镜像**只在 FREEZE 受理时仍能在预览环里找到同版本预览**才更新（`trigger_t2_runtime.c:813-820`）；找不到匹配时它保留**上一个占用者**的描述符（版本非零但基准位置是错的），据此算出的 `q` 落在游标之后，被 `q <= cursor` 静默跳过——于是永不发布、永不失败、永不推进。
- **修法**：`g>0` 组不再依赖镜像，改由 **Core0 自己上一组的计划**精确推导：`q_g = q_{g-1} + (delta_q_g − delta_q_{g-1})`，并把该位置的 `prediction_version`/`expiry_sync` 顺延；`s_deadline_mailbox` 只由 Core0 写，因此"上一组"就是同一位置的上一组（`previous.group + 1 == group` 兼作"该组是否已发布"的判据，因为组游标只在完成时前进）。挂载额外拒绝**非递增 `delta_q`** 的角色行（后一组不可能早于前一组，这是静态配置错误）。宿主定向 **39 passed**。
- **权威码表来源（用户指向 `node-sequence` 分支）**：`components/sync_trigger/inc/trigger_reservation_config.h` 的 `TRIGGER_RESERVATION_ITEM_COUNT = 8`（每位置 **8 项**），`trigger_reservation_build_plan()` 给出 `item[i] = {sp8t_state = i/2, polarization = (i&1)?V:H, output_code = (i/2) | (level << 2)}`；角色掩码 SP8T `0x03` / SP2T `0x04` / VNA `0x08`，安全码 `sp8t_safe_state | (sp2t_safe_level << 2)`。工具据此把"通道 `0,0,1,1,2,2,3,3` + 极化逐项 `h/v` 交替"作为默认序列码（`T2_SEQ_CODES_A/B` 仍可覆盖）。**待用户确认**：GATEWAY 是否同样每项一个脉冲（分支无 per-item VNA 字段，当前按 8 项安装）。
- **RAM**：本批首建触发 `region RAM overflowed by 376 bytes`（上一版已把 RAM 用满），已按"只在本模块内回收"处理：组行 32 B → 紧凑 8 B（含范围校验，越界即拒）、快照组状态 u32 → u8、事件明细缓冲 16 → 11（窗口累计仍权威且无损，短扫描 9 条回执用例仍满足）。改后 825/825 构建通过、`flash_link_contract=OK`。










### T2-PROGRESS-20261016-011：序列动作控制落地（主线 ①–③）——本角色 G_r 行即序列，末组完成才算 executed

- TODO task ID：`T2-SEQ-001`（本地展开）、`T2-EXEC-002`（执行端深度，跨域部分仍待 SYNC_IO）。
- **实现（固件，`components/sync_trigger`）**：
  1. `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` 由 `1` 改为 `TRIGGER_ACTION_TEMPLATE_CAPACITY`（=8，**不动静态表容量**），并把存储改为数组 `s_local_template[8]` + 角色级 `s_local_kind`/`s_local_span_max`；挂载按 `local_role` 保留**全部**本角色行（同角色各行 kind 必须一致，一个后端只服务一个角色）。
  2. **本角色行 = 位置内的组序列**：第 `g` 行就是第 `g` 组，携带自己的 `argument`（通道码/脉宽）与 `delta_q`（静态相对偏移），展开仍走既有 `trigger_action_template_expand()`（`action_q = target_q + delta_q_g`）。
  3. 计划（`t2_deadline_plan_t`）新增 `group`，因此"同一位置的下一个组"（prediction version 与 model token 都不变）能被 Core0 识别为新计划而不是旧计划；Core0 只备**当前组**，且**已布防的组不重解**（K-2「已接受不追改」）。
  4. 完成按 (位置,组) 记账：`action_group_done[]` 位图、`action_group_next[]` 游标、`action_group_in_flight[]` 在飞组；**只有最后一组完成**才 `action_completed_mask` + 窗口 `EXECUTED` + 出回执，中间组只推进游标。失败/取消清空该位置的组归属；选择循环忽略"属于已完成组的计划"（否则组完成后的同一遍会把位置误判 `LATE`——宿主用例先暴露、后修正）。
  5. `trigger_t2_runtime_start()` 的整结构 `memset` 之后重建组状态（否则挂载的组数被清零、Core0 不再求解——宿主 `receiver` 用例抓到这个）。
- **实现（工具，`tools/sync_trigger/t2_workflow.py`）**：新增 `sequence_rows()`/`template_rows()`，`TEMPLate:INSTall` 改发 `count = G_r` 行（两处安装点）；**默认仍是每角色 1 行**（与落地前一致），序列经环境变量显式开启（`T2_SEQ_GROUPS`/`T2_SEQ_PERIOD_SLICES`/`T2_SEQ_OFFSET_SLICES`/`T2_SEQ_CODES_A`/`T2_SEQ_CODES_B`/`T2_SEQ_ARG_G`）——**通道码必须由硬件方给出，凭空编造会把开关切到未验证的通道**，故不做默认。
- **宿主证据**：`tests/python/test_trigger_t2_runtime.py` 新增 `capacity`（同角色 2 行可挂载、组状态初值正确）与 `sequence`（组 0 完成后位置**未** executed、Core0 备组 1、组 1 布防执行、末组出回执且 `template_id` 为末组）；`test_trigger_t2_k2_recompute.py` 的 `t2_deadline_plan_current` 断言语义更新为 6 参（含组号）。定向全套 **113 passed**（T2 运行时/K-2/freeze/位置飞行/P0T 拓扑）。
- **未闭合**：① **执行端深度 2（两组滑动窗口）属 SYNC_IO 跨域**（`SYNC-SCHED-001`），本批为**深度 1 顺序推进**；② 台架验证待跑（真的逐组执行需在有码之后）；③ `trigger_action_template_table.h:10-12` 注释仍写"16 groups"，随下一批固件一并改；④ 契约登记：序列语义的冻结条目（如 `INTERFACE-T2SEQUENCE-01`）待台架验证通过后登记。










### T2-PROGRESS-20261016-010：动作序列容量口径更正——模板表 **8 已足够**（按角色 8/2/2），缺的是"按位置展开"

- TODO task ID：`T2-SEQ-001`、`T2-EXEC-002`、`T2-DEPLOY-003`。
- **用户裁定（2026-10-16）**：模板表**不用扩容**——各角色动作数本就不同：**SWITCH_A 8 个动作**（切换通道）、**SWITCH_B 2 个**（切换极化）、**GATEWAY 2 个**（接收脉冲、回复状态已完成）。`TRIGGER_ACTION_TEMPLATE_CAPACITY = 8` **已足够**（SWITCH_A 恰好用满）。此前文中的「每位置 16 组」是**模型源自身直连实测**（`total = 181×16`）的口径，不是 T2 的角色需求；`8→16` 的改法另已被实测证伪（DPLL `phase exceeds 1000 ns`）。
- **当前动作序列的实际编排（代码核实，2026-10-16）**：
  1. **装载**：`ACTion:INSTall` / `TEMPLate:INSTall` 写入动作模板表（`trigger_action_template_table_t`，容量 8），每行 `{template_id, generation, role, kind, argument, span_slices, delta_q}`；`trigger_action_template_table_validate()` 校验 id 唯一、role 合法、SWITCH 仅限 SWITCH_A/SWITCH_B 且 `argument ⊆ deployment.rows[role].output_mask`、GATEWAY 仅限 GATEWAY 角色，并核对整表 CRC（`trigger_action_template_table.h:46-70`）。
  2. **挂载**：`trigger_t2_runtime_configure()` 按**本板 `local_role`** 过滤出属于自己角色的行（`trigger_t2_runtime.c:1070-1089`）；当前 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY = 1` ⇒ **每板每角色只允许 1 行**，第 2 行即 `return false` 拒绝挂载（只有最后一行存入 `s_local_template`）。
  3. **位置→动作**：从板收到 PREVIEW/FREEZE 后本地解算 (coarse, fine)，按 `action_q = target_q + delta_q` 得本板动作时刻（`delta_q` = 部署期静态配置的规划距离延时，运行中不重算），再按 `s_local_template.kind` 布置 SWITCH 或 GATEWAY 截止（`trigger_t2_runtime.c:670-710`），Core0 解算 → Core1 PIO 提交。
  4. **结论**：现状是**「一个位置 = 一行模板 = 一个动作」**；"每位置多组序列"要补的是**运行时按位置展开本角色动作集**（`G_r ≤ 8`，两组滑动窗口 = `T2-EXEC-002`，执行端深度 2 属 SYNC_IO 跨域 `SYNC-SCHED-001`），**不是**加宽静态模板表。
- **本轮落地（文档）**：`docs/reports/distributed-trigger/DHRT100_T2_STATUS_REPORT_20261016.html` 的 F5 页改写为「容量已足够，缺的是按位置展开」（含按角色动作表、更正说明、两个前置的归属），并重渲染 PDF（8 页、无溢出/重叠，`out/report-update-20260919/` 留 PNG 与 layout.json）；`docs/trigger/t2/TRIGGER_T2_TODO.md` 的 B9、T2-SEQ-001、"每位置 16 组"相关计划条目、"模板/组"表与硬约束条目均按 8/2/2 更正（**保留**模型源实测证据行 `total = 2896 = 181×16` 不动，仅注明其口径归属）。
- **未闭合**：`components/sync_trigger/inc/trigger_action_template_table.h:10-12` 的注释仍写「a position carries 16 groups, so the installable program must hold at least that many rows」——与本次更正矛盾；因改它属**固件源**变更（会使当前 P3 凭证指纹失效），留到下一批固件改动时一并修正并在台架上重新取证。










### T2-PROGRESS-20261016-009：批次收口受阻——P3 五次尝试均未取得可接受凭证（step 29 内部 DPLL 采集：多板输出在 60 s 窗口内到期）；代码按门禁暂不提交，仅提交文档

- TODO task ID：`T2-DEPLOY-003`（批次收口）。**结论：本批固件改动目前无法取得 `check-staged` 接受的 P3 凭证，故代码保持未提交。**
- **尝试记录（本工作区指纹 `0f7b965e…`，1808 文件）**：
  | # | 命令 | 结果 |
  |---|---|---|
  | 1 | `p3_hardware_acceptance.py run --full` | FAIL @ TRN-03 四节点 process-image：调度门 `vdc_deadline_miss_count_grew`、`refmem_deadline_miss_count_grew`、`dpll_overrun_count_grew`、`dpll_max_runtime_exceeded_wcet`（`soak_validation`/`startup_barrier` 均通过） |
  | 2 | `run --full` | FAIL @ TRN-02 DATA repeat：每板 `schedule_miss_count 76k–89k`（`quarantined_mask 0`） |
  | 3 | `run`（AGENTS 规定档） | FAIL @ **step 29** 四板 DPLL-60 联合采集：子进程 `internal_health_passed=false` → `Capture exited 2` |
  | 4 | `run`（重试） | 同 #3、同一步 |
  | 5 | `hardware_acceptance_workflow.py --steps 0-7`（恢复）→ `run` | 同 #3、同一步 |
- **#3–#5 的判据（逐板 guard，来自 `capture/input-probe.json`）**：`positive` 模式要求 `guard.state==3 && reason_mask==0 && passed_mask==(1<<(s/60))-1`，且 `native` 要求 `freeze_reason==(8 if state==3 else 1)`：
  - #4（`p3-082822`）：NO1 `state 3/0/1 ✓`；**NO2 `state 4 / reason_mask 0xC00000 / output_reason 3` ✗**；NO3、NO4 ✓。
  - #5（`p3-084308`）：**NO1 `0x20000`、NO2 `0xC00000`、NO3 `0xC00000`（三块 ✗）**，仅 NO4 ✓。
  ⇒ 失败模式是**板上 VDC RUN 输出在 60 s 窗口内提前到期**（位 22|23 正是工具只允许 NO4 在 expiry 模式的 `OUTPUT_FLAGS`），属**基础件（VDC/sync_io 输出存活）**症状，随轮次在板间漂移；四板的内部评估本身（`schema_ok/interval_ok/crc_verified/coverage_complete/all_bins_have_success/success_gap_passed`、`observed_s≈60`、`success_count 6.7k–22k`）均通过。**与 T2 无关**：P3 不配置 T2（`configured==0`，T2 服务立即返回）。
- **门禁含义（为何代码未提交）**：`check-staged` 要求回执 schema ∈ `ACCEPTED_RECEIPT_SCHEMAS`（`p3_hardware_acceptance.py:1632`，不含 `HAOFV_P3_JOINT_PROVISIONAL_V1`）；失败轮只留下 step 28 的 **provisional** 回执（`joint_capture_only: true`），故 stage 固件范围文件必被 pre-commit 拒绝。**未使用 `--no-verify` 或任何绕过**。
- **本轮实际提交**：**仅文档**（本条目、TODO 现状、顶层刷新、轮转归档与两页 HTML）；固件/工具/测试改动保持在工作区待 P3。
- **下一轮建议**：① 先查 NO2/NO3（及 #5 的 NO1）**输出为何在窗口内到期**——VDC RUN 输出的 lease/duration/edges 配置与 `sync_io` OUT1 路径，属 `VDC-ISO-001`/挂载约束一侧，不是 T2 语义；② 若判定为台架物理/板状态问题，按仓库既有做法冷启动（断电/重插）后再跑一次规定档；③ 凭证到手后再 stage 代码提交（receipt 必须同批 stage，`check-staged` 从**索引**读回执）。










### T2-PROGRESS-20261016-008：**目标达成**——连续三轮 361 全程、三从板各消费并执行 354–360/361、负载均衡、步骤全绿；残差 1–3 位置/板且五类原因可读

- TODO task ID：`T2-DEPLOY-003`。三轮同固件（含 `-007` 的批量排空 + cursor 失效排空），只改引线：
  | 轮次 | 引线 | 日志 | SWITCH_A | GATEWAY | SWITCH_B | 三从 `lane_accept` | `hold_drop` | fail（late/…/backend） |
  |---|---|---|---|---|---|---|---|---|
  | 1 | 10% (`870`→实际 900 档) | `steps-20261016-015655` | 355 | 355 | 354 | 49507/48194/49497 | 3.3k–4.4k | 6/6/7 · backend ≤1 |
  | 2 | 13% (`870`) | `steps-20261016-020822` | **360** | **360** | **359** | 76076/73937/76083 | 5.1k–6.6k | 1/1/2 · 其余 0 |
  | 3 | 20% (`800`) | `steps-20261016-021933` | **360** | **358** | **360** | 111636/108584/111467 | 7.5k–9.6k | 1/1/1 · backend 2（GATEWAY） |
  源端三轮均 `361/361/361/358/0`（全程无损）；三轮 `failed steps: none`、`ACTION execution 3 of 4`、判定通过、退出码 0。
- **结论**：**"361 个位置正常发布 + 各从机接收、解码并执行序列动作" 成立**——每个位置都被发布、被三从板接收/解码/冻结（各 `361/361/361`），并被执行（354–360/361 ≈ 98–99.7%）；未执行的 1–3 个位置**全部有据**（`late`，GATEWAY 那 2 个为 `backend`），且负载在三块板间均衡、`hold_drop` 比修复前低 20–60×。
- **残余与余量旋钮（留给后续，不属本目标的"能否"问题）**：未执行位置的直接原因是**从板流水线时延 vs 宣告引线**（`late`）与个别后端拒绝（`backend`）；可控旋钮已就位并记档——`T2_SCAN_OVERRIDE_THRESHOLD_PERMILLE`（引线，13–20% 为宜）、`TRIGGER_T2_RUNTIME_RECEIVE_DRAIN_MAX`（每遍排空条数，当前 4）、`DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH`（有界环深度，当前 4）。
- **批次收口（不含在本目标内）**：一次 `--profile full` P3 凭证；新增/变更页尾字段（`action_fail` 五类 + `T2:ACTion?` 尾切片）、采样器改动（`T2_SAMPLE_TICKS`、逐板 cursor fault、停机态页面剔除）与首页刷新的契约登记 + C11；文档与代码分开提交。另记既存红 `test_trigger_grid_preview[configure_race]`（与本批无关）。










