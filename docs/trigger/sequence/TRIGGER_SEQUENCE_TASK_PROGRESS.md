# SYNC_SEQUENCE / 位置内序列进展日志

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`, `docs/sync/SYNC_IO_TASK_PROGRESS.md`
Last updated: 2026-10-16

本文件记录**动作侧序列解算与执行**的闭环证据。条目按新鲜度倒排（最新在最上面）。

## 当前 checkpoint

### TRIGGER-SEQ-PROGRESS-20261016-001：任务移交——序列解算与执行归本域（尚未开工实现）

- **移交决定（2026-10-16，触发域口径）**：`T2 只负责分发位置时间片，动作执行得到时间片之后，自行按照约定进行`；`四块板子并行执行，只对位置时间片负责`。据此，**位置内多组序列的解算与执行归 SYNC 域**，触发域只交出时间片与"约定"（本板角色的组序列表：逐组 `argument` + `delta_q`），不再逐组配速。
- **上游现状（触发域已完成侧）**：
  - 四段交付 = 分发/冻结/接收/滚动；360° 全程扫描（361 位置，同一固件、仅重跑安装与采集）实测 `entered/announced/frozen` 三列均达 361，位置窗持续滚动。
  - 本板角色组序列可按 `G_r` 行安装（`TEMPLate:INSTALL`，行序即组序），时间参数为**安装参数**：改周期/改码只需重跑安装与采集，**不需重烧**（`config/trigger/t2_sequence_profile.json`、`t2_workflow.py --seq-*`、`tools/sync_trigger/t2_sequence_sweep.py`）。
  - 触发域侧存在**过渡实现**（逐组配速：`t2_deadline_plan_t.next_tick` + 组游标 + 完成当遍推进），在架构文档与 TODO 中标注为过渡，本域接手后移除。
- **本域待办**：`SYNC-SCHED-001`（深度 ≥2 绝对 deadline 队列）→ `TRIGGER-SEQ-API-001`（序列提交接口）→ `TRIGGER-SEQ-EVIDENCE-001`（逐组证据）→ `TRIGGER-SEQ-TERM-001`（STOP/终止）→ `TRIGGER-SEQ-HIL-001`（台架验证）。见 `TRIGGER_SEQUENCE_TODO.md`。
- **支撑本域立项的实测证据（触发域台架，2026-10-16）**：组间隔取 10/25/30/40/50/60 ms 时，从板**全部**停在每位置 1 组（`T2:ACTion?` 组打包字 `done=0b1`、`next=1`、`in_flight=0xF`；`groups exec/count = 3/8`），即"观测到上一组完成再布防下一组"的往返大于任何可用组间隔——这正是深度 ≥2 队列要消除的依赖。
- **本域实现状态**：尚未开工（无代码改动、无构建/OTA、无台架证据）。契约登记待 `code_anchor` 真实存在后再进行（避免悬空锚点，见架构文档 §6）。
