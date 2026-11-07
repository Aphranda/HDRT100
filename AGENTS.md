# Agent 常设命令（Codex / Claude / DSH）

Last updated: 2026-11-07

本仓库用「文档自回归体系」治理文档：**任何 AI 助手动手前先读 §0**，违反约束由 git pre-commit 拦截。

## 0. 先读（改文档前必须）

| 文件 | 内容 |
|---|---|
| `docs/check/DOCS_REGRESSION_PLAN.md` | 体系规格与文档语料硬约束（C1–C15） |
| `docs/check/DOCS_REGISTRY.md` | **契约登记表 + 条款落点表**（唯一事实源，检查器读取） |
| `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` | 执行流程、验收/Debug 门禁、§7 汇报模板正文 |
| `docs/check/DOCS_REGRESSION_REVIEW.md` / `_TODO.md` | 实施经验 / 待办状态 |

> 长期目标/多里程碑任务：先读 §8；模板正文在 `DOCS_EXECUTION_CONSTRAINTS.md` §7（7.1–7.4）。

## 1. 体系（30 秒）

```
环1 顶层文档 7 天内反映新冻结契约，否则 FAIL
环2 冻结跨域契约 → 登记 DOCS_REGISTRY.md（id 唯一、锚点真实）
环3 文档不手写 #define 数字（--constants 校验，快照须标注）
环5 进展日志 ≤200KB、最旧优先归档、索引闭包（C14）；条目倒排（C15）
门禁 pre-commit 跑两个检查器，任一 FAIL 阻断
（环 4 未立项）
```

长期执行约束、状态机迁移闭环、Debug 安全门禁与 worker/主控边界统一在 `DOCS_EXECUTION_CONSTRAINTS.md`；本文件只留 onboarding、最短门禁和仓库级硬规则。

## 2. 硬约束（违反 = 门禁 FAIL，不可绕过）

1. 5 字段元数据（Status/Domain/Canonical/Related/Last updated）在标题后 **7 行内**
2. 硬数字**不手写**：引用代码符号、登记，或标注"快照，非事实源"
3. 新增 .md → 命名合规（`<PREFIX>_<TOPIC>_<SUFFIX>.md`）+ `docs/README.md` 加索引行
4. 登记契约不可物理删除（只能 `superseded`）
5. 修改任何文档 → 更新 `Last updated`
6. 临时内容（评审快照/草稿）**不得**登记为契约
7. 验收凭证按**改动类别**分流，唯一事实源 `tools/acceptance/credential_scope.py`
   （**不要在本文件抄目录清单**；跑 `--explain` 看当前表）：固件/构建 → P3（`workflow_steps.py run`，
   或常规迭代 `dpll60`，档位以 `check-staged` 的 `ACCEPTED_RECEIPT_SCHEMAS` 为准）；
   `tests/`、`tools/`、`.githooks/`、`.agents/**/*.py` → host 凭证（`host_acceptance.py run`；
   `check-staged` 只校验指纹与 receipt，**不重跑**用例）；文档 → 只跑 §5。新增工具须先在
   `tools/tool_workflow_triage.json` 归型（WORKFLOW/STEP/STANDALONE/LIBRARY），未归型不得入库。
8. `*_TASK_PROGRESS.md` ≤200KB（C14）；超出把**最旧**条目连续归档到 `docs/legacy/<domain>/`，
   维护 `## 归档索引`（ID 闭包、条目数相符）；只搬旧证据，禁止丢证据
9. 进展日志条目按新鲜度**倒排**（C15）：日期非递增，新条目写在 `## 当前 checkpoint` 之后
10. **禁止回滚、禁止删除（最高优先级）**：不得 `git checkout --`/`reset --hard`/`restore`/`rm`/`git rm`
    等丢弃工作；确需撤销只能归档或 `git stash create` + `refs/wip/<tag>`，且须用户明确同意
11. **积压上限 40**：`git status --porcelain` 待提交条目（不含 `out/`、`artifacts/`、`build/` 产物）
    超 40 立即冻结新增改动，先跑 `hardware_acceptance_workflow.py --profile full`（等价
    `workflow_steps.py run`，scope FULL）；未通过不得叠加，通过后按 §2.7 单批提交。豁免见
    `docs/check/WAIVERS.json`（机检到期，过期即 FAIL）。
