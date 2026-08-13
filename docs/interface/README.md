# 接口域

Status: Active
Domain: SCPI
Canonical: `docs/interface/README.md`
Related: `docs/README.md`, `docs/interface/DTC100_SCPI_COMMAND_PLANNING.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是对外接口、SCPI、USBTMC、USB488、上位机指令表和权限策略的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `DTC100_SCPI_COMMAND_PLANNING.md` | DTC100 SCPI 主域规划和指令树 |
| `SCPI_COMMANDS.md` | 当前固件 SCPI 命令清单 |
| `RP1200波导天线测试系统分布式触发方案SCPI指令表.md` | 产品级 SCPI 指令表 Markdown 源文档 |
| `SCPI_USB_INTERFACE_DESIGN.md` | USB CDC/USBTMC/USB488 接口设计 |
| `SCPI_TASK_PROGRESS.md` | SCPI 指令框架、验证脚本和接口拆分闭环记录 |

## 边界

- 接口域定义上位机可见契约，不直接描述硬件动作实现。
- SCPI 写命令只表示 accepted，完成态由 ACK/NACK、`READ:*?` 或 `SYSTem:*?` 闭环。
