# T2 STEP 扫描、位置身份与模拟源复核汇总

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_STEP_REVIEW.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/trigger/t2/TRIGGER_T2_REVIEW.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`
Last updated: 2026-10-11

## 文档接口与范围

本文是 2026-10-11 的只读复核汇总，做三件事：把 T2 当前状态与 TODO 对账、核对两份外部审计（① predictor/runtime/wire 硬编码审计，② `feature/node-sequence-reservation-trigger` 角度前端复用审计）、把「31 个位置 + STEP + 零位 + 模拟源」决定的落地影响整理成可逐条裁定的清单。

- 本文是评审快照，**不登记为契约**（AGENTS.md 硬约束 6）；**不修改** TODO 状态、架构文档、登记表和任何代码。采纳项须由用户逐条裁定后，再分别回写 canonical 文档并走门禁与交叉审核（C11）。
- 基线：分支 `wip/tdma-real-flight-processing`，HEAD `2c338fe0`（2026-10-10），工作区干净；硬件凭证为 2026-10-10 台架批次。
- 证据等级：**已核实**＝复核读码或复跑；**实测快照**＝凭证文件或宿主度量，非事实源；**读码**＝单次读码未复读。
- 术语消歧：本文的 **STEP** 指角度扫描步长；与 `docs/trigger/TRIGGER_SEQ_STEP_DESIGN.md` 的 SEQ_STEP 编码序列步进模式无关，回写文档时须区分用词。

## 采纳状态（2026-10-11 回写）

用户审核后按本文建议回写文档，回写范围只含文档，不含代码；逐条落点如下。

| 决定 | 处置 | 落点 |
|---|---|---|
| D-1 STEP 派生口径 | 已采纳 | `TRIGGER_T2_ARCHITECTURE.md`「角度扫描与 STEP 位置」；`T2-STEP-001` |
| D-5 lead = 一步比例 | 已采纳 | 同上（含公式与理由） |
| D-6 零位与 index 基点 | 已采纳（内部 index 0 基、线上 position id 1 基；零位须落在 STEP 网格，否则 STOP 拒绝） | 同上；`T2-STEP-001`、`T2-TURNTABLE-004/009` |
| D-10 声明速度用途 | 已采纳（仅配置期校验与源换算） | 同上 |
| D-7/D-8/D-9 源、脉宽、ppd | 口径已冻结，选型未定 | `TRIGGER_T2_ARCHITECTURE.md`「模拟源与配置参数」；`T2-SOURCE-001` |
| D-2/D-3/D-4 身份与容量 | 未裁定，约束已写入并转任务 | `TRIGGER_T2_ARCHITECTURE.md`「位置身份与在途容量」；`T2-IDENT-001` |
| D-11 A-13 修法 | 未裁定，转任务 | `T2-FREEZE-002` |
| D-12 验收读数口径 | 已采纳（改用 priority accept/duplicate/reject） | `TRIGGER_T2_TODO.md` 的 `T2-TRANSPORT-001`、`T2-ACCEPT-001` |
| D-13 TODO 状态回写 | 已采纳 | `TRIGGER_T2_TODO.md`、`TDMA_DOMAIN_TODO.md` |
| D-14 漂移处理 | D-04 已采纳（`ARCH_T2_RESERVATION_ARCHITECTURE.md` 标 Deprecated 并加取代说明）；D-02 转任务 | 该文档、`docs/README.md`；`T2-DOC-002` |

未执行项及其原因：**登记表未新增 T2 行**——紧凑记录的跨域契约（id 位宽、冻结确认位宽、一等席布局）在 `T2-IDENT-001` 定稿前不具备冻结条件，且登记表状态变更按 C11 必须由独立于作者的审核方确认，本轮不得自审自批。该事项在 `T2-IDENT-001` 的退出门禁中保留。

## 一、结论摘要

1. **10-09 评审的五个硬阻塞已解除**：跨板位置送达、从板冻结与动作执行首次四板闭环（凭证 `out/HardwareAcceptance/20261010/t2-bench-probe-213024`，`passed=true`，实测快照）；A-01 改写 VDC image、A-15 分片、A-07 主机编译失败均已闭环。
2. **时间语义四处高风险偏离原样保留**：A-02 锁定门、A-06 模型冻结边界、A-05 瞬时失效全量作废、A-13 无预览即卡死 FREEZE。四项都在实时路径，必须先定口径。
3. **P0 需求缺口未动**：A-14 无 STEP、A-16 每位置 1 组（16 组 + 两组滑动窗口未开始）。
4. **TODO 状态表滞后于证据**：`T2-TRANSPORT-002`、`T2-P0-FREEZE-001`、`T2-ACTION-TEMPLATE-001` 仍写 PENDING；`T2-P0-CONFIG-001` 实为 PENDING。
5. **「31 个位置」会顶穿三处容量，是本轮最大的隐藏成本**：slice 内 position id 只有 4 bit（有效 1..15）、FREEZE latch 15 槽、冻结完成 ack 只有 8 bit；另有 descriptor FREEZE 分支写死 ≤3。这条决定必然触及 TDMA 一等席布局，不只是 predictor 改动。
6. **验收读数出现漂移**：从板改走优先级通道后 `position_rx_complete_count` 不再增长，活跃计数是 priority accept/duplicate/reject；任何仍以 `rx_complete_count` 判定的验收门会读到 0。

## 二、T2 当前状态对账

### 2.1 里程碑

| 里程碑 | 10-09 | 现在 | 依据（实测快照） |
|---|---|---|---|
| M0 静态部署 | 部分达到（有倒退） | 达到（台架） | 四板 `configured=1/active=1`；统一启动事务仍未实现 |
| M1 1 kHz cursor | 未达到 | 基本达到 | 四板 cursor 推进、零隔离，三段峰值在预算内；缺丢同步/换代专项 |
| M2 预测位置 | 未达到 | 达到（3 位置单轮） | 阈值广播、FREEZE 闭环确认，NO1 `freeze_complete_mask=7`；受 A-13 约束 |
| M3 从板执行 | 未达到 | 首次达到（每位置 1 组） | 三从板 frozen/armed/done = 7/7/0 |
| M4 READY 校验 | 未达到 | 未达到 | `trigger_ready_overlap.h` 只有主机测试，固件无调用方（已核实） |
| M5 T2 闭环 | 未达到 | 未达到 | 无 position_id 级实际 latch/完成证据 |

### 2.2 偏离项闭环

| ID | 10-09 结论 | 当前 | 证据 |
|---|---|---|---|
| A-01 | 覆盖已排队 VDC image | 已闭环 | 全库无 `tdma_flight_fifo_core0_patch_tx_ready` / `tdma_service_patch_flight_tx_ready` 调用 |
| A-15 | 位置描述符 22 片 | 已闭环 | 10 B 紧缩单帧 `trigger_position_slice.h` + `distributed_refmem.c` 编解码 |
| A-07 | 主机测试编译失败 | 已闭环 | 主机条件桩 `trigger_t2_runtime.c`；`test_trigger_t2_runtime.py` 24 passed。跨域复用 seat store 的诊断插桩仍在 |
| A-04 | 窗口门默认打开 + 队头阻塞 | 部分闭环 | `tdma_seat_admission.h` 默认恢复 0；挂载/窗口门整体编译掉。仍可经 SCPI 不经 `EXE-SEAT-01` 挂到 PRIORITY |
| A-02 | VDC 锁定门形同虚设 | 仍存在 | `trigger_time_projection.c` / `trigger_time_cursor.c` 仍比较 `> VDC_DOMAIN_LOCK_FAULT`（枚举最大值）；`T2-GATE-001` 仍 BLOCKED |
| A-03 | FREEZE 不带运行代身份 | 仍存在 | FREEZE 只校验 id（≤3）+version；latch 只按 position_id；从板用自身 run_id 回填，run/epoch 校验仅在 PREVIEW 分支 |
| A-05 | 一次 cursor 失效即全量作废 | 仍存在 | `service_deadlines` 瞬时失效仍 `cancel_backend()` 并把全部已冻结未完成位置标 fail |
| A-06 | 冻结边界放错 | 仍存在 | `prepare_deadlines_core0` 冻结位直接 `continue`，`model_token` 比较只对未冻结位置生效 |
| A-13 | 无预览则 FREEZE 永不发出 | **已修（2026-10-12）** | **已修**：`stage_pending_freeze()` 遇 `preview_copy` 失败按位置记录一次（`freeze_no_preview_mask` / `freeze_no_preview_count`，追加在 `T2?` 末尾可读回）并继续检查其余 pending 位置；pending 位保留使迟到预览仍可冻结。回归测试 `tests/python/test_t2_freeze_no_preview.py`；台架不再需要 `--late-source` 绕过 |
| A-14 | 无 STEP 参数 | 未动 | predictor 3 点定长模型 |
| A-16 | 每位置仅 1 组模板 | 未动 | `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` 为 1 |
| A-08 | 挂载 SAVE 不经 STOP 门 | 仍存在 | `scpi_trigger_seat_commands.inc` 直接进 store，无 `run_stopped_maintenance` |
| A-09 | v2 映射读写偏移不一致 | 仍存在（仅 v2 构建） | store 读用绝对偏移、写用相对偏移；v2 映射把 store 放进 CALIBRATION_NVS 且相对偏移为 2 扇区 |
| A-10/A-11/A-12 | 角色写死 / Trigger 自实现正映射 / 快照 seqlock | 仍存在（读码） | 本轮未逐行复核，未见修复提交 |

### 2.3 TODO 状态滞后

| TODO ID | 文档状态 | 实际（代码/凭证） | 建议状态 |
|---|---|---|---|
| T2-TRANSPORT-002 | PENDING | 紧缩单帧已实现并经硬件送达 | IN PROGRESS 以上 |
| T2-P0-FREEZE-001 | PENDING | 冻结 + 逐镜像回报 + 重发确认已上板 | IN PROGRESS 以上 |
| T2-ACTION-TEMPLATE-001 | PENDING | 模板库是 `T2:CONFigure` 前置，有主机测试并上板 | IN PROGRESS |
| T2-P0-CONFIG-001 | IN PROGRESS | `components/sync_trigger` 内无 `CONFIG_CONTROL`/`NODE_LOAD_STAGE` 引用 | PENDING |
| T2-TRANSPORT-001 | IN PROGRESS | 描述「T2 使用 RefMem 区域」已过时 | 改为优先级通道一等记录 |
| T2-READY-001 | IN PROGRESS | 仅主机模型，未接线 | 加注「未接线」 |
| T2-STEP/SEQ/EXEC-002/DCO/GATE | PENDING / BLOCKED | 无变化 | 保持 |

TDMA 侧同源条目仍为 PENDING 且已有部分实测：`TDMA-T2-002`（一等席 load instance）、`TDMA-T2-003`（冻结诊断）。

### 2.4 文档漂移

- 已闭环：D-01（-022 已更正 021 结论）、D-03（代码已与「单帧 position_q」一致）、D-06（代码/文档分离提交）、D-07（架构已回写 K-1～K-6）。
- 仍开放：**D-02**（09-27 run33/34 与 `FLIGHT:FIFO:RESet` 未记入进展日志，-020 之后直接跳 -021）、**D-04**（`ARCH_T2_RESERVATION_ARCHITECTURE.md` 仍 Draft、未标 superseded、停在 2026-08-20）、**D-05**（TODO 状态偏差未改，即 2.3）。

## 三、外部审计一（predictor/runtime/wire）核对

### 3.1 成立项（已核实）

predictor 无 STEP 字段、3 点定长、`announced_mask` 按 `i<3`、`build_descriptor` 只被测试调用、`observe_at_sync` 无生产调用、lead 为「距 origin 的比例」、A-13 卡死路径、`take_event` 无固件调用方、`trigger_action_runtime_*` 与 `trigger_ready_overlap_*` 无生产调用方、slice id 1..15、FREEZE latch 15、descriptor FREEZE 字面量 3、冻结 ack 8 bit 只覆盖位置 1..8、ack opcode 只带 run_id 低 4 位、`T2:CONFigure` 无 step 且 ppd 写死、静态断言「紧缩记录恰好填满 RefMem 区」、台架工具写死 3 位置、`t2_enable.py` 用 roll 0 会被 `config_valid` 拒绝——全部与代码一致。

### 3.2 需精确化

- 「`prepare_core0` 没有 horizon」应表述为：**每次调用都从 index 0 起扫描、只 stage 一个**。问题不是「提前预览过多」，而是 O(N) 扫描 + 编译期定长数组 + 无在途窗口。

### 3.3 实测尺寸（宿主 gcc 度量，2026-10-11，快照非事实源）

| 类型 | 大小 |
|---|---|
| `trigger_position_descriptor_t` | 104 B |
| `trigger_action_runtime_event_t` | 32 B |
| `trigger_t2_runtime_snapshot_t`（当前 `TRIGGER_T2_RUNTIME_POSITION_COUNT` 为 3） | 880 B |

### 3.4 该审计未覆盖的两点

- **验收读数漂移**：优先级通道接收只增 `s_position_priority_accept_count` 等计数，不增 `position_rx_complete_count`；最新凭证里从板 `position_rx_complete_delta=0` 而动作已完成（实测快照）。
- **控制字节共用**：冻结 ack 写 `CONTROL_OPCODE`/`CONTROL_SEQ8`，action probe 写同两个字节，两者同时活跃时后写覆盖前者，需互斥或编码空间分离。

## 四、外部审计二（分支角度前端）核对

### 4.1 成立项（读码 + `git show`）

分支仅有 remote ref；tip `e9372866`（2026-09-23）；merge-base `7fa834df`（2026-09-15）；`ae156377` 存在。分支侧 diff 为 263 文件/+99,331/−12,841，反向（HEAD 侧）为 1150 文件/+213,751/−50,897。HEAD 上 `CONFigure:ANGLe:SWEEp` 只是 `scpi_port_result_accepted` 桩，`READ:ANGLe:*` 返回硬编码值。分支侧 `CONFigure:ANGLe:SWEEp/INPut/SPEEd` 是真实实现，`PULSe`/`BREAkpoint` 为未实现处理器；`angle_config_t` 与 `angle_derive` 的校验（整除、`count=intervals+1`、`N=|step|×ppd` 为整数、`N×count` 上限、浮点下溢防护）与描述一致；运行期只有单位置在途（阈值翻倍或 busy 即 `COUNTER_BUSY`）；`TRIGGER_SEQUENCE_STATE_MAX 256`、`TRIGGER_SEQUENCE_PLAN_MAX 4`；link 侧要求 counter 节点持 `PULSE_DISTRIBUTOR` 角色、counter 输入 1..4 且 ≠ READY；验收脚本按 `speed = source_hz / threshold` 反推速度。

### 4.2 三处修正

1. **验收凭证说错**：分支自己的进展日志记录的角度扫描验收是 500 Hz 源 × 60 个位置（`--positions 60 --threshold 500 --source-hz 500`，另有 50 Hz × 2 位置一轮）；1 kHz 的记录是 **PIO 输入捕获**测试。该日志明确写着调整频率期间「仅保留为历史观察，不能据文件名宣称完成 1kHz 验收」。「1 kHz 源跑 30 个位置」无依据，不得写入计划。
2. **行号混用两侧**：`SCPI_COMMANDS.md:229-234`、`:265-267` 是**分支副本**行号（:229 为 4 参数 `SWEEp`，:265-267 为 `CONF:ANGLe:BP*`）；HEAD 同名文件那两行内容完全不同。回写 HEAD 文档时须按 HEAD 行号重新定位，并注意 HEAD 文档仍描述 3 参数 `SWEEp`。
3. **数值口径**：分支 `input_hz = speed × pulses_per_degree`，其中 speed 为 °/s；新定的 `frequency = speed(mdeg/s) × ppd / 1000` 是同一关系的毫度变体。复用时必须显式声明单位，禁止 °/s 与 mdeg/s 混用同一字段。

### 4.3 两处补充

- 分支校验里还有可复用细节：`PLAN_ID_MAX 31`、`CHANNEL_MAX 8`、`intervals == UINT32_MAX` 守卫、「start±step 必须真的改变取值」的浮点防护。
- 分支「单位置在途 + READY 授权下一步」的推进模型与 K-1（多板 READY 只做踩踏校验）、K-6（每位置多组、两组滑动窗口）直接冲突，只能借鉴配置校验与序列定义。

## 五、31 个位置 + STEP 决定的落地影响

### 5.1 身份与容量（会触及已冻结的运输面）

| 约束 | 现状 | 31 个位置 |
|---|---|---|
| slice 内 position id | 4 bit，有效 1..15（`trigger_position_slice.h`） | 不可寻址 |
| FREEZE latch | 15 槽（`trigger_position_mailbox.h`） | 不可寻址 |
| 冻结完成 ack | 8 bit，仅位置 1..8（`distributed_refmem_position_ack.inc`） | 不可确认 |
| descriptor FREEZE 校验 | 字面量 ≤3（`trigger_position_descriptor.h`） | 不可通过 |
| 桩/工具打印 | `T2:?`、`T2:ACTion?`、台架探针按 3 槽定长 | 需重排 |

结论：改 id 位宽等于改「10 B 紧缩记录恰好填满 RefMem 区」这条静态断言所在的一等席布局，须 TDMA 侧核定并按 `EXE-SEAT-01` 提交用户审核；登记表当前**没有任何 T2 行**，因此这是新增登记而非改登记，但 TDMA-T2-001/002 需要重开。备选是不动线上宽度、改成「槽位 ≤15 + 滚动代次」的两级身份，但 ack 位宽仍须重做（≥32 bit 或多帧位图）。

### 5.2 RAM 与事件容量

按 3.3 的实测尺寸线性放大到 31 个位置（推算，非实测）：snapshot 约 +4.8 KB、`s_preview_history` 约 +8.7 KB、deadline 邮箱/计划/seen 约 +3.8 KB，合计约 **+17 KB SRAM**；而 `TDMA-PROGRESS-20261010-001` 记录的当时链接余量约 4.5 KB（快照）。因此架构里「运行期只为有限个在途位置保留状态（当前 + 下一）」是硬约束，滚动窗口只能 2～4 槽。

`TRIGGER_T2_RUNTIME_EVENT_CAPACITY` 现为 `POSITION_COUNT × LOCAL_TEMPLATE_CAPACITY`；K-6 落地后按每位置 16 组放大，而 `trigger_t2_runtime_take_event` 在固件中无消费者（只增不减），事件环必须改为按在途槽位或接上消费者。

### 5.3 lead 口径与在途位置数

现公式 `lead = (1 − threshold/1000) × (target − origin)`（距 origin 的比例，`trigger_turntable_predictor.h`）。30° 扫描、thr=950 时末尾位置 lead ≈ 0.95×30°，即约 28 个位置同时在途——这正是必须改为「一步的比例」的量化理由。改为一步比例后，thr=500、5°/s、STEP 1° 时 lead = 100 ms，在途 2～3 个位置，与 FREEZE 每 4 个时间片重发一次的节奏（`trigger_t2_runtime.c`）和环回 ack 都留有余量。

### 5.4 STEP 下限与转速

配置期下限＝「一个位置内全部序列的跨度 + 余量」必须小于「一步的时间＝STEP / 转速」。K-6 的 16 组 ×（切换 settle + 网分单次测量 + READY 窗口）约 0.1～0.13 s（评审 E-01 估算快照）。据此：1°/s 或 5°/s 配 1° STEP 有充足余量；30°/s 配 1° STEP 不成立，必须由 STOP 配置校验 fail-closed 拒绝，而不是运行期取整或兜底。

### 5.5 零位 index 基点与哨兵冲突

用户口径「零位作为普通格点，index 也有 0」与现有哨兵冲突：全链路 `position_id == 0` 表示无效/无（slice 低 4 位要求 1..15、mailbox stage 拒 0、descriptor FREEZE 要求 ≥1），且 id 为 0 时 tag 退化为 `0xE0`，与冻结 opcode 家族相撞。建议明确为「内部 index 0 起、线上与 SCPI 的 id = index + 1 保持 1 起」，或显式重定基线并同步 ack 位序；同时定死零位是否必须落在 STEP 网格上（start=0 且 STEP 整除时它天然是格点）。

### 5.6 模拟源：两条既有路径

| 路径 | 现状 | 与本次决定的关系 |
|---|---|---|
| `sync_io_turntable_source`（`TRIGger:REServation:SOURce:CONFigure <hz>`） | OUT4 硬件 PWM，不占 PIO/DMA/IRQ/Core1 服务，频率范围 100 Hz–100 kHz，**占空比固定 50%**，只在 STOP 维护窗口装载、改配置须 RELEASE，状态回报 requested/actual Hz | 5 kHz 时高电平为 100 µs，**不是** 10 µs；需新增脉宽/占空参数，并把入参改为 (speed, ppd)。改的是 SCPI + sync_io 契约。优点是 T2-TURNTABLE-008「隔离源不占实时域服务」不受影响 |
| `model_turntable`（`CONFigure:MODEl:TURNtable:LOAD/MOTion/TRIGger`） | 已收 velocity + acceleration，TRIGger 收 dimension/start/stop/step/**pulse_width_us**/edge/timeout，输出带 accel/cruise/decel 斜坡的脉冲串并回报 min/max interval | 天然可表达「5°/s、每 0.001° 一个脉冲、脉宽 10 µs」，且覆盖 pre-roll 加减速；但跑在 `model_turntable_service` 内，会改变隔离验收证据口径 |

`pulses_per_degree` 在 `trigger_turntable_predictor_config_t` 中已存在，只有 SCPI 前端写死；改为参数后 T2 与源共用同一个值。注意 `sync_io_turntable_source` 的实际频率是量化后的 `actual_hz`，因此声明速度只用于配置期校验与源，运行期仍须由 predictor 自测速率（`pulses_per_sync_q16`）。

## 六、待审核决定清单

| ID | 决定 | 选项 | 建议 | 影响面 |
|---|---|---|---|---|
| D-1 | STEP 派生口径 | 整数毫度派生 N；是否允许非整除 | 采纳 `N=(terminal−start)/step+1`，STOP 校验整除与 `step_mdeg×ppd/1000` 为整数 | 回写 ARCH「角度扫描与 STEP 位置」；T2-STEP-001 |
| D-2 | 位置身份与容量 | 加宽线上 id；或槽位+代次两级身份 | 先定在途窗口槽位数，再定是否加宽 | 重开 TDMA-T2-001/002；`EXE-SEAT-01`；新增登记行 |
| D-3 | 冻结完成 ack | 加宽位图 / 多帧位图 / 只报槽位 | 与 D-2 一起定，禁止只覆盖 8 个位置 | 控制字节预算；TDMA |
| D-4 | 在途窗口大小 | 2 / 3 / 4 槽 | 2～4 槽滚动，按 5.2 的 RAM 结论 | predictor/runtime/transport 全部按槽位索引 |
| D-5 | lead 定义 | 保持距 origin 比例；改为一步比例 | 改为一步比例 | predictor；ARCH |
| D-6 | 零位与 index 基点 | 内部 0 基 + 线上 1 基；或整体重定基线 | 前者 | ARCH；SCPI 读回格式 |
| D-7 | 扫描用哪套源 | 硬件 PWM 源；`model_turntable` | 优先硬件 PWM 源（保隔离验收），需要加减速真实性时再用 model | SCPI + sync_io 契约；T2-TURNTABLE-008 证据口径 |
| D-8 | 脉宽参数 | 固定 50%；新增 `pulse_width_us`；占空比百分数 | 新增 `pulse_width_us`（与 model_turntable 同名） | `sync_io_turntable_source` API 与状态字段 |
| D-9 | ppd 参数化 | 进 `T2:CONFigure`；或独立命令 | 进 `T2:CONFigure`，源复用同值 | SCPI；predictor 配置；工具 |
| D-10 | 声明速度用途 | 仅配置期校验；或参与运行期预测 | 仅配置期校验 + 源，运行期自测速率 | ARCH；predictor |
| D-11 | A-13 修法 | 无预览判失败并继续；或等待预览 | 判失败并继续（计数可见），先修再跑扫描 | predictor/runtime；T2 语义变更需评审 |
| D-12 | 验收读数口径 | 继续用 `rx_complete_count`；改用 priority 计数 | 改用 priority accept/duplicate/reject | 工具、凭证格式、TDMA 进度口径 |
| D-13 | TODO 状态回写 | 按 2.3 回写；或只补注 | 按 2.3 回写并注明证据 | `TRIGGER_T2_TODO.md`；TDMA TODO 同源条目 |
| D-14 | 漂移处理 | D-02 补条目、D-04 标 superseded | 两者都做 | 进展日志；`ARCH_T2_RESERVATION_ARCHITECTURE.md` |

## 七、建议顺序与门禁

1. **先修 A-13**：任何多位置扫描在它修好前都会停在第一个无预览位置。
2. **再定身份与容量（D-2/D-3/D-4）**：这是 TDMA 侧输入，决定后面所有前端的工作量。
3. **冻结口径并回写**：STEP、lead、零位/index、STEP 下限、源与 ppd（D-1/D-5/D-6/D-7/D-8/D-9/D-10）。
4. **复用边界**：只搬分支的 SCPI 解析/校验、READ 格式与整数格点数学；不搬后端，不搬「READY 授权下一步 / 单位置在途」。
5. **文档回写与门禁**：canonical 文档更新后跑 `docs_check`、`doc_regression_check` 与文档 pytest；改到固件/工具/测试后另须 P3 硬件验收凭证。
6. **本次复核时的门禁状态**（实测）：`docs_check --strict-names`、`doc_regression_check`、`log-check`、`p3 check-staged` 均 OK；T2 主机测试 24 passed；登记表 `TDMA-FLIGHT-BITMAP-01` 行因 id 不符合 ROW_RE 被检查器跳过（治理盲区）。

## 八、证据索引

| 内容 | 位置 |
|---|---|
| 四板冻结与动作闭环 | `out/HardwareAcceptance/20261010/t2-bench-probe-213024/summary.json`（实测快照） |
| 一等席收发与预算 | `out/HardwareAcceptance/20261010/t2-bench-probe-201442/`（实测快照）；`docs/tdma/TDMA_TASK_PROGRESS.md` 的 `TDMA-PROGRESS-20261010-001/002` |
| T2 侧记录 | `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 的 `T2-PROGRESS-20261010-022/023/024` |
| 10-09 代码评审 | `docs/trigger/t2/TRIGGER_T2_REVIEW.md` |
| 分支角度前端 | `git show origin/feature/node-sequence-reservation-trigger:...`（`scpi_config_commands.c`、`trigger_sequence_link.c`、`sync_io_sequence.c`、`tools/hardware_acceptance/sequence_position_validate.py`）与分支侧进展日志 `TRIGGER_SEQUENCE_TASK_PROGRESS.md`（位于该分支的 `docs/trigger/sequence/` 目录，HEAD 上不存在） |
| 结构尺寸 | 宿主 gcc 度量，2026-10-11（快照非事实源） |
