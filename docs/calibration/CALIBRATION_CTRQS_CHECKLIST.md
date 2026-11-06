# 单口引导 CTRQS 交接（新会话入口）

Status: Active
Domain: calibration / refmem / tdma
Canonical: `docs/calibration/CALIBRATION_CTRQS_CHECKLIST.md`（本文）
Related: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`
Last updated: 2026-11-06

## 0. 新会话交接摘要（先读这一节）

**当前结论**：七动作阶梯 **P1→P2→P3→T0→T1→T2→T3 已在 10 MHz 档四板全部跑通**（夜间目标 M1–M5）；
**10 kHz 引导档**的命令面已修好并实板验证（CONFig 51 s、TRIGger 78 s 四板一致），
四板 P1 在 10 kHz 下 **三块跟随板 COMPLETE、参考板 TIMEOUT**（连续 2 次），
因此四板 10 kHz 全链与 M6 设计档位收口仍未完成。

**已修正的根因（推翻旧交接的"丢包/无重试"判断）**：10 kHz 下命令面不是丢失而是**吞吐**——
维护通道每环回只送 1 个 10 B chunk，一条 216 B 的 GUIDE CONFig 需要 54 chunk
（= 7 个维护帧，实测 49–51 s），目标板在最后一 chunk 后的同一环回**同时**应用（无板间错位）。
旧判断的三处瑕疵：维护层本身有每帧 3 次重试与按环周期缩放的 ACK 预算；分片路径此前确实
没有 arm deadline；`s_pending_*` 逐步队列是从未启用的死代码。

**已修并入库**（`5a2a4849`）：`tdma_maintenance_runtime_delivery_budget_ms()` 按
`feedback_timeout_ns` 推导投递预算（10 kHz ≈57 s、10 MHz 亚秒）；分片路径 arm issuer deadline；
required ACK 位未齐时做**一次有界重放**（同 `command_seq`、新 fragment `frame_seq`，目标侧按 seq 幂等，
重放前要求维护队列/ACK 槽静默），仍失败则 `TIMED_OUT` 失败关闭；删除死队列。
P3 凭证 build `20261106101617`（PASS，0 ERROR/1 WARN）。实板复测含一次真实丢帧重放。

**当前唯一阻塞**：参考板（NO.1/slot 0）P1 窗口 `progress_status=2, trials_done=0,
reason=11(TIMEOUT), reconcile=3`，跟随板同期 `COMPLETE, trials=4, deepest=3`；
同一 build 连续两次完全一致（旧文档记为"偶发、重触发即成功"，前提已变）。
按 §7.4 已开只读子代理交叉分析，未裁决前不得改动该路径。

**已完成并可直接复用**：
- 10 MHz 七动作全链（四板）：`48b51f5c`（T3 复用 REBUILD 成环路径），证据见 §4 与 `f616ecef`。
- 低速率引导环：`TOPO → RING:BRINGUP 1 → ARM → START`（BRINGUP 必须在静默屏障后设置；
  T3/10 MHz 不得设置）。
- 速率缩放：`app_ctrqs_staged_period_ms()`/`app_ctrqs_scaled_ms()`、t0=6 s+80×period、
  FORWARD formed 驱动（`3bc8299d`/`fec92c17`/`0ef00a67`）。
- 10 kHz 四板 P1 的 C/T 段：菜单见 §7.1，含确认协议与实测耗时。

**新会话第一步建议**：
1. 读本文 §2 硬约束、§5 残余、§6.1 动作布局、§7.1 复现配方。
2. 先处理参考板 P1 TIMEOUT（§5 残余 4），再跑四板 10 kHz P1→P2（成环/测量分离）→P3→T0–T2；
   最后 S 语义与 M6。
3. 工作树里 `docs/validation/P3_FLOW.html` 的未提交改动属并发会话，勿误提交。

**关键命令/字段**（详见 §6/§7）：
`SYST:TDMA:GUIDE:CONFig <action>,<crc>,<gen>,<level>,<window>,<trials>,<count>,<c0..4>,<lo>,<hi>`；
`TRIGger <action>,<epoch>`；`PLAN?` 共 22 字段（9 计划 + 13 执行器/Q/S 只读）。
操作约束：CONFig 后**等四板 `PLAN?` gen 一致再 trigger**（10 kHz 预算 ≥60 s）；trigger 后
**等四板 epoch 一致**（10 kHz 预算 ≥120 s）；四板窗口未结束时发新命令会 -200。
**飞行中重发会重置目标侧重装**（新 `command_seq` → 新 frame_seq），这是"74 s 后仍是旧 gen"的最可能人为原因。

## 1. 目标（完成定义）

低速环（10 kHz 引导环）承担全部前序标定训练：每个动作 **P1/P2/P3/T0/T1/T2/T3 各跑一次
C→T→R→Q→S**，全部通过后只有最后一轮（T3）的 S 允许 `ARM → START 10 MHz`。host 只做单口触发与读回。
收口标准：host 单测 + 四板 HIL + P3 凭证。

## 2. 用户在本会话确立的硬约束（不可违背）

1. **统一控制面**：命令/信封/ACK/参数存储一律走 REFMEM（`refmem_command*`、`refmem_param_pool`、
   `refmem_trigger_session`），不再保留 host-SCPI 双轨。
2. **低速环是引导载体，可停可配**：档位/窗口/候选表/阈值由 C 段下发，不写死编译期宏。
3. **停环期间也要落参数**：R 段把参数/中间量/判定/失败原因写进本地 REFMEM 槽位（链路未建立也可写），
   **复环后（Q）读回对账**，回收"进度"与"问题"。
4. **每动作一轮**：P1 一轮、P2 一轮……；候选×重复在该动作的 R 段内部循环，不产生多轮 CTRQS。
5. **S 段即固化**：每动作 Q 通过后立刻写 flash；但必须在该动作**训练会话关闭之后**
   （HAOFV flash 规则 2.1 #7），幂等（同动作+计划 CRC 不重复写）。
6. **不可新建独立组件、不可重复实现**：CRC、参数存储、触发会话、flash 提交、boot 决策、
   训练判定都有唯一 canonical（见 playbook §9.27）。曾两次违规（`guide_slot_ledger`、
   `guide_ctrqs_runner`），均已撤回。
7. **V1 已归档**：不启用；v1_compat 只能经 `PROJECT_V1_RECOVERY_BUILD=ON` 的审计恢复构建。
8. **验收口径**：固件类改动必须 P3（`workflow_steps.py run --preset pico2-v2-factory-candidate --tdma-only`）；
   `tests/`/`tools/` 改动走 host 凭证。并发会话改源码时**禁止跑验收**。

## 3. 已入库（截至 2026-11-06 03:10）

| 提交 | 内容 |
|---|---|
| `12b406e8` | hop 包络聚合（`rx_slot_hop_min/max` + `RING:RXSLOT?` 扩容）与 `guide_action_plan`（C 段载荷） |
| `8c1ebd3f` | 计划身份绑定既有 `refmem_param_pool_crc` |
| `d4706ad1` | R 段进度记录（48 B：身份/进度/失败原因） |
| `4f65b1a5` | R/Q 走 refmem 参数池：停环写、复环读回 + `guide_action_reconcile()` 四态决策 |
| `ac94916c` | 七动作阶梯推进 `guide_action_ladder_advance()` + S 段提交门 |
| `5f532dec` | 单口命令：`SYSTem:TDMA:GUIDE:CONFig/TRIGger/PLAN?` + 命令目录 50/51 |
| `a9fbe9ca` | **B1 根治**：C/T 改走 `distributed_refmem_control_submit()`，删除 SCPI 本地直接派发 |
| `7d77b787` | CTRQS R/Q 执行器骨架、C 段候选参数、`PLAN?` 扩展回读 |
| `4cfdc6b5`/`642cdcf8` | 工具侧 STOP 丢 ACK 的可审计处置（marker_train / trn03_closed_loop） |
| `e18429a9` | T 段扇出、窗口自恢复、TOPO/ARM 重试（**R/Q 实板闭环**） |
| `f6bff624` | S 提交门：会话关闭来源、动作成功门、代数幂等 |
| `4df276da` | 七动作阶梯状态：顺序约束、COMPLETE 推进、拒绝留痕 |
| `542dcc0c` | 环 STOP 节流（`config_seq` 每调用递增，1 ms 每拍 STOP 使静默屏障永不成立）+ P2 窗口骨架 |
| `dab5e8b6` | `calibration_manager_restore_training_stage()`（从持久 store 重 stage）+ P2 prep 同序 clear |
| `21d50a37` | coded 请求共享 epoch（C 段 `plan.generation`）+ P2 全拆环门 + follower 接受判据 |
| `aca13990` | 引导窗口每次 TOPO 后重设 `TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE`（P1/P2/REBUILD） |
| `5a2a4849` | 分片命令面按环周期 arm deadline + 有界重放（10 kHz 命令面可用；见 §0） |

## 4. 现状（2026-11-06 03:10 快照，非事实源）

**已验证（实板）**：

- **C/T**：`CONFig`/`TRIGger` 经既有 REFMEM 命令面到达四板；四板 `PLAN?` 计划字段一致。
- **R/Q**：触发后 R 段停环跑 P1 trial，四板 `progress_valid=1,status=COMPLETE,trials_done=4,
  deepest_trial=3,reconcile=COMPLETE`；窗口关闭后用捕获的环身份自恢复 `TOPO/ARM/START`，
  四板 `RING` 回到 `enabled=1,node_count=4,up+down running`。
- **S**：Q=COMPLETE 且存在完整 stage 时走 `calibration_manager_commit_training_stage()`；
  首次 `ALLOW` 写一次，下一轮同代数 `SKIP_ALREADY_DONE`（无第二次写）；abort 动作不提交。
- **阶梯**：P1 COMPLETE 推进到下一动作（NEXT）；重复 P1 不推进（RUN_SAME）；
  action=2 因 trial body 未实现被 fail-closed 拒绝（Q=FAILED/reason=CONFIG_VALIDATION_FAILED），
  阶梯保持原位。

**未验证**：

- P2–T3 的 R 段 trial body（当前只有 P1 已实现）；七动作全链；T3 的 S 后 `ARM → START 10 MHz`。
- 参考板窗口偶发 FORWARD 超时（约半数轮次之一），自恢复后可重触发；根因未定位。

**2026-11-06 07:5x M5 完成快照（七动作全链实板通过）**：

- 同一 build `20261105234532` 上：`P1→P2→P3→T0→T1→T2→T3` 七动作全链四板 Q=COMPLETE；
  T3 后四板 `RING` 均 `enabled=1, adapter_started=1`（10 MHz 档 running）。
- T3 四板 `PLAN?` = `1,7,305419904,46,7,4000,1,0,206,0,1,1,1,0,0,1,0,2,1,0,6,2`
  （COMPLETE、trials_done=1、reconcile COMPLETE、S=SKIP、ladder_index=6）。
- 关键修复：T3 复用窗口 restore 的 `REBUILD` 成环路径（stop→TOPO+DIAG→静默→ARM→START），
  成环时补记一条 trial 再进 Q（`48b51f5c`）。
- 参数约束（实板归纳）：P2 参考板 coded 需 t0+1.5 s 启动；T0 link delay ≤ ~124 ns；
  T1 launch_guard ≥ link_base_delay_samples − search_start；T2 marker+base+data+search ≤ 32 cycles。

**证据索引（本轮及前序）**：

| 证据 | 路径/提交 |
|---|---|
| 七动作链命令序列 | gen 40–46、trigger 200–206（本轮会话记录）；四板 `PLAN?` 见上 |
| P3 凭证 | `out/HardwareAcceptance/20261106/p3-074522`（`20261105234532`，PASS 0 WARN/0 ERROR） |
| P3 历史 | `p3-073216`、`p3-071940`、`p3-070625`、`p3-065307`、`p3-064126`、`p3-062813`、`p3-060804`、`p3-055840`、`p3-054459`、`p3-050537` 等 |
| 代码提交 | `a9fbe9ca`（B1）→`7d77b787`（R/Q 骨架）→`4cfdc6b5`/`642cdcf8`（STOP 处置）→`e18429a9`（T 扇出/自恢复）→`f6bff624`（S 门）→`4df276da`（阶梯）→`542dcc0c`（STOP 节流）→`dab5e8b6`（store 重 stage）→`21d50a37`（共享 epoch）→`aca13990`（TOPO 后 DIAG）→`5ce9b926`/`54208304`（P3）→`e419b0fa`（T0）→`1c6d559c`（T1）→`a8512d83`（T2）→`48b51f5c`（T3） |
| 文档提交 | `be6701a6`、`1772c699`、`10e4eff1`、`8ede4990`、`265faf03`、`4c7c6198`、`bd69ebd8` |

**M6 剩余**：B4（10 kHz 低速引导环）未接通；七动作全链当前跑在 10 MHz，需在设计档位复验后
才能作为最终 HIL 口径；每动作 S 均 SKIP，代数语义待确认。

**2026-11-06 04:55 追加快照**：

- 已修：P2_STOP 每拍 STOP 的 `config_seq` 抖动（`542dcc0c`）；P2 prep 的 stage clear 与窗口复环
  并存（`dab5e8b6`）；四板 coded 身份不一致（`21d50a37`）。
- 已实证：P2 能进入 coded 测量（state=P2_WAIT），但参考板 `reject_reason=262`（MANCHESTER）、
  `best_distance=3210`（=6420/2，平线特征）、`margin=0`；follower 的 `program_persona` 在窗口内
  仍为 13（`FLIGHT_PROCESS_FOLLOWER`），不是 `CLOCK_CODED(4)`；同一 build 上 host 工具
  `calibration_clk_coded.py` 仍 accepted（best_lag 101、margin 486）。
- 已发现：§1 规定引导环是 **10 kHz 低速环**，而此前 P1/P2 的 C 段用了 level 7（10 MHz）；
  level 23（10 kHz）经 `OPMode:STAGe/APPLy` 可应用，但四板 `RING:ARM` 全 `-200`——标准环路径
  要求与档位匹配的校准 stage（10 kHz 的 TRN-00 矩阵尚未生成）。
- 已实证：`RING:TOPology` 会重建 staged config 并清掉 `DIAGNOSTIC_CONTINUE`；手工起环要按
  `STOP → TOPO → DIAGnostic 1 → ARM → START`，且该 flag 只容忍“警告级”相位准入，裕量不足的板仍会 `-200`。

**2026-11-06 08:0x B4 根因修正（子代理 `1aa51036` 交叉分析）**：

- `adapter_last_error=6` **不是** phys `RESOURCE_CONFLICT`：phys 错误经环会 `0x100` 偏移
  （ARM 的 263 = 0x100|PHASE_ADMISSION 即为证），phys RESOURCE_CONFLICT 会显示 262。
- 6 = `TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT`（`ring_adapter.h:94`），仅两个产生点：
  `tdma_pio_spi_ring_adapter_tx.c:363-373`（`tdma_flight_engine_expected_input_mask()` 为 false，
  map 无 foreign 段，`tdma_flight_engine.c:730`）与 `:401-405`（receive-health 配置无效）。
- 触发条件正是“清 stage 后的退化 process-image map”；与 10 kHz 分频（≈2083，16.8 定点内）、
  SM claim、DMA/IRQ 无关。ARM 在 10 kHz 被拒（263）是 stale 10 MHz stage 的 profile 身份不匹配。
- 修复选项：**（a）流程侧**——为引导档位（10 kHz/5 MHz）生成匹配 stage（TRN-00/01/02/03
  在该 level 重跑 + `trn03_stage --commit`），保持“有 stage 才起环”；**（b）适配器侧**——允许
  空 foreign-segment mask 的环用 phys 默认 payload 启动（放松 `tx.c:363-373` 的 gate），
  需独立评估 process-image 安全边界。

## 5. 阻塞点状态

1. **B1（inline 参数丢失）——已解决**：`a9fbe9ca` 把 C/T 改走
   `distributed_refmem_control_submit()`（catalog 50/51 已校验字数），参数随命令帧/分片下发；
   SCPI 里的本地直接派发已删除，不再有双重派发/绕过 take-ack。
2. **B2（P3 抖动）——已缓解**：TRN-00 / TRN-03B 的 STOP 回复可能被 USB CDC 丢失，宿主工具
   返回合成串；三处工具现只在**新鲜停止态读回**（enabled=0/adapter_started=0/config==applied）
   成立时接受该处置，否则仍 fail-closed。P3 已连续通过。
3. **B3（S 段会话关闭来源）——已解决**：R 窗口关闭且复环完成后
   `!calibration_manager_training_offline_active_core1()`；幂等键为 store 的
   `calibration_generation` 与当前 stage 相同则 SKIP。
4. **残余（OPEN，2026-11-06 升级为阻塞）**：参考板窗口超时（`reason=11=TIMEOUT`，`reconcile=3`），
   跟随板稳定 `COMPLETE`。build `20261106101617` 上已观察到**两种模式**（详见 §6.2.1）：
   (a) FORWARD 段 t0=18 s 时未成环 → 5→27 直接跳变、`trials=0/2`；(b) 进入 TRIAL 但整窗口
   21.2 s 只完成 1 trial。跟随板只查 `FORWARDING` 人设、参考板必须等 `MASTER_COMPLETE`
   （`app.c:2324-2370`），难度不对称是稳定现象。复验判据：同一 build 上参考板 P1 连续 2 次
   `progress_status=1, trials_done=4`。
5. **B4（低速引导环未接通）——未解决**：§1 的 10 kHz 引导环尚未与标准环路径匹配：
   level 23 可 apply，但 `RING:ARM` 四板全 `-200`；需要 10 kHz 档位对应的校准 stage
   （TRN-00/01/02/03 at level 23）或让 `set_tdma_ring_topology` 接受低速率档。当前 P1/P2
   窗口用 level 7（10 MHz），与“仅 T3 的 S 可起 10 MHz”冲突，需在 C 段档位与窗口档位一并修正。
6. **B5（P2 coded DATA 回路）——未解决**：参考板捕获为平线（MANCHESTER 拒绝），follower 的
   `program_persona` 停在 13；coded first-stage 只重编程 SCK 转发，DATA/CS 不在该路径。
   下一步需读 phys 原始快照或按 §7.4 开只读子代理定位 DATA 引脚归属/转发路径。

### 5.1 历史：B1 交叉排查结论（保留供追溯）

- 机制：`mirror_local_control()` 首写分支以 `params=NULL` 写节点记录，`refmem_node_control_write()`
  对 NULL 清零 16 字；且 `try_post` 不构造命令帧、节点记录按 slot 独立，远端收不到参数。
- 修复不是“补 mirror”，而是走既有带参数的 `control_submit`（本地帧回环 + 远端 fragment）。
- 旧 SCPI 本地直接派发补丁已删除，勿恢复。

## 6. 下一步

当前唯一阻塞与推进顺序以 §6.2 台账为准（M6.1 起，不可跳项）。历史项状态见 §5：
B4（低速引导环）已由 `RING:BRINGUP 1` 路径解决；B5（P2 coded）由 `c6677dcb` 通过，保留观察。

1. 每次 P3 前确认四板 `SYST:FW:BUILD?` 一致；窗口偶发超时先重触发一次再归因。
2. 命令面按 §7.1 确认协议；飞行中不得重发。
3. P3–T3 的 C 段布局见 §6.1（草案 v2 的链路推导规则已冻结，逐链路 role 由环拓扑推导）。

命令形状（C 段参数必须逗号分隔）：

```text
SYST:TDMA:GUIDE:CONFig <action>,<plan_crc>,<generation>,<level>,<window_ms>,<trials>,
                        <candidate_count>,<c0>,<c1>,...,<threshold_low>,<threshold_high>
