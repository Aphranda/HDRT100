# 触发域

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/README.md`
Related: `docs/README.md`, `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是产品触发、序列、角度、断点、core1 实时执行和底层 validation 的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `TRIGGER_SYNC_TODO.md` | 触发系统产品化待办 |
| `TRIGGER_SEQ_STEP_DESIGN.md` | 序列步进触发模式设计 |
| `TRIGGER_ENC_COUNT_DESIGN.md` | 编码器计数触发模式设计 |
| `TRIGGER_PULSE_COUNT_ANALYSIS.md` | 脉冲计数分析 |
| `TRIGGER_INDUSTRIAL_ENHANCEMENT_DESIGN.md` | 工业级触发增强方案 |
| `RP2350B_FOUR_BOARD_DISTRIBUTED_TRIGGER_SCHEME.md` | RP2350B 四板分布式触发方案 |
| `../SYNC_IO_RESOURCE_PLAN.md` | PIO、GPIO、DMA 和语义 IO 资源规划 |

## 边界

- `TRIGger:*` 是运行意图和模式控制。
- core1、PIO、FIRE_LOAD 和实时边沿执行属于内部 owner，不由 SCPI callback 直接操作。
