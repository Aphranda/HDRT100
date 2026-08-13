# 同步域

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/README.md`
Related: `docs/README.md`, `docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 SYNC、VDC、DPLL、同步检查、锁定质量和 HOLDOVER 策略的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `../SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` | 分布式 DPLL / CAL_RING 同步设计 |
| `../SYNC_IO_REFACTOR_PLAN.md` | SYNC_IO 重构计划 |
| `../SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构进度 |
| `../SYNC_IO_ARCH_REVIEW_TODO.md` | 同步架构评审待办 |

## 边界

- `SYNC:*` 是产品同步动作域。
- VDC 是虚拟 DC 时钟，DPLL 是实现 VDC 稳态同步的算法层，不建立裸顶级域。
