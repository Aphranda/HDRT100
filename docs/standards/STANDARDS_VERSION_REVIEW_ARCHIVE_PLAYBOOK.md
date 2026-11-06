# 版本、复审与归档手册

Status: Active
Domain: Standards
Canonical: `docs/standards/STANDARDS_VERSION_REVIEW_ARCHIVE_PLAYBOOK.md`
Related: `docs/standards/STANDARDS_DOMAIN_ARCHITECTURE.md`, `docs/standards/STANDARDS_DOMAIN_TODO.md`, `docs/standards/STANDARDS_TASK_PROGRESS.md`, `docs/standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`, `docs/standards/STANDARDS_ADOPTION_GUIDE_PLAYBOOK.md`, `docs/standards/STANDARDS_LOCALIZATION_KIT_PLAYBOOK.md`, `docs/standards/README.md`
Last updated: 2026-11-06

## 0. 适用范围

本手册定义 `docs/standards/` 下标准的**版本、复审、changelog 与归档**机制，适用于：

- 主标准与专题标准（`STANDARDS_*_ARCHITECTURE.md` 等）；
- 采用指南、本地化工具包、检查清单、playbook；
- 域三件套的版本记录（只记录，不改变三件套的职责边界）。

不适用于产品域契约；产品契约仍按 `DOCS_REGISTRY.md` 和 C11 流程管理。

## 1. 版本规则

采用 `主版本.次版本` 两段式，必要时应加修订号：

```text
MAJOR.MINOR[.PATCH]
```

| 变更类型 | 版本 | 例子 | 评审 |
|---|---|---|---|
| 范围/边界/规范性规则变化 | MAJOR | 六系统改为七系统；删掉一条强制门禁；改变权威顺序 | 独立复核 + owner 批准 |
| 新增章节/模板/示例/裁剪建议 | MINOR | 新增 adoption guide；增加一个领域示例 | owner + 至少一人复核 |
| 措辞、链接、排版、错别字 | PATCH | 修链接、改标题、补标点 | owner 自检即可 |

规则：

- 文档内已有“版本：x.y”的，以文档内为准；没有的，首次发布记为 `1.0`。
- 同一语义变更只升一次版本；不要为每次编辑升 MAJOR。
- `Last updated` 每次实质修改都更新；版本号只随语义/结构变化。
- 版本号与状态独立：`Draft 0.x` 可以存在；`Active 1.0` 是当前推荐版本。
- 旧版本不删除；按 §4 归档。

## 2. 状态与替代

| 状态 | 含义 | 允许动作 |
|---|---|---|
| `Draft` | 草案，未推荐采用 | 修改、试点、评审 |
| `Active` | 当前推荐版本 | 采用、引用、局部修订 |
| `Frozen` | 冻结快照，仅供追溯 | 只读；不修改 |
| `Deprecated` | 不再推荐，保留替代指针 | 只读；迁移到替代版本 |

`Active` → `Deprecated` 必须写：

- 替代文档/版本；
- 迁移方式；
- 停止推荐日期；
- 未迁移项目的风险。

## 3. 复审机制

### 3.1 节奏

- **季度复审**：每季度一次；当前基线首次复审日期 **2027-02-06**。
- **事件触发**：外部采用反馈、重大版本变化、标准与项目实践明显漂移、采用失败、法规/合同变化。

### 3.2 角色

| 角色 | 职责 |
|---|---|
| Owner | 组织复审、决定是否升版/废弃 |
| 作者 | 提供变更说明、影响分析、证据 |
| 独立复核者 | 只读检查范围、边界、可执行性、项目耦合 |
| 外部采用者 | 提供试点反馈、裁剪记录、失败案例 |

### 3.3 复审清单

- [ ] 范围与非目标仍然成立？
- [ ] 与项目实践/外部反馈是否出现明显偏差？
- [ ] 是否有未标注的硬数字或过时快照？
- [ ] 模板是否仍可执行？示例是否仍准确？
- [ ] 是否与主标准/其他标准重复或冲突？
- [ ] 索引、链接、Related 是否有效？
- [ ] 是否需要升版、废弃或归档？
- [ ] 是否有未闭环的外部反馈？

### 3.4 复审记录

| 日期 | 范围 | 复核人 | 结论 | 必改项 | 复审状态 |
|---|---|---|---|---|---|
| `<YYYY-MM-DD>` | `<文档/条款>` | `<name>` | `<PASS / PASS_WITH_CHANGES / REVISE / ARCHIVE>` | `<...>` | `<OPEN/CLOSED>` |

结论含义：

- `PASS`：无需修改，保持版本；
- `PASS_WITH_CHANGES`：小修，升 PATCH/MINOR；
- `REVISE`：范围/规则需要重写，升 MAJOR；
- `ARCHIVE`：不再推荐，转 `Frozen/Deprecated` 并归档。

## 4. Changelog

### 4.1 规则

- Changelog 只追加，不覆盖历史；最新版本在最上面。
- 每条记录：日期、文档/版本、变更类型、摘要、作者、证据/评审。
- 变更类型与 §1 一致：`MAJOR` / `MINOR` / `PATCH` / `STATUS`。
- 不在 changelog 里写未评审的草案；草案只在进度日志记录。

### 4.2 当前 changelog

