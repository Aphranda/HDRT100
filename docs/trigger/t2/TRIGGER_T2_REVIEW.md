# T2 预约触发代码与架构语义评审

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_REVIEW.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-10-10

## 文档接口与范围

本文是 2026-10-09 对 T2 代码的一次只读评审，回答两件事：代码是否偏离架构语义，以及 T2 的实际状态。本文是评审快照，**不登记为契约**（AGENTS.md 硬约束 6），也不改变 [TODO](TRIGGER_T2_TODO.md) 的任务状态；采纳的项须转为 TODO 任务，实测写入 [Task Progress](TRIGGER_T2_TASK_PROGRESS.md)。

- **基线**：分支 `wip/tdma-real-flight-processing`，HEAD `84f7f317`，包含 `7ff68367`（seat mount store / T2 enable chain）。评审过程中仓库由其他会话提交，文件:行号以该 HEAD 为准。
- **范围**：
  - `components/sync_trigger/`（t2_runtime、time_cursor、time_projection、position_*、deployment、action/template）
  - `components/distributed_refmem/src/distributed_refmem.c` 的 T2 位置发布路径
  - `components/tdma/` 的 seat mount store、loop schedule 与 origin 挂载门
  - 相关 Flash 映射、SCPI 命令和 `tools/sync_trigger/t2_enable.py`
  - `out/` 下的已有验收原件
- **方法**：读码、读取已有证据，并运行一次 T2 主机测试。未构建固件、未上板。
- **核实等级**：`已核实` 表示评审主控复读代码或复跑确认；`读码` 表示由子评审读码给出、主控未逐条复读；`推测` 表示推断，尚无代码或实测闭合。

**架构基准与优先级**：`TRIGGER_T2_ARCHITECTURE.md` 和 TODO 的“P0 最新载荷方案”取代了 `ARCH_T2_RESERVATION_ARCHITECTURE.md`（2026-08-20）的部分设计，例如 PREPARE/fence 执行门、主站 deadline 和 `TriggerReservationFB`。凡两者冲突的地方，本文以 T2 域文档为准，并在 D-04 指出 ARCH 文档未标注 superseded。

## 〇、语义口径（2026-10-10 用户确认）

评审后由用户确认以下五条口径。凡与 `TRIGGER_T2_ARCHITECTURE.md` 冲突的地方，以本节为准，并按 D-07 回写架构文档。

| # | 口径 | 对本文结论的影响 |
|---|---|---|
| K-1 | 转台到达某位置后，**先切换通道（SWITCH），再触发网分测试（GATEWAY TRIG）**。**多板模式下**网分的 READY 只做异步的时间片踩踏校验，不是许可门（单板模式可另行规定）。 | 架构中的调试默认值“SWITCH 在预测到位前两个时间片”与此冲突（D-07） |
| K-2 | **DCO 是底层时间源，无法冻结，始终正常更新**。**FREEZE 只冻结位置预测**（q 与版本），不冻结 DCO。单个动作被硬件接受后不再追改；位置内的后续动作在各自装填前按最新 DCO 重新解算——网分较慢，步间有足够间隙完成更新（评估见 E-01）。 | A-06 确认偏离：代码把边界放在 FREEZE，而不是逐动作装填 |
| K-3 | **VDC 进入 LOCKED 之后才能发布，T2 只有在 VDC 发布后才有输出**。 | A-02 的修复方向改为“只放行已发布的 LOCKED 状态”，并依赖 VDC 正式发布门禁 |
| K-4 | 除起始、零位、终止外，还有一个 **STEP 参数用来划分间隔**，起止之间按 STEP 产生一串位置；**角度单位为度，分辨率 0.001°**；**每个 STEP 位置都执行完整触发**（切通道 → 触发网分 → READY 校验）。 | 新增 A-14 |
| K-6 | **每个位置运行多组序列与采样**（多次切换通道、多次触发网分）。**16 组全部预装，执行端使用包含两组的滑动窗口**：一组执行完即用最新 DCO 解算并推入下一组，始终两组在途；更新放在网分采样期间。 | 新增 A-16 |
| K-5 | **T2 一帧必须发完**，不分片。一等席的主要功能不是 VDC：保留 VDC 的核心能力，压缩部分 VDC 字段，把空间让给 T2 等一等席功能。DPLL/VDC 同步发布走特等席。 | A-01 的修复方向收敛为“单帧、一等席”；新增 A-15 |

“VDC 核心能力”具体保留哪些字段，须由 VDC 域给出清单，再与 TDMA 一等席布局一起核定（`TDMA-T2-001`、`EXE-SEAT-01`）。

## 一、结论摘要

1. **T2 目前没有任何跨板位置送达的证据**。里程碑 M0 只是部分达到，M1–M5 均未达到。
2. **最近的台架工具配反了发布端**（S-01）：NO1 没有配置，NO2–NO4 被配成位置源。在这个配置下，即使链路完全打通也不会产生位置帧。必须先修正，否则后续实测没有意义。
3. **运输层仍改写已排队的 VDC image**（A-01），直接违反 P0 方案。09-27 之后的工作没有改变这个阻塞点。
4. **核心时间语义有三处高风险偏离**：
   - VDC 锁定门禁形同虚设（A-02）。
   - FREEZE 帧不带运行代身份（A-03）。
   - cursor 一次失效就用软件取消已装填的动作，并作废全部后续位置（A-05）。
5. **HEAD 上 T2 主机测试编译失败**（A-07），原因是临时诊断插桩没有做主机条件编译。
6. 已有符合项：时间不冒充 T2_actual、时钟统一用 TIMER1、owner 边界清楚、READY 不作为许可门、RUN 中不热加载、主站不下发 deadline。详见第四节。

## 二、偏离项

