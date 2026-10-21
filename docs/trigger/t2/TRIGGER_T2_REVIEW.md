# T2 预约触发代码与架构语义评审

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_REVIEW.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-10-21

## 文档接口与范围

本文是 2026-10-09 对 T2 代码的一次只读评审，回答两件事：代码是否偏离架构语义，以及 T2 的实际状态；2026-10-18 增补第七节 sequence 阻塞点独立审核。本文是评审快照，**不登记为契约**（AGENTS.md 硬约束 6），也不改变 [TODO](TRIGGER_T2_TODO.md) 的任务状态；采纳的项须转为 TODO 任务，实测写入 [Task Progress](TRIGGER_T2_TASK_PROGRESS.md)。

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

## 七、sequence 阻塞点独立审核（2026-10-18）

> 本节是 2026-10-18 对 T2 位置内多组序列（sequence）的只读独立审核，接在第一至六节之后；不改变既有结论，也不把任何推断登记为契约。基线为 `fdc45274` + 工作区未提交改动（`components/sync_io/src/sync_io_switch.c`：`SWITCH_QUEUE_DEPTH` 改为 4、新增 `s_complete_*` 三个计数器），与 `T2-PROGRESS-20261018-061` 同一现场。

### 7.1 范围与方法

- **对象**：`sync_io_switch_realtime_stage_sequence()` → `poll()` → `trigger_t2_runtime.c::service_deadlines()/complete_action()` 的 3 组窗口（N=3）。
- **方法**：只读读码 + 用生产 C 源码在仓库既有 PIO mock 上跑临时探针。探针不修改仓库测试，只打印 `sync_io_switch_deadline_status_t` 与 `sync_io_switch_realtime_sequence_state()`；同一探针对工作区（深度 4）和 HEAD（深度 2）分别编译运行。
- **边界**：未构建固件、未上板；台架读数引自 `T2-PROGRESS-20261018-061`，本文只做代码归因。证据分级：`已核实（探针）`＝生产 C 在主机 mock 上复现；`读码`＝逐行核对；`推测`＝尚无执行证据。

### 7.2 结论摘要

1. **队列深度不是阻塞点**。3 组窗口在武装时最多只能预装 2 组：PIO TX FIFO 只有 4 word，`sequence_feed()` 每次只推 1 组，且要求 FIFO 电平 ≤2。`SWITCH_QUEUE_DEPTH` 从 2 提到 4 不改变这条路径，HEAD 与工作区的探针输出逐字相同。
2. **阻塞点在“每组完成”的状态契约**：第 2 组完成时 `poll()` 已经置 `COMPLETE`，而第 3 组还在队列里；补装第 3 组后又没有把 `s_deadline` 指向它。状态长期停在 `seq=base+1 / COMPLETE`，第 3 组的完成只写进 `s_switch.completed_sequence`，运行期看不到。
3. **运行期把同一个 COMPLETE 消费两次**：先在 `sequence > expected` 分支记一次 advance，下一拍又在 `else if (complete)` 记一次并清空 `s_armed_index`；第 3 组完成时已无人认领 ⇒ `finish=0`、`group_next=2`、槽位不回收 ⇒ 后续位置 NO_PLAN。
4. **`stage_sequence` 每个窗口只自增 2，而不是 `count`**：该字段的语义应是“最后发出的序号”，即 `base+count-1`；N=2 时 2 恰好等于 count，N=3 起相邻窗口的序号区间重叠。这是独立于上述阻塞点的身份缺陷。
5. **主机测试没有覆盖真实契约**：runtime mock 在每组推进时给 `receipt_mask=1`，而生产在换 head 时清零；switch 测试只断言最终 `completed_sequence`。两套测试都绿，但不代表 N=3 路径成立。

### 7.3 主机探针（工作区 = 深度 4）

以下为本次主机探针快照，非事实源：

```text
PROBE t=25     state=1 seq=1 mask=0 | count=3 fed=2 queued=2 sw=3 last=0 writes=0
PROBE t=30003  state=1 seq=2 mask=0 | count=3 fed=2 queued=1 sw=3 last=1 writes=1
PROBE t=70004  state=2 seq=2 mask=3 | count=3 fed=3 queued=1 sw=3 last=2 writes=2
PROBE t=120005 state=2 seq=2 mask=3 | count=3 fed=3 queued=0 sw=4 last=3 writes=3
PROBE-END t=160024 writes1=3 | state=2 seq=2 mask=3 | count=3 fed=3 queued=0 sw=4 last=3
```

状态取值：`state 1=ARMED, 2=COMPLETE`；`sw 3=PENDING, 4=COMPLETE`；`last=completed_sequence`；`writes` 是 group0 所在 SM 的边沿数（3 组各一拍）。

读法：

- t=25 武装后 `fed=2/queued=2`：第 3 组没有预装。原因是 TX FIFO 已满（两组 = 4 word），不是“队列深度 2 拒绝”。
- t=30003 第 1 组已发，status 只把 `sequence` 推到 2，`mask=0`：第 1 组的完成没有以 COMPLETE/回执形式出现。
- t=70004 第 2 组已发、第 3 组未发（`writes=2`），`status=COMPLETE seq=2 mask=3`，同时 `queued=1`：**COMPLETE 与未完成组并存**。
- t=120005 第 3 组已发（`writes=3`），`completed_sequence=3`，但 `status.sequence` 仍是 2：**最后一组的完成没有进入 status**。
- HEAD（深度 2）跑同一探针，输出逐字相同 ⇒ 深度 4 对该路径零影响。

### 7.4 阻塞链（与 061 台架 tuple 对齐）

以第 3 个窗口为例，`base=5, last=7`（序号为什么是 5 见 7.5 的 SEQ-B04）：

1. 武装：`fed=2/queued=2`，`expected=5, window_last=7`。
2. 第 1 组（seq5）完成：status `ARMED seq6 mask0`；运行期 `sequence>expected`，但 `receipt_mask==0`，走 `no_receipt` 返回，只增加 advance 计数，不调用 `complete_action()`。
3. 第 2 组（seq6）完成：`poll` 置 `COMPLETE seq6 mask3`，补装第 3 组（seq7），但不重指 `s_deadline`；运行期在 advance 分支调用一次 `complete_action()`（把第 2 组的完成记到第 1 组），`expected=6`。
4. 下一拍：`seq6==expected6` 且 `complete=1`，走 `else if (complete)` 再调用一次 `complete_action()`（把同一事件记到第 2 组），清 `s_armed_index`。
5. 第 3 组（seq7）完成：只更新 `completed_sequence=7`；status 仍 `seq6`；运行期不再服务。
6. 槽位停在 `group_next=2`，不回收；同一槽位的后续 FREEZE 无计划/被地板拒绝 ⇒ NO_PLAN 级联。

061 的 `tuple seq=6 expected=6 window_last=7 complete=1` 正好是第 4 步后的快照；`adv=6 finish=0 group_next=2` 与“每位置 2 次 advance”一致。061 的“队列深度 2 拒掉第 3 组”不是这条链的成因：第 3 组确实在 t=70004 被补装（`fed=3`），问题是补装后的状态契约。

补充（读码推断，见 SEQ-B09）：`e6564a38` 新增的“最后一组完成直接走 `complete_action`”捷径在 N=3 时到不了（arm 已在 `seq=base+1` 被 `else if (complete)` 清掉）；在 N=2 时它会在第 2 组完成时只把第 1 组推进完就清 `s_armed_index`，按读码 `finish` 也应为 0。这与 061 的“N=2 一直正常”冲突，建议用 N=2 台架复验；该捷径不应在 `complete_action()` 尚未 finish 时无条件清 arm。

### 7.5 发现表

| ID | 严重度 | 位置 | 发现 | 证据 |
|---|---|---|---|---|
| SEQ-B01 | 阻塞 | `sync_io_switch.c:370,387` | 换 head 时先置 COMPLETE，再在队列空时补装下一组并直接 return；COMPLETE 与 `queued>0` 并存 | 探针 t=70004 |
| SEQ-B02 | 阻塞 | `sync_io_switch.c:375-382,387` | 补装只写 `s_queue[s_queue_count++]=...`，不更新 `s_deadline`；最后一组完成只更新 `completed_sequence`，`status.sequence` 停在上一组 | 探针 t=120005（`last=3, seq=2`） |
| SEQ-B03 | 阻塞 | `trigger_t2_runtime.c:1263-1290,1298-1300` | 同一个 COMPLETE 先在 advance 分支消费，下一拍再在 `else if (complete)` 消费；第二次清 `s_armed_index`；中途的 `!receipt` 又挡掉第 1 组完成 | 读码 + 061 台架 `adv=6 finish=0` |
| SEQ-B04 | 高 | `sync_io_switch.c:643,646` | 窗口内 `stage_sequence` 自增 2 次；该字段应为 `base+count-1`。N=2 时 2==count，N=3 起区间重叠（`[1,3]`、`[3,5]`、`[5,7]`） | 读码 + 061 tuple 的 base=5/last=7 与两次自增一致 |
| SEQ-B05 | 中 | `sync_io_switch.c:407-435` | TX FIFO 4 word 只容 2 组，`sequence_feed()` 每次 1 组；`SWITCH_QUEUE_DEPTH 4` 对此路径无效 | 探针 `fed=2`；HEAD 与工作区输出相同 |
| SEQ-B06 | 中 | `sync_io_switch.c:73,395-397` | 三个 `s_complete_*` 只写不读，`sync_io_switch_realtime_sequence_state()` 未暴露；061 承诺的 COMPLETE 时刻字段读不到 | 读码；全仓无读取点 |
| SEQ-B07 | 中 | `tests/python/test_trigger_t2_runtime.py:124-129` | mock 在每组推进时 `receipt_mask=1` 且 `sequence++`；生产在 `sync_io_switch.c:378` 清零、补装不重指。37 项 runtime 测试全绿，但未覆盖真实契约 | 读码 + 37 passed（本次快照） |
| SEQ-B08 | 低 | `tests/python/test_sync_io_switch.py:398-437` | `sequence_case` 只断言最终 `completed_sequence==3` 与边沿间隔，不检查中间 status 或 COMPLETE 与 queued 并存；HEAD 与深度 4 都通过 | 本审核分别跑 HEAD/工作区 |
| SEQ-B09 | 高（读码推断） | `trigger_t2_runtime.c:1276-1280` | 最后一组捷径在 `complete_action()` 只推进一组后就清 `s_armed_index`；N=2 时按读码 `finish=0`，N=3 时该捷径根本到不了（arm 已先被清） | 读码；与 061 的“N=2 正常”冲突，待台架复验 |

### 7.6 修复方向（供采纳，不代替设计）

按依赖顺序：

1. **先定义“每组完成”的可观测事件**：给 `sync_io_switch_deadline_status_t` 增加 `completed_sequence`（或保留已完成 head 的 `receipt_mask` 直到消费），运行期按该事件推进 `complete_action()`，不要用“当前 head 的 sequence + 回执”猜组号。这样第 1 组和最后一组的完成都能被计数。
2. **COMPLETE 只在“所有组已装填 + 最后一组回执已到 + FIFO 排空”后发布**；队列空但还有组待装时，补装后必须把 `s_deadline` 重指到新 head（ARMED、deadline、sequence），不得让 COMPLETE 与 `queued>0` 并存。
3. **`stage_sequence` 按 `count` 推进**：武装后置为 `base+count-1`（最后发出的序号），不要固定 +2。N=2 的“正常”只是 2==count 的巧合。
4. **去掉或证明 `SWITCH_QUEUE_DEPTH 4`**：该路径最多排队 2 组，深度 4 不改变行为；若保留，只在单 deadline API 上维持 2 深度，并补注释说明 FIFO 与 queue 两个上限的关系。
5. **补回归测试**：switch 测试在每组边沿后断言“`queued>0` 时 status 不得 COMPLETE”和“`completed_sequence` 每组 +1”；runtime mock 必须按生产语义在换 head 时清 `receipt_mask`、补装时不改 sequence，并覆盖 N=2/N=3/N=4。测试应在没有真实台架时能红。

### 7.7 未决与边界

- 本节未验证固件、未上板；`SEQ-B03`/`SEQ-B07` 的运行期部分为读码 + 台架读数对齐，不是主机执行。
- gateway 多组序列存在同源风险：`s_switch_receipt_seen` 只在 switch 分支赋值，gateway 分支的中途推进永远没有 receipt，按当前 `if(!s_switch_receipt_seen) return` 也会挡掉中途组；061 的 GATEWAY `BACKEND=114` 是否同因未验证（推测）。
- 本节不改变 TODO 状态；采纳项应转成 TODO 任务并附台架判据。

## 八、sequence 第二轮独立审核：N=16 窗口边界（2026-10-18 晚）

