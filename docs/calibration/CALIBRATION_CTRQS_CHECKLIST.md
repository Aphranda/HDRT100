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
| M6.1 | 参考板 P1 TIMEOUT 收口（10 kHz） | ✅ 同 build `20261106213111` 连续 2 次（gen 620/621）四板 P1 `progress_status=1`、`trials_done=4`、`passed_mask=0xF`、`failed_mask=0`、`reason=0`、`reconcile=1`，`ladder` 一致前进 | 四板 `PLAN?`（25 字段）+ `p3-053100` | ✅ 完成（`f1e03ae9`） |
| M6.2 | 阶梯索引与代数一致 | ✅ 同 build `20261106220406` 连续 10 轮（gen 640–649）四板 `ladder_index=1` 一致；同代数重复 `ladder_step=0`（不再推进）；P1 无 staged 载荷，`commit=0/0/0` 四板一致 | 四板 `PLAN?`（25 字段）+ `verdict` | ✅ 完成 |
| M6.2b | S 段"同动作同代数只写一次" | ⏳ P1 不写 flash（`commit_appl=0`），该路径要到有 staged 载荷的动作（T3）才能验证 | 四板 `PLAN?` 的 commit 三字段 + flash 记录 | 并入 M6.7 |
| M6.3 | P2 成环档/测量档分离 | ✅ 同 build `20261106224625`：成环沿用窗口载体（引导档 + BRINGUP），停环后 apply 测量档（level 7）；四板 P2 `prog_status=1, trials_done=1/1, passed_mask=1, failed_mask=0, reason=0, reconcile=1, ladder_idx=2` | 四板 `PLAN?` + `p3-064615` | ✅ 完成（`740eb1f9`） |
| M6.4 | P3 @10 kHz 四板 | ✅ 同 build `20261107003733`：四板 P3 `prog_status=1, trials_done=12, passed_mask=0xFFF, failed_mask=0, reason=0, ladder_idx=3`；执行器按 trial 逐个推进（`18e495a2` 修掉 state 锁存）。C 段须用链路速率（10 MHz）+ 5% 容差 | 四板 `PLAN?` + `p3-083723` | ✅ 完成（`18e495a2`） |
| M6.5 | T0/T1/T2 @10 kHz 四板 | ✅ 同 build `20261107033453` 整链：T0 四板 `passed_mask=0x1`（base=40）、T1 四板 `passed_mask=0x1`（`guard=32`、search −10..10）、T2 四板 `passed_mask=0x1`（search −4..10，因 T2 guard 硬编码 4）；阶梯四板一致推进到 6 | 四板 `PLAN?` | ✅ 完成 |
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

**2026-11-06 20:3x–21:0x 追记（build `20261106125416`，`a0079130`）**：

- 判别实验结论：ARM-only 下参考板 4-cycle 训练 `state=4/result=5`（RETURN_TIMEOUT）、
  `return_seen=0`、`burst_ns=400000`、`tx_done - tx_start ≈ 2.06 s`；跟随板全程 `state=0`
  （未收到训练请求，环未 START）。即"定值 100 ms 返回超时 < 一个 10 kHz 环回 600 ms"成立，
  但在该状态下跟随板本来就不参与，不能单独据此定因。
- **主因确认为开窗不同刻**：参考板在提交瞬间用本地 node-control 副本开窗，跟随板要等广播
  （实测 trigger 扇出 20 s / 丢帧重放后 78 s），参考板 18 s FORWARD 预算在跟随板收到计划前就耗尽。
- 修复路径评审：先试"等 required ACK 聚合再本地派发"——实测聚合走同一条慢路径，参考板反而
  落后跟随板约 42 s（作废）；改为等**最后一个维护帧 ACK**（`refmem_cmd_load_transport_idle()`），
  落后量降到 **16 s**，且参考板训练首次在 10 kHz 得到 `MASTER_COMPLETE + RETURN_OVERLAP`。
- **残余（M6.1 未闭环）**：单 trial 仍可能超过一个 5300 ms slot——参考板在 trial 1 的 slot 末尾
  abort 2367，快照为 `state=2(MASTER_RUNNING), tx_done 已观测, return=0`；四板 `trials_done` 仍 <4。
  下一步：按 §6.2.1 的判别实验把"训练返回在 slot 内到达"的比例量化，再决定
  `max(100 ms, k × cycle_period)` 的 k 与 slot 预算是否同时调整。

**2026-11-06 21:1x–21:2x 追记（build `20261106131109`，`56ea7598`）**：

- P1 的 slot 估算原来假设"一个 cycle 只花 2 个环回"；10 kHz 实测 4-cycle burst 的 `tx_done`
  在 2.06 s 才到（≈3.4×环周期/cycle），16-cycle trial 实需 ~8 s > 5.3 s slot，边界必 abort。
  改为 5×环周期后：参考板 `trials_done` 1 → **3**（4-cycle 与 16-cycle 各成功一次），跟随板仍 4/4。
- **结构性观察（2026-11-06 21:4x 更正）**：`TDMA_PIO_SPI_CLK_TRAIN_*` 枚举是
  `0=IDLE, 1=FORWARDING, 2=MASTER_RUNNING, 3=MASTER_COMPLETE, 4=ERROR`
  （`tdma_pio_spi_phys.h:195-201`）。因此参考板训练时跟随板 `state=1` **就是 FORWARDING**，
  转发人设一直在位；本条先前把它记成"不在转发"是把 1 当成 IDLE 的误读，已更正。
- 据此残余性质改变：跟随板在转发、参考板也能拿到 `RETURN_OVERLAP`，失败的是"返回在
  `MASTER_RUNNING` 期间没能在截止时刻前被采纳"——返回截止在 arm 时按
  `burst_duration_ns + 100 ms` 预算（`tdma_pio_spi_phys_arm.c:934-936`），而 10 kHz 的
  `tx_done` 本身要到 ~2 s（4-cycle）之后，晚到的 return 一定会被
  `state=4/RETURN_TIMEOUT`（`:979-982`）判掉。候选修复：把截止改为"观测到 `tx_done` 之后
  `max(100 ms, k × cycle_period)`"，10 MHz 仍是 100 ms（零回归）。

**2026-11-06 21:4x 子代理 `96f0340a` 复核结论（推翻上面两条候选的收益）**：

- `tx_done_observed_timestamp_ns` 是**软件打戳**（`tdma_pio_spi_phys_arm.c:968-970`），
  所以"2.06 s"只能证明首次服务观测晚了，不能证明 burst 时长；burst 恒为 `cycles/baud_hz`
  （4-cycle @10 kHz = 400 µs）。据此 `56ea7598` 的 3.4×/cycle 前提不成立。
- 返回判据是**硬件排序**：burst SM 在 `wait 0 irq 2` 之后才 `irq set 1`（`tdma_pio_spi.pio:48-58`），
  因此"重叠"时 IRQ1 被扣住，之后任何轮询都读成 `RETURN_OVERLAP`；反之 burst 结束 IRQ2 仍 0
  ⇒ IRQ1 置位，之后的返回只可能是 `NO_OVERLAP`。**改 deadline 无法把失败变成重叠**，
  最多把 `TIMEOUT` 变成 `NO_OVERLAP`（仍不通过）。
- 真正的结构缺陷在 app 侧：`app.c:2367-2370` 一次"无返回"就 abort 整轮，导致参考板
  `trials_done` 永远到不了 4；而跟随板一直是"记 failed_mask 后继续"。
- **假绿风险**：若只把失败改成继续，`finalize()`（`app.c:2000-2009`）只看
  `trials_done == trials` 就报 COMPLETE，且 `PLAN?` 原本不输出 mask ⇒ 4 个 `NO_OVERLAP`
  也会让"status=1 + trials=4"成立。故必须同时输出 mask 并把判据改为 `passed_mask=0xF`。

**2026-11-06 21:5x 落地（待验证）**：`app_ctrqs_trial_fail_and_advance()`——参考板 trial 在
`CLK_TRAIN_ERROR` 或 slot 到期时记 `failed_mask` 并推进到下一 trial（不再 abort 整轮）；
`app_ctrqs_finalize()` 对 P1 额外要求 `failed_mask==0` 才报 COMPLETE；`PLAN?` 末尾追加
`passed_mask`/`failed_mask` 两字段（前 22 字段位置不变）；slot 预算回到 2×（5× 的前提已证伪）。

**2026-11-06 22:0x–22:3x 追记（P3 凭证 `p3-215141` = build `20261106135157`；
`p3-223048` = build `20261106143103`）**：

- 诚实语义已验证（`ebdae6d5`）：参考板 `trials_done` 首次稳定到 4，mask 显示
  `passed=0x1, failed=0xE`，`prog_status=2`（不再假 COMPLETE）；跟随板 `0xF/0x0`。
- **失败机理定案**：per-trial 训练快照显示只有 trial 0 得到 `RETURN_OVERLAP`，trial 1..3 全是
  `return_seen=0`；而隔离实验（跟随板先各自 `RING:TRAIN 1` 进 FORWARDING，参考板连发 3 次
  4-cycle train）**三次全部 `RETURN_OVERLAP`** ⇒ 物理层与 PIO 转发不是瓶颈。
  对照时间线：跟随板在 t≈148.4 进 REBUILD 并 **DISARM**（train state 1→0），参考板当时仍在
  TRIAL（直到 t≈164.9）⇒ **跟随板先收工，拆掉转发人设**，参考板后 3 个 trial 自然收不到返回。
- **开窗对齐不可能靠投递边沿达成**（三种都试过，全部无效或有害）：
  | 边沿 | 实测偏差 | 结论 |
  |---|---|---|
  | required ACK 聚合 | 参考板晚 42 s | 聚合走同一条慢路径 |
  | 维护帧 ACK 排空 | 参考板晚 16 s | 仍晚于跟随板收工点 |
  | 已发 chunk 计数 | 参考板早 52 s | 丢帧重传使"已发"领先"已达"，作废 |
