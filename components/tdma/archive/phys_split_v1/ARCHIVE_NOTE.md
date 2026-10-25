# tdma_pio_spi_phys.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `components/tdma/src/tdma_pio_spi_phys.c` 在 N5.0.4o 拆分前的完整副本，
用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 3,037 行 / 32 个静态函数 / 47 个静态对象 / 8 个条件块
- 拆分点：第 1,004 与 2,025 行（`}` 后的空行，括号深度 0；两个切口均不跨越 `#if`）
- 该文件此前已按 N5.0.3 系列拆出 13 个同族单元（cal_control / cal_service / coded /
  data_train_restored / event / flight_io / geometry / marker_restored / origin / p3 /
  priority / programs / rx_first / rx_scan / rx_start_cut / timing）

## 拆分边界（N5.0.4o）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/tdma_pio_spi_phys.c`（留） | 时钟/锁存/训练写入、事件观察桥 | 1,031 |
| `src/tdma_pio_spi_phys_ring_io.c`（新） | OVERLAY 脚本、flight 配置、ring 字/字节读写 | 1,051 |
| `src/tdma_pio_spi_phys_arm.c`（新） | RX ring 访问、capture_words、arm/disarm、资源释放 | 1,042 |
| `src/tdma_pio_spi_phys_internal.h`（扩） | +3 个跨单元宏 +9 个 extern 对象 +18 个 helper 原型 | 262 → 332 |

跨单元状态 9 个（全部是 PIO SM/DMA 偏移与 live word 句柄，只定义一次）；跨单元
helper 18 个去 static；8 个条件块均未被切开。