> 本节接第七节，审核对象是“N=3 已跑通”之后的新现场：多窗口位置（16 组）在窗口交接处失败，以及工作区未提交的 `complete_action()` 边界分支。基线为 HEAD `21ff8c6e` + 未提交改动（`trigger_t2_runtime.c`：`!next_tick` 边界分支与 `window_boundary` 计数）。方法为只读读码 + 生产 C 在既有 runtime mock 上跑 16 组探针；探针把 mock 的 `completed_sequence`/`receipt_mask` 改成生产语义（仓库现有 mock 没有这些字段，见 8.4 的 SEQ-C06）。未构建固件、未上板。

### 8.1 结论摘要

1. **N=3 的“跑通”没有覆盖窗口交接。** 16 组位置在第一个窗口末（第 4 组）就失败：`exec=4, finish=0, boundary=0`，失败原因是 LATE；HEAD 与工作区未提交改动输出相同。
2. **直接原因 A：窗口内只推进 `group`，不推进 `action_q`。** 第 2/3/4 组完成时 `complete_action()` 走 `s_group_next < s_group_handed` 分支，只改 `group`；`s_deadline.action_q` 仍停在窗口首组（20001）。窗口末把它 + 一个 delta 得到 20201（第 2 组的 q），而 cursor 已到 20601（第 4 组）⇒ 选择循环判 LATE。
3. **直接原因 B：`!next_tick` 边界条件永远不成立。** 窗口内分支从不清理 `next_tick`，探针里每次 `complete_action` 都读到 `nt=2525125`；窗口末因此走“本地推进一组”而不是边界分支，`window_boundary=0`。
4. **直接原因 C：`completed > expected` 差一。** 武装时 expected=base；第 1 组完成时 `completed_sequence=base`，`completed > expected` 为假，第 1 组不被计数；后续每组都晚一拍，窗口末组由 `else if (complete)` 处理。该分支会清 `s_armed_index` 并落入选择循环，正是 LATE 的触发点。
5. **未提交的边界分支即使能到达也不成立。** 它把 `prediction_version` 清 0 后返回；调用方 `else if (complete)` 仍会清 arm 并立刻跑选择循环，Core0 来不及发布下一窗口 ⇒ 版本 0 会被判 NO_PLAN。窗口交接需要显式的“等待下一窗口计划”状态，不能在同一拍失效 + 选择。
6. **测试仍未覆盖。** 仓库 runtime mock 不设置 `status.completed_sequence`，37 项测试走的是旧的 `sequence` 路径；本节的 16 组探针必须先补 mock 才能复现。第七节的 SEQ-B07 仍未关闭。

### 8.2 16 组主机探针（生产语义 mock，工作区 = 未提交边界分支）

```text
step0  completed=1 expected=1 complete=0  -> 不推进（第1组完成被跳过）
step1  completed=2 expected=1 complete=0  -> ADVANCED 1->2，complete_action(g=0)，next=1
step2  completed=3 expected=2 complete=0  -> ADVANCED 2->3，complete_action(g=1)，next=2
step3  completed=4 expected=3 complete=0  -> ADVANCED 3->4，complete_action(g=2)，next=3
step4  completed=4 expected=4 complete=1  -> ELSE_COMPLETE armed=3 -> PROMOTE g=4 q=20201 cursor=20601 -> LATE
       exec=4 finish=0 boundary=0 fail_reason=LATE
```

- `complete_action` 每次的 `next_tick` 都是 2525125（第 1 组的下一拍），从未被清理。
- 第 4 组完成时 `s_deadline.action_q=20001`（窗口首组），本地推进 +200 得 20201；第 4 组的 q 是 20601（cursor 同值）⇒ LATE。
- HEAD（无未提交边界分支）输出相同：`exec=4 finish=0 boundary=0 LATE`。即该分支对本路径零作用。

### 8.3 阻塞链

1. 武装窗口 0..3，`handed=4`，`next_tick`=第 1 组 tick。
2. 第 1 组完成：`completed=base`，`expected=base`，`completed>expected` 不成立；`sequence` 前进但换 head 时 `receipt_mask=0`，第 1 组不推进。
3. 第 2 组完成：`completed=base+1`，推进第 1 组；`s_group_next=1 < handed=4`，只改 `group=1`，`action_q` 不变。
4. 第 3、4 组同理；到第 4 组完成时，`action_q` 仍是窗口首组 q，`next_tick` 仍是第 1 组 tick。
5. `else if (complete)` 处理第 4 组：`next_tick` 非零 ⇒ 走本地推进，`q=base+step`（第 2 组），`deadline_tick=next_tick`（第 1 组），清 arm。
6. 同拍选择循环看到 `plan->group=4` 但 `action_q <= cursor` ⇒ LATE，位置失败；16 组位置没有完成，后续窗口不再推进。

### 8.4 发现表

| ID | 严重度 | 位置 | 发现 | 证据 |
|---|---|---|---|---|
| SEQ-C01 | 阻塞 | `trigger_t2_runtime.c:1284` | `completed > expected` 差一：第 1 组 `completed=base==expected` 不计；每组晚一拍，窗口末组落到 `else if(complete)` | 探针 step0/step4 |
| SEQ-C02 | 阻塞 | `trigger_t2_runtime.c:1109-1114` | 窗口内推进只改 `group`，不更新 `action_q`/`deadline_tick`；窗口末 q 停留在首组 | 探针 q=20201 vs cursor=20601 |
| SEQ-C03 | 阻塞 | `trigger_t2_runtime.c:1121` | 边界条件 `!next_tick` 不可达：窗口内分支从不清理 `next_tick` | 探针 `nt=2525125`、`boundary=0` |
| SEQ-C04 | 高 | `trigger_t2_runtime.c:1121-1130,1326-1328` | 即使到达边界，清 `prediction_version` 后调用方清 arm 并立即选择，Core0 来不及发布 ⇒ NO_PLAN | 读码 + step4 路径 |
| SEQ-C05 | 高 | `trigger_t2_runtime.c:1100-1103,1326-1328` | 若只修 C01，让第 4 组走 completed 分支并在边界返回，下一拍 `else if(complete)` 会以 `GROUP_NONE(0xFF)` 再调 `complete_action`；`0xFF >= groups_total` ⇒ 直接 finish，4 组就收尾 | 读码推断；探针的 ELSE_COMPLETE 路径 |
| SEQ-C06 | 中 | `tests/python/test_trigger_t2_runtime.py:120-129` | mock 不设置 `status.completed_sequence`；37 passed 走旧 `sequence` 路径，未验证新分支/窗口交接 | 读码 + 本节探针需先补 mock |
| SEQ-C07 | 中 | `T2-PROGRESS-20261018-064` | `executed=12/LATE=3` 更像 3 个 16 组位置各在首窗口末失败（4 组×3），而不是 1 个位置的 3 个窗口；`LATE=3` 与探针每次边界 LATE 一致 | 探针 + 计数；待 position_id/槽位确认 |

### 8.5 修复方向

1. **先修 `completed_sequence` 的起点**：武装时把 expected 设为 `status.sequence - 1`（或 `completed >= expected` + first 标志），让第 1 组也走 `completed` 分支；这样第 4 组在窗口末不会被 `else if(complete)` 接管。
2. **窗口内同步推进 q/tick**：在 `s_group_next < s_group_handed` 分支按 `s_local_group[next].delta_q - s_local_group[group].delta_q` 累加 `action_q`，并把 `deadline_tick` 指到该组的 `group_tick[]`；不要让 q 停在窗口首组。
3. **把边界显式化**：用 `s_group_next == s_group_handed`（且 `s_group_next < groups_total`）判窗口末，而不是 `!next_tick`；窗口内分支应清理/忽略 `next_tick`（它只适用于单组窗口）。
4. **给窗口交接一个状态**：`complete_action()` 返回“窗口内前进 / 需要下一窗口 / 位置完成”三态；需要下一窗口时保留 `s_armed_index` 或置 `waiting_next_window`，不要清 arm、不要同拍选择。等 Core0 发布下一窗口计划后再 arm；超时才按 NO_PLAN/BACKEND 失败，不能用 LATE 代替。
5. **修测试**：mock 必须设置 `completed_sequence`（完成组，不是下一 head）、换 head 清 `receipt_mask`；新增 16 组用例，断言 16 次 advance、1 次 finish、`boundary` 计数正确、无 LATE。

### 8.6 对 064 的更正与边界

- 064 的“3 个窗口 ×4 组”与 `LATE=3` 不自洽：若一个位置真的跑了 3 个窗口，窗口之间不应产生 3 次 LATE；更合理的读法是 3 个 16 组位置各在首窗口末失败（`executed=12=3×4`，`LATE=3`）。请用 position_id/槽位探针确认；若成立，064 的“第 4 个窗口起停住”应改为“每个 16 组位置在第 1 个窗口末失败”。
- 本节未验证固件、未上板；16 组探针为生产 C + mock，未修改仓库测试。N=3 的 357 结论不否定，但它的成功路径没有覆盖窗口交接。
- 本节不改变 TODO 状态；采纳项应转成 TODO 任务并附台架判据。

## 九、约束冗余与 sequence 可实现性（2026-10-18 晚二）

> 问题：sequence 一直无法闭合，是否因为“冗余约束太多”？结论：**不是单纯的“多”，而是三条约束互不相容**；另外确有两处过时/重复口径在放大工作量。基线：HEAD `21ff8c6e` + 工作区 20:59 状态（已含 `s_waiting_window`、窗口内 `action_q` 推进）。

### 9.1 结论

1. **sequence 不是“不可实现”**；在单一 owner 模型下可实现。
2. **当前实现不可满足**：批量窗口（一次交 2–4 组）+ 逐组计划链（`previous.group+1`）+ 在途不重解（K-2）三者同时成立时，窗口边界处的下一组没有前驱计划可派生。2 组和 4 组窗口都会在第一个边界停住；只有 1 组/窗口能跑。
3. **“16 组”是过时口径**：架构 K-6 仍写 16，TODO 已更正为角色 8/2/2；台架用 16 组压测，制造 3 个边界，但真正的产品需求 SWITCH_A 8 走同一条链，按同一机制也会在第一个边界失败。16 组不是根因，但让问题看起来更“多”。
4. **冗余的是状态与口径，不是用户需求**：`groups_total`、`s_group_handed`、`plan.group_count`、`s_group_next`、`next_tick`、`completed_sequence`、`window_last`、`waiting_window` 等多套权威，都是“T2 过渡窗口”和“动作侧目标态”同时存在的症状。删变量不能解决；必须先选 owner。

### 9.2 约束清单与冲突

| 约束 / 口径 | 来源 | 现状 | 判定 |
|---|---|---|---|
| T2 只发时间片，动作侧展开整条序列 | `TRIGGER_T2_ARCHITECTURE.md:348`、`TRIGGER_T2_TODO.md:212` | 代码仍在 T2 侧跑组游标/window | **目标态** |
| T2 侧过渡窗口（`next_tick`、逐组游标、同遍推进） | `TRIGGER_T2_ARCHITECTURE.md:351` | 代码已实现 | **过渡态，应移除** |
| 一次 `stage_sequence` 交 2–4 组 | `trigger_t2_runtime.c:166,1395` | `T2_PLAN_GROUP_MAX=4` | **与逐组链冲突** |
| 第 g 组计划由 `previous.group+1` 派生 | `trigger_t2_runtime.c:2103` | Core0 solver | **与批量窗口冲突** |
| 在途组不重解（K-2） | `trigger_t2_runtime.c:2062` | `skip_inflight` | **与批量窗口冲突** |
| K-6 每位置 16 组 | `TRIGGER_T2_ARCHITECTURE.md:65` | 台架 `1,16,16,16` | **过时；与 TODO 冲突** |
| 角色组数 8/2/2 | `TRIGGER_T2_TODO.md:72-86` | 代码按模板行数 | **产品口径** |
| 两组滑动窗口 | `TRIGGER_T2_ARCHITECTURE.md:335` | 代码是 4 组批量 | **不一致** |
| TX FIFO 2 组 / queue depth 2 | `sync_io_switch.c` | 硬件/实现 | **不是根因** |

### 9.3 决定性冲突的机制

1. 武装时 Core0 发布一个从 group0 开始的计划，`group_count=N`（2 或 4）；这 N 组被一次性交给硬件，`action_group_in_flight` 指向其中一组。
2. Core0 的 solver 看到 `action_group_in_flight == group` 就跳过（K-2：在途不重解），所以 groups 1..N-1 没有各自独立的计划；邮箱里的最后一个计划仍是 group0。
3. 到窗口边界，`action_group_next=N`，运行期把 `prediction_version` 清 0 并等待 Core0 发布 group N。Core0 的 group>0 分支要求 `previous.group+1 == N`，而邮箱 `previous.group=0` ⇒ `0+1 != N`，直接跳过。
4. 于是 group N 永远不会被发布，`planver=0` 一直保持，位置停在边界。N=2 和 N=4 只是“停在第 2 组”还是“停在第 4 组”的差别；N=1 不产生边界，所以只有单组/窗口能跑。

这不是时序、回执或队列深度问题；是**计划派生模型**与**批量交付模型**不兼容。

### 9.4 探针证据

