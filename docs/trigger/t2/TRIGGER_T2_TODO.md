# T2 预约触发实施待办

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TODO.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/trigger/t2/TRIGGER_T2_REVIEW.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-10-12

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

## 附录 A：实现注记（代码级，2026-10-12）

本附录承载**代码级**内容（命令面落点、编译约束、缺陷复盘），与主体分层：主体只回答"要什么、做到哪、怎么算完成"，本附录回答"具体在哪个文件、踩过什么坑"。

### A.1 实施约束（实测教训：`-Wextra -Werror` partial initializer）

- **不得往 `trigger_turntable_predictor_config_t` 等被部分初始化的结构体加字段**：`-Wextra -Werror` 下 partial initializer 会打断既有 harness 与调用方（实测 31 个测试报错）。STEP 因此存在 snapshot（`window_step_mdeg`，由 `trigger_turntable_window_configure()` 设置），纯函数显式接收形参。

### A.2 迁移注意：legacy 三槽的 lead 已不符合 K-10

等价性检查（`tests/python/test_turntable_rolling_window.py` 的 `legacy_equivalence()`）实测：同一扫描下**目标计数**两条路径一致，**阈值不一致**——

| 路径 | lead 基准 | 结果（step=180°、ppd=100、500 脉冲的首目标） |
|---|---|---|
| 滚动窗口（符合 K-10 / `T2-PREDICT` 2026-10-11 口径） | **一个 STEP**（18000 脉冲 × 95% = 17100） | 阈值夹到 0（首位置立即到期）、1400（第二位置） |
| legacy `begin()` 三槽 | **距 origin 的距离**（500 × 95% = 475；第二位置已膨胀到 18500 × 95% = 17575） | 阈值 25（首位置）、925（第二位置） |

⇒ **legacy 路径不只是冗余，而是与已定稿口径不符**；消费者迁移时**必须**改为一步比例（不能"平移"旧值）。测试已把两侧数值同时钉住，删除 legacy 数组时该断言随之改为只校验窗口侧。

### A.3 迁移清单：legacy 三槽在运行时的使用点 → 窗口对应物

审计 `components/sync_trigger/src/trigger_t2_runtime.c`（2026-10-12，行号为当时快照）：

| 使用点 | 现状 | 迁移后 |
|---|---|---|
| origin 每采样路径 | `trigger_turntable_predictor_observe(&s_state.predictor, …)` 取 `mask`，并把 `predictor.announced_mask` 存进 `s_threshold_public`（原子） | 调 `trigger_turntable_window_advance(…)` 取 `mask`；对外发布的仍是"本次宣告了哪些位置"，数值语义不变 |
| deadline 准备路径 | `predictor.announced_mask & bit`、`predictor.threshold_count[index]`、`predictor.target_count[index]` 判断"是否已到期/还剩多少" | `trigger_turntable_window_find(&snapshot.predictor, ordinal)` 取条目的 `announced` / `threshold_count` / `target_count`；**不再用下标直接索引数组**（窗口只持有 ≤2 个在途条目，照搬下标必错） |
| 描述符构造 | `target_count[index]`、`predictor.count_epoch` | 目标取自已找到的条目；`count_epoch` 不变（共享字段） |
| 配置路径 | `s_config.predictor = config->predictor` + `begin/observe_at_sync` | 追加 `trigger_turntable_window_configure(s, step_mdeg)`（STEP 由扫描参数来，见 A.4 接线）；`begin` 换成 `window_begin_scan` |
| 速率估计 | `trigger_turntable_predictor_observe_at_sync(…)` 维护 `pulses_per_sync_q16` / `rate_valid` | **必须保留**：它是 `window_time_ready()` 与时间预测的唯一速率来源。只把"位置/阈值"逻辑搬到窗口，速率路径不动 |

**两个陷阱**（迁移时最容易出错的两点）：

1. **删错了对象**：`observe_at_sync` 不是"legacy 位置逻辑"的一部分，删掉它会让 `window_time_ready()` 永远为假 ⇒ 每个位置都被判"时间不可用"（`window_rate_stale_count` 飙升），故障表现为"能冻结但不发布"。
2. **下标照搬**：`target_count[position_id − 1]` 这类写法在窗口下不成立（条目随滚动换位），必须走 `window_find()`。

**顺序约束（2026-10-13 推演得出，务必遵守）**：**先迁移运行时，再把台架扫描切到 0..360°@1°（361 位置）**。理由：

1. legacy 只有 **3 个槽**（`TRIGGER_T2_RUNTIME_POSITION_COUNT = 3`），无法表示 361 个位置；若先把扫描参数切到 361，legacy 仍为权威路径 ⇒ 台架会跑出无意义的对照结果（3 个槽去装 361 个位置）。
2. **窗口的宣告掩码位是 slot（`1u << slot`，slot ∈ [0, 2)），不是位置序号**。当前 deadline/描述符路径用 `announced_mask & (1u << index)` 判断"该位置是否已宣告"，其中 `index` 是 legacy 的槽下标；迁移时**不能**把该表达式直接换成窗口掩码——必须改为"遍历窗口条目、按 `entry->absolute_index` 取用"（这正是 `trigger_turntable_window_find()` 与"掩码位语义"两处必须一起改的原因）。
3. 因此迁移不止是"把数组换成查表"：**宣告 → 位置的映射要重做**（从"槽下标即位置"变为"slot 是容器、`absolute_index` 才是位置"）。

