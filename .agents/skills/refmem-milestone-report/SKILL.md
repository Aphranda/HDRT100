---
name: refmem-milestone-report
description: Use at the start of any long-term multi-milestone goal (N3.x/N4.x, REFMEM/TDMA/HAOFV) or when the user asks for a milestone, HAOFV, or P3 acceptance report; declares the AGENTS §8 trigger and points at the single report template in docs/check/DOCS_EXECUTION_CONSTRAINTS.md §7.
---

# REFMEM 里程碑汇报（入口）

本 skill 只负责**触发**，不再复述模板——模板正文只有一处，避免再次分叉。

- 触发后第一件事：在回复开头声明“本次按 §8 模板汇报”。
- 六段模板、硬规则、阻塞点交叉分析的**唯一正文**：`docs/check/DOCS_EXECUTION_CONSTRAINTS.md` §7（7.1–7.4）。
- `AGENTS.md` §8 只保留触发与三条红线；契约状态以 `docs/check/DOCS_REGISTRY.md` 为准，`pending` 不得写成 `active`。

## 何时触发

- 开始任何长期目标/多里程碑任务（N3.x/N4.x、REFMEM/TDMA/HAOFV 等）；
- 用户要求“汇报/里程碑报告/HAOFV 审查/P3 验收”；
- 一个子项完成、失败或决定延后时。

## 最容易漏的三条

- 第 0 段先行：缺前提核验、或用上轮结论当证据 → 本轮流程未启动，不得开新改动。
- 证据必须可复跑；凭证写明 scope / build / 源码指纹 / 未覆盖路径。
- C1/C2 任一不成立直接 `❌`，不得降 `⚠️`。

## 参考

- `AGENTS.md` §8（触发与红线）
- `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` §7（模板正文，原 `EXE-REPORT-01`）
- `docs/check/DOCS_REGISTRY.md`（契约状态）
