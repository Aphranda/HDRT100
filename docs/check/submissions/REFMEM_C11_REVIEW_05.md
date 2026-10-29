# REFMEM 统一控制面入口契约终审（C11 第五轮）

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_C11_REVIEW_05.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`, `docs/check/submissions/REFMEM_C11_REVIEW_04.md`
Last updated: 2026-10-29

## 1. 范围

- 对象：HEAD `07969458` 对 C11-04 三项整改的落地（`REFMEM_C11_REVIEW_04.md:60-63`）：ENTRY-01 clause/check（`DOCS_REGISTRY.md:31`）、入口 12 语义（`REFMEM_DOMAIN_ARCHITECTURE.md:1702,1713`）、快照 P3/单口证据（`REFMEM_UNIFIED_CONTROL_REVIEW.md:49,53`）。
- 方法/未验证：独立只读，未重跑 HIL/P3、未编译、未碰硬件、未改登记状态；两个新 host runner 仅确认文件存在（未复跑，`tools/tests/run_host_unit_tests.ps1:20-59` 未收录）；四域/`FUNC_*` 单口同批捕获无证据（契约已自认）。

## 2. 结论

**`PASS_WITH_CHANGES`**：三项整改 **2 闭环 / 1 部分**——clause 收窄与快照回正已闭环；12 语义在契约面已统一，但 C11-04 明确要求删除的登记行过期句仍留（`DOCS_REGISTRY.md:29`），且 `REFMEM_DOMAIN_TODO.md:870` 仍是旧「fail-closed（reason 12）」口径。`REFMEM-ENTRY-01` **暂不能** `pending → active`；`REFMEM-CTRL-01` 保持 `pending`。

## 3. 三项整改核对

| # | C11-04 要求 | 现状（锚点） | 判定 |
|---|---|---|---|
| 1 | clause 第 3 条只保留已机器捕获的 T2 配置/MOUNT/聚合，四域/`FUNC_*` 降为产品约束（未验证） | `:31` 已改为「单串口下 T2 配置/MOUNT/聚合已机器捕获…四域配置与 `FUNC_*` 的单口同批捕获未做，属产品约束（未验证）」；架构 `:1715`、快照 `:76` 同口径；check 只列 30/31、无越界断言；host 缺口已注「未入 `run_host_unit_tests.ps1` 总门禁」并列入快照 `:66` | 闭环 |
| 2 | 12 语义统一为入口 `NOT_CONTROL_OWNER`（不落 reason）/目标侧 reason 12，并删 `:29` 的过期句 | 架构 `:1702,1713`、登记 `:29` check、`:31` clause、快照 `:74` 均一致；代码 `scpi_refmem_snapshot_commands.c:949`（文本错误、不落 reason）、`refmem_command.h:112`（`PERMISSION_DENIED=12`）成立；但 `:29` 仍留「原'接入点无关'字面仍不满足」「条款拆分…待下一次登记」，后者与 `:31` 已登记自相矛盾（C11-04 要求删除，未删） | **部分** |
| 3 | 快照当前 P3 回正为 build `20261029031043`/1613/`ae155f7d…`，单口证据指向 `single_port_capture.json` | 快照 `:49` 已写 build/1613/`ae155f7d…`/QUICK/`strict=false`；`:53` 单口行指向 `out/n8/single_port_capture.json`（`port=COM5`）；旧值 `20261029012332`/1609 与「四板 MOUNT 即单口证据」已无残留 | 闭环 |

### 3.1 clause 与 check 等价 / 锚点
- 等价：clause 的可判定子集（同目录/同语义、单入口、入口/目标侧 12、单口 30/31）在 check 均有对应项；check 未断言四域/`FUNC_*` 已捕获。差异：check 未复述「四域/`FUNC_*` 未验证」边界，且聚合只注 host（`single_port_capture.json:43-50` 的 `COMMand:STATus?/TARget?` 未列入）——表述窄而非越界，判**等价（有窄化）**。
- 锚点真实：`REFMEM_DOMAIN_ARCHITECTURE.md:1708` 有 `REFMEM-ENTRY-01` 小节；`refmem_control_route.h`、`refmem_command.h` 存在；`:949`/`:112` 行号可复现。
- 残留旧措辞：架构 `:1698`「非协调者远端命令 fail-closed」、`REFMEM_DOMAIN_TODO.md:870`「一律 fail-closed（reason 12）」；`REFMEM_TASK_PROGRESS.md:161,169` 为历史日志（append-only，不改写）。

## 4. 证据抽查

| # | 声称 | 实测值（锚点） | 判定 |
|---|---|---|---|
| 1 | receipt QUICK/非 strict | `acceptance_scope=FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、`acceptance_profile=QUICK_DIAGNOSTIC`、`strict_gates_passed=false`、`PASS_WITH_WARNINGS`、`build_id=20261029031043`、`source_file_count=1613`、`source_tree_sha256=ae155f7d…`（`p3_acceptance_receipt.json:3,4,8,9,12,230,244`） | 成立 |
| 2 | 单口 port | 9 条记录全为 `"port":"COM5"`（`single_port_capture.json:5,11,17,23,29,35,41,47,53`）；build 回读 `20261029031043`（`:8`）；30/31 均 state=4/ack=14（`:26,38`） | 成立 |
| 3 | host 清单 | `run_host_unit_tests.ps1:20-59` 共 38 条；`run_refmem_command_aggregate_tests.ps1`、`run_refmem_position_reset_policy_tests.ps1` 存在但不在清单（check/snapshot 已注） | 成立（未复跑） |
| 4 | FULL 卡点 | `wf_fullbench.log:50-51` step 21 FAIL、`:66` `no SCK offset candidate row`、`:67` `credential: not eligible` | 成立 |