建议的迁移顺序（每步单独跑一次台架、用并行对照读数验证）：
① 用窗口条目重写"宣告 → 位置"的取用（读 `entry->announced` 与 `entry->absolute_index`），保持 legacy 数组仍存在但不再被读；
② 删掉 legacy 数组与 `announce_mask` 的写入；③ 按 A.2 把 lead 改为一步比例（等价性测试随之收敛为只校验窗口侧）；④ 最后把台架扫描切到 361 位置并核对 `geometry 361pos/100pul`、`announced == entered`。

**迁移代码图（2026-10-13 读源码所得，行号为当时快照）**：

| 位置 | 现状 | 迁移形态 |
|---|---|---|
| `trigger_t2_runtime.c:1020-1051` 准备循环 | `for index < POSITION_COUNT`，用 `bit = 1u << index`、`threshold_count[index]`、`target_count[index]`、`s_last_calculated_coarse[index]` | 改为遍历窗口条目（`window[slot]`，跳过 `!valid`），用 `entry->absolute_index` 作身份、`entry->threshold_count/target_count` 作算术；**每槽的 `s_last_calculated_coarse` 需按新键重映射** |
| `:1023` 公开阈值掩码 | `__atomic_load_n(&s_threshold_public) & bit`（bit = slot） | **需先决定键**（见下） |
| `:1024` 宣告 | `snapshot.predictor.announced_mask & bit` | 改为 `entry->announced` |
| `:1031-1032` 跨度 | `target_count[index] - origin_count` | `entry->target_count - snapshot.predictor.origin_count`（`origin_count` 不变） |
| `:813-819` 采样路径 | `observe(...)` 取 mask + 发布 `announced_mask` | `window_advance(...)` 取 mask；对外发布语义不变 |
| `:738+` 启动 | `begin()`（本轮已并行加 `window_begin_scan`） | 迁移完成后 `begin()` 退出 |

**迁移前必须先定的两个设计决定**（否则会写出自相矛盾的映射）：

1. **`s_threshold_public` 的位语义**：现在 bit = legacy 槽（0..2），下游按"位置"读它。窗口下候选键是 ① 窗口 slot（0..1，需下游改读 `window_find` 才知道是哪个位置）或 ② `absolute_index`（位置本体，但 361 位置需要模/窗口化表达）。建议 ①（slot 为容器），并让下游一律经 `window_find`/条目取位置——与 A.3 的"slot 是容器、`absolute_index` 才是位置"一致。
2. **`s_last_calculated_coarse[]` 的键**：跟随 ①（按 slot），还是按序号滚动（仅保留在途条目）——建议跟 ①，容量即 `WINDOW_MAX`。

**验证要求（两类，缺一不可）**：`p3_hardware_acceptance.py run` 提供 pre-commit 所需的 P3 凭证，但它**不执行 `t2` 子命令**（不覆盖 origin 的 `observe`/准备/描述符路径）；因此迁移提交还必须跑 `p3_hardware_acceptance.py t2`，确认 22 步 `failed steps: none`、游标 READY、`published_mask` 正常。

**迁移后应删除**：`target_count[3]` / `threshold_count[3]` / `announced_mask` / `terminal_verified` 以及 `begin()` 的三槽初始化（`window_begin_scan` 取代）。等价性测试 `legacy_equivalence()` 届时收敛为只校验窗口侧。

### A.4 接线落点（SCPI，2026-10-12 勘察；**2026-10-12 更正**）

**更正**：先前本节建议在转台模型族下新造 `CONFigure:MODEl:TURNtable:SCAN`。实测**不需要**——T2 自己的配置命令**已经承载扫描参数**，只是缺 STEP 且把 ppd 写死：

```c
/* middleware/scpi_port/src/scpi_trigger_t2_runtime_commands.inc:91-100 */
config.predictor=(trigger_turntable_predictor_config_t){
    .pulses_per_degree=1000u,          /* ← 写死，须改为参数（TODO 依赖表已登记该偏差） */
    .start_angle_mdeg=start_angle, .zero_angle_mdeg=zero_angle,
    .terminal_angle_mdeg=terminal_angle,
    .pre_roll_mdeg=(uint32_t)pre_roll, .post_roll_mdeg=(uint32_t)post_roll,
    .threshold_per_mille=(uint32_t)threshold};
```

⇒ 接线只需在这条既有命令上做四件事：

