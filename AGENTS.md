# Agent 常设命令（Codex / Claude / AI 助手）

Last updated: 2026-10-25

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

## 1. 体系是什么（30 秒版）

三环自回归 + 门禁 + 交叉审核：

```
环1 新鲜度: 顶层文档(HAOFV_ARCHITECTURE.md) 7 天内必须反映域文档新冻结的契约，否则检查器 FAIL
环2 登记:   域文档冻结跨域契约 → 必须在 DOCS_REGISTRY.md 登记（contract_id 唯一，锚点真实）
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
7. 提交前的验收凭证按**改动类别**分流（`tools/acceptance/credential_scope.py` 是映射的唯一
   事实源，pre-commit 按它路由；每类凭证只绑定自己的指纹域，互不失效）：
   - **固件 / 验收流程**（`components/`、`application/`、`boards/`、`bootloader/`、`cmake/`、
     `config/`、`drivers/`、`linker/`、`middleware/`、`osal/`、`platform/`、`third_party/`、
     `tools/`（`tools/acceptance/` 除外）与构建文件）：必须执行
     `python tools/hardware_acceptance/p3_hardware_acceptance.py run`，或等价的步进工作流
     `python tools/hardware_acceptance/hardware_acceptance_workflow.py --profile full`（同一组
     37 个步骤函数；可 `--run <dir>` 续跑，只有所需步骤在同一运行目录、同一源码指纹下按序
     全部运行才写凭证）；pre-commit 用 `check-staged` 按 staged 源码指纹核验 P3 凭证。
   - **测试 / 门禁脚本**（`tests/`、`.githooks/`、`tools/acceptance/`）：不需要台架。提交前执行
     `python tools/acceptance/host_acceptance.py run`（默认跑本批 staged 的 host 用例）并写
     `config/hardware_acceptance/host_acceptance_receipt.json`；pre-commit 用
     `host_acceptance.py check-staged` 按 staged host 指纹核验，并重跑 receipt 记录的用例。
   - **文档**（`docs/`、`*.md`）：只跑 §5 文档门禁，不需要验收凭证。
   混合改动按各自类别分别满足；旧凭证、手工报告和 replay 一律不能放行。
8. `*_TASK_PROGRESS.md` 单文件 ≤200KB（C14）；超标必须把**最旧**条目连续归档到
   `docs/legacy/<domain>/LEGACY_<DOMAIN>_TASK_PROGRESS_<NN>.md`，并在规范文件维护
   `## 归档索引`（ID 闭包、条目数相符）；归档只搬旧证据，禁止丢证据
9. `*_TASK_PROGRESS.md` 条目按新鲜度**倒排**——**最新鲜的在最上面**（C15）：日期序列必须
   非递增（同日内部顺序不限），新条目写在 `## 当前 checkpoint` 之后，归档文件同样倒排
10. **禁止回滚、禁止删除（最高优先级）**：不得对工作区执行 `git checkout --`、`git reset --hard`、
    `git restore`、`rm`/`git rm`、`Remove-Item` 等会丢弃已有工作的操作；确需撤销/移除时，
   **只能归档**——移动到对应域的归档目录并写明理由与时间戳，或先以 `git stash create` +
   `refs/wip/<tag>` 做完整快照。任何撤销类操作必须先取得用户明确同意，且可随时完整恢复

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
- **改完 `tools/doc_regression_check.py` → 必须 `python tools/doc_regression_check.py --skill-sync`**（同步三份副本，检查器自检）
- **日期**：`Last updated` 必须 `YYYY-MM-DD` 且合法（非法日期触发 FAIL）

## 5. 验证命令（改文档后必跑）

```powershell
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py
python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider
sh .githooks/pre-commit
python tools/doc_regression_check.py --log-check   # 逃生门审计（建议每周）
python tools/hardware_acceptance/p3_hardware_acceptance.py check-staged
```

> **门禁接线**：pre-commit 依赖 `git config core.hooksPath .githooks`（本地配置，clone/迁移后**必须重配**，否则 commit 不触发检查器）。`sh .githooks/pre-commit` 可手动验证。

## 6. 当前状态与进行中任务

- 已登记契约：**条数不在此手写**（硬约束 2），以 `docs/check/DOCS_REGISTRY.md` 为准；检查器每次门禁校验 id 唯一与锚点真实
- 条款落点：顶层硬约束全覆盖，逐条状态（OK/PARTIAL/VIOLATED/PENDING）见登记表「条款落点表」
- **已完成**：顶层 `docs/arch/HAOFV_ARCHITECTURE.md` 刷新（v3，2026-08-21）；2026-08-24 审查修复（门禁接线 / 路径修正 / T18 闭环 / 顶层补 DOCS-FLASH-01）；2026-09-17 新增环5（C14 轮转 + C15 倒排）并轮转 VDC 进展日志
- 下一期：verify-doc-crosscheck 自动化强化；TDMA/RefMem/HAOFV_FLASH 进展日志轮转（见 TODO 与 C14 债务基线）

