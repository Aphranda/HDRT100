# TRIGGER T2 任务进展归档 05

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_05.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-18

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20261012-001` .. `T2-PROGRESS-20261010-024`（按新鲜度倒排，最新在前），共 5 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。

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





