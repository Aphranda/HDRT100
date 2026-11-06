# 通用规范域架构

Status: Active
Domain: Standards
Canonical: `docs/standards/STANDARDS_DOMAIN_ARCHITECTURE.md`
Related: `docs/standards/README.md`, `docs/standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`, `docs/standards/STANDARDS_DOMAIN_TODO.md`, `docs/standards/STANDARDS_TASK_PROGRESS.md`, `docs/README.md`
Last updated: 2026-11-06

## 文档接口与范围

本文定义 `docs/standards/` 的稳定语义：这个域为什么存在、边界在哪、文件如何组织、标准如何编写和评审。  
任务状态只在 [TODO](STANDARDS_DOMAIN_TODO.md)，实施与验证证据只在 [Task Progress](STANDARDS_TASK_PROGRESS.md)；本文不记录单次执行状态。

本域是**通用方法域**，不是产品域。它不冻结 HDRT100 的 wire、时序、容量、SCPI 或验收判据；引用本域内容的项目必须把适用条款本地化，并登记到自己的契约/规范体系。

## 1. 域定位

### 1.1 为什么需要独立域

项目实践中已经形成文档治理、契约、工具、Skill、工作流和证据体系。把这些方法直接留在产品文档里，会产生三个问题：

1. 通用方法被产品细节淹没，无法交付给其他项目；
2. 外部项目照抄产品契约，造成错误耦合；
3. 方法随项目演进而漂移，却没有独立的版本和评审入口。

独立 `standards/` 域的作用，是把"可复用的方法"从"本项目的具体事实"中剥离出来。

### 1.2 范围

- 跨项目可复用的治理标准、方法论、模板、检查清单、落地路线；
- 从项目实践中抽象出的模式、反模式和度量；
- 面向外部团队的 adoption guide、localization kit 和反馈闭环；
- 标准自身的版本、评审、归档和失效管理。

### 1.3 非目标

- 不定义 HDRT100 的产品契约、运行时约束或验收判据；
- 不替代具体项目的安全规范、法规、合同和签章责任；
- 不提供"放之四海而皆准"的固定流程；默认要求裁剪；
- 不收录项目单次 build、HIL 结果、板端日志或临时结论。

## 2. 边界与权威关系

权威顺序：

```text
项目安全要求 / 法规 / 用户当前指令
        >
项目章程 / AGENTS.md / 治理硬约束
        >
契约登记表 / 运行时约束
        >
项目域 canonical 文档
        >
通用规范（本域）
        >
外部文章 / 个人笔记
```

- 在 HDRT100 内，本域是**参考方法**，不是产品事实源；冲突时以项目规则为准。
- 在外部项目内，本域是**建议标准**；采用时必须转成本项目的 canonical 文档和契约，不能直接引用为项目事实。
- 本域不复制 `docs/check/` 的文档门禁规则、`docs/tools/` 的工具治理规则或 `docs/evaluation/` 的价值评估结论，只引用其概念并保持独立版本。

## 3. Owner 与角色

| 角色 | 职责 | 边界 |
|---|---|---|
| 域 owner | 维护域结构、标准版本、评审节奏 | 不替代项目 owner 做产品决策 |
| 作者 | 编写标准、模板、示例 | 不给自己发"已采纳"结论 |
| 独立复核者 | 检查范围、边界、可执行性、项目耦合 | 只读，不改文件 |
| 外部采用者 | 本地化、试点、反馈 | 不把通用规范直接当项目契约 |

本域当前 owner：董力（`mr.aphranda@gmail.com`）。  
涉及"某项目已采纳/已生效"的声明，必须有该项目自己的登记和证据，不能由本域单方面宣布。

## 4. 文档模型

| 文件 | 唯一职责 | 禁止内容 |
|---|---|---|
| `README.md` | 域入口、文件索引、适用边界 | 单次进度、未评审结论 |
| `STANDARDS_DOMAIN_ARCHITECTURE.md` | 域定位、边界、文档模型、生命周期 | 任务状态、单次验证结果 |
| `STANDARDS_DOMAIN_TODO.md` | 里程碑、任务状态、退出门禁 | 复制证据、替代架构语义 |
| `STANDARDS_TASK_PROGRESS.md` | 交付、验证、失败、证据 | 冻结新标准、替代 TODO 状态 |
| `STANDARDS_<TOPIC>_ARCHITECTURE.md` | 某通用标准/方法的稳定正文 | 项目单次快照、未标注的硬数字 |
| `STANDARDS_<TOPIC>_<TYPE>.md` | 专题标准、playbook、checklist、evaluation | 与主标准重复的第二事实源 |
| `STANDARDS_<TOPIC>_ARCHITECTURE.docx` | 排版交付版 | 作为唯一事实源（以 Markdown 为准） |

命名规则：

```text
STANDARDS_<TOPIC>_<TYPE>.md
```

- 前缀固定 `STANDARDS`；后缀使用仓库白名单（`ARCHITECTURE`、`TODO`、`TASK_PROGRESS`、`PLAYBOOK`、`CHECKLIST`、`EVALUATION` 等）。
- 文件名 ASCII、全大写、下划线分隔；新文件不使用中文名、日期、`final`、`new`、`v2`。
- 一个文件只承担一种主职责；主标准正文与域三件套分离。
- 新增文件必须更新 `docs/standards/README.md` 和 `docs/README.md` 索引。