```text
4 组窗口: stage count=4, 4 组后 boundary=1, published=1, skip_inflight=3, planver=0, 不再推进
2 组窗口: stage count=2, 2 组后 boundary=1, published=1, skip_inflight=1, planver=0, 不再推进
```

- 2 组是编译期实验（把 `T2_PLAN_GROUP_MAX` 改为 2，同一 16 组模板），未修改仓库。
- 两种窗口都在第一个边界停住；唯一变化是停住前跑了几组。⇒ 边界链是阻塞点，窗口大小不是。
- `published=1`、`skip_inflight` 随窗口增大，说明中间组全部被“在途”跳过，邮箱没有可派生的前驱。

### 9.5 冗余口径

- **16 vs 8/2/2**：架构 `:65` 仍写 K-6 的 16 组；TODO `:73`/`:82` 已更正为 SWITCH_A 8 / SWITCH_B 2 / GATEWAY 2，并明确“16 组是模型源口径，不是 T2 角色需求”。台架继续用 `T2_SEQ_GROUPS='1,16,16,16'`，等于在过时口径上压测；SWITCH_A 8 同样会在第一个边界失败，16 只是多两次边界。两套口径都停留在架构/TODO，未登记为契约（`DOCS_REGISTRY.md` 没有 `T2-SEQ-001` 行），所以文档门禁不会报冲突，台架目标可以长期漂移。
- **窗口尺寸多套**：`T2_PLAN_GROUP_MAX=4`、`SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS=8`、`SYNC_IO_SWITCH_MAX_GROUPS=2`、`SWITCH_QUEUE_DEPTH=2`、K-6 两组——没有单一事实源。4 组 plan 是 RAM 妥协（16 组单窗口溢出 364 B），不是契约。
- **状态多套**：`groups_total`（模板总数）、`s_group_handed`（窗口末）、`plan.group_count`（本窗口组数）、`s_group_next`（游标）、`next_tick`（过渡下一拍）、`completed_sequence`（完成事件）、`window_last`（序号末）各自说话。边界 bug 的每次修复都只是让其中两套暂时一致；下一套仍会打架。
- **双 owner**：架构已把序列判给动作侧（T2-SEQ-001 MOVED），但 T2 侧过渡窗口仍在逐组配速。这不是“多一条约束”，是**两个 owner 同时生效**；它才是状态爆炸的根源。

### 9.6 路线选择

- **A（目标态，推荐）**：动作侧展开整条序列（`T2-EXEC-002` / `SYNC-SCHED-001`）。T2 只发时间片 + 本角色组表（8/2/2），不再有逐组 Core0 计划链，也不再有窗口边界交接。删除 `next_tick`、`s_group_handed`、`waiting_window` 和 T2 侧组游标。这是架构 `:348-351` 的方向。
- **B（短期，仅作正确性桥）**：T2 侧改为**一组/窗口**，Core0 在上一组执行时解下一组。没有边界，但实测 Core0 往返在重载从板上 >25 ms，紧间隔不够；只能证明单组链可行，不是产品方案。
- **C（混合，若必须保留 T2 侧批量）**：允许 Core0 直接发布“下一窗口 base”计划（从 descriptor + delta_q 派生，或允许 `previous.group + K == group` 的累计 delta），不要依赖 `previous.group+1`；同时只保留一套窗口状态并加有界等待/超时。工作量大于 A，且仍保留双 owner，只建议作为台架桥。

**建议**：先明确选 A 还是 C，再动代码；不要继续在边界状态机上打补丁。若选 A，16/8 组问题自然消失；若选 C，先把“窗口 base 计划”定义成契约，并删掉单步链。

### 9.7 边界

- 本节是约束/架构评审，不是代码修复；工作区仍在变动，结论以 20:59 快照为准。
- 2 组探针是编译期实验，未修改仓库测试；16 组探针为生产 C + mock，未上板。
- N=3（单窗口）仍然成立，但它不经过边界，不能证明多组/多窗口成立。
- 16 vs 8/2/2 的口径冲突应先回写架构/登记，否则后续台架目标仍会漂移。

## 十、16 组单窗口与网关首组（2026-10-18 深夜）

> 背景：`22b0202f` 把 `T2_PLAN_GROUP_MAX` 4→16，一个窗口装完一个位置的 16 组；台架 SWITCH_B `1728=16×108`、`1824=16×114` 零失败；GATEWAY 仍 `BACKEND=108`；SWITCH_A 本轮未运行。基线：HEAD `fcbe9bf4` + 工作区 22:25 状态（`backend_identity/terminal/arm` 三计数未提交）。方法：只读读码 + 16 组主机探针（生产 C + mock）。

### 10.1 结论

1. **开关 16 组单窗口成立**：主机探针 `stage count=16`、`exec=16`、`finish=1`、`boundary=0`；台架 1728/1824 与之一致。§9 的窗口边界问题在 ≤16 组时被“单窗口”绕开，不再是当前阻塞。
2. **网关 `BACKEND=108` 的根因不是 READY/stage 守卫，而是运行期武装分支漏了开关分支的两行簿记**：`s_action_sequence_last` 未设置、`expected` 未减 1。首组完成 `completed==expected` 不计；网关随即 schedule 下一组，`sequence` 前进，运行期走 `sequence != expected` → `action_fail(BACKEND)`，每位置一次，正好 108。
3. **16 组扩展没有同步到诊断/位图**：`action_group_done` 仍是 uint8，`group_pack` 仍是 4/4/8；16 组时 `done` 只保留前 8 组、`next=16` 回绕、`in_flight=15` 与“无在飞”冲突。台架 `executed` 可信（来自 `action_groups_executed`），但 `T2:ACTion?` 的组进度字不可信。
4. **16 组是新的硬墙，不是通用解**：`T2_PLAN_GROUP_MAX=16`，但模板容量 48；17+ 组的角色仍会在第 16 组边界走 `previous.group+1` 失败，configure 不拒绝。
5. **16 位参数/32 位 delta 是新的隐式限制**：网关 `pulse_ns` 或 template_id >65535 被拒；组跨度 >2^32 ticks（约 17.18 s）时计划静默截断。

### 10.2 网关 BACKEND 的根因

同一武装路径的两个分支：

```c
/* switch: trigger_t2_runtime.c:1486-1496 */
s_state.action_sequence[selected] = status.sequence;
s_action_sequence_last[selected] = status.sequence + (handed ? handed - 1u : 0u);
if (handed && s_state.action_sequence[selected]) --s_state.action_sequence[selected];

/* gateway: :1498-1503 */
s_state.action_sequence[selected] = status.sequence;   /* 只有这一行 */
```

后果：

1. 网关 `window_last=0`（`s_action_sequence_last` 只在开关分支赋值），多组窗口没有上界。
2. 首组完成时 `completed = base`、`expected = base`，`completed > expected` 为假；`service_core1` 随即 schedule 下一组，`s_deadline.sequence` 变成 `base+1`。
3. 运行期下一次 service 看到 `sequence != expected`（`base+1 != base`），走 `:1344` 的身份/后端分支 → `cancel_backend()` + `action_fail(BACKEND)`。
4. 单组网关不受影响（完成后没有下一组，`sequence` 不变，走 complete 分支）；多组网关在首组完成时必失败。108 个 16 组位置 ×1 次 = `BACKEND=108`，与台架完全吻合。
5. stage 拒绝桶全 0、`plan_count=0` 与这条链不矛盾：stage 成功，失败发生在运行期；`plan_count` 是后续 stop/configure 清掉的，不能反推 stage 失败。

**修复**：网关分支镜像开关的三行（`action_sequence_last`、`expected--`），再补 2/4/16 组网关主机测试；先看未提交的 `backend_identity` 是否从 108 归零，确认归因。

### 10.3 16 组诊断/位图冲突

- `action_group_done` 是 `uint8_t`（`trigger_t2_runtime.h:194`）；`s_state.action_group_done[index] |= 1u << group`（`:1108`）在 `group>=8` 时被截断。
- 16 组探针实测：第 8 组后 `done=0xFF`，第 16 组后仍是 `0xFF`（应为 `0xFFFF`）。
- `trigger_t2_runtime_group_pack`（`:2012-2019`）仍是 `next` 4 位 / `in_flight` 4 位 / `done` 8 位；架构 `:559` 也仍写 4/4/8。16 组下：
  - `next=16` 被 `&0xF` 回绕成 0；
  - `in_flight=15` 与“无在飞”的 `0xF` 撞码；
  - `done` 的 bit8–15 全部丢失。
- `action_groups_executed` 是 uint32，能到 16，所以台架 `executed=1824` 可信；但任何从 `done/next/in_flight` 读出的 16 组进度都不可信。

### 10.4 新的硬限制

- **16 组硬墙**：`T2_PLAN_GROUP_MAX=16`，`TRIGGER_ACTION_TEMPLATE_CAPACITY=48`；configure 接受最多 48 行（`:1568`），但计划只覆盖 16。第 17 组会在边界走 `previous.group+1`（`:2103`）失败，且 configure 不拒绝。当前角色 8/2/2 不触发，但代码/容量表宣称 48。
- **16 位参数**：`group_argument`/`template_id` 是 uint16（`:172-173`），configure 对 `row->argument > UINT16_MAX` 直接拒绝（`:1577`）。网关 `pulse_ns` >65535 ns（65.5 µs）或模板 ID >65535 会被拒，文档未写这个上限。
- **32 位 tick delta**：`group_tick_delta` 是 uint32；`if (delta > UINT32_MAX) break;`（`:2188`）会静默停止追加组，`group_count<16`，而 `groups_total` 仍是 16，随后在边界停住。跨度 >17.18 s 才会触发，但应 fail-fast，不应静默截断。
- **RAM**：16 组单窗口 96 B/slot，当前构建通过；这是用 32 位 delta + 16 位参数换来的，后续再加字段需重新核预算。

### 10.5 发现表

| ID | 严重度 | 位置 | 发现 | 证据 |
|---|---|---|---|---|
| SEQ-E01 | 阻塞 | `trigger_t2_runtime.c:1498-1503`（对比 `:1486-1496`） | 网关武装漏 `action_sequence_last` 与 `expected--`；首组完成不计，下一组 `sequence` 前进 → BACKEND，每位置一次 | 读码 + 台架 `BACKEND=108`；待 `backend_identity` 确认 |
| SEQ-E02 | 高 | `trigger_t2_runtime.h:194`、`trigger_t2_runtime.c:2019`、架构 `:559` | done 8 位 / pack 4/4/8 无法表示 16 组；done 丢 bit8–15、next=16 回绕、in_flight=15 撞 NONE | 探针 `done=0xFF`；pack 代码/文档 |
| SEQ-E03 | 中 | `trigger_t2_runtime.c:166,1103,2103` | 16 组硬墙；17+ 组仍边界失败，configure 不拒绝 17–48 行 | 读码；与 §9 同链 |
| SEQ-E04 | 中 | `trigger_t2_runtime.c:1577,1583,2175` | 16 位 argument/template_id 上限（65.5 µs/65535），未文档化 | 读码 |
| SEQ-E05 | 低 | `trigger_t2_runtime.c:2188` | >2^32 tick 跨度静默截断计划 | 读码 |
| SEQ-E06 | 中 | `tests/python/test_trigger_t2_runtime.py` mock | 38 passed，但 mock 不设 `completed_sequence`；无 16 组/网关多组用例；16 组仅台架+临时探针 | 读码 + 38 passed |

### 10.6 修复顺序

1. **网关先补三行**（`action_sequence_last`、`expected--`），加 2/4/16 组网关主机测试；用 `backend_identity` 确认 BACKEND=108 归零。
2. **扩 `action_group_done` 到 uint32**，重设计 `group_pack`（如 5/5/16 或拆字段），同步更新架构 `:559` 与页字段顺序测试；16 组台架确认 `done=0xFFFF`。
3. **configure 拒绝 `local_count > T2_PLAN_GROUP_MAX`**（或实现多窗口），把 16 组写成显式上限，不要让 17–48 行静默停住。
4. **为 argument/template_id/delta 上限加显式拒绝理由**，禁止静默 `break`/截断。
5. **修 runtime mock** 的 `completed_sequence`/`receipt_mask`，补 16 组用例；否则 38 passed 仍不覆盖当前主路径。

### 10.7 边界

- 本节未上板；16 组探针为生产 C + mock；gateway 多组未在主机复现，SEQ-E01 为读码 + 台架计数对齐。
- SWITCH_A 本轮 `state=LOADED / executed=0` 是个案，本节不归因；需该板的 `T2?` 与开关页确认是 lease 未拿还是武装被拒。
- 16 组单窗口的开关成功不否定 §9 的 >16 约束冲突；它只是把墙从 4 挪到 16。

## 十一、代码级复审：当前 sequence 通路与吞吐阻塞（2026-10-20）

> 本节针对 HEAD `d38f2cde` + 2026-10-20 未提交改动（网关 `late/stage_late` 诊断、运行期 liveness/`backend_idle`/`why` 追加字段）做逐行审查。上一节的状态结论仍然成立：稳定性/NO_PLAN 已收口，主阻塞是网关吞吐/位置准入；本节回答“代码上具体卡在哪、哪些新改动有缺陷”。

