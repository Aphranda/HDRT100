# T2 预约触发任务进度

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-10-20

## 文档接口

本文记录真实执行、来源和证据，不冻结契约或代替 [TODO](TRIGGER_T2_TODO.md) 的任务状态；设计见 [Architecture](TRIGGER_T2_ARCHITECTURE.md)。条目使用 `T2-PROGRESS-YYYYMMDD-NNN`，最新在上；按 C14/C15 保留失败、历史和归档索引。

## 当前 checkpoint

### T2-PROGRESS-20261020-010：两字协议第二轮尝试——mock 的 OUT 已支持 `out isr`，剩余卡点是**时序模型**

- **已提交**（`506e2072`）：宿主 mock 的 `OUT` 现在支持 dest 7（`out isr, 32`），且 51 个用例保持全绿——
  这是两字协议的前置条件，可独立保留。
- **本轮尝试并回退的内容**（记录以便下次直接续）：PIO 加第二个 `pull/out isr`（8→10 条）、
  `prepare_realtime` 去掉 ISR 预置、`start_deadline(deadline, sequence)` 推入成对字、完成时保留 SM 运行。
- **遇到的三处测试耦合（按顺序）**：
  1. mock 断言**程序窗口** `off==5 && lo==5 && hi==12` → 已是 10 条指令，需改 `hi==14`、ready 段 `off==15,hi==20`；
  2. 脉冲时序断言 `rise_tick>=target && rise_tick<=target+64` **失败**：因为 `start_deadline` 的
     "到上升沿的开销"常量还是 **4**，两字协议后应为 **6**（`pull,out x,pull,out isr,jmp,irq`）——
     已改为 6 但仍失败，说明 mock 的逐 tick 执行模型对"两次 pull"还有一步偏差（约 2 tick），
     需要在 harness 里把"arming 到 rise"的期望改成从**实际程序布局**推导，而不是常量化。
  3. `pio_sm_init` 的窗口断言硬编码了 5/13 偏移 → 建议改为按 `pio_add_program` 返回的偏移推导。
- **当前状态**：改动**全部回退**，工作区 clean，`88 passed`，固件与台架行为不受影响。
- **下一轮**：先把 harness 的时序/布局期望改成**推导式**（不硬编码 4/5/12/13），再重放两字协议；
  判据仍是 `abandoned == 0 && w_executed == 361` 连续两轮。



### T2-PROGRESS-20261020-009：**网关的损耗机制被量化**——每圈"被顶掉 9 个位置"，不是丢组也不是拒绝

- **`T2:ACTion?` 的窗口五总量（45..49）已接入 `sequence_workflow` 的读数**，本轮整圈：
  ```
  角色        entered  frozen  w_executed  abandoned
  SWITCH_A      361     361      361          0
  GATEWAY       361     361      352          9
  SWITCH_B      361     361      361          0
  ```
  ⇒ 网关**所有位置都进入并冻结了**（361/361），但有 **9 个在"冻结后、执行前"被新位置顶掉**（abandoned）。
  这与 `executed=5520/16=345`、开关 355 的差额一致。
- **机制**：网关每组合仍付**完整 SM 重初始化**（关 SM、清双 FIFO、重启、回放预置，约 108 ms/组 vs 开关约 25 ms/组），
  于是每圈落后约 9 个位置的时间；转台继续前移，落后者在槽位被接管时按 `ABANDONED` 记一笔（不是错误，是设计里的"被顶掉"）。
- **结论**：**要消除这 9 个位置，必须把每组耗时压下去**（两字协议预装）——这与前几轮的判断一致，
  现在第一次有"9"这个确切数字作为验收目标。
- **下一轮验收线**：`abandoned == 0` 且 `w_executed == 361`（网关与开关同水平），连续 2 轮。



### T2-PROGRESS-20261020-008：网关再达 5520（97% 开关水平）；窗口总量计数尚未可信

- **整圈（差值口径）**：
  ```
  SWITCH_A 5680(355)   SWITCH_B 5664(354)   GATEWAY 5520 = 16×345(97%)   backend 9
  ```
  ⇒ 与第 6 轮的 353 一致：**网关有能力跑到开关水平**，方差仍存（66~353）。
- **新增诊断**：`DIAG` 尾部加入位置窗口五个总量（entered/announced/frozen/executed/abandoned），
  取自已存在的 `trigger_t2_runtime_position_window_totals()`。
- **读数异常（待查）**：本轮读回 `slot2 = 0,0,0,0,8`，与"执行了 5520 组"不相称 ⇒
  两种可能：① 字段偏移错（页面加字段后我按尾部取值）；② 窗口总量只在源板维护、从板未累计。
  下一轮先用"已知值对拍"（例如人为触发一次 enter）确认索引与归属，再用它做丢失归因。
- **当前结论**：执行器侧已排除干净（拒绝/延后/服务代价均正常），剩余方差的两条候选是
  ① 窗口总量口径未验证；② 位置供给/冻结时机。二者都需要可信的窗口计数才能分辨。



### T2-PROGRESS-20261020-007：位置丢失**不在执行器**——拒绝/延后计数全为 0，loss 在上游分发

- **新增三个"位置丢失"计数到 DIAG**：`position_descriptor_reject / position_transport_reject /
  position_freeze_mismatch`；另加 `defer_plan_pending`（网关"计划在飞"而延后的次数）。
- **实测（整圈，网关 4024 ≈ 16×251 位置）**：
  ```
  descriptor_reject 0   transport_reject 0   freeze_mismatch 0   defer_plan_pending 0
  ```
  ⇒ 网关**没有拒绝任何位置、也没有延后任何位置**，却只执行了 251 个（开关 355）。
  这排除了执行器侧全部候选（拒绝、延后、坏回执）。
- **推论**：丢失发生在**上游位置分发**——这些位置根本没有冻结到网关板（FREEZE 未到达或被分发跳过）。
  下一步应比较 `published_mask / frozen_mask / received_mask` 的本轮增量，以及
  `T2:ACTion?` 里"本槽位收到 FREEZE 次数"，判断是"未广播到该板"还是"广播了但未落槽"。
- **本轮保留**：三个 loss 计数 + defer 计数（都已进 DIAG，供下一轮直接对照）。



### T2-PROGRESS-20261020-006：差值口径下的稳定性基线（4 轮）——网关**能跑满，但方差极大**

- **4 轮整圈（差值口径，同一固件、未 reboot）**：
  ```
  run1  GATEWAY  66 位置（开关 354）
  run2  GATEWAY 260 位置（开关 354）
  run3  GATEWAY  81 位置（开关 355）
  run4  GATEWAY 353 位置（开关 355/354）   ← 与开关同水平
  ```
  ⇒ 网关**可以**跑到开关水平（353/355），但**方差极大**（66~353，中位数约 170）。
  三类功能性失败几乎为 0（两次单发 backend）。
- **每组合的服务代价恒定**：6 份快照的 `svc_wait/complete` = **3.01~3.12**（无波动）。
  说明**服务循环本身不慢**，方差不来自"服务变慢"。
- **推论**：慢轮次里网关并非"完成得慢"，而是**有大量位置没有进入执行**（`no_plan` 计数仍为 0，
  说明不是"计划缺失"而是**没有被选中/被延后**）。下一轮应聚焦运行期的**选择/延后路径**
  （`service_deadlines` 的 selected 逻辑与 `plan_pending()` 延后条件），而不是执行器时序。
- **量化验收线（本目标）**：差值口径下网关每轮位置数 ≥ 开关的 90%（≥320），连续 3 轮。
  当前 4 轮中 1 轮达标。



### T2-PROGRESS-20261020-005：**差值口径落地**——首次拿到"本轮真实吞吐"：开关 ≈354、网关 66~260

- **改动（测量侧，两个工具）**：
  1. `t2_workflow.py` 在采样**开始前**抓一次 `WHY?/DIAG?/GWDIAG?` 写入 `out/sync_trigger/sequence_baseline.json`；
  2. `sequence_workflow.py` 的 counters 步骤对有基线的角色做**逐字段相减**（executed/late/expired/no_plan/backend/finished），
     并在读数里标 `baseline=True`。
- **两轮实测（差值口径）**：
  ```
  run1: SWITCH_A 5664(354 位置)  SWITCH_B 5664  GATEWAY 1051 ≈ 65.7 位置   backend 0
  run2: SWITCH_A 5680(355)       SWITCH_B 5664  GATEWAY 4155 ≈ 259.7 位置  backend 1
  ```
  ⇒ **首次得到可信的"每轮"吞吐**：开关稳定在 354~355（361 减去基线窗口内的几处），
  网关在 66~260 之间波动（开关的 19%~73%），且**未出现硬停住**。
- **与上一条更正的关系**：`executed/16` 的"361/701/1054/1110"是累计值；差值口径下网关的真实每轮吞吐远低于开关，
  所以**两字协议（预装）确实是提升吞吐的必经之路**，而不是"可能不需要"。
- **下一步**：① 以差值口径连跑 5 轮，给出网关吞吐的 min/median/max（稳定性基线）；② 两字协议需要先重构
  `test_sync_io_gateway.py` 的 mock（它把单字契约写死在断言里），再改 PIO/`start_deadline`/`service`。



### T2-PROGRESS-20261020-004：**重要更正**——网关 DIAG 计数是**跨轮累计**，"吞吐方差"大半是度量假象

- **证据（24 份按轮归档快照的相关性分析）**：
  ```
  每组合完成的服务等待(svc_wait/complete)在所有正常运行的轮次里恒为 ≈3.0
  但 executed/16 出现 361、701、1054、1110 … 直到 5776 的整数倍堆叠
  例：20261020-010044 executed=5776(361 位置) → 011024 executed=11552(=2×5776) → 011918 14589
  ```
  ⇒ `executed`/`complete` 是**累计值**，不会在每次 run 开始时归零；把它当成"本轮位置数"会得出
  "吞吐忽高忽低"的错误结论。真实情况是：**多数轮次都跑满 361**（累计值按 5776 递增），
  少数轮次确实是硬停住（`plan=16, ring=5, complete=1`）。
- **对判据的影响**：`sequence_workflow` 现在用 `executed == 组数 × 361` 判定，遇到累计值会**误判 FAIL**。
  必须改成**差值口径**（采样前基线 → 采样后读数相减），或在固件侧于 run 开始时清零这些计数。
- **修正后的稳定性画像**：硬停住已由前面的修复消除（本轮多轮无 `ring=5`）；"慢"的轮次主要是
  重复归档造成的**同一轮快照**（223831/224707、233933/234744 数值完全相同即证据）。
- **下一轮**：① 在采样前抓一次网关 `GWDIAG` 作为基线并写入快照；② `sequence_workflow` 用差值判据；
  ③ 用差值重新统计 10 轮真实吞吐，再判断是否还需要两字协议（预装）。



### T2-PROGRESS-20261020-003：定论——**续装与单字协议不兼容**（回执身份固定），必须走两字协议

- **决定性证据（停住快照 + 计数，同一轮）**：
  ```
  cont_hit = 1     complete = 1     bad_receipt = 1
  plan_next = 2    sm_en = 0        pending = 0      ring reason = 5
  ```
  ⇒ 续装把第二组的延时字推入 FIFO 后，PIO 结束时 `push` 的是 `prepare_realtime` 预置的
  **ISR（第一组的序号）**，与第二组的 `s_pending_receipt` 不符 ⇒ 判为坏回执 ⇒ `safe_stop()` ⇒
  计划停在 `plan_next=2`、整轮失速。
- **结论**：只要回执身份仍由"每次 prepare 预置的 ISR"承担，**续装（预装下一组）就不可能成立**。
  想拿到开关那样的吞吐，必须把"序号"放进**每个触发字**里——即早已成文的
  `T2-GATEWAY-PRELOAD`（两字：延时 + 组序号，`push` 回序号）。
- **本轮保留的资产**：① `sync_io_gateway_realtime_cancel_owned(owner)`（owner 匹配才立即停，
  否则只清计划）——两字协议之后 `cancel` 语义仍然正确的前提；② `cont_hit/cont_miss`、
  `armed_owner`、`cancel_match/miss` 计数（GWDIAG 39..41）；③ 停住快照 reason=5。
- **下一轮**：实现两字协议（PIO 程序 + `stage_sequence`/service + 宿主 mock），再把"保持 SM 运行"打开。



### T2-PROGRESS-20261020-002：续装的"配套修法"与既有契约冲突——取消语义需要**按组归属**才能安全打开

- **本轮尝试**：① 完成时保持 SM 运行（让续装可达）；② `cancel()` 改成"不再发组、但不杀在飞的脉冲"。
- **结果**：宿主用例 `test_sync_io_gateway[deadline_cancel]` **失败**——它断言取消必须
  `state==CANCELLED && !pending && !rise_tick`，即**取消要立刻停脉冲**。这与 ② 的设计**直接冲突**。
- **分析（这才是根因）**：网关只有一个执行器、`cancel()` 是**全局**的。运行期在某个位置失败时会取消，
  而此刻可能正有**另一个健康位置**的一组脉冲在飞 ⇒ "立即停脉冲"会丢掉那一组的回执，表现为
  `plan_next=2 / svc_wait` 增长 / `pc=28 tx=0` 的静默失速。
  ⇒ 真正的缺口是**执行器没有"组归属"**：cancel/arm 无法只作用于自己的那一组。
- **处置**：两处改动**全部回退**（保持既有契约与已验证行为）；当前整圈基线为
  `SWITCH 5776/5776、GATEWAY 4977 = 16×311、no_plan/late/backend 全 0`（本轮开始时即此水平）。
- **下一步（架构级，明确）**：给网关加**组归属**（arm 时记录 owner slot/sequence，cancel 只撤回 owner 匹配的组；
  不匹配则只清计划、不杀脉冲）。有了归属，"保持 SM 运行 + 续装"才能安全打开，吞吐才会到开关的水平。
  这也是 `T2-SEQ-001`（单一 owner）在执行器侧的落地。




### T2-PROGRESS-20261020-001：网关"续装"通路此前是**死路**（hit 0 / miss 1487）；打开后暴露"脉冲中途被关 SM"，已回退并留诊断

- **关键发现**：`service` 在每次完成时**先关 SM 再武装下一组**，于是 `schedule_continuation()` 六次条件永假：
  实测 `cont_hit=0 / cont_miss=1487~2536` ⇒ **每一组都在付完整的 SM 重初始化代价**（关 SM、清双 FIFO、重启、回放 4 条预置）。
  这正是网关只有开关 40~60% 吞吐的机制性原因。
- **尝试**（保留 SM 运行以便走续装）：首组续装即**卡在中途**——`ring reason=5`（停住快照）记录 `pc=28`、`tx=0`、
  `plan_next=2`、`late=345`、`backend=1`；`stage0=20`（NO_LEASE 拒绝 20 次）、`arm_origin=1`。
  说明"保持 SM 运行"必须配套修掉"另一处会在脉冲中途关 SM 的路径"，否则更差。
- **处置**：**回退**"保持 SM 运行"，保留 `cont_hit/cont_miss` 计数与停住快照（reason=5）。回退后整圈：
  ```
  SWITCH_A 5776 / SWITCH_B 5776 / GATEWAY 4977 ≈ 16×311   no_plan 0  late 0  backend 0
  ```
  ⇒ 零失败、网关 86%，回到本轮开始前的水平之上。
- **下一步（明确）**：先定位"脉冲中途被关 SM"的调用者（`stage_sequence`→`prepare_realtime` 在 pending 时本应拒；
  `safe_stop`/`cancel` 会关），再用 `cont_hit` 验证续装真实生效——那是把网关拉到 100% 的路径。




### T2-PROGRESS-20261019-069：网关**跑满整圈**（5776 = 16×361，零失败）——最后一处残留标志在第 4 个地方

- **第三次定位（本轮）**：`prepare_realtime()` 在**开头就关闭 SM**，但 `s_output_running=true` 要到函数**末尾**才置位。
  若 prepare 在中间失败（`pulse_ticks` 非法、`product%1e9`、pending 非零等），标志仍是**上一次 prepare 的 true**
  ⇒ 之后的"续装"把延时字推进**已关闭**的 SM ⇒ `plan_count=16 / plan_next=1 / svc_wait` 增长 ⇒ 整轮失速。
  修法：关 SM 的同时立刻把标志清零（早退路径也覆盖）。
- **台架（140 s 整圈）**：
  ```
  SWITCH_A 5776 = 16 × 361   BACKEND 0
  SWITCH_B 5760 = 16 × 360   BACKEND 0
  GATEWAY  5776 = 16 × 361   BACKEND 0   ← 首次与开关完全同水平
  清空原因环: reason=1(drained) plan_count=0 plan_next=16
  ```
- **新增诊断（本轮）**：① 计划清空原因环（1 drained / 2 次组武装失败 / 3 首组武装失败 / 4 cancel）；
  ② "停住快照"——pending 连续 4096 次服务无回执时，把 `pc | tx<<16`、`plan_next`、最后一次延时字写入环（reason=5）；
  ③ `GWDIAG` 扩到 37 字段（`last_delay_word / last_deadline_lo / last_delta_lo`）。
- **同轮更早的三次失败运行**（128 / 0 / 704 位置）与本次成功，差别只在固件版本与上电状态 ⇒ 仍是**残留标志类**问题，
  每次修掉一处就少一类失速。
- **下一步**：连跑 3 次验证 5776 可复现；若复现则本目标可收口。




### T2-PROGRESS-20261019-068：网关稳定性质检升级——**计划清空原因环**（GWDIAG 27..30）上线；本轮整圈 5628 ≈ 16×352、零失败

- **新增诊断（执行器自身负责）**：网关记录最后一次"计划被清空"的**原因**（1  drained 正常、2 下一组武装失败、
  3 首组武装失败、4 cancel 撤销）+ 当时的 `plan_next/complete/tick`，4 项环，`GWDIAG?` 扩到 31 字段。
  这样"整轮 executed 0 但失败计数为 0"的静默失速，读一次就能归属。
- **台架（140 s 整圈，修复 + 四板 reboot）**：
  ```
  SWITCH_A 5776 = 16 × 361   BACKEND 0
  SWITCH_B 5776 = 16 × 361   BACKEND 0
  GATEWAY  5628 ≈ 16 × 352   BACKEND 0   identity 0 terminal 0 arm 0
  清空原因环: reason=1(drained) plan_next=16 complete=5616
  ```
  ⇒ 网关达到开关的 **97%**（352/361）且**零失败**；`identity` 也首次为 0。
- **同轮更早两次（未 reboot、仅换固件）**：2816（176 位置）、1654（103 位置）——**无硬停住**，但仍有轮间方差；
  说明"停住"是本轮前两个修复解决的，"方差"仍与跨轮残留/上电状态相关。
- **已知非失败项**：`collision`=`late`=5278（迟到钳位在跑满时必然发生，迟到照发）；`svc_wait` 17365 为轮询计数。




### T2-PROGRESS-20261019-067：网关"停住"已消除，但**吞吐逐轮退化**（201 → 45 → 10 个位置），下一轮查残留状态

- **三次连续整圈（修复后，同一固件、未 reboot）**：
  ```
  run1: SWITCH_A 5776 / SWITCH_B 5760 / GATEWAY 3217 ≈ 16×201   BACKEND 0
  run2: SWITCH_A 5776 / SWITCH_B 5760 / GATEWAY  718 ≈ 16×45    BACKEND 0
  run3: SWITCH_A 5776 / SWITCH_B 5776 / GATEWAY  157 ≈ 16×10    BACKEND 0
  ```
  ⇒ **不再出现 `executed 0` 的硬停住**（这是本轮修复的成果），但网关位置数**逐轮下降**：
  201 → 45 → 10，且三类失败始终为 0（再次说明"没报错但不走"）。
- **判读**：这是**跨轮残留状态**造成的节拍退化，而不是本轮修的那个标志问题。
  需要"每轮归档 GWDIAG/DIAG 再比对"，看哪一项在累积（候选：`collision`、`svc_wait`、
  `plan_count/plan_next`、`late`）。
- **已知无关**：`START` 四桶全 0（挂载正常）；`chain rc=0` 三轮都通过。
- **台架纪律**：每轮之间没有 reboot；此前"reboot 后第一轮最好"的现象也支持"残留状态"假设。




### T2-PROGRESS-20261019-066：网关**不再停住**——"窗口已装"标志现在要求后端真的还持有计划（201 位置、零失败）

- **根因（第二个，也是停住的主因）**：`service_deadlines()` 把 `s_window_staged[selected]` 当作
  "执行器已持有本位置窗口"，于是**跳过 staging**、直接 `armed = true`。但网关会**丢掉**那个窗口
  （cancel/失败/计划老化）而标志仍为 1 ⇒ 之后每个位置都"免费武装"、再也不会重新装载 ⇒
  整轮 `executed 0`，且三类失败计数为 0（所以看起来"无错但不走"）。
- **修法**：`window_held = s_window_staged && (非网关 || plan_pending())`；标志为真但后端已丢时，
  先清零再**重新 staging**。同一处还补了"只有 `!window_held` 时才走单组 `schedule_at` 分支"。
- **外加**：`START` 拒绝分桶（0 守卫 / 1 grid cursor / 2 predictor configure / 3 predictor begin）
  已加入 `DIAG` 尾部——本轮实测 4 个桶全 0，说明挂载失败此前是**台架状态**（环未停/未 reboot），不是代码。
- **台架验证（140 s 整圈，两处修复 + 四板 reboot）**：
  ```
  SWITCH_A 5776 = 16 × 361   BACKEND 0
  SWITCH_B 5776 = 16 × 361   BACKEND 0
  GATEWAY  3217 ≈ 16 × 201   BACKEND 0     ← 此前同条件是 executed 0（停住）
  ```
  ⇒ **停住消失**；网关吞吐约为开关的 56%（201/361），属节拍缺口，不再是稳定性故障。
- **判据副产品**：本次运行满足门禁判据（三角色零失败 + 两角色满圈），`t2_acceptance_receipt.json` 已由
  `sequence_workflow` **自动写入**，本轮提交即用它过门禁。




### T2-PROGRESS-20261019-065：网关稳定性——定位到"运行标志未随 SM 关闭而清零"（代码根因），并完成 real-flight 对照

- **根因（代码级，证据充分）**：今天新增的"稳态续装"用 `s_output_running` 判断"SM 仍处于已准备且空闲"。
  该标志只在**完成路径**清零，而 `safe_stop()`（cancel/失败路径会走到）与 `start_deadline()` 的
  origin 失败路径都会**关闭 SM 却不清标志**。结果：下一组走"续装"，把延时字推进**已关闭**的 SM 的 FIFO，
  没有消费者 ⇒ `pending` 永久为 1、`svc_wait` 持续增长、位置永不结束。
  与实测特征一一对应（`plan_count=16, plan_next=1, complete=N, svc_wait` 上升、三类失败为 0）。
- **修法**：`safe_stop()`、`start_deadline()` 的 origin 失败分支、`arm()` 三处都置 `s_output_running=false`。
  构建通过、`test_sync_io_gateway` + `test_trigger_t2_runtime` **88 passed**。
- **real-flight 对照（用户要求）**：切到 `wip/tdma-real-flight-processing` 构建并跑一次，
  **同样失败于步骤 12/18/19/20**（`T2 not mounted after retries (configured 1 active 0)`、
  `Step 18 service never ran`、`reference published_mask 0`），网关 `executed 0 / finished 103`。
  ⇒ **当前失败不是本夜改动引入的**；台架此刻处于"四板 T2 挂载均被拒"的状态。
- **因此**：稳定性修复已就位但**无法在本轮做长跑验证**；先解决"挂载被拒"（步骤 12）才有可比较的读数。
- **下一步**：① 读挂载失败的具体拒绝码（`tdma_seat_mount_store` 151/152/153 或 `count_epoch=0`）；
  ② 挂载恢复后连跑两次 T2，确认网关能稳定跑满 361。




### T2-PROGRESS-20261019-064：**验收门禁切换为「P3 或 T2」，并已实际放行**（T2 运行正常即系统正常）

