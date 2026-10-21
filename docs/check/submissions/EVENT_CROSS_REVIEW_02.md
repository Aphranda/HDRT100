# 核验提交单：RefMem Event Plane 子域架构补充 → RefMem/TRIGGER/HAOFV

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/EVENT_CROSS_REVIEW_02.md`
Related: `docs/refmem/event/REFMEM_EVENT_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-21

## 1. 审查范围

对象：`REFMEM_EVENT_ARCHITECTURE.md` 第三版（新增 §5.4.1、§6.1–§6.5、§9.1、§10.1、§14.1）。

目的：复核 R-B1/B2/B4/B5 是否真正闭合，确认是否引入新问题，并检查与 TRIGGER 实时域、RefMem 命令槽、HAOFV 的边界。

## 2. 结论

**`PASS_WITH_CHANGES`**：R-B1/B2/B4 的规则有实质补充；R-B5 仍是“交给 `EVENT-TRANSPORT-01` 联审”的延期；R-B3 仍缺预算。新增 **B6：POSITION 与 Event Plane EVR 的接收路径重叠**，是新的阻塞项。

不能进入 E0 契约冻结/实现，不能判定“符合 HAOFV”。

## 3. 新增阻塞项

| ID | 位置 | 问题 | 要求 |
|---|---|---|---|
| `R-B6` | 架构 §5.4/§5.4.1 | 写“POSITION 与 CONTROL_EVENT 两类都进同一个 EVR”，但 POSITION 的实时接收/预测/执行已归 TRIGGER（T2 的 `accept_remote_position`、`TriggerRun`）。Event Plane EVR 再接收 POSITION 会形成第二条接收路径 | 明确：CONTROL_EVENT 进 Event Plane EVR；POSITION 仍由 T2 接收，Event Plane 只通过只读 hook/记录观察，不复制 EVR 或 `TriggerRun` |
| `R-B7` | 架构 §6.3/§10 | `EVENT_CONFIGURE` command type 存在，但 canonical 命令列表没有对应的 SCPI 入口（只有 INJect/STATus/LOG/ABORt/MAPPing） | 增加 `SYSTem:EVENT:CONFigure`（或 `PACKage`）及参数/返回；与 `EVENT_CONFIGURE` 1:1 |
| `R-B8` | 架构 §6.2/§10 | `event_id` 与 `command_seq` 只存 `last_*`；`SYSTem:EVENT:LOG? [index]` 不能按 `event_id`/`command_seq` 查询，1:1 映射无法实际回查 | 定义按 `event_id`/`command_seq` 的查询接口，或 EventLog 的双键索引；别名 legacy `1` 的回查路径要可执行 |
| `R-B9` | 架构 §4.3/§7 | `EventSession` 与 `EventStatus.session_state` 仍是两处状态描述；`EventSession` 没有明确存储/owner | 明确 `EventSession` 是 EventStatus 的投影还是独立对象；只保留一个 writer/state |
| `R-B10` | 架构 §6.3/§10.1 | EventPack 的存储路径、manifest 集成、rollback buffer、alias 宏生成方式仍未定义；“System Pack blob”只是方向 | 给出 pack 路径/格式、与 RefMem `.rmtp` 的边界、rollback 保留策略、alias 生成/校验命令 |

## 4. 仍开放项

| ID | 状态 | 说明 |
|---|---|---|
| `R-B3` | **未闭合** | `EventPlaneService` 挂 `task_refmem_sync` 的 WCET/栈/RAM 仍未分析；没有预算就不能证明不影响 REFMEM/T2 相位 |
| `R-B5` | **延期** | §5.4.1 给了优先级/预算符号，但 class ID、buffer offset、seat 映射仍待 `EVENT-TRANSPORT-01` 联审；不能算闭合 |
| `R-M2` | **未闭合** | `event_owner` 仍只说“进入 BoardCapability/SlotClaim”，没有具体 bit/字段/gate 条件 |
| `R-M5` | **未闭合** | `event_lease_epoch` 复用 RefMem lease，但 RefMem 的 `REFMEM-CTRL-01` 租约本身尚未实现；Event Plane 不应把未实现能力当依赖 |
| `R-M4` | **部分** | post 队列/背压有规则；队列内存 owner、RAM 预算、core1 mailbox 实例仍未定 |

