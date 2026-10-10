# tools/ 引用审计（2026-10-10）

方法：对 `tools/` 顶层每个条目名，在 `docs/**`、`config/**`、`components/**`、`application/**`、
`middleware/**`、`linker/**`、`.githooks/**`、`CMakeLists.txt`、`AGENTS.md`、`tools/tool_registry.json`、
`tools/tool_layout.json` 中做子串匹配。

| 结果 | 数量 | 处置 |
|---|---|---|
| 被引用 | **111 / 113** | **原位保留**（移动会打断引用） |
| 未被引用 | 2（`__pycache__`、`t2_deploy_action_probe`） | 已**移动**到 `tools/archive/deletable-candidates/` |

结论：`tools/` 的扁平布局属"被大量文档引用的既定结构"，**不做物理重组**；
需要重组时按 `tools/README.md` 第 1 条作为独立迁移执行。原始清单见 `tools/_ref_audit.json`。
