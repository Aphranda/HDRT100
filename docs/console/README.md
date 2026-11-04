# 运维与调试控制面（CONSOLE）

Status: Active
Domain: CONSOLE
Canonical: `docs/console/README.md`
Related: `docs/console/CONSOLE_ARCHITECTURE.md`, `docs/console/CONSOLE_WORKFLOW_ARCHITECTURE.md`, `docs/console/CONSOLE_TK_ARCHITECTURE.md`, `docs/console/CONSOLE_DOMAIN_TODO.md`, `docs/console/CONSOLE_TASK_PROGRESS.md`, `docs/README.md`
Last updated: 2026-11-04

本目录是产品级运维/调试控制面的目标入口。它不定义固件语义，只负责把各主域的 bring-up 流程、状态回读、危险操作和证据收敛为一个可审计入口。

## 当前 canonical

| 路径 | 定位 |
|---|---|
| `CONSOLE_ARCHITECTURE.md` | 域定位、边界、子域、契约候选、单端口原则、阶段与验收 |
| `CONSOLE_TK_ARCHITECTURE.md` | Tk 应用框架：Shell/Router/Page/Store/Intent/UiPump/Dialog/WidgetKit、测试与迁移 |
| `CONSOLE_WORKFLOW_ARCHITECTURE.md` | **Workflow 唯一定义**：WorkflowSpec、步骤模型、能力范围/矩阵、成熟度、运行语义、证据与版本、必须工作流化的流程与禁止 |
| `CONSOLE_DOMAIN_TODO.md` | 任务表（按 P0/P1/P2）、阻塞、阶段映射与验收口径 |
| `CONSOLE_TASK_PROGRESS.md` | 真实执行、证据、失败与下一步 |

## 首个交付物

**调试 Demo**：`tools/domains/trigger/trigger_debug_demo/`。

- 单端口（串口 / USB TMC）连接协调板；
- GUI 与本地 TCP 共用同一 Dispatcher，不重复开会话；
- REFMEM 统一控制面 + 应用层 SCPI 两条通道；
- 常规单板 OTA 与自动凭证；
- 模拟器支持无台架演示。

目标路径为 `tools/domains/console/debug_console/`，迁移见 `CONSOLE_DOMAIN_TODO.md` 的 `CONSOLE-015`；迁移前不删除旧路径。

## 阅读顺序

1. `CONSOLE_ARCHITECTURE.md`
2. `CONSOLE_WORKFLOW_ARCHITECTURE.md`（workflow 唯一定义）
3. `CONSOLE_TK_ARCHITECTURE.md`
4. `CONSOLE_DOMAIN_TODO.md`
5. `CONSOLE_TASK_PROGRESS.md`
