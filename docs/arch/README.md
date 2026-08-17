# 架构域

Status: Active
Domain: ARCH
Canonical: `docs/arch/README.md`
Related: `docs/README.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 HAOFV 顶层架构、产品架构特化、RTOS 和分布式触发总纲的目标入口。HAOFV 是系统最高层架构文档，定义 owner、层次、约束传递、Vector/Blackboard 和服务边界；具体硬件 pin map、电源、隔离和网表事实下沉到 `docs/hardware/`。

## 当前主线

| 当前路径 | 定位 | 使用规则 |
|---|---|
| `HAOFV_ARCHITECTURE.md` | HAOFV 顶层架构入口，定义组件约束、层次逻辑和跨域 owner。 | 最高层架构真相；不写具体 PCB pin map。 |
| `HAOFV_MAINTENANCE_TODO.md` | HAOFV 架构符合性维护待办，记录 owner、AO/FB/Vector、反射内存和硬实时边界偏差。 | 架构偏差和未建功能域的独立追踪入口；不记录普通开发流水账。 |
| `../tdma/TDMA_DOMAIN_ARCHITECTURE.md` | TDMA Foundation 内部基础主域架构。 | TDMA 主域 canonical 入口；定义上/下行 runtime、payload registry、adapter、ring completion evidence 和 HAOFV system node 边界。 |
| `../vdc/VDC_DOMAIN_ARCHITECTURE.md` | VDC 内部主域架构。 | VDC 主域 canonical 入口；定义共同时间事实、SYNC DPLL、HOLDOVER、timestamp、质量门禁和 RefMem 映射边界。 |
| `HAOFV_VDC_DPLL_ARCHITECTURE.md` | HAOFV 下 VDC/DPLL 既有融合架构输入。 | 后续逐步迁入 VDC canonical；`sync/` 中的 DPLL 文档作为落地方案和历史设计输入。 |
| `ARCH_PRODUCT_ARCHITECTURE.md` | Distributed Hard Real-Time Trigger System 产品化系统架构特化，服从 HAOFV 顶层约束。 | 产品目标、四板角色、发布门禁和跨域契约入口。 |
| `ARCH_FUTURE_APPLICATION_PLAN.md` | Distributed Hard Real-Time Trigger System 未来应用路线图。 | 当前产品完成后的平台化、跨行业、跨平台和开源生态规划；不作为近期实现待办。 |
| `RTOS_HAOFV_ARCHITECTURE.md` | 基于 HAOFV 的 RTOS + 双核 AMP 架构，整合原 RTOS 分区、OSAL 移植、双核方案和 0614 摘要。 | 当前 RTOS / 双核 / 分布式触发唯一架构入口；不再保留独立双核方案。 |
| `RTOS_HAOFV_TODO.md` | 基于 HAOFV 的 RTOS 实施待办。 | 只维护未完成事项；不写验证流水账。 |
| `RTOS_HAOFV_TASK_PROGRESS.md` | 基于 HAOFV 的 RTOS 任务进度。 | 闭环验证记录入口；代码任务完成后追加。 |
| `../refmem/REFMEM_DOMAIN_ARCHITECTURE.md` | Distributed RefMem 内部主域架构。 | RefMem 主域 canonical 入口；定义 A0-A7 通用节点、静态分布式应用模型和 64 KB DistributedVectorTable 边界。 |

## 支撑与阶段性文档

| 当前路径 | 定位 | 当前约束 |
|---|---|---|
| `HAOFV_IMPLEMENTATION_PLAYBOOK.md` | HAOFV 实施指南、ECC 示例、Flash Job 示例和历史 GPIO 迁移样例。 | 不作为硬件资源 canonical；具体 IO 以 `docs/hardware/` 和 `docs/sync/SYNC_IO_ARCHITECTURE.md` 为准。 |
| `HAOFV_PORTABILITY_EVALUATION.md` | 2026-06/07 代码基线下的可移植性评估。 | 作为迁移风险快照使用；不覆盖当前 RTOS + 双核 AMP 产品架构。 |
| `HAOFV_ARCHITECTURE_RISK_EVALUATION.md` | HAOFV 顶层架构独立风险评估快照。 | 记录 owner、跨核契约、硬实时边界、ECC 状态机规模和恢复路径风险，含事实校正与处置去向。 |

## 当前阅读顺序

1. `HAOFV_ARCHITECTURE.md`：先确认 HAOFV owner、层次、Vector/Blackboard、TRIGger/REALtime 分层。
2. `HAOFV_MAINTENANCE_TODO.md`：查看当前代码相对 HAOFV 的偏差、未建主域和推进顺序。
3. `../tdma/TDMA_DOMAIN_ARCHITECTURE.md`：确认上/下行 TDMA、payload registry、adapter 和 ring completion evidence 基础件边界。
4. `../vdc/VDC_DOMAIN_ARCHITECTURE.md`：再确认 VDC 内部主域、共同时间、SYNC DPLL、HOLDOVER、timestamp 和质量门禁。
5. `HAOFV_VDC_DPLL_ARCHITECTURE.md`：查看既有 VDC/DPLL 融合架构输入和迁移前细节。
6. `ARCH_PRODUCT_ARCHITECTURE.md`：确认 DTC100 产品角色、四板运行模型、数据契约和发布门禁。
7. `ARCH_FUTURE_APPLICATION_PLAN.md`：了解当前产品完成后的平台化、跨平台和开源生态方向。
8. `../refmem/REFMEM_DOMAIN_ARCHITECTURE.md`：确认 Distributed RefMem 内部主域、A0-A7 通用节点、静态分布式模型和 slot 边界。
9. `RTOS_HAOFV_ARCHITECTURE.md`：确认当前 RTOS task、core0/core1、SCPI 到反射内存再到 owner 状态机的落地路径。
10. `RTOS_HAOFV_TODO.md`：查看 RTOS / 双核 / 反射内存 / VDC 的未完成实施事项。
11. `RTOS_HAOFV_TASK_PROGRESS.md`：查看已经完成的小步验证。

## 边界

- 架构域说明 owner、层次、资源仲裁和长期原则。
- HAOFV 不维护具体 PCB 资源表；板级约束见 `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` 和 `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`。
- 具体 SCPI 命令放 `interface/`；产品业务动作放 `trigger/`；底层实时维护能力放 `REALtime` 指令域和 `sync/`、`trigger/` 的实现约束中。
- TDMA Foundation 是 HAOFV 内部基础主域，不作为裸顶级业务域；VDC、RefMem、Trigger 和 OTA 只能注册 payload、提交 intent 或读取 snapshot，不直接拥有上/下行 transport。
- VDC Domain 是 HAOFV 内部基础主域，不作为裸顶级业务域；`SYNC DPLL` 维护共同时间，`Angle DPLL` 生成扫描预测时间，两者不得混用。
