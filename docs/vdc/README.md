# VDC 内部主域

Status: Active
Domain: VDC
Canonical: `docs/vdc/README.md`
Related: `docs/README.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_VDC_DPLL_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-08-13

本目录是 Virtual Distributed Clock / VDC 内部主域入口。VDC Domain 维护多节点共同时间、`local_tick -> vdc_time` 映射、SYNC DPLL、HOLDOVER/RELOCK、timestamp dictionary、时间质量和预测分发时间基准。

## 当前主线

| 当前路径 | 定位 | 使用规则 |
|---|---|---|
| `VDC_DOMAIN_ARCHITECTURE.md` | VDC 内部主域架构，定义共同时间事实、owner、数据模型、跨域关系和目标代码形态。 | VDC 主域 canonical 架构入口。 |
| `VDC_DOMAIN_TODO.md` | VDC 主域独立待办，维护文档同步、数据契约、DPLL、HOLDOVER、RefMem 映射、组件化和验证事项。 | 只记录未完成事项，不写验证流水账。 |
| `VDC_TASK_PROGRESS.md` | VDC 主域任务进度，记录阶段性工作、验证结果和后续动作。 | VDC 新任务完成后追加记录。 |

## 相关参考

| 当前路径 | 定位 |
|---|---|
| `../arch/HAOFV_ARCHITECTURE.md` | HAOFV 顶层架构，定义 VDC 的内部基础主域地位。 |
| `../arch/HAOFV_VDC_DPLL_ARCHITECTURE.md` | 既有 VDC/DPLL 融合架构输入，后续逐步迁入 VDC canonical。 |
| `../arch/RTOS_HAOFV_ARCHITECTURE.md` | 当前 RTOS task、VDC/DPLL owner 壳和双核边界。 |
| `../sync/SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` | SYNC_IO 下的分布式 DPLL 落地方案和历史设计输入。 |
| `../refmem/REFMEM_DOMAIN_ARCHITECTURE.md` | RefMem 共同事实主域，保存 VDC snapshot、版本、质量和证据。 |

## 边界

- VDC Domain 是内部基础主域，不是对外 SCPI 顶级命令域。
- `SYNC:*` 是对外同步动作域；VDC Domain 是同步动作背后的共同时间 owner。
- RefMem Domain 保存 VDC 事实快照和质量证据，但不计算 offset/rate。
- Angle DPLL 消费 VDC 时间基准并生成 `T_fire_base`，不能写 VDC offset/rate。