| ID | 严重度 | 问题 | 位置 | 违反条款 | 等级 |
|---|---|---|---|---|---|
| A-01 | 高 | T2 位置帧覆盖已排队的 VDC 状态 image | `distributed_refmem.c:1656-1677` | TODO P0 第 5 条；TDMA 架构“四级负载”一等行 | 已核实 |
| A-02 | 高 | VDC 锁定门禁实际上不拒绝任何锁态；T2 在 VDC 未 LOCKED、未发布时就能输出 | `trigger_time_cursor.c:201`、`trigger_time_projection.c:15`、`trigger_t2_runtime.c:700-702` | K-3；ARCH_T2 时钟模型；`VDC-RUN-001` | 已核实 |
| A-03 | 高 | FREEZE 描述符不带 run/epoch/generation 身份 | `trigger_position_descriptor.h:190-193`、`trigger_t2_runtime.c:204-207` | T2 架构运行代隔离；`ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` 同代身份 | 已核实（风险场景为推测） |
| A-04 | 高 | 窗口门默认打开，且挂载门会造成队头阻塞 | `tdma_seat_admission.h:197`、`tdma_pio_spi_ring_origin.inc:372-385, 468-483` | `TDMA-PROGRESS-20261009-011` 冻结决定；`EXE-SEAT-01`；TDMA 架构 TDMA-DET-04 | 已核实（阻塞影响为读码） |
| A-05 | 中 | cursor 一次失效就软件取消已装填动作并作废全部冻结位置 | `trigger_t2_runtime.c:380-392` | ARCH_T2 arm guard 后不得软件“尽量取消” | 已核实（瞬时触发概率为推测） |
| A-06 | 高 | 从 FREEZE 起就停止按新 DCO 重算 deadline，而不是从开始执行起 | `trigger_t2_runtime.c:704, 710-711, 401-416` | K-2；ARCH_T2 跨 generation 重算 | 已核实 |
| A-07 | 中 | 临时诊断插桩跨域复用 TDMA store，且导致主机测试编译失败 | `trigger_t2_runtime.c:15, 459-481` 等 | 单 writer 诊断；`EXE-SAFE-01` 结构化诊断 | 已核实（复跑失败） |
| A-08 | 中 | 席位挂载表跨核读取无保护；挂载 SAVE 不经 STOP 门 | `tdma_seat_mount_store.c:150-155, 216-224`、`scpi_trigger_seat_commands.inc:40` | TDMA 架构 guarded handoff；`HAOFV_FLASH_ARCHITECTURE.md` RUN 中不开始新 program | 已核实 |
| A-09 | 中 | v2 Flash 映射中 seat mount 的读写地址不一致（仅影响 `v2_candidate` 构建；现行 `v1_compat` 使用专用分区） | `config/flash_deployment_map.h:57-72`、`tdma_seat_mount_store.c:73-83` | Flash 分区一致性 | 已核实 |
| A-10 | 中 | 节点数与角色数写死 | `trigger_deployment.h:7`、`trigger_position_descriptor.h:19, 192` | ARCH_T2 “从节点表和 mask 派生，支持 A0–A7” | 读码 |
| A-11 | 中 | Trigger 自行实现了 VDC 正向映射 | `trigger_time_projection.c`、`trigger_t2_runtime.c:729` | ARCH_T2 “VDC 是映射唯一 owner” | 读码 |
| A-12 | 低 | 运行快照的 seqlock 写法不规范，且 Core1 长时间持有奇数 guard | `trigger_t2_runtime.c:615, 674-675` | 跨核交接规则 | 读码 |
| A-13 | 低 | 阈值越过时若尚无预测，FREEZE 永不发出且没有错误计数 | `trigger_t2_runtime.c:130, 770` | 失败须显式记录 | 读码 |
| A-14 | 高 | 没有 STEP 参数，位置固定为起始/零位/终止 3 个 | `trigger_turntable_predictor.h:22-27`、`trigger_t2_runtime.h:12` | K-4 | 已核实 |
| A-16 | 高 | 每板每位置只有一个本地模板，无法运行多组序列与采样 | `trigger_t2_runtime.h:13`、`trigger_t2_runtime.c` 装填逻辑 | K-6 | 已核实 |
| A-15 | 高 | 位置描述符分 22 片发送，违反“一帧必须发完” | `trigger_position_descriptor.h:13-19`、`distributed_refmem.c` 位置分片发布 | K-5；TODO P0 单 `position_q` | 已核实（分片数为读码） |

### A-01：T2 位置帧覆盖已排队的 VDC image

历史描述（保留作记录）：`distributed_refmem_tdma_flight_sync_publish()` 曾在 `position_publication && fifo.tx_ready_count != 0u` 时调用 `tdma_flight_fifo_core0_patch_tx_ready()`，把整个 compact mailbox 覆盖到 CORE0_READY 槽里的普通 VDC 状态 image 上（含 class 与 seq 字段）。这条路径来自 `2953ad0c`/`03e058d5`。

**现状修正（2026-10-16 复核；三处独立证据一致）**：该路径**已删除、全库无调用者** ——
`TRIGGER_T2_STEP_REVIEW.md:63`（「全库无 `tdma_flight_fifo_core0_patch_tx_ready` / `tdma_service_patch_flight_tx_ready` 调用」，状态"已闭环"）、
`TDMA_TASK_PROGRESS.md:503`（「删除 `tdma_flight_fifo_core0_patch_tx_ready` 与 `tdma_service_patch_flight_tx_ready`」）、
`TDMA_DOMAIN_ARCHITECTURE.md:153`（「改写已排队 VDC image 的补丁路径…作废删除」）。
⇒ 位置发布**不再**覆盖已排队镜像；origin 启用特等 provider 时，特等与一等**都只走快速路径**，普通 flight FIFO 不参与 origin 的座位发布。本行早前"目前仍有效"的结论**作废**（该结论会误导"T2 可抢占已排队镜像"）。

评审期间，工作区曾出现过一版用 `if (0u && ...)` 关闭该路径、并去掉 `!resident_master` 的未提交改动，但它没有进入 `7ff68367`。

关于 `!resident_master`：`DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED` 默认为 0（`distributed_refmem.h:18`），此时 `resident_master` 恒为 false，这个条件挡不住 NO1。如果日后启用 VDC command 运输并去掉该条件，position 在 class 三元表达式中的优先级高于 `VDC_COMMAND`，会顶替 VDC 命令分片，违反 VDC 优先（读码）。

合规方向：position 走独立的一等席窗口，不复用 VDC image；或者只在没有普通帧到期的空闲周期发布。完成后删除 patch 路径及其测试。

### A-02：VDC 锁定门禁形同虚设

两处判断都是 `lock_state > VDC_DOMAIN_LOCK_FAULT`。而 `VDC_DOMAIN_LOCK_FAULT` 是枚举最大值（`vdc_domain.h:43-51`），所以 OFF、CHECKING、INITIAL_SYNC、HOLDOVER、RELOCKING、FAULT 都会被当作有效。`prepare_deadlines_core0`（`trigger_t2_runtime.c:700-702`）也只检查 `dco.valid`。

