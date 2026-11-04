# Agent 常设命令（Codex / Claude / AI 助手）

Last updated: 2026-11-04

本仓库采用「文档自回归体系」治理文档。**任何 AI 助手（Codex / Claude / DSH）在本仓库工作前，必须先阅读 §0 理解该体系，再动手改文档。** 违反体系约束会被 git pre-commit 门禁拦截。

## 0. 先读（onboarding，改文档前必须）

| 文件 | 内容 |
|---|---|
| `docs/check/DOCS_REGRESSION_PLAN.md` | 体系规格：冻结契约、硬约束、交付物、迁移阶段 |
| `docs/check/DOCS_REGISTRY.md` | **契约登记表 + 条款落点表**（唯一事实源，检查器读取） |
| `docs/check/DOCS_REGRESSION_REVIEW.md` | 实施经验：常见坑与解法 |
| `docs/check/DOCS_REGRESSION_TODO.md` | 待办状态（哪些已完成、哪些下一期） |
| `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` | 跨 worker/agent 的长期执行约束、证据闭环和主控复核规则 |

> 长期目标/多里程碑任务：先读 §8，并在第一次汇报时套用固定模板；未套用视为流程未启动。
> 模板正文在 `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` §7（7.1–7.4），§8 只留触发与红线。

## 1. 体系是什么（30 秒版）

文档自回归 + 门禁 + 交叉审核（环编号沿用方案：1 新鲜度 / 2 登记 / 3 数字单一来源 / 5 轮转；环 4 未立项）：

```
环1 新鲜度: 顶层文档(HAOFV_ARCHITECTURE.md) 7 天内必须反映域文档新冻结的契约，否则检查器 FAIL
环2 登记:   域文档冻结跨域契约 → 必须在 DOCS_REGISTRY.md 登记（contract_id 唯一，锚点真实）
环3 数字:   文档不得手写 #define 数字（`--constants` 校验，快照须标注）
环5 轮转:   进展日志(*_TASK_PROGRESS.md) ≤200KB，最旧优先归档，归档索引闭包（C14）
            条目按新鲜度倒排——最新鲜的在最上面（C15）
门禁:       git pre-commit 自动跑两个检查器，任一 FAIL 阻断提交
交叉审核:   登记表状态变更禁止自审自批（C11）
```

长期执行约束、状态机迁移闭环、Debug 安全门禁和 worker/主控边界统一维护在
`docs/check/DOCS_EXECUTION_CONSTRAINTS.md`；本文件只保留 onboarding、最短门禁和仓库级硬规则。

## 2. 硬约束（违反 = 门禁 FAIL，不可绕过）

1. 文档 5 字段元数据（Status/Domain/Canonical/Related/Last updated）必须在标题后 **7 行内**
2. 文档硬数字**不手写**：引用代码符号、登记、或标注"快照，非事实源"
3. 新增任何 .md → 必须：命名合规（`<PREFIX>_<TOPIC>_<SUFFIX>.md`）+ 在 `docs/README.md` 加索引行
4. 登记契约不可物理删除（只能 `superseded`）
5. 修改任何文档 → 必须更新 `Last updated`
6. 临时内容（评审快照/草稿）**不得**登记为契约
7. 提交前的验收凭证按**改动类别**分流。分类与指纹域的唯一事实源是
   `tools/acceptance/credential_scope.py`（**不要在本文件再抄一份目录清单**）：
   运行 `python tools/acceptance/credential_scope.py --explain` 看当前分流表。
   - **固件 / 构建**（生产代码与构建文件；由 `tools/hardware_acceptance/source_scope.py`
     判定）→ P3 硬件凭证。常规迭代用 `workflow_steps.py dpll60`（scope `DPLL60_JOINT`）；
     命中 `DPLL_SHORT_UNCOVERED_PREFIXES`（校准/Flash/OTA/storage/sync 等）或 §2.11 冻结时，
     必须跑 `python tools/hardware_acceptance/workflow_steps.py run`（37 个步骤函数，可
     `--run <dir>` 续跑）。pre-commit 用 `check-staged` 按 staged 源码指纹核验；
     **接受哪些凭证档位以 `check-staged` 的 `ACCEPTED_RECEIPT_SCHEMAS` 为准**。
   - **测试 / 工具 / 门禁**（`tests/`、`.githooks/`、`tools/`）→ host 凭证：提交前执行
     `python tools/acceptance/host_acceptance.py run`，写
     `config/hardware_acceptance/host_acceptance_receipt.json`；pre-commit 用
     `check-staged` 按 staged host 指纹核验（只校验指纹与 receipt，**不重跑**用例；
     用例由 `run` 执行）。
   - **文档**（`docs/`、`*.md`）→ 只跑 §5 文档门禁，不需要验收凭证。
   混合改动按各自类别分别满足；旧凭证、手工报告和 replay 一律不能放行。
   - **迭代提速（用户要求，2026-11-02）**：若本轮**没有固件改动**（四板已跑同一 build），验收工作流加
     --reuse-flashed 跳过构建（步 2）与刷写（步 4），直接进校准/TDMA/DPLL；只有源码指纹变化时才重新构建刷写。
