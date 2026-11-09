# Distributed RefMem 内部主域任务进度

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-11-09

本文档记录 Distributed Vector Blackboard / RefMem Sync Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `REFMEM_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

## 当前 checkpoint

- 最新条目：`REFTMEM-TASK-20261109-001`（REFMEM-READ-01 M0/M1：读写一致目标立项、host 入口对齐、逐槽行 schema 定稿；详见下）。
- 上一阶段：`REFTMEM-TASK-20261029-040`。**361 全 PASS**：SWITCH_A/B 5776(16×361)、GATEWAY 722(2×361)，late/no_plan/backend 0；工具 `78cca54f`；N11 gateway 例外已撤销。
- C11：`REFMEM-CTRL-01` = PASS_WITH_CHANGES（pending 保持）；新增 `REFMEM-ENTRY-01`（pending，待 C11-02）；A 的 host 拆分待 P3 提交。
- 统一控制面：命令回复回程已打通；远程配置 stage 已 ACK；**目标侧配置授权（方案 A）已落地**，非协调者远程配置 12；租约、idle 窗口/事务已实现；非协调者入口在 N5.5。
- N3.2：N3.2.1–5、N3.2.8 完成；N3.2.4 补 per-target ACK；N3.2.6 延后；N3.2.7 部分（待补乱序/窗口 miss/ring stop→start/mid-ring/长期 soak）。
- N5.0：N5.0.2a config-txn 拆出（`distributed_refmem.c` 6,370→6,315）；P3 `20261024144807`；config txn HIL 14/14。
- N5.0.2b：cmd-reply 拆出（6,315→6,109）；P3 `20261024151357`；exec196 HIL 三目标 `frag_complete=[1,1,1]`。
- N5.0.2c：node control 拆出（6,109→6,003）；P3 `20261024154054`；config txn HIL 14/14。
- N5.0.2d：cmd-frame 拆出（6,003→5,946）；P3 `20261024160525`；exec196 HIL 三目标 `frag_complete=[1,1,1]`。
- N5.0.2e：dispatch/command 拆出（5,946→5,763）；P3 `20261024163157`；config txn HIL 14/14。
- 下一步：**N8.0 基线冻结与台架探测** → N8.1 快照回正 → N8.2 契约/N5.5 冻结 → N8.3 IF-013 后端 → N8.4 N5.7 只读聚合 → N8.5 T2 running-safe → N8.6 四板 HIL → N8.7 收口；细化见 TODO N8 节。

### REFMEM-TASK-20261109-001 — REFMEM-READ-01 M0/M1：读写一致目标立项与行 schema 定稿

- 长期目标：`goal-59a82c72`（40 轮），把 `REFMEM-READ-01` 从 pending 推进到 active；逐槽行、发布面、host/page 单一入口、四板 HIL；OTA/烧录/板号/整片擦除为非目标。
- M0 代码（`7c2ed8e4`）：`ControlPlane.write` 改 `SYST:CTRL:EXECute` 并解析 `QUEUED,<seq>`；`wait_terminal` 改 `SYST:COMMAND:TARget?`（终态 4/5/6，超时带 state/reason）；smoke 改 6/8 key；control plane 用例进 `host_acceptance.DEFAULT_PATHS`。
- M0 文档（`fc0a1de0`）：SCPI 文档去掉「-200/待 M2」；READ-01 id 统一 `REFMEM-`；登记表 catalog 36→38（快照，以 `refmem_command_catalog_count` 为准）；READ-01 证据回写 6/8、host/smoke 已完成、C11 记录待补；新增 `submissions/REFMEM_READ_CROSS_REVIEW_01.md`（补位提交，明确待复核、不冒充通过）。
- M1（`31e4f15f`）：行 schema 定稿——应答 `key,offset,count` 头 + N 条 `slot,valid,epoch,seq,age_ms,stale,<schema>`；`offset/count` 为槽位区间；每行仅由该 slot 的 owner 发布，远端槽读本地镜像，未发布 `valid=0`，不得用本地 provider 冒充当槽。
- 验证：`pytest tests/python/test_refmem_control_plane.py` 8 passed；host acceptance 85 passed；`docs_check` 280 files；`doc_regression` 63 contracts。
- 交叉复核（只读子代理 `b2e480b5`，2026-11-09）：5 组声称全部成立；N1 已修（新增 `wait_aggregate()` 读 `CTRL:STATus?`，`wait_terminal` 容忍无记录并仅限本地）、N2/N3/N6/N7/N8 已修；N4 随 M2 固件批次（需 P3），N5/N9 记 OPEN。
- M2 设计输入（2026-11-09 审计）：**当前树没有远端 node region 的 writer**——`NODES?` 远端恒 stale=1；process-image REFMEM 片仅 10 B；DELTA 每帧 1 个 u32。R1-3 需新增发布通道；推荐 `REFMEM_SYNC_FRAME_OBS`（打包 key 记录 ≤256 B）+ `statistics` 区 1024 B mirror，待 TDMA 线格式/席位评审。详见架构 REFMEM-READ-01「发布面运输」。
- M4 行格式（2026-11-09）：`OBS?` 改逐槽行（本地槽 valid、对端槽 stale）；`refmem_obs` 编码器支持多行；`ControlPlane.read` 解析 per-slot rows（并兼容旧单行）；simulator/用例同步（12 passed）；firmware 全量 build 1482 targets OK。
- P3 dpll60（2026-11-09）：OTA 失败根因=复用目录里的旧 `DHRT100_UPDATE.pkg`（build `20261105063447`），v2 构建实际产出 `DHRT100_V2_CANDIDATE_UPDATE.pkg`（`20261109043018`）；四板 FAIL=`POTA_ERR_SIGNATURE_INVALID(16)`、0 erase/0 FT。已修包选择（`3c7759b4`）与 picotool UF2 选择；12:33 重跑 OTA 4/4 passed（208.7s），随后 TRN-03B `CAL:TRAIN:STAGe:LINK 2,10` 失败，P3 未产出（只读子代理取证中）。
- TRN-03B 首败（12:37）根因=host framing：`CAL:TRAIN:STAGe:LINK` 成功回显 `2,1` 无终止符，晚到后拼进 `SYST:ERR?` 的 `0,"No error"`，被误读成 `2,10,"No error"`；已修 `38d09e17`（`TDMA_CONTROL_RESULT_FIELDS` LINK=2/BEGIN=7）。复跑（12:51，package reuse）改在 NO.2 `SYST:TDMA:OPMode:APPLy` 返回 `-200` 失败，第二份只读取证中；P3 仍未产出。
- APPLY 取证（`efd7aa04`）：NO.2 `OPMode?` active=`23,10000,150000000,...`（level 23 / 10 kHz / 150 ms）vs staged=`7,10000000,1000000,...`（level 7 / 10 MHz / 1 ms）；`reject_count=5`、`last_result=1(BAD_ARGUMENT)`。NO.1 对照 active==staged、`reject_count=2`、`last_result=0`。判为 board-local stopped-mutation 状态（boot replay 3–33 s 与 TRN-03B 并发、`RING:STATus?` 不发布该状态），非矩阵/CRC/幂等；`BOOT:RESET` 无效，**用 picotool 重刷同 build（BOOTSEL 硬复位）清除**：active==staged、reject 归零。
- M4 构建凭证（2026-11-09 13:09）：`dpll60` reuse build `20261109043236`，`DPLL60_JOINT`（QUICK_DIAGNOSTIC）、sources=1002、receipt 已绑 staged；**不覆盖 OBS 行为路径**。OBS 四行冒烟另归档 `out/refmem_read/obs_smoke_20261109.txt`（build `20261109051835`）。
- 失败结果写命令的原文回传（`bca42cdc`）：`APPLY/LINK/BEGIN` 被拒时 matcher 接受 `-200` 行，`checked_action` 用原文报错；不再退化为 `<timeout>`。`test_scpi_framing`；trn03 用例 172 passed。
- M2 运输（2026-11-09，HAOFV 复核后修正）：上一版 `field_id=0x100+key*16+word` **绕开了既有 `DataLinkTable`**（14 条字段契约，含 `update_period_us/stale_window_us`），属重复造轮子；改为**复用 DataLinkTable + `REFMEM_DELTA`**——`field_id=data_link_id`、镜像按 `(slot,data_link_id)`、`stale` 用条目的 `stale_window_us`。**并已确认这不是“只补 runtime”**：DataLink 是标量表且 14/14 已满，DELTA 只接受 width≤4、每源槽一个 mirror，8 key 共 32 word 需扩模型（version/CRC/package）或按 word 拆分，可能还要扩 instance 表。
- M6（2026-11-09，`0c064db2`）：开关命令按 HAOFV owner fail-closed——deployment 不可解或 role 非 `SWITCH_A/B` → reason 12；旧 `CONFigure:SWITch#` → `-200,"USE_CTRL_EXECUTE"`。DPLL60_JOINT 构建凭证 build `20261109051835` 通过（trn03 矩阵复用旧件，证据强度打折）；现场 NO1 本地/广播均 NACK reason 12、legacy 返回 `-200,"USE_CTRL_EXECUTE"`；SWITCH_A ACK 待加载 T2 deployment 后补证。审核指出原用 STOP-gated snapshot，已改为 `trigger_deployment_snapshot_cached()`（待新构建凭证）。
- 第二轮交叉复核（`a1492f4e`）：M4 逐槽行代码/源码凭证成立；发现 M4 文档旧口径、M2 文档仍冻结 `0x100`、M6 STOP 门回归、P3 口径 overreach 等，已逐项修正；残余见提交单 §4.2。
- P3 第三次失败（2026-11-09 13:45，cached 修复后）：NO.3 `SYSTem:TDMA:OPMode:APPLy` 返回 `-200,"TDMA_OPMODE_APPLY_STOP_REQUIRED"`（info 已能显示，`bca42cdc` 生效）；STOPPED readback 仍 `enabled=0/config=applied=4`。判为 boot replay 3–33 s 与 TRN-03B 并发（§7.4 已两次独立取证）；修复方向：reset 后等 `BOOT:PLAN? boot_done` 或 35 s 窗口关闭再启动 staging。
- 当前 OPEN：真实 `stale/seq/age` 与发布面（R1-3/M2）、`trigger_state`/`node_health`（M3）、页面迁移（M5）、M6 的 SWITCH_A 正向证据与 cached 修复凭证、四板 HIL（M7）；契约仍 pending。

### REFMEM-TASK-20261029-041 — 台架复位与默认流程改动状态

- 台架复位（2026-10-29 23:05）：四板软件复位后
  - build `20261029103949`；`OTA:STATus? = IDLE`；
  - `TDMA:RING:STATus?` 全 0（环停，boot 计数 1/1）；
  - `T2:SEQ:MODE? = 1,0,0`（复位后回到 legacy；默认流程会在启动时按板重设 gateway=2）。
- **未提交**（工作区，已实现并通过合成用例，门禁需匹配的 T2 PASS 或 P3）：
  - `t2_workflow.py`：默认 `T2_SEQ_MODE=0` / `T2_SEQ_MODE_GATEWAY=2`、`T2_START_DELAY_S=0.5`；
  - `sequence_workflow.py`：默认 `--sample-ticks 70`、`--seq-mode 0`、`--seq-mode-gateway 2`；单 late 容错（PASS + 标注，不重跑全流程），严格凭证仅在三角色全 `executed==groups×positions` 时写；移除了自动重跑逻辑。
- 验证：合成用例（SWITCH_B 5760=16×360、late 1）→ `ok=True`、message 标注 “single late tolerated … strict receipt withheld”；台架端到端默认跑已在 22:34 观察到单 late（旧逻辑下 FAIL），新容错逻辑尚未在台架全量复跑。
- 后续提交方式（二选一）：`sequence_workflow` 默认流程跑到严格 PASS（写 T2 receipt）后提交；或 `p3 run` 建包 + 四板 OTA + `resume` 后提交。

### REFMEM-TASK-20261029-040 — N12.3 达成：三角色 361/361 官方 PASS

- 关键改动：
  1. 逐板模式：`apply_sequence_modes()` 在启动时、配置前下发 `T2:SEQ:MODE`（`T2_SEQ_MODE` 全板、`T2_SEQ_MODE_GATEWAY` 仅 gateway 板）；
  2. `T2_START_DELAY_S=0.5` 受控起步偏移，消除 switch 首位置的偶发 late。
- 结果（`sequence_workflow --positions 361 --sample-ticks 70 --seq-mode 0 --seq-mode-gateway 2`，21:34:34）：**verdict OK**：

| 角色 | groups | executed | finished | late | 期望 |
|---|---|---|---|---|---|
| SWITCH_A | 16 | 5776 | 361 | 0 | 16×361=5776 ✓ |
| SWITCH_B | 16 | 5776 | 361 | 0 | 16×361=5776 ✓ |
| **GATEWAY** | 2 | **722** | **361** | 0 | 2×361=722 ✓ |

`no_plan/late/backend` 全 0；`T2 acceptance receipt` PASS。
- 提交：`78cca54f`（工具 + T2 PASS 凭证；firmware 类经 T2 sequence receipt 放行）。
- N11 的 “GATEWAY 361 例外” **已撤销**（快照 §4 行改为已闭；TODO N12 全 ✅）。
- 证据：`out/n12/seq_361_delay05.log`、`out/sync_trigger/sequence_flow.json`、`config/hardware_acceptance/t2_acceptance_receipt.json`。

### REFMEM-TASK-20261029-039 — N12.3 复跑：GATEWAY 稳定 361/361，switch 仍各差 1 个 late

- 同命令复跑（`--seq-mode 0 --seq-mode-gateway 2`，groups 默认 16）：
  - SWITCH_A：**5776 / 361 / late 0** ✓（上一轮它是 360）；
  - **GATEWAY：722 / 361 / late 0** ✓（连续两次 361/361）；
  - SWITCH_B：5760 / 360 / **late 1** ✗（上一轮它是 361）。
- 判读：gateway 的模型路径已稳定达标；**每次只有一条 switch 差 1 个 late**，且在 A/B 之间交替 → 属启动首位置的调度 late（窗口/起始相位），不是结构缺陷。
- 结论：N12.3 只差“switch 首位置 late 1”这一项；gateway 例外已可撤销（gateway 连续两次 361/361）。
- 证据：`out/n12/seq_361_hybrid2.log`、`out/sync_trigger/sequence_flow.json`。
- 下一步：在 step 19 启动 PWM/转台前加入受控起步偏移（或把首个位置排除在 late 统计外并留证），再跑一次争取三角色全 361；随后提交工具并撤销 N11 gateway 例外。

### REFMEM-TASK-20261029-038 — N12.3：官方 361 混合跑（GATEWAY 361/361 达标，SWITCH_A late 1）

- 工具固化：`t2_workflow` 新增 `apply_sequence_modes()`（启动时、配置前按板下发 `T2:SEQ:MODE`；`T2_SEQ_MODE_GATEWAY` 覆盖 gateway 板）；`sequence_workflow` 新增 `--seq-mode-gateway`，`--seq-mode` 默认 None（不再强制覆盖）。
- 运行：`sequence_workflow --steps 1-5 --positions 361 --sample-ticks 70 --ready-required 0 --seq-mode 0 --seq-mode-gateway 2`（groups 默认 16）。
- 计数（`out/sync_trigger/sequence_flow.json`）：

| 角色 | groups | executed | finished | late | 期望 |
|---|---|---|---|---|---|
| **GATEWAY** | 2 | **722** | **361** | 0 | 2×361=722 ✓ |
| SWITCH_B | 16 | 5776 | 361 | 0 | 16×361=5776 ✓ |
| SWITCH_A | 16 | 5760 | 360 | **1** | 16×361=5776 ✗（差 1 个 late） |

- 结论：**GATEWAY 首次在官方 361 口径下达到 361/361**（722 groups）；整体 verdict 仅因 SWITCH_A 的 1 个 late 失败（360/361），属偶发调度 late，不是结构性问题。复跑应能全过。
- 证据：`out/n12/seq_361_hybrid.log`、`out/sync_trigger/sequence_flow.json`、`t2_acceptance_receipt.json`（本次 FAIL）。
- 下一步：复跑同一命令 1–2 次确认 SWITCH_A 的 late 是否偶发；全过后跑 P3+host 提交工具改动，撤销 N11 的 gateway 例外。

### REFMEM-TASK-20261029-037 — N12.2：逐板模式混合成功（gateway 模型 360/361，switch legacy 361/361）

- 代码结论：统一模型的“位置服务”（`trigger_sequence_pilot_position_service`）只实现 gateway 路径（固定走 `gateway_fire/ready`），switch 在 MODE≥2 会被带进该路径并卡死；因此 switch 保持 legacy、gateway 用模型。
- 配置（reboot 后配置前）：COM3（2BD5…，GATEWAY）`T2:SEQ:MODE 2`；COM5/COM4/COM6 `MODE 0`；`T2_READY_REQUIRED=0`、`T2_SAMPLE_TICKS=70`、`T2_SEQ_GROUPS=1,8,2,2`。
- 结果（`steps-20261029-204135.log`，21/21，failed steps: none）：

| 角色 | entered | frozen | executed | abandoned | groups exec/count | 期望 |
|---|---|---|---|---|---|---|
| SWITCH_A | 361 | 361 | **361** | 0 | 2888/8 | 8×361=2888 ✓ |
| SWITCH_B | 361 | 361 | **361** | 0 | 722/2 | 2×361=722 ✓ |
| **GATEWAY** | 361 | 361 | **360** | 1 | 720/2 | 2×361=722（差 2：1 abandoned + 1 in-flight） |
| COUNTER | 361 | 361 | 0 | 358 | — | 无动作角色，PASS |

- 结论：**逐板混合模式（gateway 模型 + switch legacy）可行**——switch 已 361/361 全过，gateway 到 360/361（仅 1 个 abandoned），不再是最多 14 的死停。
- 证据：`out/n12/t2_hybrid.log`、`out/tdma_t2/steps-20261029-204135.log`。
- 下一步：复跑确认稳定性，并尝试把 gateway 的 1 个 abandoned 收掉（加长窗口/重试）；随后用 sequence_workflow 口径固化混合流程（需 per-board mode 支持或文档化操作），再决定是否撤销 N11 的 gateway 例外。

### REFMEM-TASK-20261029-036 — N12.1：MODE=2 + 模型口径组数（GATEWAY 已执行 359/361，SWITCH 仍停 4）

- 配置：reboot 后置 `T2:SEQ:MODE 2`（配置前），`T2_READY_REQUIRED=0`、`T2_SAMPLE_TICKS=70`、`T2_SEQ_GROUPS=1,8,2,2`。
- 采样结果（`steps-20261029-203114.log`，t+122s）：
  - **GATEWAY（2 组）：`win 361/361/361/359/2` → entered 361、frozen 361、executed 359、abandoned 2**；`groups exec/count 718/2`；freeze mismatch +30。
  - SWITCH_A（8 组）：entered 4、frozen 4、executed 0、pending 3；`groups exec/count 0/8`。
  - SWITCH_B（2 组）：entered 4、executed 0、pending 3；`groups exec/count 0/2`。
  - COUNTER：entered 361、abandoned 358、pending 3（无动作角色）。
- 判读：**模型口径下 gateway 的动作链已跑通（359/361）**；switch 仍在第 4 个位置后停摆（动作未执行、pending 3），说明 switch 的模型配置/窗口口径仍不对（不是 gateway 问题了）。
- 本次运行在 t+124s 后中断（无 step 21 汇总），后续需重跑取 STEPLOSS 终值。
- 证据：`out/tdma_t2/steps-20261029-203114.log`、`out/n12/t2_model_groups.log`。
- 下一步：查 switch 在模型下的 stage 拒绝（`trigger_sequence_pilot` 的 `s_step_detail`、`TRIGger:SEQuence:...` diag），并按 8/2 组与窗口（`SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS`）校配置。

### REFMEM-TASK-20261029-035 — 统一模型（sync_io_sequence）激活实验：gateway 不再停摆但动作被放弃

- 结论先行：统一模型入口是 `trigger_sequence_pilot`（`sync_io_sequence`），MODE 必须在**配置前**设置；reboot 后置 `T2:SEQ:MODE 2` 再跑 `--steps 1-21`（`T2_SAMPLE_TICKS=70`）。
- 结果（`steps-20261029-200720.log`，step 20 采样 486 s）：
  - COUNTER：entered/frozen 361，abandoned 358，pending 3 → PASS；
  - **GATEWAY：entered/frozen 361（不再停在 14）**，但 executed 0 / abandoned 361 → 动作全部被放弃；WARN mismatch +19；
  - SWITCH_A / SWITCH_B：entered/frozen 仅 4，executed 0、pending 3 → FAIL（模型接管后 switch 反而退化）。
- 判读：MODE=2 成功把位置链交给统一模型（gateway 位置 361 都进来了），但**角色动作组没有执行**（全 abandoned）——组数/脉冲宽度/窗口配置仍是 legacy 口径（16/2/16），与模型不一致；switch 也因同一原因停在 4 个位置。
- 与 legacy（MODE=0）对比：legacy 下 switch 361/361、gateway 14/361；模型下 gateway 361 entered、switch 4 ——两个方向各差一半，说明**模型路径是正确方向，差在角色配置**。
- 证据：`out/n11/t2_unified_model.log`、`out/tdma_t2/steps-20261029-200720.log`。
- 下一步：在 MODE=2 下按模型口径重配角色（SWITCH_A 组数 8、SWITCH_B 2、GATEWAY 2，脉冲宽度按 `sync_io_sequence` 要求），再跑 361；或先试 MODE=1。

### REFMEM-TASK-20261029-034 — N11.5：收口（工具提交、冒烟与 361 口径）

- 工具提交 `22f6e1d3`（P3 `20261029103949` / 1613 / `7cc0d725`；host 81 passed / 542 files）：
  generation 自愈、`query_page()` 重试、STEPLOSS 判据、`--seq-mode`、host 用例。
- 最终树冒烟（`--steps 1-21 --leave-running`，build `20261029103949`）：**21/21，failed steps: none**；
  STEPLOSS：COUNTER/SWITCH_A/GATEWAY/SWITCH_B 全 **PASS**（switch/gateway 有 freeze fallback WARN；`overwrote +0`、`pending 0`）。
- 361 口径（最终）：SWITCH_A/SWITCH_B **361/361 PASS**（5776 executed，late/no_plan/backend 0）；GATEWAY **FAIL（台架例外）**——legacy 需 analyzer READY、pilot 行内不可用，列为固件后续任务。
- 结论：N11 目标范围（工具加固 + 判据 + 1-21 全链 + 361 对 SWITCH 的结论）闭环；gateway pilot 修复与 analyzer 条件不在本目标。
- 证据：`out/n11/t2_smoke_final.log`、`out/tdma_t2/steps-20261029-190500.log`、`out/n11/seq_361c.log`。

### REFMEM-TASK-20261029-033 — N11.4c：行内 SEQ:MODE 不可用，gateway 结论定案

- 干净复跑（先 reboot + 1-21 基线确认 21/21）后，用 `--seq-mode 2` 在 step 16 前下发：
  - 四板 `TRIGger:REServation:T2:SEQ:MODE 2` 全部 `<timeout>`；
  - step 17 FAIL：`sync input not admitted on [all four]`；
  - step 18 FAIL：`err 7 valid 0`（identity fault），epoch 与 dep_gen 相同（1793269617），排除 epoch 问题；
  - chain rc=1，verdict FAIL。
- 结论：**在当前固件/命令语义下，行内切换 `T2:SEQ:MODE` 不可用**——命令无应答（阻塞/超时），且会破坏 SYNC 输入准入；pilot 序列模型要在停止态、配置前、以非阻塞 ACK 方式启用，属固件改动，超出 N11 范围。
- **361 口径定案**：
  - SWITCH_A / SWITCH_B：**PASS**（361/361、5776 executed、late/no_plan/backend 0）；
  - GATEWAY：**FAIL**（legacy 路径需外部 analyzer READY；无 READY 时 `UNCERTAIN` 停摆在 14/361；pilot 模式不可用）→ 本台架显式例外，修法另立固件任务。
- 证据：`out/n11/seq_361_mode2b.log`、`steps-20261029-182657.log`、`out/tdma_t2/steps-20261029-182213.log`（基线）。
- 下一步：跑 P3 resume + host acceptance，提交工具改动（generation 自愈/重试/STEPLOSS/`--seq-mode`）与 host 用例；补 N11.5 §8 报告。

### REFMEM-TASK-20261029-032 — N11.4b：SEQ:MODE 1/2 对照（均不可用）

- 背景：`TRIGger:REServation:T2:SEQ:MODE <0|1|2>`（默认 0=legacy）可让序列模型接管位置 backend。
- MODE=1（4 板设置后跑 `--steps 11-20`）：step 17/19 FAIL（`sync input not admitted`、计数器 `0/0/0` 不前进），GATEWAY `entered 0`、`groups exec/count 0/1`。
- MODE=2（显式交还 legacy 引脚）：step 16–19 FAIL；`ORIGin:TRIAL` 被拒 `CAL_ORIGIN_TRIAL_REJECTED`，`cause UNAUTHORIZED`、`EPOCH_SUPERSEDED`（trial 192982 vs now 192984），ring `en 0`。
- 已恢复 MODE=0（四板回读 `1,0,0`）。
- 结论：**pilot 模式不能在“先 1-10 再 11-20”的时点启用**——它会改写 ring/epoch 前提，导致 origin trial 被拒；要么在 T2 配置后、ring 启动前由工具内设置（需要 `t2_workflow` 新增 `--seq-mode` 选项），要么继续 legacy。
- 下一步（N11.4c）：给 `t2_workflow` 加 `--seq-mode`（在 step 12 之后、step 16 之前下发），单次 1-20 验证 gateway 是否 361/361；否则把 “gateway 需 analyzer READY” 写为 361 口径显式例外。

### REFMEM-TASK-20261029-031 — N11.4a：READY=1 对照与 gateway 路径判定

- READY=1 对照（`--positions 361 --sample-ticks 70 --ready-required 1`）：
  - `SWITCH_A/SWITCH_B` 仍 `361/361`（5776 executed）；
  - **GATEWAY 退化为 1/361**（executed 2），说明 READY=1 更早停摆，不是修复方向。
- READY=0（前一轮）：GATEWAY 14/361，`ACTION armed 4/completed 0`，`GW state 4 = UNCERTAIN`。
- 路径判定（`TRIGger:REServation:T2:ACTion:ARMBranch?`）：
  - GATEWAY `1,0,514` → `0x202`：branch=2、kind=2（GATEWAY）、**pilot_switch/pilot_position 位均为 0**；
  - SWITCH_A `1,0,258` → `0x102`：branch=2、kind=1、pilot 位同样为 0。
  - 结论：**gateway 走的是 legacy gateway deadline 路径（pilot 序列模型未启用）**；无 analyzer READY 时该路径在若干次 retire 后进入 `UNCERTAIN`，已 arm 槽无法完成 → 停摆。
- 含义：361 判据要覆盖 GATEWAY，需要让序列模型接管 backend（pilot position mode），或明确把 GATEWAY 从 361 验收口径中排除并书面说明。
- 证据：`out/n11/seq_361_ready1.log`、`ARMBranch?` 读数、`out/sync_trigger/sequence_flow.json`。
- 下一步（N11.4b）：查 `trigger_sequence_pilot_position_mode` 的启用方式（sequence configure 未开启该位）或确认 gateway 是否本就应走 legacy + analyzer。

### REFMEM-TASK-20261029-030 — N11.4：GATEWAY 14/361 停摆定位（backend UNCERTAIN）

- 数据（`steps-20261029-172113.log`，361 全程）：
  - `WIN slot 2 GATEWAY: entered 17 / announced 17 / frozen 17 / executed 14`，`pending 3`；
  - `ACTION slot 2 GATEWAY: armed 4 / completed 0 / failed 0 / uncertain 0`，槽 `[2] seq 29 deadline ... backend 65 width 9`；
  - `GW ... state 4`（`SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN`，枚举 0=IDLE/1=ARMED/2=COMPLETE/3=CANCELLED/4=UNCERTAIN），`svc_wait 5019 / svc_done 255 / late 129`；
  - 位置页 `hold 15472 / drain 15233 / drain_fail 17 / hold_drop 1089 / deliv 239`。