按 K-3，T2 只有在 VDC 进入 LOCKED 并发布之后才能输出。T2 架构中“不以本地 PI LOCKED 代替本地网格”（`TRIGGER_T2_ARCHITECTURE.md:228`）与此冲突，须按 D-07 修正。

修复方向：
- cursor 有效性与 Core0 解算都以“VDC 已发布”为前置，而不是直接读 `lock_state` 原始值；未发布时 cursor 可以继续推进用于诊断，但不得生成或装填动作 deadline。
- “VDC 发布”的正式判据属于 VDC 域：`VDC-LOCK-001`（formal promotion）与 `VDC-RUN-001`（正式消费门禁，明确列出 T2/READY 须拒绝 coarse/provisional/unlocked）均为 PENDING。T2 应消费 VDC 提供的发布状态，不自行判定锁态。
- 在 VDC 门禁落地前，台架调试如需放行，应使用显式的诊断开关，结果标记为诊断，不计入 T2 验收。
- HOLDOVER 是否允许输出须由 VDC 发布状态决定，T2 不另设规则。
- 补主机负测：OFF/CHECKING/INITIAL_SYNC/FREQ_LOCK/PHASE_LOCK/RELOCKING/FAULT 以及未发布的 LOCKED 均不得产生 deadline。

### A-03：FREEZE 不带运行代身份

接收端构造的 FREEZE 描述符只有 `position_id + prediction_version`。`trigger_position_descriptor_valid()` 对 FREEZE 只检查这两项（`trigger_position_descriptor.h:190-193`），而且 `position_id` 的上限是字面量 3。

本地 STOP 的收尾是完整的（`trigger_t2_runtime.c:521-522`，以及 `distributed_refmem_reset_position_stopped()` 清空 FIFO 和 TX image），但其他节点保留的、或仍在飞行中的旧 FREEZE 无法按运行代区分。推测后果：新一轮运行收到旧 FREEZE 时，如果本地没有 plan，就 `action_fail`，该位置整轮作废；如果有 plan，就会被提前冻结。

建议：FREEZE 至少带上 run_id、count_epoch 和 grid/deployment generation，并且与 PREVIEW 使用同一套身份校验。

### A-04：窗口门默认打开，挂载门造成队头阻塞

- `7ff68367` 把 `TDMA_LOOP_SCHEDULE_ENABLED` 默认改为 1。这与 `TDMA-PROGRESS-20261009-011` 的结论相反：该条实测开启后约一半的特等席发送机会被挡，并决定“保持默认关闭，等待消费者”。提交信息没有说明为何翻转。
- 当前计划按时间算窗，pending 参数写死为 `(true, true, false)`，不看物理 selection/commit 事实。这偏离了 TDMA 架构“一等席窗口须在 `PRIORITY_SELECTED`/`TX_COMMITTED` 之后计算”的规则。
- 挂载门在 `tdma_pio_spi_ring_origin.inc:468-483`：如果 FIFO 队头是已挂载的 load、又不在其窗口内，就 `return true` 推迟。双槽 FIFO 是有序的，排在它后面的 VDC 普通帧只能跟着等。
- 挂在 LOAD 席位上的 class 会被永久推迟，因为 `TDMA_LOOP_PLAN_LOAD_ENABLED=0`，计划里从不产生 LOAD 窗。
- 通过 SCPI 把 class 挂到 PRIORITY，等于不经 `EXE-SEAT-01` 审核就扩大了特等席的使用。

建议：默认值恢复为 0；启用前先提交 `EXE-SEAT-01` 预算清单并经用户审核；挂载门不得阻塞非挂载的 VDC 帧；拒绝挂到尚无窗口的 LOAD 席位，或者 fail closed 并计数。

### A-05：cursor 一次失效即全量作废

`service_deadlines(false)` 会调用 `cancel_backend()` 取消已装填的边沿，并把所有已冻结、未完成的位置标记为 failed（`trigger_t2_runtime.c:380-392`）。能触发它的失效并不都是粘滞的，例如 ring snapshot 读取失败（`trigger_time_cursor.c:233`）、MODEL_TIME、NO_SYNC。

ARCH_T2 规定：进入 arm guard 之后，不能靠软件“尽量取消”。建议区分粘滞故障与瞬时失效：瞬时失效只暂停新装填，已装填的硬件 deadline 保留，由 receipt 或超时判定结果；如果确需取消，取消结果要单独记为 uncertain。

### A-06：模型冻结的边界放错了位置

按 K-2，DCO 始终正常更新；T2 动作在**开始执行之前**跟随最新 DCO，**开始执行之后**不再参考新 DCO。代码把这个边界放在了 FREEZE：

- `prepare_deadlines_core0()` 对已冻结的位置直接跳过（`trigger_t2_runtime.c:704`），冻结后不再因 `model_token` 变化重算 deadline；只有冻结前才会重算（`:710-711`）。
- Core1 在接收 deadline、执行 FREEZE、排队装填时只比较 session 和 `ring_config_seq`，不比较 `model_token`（`:180-182, 234-237, 401-404`）。从 FREEZE 到真正装填之间（例如排队等待前一个位置执行完），DCO 的更新都被忽略。
- `TRIGGER_T2_ARCHITECTURE.md:242-243` 写的是“已冻结实例保留本次模型和 deadline”，边界同样定在冻结（D-07）。

装填之后的行为本身符合 K-2：装填时把绝对 TIMER1 tick 交给 `sync_io_switch_realtime_schedule_at()` / `sync_io_gateway_realtime_schedule_at()`（`:412-414`），之后 DCO 更新不再影响它。

修复方向：
- 把“开始执行”定义为该动作的 deadline 被硬件接受（`*_schedule_at()` 成功）的时刻，写成代码中的唯一边界。
- FREEZE 之后、开始执行之前：Core0 在 `model_token` 变化时继续为冻结位置重算 deadline；Core1 装填前比较 `model_token`，不一致就使用新解算，赶不上则判为迟到。
- 开始执行之后：沿用已装填的 deadline，不追改；DCO 继续正常更新，供尚未开始的动作与后续位置使用。
- 多组序列（A-16）下按**逐动作**为界：每个动作在装填前用最新 DCO 解算，装填后不再追改（依据见 E-01）。

T2 架构写的是“已冻结实例保留本次模型”，ARCH_T2 要求跨 generation 就重算。两者需要定一个口径：如果保留旧模型，就要给出模型年龄上限并在装填时校验。

### A-14：没有 STEP 参数（已核实）

