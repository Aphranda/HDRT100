# T2 预约触发任务进度

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-10-19

## 文档接口

本文记录真实执行、来源和证据，不冻结契约或代替 [TODO](TRIGGER_T2_TODO.md) 的任务状态；设计见 [Architecture](TRIGGER_T2_ARCHITECTURE.md)。条目使用 `T2-PROGRESS-YYYYMMDD-NNN`，最新在上；按 C14/C15 保留失败、历史和归档索引。

## 当前 checkpoint

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