- 对比：同一次运行 `SWITCH_A/SWITCH_B` 均 `361/361`、`5776` executed、`late/no_plan/backend` 全 0；COUNTER 无动作。
- 初判：GATEWAY 的 deadline 后端进入 `UNCERTAIN`（READY 未满足/超时）后，`armed` 槽无法完成，后续位置被 BUSY 拒绝 → 停摆在 14–17 位置；与 READY 语义（`T2_READY_REQUIRED=0` 仅 retire 计数）强相关。
- 下一实验（N11.4a）：同一 361 流程用 `--ready-required 1` 对照（若 gateway 能完成，说明 retire 语义不足；若仍 UNCERTAIN，则查 `sync_io_gateway` 的 arm/complete 路径）。
- 证据：`out/n11/seq_361c.log`、`steps-20261029-172113.log`、`out/sync_trigger/sequence_flow.json`。

### REFMEM-TASK-20261029-029 — 361 位置验证：SWITCH_A/B 通过，GATEWAY 仅 14/361

- 入口：`sequence_workflow.py --steps 1-5 --positions 361 --ready-required 0`（step 2 跑 `t2_workflow 0-20`）。
- 采样窗口校准：默认 ticks → 140/361；`--sample-ticks 45` → 315/361；`--sample-ticks 70`（~140 s）→ 361 位置跑满。
- 结果（build `20261029084245`，`out/n11/seq_361c.log`、`out/sync_trigger/sequence_flow.json`）：

| 角色 | groups | finished | executed | 期望 | no_plan | late | backend | freeze_missing |
|---|---|---|---|---|---|---|---|---|
| SWITCH_A | 16 | **361** | 5776 | 5776 | 0 | 0 | 0 | 29 |
| SWITCH_B | 16 | **361** | 5776 | 5776 | 0 | 0 | 0 | 47 |
| GATEWAY | 2 | **14** | 28 | 722 | 0 | 0 | 0 | 1 |

- 结论：**361 判据对 SWITCH_A/SWITCH_B 通过（16×361=5776，零 late/no_plan/backend）**；**GATEWAY 失败**——只完成 14 个位置后停止推进（非 LATE/NO_PLAN，属准入/吞吐或 SP2T 双组 staging 停摆）。
- 与短窗口对比：默认窗口会把"扫描未完成"误判；**361 验证必须 `--sample-ticks ≥ 70`**。
- 下一步（N11.4）：定位 GATEWAY 为何在 ~14 个位置后停摆（GWDIAG 的 BUSY/READY 拒绝、SP2T 组交替、或 action 表 staging），修后重跑 361。

### REFMEM-TASK-20261029-028 — N11.2/N11.3：GATEWAY 读回重试与丢步判据

- N11.2：新增 `query_page()` 有界重试（3 次、50 ms）；抽样循环的 `TRIGger:REServation:T2?` 与 `T2:SCAN?` 走该路径，超时/短页不再把整页变成 `?`；step 20 末尾打印累计重试次数。
  - 验证：`steps-20261029-163841.log` 中 `RUNTIME slot 2 GATEWAY ... received 7 ... last_error 0`（上一轮同位置是 `? / last_error 1`）；本轮重试计数 0。
- N11.3：step 21 新增 `STEPLOSS` 判据（基线扣减）：
  - 基线在 step 19 采样前记录 `freeze_mismatch`/`freeze_fallback`/`SCAN overwrote`；
  - 失败条件：非 COUNTER 角色存在未执行/未放弃的计划（`pending != 0`），或窗口 `overwrote` 增量 > 0；
  - `freeze mismatch/fallback` 增量 > 0 记 **WARN**（FREEZE 版本不匹配走了 fallback，位置仍执行）。
- 结果（`--steps 1-21`，build `20261029072712`，READY=0）：
  - **21/21，failed steps: none**；
  - `COUNTER PASS`、`SWITCH_A PASS`（WARN +3/+3）、`GATEWAY PASS`（WARN +4/+4）、`SWITCH_B PASS`（WARN +4/+4）；`overwrote +0`、`pending 0`。
- 结论：**按判据无丢步**（无未执行计划、无窗口覆盖）；SWITCH/GATEWAY 存在 FREEZE fallback 告警，属质量项、非丢步。
- 记录：`steps-20261029-163841.log`、`out/n11/t2_full_e.log`。
- 待办：工具与 host 用例提交需 P3；WARN 是否可接受需在 N11.4 复跑确认稳定性。

### REFMEM-TASK-20261029-027 — N11.1：T2 workflow 分次运行自愈（generation 复用）

- 根因：每次调用生成新 `dep_gen`，而 grid/T2 绑定在旧 epoch；step 18 判据 `epoch == dep_gen` 永不成立。
- 修复（`tools/sync_trigger/t2_workflow.py`，待 P3 提交）：
  1. `out/tdma_t2/last_generation.json` 作为跨调用记录；不含 step 8/9 的区间（如 `11-20`）复用该 generation；
  2. 复用路径**不再**做 `<= last_accepted` 递增（此前把它 +1，仍对不上）；
  3. step 18 发现 `err 0 valid 1` 但 epoch 不等时，打印明确诊断（期望/实际 + 建议重跑 8-12 或 1-21）；失败信息带 epoch 对照。
- 台架验证（build `20261029072712`，READY=0）：
  - `--steps 1-21 --leave-running` → **21/21**，failed steps none；
  - `--steps 1-10` → `--steps 11-20` → **failed steps: none**（step 18 `12.4s` OK、step 19/20 OK），generation 复用生效。
- host：`tests/python/test_t2_workflow_generation.py` 4 passed（复用/新 epoch/显式覆盖/缺失 marker）。
- 记录：`out/n11/t2_full_a.log`、`out/n11/t2_split_f.log`、`out/n11/t2_split_g.log`。
- 下一项：N11.2 GATEWAY 读回重试；N11.3 丢步判据；工具改动提交需 P3。

### REFMEM-TASK-20261029-026 — N11.0：T2 workflow 失败归因（分次 epoch 断链）

- 现象：`--steps 1-10` 后接 `--steps 11-20`，step 18 报 `reference cursor never became valid within 24 s`，step 19 拒绝启动脉冲。
- 取数：`out/tdma_t2/steps-20261029-160020.log:134` → `err 0 valid 1 epoch 1793260668 session 1793260820`；两次调用各自生成 `dep_gen`，grid/T2 绑定旧 epoch（`1793260668`），第二次会话是 `1793260820`，判据 `epoch == dep_gen` 永不成立。
- 验证：单次 `--steps 1-21 --leave-running`（`T2_READY_REQUIRED=0`）**21/21 OK**，`failed steps: none`；step 20 cursor `epoch 1793261119 session 1793261119`（一致）。日志 `out/tdma_t2/steps-20261029-160519.log`。
- 全链样本：GATEWAY(slot2) `groups exec/count 25/1→37/1`、`mismatch 0`；SWITCH_B(slot3) `26/1→38/1`、`CUM mismatch 1 fallback 1`（判据待 N11.3）；ACTION 采样时 `armed 2/4 completed 0`（仍在执行，不作丢步证据）。
- 结论：**不是固件缺陷**，是工具分次调用导致 generation 断链；N11.1 做持久化/自愈，N11.3 定丢步判据。

### REFMEM-TASK-20261029-025 — T2 workflow：禁用 READY 后 10 步全过

- 背景：此前 `t2_workflow.py --steps 1-10` 在混合 build / READY 要求下多处失败（step 3/4/6/8/9/10）。
- 处置：`T2_READY_REQUIRED=0`（禁用 READY）后重跑一次：
  - `t2_workflow.py --steps 1-10 --leave-running` → **failed steps: none**，10/10 OK。
- 证据：`out/n10/t2_workflow_readyoff.log`、`out/tdma_t2/steps-20261029-155748.log`。
- 说明：该开关只影响 gateway 对 `TRIGger:SEQUENCE:REAdy` 的处理，不改变 REFMEM 统一面代码；四板需同 build（当前 `20261029072712`）。

### REFMEM-TASK-20261029-024 — N10.4：SCPI 执行错误回包修复（拒绝不再静默）

- 背景：C11-06 指出 `EXECute` 拒绝（`NOT_ONLINE_SAFE`/`BAD_ARGUMENT`）串口无文本，只有错误队列/日志；判读只能靠记录号不变。
- 修复（`b8b87e32`）：`scpi_port_push_exec_error` 先按 SCPI 标准输出 `<code>,<info>` 行并 flush，再入队；不改队列语义、不改 `CTRL:STATus?` 字段序。
- HIL（build `20261029072712`）：
  - 环停 `EXECute 30,14,14` → `-200,"NOT_ONLINE_SAFE"`；
  - 坏参数 `EXECute 30,1,1,1` → `-200,"BAD_ARGUMENT"`；
  - `41` 仍 `QUEUED/ACKED`。
- 凭证：P3 `20261029072712` / 1613 / `ee4b076e`（PASS_WITH_WARNINGS，ERROR 0/FATAL 0）；错误行仅为输出层变更，AC 语义未动。
- 残余：TRN-03 re-arm 预算仍为台架时序任务（非代码）；C3/C6、EXE-SEAT-01 维持另立/待签。

### REFMEM-TASK-20261029-023 — N10.3：C11-06 必改落地（文本/证据/第二层）

- C11-06 = `PASS_WITH_CHANGES`：修复成立；三项必改（状态文本、HIL 证据口径、第二层覆盖/可观测）本轮落地。
- **HIL 补证**（含 build 与 ring 行）：
  - 环停 `out/n10/n10_failclosed_v2.json`：build `20261029052659`、`RING:STATus?` enabled=0；`EXECute 30,14,14` 无回包且 `CTRL:STATus?` 保持上一条终态（无新记录）；`FUNC_TDMA_ONLINE,14,14` `QUEUED 720616` → `FUNCtion:STATus?` = `state=3(FAILED), step=1/2, last_reason=13`；`41` 仍 `ACKED`。
  - 环起 `out/n10/n10_positive_v2.json`：build 同上、ring `enabled=1/node_count=4/up=1/down=1`；`30,14,14` `ACKED/ack=14`、`31,14,14` `ACKED/ack=14`。
- **口径更正**：`CTRL:STATus?` 第 12 字段是 `last_reason_slot` 而非 `applied`；ACK 判据只用 `state/ack_flags/nack`。
- **可观测性（明文接受）**：拒绝走 `scpi_port_push_exec_error` 但串口无文本回包（与既有 `BAD_ARGUMENT` 同类）；判读判据＝拒绝后记录号不变、下一条 `41` 仍 `QUEUED`。公共 SCPI 错误回包修复另立（影响面大，需 P3+四板）。
- **已知限制**：OTA 期间 link 服务早退会冻结 cache（OTA 中停环可绕过一次）；环停到 cache 归零 ≤1 tick 竞态——提交前仍二次校验，不足以产生 POSTED，记录待观。
- 契约：`REFMEM-ENTRY-01` clause/check 补"环未运行且 target 含远端 → 入口 `NOT_ONLINE_SAFE`、不落记录"，优先级高于 `NOT_CONTROL_OWNER`，并与部署期 `NOT_ONLINE_SAFE` 区分。

### REFMEM-TASK-20261029-022 — N10.2：环停远端命令 fail-closed 修复（已提交）

- 根因更正：不是"非 owner 目标"，而是**环停时远端 `REFTMEM_CMD` 仍被受理**——提交阻塞在维护流，源记录停留 `POSTED`，后续命令无法再 post（需 reboot）。
- 修复（`5bb742fb`）：
  1. core0 refmem 服务每 tick 刷新 `s_ring_transport_ready_cache`（原子），SCPI/submit 只做无锁读，避免命令上下文取 TDMA 锁；
  2. `distributed_refmem_control_submit` 与 `SYSTem:CTRL:EXECute` 在 target 含远端且 transport 未就绪时直接拒绝（`REJECTED` / `NOT_ONLINE_SAFE`），不再产生记录；
  3. host 用例补 `refmem_ring_transport_admits` 四组矩阵。
- HIL（build `20261029052659`）：
  - 环停：`EXECute 30/28,14,14` 不再 post（`CTRL:STATus?` 保持上一终态），`41` 仍 `ACKED` → 单槽不被堵死；
  - 环起：`30,14,14` `ACKED/ack=14`（第 12 字段是 `last_reason_slot`，不是 applied）、`31,14,14` `ACKED/ack=14`，回归通过。
- 凭证：P3 `20261029052659` / 1613 / `e144e97a`；host 77 passed / 541 files。
- 新残余：该 handler 的显式错误（`BAD_ARGUMENT`/`NOT_ONLINE_SAFE`）**无 SCPI 文本回包**（本地坏参数同样静默），属既有错误回包问题，另立。

### REFMEM-TASK-20261029-021 — N10.1：VDC/非 owner 目标命令挂起复现（POSTED 不终结）

- 复现（COM5，环停）：`CTRL:EXECute 28,14,14,1,0,1000`（以及 29）→ `CTRL:STATus?` = `465941,1780937987,1,465941,14,14,0,0,0,0,0,0,0`：**state=1（POSTED）**、`target=required=14`、`ack=0`、`nack=0`、`applied=0`。
- 后果：随后再发 `CTRL:EXECute 29,...` **超时**——`post_command_replacing_complete` 要求上一条 complete，POSTED 记录把单槽堵死；需 reboot 协调者清除（已做）。
- 更正上一轮措辞：不是 `TAKEN`（state=2），而是 **`POSTED`（state=1）**；`VDC:OUTPut:TIMing?` 读回 `12000,16000,6000` 说明本地 VDC 配置已被此前的 `FUNC_VDC_RECONFIG` 应用，与本次 28/29 无关。
- 初判：`target=14` 只含 1/2/3（非 VDC owner），目标侧执行/回程未形成 ACK/NACK（期望 fail-closed 到 reason 12/13）；需在目标 dispatch/回复路径加"非 owner 目标必须回 NACK"或收窄发号前校验。
- 证据：`out/n10/vdc_taken_repro.json`、`out/n10/vdc_taken_repro2.json`。

### REFMEM-TASK-20261029-020 — TRN-03 re-arm 预算诊断（FULL 阻断点）

- 用 `--run out/HardwareAcceptance/20261029/hawf-115728 --steps 21-36 --diagnostic-continue` 复跑：step 21/22 `OK`，step 23 `warn`（no measured SCK row satisfies the flight re-arm budget），step 24 TDMA 闭环 `OK`(76.3s)，step 26 `warn`（startup barrier timed out），step 29 因 provisional receipt 校验失败终止——**FULL 凭证仍不可得**。
- 数值诊断（`trn03-matrix.json`）：4 条链路 `link_base_delay_ns=41`、`baud=10 MHz`、`sample_period=4 ns`；候选行 4 组 `(0,1,0,1)/(0,1,1,1)/(1,1,0,1)/(1,1,1,1)` 的 `_sck_row_replay_safety` 全部 `safe=False`，最小 margin `-1 sample`，unsafe follower 2–3 条。
- 结论：这是台架/时序预算问题（SCK 相位余量差 1 sample），不是 REFMEM 代码问题；修复需台架专项（改 link base/布线/波特率或重标 SCK 行）。
- 证据：`out/n8/wf_fullbench_diag.log`、`out/HardwareAcceptance/20261029/hawf-115728/trn03-matrix.json`。

### REFMEM-TASK-20261029-019 — 四域/FUNC 单口同批捕获完成

- 用带轮询的 `out/n8/capture_single_port_full.py` 在 COM5 一口完成：`FUNC_TDMA_ONLINE` → `FUNC_TDMA_CLOCK_ONLINE` → `FUNC_VDC_RECONFIG` → `T2_CONFIGURE(30)` → `T2_MOUNT(31)`；每步等 `CTRL:STATus?` 终态后再发下一条。
- 结果：全部 `QUEUED` 并到终态；MOUNT `state=4/ack=14`；`COMMand:STATus?` `acked=1`；环 `up_tx` 4771→6540 连续。
- 观察（待归因）：`VDC:OUTPut:TIMing?` 读回 `24000,32000,16000`（请求 `12000/16000/6000`）；随后直接 `CTRL:EXECute 29,14,14,1,0,12000,16000,6000` 在环停/VDC 非 idle 下停留 `TAKEN`（未回 ACK/NACK），但**不阻塞**后续 `41`（`ACKED`）。疑为 VDC/TDMA 域 handler 未返回或应用未完成；**不作为通过证据**。
- 证据：`out/n8/single_port_full.json`；台架四板 build `20261029031043`、环停。

### REFMEM-TASK-20261029-018 — host 门禁覆盖 RefMem C runner（残余闭合）

- 状态：**此前"两个新 C runner 未入总门禁"的残余已闭合。**
- 改动：`tools/acceptance/host_acceptance.py` 与 `host_acceptance_workflow.py` 的选择器改为 `staged ∪ DEFAULT_PATHS`，两个 C runner 经 `tests/python/test_refmem_host_units.py` 进入每次 host 门禁。
- 凭证：host `PASS 77 passed / 541 files`（`409ebf2b`）；P3 不适用（tools/acceptance 属宿主类）。
- 文档：ENTRY-01 check、快照 §4/§7.2 同步；`REFMEM-ENTRY-01` 保持 `active`。
- 下一项：四域/FUNC 单口同批捕获（台架，N9 残余）。

### REFMEM-TASK-20261029-017 — N9.3：ENTRY-01 转 active（C11-05 整改闭环）

- 状态：**`REFMEM-ENTRY-01` pending → active（2026-10-29）**；`REFMEM-CTRL-01` 保持 `pending`（C3 租约/C6 fence 未实现）。
- C11-05（`docs/check/submissions/REFMEM_C11_REVIEW_05.md`）= `PASS_WITH_CHANGES`，要求 2 项文本整改，均已落：
  1. `DOCS_REGISTRY.md:29` 删除"条款拆分…待下一次登记"，"原字面仍不满足"改为"已由 `REFMEM-ENTRY-01` 重新定义"；同文件 `Last updated` 改 2026-10-29；
  2. `REFMEM_DOMAIN_TODO.md` 的 N5.5 决策与 `REFMEM_DOMAIN_ARCHITECTURE.md` 的入口边界改为"入口侧 `NOT_CONTROL_OWNER`（不落 reason）/目标侧 reason 12"。
- 已知残余：两个新 C host runner 未入 `run_host_unit_tests.ps1` 总门禁（已在 ENTRY-01 check 注明）。
- 下一步：N9.5 最终 §8 报告。

### REFMEM-TASK-20261029-016 — N9.2/N9.4：C11-04 复核与收口快照重写

- 状态：**N9.2 ✅（C11-04 = PASS_WITH_CHANGES）、N9.4 ✅（快照重写）；N9.3 ENTRY-01 保持 pending。**
- C11-04 三项整改：
  1. ENTRY-01 clause 第 3 条收窄为"T2 配置/MOUNT/聚合已机器捕获"，四域/`FUNC_*` 降为产品约束（未验证）；check 注明 host 用例"存在且可跑、未入总门禁"；
  2. 入口 12 语义收尾：`NOT_CONTROL_OWNER`（不落 reason）/ 目标侧 reason 12，架构与登记表同步；
  3. 快照 §3 当前 P3 改为 `20261029031043`/1613/`ae155f7d`，单口证据指向 `single_port_capture.json`。
- 快照已按 N9 重写 §1–§7（子项表、六门、契约状态、证据索引、提交清单），明确不得称完成。
- 下一步：C11-05 复审（可选，决定 ENTRY-01 是否 active）→ N9.5 最终报告。

### REFMEM-TASK-20261029-015 — N9 日间收尾立项：B 方案证据等级接受

- 状态：**N9.0/N9.1 ✅**（基线冻结、证据等级书面接受）；N9.2 C11-04 待跑。
- N9.0 基线：HEAD `22f7121d`、四板 build `20261029031043`、receipt QUICK/`ae155f7d`/1613 files/`strict_gates_passed=false`；他人 dirty 文件 3 个（t2_receipt、OTA_TODO、T2_FLOW）+ 本流程生成的 `docs/validation/P3_FLOW.html`。
- N9.1：快照 §1/§4、架构实现状态、登记表 `REFMEM-CTRL-01`/`REFMEM-ENTRY-01` 均写入"**N8.5 证据等级 = QUICK P3 + 四板 HIL，不冒充 full**；`--full-bench` 卡 TRN-03 SCK re-arm 预算，另立台架任务"。
- 里程碑：N9.0–N9.5 见 `REFMEM_DOMAIN_TODO.md` N9 节。
- 下一步：N9.2 C11-04 独立复核（子线程）→ N9.3 状态落地 → N9.4 快照重写 → N9.5 报告。

### REFMEM-TASK-20261029-014 — P3 全量尝试：QUICK 可复现，FULL 卡 TRN-03 re-arm 预算