| # | 动作 | 备注 |
|---|---|---|
| 1 | 增加 **STEP 参数**（`step_mdeg`） | 与起始/终止一起构成 K-4 的扫描定义 |
| 2 | `pulses_per_degree` **由写死改为参数** | 同时与模拟源共用同一个值（禁止两侧各自写死） |
| 3 | 处理器内调用 `trigger_turntable_scan_validate()`，**拒绝即 SCPI 错误** | fail-closed；派生几何存起来供 `READ` 读回 |
| 4 | 运行时启动路径把 `begin()` 换成 `window_configure(step)` + `window_begin_scan(...)` | 见 A.3 迁移清单；`observe_at_sync` 保留 |

#### A.4.2 STEP 如何抵达运行时（定案：独立 setter，不动结构体）

第 1 步落地时会立刻撞上一个约束：STEP **不能**随 `config.predictor` 一起下发——

- `trigger_turntable_predictor_config_t` **没有** step 字段（窗口改造时刻意放在 snapshot 的 `window_step_mdeg`，见 A.1：该结构体被大量部分初始化，加字段会在 `-Wextra -Werror` 下打断既有 harness 与调用方）；
- 因此 `scpi_cmd_trigger_t2_configure()` 即使解析出 STEP，也无处存放。

**定案**：加一个**独立 setter**，而不是扩展结构体：

```c
/* trigger_t2_runtime.h */
bool trigger_t2_runtime_scan_step(int32_t step_mdeg);   /* STOP 期调用；内部 window_configure */

/* scpi_trigger_t2_runtime_commands.inc：解析 STEP 后、configure 之前 */
if (!trigger_t2_runtime_scan_step(step_mdeg)) return scpi_trigger_t2_fail(context);
```

理由：① 不触碰任何被部分初始化的结构体（A.1）；② setter 天然要求 STOP 期调用（RUN 中改扫描几何必须被拒，与 `scpi_port_reject_if_run_forbidden` 的既有约定一致）；③ 迁移到 `window_begin_scan()` 时，运行时只需把已保存的 step 拿出来用，不需要新的配置管道。

**已写好的切片（2026-10-12，因台架凭证未通过而回退，可逐字套用）**：

```c
/* trigger_t2_runtime.c —— 文件级静态 + setter（STOP 期） */
static int32_t s_scan_step_mdeg;

bool trigger_t2_runtime_scan_step(int32_t step_mdeg)
{
    if (step_mdeg <= 0) return false;
    if (s_state.active) return false;          /* 只允许 STOP 期：绝不重整运行中的扫描 */
    s_scan_step_mdeg = step_mdeg;
    return true;
}

/* 启动路径（在 trigger_time_cursor_configure 之前） */
    if (s_scan_step_mdeg)
        trigger_turntable_window_configure(&s_state.predictor, s_scan_step_mdeg);
```

要点：**只有 `s_scan_step_mdeg` 非零时才启用窗口** ⇒ 现有工具（尚未发送 STEP）下 `step=0`、窗口惰性、行为零变化。这是刻意的灰度：先把 API 落地，再让 SCPI 工具开始发送 STEP。

（该切片已在本地形成并通过语法检查，但配套的台架凭证运行失败 ⇒ 按"不留未验证改动"的约定回退；重新实施时照抄上述代码并跑一次 `p3_hardware_acceptance.py run` 即可。）

**两次尝试均构建失败（2026-10-13 00:00–00:07）**：把上面的切片套用后，`p3_hardware_acceptance.py run` 连续两次在**第 2 步构建**失败（`ninja: build stopped: subcommand failed`），因此未取得凭证、改动已回退。两处可疑点，按可能性排序：

1. **启动路径的插入位置**：切片用"回退到上一个空行"的方式把 `if (s_scan_step_mdeg) window_configure(...)` 插到启动语句之前——若该语句跨多行且中间无空行，插入会**切断表达式**。重新实施时应改为"插在 `const bool started =` 这一行之前"，并以显式锚点（含缩进的整行）匹配。
2. **setter 的相对位置**：首次尝试插在 `s_state` 声明之前 ⇒ 编译期 `s_state` 未声明（已定位并修正为"放在 `s_state` 声明之后"），但第二次尝试仍失败 ⇒ 说明问题**不止**这一处。

**工具缺口（值得单独修）**：验收只在日志里报 `FAIL … step failed (…\tools\cmake_build_auto …)` + `ninja: build stopped`，**不含编译器的 `error:` 行**（stderr 被 ErrorRecord 包装吞掉）。构建错误本应一眼可见，实际却让定位多花了两轮。建议让 `p3_hardware_acceptance.py` 在构建失败时把子进程 stderr 原样透出（属 `tools/**` 改动，不触发 P3 门禁）。

**同一步的配套**：`scan_validate` 的 fail-closed 校验也放在该 setter 内（或紧随其后），这样"非法扫描"在配置期就被拒，而不是等到 ARM 之后。

### A.3.0 NEXT ACTION（迁移第①步的可执行改法，2026-10-13 定）

