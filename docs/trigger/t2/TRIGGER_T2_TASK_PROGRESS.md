# T2 预约触发任务进度

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-10-18

## 文档接口

本文记录真实执行、来源和证据，不冻结契约或代替 [TODO](TRIGGER_T2_TODO.md) 的任务状态；设计见 [Architecture](TRIGGER_T2_ARCHITECTURE.md)。条目使用 `T2-PROGRESS-YYYYMMDD-NNN`，最新在上；按 C14/C15 保留失败、历史和归档索引。

## 当前 checkpoint

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




### T2-PROGRESS-20261018-034：两动作窗口在台架上出现 failed，且 ACTION 页步长仍需按实测对齐

- **工具已收口一半**（`c100cf77`）：`T2:ACTion?` 的逐位置尾部**不再猜步长**——只有声明 `T2_ACTION_FIELDS` 才解析，否则打印字段数并标注“未解析”。prefix（schema/armed/completed/failed/uncertain）位置固定，始终可信。
- **实测页形状**（轮询两块从板，同一时刻各 26 字段，头部完全一致）：
  ```
  0:1  1:0  2:1  3:1  4:0            ← 1, armed=0, completed=1, failed=1, uncertain=0
  5:1792264440  6:1792264440  7:1832142496  8:1792264440  9:3814955859
  10:18097540910508788961            ← 单个 UInt64（约 1.8e19），说明 UInt64 只占 1 个字段
  11:1  12:1  13:79  14:2993488091  15:1 …（下一块）
  ```
  ⇒ 逐位置块是 **5 个字段**，但第 1 个字段不是 sequence 而是会话/纪元量级 ⇒ 该页**不是**我分析的那个 handler（`scpi_cmd_trigger_t2_action_q`）出的，得先定位真正的注册点再对齐（这是纯工具工作，不涉及固件）。
- **更重要的产品证据（两动作窗口在台架上确实有问题）**：同一条 `T2_SEQ_GROUPS='1,2,2,2'` 链路连跑三次，22 步均 OK、`DISTRIBUTED`，但 ACTION 页出现 **`SWITCH_A failed 4`、`SWITCH_B failed 1`、`GATEWAY armed 1 / failed 2 / uncertain 2`**（单动作窗口时这些计数为 0）。⇒ **双组窗口在真实链路上会失败**，而失败原因就写在同页后面的 `action_fail_reason[]` 区块里。
- **下轮顺序**：① 定位 `TRIGger:RESERVation:ACTion?` 的真实 handler、按实测步长对齐工具；② 读 `action_fail_reason[]` 与 `group_done/group_next` 判断双组失败是 LATE（第二组 tick 已过）还是 BACKEND；③ 按结论修（很可能是组周期 `T2_SEQ_PERIOD_SLICES` 太小，第二组落在第一组完成之后）。




### T2-PROGRESS-20261018-033：两动作窗口首次上台架（步骤全 OK / DISTRIBUTED），但 ACTION 页读数错位需先对齐

- **编排**（不改固件，只用工作流已有的序列编排参数）：`T2_SEQ_GROUPS='1,2,2,2'`（COUNTER/SWITCH_A/GATEWAY/SWITCH_B 每位置组数）⇒ 三个从板每位置 **2 个动作**，正好走新接的窗口交接（`group_count>1 → stage_sequence`）。
- **结果**（build `20261017184354`，`--steps 0-20`，22 步 **全 OK**）：
  - `POSITION distribution: source tx_publish 13824; follower lane_accept 12652/12481/12656 -> DISTRIBUTED`；
  - `WIN … slot 2/3 | act armed 2`（GATEWAY / SWITCH_B 每位置武装 2 组 ⇒ **窗口路径确实生效**）；
  - 从板 `owned 1`、`hold 1721/1809`、`drain 1696/1785`、`hold_delivered 25/24`。
- **读数问题（工具侧，不是固件）**：`ACTION slot 2 … [0] seq 0 deadline 0:0 backend 4294967295 width 218 / [1] seq 150405957821 …` —— 明显的错位（`seq 150405957821`、`width 218` 都是无意义值）⇒ 工具里的 `T2_ACTION_TAIL_FIELDS` 与板上 `T2:ACTion?` 的实际尾长不再一致，**每位置的组完成数还读不出来**。历史上同一个坑踩过一次（工具先改成 22、板上仍是 17，整页错位）。
- **下一步（先工具、后结论）**：从 handler 源码推 `T2:ACTion?` 的**逐位置尾长**（`middleware/scpi_port/src/scpi_trigger_t2_runtime_commands.inc`，含 5 组进度 + 5 组提前量等追加字段），把工具的 `T2_ACTION_TAIL_FIELDS` 校正，并让工具在“读到的尾长与镜像不符”时**直接告警而不是猜**；然后再跑同一条 2 组窗口链路，读 `completed / group_done / group_next` 与 `active` 计数。




### T2-PROGRESS-20261018-032：GATEWAY 两字协议试改失败后决定“不对称”——开关排队、网关逐发

- **试了什么**：把 GATEWAY 也改成两字协议（脉宽字 + 延时字，`OUTPUT_WORDS 11`，`start_deadline(width)`，首组 `−6`），并同步把 mock 改成 11 字程序 / `out y` / `mov isr,y` / wrap `hi=15`。固件能编过，但**脉宽断言仍不过**：`fall−rise ≠ 250`（把一次性 `advance(260)` 改成“等到出现下降沿”后依然不等）。这正是两次同类失败的同一处：网关 mock 的 SM 起始/停止相位与两字程序的 6 拍启动预算没有对齐，而开关 mock 是逐指令建模的。
- **决定（写进设计，不再来回试）**：**两条路径允许不对称**
  - **SWITCH**：需要“一条动作表在硬件里排队”，所以用**两字协议 + 窗口 + 排空续填**（已提交，37 + 544 用例通过）；
  - **GATEWAY**：每次都只发**一个**脉冲，下一组在上一脉冲完成时再武装（`stage_sequence` 已提交，50 passed），所以**保留预装 Y 的单字路径**即可。两字协议对网关的收益只是“省一次武装”，而代价是整条完成状态机与 mock 全部要重写——收益为负。
- **由此明确的时序约束（网关侧）**：因为武装发生在“上一脉冲完成”之后，`start_deadline` 用的是 `deadline − now − 4`；只要武装早于下一组的绝对 tick（E≈8 ms 的动作间隔下，武装只需微秒级），表里的间隔就被如实执行；若武装已经晚于该 tick，`start_deadline` 会**拒绝**（不会悄悄压缩间隔）。
- **本轮**：全部回退（`git checkout` 四个文件），`test_sync_io_gateway + test_sync_io_switch` **77 passed**、构建绿、工作区 clean。
- **下轮**：把 runtime 的窗口交接按角色分开——SWITCH 走 `stage_sequence`（已完成），GATEWAY 走 `sync_io_gateway_realtime_stage_sequence`（API 已就绪，仅需在 arm 处按角色调用 + harness 补桩），然后上单板台架。




### T2-PROGRESS-20261018-031：GATEWAY 侧序列入口已落地；链式用例卡在 mock 的“预装脉宽”路径

- **已提交**（`7894b106`，P3 `build=20261017180911`）：`sync_io_gateway_realtime_stage_sequence(base, pulse_ns[], delta_ticks[], count)` —— 一次接受整条动作表，首组立即武装，其余组在上一脉冲完成时按**各自的绝对 tick** 武装；非法表（count 为 0/越界、偏移非单调、脉宽为 0、加法溢出、已有在飞动作）整体拒绝，不留半武装。`test_sync_io_gateway` 50 passed。
- **本轮尝试的链式用例（三组 1000/2000/4000 ns）失败在 mock，而非固件**：第 0 组脉冲正确（rise 30001 → fall 30251，宽度 250 = 1000 ns ÷ 4）；服务里武装第 1 组后 `ctrl=10`、`pc=5`（停在 `pull`）却不再出脉冲 ⇒ mock 的 `prepare_realtime` 仍用 `pio_sm_exec(pull/mov y)` 预装脉宽/ISR，这套“软件预装”与真实的两字协议（脉宽随词入 FIFO）不同步，取字被预装消耗。已回退该用例、保留特性：`test_sync_io_gateway + test_sync_io_switch` **77 passed**，树 clean。
- **下轮做法（明确）**：像 SWITCH 侧那样把 mock 改成显式建模“两字/触发 + 每 SM 一个队列”，同时把 GATEWAY 的 PIO/软件真正切到两字协议（`OUTPUT_WORDS 11`、`start_deadline(width, prev_width)`，常数 `首组 −6`、`链式 间隔−前宽−8` 已推导好）。这两步必须一起做，否则 mock 永远对不上。




### T2-PROGRESS-20261018-030：序列配置面首次上台架——四板读数一致，派生周期与图完全对上

- **台架（build 20261017174216，四板一致）**：
  - TRIGger:SEQuence:TIMing? → A=1, B=1, C=1, D=1, E=8000, G=200000（µs）
  - TRIGger:SEQuence:COUNts? → N通道=8, N极化=2, 网格=500 ns
  - TRIGger:SEQuence:PERiod? → 16 动作, Δ动作=8003 µs, Δ位置=328048 µs —— 正好等于 16×8003+200000，与图里的推算一致
  - TRIGger:SEQuence:REPeat? → 重复 1, 每位置总动作 16
  - TRIGger:SEQuence:CODE? 3 → 通道 2, 极化 1（派生规则：3/2+1、3%2）
  （每页前两个字段是本树 SCPI 回包的状态前缀，值从第三个字段起。）
