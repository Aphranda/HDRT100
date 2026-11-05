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
8. **验收口径**：固件类改动必须 P3（`workflow_steps.py run --preset picoico2-v2-factory-candidate --tdma-only`）；
   `tests/`/`tools/` 改动走 host 凭证。并发会话改源码时**禁止跑验收**。

## 3. 已入库（截至 2026-11-06 00:15）

| 提交 | 内容 |
|---|---|
| `12b406e8` | hop 包络聚合（`rx_slot_hop_min/max` + `RING:RXSLOT?` 扩容）与 `guide_action_plan`（C 段载荷） |
| `8c1ebd3f` | 计划身份绑定既有 `refmem_param_pool_crc` |
| `d4706ad1` | R 段进度记录（48 B：身份/进度/失败原因） |
| `4f65b1a5` | R/Q 走 refmem 参数池：停环写、复环读回 + `guide_action_reconcile()` 四态决策 |
| `ac94916c` | 七动作阶梯推进 `guide_action_ladder_advance()` + S 段提交门（会话未关拒绝/已提交跳过） |
| `5f532dec` | 单口命令：`SYSTem:TDMA:GUIDE:CONFig/TRIGger/PLAN?` + 命令目录 50/51 |
| `22cdce32` | C 段本地派发修复（见阻塞点 1） |
| `cfc58f7e` | C/T 实测 runbook（§9.29） |
| `c9fe264b`/`4dc9a337`/`cb9fec84` | 复用清单、计划载荷登记、命令面与合并约定 |

## 4. 现状（已验证 / 未验证）

- ✅ **验证过**：计划载荷编解码、进度记录、对账决策、阶梯推进、提交门的 **host 单测**；
  单口 `CONFig` 在环跑起来后返回 `OK`，且 **NO.1 的 `PLAN?` = `1,1,305419896,7,23,4000,4,0,0`**。
- ❌ **未验证**：其余三板的 `PLAN?` 仍为 0（环上未送达/未派发）；`TRIGger` 仍返回 `-200`；
  R/Q/S 尚未在实际时序中接线（函数已存在但无人调用）；四板 CTRQS 闭环未跑过。

## 5. 三个阻塞点（附证据）

1. **inline 参数在节点控制记录里丢失**：`distributed_refmem_mirror_local_control()`
   （`components/distributed_refmem/src/distributed_refmem_link.c`）以 `params=NULL` 写记录，
   派发时读回全 0。已用"post 后本地直接 `refmem_domain_config_dispatch`"绕过（`22cdce32`），
   但 **这是绕过，不是根治**；根治应由 refmem 层把参数一并镜像。
2. **远端三板收不到**：同一个缺陷在远端路径同样成立（或环上命令帧未带 16 字 payload）。
   下一步先确认 `PLAN?` 在远端是否因参数为 0 被 handler 拒绝。
3. **`TRIGger` 返回 -200**：本地计划已就绪（action_id=1 匹配），所以要么是多节点目标路由
   被拒（环未 running 时 `unified_available=false`），要么 `refmem_domain_config_dispatch`
   对 trigger 的注册字数（2）与实际不符。需在一次环 running 的会话里复测并读 `SYST:ERR?`。

## 6. 下一步（按顺序，最短路径）

```powershell
# 1) 起 4 节点环（每板用本地槽位）
SYST:TDMA:RING:TOPology 4,<0..3>,0 ; SYST:TDMA:RING:ARM ; SYST:TDMA:RING:START
# 2) 只在协调者下发计划（参数必须逗号分隔！）
SYST:TDMA:GUIDE:CONFig 1,305419896,7,23,4000,4
# 3) 四板读回，确认远端是否落地
SYST:TDMA:GUIDE:PLAN?
# 4) 下发触发并读回
SYST:TDMA:GUIDE:TRIGger 1,55
```

然后：把 R/Q 段接到实际时序（`app_guide_scan_leave_window` 的停环/复环原语 + 每 trial
`app_cal_guide_publish_progress()`），S 段在训练会话关闭后调 `calibration_training_store_commit`；
最后串七动作循环。收口时跑一次 P3 + 四板 HIL。

## 7. 台架与操作注意

- 四板：COM5/COM4/COM3/COM6 = NO.1..4，当前镜像 `20261105160151`（P3 同源）。
- 混版会直接失败：P0T 入口现在有**板间固件版本一致门**（`bb2ceb91`）。
- P0T 抖动：workflow 已钉 `--short-open --gap 0.2`；standalone 连跑 5/5 通过。
- 验收抖动：TRN-03B 偶发 `explicit startup barrier timed out`，重跑一次即可。
- 命令语法：SCPI 多参数**必须逗号分隔**，空格会 `-101`。
- 本会话两次违规（新建 ledger / 新建相位机）已撤回，文件在 `out/rejected/`，**不要复用**。
