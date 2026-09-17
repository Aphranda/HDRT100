---
name: doc-self-regression
description: Use when maintaining the HAOFV docs corpus — registering a frozen contract, refreshing the top-level doc, reviewing doc changes, rotating an oversized `*_TASK_PROGRESS.md` into `docs/legacy/`, or on "docs drift", "register contract", "check docs", "docs out of sync", "audit docs", "progress log too big" requests. Enforces contract registration, freshness windows, progress-log rotation (C14), verify-doc-* gates (tools/docs_check/docs_check.py + tools/doc_regression_check.py), and cross-review (C11).
---

# Doc Self-Regression

适用于本仓库（以 `AGENTS.md` 所在目录为仓库根）`docs/` 的 HAOFV 文档体系。约定 + 轻量检查 + pre-commit 硬门禁。
实施过程与经验见 `docs/check/DOCS_REGRESSION_REVIEW.md`。

## 触发时机

- 域文档冻结跨域契约（wire 格式 / 错误码 / 容量 / 时序门禁）→ 必须登记
- 修改 canonical 文档 / 顶层文档 → 必须更新 Last updated，登记影响
- 评审 / 审查文档变更 → 先跑 verify 命令
- 新增任何 .md 文件 → 必须过 docs_check 三道关（元数据 / 索引 / 命名）
- `*_TASK_PROGRESS.md` 超过 200KB、或要给已轮转的日志追加 checkpoint → 先按 C14 轮转，再追加

## 硬约束（冲突时不可违反，优先级最高）

- 检查器只读：`doc_regression_check.py` 永不写文件
- 登记契约不可物理删除（只能 superseded）
- 临时内容（评审快照 / 草稿）不得登记为契约
- 新契约登记后顶层 `docs/arch/HAOFV_ARCHITECTURE.md` 7 天内刷新
- 登记表状态变更必须交叉审核（C11）：禁止自审自批
- 进展日志单文件 ≤200KB（C14 R1）；归档只搬**最旧**条目并保持连续（R2），
  归档索引必须闭包（R4），`Last updated` 不得早于最新条目（R5）
- 进展日志条目按新鲜度**倒排**——**最新鲜的在最上面**（C15 R6）；
  新条目写入 `## 当前 checkpoint` 之后，归档文件与索引区间同向倒排

## 契约登记（上提）

域文档冻结契约时，在 `docs/check/DOCS_REGISTRY.md` 加一行：

```markdown
| contract_id | domain | contract | ver | clause_loc | code_anchor | check | registered | status |
|---|---|---|---|---|---|---|---|---|
| TDMA-REASON-01 | tdma | ring reason code 9 项冻结 | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_ring_runtime.h | enum 比对 | 2026-08-19 | active |
```

- `contract_id` = `<域>-<主题>-<序号>`，全库唯一
- `clause_loc` = 域文档相对路径（文件必须存在，Status 必须 Active/Frozen）；`code_anchor` = 代码文件名（必须存在）
- **锚点必须以实际代码为准**：先确认文件真实存在再登记（曾踩坑：方案猜测的 tdma_ring_profile.h 实际叫 tdma_profile.h）

## 检查命令

```bash
# 全量（文档元数据/索引/链接/命名 + 新鲜度/登记/孤儿条款 + 进展日志轮转）
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py

# 只跑轮转环（环5）
python tools/doc_regression_check.py --progress

# 逃生门审计（检查最近 commit 是否绕过 pre-commit，建议每周一次）
python tools/doc_regression_check.py --log-check

# pytest（含检查器自回归测试；沙箱环境需 --basetemp）
python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider

# 只查变更范围
python tools/doc_regression_check.py --scope "$(git diff --cached --name-only | tr '\n' ' ')"
```

输出 `[OK]/[WARN]/[FAIL]`，任一 FAIL 阻断 commit（pre-commit 自动执行）。

## 进展日志轮转（C14 / 环5）

`*_TASK_PROGRESS.md` 是只追加的证据日志，必须可读且有界。轮转只搬**旧**证据，禁止丢证据。

