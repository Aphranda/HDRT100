# tdma_pio_spi_ring_adapter.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `components/tdma/src/tdma_pio_spi_ring_adapter.c` 在 N5.0.4l 拆分前的
完整副本，用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 4,117 行 / 97 个函数 / **1 个文件级静态**（`s_tdma_pio_spi_ring_adapter_ops`
  常量表），无 `#if` 条件块
- 拆分点：第 1,032 / 2,067 / 3,109 行（`}` 后的空行，括号深度 0）

## 拆分边界（N5.0.4l）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/tdma_pio_spi_ring_adapter.c`（留） | 节拍/几何、错误与队列、init、配置 API、DPLL 观测 | 1,051 |
| `src/tdma_pio_spi_ring_adapter_tx.c`（新） | START/STOP、TX 环循环、转发/训练 | 1,054 |
| `src/tdma_pio_spi_ring_adapter_rx.c`（新） | RX 接收、捕获泵、origin 装配 | 1,061 |
| `src/tdma_pio_spi_ring_adapter_service.c`（新） | service_impl/service、状态发布、快照、ops 表 | 1,027 |
| `inc/tdma_pio_spi_ring_adapter_api.h`（新） | 26 个跨单元 helper 原型 + ops 表 extern | 36 |

**无状态复制**：会话计数器全部在调用者持有的 `tdma_pio_spi_ring_adapter_t` 内，
拆分不产生第二个实例；唯一的静态常量表 `s_tdma_pio_spi_ring_adapter_ops` 只定义
在 service 单元，其余单元经 `extern` 引用。原文件中部（2,345 / 3,033 行）的两个
`#include` 提升到各单元头部，避免函数依赖其位置。
