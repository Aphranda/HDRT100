# 同步域

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/README.md`
Related: `docs/README.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 SYNC、VDC、DPLL、同步检查、锁定质量和 HOLDOVER 策略的同步域落地入口。VDC/DPLL 在 HAOFV 下的核心基础架构边界以 `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` 为准。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `SYNC_IO_RESOURCE_PLAN.md` | PIO、GPIO、DMA 和语义 IO 资源规划 |
| `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` | 分布式 DPLL / CAL_RING 同步设计 |
| `SYNC_IO_REFACTOR_PLAN.md` | SYNC_IO 重构计划 |
| `SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构进度 |
| `SYNC_IO_ARCH_REVIEW_TODO.md` | 同步架构评审待办 |

## 边界

- `SYNC:*` 是产品同步动作域。
- VDC 是虚拟 DC 时钟，DPLL 是实现 VDC 稳态同步的算法层，不建立裸顶级域。
- `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 是同步域实施方案；核心 owner、Vector、门禁和 Trigger/Realtime 边界见 `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`。