### 11.1 当前状态一句话

- 最新 09:05 快照：SWITCH_A 5680/5776（finished 355）、SWITCH_B 5664/5776（finished 354）、GATEWAY 2689/5776（finished 168，entered 178）；`no_plan=late=0`，网关 `backend=1`。
- 网关 361 个位置里 **183 个从未 entered/frozen**，不是 NO_PLAN/LATE；这是准入/吞吐问题。
- 网关每组仍走完整 `prepare_realtime`，`schedule_continuation` 在当前基线**结构上不可达**（见 11.2/G01）。

### 11.2 代码级发现

| ID | 严重度 | 位置 | 发现 | 证据/影响 |
|---|---|---|---|---|
| SEQ-G01 | 阻塞 | `sync_io_gateway.c:413-414,429,490-492` | 完成分支先 `pio_sm_set_enabled(false); s_output_running=false;`，随后才调用 `schedule_continuation()`；而快路径第一条判断就是 `!s_output_running`。因此 `cont_hit` 恒为 0、每组 `cont_miss`，16 组全部回退到 `prepare_realtime` 的完整重初始化 | 与 `T2-PROGRESS-20261020-001` 的台架 `cont_hit=0 / cont_miss=1487~2536` 完全一致；这是当前吞吐阻塞的直接机制 |
| SEQ-G02 | 高 | `sync_io_gateway.c:86,492,499,632-633`；`scpi...inc:580,585` | 新诊断把 `out[37]/[38]` 从 `cont_hit/cont_miss` **改语义**为 `stage_late_max/last`；`cont_hit/miss` 变量还在，但已无任何读取点。GWDIAG schema 仍为 `0xDA02`，字段数仍 42 | 现场无法再验证续装是否生效；旧的 GWDIAG 读者会把 37/38 读成 cont；没有字段顺序测试/版本号 |
| SEQ-G03 | 中 | `trigger_t2_runtime.c:1533-1542,1553-1555`；`sync_io_gateway.c:554` | `window_held` 只信 `s_window_staged[selected]` 布尔量，不校验 `s_deadline` 里的 plan 是否仍是当窗（version/group）。若后端丢了旧窗、`s_deadline` 已换成新 plan，而 `s_plan_count` 还没清，`window_held` 可能把新 plan 当旧窗；否则清标志后 `stage_sequence` 因 `s_plan_count` 非零直接 BUSY，位置被记 BACKEND | 读码推断；与“后端丢窗口后重新 staging”的注释矛盾：注释说会重新 staging，代码在 plan_pending 时实际会撞 BUSY |
| SEQ-G04 | 中 | `trigger_t2_runtime.c:1413-1414,1421` | 网关专用 `gateway_armed_group = (sequence == expected + 1)` 无条件接受“下一组已武装”，不检查 `s_group_armed`/`s_group_next` 是否匹配。它本意是避开中途 `no_receipt` 的 identity 失败，但也可能让一个停滞/错位的槽位一直保持 armed，只靠 `backend_idle` 兜底 | 读码推断；需要“sequence=expected+1 但 group 不匹配”的负测 |
| SEQ-G05 | 中 | `trigger_t2_runtime.c:1317,1915-1922` | `s_sweep_no_template` 在 `service_deadlines()` 里自增，但 `deadlines_need_service()` 在 `s_local_template_count==0` 时返回 false，`service_deadlines()` 根本不会进；该计数恒为 0，无法区分“无模板”与“无冻结/无 armed” | 代码路径确定；liveness 页少一个关键分桶 |
| SEQ-G06 | 低 | `trigger_t2_runtime.c:180,787,829,1570` | `s_slot_armed_once` 只写不读；注释说“armed-but-empty backend 只有曾武装过才算 lost”，但 `backend_idle` 分支并未使用它 | 死状态；要么删，要么接进 `backend_idle` 条件 |
| SEQ-G07 | 低 | `trigger_t2_runtime.c:1435-1444,1518-1542` | `backend_idle` 失败路径只 `action_fail(BACKEND)`，没有先 `cancel_owned`/清 `s_plan_count`；若后端还留着 staged plan，下一次选择可能再次 BUSY，再吃一个 BACKEND | 读码推断；liveness 修好了，但“恢复”仍可能连续失败 |
| SEQ-G08 | 低 | `trigger_t2_runtime.c:2030-2046` | `service_calls` 在 configured/active 两个 gate 之后才自增，注释却写“entered at all”；被 gate 拒绝的调用只进 `svc_gate_*`。统计口径与注释不一致 | 诊断口径问题，不影响功能 |

### 11.3 关键代码段

**G01：续装死路**

```c
/* sync_io_gateway.c:413-414 */
pio_sm_set_enabled(GATEWAY_PIO,OUTPUT_SM,false);
s_output_running=false;

/* :429-430 */
const bool armed=schedule_continuation(group.pulse_ns,group.deadline_tick) ||
                 sync_io_gateway_realtime_schedule_at(group.pulse_ns,group.deadline_tick);

/* :490-492 */
if(!s_output_running || pulse_ns!=s_gateway.pulse_ns ||
   pio_sm_get_tx_fifo_level(GATEWAY_PIO,OUTPUT_SM)!=0u) {
    ++s_continuation_miss;
    return false;
}
```

先关 SM 再试 continuation，所以快路径必然 miss。单字协议的注释（`:409-412`）说明保持 SM 运行会让第二组回执身份不匹配；两字协议（delay+sequence）是为此设计的，但 `a053bae0` 后因台架回归被 `0abc12c4` 回退。**当前吞吐问题不是“再调一个参数”能解决的，需要恢复两字协议 + 续装，或走动作侧展开。**

**G02：GWDIAG 37/38 改语义**

```c
/* sync_io_gateway.c:627-633 */
out[31]=s_late_max_ticks;
out[32]=s_late_last_ticks;
out[33]=s_stage_early_max_ticks;
out[37]=s_stage_late_max_ticks;   /* was s_continuation_hit */
out[38]=s_stage_late_last_ticks;  /* was s_continuation_miss */
```

`GWDIAG?` 仍输出 42 个字段、schema 仍 `0xDA02`；没有版本号/字段顺序测试。`s_continuation_hit/miss` 仍被写入但无人读取。现场若按旧索引读 37/38，会把“续装命中”误读成“stage 迟到”。

**G05：死计数**

```c
static bool deadlines_need_service(void) {
    return s_local_template_count && ...;   /* 无模板直接 false */
}
...
if (!s_local_template_count) { ++s_sweep_no_template; return; }  /* 进不来 */
```

`service_deadlines()` 只有 predicate 为真才调用，所以无模板时 `s_sweep_no_template` 永远 0。liveness 页会出现 `service_calls>0, arm_sweeps=0, sweep_no_template=0`，无法判断是“无模板”还是“无冻结”。

### 11.4 测试与证据

- `test_trigger_t2_runtime.py` + `test_sync_io_gateway.py`：**88 passed**（本次工作区）。
- 但现有 harness 不检查 GWDIAG 字段 37/38、不覆盖 `window_held` 的“旧窗 plan 被新 plan 替换”、也不覆盖 `backend_idle` 失败后的再次 staging；88 绿不能证明 G02/G03/G07。
- `sequence_workflow.py` 只读 `DIAG?`，不读 `GWDIAG?`；新增的 `late/stage_late` 不会出现在 `sequence_report.html`，现场仍要手工读 GWDIAG。

### 11.5 建议顺序

1. **先恢复续装可观测性**：把 `cont_hit/cont_miss` 放回 GWDIAG（新开字段或升 schema/字段顺序测试），再谈吞吐；否则无法验证修复是否生效。
2. **修 G01 的协议路径**：恢复两字协议（delay+sequence）并保持 SM 运行；同时用 `cancel_owned`/`s_armed_owner` 做组归属，避免 G03/G07 的旧窗/取消误伤。若两字协议仍回归，走 §9 路线 A（动作侧展开整条序列），不要在单字协议上继续堆诊断。
3. **修 G05/G06**：把无模板分桶移到 `trigger_t2_runtime_service()` 的 gate 层；`s_slot_armed_once` 要么接进 `backend_idle` 条件，要么删除。
4. **修 G03**：给 `s_window_staged` 配一个 plan 身份（group/version），`window_held` 只在身份一致时成立；丢窗时先 `cancel_owned` 清 plan，再 staging，避免 BUSY 直接吃 BACKEND。
5. **修 G04**：`gateway_armed_group` 增加 `s_group_armed != NONE` 且 `s_group_next` 在窗口内的条件；补“sequence=expected+1 但 group 不匹配”的负测。
6. **不要用 09:05 的 47% 直接归因吞吐**：先记录 build、groups、period、sample、cont_hit/miss；47%~99% 的波动目前缺控制变量。

### 11.6 边界

- 本节为代码级复审，未重新上板；G03/G04/G07 为读码推断，G01/G02/G05/G06 为代码路径确定。
- 工作区仍在变动，若后续提交改了这些行号，以提交内容为准。
- 稳定性/NO_PLAN 的结论不变；本节只针对吞吐、诊断可信度和窗口恢复路径。

### 11.7 2026-10-20 下午复核：每条发现的当前状态（已上板对账）

> 本节把 11.2 的八条发现与当天的台架数据逐条对账，并更正 11.1/11.3/11.5 的因果与顺序。读数为快照，非事实源。

| 发现 | 状态 | 台架/代码依据 |
|---|---|---|
| **G01** 续装结构不可达 | **确认**（结构事实成立） | 实测 `cont_hit` 差值 0、`cont_miss` 差值 = 完成组数 − 完成位置数（残差 0）。**但“重初始化 = 吞吐瓶颈”被否**：2507 次 miss × 段峰值 35026 cyc ≈ 0.7 s ≈ 整轮 0.14%；要吃掉一半整轮需每次 ≈101 ms |
| **G02** GWDIAG 37/38 改语义 | **确认，且比原文更糟** | 除改语义外，函数后段还有 `out[37]=s_continuation_hit; out[38]=s_continuation_miss;` **覆盖**了 `stage_late`，使该字段恒 0——前几轮读到的“表准时到手”全是假的。已修：37/38 只保留 `stage_late`，early 值移到 33 |
| **G03** `window_held` 只信布尔 | **确认 = 本轮修掉的根因** | 已改为 `plan_pending() && !backend_idle`。台架：网关 26k 次扫描全部停在 `if (s_armed_index < COUNT) return;`，而该槽位正是被 `window_held` 在未调用任何执行器接口的情况下标成“已武装”的 |
| **G05** `s_sweep_no_template` 死计数 | **确认** | predicate 先返回 false，`service_deadlines()` 进不去；该桶至今恒 0 |
| **G06** `s_slot_armed_once` 只写不读 | **确认** | 把它接进 `backend_idle` 条件会让 `[gateway]` 主机用例转红（ARMED+无 pending 在合法流程里也会出现），故暂时保留为死状态 |
| **G08** `service_calls` 口径 | **确认** | 注释与实现不符；已补 `svc_gate_configured/active` 两个分桶 |
| 11.1「183 个未进入 = 准入/吞吐」 | **细化（结论方向对，机制不同）** | 准入确实被挡，但直接原因是**三个槽位全冻结**（`frozen_mask=7`）且永不退休；而冻结不解除的原因是网关在 **t≈2 s 后彻底停摆**——不是“逐步变慢” |
| 11.3/11.5「优先恢复两字协议」 | **降级为待议** | 步长 8 ms→16 ms 实测无改善（`late=91`、`entered=3` 不变）；`late` 稳态仅 3 ms、偶发 75 ms。当前证据不支持“协议/步长”是主因 |

**当天新增、11.2 里没有的事实**（三条都能复现）：

1. **网关 t≈2 s 后完全停摆**：t=2..40 s 共 19 次采样，`groups=97 / entered=9 / frozen=9 / executed=6 / abandoned=0` **一字不变**，开关同时段 176→1824 组匀速。
2. **表早到、武装稳态只晚 3 ms**：`stage_early_max=193 ms`、`stage_late_max=0`、`late_max=75 ms`、`late_last=3.1 ms`。
3. **停摆期间失败计数全 0**：`no_plan/late/backend` 均 0，`abandoned=0`——这是“静默停摆”，不是“执行失败”。

⇒ 因此 11.5 的顺序应改为：**先把“停摆”变成具名失败或自动恢复**（本轮已做两处：`window_held` 校验 + 执行器侧“ARMED 且无在途”退休），再谈吞吐；两字协议与动作侧展开都排在其后。

### 11.8 边界（复核）

- 全部为 2026-10-20 当天台架，同固件同配置；未做多轮重复，单次读数不足以给出百分比契约。
- 本轮修复（`window_held` 校验、执行器退休）仍需多轮台架确认不回归，且提交前须走 P3/T2 验收。

## 十二、对三处“停摆修复”的代码复核（2026-10-20 下午）