- 结论：窗口必须共享同一个起始时刻（或让跟随板在整轮结束前不拆转发人设）。前者是把
  `t0` 从"本板 receipt"改为"环上共享时刻"（§6.1 C 段语义变更）；后者是一个较小的
  follower 侧收工延迟，但只对 P1（跟随板贡献=人设）成立，P3/T1/T2 的逐链路动作仍需真对齐。

**2026-11-06 23:1x–23:5x 追记：hold 方案两次实测都失败，证明"部分延迟"不可行**

- 23:13（只延迟 close、只跟随板）：四板**首次全部** `passed=0xF, failed=0, trials_done=4`，
  但 `prog_status=3, reason=14`——参考板先 rebuild、跟随板还在 hold，环组不起来。
- 23:46（hold 应用于四板）：参考板连 TRIAL 都没进（`ctrqs 5→27`，trials=0）——跟随板更早
  进入 TRIAL 后**环已被停**，参考板 16 s 后到达时 `ring_formed()` 为假。
- 结论（新，且有决定性）：**环状态是四板共享的**。谁先进 TRIAL 就会停环，晚到者的 FORWARD
  自然失败。因此"让跟随板晚点收工"这类局部延迟无法替代同时开窗；M6.1 必须把 `t0` 从
  "本板 receipt + 固定偏移"改为**共享时刻**（环 epoch 或双方可观测的同一事件），
  这正是 §6.1 的 C 段语义变更。
- 当前工作树留有一处**未提交**的 `app.c` hold 实验（`hold_close_pending` /
  `app_ctrqs_hold_ms`），按 §2 规则 10 未经用户同意不撤销；在共享起始时刻落地前不应合入。
- 该项已按 §7.4 开只读子代理（`96f0340a`）复核状态机与回落原因；裁决前不改该路径。
- 附带结论：P1 的 cycle ladder 不属于冻结契约——canonical host 工具是扫频（`calibration_clk_train.py`
  的 `pulse_start/pulse_limit/growth_factor`），`{4,16}` 只是交接配方里的取值，可按档位调整。

#### 6.2.2 共享起始时刻（已批准方向，设计待冻结）

**需求**：四板必须在同一时刻进入 R 窗口。已实测排除所有"投递边沿"近似（§6.2.1：ACK 聚合晚
42 s、帧 ACK 排空晚 16 s、已发 chunk 早 52 s），也排除"局部延迟收工"（23:13/23:46 两次实测：
环是共享资源，先进 TRIAL 的板会停环，晚到者 FORWARD 必败）。因此 `t0` 必须来自**四板可独立
计算出的同一时刻**，而不是"本板 receipt + 固定偏移"。

**候选机制**（下一轮冻结其一）：

1. **绝对环序号**（倾向）：trigger 携带 `start_ring_seq = ring_seq + K`；每板在本地
   `ring_seq` 到达该值时开窗（`ring_seq` 在环运行时四板同源、单调）。K 取值要覆盖最坏投递
   （10 kHz 实测 78 s ≈ 520 cycle，取 K=1200 有 3 倍余量）。晚到且已错过 `start_ring_seq`
   的板必须 fail-closed（`MISSED_START`），不得补跑。
2. **两段式 trigger**：先广播 arm（无窗口），再由 host 在四板 arm 确认后广播 start；start 的
   投递时间仍是问题，除非同样用绝对序号。

**待冻结点**：K 的来源（host 计算 or C 段下发）、`epoch`/`ring_seq` 的字段承载（新增候选槽 or
epoch 高位标志）、10 MHz 档是否直通（其投递偏差 ms 级，可保持 receipt 语义）、`PLAN?` 回读
新增 `start_ring_seq`/`missed_start` 字段、以及 P2–T3 的逐链路动作是否共用同一机制。

**验收**：同一 build 上连续 2 次四板 P1 `progress_status=1, trials_done=4, passed_mask=0xF,
failed_mask=0`，且四板 `PLAN?` 的 t0 派生量（环序号）一致。

#### 6.2.3 环状态协商（barrier）：停环/起环必须先达成共识

**问题（有实测证据）**：当前 C→T→R→Q→S 的每个交接点都是**本地独断**——每板在 FANOUT 后
自己 `stop`，再自己 TOPO→ARM→START（`app.c:2802-2847`）。环是四板共享资源，于是：

- 谁先进 TRIAL，环就被谁停掉；晚 16 s 到的参考板 `ring_formed()` 为假（23:46 实测）；
- 三种投递边沿（ACK 聚合 +42 s / 帧 ACK 排空 +16 s / 已发 chunk −52 s）和两种"延迟收工"
  全部无法补偿（§6.2.1 表）；
- 结论：缺的不是某个更聪明的边沿，而是**没有协商**。类比以太网：停环/起环需要
  link negotiation + barrier，交接才稳定。

**设计**：把每个交接点做成 barrier——本板发布 `(phase, round)`，只有看到**四板都处于目标
phase 且 round 相同**才允许推进；推进前再稳一个环回，避免"最后一板还没看到"。超时按环周期
缩放并 fail-closed（记 reason，不进入下一段）。barrier 通过的瞬间即四板的共享边界，
`FORMED → TRIAL` 这个 barrier 通过后同刻开窗——M6.1 的"共享起始时刻"便是它的自然结果，
不需要绝对时钟，也不需要 §6.2.2 的环序号机制。

**载体（唯一待拍板项）**：

| 方案 | 时延 | 代价 |
|---|---|---|
| (a) flight process image 的每板 short slot（32 B）加 phase/round 字节 | 每环回可达（10 kHz ≈600 ms） | 改 payload class 布局：版本号 + 契约登记 + 双端同 build |
| (b) 复用 REFMEM node-control 记录广播 phase | 维护通道 1 chunk/s，数十秒 | 零协议改动，只够做慢 barrier / 重启兜底 |
| (c) 新增 catalog 命令（ring phase propose/ack） | 同 (b) | 新命令 id + 契约 |

**倾向**：(a) 做快 barrier，(b) 作为跨代/重启后的兜底。落地前须先确认 flight short slot 的
空闲字节与 payload 版本升级路径（`tdma_flight_engine.h:46-58` 的 payload 版本/槽位约束）。

**验收**：barrier 完成时刻四板差 ≤1 环回；同一 build 连续 2 次四板 P1
`passed_mask=0xF, failed_mask=0`。

**载体已定（2026-11-07，用户裁决）= 不改 flight 载荷；在维护面加一条"缩小版" phase 小消息。**

关键观察：维护通道的慢不是"1 chunk/环回"这个上限，而是**命令消息本身太大**——一条 GUIDE
CONFig 的 REFMEM 帧是 216 B，被切成 54 chunk（实测 49–51 s）。一条 phase/round 小消息只要
**1 chunk（10 B 净荷）**，一个环回就送达，量级从"几十秒"降到"秒级"，足以支撑停环/起环的
barrier，而且完全不动 flight process image（无需版本升级、无需挤 body 字节）。

已核实的落点：

- 维护面第二类消息注册表：`TDMA_SECOND_CLASS_TYPE_REFMEM_CMD` / `_FRAGMENT` / `T2_STATIC` /
  `T2_DYNAMIC`（`components/tdma/src/refmem_cmd_load.c` 注册处），新增一个 `_RING_PHASE` 类型；
- 每帧 ACK 门控仍在：一条 phase 消息 ≈ 1 帧 ≈ 1 个 ACK 往返（10 kHz 约 2.4 s），
  所以 barrier（发布 + 看到四板一致）≈ 2–3 帧 ≈ **5–7 s**，用于交接点可接受；
- 不改 `TDMA_FLIGHT_MAILBOX_VERSION`，不碰 `tdma_flight_engine.c:403-409` 的 slot 写入。

实现清单（下一轮按序做，每步独立可验）：

1. 定义 `ring_phase_msg_t {version, phase, round, reason}`（≤10 B）与 `_RING_PHASE` 类型 id；
   注册解析器，收到即记录 `peer_phase[node]`/`peer_round[node]`（不产生 ACK 之外的副作用）。
2. 发布端 `refmem_ring_phase_publish(phase, round)`：环活跃时每 N 个环回（N 可配，默认 1）
   广播一次；只在 phase/round 变化时立即发一次，稳态降频。
3. barrier API `refmem_ring_phase_barrier(target, round, timeout_ms)`：四板 phase==target、
   round 相同，再稳一个环回才返回；超时 fail-closed（reason 可读）。
4. 执行器接入两个交接点：FANOUT 前（停环共识）、`FORMED→TRIAL`（同刻开窗）。
5. 回读 `SYST:TDMA:RING:PHASE?`（4 板 phase + round + barrier 结果），并进 `PLAN?` 或独立页。
6. 单测：编码/解码、round 变化、超时、稳态降频；P3 后按验收判据实板验证。

**被取代的实验**（已存档，勿合入）：`refs/wip/m61-hold`（延迟收工）、
`refs/wip/m61-lastframe-gate`（末帧上线门 + hold）。两者都只解决"晚到"，不解决"没有协商"。

**2026-11-07 01:2x 设计修订（用户指示：参考 T2 FREEZE 的操作）**：

- **维护通道实测是单源广播**：参考板 `RING:PHASE?` 的 `recv=0`（收不到自己），跟随板只收到
  slot 0（参考板），四板互相收不到——因为只有 origin 会发射维护帧。所以"四板互发 phase"
  这条路线在该通道上不成立；已存档 `refs/wip/m61-phase-msg`（含模块、SCPI 与执行器接线）。
