# distributed_refmem.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `components/distributed_refmem/src/distributed_refmem.c` 在 N5.0.4p
拆分前的完整副本，用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 5,341 行 / 32 个跨单元静态对象 / 13 个条件编译块
- 拆分点：第 1,368 / 2,675 / 4,001 行（`}` 后的空行、括号深度 0；13 个 `#if`
  块均不跨越）
- 该文件此前已由 N5.0.3 系列拆出 12 个同族单元（command / cmd_frame / cmd_reply /
  diag / node_control / position / position_ack / position_state /
  priority_health / vdc_feedback 等）

## 拆分边界（N5.0.4p）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/distributed_refmem.c`（留） | init/服务入口、帧解析、状态机骨架 | 1,343 |
| `src/distributed_refmem_plane.c`（新） | 位置/参数池/节点装载平面 | 1,356 |
| `src/distributed_refmem_link.c`（新） | 同步链路、证据与 ACK | 1,375 |
| `src/distributed_refmem_status.c`（新） | 状态发布、自动同步、诊断 | 1,389 |
| `src/distributed_refmem_split_internal.h`（新） | 5 个顶部类型 + 跨单元宏 + 33 个 extern 对象 + 60+ helper 原型 | 219 |

**跨文件重名**：`s_initialized` 与 `refmem_application_model.c` 的同名全局冲突，
重命名为 `s_refmem_core_initialized`（模型侧随之更新其 harness）。
**static-inline 依赖**：`refmem_le.h` 原在第 457 行才被包含，已提升到私有头。
