# 文档自回归体系 — 实施规格（v3，归档版）

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/DOCS_REGRESSION_PLAN.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/check/DOCS_REGRESSION_TODO.md`
Last updated: 2026-11-04

> 本文件由工作区 `doc-skill/方案_文档自回归体系.md` 归档而来；执行状态与最终调整见 `docs/check/DOCS_REGRESSION_TODO.md`。
> 设计依据一句话：需求追溯矩阵思想（契约登记）+ docs-as-code 门禁（commit 拦截）。

## 交付物总览

| # | 文件 | 内容 | 位置 |
|---|---|---|---|
| 1 | `DOCS_REGISTRY.md` | 契约登记表 + 条款落点表（含 5 条真实契约） | `docs/check/` |
| 2 | `doc_regression_check.py` | 环1 新鲜度 + 环2 登记，纯 stdlib | `tools/` |
| 3 | `test_doc_regression.py` | pytest 包装 | `tests/python/` |
| 4 | `pre-commit` | 硬门禁 | `.githooks/` |
| 5 | `SKILL.md` | skill 定义（frontmatter 触发，agent 自动加载） | `.agents/skills/doc-self-regression/` |
| 6 | `AGENTS.md` 文档章节 | 常设命令（Codex/agent 每次会话可读） | 项目根 |

## 设计来源：Harness 文档治理模式（精髓映射）

本方案不是自创，而是把 DeepSeek Harness 自身文档治理的成熟模式映射到本项目：

| DSH 模式 | 本项目落地 |
|---|---|
| **verify-\* 门禁**（`verify-doc-budgets` / `verify-md-links` 命名 + 自动运行） | pre-commit + pytest 运行 `verify-doc-registry` / `verify-doc-freshness`（检查器即门禁，命名同风格） |
| **SKILL.md**（YAML frontmatter 的 name/description 触发加载） | `.agents/skills/doc-self-regression/SKILL.md`，Codex/Claude/DSH 识别同类格式 |
| **AGENTS.md 常设命令**（根文档，agent 每次会话必读） | 根 `AGENTS.md` 增加文档维护章节 |
| **Agent Note**（header 块 + Alternatives considered） | 核验提交单采用同格式（见 §2.6） |
| **change-scope**（只审计变更，不全量扫描） | 检查器支持 `--scope <git diff 文件>`，pre-commit 只查本次变更涉及的文档 |
| **归档不删除**（archived 冻结快照） | 契约 `superseded` 移入归档区，禁止物理删行（C4） |
| **skill 自回归测试**（evolveguard：skill 改动要跑自己的测试） | `test_doc_regression.py` 同时校验"登记表格式 ↔ 检查器假设"一致性 |

## §0 硬约束（优先级最高，冲突时以本节为准）

任何交付物、文档、代码或后续变更与本节冲突时，以本节为准，并同步修正下级内容。

| # | 约束 | 规则 | 机检 |
|---|---|---|---|
| C1 | 检查器只读 | `doc_regression_check.py` 只读文档并输出，永不写文件 | 代码审查 |
| C2 | 零依赖 | 纯 Python 标准库；pre-commit 在无网络环境必须可运行 | 离线实测 |
| C3 | 门禁可绕过但留痕 | 允许 `git commit --no-verify`，但绕过必须可被检查器感知（登记表状态/日志） | WARN |
| C4 | 契约不可物理删除 | `contract_id` 一经登记只能 `superseded`，禁止删行 | 环2 校验历史行 |
| C5 | 格式三同步 | 登记表格式 ↔ 检查器 ↔ pytest 用例必须同一次 commit 变更 | pre-commit 比对 |
| C6 | 门禁快速 | pre-commit 内全套检查 ≤ 5 秒 | 实测 |
| C7 | 临时内容不污染 | 评审快照、临时备注、草稿**不得**登记为契约；只有域文档正式冻结的内容可登记（clause_loc 必须指向真实冻结文档） | 环2 + 人工 |
| C8 | 顶层刷新窗口 | 新契约登记后顶层 `HAOFV_ARCHITECTURE.md` 7 天内必须刷新 | 环1 |
| C9 | 可逆 | 每个落地步骤必须可 `git revert`，不产生一次性不可逆改动 | — |
| C10 | 冲突裁决 | 任何下级文档/代码与本节冲突时，以本节为准并同步修正 | — |
| C11 | 状态变更需交叉审核 | 登记表 status 变更（新契约登记、VIOLATED→OK）必须由**独立于作者的审核方**确认（agent 交叉 / 文档交叉 / 层间交叉），提交单记录审核方+方式+结论；禁止自审自批 | 提交单字段必填 |
| C12 | 域文档标准三件套 | 具有架构、实施清单和实施证据的域必须分别维护 Architecture、TODO、Task Progress；语义、状态、证据不得跨文件复制充当事实源 | 文档审查 + DOCS-FLASH-01 |
| C13 | 三件套最小格式 | Architecture、TODO、Task Progress 必须使用本节定义的最小结构、稳定 ID 和文件接口；新增文件立即执行，既有文件在实质修改时迁移，禁止继续扩散无 ID 清单和混合状态格式 | 文档审查 + DOCS-TRIPLETFORMAT-01 |
| C14 | 进展日志轮转 | Task Progress 单文件 ≤200KB；超出部分**最旧优先连续截断**归档到 `docs/legacy/<domain>/LEGACY_<DOMAIN>_TASK_PROGRESS_<NN>.md`；规范文件保留 `## 当前 checkpoint` 与 `## 归档索引`（文件\|ID 区间\|条目数\|归档日期）；归档与规范文件 ID 不得重复、不得有未挂账条目；`Last updated` 不得早于最新条目日期 | 环5 |
| C15 | 进展日志倒排 | Task Progress 条目按新鲜度**倒排**：**最新鲜的在最上面**，日期序列必须非递增（同日内部顺序不限）；归档文件与 `## 归档索引` 区间同向倒排 | 环5 |

