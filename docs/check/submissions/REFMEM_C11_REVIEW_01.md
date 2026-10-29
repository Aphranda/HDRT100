# RefMem 统一控制面 `REFMEM-CTRL-01` 独立交叉复核（C11）

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_C11_REVIEW_01.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`, `docs/check/submissions/REFMEM_HAOFV_REVIEW_01.md`
Last updated: 2026-10-29

## 1. 审查范围

- 对象：`REFMEM-CTRL-01` 条款文本（`docs/check/DOCS_REGISTRY.md:29`）、域架构统一控制面各节、2026-10-29 声称的 N8.3/N8.4/N8.5 增量及四板证据。
- 方法：独立只读复核，逐条核对代码锚点与证据文件的实际取值；复核人未参与 2026-10-29 的实现与提交，不代作者背书。
- 未覆盖：本次不重跑 HIL/串口/P3，不做编译验证；`REFMEM-TRANSPORT-01`/`REFTMEM-LEASE-01` 等相邻契约只作交叉引用。
- 判定口径：**满足**＝代码与证据同时成立；**部分**＝机制存在但条款要素不全；**未满足**＝条款明文不成立。

## 2. 结论

**`PASS_WITH_CHANGES`**：N8 增量的机制与四板读数可确认，但 `REFMEM-CTRL-01` 不能转 `active`，且“N8.5 已闭环”的文档表述不被本复核接受为完成。

- 条款 8 项：5 满足 / 2 部分（协调者租约、fence 聚合）/ 1 未满足（接入点无关）。
- 代码事实：单命令槽、单 ACK 回程、显式掩码、目标本地执行、只读本地聚合均成立；无第二总线/payload class/命令槽。
- 主要问题集中在证据与同步：N8.5 四板证据不可复跑、无 host 用例、P3 为 QUICK/strict=false；多处 canonical/快照对 N8.5 状态自相矛盾。

## 3. 逐条款判定表

| # | 子条款 | 判定 | 锚点 / 说明 |
|---|---|---|---|
| C1 | 控制面经 RefMem 命令槽/快照面挂接，不另建控制总线 | 满足 | 单一 `refmem_command_slot_t`（`components/distributed_refmem/inc/refmem_command.h:173-201`），实例唯一（`components/distributed_refmem/src/distributed_refmem.c:116`）；跨节点走既有 `TDMA_SECOND_CLASS_TYPE_REFMEM_CMD`（`src/refmem_cmd_load.c:146`），无新 payload class。 |
| C2 | 接入点无关（任一节点维护接口同语义） | 未满足 | 非协调者远端命令 fail-closed：`src/refmem_control_route.c:45-61`、`middleware/scpi_port/src/scpi_refmem_snapshot_commands.c:939-951`；架构自述未满足（`docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md:1700-1706`）。 |
| C3 | 单一控制协调者租约 | 部分 | 单一发号由派生 `control_owner_slot` 保证（`scpi_refmem_snapshot_commands.c:941-951`），但无租约/epoch/boot_id/迁移/双主负测；架构仍记“协调者租约…未实现”（`REFMEM_DOMAIN_ARCHITECTURE.md:1595`）。 |
| C4 | 显式 `target_mask`/`required_mask` | 满足 | 字段在信封（`refmem_command.h:130-131`），SCPI 入口强制非零且 required⊆target（`scpi_refmem_snapshot_commands.c:912-919`；`src/refmem_control_submit.c:39`）；四板实测 `target=required=ack=14`（`out/n8/n85_mount_four_board.json:3`）。 |
| C5 | 目标本地 owner 执行 | 满足 | 目标在本板 RefMem 服务上下文读 node 记录并分发（`src/distributed_refmem_command.c:185-242`），handler 在应用层注册（`application/src/app_refmem_domain_config.c:730`）；三目标各自置 ack 位。 |
| C6 | ACK/NACK/fence 聚合 | 部分 | ack/nack 聚合回源板 node 记录（`src/distributed_refmem_cmd_reply.c:145-200`；NACK 镜像 `distributed_refmem_command.c:259-269`）；**fence 未实现**（`REFMEM_DOMAIN_ARCHITECTURE.md:1597`），聚合视图仅本地、未广播（同文件 `:1596`）。 |
| C7 | 本地快照读 | 满足 | `COMMand:STATus?` 由本地 node 记录派生（`src/distributed_refmem_status.c:1391-1427`；`scpi_refmem_snapshot_commands.c:833-852`），`TARget?` 只读本地记录（`:854-874`），无跨板查询。 |
| C8 | 不提供远程裸写、跨板阻塞查询或绕过门禁的强制位 | 满足 | 无远程写内存/IO API；执行前过授权与目标本地门禁（`distributed_refmem_command.c:89-107`）；读路径只读快照（C7 锚点）；边界表述见 `REFMEM_DOMAIN_ARCHITECTURE.md:1563-1567`。 |

## 4. HAOFV 六门与 C1/C2

| 门 | 判定 | 依据 / 缺口 |
|---|---|---|
| ① 无新增总线/payload class/第二命令槽/第二 ACK 事实 | ✅ | 四个提交只动 refmem/应用/SCPI 源码、host 用例、`t2_workflow.py` 与验收 receipt（`git show --stat 196cd464 f07fb2b6 4dc41dfe 4f7a94f0`），无 wire/枚举/布局文件；命令槽实例唯一（`distributed_refmem.c:116`）。 |
| ② 不占 core1 硬实时窗口、不拉长一等席周期 | ⚠️ | N8.5 全在 Core0 refmem 服务（`distributed_refmem_plane.c:684-701`、`app_refmem_domain_config.c:203-223`），未改 core1；但无 phase/seat/静态预算重算证据，TDMA 连续性只有计数读数（`n85_mount_four_board.json:5`），EXE-SEAT-01 相位预算仍未签。 |
| ③ 单写者/owner 边界、RUN 只读 active、层间方向 | ✅ | 状态实例 1（`distributed_refmem.c:116`）；dispatch 与位置 TX 同属唯一 refmem 服务（`distributed_refmem_link.c:912`）；RUN 中 T2 配置被 `state.active` 拒绝（`app_refmem_domain_config.c:139-142`）。 |
| ④ fail-closed + reason/计数/快照 | ⚠️ | 复位失败→reason 14（`app_refmem_domain_config.c:214-216`），NACK→记录+镜像（`distributed_refmem_command.c:259-269`）；但复位失败无专用计数器/快照，`REFTMEM_POS_RESET_TIMEOUT` 不可达（`distributed_refmem_plane.c:684-701`），架构要求的 9 个 reason 未入枚举（`refmem_command.h:99-115` vs `REFMEM_DOMAIN_ARCHITECTURE.md:1582`）。 |
| ⑤ 布局/版本/契约同步 | ⚠️ | 无 layout/CRC/错误码变化，P3 指纹前缀 `af6fbe0f…`（`config/hardware_acceptance/p3_acceptance_receipt.json:8`）与提交声明一致（未重算 staged 指纹）；但登记表 check 文本与 canonical 未同步（见 §7-1、§7-3）。 |
| ⑥ host + 四板证据可复跑 | ❌ | N8.6 有结构化摘要（`out/n8/four_board_hil/n86_summary.json:23-43`，ack=14）；N8.5 只有 7 行 transcript（`out/n8/n85_mount_four_board.json`，无 build/端口/run 目录）；`tests/` 内无 `reset_position_running`/`command_aggregate` 用例；P3 为 QUICK、`strict_gates_passed=false`，TDMA 闭环诊断 step 26 失败（receipt `:230,:244-253`），scope 不覆盖 T2 在线 MOUNT。 |
| **C1 Flash/XIP** | ✅ | 本次改动无 erase/program，未触碰 core1 park 与 Flash 事务；复位函数只清 SRAM 发布状态（`distributed_refmem_plane.c:663-701`），临界区无 XIP/USB/SD/LCD 访问。 |
| **C2 时间事实** | ✅ | 本轮 diff 无 raw tick 回写、无新增 tick 差值；`CTRL:STATus?`/聚合只用序号与状态（`scpi_refmem_snapshot_commands.c:967-996`）。遗留观察（非本轮引入）：`refmem_command_mark_timeout` 以 uint32 ms 与 `timeout_us` 比较（`refmem_command.c:280-281`），聚合取最大值用无符号 `>=`（`distributed_refmem_status.c:1417`），序号回绕时不安全，需另立条目。 |

## 5. 证据抽查

| # | 声称 | 实际看到 | 判定 |
|---|---|---|---|
| 1 | 四板 MOUNT `ack=14` | `n85_mount_four_board.json:3` = `124139,3925911955,4,124138,14,14,14,0,0,0,0,3,124138` → state=4/ACKED、ack=14、nack=0；同文件 `:5` 环 `enabled=1,up=1,down=1`。该文件无 build/端口字段（本地对照 `n85_mount_local_ok.json:3` 才有 build `20261029012332`）。 | 成立（限读数）；可复跑性不足 |
| 2 | 命令目录 36 条 / 6 个 function | `config/refmem/command_catalog.yaml` 538 行、36 条（id 1–6、20–34、35–49）；`config/refmem/function_catalog.yaml` 61 行、6 个 function（id 1–6）。 | 成立 |
| 3 | 失败路径 NACK 镜像 | `distributed_refmem_command.c:259-269` 写 NACKED+nack 掩码+reason；host 断言 `tests/unit/test_refmem_command_authority.c:280-289`；单板读数 `out/n8/n57_smoke.json:5` = `8,0,0,0,0,1,0,5,13,0,21894`（nacked=1/reason=13）。 | 成立 |
| 4 | N5.7 合法 41 后 `acked=1` | 仅见 `docs/refmem/REFMEM_TASK_PROGRESS.md:75` 叙述；`out/n8/n57_smoke.json` 只有非法 41 一例，`out/n8` 内未找到 `8,0,0,0,1,0,0,4,0,0,21893`。 | **未验证** |
| 5 | P3 凭证 | build `20261029012332`、`source_tree_sha256` 前缀 `af6fbe0f`、1609 files、`passed=true`、`PASS_WITH_WARNINGS`、`strict_gates_passed=false`（receipt `:8-12,:230,:244`）。 | 成立（QUICK/非严格） |
| 6 | 单串口下发 | `n85_mount_*.json` 为 `scpi_query` transcript，不记录 COM 端口；仅任务记录称“全部在 COM5”。 | **未验证**（证据层） |

## 6. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复核人 C11（非本仓库 2026-10-29 改动作者） |
| 审核方式 | agent 交叉（代码↔登记表↔域文档↔证据四向对照，只读） |
| 审核结论 | `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-29 |