SYST:TDMA:GUIDE:TRIGger <action>,<epoch>
SYST:TDMA:GUIDE:PLAN?
```

`PLAN?` 字段顺序：计划 9 项（valid/action/crc/gen/level/window/trials/pending/epoch）→
执行器与 Q/S 只读项（ctrqs_state、progress_valid、progress_status、trials_done、deepest_trial、
progress_reason、reconcile_decision、reconcile_resume_trial、commit_decision、commit_applicable、
commit_ok、ladder_index、ladder_step）。

### 6.1 P3–T3 动作的 C 段布局（待冻结，勿臆造）

P3–T3 的 R 体尚未实现；其 C 段布局必须先按下列 canonical 工具的参数面冻结，再写执行器
（禁止在固件里写死候选/档位）。对应关系来自 playbook §9.24：

| 动作 | canonical host 工具 | 需要由 C 段下发的关键输入 | 板端既有原语 |
|---|---|---|---|
| P3 | `calibration_link_p3.py` | `signal_group`、`repeats`、`pulse_count`、`capture_words`、`baud`、频率档 | `calibration_manager_request_p3(role, baud, pulse_count, capture_words, epoch, signal_group)` |
| T0 | `calibration_marker_train.py` | `level`、reference/origin node、`codebook`、`epoch`、residence/offset 矩阵开关 | `calibration_manager_request_marker_training(...)` |
| T1 | `calibration_sck_train.py` | `link_index`、`link_delay_ns`、search start/end、guards、max distance、min margin、repeats | 既有 SCK 训练原语（T1 工具同源） |
| T2 | `calibration_data_train.py` | link、marker/data 方向、`marker_to_data_samples`、node offsets、search/guards | 既有 DATA 训练原语（T2 工具同源） |
| T3 | `trn03_closed_loop.py` / 开机 staging | stage 数据经环上来；`GUIDE_STAGE_BOOT` | 板端 boot/stage → `ARM → START 10 MHz` |

约束：每个动作一轮 C→T→R→Q→S；候选×重复只在该动作 R 段内部循环；只有 T3 的 S 允许
`ARM → START 10 MHz`。在该表冻结前，P3–T3 的 `CONFig` 会被 `plan_valid` 拒绝（fail-closed），
这是预期行为。

**C 段布局草案 v1（待实现验证后转正；字段顺序即 `candidates[]`）**：

| 动作 | candidates[0..4] | threshold_low | threshold_high | 说明 |
|---|---|---|---|---|
| P3 | source_slot, destination_slot, signal_group(0=CLK_DATA,1=CS_DATA,2=BOTH), pulse_count, capture_words | baud_hz | frequency tolerance（千分比） | `trials` = repeats；`window_ms` = 每 trial 上限 |
| T0 | reference_node, origin_node, codebook, node_offset_samples, matrix_repeats | matrix_offset_value | max_best_distance | `profile_level` = level |
| T1 | link_index, link_delay_ns, search_start, search_end, guard_samples | max_best_distance | min_margin | `trials` = repeats |
| T2 | link_index, marker_to_data_samples, search_start, search_end, guard_samples | max_best_distance | min_margin | marker/data 方向由后续位或专用动作变体承载（待定） |
| T3 | （无；stage 经环上来） | — | — | 仅 `ARM → START 10 MHz`，S 后置 boot 决策 |

草案只用于下一步实现与联调；`plan_valid` 的最终校验必须按上表实现后才能放行对应 action。

**实现前的关键设计约束（本轮新增）**：

- P3/T1/T2 是**逐链路**动作（source/destination 或 link_index），不是单板动作；四板必须按
  同一链路顺序协调，每板按 `local_slot` 计算自己在当前链路的 role（initiator/responder/无关），
  无关板在该 trial 保持静默并在链路结束后继续。host 工具是逐链路串行编排的
  （`run_trial(source, destination)`），板端执行器必须复刻这个“链路串行 + 每板 role”语义，
  不能用单板 role 直接跑。
- 因此 P3 的 C 段除了候选，还需携带链路顺序（例如 candidates 中的 source/destination 对，
  或 window 内的链路表）；该布局需要在实现前再冻结一次（草案 v1 的 5 槽可能不够）。
- T3 例外：它是整环 staging + ARM，不需要逐链路 role。

**链路推导规则（v2 决议，2026-11-06）**：链路不写入 C 段，由环拓扑推导——N 节点环的第 L 条链路
为 `source=L`、`destination=(L+1) mod N`，按 L=0..N-1 顺序串行；每板在链路 L 的 role：
`local==source → INITIATOR`、`local==destination → RESPONDER`、其余静默。C 段的 5 个候选因此
保持为：`起始链路(0)`、`signal_group`、`pulse_count`、`capture_words`、`每链路重复数`；
`threshold_low=baud_hz`、`threshold_high=frequency tolerance`；`window_ms` 覆盖全部链路。
Q 仅在全部链路 × 重复都通过时 COMPLETE（`passed_mask` 按链路×重复置位）。

**P3 实板结论（2026-11-06，`54208304`）**：P1→P2→P3 全链四板通过；P3 四板
`PLAN?` = `1,3,305419898,9,7,4000,4,0,132,0,1,1,4,3,0,1,0,2,1,0,3,1`（COMPLETE、4 trials、
reconcile COMPLETE、ladder_index=3）。关键修复：P3 停止后必须等 `p3 state=IDLE` 且
`phys persona=NORMAL` 才关窗，否则 rebuild ARM 报 267=PROGRAM_LOAD。

**B4 下一会话 runbook（低速率引导环，截至 `6b8d553d` 仍未闭环）**：

1. 现状：level 23（10 kHz）清 stage 后 START 报 **274 = 0x100|18 = OWNER_TOPOLOGY_PHASE**
   （适配器 flight-map 门已放宽，物理层仍要求校准 stage）；level 7 + P3 的 10 MHz stage 可起环。
2. **突破（2026-11-06，实板已验证）**：`SYSTem:TDMA:RING:BRINGUP 1`（`TDMA_RING_FLAG_BRINGUP_NO_CAL`）
   在 TOPO 之后设置，可让 level 23 四板 **ARM OK + START OK**（`enabled=1, adapter_started=1`）。
   即低速率引导环走的是 bring-up no-cal 路径，不依赖校准 stage。
3. 引导窗口需要在每次 TOPO 之后重新设置 BRINGUP（TOPO 重建 config 会清除该 flag），
   与 `DIAGnostic 1` 同一位置；**T3/10 MHz 产品启动不得设置 BRINGUP**（否则绕过校准门）。
4. 剩余设计点：P2 的 coded 测量需要 10 MHz（其 prep 会 apply level 7），与“低速引导环”冲突；
   需要把 P2 的“成环档位”和“测量档位”分离（成环走 guide 档 + BRINGUP，停环后再 apply 测量档）。
5. 验收：level 23 四板起环后，把 P1–T3 全链复验到引导档；只有 T3 的 S 允许 `ARM → START 10 MHz`。

**2026-11-06 日间收尾快照（10 kHz）**：

- 已通：level 23 + BRINGUP 起环；参考板单板 P1 完成 4 trials（epoch 244）；跟随板 epoch 241 也完成
  4 trials；速率缩放（t0/ARM/FORWARD/slot）已入库（`3bc8299d`/`fec92c17`/`0ef00a67`）。
- **关键卡点**：10 kHz 下 REFMEM 命令面（catalog 50/51）跨板交付不可用——`CONFig 84` 发出 74 s 后
  COM4 的 gen 仍 83；trigger 出现分钟级延迟（244 晚于 245 到达），四板 epoch 错位。
- 根因链：`distributed_refmem_control_submit()` 远程路径 = `ring_transport_ready()` +
  `refmem_cmd_load_submit_fragments()` 入队；fragment 经 `tdma_maintenance_runtime_submit()` **单次提交、
  失败不重试**（`refmem_cmd_load.c:200-212`）；50/51 的 catalog `timeout_us=0` → 无 issuer deadline
  （`:241-249`）。10 kHz 下丢一帧即“命令永不送达”。
- 下一步：给 fragment 有界重试 + 50/51 按环周期缩放的 `timeout_us`，或改用“提交后四板读回确认”的
  显式同步；之后四板 P1→P2→P3@10 kHz、P2 成环/测量分离、T0–T2、S 语义与 M6。
- 操作约束（实测）：CONFig 后先读回四板 `PLAN?` gen 一致再 trigger；trigger 后读回四板 epoch 一致；
  四板窗口未结束时发新命令会 -200。

**T0 布局 v2 草案（待实现验证）**：T0 是 marker/residence 训练，身份字段（epoch/sequence/
marker_id/generation）由 `plan.generation` 派生，fault_flags=0；5 个候选改为
`origin_node, codebook, link_base_delay_ns, offset_sample_count, repeats`；
`threshold_low/threshold_high` 暂保留（marker 的接受判据由驱动快照 state=ACCEPTED 给出，
不依赖距离阈值）。T0 的每 trial 仍是“全板 prepare → origin inject → 全板等 ACCEPTED/REJECTED”，
不需要逐链路 role；`repeats` 决定重复次数。

### 6.2 M6 细化里程碑台账（长期目标 `goal-ead7c1ef`）

状态口径：✅ 仅当「完成判据」有实板读数/凭证；⏳ 进行中或未达判据；— 未开始。
未验证、延后、`pending` 一律不得写 ✅。

| 里程碑 | 内容 | 完成判据 | 证据 | 依赖 |
|---|---|---|---|---|
| M6.1 | 参考板 P1 TIMEOUT 收口（10 kHz） | 同一 build 连续 2 次四板 P1 `progress_status=1, trials_done=4`、`reason=0` | 四板 `PLAN?` 前 15 字段 + `RING:STATUS?` | §7.4 交叉分析裁决（子代理 `7a077e37`） |
| M6.2 | 阶梯索引与代数一致 | 四板 `ladder_index` 相同；同动作同代数 S 只写一次；S 决策四板一致 | `PLAN?` 第 18–22 字段 + 提交证据 | M6.1 |
| M6.3 | P2 成环档/测量档分离 | 成环走 level 23 + BRINGUP，停环后再 apply 测量档；四板 P2 `COMPLETE` | `PLAN?` + `RING:CONFIG?`（cycle_period/flags） | M6.2 |
| M6.4 | P3 @10 kHz 四板 | 全部链路×重复通过，`COMPLETE` 且 `deepest_trial` 覆盖满 | `PLAN?` + P3 工具产物 | M6.3 |
| M6.5 | T0/T1/T2 @10 kHz 四板 | 每动作一轮 C→T→R→Q→S 四板 `COMPLETE` | 四板 `PLAN?`（逐动作 gen/epoch 记录） | M6.4 |
| M6.6 | T3 @10 kHz | T3 `COMPLETE` 后**仅其 S** 允许 `ARM → START 10 MHz`；四板 `enabled=1, adapter_started=1` | `RING:STATUS?` + `PLAN?` | M6.5 |
| M6.7 | S 代数语义 + M6 收口 | S 代数语义冻结成文；P3 凭证 + host 凭证；本节全行 ✅ | 凭证 + 提交清单 | M6.6 |
| M6.8 | 10 MHz 七动作全链回归 | `P1→T3` 四板 `COMPLETE`（改动后回归，防 10 kHz 修复伤高速档） | 四板 `PLAN?` + P3 | 可与 M6.1 并行 |
| M6.9 | 探针工具归型 | `out/calibration/guide-10k/guide_cmdplane_probe.py` 或归型入库、或明确废弃并记录 | `tools/tool_workflow_triage.json` 条目或文档说明 | 收口前 |

执行约束（与 §2、§7 同源，不重复）：

- 每动作前确认四板 `SYST:FW:BUILD?` 一致；命令面按 §7.1 的确认协议（gen ≥60 s、epoch ≥120 s 预算），
  **飞行中不得重发**（新 `command_seq` → 新 frame_seq 会重置目标侧重组）。
- 每个里程碑结案都要给出「命令序列 + 四板读数 + 产物路径」，不得只给结论。
- 里程碑顺序不可跳：M6.1 未收口不得推进 M6.2（阶梯索引分叉正是 M6.1 的后果）。

#### 6.2.1 M6.1 诊断记录（2026-11-06 19:0x，build `20261106101617`）

**§7.4 交叉分析**：主会话结论（参考板 FORWARD 未成环）与只读子代理 `7a077e37` 结论
（abort 更可能在 TRIAL 段 2361/2367）**部分相左**；实测在同一 build 上两种模式都出现过：

| 轮次 | 参考板状态链（`PLAN?` 第 10 字段） | 结果 |
|---|---|---|
| 19:03 | 1(FANOUT) t=50.5 → 5(FORWARD) t=52.3 → **27(REBUILD) t=68.7** → 0 t=70.5 | 未经 TRIAL，t0=18 s 时未成环 → `reason=11`，`trials=2` |
| 18:51 | 1 t=185.7 → 5 t=187.4 → **6(TRIAL) t=203.9 → 27 t=225.4** | 进 TRIAL 但整窗口 21.2 s 只完成 1 trial → `reason=11` |
| 跟随板 | 1 → 5 → 6 → 27 → 0，`trials=4, deepest=4` | 稳定 COMPLETE |

**采信与差异**：采信子代理的两条结构事实——(a) 参考板 P1 trial 必须等
`tdma_pio_spi_clk_train_snapshot().state == MASTER_COMPLETE`（`app.c:2364-2370`），
跟随板只查 `FORWARDING` 人设（`app.c:2324-2342`），两者难度不对称；
(b) 训练返回超时是定值 100 ms（`TDMA_PIO_SPI_TRAIN_RETURN_TIMEOUT_NS`），而 10 kHz 环回
`feedback_timeout = 150 ms × 4 = 600 ms`，形成/训练在 600 ms 尺度上必然吃紧。
**差异**：子代理认为"`ctrqs=6` 证明已成环，故 FORWARD 超时定性大概率错"；实测 19:03 那轮
证明了 FORWARD 未成环同样会发生（5→27 直接跳变），所以两种模式都要修，不能只按 TRIAL 段归因。

**待取的判别证据**（一次即可）：ARM 后（禁止 START，`data_enabled=0`）在参考板发
`SYST:TDMA:RING:TRAIN 4`，100 ms 轮询 `RING:TRAIN:STATus?` 30 次，直接读 `return_seen` /
`tx_done_observed_timestamp_ns` / `return_observed_timestamp_ns`；若返回观测发生在
>100 ms 且随后 `result=RETURN_TIMEOUT`，则定值超时成立，改为
`max(100 ms, 4 × cycle_period)`（10 MHz 1 ms 周期仍取 100 ms，零回归）。

## 7. 台架与操作注意

- 四板：COM5/COM4/COM3/COM6 = NO.1..4；混版会被 P0T 的固件版本门直接拒绝。
- **起环要在 P3 刚结束的 RAM 校准态下做**：先全板 `RING:TOPology 4,<slot>,0`，再跟随板
  `ARM`/`START`，最后参考板 `ARM`/`START`；参考板 `START` 偶发 `-200`，重发一次通常即成功。
- `RING:TOPology` 会清掉 `DIAGNOSTIC_CONTINUE`；台架诊断起环顺序是
  `STOP → TOPO → DIAGnostic 1 → ARM → START`。该 flag 只容忍警告级相位准入，裕量不足仍 `-200`。
- 窗口自恢复不依赖 boot plan（bench 镜像 `BOOT:PLAN?` 首字段为 0）；恢复用捕获的
  `node_count/local/reference` 走同一组 TOPO/ARM/START。
- 工具侧 STOP 合成串（`OK(no payload; verified by state readback)`）只在新鲜停止态读回后接受；
  其他命令的合成串仍按失败处理。
- 验收抖动重试一次；失败证据保留在 `out/HardwareAcceptance/<date>/<run>/`。
- 本会话两次违规（新建 ledger / 新建相位机）文件在 `out/rejected/`，**不要复用**。

### 7.1 10 kHz 引导档复现步骤（交接实测配方）

```text
# 1) 清 stage + 应用低速率档（每板）
CAL:TRAIN:STAGE:CLEAR
SYST:TDMA:OPMode:STAGe 23
SYST:TDMA:OPMode:APPLy