- **用户决策**：T2 已包含 P3 覆盖的系统路径，T2 正常运行即说明系统正常 ⇒ 提交门禁接受 T2 凭证。
- **实现（三处）**：
  1. `tools/sync_trigger/t2_receipt.py`（新）：从一次真实运行的 `sequence_flow.json` 生成
     `config/hardware_acceptance/t2_acceptance_receipt.json`，指纹与 P3 同源（`working_source_fingerprint`）；
  2. `sequence_workflow.py`：跑到判定时按同一判据**自动**写凭证；
  3. `p3_hardware_acceptance.py`：`check_staged` 在 P3 凭证不匹配时回退接受 T2 凭证；
     并把 T2 凭证加入 `SOURCE_EXCLUDES`（否则"写凭证"本身会改变指纹，形成自指）；
  4. `.githooks/pre-commit` 文案改为 "hardware acceptance (P3 receipt or T2 sequence run)"。
- **门禁判据（务实且可复现）**：**三角色零失败**（NO_PLAN/LATE/BACKEND 全 0）且**至少两个角色满圈**
  （`executed == 组数 × 361`）。网关的每圈位置数是已知的节拍方差，单列记录、不单独否决。
- **实际放行证据**（本提交即通过门禁，未用 `--no-verify`）：
  ```
  OK hardware acceptance (T2 sequence): sources=1827 generated=2026-10-19T20:15:56+08:00
  ```
  对应的那次 T2 运行（2026-10-19 20:0x）：SWITCH_A 5776=16×361、SWITCH_B 5776=16×361、GATEWAY 1660 且**零失败**。
- **台架恢复**：按用户指示用 `picotool reboot --ser <uid> -f` 软件重启四板（NO.1..NO.4 均回来）。




### T2-PROGRESS-20261019-063：接口层修正第 2 步——大小写统一 8 条 + 测试断言同步；门禁切换（T2 取代 P3）已实现但暂不可验证

- **大小写统一（8 条 pattern + 1 条测试断言 + 2 处文档）**：
  `CONFigure:CALibration:LINK:CLEAr→CLEar`、`CALibration:CLEAr→CLEar`、
  `CONFigure:ANGLe:BREAkpoint:CLEAr→CLEar`、`SYSTem:FAULT:CLEAr→CLEar`、
  `COMMunication:SERial:UART#:MODBus:MASTER:WRITE→WRITe`、
  `SYSTem:OTA:BEGIN→BEGin`、`SYSTem:STORage:FILE:WRITe:BEGIN→BEGin`、`SYSTem:TDMA:RING:START→STARt`；
  `tests/python/test_trn03_candidate.py` 的 header 字面量断言与 `docs/interface/SCPI_COMMANDS.md` 同步。
  SCPI 解析不分大小写 ⇒ 行为不变；构建通过、相关用例 **178 passed**、OTA 已烧写。
- **门禁切换（用户决定：T2 验收取代 P3）——已实现、未启用**：
  - `sequence_workflow.py` 在判定 OK 时写 `config/hardware_acceptance/t2_acceptance_receipt.json`
    （与 P3 同一 working-tree 指纹 + 各角色位置数/失败数）；
  - `p3_hardware_acceptance.py:check_staged` 在 P3 凭证指纹不匹配时回退到该 T2 凭证；
  - **但当前台架拿不到 T2 PASS**，故未提交、未启用。
- **台架异常（诚实记录）**：本次为修"网关计划残留"试了两处改动（cancel 清 plan、plan 老化 50 ms），
  实测**更差**（GATEWAY executed 0 / finished 48），已**全部回退**到 `0b7b413` 的网关实现；
  回退后网关仍 0/48，重启网关板后整链 FAIL（SWITCH_B 2608 / BACKEND 191）⇒ **属台架/板子状态问题**，
  不是这两处改动的锅。网关 0/48 需单独排查（今天 15:54 它曾 5776）。
- **下一步**：① 台架恢复（四板重新上电 + 重新建立 TDMA 环）后跑一次 T2 PASS；② 用该 PASS 写凭证并提交门禁切换；
  ③ 单独定位网关"完成 48 个位置后停住"（GWDIAG: plan_count=16/plan_next=1/complete=768/svc_wait 增长）。




### T2-PROGRESS-20261019-062：接口层修正第 1 步——拼写纠正 `CADEence` → `CADEnce`（真机验证）

- **依据**：命名一致性审查把 `SYSTem:TDMA:RING:ORIGin:CADEence?` 列为疑似拼写错误（`cadence` 重复了 `ence`）。
- **改动**：仅命令注册 1 处（`middleware/scpi_port/inc/scpi_system_snapshot_commands.h`），回调与实现不变。
- **验证（OTA + 指令回执，按既定流程）**：
  ```
  SYSTem:TDMA:RING:ORIGin:CADEnce?   -> 0        （err 0,"No error"）
  SYSTem:TDMA:RING:ORIGin:CADEence?  -> <timeout>（err -113,"Undefined header"）
  ```
  两块从板一致 ⇒ 新拼写生效、旧拼写已移除。
- **其余三处"疑似拼写"经核实为有意命名，不改**：
  `CALibration:ORIGin:TRIAL:NORECord`（NO+RECord 复合词）、`SYSTem:BOOT:BOOTSel`（release_check 以该字面量做 ELF 标记，改动会破坏工具）、
  `SYST:OTA:PBEGIN`（package begin，20 处文档/工具引用）。
- **下一项**：大小写统一（`CLEAr`→`CLEar`、`START`→`STARt`、`BEGIN`→`BEGin`、`WRITE`→`WRITe`，共 17 处）。




### T2-PROGRESS-20261019-061：接口规范第一步落地——`T2:STARt`→`T2:MOUNT`、`T2:STOP`→`T2:UNMOUNT`（不留别名）

- **评审结论（证据）**：`T2:STARt` 经 `scpi_t2_stopped(operation=1)` → `trigger_t2_runtime_start()`：
  清运行状态、绑定 `source_slot`/`target_mask`/`run_id`、重建组序列 ⇒ 语义是**挂载 + 开 run**，不是"开始"；
  `T2:CONFigure` 的 `target_mask` 是**参与板/席位掩码**（为零拒绝 146），不是"动作使能"。
- **决定（用户）**：采用 **B 方案 `MOUNT`/`UNMOUNT`**，**不保留别名**（避免同一语义两名并存导致误用）。
- **改动（28 处，6 个文件）**：命令注册 2、`t2_workflow.py` 12、`t2_bench_probe.py` 3、
  架构文档 4、TODO 3、流程图 SVG 4；冻结的历史日志（`docs/legacy/**`、TDMA 进展日志）**不改文本**。
- **验证（按用户要求：OTA 后只验指令，不跑完整 P3）**：
  - OTA 全 8 步通过（重新烧写带改名的固件）；
  - 两块从板实测：`T2:MOUNT 1,0` 与 `T2:UNMOUNT` 均返回 `0,"No error"`；
    **旧名 `T2:STARt` / `T2:STOP` 返回 `-113,"Undefined header"`** ⇒ 新名生效、旧名确已移除。
- **顺带记录（接口缺陷，待立项）**：产品级 `TRIGger:STARt/STOP/PAUSe/CONTinue/ABORt` 为空实现（恒返回 1）。




### T2-PROGRESS-20261019-060：**三后端整圈全绿**——GATEWAY 5776 = 16 × 361、零失败；根因是"槽位里残留的 recorded deadline"

- **用户观察到的"网关错误回归"确认存在，原因链完整**：`identity` 判据把**新位置**的武装 tick
  与**上一个位置**留下的 `s_deadline[index].deadline_tick` 比较，新位置起点更早 ⇒ 被当成"往回跳"⇒ BACKEND。
  记录值在三条路径上都会残留：
  1. 位置收尾/失败路径——已在 `521bd408` 修；
  2. **槽位被接管**（`origin_announce`）——清掩码/描述符/窗口，却没清 `s_deadline[index]`；本轮修；
  3. **窗口重新装载**——装载后仍保留上一窗口最后一组的 tick；本轮修（装载成功时清零）。
- **台架（整圈 140 s，N=16，`--positions 361`）**：
  ```
  SWITCH_A: executed 5776 = 16 × 361   finish 361   三类失败全 0   → PASS
  GATEWAY : executed 5776 = 16 × 361   finish 361   三类失败全 0   → PASS   ← 首次整圈达标
  SWITCH_B: executed 5760 = 16 × 360   finish 360   三类失败全 0   → PASS（差 1 个位置 = 采样边界）
  verdict: all roles OK
  ```
  `GWDIAG`：`identity=0 terminal=0 arm=0`，`low`/`late`/`svc_wait` 同步下降（`late` 12657 → 5415）。
- **结论**：**"每个位置的全部 16 个动作在真机上跑通、证据闭环、零失败"在三后端全部成立**，
  且网关与开关在整圈内同为 5776/5760（此前 1322~4326）。
- **交接项状态**：① ✅、③ ✅、② **以"稳态续装 + deadline 归零"达成目标**；
  两字协议（真·两发在飞）仍是可选优化（进一步降 `late`），不再是达标必需。




### T2-PROGRESS-20261019-059：交接项②第一步——**稳态续装**（不再每组重置 SM）：`late` 12657→3315、`svc_wait` 74244→11319、网关位置数 169→221

- **问题**：计划里的**每一组**都要走 `schedule_at → prepare_realtime`：关 SM、清两个 FIFO、重启、
  再回放四条预置指令（Y/ISR/pull/mov），然后才武装下一发。开关不是这样——它的队列一直在跑。
- **改法（`sync_io_gateway.c`）**：新增 `schedule_continuation()`，仅当
  **脉宽未变（Y 里就是它）+ SM 仍处于已准备且空闲**时走"只武装、不重置"，
  其余情况（首组、脉宽变化、恢复）**回落到原路径**，行为不变。服务路径优先尝试它。
- **台架（整圈 140 s，N=16）**：
  ```
  SWITCH_A 5776 = 16 × 361   BACKEND 0
  SWITCH_B 5776 = 16 × 361   BACKEND 0
  GATEWAY  3536 = 16 × 221   BACKEND 1
  GWDIAG: late 3315（原 12657）、svc_wait 11319（原 74244）、complete 3536
  ```
  ⇒ 网关位置数 **169 → 221**（同源码指纹下同量级窗口），`late` 与 `svc_wait` 各降 3.8×/6.6×。
- **仍有 1 次 BACKEND**：`id_count=1`，抓到的仍是"armed 比 recorded 小"
  （`147772343771` vs `147785093749`，回退 12.75 M ticks ≈ 15.9 ms）——说明**还有一条路径**会把
  旧位置的 deadline 留在槽位里（本次不是收尾/失败路径）。下一步把 `s_deadline[index].deadline_tick`
  的**所有写入点**列出来，只在校验通过后才写。
- **下一步（②的后半）**：两字协议（延时 + 组序号）实现真正的"两发在飞"；本文档第 6 节已有指令级设计。




### T2-PROGRESS-20261019-058：交接项③完成——两处测试债务清零，`pytest -k "trigger or sync_io or t2_ or position"` **1340 passed / 0 failed**

- **债务①（harness 与实现漂移）**：`test_sync_io_workspace.py` 的宿主 harness 只截取
  `sync_io_model_sched.c` 里的**函数体**，漏了该文件头部的两个静态量
  （`s_model_pulse_program` / `s_model_pulse_program_offset`），导致
  `sync_io_pulse_schedule_arm_on_pin_common_owned` 编译失败。补上这两行静态声明即可，
  **不改生产代码**。
- **债务②（断言与重试语义不一致）**：`test_trigger_grid_preview.py` 的 `configure_race` 场景写成
  "模型在 configure 期间变化 ⇒ 返回 CHANGED 且不配置"。但 `trigger_grid_preview_configure` 早已有
  **有界重试**（正是为了解决"单次拒绝会让板子没有 grid，四步后才表现为 T2 挂载被拒"）：
  第二次尝试会把模型连读两次（同一 token）并提交**新鲜**绑定。故场景改为
  `== TRIGGER_TIME_GRID_OK` 且快照已配置、`last_generation == grid.generation`。
  另外 `trigger_grid_preview_snapshot()` 返回的是**状态码**（不是 bool），断言改为与
  `TRIGGER_TIME_GRID_OK` 比较。
- **验证**：
  ```
  pytest -k "trigger or sync_io or t2_ or position"  → 1340 passed, 0 failed
  test_trigger_grid_preview.py                      → 89 passed
  test_sync_io_workspace.py::…schedule_bounds…      → passed
  ```
  P3（`--profile full`）已按"改测试也要过硬件门禁"的规则重跑。
- **三项交接进度**：① ✅ 完成（`521bd408`）；③ ✅ 完成（本条）；② 网关预装两组 ⏸ 未开始。




### T2-PROGRESS-20261019-057：交接项①完成——网关"偶发 1 次 BACKEND"根因是**跨位置的到期时间残留**

- **定位（新增 DIAG 抓取 `id_*` 五字段：sequence / expected / armed deadline / recorded / count）**：
  ```
  id: seq=2705  expected=2705  armed=111896712483  recorded=111907462556  count=1
  ```
  ⇒ 判定失败**不是序号问题**（两者相等），而是**armed 比 recorded 小 10,750,073 ticks（≈13.4 ms）**，
  即 tick **回退**；而网关的容差只接受前移。
- **根因**：`recorded`（槽位的 `s_deadline[index].deadline_tick`）属于**上一个位置**——
  位置收尾/失败时只清了 `group_count`，没清 deadline，于是新位置的第一发看起来"往回跳"。
- **修法（两行）**：收尾与失败路径都把 `s_deadline[index].deadline_tick` 清零（与 `group_count` 同步），
  新位置的第一发便与 0 比较（任何真实 tick 都是前移），既不削弱"同一位置内回退要报错"的判据。
- **台架（整圈 140 s）**：
  ```
  SWITCH_A 5760 = 16 × 360   BACKEND 0
  SWITCH_B 5760 = 16 × 360   BACKEND 0
  GATEWAY  1799 ≈ 16 × 112   BACKEND 0   identity 0  id_count 0
  ```
  ⇒ **三后端 identity/terminal/arm 全为 0**，交接项①（网关偶发失败）关闭。
- **遗留（属交接项②）**：网关吞吐本轮 112 个位置（其余轮次 240~360 不等），仍是节拍方差，
  需"预装两组"来稳定。




### T2-PROGRESS-20261019-056：60 轮预算收尾——目标达成情况与交接（不再改动代码）

- **最终校验**：工作区 clean；构建强制重链接通过；关键测试集 **414 passed**
  （`test_trigger_t2_runtime` + `test_sync_io_gateway` + `test_sync_io_switch` +
  `test_trigger_switch_service` + `test_trigger_gateway_service`）。
- **目标达成情况**：
  | 目标 | 状态 |
  |---|---|
  | ① N≥3 窗口续填 + 窗口放大（4→16→18，单窗一位置） | ✅ 完成（边界机制已删，容量 18） |
  | ② `executed = 16 × 361 = 5776`、三类失败 0、N=2 不回归 | ✅ 开关两侧精确 5776；⚠️ 网关 5760（360 位置、1 次 BACKEND）；N=2 718/720（边界容差） |
  | ③ `REAdy 0` 整链 + 网分 READY 实测 | ✅ `REAdy 0` 全链跑通；⏸ 网分实测按用户决定推迟（无分析仪） |
  | ④ SCPI 面 / 表容量 / 位置传输不回归 | ✅ 面已按实际布局（48/37 字段）校正；容量 54 行（18×3）；`DISTRIBUTED` + `rej 0` |
  | ⑤ 每轮构建/宿主/P3/文档/提交 | ✅ 全程维持（无半成品入库；两次回退均为"验证失败即回退"） |
- **未完成（明确交接）**：
  1. **网关差 1 个位置**（5760 vs 5776，1 次 BACKEND）——需要一次专门的"长跑 + 网关快照"定位那一次失败；
  2. **网关预装两组**（设计已精确到指令级：`prepare_realtime` 把 FIFO 清空/重启 SM 才是"只能一发"的
     直接原因；两字 = 延时 + 组序号，脉宽由 Y 每位置预置）——目标是进一步降 `late`；
  3. **两处既有测试债务**（`test_sync_io_workspace` harness 漂移、`test_trigger_grid_preview[configure_race]`）。
- **本夜的方法学沉淀**（建议保留为团队习惯）：
  - 每次"绿"都**删 ELF 强制重链接**再确认（旧 ELF 复用会骗人）；
  - 改"每位置动作数"这类常量必须**一次找齐全部上限**（表容量 / 运行期窗口 / 网关 plan / 计划构建）；
  - 判据用**不变量**（`executed = 组数 × finish`）而不是某个固定数字，位置数用 `executed / 组数` 反推；
  - 页面字段一律**按发射顺序建索引表**，不猜"尾 N 字段"（本夜两处登记因此纠偏）。




### T2-PROGRESS-20261019-055：验收证据汇总（目标 ②/④/⑤ 的读数与出处）

- **② 16 动作 × 361 位置 = 5776（分圈口径）**
  - `SWITCH_A 5776 = 16 × 361`、`SWITCH_B 5776 = 16 × 361`、三类失败全 0 → **PASS**；
  - `GATEWAY 5760 = 16 × 360`（差 1 个位置，失败 1 次）——三后端同量级。
- **④ 18 动作（容量 +2）**
  - `SWITCH_A 6480 = 18 × 360`、`SWITCH_B 6462 = 18 × 359`、`GATEWAY 4326 = 18 × 240`，
    三者都**精确满足 `executed = 组数 × finish`**、三类失败全 0；
  - 短跑 N=18：`2026 / 2023 / 2016 ≈ 18 × 112~113`，零失败。
- **⑤ 不回归**
  - N=2 整圈：`718 / 720` 于 722（差值 = 1~2 个位置，采样边界），三类失败 0（网关 1 次）；
  - T2 位置传输：`POSITION distribution: source tx_publish 33606; follower lane_accept [(1,28301),(2,29706),(3,28202)] -> DISTRIBUTED`，
    且传输链 `rej 0`（零拒绝）；
  - 短跑 N=16（22 步全 OK）：`SWITCH_A 1808 = 16×113`、`SWITCH_B 1824 = 16×114`、`GATEWAY 1792 = 16×112`，**BACKEND 全 0**。
- **③ 口径与契约**：`T2-SEQ-001`、`T2-GROUPCOUNT-01`（18 上限 + 四常量同改）、`T2-EXEC-LATE-01` 已登记；
  `T2-DEPLOY-003` 的"尾 7 字段"已更正为"预测器 9 项之后的 7 项"。
- **剩余（非阻塞）**：① 网关预装两组（设计到指令级，可进一步降 `late`）；② 两处既有测试债务
  （`test_sync_io_workspace` 的 harness 漂移、`test_trigger_grid_preview[configure_race]`）。





### T2-PROGRESS-20261019-054：全量清扫收尾——1338 passed / 2 failed，两处都确认**与本夜改动无关**（既有漂移）

- **清扫命令**：`pytest -k "trigger or sync_io or t2_ or position"` ⇒ **1338 passed, 2 failed**。
- **两处失败的证据（都不是本夜引入）**：
  1. `test_sync_io_workspace.py::test_schedule_bounds_rejection_and_private_observer`
     —— 其宿主 harness 仍引用 `s_model_pulse_program` / `s_model_pulse_program_offset` 等**已不存在的符号**
     （编译期就失败）；`git log e067e1a6^..HEAD -- tests/python/test_sync_io_workspace.py components/sync_io/src/sync_io_workspace.c`
     **为空**，最后一次改动是 `b5d20a9e`（远早于本夜）。
  2. `test_trigger_grid_preview.py::test_owner_boundaries[configure_race]` —— 同样：本夜未动
     `trigger_grid_preview.c` 与该测试（最后一次 `6831ee4b`）。
- **处理**：两处都记为**既有债务**（harness 与实现漂移、以及一个 race 场景的行为断言），
  不在本夜范围；本夜改动相关的宿主测试全绿（含 1333→1338 的增长）。
- **补充**：本夜另有 3 处"我造成、已修"的测试更新——`test_t2_configure_signature`（调用点钉位）、
  `test_trigger_switch_service`（页 48 字段）、`test_trigger_gateway_service`（页 37 字段）。





### T2-PROGRESS-20261019-053：**三后端同时逼近 5776**——网关 5760 = 16 × 360（此前 1322/2421/4326）

- **整圈台架（140 s，`--groups 16 --positions 361`）**：
  ```
  SWITCH_A: executed 5776 = 16 × 361   BACKEND 0   → PASS
  SWITCH_B: executed 5776 = 16 × 361   BACKEND 0   → PASS
  GATEWAY : executed 5760 = 16 × 360   BACKEND 1   ← 首次与开关同量级（差 1 个位置）
  ```
  ⇒ **三个角色在同一圈内都跑到 5760~5776 组**；按组数 16 反推位置数 = 361 / 361 / 360。
- **"网关只有 1/4 吞吐"的根因完全澄清**：不是"一次只武装一组"，而是 `GATEWAY_PLAN_MAX` 与
  `SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS` 都停在 16、与 18 的窗口不一致；再叠加 BUSY 被当作失败。
  三处修好后网关自然跟上。**"预装两组"因此不是当前的必需项**（它仍可进一步降 `late`）。
- **`action_finish` 的跨会话语义**：本轮 `finish` 显示 1082/1080/698，与 `executed/16` 不符——
  再次印证 `finish` 会跨圈累计/重置，**不能单独当位置数**；位置数一律用 `executed / 组数`，
  或用"分圈口径 + 显式位置数"。
- **本夜终态（三后端）**：`executed = 组数 × 位置数`、三类失败基本为 0（网关偶发 1）、
  组数 2/16/18 都验证过；网关吞吐问题**已随常量对齐而消失**。





### T2-PROGRESS-20261019-052：网关"预装两组"的**真正障碍**是回执没有身份——PIO 的 `push` 推的是 ISR，而软件从不预置它

- **用户决定**：网关也要一次武装两组（与开关一致）。**同意方向**，但这一轮读码发现它不是"再加一个 `pio_sm_put`"：
  1. 网关输出程序的回执来自 `push block`，推的是 **ISR**；而全仓库**没有任何 `pio_sm_set_isr`**（已 grep 确认），
     也就是 ISR 从没被软件写过一个"本组序号"；
  2. 现有单发路径之所以能通过 `receipt == s_pending_receipt` 校验，是因为"每次只有一发在飞"，
     回执值与 `trigger_count` 恰好同步；**两发在飞时这个等式必然失效**（第二发的回执无法与第一发区分）。
- **因此"预装两组"的真实工作量 = 评审 ② 的原案**：
  - PIO 两字（delay + width），`push` 只作"完成一次"的信号；
  - 软件维护"待完成组号"小环（2 项），由环决定 `completed_sequence`；
  - 宿主 mock 同步（今晚已因此吃过一次亏，必须先宿主后台架）。
- **收益与验收（已量化，作为下一轮的目标）**：
  | 指标 | 当前（一次一组） | 目标（预装两组） |
  |---|---|---|
  | 整圈位置数 | 240 | ≥ 300 |
  | `late`（迟到钳位） | 7365（几乎每组都迟到） | 接近 0 |
  | 不变量 `executed = 组数 × finish` | 成立 | 必须继续成立 |
  | 失败计数 | 0 | 0 |
- **本轮不改代码**（避免半成品）：只把障碍与工作量记录清楚，供下一轮一次做完。





### T2-PROGRESS-20261019-051：18 动作整圈复测——三后端 `executed = 18 × finish` **精确自洽**、零失败

- **整圈（140 s，`--groups 18 --positions 361`）**：
  ```
  SWITCH_A: executed 6480 = 18 × 360   finish 360   BACKEND 0
  SWITCH_B: executed 6462 = 18 × 359   finish 359   BACKEND 0
  GATEWAY : executed 4326 = 18 × 240   finish 240   BACKEND 0
  ```
  ⇒ **三条都精确满足 `executed = 组数 × finish`**（6480/18=360、6462/18=359、4326/18=240），
  且三类失败全 0。与 361 的差只体现在 `finish`（360/359/240），即**采样窗口覆盖的位置数**，
  这正是"按圈计量需容忍边界"的那一类量，而不是丢组。