- **T2 FREEZE 的协商形态可复用**（`components/sync_trigger/src/trigger_t2_runtime.c`）：
  origin 用 `distributed_refmem_stage_position_descriptor(FREEZE, target_mask)` 下发描述符，
  按片重发（`T2_FREEZE_RESEND_SLICES`），跟随者冻结后由
  `distributed_refmem_note_position_frozen(run_id, active, frozen_mask)` 记录，
  **每个 process image 都回报一次**；origin 用
  `distributed_refmem_position_freeze_acked(run_id, target_mask)` 收齐 ack 掩码。
  这条回报路径是每环回到达的，比维护通道（1 chunk/环回）快两个数量级。
- **环状态协商改用同一形态**：origin 下发"环阶段"描述符（phase+round+run_id），跟随者应用并
  每帧回报，origin 收齐 ack 掩码后才推进；预期四板推进差 ≈1–2 个环回（10 kHz 0.6–1.2 s）。
- **待冻结点**：① 扩展 `TRIGGER_POSITION_STAGE_*` 增加 RING_PHASE（单机制，但要动描述符契约
  与 T2 校验 `trigger_position_descriptor.h:199-210`），还是新增同形态的环阶段描述符通道
  （不污染 T2 语义）；② phase/round 在 position 镜像里的字节位置与版本；③ 谁做 origin
  （当前是 CTCQS 的 ref_slot）。
- **夜间作业注意（重要）**：本会话期间检出**并发会话**在改树（`docs/console/*`、
  `tests/python/test_trigger_debug_*`、`tools/domains/trigger/*`）。按 §2.8，并发会话改源码时
  禁止跑验收；夜间每轮开跑 P3 前必须先 `git status` 确认无他人未提交的源码改动。

**2026-11-07 01:1x 待冻结点 ① 裁决：不重载 T2 描述符**

- 审计 `stage` 判定共 15 处（`trigger_t2_runtime.c:1021/1095/1103`、
  `distributed_refmem_position.c:29-37`、`distributed_refmem.c:1156`、
  `trigger_position_mailbox.h:42-43`、`trigger_position_descriptor.h:86/199/209/220` 等）。
  新增 `STAGE_RING_PHASE` 会让其中至少 6 处非归档位置**拒绝或误路由**（mailbox 校验、
  position 镜像封装、descriptor 编码都只接受 0/1/2）。
- 结论：T2 描述符的语义是"转台位置预测"，把标定阶梯的环阶段塞进去会把两个域的契约绑在一起，
  回归面覆盖 T2 全部路径——不符合 HAOFV"单一 canonical、不重复实现"的边界（复用**形态**，
  不复用**语义**）。
- 改用**兄弟通道**：origin 下发环阶段（沿用 T2 的"下发 + 按片重发"形态），跟随者经
  **process image 每帧回报**（这是 T2 真正值得复用的部分），origin 收齐回报掩码再推进。
- 下一步必须先确认**跟随者回报字节的落点**：flight node slot = 32 B（8 B 快头 + 24 B body），
  需读 `TDMA_FLIGHT_MAILBOX_BODY_SIZE` 的生产者，确认 body 是否有 ≥2 B 空闲（phase+round），
  再决定是否需要版本升级；若无空闲，则退回"维护面小消息 + 跟随者以 ACK 帧回报"的折中。

**2026-11-07 01:4x 协商 v1 实测（`988f6bf2`，build `20261106172420`）**：

- **下发-采纳已通**：参考板 `RING:PHASE?` `pub=5`，三块跟随板 `recv=5`、`local=6(TRIAL)`——
  origin 广播 + 跟随者收到即采纳的链路成立（每环回到达，实测远快于命令面）。
- **仍未开窗**：跟随板在 FORWARD 等到 90 s 预算耗尽（`reason=11`）；参考板进 FORWARD 约
  0.7 s 就 `abort(NOT_ONLINE_SAFE=14)`——它自己的 ring START 失败（`enabled=0` +
  `adapter_last_error`），而跟随板此时已长时间停在 FORWARD（等待 origin 的 TRIAL 广播）。
- 新结论：协商解决的是"何时开窗"，但**起环本身变成了竞争**——origin 起环时跟随板已长期停在
  FORWARD，START 被拒。下一步必须让 origin 的起环也走协商（跟随板在 origin 起环前保持
  "待起环"可接受状态），并给 START 增加回读/重试（现有 `472`/`473` 之外的失败分支要带 reason）。
- M6.1 仍 OPEN。

**2026-11-07 01:5x 关键实测（`4db6db93`，build `20261106174316`）**：

- **参考板 P1 首次 4/4 全过**：`passed_mask=0xF, failed_mask=0, trials_done=4`。条件正是
  "跟随板全程停在 FORWARD 保持转发人设"——跟随板一旦进 TRIAL 就会重初始化物理层，
  参考板后续 trial 立刻收不到返回（这解释了此前 `passed=0x1, failed=0xE` 的稳定现象）。
  ⇒ **P1 的正确角色分工：跟随板在参考板窗口期间保持 FORWARD，不进入自己的 TRIAL。**
- 同轮暴露两点：① 跟随板 `RING:PHASE? recv=0`——环被拆时维护帧根本发不出去，phase 广播
  在此之前/之后才可能到达；② 参考板收尾 rebuild `14(NOT_ONLINE_SAFE)`——跟随板此时还在
  等 phase，没人一起重建。
- 结论（下一步设计）：协商消息必须**按"未来环序号"下发**，而不是"立即切换"：
  origin 在环还跑着时广播 `stop at ring_seq = now + K`（K 覆盖投递），各板在自己的 ring_seq
  到达该值时一起停；重建同理用 `form at ring_seq`。这样消息在环上可达，切换点又四板同刻。
- 相应地 P1 需要新增 `TRIAL_DONE` 阶段：跟随板在 origin 窗口期间保持 FORWARD，收到
  `TRIAL_DONE` 后才进入自己的 TRIAL（只做 persona 计数），随后与 origin 一起按环序号重建。

**2026-11-07 02:2x abort_site 归因（`5d15610c`，build `20261106181408`）**：

- 参考板：`prog_status=3` 但 `trials_done=4, passed_mask=15, failed_mask=0, reason=14`——
  **训练 4/4 全过，失败只在收尾 rebuild**。这个形态已连续多轮复现。
- 跟随板：`abort_site=5(FORWARD), reason=11, trials_done=0`；`RING:PHASE? recv=0`。
- **决定性**：phase 帧全部积压在 origin 的维护队列里——FANOUT 一进入就停环，队列再也没有
  机会发出。所以"立即切换"式 phase 广播在窗口内不可达；协商帧必须在环还跑着时送出，
  停环/起环按**未来环序号**对齐（各板在自己的 `ring_seq` 到达 `base+K` 时一起动作）。
- 结论：M6.1 的剩余工作收敛为两件：① 把窗口的停环/起环改成环序号对齐（消息在环上发出）；
  ② 收尾 rebuild 也走同一对齐，避免"参考板独自重建 → 14"。

**2026-11-07 04:2x P1 四板首次全绿（build `20261106200959`，`94a6ba91`）**：

- **第 1 轮：四板全部 `prog_status=1, trials_done=4, passed_mask=0xF, failed_mask=0,
  prog_reason=0, reconcile=1`**（gen 460 / epoch 560）——M6.1 判据首次整套满足。
- **第 2 轮（gen 461 / epoch 561）**：三块跟随板仍全绿；参考板 `abort_site=6(TRIAL)`、
  `trials_done=1`、`passed=0x1`、`reason=11`——即再次出现"某个 trial 的训练没在一个 slot 内
  完成"。这是参考板侧仅剩的抖动。
- 达成全绿的三个条件（本提交）：① 跟随板驻留 60 s 且到期视为完成；② 跟随板完成判定先于
  FORWARD 的通用 deadline；③ rebuild 预算 60 s（否则先到的板把收尾判成 14）。
- **M6.1 状态**：1/2 连续通过，仍 **OPEN**；下一步压参考板 trial 的 slot 抖动。
- **cycle ladder 不能缩短（负结果）**：把 C 段候选从 `{4,16}` 降到 `{4,8}` 后，参考板
  `trials_done=4` 但 `passed_mask=0, failed_mask=15`——4 个 trial 全部拿不到返回。
  说明返回检测依赖足够的 burst 长度（跟随板要有时间把沿转回来），10 kHz 下必须保留 16-cycle
  档；抖动要靠 slot/时序而不是缩小 ladder 解决。

**2026-11-07 04:4x–04:5x 连续复跑（`1f6438f8`，build `20261106203152`）**：

- slot 估算 2x → 3x（10 kHz 下 16-cycle slot = 7.7 s；10 MHz 不变）。
- 同一 build 两轮：**A 轮四板全绿**（`passed=0xF/failed=0`）；**B 轮参考板
  `passed=0x3, failed=0xC`**（4 个 trial 都跑完，后两个失败），跟随板仍全绿。
- 结论：抖动已从"整轮 abort"降级为"单 trial 失败"；失败集中在参考板**后两个 trial**，
  说明是返回路径在窗口后半段退化，而不是窗口预算不足。下一步查"第二个 slot 之后
  跟随板/参考板物理层状态"（`RING:TRAIN:STATUS?` 的 request_seq 与 state 变化）。
- **M6.1 累计**：全绿出现 3 次（gen 460 / 470 / 与 461 前一轮），但**从未连续 2 次**，
  仍 OPEN。

**2026-11-07 05:1x phase 复位（`e5c5af33`，build `20261106204950`）**：

- 修复"phase 跨轮粘滞"：跟随板不再沿用上一轮的 TRIAL 提前驻留（`tdma_ring_phase_set_local()`
  + `app_ctrqs_start` 每轮置 READY）。这是"后两个 trial 失败"的直接解释。
