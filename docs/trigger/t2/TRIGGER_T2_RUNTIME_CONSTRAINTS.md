# T2 预约触发运行时约束

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_RUNTIME_CONSTRAINTS.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-10-13

本文承接 T2 的**实施级约束**：启动期资源准入、运行期禁止事项、越界判定与回滚、验证顺序与证据分级。
稳定语义以 `TRIGGER_T2_ARCHITECTURE.md` 为准；任务状态以 `TRIGGER_T2_TODO.md` 为唯一事实源；
跨域挂载规范以 `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` 为准；顶层约束以 `docs/arch/HAOFV_ARCHITECTURE.md` 为准。

---

## 1. 资源准入：启动期 PIO 资源与运行期禁止热加载：启动期 PIO 资源与运行期禁止热加载（2026-10-13）

适用于 T2 及同类脉冲输出 persona。与 `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md` 的 `T2-DEPLOY` 、
`docs/arch/HAOFV_ARCHITECTURE.md` 的 PIO 资源分配表、`ARCH-PIOCAT-01`、`ARCH-PIOPARTITION-01`、
触发模式扩展表的 `sync_io_mode_ops_t.hw` 声明与资源冲突负测、以及 DeploymentGate 的
PIO/SM/DMA/IO/IP claims 一致。

- 所有 PIO 程序、IO route 和动作模板属**启动期资源**；装载与读回只能发生在 STOP/config ACK 阶段。
- **TDMA RUN 中禁止** persona/lease 退休、申请、**热加载**、卸载或路由改写；运行期只允许切换**已装载使能**。
- 变更角色或模板必须：停止运行 → 处理在飞实例 → 发布新 generation → **重新装载并读回**。
- **越界判定**：若实现在运行期发生程序装载，即判为越界；T2 方面表现为模型状态页 `fault_code = 3`（Trigger 域错误码：PIO/DMA 配置失败）。
- **验收判据**：连续 `START/STOP` 循环后**可用程序槽位不减少**，且 PIO 装载只发生在 STOP 阶段。

## 2. 验证顺序与证据分级（2026-10-13）

- **验证顺序**：① 单板回环（raw latch 连续性、正反映射 round trip、预约状态机、late 拒绝、PIO 输出 latch 与 T2 completion）；② 两节点（TRAIN → LOCKED → PREPARE/READY/fence → ARMED → completion）；③ 三节点（逐 hop generation、READY mask、fence 与 completion 聚合）；④ 五节点（lead-time 水位、window budget、故障注入）；⑤ 八节点（A0–A7 profile 只扩表不改算法）；⑥ 长期（lock/holdover、map generation、late/window miss、FIFO 水位、硬件 latch 缺失、T2 error 分布、watchdog）。
- **证据分级**（低等级不得替代高等级结论）：软件模型测试 < 完整帧 store-and-forward HIL < 真正 cut-through HIL < 多节点时钟分发闭环。
- 每个验证对象的最小证据必须标注其所属等级。
