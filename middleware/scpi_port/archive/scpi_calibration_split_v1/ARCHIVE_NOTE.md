# scpi_calibration_commands.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `middleware/scpi_port/src/scpi_calibration_commands.c` 在 N5.0.4c
拆分前的完整副本，用于审计与回退取证。不参与任何构建（根 `CMakeLists.txt` 使用
显式源文件列表），不得删除。

## 2026-10-26：拆分前快照

- 源文件 2,131 行 / 74 个函数 / 1 个文件级静态数组（`s_scpi_sma_cable_capture`）
- SHA256：`FE20A10E34106F4373FD8EAD2F2754CBA7F32225B5158963EA7F96EE8D352848`
- 拆分点：第 1021/1022 行之间（`scpi_calibration_data_arm` 之前，括号深度 0）

## 拆分边界（N5.0.4c）

| 单元 | 保留内容 | 目标行数 |
|---|---|---|
| `src/scpi_calibration_commands.c`（留） | ORIGin 全族、loopback、SMA 电缆、MARKer、共享 helper | 1,022 |
| `src/scpi_calibration_capture_commands.c`（新） | DATA / SCK / RING 抓取 / TRAINing / PATH / BIAS / SAVE / CLK coded / P3 / LINK 与元信息 | 1,122 |

跨边界只有一个私有 helper：`scpi_calibration_result_u64` 由 `static` 改为外部符号，
声明放进 `inc/scpi_calibration_commands_internal.h`。命令注册表（`SCPI_CALIBRATION_COMMANDS`
宏）未改动，回调仍按原名被 `scpi_port.c` 引用。
