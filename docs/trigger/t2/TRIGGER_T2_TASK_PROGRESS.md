# T2 预约触发任务进度

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-10-12

## 文档接口

本文记录真实执行、来源和证据，不冻结契约或代替 [TODO](TRIGGER_T2_TODO.md) 的任务状态；设计见 [Architecture](TRIGGER_T2_ARCHITECTURE.md)。条目使用 `T2-PROGRESS-YYYYMMDD-NNN`，最新在上；按 C14/C15 保留失败、历史和归档索引。

## 当前 checkpoint

### 2026-10-13 T2-AUDIT-04 参考实现取证：转台输入与测量脉冲的已定角色

取证来源：分支 `origin/feature/node-sequence-reservation-trigger`（commit `19b9c5ff`）内的
《TRIGGER_SEQUENCE_ARCHITECTURE》第 **NSEQ-RSV-01** 节。

该节的槽位职责表（逐字要点）：

| 槽位职责 | 本地端 IO | 执行事实 |
|---|---|---|
| **COUNTER** | **IN1（转台）** | 累计计数达到位/阈值后才进入业务位 |
| SP8T 通路控制 | OUT1、OUT2 | 记录所选状态 |
| SP2T 极化控制 | OUT3（H/V） | 记录对应状态 |
| **VNA_GATEWAY** | **OUT4（测量脉冲）、IN2（READY）** | 只对应本地预约 |

两条推论：
1. **转台侧的含义已定**：转台作为 **COUNTER 角色、输入 IN1**；测量脉冲在 **OUT4**。
   后者与现有 `TRIGger:REServation:SOURce:*` 的 OUT4 硬件 PWM 路径**一致**。
2. **模式化的做法已定**（A-04/A-06）：通过 **SYNC_IO owner/persona 机制**实施热加载，
   常驻 SM 角色为 `TURNTABLE_SM`、`INGRESS_SM`、`EXECUTOR_SM`、`COUNTER_SM`；
   资源以 `sync_io_persona_resources.c` 与 `SYNC_IO_PERSONA_ID_SEQUENCE_COUNTER` 为准；
   切换顺序：停 SM → 停 DMA/IRQ → 清 FIFO → 卸载/装载 → 初始化执行位置 → **原子发布 generation**。

本条与本仓库现状的差异：本仓 `components/sync_io` 尚无 `sync_io_sequence.*`，
也尚无对应 persona；因此 T2-AUDIT-01 落地时应**按上述机制**实现，而非自制薄配置。


### 2026-10-13 T2-AUDIT-04 补充：“模型源”API 命名跨两条物理通道

取证（逐字）：

