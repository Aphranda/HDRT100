# 触发域

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/README.md`
Related: `docs/README.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-10-09

本目录是产品触发、序列、角度、断点、core1 实时执行和底层 validation 的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `TRIGGER_SYNC_TODO.md` | 触发系统产品化待办 |
| `TRIGGER_SEQ_STEP_DESIGN.md` | 序列步进触发模式设计 |
| `TRIGGER_ENC_COUNT_DESIGN.md` | 编码器计数触发模式设计 |
| `TRIGGER_PULSE_COUNT_ANALYSIS.md` | 脉冲计数分析 |
| `TRIGGER_INDUSTRIAL_ENHANCEMENT_DESIGN.md` | 工业级触发增强方案 |
| `TRIGGER_FOUR_BOARD_DISTRIBUTED_PLAN.md` | RP2350B 四板分布式触发方案 |
| `t2/TRIGGER_T2_ARCHITECTURE.md` | T2 实施架构草案：多板槽位部署、预约 owner、时间与执行边界 |
| `t2/TRIGGER_T2_TODO.md` | 先多板实际部署、再 T2 预约触发的实施任务与退出门禁 |
| `t2/TRIGGER_T2_TASK_PROGRESS.md` | T2 参考来源、实现/验证、失败与交接证据 |
| `t2/TRIGGER_T2_REVIEW.md` | T2 代码与架构语义评审快照；采纳项转入 TODO，不作为契约或实测证据 |
| `../sync/SYNC_IO_ARCHITECTURE.md` | PIO、GPIO、DMA 和语义 IO 资源规划 |

## 边界

- `TRIGger:*` 是运行意图和模式控制。
- core1、PIO、FIRE_LOAD 和实时边沿执行属于内部 owner，不由 SCPI callback 直接操作。
