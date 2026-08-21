# 校准域

Status: Draft
Domain: CALIBRATION
Canonical: `docs/calibration/README.md`
Related: `docs/README.md`, `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/interface/SCPI_COMMAND_PLAN.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-21

本目录是 CAL link、物理 topology、delay、校准参数、active/staging、版本和质量管理的
目标入口。有向线序/邻接矩阵、环路顺序、slot map、训练物理测量与接受门禁属于本域；
TDMA 只承载隔离 probe/训练 persona 和实时资源编排。

## 当前状态

| 当前路径 | 定位 |
|---|---|
| `../interface/SCPI_COMMAND_PLAN.md` | 当前 CALibration 指令和边界定义 |
| `../arch/RTOS_HAOFV_ARCHITECTURE.md` | `task_calibration` owner、slot 和 ACK/NACK 闭环 |
| `../interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md` | 对外 CALibration 指令表 |
| `CALIBRATION_TDMA_CLK_TRAINING_PLAN.md` | 多板 TDMA SPI CLK 训练、双向时间传递、residence/bias/path-delay 证据和 EtherCAT DC 风格门禁。 |
| `CALIBRATION_DOMAIN_TODO.md` | 校准域分阶段待办、跨域边界、验收门禁和八节点扩展准备。 |
| `CALIBRATION_TASK_PROGRESS.md` | 校准域方案、粗捕获、编码测距和双向测距的任务记录与证据规则。 |

校准域 host 工具统一使用 `calibration_*` 命名并放在
`tools/calibration_ring_validate/`。当前包括物理 topology、第一阶段 CLK bracket 和码本
评估工具；`tools/tdma_ring_monitor/` 只保留 TDMA transport/runtime 工具。

## 待补 canonical

- 需要新增 `CALIBRATION_LINK_DELAY_DESIGN.md`，从 SCPI/RTOS 文档中抽出通用 link、parameter、quality 和 version 细节；TDMA CLK 训练方案已由本目录承接。