## 5. 对 R-B1/B2/B4 的复核

| 项 | 判定 | 说明 |
|---|---|---|
| `R-B1` handler 上下文/ACK | 部分通过 | 上下文/ACK 写者已明确；但 handler 返回 `BUSY` 后的异步完成路径未定义（谁回调 `DistributedRefMemAO` 写最终 ACK） |
| `R-B2` event_id/command_seq | 部分通过 | 分配者/1:1 已定义；查询接口与多事件排队回查不足（见 B8） |
| `R-B4` EventPack | 部分通过 | header/装载顺序有；路径/manifest/rollback/alias 生成不足（见 B10） |

## 6. HAOFV 符合性

| 原则 | 复评 |
|---|---|
| 表负责规则 | 部分：EventPack 路径/rollback 未定 |
| Vector 负责事实 | 部分：EventVector 静态已定；EventSession/EventStatus 仍重复（B9） |
| 事件负责意图 | 符合（设计） |
| 事件载荷所有权 | 部分：wire budget 仍待 B5 |
| AO 负责流程 | 部分：AO/task 预算缺（B3） |
| FB 负责逻辑 | 部分：post 异步完成路径缺 |
| Resource Arbiter | 部分：event_owner 未具体化 |
| Service Layer | 符合（设计） |
| 写权限 | 部分：EventSession/EventStatus writer 重复 |
| 分布式共同事实 | 部分：命令槽 owner 正确；RefMem lease 依赖未落地 |
| 分布式共同时间 | 符合（设计） |
| TDMA 通讯 | 部分：class/buffer 未联审（B5） |
| 静态部署 | 部分：EventPack 路径/回滚未定 |
| FB 非阻塞 | 部分：post 异步完成/背压未闭环 |
| 顶层安全 | 部分：日志/RAM 预算未定 |

结论：**方向符合，不能判定“符合 HAOFV”**；B6–B10 + B3/B5 先闭合。

## 7. 建议修正顺序

1. `R-B6` POSITION/EVR 接收路径去重（影响实时域边界，最高优先）。
2. `R-B7` canonical `EVENT_CONFIGURE` 命令。
3. `R-B8` event_id/command_seq 查询接口。
4. `R-B9` EventSession/EventStatus 单一 writer。
5. `R-B10` EventPack 路径/manifest/rollback/alias 生成。
6. `R-B3` core0 service 预算。
7. `R-B5` TDMA 联审。

## 8. C11 记录

| 项 | 内容 |
|---|---|
| 审核方 | 独立复评 R2（同一 agent 的 fresh pass，非第三方） |
| 审核方式 | 文档交叉 + 与 T2/RefMem/HAOFV 对照 |
| 审核结论 | `PASS_WITH_CHANGES` |
| 审核日期 | 2026-10-21 |
| 备注 | 不构成 C11 最终签署；E0 前需另一 agent/人工复核并签署 |

## 9. 约束变更记录（2026-10-21，复审后）

用户约束：**Event Plane 目前只负责静态域，不碰实时域**。架构已按此重写：

- `R-B6` 关闭：POSITION/EVR/transport/TriggerRun 已从 Event Plane 移除；POSITION 仍归 TRIGGER，Event Plane 不再接收。
- `R-B7` 关闭：静态接口保留 `SYSTem:EVENT:CONFigure/STATus?/MAPPing?/VALidate?`；不提供 `INJect/ABORt`。
- `R-B8`/`R-B9` 归为运行时（RefMem/TRIGGER）范围，不在本域；EventStatus/EventLog 运行态不再由 Event Plane 定义。
- `R-B5` 归 TDMA/TRIGGER，不在本域；`R-B3` 实时预算不适用，但静态 activation/service 的 core0 预算仍需评估。
- `R-B10` 仍开放：EventPack 路径/manifest/rollback/alias 生成未冻结。

结论调整为：**静态域范围 `PASS_WITH_CHANGES`**；剩余阻塞是 EventPack 路径/rollback 与静态表实现，不再是实时域问题。C11 最终签署仍待独立复核。
