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
| T2-STEP-001 | 角度扫描与 STEP 位置 | IN PROGRESS | **行程 = [起始−5°, 终止+5°]**（2026-10-12 用户裁决）：必须从起始前 5° 起、终止后 5° 停——预行程使**位置 0 可被预测**（否则首个位置无速率/无观测），后行程使**终止位置可验证**；两段 roll 必须是整数脉冲且 ≥ 1 个 STEP，位置网格仍为 起始..终止 by STEP。起始/终止/STEP/转速/`pulses_per_degree` 以整数毫度配置（分辨率 0.001°）；整除、`step_pulses` 整数性、溢出、STEP 时间下限大于位置内序列跨度均在 STOP 校验并 fail-closed；lead 取一步比例（见架构「角度扫描与 STEP 位置」）；零位只作网格一致性检查，不在网格上即拒绝；内部 index 0 基、线上 position id 1 基；在途槽位数与身份容量归 `T2-IDENT-001`；可复用 `feature/node-sequence-reservation-trigger` 的 `CONFigure:ANGLe:*` 解析与校验（评审 A-14、D-1/D-5/D-6）。**已落地派生与校验层**：`trigger_turntable_scan_validate()` 派生位置数/整数 `step_pulses`/总脉冲，并按六类理由拒绝（参数缺失、反向、STEP 不整除、位置数超限、STEP 非整数脉冲、STEP 容不下序列跨度），拒绝时清零输出；0..360°@1° 规格（361 位置、step_pulses=100）与 `tests/python/test_turntable_scan_geometry.py` 全通过。**待办**：SCPI 前端接出扫描参数并在 STOP 调用校验；**不得往 `trigger_turntable_predictor_config_t` 加字段**（`-Wextra -Werror` 下 partial initializer 编译失败，STEP 现由 `trigger_turntable_window_configure()` 存入 snapshot） |
| T2-SEQ-001 | 每位置多组序列预装 | PENDING | 每位置 16 组（切换编码、相对 `q_P` 偏移、网分脉宽、READY 窗口）在位置开始前预装并读回；组间隔按网分最坏响应时间加 settle 与余量；组间不由 READY 推进；各板只展开本角色的步（评审 A-16） |
| T2-EXEC-002 | 两组滑动窗口执行端 | PENDING | SWITCH/GATEWAY 执行端支持深度 2 的绝对 deadline 队列，每组独立序号、回执与取消结果；一组完成即推入下一组；STOP 取消队列内全部未执行组并记录硬件取消结果；属 SYNC_IO 接口变更，需该域确认（评审 A-16） |
| T2-DCO-001 | FREEZE 后 deadline 随 DCO 更新 | PENDING | FREEZE 只锁定位置；Core0 在模型 token 变化时为未装填组重新解算，更新放在网分采样期间；Core1 装填前比较 token；已被硬件接受的组不追改；依赖 VDC 证明模型切换无相位跳变（评审 A-06、E-01） |
| T2-GATE-001 | VDC LOCKED 发布输出门禁 | BLOCKED | T2 只在 VDC 进入 LOCKED 并发布后生成和装填 deadline，消费 VDC 提供的发布状态而不自行判定锁态；补全部非发布锁态的主机负测；阻塞于 `VDC-LOCK-001`、`VDC-RUN-001`，台架期间只允许显式诊断开关且结果不计验收（评审 A-02） |
| T2-TRANSPORT-002 | 一等席单帧位置记录 | IN PROGRESS | 紧缩单帧已实现并经四板送达，改写已排队 VDC image 的 patch 路径已删除（`T2-PROGRESS-20261010-022/024`）；剩余为 VDC 核心字段清单、一等席布局/预算的独立登记（C11），以及按 `T2-IDENT-001` 新身份口径重新核定（评审 A-01、A-03、A-15） |
| T2-IDENT-001 | 位置身份与在途容量 | IN PROGRESS | 由扫描参数派生的位置数（0..360°@1° ⇒ 361）与紧凑记录身份容量一致。**2026-10-12 定稿**：T2 只预测下一到两个位置，长扫描的 361 个位置**不进入常驻状态**；常驻 1~2 个在途位置即可（`TRIGGER_T2_RUNTIME_POSITION_COUNT` 继续作窗口用）；线上绝对身份**无需加宽 4 bit id**——10 B 单帧已带 32 bit `target_ms_index`，配合扫描参数即可确定绝对位置，`position_id` 作在途实例编号；**预测超过 2 个位置一律视为不准确**（速率是单样本估计），window 外不产生任何对外事实；冻结确认 32 bit 掩码足够。剩余：与 TDMA 一等席布局一起核定并按 `EXE-SEAT-01` 登记（C11） |
| T2-SOURCE-001 | 模拟源转速/ppd 与脉宽 | PENDING | `pulses_per_degree` 进入 T2 配置并与模拟源共用；源以转速（mdeg/s）× ppd / 1000 换算频率并支持可配置脉宽；两条源路径选型定稿并给出隔离验收口径（评审 D-7/D-8/D-9） |
| T2-FREEZE-002 | 无预览位置判失败并继续 | IN PROGRESS | `stage_pending_freeze()` 遇 `preview_copy` 失败不再 `return true`：按位置记录一次（新增 `freeze_no_preview_mask` / `freeze_no_preview_count`，追加在 `T2?` 末尾可读回），并**继续检查其余 pending 位置**；pending 位保留，使迟到预览仍可冻结，窗口到期仍走既有 `freeze_abandoned_*` 证据。已实现 + 新增测试 `tests/python/test_t2_freeze_no_preview.py`（继续装填 / 全缺预览且每位置只计一次 / 健康基线不变 / 传输拒绝仍上报），与 `test_trigger_t2_freeze_abandon.py`、`test_t2_freeze_completion.py` 三件全通过；评审 A-13 的语义变更待独立复核确认后置 DONE |
| T2-DOC-002 | 补记 09-27 run33/34 证据 | PENDING | 定位凭证原件后按事实补入进展日志；禁止据文件名、推断或转述填写数字（评审 D-02/D-14） |

