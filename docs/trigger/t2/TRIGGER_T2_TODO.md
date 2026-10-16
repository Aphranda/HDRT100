# T2 预约触发实施待办

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TODO.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/trigger/t2/TRIGGER_T2_REVIEW.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-10-16

## 现状汇总与待办（2026-10-15，入口节）

> 这一节是给"下一个自行启动的窗口/代理"看的入口，**只记当前状态与按序待办**；过程、实测原文、历史口径一律在
> [Task Progress](TRIGGER_T2_TASK_PROGRESS.md)（最新 `T2-PROGRESS-20261015-001` 为自原 `T2-DEPLOY-003` 行下沉的长记录）与
> `docs/vdc/VDC_TASK_PROGRESS.md` 顶部 `VDC-PROGRESS-20261015-058..064`。与下方任务表冲突时，以本节为准。

**一句话现状（2026-10-16 02:3x 更新）**：**目标已达成**。**连续三轮** 361 全程（引线 10%/13%/20%）：源端每轮 `361/361/361/358/0`（全程无损），**三块从板各自进窗·宣告·冻结 361/361/361 并执行 354–360/361**（13% 与 20% 两轮为 358–360，即 99.2–99.7%），`failed steps: none`、`ACTION execution 3/4`、判定通过、退出码 0；三从 `lane_accept` 均衡（如 111636/108584/111467）、`hold_drop` 7.5k–9.6k（修复前 176k–214k）。未执行的 1–3 个位置**全部有据**（`fail late` 1–2，GATEWAY 一轮另有 `backend` 2），原因与余量旋钮均已记档（`T2_SCAN_OVERRIDE_THRESHOLD_PERMILLE` 引线、`TRIGGER_T2_RUNTIME_RECEIVE_DRAIN_MAX` 排空条数、`DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH` 环深度）。达成路径（每一环都有反证或台架对照）见 [Task Progress](TRIGGER_T2_TASK_PROGRESS.md) 的 `T2-PROGRESS-20261016-001..008`。**余下非功能性收口**：一次 `--profile full` P3 凭证 + 页尾/采样器/首页变更的契约登记与 C11 + 文档代码分开提交。

### 已完成（含证据）

- **Step 1 从板消费已落地并越界 3 槽**：提交 `87706fee`（含 P3 凭证）；台架 `out/tdma_t2/steps-20261015-154107.log` → `slot 2: window frozen 4 / executed 3 / rejected mark=0 alloc=0`（修复前同板 `frozen 0 / executed 0 / mark_rejected 34`）。**注意**：这是"同一 3 槽被回收重用"，不等于源端也已按窗口发布。
- **两条真根因（均已修）**：① `mark()` 禁止跳级，`accept_remote_position()` 里直接 `mark(FROZEN)` 每次被拒 ⇒ 先 `ANNOUNCED` 再 `FROZEN`；② `trigger_position_window_alloc()` 同实例重解路径保留旧 `state` ⇒ 重解复位 `ENTERED`。
- **读数可信化**：`T2:ACTion?` 尾追加 7 字段（`entered/announced/frozen/executed/abandoned/mark_rejected/alloc_rejected`），**位掩码不是计数**（`7` = 0b111 = 3 槽）。
- **契约登记**：`T2-DEPLOY-003` / `T2-IDENT-002` / `T2-TURNTABLE-002`（`docs/check/DOCS_REGISTRY.md:18-20`）；C11 交叉复核**待他人签署**。
- **基础件正常**：完整 `--profile full` → `credential: written`，`hawf-171753` 的 `dpll-short` `passed=true, errors=[]`。
- **扫描派生层**：`trigger_turntable_scan_validate()`、滚动窗口 `window_begin/enter/advance`、SCPI `T2:CONFigure` 12 参数、361 位置的 `T2:SCAN?` 自洽均已落地并有宿主/台架证据。

### 361 位置分发的阻塞项（2026-10-15 代码审查，按依赖顺序）

| 编号 | 阻塞 | 位置（代码） | 状态 |
|---|---|---|---|
| B1 | **（已定：FREEZE 携带 `target_ms_index`，见实施进展）位置的绝对身份不明**：从板窗口以 `descriptor.count_epoch` 作身份，而 361 个位置共用同一 epoch；PREVIEW 的 `target_ms_index` 随每次重解变化，不能作身份 | `trigger_t2_runtime.c` `accept_remote_position()`、`trigger_position_window_alloc()` | 需先定口径 |
| B2 | **源端只发布序号 < 3 的位置**：`s_pending_mask` 来自旧预测器（仅 START/ZERO/TERMINAL，`announced_mask` 循环 `i<3`）；窗口的 `t2_published_mask()` 丢弃 `absolute_index >= 3` | `trigger_t2_runtime.c:66-77,1012,1037`；`trigger_turntable_predictor.h:196` | 未做 |
| B3 | **Core0 预览准备只遍历 index 0..2**，且跳过条件含旧 `announced_mask`——对序号 ≥3 的位置永远不出预览 | `trigger_t2_runtime.c:1243-1262` | 未做 |
| B4 | **从板位掩码粘性、无清除点**：`frozen_mask`/`received_mask`/`action_completed_mask`/`action_failed_mask`/`emitted_position_mask` 只置位；槽位在窗口里回收后，同槽新位置仍被 `frozen_mask` 与 `ack_mask` 早退挡住，`s_deadline[]`/`freeze_*[]`/预览历史也不复位 | `trigger_t2_runtime.c:392,470-471,550,579,595-596` | 未做 |
| B5 | **版本 16 位会耗尽**：`TRIGGER_T2_PREDICTION_VERSION_MAX = UINT16_MAX`；若按槽单调递增（免去从板识别新序号），361 位置约每槽 ~120 个序号，需先量每序号的预览版本数；耗尽后该槽不再出预览 | `trigger_t2_runtime.h:26`、`trigger_t2_runtime.c:1331` | 需先实测量化 |
| B6 | **位置域三方口径**（`T2-IDENT-002`）：FREEZE 校验硬编码 `position_id <= 3`，改宏不带动 | `trigger_position_descriptor.h:193`、`trigger_position_slice.h:38`、`trigger_position_mailbox.h:74` | 待并入 B2~B4 同批 |
| B7 | **原判据「分发公平性」作废（2026-10-15 用户澄清；原观测保留作历史）**：车道对四板**广播同一批位置时间片**——一次飞行里各板收到预测时间片，**板子的序列动作依据就是冻结的时间片：一旦收到（冻结的）时间片，就按「规划距离」推出的延时动作**（延时是本板**静态配置**、部署期预先配好；动作时刻 = 冻结时间片 + 本板静态延时；**收到即执行，不再等第二次下发**）。业务预算见 `T2_MULTI_SYNC_POSITION_PLAN_500NS_V1`（快照 `out/archive/cold/misc/t2-multi-sync-plan-500ns-20260922/`：500 ns 网格 + switch settle/margin + trigger lead + VNA scan/feedback 窗口；其自述为 planning snapshot，非事实源）。⇒ **不存在「按板分发」「公平性」「逐板到位率」这类目标**；台架 `lane_accept` 三值离散只作**接收侧**（单深度邮箱 + 有界 hold 环）争用/背压的证据，缺位置的原因在 R1–R3 那条链路查——本条不再作为独立阻塞项 | 一等席车道 / `distributed_refmem` | 口径作废（证据并入 R1–R3） |
| B8 | **旧语义用例钉死**：`test_trigger_t2_runtime.py` 16 个用例固定三槽语义（粘性冻结位、每板一个动作…），运行时切换必须与其同批更新 | `tests/python/test_trigger_t2_runtime.py` | 随 B2~B4 |
| B9 | **每位置多组序列（按角色 8/2/2） / 执行端深度 2**：模板表容量 **8 已足够**（SWITCH_A 8 / SWITCH_B 2 / GATEWAY 2），**无需扩容**；`8→16` 的改法另已被实测证伪（DPLL `phase exceeds 1000 ns`）。缺的是运行时**按位置展开**本角色动作集（`TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` 当前 = 1，多一行即拒绝挂载）；执行端深度 2 属 SYNC_IO 跨域（`SYNC-SCHED-001`） | `trigger_action_template_table.h:13`、`trigger_t2_runtime.c:1070-1089`、`T2-EXEC-002` | 不阻塞"单组位置分发"，阻塞完整 T2 |
| B10 | **台架未回归绿**：最新一轮 `steps-20261015-173316.log` `failed steps: [10, 18, 19]`，另有 `PRIORity:RX?` 8.1 s 超时；既有红 `freeze_missing`、短扫描下从板 `failed` 掩码非零 | 见 `VDC-PROGRESS-20261015-064` | 待查（`freeze_missing` 与从板 failed 为非关键） |

### 实施进展（2026-10-15，宿主已通过，**未上板、未做 P3、未提交**）

- **B1 已定（用户确认）**：位置绝对身份 = 线上 `target_ms_index`，**FREEZE 帧携带它**；`position_id` 只作在途槽位编号（槽位 = 扫描序号 mod 3，线上 id = 槽位 + 1）。从板用「FREEZE 目标与槽内 PREVIEW 目标之差 ≤ `descriptor_window_sync`」判同一位置，超出则视为复用该槽的另一位置的残留 FREEZE（计 `freeze_mismatch_count`，不冻结不失败）。该容差是实现取值，**未经台架标定**。**2026-10-15 复核**：工作区已把该判定换成「离上一个占用者更近还是离槽内现有 PREVIEW 更近」（见下方 R1 行），本段的固定容差口径仅存于 HEAD。
- **跨域改动**：`trigger_position_freeze_latch_t` 增 `target_ms[]`，`put` 增参数（`distributed_refmem.c`、`distributed_refmem_position_ack.inc` 两处调用已同步）；该头文件属已登记契约 `ARCH-TRIGGERMAILBOX-01`，**登记行与 C11 复核待补**。
- **B2/B3/B4/B6 已改（`trigger_t2_runtime.c`）**：窗口驱动源端 FREEZE（`origin_announce`）、Core0 预览准备按窗口条目遍历并映射槽位、从板槽位完成/失败后 `recycle_slot` 清粘性位并设版本下限、失败位不再重解。
- **B8**：新增宿主用例 `follower_scan`、`follower_scan_busy`、`origin_scan`、`freeze_other_position`；`freeze_missing`（既有红）随「失败位不再重解」转绿；`test_t2_freeze_no_preview.py` 加槽位复用用例；`test_turntable_rolling_window.py` 修正「每次 advance 只滚 1 位」的旧期望。相关宿主集合 453 passed。
- **仍红且与本批无关**：`test_t2_configure_signature`（调用点行号钉死，stash 掉 `t2_workflow.py` 仍红）、`test_vdc_idle_maintenance` 两个 error；`test_trigger_grid_preview[configure_race]` 偶发。
- **B5 未量化**：每序号预览版本数需台架量（`T2_SCAN_OVERRIDE_TERMINAL_MDEG=8000`）；版本 16 位上限在 361 位置下可能耗尽，当前按槽单调递增。
- **台架实测（2026-10-15，9 位置短扫描 `T2_SCAN_OVERRIDE_TERMINAL_MDEG=8000`，`out/tdma_t2/steps-20261015-204426.log`，构建 `20261015123418` 已 OTA 四板）**：
  - ✓ 固件交叉编译与四板 OTA 通过；源端窗口 `entered/announced/frozen = 9/9/9`（此前停在 3）。
  - ✓ **SWITCH_A（slot 1）消费全部 9 个位置**：`win 9/9/9/9`，`mark_rejected=0 alloc_rejected=0`——越过 3 槽、槽位回收与新身份口径在真板上成立。
  - ✗ **GATEWAY / SWITCH_B 本轮只进窗 2 个 PREVIEW**：`announced 0 / frozen 0`，`freeze_mismatch=0`（不是目标校验拒收的）；`lane_reject 143/179`、`hold_drop 127/169`、`rx_pending_mask 1`。⇒ 这是**接收侧**（单深度邮箱 + 有界 hold 环）的争用，按 2026-10-15 澄清**不是分发问题**（四板收到的是同一批预测时间片），故并入 R1–R3 那条链路查，不再计作 B7 的独立判据。
  - ⚠ **新发现**：SWITCH_A 执行 9 次但 `CUM events 3 / dropped 6`——事件容量 `POSITION_COUNT × LOCAL_TEMPLATE_CAPACITY = 3`，扫描 >3 位置会丢回执事件（需随扫描放大或被及时 take）。
  - 未做：361 全扫描、完整 P3、B5 版本量化（本轮短扫描未触达 16 位上限）。