- 同轮时间线（`trace-*.json`）：跟随板在 t≈124.8 已是 `1/1(FORWARDING)`；参考板
  124.8 FANOUT → 125.6 ARM → 126.6 **REBUILD**（约 1 s 内 abort），从未进入 TRIAL。
  `PLAN?` 记录的 `abort_site` 与时间线不完全一致（说明该字段被后续 abort 覆盖或状态枚举
  另有含义），下一步要把 abort 站点做成"只记第一次 + 带阶段名"，否则归因仍会漂移。
- 结论：剩余抖动是**参考板自身的起环/ARM 阶段**在 1 s 内失败（跟随板此时已就绪），
  不是 persona 问题。下一步给 ARM 路径加 reason 细分（哪一步被拒：quiescent / arm 结果 /
  adapter 错误），再决定修复。

**2026-11-07 05:2x 0.2 s 状态探针（root cause 收敛，build `20261106211247`）**：

- 探针（`guide_cmdplane_probe.py stateprobe`）在干净台架上抓到完整时间线：
  `t=0.1s ctrqs=0` → **`t=65.5s ctrqs=1(FANOUT)`** → `t=66.4 ctrqs=4(ARM)` →
  `t=67.1 ctrqs=6(TRIAL)` → `t=97.9 ctrqs=27(REBUILD)`。
- 即：**参考板自己的 trigger 应用被"广播落地门"推迟了 65 s**（门等的是帧 ACK 排空，
  而 10 kHz 下排空包含重传尾巴），它进 TRIAL 时跟随板的 60 s 驻留已经到期 →
  参考板第一个 trial 就收不到返回（正是 `abort_site=6, trials=0`）。
- 结论：剩余抖动的**唯一根因是 trigger 的投递时间**（160 B 命令帧 → 5 个维护帧 →
  20–65 s），不是窗口预算、不是 persona、不是 rebuild 预算。修法与用户先前方向一致：
  **把 trigger 也做成"缩小版"维护消息**（≤10 B，1 帧、约 1 个环回），四板据此几乎同刻
  开窗；届时"广播落地门"可以整段删除。
- 台架退化与恢复（记录备用）：连续跑数小时后出现 bring-up 失败/命令面停摆；恢复步骤为
  四板 `SYST:BOOT:RESET` + `stage` + `up`，若仍异常则跑一次 P3（其初始化比手工更完整）。
  本轮 P3 `p3-051235` 后恢复，随后 gen 610 四板全绿、gen 611 参考板再次失败。

**2026-11-07 05:4x M6.1 收口（`f1e03ae9`，build `20261106213111`）**：

- 紧凑 trigger（0x57，8 B，1 chunk）上线后：参考板 trigger 应用从 **t=65.5 s 降到 t=0.09 s**，
  四板在同一个环回内看到 trigger。
- **连续两轮（gen 620、gen 621）四板全部** `prog_status=1, trials_done=4,
  passed_mask=0xF, failed_mask=0, reason=0, reconcile=1`，`ladder_idx` 四板一致。
  ⇒ **M6.1 判据达成（同 build 连续 2 次）**。
- 复现配方：`stage`（level 23）→ `up`（TOPO/BRINGUP/ARM/START）→ `GUIDE:CONFig`（gen G，
  等四板 gen 一致）→ `GUIDE:TRIGger`（epoch E）→ 等四板 `PLAN?` 终态。
- 下一步 M6.2：四板阶梯索引与 S 代数一致——两轮已看到 `ladder_idx=1` 一致，但
  `ladder_step` 在第二轮为 0（同代数重复），需要确认"S 段代数幂等"在 10 kHz 下同样成立。
- **残余（OPEN）**：第 3 次复跑（gen 622）参考板 `passed_mask=0xE, failed_mask=0x1`
  ——4 个 trial 里仍有 1 个丢返回（跟随板全绿）。3 次里 2 次全绿、1 次单 trial 失败。
  复验判据：连续 10 次四板全绿；未达标前 M6.1 的"稳定"只到"判据满足"这一级。

**2026-11-07 06:4x 稳定性残余 CLOSED（`605a7194`，build `20261106220406`）**：

- 非绿轮次的 `failed_mask` 恒为 `0x1`（失败总在 **trial 0**，即进 TRIAL 后第一个训练）：
  phys 刚 re-arm、跟随板转发人设仍在收敛。在 t0 后加 3× 环周期 settle（10 kHz 450 ms、
  10 MHz 3 ms）再发第一个训练请求。
- **连续 10 轮（gen 640–649）四板全部 GREEN**：`status=1, trials=4/4, passed=0xF,
  failed=0, reason=0, ladder=1/0, commit=0/0/0`。⇒ M6.1 稳定性残余 **CLOSED**；
  M6.2 的索引/决策一致性同批证据达成。

**2026-11-07 07:0x M6.3 达成（`740eb1f9`，build `20261106224625`）**：

- P2 原来在 PREP 第一步就 apply 计划的 `profile_level` 再成环——在设计档位下等于要求用
  10 MHz 成环，成不了。改为两段：**成环沿用窗口载体（引导档 + BRINGUP）**，`ring_torn_down`
  之后再 stage/apply **测量档（level 7）**，然后才进 P2_WAIT 发 coded 测量。
- 实板：先跑一轮 P1（四板全绿、ladder→1），再发 P2 计划 → **四板全部**
  `status=1, trials_done=1/1, passed_mask=1, failed_mask=0, reason=0, reconcile=1,
  ladder_idx=2, ladder_step=1`，计划 `level=7`。M6.3 达成。
- 观察：P2 的 S 段 `commit_decision=2, commit_appl=1, commit_ok=0`——有 staged flash 载荷
  但未写；这正是 M6.2b/M6.7 要收口的"S 代数语义"，留待 T3 一并验证。
- 注意：阶梯单调（P1→2），重跑 P2 需先让 ladder 回到 1（跑一轮 P1），因此 P2 的"连续多次"
  验证成本高于 P1。

**2026-11-07 07:1x M6.4 进展（build `20261106230517`）**：

- 首次在 10 kHz 跑 P3 动作：`status=1, trials_done=12`（4 链路 × 3 repeats）但
  `passed_mask=0`、`failed_mask≈0xFFF`——测量全被拒。原因同 M6.3：**P3_PREP 只停环、不切测量档**，
  ranging 实际跑在引导档载体上。
- 修复（`c94dafa4`）：`ring_torn_down` 后 stage/apply 计划的 `profile_level`（7）再进 P3_RUN，
  与 P2 同一规则。
- **未复验**：上次 P3 动作已推进阶梯到 3，重跑需阶梯回 0；而实测"重启 + stage/up"之后
  guide 计划面不接受新计划（P1/P2/P3 三轮全部 `status=0`），需先跑一次 P3 验收流程做完整
  初始化。故 M6.4 仍 **⏳**，不作完成。

**2026-11-07 08:0x P3 参数定案与四板不对称（build `20261106235412`）**：

- **C 段参数是 P3 能否成立的关键**：`threshold_low=baud_hz` 必须填**链路速率**（10 MHz =
  10000000，不是 SCPI 的 115200），`threshold_high` 用工具默认的 **5%**（50000 permille）。
  用 115200/5000 时 12 个 trial 全失败（`passed=0, failed=0xFFF`）。
- 改对之后（`level=7`、`pulse=32`、`capture=256`、`repeats=3`）：
  NO.1、NO.2 **12/12 全过**（`passed=0xFFF`）；**NO.3 仅 3/12、NO.4 仅 6/12**。
- 即 P3 的执行器/档位分离已成立，剩下的失败是**逐链路不对称**（不是协议问题）：
  与验收里"NO.1 侧链路边缘"的观察一致，下一步按链路逐个定位（哪条 link、哪个 group、
  实测 offset 与判据差多少），而不是再动执行器。
- M6.4 仍 **⏳**：判据要求四板全部链路×重复通过。

**2026-11-07 08:1x P3 形态改判（`18e495a2`，build `20261107001844`）**：

- 只读子代理 `91c46f6a` 独立取证 + 主会话复核后**改判**：NO.3 `0x7` / NO.4 `0x3F`
  **不是"逐链路物理不对称"**，而是 P3 执行器缺陷——`app_ctrqs_p3_advance()` 不恢复
  `state=APP_CTRQS_P3_RUN`（该常量只在 P3_PREP 赋值），一旦某个 trial 以 `P3_STOP` 结束，
  状态机永久停在 STOP，之后每个 service 拍用**同一陈旧判决**盖章剩余 trial → 纯前缀掩码。
  NO.3/NO.4 通过的位全是 **no-op pass**（该板既非 source 也非 destination，代码注释明说
  "reports no hardware evidence"）；NO.1/NO.2 的 `0xFFF` 同样**不能当作 12 次实测证据**。
  T0/T1/T2 都显式恢复 run 态（`app.c:2666/2742/2829`），只有 P3 漏了。
- 第二处：`finalize()` 只对 P1 检查 `failed_mask`，P3 即使真实 trial 全失败也报 COMPLETE
  并让阶梯推进到 `ladder_idx=3`（**在失败证据上推进**）。已改为对全部动作遵守
  `failed_mask==0` 才 COMPLETE。
- 因此 M6.4 的"12 trial 全覆盖"判据在修复前**不可达**；修复后尚未取得实板证据（本轮随后
  台架 bring-up 失败 + 命令面停摆，阶梯未能重跑）。M6.4 仍 **⏳**。

**2026-11-07 08:5x M6.4 达成（build `20261107003733`，`18e495a2`）**：