**排序约定：最新鲜的在最上面（倒排）。** 新条目追加在文件顶部（`## 当前 checkpoint` 之后），
轮转从**尾部**连续截取最旧的一段；归档文件与 `## 归档索引` 区间同样倒排
（`<最新 ID>..<最旧 ID>`）。R2 只按**日期**判定边界，不强制日内顺序——存量日志在同一天内
多用升序块，轮转时保留各自既有风格。

| # | 规则 | 判定 |
|---|---|---|
| R1 | 单文件 ≤200KB | 超限 FAIL（在 `PROGRESS_ROTATION_DEBT` 债务内者 WARN 至截止日） |
| R2 | 归档必须**最旧优先连续**：归档条目最大日期 ≤ 规范文件保留条目最小日期 | 违反 FAIL |
| R3 | 归档到 `docs/legacy/<domain>/LEGACY_<DOMAIN>_TASK_PROGRESS_<NN>.md` | 位置/命名不符 FAIL |
| R4 | `## 归档索引` 闭包：文件存在、声明条目数相符、规范∪归档 ID 无重复、归档 ID 都在索引区间内 | 违反 FAIL |
| R5 | 保留 `## 当前 checkpoint`；`Last updated` 不得早于最新条目日期 | 违反 FAIL |
| R6 | 条目按新鲜度**倒排**（C15）：日期序列非递增，**最新鲜的在最上面**；归档文件同样倒排 | 违反 FAIL（登记 `PROGRESS_ORDER_DEBT` 者 WARN 至截止日） |

轮转步骤：

1. 逐条解析条目（`^#{2,3}\s+<PREFIX>-YYYYMMDD-NNN`），**只搬最旧的一段**，规范文件留最新的一段。
2. 迁出的条目逐字写入归档文件（不改写），归档文件补 5 字段元数据（Status: Frozen）。
3. 规范文件文末补 `## 归档索引` 表：`| 文件 | ID 区间 | 条目数 | 归档日期 |`。
4. 规范文件保留 `## 当前 checkpoint`；更新 `Last updated`。
5. 归档文件在 `docs/README.md` 加索引行（docs_check 强制）。
6. 债务表 `PROGRESS_ROTATION_DEBT` **只允许删除条目**（轮转完成后），新增豁免需交叉审核（C11）。

保留窗口 90 天是**指导值**，R1 的字节上限优先——实测 VDC 176 条全在 12 天内已达 668KB，
时间窗口不构成约束。

## 新增文档三道关（docs_check 会查，先自查）

1. **元数据**：5 字段（Status/Domain/Canonical/Related/Last updated）必须在标题后 7 行内
2. **索引**：在 `docs/README.md` 对应表加一行
3. **命名**：`<PREFIX>_<TOPIC>_<SUFFIX>.md`，前缀/后缀须在白名单（不够时增量加白名单，同时改 docs_check 测试）

## 工作流

1. 冻结契约 → 登记表加一行（contract_id 唯一，锚点真实）
2. 改文档 → 更新 Last updated；数字引用代码符号，不手写
3. 提交前 → 检查器自动跑（pre-commit）；FAIL 先修
4. 评审 → 结论写回登记表 status（OK/PARTIAL/VIOLATED/PENDING）+ 交叉审核记录（提交单模板见 `docs/check/submissions/README.md`）

## 常见坑（来自实施经验）

- 日期解析：`Last updated` 必须 `YYYY-MM-DD` 且合法（非法日期会触发检查器 FAIL）
- 元数据窗口：额外字段（Target/Source of truth）会挤掉 Required 字段——Required 五个放最前
- 文档数字：一律引用代码符号或登记，禁止裸写 `#define X 292`
- 检查器改签名 → 同步改测试，否则 pytest 立刻红
- **条目日期只能按 ID 解析**：正文里的 8 位数字会造出 `2078-49-87` 这种假日期，必须用
  `^#{2,3}\s+<PREFIX>-YYYYMMDD-NNN` 并校验合法日历日
- **阈值按字节不按字符**：中文 1 字符 = 3 字节，用 `len(s)` 判 200KB 会放过 3 倍超限的文件
- 归档路径写在表格里**带反引号**时，检查器必须 `strip("`")` 再比对（否则误报 R3）