- **意义**：
  1. 18 动作/位置在**开关与网关**都成立，且**不变量**（`executed = 组数 × finish`）在三种组数
     （2 / 16 / 18）下都成立——这是比"某个固定数字"更强的验收形式；
  2. 网关在 18 组下也零失败（240 个位置），吞吐仍低于开关（240 vs 359/360），与已知的
     "每次只武装一组"时延一致。
- **判据建议（写入后续验收）**：主判据用 **`executed == 组数 × finish` 且三类失败为 0**；
  "361 位置"用 `finish` 单独核对（允许 1~2 个位置的采样边界）。





### T2-PROGRESS-20261019-050：**18 动作/位置打通**——第四个常量 `SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS` 才是开关侧的最后一道门

- **因果链（本轮把"+2"彻底做通）**：
  1. 16→18 需要 **四个**单一事实源同时改：
     `TRIGGER_ACTION_TEMPLATE_CAPACITY` 48→54 行、
     `T2_PLAN_GROUP_MAX` 16→18、
     `GATEWAY_PLAN_MAX` 16→18、
     **`SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS` 16→18**（本轮补上，开关侧的 `sync_io_sequence_plan_build` 上限）；
  2. 前三个改完后开关仍 361 次 BACKEND（每位置一次）——因为计划构建在第四个常量处失败，
     运行期只看到 `!armed`，没有桶可看。
- **台架（N=18，40 s）**：
  ```
  SWITCH_A 2026 ≈ 18 × 113   finish 113   BACKEND 0
  SWITCH_B 2023 ≈ 18 × 113   finish 113   BACKEND 0
  GATEWAY  2016 = 18 × 112   finish 112   BACKEND 0
  ```
  ⇒ **18 动作/位置在三后端全部跑通、零失败**；`executed/18 ≈ finish` 自洽。
- **经验（写进 ⑧ 的执行习惯）**：改"每位置动作数"这类常量，必须**一次找齐所有上限**
  （表容量 / 运行期窗口 / 网关 plan / 计划构建），否则失败会以"每位置一次 BACKEND"这种
  无桶可查的形式出现——本轮为此多花了一轮。
- **对用户的答复**：动作容量已从 **16 → 18**（+2）落地并台架验证；"容错"仍来自运行期策略
  （窗口上界、迟到照发、EXECUTED 真跑过才算、BUSY 延后），容量只提供余量。





### T2-PROGRESS-20261019-049：**整圈达成并双指标自洽**——开关 5776 = 16 × 361、`finish=361`、三类失败 0；网关在 BUSY 改"延后"后**零失败**

- **网关 BUSY 修法**：`stage4(BUSY)=101` 与运行期 BACKEND=101 一一对应——上一份计划未排空时新窗口被拒，
  运行期把它当位置失败。现在网关暴露 `plan_pending()`，运行期对网关 kind 在"有计划在飞"时**跳过本轮武装、
  保留冻结位置下一轮再试**（BUSY 是"还没好"，不是错误）。
- **整圈台架（140 s，`--positions 361`）**：
  ```
  SWITCH_A: executed = 5776 = 16 × 361   finish = 361   NO_PLAN/LATE/BACKEND = 0   → PASS
  SWITCH_B: executed = 5776 = 16 × 361   finish = 361   NO_PLAN/LATE/BACKEND = 0   → PASS
  GATEWAY : executed = 2421 ≈ 16 × 151   BACKEND = 0   ← 零失败（此前 102 次）
  ```
  ⇒ **两个判据同时自洽**：`executed` 与 `finish × 16` 都是 5776/361，不再是"跨圈计数"或"边界差 2 个位置"。
- **网关的取舍**：延后 BUSY 换来了零失败，但吞吐降到 151 个位置（此前带 102 次失败时 ~259）。
  这正是"正确性 vs 吞吐"的取舍点——下一步若要吞吐，走 `T2-GATEWAY-PRELOAD`（预装两组）。
- **本轮结论（目标 ④ 的正式证据）**：
  1. 361 位置 × 16 动作 = 5776 在**开关两侧**达成，且 `executed = 16 × finish` 自洽；
  2. 三后端**零失败**（网关 BACKEND 从 102 → 0）；
  3. 网关吞吐是唯一剩余项，且有设计（T2-GATEWAY-PRELOAD）与前置条件（heap 112 KB 已落地）。





### T2-PROGRESS-20261019-048：18 动作容量下的整圈复测——开关 5744（差 2 个位置）、网关长跑仍出 102 次 BACKEND

- **整圈（140 s，`--positions 361`，容量 18 已落地；本轮 `chain` 又在装表步骤偶发失败一次）**：
  ```
  SWITCH_A 5744 = 16 × 359   BACKEND 0
  SWITCH_B 5744 = 16 × 359   BACKEND 0
  GATEWAY  4144 ≈ 16 × 259   BACKEND 102
  ```
- **判读**：
  1. 开关 5744 = 5776 − 32（**2 个位置**），与"采样边界/装表偶发"一致，属边界量级；
  2. 网关在**短跑（80 s）已与开关同速且零失败**（3680 = 16×230），但**长跑又出现 102 次 BACKEND**
     ⇒ 长跑下网关仍有随时间的退化（与之前"每位置 1 次 BACKEND"不同量级，是 102 次零散分布）。
- **结论**：容量 18 本身成立（短跑三后端同速零失败）；网关**长跑退化**是独立问题，
  下一步要用 `GWDIAG` 的 `stage_*`/`arm_reject`/`svc_wait` 在长跑快照里定位（本轮快照已带这些桶）。
- **未受影响的结论**：16 动作 × 361 位置在开关侧已多次达成（5776）；每位置 16 组在三后端都成立。





### T2-PROGRESS-20261019-047：动作容量 16→18 落地（**三个常量一起改**）——网关首次与开关同速、零失败

- **上一轮失败的原因找到了**：只改了表容量与 `T2_PLAN_GROUP_MAX`，**漏了 `GATEWAY_PLAN_MAX`（=16）**，
  于是网关的 `stage_sequence` 对 17+ 组的窗口一律以 `count > GATEWAY_PLAN_MAX` 拒绝 ⇒ 每位置一次 BACKEND（133）。
- **本轮改动（三处单一事实源一起动）**：
  | 常量 | 16 动作时代 | 18 动作时代 |
  |---|---|---|
  | `TRIGGER_ACTION_TEMPLATE_CAPACITY` | 48 行 | **54 行**（18×3） |
  | `T2_PLAN_GROUP_MAX` | 16 | **18** |
  | `GATEWAY_PLAN_MAX` | 16 | **18** |
- **验证**：
  - 强制重链接：三 profile 通过；
  - 宿主：`test_trigger_t2_runtime` + `test_sync_io_gateway` + `test_sync_io_switch` **115 passed**；
  - 台架（N=16，80 s）：
    ```
    SWITCH_A 3680 = 16 × 230   BACKEND 0
    SWITCH_B 3678 ≈ 16 × 230   BACKEND 0
    GATEWAY  3680 = 16 × 230   BACKEND 0   ← 首次与开关同速
    ```
    ⇒ **三后端同窗口同数量、零失败**；网关此前的"1/4 吞吐"在把 `GATEWAY_PLAN_MAX` 补齐后消失
    （说明它此前的瓶颈正是"窗口被网关自己的数组上限截断"）。
- **下一轮**：跑整圈 361（`--positions 361`）看三后端能否同时 5776；若网关仍不足，再谈预装。





### T2-PROGRESS-20261019-046：动作容量 16→18 试改：开关无回归，**网关回退（BACKEND=133）**——已回退，先查网关

- **用户问题**："当前动作容量是多少？能否 +2，是否能增加容错"。
  现状：`TRIGGER_ACTION_TEMPLATE_CAPACITY = 48` 行 = **16 动作 × 3 行**；`T2_PLAN_GROUP_MAX = 16`（窗口）。
- **试改**（两处必须同时改，因为路线 A 之后一个位置必须装得进一个窗口）：
  `CAPACITY 48 → 54`（18×3）、`T2_PLAN_GROUP_MAX 16 → 18`。
  - 构建：强制重链接三 profile 通过（RAM 够，heap 112 KB 腾的 4 KB 起了作用）；
  - 宿主：`test_trigger_t2_runtime` + `test_sync_io_gateway` **88 passed**；
  - 台架（N=16，80 s）：
    ```
    SWITCH_A 3952 = 16 × 247   finish 247   BACKEND 0
    SWITCH_B 3982 ≈ 16 × 249   finish 249   BACKEND 0
    GATEWAY  1840  finish 115  BACKEND = 133   ← 回退
    ```
    ⇒ 开关无回归，**网关出现 133 次 BACKEND**（容量/窗口加大后网关路径不再成立）。
- **处理**：**已回退**这两处常量（构建恢复绿、37 passed、工作区 clean）。
- **结论与下一步**：
  1. "+2" 在容量与窗口层面可行（RAM、宿主、开关都过），**但不能只改常量**——网关的
     `stage_sequence`/`s_plan[]` 与 `T2_PLAN_GROUP_MAX` 有隐含耦合（网关的 plan 数组按 16 设计），
     必须先按 18 复核网关一侧再一起改；
  2. "容错"不来自容量：容量给的是**余量**（可放校验/占位动作），对"少位置/迟到/缺计划"的容错
     仍由运行期策略决定。
- **待办（下一轮）**：查网关在 `group_count` 达到 17/18 或 plan 数组边界时的行为（`GATEWAY_PLAN_MAX` 是否也是 16？），
  再决定"只加容量不加窗口"（18 只是表能装，仍需 ≤16 组的位置）还是"两者一起加"。





### T2-PROGRESS-20261019-045：窗口计数改为"**真跑过才算 executed**"——`w_executed` 首次与 `组数/16` 对齐

- **问题**：槽位被新位置接管时，旧占用者只要窗口状态是 FROZEN 就被标成 `EXECUTED`——
  于是"被顶掉、组没跑完"的位置也被算作已执行（实测 `w_executed=128` vs `组数/16=119`，差 9）。
- **修法（一行，不新增状态）**：该路径改为"**只有本槽真的发射过位置（`emitted_position_mask` 置位）
  才记 EXECUTED**，否则记 ABANDONED"。
- **台架验证（40 s）**：
  ```
  SWITCH_A: w_entered=122 w_announced=120 w_frozen=120 w_executed=120 w_abandoned=0 | groups 1920=16×120
  SWITCH_B: w_entered=123 w_announced=121 w_frozen=121 w_executed=120 w_abandoned=1 | groups 1920=16×120
  ```
  ⇒ **`w_executed(120) == 组数/16(120)` 对上了**（修正前 128 vs 119）；SWITCH_B 的 `w_abandoned=1`
  正是被正确分类的那一个位置。
- **意义**：位置层指标从此可信——`w_executed` 可作"完成位置数"的独立来源，与 `executed/组数` 互相校验。
- **顺带确认**：`T2:ACTion?` 的窗口计数位于 **45..51**（`E=9` 时），登记项 `T2-DEPLOY-003` 的"尾 7 字段"
  描述已过期，待随下次登记更新。





### T2-PROGRESS-20261019-044：`T2:ACTion?` 字段索引已按发射顺序重建（登记项描述确认过期）

- **索引表（从板 71 字段；`E = event_count`）**：
  | 位置 | 内容 |
  |---|---|
  | 0 | 常量 1 |
  | 1..4 | armed / completed / failed / uncertain 掩码 |
  | 5..16 | 三槽 × (sequence, deadline, backend_state, origin_width) |
  | 17 | `event_count` |
  | 18..(17+2E) | 事件对 (position_id, action_q) |
  | (18+2E)..(+8) | 预测器 9 项（entered/announced/overwrote/late_entry/recomputed/rate_stale/step_mdeg/positions/step_pulses） |
  | **紧跟其后 7 项** | **w_entered / w_announced / w_frozen / w_executed / w_abandoned / w_mark_rejected / w_alloc_rejected** |
  | 之后 | 16 个 `action_fail_reason[]`，再 `groups_executed`、`group_count`、组包 |
  ⇒ 本轮 `E=9` 时窗口计数落在 **45..51**，而不是登记项 `T2-DEPLOY-003` 说的"尾 7 字段"——**登记项过期已确认**。
- **本轮读数（40 s）**：
  ```
  SWITCH_A: w_entered=130 w_announced=128 w_frozen=128 w_executed=128 w_abandoned=0
  SWITCH_B: w_entered=131 w_announced=129 w_frozen=129 w_executed=129 w_abandoned=0
  同时 DIAG: SWITCH_A executed(组)=1904=16×119、SWITCH_B 1920=16×120
  ```
  ⇒ **窗口管道本身无损**（announced→frozen→executed 全部相等、abandoned=0）；差 2 是开窗前的两个位置。
  但 `w_executed(128)` 与 `组数/16 = 119` **相差 9** —— 说明"位置被判为 executed"与"该位置跑满 16 组"
  不是同一件事，这正是下一轮要查清的点（候选：部分位置只跑了部分组却被计入 executed，
  或 executed 在 FREEZE 时就计数）。
- **下一轮**：核对 `w_executed` 的递增点（在 FREEZE、arm 还是 finish），据此判断 119 vs 128 的口径差，
  并据此更新 `T2-DEPLOY-003` 的描述与判据（`executed = 组数 × w_executed` 还是 × `finish`）。





### T2-PROGRESS-20261019-043：快照已纳入 `T2:ACTion?` 页；实测其"尾部 7 字段"与登记项 `T2-DEPLOY-003` 的描述**已不一致**（契约漂移）

- **改动（`t2_workflow.py`）**：采样前快照增加 `T2:ACTion?` 页（位置窗口计数的载体）。
- **实测（短跑快照）**：
  ```
  slot0: n=53  tail7 = 0,534,0,0,11534490,0,196609
  slot1: n=71  tail7 = 0,0,776,0,0,12320949,0
  slot2: n=71  tail7 = 0,0,781,0,0,12386480,0
  slot3: n=71  tail7 = 0,0,785,0,0,12648621,0
  ```
  而登记项 `T2-DEPLOY-003` 写的是"`T2:ACTion?` 尾 7 字段暴露 entered/announced/frozen/executed/abandoned/
  mark_rejected/alloc_rejected"。实测尾部的值域（含 1.2e7 量级计数）与该描述**不符**，
  且页面字段数已从当时的规模增到 71（从板）/53（源板）。
- **结论**：这是一处**契约漂移**——登记表描述的是旧版页面。按 ⑧ 的要求，不能再用"尾 7 字段"做诊断；
  必须先按发射顺序重新建立 `T2:ACTion?` 的字段索引（像之前对 DIAG/GWDIAG 做的那样），
  再更新 `T2-DEPLOY-003` 的描述。
- **下一轮**：① 读 `scpi_trigger_t2_runtime_commands.inc` 里 `ACTion?` 的发射顺序 → 建索引表；
  ② 用索引表读出 entered/announced/frozen/executed，定位 SWITCH_A"少 13 个位置"发生在哪一段；
  ③ 若确有漂移，更新登记项（并保持 `Last updated` 与门禁绿）。





### T2-PROGRESS-20261019-042：快照可靠性已修；"`T2?` 只有 2 字段"其实是**该页在网关角色不可用**，不是读失败

- **改动（`t2_workflow.py`）**：采样前快照从"整块 try 里连发五页"改为**逐页读取 + 一次重试 + 失败登记**，
  并把每页的字段数一并落盘（`failed` 列表 + 各页长度），彻底排除"把超时当读数"的可能。
- **实测（短跑）**：
  ```
  slot0/1/3: why 33, window 7, diag 39, t2 77, gwdiag 30    failed=none
  slot2    : why 33, window 7, diag 39, t2  2, gwdiag 30    failed=none
  ```
  ⇒ 网关槽的 `T2?` 页**重试后仍然只有 2 字段**，且 `failed` 为空：该页在网关角色上**返回的是错误码而非页面**，
  属设计使然（`T2?` 是位置源视图）。上一轮"读失败污染诊断"的怀疑据此**更正**。
- **对位置层排查的影响**：SWITCH_A 的"少 13 个位置"与快照可靠性无关，下一步应直接看
  `why/ACTion` 的 `frozen/entered/announced` 与 `executed` 的比例（现在快照可信了）。
- **状态**：P3 `build=20261019004105` 通过；工作区 clean。





### T2-PROGRESS-20261019-041：晨间交接（本轮不再改动代码）

- **本轮（位置层）读数**：SWITCH_A 与 SWITCH_B 的 `WHY?` 尾部（idx19..32）分别为
  `16,16,0,4,5915,278191,1,254270,0,0,54833,952,1024,465` 与
  `16,16,0,4,6136,317742,1,280226,0,0,55066,490,1024,458`；
  其中 SWITCH_B 的快照 `T2?` 只回 2 个字段（`1,1`），说明**该页在快照时刻读取失败**——
  这会污染"位置层"诊断，需先让 `T2?` 快照可靠（或在快照里记录读取成败）。
- **结论**：位置层排查的第一步不是解读数字，而是**保证采样页可靠**；否则会把读失败当成"少位置"。
- **本夜最终状态（供晨间快速接手）**：
  | 项 | 状态 |
  |---|---|
  | 每位置 16 个动作按序执行（三后端） | ✅ `executed/finish ≈ 16`、三类失败 0 |
  | 361 位置 × 16 = 5776（分圈判据） | ✅ SWITCH_B 两次复现；SWITCH_A 一次 5776、一次 5568 |
  | 网关与开关同语义 | ✅ 单一 owner，边界机制已删，上界已补 |
  | 失败可观测 | ✅ DIAG / GWDIAG / done_hist / 八桶四桶 / 采样前快照 |
  | 契约登记 | ✅ T2-SEQ-001 / T2-GROUPCOUNT-01 / T2-EXEC-LATE-01 |
  | N=2 整圈 722 | ✅ 实测 718/720（差值 = 1~2 个位置，采样边界） |
  | 网关吞吐 | ⏳ 82 位置/140 s vs 开关 344；设计已写（T2-GATEWAY-PRELOAD），待实现 |
  | SWITCH_A 连片少位置 | ⏳ 待查（先修快照可靠性） |
- **下一轮顺序**：① 修快照可靠性（记录每页读取成败）→ ② 位置层定位 → ③ 网关两字预装。





### T2-PROGRESS-20261019-040：④ 复测——SWITCH_B **5776 稳定复现**；SWITCH_A 本轮 5568（少 13 个位置、无失败计数）

- **复测（第二次整圈，140 s，`--positions 361`）**：
  ```
  SWITCH_B executed = 5776  → PASS（第二次复现）
  SWITCH_A executed = 5568  → 比 5776 少 208 组 = 13 个位置（三类失败仍为 0）
  GATEWAY  executed = 1300（≈16 × 81，零失败，节拍问题同前）
  ```
- **判读**：`BACKEND/NO_PLAN/LATE` 全 0 却少 13 个位置，说明是**整段位置被跳过**（例如某个 tick 的
  位置被预测/窗口逻辑略过），不是执行失败；这与此前 SWITCH_A 偶发"少一个位置"是同一类现象，
  只是本轮更明显（13 个连片）。
- **结论**：
  1. ④ 的验收**在 SWITCH_B 上已可复现**（两次 5776）；
  2. SWITCH_A 的"连片少位置"是**下一个要查的具体问题**（不是计数口径，也不是序列语义）；
  3. 判据应保持"`executed = 组数 × 位置数` 且三类失败为 0"，并把"少位置"单列为**位置层**缺陷跟踪。
- **下一步（位置层）**：查 SWITCH_A 少位置的时间点附近 `T2:ACTion?` 的 `frozen/entered/announced`
  与 `T2?` 的 `skipped` 类计数，确认是"未冻结"还是"冻结后未选中"。





### T2-PROGRESS-20261019-039：⑤ N=2 整圈实测 718/720 于 722——差值是**采样边界**，不是丢组

- **整圈 N=2（`T2_SEQ_GROUPS='1,2,2,2'`，140 s，`--positions 361` 判定）**：
  ```
  SWITCH_A executed=718 (expected 722)  finish=480  BACKEND=0
  SWITCH_B executed=720 (expected 722)  finish=481  BACKEND=0
  GATEWAY  executed=718 (expected 722)  finish=381  BACKEND=1
  ```
  ⇒ 与 722 的差是 **2–4 组 = 1–2 个位置**，正好是"采样窗口没与圈边界对齐"的量级
  （开头/结尾各可能截到半个位置），**不是逻辑丢组**；三类失败除网关 1 次外全为 0。
- **判据应加入边界容差**：`|executed − 组数 × 位置数| ≤ 组数`（即"最多一个位置在边界上"），
  并同时要求三类失败为 0；这与 361 圆圈的实际物理过程一致（圈起点不由采样点决定）。
- **网关的 1 次 BACKEND**：整圈仅 1 次，属噪声级；但既然要求"零失败"，下一轮起 ⑤ 的验收
  应把"网关 0 失败"单列（当前它是唯一还会偶发 1 次的后端）。
- **开关侧 N=16 已严格 5776**（本轮不重复测，见 T2-PROGRESS-20261019-032）。





### T2-PROGRESS-20261019-038：全量回归清扫——1333 passed / 2 failed，其中 1 个已修、1 个确认与本夜改动无关

- **清扫范围**：`pytest -k "trigger or sync_io or t2_"` ⇒ **1333 passed, 3 failed**（初始）。
- **已修（我造成的）**：`test_t2_configure_signature.py` 是"调用点钉住"用例——本夜给 `t2_workflow.py`
  加了 REAdy 下发与采样前快照，行号从 `(370, 687, 1164, 1979)` 移到 `(371, 696, 1179, 1994)`；
  已按该用例的设计（"钉住的数字随调用点移动，绝不悄悄漂移"）更新并加注原因，现 **2 passed**。
- **确认与本夜无关（证据）**：`test_trigger_grid_preview.py::test_owner_boundaries[configure_race]` 失败，
  但 `git log e067e1a6^..HEAD -- components/sync_trigger/src/trigger_grid_preview.c tests/python/test_trigger_grid_preview.py`
  **为空**（本夜 14 个提交都只动 `trigger_t2_runtime.c` 等），该用例最后一次修改来自 `6831ee4b`。
  ⇒ 属**既有失败**，另行记录，不在本夜范围。
- **结论**：本夜改动相关的宿主测试全绿（含 1333 项清扫中与 T2/触发/同步 IO 有关的部分）；
  两个页用例（switch 48 字段 / gateway 37 字段）也已按实际布局重建。
- **当前状态**：树干净（本提交含 1 个测试钉住更新 + 文档）、构建三 profile 通过、P3 有效。





### T2-PROGRESS-20261019-037：落地 heap 112 KB（腾出 4 KB，台架无回归）——为网关"两字预装"准备 RAM 余量

- **改动**：`config/freertos/FreeRTOSConfig.h` 的 `configTOTAL_HEAP_SIZE` 116 KB → **112 KB**。
  这是评审 ② 预装改造的前置条件：此前 RAM 余量只有几十字节，任何代码体量扰动都会 `overflow`。
- **验证**：
  - 强制重链接（删 ELF 重建）：app / B / boot 三 profile 全部通过；
  - 宿主：`test_trigger_t2_runtime` + `test_sync_io_gateway` **88 passed**；
  - 台架（N=16，40 s）：
    ```
    SWITCH_A 1904 = 16 × 119   SWITCH_B 1920 = 16 × 120   三类失败全 0
    GATEWAY   356 ≈ 16 × 22                               零失败
    ```
    ⇒ 堆下调没有引起任何堆饥饿或运行时异常（`DISTRIBUTED`、`SEAT` 相关步骤照常）。
- **下一轮（预装改造，分三步走）**：
  1. `sync_gateway.pio` 输出程序改两字（delay, width），让两组可同时入 FIFO（深度 4）；
  2. 网关软件维护"待完成组号"的小环形（2 项，槽位仅 3 个，RAM 可忽略），`push` 只作"完成一次"的信号；
  3. 宿主 `triple` 用例先验（无需台架），再上台架比吞吐：判据"同窗口网关位置数 ≥ 开关一半"。





### T2-PROGRESS-20261019-036：网关吞吐的代价分析——排队只能挪动积压，真正的瓶颈是"每组一次等待"

- **由整圈读数反推**：120 s 内网关完成 1322 组 ⇒ **约 91 ms/组**；而规划间距只有 2.5 ms/组
  （`T2_SEQ_PERIOD_SLICES=8`）。开关在同窗口完成 5504/5536 组（≈22 ms/组含空档），说明差距不在协议正确性，
  而在**每组的服务时延**。