- 修掉 P3 执行器的 state 锁存后重跑整条阶梯（P1 → P2 → P3，均在 10 kHz 设计档位）：
  - P1：四板 `status=1, trials=4/4, passed=0xF, failed=0, ladder=1`；
  - P2：四板 `status=1, trials=1/1, passed=0x1, failed=0, ladder=2`；
  - **P3：四板 `status=1, trials_done=12, passed_mask=0xFFF, failed_mask=0, ladder=3`**。
- 判据说明（诚实口径）：12 个 trial 里每块板有 6 个是 **no-op pass**（该板既非 source 也非
  destination，`app.c:2591-2596` 明确"不提供硬件证据"）；4 条链路各 3 次重复由链路两端两块板
  真实执行，两端的真实 trial **全部通过**（`failed_mask=0`），故"全部链路×重复通过"成立。
- 目标档位口径：P2/P3 的测量段都切到 `profile_level=7`（10 MHz），成环与窗口仍在引导档 23 上；
  这是 M6.3 引入的分离规则在 P3 上的复用。
- M6.4 ✅ 完成；阶梯已推进到 `ladder_idx=3`，下一动作是 T0。

**2026-11-07 09:0x M6.5 T0 起始（build `20261107003733`）**：

- T0 的 C 段：`origin_node, codebook(1..3), link_base_delay_ns, offset_sample_count, repeats(1..4)`；
  `trials = repeats`，`slot = window/trials`（`app.c:2295-2317`）。
- **坑 1（已定位）**：`offset_sample_count` 的上限是
  `CALIBRATION_TRAINING_PHASE_MAX_OFFSET_SAMPLES = 10`（`calibration_training_phase.h:8`），
  超过即 `reason=13`。传 32 时计划直接被拒。
- **坑 2（待解）**：改成合法值后计划被接受（四板进入 FANOUT/T0_RUN），但
  `calibration_manager_request_marker_training()` 返回失败 → `reason=13, status=2, trials=0/1`；
  四板 `ladder_step=3`（同代数不推进，符合 M6.2 语义）。
- 已给探针加通用入口 `ga <action> <gen> <epoch> <level> <window> <trials> <c1..c5> <lo> <hi>`，
  便于 T0/T1/T2 逐个试参数而不必每次改代码。
- 下一步：读 `calibration_manager_request_marker_training()` 的前置条件（codebook/offset/persona/
  环状态），先判断是参数问题还是需要先切测量档（P2/P3 的分离规则可能同样适用于 T0）。

**2026-11-07 09:3x T0 请求门槛（`00283cb5`，build `20261107010829`）**：

- **根因**：T0/T1/T2 把 32 位 `plan.generation` 当 `train_epoch/sequence/marker_id` 传给
  `calibration_manager_request_*_training()`，而 manager 要求 `train_epoch ∈ 1..255` 且三者相等
  （`calibration_manager.c:3030-3031`）。generation 一旦 >255，T0/T1/T2 **必然**被拒
  （`reason=13`），与窗口/档位/persona 无关。
- 修法：`app_ctrqs_marker_epoch() = generation % 255 + 1`，四板由同一 generation 确定性派生，
  身份一致；32 位 generation 继续用于计划与证据追溯。
- 实板：修复前 T0 一进 T0_RUN 就 `reason=13, trials=0`；**修复后 T0 正常执行到 `trials=1/1`**，
  但 marker 训练**超时**（`reason=11, failed_mask=0x1`）。即"请求被拒"已解决，
  剩下的是训练时序/预算，归 M6.5 继续。
- 另记：`CALIBRATION_TRAINING_PHASE_MAX_OFFSET_SAMPLES = 10`，T0 的 `offset_sample_count`
  上限即 10（传 32 时计划被拒）。探针已加通用入口 `ga`，T0/T1/T2 可直接试参数。

**2026-11-07 09:5x T0 预算锚点（`135e455f`，build `20261107013327`）**：

- T0_PREP 等到停机完成后才进 T0_RUN，但 trial 起点仍锚在**窗口开始**（`t0_ms`），
  截止期 = `t0 + APP_CTRQS_T0_STEP_MS`；低速率档停机+收敛要几秒 → T0_RUN 第一个 service 拍
  就已过期，marker 从未注入即判超时（实测 T0_RUN 存在 <1 s）。
- 改为 PREP 与 advance 都把 `t0_trial_t0_ms` 锚在 `now`、`deadline = now + STEP`
  （每个 repeat 都有完整预算，窗口 `t_end` 仍是上限）。
- 实板：T0 现在**确实跑满预算**，但 marker 训练**仍不完成**：NO.2/3/4 `reason=11,
  failed_mask=0x1`；NO.1 仍显示上一轮 P3 的陈旧结果（其 T0 未受理，属另一现象待查）。
- 结论：R5 从"预算被吃掉"推进到"训练本身不完成"——下一步要读 marker 训练快照
  （state/结果位/consume 计数）判断是未注入、未同步还是被拒。

**2026-11-07 10:1x T0 立即超时的下一步取证（build `20261107013327`）**：

- 探针补了"等执行器回 IDLE 再触发"（此前在 RESTORE_WAIT 期间触发会被静默挂起，
  计划进度保留上一动作的数字——这也是之前 NO.1 显示陈旧 P3 结果的原因）。
- 加上空闲等待后，T0 四板都进入 `T0_PREP(16)`，约 0.5 s 后统一进 `27(REBUILD)`。
- `PLAN?` 显示 `prog_status=2, prog_reason=11, trials_done=1, failed_mask=1, **abort_site=0**`：
  即失败**不是** `app_ctrqs_abort()` 造成的（那会写 abort_site），而是
  `app_ctrqs_t0_step()` 顶部的 `now >= deadline` 分支（`app.c:2689-2695`）直接判失败，
  随后 finalize 按"有 failed_mask 即 STOPPED"报 `reason=11`。
- 矛盾点（下一轮首要）：PREP→RUN 已把 `deadline = now + APP_CTRQS_T0_STEP_MS(6000)` 锚在 now，
  但实测 0.5 s 就判过期 → 说明 `deadline_ms` 在 PREP 与 RUN 之间被别处覆写，或 RUN 用的不是
  这个字段。下一步在 `t0_step` 里把 `now/t0_trial_t0_ms/deadline_ms` 三者一起打进诊断
  （或逐个检查 `deadline_ms` 的其它写入点）再修。

**2026-11-07 10:2x T0 拒绝原因取证（`53330afb`，build `20261107021142`）**：

- **拒绝原因可回读**：把 marker 快照的 `reject_reason` 记进 `hard_fail_reason`，finalize 在
  `failed_mask!=0` 时优先发布它。实板读数 **`prog_reason=6` =
  `CALIBRATION_TRAINING_MARKER_REJECT_CRC`**（`calibration_training_marker.h:52`）——
  marker 训练**确实跑完**，是观测 CRC 与请求不符，而不是超时。
- **codebook 准入**：T0 原要求 `codebook ∈ 1..3`，manager 接受 `0..M255_MANCHESTER_32`、
  工具默认 0。改为与 manager 同范围；但用 codebook 0 复跑仍 `reason=6`，说明 CRC 不符另有原因。
- 探针每轮触发前等四板回 IDLE（此前在 RESTORE_WAIT 触发会被静默挂起，这才是"NO.1 未受理"的真因）。
- 整链复跑：P1 4/4 → P2 → P3 12/12（`0xFFF`）全过；T0 仍 `failed_mask=0x1, prog_reason=6`。
- **下一步**：CRC 不符的最小怀疑是 marker 身份/载荷与 phys 观测不一致（epoch 派生、marker_id、
  codebook 载荷编码三选一）；读 `calibration_training_marker.c:139-180` 的 CRC 判定输入与
  `marker_crc32` 的生成处，对比请求侧与 phys 侧的构造差异。

**2026-11-07 11:0x 时间参数定案（经子代理 `497427e6` 独立复核后改写）**：

- **字段语义**：`link_base_delay_ns` 是**采样相位基线**，写进设备前必须已折半。
  固件只做一次换算 `cycles = round(base_ns / sample_period_ns) + offset`，
  **不再除以 2**（`components/calibration_manager/src/calibration_training_phase.c:20-23`；
  T0 的 capture 相位同源 `calibration_training_marker.c:14-17`）。采样 4 ns（250 MHz），
  `MAX_PIO_DELAY_SAMPLES = 31`（=124 ns），`offset ∈ ±10 samples`（±40 ns），
  且 `base+offset ∈ [1,31]`（`components/tdma/src/tdma_ring_runtime.c:260-266`）。
- **现行管线（关键，不是"实测单向/2"那么简单）**：
  ① P3 双向对打测得 `path_sum = (t4-t1) - (t3-t2)`，`delay_estimate_ns = path_sum / 2`
  （`tools/calibration_ring_validate/calibration_link_p3.py:313-315, 385`），实板 80/82 ns；
  ② 验收管线把 `delay_estimate_ns` 当"测量值"，再应用 baseline divisor：
  `base = round(delay_estimate / 2)`（`tools/hardware_acceptance/workflow_steps.py:2468-2492, 3637-3639`），
  实板 `link_base_delays = [41,40,40,41]`（`out/HardwareAcceptance/20261107/p3-103206/`）。
  净效果 **base = path_sum / 4**。
- **契约矛盾（OPEN，见下方残余登记 R9）**：`calibration_stage_derive.h:39-43` 说该函数的输入应是
  **往返（round-trip）**值、由它折半一次；而 `workflow_steps.py:2488-2489` 的注释说
  divisor 是"在更晚阶段"再应用的。两者不能同时成立，仓库里没有文档解释 "base = path_sum/4"。
  ⇒ 在契约裁决前，**"40 还是 80"只能按管线口径（40）执行，不能当作物理定论**。