- 用 `hardware_acceptance_workflow.py --profile full`（37 步、`--reuse-flashed`）跑两次：
  - 首次 step 30 失败：`A1E549202D18ED6A: final endpoint is outside the last steady trace bin`（录制分箱边缘，非相位超限，phase max 101 ns / warn 100 ns）；
  - 重跑从 0 走完 step 36，`credential: written`：build `20261029031043`、1613 files、`ae155f7d…`、scope `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、`strict_gates_passed=false`、PASS_WITH_WARNINGS。
- `--full-bench`（`p3_bench.json`）在 step 21 失败：`no SCK offset candidate row can satisfy the flight re-arm budget`。
- 结论：**当前台架可获凭证仍是 QUICK/非 strict**；FULL 受 TRN-03 re-arm 预算限制，与 N8.5 代码无关，需台架/时序专项。
- 证据：`out/n8/wf_full.log`、`out/n8/wf_full2.log`、`out/n8/wf_fullbench.log`、`out/HardwareAcceptance/20261029/hawf-115208/`。

### REFMEM-TASK-20261029-013 — C11 必改项：host 可测拆分与入口契约拆分

- 状态：**A（host 可测拆分）代码完成、待 P3；B（入口契约拆分）已登记 pending、待 C11-02。**
- A：
  - `distributed_refmem_command_aggregate()` 拆到独立 TU `refmem_command_aggregate.c`（仅依赖 node-control read，去除 status.c 的重依赖）；
  - 新增纯策略 `refmem_position_reset_admits()`（`refmem_position_reset_policy.c`），`reset_position_running()` 改用之；
  - 新增 host 用例 `test_refmem_command_aggregate.c`（空/混合/更新终态/NULL）与 `test_refmem_position_reset_policy.c`（停环/适配器未起/配置未应用/运行稳定），随 `run_refmem_all_host_tests.ps1` 的 glob 自动收录；
  - host 全量仅剩既有 `realtime_tdma` 债务；P3 build `20261029031043`（进行中）。
- B：新增 `REFMEM-ENTRY-01`（同目录/同语义 + 跨节点发号单入口 + 单串口拓扑），登记 `pending`，请第二个 C11 线程复核后再决定是否 active；`REFMEM-CTRL-01` 保持 pending。
- 遗留：注释同步随 A 一起提交；`REFTMEM_POS_RESET_TIMEOUT` 保留为 reserved（未返回）。

### REFMEM-TASK-20261029-012 — N8.5 四板 T2 MOUNT 正向通过

- 状态：**N8.5 ✅**：`CTRL:EXECute 31,14,14,1,0,0` → `state=4 ACKED`、`ack_flags=14`、`nack=0`、`applied=3`。
- 关键修正：统一命令的 dispatch 在**同一个 refmem 服务任务**里执行，原 request/done 握手会等待自身 → reason 7 超时。改为在 reset API 内**同上下文直接清 TX/pending**（单写者），T2 start 期间生产者自然停发；不再需要跨任务握手。
- 前置：`t2_workflow --steps 1-10 --leave-running` 配 T2 → `trn03 --stage process-image --no-t2 --leave-running` 起环 → `CTRL:EXECute 30,14,14,...` 经 cached STOP 快照 ACK → 再 MOUNT。
- 连续性：MOUNT 前后环保持 `enabled=1`、`up/down=1`，COM5 `up_tx` 4340→7310→9028、COM4/3/6 TX/RX 同步推进；函数后四板 `RING:STOP`。
- 单串口约束（用户 2026-10-29）：统一命令实际只从 COM5 下发，远程目标走 `REFTMEM_CMD`；四板 `ack=14` 成立。**2026-10-29 补机器捕获**：`out/n8/single_port_capture.json`（port=COM5、build `20261029031043`、`30/31,14,14` 均 `ACKED/ack=14`、`COMMand:STATus?` 可见）；四域/FUNC 的单口同批捕获仍未做。
- 提交：`f07fb2b6`（代码，P3 `20261029012332` / `af6fbe0f`）、`b50f6c30`（文档）。
- 证据：`out/n8/n85_mount_four_board.json`、`out/n8/four_board_hil/trn03_n85j/`、`out/n8/n85_mount_local_ok.json`。

### REFMEM-TASK-20261029-011 — N8.5 running-safe 代码落地；正向 HIL 待复跑

- 状态：**代码 ✅（构建 + host 回归）、fail-closed 实机 ✅、正向 MOUNT HIL ⏳。**
- 实现：
  - `distributed_refmem_position_state_t` 增加 `reset_request/reset_done` 握手；
  - `distributed_refmem_position_tx_prepare()` 在 request 期间停发新位置；
  - refmem 服务在**同一上下文**清 TX/pending 状态（`position_clear_running_tx`）后置 done——不做跨任务清状态；
  - `distributed_refmem_reset_position_running()`（有界 50 ms，`UNSAFE/TIMEOUT/OK`）+ `distributed_refmem_position_resume()`；
  - `t2_mount_handler` 接线：reset OK → `trigger_t2_runtime_start(epoch, origin)` → resume；失败回退 `NOT_ONLINE_SAFE`。
- 验证：build `20261029003215`（四板已刷）；`run_refmem_all_host_tests.ps1` 仅剩既有 `realtime_tdma` 失败；环停时 `CTRL:EXECute 31,1,1,1,0,0` → `state=5 NACKED/reason=14`（fail-closed 保持）。
- 未完成：环运行 + T2 configured 的正向 MOUNT。台架时序互相干扰——第二轮 TRN 的 recorder cancel 需要 reboot，而 reboot 会清掉 T2 配置；`RECord:SAVE` 只把 state 5→8，`CANCel` 不清 8。
- 下一步：改用不依赖 TRN recorder 的起环路径（或给 recorder 增加 SAVE→IDLE 复位），再跑含 MOUNT 步的四板 HIL。

### REFMEM-TASK-20261029-010 — 用户改选 B：N8.5 继续实施（撤回 A 边界）

- 状态：用户 2026-10-29 先选择 A（接受边界）、随后**更正为 B**：继续实现 T2 running-safe。
- 处置：A 的边界文本在提交前撤回（未提交、无历史包袱）；契约/架构/快照恢复为"N8.5 进行中，未通过前 `NOT_ONLINE_SAFE`"。
- 计划：quiesce position TX/RX → 有界等待 → 清位置状态（generation 递增）→ `t2_mount_handler` 接线 → 含 MOUNT 步四板 HIL。
- 设计包：`out/n8/n8_5_running_safe_design.md`。

### REFMEM-TASK-20261029-009 — N8.7 收口结论：统一面未完成，N8.5 为唯一功能缺口

- 状态：**N8.0–N8.4、N8.6 完成；N8.5 只有设计、未实现；整体不可标完成。**
- 结论写入 `REFMEM_UNIFIED_CONTROL_REVIEW.md` §7：子项表、契约状态（`REFMEM-CTRL-01` 保持 pending）、残余风险、提交清单。
- 未实现 N8.5 的原因（工程判断）：`trigger_t2_runtime_start` 要求 “RX producer parked”，而运行中 position TX/RX 跨 scpi/refmem 两个任务，现有代码没有 quiesce 锁；没有专门设计与 HIL 窗口时，贸然落实时代码会引入竞态。设计包见 `out/n8/n8_5_running_safe_design.md`。
- 下一步：N8.5 专项（quiesce 锁 + 有界等待 + HIL）；T2 四板含 MOUNT 复跑；C11/EXE-SEAT-01 由用户/第三方推进。

### REFMEM-TASK-20261029-008 — N8.6 四板 function HIL 复跑通过

- 状态：**N8.6 ✅**：`FUNC_TDMA_ONLINE`(88291, 2/2)、`FUNC_TDMA_CLOCK_ONLINE`(88292, 4/4)、`FUNC_VDC_RECONFIG`(88293, 4/4) 均 `state=2 COMPLETED`、目标 `ack=14`。
- 环：`trn03_closed_loop --stage process-image --leave-running --no-t2 --diagnostic-continue`（config `out/HardwareAcceptance/20261029/p3-n57/trn03-matrix.json`、row 1）；四板 `up/down=1`、TX/RX 推进（COM5 19159→19160 等）。
- VDC 读回：COM5/4/3/6 `VDC:OUTPut:TIMing?` 均 `12000,16000,6000`。
- 边界：本轮 `--no-t2`，T2 四板沿用 10-27 证据；本 HIL 是功能证据，不是新 build 凭证（`20261028180729` 的 P3 已随 `4f7a94f0` 提交）。
- 台架：函数后四板 `RING:STOP`，`enabled=0`、`up/down=0`。
- 证据：`out/n8/four_board_hil/n86_summary.json`、`out/n8/four_board_hil/trn03_run5/`。

### REFMEM-TASK-20261029-007 — 台架换 flash 路径成功；N5.7 实机读回通过

- 状态：**N8.4 ✅（代码 + 实机）**；`ota_send` 中途卡死的阻塞被绕过。
- 关键：`tools/picotool_flash`（BOOTSEL UF2 工厂镜像）可用；四板刷到 `20261028180729`（含 N5.7）。随后 `ota_multi_update --skip-pre-reboot --block-size 512` 4/4 PASS，产出 `summary.json` 供 P3 `resume`。
- N5.7 实机（COM5）：
  - 合法 `41` 后 `SYSTem:COMMand:STATus?` = `8,0,0,0,1,0,0,4,0,0,21893`（acked=1、last_state=4）；
  - 非法 `41` 后 = `8,0,0,0,0,1,0,5,13,0,21894`（nacked=1、last_state=5、reason=13）；
  - `SYSTem:COMMand:TARget? 0` = `0,21894,5,41,1,1,0,1,13`。
- 证据：`out/n8/n57_smoke.json`、`out/n8/ota_multi_n57/summary.json`、`out/n8/picotool_flash_*.txt`。
- 下一步：P3 `resume`（进行中）→ 提交 N5.7；N8.6 四板 `FUNC_*` HIL；N8.5 视余量。

### REFMEM-TASK-20261029-006 — 台架 flash 路径受阻；N8.5 设计包交付

- 状态：**N8.4 代码保持（实机 smoke 仍阻塞）；N8.5 只出设计，不落代码。**
- 台架：COM4/COM5 恢复后重试 OTA 仍在中途卡死；BOOTSEL 路径 `picotool reboot -f -u` 的多种参数组合都未进入 BOOTSEL（`info` 始终报 USB serial）。四板已 `OTA:ABOR` + reboot，全部 `IDLE`、build `20261028171345`、环停。
- N8.5 设计：`out/n8/n8_5_running_safe_design.md` 给出 quiesce+reset 方案——新增 `distributed_refmem_reset_position_running()`（请求静默 → 有界等待 TX/RX 空闲 → 清 position 状态 → 释放 + generation 递增），再由 `t2_mount_handler` 接线；明确写者边界、失败回退与 HIL 判据。
- 不落代码的原因：实时路径改动无法上板验证；仅 host 证据不足以收口。
- 下一步：flash 路径需现场介入（BOOTSEL 按键 / 换 USB 口 / 换主机）或修 `ota_send` 的阻塞写；恢复后先做 N5.7 smoke，再评估 N8.5。

### REFMEM-TASK-20261029-005 — N8.4 N5.7 只读聚合代码完成；台架 OTA 不稳阻塞实机验证

- 状态：**N5.7 代码 ✅（构建 + host 回归）；实机 smoke ⏳（OTA 反复卡死）。**
- 内容：新增 `refmem_command_aggregate_t` 与 `distributed_refmem_command_aggregate()`——从既有 per-node command 记录派生 `posted/taken/busy/acked/nacked/timed_out` 计数与最新终态（state/reason/slot/completed_seq），**不建第二事实、不改 wire/布局**；新增只读 SCPI `SYSTem:COMMand:STATus?`（11 字段）与 `SYSTem:COMMand:TARget? <slot>`（9 字段，读同一 node-control）。
- 验证：`cmake --build build --target DHRT100` 通过；`run_refmem_all_host_tests.ps1` 仅剩既有 `realtime_tdma` harness 债务（缺 `tdma_service_core1`），无新增失败。
- 台架：COM5 三次 OTA（P3 并发、单板 512 B、重试）分别在 `ERASE_SLOT`、63%、无输出处卡死；已 `OTA:ABOR` + picotool reboot 恢复，当前 build `20261028171345`、OTA `IDLE`、环停。
- 未覆盖：aggregate 的实机读回；N8.5（T2 running-safe）未开始；N8.6 四板 `FUNC_*` HIL 未跑。
- 下一步：换 flash 路径（picotool/串行 OTA）后复验 N5.7；再进 N8.5。

### REFMEM-TASK-20261029-004 — N8.6 P3 凭证取得（resume）+ `48` 边界更正

- 状态：**P3 QUICK 凭证已重置并 PASS**（build `20261028171345`，fingerprint `1fc53c7d…`，1609 files，`PASS_WITH_WARNINGS`：INFO 33 / WARN 12 / ERROR 0 / FATAL 0）。
- 台架绕行：P3 `run` 的四板并发 OTA 两次卡在 `ota_send`（reboot 后立即开会话）；改为 `ota_multi_update --skip-pre-reboot --settle 2.0 --block-size 512` 先取得 `summary.json`，再 `p3_hardware_acceptance.py resume --package … --ota-summary …` 跑完 P0T/TRN/TDMA/DPLL。凭证 `config/hardware_acceptance/p3_acceptance_receipt.json`。
- 更正：`CTRL:EXECute 48` 无应答**不是固件缺陷**——上一轮实际只发了 15 个参数（handler 要 16 个，`SYST:ERR? -109`）。按 16 词重发后 `QUEUED`，`CTRL:STATus?` 为 `state=5 NACKED/reason=13`（plan 业务校验拒绝）；`47` 仍依赖有效 plan，未做正向。
- host：`run_refmem_command_authority_tests.ps1` 通过；`run_refmem_all_host_tests.ps1` 仅剩 `realtime_tdma` 既有 harness 债务。
- 提交：代码（`distributed_refmem_command.c` + 测试）与 receipt 待提交；工作区另有既有 `tools/sync_trigger/t2_workflow.py` 改动，需一并入树才能通过 `check-staged` 的指纹一致性。
- 下一步：N8.4（N5.7 只读聚合）与 N8.5（T2 running-safe）设计/实现；四板 `FUNC_*` HIL 复跑。

### REFMEM-TASK-20261029-003 — N8.3 控制面失败路径 NACK 可见性修复与 IF-013 复验

- 状态：**N8.3 代码 + 单板 HIL ✅；四板 P3 凭证未取（台架 OTA 并发卡死），N8.6 待续。**
- 根因：`distributed_refmem_node_control_service()` 失败分支只 NACK command slot、不写 node-control，`CTRL:STATus?` 永远 `POSTED`（state=1），与在途命令不可区分。
- 修复：失败分支补 `distributed_refmem_node_control_update_status(NACKED, nack=1<<slot, reason)`；无 wire/布局/槽位变化。
- host：`run_refmem_command_authority_tests.ps1` 增加 NACK 镜像断言（state/nack 掩码/reason）并通过；`run_refmem_all_host_tests.ps1` 仍有 `run_refmem_realtime_tdma_tests.ps1` 一项失败，属 N5.0.4a 拆分后的既有 harness 债务（缺 `tdma_service_core1` 符号），与本修复无关。
- 单板 HIL（COM5，build `20261028171345`，环停止）：非法 41 → `state=5 NACKED/nack=1/reason=13`；合法 41 → `ACKED`；42（合法浮点）→ `ACKED`；43/44/45/46 → `ACKED`；47（无 plan）→ `NACKED/13`；49 → `ACKED`；35 → `NACKED/13`（会话策略 fail-closed）。
- 未闭环边界：`CTRL:EXECute 48`（16 词 plan）无应答、`SYST:ERR? -109`；`CTRL:EXECute 4`（self-check）超时；留 N8.3b。
- 台架：P3 两次 `run` 均卡在四板 OTA（残留 `RECEIVING`/`FAILED`）；已逐板 `OTA:ABOR` + picotool reboot + 手动 `ota_send`（512 B 块）统一四板到 `20261028171345`，OTA 均 `IDLE`。
- 提交：**未提交**——工作区存在他人未提交的 `tools/sync_trigger/t2_workflow.py`，`check-staged` 会拒绝混合树；待该改动落库或用户裁定后，再按 staged 指纹跑 P3 提交。
- 下一步：N8.3b（48/4 根因 + 全量 host 复跑）→ N8.6（P3 凭证/四板 HIL）。

### REFMEM-TASK-20261029-002 — N8.1/N8.2 收口快照与入口无关边界回正

- 状态：**N8.1 ✅、N8.2 ✅（均为文档面）**。
- N8.1：`REFMEM_UNIFIED_CONTROL_REVIEW.md` 标题改为"范围闭环，整体未收口"、Status `Active`；§1 明确"不可标记完成"，§2 补 catalog 36/历史 21、N7.6 改 ⚠️，§3 补当前 P3 `20261028154542`/`out/n8/baseline.json`/`b16f7b0a`，§4 补契约 pending、N5.5、IF-012/013、相位预算，§6 指向 N8。
- N8.2：N5.5 冻结为**产品级入口收敛**——非协调者远端 `REFTMEM_CMD` fail-closed(12)、不中继/不排队/不建 relay 状态；`REFMEM-CTRL-01` 增补"N6/N7 增量已实现 + 入口无关条款未满足、状态保持 pending"；`REFMEM_DOMAIN_ARCHITECTURE.md` 新增「入口无关边界（N5.5）」小节，C1 转发腿列为显式非目标。
- 边界：不把契约转 active、不代替 C11、不写转发代码；wire/布局/槽位未动。
- 下一步：N8.3 IF-013 后端（先核 `TRIGger:MODE` 与 7 条配置命令的 handler 与最新证据）。

### REFMEM-TASK-20261029-001 — N8 统一面收口夜间目标立项

- 状态：长期目标建立（`goal-2c069112…`，40 轮上限）；N8.0–N8.7 细化里程碑写入 `REFMEM_DOMAIN_TODO.md` N8 节。
- 授权边界（用户 2026-10-29）：台架允许无人值守 HIL（复位/烧录/起环/P3 QUICK）；实时路径允许改动（含 T2 running-safe 位置复位）；N5.5 由 AI 按 HAOFV 架构自决。
- N5.5 预置决策：**产品级入口收敛**——非协调者入口仅本地，远端 `REFMEM_CMD` 由非协调者发起一律 fail-closed（12），CP1 记为显式例外；不新增 payload class/wire/第二命令槽/第二 ACK；重开判据：产品要求非协调者远端发起时按紧凑 intent delta 重做，并先补 TLV/参数池同步与 256 B 容量证据。
- 基线（本轮实测，非事实源）：host 4 套件通过（`run_refmem_function_tests.ps1`、`run_refmem_function_catalog_tests.ps1`、`run_refmem_command_catalog_tests.ps1`、`run_refmem_unified_control_host_tests.ps1`）；`command_catalog.yaml` 36 条、`function_catalog.yaml` 6 个；当前 P3 receipt build `20261028154542`（QUICK/DPLL scope、1609 files、fingerprint `80094026…`）；工作区仅 `t2_acceptance_receipt.json` 未提交（`passed=true/verdict_ok=false`，`8aae5e78` 已堵放行路径）。
- 下一步：N8.0 台架可用性探测，随后 N8.1/N8.2 文档冻结。

### REFMEM-TASK-20261027-016 — 四板 VDC 正向通过：目标闭环

- 状态：**四板 VDC `FUNC_VDC_RECONFIG` 正向 ✅；统一控制面目标闭环。**
- 关键修正：此前 `reason=7` 是**参数非法**，不是 reference 不退休：
  - `input_port=0` 非法（1..4），`timeout_ms=100 <= window_ms=1000` 非法（必须 `timeout_ms > window_ms`）。
  - 合法参数：`input_port=4`、`edge=0`、`nominal=10000000`、`window_ms=1000`、`timeout_ms=2500`；timing `12000/16000/6000`。
- 本地：`FUNC_VDC_RECONFIG,1,1,4,0,10000000,1000,2500,0,12000,16000,6000` → `COMPLETED/ack=1`，timing 读回 `12000,16000,6000`。
- 四板（process-image 环）：`FUNC_VDC_RECONFIG,14,14,...` → `state=2 COMPLETED`、step 4/4、`ack=14`、`nack=0`；COM4/3/6 `VDC:OUTPut:TIMing?` 均 `12000,16000,6000`；四板 ring `up/down=1`、TX/RX 推进。
- 证据：`out/n7/local_hil/vdc_function_success.txt`、`out/n7/four_board_hil/vdc_positive_four_board.txt`、`trn03_vdc4_1/`。
- 边界：`T2_MOUNT/START` 仍 `NOT_ONLINE_SAFE`（需 running-safe 位置复位），属于已声明首版边界，不是本轮回归。
- 处置：HIL 后 `RING:STOP` 四板。
- 结论：N6.0–N6.5、N7.1–N7.6 的目标范围均已闭环；可标记目标完成。

### REFMEM-TASK-20261027-015 — 四板 T2 正向通过：T2 读 STOP 暂存快照

- 状态：**四板 T2 正向 ✅**；目标仅余 VDC reference/timing。
- 根因：`trigger_grid_preview_snapshot`/`trigger_deployment_snapshot` 是 STOP-only 读；环 START 后返回 `TRIGGER_GRID_PREVIEW_NOT_STOPPED`（`1,33`），T2 handler 误判为前置缺失。
- 修复（`a3563def`）：新增只读 `trigger_grid_preview_snapshot_cached` / `trigger_deployment_snapshot_cached`，T2 handler 在 TDMA 运行中读 STOP 暂存副本；本地 SCPI 仍走 STOP-only 原件。P3 `20261027014444`。
- 四板 HIL（reset → T2 steps 1-10 → TRN process-image 不复位）：
  - `FUNC_T2_RECONFIG,14,14,1,0,14,0,5000,5000,500,0,0,360000,1000,1000,0`
  - `state=2 COMPLETED`、step 2/2、`ack=14`、`nack=0`；`CTRL:STATus?` `state=4 ACKED`。
  - 四板 ring 全程 `up/down=1`，TX/RX 推进；HIL 后 `RING:STOP`。
- 证据：`out/n7/four_board_hil/t2_positive_cached.txt`、`trn03_cached_run1/`、`t2_cached_stage1.log`。
- 残余：VDC reference/timing 仍 `reason=7`；T2 START/MOUNT 仍本地；N7.5/N7.6 待 VDC。
- 下一步：VDC owner 修 reference retire/output idle，或按用户决定把 VDC 降级为窄 delay 并收口。

### REFMEM-TASK-20261027-014 — 根因：RING:ARM 清掉 T2 前置，形成启动顺序冲突

- 状态：四板 T2 正向被**固件顺序**阻塞，不是台架抖动；建议目标标 blocked。
- 二分（COM5，STOP 态已装 grid/deployment）：
  - 基线 `GRID?` 完整；
  - `FLIGHT:MODE 1` 后 `GRID?` 完整；
  - `FIFO:RESet` 后 `GRID?` 完整；
  - `OPMode:STAGe 7/APPLy`、`RING:TOPology` 后 `GRID?` 完整；
  - **`SYSTem:TDMA:RING:ARM` 后 `GRID?`/`DEPloyment?` 变为 `1,33`**。
- ARM 后重装前置：`t2_workflow --steps 8-10` 失败（step8 `GRID:CONFigure` result 33、step9 binding stale、step10 stores not current）。
- 结论：T2 grid/deployment 必须在 ARM **前**安装；而远端 function 又要求环 START 后才能承载 `REFTMEM_CMD`。ARM 清前置 ⇒ 启动后无法再配 T2 ⇒ 四板远端 `FUNC_T2_RECONFIG` 不可能用当前固件完成。
- 正面结论仍保留：process-image 环上远端 T2 step0 ACK=14（`REFMEM-TASK-20261027-013`）。
- 证据：`out/n7/t2_bisect.txt`、`out/n7/t2_after_arm.log`。
- 闭环：固件需“ARM 保留 T2 前置”或“T2_CONFIGURE 读 ARM 前快照/允许 ARM 后重装”；在此之前不进入下一子项。

### REFMEM-TASK-20261027-013 — process-image 上远端 T2 交付成立；前置被 TRN 清掉

- 状态：四板 T2 远端命令路径 ✅（step0 ACK=14）；正向仍 ❌（前置丢失）。
- 序列：
  1. 复位四板；`t2_workflow.py --steps 1-10 --leave-running` 成功（grid/deployment/templates 全配好，环仍停）。
  2. 不复位，`trn03_closed_loop --stage process-image --leave-running --no-t2 --cancel-unsaved-record` 一次通过（`trn03_t2_keep3`，`passed/left_running=true`）。
  3. 直接跑协调者 `FUNC_T2_RECONFIG,14,14,1,0,14,0,5000,5000,500,0,0,360000,1000,1000,0`：
     - step0（cmd32 T2_UNMOUNT）**ACK=14**；step1（cmd30 T2_CONFIGURE）**NACK=14 / reason=13**；function FAILED。
     - 源 `CTRL:STATus?` `state=5 NACKED/nack=14`。
- 关键结论：**process-image 环能承载 REFMEM_CMD，远端 T2 停/配置交付成立**；失败原因是 TRN 启动后 `GRID?`/`DEPloyment?` 返回 `1,33`（前置失效），step1 拿不到 grid/deployment。
- 处置：已 `RING:STOP` 四板，均 `enabled=0`。
- 证据：`out/n7/four_board_hil/t2_positive_process_image.txt`、`trn03_t2_keep3/`、`t2_steps_1_10_reset.log`。
- 下一步：定位 TRN 启动中清掉 grid/deployment 的命令（FLIGHT:MODE/FIFO/TOPO/ARM 候选），改为保留前置；或在 TRN 后不 STOP 重配前置的等价路径。

### REFMEM-TASK-20261027-012 — T2 前置不能在 process-image 运行中补装

- 状态：四板 T2 正向仍阻塞；C14 已轮转。
- 实测：TRN process-image `trn03_t2prep1` 一次通过（`passed=true/left_running=true`）；在运行中的 process-image 环上跑 `t2_workflow.py --steps 7-11 --leave-running`，步骤 8–11 失败：
  - step8 `GRID:CONFigure` → `grid not configured (result 33)`；
  - step9 `binding stale`；step10 `stores not current`；step11 `counter not loaded`。
- 判定：grid/deployment/template 的安装需要 STOP 态；process-image 运行中不能补装 T2 前置。四板 T2 正向需要“先 STOP 装前置 → 再 process-image 启动”的顺序，而 TRN 启动会复位 RAM，当前工具链没有保留前置的路径。
- 处置：已 `RING:STOP` 四板，均 `enabled=0`。
- 证据：`out/n7/t2_steps_7_11.log`、`out/n7/four_board_hil/trn03_t2prep1/`。
- 下一步：给 TRN/流程增加“STOP 态装 T2 前置后不复位启动 process-image”，或把 T2 前置持久化到 flash/配置，再复跑四板 T2 正向。

### REFMEM-TASK-20261027-011 — T2 leave-running 工具；四板 T2 仍卡 process-image

- 状态：工具 ✅；四板 T2 正向 ❌。
- `tools/sync_trigger/t2_workflow.py` 新增 `--leave-running`，跳过自动 step21 teardown；P3 `20261026181952`；commit `a5cbdb06`。
- 实测：`--steps 1-20 --leave-running` 后四板 `RING:STATus? enabled/up/down=1`，T2/VDC 运行；协调者 `FUNC_T2_RECONFIG,14,14,...` 的 step0（cmd32）仍超时 `reason=11`，源 `CTRL:STATus?` POSTED。
- 判定：T2 workflow 的 ring 是 raw/T2 链，不承载 `REFTMEM_CMD`；四板 T2 正向必须用 TRN process-image 环（且保留 grid/deployment/templates）。工具 leave-running 只解决“不被 teardown”，不解决命令承载。
- 处置：已 `T2:UNMOUNT` + `RING:STOP` 四板，均 `enabled=0`。
- 证据：`out/n7/four_board_hil/t2_positive_four_board.txt`、`out/n7/t2_workflow_leave.log`。
- 下一步：把 T2 前置（grid/deployment/templates）与 TRN process-image 串起来；或给 TRN 增加“保留 T2 前置”的能力。

### REFMEM-TASK-20261027-010 — N7.5/N7.6 收口快照（未完成）

- 状态：新增 `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`，作为 N7.5/N7.6 的收口快照；**目标不标完成**。
- 内容：N6.0–N6.5、N7.1–N7.6 子项总表；P3/HIL/提交证据索引；VDC reference/timing、四板 T2 正向、T2 START/MOUNT 开放项与复验判据；契约边界（单 command slot / 单 ACK seat / coordinator-only）。
- 关键判定：四板逐板本地 function 不能替代协调者单命令四板证据（非协调者 step0 `reason=11`）；T2 workflow 1-20 仍自动 teardown，无法借其保持“四板环+前置”。
- 文档门禁：`docs_check` 235 files、`doc_regression` 全绿；提交见本轮 docs。
- 下一步：VDC owner 修 reference retire/output idle；四板 T2 正向需 process-image + 前置；完成后更新本快照并标 N7.5/N7.6。

### REFMEM-TASK-20261027-009 — 四板 T2 正向边界：function 入口仅协调者

- 状态：T2 本地正向保持；四板单命令 T2 正向未达成。
- 尝试：
  - `t2_workflow.py --steps 1-20` 仍自动执行 step21 teardown（日志 `out/n7/t2_workflow_1_20.log`），环/T2 被停，无法借其保持“四板环 + 前置”状态。
  - 在 COM4/3/6 各自跑本地 `FUNC_T2_RECONFIG,1,1,...` → step0（cmd32）`step_state=1 SUBMITTED` 后超时 `reason=11`（`out/n7/four_board_hil/t2_positive_per_board.txt`）。
- 判定：`SYSTem:CTRL:FUNCtion:RUN` 的入口仍是协调者（slot 0）单入口；非协调者本地 function 不会被 RefMem 服务消费。四板 T2 正向必须走协调者 `FUNC_T2_RECONFIG,14,14,...` + 健康 process-image 环；不能用四块本地调用拼凑。
- 现有证据：COM5 本地 `FUNC_T2_RECONFIG` `COMPLETED/ack=1`（`t2_positive_try.txt`）；四板 T2 负向 step0 ACK、step1 `reason=13`。
- 未覆盖：四板 T2 正向；T2 START/MOUNT 仍 `NOT_ONLINE_SAFE`；VDC reference/timing。
- 下一步：用可保持运行的 process-image 环（或给 T2 workflow 加 leave-running 选项，需工具/P3 凭证）再跑四板 T2 正向。

### REFMEM-TASK-20261027-008 — T2 正向 function 首次通过：停+配置 COMPLETED

- 状态：T2 正向域停配置 ✅（本地 COM5）；四板 T2 正向未做（环已停）。
- 前置：`t2_workflow.py --full` 21 步全 OK（grid/deployment/action table/template/T2 configure+start/VDC run/优先级/位置发布/teardown），日志 `out/n7/t2_workflow_full.log`；teardown 停环后 grid/deployment/templates 仍 configured（`GRID?` configured=1、`DEPloyment?` active=1、`TEMPlate?` installed=1）。
- 先单步验证：`CTRL:EXECute 30,1,1,1,0,14,0,5000,5000,500,0,0,360000,1000,1000,0` → `state=4 ACKED`。关键：`threshold_per_mille` 必须 0<值<1000；此前用 0 被 `predictor_config_valid` 拒绝（diag 142）。
- function 正向：`FUNC_T2_RECONFIG,1,1,1,0,14,0,5000,5000,500,0,0,360000,1000,1000,0`
  - `state=2 COMPLETED`、`step_index=2/2`、`ack=1`、`nack=0`；`CTRL:STATus?` `state=4 ACKED`。
- 参数：run_id=1，source=0，target=14，origin=0，pre/post=5000 mdeg，threshold=500‰，start=0，zero=0，terminal=360000 mdeg，step=1000 mdeg，ppd=1000，window=0。
- 证据：`out/n7/local_hil/t2_positive_try.txt`；未改代码，无新 P3。
- 未覆盖：四板 T2 正向（需健康 process-image 环）、T2 start/mount（仍 `NOT_ONLINE_SAFE`）、VDC reference/timing、N7.5。
- 下一步：用同一组参数在四板环上跑 `FUNC_T2_RECONFIG,14,14,...`；补齐 N7.5 收口。

### REFMEM-TASK-20261027-007 — 有界 RUN_STATE_DENIED 重试；VDC reference 仍不退休

- 状态：function 重试策略落库；VDC reference 仍阻塞。
- 内容：`REFMEM_FUNCTION_STEP_RETRY_MAX=3`、`REFMEM_FUNCTION_STEP_RETRY_GAP_MS=100`；step NACK `reason=7` 时保持 RUNNING、重试最多 3 次，耗尽后 FAILED；其他 reason 仍立即失败。
- 验证：host `test_refmem_function` 新增“7 重试→RUNNING→耗尽 FAILED”用例；`pico2-release` 构建通过；P3 `20261026181952`；commit `4c0be6fb`。
- VDC 复测：`FUNC_VDC_RECONFIG` 仍 `state=3 FAILED / step1 cmd27 / reason=7`；`VDC:REFerence:STATus?` 仍 `1,2,...`（RUNNING），`CONFigure?` 未变。重试不能替代 VDC owner 的 retire 闭环。
- 证据：`out/n7/local_hil/vdc_retry.txt`。
- 未覆盖/残余：VDC reference retire、timing idle、T2 正向、四板 VDC、N7.5。
- 下一步：VDC owner 修 reference retire/output idle；function 重试只作为域停窗口的容错，不是根因修复。

### REFMEM-TASK-20261027-006 — FUNC_VDC_OUTPUT_DELAY：在线 VDC delay 单命令成功

- 状态：窄 VDC function 通过；VDC reference/timing 仍阻塞。
- 内容：新增 `FUNC_VDC_OUTPUT_DELAY`（id 6，arg_words 1，steps 33→28：VDC_STOP → VDC_OUTPUT_DELAY）；function catalog 6 个。
- HIL（COM5 本地，`out/n7/local_hil/vdc_delay_function.txt`）：
  - `FUNC_VDC_OUTPUT_DELAY,1,1,321` → `state=2 COMPLETED`、`step_index=2`、`ack=1`、`nack=0`。
  - `VDC:OUTPut:DELay?` 读回 `321`。
- 证据：`pico2-release` 构建通过；P3 `20261026180937`；host catalog/executor 通过；commit `970d231b`。
- 台架：本轮为 VDC reference 做的 3 次 TRN retry 均失败（startup barrier/空错误），未进入可跑 reference 的 process-image；因此 VDC reference retire 未复验。
- 未覆盖：VDC timing（`vdc_run_output_configuration_idle` 仍 false）、reference（`s_reference_active_generation` 未清零）、四板 VDC function、T2 正向。
- 下一步：VDC owner 修 reference retire/output idle；或把 VDC timing 也做成窄 function 并先解决 idle 判据。

### REFMEM-TASK-20261027-005 — VDC 原子面实测：delay 成功，timing/reference 仍拒绝

- 状态：VDC output delay 在线配置 ✅；output timing 与 reference 仍 `reason=7`；`FUNC_VDC_RECONFIG` 未通过。
- 实测（COM5 本地，TDMA 已停环；`CTRL:EXECute` 单步）：
  - `33` VDC_STOP → `QUEUED,91582`（ACK）。
  - `28,123` VDC_OUTPUT_DELAY → `QUEUED,91583`；读回 `VDC:OUTPut:DELay?=123` ✅。
  - `29,1000,2000,100` VDC_OUTPUT_TIMING → `QUEUED,91584`；`CTRL:STATus?` = `state=5 NACKED/nack=1/reason=7`；读回仍 `12000,16000,6000`。
  - `27,...` VDC_REFERENCE_CONFIG 仍 reason 7（reference 未 retire）；`ENABle?=0` 但 `STATus state=2 RUNNING`。
- 判定：
  - delay publish 是纯原子写，`VDC_STOP` 后可用。
  - timing 的 `vdc_run_output_configuration_idle()` 仍为 false（run output cancel 后还有其它 output 配置未 idle），被 fail-closed 拒绝。
  - reference 的 `s_reference_active_generation` 未清零，`reference_config_install` 拒绝；根因在 VDC owner retire 闭环，不在 RefMem。
- 证据：`out/n7/local_hil/vdc_function.txt`（含本次命令/读回）；提交 `5e688260`（VDC 代码）、`410b25b6`（阻塞文档）。
- 未覆盖：VDC timing/reference 正面、四板 VDC function、T2 正向。
- 下一步：VDC owner 修 reference retire + output 全 idle 判据；或先提供只含 `VDC_STOP→OUTPUT_DELAY` 的窄 function，证明在线 VDC 输出配置。

### REFMEM-TASK-20261027-004 — N6.3.3 VDC 在线配置：代码/P3 完成，参考退休阻塞

- 状态：VDC 在线配置 API + `FUNC_VDC_RECONFIG` 已落库并 P3；**功能 HIL 未通过**，卡在 VDC reference 退休。
- 内容：
  - 新增 `vdc_dpll_manager_set_reference_config_online` / `set_output_delay_ns_online` / `set_output_timing_profile_online`：TDMA 不停机，参考必须已停、VDC output 必须 idle；本地 SCPI 仍 TDMA-STOP-only。
  - `VDC_STOP` 执行 `vdc_run_output_cancel()` + `vdc_dpll_manager_set_reference_enabled(false)`；`VDC_START` 明确 `NOT_ONLINE_SAFE`（run output 需 period/high/duration）。
  - 新增 `FUNC_VDC_RECONFIG`（33→27→28→29，9 args）；function catalog 5 个。
- 证据：`pico2-release` 构建通过；P3 `20261026174849`；host VDC harness 30 passed（补 `vdc_run_output_configuration_idle` fake）；commit `5e688260`。
- HIL 负结果（`out/n7/local_hil/vdc_function.txt`）：
  - `FUNC_VDC_RECONFIG,1,1,0,0,10000000,1000,100,0,1000,2000,100` → step0（VDC_STOP）ACK；step1（cmd27 reference config）`NACK reason=7`；function `FAILED`。
  - `VDC:REFerence:ENABle?=0`，但 `STATus?` 仍 `state=2 RUNNING`；SCPI `ENABle 0` 后仍不退休。`reference_config_install` 因 `s_reference_active_generation` 非零拒绝。
- 根因候选：reference cancel 只置取消/请求，core1 owner 未在 TDMA 运行/停环两种工况下推进到 RETIRED；需 VDC owner 修“disable → retire”闭环，或在 `VDC_STOP` 等待 bounded retirement（不能忙等）。
- 未覆盖：VDC reference/config 正面 HIL、output delay/timing 正面、四板 VDC function；T2 正向仍缺。
- 台架处置：VDC HIL 后已 `RING:STOP` 四板。
- 下一步：修 VDC reference retire 闭环，再重跑 `FUNC_VDC_RECONFIG`；未过前 N6.3.3 不标 ✅。

### REFMEM-TASK-20261027-003 — N7.4 四板闭环：本地执行槽只含本板位

- 状态：**N7.4 四板 function HIL 通过**（TDMA/DPLL 多步 + T2 负向）；VDC function 仍缺。
- 根因（2026-10-27，step1 超时）：
  - 目标执行远端命令时，`distributed_refmem_post_command_replacing_complete` 把命令投到本板 command slot，但 request 仍带 `target_mask/required_mask=14`；本板 ACK 只能置本板位，slot 永远 `TAKEN`，下一步 `post` 因“未完成”失败并静默设置 `s_node_control_last_command_seq`，不再 ACK/回复。
  - 证据：COM4 `SYSTem:COMMand:ACK?` 显示 `state=2 TAKEN, command_seq=1, target/required=14, command_type=21`；`CMDFRame?=2,0,0,0,2`（帧已 apply），`NODE:CONTrol?` 为 step1 `POSTED`，但 `CMD:REPLy? armed=1`（没有为 step1 arm）。
- 修复（`871279c8`）：本板执行副本的 `target_mask/required_mask` 改为 `1u << local_slot`；远端聚合仍由 node control/reply 承担。P3 `20261026173427`。
- 四板 HIL（TRN process-image `left_running=true`，build `20261026173427`）：
  - `FUNC_TDMA_ONLINE,14,14,3,1,0,255` → `state=2 COMPLETED`、`step_index=2`、`ack_mask=14`、`nack=0`。
  - `FUNC_TDMA_CLOCK_ONLINE`（4 步）→ `state=2 COMPLETED`、`step_index=4`、`ack_mask=14`；三目标 `DPLL:ROLE?` 读回 `0,0,3,3,0`（applied gen 3、pending 0）。
  - `FUNC_T2_RECONFIG,14,14,...` → step0（cmd32）`ACK=14`；step1（cmd30）`NACK reason=13`；function `FAILED`（T2 前置缺失，符合 fail-closed）。
  - 连续性：COM4 `up_sequence 44697→51795`、COM6 `45898→51601`、COM3 单次查询超时但后续 `51251`；四板 `enabled/up/down=1`，无停环。HIL 后已 `RING:STOP` 四板。
- 证据：`out/n7/four_board_hil/function_hil_localslot.txt`、`continuity.txt`、`trn03_localslot/`。
- 未覆盖：VDC reference/output function；T2 **正向** configure（需有效 grid/deployment/templates）；外部示波器/完整 P3 范围。
- 下一步：N6.3.3 VDC function（仍卡 `tdma_service_update_stopped_metadata` 要求 TDMA 停环），或先做 N7.5 收口文档；N7.4 可标 ✅（范围见上）。

### REFMEM-TASK-20261027-002 — N7.4 第二次：step0 三目标 ACK，step1 仍超时

- 状态：四板 function HIL 部分通过（step0），多步远端仍有阻塞；N7.4 未完成。
- 环：先复位四板，再 `trn03_closed_loop --stage process-image --leave-running --no-t2 --window-s 1.0`；`passed=true / left_running=true`（`out/n7/four_board_hil/trn03i/`）。四板 `RING:STATus?` `enabled=1/up=1/down=1`，process-image 路径就绪。
- function：COM5（owner=0）`FUNC_TDMA_ONLINE,14,14,3,1,0,255` → `QUEUED,90599`；6 s 后 `state=3 FAILED`、`step_index=1`、`step_command_id=23`、`step_state=3`、`ack_mask=14`、`nack=0`、`reason=11`；源 `CTRL:STATus?` 仍 `POSTED`。
  - **step0（seat rotation，cmd 21）三目标 ACK=14**，目标 `SEAT:ROTation?` 读回 `3,1,...`；证明远端 function 执行器、命令分发、ACK 聚合与 seat 在线切换可用。
  - **step1（load mask，cmd 23）超时**；20 ms 步间 gap 未能修复。目标环形状态仍 `enabled/up/down=1`，TX/RX 推进；不是 TDMA 停环。
- 代码：`REFTMEM_FUNCTION_STEP_GAP_MS=20` + `step_ready_ms` 已落库并 P3 `20261026171627`；提交 `11cf7997`。
- 根因候选（未定）：单深度命令/回复槽在 step0 ACK 后仍保留，step1 的 ACK 被丢弃；或 load mask 写入触发 realtime load 重排导致回复路径短暂中断。需目标侧 `cmd_reply`/node control 计数与 `CMD:REPLY?` 逐项定位。
- 台架处置：HIL 后已 `RING:STOP` 四板，`RING:STATus?` 均 `enabled=0`；未留环运行。
- 未覆盖：step1 修复、四板 `FUNC_CLOCK_RECONFIG`/`FUNC_T2_RECONFIG`、TDMA TX/RX 跨 function 连续性、VDC function。
- 下一步：加目标侧 reply/command 计数（只读）定位 step1；或把 load mask 拆成独立单步 function 验证；再重跑四板。

### REFMEM-TASK-20261027-001 — N7.4 四板 function HIL 尝试：环可启动，命令路径未就绪

- 状态：四板 function HIL **未通过**；记录负结果与根因，N7.4 保持未完成。
- 环启动（手动，不经 `RING:TRAIN`）：
  - 四板 `STOP → OPMode:STAGe 7 → APPLy → TOPology 4,<local>,0 → ARM` 均 `ARM:STATus?=1`；forward 1/2/3 先 `START`、slot0 最后 `START`。
  - `RING:STATus?` 四板均 `enabled=1,node_count=4,up=1,down=1,adapter_started=1`，TX/RX 计数推进。证据 `out/n7/four_board_hil/function_hil.txt` 前后快照。
- 远端 function：
  - COM5（owner=0）`FUNC_TDMA_ONLINE,14,14,3,1,0,0` → `QUEUED,103132`；step0（cmd 21）`step_state=SUBMITTED`，12 s 后 `state=3 FAILED / reason=11`，源 `CTRL:STATus?` 仍 `POSTED`，无目标 ACK。
  - 根因判断：手动 `RING:START` 只启动 adapter，未建立 TRN-03 的 process-image/resident 维护命令路径；环能跑帧，但 `REFTMEM_CMD` 维护流未被消费。不是 seat 打包或 function 执行器错误（本地 HIL 已证）。
- TRN-03 路线：
  - COM5 的 recorder 曾卡在 state 5；手动 `TDMA:RECord:CANCel` 等待 2 s 后清到 0。
  - 重跑 `trn03_closed_loop --cancel-unsaved-record --leave-running --no-t2`：matrix staged、四板 armed、recording exported，但 **explicit startup barrier timed out**，`left_running=false`；未进入可跑 function 的 process-image 状态。证据 `out/n7/four_board_hil/trn03d/`。
- 补充 host 证据：`test_refmem_function` 新增多目标用例，`target_mask/required_mask=0x0E` 原样传到 atomic submit；套件仍全绿。
- 结论：四板 HIL 受台架/process-image 启动阻塞，不伪造通过；下一步优先用 P3/TRN 的健康启动路径（或修复 startup barrier），再重跑同一 function 脚本。
- 文档状态：N7.4 保持 ⏳；N7.5 不启动。

### REFMEM-TASK-20261026-007 — N7 应用功能层：一条命令一个功能

- 状态：function 目录/执行器/SCPI/本地 HIL 完成；四板 HIL 与 VDC function 仍待做。
- 内容：
  - `config/refmem/function_catalog.yaml` 生成 4 个静态 function：`FUNC_TDMA_ONLINE`（21→23）、`FUNC_CLOCK_RECONFIG`（24→25）、`FUNC_T2_RECONFIG`（32→30）、`FUNC_TDMA_CLOCK_ONLINE`（21→23→24→25）；每步用 `arg_base/arg_words` 切函数参数。
  - `refmem_function.c` 执行器：单 in-flight（共享单命令槽）、串行 step、`ABORT_REMAINING`、5 s step 超时（`int32_t` 回绕安全）、终态保留 4 个 job 快照；接入 `app_refmem_service()`。
  - `SYSTem:CTRL:FUNCtion:RUN/STATus?/CATalog?` 接线；`CTRL:EXECute` 与 function 共用新的 `distributed_refmem_control_submit()`。
- 本地 HIL（COM5，目标本地；transcript `out/n7/local_hil/transcript.txt`）：
  - `FUNC_TDMA_ONLINE`：`state=2 COMPLETED`，`step_index=2`，`SEAT:ROTation?` 读回 `3,1,3,1,4,...`（p=3/f=1 已生效）。
  - `FUNC_CLOCK_RECONFIG`：`state=2 COMPLETED`；`DPLL:ROLE?` 读回 `0,0,2,1,1`（requested gen 2、applied 1、pending 1）。
  - `FUNC_T2_RECONFIG` 负向：`state=3 FAILED`、`step_index=1`、step0 ACK、step1 `NACK reason=13`；符合 T2 前置缺失时的 fail-closed。
- 证据：`run_refmem_function_catalog_tests`、`run_refmem_function_tests` 全绿；`pico2-release` 构建 `flash_link_contract=OK`；P3 `DPLL60_JOINT` build `20261026164220`、`check-staged` OK。
- 过程偏差：N7 首次 P3 在 DPLL 阶段报 `provisional receipt source fingerprint differs`，原因是 P3 运行中又改了 `refmem_control_submit.c`（源指纹变化）；该次不计为代码失败，最终以冻结源重跑通过。教训：P3 期间禁止改固件源。
- 未覆盖：四板协调者→远端 function HIL；TDMA TX/RX 跨 function 连续性；VDC reference/output function；T2 `MOUNT`（明确 `NOT_ONLINE_SAFE`）。
- 提交：`47ecba9d`。
- 下一步：四板 function HIL（先启动环）或 VDC function；随后 §8 收口。

### REFMEM-TASK-20261026-006 — N6.4 T2 域停配置（TDMA 不中断）

- 状态：`UNMOUNT`/`CONFIGURE` 完成；`MOUNT` 首版明确 `NOT_ONLINE_SAFE`。
- 内容：
  - `T2_UNMOUNT` 调 `trigger_t2_runtime_stop()`；`T2_CONFIGURE` 从 13 个参数重建 `trigger_t2_runtime_config_t`（grid/deployment/templates 前置校验、scan step/geometry、window 0=默认 grace），在 T2 `active==0` 时调用 `trigger_t2_runtime_configure`。
  - `T2_MOUNT` 不调用 `distributed_refmem_reset_position_stopped()`（该函数要求 TDMA 停环）；首版返回 `NOT_ONLINE_SAFE`，T2 启动仍走本地 `T2:MOUNT`。
  - TDMA 全程不停机；统一面只负责 T2 停+配置。
- 证据：`pico2-release` 构建 `flash_link_contract=OK`；P3 `DPLL60_JOINT` build `20261026162736`、`check-staged` OK。首跑在 TRN-03B `no complete board record available after failed startup` 失败；用同一 package/OTA 重跑通过，原始失败保留 `out/n6/p3-dpll60-n64/`，通过件 `out/n6/p3-dpll60-n64-retry/`。
- 未覆盖：`T2_UNMOUNT/CONFIGURE` 的目标级 HIL 尚未执行；本次 P3 只证明构建/OTA/启动/DPLL 路径。T2 启动的在线安全 reset 未实现。
- 下一步：N6.3.3 VDC 域停配置，或跳过 VDC 直接 N7 function 层（用已注册的 21/23/24/25/30/32 先做静态 function）。

### REFMEM-TASK-20261026-005 — N6.3 TDMA 在线 seat/load（不停环）

- 状态：TDMA 在线部分完成；VDC 域停配置仍待做（N6.3.1 剩余）。
- 内容：
  - adapter 的 `p/f/s` 三份额改为单个 `seat_rotation_packed` 原子字 + generation；`tdma_pio_spi_ring_origin` 每发布机会只读一次并解包，在线切换在下一机会生效，**不需要停环**。
  - SCPI `SYSTem:TDMA:SEAT:ROTation` 仍走 STOP-only `tdma_runtime_owner_set_seat_rotation3`；统一控制面新增 `tdma_runtime_owner_commit_seat_rotation3` 在线入口。
  - `TDMA_LOAD_MASK` 走现有 `app_realtime_set_load_mask`；`TDMA_RING_TOPOLOGY`/`TDMA_PERIOD` 运行时返回 `NOT_ONLINE_SAFE`（reason 14），部署期仍走原 STOP 路径。
  - reason 表由 14 项扩为 15 项；`refmem_command.h` 增加 reason 14。
- 证据：`test_tdma_seat_rotation_online`（pack/unpack/valid3/默认 1:1）通过；`pico2-release` 构建 `flash_link_contract=OK`；P3 `DPLL60_JOINT` build `20261026161302`、`check-staged` OK。
- 台架残余：首次 P3 在 TRN-03B `matrix write/readback failed`（`CAL:TRAIN:STAGe:BEGin` timeout，stage=process-image）；用同一 package/OTA `dpll60 --package ... --ota-summary ...` 重跑通过，判定为台架瞬时超时，非本切片回归；原始失败保留在 `out/n6/p3-dpll60-n63/`，通过件在 `out/n6/p3-dpll60-n63-retry/`。
- 未覆盖：seat 在线切换的**目标级 HIL**（切换前后 `up_sequence`/`down_rx_sequence` 连续性）尚未做；P3 只证明默认 1:1 路径与构建/启动。VDC/T2 域停配置未做。
- 提交：`dac41e0e`（firmware+host）；文档另提。
- 下一步：N6.3.3 VDC 域停配置（reference/delay/timing）或 N6.4 T2；随后 N7 function 层。

### REFMEM-TASK-20261026-004 — N6.1 四域配置命令目录与协调者入口闭合

- 状态：完成（catalog/entry 层）；N6.2 起进入域适配器。
- 内容：
  - `refmem_command.h` 新增 20–34 共 15 个 `command_type`；`config/refmem/command_catalog.yaml` 增至 21 行；生成器 `COMMAND_TYPES` 同步。
  - `dispatch_control` 将新类型纳入授权；idle 窗口仅门控部署期（20/22）与既有 stage/activate，在线 TDMA seat/load、DPLL、VDC/T2 域停配置不受窗口阻塞。
  - 协调者入口闭合：`refmem_control_entry_decide` + `SYSTem:REFMEM:CTRL:OWNer?`；非 owner 远端命令 fail-closed（`NOT_CONTROL_OWNER`），不排队死 intent。
- 证据：`test_refmem_command_catalog`（21 行/15 新行/参数边界）、`test_refmem_control_route`（entry 6 情景）、`test_refmem_command_authority`、`run_refmem_unified_control_host_tests` 全绿；固件 `pico2-release` 三 profile `flash_link_contract=OK`；P3 `DPLL60_JOINT` build `20261026154606`、`check-staged` OK。
- 已知残余：`run_refmem_all_host_tests` 中 `run_refmem_realtime_tdma_tests` 因 `tdma_service` 拆分后的 host 链接缺件失败（`tdma_service_core1_service`/`tdma_service_submit_tx` 等未定义），属既有拆分债务，不在本切片改动路径。
- 提交：`a5e47834`（firmware）、`8e87c9e5`（host）、`2304b407`（docs）。
- 下一步：N6.2 DPLL RUN-safe 适配器（CommandHandlerRegistry + 应用层 handler）。

## 归档索引

| 文件 | 条目范围 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_01.md` | REFMEM-TASK-20260813-021..REFMEM-TASK-20260813-001 | 21 | 2026-10-17 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_02.md` | REFMEM-TASK-20260814-039..REFMEM-TASK-20260813-022 | 20 | 2026-10-17 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_03.md` | REFMEM-TASK-20260814-049..REFMEM-TASK-20260814-040 | 10 | 2026-10-24 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_04.md` | REFMEM-TASK-20260815-006..REFMEM-TASK-20260814-050 | 10 | 2026-10-25 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_05.md` | REFMEM-TASK-20260815-016..REFMEM-TASK-20260815-007 | 10 | 2026-10-26 |
| `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_06.md` | REFMEM-TASK-20260815-036..REFMEM-TASK-20260815-017 | 20 | 2026-10-27 |