| 入口 | 实际转发到 | 物理通道 |
|---|---|---|
| `sync_io_model_pulse_schedule_arm` / `…_arm_ns` | `sync_io_output_pulse_schedule_arm[_ns]` | **波形输出**（PIO0/**SM2**，`BOARD_SYNC_PIO0_WAVE_OUTPUT_SM`） |
| `sync_io_model_pulse_schedule_arm_periodic_ns` | `sync_io_sma_observer_pulse_schedule_arm_periodic_ns` | **证据脉冲**（PIO0/**SM1**；`sync_io.h` 原文“External DPLL evidence pulses use PIO0/SM1 and SMA OUT1”） |

两条推论：
1. T2-AUDIT-04（转台源路径选型）必须声明验收所需的是**波形输出（SM2）**还是**证据脉冲（SM1）**。
2. T2-AUDIT-01 的模式声明只能声明 **SM2**（SM1 归证据通道），否则资源声明与物理不符（`ARCH-PIOPARTITION-01`）。

地址取证：`sync_io.h:216-233`（API 签名）、`sync_io.h:234`（SM1 注释）、`settle`——`sync_io_model_sched.c:1120-1131`（periodic 转发）、
`boards/rp2350_trig/inc/board_config.h:254/275`（`BOARD_SYNC_PIO_FAST=pio0`、`BOARD_SYNC_PIO0_WAVE_OUTPUT_SM=2u`）。


### T2-PROGRESS-20261012-001：T2 主线稳定性——三个根因定位与修复，判据收紧

- TODO task ID：`T2-STEP-001`、`T2-SOURCE-001`、`T2-DOC-001`；涉及 TDMA/VDC/RefMem/SYNC_IO 的 Core1 驻留与 TDMA 环时钟快照。
- **更正 2026-10-11 的阻塞链（已核实）**：「SYNC_TRIGGER 窗口 < WCET → 每周期 `start_missed` → 需优先级源 active 的 ingress 余量 → FOLLow 生效」不成立，来自两处解析错误：`SYSTem:TDMA:SCHEDule?` 每相位 11 字段（`SCHEDULE_PHASE_FIELDS`），SYNC_TRIGGER 是相位 7（`APP_REALTIME_PHASE_TABLE`）而非 6，原读数实为 REFMEM 的 `max_runtime`；`T2?` 的 `segment_max_cycles` 在 [42..44] 而非 [47..49]。`close_cycle` 为静态值（`app_realtime_phase_priority()`），与优先级源是否 active 无关。`T2:CURSor?` 的 [9] 是恒 0 的 legacy `identity_crc32`，绑定的 `ring_config_seq`/`model_token` 在 [10]/[11]。
- **根因 A：计数在游标有效前越过阈值（工具，已修）**。计数器由 PWM 源驱动（停源即冻结；源启动后约 1 kHz，快照非事实源）。源原先在 Step 16 启动，游标有效前计数已越过全部阈值；Core0 对 `last_counter_count >= threshold_count` 的位置不再生成 preview，该轮不可能发布。`t2_workflow.py` 现在 Step 1/21 停源，Step 19 仅在参考游标有效且计数未达首阈值时 `SOURce:STARt`，越过首阈值即具名失败。
- **根因 B：TRAIN 人格恢复期绑定游标后锁死（固件，已修）**。新增粘性故障现场记录（`trigger_time_cursor_fault_t`，`T2:CURSor?` 末尾追加 7 字段）实测：故障发生在首次绑定后下一拍（seq 1），`site RING`、`adapter_started=0`、config 未变。原因：`RING:START` 后环运行时为恢复 DATA/CS 人格停适配器一拍（`tdma_ring_runtime.c` 的 `training_dirty` 分支），若游标恰在此前一拍绑定即置粘性。修复：`tdma_ring_clock_snapshot_t` 增加 `training_dirty`，游标在其置位期间按未就绪重试、不绑定；绑定后再训练仍为粘性（单测 `train_persona` / `bound_retrain`）。
- **根因 C：SYNC_TRIGGER 超 WCET 被隔离（固件，已修）**。阈值切片（origin）与布防切片（从板）在 XIP 冷取指下超出 `PROJECT_CORE1_PHASE_SYNC_TRIGGER_WCET_CYCLES` 并被隔离（掩码位 6），冻结计数与游标。按 ELF 调用图把 SYNC_TRIGGER 每片/布防片路径上的 XIP 函数移入 SRAM：origin FREEZE 路径（`stage_one_freeze`、`resend_one_freeze`、RefMem stage/pending）；所有板的游标投影（`vdc_domain_dco_local_to_output_ns`、`vdc_dpll_manager_feedback_session`、`cursor_qualify_model`）；从板 `distributed_refmem_position_priority_drain`、`deadlines_need_service`、`cancel_backend`、`action_fail`、SWITCH/GATEWAY 的 `schedule_at`/`cancel`/`safe_stop`，以及 GCC 分裂到 XIP 的 SWITCH `poll.part.0`（改 `noinline`）。调用图上剩余的 flash 函数只在 legacy 触发、故障或会话切换路径，另有共享 libc `memcpy`/`memset` 与 SDK `clock_get_hz` 未迁移。origin FREEZE 路径的分段最大耗时追加在 `T2?` 末尾（`trigger_t2_runtime_freeze_timing()`）。
- **SRAM**：为驻留腾挪，用户决定把 `VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS` 缩为当前值（缩容史见该宏注释）；本轮链接后主 SRAM 余量见构建产物，快照非事实源。
- **判据收紧（工具）**：原判据 `configured==4 && publishing>0` 统计采样行，曾把「只发布一个位置随后从板被隔离」判为通过。现在 `t2_workflow.py` Step 20 要求参考 `published_mask` 为全部位置（`ALL_POSITIONS_MASK`）并把 SYNC_TRIGGER 隔离记为失败；`p3_hardware_acceptance.py t2` 另要求 workflow `failed steps: none`。按新判据离线重评旧日志：隔离轮全部转为 FAIL，干净轮保持 PASS。
- **台架（本次实测，快照非事实源）**：OTA（新增 `tools/ota/OTA_workflow.py` 首次实跑，标定保持）→ `DPLL60_workflow.py` 通过（Step 3/9 为非阻断备注）→ T2 连续 6 轮：前 3 轮全步骤通过；收紧判据后 3 轮中 2 轮通过、1 轮失败（根因见下条未闭合项）。修复后从板 SYNC_TRIGGER 最大耗时由超 WCET 降至约其 2/3，无 overrun、无隔离；参考板每轮发布全部 3 个位置。证据：`out/HardwareAcceptance/20261012/t2-20261012-1114*`、`t2-20261012-1117*`、`t2-20261012-1119*`、`t2-20261012-1125*`、`t2-20261012-1128*`、`t2-20261012-113046`。
- **未闭合**：① `CALibration:ORIGin:TRIAL` 间歇停参考环（`enabled`/`adapter_started` 归 0、config 前进 1），已与 `SOURce:STARt` 拆开确认由 TRIAL 单独触发；游标按 `site RING` 正确记录。② 从板 SYNC_TRIGGER 仍有大量 `start_miss`（前序相位迟到继承，不隔离、游标按 `missed_sync_count` 计入），既有现象，未处理。③ 游标段在所有板仍占较大份额。④ 构建号 `PROJECT_BUILD_ID` 为 CMake 缓存值，增量构建不变，凭证/OTA 以构建号辨识镜像存在盲区。
- **门禁（代码提交 `b2c527ed`）**：按用户决定取完整 P3 凭证。前两次 P3 均止于 DPLL 60 s 的主机侧判据，固件与采集本身通过：① RAM 页读取被瞬时拒绝即中止并残留 `-200` 污染复原（固件读租约需一次性 seqlock 复制与池所有者 CAS 同时成功）→ `vdc_priority_trace.py` 有界重试并清错误队列；② STOP 恰在 60 s 区间边界后产生单个 `PARTIAL|TERMINAL` 短尾 bin（固件设计内），判据要求恰好 6 个 bin → `p3_dpll_short.py` 只接受这一形状，端点按最后完整 bin + 尾 bin 包络判定。第三次 `p3_hardware_acceptance.py run`（quick）`PASS_WITH_WARNINGS`（ERROR/FATAL 为 0，WARN 类别与 2026-10-11 通过轮相同），`check-staged` OK；证据 `out/HardwareAcceptance/20261012/p3-133853`。

### T2-PROGRESS-20261011-027：M:N 轮换比例 SCPI 入口落地，一等席对齐特等列为下一目标

- TODO task ID：`T2-TRANSPORT-001`、`T2-TRANSPORT-002`；TDMA 侧 `TDMA-SEAT-02`（IN PROGRESS）、新增 `TDMA-SEAT-03`（PENDING）。
- **实现（TDMA 侧）**：轮换比例从编译期常量改为 STOP-only 的每板设置，`SYSTem:TDMA:SEAT:ROTation <M>,<N>` 写入（每 share 1..`TDMA_SEAT_ROTATION_SHARE_MAX`，`M+N <= TDMA_SEAT_ROTATION_CYCLE_MAX`，越界/零值/运行中拒绝），`SYSTem:TDMA:SEAT:ROTation?` 读回（未设置回报默认 1:1），`SYSTem:TDMA:SEAT:COUNters?` 读回 origin 六个席位计数；读到 0 时回落到编译默认，避免零周期。比例不进入 ring profile/geometry，wire CRC 不变。
- **验证（主机，本次实测）**：适配器 host 单测通过；`tests/python/test_tdma_seat_rotation.py` 通过（含 2:1、1:2 领队序列与零值回落）；SCPI/TDMA 相邻 python 测试 50 项通过。
- **对 T2 的含义**：比例现在可现场调，但调向特等会按比例拉长一等的等待上限；配置一等席前必须先有一等的期限/背压预算（见下条目标）。
- **下一步（用户指示）**：把一等席的服务等级提升到特等席水平。已定义为 `TDMA-SEAT-03`，缺口清单：①无一等专属 RX/TX 保全与独立队列（现与特等共用 lane 后按 class 分流）；②失败原因未按 class 细分；③从板记录级完整性校验为保预算被移除，仅 version 去重；④一等无独立期限/背压预算；⑤单深度邮箱的"有等待记录即拒绝 stage"未作为显式背压上报。
- **未闭合**：固件改动尚未取得 P3 硬件验收凭证（本机无四板台架），pre-commit 会按 staged 源码指纹拦截；比例与一等扩容须走 `EXE-SEAT-01` 与 C11 登记。

### T2-PROGRESS-20261011-026：按当前代码回写一等席快速路径与轮换比例语义

- TODO task ID：`T2-TRANSPORT-001`、`T2-TRANSPORT-002`、`T2-DOC-001`；关联新增 `TDMA-SEAT-02`。
- **触发**：用户指出「一等席现在也走快速路径」「1:1 是默认值但可通过 SCPI 配置」；复核代码后按现状回写语义，不按未实现的能力写。
- **代码事实（已核实）**：origin 快速路径的 1:1 轮换是**编译期常量** `TDMA_SEAT_ROTATION_PRIORITY_SHARE` / `TDMA_SEAT_ROTATION_FIRST_CLASS_SHARE`（`tdma_pio_spi_ring_adapter.h`），全库无写入者、无 SCPI 入口；SCPI 侧只有 `TRIGger:REServation:SEAT:MOUNT/MOUNT?/CLEar`（class→seat 挂载表），其唯一消费点位于默认关闭的窗口门（`TDMA_LOOP_SCHEDULE_ENABLED` 为 0）；origin 的 `seat_*` 计数未接 SCPI 只读页。从板侧一等记录已确认由优先级通道第二接收端承担，旧 `rx_complete_count` 不再增长。
- **回写**：`docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`（轮换比例改为「编译期常量、当前 1:1、无配置入口」；验收读数改 priority 计数；实现状态补从板接收与已闭环项）、`docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`（一等席行改为快速路径一等邮箱 + 发布机会轮换，删除「与普通 VDC 状态同帧、临时占用二等 RefMem 区域」）、`docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`（分片仅为迁移/回退路径）、`docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`（一等运输行按现状；补 K-5 实现方式说明：当前为独占发布机会，非同帧字段压缩）。
- **未闭合**：轮换比例的 SCPI 配置入口与 seat 计数只读页（新增 `TDMA-SEAT-02`，比例属席位预算须走 `EXE-SEAT-01`）；K-5 的「帧内压缩 VDC 字段」与当前「独占发布机会」两种资源模型须择一，待用户裁定。

### T2-PROGRESS-20261011-025：2026-10-11 复核汇总与口径回写

- TODO task ID：`T2-DOC-001`、`T2-STEP-001`、`T2-IDENT-001`、`T2-SOURCE-001`、`T2-FREEZE-002`、`T2-DOC-002`；产物 `docs/trigger/t2/TRIGGER_T2_STEP_REVIEW.md`（评审快照，非契约）。
- **范围（基线 `2c338fe0`，工作区干净）**：复核 10-09 评审的每条偏离项在当前代码是否仍成立、核对两份外部审计（① predictor/runtime/wire 硬编码，② `feature/node-sequence-reservation-trigger` 角度前端复用），并评估「31 个位置 + STEP + 零位 + 模拟源」决定的落地影响。
- **偏离项对账（已核实）**：A-01（改写已排队 VDC image）、A-15（22 片分片）、A-07（主机测试编译失败）已闭环；A-04 部分闭环（窗口门默认恢复关闭，SCPI 挂载路径仍在）；A-02、A-03、A-05、A-06、A-13、A-14、A-16、A-08、A-09 仍成立；`trigger_ready_overlap.h` 仍无固件调用方。
- **新发现（已核实）**：① 从板改走优先级通道后 `position_rx_complete_count` 不再增长，活跃计数为 priority accept/duplicate/reject，任何按 `rx_complete_count` 判定的验收门都会读到 0；② 冻结确认写的 `CONTROL_OPCODE`/`CONTROL_SEQ8` 与 action probe 写同两个字节，同时活跃时后写覆盖前者。
- **外部审计核对**：predictor/runtime/wire 审计逐条成立，仅「`prepare_core0` 无 horizon」需精确化为「每次调用从 index 0 起扫描、只 stage 一个」；分支角度前端审计的拓扑、命令表、`angle_derive` 校验、单位置在途与 link 角色校验成立，但验收凭证应为 500 Hz 源 × 60 位置（分支进展日志记录，1 kHz 记录属 PIO 输入捕获，且该日志声明不得据文件名宣称 1 kHz 验收），其引用的 `SCPI_COMMANDS.md` 行号属分支副本。
- **实测尺寸（宿主 gcc，快照非事实源）**：`trigger_position_descriptor_t` = 104 B、`trigger_action_runtime_event_t` = 32 B、`trigger_t2_runtime_snapshot_t`（按当前 `TRIGGER_T2_RUNTIME_POSITION_COUNT`）= 880 B。据此按现布局线性放大到长扫描的 SRAM 代价不可接受，在途槽位必须有界。
- **门禁（本次实测）**：`docs_check --strict-names` OK（176 文件）、`doc_regression_check` OK（仅既有已登记 WARN）、文档 pytest 38 passed、`test_trigger_t2_runtime.py` 24 passed、`p3 check-staged` OK、`--log-check` OK。
- **未闭合**：身份/确认位宽与在途槽位数（`T2-IDENT-001`）、模拟源选型与脉宽/ppd 参数（`T2-SOURCE-001`）、A-13 修法（`T2-FREEZE-002`）、09-27 run33/34 证据补记（`T2-DOC-002`，凭证原件待定位，禁止据推断填数字）。

### T2-PROGRESS-20261010-024：一等席经优先级通道接收、FREEZE 闭环确认，三从板动作全部完成

- TODO task ID：`T2-EXEC-001`、`T2-TRANSPORT-002`；提交 `3ddeb258`。
- **丢帧根因（TX/RX 预算实测，`t2-bench-probe-201442`，快照）**：从板普通 flight 路径每次捕获跳过积压、只取最新一帧，平均约 4 ms 一次（最长 12–14 ms），而一等记录在线上只停留约一个发布机会；另一缺陷是一等席上车不推进 `next_seq32`（实测增量 0），连续采到的两条 T2 记录 seq16 相同而被按段去重。送达率 33%/24%/2.5%；同期优先级通道 IRQ 覆盖约 99.7% 帧。
- **修复**：从板改由优先级通道接收一等记录（每帧捕获、线上重复帧视为同一记录、未入箱者由下一份重复帧重试）；每条一等记录独占 seq16；从板接收路径残余 XIP 调用入 RAM。
- **FREEZE 语义（用户确认）**：FREEZE 为开始冻结；从板冻结后经每个镜像的 control 字节回报冻结掩码，一次环回到 NO1；NO1 重发未完成位置直至全部目标确认，置冻结完成。
- **实测（四板，`t2-bench-probe-213024`，快照）**：三从板三个位置 frozen=7、ACT armed=done=7、fail=0；NO1 `freeze_complete_mask=7`、`abandoned=0`、重发 15 次；四板零隔离，从板 receive 段峰值 ≤8.5k 拍。
- **DPLL60**：首次 `dpll60-132430` 因主机调度停顿约 3.9 s 触发 25 s 窗口 `acquisition_overrun`（示波器采集 1.89 s 正常、相位最大约 34 ns、内部健康通过）；重跑 `dpll60-135802` PASS，作为本批凭证。
- **未闭合**：A-13（源早于预览越阈时 FREEZE 阻塞）仍以 `--late-source` 绕过；STEP/16 组/滑动窗口（T2-STEP-001、T2-SEQ-001、T2-EXEC-002）与 VDC LOCKED 门控（T2-GATE-001）未开始。

### T2-PROGRESS-20261010-023：从板首次完成 T2 动作闭环，剩余缺失定位为一等席传输丢帧

- TODO task ID：`T2-EXEC-001`、`T2-TRANSPORT-002`；提交 `914a854d`。
- **源起动时序（A-13）**：探针在 T2 START 前即启动模拟源时，NO1 在任何 PREVIEW 保存前越过全部阈值（`announced_mask=7`），`stage_one_freeze()` 遇首个无预览位置即返回，后续位置不再检查，FREEZE 永不发出。以既有 `--late-source` 绕过后 NO1 冻结三个位置；固件侧"无预览位置判失败并继续"属 T2 语义变更，待单独评审，未改。
- **从板隔离**：动作武装后从板 SYNC_TRIGGER 升至 27–36k 拍（预算 26800，快照）被隔离，原因是已武装 deadline 的逐片轮询（`service_deadlines`、sync_io switch/gateway `realtime_service_core1`/`realtime_status`）从 XIP 执行；放入 RAM 后四板零隔离。
- **FREEZE 丢失（接收侧）**：FREEZE 每位置只发一次，而从板 RX 邮箱单深度，前一 PREVIEW 未被取走时 FREEZE 以 FRAGMENT 拒收（NO2 达 77 次，CRC 拒收 0）。按用户选定方案 a 增加接收侧按位置 FREEZE 锁存，取用时 FREEZE 优先；拒收降至 3–7 次。
- **分段计时**：T2 Core1 服务新增 cursor/receive/deadlines 三段峰值，附于 `TRIGger:REServation:T2?` 末尾。从板峰值约 11.4k/17.7k/10.1k 拍（快照，`t2-bench-probe-194914`）。
- **实测（四板，`t2-bench-probe-194914`，快照）**：NO2 三个位置 received/frozen/armed/done 全为 7，为首次从板端到端动作闭环；NO3 frozen=2、NO4 frozen=6，已冻结者全部完成。P3 `p3-195851` PASS_WITH_WARNINGS（DPLL PASS；新增 T1/T3 SCK 训练诊断告警，TDMA deadline miss 告警与前次相同）。
- **未闭合**：NO1 一等席记录 137 次上车，三从板 `rx_complete_count` 均为 43，同帧共同丢失、无 CRC 错误，指向 origin 或从板对背靠背快速路径帧的公共处理。用户判断为特等与一等发射间隔过近、从板尚未处理完特等帧；先以 TX/RX 时间戳精确预算，再决定是否在 TDMA-SEAT-01 中引入最小间隔（底层语义变更，须先评审）。

### T2-PROGRESS-20261010-022：紧缩单帧记录经一等席快速路径跨环送达，动作执行未闭合

- TODO task ID：`T2-TRANSPORT-002`、`T2-TURNTABLE-003`、`T2-EXEC-001`；提交 `19bb91dd`（紧缩单帧）、`71ed3e5f`（NO1 去隔离、台架链）、`8bd547b2`（TDMA-SEAT-01）。
- **台架链更正**：`tools/sync_trigger/t2_enable.py` 的 COUNTER 行输出掩码改为 0（`trigger_deployment_validate()` 要求 COUNTER 无输出，原 0x8 使整表被拒、`T2:CONFigure` 返回 92）；部署 generation 每次单调递增（`DEPloyment:CLEar` 保留 `last_generation`）；仅 NO1 为 origin、全板 source_slot=0。`T2-PROGRESS-20261009-021` 的“唯一阻塞”与“NO1 不配置符合预期”两项结论据此更正，原条目保留。已跑通的 09-27 台架链固化为 `tools/sync_trigger/t2_bench_probe.py`。
- **NO1 SYNC_TRIGGER 去隔离**：origin 热路径（`service_origin`、`preview_copy`、`publish_begin/end`、COUNTER 读取）放入 SRAM，运行时最大 27460→约 9600 拍（预算 26800，快照）。
- **载荷**：位置记录改为 10 B 紧缩单帧（tag、version16、target_ms_index32、fine24），恰占 mailbox RefMem 区，一帧发完；线上不携带运行身份，从板以启动期配置补全后校验。
- **运输**：TDMA-SEAT-01 后 T2 记录写入一等邮箱，经 origin 快速路径与特等按发布机会 1:1 轮换上车；不再改写已排队 VDC image。
- **实测（四板，单轮 12 s，快照，`t2-bench-probe-183247`）**：NO1 记录上车；三从板 `rx_complete_count` 6/1/1、`received_mask` 含位置 3、SYNC_TRIGGER 零隔离；DPLL60 PASS（`dpll60-102638`）。
- **未闭合**：三从板 `T2:ACTion?` 全零，尚无冻结与执行；需逐环检查 PREVIEW 版本推进、阈值 FREEZE 送达、从板本地 deadline 解算与 PIO 装填。NO3/NO4 各 5 次 RX 拒绝（FRAGMENT），待查。

### T2-PROGRESS-20261009-021：使能链逐环打通至 `T2:CONFigure` 成功（发布仍受飞行 FIFO 静止条件阻塞）

- TODO task ID：T2-DEPLOY-002、T2-EXEC-001、T2-SLOT-GATEWAY-001、T2-ACCEPT-001。
- 本轮定位并逐环打通了"从加载到 T2 运行"的完整前置链，全部以硬件读回为证据（不再依赖推断）：

**1. 网格**：`GRID:CONFigure` 被拒的根因是 `subdivisions` 必须 ≥ 2
（`trigger_time_grid_valid()` 要求 `epoch && generation && subdivisions >= 2 && period_ns >= subdivisions`）。
修正后 `GRID?` 显示 configured=1、period=1 ms、subdivisions=2。

**2. 部署**：`DEPloyment:STAGe`/`:ACTivate` 的 CRC 由 `trigger_deployment_crc()` 生成，主机实现与设备一致
（设备直接接受并在 `DEPloyment?` 回显该 CRC）⇒ 部署 staged+active 成立。

**3. 动作表**：安装校验包含两道独立闸门，二者都需满足
（`flash_transaction` 层与动作层）：`trigger_action_table_validate()` 要求
**动作行 `role` 等于本板 `binding.local_role`**，因此必须**逐板、按本板角色**构造动作表；
且 COUNTER 板（NO1）无对应 `kind`（`SWITCH_CODE`/`GATEWAY_PULSE`），其动作安装被 ROLE 拒绝，符合"COUNTER 只观察"。

**4. 模板库（此前完全缺失的一环）**：`T2:CONFigure` 的前置不是动作运行时，而是**独立的动作模板库**
`trigger_action_template_store`（命令族 `TRIGger:REServation:TEMPLate:{INSTall,CLEar,ACTivate,RELease,?}`）。
`TEMPLate:INSTall` 的 CRC 由 `trigger_action_template_table_crc()` 生成（header 两字 + 每行
`template_id,generation,role,kind,argument,span_slices,delta_q` 两个 32 位字），主机实现经设备验证为 OK；
随后 `TEMPLate:ACTivate` 置 `resources_loaded`/`binding_current`。

**5. 停机维护窗口**：`T2:CONFigure` 经 `tdma_service_run_stopped_maintenance()` 落地；
该函数要求"环未使能、适配器未启动、配置已应用、无挂起停机元数据更新/环控制、且
`ring_staged_config.geometry_generation == 0`"。注意与第 3/4 步**存在时序张力**：
绑定成立需要 staged 环配置，而维护窗口要求 staged 几何已消费 ⇒ 需在同一停机窗口内按序完成。

**6. 预测器取值规则**：`trigger_turntable_predictor_config_valid()` 要求
`pulses_per_degree`、**`pre_roll_mdeg` 与 `post_roll_mdeg` 均非零**、
`start_angle_mdeg <= zero_angle_mdeg <= terminal_angle_mdeg`、`0 < threshold_per_mille < 1000`。
此前 `T2:CONFigure` 一直失败的直接原因即 roll 为 0。

**7. 结果**：修正预测器参数后，三块执行板（NO2 SWITCH_A / NO3 GATEWAY / NO4 SWITCH_B）
`T2:CONFigure` **成功**，`T2?` 的 `configured = 1`（NO1 因 COUNTER 无模板未配置，符合预期）。

**8. 当前唯一阻塞**：`T2:STARt` 的准入第二条 `distributed_refmem_reset_position_stopped()` 返回 false
（`scpi_t2_update_stopped()` operation=1 路径）——该函数要求环未使能/适配器未启动/配置已应用、
飞行 FIFO `tx_ready_count == 0 && tx_active_generation == 0 && rx_parse_count == rx_queued_count`、
且本地与各节点位置邮箱不处于 WRITING/READING。实测在"复位后未启环"的静止态下仍不满足
⇒ **位置帧发布（`SYSTem:REFMEM:SYNC:FLIGHT:POSition?` 的计数推进）尚未观测到**，为下一步唯一待办。

- 证据与可复现手段：本轮为使能链每一环建立了"诊断通道"（复用席位挂载状态查询
  `TRIGger:REServation:SEAT:MOUNT?` 的 last_reject 字段，采用"基线 + 首个触发者优先"语义），
  可一次读出拒绝原因；相关临时插桩需在目标达成后清理或规范化为正式诊断。
- 台架：四板在线；硬件验收跑（含 1 ms DPLL 60 s）多次 PASS，构建 `flash_link_contract=OK`。

### T2-PROGRESS-20260927-020：本地提前动作后端与隔离模拟源接线

- TODO task ID：T2-EXEC-001、T2-TURNTABLE-004/008、T2-SLOT-GATEWAY-001、T2-READY-001。预装 SWITCH/GATEWAY 增加本地 TIMER1 deadline 提交、取消和实际 PIO 回执；不新增 TDMA 席位、DMA 或 CPU IRQ。装填 guard 与 origin 不确定性分别记录，不能把提前装载当作执行完成。
- 从板 Core0 提前解算 coarse/fine q，使用本地原子镜像交给 Core1；阈值采用本地最新已完成解算版本，冻结后停止解算。当前本地模板容量以 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` 为准，起始/零位/终止实例独立；同板密集多动作和重合位置的业务去重尚未支持，冲突不重排。
- 新增独立 OUT4 PWM 模拟源，STOP 预装，RUN 只开启/关闭已装载输出，COUNTER 可共存；SCPI 为 `TRIGger:REServation:SOURce:CONFigure/STARt/STOP/RELease/?`。另外新增 `TRIGger:REServation:TIMing?` 分段周期读回和 `TRIGger:REServation:T2:ACTion?` 提交/回执/失败诊断。
- 独立审核发现并处理 Core0 SWITCH 查询争抢 Core1 回执、旧 FREEZE 跨运行残留、PWM 预装时旧内部高电平可能漏出的边界；相关负测和集成证据仍在收敛，不能据 host 通过关闭硬件动作或 READY 校验。
- 用户明确保持示波器四通道接四板 OUT1：本轮可外测 NO2/NO4 动作；NO1 OUT4 转台源与 NO3 OUT4 TRIG/IN1 READY 只取板端证据，外部波形缺口保留。下一 gate 为统一源码构建、专项上板、原预算分段计时、真实 position_id 回执与可见通道边沿，最后才执行阶段 P3 和分离提交。
- 后续四板动作探针确认本地游标在 24 位分段计时优化后持续推进，NO1 模拟源与 COUNTER 均有脉冲读回；但 Core0 预测诊断显示 `core0_preview_saved` 已增加而位置运输长期保持 `tx_active=1, tx_publish_count=0`，因此 `published_mask/action_completed_mask` 仍为零。该新阻塞位于 RefMem T2 位置帧发布背压/主站 resident frame 选择，不能把计数阈值经过或预览镜像生成宣称为跨板预约回执。

### T2-PROGRESS-20260927-019：复用本地网格的接入纠偏

- TODO task ID：T2-CLOCK-001/002、T2-EXEC-001、T2-ACCEPT-001。纠正上一 checkpoint 对“需要建立新的逐脉冲源”的推断：已验证的本地 VDC/DCO/TIMER1 网格存在，当前缺口是 T2 消费路径和本地执行接线；不新增 TDMA 预算或载荷。
- 独立只读复核确认：`vdc_run_output.inc` 使用 committed DCO 与当前 TIMER1 生成连续 ordinal，未要求本地 PI LOCKED；T2 原游标却绑定 ring observation 的 `sync_seq` 和旧接收时间戳。FOLLOWER 本地 PI 状态不等于 typed FOLLOW/PHASE 已采用模型的状态。
- 历史证据复核：`out/HardwareAcceptance/20260923/run-slices-on-r5/joint/capture/actions.jsonl` 包含 SYNC/MATCH/FOLLOW/PHASE 完整启动配置；仅 ARM/START 不能重现。`last_ordinal` 是已经提交的未来块尾，不能用作当前游标。保留上一条硬件失败事实，不据此否定已有细分能力。
- 游标已改为本地完整 coarse/fine q，跨格轮询只统计未逐格观察数量；身份换代和时间回退显式失效。定向 cursor 回归及关联 grid owner 回归通过，证据目录为 `out/pytest/cursor-local-r2`。下一步为 Release、实际游标读回和提前硬件 deadline 接线；尚未完成 position_id/READY 闭环，未签发新的 P3 凭证。
- 静态现场读回存于 `out/t2-local-cursor-20260927/preflight.json`；错误的简写 build 查询超时保留，不作为固件身份依据。
- 首轮固件定向 HIL（构建快照 `20260927032758`）四板升级成功；启动探针漏掉既有 `DPLL:PROVisional` 模型绑定，且曾在 RUN 查询 STOP-only PHASE 状态，相关拒绝/超时保留于 `run1` 至 `run5`。四板游标未取得有效身份，`SYNC_TRIGGER` 实测超过现有 WCET 并隔离，不能宣称游标上板通过。
- 预算修复只调整 T2 本地布局：生命周期门、快照和失效发布驻留 SRAM，模型投影留在 XIP；全函数驻留曾导致 RAM 链接超限，失败日志保留，未缩减栈或借用其他域预算。拆分后 Release 和定向 cursor/grid 回归通过，独立复核未发现语义回归；后继 HIL 必须使用有效模型路径实测 WCET。
- 后继算术优化将常用整除网格的 cursor 除法次数缩减，原子字复制消除 cursor 自身的大块 XIP 复制；通过独立小函数控制体积后，cursor 投影也已驻留 SRAM，原 VDC API 保持不变。构建快照 `20260927035722` 四板升级成功，`run8-fast` 的四板有效 coarse/fine cursor 持续推进，未发生该次 SYNC_TRIGGER 超预算。
- 长测保留失败：`run11-scope` 在后继观测中按每隔 5 s 同步采集四通道，共 12 个新鲜 RAW 窗口；每窗口均在 50 ns 内，最大相对 NO1 偏差约 28.53 ns（本轮快照，不证明采样间隙或绝对 ordinal）。NO3 在此期间出现 27189-cycle 调用并触发 SYNC_TRIGGER 隔离，整体仍为失败，不能关闭本地执行预算。其他三板游标继续推进；最终四板 STOP 和输出参数恢复均读回。下一步分段计时定位偶发超预算，同时接入预装动作硬件 deadline。

### T2-PROGRESS-20260927-018：静态装载跑通与运行时间基阻塞定位

- TODO task ID：T2-DEPLOY-002、T2-CLOCK-001/002、T2-EXEC-001。修复 STOP grid 配置对 RUN committed model 的循环依赖：无锁定模型时只保存共同 grid 几何与 staged ring 身份，精确规划仍需原模型门禁。四板 STOP 实测 grid、deployment、模板资源和 T2 配置依次返回成功；NO1 COUNTER 在 ARM 前 START 成功。TRN-03 复跑的旧 recorder `FROZEN` 需先 SAVE 到 `SAVED` 才能 ARM，新工具前置读回/保存已通过 124 项 host 回归。
- 本切片最终源码四板 P3 build=`20260927024513` 为 `PASS_WITH_WARNINGS`（INFO 32 / WARN 12 / ERROR 0 / FATAL 0），原件在 `out/HardwareAcceptance/20260927/p3-104507/`。先前 `p3-094807/` 的 NO3 TDMA 第 7 个记录缺失失败及 `p3-100619/`、`p3-103123/` 的中间通过均保留，不互相改写。相关静态 grid、T2、事件镜像、模板恢复和 TRN-03 回归共 237 项通过，Release build 及链接门禁通过。
- RUN 探针：四板新 session、静态装载、COUNTER START、ring ARM/START 均读回成功，但 `SYSTem:TDMA:SCHEDule?` 的 quarantine mask 为 72，其中 bit 6 是 `SYNC_TRIGGER`；显式关闭再启用该负载后，四板各再次出现一次超过 `PROJECT_CORE1_PHASE_SYNC_TRIGGER_WCET_CYCLES` 的调用并重新隔离。NO1 cursor 曾从 `NO_SYNC` 前进到 `MODEL_TIME`，NO2–NO4 前进到 `NO_LOCK`，但均未得到有效共同 q。
- 同一 ring 的 VDC `sync_seq` 是稀疏 observation 序列，不能证明每个 1 kHz 输出脉冲都推进一次 cursor。NO2–NO4 在原 FOLLOWER 模式下只保留本地观察诊断且 DCO 没有共同锚；临时切为 MASTER 的有限对照仍因 `VDC_DOMAIN_GATE_LOCAL_PHASE_UNALIGNED` 拒绝正式锁定。T2 cursor 的未锁定分支已提前返回，不再复制模型或处理事件；该预算优化不授予从板时间资格。没有共同相位映射前不得放宽 T2 的锁定门或用 TDMA 计数代替 VDC 时间片。
- STOP 恢复：四板 ring/T2 STOP，NO1 COUNTER STOP 后模板、deployment、grid 全部释放读回成功。代码已把模板释放顺序固定为 COUNTER STOP→UNLOAD，避免运行中的 counter 令模板/部署清理卡住。`node-sequence` 分支可参考静态槽位与 IO 管理，但没有现成的共享 VDC q 硬件 deadline 执行器。当前尚无真实位置动作、READY 或示波器 `position_id` 证据；下一项先建立逐脉冲时间源和从板共同相位映射，再预算化本地动作执行。

### T2-PROGRESS-20260927-017：异步窗口预测与本地最新镜像回退

- TODO task ID：T2-TURNTABLE-002/003、T2-TRANSPORT-001、T2-ACCEPT-001。NO1 Core1 只取得每个 VDC 基准同步的计数观察；Core0 在 `app_refmem_service()` 边界按最近完成的计数窗口异步计算预约位置时间片。距离阈值越近，窗口和计算间隔缩短；没有新结果时复用上一次完整预览。
- 阈值跨越现在提交 `TRIGGER_POSITION_STAGE_FREEZE`，TDMA 普通 T2 帧只发送 `position_id` 和 `prediction_version` index；完整预览仍沿现有 T2 descriptor 分片发送，不改变 VDC cadence 或 DPLL/SYNC owner。每个板本地保留最新预览和两份旧预览，CRC 覆盖窗口字段。
- 从板收到冻结 index 后不在临界路径重新计算：有匹配镜像则采用匹配版本；缺失或版本不一致时使用本地最新有效镜像，并在 T2 snapshot/SCPI 查询中记录远端版本、本地版本、本地镜像年龄、fallback 和 missing 计数。
- host 验证：`test_trigger_t2_runtime.py` 已改为积累完成窗口、验证预览后冻结及版本不一致回退；T2 descriptor/mailbox/flight 合计 `23 passed`。Release 增量构建、三个 link contract、docs_check 和 doc_regression 均通过；进展日志 WARN 为仓库既有轮转债务。
- 当前硬件证据：当前源码指纹 P3 build=`20260926171324` 最终 `PASS_WITH_WARNINGS`（INFO 31 / WARN 15 / ERROR 0 / FATAL 0），证据在 `out/HardwareAcceptance/20260927/p3-011317/`，四板 DPLL 60 秒和 TDMA process-image/FIFO 阶段完成。P3 结束后重新启动 T2 专用 ring 时 NO4 ARM 超时，当前仍没有共同 VDC observation/session，不能宣称真实 `position_id`、READY 或示波器动作闭环；失败原件保留在 `out/t2-ring-p3-20260927/`。

### T2-PROGRESS-20260926-016：位置片普通帧同载与物理准入纠偏

- TODO task ID：T2-TRANSPORT-001、T2-CLOCK-001。代码对账发现旧的“仅空闲 flight cycle 发送位置片”无法保证进展：Core0 RefMem 任务按 `app_tasks.c::task_refmem_sync()` 至少每轮延迟一次，普通 VDC 状态的间隔由 `DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_INTERVAL_MS` 定义；同时物理 origin/overlay 白名单未准入独立 T2 class。原片段重组还要求相邻 transport sequence 连续，与普通帧交错相冲突。
- 当前实现将 T2 index/count/data 放入 `TDMA_PROCESS_IMAGE_T2_FRAGMENT_*` 指定的二等 RefMem 区域，普通 VDC 状态、ACK 和 mailbox CRC 保持原位置；正常到期帧携带 T2 片段时仍推进 `last_publish_ms`。typed SYNC、专用 VDC feedback/boundary 仍优先，T2 不改 VDC/DPLL phase 或特等邮箱。flight 准入独立识别 T2 class，普通 RefMem delta/parser 不把它当业务 delta。
- 片段重组允许普通帧造成的 sequence 空隙、同一 resident 片段重复和 16 位 sequence 回绕；旧序号、乱序、错源/目标及坏 CRC 继续拒绝。物理 mailbox、优先 RX、发布节拍与位置 descriptor host 回归通过；真实四板 T2 class 送达、从板 `position_id` 和示波器动作仍待本轮固件 HIL 复核。
- Release build=`20260926123251`，当前源码四板 quick P3 build=`20260926123557`，`PASS_WITH_WARNINGS`（INFO 33 / WARN 10 / ERROR 0 / FATAL 0），源码指纹=`532c63b6d653f7e797c25b278afe4fd86c4a6c577999e476b2aaad3b24722ad7`，证据目录 `out/HardwareAcceptance/20260926/p3-203549/`。外部 HDO 60 s PASS，最大采样相差 `39.669735326297236 ns`；该值为采样快照，`strict_gates_passed=false`，不授予 T2 实际动作精度。
- P3 停机后只读读回（当前构建，COM5/COM4/COM3/COM6 对应 NO1/NO2/NO3/NO4）：本地 VDC `sync_seq` 分别为 560/255/263/226，follower `LOCK:READiness?` 仍为未就绪；位置运输快照全零。该读回证明各板本地序号不能直接当共同 q，且本轮未激活 T2 预约。下一步先完成 `T2-CLOCK-002` 的共同 anchor/同事件绑定，再进行实际 position_id 动作试装。

### T2-PROGRESS-20260926-015：异步位置描述符交接与 TDMA owner 分工

- TODO task ID：T2-TRANSPORT-001、T2-TURNTABLE-003、T2-TURNTABLE-004。新增 `trigger_position_mailbox` 固定单槽状态交接：Core1 T2 phase 只提交完整 descriptor；Core0 RefMem service 领取后编码 wire、分片并在 VDC 未到期的普通机会发布。发送中或单槽忙时拒绝新请求，不覆盖在飞片段。
- RX 在 Core0 验证本板 target mask、重组完整描述符后，经 source slot 专属单槽交给 Core1；Core1 一次性领取，避免直接复制 Core0 正在写的多字段结构。T2 STOP 以原子撤销意图交给 Core0 清除未发送请求/片段，已入 FIFO 的片段继续由 receiver 代际门禁约束。
- host：mailbox、生产版提交/编码/撤销函数、T2 runtime 与 VDC reference spacing/publish 合计 78 项通过；补齐 VDC host harness 既有的 `sync_trigger/inc` include path。Release 增量构建与三个 link contract 通过。
- 当前源码四板 quick P3 build=`20260926105323`，`PASS_WITH_WARNINGS`（INFO 33 / WARN 9 / ERROR 0 / FATAL 0），源码指纹=`dd026409bf31e0118d75e4abc0fd1562bb40eb8d535937941ee24a704e32f70b`，证据目录 `out/HardwareAcceptance/20260926/p3-185313/`。外部 HDO 60 s 结果 PASS，最大采样相差 `41.85197145025214 ns`；该值为采样快照，不是连续精度契约，quick 凭证的 `strict_gates_passed=false`。
- 本轮 P3 只验证基础 TDMA/DPLL 与四板输出未明显退化，未在 TDMA RUN 中完成真实 T2 position_id 广播、从板动作边沿或 READY 证据；这些仍按 TODO 保持开放。

### T2-PROGRESS-20260926-014：按实时负载规范收紧 T2 描述符绑定

- TODO task ID：T2-TRANSPORT-001、T2-TURNTABLE-003、T2-TURNTABLE-004、T2-EXEC-001。T2 继续只挂载 `APP_REALTIME_PHASE_SYNC_TRIGGER`；VDC/DPLL/TDMA phase 不调用 T2 runtime。
- 描述符绑定修正：`sync_epoch` 使用本地 grid epoch，不再误用 feedback session；从板同时校验 `sync_epoch`、`count_epoch`、grid/deployment generation/CRC 和有效同步窗口。
- 描述符有效期改为“目标同步位置 + 配置中的 grace window”，当前 SCPI 配置使用 `TRIGGER_T2_DESCRIPTOR_TARGET_GRACE_SYNC` 默认值；不再使用 `UINT32_MAX` 长期有效期。同一描述符重复到达幂等忽略；位置实例一旦发出，不接受该位置的不同版本回写。
- 本地动作 staging 改为先在临时 runtime 中展开和冲突检查，确认所有 action q 仍在当前 cursor 之后且落在当前可服务的 coarse 边界后再提交；非零 fine q 明确拒绝，不能在下一次 coarse service 迟到补发。模板资源未在 STOP 阶段装载或绑定不当前时，T2 SCPI 配置拒绝进入运行态。
- VDC `sync_seq` 缺口使 T2 cursor 在该会话内保持失效，直到新 session/grid 起点建立；不改变 VDC 的发布或 DPLL 控制状态。
- 定向回归：T2 runtime、cursor、action runtime、descriptor 和 phase/owner 共 27 项通过。Release build=`20260926093111` 及最终增量 link contract 通过。最终当前源码四板 quick P3 build=`20260926100116`，`PASS_WITH_WARNINGS`（INFO 33 / WARN 10 / ERROR 0 / FATAL 0），源码指纹=`c68e852171292589f600ef4374dc3892d5e03222c7570f72ff6e031b1c191c92`，证据目录 `out/HardwareAcceptance/20260926/p3-180108/`。外部 HDO 60 s 结果 PASS，最大采样相差 `32.14992688125575 ns`；该值为本轮采样快照，不是连续精度契约。quick 凭证的 `strict_gates_passed=false`，不能宣称严格产品验收。
- P3 是基础链路与 DPLL 验收，未装载并执行真实 T2 position_id；非零 fine q 的硬件 deadline、跨板同一同步事件身份、READY 与实际动作 latch 仍按 TODO 保持开放。

### T2-PROGRESS-20260926-013：实时负载挂载边界与 observer phase 分离

- TODO task ID：T2-CLOCK-001、T2-TRANSPORT-001、T2-EXEC-001、T2-ACCEPT-001。新增 `ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` 草案，区分 Core1 phase 与 TDMA seat，明确 TDMA、VDC、DPLL、SYNC_TRIGGER/T2 的唯一 owner、标准 staging→gate→STOP/ARM→RUN 流程和越界回滚规则。
- T2 固定挂在 `APP_REALTIME_PHASE_SYNC_TRIGGER`；position descriptor 使用 `TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS` 的一等普通运输机会，VDC 到期时让路，不能改变 VDC cadence 或 `last_publish_ms`。
- 修正 `application/src/app.c`：`tdma_runtime_owner_service_observer()` 只在 TDMA phase、`tdma_component_core1_service()` 之后调用一次，从 VDC/DPLL phase 移除；源码回归与相关 31 项 pytest 通过。
- Release build=`20260926082128`；当前源码四板 quick P3 build=`20260926082353`，结果 `PASS_WITH_WARNINGS`（ERROR/FATAL=0），receipt source tree sha256=`20f543dbc984d86100e931cfaf7951daa0ab2d584ca2f5bb0269407c1a2d14e6`，证据目录 `out/HardwareAcceptance/20260926/p3-162345/`。本轮 P3 的既有 TDMA diagnostic warning 保留，不宣称 T2 position_id/READY 硬件闭环已完成。

### T2-PROGRESS-20260925-012：修复 feedback session 哨兵并完成当前源码 P3

- TODO task ID：T2-CLOCK-001、T2-ACCEPT-001。修复 `model_feedback_set_stopped()` 对 `UINT32_MAX` 清 session 哨兵的处理：清除活动 session，但不再写入 monotonic admission watermark，避免后续所有非零 session 被永久拒绝。新增 owner 回归覆盖“哨兵清除后重新建立非零 session”。
- 当前源码 P3：build=`20260924161306`，四板 quick P3 为 `PASS_WITH_WARNINGS`（INFO 33 / WARN 16 / ERROR 0 / FATAL 0）。外部 HDO 5..60 s 共 12 个窗口全部有效，`HAOFV_P3_DPLL_SHORT_V4` 为 `PASS`，最大相对相差绝对值 `49.818003747704054 ns`；证据目录 `out/HardwareAcceptance/20260925/p3-001255/`。数值为本轮采样快照，不是连续全区间精度契约。
- 现场复核确认：旧的 `UINT32_MAX` session 清理流程会使新的 session 命令进入拒绝路径；修复后主机 owner/SCPI 回归通过。四板真实 `position_id` 闭环仍未关闭，后续需在新 session 下重新建立连续 follower observation，再进入 T2 动作验证。

### T2-PROGRESS-20260924-011：当前源码指纹下 DPLL 60 s 外部复测

- TODO task ID：T2-CLOCK-001、T2-ACCEPT-001。由于 TDMA event bridge/CMake 在上一张凭证之后发生过实现变更，先以当前源码重新执行四板 P3；build=`20260924142804`，结果为 `PASS_WITH_WARNINGS`（INFO 33 / WARN 19 / ERROR 0 / FATAL 0），当前凭证已绑定源码指纹。
- 外部 HDO 按 5 s 间隔采集 5..60 s 共 12 个四通道 RAW 窗口，全部 `capture_complete=true` 且无 overrun/missed；`HAOFV_P3_DPLL_SHORT_V4` 校验通过，60 s checkpoint 无窗口失败。证据目录 `out/HardwareAcceptance/20260924/p3-222755/`。
- 12 个窗口的最大四通道相对相差绝对值为 `35.893592718926406 ns`（sample-0005）；该数值是本轮采样快照，不是连续全区间精度契约。此次结果确认当前固件改动未破坏 DPLL/VDC 外部发布相位，但不提升 follower observation eligibility，也不关闭 position_id/READY 闭环。

### T2-PROGRESS-20260924-010：follower latch 恢复与 event bridge 边界确认

- TODO task ID：T2-CLOCK-001、T2-EXEC-001、T2-ACCEPT-001。将 `PROJECT_TDMA_EVENT_OBSERVER` 保持在正常固件路径，并在 process-follower 的 `local_tx_edge_ex` 接入 exact event bridge：只有 observer epoch/ARM anchor、单调事件记录、TIMER1 时钟和 transport sequence/identity 全部匹配时才生成本地 TX latch；不匹配继续拒绝。
- 当前指纹 P3：build=`20260924125233`，四板 quick P3 为 `PASS_WITH_WARNINGS`（33 INFO / 19 WARN / 0 ERROR / 0 FATAL）。外部 HDO 5..60 s 共 12 个窗口全部有效，`HAOFV_P3_DPLL_SHORT_V4` 为 `PASS`，最大相对相差 `37.941900686 ns`；证据目录 `out/HardwareAcceptance/20260924/p3-event-bridge/`。这确认 event observer 仍不干扰已稳定的 DPLL 输出相位。
- live ring probe 显示四板 hardware latch `resolution=8 ns`、clock observation valid/correlated；NO1 VDC/DPLL 正常。NO2–NO4 的 ring observation 也已产生，但 follower Domain 仍明确走 local-evidence bypass，quality accepted/LOCKED 不提升。event TAP 的 `prefix=98/128` 试验记录仍与 transport sequence 不一致（仅得到 event sequence 1/45），exact bridge 因此保持 fail-closed，不能把诊断记录冒充 DPLL eligibility。
- 后续 prefix 扫描原件 `out/t2-event-tap-prefix-scan-20260924/` 不具备判定力：候选窗口的 `VDC:OUTPut:RUN`/`RING:START` 出现超时，读取到的是同一条历史 LIVE 记录且 `ring_down_rx_sequence=0`；所有 TAP 请求已恢复为 STOP/disabled，不把该轮当作序列映射证据。
- 结论：`sync_seq` 的从板发布已能在 legacy-latch 构建中推进，但要让 RUN cursor 获得正式 `LOCKED` 门和位置动作资格，还需冻结 process-image transport sequence 的硬件采样字段或建立可信的 wire identity 关联；本轮不关闭 M1/M3，也不开始实际 position_id 动作矩阵。

### T2-PROGRESS-20260924-009：当前指纹 DPLL 60 s 外部复测完成

- TODO task ID：T2-CLOCK-001、T2-ACCEPT-001。当前源码经定向回归、Release build 和四板 quick P3 后，使用外部 HDO 按 5 s 间隔完成 5..60 s 共 12 个四通道 RAW 窗口；证据目录 `out/HardwareAcceptance/20260924/p3-dpll-rerun3/`，build=`20260924113926`，P3 为 `PASS_WITH_WARNINGS`（INFO 33 / WARN 19 / ERROR 0 / FATAL 0）。
- 外部结果：`HAOFV_P3_DPLL_SHORT_V4` 为 `PASS`，12/12 窗口完整，最大四通道相对相差绝对值 `56.416580 ns`，按本轮 ±1 us 判据无窗口失败；scope setup 首条缩写 `:SYST:ERR?` 在当前 HDO 返回 `-100`，公共 reader 已记录并回退到 `:SYST:ERRor?`，本轮错误队列正常清空。上述数值均为本次采样快照，不是连续全区间精度契约。
- 停机后只读状态：NO1 `SYST:SYNC:VDC:STATus?` 的 `sync_seq=533`、readiness `locked=1`；NO2–NO4 的 `sync_seq=0`、readiness `reason=2`。这表明外部 DPLL 输出相位稳定，但从板没有形成有效 VDC observation/sync_seq；不能用 `ring_down_rx_sequence` 或其他 transport 计数替代 VDC 序列。下一步保持 DPLL/VDC 发布路径不变，定位从板 hardware-latched observation eligibility 与本地发布闭环。

### T2-PROGRESS-20260924-008：VDC 优先与位置片低优先级快速发布

- TODO task ID：T2-TRANSPORT-001、T2-CLOCK-001、T2-TURNTABLE-003。明确两条发布语义：VDC sync token 继续使用现有 priority/正常 flight owner；position descriptor 使用同一确定性 flight owner 的低优先级一等普通席位，带独立 message class 和 descriptor CRC/`valid_from_sync` 绑定。
- 调度规则：当 VDC/feedback/boundary 正常帧到期时，先发布 VDC；position fragment 只占用未到普通 VDC cadence 的空闲 flight cycle，position 发送不推进 `last_publish_ms`，不会降低 VDC 发布频率，也不新增 priority 席位。位置预测提前完成，因此保留 descriptor 的代际、sync sequence、grid 和 deployment 校验优先级。
- 代码证据：`distributed_refmem_tdma_flight_sync_publish()` 已按 `normal_due` 分离 VDC 与 position 发送；最终 P3 receipt 绑定当前 staged source，`PASS_WITH_WARNINGS`，ERROR/FATAL 为零。
- 硬件边界：当前 HIL 已看到 NO1 origin 的 VDC sync sequence/counter 推进；从板 observation eligibility 尚未在同一轮形成有效 sync sequence，因此仍未关闭四板 position_id/实际动作边沿/READY 闭环。

### T2-PROGRESS-20260924-007：VDC sync_seq 与 Core1 COUNTER 运行期接线

- TODO task ID：T2-CLOCK-001、T2-TURNTABLE-001/002、T2-EXEC-001、T2-ACCEPT-001。cursor 现在直接消费 VDC Domain `vdc_status.sync_seq`；诊断 observer 只保留可选 `last_event_id` provenance，不再作为运行中 1 kHz 时间片来源。NO1 COUNTER 增加 Core1 有界新鲜 FIFO 快照和 seqlock 发布，origin runtime 能在 TDMA RUN 中读取计数。
- 四板短 HIL：在当前 build 上，COM5/NO1 origin 进入 T2 RUN 后 `last_sync_sequence` 与 counter count 持续增长（原始记录见 `out/t2-runtime-hil-20260924/vdc-seq-runtime-evidence.jsonl`）；位置阈值尚未跨越，因此 published mask 为零是预期结果。部分 follower 的 VDC `sync_seq` 未发布，仍报告 cursor 无同步，未形成四板 position descriptor/action 闭环。
- 失败边界：曾试验用 TDMA `ring_seq` 作为 follower fallback；一块板出现 sequence 回退，已撤销该实现和测试，避免把 transport sequence 冒充 VDC sync。当前正式代码在 follower 缺少 VDC observation eligibility 时保持 fail closed。
- 调度证据：早期运行片把 `SYNC_TRIGGER` phase 推入 quarantine，根因为 Core1 counter 快照使用过大的 FIFO 轮询；现已将实时路径轮询上限收紧为有界窗口，最终 P3 未出现 ERROR/FATAL。P3 receipt 仍为 `PASS_WITH_WARNINGS`，严格质量字段保持原值。
- 清理：每轮 HIL 均在 TDMA STOP 后停止/卸载 COUNTER、释放 template、清理 deployment；scope 仍接四板 OUT1，未宣称实际 position_id 边沿或 READY 硬件 latch。

### T2-PROGRESS-20260924-006：四板 T2 SCPI/HIL 配置探针

- TODO task ID：T2-DEPLOY-002、T2-TURNTABLE-003、T2-TRANSPORT-001、T2-ACCEPT-001。使用当前 P3 build `20260924034302` 对 COM3–COM6 做受控配置探针：deployment 代际提升后四板 STAGE/ACTIVATE 读回通过；首次 template CRC 计算错误和后续修正均保留在 `out/t2-runtime-hil-20260924/scpi-evidence.jsonl`。
- 结果：修正 CRC 后 NO1/NO3/NO4 template install 成功，NO2 在 STOP template install 返回 BUSY；四板 T2 grid configure 返回 `TRIGGER_GRID_PREVIEW_MODEL`，未进入 T2 START，也未产生位置动作或边沿证据。原因尚需在停止态保留有效 VDC/TDMA binding 后复核，不能把该探针当作 runtime 失败或通过。
- 清理：四板 T2 STOP、TDMA RING STOP、template RELEASE/CLEAR、deployment CLEAR 全部返回成功；清理原始响应保存在 `out/t2-runtime-hil-20260924/cleanup.jsonl`。当前仍无实际 `position_id` 示波器证据。

### T2-PROGRESS-20260924-005：cursor/predictor/position/runtime service 接线

- TODO task ID：T2-CLOCK-001、T2-TURNTABLE-001/002/003/004、T2-TRANSPORT-001、T2-EXEC-001。新增 `trigger_t2_runtime` bounded realtime service，并在 `sync_trigger_service()` 中接入；runtime 每次新 VDC cursor sample 读取本板 COUNTER，按 count epoch 更新单样本 pulses-per-sync 估计，在起始/零位/终止阈值跨越后逐个占用 flight mailbox 发布位置描述符。
- 从板路径：RefMem realtime RX 之后读取 source descriptor，校验 run/grid/deployment 身份和有效同步区间，调用本地 `trigger_action_runtime` 展开模板实例；cursor 到达动作 q 后生成带 `position_id` 的本地事件队列。新增 `TRIGger:REServation:T2:CONFigure/STARt/STOP/?`，配置读取当前 grid、活动 deployment 和静态 template store，阈值和起止角度由外部参数提供。
- 细分语义：预测 q 使用当前 cursor 与 measured pulses-per-sync 解算 coarse/fine；计数仍是位置权威，速度估计只用于将目标计数映射到未来时间片。位置运输保持单描述符分片占用，flight owner 忙时不覆盖正在发送的 descriptor。
- 主机验证：origin 阈值跨越发布起始/零位 descriptor、receiver 身份校验/本地 q 展开/事件输出共 2 个 runtime 场景，连同 cursor/predictor/descriptor/template/action/READY 相关回归共 47 项通过；Release app/app-B/boot、PIO、SCPI namespace、Flash map 和 link contract 通过。
- 当前边界：runtime 已调用已装载 SWITCH/GATEWAY PIO 的 Core1 fire/service 接口，但尚未有当前指纹对应的四板实机边沿、READY 硬件 latch、实际 `position_id` 示波器证据或恢复闭环；本片不关闭 M3–M5 验收。

### T2-PROGRESS-20260924-004：位置描述符接入 flight mailbox owner

- TODO task ID：T2-TRANSPORT-001、T2-TURNTABLE-003、T2-TURNTABLE-004。沿现有 `tdma_service_publish_flight_tx` 和 32 字节 flight mailbox 增加独立 T2 position message class；每个 mailbox 携带一个 4 字节 descriptor fragment，发送端保持 target mask/sequence，接收端按 source slot 重组。
- 接口：NO1/业务 owner 通过 `distributed_refmem_stage_position_descriptor()` staging 最新 descriptor；发布成功后逐片推进，完整描述符在从板由 `distributed_refmem_get_position_descriptor()` 读回。旧 descriptor、乱序、换源和 CRC 失败不会形成有效位置。
- 边界：该接入复用现有 flight FIFO 和 Core0 RefMem owner，不新增 Core1 实时席位；尚未连接实际 NO1 predictor 调用、目标板动作实例展开或 READY 校验。
- NO1 连接准备：predictor 新增 descriptor builder，仅允许已跨阈值的 start/zero/terminal position 生成 descriptor，并绑定 count epoch、target count、prediction version、grid/deployment 身份和有效同步窗口；实际运行 service 仍待接线。
- M3 接口准备：新增本地 position instance expansion，将 descriptor 的 coarse/fine 索引按 profile `delta_q` 解算为动作 q，拒绝下溢/上溢；尚未连接静态 SWITCH/GATEWAY 模板或 PIO 执行。
- 静态模板准备：新增 template kind/role/argument/span/delta_q 定义和 descriptor→独立动作实例展开；SWITCH/GATEWAY 仍不在运行中热加载，PIO 执行接入尚未开始。
- 模板表准备：增加有界模板表的 generation、CRC、template ID、role/kind 和输出参数校验；重复 ID、错 role/kind 和错误 CRC 均拒绝，尚未连接启动期 PIO 资源读回。
- 静态模板 owner：新增 STOP-only template store，支持 deployment 校验、同代幂等、generation 高水位和 CLEAR；它只保存模板元数据，不申请 PIO/SM/GPIO，也不授予 RUN 执行权。
- 资源装载边界：template store 新增显式 `activate_resources/release_resources`，按本板 deployment role 调用现有 COUNTER/SWITCH/GATEWAY LOAD/UNLOAD owner；静态模板元数据与 PIO lease 分离，仍需四板启动期读回验证。
- 主机验证：template store 的 install、STOP busy、同代幂等、CLEAR 高水位和 SWITCH 资源 activate/release 共 5 项通过；尚未进行四板实际静态资源读回。
- 本地执行准备：新增 bounded action runtime queue，接收 descriptor 展开后的模板实例，检查 q 区间冲突，在 cursor 到达时一次性生成带 position_id 的动作事件；重复 cursor 不重复发射，cursor 回退显式拒绝。尚未连接 PIO 写码。
- 软件证据：新增 owner 接入后的完整 Release 构建和三份 flash link contract 通过；本片未执行新的 P3，待 position descriptor staging、四板接收读回和本地实例展开形成完整切片后再执行。

### T2-PROGRESS-20260924-003：转台起始/零位/终止预测模型

- TODO task ID：T2-TURNTABLE-001、T2-TURNTABLE-002、T2-TURNTABLE-009。新增确定性 predictor 模型，使用计数 epoch、脉冲/度、起始/零位/终止角度、前后 5° 区间和外部阈值生成三类目标计数与阈值计数。
- 预测语义：每个位置只在计数跨过阈值时产生一次 position mask；终止后的 post-roll 只设置终止校验状态，不新增或重排终止动作；计数回退显式返回 BACKWARD。
- 主机验证：目标计数、阈值边界、终止校验和回退处理共 4 项通过。该模型尚未连接真实 COUNTER、速度估计、TDMA 广播或硬件输入边沿。

### T2-PROGRESS-20260924-002：位置描述符基础与本地 q 解算

- TODO task ID：T2-TURNTABLE-003、T2-TURNTABLE-004、T2-TRANSPORT-001。新增 `trigger_position_descriptor_t`，明确 NO1 广播的业务字段为 `position_id`、`target_ms_index`、`target_fine_slice_index`，并绑定 run/sync、count epoch、prediction version、grid/deployment generation 和有效同步区间。
- 接收端校验：schema、身份字段、细分范围、有效窗口和 canonical CRC；验证通过后才允许将 coarse/fine 索引转换为本地绝对 `q`，不传输主站 TIMER1 deadline，不使用消息到达时间作为动作时间。
- 运输基础：增加固定 4 字节顺序分片 codec，带 source/target mask、transport sequence、重复/乱序/换源拒绝和 descriptor CRC；它复用现有 RefMem/TDMA 分片边界，但尚未接入实际 TDMA owner。
- 主机验证：位置描述符有效性、CRC 变更、细分越界、窗口倒置、schema 错误、q 解算、完整分片和乱序拒绝共 9 项通过。TDMA wire 发送、从板实时接收和动作展开尚未实现。

### T2-PROGRESS-20260924-001：1 kHz VDC 时间片 cursor 基础

- TODO task ID：T2-CLOCK-001、T2-DEPLOY-002。新增 `trigger_time_cursor`，消费现有 VDC sync observer 的 `last_sample_seq`，为每个新同步样本推进 `pulse_index`，派生 `fine_slice_index=0` 和 `action_cursor=pulse_index*subdivisions`，并记录 session、model token、DCO update sequence、event ID 和丢样本计数。
- 语义边界：cursor 不输出 GPIO、不装载 PIO、不授予动作执行权；grid 配置成功时建立 cursor 配置，CLEAR 时清除。session 换代重新建立起点；样本序号跳跃标记 `SAMPLE_GAP` 并使 cursor 无效，不能静默执行旧位置。
- 观测接口：新增 `TRIGger:REServation:CURSor?`，返回配置、有效性、grid、session/sample、模型代际、脉冲序号、action cursor、丢样本和错误码，供后续位置实例与示波器证据绑定。
- 主机验证：cursor/grid/action 相关测试共 246 项通过；本片未宣称四板 live RUN、实际 VDC 1 kHz 边沿或 T2 动作执行完成。硬件证据仍需当前 staged 源码指纹对应的 P3 与后续示波器闭环。

### T2-PROGRESS-20260923-008：转台起止边界与精确位置输出语义

- TODO task ID：T2-TURNTABLE-001、T2-TURNTABLE-004、T2-TURNTABLE-009。用户补充转台从起始位置前 5°开始运行，在终止位置后 5°停止；前置区用于起始预测，后置区用于终止校验。5°/5°是当前调试配置快照，后续由外部 profile 配置，不作为产品额定数字。
- 语义冻结输入：预运行区建立 count epoch、方向、周期/速度估计和首个预测版本；零位和终止位置各自绑定确定的 target count、position ID、prediction version 和 coarse/fine 时间片。动作必须在零位/终止位置的计数边界执行，不能用区间中心、主机到达时间或 READY 电平代替实际边沿。
- 终止位置之后的校验区只确认最后位置动作已到达，检查迟到、重复、缺失和越过终点的额外动作；停止事件不得回写或重排已经执行的终止位置动作。
- 本条只更新 Architecture/TODO/Progress 语义，未宣称当前 COUNTER 快照已经具备边沿 latch、精确零位/终止位置输出或示波器证据；实现后需按 position_id 绑定输入边沿、动作边沿和恢复证据。
- 文档门禁：`docs_check.py --strict-names`、`doc_regression_check.py --progress` 和文档 pytest 38 项通过；全库既有进度轮转债务仅保留 WARN。
- Git Bash 运行 `.githooks/pre-commit` 通过，检测到仅文档范围变更，P3 固件门禁按规则跳过。

### T2-PROGRESS-20260923-009：起始角度和终止角度纳入预测边界

- TODO task ID：T2-TURNTABLE-001、T2-TURNTABLE-003、T2-TURNTABLE-009。用户进一步明确：起始角度和终止角度本身也是预测目标；前置 5°用于建立起始预测，后置 5°用于继续校验终止预测。
- 语义修正：起始角度、零位、终止角度分别生成 position_id、预测目标计数、prediction version 和 coarse/fine 时间片；动作绑定的是确定的预测目标索引，实际到位仍需用输入/动作边沿验证，不能把边界角度当作已知硬件 latch。
- 本条只更新文档语义，未宣称当前 COUNTER 已实现起止角度预测、实际边沿 latch 或终止校验区证据。

### T2-PROGRESS-20260923-007：按审核语义重构 T2 Architecture/TODO

- TODO task ID：T2-DOC-001、T2-CLOCK-001、T2-TURNTABLE-002/003/004、T2-EXEC-001、T2-READY-001。基于 `out/t2-architecture-review-20260923/corrected-frozen-semantics.md` 重构三件套，保留历史实现和失败证据，不把当前源码能力升级为完整 T2。
- 语义纠偏：细分脉冲只做能力验证；运行时由 1 kHz VDC 基准同步推进本地 pulse index/fine cursor。NO1 每次同步更新预测，阈值跨越时通过 TDMA 广播一次位置索引；从板使用本地 VDC/grid/model 解算，不接收主站逐板 TIMER1 deadline。
- 执行模型：动作模板、PIO、IO 和循环表在 TDMA START 前静态装载；动作表拆为静态循环模板和运行时位置实例；动作互相解耦，到达自身时间片即执行，不再使用“更新下一动作”的串行语义。
- 反馈模型：SWITCH settle、GATEWAY TRIG、READY 检查窗口使用可配置相对 offset；READY 只校验上一动作实际响应与相邻时间片是否重叠，不作为当前动作执行许可或 fence。
- 交付边界：现有 STOP action install/一次性 ARM 后端仍标记为迁移材料；本次文档重构没有声称 1 kHz cursor、位置描述符、从板解算、静态启动装载或实际 T2 latch 已实现。
- 证据位置：语义审核快照为 `out/t2-architecture-review-20260923/corrected-frozen-semantics.md`；本条只记录文档重构，后续代码实现必须分别经过相应 host/Release/P3/示波器证据。

### T2-PROGRESS-20260923-006：共享时间片动作表安装

- TODO task ID：T2-ACTION-INSTALL-001。用户要求试安装时间片动作。本轮按安装与读回切片推进，不使用现有维护 APPLY 假冒本地按 q 执行。当前没有可直接复用的 VDC marker→PIO 动作 ARM 通道：RUN OUT1 程序不发 PIO IRQ，转台 IN1 与同步源不同；细分末格空白也意味着不能直接把物理脉冲序号当绝对 q。
- 安装内容：完整表包含动作表代际、部署 generation/CRC、grid epoch/generation/A/P/N 与有限动作行，规范小端 CRC 不含结构体 padding；每行独立 action ID、业务角色、动作类型/参数、绝对 q、计划占用格数。全局严格 q 顺序与半开区间不重叠，保留格只禁起点；参数按已部署 IO mask 验证。IO/时基/执行资源不由表内容自授予。
- 事务：`trigger_action_service` 只在 TDMA STOP metadata 内校验并发布 SRAM，不操作 GPIO/FIFO。全表四板一致，本地行位图随角色绑定。INSTALLED_CONFIG 的执行授权、ARM、deadline valid、anchor authenticated 为零；部署变更保留旧表但报告 binding_current=0，ROW 返回 CHANGED。CLEAR 保留高水位，旧代不得复活。
- 诊断样本计划：沿独立 500 ns 网格安装 SWITCH_A、SWITCH_B、GATEWAY 三行，分别 q=2010/2012/2014、各占两格；该数字为本轮测试快照，A=0 是未认证安装标签，数学目标不是已装载 deadline，也不宣称满足真实器件 settle/READY。原 `out/t2-multi-sync-plan-500ns-20260922` 业务预算保持不变。
- 纯内核 49 项通过（含独立 Python struct/zlib CRC 对照），owner/实际 SCPI 75 项通过，Release 与 flash link 检查通过（数量为本轮快照，非产品事实源）。独立代码复核确认 STOP 锁、完整绑定、旧代隔离、输出保持和未授予执行权限。
- 当前源码 P3：`out/HardwareAcceptance/20260923/p3-action-install-r1/`，build=`20260923102554`，quick `PASS_WITH_WARNINGS`（INFO 32 / WARN 21 / ERROR 0 / FATAL 0）。当前源码指纹、包、四板身份、TDMA 和示波器同步观察均保留，严格质量告警不改写为全绿。
- 四板安装实测：`out/action-install-20260923/run-r1/action-install-evidence.json` 完整通过，四板同表 CRC 和三行原始读回正确；NO1/NO2/NO3/NO4 本地行位图分别 0/1/4/2，对应 SWITCH_A、SWITCH_B、GATEWAY 的数学目标为 1005000/1006000/1007000 ns（A=0 的安装样本，不是未来 deadline）。同代原样重装幂等，保留格、跨度重叠、同 q、重复 ID、错部署 CRC、旧代、同代重解释、错误表 CRC 八类负测均保留原表。
- 换代与恢复实测：新部署代使旧表 installed 仍保留而 binding_current=0，ROW 返回 CHANGED；CLEAR 后旧动作表代仍被拒绝，新表代重新安装通过。阶段前后 COUNTER/GATEWAY/SWITCH 读回、未装载状态、执行计数与输出事实保持，没有输出命令；READY 电平允许随外部信号变化，不把它当成完成。最终四板动作表、临时部署清空并确认 STOP，清理均通过。
- 本轮关闭诊断动作清单安装，不关闭时间片实际执行。共同 anchor 认证、指定 marker/格计数与硬件执行的绑定、实时模型/lead/租约和 READY/fence 仍需下一执行片；安装不发布执行授权或 ARM。

### T2-PROGRESS-20260923-005：SWITCH 本地编码与动态部署

- TODO task ID：T2-SLOT-LINK-001、T2-SLOT-SWITCH-001。用户要求继续 SWITCH 开发部署，随后要求先按指纹提交推送。已先完成 `87235913` 代码与 `ee3d24ed` 文档提交，实际推送 `origin/wip/tdma-real-flight-processing` 并核对远端 SHA；其后才开始本 SWITCH 切片。此前本地无关 skill/评审文件不纳入槽位交付。
- 实现范围：活动 SWITCH_A/B 共用 owner，UID/role/slot/dep generation/CRC/output mask 在 LOAD、STAGE、APPLY 前复核；PIO0 按所选输出拆连续组，预装编码后同步启动，真实回显全组完成才发布 COMPLETE。STOP 取消不伪造完成，卸载恢复所选 GPIO/PIO 状态。当前 APPLY 只允许 STOP maintenance，用于本地输出验证，不是预约 q、fence 或外部开关稳定资格。
- 证据纠偏：最初 backend harness 在 enable 时直接生成回执且编码循环提前退出，原先“全部合法编码和 PIO 已验证”的声明过大；其通过结果只证明受控桩下 C 状态推进。该缺口由独立 reviewer 指出，原日志保留，不据其放行硬件。后继要求解释真实 pioasm 指令、遍历全组合并核对实际选中码、未选 GPIO 与恢复。
- 软件状态：owner 与实际 libscpi 测试 204 项、相邻资源/输出/部署回归 336 项通过，Release 编译成功；计数是本轮快照，非产品事实源。后继真实 pioasm 指令解释器 20 项通过，C 显式断言全部 15 个 mask、80 个合法编码；验证 PULL 尚不输出、MOV PINS 两组同 tick 写码、PUSH 产生回执，未选 GPIO 保留且卸载恢复。真实 manager/runtime/phase 资源测试 2 项通过，覆盖全部 mask 与 reference 双顺序共存、counter/gateway/RUN/legacy 互斥及释放账目。独立复核关闭原覆盖缺口；当前源码硬件证据见下。

- 当前源码 P3：`out/HardwareAcceptance/20260923/p3-switch-r1/`，build=`20260923090341`，quick `PASS_WITH_WARNINGS`（INFO 32 / WARN 21 / ERROR 0 / FATAL 0），包含四板 OTA、当前链路/TDMA 与示波器同步观察；原严格质量告警保留。
- 跨板角色实测：`out/switch-slot-20260923/matrix-r1/` 的 SWITCH_A/NO1–NO4、SWITCH_B/NO4 与 NO2 六轮通过，每轮两次 LOAD/STOP/UNLOAD、三次编码事务；活动 UID、role、slot、完整部署表与 CRC 均读回。两 SWITCH 角色共用实现，非 SWITCH 的 LOAD 拒绝，已加载期间 DEP ACTIVATE/CLEAR 拒绝；每轮四板资源/部署清理和 STOP 确认完成。
- 编码矩阵：`out/switch-slot-20260923/masks-r2/no2-switch-a-masks/switch-evidence.json` 完整通过，在 NO2 遍历全部非零输出 mask 及其合法编码。逐事务核验 STAGE 全部 OUT 不变、所选 PIO group 路由/TX 单字、APPLY 后全组真实回显与完成序号、所选 GPIO 码及未选 OUT 的离散读回保持；重复 APPLY 和越 mask 编码拒绝，STOP 取消队列且不增加完成计数，最终卸载和四板清理通过。全部 80 个编码均为实测，不用离线脚本结果替代。
- 首轮矩阵失败保留：`matrix-r1/no2-switch-a-masks/` 的 DEP CLEAR 返回 `1,33`，先导致 mask3 事务未安装，后于 mask9 清理阶段触发 abort；已执行的编码无输出失败，最终清理四板通过。旧脚本 `switch_slot_hil_v2_r1.py` 保留原 SHA。后继仅对 DEP 的显式 BUSY/NOT_IDLE 拒绝加入 STOP 新鲜读回后有限重试，未知超时或 SWITCH APPLY 不重试；不改变固件和输出判据。新完整矩阵单独留证，旧失败不覆盖。
- 独立证据复核：`out/switch-slot-20260923/independent-review.json` 重算七份合格报告、2637 条原始命令、27 次加载与 116 次编码事务；包含六轮迁移的 36 次写码和独立矩阵的 80 次写码。每个快照回溯 SCPI 原文、JSONL、源文件与当前 P3 指纹，均通过。
- 验收边界：当前关闭的是 STOP 维护态的本地 SWITCH 编码和跨板部署；离散 GPIO 读回不是外部 RF 通路稳定、连续无毛刺或示波器边沿对齐证据。两组同 tick 来自 PIO 指令解释与同步使能语义，尚无专项物理时差量测。没有预约 q、READY/fence 或跨板时间片执行放行。最后状态为停止、租约卸载、临时部署清空，未留下运行中的输出任务。

### T2-PROGRESS-20260923-004：VNA 网关动态加载与槽位任意 IO

- TODO task ID：T2-SLOT-GATEWAY-001、T2-SLOT-IO-001。用户要求继续 VNA 网关动态加载，并明确各槽位可选任意输入/输出，COUNTER 也可选任意 IN；指定参考 node-sequence 的 `tools/sequence_trigger_debug_ui`。本片只交付加载、停止、卸载及 IO 路由，不以即时主机触发替代本地预约格执行。
- 本条测试数量、build 和矩阵计数均为本轮快照，非产品事实源；资源预算以 `sync_io_gateway.h`、`sync_gateway.pio` 与 persona 描述符为准。
- 参考来源：本地远端跟踪引用 `origin/feature/node-sequence-reservation-trigger@0c913e83f241fa8b40c059792141ae6051a85139`。UI 的 READY input、trigger output、counter input 是独立配置，输出按逻辑 mask 分配；本片沿用该语义，以当前 deployment 表为唯一 IO 来源，不迁移整套 GUI 或该分支单板门禁。
- 实现：新 GATEWAY owner 通过活动角色/UID/slot/代际和 CRC 授权；网关选择单 IN、单 OUT，COUNTER 选择单 IN。PIO0 两段程序经 persona manager 热加载，状态机禁止且不排触发字；STOP 清私有 FIFO/grant 并安全低，UNLOAD 恢复输出配置、释放程序和 SM。没有 FIRE/ARM 入口，READY 只读电平不构成完成。
- 动态资源：lease 保存 descriptor 副本，只改实际 GPIO 路由，其他资源预算保持声明和仲裁；避免对临时栈 descriptor 的引用。网关加载态实读 PINCTRL 输入/输出选择，COUNTER 实读 EXECCTRL JMP 输入，查询真实 SM enable/FIFO/grant。输出恢复包含 SDK 实际改写的 CTRL/PAD、SIO latch/direction，以及旧 PIO0 latch/OE；在仍持有 SM 时隔离 PAD 恢复，只动所选引脚。
- 软件与复核：gateway owner/实际 libscpi 95 项、扩展 COUNTER owner/解析 87 项、相邻 reference/workspace/RUN/部署 334 项通过；backend counter/gateway/reference/workspace 111 项及动态租约隔离用例通过，追加旧 PIO0 mux 恢复后的 gateway 回归 43 项通过（集合有重叠，不相加）。Release 与 flash link 检查通过。独立 reviewer 核对 IO 生命周期、descriptor 所有权和部署占用保护，发现的 GPIO 完整恢复与 FIFO 观测缺口已补齐。
- 当前源码 P3：build=`20260923081422`，`p3-gateway-load-r2` 在 MARK 准备时遇到 NO1 TOPOLOGY 超时/`-200`，未进入 MARK 采样；同包 `p3-gateway-load-r2-resume` 完成 quick `PASS_WITH_WARNINGS`（INFO 31 / WARN 28 / ERROR 0 / FATAL 0）。原失败保留；更早 `r1` 的拓扑复用申请被当前工具限定的 profile 拒绝，未操作硬件，随后按完整范围运行，没有缩减必验阶段。
- 网关硬件矩阵：`out/gateway-load-20260923/matrix-r1/migration-summary.json` 全部通过。NO3→NO2→NO4→NO1 各两次 LOAD/STOP/UNLOAD，再在 NO3 遍历全部 IN/OUT 组合；原始记录共 1377 条 SCPI，所有查询可回溯实际字段。加载/STOP 样本确认实际 PIO 输入/输出映射、程序驻留与 SM 申请、状态机禁止、私有 grant 为零、四 FIFO 为空、所选输出低。错角色 LOAD、已加载期间 DEP ACTIVATE/CLEAR 的拒绝与后续资源/配置清理均通过。当前工作树指纹与凭证及各轮一致。
- COUNTER 任意输入：`out/gateway-load-20260923/counter-inputs-r1/counter-evidence.json` 通过，在 NO1 分别选择 IN1–IN4，核验 PIO EXECCTRL 实际输入引脚及 LOAD/STOP/UNLOAD。此项没有 START，不将无信号端口声明为频率或事件采集通过。
- 最新信号在 NO1 IN1：`out/gateway-load-20260923/no1-frequency-r1/counter-evidence.json` 在新 build 复测通过。三窗计数差为 1662/1531/1524，TIMER1 观察频率为 1000.014368/999.445413/999.985197 Hz，含采样区间与 ±1 count 的频率全界均落入预定 990–1010 Hz；停止冻结、累计恢复、清零重启与四板清理通过。NO2 IN1 是否恢复回环未确认，网关矩阵未要求固定 READY 电平。
- 证据边界：本轮只有加载与资源生命周期，没有 FIRE/ARM/START 网关执行，不能证明脉宽、READY 握手、边沿 latch 或预约 q。reference 在这些硬件轮次未启用，只有配置保持证据，不宣称硬件并行共存。GPIO CTRL/PAD/SIO/PIO 状态恢复经真实 C host 与 SDK 语义复核；HIL 校验资源和程序释放，但未逐寄存器重读完整 GPIO 恢复状态。
- 源码交付：用户要求先按指纹提交推送。代码与验收凭证已提交为 `87235913`，提交钩子核验当前 staged 全源码指纹与上述 P3 一致；文档按仓库约定独立提交。SWITCH 尚未开始实现，不包含在本次已验收的能力声明内。

### T2-PROGRESS-20260923-003：NO1 IN1 脉冲计数槽位与 PIO0 热加载

- TODO task ID：T2-SLOT-COUNTER-001。用户明确先完成 NO1 脉冲计数槽位，再逐步实现 NO3 VNA 网关与链路控制；本地以捕获的时间片计数执行，各板加载的槽位决定 PIO 程序，SMA 统一 PIO0 热加载。本片先实现外部转台脉冲累计，不把位置脉冲数解释成预约格号。
- 参考：只读本地 `origin/feature/node-sequence-reservation-trigger` 的 `0c913e83f241fa8b40c059792141ae6051a85139`，参考 `refmem_slot_claim` / `refmem_sequence_roles` 的身份、代际与资源职责，以及 `sync_io_sequence` 累积计数、非阻塞快照思路；不复制该分支的本地 board_id 假设、整套治理或单板验收凭证。
- 实现：`sync_io_pulse_counter` 通过 PIO0 persona 热加载，只读取公开 IN1，PIO X 累计上升沿，FIFO 满仅丢快照不丢累计；不驱动输出，不使用逐脉冲 CPU IRQ、DMA 或共享 arena。资源数量、SM 和指令数以头文件、PIO 与 persona 描述符为事实源。可与 reference persona 共存；当前维护入口仍要求 TDMA STOP，不宣称与持续 RUN 输出共存。
- 生命周期：`trigger_counter_service` 用活动 deployment 的 UID、COUNTER 角色、generation/CRC、slot/schedule 和输入路由授权，LOAD 与 START 都重新核验；硬件操作进入 `tdma_service_run_stopped_maintenance`，不塞进仅供 SRAM 发布的 metadata 回调。已加载租约阻止 DEP ACTIVATE/CLEAR，先停止、卸载再换角色。`TRIGger:REServation:COUNter` 提供 LOAD、START、STOP、CLEAR、UNLOAD 与查询；新接口不复用旧 REAL:PCNT 的 TriggerVector 统计。
- 计数与时间：硬件 count32 按模数累计，STOP 冻结、START 保留累计、停止态 CLEAR 更新清零代际，UNLOAD 保留最后事实供读回。运行快照不暂停 PIO；排除 FIFO 与待 PUSH 的旧观察后，给出 TIMER1 采样区间。该区间包围计数观察，不是 IN1 边沿 latch；计数代际或清零代际变化时不能跨界差分。
- 软件验证：actual owner 与实际 libscpi 83 项、部署/owner 联动 112 项、counter/reference/workspace 65 项、相邻 RUN gate/output/rate schedule 215 项通过（部分集合重叠，不相加）。Release 编译与三份 flash link 检查通过；初次 rate schedule 提取 fixture 因独立 counter token 未使用触发编译警告，已隔离该不属于 fixture 的声明，原输出行为断言未放宽。原始日志在 `out/no1-counter-*.log` 与对应 `out/pytest/` 目录。
- 当前源码 P3：build=`20260923072308`，首轮 `p3-no1-counter-r1` 在 NO3 校准档位准备中断，未开始线序采集。确认 `run` 固定 quick 配置、忽略传入的配置路径；`resume` 才使用指定配置。`p3-no1-counter-r1-resume` 同包续跑完成 quick `PASS_WITH_WARNINGS`（INFO 33 / WARN 18 / ERROR 0 / FATAL 0），保留严格质量告警，不使用旧 build 的凭证。本段数字是该次验收快照，非产品事实源。
- NO1 实测：`out/no1-counter-20260923/run-r2/counter-evidence.json` 完成并通过，清理四板均通过。三段累计增量分别为 1638/1524/1520，按 TIMER1 实际观察区间得到 999.814618/999.998583/1000.538437 Hz；包含计数相位量化后的频率区间均在事先声明的 990–1010 Hz 内（本轮 1 kHz 台架快照，非校准精度契约）。STOP 后间隔读回不增长、START 保累计、运行中 CLEAR 拒绝且代际不变、停止后 CLEAR 清零并增加清零代际、重启后继续增长均通过。
- 身份和恢复：NO2/NO3/NO4 非 COUNTER 的 LOAD 返回 WRONG_ROLE 且 backend 未取得租约；COUNTER 已加载时 DEP ACTIVATE/CLEAR 返回 BUSY，原绑定保留；最终 STOP/UNLOAD、四板 DEP CLEAR、STOP 与 UID/build 读回通过，reference 配置保持。只读计数采样不作为输入边沿 latch；本片没有预约 q 执行、输出脉冲或设备 READY。
- 跨板动态加载：用户要求先让 NO2 搭载计数槽位，确认其他板卡能否加载不同槽位。`migration-summary.json` 与 `migration-no2-r1` / `migration-no3-r1` / `migration-no4-r1` / `migration-no1-r1` 四轮全部通过：仅交换 COUNTER 和对应业务角色的 UID/TDMA slot，保留各角色 IO 语义；逐目标两次 PIO0 LOAD/START/STOP/CLEAR/UNLOAD，lease generation 更新，非 COUNTER 板 LOAD 拒绝，每轮配置清理成功。同一固件无需编译绑定 NO1，证明当前已实现的 COUNTER 可在四块板动态加载；不据此宣布尚未实现的任意槽位均可运行。
- NO2 实际信号：用户将 1 kHz 源移至 NO2 IN1 后，`no2-frequency-r1/counter-evidence.json` 完整通过。活动身份为 NO2 UID、TDMA slot=1、COUNTER role=0；三段增量 1518/1527/1530，TIMER1 观察频率为 1000.002901/1000.140216/1000.245302 Hz，含 ±1 count 与采样区间的全界均在预定 990–1010 Hz 内。热加载、冻结、清零、卸载与四板清理通过。其余板在轮换时仅验证加载生命周期，输入零计数不作为频率或 READY 证明；信号接线变更不由主机推断为其他板仍有外部源。
- 失败保留：首个 `run-r1` 的 HIL 脚本错误地从完整 TDMA 统计查询读取只存在于独立 ring 查询的 applied config 字段，产生 KeyError，尚未 LOAD。旧脚本 `no1_counter_hil_r1.py` 与原始响应保留；改用 `SYST:TDMA:RING:STAT?` 的 `RUNTIME_FIELDS` 后重跑，未修改固件或放宽硬件判据。
- 后续顺序：COUNTER 当前切片关闭，NO3 VNA 网关与链路控制继续按用户顺序推进。参考分支的 gateway/READY IRQ 授权握手可借鉴，但原 fire 只提交脉宽、没有目标 q；LINK 是本地开关编码输出而非 TDMA 物理链路控制。当前分支仍须补预约格驱动和同预约反馈/实际时间戳，不能把源分支的计数或软件到达时刻当成完成证据。

### T2-PROGRESS-20260923-002：真实跨板诊断动作、回执与角色路由

- TODO task ID：T2-DEPLOY-003。用户要求跨板动作验证。本片由 NO1 向目标角色发送诊断 RAM `SET_VALUE`，目标本地执行并由真实普通 RefMem mailbox 返回完整身份回执；主机只做 STOP 下部署/排队及运行后的取证，不代转请求或生成回执。物理 GPIO、设备 READY、SYNC 时间戳对应关系、预约 deadline、fence 和实际 T2 latch 不由本片证明。以下运行参数与计数为台架快照，非产品事实源。
- 协作复核：独立 reviewer 发现并闭合动作 RX 截断 VDC/feedback 消费、广播 target mask 被收窄及 STOP 轮询漏采三个问题；STOP pending 在 owner 忙时保留，旧 session 不复活。实际 owner 与 protocol C 编译测试验证单源三目标、广播非目标不执行、错误身份/CRC、丢片重发和 STOP。
- 首轮当前源码 quick P3：`out/HardwareAcceptance/20260923/p3-cross-board-action-r1/`，build=`20260923055715`，`PASS_WITH_WARNINGS`，INFO 32 / WARN 21 / ERROR 0 / FATAL 0；原严格 TDMA 质量失败保持在阶段原件内。前序 `p3-action-loop-resume-r1` 只属于此前 build，不能验证本次新增诊断实现。
- 首轮专项失败保留：`out/t2-cross-board-action-20260923/original-run/capture/action-evidence.json`。NO1 queued=3/completed=1/mask=2，NO2 applied=1/value=123，NO3/NO4 applied=0；全部 STOP/CLEAR/外层恢复成功。旧状态只含混合 cancelled，不能判定 STOP、超时或运行失效；旧 sent 也只是准备次数，不能当成功发布数。旧脚本 `joint_action_r1.py` 的 SHA256 与原件一致，不覆盖首轮失败。
- 确定性修复：准备分片不再推进游标，只有 `tdma_service_publish_flight_tx` 成功后 `action_mailbox_published` 才推进并计 sent；发布失败保持原片。状态 schema 增加首个有效撤销原因、会话耗时、最后完成耗时及接收结果，后续 STOP/CLEAR 不覆盖已有失败原因。诊断期限引用 `DISTRIBUTED_ACTION_PROBE_TIMEOUT_MS`；下一轮运行窗口短于期限，明确要求 STOP 原因撤销，不能用扩大期限解释或抹去首轮失败。
- 第二轮当前源码 P3：build=`20260923061212`，`p3-cross-board-action-r2` 在 TDMA 启动未取得完整 board record，四板 STOP 成功，DPLL 因交接节点集合不完整拒绝；同包、不重刷的 `p3-cross-board-action-r2-resume` 完成 quick `PASS_WITH_WARNINGS`（INFO 33 / WARN 15 / ERROR 0 / FATAL 0）。原失败与恢复目录分别保留。
- 第二轮专项：`original-run-r2` 和 `swapped-run-r2` 都只完成首个 request=10，source completed=1/mask=2，运行至约 63 s 由 STOP 原因撤销；首个回执耗时分别 951/889 ms，不能解释成预约延迟或物理输出时间。原映射 NO2 applied=1/value=123，交换后只有承载 SWITCH_A 的 NO4 applied=1/value=123，证明首目标随业务角色变化；余下两目标未执行，整体仍 FAIL，不能据首目标路由成功关闭三目标聚合。各轮 CLEAR 与外层恢复均完成。
- 后继共载修复：原 action 只接受 `TDMA_PROCESS_IMAGE_MESSAGE_CLASS`，遗漏既有 VDC feedback/boundary 类同样保留的独立 RefMem 与 CONTROL 区域。三个 action 入口改为 `tdma_process_image_transport_class_valid`，使用原有区域与 CRC，不增加席位、不改变 VDC 分片内容；完整 body 的 priority SYNC 仍排除。相邻 service 共用同一 ring snapshot，避免新增一次完整读取。该限制可由代码确认；它是否解释全部台架失败，以第三轮结果为准。
- 软件证据：实际 action owner/kernel 42 项通过（`out/pytest/distributed-action-carriers-r7-20260923`），覆盖成功发布后推进、连续失败不跳片、STOP 迟到确认、全部 feedback/boundary/混合载体和 priority/旧命令拒绝。相邻 VDC 运输、reference spacing、owner、preacquire、ingress 等 235 项通过（`out/t2-cross-board-action-20260923/ingress-tests-r2.log`）。初次相邻测试因提取 harness 未提供新诊断 OFF 接口桩而编译失败，保留日志；补桩未放宽原 VDC 断言。第三轮源码已冻结，P3 与动作专项正在执行。
- 用户最新接线与语义：NO1 IN1 外部 1 kHz 模拟转台，NO2/NO3/NO4 各 OUT4 回接 IN1 模拟 READY；共同时间轴、不同目标 q、各板本地提前装载与执行。`passive-input-readback-r1/passive-inputs.json` 校验四板 UID/build/STOP，NO1 IN1 观察到高/低，其他回环板仅静态读回；该查询不证明频率、READY 或实际事件时间戳。独立代码复核确认当前 OUT4 仅相对延迟接口，绝对 deadline RUN 固定 OUT1，IN1 capture 与 RUN 共用 arena；本片仍不能验收指定 q 的 OUT4→IN1 顺序闭环。
- 最终该版证据：build=`20260923063812`，`p3-cross-board-action-r3` 在 NO1 RUN prepare 超时，`r3-resume` 在 NO2 TOPOLOGY 超时/`-200` 导致 MARK 未开始；失败均保留。`r3-resume2` 使用显式主机观察/交接预算后完成 quick `PASS_WITH_WARNINGS`（INFO 32 / WARN 21 / ERROR 0 / FATAL 0），原必验范围未缩减。`original-run-r3` 与重复轮只 completed=2/mask=6，末回执为 310/313 ms；`swapped-run-r3` completed=3/mask=14、三目标各 applied=1、末回执为 398 ms，STOP/CLEAR 与外层恢复通过（以上是该版台架快照，非实时期限保证）。交换轮成功不能覆盖原映射不稳定。
- 根因复核：NO1 priority generation 启用后，`tdma_pio_spi_ring_origin_publish_priority` 的 READY 和 EMPTY 均声明 handled，取消普通 offer 并阻止普通 FIFO 消费；后续 Core0 在 `tx_ready_count` 背压处停止新动作发布。完整 body 的 priority SYNC 不能再塞入 action。共载修复消除了不必要的 class 限制，但没有解决 source priority/ordinary 仲裁；保留该缺口，按用户顺序先转入本地 COUNTER 槽位，不把启动窗口内偶然全部完成当成稳定运输资格。
- PIO0 纠偏：当前常驻组合的指令总数仅限制同时驻留，不能否定热加载能力。应按实际 persona/lease 的退休与换装流程核算；现有 RUN cancel 会截断而非自然排空，后续预约状态机必须提前准备并保留共同格身份。

### T2-PROGRESS-20260923-001：同步 RUN 时间片开关与四通道逐格测量

- TODO task ID：T2-CLOCK-001、T2-ACCEPT-001。用户要求同步期间在同步脉冲后输出可选时间片，增加 SCPI 开关并用外部示波器同步采样。本轮只接入诊断 RUN 输出；预约 wire、READY/fence、实际业务 T2 latch 不由本片关闭。以下数值与命令均为本轮实现/台架快照，非通用产品精度契约。
- 实现：`vdc_run_output_slices_t` 为 STOP 下 RAM 配置，默认关闭，PREPARE 锁存；`SYST:VDC:OUTP:SUBD:AFTER:ENAB`、`CONF`、`STAT?`、`RUN?` 提供开关、几何、请求及运行实例读回。开启时复用既有 RUN lease、SM/DMA/arena，PIO 有限循环输出时间片，每同步周期只提交一个 FIFO 字；关闭后再次 RUN 恢复原同步输出。独立维护 `SUBD:WINdow` 保持原语义。
- 参数快照：同步周期 1 ms、细分 500 ns、窄脉冲 100 ns、同步标记 200 ns；q1…1998 为窄脉冲、q1999 留空、q2000 为下一同步标记。marker 沿绝对 DCO deadline，周期内使用固定整数 tick；不宣称任意 rate 下都有固定精度。PIO 20 词、现有固定 persona 空间按实际资源校验，不增加 Core1 席位。实际尾部为末细分下降沿，参与 refill/commit 预算。
- 纠偏与失败保留：此前维护窗口开关不满足 live RUN 要求，已撤去该误接线。查询值 1 超时由主机通用串口工具丢弃未登记 scalar-one 回复导致，`out/HardwareAcceptance/20260923/scpi-query-raw/readback.json` 保存真实 `1\r\n`；最终配置查询返回完整元组。曾误改既有 REFERENCE ACK 导致 `p3-092018` 失败，已恢复且后继 P3 通过。
- ON 首轮 `run-slices-on-r1` 在 NO4 RUN prepare 超时，r2/r3 在 START 确认超时；均保留原命令与恢复。r4 将诊断 action ACK 观察预算显式设为 1 s 后进入 TRIAL，但四板最终 FIFO 耗尽，NO1 首批 16 marker 后 6 次编码拒绝。删除新增的 12 tick 周期质量拒绝，保留真正重叠/低段不足/溢出检查；未放宽外部测量判据。原 P3/r1–r4 文件不覆盖。
- 软件证据：backend/真实 PIO 指令解释器 162 项通过，VDC client/真实 libscpi 197 项通过，时间线/配置相邻回归 123 项通过；更早集成 284 项覆盖 grid owner，不将重叠数量相加。日志 `out/run-slices-final-client.log`、`out/run-slices-timeline-r1.log`；后端由协作 worker 验证，独立 reviewer 复核实际 tail、完整几何校验及运行锁存读回后放行硬件。observer.sample_period_ns 恢复为采样分辨率语义，不再误要求等于同步周期。
- 当前源码 P3：`out/HardwareAcceptance/20260923/p3-105208/` 完整执行，build=`20260923025220`，quick `PASS_WITH_WARNINGS`，INFO 31 / WARN 23 / ERROR 0 / FATAL 0；原始严格质量失败仍由 diagnostic/alarms 保留，不能据此宣称严格全绿。
- ON 外部专项：`out/HardwareAcceptance/20260923/run-slices-on-r5/` 成功；60 s 内部 guard/输出退休、六个同步四通道 RAW 窗口和恢复全部通过，无 errors/cleanup_errors。每窗用宽 marker 配对相同周期，再逐一比较相同细分序号，不以 nearest 500 ns 脉冲配对；每通道每完整周期含 2000 个上升沿（首marker、1998窄脉冲、末marker），尾格留空。全部对应边沿相对 CH1 的最大绝对偏差 24.085151 ns；CH2/CH3/CH4 相对CH1最大绝对差分别 21.833878/24.085151/19.857155 ns；独立复算任意两通道对应边沿最大差 36.000000 ns，均在本次 ±50 ns 判据内。四通道 TCAL 读回均为零；采样、阈值插值、线缆/未独立校准时基的界仍保留。
- 原件和图：`channel-slice-summary.json`、`channel-slice-skew.png`、各 `window-*/scope.json` 与四份 RAW block；`AFTER:RUN?` 绑定 request 与锁存参数。示波器结果仅证明所采窗口内对应时间片同步，不证明未观测区间连续资格或绝对业务 reservation ID。四槽预算模型仍是下一业务加载输入。
- OFF 恢复专项：`out/HardwareAcceptance/20260923/run-slices-off-r1/` 在 ON 后恢复关闭并重新 RUN，十二个外部同步窗口、内部 guard 和 STOP/恢复全部通过，errors/cleanup_errors 为空。每窗每通道仅两个周期同步上升沿，证明关闭后不再插入时间片；不使用只读开关值替代物理 OFF 证据。
- 文档门禁通过，检查器测试 38 项通过；pre-commit/check-staged 因暂存源码与本次工作树不同而拒绝，日志在 `out/run-slices-hook-final.log` / `out/run-slices-staged-final.log`。本轮未提交，保留用户原暂存区；本次硬件结论绑定工作树构建及包，不能表述成 staged 提交已放行。

### T2-PROGRESS-20260922-010：P0T 线序重测通过，magic_info 不参与判定

- 用户要求重新运行 P0T；当前 build=`20260922132053`，四板有界拓扑探测原件为 `out/p0t-anchor-r2/summary.json`。
- P0T `passed=true`，唯一环序为 `0010071E65B5CB38 → FB276192BEF9CCE1 → 2BD5090FE009FA2A → A1E549202D18ED6A → 0010071E65B5CB38`。四个真实相邻方向均检测到原始 words，十二个非相邻方向均无活动，板卡 NO assignment/cleanup 完成。
- 各真实相邻方向的 `magic_info` 分别为 76、95、71、73；该字段仅保留接收扫描诊断，不参与 `detected`、线序推导或 P0T 通过判定。P0T 目标已完成：唯一线序成立。
- 结论：P0T 不再是当前 TDMA 启动的阻塞项；下一步可在本次线序/NO assignment 已恢复的前提下，单独执行 `RING:ARM → RING:START → session/ring readback`。本条不宣称 TDMA 或 VDC grid 已运行。

### T2-PROGRESS-20260922-009：VDC 时间网格同步验证前置阻塞

- TODO task ID：T2-CLOCK-001、T2-EXEC-001。对当前四板固件执行 observer → grid 配置 → anchor 前置验证；不 ARM 业务输出，不把本地细分窗口当共同 anchor。
- 四板 `vdc_observer_validate` 通过，build=`20260922132053`，四板 schedule CRC 均为 `124464440`，dictionary CRC 均为 `1099444114`，原件在 `out/vdc-grid-sync-20260922/observer-validate/summary.json`。这只证明 observer 配置/读回路径，不证明共同事件。
- 四板初始均 STOP，`SYST:VDC:FEED:SESS?` 均为 0，`SYST:SYNC:VDC:DCO?` 均为无效 model；发送 `TRIG:RES:GRID:CONF 20260922,1,0,1000000,1000` 四板均返回 `1,35`（MODEL），没有写入 grid。有限 VDC RUN 尝试超时/执行错误；TDMA ARM 读回 1 但 session 仍为 0，TDMA START 四板均执行错误。
- 清理：四板 `RING:STOP` 成功，`GRID?` 回到未配置，`OBServer?` 全零，`SYST:ERR?` 为 `0,"No error"`。完整原件见 `out/vdc-grid-sync-20260922/summary.json`。
- 结论：`grid_sync_qualified=false`、`same_anchor_occurrence=false`。尚未执行 `GRID:ANCHor` 或 `ANCHor:PLAN?`，不输出四板 deadline/首沿同步结论。恢复条件是 TDMA/VDC session 与有效 committed DCO model 可用；恢复后先重跑四板 `OBServer:TDMA`，仅当四板 `expected_window_start` 和 schedule CRC 完全一致时才绑定 anchor。
- 投入回收：本轮验证价值 4/5，已确定阻塞在时钟/TDMA 前置而非网格算术或 SCPI parser；停止继续发送 RUN/START 重试，保留原始拒绝与清理读回。

### T2-PROGRESS-20260922-008：共同 anchor 接线修复与当前指纹实测阻塞

- TODO task ID：T2-CLOCK-001、T2-EXEC-001。独立复核发现新增 anchor API 尚未接入真实部署规划，且 anchor 可跨 generation/样本身份复用；本片只修复这条证据链，不扩展输出执行器。
- 实现：`trigger_deployment_plan()` 现在调用 `trigger_grid_preview_plan_anchored()`；anchor 绑定要求当前 grid 的 `anchor_vdc_ns`、event、observer sample、schedule CRC、DPLL-eligible 硬件时间戳和不超过 100 ns 分辨率全部匹配。anchor 保存 generation；新 generation/CLEAR 清除旧 anchor。规划前后复核 sample、event、A、schedule、source、resolution、flags、model token/session，任何变化返回 anchor reject。
- 接口：新增 `TRIG:RES:GRID:ANCHor event_id,anchor_vdc_ns`、`GRID:ANCHor?`、`GRID:ANCHor:PLAN?`；仍是 STOP-only preview/plan，不授予 output、READY、ARM 或 RUN 权限。实现落点为 `trigger_grid_preview.h/.c` 与 `scpi_trigger_grid_commands.inc`。
- 软件证据：当前源码 owner/parser/deployment 回归 `154 passed`，数学网格回归 `6 passed`；覆盖无 anchor、错误 event、诊断时间戳、A 不一致、规划期间 sample 变化和 generation 清除。Release `pico2-release` 编译及链接成功，P3 构建目录 `out/build/p3-acceptance-20260922-212046/` 产生四板固件。
- 当前指纹 P3 实测：首轮 `out/HardwareAcceptance/20260922/p3-211255/` 在 HDO `:TIM:SCAL 0.0002` 查询超时退出 2，四板串口/校准/恢复阶段完成；第二轮 `out/HardwareAcceptance/20260922/p3-212046/` 在 P0T 拓扑阶段失败，NO1→NO2、NO2→NO3 有原始 words，`magic_info` 仅作为接收扫描诊断信息，不参与线序判定；当前仍因完整环序未形成、NO2 ARM 状态读回无 payload 而不能签发 P3。窄范围复测 `out/p0t-anchor-r1/` 保持同类结果，不能将失败归因于 anchor 运行路径。
- 投入回收：代码修复价值 5/5，软件验证已达到停止点；硬件重复完整 P3 的边际收益降为低，保留两轮失败原件，暂停重复验收。恢复条件是 P0T 帧识别/ARM 读回恢复，或示波器采集链恢复；恢复后只重跑 P3 必需阶段，不重做已通过的 host owner 回归。

### T2-PROGRESS-20260922-007：DPLL 60 s 联合示波器复核与 grid anchor 边界

- TODO task ID：T2-CLOCK-001、T2-EXEC-001。按用户要求重新执行当前 build 的四板 DPLL 60 s 联合示波器复核；证据目录 `out/dpll-sync-rerun-20260922/`，12 个 5–60 s 检查窗口均有 CH1–CH4 RAW，scope checkpoint 无失败，四板状态和资源恢复无异常。
- 结果：外部 sampled/steady 均通过，最大外部相对相差绝对值 `31.984400 ns`（范围 `[-31.984400,+13.959133] ns`），按 ±100 ns WARN、±1 us ERROR 分级，本轮无 WARN/ERROR。报告 `physical_continuous_lock_qualified=false`、`exact_event_correlation_qualified=false`，所以只证明当前 DPLL 外部采样相位质量，不证明同步脉冲已成为 T2 grid anchor 或具体事件 latch。
- 误用边界：先前 500 ns 四板有限窗口联合采集 `out/out1-window-500ns-sync-20260922/run1/` 不是同步结论；四板 host dispatch spread 约 1.698 ms，单次窗口 `[-200,+1799.998] us`，NO3/NO4 波形被截断、NO2 未捕获有效边沿。该失败原件保留，不能解释为 DPLL 失同步。
- 代码复核结论：当前 `SUBDIVISION:WINDOW` 起始点由本地 PIO enable/维护 owner 产生，`trigger_time_grid_t.anchor_vdc_ns` 还是配置标签，没有共同 event id、raw latch 或跨板 fence。要继续 T2-EXEC-001，必须增加“同一 VDC 同步事件/A_vdc + event identity → 各板 TIMER1 deadline → 实际 latch”的入口；WINDOW 继续仅作为局部间隔工具。
- 投入回收：本次硬件复核约 95.063 s，价值 4/5，明确排除了“先继续调局部窗口即可证明 anchor”的错误路径；保留 DPLL 60 s 证据，停止额外波形矩阵。下一项是同步事件身份和本地 deadline 执行切片，不重复跑 DPLL，除非源码、模型或 scope 连接变化。

### T2-PROGRESS-20260922-006：四槽部署抽象、SWITCH 复用与动态映射事务

- TODO task ID：T2-DEPLOY-001、T2-DEPLOY-002。用户明确当前接线：NO1 COUNTER=IN1；NO2 SP8T=OUT1–OUT3；NO3 VNA=OUT4 TRIG、IN1 READY；NO4 SP2T/HV=OUT1。NO2/NO4 归一为 SWITCH 类型，设备名只描述 profile；不同板卡的 OUT1 是各自本地命名空间。
- 角色模型：四个槽身份为 COUNTER/SWITCH_A/GATEWAY/SWITCH_B，运行类型为 COUNTER/SWITCH/GATEWAY。两个 SWITCH 使用同一校验/路由，输出宽度由非零四位 mask 决定；当前分别为 0x7/0x1。GATEWAY 要求一入一出和 REQUIRED_EDGE，当前 IN1/OUT4；其他 `NONE_UNOBSERVED` 不是反馈完成。
- 实现：`trigger_deployment.h` 提供完整表验证、规范 100-byte LE CRC、本地 UID/slot resolve 和类型映射；`trigger_deployment_service.c` 在 STOP+Trigger IDLE 下 STAGE/ACTIVATE/CLEAR，active/staged 分离、旧代不可复活。PLAN 先验证本地槽，再消费共享 grid exact-q 计划，并在返回前复核 UID、slot、ring config 与 schedule；只读规划，不申请资源或输出。
- SCPI：`TRIG:RES:DEP:STAG/ACT/CLE/DEP?/PLAN?` 使用严格整数解析；STAGE 一次提交四行，PLAN 成功返回本地路由、角色类型和共享目标/本地 deadline。四板依次 ACTIVATE 是显式安装，不宣称原子分布式提交。
- 软件证据：部署纯核、owner/libscpi、grid owner 联合 151 项通过；相邻 VDC suites 325 项通过。独立 `/root/t2_triplet_review` 结论 PASS，限定部署映射与无输出规划。首次 P3 `p3-t2-deployment-r1` 在编译前因 SDK include 规则失败；改为 `pico.h` 后部署定向回归 73 项通过。修正版 r2 完成 build/OTA/校准，但 NO3 的 TDMA provisional 命令单次超时导致 STOP handoff 缺 board records，未进入 DPLL；失败与清理原件保留。r3 使用同包和已验证 OTA resume，不重复构建/刷写，正在执行。
- 投入边界：本片价值 4/5，完成原映射→交换 SWITCH_A/B 的 UID/slot→恢复原映射，核对错误 CRC、旧代、错目标和 CLEAR 后恢复即停止。真实 GPIO 驱动、READY 极性、跨板预约运输和设备完成不在本片冒充完成。
- 当前源码 P3：修正版 r3 复用 r2 已验证包/OTA，跳过重复构建刷写；quick 分级 PASS_WITH_WARNINGS（33 INFO / 18 WARN / 0 ERROR / 0 FATAL），DPLL 60 s 外部最大相位差 70.702232 ns，验收总耗时 188.700 s 为 PASS。源码 SHA `b7ca688c934213c94ec17f4e4bccddefef1716e0752cbbdb5ef4f86db64ba34e`，包 SHA `4845a81223f265c4c76728cd77acac2b789f86287425724d5461db8ce6fc9c55`，1450 sources，`check-staged` 通过。r2 NO3 单次超时及缺 record 失败原件未覆盖。
- 四板部署专项：`out/t2-deployment-20260922/hil-r1/summary.json` 在 28.079 s 内完成三代。generation 1 原映射为 NO2 mask 0x7 / NO4 mask 0x1；generation 2 交换两者 UID/TDMA slot 后，NO2 获得一位 SWITCH 配置、NO4 获得三位 SWITCH 配置，角色类型均保持 SWITCH；generation 3 恢复原映射。每代四板均完成同表 STAGE 读回、顺序 ACTIVATE 和本地 exact-q 路由计划，非目标角色返回 NOT_LOCAL；坏 CRC STAGE 保留拒绝回执，其后正常 STAGE 的 active 仍与此前相同；坏 CRC ACT 有直接不变读回，旧 generation 不再授权。一次 ACT 暂态返回 NOT_IDLE 后有界重试成功，原拒绝保留，不称全程零拒绝。
- 恢复与边界：专项共 289 条有界 STOP/身份/部署/grid 命令，不含 GPIO/ARM/FIRE/RUN；最终四板 deployment/grid configured=0，session/load mask 恢复，稳定 STOP/config ACK，零 cleanup error。last_generation=3 按防旧代设计保留，不宣称逐位回滚。当前部署 active 只证明本地路由安装，不是跨板原子提交、远端在线、IO claim、READY 或实际动作完成。
- 结论：T2-DEPLOY-001/002 关闭。证据目录为 `out/HardwareAcceptance/20260922/p3-t2-deployment-r*/` 与 `out/t2-deployment-20260922/`。下一 gate 是 T2-DEPLOY-003 的真实跨板运输与目标板动作；动态切换的在飞预约/输出退休仍由 T2-DEPLOY-004 承接。

### T2-PROGRESS-20260922-005：共享 VDC 格配置与指定 q 的本地 deadline 规划

- TODO task ID：T2-CLOCK-GRID-001、T2-CLOCK-001。用户明确 T2 应在共享 VDC 时间片内规划动作并要求继续。本片将共同 A/P/N/epoch/generation、绝对 q 与本板 committed DCO/TIMER1 反解接线；只作 Core0 STOP 暂存和无输出规划，不写 TriggerAO 活动态，不发布 READY/ARM，不扩充 Core1/wire 资源。以下数值为本片快照，非产品能力事实源。
- 分支借鉴：沿先前已核来源的 `trigger_reservation_clock.c` 采用“固定目标→真实逆映射→lead→同板身份复核”；未照搬依赖 formal LOCKED 大向量的捕获门禁。发现当前周期 RUN planner 会跳过迟到 ordinal，因此指定 q 路径直接调用 Domain inverse，明确返回 LATE，不沿用自动选格逻辑。
- 新纯数学核 `trigger_time_grid.h`：绝对序号拆周期/余数受检计算，末格保留，正负 rate、非整除、模型起点夹持、量化、提前量/未来范围和相邻格可区分性。`trigger_grid_preview.c` 复用已有 STOP metadata 发布入口，绑定每板 session/ring/role/local epoch-run/slot/schedule；共同 epoch 为实验标签，不自动证明参考源资格。CLEAR 保留代际上界，旧代不能复活。
- 独立审核修正：补充实际 ring schedule CRC 与 committed DCO schedule CRC 一致性，防止把新环配置配旧模型；数学核补充早于模型有效起点的目标拒绝，避免 inverse 夹持冒充原目标。独立 `/root/t2_triplet_review` 对代码/边界复核 PASS。
- 软件验证：数学与真实 owner/libscpi 联合 74 项通过，独立有理数/二分逆映射覆盖 850+ 样本；相邻 VDC fixed/run suites 与 owner/parser 合计 393 项通过（有测试重叠，不相加为总数）。严格 u64 参数不经 double，配置/清理/失败返回 schema+result，配置查询成功 16 字段，规划成功 34 字段。命令为 `TRIG:RES:GRID:CONF`、`GRID?`、`GRID:CLE`、`GRID:PLAN?`；具体字段事实源为 `scpi_trigger_grid_commands.inc`。
- 投入：EXECUTE，价值 4/5；实现后预计再用 20–35 分钟闭合四板读回及验收。退出点为共同目标一致、各板本地 deadline 独立可复算、拒绝保留原目标、无输出与清理读回；停止扩展新观测或重复极限实验。
- 进行中：独立目录 Release/P3 `out/build/t2-grid-r1/`、`out/HardwareAcceptance/20260922/p3-t2-grid-r1/`，专项原件 `out/t2-grid-20260922/`。不再覆盖上一片构建包，不以同 build ID 替代新源码/包指纹。
- 首轮实测失败及修复：r1 P3 quick 分级通过，但 `hil-r1/summary.json` 在 NO1 GRID CONF 连续读回 MODEL(35)，未装载配置且 session/mask 已恢复。实际 STOP/unconfigure 会将 runtime local_slot/schedule 清零；首轮单测错误地保留了 RUN 身份，导致身份检查拒绝正常 STOP。已改为从 `tdma_runtime_owner_get_staged_ring_config()` 读取真实保留配置，前后确认 STOP/config ACK；继续对比模型与 staged 身份，不删除检查。真实 STOP 清零、staged 缺失/禁用/错代/配置变动/读取竞态及无重配置副作用的 owner/parser 测试 78 项通过；纠正源码在独立 `t2-grid-r2` 构建及 `p3-t2-grid-r2` 验收，保留 r1 全部原件。
- 修正版构建与 P3：独立 Release build `20260922052040` 通过，新增静态状态 68 字节，无 Core1 工作扩容；源码 SHA `0fca320ed0e5188a656164d9efda0e124eddb31586a009e8a7fa74b67a5de533`，包 SHA `b403c92d142f9acded09ff695f59c14e44f194864cbf0d4984b637dafde59ebe`。`p3-t2-grid-r2` quick 分级 PASS_WITH_WARNINGS（32 INFO / 21 WARN / 0 ERROR / 0 FATAL）；coded marker/TDMA 原始严格失败与总耗时 497.092 s 超过 450 s 的 budget ERROR 仍保留，不能称严格全绿。DPLL 60 s 联合观测外部最大相位差 31.840751 ns，验收凭证按修正版指纹签发。
- 四板专项：`out/t2-grid-20260922/hil-r2/summary.json` 成功，11.083536 s。共同配置 A=0、P=1000000 ns、N=1000、实验 epoch=20260922、generation=1；q=767917000、767917001、767918000 在四板分别形成一致 VDC 目标 767917000000、767917001000、767918000000 ns。每板独立的真实 committed 模型由 host 大整数 forward/二分 inverse 复算，TIMER1 deadline 与固件一致，投影量化超前 0–3 ns，无输出。远期目标用于避免串口编排消耗 lead，不证明该 horizon 内保持时钟精度或已预留预约。
- 每板七类负测通过：保留格返回 RESERVED；错 generation 返回 GENERATION；超 horizon 返回 HORIZON；过去目标返回 LATE；同代修改 anchor 被拒且配置未变；CLEAR 后返回 UNCONFIGURED；原代不能重新配置。共同标签仅证明配置一致，不代表跨板正式参考身份，个别板的 local epoch/run/token 和 raw deadline 无相等要求。
- 恢复/证据：四板网格 configured=0，保留消费过的 last_generation=1（防旧代复活，非逐字回滚）；session/load mask 恢复、稳定 STOP/config ACK 读回，零 cleanup error。`hil-r2/session-prepared.json` 不可变，`session.json` 另记恢复；固件包及 receipt 独立复制，原命令、重试、模型、配置/数据哈希可复核。r1 失败与恢复原件完整保留，未增加其他观测矩阵。
- 文档检查器测试 38 项通过，最终文档/P3 staged/差异门禁记录在 `out/t2-grid-20260922/final-gates/`。仅关闭无输出共享格切片；T2-CLOCK-001 仍为 IN PROGRESS，活动部署、共同参考认证、PREPARE/READY/fence、真实装载与每节点反馈继续由既定任务承接。

### T2-PROGRESS-20260922-004：两端同步标记、整窗细分和末格留空

- TODO task ID：T2-CLOCK-PROBE-002。用户要求铺满两次同步之间的间隔，末尾一次不触发，为下一同步留余量；随后明确选用 1 us 细分 / 1 ms 周期的现有容量路径。以下数字为本片配置及验证快照，非产品时序事实源。
- 定义：q0 为 200 ns 标记；q1…998 为 100 ns 窄脉冲；q999 留空；q1000 为下一标记。总共 1000 个脉冲，最后两次上升沿间隔 2 us，标称末次低电平余量 1.9 us。两个标记在同一有限预装字流内，不采用主机分发或逐脉冲软件输出；仍为 STOP 本地维护窗口，未接入 live RUN 或共同 VDC anchor。
- 实现：`sync_io_rate_schedule_generate_window()` 按商/余数计算累计格位置，尾双格间隔校验范围，末 marker 高宽进入总时长；复用已有 finite owner、SM/DMA/workspace。独立 mode 2 `SYST:VDC:OUTP:SUBD:WIND 1000000,1000,100,200,4` 及查询/STOP；原 FIXED 32 字段与 SUBDIV 37 字段接口保持。窗口查询仍为独立 mode 的 37 字段，窄脉冲数 N−2、总脉冲数 N，period*N 为标称同步周期。
- 软件：首轮负测把恰等于 UINT32_MAX 的可表示尾间隔误当越界，已将测试 rate 改为实际越界值；生产算法未为该测试改变。累计边沿/容量/尾marker/取消、owner/SCPI 三模式互斥和既有 RUN 合计 350 项通过；命令及原始失败说明见 `out/out1-window-20260922/software-evidence.json`。独立审核 `/root/t2_triplet_review` 为 PASS，物理精度仍待板上证据。
- 构建：Release 通过，静态 BSS 末址未变。本轮增量构建复用 `out/build/out1-subdivision-r1/`，导致上一版包被替换，而 build ID 未变化；旧报告/RAW 保留，旧包 SHA 在该构建路径不再可重验，此缺口显式记录。本轮依靠新源码指纹和新包 SHA 绑定验收，不以相同 build ID 冒充同一二进制；后续构建需独立目录或事先保全旧包。
- 投入评估：EXECUTE，价值 4/5，预计 20–40 分钟完成接口、回归及硬件；四板各一整窗证明数量、累计误差、尾部留空/低电平余量后停止扩展，恢复台架。100 ns 铺满需要超出现有字流容量，本轮按用户选择不扩容，不增加 Core1 席位。
- 当前源码 P3：`out/HardwareAcceptance/20260922/p3-out1-window-r1/` quick 分级 PASS_WITH_WARNINGS，30 INFO / 24 WARN / 0 ERROR / 0 FATAL；原始 coded marker、SCK 训练/重装行选择、TDMA 严格闭环未全绿，见 `diagnostic.json`，不得以分级结果覆盖原失败。DPLL 60 s 联合观测最大外部相位差 30.773659 ns。验收耗时 394.541 s，时间预算 WARN，未超过 ERROR 门限；未重复完整验收。
- 当前指纹：源码 `1b985797f3be57e14691aac5ccd469007e10856ca31fa9690a2631318ab5c97a`，包 SHA `c343afc36432ee52f82f2eed1a06e03de5e89b337713f14acf04f501e1706877`；build ID 仍为 `20260922023612`。新包和本轮凭证另存 `out/out1-window-20260922/r1/firmware.pkg`、`receipt.json`，与新指纹绑定；`check-staged` 通过。
- 整窗示波器实测：四板各一次，每板恰 1000 个完整升降沿、2 宽标记/998 窄脉冲，q999 无边沿；marker 间隔为 1000002.608633 / 1000003.297053 / 1000003.877936 / 1000002.352576 ns。末次上升沿间隔 1999.568775–2000.383872 ns，末低电平余量 1897.299007–1899.079329 ns。相对请求网格最大相邻误差 4.000000 ns、累计误差 4.526489 ns；相对模型编码最大相邻误差 1.955048 ns、累计误差 6.352576 ns。两口径均 INFO，未触发 ±100 ns WARN / ±1 us ERROR；NO4 的累计编码量化差保留，不与物理请求误差混写。
- 证据与恢复：`r1/verified-summary.json` 关联四份 manifest、16 个 RAW 块及输入哈希；`r1/window-and-guard.png` 展示首尾波形、q999 空格和整窗累计差。不可变 `r1/prepared.json` 绑定采集，`r1/session.json` 另记恢复，四板 session/load mask、稳定 STOP/config ACK、PIO/DMA 退休和示波器原设置均恢复且读回，无 cleanup error。专项含准备/采集/恢复共 58.203 s，达到整窗退出条件即停止扩展。
- 测量范围：RAW 采样步长 2 ns，差值使用阈值插值；未独立标定示波器时基，不将小数纳秒解释为绝对精度。两个标记来自同一个本地有限批次，未证明跨板共同相位、持续 DPLL 输出后的细分、业务反馈或 T2 实际 latch。
- 文档与下一 gate：文档检查器测试 38 项通过，最终门禁及独立证据复核见 `out/out1-window-20260922/`。本片整窗验证关闭，下一项回到多板部署；持续同步接入和 T2 预约保持独立任务。

### T2-PROGRESS-20260922-003：OUT1 有限细分间隔预验证

- TODO task ID：T2-CLOCK-PROBE-001。用户要求先输出同步标记后跟随可配置间隔的细分脉冲并测精度；本片采用 STOP 维护态有限批次，先验证局部间隔，不将本地任意相位标记冒充四板共同 anchor 或 live RUN 后继细分。
- 实现边界：复用 FIXED 维护 owner、共享 DMA 字流和 PIO persona；宽 marker 只补偿紧随其后的低段，不改变累计上升沿。SUBDivision 使用独立命令与状态，保留旧 FIXED 的参数下限及查询 schema；无新增 Core1 席位、PIO SM、DMA 或 SRAM 字流池。
- 投入评估（本次估计，非产品事实）：EXECUTE，价值 4/5，后续预计 30–60 分钟；两档主要间隔的原始波形、脉冲完整性与误差足够决定是否继续用此通路承载 T2 时间格。停止扩展无关实验矩阵，恢复 STOP 并保留全部失败与原件。
- 软件/审查快照（非产品事实源）：累计边沿独立有理数 oracle、backend 资源/取消、owner/SCPI 与既有 RUN 回归合计 287 项通过；独立 agent `/root/t2_triplet_review` 结论 PASS，修正启动后验证失败仍保留编码 marker ticks 的测试预期。Release build `20260922023612` 通过，map 相对前版仅增加 8 字节 BSS；未新增波形池或 Core1 席位。
- 当前源码 P3：`out/HardwareAcceptance/20260922/p3-out1-subdivision-r2/` 为 PASS_WITH_WARNINGS，33 INFO / 20 WARN / 0 ERROR / 0 FATAL；DPLL 60 s 联合示波器最大相位偏差 34.778496 ns，内部原件及零 RUN 查询证据由该报告保留。staged 指纹 `14e188ee694ee9ff35f46b99ffbab508bf806ae956ec663c3e3d890997f739b8`，1438 sources，凭证 `check-staged` 通过。前次 r1 因 `--reuse-topology` 仅支持 TDMA-only 而在构建/硬件前拒绝，原件保留，不缩减 DPLL 范围。
- P3 实际耗时快照：451.428 s，其中首次 Release 构建 102.289 s；总耗时超过配置的 450 s 预算约 1.428 s，`timing.json` 原始 budget ERROR 保留。上述零 ERROR 指 quick 分级验收告警结果，不能解读为原始严格检查全绿；本次不为消除时间告警再跑完整验收。
- 实测准备与失败：`session-r1.json` 记录脚本未识别 LOAD MASK 复合 ACK 的超时，已按读回恢复后仅修 out 脚本；`session-r2.json` 在 STOP 下建立新会话并读取 Core1 真实 committed model，不写 DCO 数值。首个 `no1-10us-r1/` 批次完成、九个 pulse 计数及资源释放有效，但示波器继承旧 RAW 选择范围导致跨通道点数不一致而导出拒绝，恢复 offset 又受旧冻结记录范围钳制；保留失败，不报告物理精度通过。
- 示波器恢复：显式统一完整 RAW 传输范围，并在原件落盘后用示波器 RUN 重建原时基再 STOP/读回；`scope-recovery-r1/` 确认原设置恢复。`no1-10us-r2/` 又记录上一轮遗留的 `-222` 错误队列，未发新脉冲；采集前单独记录并有界清空旧错误，未将旧错误伪装为本轮设置错误。此后九组全部完成并恢复，无 production 改动或第二轮完整 P3。
- 实测快照（仅本轮，非产品精度事实源）：每组一个 2000 ns marker 和八个 500 ns 窄脉冲，tick 4 ns；四板分别测试 10000 ns 与 100000 ns，NO1 补 10003 ns 非整 tick 档。共九组、每组九个完整上升/下降沿，实际 RAW 采样步长为 1 ns / 2 ns；相对请求的最坏相邻间隔偏差如下：10 us 档四板最大 0.775874 ns，100 us 档最大 1.936321 ns，10003 ns 档 3.258492 ns。全组最大累计偏差 4.058274 ns，全部 INFO，未触发 ±100 ns WARN 或 ±1 us ERROR。10003 ns 档相对独立编码的最大间隔/累计误差 0.596505 ns，体现累计量化而非逐格截断。
- 结论边界：插值后的数值不是亚纳秒绝对精度保证；示波器未提供独立校准时基不确定度，本轮也未做长期/温漂/全参数范围验证。PIO COMPLETE 与示波器实测分开记录，只关闭有限局部间隔实验，未证明持续 DPLL 同步后继细分或四板共同 marker 相位。
- 实测入口（本片调试接口快照）：`SYST:VDC:OUTP:SUBD interval_ns,pulse_high_ns,subdivision_count,marker_high_ns,tick_period_ns`；例如本轮 `10000,500,8,2000,4`。独立查询 `SYST:VDC:OUTP:SUBD?` 返回版本/模式/marker 与原 FIXED 批次状态，`...:STOP` 退休该模式。STOP、有效 session 与当前真实 committed model 是前提，不能将接口用于无条件即时 T2。
- 证据与恢复：`out/out1-subdivision-20260922/summary.json` 汇总九组原件和 manifest 哈希，`waveform-and-error.png` 展示实际波形与累计差；`session-r2.json` 确认四板 session/load mask 恢复、STOP/config ACK 闭合，各组确认输出 PIO/DMA 退休及示波器设置恢复。session 工作 JSON 在恢复时追加读回，原采集引用通过 `session-evidence-resolution.json` 解析到不可变 `session-r2-prepared.json`；恢复出的原始字节 SHA 与全部采集预先记录的 SHA 完全一致，未修改旧 manifest 或波形。
- 投入回收：新增固件只做有限模型率编码和独立维护入口；测量脚本留在本次 out 原件目录，不增加主 tools 治理层。达到两档四板和一个非整 tick 决策点后停止扩展，恢复台架；后续不因新部署工作自动重复本实验。文档三件套及最终门禁证据位于 `out/doc-audit/out1-subdivision-20260922/`，检查器测试 38 项通过。
- 下一 gate：回到 T2-DEPLOY-001/002；共同 anchor、持续同步输出细分、正式 deadline、T2 预约及反馈继续由各自未完成任务承接，不由本片局部精度结果授权。

### T2-PROGRESS-20260922-002：OUT1 不升频与 T2 内部细分时间基准

- TODO task ID：T2-CLOCK-001、T2-FEEDBACK-001。用户明确 OUT1 外部频率无需提高，但要在周期内细分，T2 参考内部细分间隔。本轮只更新三件套设计与待办，未改固件、输出频率、PIO 或运行配置。
- 架构新增 T2-GRID：共同 VDC anchor、周期、细分数及 grid generation，绝对细分序号跨周期连续；从绝对位置生成目标，不逐格累加截断步长。各板用自己的稳定 VDC 模型转换 TIMER1 deadline，不靠每次物理 OUT1 的软件采样重置时钟；OUT1 被业务 TRIG 占用时显式协调 IO owner。
- 明确实际 TRIG/REDY 原始时间与格标签分离；T2 目标和窗口可以用格数描述，但实际边沿不吸附到格。跨格/跨窗口的不确定观测不强行裁定；网格和角色换代隔离旧反馈，已装载前缀保持原冻结时间。细分数、最小间隔与接口仍待 T2-CLOCK-001 选定，不虚构同步/捕获精度。
- 验证原件目录：`out/doc-audit/t2-grid-20260922/`；独立复核与文档门禁按本次差异执行，不启动硬件回归。下一 gate 仍为多板部署，细分算法和反馈归类在预约时钟/执行切片接入。

### T2-PROGRESS-20260922-001：多板槽位部署与 T2 主线建档

- TODO task ID：T2-DOC-001、T2-DEPLOY-001。用户选择下一主线为 T2 预约触发，指定参考 node-sequence，并澄清先将单节点多槽位部署到多板，再开发预约触发。本轮要求先交付标准三件套；未执行固件迁移、槽位装载或硬件操作。
- 仓库快照：当前分支 `wip/tdma-real-flight-processing`，保留已有模型监控实现/凭证的暂存改动及文档工作树，未切分支、合并、提交或推送。此前 VDC 模型监控已交付，实际 GPIO 内部观测另列 VDC-OBS-009，不能重新作为 T2 部署的默认前置。
- 参考来源是本地已存在的远端跟踪引用 `origin/feature/node-sequence-reservation-trigger`，解析 commit 为 `0c913e83f241fa8b40c059792141ae6051a85139`；本轮未 fetch，不宣称它是远端最新。使用 `git show`/`git ls-tree` 只读核对，不执行分支工具或套用其硬件凭证。
- 已核实参考模块：分支内 `components/distributed_refmem/{inc,src}/refmem_sequence_roles.*`、`components/sync_trigger/{inc,src}/trigger_reservation_{config,clock,planner}.*`、`trigger_sequence_{config,service,link}.*`、`components/sync_io/{inc,src}/sync_io_sequence.*`。这些为来源树路径，不表示当前树已迁入。当前有 `refmem_slot_claim`、`refmem_application_model`、`refmem_command`、TriggerAO、VDC 映射及 SYNC_IO RUN 基础，需要核对部署与实际调用接线。
- 分支证据边界：其序列 TODO 明确前三种单板模式已有功能证据，第四种共享 VDC 预约仍未完成；NSEQ-121 只有基础快照/映射/规划切片，NSEQ-122/123 的预约运输、fence 和硬件执行仍待实施。普通 LINK 消息不是完整预约对象，单板角色 resolver 要求目标槽位归 local_board_id，不能直接当多板全局执行器。
- 创建本文、T2 Architecture 和 TODO，并在总索引/Trigger 域入口挂接；跨域总纲仍为 `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`。用户随后确认首轮四板四槽，交换原建议的 NO3/NO4：NO1=COUNTER、NO2=SP8T、NO3=VNA、NO4=SP2T/HV，并要求身份能动态切换。文档按受控切换部署代、旧资源/预约退休和新角色读回设计，实际 UID/IO 尚待核对，未操作板卡。
- 用户进一步要求网分网关同时处理 TRIG 和 REDY，每个 T2 节点都具备反馈处理功能。架构新增每节点反馈 profile/生命周期，区分预约 READY、设备 REDY 和运输回执；TRIG 与反馈实际时间分开，迟到/重复/无反馈及动态切换隔离进入必验任务。设备反馈的具体极性、前置许可/后置完成语义尚待实物协议确认；未假设全部角色已具备物理反馈，也未将输出提交作为业务完成。
- 独立复核指出还需明确 SP8T/SP2T 稳定到 VNA TRIG 的跨角色时序，已补两开关各自 settle/必要反馈及角色目标偏移；不可预测反馈采用分阶段预约，先取得切换阶段结果，再预约足够远的 TRIG，不以 PREPARE READY 冒充设备稳定，也不依靠临近 deadline 的软件等待阻止已装载输出。
- 本次未新增 registry 契约或修改现有契约状态；来源分支的单板许可及配置不能替代本轮多板规则。
- 验证与复核：首轮检查发现分支专属引用被当作当前文件、空归档索引被拒，已改为来源说明和未归档状态，未改检查器。strict-names、文档回归、检查器测试、pre-commit、逃生门审计及 `check-staged` 通过；已有登记债务 WARN 保留。独立复核的业务时序问题已修正，结论及最终检查原件目录为 `out/doc-audit/t2-triplet-20260922/`。本轮只交付草案文档，不运行硬件 P3；`check-staged` 仅核对工作区已有实现凭证，不表示 T2 已验收。
- 下一 gate：按 T2-DEPLOY-001 明确多板映射与实际 IO，再按 T2-DEPLOY-002 适配最小部署模型；完整预约仍按 M1–M3 推进，不以本次建档宣布功能完成。

## 验证与证据索引

| 类型 | 位置 | 范围 |
|---|---|---|
| 分支参考 | 上述 commit 的 Trigger sequence 三件套（目录 `docs/trigger/sequence/`，文件前缀 `TRIGGER_SEQUENCE_`） | 仅存在于来源树的方案与单板证据，不是本分支文件或多板验收 |
| 当前基础能力 | `docs/vdc/VDC_TASK_PROGRESS.md`、`docs/tdma/TDMA_TASK_PROGRESS.md`、`docs/refmem/REFMEM_TASK_PROGRESS.md` | 各域现有实现与证据；按实际身份和配置复用 |
| 本次文档检查 | `out/doc-audit/t2-triplet-20260922/` | 三件套、导航和文档门禁，不含 T2 硬件结果 |
| OUT1 有限细分 | `out/out1-subdivision-20260922/summary.json` | 四板局部间隔、非整 tick、RAW 原件与恢复；不含共同同步起点 |
| OUT1 整窗与尾部余量 | `out/out1-window-20260922/r1/verified-summary.json` | 四板两端标记、中间细分、末格留空和恢复；不含 live RUN |
| 共享格规划 | `out/t2-grid-20260922/hil-r2/summary.json` | 四板同目标、本地 deadline 独立复算、七类负测/板和恢复；无输出、非共同参考资格证明 |

## 失败、回退与未验证项

T2 预约本身尚未运行；有限 OUT1 维护实验的真实 build、波形、脚本失败和恢复见 T2-PROGRESS-20260922-003/004。整窗增量构建替换上一版包的历史证据缺口在 004 显式记录，不能宣称旧包仍可重验。多板槽位执行、预约 wire、持续同步与细分共存、正式时钟资格、实际 T2 latch 与 completion 均未在本轮验证。失败按独立运行目录保留，不以摘要重写通过。

## 归档状态

暂无归档。

## 2026-10-13 T2 架构逐级审查（自架构文档下沉，13/13 块）

### 2026-10-13 审查结论（`T2-TIME` / `T2-PREDICT`，对照 HAOFV 顶层）

结论：**两块均与顶层强一致**，无内容性冲突；仅一项治理类待办。

- **`T2-TIME` 时间坐标**：本地网格由 `TIMER1` 经本板 committed DCO 投影到共同 anchor 后派生；明确“接收观察的 `sync_seq` 与本地毫秒格不同种”、“观察未更新时本地网格仍推进”、“本地 `sample_seq` 不能跨板直接比较”。这与顶层的 `local_tick_raw` 自由运行、`system_tick` 只读派生一致。“身份失配或本地时间回退必须显式失效，不补发越过的动作、不重复执行”对应顶层 generation fencing。“软件轮询频率不决定硬件细分分辨率”与“映射有效 ≠ 完整动作精度”对应“不得用 CPU 时刻充当边沿证据”。
- **`T2-TIME` 位置索引**：网络描述符的业务内容是**位置索引**，不是纳秒、本地 `TIMER1` tick 或主站算出的每板 deadline；身份字段只用于防旧版本复活。这是有意的分工（`T_fire_target_vdc` 属 Trigger 本地，线上只走位置索引），与顶层 T2 行不冲突。
- **`T2-PREDICT`**：预测**不在 VDC 发布路径内**；Core1 仅取有界计数观察并记阈值事实，Core0 按最近**已完成**窗口异步解算；无新窗口不伪造新版本；阈值广播只标记应采用版本，**不发全量镜像、不触发槽位热加载**。与顶层“Angle DPLL 只生成扫描预测时间、不得写 VDC offset/rate”一致。
- **`T2-PREDICT` 扫描与在途容量**：角度全用**整数毫度**；配置阶段 **STOP 阶段 fail-closed 校验**（整除、`step_pulses` 正整数、累计不溢出、STEP 时间下限），**运行期不取整、不兜底**；阈值/lead 改为**按一步比例**（避免远端 lead 线性膨胀导致数十个位置同时在途）；**K-7/K-12 已定稿**：只预测下一到两个位置、361 个位置不进常驻状态、`position_id` 保持 4 bit 作在途实例编号、绝对位置由 32 bit `target_ms_index` 确定。“按实测尺寸而非‘一位一槽’线性放大 RAM”对应顶层“不把 Vector 当全局变量”的容量纪律。

**唯一待办（治理类）**：紧凑记录的**跨域契约尚未登记**（须在 `docs/check/DOCS_REGISTRY.md` 登记并经独立交叉审核 C11），
且与 **TDMA 一等席布局的联审**仍为未决项；登记完成前不得写成冻结契约。

### 2026-10-13 审查结论（`T2-ACTION` / `T2-OFFSET`，对照 HAOFV 顶层）

结论：**与顶层强一致**；另有一项**未冻结选型决策**待定。

- **静态模板与不热加载**：模板在 **TDMA START 前**装载并冻结（template ID/generation/CRC、循环跨度、动作 kind/role/本地 IO route、相对偏移、占用与 settle 窗口、SWITCH 编码/GATEWAY 脉宽）；
  运行期位置实例**只标记时间片，不修改 PIO 程序与业务槽位**。与本文 `T2-DEPLOY` 及顶层模式声明/DeploymentGate 一致。
- **位置内多组（K-6，16 组）**：各组均锚定于同一 `q_P` 加模板偏移；**多板模式下组间不由 READY 推进**（K-1），READY 窗口只发现踩踏、不推迟下一组；组间隔按网分**最坏**响应 + settle + 余量排布。
- **预装 + 两组滑动窗口**：执行端同时只持有**两组**已解算绝对 deadline；组 k 执行后在网分测量期内用最新 DCO 解算组 k+2；**装填前比较模型 token**，不一致用新解算，赶不上**判迟并记录、不立即补发**；某组 deadline 一旦被硬件接受**不再追改**（K-2）。与顶层“guard 后不得静默改写已装载 deadline”一致。
- **前提与跨域接口**：DCO 模型切换**必须连续**（不得在模型块边界产生相位跳变），否则同一位置内相邻两组会出现等于跳变量的间隔误差；**连续性由 VDC 域证明**。
- **动作解耦**：不用“下一动作”串行提交；每个动作只负责自己的时间片/输出/证据，顺序由静态 profile 预定，**不转为运行时等待或临界期软件补救**。与顶层“core0 拥堵不得停止 core1 或已装载 PIO deadline / FB 非阻塞”一致。

**未冻结选型（待定，`T2-TURNTABLE-008` 相关）**：本节已记“现有两条源路径——`TRIGger:REServation:SOURce:*` 的 OUT4 硬件 PWM 源 与
`CONFigure:MODEl:TURNtable:*` 的加减速模型源——**选型尚未冻结**，且选型会改变**隔离验收的证据口径**”。
该决定属产品/验收口径，必须在实现前定稿；本次审查不提前预设结论。

### 2026-10-13 审查结论（对照 HAOFV 顶层架构）

本文保留**语义级**结论；实施级规则（资源准入、越界判定、验证与证据分级）已按其职责移入约束文件。

- **资源准入（PIO/SM/DMA/IO 与启动期资源）**：见 `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` §4.3 与 §6。当前实现在 arm 路径内调用 `pio_add_program()`（即运行期热加载），与本节 `T2-DEPLOY` “所有 PIO 程序…属于启动期资源；TDMA RUN 中不做热加载”相冲；整改方向与判据见该文件。
- **故障语义与错误码**：按 `docs/arch/HAOFV_ARCHITECTURE.md` 的 Trigger 域 100–199 与 F0–F3 分级执行（不在本文重述）；实测 `fault_code = 3` 即 PIO/DMA 配置失败。
- **验证顺序与证据分级**：见 `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` §4.5。
- **证据**：A-06 / A-16 的硬件闭环（`total = 2896 = 181 × 16`、`fault = 0`）记录在 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`。

