---
name: doc-self-regression
description: Use when maintaining the HAOFV docs corpus — registering a frozen contract, refreshing the top-level doc, reviewing doc changes, or on "docs drift", "register contract", "check docs", "docs out of sync", "audit docs" requests. Enforces contract registration, freshness windows, verify-doc-* gates (tools/docs_check/docs_check.py + tools/doc_regression_check.py), and cross-review (C11).
---

# Doc Self-Regression

适用于本仓库（以 `AGENTS.md` 所在目录为仓库根）`docs/` 的 HAOFV 文档体系。约定 + 轻量检查 + pre-commit 硬门禁。
实施过程与经验见 `docs/check/DOCS_REGRESSION_REVIEW.md`。

## 触发时机

- 域文档冻结跨域契约（wire 格式 / 错误码 / 容量 / 时序门禁）→ 必须登记
- 修改 canonical 文档 / 顶层文档 → 必须更新 Last updated，登记影响
- 评审 / 审查文档变更 → 先跑 verify 命令
- 新增任何 .md 文件 → 必须过 docs_check 三道关（元数据 / 索引 / 命名）

## 硬约束（冲突时不可违反，优先级最高）

- 检查器只读：`doc_regression_check.py` 永不写文件
- 登记契约不可物理删除（只能 superseded）
- 临时内容（评审快照 / 草稿）不得登记为契约
- 新契约登记后顶层 `docs/arch/HAOFV_ARCHITECTURE.md` 7 天内刷新
- 登记表状态变更必须交叉审核（C11）：禁止自审自批

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
# 全量（文档元数据/索引/链接/命名 + 新鲜度/登记/孤儿条款）
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py

# 逃生门审计（检查最近 commit 是否绕过 pre-commit，建议每周一次）
python tools/doc_regression_check.py --log-check

# pytest（含检查器自回归测试；沙箱环境需 --basetemp）
python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider

# 只查变更范围
python tools/doc_regression_check.py --scope "$(git diff --cached --name-only | tr '\n' ' ')"
```

输出 `[OK]/[WARN]/[FAIL]`，任一 FAIL 阻断 commit（pre-commit 自动执行）。

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