# 2) 起 10 kHz 引导环（每板 TOPO，然后全板 BRINGUP，再跟随板/参考板 ARM+START）
SYST:TDMA:RING:TOPology 4,<slot>,0
SYST:TDMA:RING:BRINGUP 1
SYST:TDMA:RING:ARM
SYST:TDMA:RING:START

# 3) P1 计划（level=23、window 4000、trials 4、candidates 4/16）
SYST:TDMA:GUIDE:CONFig 1,<crc>,<gen>,23,4000,4,3,0,4,16,1,64

# 4) 等四板 PLAN? 的 gen 一致后再触发（命令面在 10 kHz 下交付慢/易丢，见 §0）
SYST:TDMA:GUIDE:TRIGger 1,<epoch>
```

实测时间预算（10 kHz）：窗口 t0 = 6 s + 80×150 ms = 18 s；P1 slot=5.3 s（16 cycles×2×150 ms+0.5）；
4 trials 约 21 s；恢复另计。P1 计划可用 window=4000（执行器会自动放大 slot）。

**2026-11-06 实测（build `20261106101617`，四板，`5a2a4849` 之后的命令面）**：

- `CONFig` 四板 gen 一致：**51.0 s**；`TRIGger` 四板 epoch 一致：**78.3 s**（含一次维护帧丢失后的
  有界重放；无丢失时约 20 s）。两者都远慢于 10 MHz，属维护通道吞吐（1 chunk/环回），不是故障。
- 确认协议（必须）：CONFig 后轮询四板 `PLAN?` 第 4 字段（gen）一致再 trigger，预算 ≥60 s；
  trigger 后轮询第 9 字段（epoch）一致再继续，预算 ≥120 s。
- 失效率：本轮 2 次 P1 中参考板 2 次 TIMEOUT、跟随板 2 次 COMPLETE；命令面本身 0 次丢失
  （issuer `ack_fail` 增长后均由重放补齐，四板 gen/epoch 最终一致）。
