# Agent 常设命令（Codex / Claude / AI 助手）

本仓库采用「文档自回归体系」治理文档。**任何 AI 助手（Codex / Claude / DSH）在本仓库工作前，必须先阅读 §0 理解该体系，再动手改文档。** 违反体系约束会被 git pre-commit 门禁拦截。

## 0. 先读（onboarding，改文档前必须）

| 文件 | 内容 |
|---|---|
| `docs/check/DOCS_REGRESSION_PLAN.md` | 体系规格：冻结契约、硬约束、交付物、迁移阶段 |
| `docs/check/DOCS_REGISTRY.md` | **契约登记表 + 条款落点表**（唯一事实源，检查器读取） |
| `docs/check/DOCS_REGRESSION_REVIEW.md` | 实施经验：常见坑与解法 |
| `docs/check/DOCS_REGRESSION_TODO.md` | 待办状态（哪些已完成、哪些下一期） |

## 1. 体系是什么（30 秒版）

三环自回归 + 门禁 + 交叉审核：

```
环1 新鲜度: 顶层文档(HAOFV_ARCHITECTURE.md) 7 天内必须反映域文档新冻结的契约，否则检查器 FAIL
环2 登记:   域文档冻结跨域契约 → 必须在 DOCS_REGISTRY.md 登记（contract_id 唯一，锚点真实）
门禁:       git pre-commit 自动跑两个检查器，任一 FAIL 阻断提交
交叉审核:   登记表状态变更禁止自审自批（C11）
```

## 2. 硬约束（违反 = 门禁 FAIL，不可绕过）

1. 文档 5 字段元数据（Status/Domain/Canonical/Related/Last updated）必须在标题后 **7 行内**
2. 文档硬数字**不手写**：引用代码符号、登记、或标注"快照，非事实源"
3. 新增任何 .md → 必须：命名合规（`<PREFIX>_<TOPIC>_<SUFFIX>.md`）+ 在 `docs/README.md` 加索引行
4. 登记契约不可物理删除（只能 `superseded`）
5. 修改任何文档 → 必须更新 `Last updated`
6. 临时内容（评审快照/草稿）**不得**登记为契约

## 3. 修改文档的标准流程

1. 冻结契约 → `docs/check/DOCS_REGISTRY.md` 加一行（contract_id 唯一，clause_loc/code_anchor 必须真实存在）
2. 改文档 → 更新 Last updated；数字引用符号
3. 验证 → 跑 §5 全部命令，必须全绿
4. 提交 → pre-commit 自动跑（FAIL 先修）

## 4. 常见坑（来自实施经验，DOCS_REGRESSION_REVIEW.md）

- **元数据窗口**：额外字段（Target/Source of truth 等）会挤掉 Required 字段——5 个 Required 放最前
- **code_anchor 必须是真实文件**：方案猜测的文件名往往不存在，登记前先确认（曾踩坑：tdma_ring_profile.h 实际叫 tdma_profile.h）
- **检查器改签名 → 同步改测试**，否则 pytest 立刻红
- **改完 `tools/doc_regression_check.py` → 必须 `python tools/doc_regression_check.py --skill-sync`**（同步三份副本，检查器自检）
- **日期**：`Last updated` 必须 `YYYY-MM-DD` 且合法（非法日期触发 FAIL）

## 5. 验证命令（改文档后必跑）

```powershell
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py
python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider
sh .githooks/pre-commit
python tools/doc_regression_check.py --log-check   # 逃生门审计（建议每周）
```

> **门禁接线**：pre-commit 依赖 `git config core.hooksPath .githooks`（本地配置，clone/迁移后**必须重配**，否则 commit 不触发检查器）。`sh .githooks/pre-commit` 可手动验证。

## 6. 当前状态与进行中任务

- 已登记契约：16 行（5 active + 10 pending + 1 superseded，见 `docs/check/DOCS_REGISTRY.md`）
- 条款落点：10 条顶层硬约束全覆盖（1 条 VIOLATED：HAOFV-879 seqlock）
- **已完成**：顶层 `docs/arch/HAOFV_ARCHITECTURE.md` 刷新（v3，2026-08-21）；2026-08-24 审查修复（门禁接线 / 路径修正 / T18 闭环 / 顶层补 DOCS-FLASH-01）
- 下一期：verify-doc-crosscheck 自动化强化（见 TODO）

## 7. 提交约定

- 文档/契约变更与代码变更**分离提交**，便于 `git log` 审计
- 提交信息含契约变更时带 `contract_id`（如 `docs(tdma): 登记 TDMA-REASON-01`）
