# TDMA 基础件主域 README

Status: Active
Domain: TDMA
Canonical: `docs/tdma/README.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-19

`docs/tdma/` 维护 TDMA 基础件主域。TDMA 在 HAOFV 中不是 VDC 的内部组件，而是底层确定性通讯与环路反馈基础件。

当前重点是完整实时预算、自主飞行与有界装卸。普通负载由 Core0 提前准备；同步特等席通过 typed mailbox 与 Core1 快速交接；PIO/DMA 执行固定运输。配置、物理发车、数据送达、DCO 采用和正式发布分别评价。

TDMA 负责 profile/拓扑、固定映像、资源准入、runtime、adapter、运输身份与证据；VDC 负责时间控制，RefMem 负责事实提交，Calibration 负责测量质量，Trigger/SYNC_IO 负责业务与硬件输出。四板是当前验证范围，NO5 不作为主线前置。

标准文件：

| 文件 | 定位 |
|---|---|
| [TDMA_DOMAIN_ARCHITECTURE.md](TDMA_DOMAIN_ARCHITECTURE.md) | 总体评审入口：按代码说明 owner、飞行数据流、预算及跨域边界。 |
| [TDMA_RUNTIME_CONSTRAINTS.md](TDMA_RUNTIME_CONSTRAINTS.md) | 固定布局、异步工位、特等席、实时预算推导、生命周期和验收判据。 |
| `CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`（校准域） | 多板 SPI CLK 训练、双向测量和校准门禁；TDMA 仅负责 transport/resource integration。 |
| [TDMA_DOMAIN_TODO.md](TDMA_DOMAIN_TODO.md) | 当前主线、未完成、已完成与退出门禁；保留原 task ID。 |
| [TDMA_TASK_PROGRESS.md](TDMA_TASK_PROGRESS.md) | 近期原始记录及历史归档索引，失败与回退保留。 |
| [历史架构](../legacy/tdma/LEGACY_TDMA_DOMAIN_ARCHITECTURE.md) / [历史 TODO](../legacy/tdma/LEGACY_TDMA_DOMAIN_TODO.md) | 重构前快照，不作为当前能力或任务状态。 |

预算和节点数引用当前代码符号；实测以相应源码指纹、profile 和原件为准。基础 quick P3、严格调度、逐圈特等席与 DPLL 精度各自验收，不互相替代。
