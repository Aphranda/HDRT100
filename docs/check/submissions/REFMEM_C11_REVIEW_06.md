# REFMEM N10.2 环停远端命令 fail-closed 修复独立复核（C11 第六轮）

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_C11_REVIEW_06.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`, `docs/check/submissions/REFMEM_C11_REVIEW_05.md`
Last updated: 2026-10-29

## 1. 范围

- 对象：HEAD `488f8e19`（`5bb742fb` 修复；其前 `11a8fac8` 立项/复现）。只读复核：未改代码/登记状态、未碰硬件、未跑 HIL；P3/host 凭证以只读脚本按工具同算法复算指纹，并调用 receipt 证据校验器。
- 覆盖：cache 写点与早退、SCPI/submit 双层、reset 策略复用、矩阵用例、N10 文本与证据。
- 未验证：OTA 期间 cache 冻结的真实可达性（未上台架造场景）；HIL 的端口/拓扑仅由 `out/n10/trn03_positive3.log:2` 的 board→port 映射间接支撑。

## 2. 结论

**`PASS_WITH_CHANGES`**：修复成立——远端目标命令在入口侧（`scpi_refmem_snapshot_commands.c:946-949`）与提交侧（`refmem_control_submit.c:67-80`）都被拒绝，且拒绝先于 frame 构建、维护流投递与源记录写入（`refmem_cmd_load.c:439-452`），不产生 POSTED 记录、不占单槽。三项必改均为文本/证据/可观测性，不涉核心逻辑。
- **N10.2 可记闭环（有条件）**：作为"修复 + host + HIL"项成立；但 `REFMEM_DOMAIN_TODO.md:929` 的 `✅` 与 `REFMEM_UNIFIED_CONTROL_REVIEW.md:72` 的"已闭"先于本复核，须改为"C11-06 = PASS_WITH_CHANGES，必改 1–3 落地后闭环"，且不得读成"N10 完成"（N10.3–10.5 未做）。

## 3. 六门

| 门 | 判定 | 依据 / 缺口 |
|---|---|---|
| ① 无新增总线/payload class/第二命令槽/第二 ACK | ✅ | `git show 5bb742fb --stat` 只动 4 个 C、头文件原型（`distributed_refmem.h:377-385`）、1 个 host 用例与 2 个 receipt，无 wire/布局；命令槽实例仍 1（`distributed_refmem.c:116`）；拒绝后不投 fragment（`refmem_control_submit.c:73-76`），回程唯一。 |
| ② 不占 core1 硬实时/不拉长一等席 | ✅ | 新增只在 core0：写点在 `distributed_refmem_tdma_flight_sync_service`（`distributed_refmem_plane.c:716-740`），由 `distributed_refmem_service` 调用（`distributed_refmem_link.c:946`；任务 `app_tasks.c:117-133`）；ring 快照本来每 tick 已取（`plane.c:730`），未新增锁或席位；代价=每 1 ms 一次 32 bit 存 + 每条命令一次原子读；无 seat/phase 变化，不涉 EXE-SEAT-01。 |
| ③ 单写者/owner 边界 | ✅ | `s_ring_transport_ready_cache` 仅 core0 服务写（`plane.c:720,731,735`），SCPI/submit 只 acquire 读（`plane.c:708-714`）；host 只编 policy TU，跨 TU 无双实例；RUN 只读 active 边界未动。 |
| ④ fail-closed + reason/计数/快照 | ⚠️ | fail-closed 真：拒绝先于记录写入（`refmem_cmd_load.c:439-452`），HIL 记录号不变（§6-1）；但新失败路径**无 reason/计数/快照**、串口静默（§7-3），与 ENTRY-01"入口侧文本错误不落 reason"模型一致却无任何可读事实。 |
| ⑤ 布局/版本/契约同步 | ⚠️ | 无 layout/枚举/版本变化，P3 指纹与 HEAD 代码树逐字一致（§6-3）；但 `REFMEM-ENTRY-01` clause/check（`DOCS_REGISTRY.md:31`、架构 `:1713`）未含"环未运行"拒绝；`NOT_ONLINE_SAFE` 与部署期配置拒绝共用（架构 `:1697/:1725/:1727`）；非协调者+环停时实得 transport 错误而非条款写的 `NOT_CONTROL_OWNER`（`:946` 先于 `:953`）。 |
| ⑥ host + 四板可复跑 | ⚠️ | host：矩阵 `tests/unit/test_refmem_position_reset_policy.c:27-31` 经 `tests/python/test_refmem_host_units.py:20` 入总门禁，77 passed、指纹与 HEAD 一致；P3 为 QUICK/`strict=false`/step 26 既有失败，不执行本路径；HIL 双路径成立但 capture 无 build/ring 状态（§7-2）。 |

## 4. C1 / C2

- **C1 Flash/XIP ✅**：diff 无 erase/program、无 XIP/USB/SD/LCD、无临界区；新增状态在 SRAM，32 bit 原子读写。
- **C2 时间事实 ✅**：无 raw tick 回写、无新增 tick 差值；cache 是布尔量；`s_control_seq` 的 `osal_tick_ms()` 初始化（`refmem_control_submit.c:47-52`）未改。既有 `mark_timeout` 的 us/ms 单位问题不在本轮（`refmem_command.c:280-281`）。

## 5. 正确性

- **初值/未跑**：`static` BSS=0 → fail-closed；app 未 ready 或 core 未初始化时服务不跑（`distributed_refmem_link.c:900-902`），cache 保持 0；`enabled==0`/board5 显式写 0（`plane.c:719-721`）。
- **写点**：正常路径先取快照再用同一谓词（`plane.c:735-740`）；快照失败写 0（`:730-733`）；`owner==NULL` 早退不写（`:728`）——生产上 owner 初始化后不再复位（`tdma_runtime_owner.c:443-450`），仅防御性风险。
- **OTA 期间（⚠️ 残余）**：`distributed_refmem_service` 在 `ota_ao_is_active()` 提前返回（`link.c:931-933`），cache 冻结在最后值；若 OTA 期间从另一 SCPI 口 `RING:STOP` 后再下发远端命令，可绕过门禁（时长无界）。触发罕见（同口流式 OTA 时命令难插入），影响=回到 POSTED 堵槽；复验=OTA 中 stop ring + EXECute；闭环=N10.3 或另立。
- **环停竞态（⚠️ 残余）**：cache 每 1 ms 刷新（`app_tasks.c:132`），环停到归零 ≤1 tick 内仍可能受理；窗口毫秒级。复验=STOP 后紧接 EXECute 连测；闭环=另立。
- **双层一致性**：同一谓词、同一顺序（transport 先于 owner）；SCPI 回 `NOT_ONLINE_SAFE`，submit 返回 false；非 SCPI 调用者可经 `CTRL:FUNCtion:RUN` 走到 submit（`refmem_function.c:197-201`），失败被记为 `CONFIG_VALIDATION_FAILED` 终态，不堵槽但 reason 语义不精确。
- **语义复用**：`refmem_position_reset_admits` 现转发 `refmem_ring_transport_admits`（`refmem_position_reset_policy.c:6-22`），两者同义；谓词只证"enabled+started+applied"，不证对端在线/可应答，故只覆盖"环停"根因；"环起但对端失联"仍可能 POSTED（`distributed_refmem_command_mark_timeout` 全仓无调用点，仅 `distributed_refmem_link.c:1230` 定义）——既有残余，非本轮引入。
- **ABA/竞态**：单 32 bit 单写多读 + acquire/release，无 ABA；读-判-用之间环状态可变（见 1 tick 窗口）。
- **已知残余（错误文本无回包）**：与本修复**相关但非其引入**——`NOT_ONLINE_SAFE` 走既有 `scpi_port_push_exec_error`，与 `BAD_ARGUMENT`/`UNSUPPORTED_COMMAND` 同一路径（`scpi_refmem_snapshot_commands.c:917,931,948,968`、`scpi_port.c:248-254`），HIL 实测均表现为 `<timeout>`。**本轮不必处理**（改公共错误回包会波及全部 SCPI 命令，需 P3+四板），但须写进 §4 残余并给判读/复验判据：拒绝后 `CTRL:STATus?` 记录号不变、下一条 `EXECute` 仍可 `QUEUED`（本 capture 已示），并复验一次"拒绝后立即 `STATus?` 不被残留错误污染"（代码注释 `scpi_refmem_snapshot_commands.c:983-986` 记录过该风险）。

## 6. 证据

| # | 声称 | 实测（锚点） | 判定 |
|---|---|---|---|
| 1 | 环停拒绝且 `41` 仍 ACKED | `out/n10/n10_failclosed_final.json`：`30,14,14,…` → `<timeout>`；`CTRL:STATus?` = `140837,1533564105,4,140835,…`，仍是前一条 31 的 seq=140835/state=4，无新 POSTED；`41,1,1,…` → `"QUEUED",140836`、`state=4,ack=1`。 | 成立（无 build/ring 行，见 §7-2） |
| 2 | 环起 `30/31 ack=14` | `out/n10/n10_positive_hil.json`：`RING:STATus?` enabled=1/node_count=4/up=1/down=1/adapter_started=1；30→140834 `4,…,14`、31→140835 `4,…,14`。同 run 的 trn03 包装 `passed=false`（startup barrier timeout，`out/n10/trn03_positive3.log:10`），与 CTRL 结果无关。 | 成立 |
| 3 | P3 凭证 | `p3_acceptance_receipt.json:12` build `20261029052659`、`:9` 1613、`:8` `e144e97a…`；复算 `git ls-tree HEAD` = `e144e97a…`/1613；`validate_receipt_record` OK；`ota_summary`(d0e012fa)/pkg(7146bbd8) 哈希匹配；`:230` PASS_WITH_WARNINGS、`:244` strict=false、`:251` step 26 既有失败。 | 成立（QUICK） |
| 4 | host 77 passed | `host_acceptance_receipt.json:38` 指纹 `6f1fe281…`、`:39` 541，与 HEAD host 范围复算一致；`:31` `77 passed`。 | 成立 |
| 5 | "applied=3/2" | `CTRL:STATus?` 第 12 字段是 `last_reason_slot`（`scpi_refmem_snapshot_commands.c:1002`；写入 `refmem_cmd_reply.c:199` = `aggregate.last_slot`），不是 applied 计数（全局计数见 `distributed_refmem_cmd_reply.c:184`）；31 已 ack=14 却为 2，可反证。 | 口径不符（不影响 ack=14） |

## 7. 必改项（3）

1. **状态/契约文本**：`REFMEM_DOMAIN_TODO.md:929` 的 `✅` 与 `REFMEM_UNIFIED_CONTROL_REVIEW.md:72` 的"已闭"先于本复核（N10 硬门禁②）→ 改为"C11-06 = PASS_WITH_CHANGES，必改 1–3 落地后闭环"；`REFMEM-ENTRY-01` clause/check（`DOCS_REGISTRY.md:31`、架构 `:1713`）补"环未运行 → 入口 `NOT_ONLINE_SAFE`、不落记录"，并说明它与部署期 `NOT_ONLINE_SAFE`（架构 `:1697`）的区别及与 `NOT_CONTROL_OWNER` 的优先级。
2. **HIL 证据前提**：两份 capture 均无 `SYSTem:FW:BUILD?`，fail-closed 侧还缺 `SYSTem:TDMA:RING:STATus?`（"环停"只能由"同 boot + 同命令前一分钟 ACKED"推断）→ 同一次 capture 补 build + ring 行；同步更正 §6-5 的 `applied=3/2`。
3. **第二层覆盖/可观测**：submit 侧（`refmem_control_submit.c:73`）无测试且 HIL 不可达（SCPI 先拒）→ 补一次 `CTRL:FUNCtion:RUN` 环停（期望 function 终态 FAILED、记录号不变），或抽 host 可测最小函数；拒绝路径留可读证据（日志/计数，勿改 `CTRL:STATus?` 字段序），或在 §4 明文接受"无 reason/计数/快照"并给复验判据。

## 8. 检查器结果

```text
python tools/docs_check/docs_check.py --strict-names
OK   docs check passed: files=242 warnings=0            (exit 0)
python tools/doc_regression_check.py
OK   freshness: top doc within window
WARN docs\check\DOCS_REGISTRY.md: row id '...' does not match ROW_RE ×10（既有 T2-*/REFTMEM-LEASE-01/IDLE-01/TDMA-* 行，未计入）
OK   registry: 49 contracts, ids unique
OK   orphan: 10 top constraints all covered
OK   progress: 19 progress logs checked
OK   doc_regression passed                              (exit 0)
```