`trigger_turntable_predictor_config_t` 只有 `start/zero/terminal_angle_mdeg`、`pre_roll/post_roll` 和 `threshold_per_mille`（`trigger_turntable_predictor.h:22-27`）；预测状态用固定长度 3 的数组（`target_count[3]`、`threshold_count[3]`）；运行时用 `TRIGGER_T2_RUNTIME_POSITION_COUNT`（为 3）分配全部 per-position 状态与掩码。`trigger_position_descriptor_valid()` 也把 `position_id` 上限写成字面量 3。

按 K-4，起止之间要按 STEP 划分出一串位置。影响面：
- 配置：predictor 与 `T2:CONFigure` 需要 STEP 参数（单位、与 pulses_per_degree 的取整、起止是否落在整 STEP 上）。
- 容量：位置数由 `(terminal − start) / STEP` 决定，不能再用固定数组和 `uint32_t` 位掩码；需要定义运行期同时在途的位置窗口（例如只保留“当前 + 下一个”），其余按序号滚动生成。
- 线上身份：`position_id` 不再是 1–3，要能表示序号并防回绕；FREEZE 与 PREVIEW 的身份校验同步扩展（与 A-03 一起改）。
- 时间片：相邻 STEP 位置的“切换 + 触发 + READY 窗口”不能互相踩踏（K-1），STEP 下限应由模板 offset 与 READY 窗口推出，并在配置时校验。

这是 P0 方案没有覆盖的需求，应先在 T2 架构与 TODO 中补充，再改代码。

**角度表示**：按 K-4，角度分辨率为 0.001°。当前 T2 predictor 已使用整数毫度（`*_mdeg`），STEP 也应使用整数毫度，位置角度为 `start_mdeg + k × step_mdeg`，配置时用整数运算校验 `(terminal − start)` 能被 STEP 整除、`step_mdeg × pulses_per_degree / 1000` 为整数脉冲。线上与 Core1 只处理整数。

**参考实现：`feature/node-sequence-reservation-trigger` 分支的角度控制**（与本分支自 2026-09-15 分叉，之后各有约 46 个提交；读码，未合并、未构建）：

| 项 | 该分支做法 | 对 T2 的意义 |
|---|---|---|
| SCPI | `CONFigure:ANGLe:SWEEp start,stop,step,speed`、`CONFigure:ANGLe:INPut INn,pulses_per_degree`、`CONFigure:ANGLe:SPEEd`，读回 `READ:ANGLe:SWEEp?/INPut?/SPEEd?/POSition?`；`PULSe`、`BREAkpoint` 明确返回未实现（`middleware/scpi_port/inc/scpi_config_commands.h:38-49`） | 可直接复用为 T2 的角度配置入口，不必另起一套命令 |
| 派生 | 位置数 = `(stop − start)/step + 1`，必须为整数；每 STEP 阈值脉冲 = `|step| × pulses_per_degree`，必须为整数；校验累计计数不溢出（`scpi_config_commands.c` 的 `angle_derive()`） | 与 A-14 所需的配置校验一致 |
| 数值 | Core0 用 `double` 加 ULP 容差判整；Core1 只消费整数阈值和位置数 | T2 应改用整数毫度判整，避免 0.001° 网格下的浮点误判 |
| 位置语义 | START 后第一个计数阈值对应起始角度；`READ:ANGLe:POSition?` 返回当前与下一位置角度，并声明不是机械实测位置 | 可沿用 |
| 前后窗口 | 第四模式按“位置数”配置前置/后置 VDC 窗口（NSEQ-RSV-05） | T2 现用 pre/post-roll 角度，两者需统一口径 |

该分支与第〇节口径的对照：
- **READY 语义（冲突，限多板）**：其第四模式（共享 VDC 预约模式）规定“每次 VNA READY 后才预约下一步”（`TRIGGER_SEQUENCE_ARCHITECTURE.md` NSEQ-RSV-02/03），即 READY 授权下一步。按 K-1，多板模式下 READY 只做异步踩踏校验，这一推进方式不能用于多板 T2；单板模式可保留。
- **模型更新（一致，边界需统一）**：其 NSEQ-RSV-06 规定“已承诺前缀的目标和 deadline 不可被新模型追改，未提交后缀重新准入”，与 K-2 一致，前提是“已承诺”指“已开始执行”。两边应使用同一个边界定义（见 A-06）。
- **每位置组合序列（一致）**：该分支每个位置执行完整的 SP8T/SP2T 组合序列，与 K-6 一致；T2 侧的缺口见 A-16。

### A-16：每位置只有一个本地模板（已核实）

`TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` 为 1（`trigger_t2_runtime.h:13`），每块板每个位置只能展开一个动作；装填逻辑一次只选一个最早的冻结位置、装一个 deadline（`trigger_t2_runtime.c:395-416`）。按 K-6，每个位置要运行多组序列与采样，即在同一位置内按顺序多次切换通道、多次触发网分。

在多板模式下，K-1 规定 READY 只做异步踩踏校验，因此位置内各步**不能由 READY 推进**，只能按静态模板中预先算好的相对时间片排布：

```text
位置 P（q_P）:
  SWITCH(code_1) @ q_P + d_s1 -> TRIG_1 @ q_P + d_t1 -> READY 窗口_1（异步校验）
  SWITCH(code_2) @ q_P + d_s2 -> TRIG_2 @ q_P + d_t2 -> READY 窗口_2
  ...
  SWITCH(code_n) ...           -> TRIG_n ...          -> READY 窗口_n
```

