# T2 预约触发实施待办

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TODO.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/trigger/t2/TRIGGER_T2_REVIEW.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-10-13

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
7. **输出门禁**：VDC 进入 LOCKED 并发布后 T2 才有输出（K-3）。

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
| T2-DEPLOY-003 | 多板槽位业务闭环 | IN PROGRESS | 四板真实运输和角色互换闭合；诊断 ACK 不冒充位置描述符或实际动作证据 |
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
| T2-EXEC-002 | 两组滑动窗口执行端 | PENDING | SWITCH/GATEWAY 执行端支持深度 2 的绝对 deadline 队列，每组独立序号、回执与取消结果；一组完成即推入下一组；STOP 取消队列内全部未执行组并记录硬件取消结果；属 SYNC_IO 接口变更，需该域确认（评审 A-16） |
| T2-DCO-001 | FREEZE 后 deadline 随 DCO 更新 | PENDING | FREEZE 只锁定位置；Core0 在模型 token 变化时为未装填组重新解算，更新放在网分采样期间；Core1 装填前比较 token；已被硬件接受的组不追改；依赖 VDC 证明模型切换无相位跳变（评审 A-06、E-01） |
| T2-GATE-001 | VDC LOCKED 发布输出门禁 | BLOCKED | T2 只在 VDC 进入 LOCKED 并发布后生成和装填 deadline，消费 VDC 提供的发布状态而不自行判定锁态；补全部非发布锁态的主机负测；阻塞于 `VDC-LOCK-001`、`VDC-RUN-001`，台架期间只允许显式诊断开关且结果不计验收（评审 A-02） |
| T2-TRANSPORT-002 | 一等席单帧位置记录 | IN PROGRESS | 紧缩单帧已实现并经四板送达，改写已排队 VDC image 的 patch 路径已删除（`T2-PROGRESS-20261010-022/024`）；剩余为 VDC 核心字段清单、一等席布局/预算的独立登记（C11），以及按 `T2-IDENT-001` 新身份口径重新核定（评审 A-01、A-03、A-15） |
| T2-IDENT-001 | 位置身份与在途容量 | IN PROGRESS | 由扫描参数派生的位置数（0..360°@1° ⇒ 361）与紧凑记录身份容量一致。**2026-10-12 定稿**：T2 只预测下一到两个位置，长扫描的 361 个位置**不进入常驻状态**；常驻 1~2 个在途位置即可（`TRIGGER_T2_RUNTIME_POSITION_COUNT` 继续作窗口用）；线上绝对身份**无需加宽 4 bit id**——10 B 单帧已带 32 bit `target_ms_index`，配合扫描参数即可确定绝对位置，`position_id` 作在途实例编号；**预测超过 2 个位置一律视为不准确**（速率是单样本估计），window 外不产生任何对外事实；冻结确认 32 bit 掩码足够。剩余：与 TDMA 一等席布局一起核定并按 `EXE-SEAT-01` 登记（C11） |
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
| VDC 发布 | T2 输出以 VDC LOCKED 并发布为前提；`VDC-LOCK-001`、`VDC-RUN-001` 为 PENDING | M3 及之后的正式输出 |
| T2:STARt 命令语义 | 固件命令名沿用历史命名 `T2:STARt`，**实际语义是「挂载/武装」**（走停机态维护路径，环 `enabled`/`adapter_started` 后即被拒，`tdma_service.c:1033-1035/1079-1081`），不产生输出；**动态启动统一由 `LOAD:MASK` 位 6（`APP_REALTIME_LOAD_SYNC_TRIGGER`）承担**。把命令重命名为 `T2:MOUNT`（并与 `T2:STARt` 兼容或替换）待 T2 稳定后独立实施：需同步 SCPI 注册表、测试与工具，并按 AGENTS.md §7 付一次 P3 凭证 | 无（当前只影响命名清晰度；流程与文档已按三语义书写） |
| ORIGin:TRIAL 停环 | `CALibration:ORIGin:TRIAL` 间歇使参考环 `enabled`/`adapter_started` 归 0、config 前进 1（已与 `SOURce:STARt` 拆开确认由 TRIAL 单独触发，见 `T2-PROGRESS-20261012-001`）；游标随之按 `site RING` 粘性失效，该轮无发布 | T2 台架稳定性 |
| 从板相位起始迟到 | 从板 SYNC_TRIGGER 持续累计 `start_miss`（前序相位迟到继承，不隔离、游标计入 `missed_sync_count`）；需定位前序相位的迟到来源再判断是否影响动作时限 | M3 动作时限 |
| 镜像辨识 | `PROJECT_BUILD_ID` 是 CMake configure 时的缓存值，增量构建不变；OTA 与验收凭证以构建号辨识镜像存在盲区，需改为每次构建唯一或以包 SHA256 绑定 | 验收证据可信度 |
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