## 7. 提交约定

- 文档/契约变更与代码变更**分离提交**，便于 `git log` 审计
- 提交信息含契约变更时带 `contract_id`（如 `docs(tdma): 登记 TDMA-REASON-01`）

## 8. 长期目标里程碑汇报约束（核心）

**触发条件**：任何长期目标/多里程碑任务（N3.x/N4.x 等）的第一次汇报前，先声明“本次按 §8 模板汇报”；未声明视为流程未启动。新开会话时，本文件随仓库指令加载，因此 §8 即默认钩子；也可在用户侧直接说“按 AGENTS §8 汇报”。

**每次子项完成/失败/延后必须按同一模板**，汇报即门禁记录：

1. **完成状态表**：子项、结果（✅/⏳/❌/⚠️）、改动类别（feature / refactor / fix / tooling / docs，对应 §2.7 的凭证分流）、P3 build/fingerprint/files、提交号。
2. **证据**：host 用例、四板 HIL 路径、P3 凭证路径；路径必须可复跑；凭证必须写明 scope（如 `QUICK_DIAGNOSTIC` / FULL）与未覆盖路径。
3. **HAOFV 六门**：逐门给 ✅/⚠️/❌，不空、不合并；每门必须正面回答括号内判定问句，答不上来不得给 ✅：
   - ① 无新增总线/payload class/第二命令槽/第二 ACK 事实（新增载荷是否已在注册表白名单、回程是否仍唯一？）；
   - ② 不占 core1 硬实时窗口或拉长一等席周期（phase/seat/静态预算是否用工具或符号重新核算，而非断言“没变”；新增或扩容席位是否有 EXE-SEAT-01 用户审核记录？）；
   - ③ 单写者/owner 边界保持（状态实例数是否仍为 1；phase 归属、模式边界（RUN 只读 active）、层间方向 component→service→SDK 是否未变？）；
   - ④ 故障 fail-closed + reason/evidence（每条新失败路径是否有 reason、计数和快照；F0–F3 分级是否仍成立？）；
   - ⑤ 布局/版本/契约同步（DeploymentGate 的 build/app map/hw/config/cal/sync CRC 是否同步；错误码是否落在本域段且未复用已冻结枚举？）；
   - ⑥ host + 四板证据可复跑（凭证 scope 是否覆盖改动路径；host/build/runtime/HIL/hardware 五层是否按需给出？）。
   横切核验（与六门并列、单独给结论；任一不成立即 ❌，不得记为 ⚠️）：
   - **C1 Flash/XIP**：erase/program 仍只由 core0 发起，core1 park ACK 有效，临界区内无 XIP/USB/SD/LCD 访问；
   - **C2 时间事实**：raw tick 未被回写，时间差使用 `int32_t` 回绕安全写法，换代重基与同代回退判据未混淆。
4. **偏差与残余**：未覆盖、延后、已知限制、台架问题及重试。每条 ⚠️ 必须写全“归属门 / 触发条件 / 影响面 / 复验判据 / 闭环里程碑”，缺项按 ❌ 处理；延后必须用户明确接受并记录为**非完成项**。下列情形直接 ❌，不得降级为 ⚠️：
   - 触碰 C1 或 C2；
   - 结构改动（refactor）出现：同名状态跨 TU 双实例、`#if`/`#include` 边界被切开、段/RAM_TEXT 归属变化未核算、初始化顺序依赖未声明；
   - RUN 态写 active 配置，或绕过 STOP/ARM 边界激活；
   - 用旧 build、旧源码指纹或 scope 不覆盖的凭证充当本轮证据；
   - 命中 `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` §6 的任一条越界判定。
5. **提交与凭证**：代码/工具与文档分离；代码提交前 `check-staged` 按 staged 指纹核验 P3；文档提交前跑 §5 全部门禁。
6. **下一步**：下一子项、前置条件、是否被门禁阻塞。

**硬规则**：
- 任一门禁 ❌/未通过 → 不进入下一子项，先归因修正；⚠️ 只用于“已定位、已隔离、有复验判据”的残余，且必须写偏差、影响、闭环里程碑。
- refactor 类（拆分/迁移/重命名）另须给等价性证据：状态实例数、导出符号集、条件编译组合、段/RAM_TEXT 归属、host 用例同集全绿，必要时给 map diff；缺任一项按未验证处理。
- `pending` 契约、未验证项、延后项不得写成 ✅ 或“完成”。
- 禁止“先做下一项，回头再补”；禁止用“基本可用/应该没问题”替代证据。
- 最终收口必须给：子项总表、未完成/残余风险、契约状态、证据索引、提交清单，并明确目标是否可标记完成。

