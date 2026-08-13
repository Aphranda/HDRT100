# 架构域

Status: Active
Domain: ARCH
Canonical: `docs/arch/README.md`
Related: `docs/README.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 HAOFV 顶层架构、产品架构特化、RTOS、双核 AMP 和分布式触发总纲的目标入口。HAOFV 是系统最高层架构文档，定义 owner、层次、约束传递、Vector/Blackboard 和服务边界；具体硬件 pin map、电源、隔离和网表事实下沉到 `docs/hardware/`。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `HAOFV_ARCHITECTURE.md` | HAOFV 顶层架构入口，定义组件约束、层次逻辑和跨域 owner |
| `HAOFV_VDC_DPLL_ARCHITECTURE.md` | HAOFV 下 VDC/DPLL 核心基础架构，定义共同时间事实、同步 DPLL、角度 DPLL 和预测分发边界 |
| `ARCH_PRODUCT_ARCHITECTURE.md` | DTC100/RP2350_TRIG 产品化系统架构特化，服从 HAOFV 顶层约束 |
| `HAOFV_IMPLEMENTATION_PLAYBOOK.md` | HAOFV 实施指南 |
| `HAOFV_PORTABILITY_EVALUATION.md` | HAOFV 可移植性评估 |
| `RTOS_PORTING_PLAN.md` | RTOS / OSAL 迁移计划 |
| `RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` | RTOS + 双核 AMP 分区和 SCPI 自上而下适配 |
| `RTOS_DISTRIBUTED_TRIGGER_TASK_PROGRESS.md` | RTOS / 双核 / 分布式触发任务进度 |
| `RTOS_DISTRIBUTED_TRIGGER_0614_SUMMARY.md` | 0614 分布式触发摘要 |
| `MULTICORE_PARTITION_PLAN.md` | RP2350 双核分区计划 |

## 边界

- 架构域说明 owner、层次、资源仲裁和长期原则。
- HAOFV 不维护具体 PCB 资源表；板级约束见 `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` 和 `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`。
- 具体 SCPI 命令放 `interface/`，具体实时执行放 `trigger/`。
