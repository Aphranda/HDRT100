# 同步域

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/README.md`
Related: `docs/README.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 `SYNC:*` 动作、SYNC_IO、同步链路、PIO/GPIO/DMA 资源和分布式同步落地方案入口。VDC/DPLL 已升级为 HAOFV 内部基础主域，canonical 入口为 `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`；既有 `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` 作为融合架构输入保留。

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
- `sync/` 负责同步动作和 SYNC_IO 落地方案，不作为 VDC canonical。
- VDC 是虚拟 DC 共同时间主域，DPLL 是实现 VDC 稳态同步的基础件，不建立裸顶级 SCPI 域。
- `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 是同步链路实施方案；VDC owner、Vector、门禁和 Trigger/Realtime 边界见 `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`。