- **台架复测（同日，加 FREEZE 锁存计数后重烧，`out/tdma_t2/steps-20261015-212631.log`，构建 `20261015` 新包）**：`t2_iterate` 判据通过——三块目标板都执行且越过 3 槽：SWITCH_A `executed 7`、GATEWAY `7`、SWITCH_B `9`（源端 9 位置全部宣告冻结），`ACTION execution 3/4`，`failed steps: none`。
  - **上一轮 GATEWAY/SWITCH_B「饿死」没有复现**，`lane_accept` 由 `81/41/24` 变为 `526/524/531`。两轮之间只多了计数代码，故更像**接收侧的非确定性**（与旧记「每轮总有一块板拿不到新鲜片」一致），不能据此宣布结论，需多轮复验。**口径更正（2026-10-15 用户澄清）**：车道对四板**广播同一批预测时间片**，`lane_accept` 三值接近或离散都**不是判据**（该计数的差只说明接收侧争用与背压）；每块板在预测位置到达后按**本板预置的时间差**动作，故也没有「逐板到位率」这项指标。
  - **FREEZE 送达证据**（新计数 `latched/kept/taken`）：三从板 `17/0/15`、`17/0/17`、`16/0/16`——`kept=0`（无「未取走即被覆盖」的静默丢失）；SWITCH_A `latched 17 / taken 15` 差 2，且 `rx_pending_mask 1`，与其 `executed 7`（少 2 个位置）同向，**SWITCH_A 与 GATEWAY 各少 2 个位置的原因待查**。
  - **事件容量仍丢**：各板 `CUM events 3 dropped 4~6`（容量 3），扫描 >3 位置必丢回执事件。
  - **本轮台架已中断（2026-10-15 23:0x 只读复核）**：最近一次 `--steps 0-7` 运行 `out/HardwareAcceptance/20261015/hawf-221039` 停在 **step 4（刷写）**——`COM5` 的 `ota_send.stdout.txt` 尾为 `progress=852480,2081144`，最后一笔写入 22:15:27，此后无写入，且复核时**无 python 进程** ⇒ 该轮是中断而非"仍在跑"（需重跑，或 `python tools/hardware_acceptance/hardware_acceptance_workflow.py --run out/HardwareAcceptance/20261015/hawf-221039` 续跑；COM5 上那次 OTA 只发了一部分）。其 `workflow-state.json` 记 `build_id=20261015141052`（UTC，＝本地 22:10:52）、`fingerprint_before=31148c84…`、`source_file_count=1808`，与复核时 `working_source_fingerprint()` **逐字相同** ⇒ 该包由当前工作区构建、**已含工作区的 R1/R2/R4 改动**；`docs/**` 不在验收源码指纹（`p3_hardware_acceptance.SOURCE_ROOTS`）内，故文档更正不影响续跑。
- **下一步**：多轮复验分发是否稳定；查 SWITCH_A/GATEWAY 少执行 2 个位置；事件容量/外取；再 361 全扫描与一次完整 P3。

### 分发与执行的边界（用户澄清，2026-10-15）

一等席车道**只分发位置时间片**（位置编号、版本、`target_ms_index`、细分时间片、FREEZE）；SWITCH_A/SWITCH_B/GATEWAY 收到后**全部在本地解算**，并只展开**本板角色**的序列动作。因此：① 「361 位置发布并被各从机收到、解码」归车道与从板槽位逻辑（R1–R6、B7）；② 「各从机执行序列动作」归本地展开（`T2-SEQ-001` 每位置多组、`T2-EXEC-002` 执行端深度 2），**不是车道问题，不要再往车道加载荷**；③ 现状本地展开每位置仅 1 个动作（`LOCAL_TEMPLATE_CAPACITY = 1`）；**模板表容量 8 已足够**（按角色动作数 SWITCH_A 8 / SWITCH_B 2 / GATEWAY 2），要的是运行时**按位置展开**本角色动作集，而不是扩容（`8→16` 另已被实测证伪）。

**完整链路（2026-10-15 用户澄清，补全）**：主机**只发位置时间片 + FREEZE**；从机本地解算后**回「已冻结」**（`report_frozen()` → 出站镜像控制字节），主机按回执判定该位置是否闭环（`distributed_refmem_position_freeze_acked()`）；**板子的序列动作依据就是冻结的时间片**——收到该（冻结的）时间片后即按**规划距离**推出的延时在自己的时刻动作，**不再等第二次下发**。本板在部署期只取本角色那一行（`config->local_role` 筛选），按 `trigger_position_instance_expand()` 的 `action_q = target_q + delta_q` 得本板动作时刻（`delta_q` = 本板**静态配置的动作延时**，即规划距离对应的时间片偏移，部署期预先配好、STOP 期经 SCPI 装载，**运行中不再计算**，**仓内无硬编码数值**；例如 SWITCH_A/SWITCH_B/GATEWAY 各取自己的规划距离时间片。该延时的业务预算见 `T2_MULTI_SYNC_POSITION_PLAN_500NS_V1`，快照在 `out/archive/cold/misc/t2-multi-sync-plan-500ns-20260922/`；**一次飞行内四板共用同一批预测时间片**，各板只用自己的规划距离延时），到点由 `sync_io_*_realtime_schedule_at()` 触发。**语义与实现的分工（关键）**：语义上唯一依据是**冻结的时间片**；实现上 FREEZE 帧只带身份（`position_id`/版本/`target_ms_index`），冻结那一片的载荷由**被这次 FREEZE 绑定住的那片 PREVIEW**承载（`accept_remote_position()` 的 FREEZE 分支把槽内 `descriptors[index]` 回绑到与计划同版本的那片，Core0 再由 `preview_copy()` 解 deadline）——两者由 R1 的目标校验绑定为同一位置实例，所以 R1 的校验不是单纯的丢包问题：过紧丢合法 FREEZE，过松就用错片的基准展开。

### 序列与角色时间的定义（用户要求，2026-10-15；**草案，数值待用户定**）

**原则**：序列在 STOP 阶段一次定死并读回（随部署事务装载），运行中只按位置时间片 `q_P`（线上 `target_ms_index` + 细分片）展开；各角色的动作时刻 = `q_P` + 本角色相对偏移，**不由车道或主站逐板下发**。现有模板行已有 `role / kind / argument / span_slices / delta_q`，`delta_q`（单位：细分时间片）就是「角色相对位置时间片的时间」，缺的是把一个位置内的**本角色动作序列**定死。

**每位置序列（按角色动作组数：SWITCH_A 8 / SWITCH_B 2 / GATEWAY 2；各板只展开本角色的步）**：角色 `r` 的组 `g = 0..G_r−1`，第 `g` 组动作时刻
`q = q_P + O_r + g × P`，占用 `span_r` 个细分片。（早期文中的「K-6：16 组」为模型源口径，已按角色实际动作数更正。）

| 角色 | 动作 | 相对偏移 `O_r` 的含义 | 需要用户定的量 |
|---|---|---|---|
| SWITCH_A（NO2，SP8T） | 每组切到该组的通道编码 `code_A[g]` | 组起点（最先动，留出切换整定） | **8** 个动作/编码、整定时间 |
| SWITCH_B（NO4，SP2T/HV） | 每组切到 `code_B[g]` | 与 SWITCH_A 同组起点或其后固定间隔 | **2** 个动作/编码、与 A 的先后 |
| GATEWAY（NO3，VNA 网关） | 每组一个 TRIG 脉冲，READY 窗口做上一组重叠校验（K-1） | 晚于两个开关的整定完成点 | **2** 个动作（接收脉冲、回复状态已完成）、TRIG 脉宽、READY 窗口、`O_G` |
| COUNTER（NO1） | 不展开序列（只预测位置） | — | — |

> **容量口径（2026-10-16 更正）**：模板表按**角色**存放动作，各角色需求不同——SWITCH_A **8**、SWITCH_B **2**、GATEWAY **2**——因此 `TRIGGER_ACTION_TEMPLATE_CAPACITY = 8` **已经足够，不需要扩容**（SWITCH_A 恰好用满 8）。此前文中的「每位置 16 组」是模型源自身直连实测的口径，不是 T2 角色需求，已按角色实际动作数更正。

**组周期 `P`** ≥ 网分最坏响应时间 + 开关整定 + 余量（组间不由 READY 推进，K-1）；**位置内总跨度** `O_r + (G_r − 1)×P + span`（`G_r` = 本角色动作组数：SWITCH_A 8、SWITCH_B 2、GATEWAY 2）必须 < STEP 对应时间，`trigger_turntable_scan_validate()` 的 `SPAN_TOO_SHORT` 已按此拒绝。

**省内存的存法（不放大静态模板表，避开 DPLL 相位回归）**：每个角色只存一份**紧凑序列程序**——`{kind, base_offset O_r, period P, group_count G_r, span, code[G_r]}`（`G_r ≤ 8`，即不超过模板表容量），运行时按「两组滑动窗口」逐组展开成 deadline（即 `T2-EXEC-002` 的深度 2），不为每组占一个 `trigger_action_template_t` 行。`TRIGGER_ACTION_TEMPLATE_CAPACITY` 保持 8（**且 8 已足够**）。

**待用户给出的数值**（硬件事实，我不能替定）：① `P`；② 开关整定时间；③ 网分最坏响应时间与 TRIG 脉宽；④ READY 窗口；⑤ 各角色动作编码（SWITCH_A **8** 组 / SWITCH_B **2** 组 / GATEWAY **2** 组）；⑥ SWITCH_A 与 SWITCH_B 同时动还是错开。给出后：宿主先写序列展开与时间关系的测试（各角色时刻、跨度、STEP 容得下），再改固件。

### 代码审查发现（2026-10-15，审查本批 `trigger_t2_runtime.c` 等改动；只读，**未修复**）

| 编号 | 问题 | 后果 | 处置 |
|---|---|---|---|
| R1 | **FREEZE 目标校验容差过紧**：容差取 `descriptor_window_sync`，生产配置恒为 `TRIGGER_T2_DESCRIPTOR_TARGET_GRACE_SYNC`＝2（`scpi_trigger_t2_runtime_commands.inc:121`）；而源端 FREEZE 带的是**最新**解算目标，从板保存的可能是更早的 PREVIEW（中间版本丢失），两者目标漂移可轻易超过 2 个同步周期 | 合法 FREEZE 被当作「复用该槽的另一位置的残留」丢弃（只计 `freeze_mismatch_count`），该位置不冻结、不执行——**正是台架上 SWITCH_A/GATEWAY 少执行 2 个位置的候选根因之一**（`freeze_mismatch` 在 GATEWAY 读到 5、SWITCH_B 读到 2，与此吻合） | **工作区已有实现（未提交、未上台架）**：不再用固定容差，改判「这个 FREEZE 的目标离上一个占用者更近、还是离槽内现有 PREVIEW 更近」，只在更近上一个占用者时丢弃，并由版本 floor 兜底（`trigger_t2_runtime.c:577-597`，注释写明固定 2-sync 容差曾丢弃合法 FREEZE）；原建议（半个位置周期／版本单调性）已被该实现取代。台架复验待那一轮完成 |
| R2 | **从板「完成但未回收」的槽位仍上报 frozen**：回收只发生在**下一个更高版本 PREVIEW 被接收时**；完成后若无新 PREVIEW 到达，该槽 frozen 位一直上报 | 源端按槽位复用后清自己的 `freeze_complete` 位，却读到从板仍在上报该槽 frozen ⇒ **新位置被误判已确认**（假 ACK），FREEZE 不再重发，从板若漏收则该位置丢失 | **工作区已有实现（未提交、未上台架）**：`report_frozen()` 的上报掩码改为 `frozen & ~action_completed`（`trigger_t2_runtime.c:541-549`：源端复用时清自己的确认位，若从板仍报「已完成但未回收」的旧槽就会假确认新位置）；同一处的早退分支已区分「已冻结且仍在飞」（整条视为重复）与「已完成未回收」（只有比旧占用者更高的版本才开始下一位置，`:564-573`），动作完成时再次上报以撤下该位（`:731`）。原处置保留如左；注意既有用例 `freeze_missing` 仍钉住「失败的槽位仍报 frozen」，本次只排除完成位 |
| R3 | **槽位忙时到达的下一位置 PREVIEW 被整条丢弃**：从板对「已冻结且未完成」的槽位一律返回（视为重复）。下一位置的 PREVIEW 恰在上一位置宣告后不久发出，此时多半仍在飞 | 该 PREVIEW 不入保存，仅靠 Core0 后续「目标变化才重发」补发；一等席车道又有 `hold_drop`，补发不可靠 ⇒ 位置间歇性缺失（与 R1 叠加） | **工作区已实现（未提交、未上台架）**：忙槽时把「更高版本 **且目标不同**」的 PREVIEW 暂存为本槽的**下一占用者**（`s_stash`/`stash_successor()`），占用者完成或失败后**先接管再读传输**（`promote_stashed()`），于是随后到达的 FREEZE 重发能冻结后继，而不是被「已完成未回收」分支整条丢掉；同目标的重解不入暂存，避免同一位置二次动作。宿主新增 `follower_scan_successor` 直接复现该丢失路径（后继只在飞、之后**只有 FREEZE 重发**：修复后仍冻结并执行）；**禁用暂存后该用例转红**（反证已做）。原处置（暂存到下一占用者缓冲或源端有界重发）已被本实现取代 |
| R4 | **`s_threshold_public` 对扫描形同虚设**：`t2_published_mask()` 只含窗口内「已宣告」的条目，而 `window_advance()` 在同一次调用里把已宣告的最老条目滚出窗口 ⇒ 掩码几乎恒为 0 | Core0 在位置已宣告后、暂存前的二次检查失效，可能在 FREEZE 之后仍发出该位置的 PREVIEW；从板收到则按 R3 被忽略，或在回收后被当成下一位置的起点（带旧目标） | **工作区已按此实现（未提交、未上台架）**：新增按槽的已宣告序号原子量 `s_announced_ordinal`，Core0 两处检查改按序号比较（`trigger_t2_runtime.c:1400`、`:1498-1499`，写入在 `:434`）；`s_threshold_public` 本身仍由窗口掩码派生、仍几乎恒 0，但已不再是唯一闸门 |
| R5 | **Core0 重解可能重发已完成占用者的旧计划**：回收后位掩码被清，若此时模型 token 变化，Core0 会用槽内旧预览重算并发布旧版本计划，从板接收后 FREEZE 判「版本 ≤ 下限」而失败 | 个别位置被误判 `freeze_missing` | **工作区复核：从板侧已不再静默（未提交、未上台架）**：计划版本 `<= s_slot_floor[index]` 时计 `freeze_missing_local_count` 并 `action_fail`（`trigger_t2_runtime.c:623-628`），回收时把旧占用者的最高版本抬为该槽 floor（`:514-518`）；**Core0 侧仍未加版本下限**，故「重发旧占用者计划」仍可能发生，只是现在有计数与失败证据 | 回收时同步清 Core0 侧该槽的预览历史，或让 Core0 也尊重版本下限 |
| R6 | **从板未收到 FREEZE 的前一占用者不计放弃**：新 PREVIEW 直接覆盖，窗口 `abandoned` 不增 | 丢位置无证据（违背「丢失必须可见」） | **工作区已实现（未提交、未上台架）**：新增 `note_position_lost()`，两处计入——① 未冻结的已收占位者被**不同目标**的新 PREVIEW 覆盖（`admit_preview()`），② 已暂存的后继被另一目标的后继替换（`stash_successor()`）；同目标重解不计。宿主用例 `follower_overwrite_abandon` 覆盖两种计数与「同目标不计」。**掩码不加**：`freeze_abandoned_mask` 保持主机侧语义，从板只出计数（`trigger_t2_runtime_snapshot_t.freeze_abandoned_count` 已在页上，无页/ABI 改动） | 覆盖时若旧占用者未冻结，计一次放弃 |
| R7 | **事件容量 16 仍是静态上限**：361 位置必满（`dropped` 增长），明细不可作 361 的判据 | 仅诊断；判据须取窗口 `executed` | 已记；后续可外取或改环形 |

