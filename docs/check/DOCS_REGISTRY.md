# 文档契约登记表

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/DOCS_REGISTRY.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`
Last updated: 2026-08-20

> 注：本文件必须满足 `tools/docs_check/docs_check.py` 的元数据要求（5 字段齐全），否则自回归门禁自相矛盾。

## 契约登记表

规则：域文档每冻结一个跨域契约，必须在此登记一行（contract_id 唯一，登记即上提）。
格式由 `tools/doc_regression_check.py` 校验（id 唯一 / 文件锚点存在）。

| contract_id | domain | contract | ver | clause_loc | code_anchor | check | registered | status |
|---|---|---|---|---|---|---|---|---|
| TDMA-REASON-01 | tdma | ring reason code 9 项冻结 | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_ring_runtime.h | enum 比对 | 2026-08-19 | active |
| TDMA-SEQLOCK-01 | tdma | runtime snapshot 必须 seqlock | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_service.c | 代码审查 | 2026-08-19 | active |
| TDMA-HOP-01 | tdma | hop_limit 归属 ring profile | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_profile.h | 符号存在性 | 2026-08-19 | active |
| REFMEM-260B-01 | refmem | critical delta ≤260B | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | refmem_sync.h | 常量比对 | 2026-08-19 | active |
| VDC-DPLL-01 | vdc | DPLL 准入 resolution≤100ns | 1 | docs/vdc/VDC_DOMAIN_ARCHITECTURE.md | vdc_timestamp_clock.h | 符号存在性 | 2026-08-19 | active |
| TDMA-FLIGHT-BITMAP-01 | tdma | SHORT process image 固定 8×32B，slot 前 8B 由 core1 生成 RX 位图 | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_flight_engine.h | 常量与单测比对 | 2026-08-20 | pending |

## 条款落点表

规则：顶层每条硬约束必须有落点行，不允许孤儿条款。评审发现违规 → 状态改 VIOLATED。

| clause_id | 顶层条款 | domain_loc | module | verify | status |
|---|---|---|---|---|---|
| HAOFV-137 | 双核 Flash/XIP 安全：erase/program 仅 core0 + core1 park ACK | rtos: Flash lockout 框架 | flash_write_owner | 代码审查 | PENDING |
| HAOFV-138 | core1 实时 owner：TriggerAO/FB 运行在 core1 | rtos: 任务模型 | app_tasks | 代码审查 | PENDING |
| HAOFV-139 | 跨核共享事实：多字段必须 seqlock/双缓冲/version | tdma: 双 FIFO 契约 | tdma_flight_fifo | 代码审查 | PARTIAL |
| HAOFV-140 | 分布式共同事实：RefMem 承接跨节点事实 | refmem: 主域 | distributed_refmem | 代码审查 | PENDING |
| HAOFV-141 | 分布式共同时间：VDC 唯一 owner | vdc: 主域 | vdc_dpll_manager | 代码审查 | PENDING |
| HAOFV-142 | 分布式确定性通讯：TDMA 唯一 owner，声明 UP/DOWN group | tdma: ring profile | tdma_profile.h | 符号存在性 | OK |
| HAOFV-143 | Vector 字段契约：唯一 writer/值域/生命周期 | rtos: Vector 规则 | system_vector | 代码审查 | PENDING |
| HAOFV-144 | 时间回绕：int32 diff 回绕安全写法 | rtos: 时间回绕 | 全局 | 代码审查 | PENDING |
| HAOFV-145 | Metadata failsafe：双副本无效强制恢复 | ota: failsafe 状态机 | bootloader | 代码审查 | PENDING |
| HAOFV-146 | FB 非阻塞：action 立即返回 | arch: FB 规则 | function_block | 代码审查 | PENDING |
| HAOFV-879 | 多字段事实必须 seqlock（RTOS 细化条款） | tdma: runtime snapshot | tdma_pio_spi_ring_adapter | 代码审查 | VIOLATED |

## 状态说明

- contract status: active / pending / superseded
- clause status: OK / PARTIAL / VIOLATED / PENDING
- 新契约登记后，顶层 `docs/arch/HAOFV_ARCHITECTURE.md` 须在 7 天内刷新（环1 校验）
