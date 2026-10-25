# storage_manager.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `components/storage_manager/src/storage_manager.c` 在 N5.0.4g 拆分前的
完整副本，用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 3,566 行 / 97 个函数 / 19 个文件级静态对象（其中 16 个跨单元共享）
- 拆分点：第 1,230 与 2,299 行（均为 `}` 后的空行，括号深度 0）

## 拆分边界（N5.0.4g）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/storage_manager.c`（留） | 类型/宏、19 个状态对象定义、白名单表、路径与预算 helper、probe、直读 FS API、service 循环 | 1,133 |
| `src/storage_manager_jobs.c`（新） | 全部 `post_*` 投递、写事务（begin/chunk/commit/finish/copy/abort）、`service_job` 分片状态机、结果租约 | 1,070 |
| `src/storage_manager_evidence.c`（新） | 系统包 bootstrap、manifest 扫描、snapshot/trace/runtime-log/fault 落盘、字符串表 | 1,269 |
| `inc/storage_manager_internal.h`（新） | 类型/宏 + 16 个 `extern` 状态 + 26 个跨单元 helper 原型 | 148 |

**状态仍是单实例**：16 个对象只在主单元定义一次，另两单元经 `extern` 访问；仍由 StorageAO
单写者持有。trace 环的两个临界区（原 :630 / :3043）随所属函数留在各自单元，成对关系不变。
