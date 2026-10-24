---
name: refmem-milestone-report
description: Use at the start of any long-term multi-milestone goal (N3.x/N4.x, REFMEM/TDMA/HAOFV) or when the user asks for a milestone, HAOFV, or P3 acceptance report; enforces the fixed status-table, evidence, six-gate, residual, commit, and next-step template from AGENTS.md §8.
---

# REFMEM 里程碑汇报

本 skill 是 `AGENTS.md` §8 的按需入口。**触发后第一件事**：在回复开头声明“本次按 AGENTS §8 模板汇报”，然后按下面模板执行。

## 何时触发

- 开始任何长期目标/多里程碑任务（N3.x/N4.x、REFMEM/TDMA/HAOFV 等）；
- 用户要求“汇报/里程碑报告/HAOFV 审查/P3 验收”；
- 一个子项完成、失败或决定延后时。

## 每次汇报的六段

1. **完成状态表**：子项、结果（✅/⏳/❌/⚠️）、P3 build/fingerprint/files、提交号。
2. **证据**：host 用例、四板 HIL 路径、P3 凭证；路径必须可复跑。
3. **HAOFV 六门**：逐门 ✅/⚠️/❌，不空、不合并：
   ①无新增总线/payload class/第二命令槽/第二 ACK 事实；②不占 core1 硬实时窗口或拉长一等席；③单写者/owner 边界；④fail-closed + reason/evidence；⑤布局/版本/契约同步；⑥host + 四板证据可复跑。
4. **偏差与残余**：未覆盖、延后、已知限制、台架问题及重试；延后必须用户明确接受并记为非完成项。
5. **提交与凭证**：代码/工具与文档分离；代码提交前 `check-staged` 按 staged 指纹核验 P3；文档提交前跑 AGENTS §5 全部门禁。
6. **下一步**：下一子项、前置条件、是否被门禁阻塞。

## 硬规则

- 任一门 ❌/未通过 → 不进入下一子项，先归因修正；⚠️ 必须写偏差、影响、闭环里程碑。
- `pending` 契约、未验证项、延后项不得写成 ✅ 或“完成”。
- 禁止“先做下一项，回头再补”；禁止用“基本可用/应该没问题”替代证据。
- 最终收口必须给子项总表、未完成/残余风险、契约状态、证据索引、提交清单，并明确目标是否可标记完成。

## 参考

- `AGENTS.md` §8（长期目标里程碑汇报约束）
- `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 的 `EXE-REPORT-01`
- `docs/check/DOCS_REGISTRY.md`（契约状态，pending 不写成 active）