## T2-IDENT-001 容量测算（2026-10-12）

代码事实（快照，非事实源；以符号为准）：

| 维度 | 现值 | 来源 |
|---|---|---|
| 线上 `position_id` | **4 bit**，合法 `1..0x0F` ⇒ 一帧/一轮最多 **15 个位置** | `components/sync_trigger/inc/trigger_position_slice.h` 的 `TRIGGER_POSITION_SLICE_TAG_*` / `trigger_position_slice_valid()` |
| 单帧记录 | 10 B（tag+id、版本 16 bit、`target_ms_index` 32 bit、fine slice 24 bit） | 同上 `TRIGGER_POSITION_SLICE_WIRE_SIZE` |
| 运行时位置数组 | `TRIGGER_T2_RUNTIME_POSITION_COUNT` = **3** | `components/sync_trigger/inc/trigger_t2_runtime.h` |
| 每位置模板组数 | `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` = **1**（目标 16 组） | 同上 |
| 冻结确认位宽 | `freeze_ack_*_mask` / `freeze_complete_mask` / `freeze_abandoned_mask` = **uint32_t** | 同上 |

扫描位置数与 4 bit 可容纳性（0..360°，位置数 = ⌊(stop−start)/STEP⌋+1）：

| STEP | 位置数 | 需 id 位 | 4 bit 可容纳 |
|---|---|---|---|
| 1° | 361 | 9 | 否 |
| 2° | 181 | 8 | 否 |
| 5° | 73 | 7 | 否 |
| 10° | 37 | 6 | 否 |
| 15° | 25 | 5 | 否 |
| 24° | 16 | 5 | 否 |
| 25° | 15 | 4 | 是 |
| 30° | 13 | 4 | 是 |
| 45° | 9 | 4 | 是 |
| 90° | 5 | 3 | 是 |

