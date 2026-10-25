# tdma_service.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `components/tdma/src/tdma_service.c` 在 N5.0.4a 拆分开始前的完整副本，
用于审计与回退取证。它**不参与任何构建**（根 `CMakeLists.txt` 使用显式源文件列表，
不包含本目录），也不得删除。

## 2026-10-26：拆分前快照

- 归档时间：2026-10-26（Asia/Shanghai）
- 源文件：`components/tdma/src/tdma_service.c`，2,283 行 / 73 个函数 / 0 个文件级静态变量
- SHA256：`BFA4829DF89109AA6BDD396209D46FAB427C48CEFDC0F993C28E3AB70168F84C`
- 字节数：99,959

## 拆分边界（N5.0.4a）

| 单元 | 保留内容 | 目标行数 |
|---|---|---|
| `src/tdma_service.c`（留） | 时间/原子守卫/时间戳发布（39–155）、init/bind/profile/标定/生命周期/STOP/ARM（323–1230）、飞行访问器（2053–2200） | ≈1,227 |
| `src/tdma_service_core1.c`（新） | intent 提交（157–321）、调度与恢复（1232–1395）、Core1 执行（1400–1649）、CRC+快照（1651–2051）、结果帧（2206–2283） | ≈1,056 |

跨边界调用经实测为**单向**：Core1 单元调用主单元 10 个 helper，主单元对 Core1 单元
零调用。私有头 `inc/tdma_service_internal.h` 只导出这 10 个 helper，不新增状态定义。

## 规则

- 后续如需再次拆分，按 `tdma_service_split_v2/` 另建目录，不覆盖本目录。
- 任何被放弃的拆分尝试也只能移入本目录并在本文件记录原因，不得删除。
