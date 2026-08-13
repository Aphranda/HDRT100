# 反射内存域

Status: Draft
Domain: REFMEM
Canonical: `docs/refmem/README.md`
Related: `docs/README.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是分布式向量表、命令槽、ACK/NACK、节点事实、stale/CRC/epoch 和多板共同状态的目标入口。

## 当前参考

| 当前路径 | 定位 |
|---|---|
| `../arch/RTOS_HAOFV_ARCHITECTURE.md` | 当前 DTC100 反射内存和 RTOS owner 设计 |
| `../LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md` | PinProbe A1 RAM 反射内存历史方案 |
| `../interface/DTC100_SCPI_COMMAND_PLANNING.md` | SCPI 与反射内存边界 |

## 待补 canonical

- 需要新增 `REFMEM_DISTRIBUTED_VECTOR_ARCHITECTURE.md`。
- 需要新增 `REFMEM_COMMAND_ACK_DESIGN.md`，统一 command_seq、ACK/NACK、busy、timeout 和 reason 表。