约束传递方向（单向收敛）：

```text
域文档正式冻结契约 ──登记──▶ 登记表（上提）──刷新──▶ 顶层感知
评审快照/临时备注 ────────✗ 禁止进入登记表
```

### 域文档标准三件套

| 文件类型 | 唯一职责 | 禁止内容 |
|---|---|---|
| `*_ARCHITECTURE.md` | 稳定语义、不变量、owner 边界、契约落点 | 单次构建号、板端日志、临时完成判断 |
| `*_TODO.md` | 里程碑、子项状态、进入/退出门禁、证据索引 | 复制任务快照、把一次性证据写成架构事实 |
| `*_TASK_PROGRESS.md` | 提交、构建/HIL、报告、失败、回退、阻塞 | 冻结新契约、擅自改变 registry status、替代 TODO 状态 |

三件套必须互相引用；语义变更先更新架构并登记，状态变更更新 TODO，实施证据只追加到任务进度。

### 三件套最小格式

| 文件类型 | 必备结构 | 稳定索引 | 状态/记录格式 |
|---|---|---|---|
| `*_ARCHITECTURE.md` | 文档接口、范围与边界、owner/不变量、数据或状态模型、跨域契约、失败与恢复、验证映射 | 条款 ID 或契约 ID | 只描述稳定语义；不得记录单次 build、HIL 结果或当前执行状态 |
| `*_TODO.md` | 文档接口、状态规则、已有基线、当前主线、里程碑总览、分阶段任务表、当前阻塞项、统一完成定义 | 每个任务使用域内唯一且稳定的 task ID | 状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`；任务表固定为 `ID / 任务 / 状态 / 完成或退出门禁` |
| `*_TASK_PROGRESS.md` | 文档接口、当前 checkpoint、按时间追加的任务记录、验证与证据索引、失败/回退、下一 gate | 每条记录必须引用 TODO task ID，并使用唯一 progress ID | 记录必须包含日期、变更/提交、构建或验证、结果、证据位置和下一步；不得替代 TODO 状态或冻结架构契约 |

格式约束不要求把三份文档写成相同正文；它只统一导航、状态和追溯接口。`TODO` 中不得复制
单次 build/HIL 数值，`TASK_PROGRESS` 中不得自行宣布契约生效，`ARCHITECTURE` 中不得使用
执行清单替代稳定语义。旧文档在未实质修改前允许保持原格式，但一旦重排状态或新增里程碑，
必须在同一文档变更中迁移到本表格式。

### 进展日志轮转（C14 / 环5）

`*_TASK_PROGRESS.md` 是只追加的证据日志，必须可读且有界；轮转只搬移**旧**证据，禁止丢失证据。

**排序约定（倒排）**：条目按新鲜度**倒排**——**最新鲜的在最上面**。新条目追加在文件顶部
（`## 当前 checkpoint` 之后），因此轮转一律从**尾部**连续截取最旧的一段；规范文件、归档文件与
`## 归档索引` 的 ID 区间都按倒排书写（`<最新 ID>..<最旧 ID>`），检查器对区间方向双向容忍。

R2 以**日期**为边界判定：归档条目最大日期 ≤ 规范文件保留条目最小日期。**不强制日内顺序**——
实测 14 份规范日志中 12 份在同一天内用升序块（如 `FLASH-20260823-077 → -078`），只有 VDC 为
严格倒排；按序列单调判定会误伤存量文档。轮转时必须保留每份日志自身的既有风格。

条目 ID 语法：`^#{2,3}\s+<PREFIX>-YYYYMMDD-NNN`，`YYYYMMDD` 必须是合法日历日期；
`YYYYMMDD-NNN` 字面模板（未填充的模板文件）不计入条目。