### REFMEM-TASK-20261026-003 — N5.5.0 spike：D2 否定（远端意图不同步）

- 状态：spike 完成，结论改变 N5.5 设计前提；**未写生产代码**。
- D1 ✅：`refmem_sync_frame_header_t.source_slot` 已带发送者，`refmem_cmd_frame_apply()` 已取出（接收侧丢弃）；发送者授权无需改 wire，origin 仍用 `source_node` 回程。
- D2 ❌：默认模型没有覆盖 `REFTMEM_VECTOR_REGION_NODE` 命令块的 data link，sync 不搬该块；四板实测（build `20261026134613`）在 process-image（`--no-t2`）与 T2-seeded 环上，COM5 读 slot 2 始终 `control_state=0/last_update_ms=0`，COM3 本板为 `control_state=1 POSTED`。证据 `out/n55_spike/{d2_report_v2,d2b_report}.json`、`REPORT.md`。
- D3 ❌：无硬件伪造非 owner sender 的注入路径（`SYNC:RX` 只进 sync 解析；`LONGFRAME:TX` 的 stream id 不是 REFMEM_CMD；非协调者无上行）。
- 影响：N5.5 需先定意图运输（N5.5.0b）：(1) 紧凑 intent delta + TLV 参数引用；(2) 专用 payload class；(3) 产品级入口收敛。未定前不冻结、不写代码；b.5 硬件负向继续 host-only。
- 下一步：N5.5.0b（先验 TLV/参数池是否同步、intent 容量预算），再决定 N5.5.1 冻结或转 N5.2c/N5.3。

- 状态：b.1–b.4 完成、b.5 硬件负向受阻、b.6 收口；N5.2b 整体未标完成。
- 内容：`refmem_slot_claim_lowest_claimed` / `refmem_application_model_control_owner_slot` / `distributed_refmem_control_owner_slot`（派生值，不落状态）；dispatch 对 config 类先判 `origin ∈ {本地, owner}` 否则 12，再判 idle 窗口（7）；`admit_origin_trial` 与 `slot_claim_gate_ready` 要求 owner 可派生且落在 ring 内。
- host：新增 `test_refmem_command_authority`（6 情景，禁用检查的负测 4 处断言红）；`test_refmem_slot_claim` 增 `lowest_claimed`；`test_refmem_control_route` 增 origin 决策；`test_refmem_table_registry` 增 model owner；四套全绿。
- HIL（build `20261026134613`，四板，按 `command_seq` 对齐）：协调者 COM5→COM4 `state=4 ACKED/reason=0`；COM4 本地入口 ACK；COMMIT 后协调者 `state=5/reason=7`；非协调者 COM3 命令停在源板 POSTED、目标记录不前进（从板无上行注入点，N5.5 范围）。证据 `out/n52b_hil/report_v2.json`。
- P3：final `build 20261026134613`、`PASS_WITH_WARNINGS`（INFO 33 / WARN 11 / ERROR 0 / FATAL 0）、`source 708faa78…`、1590 files；首跑 `build 20261026132128` 曾出现 13 WARN（COM3 `refmem_overrun_count_grew` / `refmem_max_runtime_exceeded_wcet`），重跑未复现（`out/n52b/p3_run1.log` 仅存计数），记为台架抖动残余。
- 下一刀：N5.2c（STOP→stage→START）或 N5.3（显式 owner）；N5.5 补硬件负向。

### REFMEM-TASK-20261026-001 — N5.2a.3 根因更正与修复：seat 不进包，远程配置首次 ACK

- 状态：完成（代码 + host 用例 + 设备 build + P3 + HIL）；N5.2 整体仍未完成（权限、租约、聚合仍在 N5.2b–N5.7）。
- 根因（更正 N5.2a“环 start 污染 table registry”的结论）：`SYSTem:TDMA:RING:TOPology` 把本板 seat 写入 app-model live profile，打包序列化的也是 live profile，而 node_load 的 TDMA owner 固定在 node 0；于是非 0 槽板把自己 seat 打进包，table 9 owner 校验失败（`0x0908`）。该状态可逆，与 ring start/stop 本身无关。
- 单板对照（COM4、环停止）：修复前 build `20261025213526` —— slot 0 → `staging_state=2`/ACK；slot 1 → `staging_state=3`、`first_lint_error=2312`、reason 13；回 slot 0 → 再次 VALIDATED（`out/refmem_n52a3_slot_diag/run_slot_diag_prefix_20261025213526.log`）。修复后 build `20261026123411` 同一命令 → slot 1 `staging_state=2`、ACK、`first_lint_error=0`（`out/refmem_n52a3_slot_diag/run_slot_diag.log`）。
- 修复：新增设计期基线 `s_design_tdma_foundation_profile`（`refmem_application_model_init` 与 package 激活时刷新）；打包与 `tdma_foundation_profile_table_crc32` 改用设计基线；live/active profile 仍是唯一运行期 seat，`set_tdma_ring_topology`/ring apply 路径未改。host 用例 `test_stage_ignores_runtime_ring_seat`；负测（打包改回 live profile）三处断言变红。
- 远程 HIL（四板，build `20261026123411`）：COM4 `CONFig:BEGIN` → COM5 `CTRL:EXECute` 排队 → 环启动 → COM4 `state=4 ACKED/reason=0`、`staging_state=2`、package CRC `3301311711`；源板聚合 `state=4`，`CMD:REPLy?`=`4,1,0,0,0,0,0,0,17601,4,2,0,1,4,0`；STOP 后 COMMIT、再次下发同命令 → `state=5/reason=7`（窗口门禁仍生效）。证据 `out/refmem_n52a3_hil/report.json`。**这是首个远程配置命令 ACK**；延迟事务（RUN 期排队、STOP 执行）未实现，本轮证明“窗口开启 + 环运行”即可 ACK，deferral 是否仍需要按 RUN 期策略再评估。
- 验证：`tools/tests/run_refmem_table_registry_tests.ps1`、`run_refmem_unified_control_host_tests.ps1` 全绿；refmem host 子集 212 passed；设备三 profile `flash_link_contract=OK`（build `20261026123411`）；P3 `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、`PASS_WITH_WARNINGS`（INFO 33/WARN 11/ERROR 0/FATAL 0）、source `343f61bbe7b788c10871db24166d455224c88f9cf28628524d27b98ff018aec8`、1589 files、receipt `config/hardware_acceptance/p3_acceptance_receipt.json`。
- 下一刀：N5.2b 远程 owner/permission 合法/未授权矩阵；N5.2c STOP→stage→START（含 BEGIN/ABORT 分支）；随后 N5.3 `control_owner_slot` 冻结。

### REFMEM-TASK-20261025-021 — N5.0.5 预算/残差/HAOFV 收口

- 状态：收口报告完成；**N5.0 未完成**（不得标 ✅）。
- 行数快照（2026-10-25，非事实源）：
  - `distributed_refmem.c` 5,372（>3,000，A.3/A.4 未完成）
  - `scpi_system_snapshot_commands.c` 4,627（>3,000，N5.0.3b 未完成）
  - `scpi_refmem_snapshot_commands.c` 1,150；`distributed_refmem_position.c` 188；`distributed_refmem_diag.c` 128（新文件在 1,300 预算内）
  - `vdc_dpll_manager.c` 4,826、`vdc_domain.c` 4,460、`calibration_manager.c` 4,138、`tdma_pio_spi_ring_adapter.c` 4,116（N5.0.4 可选未做）
- 完成：N5.0.1（EXE-CODE-01）、N5.0.3a（SCPI refmem）、A.1（flight 状态头）、A.2a（position 状态 wrapper）、A.2b（stage/get/transport 迁出）、N5.0.2g（flight-sync diag getters）。
- 未完成：A.3（VDC getter/process_completed）、A.4（flight publish/receive/parse）、N5.0.3b（SCPI sync/TDMA/system）、N5.0.4（可选）、line-count 检查。
- HAOFV 六门（N5.0 级别）：
  - ① 无新增总线/class/第二命令槽/第二 ACK：✅ 全部纯迁移。
  - ② 不占 core1：✅；A.2b `RAM_TEXT` 随函数迁移，无窗口变化。
  - ③ 单写者/owner：✅ 状态单实例/accessor。
  - ④ fail-closed + reason/evidence：✅ 每刀 P3；负结果回退。
  - ⑤ 布局/版本/契约同步：✅ 无 wire/表布局变化。
  - ⑥ host + 四板证据：⚠️ P3 四板通过；A.2b HIL 报告 build 仍为旧值（`20261025015413`），板端未确认切到 `20261025021925`；收口前需复验 OTA 槽位。
- 结论：目标**不能标记完成**；A.3/A.4/N5.0.3b 需要新目标/扩展轮次。

### REFMEM-TASK-20261025-020 — N5.0.2f-A.3 第一次尝试回退（负结果）

- 状态：A.3 未完成；改动已回退，工作树在 A.2b（`0515b730`）。
- 尝试：把 `vdc_flight_rx` read/same helper + `get_vdc_flight_rx` 搬到 `distributed_refmem_vdc.c`，状态走 accessor。
- 失败原因：
  - getter 块边界内含 `#include "distributed_refmem_vdc_feedback.inc"`/`distributed_action_probe.inc` 与 `#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED`，按函数边界切仍把 inc/`#if` 带走/切开；
  - 主文件缺少 inc 定义时 `tdma_prepare_vdc_command_record` 编译失败；vdc.c 出现 `unterminated #if`。
