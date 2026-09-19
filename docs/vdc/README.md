# VDC 内部主域

Status: Active
Domain: VDC
Canonical: `docs/vdc/README.md`
Related: `docs/README.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-09-19

> 分支范围：本文同步自real-flight的`7a3e0954`，正文任务状态、实现与证据指来源分支。
> 本地固件状态及契约边界见[同步说明](../README.md#序列分支的上游文档同步范围)。

本目录是 Virtual Distributed Clock / VDC 内部主域入口，负责本地时间到共同输出时间的映射、DPLL 控制和时间质量发布。失联保持、恢复及正式共同时间服务的完成边界见主架构与 TODO。

Architecture 描述当前实现、owner 和能力边界，运行细则保留契约细节；TODO 维护任务与门禁，Task Progress 保存验证证据。历史由 `docs/legacy/vdc/` 与进度归档索引查找。

当前主线为 TDMA typed 参考运输、Core1 同事件匹配与从板本地 DCO 跟踪、SYNC_IO 的 PIO/DMA 输出。低频晶振 actuator、完整 HOLDOVER/恢复及产品质量发布仍需接线和验收，不能由已有输出闭环推定完成。

## 当前主线

| 当前路径 | 定位 | 使用规则 |
|---|---|---|
| `VDC_DOMAIN_ARCHITECTURE.md` | 面向总体评审，说明当前模块、数据流、owner、时间模型及完成边界。 | VDC 主域 canonical 架构入口。 |
| `VDC_RUNTIME_CONSTRAINTS.md` | 编码 ABI、生命周期、并发、输出与探针细则。 | 由主架构契约入口引用，旧模式与当前 TIMER1 主线分别解释。 |
| `VDC_DOMAIN_TODO.md` | VDC 主域任务索引，按未完成与已完成分组，维护当前主线、状态、依赖和退出门禁。 | 实施与验证证据见 Task Progress，不在 TODO 重复流水账。 |
| `VDC_TASK_PROGRESS.md` | VDC 主域任务进度，记录阶段性工作、验证结果和后续动作。 | VDC 新任务完成后追加记录。 |
| `VDC_DOMAIN_RISK_REVIEW.md` | VDC/DPLL 风险评审记录，收敛正确性缺陷、半接线路径、文档漂移和测试缺口。 | 评审/纠偏时更新，不写普通流水账。 |

## 当前主线摘要

| 主线 | 当前结论 | 后续落点 |
|---|---|---|
| TDMA Foundation 硬实时环 | TDMA Foundation 负责无冲突同步窗口、同步帧参考边沿、上/下行 runtime 和 PIO/DMA timestamp capture。 | TDMA schedule profile、sync window、guard window、capture FIFO word 和 late 规则归 `docs/tdma/`；VDC 只读 observation binding。 |
| DPLL 锁相环 | MASTER Domain PI 与显式 typed follower 本地控制分开；当前输出以 committed DCO 为准。 | 固件状态、内部质量和实际 GPIO 精度分别验收。 |
| 低频驯服与恢复 | Domain 有 trim/freeze 接口与 HOLDOVER 枚举，实物 actuator 和完整失联恢复尚未闭合。 | 见 TODO 的角色扩展、HOLDOVER 和质量发布任务。 |
| Core 边界 | Core1 是运行态控制/DCO writer；Core0 配置/intent/显式保存与管理发布，PIO/DMA 执行边沿。 | 以主架构的生命周期和 guard 边界为准。 |

## 相关参考

| 当前路径 | 定位 |
|---|---|
| `../arch/HAOFV_ARCHITECTURE.md` | HAOFV 顶层架构，定义 VDC 的内部基础主域地位。 |
| `../arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` | 既有 VDC/DPLL 融合架构输入，后续逐步迁入 VDC canonical。 |
| `../arch/RTOS_HAOFV_ARCHITECTURE.md` | 当前 RTOS task、VDC/DPLL owner 壳和双核边界。 |
| `../tdma/TDMA_DOMAIN_ARCHITECTURE.md` | TDMA Foundation 主域，定义上/下行 runtime、payload registry、adapter 和 ring completion evidence。 |
| `../sync/SYNC_IO_ARCHITECTURE.md` | SYNC_IO 下的分布式 DPLL 落地方案和历史设计输入。 |
| `../refmem/REFMEM_DOMAIN_ARCHITECTURE.md` | RefMem 共同事实主域，保存 VDC snapshot、版本、质量和证据。 |

## 边界

- VDC Domain 是内部基础主域，不是对外 SCPI 顶级命令域。
- `SYNC:*` 是对外同步动作域；VDC Domain 是同步动作背后的共同时间 owner。
- RefMem Domain 保存 VDC 事实快照和质量证据，但不计算 offset/rate。
- Angle DPLL 消费 VDC 时间基准并生成 `T_fire_base`，不能写 VDC offset/rate。