由此产生的要求（按 K-6：16 组预装、每次推送两组）：
- 预装：每板每位置的 16 组序列内容在位置开始前全部就位，属于静态模板或位置实例，RUN 中不热加载。当前 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` 为 1，需扩到每位置 16 组。
- 推送两组：执行端需要能同时持有两组未到期的绝对 deadline。当前 `sync_io_switch_realtime_schedule_at()` 与 `sync_io_gateway_realtime_schedule_at()` 都是单槽：上一个处于 PENDING 时拒绝新的（`sync_io_switch.c:332-338`），因此执行端要扩成深度 2 的队列（或双缓冲），并为每组保留独立的序号、回执与取消结果。
- 推送时机：第 k 组执行完、网分开始测量时，用最新 DCO 解算第 k+2 组并推入，使队列始终保持两组；装填时比较 `model_token`。
- 各板分工：NO2/NO4 只负责切换步，NO3 只负责触发与 READY；所有步共享同一个 q_P，偏移来自各板模板。
- 时间预算：一组的跨度 = 切换 settle + 网分单次测量时间 + READY 窗口；整个位置的跨度 = n × 一组跨度。STEP 的时间间隔（由 STEP 角度与转速换算）必须大于整个位置的跨度加余量，否则下一位置会踩到上一位置。该下限应在配置时校验。
- READY 校验：每组 READY 都要和对应那一步关联，踩踏判定逐组进行。
- 参考：`feature/node-sequence-reservation-trigger` 已实现单板上的“每位置完整 SP8T/SP2T 组合序列”，但它由 READY 推进下一步，只能借鉴序列定义与资源核算，不能照搬推进方式（见 A-14）。

### E-01：位置内多组序列的 DCO 新鲜度评估（估算，快照，非事实源）

**场景**：每个位置 16 组序列，网分单次响应约 6–7 ms（用户给出），加上切换 settle，单个位置跨度约 0.1–0.13 s。问题是：如果整个位置都沿用开始时的 DCO 模型，最后一步会偏多少。

**误差来源**：模型里已经含有本板相对 NO1 的频率（实测基线为数 ppm 量级，见 VDC 进展），冻结模型的误差只来自“冻结后真实频率与模型频率之差”的积累：`e(t) ≈ Δf × t + ½ × 斜率限制 × t²`。

| 情形 | 依据 | t ≈ 0.12 s 时的偏移 |
|---|---|---|
| LOCKED 稳态 | 60 s 窗平均频偏约 0.3–0.4 ppb，600 s 窗约 0.004 ppb（2σ），τ=5 s ADEV 约 1.3 ppb（`VDC_TASK_PROGRESS.md`）；按 10 ppb 保守取 | ≈1 ns |
| DCO 速率按限速调整 | `slew_ppb_per_s` 默认 100 ppb/s（`vdc_reference.h`） | ≈0.7 ns |
| 未锁定、按单步上限追赶 | `VDC_BOUNDARY_AUTO_MAX_DELTA_PPB` | ≈120 ns |
| 合理性上限 | `VDC_DOMAIN_DEFAULT_SANITY_FREQ_LIMIT_PPB` | ≈1.2 µs |
| 模型完全不含速率（对照） | 板间晶振差数 ppm | 数百 ns |

**参照量**：
- 现有板间相位误差：外部示波器约 40–65 ns，内部稳态残差约 ±120 ns，白相位噪声约 3.8 ns rms（VDC 进展快照）。LOCKED 下冻结整个位置带来的约 1 ns 低于这个噪声底。
- 角度分辨率 0.001° 对应的时间容差是 `0.001° / 转速`：5°/s 时约 200 µs，30°/s 时约 33 µs。DCO 新鲜度带来的误差比它小 4–5 个数量级。
- 板间顺序余量（先切换、后触发）由切换 settle 决定，是毫秒量级。

**结论**：
1. 在 K-3 的前提下（只在 VDC LOCKED 并发布后输出），即使整个位置沿用同一模型，偏移也只有 ns 量级，不会“很大”；明显的偏移只出现在未锁定或模型长时间不更新（holdover）时，这应由 K-3 的发布门禁直接拒绝输出，而不是靠 T2 补偿。
2. 网分较慢，步间有约 6–7 ms 间隙，远大于 Core1 整表周期（1.5 ms）和 Core0 单次解算耗时。因此推荐**逐动作 just-in-time**：
   - Core0 在 DCO 模型变化时为该位置剩余的每个动作重新解算 deadline。
   - Core1 只在下一个动作即将到期前装填（提前量 = 硬件所需最小提前量 + 一个整表周期），装填前比较 `model_token`。
   - 单个动作被硬件接受后不再追改。
   这样每个动作的模型年龄不超过约一个整表周期，优于整位置冻结。
   - **预装与推送分开**（用户确认）：16 组的序列内容（各组的通道编码、相对 `q_P` 的偏移、网分脉宽等）在位置开始前全部预装；执行端使用**包含两组的滑动窗口**（用户确认，非成批推送）：同一时刻只持有两组已解算的绝对 deadline；一组执行完，就用最新 DCO 解算并推入下一组，窗口前移一格。每组装填时用的都是当时最新的 DCO。
   - **更新窗口放在网分采样期间**（用户确认）：第 k 组的 TRIG 被硬件接受后，到第 k+1 组 SWITCH 必须装填之前，正好是网分测量的 6–7 ms。这段时间内 Core0 用最新 DCO 解算第 k+1 组的 SWITCH/TRIG deadline，Core1 在 SWITCH_{k+1} 到期前装填。网分测量期间线上与输出都没有本组动作，重算不会影响正在执行的步骤。
   - 各板分工下，NO2/NO4 的更新窗口是“本板上一个 SWITCH 已执行、下一个 SWITCH 尚未装填”之间；NO3 的更新窗口是“本组 TRIG 已执行、下一组 TRIG 尚未装填”之间。两者都落在网分测量期间内。
3. 逐动作重算的前提：DCO 模型切换必须连续，不得在模型块边界产生相位跳变；否则同一位置内相邻两步之间会出现等于跳变量的间隔误差。`VDC_TASK_PROGRESS.md` 曾记录过模型块切换放大成毫秒级边界跳变的缺陷，需由 VDC 确认已修复并给出连续性证据。
4. 各步都锚定在同一个 `q_P` 加模板偏移上，不以上一步的 READY 为起点，所以网分响应时间的波动不会逐步累积；步间间隔必须按网分**最坏**响应时间加余量来排，READY 窗口用来发现踩踏。

### A-15：位置帧分片，违反“一帧必须发完”（已核实）

运行期仍发送完整 `trigger_position_descriptor_t`，分 `TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT` 片（读码为 22 片）。压缩格式 `trigger_position_slice.h` 在固件中没有调用方。09-27 实测只发出第 1 片（S-02），说明分片还放大了运输失败面：任意一片丢失或被背压，整条位置作废。

按 K-5：
- T2 位置必须在一帧内完整送达：一个紧凑记录（位置身份、`position_q`、版本或 FREEZE 状态），装进一个 mailbox 段。
- 一等席的帧内空间可以通过压缩“其余 VDC 状态”字段的占位来腾出，分配给 T2 等一等席负载；特等席（DPLL/VDC 同步发布）不受影响。
- 这需要 TDMA 定义一等席的帧内布局与预算（`TDMA-T2-001`、`TDMA-T2-002`），并按 `EXE-SEAT-01` 提交用户审核；做完之后 A-01 的 patch 路径可以整体删除。

### A-07：临时诊断插桩

`7ff68367` 自述包含“temporary bench diagnostic instrumentation”。它把 Trigger、VDC、TDMA STOP 闸门、SCPI 各处的拒绝码写进 `tdma_seat_mount_store_note_reject()`，覆盖面如下：

| 拒绝码 | 位置 |
|---|---|
| 140–148 | `trigger_t2_runtime.c` |
| 41–65 | `trigger_action_service.c` |
| 51–58 | `trigger_deployment_service.c` |
| 111–137 | `tdma_service.c`，VDC 与 Trigger 共用的 STOP 闸门 |
| 171–175 | `vdc_run_output.inc` |
| 90+ | 各 SCPI 命令 |

问题有四个：

- **层次倒置**：VDC 和 Trigger 反向依赖 TDMA 的 seat store。
- **诊断被覆盖**：所有路径共用一个标量，seat store 自己的 save 拒绝码（1–8）会被盖掉。
- **门禁被拆写**：原本的单表达式门禁被拆成多个分支，原则上语义等价，但每个分支都要逐一复核。
- **主机测试编译失败（已核实）**：`trigger_t2_runtime.c:15` 的 include 只在 `PICO_ON_DEVICE` 下生效，调用处却没有条件编译。`python -m pytest tests/python/test_trigger_t2_runtime.py` 在 HEAD 上报错 `implicit declaration of function 'tdma_seat_mount_store_note_reject'`。

建议：先恢复编译；然后改用各域自己的结构化诊断（原因、原始返回值、快照），或者把插桩收进一个仅台架构建开启的诊断模块；最后再按提交信息的承诺清理掉。

### A-08 / A-09：席位挂载存储

- **跨核读取无保护**：Core0 保存时直接写 `s_mount = *table`，再置 valid；清除时用 `memset`。Core1 在 origin 发布路径中通过 `tdma_seat_mount_store_get()` 整表复制。两边都没有 seqlock 或 barrier，可能读到撕裂的表。建议在 STOP/ARM 边界冻结一份 Core1 私有副本，或者使用 seqlock。
- **SAVE 不经 STOP 门**：`TRIGger:REServation:SEAT:MOUNT` 直接调用 `tdma_seat_mount_store_save()`，不经过 `tdma_service_run_stopped_maintenance()`。FlashTransaction 的 policy 也不拦截 TDMA 环处于 RUN 的情况（读码）。Core1 park 本身是安全的，但 RUN 中下发挂载命令会打断环。
- **v2 映射读写不一致**（`CMakeLists.txt` 中 `PROJECT_FLASH_DEPLOYMENT_MAP` 现行为 `v1_compat`，台架不受影响，迁移 v2 前必须修正）：v2 把 store 放在 `FLASH_MAP_CALIBRATION_NVS` 内，`RELATIVE_OFFSET` 为 2 个扇区。读取用 `sector_absolute_offset()`，即分区基址加扇区号，没有加 RELATIVE_OFFSET；写入用 `sector_relative_offset()`，加了 RELATIVE_OFFSET。结果是读到的是校准数据所在的扇区。同时 `ota_partition.h:33` 的断言要求 OTA scratch 末端等于 `SEAT_MOUNT_STORE_OFFSET`，在 v2 下推测无法成立。提交信息称使用了“dedicated SEAT_MOUNT flash partition”，但 v2 分支实际没有使用生成的 `FLASH_MAP_SEAT_MOUNT` 分区。
- **v1 compat 及登记问题（读码）**：v1 compat 的注释与实际分区来源不符；`flash_raw_call_allowlist.json` 登记的分区名与实际不符；改动已部署的 Flash 映射，应当按 Flash 契约走登记和交叉审核。

### A-10 至 A-13（读码）

- **A-10**：`TRIGGER_DEPLOYMENT_ROLE_COUNT` 写死为 4；source_slot 的上限用 `TRIGGER_POSITION_DESCRIPTOR_NODE_COUNT`，而 `PROJECT_NODE_CAPACITY`、`DISTRIBUTED_REFMEM_NODE_COUNT` 各不相同，三者都不是从节点表派生的；`configure` 不校验 target_mask 是否在范围内、是否排除了自身；`position_id` 的上限是字面量。T2 架构声明“部署表固定四个业务身份”，与 ARCH_T2 的 A0–A7 要求冲突，需要定一个口径。
- **A-11**：`trigger_time_projection.c` 自己实现了一份正向映射的快速路径（包括 rate 截断），cursor 用它；deadline 则用 VDC Domain 的反向映射。两者在切片边界上的舍入可能不一致（推测）。`prepare_deadlines_core0` 也没有复用 `trigger_time_grid_prepare` 的末格保留、最小 lead 和分辨率校验。
- **A-12**：`trigger_t2_runtime_snapshot()` 先普通拷贝、再做 acquire 读，中间没有 fence；Core1 在整个 service 期间（包括 PIO 调用）一直让 guard 保持奇数，而 Core0 只重试 4 次，`prepare_core0` 可能频繁跳过。
- **A-13**：`stage_one_freeze()` 在 `preview_copy` 失败时直接返回 true；之后 Core0 因阈值位已置位，不再解算。结果是 FREEZE 永不发出，也没有错误计数。

## 三、实际状态

### S-01：台架工具配反了发布端（已核实）

`tools/sync_trigger/t2_enable.py` 的 `run_full()` 用 `if slot:` 跳过了 slot 0（NO1），NO1 的 T2 从未被配置。NO2–NO4 则执行 `TRIGger:REServation:T2:CONFigure 1,{slot},15,1,...`；按 SCPI 的参数顺序（run_id, source_slot, target_mask, origin），这表示三块执行板都把自己设为源，并且 origin=1。

09-27 的台架配置是 NO1 为 origin、其余为接收端，与此相反（读码）。按当前配置，即使运输链路全部打通，也不会产生位置帧。

此外，21:38 那一轮工具因 `UnboundLocalError: src` 崩溃（`out/.../t2-20261009-213841`，读码）。

### S-05：部署 STAGe 被拒的代码级候选根因（已核实读码；与实测的对应为推测）

`TDMA_DOMAIN_TODO.md` 交接节把“`DEPloyment:STAGe` 被拒、`T2:CONFigure` 返回 92”列为当前唯一阻塞。代码中有一条确定的拒绝路径：

- `stage_stopped()` 在 `table->generation <= s_deployment.last_generation` 时，只有在“已 staged 且表相同”的情况下才放行，否则返回 `TRIGGER_DEPLOYMENT_GENERATION`（`trigger_deployment_service.c:116-120`）。
- `clear_stopped()` 清零整个状态，但**保留 `last_generation`**（`:194-196`）；`activate_stopped()` 成功后会把 `staged_valid` 清零。
- `t2_enable.py` 每轮都先 `DEPloyment:CLEar`，再固定用 generation 1 执行 `DEPloyment:STAGe 1,...`，并且丢弃了 STAGe 的返回值（`tools/sync_trigger/t2_enable.py:170-173`）。

因此，开机后第一轮可以成功；之后只要不重启，后续每轮 STAGe 都会以 GENERATION 被拒。之后 `DEPloyment?` 全为 0，`T2:CONFigure` 返回 92（`out/HardwareAcceptance/20261009/t2-20261009-222618/t2-enable.log` 四板均如此）。这与同日前几轮出现过 `act 1`、后几轮全为 0、以及交接节建议的“软复位四板”可绕过等现象一致。

验证：打印 STAGe 的返回码；或让工具使用单调递增的 generation。若确认，修工具而不是修固件：generation 单调是 STOP 换代语义的一部分。

### S-02：位置运输阻塞点

| 时间 | 证据 | 含义 |
|---|---|---|
| 09-27 run32 | NO1 `tx_active=1, publish=0`，等待资源 | FIFO 一直 ready，位置帧发不出去；`T2-PROGRESS-20260927-020` 记录的是这一轮 |
| 09-27 run33/34（coalesce 修复后） | 发出 1 个分片后，又卡在 FIFO 背压；NO2–NO4 rx 全为 0 | 完整描述符分 22 片，只发出第 1 片；**未记入进展日志** |
| 10-09 `t2-20261009-*` 约 20 轮 | 四板 POS 字段全为 0 | 与 S-01 配置错误相符，没有新的运输证据 |

推测根因：`tdma_flight_fifo_core0_patch_tx_ready()` 要求被 patch 的 ready 槽在 guard 字节处仍是普通 class。打过一次 T2 帧后条件不再满足，而 Core1 又迟迟不消费 TX，后续分片永远进不去。这与 `TDMA-PROGRESS-20261009-010` 记录的 `tx_publish_reject_count` 很高、`fifo_tx_reject_grew` 相符。阻塞点在 TDMA 的 TX 消费和席位层（`TDMA-FLIGHT-002K2`、`TDMA-T2-002`），而不在 T2 内部。

09-27 之后的席位准入、loop plan 和 seat mount store **都没有给 T2 挂上实际的发射者**，阻塞未被解除；A-04 反而可能进一步降低 TX 排空能力（推测）。

### S-03：任务状态对账

| TODO ID | 文档状态 | 实际（代码 / 测试 / 证据） | 偏差 |
|---|---|---|---|
| T2-DEPLOY-002 | IN PROGRESS | 09-27 四板静态装载读回成功；10-09 最新一轮 `deploy after activate` 四板均为未激活（读码） | 硬件出现倒退，未记录 |
| T2-ACTION-TEMPLATE-001 | PENDING | template store 已有 CRC、INSTall/ACTivate，有主机测试，并多次上板（读码） | 文档偏低，应为 IN PROGRESS |
| T2-CLOCK-001/002 | IN PROGRESS | 本地 cursor 已实现；10-09 多轮 NO2–NO4 报 cursor 无效（读码） | 一致，未闭合 |
| T2-TURNTABLE-001/002 | IN PROGRESS | predictor 与异步预览已实现，有主机测试；NO1 计数有硬件读回 | 一致 |
| T2-TURNTABLE-003 / P0 compact-q | IN PROGRESS | `trigger_position_slice.h` 在固件中无调用方；运输仍是完整描述符分片（已核实） | 一致；但 T2 架构称“不再使用完整描述符分片”，与代码不符（D-03） |
| T2-P0-CONFIG-001 | IN PROGRESS | `components/sync_trigger` 中没有 CONFIG_CONTROL / NODE_LOAD_STAGE 引用，仍是逐板 SCPI（读码） | 实际接近 PENDING |
| T2-TRANSPORT-001 | IN PROGRESS | 见 S-02 | 未打通 |
| T2-P0-FREEZE-001 | PENDING | FREEZE stage、阈值公开、`frozen_mask` 已实现，有主机测试（`97ad9199`） | 文档偏低，应为 IN PROGRESS（仅主机） |
| T2-P0-DIAG-001 | PENDING | 没有 T2 诊断 class | 一致 |
| T2-TURNTABLE-004 / T2-EXEC-001 | IN PROGRESS | Core0 deadline、Core1 PIO 提交与 TIMER1 后端已接线；`TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` 很小；没有位置驱动的硬件边沿 | 一致 |
| T2-READY-001 | IN PROGRESS | `trigger_ready_overlap.h` 只被主机测试使用，固件无调用方（读码） | 应注明“仅主机模型，未接线” |
| T2-TURNTABLE-008 | IN PROGRESS | SYNC_TRIGGER 曾被隔离；NO3 出现过超预算 | 一致 |
| 其余 PENDING 项 | PENDING | 无实现证据 | 一致 |

### S-04：里程碑

| 里程碑 | 状态 | 依据 |
|---|---|---|
| M0 静态部署 | 部分达到 | 09-27 四板读回成功；统一启动事务未实现，10-09 出现倒退 |
| M1 时间片 cursor | 未达到 | 最新多轮 cursor 无效，NO3 超预算 |
| M2–M5 | 未达到 | 没有任何接收端 `rx_complete`、position_id 级动作或 READY 关联 |

## 四、符合项（已核实或读码）

- **T2_actual 语义**：代码里没有用目标时间或 CPU 读时间冒充实际值。完成只认 PIO receipt 序号；`origin_before/after` 只作为诊断区间；`trigger_action_service` 的结果固定为 `diagnostic=1`、`actual_event_valid=0`。缺口是硬件 latch 还没实现（M5）。
- **时钟**：T2 全程使用 TIMER1（`vdc_timestamp_clock`、sync_io），不用 TIMER0；committed model 的读取有 seqlock 保护。
- **Owner 边界**：Trigger 不写 VDC/DPLL 状态，不直接操作 TDMA ring 或 PIO 寄存器；运输走 `distributed_refmem_*`，硬件走 `sync_io_*`。挂载表对 TDMA 来说是不透明的 class→seat 映射，TDMA 不解析 T2 业务含义。
- **双核分工**：Core0 负责预测与 q→deadline 解算，Core1 读计数、判阈值、装填预装的 PIO；位置 mailbox 使用正确的单生产者/单消费者 CAS 状态机（`trigger_position_mailbox.h`）。
- **FREEZE 行为**：主站越过阈值后停止发 PREVIEW；从板冻结后停止解算、不接受新版本，采用本地最新已完成的解算。
- **READY**：没有任何地方把 READY 当作许可门。
- **RUN 中不热加载**：资源装载都走 `run_stopped_maintenance`，运行期只做 `pio_sm_put` 和使能。
- **主站不下发 deadline**：线上只传位置，各从板按本地 DCO 自行解算。
- **迟到 fail closed**：deadline 被拒就标记失败，没有立即补发的回退路径。
- **seat mount store 本体**：Flash 写入经 FlashTransactionAO，有来源白名单、范围校验和 Core1 park/ACK；采用双扇区 generation 选取与两段提交；开机在 ARM 前加载，加载失败视为全部未挂载。

## 五、文档漂移

- **D-01**：`T2-PROGRESS-20261009-021` 称唯一阻塞是 `reset_position_stopped()`，但同日后几轮已经出现 NO2–NO4 `act 1`，说明 START 已经通过；`TDMA_DOMAIN_TODO.md` 的交接节又把阻塞改成 `DEPloyment:STAGe` 被拒。两份文档互相矛盾。021 还称“NO1 未配置符合预期”，与 S-01 矛盾；“多次 PASS”没有给出任何证据路径。
- **D-02**：09-27 run33/34 的结果，以及 `FLIGHT:FIFO:RESet` 的作用，都没有记入 T2 进展日志。`T2-PROGRESS-20260927-020` 的 `tx_publish_count=0` 只对应 run32。
- **D-03**：`TRIGGER_T2_ARCHITECTURE.md` 称运行期使用单 `position_q`、不再使用完整描述符分片，代码与此不符；TODO 承认“迁移实现”。架构文档应改为目标态描述，或者标注尚未实现。
- **D-04**：`ARCH_T2_RESERVATION_ARCHITECTURE.md` 的 PREPARE/READY/fence 执行门与 `TriggerReservationFB` 已被 T2 域文档取代，但它仍是 Draft，未标注 superseded，Last updated 停在 2026-08-20。
- **D-05**（2026-10-10 已更新 Last updated、P0 方案与依赖表，并新增 T2-STEP-001、T2-SEQ-001、T2-EXEC-002、T2-DCO-001、T2-GATE-001、T2-TRANSPORT-002；S-03 的状态偏差尚未改动）：`TRIGGER_T2_TODO.md` 的 Last updated 仍是 2026-09-27；“当前依赖”表没有写入发布阻塞；S-03 所列的状态偏差未更新。
- **D-06**：`7ff68367` 把固件、工具与文档（两份进展日志、README、`TDMA_TIMING_REVIEW.md`）混在一次提交里，违反 AGENTS.md §7 的分离提交约定。021 中“1 ms”“60 s”等数字没有标注快照（硬约束 2，读码）。
- **D-07**（2026-10-10 已按第〇节回写 `TRIGGER_T2_ARCHITECTURE.md` 新增 T2-SEMANTICS 一节并修改下列各处）：`TRIGGER_T2_ARCHITECTURE.md` 有四处与第〇节口径冲突，须回写：
  - `:184` 调试默认值“SWITCH 在预测到位位置前两个逻辑时间片执行”，与 K-1“到位后先切通道、再触发网分”冲突。
  - `:228` “不以本地 PI LOCKED……代替本地网格”，与 K-3 冲突。
  - `:242-243` “已冻结实例保留本次模型和 deadline”，把边界定在冻结；按 K-2 应定在开始执行。
  - `T2-ACTION` 一节的“动作解耦”示例每位置只有 SWITCH_A → SWITCH_B → GATEWAY_TRIG 一组，缺多组序列与采样（K-6）。
  - `:122-124` 只定义起始/零位/终止，缺 STEP、0.001° 分辨率与“每 STEP 全流程触发”（K-4）；`:218` 称 T2 使用“较低优先级的 RefMem 区域”，需按 K-5 改为一等席单帧紧凑记录。

## 六、建议顺序

1. **恢复可验证基线**：修复 A-07 的主机编译；修正 `t2_enable.py`，NO1 设为 origin、source_slot 为 0，NO2–NO4 设为 origin=0、source_slot=0，部署 generation 改为单调递增并检查 STAGe 返回值（S-05），并修掉 `src` 未定义的问题；把 `TDMA_LOOP_SCHEDULE_ENABLED` 恢复为 0（A-04），如需开启，先按 `EXE-SEAT-01` 提交预算并经用户审核。
2. **复现 M0**：四板部署 staged/active 读回、`cfg/act` 均为 1，结果记入进展日志，并更正 D-01、D-02。
3. **打通运输（TDMA 侧）**：用 `SYSTem:TDMA:FLIGHT:FIFO?` 的分因拒绝计数确认 Core1 是否在消费 ready 槽；以独立的一等席窗口取代 A-01 的 patch 路径（`TDMA-FLIGHT-002K2`、`TDMA-T2-002`）。完成标准是 NO1 发出全部分片，且至少一块从板 `rx_complete_count > 0`。
4. **修正时间语义**：按第〇节口径修 A-02（VDC 发布门禁，依赖 `VDC-RUN-001`）、A-06（冻结只锁位置，deadline 跟随 DCO）、A-03 FREEZE 身份、A-05 瞬时失效处理，每项都补主机负测；先按 D-07 回写 T2 架构。
5. **补 STEP、多组序列与单帧载荷**：先在架构与 TODO 中定义 STEP（A-14）、每位置多组序列及其时间预算（A-16）和一等席单帧紧凑记录（A-15），再改 predictor、模板容量、执行侧连续装填和运输，并删除 A-01 的 patch 路径。
6. **修 Flash 与跨核问题**：A-08 挂载 SAVE 加 STOP 门、挂载表加跨核保护；A-09 v2 映射改用专用分区或统一读写偏移，并走 Flash 登记。
7. **继续 P0 主线**：修 cursor 有效性（M1）→ compact-q 接线 → FREEZE 上板 → 诊断回传 → READY 接线；同步更新 TODO 状态（S-03、D-05）。
