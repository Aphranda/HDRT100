# RP1200波导天线分布式触发方案 0614 摘要

Status: Active
Domain: RTOS
Canonical: `docs/arch/RTOS_DISTRIBUTED_TRIGGER_0614_SUMMARY.md`
Related: `docs/reports/distributed-trigger/RTOS_DISTRIBUTED_TRIGGER_0614_REPORT.html`, `docs/trigger/RP2350B_FOUR_BOARD_DISTRIBUTED_TRIGGER_SCHEME.md`, `docs/sync/SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md`
Last updated: 2026-08-13

这是一份仓库内摘要，用来替代外部 `DOC/0614.html` 路径引用。完整原始报告已补入
`docs/reports/distributed-trigger/RTOS_DISTRIBUTED_TRIGGER_0614_REPORT.html`；本文只保留当前工程会复用的设计输入。

> 当前定位：本文档是历史设计输入摘要，不是当前产品化 RTOS 分区或 VDC/DPLL 细节入口。
> 当前落地以 `docs/arch/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` 为准；VDC/DPLL 的 HAOFV
> 链式定义以 `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` 为准；0804 完整报告位于
> `docs/reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.html`。

## 设计要点

- DPLL 作为下位机时间主站，生成 `T_fire_base`。
- 虚拟 DC 时间轴用于统一多板时间认知。
- 预约触发只传未来动作，不串行传递实时边沿。
- `T2_i` / READY 回读用于验证实际动作残差。
- 反馈校准和时间补偿要和业务动作分层，不能混在实时边沿路径里。

## 对当前工程的意义

- 支持 `task_dpll`、`task_vdc_sync`、`task_loop_engine` 的分层设计。
- 支持 `SYST:CFG:STAT?` 的配置门禁和 `SYST:RTOS:STAT?` 的任务分解。
- 支持后续 `FIRE_LOAD`、`T2` 和 `e_act` 闭环验证。