| # | 规则 | 判定 |
|---|---|---|
| R1 | 规范进展日志单文件 ≤ 200KB（204800 字节） | 超限 FAIL（在登记债务内者 WARN 至截止日） |
| R2 | 归档必须**最旧优先连续截断**：归档条目的最大日期 ≤ 规范文件保留条目的最小日期 | 违反 FAIL |
| R3 | 归档路径 `docs/legacy/<domain>/LEGACY_<DOMAIN>_TASK_PROGRESS_<NN>.md` | 位置/命名不符 FAIL |
| R4 | `## 归档索引` 闭包：列出的文件存在；声明条目数 == 实际条目数；规范 ∪ 归档 ID 无重复；归档中每个 ID 都落在某条索引行的 ID 区间内 | 违反 FAIL |
| R5 | 新鲜度：规范文件保留 `## 当前 checkpoint`；`Last updated` 不得早于最新条目日期 | 违反 FAIL |

保留窗口 90 天为**指导值**（R1 允许时尽量保留），R1 的字节上限优先。实测依据：`VDC_TASK_PROGRESS.md`
176 条全部落在 12 天内已达 668KB，`HAOFV_FLASH_TASK_PROGRESS.md` 4 天即 209KB——**时间窗口不构成
约束，字节上限才是**，故 R2 用"最旧优先连续"而非"按时间裁剪"。

轮转与环1 的关系：`legacy/` 已在 `FRESHNESS_EXCLUDE_DIRS` 内，归档不改变顶层 7 天刷新判定。

轮转债务基线（R1 豁免，带截止日；**只可删除条目，不可新增**）与完整操作规程见
`docs/check/DOCS_PROGRESS_ROTATION_PLAN.md`——该文件是债务基线的**唯一落点**，本文件不复制
条目明细（避免双源漂移）。

### 进展日志倒排（C15 / 环5）

条目按新鲜度**倒排**：**最新鲜的在最上面**。新条目写入文件顶部（`## 当前 checkpoint` 之后），
因此轮转一律从尾部截取最旧的一段；归档文件与 `## 归档索引` 区间同样倒排
（`<最新 ID>..<最旧 ID>`）。

判定按**日期**非递增，**同日内部顺序不限**：实测 14 份规范日志中 12 份在同一天内使用升序块
（如 `FLASH-TASK-20260823-077 → -078`），只有 VDC 为严格倒排；若按序列单调判定会误伤存量文档。
轮转时必须保留每份日志自身的既有日内风格。

日期级倒序基线（带截止日；**只可删除条目，不可新增**）见
`docs/check/DOCS_PROGRESS_ROTATION_PLAN.md` §5.2；存量偏离均为尾部补记或顶部回填造成的单点倒置。

**验收路径**：C14/C15 的机检随**文档门禁**运行，验收定义见
`docs/check/DOCS_EXECUTION_CONSTRAINTS.md` §6（`docs_check --strict-names` +
`doc_regression_check.py` + 对应 pytest + pre-commit，留证 `out/doc-audit/<run-id>/`），
**不走** EXE-CHANGE-01 第 4 步的四板 P3 路径——后者适用对象是固件/PIO/构建/工具/测试的
**功能实现**改动，而文档门禁工具由 §6 单独定义。

## §1 契约登记表

唯一事实源：`docs/check/DOCS_REGISTRY.md`（契约登记表 / 条款落点表 / 状态说明）。本文件不再复制表体——
历史副本已与实现分叉（契约条数、status、id 格式都变过）。登记规则见该文件与 §0。


## §2 检查器

实现唯一事实源：`tools/doc_regression_check.py`。**本文件不再复制源码** —— 历史副本已与实现分叉（旧 `ROW_RE` 只认两段 id，导致 10 行契约长期未被校验；现行为 `<DOMAIN>-<TOPIC>-<NN>` 全量校验）。需要看实现请直接读该文件；skill 模板见 `.agents/skills/doc-self-regression/`。

## §2.5 文档治理 skill

唯一事实源：`.agents/skills/doc-self-regression/SKILL.md`（frontmatter + 触发 + 规则 + 命令）。
本文件不再复制 skill 全文；改 skill 后按 skill 自身流程跑 `--skill-sync`。


## §2.6 核验提交单与 AGENTS 章节

- 提交单模板：`docs/check/submissions/README.md`（复制使用；C11 交叉审核记录必填）。
- AGENTS 文档章节：以 `AGENTS.md` 当前内容为准，本章节不再保留历史快照。


### 根 AGENTS.md 文档章节（Codex 每次会话可读）