- **命名陷阱（已踩）**：C 段里 T0 叫 `link_base_delay_ns`、T1/T2 叫 `link_delay_ns`，
  但三者在 `calibration_manager_request_*_training()` 的形参**都叫 `link_base_delay_ns`**
  （`calibration_manager.c:3109 / 3370 / 3164`）。T0/T1/T2 **同语义**：都要填 base（管线口径）。
- **A/B 证据（限定范围）**：同 build、同四板、`--reuse-ring-identity`、codebook 1、offset=0，
  `--link-delay-ns 160`（base 80 → 20 cycles）→ `MANCHESTER` 失败；
  `--link-delay-ns 80`（base 40 → 10 cycles）→ `NONE` 通过。
  **该 A/B 只证明"offset=0 时 10 cycles 通过、20 cycles 不通过"**，
  不能证明 10 cycles 是物理最优（base 80 + offset −10 同样得到 10 cycles）。
- **待办**：用管线口径的 base（41/40/40/41）复验 APP 的 T0；通过则 `REJECT_CRC` 闭，
  但仍需 R9 的契约裁决与 offset 扫描，才能把"物理最优相位"定下来。

**残余登记（本轮）**

| # | 残余 | 状态 | 触发条件 | 影响面 | 复验判据 | 闭环里程碑 |
|---|---|---|---|---|---|---|
| R9 | "base = path_sum/4"（双重折半）无契约解释，两处实现注释互相矛盾 | OPEN | 任何人改 P3→base 管线或直接手填 base | 相位差 40 ns ≈ 10 samples，静默失败 | 在 `CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md` 或等价契约处写明"测量值→base"的唯一换算链并登记 | M6.7 |

**2026-11-07 11:2x T0 通过（管线口径 base=40）；T1 卡在请求前置**：

- **T0 ✅**：整链 P1（四板 4/4）→ P2 → P3（四板 `0xFFF`）→ **T0 四板
  `prog_status=1, trials_done=1/1, passed_mask=0x1, failed_mask=0, reason=0`，阶梯推进 4**。
  证实 `REJECT_CRC` 的根因就是 base 口径（40 = 管线值；80 = 实测单向值误填）。
- **T1 ⏳**：只有链路端点两板被拒（`prog_status=2, prog_reason=13, abort_site=19 = T1_PREP`），
  非端点板按设计 no-op pass。已排除 `launch_guard > base_samples`（guard 4→12 仍拒）。
  下一步候选：`calibration_manager_request_sck_training()` 的外层前置里
  `s_marker_active / s_clk_coded_active / s_data_active` 是否仍为真
  （`calibration_manager.c:3414-3417`）；`stop_marker_training()` 只发 STOP 意图、
  由 core1 异步清除（`:3144-3148`），需要确认 T0 结束后该标志确实已落。
- **新发现（阶梯分叉）**：T1 在 NO.1/NO.2 失败、NO.3/NO.4 成功 → 四板 `ladder_idx`
  变成 4/4/5/5，**阶梯不再一致**。这意味着"部分板失败"会破坏 M6.2 的一致性前提，
  后续动作会因期望动作不同而互相拒绝。需要机制（或在探针侧）做阶梯重同步；
  这条单独记为残余 R10。

**残余登记（补充）**

| # | 残余 | 状态 | 触发条件 | 影响面 | 复验判据 | 闭环里程碑 |
|---|---|---|---|---|---|---|
| R10 | 部分板失败后 `ladder_idx` 四板分叉，后续动作互相拒绝 | OPEN | 任一动作只在部分板成功 | 阶梯无法继续推进 | 提供一个显式的阶梯重同步手段并验证四板一致 | M6.2/M6.7 |

**2026-11-07 11:3x T1 阻断取证（设备侧对照）**：

- **不是残留活动**：四板 `READ:CALibration:ACTive?` 均返回 `"NONE","EMPTY",0,0,0,"NO_ACTIVE"`。
- **不是 app 阶梯问题**：用 host 工具 `calibration_sck_train.py`（绕开 CTRQS 阶梯）在 link 0
  直接发 `CALibration:SCK:ARM`，报 `SCK state timeout, accepted=(1,)`。
  **注意语义**：`accepted` 是 `wait_states()` 的**期望状态元组**（`(1,)` = `STATE_PREPARED`），
  **不是"被接受的板列表"**（`calibration_sck_train.py:601-613`）。真实情况是
  **两块端点板都没到达 PREPARED**，且 ARM 命令本身返回 OK（否则工具会在 `sck_arm()` 里先抛
  `SCK ARM rejected`，`:594-597`）。带 `--reuse-ring-identity`（跳过净场）与不带
  （完整 STOP/OPMODE:APPLY/TOPOLOGY）结果相同。
- 已排除：`launch_guard > base_samples`（12 > 10 满足）、offset/search 边界（±10 内）、
  环净场与否、活动标志残留（四板 `NO_ACTIVE`）。
- **当前最可能**：ARM 被接受并发布了 intent，但 **core1 侧没有把它推进到 PREPARED**
  （marker 路径同机制能到 state=4，说明不是全局握手坏了，而是 SCK 分支内部某步失败且未落快照）。
- 下一步：在**被拒的那一端**读 `READ:CALibration:SCK?` 与 `READ:CALibration:ACTive?`，
  并逐项比对 `T1 请求参数 → calibration_training_sck_request_valid()` 的 18 条前置
  （`calibration_training_sck.c:35-63`），定位具体是哪一条不成立；必要时在该函数里
  加一条"首个失败项"诊断位（类似 P3 的 abort_site）。

**2026-11-07 11:4x T1 根因（子代理 `274a7339` 独立取证 + 三条前提更正）**：

- **更正 1（读数语义）**：`CAL:SCK:ARM` 的 "OK" 是 **host 合成**的
  （`tools/tdma_ring_monitor/tdma_start_ring.py:325,353-359` 的 `ack_only_actions` 超时桩 +
  `scpi_serial.py:300-301,448-455` 的 matcher 丢弃），固件真正拒绝时发的
  `-200,"CAL_SCK_ARM_REJECTED"`（`scpi_port.c:284-303`）被当噪声丢掉。⇒
  **"ARM 返回 OK"不能作为"请求被接受"的证据**；`accepted=(1,)` 只是 `wait_states` 的期望状态。
- **更正 2**：`abort_site=19` 是 **`APP_CTRQS_T1_RUN`**，不是 T1_PREP（枚举 `app.c:1727-1758`：
  18=T1_PREP、19=T1_RUN；`app.c:2014` 存的是 `s_ctrqs.state`），即
  `app.c:2802-2812` 的 `request_sck_training()==false`。
- **更正 3**：`READ:CALibration:ACTive?` 报的是校准 **path 快照**
  （`scpi_calibration_capture_commands.c:1066-1085`），与 `s_sck_active/s_marker_active/...`
  **无关**；此前"已排除活动残留"的结论**作废**。
- **根因（H1，两端共有）**：`calibration_manager.c:3430-3432` 的外层判据
  `guard_sample_count − base_samples + search_start ≥ 0`。
  base=40 ns → base_samples=10；此前 guard=12、search_start=−10 → **−8 < 0 → 直接拒**。
  等价要求 `guard ≥ base_samples + |search_start|`，即 **guard ≥ 20**；
  工具默认 guard = **32**（`calibration_sck_train.py:455`，官方样例串 `…,32,40,0,-10,10,…`）。
- **内层/外层不等价**：内层 `calibration_training_sck.c:48-51` 只要求 `guard > base`，
  外层 3430 额外要求 `+ search_start ≥ 0`。此前"guard 12>10 已满足"只验了内层，是误判。
- **修复方向（参数层，无需改固件）**：T1 计划用 `launch_guard ≥ base_samples + |search_start|`
  （如 32）；并建议在 app 的 `plan_valid` 里补同一判据，让失败提前到计划阶段而不是 T1_RUN。
- 本轮末次复跑无效：`gen` 未更新（板仍 1025，`epoch` 96/95 不一致）→ 命令面投递失败，
  不作 T1 结论。

**2026-11-07 11:5x M6.5 达成（T0/T1/T2 四板全绿，build `20261107033453`）**：

- **T0（marker）**：`link_base_delay_ns = 40`（管线口径 = 实测 80 / 2）→ 四板
  `prog_status=1, trials_done=1/1, passed_mask=0x1, failed_mask=0, reason=0`，阶梯→4。
- **T1（SCK）**：计划 `launch_guard=32`、`search=-10..10` → 四板全绿，阶梯→5。
  判据是外层 `guard − base_samples + search_start ≥ 0`（`calibration_manager.c:3430-3432`），
  base=40 → base_samples=10，故 `guard ≥ 20`；**内层只要求 `guard > base`，两者不等价**。
- **T2（DATA）**：`guard` 在 app 里**硬编码 4**（`app.c:2892`），故 `search_start` 必须满足
  `10 + (search_start − 4) ≥ 0`，即 **≥ −6**；用 `search=-4..10` 后四板全绿，阶梯→6。
- **重要教训（R13）**：T1 连续失败两次后，**每次后续请求都会被拒**——失败的请求留下了
  未被 core1 消费的 intent（`pending.sequence != consumed`，`calibration_manager.c:3418-3421`），
  只有重启后的首次尝试才能成功。这是"失败即不可重试"的可用性缺陷，需在 core1 侧保证
  失败 intent 也被标记消费（或允许覆盖）。
- **阶梯一致性**：T1 期间曾出现 NO.1/2 在 4、NO.3/4 在 5 的分叉（R10）；T2 两端板追平后
  四板重新一致到 6。分叉期间非端点板的 no-op pass 会"空推进"阶梯，需机制约束。

**2026-11-07 11:5x–12:0x M6.6 起始（T3）**：

