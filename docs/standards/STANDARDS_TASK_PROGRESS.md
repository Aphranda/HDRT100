# 通用规范域任务进度

Status: Active
Domain: Standards
Canonical: `docs/standards/STANDARDS_TASK_PROGRESS.md`
Related: `docs/standards/STANDARDS_DOMAIN_ARCHITECTURE.md`, `docs/standards/STANDARDS_DOMAIN_TODO.md`, `docs/standards/README.md`, `docs/standards/STANDARDS_VERSION_REVIEW_ARCHIVE_PLAYBOOK.md`, `docs/check/DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md`
Last updated: 2026-11-06

## 文档接口

本文件保存通用规范域的交付、验证、失败和回退证据，不替代 [TODO](STANDARDS_DOMAIN_TODO.md) 的状态，也不冻结标准正文。条目按新鲜度倒排，最新在最上面。

## 当前 checkpoint

2026-11-06：G1-M1～M5 完成，G1「标准产品化」完成；M6 保持 IN PROGRESS，等待 G3 外部试点反馈。

## 记录

### STANDARDS-PROGRESS-20261106-007：反馈与评估固化（G1-M5）

- **任务**：`STANDARDS-M4` 反馈入口 / `STANDARDS-M6-01` / `STANDARDS-M6-02` / `G1-M5`
- **变更/提交**：新增 `STANDARDS_FEEDBACK_EVALUATION_PLAYBOOK.md`（反馈来源/日志/分诊/回流、2026-11-06 反向评估记录、度量与模板）；更新 `docs/standards/README.md`、`docs/README.md`、`STANDARDS_DOMAIN_ARCHITECTURE.md`；`STANDARDS_DOMAIN_TODO.md` 将 `M4` 置 DONE、`M6` 置 IN PROGRESS、`M6-01`/`M6-02` 置 DONE；`DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md` 将 `G1-M5` 置 DONE、G1 标记完成；当前为工作区改动，未提交 commit。
- **构建/验证**：`python tools/docs_check/docs_check.py --strict-names` 通过（272 files，0 warnings）；`python tools/doc_regression_check.py` 通过（60 contracts；4 条既有 T2 Draft `clause_loc` WARN）；文档测试 45 passed。
- **结果**：PASS
- **证据**：`docs/standards/STANDARDS_FEEDBACK_EVALUATION_PLAYBOOK.md`、`docs/standards/README.md`、`docs/README.md`、`docs/standards/STANDARDS_DOMAIN_TODO.md`
- **失败/回退**：无。外部反馈当前 0 条，未写成已验证；M6 保持 IN PROGRESS。
- **下一步**：G3 外部试点提供反馈后关闭 M6；G2-M4 日志轮转。

### STANDARDS-PROGRESS-20261106-006：版本、复审与归档机制（G1-M4）

- **任务**：`STANDARDS-M5-01` / `STANDARDS-M5-02` / `G1-M4`
- **变更/提交**：新增 `STANDARDS_VERSION_REVIEW_ARCHIVE_PLAYBOOK.md`（版本规则、状态/替代、季度复审、changelog、归档与模板）；更新 `docs/standards/README.md`、`docs/README.md`、`STANDARDS_DOMAIN_ARCHITECTURE.md`；`STANDARDS_DOMAIN_TODO.md` 将 `M5-01`/`M5-02`/`M5` 置 DONE；`DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md` 将 `G1-M4` 置 DONE；当前为工作区改动，未提交 commit。
- **构建/验证**：`python tools/docs_check/docs_check.py --strict-names` 通过（271 files，0 warnings）；`python tools/doc_regression_check.py` 通过（60 contracts；4 条既有 T2 Draft `clause_loc` WARN）；文档测试 45 passed。
- **结果**：PASS
- **证据**：`docs/standards/STANDARDS_VERSION_REVIEW_ARCHIVE_PLAYBOOK.md`、`docs/standards/README.md`、`docs/README.md`
- **失败/回退**：首次 `docs_check` 因示例引用尚未创建的归档 `.md` 路径而失败；改为非 docs 路径示例后通过。
- **下一步**：`G1-M5` 外部反馈日志与反向评估记录；`M4` 试点反馈入口。

### STANDARDS-PROGRESS-20261106-005：本地化工具包（G1-M3）

- **任务**：`STANDARDS-M4-02` / `G1-M3`
- **变更/提交**：新增 `STANDARDS_LOCALIZATION_KIT_PLAYBOOK.md`（六系统映射、三件套、契约/工具/WorkflowSpec、验收凭证、结构工程示例、评审与裁剪记录）；更新 `docs/standards/README.md`、`docs/README.md`；`STANDARDS_DOMAIN_TODO.md` 将 `M4-02` 置 DONE，`M4` 置 IN PROGRESS；当前为工作区改动，未提交 commit。
- **构建/验证**：`python tools/docs_check/docs_check.py --strict-names` 通过（270 files，0 warnings）；`python tools/doc_regression_check.py` 通过（60 contracts；4 条既有 T2 Draft `clause_loc` WARN）；文档测试 45 passed（与 004 同批）。
- **结果**：PASS
- **证据**：`docs/standards/STANDARDS_LOCALIZATION_KIT_PLAYBOOK.md`、`docs/standards/README.md`、`docs/README.md`
- **失败/回退**：首次 `docs_check` 因示例引用被误判为不存在的 docs 路径而失败；改为项目本地路径 `LOCALIZATION.md` 后通过。
- **下一步**：`M4` 试点反馈入口；`G1-M4` 版本/复审/归档。

### STANDARDS-PROGRESS-20261106-004：采用指南（G1-M2）