## 5. 状态建议

- **ENTRY-01 转 `active`：暂不可以。** 必改（2 项，均文本，不涉语义）：
  1. `DOCS_REGISTRY.md:29`：删「条款拆分…待下一次登记」，把「原'接入点无关'字面仍不满足」改为「原条款已由 `REFMEM-ENTRY-01` 重新定义（产品级入口收敛）」；同文件 `:7` `Last updated: 2026-10-26` → `2026-10-29`（硬约束 5；该文件 2026-10-29 已多次修改）。
  2. `REFMEM_DOMAIN_TODO.md:870` 的「一律 fail-closed（reason 12）」改为「入口侧 `NOT_CONTROL_OWNER`（不落 reason）/目标侧 reason 12」；`REFMEM_DOMAIN_ARCHITECTURE.md:1698` 同步改写。历史日志 `REFMEM_TASK_PROGRESS.md:161,169` 不改写。
- 完成后状态行：`REFMEM-ENTRY-01` 的 `registered` 保持 `2026-10-29`，`status` 改 `active`，check 列把「C11-04…待 C11-05 复审」改为「C11-05 = PASS_WITH_CHANGES（整改闭环）」。
- **`REFMEM-CTRL-01`：保持 `pending`。** C3 租约/C6 fence 未实现（`DOCS_REGISTRY.md:29`；C11-01/03/04）；本次不改其状态。

## 6. 检查器结果

```text
python tools/docs_check/docs_check.py --strict-names
OK   docs check passed: files=241 warnings=0            （exit 0）
python tools/doc_regression_check.py
OK   freshness: top doc within window
WARN docs\check\DOCS_REGISTRY.md: row id 'T2-DEPLOY-003' does not match ROW_RE（×10，既有 T2-*/REFTMEM-*/TDMA-* 行）
OK   registry: 49 contracts, ids unique
OK   orphan: 10 top constraints all covered
OK   progress: 19 progress logs checked
OK   doc_regression passed                              （exit 0）
```