> 先接受 11.7 的因果更正：`cont_miss` 的量级只有整轮的约 0.14%，**不是** t≈2 s 硬停的原因；停摆是“运行时认为有槽位在途、执行器实际无进展”的状态分叉。本节只审三处修复是否覆盖实际停摆，不重复吞吐讨论。

### 12.1 结论

三处修复方向正确，但**当前实现只覆盖了窄状态**；观测到的 `plan_count=16 / plan_next=1 / svc_wait` 增长这一主场景，仍可能不被任何一处接住。

### 12.2 关键发现

| ID | 严重度 | 位置 | 发现 | 后果 |
|---|---|---|---|---|
| SEQ-H01 | 阻塞 | `sync_io_gateway.c:429-435` | “ARMED 且无在途”退休条件同时要求 `!s_plan_count && !pending`。观测停摆是 `plan_count=16 / plan_next=1 / svc_wait` 增长 ⇒ `plan_count>0` 且 pending 为 1；条件为假，退休永不触发 | 修复 #2 对主场景无效；网关继续停在 `s_armed_index < COUNT` |
| SEQ-H02 | 阻塞 | `trigger_t2_runtime.c:1348,1533-1535` | `backend_idle` 只认 `state==IDLE && !pending`；停摆状态是 ARMED/COMPLETE/CANCELLED，不是 IDLE。`window_held` 仍用 `plan_pending && !backend_idle`，因此 ARMED+plan_pending 依旧被当成“窗口仍在泵” | 修复 #1 对主场景无效；运行期继续信布尔量 |
| SEQ-H03 | 严重 | `trigger_t2_runtime.c:1165-1167,1212-1215` | `complete_action()` 对 `group==GROUP_NONE`（以及 `group>=groups_total`）只加 `s_done_skip_none/range`，随后 `group<groups_total` 为假，**直接落入 finish 块**：`++s_action_finish_count`、置 `action_completed_mask`、发位置事件、释放窗口 | 把“组簿记丢失”伪装成“位置完成”；`finished/executed` 会虚增，丢失的组被掩盖；这不是“具名失败” |
| SEQ-H04 | 高 | `sync_io_gateway.c:649,655` | 新退休计数 `out[39]=s_armed_idle_released` 被同函数后面的 `out[39]=s_armed_owner` **覆盖**；与 11.7 修的 37/38 是同一类错误 | 无法从 GWDIAG 证明退休是否发生；`cont_hit/miss` 也仍无读取点 |
| SEQ-H05 | 中 | `sync_io_gateway.c:431-435,680-691` | 退休只把 `s_deadline.state` 置 CANCELLED，没有 `safe_stop()`/关 SM/清 FIFO/清 `s_plan_count`；运行期 terminal 分支因 `terminal=true` 不会调用 `cancel_backend()` | 可能留下孤儿脉冲；下一位置可能继续看到 `plan_pending`，再次 BUSY/假武装 |
| SEQ-H06 | 中 | `trigger_t2_runtime.c:1533-1542` | `window_held` 仍不校验 `s_deadline` 里的 plan 是否还是“当窗”（version/group）；新 plan 覆盖旧 plan 时可能把新窗当旧窗 | 修复 #1 只解决“IDLE”分支，未解决 plan 身份分叉 |
| SEQ-H07 | 低 | `trigger_t2_runtime.c:150-180,1317,1915` | `sweep_no_template` 仍因 predicate 先返回 false 而恒 0；`s_slot_armed_once` 仍只写不读 | liveness 漏斗仍缺“无模板”分桶；死状态未清 |

### 12.3 代码细节

**H01：退休条件排除了真正的停摆**

```c
/* sync_io_gateway.c:429-435 */
if(!s_plan_count && !__atomic_load_n(&s_realtime_pending,__ATOMIC_ACQUIRE) &&
   s_deadline.state==SYNC_IO_GATEWAY_DEADLINE_ARMED) {
    if(++s_armed_idle_passes>GATEWAY_ARMED_IDLE_PASSES) {
        s_armed_idle_passes=0u;
        ++s_armed_idle_released;
        s_deadline.state=SYNC_IO_GATEWAY_DEADLINE_CANCELLED;
    }
} else s_armed_idle_passes=0u;
```

观测停摆的 `svc_wait` 增长说明 `pending=1`、RX 空；`plan_count=16`。前两个条件都不满足，计数器根本不增长。**正确的看门狗应看“progress”而不是“pending/plan_count”**：记录 `s_service_complete`/`plan_next`/`s_pending_receipt` 的联合指纹，连续 N 次 service 不变即退休，不论 pending 是否为 1、plan_count 是否为 16。

**H02：`backend_idle` 的定义太窄**

```c
backend_idle = status.state == SYNC_IO_GATEWAY_DEADLINE_IDLE && !status.pending;
...
window_held = s_window_staged[selected] &&
    (kind != GATEWAY || (plan_pending() && !backend_idle));
```

停摆时状态通常是 ARMED（有 pending）或 COMPLETE/CANCELLED（无 pending 但不是 IDLE）。`!backend_idle` 全为真 ⇒ `window_held` 仍为真。应把“pumping”定义为：`state==ARMED && (pending || tx_fifo/SM 在跑)`，或 `plan_next` 在最近 N 次 service 中有增长；COMPLETE/CANCELLED/STOPPED 一律不算 held。

**H03：`complete_action` 的 NONE 分支是假完成**

```c
if (group >= groups_total) ++s_done_skip_range;
if (group == TRIGGER_T2_RUNTIME_GROUP_NONE) ++s_done_skip_none;
...
if (group < groups_total) { ... advance ... }
/* group==NONE 时直接落到这里 */
++s_action_finish_count;
s_finish_group_last = group;          /* 255 */
...
s_state.action_completed_mask |= bit;
```

`GROUP_NONE=0xFF >= groups_total`，所以 `group<groups_total` 为假，finish 无条件执行。`s_done_skip_none` 只是一个计数，不是失败；这与“异常计为具名失败”相反。正确做法：在 NONE/越界时**早退并 action_fail**（或返回“lost”给调用方），不增加 finish/executed/event。

**H04：out[39] 覆盖**

```c
out[39]=s_armed_idle_released;   /* :649 */
...
out[39]=s_armed_owner;           /* :655 覆盖 */
```

与 37/38 完全同类。需要把 GWDIAG 扩到 43 字段（SCPI handler 的 `fields[42]`/循环 42 也要改），或把 retired 放到真正空闲的槽位；并加字段顺序测试。`cont_hit/miss` 仍应恢复。

### 12.4 测试缺口

- 当前 `test_trigger_t2_runtime.py` + `test_sync_io_gateway.py`：**88 passed**。
- 但没有用例覆盖：
  1. `plan_count=16, pending=1, state=ARMED`，连续 65 次 service 无进展 → 期望退休，当前不会；
  2. `plan_count=16, pending=0, state=ARMED` → 期望退休，当前不会（`!plan_count` 挡住）；
  3. `complete_action` 收到 `GROUP_NONE` → 期望**不增加 finish/executed**、记具名失败；当前会增加；
  4. GWDIAG out[39] 的退休计数可读；
  5. `window_held` 在 ARMED/CANCELLED + plan_pending 时不得为真。
- 这五个用例都能用现有 mock 加，不必上板；先让它们红，再改代码。

### 12.5 建议

1. **把退休改成 progress watchdog**：不看 pending/plan_count，连续 N 次 service 的 `(complete, plan_next, pending_receipt)` 不变即触发；触发时 `safe_stop`、清 plan、置 CANCELLED，并让运行期走 terminal→具名失败/释放。
2. **收窄 `window_held`**：只接受 `state==ARMED` 且最近有 progress 的窗口；COMPLETE/CANCELLED/STOPPED/IDLE 一律不算 held。必要时加 plan identity（version/group）。
3. **修 H03**：NONE/越界早退，`action_fail(TRIGGER_T2_ACTION_FAIL_BACKEND)` 或新增 `GROUP_LOST`，不 finish、不发 completed 事件；`s_done_skip_none` 只作诊断。
4. **修 H04/H05**：GWDIAG 扩字段并恢复 cont_hit/miss；退休路径统一 `safe_stop`+清 plan。
5. **先跑上述五个负测**，再上板；否则台架“不再停”可能只是 H03 的假完成把停摆掩盖了。

### 12.6 边界

- H01/H02/H03/H04 都是代码路径确定的事实；H05/H06 为读码推断。
- 本节不否定 11.7 的台架观察：t≈2 s 停摆、`svc_wait` 增长、失败计数全 0。正是这些数据说明退休条件必须覆盖 pending=1/plan_count>0。
- 若下一轮台架看到 `finished` 上升但 `executed` 与 `groups×positions` 仍不符，优先怀疑 H03 的假完成。

## 十三、最新修复与 15:59 台架复核（2026-10-20 傍晚）

> 对 14:26/14:56 后的工作区做复审：`complete_action` 的 NONE 分支、`window_held` 的 ARMED 校验、网关 progress watchdog、GWDIAG 扩字段，以及 15:53–15:59 的新台架。读数为快照，非事实源。

### 13.1 先确认已修好的

- **H03 假完成**：`complete_action()` 在 `group==NONE || group>=groups_total` 时已早退（`trigger_t2_runtime.c:1190-1207`），清窗口簿记、`++s_finish_lost`、`action_fail(BACKEND)`，不再增加 finish/completed 事件。方向正确。
- **H01 主停摆**：网关新增 progress fingerprint watchdog（`sync_io_gateway.c:426-448`），`busy = plan_count || pending`，指纹看 `complete/plan_next/pending_receipt/trigger_count/state`，连续 256 次不变则 `safe_stop()`、清 plan、置 CANCELLED。覆盖了 “pending=1 但回执不来” 的场景。
- **H02 窗口在泵**：`window_held` 现在要求 `backend_state == ARMED`（`trigger_t2_runtime.c:1585-1588`），COMPLETE/CANCELLED/UNCERTAIN/IDLE 不再算 held。
- **H04 诊断覆盖**：GWDIAG 扩到 47 字段，`out[39]=progress_retired`，owner 移到 42，stale 43，state/seq/done_seq 44–46；SCPI 的 `fields[47]`/循环 47 已同步。
- 主机：`test_trigger_t2_runtime.py` + `test_sync_io_gateway.py` **89 passed**（新增 `deadline_stall`）。

### 13.2 仍未闭合的阻塞

| ID | 严重度 | 位置/证据 | 发现 | 后果 |
|---|---|---|---|---|
| SEQ-H08 | 阻塞 | 15:54–15:59 日志；`sync_io_gateway.c:426-448` | 主停摆已从“ARMED+pending”变成“**UNCERTAIN + plan0 + pending0**”：slot2 连续 20 次采样 `state=4(UNCERTAIN) svc_wait=546 svc_done=181 late=170 plan=0 pending=0 sm=0 prog_retired=0 prog_stale=0`。`busy=(plan_count||pending)` 为假，watchdog 根本不跑；`prog_retired=0` 证明它不会退休 | 若该槽不是 `s_armed_index`，运行期 terminal 分支不会处理；窗口槽保持 frozen，新位置不再准入（本轮 gateway 只 entered 14） |
| SEQ-H09 | 高 | 日志 `snapshot page(s) missing for slot 0..3: window`；`t2_workflow.py:3369` | `_min_fields["window"]=8`，但 `ACTion:WINdow?` 只有 7 个字段；所有 window 页被判 “missing”，状态文件里 `window: [""]` | 窗口五总量（entered/frozen/w_executed/abandoned）全部丢失；报告无法对账，H08 的 frozen 槽也看不到 |
| SEQ-H10 | 高 | `sequence_workflow.py:253-255`；15:59 状态 | 判据仍 `positions = args.positions if args.positions else finished`，即 `expected = groups × finished`。本轮 SWITCH_B 只跑了 121 个位置（124 s 采样），仍判 `ok:true` | 截断/部分扫描可以“通过”；这不是全圈验收，不能拿来证明修复成功 |
| SEQ-H11 | 中 | `tests/python/test_sync_io_gateway.py` 新 `deadline_stall` | 测试用 `uint32_t f[44]`，但 `sync_io_gateway_realtime_diag()` 现在写 `out[0..46]` | 栈缓冲区越界 3 个字；测试能过是未定义行为，不是证据 |
| SEQ-H12 | 中 | `sync_io_gateway.c:654-673`；SCPI `:577-585` | GWDIAG 字段从 42 扩到 47，schema 仍 `0xDA02`；owner 从 39 移到 42；无字段顺序测试/版本号 | 旧读者按 39 读 owner、按 42 字段截断，会误读；`cont_hit/miss` 仍无读取点 |
| SEQ-H13 | 中 | `sync_io_gateway.c:426-448` 与 `:450+` | progress watchdog 在**收 RX 之前**执行；若第 256 次 stale 恰好有一笔迟到回执在 FIFO 里，会先 `safe_stop` 清掉它 | 合法但晚到的完成被误判为停摆，产生 BACKEND 失败 |
| SEQ-H14 | 低 | `GATEWAY_PROGRESS_STALL_PASSES=256` | 阈值是**服务次数**，不是时间；冻结 lead 实测 144–192 ms，若 service 周期小于约 0.75 ms，256 次可能早于合法首组到期 | 合法首组被提前退休；应改用 tick 时间或大于最大 lead 的阈值 |

