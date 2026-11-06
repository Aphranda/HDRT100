# 通用规范域待办

Status: Active
Domain: Standards
Canonical: `docs/standards/STANDARDS_DOMAIN_TODO.md`
Related: `docs/standards/STANDARDS_DOMAIN_ARCHITECTURE.md`, `docs/standards/STANDARDS_TASK_PROGRESS.md`, `docs/standards/README.md`, `docs/standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`, `docs/check/DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md`
Last updated: 2026-11-06

## 文档接口与状态规则

本表是通用规范域任务状态的**唯一落点**；稳定语义见 [Architecture](STANDARDS_DOMAIN_ARCHITECTURE.md)，交付与验证证据见 [Task Progress](STANDARDS_TASK_PROGRESS.md)。

状态只使用：

- `DONE`：退出门禁完成，证据已记录；
- `IN PROGRESS`：已启动，尚未满足退出门禁；
- `PENDING`：未启动；
- `BLOCKED`：有明确外部阻塞、失败证据和解除条件。

`DONE` 不表示"完美"，只表示该任务的完成定义已满足；父任务不因个别子项通过而自动关闭。

## 已有基线

| 能力 | 已有范围 | 证据入口 |
|---|---|---|
| 域入口与索引 | `docs/standards/README.md`；`docs/README.md` 已登记 standards 域 | README、总索引 |
| 主标准 | `STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md` v1.0，含六系统、模板、路线图和结构工程适配 | 主标准 |
| 排版交付 | DOCX 含封面、目录、作者署名、封面/文末签署栏 | DOCX |
| 命名与门禁 | `STANDARDS` 前缀已加入 `docs_check` 白名单，并有测试覆盖 | `tools/docs_check/docs_check.py`、`tests/python/test_docs_check.py` |
| 域三件套 | 本文件、Architecture、Task Progress | 三件套 |

## 当前主线

本域的长期目标 `G1 标准产品化` 及跨域目标 `G2–G4` 见 [项目治理长期目标与里程碑](../check/DOCS_PROJECT_GOVERNANCE_GOALS_PLAN.md)。

从"一次性交付的通用文档"收束为"可复用、可版本化、可交付、可反馈"的标准产品：

1. 把域三件套补齐，明确稳定语义、状态和证据的边界；
2. 为外部采用者提供 adoption guide 和 localization kit；
3. 建立季度复审、版本归档和外部反馈闭环；
4. 把反向评估从会话结论固化为可追踪的评估记录。

## 里程碑总览

| ID | 里程碑 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| STANDARDS-M1 | 域建立与索引接线 | DONE | README、总索引、`STANDARDS` 前缀、文档门禁全绿 |
| STANDARDS-M2 | 主标准 v1.0 与排版交付 | DONE | Markdown 正文、DOCX、作者署名/签署、结构校验 |
| STANDARDS-M3 | 域三件套 | DONE | Architecture / TODO / Task Progress 互相引用，门禁通过 |
| STANDARDS-M4 | 外部采用工具包 | DONE | adoption guide + localization template + 试点反馈入口（反馈日志已建立） |
| STANDARDS-M5 | 版本与复审机制 | DONE | 版本规则、季度复审、旧版归档、changelog 已建立；首次复审 2027-02-06 |
| STANDARDS-M6 | 反馈与评估闭环 | IN PROGRESS | 反馈日志与反向评估记录已建立；外部反馈待 G3 试点输入 |

## 分阶段任务

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| STANDARDS-M1-01 | 建立 `docs/standards/` 子域、README、总索引 | DONE | `docs_check` 索引/元数据/命名通过 |
| STANDARDS-M1-02 | 新增 `STANDARDS` 命名前缀并补测试 | DONE | `test_docs_check.py` 覆盖；文档门禁通过 |
| STANDARDS-M2-01 | 编写主标准 v1.0 | DONE | 六系统、模板、路线图、结构工程适配齐全 |
| STANDARDS-M2-02 | 生成 DOCX（封面、目录、署名、签署） | DONE | 结构校验：标题/表格/目录域/签名齐全 |
| STANDARDS-M3-01 | 编写域 Architecture | DONE | 定位、边界、文档模型、生命周期、验证映射齐全 |
| STANDARDS-M3-02 | 编写域 TODO | DONE | 状态词汇、里程碑、任务表、完成定义齐全 |
| STANDARDS-M3-03 | 编写 Task Progress | DONE | 当前 checkpoint、条目、证据、失败/回退、下一 gate 齐全 |
| STANDARDS-M4-01 | 编写 adoption guide（外部团队 90 天采用路径） | DONE | 可按步骤裁剪；含角色、产物、门禁和止损点 |
| STANDARDS-M4-02 | 编写 localization template（领域映射与填空模板） | DONE | 结构工程示例可替换为其他领域；含契约/工具/工作流映射 |
| STANDARDS-M5-01 | 建立季度复审与 changelog | DONE | 版本规则、复审记录模板、变更历史可追踪 |
| STANDARDS-M5-02 | 建立旧版归档规则 | DONE | Frozen/Deprecated 标准可查、可引用、不删除 |
| STANDARDS-M6-01 | 建立外部反馈日志 | DONE | 反馈来源、问题、处置、回流任务可追踪 |
| STANDARDS-M6-02 | 固化反向评估记录 | DONE | 评估口径、评分、证据、改进项进入受控文档 |

## 当前阻塞

当前没有硬阻塞。以下为已知限制：

- 外部采用反馈尚未收集，M4/M6 的"试点反馈"只能先做模板，不能宣称已验证；
- DOCX 的页面级视觉复核受本机 LibreOffice 预览组件崩溃限制，当前以结构校验为准；
- 本域不是产品域，不产生固件/硬件凭证；需要台架的结论必须回到具体项目验收。

## 完成定义

一个任务标记 `DONE`，必须同时满足：

1. 产物存在且路径明确；
2. 已加入 `docs/standards/README.md` 与 `docs/README.md` 索引（如适用）；
3. `docs_check --strict-names` 与 `doc_regression_check.py` 通过；
4. 相关测试通过（如涉及检查器/白名单）；
5. Task Progress 有对应条目，写清验证命令、结果和证据；
6. 未验证、未试点、未评审的内容不得写成 `DONE`。

## 下一步

`M6` 保持 IN PROGRESS：等 G3 外部试点提供真实反馈后，把反馈分录、处置和回流任务写进反馈手册，再关闭 M6。
本域标准进入维护态；首次季度复审按版本手册在 2027-02-06 执行，复审前不新增 MAJOR 版本。