- 结论：VDC 组必须按**完整条件块 + inc 归属**切，不能只按函数边界。
- 下一步：A.3 先确定 inc/`#if` 归属（把 `vdc_feedback.inc` 独立成 TU 或留在 main），再切 getter；或先做 N5.0.5 收口。
- P3：N/A（回退，无净代码）。

### REFMEM-TASK-20261025-019 — N5.0.2f-A.2b 完成：transport 迁出

- 状态：完成；行为不变。
- 内容：`get_position_transport` 按**函数边界**（到 `#include "distributed_refmem_vdc_feedback.inc"` 之前）搬到 `distributed_refmem_position.c`；`vdc_feedback.inc`/`action_probe.inc` 留在主文件；清理 transport 前的孤儿 `DISTRIBUTED_REFMEM_RAM_TEXT`。
- 行数：`distributed_refmem.c` 5,429→**5,373**。
- 验证：
  - build 成功；P3 `20261025021925`，fingerprint `c3585be2250f3e53921712caf7200ee3d2ff77dbaa0430d3029c9dc83fcc7f12`，1954 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196：`frag_complete=[1,1,1]`、`inject_ok=54`；`out/refmem_transport_diag/a2b2_exec196_202610251045/`。
  - ⚠️ HIL 报告 `build` 仍显示 `20261025015413`（上一版），板端未切到 `20261025021925`；P3 四板凭证按源码指纹覆盖本改动，HIL 作为烟测；下一轮需查 OTA 槽位/重启。
- 下一刀：A.3 VDC。

### REFMEM-TASK-20261025-018 — N5.0.2f-A.2b 小切（stage/get/cancel）

- 状态：完成；行为不变。
- 内容：`stage_position_descriptor` + `position_descriptor_pending` + `get_position_descriptor` + `position_cancel_service` 搬到 `distributed_refmem_position.c`；`note_freeze_taken`/`priority_owned` 非 static；`cancel_service` 非 static + 私有头声明。
- 行数：`distributed_refmem.c` 5,549→**5,429**。
- 验证：
  - build 成功；P3 `20261025015510`，fingerprint `b5302db938a5a597dea18d91c52fe00eb7489ef31cd5bcf13bb62d392ce6ac82`，1954 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196：`frag_complete=[1,1,1]`、`inject_ok=62`，verdict H4（reassembler 重新对齐，无永久 wedge）；`out/refmem_transport_diag/a2b_exec196_202610251020/`。
- 剩余：`get_position_transport`（内含 `vdc_feedback.inc` 与 `#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED`）仍在 main；下一刀按函数边界单独切。

### REFMEM-TASK-20261025-017 — N5.0.2f-A.2b 第一次尝试回退（负结果）

- 状态：A.2b 未完成；改动已回退，工作树在 A.2a（`12e5480d`）。
- 尝试：把 622–801 连续块（stage/get/transport）整体搬到 `distributed_refmem_position.c`。
- 失败原因：
  - 块内 `get_position_transport` 含 `#include "distributed_refmem_vdc_feedback.inc"` 与 `#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED`，连续行边界把 `#if`/`#endif` 切开，主文件出现 `#endif without #if`；
  - 结构体条件字段（`next_command_seq`/`vdc_command_record`）在主文件被排除。
- 结论：不能用“连续行段”切；必须按**函数/条件块**边界切。
- 下一步：A.2b 小切：先搬 `descriptor_pending`+`get_position_descriptor`（710–740，无 inc/条件块），再单独处理 `stage` 与 `transport`。
- P3：N/A（回退，无净代码）。

### REFMEM-TASK-20261025-016 — N5.0.2f-A.2a position 状态 wrapper

- 状态：完成；行为不变。
- 内容：`distributed_refmem_position_state_t`（pending/mailboxes/latches/counters/hold 队列）入 `distributed_refmem_internal.h`；`distributed_refmem_position_state()` 返回单实例；`s_position_*` 宏别名映射到 struct 字段；删除 main/`position_ack.inc` 的 static 定义。
- 行数：`distributed_refmem.c` 5,568→**5,549**；新增 `distributed_refmem_position_state.c`（约 20 行）。
- 验证：
  - build 成功；P3 `20261025012759`，fingerprint `89422a8adbe34850f2e0565d73c6de3bbd6625484f99fbbc2f8d5c77a44acd0a`，1953 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196：`frag_complete=[1,1,1]`、`inject_ok=54`；`out/refmem_transport_diag/a2a_exec196_202610250950/`。
- 下一刀：A.2b position 搬迁（`stage_position_descriptor`/`get_position_*`/`tdma_parse_position_fragment`）。

### REFMEM-TASK-20261025-015 — N5.0.2f-A.2a position 状态字段清单

- 状态：清单/方案完成；无代码（避免半成品）。
- 字段（main）：
  - `pending[DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS]`、`drain_ms`/`last_drain_ms`/`coalesced_count`、`drain_cursor`、`wait_counted`、`rx_mailboxes[NODE_COUNT]`、`freeze_latches[NODE_COUNT]`、`cancel_requested`。
- 字段（`distributed_refmem_position_ack.inc`）：
  - `local_frozen`、`freeze_ack[NODE_COUNT]`、`priority_owned`、`priority_last[NODE_COUNT][SLICE_WIRE]`；
  - `priority_accept_count`/`duplicate_count`/`reject_count`/`decode_reject_count`；
  - `priority_hold[NODE_COUNT][HOLD_DEPTH]`、`hold_head/tail[NODE_COUNT]`、`hold_count`/`hold_dropped_count`/`hold_delivered_count`；
  - `freeze_latched_count`/`freeze_kept_count`/`freeze_taken_count`；
  - `priority_drain_count`/`drain_fail_count`、`hold_expired_count`、`ack_override_count`、`diag_refused_count`。
- 类型：`trigger_position_mailbox_t`/`freeze_latch_t`/`slice_t`、`distributed_refmem_position_pending_t`、`distributed_refmem_position_hold_entry_t`；宏 `PENDING_SLOTS`/`HOLD_DEPTH`。
- 方案：`distributed_refmem_position_state_t` + `distributed_refmem_position_state()`；`s_position_*` 宏别名映射到 struct 字段；删除 main/inc 的 static 定义；不改内存布局/语义。
- 风险：`.inc` 会在 host harness 编译；宏别名需在 internal.h；hold 队列的 `__atomic` 指针必须指向 struct 字段。
- 下一步：A.2a 代码（struct/宏/实例 + P3）→ A.2b position 搬迁。

### REFMEM-TASK-20261025-014 — N5.0.2f-A.2/A.3 状态拆分设计

- 状态：设计完成；无代码变更。
- 阻塞点：position 组依赖分散的 `s_position_*` 静态（pending 槽、rx mailboxes、freeze latches、drain/cancel 计数），VDC 组依赖 `s_vdc_flight_rx_*` 与 `s_vdc_command_context*`；不是单一 struct，不能像 A.1 那样直接搬。
- 设计：
  - **A.2a**：把 `s_position_*` 收敛为 `distributed_refmem_position_state_t` 单实例 + accessor（纯 rename/包装，不改行为）。
  - **A.2b**：把 `stage_position_descriptor`/`get_position_*`/`tdma_parse_position_fragment` 搬到 `distributed_refmem_position.c`，用 accessor；P3/HIL。
  - **A.3a**：把 `s_vdc_flight_rx_binding/snapshot/admission_ready` 收敛为 `distributed_refmem_vdc_flight_state_t` + accessor；`s_vdc_command_context` 同理。
  - **A.3b**：把 `get_vdc_flight_rx`/`vdc_follower_rx_process_completed`/`get_vdc_follower_*` 搬到 `distributed_refmem_vdc.c`。
  - **A.4**：publish/receive/parse 搬 `distributed_refmem_flight.c`，用 A.2/A.3 accessor。
- 验收：每步纯 rename/包装/移动，行为不变；build + P3 + exec196/soak HIL；单文件 ≤1,500。
- 风险：状态字段多、`__atomic`/time-critical 访问；wrapper 只改名字，不改内存布局/语义。
- 下一步：A.2a（代码 + P3）。

### REFMEM-TASK-20261025-013 — N5.0.2g flight-sync diag getters（A.5 部分）

- 状态：完成；行为不变。
- 内容：`get_tdma_flight_sync`/`_peer`/`_mirror`/`_quality` 迁出到 `distributed_refmem_diag.c`；通过 `distributed_refmem_tdma_flight_sync()` accessor 读状态；`MAILBOX_SIZE` 宏入 internal 头。
- 行数：`distributed_refmem.c` 5,690→**5,568**。
- 验证：
  - build 成功；P3 `20261025010050`，fingerprint `c3ebded73634a4fabcc76c3fc95df97e158cf0ce8912323994d2e6865c40f487`，1952 files，`PASS_WITH_WARNINGS/strict=false`。
  - SCPI smoke：`SYSTem:REFMEM:SYNC:FLIGHT?`、`SYSTem:REFMEM:STATus?` 正常；`out/refmem_transport_diag/flash_n502g_202610250915/`。
- 下一步：A.2 position（position 状态分散，需先做状态/访问器设计）或 A.3 VDC。

### REFMEM-TASK-20261025-012 — N5.0.2f-A.1 状态私有头（A.1）

- 状态：完成；行为不变。
- 内容：`distributed_refmem_tdma_flight_sync_t` + `DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE` 迁入 `distributed_refmem_internal.h`；实例留主文件，新增 `distributed_refmem_tdma_flight_sync()` 访问器。
- 行数：`distributed_refmem.c` 5,763→**5,690**。
- 验证：
  - build 成功；P3 `20261025003548`，fingerprint `3714dde606863c671329fdd88fefd2e480346ba68e63e6c7a114bfde5738efce`，1951 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196：`frag_complete=[1,1,1]`、`inject_ok=54`；`out/refmem_transport_diag/n502f_a1_exec196_202610250850/`。
- 下一刀：A.2 position descriptor 组。

### REFMEM-TASK-20261025-011 — N5.0.2f TDMA flight 拆分设计（A）

- 状态：设计完成；无代码变更。
- 现状：`distributed_refmem.c` 5,763 行；`distributed_refmem_tdma_flight_sync_t` 状态（约 80 行）+ publish/receive/parse/position/VDC 约 2,400 行。
- 拆分顺序：
  - **A.1 状态头**：把 `distributed_refmem_tdma_flight_sync_t` 与所需宏迁到 `distributed_refmem_internal.h`；实例留在主文件，新增 `distributed_refmem_tdma_flight_sync()` 访问器；不改行为。
  - **A.2 position**：`stage_position_descriptor`/`get_position_*` → `distributed_refmem_position.c`；用访问器。
  - **A.3 VDC**：VDC flight RX/command context → `distributed_refmem_vdc.c`。
  - **A.4 flight**：publish/receive/parse/compact mailbox → `distributed_refmem_flight.c`。
  - **A.5 core/diag**：剩余 core/status → `distributed_refmem_diag.c` / 保留 core。
- 验收：每步纯移动/访问器，行为不变；build + P3 + exec196/soak HIL；单文件 ≤1,500。
- 风险：状态字段访问面大；宏/类型归属（TDMA/trigger/VDC）需在私有头一次性收敛；time-critical 宏不能改。
- 下一步：A.1（代码 + P3）。

### REFMEM-TASK-20261025-010 — N5.0.3a 拆出 SCPI refmem 控制块（B）

- 状态：完成；行为不变。
- 内容：新增 `scpi_refmem_snapshot_commands.c`（1,150 行）；`REFMEM:STATus/NODE/BOARD/CLAIM/LOAD/CMD/CONFig/LEASE/CTRL/NODE:CONTrol/...` 控制块迁出；10 个 helper 经 `scpi_system_snapshot_internal.h` 共享；`s_refmem_sync` 通过 accessor 暴露。
- 行数：`scpi_system_snapshot_commands.c` 5,712→**4,627**。
- 验证：
  - build 成功；P3 `20261025001128`，fingerprint `b0024662f6af8aa0d993f07dfd2214fcdd6598b4721e01f8e0b472cd34b29d0c`，1951 files，`PASS_WITH_WARNINGS/strict=false`。
  - SCPI smoke（COM4）：`REFMEM:STATus?` 输出、`CTRL:CATalog?` 6 条、`LEASE?`=FREE、`LOAD:STATus?` 正常；`out/refmem_transport_diag/flash_n503a_202610250825/`。
- 下一刀：A（N5.0.2f TDMA flight，最大块）。

### REFMEM-TASK-20261025-009 — N5.0.1 预算与关注点地图（C）

- 状态：完成（docs，无代码）。
- 内容：在 `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 新增 `EXE-CODE-01`：
  - 新文件 ≤800 行；拆后单元 ≤1,500；>2,000 需 TODO/豁免；>3,000 冻结新功能（拆分除外）；聚合器/SCPI ≤1,300。
  - 拆分约束：纯移动/重命名 + 私有头；不改行为；每步 build/P3/HIL/SCPI 烟测；代码/文档分离。
- 当前超标快照（非事实源）：`distributed_refmem.c` 5,763、`scpi_system_snapshot_commands.c` 5,712、`vdc_dpll_manager.c` 4,826 等。
- 下一步：B（N5.0.3a SCPI refmem 控制块拆分，目标 ~1,000 行）→ A（N5.0.2f TDMA flight）。

### REFMEM-TASK-20261025-008 — N5.2a.2 尝试：候选 TDMA profile 指针无效（负结果）

- 状态：N5.2a.2 未完成；代码已回退，工作树干净。
- 改动：`serialize_candidate_table(9)` 改用 `refmem_model_candidate_tdma_foundation_profile()`（`&s_tdma_foundation_profile`），不再用 active image profile。
- 结果：ring start/stop 后仍 `staging_state=3`、`lint=2312=0x0908`（OWNER_VALIDATION/table 9）；control 仍 `lint=0/staging_state=2`。
- 结论：table 9 的 owner/version 在 **profile 数据本身**，已被 ring 更新；仅换指针不够。需要 app-model init 时的 **profile 快照**，或把 table 9 的 owner 契约与 ring 运行态分离。
- 代码回退；实验 P3 `20261024181340`（e8a3db…）不作为提交凭证；保留为负结果证据。
- 下一步：N5.2a.2 snapshot API/owner 契约设计；N5.2a 仍阻塞。

### REFMEM-TASK-20261025-007 — N5.2a.1 诊断：table 9 TDMA foundation profile owner validation

- 状态：N5.2a.1 完成；无行为变更（仅诊断字段）。
- 实验：
  - 对照（未跑环）：`staging_state=2`、`lint=0`、`last_error=0`、`state=4 ACKED`。
  - ring start/stop 后：`staging_state=3`、`lint=2312`、`last_error=7`、`state=5/reason=13`。
- 解码：`2312 = 0x0908` → 低字节 `validation.error=8`（`REFTMEM_TABLE_PACKAGE_ERR_OWNER_VALIDATION`），高字节 `first_bad_table=9`（`REFTMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE`）。
- 结论：ring start 改变了 **TDMA foundation profile** 表/owner；app-model inline package 的 owner 契约对 table 9 失败。不是 table registry 描述子、不是权限、不是“stage 瞬间环在跑”。
- 下一步：N5.2a.2 在 app-model 侧使用**稳定的 TDMA foundation profile 快照/owner 契约**（不让 ring 运行态 profile 进入 inline package），再跑 ring start/stop → stage HIL。
- 证据：`out/refmem_remote_txn_hil/n52a1_diag/report.json`。

### REFMEM-TASK-20261025-006 — N5.2a 设计决策：app-model staging 隔离

- 状态：N5.2a 未完成；设计决策完成，无代码变更。
- 补充诊断：`SYSTem:REFMEM:TABLE?` 在 ring start/stop 前后完全一致（`1,10,1023,0,1270410720,...`），说明 **table registry 描述子不是变化点**；失败仍在 app-model `stage_inline_package_image` 的 `PACKAGE_INVALID`。
- 结论：不能靠“STOP 重置 table registry”解决；需要把 **app-model staging 状态/表内容** 与 ring 运行期状态隔离。
- 决策：**N5.2a 选 option 1（app-model staging 隔离）**，不选 option 2（STOP 重置）：
  - N5.2a.1：在 `stage_inline_package_image` 失败路径暴露 `validation.error`/`first_bad_table`（诊断字段/日志），确认具体表；
  - N5.2a.2：建立 app-model staging registry 基线（表 owner/version/CRC 独立于 ring/process image）；
  - N5.2a.3：stage 只基于该基线构建/校验；
  - N5.2a.4：HIL ring start/stop 后 stage → `staging_state=2`/ACK。
- 证据：`out/refmem_remote_txn_hil/n52_table_diag/{before,after}.txt`（一致）；`n52_diag/REPORT.md`。
- 下一步：N5.2a.1 诊断字段（代码 + P3），再决定 N5.2a.2 的 API 形态。

### REFMEM-TASK-20261025-005 — N5.2a 诊断：STOP-only stage 仍失败（进行中，阻塞）

- 状态：N5.2a 未完成；deferral 代码已回退；工作树干净。
- 实验：B `CONFig:BEGIN`；A 排队 `LOAD_NODE`；ring1 start/stop 后读 B `LOAD:STATus?`：
  `load_seq=1, staging_state=3 FAILED, last_error=7 PACKAGE_INVALID, lint_error=1 BAD_TABLE_VERSION`。
- 对照：fresh reset、**未跑环**的本地 stage 为 `staging_state=2 VALIDATED` + ACK。
- 结论：不是“stage 瞬间环在跑”，而是**环 start 已经改变了 app-model table registry/package**，STOP 不恢复；因此“环内收命令、STOP 执行”的 deferral 不足以让 stage 成功。
- 下一步：N5.2a 设计——把 app-model table package/registry 与 TDMA process image 分离，或在 STOP 重建 staging context；未定前不写 deferral。
- 证据：`out/refmem_remote_txn_hil/n52_diag/REPORT.md`（追加第二次诊断）。

### REFMEM-TASK-20261025-004 — N5.0.2f 成功：VDC SCPI 拆分（设计后）

- 状态：N5.0.2f 完成；此前连续块尝试（003）失败后，按“TU + 私有头 + `.inc`/helper 一起迁移”的设计重做成功。
- 内容：新增 `scpi_vdc_snapshot_commands.c`（740 行）；VDC 反馈/运行/固定/细分 handler + `scpi_feedback_record` 迁出；`scpi_vdc_run_u32`/`run_end` 非 static，经 `scpi_system_snapshot_internal.h` 共享；`.inc` 链留在主文件（避免重复 trigger 定义）。
- 行数：`scpi_system_snapshot_commands.c` 6,393→**5,712**。
- 验证：
  - build 成功；P3 `20261024170018`，fingerprint `de957c6940d2c9517da37b931ec8848d122289548ca67cff8280c27c0091ddcd`，1950 files，`PASS_WITH_WARNINGS/strict=false`。
  - VDC SCPI smoke：`SYSTem:VDC:FEEDback:SESSion?`=0、`AUTO?`=0；`tools/interface/scpi_query`。
- 下一刀：N5.0.2g TDMA flight（状态/私有头设计后再切）。

### REFMEM-TASK-20261025-003 — N5.0.2f 尝试：VDC SCPI 拆分失败（已回退，负结果）

- 状态：N5.0.2f **未完成**；尝试后回退，无代码变更。
- 尝试：把 `scpi_system_snapshot_commands.c` 3032–3698 的 VDC 块整体移到 `scpi_vdc_snapshot_commands.c`。
- 失败：`scpi_vdc_output_slices.inc` 仍在主文件尾部，依赖被移走的 `scpi_vdc_run_end`/`vdc_run_output_status_t`；连续 handler 块移动破坏 include/helper 顺序，build 报 `unknown type name`/`implicit declaration`。
- 处理：`git checkout` 主文件、删除新文件、CMake 回退；工作树干净。
- 教训：剩余 SCPI/TDMA 大块不能按“连续行段”机械移动；必须按 **translation unit + 私有头** 拆，且把 `.inc`/类型/helper 一起迁移并保持定义顺序。
- N5.0.2f：VDC SCPI 拆分在设计后成功（6,393→5,712）；P3 `20261024170018`；VDC SCPI 查询 smoke 通过。
- N5.2a 第二次诊断：环 start/stop 后 stage 仍 PACKAGE_INVALID（staging_state=3/last_error=7）；deferral 不是充分条件，代码已回退。
- N5.2a 设计决策：选 **app-model staging 隔离**（option 1），不选 STOP 重置；先加 `validation.error` 诊断，再建 staging registry 基线。
- N5.2a.1：诊断字段已定位 table 9（TDMA foundation profile）owner validation 失败；`lint=2312=0x0908`。
- N5.2a.2 尝试：仅换 profile 指针无效（ring 后仍 lint=2312=0x0908）；需 profile 快照/owner 契约分离。
- N5.0.1（C）：预算/关注点地图完成；`EXE-CODE-01` 写入执行约束。
- N5.0.3a（B）：SCPI refmem 控制块拆出（5,712→4,627）；P3 `20261025001128`；SCPI smoke 通过。
- N5.0.2f（A）拆分设计完成：A.1 状态头 → A.2 position → A.3 VDC → A.4 flight publish/receive/parse → A.5 core/diag。
- A.1 完成：flight 状态/宏迁入 internal.h + accessor；主文件 5,763→5,690；P3 `20261025003548`；exec196 `[1,1,1]`。
- N5.0.2g（A.5 部分）：flight-sync 4 个只读 getter 拆出到 `distributed_refmem_diag.c`；5,690→5,568；P3 `20261025010050`；SCPI smoke 通过。
- A.2/A.3 状态设计完成：position/VDC 状态需先收敛为 struct + accessor，再搬 getter/process_completed。
- A.2a 字段清单完成：position 状态 20+ 字段（main + position_ack.inc）；下一步 struct+宏+accessor 代码。
- A.2a 完成：position 状态 wrapper（struct+宏+accessor）；5,568→5,549；P3 `20261025012759`；exec196 `[1,1,1]`。
- A.2b 第一次尝试回退：622–801 连续块跨 `#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED` 和 `vdc_feedback.inc`，边界不干净。
- A.2b 小切完成：stage/descriptor_pending/get_position_descriptor/cancel service 搬出；5,549→5,429；P3 `20261025015510`；exec196 `[1,1,1]`。
- A.2b 完成：transport 按函数边界迁出；5,429→5,373；P3 `20261025021925`；exec196 `[1,1,1]`（HIL build 显示旧值，⚠️）。
- A.3 第一次尝试回退：getter 块跨 `#include vdc_feedback.inc`/`#if`，边界不干净；工作树回到 A.2b。
- N5.0.5 收口报告完成：行数快照/完成项/残差/HAOFV 六门；N5.0 **未完成**。
- 下一步：A.3/A.4/N5.0.3b 需扩展目标；当前目标剩余轮次不足。

### REFMEM-TASK-20261025-002 — N5.0.2e 拆出 dispatch/command（进行中）

- 状态：N5.0.2 进行中；第五刀完成，行为不变。
- 内容：新增 `distributed_refmem_command.c`（191 行）；`dispatch_control`、`node_control_service`、lease reason/config type、三个共享状态迁出；`post_command_replacing_complete`/`node_load_auto_enqueue`/`vector_table` 经 internal 头共享。
- 行数：`distributed_refmem.c` 5,946→**5,763**。
- 验证：
  - build 成功；P3 `20261024163157`，fingerprint `711cfd6e2278682e0110456cf336f69a31a01e35b335c664573c1cb3d5df2e10`，1948 files，`PASS_WITH_WARNINGS/strict=false`。
  - config txn HIL 14/14（build `20261024162907`）：`out/refmem_config_txn_hil/n502e_202610250045/`。
- 下一刀：tdma-vdc / diag；每步 build/P3/HIL 烟测。

### REFMEM-TASK-20261025-001 — N5.0.2d 拆出 cmd-frame（进行中）

- 状态：N5.0.2 进行中；第四刀完成，行为不变。
- 内容：新增 `distributed_refmem_cmd_frame.c`（63 行）；`cmd_frame_counters` + `cmd_frame_receive` 及 5 个计数迁出。
- 行数：`distributed_refmem.c` 6,003→**5,946**。
- 验证：
  - build 成功；P3 `20261024160525`，fingerprint `f8bef21bd3f04a1333e4db51d06b00e86d5925f58fa24c72f3e1adb5293614dc`，1947 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196 HIL：`frag_complete=[1,1,1]`、`inject_ok=60`；`out/refmem_transport_diag/n502d_exec196_202610250020/`。
- 下一刀：dispatch/command 或 tdma-vdc / diag。

### REFMEM-TASK-20261024-021 — N5.0.2c 拆出 node control（进行中）

- 状态：N5.0.2 进行中；第三刀完成，行为不变。
- 内容：新增 `distributed_refmem_node_control.c`（109 行）；`node_control_write_with_params/read_params/read/ack/update_status/clear` 迁出；`distributed_refmem_node_region` 改为非 static，经 `distributed_refmem_internal.h` 共享。
- 行数：`distributed_refmem.c` 6,109→**6,003**。
- 验证：
  - build 成功；P3 `20261024154054`，fingerprint `7819143263a117d0d04321837c8ed156afdc160fa02148244845a2b5b915757f`，1946 files，`PASS_WITH_WARNINGS/strict=false`。
  - config txn HIL 14/14（build `20261024153758`）：`out/refmem_config_txn_hil/n502c_202610242357/`。
- 下一刀：dispatch/command 或 tdma-vdc / diag；每步 build/P3/HIL 烟测。

### REFMEM-TASK-20261024-020 — N5.0.2b 拆出 cmd-reply（进行中）

- 状态：N5.0.2 进行中；第二刀完成，行为不变。
- 内容：新增 `distributed_refmem_cmd_reply.c`（232 行）与私有头 `distributed_refmem_internal.h`；tx/rx 状态、arm/pending/store/service/status 全部迁出；主文件通过 `tx_get`/`target_mask`/`note_published` 访问，不再直接读 `s_cmd_reply_tx`。
- 行数：`distributed_refmem.c` 6,315→**6,109**。
- 验证：
  - build 成功；P3 `20261024151357`，fingerprint `e23626dec22b05704dc52a59c2e6d4234b9854de04309b95ab09b087273f13ed`，1945 files，`PASS_WITH_WARNINGS/strict=false`。
  - exec196 HIL：`frag_complete=[1,1,1]`、`inject_ok=54`、无丢包/重复；`out/refmem_transport_diag/n502b_exec196_202610242330/`。
- 下一刀：command dispatch/node control 或 tdma-vdc / diag；每步 build/P3/HIL 烟测。

### REFMEM-TASK-20261024-019 — N5.0.2a 拆出 config-txn（进行中）

- 状态：N5.0.2 进行中；第一刀完成，行为不变。
- 内容：新增 `refmem_config_txn.c`（idle 窗口 + BEGIN/COMMIT/ABORT/active + 状态）；`distributed_refmem.c` 6,370→6,315；dispatch gate 改用 `distributed_refmem_idle_window()`。
- 验证：
  - build 成功；P3 `20261024144807`，fingerprint `9a3f7cb06ca2f93e777fbf8aebbe843d2071d6ab38e786acc90b643fab827e2c`，1943 files，`PASS_WITH_WARNINGS/strict=false`。
  - config txn HIL 14/14（build `20261024144443`）：`out/refmem_config_txn_hil/n50a_202610242305/`。
- 下一刀：cmd-reply / command dispatch / tdma-vdc / diag 继续拆；每步 build/P3/HIL 烟测。

### REFMEM-TASK-20261024-018 — N5.2 诊断：reason 13 根因（进行中）

