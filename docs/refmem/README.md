# 反射内存域

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/README.md`
Related: `docs/README.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-08-14

本目录是 Distributed Vector Blackboard / RefMem Sync 内部主域入口，维护分布式向量表、RefMemAO、通用 A0-A7 逻辑插槽、静态分布式应用模型、节点装载、SlotClaim 协调、表镜像加载、命令槽、ACK/NACK、stale/CRC/epoch 和多板共同状态。

当前主线已经从单纯 64 KB 表骨架推进到“RefMemAO 基础件 + 静态模型表 + StorageAO 通用文件管理 + `.rmtp` staging load”的实现阶段。`app_model.rmtp` 通过 `SYSTem:STORage:FILE:*` 上传、读回和 `SYSTem:REFMEM:LOAD:SD` 进入 staging 的正向闭环已经完成；RefMem 向量表只记录状态、CRC、path hash、registry 和 evidence，不承载文件数据。

下一轮主线以 `REFMEM_DOMAIN_TODO.md` 的优先级为准：先把当前 staging snapshot 升级为真实 active/staging/rollbackable table image，补齐 owner validation callback、activation gate 和回滚证据；随后把 `LOAD:NODE`、`CONFigure:MODEl:*:LOAD` 和 SlotClaim/RealtimeCapabilityContract 接入同一套表镜像激活链路。

## 当前主线

| 当前路径 | 定位 | 使用规则 |
|---|---|---|
| `REFMEM_DOMAIN_ARCHITECTURE.md` | Distributed RefMem 内部主域架构，定义职责边界、HAOFV 层级、静态分布式模型、通用基础件、核心数据面、load 状态机和目标代码形态。 | RefMem 主域 canonical 架构入口。 |
| `REFMEM_DOMAIN_TODO.md` | RefMem 主域独立待办，维护 TableRegistry、staging/active 表镜像、SlotClaimMap、SlotContract、ACK/NACK、sync protocol、代码组件化和验证事项。 | 只记录当前未完成事项和执行顺序，不写验证流水账。 |
| `REFMEM_TASK_PROGRESS.md` | RefMem 主域任务进度，记录阶段性工作、验证结果和后续动作。 | RefMem 新任务完成后追加记录。 |
| `REFMEM_SYNC_ARCHITECTURE.md` | RefMem Sync 内部架构，定义总线无关同步协议、adapter 边界、RMA/fence/quality 约束。 | RefMem 同步协议的 canonical 架构说明。 |
| `REFMEM_MIN_SYSTEM_PLAYBOOK.md` | RefMem 最小系统板 bring-up 记录，维护当前两板线序、端口、验证和执行日志。 | 只记录当前最小系统板实际操作，不写架构约束。 |

## 当前状态摘要

| 项目 | 当前状态 | 下一步 |
|---|---|---|
| StorageAO 文件/目录 CRUD | 已完成 SCPI 通用文件/目录管理闭环，`SYSTem:REFMEM:PACKage:*` 专用入口已删除。 | 扩展更大文件的分片落盘或后端流式事务。 |
| `.rmtp` table image | 已支持 SD/Storage 文件读取、header/directory/payload/package/table CRC 校验，并写入 RefMem staging snapshot。 | 将 staging snapshot 扩展为真实 staging table image。 |
| TableRegistry | 已能观察 active/staging CRC、validation state 和 evidence。 | 增加 owner validation callback、active/staging/rollbackable 切换和 rollback evidence。 |
| NodeLoad / BoardCapability | 已支持单条 SCPI staging 候选和基础合法性校验。 | 升级为多条 staging table image，并接入 SlotClaimMap、RealtimeCapabilityContract 和 DeploymentGate。 |
| RefMem Sync | 已有总线无关 frame、HELLO helper、PIO SPI adapter skeleton 和纯 C 测试。 | 进入两板 HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY 最小闭环。 |

## 相关参考

| 当前路径 | 定位 |
|---|---|
| `../arch/HAOFV_ARCHITECTURE.md` | HAOFV 顶层架构，定义 Distributed RefMem 的内部主域地位。 |
| `../arch/RTOS_HAOFV_ARCHITECTURE.md` | 当前 DTC100 反射内存和 RTOS owner 设计。 |
| `../vdc/VDC_DOMAIN_ARCHITECTURE.md` | VDC 共同时间主域；RefMem 只保存 VDC snapshot、版本、质量和证据。 |
| `../legacy/pinprobe/LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md` | PinProbe A1 RAM 反射内存历史方案。 |
| `../interface/SCPI_COMMAND_PLAN.md` | SCPI 与反射内存边界。 |