### 13.3 15:59 台架读数

```text
SWITCH_A: executed 1888 = 16×118, finished 118, entered 129, late 1, backend 0
SWITCH_B: executed 1936 = 16×121, finished 121, entered 131, backend 0, 判定 ok:true
GATEWAY : executed   68 = 16×4,   finished   4, entered  14, backend 0
window  : 所有角色 [""]（H09 丢弃了 7 字段页）
chain   : 276 s，采样 124.2 s（不是 361 位置全圈）
```

- `SWITCH_B ok:true` 不代表全圈通过：它只覆盖 121 个位置，判据用 `finished` 当分母（H10）。
- GATEWAY 只 entered 14、finished 4；GWDIAG 的 slot2 是 `UNCERTAIN/plan0/pending0`，watchdog 不跑（H08），所以“修好停摆”的结论**还没有被这轮数据证明**。
- `window` 全空（H09），本轮无法看到 frozen/w_executed/abandoned，也无法判断 H08 的 frozen 槽是否就是准入停止的原因。

### 13.4 建议

1. **先修 H09**：`window` 最小字段改 7（或每页一个常量），让窗口五总量重新可读；这是验证 H08 的前提。
2. **修 H08**：progress watchdog 不能只看 `plan_count||pending`；对 `state==UNCERTAIN/CANCELLED` 且无 plan/pending 的槽，也要有“冻结槽退休”路径（按 `s_window_staged`/`s_slot_armed_once` 或 per-slot backend 记录），否则 frozen 槽永远不释放。
3. **修 H10**：验收必须用配置的扫描位置数（361）或要求 `--positions`，并断言 `finished == expected_positions`；不能用 `finished` 当分母。
4. **修 H11**：测试数组改 `f[47]`/常量 `SYNC_IO_GATEWAY_DIAG_FIELDS`；这是未定义行为。
5. **修 H13/H14**：watchdog 先检查 RX FIFO/回执，再决定退休；阈值改 tick 时间，覆盖 144–192 ms lead。
6. **H12**：GWDIAG 升 schema/加字段顺序测试，恢复 `cont_hit/miss` 可读。
7. **重跑一轮 361 位置全圈**，记录 window 五总量 + GWDIAG state/seq/retired；在 H08/H09/H10 修好前，不要把 15:59 的 SWITCH_B ok 当作通过。

### 13.5 边界

- H08 的“frozen 槽不再准入”是读日志推断；H09/H10/H11/H12 是代码/工具确定事实，H13/H14 为代码路径推断。
- 本轮 89 passed 不覆盖 H08 的 UNCERTAIN/no-plan 场景，也不覆盖 window 页 7 字段与 GWDIAG 47 字段的解析。
- 15:59 采样只有 124 s，不是全圈；任何百分比/通过结论都不成立。

## 十四、基础模型（node_sequence）拆解与网关试点方案（2026-10-20）

> 依据：`origin/feature/node-sequence-reservation-trigger` 上的 `sync_io_sequence.{c,h}` + `sync_io_sequence.pio`（1322/154/149 行）。本节只做拆解与适配设计，不动代码。

### 14.1 一句话

基础模型 = **一份器件配置 + 一条计划 + 四个 PIO 状态机**：外部脉冲（或内部 FIFO）驱动"序列切换"，每步写输出、计数 settle/pulse、回一条回执，可选地回一条 READY。**网关不是独立执行器，而是配置里的 `gateway_enabled`**。

### 14.2 PIO 程序（4 个，块级职责）

| 程序 | 指令块 | 职责 |
|---|---|---|
| `sequence_ingress` | `inactive/active/observed/decide/admitted/request` | 输入边沿 → 采样状态 → 判忙 → IRQ4/5 请求一次推进 |
| `sequence_finite_ingress` | 同上 + `quota/parked` | 有限配额（`max_steps`）：Y 递减，用完永久停 |
| `sequence_executor` | `admission/waiting/writing/written/settling/status_prepare/status_mode/pulsing/status_active/completed` | 每步吃 3 字 {packed values, settle, pulse}；写 `pins`、发回执、可切 status 模式（LEVEL/PULSE/NONE） |
| `sequence_gateway` | `pull/grant/pulsing` | **一发**：等 IRQ6 授权 → 拉高 → 计数 → 拉低 → 回执 |
| `sequence_ready_counter` | `grant/inactive/active/observed/counted` | **一次完整 READY 边沿**：授权后等 0→1，计数并回执 |
| `sequence_counter` | `inactive/active/observed/counted` | 转台累计边沿计数（X 递减，noblock 推） |

### 14.3 状态块（实现侧）

| 块 | 位置 | 职责 |
|---|---|---|
| `reserve/release`、`outputs_available` | 266/281/251 | Core0 预约；与 `resource_arbiter` 互斥 |
| persona hooks `load/arm/start/cleanup` | 332–609 | 申请 PIO0 SM/DMA、装载程序、`prime_initial_state`、起 SM |
| `config_valid`、`build_plan`、`arm_plan` | 629–764 | 校验 + 把 256×3 字计划推入 FIFO/DMA |
| `receive_word`/`drain_receipts`/`produced_receipts` | 772–875 | 回执 DMA 与背压 |
| `account_input`/`account_counter*` | 900–962 | 边沿入账、计数器阈值与故障 |
| `service` | 962 | 一次推进 + 故障聚合 |
| `software_step(_prepared)` | 1029/1036 | **内部 FIFO 驱动**（无外部脉冲时等价于一次边沿） |
| `gateway_service/start/cancel/fire/ready` | 1082–1174 | 网关一次性脉冲与 READY 回执；READY 只是回执，不请求推进 |
| `pause/stop/finish_ingress` | 1267/1314/1234 | 收尾、停机、清owned |
| `set_switch/get_switch` | 177/197 | SP8T 掷向（独立维护通道） |

### 14.4 与当前分布式项目的差异（必须适配的四处）

1. **时间轴**：模型内部是 PIO 4 ns tick + 整 us；当前 T2 用 TDMA/`vdc_timer1` 绝对 tick。适配点：在 `arm_plan` 入口把绝对 tick 折算成 (settle_us, pulse_us)，或引入分支里的 `trigger_reservation_clock` 做单一换算处。
2. **计划来源**：模型假设本机 FIFO 承载计划；当前由 origin 解算、REFMEM 分发。适配点：保留分发，把每组的 `deadline_tick + argument` 转成 3 字项；`s_group_next` 一类的软件游标**不再需要**。
3. **SCPI/页面**：分支用 `scpi_sequence_*`，当前用 `trigger_t2_*` 与 `T2?/WHY?/GWDIAG?`。试点期只映射最小集（arm / fire / ready / status / stop / switch），并把 `sequence_snapshot_t` 的关键字段并入现有页面，保证 `sequence_workflow.py` 仍能读。
4. **资源**：模型要 4 个 SM + 3 条 DMA；当前 switch 已占 2 SM。试点期按板分配，避免同板双栈。

### 14.5 网关试点（最小切片，只动 GATEWAY 板）

- 配置：`gateway_enabled=true`、`gateway_pulse_us=8000`、`gateway_input_channel=0`（软件 READY）、`status_mode=NONE`、`pulse_us` 留 0；计划 = **2 步**（SP2T 的 A/B）。
- 推进：`software_step()`（内部 FIFO）驱动，不接外部脉冲；`gateway_fire()` 产生一次 8 ms 脉冲，`gateway_ready()` 完成一次 READY。
- 并行：SWITCH_A/B 与 COUNTER 继续跑现有 T2 运行时（异板，互不干扰）。
- 观测：`snapshot.gateway_trigger_count / gateway_ready_count / gateway_waiting / gateway_pulse_busy / fault`，与现有 `GWDIAG?` 对照。
- 通过判据：`executed == 2 × positions`、`fault=0`、无 `busy_rejected`；位置吞吐 ≥ 当前 SP2T 实测（37 位置/40 s）。

### 14.6 依据与风险

- **依据**：本轮 SP2T 试点（网关 16 组 → 2 组）已把网关从 1–11 个位置提到 **37 个位置 / 74 组**，`no_plan/late/backend` 全 0——说明"少交接"是正确方向，而基础模型正是把它做成硬件序列。
- **风险**：PIO0 SM/DMA 配额；tick 换算精度；双栈共存；`sync_io_sequence.c` 来自另一分支（含资源仲裁/人设接口），需按当前 `sync_io_pio0_runtime` 的 hooks 重新对接。
- **不做的事**：不整分支合并；不改 SWITCH 的执行器；不在同板并行两套执行器。

## 十五、基础模型/pilot 实现独立复核（2026-10-20 傍晚）

> 复核对象是当前工作区里已经落地的 `sync_io_sequence.{c,h,pio}`（1392 行）+ `trigger_sequence_pilot.{c,h}` + `TRIGGER:REServation:T2:SEQ:PILot` 集成。结论：**这是可评审的实现，但按当前代码，pilot 连第一步都走不通**；§14 的适配清单里最关键的 persona 对接还没有完成。

### 15.1 阻塞级发现

| ID | 严重度 | 位置 | 发现 |
|---|---|---|---|
| P01 | 阻塞 | `sync_io_persona_resources.h:69-70` vs `sync_io_persona_resources.c:65-245` | 新增了 `SYNC_IO_PERSONA_ID_SEQUENCE` / `_SEQUENCE_COUNTER`，但 `s_sync_io_personas[]` **没有对应 descriptor**。`sync_io_persona_catalog_valid()` 要求 descriptor 数 == ID_COUNT-1，直接失败；`sync_io_sequence_arm_plan()` 的 `sync_io_persona_manager_claim(..., SYNC_IO_PERSONA_ID_SEQUENCE, ...)` 找不到人设，arm 必失败（`FAULT_RESOURCE`）。主机 `test_switch_reference_resource_budget` 已实测红。 |
| P02 | 阻塞 | `sync_io_sequence.c:1036-1053`、`:1156-1164`；`trigger_sequence_pilot.c:163-167` | pilot 先 `software_step()` 再 `gateway_fire()`；但 `software_step_prepared()` 在提交时把 `status.ready=false`（:1049），而 `gateway_fire()` 第一条就要求 `status.ready`（:1161）。所以 **fire 必返回 false**，pilot 进 DONE/errors。反过来先 fire 也不行：`software_step_prepared()` 又拒绝 `gateway_waiting/gateway_pulse_busy`（:1042-1043）。当前 API 下没有一个顺序能让两者在同一轮都成功。 |
| P03 | 阻塞 | `trigger_sequence_pilot.c:169-177`；`sync_io_sequence.c:150-165` | WAITING 只调用 `sync_io_sequence_get_snapshot()`，不调用 `sync_io_sequence_service()`。fire 后 `gateway_pulse_busy=true`，只有 `gateway_service()`（在 `sync_io_sequence_service()` 内）会把它清掉；而 `sync_io_sequence_service()` 在仓库里**没有其他周期调用点**（`sync_trigger_service` / T2 runtime / app_tasks 都没有）。因此即使 P02 修好，WAITING 也会永远看到 busy，pilot 死锁。 |
| P04 | 高 | `trigger_sequence_pilot.c:174-175` | `(void)sync_io_sequence_gateway_ready();` 忽略返回值，随后无条件 `++s_index`。如果 READY 没完成（不在 waiting、fault、pulse 未收），pilot 仍伪造一个完成步，`steps_done` 与 `gateway_ready_count` 不一致。修 P03 时必须同时检查返回值。 |
| P05 | 高（外部 READY） | `sync_io_sequence.pio:120-149`；`sync_io_sequence.c:302,1143` | `sequence_gateway` / `sequence_ready_counter` 都 `wait 1 irq 6`，但 C 侧只有 `pio_interrupt_clear(GATEWAY_GRANT_IRQ)`，**从未 set/force IRQ6**。软件 READY（`gateway_input_channel=0`）把 grant 补丁成 NOP[2]，所以 pilot 不受影响；一旦用 1..4 外部 READY，grant 永远不来，外部网关/READY 路径是死的。 |
| P06 | 中 | `trigger_sequence_pilot.c:92-103,179-189` | `s_index`/`s_errors` 标注为 Core1-private，却在 `trigger_sequence_pilot_request()`（Core0/SCPI）里直接写。请求只检查 `req_sequence == done_sequence`；而 Core1 在 DONE 分支先写 `s_done_sequence`（:182），再置 IDLE、最后 `pilot_publish()`（:189）。Core0 可在 Core1 还没 publish 完时通过 busy 检查并改写 `s_index/s_errors`，与 Core1 的旧 run 发布竞争。 |
| P07 | 高（流程） | 新文件 `sync_io_sequence.*`、`trigger_sequence_pilot.*` | 新引擎 61KB + PIO 没有对应的 host 测试；现有 89 个 T2/gateway 用例覆盖不到它。`test_switch_reference_resource_budget` 因 P01 已经红；新代码还未构建、未上板，`16:54` 的 `sequence_flow.json` 仍是旧 switch/gateway 路径，不是 pilot 证据。 |