- **同轮 T2 不回归**：--steps 0-20 全步 OK；	x_publish 26774、DISTRIBUTED（从板 24040/23665/24009）、SEAT refused 0。
- **仍未做**：④ runtime 一次交出整条动作表（计划表驱动重构）；GATEWAY 侧 stage_sequence；单板实测 Δ动作/TRIG→READY 对齐与踩踏计数。






### T2-PROGRESS-20261018-029：一次交出 N 组的第二个卡点——`complete_action` 用全局模板数判断“是否最后一组”

- **本轮实测（窗口版代码，构建通过、用例失败）**：一次交出两组后，第 0 组完成时 `service()` 已能走“窗口内 sequence 前进”分支（不再误判 BACKEND），但该位置**立刻被判为完成**：`mask=1 done=1 exec=1`。原因是 `complete_action()` 用 `group + 1u < s_local_template_count` 决定“还有没有下一组”，当该位置在当前配置里只有 1 个**角色组**（模板数 ≠ 计划表组数）时，它会在收到第 0 组回执时直接收尾。
- **结论**：多组交接的“还有没有下一组”必须由**计划表自己**回答（`plan->group + plan->group_count` 与 `plan->group`），不能再用全局 `s_local_template_count`；这也解释了此前“组 0 完成就被判完成 / 或停在 PENDING”两种相反症状。
- **本轮已回退**该尝试（`trigger_t2_runtime.c` 与用例），`test_trigger_t2_runtime` **37 passed**、构建绿、工作区 clean；网关侧 READY 踩踏计数已单独提交（`67be60fd`，P3 `build=20261017164327`）。
- **下轮一次到位**：`complete_action` 改读计划的组表（`s_group_handed` 一并对齐），harness 里计划表与模板数保持一致；再补 `[sequence]` 断言“一次 arm、按组回执、最后一组才收尾”。




### T2-PROGRESS-20261018-028：定位“交出一整条序列后 service 非 OK”的根因（runtime 的身份检查），并补齐计划构建的拒绝路径用例

- **根因（代码定位，`trigger_t2_runtime.c:1137`）**：`service_deadlines()` 用**单一** sequence 做身份检查——`sequence != s_state.action_sequence[index]` 或 `deadline != s_deadline[index].deadline_tick` 即判 `BACKEND` 失败。一次交出 N 组后，第 0 组完成时后端把 `status.sequence` 推进到 **base+1**（下一组已 armed），而 runtime 只记了 base ⇒ 判身份不符 ⇒ `action_fail(BACKEND)` ⇒ `service()` 非 OK。这是三次尝试都停在“组 0 完成那次 service”的真实原因（此前只是推测）。
- **正确修法（下轮实施）**：把“已交出窗口”记成区间 `[base, base+N−1]`；`service_deadlines` 接受窗口内任一 sequence，并把“sequence 前进一格”解释为“上一组完成、下一组已在硬件”，据此推进 `s_group_next/armed` 而**不再 arm**；只有落在窗口外才走现有 BACKEND 失败路径。
- **本轮落地（可验证的小步）**：执行器的计划构建补**拒绝路径**用例（此前只测成功路径）：非单调偏移、间隔小于指令预算（0,5）、组 0 偏移不为 0、基点已过——四种都必须返回 false 且**不留半装载的管道**（断言 `ctrl` 未使能、TX FIFO 空、无引脚写）。`test_sync_io_switch + test_sync_io_sequence_plan` **28 passed**，构建绿。




### T2-PROGRESS-20261017-026：序列执行接线（`stage_sequence` + 排空续填）与三位置序列流图

- **接线（SWITCH 侧，按你的口径：以时间片为基点、在共同时间轴编序列）**：`sync_io_switch_realtime_stage_sequence(base_deadline_tick, codes, delta_ticks, count)`：
  1. 用 `sync_io_sequence_plan_build()` 以 `now`（一次 TIMER1 采样）为起点、`base_deadline_tick` 为位置时间片，把共同轴上的组偏移编成两字流（首个 `base−5`、链式 `间隔−7`）；
  2. 第一组按**与单触发完全相同**的方式起边沿：同一采样窗口做 origin 检查（≤ `SYNC_IO_SWITCH_ORIGIN_MAX_NS`），失败则清 FIFO/关 SM 并置 FAULT；
  3. 其余词对由 `sequence_feed()` 在两个 SM 的 TX FIFO **都**有 ≥2 字空间时成对补入（避免把某一组的 code/delay 拆散），`poll()` 在队头完成后调用它；
  4. 组表耗尽且回执齐 ⇒ 关 SM、`state=COMPLETE`；STOP 取消全部并清序列表。
- **不变量**：续填只决定"还有没有下一组"，**不决定下一组何时打**——那个由表里的 delay 决定；这正是 `SYNC-SCHED-001` 要的"tick 驱动而非完成驱动"。
- **图示**：新增 [`T2_SEQUENCE_FLOW.svg`](T2_SEQUENCE_FLOW.svg)，按三个位置（P1/P2/P3）编排：① 共同时间轴上的时间片与组偏移（`Δq=0/+2/+5`，示例值）→ ② 本地 tick 投影（与 deadline 同一换算）→ ③ 两字触发流与 `−5 / −7` 预算 → ④ PIO 执行、FIFO 排空续填与回执；图上标明 `rx_local_tick`（受理戳）与 `base_tick` 同域。架构文档 `T2-SEQ-TRANSPORT` 已引用此图。
- **验证**：`test_sync_io_switch + test_sync_io_sequence_plan` **27 passed**；固件构建绿（`sync_io_sequence_plan.c` 已入 `CMakeLists`，`sources=1822`）；P3 全链 PASS_WITH_WARNINGS（见凭证）。
- **未闭合**：`stage_sequence` 的**专用宿主用例**（三组边沿+回执）尚未补；GATEWAY 侧同构接线；`trigger_t2_runtime` 把该位置整条组序列一次交出的调用点；单板台架的"组间间隔误差"实测。




### T2-PROGRESS-20261017-025：借鉴 node-sequence 分支定下组序列通道（DMA 喂 FIFO），并修正 SWITCH 链式预算常数

- **借鉴结论（先读代码，再决定）**：`origin/feature/node-sequence-reservation-trigger` 的序列能力是**边沿驱动**（`docs/trigger/TRIGGER_SEQ_STEP_DESIGN.md`：每个 `TRIG_IN` 上升沿按编码表步进一位；`sync_io_sequence.pio` 是 ingress + 状态机），与 T2 的**时间调度**不同源，不能直接搬。可借的是**执行面形态**：
  1. **表驱动 + DMA 喂 PIO 的 TX FIFO**：`sync_io_sequence.c` 用 `channel_config_set_dreq(pio_get_dreq(SM,true))` + `read_increment` over `s_plan` + `chain_to` 重载（`DMA_SLOTS 5`）；
  2. **plan 一致性**：`plan` 带 `id/generation/count/crc`（`trigger_sequence_config.h`）；
  3. **有限配额**：`max_steps` + PIO 内 `jmp y--` 到永久 park（`sequence_finite_ingress`）；
  4. **结果码枚举**：`trigger_sequence_service_result_t` 12 项。
- **由此修正设计（写进架构）**：组序列不再靠软件逐组 `pio_sm_put`（"两字/触发"后 FIFO 只容 **2 组**，第 3 组又回到软件时序），改为**DMA 按 TX DREQ 喂同一份组表**（元素 `[code/width, delay_ticks]`，delay 为与上一组边沿的间隔）。组数上限改由 RAM 决定，软件只在换表时介入。见 `TRIGGER_T2_ARCHITECTURE.md` §「T2-SEQ-TRANSPORT」。
- **SWITCH 链式常数修正（本轮）**：按 `sync_switch.pio` 逐条数指令推得 `next_edge = prev_edge + 6 + 1 + X`，故队列触发的 delay 应为 `delta − 7`（此前写 8，提前 1 拍）。已改 `components/sync_io/src/sync_io_switch.c` 并注明推导，测试 28 passed、构建绿。
- **GATEWAY 深度 2 仍卡在同一处**：新程序下"脉宽精确 250 周期"断言不过（`fall−rise ≠ 250`；结合 `pc=13` 现象，怀疑 mock 里 `out y` 与 `pull` 的取字顺序/相位仍与固件不一致）。已二度回退，`test_sync_io_gateway + test_sync_io_switch` **76 passed**，树内只保留 SWITCH 的 `−7` 修正。下次应按 `T2-SEQ-TRANSPORT` 直接上 DMA 组表，而不是继续在 2 深队列上磨 gateway。




### T2-PROGRESS-20261017-024：接收时间戳落地（语义/落点/台架证据），P3 再次通过

