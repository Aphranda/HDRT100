# 同步域

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/README.md`
Related: `docs/README.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-15

本目录是 `SYNC:*` 动作、SYNC_IO、底层 realtime IO、PIO/GPIO/DMA 资源和同步链路落地方案入口。VDC/DPLL 已升级为 HAOFV 内部基础主域，canonical 入口为 `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`；既有 `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` 作为融合架构输入保留。

`docs/sync` 后续采用三分标准：

```text
SYNC_IO_ARCHITECTURE.md  架构和边界
SYNC_IO_TODO.md          未完成待办
SYNC_IO_TASK_PROGRESS.md 已完成闭环和验证记录
```

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `SYNC_IO_ARCHITECTURE.md` | SYNC_IO / realtime IO 架构入口，定义 HAOFV owner、层级、mode driver、snapshot、RefMem/VDC 边界。 |
| `SYNC_IO_TODO.md` | SYNC_IO / realtime IO 当前待办，按优先级维护，不记录流水账。 |
| `SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构和 realtime IO 的闭环验证记录。 |

## 边界

- `SYNC:*` 是产品同步动作域。
- `REALtime:*` 是底层实时维护域，负责 PIO/DMA/IRQ、SEQ/ENC/PCNT、底层 IO 和验证观测。
- `sync_io` 是 HAOFV Hardware Service / realtime IO owner，不是产品业务动作域。
- `sync/` 负责同步动作和 SYNC_IO 落地方案，不作为 VDC canonical。
- VDC 是虚拟 DC 共同时间主域，DPLL 是实现 VDC 稳态同步的基础件，不建立裸顶级 SCPI 域。
- 同步链路、CAL_RING、预约触发和 PIO/DMA 资源约束已并入 `SYNC_IO_ARCHITECTURE.md`；VDC owner、Vector、门禁和 Trigger/Realtime 边界见 `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`。