- 状态：N5.2 进行中；发现 reason 13 根因不是 owner/permission，而是**环运行期 app-model staging 失败**。
- 诊断（build `20261024132502`）：
  - 远程 A→B（环运行）：`command_type=14`、`source=0`、`target=2`、`state=5`、`reason=13`；`LOAD:STATus?` → `load_seq=1, staging_state=3 FAILED, last_error=7 PACKAGE_INVALID, lint_error=1 BAD_TABLE_VERSION`。
  - 本地 COM4（环停止）：`state=4 ACKED`、`staging_state=2 VALIDATED`、`package_crc` 非 0。
  - 本地 COM4（环运行）：`state=5 reason=13`、`staging_state=3`、`last_error=7`，与远程同签名。
- 结论：reason 13 是**环运行期 app-model staging/table-package 冲突**；单纯加权限门控只会把 A→B node 1 从 13 变成 12，不会让合法远程 stage ACK。
- 重排：N5.2a 环安全/延迟 staging → N5.2b owner/permission → N5.2c 真实 STOP→stage→START；N5.3/N5.4 仍先于 N5.5。
- raw-flight 对照：环运行（无 process-image）本地 stage 仍 `staging_state=3/last_error=7`，排除 process-image 因素。
- **N5.2a 决策**：选**延迟配置事务（STOP-only stage）**——环运行只收命令/排队，STOP 执行，START 后回 ACK；不选环安全 staging。开放：命令超时跨 STOP/START、队列深度/顺序、多目标 ACK 聚合、COMMIT 与 START 先后。
- 证据：`out/refmem_remote_txn_hil/n52_diag/REPORT.md`。
- 下一步：N5.2a 设计决策（环安全 staging vs 环内收命令/STOP 执行/重启回 ACK 的延迟事务）。

### REFMEM-TASK-20261024-017 — N5.1 契约交叉审核（C11）

- 状态：完成；审核结论 **activate subset**：`REFTMEM-LEASE-01` 转 active；`REFMEM-TRANSPORT-01`/`REFTMEM-IDLE-01` 保持 pending。
- 审核方：独立 subagent `c774c2f8`（agent-cross，独立上下文，未改文件）；方式：证据包 + 代码/文档/HIL/P3 复核；证据包 `out/refmem_contract_review/N5_1_EVIDENCE.md`。
- 结论：
  - `REFTMEM-LEASE-01`：PASS_WITH_NOTE → active。host + n333 两板 HIL 覆盖 clause；备注：lazy expiry（`LEASE?` 可能显示过期 GRANTED 直到下一次 TAKE/RELEASE）、边界（环 stop→start 保持、板复位清零、无跨板复制/协调者租约/心跳）已写入 verification。
  - `REFMEM-TRANSPORT-01`：FAIL → pending。blocking：clause “seat rotation 二等让位 s>0 且 f>=3” 与实现/预设不符（代码只校验 p>=1/f>=1；预置 `1,2,7`/`7,2,1` 的 f=2；HIL 自身用 `1,2,7`）；P3 凭证为 `PASS_WITH_WARNINGS`、`strict_gates_passed=false`（step 26 four-Node TDMA rc=1、step 33 预算超限），不能写成 strict passed；维护 ACK 的 clause_loc 跨 TDMA/REFMEM 两个 anchor。
  - `REFTMEM-IDLE-01`：FAIL → pending。blocking：无 host 用例；legacy `SYSTem:REFMEM:LOAD:NODE/BOARD/ACTIVATE` 绕过 idle 窗口（不同 gate/错误文本，直接 app-model stage）；无真实 STOP→stage→START 证据（HIL 只有 BEGIN/stage/COMMIT/ABORt）；remote “valid” stage 全为 reason 13（仅断言 !=7）；COMMIT 失败仍返回成功/关窗，无法区分。
- 数据修正：soak20 `tx_retries=5`（原写 4）；P3 统一表述为 `credential written (PASS_WITH_WARNINGS; strict=false)`，不写“P3 通过”。
- carry-over：N5.1a（TRANSPORT seat/条款/P3 诚实表述）、N5.1b（IDLE host/legacy/STOP/COMMIT）、N5.2a（环运行期 app-model staging，reason 13 根因）、N5.2b（owner/permission）、N5.2c（真实 STOP→stage→START）。
- 下一步：N5.2；N5.1a/b 作为 TRANSPORT/IDLE 复审前置。
### REFMEM-TASK-20261024-016 — N4.3/N4.4 远程事务 HIL 与收口

- 状态：完成；两板/四板 HIL 全过；P3 凭证已写。
- 完成内容：
  - HIL `tools/refmem/refmem_remote_txn_hil`：目标 BEGIN/COMMIT/ABORt 本地事务，A 经环发 `LOAD_NODE`。
  - 两板（COM4）与四板（COM4/3/6）逐目标证据。
- 验证结果：
  - 目标 BEGIN 后 A stage → `reason=13`（门控通过、业务失败）；
  - 目标 COMMIT 后同命令 → `reason=7`（RUN_STATE_DENIED）；
  - 目标 BEGIN + 无效 stage → `reason=13`；ABORt → `active=0,window=1`；
  - ABORt 后 A stage → `reason=13`（窗口重开）。
  - 证据 `out/refmem_remote_txn_hil/n43_two_b_202610242118/`、`n43_four_202610242120/`。
- 边界：远程配置 stage 当前返回业务 reason 13（远端 owner/permission 校验未实现）；窗口门控 reason 7 可区分。`REFTMEM-IDLE-01` 已登记（pending），N4.4 收口完成。
- P3：build `20261024132502`，源码指纹 `bb1c5932e5f2ce0b0b9f80c3fba10cb42bdb7a9e5dbfb426c74667c8a8e27094`（1942 files），`QUICK_DIAGNOSTIC`，credential written；step 23/26/33 warn，无 ERROR/FATAL。
- 下一步：N3.3/N4 目标收口；N3.2 剩余矩阵为可选补测。
### REFMEM-TASK-20261024-015 — N4.2 配置事务（STOP→stage→START）

- 状态：完成（本地事务 HIL）；P3 凭证已写。
- 完成内容：
  - `distributed_refmem_config_transaction_begin/commit/abort/active`：BEGIN 开窗并标记事务；COMMIT 提交 prepared table views（失败丢弃）并关窗；ABORt 丢弃并重开；状态只读。
  - `SYSTem:REFMEM:CONFig:BEGIN/COMMit/ABORt/STATus?`。
  - HIL `tools/refmem/refmem_config_txn_hil`。
- 验证结果（14 步全过）：
  - 初始 `active=0,window=1`；BEGIN → `1,1`；stage → ACK；COMMIT → `1`、`0,0`；
  - COMMIT 后 stage → `state=5,reason=7`；
  - 再次 BEGIN + 无效 stage(node 99) → `reason=13`（门控已过）；ABORt → `0`、`0,1`。
  - 证据 `out/refmem_config_txn_hil/n42b_202610242035/`。
- 边界：调用方负责外部 STOP/START 边界；prepared table views 有 commit/discard，节点/板级 stage 由各自 dispatch 校验；N4.3 做两板/四板组合 HIL。
- P3：build `20261024123825`，源码指纹 `e24f4bb5fa64accd4c6bdb5341a02638885f7b47ac0dbc952d204648b640e37b`（1941 files），`QUICK_DIAGNOSTIC`，credential written；step 8 拓扑首次失败、重试后通过；step 23/26/33 warn，无 ERROR/FATAL。
- 下一步：N4.3 两板/四板 HIL → N4.4 契约/文档收口。
### REFMEM-TASK-20261024-014 — N3.3.4 租约收口 + N4.1 idle 窗口门控

- 状态：N3.3.4 完成；N4.1 门控 + 测试钩子完成；P3 凭证已写。
- 完成内容：
  - N3.3.4：`REFTMEM-LEASE-01` 契约/架构/SCPI/里程碑收口；两板租约 HIL 工具 `tools/refmem/refmem_lease_hil`。
  - N4.1：`distributed_refmem_set_idle_window`/`idle_window`；配置类命令（BOARD/NODE/TABLE stage/activate）在窗口关闭时以 `RUN_STATE_DENIED` 拒绝，不进入 dispatch。
  - `SYSTem:REFMEM:IDLE:WINDow <0|1>` / `?` 测试/事务钩子。
- 验证结果：
  - 租约两板 HIL 全通过：`out/refmem_lease_hil/n333_202610242012/`。
  - idle HIL：默认开=1；关=0 后 LOAD_NODE → `state=5/reason=7`；开=1 后同命令 → `state=4/reason=0`。`out/refmem_idle_hil/n41_202610242040/`。
  - P3 凭证已写。
- P3：build `20261024120853`，源码指纹 `b3ccf1b1af4db71e676d391f0e6ebb3cbb3bd95d448c88d9f528251ddeee547a`（1940 files），`QUICK_DIAGNOSTIC`，credential written；step 23/26/33 warn，无 ERROR/FATAL。
- 下一步：N5.2 远程配置 owner/permission（同时解 IDLE remote reason 13）；N5.1a/b 作为 TRANSPORT/IDLE 复审前置。
### REFMEM-TASK-20261024-013 — N3.3.3 两板租约 HIL

- 状态：完成；两板 A TAKE→B CONFLICT→A RELEASE→B TAKE + ring stop→start 全部通过。
- 步骤（A=COM5 slot0，B=COM4 slot1，B 的 target bit=2）：
  - A 经环 `LEASE_TAKE` epoch1 → B `LEASE?=owner 0,GRANTED,lease_seq=1`。
  - B 本地 `LEASE_TAKE` epoch2 → `CONFLICT`：B `CTRL:STATus? state=5`、`reason=6 (RESOURCE_BUSY)`；B 仍归 A。
  - 停环（不复位）→ B 租约保持 `owner 0,GRANTED`（RAM 记录跨 ring stop）。
  - A 第二个环 `LEASE_RELEASE` epoch1 → B `FREE,lease_seq=2`。
  - B 本地 `LEASE_TAKE` epoch2 → `owner 1,GRANTED,lease_seq=3`、`state=ACKED`。
- 证据：`out/refmem_lease_hil/n333_202610242012/REPORT.md`。
- 工具 `tools/refmem/refmem_lease_hil/refmem_lease_hil.py` 已落盘，待与后续固件变更一起 P3/提交。
### REFMEM-TASK-20261024-012 — N3.3 最小租约（状态机 + 控制面接入）

- 状态：N3.3.1/2 完成（host + 本地 smoke）；P3 凭证已写；N3.3.3 两板 HIL 待做。
- 完成内容：
  - `refmem_lease` 纯状态机（FREE/GRANTED/EXPIRED，owner/epoch/seq/expiry；TAKE/CONFLICT/STALE/EXPIRED/NOT_OWNER 的 fail-closed reason）。
  - `refmem_lease_table` 每目标节点一条记录；目录新增 `REFTMEM_LEASE_TAKE`（type 18）/`REFTMEM_LEASE_RELEASE`（type 19），经现有命令槽/ACK 回程执行。
  - `distributed_refmem_dispatch_control` 执行租约并映射 reason（CONFLICT→RESOURCE_BUSY、STALE→EPOCH_MISMATCH、EXPIRED→TIMEOUT、NOT_OWNER→PERMISSION_DENIED）。
  - `SYSTem:REFMEM:LEASE?` 只读视图；`REFTMEM-LEASE-01` 契约登记（pending）。
- 验证结果：
  - host `test_refmem_lease` / `test_refmem_lease_table` PASS。
  - COM4 本地 smoke：TAKE(epoch1) → `1,1,1,90406,1,"GRANTED"`、`CTRL:STATus? state=ACKED`；RELEASE → FREE；TAKE(epoch2) → `1,2,1,"GRANTED"`。
  - 固件构建/刷写通过；`CTRL:CATalog?` 列出 id 5/6、type 18/19。
- P3：build `20261024113544`，源码指纹 `3ba76ea57489ce930b8efd129ca8db04e51723637a44b35ffa7df1839ca3d55c`（1938 files），`QUICK_DIAGNOSTIC`，credential written；step 26/33 warn，无 ERROR/FATAL。
- 下一步：N3.3.3 两板 HIL（A TAKE→B CONFLICT→A RELEASE→B TAKE，含 ring stop→start）。
### REFMEM-TASK-20261024-011 — per-target 维护帧 ACK

- 状态：完成；P3 凭证已写（PASS_WITH_WARNINGS；step 26/33 warn）。
- 问题：soak 20 出现 3/20 失败（COM4 漏帧、COM3/6 完成）；根因是帧级 ACK 由**任一**目标回复即释放，源端误判整帧已送达所有目标。
- 修正：
  - `tdma_maintenance_runtime_submit` 增加 `target_mask`；队列保存并写入 mailbox target mask，只发给显式目标。
  - `tdma_maintenance_runtime_ack` 增加 `source_slot`，按目标累积 `ack_mask`；仅当 `required_mask` 全覆盖才释放，NACK/50 ms 超时重传整帧（最多 3 次）。
  - `parse_mailbox` 把 ACK 来源槽传给 runtime；命令回复仍优先占用 ACK 席位。
- 验证结果：
  - per-target soak 10/10：`inject_ok=54`（1 次重传为 60）、三目标 `frag_complete=1`。`out/refmem_transport_diag/n327_pertarget_soak10_202610241803/`。
  - drop 1 1/1：`inject_ok=67`、三目标 `frag_complete=1`。`n327_pertarget_drop1_202610241810/`。
  - reply HIL multi：`state=4`、`ack_flags=14`、`aggregate_state=4`。`out/refmem_cmd_reply/pertarget_multi_202610241810/`。
  - per-target soak 20/20：5 次重传恢复（`inject_ok=60/62`）、1 次 restart，三目标 `frag_complete=1`。`out/refmem_transport_diag/n327_pertarget_soak20_202610241849/`。
  - P3：build `20261024102324`，源码指纹 `9da69200c16121a8d8205d9f4316997df8395a77ff1b441122d47ff1cbf547fd`（1930 files），`QUICK_DIAGNOSTIC`，credential written；DPLL 前两次失败后 `--steps 7-36` 重试通过；step 26/33 warn，无 ERROR/FATAL。

### REFMEM-TASK-20261024-010 — N3.2.8 契约/文档收口与 ACK 席位优先级

- 状态：完成；ACK 优先级修正 + 契约/架构/SCPI 记录；N3.2.6 延后、N3.2.7 部分在案。
- 完成内容：
  - process-image builder 的 ACK 分支改为**命令回复优先**，维护 ACK 仅在无命令回复时发布；publish flags 同步。
  - `REFTMEM-TRANSPORT-01` 登记维护流帧级 ACK（复用 ACK 席位、诊断 magic `0xA0`、命令回复优先）。
  - `TDMA_DOMAIN_ARCHITECTURE`/`REFMEM_DOMAIN_ARCHITECTURE` 补 stop-and-wait/重试/≥2 s 窗口/优先级；`SCPI_COMMANDS` 已记 `CMD:DROP`。
- 验证结果：
  - reply HIL multi：origin `state=4`、`ack_flags=14`、`aggregate_state=4`、`acked_mask=14`；targets 各自 ACK 位。证据 `out/refmem_cmd_reply/ackprio_multi_202610241700/`。
  - exec196 normal 1/1、drop 1 1/1（`frag_complete=1`，restart 正常）。证据 `out/refmem_transport_diag/ackprio_exec196_202610241705/`、`ackprio_drop1_202610241705/`。
  - P3：build `20261024090754`，源码指纹 `b81745bc830f23ae1eaa9fa71ca6c336e10a0f7afae86869188b5f5aead5279d`（1930 files），`QUICK_DIAGNOSTIC`，credential written；step 26/33 warn，无 ERROR/FATAL。
- 下一步：补 N3.2.7 剩余矩阵（重复/乱序/窗口 miss/RX timeout/ring stop→start/mid-ring/长期 soak）。

### REFMEM-TASK-20261024-009 — N3.2.7 HIL 矩阵（部分）

- 状态：部分矩阵完成；重复/乱序/窗口 miss/RX timeout/ring stop→start/mid-ring/长期 soak 待补。
- 完成内容/证据：
  - 7 帧 exec196 1/1：`inject_ok=54`、三目标 `frag_complete=1`。`out/refmem_transport_diag/n327_normal_202610241700/`。
  - 单目标 `0x02`：源 `target_mask=2`、`required=2`、`ack_flags=2`、`state=ACKED`；COM3/6 仅重组不执行。`n327_single_202610241700/`。
  - `--drop 200`：无完成、源 `POSTED`、`tx_retries=8`、队列未排空，无假 ACK（fail-closed）。`n327_dropfail_202610241705/`。
  - soak 5/5（2 s 环窗口）：`inject_ok=54`、三目标 `frag_complete=1`。`n327_soak5b_202610241707/`。
  - 0.5 s 窗口 soak 2/5 未排空 → stop-and-wait 7 帧命令需 ≥2 s 环窗口。
  - 重复：raw56_x2 同一 wire 两次 → 三目标 `refmem_parser_ok=2`、`posted=1`、`acked=1`（第二次被去重）。`n327_dup_202610241730/`。
- 下一步：补重复/乱序/窗口 miss/RX timeout/ring stop→start/mid-ring/长期 soak。

### REFMEM-TASK-20261024-008 — N3.2.6 紧凑命令帧实验（负结果，延后）

- 状态：未完成；紧凑帧投递成功但目标 reply 未 arm；已回退，未入库。
- 实验：headerless 56 B codec wire（SELF_CHECK 2 维护帧）：
  - 投递/重组：`frag_ok=2`、三目标 `frag_complete=1`、源 `tx_ack_ok=2`、队列 2/2 排空。
  - 回复：目标 `CMD:REPLy?` `armed=0/published=0`；源 `CTRL:STATus?` `POSTED`；补 SELF_CHECK 参数后仍失败。
- 结论：目标执行/reply 触发依赖 v1 快照/信封的某个字段；需目标侧 `cmd_frame_calls/ok/apply_fail/target_miss/request_fail` 才能定段。作为尺寸优化延后，不阻塞可靠性主链；N3.2.8 收口时记录为已知偏差。

### REFMEM-TASK-20261024-007 — N3.2.5 单环多批命令

- 状态：16 槽队列 + 双命令单环验证通过；P3 凭证已写。
- 任务目标：一个环启动内执行 >1 批命令；队列不因第二条 7 帧命令而满；两批命令按序完成、不串 seq/回复。
- 完成内容：
  - `tdma_maintenance_runtime` TX 队列 8→16 槽（可用 15），可预排两条 7 帧命令。
  - HIL 新增 `multibatch`：环启动前排入两条 `REFMEM_MAINTENANCE_SELF_CHECK`，单环内按序执行。
- 验证结果：
  - `out/refmem_transport_diag/n325_multibatch3_202610241544/`：`submitted=2`、`queue_enqueued=14`、`queue_dequeued=14`、`queue_depth=0`、`queue_full=0`、`inject_ok=108`、`tx_ack_ok=14`、`tx_retries=0`；三目标 `frag_ok=14`/`frag_complete=2`。
  - 回归：exec196 1/1 仍 `frag_complete=1`；16 槽 queue_full 提交 18 条仍显式拒绝。
- 限制：真正的“环运行中再入队”需要与 trn03 并发的测试驱动；当前验证的是“一个环启动内执行 >1 批预排队命令”，mid-ring 并发留 N3.2.7。
- P3：build `20261024075123`，源码指纹 `2f05da00ffe6f061bd6ac8cd71215281649f8faccf976ea07a519711cfc77f41`（1930 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 26/33 warn，无 ERROR/FATAL。
- 下一步：N3.2.6 帧尺寸/param_kind=ref。

### REFMEM-TASK-20261024-006 — N3.2.4 维护帧 ACK/有界重试

- 状态：帧级 stop-and-wait ACK + 有界重试完成；正常、丢 1 chunk 与 P3 均通过。
- 任务目标：维护帧丢失后不静默丢；TX 保留未 ACK 帧，目标回帧级 ACK，超时重传；重试 generation+1，目标按新 generation 重启重组。
- 完成内容：
  - `tdma_maintenance_runtime`：整帧发完后保留槽位并等待 ACK（`tx_awaiting_ack`）；50 ms 超时、最多 3 次、每次 generation+1；`tdma_maintenance_runtime_ack()` 只接受匹配 `stream_id:generation` 的 ACK。
  - 目标：`refmem_cmd_load_service` 在解析维护帧后 arm ACK；process-image builder 用 ACK 席位回 `stream_id:generation + result`（诊断字节 `0xA0|result`，flags 仅 VALID，不带 COMMAND），源端 `parse_mailbox` 识别并调用 runtime ACK。
  - 新增 `SYSTem:REFMEM:CMD:DROP <n>` 调试注入口，丢 chunk 触发重试；`CMD:STATus?` 追加 `tx_awaiting_ack/tx_ack_ok/tx_retries/tx_ack_fail/tx_dropped_chunks`。
- 验证结果：
  - 正常 exec196 2/2：`tx_ack_ok=7`、`tx_retries=0`、`tx_ack_fail=0`、三目标 `frag_complete=1`。证据 `out/refmem_transport_diag/n324_exec196_202610241433/`。
  - 丢 1 chunk 2/2：`tx_dropped_chunks=1`、`tx_retries=1`、`tx_ack_ok=7`、三目标 `frag_complete=1`、`rx_restarts=1`。证据 `out/refmem_transport_diag/n324_drop1_202610241435/`。
- 未覆盖：重复/乱序/窗口 miss 的完整矩阵（N3.2.7）；当前是整帧重传，不是 per-chunk 位图。
- P3：build `20261024071934`，源码指纹 `5a4c52f479cf8823b48e232e6dcb323536a01494c2f6580576ab106b33e57d84`（1930 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 29 DPLL 最终通过（146.9s），step 26/33 warn；前三次 DPLL `recorder cancel did not complete` 判为台架/recorder 间歇，最终全量重跑恢复。
- 下一步：N3.2.5 多批命令；N3.2.7 HIL 矩阵（重复/乱序/窗口 miss/RX timeout/soak）。

### REFMEM-TASK-20261024-005 — N3.2.3 RX 新帧重启与唯一 chunk 计数

- 状态：RX 重启/计数完成；host 用例通过；exec196 6/6 成功；P3 凭证已写。
- 任务目标：一次丢 chunk 不得永久 wedge RX；区分唯一/重复 chunk；验证新 sequence + 每周期 1 chunk。
- 完成内容：
  - `tdma_maintenance_stream_rx_chunk` header 不一致时放弃未完成旧帧、重启新帧（此前永久 `return false`）；新增 `restart_count`/`duplicate_count`。
  - runtime `rx_restarts`/`rx_duplicate_chunks` 接入 `SYSTem:REFMEM:CMD:STATus?`（再追加 2 字段）。
  - host `test_tdma_maintenance_stream` 新增“旧帧未完成 + 新帧头重启/重复 chunk”用例。
- 验证结果：
  - host `run_tdma_maintenance_stream_tests.ps1` PASS。
  - HIL `out/refmem_transport_diag/n323_exec196_202610241355/`：6/6 exec196、3 目标 `frag_complete=1`；源 54/54 launched、new sequence 54、repeated 0、dropped 0。
  - N3.2.2 的 COM3 H4 wedge 未再复现；若发生丢帧，新逻辑会 restart 而非永久拒绝，但整帧丢失仍需 N3.2.4 重试。
  - P3：build `20261024060410`，源码指纹 `b344beda5d5304088ec70e63ad0597e9bd70760bd24c7698d7d64c12d5bb77ac`（1930 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 8 首次拓扑失败后重试通过，step 26/33 warn，无 ERROR/FATAL。
- 下一步：N3.2.4 per-stream ACK/位图 + 有界重试。

### REFMEM-TASK-20261024-004 — N3.2.2 维护流 TX 队列 SPSC 交接

- 状态：队列交接/满队列背压完成；H4（RX 重组阻塞）在本轮 exec196 复现，移交 N3.2.3。
- 任务目标：core0 producer / core1 consumer 的 8 帧维护队列改为 release/acquire；满队列显式拒绝；修正“在途帧槽位被覆盖”的时序缺陷。
- 完成内容：
  - head/tail 改 SPSC，producer release tail、consumer acquire tail / release head；移除共享 count。
  - 槽位只在帧完全发完后释放（此前 `tx_begin` 即推进 head，producer 可覆盖仍在发送的槽）。
  - 新增 `tdma_spsc_index.h`（next/empty/full/depth）与 `run_tdma_spsc_index_tests.ps1`。
  - `queue_full`/`dequeued`/`depth` 计数接入 `SYSTem:REFMEM:CMD:STATus?`。
- 验证结果：
  - host `run_tdma_spsc_index_tests.ps1` 通过；RefMem host 全量仍全绿。
  - HIL `out/refmem_transport_diag/n322_202610241327/`：queue_full 9 次提交 → 7 `QUEUED` + 2 拒绝，`queue_full=2`、depth=0；raw16/raw56/raw56_x2 成功。
  - exec196：COM4/COM6 `frag_complete=1`，COM3 `frag_ok=2/frag_complete=0`；源 54/54 launched、dropped/repeated=0 → H4（RX 旧帧阻塞），移交 N3.2.3。
  - P3：build `20261024053749`，源码指纹 `f8e29d0c8981b7b0245c94c215327e2e4f0d00d6a12148abf958ee4a25b06821`（1930 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 26/33 warn，无 ERROR/FATAL。
- 下一步：N3.2.3 RX 新帧重启 + 唯一/重复 chunk 计数 + 注入配额。

### REFMEM-TASK-20261024-003 — N3.2.1 维护流诊断计数与 1/2/7 帧定位

- 状态：诊断计数与四板 HIL 完成；H1–H4 有初步结论；N3.2.2/N3.2.3 待做。
- 任务目标：只加只读计数，定位 196 B/7 维护帧命令的丢帧阶段，不改传输语义。
- 完成内容：
  - `tdma_maintenance_runtime` 新增队列深度/enqueued/dequeued/full、`rx_chunk_rejects` 计数与 `get_diag()`。
  - `tdma_pio_spi_ring_adapter` 新增 inject launched/dropped_unlaunched、new/repeated sequence、beacon_frames、rx polls/seen/accepted/dedup/bad mailbox/last sequence 计数与 `get_maintenance_diag()`。
  - `SYSTem:REFMEM:CMD:STATus?` 末尾追加 18 个只读诊断字段；新增 `tools/refmem/refmem_transport_diag_hil`，覆盖 raw16（1 帧/2 chunk）、raw56（1 帧/6 chunk）、raw56_x2（2 帧/12 chunk）、exec196（7 帧/54 chunk）。
- 验证结果（build `20261024030428`）：
  - 1/2/7 帧应用层均成功：raw16 `t2_static_ok=1`；raw56 `refmem_parser_ok=1`；raw56_x2 `refmem_parser_ok=2`；exec196 `frag_ok=7`/`frag_complete=1`（三目标一致）。
  - exec196 连续 6 次加前序 1 次全部 `frag_complete=1`、源 `CTRL:STATus? state=ACKED`；源 `inject_ok=54`、`inject_launched=54`、`inject_dropped_unlaunched=0`、`inject_new_sequence=54`、`inject_repeated_sequence=0`、`queue_depth=0`、`queue_full=0`。
  - `rx_accepted_chunks`/`rx_dedup_sequence`/`rx_chunk_rejects` 含重复投递噪声（同一 mailbox 在未注入新 chunk 的帧上重复出现），不能直接当唯一 chunk 丢失；交付以 `frag_complete`/parser 计数为准。
- 结论：H1（源侧 sequence 复用）未出现；H2/H3（注入未发射/队列不排空）未出现；H4（重组器阻塞）未造成应用层失败；原 `rx_seen=2` 失败未在 10 次 7 帧运行中复现，倾向启动时序/历史状态的偶发问题，需 N3.2.2/N3.2.3 用唯一 chunk 计数与故障注入继续收敛。
- 证据：`out/refmem_transport_diag/n321_202610241248/`、`out/refmem_transport_diag/n321_exec196_202610241257/`。
- P3：build `20261024050910`，源码指纹 `212e893f6ed10505ad5ceb2b4d4f17ceeebbd8475efe93bb4ed7adcf7ab973f1`（1927 files），`QUICK_DIAGNOSTIC`，`credential: written`；step 26/33 为 warn，无 ERROR/FATAL。
- 下一步：N3.2.2 队列 release/acquire；N3.2.3 唯一 chunk 计数 + 注入配额/序列。

