# 核验提交单：TRIGGER（T2 位置邮箱） → RefMem/TDMA 层

Status: Active
Domain: TRIGGER
Canonical: `docs/check/submissions/TRIGGER_CROSS_REVIEW_02.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-13

> 提交方: T2 域（作者） → 核验方: **独立 AI 评审**（独立 subagent `c925441d-a324-456c-8606-d040f627e01a`）；
> 方式: 代码与文档**只读交叉核验**（C11）；核验未创建/修改/删除任何文件，未运行构建或硬件工具。
> 结论: **APPROVE WITH NOTES**（无编造项；四项为措辞/归属缺陷，已按下述**修正登记行与小节**）。
> 契约: `ARCH-TRIGGERMAILBOX-01`（status `pending`，2026-10-13 登记）。

## 提交内容

| 父层/对端条款 | 符合性 | 证据 |
|---|---|---|
| ① 结构体成员逐字一致（`volatile state` + `target_mask` + `descriptor`） | ✅ | `components/sync_trigger/inc/trigger_position_mailbox.h:11-15`；类型 `trigger_position_descriptor.h:21-37` |
| ② 四态状态机真实存在（EMPTY=0/WRITING=1/READY=2/READING=3） | ✅（附注：另有 READY→EMPTY 直通复位） | 取值 `mailbox.h:17-22`；转移 `:44-46`／`:49-50`／`:60-62`／`:65-66`；附加复位 `distributed_refmem.c:471-475`、`:1948`、`:1962` |
| ③ 载荷仅在 WRITING 写、仅在 READING 读 | ✅（**运行时**；静默边界例外已入条款） | 写 `mailbox.h:47-48`；读 `:63-64`；全仓 grep `mailbox(.\|->)(descriptor\|target_mask)` 仅此 4 行；`pending()` `:27` 及各诊断点只读 `state` |
| ④ READY 为 release/acquire 交接 | ✅（配对真实，只强不弱） | 发布 `mailbox.h:49-50` RELEASE ↔ 接收 `:60-62` CAS ACQ_REL（含 acquire）；反向 `:65-66` RELEASE ↔ `:44-46` ACQ_REL |
| ⑤ `TRIGGER_POSITION_FREEZE_LATCH_IDS = 15u` 与 `[1, LATCH_IDS]` | ⚠️ 域口径已限定 | `mailbox.h:74`／`:77`／`:84`（`>` 非 `>=`，含端点）；但 `1..15` 是 **latch 槽位与线上 nibble 的寻址域**，非端到端消费域 |
| ⑥ RefMem 跨域使用，文件与行号精确 | ✅ | `components/distributed_refmem/src/distributed_refmem.c:30` 逐字 include；实例 `:301-305`；stage/take/pending `:668`、`:687`、`:701`、`:714`、`:720`、`:729`、`:770`、`:1345`、`:1374`、`:1409` |
| ⑦ clause 文档 Status 满足登记门禁 | ✅ | `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md:3` `Status: Active`；门禁 `tools/doc_regression_check.py:256-261` |
| ⑧ code_anchor 真实且承载语义 | ✅ | `components/sync_trigger/inc/trigger_position_mailbox.h`（112 行）：协议 `:9-22`、`:31-68`；latch `:74`、`:84` |
| ⑨ `contract_id` 符合 `ROW_RE` 前缀白名单 | ✅ | `tools/doc_regression_check.py:89-91`；实测 `OK registry: 39 contracts`（HEAD 38 + 本行） |
| ⑩ latch 归属是否清晰（是否属 mailbox 契约范围） | ⚠️ 已显式声明 | `mailbox.h:75-78`、`:81-91`、`:94-110`；协议注释 `:70-73` |
| ⑪ 与 `ARCH-TRIGGERSLICE-01` 是否重叠/冲突 | ⚠️ 不矛盾，但曾重复立法 | slice `trigger_position_slice.h:38`、`:65`；latch `mailbox.h:74`、`:77`、`:84`；**无静态断言绑定二者** |
| ⑫ 是否存在过度声明（硬约束 2、6 边界） | ⚠️ 两处口径不一致 | `trigger_position_descriptor.h:193`；`trigger_t2_runtime.c:403-404`；`trigger_t2_runtime.h:12`；文档 `:485` vs `:487` |

## 偏差声明

核验方指出四项**措辞/归属缺陷**（均已修正，无实现缺失、无编造）：

1. **（A，重点）** 原措辞把 `[1, LATCH_IDS]` 写成通用「有效 `position_id` 域」，与 `ARCH-TRIGGERSLICE-01` 的线上域**重复立法**且未划分所有权，同时被代码消费域收窄（15 vs 3）。
   → 已改为：明确这是 **FREEZE latch 槽位域**；线上域归 `ARCH-TRIGGERSLICE-01`；T2 运行时消费域为 `[1, TRIGGER_T2_RUNTIME_POSITION_COUNT]`；FREEZE validator 另限 `position_id <= 3u`；三方收敛另立 `T2-IDENT-002`。
2. **（B）** 小节标题写「冻结」，与同文档 `:485`「本文当前仍为 Draft……登记前不把本文数值快照当产品契约」及本行 status `pending` 相左。
   → 已改为「（2026-10-13 登记，status `pending`）」，并重写 `:485` 的 Draft 表述以消除自相矛盾。
3. **（C）** 「载荷**仅**在 WRITING 写、**仅**在 READING 读」对并发运行期成立，但 init `memset` 在 EMPTY 态写整块载荷（`distributed_refmem.c:417-418`），且 STOP／环拓扑复位可**未经 READING** 直接置 EMPTY 丢弃 READY 载荷（`:471-475`、`:1948`、`:1962`）。
   → 已限定为「**运行时（并发交接期）**」，并补记静默边界清空载荷与 WRITING/READING 期间拒绝复位（`:1927`、`:1931`）。
4. **（D）** FREEZE latch 被并入 mailbox 契约行，易被读成受「载荷仅在 WRITING 写／READING 读」约束。
   → 已在登记行与小节**显式声明**：latch 是同头文件内**独立于 mailbox** 的结构体，其载荷规则为「bit 清写／bit 置读」，**不适用**四态规则。

**本次审核的新产出（不属本行缺陷，另立待办）**：同一「位置」域在代码内存在**三方不一致**——

- 线上／latch 寻址域 `[1, 15]`：`trigger_position_slice.h:38` 的 `0x0Fu` 与 `trigger_position_mailbox.h:74` 的 `TRIGGER_POSITION_FREEZE_LATCH_IDS`，二者**无静态断言绑定**；
- `trigger_position_descriptor_valid()` 的 FREEZE 分支硬编码 `position_id <= 3u`（`trigger_position_descriptor.h:193`，**未引用宏**）；
- T2 运行时消费域 `[1, TRIGGER_T2_RUNTIME_POSITION_COUNT]`（`= 3u`，`trigger_t2_runtime.h:12`；拒绝见 `trigger_t2_runtime.c:403-404`）。

数值当前自洽，但改宏不会带动该字面量；已登记为 **`T2-IDENT-002`**（不预设裁决）。

## Alternatives considered

- **方案A** 把 latch 拆为**独立登记行**（拒绝：两结构体同锚点、同头文件，拆行只增加维护面；显式归属声明已消除歧义。待 `T2-IDENT-002` 收敛时再评估是否需要独立行）。
- **方案B** 把 FREEZE validator 的 `3u` **立即改为宏引用**（拒绝：属固件改动，按 AGENTS.md 硬约束 7 需付一次四板 P3 凭证；且 `trigger_position_descriptor.h` 是否应依赖 `trigger_t2_runtime.h` 属设计问题，应由 `T2-IDENT-002` 裁决，不在本提交内夹带）。
- **方案C** 本条 status 由 `pending` 改 `active`（拒绝：见「status 说明」）。

## 核验结论

- 结论: `ACCEPT_WITH_DEVIATION`（对应审核方 `PASS_WITH_NOTE`；四项措辞/归属缺陷已按意见修正，无实现缺失）
- 核验人: T2 域（作者侧；**仅按审核意见实施修正**，不参与本次核验的判定）

## 交叉审核记录（C11，必填）

- 审核方: 独立交叉审核 agent（独立 subagent `c925441d-a324-456c-8606-d040f627e01a`），与起草方**不同会话、未参与起草**，仅只读核验
- 审核方式: agent 交叉／代码与文档**只读**交叉核验（未创建、修改、删除任何文件；未 `git add`／`commit`／`checkout`／`stash`；未运行构建或硬件工具；仅运行两个只读文档检查器）
- 审核结论: `PASS_WITH_NOTE`
- 审核日期: 2026-10-13

## status 说明

`ARCH-TRIGGERMAILBOX-01` 保持 **`pending`**，与同域先例 `ARCH-TRIGGERSLICE-01` 一致。理由：本次证据为**静态只读核验**，
不含同源码构建与四板 P3；且 `T2-IDENT-002` 的域口径尚未收敛，此时置 `active` 等于冻结一条被消费端代码收窄的域声明。

## 未核验范围（据实记录）

- 未运行任何构建／HIL／仿真，未运行 `tools/hardware_acceptance/*`（核验约束禁止）→ release/acquire 仅作**内存序与配对的形式核验**，未做多核压力或形式化验证。
- 未运行 pytest（为遵守「不创建文件」约束）→ `tests/python/` 中相关用例当前是否全绿**未经核验方确认**。
- 未核验 latch「每位置首个 FREEZE 生效」的跨板硬件行为；未核验 `target_mask <= 0xFFu`（`distributed_refmem.c:664`）与 8 节点拓扑的一致性。
- 未做意图认定：`1..3` 上限是设计意图还是缺陷，留待 `T2-IDENT-002`。