**对 361 位置的阻塞排序（2026-10-16 01:4x 更新）**：**① 引线 / 流量平衡**（唯一剩下的功能项）：引线必须覆盖从板流水线时延——`threshold_per_mille=950`（5%）时残差 100% 是 `LATE`（38/39/36）；**`800`（20%）时两轮里各有一轮三块板全部 `361/361/361/361`、零 abandoned**，但引线越长 PREVIEW 流量越大（`tx_publish` 50k→155k、`lane_dup` →179k、`hold_drop` →176k），触到**每板有界接收队列**上限：同参数复跑时 GATEWAY 被压垮（`0/0/0/0`、五类 fail 全 0、无锁存 ⇒ 非 T2 逻辑丢位置）。候选参数点：引线 850–900；源端 PREVIEW 去重/按位置只送最新；`DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH` 加深。**② 可重复性**：能力已证（存在三块同时零丢失的一轮），但需连续多轮同参数复现。**③ 批次收口**：一次 `--profile full` P3 凭证 + 页尾新增 5 字段（`action_fail` 拆分）与采样器的契约登记 + C11 + 文档/代码分开提交。④ B5 版本 16 位余量（取 361 全程 `freeze_version[]` 终值）。⑤ 既存红 `test_trigger_grid_preview[configure_race]`（`owner.c:129`，与本批无关）；step 8 在 slot 0 的瞬时 `result 33` 拒绝属既存 BUSY/NOT_IDLE 现象（另一轮全绿）。**已完成并闭合**：R1/R2/R3/R4/R6（含反证）、cursor 换代重基 BACKWARD+OVERFLOW（含反证）、cursor 瞬态不再取消/判失败（含用例改写）、`action_fail` 五类原因拆分入页、采样器停机态页面缺陷、采样窗覆盖、引线可调、C14 轮转。

### 设计方向（已按上节实现，保留作依据）

- **槽位 = 位置序号 mod `POSITION_COUNT`，线上 `position_id` = 槽位 + 1**（保持 1..3，不改 wire）；窗口深度 2 + 1 个"已宣告待确认"位 = 3 槽恰好够用。
- 新序号占用槽位时：旧占用者未完成则计一次放弃（`freeze_abandoned_count`），再复位该槽全部簿记（B4 清单）。
- 源端：`s_pending_mask`/`published_mask`/`s_threshold_public` 改由窗口条目派生；Core0 预览准备改为遍历窗口条目（序号 → 槽），用 `target_count` 判"新序号"以重置版本（或按 B5 结论改单调）。
- 从板：以动作完成/放弃为回收点，清粘性掩码与 `s_deadline[slot]`。
- 判据：`T2_SCAN_OVERRIDE_TERMINAL_MDEG=8000`（9 位置，>3）下，每块目标板累计 `executed` 覆盖全部位置、`lane drop` 不再单向增长、`ACTION execution` = 目标板数；再上 361 全扫描。

### 硬约束（勿再犯，均有实测依据）

- `TRIGGER_ACTION_TEMPLATE_CAPACITY` **保持 8**（改 16 致 DPLL 相位回归，A/B 已证；且按角色动作数 8/2/2 **8 已足够**，无需扩容）。
- **台架一次只允许一个消费者**；完整 P3 **不可中途打断**（会留下半途态，需 `--steps 0-7` 恢复）。
- 对照实验排除工作区改动用 `git stash --keep-index`；**不要**用 `git checkout HEAD -- <paths>`。
- 读数归属：窗口累计只在 **`T2:ACTion?`** 尾；从板真正写 `frozen_mask` 的是 `stage_pending_freeze()`；`T2?` 尾部字段随状态变化，勿取尾 N 项。
- **迭代纪律（用户裁决）**：不为每个小改动付 P3；P3 只覆盖 DPLL/联合采集，T2 功能判据必须由台架 T2 运行给出——先 `hardware_acceptance_workflow.py --steps 0-7`（构建 + OTA）再 `t2_workflow.py --full`；功能通过后整批跑一次 `--profile full` 取凭证并提交。
- 运行时切换**必须与 B8 的 16 处用例期望、页/契约同批**，否则不构成可验证状态；页尾追加纪律：不移动既有页索引。

### 按序待办

1. **P0 B1 口径定稿**（需用户确认）：位置绝对身份取什么（`target_ms_index` 需在 FREEZE 帧可得，还是以"槽位 + 序号 mod"配合源端一致性）；同时给出 B5 的版本数实测（一轮短扫描里每序号发了多少版本）。
2. **P0 B2+B3+B4+B6+B8 同批**：源端/Core0/从板按"设计方向"切到窗口；同批更新 16 处宿主用例；页尾追加诊断（不动既有索引）；宿主全绿。
3. **P0 台架判据**：`--steps 0-7` → `t2_iterate.py`（`T2_SCAN_OVERRIDE_TERMINAL_MDEG=8000`）→ `t2_workflow.py --full`。
4. **P0 台架复验 R1/R2/R3/R4/R6**（工作区均已改；R3/R6 已有宿主用例与「禁用即红」反证，待一轮完整 T2 判定；见下条中断取证与续跑命令）；**接收侧争用**（单深度邮箱 + 有界 hold 环：按 R1–R3 查缺位置，**不再按「分发公平性／逐板到位率」立判据**——四板收到同一批预测时间片，各板按本板静态配置的延时动作）；**B10 查 10/18/19 与命令超时**；**两条宿主红**（`test_t2_configure_signature` 的 `t2_workflow.py` 调用点行号需随批次更新；`test_trigger_grid_preview[configure_race]` 现为稳定红且其 owner harness 不编译 `trigger_t2_runtime.c`）随批次清掉。
5. **收口**：361 全扫描 → 一次 `--profile full` P3 → 契约登记（挂 `TDMA-SEAT-01`）+ C11 → 文档/代码分开提交。
6. **P1**：B9（按角色展开动作序列 8/2/2 + `T2-EXEC-002`）、Steps 4–6（未装填动作重算 / READY 按组校验 / 位置级完成证据）。
7. **P2**：C11 签署 `T2-DEPLOY-003`/`T2-IDENT-002`/`T2-TURNTABLE-002`；未提交工具改动（`t2_iterate.py` 真计数判据、`t2_workflow.py` step 17 分层与自诊断）随收口一次 P3。

### 本文整理说明（2026-10-15）

原 `T2-DEPLOY-003` 行内约 12 KB 的 10-15 过程记录已按 C12 下沉至 Task Progress `T2-PROGRESS-20261015-001`（逐字保留）；任务表内与本节冲突的"备用方案/容量 8→16"等旧口径以本节为准。后续新增过程性结论请写进度日志，本文只改状态与门禁。

**C14 轮转（2026-10-15 已完成）**：`TRIGGER_T2_TASK_PROGRESS.md` 曾达 **200,398 B**（检查器上限 200 KiB，余量约 4 KB）。已于同日把最旧一段条目轮转出去：归档 `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_01.md`（`T2-PROGRESS-20260926-016`..`T2-PROGRESS-20260922-001`，35 条，逐字保留），规范文件补 `## 归档索引` 并降到 **113,347 B**。切点是 **日期边界**（归档 ≤2026-09-26、保留 ≥2026-09-27），因此 C14 R2「归档最新 ≤ 保留最旧」按构造成立；日志尾部两段附录（`T2-AUDIT-03 前置：契约冻结清单`、`C14 R3 命名规则`）非条目，留在规范文件内、`## 归档索引` 置于全文末尾，避免检查器的章节扫描把附录表格当索引行。**注意**：此前本文件里"日志尾部是乱码、需先修编码"的记录是**误判**——那是 PowerShell 5.1 按 ANSI 代码页显示 UTF-8 造成的观感，文件本身为正常 UTF-8（已用 UTF-8 读取核对），该错误记录已删除。

## 文档接口与状态规则

本文是 T2 任务状态唯一落点；[Architecture](TRIGGER_T2_ARCHITECTURE.md) 维护稳定语义，[Task Progress](TRIGGER_T2_TASK_PROGRESS.md) 维护执行证据。状态仅用 `PENDING`、`IN PROGRESS`、`BLOCKED`、`DONE`；DONE 只关闭对应任务，不自动提升父任务、物理精度或产品资格。

之前完成的槽位、动作表和一次性 action ARM 是动作基础，不是当前主线。当前主线是同步 VDC 与 1 kHz 时间片构成准确时间系统：细分脉冲只验证能力，NO1 广播位置索引，从板本地解算，动作按时间片执行，READY 只做上一动作重叠校验。

## 已有基线与当前主线

已具备四板 deployment、COUNTER、SWITCH、GATEWAY 动态加载切片、STOP 网格规划、共享动作表安装和示波器细分诊断。这些不证明 TDMA RUN 中的 1 kHz cursor、位置描述符、从板解算或实际 T2 完成。

启动期资源主线是 deployment、PIO/SM/FIFO/GPIO、静态循环动作模板、grid/profile 一次性装载并读回；TDMA START 后只接收位置索引、标记本地时间片并执行。

## P0 最新载荷方案

