# 文档契约登记表

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/DOCS_REGISTRY.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`
Last updated: 2026-08-28

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
| VDC-PATHMATRIX-01 | vdc | calibration load 生成完整 observation path matrix，运行态禁止 ring path inference | 1 | docs/vdc/VDC_DOMAIN_ARCHITECTURE.md | vdc_domain.h | C/host matrix completeness and lookup tests | 2026-08-28 | pending |
| TDMA-FLIGHT-BITMAP-01 | tdma | SHORT process image 固定 8×32B，slot 前 8B 由 core1 生成 RX 位图（旧 ID 不符合检查器单段主题格式，由 TDMA-FLIGHTBITMAP-01 接替） | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_flight_engine.h | 常量与单测比对 | 2026-08-20 | superseded |
| TDMA-FLIGHTBITMAP-01 | tdma | SHORT process image 固定 8×32B，slot 前 8B 由 core1 生成 RX 位图 | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_flight_engine.h | 常量与单测比对 | 2026-08-21 | pending |
| TDMA-PROCESSIMAGE-01 | tdma | 固定 SHORT process image 静态装配 Node mailbox 与全局 DPLL observation trailer；DPLL 不得替换帧型、长度、序列或 PIO 节拍 | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_process_image_layout.h | 编译断言、预算工具、固定帧型回归、publisher/parser 与多板 HIL | 2026-08-28 | pending |
| TDMA-RECOVERY-01 | tdma | 短帧错误恢复由 Core0 准备、Core1 有界装载、PIO 发送；双 recovery buffer 交替、每周期最多一帧、独立静态预算并复用原 Node segment offset | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_traffic_scheduler.h | 状态机/预算门禁/ACK-retry/backpressure 与多板 HIL | 2026-08-28 | pending |
| TDMA-OPMODE-01 | tdma | SPI 速率与 TDMA 周期按离散 operating profile 成对切换，STOP 后生效 | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md | tdma_operating_profile.h | SCPI/状态机/单测比对 | 2026-08-20 | pending |
| ARCH-FLASHMAP-01 | arch | FlashMap v2 是 Boot/linker/App/factory/tool 的唯一分区词汇 | 1 | docs/arch/HAOFV_FLASH_ARCHITECTURE.md | ota_partition.h | 生成表/链接/map/factory 工具比对 | 2026-08-21 | pending |
| ARCH-FLASHOWNER-01 | arch | App erase/program 仅 core0 FlashTransactionAO，Boot 使用最小 BootFlashService | 1 | docs/arch/HAOFV_FLASH_ARCHITECTURE.md | drv_flash_lockout.h | 裸调用扫描/双核 HIL/Boot 依赖审计 | 2026-08-21 | pending |
| DOCS-FLASH-01 | docs | Flash 域架构、TODO、任务进度三类文档的事实边界与变更接口 | 1 | docs/arch/HAOFV_FLASH_ARCHITECTURE.md | doc_regression_check.py | 文档边界/门禁/索引审查 | 2026-08-22 | pending |
| DOCS-TRIPLETFORMAT-01 | docs | 域文档 Architecture、TODO、Task Progress 三件套的最小格式、稳定 ID、状态词汇和文件接口 | 1 | docs/check/DOCS_REGRESSION_PLAN.md | doc_regression_check.py | 结构审查与后续自动化检查 | 2026-08-27 | pending |
| ARCH-BOOTCTRL-01 | ota | BCB 双 lane append/commit 与 Direct A/B test-confirm-revert | 1 | docs/arch/HAOFV_FLASH_ARCHITECTURE.md | ota_metadata.h | torn-write/boot/revert HIL | 2026-08-21 | pending |
| ARCH-OTASTREAM-01 | ota | USB CDC/USBTMC/UART/RS485/SD/TDMA 共用 OtaStreamSession，ACK 只确认 durable offset | 1 | docs/arch/HAOFV_FLASH_ARCHITECTURE.md | ota_package.h | transport 回归/parser fuzz/续传 HIL | 2026-08-21 | pending |
| REFMEM-PERSIST-01 | refmem | 只持久化部署 package/ref，上电建立新 epoch 且 live mirror 保持 stale | 1 | docs/arch/HAOFV_FLASH_ARCHITECTURE.md | refmem_table_registry.h | power-cut/reboot/epoch/stale HIL | 2026-08-21 | pending |
| VDC-PERSIST-01 | vdc | 只持久化低频 profile，上电从 OFF/CHECKING 基于新观测重新锁相 | 1 | docs/arch/HAOFV_FLASH_ARCHITECTURE.md | vdc_domain.h | reboot/negative restore/DPLL HIL | 2026-08-21 | pending |
| ARCH-PIOCAT-01 | arch | 动态 PIO 只装载签名 App catalog program，System Pack 只选择 ID | 1 | docs/arch/HAOFV_FLASH_ARCHITECTURE.md | tdma_pio_spi_phys.h | catalog/resource/deployment/persona HIL | 2026-08-21 | pending |
| CALIBRATION-PHASE-01 | calibration | MARK/SCK/DATA 共用 per-link base、per-Node offset、原始证据、全量矩阵和 residual gate | 1 | docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md | calibration_training_phase.h | C/host/矩阵/TRN-03 回归与四板 HIL | 2026-08-26 | pending |

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