12. **归档资料不可启用（最高优先级）**：`docs/legacy/`、`out/archive/`、任何 `*/archive/` 目录，
    以及标记为 `Status: Frozen`/`Archived` 的代码、脚本、配置、凭证和构建输入，只允许读取、
    审计和引用说明，不得作为默认/普通构建、preset、CMake include、工具自动发现/执行、release
    或验收输入、运行时配置启用。
    - 不得通过复制回 active 路径、修改 include/preset、解除 Frozen/Archived 标记来“启用”；
      需要恢复时必须先提恢复提案，经 C11 独立审核，新建 active 副本，归档原件原样保留。
    - 归档资料不得作为 P3/host 凭证、基线或默认 V1/V2 构建输入；显式 recovery 使用必须记录
      runbook、命令和用户授权，且不得改变默认构建路径。
    - 归档文档本身可读；禁止把其中旧代码、旧 offset、旧配置或旧凭证当作当前 active 事实源。

**文档格式范围**：语料为 `docs/**`；`.agents/skills/**` 的 SKILL.md/说明/评审文本只用 skill 格式，
不适用第 1/3/5 项，但仍受 Git 与独立评审治理。该例外只涉文本格式，不豁免安全/授权/契约/验收义务；
skill 附带的工具、测试、hook 按第 7 项处理。本文件与治理实现的变更仍跑 §5。

## 3. 改文档流程

冻结契约 → 登记 → 改文档并更新 `Last updated` → 跑 §5 全绿 → 提交（pre-commit 自动跑）。
细则与常见坑见 `doc-self-regression` skill。

## 4. 常见坑

- 元数据窗口：额外字段会挤掉 Required——5 个必填放最前；`Last updated` 必须是合法 `YYYY-MM-DD`
- `code_anchor` 必须是真实文件（曾把 `tdma_profile.h` 猜成 `tdma_ring_profile.h`）
- 改检查器 → 同步改测试；改 `doc_regression_check.py` → 跑 `--skill-sync`

## 5. 验证命令（改文档后必跑）

```powershell
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py
python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider
sh .githooks/pre-commit
python tools/doc_regression_check.py --log-check   # 每周逃生门审计
python tools/hardware_acceptance/workflow_steps.py check-staged
```

> 门禁接线：pre-commit 依赖 `git config core.hooksPath .githooks`（本地配置，clone/迁移后必须重配）。

## 6. 当前状态

- 契约条数**不手写**（硬约束 2），以 `DOCS_REGISTRY.md` 为准；条款状态见「条款落点表」
- 已完成：顶层文档刷新（2026-11-03）、门禁接线与路径修正、环5（C14/C15）、2026-11-04 约束收敛

## 7. 提交约定

- 文档/契约与代码**分离提交**；契约变更在提交信息带 `contract_id`

## 8. 长期目标里程碑汇报（核心）

> 六段模板、硬规则、阻塞点交叉分析正文在 `DOCS_EXECUTION_CONSTRAINTS.md` §7（7.1–7.4）；本节只留触发与红线。

- **触发**：长期目标/多里程碑任务第一次汇报前声明"本次按 §8 模板汇报"，否则流程未启动
- **第 0 段先行**：续轮复议上轮第 6 段；首轮写"无（首轮）"+ 计划来源；缺前提核验视为未启动
- **三条红线**：门禁 ❌ 不进入下一子项；C1/C2 不成立直接 ❌；`pending`/未验证/延后不得写成 ✅
- **每轮开场先交叉审核**：新轮第一件事是把上轮「下一步/偏差/残余」与 `✅` 声称交给只读子代理独立复核并记录
  （正文见 `DOCS_EXECUTION_CONSTRAINTS.md` §7.1/§7.3/§7.4 第 6 条）；未记录不得写 `✅` 或发布完成结论
- **证据**：host / 四板 HIL / P3 路径可复跑，写明 scope、build、指纹、未覆盖路径
- **阻塞点**：命中 §7.4 判定时先开只读子代理独立取证，交叉分析记录后裁决
