# 通信域

Status: Active
Domain: BISSC
Canonical: `docs/communication/README.md`
Related: `docs/README.md`, `docs/interface/DTC100_SCPI_COMMAND_PLANNING.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 BiSS-C、UART、RS485、RJ45 后端维护和通信 validation 的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `BISSC_TAP_BRIDGE_DESIGN.md` | BiSS-C TAP Bridge 协议和固件架构 |
| `BISSC_IMPLEMENTATION_TODO.md` | BiSS-C 实现待办 |
| `BISSC_TASK_PROGRESS.md` | BiSS-C 任务进度和验证记录 |
| `BISSC_NETWORK_LOOPBACK_PLAYBOOK.md` | BiSS 组网 preflight 和跨电脑交接 |
| `BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md` | BiSS-C 与 SYNC_IO 外围电路约束 |
| `BISSC_SYNC_IO_HARDWARE_ARCHITECTURE.html` | BiSS-C / SYNC_IO 硬件架构图 HTML |

## 边界

- BiSS、UART、RS485 统一归入 `COMMunication:*`。
- USB 是 SCPI 传输与 validation 模式切换，保留在 `SYSTem:USB:*`，不迁入通信域。