| 日期 | 文档 | 版本 | 类型 | 摘要 | 作者 | 证据 |
|---|---|---|---|---|---|---|
| 2026-11-06 | `STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md` | 1.0 | MAJOR | 首次发布：六系统、模板、路线图、结构工程适配 | 董力 | 文档门禁 + DOCX 结构校验 |
| 2026-11-06 | `STANDARDS_ADOPTION_GUIDE_PLAYBOOK.md` | 1.0 | MAJOR | 90 天采用路线、角色、门禁、裁剪、止损 | 董力 | 文档门禁 |
| 2026-11-06 | `STANDARDS_LOCALIZATION_KIT_PLAYBOOK.md` | 1.0 | MAJOR | 六系统映射、契约/工具/WorkflowSpec、验收凭证模板 | 董力 | 文档门禁 |
| 2026-11-06 | `STANDARDS_DOMAIN_ARCHITECTURE.md` | 1.0 | MAJOR | 域定位、边界、文档模型、生命周期 | 董力 | 文档门禁 |
| 2026-11-06 | `STANDARDS_DOMAIN_TODO.md` | 1.0 | MAJOR | 里程碑、任务状态、完成定义 | 董力 | 文档门禁 |
| 2026-11-06 | `STANDARDS_TASK_PROGRESS.md` | 1.0 | MAJOR | 证据日志、当前 checkpoint | 董力 | 文档门禁 |

> 版本号是本文档记录的管理版本，不等同于项目契约版本或产品版本。

## 5. 归档规则

### 5.1 何时归档

- 发布新 MAJOR 版本，旧版本需要保留追溯；
- 标准转 `Deprecated`，且已有替代版本；
- 标准被拆分/合并，原文件不再维护；
- 外部采用者需要引用旧版做对照。

### 5.2 归档位置与命名

```text
docs/legacy/standards/
  LEGACY_<ORIGINAL_STEM>_<YYYYMMDD>.md
  README.md（首次归档时建立）
```

示例：

```text
docs/legacy/standards/ 下的
LEGACY_STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE_20270206.md
```

规则：

- 归档文件保留原文，不修改正文；补 5 字段元数据，`Status: Frozen`；
- 文件名符合仓库命名规则；`LEGACY` 前缀 + 原主题 + 日期后缀；
- 归档文件在总索引加索引；在 legacy 目录的 `README.md` 记录来源、替代版本、归档原因；
- 归档不删除、不改写；不把归档文件作为当前标准引用；
- 当前标准在 `Related` 或正文中给出“已归档版本”指针。

### 5.3 归档清单

- [ ] 旧版本完整复制，字节未改；
- [ ] 元数据 `Status: Frozen`；
- [ ] 归档路径/命名合规；
- [ ] 总索引与 legacy README 已登记；
- [ ] 当前版本给出替代指针；
- [ ] Task Progress 记录归档原因和证据。

## 6. 失效与迁移

1. 将当前文档 `Status` 改为 `Deprecated`；
2. 写替代文档/版本、迁移步骤、停止推荐日期；
3. 保留至少一个季度的过渡期；
4. 归档旧版本；
5. 更新所有索引和引用；
6. 在 Task Progress 记录“为什么废弃、迁移到哪、谁还在用”。

## 7. 度量

| 指标 | 口径 | 目标 |
|---|---|---|
| 复审按时率 | 按时完成的季度复审 / 应复审数 | 100% |
| 标准陈旧率 | 超过复审周期未复审的 Active 文档 / Active 总数 | 0 |
| 归档覆盖率 | 已归档 MAJOR 旧版 / 应归档数 | 100% |
| Changelog 滞后 | 已发布但未记录的变更 | 0 |
| 外部反馈闭环 | 已关闭反馈 / 收到反馈 | ≥80% |

## 8. 当前基线

- 主标准、adoption guide、localization kit、域三件套：`1.0`；
- 首次季度复审：`2027-02-06`；
- 当前无归档版本；首次归档时建立 legacy 目录的 `README.md`；
- 外部反馈尚未收集；M6 之前不把“未试点”写成“已验证”。

## 9. 模板

### 9.1 复审记录

```markdown
## 复审记录

- 日期：
- 范围：
- 复核人：
- 结论：PASS / PASS_WITH_CHANGES / REVISE / ARCHIVE
- 必改项：
- 复审状态：OPEN / CLOSED
- 证据：
```

### 9.2 Changelog 条目

```markdown
| YYYY-MM-DD | <文档> | <x.y> | MAJOR/MINOR/PATCH/STATUS | <摘要> | <作者> | <证据> |
```

### 9.3 归档头

```markdown
# <原标题>（归档）

Status: Frozen
Domain: Standards
Canonical: `<archive-path>/<file>.md`
Related: `<当前版本>`, `<替代文档>`
Last updated: YYYY-MM-DD

> 本文件是 <原版本/日期> 的冻结快照，仅用于追溯；当前标准见 <替代文档>。
```

## 10. 变更接口

- 本手册自身的 MAJOR/MINOR 变更，按 §1/§3 执行；
- 新增标准时，在 §4 changelog 增加首条记录；
- 归档时，按 §5 执行并更新索引；
- 复审完成后，把结论写回本手册 §3.4 或对应 Task Progress；
- 本手册不替代 `DOCS_REGISTRY.md` 的产品契约流程。
