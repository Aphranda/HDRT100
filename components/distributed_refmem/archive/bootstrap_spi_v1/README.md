# bootstrap_spi_v1 — 归档说明

Status: Archived
Domain: REFMEM / CONSOLE
Archived: 2026-11-05
Reason: 四板 TDMA 环的相邻链路是单向的（时钟 A→B、数据 B→A），点对点 SPI 的
master→slave 请求/应答无法在环上成立；10 kHz TDMA 低速档已在环上跑通并承载
REFTMEM 命令/回复，低速 SPI bootstrap 不再是产品路径。

## 内容

- `inc/`、`src/`：`refmem_bootstrap_hello`、`refmem_bootstrap_relay`、
  `refmem_bootstrap_transport`（P4.5 两板 SPI 探针/中继/adapter owner）。
- `tests/`：三个 host 用例。
- `tools/`：三个 host runner。

## 已从主构建移除

- `CMakeLists.txt` 的 `PROJECT_REFMEM_BOOTSTRAP_ENABLED` 选项、条件源与编译定义。
- SCPI 页面 `SYSTem:REFMEM:SYNC:SPI:ARM/DISarm/STATus?`、
  `SYSTem:REFMEM:BOOTstrap:STATus?`（handler、prototype、注册表项）。
- `distributed_refmem_link.c` 中的 bootstrap binding/start/stop 仍保留在
  `#if PROJECT_REFMEM_BOOTSTRAP_ENABLED` 下；该宏不再定义，块不参与编译。

## 未归档（仍在使用）

- `refmem_spi_physical_adapter`：仍被 `SYNC:AUTO` 旧 node-load 路径引用；
  若后续退役 AUTO，可再评估归档（约 421 B RAM）。
- `s_refmem_sync` / `s_refmem_sync_context`：SCPI 诊断桥与常驻 sync 接收器，
  属另一批清理项，不在本归档内。

## 恢复

按 §2.10 从本目录移回原路径，并恢复 CMake/SCPI 引用即可；提交历史保留了
删除前的完整实现。