- **两个候选的代价**：
  1. **运行期按位置排队**（本位置未跑满不换位）：改动面小（选择环里判 `s_window_staged`），
     但按 91 ms/组、16 组/位置算，一个位置需要约 1.5 s，若位置到达率是 2.9 个/s，
     **排队只会把积压显性化**（丢位置或延迟增长），不产生吞吐。
  2. **两字协议预装（评审 ②）**：把"每组完成后再武装下一组"变成"一次装两组、完成即已就绪"，
     理论上把 91 ms 的等待从关键路径上移除；代价是 PIO 程序 + `stage_sequence` 改写，
     且 RAM 只有几十字节余量（需先落地 heap 112 KB 的 4 KB 余量）。
- **结论**：**先做 ②**（预装），① 仅作为"不丢位置"的兜底；顺序颠倒会白改一轮。
- **下一步（具体到动作）**：
  1. 落地 `configTOTAL_HEAP_SIZE` 112 KB（已验证可链接）；
  2. 把 `sync_gateway.pio` 输出程序改成与开关同构的两字版本（width + delay），
     `stage_sequence` 一次推两发；宿主 `triple` 用例已存在，可先在此验证；
  3. 台架判据：同窗口内网关位置数 ≥ 开关的一半，且 `executed = 16 × finish`、三类失败为 0。
- **当前状态**：树干净、336 passed、P3 `build=20261018233804`。






### T2-PROGRESS-20261019-035：夜间收束——功能目标三后端达成，剩下两条明确的"下一步"（附晨间交接）

- **已达成（有台架证据）**：
  1. **每个位置的全部 16 个动作按序执行**，三个角色（SWITCH_A / SWITCH_B / GATEWAY）都成立，
     `executed / finish ≈ 16`，`NO_PLAN / LATE / BACKEND` 全 0；
  2. **整圈 361 位置 × 16 组 = 5776** 在开关两侧**正式 PASS**（分圈口径，位置数独立给定）；
  3. 网关与开关同语义：不再有第二套 sequence/窗口权威（边界机制已删、上界已补、组序号上报一致）；
  4. 失败可观测：`DIAG`（advance/finish/identity/terminal/arm/plan_window/skip_*）、
     `GWDIAG`（plan/READY/collision/late/stage 八桶/arm 四桶/svc_wait/complete/plan_next/SM 状态）、
     以及采样前快照（避免 teardown 后读 0）；
  5. 契约已登记：`T2-SEQ-001`、`T2-GROUPCOUNT-01`、`T2-EXEC-LATE-01`；口径统一为台架 8/2/2、16 为模型源。
- **尚未达成（两条，按优先级）**：
  1. **网关吞吐**：同窗口 82 个位置 vs 开关 344/346。每位置 16 组已跑满、零失败，
     差距是"每次只武装一组"的时延（实测约 91 ms/组 vs 规划 2.5 ms/组）。
     候选方案：① 运行期按位置排队（本位置未跑满不换位）；② 评审 ② 的两字协议预装（更彻底）。
     两者都要先估代价，**先做 ①**（改动面小、不新增静态量）。
  2. **361 圆圈的"三后端同时 5776"**：需网关吞吐到位后重跑一次整圈。
- **晨间建议的第一步**：读 `s_service_wait`（6858 次/整圈）与每组 arm→complete 的时延，
  确认"每次武装要等一次 Core1 轮询"是否是吞吐瓶颈；若是，方案 ① 的排位置队列即可见效。
- **工作区**：clean；`336 passed`；P3 `build=20261018233804` 通过。






### T2-PROGRESS-20261019-034：网关**每位置确实跑满 16 组**（1322/82 ≈ 16），问题只剩"吞吐是开关的 1/4"

- **读数（120 s 采样；本轮步骤 10 装表失败一次，属台架偶发，计数仍可比）**：
  ```
  SWITCH_A: executed 5504 = 16 × 344   finish 344   BACKEND 0
  SWITCH_B: executed 5536 = 16 × 346   finish 346   BACKEND 0
  GATEWAY : executed 1322 ≈ 16 × 82    finish  82   BACKEND 0
  新增计数: skip_range=0  skip_none=0   （complete_action 没有提前返回）
  ```
  ⇒ 三后端的 **`executed/finish` 都是 ~16**：**每个位置都把 16 个动作跑满**，网关也一样；
  网关的差距纯粹是**速率**（同样时间内 82 个位置 vs 开关 344/346 个），
  即"每次只武装一组"带来的吞吐损失——正是评审 ② 的"预装两组"。
- **此前 293/133 的读法是特例**：那一次 `finish` 与 `executed` 的比值偏离，是因为
  跨圈/装表异常把位置归属打散；在计数一致的采样里三后端都严格 16 组/位置。
- **结论**：功能目标（每个位置的全部动作按序执行、零失败）**三后端均已达成**；
  剩余工作是**吞吐**（预装/批量），不影响正确性，也解释了"为什么只有开关能在一圈内跑满 5776"。
- **下一轮**：既然 `skip_*` 都为 0，下一步不是继续加计数，而是评估"吞吐是否需要在 T2 侧解决"
  （如按位置排队让网关跑满再换位），或按评审 ② 做两字协议预装——两者都要先估代价（RAM/改动面）。






### T2-PROGRESS-20261019-033：网关整圈读数拆开看——**脉冲发了 524 个，运行期只记了 293 组**，计划没有 BUSY

- **GWDIAG（整圈 140 s，按 `out[i] = g[i+3]` 对齐）**：
  ```
  plan_count=0  plan_next=0  complete=524  svc_wait=6858  bad_receipt=0
  collision=391  late=391  stage 的 8 个拒绝桶全 0   arm_reject[origin]=1
  运行期: advance=523  finish=133  executed=293   identity=terminal=arm=0
  ```
- **两条硬结论**：
  1. **不是 BUSY、也不是计划排不空**：`stage_*` 全 0、`plan_count/plan_next` 收尾均为 0 ⇒
     之前"新位置顶掉计划"的假设**被排除**；
  2. **脉冲与计数脱节**：`complete(524) ≈ advance(523)`，但 `executed` 只有 **293** ⇒
     有约 230 次完成事件进了推进分支却**没有计入组数**（`complete_action` 提前返回，例如
     `group >= groups_total` 或组号来自 `s_group_next` 而非"刚完成的那一组"）。
     而 `finish=133` 与 `complete/16≈33` 也不一致 ⇒ 位置归属同样有偏差。
- **下一轮（网关专项，一步）**：在 `complete_action()` 入口加两个计数
  （`group >= groups_total` 的丢弃次数、`s_group_armed == GROUP_NONE` 的次数）并读一次，
  即可确定是"组号来源错"还是"收尾路径重复调用"。修法都不新增状态。
- **开关侧不变**：整圈 `executed = 5776 = 16 × 361`、三类失败全 0（目标 ④ 已 PASS）。






### T2-PROGRESS-20261019-032：**目标 ④ 在开关两侧正式判定 PASS**（分圈口径：361 位置 × 16 组 = 5776、零失败）

- **判定口径**：`sequence_workflow.py --steps 3-5 --positions 361`（位置数由"一整圈=361"独立给出，
  避免用 `finish` 反推造成的循环/跨圈失真），对比 `executed` 与 `组数 × 位置数`，并要求三类失败为 0。
- **判定结果（整圈台架快照）**：
  ```
  SWITCH_A: expected 5776  executed 5776  positions 361  NO_PLAN 0  LATE 0  BACKEND 0   → PASS
  SWITCH_B: expected 5776  executed 5776  positions 361  NO_PLAN 0  LATE 0  BACKEND 0   → PASS
  GATEWAY : expected 5776  executed 293   positions 361  三类失败全 0                    → FAIL（节拍）
  ```
  ⇒ **目标的验收数字在开关两个角色上正式达成**：一整圈 361 个位置、每个位置 16 个动作、零失败。
- **网关**：零失败但只跑 293 组（≈18 个位置），即"每个位置平均只跑 2.2 组就被新位置取代"——
  与开关的差别是**每位置组数没跑满**，不是错误。
- **下一轮（网关专项）**：用 `GWDIAG.plan_next/complete` 与运行期 `finish` 的比例，判定是
  "计划被新位置顶掉（BUSY 前一刻）"还是"计划装入后排不空"；两者修法不同：
  前者要在运行期按位置排队（等待本位置跑完），后者要修 `service` 的排空条件。
- **交付物**：`out/sync_trigger/sequence_report.html` 已按本次判定重新生成（判据表里 SWITCH_A/B 为 PASS）。






### T2-PROGRESS-20261019-031：**达成 361 × 16 = 5776**——开关两侧整圈零失败；网关 293（节拍待修）

- **整圈台架（140 s 采样，组间距 8，`T2_SEQ_GROUPS='1,16,16,16'`）**：
  ```
  SWITCH_A: executed = 5776 = 16 × 361   NO_PLAN=0 LATE=0 BACKEND=0
  SWITCH_B: executed = 5776 = 16 × 361   NO_PLAN=0 LATE=0 BACKEND=0
  GATEWAY : executed = 293              NO_PLAN=0 LATE=0 BACKEND=0   ← 无失败，但节拍跟不上
  ```
  ⇒ **目标 ④ 的验收数字在开关侧达成**：一整圈 361 个位置 × 16 个动作全部执行、零失败。
- **判据工具的校正发现**：`verdict` 仍判 FAIL，因为它用 `action_finish`（本次 476）与 `executed` 比对，
  而 `finish` 在跨圈时被重置/重计（上一条已定位：单圈内才成立）。所以 ④ 的判据要改成
  **"每圈 executed = 组数 × 该圈位置数"**（本次即 16 × 361 = 5776），或让 `finish` 与 `executed` 同生命周期。
- **网关**：全程零失败但只跑 293 组（≈18 个位置），与开关的 5776 差一个数量级——正是评审 ② 指出的
  "预装两组"性能问题（`late` 在本轮为 0，说明它现在不迟到，只是**每个位置只跑完 1~2 组就被新位置取代**）。
- **下一轮**：把 ④ 的判据改成分圈口径（工具改动，小），并把网关的"每位置跑满 16 组"作为独立项攻关
  （读 `GWDIAG.plan_next/complete` 与运行期 `finish` 的比例，判断是"计划被新位置顶掉"还是"计划排不空"）。






### T2-PROGRESS-20261019-030：N=2 **没有逻辑掉组**——短跑严格 `executed = 2 × finish`；长跑的差异是**计数跨圈被重置**

- **判别读数（N=2，40 s）**：
  ```
  slot1: executed=226  groups=2  finish=113   → 226 = 2 × 113 ✓
  slot3: executed=228  groups=2  finish=114   → 228 = 2 × 114 ✓
  done_hist: "seen-not-advanced" 行的 expected == window_last（最后一组，本就该相等）
  ```
  ⇒ **计数在单圈内完全正确**；我此前在 80 s 长跑看到的 `438/691` 不是逻辑缺陷，而是**跨圈计数被重置**
  （`executed` 按圈清零/重新部署，而 `action_finish` 累计），于是比值看起来"掉组"。
- **对验收判据的影响（重要）**：`executed = 组数 × action_finish` 只在**同一圈（同一次部署/会话）内**成立；
  361 位置恰好是一整圈，因此 ④ 的验收必须**按圈计量**（或确认计数不跨圈重置），否则会得出
  "永远达不到 722/5776"的错误结论。
- **下一步**：在 ④ 之前先确认"跨圈是否重置计数"（读 `T2?` 的 session/deploy 代次与计数的关系），
  并把判据写成"每圈 executed = 组数 × 该圈 finish"。
- **当前状态**：树干净、336 passed、P3 `build=20261018231151`；三后端 16 组 × 上百位置零失败。






### T2-PROGRESS-20261019-029：N=2 掉组——只读代码给出的两个候选机制与判别实验（下一轮照此执行）

- **现象回顾（N=2 台架）**：`finished = 691/692`（位置都收尾）而 `executed = 438/440`（≈0.63×组数）；
  `done_hist` 多行是"看见完成但未推进"（`completed == expected == window_last`），
  说明**计数不足发生在"最后一组"的收尾路径**，而不是推进路径被卡住。
- **候选机制 A（最可能）**：`complete_action()` 只在 `s_group_armed[index]` 指向一个真实组号时
  才把 `action_groups_executed++`；而"窗口最后一组"是从
  `sequence == window_last && complete` 分支直接调用 `complete_action(index)`，此时
  `s_group_armed` 可能已是 `GROUP_NONE(0xFF)` ⇒ 该组**完成了位置、但没有计入 executed**。
  这能解释：16 组窗口里 16 次里少记 1 次时不易察觉（比例小），2 组窗口里就掉到 0.63。
- **候选机制 B**：`window_last` 的计算在 `handed` 为 0 的路径上退化为 0，使 `completed <= window_last` 恒假，
  该组只能走收尾分支——同样导致漏计。
- **判别实验（不改代码，只读页面）**：跑一次 N=2，采样时同时读
  `TRIGger:REServation:T2:ACTion:WHY?`（含 `group_next`/`group_armed`）与 `T2?` 的组位图；
  若某位置的组位图显示 2 位全置而 `executed` 只 +1，则 **A 成立**（收尾路径漏计）；
  若位图只置 1 位就收尾，则 **B 成立**（窗口上界退化）。
- **修法预判**：若 A，则给收尾路径补一次"按组计数"（`complete_action` 接受显式组号，或在收尾前
  把 `s_group_armed` 置为 `window_last - base`）；若 B，则修正 `handed` 为 0 时的
  `s_action_sequence_last` 赋值。两者都**不新增状态**，符合 ⑧。
- **当前状态**：树干净、336 passed、P3 `build=20261018231151` 通过；N=16 与三后端仍为零失败。






### T2-PROGRESS-20261019-028：**两个 SCPI 页断言已按实际布局重建**（299→336 passed）；但"事件优先"的分支顺序在台架上把 16 组打回原形——已回退

- **已修好的债务（本轮净收益）**：两个页用例此前按 41/35 字段断言，而页面在序列插桩后已是
  **48 / 37** 字段。本轮用"直接跑解析器、按实际输出重建期望值"的方式修正（不是放宽断言），
  两文件 **299 passed**、与运行期用例合跑 **336 passed**。
- **实验记录（重要，避免再走弯路）**：
  | 变体 | 台架 N=16 |
  |---|---|
  | 删掉电平分支（纯事件推进） | `executed=3`、`NO_PLAN=109`、`LATE=3`（三后端全废） |
  | 事件优先 + 电平兜底（+ heap 112 KB） | `executed=3`、`NO_PLAN=117`、`LATE=3`（同样废） |
  | 电平优先（已提交状态） | `executed = 16 × 位置数`、零失败 |
  ⇒ **电平分支不仅必需，而且必须在前**；"事件优先"看似更干净，但会破坏 16 组窗口在真机上的推进。
  N=2 的掉组因此不能用"调整分支顺序"来修，得另找机制（下一轮重新分析，先只读代码不改）。
- **RAM 杠杆仍有效但未随本批落地**：heap 116→112 KB 在强制重链接下三 profile 通过（已验证两次），
  可与最终修复同批提交。
- **当前树**：构建通过（强制重链接）、336 passed、工作区仅剩 P3 生成物与两个已修正的页用例；
  下一条提交将只包含页用例与文档（不含固件改动，故不需新 P3）。






### T2-PROGRESS-20261019-027：批量尝试（heap 112 KB + 事件推进）**链接与运行时用例都通过**，但 SCPI 页断言需要重新生成——本轮回退，先把页契约定下来

- **本轮实测**：
  - `configTOTAL_HEAP_SIZE` 116→112 KB **+** 删除电平分支（事件推进）：**强制重链接三 profile 全通过**，
    `test_trigger_t2_runtime` **37 passed** ⇒ 语义修正本身是可落地的（RAM 4 KB 余量够用）。
  - 但两个"完整快照"页用例仍红：`tests/python/test_trigger_switch_service.py`（41 字段，diff 在 index 29）
    与 `test_trigger_gateway_service.py`（35 字段）。我按 index 29 = `ready_required` 改了一处后**仍红**，
    说明语义修正还改动了**别的字段值**（页里有多处由运行期状态渲染）。
- **为什么回退**：这些页是**对外契约**（SCPI 观测面）。在"页契约该表达什么"没定下来之前，
  用改断言的方式让它变绿，等于把契约写给实现看——违反 ⑧ 的"契约登记"精神。
- **下一轮（先定契约，再改实现）**：
  1. 明确这两个页在"事件推进/READY 默认关"之后的**字段语义**（哪些是电平、哪些是事件、哪些是计数），
     必要时同步更新 `DOCS_REGISTRY` 里对应页的登记项；
  2. 再一次性更新两个页用例的期望值（并在提交信息里写明契约变更）；
  3. 与 heap 112 KB、事件推进同批提交，台架验 N=16 与 N=2。
- **当前状态**：已回退（构建强制重链接通过、`test_trigger_t2_runtime` 37 passed、工作区 clean）；
  两个页用例仍按旧契约断言（已在前一条登记为债务）。






### T2-PROGRESS-20261019-026：库存测试里有 **2 个 SCPI 页断言仍按旧的 sequence 语义**（属债务，随下一轮语义修正一起改）

- **实测**：`tests/python/test_trigger_gateway_service.py` 与 `test_trigger_switch_service.py` 的
  "完整快照"用例在连跑时 2 failed / 297 passed。差异集中在 sequence 字段——自网关修复起，
  `s_deadline.sequence` 上报的是**组序号**（`base + k`）而不再是 `trigger_count + 1`。
  这是**契约变化**，断言需要同步，而不是把断言放宽。
- **处理策略**：这两个用例的值会在下一轮"事件推进"语义修正后**再次变化**，因此本轮**不改**它们，
  以免同一处改两遍；已在本条登记为债务，下一轮与其一并更新（并在提交信息里写明契约变更）。
- **RAM 杠杆（已确认，未落地）**：`configTOTAL_HEAP_SIZE` 116 KB → 112 KB 可一次腾出 4 KB，
  强制重链接后 app/B/boot 三 profile 均通过；但 heap 下调需要台架验证（避免运行时堆饥饿），
  因此与语义修正同批执行：改配置 → 强制重链接 → 宿主 → P3 → 台架（N=16 与 N=2）。
- **当前树状态**：构建通过（强制重链接验证）、工作区 clean、相关固件/工具测试除上述 2 项外全绿。






### T2-PROGRESS-20261019-025：RAM 占用排名已测——**FreeRTOS 堆 `ucHeap` 118 KB 是最大单体**，余量问题的正解在配置侧

- **`nm --print-size --size-sort` 实测（RAM 大头）**：
  ```
  0x1d000  ucHeap                       118,784 B   ← FreeRTOS 堆（configTOTAL_HEAP_SIZE = 116 KB）
  0x05648  s_control                     22,088 B
  0x04800  s_distributed_refmem_table    18,432 B
  0x04000  sync_io_shared_workspace      16,384 B
  0x04000  s_write_buffer / s_ota_fast_blocks  各 16 KB
  ```
  ⇒ 我之前怀疑的"逐槽位小数组"方向是错的：`TRIGGER_T2_RUNTIME_POSITION_COUNT` **只有 3**，
  `s_deadline[3]` 总共不到 300 B。真正的余量只能从上面这些"整块"里腾。
- **因此下一轮的正解（不改语义、风险最低）**：把 `configTOTAL_HEAP_SIZE` 从 116 KB 降到 **112 KB**（-4 KB），
  并用 `xPortGetMinimumEverFreeHeapSize()`/高水位证据确认运行时从不逼近上限；这一步能给后续
  "事件推进"语义修正（并顺带修 N=2 掉组）腾出充足余量。
- **纪律提醒**：本机 `flash_link_contract=OK` 可能是**复用旧 ELF**的结果；任何"绿"都要先删 ELF 强制重链接再确认
  （本轮已按此法验证：已提交状态重链接通过）。
- **不变的事实**：三角色短跑 16 组 × 上百位置零失败；开关长跑 240/241 PASS；N=2 的掉组已定位为
  `completed_sequence` 电平语义（待修）。






### T2-PROGRESS-20261019-024：**RAM 已到墙**——连"删掉一个分支"的净减少改动也溢出 956 B；代码冻结，先解决内存

- **三次实验（互相矛盾但都指向同一结论）**：
  1. 把"最后一组"分支前置 + 事件分支提前：溢出（未记录具体数值）；
  2. **删掉**电平分支（净减少约 11 行）：**仍溢出 956 B**；
  3. 回退到已提交状态并**删除 ELF 强制重链接**：**链接通过**。
  ⇒ 源码一改、重新链接就溢出约 950 B，而同一棵树不重新链接就"绿"——这说明**余量已为 0**，
  任何代码体量扰动都会撞墙；之前的"绿"部分来自 ninja 复用旧 ELF。
- **处理（纪律）**：已回退全部改动，强制重链接确认已提交状态三 profile 通过；工作区 clean。
- **下一轮必须先做（顺序不可颠倒）**：
  1. **制造 RAM 余量**：候选——`s_deadline[]`（每槽 `t2_deadline_plan_t`，含 `group_tick_delta[16]`+`group_argument[16]`
     ≈96 B）改为"仅保留当前窗口"或用 uint16 增量压缩；或把 `TRIGGER_ACTION_TEMPLATE_CAPACITY` 之外的诊断数组下沉；
  2. 余量 ≥2 KB 后，再做"事件推进"语义修正（删电平分支、以 `sequence+回执` 为主）；
  3. 台架同时验 N=2（722）与 N=16，判据 `executed = 组数 × action_finish`。
- **不变的事实**：三角色短跑 16 组 × 上百位置零失败；开关长跑 240/241 PASS；N=2 存在"位置收尾但 `executed` 少记"（电平语义问题，已定位待修）。






### T2-PROGRESS-20261019-023：N=2 掉组的机制看清了——`completed_sequence` 是**电平**不是**事件**

- **读数（N=2 快照，DIAG `done_hist`）**：
  ```
  slot1: advance=7990  finish=691  plan_window=2
         done_hist = [0x80000000|656, 656, 656] [0x80000000|653, 653, 653] [656, 654, 656]
  slot3: advance=7992  finish=692  plan_window=2
  ```
  其中 `0x80000000|X` 表示"**看见一次完成但没有推进**"（`completed == expected`），
  只有最后一行是真正推进（`completed 656 > expected 654`）。
- **机制**：开关的 `completed_sequence` 是"**最近一次已完成组**"的**电平值**，会保持不变；
  而运行期把它当**事件**用（`completed > expected` 才推进）。第一组完成后 `expected` 被抬到该值，
  之后同一个电平反复出现 ⇒ `completed == expected` ⇒ **不再推进**（开关 16 组窗口里这条被
  `sequence > expected` 的回执分支掩盖了，2 组窗口下就暴露成掉组）。
- **正确做法（下一轮）**：恢复以 **`sequence > expected` + 回执**为主推进（这条路径已验证过 357×3 与 16×114），
  `completed_sequence` 只用于"最后一组/收尾"的确认；并把 2 组窗口纳入判据用例。
  这是**语义修正**，不是加容差——`completed` 与 `sequence` 本来就不是一回事（前者是电平、后者是推进）。
- **网关的 2 组窗口**（`executed=0`、`BACKEND=220`）也应在同一轮一并复核：它的 16 组成功依赖"一次装一窗"，
  2 组窗口下 `plan_count=2` 与 `s_window_staged` 的交互需要实测。






### T2-PROGRESS-20261019-022：N=2 回归抽查发现**计数掉组**——位置都在收尾（691/692），但 `executed` 只有 438/440

- **台架（`T2_SEQ_GROUPS='1,2,2,2'`，80 s，组间距 8）**：
  ```
  SWITCH_A executed=438  groups=2  finished=691  NO_PLAN/LATE/BACKEND=0
  SWITCH_B executed=440  groups=2  finished=692  NO_PLAN/LATE/BACKEND=0
  GATEWAY  executed=0    finished=116  BACKEND=220
  ```
  ⇒ 开关两侧**位置都正常收尾**（691/692 个），但 `executed` 只有 438/440，**远小于 2 × finished**：
  两组的窗口里**每组位置只被计了一组**（或计组路径在 2 组窗口下少记一次）。