- **语义（写进头文件，不靠口头约定）**：`trigger_t2_runtime.h` 新增 `rx_local_tick`＝本板**受理**位置记录那一刻的本地 TIMER1 tick，由本地 `action_cursor` 经**与 deadline 相同的 DCO/TIMER1 换算**投影得到（不读额外时钟、不伪造）；`rx_local_sample_seq`＝该时刻的本地 coarse 网格号；`rx_timestamp_count`＝已打戳受理次数；`rx_stale_count`＝**受理时该记录 deadline 已过去**的次数（"别把旧接收时间戳套进新模型"的可观测形式）。注释明确区分：这**不是**传输车道的到达时刻（那个在 `distributed_refmem`）。
- **落点**：一条记录一个戳（一条记录带两个位置、同一时刻受理），放引擎状态而非 361 位置数组；打戳点在 `prepare_deadlines_core0()` 的**新描述符被采纳并发布计划**处（`group==0`），不在每轮重复解算处。页追加（只增不改）：`TRIGger:RESERVation:T2?` 尾 5 字段 = `rx_local_tick_lo/hi, rx_local_sample_seq, rx_timestamp_count, rx_stale_count`（页面 72 → 77 字段）。
- **台架证据（build `20261017142410`，`--steps 0-20`，22 步全 OK）**：三块从板 `stamps 14,531 / 14,598 / 14,898`、`rx_local_tick` 非零、**`stale 0`**；源板（COUNTER）`stamps 0`——它对位置只发布不受理，符合语义。同轮非回归：`tx_publish 15,963`、`DISTRIBUTED`（从板 `13,697/13,530/13,758`）、`SEAT refused 0`。
- **注意（未台架验证项）**：同伴字段初版用的是 `carrier_sequence`，实测四板恒为 0（无 carrier 流），故改成语义明确的本地网格号 `sample_seq`；该**改名只过了宿主用例**，尚未再上板读数。
- **P3**：全链再跑一次 **PASS_WITH_WARNINGS**，凭证 `build_id=20261017144026`、`source_tree_sha256=8b78db5e…`、`generated_at_utc=2026-10-17T14:49:04Z`；宿主 `trigger_t2/t2_/seat/refmem` 选择 **321 passed**。
- **未闭合**：GATEWAY 深度 2（上一条的失败证据仍在）；组序列接口 `base_tick + 组序列表`；契约登记债务。




### T2-PROGRESS-20261017-023：GATEWAY 深度 2 首次尝试未通过宿主时序断言，已回退（保留证据供下次直接接续）

- **做了什么**：按 SWITCH 已验证的模板把 GATEWAY 也改成"两字/触发"（word A=脉宽写入 Y 并作回执、word B=延时）+ 深度 2 队列：`sync_gateway.pio` 8→11 条指令、`SYNC_IO_GATEWAY_OUTPUT_WORDS 11`、`start_deadline(deadline)` 内部按"首触发/排队触发"分流（排队时用 `deadline − previous − 12` 的**间隔**）、`s_queue[2]` 与逐条 `sequence/width`、STOP 取消全部、回执按 head 的 width 归属、`safe_stop()` 清队列。
- **卡在哪里（精确证据）**：mock 已同步（程序长度 11、wrap `hi=15`、`out y` 与 `mov isr,y` 解码、`pio_sm_put` 允许在运行时喂 FIFO、"第二次递交必须被拒"翻转为"第二次入队/第三次拒绝"），但脉冲**宽度断言**不过：`advance(260)` 时 `pc=13`（恰停在 `set pins,0` 前），放宽到 `advance(300)` 后 `fall_tick-rise_tick != 250` 仍失败。说明新程序相对旧程序在"上升沿→下降沿"上存在 **±1 周期**的相位差（新增 `pull/out y` 使 `out x` 晚 2 拍，我用 `−5` 只补了 1 拍），而用例要求"脉宽精确等于配置值"。这条必须先在 mock 里把**边沿→循环计数**的相位算清（或让用例断言 `|delta-250|<=1` 并另加一条"绝对脉宽由硬件保证"的证据），再继续。
- **当前状态**：**已 `git checkout` 回退** GATEWAY 四个文件（保持树干净、不提交半成品）；`test_sync_io_gateway + test_sync_io_switch` 重新 **76 passed**。SWITCH 侧的深度 2 不在回退范围内（已在提交 `d3ea7fb5` 内）。
- **下次接续清单**：① 在 mock 里显式建模"边沿指令周期"（`set pins` 的生效时刻），把 `−5` 与 `−12` 两个常数一次定准；② 用例区分"精确脉宽（硬件保证）"与"宿主模拟相位（可 ±1）"；③ GATEWAY 通过后再做接口层 `base_tick + 组序列`。




### T2-PROGRESS-20261017-022：P3 全流程通过（含 DPLL60）——修复的是"判据尾巴"，不是 VDC 发布；并修掉构建身份冻结

- **P3 结果（本轮新增凭证）**：`--profile full` 全链跑通，`PASS_WITH_WARNINGS`（INFO 33 / WARN 10 / ERROR 0 / FATAL 0），step 36 自检 OK，凭证写入 `config/hardware_acceptance/p3_acceptance_receipt.json`：`schema=HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_TDMA_4NODE_QUICK_GRADED_V1`、`passed=true`、`generated_at_utc=2026-10-17T14:00:46Z`、`build_id=20261017135128`、`source_tree_sha256=7433b7e9…`（1819 文件）。DPLL60 摘要 `passed=true / evidence_status=PASS`。
- **"VDC 发布坏了"是我的误判，已更正**：`tx_publish`/`tx_release` 在今天**每一次** T2 整跑（含 17:43，改动之前）都静止，`tx_ready_count` 恒 1 —— 那是 T2 启动前的遗留镜像，T2 走一等席不喂普通 FIFO，**不是 VDC 故障证据**。据此追的三轮"FIFO 停滞"结论作废。
- **DPLL60 反复失败的真实原因＝终止 bin 的停机尾巴**：离线解析 `capture/NO*/decoded.json`（`success_gap_basis=after_first_success`），11:39 那轮稳态各 bin 最大成功间隔 **37.6/47.2/45.3/50.1 ms**，而 **TERMINAL bin 为 1.70/1.16/0.54/0.04 s**；判据此前对**所有** bin 取 `max()`，于是把"目标完成后到窗口结束的静默"当成成功间隔，`1.70 s` 直接顶掉 1.0 s 门限。修复：判据只在**非 TERMINAL 稳态 bin** 取最大值，尾巴另存 `terminal_success_gap_s` 供复核；本轮通过跑实测稳态 13–41 ms、尾巴 0.01–0.03 s。
- **`T2` 拍抢占规则的反例（重要，勿再"优化"回来）**：曾把 T2 准入改为"仅空闲拍"（`&& !normal_due`），结果**自锁**——`last_publish_ms` 只在真正发帧时推进，普通通道一旦有帧未被取走就停发，`normal_due` 永远为真，而 T2 是唯一绕过 FIFO 的通路，于是整链 0 帧（两轮整跑实测 `tx_publish 0`、313–568 s、`NO DELIVERY`，同轮生产端仍 `coalesced 22,788`）。已回退并在代码注释中写明原因；同时把 `wait_reason` 归因改准：只有 `resident_master || feedback>0` 才记 `FAILURE_FEEDBACK`，否则记新的 `FAILURE_FIFO_READY`。
- **feedback 拒发仪表（保留，用于以后定位）**：`feedback_prepare()` 的各 `-1` 分支加 `last_reason` + 分桶计数（1 未就绪+在飞、2 未就绪+offer、3 快照忙、4 绑定变化），`SYSTem:REFMEM:SYNC:FLIGHT?` 页尾追加 9 字段（只增不改）。本轮四板读数**全 0** ⇒ feedback 从未拒发，这是"VDC 发布无辜"的直接证据。
- **构建身份冻结（真工具缺陷，已修）**：`PROJECT_BUILD_ID` 在 configure 时生成，而 `tools/cmake_build_auto/cmake_build_auto.py:cache_matches()` 只比较路径 ⇒ 复用构建目录一律跳过 configure ⇒ id 永远停在首配值（`dpll60-latest` 的包声称 `20261016011253`，却在编译当天源码）。修复：新增 `source_state_digest()`（源码路径+大小+mtime 的 SHA-256）落盘为 `.dsh_source_state`，`cache_matches()` 追加该比对。实测：源码变 ⇒ `configure=needed`；未变 ⇒ `configure=skip`；生成 id 变为 `20261017134124`。
- **FIFO 防御性修复（保留）**：`tdma_pio_spi_ring_origin.inc` 的"重复帧"分支与 5 条拒收路径在 `acquire_tx()` 后补 `release_tx()`，并新增 `origin.flight_publish_reject_count`；坏帧不得永久占用唯一活动槽。
- **未闭合**：`SYNC-SCHED-001` 深度 2 仍只完成 SWITCH 侧（GATEWAY 未改）；契约登记（`ARCH-TRIGGERSLICE-01` 仍写 10 B@14、`TDMA-PROCESSIMAGE-01` C2、`POSition?` 35 字段与 `T2?` 72 字段）；`T_drain` 默认值需按 Core1 相位预算重新定档并登记。




### T2-PROGRESS-20261017-020：位置 TX 改为"最新值表 + 定周期取数"——台架首次 `coalesced 671`、零拒绝、零迟到

