# TDMA 基础件主域 README

Status: Active
Domain: TDMA
Canonical: `docs/tdma/README.md`
Related: `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
Last updated: 2026-08-17

`docs/tdma/` 维护 TDMA 基础件主域。TDMA 在 HAOFV 中不是 VDC 的内部组件，而是底层确定性通讯与环路反馈基础件。

TDMA Domain 负责：

- 定义上行/下行 TDMA runtime、ring profile、window、guard、deadline 和 completion evidence。
- 管理 PIO/SM/DMA/core1 service 资源声明、payload registry、short/long frame capacity 和 adapter 边界。
- 管理 TSN-style traffic class、准入、周期预算、time-aware gate、整形、背压和逐流质量；VDC/RefMem 预留资源，配置/OTA/LOG 使用受控维护或剩余预算。
- 为 VDC 提供 observation window、timestamp evidence 和质量摘要。
- 为 RefMem 提供 payload window、delta/ACK/fence/quality 承载和可靠性证据。
- 为后续 BISS-C、PIO SPI、UART、RS485 等 transport adapter 提供统一调度骨架。

TDMA Domain 不负责：

- 计算 VDC offset/rate/lock。
- 修改 RefMem active fact。
- 执行业务触发序列。
- 解析产品业务语义。
- 直接暴露裸 GPIO 或板级临时接线作为架构规则。

标准文件：

| 文件 | 定位 |
|---|---|
| `TDMA_DOMAIN_ARCHITECTURE.md` | TDMA 基础件架构、HAOFV 边界、上/下行环路、adapter 和跨域契约。 |
| `TDMA_DOMAIN_TODO.md` | TDMA 基础件待办，跟踪 runtime、可靠性、HAOFV system node 和 HIL 验收。 |
| `TDMA_TASK_PROGRESS.md` | TDMA 基础件任务进度、验证结果和可回溯记录。 |
