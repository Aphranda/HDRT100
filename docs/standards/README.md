# 通用规范子域

Status: Active
Domain: Standards
Canonical: `docs/standards/README.md`
Related: `docs/standards/STANDARDS_DOMAIN_ARCHITECTURE.md`, `docs/standards/STANDARDS_DOMAIN_TODO.md`, `docs/standards/STANDARDS_TASK_PROGRESS.md`, `docs/standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`, `docs/standards/STANDARDS_ADOPTION_GUIDE_PLAYBOOK.md`, `docs/standards/STANDARDS_LOCALIZATION_KIT_PLAYBOOK.md`, `docs/standards/STANDARDS_VERSION_REVIEW_ARCHIVE_PLAYBOOK.md`, `docs/standards/STANDARDS_FEEDBACK_EVALUATION_PLAYBOOK.md`, `docs/README.md`
Last updated: 2026-11-06

本目录存放**可复用的通用规范**：从具体项目治理实践中抽象出来、可交付给其他团队/项目复用的标准与方法。它与产品域文档分开，避免把"通用方法"误当成某个产品的冻结契约。

## 定位

- **是**：跨项目可复用的治理标准、模板、检查清单、落地路线。
- **不是**：HDRT100 的产品契约、运行时约束或验收判据；也不替代具体项目的安全规范、法规和合同要求。
- **权威关系**：具体项目内仍以该项目自己的 `AGENTS.md`、契约登记表和域 canonical 文档为准；本目录只提供方法和模板。

## 文件索引

| 文件 | 定位 |
|---|---|
| `STANDARDS_DOMAIN_ARCHITECTURE.md` | 通用规范域架构：定位、边界、文档模型、标准生命周期、编写规则与验证映射。 |
| `STANDARDS_DOMAIN_TODO.md` | 通用规范域任务状态唯一落点：里程碑、任务表、阻塞与完成定义。 |
| `STANDARDS_TASK_PROGRESS.md` | 通用规范域交付与验证证据：按时间倒排，不替代 TODO 状态。 |
| `STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md` | 通用化大型项目 AI 协作管理体系：文档、契约、工具、Skill、工作流、证据与多角色协作的一体化治理；含模板、路线图与结构工程适配示例。 |
| `STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.docx` | 上述规范的排版交付版（含封面、目录、作者署名与签署栏），内容以 Markdown 为准。 |
| `STANDARDS_ADOPTION_GUIDE_PLAYBOOK.md` | 外部团队 90 天采用手册：阶段路线、角色、最小产物、门禁、裁剪和止损。 |
| `STANDARDS_LOCALIZATION_KIT_PLAYBOOK.md` | 本地化填空工具包：六系统映射、三件套、契约/工具/WorkflowSpec、验收凭证和结构工程示例。 |
| `STANDARDS_VERSION_REVIEW_ARCHIVE_PLAYBOOK.md` | 版本、复审、changelog 与归档机制：版本规则、季度复审、旧版归档和模板。 |
| `STANDARDS_FEEDBACK_EVALUATION_PLAYBOOK.md` | 反馈与评估手册：反馈日志、分诊/处置/回流、反向评估记录和模板。 |

## 域三件套

本域按仓库的域文档规范维护三件套：

- **Architecture**（`STANDARDS_DOMAIN_ARCHITECTURE.md`）：稳定语义、边界和标准编写规则；
- **TODO**（`STANDARDS_DOMAIN_TODO.md`）：里程碑与任务状态；
- **Task Progress**（`STANDARDS_TASK_PROGRESS.md`）：交付、验证、失败与证据。

主标准 `STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md` 是本域的核心交付物，不替代域三件套。

## 使用方式

1. 先读 `STANDARDS_DOMAIN_ARCHITECTURE.md` §1–§5，了解域边界和文档模型；再读 `STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md` §0、§1，判断适用性和成熟度目标。
2. 按 §10.1 的"最小可行治理"选 6 件事启动，不要一次全量铺开。
3. 用附录 A–H 的模板建立本项目的第一版文档、契约、工具、Skill、工作流和里程碑报告。
4. 每季度按 §11.3 审计并**删减**低价值规则；通用规范本身也按版本维护。
5. 本域的长期目标（G1）与跨域目标（G2–G4）见 [项目治理长期目标与里程碑](../check/DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md)。
6. 外部团队采用时，先读 `STANDARDS_ADOPTION_GUIDE_PLAYBOOK.md` 的 90 天路线，再用 `STANDARDS_LOCALIZATION_KIT_PLAYBOOK.md` 做本地化填空。

## 边界

- 本目录的文档不登记为 `DOCS_REGISTRY.md` 的产品契约。
- 引用本目录的具体项目，应把适用条款**本地化**到自己的域文档，并登记本地契约；不要直接把通用规范当成项目事实源。
- 结构专业的安全相关计算必须由具备相应资格的人员复核和签章；本规范中的 AI 工作流不改变这一责任边界。
