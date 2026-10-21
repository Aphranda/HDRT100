# 核验提交单：RefMem Event Plane 子域迁移与静态域复审 R4

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/EVENT_CROSS_REVIEW_04.md`
Related: `docs/refmem/event/REFMEM_EVENT_ARCHITECTURE.md`, `docs/refmem/event/REFMEM_EVENT_TODO.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

## 1. 审查范围

对象：

- `docs/event/` → `docs/refmem/event/` 的迁移与文件更名；
- `REFMEM_EVENT_ARCHITECTURE.md`/`REFMEM_EVENT_TODO.md` 静态域内容；
- `REFMEM_DOMAIN_ARCHITECTURE.md`/`REFMEM_DOMAIN_TODO.md` 的事件面挂接；
- 索引、父域 README、评审记录路径。

目的：确认迁移一致、静态域约束仍成立、S-B1/S-B2/S-B3 是否闭合。

## 2. 结论

**`PASS_WITH_CHANGES`**。

- 迁移本身通过：路径、命名、Domain、索引、父域引用一致；旧 `docs/event/` 已删除。
- `S-B1` 已闭合：RefMem 只保留 `EVENT_CONFIGURE`，`EVENT_INJECT/ABORT` 已明确不归 Event Plane。
- `S-B2`、`S-B3` 仍未闭合：静态 owner 未命名、命令语义仍不清。
- `S-M1`（EventPack 路径/manifest/rollback）、`S-M2`（EventVector 存放）、`S-M5`（静态部署“符合”过强）仍开放。

## 3. 迁移检查

| 项 | 结果 |
|---|---|
| 目录 `docs/refmem/event/` | 通过 |
| 文件 `REFMEM_EVENT_ARCHITECTURE/TODO/TASK_PROGRESS` | 通过 |
| `Domain: REFMEM` | 通过 |
| `docs/README.md` 域入口/canonical/进度/文件索引 | 通过 |
| `docs/refmem/README.md` 子域入口 | 通过 |
| `DOCS_DOMAIN_STRUCTURE_PLAN` 子域归属 | 通过 |
| 三份 `EVENT_CROSS_REVIEW_*` 的 Related/正文路径 | 通过 |
| 旧 `docs/event/` | 已删除 |
| 残留旧路径引用 | 无（仅迁移说明中的历史目录名） |

## 4. 仍开放项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `S-B2` | 子域架构 §4.1/§4.5；RefMem 架构事件面 | 子域文档没有命名静态 owner；`EventVector` writer 写成 “EventPack activation”，没有 `EventPackService` 或等价组件。RefMem 侧已写 `EventPackService`，两边不一致 | 在子域架构中明确 `EventPackService`（或等价）及其执行上下文、EventVector 唯一 writer；与 RefMem 侧同名 |
| `S-B3` | 子域架构 §6 | `SYSTem:EVENT:VALidate?` 是查询名，但校验是动作；`SYSTem:EVENT:CONFigure` 同时被写成 staging/activation | 拆成 `CONFigure`=stage、`VALidate`=校验、`ACTivate`=激活；或单命令 mode 字段。查询只读 `STATus?`/`MAPPing?`。命名建议对齐 RefMem 的 `LOAD/ACTivate` 习惯 |
| `S-M1` | 子域架构 §4.1/§7 | EventPack 路径、manifest 集成、previous pack/rollback 仍未定义 | 给出路径/manifest、previous pack 存放介质、rollback 原子性 |
| `S-M2` | 子域架构 §4.5 | EventVector 存放位置未定：本地 static、域 Vector 表，还是别处？ | 明确存储对象、owner、seqlock/CRC、读取 API |
| `S-M3` | 子域架构 §6；RefMem 事件面 | `EVENT_CONFIGURE` 的 ACK 完成点未定：accepted（staged）还是 activated？校验失败如何 NACK | 定义 command 完成点；ACK/NACK 由 `DistributedRefMemAO` 写 |
| `S-M5` | 子域架构 §2 | “静态部署 符合（设计）”与 S-M1 矛盾 | 降为“部分”，直到 EventPack 路径/rollback 闭合 |
| `S-M6` | 子域架构 §4.5/§5.3 | `evg_slot` 存在，但 EVG 运行时 out of scope；谁消费、何时写未定 | 明确它是静态席位声明，供 RefMem/TRIGGER 读取；无运行时 EVG 实现 |
| `S-M7` | 子域架构 §4.2 | `payload_schema`/`flags` 只定义 schema，不校验运行时 payload；边界未写清 | 明确 Event Plane 只定义 schema，运行期校验归 owner |

