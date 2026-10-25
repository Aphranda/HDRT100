# scpi_sync_commands.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `middleware/scpi_port/src/scpi_sync_commands.c` 在 N5.0.4k 拆分前的
完整副本，用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 2,333 行 / 99 个函数 / 0 个文件级静态变量
- 拆分点：第 1,094 行（`scpi_cmd_vdc_output_timing_q` 的 `}` 之后、
  `scpi_cmd_vdc_output_delay` 之前，括号深度 0）

## 拆分边界（N5.0.4k）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/scpi_sync_commands.c`（留） | SYSTem:SYNC / DPLL 配置 / SYNC:VDC:TDMA / 优先级 TRACE/GUARD | 1,078 |
| `src/scpi_vdc_output_priority_commands.c`（新） | OUTPut:DELay/TIMing、优先级 FOLlow/BASEline/HEALth/MATCh/RX/SYNC/TX/GAP、TDMA:PRIORity、path_delay/observer | 1,257 |
| `inc/scpi_sync_commands_internal.h`（新） | 读数原因枚举 + 3 个共享 helper 原型 | 32 |

**命令注册宏未改动**：`SCPI_SYNC_COMMANDS` 仍按原名字/顺序在 `scpi_port.c` 展开，
回调名与相对顺序不变。三个共享 helper（published lock / u64 result / DPLL-eligible）
与 `scpi_sync_vdc_lock_readiness_reason_t` 枚举移入私有头，主单元保留唯一定义。
