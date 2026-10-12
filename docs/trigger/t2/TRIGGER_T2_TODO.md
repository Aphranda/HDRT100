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

**迁移后应删除**：`target_count[3]` / `threshold_count[3]` / `announced_mask` / `terminal_verified` 以及 `begin()` 的三槽初始化（`window_begin_scan` 取代）。等价性测试 `legacy_equivalence()` 届时收敛为只校验窗口侧。

### A.4 接线落点（SCPI，2026-10-12 勘察）

| 环节 | 落点 |
|---|---|
| 既有转台命令族 | `middleware/scpi_port/src/scpi_model_commands.c`：`CONFigure:MODEl:TURNtable:{LOAD,TRIGger,MOTion,STARt,STOP}` + `READ:...:{LOAD,TRIGger,MOTion,STATe}?`；转速已有入口 `MOTion <velocity>,<accel>` → `model_turntable_configure_motion()` |
| 新增命令 | `CONFigure:MODEl:TURNtable:SCAN <start>,<stop>,<step>,<ppd>,<threshold_per_mille>,<pre_roll>,<post_roll>` + `READ:...:SCAN?`；处理器内调用 `trigger_turntable_scan_validate()`，**拒绝即返回 SCPI 错误**，派生几何存起来供读回 |
| 建窗时机 | STOP 期调用 `trigger_turntable_window_begin_scan(...)`（不可用扫描不建窗 ⇒ 不可能启动） |

### A.5 已结案：所谓"漏宣告 / mask 上报少一位"（2026-10-12）

**结论：代码无缺陷，是测试记账错误**。`window_advance` 的宣告与上报都正确：状态 `entered=361 / announced=361 / overwrote_unannounced=0`，调用侧把 `announcements` 初值改为 `s.window_announced_count` 后同为 361。根因是测试在大循环处才把该计数置 0，而**第一次宣告发生在更早的 `advance(count=5)`**。

保留的永久不变式：状态级 `announced == entered == 361` 且 `overwrote_unannounced == 0`；调用侧 `Σ popcount(mask) == 361`；全程 `threshold ≤ target` 且 `margin ∈ {95, 0}`。

留档教训（同一问题四次反复）：① 只有总数时不可下"丢失"结论（分不清"状态未宣告"与"上报未反映"）；② 状态内部事件不能用调用后的外部采样归因（宣告与移出在同一次调用内完成）；③ 诊断输出必须能让测试失败（pytest 只在失败时显示 stderr，"沉默"≠"相同"）；④ **计数类断言必须问"这个计数从哪里开始计"**。