## 5. 新发现（迁移相关）

| ID | 问题 | 要求 |
|---|---|---|
| `S-M8` | 契约 ID 仍是 `EVENT-*`，但域已迁到 REFMEM 子域；登记时 `domain` 字段应写 `refmem` 还是 `event` 未定 | 在 E0 登记前统一：建议 `domain=refmem`，contract_id 保留 `EVENT-*` 作为子域前缀 |
| `S-M9` | 进度条目仍用 `EVENT-TASK-*`，文件名是 `REFMEM_EVENT_TASK_PROGRESS.md`；ID 与文件前缀不一致 | 保留 `EVENT-TASK-*`（稳定 ID）并在迁移说明中注明，或统一为 `REFMEM-EVENT-*`；不要两边混用 |

## 6. HAOFV 符合性

| 原则 | 复评 |
|---|---|
| 表负责规则 | 符合（设计） |
| Vector 负责事实 | 部分：EventVector 存放/owner 未定（S-M2） |
| 事件负责意图 | 符合（边界）：运行期归 RefMem/TRIGGER |
| AO/流程 | 部分：静态 owner 未命名（S-B2） |
| 静态部署 | 部分：EventPack 路径/rollback 未定（S-M1） |
| 写权限 | 部分：EventVector writer 未具体化 |
| 其余运行时项 | 不适用 |

结论：**静态域方向符合；S-B2/S-B3/S-M1/S-M2/S-M5 闭合前不能判定“符合 HAOFV”**。

## 7. 建议修正顺序

1. `S-B2`：子域命名 `EventPackService`，与 RefMem 侧统一；EventVector writer 写具体。
2. `S-B3`：拆 `CONFigure`/`VALidate`/`ACTivate`，或定义 mode。
3. `S-M1`/`S-M2`：EventPack 路径/rollback、EventVector 存放。
4. `S-M5`：降级静态部署判定。
5. `S-M8`/`S-M9`：契约 domain/进度 ID 口径。
6. 其余 S-M3/S-M6/S-M7。

## 8. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复评 R4（同一 agent 的 fresh pass，非第三方） |
| 审核方式 | 迁移一致性 + 静态域/父域对照 |
| 审核结论 | `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-21 |
| 备注 | 不构成 C11 最终签署；E0 前需另一 agent/人工复核并签署 |

## 9. 修正记录（2026-10-21，复审后）

- `S-B2` 已闭合：子域架构新增 `EventPackService` 静态 owner；EventVector 唯一 writer 明确；RefMem 侧同名。
- `S-B3` 已闭合：`CONFigure`=stage、`VALidate`=校验、`ACTivate`=激活；`VALidate` 改为动作命令，`STATus?`/`MAPPing?` 只读。
- `S-M1` 已有规则：EventPack 路径 `/refmem/event/event_pack.bin`、previous `event_pack.prev.bin`、manifest 可选行、rollback 切换；实现未做。
- `S-M2` 已有规则：EventVector 为 `components/event_plane/src/event_vector.c` 的 `s_event_vector`，seqlock + `event_plane_get_static_snapshot()`；实现未做。
- `S-M5` 已改为“设计符合，路径/rollback 见 §4.1”；仍以实现为准。
- `S-M8` 已定：`domain=refmem`，contract_id 保留 `EVENT-*` 子域前缀。
- `S-M9` 采用：进度 ID 保留 `EVENT-TASK-*`（稳定 ID），文件前缀 `REFMEM_EVENT_` 表示父域归属；不要求两者相同。
- 结论仍为 `PASS_WITH_CHANGES`：架构阻塞项已改，但实现、契约登记和 C11 签署未完成。