**状态语义映射（架构级，保留于本文）**：运行时 8 态与预约状态机（canonical 见 `ARCH_T2_RESERVATION_ARCHITECTURE.md`）的对应关系为：
`STOP_CONFIGURED`↔`PREPARED`、`RUN_SYNCING`↔`PREPARED`/`WAIT_FENCE`、`PREDICTING`↔`PREPARED`、`POSITION_BROADCAST`↔`DISTRIBUTING`/`WAIT_FENCE`、
`LOCAL_MARKED`↔`ARMED`、`FIRED`↔`FIRED`、`FEEDBACK_CHECK`↔`WAIT_T2`、`COMPLETED`/`UNDETERMINED`↔`COMPLETED`；
预约态中 **本文尚无对应的 5 态**：`DRAFT`、`ARMED`（预约意义的装载提交）、`REJECTED`、`CANCELLED`、`FAULT`，属预约闭环范围。

### 2026-10-13 完善：状态语义对齐与故障分级

**（1）运行时状态 ↔ 预约状态机的映射**（两轴并存，不合并；预约态 canonical 见 `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` 的"预约状态机"）

| 本文件运行时状态 | 预约状态机（canonical） | 说明 |
|---|---|---|
| `STOP_CONFIGURED` | `PREPARED` | 配置、资源与身份读回完成；尚未进入运输 |
| `RUN_SYNCING` | `PREPARED`/`WAIT_FENCE` | 共同时间与 ring 身份就绪，等待 READY/fence 闭环 |
| `PREDICTING` | `PREPARED` | NO1 本地预测推进，尚未产生可发布位置 |
| `POSITION_BROADCAST` | `DISTRIBUTING`/`WAIT_FENCE` | 位置记录进入 TDMA 一等席运输与 fence |
| `LOCAL_MARKED` | `ARMED` | 从板已标记本地动作；对应"本地 deadline 已冻结并装载" |
| `FIRED` | `FIRED` | 输出已执行并获得输出 latch/回执 |
| `FEEDBACK_CHECK` | `WAIT_T2` | 等待设备 READY/T2 latch |
| `COMPLETED`/`UNDETERMINED` | `COMPLETED` | 完成事实与质量闭合；证据不足时保持未确定 |