### 15.2 代码路径

**P02：step 清 ready，fire 又要求 ready**

```c
/* sync_io_sequence.c:1049 */
s_sequence.status.ready = false;      /* software_step_prepared 提交后 */

/* sync_io_sequence.c:1161 */
if (!s_sequence.status.ready || s_sequence.status.gateway_waiting ||
    s_sequence.status.gateway_pulse_busy || ...) return false;

/* trigger_sequence_pilot.c:165-166 */
if (!sync_io_sequence_software_step()) { ... }
if (!sync_io_sequence_gateway_fire()) { ... }   /* 必失败 */
```

这不是参数问题，是 pilot 编排与模型 API 的契约冲突：需要明确“step 完成/ready 重新置位后再 fire”，或把 gateway 触发做成与 step 分离的独立轮次；当前同一轮 step→fire 不成立。

**P03：WAITING 没有 drive service**

```c
case PILOT_STATE_WAITING: {
    sync_io_sequence_snapshot_t snap;
    sync_io_sequence_get_snapshot(&snap);
    if (snap.gateway_pulse_busy) break;          /* 没人会清它 */
    (void)sync_io_sequence_gateway_ready();      /* 只有这里才间接 service */
    ++s_index;
    ...
}
```

`gateway_pulse_busy` 的清除在 `gateway_service()`，而它只由 `sync_io_sequence_service()` 调用；pilot 在 busy 时直接 break，形成自锁。最小修法：WAITING 每轮先 `sync_io_sequence_service()`（或把 service 挂到 `sync_trigger_service`），再看 snapshot；然后检查 `gateway_ready()` 返回值。

**P01：catalog 缺口**

```c
/* sync_io_persona_resources.h:69-70 新增 ID */
SYNC_IO_PERSONA_ID_SEQUENCE,
SYNC_IO_PERSONA_ID_SEQUENCE_COUNTER,

/* sync_io_persona_resources.c:65-245 的数组没有这两项 */
```

测试实测：

```text
FAILED tests/python/test_sync_io_switch.py::test_switch_reference_resource_budget
Assertion failed: sync_io_persona_catalog_valid()
```

在补 descriptor 之前，`sync_io_sequence_arm_plan()` 的 manager claim 不可能成功，pilot 的 `errors` 只会增长。

### 15.3 建议顺序

1. **先补 P01**：为 `SEQUENCE` / `SEQUENCE_COUNTER` 加 descriptor（instruction_words=31、SM mask 覆盖 ingress/executor/counter，counter 还要 SM0/capture lease、5 路 DMA、SMA GPIO 读/写/safe-low/restore 掩码），并让 `sync_io_persona_catalog_valid()` 恢复。先跑 `test_switch_reference_resource_budget`。
2. **修 P02/P03**：把 pilot 的 step→fire→wait 改成模型允许的顺序；WAITING 每轮 drive `sync_io_sequence_service()`；`gateway_ready()` 返回值必须检查。
3. **修 P05**：外部 READY 要么在 C 侧 grant IRQ6（明确谁在什么边沿 set），要么在配置校验里暂时拒绝 `gateway_input_channel!=0`，不要留一条永远 wait 的路径。
4. **P06**：把 `s_index/s_errors` 的复位移到 Core1 的 IDLE/ARMING，或把命令放进独立 command slot；不要用 `done_sequence` 作为唯一“Core1 已完全退出”的判据。
5. **P07**：给新引擎加 host 测试（plan/build、step/fire/ready、DMA 回执、stop/cleanup、persona claim），再构建、再上板；在 pilot 通过前不要把 §14 的 37 位置/40 s 当作新模型证据（那是旧执行器的试点）。

### 15.4 边界

- P01/P02/P03/P04/P05 是代码路径确定；P06 是跨核时序推断；P07 是流程事实。
- 本节不评价新模型 PIO 指令级时序（未上板、未构建）；只评价“能否进入第一步”和集成契约。
- §14 的方案方向（硬件序列、少交接）仍然成立；问题在落地代码的 persona/编排/服务调用，不在方案本身。

### 14.7 网关试点通过（2026-10-20 19:45，build 20261020…）

统一模型（`sync_io_sequence`）已在当前基线落地并驱动网关输出，实测三组配置：

| `T2:SEQ:PILOT` | steps_done | trigger | ready | written/completed/accepted | pulse_us | errors/fault |
|---|---|---|---|---|---|---|
| 2, 8000 | 2 | 2 | 2 | 2/2/2 | 8000 | 0/0 |
| 2, 4000 | 2 | 2 | 2 | 2/2/2 | 4000 | 0/0 |
| 1, 2000 | 1 | 1 | 1 | 1/1/1 | 2000 | 0/0 |

移植与适配清单（每一处都是"当前树与分支语义不同"导致的）：

1. 三个接口：`SYNC_IO_SEQUENCE_INSTRUCTION_WORDS`、两个 persona id（追加，不改旧值）、`RESOURCE_ARBITER_RESOURCE_SMA_GPIO`；
2. 两个 SM 租借 helper：`sync_io_core_capture_sm_lease/restore`，且"空闲即可"，不要求预 claim；
3. 两个 persona 描述符（`SEQUENCE` / `SEQUENCE_COUNTER`），否则 `persona_manager_claim` 必然拒绝；
4. 四处语义修正：`model_output_active`（空闲恒真→纯脉冲判据）、`seq_step/enc_count` 的"运行中"改为 SM 实测使能、软件 READY 下的 fire 前置按模型契约保留（不自改语义）、命令槽代际消费。

HAOFV 归属（L120/L150/L151）：SCPI 只写命令槽；core0 域服务做软件预约/释放；core1 独占 arm/step/fire/ready/stop；跨核代际 + seqlock。

边界：这是**试点命令**，不参与位置验收判据；把 GATEWAY 的位置流正式切到该模型是下一步部署工作。

### 14.9 开关迁移的设计结论（2026-10-21，阻塞项）

**代码事实**（`sync_io_switch.c`）：

```c
static void split_groups(void)            /* 输出掩码 -> 连续段，每段一个 SM */
{
    g->sm = s_switch.group_count ? SYNC_IO_SWITCH_SECOND_SM : SYNC_IO_SWITCH_FIRST_SM;
    g->output_base_pin = BOARD_SYNC_OUTPUT_BASE_PIN + i;
    do { ++g->output_count; ++i; } while (连续);
}
pio_sm_put(..., FIRST_SM,  s_switch.groups[0].code);   /* 各自截止时刻 */
pio_sm_put(..., SECOND_SM, s_switch.groups[1].code);
```

即：开关是"**最多两条路由、每条一个 SM、在各自截止时刻写入自己那段引脚**"的器件；码字来自部署绑定
（设备配置），不是每组的脉冲宽度。

**为什么现在的模型表达不了**：`sync_io_sequence` 的执行器只有 **1 个 SM**，`sm_config_set_out_pins(BASE, 4)`
一次写全部四位。若用单 SM 按组写整字，两条路由会在**同一个时刻**一起变化，而开关要求它们**相隔一个组步长**
各自变化——引脚时序不等价。

**修正（同日复核）**：每条路由的 SM 只驱动自己的引脚段并**保持电平**，所以"单 SM 每步写**合成字**"
（本条路由的新值 + 其余路由的上一值）在引脚时序上与原实现等价——**不需要多执行器**。
真正的缺口是**数据**：模型的计划值需要"每步的完整 4 位码字"，而运行时当前只拿到脉冲宽度
（`stage_sequence(base, pulses, deltas, count)`），码字在 `sync_io_switch` 内部由部署绑定给出、从未上送。

按这个修正，迁移缩小为三件事：

1. 把部署绑定里的开关**码字 + 输出掩码切分**上送（SCPI/模板层新增字段，或由 `switch_load` 导出）；
2. 运行时按路由切分把每步合成成完整 4 位值，作为模型计划值；
3. `MODE 2` 两次 361（`16 × 361`、零失败）通过后删旧开关路径。

**（原方案，保留作为备选）迁移所需的最小模型扩展**（建议实现顺序）：

1. `sync_io_sequence_config_t` 增加 `route_mask` / `route_count`（≤2）以及每路由的 `base/count`；
2. `arm_hardware` 为每条路由分配一个 executor SM，计划项增加"路由号"，各 SM 只写自己的引脚段；
3. 回执按路由区分（复用现成的 `(logical<<SHIFT)` 字段），完成判定 = 所有路由当步回执齐；
4. 运行时侧：把部署绑定里的 `code` 与输出掩码传给模型（当前 T2 计划只携带脉冲宽度，拿不到码字）；
5. 验收：`T2:SEQ:MODE 2` 跑两次 361，`groups = 16 × 361` 且零失败，才允许删旧开关路径。

**在此之前**：`T2:SEQ:MODE 1`（网关模型化、开关旧路径）是已验证配置，连续两次严格口径 PASS。

**同日实测补充（mode 2 的不可回退性）**：

- `MODE 2` 下开关报 `fault=2 / fault_detail=8`（`outputs_available()` 为假）：旧开关 persona 仍持有引脚，
  仅"跳过装载"会让"已装载"标志与实际硬件不一致，回退 mode 1 后旧路径不再重载。
- 更关键的是**引脚状态残留**：mode 2 的实验会把 SMA 引脚留在非 SIO 功能上，之后即使回到 mode 1，
  模型的 `reserve()` 也会因 `outputs_available()` 失败而永久被拒（`fault_detail=8`、`reserved=0`），
  **只能靠重新刷写/复位恢复**。
- 因此 mode 2 的切换必须实现为**原子转换**：先 `trigger_switch_unload`（或 persona STOP）让出引脚 →
  再 `sync_io_sequence_reserve`；回退时反向执行并恢复引脚功能。在做完这一步之前，mode 2 不具备可回退性，
  不应在验收链路上启用。
- 恢复验证：重新刷写后 mode 1 再次 `SWITCH_A 5776/5776、GATEWAY 722/722、SWITCH_B 5776/5776`，
  零失败，`verdict OK`（当晚第 3 次严格口径 PASS，凭证已生成）。

**同日补充（数据链路已通 + 顺序问题）**：

- 开关 `stage_sequence(base, codes, delta, count)` 的参数名就是 **`codes`**：运行时的
  `plan->group_argument[k]` **本来就是每组码字**，所以"码字上送"这一步其实不需要新增接口——
  计划值直接取 `group_argument[k]` 即可（网关仍取 0，其触发由 `gateway_fire` 产生）。
- 已新增 `trigger_switch_force_unload()`（Core1）并让试点在 mode 2 下 arm 前先让出引脚，
  但实测仍 `fault_detail=8`：**顺序不对**——core0 的 `reserve()` 先于 core1 的卸载执行，
  于是每次 reserve 都撞在旧 persona 上。正确顺序应是：core0 发出"让出"意图 → core1 卸载 →
  core0 再 reserve（三段握手），或在进入 mode 2 时就完成卸载，而不是在 arm 时。
- 恢复验证：重新刷写后 mode 1 再次 `5776/722/5776` 零失败，`verdict OK`（当晚第 4 次严格口径 PASS）。

**日间补充（2026-10-21 上午，断电重启后）**：

1. **网关统一模型恢复并双跑验证**：断电重启后，`MODE 1` 连续两次 361（第二次未重设 MODE）：
   `SWITCH_A 5776 / GATEWAY 722 / SWITCH_B 5776`，其中第二次为全 PASS。这验证了当夜新加的
   **"预约丢失后自动重约"**修复（`sync_io_sequence_is_reserved()` + core0 重约）——此前第二次运行起必然全崩。
2. **`t2:STOP` 会释放模型预约**，而 core0 的 `s_core0_reserved` 标志不会自动清零；已按"每次检查模型
   是否仍持有预约"修正。
3. **开关的模型钩子从未被进入**：`MODE 2` 下开关 `pos_count=0`、`arm_branch=2`（窗口 legacy），
   说明开关的 arm 走的是窗口 legacy 路径，两处 `switch_enabled()` 钩子条件均未命中；
   `backend` 全部计入 `BACKEND`，因为 legacy `stage_sequence` 在模型持有引脚后被拒。
4. **诊断通道需要固定字段页**：把 `arm_branch` 追加在 `WHY?` 尾部不可靠（该页尾部字段数随 solve-stats
   变化），应新增单字段查询（如 `T2:ACTion:ARMBranch?`）返回 `branch | kind<<8 | switch_en<<16 | pos_en<<17`。
5. **mode 2 实验会再次泄漏网关的状态**：实验后即使切回 mode 1，网关也报 `backend=361`
   （模型仍 armed、预约被拒），需重启才恢复。因此 **mode 2 的调试必须每轮重启**，或先把"交接"做成
   core1 主动 stop + core0 重约的完整闭环。

**日间结论（2026-10-21 下午）：统一模型侧三处修复 + 开关迁移的剩余点**

按"修统一模型、再适配不同角色"的方向，改的是模型本身（不是给网关打补丁）：