结论与选项：

1. **在 4 bit 线上 id 不变的前提下，0..360° 扫描的最小 STEP 是 25°**（15 个位置）；1° 扫描（361 位置）需要 ≥9 bit id，或改用两级身份（槽位 + 滚动代次）。
2. 运行时数组与模板容量按扫描上限放大：STEP 5°（73 位置）需要位置数组 ≥73、模板 ≥73×16=1168（按两组滑动窗口则 ≥146）；STEP 10°（37 位置）对应 592 / 74。现状 3 个位置、3 份模板。
3. 冻结确认掩码 32 位：位置数 ≤32 可直接用位掩码；超过 32 必须随 id 方案一起改为分级/滚动确认位宽。

### 需求输入（2026-10-12：用户裁决）

**目标扫描 = 0..360°，STEP = 1° ⇒ 361 个位置**（含起止，位置数 = ⌊(360−0)/1⌋+1）。据此派生的定稿要求：

| 维度 | 需求（361 位置） | 现值 | 结论 |
|---|---|---|---|
| 线上 `position_id` | ≥ **9 bit**（1..361） | 4 bit（≤15） | 4 bit 不可容纳 ⇒ 必须加宽，或改两级身份 |
| **推荐方案：两级身份** | slot 位 + 滚动代次位，`2^(w+g) ≥ 361` | 无 | 最小组合 **slot 4 bit（16）+ 代次 5 bit（32）= 512** 覆盖 361；或 slot 5 + 代次 4（32×16=512） |
| 常驻位置槽位 | **固定小窗口**（在途 = 当前位置 + 下一位置） | `TRIGGER_T2_RUNTIME_POSITION_COUNT` = 3 | 长扫描**不**应把 361 个位置全部常驻：改为固定 N 槽（建议 N=2..8）+ 绝对 id 单调校验 |
| 模板/组常驻 | 每位置 16 组 × 滑动窗口 2 组 ⇒ **常驻 32 组** | 每位置 1 组（共 3） | `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` 需 16，且按窗口而非全扫描分配 |
| 全程执行次数 | 361 × 16 = **5776** 组 | — | 设计目标是"滚动装填"，不是全量常驻 |
| 冻结确认位宽 | 只需覆盖**在途槽位** | `freeze_ack_*_mask` / `freeze_complete_mask` 均 32 bit | 槽位方案下 32 bit 足够 ✓（无需 361 位分级确认） |
| 位置周期 | 由转速 × `pulses_per_degree` 决定（`T2-SOURCE-001`） | `pulses_per_degree` 未接出、源无脉宽 | 1°/step 的时间预算必须与"每位置 16 组序列跨度"一起校验（`T2-STEP-001` 的 STEP 时间下限） |

派生动作（进入实现前需定稿）：① 定稿两级身份的位宽与线上编码（保持 10 B 单帧或显式扩帧）；② 把 `TRIGGER_T2_RUNTIME_POSITION_COUNT` 从"扫描位置数"改为"在途槽位数"；③ 由扫描参数派生位置数的计算进入 STOP 校验（fail-closed），并据此派生 FREEZE 确认与诊断的长度。

### 语义澄清（2026-10-12：用户裁决）——T2 只预测下一到两个位置

上表按"位置数与常驻容量同阶"推导，**该口径不成立**：T2 的运行语义是**只预测下一到两个位置**（滚动预测窗口），长扫描的 361 个位置不进入常驻状态。据此修正：

