# `REFMEM-ENTRY-01` 拆分复核（C11 第二轮）

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_C11_REVIEW_02.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/check/submissions/REFMEM_C11_REVIEW_01.md`
Last updated: 2026-10-29

## 1. 范围

- 对象：`REFMEM-ENTRY-01`（`docs/check/DOCS_REGISTRY.md:31`）与其架构小节（`docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md:1708-1719`），以及未改动的 `REFMEM-CTRL-01`（`DOCS_REGISTRY.md:29`）。
- 对照：`REFMEM_DOMAIN_ARCHITECTURE.md:1700-1706`（入口无关边界）、`:1743`（T2 在线启动）；`REFMEM_UNIFIED_CONTROL_REVIEW.md:15-19,80-82,92-104`（§1/§5/§7）；`REFMEM_TASK_PROGRESS.md:24-52`（`-010`~`-012`）。
- 方法：独立只读复核；未重跑 HIL/P3，未做编译；证据以 `out/n8/**` 现存文件为准。
- 口径：**满足**＝条款+锚点+证据同时成立；**部分**＝机制存在但条款要素/证据不全；**未满足**＝明文不成立；**未验证**＝仅作者自述或证据不承载该事实。

## 2. 结论

**`PASS_WITH_CHANGES`**：拆分方向忠实，但只做了“加一条”，没有把 `REFMEM-CTRL-01` 的旧入口子句移交出去，形成同语义双源；`REFMEM-ENTRY-01` **不能**转 `active`。

## 3. 拆分忠实性

- 忠实之处：原“接入点无关（任一节点维护接口同语义）”被拆为“同目录/同语义/可本地控制”（`:1712`）与“跨节点发号单入口”（`:1713`），与 `REFMEM_C11_REVIEW_01.md:71` 建议的表述一致；未把产品收敛写成能力缺失，并显式保留“不中继/不排队/不建 relay”（`:1702`），未新增 payload class/wire/第二命令槽。
- 缺口 1（双源，阻断）：`DOCS_REGISTRY.md:29` 的 CTRL-01 clause 仍为“接入点无关（任一节点维护接口同语义）”，check 仍写“**入口无关条款未满足**”。旧子句未被删除、改写或标 `superseded`，新行也未声明承接关系 ⇒ 同一语义存在两条并行断言；若 ENTRY-01 转 active，登记表将同时存在“已满足”与“未满足”。
- 缺口 2（重复计数）：ENTRY-01 第 1 条“本地控制与维护可用”与 `REFMEM-LOCAL-01` clause（`DOCS_REGISTRY.md:34`）重复，应写成指针而非第二份承诺。
- 缺口 3（范围外溢）：第 3 条把“单串口拓扑”这一产品约束写成 clause，但 check 只列 MOUNT 一项（见 §4-E1、§5-2）。

## 4. 逐条判定

| # | 检查项 | 判定 | 锚点 / 说明 |
|---|---|---|---|
| E1 | clause 与 check 文本一致 | 部分 | clause 四项（目录/function、同 SCPI 语义与只读聚合、单口完成四域配置+FUNC_*+MOUNT+聚合、非目标），check 仅覆盖“目录/函数表比对 + owner 三类 host + MOUNT ack=14 + 非协调者 12”，未覆盖四域配置/FUNC_*/聚合的单口证据（`DOCS_REGISTRY.md:29` vs `REFMEM_DOMAIN_ARCHITECTURE.md:1715`）。 |
| E2 | 同命令目录 36 / function 6 | 满足（静态） | `config/refmem/command_catalog.yaml` 36 个 id、`config/refmem/function_catalog.yaml` 6 个 id / 6 个 name；四板同 build `20261029012332`（`out/n8/n85_mount_local_ok.json:2-3`）。板级逐板读回**未验证**。 |
| E3 | 同 SCPI 语义与只读聚合 | 部分 | 聚合只读本地 node 记录、无自有状态（`components/distributed_refmem/src/refmem_command_aggregate.c:9-45`；handler `middleware/scpi_port/src/scpi_refmem_snapshot_commands.c:833-852`），host 用例 `tests/unit/test_refmem_command_aggregate.c`；但只有 COM5 单板读回（`out/n8/n57_smoke.json`），四板同视图**未验证**。 |
| E4 | 跨节点发号单入口 | 满足 | 发号者＝最低 claimed 插槽（`refmem_slot_claim.c:302-319`）；入口判定（`refmem_control_route.c:45-61`，锚点 `components/distributed_refmem/inc/refmem_control_route.h:44-53`）；SCPI 侧在提交前拒绝（`scpi_refmem_snapshot_commands.c:935-952`）。 |
| E5 | 非协调者 fail-closed(12) | 部分 | 入口侧返回 SCPI `NOT_CONTROL_OWNER` 且**不写 reason 12**（`scpi_refmem_snapshot_commands.c:949`）；12 来自目标侧 dispatch 授权（`distributed_refmem_command.c:88-101`），仅 host 可达（`tests/unit/test_refmem_command_authority.c:275-289`）；非协调者硬件负向结构性不可达（`REFMEM_DOMAIN_TODO.md:542`）。 |
| E6 | 不中继/不排队/不建 relay | 满足 | 入口拒绝后直接返回，无入队调用（`scpi_refmem_snapshot_commands.c:944-951`）；分支内无 relay/queue 状态。 |
| E7 | 锚点真实性 | 部分 | `refmem_control_route.h` 存在且含 `refmem_control_entry_decide`；`REFMEM-ENTRY-01` 标题（`:1708`）可定位；但 `:1717` 引用了不存在的 `REFMEM-CTRL-ENTRY-01`（悬空 id，须修）。 |
| E8 | 硬数字处理 | 部分 | clause 手写“36 条/6 个”，未引用 `refmem_command_catalog`/`refmem_function_catalog` 符号，也未标“快照，非事实源”（AGENTS §2.2）。 |