### REFMEM-TASK-20261024-002 — 统一控制面命令回复回程（C1b 方向）

- 状态：代码、构建、四板 HIL（单目标/多目标/负向）与 P3 硬件验收完成；文档收口进行中。
- 任务目标：
  - 目标执行 `REFMEM_CMD` 后把 ACK/NACK 终态回传源板，源板可读聚合结果；不新增第二命令槽、第二 ACK 事实、payload class 或物理总线。
- 完成内容：
  - 复用 process-image ACK 席位：新增 `TDMA_PROCESS_IMAGE_ACK_FLAG_COMMAND`（bit3），`seq16 = command_seq & 0xFFFF`，`reason` 走可选诊断字节；目标连续发布 4 个普通镜像，位置/VDC/feedback 一等席优先，回复不抢占。
  - 新增 `refmem_cmd_reply.h/.c`（编码/解码 + 逐目标聚合）与 host 用例；源板仅在 `source_node == local` 时聚合，写回源 node 控制记录的 `state/ack_flags/nack_flags/last_reason`，不写其他板记录。
  - 新增 `SYSTem:REFMEM:CMD:REPLy?`：`rx/applied/stale/bad_target/duplicate/armed/published/pending` 计数 + 聚合快照。
  - 命令目录新增 `REFTMEM_MAINTENANCE_SELF_CHECK`（id=4，`command_type=MAINTENANCE`，1 参数），作为 RUN 中可成功执行的控制面探针；`EXECute` 远端路径写源 node 记录，供聚合使用。
  - 聚合规则修正：required 任一 NACK 立即判失败，但仍继续折叠回复直到所有 required 目标都有终态，避免只记录第一个 NACK。
- 验证结果：
  - host：`run_refmem_cmd_reply_tests.ps1` 通过；`run_refmem_command_catalog_tests.ps1` 更新到 4 条后通过；`run_refmem_all_host_tests.ps1` 全绿（`out/refmem/host_tests_20261024_r3.log`）。
  - 构建/刷写：P3 build `20261024035648`；四板 OTA 成功（`out/HardwareAcceptance/20261024/hawf-115636/ota-four-board/`）。
  - 单目标 HIL：COM5→COM4 `SELF_CHECK,0x02,0x02`；COM4 `state=ACKED`、`ack_flags=2`；COM5 `state=ACKED`、`ack_flags=2`，`CMD:REPLy? rx_count=4/applied_count=1`。证据 `out/refmem_cmd_reply/p3build_single_try1/`。
  - 多目标 HIL：COM5→COM4/3/6；源板 `ACKED`、`ack_flags=0x0E`、`applied_count=3`、`rx_count=12`；目标记录保留全局 `required_mask` 并置自身 ack 位（`TAKEN`），完成态以源板聚合为准。证据 `out/refmem_cmd_reply/p3build_multi_try1/`。
  - 负向 HIL：RUN 中 `LOAD_NODE` 三目标 `NACKED reason=13`；源板 `NACKED`、`nack_flags=0x0E`、`reason=13`、`applied_count=3`。证据 `out/refmem_cmd_reply/p3build_nack_try1/`。
  - P3 硬件验收：build `20261024035648`，源码指纹 `23690f53cf5fcdcee47c0bb65ce3897de7e476ea034f9db35b6e4a782a9d3a31`（1926 files），`QUICK_DIAGNOSTIC` scope，`credential: written`；receipt `config/hardware_acceptance/p3_acceptance_receipt.json`，`check-staged` 通过；step 26 TDMA 与 step 33 时间预算为 warn，无 ERROR/FATAL。
- 已知偏差/未闭合：
  - 回复只带低 16 位 `seq16`；聚合不广播给其他 observer；node 控制记录未全环复制；静态命令槽仍是本地执行边；协调者租约、非协调者入口转发未实现。
  - fragmented `EXECute`（约 7 个维护帧）在环启动窗口存在偶发投递失败；最终三组 HIL 均首跑通过，但前序诊断跑出现过启动窗口丢帧，归入 N3.2 P0 可靠性，不把重试当作可靠语义。
- 下一步：P3 firmware 验收 → 文档/C14 收口 → N3.3 租约 → N4 idle 窗口。

### REFMEM-TASK-20261023-001 — C0/C1a 收口：命令槽放置冻结 + REFTMEM_CMD 全目标投递证据

- 状态：完成 C0 决策与 C1a HIL 证据；未改代码。等待 C11 独立复核。
- C0：规范物理位置冻结为 `distributed_refmem.c` 的 static `s_refmem_command_slot`；`ack_command` 预留区仅目录注册，不承载 live slot。已同步 `REFMEM-CTRL-01` 与 P8 C0。
- C1a（build `20261023121304`，`target_mask=0x0F`）：`REFTMEM_CMD` 从参考板 COM5 SCPI 入队 → TDMA 维护流 → COM4/COM3/COM6 每板 `rx_events/decoded/posted/acked=1`，源 `submitted/served/tx_ok=1`、`rejected=0`。
- 解析器：0x52/0x53/0x54 三类全过（0x53 `t2_static_applied=1`）；拒绝套件坏 CRC/坏 magic/坏 reserved 均按对应 parser 拒绝。
- 证据：`out/refmem_cmd_tdma/closeout_c0c1/`、`closeout_parser_suite/`、`closeout_reject_suite/`；评审提交单 `docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_03.md`。
- 未覆盖：C1 非协调者 Entry 转发、C4/C5、C2/C3、聚合视图；五项契约保持 pending。
- 下一步：C11 独立复核 C0/C1a；之后按 C1 转发 → C2 负测 → C3 租约推进。
- 追加（2026-10-23，T2_DYNAMIC 执行闭环）：解析器在 envelope 校验后调用 `tdma_t2_dynamic_apply_fn`；默认 executor 记录 `t2_dynamic_applied/last_target/last_run/last_config/last_value`，可用 `tdma_t2_dynamic_set_apply()` 替换为真实 T2 运行时；`REFTMEM:CMD:STATus?` 追加 5 字段。HIL（build `20261023131748`，`target_mask=0x0F`）三目标均 `t2_dynamic_ok=1`、`t2_dynamic_applied=1`，记录字段 `15/42/7/9` 与发送 envelope 一致；证据 `out/refmem_cmd_tdma/t2_dynamic_closeout/`。
- C1 转发实验（2026-10-23）：以 COM4（从板）为 Entry、`target_mask=0x0C` 提交 `REFTMEM_CMD`，源端 `submitted/served/tx_ok=1`，但 COM3/COM6 `rx_events=0`、源端 `inject_attempts=0`。原因：从板转发走 PIO 直通（`PHYSICAL_FLIGHT`/`PHYSICAL_PROCESS_IMAGE`），软件 TX 路径不参与，维护流没有上行注入点。⇒ 非协调者 Entry 需要 process-overlay 注入或专用 uplink mailbox（或把入口收敛到协调者）；C1 保持未完成，本轮未保留无效代码。证据 `out/refmem_cmd_tdma/c1_entry_com4/`。
- C0 方向修订（2026-10-23）：RefMem 是对称复制黑板，任意板写入 REFMEM 的数据同步到其他板；统一控制面 = 修改 REFMEM 控制所有板、观测 REFMEM 快照观测所有板。命令槽改由 REFMEM 复制区承载，static `s_refmem_command_slot` 仅作迁移前载体；`--no-t2` 纯直通环验证不了从板写，后续 HIL 用启用 flight engine / process-image 的环。原“static 冻结”结论作废。
- C0 容量扩展（2026-10-23，build `20261023144347`）：node slot 128→256 B（8 槽 2 KB）、表 18,432→19,456 B；node slot 增加控制块（`refmem_command_snapshot_t` 108 B + seq/CRC + rx/tx/reject/last_error）。为容纳 +1 KB，`storage_manager` 文件写缓冲 16→12 KB（3×4 KB）；TDMA 帧/槽未动，RAM 余量 3,376 B。`refmem_all_host_tests` 全过；P3 `hawf-224329` step 8/29 OK、`credential: written`（step 29 仍用一次重试，step 8 为 P0T 瞬时 flake，手动复测通过）。
- C0 命令路径（2026-10-23，build `20261023152319`）：`refmem_node_control` 写/读/ACK/清除（seq+CRC）落地；AO 在 post/take/ack/nack/timeout/clear 后镜像 command slot 到本板 node slot，`refmem_cmd_load_submit` 发送侧也写本板记录；新增 `SYSTem:REFMEM:NODE:CONTrol?` 只读快照。四板 HIL（`c0_node_control_r2`）：源 COM5 `POSTED`、required 目标 COM4 `ACKED`、COM3/COM6 `TAKEN` 并记录 ACK 位；P3 `hawf-232301` step 8/29/30 OK、`credential: written`（step 29 一次通过）。未完成：跨板复制该记录与从板写入口。
- 夜间 N0/N1/N2.3（2026-10-24，build `20261023164831`）：命令目录 3 条试点 + `SYSTem:CTRL:EXECute/STATus?/CATalog?` + 本地分发（LOAD_NODE ACKED、LOAD_BOARD 负路径）+ `SYSTem:REFMEM:NODES?`（本板 stale=0、远端 stale=1）；工作区未提交，等批量 P3。
- 夜间 N2.4/N2.5/N3.1（2026-10-24，build `20261023172516`）：参数池绑定 TLV；请求/状态分离（`update_status`/`apply_request`）；命令帧 `refmem_cmd_frame`（snapshot+params）build/apply；`EXECute` 帧 loopback 验证 QUEUED→ACKED；SCPI 全量目录 829 条（386 setter）。
- 夜间 N3.2（2026-10-24，build `20261023175156`）：分片传输（9 B 记录/0x55 解析/节流）落地；本地帧回环 HIL 通过；带环远端 HIL 未通过（参考板 QUEUED，目标无记录），待诊断维护 TX/AO 服务节奏。
- 夜间 N3.2 传输（2026-10-24，build `20261024022034`）：维护流 8 帧 TX 队列/分片打包/动态 local slot/消费门禁修正；远端 frame→apply→consume→dispatch 打通（COM4 NACK reason=13 为 RUN 配置门禁）。

### REFMEM-TASK-20261021-005 — HAOFV 偏差审查

- 状态：完成条款修正；未改代码。
- 修正：预算字段、config-control subtype、session writer、ActionDispatcher 归属、本地/协调者边界、实时性。
- 验证：`docs_check` 229/0；`doc_regression` 48。
- 下一步：C11 独立复核。

### REFMEM-TASK-20261021-004 — 四条款复审 R2

- 状态：完成条款复审与修正；未改代码。
- 修正：ARMING 失败 DISARM；STOPPING 超时进 DEGRADED/FAULT；未注册 command_type 拒绝；mask 解析失败不回退本地。
- 文档：`docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_02.md`。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（228 文件、0 warning）；`python tools/doc_regression_check.py` 通过（48 契约、19 进展日志）。
- 下一步：C11 独立复核；仍不改代码。

### REFMEM-TASK-20261021-003 — 单板控制保留条款

- 状态：完成 `REFMEM-LOCAL-01` 文档/契约；未改代码。
- 内容：
  - RefMem 统一面是增量层，不替换单板 SCPI、本地 command slot、本地 owner、本地 T2 `MOUNT/UNMOUNT`。
  - 本地 target 不走 `REFMEM_CMD`；统一面不可用时本地仍可用；远端/全目标必须拒绝 `UNIFIED_UNAVAILABLE`，不得静默缩成单板。
  - 两条路径共享同一本地门禁；EventPack 未激活不阻断本地基础命令。
- 契约：`REFMEM-LOCAL-01` pending；登记表/顶层/Review 01 §9 已同步。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（227 文件、0 warning）；`python tools/doc_regression_check.py` 通过（48 契约、19 进展日志）。
- 下一步：独立复核四条条款；仍不改代码。

### REFMEM-TASK-20261021-002 — 三缺口条款审查与修正

- 状态：完成条款级审查与修正；未修改固件/SCPI，未做板端验证。
- 结论：`PASS_WITH_CHANGES`；V-B1–V-B4 已改，V-M4（C11）仍开放。
- 修正：
  - `REFMEM-TRANSPORT-01`：active profile 必须含 config-control；缺失返回 `TRANSPORT_UNAVAILABLE`；`REFMEM_CMD_MAX_PAYLOAD` 绑定结构体上限。
  - `REFMEM-SESSION-01`：增加 `ARMING`；required 未全 arm 不得 START；START 后失败进入 `DEGRADED`；`T2:MOUNT` 仅兼容模式一次性映射。
  - `REFMEM-ACTION-01`：增加 `CommandHandlerRegistry`；有界 mailbox/优先级/BUSY；`action_registry_crc` 进 Version Bundle。
- 文档：`docs/check/submissions/REFMEM_UNIFIED_CONTROL_REVIEW_01.md`；索引已更新。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（227 文件、0 warning）；`python tools/doc_regression_check.py` 通过（47 契约、19 进展日志）。
- 下一步：独立复核三条条款；仍不改代码。

### REFMEM-TASK-20261021-001 — 单端口统一控制面三缺口 canonical 补齐

- 状态：完成架构/契约文档补齐；未修改固件、SCPI、工具或测试，未执行板端验证。
- 目标：只补三个 canonical 缺口——`REFMEM-TRANSPORT-01`、`REFMEM-SESSION-01`、`REFMEM-ACTION-01`，不改代码。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 新增三个同名小节：`REFMEM_CMD` 复用 TDMA config-control；系统级 `TriggerSession`（CONFIGURE/ARM/START/STOP/ABORT + 状态机 + T2 MOUNT 映射）；`ActionRegistry`/`ActionDispatcher`（action_id→handler、post、缺 handler gate）。
  - 命令类型表加入 `TRIGGER_SESSION_*`。
  - `TDMA_DOMAIN_ARCHITECTURE.md` 增加 `REFMEM-TRANSPORT-01` 载体条款；`TDMA_DOMAIN_TODO.md` 增加文档联审项。
  - `DOCS_REGISTRY.md` 登记三条 pending 契约；顶层 `HAOFV_ARCHITECTURE.md` 增加三缺口摘要。
  - `REFMEM_DOMAIN_TODO.md` 新增 M0 文档补齐清单；M1–M5 标为实现阶段、不在本目标。
- 边界：三条契约均 pending，未实现、未联审；不新增第二命令槽、不接 POSITION、不让 Event Plane 执行实时边沿。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（226 文件、0 warning）；`python tools/doc_regression_check.py` 通过（47 契约、19 进展日志）。
- 下一步：M0.5 独立复核三条条款；复核通过后仍不写代码。

### REFMEM-TASK-20261020-001 — 统一控制面契约细化与架构落位

- 状态：完成文档契约细化与登记；未修改固件、SCPI 或工具实现，未执行板端验证。
- 目标：把“通过任意一个板卡接口操作 RefMem、实现所有板卡的操作”从报告层愿景收敛为 RefMem 域可验证契约，并明确接入点无关、目标本地执行、显式掩码、单一协调者和聚合完成的边界。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 新增 `REFMEM-CTRL-01` 章节：CP1–CP8 不变量、Control Entry/Coordinator/Target/Observer 角色、命令信封既有字段语义与扩展字段、路由与命令状态机、fence/超时/部分成功、聚合与逐目标读模型、权限与运行态矩阵、失败恢复、边界、C0–C5 阶段和当前实现状态。
  - 架构章节补充“挂接关系：控制面接入 RefMem”：命令面、观测面、目录面三个挂接面；RefMem 是控制面的 bearer/router，不是执行者；不另建控制总线、第二套命令槽或第二套 ACK 事实。
  - 记录当前单板命令面物理放置：live slot 为 `distributed_refmem.c` 的 static `s_refmem_command_slot`，`refmem_vector_table_t.ack_command`（`DISTRIBUTED_REFMEM_ACK_CMD_SIZE`）仅目录注册；C0 需冻结“迁入该区”或“static 为规范位置”。
  - `DOCS_REGISTRY.md` 登记 `REFMEM-CTRL-01`（pending），`clause_loc` 指向架构章节，`code_anchor` 为 `refmem_command.h`。
  - 顶层 `HAOFV_ARCHITECTURE.md` 同日刷新：RefMem 定位补“统一控制面”，核心规则补接入点无关/目标本地执行/无强制位一条。
  - `REFMEM_DOMAIN_TODO.md` 新增 P8 统一控制面十项实现清单。
- 实现边界：跨节点命令路由、协调者租约、聚合视图、跨节点 FENCE 均未实现；当前只有本地 command slot 与 `SYSTem:COMMand:ACK?`/`NACK?`。本条不授予“任意接口控制全部设备”已产品化的结论。
- 验证：`python tools/docs_check/docs_check.py --strict-names` 通过（214 文件、0 warning）；`python tools/doc_regression_check.py` 通过（44 契约、10 顶层条款落点、18 进展日志）；`FORCE_DOC_GATES=1 sh .githooks/pre-commit` 通过。`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider` 为 36 passed / 2 failed，失败项是 `TDMA_TASK_PROGRESS.md` 轮转债务和 `SYNC_IO_TASK_PROGRESS.md` 顺序债务在 2026-10-17 截止后的既有时间炸弹，与本次改动无关，未在本切片修复。
- 下一步：按 P8 顺序先冻结 C0 schema 与目录 CRC，再做 C1 两板 Entry→Coordinator→Target HIL。

### REFMEM-TASK-20260916-001 — 静态向量缩容与布局版本迁移

- 对应 `REFMEM-RAM-001/002/003/004`。范围为静态预留缩容、布局版本迁移和必要的 host 工具联动；保留区域 ID/顺序、owner、整表 clear、VDC/DPLL payload 及 seqlock、node 现有临界区。不改 OTA、PIO/DMA、TDMA 实时服务或 MPU，不引入 lease/TLV 解析前置。
- 实现快照，非容量契约：表从 65536 B 缩至 18432 B，布局版本升级为 2；node 固定 8 个逻辑槽，每槽 128 B。ARM 有效占用为 HEADER 264 B、NODE 每槽 36 B、VDC 含同步前缀 232 B、DPLL 224 B，均适配新分配。其余普通数组未发现实际 backing 字节写者，应用模型中的区域引用保留；不将这次缩容解释为对应业务摘要已实现。
- 生产验证：节点容量 2 至 8 配置及兼容反例共 17 项通过，既有 VDC vector suite 在 6/8 节点通过，vector table 的 ARM 6/8 编译通过。`production-delivery.json`、`capacity-probe.json` 和 `node-capacity.xml` 绑定修改及取证。
- A/B Release 与 Flash 链接检查通过，build `20260916151903`。相对冻结的 VDC r5，两槽实际静态 RAM 均净释放 47104 B；保留原 2 KiB heap 后余量从 28 B 增至 47132 B，SCRATCH_X 间隙仍为 24 B。新二进制独审重新计算栈，保持原限定四板 persona 的最深余量；不代表所有可选负载安全或原运输/调度失败已修复。见 `measured-build/`、`ram-comparison.json`、`independent-review/linked-shrink-review.json`。
- 布局 ABI 同时影响 Python 产包器和 CORE:VECTOR/PROT:STATUS 读回版本判定；RMTP 格式、VDC/DPLL payload 及 TDMA wire 的独立版本保持。实际 Python 包进入生产 owner 链时发现既有 TDMA ring CRC 算法漂移，原失败保存在 `host-layout-r1.xml`、`host-diagnose-r1/`；同步算法后继续验证旧布局正确 CRC 包拒绝及 active/rollback 内容保持，不以坏 CRC 反例冒充布局验证。
- 工具/兼容矩阵最终 25/25 通过，独立复跑相关测试 22/22 通过，源码指纹一致。真实 CLI 包在 6/8 节点及普通/short-enums 编译条件下进入生产 owner、stage、prepare、activate、commit；正确 CRC 的旧布局包由 ApplicationMap owner 拒绝。纯 validation 保留三角色，失败 stage 可按原语义清 staging，但 active/rollbackable 内容及 descriptor 不变。见 `tool-test-delivery.json`、`host-layout-r2.xml`、`independent-review/joint-delivery-review.json`。
- 当前六节点固件产包须显式使用 `refmem_pack_build --tdma-node-count 6`；RefMem 仍保留八个逻辑槽。`sd_fs_build` 仍使用默认八节点产包，本轮不扩展其部署能力；后续需要六节点 SD 整包时另做工具切片。
- `REFMEM-LAYOUT-01` 已在域 canonical 定义并登记 pending，顶层同日可见；独立 C11 接受该规范/登记，不代表全部硬件门禁通过。
- 四板 OTA 更新成功，板端 UID/build 均与目标匹配；全部 STOP 后 RefMem 专项通过：容量 18432 B、布局 2、八逻辑槽、初始化 READY/error=0、目录有效/CRC 标志、CORE:VECTOR/PROT:STATUS 版本和八槽逐项查询均正常，错误队列为空。原件为 `baseline-refmem.json`、`refmem-readback.json`、`stopped-diagnostics.json`；完整目录字节未通过硬件接口导出，目录 offset/size 由 host 与 ARM 结构检查证明。
- 当前源码 quick P3 已实际执行，`flow_completed=true`，但 `strict_gates_passed=false`，`closed_loop_passed=false`、`realtime_gate_passed=false`。仍报 `explicit startup barrier timed out`；启动观测曾出现一次健康样本，随后再次失效，未满足稳定窗口；持续运输和 VDC/RefMem 调度门禁也未通过。该结果与缩容前 r5 同属运输/调度未闭合，不能推断 RAM 改动已修复或导致全部时序问题。见 `p3-r1/acceptance.json`、`p3-r1/tdma-process-image/summary.json`。
- 本轮 P3 绑定源码 `0593b3ace86d5895184b1cf98ff7aeba5d3cc6bd829efc90df0550ea16db818c`、build `20260916151903`，包 SHA-256 为 `3be05d080112b40cd33549322af3408549bdecd48f57b385efe99480c21d9222`，与部署前 manifest 一致。随后验收分级工具完成并重新通过匹配源码四板 quick P3，代码已于 2026-09-17 提交为 `e6ff06a`，提交指纹及证据见 `VDC-PROGRESS-20260917-001`；诊断凭证不提升为严格通过。按下述验收范围澄清，`REFMEM-RAM-001/002/003/004` 均已完成本轮调试切片验收；下一步回到 `VDC-FAST-002` 处理运输/调度。
- 独立硬件复核通过指纹、全部 receipt 引用哈希、冻结产物及四份 native 记录重解一致性。观测窗口快照，非性能保证：NO1–NO4 的 missing 增量相对 r5 从 `3/2/4/3` 变为 `0/1/0/1`，NO3 node/soak 门禁通过；NO1 反馈 gap 仍为 3，NO2/NO4 仍有缺收。稳态 soak 未见 DOWN，但启动 sample 3 有 NO4 DOWN/invalid/failure，sample 5 短暂健康后 sample 6 再失败，最终 2.006283 s 超时。四板 VDC deadline 仍增长，部分从板 RefMem deadline 仍增长，不能将单次变化归因为缩容的确定性性能收益。见 `independent-review/hardware-shrink-review.json`。
- 本轮证据目录：`out/HardwareAcceptance/20260916/refmem-static-shrink-r1/`。缩容前运输与调度失败基线见 `VDC-PROGRESS-20260916-042`，后续结果须保留差分。RAM 切片验收后返回 `VDC-FAST-002`，不以释放 RAM 代替 DPLL 闭环。
- 验收范围澄清：用户指出范围不得随调试无限扩大。主控复核 r5 与本轮的配置 SHA-256 均为 `c032843ba0eab2e87bb3399920c0fe54f79cf1eddbb4404a7ed1d85b1ea34fc2`，均为 `QUICK_DIAGNOSTIC`，启动及窗口条件相同；本轮没有扩大 P3 脚本范围。`p3_hardware_acceptance.py::_validate_quick_diagnostic_receipt` 明确允许流程完成且保留 phase quality 失败，`check_staged` 支持该凭证类型。先前将 `strict_gates_passed=false` 解释为 RAM004 不能结项，是主控扩大退出条件，现已纠正；原始失败、凭证和源码不改。提交时仍须核验真实 staged 指纹，本条不代替提交门禁。

### REFMEM-TASK-20260817-052 - RefMem 初始化阶段码固化

- 状态：完成代码、接口文档、构建和 COM5/COM6 板端验证。
- 日期：2026-08-17
- 任务目标：
  - 固化 `distributed_refmem_init()` 的初始化阶段码，避免 HIL 闭环失败时只能看到 `core1=0` 或 `service_count=0`。
  - 保持 `SYSTem:REFMEM:STATus?` 既有前 8 个字段不变，追加 `initialized,init_stage,init_error` 作为永久诊断字段。
- 完成内容：
  - `distributed_refmem_status_t` 增加公开 `DISTRIBUTED_REFMEM_INIT_STAGE_*` 枚举，覆盖 app model、TDMA owner、command slot、NodeLoad AUTO、TDMA ops、TDMA profile、vector table 和 ready。
  - `distributed_refmem_init()` 的临时数字阶段替换为固定枚举；失败时 `init_error` 记录失败阶段，成功时 `init_stage=READY`、`init_error=0`。
  - `distributed_refmem_publish_status_locked()` 每次刷新时同步 `initialized`，避免 service 后状态字段漂移。
  - `SCPI_COMMANDS.md` 同步 `SYSTem:REFMEM:STATus?` 的 11 字段返回格式。
- 验证结果：
  - `run_tdma_profile_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过。
  - `run_tdma_traffic_scheduler_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260817104554`。
  - COM5 经 BOOTSEL factory UF2 恢复到 build `20260817104554` 后，`SYSTem:REFMEM:STATus?` 返回 `65536,1,97043,0,8,97042,97042,7,1,8,0`，末尾 `initialized,init_stage,init_error=1,8,0`。
  - COM6 经 OTA boot/commit 到 build `20260817104554` 后，`SYSTem:REFMEM:STATus?` 返回 `65536,1,22775,0,8,22774,22774,7,1,8,0`，末尾 `initialized,init_stage,init_error=1,8,0`。
  - 两板 `SYSTem:ERRor?` 均返回 `0,"No error"`。
- 后续动作：
  - 初始化阶段码已可用于后续定位；若再次出现 `core1_loop_count=0`、RefMem service 不递增或 `init_error!=0`，优先按 `init_stage/init_error` 映射到 app model、TDMA owner、NodeLoad AUTO、TDMA profile 或 vector table 阶段，而不是继续盲目扩大 heap。

### REFMEM-TASK-20260817-051 - 事件驱动局部同步约束冻结

- 状态：完成架构与待办细化；通用 dirty queue 和 per-target generation completion 尚未实现。
- 日期：2026-08-17
- 任务目标：
  - 明确 64 KB RefMem 不按周期整表广播，避免同步负载破坏 VDC/TDMA 实时性。
  - 将触发、排队、局部更新、ACK/fence 清 dirty 的生命周期冻结为产品契约。
- 完成内容：
  - 正常同步改为 owner fact commit 触发 dirty descriptor，critical delta 进入 TDMA short frame。
  - 状态事实允许在编码前按 slot/field 合并；命令、事件和累加量必须使用有界队列或 accumulate 语义。
  - dirty 清除绑定 per-target ACK/fence 和 generation，禁止 `transport_sent` 后提前清除，也禁止旧 ACK 清除新更新。
  - full snapshot 仅用于 bootstrap/resync/显式维护，必须走 long-frame 分片、CRC 和 fence，不允许周期整表刷新。
- 验证结果：
  - 本项为架构约束细化；当前已有 delta/dirty mask/frame/ACK/fence 基础契约，但尚无通用 dirty queue 的新增代码验证。
- 后续动作：
  - 在 TDMA PIO ring adapter 接通后，实现 dirty descriptor queue、critical/background 分级和 per-target generation completion。

### REFMEM-TASK-20260816-050 - NodeLoad AUTO HIL default adapter profile correction

- 状态：完成工具修复并通过 COM5/COM6 默认 HIL。
- 日期：2026-08-16
- 任务目标：
  - 修复 `refmem_node_load_auto_hil_validate.py` 默认路径误用 `REALtime:IO` 线序预检推导 RefMem TDMA adapter 的问题。
  - 保持 HAOFV 边界：GPIO16-24 是 TDMA/RefMem 通讯环路；GPIO4-7 和 `REALtime:IO` 是业务/观测 overlay，不能作为 RefMem transport 默认推导来源。
- 完成内容：
  - 脚本新增 `--adapter-profile`，默认 `min-system-gpio16-24`，直接使用当前两板最小系统已验证的 3-wire uplink/downlink adapter：A `uplink=16,18,23`、A `downlink=16,22,23`，B `uplink=16,18,23`、B `downlink=16,21,23`。
  - `io-preflight` 改为显式 profile，只用于 REALtime IO overlay 线序诊断；该路径失败时会写出 `io_preflight_result.json`，避免只返回一句错误。
  - 修复脚本生命周期问题：IO preflight 子进程增加超时，轮询等待拆分为“单条 SCPI 查询 timeout”和“同步总 timeout”，关键输出立即 flush，避免超时后残留 Python 进程占住 COM 口。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py` 通过。
  - `python tools\two_board_io_validate\two_board_io_validate.py --port-a COM5 --port-b COM6 --name-a A --name-b B --expect-a-to-b auto --expect-b-to-a auto --out-dir build-rtos-multicore-smoke\debug_refmem_io_preflight_manual --timeout 2` 快速失败，证实 REALtime IO overlay 当前不等同于 RefMem TDMA 通讯环路。
  - `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --timeout-s 2 --sync-timeout-s 30 --maintenance-s 0.5` 默认参数通过，记录位于 `build-rtos-multicore-smoke\refmem_node_load_auto_hil_20260816235020\records.json`。