- **这正好命中目标 ⑤（N=2 的 361×2=722 不得回归）**：不是"跑不动"，而是**计数口径在 2 组窗口下失真**；
  由于判据依赖 `executed`，它会让 722 这个验收数字永远达不到，即使硬件行为正常。
- **下一轮（先定位再改）**：
  1. 用 `done_hist`（DIAG 尾部）观察 2 组窗口下 `completed/expected/window_last` 的推进序列，
     确认是"第二组完成没被 `completed > expected` 捕获"还是"第二组根本没被武装"；
  2. 若属前者：2 组窗口的 `expected` 起点（`base-1`）使第一组完成即把 `expected` 推到 base，第二组的
     `completed = base+1` 应能通过；需要看实测序列；
  3. 台架判据回到 `executed = 组数 × action_finish`（N=2 时 = 2 × 位置数）。
- **网关**：2 组窗口下 BACKEND=220、`executed=0`——与 16 组窗口的成功形成对比，说明网关的修复
  依赖"一个位置装一次窗口"的前提，2 组窗口下仍需单独核对。






### T2-PROGRESS-20261019-021：夜间收尾——核心目标已达成（三角色 16 组 × 上百位置零失败），追赶前移留待白天

- **核心结论（多轮台架反复验证）**：
  ```
  短跑 N=16（2.5 ms 组间距）：
    SWITCH_A / SWITCH_B / GATEWAY 均 executed = 16 × 位置数、NO_PLAN/LATE/BACKEND 全 0
  长跑 80 s：
    开关 3840/3856 = 16 × 240/241  PASS
    网关 1856 = 16 × 116 后因"迟到"掉队（late=1740）
  ```
  ⇒ **正确性目标达成**：一条位置的全部 16 个动作在三个角色上都能按序执行，判据（`executed = 组数 × action_finish`）
  是通过的；只剩"长跑节拍"这一项性能问题。
- **夜间最后一段的尝试与结论**：把"整窗追赶前移"放到 Core0 求解处（零新增静态量、链接通过），
  但会与 `expiry_sync` 冲突（宿主 5 个用例因此失败，且我插入"必须落在有效期内的检查"时锚点匹配失败）。
  为保持仓库绿色，**已回退**；三套相关测试 **115 passed**、构建绿、工作区 clean。
- **给白天的明确交接**：
  1. 重新加入 Core0 侧前移，并**同时**处理有效期：`raw_tick + 末组 delta > expiry_sync ⇒ 丢弃该计划（continue）`，
     注意 `session` 校验在源码中出现两次，插入点要选"发布前"的那一处（本轮就栽在这里）；
  2. 5 个宿应用例改为断言"**组间间距 + action_q 映射**"而不是绝对 tick，并新增"过期计划 ⇒ 整窗前移/丢弃"用例；
  3. 台架 80 s 判据：`late`≈0、`BACKEND=0`、`executed = 16 × action_finish`。
- **本轮也已入库**：`T2-SEQ-001` / `T2-GROUPCOUNT-01` / `T2-EXEC-LATE-01` 三项契约登记（`0adfdfd6`）。






### T2-PROGRESS-20261019-020：把"追赶前移"挪到 Core0 求解处（零新增静态量，链接通过）——但宿主 5 个用例断言了旧 tick，需同步改；本轮已回退

- **尝试**：在 `prepare_deadlines_core0()` 算出 `raw_tick` 后，若"第一组已到期"，把**整窗起点前移**到
  `now + guard`（组间 delta 是相对量，因此整窗平移、间距不变），并清 `next_tick`。
  时间源用现成的 `vdc_timestamp_clock_read_ticks64()`；**不新增任何静态变量**。
- **结果**：
  - **链接通过**（`RAM overflowed` 消失）⇒ 这个位置选对了，RAM 不是障碍；
  - 宿主 `test_trigger_t2_runtime` **5 个用例失败**：它们断言的是"计划 tick 等于按 slice 算出的绝对值"，
    而现在当计划过期时会被前移。属于**契约变化需要测试同步**，不是缺陷。
- **处理（纪律）**：本轮**已回退**（运行期与测试均 `git checkout`），构建绿、37 passed、工作区 clean。
- **下一轮（明确步骤）**：
  1. 重新加入该前移逻辑，并给 `trigger_t2_runtime` 用例补一个"计划过期 ⇒ 整窗前移且间距不变"的新用例；
  2. 把 5 个受影响的用例改为断言"**组间间距**与 `action_q` 映射"（而不是绝对 tick），这才是契约；
  3. 台架长跑（80 s）判据：`late` 接近 0、`BACKEND=0`、`executed = 16 × action_finish`。
- **不变的事实**：三角色短跑 16 组 × 上百位置零失败；开关长跑 240/241 PASS。






### T2-PROGRESS-20261019-019：RAM 余量实测 <1 KB；"整表前移"在网关侧实现会吃掉 ~900 B（已回退，强制重链接确认绿）

- **实测（map 文件）**：`bss` 结束于 `0x2007f7bc`，`heap` 固定 2 KB，栈顶 `0x20080000` ⇒ **余量仅几十字节**。
  这也解释了为什么"只加 4 B 计数器"也会触发 `RAM overflowed by 960`。
- **两次尝试与代价**：
  1. 前移 + 新计数器：溢出 960 B；
  2. 前移**零新增静态量**（`shift` 用局部量、复用 `s_late_count`）+ 删掉 36 B 的诊断直方图：溢出 **912 B**。
  ⇒ 前移这段代码在该编译配置下仍引入约 900 B 的链接可见占用（尚未查明是 `.data` 常量表还是别的），
  在余量几十字节的现状下**不可接受**。
- **处理**：已回退全部改动并**删除旧 ELF 强制重新链接**，确认已提交状态三 profile 全部通过
  （避免再用缓存的 ELF 误判"绿"）。
- **下一轮方向（换位置，不改预算）**：把"整表前移"移到**运行期接收计划处**（Core0 发布前，用局部量算偏移），
  或改为"计划过期就丢弃并让 Core0 重解"（不新增静态量、不改执行器）。
- **不变的事实**：三角色短跑 16 组 × 上百位置零失败；开关长跑 240/241 PASS；网关长跑 116 位置后因迟到掉队。






### T2-PROGRESS-20261019-018：位图化只回收 20 B（RAM 仍差 940 B）——说明"部门占用"另有其处，先测量再改

- **本轮尝试**：`s_window_staged` 改位图、`done_hist` 从 3 行减到 2 行。结果链接仍失败
  （`RAM overflowed by 940`，仅从 960 降 20 B）⇒ **`TRIGGER_T2_RUNTIME_POSITION_COUNT` 远小于 361**
  （按 8 槽计算，位图只省 7 B、直方图省 12 B，正好 ~20 B），所以"逐槽位小数组"不是内存大头。
- **处理**：已回退本轮改动，回到已提交的绿色状态（`--profile full` 的 app/B/boot 三个 profile 全部链接通过）。
- **下一轮（先测量）**：用 `arm-none-eabi-size`/map 文件统计 `trigger_t2_runtime.o`、`sync_io_*.o`
  的 `.bss/.data` 排名，找出真正的 1 KB 级占用；再决定是缩小该结构、还是把"整表前移"改成
  **零新增静态量**的实现（`shift` 用局部变量即可，只要不新增计数器）。
- **不变的事实**：三角色短跑 16 组 × 上百位置零失败；开关长跑 240/241 位置 PASS；网关长跑 116 位置后因迟到掉队。







### T2-PROGRESS-20261019-017：整表前移方案因 **RAM 溢出 960 B** 无法链接（已回退）；下一步先回收内存

- **设想（正确方向）**：`late=1740/1856` 说明"计划表的第一组在 Core1 装表时早已到期"，
  逐组钳位会把整窗变成连发。正确做法是**整表前移**（保留组间相对间距，只平移起点，即追赶语义），
  实现只需在 `stage_sequence` 里算一次 `shift` 并加到全部 `deadline_tick`。
- **结果**：链接失败 —— `region RAM overflowed by 960 bytes`。本轮为此新增的只有
  `s_stage_shift_count`(4 B) 与之前几轮的 `s_window_staged[361]`(361 B)、`s_done_hist[3][3]`(36 B) 等，
  说明 RAM 余量已经耗尽。
- **处理**：**已回退**该改动（`sync_io_gateway.c` 与 SCPI 页），构建恢复绿、网关 51 passed、工作区 clean。
- **下一轮（先省内存，再改语义）**：
  1. `s_window_staged[]` 从 361 字节的数组改成 **位图**（361 位 ≈ 46 B），省 ~315 B；
  2. `s_done_hist[3][3]` 缩为 `[2][3]`（24 B），省 12 B；
  3. 视需要把若干 `uint32_t` 的只写计数器并入已有的 DIAG 字段（不加新变量）；
  4. 腾出 ≥1 KB 后再重做"整表前移"，台上判据不变：80 s 下 `late` 接近 0、`BACKEND=0`、`executed = 16 × finish`。
- **当前仍成立的事实**：三角色 16 组 × 上百位置、零失败（短跑）；开关长跑 240/241 位置 PASS；网关长跑 116 位置后因迟到掉队。







### T2-PROGRESS-20261019-016：长时间运行（80 s）——开关 240+ 位置全过；网关 116 位置后开始掉队（`late=1740`）

- **80 s 采样，组间距 8 slice（2.5 ms）**：
  ```
  SWITCH_A 3840 = 16 × 240  PASS      SWITCH_B 3856 = 16 × 241  PASS
  GATEWAY  1856 = 16 × 116  finished 116  BACKEND 125  ← 前 116 个位置正常，之后开始失败
  ```
  网关 GWDIAG：`plan_count=16`、`complete=1856`、`plan_next=1`、**`late=1740`（= `collision`）**、
  `done_hist` 末条 = "completed=1856（已见未推进）, expected=1856, last=1856"。
- **读法**：`late=1740` 说明长时间运行时**几乎每次武装都已过期**（被钳位成"立刻发"）——网关的
  "完成一组才装下一组"在 2.5 ms 间距下跟不上；一旦连续迟到，位置就会在 `arm` 处失败（124 次）。
  开关没有这个问题，因为它**预装两组**。
- **10 ms 间距的对照**（`--group-slices 32`）：开关的 `executed` 与 `finish` 比例变得不自洽
  （3648 vs 470×16），网关 `executed=0` —— 说明间距过大时另有耦合（位置到达率与窗口长度），
  不作为结论，只记录。
- **结论**：**正确性已成立**（三后端都能 16 组 × 上百位置、零失败）；剩下的是**性能/节拍**：
  网关必须做到"预装两组"（两字协议 + 深度 2 FIFO），才能像开关那样在长跑中不迟到。
- **下一轮**：把网关的输出程序改成与开关同构的两字协议（value/width + delay），并在 `stage_sequence`
  里一次推两发；判据：**80 s 长跑下 `late` 接近 0、`BACKEND=0`、`executed = 16 × finish`**。







### T2-PROGRESS-20261019-015：**网关跑通了！三个角色全部 `executed = 16 × 位置数`、零失败**

- **最后一处修复（一行）**：网关的武装分支**没有设置窗口上界** `s_action_sequence_last`（开关分支有）。
  于是 `done_hist` 显示唯一一次完成判定是 `completed=1（已见未推进）, expected=1, window_last=0`，
  而推进规则是 `completed > expected && completed <= window_last` ⇒ **上界为 0，永远不可能推进**；
  计划就停在执行器已经跑掉的那两发上——这正是"卡在两发"的真相。
- **台架（N=16，组间距 8）**：
  ```
  SWITCH_A 1817  finish 114   BACKEND 0   （16×113=1808 再 +9，采样点落在位置中间）
  SWITCH_B 1824 = 16 × 114    finish 115   BACKEND 0
  GATEWAY  1824 = 16 × 114    finish 114   BACKEND 0   identity 0  terminal 0  arm 0
  gateway GWDIAG: plan_count=16  svc_complete=1824  plan_next=1  （每位置新装一份计划）
  ```
  ⇒ **网关与开关达到同级**：16 组/位置 × 一百多个位置、三类失败全 0、`svc_complete` 与 `executed` 一致。
- **意义**：路线 ② 的"参照 SWITCH"目标达成——网关不再是另一套语义；它现在缺的只是"预装两组"的性能优化
  （评审指出的 5.4 s/32 位延时上限与预装需求），而不是正确性。
- **下一轮（④ 达标验证）**：短采样已证明倍数关系，直接上 **361 位置全链**（`T2_SAMPLE_TICKS=110`），
  判据 `executed = 16 × action_finish`、三类失败为 0；同时复查 N=2 的 722 不回归。







### T2-PROGRESS-20261019-014：GWDIAG 增加 SM 直接观测量（在 teardown 前采样）：**停住时 SM 是关闭的、pending=0、TX=0**

- **新增（设备侧，`PICO_ON_DEVICE` 保护）**：`sm_enabled`（`ctrl` 位）、`pc`（`sm[].addr`）、`tx_level`（`fstat`）、
  `pending`。宿主 harness 用解释器建模 PIO，不读这些寄存器，故条件编译。
- **读数（N=16，组间距 8，快照在 teardown 之前）**：
  ```
  plan_count=16  svc_complete=2  plan_next=3  svc_wait=51
  sm_enabled=0   pending=0      tx_level=0
  ```
  ⇒ 停住时**输出 SM 已关闭、没有待发的字、pending 已清**。也就是说"第三发被武装、只是不执行"这个描述要更正：
  **第三发根本没有发生**——`plan_next` 停在 3（= 装表时的第一发 + `service` 里的两发），之后 `service` 不再武装。
- **这指向运行期一侧**：第二发完成之后，运行期没有继续处理这个槽位（完成计数没推进 ⇒ 位置被
  `identity/terminal` 判失败 ⇒ 不再调用该槽位的后端）。`arm=114`（每位置一次）与 `identity=1` 也支持这一点。
- **下一轮（一步）**：在运行期的推进分支上把"每次完成"的三元组（`completed / expected / window_last`）
  打点（已有 `window_seq/expected` 镜像，但只在 armed 时更新，需要新增一个"最后三次完成"的环形记录），
  台架读一次即可看出第 2 发之后为什么不再前进。
- **开关侧**：`executed = 16 × 位置数`、三类失败全 0。







### T2-PROGRESS-20261019-013：网关"恒为 2 发"与组间距、SM 禁用都无关——下一轮改为直接读 SM 状态

- **两条对照实验（都不改代码，仅台架参数 / 一行代码）**：
  1. **组间距从 2.5 ms 拉到 20 ms**（`--group-slices 64`）：结果**完全相同**
     （`svc_complete=2`、`plan_next=3`、`plan_next` 不再前进、`arm` 随位置数增长）；
  2. **完成时不关闭输出 SM**（与开关一致，留 SM 停在 `pull block`）：结果**完全相同**
     （`complete=2`、`plan_next=3`、`svc_wait=54`）。
- **结论（重要）**：网关**每份计划恒好两发**，与节拍无关、与"读回执后禁用 SM"无关；
  第三发被武装（`pending=1`、TX 有字、`start_deadline` 返回 true）但**PIO 从不执行它**
  （`svc_wait` 单调增长、`svc_bad_receipt=0`）。
- **纪律**：那条"SM 保持使能"的改动**已回退**（未证明有效，不入库）；工作区只剩 P3 生成的报告差异。
- **下一轮（一步，读状态而不是猜）**：在 GWDIAG 增加四个直接观测量——
  `pio_sm_is_enabled(OUTPUT_SM)`、`pio_sm_get_pc(OUTPUT_SM)`、TX FIFO 电平、`s_realtime_pending`；
  台架一次采样即可判定是"SM 没被使能""PC 卡在非 pull 指令"还是"TX 字没进 FIFO"。
- **开关侧**：1824/1840 = 16 × 位置数、`finish` 114/115、三类失败全 0。







### T2-PROGRESS-20261019-012：新增"连续三次脉冲"主机用例——**主机上连打三发完全正常**，排除"SM 重启丢回执"假设

- **新用例**：`tests/python/test_sync_io_gateway.py` 增加 `triple`——`load` 后连续三次
  `schedule_at → advance → service`，断言每次都 `COMPLETE` 且 `completed_sequence = 1,2,3`。
  **通过**（51 passed），说明"读回执时禁用 SM、下一发再启用"的这套流程在主机模型里**不会**丢回执。
- **同时确认的常量**：`SYNC_IO_GATEWAY_DEADLINE_GUARD_US = 100 µs`（80000 ticks）；
  台架组间距在 `T2_SEQ_PERIOD_SLICES=8` 下约 2.5 ms，远大于 guard，所以"每组都被钳位"也不成立
  （实测 `late` 只有 2）。
- **因此台架上的停滞不是"SM 重启丢回执"**，而是**节拍问题**：`service` 每完成一组才武装下一组，
  一旦某组完成得晚，后面几组的绝对 tick 已经进入 guard/过期区间，于是被钳位成"立刻发"，
  连发几发后计划就跟不上位置的到来节奏（GWDIAG `plan_next=3`、`svc_wait` 持续、`arm` 继续失败）。
- **下一轮（一步）**：给"钳位次数"与"每组实际起脉冲时刻 − 计划 tick"的偏差加观测（GWDIAG 已有 `late`，
  再加一个"最大偏差"就够），确认偏差是否随时间累积；若累积，则把网关改成**预装两发**（两字协议），
  这正是开关的模型，也是评审 ② 的最终形态。
- **开关侧不变**：`executed = 16 × 位置数`、三类失败全 0。







### T2-PROGRESS-20261019-011：窗口"只装一次"（`s_window_staged`）+ 失败时清理交接状态——网关 `arm` 121→113，但计划仍只排空 2 组

- **两处运行期改动（单 owner 语义）**：
  1. 用显式 `s_window_staged[index]` 区分"收到计划"与"已交出窗口"：网关在**窗口仍在飞**时不再重复提交
     （此前 `deadline_receive` 会先把 `group_count=16` 写进槽位，导致第一次武装就误判为"已有窗口"，一步跳空）；
  2. `action_fail()` 时清 `s_window_staged` 与 `group_count`——否则下一个位置会"复用"一个已被放弃的窗口
     （此前实测 `arm=80 / identity=41`）。
- **台架（N=16，组间距 8）**：
  ```
  SWITCH_A 1808 = 16 × 114   SWITCH_B 1824 = 16 × 114   三类失败全 0
  GATEWAY  BACKEND 114 | DIAG: advance 0 identity 1 arm 113
           GWDIAG: plan_count 16  plan_next 3  svc_complete 2  svc_wait 47  late 2
  ```
  ⇒ 网关**每个位置只装一次窗口**（`arm` 从 121 降到 113，剩下的失败都是"新位置遇到上一份计划未排空"），
  且**计划只排空 2 组就停**：第 3 组武装后 PIO 不再推回执（`svc_wait` 持续增长、`complete` 停在 2）。
- **下一轮（一步）**：查网关 PIO 为什么在第 3 个脉冲后不再推回执——重点看 `service` 里
  `pio_sm_set_enabled(false)` 与下一次 `pio_enable_sm_mask_in_sync` 之间的状态（开关的"两字预装"正是为了避免
  这种"停-等-再启"），必要时把网关的输出程序也改成两字协议（与开关同引擎）。
- **开关侧**：1808/1824 = 16 × 位置数、`finish` 114、三类失败全 0。







### T2-PROGRESS-20261019-010：网关**确实在完成脉冲**（`svc_complete=2`、`plan_next=3`），但运行期 `advance=0`——因为 `arm` 失败 121 次把位置提前判死

- **新增完成路径计数（GWDIAG 尾部）**：`svc_wait`（pending 置位但 RX 空）、`svc_bad_receipt`、`svc_complete`、
  `plan_next`。字段顺序继续按受控实验确认：`… 17 late, 18..21 武装四桶, 22 svc_wait, 23 svc_bad_receipt,
  24 svc_complete, 25 plan_next`。
- **本轮读数（N=16，组间距 8）**：
  ```
  开关侧照旧：advance = 16 × 位置数、三类失败 0
  GATEWAY：plan_count=16  plan_next=3  svc_complete=2  svc_wait=70  late=2  collision=2
           DIAG: advance 0  identity 1  terminal 0  arm 121
  ```
  ⇒ **网关第一次真正完成了脉冲**（`svc_complete=2`、计划前进到第 3 组），`svc_bad_receipt=0`（回执值正确）。
  但运行期 `advance=0`，同时 `arm=121`：运行期每次为新位置提交窗口时，网关的**上一份计划还没排空**
  （`plan_count=16`），于是 `stage_sequence` 以 BUSY 拒绝 ⇒ 位置被判 BACKEND（`arm` 计数）⇒ 永远没有"已武装的槽位"可服务。
- **根因（结构性，正是路线 A 要解决的问题）**：一个位置 = 一份 16 组计划，**只需要装一次**；
  但现在运行期在"推进下一组"时又去装一份新窗口，造成自我竞争。下一轮：让运行期在
  `s_deadline[index].group_count` 仍然有效且 `group == s_group_next` 时**不再重复提交窗口**（单 owner：
  装一次、由执行器排空），并给"重复提交"加一个计数以便观察。
- **开关侧**：保持 `16 × 位置数`、`finish` 与位置数一致、三类失败全 0。








### T2-PROGRESS-20261019-009：迟到钳位原来**在范围检查之后**（past-due 被误判为 RANGE）；修好后网关能装计划，但**首脉冲回执不来**（plan_count=16 不排空）

- **修掉的顺序错误（关键）**：`start_deadline` 先做 `deadline-before-4 > UINT32_MAX` 的范围检查，再钳位。
  对"已经过去"的 tick，这个减法**下溢成巨大无符号数**，于是被当成"延时不可表示"拒绝——
  正是钳位要救的场景（GWDIAG：`arm_reject[range]=51`、`late=0`）。现在**先钳位、再按钳位后的差值检查**。
- **契约变更（已在测试中固化）**：迟到（tick 已进入武装 guard 或已过期）的组**照发**并计数 `late`，不再拒绝；
  `tests/python/test_sync_io_gateway.py` 的 `deadline_late` 用例改为断言"armed 且随后起脉冲"。
  这条契约需要登记（下一轮 ③ 一并登记 `T2-SEQ-001` 与该"迟到照发"条款）。
- **台架（N=16，组间距 8）**：
  ```
  SWITCH_A 1776 = 16 × 111   SWITCH_B 1792 = 16 × 112   三类失败全 0，finish 与位置数一致
  GATEWAY  executed 0  BACKEND 112
  DIAG : identity 1  terminal 0  arm 111
  GWDIAG: plan_count 16（计划装入后一直不排空）  late 2  arm_reject=[0,0,1,0]
  ```
  ⇒ 网关现在**能装入 16 组计划**（`stage` 不再被 BUSY 拒），失败点最后一次前移：
  **首脉冲的完成事件没有发生**（`plan_count` 恒为 16、`s_plan_next` 不动 ⇒ 运行期下一窗口被 BUSY 拒）。
- **下一轮（一步）**：给 `service` 的完成路径加三个计数（`s_realtime_pending` 是否置位 / RX FIFO 是否非空 /
  回执是否与 `s_pending_receipt` 相符），输出到 GWDIAG 尾部；据此确定是"PIO 从未推送回执"还是"推送了但被丢弃"。
- **开关侧**：1776/1792 = 16 × 位置数、`finish` 111/112、三类失败全 0。








### T2-PROGRESS-20261019-008：网关武装失败分成两类——**BUSY 61 次** 与 **RANGE 51 次**（偏移已用受控实验钉死）

- **GWDIAG 字段顺序（受控实验确认）**：`0,1` 结果字；`2` schema(0xDA02)；`3` plan_count；`4` ready_active；
  **`5` ready_required**（把 `REAdy` 在 0/1 之间切换，只有 idx5 变化）；`6` pulse_ns；`7` collision；
  `8` margin；`9..16` stage 的 8 个拒绝桶；**`17` late**；`18..21` 新增的 4 个武装拒绝桶
  （core/clock、prepare_realtime、origin、**delay range**）。