## 5. 标准生命周期

```text
Draft（草案）
   │ 范围/边界/模板齐全，完成一次内部评审
   ▼
Active（可交付）
   │ 外部试点或项目内使用，收集反馈
   ▼
Frozen（冻结版本）
   │ 被新版本替代 / 不再维护
   ▼
Deprecated（保留引用，不再推荐）
```

规则：

- `Status` 只用 `Draft` / `Active` / `Frozen` / `Deprecated`，不发明近义词。
- 语义变化升主版本，措辞/示例调整升次版本；版本号写在正文显著位置。
- 旧版本不删除，归档到 `docs/legacy/standards/` 或保留 `Frozen` 快照。
- 每次实质修改更新 `Last updated`，并在 Task Progress 记录变更与验证。
- Active 标准至少每年复审一次；外部反馈集中时提前复审。
- 版本、复审、changelog 与归档的操作细则见 `STANDARDS_VERSION_REVIEW_ARCHIVE_PLAYBOOK.md`。

## 6. 标准编写规则

1. **问题先行**：先说清要解决什么失败模式，再给原则和模板。
2. **范围与非目标并列**：没有非目标的标准一定会被过度套用。
3. **规范性与示例分离**：用"必须/应该/可以"区分强制程度；示例标注"示例，非强制"。
4. **模板可执行**：给字段、表格、命令、检查清单，不给空泛口号。
5. **不写未登记硬数字**：数字必须能追到源，或标注"快照，非事实源"。
6. **项目细节只进附录**：主标准保持领域中立；具体案例放附录并标明来源。
7. **无秘密**：不写密钥、token、个人绝对路径、客户敏感信息。
8. **可评审**：每个标准有 owner、版本、复审周期和变更接口。
9. **可裁剪**：按团队规模、项目类型给出裁剪建议，不把"全量"当默认。
10. **可验证**：至少给出文档门禁、结构检查或 adoption 反馈中的一种验证方式。

## 7. 与项目实践的关系

HDRT100 是本域的主要来源案例。关系是：

```text
项目实践 → 抽象 → 通用标准 → 外部项目本地化 → 反馈 → 标准修订
```

- 主标准附录可以保留 HDRT100 映射，但必须标注"来源说明，非通用要求"。
- 不把 HDRT100 的 `contract_id`、build 号、P3 凭证、板端日志写进主标准正文。
- 外部项目采用时，应生成自己的 `AGENTS.md`、契约登记表、工具登记表和验收凭证；本域只提供模板。
- 本域文件不登记为 `DOCS_REGISTRY.md` 的产品契约。

## 8. 失败与恢复

| 失败模式 | 表现 | 恢复 |
|---|---|---|
| 标准漂移 | 主标准与项目实践脱节 | 复审、修订、在 Task Progress 记录差异 |
| 过度泛化 | 只剩原则，无法执行 | 补模板、命令、检查清单和案例 |
| 产品耦合 | 外部项目照抄产品契约 | 把产品细节移入附录，主标准去具体化 |
| 双源重复 | 同一规则在多个标准各写一份 | 指定唯一正文，其他改引用 |
| 版本失控 | 旧版仍被引用 | 标记 Frozen/Deprecated，保留归档和替代指针 |
| 交付件过期 | DOCX 与 Markdown 不一致 | 以 Markdown 为准，重新生成并记录 |

## 9. 验证映射

| 验证 | 对象 | 是否需台架 |
|---|---|---|
| `docs_check.py --strict-names` | 元数据、索引、命名、链接 | 否 |
| `doc_regression_check.py` | 新鲜度、契约登记、条款落点、进度轮转 | 否 |
| `pytest tests/python/test_docs_check.py` | 命名白名单与检查器行为 | 否 |
| DOCX 结构校验 | 标题、表格、目录域、署名/签署 | 否 |
| 外部 adoption 反馈 | 可读性、可裁剪性、落地成本 | 否 |

反馈与评估的记录、分诊与回流细则见 `STANDARDS_FEEDBACK_EVALUATION_PLAYBOOK.md`。

本域不产生固件/硬件凭证，**不需要台架**。只有把标准应用到具体项目并触发固件/硬件改动时，才按该项目凭证路由执行。

## 10. 变更接口

### 新增标准

1. 在 `docs/standards/` 建 `<STANDARDS>_<TOPIC>_<TYPE>.md`；
2. 写 5 字段元数据、范围、非目标、正文、模板、验证；
3. 更新 `docs/standards/README.md` 与 `docs/README.md` 索引；
4. 在 TODO 建任务、在 Task Progress 记证据；
5. 跑文档门禁。

### 修改标准

1. 判断是语义变更还是措辞变更；
2. 语义变更升主版本，更新 Related/影响说明；
3. 更新 `Last updated`；
4. 在 Task Progress 记录变更、验证和残余；
5. 需要时重新生成 DOCX。

### 废弃标准

1. `Status` 改 `Deprecated`，保留正文和替代指针；
2. 更新 README/索引，不物理删除；
3. 归档旧版本，Task Progress 记录原因和替代关系。
