# 单口引导 CTRQS 交接（新会话入口）

Status: Active
Domain: calibration / refmem / tdma
Canonical: `docs/calibration/CALIBRATION_CTRQS_CHECKLIST.md`（本文）
Related: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`
Last updated: 2026-11-06

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
4. **残余**：参考板窗口偶发 FORWARD 超时（`reason=TIMEOUT`，trials=0），跟随板正常；
   窗口自恢复成功后可重触发。未定位确定性根因，按残余记录。

### 5.1 历史：B1 交叉排查结论（保留供追溯）

- 机制：`mirror_local_control()` 首写分支以 `params=NULL` 写节点记录，`refmem_node_control_write()`
  对 NULL 清零 16 字；且 `try_post` 不构造命令帧、节点记录按 slot 独立，远端收不到参数。
- 修复不是“补 mirror”，而是走既有带参数的 `control_submit`（本地帧回环 + 远端 fragment）。
- 旧 SCPI 本地直接派发补丁已删除，勿恢复。

## 6. 下一步

1. **P2 trial body**：按 C 段 `candidates[]` 驱动 coded marker 的 R 段（复用
   `calibration_manager_*` 训练原语与窗口原语，不新建相位机/存储），Q/S 复用现有路径。
2. P2 通过后逐动作推进 P3 → T0 → T1 → T2 → T3；只有 T3 的 S 允许 `ARM → START 10 MHz`。
3. 每次 P3 前确认四板 `SYST:FW:BUILD?` 一致；窗口偶发超时先重触发一次再归因。

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

## 7. 台架与操作注意

- 四板：COM5/COM4/COM3/COM6 = NO.1..4；混版会被 P0T 的固件版本门直接拒绝。
- **起环要在 P3 刚结束的 RAM 校准态下做**：先全板 `RING:TOPology 4,<slot>,0`，再跟随板
  `ARM`/`START`，最后参考板 `ARM`/`START`；参考板 `START` 偶发 `-200`，重发一次通常即成功。
- 窗口自恢复不依赖 boot plan（bench 镜像 `BOOT:PLAN?` 首字段为 0）；恢复用捕获的
  `node_count/local/reference` 走同一组 TOPO/ARM/START。
- 工具侧 STOP 合成串（`OK(no payload; verified by state readback)`）只在新鲜停止态读回后接受；
  其他命令的合成串仍按失败处理。
- 验收抖动重试一次；失败证据保留在 `out/HardwareAcceptance/<date>/<run>/`。
- 本会话两次违规（新建 ledger / 新建相位机）文件在 `out/rejected/`，**不要复用**。