**目标**：把 `trigger_t2_runtime.c` 准备循环（`1020-1051` 区域）从"遍历 legacy 槽"改为"遍历窗口条目"，
使宣告/阈值/目标全部来自窗口；legacy 数组此时**仍存在但不再被读**（下一步才删）。

**改法（形状，非逐字）**：

```c
    /* 遍历窗口条目：slot 是容器，absolute_index 才是位置（A.3 的坑） */
    for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot) {
        const trigger_turntable_window_entry_t *entry =
            &snapshot.predictor.window[slot];
        if (!entry->valid) continue;
        const uint32_t absolute_index = entry->absolute_index;
        /* 公开阈值掩码按 SLOT 置位（决定①），下游一律经 window_find 取位置 */
        const uint32_t bit = 1u << slot;
        if (__atomic_load_n(&s_threshold_public, __ATOMIC_ACQUIRE) & bit ||
            entry->announced ||
            snapshot.last_counter_count >= entry->threshold_count ||
            snapshot.last_counter_count >= entry->target_count)
            continue;
        const uint64_t to_threshold =
            entry->threshold_count - snapshot.last_counter_count;
        const uint64_t span = entry->target_count - snapshot.predictor.origin_count;
        /* 节流表 s_last_calculated_coarse[] 改用同一 SLOT 键（决定②）；容量即 WINDOW_MAX */
        ...（其余逻辑不变：window/interval 选择、历史查找、描述符构造）...
    }
```

**两个决定（已建议，照此执行）**：① `s_threshold_public` 与下游一律以 **slot** 为位键，位置一律经
`trigger_turntable_window_find()`/条目取得；② `s_last_calculated_coarse[]` 同样按 **slot** 索引，容量 `WINDOW_MAX`。

**必须保留**：`observe_at_sync`（窗口时间就绪的唯一速率来源；删掉会导致"能冻结但不发布"）。

**执行与验证顺序（实测有效）**：改源码 → `p3_hardware_acceptance.py run`（构建+刷写+凭证）→ 提交 →
`p3_hardware_acceptance.py t2`（验证流程；**不刷写**，必须在 `run` 之后跑）→ 查看采样行的
`window announced N/entered N overwrote_unannounced 0 … geometry Npos/Mpul`。

### A.3.6 完整原始回复已取得（决定性）：窗口在 A.3.3 修复后**仍未运行**（2026-10-13）

按 A.3.4 的方式（`step_20_sampling` 内、`raw_state` 落盘）取得**完整回复**（字段数 67）：

```
1,0,1,1,1,0,14,1791830178,56846,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,902423,1656,0,
5000,4750,10000,9500,15000,14250,0,58543,0,2,6187,1560,35,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
19,0,51,108,0,0
```

判读（与工具 1 基下标一致，回复首字段为头部元素）：

| 观察 | 结论 |
|---|---|
| `5000,4750,10000,9500,15000,14250` 出现在中部 | 这是 legacy 的三组 (target, threshold)：`5000/4750`、`10000/9500`、`15000/14250` ✓ 与台架发送值一致 |
| 全表**只有一个 5000** | 窗口步长 `window_step_mdeg` 本应再出现一个 5000 ⇒ **窗口仍未启用** ✗（A.3.3 的修复没解决它） |
| 尾部 `…,19,0,51,108,0,0` | 末尾 `0,0` 与"几何未填充"一致；`19/51/108` 等需对照页面定义逐字段确认（**不要再按尾部猜测**） |

**因此下一步必须换手段**：在 `trigger_t2_runtime_start()` 内加**一次性可读回诊断**（把 `s_scan_step_mdeg` 与 `window_begin_scan()` 的返回值写进快照中**下标已知**的字段，或写进一个专用诊断页），用台架确认：
1. `start()` 是否真的被 `T2:STARt` 调用（也可能是别的启动入口）；
2. 进入该分支时 `s_scan_step_mdeg` 是否为 5000；
3. `window_begin_scan()` 的返回值（0=成功）。

**已顺手清理**：临时 `T2RAW` 打印已从两处采样点移除（该诊断按需再加，且每次只打印一次）。

### A.3.5b 修正：串口层**没有**长度上限；且 `len=67` 采样早于 A.3.3 的修复（2026-10-13）

- 已查：`t2_workflow.send()` 直接委托 `board_command(...)`，共享层 `tools/common/scpi/*` 中**未发现**回复长度/字节上限 ⇒ **截断假设减弱** ✗（并非已排除，但不再是首选）。
- 已查：Round 67 的 `len=67`、尾部 `['…','19','3774','3239','3827','0','0']` **取自 A.3.3 修复之前**的运行——当时窗口确为禁用 ⇒ 那些非零值与"窗口被禁用"矛盾，说明它们**不是**窗口块字段（或页面在窗口块之后仍有字段）。
- **必要动作**：在 A.3.3 修复**已刷写**之后重测一次（本轮 `t2` 已通过：`passed=True/configured=4/publishing=6`），仍按 A.3.4 的方式（在采样步骤函数内、已知作用域）把**原始回复整行**落到运行目录，用于：
  1. 判断窗口是否真的在跑（`window_entered_count > 0`、`scan_geometry.positions == 3`）；
  2. 用已知发送值（`step_mdeg=5000`、`ppd=1000`）锚定窗口块的真实位置，一次性解决偏移问题。

