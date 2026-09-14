# 归档：Codex 角色定义（2026-09-14）

本目录保留**已停用**的 Codex CLI 自定义 agent 角色定义：`agents/worker.toml`、`agents/reviewer.toml`。

## 当前位置与生效路径（先看这一节）

Codex 读取自定义 agent 的位置是 `~/.codex/agents/`（个人）与 `.codex/agents/`（项目级，且仅
trusted 项目加载）；配置层是 `~/.codex/config.toml` 与项目级 `.codex/config.toml`。本仓库没有
`.codex/` 目录，因此**这两份文件不在任何 Codex 读取路径上，当前不生效**；留在这里只是留档。
原先同在 `.agents/` 下的 `.agents/config.toml` 属于同一类（无消费者），已于 2026-09-14 删除。

## 为什么归档

最初的版本在 `developer_instructions` 里写入了大量针对特定模型能力的限制（作用域约束、禁止自审、
必须从 `accept`/`revise`/`escalate` 中选一个 disposition、逐项检查清单等），并把模型钉死为
`model = "gpt-5.6-sol"`。当前使用的模型能力更强，不需要角色级的限制。

## 已做的解除限制处理（2026-09-14）

- **删除全部能力相关覆盖**：不再写 `model` / `model_reasoning_effort` / `sandbox_mode` /
  `nickname_candidates`。省略这些可选字段时，子 agent 继承父会话设置（当前为 6 Astra、high、
  父会话 sandbox），因此不会被子文件钉到旧模型或更窄的 sandbox。
- **重写 `developer_instructions`**：只描述该 lane 做什么，并显式声明"本文件不设角色级能力
  限制、不规定固定措辞或 disposition 词表"。实际约束只来自 `AGENTS.md` 与用户当次指令。
- `reviewer.toml` 原先把"只读、不得编辑文件"写在 `developer_instructions` 里，却又设
  `sandbox_mode = "workspace-write"`——两者互相矛盾，现已一并移除。评审独立性（C11 禁止自审
  自批）是文档治理契约，不依赖角色文件，见下节。
- **删除 `.agents/config.toml`**（2026-09-14）：它不在 Codex 读取路径上（Codex 只读
  `~/.codex/config.toml` 与项目级 `.codex/config.toml`），仓库内也没有任何工具读取它；其三个
  数值与 Codex 内置默认值完全相同，删除不改变任何行为。数值记录保留在下节，文件本身可从
  `git show <commit>^:.agents/config.toml` 取回。

## 仍然有效的部分（不因归档而失效）

- **C11 交叉审核（禁止自审自批）** 是 HAOFV 文档治理契约，与角色文件无关。契约 /
  `docs/check/DOCS_REGISTRY.md` 登记状态变更仍必须由独立复核方确认，记录落在
  `docs/check/submissions/`。
- **写者互斥**：并行工作时文件域必须互不相交。跨域索引文件（`docs/README.md`、
  `docs/check/submissions/README.md`、`docs/check/DOCS_REGISTRY.md`）建议只由单一主控修改；
  两个写者同改一份索引会出现 `file changed since it was read` 或令对方 worker 因
  "外来文档散列变化"中止。
- **树隔离**：需要真正并行时使用独立 `git worktree`，主树只做合并与提交。
- **并行上限**：由 Codex 内置默认值决定——`agents.max_threads = 6`、`agents.max_depth = 1`、
  `agents.job_max_runtime_seconds = 1800`。其中 `max_depth = 1` 正好对应"主 → 从"一层，主控可
  派从任务、从任务不能再派。本仓库没有任何 Codex 读取路径上的配置文件覆盖它们，因此既不需要、
  也不存在仓库侧的并行上限设置。

## 如何取回

文件未删除。若要真正生效，目标是 **`.codex/agents/`**，而不是 `.agents/agents/`（后者 Codex 不读）：

```powershell
git mv .agents/archived/agents/<file> .codex/agents/<file>
```

取回前注意两点：

- `name = "worker"` 与 Codex 内置 agent `worker` 同名，同名自定义文件会**覆盖内置定义**；
  用一份薄定义替换内置的调优定义通常是净损失，建议改名为 `hdr-worker` 之类的项目专属名字。
- 项目级 `.codex/` 层只在项目被标记 trusted 时加载（本机已在 `~/.codex/config.toml` 的
  `[projects.'d:\work\hdrt100']` 中设为 `trusted`）。

`.agents/config.toml`（已删除）取回：`git show <提交>^:.agents/config.toml`，但它在 Codex 侧不
生效，取回只对留档有意义。

历史见 `git log --follow`。