8. `*_TASK_PROGRESS.md` 单文件 ≤200KB（C14）；超标必须把**最旧**条目连续归档到
   `docs/legacy/<domain>/LEGACY_<DOMAIN>_TASK_PROGRESS_<NN>.md`，并在规范文件维护
   `## 归档索引`（ID 闭包、条目数相符）；归档只搬旧证据，禁止丢证据
9. `*_TASK_PROGRESS.md` 条目按新鲜度**倒排**——**最新鲜的在最上面**（C15）：日期序列必须
   非递增（同日内部顺序不限），新条目写在 `## 当前 checkpoint` 之后，归档文件同样倒排
10. **禁止回滚、禁止删除（最高优先级）**：不得对工作区执行 `git checkout --`、`git reset --hard`、
    `git restore`、`rm`/`git rm`、`Remove-Item` 等会丢弃已有工作的操作；确需撤销/移除时，
   **只能归档**——移动到对应域的归档目录并写明理由与时间戳，或先以 `git stash create` +
   `refs/wip/<tag>` 做完整快照。任何撤销类操作必须先取得用户明确同意，且可随时完整恢复
11. **未提交改动闸门（积压上限 40）**：`git status --porcelain` 的待提交条目（被跟踪文件的
   修改/删除/重命名 + 未被忽略的新建源码/文档；`out/`、`artifacts/`、`build/` 下的运行产物
   不计）**超过 40** 时，立即冻结新增改动，先对当前工作树做一次完整硬件验收：
   `python tools/hardware_acceptance/hardware_acceptance_workflow.py --profile full`
   （等价 `python tools/hardware_acceptance/workflow_steps.py run`），凭证必须覆盖
   当前源码指纹且 scope 为 FULL。验收未通过前**不得叠加新改动**（先归因，再决定修还是
   按 §2.10 归档回退）；通过后按 §2.7 分域提交收口——P3 凭证绑定 staged 源码指纹，
   `check-staged` 要求工作区无未暂存源码差异，因此同一凭证覆盖的改动应作为**一个**代码
   提交落地，拆批即需重新验收。目的：控制改动面，避免积重难返。计数示例：
   `(git status --porcelain | Where-Object { $_ -notmatch '^\?\? (out|artifacts|build)/' }).Count`
   **临时豁免（2026-10-30，用户授权）——已失效（2026-11-02 到期，2026-11-04 记账）**：
   并行会话的 NO.1 工厂刷写 + COM5 stream OTA 任务（2026-10-30 14:57 起，证据
   `out/ota_workflow/run-20`）的冻结豁免按原文约定"到期即自动失效"，自 2026-11-02 起不再有效；
   该任务若仍需落地，按 §2.11 正常流程处理（先 FULL 验收，再单批提交）。原豁免范围仅覆盖该任务
   自身改动与其提交批次，不覆盖新开任务，也不覆盖其它会话追加的无关改动；§2.10 禁回滚/禁删除
   始终有效。
   **临时豁免（2026-11-02，用户授权）**：为**修复硬件验收本身**而新增/修改的文件（例如 P3 FULL 阻塞的
   归因与修复：相位预算/DPLL 调度、验收工具与证据脚本、对应文档与进度日志）**不受本条冻结约束**。
   范围仅限“让 `hardware_acceptance_workflow.py --profile full` 跑通”这一目标所需的改动；与验收修复
   无关的功能开发、重构、清理仍受冻结。§2.10 禁回滚/禁删除仍然有效。豁免在拿到覆盖当前源码指纹的
   FULL 凭证、并把积压按 §2.7 单批提交后自动失效；届时本条改写为 expired 并记录提交号。
   **临时豁免（2026-11-03，用户授权）**：为**修复 BUG** 而新增/修改的文件（缺陷/故障/卡死超时/数据损坏/
   fail-closed 误触发等导致的现场或验收阻塞，含其最小回归用例与进度日志记录）**不受本条冻结约束**。
   目的：打破“BUG 未修 → 验收不通过 → 积压无法提交 → 新改动被冻结 → BUG 无法修”的死锁。
   范围仅限该 BUG 的定位与修复（含必要的只读取证），与 BUG 无关的功能开发、重构、清理仍受冻结；
   §2.10 禁回滚/禁删除仍然有效。豁免有效期**一周**（至 **2026-11-10** 止），到期未收口即自动失效；
   提前收口时在该 BUG 修复完成、拿到覆盖当前源码指纹的 FULL 凭证、并按 §2.7 单批提交后失效——
   届时本条改写为 expired 并记录提交号，不得静默保留。