**预约态中本文件尚无对应项的 5 个状态**（属预约闭环实现范围，按顶层"最后开放预约执行"顺序推进）：`DRAFT`、`ARMED`（预约意义上的装载提交）、`REJECTED`、`CANCELLED`、`FAULT`。

**（2）故障分级与错误码空间**（对齐 `docs/arch/HAOFV_ARCHITECTURE.md`）

- 错误码空间：**Trigger 域 100–199**；示例语义 `1=非法参数`、`2=资源忙`、**`3=PIO/DMA 配置失败`**、`10=ENC target=0`、`11=非法编码器引脚`。模型状态页的 `fault_code` 使用该空间（实测 `fault_code = 3` 即 PIO/DMA 配置失败）。
- 故障分级：**F0 可恢复**（记 `error_code` 后继续，可自动清除或 SCPI `*CLS`）；**F1 域级故障**（本域进入 FAULT，其他域正常，**须显式 CLEAR 事件**，`*CLS`/`*RST` 无效）；**F2 系统级**（`SYSTEM_MODE_FAULT`，停危险输出，需复位或维护模式）；**F3 致命**（Watchdog 复位）。
- 多故障并发：`error_code` 记最严重，`fault_summary` 位掩码保留全部故障位（资源类用 `SYS_FAULT_RESOURCE_MASK`）。

