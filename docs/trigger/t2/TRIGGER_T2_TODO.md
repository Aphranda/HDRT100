# T2 预约触发实施待办

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TODO.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/trigger/t2/TRIGGER_T2_REVIEW.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-10-15

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

6. **角度扫描与多组序列**（2026-10-10 确认）：扫描由起始、终止和 STEP 定义，单位为度、分辨率 0.001°，每个 STEP 位置都执行完整触发（K-4）；每个位置运行多组序列与采样（当前 16 组），16 组预装，执行端按包含两组的滑动窗口装填，DCO 更新放在网分采样期间（K-6）。多板模式下 READY 只做异步踩踏校验（K-1）。
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
| T2-DEPLOY-003 | 多板槽位业务闭环 | IN PROGRESS | 四板真实运输和角色互换闭合；诊断 ACK 不冒充位置描述符或实际动作证据。**2026-10-15 角色动作实测**：动作表按本板角色筛选（`trigger_action_service.c:109`），运行时按本板模板种类布置 SWITCH/GATEWAY 截止（`trigger_t2_runtime.c:670-710`）；判据页 `TRIGger:RESERVation:T2:ACTion?`。两轮 `--full`（`out/tdma_t2/steps-20261015-091127.log`、`steps-20261015-092034.log`）中各正好两板 `armed 3 / completed 3 / failed 0 / uncertain 0`，未执行板 `received 0 / frozen 0`，且**饿死者在两轮间轮换**（SWITCH_B ↔ GATEWAY）⇒ 角色差异执行可用，缺口是**竞争型单深度邮箱 + 有界 hold 环**（`lane_reject` 14k–69k、`hold_drop` 26k–69k）导致每轮总有一块目标板拿不到新鲜片。剩余：改分发语义使**每个目标角色**每轮都拿到片（每目标独立副本/邮箱、加深 hold 环或按目标单播），并让 `ACTION execution: N of 4` 成为判据。**2026-10-15 Step 1 根因与实施清单（不改 wire）**：源侧只把冻结回执与"发布但未完成"集合按位与（`trigger_t2_runtime.c:186-216`），8 位回执是**当前待确认窗口**而非全量历史 ⇒ 无需改 run id 语义或回执格式；三槽限制来自运行时自己的簿记。实施顺序：① 新增 `TRIGGER_T2_RUNTIME_POSITION_WINDOW = 8`（与 `TRIGGER_T2_RUNTIME_POSITION_COUNT = 3` 的在途预测窗口分离）；② 源侧 `published_mask`/`freeze_complete_mask`/`freeze_abandoned_mask` 与从板 `frozen_mask`/`received_mask`/`s_pending_mask`、`s_deadline[]`/`freeze_version[]`/`freeze_local_version[]`/`freeze_local_age[]` 按新窗口重标；③ `accept_remote_position()` 接受 `position_id ∈ [1,15]`，加 `s_slot_position_id[WINDOW]` 实例→槽映射与 15 位 `s_executed_instances` 防重复执行，槽在动作完成/放弃后回收；④ 同批 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY 1→16`、`TRIGGER_ACTION_TEMPLATE_CAPACITY 8→16`；⑤ 工具短扫描覆盖（`T2_SCAN_OVERRIDE_TERMINAL_MDEG` ≤8 位置）验证从板累计 `events`/冻结覆盖全部位置、`lane drop` 不再单向增长、`ACTION execution` 覆盖全部目标板，再上全扫描。**2026-10-15 迭代纪律（用户裁决，取代逐改动付 P3 的做法）**：① 不再为每个小改动单独跑 P3——批次做大再做；② **P3 只覆盖 DPLL/联合采集，不能证明 T2 的改动正确** ⇒ T2 的功能判据必须由台架 T2 运行给出：先 `python tools/hardware_acceptance/hardware_acceptance_workflow.py --steps 0-7`（构建 + OTA 烧录，~3.5 min）把改动烧进四板，再 `python tools/sync_trigger/t2_workflow.py --full`（~8 min）验证 T2 行为；③ **功能验证通过后**，再对整批跑一次全项 P3（`--profile full`）拿凭证并提交；④ 期间允许工作区保留已宿主验证但未提交的改动（不再为提交而提交）；⑤ P3 的 step 29（DPLL60 联合采集）偶发失败时重试即可（第三次通常通过）。原句"固件改动 ⇒ 契约登记 + C11 + 一次全项 P3"仍成立，只是"一次"指**批次收口处**。**2026-10-15 实证边界（批次必须包含 harness 期望）**：把从板位置消费局部切到 `trigger_position_window_t`（实例键滚动 + 回执由窗口派生 + 槽位越界守卫）可以编译通过，但会让 `tests/python/test_trigger_t2_runtime.py` 的 **16 个用例转红**——它们钉住的是旧三槽语义（每板一行/一个动作、冻结位粘滞等）⇒ 运行时切换**必须与这 16 处期望、页/契约同批完成**，否则不构成可验证状态。该局部改动已按回收规则回退，树中不留未验证改动；回退后宿主为 `26 passed, 1 failed`（唯一失败是既有红 `freeze_missing`）。批次清单因此再确认：① 运行时两侧切换；② 同批更新 16 处运行时用例期望；③ 尾追加页 `T2:WINDow?` + 页契约；④ 模板容量 1→16/8→16；⑤ 修既有红 `freeze_missing`。验证用 `T2_SCAN_OVERRIDE_TERMINAL_MDEG=8000 python tools/sync_trigger/t2_iterate.py`（自动判据）。**2026-10-15 批次项⑤现状（既有红定位）**：`test_trigger_t2_runtime[freeze_missing]` 的失败断言在 `tests/python/test_trigger_t2_runtime.py:343`：`snapshot().freeze_missing_local_count==1 && !inverse_calls && !schedule_calls`——语义是"FREEZE 到达但本板没有已保存的预览时，必须把一个位置记入 `freeze_missing_local_count`，且**不得**调用 inverse/schedule 后端"。该用例在本会话所有改动**之前**即为红（已用 `git stash` 反证），修复需沿 `stage_pending_freeze()` / `preview_copy()` 路径追因（与 `T2-FREEZE-002` 的"无预览不中止整轮"语义相关），本次余量内未完成。注意：切换到 `trigger_position_window_t` 后该计数仍须保留（它是丢失证据，不是旧槽位语义）。**2026-10-15 追加观察（项⑤根因更清晰）**：该用例在不同轮次表现不同——先是断言不等（`freeze_missing_local_count != 1`），后一次是**子进程 5 s 超时挂死**（`subprocess.TimeoutExpired: out/pytest/runs/t2-runtime0/runtime.exe freeze_missing`）。⇒ 除计数语义外，"FREEZE 到达但本板无预览"这条路径很可能**阻塞**（等待/自旋），追因时优先看 `stage_pending_freeze()` 的 `preview_copy()` 失败分支与 `s_pending_mask` 的保持/重发循环（`trigger_t2_runtime.c:301-357`）是否会在无预览时反复进入而不推进。同批次内顺手把该用例从"挂死"变成"有界失败"更符合 `T2-FREEZE-002` 的"不中止整轮"口径。另：模板表容量已按本批扩容 `TRIGGER_ACTION_TEMPLATE_CAPACITY 8→16`（`trigger_action_template_table.h:10-13`，宿主点名集合 `188 passed / 1 failed`＝仅上述既有红，未提交、待批次 P3）。固件改动 ⇒ 契约登记 + C11 + 一次全项 P3。**页/ABI 约束（实施前必读）**：`TRIGGER_T2_RUNTIME_POSITION_COUNT` 同时定尺寸于快照结构（`trigger_t2_runtime_snapshot_t` 的 `freeze_version[]`/`freeze_local_version[]`/`freeze_local_age[]`/`action_*[]`）**与** SCPI 页的逐位置发射（`T2?` 第 19..27 字段、`T2:ACTion?` 第 5..19 字段）⇒ 3→8 会**移动既有页索引**，违反追加字段纪律。实施必须：既有 3 项区块保持原位不动，新增的 5 项**追加到各自页尾**（或另开一张独立诊断页），并在登记表补一条页契约变更；宿主 harness（`test_trigger_t2_runtime.py`、`test_t2_page_field_order.py` 等）随页形状同步改。**2026-10-15 收窄结论（优先方案，避开页/ABI）**：源侧每个位置本来就是"进窗→公告→冻结→滚出"顺序发布（实测 `announced 9→283`），其 `published_mask` 只表达**当前在途的 1~2 个位置**、`open` 集合同样很小 ⇒ 回执只需要覆盖当前在途那几个位，**窗口宽度不必扩到 8，也不必改任何页/快照**。因此 Step 1 可收窄为**纯从板侧**改动：① `accept_remote_position()` 不再把 `position_id` 当槽号（`position_id-1`）而按**实例**映射到 3 个滚动槽，接受域放宽到线上 `[1,15]`，新实例占用某槽时**清除该槽的冻结位**（回执位语义＝"当前在途实例是否已冻结"）；② 新增 15 位 `s_executed_instances` 防止同一实例的重复 FREEZE 再次布防；③ 槽在动作完成/放弃后回收，使从板沿扫描持续消费；④ 同批并入模板容量 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY 1→16` 与 `TRIGGER_ACTION_TEMPLATE_CAPACITY 8→16`（Step 3 前置）。判据不变：短扫描（`T2_SCAN_OVERRIDE_TERMINAL_MDEG`）一轮内从板累计 `events`/冻结覆盖全部位置、`lane drop` 不再单向增长、`ACTION execution` 覆盖全部目标板。上文的"扩到 8 位窗口"降级为**备用方案**（仅当需要同时在途 >3 个位置时再启用，届时按页尾追加处理）。 |
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
| T2-SEQ-001 | 每位置多组序列预装 | PENDING | 每位置 16 组（切换编码、相对 `q_P` 偏移、网分脉宽、READY 窗口）在位置开始前预装并读回；组间隔按网分最坏响应时间加 settle 与余量；组间不由 READY 推进；各板只展开本角色的步（评审 A-16） |
| T2-EXEC-002 | 两组滑动窗口执行端 | PENDING | SWITCH/GATEWAY 执行端支持深度 2 的绝对 deadline 队列，每组独立序号、回执与取消结果；一组完成即推入下一组；STOP 取消队列内全部未执行组并记录硬件取消结果；属 SYNC_IO 接口变更，需该域确认（评审 A-16）。**2026-10-14 核实**：现状确为单槽——`sync_io_switch.c` 以 `SYNC_IO_SWITCH_PENDING` 显式拒绝、`sync_io_gateway.c` 经 `prepare_realtime()` 的 pending 标志间接拒绝（失败路径另清 OUTPUT/READY FIFO），两侧各只有一份 `s_deadline`，头文件自述 “One transaction at a time, Core1 only.”；而 SYNC_IO 域此前**无对应任务项**，故本项长期无受理方。已在该域登记 **`SYNC-SCHED-001`** 作为本项跨域前置（`docs/sync/SYNC_IO_TODO.md`）。**T2 侧可动部分**：提交/取消/回执状态机（在途 2、窗口前移、逐组 `sequence` 绑定）与容量满、序号错配的终止性失败负测。 |
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
| 模板/组 | 每位置 **16 组** × 在途 1～2 ⇒ 常驻 16～32 组（现每位置 1 组 ⇒ `LOCAL_TEMPLATE_CAPACITY` 需 1 → 16） |
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

**仍在待办**：① SCPI 接线；② `trigger_t2_runtime.c` 消费者迁移（legacy 3 槽 → 窗口条目，并把附录 A 之外的健康计数读回）；③ 每位置 16 组与执行端窗口深度 2；④ 边界位置执行侧证据闭合。代码级落点、编译约束与结案复盘见 **附录 A**。

本节的代码级落点（SCPI 命令面）、实施约束（`-Werror` partial initializer）与结案复盘见文末**附录 A**。

**仍在待办（原清单，保留引用）**：① SCPI 接线（见下节）；② `trigger_t2_runtime.c` 消费者迁移（legacy 3 槽 → 窗口条目，并把上表计数读回）；③ 每位置 16 组与执行端窗口深度 2；④ 边界位置证据闭合。

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
