# Watchdog 与故障证据子域

Status: Draft
Domain: WATCHDOG
Canonical: `docs/watchdog/README.md`
Related: `docs/watchdog/ARCH_WATCHDOG_ARCHITECTURE.md`, `docs/watchdog/ARCH_WATCHDOG_TODO.md`, `docs/watchdog/ARCH_WATCHDOG_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_HAOFV_ARCHITECTURE.md`, `docs/storage/LOG_SYSTEM_TODO.md`
Last updated: 2026-09-06

`docs/watchdog/` 是硬件 watchdog、软件健康监督、复位后故障证据和故障注入验证的子域入口。
它不拥有 OTA、Flash、RTOS、日志或 SCPI 的业务状态；这些域只向 Watchdog 发布受限的心跳、阶段和故障证据。

## 文件分工

| 文件 | 唯一职责 |
|---|---|
| `ARCH_WATCHDOG_ARCHITECTURE.md` | 稳定语义、owner 边界、不变量、证据格式和恢复模型 |
| `ARCH_WATCHDOG_TODO.md` | 分阶段任务、状态和退出门禁 |
| `ARCH_WATCHDOG_TASK_PROGRESS.md` | 构建、测试、板端验收、失败和回退的原始证据索引 |

## 当前实现落点

| 层 | 当前路径 | 目标职责 |
|---|---|---|
| 硬件适配 | `drivers/mcu/watchdog/inc/`, `drivers/mcu/watchdog/src/` | enable/feed/reboot、reset reason、retained scratch |
| 监督与健康门禁 | `components/diagnostics/inc/`, `components/diagnostics/src/` | task heartbeat、required mask、supervisor、stale 判定 |
| OTA 阶段证据 | `components/ota_manager/`, `middleware/portable_ota_port/`, `components/flash_transaction/` | OTA/Flash 非阻塞 phase breadcrumb 和 progress telemetry |
| 外部查询 | `middleware/scpi_port/`, `tools/watchdog_monitor/` | USB/SCPI 查询、复位后采样和证据归档 |

后续可将监督器从 `components/diagnostics` 拆为独立 `components/watchdog_supervisor/`，但在 owner、接口和硬件验收门禁冻结前不提前搬迁代码。
