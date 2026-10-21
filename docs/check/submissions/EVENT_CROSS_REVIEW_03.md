# 核验提交单：RefMem Event Plane 子域静态域架构 → RefMem/HAOFV

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/EVENT_CROSS_REVIEW_03.md`
Related: `docs/refmem/event/REFMEM_EVENT_ARCHITECTURE.md`, `docs/refmem/event/REFMEM_EVENT_TODO.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

## 1. 审查范围

对象：`REFMEM_EVENT_ARCHITECTURE.md` 静态域重写版、`REFMEM_EVENT_TODO.md` 静态域重排，以及 `REFMEM_DOMAIN_ARCHITECTURE.md`/`REFMEM_DOMAIN_TODO.md` 的事件面挂接。

目的：确认“Event Plane 只负责静态域、不碰实时域”的约束是否真正落实，跨文档是否一致，静态域是否有明确 owner。

## 2. 结论

**`PASS_WITH_CHANGES`**：Event Plane 文档本身已收敛到静态域，但跨文档仍有 2 项阻塞：

- RefMem 文档仍写着 `EVENT_INJECT`/`EVENT_ABORT` 和 Event Plane 运行时 intent，违反“静态 only”；
- Event Plane 静态 owner 未命名（`EventPlaneService` 被移除后没有替代），谁解析/校验/激活 EventPack、谁写 EventVector 未定义。

另有 `VALidate?`/`CONFigure` 命令语义、EventPack 路径/rollback、EventVector 存放等重大项。当前不能进入 E0 实现。

## 3. 阻塞项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `S-B1` | `REFMEM_DOMAIN_ARCHITECTURE.md` 事件面挂接/命令类型表；`REFMEM_DOMAIN_TODO.md` P8 | RefMem 仍列 `EVENT_INJECT`/`EVENT_ABORT`，并把 Event Plane 描述为运行时 intent consumer；Event Plane 静态域已不提供 INJect/ABORt | 删除/改归属：RefMem 只保留 `EVENT_CONFIGURE`（静态包装载）；`EVENT_INJECT`/`EVENT_ABORT` 归 RefMem/TRIGGER 运行时，不由 Event Plane 承接 |
| `S-B2` | `REFMEM_EVENT_ARCHITECTURE.md` §4/§7/§8 | 没有命名静态 owner：谁解析 EventPack、校验、activation、写 EventVector？原文只说“EventPack activation”，但 `EventPlaneService` 已从运行时移除 | 明确 `EventPackService`（或等价静态 owner）及其执行上下文（STOP 期、core0、非 AO/FB 或说明其 AO 身份）；写入 EventVector 的唯一 owner |
| `S-B3` | `REFMEM_EVENT_ARCHITECTURE.md` §6 | `SYSTem:EVENT:VALidate?` 是查询名，但校验是动作；`SYSTem:EVENT:CONFigure` 同时被描述为 staging/activation，语义不清 | 拆成明确命令：`CONFigure`=stage、`VALidate`=校验、`ACTivate`=激活；或明确单命令的 mode 字段。查询只读 `STATus?`/`MAPPing?` |

## 4. 重大项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `S-M1` | EventPack §4.1/§7 | 路径、manifest 集成、previous pack 存储/rollback 仍未定义（继承 R-B10） | 给出 pack 文件路径/manifest 行、previous pack 存放介质、rollback 原子性 |
| `S-M2` | EventVector §4.5 | EventVector 存放位置未定：本地 static、域 Vector 表还是 RefMem 表？只说“不进 64 KB” | 明确存储对象、owner、seqlock/CRC、读取 API |
| `S-M3` | Event Plane §6/RefMem 边界 | `EVENT_CONFIGURE` 的 ACK 是“accepted”还是“activated”？静态校验失败如何回写 ACK/NACK | 定义 command 完成点：accepted（staged）与 activated 分离；ACK 由 DistributedRefMemAO 写 |
| `S-M4` | AliasRegistry §4.4 | alias 的 `runtime owner` 已登记，但运行期谁执行、Event Plane 只提供静态映射的边界未写清 | 明确 alias 执行归 RefMem/TRIGGER；Event Plane 不执行 |
| `S-M5` | §2 HAOFV 表 | “静态部署 符合（设计）”与 S-M1 矛盾；EventPack 路径/rollback 未定 | 降级为“部分”，直到 S-M1 闭合 |
| `S-M6` | §4.5 `evg_slot` | EVG 运行时已 out of scope，但 EventVector 仍存 `evg_slot`；谁消费、何时写未定 | 明确它是静态席位声明，供 RefMem/TRIGGER 读取；无运行时 EVG 实现 |
| `S-M7` | §4.2 `payload_schema`/`flags` | 静态 schema 与运行时 payload 校验的边界未写清，可能被误读为 Event Plane 校验运行时 payload | 明确 Event Plane 只定义 schema，不校验运行时 payload |

## 5. HAOFV 符合性

| 原则 | 复评 |
|---|---|
| 表负责规则 | 符合（设计）：Catalog/Map/Alias/Pack |
| Vector 负责事实 | 部分：EventVector 静态字段有，但存放/owner 未定（S-M2） |
| 事件负责意图 | 符合（边界）：运行期归 RefMem/TRIGGER |
| 事件载荷所有权 | 不适用：本域不承载运行时 payload |
| AO 负责流程 | 部分：静态 owner 未命名（S-B2）；“不适用”不能代替 owner |
| Function Block | 不适用 |
| Resource Arbiter | 部分：activation 与运行时仲裁的边界未写 |
| Service Layer | 不适用 |
| 写权限 | 部分：EventVector writer 未具体化 |
| 分布式共同事实 | 符合（边界）：静态模型供运行期读取 |
| 分布式共同时间 | 不适用 |
| 分布式确定性通讯 | 不适用 |
| 静态部署 | 部分：EventPack 路径/rollback 未定（S-M1） |
| FB 非阻塞 | 不适用 |
| 顶层安全 | 符合（设计）：无实时路径 |

结论：**静态域方向符合；S-B1/S-B2/S-B3 闭合前不能判定“符合 HAOFV”**。

## 6. 与 RefMem 的一致性

- RefMem 的“命令槽唯一 owner”正确；Event Plane 不 take/不写 ACK 正确。
- 但 RefMem 仍列 `EVENT_INJECT`/`EVENT_ABORT`，与 Event Plane 静态域冲突（S-B1）。
- RefMem 的 `EVENT_CONFIGURE` 只应承接静态 EventPack 装载；运行期事件注入/中止归 RefMem/TRIGGER，不应经 Event Plane。

## 7. 建议修正顺序

1. `S-B1`：清理 RefMem 文档中的 `EVENT_INJECT`/`EVENT_ABORT` 与运行时 Event Plane 描述。
2. `S-B2`：命名静态 owner（建议 `EventPackService`）及其写入 EventVector 的职责。
3. `S-B3`：拆分 `CONFigure`/`VALidate`/`ACTivate` 语义。
4. `S-M1`/`S-M2`：EventPack 路径/rollback、EventVector 存放。
5. `S-M3`–`S-M7`：ACK 完成点、alias 执行边界、EVG 静态席位、schema 边界。

## 8. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复评 R3（同一 agent 的 fresh pass，非第三方） |
| 审核方式 | 文档交叉 + RefMem/HAOFV 对照 |
| 审核结论 | `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-21 |
| 备注 | 不构成 C11 最终签署；E0 前需另一 agent/人工复核并签署 |