**文档格式范围**：文档语料为 `docs/**`。`.agents/skills/**` 下的 `SKILL.md`、配套说明与评审文本
不属于该语料，不适用第 1、3、5 项的五字段元数据、文档命名、总索引和 `Last updated` 格式要求。
这些文件仍由 Git 跟踪与独立评审治理：`SKILL.md` 使用 skill frontmatter/结构，评审文本使用已约定的回合格式。
环境提供的外部 skill 校验器用于辅助检查，不作为仓库硬门禁。本例外仅涉及文本格式，不豁免安全、授权、
契约或实现验收义务；skill 附带的工具、测试和 hook 等可执行实现仍按第 7 项处理。
本文件及文档治理实现的变更继续执行 §5 文档门禁，不因位于 `docs/**` 之外而跳过。

## 3. 修改文档的标准流程

1. 冻结契约 → `docs/check/DOCS_REGISTRY.md` 加一行（contract_id 唯一，clause_loc/code_anchor 必须真实存在）
2. 改文档 → 更新 Last updated；数字引用符号
3. 验证 → 跑 §5 全部命令，必须全绿
4. 提交 → pre-commit 自动跑（FAIL 先修）

## 4. 常见坑（来自实施经验，DOCS_REGRESSION_REVIEW.md）

- **元数据窗口**：额外字段（Target/Source of truth 等）会挤掉 Required 字段——5 个 Required 放最前
- **code_anchor 必须是真实文件**：方案猜测的文件名往往不存在，登记前先确认（曾踩坑：tdma_ring_profile.h 实际叫 tdma_profile.h）
- **检查器改签名 → 同步改测试**，否则 pytest 立刻红
- **改完 `tools/doc_regression_check.py` → 必须 `python tools/doc_regression_check.py --skill-sync`**（同步到 `.agents/skills/doc-self-regression/`；harness 副本仅在 `DOC_SKILL_HARNESS_DIR` 指向真实目录时同步，检查器自检）
- **日期**：`Last updated` 必须 `YYYY-MM-DD` 且合法（非法日期触发 FAIL）

## 5. 验证命令（改文档后必跑）

```powershell
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py
python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider
sh .githooks/pre-commit
python tools/doc_regression_check.py --log-check   # 逃生门审计（建议每周）
python tools/hardware_acceptance/workflow_steps.py check-staged
```

> **门禁接线**：pre-commit 依赖 `git config core.hooksPath .githooks`（本地配置，clone/迁移后**必须重配**，否则 commit 不触发检查器）。`sh .githooks/pre-commit` 可手动验证。

## 6. 当前状态与进行中任务

- 已登记契约：**条数不在此手写**（硬约束 2），以 `docs/check/DOCS_REGISTRY.md` 为准；检查器每次门禁校验 id 唯一与锚点真实
- 条款落点：顶层硬约束全覆盖，逐条状态（OK/PARTIAL/VIOLATED/PENDING）见登记表「条款落点表」
- **已完成**：顶层 `docs/arch/HAOFV_ARCHITECTURE.md` 已刷新至最新 Version（2026-11-03，版本号见其元数据）；2026-08-24 审查修复（门禁接线 / 路径修正 / T18 闭环 / 顶层补 DOCS-FLASH-01）；2026-09-17 新增环5（C14 轮转 + C15 倒排）并轮转 VDC 进展日志；2026-11-04 约束收敛（tools/ 归 host、契约 id 全量校验、T2 clause_loc 债务可见）
- 下一期：verify-doc-crosscheck 自动化强化；TDMA/RefMem/HAOFV_FLASH 进展日志轮转（见 TODO 与 C14 债务基线）

## 7. 提交约定

- 文档/契约变更与代码变更**分离提交**，便于 `git log` 审计
- 提交信息含契约变更时带 `contract_id`（如 `docs(tdma): 登记 TDMA-REASON-01`）

## 8. 长期目标里程碑汇报约束（核心）

> 六段模板（0–6）、硬规则与阻塞点交叉分析的**唯一正文**已迁到
> `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` §7（7.1–7.4）。本文件只保留触发与红线，
> 不再维护第四份副本；`refmem-milestone-report` skill 同样只保留触发。

- **触发**：任何长期目标/多里程碑任务（N3.x/N4.x 等）的第一次汇报前，必须声明
  “本次按 §8 模板汇报”；未声明视为流程未启动。非目标一次性任务不触发。
- **第 0 段先行**：续轮逐条复议上轮第 6 段“下一步”；新目标首轮写“无（首轮）”+ 计划来源。
  缺失、只有复述没有前提核验、或用上轮结论充当证据 → 本轮流程未启动，不得开新改动、不得标完成。
- **三条红线**：任一门禁 ❌/未通过不得进入下一子项；C1/C2 不成立直接 ❌ 不得降 ⚠️；
  `pending` 契约、未验证项、延后项不得写成 ✅。
- **证据**：host 用例、四板 HIL、P3 凭证路径必须可复跑，凭证写明 scope/build/源码指纹/未覆盖路径。
- **阻塞点**：命中 §7.4 判定（连续 2 次失败、现象复现 ≥2、前提与上轮不一致、不可逆决策、
  长循环 ≥3 轮）时，先开只读子代理独立取证，交叉分析并记录采信理由后才允许继续。