- **任务**：`STANDARDS-M4-01` / `G1-M2`
- **变更/提交**：新增 `STANDARDS_ADOPTION_GUIDE_PLAYBOOK.md`（90 天采用路线、角色、最小产物、门禁、裁剪、结构工程示例、止损与检查清单）；更新 `docs/standards/README.md`、`docs/README.md`；`STANDARDS_DOMAIN_TODO.md` 将 `M4-01` 置 DONE；当前为工作区改动，未提交 commit。
- **构建/验证**：与 005 同批验证：`docs_check` 270 files / 0 warnings；`doc_regression_check.py` 通过（60 contracts）；文档测试 45 passed。
- **结果**：PASS
- **证据**：`docs/standards/STANDARDS_ADOPTION_GUIDE_PLAYBOOK.md`、`docs/standards/README.md`、`docs/README.md`
- **失败/回退**：无（引用问题见 005）。
- **下一步**：`G1-M3` localization kit。

### STANDARDS-PROGRESS-20261106-003：长期目标与里程碑登记

- **任务**：`G1`–`G4` 目标登记；`G1` 对应 `STANDARDS-M4`–`M6`
- **变更/提交**：新增 `docs/check/DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md`（G1 标准产品化、G2 项目治理收束、G3 外部采用、G4 发布准备）；更新 `docs/README.md`、`STANDARDS_DOMAIN_TODO.md`、`README.md` 的入口/引用；当前为工作区改动，未提交 commit。
- **构建/验证**：`python tools/docs_check/docs_check.py --strict-names` 通过（268 files，0 warnings）；`python tools/doc_regression_check.py` 通过（60 contracts；4 条既有 T2 Draft `clause_loc` WARN）；`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider` 45 passed。
- **结果**：PASS
- **证据**：`docs/check/DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md`、`docs/README.md`、`docs/standards/STANDARDS_DOMAIN_TODO.md`、`docs/standards/README.md`
- **失败/回退**：无。
- **下一步**：`G1-M2` adoption guide；`G1-M3` localization template；`G2-M4` 日志轮转。

### STANDARDS-PROGRESS-20261106-002：通用规范域三件套建立

- **任务**：`STANDARDS-M3-01` / `STANDARDS-M3-02` / `STANDARDS-M3-03`
- **变更/提交**：新增 `STANDARDS_DOMAIN_ARCHITECTURE.md`、`STANDARDS_DOMAIN_TODO.md`、`STANDARDS_TASK_PROGRESS.md`；更新 `docs/standards/README.md` 与 `docs/README.md` 索引；当前为工作区改动，未提交 commit。
- **构建/验证**：`python tools/docs_check/docs_check.py --strict-names` 通过（267 files，0 warnings）；`python tools/doc_regression_check.py` 通过（60 contracts；4 条既有 T2 Draft `clause_loc` WARN）；`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider` 45 passed。
- **结果**：PASS
- **证据**：`docs/standards/STANDARDS_DOMAIN_ARCHITECTURE.md`、`docs/standards/STANDARDS_DOMAIN_TODO.md`、`docs/standards/STANDARDS_TASK_PROGRESS.md`、`docs/standards/README.md`、`docs/README.md`
- **失败/回退**：无。
- **下一步**：`STANDARDS-M4-01` adoption guide；`STANDARDS-M4-02` localization template。

### STANDARDS-PROGRESS-20261106-001：标准域建立与主标准交付

- **任务**：`STANDARDS-M1-01` / `STANDARDS-M1-02` / `STANDARDS-M2-01` / `STANDARDS-M2-02`
- **变更/提交**：新增 `docs/standards/README.md`、`STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`、排版 DOCX；`docs/README.md` 增加 standards 域入口与索引；`docs_check.py` 增加 `STANDARDS` 前缀，`test_docs_check.py` 增加对应断言；当前为工作区改动，未提交 commit。
- **构建/验证**：`docs_check --strict-names` 通过（264 files，0 warnings）；`doc_regression_check.py` 通过（60 contracts）；文档测试 45 passed；DOCX 结构校验通过（24 个一级标题、103 个二级标题、45 张表，作者/邮箱/封面签署与文末签署表齐全）。
- **结果**：PASS
- **证据**：`docs/standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`、`docs/standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.docx`、`docs/standards/README.md`、`docs/README.md`、`tools/docs_check/docs_check.py`、`tests/python/test_docs_check.py`
- **失败/回退**：DOCX 页面级视觉复核未完成——本机 LibreOffice 预览组件对最小 DOCX 也崩溃；已用 `python-docx` 与 OOXML 结构/顺序校验替代，未发现结构性错误。
- **下一步**：建立域三件套。

## 验证与证据索引

| 验证 | 命令 | 结果 | 证据 |
|---|---|---|---|
| 文档元数据/索引/命名/链接 | `python tools/docs_check/docs_check.py --strict-names` | OK，267 files，0 warnings | stdout |
| 契约登记/新鲜度/轮转 | `python tools/doc_regression_check.py` | PASS；60 contracts；4 条既有 T2 Draft `clause_loc` WARN | stdout |
| 检查器回归 | `python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider` | 45 passed | stdout |
| DOCX 结构 | `python-docx` 重新打开并校验标题/表格/目录域/署名 | PASS | DOCX |

## 失败与回退

- 无功能失败。
- DOCX 视觉复核不可用属于本机工具限制，不是文档内容失败；当前以结构校验和 Markdown 源为准。
- 未提交、未 push；如提交，文档与工具/测试改动按仓库规则分流凭证。

## 下一 gate

- `STANDARDS-M4`：adoption guide 与 localization template 完成并通过文档门禁；
- `STANDARDS-M5`：季度复审、changelog、旧版归档规则建立；
- `STANDARDS-M6`：外部反馈日志与反向评估记录固化。
