# 文档索引

Status: Active
Domain: Documentation
Canonical: `docs/README.md`
Related: `docs/DOCS_NAMING_AND_STRUCTURE.md`
Last updated: 2026-07-07

本文档是 `docs/` 的总入口。新文档的命名、层级、元数据和迁移规则以
`docs/DOCS_NAMING_AND_STRUCTURE.md` 为准。

当前阶段先建立统一入口和归属关系，不批量重命名历史文件，避免破坏现有交叉引用。
后续需要改名时，应按域小批量迁移，并同步更新所有引用。

## 00 文档治理

| 文件 | 定位 |
|---|---|
| `DOCS_NAMING_AND_STRUCTURE.md` | 文档命名格式、层级关系、新增文件规则和迁移规则。 |
| `README.md` | 本索引文件，提供当前 `docs/` 文件归属。 |

## 01 系统架构

| 文件 | 定位 |
|---|---|
| `HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md` | HAOFV 顶层产品架构主文档。 |
| `HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE_SUPPLEMENT.md` | HAOFV 实施补充、示例和迁移说明。 |
| `HAOFV_PORTABILITY_EVALUATION.md` | HAOFV 可移植性评估。 |
| `RTOS_PORTING_PLAN.md` | FreeRTOS/OSAL 迁移计划。 |
| `MULTICORE_PARTITION_PLAN.md` | RP2350 双核分区计划。 |

## 02 硬件与资源约束

| 文件 | 定位 |
|---|---|
| `PIO_RESOURCE_PLAN.md` | PIO、State Machine、DMA、GPIO 和语义 IO 资源规划。 |
| `BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md` | BiSS-C TAP Bridge、RJ45、SYNC_IO、AUX 两收两发和外围电路约束。 |

## 03 触发与 SYNC_IO

| 文件 | 定位 |
|---|---|
| `SYNC_IO_REFACTOR_PLAN.md` | SYNC_IO 硬件 profile + 多模式重构计划。 |
| `SYNC_TRIGGER_TODO.md` | 触发系统生产化待办。 |
| `TRIGGER_SEQ_STEP_MODE.md` | 序列步进触发模式设计。 |
| `TRIGGER_ENC_COUNT_MODE.md` | 编码器计数触发模式设计。 |
| `TRIGGER_PULSE_COUNT_ANALYSIS.md` | 脉冲计数分析。 |
| `TRIGGER_INDUSTRIAL_ENHANCEMENT.md` | 工业级触发增强方案。 |
| `DISTRIBUTED_DPLL_SYNC_DESIGN.md` | 多板分布式 DPLL / CAL_RING 同步设计。 |

## 04 BiSS-C

| 文件 | 定位 |
|---|---|
| `BISSC_TAP_BRIDGE_DESIGN.md` | BiSS-C TAP Bridge 协议、模式和固件架构主设计。 |
| `BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md` | BiSS-C 相关硬件和 SYNC_IO 外围电路约束。 |
| `BISSC_IMPLEMENTATION_TODO.md` | BiSS-C 实现待办，按 P0/P1/P2 细分。 |
| `BISSC_TASK_PROGRESS.md` | BiSS-C 任务进度、闭环验证和决策记录。 |

## 05 OTA 与启动

| 文件 | 定位 |
|---|---|
| `OTA方案.md` | 现有 OTA 主方案历史文件。后续迁移建议见命名规则文档。 |
| `OTA_TODO.md` | OTA 产品化待办。 |
| `OTA_AB_SWITCH_DESIGN.md` | Direct A/B 切换设计。 |
| `OTA_COPY_TRANSACTION_DESIGN.md` | Copy-to-active 掉电恢复事务设计。 |
| `PORTABLE_OTA_ARCHITECTURE.md` | Portable OTA 架构和复用方案。 |
| `OTA_OPEN_SOURCE_COMPARISON.md` | OTA 开源方案对比。 |
| `OTA_LIBRARY_MIGRATION_PLAYBOOK.md` | Portable OTA 库化迁移 playbook。 |

## 06 存储与 SD

| 文件 | 定位 |
|---|---|
| `SD_TODO.md` | SD 文件系统、StorageAO 和持久化观测层设计/待办。 |
| `TASK_PROGRESS_SD.md` | SD 域任务进度和验证记录。 |

## 07 诊断、日志与 SCPI

| 文件 | 定位 |
|---|---|
| `SCPI_COMMANDS.md` | SCPI 命令清单、语义和边界。 |
| `LOG_SYSTEM_TODO.md` | 日志系统待办和演进方向。 |

## 08 发布、验证与全局进度

| 文件 | 定位 |
|---|---|
| `RELEASE_CHECKLIST.md` | 发布门禁检查表。 |
| `TASK_PROGRESS.md` | 全局历史任务进度。新域建议使用 `<DOMAIN>_TASK_PROGRESS.md`。 |

## 快速查找规则

- 查系统边界：先读 `HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md`。
- 查 IO/PIO 资源：先读 `PIO_RESOURCE_PLAN.md`，再读具体域设计。
- 查 SYNC_IO 当前代码重构：先读 `SYNC_IO_REFACTOR_PLAN.md`。
- 查 BiSS-C：先读 `BISSC_TAP_BRIDGE_DESIGN.md` 和
  `BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`。
- 查命令：先读 `SCPI_COMMANDS.md`。
- 查待办：优先读对应域的 `*_TODO.md`。
- 查验证记录：优先读对应域的 `*_TASK_PROGRESS.md`。