### 2026-10-13 审查结论（`T2-READY` / `T2-START`，对照 HAOFV 顶层）

- **`T2-READY`**：多板下只做**异步时间片踩踏校验**（逐组关联本组 TRIG）；五步（记录上一动作身份与计划窗口 → 窗口内采集 → 映射回共同坐标 → 检早到/迟到/缺失/重复/相邻重叠 → 生成诊断与业务证据）；
  **READY 不作当前动作执行许可、不形成当前动作 fence、不替代 PIO 执行回执、不自动表示 VNA 完成**；无硬件时间戳时只报告状态观察或不确定区间。与本文 owner 表与顶层“不把 READY 当当前授权 / 不填缺失 latch”完全一致。
- **`T2-START`**：启动前装载（deployment、PIO/SM/FIFO/GPIO persona、静态模板含每位置全部组、角度扫描参数、grid/profile）并校验 CRC/generation；
  运行中：VDC 每 1 kHz 同步更新一次 local cursor，**未 LOCKED 并发布时 T2 不生成也不装填 deadline（K-3）**；NO1 取观察、Core0 异步更新预测、阈值跨越后一帧广播；从板解析并标记各组时间片；
  两组滑动窗口装填，**DCO 更新只作用于尚未装填的组**；**不通过 STOP/START 重新加载槽位或改变 IO route**；**不在目标时间片由主机发送即时命令**。与顶层“SCPI 不能驱动硬实时边沿 / core0 拥堵不得停 core1 或已装载 deadline”一致。
