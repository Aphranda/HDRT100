# 文档索引

Status: Active
Domain: Documentation
Canonical: `docs/README.md`
Related: `docs/DOCS_NAMING_STRUCTURE_PLAN.md`
Last updated: 2026-07-07

本文档是 `docs/` 的总入口。新文档的命名、层级、元数据和迁移规则以
`DOCS_NAMING_STRUCTURE_PLAN.md` 为准。

当前阶段已经完成平铺文档命名迁移，暂不引入 `docs/<domain>/` 子目录。
后续需要移动目录时，应按域小批量迁移，并同步更新所有引用。

## Canonical 主文档

| 领域 | 当前 canonical 主文档 | 说明 |
|---|---|---|
| ARCH/HAOFV | `HAOFV_ARCHITECTURE.md` | 顶层 HAOFV 架构入口。 |
| SYNC_IO | `SYNC_IO_RESOURCE_PLAN.md` | PIO、GPIO、DMA、语义 IO 和硬实时资源约束入口。 |
| TRIGGER | `TRIGGER_SYNC_TODO.md` | 触发业务模式、生产化缺口和跨模式待办入口。 |
| BISSC | `BISSC_TAP_BRIDGE_DESIGN.md` | BiSS-C 协议、TAP bridge、固件 persona 和验证边界入口。 |
| OTA | `OTA_SYSTEM_DESIGN.md` | 历史 OTA 主方案入口；后续迁移方向见 `DOCS_NAMING_STRUCTURE_PLAN.md`。 |
| SD | `SD_TODO.md` | SD、StorageAO、System Pack、快照和持久化观测入口。 |
| LOG | `LOG_SYSTEM_TODO.md` | 日志 core、诊断 trace、持久化和故障证据入口。 |
| SCPI | `SCPI_COMMANDS.md` | SCPI 命令语义、兼容性和用户可调用接口入口。 |

## 进度记录路由

| 领域 | 当前进度入口 | 规则 |
|---|---|---|
| BiSS-C | `BISSC_TASK_PROGRESS.md` | BiSS-C 新任务记录写入本文件。 |
| SYNC_IO | `SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构闭环记录写入本文件。 |
| SD | `SD_TASK_PROGRESS.md` | SD / StorageAO / System Pack 新任务记录写入本文件。 |
| Documentation | `DOCS_MIGRATION_TODO.md` | 文档治理和迁移记录写入本文档体系待办。 |
| 其他领域 | 新建或补齐 `<DOMAIN>_TASK_PROGRESS.md` | 后续新闭环记录优先建立领域进度文件，不再追加到全局历史文件。 |
| 全局历史 | `TASK_PROGRESS.md` | 只保留跨域历史和迁移前记录；除跨域总览外不再作为默认新任务入口。 |

## 00 文档治理

| 文件 | 定位 |
|---|---|
| `DOCS_NAMING_STRUCTURE_PLAN.md` | 文档命名格式、层级关系、新增文件规则和迁移规则。 |
| `DOCS_MIGRATION_TODO.md` | 文档体系迁移待办，跟踪元数据补齐、历史改名和索引维护。 |
| `README.md` | 本索引文件，提供当前 `docs/` 文件归属。 |

## 01 系统架构

| 文件 | 定位 |
|---|---|
| `HAOFV_ARCHITECTURE.md` | HAOFV 顶层产品架构主文档。 |
| `HAOFV_IMPLEMENTATION_PLAYBOOK.md` | HAOFV 实施补充、示例和迁移说明。 |
| `HAOFV_PORTABILITY_EVALUATION.md` | HAOFV 可移植性评估。 |
| `RTOS_PORTING_PLAN.md` | FreeRTOS/OSAL 迁移计划。 |
| `MULTICORE_PARTITION_PLAN.md` | RP2350 双核分区计划。 |

## 02 硬件与资源约束

| 文件 | 定位 |
|---|---|
| `SYNC_IO_RESOURCE_PLAN.md` | PIO、State Machine、DMA、GPIO 和语义 IO 资源规划。 |
| `BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md` | BiSS-C TAP Bridge、RJ45、SYNC_IO、AUX 两收两发和外围电路约束。 |

## 03 触发与 SYNC_IO

| 文件 | 定位 |
|---|---|
| `SYNC_IO_REFACTOR_PLAN.md` | SYNC_IO 硬件 profile + 多模式重构计划。 |
| `TRIGGER_SYNC_TODO.md` | 触发系统生产化待办。 |
| `TRIGGER_SEQ_STEP_DESIGN.md` | 序列步进触发模式设计。 |
| `TRIGGER_ENC_COUNT_DESIGN.md` | 编码器计数触发模式设计。 |
| `TRIGGER_PULSE_COUNT_ANALYSIS.md` | 脉冲计数分析。 |
| `TRIGGER_INDUSTRIAL_ENHANCEMENT_DESIGN.md` | 工业级触发增强方案。 |
| `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` | 多板分布式 DPLL / CAL_RING 同步设计。 |
| `SYNC_IO_ARCH_REVIEW_TODO.md` | SYNC_IO 架构评审待办，跟踪重构中途发现的架构债务。 |
| `SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构任务进度和闭环验证记录。 |

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
| `OTA_SYSTEM_DESIGN.md` | 现有 OTA 主方案历史文件。后续迁移建议见命名规则文档。 |
| `OTA_TODO.md` | OTA 产品化待办。 |
| `OTA_AB_SWITCH_DESIGN.md` | Direct A/B 切换设计。 |
| `OTA_COPY_TRANSACTION_DESIGN.md` | Copy-to-active 掉电恢复事务设计。 |
| `OTA_PORTABLE_ARCHITECTURE.md` | Portable OTA 架构和复用方案。 |
| `OTA_OPEN_SOURCE_COMPARISON.md` | OTA 开源方案对比。 |
| `OTA_LIBRARY_MIGRATION_PLAYBOOK.md` | Portable OTA 库化迁移 playbook。 |

## 06 存储与 SD

| 文件 | 定位 |
|---|---|
| `SD_TODO.md` | SD 文件系统、StorageAO 和持久化观测层设计/待办。 |
| `SD_TASK_PROGRESS.md` | SD 域任务进度和验证记录。 |

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

- 查系统边界：先读 `HAOFV_ARCHITECTURE.md`。
- 查 IO/PIO 资源：先读 `SYNC_IO_RESOURCE_PLAN.md`，再读具体域设计。
- 查 SYNC_IO 当前代码重构：先读 `SYNC_IO_REFACTOR_PLAN.md`。
- 查 BiSS-C：先读 `BISSC_TAP_BRIDGE_DESIGN.md` 和
  `BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`。
- 查命令：先读 `SCPI_COMMANDS.md`。
- 查待办：优先读对应域的 `*_TODO.md`。
- 查验证记录：优先读对应域的 `*_TASK_PROGRESS.md`。
