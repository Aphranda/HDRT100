# Watchdog 与故障证据子域待办

Status: Draft
Domain: WATCHDOG
Canonical: `docs/watchdog/ARCH_WATCHDOG_TODO.md`
Related: `docs/watchdog/README.md`, `docs/watchdog/ARCH_WATCHDOG_ARCHITECTURE.md`, `docs/watchdog/ARCH_WATCHDOG_TASK_PROGRESS.md`, `docs/ota/OTA_TODO.md`, `docs/arch/RTOS_HAOFV_TODO.md`
Last updated: 2026-09-06

本文只维护 Watchdog 子域任务状态；稳定语义见 `ARCH_WATCHDOG_ARCHITECTURE.md`，板端和失败证据见 `ARCH_WATCHDOG_TASK_PROGRESS.md`。

## 状态规则

任务状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`。代码或 host test 通过但未完成板端验收的任务不得标记为 `DONE`。

## 目录迁移目标

```text
drivers/mcu/watchdog/                 # 硬件 adapter，保留最小 API
components/watchdog_supervisor/       # 目标：唯一 feed owner、health gate、fault record
components/diagnostics/               # 只读投影、兼容 SCPI/status 接口
tools/watchdog_monitor/               # 复位后采样、五板探针、证据打包
tests/unit/test_watchdog_*.c          # supervisor/record/故障注入
tests/python/test_watchdog_*.py       # 工具、解析和证据 schema
docs/watchdog/                        # 本子域三件套
```

## 任务表

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| WDT-ARCH-001 | 冻结 hardware adapter / supervisor / projection 三层 owner 边界 | IN PROGRESS | 架构文档、代码入口和禁止依赖完成交叉审查 |
| WDT-ARCH-002 | 定义 reset record、heartbeat、phase breadcrumb 的版本化 schema | PENDING | host 正反解析测试；字段有保留空间和 CRC/完整性策略 |
| WDT-SUP-001 | 将 feed 权限收敛到唯一 supervisor，清理业务路径直接 feed | PENDING | 静态扫描 + 单测 + 运行态状态查询证明无旁路 feed |
| WDT-SUP-002 | 将 supervisor 与 OTA/Flash 阻塞链隔离 | PENDING | 注入 Flash lockout、RTOS stall、BCB/END stall；关键 stale 均能有界复位 |
| WDT-EVID-001 | 关键 OTA/Flash 阶段改为 BEGIN/DONE breadcrumb | IN PROGRESS | END/BOOT/BCB/commit 每一步均可由 USB 查询或 reset record 区分 |
| WDT-EVID-002 | 扩展 retained record：task、core、phase、PC/LR 或最小 fault frame | PENDING | 复位后记录完整性、版本迁移和未知字段兼容测试通过 |
| WDT-EVID-003 | 形成 USB 主诊断 + SD 补充 fault bundle | PENDING | USB 不可用时 retained record 仍可读；SD 写失败不改变复位判定 |
| WDT-TEST-001 | 建立 supervisor/heartbeat/lockout/timeout 故障注入矩阵 | PENDING | 每类故障有 expected reset classification、stale mask 和 recovery gate |
| WDT-HIL-001 | 最新 UF2 刷入五板并执行 P3 硬件验收 | IN PROGRESS | 五板源码指纹一致、P3 凭证绑定当前构建、原始输出归档 |
| WDT-HIL-002 | 五板执行 OTA END→BOOT→pending→COMM 闭环 | PENDING | 每板 reset/re-enumeration/slot/commit 时间线完整，失败不得用旧证据覆盖 |
| WDT-OPS-001 | 完善 watchdog_monitor 和 post-reset 自动采样 | PENDING | NO 号自动发现、COM 映射、重试和 JSON/CSV 证据包稳定 |

## 分阶段门禁

```text
P0 owner/schema
  -> P1 supervisor/feed isolation
  -> P2 OTA/Flash breadcrumb
  -> P3 host fault injection
  -> P4 five-board UF2 + P3
  -> P5 OTA/reset/commit HIL
  -> P6 long-run and release gate
```

任何阶段失败都保留原始证据并回到本阶段；不得通过增大 timeout、屏蔽 stale bit 或只查询“当前状态”来宣称通过。

## 当前阻塞项

- supervisor 目前仍与 `components/diagnostics` 共享实现，尚未完成独立 owner 拆分。
- retained scratch 容量有限，PC/LR、stack watermark 和 fault bundle 仍未形成版本化格式。
- 五板最新 UF2、P3 验收和 OTA 闭环必须使用同一源码指纹；旧 build 证据不可替代。