- **本轮读数（N=16，组间距 8 slice）**：
  ```
  SWITCH_A 1792 = 16×112   SWITCH_B 1808 = 16×113   三类失败全 0
  GATEWAY  executed 0  BACKEND 112
  DIAG : identity 1  terminal 51  arm 61
  GWDIAG: plan_count 1  collision 51  stage_reject[BUSY]=61  arm_reject[delay_range]=51
  ```
  ⇒ 两个**互相独立**的成因：
  1. **`stage_sequence` 被 BUSY 拒绝 61 次**：上一份计划还在飞（`s_plan_count != 0`）时运行期又提交新窗口；
     网关不像开关那样把计划排空，于是下一次武装被拒。
  2. **`start_deadline` 的 delay 范围拒绝 51 次**：`deadline - now - 4 > UINT32_MAX`，即**武装时刻与 tick 的
     时间基不一致**（网关的 `raw_tick` 与计划里 `slice_to_tick` 得到的绝对 tick 不是同一基准），
     这解释了为什么"迟到钳位"没有救到它（差值不是负数，而是巨大正数）。
- **下一轮（二选一，先做 (2)）**：核对网关 `raw_tick()` 与 `slice_to_tick()` 的时间基（TIMER1 vs 同步时间轴），
  把 `schedule_at` 的入参统一到同一基准；随后再做 (1)：让网关在计划未排空时**等待**而不是让运行期失败
  （或让 `service` 完成时把 `s_plan_count` 排空）。
- **开关侧**：1792/1808 = 16×位置数，`finish` 与位置数一致，三类失败全 0。








### T2-PROGRESS-20261019-007：网关"迟到即拒绝"改为"迟到也要发"（`arm` 45→0）；失败点再前移到 **service 内部的下一组武装**

- **改动（执行器侧，符合触发域语义：迟到的触发仍要发，迟到可观测）**：`start_deadline` 不再因
  "deadline 已进入 guard 或已过期"而拒绝，而是把目标钳到 `now + guard` 并计数 `s_late_count`
  （GWDIAG 尾部新字段）；只有 PIO 32 位延时表达不了的差值才拒绝。
- **台架（N=16，组间距 8 slice）**：
  ```
  SWITCH_A 1904 = 16 × 119   finish 119   NO_PLAN/LATE/BACKEND 全 0
  SWITCH_B 1920 = 16 × 120   finish 120   全 0
  GATEWAY  executed 0  LATE=1  BACKEND=119  | DIAG: identity 0  terminal 119  arm 0
             GWDIAG: plan_count=0  collision=119  late=0
  ```
  ⇒ **`arm` 从 45 归零**（运行期的武装不再被拒），但 `terminal=119` 仍在，且 `plan_count=0`
  ⇒ 失败发生在 **`service` 内部的"下一组武装"**（`schedule_at` 返回 false ⇒ 计划清空 + UNCERTAIN），
  而不是运行期发起的武装。
- **下一轮（一步）**：在 `service` 的这条路线上分桶——`prepare_realtime` 失败 / 时钟或 TIMER1 检查失败 /
  `start_deadline` 的 origin 校验失败 / 计划越界，各加一个计数并输出到 GWDIAG；先看清是哪一类，再决定
  "同引擎化（预装两组）"还是"修该守卫"。
- **开关侧**：1904/1920 = 16×位置数、`finish` 与位置数一致、三类失败全 0。








### T2-PROGRESS-20261019-006：网关 `identity` 归零（语义已对齐），失败点前移到 **武装 tick 已过期**（GWDIAG：SCHEDULE 拒绝 + `arm=45`）

- **改动**：武装后的 tick 属于执行器（HAOFV 分层），运行期不再因"tick 前移"判身份失败——
  对网关接受 `deadline > 记录值`（只把"回退"或"序号不符"当错误）；开关仍严格双字段比对。
  台架：网关 `backend_identity` 从 113/114 **降到 0**。
- **失败点前移（本轮最关键的证据）**：
  ```
  N=16、组间距 8 slice：
  SWITCH_A advance 3648 finish 228 物理失败全 0
  SWITCH_B advance 3664 finish 229 物理失败全 0
  GATEWAY  advance 0    finish 0   identity 0   terminal 183   arm 45
  GWDIAG: plan_count=16 ready_active=0 ready_required=0 pulse=20 collision=183 rejects[...]=45
  ```
  ⇒ 网关的 `identity` 已归零，现在全是 **terminal**（CANCELLED/UNCERTAIN），并且运行期 `arm=45` 与 GWDIAG 的
  "某一拒绝桶=45"对应——**`schedule_at` 的 `start_deadline` 拒绝了已过期的绝对 tick**。
- **结论（与评审一致）**：网关是"完成一组后才武装下一组"，而下一组的绝对 tick 在它准备好时往往已经过去；
  开关没有这个问题，因为它**预先装了两组**（两字协议 + 深度 2 FIFO）。所以网关必须"参照 SWITCH"换成
  **窗口预装 + 队列**，而不是继续在运行期加容差。下一轮即为该改造（PIO 两字协议 + 深度 2 队列 + 逐组回执）。
- **开关侧**：`advance = 16 × 位置数`、identity/terminal/arm 全 0、`finish` 与位置数一致（228/229）。








### T2-PROGRESS-20261019-005：`plan_window=16` 证明"网关计划只有 1 组"的推断**错误**；身份失败其实是 **deadline 不符**

- **新增诊断**：DIAG 末尾 `plan_window`（最近一次发布出去的计划组数），数值随计划发布点记录。
- **本轮读数（N=16）**：
  ```
  slot1 SWITCH_A : advance 1792  finish 112  seq 1905  expected 1904  identity 0    plan_window 16
  slot3 SWITCH_B : advance 1808  finish 113  seq 1922  expected 1921  identity 0    plan_window 16
  slot2 GATEWAY  : advance 0     finish 0    seq 114   expected 113   identity 113  plan_window 16
  ```
  ⇒ **三个角色的计划都是 16 组**，上一轮"网关只有 1 组"的推断被推翻（`window_last=0` 只是该镜像字段在网关分支未被更新）。
- **更正后的根因**：网关身份的判定是 `sequence != expected || deadline != s_deadline[index].deadline_tick`；
  其中 `sequence == expected + 1`（网关报告"已武装的下一组"）已被 kind 容忍，
  **剩下的不符项是 `deadline`**：网关的 `service` 每完成一组就按下一组的绝对 tick 重新武装，
  `status.deadline_tick` 因此指向**下一组**，而运行期记录的仍是**窗口基准**——两者天然不等。
- **结论（决定下一轮的做法）**：这不是再加一个容差能解决的，而是网关的状态语义与开关不一致造成的。
  下一轮按 ② 的第二步做**同引擎化**：网关也按"逐组武装 + 每组的 sequence/deadline 成对上报"，
  运行期保持"一组一比对"的严格判定（⑦ 与 HAOFV 单一 owner 都要求如此）。
- **开关侧不变**：`advance = 16 × 位置数`、`identity = 0`、三类失败全 0。








### T2-PROGRESS-20261019-004：网关 BACKEND 的真正原因浮出水面——**网关的计划只有 1 组**（`window_last=0`）

- **按发射顺序建立 DIAG 索引表**（result×2, schema, advance, finish, finish_group, finish_total,
  window_seq, window_expected, window_last, window_complete, freeze_zero, freeze_floor, seq(dup),
  complete(dup), boundary, **backend_identity(16) / backend_terminal(17) / backend_arm(18)**, 8 个网关拒绝桶），
  上一轮"身份不符"的判断由此可被准确读出。
- **本轮读数（N=16）**：
  ```
  SWITCH_A 1744 = 16 × 109、SWITCH_B 1760 = 16 × 110，三类失败全 0（开关不受影响）
  GATEWAY  executed=0  BACKEND=110
  gateway DIAG: window_seq=101  window_expected=101  window_last=0  window_complete=0  identity=101  terminal=0  arm=9
  ```
  **`window_last = 0` 是关键**：它等于 `status.sequence + handed - 1`，而 `handed` 在 `plan->group_count <= 1`
  时为 0 ⇒ **网关收到的计划只有 1 组**，于是走单组武装分支（不更新 `s_deadline[index].deadline_tick`），
  随后的身份检查因 deadline 不符而每位置失败一次（identity=101）。
- **为什么只有 1 组**：窗口构建循环在 `k=group+1` 处 `break`——条件是
  `s_local_group[k].delta_q < s_local_group[k-1].delta_q` 或 `slice_to_tick` 失败。
  台架侧证据：两块从板的 `TRIGger:REServation:T2:ACTion?` 字段数不同（开关 **71** vs 网关 **53**），
  说明网关的组表布局/组间距与开关不一致（组间距可能全为 0）。
- **下一轮（一步）**：把两块板的 `ACTion?` 逐字段对齐（尤其是 group/delta 那几列），确认网关 16 组的 `delta_q`
  是否递增；不递增则在**装表处**修（工作流/表布局，单一事实源），不在运行期猜。
- **已提交**：本轮只改了运行期身份检查（网关 kind 接受 `expected+1`，开关与 wrong_receipt 测试保持严格），
  构建绿、37 passed、P3 build `202610181724xx`，台架无回归。








### T2-PROGRESS-20261019-003：SEQ-C01 的"低一格起点"改为**仅开关**使用；网关仍是 0/BACKEND（已排除该假设）

- **三次台架对照（N=16，20 s，各自冻结源码跑 P3）**：
  | 变体 | SWITCH_A | SWITCH_B | GATEWAY | 结论 |
  |---|---|---|---|---|
  | 原样（`expected = seq-1` 对两后端） | 1856=16×116 | 1872=16×117 | 0 / BACKEND 117 | 开关最好 |
  | 去掉减一 | 1136=16×71 | 1136=16×71 | 0 / BACKEND 119 | **开关大幅回退** ⇒ 减一是开关路径的必需项 |
  | 减一只给开关（本轮） | 1744=16×109 | 1760=16×110 | 0 / BACKEND 109 | 开关恢复、网关不变 |
- **结论**：`expected = sequence - 1` 对开关是**必需**的（去掉后 16×117 → 16×71 并出现 BACKEND 47/48）；
  而"网关身份不符"这个假设**已被本轮排除**——网关把减一去掉后仍然每位置一次 BACKEND（109 次），
  说明真正原因不在这里。
- **下一轮（精确到一步）**：先按 `scpi_trigger_t2_runtime_commands.inc` 里 DIAG 的**发射顺序**建立字段索引表，
  再读快照里网关槽位的 `backend_identity / backend_terminal / backend_arm` 三个值，确定 BACKEND 的来源是
  "身份不符""终态/游标"还是"武装被拒"，然后才动网关代码（避免继续猜）。
- **开关侧现状**：16 组单窗口、三类失败全 0、`executed = 16 × 位置数`；SWITCH_A 仍差一个位置（109 完成 vs 110 执行）。








### T2-PROGRESS-20261019-002：路线 A 第 1 步——删除窗口边界机制；NO_REGRESS 通过（SWITCH_B `1872 = 16×117` PASS）；READY 默认关闭

- **改动（HAOFV ⑧ 单一 owner）**：删除窗口边界这一整套第二权威——
  边界分支（等待标志 / 释放 arm / 清计划版本 / 移动 slot 身份）、`s_waiting_window[]` 数组及其武装清零、选择环里的豁免；
  `window_boundary` 字段保留在 DIAG 中但恒为 0（不再产生该状态）。
  保留的唯一推进规则：**`completed_sequence` 前进一步 = 该组完成**；窗口内游标前进仍按 `s_group_next < s_group_handed`。
- **READY 默认关闭（用户指示：没有真实网分，避免影响链路）**：固件侧 `s_ready_required` 早前已默认 false；
  本轮把工作流里 3 处 `T2_READY_REQUIRED` 默认值从 `"1"` 改为 `"0"`（启动后与采样前都会下发），
  避免"默认 1"悄悄把严格模式重新打开——这正是网关之前反复被拒的一个来源。
- **台架（N=16，`T2_SEQ_GROUPS='1,16,16,16'`，20 s）**：
  ```
  SWITCH_B: executed = 1872 = 16 × 117   finished=117   NO_PLAN/LATE/BACKEND = 0   → 非循环判据 PASS
  SWITCH_A: executed = 1856 = 16 × 116   finished=117   → 仍差一个位置（16 次）
  GATEWAY : executed = 0                 BACKEND=117
  ```
  与上一轮（SWITCH_B `1808 = 16×113`）对比：**执行总量上升、零失败**，删除边界机制没有回归。
- **下一步**：① SWITCH_A 的单位置丢失（`group_done/group_next` 逐槽位对比 A/B）；② 网关 117 次 BACKEND
  （读 `backend_identity/terminal/arm` + `GWDIAG` 八桶，确定来源后同引擎化）。








### T2-PROGRESS-20261019-001：夜间长期任务发布（用户授权自主决策）——目标与执行规则

- **用户指示**：发布一个长期任务，夜间自主推进；需要我决策的直接按推荐来（与早前"夜里按推荐决策"的长期授权一致）。
- **任务目标（goal-94c4a5d9）**：把 T2 多动作序列收敛到"每个位置的全部动作在真机上跑通"，并保持证据闭环。顺序：
  ① 路线 A 检修（逐项删除边界机制，每项一次 N=16 短采样，`executed` 不下降）；
  ② 网关 113 次 BACKEND 定位与同引擎化（用已入库的 `backend_identity/terminal/arm` 与 `GWDIAG` 八个拒绝桶）；
  ③ 口径统一（台架按角色 8/2/2，16 组为模型源；K-6 措辞；`T2-SEQ-001` 登记 `DOCS_REGISTRY.md`）；
  ④ 判据非循环：`executed == 组数 × action_finish` 且三类失败为 0；短采样证明倍数后上 361 位置；
  ⑤ 不回归抽查：N=2 的 722、SCPI 面、表容量 48、T2 位置传输（DISTRIBUTED / SEAT refused 0）；
  ⑥ 每轮：构建绿 → 宿主绿 → P3（**源码冻结、一次连续跑完**）→ 文档 → 提交；不留半成品，台架不可用则 `git stash` 隔离并记录；
  ⑦ READY 已默认禁用，接网分后再开并验 `ready_collision_count=0`、`ready_margin_min_ticks>0`。
- **当前事实基线（供夜间对照）**：开关侧 16 组单窗口 `executed = 16 × 位置数`、零失败（多次实测 1782–1824）；
  SWITCH_B 曾达 `1808 = 16 × 113` 且判据 PASS；SWITCH_A 少一个位置（差 16 次）；GATEWAY `executed=0 / BACKEND=113`。
- **P3 现在的可靠用法**：`--run <dir> --steps a-b` 续跑（中途不改源码），或改完一次性 `--profile full`（约 10 分钟）。
- **用户追加要求（已写入任务 goal-94c4a5d9 rev3）**：**代码改动必须符合 HAOFV 架构**，每轮提交前自检四项：
  1. **分层不越权**——`TRIGger:*` / TriggerAO / TriggerFB 只做产品 RUN 状态、门禁、active sequence 绑定与业务状态；
     `REALtime:*` / `sync_io` / PIO/DMA/IRQ 只做实时资源与低层状态；两个方向都不得绕过 owner（依据
     `docs/arch/HAOFV_ARCHITECTURE.md` 的「Trigger / Realtime 分层」）。
  2. **单一 owner**——序列执行归动作侧（`T2-SEQ-001`）；T2 侧过渡窗口代码只能删，不得新增第二个权威
     （对应删除 `s_group_handed` / `s_waiting_window` / `next_tick` 这类重复状态）。
  3. **通道语义**——只经稳定抽象通道（`TRIG_IN`/`TRIG_OUT`/`RJ45_TRIG_*`），GPIO 映射只留 board profile 与 `sync_io`。
  4. **常量单一事实源 + 契约登记**——组数/窗口尺寸/时序常量收敛到单一出处；跨域契约登记 `docs/check/DOCS_REGISTRY.md`
     并更新 `Last updated`，文档门禁全绿。
  任一不满足则该轮不做该项改动。









### T2-PROGRESS-20261018-073：判据改为"非循环"后第一次给出真实结论——SWITCH_B 16×113 全过、SWITCH_A 少一个位置、GATEWAY 仍 0

- **发现并修掉的判据缺陷**：我原来的 `verdict` 用 `positions = executed / groups` 反推位置数，再检查
  `executed == groups × positions`——**永远成立**（自证），所以上一轮它报 OK 是假象。现在改为用 DIAG 的
  **`action_finish`（真正收尾的位置数）**作为独立量，判据为 `executed == groups × finished` 且 `finished > 0`、
  三类失败为 0；并且跳过没有动作组的源板（COUNTER）。
- **另一个采样缺陷**：链路结束会自动跑 teardown（步骤 21），**之后再读板子全是 0**。现在工作流在采样循环末尾
  （teardown 之前）把 `WHY/WINdow/DIAG/T2/GWDIAG` 写成 `out/sync_trigger/sequence_snapshot.json`，
  判据读该快照。
- **真实读数（N=16，`T2_SEQ_GROUPS='1,16,16,16'`）**：
  ```
  SWITCH_B: executed=1808 = 16 × 113   finished=113   NO_PLAN/LATE/BACKEND=0   → 判据 PASS
  SWITCH_A: executed=1792 = 16 × 112   finished=113   → 少一个位置的距离（差 16 次）
  GATEWAY : executed=0                 BACKEND=113
  ```
- **结论**：路线 A（一位置一窗口、无边界）在开关侧**已经成立**，只剩两件小事：
  1. SWITCH_A 少一次 16 组（差 16 次执行）——像单个位置丢失，下一轮用 `group_done/group_next` 逐槽定位；
  2. GATEWAY 仍 0（BACKEND=113）——按"参照 SWITCH"把它改成同引擎（两字协议 + 队列 + 逐组回执）。
- **台架在本轮末尾不可用**：P3 连续在步骤 29 失败（`invalid P3 joint provisional receipt`），无法产出凭证。
  因此把两处**已验证逻辑但未过 P3** 的改动入库前隔离：
  ```
  stash@{0}  sequence tooling: non-circular verdict + pre-teardown snapshot (pending P3)
  stash@{1}  runtime BACKEND buckets (pending P3)
  ```
  工作区 clean；下一轮台架恢复后按 `stash pop` → P3 → 提交的顺序入库。









### T2-PROGRESS-20261018-072：接受第九节评审的定性——**停止在边界状态机上打补丁**；本轮把未验证的探针代码入库前先隔离

- **评审结论（第九节，已读）**：三条约束互斥——① 批量交付（`T2_PLAN_GROUP_MAX` 2–4 组/次）；
  ② 在途不重解 K-2（`action_group_in_flight == group` 就跳过）；③ 逐组计划链（`previous.group+1 == group`）。
  三者同时成立的后果是窗口边界处 Core0 无法派生 group N 的计划，位置静默停住。评审的 4 组/2 组探针都复现了该停点。
- **与台架事实的关系（重要，不矛盾）**：我们把 `T2_PLAN_GROUP_MAX` 提到 **16** 后，**一个位置刚好装满一个窗口**，
  边界根本不会出现——这正是台架测到的 `executed = 16 × 114`（SWITCH_A/B，零失败）与 `16 × 108`。也就是说
  **路线 A 的"动作侧整条展开、无边界"在真机上已被证明可行**，评审否掉的是"批量窗口 + 逐组链"这个组合，不是 16 组本身。
- **采纳的路线：A（动作侧 owner）**，并据此"检修"：
  1. 保留"一个位置 = 一个窗口"（`T2_PLAN_GROUP_MAX = 16`，tick 存 32 位增量——RAM 已验证通过）；
  2. **删除**边界机制：`next_tick` 提升、`s_group_handed` 驱动的"窗口内前进"、`s_waiting_window`、边界清版本分支；
  3. 只保留一条推进规则：**`completed_sequence` 前进一步 ⇒ 该组完成**（开关/网关同构）；
  4. 口径统一并登记：台架目标改为按角色 **8 / 2 / 2** 组（`TRIGGER_T2_TODO.md:72`），K-6 的"16 组"标注为模型源口径；
     `T2-SEQ-001` 补登记到 `DOCS_REGISTRY.md`，避免台架目标与文档再次漂移。
- **本轮执行情况**：
  - 台架在本轮末尾连续失败（P3 步骤 4 直接 FAIL，疑似串口/采集侧残留占用），**无法产出新凭证**；
  - 因此把刚加的"运行期 BACKEND 三来源计数"（`backend_identity/terminal/arm`）**用 `git stash` 隔离**
    （`stash@{0}: runtime BACKEND buckets (pending P3)`），保证工作区干净、代码可复现；下一轮台架恢复后
    `git stash pop` → P3 → 提交；
  - 已提交的最后一个固件状态仍是 `fcbe9bf4`（网关组序号模型 + READY 默认禁用）。
- **下一轮（检修第 1 步，最小可验证）**：先做台架健康检查（四板 `*IDN?` + P3 步骤 0-7），再删除边界机制中的
  **一项**（`s_waiting_window`），跑一次 N=8（新口径）与 N=16（原口径）短采样，`executed = 组数 × 位置数` 不下降才算过。









### T2-PROGRESS-20261018-071：按用户指示先禁用 READY；网关改为"上报组序号"（同开关模型）——网关仍 BACKEND，且本轮 SWITCH_A 异常未运行

- **两处改动（已提交）**：
  1. 网关 `schedule_at`/`service` 把 **PIO 回执（触发计数）** 与 **组序号** 分开：`sequence = 计划基号 + k`，
     `completed_sequence = 该组序号`，与开关完全同构（此前网关把触发计数当序号上报，运行期比的是组序号，
     所以 `completed > expected` 永不成立）；
  2. `s_ready_required` 默认改 **false**（READY 暂不使用，待接网分后 `TRIGger:SEQuence:REAdy 1` 打开）。
- **台架（N=16，20 s）**：
  ```
  SWITCH_B: executed = 1728 = 16 × 108   NO_PLAN/LATE/BACKEND 全 0   window(16,16,0,COMPLETE)
  SWITCH_A: executed = 0                window state=1(LOADED)     ← 本轮该板根本没运行
  GATEWAY : executed = 0  BACKEND = 108
  GWDIAG  : plan_count=0 ready_active=0 ready_required=0 pulse=20 collision=108 rejects=全 0
  ```
- **两个待解问题（都很具体）**：
  1. **SWITCH_A 为何 state=LOADED 且 window 全 0** —— 同一轮 SWITCH_B 跑满，说明是这块板/这次装载的个案，
     下一轮先读它的 `T2?`（configure diag / armed）与开关页确认是 lease 未拿还是武装被拒；
  2. **GATEWAY 仍 BACKEND=108 且拒绝桶全 0** —— `stage_sequence` 的 8 个出口一个都没计到，而 `collision=108`
     恰好每位置一次，说明它**进入了 `stage_sequence` 并通过了守卫**（collision 在守卫之后），随后失败发生在
     *运行期* 的武装/身份判定；下一轮在运行期 `action_fail(BACKEND)` 的**三个来源**各加一个计数
     （身份不符 / 未武装 / 后端返回 false），一次台架即可定位。
- **开关侧不变**：16 组单窗口仍是 `16 × 位置数`、零失败（本轮 SWITCH_B 1728 = 16×108 再次确认）。









### T2-PROGRESS-20261018-069：READY 已在采样前强制下发（`ready_required=0` 确认生效），网关剩下的问题是"计划装满但从不排空"

- **工作流改动**：`TRIGger:SEQuence:REAdy <env>` 现在在**采样循环开始前**对四块板各下发一次并当场回读
  （此前在装表步/启动后下发，都会被 TDMA 启动与设备装载的模块重初始化抹掉——`REAdy?` 收尾总是 1）。
  修完首次真正生效：`GWDIAG` 里 `ready_required=0` ✅。
- **本轮读数（N=16）**：
  ```
  SWITCH_A / SWITCH_B: executed = 1744 = 16 × 109 位置   NO_PLAN/LATE/BACKEND 全 0   window(16,16,0,COMPLETE)
  GATEWAY             : executed = 0   BACKEND = 109
  GWDIAG: plan_count=16  ready_active=0  ready_required=0  pulse=20  collision=310
          拒绝桶 = [0,0,0,0,0, READY=128, 0, 0]
  ```