- **跨域契约与验证映射**：本文仍为 **Draft**；位置描述符 wire、调试 profile 默认值、资源配额与正式时限**在实现与独立复核后登记**，登记前不把数值快照当产品契约。
  该声明与 AGENTS.md 的文档自回归纪律（契约登记 + C11 交叉审核 + 硬数字不手写）一致。

### 审查收尾（2026-10-13，13/13 块完成）

对照 `docs/arch/HAOFV_ARCHITECTURE.md` 与 `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` 逐块审查完毕：
**语义与 owner 边界层面无内容性冲突**；遗留项为：
① 实现偏离“运行期 PIO 热加载”（整改方向见约束文件）；② “每位置一组”已由硬件闭环（A-06/A-16）；
③ 紧凑记录跨域契约尚未登记与 TDMA 一等席布局联审；④ 转台源路径选型（T2-TURNTABLE-008 隔离验收口径）未冻结。

## 2026-10-13 T2 待办长记录（自待办下沉）

### A-2a 判别规则（2026-10-13 实测得出，搬移前必读）

**不能按标题层级机械切分** ✗：`A.4` 段内既有**接线事实小节**（如 `A.4.2 STEP 如何抵达运行时（定案：独立 setter，不动结构体）`），
也有**按时间追加的日志条目**，两者**同为四级标题、同为两段编号** ✗（如 `A.4.0` / `A.4.1` / `A.4.3`），
仅凭 `#{2,4}\s+A\.\d+\.\d+` 无法区分 ✗✓。