## 5. 证据抽查（5 项，前 4 项为值）

| # | 声称 | 实际看到 | 判定 |
|---|---|---|---|
| 1 | 四板 `CTRL:EXECute 31,14,14,1,0,0` → `ack=14` | `out/n8/n85_mount_four_board.json:3` = `124139,3925911955,4,124138,14,14,14,0,0,0,0,3,124138` → state=4 ACKED、target=required=ack=14、nack=0、applied=3。 | 成立（限读数） |
| 2 | 命令 id=31 且端口 COM5 | 全 `out/n8/**` 检索 `CTRL:EXECute 31` 只命中 `n85_mount_four_board_annotated.json:20`；该文件 `:6` 的 `"port":"COM5"` 由其 `:4` 自述“metadata added because scpi_query transcripts do not carry build/port fields”。原始 transcript 无命令 id、无端口；`CTRL:STATus?` 回读不含 command_type。 | **未验证** |
| 3 | 非协调者 12 负测 | `tests/unit/test_refmem_command_authority.c:275-289` 断言 nack=1、reason=`PERMISSION_DENIED`(12)、never stages、node 记录同步。 | 成立（host，目标侧） |
| 4 | 目录 36 / function 6 | `command_catalog.yaml` 36 个 `- id:`；`function_catalog.yaml` 6 个 `- id:`、6 个 name（`FUNC_*`）。 | 成立 |
| 5 | 单串口下发 | 机器证据只有 `n85_mount_four_board.json`（无端口）；`out/n8/four_board_hil/trn03_n85j/summary.json` 内含 COM5/COM4/COM3（台架读回用了多口）；单口属产品拓扑约束。 | **未验证**（拓扑层） |

## 6. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复核人 C11（非 2026-10-29 改动作者） |
| 审核方式 | agent 交叉（登记表↔架构↔代码↔证据四向对照，只读） |
| 审核结论 | `PASS_WITH_CHANGES`；`REFMEM-ENTRY-01` 不得转 `active` |
| 审核日期 | 2026-10-29 |

- 与 `REFMEM-CTRL-01` 的关系：现状是**补充**（新增一条），未构成**替代**；因 CTRL-01 子句未回写，二者对“接入点无关”给出相反断言，即**双源**。正确关系应为“CTRL-01 的入口子句委派给 ENTRY-01（补充，不替代），CTRL-01 的 C3 租约/C6 fence 阻断仍留在 CTRL-01”。
- ENTRY-01 只承接入口语义，不承接租约（C3）与 fence 聚合（C6），故即使 ENTRY-01 转正也不能使 CTRL-01 转正。

## 7. 必改项（≤5）

1. **同步 CTRL-01 行**（`DOCS_REGISTRY.md:29`）：把“接入点无关（任一节点维护接口同语义）”与“**入口无关条款未满足**”改为“入口语义已委派 `REFMEM-ENTRY-01`（补充，不替代）”，并保留 C3/C6 的 pending 理由；否则不可转 active。
2. **补 check 文本**（`:31`）：逐项列出 目录/函数表、owner/非 owner/无 owner host、聚合 host、四域配置与 `FUNC_*`/MOUNT 的单口证据、非协调者 12，并注明 12 为**目标侧 host-only**、入口侧是 `NOT_CONTROL_OWNER`；`本地控制` 一条改为指向 `REFMEM-LOCAL-01`。
3. **修悬空引用**：`REFMEM_DOMAIN_ARCHITECTURE.md:1717` 的 `REFMEM-CTRL-ENTRY-01` → `REFMEM-ENTRY-01`。
4. **单口证据升级**：补含命令原文与端口的机器捕获，或把 clause 第 3 条降为“产品约束（未验证）”，并从 check 的 `ack=14` 推断中移除拓扑含义。
5. **硬数字**：36/6 改为引用 `refmem_command_catalog`/`refmem_function_catalog` 或标注“快照，非事实源”。

## 8. 检查器结果

```text
python tools/docs_check/docs_check.py --strict-names
OK   docs check passed: files=238 warnings=0

python tools/doc_regression_check.py
OK   freshness: top doc within window
WARN docs\check\DOCS_REGISTRY.md: row id ... does not match ROW_RE (×10，既有行 id 形态问题：T2-*/REFTMEM-*/TDMA-*)
OK   registry: 49 contracts, ids unique
OK   orphan: 10 top constraints all covered
OK   progress: 19 progress logs checked
OK   doc_regression passed
```

两项均通过（exit 0）；`REFMEM-ENTRY-01` 已被计入 49 条（id 形态可校验），10 条 WARN 为既有登记行形态问题，不构成本轮阻断。