- `REFMEM-CTRL-01` **不能**从 `pending` 转 `active`：C2 明文未满足，C3/C6 部分未闭合，N8.5 证据等级不足。
- “接入点无关”在单串口产品下的正确表述：**命令目录、语义与观测在每块板相同；跨节点发号唯一归 `control_owner_slot`（最低 claimed 插槽），非协调者远端命令 fail-closed，不中继、不排队；本地维护不受影响**。即把“任一节点可发起远端命令”降为“任一节点同目录、同语义、可本地控制；远端发号单入口”。现文 CP1 前半句（同接口/同目录）已成立，后半句（接入点只改变 origin）不成立；条款需拆成两条可判定子句后重签，不能直接翻状态。
- 必须阻断的状态变更：`pending`→`active` 本轮阻断；本轮也不同意把 N8.5 记作契约层“完成”。

## 7. 必须修改项

1. **条款修订（C2/C3/C6）**：CP1 拆为“同目录/同语义（满足）”与“远端发号单入口（产品级收敛）”；`fence 聚合`、协调者租约降为 v1 显式非目标或补实现与负测；同步 `DOCS_REGISTRY.md:29` 的 contract 与 check 文本。
2. **N8.5 证据补齐**：以 build/端口/run 目录重出四板 MOUNT 结构化摘要（含 MOUNT 前后 ring 计数与 T2 状态），替换裸 transcript；补 `reset_position_running`（UNSAFE/OK、单写者）与 `command_aggregate`（合法/非法）host 用例。
3. **文档同步**：`REFMEM_UNIFIED_CONTROL_REVIEW.md:25,36`（N8.5 未实现/仍边界）、`:49`（P3 build `20261028154542` 过期）；`REFMEM_DOMAIN_ARCHITECTURE.md:1596`（聚合视图“未实现”表述）、`:1723,1751`（T2_MOUNT 仍 `NOT_ONLINE_SAFE`）、`:1744`（function 步序与 `function_catalog.yaml` 不符）；`distributed_refmem.h:359-368`、`distributed_refmem_plane.c:660-662`、`app_refmem_domain_config.c:209-211` 的 park/resume/TIMEOUT 注释与代码不符。
4. **错误码同步**：按 `REFMEM_DOMAIN_ARCHITECTURE.md:1582` 将 `UNSUPPORTED_COMMAND/CATALOG_MISMATCH/CONTROL_BUSY/…` 落入 reason 枚举与错误文本，或删除该承诺；当前 SCPI 错误 `NOT_CONTROL_OWNER/UNSUPPORTED_COMMAND/REJECTED` 不在原因表内（`scpi_refmem_snapshot_commands.c:900,908,949,959`）。
5. **证据等级声明**：在契约行与快照中显式写“P3 为 QUICK/DPLL scope、strict=false，不覆盖 T2 在线 MOUNT 与位置复位路径；N8.5 需 full 验收或等价专项后才可称闭环”。

## 8. 检查器结果

```text
python tools/docs_check/docs_check.py --strict-names
OK   docs check passed: files=237 warnings=0

python tools/doc_regression_check.py
OK   freshness: top doc within window
WARN docs\check\DOCS_REGISTRY.md: row id ... does not match ROW_RE (×10，行 id 含 T2-*/REFTMEM-*/TDMA-SEAT-SHARE-01 等)
OK   registry: 48 contracts, ids unique
OK   orphan: 10 top constraints all covered
OK   progress: 19 progress logs checked
OK   doc_regression passed
```

两项均通过；10 条 `WARN` 为既有登记行 id 形态问题（`REFMEM-CTRL-01` 自身可被校验），不构成本轮阻断，但登记表“48 条中 10 条未计数/未校验”应另立治理条目。
