# TRIGGER T2 任务进展归档 11

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_11.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-20

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20261018-034` .. `T2-PROGRESS-20261017-020`（按新鲜度倒排，最新在前），共 13 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。

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












