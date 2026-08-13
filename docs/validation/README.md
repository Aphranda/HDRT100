# 验证域

Status: Active
Domain: VALIDATION
Canonical: `docs/validation/README.md`
Related: `docs/README.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 HIL、工具验证、任务进度、闭环验证记录和脚本说明的目标入口。

## 当前入口

| 当前路径 | 定位 |
|---|---|
| `../SCPI_TASK_PROGRESS.md` | SCPI 指令框架、验证脚本和接口拆分闭环记录 |
| `../RTOS_DISTRIBUTED_TRIGGER_TASK_PROGRESS.md` | RTOS / 双核 / 分布式触发任务进度 |
| `../communication/BISSC_TASK_PROGRESS.md` | BiSS-C 任务进度和验证记录 |
| `../storage/SD_TASK_PROGRESS.md` | SD 域任务进度和验证记录 |
| `../sync/SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构任务进度 |

## 边界

- 任务进度可以保留在业务域，也可以由本目录建立总验证索引。
- 验证文档必须记录命令、固件版本、工具、现象、结果和后续动作。
