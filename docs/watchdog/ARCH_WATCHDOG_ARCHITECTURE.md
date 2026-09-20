# Watchdog 与故障证据子域架构

Status: Draft
Domain: WATCHDOG
Canonical: `docs/watchdog/ARCH_WATCHDOG_ARCHITECTURE.md`
Related: `docs/watchdog/README.md`, `docs/watchdog/ARCH_WATCHDOG_TODO.md`, `docs/watchdog/ARCH_WATCHDOG_TASK_PROGRESS.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_HAOFV_ARCHITECTURE.md`, `docs/storage/LOG_SYSTEM_TODO.md`
Last updated: 2026-09-06

本文定义 Watchdog 子域的目标边界。它先作为设计草案，不把当前一次 OTA 排查结论、某次 timeout 数值或某块板的观测结果冻结为产品契约。

## 1. 子域目标

Watchdog 子域必须同时解决两件事：

1. 在系统失去前进能力时有界地复位，避免无限卡死；
2. 复位后能回答“哪个执行体、哪个阶段、哪类资源最后有进展”，而不是只返回一个 watchdog timeout。

硬件 watchdog 是最后的复位门，不是异常回调机制。复位前不得依赖 USB、SD、FatFs、日志队列或动态内存仍然可用。

## 2. Owner 与边界

```text
task / core / flash owner
        │ heartbeat + bounded progress + phase breadcrumb
        ▼
WatchdogSupervisorAO  ── health gate ──> hardware watchdog adapter
        │                                      │
        └── retained crash record <────────────┘
                         │
                  boot-time decoder
                         │
             SCPI / USB / log / SD projection
```

| 责任 | Owner | 禁止事项 |
|---|---|---|
| 硬件计时器、复位原因、少量 retained scratch | `drivers/mcu/watchdog` | 不解释业务阶段，不直接写 SD |
| 心跳注册、required gate、stale 判定、唯一 feed owner | Watchdog Supervisor（当前实现位于 `components/diagnostics`） | 业务任务不能直接 feed/reconfigure hardware watchdog |
| OTA/Flash 阶段 breadcrumb | OTA/Flash 各自 owner | 不在 breadcrumb hook 中 delay、阻塞等待或调用复杂日志 |
| 复位后解码与查询 | Diagnostics/SCPI/tooling | 不把一次查询结果当成持久事实 |
| SD/持久化故障包 | Storage/Log owner | 不能成为复位临界路径的必要条件 |

Watchdog 不拥有 OTA 状态、Flash transaction 状态、TDMA 状态或 RTOS 调度状态；它只拥有健康判定和故障证据的投影。

## 3. 两级门禁模型

### 3.1 硬件 watchdog

硬件 watchdog 由单一 supervisor feed。任何业务任务、Flash transaction 或 OTA callback 都不得直接喂狗。
supervisor 自身、Core0 调度链或关键临界区失去运行能力时，硬件 watchdog 只能在超时后复位；不能保证执行软件异常回调。

### 3.2 软件健康门禁

每个关键执行体在自己的正常完成点上报 heartbeat。supervisor 周期性地交换本周期位图并计算 stale mask：

```text
expected_mask - seen_mask = stale_mask
stale_mask ∩ required_mask == 0  -> feed
stale_mask ∩ required_mask != 0  -> 保留证据，停止 feed，等待硬件复位
```

Core1 在受控 Flash lockout/park 窗口内可以被标记为 owner transition；这必须由明确的 lockout 状态证明，不能用“最近有 Flash 活动”自动豁免。

## 4. 证据模型

证据分为三层，按可靠性递减：

1. **复位后可保留证据**：reset reason、watchdog classification、expected/seen/stale mask、core progress、OTA phase；写入硬件 retained scratch 或等价无阻塞区域。
2. **运行态诊断快照**：supervisor count、last seen/stale、lockout owner、当前 task/phase、stack watermark、故障计数；通过 seqlock/双缓冲发布。
3. **富文本日志/SD fault bundle**：日志尾部、调用路径、版本、板卡身份、时间线和原始 USB/SD 记录；只作为补充，不能替代前两层。

每个关键阶段应采用 `BEGIN` / `DONE` 成对 breadcrumb。只写“最后阶段”无法区分“刚进入”与“已完成后卡在下一步”。breadcrumb 写入必须是常数时间、无锁、无分配、不可调用 RTOS delay。

## 5. OTA/Flash 特别规则

- `SYST:OTA:END`、BCB/metadata 选择、Flash lockout、pending seal、reboot request 和 commit 必须拥有独立阶段编码。
- 同步 service hook 只能做 breadcrumb 或 bounded telemetry；不得在 BCB selector、Flash callback 或 OTA END 路径中调用 `vTaskDelay`、USB flush、SD 写入或等待 supervisor。
- Flash transaction 允许报告 progress，但不能以 progress 直接 feed hardware watchdog；feed 仍由 supervisor 决定。
- 显式 `BOOT`/reboot 与 watchdog timeout 必须在 reset reason 和 OTA phase 上可区分。
- `END` 接受、`BOOT` 请求、实际 reset、USB 重枚举、pending 启动和 app commit 必须分别记录，不能把“收到 END”当成“升级完成”。

## 6. 失败与恢复模型

```text
HEALTHY
  -> SUSPECTED_STALE       发现非关键 stale，保留快照并继续观察
  -> RESET_PENDING         关键 stale 或 supervisor 失联，停止 feed
  -> HARDWARE_RESET        硬件复位
  -> BOOT_DIAGNOSE         读取 retained record，发布只读诊断
  -> RECOVERED / ROLLBACK  由上层决定恢复或 OTA 回退
```

Watchdog 只负责 `RESET_PENDING` 到 `BOOT_DIAGNOSE` 的安全边界，不负责猜测根因，也不负责自动重试无限次数。启动后的回退/commit 仍由 Bootloader/OTA owner 决定。

## 7. 必须验证的故障类别

| 类别 | 最小证据 |
|---|---|
| supervisor 自身卡住 | 硬件 timeout、最后 supervisor count/progress、reset reason |
| Core0 任务失联 | task bit、stale mask、core0 progress、当前 OTA phase |
| Core1 失联 | core1 bit、lockout/park 状态、core1 progress |
| Flash/XIP 临界区卡住 | transaction phase、lockout owner、begin/done breadcrumb |
| USB/SCPI 时序竞态 | END/BOOT/reset/re-enumeration 原始时间线 |
| 日志或 SD 不可用 | retained evidence 仍完整，不能因日志写失败丢失复位诊断 |
| 显式软件 reboot | 与 watchdog timeout 不同的 reset classification |

## 8. 迁移原则

先冻结接口和证据语义，再拆分代码目录。迁移过程中 `components/diagnostics` 可以继续承载兼容投影；任何新 owner 必须经过 host/build、P3 硬件验收、五板 OTA 和故障注入验证后，才允许把旧实现标为兼容层。