### 体量合规（2026-10-13

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。

## 遗留待办（对照 HAOFV 逐级审查收尾，2026-10-13）

审查结论与证据见 [Task Progress](TRIGGER_T2_TASK_PROGRESS.md)；本节只记任务状态。状态用 `PENDING` / `IN PROGRESS` / `BLOCKED` / `DONE`。

| ID | 任务 | 状态 | 前置与判据 |
|---|---|---|---|
| T2-AUDIT-01 | 模型脉冲路径的**运行期 PIO 热加载**整改：登记为启动期资源，经 mode descriptor 声明 PIO/SM/DMA/GPIO，STOP/config ACK 阶段装载并读回 | `PENDING` | **前置已解决（2026-10-13 只读定位）**：模式声明机制已存在——ops 表类型 `sync_io_mode_ops_t`（`components/sync_io/inc/sync_io_mode.h:58`）带 **`.hw = { … }` hardware descriptor**（样例 `sync_io_mode_biss_tap.c:231`），中央派发表 `sync_io_mode.c:9-17/:27-35`，各模式均有 `*_mode_ops()` 访问器，公共 API 样板为 `sync_io_seq_step_arm/disarm/get_index/get_runtime`（`sync_io.h:304/:310/:311/:314`）。**实现形态**：新建 `sync_io_mode_<name>.c/.h` 声明 PIO 实例/SM/DMA/GPIO 并登记进派发表，`sync_io.h` 增 arm/disarm/runtime；**装载改在 STOP 阶段的 mode arm 内**，不再在模型 arm 路径内 `pio_add_program`；并补 HAOFV 要求的**资源冲突负测**与 DeploymentGate 的 PIO/SM/DMA/IO/IP claims；判据：连续 `START/STOP` 后可用程序槽位不减少且装载只在 STOP 阶段；需 P3 硬件验收凭证 |

**核验结论（2026-10-13）：该项已合规，无需改造**。
证据（均为本仓现状）：`sync_io_model_sched.c` 的 persona 判据在 `:931-934`（PIO0 且
`pulse_sm` 为 `BOARD_SYNC_PIO0_WAVE_OUTPUT_SM` 或 `BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM`）；
满足时走 `:955` 的 persona 路径：`:173` 的 `sync_io_wave_output_load` 在**停态装载**（claim + `pio_add_program`），
`:411` 注册为 `.load` 钩子，而 `:213` 的 arm 路径**只校验** `s_wave_output_program_loaded`。
模型/转台通道正在该判据内（SM2），故 **arm 期不再发生运行时热装载**。
仅当 `pulse_pio` 非 PIO0 或 SM 不属于上述两者时才会到达 `:980-1002` 的**非 PIO0 回退**（例如 `BOARD_SYNC_PIO_WAVE` 一路），该回退不在 T2 转台路径上。