**不要再依据 Round 67 的尾部数据下任何结论。**

### A.3.5 更强的假设：**回复被截断**（`len` 恒为 67 ⇒ 尾部不是页面真正的末尾）

A.3.3 的健康轮诊断中，**两次采样的 `len` 恒为 67**，且尾部 12 字段结构雷同（`…,'19','3774','3239','3827','0','0'` 与 `…,'19','4271','3858','4324','0','0'`）。而按 `.inc` 的书写顺序，窗口块（9 个字段）确实位于**页面末尾**（events 之后）。两者矛盾 ⇒ 最可能的解释是**工具读到的是被截断的回复**（固定读取上限），于是 `state[-9:]` 取到的是"页面中部的若干个零/段最大值"，`geometry 0pos/0pul` 只是**恰好两个 0**。

**验证方法（下次按序做）**：
1. 在采样处打印 `len(raw_reply_text)`（**原始文本长度**）与 `len(state)`；若两者不随页面内容变化而恒定，即为固定上限；
2. 查工具的读取路径（`send()`/串口读循环）是否存在固定长度/固定行数上限或提前结束条件；
3. 确认后：把读取改为"读到帧结束/预期字段数"或为 `T2?` 增加一个**只返回尾块**的查询（如 `T2:WINdow?`），从根上避开位置化解析。

**注意**：A.3.3 的修复（删掉冗余预配置）**已提交并台架验证流程仍通过**（`passed=True / configured=4 / publishing=6`）；"窗口是否真的在跑"仍需按本节验证后再下结论——**不要**再用被截断的尾部去判断。

### A.3.4 获取完整回复的正确做法（2026-10-13）

为把 `T2?` 的 67 个字段与页面定义逐一对照，我尝试在采样打印块里加"一次性完整回复转储"，条件是 `slot == 0 and tick == 0`——**没有输出**（该块里这两个名字不是那样的语义，条件恒假），而 `t2` 仍 `passed=True`、工具无异常 ⇒ 该尝试无效（已回退）。

**正确做法（下次直接照做）**：
1. 不要在打印表达式里加条件式转储；改为在**采样步骤函数**内、已知变量作用域处，用 `ctx.log(...)`/写文件方式把**原始回复整行**落到运行目录（例如 `out/tdma_t2/<stamp>-t2-raw-<slot>.txt`），每次运行只写一次；
2. 或者把"完整回复"作为**步骤产物**保存（与 `t2_flow.json` 同级），便于离线用页面定义逐字段对齐；
3. 对照时以**已知发送值**为锚（`step_mdeg` 应为 5000、`start/zero/terminal` 为 5000/10000/15000、`ppd` 为 1000），可一次确定窗口块的起始偏移。

**本轮健康轮已采集到的可用基准**：回复长度 **67**；尾部 12 个字段为
`['1382340','0','0','0','0','0','19','3774','3239','3827','0','0']`（第二轮为 `['1382340','1387338','1387340','0','0','0','19','4271','3858','4324','0','0']`），
其中末尾两个 `0/0` 与"几何未填充"一致，而 `19 / 3774 / 3239 / 3827` 需在拿到完整回复后再判定归属（不要先入为主认定为窗口字段）。

### A.3.3 决定性实测：**并行窗口在台架上并未运行**（2026-10-13，健康轮）

在一次**健康**的 `t2` 运行（`passed=True`、`published_mask 3→7`）中，临时诊断打印了 `T2?` 回复长度与尾部 12 个字段：

```
diag len 67 tail ['1382340','0','0','0','0','0','19','3774','3239','3827','0','0']
```

两条结论：

1. **窗口确实没跑**：尾部既无发送的 `step_mdeg=5000`，末尾两个几何字段也是 `0/0` ⇒ `window_step_mdeg`/`scan_geometry` 未被填充。因此此前所有"announced/entered"读数（含 0）以及"几何 0pos"都**不是**解析错位造成的，而是**窗口真的没被启用**。
2. **`state[-9:]` 的偏移也确实不对**（尾部是别的内容），故采样行已恢复为诚实的 `?`（诊断打印已撤）。

**已排除**：STEP setter 未被调用（命令被接受 ⇒ setter 返回 true ⇒ 静态已写入）；`configure()`/`start()` 会 memset `s_state` 但**不会**清 `s_scan_step_mdeg`（文件级静态），且调用点在 `start()` 的 memset **之后**。

