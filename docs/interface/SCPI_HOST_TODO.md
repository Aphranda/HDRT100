# 接口域待办

Status: Active
Domain: INTERFACE
Canonical: `docs/interface/SCPI_HOST_TODO.md`
Related: `docs/interface/SCPI_HOST_ARCHITECTURE.md`, `docs/interface/SCPI_HOST_TASK_PROGRESS.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-13

## 任务表

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| IF-001 | 冻结“单串口 + 其余板卡经 RefMem 分布式控制”的**会话与寻址**语义 | PENDING | 用户 2026-10-13 指定拓扑 |
| IF-002 | 定义**命令转发与应答**规则（参考/forward 节点） | PENDING | 与 T2-AUDIT-08 同源；登记前需 C11 |

> 来源（2026-10-13，用户裁定）：“后续只有一个串口连接到 PC，其他板卡都用 REFMEM 进行分布式控制”。本项曾以 `T2-AUDIT-08` 形式误归于 T2 待办，现已移回本域；T2 待办仅保留指针。
| IF-003 | 明确**隔离与权限**：哪些命令允许跨板、哪些仅本板 | PENDING | 基于实测的 4 条节点作用域命令 |

## 决策输入（IF-002）

| 项 | 选项 | 说明 |
|---|---|---|
| 转发实现 | (a) SCPI 层透明转发 | 需定寻址/应答/超时/权限/幂等；偏离 SCPI 现有边界 |
|  | **(b) RefMem 控制意图 + delta（推荐）** | 保持命令本板作用域；由参考节点写控制意图，经既有 node-load sync 帧分发，各板本地 apply |

## 状态词汇

PENDING / IN PROGRESS / BLOCKED / DONE（与仓库统一状态词汇一致）。