- T3 的计划准入只看 `profile_level != 0`（`app.c:2392-2398`），C 段被忽略，阶段里已带训练相位；
  只有它能以 10 MHz 起环（`app_ctrqs_low_rate_bringup()` 对 T3 不放行 BRINGUP，见 `app.c:1897-1905`）。
- 实测：`CONFig 7,…` 在四板落地（`action=7, gen=1061`），补发 `TRIGger 7,142` 后四板
  `epoch=142` 已更新，但 **执行器仍是 `ctrqs=0`（IDLE）**、进度停留在 T2（`prog_status=1,
  trials_done=1` 来自上一动作），`ladder_step=2`。即 **T3 动作没有被启动**，不是被拒（无 `reason`）。
- 下一轮取证：用 0.2 s `stateprobe` 观察触发后 60 s 内是否出现 `T3_PREP/T3_RUN`，
  以及 `SYST:TDMA:GUIDE:PLAN?` 的 `pending`/`ctrqs` 变化；据此判断是
  "触发器未被 `app_cal_guide_post_trigger` 接受" 还是 "接受了但 `app_ctrqs_start` 早退"。
- 附带：T3 的 CONFig 在 NO.1 上首次投递失败（`gen` 停在 1056），重试后成功——
  命令面在长时间运行后偶发丢一次，属于台架已知抖动。

**2026-11-07 12:0x M6.6 T3 进展（build `20261107035659`）**：

- **修复生效**：T3 现在从 FANOUT → `T3_PREP` → **`T3_ARM`(25)**，不再直接掉进通用 REBUILD；
  `T3_PREP` 也已 stage/apply 计划的 `profile_level`（只有 T3 允许以测量档起环）。
- **仍失败**：`T3_ARM` 在 60 s 预算内未形成 10 MHz 环 → `abort(NOT_ONLINE_SAFE)`，
  四板 `abort_site=25`。40 s 采样显示进入 `T3_ARM` 后 **`enabled/up/down/adapter` 全 0、
  `ring_last_error=0`，状态与错误码都不再变化**——既看不到 arm 尝试，也没有错误，
  说明卡在 `T3_ARM` 的**前段**（`set_tdma_ring_topology` 或 `ring_quiescent` 等待），
  而不是 arm/start 被拒。
- 已排除：S 段提交（`commit_appl=1, commit_ok=0` 在"同代已提交"时是预期：
  `app.c:2164-2172` 只有 `COMMIT_ALLOW && applicable` 才调用提交；`decision=2` 非 ALLOW）。
- 下一轮：给 `T3_ARM` 的三个前段步骤（topology / quiescent / arm）各加一个"停留站点"诊断位
  （复用 `abort_site` 的思路），一次上板即可定位卡点。

**2026-11-07 12:3x T3_ARM 分层诊断结果（build `20261107041636`）**：

- 新增 `abort_site = 100 + 站点` 诊断（站点：1 拓扑尝试 / 3 拓扑完成查 quiescent / 4 进入 arm 段 /
  5 发出 arm / 7 已形成），实板读出 **`abort_site=104`**：四板都过了 quiescent、进入 arm 段。
- **决定性**：`SYST:TDMA:RING:ARM:STATus?` 四板均返回 **1 = `DISTRIBUTED_REFMEM_TDMA_ARM_OK`**
  （`distributed_refmem.h:97`）——**10 MHz 的 ARM 是成功的**，校准门（`CALIBRATION_GATE_REJECTED=7`）
  也没有拒绝。
- **真正卡点**：`T3_ARM` 后半段 `distributed_refmem_tdma_ring_start()` 的返回值被忽略
  （`app.c:3640` `(void)`），随后只在 `restore_formed()` 成立时才算成功；实测 START 之后
  环仍 `enabled=0`，60 s 到点 `abort(NOT_ONLINE_SAFE)`（stage 保持 104，因为下一拍又先置 4）。
  即：**ARM 成功、START 没有把环带起来**，且 START 没有可回读的结果码。
- 下一轮：给 START 段加同样的"站点 + 结果"诊断（记录 `ring_start()` 返回值与调用前后
  `enabled/adapter_started/config_seq/applied_config_seq`），并核对 10 MHz profile 是否真的
  被 `stage/apply` 接受（在 T3_PREP 记录 `stage/apply` 的返回值）。

**2026-11-07 12:4x T3 START 前置读码（主会话独立取证）**：

- 调用链：`distributed_refmem_tdma_ring_start()`（`distributed_refmem_status.c:1282-1287`）→
  `tdma_service_ring_start()`（`tdma_service.c:1027-1036`）→
  `tdma_ring_runtime_set_data_enabled(runtime, true)`。
- **前置条件**（`tdma_ring_runtime.c:508-529`）：`enabled != 0`、`adapter_started != 0`、
  `adapter_stop_pending == 0`、`adapter_config_seq == config_seq`、
  `applied_config_seq == config_seq`、`train_command_seq == train_accepted_seq`。
- 实测 T3_ARM 期间 `enabled=0`（连续 40 s）⇒ **START 必然返回 false**（新站点码 60），
  不是"START 成功但没使能"。
- 所以真正的问题是：**ARM 返回 `TDMA_ARM_OK` 之后，环的 `enabled` 从未置 1**。
  这指向 ARM 的"接受"与 runtime 的"配置生效（apply）"之间存在缺口，
  而不是 T3 的 START 调用时机。
- 下一步（与子代理 `ece03fbd` 交叉后定）：确认 `enabled` 由谁置位（apply/control 循环）、
  以及 T3_ARM 在 `set_tdma_ring_topology` 之后是否缺少一次显式 apply/control 推进。

**工作规则：阻塞点默认开只读子代理交叉审核（2026-11-07 起）**

长期任务跨多轮、多次上下文压缩后极易复用旧结论。实测两次，子代理都推翻了主会话当时的判断：

| 阻塞点 | 主会话当时的判断 | 子代理独立取证 | 结果 |
|---|---|---|---|
| P3 四板掩码 `0x7/0x3F` | "逐链路物理不对称" | `P3_RUN` 只在 P3_PREP 赋值、`P3_STOP` 每拍用陈旧判决盖章 ⇒ 前缀掩码 | 改判并修复，M6.4 四板 `0xFFF` |
| T3 START 失败 | "ARM 接受与 apply 之间有缺口" | ARM 内**同步**置 `enabled=1`；是下一拍 T3_ARM 自己的 quiescent 门把刚 arm 的环停掉，且 `t3_armed` 锁存无回退 | 按此改（quiescent 门仅在未 arm 时生效 + 等 adapter 起来再 START + START 失败清锁存） |

**执行方式**：命中 §7.4 任一条件（同一子项连续 2 次失败 / 现象复现 ≥2 次 / 证据互相矛盾 /
需要不可逆决策 / 长循环 ≥3 轮未收敛）即开只读子代理，给它：目标与里程碑、已做动作、
完整失败命令与读数、证据路径、已排除假设、只读约束；要求它给"独立结论 + `文件:行` 证据 +
与主会话结论的异同 + 最小下一步"。主会话必须记录交叉分析与采信理由后才继续上板。

**工作规则二：每轮开始时，上一轮的"下一步 / 偏差 / 残余"必须先经子代理独立交叉分析**

- **触发**：每一新轮的第一件事（在开任何新改动、上板之前）。
- **输入给子代理**：上一轮 §8 报告的"第 6 段下一步"、第 4 段偏差与残余表（含状态与复验判据）、
  相关的提交号/凭证路径/读数文件，以及"上一轮声称的 ✅ 项"。
- **要求它独立回答**：
  1. 每个 ✅ 声称是否真的有仓库内证据（凭证/读数/提交）支撑，有没有"未验证却记完成"；
  2. 每条残余的 `OPEN/MITIGATED/CLOSED` 是否与代码/证据相符，`CLOSED` 是否有证据；
  3. 残余表是否**遗漏**了由近期改动新引入的风险（例如某修复可能引入死循环/新盲区）；
  4. "下一步"是否仍然成立（前提有没有被上一轮的改动推翻）；
  5. 台账里是否有因修复而**过时**的表述（需改写而不是继续引用）。
- **主会话义务**：记录子代理 id、结论、与主会话判断的差异、采信理由；发现过时表述必须当场改写。
- **理由**：长期任务里最贵的错误不是"卡住"，而是"把没验证的写成完成"或"继续引用已过时的结论"。

**2026-11-07 13:1x 交叉审核改判：证据口径与 T3 三处缺陷（子代理 `7b9a0f5c`）**

> 本节**取代**本台账中所有"T3 已收口/已成功"的暗示；下列 8 条为审核给出的、必须按此理解的口径。

1. **M6.1–M6.5 的 ✅ 口径更正**：这五项的判据来自**实板 `PLAN?` 实时读数**（当时未落盘归档），
   而所引 P3 凭证（`p3-053100/p3-060355/p3-064615/p3-083723/p3-113433` 等）均为
   `PASS_WITH_WARNINGS` 且 `strict_gates_passed=false`（唯一失败项是 450 s 时间预算被
   `DEBUG_BOUNDED_FORCE_CONTINUE` 放行）；其中 **`p3-053100`（M6.1 引用）还带一条真失败
   `four-Node TDMA closed loop`（step 26）**。即：**P3 凭证的 scope 不覆盖 GUIDE 阶梯**，
   这些 ✅ 只能表述为"实板读数达标 + 凭证不覆盖该路径"，不能表述为"已过 P3 验收"。
