# 反射内存域

Status: Draft
Domain: REFMEM
Canonical: `docs/refmem/README.md`
Related: `docs/README.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-08-13

本目录是 Distributed Vector Blackboard / RefMem Sync 内部主域入口，维护分布式向量表、命令槽、ACK/NACK、节点事实、stale/CRC/epoch、静态分布式应用模型和多板共同状态。

## 当前主线

| 当前路径 | 定位 | 使用规则 |
|---|---|---|
| `REFMEM_DOMAIN_ARCHITECTURE.md` | Distributed RefMem 内部主域架构，定义职责边界、HAOFV 层级、静态分布式模型、核心数据面和目标代码形态。 | RefMem 主域 canonical 架构入口。 |
| `REFMEM_DOMAIN_TODO.md` | RefMem 主域独立待办，维护文档同步、静态模型、slot 契约、ACK/NACK、sync protocol、代码组件化和验证事项。 | 只记录未完成事项，不写验证流水账。 |
| `REFMEM_TASK_PROGRESS.md` | RefMem 主域任务进度，记录阶段性工作、验证结果和后续动作。 | RefMem 新任务完成后追加记录。 |

## 相关参考

| 当前路径 | 定位 |
|---|---|
| `../arch/HAOFV_ARCHITECTURE.md` | HAOFV 顶层架构，定义 Distributed RefMem 的内部主域地位。 |
| `../arch/RTOS_HAOFV_ARCHITECTURE.md` | 当前 DTC100 反射内存和 RTOS owner 设计。 |
| `../vdc/VDC_DOMAIN_ARCHITECTURE.md` | VDC 共同时间主域；RefMem 只保存 VDC snapshot、版本、质量和证据。 |
| `../legacy/pinprobe/LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md` | PinProbe A1 RAM 反射内存历史方案。 |
| `../interface/SCPI_COMMAND_PLAN.md` | SCPI 与反射内存边界。 |