以下方案优先级高于旧的完整位置描述符分片闭环。它仍保持 T2、VDC、DPLL 和 TDMA Foundation owner 分离；具体 wire subtype 和紧凑状态位在实现与独立复核后再登记为契约。上位语义口径 K-1～K-6 见 [架构 T2-SEMANTICS](TRIGGER_T2_ARCHITECTURE.md#t2-semantics已确认的语义口径2026-10-10)，与本节冲突时以该节为准。

1. **启动前配置**：VDC 启动前通过 `CONFIG_CONTROL` / `NODE_LOAD_STAGE` 完成一次 `BEGIN → DATA → ACK → COMMIT` 事务。事务可跨多个物理帧，但必须在 STOP→ARM 后才启动 VDC。配置包括 run/grid/count epoch、deployment/template/PIO/IO 绑定、位置到动作映射、source/target、阈值、有效窗口、nonce 和回退策略。
2. **运行时位置**：NO1 根据最新完成的计数窗口异步解算本地完整预测记录；TDMA 只发布一个 `position_q`，即共享网格中的绝对位置时间片。一个位置记录作为一等席负载**一帧发完**；一等席走独立一等邮箱、与特等轮流占用发布机会（K-5 落地方式，2026-10-11 确认）。完整预测依据、窗口和历史镜像留在本地，不进入 T2 实时载荷。
3. **阈值冻结**：达到可配置阈值后只发布 `position_id + prediction_version/FREEZE`，停止新的 `position_q` 发送。从板停止该位置的位置预测更新，采用本地最新有效版本并准备预装动作。FREEZE **只冻结位置预测，不冻结 DCO**：尚未装填的动作仍按最新 DCO 重新解算（K-2）。
4. **诊断回传**：冻结后用低优先级 T2 diagnostic class 回传一帧摘要；仅在版本缺失、旧代、CRC、q 冲突或 READY 异常时追加第二帧。诊断不覆盖 VDC、ACK 或已排队 VDC image。
5. **通用 FIFO 保留**：现有 `tdma_flight_fifo` 双槽 publish/acquire/release 保留给 TDMA 流式 OTA、配置数据和可靠控制。T2 不通过修改已排队 VDC image 来插入位置数据；旧完整描述符分片仅作为迁移材料和历史失败证据保留。

6. **角度扫描与多组序列**（2026-10-10 确认，2026-10-16 更正组数）：扫描由起始、终止和 STEP 定义，单位为度、分辨率 0.001°，每个 STEP 位置都执行完整触发（K-4）；每个位置运行多组序列与采样——组数**按角色**为 SWITCH_A 8 / SWITCH_B 2 / GATEWAY 2（原记「16 组」为模型源口径，已更正），本角色序列在位置开始前预装，执行端按包含两组的滑动窗口装填，DCO 更新放在网分采样期间（K-6）。多板模式下 READY 只做异步踩踏校验（K-1）。
7. **锁态只记录不阻断**（2026-10-14 用户裁定，取代原「VDC 进入 LOCKED 并发布后 T2 才有输出」的 K-3）：调试阶段 T2 消费 VDC 锁定判定并记入快照与证据，但**不因锁态拒绝、阻塞或跳过** deadline 的生成与装填；正式输出门禁延后为独立任务（`T2-GATE-001`）。

当前状态：方案为 `P0 / IN PROGRESS`，尚未冻结 compact-q wire subtype；完成定义是四板真实单 `position_q` 一帧送达、阈值冻结、从板本地准备、1～2 帧诊断回传，且 VDC cadence 和外部同步相位不变。现行代码与上述口径的偏差逐项见 [T2 评审](TRIGGER_T2_REVIEW.md)。

## 里程碑

首轮部署由用户确认（非硬件事实源）：NO1=COUNTER、NO2=SP8T、NO3=VNA 网分网关、NO4=SP2T/HV。板卡身份可动态切换，不能编译绑定板号。

| 阶段 | 可交付结果 | 进入下一阶段依据 |
|---|---|---|
| M0 静态部署 | 四板 deployment、PIO/IO、模板和 profile 在 TDMA START 前读回一致 | 配置、资源、模板、安全电平和恢复通过 |
| M1 1 kHz 时间片 | 每次 VDC 基准同步推进 pulse index/fine cursor | 四板 cursor、丢同步、epoch 换代和参考证据通过 |
| M2 预测位置 | NO1 按最新完成计数窗口异步更新预测，阈值跨越广播一次位置索引 | count epoch、prediction version、描述符和本地回退证据通过 |
| M3 从板执行 | 从板解析索引、本地展开每位置多组序列模板、按两组滑动窗口装填并执行 | 不依赖 host-at-deadline，四板不同 q 边沿顺序与位置内多组顺序通过 |
| M4 READY 校验 | READY 按组关联上一动作并检查时间重叠（多板只做异步踩踏校验） | 早到/迟到/缺失/重复/重叠及恢复通过 |
| M5 T2 闭环 | 实际动作、READY、转台位置和共同时间证据闭合 | position_id 级实际 latch/completion 和范围声明通过 |

## 分阶段任务

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| T2-DOC-001 | 三件套重构与语义导航 | IN PROGRESS | 三件套与审核版语义一致，历史证据未改写，文档门禁通过 |
| T2-DEPLOY-001 | 活动槽位与多板映射 | DONE | NO1 COUNTER、NO2 SWITCH_A、NO3 GATEWAY、NO4 SWITCH_B 已确认；UID/slot/IO 仍须现场读回 |
| T2-DEPLOY-002 | 启动前静态部署事务 | IN PROGRESS | deployment、PIO/SM/FIFO/GPIO、模板/profile 在 TDMA START 前统一装载、CRC/generation 读回；RUN 中禁止热加载 |
| T2-DEPLOY-003 | 多板槽位业务闭环 | IN PROGRESS | 四板真实运输和角色互换闭合；诊断 ACK 不冒充位置描述符或实际动作证据。 **2026-10-15 现状**：Step 1 从板消费已落地（`87706fee`）；分发缺口与 361 位置阻塞项见本文「现状汇总与待办」；角色动作实测、Step 1 实施清单、迭代纪律、批次边界、读数归属等长记录已下沉至 [Task Progress](TRIGGER_T2_TASK_PROGRESS.md) 的 `T2-PROGRESS-20261015-001`（C12）。 |
| T2-SLOT-COUNTER-001 | COUNTER 输入计数 | DONE | 四板 PIO0 计数、任意 IN、停止/清零/卸载和身份恢复通过；不等于位置预测或边沿 latch |
| T2-SLOT-GATEWAY-001 | GATEWAY 常驻 TRIG/READY 槽位 | IN PROGRESS | 模板启动前装载，TRIG/READY 同代读回，RUN 按本地时间片执行 |
| T2-SLOT-SWITCH-001 | SWITCH 常驻模板输出 | DONE | 本地编码和身份部署通过；维护 APPLY 不等于 T2 时间片动作 |
| T2-ACTION-TEMPLATE-001 | 静态循环动作模板 | IN PROGRESS | 模板库已实现、有主机测试并经台架用作 `T2:CONFigure` 前置（`T2-PROGRESS-20261009-021`）；剩余为每位置多组序列模板与启动期读回证据（评审 S-03） |
| T2-CLOCK-PROBE-001 | 细分脉冲能力验证 | DONE | 细分边沿、尾格空白、示波器 RAW 和恢复通过；不授予 live cursor |
| T2-CLOCK-001 | 1 kHz VDC 时间片 cursor | IN PROGRESS | 复用已验证本地 VDC/DCO/TIMER1 网格，修正 T2 消费入口；接收观察不更新时仍推进 coarse/fine q，轮询跨格不误判丢同步；身份/回退/回绕及硬件读回通过 |
| T2-CLOCK-002 | 四板共同 q 与同事件绑定 | IN PROGRESS | 沿用既有 typed SYNC/MATCH/FOLLOW/PHASE 配置和 committed DCO；四板共同 q 的本地解算、示波器相位与换代恢复通过，不以从板本地 PI LOCKED 代替实际模型，不新增 TDMA 预算 |
| T2-TURNTABLE-001 | NO1 计数 epoch 和位置观测 | IN PROGRESS | 任意 IN、起始/STEP 网格位置/终止位置预测、预运行区与终止校验区、脉冲/位置配置、预测目标计数、方向/回绕和采样区间通过；当前 5°/5° 是调试快照，非产品额定值 |
| T2-TURNTABLE-002 | 异步窗口预测与阈值提示 | IN PROGRESS | Core0 按最新完成计数窗口更新同一个预约位置时间片；接近阈值缩短计算间隔；阈值只广播 prediction version/index；从板使用本地最新有效镜像回退并记录差异；不触发热加载。**已落地预测器侧滚动窗口**（`components/sync_trigger/inc/trigger_turntable_predictor.h`：`window_begin`/`window_enter`/`window_advance`/`window_time_ready`，窗口 ≤2、进窗时用当时计数重算、速率不可用即 fail-closed；测试 `tests/python/test_turntable_rolling_window.py`）；**待办**：`trigger_t2_runtime.c` 由 legacy 3 槽迁移到窗口条目 |
| T2-P0-CONFIG-001 | VDC 前一次性 T2 配置事务 | PENDING | CONFIG_CONTROL/NODE_LOAD_STAGE 完成多帧配置、nonce/generation ACK、完整节点掩码、STOP→ARM 激活；VDC 启动后只读。现状仍为逐板 SCPI，`components/sync_trigger` 内无该两条命令引用（评审 S-03） |
| T2-TURNTABLE-003 | 本地预测与 compact position_q | IN PROGRESS | NO1 保留完整预测/诊断镜像；TDMA 只发布单 `position_q` 和显式位置身份；阈值只发布版本 index；从板拒绝旧代、错目标和过期状态 |
| T2-TRANSPORT-001 | 单 position_q TDMA 运输 | IN PROGRESS | Core1→Core0 TX 与 Core0→Core1 RX 固定单槽交接；T2 记录经优先级通道一等席接收，不覆盖 VDC 状态；验收读数以 priority accept/duplicate/reject 为准（优先级路径上 `position_rx_complete_count` 不再增长）；验证 q、身份、去重、期限、STOP 冻结、满载优先级和四板实际送达 |
| T2-P0-FREEZE-001 | 阈值冻结与本地动作准备 | IN PROGRESS | 阈值后停止 T2 位置发送，从板停止临界解算、冻结最新有效版本并装填预装动作；不形成当前动作许可门。FREEZE 冻结、逐镜像冻结掩码回报与 NO1 重发至全部目标确认已上板（`T2-PROGRESS-20261010-024`） |
| T2-P0-DIAG-001 | 冻结后 1～2 帧诊断回传 | PENDING | 正常一帧摘要，异常追加一帧；版本年龄、q 有效性、准备状态、READY/CRC/旧代原因可编码读回，不占 VDC 字段 |
| T2-TRANSPORT-OTA-001 | 通用 TDMA FIFO 保留 | PENDING | 双槽 publish/acquire/release 保持可用于流式 OTA、配置和可靠控制；与 T2 单 q 语义隔离 |
| T2-TURNTABLE-004 | 从板本地位置解算 | IN PROGRESS | 各板用自己的 VDC model/grid 将索引解算为时间片，不接收主站 deadline；每个 STEP 位置（含起始、零位格点与终止）必须生成确定的 target count、position ID 和时间片 |
| T2-EXEC-001 | 本地 cursor 时间片执行 | IN PROGRESS | 位置实例展开、各动作独立执行、不同 q 顺序、PIO 回执和示波器边沿通过 |
| T2-OFFSET-001 | SWITCH/GATEWAY/READY offset | PENDING | offset/window 可配置、读回、代际绑定；调试默认值不作为产品事实 |
| T2-READY-001 | READY 上一级重叠校验 | IN PROGRESS | READY 按 position/instance 关联上一动作，早到/迟到/缺失/重复/重叠通过；不作当前许可。现状仅主机模型（`trigger_ready_overlap.h`），固件无调用方（评审 S-03） |
| T2-TURNTABLE-005 | 阈值提示后的跨板位置确认 | PENDING | 目标板确认同一 position/version/grid/deployment 并建立本地实例；不形成 PREPARE/fence 执行门 |
| T2-TURNTABLE-006 | 多板本地动作顺序 | PENDING | 各板按不同 q/offset 自主执行，host 不逐板发即时命令，输出与 READY 分字段保存 |
| T2-TURNTABLE-007 | 预测更新、异常与恢复 | PENDING | 每次同步更新、阈值重复/跳过、速度变化、停止、回绕、方向、旧版本、分区和换代有明确事实 |
| T2-TURNTABLE-008 | 计数、同步和动作共存 | IN PROGRESS | TDMA RUN 中 COUNTER/VDC/template/executor 共存，隔离 OUT4 模拟源不占用实时域服务，不 STOP/START 打断或重置位置 |
| T2-TURNTABLE-009 | 起始/终止与 STEP 网格边界校验 | PENDING | 起始、各 STEP 网格位置（含零位格点）和终止均生成 position_id、预测目标计数和时间片；预运行区建立起始预测，终止后校验区验证终止预测和实际边沿，不新增或重排已执行动作 |
| T2-COMPLETE-001 | 实际动作/READY 证据闭合 | PENDING | 实际边沿、READY、模型和 position_id 绑定；缺 latch 只报告未确定 |
| T2-ACCEPT-001 | 当前切片交付 | PENDING | host/Release/P3/TDMA 短帧/示波器/恢复证据闭合，不宣布产品资格；从板送达读数以优先级通道 accept/duplicate/reject 为准，不以 `position_rx_complete_count` 判定 |
| T2-STEP-001 | 角度扫描与 STEP 位置 | IN PROGRESS | **行程 = [起始−5°, 终止+5°]**（2026-10-12 用户裁决）：必须从起始前 5° 起、终止后 5° 停——预行程使**位置 0 可被预测**（否则首个位置无速率/无观测），后行程使**终止位置可验证**；两段 roll 必须是整数脉冲且 ≥ 1 个 STEP，位置网格仍为 起始..终止 by STEP。起始/终止/STEP/转速/`pulses_per_degree` 以整数毫度配置（分辨率 0.001°）；整除、`step_pulses` 整数性、溢出、STEP 时间下限大于位置内序列跨度均在 STOP 校验并 fail-closed；lead 取一步比例（见架构「角度扫描与 STEP 位置」）；零位只作网格一致性检查，不在网格上即拒绝；内部 index 0 基、线上 position id 1 基；在途槽位数与身份容量归 `T2-IDENT-001`；可复用 `feature/node-sequence-reservation-trigger` 的 `CONFigure:ANGLe:*` 解析与校验（评审 A-14、D-1/D-5/D-6）。**已落地派生与校验层**：`trigger_turntable_scan_validate()` 派生位置数/整数 `step_pulses`/总脉冲，并按六类理由拒绝（参数缺失、反向、STEP 不整除、位置数超限、STEP 非整数脉冲、STEP 容不下序列跨度），拒绝时清零输出；0..360°@1° 规格（361 位置、step_pulses=100）与 `tests/python/test_turntable_scan_geometry.py` 全通过。**已完成（2026-10-13，台架验证）**：SCPI 前端已接出扫描参数并在配置期调用校验——`T2:CONFigure` 现为 12 个位置参数（`run_id, source_slot, target_mask, origin, pre_roll, post_roll, threshold, start_angle, zero_angle, terminal_angle, step_mdeg, pulses_per_degree`），处理器在构造 config 后调用 `trigger_turntable_scan_validate()`（六类拒绝理由 fail-closed），4 处台架调用点已同步；`t2` 验收 `passed=True / configured=4 / failed steps: none`。**剩余**：把台架扫描参数从 5/10/15° 切到 0..360°@1°（须先完成 A.3 迁移，见 A.3 顺序约束）；**不得往 `trigger_turntable_predictor_config_t` 加字段**（`-Wextra -Werror` 下 partial initializer 编译失败，STEP 现由 `trigger_turntable_window_configure()` 存入 snapshot） |
| T2-SEQ-001 | 每位置多组序列预装 | MOVED TO ACTION SIDE | **边界判定（2026-10-16 用户口径）**：`T2 只负责分发时间片，动作执行得到时间片之后自行按照约定进行`——位置内多组序列**不属于 T2 的交付**，T2 只交出"约定"（本板角色的组序列表：逐组 `argument` + `delta_q`）并保持可安装/可读。**T2 侧已落地**：本角色行按 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY`（= 静态表容量）挂载、紧凑存储（8 B/组，含范围与递增校验）、时间参数可配置安装（`config/trigger/t2_sequence_profile.json` + `t2_workflow.py --seq-*` + `t2_sequence_sweep.py`，改周期/码不重烧）。**动作侧待办**：取得时间片后自行按 `时间片 + (delta_q_g − delta_q_0)` 展开整条序列，需深度 ≥2 绝对 deadline 队列（`SYNC-SCHED-001`）。**T2 侧过渡实现**（逐组配速：`next_tick` + 组游标 + 同遍推进）在动作侧接手后移除；真板实测周期 30/40/50/60 ms 均停在每位置 1 组，证明该过渡路径不作为长期方案。 |
| T2-EXEC-002 | 两组滑动窗口执行端 | PENDING | SWITCH/GATEWAY 执行端支持深度 2 的绝对 deadline 队列，每组独立序号、回执与取消结果；一组完成即推入下一组；STOP 取消队列内全部未执行组并记录硬件取消结果；属 SYNC_IO 接口变更，需该域确认（评审 A-16）。**2026-10-14 核实**：现状确为单槽——`sync_io_switch.c` 以 `SYNC_IO_SWITCH_PENDING` 显式拒绝、`sync_io_gateway.c` 经 `prepare_realtime()` 的 pending 标志间接拒绝（失败路径另清 OUTPUT/READY FIFO），两侧各只有一份 `s_deadline`，头文件自述 “One transaction at a time, Core1 only.”；而 SYNC_IO 域此前**无对应任务项**，故本项长期无受理方。已在该域登记 **`SYNC-SCHED-001`** 作为本项跨域前置（`docs/sync/SYNC_IO_TODO.md`）。**2026-10-16 边界更新**：位置内序列归动作侧后，本项＝动作侧"取得时间片后自行走完序列"的能力，T2 侧不再承担逐组配速（`T2-SEQ-001` 已转 MOVED）。 |
| T2-DCO-001 | FREEZE 后 deadline 随 DCO 更新 | PENDING | FREEZE 只锁定位置；Core0 在模型 token 变化时为未装填组重新解算，更新放在网分采样期间；Core1 装填前比较 token；已被硬件接受的组不追改；依赖 VDC 证明模型切换无相位跳变（评审 A-06、E-01） |
| T2-GATE-001 | VDC 锁态**记录**（正式输出门禁延后） | PENDING | **2026-10-14 用户裁定**：调试阶段 T2 接受任何可达锁态，**只记录、不拒绝、不阻断**——能跑通、能成功即本阶段目标，规则细化阶段再收紧；K-3 已据此改写（见 [架构](TRIGGER_T2_ARCHITECTURE.md)）。**剩余**：① T2 消费 `vdc_dpll_manager_lock_verdict()`，把 `locked`/`provisional`/`published_qualified`/`reason` **记入快照与证据，但不参与任何准入判断**；② 正式输出门禁（含非发布态拒绝）另立**独立后续任务**，本项不再阻塞主线。**历史取证与更正全过程**（原 A-02 判据成立：生成/装填两侧都无锁态与发布检查，唯一相关判据是枚举越界 `lock_state > VDC_DOMAIN_LOCK_FAULT`；发布位 `REFMEM_VECTOR_FLAG_LOCKED` 在台架因 provisional **结构不可达**，实测四板 `flags` 恒为 `0x55`、bit5 恒 0；固件入口已于 2026-10-14 具备）见 `T2-PROGRESS-20261014-003/004/005`。 |
| T2-TRANSPORT-002 | 一等席单帧位置记录 | IN PROGRESS | 紧缩单帧已实现并经四板送达，改写已排队 VDC image 的 patch 路径已删除（`T2-PROGRESS-20261010-022/024`）；剩余为 VDC 核心字段清单、一等席布局/预算的独立登记（C11），以及按 `T2-IDENT-001` 新身份口径重新核定（评审 A-01、A-03、A-15）。**2026-10-14 落点**：一等席布局/预算登记挂 TDMA 域 2026-10-14 建立的 `TDMA-SEAT-01`（原引用的 `EXE-SEAT-01` 只有定义、无登记行，且宿主文档为 Draft，不能作 clause_loc）；VDC 核心字段清单仍归 VDC 域，当前**尚无该清单**。**2026-10-15 分发实测**：T2 运行时位置经**一等席车道**分发到三从并各自提交（`out/tdma_t2/steps-20261015-083111.log`：源 `tx_publish 22935`；从 `lane_accept 22860/11748/25481`、`owned 1`、`hold_delivered 461/281/704`、`drain_fail 1/0/0`），工具新增 `POSITION slot …` 明细与 `POSITION distribution … -> DISTRIBUTED/PARTIAL/NO DELIVERY` 判定。**读数纪律**：判定分发只能看车道路径计数 `lane_accept_count`（`distributed_refmem_position_ack.inc:287`，仅在解码+提交成功时自增）；`position_rx_complete_count` 属经典 mailbox 路径，而该路径在一等席拥有运输时被设计性跳过（`distributed_refmem.c:1893-1896`），正常分发下恒为 0——按它判读会得到相反的结论。 |
| T2-IDENT-001 | 位置身份与在途容量 | IN PROGRESS | 由扫描参数派生的位置数（0..360°@1° ⇒ 361）与紧凑记录身份容量一致。**2026-10-12 定稿**：T2 只预测下一到两个位置，长扫描的 361 个位置**不进入常驻状态**；常驻 1~2 个在途位置即可（`TRIGGER_T2_RUNTIME_POSITION_COUNT` 继续作窗口用）；线上绝对身份**无需加宽 4 bit id**——10 B 单帧已带 32 bit `target_ms_index`，配合扫描参数即可确定绝对位置，`position_id` 作在途实例编号；**预测超过 2 个位置一律视为不准确**（速率是单样本估计），window 外不产生任何对外事实；冻结确认 32 bit 掩码足够。剩余：与 TDMA 一等席布局一起核定并按 `EXE-SEAT-01` 登记（C11）。**2026-10-14 更正落点**：`EXE-SEAT-01` **有定义**（`docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 的「特等席增设须用户审核」节），但该文档 `Status: Draft`、**不可作 clause_loc**，自述也不新增 wire 契约，且在登记表中无行 ⇒ 「按 `EXE-SEAT-01` 登记」**仍无落点**。真实落点是 TDMA 域 2026-10-14 建立的 **`TDMA-SEAT-01`**（一等席容器与预算条款登记），本项与 `T2-TRANSPORT-002` 一并挂该条。 |
| T2-IDENT-002 | 位置域三方口径收敛（`T2-AUDIT-07` 的 C11 审核新产出） | PENDING | 同一「位置」域在代码内存在三处**独立口径**：① 线上／FREEZE latch 寻址域 `[1, 15]`——`trigger_position_slice.h:38` 的 `0x0Fu` 与 `trigger_position_mailbox.h:74` 的 `TRIGGER_POSITION_FREEZE_LATCH_IDS`，两者**无静态断言绑定**；② `trigger_position_descriptor_valid()` 的 FREEZE 分支硬编码 `position_id <= 3u`（`trigger_position_descriptor.h:193`，**未引用宏**）；③ T2 运行时消费域 `[1, TRIGGER_T2_RUNTIME_POSITION_COUNT]`（`= 3u`，`trigger_t2_runtime.h:12`；拒绝见 `trigger_t2_runtime.c:403-404`）。数值当前自洽，但**改宏不会带动 ② 的字面量**。判据：三方以符号或静态断言绑定（并确认 `[1, 3]` 是设计意图还是缺陷）；若结论落到固件改动，按 AGENTS.md 硬约束 7 付一次 P3 硬件验收凭证。依据与未核验范围见 `docs/check/submissions/TRIGGER_CROSS_REVIEW_02.md` |
| T2-SOURCE-001 | 模拟源转速/ppd 与脉宽 | PENDING | `pulses_per_degree` 进入 T2 配置并与模拟源共用；源以转速（mdeg/s）× ppd / 1000 换算频率并支持可配置脉宽；两条源路径选型定稿并给出隔离验收口径（评审 D-7/D-8/D-9） |
| T2-FREEZE-002 | 无预览位置判失败并继续 | IN PROGRESS | `stage_pending_freeze()` 遇 `preview_copy` 失败不再 `return true`：按位置记录一次（新增 `freeze_no_preview_mask` / `freeze_no_preview_count`，追加在 `T2?` 末尾可读回），并**继续检查其余 pending 位置**；pending 位保留，使迟到预览仍可冻结，窗口到期仍走既有 `freeze_abandoned_*` 证据。已实现 + 新增测试 `tests/python/test_t2_freeze_no_preview.py`（继续装填 / 全缺预览且每位置只计一次 / 健康基线不变 / 传输拒绝仍上报），与 `test_trigger_t2_freeze_abandon.py`、`test_t2_freeze_completion.py` 三件全通过；评审 A-13 的语义变更待独立复核确认后置 DONE |
| T2-DOC-002 | 补记 09-27 run33/34 证据 | PENDING | 定位凭证原件后按事实补入进展日志；禁止据文件名、推断或转述填写数字（评审 D-02/D-14） |

## 2026-10-12 定稿（长扫描口径，用户裁决）

本节取代此前零散追加的口径说明；与旧版"位置数与常驻容量同阶"的推导冲突时，以本节为准。架构侧对应 [T2-SEMANTICS K-7～K-12](TRIGGER_T2_ARCHITECTURE.md#2026-10-12-增补长扫描口径)。

### 1. 目标扫描与行程

- **目标扫描 = 0..360°，STEP = 1° ⇒ 361 个位置**（位置数 = ⌊(stop−start)/STEP⌋+1）。
- **行程 = [起始−5°, 终止+5°]**：预行程使**位置 0 可被预测**，后行程使**终止位置可验证**（K-9）。0..360°@1°、100 pulses/° 时：网格 36000 脉冲、行程 37000 脉冲。

### 2. 身份与容量（原 `T2-IDENT-001` 容量测算的结论）

| 维度 | 结论 |
|---|---|
| 常驻位置 | **1～2 个在途位置**（`TRIGGER_T2_RUNTIME_POSITION_COUNT` = 3 继续作窗口用，**不放大到 361**） |
| 线上身份 | **不加宽 4 bit `position_id`**：10 B 单帧已带 32 bit `target_ms_index`，配合扫描参数即可确定绝对位置；两级身份方案作废 |
| 模板/组 | 每位置按角色 **8/2/2 组** × 在途 1～2 ⇒ 常驻 ≤ 16 组（现每位置 1 组 ⇒ `LOCAL_TEMPLATE_CAPACITY` 需 1 → 本角色组数（≤8）；模板表容量 8 已足够，不扩容） |
| 冻结确认 | 32 bit 掩码足够（覆盖在途位置/槽位），无需 361 位分级确认 |
| 361 的用处 | 仅用于**扫描派生与 STOP 期 fail-closed 校验** |

（对照数据：4 bit id 只能表达 15 个位置，0..360° 在 4 bit 下的最小 STEP 是 25°；若将来要放弃 `target_ms_index` 绝对时间而改用纯 id 表达位置，则必须回到"加宽 id / 两级身份"方案。）

### 3. 滚动窗口规则

1. **只预测下一到两个位置**，窗口深度硬上限 **2**；`begin()` 不得枚举整段扫描。
2. **位置目标/阈值在进入窗口时计算，且目标是扫描绝对的**（K-8）；阈值 = 目标 − 一个 STEP 的 950/1000（K-10）。
3. **超出 2 个位置的预测不准确**：窗口外不产生任何对外事实。
4. **速率过期即 fail-closed**（`window_time_ready()` 为假 ⇒ 判该位置时间不可用并计数），不得用旧速率外推补数。
5. **迟到进入**（进入时计数已过目标）判定为**立即到期**（阈值夹紧到目标，保持 `threshold ≤ target`）并计数。

### 4. 实现状态与判据

| 能力 | 实现符号 | 测试 |
|---|---|---|
| 滚动窗口（进窗、宣告、滚动、速率就绪） | `trigger_turntable_window_begin/enter/advance/time_ready/configure` | `tests/python/test_turntable_rolling_window.py` |
| 扫描派生与 fail-closed 校验（六类拒绝 + rolls ≥ 1 STEP） | `trigger_turntable_scan_validate` | `tests/python/test_turntable_scan_geometry.py` |
| 产品入口（校验 + 建窗合一；拒绝即不建窗、清空全部窗口事实） | `trigger_turntable_window_begin_scan` | 同上 |
| 可读回健全性事实（**已接 `TRIGger:REServation:T2?` 末尾**，含 `window_step_mdeg` 与派生几何；台架判据 `announced == entered`、`overwrote_unannounced == 0`） | `window_entered_count` / `window_announced_count` / `window_overwrote_unannounced_count` / `window_late_entry_count` / `window_recomputed_count` / `window_rate_stale_count` | 滚动窗口测试 |

**仍在待办**：① SCPI 接线；② `trigger_t2_runtime.c` 消费者迁移（legacy 3 槽 → 窗口条目，并把附录 A 之外的健康计数读回）；③ 每位置多组（按角色 8/2/2）与执行端窗口深度 2；④ 边界位置执行侧证据闭合。代码级落点、编译约束与结案复盘见 **附录 A**。

本节的代码级落点（SCPI 命令面）、实施约束（`-Werror` partial initializer）与结案复盘见文末**附录 A**。

**仍在待办（原清单，保留引用）**：① SCPI 接线（见下节）；② `trigger_t2_runtime.c` 消费者迁移（legacy 3 槽 → 窗口条目，并把上表计数读回）；③ 每位置多组（按角色 8/2/2）与执行端窗口深度 2；④ 边界位置证据闭合。

## 实施顺序与停止条件

先定位置身份与在途容量（`T2-IDENT-001`）和 A-13 修法（`T2-FREEZE-002`）——长扫描的身份容量决定后面全部前端的工作量；再完成 P0 配置事务和一等席单帧位置运输（`T2-TRANSPORT-002`），随后阈值冻结、从板本地准备和 1～2 帧诊断回传；之后闭合 STEP 扫描（`T2-STEP-001`）、每位置多组序列与两组滑动窗口执行（`T2-SEQ-001`、`T2-EXEC-002`、`T2-DCO-001`）和 READY 重叠校验。正式输出受 `T2-GATE-001` 约束。通用 FIFO/流式 OTA 保留为独立后续负载，不作为 T2 单 q 的前置条件。

达到一个真实 position_id 的四板位置广播、从板解算、不同 q 动作边沿和 READY 重叠结果后，停止无关矩阵，保留原始证据并进入独立复核。

## 当前依赖
**更新（2026-10-13）**：T2 自身**已无开放的 AUDIT 项**——`T2-AUDIT-03` 与 `T2-AUDIT-07` 均已 DONE（登记 ✓ + C11 ✓ + TDMA 域联审 ✓）。`T2-AUDIT-03` 的联审条件分属 TDMA/arch 域（C1–C5，见该项下方指针，T2 不代管）；`T2-AUDIT-07` 的审核新产出（位置域三方口径不一致）已另立 `T2-IDENT-002`。原 `T2-AUDIT-08`（单串口控制面）属接口域，已移出。


| 项目 | 当前处置 | 阻塞范围 |
|---|---|---|
| 静态启动装载 | 现有槽位可在 STOP 下动态加载，统一启动事务未实现 | M0/M1 |
| 1 kHz cursor | 复用已有本地网格，修正 T2 接收观察入口；恢复完整 typed FOLLOW/PHASE 调试配置，定向验证本地 cursor 与 SYNC_TRIGGER 预算；见 T2-PROGRESS-20260927-019 | M1/M3 |
| T2 载荷 | 紧缩单帧（10 B RefMem 区）已实现并经四板送达；完整 descriptor 分片路径仅作为迁移/回退保留；冻结诊断 class 未实现 | P0 诊断 |
| 位置身份与容量 | 紧凑记录 id、FREEZE latch 与冻结确认位宽的容量均小于长扫描位置数；在途槽位数与 id 表达未定稿 | 长扫描（`T2-STEP-001`） |
| 模拟源与 ppd | `pulses_per_degree` 目前在 SCPI 前端写死；模拟源只收频率且占空比固定，无脉宽参数；两条源路径未选型 | STEP 上板验收 |
| 通用 FIFO/OTA | 双槽 flight FIFO 已具备基础 publish/acquire/release；保留给后续流式 OTA，不与 T2 单 q 共用业务语义 | 后续 OTA |
| 从板解算 | Core0 fine q→deadline 与 Core1 预装 PIO 提交已接线；本地模板容量见 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY`（每位置一组），位置数为编译期定长（`TRIGGER_T2_RUNTIME_POSITION_COUNT`），FREEZE 后不再随 DCO 重算；需扩展为每位置多组、两组滑动窗口和随 DCO 重算 | M3 |
| 执行端队列 | `sync_io_switch/gateway_realtime_schedule_at()` 为单槽，PENDING 时拒绝新 deadline；两组滑动窗口需 SYNC_IO 提供深度 2 队列 | M3 |
| VDC 发布 | T2 输出以 VDC LOCKED 并发布为前提；`VDC-LOCK-001`、`VDC-RUN-001` 为 PENDING。**消费方边界（用户，2026-10-14；HAOFV 审查 T5）**：T2 是基础件上的负载，只接收 VDC 按自身最优方式发布的事实——在固件内读取 RefMem 向量 `REFMEM_VECTOR_FLAG_LOCKED`（VDC owner 的 1 bit 声明，`VDC-LOCKPUB-002`）与全局锁定（`VDC-LOCKPUB-003`），VDC 未达正式质量时 fail closed；当前 step 17 在主机轮询 SCPI 判"四板锁定"，属过渡做法 | M3 及之后的正式输出 |
| 不配置基础件 | HAOFV 审查 T1/T4（2026-10-14）：step 15/16 由 T2 流程下发 VDC 的 `FEEDback:SESSion`、`PRIORity:SYNC/MATCH/FOLLow/FOLLow:PHASe`，属负载配置基础件；VDC 自管 arm（`VDC-LOCKPUB-001` ①）与一等席独立（`TDMA-ISO-001`）落地后移除。step 16 "运行中重下发"在环运行后被 STOP 门禁拒绝，不起作用；其注释所述"`SYNC_TRIGGER` 需 priority ingress 余量才能入周期"若成立即违反挂载约束 §4.3（不得借用他域余量），待对照 `app.c` 准入与实测核实。T2 负载下基础件退化见 `VDC-ISO-001` | T2 台架、挂载约束升格 |
| T2:STARt 命令语义 | 固件命令名沿用历史命名 `T2:STARt`，**实际语义是「挂载/武装」**（走停机态维护路径，环 `enabled`/`adapter_started` 后即被拒，`tdma_service.c:1033-1035/1079-1081`），不产生输出；**动态启动统一由 `LOAD:MASK` 位 6（`APP_REALTIME_LOAD_SYNC_TRIGGER`）承担**。把命令重命名为 `T2:MOUNT`（并与 `T2:STARt` 兼容或替换）待 T2 稳定后独立实施：需同步 SCPI 注册表、测试与工具，并按 AGENTS.md §7 付一次 P3 凭证 | 无（当前只影响命名清晰度；流程与文档已按三语义书写） |
| ORIGin:TRIAL 停环 | `CALibration:ORIGin:TRIAL` 间歇使参考环 `enabled`/`adapter_started` 归 0、config 前进 1（已与 `SOURce:STARt` 拆开确认由 TRIAL 单独触发，见 `T2-PROGRESS-20261012-001`）；游标随之按 `site RING` 粘性失效，该轮无发布 | T2 台架稳定性 |
| 从板相位起始迟到 | 从板 SYNC_TRIGGER 持续累计 `start_miss`（前序相位迟到继承，不隔离、游标计入 `missed_sync_count`）；需定位前序相位的迟到来源再判断是否影响动作时限。**2026-10-14 实测（`VDC-PROGRESS-20261014-033`）**：同固件 DPLL60 下 `start_miss/run` 9–15%、`deadline_miss` 1/545/494/606；T2 下 19–20%、`deadline_miss` 9/21754/22394/33307（四板一致），即负载在位时该相位的跳过与错过期限显著上升 | M3 动作时限 |
| 优先级读回超时被吞 | `tools/sync_trigger/t2_workflow.py` 的 step 16 收尾读 `SYSTem:VDC:PRIORity:FOLLow:STATus?` 时，活跃车道会以 8 s 超时回答；该行只在"读得到且未 armed"时记失败，超时被标成 "n/a (reference)"，于是**未 armed 也不会让 step 16 失败**（2026-10-14 两轮均在四板复现）。须改为：超时与不可读单独归类并计入失败证据，或改读在活跃车道可答的伴随页 | T2 门禁可信度 |
| 镜像辨识 | `PROJECT_BUILD_ID` 是 CMake configure 时的缓存值，增量构建不变；OTA 与验收凭证以构建号辨识镜像存在盲区，需改为每次构建唯一或以包 SHA256 绑定。**2026-10-14 处置**：`tools/ota/OTA_workflow.py` 构建时经 `tools/cmake_build_auto/cmake_build_auto.py --build-id` 传入新 id，实机已见四板 build 号随刷写改变（`VDC-LOCKPUB-002`）；其他构建入口仍按原口径 | 验收证据可信度 |
| 一等席布局 | T2 单帧记录需要 VDC 核心字段清单、TDMA 一等席布局/预算与新的身份/确认位宽，经 `EXE-SEAT-01` 用户审核后在登记表登记（C11） | P0 运输与长扫描 |
| READY 重叠 | GATEWAY 已有独立上升沿计数且脉冲完成后继续采集；缺少按 position 关联的物理时间戳和重叠窗口结论，计数不替代验收 | M4 |
| 实际 latch | 需要硬件时间戳和 position_id 绑定 | M5 |

## 统一完成定义

```text
TDMA START 前 CONFIG_CONTROL 配置并 STOP→ARM 激活
→ 1 kHz VDC cursor
→ NO1 按完成计数窗口异步预测
→ VDC LOCKED 并发布
→ 按起始/终止/STEP 逐位置预测
→ TDMA 一等席单帧 position_q 运输
→ 阈值 FREEZE，只冻结位置预测，停止位置发送
→ 从板采用最新有效版本，展开该位置全部组序列
→ 1～2 帧压缩诊断回传
→ 两组滑动窗口装填，未装填组随 DCO 更新，各动作按自己的时间片执行
→ READY 逐组对上一动作做异步踩踏校验
→ position_id 级实际证据和恢复闭合
```

配置读回、动作表 CRC、PIO receipt、READY 电平、单窗示波器或模型仿真只能证明各自范围，不能单独关闭完整 T2。

## 附录块地图（A-1，2026-10-13 机械生成）

附录 A 含**两套编号家族**，且**交错排布**（数字大小与文件位置无关）：章节家族 `A.1…A.5` 与按时间追加的日志家族 `A.3.x / A.4.x`。因此分流**按块**进行，不按编号。

章节家族（行区间为机械计算值，插入本图后整体后移）：

| 块 | 行区间 | 规模 | 标题 |
|---|---|---|---|
| `A.1` | 行 215–218 | 4 行 | 实施约束（实测教训：`-Wextra -Werror` partial initializer） | 
| `A.2` | 行 219–229 | 11 行 | 迁移注意：legacy 三槽的 lead 已不符合 K-10 | 
| `A.3` | 行 230–276 | 47 行 | 迁移清单：legacy 三槽在运行时的使用点 → 窗口对应物 | 
| `A.4` | 行 277–5024 | 4748 行 | 接线落点（SCPI，2026-10-12 勘察；**2026-10-12 更正**） | 
| `A.5` | 行 5025–5032 | 8 行 | 已结案：所谓"漏宣告 / mask 上报少一位"（2026-10-12） | 

日志家族 `A.3.x / A.4.x`：共 **154** 条，分布于行 **300 … 5003**，与上表各块交错（不得按区间整体删除或搬移）。

分流的单一去向（A-2 规则）：

| 块 | 去向 |
|---|---|
| `A.1` | 保留并并入现状约束（编译器 `-Wextra -Werror` partial initializer 教训） |
| `A.2` | 保留并并入 K 条偏差（legacy 三槽 lead 不符合 K-10） |
| `A.3 迁移清单` | 保留为迁移材料；完成后移入结论区 |
| `A.4 接线落点` | 保留为接线事实；其中已定案项（STEP 独立 setter）并入主表 |
| `A.3.x/A.4.x` 日志族 | **结论冻结**：结论进结论区，过程删除；跨界项改域依赖 |
| `A.5 已结案` | 移入结论区 |

### A-2a 判别规则

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。

> 本节长记录已按 C12 下沉至 `TRIGGER_T2_TASK_PROGRESS.md`（单一事实源）。

## 遗留待办（对照 HAOFV 逐级审查收尾，2026-10-13）

审查结论与证据见 [Task Progress](TRIGGER_T2_TASK_PROGRESS.md)；本节只记任务状态。状态用 `PENDING` / `IN PROGRESS` / `BLOCKED` / `DONE`。

| ID | 任务 | 状态 | 前置与判据 |
|---|---|---|---|
| T2-AUDIT-01 | 模型脉冲路径的**运行期 PIO 热加载**整改：登记为启动期资源，经 mode descriptor 声明 PIO/SM/DMA/GPIO，STOP/config ACK 阶段装载并读回 | ✅ 核验为已合规（见结论） | **前置已解决（2026-10-13 只读定位）**：模式声明机制已存在——ops 表类型 `sync_io_mode_ops_t`（`components/sync_io/inc/sync_io_mode.h:58`）带 **`.hw = { … }` hardware descriptor**（样例 `sync_io_mode_biss_tap.c:231`），中央派发表 `sync_io_mode.c:9-17/:27-35`，各模式均有 `*_mode_ops()` 访问器，公共 API 样板为 `sync_io_seq_step_arm/disarm/get_index/get_runtime`（`sync_io.h:304/:310/:311/:314`）。**实现形态**：新建 `sync_io_mode_<name>.c/.h` 声明 PIO 实例/SM/DMA/GPIO 并登记进派发表，`sync_io.h` 增 arm/disarm/runtime；**装载改在 STOP 阶段的 mode arm 内**，不再在模型 arm 路径内 `pio_add_program`；并补 HAOFV 要求的**资源冲突负测**与 DeploymentGate 的 PIO/SM/DMA/IO/IP claims；判据：连续 `START/STOP` 后可用程序槽位不减少且装载只在 STOP 阶段；需 P3 硬件验收凭证 |

**核验结论（2026-10-13）：该项已合规，无需改造**。
证据（均为本仓现状）：`sync_io_model_sched.c` 的 persona 判据在 `:931-934`（PIO0 且
`pulse_sm` 为 `BOARD_SYNC_PIO0_WAVE_OUTPUT_SM` 或 `BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM`）；
满足时走 `:955` 的 persona 路径：`:173` 的 `sync_io_wave_output_load` 在**停态装载**（claim + `pio_add_program`），
`:411` 注册为 `.load` 钩子，而 `:213` 的 arm 路径**只校验** `s_wave_output_program_loaded`。
模型/转台通道正在该判据内（SM2），故 **arm 期不再发生运行时热装载**。
仅当 `pulse_pio` 非 PIO0 或 SM 不属于上述两者时才会到达 `:980-1002` 的**非 PIO0 回退**（例如 `BOARD_SYNC_PIO_WAVE` 一路），该回退不在 T2 转台路径上。

| T2-AUDIT-02 | 每位置一组 → **每位置 16 组 + 两组滑动窗口**（审查 A-06/A-16） | `DONE` | 硬件闭环：`total = 2896 = 181 × 16`、`fault = 0`；残余：FREEZE 后按最新 DCO **重算未装填组**（K-2）尚未实现；**行级定位已完成（2026-10-13，只读核实）**：“按最新 DCO 重算”的**机制已存在**（`trigger_t2_runtime.c:1054-1055`：`prediction_version` 与 `model_token` 均未变才 `continue`，否则用**当前 `model.dco`** 重算），但被 **FREEZE 门抑制**：`:1048` 对已冻结位置**整位 `continue`**（不再重算）、`:1082` 对已冻结位置**不发布**，因此架构原文的“FREEZE 后停止重算（A-06/A-16）”**属实**，与 K-2（FREEZE 只冻结位置预测、不冻结 DCO）**直接冲突**。**修正（2026-10-13，ROI 复核）**：上述“仅两行”属**过度简化**，实际改动面**更大**：① `:1048` 改为“仅停位置预测/新实例、仍对**未被硬件接受**的动作重算”，需区分**已接受 / 已装填 / 尚未装填**；② `:1082` 要“允许发布尚未接受的新解算、且**不改写已接受**”，而当前代码**并无**“已接受”状态信号（Core1 负责 `service_deadlines`/回执，Core0 只能看到邮箱旧字）⇒ 需新增**Core1→Core0 的“已装填/已接受”回信或等价判据**，否则无法同时满足 K-2 两半（未装填重算 ✓ + 已接受不追改 ✓）。**第二次修正（2026-10-13 只读核实，本轮）**：上述“需新增 Core1→Core0 已接受回信”属**过度复杂化**：快照已携带可判据的掩码——`action_armed_mask`（**仅准入**）、`action_completed_mask`（**需匹配 PIO 回执**）、`emitted_position_mask`、`action_deadline_tick[]`（见 `trigger_t2_runtime.h:87-94`，原注释：“ARMED is admission only. COMPLETE/emitted require the matching PIO receipt”），Core1 侧另有 `s_armed_index`（`:116/:677`）与 `s_deadline_seen`（`:109`）。因此 `:1048`/`:1082` 可**直接改用这些现有掩码判据**（仅跳过已被接受/已完成位），**无需新机制**；**判据已定义（2026-10-13）**：代码自述——`trigger_t2_runtime.c:677-680` 在记录 `action_armed_mask` 的**同一时刻**写入 `action_deadline_tick[selected]`，并注释“**Read the owner's accepted sequence; a later mismatch cannot complete us**”，随后 `:683-691` 读取 owner 的 accepted sequence；而 `:524-525` 表明**完成/发射**才置 `action_completed_mask`/`emitted_position_mask`（需匹配 PIO 回执）。因此：**“已被硬件接受” = `action_armed_mask`**；**“已完成” = `action_completed_mask`/`emitted_position_mask`**。据此 `:1048` 应改为 `if ((s_state.action_armed_mask | s_state.action_completed_mask) & (1u << index)) continue;`（即**仅跳过 deadline 已被接受的位**，**已冻结但尚未装填的位仍重算**）；`:1082` 同步去掉“已冻结即不发布”的朡断（由 `:1048` 保证已接受者不再进入）。原因：因此 ② 回到“**小改动 + P3**”估时，但时长估计需按“**小型设计 + 实现 + P3**”计（而非两行修改）。：① `:1048` 改为仅停“位置预测/新实例”、**仍对未被硬件接受的动作重算**；② `:1082` 改为**允许发布尚未被接受的新解算**，**已被硬件接受的 deadline 绝不改写**（需“已接受”标记）。**落点已查明（2026-10-13）**：`components/model_turntable/` 内无 `freeze`/`DCO`/`token` 符号，`model_turntable_recompute_plan()`（`model_turntable.c:111`）仅在**配置/启动**路径被调用（`:263/:297/:312/:323`），**运行期不重算**；因此 K-2 应在**上层装填路径**实现（`trigger_t2_runtime` 的 `prepare_deadlines_core0()` / `service_deadlines()`，即架构 `T2-OWNER` 所指入口），**不在模型层动刀**；判据：装填前比较模型 token，不一致则用新解算，**已被硬件接受的 deadline 不得追改**，赶不上则判迟并记录 |
| T2-AUDIT-03 | 紧凑记录的**跨域契约登记**（`docs/check/DOCS_REGISTRY.md`）+ **C11 独立交叉审核** + 与 **TDMA 一等席布局联审** | `DONE` | 原阻塞条件（登记前不得写成冻结契约）已由 2026-10-13 的三项完成解除；TDMA 域联审结论为**有条件接受**，其条件分属 TDMA/arch 域（见下方指针，不计为 T2 待办项），故 `ARCH-TRIGGERSLICE-01` 保持 `pending` |

补充（2026-10-13，**T2-AUDIT-03 结案**）：三项均已完成——

1. **跨域契约登记**：`ARCH-TRIGGERSLICE-01`（status `pending`）；
2. **C11 独立交叉审核**：结论 APPROVE WITH NOTES，三处措辞/归属缺陷已修正；提交单 `docs/check/submissions/TRIGGER_CROSS_REVIEW_01.md`；
3. **TDMA 域一等席布局联审**（核验方为**独立 TDMA 域侧 subagent**，未参与任一侧起草）：结论 **有条件接受（`PASS_WITH_NOTE`）**——10 B 单帧的布局/越界/静态断言与轮换、「机会不空过」机制**均成立**，`SYNC_TRIGGER` 相位预算（相位 7、WCET 26800、窗口 335000..365500、每相位 11 字段）获**独立重算确认**；但须由 TDMA 侧补/改条款后方可作为冻结契约。提交单 `docs/check/submissions/TRIGGER_CROSS_REVIEW_03.md`。

**本次联审的 T2 侧已办事项（据实修正本域登记行）**：

1. 错别字兼**悬空引用**：`REPMEM-LAYOUT-01` → **`REFMEM-LAYOUT-01`**（原写法在登记表中不存在，所引域文档章节为 `REFMEM_DOMAIN_ARCHITECTURE.md:982`）；
2. 归属错误：「前 8 B 为 **core1** 元数据」→ 实为 mailbox **fast header**，由 **Core0** 镜像构建写入（`distributed_refmem.c:984-990`），Core1 仅上车前复验；
3. 偏移歧义：「offset 14」→ 明确为 **slot 绝对偏移 14**（24 B body 内相对偏移 6）；
4. 版本耦合强度收紧：由「尺寸随布局版本耦合」改为**符号级**耦合，并写明 `TDMA_PROCESS_IMAGE_LAYOUT_VERSION` 自引入未递增、混版本保护实际靠 **class 白名单拒帧**（普通 transport 白名单不含 0x15）而非版本字段。

**未闭合条件（跨域，仅指针，不计为 T2 待办项）**：TDMA 侧 **C1**（新登记 `TDMA-SEAT-01` 一等席容器与预算条款）、**C2**（`TDMA-PROCESSIMAGE-01` ver 1→2，补 RefMem 子区按 class 复用与版本递增规则）、**C3**（`SHARE_MAX == WAIT_MAX == 8` 使合规 8:1 按设计置位 `SEAT:HEALth?` bit3，须择一修正）、**C4**（TDMA 不声明时间期限，待 T2 冻结正式时限后复核）；arch 侧 **C5**（`ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md:57` 引用**已删除**的符号 `TDMA_SEAT_ROTATION_*_SHARE` 且称「编译期常量」，属硬约束 2 违规；另有 `:27`/`:135`/`:151`/`:163` 的已作废模型描述）。上述归各域 TODO，T2 不代管。

**零命中（治理缺口，据实记录；2026-10-14 更正一处误报）**：`TDMA-SEAT-01/02/03` 在 `docs/check/DOCS_REGISTRY.md` 中**无任何登记行**（成立）；但 `EXE-SEAT-01` **并非「全库无定义文档」**——它在 `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 的「特等席增设须用户审核」节有定义（该文档 `Status: Draft`，自述不新增 wire 契约）。可成立的较弱表述是：`EXE-SEAT-01` **无登记行、无用户审核结论记录**，却被多个域当作前置引用。故 `T2-IDENT-001` 与 `T2-TRANSPORT-002` 中「按 `EXE-SEAT-01` 登记（C11）」这一步**仍无落点**（Draft 文档不可作 clause_loc），现改指 TDMA 域 2026-10-14 建立的 `TDMA-SEAT-01`（一等席容器与预算条款登记）。同一「全库无定义」断言另见 `docs/check/submissions/TRIGGER_CROSS_REVIEW_03.md`——该文件是已归档的评审原件，按惯例**不修改**，更正记于此与本域进展日志。
| T2-AUDIT-04 | 转台**源路径选型**定稿（`TRIGger:REServation:SOURce:*` 硬件 PWM 源 vs `CONFigure:MODEl:TURNtable:*` 加减速模型源） | ✅ 口径已定（生产 IN1 / 台架 OUT4→IN1 回环模拟） | 阻塞条件：属**产品/验收口径决策**，选型会改变隔离验收（`T2-TURNTABLE-008`）的证据口径；待用户定稿，本任务不预设结论 |

说明：`T2-AUDIT-03` 与 `T2-AUDIT-04` 的 `BLOCKED` 为**前置未满足**（契约未冻结、口径未定），非技术不可行；
前置满足后按 AGENTS.md 的契约登记流程（含 C11 交叉审核）推进。

> 本节长记录已按 C12 下沉至 `TRIGGER_T2_TASK_PROGRESS.md`（单一事实源）。

> 本节长记录已按 C12 下沉至 `TRIGGER_T2_TASK_PROGRESS.md`（单一事实源）。

> 本节长记录已按 C12 下沉至 `TRIGGER_T2_TASK_PROGRESS.md`（单一事实源）。

> 本节长记录已按 C12 下沉至 `TRIGGER_T2_TASK_PROGRESS.md`（单一事实源）。

> 本节长记录已按 C12 下沉至 `TRIGGER_T2_TASK_PROGRESS.md`（单一事实源）。

### 进度日志轮转就绪（T2-AUDIT-05，2026-10-13）

**触发**：`TRIGGER_T2_TASK_PROGRESS.md` 当前 **144 KB**（上限 200 KB，见 `DOCS_REGRESSION_PLAN.md` 的 C14/R1）；
若继续收录审查记录则可能越限。**本条不提前轮转**（未越限即不动）。

**轮转时的正确做法**（含本会话实测得到的机制细节）：
1. 按 C14 **最旧优先连续**截断到 `docs/legacy/trigger/` 下的 `LEGACY_TRIGGER_T2_TASK_PROGRESS_<NN>.md`（轮转时才创建，故本条按 R3 的**模式形式**书写）；
2. 在本文件维护**归档索引章节**（名称见下方注意事项），行必须指向 **`LEGACY_<DOMAIN>_TASK_PROGRESS_<NN>.md`** 的**确切文件名**（R3）；
3. 保持 **ID 闭包与条目数相符**；**只搬旧证据、不删证据**；
4. 新归档文件需带五字段元数据（Status/Domain/Canonical/Related/Last updated），并在 `docs/README.md` 登记一行。

**注意事项（本会话实测教训）**：
`doc_regression` 的 R3 检查**按字符串匹配**：正文中只要**字面出现**该章节标题（即使在反引号内）也会被当作章节并因“无数据行”而 FAIL；
故提及该章节名时**不得带 `##`**，本条即按此约束编写。

### T2-AUDIT-06：persona 资源与 WAVE_OUTPUT persona 核验（本仓已具备，无需移植）（2026-10-13）

状态：**核验完成——本仓已具备，无需移植**。

**核验结论（2026-10-13，前提已过期）**：本仓 `sync_io_persona_resources.c` 第 `:100` 行已有完整的
`SYNC_IO_PERSONA_ID_WAVE_OUTPUT` 行（`.name = "wave_output"`、`.program_name = "sync_model_sched_pulse_high_or_low"`、
`instruction_words = SYNC_IO_WAVE_OUTPUT_INSTRUCTION_WORDS`、`.sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_WAVE_OUTPUT_SM)`、
`.tx_fifo_sm_mask` / `.tx_dreq_sm_mask` 同 SM、`.gpio_write_mask = SYNC_IO_MAIN_OUTPUT_GPIO_MASK`、
`.dma_channel_mask = SYNC_IO_DMA_BIT(SYNC_IO_MODEL_PULSE_DMA_CH)`），且 `sync_io_model_sched.c:931-934` 已按该 persona 路由。
因此**不需要移植 persona 资源与 WAVE_OUTPUT persona**；本会话曾自制一份模式并已回退删除（避免双轨）。

**仍真正缺失者（可选后续，非本项前提）**：参考实现的 **SEQUENCE 全域**（`sync_io_sequence.pio` / `.c` / `.h`、
`SYNC_IO_PERSONA_ID_SEQUENCE` / `_SEQUENCE_COUNTER`）与其 **Python 调试界面**（`rp2350_tk_toolbox.py`）。

**原状态记录（保留供追溯）**：PENDING（参考取证已完成，待实施）。参考实现：分支 `origin/feature/node-sequence-reservation-trigger`（`19b9c5ff`）。

**已取证的对齐事实**（逐字）：
- 该分支的 persona 枚举包含 **`SYNC_IO_PERSONA_ID_WAVE_OUTPUT`**（已有）；
  其资源表项为：`sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_WAVE_OUTPUT_SM)`、
  `dma_channel_mask = SYNC_IO_DMA_BIT(SYNC_IO_MODEL_PULSE_DMA_CH)`、`gpio_write_mask = SYNC_IO_MAIN_OUTPUT_GPIO_MASK`、
  `instruction_words = SYNC_IO_WAVE_OUTPUT_INSTRUCTION_WORDS`。
- 该选择与本仓已实现的 `sync_io_mode_wave_output`（PIO0 / SM2）**一致**；差别仅在表达形式：应为 **persona 表项**，而非模式 ops 裸掩码。
- 预算与冲突检查已有现成写法：`SYNC_IO_PERSONA_PIO_INSTRUCTION_CAPACITY = 32u`、`SYNC_IO_PERSONA_PIO_SM_COUNT = 4u`、
  `SM_MASK = 0x0Fu`、`SYNC_IO_PERSONA_DMA_CHANNEL_COUNT = 16u`；并有静态断言（如
  “sequence PIO instruction budget changed”、“sequence must preserve capture”）守住指令预算。
- DMA 通道已分配：`SEQ_STEP=0`、`ENC_COUNT=1`、`MODEL_PULSE=2`、`CAPTURE=3`。

**移植清单**（三类）：
1. **直接搬**：`sync_io_persona_resources.h` / `.c`（预算常量 + 资源表 + 静态断言）、`sync_io_sequence.pio`（`sequence_ingress` / `sequence_finite_ingress` 等程序）。
2. **需改写**：`sync_io_sequence.c/.h`（SM 角色 `TURNTABLE_SM=0` / `INGRESS_SM=1` / `EXECUTOR_SM=2` / `COUNTER_SM=3`、`sm_claimed` 与 `pio_add_program` 负责装载）；
   以及本仓 `sync_io_mode_wave_output.*`、`sync_io_mode.c` 注册、`CMakeLists.txt` 清单。
3. **暂不搬**：SEQUENCE 全域运行时（ingress/executor/counter/gateway）与 RefMem sequence roles；待 WAVE_OUTPUT persona 落地并验收后再议。

**验收**：指令预算与 SM 冲突作为**负测**（静态断言 + 运行期冲突拒绝）；装载按既定切换序（停 SM → 停 DMA/IRQ → 清 FIFO → 卸载/装载 → 初始化执行位置 → **原子发布 generation**）；
最终走 **workflow 前端**验收后提交。

### T2-AUDIT-07：登记位置邮箱跨域契约（2026-10-13，状态 DONE）

**发现**：`components/sync_trigger/inc/trigger_position_mailbox.h` 被跨域共享（`components/distributed_refmem/src/distributed_refmem.c:30` 直接 include），
但 `docs/check/DOCS_REGISTRY.md` 中**尚无**对应契约行。

**处置（2026-10-13 完成）**：起草登记行 `ARCH-TRIGGERMAILBOX-01`（前缀 `ARCH-*` 在 `ROW_RE` 白名单内，`status` 初值 `pending`）
⇒ **C11 独立交叉审核**（核验方为**独立 subagent**：与起草方不同会话、未参与起草，只读核验）
⇒ 结论 **`PASS_WITH_NOTE`** ⇒ 按其意见修正四项措辞/归属缺陷后登记。
提交单 `docs/check/submissions/TRIGGER_CROSS_REVIEW_02.md`。

**C11 修正四项（均属措辞/归属，无实现缺失、无编造）**：

1. `[1, LATCH_IDS]` 不再写作通用「有效 `position_id` 域」，改为 FREEZE latch **槽位域**并显式划分所有权（线上域归 `ARCH-TRIGGERSLICE-01`，本条不重复立法）；
2. 小节标题由「冻结」改为「登记，status `pending`」，并同步消除同文档原「本文当前仍为 Draft」的自相矛盾表述；
3. 「载荷仅在 `WRITING` 写／`READING` 读」限定为**运行时（并发交接期）**，补记静默边界（init／STOP／环拓扑变更）清空载荷与 `WRITING`/`READING` 期拒绝复位；
4. 显式声明 FREEZE latch 是同头文件内**独立于 mailbox** 的结构体，其载荷规则为「bit 清写／bit 置读」，不适用四态规则。

**本次审核的新产出（不属本行缺陷，另立 `T2-IDENT-002`）**：同一「位置」域在代码内**三方口径不一致**——线上／latch 寻址域 `[1, 15]`
（`trigger_position_slice.h:38` 的 `0x0Fu` 与 `trigger_position_mailbox.h:74` 的 `TRIGGER_POSITION_FREEZE_LATCH_IDS`，二者**无静态断言绑定**）、
`trigger_position_descriptor_valid()` 的 FREEZE 分支硬编码 `position_id <= 3u`（`trigger_position_descriptor.h:193`，**未引用宏**）、
T2 运行时消费域 `[1, TRIGGER_T2_RUNTIME_POSITION_COUNT]`（`= 3u`）。

**未核验**：本次为**静态只读核验**，不含同源码构建、四板 P3 或 pytest 运行；内存序仅作形式配对核验，未做多核压力验证。

### T2-AUDIT-08（已移出：属接口域）

> 本项为**跨域项**（归 INTERFACE/SCPI_HOST 域），已从 T2 待办移出，避免同一事实在两处复制（C12）。
> 请见 `docs/interface/SCPI_HOST_TODO.md` 的 **IF-001..004**（单串口拓扑下的会话/寻址、命令转发与应答、隔离与权限、错误与故障可观测性口径）。

### 跨域口径已归口接口域：错误/故障可观测性

> 本项为**跨域口径**（归 INTERFACE/SCPI_HOST 域），已在 `docs/interface/SCPI_HOST_TODO.md` 的 **IF-004** 展开：
> 主机在「命令被拒绝」「传输超时」「域级故障已 latch」三种情形下分别能观测到什么、又分别由什么清除
> （含 `*CLS` 清错误队列但**不清** F1 域级故障这条分界）。
> T2 域只保留自身的功能条目与已冻结的实测结论（见下表 C1 及其依据）；口径与条款不在本域重复（C12）。
> 该口径由本仓 P3 验收 step 17 现场暴露，证据见 `docs/interface/SCPI_HOST_TASK_PROGRESS.md`。

## 结论区（冻结，只增不改）

本区收录**已确认/已证伪**的结论，每条一行：结论 + 依据 + 状态。过程性排查不再增长（原附录 A 保留为证据位置）。
架构锚点以 `docs/arch/HAOFV_ARCHITECTURE.md` 与 `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` 为准。

| # | 结论 | 依据 | 状态 |
|---|---|---|---|
| C1 | `fault_code = 3` 即 Trigger 域错误码 **3 = PIO/DMA 配置失败**；且属 **F1 域级故障**，`*CLS`/`*RST` 无效，需**显式 CLEAR** | `HAOFV_ARCHITECTURE.md:1033`（错误码空间）、`:1197`（F0–F3 分级）；实测 `*RST`/`*CLS` 后 `fault` 仍为 3 | 冻结 |
| C2 | 模型 arm 失败根因：**在 arm 路径内反复 `pio_add_program` ⇒ PIO0 指令内存耗尽**（程序不自动移除） | `sync_io_model_sched.c:975/985`；重刷后立即最小序列 ⇒ `START "OK"`、`running=1 fault=0 total=2896` | 冻结 |
| C3 | 架构正解：动态 PIO 须经 **mode descriptor（`sync_io_mode_ops_t.hw`）+ PIO 资源分配表 + Resource Arbiter + DeploymentGate 的 PIO/SM/DMA/IO/IP claims**；"一次装载"仅过渡合规 | `ARCH-PIOCAT-01`（`:189`）、`ARCH-PIOPARTITION-01`（`:1236`）、`:1520`、`:1724` | 冻结 |
| C4 | **已证伪**：索引越界 ✗（`PIN_COUNT = 4` ⇒ `LOAD 0,3` 合法）、表项超限 ✗（`MAX_ENTRIES = 2048` ⇒ 64 合规）、累积卡死 ✗（`START` 前 `phase=0 fault=0`）、counter/source/run-output/reference/persona 占用 ✗（逐一停用/释放后仍失败） | A.3.122 / A.3.125 / A.3.132 / A.3.133 / A.3.137 / A.3.141 实测 | 冻结 |
| C5 | 判据纪律：**"无回显 ≠ 失败"**（以状态页随动为准）；`<timeout>` 不构成失败判据 | A.3.141 实测（`WAVEform:STATus?` 随动而回显为空） | 冻结 |
| C6 | 架构闭环**未实现**（仓库零命中）：`TriggerReservationFB`、`T_fire_target_vdc`、`T_fire_deadline_local`、`T2_actual_local`、`T2_error_ns`、预约 11 态状态机、预约 SCPI ⇒ 属收敛顺序**第 6–7 项，设计上最后开放** | A.3.145 取证；`HAOFV_ARCHITECTURE.md:1720-1723`、`:1707` | 冻结 |
| C7 | 入口纪律：SCPI 只能**投递事件/写命令槽/读快照**；配置写入属 `MAINTENANCE`；`TRIGger` 是产品动作域，`REALtime` 只做底层维护与调试 | `HAOFV_ARCHITECTURE.md:120/605/1186/1349/1414` | 冻结 |

## 已验证证据页（硬件实测）

| 项 | 结果 | 证据 |
|---|---|---|
| 361 档角度扫描 | `T2:SCAN?` 15 字段自洽（361 位置、窗口与行程一致） | 流程实测 |
| 边界档 | `T2_SCAN_OVERRIDE_TERMINAL_MDEG=5000` ⇒ 6 位置，`entered=announced=6` | 流程实测 |
| **每位置 16 组** | `total = 2896 = 181 × 16`，`fault = 0` | 直连实测 |
| 分块续装订 | 调度页首块 `64` 完成后续装订（`1,1,1,0,1,64,3,113,…` 递增） | 直连实测 |
| `GROups` 配置 | 读-改-写生效；越界拒绝（17 ⇒ 保持 16） | 直连实测 |
| `TRIGger` 写修复 | 流程与直连均回 `"OK"`，`1,0,360,2,100,1,1000` 生效 | A.3.107 |
| **PIO 一次装载** | 连续 6 次 `START/STOP` 全 **ARM OK**（`running=1 fault=0 total=2896`） | 提交 `310ff141`（P3 凭证 ✓） |

**git 教训（2026-10-13 实测）**：若文件已 `git add`（暂存），`git checkout -- <file>` 会用**索引版本**覆盖工作树，
从而把已暂存的修改（包括不合规内容）“恢复”回来；正解是 `git restore --source=HEAD --staged --worktree -- <file>`（或 `git checkout HEAD -- <file>`）。
本次即因此一度使 C14 R3 的 FAIL 持续，已用正解清除。

> 本节长记录已按 C12 下沉至 `TRIGGER_T2_TASK_PROGRESS.md`（单一事实源）。

## 附录 A（已归档）

原「实现注记」全部内容（编译器约束、legacy 三槽迁移清单、SCPI 接线落点、已结案项与 152 条探索日志）
已按 C14 转出本文件，以保持本文为**任务清单**：`docs/legacy/trigger/LEGACY_TRIGGER_T2_TODO_01.md`。
本节只保留指针；新结论请写入上方「结论区（冻结，只增不改）」。
