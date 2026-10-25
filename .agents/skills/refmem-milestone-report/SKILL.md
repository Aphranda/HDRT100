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

1. **完成状态表**：子项、结果（✅/⏳/❌/⚠️）、改动类别（feature/refactor/fix/tooling/docs）、P3 build/fingerprint/files、提交号。
2. **证据**：host 用例、四板 HIL 路径、P3 凭证；路径必须可复跑；凭证必须写明 scope（QUICK/FULL）与未覆盖路径。
3. **HAOFV 六门**：逐门 ✅/⚠️/❌，不空、不合并；每门必须回答括号内判定问句，答不上来不得给 ✅：
   ①新增载荷是否已注册、回程是否唯一；②预算是否用工具/符号重新核算、新席位是否经 EXE-SEAT-01 审核；③状态实例数/phase/模式边界/层间方向是否未变；④新失败路径是否有 reason/计数/快照、F0–F3 是否成立；⑤DeploymentGate CRC 与错误码段是否同步；⑥凭证 scope 是否覆盖改动路径、五层证据是否按需给出。
   横切核验（单独给结论，不成立即 ❌）：**C1** erase/program 仅 core0 + park ACK + 临界区无 XIP/USB/SD/LCD；**C2** raw tick 不回写 + int32 回绕安全 + 换代重基/同代回退未混淆。
4. **偏差与残余**：未覆盖、延后、已知限制、台架问题及重试；每条 ⚠️ 写全“归属门/触发条件/影响面/复验判据/闭环里程碑”，缺项即 ❌；延后必须用户明确接受并记为非完成项。
5. **提交与凭证**：代码/工具与文档分离；代码提交前 `check-staged` 按 staged 指纹核验 P3；文档提交前跑 AGENTS §5 全部门禁。
6. **下一步**：下一子项、前置条件、是否被门禁阻塞。

## 硬规则

- 任一门 ❌/未通过 → 不进入下一子项，先归因修正；⚠️ 只用于“已定位、已隔离、有复验判据”的残余。
- refactor 类（拆分/迁移）另须给等价性证据：状态实例数、符号集、条件编译组合、段/RAM_TEXT、host 同集全绿、必要时 map diff；缺项按未验证处理。
- `pending` 契约、未验证项、延后项不得写成 ✅ 或“完成”。
- 禁止“先做下一项，回头再补”；禁止用“基本可用/应该没问题”替代证据。
- 最终收口必须给子项总表、未完成/残余风险、契约状态、证据索引、提交清单，并明确目标是否可标记完成。

## 参考

- `AGENTS.md` §8（长期目标里程碑汇报约束）
- `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` 的 `EXE-REPORT-01`
- `docs/check/DOCS_REGISTRY.md`（契约状态，pending 不写成 active）
