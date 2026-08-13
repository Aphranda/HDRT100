# 反射内存域

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/README.md`
Related: `docs/README.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-08-13

本目录是 Distributed Vector Blackboard / RefMem Sync 内部主域入口，维护分布式向量表、RefMemAO、通用 A0-A7 逻辑插槽、静态分布式应用模型、节点装载、SlotClaim 协调、表镜像加载、命令槽、ACK/NACK、stale/CRC/epoch 和多板共同状态。

当前主线已经从单纯 64 KB 表骨架推进到“RefMemAO 基础件 + 静态模型表 + staging load”的实现阶段。后续待办以 `REFMEM_DOMAIN_TODO.md` 的优先级为准：先收敛 TableRegistry、staging/active 表镜像、SlotClaimMap 和 SlotContract，再推进 sync protocol 与 command slot。

## 当前主线

| 当前路径 | 定位 | 使用规则 |
|---|---|---|
| `REFMEM_DOMAIN_ARCHITECTURE.md` | Distributed RefMem 内部主域架构，定义职责边界、HAOFV 层级、静态分布式模型、通用基础件、核心数据面、load 状态机和目标代码形态。 | RefMem 主域 canonical 架构入口。 |
| `REFMEM_DOMAIN_TODO.md` | RefMem 主域独立待办，维护 TableRegistry、staging/active 表镜像、SlotClaimMap、SlotContract、ACK/NACK、sync protocol、代码组件化和验证事项。 | 只记录当前未完成事项和执行顺序，不写验证流水账。 |
| `REFMEM_TASK_PROGRESS.md` | RefMem 主域任务进度，记录阶段性工作、验证结果和后续动作。 | RefMem 新任务完成后追加记录。 |

## 相关参考

| 当前路径 | 定位 |
|---|---|
| `../arch/HAOFV_ARCHITECTURE.md` | HAOFV 顶层架构，定义 Distributed RefMem 的内部主域地位。 |
| `../arch/RTOS_HAOFV_ARCHITECTURE.md` | 当前 DTC100 反射内存和 RTOS owner 设计。 |
| `../vdc/VDC_DOMAIN_ARCHITECTURE.md` | VDC 共同时间主域；RefMem 只保存 VDC snapshot、版本、质量和证据。 |
| `../legacy/pinprobe/LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md` | PinProbe A1 RAM 反射内存历史方案。 |
| `../interface/SCPI_COMMAND_PLAN.md` | SCPI 与反射内存边界。 |