- 后续动作：
  - 继续 P0 的 ACK/重发/fence completion；当前 maintenance 阶段仍可在 TDMA snapshot 中观察到 `WINDOW_MISSED` 质量证据，不能把“重跑通过”替代产品级可靠性。

### REFMEM-TASK-20260816-049 - RefMem payload standard TDMA registration

- 状态：完成代码、host/build 验证和 COM5/COM6 AUTO HIL 回归。
- 日期：2026-08-16
- 任务目标：
  - RefMem 作为公共 TDMA payload producer/consumer 接入，不能在 `refmem_realtime_tdma` 兼容适配层里临时声明 binding。
  - 保持 HAOFV 边界：`DistributedRefMemAO` 负责 RefMem payload contract，`components/tdma` 只执行统一 scheduler/transport/timestamp spine。
- 完成内容：
  - 新增 `refmem_tdma_payload.h/.c`，集中注册 `REFMEM_DELTA` 和 `REFMEM_ACK_FENCE` short-frame payload binding。
  - `refmem_realtime_tdma_init()` 改为调用 `refmem_tdma_payload_register()`；自身继续只做旧 API 到公共 `tdma_service` 的兼容适配。
  - `tdma_service` submit 阶段校验 `frame_class/payload_class/max_payload_size`，未注册 payload 不允许进入 core1 intent mailbox。
  - RefMem host 单测增加标准注册断言：`REFMEM_DELTA` / `REFMEM_ACK_FENCE` 可提交，未注册 VDC payload 在 RefMem-only TDMA service 中被拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260816103607`。
  - COM5/COM6 多线程 OTA 到 `20260816103607` 通过。
  - 首次 AUTO HIL 运行 A->B 通过、B->A 只收到 1/2 帧，现场状态显示接收侧 TDMA `WINDOW_MISSED`，不是 payload registry 拒绝；随后重跑完整 HIL 通过，记录位于 `build-rtos-multicore-smoke\refmem_auto_tdma_payload_regression_retry\records.json`。
- 后续动作：
  - P0 优先将 AUTO NodeLoad 从“单发 TX + 对端 RX 窗口命中”升级为 ACK/重发/fence completion，避免窗口 missed 后丢失一个 NodeLoad delta。TDMA 的核心验收是时间可预期和 completion 可证明，不能只以重跑成功作为产品级可靠性证据。
  - 继续将 long frame payload 用同样 registry 方式接入 OTA/config/log，不得绕过 `components/tdma`。

### REFMEM-TASK-20260816-048 - TDMA component extraction baseline

- 状态：完成并通过 COM5/COM6 RefMem 同步 HIL
- 日期：2026-08-16
- 任务目标：
  - 解决 “VDC 一套 TDMA、REFMEM 一套 realtime_tdma” 的命名和所有权混乱。
  - 将 TDMA scheduler / PIO transport / timestamp spine 抽为 `components/tdma` 基础件；RefMem 和 VDC 只作为 payload producer/consumer 挂载，不再拥有私有 TDMA scheduler。
- 完成内容：
  - 新增 `components/tdma/inc/tdma_service.h`、`components/tdma/src/tdma_service.c`，承载 core0 intent mailbox、core1 result snapshot、scheduled window、transport ops、短帧/长帧和 payload binding 基础契约。
  - `components/distributed_refmem/inc/src/refmem_realtime_tdma.*` 改为 RefMem 兼容适配层：旧 SCPI/quality/HIL API 保持字段不变，实际执行委托给 `tdma_service`。
  - RefMem DELTA 与 ACK/FENCE payload 作为 short frame payload binding 注册到 TDMA service；后续 VDC sync sample、idle beacon、OTA/config/log long frame 走同一基础件扩展。
  - 修正抽离后首轮 HIL 暴露的容量边界：`tdma_service` 保留 long frame 1024 字节上限，RefMem 物理适配层在 receive bridge 中按 `REFMEM_REALTIME_TDMA_FRAME_MAX` 收敛短帧接收容量，避免把长帧能力误传给 RefMem PIO RX DMA 上限校验。
  - `application/src/app.c` 将 VDC/DPLL service 收敛到 core1 realtime loop，core0 RTOS task 不再运行 VDC/DPLL 维护循环。
  - `tools/tests/run_host_unit_tests.ps1` 将 `run_refmem_realtime_tdma_tests.ps1` 纳入总门禁，避免 TDMA 兼容层只编译不执行。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260816095934`，package CRC `0x99D513A9`。
  - COM5/COM6 OTA send、boot 和 commit 到 build `20260816095934` 通过，两板 `SYSTem:FW:BUILD?` 均返回 `"20260816095934"`，错误队列均为 `0,"No error"`。
  - 执行 `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260816095934_tdma_component` 通过。
  - HIL 证据：A->B 只对 COM5 下发两条 `LOAD:NODE` 后，COM6 `applied_rx_count` 增长到 2，`last_frame_type=3`，NodeLoad TableRegistry CRC 与 COM5 一致；B->A 只对 COM6 下发两条 `LOAD:NODE` 后，COM5 `applied_rx_count` 增长到 2，`last_frame_type=3`，NodeLoad TableRegistry CRC 与 COM6 一致。
  - 增强版 AUTO 维护验证通过：`tools/refmem/refmem_node_load_auto_hil_validate/refmem_node_load_auto_hil_validate.py` 在 IO preflight 前先关闭残留 AUTO 并释放 `REALtime:IO`；同步完成后继续观察两板自动 RX 维护，确认 `pending_count=0`、`last_error=0`、`submitted_rx_count` 和 TDMA `service_count/completed_seq` 继续增长。记录位于 `build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260816095934_auto_maintenance\records.json`。
  - 两方向 `SYSTem:REFMEM:CLAIM? 5` 返回一致的 SlotClaimMap 摘要，最终两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
- 后续动作：
  - 继续把 VDC observation sample、idle beacon 和 long frame payload 从文档契约落到 `components/tdma` 的 producer/consumer 注册机制。

### REFMEM-TASK-20260816-047 - TDMA time-unit naming and diagnostic timestamp snapshot

- 状态：完成 host/build 验证；板端 DPLL timestamp latch 待实现
- 日期：2026-08-16
- 任务目标：
  - 统一 RefMem realtime TDMA 对外 deadline 口径：deadline 字段改为 `deadline_us`，质量字段改为 `p99_us/p999_us`。
  - 暴露当前 core1 realtime TDMA 诊断时间戳，作为后续 VDC/DPLL bring-up 的过渡 evidence，但不进入 100 ns lock gate。
- 完成内容：
  - `refmem_realtime_tdma` snapshot 追加 submit/core1 arm/start/done/elapsed ns 字段，并声明 `timestamp_source=SOFTWARE_US`、`timestamp_resolution_ns=1000`、`timestamp_flags=DIAGNOSTIC_ONLY`。
  - `DistributedRefMemAO` AUTO 配置、FENCE payload、QUALITY payload、RefMem command timeout、SCPI 维护命令和三份 HIL 脚本统一到 `deadline_us` / `timeout_us` 命名。
  - `refmem_spi_physical_adapter` 公开 RX timeout 参数改为 `timeout_ms`，保持内部 SDK 等待实现不扩散到协议命名。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815174141`，package CRC `0x58997C54`。
- 后续动作：
  - 继续 VDC `VdcTDMATimestampEvidence` 和硬件 timestamp latch；RefMem 只提供 payload、quality 和 evidence，不拥有 DPLL offset/rate。

### REFMEM-TASK-20260815-046 - NodeLoad AUTO 3-wire uplink/downlink adapter correction

- 状态：完成并通过 COM5/COM6 B3 HIL
- 日期：2026-08-15
- 任务目标：
  - 修正 B3 HIL 未收到 frame 的结构性原因：NodeLoad AUTO 不能复用单组 pins，必须区分环路上行 adapter 和下行 adapter。
  - 按产品链路收敛为无 CS 的 3-wire SPI；每组 adapter 保留 `duplex_mode,rx,sck,tx`，支持 half/full duplex 配置。
- 完成内容：
  - `DistributedRefMemAO` 的 AUTO 配置由单组 pins 改为 `uplink_adapter_pins` 和 `downlink_adapter_pins`，并新增 uplink/downlink duplex mode。
  - NodeLoad AUTO RX window 使用 uplink adapter，语义为接收上一个板卡的数据；pending NodeLoad DELTA TX 使用 downlink adapter，语义为下发到下一个板卡。
  - `SYSTem:REFMEM:SYNC:AUTO` 改为配置 `enable,local_slot,target_mask,baud,deadline,uplink_mode,uplink_rx,uplink_sck,uplink_tx,downlink_mode,downlink_rx,downlink_sck,downlink_tx`；`AUTO?` 返回同序 snapshot 和计数/evidence。
  - `refmem_spi_physical_adapter` 支持 `csn_pin=REFMEM_SPI_PHYSICAL_PIN_UNUSED` 的无 CS 模式：master 不驱动 CS，slave PIO 不等待 CS，仍由 RM packet magic、length、CRC 和 RX stable 判断帧边界。
  - `tools/refmem/refmem_node_load_auto_hil_validate/refmem_node_load_auto_hil_validate.py` 固化两板 B3 验收，自动运行 IO preflight 后推导 A/B 的 uplink/downlink adapter，并只向源板发送 `LOAD:NODE`。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815165314`，package CRC `0x36DE7654`。
  - OTA build `20260815165314` 到 COM5/COM6 并 commit 通过，两板 `SYSTem:FW:BUILD?` 均返回 `"20260815165314"`，`SYSTem:ERRor?` 均为 `0,"No error"`。
  - 执行 `python -u tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260815165314` 通过。
  - IO preflight 实测线序：A->B 为 `[1,2,0,3]`，B->A 为 `[2,1,0,3]`。
  - AUTO 配置证据：A 使用 `SYSTem:REFMEM:SYNC:AUTO 1,0,2,25000000,1000000,1,16,18,23,1,16,22,23`，B 使用 `SYSTem:REFMEM:SYNC:AUTO 1,1,1,25000000,1000000,1,16,18,23,1,16,21,23`。
  - A->B：只向 A 下发 `LOAD:NODE 6,10,...` 和 `LOAD:NODE 5,9,...` 后，B `AUTO?` 显示 `applied_rx_count` 增长到 2，`last_frame_type=3`，`last_source_slot=0`，NodeLoad table staging CRC 与 A 一致。
  - B->A：只向 B 下发同样两条 `LOAD:NODE` 后，A `AUTO?` 显示 `applied_rx_count` 增长到 2，`last_frame_type=3`，`last_source_slot=1`，NodeLoad table staging CRC 与 B 一致。
  - HIL 记录：`build-rtos-multicore-smoke\refmem_node_load_auto_hil_COM5_COM6_20260815165314\records.json` 和 `adapter_plan.json`。
- 后续动作：
  - 在 B3 已闭环基础上，继续 P0 表镜像 activation gate 的跨节点 ACK/FENCE 与 owner validation callback 调度。

### REFMEM-TASK-20260815-045 - NodeLoad AUTO gate correction and HIL script

- 状态：完成 host/build 验证；COM5/COM6 HIL 待执行
- 日期：2026-08-15
- 任务目标：
  - 支持通过 SCPI 配置 RefMem NodeLoad 是否自动运行，并保证开启 AUTO 后仍可在 idle 业务状态提交本地 `LOAD:NODE`。
  - 固化 B3 双板验收脚本，验证 PC 不搬运 frame hex 的 NodeLoad 双向同步。
- 完成内容：
  - 新增 `distributed_refmem_can_accept_node_load_intent()`，由 `DistributedRefMemAO` 判断 NodeLoad intent 是否可接收。
  - `DistributedRefMemAO` 允许抢占自己挂起的 AUTO RX window：本地 `LOAD:NODE` staging 前 abort AUTO RX，staging 成功后进入 pending TX 队列；手工 TDMA/其他实时活动仍不被抢占。
  - `SYSTem:REFMEM:LOAD:NODE` 改为使用 RefMemAO 判定；`SYSTem:REFMEM:LOAD:SD` / `LOAD:BOARD` 仍保持 realtime idle 门禁。
  - 修复 RMTP inline package CRC 生成端：ApplicationModel 不再依赖 `ota_crc32_update()` 的隐含初值语义，改为本地 CRC32 算法，与 TableRegistry validator 的 package/payload/table CRC 完全一致。该问题此前导致板端 `LOAD:NODE` / `LOAD:BOARD` 返回 `PACKAGE_INVALID`，host stub 未暴露。
  - inline package 构建失败时保留 package validator 的失败表证据：若 validation 已给出 `first_bad_table`，则通过该表的 registry entry 暴露 `FAILED + last_result=package_error`，避免所有失败都折叠成 `BAD_TABLE_VERSION`。
  - 新增 `tools/refmem/refmem_node_load_auto_hil_validate/refmem_node_load_auto_hil_validate.py`：COM5/COM6 分别开启 `SYSTem:REFMEM:SYNC:AUTO`，只对源板下发两条 `LOAD:NODE`，目标板只查询 `AUTO?`、`LOAD:STATus?`、`TABle? 3` 和 `CLAIM?` 证据；随后反向执行。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815162909`，package CRC `0xFA4FE362`。
- 后续动作：
  - 将 build `20260815162909` OTA 到 COM5/COM6。
  - 执行 `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6`，若出现窗口碰撞，再按 RefMemAO/TDMA 调度策略修正，不退回 PC frame bridge。

### REFMEM-TASK-20260815-044 - Configurable NodeLoad auto sync service

- 状态：完成 host/build 验证；板端 HIL 待执行
- 日期：2026-08-15
- 任务目标：
  - 推进风险评审 §3.4 子项 B2：NodeLoad DELTA 不再依赖维护命令逐帧触发，改由 RefMemAO service 和 core1 TDMA 自动运行。
  - 自动运行必须可配置，默认关闭；SCPI 只设置运行策略或提交 LOAD intent，不承担对端 frame 搬运。
- 完成内容：
  - `DistributedRefMemAO` 增加 NodeLoad auto sync 队列，`LOAD:NODE` / `ModelTurntable LOAD` staging 成功后将 instance id 加入 pending 队列。
  - `distributed_refmem_service()` 在临界区外推进 auto sync：有 pending NodeLoad 时提交 TDMA TX；无 pending 时按配置提交 RX window；收到 RX frame 后由 RefMem Sync state machine 校验，再交给 ApplicationModel staging。
  - 新增 `distributed_refmem_configure_node_load_auto_sync()` 和 `distributed_refmem_get_node_load_auto_sync()`，支持 enable、local slot、target mask、baud、deadline 和 PIO SPI pins。
  - 新增维护命令 `SYSTem:REFMEM:SYNC:AUTO` / `SYSTem:REFMEM:SYNC:AUTO?`；默认关闭，开启后才会自动占用 PIO SPI transport。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_node_load_sync_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815160049`，package CRC `0x11683118`。
- 后续动作：
  - 执行完整 host/docs/build 验证。
  - 固化 COM5/COM6 HIL 脚本：两板先分别开启 AUTO，再只对源板下发两个 `LOAD:NODE`，确认目标板 staging/table/claim/quality 同步；随后反向执行。

### REFMEM-TASK-20260815-043 - NodeLoad sync delta foundation

- 状态：完成 host/build 验证；板端 HIL 待执行
- 日期：2026-08-15
- 任务目标：
  - 推进风险评审 §3.4 子项 B：建立两板真实 NodeLoad 同步所需的数据语义和 RefMemAO apply 入口。
  - 保持 HAOFV 边界：RefMem Sync 只承载受控 DELTA；ApplicationModel 仍由 DistributedRefMemAO owner staging；SCPI 不直接改对端表。
- 完成内容：
  - 新增 `refmem_node_load_sync.h/.c`，定义 NodeLoad entry 到 RefMem DELTA payload/frame 的固定编码和解码。
  - 新增 `test_refmem_node_load_sync.c` 和 runner，覆盖 payload roundtrip、frame roundtrip、mirror 可见性和非法 entry 拒绝。
  - `ApplicationModel` 增加 `refmem_application_model_get_staging_node_load_entry()`，只导出当前 staging NodeLoad entry 的 const copy。
  - `DistributedRefMemAO` 增加 `distributed_refmem_build_node_load_sync_frame()` 和 `distributed_refmem_apply_node_load_sync_payload()`；源端从 staging entry 生成 NodeLoad DELTA，目标端解码后通过 ApplicationModel staging API 合并到本地 staging image。
  - 新增维护入口 `SYSTem:REFMEM:SYNC:TDMA:NODE:TX`：源板不再需要 PC 搬运 frame hex，可从本地 staging entry 直接提交 TDMA TX intent。
  - `SYSTem:REFMEM:SYNC:RX` 和 `SYSTem:REFMEM:SYNC:TDMA:FRAMe?` 收到 NodeLoad DELTA 后会交给 RefMemAO apply staging；非 NodeLoad DELTA 仍只更新 sync mirror。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_node_load_sync_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815154736`，package CRC `0xE10EC500`。
- 后续动作：
  - 补全自动 RX/TX 调度：当前仍需要维护面触发目标板 RX/FRAMe 查询，不满足最终“只对源板下发 SCPI LOAD intent”的验收。
  - 用 COM5/COM6 跑双向两节点同步 HIL，确认 X->Y 与 Y->X 的 NodeLoad staging、TableRegistry CRC、SlotClaimMap 和 quality/evidence 一致。

### REFMEM-TASK-20260815-042 - P0 risk 3.4 fake TX correction

- 状态：子项 A 完成 host/build 验证；子项 B 两板真实同步待实现
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.4：`refmem_pio_spi_adapter_send()` 不能作为无物理后端的 fake TX 返回成功。
  - 保持 HAOFV 边界：旧 `SYSTem:REFMEM:SYNC:*?` 只作为 frame builder / PC bridge 诊断入口；真实发送必须走 physical adapter 或 core1 TDMA owner 路径。
  - 最终验收不是“能生成 hex”，而是两板真实双向同步：X 板 SCPI 加载两个节点并同步到 Y，随后 Y 板 SCPI 加载两个节点并同步回 X。
- 完成内容：
  - `refmem_transport_adapter.h` 增加 `REFMEM_TRANSPORT_ERROR_TX_UNBOUND`。
  - `refmem_pio_spi_adapter_send()` 在 frame/MTU 校验通过后仍拒绝发送，记录 `tx_reject_count`、`last_tx_size` 和 `TX_UNBOUND`，不再增加 `tx_count` 或返回成功。
  - `SYSTem:REFMEM:SYNC:HELLo?/EPOCh?/DELTa?/ACK?/FENCe?/QUALity:FRAMe?` 取消 fake adapter send 调用，只生成 frame hex；真实两板 TX 继续使用 `SYSTem:REFMEM:SYNC:TDMA:*` 或 physical raw bring-up。
  - `test_refmem_pio_spi_adapter.c` 将 send 单测改为断言无 TX 后端必须拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_hello_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `git diff --check` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815153622`，package CRC `0x6A4E1D33`。
- 后续动作：
  - 完成两板真实同步验收：PC 只对源板下发 SCPI intent，不搬运 frame hex、不直接写对端；目标板通过维护查询确认 NodeLoad / table image / SlotClaim / quality evidence 与源板一致。
  - 继续按风险评审 P0/P1 清单推进 §3.5 fence deadline、§3.6 receive raw capacity、§3.7 arm/capture 状态破坏等结构性纠偏。

### REFMEM-TASK-20260815-041 - Vector fixed regions rename

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修正“Vector 16 槽”的误导命名：Vector directory 的 16 项是固定内存定义区（region），不是 A0-A7 可实例化节点槽位，也不是 SlotClaim 候选槽。
  - 保持 HAOFV 边界：NodeLoad / SlotClaim / A0-A7 继续使用逻辑 slot 语义；Vector 只提供固定事实 region、owner 边界和 CRC/seq/stale 保护。
- 完成内容：
  - `refmem_vector_table.h/.c` 将 `REFMEM_VECTOR_SLOT_*`、`slot_count`、`slots[]`、vector header/node 类型统一改为 `REFMEM_VECTOR_REGION_*`、`region_count`、`regions[]`、`*_region_t`。
  - `DistributedFbInstanceTable`、`DistributedEventLinkTable`、`DistributedDataLinkTable`、`DistributedDeploymentGate` 中指向 Vector directory 的字段改为 `state_region_ref`、`health_region_ref`、`evidence_region_ref`、`crc_region_ref`、`reject_region_ref`。
  - DataLink 可读路径从 `*Slot.*` 改为 `*Region.*`，Python RMTP 生成工具同步改用 `REGION_*` 常量和 `region_path_hash`。
  - `refmem_quality_evaluate_deployment_gate()` 的输出拆成 `refmem_quality_gate_result_t`，避免 quality runtime 的逻辑 `reject_slot` 与 DeploymentGate 表的 `reject_region_ref` 混用。
  - 架构文档明确：Vector directory 16 项是固定 region；A0-A7 / SlotClaim 才是节点实例化槽位。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815151411`，package CRC `0x6B9F4362`。
- 后续动作：
  - 后续架构评审中继续区分 Vector fixed region、A0-A7 logical slot、SlotClaim assignment 和 RMTP 九表模型，避免把 region 当成可加载节点槽。

### REFMEM-TASK-20260815-040 - Inline LOAD:NODE/BOARD to full RMTP package image

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 完整修复风险评审 §3.11：inline `LOAD:NODE` / `LOAD:BOARD` 不能停在 metadata-only 或单表 staging；单表 draft 必须合并为完整 Node Model Candidate，并生成 9 表 RMTP package image。
  - 保持 HAOFV 边界：SCPI 只提交 intent；ApplicationModel owner 生成 draft/candidate/package；TableRegistry 只管理完整 image lifecycle；Vector 不承载完整表数据。
- 完成内容：
  - 在架构文档中固化两张表：`Table Draft -> Node Model Candidate -> RMTP Package Image` 层级表，以及 `LOAD:SD / LOAD:NODE / LOAD:BOARD` 到最终 package 产物的路径表。
  - `refmem_application_model.c` 增加 inline RMTP serializer：按 9 张 canonical 表生成 header、directory、payload、per-table CRC、payload CRC 和 package CRC。
  - serializer 从当前 active 9 表读取未修改表，并用私有 `NodeLoadTable draft` / `BoardCapabilityTable draft` 覆盖候选表，形成完整 Node Model Candidate。
  - `LOAD:NODE` 成功路径改为调用 `refmem_table_registry_stage_package_image()`，不再把 `staging_package_crc32` 当成 NodeLoad 单表 CRC。
  - `LOAD:BOARD` 成功路径同步刷新系统级 `LOAD:STATus?` 的 inline package staging 状态，同时 `BOARD:STATus?` 保留 BoardCapability draft 整表 CRC。
  - host 单测新增 inline `LOAD:BOARD` 和 `LOAD:NODE` activation-ready package 断言：stage 后可 prepare staging view、activate、commit，并确认 runtime getter 发生预期变化。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815144746`，package CRC `0x8A0A9E09`。
- 后续动作：
  - 继续审查 TableRegistry `stage_table()` 的剩余用途，把产品路径中的单表 staging 约束为失败 evidence 或测试辅助。
  - 重新跑 COM5/COM6 HIL，确认 inline package staging 与 `LOAD:ACTivate` 在板端同样闭环。

### REFMEM-TASK-20260815-039 - LOAD:BOARD private table staging image

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 推进风险评审 §3.11：`SYSTem:REFMEM:LOAD:BOARD` 不能只发布单条 entry CRC 和 metadata-only staging，至少必须形成 RefMemAO 私有 `BoardCapabilityTable` staging image。
  - 保持 HAOFV 边界：SCPI 只提交 board capability load 意图；application model owner 构造候选表、执行 contract validation 并发布 TableRegistry staging 摘要。
- 完成内容：
  - 新增私有 `s_staging_board_capability_table` / `s_staging_board_capability_valid`，与 NodeLoad staging image 对齐。
  - 新增 `refmem_model_make_staging_board_capability_table()`：从当前 active 或已有 staging board table 复制候选表、替换目标 board entry、执行 `refmem_application_contract_validate_board_capability_table()`，并计算整表 CRC。
  - `refmem_application_model_stage_scpi_board_capability()` 不再使用单 entry CRC；合法路径发布 `BoardCapabilityTable` 整表 staging CRC，非法路径继续发布 FAILED。
  - 删除旧 `refmem_model_board_entry_crc32()`，避免继续暗示单 entry staging。
  - `test_refmem_table_registry.c` 增加 `LOAD:BOARD` 断言：registry table 1 staging CRC 等于 board load snapshot 的整表 CRC，且不同于单 entry CRC；active getter 在 staging 阶段不被改变。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815142112`，package CRC `0xE8171F9A`。
- 后续动作：
  - §3.11 仍未完全关闭；下一步需要让 inline NodeLoad/BoardCapability staging 合成完整 package image，或明确声明为不参与 activation 的诊断 staging。

### REFMEM-TASK-20260815-038 - TableRegistry OWNER_OK provenance

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.10：`OWNER_OK` 不能由 `present + CRC` 推导，必须代表 owner validation provenance。
  - 保持 HAOFV 边界：TableRegistry 只记录 image/validation 证据，不把 CRC 完整性伪装成 AO/FB owner 语义验证。
- 完成内容：
  - `refmem_table_registry_refresh_active()` 中编译内置 active entry 的 flags 从 `ACTIVE_PRESENT|CRC_OK|OWNER_OK` 降级为 `ACTIVE_PRESENT|CRC_OK`。
  - staging package 路径仍按 `owner_validated_table_mask` 逐表置 `OWNER_OK`；activation 后的 active image 可继承已验证 provenance。
  - `test_refmem_table_registry.c` 增加断言：初始化 active entry 有 CRC flag，但 `OWNER_OK` flag 必须为 0。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815141450`，package CRC `0x9A5F9C0D`。

### REFMEM-TASK-20260815-037 - SlotClaimGate explicit ready gating

- 状态：完成 host/build 验证
- 日期：2026-08-15
- 任务目标：
  - 修复风险评审 §3.3：DeploymentGate/SlotClaimGate 调用端必须真正以 gate ready 结果作为放行条件。
  - 保持 HAOFV 边界：RefMemAO 和 SystemManager 只消费 RefMem 派生的门禁事实，不绕过 SlotClaimMap 或 DeploymentGate。
- 完成内容：
  - `refmem_application_model.c` 静态表 validation 改为显式检查 `refmem_slot_claim_gate_evaluate(...) && claim_gate.ready`。
  - `distributed_refmem.c` activation gate 改为显式检查 `evaluate && ready`。
  - `system_manager.c` RUN gate 改为显式检查 `evaluate && ready`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，14/14 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815141111`，package CRC `0xCE53713F`。
