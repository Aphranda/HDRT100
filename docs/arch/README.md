# 架构域

Status: Active
Domain: ARCH
Canonical: `docs/arch/README.md`
Related: `docs/README.md`, `docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是产品架构、HAOFV、RTOS、双核 AMP 和分布式触发总纲的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `../ARCH_PRODUCT_ARCHITECTURE.md` | 产品化系统架构总纲 |
| `../HAOFV_ARCHITECTURE.md` | HAOFV 架构入口 |
| `../HAOFV_IMPLEMENTATION_PLAYBOOK.md` | HAOFV 实施指南 |
| `../RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` | RTOS + 双核 AMP 分区和 SCPI 自上而下适配 |
| `../MULTICORE_PARTITION_PLAN.md` | RP2350 双核分区计划 |

## 边界

- 架构域说明 owner、层次、资源仲裁和长期原则。
- 具体 SCPI 命令放 `interface/`，具体实时执行放 `trigger/`。