**下一步（按可能性）**：
1. **确认刷入镜像真的含该调用**：读 `trigger_t2_runtime_start()` 中插入点前后（`s_state.active = 1u;` 之前）的**实际源码**，并核对最近一次 `run` 的构建指纹与提交是否包含 Round 50 的改动；
2. 若源码与镜像都正确，则在 `start()` 里加**一次性诊断**（把 `s_scan_step_mdeg` 与 `window_begin_scan` 的返回值写入一个可读回字段/诊断页），用台架确认分支是否进入；
3. 顺带把"窗口块在 `T2?` 中的真实偏移"用**健康轮**的完整回复核对（本轮已给出 `len=67` 与尾部样貌，可作为解析的基准数据）。

### A.3.2d 校正：窗口尾部读数的自相矛盾**可能来自短/超时回复**，不必然是偏移错（2026-10-13）

A.3.2c/采样行的实测矛盾值（`entered 0` 与 `late 18` 并存、`step_mdeg 104`、`geometry 0pos`）出现在一次**失败的 `t2` 运行**中（同一轮 `publishing=0`、`reason 1`，且工具自身注释指出"超时回复只有一个字段"）。短回复会让**任何**尾部切片读到别处的字段 ⇒ 矛盾值既可能是"偏移算错"，也可能是"**回复本身不完整**"。

**区分方法（下次务必先做）**：
1. 在**健康板/健康轮**上读同一字段：若 `step_mdeg` 等于发送值（5000）、`entered/announced` 内部自洽，则偏移正确，矛盾值只来自短回复 ⇒ 需要的是**完整性判据**（如先比较回复字段数与预期下界）；
2. 若健康轮仍不匹配，再按 A.3.2c 选项①（页面自描述块长度）修偏移。

**当前处置**：采样行已停印该切片（显示 `?`），因此不会再把短回复的错位值当作证据。恢复的前提是**先有完整性判据**（或页面自描述）。

### A.3.2c 结论：freeze 最大值**没有稳定下标**，只能靠页面自描述（2026-10-13）

进一步查明 `tools/sync_trigger/t2_workflow.py` 的具名下标表（约 951 行）只覆盖到
`preview_saved: 40`；而 `T2?` 页面里 freeze 路径最大值（4 个）位于更后面，且与页面末尾之间隔着
`freeze_no_preview_mask/count`（2）与 **events 块（`2 × position_id/action_q`，条数运行期可变）** ⇒

- `state[-4:]` ✗（现在读到窗口字段）
- `state[-13:-9]` ✗（读到 events 的最后两对）
- 任何"倒数 N 个"✗（events 长度可变）

**安全选项（择一）**：
1. **页面加字段计数前缀**：`T2?` 首字段给出"本回复字段总数"或各追加块的长度，工具据此定位；
2. **扩展具名下标表**并把 freeze 最大值登记进去（需要它们在页面中的**固定位置**——即要求"events 之后、窗口之前"不得再插块，并由守卫断言块顺序）；
3. **暂不显示**：把该 4 个值从采样行移除或改标为"tail diagnostics"，等 1 或 2 落地再恢复（避免**贴错标签的读数**被当成证据）。

建议先做 3（一行删除，消除误导），再做 1（一次性给所有追加块自描述，根治此类问题）。

### A.3.2b 同类隐患：工具里"T2? 以 X 结尾"的位置假设已过时（2026-10-13）

`tools/sync_trigger/t2_workflow.py` 有一处 `freeze = [value.strip() for value in state[-4:]]`（约 2475 行），
注释是"T2? ends with the origin FREEZE path maxima"。但 2026-10-24 的窗口改造把**窗口块追加到了页面末尾**，
因此 `state[-4:]` 现在读到的是**窗口/几何字段**而不是 freeze 最大值——**输出被贴错标签**（不报错）。

**修法（已更正）**：**不要**用尾部切片去猜 freeze 最大值的位置——按 `.inc` 的实际顺序，`freeze_cycles`（4 个）位于**很早**处（`scpi_trigger_t2_runtime_commands.inc` 227-231 行），其与页面末尾之间还隔着 `freeze_no_preview_mask/count` 与 **events 块**（条数运行期可变）⇒ `state[-13:-9]` 这类写法同样会读到 events 字段。
正确做法：用工具里已有的**具名/下标访问**（`t2_field(state, …)` 或固定的索引常量）读 freeze 最大值，或在 `T2?` 页面为各追加块加**长度前缀**、并让 `test_t2_page_field_order.py` 断言**各块的相对顺序**。

**教训（与 A.3.2 同源）**：追加式页面里**任何"末尾 N 个"的假设都会随下一次追加失效**；守卫需要断言的是**块的相对顺序**，而不只是新块内部的顺序。

### A.3.2 待修：`window …` 采样尾巴加在了**另一处**打印块（2026-10-13 发现）

`tools/sync_trigger/t2_workflow.py` 里有两处采样打印块，`t2` 流程实际走的是**后一处**（`rx {fields[12] …}` 附近，约 2471 行），而 2026-10-12 的改动只加到了**前一处**（约 846 行）。因此台架日志里**看不到**窗口健康事实（`window announced …/entered … overwrote_unannounced … geometry …pos/…pul`）——第三次 `t2` 运行日志中确认缺失。

