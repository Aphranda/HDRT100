# 存储与证据域

Status: Active
Domain: SD
Canonical: `docs/storage/README.md`
Related: `docs/README.md`, `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-21

本目录是 SD、StorageAO、日志、trace、snapshot、T2 分页和报告证据的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `SD_TODO.md` | SD 文件系统、StorageAO 和持久化观测层设计/待办 |
| `SD_TASK_PROGRESS.md` | SD 域任务进度和验证记录 |
| `LOG_SYSTEM_TODO.md` | 日志 core、诊断 trace、持久化和故障证据入口 |
| `../arch/HAOFV_FLASH_ARCHITECTURE.md` | 板载 NVS/blob/FCB 与 SD Storage Domain 的边界 |

## 边界

- `MMEMory:*` 只表达文件系统式访问。
- SD driver、storage job、manifest、trace、snapshot 和报告证据归本域 owner。
- 高频日志、完整 trace 和报告写 SD；板载 Flash 只保存关键 KV、低频故障事件和受控 blob，写入统一经过 `FlashTransactionAO`。
