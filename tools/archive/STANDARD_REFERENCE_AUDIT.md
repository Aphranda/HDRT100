# 标准：tools/ 引用审计

Last updated: 2026-10-10

适用：**仅 `tools/`**（OUT 侧不设引用审计：`out/` 无被引用路径）。

## 1. 目的

移动前确认工具是否被引用；移动后确认旧路径引用归零。

## 2. 语料范围

`docs/**`（含 `legacy/`）、`config/**`、`components/**`、`application/**`、`middleware/**`、
`linker/**`、`.githooks/**`、`CMakeLists.txt`、`AGENTS.md`、`tools/tool_registry.json`、
`tools/tool_layout.json`、`tools/README.md`。排除：`out/**`、被审计对象自身、`tools/archive/**`。

## 3. 方法

对每个条目取目录/文件名做**子串匹配**（覆盖字符串拼接与动态 import），记录命中次数与样例；
输出 `tools/_ref_audit.json`（机器可读）与 `tools/archive/REFERENCE_AUDIT.md`（人读 + 旧→新映射）。

## 4. 判据

移动前：强引用命中 ⇒ 同批更新引用或放弃移动；批次验收：旧路径引用 = 0（wrapper 自身除外）；
全量完成：`_ref_audit.json` 不再出现旧顶层路径。

## 5. 复审计与记录

触发：每次迁移批次结束、新增/改名/移动工具、`docs/**` 大批改动、用户要求。历史追加不覆盖。
记录模板：范围 / 引用更新（文件:行 → 新路径）/ 审计结果（旧路径引用=N）/ 抽样验证 / 回滚路径。
