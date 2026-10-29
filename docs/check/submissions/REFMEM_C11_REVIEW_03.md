# `REFMEM-ENTRY-01` 整改复审（C11 第三轮）

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_C11_REVIEW_03.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/check/submissions/REFMEM_C11_REVIEW_01.md`, `docs/check/submissions/REFMEM_C11_REVIEW_02.md`
Last updated: 2026-10-29

## 1. 范围

- 对象：C11-02 五项必改（`REFMEM_C11_REVIEW_02.md:62-68`）在 `3085963c`（文档）与 `09ab9791`（代码/host）的整改；HEAD = `09ab9791`。
- 对照：`DOCS_REGISTRY.md:29`（CTRL-01）、`:31`（ENTRY-01）；`REFMEM_DOMAIN_ARCHITECTURE.md:1700-1719`、`:1743`、`:1764`。
- 方法：独立只读；未重跑 HIL/P3，未改代码与登记状态。新增 host 用例我另以 `gcc -std=c11 -Wall -Wextra -Werror` 在系统临时目录复跑（不写仓库）。
- 口径：**闭环**＝必改项要求全部落地；**部分**＝要素缺一；**未验证**＝证据不承载该事实。

## 2. 结论

**`PASS_WITH_CHANGES`**：5 项必改 2 项闭环（悬空 id、36/6 引用化）、3 项部分（委派标注、check 补全、单口证据等级）。`REFMEM-ENTRY-01` **不能**转 `active`；`REFMEM-CTRL-01` 保持 `pending`。

## 3. 整改逐条

| # | C11-02 必改项 | 现状 | 判定 | 锚点 |
|---|---|---|---|---|
| 1 | 同步 CTRL-01 行、标委派 | clause 尾部已加"入口子句已委派 `REFMEM-ENTRY-01`（补充，不替代）"，check 同改；但原句"接入点无关（任一节点维护接口同语义）"仍留在 clause 内，而 ENTRY-01 第 1 条把同一语义判"满足" | 部分（双源弱化未消除） | `DOCS_REGISTRY.md:29` vs `REFMEM_DOMAIN_ARCHITECTURE.md:1712` |
| 2 | 补 ENTRY-01 check 文本 | 已列 目录/函数表（指向 catalog）、owner/非 owner/无 owner host、聚合 host、`ack=14` 读数、12 目标侧 host-only 与入口侧 `NOT_CONTROL_OWNER`；未列 clause 第 3 条要求的 四域配置/`FUNC_*` 单口证据 | 部分 | `DOCS_REGISTRY.md:31` vs `REFMEM_DOMAIN_ARCHITECTURE.md:1715` |
| 3 | 修悬空引用 | `:1717` 已改 `REFMEM-ENTRY-01`；全库仅 C11-02 历史文本保留旧 id | 闭环 | `REFMEM_DOMAIN_ARCHITECTURE.md:1717` |
| 4 | 单口证据等级 | registry 与 `:1715` 已降为"产品约束、端口未机器记录／证据未机器化"，check 不再由 `ack=14` 推拓扑；但 `:1719` 仍写"（仅 COM5 一口）"，`REFMEM_TASK_PROGRESS.md:41` 仍称"`ack=14` 即单口拓扑证据" | 部分 | `REFMEM_DOMAIN_ARCHITECTURE.md:1715,1719`、`REFMEM_TASK_PROGRESS.md:41` |
| 5 | 36/6 改为引用 | ENTRY-01 clause 已改为"条目数以 `command_catalog.yaml`/`function_catalog.yaml` 为准" | 闭环（ENTRY-01）；残余 CTRL-01 check 仍手写"catalog 36 行；6 个 function"（`:29`）、架构 `:1764` 手写"6 个 function" | `DOCS_REGISTRY.md:31`、`REFMEM_DOMAIN_ARCHITECTURE.md:1712` |

## 4. clause 与 check 等价性

- 锚点真实：`refmem_control_route.h:50` 声明 `refmem_control_entry_decide`，实现 `refmem_control_route.c:45-61`；registry clause_loc `…:REFMEM-ENTRY-01` 可在 `:1708` 定位。
- 不等价 1：clause 写"非协调者远端命令 fail-closed(12)"（`:31`、架构 `:1713`），实现入口在提交前即以文本错误 `NOT_CONTROL_OWNER` 拒绝且**不落 reason**（`scpi_refmem_snapshot_commands.c:935-951`）；reason 12 只来自目标侧 dispatch 授权（`distributed_refmem_command.c:99`，`refmem_command.h:112`），check 自注"12 为目标侧 host-only"（`:31`）⇒ clause 与 check 仍冲突。
- 不等价 2：clause 第 3 条（单口完成四域配置/`FUNC_*`/MOUNT/聚合）在 check 无对应项，只有 `CTRL:EXECute 31,14,14` 读数。
- 双源：见 §3-1，CTRL-01 仍保留被委派语义的原文，而 ENTRY-01 判其"满足"。

## 5. 证据抽查（5 项）

| # | 声称 | 实际看到 | 判定 |
|---|---|---|---|
| 1 | 新增 host 用例"随全量套件运行" | 用例存在（`tests/unit/test_refmem_command_aggregate.c` 4 组、`test_refmem_position_reset_policy.c` 4 例），我复跑均 exit=0 并打印 passed；但**未被任何门禁执行**：`tools/acceptance/host_acceptance.py:36-39` 固定 3 个 pytest 文件，receipt paths 不含新用例（75 passed 与之无关），`run_host_unit_tests.ps1:20-59` 的 38 条清单不含新 runner，仅 `run_refmem_all_host_tests.ps1:23` 通配会收录 | 存在且可跑；"已运行"**未验证** |
| 2 | 四板 `ack=14` | `out/n8/n85_mount_four_board.json:3` = `124139,3925911955,4,124138,14,14,14,0,0,0,0,3,124138`；按 `CTRL:STATus?` 13 字段序（`scpi_refmem_snapshot_commands.c:982-994`）= state=4/ACKED、command_seq=124138、target=required=ack=14、nack=0；第 12 字段为 `last_reason_slot`，响应**无 `applied` 字段** ⇒ `n85_mount_four_board_annotated.json:17,23` 的"applied=3"非机器字段 | 成立（限读数） |
| 3 | 单串口拓扑 | annotated `:4` 自述 metadata 为人工补注（transcript 不带 build/port），`:36-39` 逐板读数取自 COM4/COM3/COM6；原始 transcript 无 build/port | **未验证** |
| 4 | P3 覆盖整改源码 | receipt `build_id=20261029031043`、`acceptance_scope=FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、`source_file_count=1613`、`source_tree_sha256=ae155f7d…`、`passed=true`、`strict_gates_passed=false`、`PASS_WITH_WARNINGS`、step 26 TDMA 闭环 rc=1；我重算 `working_source_fingerprint()` = `ae155f7d…`/1613，与该 receipt 一致（覆盖 HEAD 源码），但 scope 不含 T2 在线 MOUNT/位置复位 | 成立（QUICK/非严格） |
| 5 | 拆分等价 | 聚合函数体从 `distributed_refmem_status.c` 原样移入 `refmem_command_aggregate.c:9-46`；reset 准入抽为纯函数后被 `distributed_refmem_plane.c:691` 调用；无新增状态实例、无 wire/布局变更 | 成立 |

