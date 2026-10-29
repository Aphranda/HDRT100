# REFMEM 统一控制面收口快照（2026-10-29 日间收口）

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/check/submissions/REFMEM_C11_REVIEW_04.md`
Last updated: 2026-10-29

## 1. 结论

**N6–N8 目标范围闭环；契约仍 `pending`，不得称"统一面完成/产品化"。**

- N8.5（T2 running-safe MOUNT）已实现并四板通过；**证据等级按用户 2026-10-29 选择 B**：QUICK P3 + 四板 HIL，`strict_gates_passed=false`，**不冒充 full**。
- `--full-bench` 卡在 TRN-03 SCK re-arm 预算，已另立台架任务（`REFTMEM-TASK-20261029-014`），不阻塞本次收尾。
- 契约：`REFMEM-CTRL-01` 保持 `pending`（C3 租约 / C6 fence 未实现）；`REFMEM-ENTRY-01` 按 C11-05 整改闭环转 `active`（2026-10-29）。

## 2. 子项总表

| 里程碑 | 状态 | 证据 / 说明 |
|---|---|---|
| N6.0 在线语义/schema | ✅ | 命令目录以 `config/refmem/command_catalog.yaml` 为准；function 表以 `function_catalog.yaml` 为准 |
| N6.1 类型+目录+入口 | ✅ | 生成器 + 静态表；`SYSTem:CTRL:EXECute` 协调者入口 |
| N6.2 DPLL RUN-safe | ✅ | 四板 `DPLL:ROLE? 0,0,3,3,0` |
| N6.3 TDMA 在线 | ✅ | seat 原子字 + load mask；20/22 运行时 `NOT_ONLINE_SAFE` |
| N6.3 VDC | ✅ | 四板 `FUNC_VDC_RECONFIG` `ack=14`；timing 读回 `12000,16000,6000` |
| N6.4 T2 | ✅ | 四板 `FUNC_T2_RECONFIG` `ack=14`；在线 MOUNT 见 N8.5 |
| N6.5 HIL/P3 | ✅ | 历史凭证见 §3 |
| N7.1 function 表 | ✅ | 6 个 function（条目数以 catalog 为准） |
| N7.2 执行器 | ✅ | 单 in-flight、串行 step、`ABORT_REMAINING`、有界重试 |
| N7.3 单命令 SCPI | ✅ | `CTRL:FUNCtion:RUN/STATus?/CATalog?` |
| N7.4 host + 四板 | ✅ | `ack=14` 读数 |
| N7.5 HIL/P3 | ✅ | 历史凭证 + N8.6 复跑 |
| N7.6 收口 | ⚠️ | 契约未转正（C11） |
| N8.0 基线 | ✅ | `out/n8/baseline.json` |
| N8.1 快照回正 | ✅ | `a01b8004` |
| N8.2 N5.5 入口边界 | ✅ | 架构「入口无关边界」；`REFMEM-ENTRY-01` |
| N8.3 NACK 可见性 | ✅ | `4dc41dfe`；P3 `20261028171345` |
| N8.4 N5.7 聚合 | ✅ | `4f7a94f0`；P3 `20261028180729`；实机 `acked/nacked` 变化 |
| N8.5 T2 running-safe | ✅（QUICK+HIL） | 四板 `31,14,14` `ACKED/ack=14`、环连续；证据等级见 §1/§4 |
| N8.6 四板 FUNC HIL | ✅ | `out/n8/four_board_hil/n86_summary.json`；`ack=14` |
| N9.0 基线冻结 | ✅ | `out/n9/baseline.json` |
| N9.1 证据等级接受 | ✅ | 快照/架构/登记表三处一致 |
| N9.2 C11-04 | ✅（结论 `PASS_WITH_CHANGES`） | `docs/check/submissions/REFMEM_C11_REVIEW_04.md` |
| N9.3 契约状态落地 | ✅ | `REFMEM-ENTRY-01` → `active`（C11-05 整改闭环）；`REFMEM-CTRL-01` 保持 `pending` |

## 3. 证据索引

| 类别 | 路径 |
|---|---|
| 当前 P3 凭证 | `config/hardware_acceptance/p3_acceptance_receipt.json`（build `20261029031043`、1613 files、`ae155f7d…`、QUICK/DPLL、`strict_gates_passed=false`） |
| 四板在线 MOUNT | `out/n8/n85_mount_four_board.json`（`state=4`、`ack=14`、`nack=0`） |
| 四板 function HIL | `out/n8/four_board_hil/n86_summary.json`（TDMA_ONLINE/CLOCK_ONLINE/VDC_RECONFIG `ack=14`） |
| N5.7 实机聚合 | `out/n8/n57_aggregate_com5.json`（`acked=1` / `nacked=1/reason=13`） |
| 单口机器捕获 | `out/n8/single_port_capture.json`（`port=COM5`、build 回读、`30/31` `ack=14`） |
| C11 记录 | `docs/check/submissions/REFMEM_C11_REVIEW_01.md`…`_04.md` |
| P3 全量尝试 | `out/n8/wf_full.log`、`wf_full2.log`、`wf_fullbench.log`（TRN-03 预算失败行） |
| N9 基线 | `out/n9/baseline.json` |

## 4. 开放项与残余风险

| 开放项 | 归属门 | 触发条件 | 影响面 | 复验判据 | 闭环里程碑 |
|---|---|---|---|---|---|
| `REFMEM-CTRL-01` pending | ⑤ | 任何"统一面完成"表述 | 契约不可标完成 | C3 租约 / C6 fence 实现 + C11 | 后续 |
| `REFMEM-ENTRY-01` active | ⑤ | 任何"未转正"表述 | 已转正但 host 总门禁未含新 runner | C11-05 整改闭环；host 门禁补 runner 另列 | 已转正（C11-05） |
| N8.5 证据等级 | ⑥ | 宣称 full 验收 | 不得冒充 full | 已按 B 接受；TRN-03 预算另立台架任务 | 已接受（非 full） |
| TRN-03 re-arm 预算 | ⑥ | `--full-bench` step 21 | 无法产 FULL 凭证 | 台架时序专项后重跑 | 台架任务 |
| host 门禁覆盖 | ⑥ | 依赖总门禁跑新 runner | 已闭合 | `host_acceptance.py` 改为 staged∪DEFAULT_PATHS，两个 C runner 进入每次 host 门禁（77 passed） | 已闭合（`409ebf2b`） |
| IF-012/IF-013 | ⑥ | Trigger/角度统一命令 | 接口域 `IN PROGRESS` | 8 条 ACK + 角度 RUN | 接口域 |
| EXE-SEAT-01 | ② | 相位预算 | 全量验收可能再红 | 用户签字 | 非目标 |
| OTA 工具不稳 | ⑥ | 大包传输 | 例行 OTA 改用 picotool 绕行 | 工具修复 | 独立 |

## 5. 契约与边界

- 单一 command slot、单一 `REFTMEM_CMD` 维护流、单一 ACK seat；无新增总线/payload class/第二命令槽。
- 入口语义（`REFMEM-ENTRY-01`）：非协调者远端在**入口侧**返回 `NOT_CONTROL_OWNER`（文本错误、不落 reason），**目标侧**授权拒绝落 reason 12；不中继、不排队、不建 relay。
- T2 在线：`T2_MOUNT` 在 TDMA 不停环下可用（同上下文清 TX/pending 后启动 T2）；运行中配置仍走 cached STOP 快照。
- 单串口：T2 配置/MOUNT/聚合已机器捕获；四域配置与 `FUNC_*` 的单口同批捕获未做（产品约束，未验证）。
- 不提供远程裸写、跨板阻塞查询或绕过本地门禁的强制位。

## 6. N9 剩余与外部项

1. N9.3 ✅：`REFMEM-ENTRY-01` 转 `active`（2026-10-29，C11-05 整改闭环）；`REFMEM-CTRL-01` 保持 `pending`（C3 租约/C6 fence）。
2. N9.5：最终 §8 报告（本文与进度日志为准）。
3. 外部：C11-05 复审、TRN-03 预算台架任务、EXE-SEAT-01 签字、host 总门禁补 runner。

## 7. 收口结论（N9，2026-10-29）

**N6–N8 范围闭环；N8.5 以 QUICK P3 + 四板 HIL 为证据等级（B 方案）；契约 `REFMEM-CTRL-01`/`REFMEM-ENTRY-01` 均保持 `pending`，不得称"统一面完成/产品化"。**

### 7.1 HAOFV 六门（统一面收口）

| 门 | 判定 | 依据 |
|---|---|---|
| ① 无新增总线/payload class/第二命令槽/第二 ACK | ✅ | 四个功能提交仅动 refmem/应用/SCPI；命令槽实例唯一 |
| ② 不占 core1 硬实时窗口 | ⚠️ | N8.5 在 Core0；无相位预算重算，EXE-SEAT-01 未签 |
| ③ 单写者/owner 边界 | ✅ | 状态实例 1；dispatch 与位置 TX 同属唯一 refmem 服务 |
| ④ fail-closed + reason/计数/快照 | ✅ | NACK 镜像 + reason 13/14；入口侧文本错误不冒充 reason |
| ⑤ 布局/版本/契约同步 | ⚠️ | wire/布局未变；契约未转正（C3/C6） |
| ⑥ host + 四板可复跑 | ✅（限等级） | host 75 passed、P3 QUICK、四板 `ack=14`；非 full |

**C1 Flash/XIP**：✅ 未触碰 flash 写路径。
**C2 时间事实**：✅ 未改 tick/时间差；N8.5 只动 position TX 状态。

### 7.2 契约状态

- `REFMEM-CTRL-01`：`pending`（C11-01/03/04 = `PASS_WITH_CHANGES`；C3 租约 / C6 fence 未实现）。
- `REFMEM-ENTRY-01`：`active`（2026-10-29，C11-05 整改闭环；host 门禁已于 `409ebf2b` 覆盖两个 C runner）。
- `REFMEM-TRANSPORT-01`/`SESSION-01`/`ACTION-01`/`LOCAL-01`：`pending`。

### 7.3 提交清单（截至 2026-10-29 12:20）

`a01b8004`、`2811ce31`、`bf811775`、`4dc41dfe`、`0525beed`、`20d16eb9`、`4f7a94f0`、`e2081303`、`e1d426a9`、`ae5a257f`、`196cd464`、`5c7899f5`、`b50f6c30`、`f07fb2b6`、`8ac3669c`、`7ba2b7d1`、`3085963c`、`09ab9791`、`16907b54`、`5ece5f79`、`22f7121d`、`ecf9cddd`；本轮收尾提交见 `git log` 顶部。