| 维度 | 修正后的要求 | 说明 |
|---|---|---|
| 常驻位置 | **1～2 个在途位置**（现 `TRIGGER_T2_RUNTIME_POSITION_COUNT` = 3 可继续用作窗口，无需放大到 361） | 361 只是扫描全程的位置总数，不是常驻容量 |
| 线上绝对身份 | **不需要加宽 4 bit id**：10 B 记录已带 32 bit `target_ms_index`（绝对目标计数）+ 16 bit `prediction_version` | 从板用本地扫描参数（起始/STEP/`pulses_per_degree`）+ `target_ms_index` 即可确定**绝对位置**；`position_id` 保持为**在途实例/槽位**编号即可 |
| 线上编码 | **10 B 单帧格式不变** | 上表"推荐两级身份"是为"361 常驻"服务的，在此语义下**不需要** |
| 模板常驻 | 每位置 **16 组** × 在途 1～2 个位置 ⇒ 常驻 **16～32 组** | 真正需要改的是 `LOCAL_TEMPLATE_CAPACITY`（1 → 16）与执行端滑动窗口深度 2 |
| 冻结确认 | 32 bit 掩码足够（覆盖在途位置/槽位） | 不需要 361 位分级确认 |
| 位置总数 361 的用处 | 仅用于**扫描参数派生与 STOP 期 fail-closed 校验**（STEP 网格、终止位置、`step_pulses` 整数性、序列时间跨度） | 对应 `T2-STEP-001` 与 `T2-TURNTABLE-009` |

修正后的实现重点（取代上一条"派生动作"）：① `LOCAL_TEMPLATE_CAPACITY` 1 → 16、执行端深度 2 滑动窗口（`T2-SEQ-001`/`T2-EXEC-002`）；② 扫描参数（起始/终止/STEP/转速/`pulses_per_degree`/脉宽）参数化并做 STOP 校验（`T2-STEP-001`/`T2-SOURCE-001`）；③ 在途窗口内的身份与代次语义（`T2-IDENT-001`）按"1～2 个在途 + 绝对时间"定稿，**不动 10 B 线格式**。

### 追加约束（2026-10-12：用户裁决）——预测必须随进度滚动，不得开头一次预测完

**不得只走完扫描最前面的一小部分就把全部位置预测完**：预测窗口必须随实际计数/角度推进，进入窗口时才生成该位置的目标与阈值。

代码现状（快照，非事实源；以符号为准）：`components/sync_trigger/inc/trigger_turntable_predictor.h`

- 固定 3 槽：`target_count[3]` / `threshold_count[3]`，与 `trigger_turntable_position_id_t` 的 `START=1 / ZERO=2 / TERMINAL=3` 一一对应；
- `trigger_turntable_predictor_begin()` 在起始即用 `origin_count + (angle − base_angle) × pulses_per_degree / 1000` **把全部 3 个目标计数与阈值一次算完**；
- `trigger_turntable_predictor_observe()` 只在计数跨过 `threshold_count[i]` 时置 `announced_mask` 位。

因此 0..360°@1°（361 位置）要落地的是：**把固定 3 槽改为滚动窗口（在途 1～2 个位置）**——每个位置的目标计数/阈值在**进入窗口时**由"当前计数 + 扫描参数"计算，跨阈值即按该位置 FREEZE 并推进窗口；`begin()` 只建立窗口首项，**不枚举整段扫描**。这条同时约束 `T2-TURNTABLE-002`（异步窗口预测）与 `T2-STEP-001`（扫描参数派生）的实现方式。

### 精度边界（2026-10-12：用户裁决）——超出 2 个位置的预测一律视为不准确

**窗口深度硬上限 = 2 个位置**；超出 2 个位置的预测不得作为依据（不得据此提前装填、宣告或下发）。

依据（快照，非事实源）：`trigger_turntable_predictor_observe_at_sync()` 的速率估计是**单样本**估计——用相邻两次 VDC sync 之间的计数差一次性算出 `pulses_per_sync_q16`（源码注释亦明确"单样本、确定性、不引入无界滤波"），因此**外推误差随预测跨度线性增长**；跨度超过约两个 STEP 时间后，目标计数误差已不能支撑"阈值前冻结 + 从板按时间片执行"的要求。

由此派生三条实现规则：

