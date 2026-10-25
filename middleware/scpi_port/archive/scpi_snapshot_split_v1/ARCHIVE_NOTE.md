# scpi_system_snapshot_commands.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `middleware/scpi_port/src/scpi_system_snapshot_commands.c` 在
N5.0.4n 拆分前的完整副本，用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 4,615 行 / 142 个函数 / 4 个跨单元静态对象
- 拆分点：第 1,146 / 2,306 / 3,465 行（`}` 后的空行，括号深度 0）
- 该文件此前已由 N5.0.3 拆出 `scpi_refmem_snapshot_commands.c`（1,062）与
  `scpi_vdc_snapshot_commands.c`（692）；本轮沿用既有
  `scpi_system_snapshot_internal.h` 的 helper 约定

## 拆分边界（N5.0.4n）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/scpi_system_snapshot_commands.c`（留） | 系统/构建/身份、TM 面、SYSTem:ERRor 族 | 1,096 |
| `src/scpi_platform_snapshot_commands.c`（新） | 平台/诊断/存储/板级观测族 | 1,152 |
| `src/scpi_tdma_snapshot_commands.c`（新） | TDMA 环/席位/时序/发布快照族 | 1,161 |
| `src/scpi_node_snapshot_commands.c`（新） | 节点/RefMem NODES/命令原因族 | 1,131 |
| `inc/scpi_snapshot_commands_internal.h`（新） | 类型/宏 + 18 个跨单元 helper 原型 | 36 |

命名避让：新单元不使用 `scpi_refmem_snapshot_commands.c`（该名字已由 N5.0.3a 占用），
避免覆盖既有单元导致符号丢失（首轮尝试即因此失败）。
`SCPI_SYSTEM_SNAPSHOT_COMMANDS` 注册宏与回调顺序未改。
