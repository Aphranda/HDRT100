# RefMem布局压缩移植交叉审核

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_CROSS_REVIEW_01.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`
Last updated: 2026-09-17

## 范围与来源

审核序列分支对 real-flight `e6ff06ae` 的 RefMem RAM 布局、产包工具与回归测试切片移植，
及配套 `REFMEM-LAYOUT-01` pending 契约。未移植该提交的TDMA优先接收、VDC、实时调度配置或P3凭证。
上游契约来源为 `e2d7cda6`，本记录为当前分支独立审核，不代替上游提交单或复用其验收结果。

## C11交叉审核记录

- 审核方：`/root/turntable_link_mode`，未编写RAM切片及契约文档。
- 实现方：`/root/counter_role_config`；文档主控：`/root`。
- 审核方式：独立只读检查上游与当前补丁、代码符号、布局/实际产包测试证据和文档门禁。
- 审核结论：`PASS_WITH_NOTE`，允许维持pending登记，不提升为active。
- 审核日期：2026-09-17。

## 结论与证据

- 保留逻辑节点数量、区域ID及writer，目录按`offsetof`/`sizeof`生成；布局升级而RMTP封装独立。
- VDC/DPLL有效payload、自然对齐与seqlock保持；三槽位序列角色入口及事务不被上游代码覆盖。
- 有效CRC的旧布局包在owner层拒绝，active/rollback镜像保持；host拓扑CRC与本分支C实现一致。
- `out/pytest/ram-layout-20260917-r1` 的布局/实际产包/角色/节点容量回归通过；当次98项为验证快照。
- 审核方独立运行docs_check和doc_regression通过；旧`TDMA-FLIGHT-BITMAP-01`格式警告保留。

审核不声明当前固件硬件验收、P3、多板混合布局准入或时序通过。当前分支构建及单板结果
另见序列任务进度036及后续记录；提交门禁凭证必须绑定本分支实际staged源码。