**安全判别（择一，且须逐条列出后再搬）**：

1. **日期后缀**：日志条目标题通常含 `20xx-xx-xx`（如"…（2026-10-13 定）"）；接线事实小节不含 ✗；
2. **显式清单**：搬移前先输出"待搬条目 + 行号"清单，经人工确认后再执行 ✓；
3. **只搬不改**：搬移仅改**归属**（移入冻结归档章节），**不得改写或删除正文** ✗，以便任何一步都可回滚 ✓。

**结论**：A-2a 的正确顺序是 ① 生成清单 → ② 人工确认 → ③ 只改归属的机械搬移 → ④ 更新块地图行区间 ✓。

### 体量合规（2026-10-13 实测：TODO 376 KB = 上限 1.88 倍，必须把日志移出）

**约束依据**：`docs/check/DOCS_REGRESSION_PLAN.md` C14（`:56`）与 R1（`:106`）——**Task Progress 单文件 ≤ 200 KB（204800 字节）**，超限 FAIL；R2（`:112-114`）明确**字节上限是硬约束**、保留窗口只是指导值，故归档用"**最旧优先连续**"。三件套文件接口见 `DOCS-TRIPLETFORMAT-01`。

**实测（2026-10-13）**：

| 文件 | 体量 | 行数 | 判定 |
|---|---|---|---|
| `TRIGGER_T2_ARCHITECTURE.md` | 45 KB | 441 | 合规 |
| `TRIGGER_T2_TASK_PROGRESS.md` | 121 KB | 536 | 合规（余量 79 KB） |
| **`TRIGGER_T2_TODO.md`** | **376 KB** | 5072 | **超限 1.88 倍，必须瘦身** |