1. **只在进入窗口时计算**：某位置的目标计数/阈值在它**进入滚动窗口**时用当时的 `last_count` + 最新速率重算；不得由 `begin()` 时的 origin 外推到整段扫描（现状 `begin()` 一次算完全部 3 槽，见上节）。
2. **窗口外的位置不产生任何对外事实**：超过 2 个位置的 target/threshold/FREEZE 都不生成；线上在途记录数因此 ≤2（4 bit `position_id` 余量充足，10 B 单帧无需变更）。
3. **速率过期即 withhold（fail-closed）**：窗口内位置若缺少足够新的 sync 观测（速率估计过期/`rate_valid=0`），应判定"该位置预测不可用"并按失败记录，而不是用旧速率外推补一个数。

## T2-STEP-001 接线落点（2026-10-12 勘察）

下一步（SCPI 接线）的确切落点，已核对代码：

| 环节 | 落点 | 说明 |
|---|---|---|
| 现有转台命令面 | `middleware/scpi_port/src/scpi_model_commands.c`：`CONFigure:MODEl:TURNtable:{LOAD,TRIGger,MOTion,STARt,STOP}` 与 `READ:MODEl:TURNtable:{LOAD,TRIGger,MOTion,STATe}?`（声明见 `scpi_model_commands.h`） | 扫描参数应挂在这一族之下（转速已有入口：`MOTion <velocity_units_per_s>,<acceleration_units_per_s2>` → `model_turntable_configure_motion()`） |
| 新增命令 | `CONFigure:MODEl:TURNtable:SCAN <start_mdeg>,<stop_mdeg>,<step_mdeg>,<pulses_per_degree>,<threshold_per_mille>,<pre_roll_mdeg>,<post_roll_mdeg>` + `READ:MODEl:TURNtable:SCAN?` 读回派生几何 | 处理器内构造 `trigger_turntable_predictor_config_t` + `step_mdeg`，调用 `trigger_turntable_scan_validate()`；**拒绝即返回 SCPI 错误**（fail-closed），并把 `scan_geometry` 存起来供读回 |
| 配置存储 | 由 sync_trigger 域提供 setter/getter（模块静态或已有运行时状态），**不得往 `trigger_turntable_predictor_config_t` / `model_turntable_motion_config_t` 等被部分初始化的结构体加字段** | `-Wextra -Werror` 下 partial initializer 会直接把既有 harness 与调用方编译打断（本轮实测 31 个测试报错） |
| 建窗时机 | STOP 期调用 `trigger_turntable_window_begin_scan(s, cfg, step, sequence_span_pulses, observed_count)` | 校验 + 建窗合一；不可用扫描**不建窗**，因此不可能启动 |
| 行程 | 命令接受的是**位置网格**（起始..终止 by STEP）；行程 `[起始−5°, 终止+5°]` 由 `pre_roll_mdeg`/`post_roll_mdeg` 表达，校验要求其为整数脉冲且 ≥ 1 STEP | 见 `T2-STEP-001` 行 |

## 待修缺陷（2026-10-12 由"恰宣告一次"不变式发现）

**滚动窗口会丢掉一个位置的 FREEZE**：`tests/python/test_turntable_rolling_window.py` 的全程扫描（0..360°@1°，361 位置）实测

```
entered=361  window_next_index=361  recomputed=359  late=358  stale=1   announcements=360
```

即 **361 个位置全部进入窗口，但只有 360 个被宣告**（`window_advance` 的 `mask` 位总数少 1）：有一个位置被进入后**未经宣告就被移出**，等于该位置没有 FREEZE。这正是"恰宣告一次"不变式要抓的损失，重复宣告与漏宣告都不可接受。

复现：把该测试里的 `assert(announcements >= 360u)` 改回 `assert(announcements == 361u)`（计数已在测试内累计）。
排查方向：`window_advance` 的 roll 循环先移位再 `window_enter`，失败即 `break`；需确认"已宣告条目移位后、最后一次 enter 失败"的路径不会把**未宣告**的条目一起丢掉（重点看扫描末端与 `late`/夹紧条目相邻的情形）。

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