## 6. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复核人 C11（非 2026-10-29 改动作者） |
| 审核方式 | 只读四向对照（登记表↔架构↔代码↔证据）+ 临时目录 host 复跑 |
| 审核结论 | `PASS_WITH_CHANGES`；ENTRY-01 不得转 active；CTRL-01 保持 pending |
| 审核日期 | 2026-10-29 |

- 即使 C11-02 五项全部闭环，ENTRY-01 转正也不能使 CTRL-01 转正：C3 协调者租约与 C6 fence 聚合仍未闭合（`REFMEM_C11_REVIEW_01.md:30,33`）。
- 未验证：四板单口一次完成四域配置/`FUNC_*` 的机器捕获；四板同 SCPI 语义视图；非协调者硬件负向（`REFMEM_DOMAIN_TODO.md:542` 记结构性不可达）。

## 7. 必改项（≤5）

1. **措辞一致**：ENTRY-01 clause 的"fail-closed(12)"改为"入口侧 SCPI `NOT_CONTROL_OWNER`（不落 reason）；目标侧授权 reason 12 仅 host 可达"，或删 (12)（`DOCS_REGISTRY.md:31`、架构 `:1713`）。
2. **clause/check 等价**：check 增列 clause 第 3 条的单口证据项（四域配置/`FUNC_*`/MOUNT/聚合，注明"产品约束，端口未机器记录"），或把第 3 条移出契约要素（`:31` vs `:1715`）。
3. **残余双源**：CTRL-01 clause 的"接入点无关（任一节点维护接口同语义）"改为纯指针"入口语义见 `REFMEM-ENTRY-01`"，并删/重述 check 的"原…字面仍不满足"（`:29` vs `:1712`）。
4. **证据收口**：架构 `:1719` 的"（仅 COM5 一口）"标注"人工补注、未机器验证"或删除；`REFMEM_TASK_PROGRESS.md:41` 撤回"`ack=14` 即单口拓扑证据"。
5. **host 入闸**：新 runner 加入 `run_host_unit_tests.ps1` 清单或 host_acceptance 用例发现并重出 receipt，否则 check 的 host 一项只能记"存在且可跑"。

## 8. 检查器结果

```text
python tools/docs_check/docs_check.py --strict-names
OK   docs check passed: files=239 warnings=0            （exit 0）

python tools/doc_regression_check.py
OK   freshness: top doc within window
WARN docs\check\DOCS_REGISTRY.md: row id ... does not match ROW_RE (×10，既有 T2-*/REFTMEM-*/TDMA-* 行)
OK   registry: 49 contracts, ids unique
OK   orphan: 10 top constraints all covered
OK   progress: 19 progress logs checked
OK   doc_regression passed                              （exit 0）
```

两项均通过；10 条 `WARN` 为既有登记行 id 形态问题，不构成本轮阻断。