**根因**：14x 条按时间追加的日志条目（`A.3.x / A.4.x`）以四级标题形式**挂在 `A.4 接线落点` 之下**，使该章节名义范围达 **4748 行**；日志与"任务清单"混居是体量超限的直接原因。

**处置（只改归属，不删证据）**：

1. 日志条目**移出 TODO** ⇒ 迁入 `TRIGGER_T2_TASK_PROGRESS.md`（本就以"执行证据"为唯一职责）；若该文件亦将超限 ⇒ 按 C14 以"最旧优先连续"归档到 `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_<NN>.md` 并维护归档索引（ID 闭包、条目数相符）；
2. TODO **只保留**：K 条、M0–M5、本结论区、已验证证据页，以及体量很小的迁移附录（`A.1` 实施约束 4 行 / `A.2` K-10 偏差 11 行 / `A.3` 迁移清单 47 行 / `A.5` 已结案 8 行）；
3. **验收判据**：TODO **不含探索日志**且体量降至任务清单量级（建议 < 100 KB）；Task Progress 接收后仍 ≤ 200 KB 或已按 C14 归档；文档门禁（含 R1 字节检查与 C14 轮转）全绿。

**搬移判别**：四级标题内**接线事实小节**与**日志条目**形状相同（同为两段编号）⇒ **禁止机械切分**；以"标题含 `20xx-xx-xx` 日期"为首要判据，并**先出清单、只改归属、可回滚**。

### 执行交接（ROI 状态，2026-10-13）

按用户指定的 ROI 规则记录下一步执行的状态、未验证范围与重启条件。

| 项 | 状态 | 预计时间 | 价值 | 改变的决策 | 最早停止点 |
|---|---|---|---|---|---|
| ① PIO 模式化 + STOP 装载 | `EXECUTE`（待启动） | 1.5 / 2.5 / 4 h | 5 | 执行端能否声明**架构合规** | 模式注册完成 + 连续 `START/STOP` **槽位不减** |
| ② K-2 重算判据 | `EXECUTE`（待启动） | 0.5 / 1 / 2 h | 5 | FREEZE 后未装填组是否仍随最新 DCO 重算 | `:1048`/`:1082` 改用既有掩码后，扰动 DCO 用例通过 |

**本会话不执行的原因**（`DEFER_OR_RECOVER`，不改状态）：两项均为**固件范围**，
按 AGENTS.md 硬约束，提交前必须执行 `python tools/hardware_acceptance/p3_hardware_acceptance.py run`
（四板、约 25 分钟）；在无法完成该验收的会话内提交固件会违反约束，故**不提交未验证改动**。

**未验证范围**：上述两项改动本身（均尚未实施）；已验证部分不受影响（本会话已硬件证实：16 组 `total=2896=181×16`、
六次循环 ARM OK）。

**重启条件**：获得执行许可且可安排一次四板 P3 验收窗口；建议两项**合并一批**（同源码指纹 ⇒ 一次 `run`）。

### ② 的台架语义验收用例（K-2，2026-10-13 设计定稿）

**现状**：已提交的 K-2 改动（`73dcd61a`）已过**回归**验收（P3 四板 PASS_WITH_WARNINGS，ERROR=0/FATAL=0），
但**语义本身无测试覆盖**：`tests/**` 中 `action_armed_mask`/`action_completed_mask`/`prepare_deadlines` **零命中**，
`tools/sync_trigger/**` 也**无 DCO 扰动工具**。

**采用方式（照既有先例，2026-10-13 已核实机制）**：`tests/python/test_trigger_t2_freeze_abandon.py` 的做法是——用 `tools.state_machine_resource_check` 的 **`c_definition_body` 抽取真实函数体**，配一个 C harness（**同名复刻 runtime 静态存储** + **为依赖提供桩函数**），编译运行后断言（见该文件 `:14-15/:17/:19-49`）。

**前提（新增，必须先做）**：本次 K-2 改动是**大函数内部的判据行**，**无法直接抽取**；需先把判据**重构为小函数**（如 `static bool t2_deadline_recompute_needed(uint32_t frozen_bit_accepted, uint32_t prev_version, uint32_t prev_token, uint32_t descriptor_version, uint32_t model_token)`），`prepare_deadlines_core0()` 只调用它；然后 harness 抽取**该小函数**并驱动 C1–C6。

**约束**：该重构属**固件范围**（行为等价），仍需独立走“改动 → 编译 → 烧录 → 四板验收 → 提交”周期，**不得与其他改动并行**。

**原则**：：`tests/python/test_trigger_t2_freeze_abandon.py` 已证明**抽取式 harness**
可直接驱动 freeze 分支（无需硬件）。K-2 同理：把 `prepare_deadlines_core0()` 的判据行抽取为可测函数，
在 host 侧直接驱动**谓词真值表**（无需真实 DCO 扰动）。

| 用例 | 输入（snapshot 掩码） | 期望 |
|---|---|---|
| C1 已冻结且**未装填** | `frozen=1`, `armed=0`, `completed=0` | **不跳过** ⇒ 按最新 DCO 重算并发布（K-2 前半） |
| C2 已冻结且**已接受** | `frozen=1`, `armed=1`, `completed=0` | **跳过** ⇒ 不追改（K-2 后半） |
| C3 已完成 | `frozen=1`, `armed=1`, `completed=1` | **跳过** |
| C4 未冻结 | `frozen=0`, `armed=0` | 重算（原行为不变） |
| C5 token 未变 | `prediction_version` 与 `model_token` 均未变 | 跳过（原行为保留） |
| C6 token 已变 | `model_token` 变 | 重算（原行为保留） |

**判据**：C1 必须**重算/发布**（本次修正的直接目标）；C2/C3 必须**跳过**；C4–C6 保持原行为。
该用例集**免硬件凭证**，属 host 测试，可纳入 `pytest` 常规门禁。

### 判据抽取重构：P3 验收未通过（分支 B，2026-10-13）

**事实**：`
- 首次：`FAIL Latency Cal hardware acceptance: step failed`；子步 `child_capture` 报 `RuntimeError('Capture exited 2')`。
- **重试（1/1，预算已尽）**：编译与四板 OTA 完成后**卡住**；最后一次写入为 `16:04:04`，
  此后阶段目录始终只有 `ota-four-board`，产物年龄与经过时间 **1:1 增长**（56→70→85→106→119→132→143→… s）
  ⇒ 直接证据表明**并非缓慢推进而是无任何新写入**。
- 分级凭证始终未写入（现存为 `HAOFV_P3_JOINT_PROVISIONAL_V1`，非 `..._GRADED_V1`）。

**未验证范围**：该重构的硬件回归（编译已过：`build exit=0`）；**行为等价性仅由代码审阅保证**。

**处置（按用户指令与 ROI）**：
- 重构**保留在工作树且已暂存**，**未提交**；不影响已提交的 K-2 语义修复（`73dcd61a`）。
- **不并行修补其他部分**；不连累其他改动。
- **重启条件**：台架恢复（排查 `Latency Cal`/`child_capture` 与 OTA 后卡住）后重跑一次 `run`；
  若仍卡在同一步，则先排查环境而非本次改动（本次为**纯抽取、零顺序变化**）。

### T2-AUDIT-04 决策输入：两条转台源路径的对照（2026-10-13，不预设结论）

供用户定稿；本表只罗列架构条款与现有代码事实，不替代决策。

| 维度 | **A. `TRIGger:REServation:SOURce:*`（OUT4 硬件 PWM 源）** | **B. `CONFigure:MODEl:TURNtable:*`（加减速模型源）** |
|---|---|---|
| 架构描述 | 本文 `T2-OWNER`：“STOP 预装本板 **OUT4** 的独立 PWM，RUN 只切换已装载波形使能；**不由 Core1 定时发脉冲**；不占用 VDC/TDMA 运行服务”。它**提供名义转台输入，不授予输入边沿时间戳或位置到位精度** | 模型组件 `components/model_turntable/`（本会话已实现并硬件验证：16 组 `total=2896=181×16`、分块续装订、`GROups`、PIO 一次装载 + 六次循环 ARM OK）；经 `sync_io_model_sched` 驱动 **PIO 波形输出** |
| 边沿与证据 | 仅名义输入（不授予边沿时间戳/到位精度）⇒ 适合**驱动计数/预测链路的名义激励** | 产生**真实 PIO 边沿**（可作为 sync_io 的实际输出事实）；但当前状态属**未冻结选型**，且其 PIO 声明尚未纳入模式设施 |
| 资源与模式 | 作为 `SOURce:*` 现有路径，与 OUT4 本地命名空间绑定 | 需成为**已声明模式**（`sync_io_mode_ops_t.hw` + 派发表），否则违反 `T2-DEPLOY`与 `ARCH-PIOCAT-01`/`ARCH-PIOPARTITION-01`（即 T2-AUDIT-01） |
| 隔离验收（`T2-TURNTABLE-008`） | 选型决定后，验收证据口径随之变化（本文原文已声明） | 同上 |
| 与约束的关系 | 不需新增 PIO 资源（现有路径） | 必须先完成 **T2-AUDIT-01**（模式化 + STOP 装载）才能声称合规 |

**待用户明确的三个问题**（本任务不预设答案）：
1. 验收（`T2-TURNTABLE-008`）要求的是**名义转台输入**还是**可作为事实的硬件边沿**？
2. 若选 B，是否同意先完成 **T2-AUDIT-01**（否则不具备声明合规的前提）？
3. 是否允许两路并存（A 作台架驱动、B 作产品输出），还是只保留一条？

### T2-AUDIT-03 前置：契约冻结清单（2026-10-13，免台架）

依据：`TRIGGER_T2_ARCHITECTURE.md` 尾节“位置描述符 wire、调试 profile 默认值、资源配额和正式时限
在实现与独立复核后登记”与 `T2-PREDICT`“紧凑记录的跨域契约须在 `docs/check/DOCS_REGISTRY.md` 登记并经独立交叉审核（C11）；
**登记完成前，当前实现不得写成冻结契约**”。

**必须先冻结的五项**（每项需明确到“可作为契约条款”的精度）：

| # | 项 | 当前状态与待定义点 |
|---|---|---|
| 1 | **位置描述符 wire 布局** | K-12 已定：**10 B 单帧**（tag + 4 bit `position_id` + 16 bit 版本 + 32 bit `target_ms_index` + 24 bit fine slice）；待定义：它在既有 RefMem 区的**精确偏移/长度**（`TRIGGER_POSITION_SLICE_WIRE_SIZE` 恰填）、**message subtype/身份字段**、CRC 归属与版本化 |
| 2 | **与 TDMA 一等席布局的联审** | 待定：一等邮箱在 process image 的位置、轮换比例（`SYSTem:TDMA:SEAT:ROTation`，默认 1:1）、与两板 **effective schedule CRC** 一致性门的关联（见 K-5 增补） |
| 3 | **调试 profile 默认值** | `switch_settle_offset_q` / `gateway_trig_offset_q` / `ready_check_offset_q` / `ready_window_q`；`threshold_per_mille`（当前 950/1000）；`pre_roll_mdeg`/`post_roll_mdeg`（台架 5°，**调试快照非产品额定值**） |
| 4 | **资源配额** | `TRIGGER_T2_RUNTIME_POSITION_COUNT`、`TRIGGER_POSITION_FREEZE_LATCH_IDS`、控制字节偏移、**RAM 按实测尺寸**（K-7/K-12）；以及 **T2-AUDIT-01 落地后**的 mode 声明与 PIO/SM/DMA/GPIO claims |
| 5 | **正式时限** | STEP 时间下限（由位置内多组跨度推出）、Core1 服务预算（顶层 `HIGH_PRIORITY` 500 µs）、`T2_FREEZE_RESEND_SLICES` 节奏、窗口到期判据 |

**流程（不可跳步）**：决定上表 → 起草契约行（`contract_id` 唯一、**锚点必须真实存在**）→ **C11 交叉审核**（禁自审自批）→ 登记 → 此后方可将实现描述为已冻结。
**护栏**：登记前不得把任何**数值快照**写成产品契约（AGENTS.md 硬约束 2）。

### C14 R3 命名规则（实测教训，2026-10-13）

`doc_regression` 的 **C14 R3** 要求：写在 `*_TASK_PROGRESS.md` 的 `归档索引`（章节） 行，
**必须指向** `docs/legacy/<domain>/LEGACY_<DOMAIN>_TASK_PROGRESS_<NN>.md`。
因此：**归档的是「进度日志」时**，才在 Task Progress 里登记索引；
**归档的是「待办附录/历史快照」时**，只在 `docs/README.md` 登记，并在本文保留指针，
不得写入 Task Progress 的 `归档索引`（章节）（否则 FAIL）。

当前实际登记：`docs/README.md` 中的 `legacy/trigger/LEGACY_TRIGGER_T2_TODO_01.md` 行（已归档、已登记）；
若今后需轮转 `TRIGGER_T2_TASK_PROGRESS.md`，则按 R3 命名为 `LEGACY_TRIGGER_T2_TASK_PROGRESS_<NN>.md` 并在该文件内登记。
