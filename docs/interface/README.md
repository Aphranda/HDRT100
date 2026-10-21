# 接口域

Status: Active
Domain: SCPI
Canonical: `docs/interface/README.md`
Related: `docs/README.md`, `docs/interface/SCPI_COMMAND_PLAN.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-10-20

本目录是对外接口、SCPI、USBTMC、USB488、上位机指令表和权限策略的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `SCPI_COMMAND_PLAN.md` | DHRT100 SCPI 主域规划和指令树 |
| `SCPI_COMMANDS.md` | 当前固件 SCPI 命令清单 |
| `RP1200波导天线测试系统分布式触发方案SCPI指令表.md` | 产品级 SCPI 指令表 Markdown 源文档 |
| `SCPI_USB_INTERFACE_DESIGN.md` | USB CDC/USBTMC/USB488 接口设计 |
| `SCPI_TASK_PROGRESS.md` | SCPI 指令框架、验证脚本和接口拆分闭环记录 |
| `SCPI_HOST_ARCHITECTURE.md` | 上位机与多板系统的通信边界、单串口 + RefMem 拓扑（Draft） |
| `SCPI_HOST_TODO.md` | 接口域待办：会话/寻址、转发、权限、可观测性（IF-001..IF-010） |
| `SCPI_HOST_TASK_PROGRESS.md` | 接口域建域、现状取证与评审记录 |
| `SCPI_HAOFV_REVIEW.md` | 2026-10-20 HAOFV 符合性评审：实现面与契约面不符合项、证据和处置顺序（非契约） |

## 边界

- 接口域定义上位机可见契约，不直接描述硬件动作实现。
- SCPI 写命令只表示 accepted，完成态由 ACK/NACK、`READ:*?` 或 `SYSTem:*?` 闭环。
- 维护命令与产品命令同受 HAOFV 约束，不设“维护强操作”例外；硬件动作由 owner 执行。
- 当前实现与 HAOFV 的偏差、证据和处置顺序见 `SCPI_HAOFV_REVIEW.md`；评审快照不构成契约。