```markdown
## 文档维护常设命令（doc-self-regression）
- 冻结跨域契约 → 必须在 docs/check/DOCS_REGISTRY.md 登记（contract_id 唯一）
- 登记后顶层 7 天内刷新；每次文档改动更新 Last updated
- 文档硬数字不手写：引用代码符号，或登记，或标注"快照"
- 提交前必须通过 verify-doc-*（pre-commit 自动执行）
- 冲突处理：以文档治理方案 §0 硬约束为准
```

## §3 自回归测试

唯一事实源：`tests/python/test_doc_regression.py`（含检查器正反用例）。本文件不再复制测试代码。


## §4 门禁钩子

唯一事实源：`.githooks/pre-commit`（按改动类别路由 docs / host / firmware 三类凭证）。本文件不再复制钩子脚本。


## §5 安装与验收

命令唯一事实源：`AGENTS.md` §5（改文档后必跑）+ pre-commit 门禁。安装只需
`git config core.hooksPath .githooks`（clone/迁移后重配）。本文件不再复制命令块。


## §6 本次明确不做

| 不做 | 原因 |
|---|---|
| 环3 数字单一来源检查 | 已实现（`--constants`，T16 于 2026-08-19 完成）；文档手写 `#define` 数字由检查器 FAIL |
| `verify-doc-crosscheck` 自动交叉核验 | 下一期：对登记契约自动比对"域文档描述 vs code_anchor 实际值"；本期交叉审核走提交单人工/agent 流程（C11） |
| 顶层 HAOFV_ARCHITECTURE.md 刷新 | 另行待办（涉及正文修改，单独评审） |
| 逐级核验流程的完整自动化 | 提交单格式本期定义（§2.6），流程依赖登记表跑顺后再启用 |
| CI / pre-commit 框架 / 第三方依赖 | 单人项目 git hook 足够 |

## §7 遗留缺口与处置（审核补充，2026-08-19）

| 缺口 | 处置 | 机检 |
|---|---|---|
| G1 登记表元数据与 docs_check 冲突 | 补 Related 字段（已修，见 §1 表头） | docs_check 自检 |
| G2 孤儿条款无机检 | 环2 扩展：解析顶层硬约束表 vs 落点表比对（T8） | 检查器 |
| G3 检查器性能/误匹配 | 预索引文件清单一次；排除 `build*/`、`.git/`（T9） | 实测 ≤5s |
| G4 GBK 编码崩溃 | read_text 加 errors="ignore" + 编码检测（T9） | 防御 |
| G5 逃生门留痕 | hook 写 `.git/doc-verify.log`；检查器比对 git log 与 hook log 差异 → WARN（T10） | 检查器 |
| G6 提交单归档位置 | `docs/check/submissions/YYYY-MM-DD-<id>.md`（T11） | 目录约定 |
| G7 Draft 文档不可登记 | 环2 校验 clause_loc 文档 Status 必须 Active/Frozen（T8） | 检查器 |
| G8 检查器自身交叉审核 | T12：用独立 agent/人工评审 doc_regression_check.py 代码 | 评审 |
| G9 方案收尾路径 | 执行完成后本方案转 Active 归档入 `docs/check/`（T13） | — |
| G10 SKILL/AGENTS 权威关系 | AGENTS.md=常设命令（权威），SKILL.md=触发工作流并引用 AGENTS（§2.5 已体现） | — |

## §8 待办分解（批准后细化执行序）

```
T1  建 docs/check/DOCS_REGISTRY.md（5 字段元数据 + 5 契约 + 落点表）   [§1, G1]
T2  建 tools/doc_regression_check.py（环1+环2 基础，--scope）          [§2]
T3  建 tests/python/test_doc_regression.py（3 用例）                  [§3]
T4  建 .githooks/pre-commit + git config core.hooksPath                [§4+§5]
T5  建 .agents/skills/doc-self-regression/SKILL.md                     [§2.5]
T6  根 AGENTS.md 追加文档维护章节                                      [§2.6]
T7  全量验收（检查器 / pytest / 门禁三连）                            [§5]
T8  环2 扩展：孤儿条款检查 + Draft 文档禁止登记                        [G2, G7]
T9  检查器加固：预索引 + 排除 build + 编码防御                         [G3, G4]
T10 逃生门留痕：hook 日志 + git log 比对                               [G5]
T11 提交单归档目录 + 模板                                             [G6]
T12 检查器交叉审核（独立评审 doc_regression_check.py）                 [G8]
T13 方案收尾：v3 转 Active 归档入 docs/check/                          [G9]
```

---

## 决策确认记录

- 2026-08-19: 用户认可 §0-§2.6 交付物内容与 pre-commit 门禁
- 2026-08-19: 用户补充需求已并入 — 逐级核验(§2.6)、硬约束(§0)、harness 精髓(映射表)、git 经验(§7)、交叉审核(C11)、遗留缺口(§7 G1-G10)
- 执行入口: §8 待办 T1→T13，逐条执行、每步反馈，T12 检查器交叉审核在 T2 代码完成后进行