1. **陈旧调度相位**：`sync_io_core_schedule_clear_stale()`——操作结束后残留的相位会让
   `outputs_available()` 永远为假；无运行时清回 IDLE。
2. **输出所有权**：`sync_io_sequence_reserve()` 取得仲裁后主动**接管并规整引脚**（SIO/输出/低电平），
   而不是因"引脚被占"就报 RESOURCE。这就是 §14.9 所说的"模型作为所有者"。
3. **保留相位不算阻塞**：`sync_io_core_schedule_blocks_outputs()` 把 `*_RESERVED` 视为合法保留态
   （开关板在模型驱动期间就持有 `SWITCH_RESERVED`），只有 RUNNING/LEGACY 才阻塞；
   另外 ARM 时改用"已持有预约"口径（模型自己会把输出拉高，不能要求"全低"）。

**验证**：修复后网关连续两次 361 全 PASS（第二次未重设 MODE），
`SWITCH_A 5776 / GATEWAY 722 / SWITCH_B 5776`，零失败，凭证已生成。

**开关剩余**：`MODE 2` 下模型已能 arm 到 `pos_count=16`、`pos_stage=3`，但模型的 persona claim 仍失败
（`fault_detail=9`）：SEQUENCE 描述符在开关板上申请不到资源，需要**按角色给开关板一份自己的
SEQUENCE 资源描述（SM/DMA 布局）**——即"适配不同角色"的最后一处配置，而不是再改执行逻辑。

## 十六、网关模型通过后的独立复核：开关 mode 2 与时序判据（2026-10-21）

> 复核对象是 2026-10-21 15:12 的工作区（persona 描述符已补、pilot step/fire/WAITING 已修、`T2:SEQ:MODE` 已接入），以及 15:11 的台架快照。先确认成绩，再指出尚未闭合的阻塞。

### 16.1 先确认：网关 mode 1 已经能跑

- 进度 022 记录：`MODE 1` 下连续三轮 `SWITCH_A 361/5776、GATEWAY 361/722、SWITCH_B 361/5776`，`no_plan/late/backend=0`，凭证已生成；这与 15:11 的快照方向一致（GATEWAY `executed=312=2×156`、`finished=156`、`entered=159`，`no_plan/late=0`）。
- 15:11 的开关 `executed=0 / entered=4 / backend=1` 与进度 022 的 mode 2 `entered=126 / executed=0 / backend=124` 一致：**失败集中在开关模型路径**，不是网关模型路径。
- persona `SEQUENCE`/`SEQUENCE_COUNTER` 已补（`sync_io_persona_resources.c:260-302`），`test_switch_reference_resource_budget` 已恢复通过。

### 16.2 开关 mode 2 的代码级阻塞

| ID | 严重度 | 位置 | 发现 |
|---|---|---|---|
| S01 | 阻塞 | `trigger_sequence_pilot.c:389-425` | `position_service()` **无条件**调用 `sync_io_sequence_gateway_fire()/gateway_ready()`，没有保存 `gateway_role`。开关的 config 是 `gateway_enabled=false`，`gateway_fire()` 第一条就返回 false（`sync_io_sequence.c:1156-1164`），所以开关位置永远停在 stage 3，不会走 step 完成/下一组。 |
| S02 | 阻塞 | `trigger_t2_runtime.c:1691-1699`；`§14.9` | 运行时把 `plan->group_argument[k]` 当作开关的**引脚码**传给模型；但按 §14.9，T2 的 argument 是脉冲宽度/时序参数，开关码字在 `sync_io_switch` 的部署绑定里，**从未上送**。结果要么 `build_plan` 的 `value ⊆ output_mask` 失败（CONFIG），要么写入错误码字。 |
| S03 | 高 | `trigger_sequence_pilot.c:379` | `if (!sync_io_sequence_is_armed() && !arm_plan(...))`：模型一旦 armed，后续位置的 `values/count/pulse_us` 全部被忽略。网关两组的 values 恒 0，暂时看不出来；开关每个位置的 16 组码字/组数不同，会被第一位置的计划覆盖。 |
| S04 | 高 | `sync_io_sequence.c:677-681`；`trigger_sequence_pilot.c:381` | `build_plan` 把计划旋转一位：`initial_output=values[0]`，计划项从 `values[1]` 开始；而 pilot 仍执行 `count` 步。对 count 组序列，首组已 prime，后面只需要 `count-1` 次推进；执行 `count` 次会多走一步（wraps 到 values[0]），开关 16 组的组序/时序因此错位。网关 values 全 0 掩盖了它。 |
| S05 | 阻塞（时序） | `trigger_t2_runtime.c:1412-1422` | 模型分支在 `service_deadlines()` 里直接 `return`，绕过了 legacy 的 `deadline_ok/late` 检查；`position_arm()` 只接收 `pulse_us/step_us`，**没有接收 `plan->deadline_tick`/组 delta**，`software_step()` 是立即推进。也就是说：361 PASS 是**计数/完成 PASS**，`late=0` 在模型路径上是**未被测量**，不是“时序正确”。 |
| S06 | 高 | `trigger_sequence_pilot.c:304-325`；SCPI `:870-877` | `T2:SEQ:MODE` 是 SCPI/Core0 调用的，但 `trigger_sequence_pilot_position_mode()` 直接调用 `sync_io_sequence_stop()`（Core1-only，`sync_io_sequence.c:1314` 首行 `get_core_num()!=1` 直接 return）。Core0 上这是 no-op；随后 `release()` 又因模型仍 armed 而 no-op。模式切回 0/1 时模型可能仍持有 SMA/persona，旧路径再申请就冲突。 |
| S07 | 中 | `sync_io_sequence.c:33,355,1212` | 外部 READY 的 `wait 1 irq 6` 仍没有 C 侧 set/force（只有 clear）；软件 READY（input=0）把 grant 补丁成 NOP[2] 所以 mode1 不受影响，但 1..4 外部 READY 仍是死路径。 |
| S08 | 中 | `trigger_sequence_pilot.c:117-120` | `s_index/s_errors` 仍由 Core0 的 `request()` 直接写；Core1 在 DONE 先写 `done_sequence` 再置 IDLE/publish，Core0 可在 publish 前通过 busy 检查并改写，旧 run 的状态发布会被污染。 |
| S09 | 中（证据） | 15:11 `sequence_flow.json` | `--positions 40` 与 `positions_entered=159` 不一致；`window` 五个总量全 0；verdict 的 expected=80、executed=312。该快照不能作为“网关通过”或“开关失败”的定量凭证，只能作为方向证据。 |

### 16.3 关键代码

**S01：开关也走 gateway_fire**

```c
/* trigger_sequence_pilot.c:396-415 */
if (!s_pos_fired) {
    if (!s_pos_submitted) { ... software_step ... }
    if (!snap.ready) return false;
    if (!sync_io_sequence_gateway_fire()) { s_pos_stage = 3u; return false; }
    ...
}
if (snap.gateway_pulse_busy) ...
(void)sync_io_sequence_gateway_ready();
```

开关的 `config.gateway_enabled=false`，`gateway_fire()` 直接 false；这段没有“非网关”的完成判据（应该等 `snap.completed`/executor receipt），所以开关不可能完成。

**S05：模型分支绕过 late**

```c
/* trigger_t2_runtime.c:1412-1422 */
if (s_armed_index < COUNT && model_enabled) {
    const uint32_t index = s_armed_index;
    if (trigger_sequence_pilot_position_service(index)) { ... external_complete ... }
    return;                    /* legacy deadline/late 检查在这里被跳过 */
}
```

`position_arm()` 也没有 `plan->deadline_tick` 参数（`trigger_sequence_pilot.c:332-334`）。因此 `no_plan/late/backend=0` 中的 `late=0` 对模型位置没有证明力。

**S06：Core0 上调 Core1-only stop**

```c
/* trigger_sequence_pilot.c:313-320，SCPI 线程/Core0 调用 */
sync_io_sequence_stop();      /* Core1-only: Core0 上直接 return */
sync_io_sequence_release();   /* armed 时也直接 return */
```

模式切换没有真正停掉模型；这也是“回退 mode 1 后旧路径再冲突/需要 reboot”的代码根因之一。

### 16.4 建议

1. **先修 S01/S03/S04**：给 pilot 保存 `s_pos_gateway`；开关分支不调用 `gateway_fire/ready`，改用 `snap.completed/written` 推进；每个位置按 code word/count 重新 arm 或提供“更新计划”接口；明确 prime 后是 `count-1` 次推进，补 16 组开关的单测。
2. **修 S02**：把开关部署绑定的码字 + 输出段上送（模板/SCPI 或 `switch_load` 导出），运行时按段合成完整 4 位值；不要再用 `plan->group_argument` 当码字。先让 `pos_count=16` 的 `fault=0`、`fault_detail=0`。
3. **修 S05**：模型 arm 接口必须带 `base_deadline_tick`/每组 delta，或在模型内实现绝对截止比较；在模型分支恢复 `late` 计数，否则 361 PASS 只能证明“都执行了”，不能证明“按 T2 时间执行”。
4. **修 S06**：`T2:SEQ:MODE` 只写 Core0 命令槽/代际，由 Core1 service 执行 stop+release+handback；不要从 SCPI 直接调 Core1-only API。模式切换后等 `armed=0` 再允许旧路径。
5. **S07/S08**：外部 READY 要么补 IRQ6 grant，要么配置校验拒绝 `gateway_input_channel!=0`；把 `s_index/s_errors` 的复位移到 Core1 IDLE/ARMING。
6. **证据**：15:11 用 `--positions 361 --sample-ticks 90` 重跑，修好 window 总量读取；在 S05 修好前，报告里不要把 `late=0` 写成时序通过。

### 16.5 边界

- S01/S02/S03/S04/S05/S06 是代码路径确定；S07/S08 是代码/时序推断。
- 网关 mode 1 的 361 PASS（进度 022）是当前已验证的配置；本节不否定它，只指出它是**计数/完成**通过，且开关 mode 2 仍有独立阻塞。
- 15:11 快照的 `positions 40` 与 `entered 159` 不一致，不能用于定量验收；以进度 022 的 361 全圈和修复后的重跑为准。

**架构决策（2026-10-21 傍晚，按主控指示）：PIO 程序按角色热加载**

PIO0 是 SMA/sync 专用块（`BOARD_TDMA_SMA_PIO_BLOCK_ID = 0`），只有 **32 条指令**，且 persona 必须
在描述符里申报占用（`instruction_words`）。实测开关板在 `MODE 2` 下可用余量装不下 18 条 executor
（`fault_detail = 60 + 剩余字数` 已可读出预算）。按主控指示，正确方向不是挤占，而是**按角色热加载**：

| 板角色 | 装载的 PIO 程序 | 约需字数 |
|---|---|---|
| 开关（SWITCH） | `sync_switch`（现有开关程序，2 个 SM，`(code, delay)` 协议） | ~8 |
| 网分（GATEWAY） | `sequence_gateway`（现有 SP2T 程序，2 个 SM） | ~14 |
| 计数器（COUNTER） | `sequence_counter` / 捕获 | 现有 |

即：统一模型保留**计划与时序**（value + settle + 脉冲、绝对截止、组序列），但**执行器按角色选择**，
不再所有板都装 31 条通用程序。实现要点：

1. `sync_io_sequence_config_t` 增加 `executor_kind`（SEQUENCE | SWITCH | GATEWAY）；
2. `load_hardware` 按 `executor_kind` 选择程序与 SM 布局（开关用 `sync_switch` 的 2-SM 布局）；
3. 计划项 → 程序协议适配：开关推送 `(code, settle_ticks)`，网关推送现有格式；
4. persona 描述符按角色给出 `instruction_words`，让 claim 的预算检查反映真实占用；
5. 验收仍以 `MODE 2` 下两次 361（`16 × 361` 组、零失败、`late` 可观测）为准。

**S06 修复（2026-10-21 晚）：模式切换改为三段握手，回退不再需要重启**

- `T2:SEQ:MODE` 是 Core0/SCPI 路径，而 `sync_io_sequence_stop()/release()` 是 Core1-only。原实现让
  Core0 直接调用，等于什么都没做：切回 mode 1 后模型仍持引脚，旧路径整轮 `backend=361`，只能重启恢复。
- 现改为：Core0 只增加"模式变更请求"代数并清零自己的预约标志；Core1 的
  `trigger_sequence_pilot_service()` 发现代数不一致后执行 `stop + release + 解除 plan 签名 +`
  （mode≥2 时）让出旧 persona，然后回执；Core0 等回执后才碰仲裁器。
- **实测**：先切 mode 2、再切回 mode 1（不重启），开关立刻恢复（`SWITCH_B 5776`、`SWITCH_A 5760`）；
  随后干净复跑 `SWITCH_A 5776 / SWITCH_B 5776 / GATEWAY 720`（网关差 1 个位置，窗口边缘）。
- 结论：**开关旧模式 + 网关统一模型的混合配置可以完整跑完 361**，且模式切换可逆。