- **背景与根因（代码事实，不是推断）**：单深度一等席时代，生产者每轮解算都"递交一次"，座位被占就 `stage_refused_count++` 并重试同一条 ⇒ 该计数实际是**轮次忙等计数**（实测 1.07–1.50 次/条），拒绝率随公告簇发在 6.6%–33.4% 之间漂移；`T_drain` 也无法改变发送量，因为 `trigger_t2_runtime.c` 里还有一句旧邮箱门 `if (distributed_refmem_position_descriptor_pending()) return;` 让**生产者被发射端反向锁死**（50 ms 那轮发送速率仍随取数上限走、`coalesced` 恒 0）。
- **实现**：`distributed_refmem` 的 TX 侧删除单槽邮箱，改为每位置一格最新值表（`DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS=3`）；生产端 `stage_position_descriptor()` 永不因背压失败（仅非法描述符失败），写已有未发值即覆盖并计 `tx_coalesced_count`，等待中的 FREEZE 不被 PREVIEW 覆盖；发射端 `position_tx_prepare(owner)` **只在 `tdma_service_first_class_free(owner)` 为真时取值**，因此递交不可能被拒；PREVIEW 受 `T_drain` 约束、FREEZE 不受约束；取值时把下一位目标写进同一 16 B 切片 `[10..15]`（一条记录两个位置）。删除上述生产者 pending 门。
- **安装参数（新增 SCPI，掉电回默认）**：`T2:DRAIN <ms>` / `DRAIN?`（默认 6，上限 1000，0=不限速）；`T2:ANNOUNCE <tol>,<min>,<global>` / `ANNOUNCE?`（默认 4/10/6，**0 = 该闸门不抑制**，越界拒绝且不改已安装值）。页追加：`SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 第 35 字段 `tx_coalesced_count`；`TRIGger:RESERVation:T2?` 末字段 `fallback_promoted_count`。
- **台架判据（build `20261017102517`，`--steps 0-20`，220 s 采样，22 步全 OK）**：`SEAT refused 0`、`tx_reject 0`、**`tx_coalesced_count 671`**、发送速率 **171/s**（贴住 6 ms 上限 ≈167/s）、四板 **361/361/361/361**、`late/expired/no_plan/conflict/backend` 全 0、从板 `decode_reject 0`、`DISTRIBUTED`；`freeze_fallback`（版本不匹配）27/30/16。
- **反例（同一固件、只改安装参数）**：`T_drain=50 ms` ⇒ SWITCH_A `abandoned 28 / late 28`、SWITCH_B `abandoned 29 / late 27 / backend 2` ⇒ **50 ms 超出从板流水线预算，6 ms 才是安全区**；该轮 `coalesced` 仍为 0，正是生产者 pending 门未删前的证据。
- **口径更正**：此前把 `freeze_fallback_count`（FREEZE 版本 ≠ 本地最新解算版本）当作"第二位置使用次数"是**错的**；第二位置的真实计数是 `fallback_promoted_count`，全 361 位置实测 **0 次**。方向（本地更新/更旧）尚未记录，待加两个方向计数后定性。
- **未闭合**：契约登记（`ARCH-TRIGGERSLICE-01` 仍写 10 B@14，实际已是 16 B@8；`TDMA-PROCESSIMAGE-01` C2 按 class 子区复用未闭环；`POSition?` 31→35 字段与 `T2?` 72 字段待登记）；P3 凭证与提交。宿主：`refmem/trigger_position/t2_/seat/vdc_feedback` 选择 **576 passed**，受影响 4 文件 24 passed。





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




### T2-PROGRESS-20261014-003：GATE-001 锁态实测——台架已由主机侧保证 LOCKED，固件侧仍无门禁

- TODO task ID：`T2-GATE-001`。
- **实测（主控直达探针，复用本仓连接层；四板，静止态）**：`SYSTem:SYNC:VDC:LOCK:READiness?`（24 字段）读数——NO.1 `0010071E65B5CB38`：`input_ready=1, locked=1, reason=0, dpll.state=5(LOCKED), health=2, accepted=529, rejected=0`；NO.2 / NO.3 / NO.4：`input_ready=0, locked=0, reason=2, dpll.state=1, health=1, accepted=0`。即**静止时只有原点 LOCKED**，三块从板未就绪。
- **成因（工具 + 代码直读）**：VDC 只在 sync-IO observer 启用后接受输入；`t2_workflow.py` 步骤 16 对每板发 `SYSTem:SYNC:VDC:OBServer:TDMA`，步骤 17 再以 `LOCK:READiness?` 第 1 字段逐板判定 `locked == 1`（5 轮 × 3 s，全板达成才 break），未达成则打印 “dynamic T2 start withheld” 并 `return 3` **中止本轮**；只有全板 locked 之后才置 `LOAD:MASK` 位 6 启动 T2 相位。⇒ 台架上「已 LOCKED」这一前置**由主机侧工作流强制**，且实测可达（否则 T2 轮不会通过）。
- **GATE-001 的缺口因此定位得更准**：K-3 的前置**主机侧已强制、固件侧无检查**——生成侧 `prepare_deadlines_core0()` 与装填侧 `service_deadlines()` 都不看锁态/发布（评审 A-02 成立）。任何不经该工作流的主机路径、以及运行中锁态退化，都没有固件侧拒绝。
- **对上一轮取舍问题的回答**：既然工作流已保证四板 `locked==1` 才启动 T2 输出，固件侧落 **fail-closed** 门禁**不会改变台架行为**，只会拒绝台架不会进入的状态。故「严格门禁会让台架失去 T2 输出」的顾虑**不成立**，无需为此引入额外开关。
- **发布面的可读入口（实测存在）**：`SYSTem:REFMEM:VDC:VECtor?`、`SYSTem:REFMEM:DPLL:VECtor?`；仓库已有同时采集 readiness 与两个向量的工具 `tools/vdc/dpll_vdc_monitor/dpll_vdc_monitor.py`。
- **实现前待定（未核验）**：T2 固件应消费的**访问器**尚未确定——`REFMEM_VECTOR_FLAG_LOCKED` 今天只有定义、置位、测试与文档引用，**无固件消费方**，需先确认从 `components/sync_trigger` 读取该发布的合法入口（或经 `vdc_dpll_manager_get_snapshot()` 读同等字段而**不自行判定锁态**），再动代码。
- **未核验**：本轮未改任何源码，未运行 pytest / 构建 / 四板 P3；以上读数取自静止态，运行中锁态随 `t2_workflow` 步骤 16/17 变化（其日志已按轮打印 `lock t+Ns … LOCKED …`，可作为运行中佐证）。




### T2-PROGRESS-20261014-002：更正——「已 LOCKED 并发布」的输入确实存在，T2 未消费

- TODO task ID：`T2-GATE-001`。
- **触发**：用户指正两点——「LOCK 是可以用指令查到」「应该有进行发布」。主控据实复核后确认指正成立；本条**覆盖**同日 `T2-PROGRESS-20261014-001` 中「VDC 侧不存在可被消费的『已 LOCKED 并发布』符号」的结论（001 条原文保留，不改写）。
- **证据（主控直读）**：
  1. `components/distributed_refmem/inc/refmem_vdc_vector.h`：`REFMEM_VECTOR_FLAG_LOCKED = (1u << 5u)`。
  2. `components/distributed_refmem/src/distributed_refmem.c` 的 `distributed_refmem_vector_flags()`：在一份 guarded 快照上按**严格合取**置位——非 provisional、`dpll.debug_continue_enabled == 0`、`dpll.state == VDC_DOMAIN_LOCK_LOCKED`、`quality.valid == 1`、`quality.lock_state == VDC_DOMAIN_LOCK_LOCKED`、`quality.health_state == VDC_DOMAIN_HEALTH_HEALTHY`、`lock_quality_tier >= VDC_DOMAIN_LOCK_QUALITY_FINE_100NS`、`last_sample_time_ns != 0`、`freshness_limit_us != 0`、`last_sample_age_us <= freshness_limit_us`、`gate.passed != 0`；注释自述「No new observation, publication or formal promotion is performed by the vector writer」。该位写入两向量轮转的 VDC/DPLL RefMem 向量。
  3. 主机可见面：`middleware/scpi_port/src/scpi_sync_commands.c` 的 `scpi_cmd_sync_vdc_lock_readiness_q`（`SYSTem:SYNC:VDC:LOCK:READiness?`）回 `input_ready`、`locked = has_snapshot && dpll.state == LOCKED && input_ready`、reason、`dpll.state`、健康与接受/拒绝计数、时间戳资格、字典 CRC 等。
  4. 该标志已被测试：`tests/python/test_vdc_publication_generation.py` 断言发布向量 payload 的该位与期望一致（另有 `tests/unit/test_refmem_vector_projection.c`、`tests/python/test_vdc_idle_maintenance.py`）。
  5. 文档已记该投影语义：`docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`（该位是同一快照中健康锁定状态的投影，**不代表**完整 formal promotion）。
- **GATE-001 的真实缺口据此改写**：不是「无输入」，而是 **T2 未消费已发布的锁定标志**——全仓检索 `REFMEM_VECTOR_FLAG_LOCKED` 只有定义、置位、测试与文档引用，**尚无固件消费方**；T2 侧实际使用的仍是 `vdc_dpll_manager_get_committed_model()` 加弱判据 `model->dco.lock_state > VDC_DOMAIN_LOCK_FAULT`（把 OFF…RELOCKING 全当有效，评审 A-02 判据成立）。
- **先前结论的错误来源（据实记录）**：001 条依赖子代理结论，而该结论有两处偏差——① 把符号名写成 `REFRESH_VECTOR_FLAG_LOCKED`（代码中无此拼写，正确为 `REFMEM_VECTOR_FLAG_LOCKED`）；② 由「T2 不消费该标志」推成「不存在可消费符号」。主控未先直读 `distributed_refmem.c` 的置位点即写入文档，属**核验不足**，已按本条更正。
- **未核验**：本轮仍未运行 pytest / 构建 / 四板 P3；该标志在从板上的实际可读路径（T2 从 RefMem 区域读取的具体入口，以及是否经 `distributed_refmem` 暴露查询）与「该投影是否等同 `VDC-RUN-001` 正式判据」均待定。




### T2-PROGRESS-20261014-001：三个卡点的跨域前置核实与归口（GATE-001 / EXEC-002 / TRANSPORT-002+IDENT-001）

- TODO task ID：`T2-GATE-001`、`T2-EXEC-002`、`T2-TRANSPORT-002`、`T2-IDENT-001`。
- **取证方式**：三个**独立只读**侦察子代理分项核实（GATE-001 / EXEC-002 / 一等席与身份），均为代码 + 文档只读交叉核验，未创建/修改/删除文件、未运行构建与硬件工具；结论由主控按「实测代码 / 登记表事实 / 文档声明」三类分别复核，并**驳回了一条与主控直读冲突的子代理断言**（见末条）。
- **GATE-001：缺陷成立，且 `BLOCKED` 是实质性的**。K-3（`TRIGGER_T2_ARCHITECTURE.md`）要求「VDC 进入 LOCKED 并发布之后 T2 才有输出」，而生成侧 `prepare_deadlines_core0()` 只守 local template、会话/token/`dco.valid`/period，装填侧 `service_deadlines()` 只守 `cursor_valid` 与身份/期限，**两侧都没有锁态或发布检查**；sync_trigger 内唯一相关判据是 `model->dco.lock_state > VDC_DOMAIN_LOCK_FAULT`（`trigger_time_cursor.c`、`trigger_time_projection.c`），该「枚举最大值」判据把 OFF…RELOCKING 全部当作有效。**主机实测反证**：`tests/python/test_trigger_t2_runtime.py` 的 fake 模型未置 `lock_state`（= OFF）却断言装填成功。VDC 侧**无**可被消费的「已 LOCKED 并发布」符号（`VDC-LOCK-001`/`VDC-RUN-001` 在 `docs/vdc/VDC_DOMAIN_TODO.md` 均 PENDING、登记表零命中、无实现；`published_update_seq`、`REFRESH_VECTOR_FLAG_LOCKED` 等按 VDC 域架构自述**不代表**完整 formal promotion）。⇒「消费 VDC 提供的发布状态」这一半**当前不可实现**；T2 侧可动部分为按 K-3 落 fail-closed 拒绝（非 LOCKED/未发布一律不生成不装填）+ 非发布锁态主机负测。
- **EXEC-002：依赖长期无受理方**。现状确为单槽——`sync_io_switch.c` 以 `SYNC_IO_SWITCH_PENDING` 显式拒绝，`sync_io_gateway.c` 经 `prepare_realtime()` 的 pending 标志间接拒绝（失败路径另清 OUTPUT/READY FIFO），两侧各只有一份 `s_deadline`，头文件自述 “One transaction at a time, Core1 only.”。SYNC_IO 域此前**无任何对应任务项**（该域无 REVIEW 文件，找不到任何受理记录），已在该域登记 `SYNC-SCHED-001`（含退出门禁）。
- **TRANSPORT-002 / IDENT-001：登记落点缺失，且原表述含一处误报**。`EXE-SEAT-01` **有定义**（`docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 的「特等席增设须用户审核」节），但该文档 `Status: Draft`、**不可作 clause_loc**、自述不新增 wire 契约，且在登记表中**无行** ⇒ 原「按 `EXE-SEAT-01` 登记（C11）」仍无落点。真实落点为 TDMA 域 2026-10-14 建立的 `TDMA-SEAT-01`（一等席容器与预算条款登记）。布局事实（10 B 子区 = 24 B body 的相对偏移 6，绝对偏移 14..23；`_Static_assert(TRIGGER_POSITION_SLICE_WIRE_SIZE == TDMA_PROCESS_IMAGE_REFMEM_SIZE, …)`）经代码复核成立。
- **评审引用行号已漂移（据实记录，未改评审原件）**：主控直读确认 A-02 所引 `trigger_t2_runtime.c:700-702` 现为 SyncIO 回执读取、`trigger_time_cursor.c:201` 现为溢出保护，对应的检查现位于别处；子代理另报 A-16 所引 `sync_io_switch.c:332-338` 处实为服务函数（拒绝条件在同文件更后处）、`trigger_t2_runtime.c:395-416` 现为 mailbox 读取。
- **被驳回的子代理断言**：EXEC-002 侦察称 `prepare_deadlines_core0()` 「在现文件中已不存在（更名为 `service_deadlines()`）」。主控直读**证伪**——两者同时存在且职责不同：`service_deadlines()` 是装填路径（Core1 侧准入与提交），`prepare_deadlines_core0()` 是 Core0 侧的 deadline 解算/发布路径。故该断言未采信；登记的 `SYNC-SCHED-001` 文本不含此说法。
- **未核验**：本轮为静态只读核验，**未运行 pytest / 构建 / 四板 P3**；上述「可测 / 可接」仅为读码判断，不代表已通过。




