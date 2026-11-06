# 核验提交单：文档治理 → 契约登记表（C14/C15 进展日志）

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/submissions/DOCS_PROGRESS_CROSS_REVIEW_01.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/check/DOCS_REGRESSION_PLAN.md`, `docs/check/DOCS_PROGRESS_ROTATION_PLAN.md`, `docs/check/DOCS_CONTRACT_ACTIVATION_WAVE_PLAN.md`
Last updated: 2026-11-06

## 提交内容

对 Wave 0 的两条文档治理契约做独立 C11 复核：

- `DOCS-PROGRESSROTATE-01`：进展日志 C14 轮转；
- `DOCS-PROGRESSORDER-01`：进展日志 C15 倒排。

复核只读，未修改代码、契约状态或日志；目标不是“凑一个 active”，而是判断 `check` 字段是否真的被覆盖。

| contract_id | C11 结论 | 证据 | 状态决定 |
|---|---|---|---|
| DOCS-PROGRESSROTATE-01 | `PASS_WITH_NOTE` | `tests/python/test_doc_regression.py` 39 passed；`doc_regression_check.py --progress` OK，21 份日志 | **保持 pending**：R4 反向闭包未实现，若干负例/边界未测 |
| DOCS-PROGRESSORDER-01 | `PASS_WITH_NOTE` | 同上；C15 正反用例覆盖日期非递增、同日顺序、归档倒序 | **保持 pending**：归档/索引“同向倒排”未被强制，升序多条目区间无用例 |

## 独立审核发现

### DOCS-PROGRESSROTATE-01

1. **实现缺口**：legacy 目录下的 `LEGACY_*_TASK_PROGRESS_*.md` 若存在但未被任何 `## 归档索引` 行列出，检查器不会扫描 legacy 目录；C14 R4 的“闭包/不得有未挂账条目”只实现了“索引 → 文件/条目”，反向“目录 → 索引”未实现。
2. **归档间重复 ID 未检查**：实现只比较归档 ID 与规范文件 ID，两个归档文件之间的重复 ID 会通过。
3. **多归档连续性未检查**：只检查“所有归档最大日期 ≤ 规范文件最小日期”，`_01`/`_02` 之间的重叠或断档会通过；无多归档行用例。
4. **R3 域目录未匹配**：只校验 `docs/legacy/` 前缀与文件名正则，sync 日志归档到 `docs/legacy/tdma/` 也能通过；缺“legacy 内错域目录”负例。
5. 缺少负例：索引列出但文件缺失；`id_range` 无 `..`；有归档但规范文件无保留条目；缺 `Last updated`。
6. 200KB 阈值只测了 `PROGRESS_MAX_BYTES + 16`，未锁“正好 204800 字节通过”的边界。
7. 条目解析未测：非法日历日期被丢弃、字面 `YYYYMMDD-NNN` 模板不计入。
8. **R5 checkpoint 检查被“已有归档索引”前置**：未轮转的规范日志即使没有 `## 当前 checkpoint` 也会通过；只有已轮转场景有负例。

### DOCS-PROGRESSORDER-01

1. “区间方向双向容忍”只测了降序多条目区间；升序多条目区间（oldest..newest）无用例，单条目 `002..002` 不构成方向证据。
2. C15 正文要求“归档文件与 `## 归档索引` 区间同向倒排”，实现把两者独立宽松判定（区间取 min/max，归档只判日期非递增），同向未被强制，也无正反用例；登记行“双向容忍”的措辞与正文存在口径差异。
3. 归档文件同日顺序不限与规范一致，但缺归档同日用例；同日非单调顺序（如 `-002,-001,-003`）也未测。
4. `PROGRESS_ROTATION_DEBT` / `PROGRESS_ORDER_DEBT` 中 2026-10-17 已过期条目未清理且无机检；当前全绿只说明对应文件已不超限/不倒置。

## 偏差声明

- 两条契约**不转 active**。`PASS_WITH_NOTE` 表示“用例与门禁全绿”，不等于 `check` 字段全部覆盖；在 R4 反向闭包、C15 同向语义和边界测试补齐前，保持 `pending` 是更诚实的结论。
- 本次不改 `DOCS_REGISTRY.md` 状态行；registry 的 `Last updated` 也不因本提交单变化。
- 未启动台架；本提交单属于 host/docs 范围。

## Alternatives considered

- 方案A：看到 39 passed 就直接转 active（拒绝：check 字段包含负例/闭包/同向语义，当前未全部覆盖）。
- 方案B：把契约文本改成“只要求正向闭包/双向容忍”（拒绝：这是降低契约标准来迁就实现，需单独评审，不能在本提交单里顺手改）。
- 方案C：保持 pending，补实现/测试后再审（接受）。

## 核验结论

- 结论：`ACCEPT_WITH_DEVIATION`（复核通过，激活延后）
- 核验人：主会话（作者）
- 未关闭项：见下方“后续任务”

## 交叉审核记录（C11，必填）

- 审核方：独立只读子 agent `6168f704-42c8-4191-9956-93014c63f081`；第二位独立只读子 agent `5a8bb1e2-de73-40c5-b67a-fab69cbd0a28` 复核同结论
- 审核方式：agent 交叉；两轮独立只读代码/测试审查 + `pytest tests/python/test_doc_regression.py` + `doc_regression_check.py --progress`
- 审核结论：`DOCS-PROGRESSROTATE-01`、`DOCS-PROGRESSORDER-01` 均为 `PASS_WITH_NOTE`；两次独立复核结论一致
- 审核日期：2026-11-06

## 后续任务

| ID | 任务 | 门禁 |
|---|---|---|
| `DOCS-PROGRESSROTATE-01-F1` | 实现 legacy 目录反向闭包、归档间重复 ID、多归档连续性、R3 域匹配、R5 checkpoint 覆盖；补 204800/204801 边界与缺失文件/无 `..`/无保留条目负例 | host tests + C11 |
| `DOCS-PROGRESSORDER-01-F1` | 明确 C15“同向”语义：要么实现强制，要么改契约措辞；补升序多条目区间、同日非单调顺序用例 | host tests + C11 |
| `DOCS-PROGRESS-01-F2` | 清理过期 `PROGRESS_*_DEBT` 条目，或加截止日机检 | host tests + C11 |

完成上述任务并再次 C11 通过后，才把两条契约从 `pending` 转 `active`。
