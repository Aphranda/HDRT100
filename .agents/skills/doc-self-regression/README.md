# doc-self-regression 插件说明

Status: Active
Domain: Documentation Governance
Canonical: `.agents/skills/doc-self-regression/README.md`
Related: `docs/check/DOCS_REGRESSION_PLAN.md`, `docs/check/DOCS_REGRESSION_REVIEW.md`
Last updated: 2026-08-24

> 本目录是「文档自回归 skill 插件」的完整形态：一个自包含、可复用的文档治理包。

## 插件组成

本目录（插件本体）共 4 个文件：

| 文件 | 角色 |
|---|---|
| `SKILL.md` | skill 本体（YAML frontmatter 触发 + 工作流 + 常见坑），Codex/Claude/DSH 均识别 |
| `README.md` | 本说明（插件清单 + 安装 + 使用） |
| `doc_regression_check.py` | 检查器：环1 新鲜度 + 环2 登记/孤儿条款 + C3 逃生门审计（纯 stdlib，**插件自包含副本**） |
| `DOCS_REGISTRY.template.md` | 契约登记表模板（插件自包含副本） |

插件运行依赖的仓库文件：

| 文件 | 角色 |
|---|---|
| `tools/docs_check/docs_check.py` | 既有检查器（元数据/索引/链接/命名/冲突标记），被插件引用 |
| `docs/check/DOCS_REGISTRY.md` | 契约登记表 + 条款落点表（唯一事实源，**live 版**） |
| `docs/check/DOCS_REGRESSION_TODO.md` | 实施待办（执行跟踪） |
| `docs/check/DOCS_REGRESSION_PLAN.md` | 实施规格（方案归档） |
| `docs/check/DOCS_REGRESSION_REVIEW.md` | 实施经验总结 |
| `docs/check/submissions/README.md` | 核验提交单模板（层间核验 + C11 交叉审核） |
| `.githooks/pre-commit` | 硬门禁（git config core.hooksPath .githooks 激活） |

> **脚本副本关系**：`tools/doc_regression_check.py` 是 live 版（pre-commit/pytest 使用）；
> 本目录与 harness `.agents/skills/doc-self-regression/` 各持一份插件快照。
> 修改 live 版后须同步两个 skill 快照（见待办 T21 同步规则）。

## 安装（一次性）

```bash
cd <仓库根>                            # AGENTS.md 所在目录
git config core.hooksPath .githooks    # 激活 pre-commit 门禁（clone/迁移后必须重配）
```

## 使用

- 日常：改文档/代码时按 `SKILL.md` 工作流；提交时 pre-commit 自动检查
- 审计：`python tools/doc_regression_check.py --log-check`（逃生门检查，建议每周）
- 测试：`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider`

## 在其他项目复用

1. 拷贝本目录 + `tools/doc_regression_check.py` + `.githooks/pre-commit`
2. 修改 `doc_regression_check.py` 顶部常量：`REGISTRY_REL`（登记表路径）、`TOP_DOC_REL`（顶层文档路径）、`FRESHNESS_EXCLUDE_DIRS`
3. 建登记表（参考 `docs/check/DOCS_REGISTRY.md` 格式），跑一次检查器验证