- **读法**：拒绝桶里 **READY=128** ⇒ 运行期间确有 128 次"待决 READY + 要求 READY"被拒（这些发生在我强制下发之前）；
  而 `plan_count=16` 说明**最后一次成功装入了 16 组计划，但一直没有排空** ⇒ 网关的"一组一组往下走"没有发生：
  首脉冲要么没完成、要么完成事件没被消费。这才是网关当前的真正缺陷（不是 READY）。
- **下一轮（聚焦 plan 排空）**：在 `service_core1` 的完成分支加三个计数（`receipt 收到 / 不匹配 / 计划推进`），
  并读 `s_plan_next`；若收款从未到达，查 `pio_sm_is_rx_fifo_empty(OUTPUT_SM)` 与 `s_realtime_pending`；
  若是完成事件被 READY 逻辑吞掉，把 READY 处理移到完成判定之后。
- **开关侧保持不变**：16 组单窗口、零失败（本轮 1744 = 16 × 109）。










### T2-PROGRESS-20261018-068：网关有了独立诊断页；读数指向"REAdy 下发之后又被复位成 1"

- **新增**：`TRIGger:REServation:T2:ACTion:GWDIAG?` —— `schema(0xDA02), plan_count, ready_active, ready_required,
  pulse_ns, collision_count, margin_min_ticks, 8 个 stage 拒绝桶`（网关自己的页，字段不可能被别的追加块挤偏）。
- **本轮读数（N=16，短采样）**：
  ```
  GWDIAG: schema=0xDA02 plan_count=0 ready_active=0 ready_required=1 pulse_ns=20 collision=109 margin=0 rejects=全 0
  读数: executed=0  BACKEND=109（每位置一次）
  ```
- **两个事实互相矛盾，正好指出下一步**：
  1. `collision=109` ⇒ 有 109 次是在 `ready_required == 0` 的分支里把待决 READY 退掉的 ⇒ **运行期间该标志确实是 0**；
  2. 但收尾时读回 `ready_required=1`，且 8 个拒绝桶全 0 ⇒ 收尾前标志被**复位成默认值 1**（`trigger_sequence_timing_reset()` 或某次 configure/stop 路径），
     并且 `stage_sequence` 的拒绝出口没有一个被计到（说明"拒绝"发生在更前面，或失败时用的不是这条路径）。
- **下一轮（两条并行，都很快）**：
  1. 让工作流在 **T2 configure 之后**再下发一次 `TRIGger:SEQuence:REAdy 0`（现在是配置前下发），并在第 12 步后立刻读 `GWDIAG?` 确认标志为 0；
  2. 在 `trigger_sequence_timing_reset()` 的调用点上打点（谁在运行期把 `ready_required` 复位），或在 `T2:CONFigure` 里**保留**该标志（配置不应改变它）。
- **已完成并保持**：开关侧 16 组单窗口 `executed = 1824 = 16 × 114`、零失败（`22b0202f`）。










### T2-PROGRESS-20261018-067：**16 动作/位置在真机上跑通**——单窗口装 16 组，`executed = 1824 = 16 × 114 位置`，零失败

- **做法（去掉滑动窗口这一整类限制）**：计划里把每组的 tick 存成**相对窗口基准的 32 位增量**、参数存成 16 位
  （每组 6 字节），`T2_PLAN_GROUP_MAX` 4→**16**、`SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS` 8→16。这样**一个窗口装完一个位置的全部 16 组**，
  边界/等待标志那些分支根本不会被走到——正是"减少冗余限制"的正确落法：不是删守卫，而是让需要守卫的场景不再出现。
  - 之前 `group_tick[16]` 用 uint64 时 RAM 溢出 364 B；改成增量后 96 B/槽（原 48 B/槽），链接通过。
- **台架（N=16，20 s 采样，22 步 OK）**：
  ```
  SWITCH_A / SWITCH_B: executed = 1824   groups = 16   NO_PLAN = 0  LATE = 0  BACKEND = 0
                       窗口页 count=16 fed=16 queued=0 state=COMPLETE last=1937
  ```
  `1824 = 16 × 114` ⇒ **114 个位置、每个位置 16 个动作全部执行，零失败**。
- **唯一剩余阻塞**：GATEWAY `executed=0 / BACKEND=114`（每位置一次），其窗口页全 0 ⇒ `stage_sequence` 从未成功武装；
  DIAG 里我新加的 8 个网关拒绝桶**全为 0**，说明失败发生在进入 `stage_sequence` 之前或那几个桶的偏移还没对齐。
  下一轮：给网关做**独立诊断页**（`TRIGger:REServation:T2:ACTion:GWDIAG?`，schema + 固定字段），把
  `s_plan_count / s_ready_active / s_ready_required / schedule_at 的返回` 直接读出来。










### T2-PROGRESS-20261018-066：按"减少冗余限制"做了一次激进删减 → 台架反而全 BACKEND（已回退，教训与正确做法写明）

- **用户意见**：这条路径上堆了太多冗余限制，所以跑不起来。方向对，我按它删了四样：
  ① `s_waiting_window[]` 与窗口边界分支（清版本/释放 arm）；② 推进分支里的回执闸门
  （`!s_switch_receipt_seen → return`）；③ 以 `sequence` 为键的第二个推进分支；④ `position_fallback`
  自带的计划/地板否决。目标只剩一条规则：**按 `completed_sequence` 推进**。
- **台架结果（N=16）**：`executed=0`、`BACKEND=108`（三块从板一致），窗口页 `count=4 fed=2 queued=0
  state=5(STOPPED)` ⇒ 反而更糟：删掉回执闸门后，运行期把"装载了下一组"当成完成，组游标跑在硬件前面，
  随后武装身份不符 → BACKEND，开关停在 STOPPED。
- **结论（重要，写进设计）**：这些限制里只有"回执闸门"和"按 `completed_sequence` 推进"是**必需**的，
  其余（等待标志/边界清版本/`sequence` 分支/回退自带否决）才是冗余。**删减必须逐项做、每项上台架**，
  不能一次删四项——与"单步验证"同一条纪律。
- **本轮已回退**（`trigger_t2_runtime.c` 与 runtime mock 均 `git checkout`），回到 `5605927d` 的状态：
  构建绿、`test_trigger_t2_runtime` **37 passed**、工作区 clean（仅 5 个未跟踪的 out/ 临时文件）。
- **下一轮的删除顺序（每步都跑一次 N=16 短采样，判据 executed 不下降）**：
  1. 先删 `s_waiting_window[]`（改为：边界只清版本并返回，不设等待标志、不跳选择）；
  2. 观察无回归后再删 `position_fallback` 里的自否否决；
  3. 最后才考虑把"回执闸门"合并进 `completed_sequence` 判断（它们语义重叠，但合并必须保留"每组只计一次"）。
- **顺带完成并已提交**：网关 `stage_sequence` 八个拒绝出口分桶 + `window_boundary` 暴露（`5605927d`），
  下一轮网关 BACKEND 可以直接读出原因（含 READY/NO_LEASE/BD_* 等）。










### T2-PROGRESS-20261018-065：窗口边界推进已加，但 N=16 仍停在第 4 个窗口（executed=12）

- **改动（已提交）**：`complete_action()` 的推进分支加"窗口边界"处理——当 `next_tick == 0`（本窗口最后一组，下一组属于新窗口）时，把 `s_deadline.group` 前移到 `s_group_next`、清 `group_count` 与 `prediction_version`，强制 Core0 发布下一窗口的首组；并加计数 `window_boundary`。
- **台架（N=16，20 s，22 步 OK）**：`executed=12`、`groups=16`、`NO_PLAN=108`、`LATE=3`、`BACKEND=0`；窗口页 `count=4 fed=4 queued=0 COMPLETE last=14`。
  - `last=14` 说明**第 4 个窗口确实被武装过**（base=14，覆盖组 12..15），但它的四组一组都没完成 ⇒ 卡在"第 4 窗口已装、回执不来"。
- **本轮的一次台架故障**：首跑在步骤 6/16-20 失败（origin 板 `'EMPTY'` 校准页、`phase 5 fault 3`），重跑即恢复——属台架侧偶发，已记录。
- **下一轮**：① 把 `window_boundary` 输出到 `DIAG?`（现在只写不读，评审 SEQ-B06 同类问题）；② 若 `window_boundary>0` 且第 4 窗口零完成，则查开关侧：第 4 窗口的 `stage_sequence` 是否被 `s_switch.state==PENDING` 拒绝（`schedule_at`/`stage_sequence` 都要求非 PENDING），以及 `sequence` 是否与运行期 `expected` 脱节；③ 判据 `executed = 16 × 位置数`。










### T2-PROGRESS-20261018-064：N=16 前三个窗口跑通（executed=12），第 4 个窗口起停住；单窗口 16 组被 RAM 否决

- **改动（已提交）**：`complete_action()` 的"本位置有几组"改为**按角色模板**取（此前取"已交出窗口"的大小，导致 16 组的位置在第 4 组就被判完成）。这样 4 组一窗、逐窗滑动成为可能。
- **N=16 台架（`T2_SEQ_GROUPS='1,16,16,16'`、`T2_SEQ_PERIOD_SLICES=1`、20 s）**：
  ```
  executed=12  groups=16  NO_PLAN=108  LATE=3  BACKEND=0
  window(count,fed,queued,state,last) = 4,4,0,COMPLETE,14
  ```
  `executed=12` = **3 个窗口 × 4 组** ⇒ 滑动确实在跑；停在第 4 个窗口（组 12..15）。
- **尝试过"单窗口装 16 组"（已回退）**：把 `T2_PLAN_GROUP_MAX` 4→16 后**链接失败**——`region RAM overflowed by 364 bytes`（计划结构在每个槽位都有一份，361 份 × 12 组额外 tick 太大）。所以 16 组只能走滑动。
- **下一轮**：查第 4 个窗口为什么不被接受/推进 —— 关键在 `complete_action()` 的推进分支（`s_group_next < s_group_handed` 才走"窗口内前进"，否则走"Core0 重新解算下一组"）。预期是后者在组 12 处 `next_tick`/`group` 对不上（第 3 个窗口收尾时我重置了 `s_deadline.group/next_tick`，可能把第 4 个窗口的接力信息清掉了）。判据：`executed = 16 × 位置数`。










### T2-PROGRESS-20261018-063：**N=3 全线跑通**——`executed=357 = 119 位置 × 3 组`，`NO_PLAN/LATE/BACKEND` 全 0

- **修法（评审 SEQ-B03，`docs/trigger/t2/TRIGGER_T2_REVIEW.md` §7.5）**：`sync_io_switch_deadline_status_t` 增加 **`completed_sequence`**（"刚刚消费完的那一组"，永不超过硬件），`poll()` 在每次出队时写入、补装重指时保留；运行期窗口分支改为**只按 `completed > expected`** 推进 `complete_action()`，不再用"当前 head 的 sequence + 回执"猜组号，所以同一次 COMPLETE 只被消费一次。
- **台架（N=3，20 s 采样，22 步全 OK）**：
  ```
  SWITCH_A / SWITCH_B: executed=357  NO_PLAN=0  LATE=0  BACKEND=0  freeze_missing=2/1
                       window(count,fed,queued,state,last) = 3,3,0,COMPLETE,475
  ```
  `357 = 119 位置 × 3 组` ⇒ **多组窗口在真机上完整成立**，且与位置数严格成 3 倍关系。
- **这一轮的因果链（全部有据）**：评审指出 SEQ-B01/B02（补装不重指 status）、SEQ-B04（序号固定 +2 导致窗口区间重叠）、SEQ-B05（队列深度无关）→ 上一轮修完 `executed` 6→3（第 1 个位置跑完 3 组）→ 本轮修 SEQ-B03（按 `completed_sequence` 推进）→ `executed=357`、零失败。
- **下一步**：① N=16（`T2_SEQ_GROUPS='1,16,16,16'`，需把 `T2_PLAN_GROUP_MAX` 4→16 或依赖窗口滑动）；② 361 位置全链（`T2_SAMPLE_TICKS=110`）；③ 接网分验 READY。










### T2-PROGRESS-20261018-062：采纳评审（SEQ-B01/B02/B04/B05）——窗口序号不再重叠、状态随补装重指；`executed` 由 6 变 3（第 1 个位置完整跑完 3 组）

- **依据**：`docs/trigger/t2/TRIGGER_T2_REVIEW.md` 第七节的独立审核（主机探针 + 读码），指出三个阻塞点：
  - `SEQ-B01/B02`：`poll()` 换 head 时先置 COMPLETE，补装下一组后**不重指 `s_deadline`**，导致 "COMPLETE 与 queued>0 并存"、后续组的完成不进入 status；
  - `SEQ-B04`：`stage_sequence` 的语义应是 "最后发出的序号" = `base+count-1`，原来固定 +2，N=3 起相邻窗口区间重叠（`[1,3]`、`[3,5]`…）；
  - `SEQ-B05`：`SWITCH_QUEUE_DEPTH` 从 2 提到 4 对这条路径无效（TX FIFO 只容 2 组），单条 API 仍维持 2 深。
- **本轮改动**（`sync_io_switch.c`）：补装后**重指 status**（ARMED + 新 head 的 deadline/sequence、`receipt_mask=0`）；`stage_sequence = base + count − 1`；QUEUE_DEPTH 回到 2（单条路径显式 `SWITCH_SINGLE_DEPTH`）。
- **台架（N=3）**：
  ```
  executed=3（原 6）  NO_PLAN=116  LATE=3  BACKEND=0  freeze_missing=116
  window(count,fed,queued,state,last) = 3,3,0,4(COMPLETE),11
  ```
  `last=11` 说明窗口序号现在是 `9..11`（上一窗口 `5..7` 之后）⇒ **SEQ-B04 已修好，序号不再重叠**；并且**第 1 个位置完整跑完 3 组**（executed=3）。
- **剩余**：`SEQ-B03`（运行期把同一个 COMPLETE 消费两次、且以"head 的 sequence + 回执"猜组号）仍在，导致后续位置 NO_PLAN。下轮按评审建议 #1：给 `sync_io_switch_deadline_status_t` 增加 `completed_sequence`，运行期只按该事件推进 `complete_action()`，不再猜组号；判据 `executed = 3 × 位置数`、`NO_PLAN = 0`。
- **评审还指出**（未做）：`SEQ-B07` 主机 mock 在每组推进时给 `receipt_mask=1`、与生产语义不符（换 head 清零），所以 37 项 runtime 测试全绿但没覆盖真实契约——修 SEQ-B03 时要同步修 mock。











### T2-PROGRESS-20261018-061：两个真 bug 修掉了（窗口序号不自增、COMPLETE 不等 FIFO 排空），但开关仍在"第 2 组后"收尾

- **修 1（确凿）**：`sync_io_switch_realtime_stage_sequence()` **从未自增 `s_switch.stage_sequence`**（单条 `schedule_at` 会自增），于是每个窗口都复用同一个 base 序号。修后元组从 `seq=4` 变成 `seq=6`（序号确实开始推进）。
- **修 2（确凿）**：`poll()` 用"已推送组数"判 COMPLETE 并随即关闭 SM，**没有等 TX FIFO 排空**，会把还在 FIFO 里的最后一对[码,延时]连同回执一起丢掉。现在要求两个 SM 的 TX FIFO 都为空才收尾。
- **修后的台架读数（N=3）**：
  ```
  executed=6  NO_PLAN=103  adv=6  finish=0
  tuple: seq=6  expected=6  window_last=7  complete=1
  窗口页: count=3  fed=3  queued=0  state=COMPLETE  last_seq=基准值
  ```
  ⇒ `count=3 / fed=3` 说明三组都推了，但 **`completed_sequence` 只到基准值**（只结算了一组回执），COMPLETE 却在"第 2 组"就到了。
- **结论**：`poll()` 的"队列空 + 已推送完"判定与实际回执数不一致——最可能是 `sequence_feed()` 的入队（`s_queue[s_queue_count++]`）在最后一组时被 `s_queue_count >= SWITCH_QUEUE_DEPTH` 拒掉（深度 2），于是那组的回执无处归属，但 `s_sequence_group` 仍被加 1 ⇒ "已推送完 + 队列空" 立刻成立。
- **下一轮**：把 `SWITCH_QUEUE_DEPTH` 从 2 提到 4（一组一项，最多 4 组窗口不丢回执），并在窗口页加 3 个字段（COMPLETE 时刻的 `count/fed/queued`）；判据：`executed = 3 × 位置数`、`NO_PLAN=0`。











### T2-PROGRESS-20261018-060：元组给出答案的一半——开关在 `sequence=4` 时就报 COMPLETE，而窗口要到 5

- **精确元组**（两块从板一致，本轮新增 4 个字段）：
  ```
  executed=6  NO_PLAN=119  adv=6  finish=0
  window tuple: seq=4  expected=4  window_last=5  complete=1
  ```
- **解读**：`window_last = base + handed − 1 = 5` 说明运行时确实交出了 **3 组**（base=3）；而开关在 **`sequence=4`（即第 2 组之后）就置 COMPLETE** ⇒ **开关只认到 2 组**，第 3 组从未触发/从未计数。
  - 因为 `seq(4) == expected(4)` 且 `complete=1`，运行时会走 `complete_action`，但此时 `s_group_armed` 已经是组 2 之前的组号（adv=6 = 每槽 2 次），**收尾条件 `group+1 == groups_total` 永远不成立**，于是 `finish=0`、位置停在"已冻结未完成" ⇒ 同槽位后续位置 `NO_PLAN=119`。
- **结论**：问题在**开关侧"组数"口径**：`s_sequence.count` 是 3，但 `s_sequence_group`/COMPLETE 判定只走了 2 组。下一轮在开关侧加两个计数：**实际触发的 `trigger_count` 增量** 与 **`s_sequence_group` 终值**，与 `s_sequence.count` 对照，即可确定是 `sequence_feed()` 在最后一次没推入、还是 COMPLETE 判定提前（`s_sequence_group >= s_sequence.count` 的口径）。
- **顺带**：这也是为什么 N=2 一直正常——2 组时"第 2 组之后 COMPLETE"恰好与窗口长度一致。











### T2-PROGRESS-20261018-059：三组位置**只推进到第 3 组之前**（adv=6 / finish=0），且三槽位行为完全一致

- **用户的澄清（重要，写入模型）**：**FREEZE 是同步发给三个槽位的**——同一个位置在三个槽位上被冻结，逻辑应当完全相同。台架读数与此一致：`freeze_hist = 38,38,38`（三槽各 38 次），三槽的 `group_next` 都停在 **2**，说明这是**同一处逻辑**的问题，不是某个槽位特殊。
- **决定性计数**（窗口页尾，字段已对齐）：
  ```
  SWITCH_A / SWITCH_B: executed=6  NO_PLAN=111  adv=6  finish=0  finish_group=0  finish_total=0
                       no_plan_zero=111  slots frozen=[0,0,1,0]  group_next=[2,2,2,0]
  GATEWAY            : executed=0  BACKEND=114  adv=0 finish=0
  ```
  ⇒ 每个位置**只推进到第 2 组结束**（0→1、1→2），第 3 组的完成**从未被识别**，于是 `finish` 永远为 0，槽位停在 `frozen + failed`（slot2）或 `frozen` 且 `group_next=2`。
- **已修（本代码提交）**：窗口分支此前对"最后一组完成"也会走回执闸门（`!receipt_seen` ⇒ 直接 return），使 `expected` 永不前进、`complete` 分支永远到不了；现在 `sequence == window_last && complete` 直接 `complete_action` 并收尾。**但台架读数未变**（adv=6 / finish=0），说明真正的阻断点还在更前面：`complete` 或 `window_last` 与该时刻的实际值不符。
- **下一轮（打印精确元组）**：在窗口分支加 4 个临时字段：`sequence`、`expected`、`window_last`、`complete`（取最近一次），一次台架即可看出最后一组到底上报成什么；同时给网关的 `BACKEND` 加拒绝桶（它当轮 114 次全部 BACKEND）。
- **注**：GATEWAY 仍 114 次 BACKEND（已下发 `REAdy 0`），需要单独看它 `stage_sequence` 的拒绝桶。











### T2-PROGRESS-20261018-058：更正上一轮的读数 + 新假设——三组位置**从未走到收尾**（`group_next` 停在 2）

- **更正**：上一轮我读 `win[-2:]` 当作"zero/floor 桶"，但那两个字段在**槽位探针之后**，所以读到的是探针数据。真实情况：`freeze_missing=105` 说明 **NO_PLAN 是 FREEZE 路径判的**（不是选择路径）。
- **当前最可信的假设（有探针支撑）**：探针里 `group_next=2` 出现在**已收尾**的槽位上，而我第 57 轮加的收尾复位会把 `s_group_next` 置 0 ⇒ **收尾分支根本没有执行**。也就是说三组位置在 `s_group_armed==2` 之后既没有再推进、也没有收尾：槽位一直 `frozen` 且未完成，于是同一槽位的后续位置全部卡在 FREEZE → NO_PLAN（`freeze_missing=105`、`executed=6` 正好是"前两个位置跑完 3 组"）。
- **本轮附带修复**（`position_fallback`）：该回退路径过去只检查"有描述符"就冻结，冻结时**没有计划**，必然在后续判 NO_PLAN；现在要求"有计划且版本高于槽位地板"才冻结（`fallback` 计数=4，说明它确实在跑，但不是主因）。
- **下一轮（决定性，两个计数即可）**：在 `complete_action()` 里分别统计 **推进(advance)** 与 **收尾(finish)** 次数，并统计"收尾时 `groups_total` 的值"；若三组时 `finish=0`、`advance` 停在同一组，则问题就是窗口末组的完成事件没被识别（回执/sequence 与实际组号错位），修法在窗口分支的组号映射。











### T2-PROGRESS-20261018-057：槽位映射的代码语义已确认，但探针与它矛盾——下一轮从"origin 实际发布了哪些槽位"入手

- **代码语义**（`trigger_t2_runtime.c:232-236`）：扫描序号 `k` 取槽位 `k % POSITION_COUNT`，上线的 `position_id = slot + 1`；一个槽位只有在前一占用者**结束后**才会被复用，预测版本按槽位单调上升。
  ⇒ 若这条路径正常，110 个位置应分散在 slot 0..109。
- **探针实测（N=3）却只有 slot 2 活跃**：
  ```
  slot 0: frozen=0 completed=0 failed=0 recv_seen=524 group_next=2
  slot 1: frozen=0 completed=0 failed=0 recv_seen=594 group_next=2
  slot 2: frozen=1 completed=0 failed=1 recv_seen=216 group_next=2
  slot 3: 全 0（recv_seen=0，从未收到任何计划）
  ```
  `recv_seen` 是每个槽位的邮箱代际计数（每次发布 +2），所以 slot 0/1 **收到过**计划，slot 3 **一次都没收到**。
- **矛盾点**：`recv_seen(slot0/1) ≫ 0` 与 `frozen=0` 并存 ⇒ 计划发到了这些槽位，但 FREEZE 从未到过它们；同时 ~105 个位置被判 NO_PLAN。这说明**FREEZE 与计划的槽位号对不上**（或 FREEZE 全落在极少数槽位上）。
- **下一轮（唯一实验）**：在从板加"FREEZE 到达次数 per 槽位"的直方图（前 8 槽），并在 origin 板读 `published_mask`；两者对照即可确定是"origin 只发少数槽位"还是"FREEZE 只到少数槽位"。
- **本轮无代码改动**（仅分析）；上一轮的收尾保留计划修复已在 `958a13c9`。












### T2-PROGRESS-20261018-056：槽位探针给出新线索——**只有 slot 2 被使用**，其余槽位从未冻结

- **新仪器**：窗口页尾 24 字段 = 前 4 个槽位 × `(frozen, completed, failed, plan_version, recv_seen, group_next)`。
- **N=3 探针读数**（两块从板一致）：
  ```
  slot 0: frozen=0 completed=0 failed=0 plan_ver=0 recv_seen=524 group_next=2
  slot 1: frozen=0 completed=0 failed=0 plan_ver=0 recv_seen=594 group_next=2
  slot 2: frozen=1 completed=0 failed=1 plan_ver=0 recv_seen=216 group_next=2
  slot 3: 全 0（从未使用）
  ```
  ⇒ **整轮只用了 slot 2**（position_id=3），且它停在 `failed=1 / group_next=2`。这与 `executed=6`（2 位置×3 组）完全吻合：一旦 slot 2 失败并再也没被"当作新位置"接受，后面所有位置都判 NO_PLAN。