2. **T3 三处缺陷（审核 N1/N2/N3）**：
   - **N1（高，已修）**：`T3_ARM` 的"START 失败后清 `t3_topo`"会让下一拍先走拓扑块，
     而拓扑要求 `enabled==0`、此时 `enabled==1` ⇒ 永远卡在拓扑、连 quiescent 门都到不了，
     60 s 后只剩 `abort_site=101`。改为**不清 `t3_topo`**，只清 arm 并主动 STOP 回静默。
   - **N2（中，已修）**：arm/START 无尝试上限（`arm_tries` 原本是死代码）；已在 START 失败
     分支加计数，达 `APP_CTRQS_ARM_RETRIES` 即 fail-closed abort。
   - **N3（高，已修）**：`REBUILD` 回退路径曾把 T3 直接标 `passed_mask|=1 / COMPLETE`，
     使**失败的 T3 被回退成功掩蔽**；已删除该段——T3 结果只能来自 `T3_ARM` 成功站点。
   - **附带（已修）**：`abort_site` 是独立静态量、每轮不清零，跨轮粘滞会误读；已在
     `app_ctrqs_start()` 里随 `memset(&s_ctrqs)` 一起清零（连同 T3 站点）。
3. **R16/R17 改判**：R16（START 返回值被丢弃）→ **CLOSED**（工作树已捕获返回值并编站点码）；
   R17（`enabled` 置位路径未定位）→ **CLOSED**（唯一写点 `tdma_ring_runtime.c:449`，
   经 `tdma_service_ring_arm → configure` 同步置 1）。
4. **R13 改判**：core1 侧 intent 消费是**无条件**的（`calibration_manager.c:2522-2523` SCK、
   `:2387-2388` DATA），"失败留下未消费 intent"**不能由代码解释**；改为"待实板复现"，
   不作为已确认机制引用。
5. **T3 判据加强**：只读 `enabled=1 && adapter_started=1` 不够（空转 60 s 也满足）。
   必须同时满足：`up_running=1 && down_running=1`、`adapter_service_count` 有增量、
   `abort_site=0`，且成功来自 `T3_ARM` 站点（106/7）。
6. **`data_enabled` 无 SCPI 出口**（`scpi_tdma_snapshot_commands.c:869-921` 无该字段）；
   可用 `adapter_service_count` 增量 + `up/down` 两次采样间接判定。
7. **过时表述**（本台账内需按此理解）：`START 返回值被忽略`、`站点表 1/3/4/5/7`、
   `enabled=0 指向 ARM→apply 缺口`、`下一步确认 enabled 由谁置位`、`卡在前段 topology`、
   `M6.6 判据只查 enabled+adapter`、`T3 复用 REBUILD 成环路径` —— 均**已被证伪或取代**。
8. **"下一步"口径**：`T3_PREP` 修复与三处新改动**均已有 build 与 P3**（`20261107035659` /
   `20261107045359`，后者见 `p3-125344`）；"待取证"只对 **T3 行为**成立（QUICK_DIAGNOSTIC 不跑 GUIDE 阶梯）。

**2026-11-07 13:4x 二次审核改判（子代理 `11e83025`）——残余状态与判据修正**

1. **R18 不成立 CLOSED**：重臂上限只有代码、**零实板证据**；且只对 START 拒绝计数、ARM 失败不计；
   `arm_tries` 与 FORWARD 路径共用（`app.c:3118` 只增不读）。改记 **MITIGATED-未验证**。
2. **R16/R17**：仅**代码层** CLOSED（`:3666` 已捕获 START 返回值；`enabled` 唯一写点
   `tdma_ring_runtime.c:429/449`），T3 行为仍无实板证据。
3. **正确凭证（以磁盘 receipt 为准）**：`p3-131415`（build `20261107051434`）含 round-36 的四处改动；
   其后 `p3-133844`（build `20261107053903`）与 `p3-140351`（build `20261107060408`）分别含后续改动。
   三者均为 `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`、`strict_gates_passed=false`，**不跑 GUIDE 阶梯**，
   不可引作 T3/P1 等动作的证据。
4. **新增残余 N4–N9**：
   - **N4（高）**：`PLAN?` 的 progress 是"上次 Q"的读数、不带 epoch；而复跑工具
     （`tools/domains/trigger/trigger_debug_demo/calibration_workflow.py:202-230`）只判
     `progress_valid && status==1 && failed_mask==0` ⇒ **陈旧 Q 会被判 pass**，足以把 R15 "假闭合"。
   - **N5（中）**：T3 成功判据不绑定速率（level 23 的 10 kHz 也能 COMPLETE，`PLAN?` 上与 10 MHz 同构）。
   - **N6（中）**：T3 成功复用开窗前 `restore_*` 身份（`candidates[0]%4 != reference_slot` 时永不可达，abort 106）。
   - **N7（低）**：START 无锁存时每 150 ms 重发，瞬时拒绝会拆掉正在成形的环。
   - **N8（低）**：abort 后同拍可二次 abort 覆盖诊断码；`s_ctrqs_abort_site` 有重复声明（既有）。
   - **N9（操作）**：阶梯在 RAM，重启后 T3 会被 `plan_valid` 拒（status=3/reason=13/abort_site=0），易误读为 T3 失败。
5. **本轮已按审核修**：N5（`app_ctrqs_t3_profile_formed()` 要求环周期 == staged 周期）、
   N7（`t3_started` 锁存）、N8（abort 后 `return`）；并新增严格验证入口
   `guide_cmdplane_probe.py verify <action> <epoch>`：要求**本轮新鲜度（epoch 匹配）+ 执行器空闲 +
   trials 计数 + passed 掩码 + failed=0 + abort_site=0 +（T3）周期=1 ms**，缺一即 FAIL——
   这是对 N4 的直接对策（陈旧 Q 会被判 FAIL 而不是 pass）。
6. **判据改写**：不再写"成功来自 `T3_ARM` 站点（106/7）"（`abort_site` 只在 abort 时写、成功恒为 0，
   站点无 SCPI 出口）；改为 **`status=1` + `passed_mask=1` + `trials_done=1` + `abort_site=0` +
   `epoch` 为本轮 + `ctrqs` 在本轮经历过 25/28**（`ctrqs` 需在同一轮内采样）。
7. **`arm_tries` 表述更正**：不是"死代码"，而是"只增不读"（`app.c:3118` FORWARD 重试也自增）。

**2026-11-07 14:1x 三次审核改判（子代理 `ae9a8a43`）——三条硬推翻**

1. **N5 原修复无效（已改）**：原 `app_ctrqs_t3_profile_formed()` 比较"环周期 == staged 周期"，
   而 ARM 就是把 staged 抄进 runtime（`tdma_service.c:1005-1013` → `tdma_ring_runtime.c:460`），
   成功路径恒真；且 T3 只校验 `profile_level != 0`，**level 23（10 kHz/150 ms）也会 COMPLETE**。
   改为在 `T3_PREP` 里对 staged profile 做硬门：`cycle_period_ns == APP_CTRQS_T3_PERIOD_NS(1 ms)`
   且 `baud_hz == APP_CTRQS_T3_BAUD_HZ(10 MHz)`，否则 `abort(CONFIG_VALIDATION_FAILED)`。
2. **verify 的 P1 判据不可满足（已改）**：原要求 `trials_done==4 && passed_mask==0x1`，
   而 P1 每个 trial 置一位 ⇒ passed=0xF、failed=0xE，必被 `no_fail` 否掉。
   已按动作区分掩码：P1=0xF、P3=0xFFF、P2/T0/T1/T2/T3=0x1。
3. **T3 的 10 MHz 证明链（14:5x 再改）**：`RING:CONFig?` 读的是 **staged**、无 baud 出参；
   而 `RING:STATUS?` 共 **47** 字段，`mean_period_ns` 是**倒数第二**（末字段是 `sample_count`），
   且 T3 窗口关闭会重放 boot 档并 `memset(loop_time)` ⇒ **用环回周期证 10 MHz 不可实现**。
   `verify` 改为断言**计划档位 `level == 7`**（=10 MHz/1 ms），并把 `ctrqs∈{28,29} + trigger_pending==0`
   作为"本轮已完成"的必要条件（单看 `ctrqs==0` 会被"尚未 take"骗过）。
4. **N8 经审核确认正确且必要**（abort 后若落回 formed 检查，同拍可把已 abort 的轮次改判成功）。
5. **N7 无新死锁，但有"一次 START 后无恢复"窗口**：锁存取自 API 返回值，若环 `enabled=1`
   但 up/down 不起，只能等 60 s 超时。
6. **新增残余 N10–N13**：
   - **N10（高）**：`c823922d` 同时替换了 P3 凭证（`p3-131415` → `p3-133844`），
     而本台账 13:4x 节仍写"正确凭证是 p3-131415" ⇒ **文档与已提交凭证矛盾**（以磁盘凭证为准）。
   - **N11（中）**：严格验证入口未入库、零实板运行证据；且 `guide_action()` 末尾调用的是
     **P1 专用** `verdict()`，任何非 P1 实跑都会打印 NOT-GREEN。
   - **N12（中）**：跟随板 P1 完成是"时间到 + 此刻处于 FORWARDING"一次性盖章全部 trials
     （`app.c:2104-2125`）⇒ `passed_mask=0xF` **不是逐 trial 证据**。
   - **N13（低）**：`verify` 早期版本用"环回周期"做 T3 速率判据不可实现（见第 3 条）；
     有 boot plan 的镜像在窗口关闭后会重放引导档（150 ms 周期，即 1.5×10⁸ ns），
     台架无 boot plan 时走 REBUILD、staged 仍是 level 7——**该判据依赖镜像形态，已弃用**。
7. **台账 ID 悬空**：R11/R12/R14/R15/R19 只在审核正文出现、无登记条目，需补登记或删引用；
   另多处引用的 `app.c` 行号已随改动漂移（引用时以符号名为准，不要只写行号）。

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
