# 校准域

Status: Draft
Domain: CALIBRATION
Canonical: `docs/calibration/README.md`
Related: `docs/README.md`, `docs/interface/DTC100_SCPI_COMMAND_PLANNING.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 CAL link、delay、校准参数、active/staging、版本和质量管理的目标入口。

## 当前状态

| 当前路径 | 定位 |
|---|---|
| `../interface/DTC100_SCPI_COMMAND_PLANNING.md` | 当前 CALibration 指令和边界定义 |
| `../arch/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` | `task_calibration` owner、slot 和 ACK/NACK 闭环 |
| `../interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md` | 对外 CALibration 指令表 |

## 待补 canonical

- 需要新增 `CALIBRATION_LINK_DELAY_DESIGN.md`，从 SCPI/RTOS 文档中抽出 link、parameter、quality 和 version 细节。
