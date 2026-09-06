# Watchdog 与故障证据子域任务进度

Status: Draft
Domain: WATCHDOG
Canonical: `docs/watchdog/ARCH_WATCHDOG_TASK_PROGRESS.md`
Related: `docs/watchdog/README.md`, `docs/watchdog/ARCH_WATCHDOG_ARCHITECTURE.md`, `docs/watchdog/ARCH_WATCHDOG_TODO.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-06

本文只追加 Watchdog 子域的执行证据。任务状态仍以 `ARCH_WATCHDOG_TODO.md` 为唯一事实源；本文件不得自行冻结契约或替代原始验收产物。

## 当前 checkpoint

- Checkpoint: `WDT-PLAN-20260906-01`
- Scope: 建立 watchdog 子域文档边界，准备五板 UF2/P3/OTA 验收。
- Status: IN PROGRESS
- Evidence root: `out/`
- Next gate: `WDT-HIL-001`

## 进度记录

### WDT-PROGRESS-20260906-001

- 日期：2026-09-06
- TODO task ID：`WDT-ARCH-001`, `WDT-EVID-001`
- 变更：创建 `docs/watchdog/` README、Architecture、TODO、Task Progress 三件套；依据当前 `drv_watchdog`、`diagnostics`、OTA/Flash 和 SCPI 实现划分 owner 和证据边界。
- 验证：待执行文档门禁；未宣称代码或硬件验收通过。
- 结果：Watchdog 设计保持 Draft；未新增 `DOCS_REGISTRY` 冻结契约。
- 证据位置：本文件；后续板端原始证据写入 `out/hardware-acceptance/`、`out/ota/` 和 `out/watchdog/`。
- 下一步：完成最新 UF2 五板烧录、P3 硬件验收和 OTA END/BOOT/COMM 逐板时间线。

## 证据索引规则

每条后续记录必须包含：日期、TODO task ID、源码/UF2 指纹、命令、构建或硬件结果、失败原始输出、证据路径和下一 gate。P3 凭证必须绑定当前 staged 源码指纹；旧凭证、手工摘要和 replay 不作为通过证据。