| T2-AUDIT-02 | 每位置一组 → **每位置 16 组 + 两组滑动窗口**（审查 A-06/A-16） | `DONE` | 硬件闭环：`total = 2896 = 181 × 16`、`fault = 0`；残余：FREEZE 后按最新 DCO **重算未装填组**（K-2）尚未实现；**行级定位已完成（2026-10-13，只读核实）**：“按最新 DCO 重算”的**机制已存在**（`trigger_t2_runtime.c:1054-1055`：`prediction_version` 与 `model_token` 均未变才 `continue`，否则用**当前 `model.dco`** 重算），但被 **FREEZE 门抑制**：`:1048` 对已冻结位置**整位 `continue`**（不再重算）、`:1082` 对已冻结位置**不发布**，因此架构原文的“FREEZE 后停止重算（A-06/A-16）”**属实**，与 K-2（FREEZE 只冻结位置预测、不冻结 DCO）**直接冲突**。**修正（2026-10-13，ROI 复核）**：上述“仅两行”属**过度简化**，实际改动面**更大**：① `:1048` 改为“仅停位置预测/新实例、仍对**未被硬件接受**的动作重算”，需区分**已接受 / 已装填 / 尚未装填**；② `:1082` 要“允许发布尚未接受的新解算、且**不改写已接受**”，而当前代码**并无**“已接受”状态信号（Core1 负责 `service_deadlines`/回执，Core0 只能看到邮箱旧字）⇒ 需新增**Core1→Core0 的“已装填/已接受”回信或等价判据**，否则无法同时满足 K-2 两半（未装填重算 ✓ + 已接受不追改 ✓）。**第二次修正（2026-10-13 只读核实，本轮）**：上述“需新增 Core1→Core0 已接受回信”属**过度复杂化**：快照已携带可判据的掩码——`action_armed_mask`（**仅准入**）、`action_completed_mask`（**需匹配 PIO 回执**）、`emitted_position_mask`、`action_deadline_tick[]`（见 `trigger_t2_runtime.h:87-94`，原注释：“ARMED is admission only. COMPLETE/emitted require the matching PIO receipt”），Core1 侧另有 `s_armed_index`（`:116/:677`）与 `s_deadline_seen`（`:109`）。因此 `:1048`/`:1082` 可**直接改用这些现有掩码判据**（仅跳过已被接受/已完成位），**无需新机制**；**判据已定义（2026-10-13）**：代码自述——`trigger_t2_runtime.c:677-680` 在记录 `action_armed_mask` 的**同一时刻**写入 `action_deadline_tick[selected]`，并注释“**Read the owner's accepted sequence; a later mismatch cannot complete us**”，随后 `:683-691` 读取 owner 的 accepted sequence；而 `:524-525` 表明**完成/发射**才置 `action_completed_mask`/`emitted_position_mask`（需匹配 PIO 回执）。因此：**“已被硬件接受” = `action_armed_mask`**；**“已完成” = `action_completed_mask`/`emitted_position_mask`**。据此 `:1048` 应改为 `if ((s_state.action_armed_mask | s_state.action_completed_mask) & (1u << index)) continue;`（即**仅跳过 deadline 已被接受的位**，**已冻结但尚未装填的位仍重算**）；`:1082` 同步去掉“已冻结即不发布”的朡断（由 `:1048` 保证已接受者不再进入）。原因：因此 ② 回到“**小改动 + P3**”估时，但时长估计需按“**小型设计 + 实现 + P3**”计（而非两行修改）。：① `:1048` 改为仅停“位置预测/新实例”、**仍对未被硬件接受的动作重算**；② `:1082` 改为**允许发布尚未被接受的新解算**，**已被硬件接受的 deadline 绝不改写**（需“已接受”标记）。**落点已查明（2026-10-13）**：`components/model_turntable/` 内无 `freeze`/`DCO`/`token` 符号，`model_turntable_recompute_plan()`（`model_turntable.c:111`）仅在**配置/启动**路径被调用（`:263/:297/:312/:323`），**运行期不重算**；因此 K-2 应在**上层装填路径**实现（`trigger_t2_runtime` 的 `prepare_deadlines_core0()` / `service_deadlines()`，即架构 `T2-OWNER` 所指入口），**不在模型层动刀**；判据：装填前比较模型 token，不一致则用新解算，**已被硬件接受的 deadline 不得追改**，赶不上则判迟并记录 |
| T2-AUDIT-03 | 紧凑记录的**跨域契约登记**（`docs/check/DOCS_REGISTRY.md`）+ **C11 独立交叉审核** + 与 **TDMA 一等席布局联审** | `BLOCKED` | 阻塞条件：描述符 wire 布局、profile 默认值、资源配额与正式时限**尚未冻结**；按本文与架构文件规定，**登记前不得写成冻结契约**，因此不得提前登记 |
| T2-AUDIT-04 | 转台**源路径选型**定稿（`TRIGger:REServation:SOURce:*` 硬件 PWM 源 vs `CONFigure:MODEl:TURNtable:*` 加减速模型源） | `BLOCKED` | 阻塞条件：属**产品/验收口径决策**，选型会改变隔离验收（`T2-TURNTABLE-008`）的证据口径；待用户定稿，本任务不预设结论 |

说明：`T2-AUDIT-03` 与 `T2-AUDIT-04` 的 `BLOCKED` 为**前置未满足**（契约未冻结、口径未定），非技术不可行；
前置满足后按 AGENTS.md 的契约登记流程（含 C11 交叉审核）推进。

### 执行交接（ROI 状态

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。

### ② 的台架语义验收用例

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。

### 判据抽取重构：P3 验收未通过

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。

### T2-AUDIT-04 决策输入

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。

### T2-AUDIT-03 前置：契约冻结清单

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。

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

### T2-AUDIT-06：按参考实现移植 persona 资源与 WAVE_OUTPUT persona（2026-10-13）

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

### C14 R3 命名规则

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。

## 附录 A（已归档）

原「实现注记」全部内容（编译器约束、legacy 三槽迁移清单、SCPI 接线落点、已结案项与 152 条探索日志）
已按 C14 转出本文件，以保持本文为**任务清单**：`docs/legacy/trigger/LEGACY_TRIGGER_T2_TODO_01.md`。
本节只保留指针；新结论请写入上方「结论区（冻结，只增不改）」。