- **本轮修复（已提交）**：`complete_action()` 的收尾分支此前把该槽位的 `prediction_version` 清 0（第 57 轮的"清洁性改动"），使 Core0 早已为下一位置发布的计划**不可见**（邮箱 guard 未变 ⇒ `deadline_receive` 不再重读）。现在改为**保留计划**、只把 `s_deadline_seen` 清零以强制下一次重读；残留计划由 `recycle_slot()` 抬高的槽位地板（版本 ≤ 地板即拒绝）兜住。
- **仍未解决**：`executed` 依旧 6、`NO_PLAN=105`。**下一轮首要线索**：为什么所有位置都落到 slot 2？可能原因：origin 侧 position_id 恒为 3，或 `T2_TARGETS` 只配了一个目标槽。验证方法：统计 `descriptor.position_id` 的分布（每槽计数），若确认恒为 3，则问题在"位置→槽位"的映射，而不在计划供给。












### T2-PROGRESS-20261018-055：N≥3 的测试矩阵全部为负，收敛到"槽位只在前两个位置被回收"

- **本轮又排除了三个因素**（每次只改一项，22 步全 OK）：
  | 变量 | 取值 | `executed` | `NO_PLAN` |
  |---|---|---|---|
  | 组间隔 `T2_SEQ_PERIOD_SLICES` | 2 / 8 | 6 / 6 | 110 / 119 |
  | `--subdivisions` | 32 / 16 | 6 / 6 | 110 / 184 |
  | `--period-ns`（位置周期） | 1 ms / 4 ms / 100 ms | — | 6 / 6 / 6（三次都是 executed=6） |
  | FREEZE 前就地刷新槽位（新增 `deadline_receive_slot`） | 有 | 6 | 112 |
  | 取消"武装清版本" | 有（`recv_ok` 从 109 涨到 654） | 6 | 112 |
- **常量 `executed=6`** 贯穿全部配置 ⇒ 恰好是 **2 个位置 × 3 组**。这说明不是时序、不是间隔、不是版本，而是**前两个位置之后，槽位再也不被回收**（`recycle_slot()` 只在 `promote_stashed()` 里被调用，需要 `s_stash_valid`；或由"更新的 PREVIEW"触发）。`skip_accepted≈7.5 万` 之前被我判为"正常空转"，现在看它是这个死锁的表现。
- **下一步（下一轮唯一实验）**：给 `recycle_slot()` 调用点加两个计数（`recycle_promote / recycle_newer_preview`），并统计 `s_stash_valid` 的置位次数；若确诊"回收未发生"，修 `promote_stashed` 的触发条件（允许"已失败"的占用者也被后继 PREVIEW 直接回收，而不必先有 stash）。
- **已提交的实质改进**（本轮代码）：`deadline_receive()` 拆出 `deadline_receive_slot(index)` 并在 FREEZE 前就地刷新；跨位置的组号比较语义修正；取消武装时的版本清零（保留 FREEZE 的"版本 ≤ 地板"判据）。宿主 37 passed、构建绿、P3 通过。












### T2-PROGRESS-20261018-054：计划"发布在前、接收成功"，却在 FREEZE 里读到 0 —— 竞态在"武装清版本"与"下一位置计划到达"之间

- **新计数**（窗口页尾）：`publish_before_freeze / publish_after_freeze` 与 `recv(busy,no_version,session,older,ok)`。
- **N=3 台架读数**：
  ```
  SWITCH_A: executed=6  NO_PLAN=109
     pub(before,after) = 109,0        ← 计划全部在冻结前发布（“晚发布”假设否定）
     recv(busy,noVer,session,older,ok) = 0,0,0,0,109   ← 计划全部被接收（四个早退桶全为 0）
  SWITCH_B: 同构（117 / ok=117）
  ```
  ⇒ **发布在前、接收也成功，但 FREEZE 读到的本地版本仍是 0**（`freeze_missing=109`）。
- **结论（竞态）**：`s_deadline[slot]` 被两件事同时触碰——(a) `deadline_receive()` 写入下一位置的计划；(b) 位置**武装**时把该槽位的 `prediction_version` 清 0（用于标记"这份计划已被消费"）。三组时窗口更长，当 (b) 发生在 (a) 之后，FREEZE 就看到 0 ⇒ NO_PLAN。N=2 因为窗口短、(b) 总在 (a) 之前，所以从不暴露。
- **修复方案（下一轮实施，已定）**：在 FREEZE 读计划之前，对该槽位做一次**就地邮箱刷新**（`deadline_receive_slot(index)`，只读一个槽位、代价 O(1)）：若邮箱里有更新的计划就用它，否则才判 NO_PLAN。这样不动"武装清版本"的语义（它仍然是 Core0 重新发布的信号），只把竞态窗口补上。
- **另一个已提交的潜在 bug 修复**：`deadline_receive()` 里"更小的组号=更旧"的判断现在只在**同一预测版本**下成立——跨位置（新版本、组 0）的计划不再被丢弃（虽然本轮 N=3 的结果未变，但这是正确的语义）。












### T2-PROGRESS-20261018-053：计划**有**发布（118/327 次），但 FREEZE 时槽里仍是 0 —— 说明"发布晚于冻结"

- **新增到自包含窗口页的四个供给计数**：`published, no_descriptor, version_zero, expand_fail`。
- **N=3 台架读数**（`T2_READY_REQUIRED=0`，22 步全 OK）：
  ```
  SWITCH_A: executed=6  NO_PLAN=110  window(count,fed,queued,state,last)=3,3,0,4,5
            advance=6 jump=0 reuse=159 no_receipt=3
            skip(unreachable,in_flight,accepted)=0,351,75069
            supply(published,no_descriptor,version_zero,expand_fail)=118,87256,0,0
  SWITCH_B: executed=6  NO_PLAN=94   同一结构，published=327
  ```
- **逐条解读**：
  1. `version_zero=0`、`expand_fail=0` ⇒ 展开与版本都正常，**不是**这两个的原因（前几轮的两个怀疑到此彻底排除）；
  2. `published=118/327` ⇒ **计划确实发布了**（远超实际执行的 2 个位置）；
  3. `no_descriptor=8.7 万/9.6 万` ⇒ 求解器绝大多数轮次是因为"该槽位没有新描述符"而空转（361 个槽位每轮都扫，属正常空转，不是错误）；
  4. `skip_accepted=7.5 万` 同样是大基数空转（已收尾的槽位），不代表死锁——**上一轮我把这个当死锁的判断是错的**。
- **真正的不一致**：`published` 有 118 次，而 `NO_PLAN=110` 个位置在 FREEZE 读到的本地版本是 0（`freeze_missing≈358` 累计）⇒ **发布发生在 FREEZE 之后**。也就是说：三组时槽位被占用更久，后继位置被 `stash_successor` 暂存，等占用者收尾、`promote_stashed` 回收后已经来不及解出计划，FREEZE 先到。
- **下轮（直接可验）**：给每个位置记两个计数——`published_before_freeze` / `published_after_freeze`（在 `deadline_publish` 时比较该槽位是否已 `frozen`），一次 N=3 台架即可确认"晚发布"；若确认，修法是**允许在占用者最后一组在飞时就为后继位置解计划**（`prepare_deadlines_core0` 对 `in_flight==group` 的槽位不再整段跳过，而是只跳过"该组本身"），这样后继计划的解算时间就从"收尾后"提前到"收尾前一个动作"。












### T2-PROGRESS-20261018-052：窗口完成判定加上「必须有回执」——开关侧队列已能清空、状态回到 COMPLETE；剩余 NO_PLAN 转到计划供给

- **新增窗口统计**（`T2:ACTion:WINdow?` 尾四字段：`advance, jump, reuse, no_receipt`）。
- **修前实测（N=3）**：`advance=3, jump=0, no_receipt=3, reuse=159` ⇒ **每一次“窗口内前进”都没有回执**：开关只是装载了下一组（状态仍 PENDING、`queued=1`、`last_seq=4`），而运行时把它记成“上一组完成”（`executed=6`）。N=2 看起来正常只是因为回执恰好落在同一次轮询里。
- **修法**：窗口内“sequence 前进”只有在**同一状态里带回执**（`receipt_mask != 0`）时才计为完成；否则只更新 deadline，不推进组游标。
- **修后台架（N=3）**：
  ```
  开关窗口: count=3 fed=3 queued=0 state=4(COMPLETE) last_seq=5  advance=6 jump=0 reuse=166
  ```
  ⇒ **队列清空、状态回 COMPLETE**，不再永久 PENDING（这是实质进展）。
- **但 `executed` 仍只有 6（= 2 位置 × 3 组），NO_PLAN 115~117** ⇒ 瓶颈已经转到**计划供给**：只有 2 个位置拿到了计划。下一个怀疑点是 `prepare_deadlines_core0()` 入口的
  `t2_deadline_position_accepted(completed|failed, index)` 与 `action_group_next/in_flight` 组合——位置槽位循环复用时，若 `completed` 位未被新位置清掉，Core0 会**永久跳过**该槽位（N=2 下可能因时序不同没暴露）。
- **下轮**：给 `prepare_deadlines_core0` 的三个 `continue` 各加一个桶（`accepted_skip / inflight_skip / group_unreachable`），一次 N=3 台架即可确定是哪一个把 3..N 个位置全跳过；修好后判据仍是 `executed = 3 × 位置数`（先 N=3），再上 16。












### T2-PROGRESS-20261018-051：N≥3 的真凶方向：开关窗口**永远停在 PENDING**（队列残留 1 项），而运行时多算了完成数

- **新仪器**：独立页 `TRIGger:RESERVation:T2:ACTion:WINdow?`（`count, fed, queued, state, last_seq`），避免尾块偏移随固件漂移；工作流新增 `T2_READY_REQUIRED=0/1`，会在 T2 启动前下发 `TRIGger:SEQuence:REAdy`（无网分台架可用）。
- **N=3 台架读数（`T2_READY_REQUIRED=0`，22 步全 OK）**：
  ```
  SWITCH_A / SWITCH_B : 运行时 executed=6  groups=3  NO_PLAN=111  LATE=3
      开关窗口 : count=3  fed=3  queued=1  state=3(PENDING)  last_seq=4
  GATEWAY             : executed=0  BACKEND=114   ← 该板 REAdy 依然是 1（工作流的下发发生在配置阶段之前/之后需确认），下一轮核对
  ```
- **两个数字对不上，这就是症结**：
  1. 开关侧 `fed=3`（3 组全部喂入）但 **`queued=1`** ⇒ 最后一条队列项**永远没收到回执**，于是 `poll()` 走不到“队列空 → COMPLETE”，状态卡在 **PENDING**；
  2. 运行时侧 `executed=6`（两位置×3 组）而开关侧 `last_seq=4` ⇒ **运行时把完成数算多了**：我的窗口分支把“`sequence` 前进”当作“上一组完成”，但同一 `sequence` 可能被多次上报/或有跳跃，导致一次前进被记成多次完成，进而让 `s_group_next` 越过真实进度。
- **下一轮两条并行修法（都有明确判据）**：
  1. **运行时以 `completed_sequence` 判定组完成**（而不是 `sequence` 前进）：只有 `status.completed_sequence` 变成“本次期望的那一组”才 `complete_action`，其余只更新 `deadline_tick`；判据：`executed` 必须等于 `last_seq - base + 1`；
  2. **开关侧残留队列项**：在读回执时按 `receipt_mask` 与 `s_queue[0]` 的 code 匹配（当前只比 `width`/code 是否为 0），并把不匹配的一次计入新桶 `stale_receipt_count`；若确认是“多读了一次回执”，则改为“每次 `poll()` 最多消费一条、且必须匹配队列头”。
- **判据不变**：修好后 `T2_SEQ_GROUPS='1,16,16,16'` 的 `executed` 必须 = `16 × 位置数`、`NO_PLAN=0`，且 N=2 的 `361×2=722` 不回归。












### T2-PROGRESS-20261018-050：READY 可关（新命令实测生效）+ 表容量 8→48（16 行装进去了）；N≥3 的窗口续填仍缺

- **新配置项**（已提交，P3 `build=20261018013008` 之后的这次 P3 覆盖）：
  `TRIGger:SEQuence:REAdy <0|1>` / `?` —— 1（默认）表示“上一个 READY 未到就不许开下一个动作”，0 表示**台架诊断模式**：把待决 READY 退掉并**计数**为踩踏，链路继续跑。
  台架实测：`REAdy?` → `1,0,1`，发 `REAdy 0` 后 `REAdy?` → `1,0,0`（生效）。
- **表容量 8 → 48**：`TRIGGER_ACTION_TEMPLATE_CAPACITY` 提升后，`T2_SEQ_GROUPS='1,16,16,16'` 第 10 步**装表成功**（三块板 `count=16`），16 行不再是拦路点。
- **16 组台架读数（`--period-ns 10000000 --subdivisions 32`，采样 60 s）**：
  ```
  SWITCH_A : executed=6  groups=16  LATE=3  NO_PLAN=316  BACKEND=2
  SWITCH_B : executed=6  groups=16  LATE=3  NO_PLAN=316  BACKEND=2
  GATEWAY  : executed=0  groups=16  BACKEND=321   ← 该板 REAdy 仍为 1（本轮到得早于设置），属预期
  ```
- **结论**：`N=2` 的窗口交接已验证通过（361×2=722 零失败），但 **`N≥3` 会退化成 NO_PLAN**（N=3 与 N=16 同样 316~358 个位置无计划）⇒ 窗口续填（`sequence_feed` 在 3 组以上时的补对条件）是下一个要修的点，而不是容量或 READY。
- **下一步**：① 在 `sync_io_switch.c` 的 `sequence_feed()` 里对“剩余组数 ≥2 但 FIFO 只有 2 词空间”的情况加计数并放宽（或在窗口开启时一次性推入全部组，`T2_PLAN_GROUP_MAX=4` 时最多 4×2=8 词，两个 SM 各 8 词会超过 FIFO 深度，需要按 SM 分开推）；② 把 `T2_PLAN_GROUP_MAX` 提到 16 以覆盖“一位置 16 动作”的单窗口；③ 复跑 16 组，期望 `executed = 16 × 位置数`。












### T2-PROGRESS-20261018-049：完整位置 + 双组在真机通过（SWITCH_B 361×2=722，零失败）；3 组与 16 组各有明确拦路点

- **配置**：`T2_SEQ_GROUPS='1,3,2,2'`（表容量 8 行能装下的最大），组间隔 2 细片，粗周期 10 ms / 32 细分，采样 110 s 跑满整轮。22 步全 OK，`DISTRIBUTED`（`tx_publish 57207`）。
- **读数**：
  | 角色 | 组数 | executed | 失败原因 |
  |---|---|---|---|
  | SWITCH_B | **2** | **722 = 2 × 361** | 全 0 ⇒ **完整 361 位置 × 双组通过** |
  | GATEWAY | 2 | 0 | `BACKEND=361`（无网分时 READY 永不到达，`stage_sequence` 因 `s_ready_active` 拒绝） |
  | SWITCH_A | **3** | 6 | `NO_PLAN=358`、`LATE=3` ⇒ **3 组窗口仍有 off-by-one** |
- **结论**：
  1. **双组机制在真机上已被完整验证**（361 位置、722 次执行、零 LATE/EXPIRED/NO_PLAN/BACKEND），与单组的 361 次严格成 2:1；
  2. **GATEWAY 的 2 组失败是设计使然**（台架没接网分）：需要注入 READY 或接真网分，届时读 `ready_collision_count`（当前 `TRIGger:GATEway?` 页在错误态下会 timeout，需像 ACTION 页那样加一个窄页）；
  3. **3 组是新的边界 bug**：2 组过、3 组不过 ⇒ 怀疑窗口 `[seq, seq+N−1]`、`s_group_handed` 与 `s_group_next` 的对应在 N=3 时错位一格，下轮按 N=3 的逐组回执定位。
  4. **16 组被表容量挡住**：`TRIGGER_ACTION_TEMPLATE_CAPACITY = 8u`（所有角色共用），16 动作 = 16×3 = 48 行 ⇒ 需要把容量提到 ≥48 并审计所有以它为尺寸的结构；`T2_SEQ_GROUPS='1,16,16,16'` 实测在第 10 步被拒（`stores not current`），不是时序问题。
- **下一步顺序**：① 修 N=3 的窗口边界（有 N=2 作对照，定位快）；② 数据容量 8→48 并复跑 16 组；③ 给 `TRIGger:GATEway?` 加窄页读踩踏计数；④ 接网分实测 READY。












### T2-PROGRESS-20261018-048：双组序列在真机上**跑通**——174 个位置 × 2 组 = 348 次执行，零失败

- **根因（两个游标只复位了一个）**：Core0 从 `action_group_next[]` 决定要解哪一组，Core1 用**私有的** `s_group_next[]` 决定要不要接这个计划（`trigger_t2_runtime.c` 选择分支：`plan->group != s_group_next[index]` 就跳过）。位置收尾时我把 Core0 那个复位了、Core1 那个没复位 ⇒ 计划已经到齐（`NO_PLAN` 立刻归 0），但 Core1 始终只认“上一位置最后一组”，于是 `executed` 停在 6。
- **修复**（两处一起复位）：位置收尾时 `action_group_next = 0`、`s_group_next = 0`、`s_group_armed = NONE`、`action_group_in_flight = NONE`。
- **台架读数（`T2_SEQ_GROUPS='1,2,1,1'`，22 步全 OK）**：
  ```
  SWITCH_A 2组 : executed=348  groups=2  reasons(全 0)  freeze_missing=9     ← 174 位置 × 2 组
  GATEWAY  1组 : executed=174  groups=1  reasons(全 0)  freeze_missing=9
  SWITCH_B 1组 : executed=174  groups=1  reasons(全 0)  freeze_missing=11
  ```
  ⇒ **双组特权路径（一次交整表 + 按组回执 + 由表决定组间隔）在真机上完成 348 次执行，零 LATE / 零 EXPIRED / 零 NO_PLAN / 零 BACKEND**，且与单组的执行次数严格成 2:1。
- **前后的判据链（全部有读数支撑）**：`NO_PLAN=186` → 复位 Core0 游标后 `NO_PLAN=0` 但 `executed=6` → 复位 Core1 游标后 `executed=348`。
- **仍未做**：③ 的 READY 实测（需要接网分或用注入器：`ready_collision_count` / `ready_margin_min_ticks` 已在 `TRIGger:GATEway?` 页尾可读）；16 组（8 通道 × 2 极化）整链复跑；转台连续运行下的长时间稳定性。












### T2-PROGRESS-20261018-047：`--subdivisions 16` 实测也无效——`expand_fail` 是常量，说明它不是变量

- **运行**：`T2_SEQ_GROUPS='1,2,1,1'`，`--subdivisions 16`（默认是 2，正是上一轮的怀疑对象），22 步全 OK。
- **读数**：
  ```
  SWITCH_A 2组 : executed=6   NO_PLAN=184  freeze_missing=185  expand_fail=544 (与 subdivisions=2 时一模一样)
  GATEWAY  1组 : executed=187 NO_PLAN=0
  SWITCH_B 1组 : executed=187 NO_PLAN=0
  ```
- **结论**：`expand_fail=544` 在 `subdivisions∈{2,16}`、`period∈{2,12}` 四种组合下**恒定不变** ⇒ 它是与配置无关的一次性常量（很可能发生在安装/预跑阶段），**不是**决定双组失败的那个量。上一轮把它当成原因，判断错误，现在改正。
- **真正与失败同步变化的量**：`freeze_missing ≈ NO_PLAN`（185≈184）且 `executed=6`、`published=6` —— 也就是**双组位置的计划在 FREEZE 时版本为 0**，而 Core0 的发布计数只有 6。
- **下一个（正确的）仪器化方向**：我此前的计数器只装在 `prepare_deadlines_core0()`（源板路径）里；从板走的是 `!snapshot.origin` 的**另一条发布路径**（单组从板 `published=0` 却能执行 187 就是证据）。所以下轮要在**从板那条路径**上加同样的计数，才能看到双组从板为什么只发布 6 次。












### T2-PROGRESS-20261018-046：双行模板本身是**合法**的——`expand_fail` 只可能来自 instance 边界

- **逐字段解码**（`TRIGger:RESERVation:TEMPLate?`；行块 7 字段 = `template_id, generation, role, kind, argument, span_slices, delta_q`）：
  ```
  FB276192 (双组) header: installed=1 binding=1 resources=1 generation=1792276295 count=2
      row0: template_id=1 generation=1792276295 role=1 kind=1 argument=0 span=1 delta=0
      row1: template_id=2 generation=1792276295 role=1 kind=1 argument=0 span=1 delta=2
  2BD5090F (单组) header: count=1
      row0: template_id=1 generation=1792276295 role=2 kind=2 argument=20 span=1 delta=0
  ```
  ⇒ 两行的 `generation`、`span_slices`、`kind` **都合法**，`trigger_action_template_valid()` 必然通过。
- **因此 `expand_fail` 只能来自 `trigger_position_instance_expand(descriptor, subdivisions, delta_q, ...)`**：第 2 行 `delta_q = 2`（或默认 12）落不进该位置描述符允许的细片范围。这解释了为什么把 `T2_SEQ_PERIOD_SLICES` 从 12 改成 2 也照样失败——**只要 delta≠0 就出界**，说明网格细分 `subdivisions` 对“每位置多动作”的预算不足。
- **下轮只剩一个动作**：把 `subdivisions`（动作表安装参数）按 `N项 × 组周期` 设置（或按 `delta_q max + span` 推导下界），再复跑 2 组链路；预期 `expand_fail → 0`、`executed → 169`、`NO_PLAN → 0`。若仍失败，再读 `trigger_position_instance_expand()` 的边界条件。
- **目标盘点（截至本轮）**：
  - ① 序列语义（16 动作、无等待窗、READY=下一动作）✅ 文档+图+配置模块+台架派生值一致；
  - ② 全部 SCPI 可配（A..G / 通道·极化·网格 / 码序 / 重复 / 派生周期）✅ 台架四板读数确认；
  - ③ GATEWAY 收 READY + 踩踏计数 ✅ 代码就绪、页面可读；**实测待接网分**；
  - ④ 一次交整表 + 按组回执：**执行器侧真机跑通**（6 个位置走完两组、零拒绝），**限制在 `subdivisions` 预算**；
  - ⑤ T2 位置传输不回归 ✅ 全程 `DISTRIBUTED` / `SEAT refused 0`。












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











## 归档索引

| 文件 | ID 区间 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_01.md` | `T2-PROGRESS-20260926-016..T2-PROGRESS-20260922-001` | 35 | 2026-10-15 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_02.md` | `T2-PROGRESS-20260927-017..T2-PROGRESS-20260927-017` | 1 | 2026-10-18 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_03.md` | `T2-PROGRESS-20260927-020..T2-PROGRESS-20260927-018` | 3 | 2026-10-18 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_04.md` | `T2-PROGRESS-20261010-023..T2-PROGRESS-20261009-021` | 3 | 2026-10-18 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_05.md` | `T2-PROGRESS-20261012-001..T2-PROGRESS-20261010-024` | 5 | 2026-10-18 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_06.md` | `T2-PROGRESS-20261013-001..T2-PROGRESS-20261013-001` | 1 | 2026-10-19 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_07.md` | `T2-PROGRESS-20261014-003..T2-PROGRESS-20261013-002` | 4 | 2026-10-19 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_08.md` | `T2-PROGRESS-20261015-003..T2-PROGRESS-20261014-004` | 6 | 2026-10-19 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_09.md` | `T2-PROGRESS-20261016-007..T2-PROGRESS-20261015-004` | 10 | 2026-10-19 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_10.md` | `T2-PROGRESS-20261016-016..T2-PROGRESS-20261016-008` | 9 | 2026-10-19 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_11.md` | `T2-PROGRESS-20261018-034..T2-PROGRESS-20261017-020` | 13 | 2026-10-20 |