### T2-PROGRESS-20261013-002：TDMA 域一等席布局联审完成，`T2-AUDIT-03` 结案（有条件接受）

- TODO task ID：`T2-AUDIT-03`；契约 `ARCH-TRIGGERSLICE-01`（status 保持 `pending`）。
- **联审**：核验方为**独立 TDMA 域侧 subagent**（未参与 T2 或 TDMA 任一侧起草），方式为代码 + 登记表 + 文档只读交叉核验；结论 **有条件接受（`PASS_WITH_NOTE`）**；提交单 `docs/check/submissions/TRIGGER_CROSS_REVIEW_03.md`。
- **被确认成立**：10 B 子区位于 slot 14..23（24 B core0 body 内），与 VDC 8..13／ACK 24..26／CONTROL 27..28／DIAG 29／CRC 30..31 **无重叠无空洞**；`_Static_assert(TRIGGER_POSITION_SLICE_WIRE_SIZE == TDMA_PROCESS_IMAGE_REFMEM_SIZE, ...)` **逐字存在**（`distributed_refmem.c:33-34`）；发布机会轮换、「特等 `EMPTY` 让位」「两级皆空不消耗机会」「一等记录等待中不被覆盖」均为**代码机制**而非意向；`SYNC_TRIGGER` 相位预算获**独立重算确认**（相位 7、WCET 26800、窗口 335000..365500、REFMEM 298500..332000、SCPI 8 头部 + 每相位 11 字段）。
- **T2 侧据实修正登记行 4 处**：① `REPMEM-LAYOUT-01` → **`REFMEM-LAYOUT-01`**（原为**悬空引用**，真实 id 见 registry:22）；② 「前 8 B 为 **core1** 元数据」→ mailbox **fast header，由 Core0 镜像构建写入**（`distributed_refmem.c:984-990`），Core1 仅上车前复验；③ 「offset 14」→ **slot 绝对偏移 14**（24 B body 内相对偏移 6）；④ 版本耦合由「尺寸随布局版本耦合」收紧为**符号级**，并写明 `TDMA_PROCESS_IMAGE_LAYOUT_VERSION` 自引入未递增、混版本保护实际靠 **class 白名单拒帧**（普通 transport 白名单不含 0x15）。
- **跨域未闭合条件（仅指针，不计为 T2 待办项）**：TDMA 侧 **C1**（`TDMA-SEAT-01` 一等席容器与预算条款缺失）、**C2**（`TDMA-PROCESSIMAGE-01` 待补 RefMem 子区按 class 复用与版本递增规则）、**C3**（`SHARE_MAX == WAIT_MAX == 8`：合规 8:1 按设计置位 `SEAT:HEALth?` bit3）、**C4**（TDMA 不声明时间期限）；arch 侧 **C5**（`ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md:57` 引用**已删除**符号 `TDMA_SEAT_ROTATION_*_SHARE` 且称「编译期常量」，违反硬约束 2；另有多处已作废模型描述）。
- **零命中（治理缺口）**：`TDMA-SEAT-01/02/03` 在 `docs/check/DOCS_REGISTRY.md` 中**无任何登记行**；`EXE-SEAT-01` **全库无定义、无登记、无结论**，却被 T2 与 TDMA 共 5 处当作前置引用 ⇒ `T2-IDENT-001` 的「按 `EXE-SEAT-01` 登记」**目前无落点**。**（更正 2026-10-14：`EXE-SEAT-01` 并非「全库无定义」——它在 `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 的「特等席增设须用户审核」节有定义；成立的是「无登记行、无用户审核结论」，且其宿主文档为 Draft、不可作 clause_loc。落点已改指 TDMA 域 `TDMA-SEAT-01`。见 `T2-PROGRESS-20261014-001`。）**
- **未核验**：本次为**静态只读核验**，不含编译、pytest 与四板 P3；所有实测数字（2.13 ms 快速路径间隔、四板送达/冻结等）均为**文档快照**，仅核其与编译期常量自洽。




### T2-PROGRESS-20261013-001：位置邮箱契约登记完成并经 C11 独立交叉审核（`T2-AUDIT-07`）

- TODO task ID：`T2-AUDIT-07`；契约 `ARCH-TRIGGERMAILBOX-01`（status `pending`）。
- **登记**：`docs/check/DOCS_REGISTRY.md` 新增该行；clause 载体 `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`「位置邮箱交接契约」小节；code_anchor `components/sync_trigger/inc/trigger_position_mailbox.h`。
- **C11 独立交叉审核**：核验方为**独立 subagent**（与起草方不同会话、未参与起草），方式为代码与文档**只读**交叉核验（未创建/修改/删除文件，未运行构建或硬件工具）；12 项断言逐条核验，结论 **`PASS_WITH_NOTE`**；提交单 `docs/check/submissions/TRIGGER_CROSS_REVIEW_02.md`。
- **按结论修正 4 项**（均属措辞/归属，无实现缺失、无编造）：① `[1, LATCH_IDS]` 不再写作通用「有效 `position_id` 域」，改为 FREEZE latch **槽位域**并划分所有权（线上域归 `ARCH-TRIGGERSLICE-01`，本条不重复立法）；② 小节标题「冻结」→「登记，status `pending`」，并消除同文档原「本文当前仍为 Draft」的自相矛盾；③ 「载荷仅在 `WRITING` 写／`READING` 读」限定为**运行时（并发交接期）**，补记静默边界清空与 `WRITING`/`READING` 期拒绝复位；④ 显式声明 FREEZE latch 为同头文件内**独立于 mailbox** 的结构体（载荷规则「bit 清写／bit 置读」）。
- **新产出：位置域三方口径不一致（另立 `T2-IDENT-002`）**：线上／latch 寻址域 `[1, 15]`（`trigger_position_slice.h:38` 的 `0x0Fu` 与 `trigger_position_mailbox.h:74` 的 `TRIGGER_POSITION_FREEZE_LATCH_IDS`，二者**无静态断言绑定**）；`trigger_position_descriptor_valid()` 的 FREEZE 分支硬编码 `position_id <= 3u`（`trigger_position_descriptor.h:193`，**未引用宏**）；T2 运行时消费域 `[1, TRIGGER_T2_RUNTIME_POSITION_COUNT]`（`= 3u`，`trigger_t2_runtime.h:12`；拒绝见 `trigger_t2_runtime.c:403-404`）。数值当前自洽，但改宏不会带动该字面量。
- **本次核验未覆盖**：同源码构建、四板 P3、pytest 运行；内存序仅作形式配对核验，未做多核压力验证。
- **附带更正**：`docs/README.md` 中 `TRIGGER_T2_HANDOFF_CHECKLIST.md` 的索引描述仍写「用户偏好」，而该小节已于 `488a0559` 移除（该提交只改清单、未同步索引），随本次一并更正。

### 2026-10-13 自 T2 待办下沉的长记录（避免 C12 跨文件复制）

### 体量合规（2026-10-13

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### 执行交接（ROI 状态

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### ② 的台架语义验收用例

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### 判据抽取重构：P3 验收未通过

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### T2-AUDIT-04 决策输入
**台架验收基线（用户裁定）**：无转台时以 **OUT4→IN1 硬件回环模拟转台输入**；接真实转台时由转台直接驱动 **IN1**。参见 Task Progress 的两条取证。


本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### T2-AUDIT-03 前置：契约冻结清单

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### C14 R3 命名规则

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。


### 2026-10-13 用户指定：后续单串口控制拓扑（其余板卡走 RefMem 分布式控制）

用户裁定（逐字要点）：**后续只有一个串口连接到 PC 上，其他板卡都用 REFMEM 进行分布式控制**。

对设计的直接含义（供后续落位使用）：
1. **RefMem 是控制面镜像，不是运输**：跨板传输仍由 TDMA 帧（SHORT process image）8 × 32 B）承担；
   写入本地 RefMem 表後由 TDMA 携带增量，对板 RefMem 表随之更新（见“八槽 compact mailbox 消费规则”与 delta 规则）。
2. **每帧预算有限**：控制命令必须适配每帧预算（每 slot 前 8 B 元数据 + 后等待 core0 内容；T2 切片为 10 B 子区），
   否则需分块；因此 **TLV 区（3072 B）适合保留态/控制面，不适合批量流式数据**。
3. **需要命令转发契约**：单串口下，其余板卡的 SCPI/配置必须由“接 PC 的那块板”转发；
   该转发的**寻址/路由/应答与权限**属跨域契约，需登记并走 C11。
4. **与现有机制的关系（待核）**：本仓已有 `refmem_node_load_sync`（节点装载同步）与 TDMA 一等席机制；
   其能否直接承担该转发职责，尚需逐条比对后才能声明。


### 2026-10-13 台架接线已确认：OUT4 ↔ IN1（模拟转台）

用户确认：**OUT4 与 IN1 已接在一起**（先前一条提到的 "OUT5" 已更正为 OUT4）。

核验（本仓现状）：
- `boards/rp2350_trig/inc/board_config.h:314` ：`BOARD_SYNC_OUTPUT_PIN_COUNT = 4u`，即本地同步输出为 **OUT1..OUT4**；
  `:316` `BOARD_SYNC_PULSE_OUT_PIN = BOARD_SYNC_OUTPUT_BASE_PIN + 1u`，`:317` `BOARD_SYNC_MODE_OUT2_PIN = +2u`。
- 全仓对 **"OUT5" 零引用** （`git grep` 无命中），故 "OUT5" 不存在；用户已更正为 OUT4，与此一致。
- 输入侧：`:300` `BOARD_SYNC_INPUT_BASE_PIN`；`:305` `BOARD_SYNC_GATE_IN_PIN = +0u`、`:304` `EXT_CLK_IN = +1u`、`:303` `ARM_IN = +2u`、`:302` `TRIG_IN = +3u`。

**结论**：台架无转台时，由 **OUT4 发脉冲经硬件回环到 IN1**，作为转台输入的模拟；
接实际转台时则由转台直接驱动 **IN1**。因此 `T2-TURNTABLE-008` 的台架验收基线为
"**OUT4 → IN1 回环模拟**"，而非真实转台输入。此前关于接线方式的待确认项已闭合。


### 2026-10-13 T2-AUDIT-04 口径已定（用户裁定）：生产接 IN1、台架用 OUT4 模拟转台

用户裁定（逐字要点）：**后续接实际转台时，输入 IN1；现在没有转台的情况下，用 OUT4 模拟转台**。

两条推论：
1. **生产/实际转台** ⇒ 转台输入走 **IN1**，与参考实现的 **COUNTER 角色 / 输入 IN1** 一致（见前条取证）。
2. **台架（无转台）** ⇒ 用 **OUT4 模拟转台**，作为本地激励；因此台架验收的基线是“**OUT4 模拟**”，而非真实转台。

**待确认的实现细节（用户未说明，不推测）**：OUT4 与 IN1 的**物理连接方式**——是 OUT4 回环接入 IN1，
还是 OUT4 作为独立激励而由另一路取样；该细节影响 `T2-TURNTABLE-008` 验收的接线描述与证据口径。

**对后续工作的影响**：T2-AUDIT-04 的“选型”问题已结束；
接下来只剩把台架验收以“OUT4 模拟转台”为基线写清，并在接真实转台时切到 IN1。


### 2026-10-13 T2-AUDIT-03 决策单（五项冻结，可逐项批准）

用法：每项给出「现状锚点 / 可选取值 / 影响」；你只需按编号选一个取值。

| # | 项 | 现状锚点 | 可选取值 | 影响 |
|---|---|---|---|---|
| 1 | **wire 布局** | K-12 已定 10 B 单帧（tag + 4 bit `position_id` + 16 bit 版本 + 32 bit `target_ms_index` + 24 bit fine slice） | A：按此布局冻结并定义 RefMem 精确偏移/长度；B：改为可变长帧 | A 可直接登记；B 需重开 wire 设计与兼容性评估 |
| 2 | **与 TDMA 一等席布局联审** | 一等邮箱位置、`SYSTem:TDMA:SEAT:ROTation`（默认 1:1）、两板 effective schedule CRC 一致性门 | A：按现状联审并冻结；B：先调整轮换比或邮箱位置 | A 可直接推进；B 会触及 TDMA 域契约与 CRC 门提価 |
| 3 | **调试 profile 默认值** | 四个 offset/window 、`threshold_per_mille`（当前 950/1000）、`pre_roll_mdeg`/`post_roll_mdeg`（台架 5°，调试快照非产品额定值） | A：按台架快照冻结为**调试默认**；B：另行给出产品额定值 | A 可登记为调试契约；B 需额定依据与新台架验收 |
| 4 | **资源配额** | `TRIGGER_T2_RUNTIME_POSITION_COUNT`、`TRIGGER_POSITION_FREEZE_LATCH_IDS`、控制字节偏移、RAM 按实测 | A：按当前实测值冻结；B：预留余量后冻结 | A 可登记；B 可降低后续调整风险 |
| 5 | **正式时限** | STEP 下限（由转速与位置内序列跨度推出）、Core1 预算（顶层 `HIGH_PRIORITY` 500 µs）、`T2_FREEZE_RESEND_SLICES`、窗口到期判据 | A：按现状冻结；B：按高速扫描另计 | A 可登记；B 需重新取证时限 |

注：本单不改变任何实现；你逐项选 A/B（或给出第三种）后，我起草契约行并送 C11 交叉审核。


### 2026-10-13 T2-AUDIT-04 参考实现取证：转台输入与测量脉冲的已定角色

取证来源：分支 `origin/feature/node-sequence-reservation-trigger`（commit `19b9c5ff`）内的
《TRIGGER_SEQUENCE_ARCHITECTURE》第 **NSEQ-RSV-01** 节。

该节的槽位职责表（逐字要点）：

| 槽位职责 | 本地端 IO | 执行事实 |
|---|---|---|
| **COUNTER** | **IN1（转台）** | 累计计数达到位/阈值后才进入业务位 |
| SP8T 通路控制 | OUT1、OUT2 | 记录所选状态 |
| SP2T 极化控制 | OUT3（H/V） | 记录对应状态 |
| **VNA_GATEWAY** | **OUT4（测量脉冲）、IN2（READY）** | 只对应本地预约 |

两条推论：
1. **转台侧的含义已定**：转台作为 **COUNTER 角色、输入 IN1**；测量脉冲在 **OUT4**。
   后者与现有 `TRIGger:REServation:SOURce:*` 的 OUT4 硬件 PWM 路径**一致**。
2. **模式化的做法已定**（A-04/A-06）：通过 **SYNC_IO owner/persona 机制**实施热加载，
   常驻 SM 角色为 `TURNTABLE_SM`、`INGRESS_SM`、`EXECUTOR_SM`、`COUNTER_SM`；
   资源以 `sync_io_persona_resources.c` 与 `SYNC_IO_PERSONA_ID_SEQUENCE_COUNTER` 为准；
   切换顺序：停 SM → 停 DMA/IRQ → 清 FIFO → 卸载/装载 → 初始化执行位置 → **原子发布 generation**。

本条与本仓库现状的差异：本仓 `components/sync_io` 尚无 `sync_io_sequence.*`，
也尚无对应 persona；因此 T2-AUDIT-01 落地时应**按上述机制**实现，而非自制薄配置。


### 2026-10-13 T2-AUDIT-04 补充：“模型源”API 命名跨两条物理通道

取证（逐字）：

| 入口 | 实际转发到 | 物理通道 |
|---|---|---|
| `sync_io_model_pulse_schedule_arm` / `…_arm_ns` | `sync_io_output_pulse_schedule_arm[_ns]` | **波形输出**（PIO0/**SM2**，`BOARD_SYNC_PIO0_WAVE_OUTPUT_SM`） |
| `sync_io_model_pulse_schedule_arm_periodic_ns` | `sync_io_sma_observer_pulse_schedule_arm_periodic_ns` | **证据脉冲**（PIO0/**SM1**；`sync_io.h` 原文“External DPLL evidence pulses use PIO0/SM1 and SMA OUT1”） |

两条推论：
1. T2-AUDIT-04（转台源路径选型）必须声明验收所需的是**波形输出（SM2）**还是**证据脉冲（SM1）**。
2. T2-AUDIT-01 的模式声明只能声明 **SM2**（SM1 归证据通道），否则资源声明与物理不符（`ARCH-PIOPARTITION-01`）。

地址取证：`sync_io.h:216-233`（API 签名）、`sync_io.h:234`（SM1 注释）、`settle`——`sync_io_model_sched.c:1120-1131`（periodic 转发）、
`boards/rp2350_trig/inc/board_config.h:254/275`（`BOARD_SYNC_PIO_FAST=pio0`、`BOARD_SYNC_PIO0_WAVE_OUTPUT_SM=2u`）。





### T2-PROGRESS-20261012-001：T2 主线稳定性——三个根因定位与修复，判据收紧

- TODO task ID：`T2-STEP-001`、`T2-SOURCE-001`、`T2-DOC-001`；涉及 TDMA/VDC/RefMem/SYNC_IO 的 Core1 驻留与 TDMA 环时钟快照。
- **更正 2026-10-11 的阻塞链（已核实）**：「SYNC_TRIGGER 窗口 < WCET → 每周期 `start_missed` → 需优先级源 active 的 ingress 余量 → FOLLow 生效」不成立，来自两处解析错误：`SYSTem:TDMA:SCHEDule?` 每相位 11 字段（`SCHEDULE_PHASE_FIELDS`），SYNC_TRIGGER 是相位 7（`APP_REALTIME_PHASE_TABLE`）而非 6，原读数实为 REFMEM 的 `max_runtime`；`T2?` 的 `segment_max_cycles` 在 [42..44] 而非 [47..49]。`close_cycle` 为静态值（`app_realtime_phase_priority()`），与优先级源是否 active 无关。`T2:CURSor?` 的 [9] 是恒 0 的 legacy `identity_crc32`，绑定的 `ring_config_seq`/`model_token` 在 [10]/[11]。
- **根因 A：计数在游标有效前越过阈值（工具，已修）**。计数器由 PWM 源驱动（停源即冻结；源启动后约 1 kHz，快照非事实源）。源原先在 Step 16 启动，游标有效前计数已越过全部阈值；Core0 对 `last_counter_count >= threshold_count` 的位置不再生成 preview，该轮不可能发布。`t2_workflow.py` 现在 Step 1/21 停源，Step 19 仅在参考游标有效且计数未达首阈值时 `SOURce:STARt`，越过首阈值即具名失败。
- **根因 B：TRAIN 人格恢复期绑定游标后锁死（固件，已修）**。新增粘性故障现场记录（`trigger_time_cursor_fault_t`，`T2:CURSor?` 末尾追加 7 字段）实测：故障发生在首次绑定后下一拍（seq 1），`site RING`、`adapter_started=0`、config 未变。原因：`RING:START` 后环运行时为恢复 DATA/CS 人格停适配器一拍（`tdma_ring_runtime.c` 的 `training_dirty` 分支），若游标恰在此前一拍绑定即置粘性。修复：`tdma_ring_clock_snapshot_t` 增加 `training_dirty`，游标在其置位期间按未就绪重试、不绑定；绑定后再训练仍为粘性（单测 `train_persona` / `bound_retrain`）。
- **根因 C：SYNC_TRIGGER 超 WCET 被隔离（固件，已修）**。阈值切片（origin）与布防切片（从板）在 XIP 冷取指下超出 `PROJECT_CORE1_PHASE_SYNC_TRIGGER_WCET_CYCLES` 并被隔离（掩码位 6），冻结计数与游标。按 ELF 调用图把 SYNC_TRIGGER 每片/布防片路径上的 XIP 函数移入 SRAM：origin FREEZE 路径（`stage_one_freeze`、`resend_one_freeze`、RefMem stage/pending）；所有板的游标投影（`vdc_domain_dco_local_to_output_ns`、`vdc_dpll_manager_feedback_session`、`cursor_qualify_model`）；从板 `distributed_refmem_position_priority_drain`、`deadlines_need_service`、`cancel_backend`、`action_fail`、SWITCH/GATEWAY 的 `schedule_at`/`cancel`/`safe_stop`，以及 GCC 分裂到 XIP 的 SWITCH `poll.part.0`（改 `noinline`）。调用图上剩余的 flash 函数只在 legacy 触发、故障或会话切换路径，另有共享 libc `memcpy`/`memset` 与 SDK `clock_get_hz` 未迁移。origin FREEZE 路径的分段最大耗时追加在 `T2?` 末尾（`trigger_t2_runtime_freeze_timing()`）。
- **SRAM**：为驻留腾挪，用户决定把 `VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS` 缩为当前值（缩容史见该宏注释）；本轮链接后主 SRAM 余量见构建产物，快照非事实源。
- **判据收紧（工具）**：原判据 `configured==4 && publishing>0` 统计采样行，曾把「只发布一个位置随后从板被隔离」判为通过。现在 `t2_workflow.py` Step 20 要求参考 `published_mask` 为全部位置（`ALL_POSITIONS_MASK`）并把 SYNC_TRIGGER 隔离记为失败；`p3_hardware_acceptance.py t2` 另要求 workflow `failed steps: none`。按新判据离线重评旧日志：隔离轮全部转为 FAIL，干净轮保持 PASS。
- **台架（本次实测，快照非事实源）**：OTA（新增 `tools/ota/OTA_workflow.py` 首次实跑，标定保持）→ `DPLL60_workflow.py` 通过（Step 3/9 为非阻断备注）→ T2 连续 6 轮：前 3 轮全步骤通过；收紧判据后 3 轮中 2 轮通过、1 轮失败（根因见下条未闭合项）。修复后从板 SYNC_TRIGGER 最大耗时由超 WCET 降至约其 2/3，无 overrun、无隔离；参考板每轮发布全部 3 个位置。证据：`out/HardwareAcceptance/20261012/t2-20261012-1114*`、`t2-20261012-1117*`、`t2-20261012-1119*`、`t2-20261012-1125*`、`t2-20261012-1128*`、`t2-20261012-113046`。
- **未闭合**：① `CALibration:ORIGin:TRIAL` 间歇停参考环（`enabled`/`adapter_started` 归 0、config 前进 1），已与 `SOURce:STARt` 拆开确认由 TRIAL 单独触发；游标按 `site RING` 正确记录。② 从板 SYNC_TRIGGER 仍有大量 `start_miss`（前序相位迟到继承，不隔离、游标按 `missed_sync_count` 计入），既有现象，未处理。③ 游标段在所有板仍占较大份额。④ 构建号 `PROJECT_BUILD_ID` 为 CMake 缓存值，增量构建不变，凭证/OTA 以构建号辨识镜像存在盲区。
- **门禁（代码提交 `b2c527ed`）**：按用户决定取完整 P3 凭证。前两次 P3 均止于 DPLL 60 s 的主机侧判据，固件与采集本身通过：① RAM 页读取被瞬时拒绝即中止并残留 `-200` 污染复原（固件读租约需一次性 seqlock 复制与池所有者 CAS 同时成功）→ `vdc_priority_trace.py` 有界重试并清错误队列；② STOP 恰在 60 s 区间边界后产生单个 `PARTIAL|TERMINAL` 短尾 bin（固件设计内），判据要求恰好 6 个 bin → `p3_dpll_short.py` 只接受这一形状，端点按最后完整 bin + 尾 bin 包络判定。第三次 `p3_hardware_acceptance.py run`（quick）`PASS_WITH_WARNINGS`（ERROR/FATAL 为 0，WARN 类别与 2026-10-11 通过轮相同），`check-staged` OK；证据 `out/HardwareAcceptance/20261012/p3-133853`。




### T2-PROGRESS-20261011-027：M:N 轮换比例 SCPI 入口落地，一等席对齐特等列为下一目标

- TODO task ID：`T2-TRANSPORT-001`、`T2-TRANSPORT-002`；TDMA 侧 `TDMA-SEAT-02`（IN PROGRESS）、新增 `TDMA-SEAT-03`（PENDING）。
- **实现（TDMA 侧）**：轮换比例从编译期常量改为 STOP-only 的每板设置，`SYSTem:TDMA:SEAT:ROTation <M>,<N>` 写入（每 share 1..`TDMA_SEAT_ROTATION_SHARE_MAX`，`M+N <= TDMA_SEAT_ROTATION_CYCLE_MAX`，越界/零值/运行中拒绝），`SYSTem:TDMA:SEAT:ROTation?` 读回（未设置回报默认 1:1），`SYSTem:TDMA:SEAT:COUNters?` 读回 origin 六个席位计数；读到 0 时回落到编译默认，避免零周期。比例不进入 ring profile/geometry，wire CRC 不变。
- **验证（主机，本次实测）**：适配器 host 单测通过；`tests/python/test_tdma_seat_rotation.py` 通过（含 2:1、1:2 领队序列与零值回落）；SCPI/TDMA 相邻 python 测试 50 项通过。
- **对 T2 的含义**：比例现在可现场调，但调向特等会按比例拉长一等的等待上限；配置一等席前必须先有一等的期限/背压预算（见下条目标）。
- **下一步（用户指示）**：把一等席的服务等级提升到特等席水平。已定义为 `TDMA-SEAT-03`，缺口清单：①无一等专属 RX/TX 保全与独立队列（现与特等共用 lane 后按 class 分流）；②失败原因未按 class 细分；③从板记录级完整性校验为保预算被移除，仅 version 去重；④一等无独立期限/背压预算；⑤单深度邮箱的"有等待记录即拒绝 stage"未作为显式背压上报。
- **未闭合**：固件改动尚未取得 P3 硬件验收凭证（本机无四板台架），pre-commit 会按 staged 源码指纹拦截；比例与一等扩容须走 `EXE-SEAT-01` 与 C11 登记。




### T2-PROGRESS-20261011-026：按当前代码回写一等席快速路径与轮换比例语义

- TODO task ID：`T2-TRANSPORT-001`、`T2-TRANSPORT-002`、`T2-DOC-001`；关联新增 `TDMA-SEAT-02`。
- **触发**：用户指出「一等席现在也走快速路径」「1:1 是默认值但可通过 SCPI 配置」；复核代码后按现状回写语义，不按未实现的能力写。
- **代码事实（已核实）**：origin 快速路径的 1:1 轮换是**编译期常量** `TDMA_SEAT_ROTATION_PRIORITY_SHARE` / `TDMA_SEAT_ROTATION_FIRST_CLASS_SHARE`（`tdma_pio_spi_ring_adapter.h`），全库无写入者、无 SCPI 入口；SCPI 侧只有 `TRIGger:REServation:SEAT:MOUNT/MOUNT?/CLEar`（class→seat 挂载表），其唯一消费点位于默认关闭的窗口门（`TDMA_LOOP_SCHEDULE_ENABLED` 为 0）；origin 的 `seat_*` 计数未接 SCPI 只读页。从板侧一等记录已确认由优先级通道第二接收端承担，旧 `rx_complete_count` 不再增长。
- **回写**：`docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`（轮换比例改为「编译期常量、当前 1:1、无配置入口」；验收读数改 priority 计数；实现状态补从板接收与已闭环项）、`docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`（一等席行改为快速路径一等邮箱 + 发布机会轮换，删除「与普通 VDC 状态同帧、临时占用二等 RefMem 区域」）、`docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`（分片仅为迁移/回退路径）、`docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`（一等运输行按现状；补 K-5 实现方式说明：当前为独占发布机会，非同帧字段压缩）。
- **未闭合**：轮换比例的 SCPI 配置入口与 seat 计数只读页（新增 `TDMA-SEAT-02`，比例属席位预算须走 `EXE-SEAT-01`）；K-5 的「帧内压缩 VDC 字段」与当前「独占发布机会」两种资源模型须择一，待用户裁定。




### T2-PROGRESS-20261011-025：2026-10-11 复核汇总与口径回写

- TODO task ID：`T2-DOC-001`、`T2-STEP-001`、`T2-IDENT-001`、`T2-SOURCE-001`、`T2-FREEZE-002`、`T2-DOC-002`；产物 `docs/trigger/t2/TRIGGER_T2_STEP_REVIEW.md`（评审快照，非契约）。
- **范围（基线 `2c338fe0`，工作区干净）**：复核 10-09 评审的每条偏离项在当前代码是否仍成立、核对两份外部审计（① predictor/runtime/wire 硬编码，② `feature/node-sequence-reservation-trigger` 角度前端复用），并评估「31 个位置 + STEP + 零位 + 模拟源」决定的落地影响。
- **偏离项对账（已核实）**：A-01（改写已排队 VDC image）、A-15（22 片分片）、A-07（主机测试编译失败）已闭环；A-04 部分闭环（窗口门默认恢复关闭，SCPI 挂载路径仍在）；A-02、A-03、A-05、A-06、A-13、A-14、A-16、A-08、A-09 仍成立；`trigger_ready_overlap.h` 仍无固件调用方。
- **新发现（已核实）**：① 从板改走优先级通道后 `position_rx_complete_count` 不再增长，活跃计数为 priority accept/duplicate/reject，任何按 `rx_complete_count` 判定的验收门都会读到 0；② 冻结确认写的 `CONTROL_OPCODE`/`CONTROL_SEQ8` 与 action probe 写同两个字节，同时活跃时后写覆盖前者。
- **外部审计核对**：predictor/runtime/wire 审计逐条成立，仅「`prepare_core0` 无 horizon」需精确化为「每次调用从 index 0 起扫描、只 stage 一个」；分支角度前端审计的拓扑、命令表、`angle_derive` 校验、单位置在途与 link 角色校验成立，但验收凭证应为 500 Hz 源 × 60 位置（分支进展日志记录，1 kHz 记录属 PIO 输入捕获，且该日志声明不得据文件名宣称 1 kHz 验收），其引用的 `SCPI_COMMANDS.md` 行号属分支副本。
- **实测尺寸（宿主 gcc，快照非事实源）**：`trigger_position_descriptor_t` = 104 B、`trigger_action_runtime_event_t` = 32 B、`trigger_t2_runtime_snapshot_t`（按当前 `TRIGGER_T2_RUNTIME_POSITION_COUNT`）= 880 B。据此按现布局线性放大到长扫描的 SRAM 代价不可接受，在途槽位必须有界。
- **门禁（本次实测）**：`docs_check --strict-names` OK（176 文件）、`doc_regression_check` OK（仅既有已登记 WARN）、文档 pytest 38 passed、`test_trigger_t2_runtime.py` 24 passed、`p3 check-staged` OK、`--log-check` OK。
- **未闭合**：身份/确认位宽与在途槽位数（`T2-IDENT-001`）、模拟源选型与脉宽/ppd 参数（`T2-SOURCE-001`）、A-13 修法（`T2-FREEZE-002`）、09-27 run33/34 证据补记（`T2-DOC-002`，凭证原件待定位，禁止据推断填数字）。




### T2-PROGRESS-20261010-024：一等席经优先级通道接收、FREEZE 闭环确认，三从板动作全部完成

- TODO task ID：`T2-EXEC-001`、`T2-TRANSPORT-002`；提交 `3ddeb258`。
- **丢帧根因（TX/RX 预算实测，`t2-bench-probe-201442`，快照）**：从板普通 flight 路径每次捕获跳过积压、只取最新一帧，平均约 4 ms 一次（最长 12–14 ms），而一等记录在线上只停留约一个发布机会；另一缺陷是一等席上车不推进 `next_seq32`（实测增量 0），连续采到的两条 T2 记录 seq16 相同而被按段去重。送达率 33%/24%/2.5%；同期优先级通道 IRQ 覆盖约 99.7% 帧。
- **修复**：从板改由优先级通道接收一等记录（每帧捕获、线上重复帧视为同一记录、未入箱者由下一份重复帧重试）；每条一等记录独占 seq16；从板接收路径残余 XIP 调用入 RAM。
- **FREEZE 语义（用户确认）**：FREEZE 为开始冻结；从板冻结后经每个镜像的 control 字节回报冻结掩码，一次环回到 NO1；NO1 重发未完成位置直至全部目标确认，置冻结完成。
- **实测（四板，`t2-bench-probe-213024`，快照）**：三从板三个位置 frozen=7、ACT armed=done=7、fail=0；NO1 `freeze_complete_mask=7`、`abandoned=0`、重发 15 次；四板零隔离，从板 receive 段峰值 ≤8.5k 拍。
- **DPLL60**：首次 `dpll60-132430` 因主机调度停顿约 3.9 s 触发 25 s 窗口 `acquisition_overrun`（示波器采集 1.89 s 正常、相位最大约 34 ns、内部健康通过）；重跑 `dpll60-135802` PASS，作为本批凭证。
- **未闭合**：A-13（源早于预览越阈时 FREEZE 阻塞）仍以 `--late-source` 绕过；STEP/16 组/滑动窗口（T2-STEP-001、T2-SEQ-001、T2-EXEC-002）与 VDC LOCKED 门控（T2-GATE-001）未开始。




## 归档索引

| 文件 | ID 区间 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_01.md` | `T2-PROGRESS-20260926-016..T2-PROGRESS-20260922-001` | 35 | 2026-10-15 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_02.md` | `T2-PROGRESS-20260927-017..T2-PROGRESS-20260927-017` | 1 | 2026-10-18 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_03.md` | `T2-PROGRESS-20260927-020..T2-PROGRESS-20260927-018` | 3 | 2026-10-18 |
| `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_04.md` | `T2-PROGRESS-20261010-023..T2-PROGRESS-20261009-021` | 3 | 2026-10-18 |
