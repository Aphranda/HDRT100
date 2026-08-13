# 测量域

Status: Draft
Domain: MEASURE
Canonical: `docs/measure/README.md`
Related: `docs/README.md`, `docs/DTC100_SCPI_COMMAND_PLANNING.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是测量原语、T2 摘要、链路 delay 测量服务和诊断测量接口的目标入口。

## 当前状态

| 当前路径 | 定位 |
|---|---|
| `../DTC100_SCPI_COMMAND_PLANNING.md` | `MEASure:*?` 边界和指令树 |
| `../arch/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` | measurement service、T2 和报告证据关系 |

## 待补 canonical

- 需要新增 `MEASURE_SERVICE_DESIGN.md`，明确 `MEASure:LINK:DELay?`、`MEASure:T2?` 与 CAL/SYNC/storage 的边界。
