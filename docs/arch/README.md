# 架构域

Status: Active
Domain: ARCH
Canonical: `docs/arch/README.md`
Related: `docs/README.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 HAOFV 顶层架构、产品架构特化、RTOS、双核 AMP 和分布式触发总纲的目标入口。HAOFV 是系统最高层架构文档，定义 owner、层次、约束传递、Vector/Blackboard 和服务边界；具体硬件 pin map、电源、隔离和网表事实下沉到 `docs/hardware/`。

## 当前主线

| 当前路径 | 定位 | 使用规则 |
|---|---|
| `HAOFV_ARCHITECTURE.md` | HAOFV 顶层架构入口，定义组件约束、层次逻辑和跨域 owner。 | 最高层架构真相；不写具体 PCB pin map。 |
| `HAOFV_VDC_DPLL_ARCHITECTURE.md` | HAOFV 下 VDC/DPLL 核心基础架构，定义共同时间事实、同步 DPLL、角度 DPLL 和预测分发边界。 | VDC/DPLL 的当前架构入口；`sync/` 中的 DPLL 文档作为落地方案和历史设计输入。 |
| `ARCH_PRODUCT_ARCHITECTURE.md` | DTC100/RP2350_TRIG 产品化系统架构特化，服从 HAOFV 顶层约束。 | 产品目标、四板角色、发布门禁和跨域契约入口。 |
| `RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` | RTOS + 双核 AMP 分区、SCPI 自上而下适配、反射内存和小步落地待办。 | 当前 RTOS/双核/分布式触发实施主线。 |
| `RTOS_DISTRIBUTED_TRIGGER_TASK_PROGRESS.md` | RTOS / 双核 / 分布式触发任务进度。 | 闭环验证记录入口；代码任务完成后追加。 |

## 支撑与阶段性文档

| 当前路径 | 定位 | 当前约束 |
|---|---|---|
| `HAOFV_IMPLEMENTATION_PLAYBOOK.md` | HAOFV 实施指南、ECC 示例、Flash Job 示例和历史 GPIO 迁移样例。 | 不作为硬件资源 canonical；具体 IO 以 `docs/hardware/` 和 `docs/sync/SYNC_IO_RESOURCE_PLAN.md` 为准。 |
| `HAOFV_PORTABILITY_EVALUATION.md` | 2026-06/07 代码基线下的可移植性评估。 | 作为迁移风险快照使用；不覆盖当前 RTOS + 双核 AMP 产品架构。 |
| `RTOS_PORTING_PLAN.md` | FreeRTOS / OSAL 迁移计划。 | 作为 RTOS 引入和 OSAL 边界依据；产品化任务划分以 `RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` 为准。 |
| `MULTICORE_PARTITION_PLAN.md` | RP2350 双核 bring-up、裸机双核和 AMP 边界说明。 | 作为双核底层隔离依据；产品化 release 以 RTOS + 双核 AMP 主线为准。 |
| `RTOS_DISTRIBUTED_TRIGGER_0614_SUMMARY.md` | 0614 分布式触发报告摘要。 | 历史设计输入；0804 报告和当前 RTOS 分区文档承接产品化实现。 |

## 当前阅读顺序

1. `HAOFV_ARCHITECTURE.md`：先确认 HAOFV owner、层次、Vector/Blackboard、TRIGger/REALtime 分层。
2. `HAOFV_VDC_DPLL_ARCHITECTURE.md`：再确认 timestamp、VDC、SYNC DPLL、Angle DPLL、T2 和预测分发链。
3. `ARCH_PRODUCT_ARCHITECTURE.md`：确认 DTC100 产品角色、四板运行模型、数据契约和发布门禁。
4. `RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`：确认当前 RTOS task、core0/core1、SCPI 到反射内存再到 owner 状态机的落地路径。
5. `RTOS_DISTRIBUTED_TRIGGER_TASK_PROGRESS.md`：查看已经完成的小步验证和剩余任务。

## 边界

- 架构域说明 owner、层次、资源仲裁和长期原则。
- HAOFV 不维护具体 PCB 资源表；板级约束见 `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` 和 `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`。
- 具体 SCPI 命令放 `interface/`；产品业务动作放 `trigger/`；底层实时维护能力放 `REALtime` 指令域和 `sync/`、`trigger/` 的实现约束中。
- VDC/DPLL 是 HAOFV 核心基础设施，不作为裸顶级业务域；`SYNC DPLL` 维护共同时间，`Angle DPLL` 生成扫描预测时间，两者不得混用。