**修法**（简单直接，别用正则改写整块）：在后一处（约 2471 行）的 `f" rx {fields[12] …}"` 之后、`, flush=True)` **之前**插入同样的三行，使用内联切片而非变量：

```python
                      f" | window announced {state[-9:][1] if len(state) >= 9 else '?'}"
                      f"/entered {state[-9:][0] if len(state) >= 9 else '?'}"
                      f" overwrote_unannounced {state[-9:][2] if len(state) >= 9 else '?'}"
                      f" step_mdeg {state[-9:][6] if len(state) >= 9 else '?'}"
                      f" geometry {state[-9:][7] if len(state) >= 9 else '?'}pos"
                      f"/{state[-9:][8] if len(state) >= 9 else '?'}pul",
```

**为什么用尾部切片**：窗口字段是 `T2?` 回复的**末尾**追加块（顺序由 `tests/python/test_t2_page_field_order.py` 守卫），取 `state[-9:]` 对页面中部的未来增长免疫。
**注意**：第一次尝试用正则重写整块导致 Python 语法错误（把 `flush=True)` 切断），已回退——请用"插入若干 `f""` 相邻字符串行"的方式，不要动原有的括号结构。

### A.3.1 测试环境坑：pytest basetemp 被卡住会伪装成"代码坏了"（2026-10-13 实测）

一次 C harness 测试运行**超时挂起**（`subprocess.TimeoutExpired`）后，`out/pytest/runs` 目录被残留句柄锁住，
pytest fixture 的 `rmtree` 随之失败，**之后所有同类测试都以 setup ERROR 立即失败**（0.5 s 内、无断言输出）。

**症状与误判**：看起来像"回退后代码坏了"，实为**临时目录被卡死**。判定方法：用全新 basetemp 复跑——

```powershell
python -m pytest tests/python/test_turntable_rolling_window.py -p no:cacheprovider -q `
  --basetemp="$env:TEMP\pytest-t2-<stamp>"
