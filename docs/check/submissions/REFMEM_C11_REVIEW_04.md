# REFMEM 统一控制面 N9 收尾文本复核（C11 第四轮）

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_C11_REVIEW_04.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/check/submissions/REFMEM_C11_REVIEW_03.md`
Last updated: 2026-10-29

## 1. 范围

- 对象：N9 收尾文本（HEAD `ecf9cddd`）——快照 §1/§2/§4/§7（`REFMEM_UNIFIED_CONTROL_REVIEW.md`）、入口与实现状态（`REFMEM_DOMAIN_ARCHITECTURE.md:1700-1719,1764`）、登记行（`DOCS_REGISTRY.md:29,31`）、进度 `-013/-014/-015`（`REFMEM_TASK_PROGRESS.md:24-59`）、TODO N9（`REFMEM_DOMAIN_TODO.md:897-917`）。
- 方法：独立只读；未重跑 HIL/P3、未编译、未碰硬件；证据以现存 `out/n8/**`、receipt、catalog 为准。
- 未覆盖/未验证：TRN-03 re-arm 预算本身；四域配置与 `FUNC_*` 的单口同批捕获（无端口机器记录）；四板同 SCPI 视图只由同一 build 推断。
- 口径：**闭环**＝指定要求全部落地；**部分**＝要素缺一；**未验证**＝证据不承载该事实。

## 2. 结论

**`PASS_WITH_CHANGES`**：`QUICK+HIL 不冒充 full` 的书面定级成立且三处一致；C11-03 五项 **2 闭环 / 2 部分 / 1 未闭环**。`REFMEM-ENTRY-01` **不能**转 `active`；`REFMEM-CTRL-01` 保持 `pending`。

## 3. 证据等级判定

- 一致：快照 `:19`、`:71`，架构 `:1764`，登记 `:29`/`:31` 均写"QUICK/DPLL、`strict_gates_passed=false`、不作 full、`--full-bench` 卡 TRN-03 另立台架任务"；全库未见"full 通过/strict passed"表述（`out/n8/wf_fullbench.log` step 21 为 `no SCK offset candidate row...`）。
- 不一致 1（过期值）：快照 §3 `:48`"当前 P3 凭证"仍写 build `20261029012332`/1609 files；实际 receipt 为 `20261029031043`/1613 files/`ae155f7d…`（receipt `:8,9,12`）。定级本身不冒充 full，但证据索引指向旧 build。
- 不一致 2（证据越界）：快照 §5 `:83` 仍写"四板 `ack=14` 的 MOUNT 即单口证据"；端口事实只来自 `single_port_capture.json` 的 `port` 字段，`n85_mount_four_board.json:1-7` 无端口/build。
- 不一致 3（限定缺失）：快照 §7 `:93`/`:102` 的 ✅ 表未重复等级限定，单看该节易读成验收通过；§1/§4 已限定，N9.4 重写时应带上。判定：**不冒充满/严格验收**成立；**证据索引自洽**不成立（见 §7-3）。

## 4. C11-03 五项闭环

| # | 必改项 | 现状 | 判定 |
|---|---|---|---|
| 1 | 12 语义一致 | ENTRY-01 clause 与架构 `:1713` 已改"入口 `NOT_CONTROL_OWNER`（不落 reason）/目标侧 12"；架构 `:1702` 与 `DOCS_REGISTRY.md:29` check 仍写入口"fail-closed（12）" | 部分 |
| 2 | clause/check 等价 | check `:31` 增"单口 `30/31` `ack=14` 机器捕获"，并注 12 为目标侧 host-only；clause 第 3 条仍列四域配置/`FUNC_*` 单口，架构 `:1715` 自认"未做"，check 无对应项 | 部分 |
| 3 | 残余双源 | CTRL-01 clause 已改纯指针"接入点语义由 `REFMEM-ENTRY-01` 定义…本条不再单独判定"（`:29`），双源消除；同格 check 仍留"原'接入点无关'字面仍不满足""条款拆分…待下一次登记"，与 `:31` 已登记冲突 | 闭环（clause）；check 过期句残 |
| 4 | 证据收口 | 架构 `:1719` 已删"仅 COM5 一口"并指向 `single_port_capture.json`；进度 `:58` 已改机器捕获并注"四域/FUNC 单口同批捕获未做" | 闭环（指定锚点） |
| 5 | host 入闸 | 两个新 runner 存在（`tools/tests/run_refmem_command_aggregate_tests.ps1`、`run_refmem_position_reset_policy_tests.ps1`），但不在 `run_host_unit_tests.ps1:20-59` 的 38 条清单；host receipt paths 仅 `tests/python/test_refmem_host_units.py`（receipt `:13-14`）；check 未注"未入总门禁" | 未闭环 |

## 5. 证据抽查（7 项，均为值）

| # | 声称 | 实际看到 | 判定 |
|---|---|---|---|
| 1 | receipt QUICK/非 strict | `acceptance_scope=FOUR_NODE_DPLL_QUICK_DIAGNOSTIC`、`acceptance_profile=QUICK_DIAGNOSTIC`、`build_id=20261029031043`、`source_file_count=1613`、`sha256=ae155f7d…`、`strict_gates_passed=false`、`PASS_WITH_WARNINGS`（receipt `:3,4,8,9,12,230,244`） | 成立 |
| 2 | 单口 port 字段 | `single_port_capture.json` 每条 `"port":"COM5"`（`:5,11,17,23,29,35,41,47,53`）；build 回读 `"20261029031043"`（`:8`） | 成立 |
| 3 | 单口 `30/31` ack | cmd 30 后 `CTRL:STATus?`=`123223,23851438,4,123221,14,14,14,…`（state=4、target=required=ack=14）；cmd 31 后=`123225,3485351640,4,123222,14,14,14,…`（`:26,38`） | 成立 |
| 4 | 四板 HIL | `n85_mount_four_board.json:3`=`124139,3925911955,4,124138,14,14,14,…`（ACKED/ack=14，**无 port/build**）；`n86_summary.json:4` build `20261028180729`，3 个 function `COMPLETED`/ack=14（`:21-44`） | 成立（限读数） |
| 5 | FULL 卡点 | `wf_fullbench.log` step 21 `FAIL`：`ValueError: no SCK offset candidate row can satisfy the flight re-arm budget`；同文件 `credential: not eligible` | 成立 |
| 6 | 12 语义 | 入口 `NOT_CONTROL_OWNER`（`scpi_refmem_snapshot_commands.c:949`）；目标侧 `PERMISSION_DENIED=12`（`refmem_command.h:112`、`distributed_refmem_command.c:99`） | 成立（静态/host） |
| 7 | 四域/FUNC 单口 | 无 port 字段机器捕获；`single_port_capture.json` 只有 30/31 | **未验证** |

## 6. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复核人 C11-04（非 2026-10-29 改动作者） |
| 审核方式 | 只读四向对照（登记表↔快照↔架构↔证据） |
| 审核结论 | `PASS_WITH_CHANGES`；ENTRY-01 不得转 active；CTRL-01 保持 pending |
| 审核日期 | 2026-10-29 |

## 7. 状态建议

- **ENTRY-01 转 `active`：不可以。** 最小必改（≤3）：
  1. **clause/check/host 收口**：第 3 条只保留已机器捕获的 T2 配置/MOUNT/聚合，四域/`FUNC_*` 单口降为"产品约束、未机器捕获"；check 同步，并注明 host 用例"存在且可跑、未入 `run_host_unit_tests` 总门禁"（或补入清单并重出 receipt）（`DOCS_REGISTRY.md:31`、`REFMEM_DOMAIN_ARCHITECTURE.md:1715`）。
  2. **12 语义收尾**：`REFMEM_DOMAIN_ARCHITECTURE.md:1702` 与 `DOCS_REGISTRY.md:29` check 的入口"fail-closed（12）/fail-closed 12"改为"入口 `NOT_CONTROL_OWNER`（不落 reason）；目标侧 reason 12"，并删 `:29` 的"原字面仍不满足""待下一次登记""C11 待第三方复核"过期句。
  3. **收口快照回正（不阻断状态行，收口前必改）**：`REFMEM_UNIFIED_CONTROL_REVIEW.md:48` 改为 build `20261029031043`/1613 files/`ae155f7d…`；`:83` 改为指向 `out/n8/single_port_capture.json`（port=COM5）。
- **CTRL-01：保持 `pending`。** C3 协调者租约/epoch/迁移、C6 fence 聚合仍未实现（`REFMEM_C11_REVIEW_01.md:30,33`；架构 `:1702,1706`）；`DOCS_REGISTRY.md:29` 仍指"C11 待第三方复核"，与 C11-01/02/03 记录冲突。

## 8. 检查器结果

```text
python tools/docs_check/docs_check.py --strict-names
OK   docs check passed: files=240 warnings=0            （exit 0）
python tools/doc_regression_check.py
OK   freshness: top doc within window
WARN docs\check\DOCS_REGISTRY.md: row id ... does not match ROW_RE (×10，既有 T2-*/REFTMEM-*/TDMA-* 行)
OK   registry: 49 contracts, ids unique
OK   orphan: 10 top constraints all covered
OK   progress: 19 progress logs checked
OK   doc_regression passed                              （exit 0）
```

两项均通过（exit 0）；10 条 `WARN` 为既有登记行 id 形态问题，不构成本轮阻断。