```

实测：换 basetemp 后立即 **95 passed**（T2 全套），证明工作树无恙。

**处置**：出现 setup ERROR 或挂起时，先换 basetemp 判定真伪，再决定是否怀疑代码；被卡住的 `out/pytest/runs` 需另行清理（或保持使用 TEMP basetemp）。

**另有一处未解**：本轮尝试为迁移加"按序号问宣告"的访问器 `trigger_turntable_window_is_announced()`（内部走 `window_find`）时，
C harness 测试出现**挂起**而非编译错误；因无法在当轮定位已回退。该访问器**非必需**——迁移可直接调
`window_find()` 后读 `entry->announced`，故不阻塞 A.3。留作待查项。

#### A.4.0 已实现待重放的改动 + 当前阻塞（2026-10-13）

**（一）加 STEP 的改动已写好并静态验证通过**（因凭证受阻而回退，可逐字重放）：

1. `scpi_trigger_t2_runtime_commands.inc`：声明加 `step_mdeg`；在 `terminal_angle` 的解析行后追加
   `SCPI_ParamInt32(context, &step_mdeg, TRUE) != TRUE ||`；在 `trigger_t2_runtime_config_t config={0};`
   之前插入 `if (!trigger_t2_runtime_scan_step(step_mdeg)) return scpi_trigger_t2_fail(context);`
2. 4 处调用点追加 `,5000`（其扫描为 5/10/15°）：`t2_workflow.py` 三处（`…999,0,0,0` 那处与两处 `…,15000`）、`t2_bench_probe.py` 一处；
3. `tests/python/test_t2_configure_signature.py` 的 `EXPECTED_PARAMETERS` 末尾加 `"step_mdeg"`。

静态验证：协调守卫 2 项 + T2 全套 95 项通过；验收 attempt 2 的失败点在 DPLL 步骤（见下），**说明该改动编译通过**。

**（二）当前阻塞：验收在 `validate_tdma_stopped_handoff()` 处失败（`nodes board set mismatch`）**

`p3_hardware_acceptance.py:1320-1322` 要求停机上交流程的 `stopped` / `boards` / `board_records` / `nodes`
四个字典的**键集都等于验收期望的 `board_ids`**。实测同一摘要（`<run>/tdma-process-image/summary.json`）：

| 字段 | 内容 |
|---|---|
| `stopped` / `boards` / `board_records` | **完整 4 板** ✓ |
| `actions`（88 条 START/STOP） | 覆盖 4 板 ✓ |
| **`nodes`** | **空** ✗ |

⇒ **不是缺板**，是采集工具**没有填 `nodes`**：该字段由自主记录证据路径写入（`tools/calibration_ring_validate/tdma_autonomous_record.py` 的 `evidence["nodes"][uid] = …`），本次为空。下一步应查 `trn03_closed_loop.py` 组装摘要时 `nodes` 的来源，以及它在什么条件下会保持为空（超时/取消/异常路径）。**该阻塞挡在所有固件提交之前**，优先于 T2 自身实现。

#### A.4.3 两条来自实测的教训（2026-10-13）

**① 位置参数式命令的参数顺序 = 处理器读取顺序；守卫必须按源码顺序校验。**
`pulses_per_degree` 一度被解析在角度**之前**（为迁就守卫的分组收集），而调用点把它追加在**最后**（第 12 位）⇒ 位置参数错位、`T2:CONFigure` 被拒（台架表现为 `targets ['0','0','0']`、`boards_configured=0`、失败步骤 `[12,16,18,19,20]`）。
教训：`test_t2_configure_signature.py` 原先**分两趟**收集（先 `scpi_trigger_grid_uint` 再 `SCPI_ParamInt32`），恰好掩盖了交错错误；已改为**单趟按源码顺序**收集。**分组校验会漏掉交错错误。**

**② `run` 与 `t2` 的验证职责不同：改固件后必须先 `run` 再 `t2`。**
- `p3_hardware_acceptance.py run`：**构建 + 刷写** + 提供 pre-commit 凭证；
- `p3_hardware_acceptance.py t2`：只跑 T2 流程，**用的是板上已有镜像**（不构建、不刷写）。

因此"改了源码→跑 `t2` 仍失败"极可能只是**旧镜像**在跑（本次连续三次误判即由此）。正确顺序：`run`（刷写+凭证）→ 提交 → `t2`（验证）。刷写后 `t2` 立即 `failed steps: none` ✓。

**③ 相关：STEP setter 不应读 `s_state.active`。** `trigger_t2_runtime_configure()` 会 memset 状态，且 SCPI 配置路径本就有 RUN 期禁配检查 ⇒ 该守卫只会拒掉合法的重配置（实测把 step 16 也拖下水）。现仅校验 `step_mdeg > 0`。

#### A.4.1 第 1 步（加 STEP）的确切改动面

`scpi_cmd_trigger_t2_configure()` 现有参数（按解析顺序，`scpi_trigger_t2_runtime_commands.inc`）：

```text
run_id, source_slot, target_mask, origin, pre_roll, post_roll, threshold,
start_angle_mdeg, zero_angle_mdeg, terminal_angle_mdeg        ← 10 个参数，无 STEP
```

台架工具侧共 **4 处**发送该命令，加参数必须同步（否则工具立即被拒）：

| 文件 | 行 |
|---|---|
| `tools/sync_trigger/t2_workflow.py` | 214（配置打印）、520（T2 挂载）、1718（采样前配置） |
| `tools/sync_trigger/t2_bench_probe.py` | 242 |

现状（据此可核对改造前后差异）：台架发送 `…,0,14,{origin},5000,5000,950,5000,10000,15000`，即 **pre/post roll = 5000 毫度（5°）、阈值 950‰、位置 5°/10°/15°**，配合写死的 `pulses_per_degree=1000` ⇒ 三个位置对应 5000/10000/15000 脉冲——这正是台架日志里 `targets ['5000','10000','15000']` 的来源，也说明当前台架**只跑 3 个位置**（与 `TRIGGER_T2_RUNTIME_POSITION_COUNT = 3` 一致），而非 361。

⇒ 第 1 步的验收：命令与 4 处工具同步后，`T2:CONFigure` 能带 STEP 下发，且 `READ` 侧读回 `scan_geometry.positions` 与扫描参数一致（0..360°@1° 时为 361）。

转台模型族（`CONFigure:MODEl:TURNtable:{LOAD,TRIGger,MOTion}`）仍是**转速等运动参数**的落点（`MOTion <velocity>,<accel>` → `model_turntable_configure_motion()`），不是扫描几何的落点——先前把两者混为一谈是错的。

### A.5 已结案：所谓"漏宣告 / mask 上报少一位"（2026-10-12）

**结论：代码无缺陷，是测试记账错误**。`window_advance` 的宣告与上报都正确：状态 `entered=361 / announced=361 / overwrote_unannounced=0`，调用侧把 `announcements` 初值改为 `s.window_announced_count` 后同为 361。根因是测试在大循环处才把该计数置 0，而**第一次宣告发生在更早的 `advance(count=5)`**。

保留的永久不变式：状态级 `announced == entered == 361` 且 `overwrote_unannounced == 0`；调用侧 `Σ popcount(mask) == 361`；全程 `threshold ≤ target` 且 `margin ∈ {95, 0}`。

留档教训（同一问题四次反复）：① 只有总数时不可下"丢失"结论（分不清"状态未宣告"与"上报未反映"）；② 状态内部事件不能用调用后的外部采样归因（宣告与移出在同一次调用内完成）；③ 诊断输出必须能让测试失败（pytest 只在失败时显示 stderr，"沉默"≠"相同"）；④ **计数类断言必须问"这个计数从哪里开始计"**。
