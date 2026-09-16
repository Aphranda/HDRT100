# RefMem 静态缩容评审结论与 TODO 移交

Status: Active
Domain: RefMem
Canonical: `docs/check/submissions/REFMEM_CROSS_REVIEW_01.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-09-17

> 范围：**只做范围小、收益大的静态缩容**——压缩预留空间，**保留 owner、区域 ID 与现有同步机制**。
> 不含 MPU 启用、动态租约与区域重分类（三者另开轨道）。
> 依据 = 代码锚点 + ARM 实编 sizeof 结果 + 指定构建 map 快照；**容量以链接 map 为准**。
> DPLL/VDC 始终常驻；特等席快速路径不引入 lease、TLV 解析或 RefMem 分片依赖。
> 本文件保存审查依据；后续实施、布局规范与验收分别由 RefMem TODO、Architecture 和 Task Progress 承接。
> 临时评审本身不登记为契约，也不代替实现或硬件验收。

> 审查已转执行待办：[REFMEM-RAM-001 至 004](../../refmem/REFMEM_DOMAIN_TODO.md#当前优先插入任务refmem-静态缩容)。
> 用户要求的下次接续入口位于 [VDC TODO](../../vdc/VDC_DOMAIN_TODO.md#下次接续入口先释放-ram再返回特等席闭环)。
> 本文件保留提案与独立审查依据，不再维护第二份实施状态；当前进展见 `REFMEM-TASK-20260916-001`。

## 1. Review 状态字段

| 字段 | 值 |
|---|---|
| review_scope | RefMem 向量表静态缩容：64 KiB → 18 KiB，保留全部 16 个区域 ID 与 owner |
| review_basis | 代码锚点 + ARM 实编 sizeof + 指定构建 map 快照（**快照，非事实源**） |
| review_verdict | `ACCEPTED_FOR_TODO`：接受最小静态缩容方向；容量和兼容测试仍须按 TODO 验证，不代表实现验收通过 |
| review_blocking_layer | RefMem 域（容量与 offset 定义）；依赖 TDMA / SYNC_IO 对每节点摘要上限的确认 |
| review_c11 | 独立方案复核及后续 `REFMEM-LAYOUT-01` pending 登记审核已完成；本评审不登记为契约，严格产品验收不由此授予 |
| review_impact_on_contracts | `layout_version` 1 → 2（区域 offset 变更、`node` 槽步长 512 → 128 B）；须在 `docs/check/DOCS_REGISTRY.md` 登记并验证旧部署包拒绝路径 |
| review_date | 2026-09-16 |

**代码锚点**：`components/distributed_refmem/src/refmem_vector_table.c`、`refmem_vector_table.h`、
`distributed_refmem.h`、`refmem_sync.h`、`refmem_sync_frame.h`、`refmem_vdc_vector.h`、
`refmem_application_model.c`、`refmem_slot_claim.c`、`refmem_table_registry.{c,h}`；
`components/tdma/inc/tdma_flight_engine.h`、`tdma_transport_frame.h`。

## 2. 本轮范围与后续轨道

| 轨道 | 内容 | 本轮 |
|---|---|---|
| **本轮** | 静态缩容：保留区域 ID / owner / 同步机制，压缩预留空间与槽步长 | ✅ |
| 后续 | MPU 启用与保护边界 | ❌ 另开轨道 |
| 后续 | 动态租约（T1 按需 commit） | ❌ 另开轨道 |
| 后续 | 区域重分类 / 合并 / 删除 | ❌ 另开轨道 |

## 3. 父层条款符合性

| 父层条款 | 现状 | 本轮 | 说明 |
|---|---|---|---|
| `Vector 字段契约` | 应用模型侧已有字段契约 | ✅ 不改字段语义 | 本轮只改容量与 offset，字段 writer / lifecycle 定义不变 |
| `Vector 不保存大块数据` | 存在 8 KB 级区域 | ➖ **本轮不动区域语义** | 重分类另开轨道；**不再以本条作为删除区域的理由** |
| `分布式共同事实` / `分布式共同时间` | ✅ | ✅ 区域 ID 与 owner 不变 | — |
| `分布式确定性通讯` | ✅ | ✅ 跨节点实时载荷仍在 TDMA SHORT process image | 本表只承载 RefMem 自身事实 |
| `双核 Flash/XIP 安全` | ✅ | ✅ `header` 的 RuntimeProtection 字段不动 | — |
| `跨核共享事实` | ⚠️ **待补证** | ➖ 本轮不改同步机制 | `node` 当前读写有 `osal_critical` 保护，**不能仅凭缺 seqlock/CRC 判定存在裸竞争**；该锁是否适合具体实时路径需另查，不作为本轮阻断项 |
| `REFMEM-260B-01` | ✅ | ✅ 但**不以整表刷新为容量依据** | 见 §4.1；契约原文含 36 B 头，净 delta ≤ 224 B，并明确"RefMem 不做周期整表刷新" |
| `REFMEM-PERSIST-01` | **登记状态 pending，非冻结约束** | ➖ 本轮不据此改动 | 不得据此认定"分配 8 KiB 校准 RAM = 持久化整张运行表" |

## 4. 现状取证

### 4.1 传输契约（修正上一版错误）

| 项 | 值 | 来源 |
|---|---:|---|
| critical delta 的 RefMem 内帧上限 | 260 B（**含 RefMem 头 36 B**） | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md:536` |
| 净 delta 上限 | **224 B** | 同上 |
| 更大事实 | 走分片、background delta 或后续专用 bulk class | 同上 |
| **周期整表刷新** | **禁止**；TDMA short queue 只接收由 dirty fact 触发的局部编码 critical delta；首次加入/失步恢复的 full snapshot 只走 maintenance long-frame 分片 | 同上 `:537` |
| 6 ms 发布间隔 / 4 B 分片 | **仅为 VDC 参考发布的传输条件**，不是所有 RefMem 数据的统一条件 | `DISTRIBUTED_REFMEM_REFERENCE_PUBLISH_INTERVAL_MS`、`REQMEM_SYNC_VDC_FRAGMENT_DATA_SIZE` |

**据此撤回**上一版的"全表轮刷 96 ms / 100 ms 预算"测算，以及由它推出的"T0-B 只换不扩"扩容限制。
现有路线是**变化增量发布**，整表轮刷时间不是容量依据；本地保留较大地址空间也不直接违反
单帧容量契约。

### 4.2 有效占用（ARM 实编 + map，快照，非事实源）

| 对象 | 当前有效占用 | 本轮分配 | 判定 |
|---|---:|---:|---|
| VDC payload + 同步前缀 | 232 B | 1 KiB | 足够 |
| DPLL payload + 同步前缀 | 224 B | 1 KiB | 足够 |
| Node | 每槽 512 B，其中现有字段 36 B | 每槽 128 B × 8 = 1 KiB | 可行，**但必须改槽步长** |
| HEADER | RuntimeProtection、目录及其他现有字段 | 1 KiB | 保持现有容量与字段 |
| 11 个普通字节数组区域 | 逐项核对实际字段访问上界及应用模型引用 | 各 1 KiB 候选 | 保留 ID，压缩预留 |
| TLV | 当前分配 2 KiB，不等于已实现同等字节的语义 | 3 KiB 候选 | 额外 1 KiB 只是静态预留，不增加权限或协商机制 |

注：RefMem 当前**固定 8 槽，未跟随六节点编译配置**；本轮不改这一点。

### 4.3 「无运行时字节读写」不等于「无契约依赖」（修正）

- 应用模型侧已有字段契约（`refmem_model_*_crc32` 一族）。
- 实例、事件与门禁**引用** `STATS` / `FAULT` / `GATEWAY` 等区域（`evidence_index`、
  `REQMEM_APP_GATE_*` 证据落点）。
- 因此**删除或重排区域 ID 必须同步迁移这些引用，并验证旧部署包的拒绝路径**。
- **本轮决定：保留全部区域 ID，只压缩容量**——ID 不变则上述引用无需迁移，风险最小。

## 5. 提案布局：16 区域 / 18 KiB，1 KiB 区块，区域 ID 不变

| id | 区域 | offset | size | owner（不变） |
|---|---|---|---:|---|
| 0 | `HEADER` | `0x0000` | 1,024 B | SystemAO / ConfigGate |
| 1 | `SYSTEM` | `0x0400` | 1,024 B | SystemAO |
| 2 | `ROLE` | `0x0800` | 1,024 B | SystemAO / config loader |
| 3 | `VDC` | `0x0C00` | 1,024 B | VdcSyncAO（**始终常驻**） |
| 4 | `LOOP` | `0x1000` | 1,024 B | LoopEngineAO |
| 5 | `DPLL` | `0x1400` | 1,024 B | AngleDpll owner（**始终常驻**） |
| 6 | `NODE` | `0x1800` | 1,024 B | 各节点 owner（8 × 128 B） |
| 7 | `TRIGGER` | `0x1C00` | 1,024 B | 各节点 core1 摘要 |
| 8 | `IO` | `0x2000` | 1,024 B | 各节点 IO owner |
| 9 | `CAL` | `0x2400` | 1,024 B | CalibrationAO |
| 10 | `STATS` | `0x2800` | 1,024 B | Statistics / Measure owner |
| 11 | `ACK_CMD` | `0x2C00` | 1,024 B | 命令 owner + 节点 ACK |
| 12 | `FAULT` | `0x3000` | 1,024 B | SystemAO / DiagnosticsAO |
| 13 | `GATEWAY` | `0x3400` | 1,024 B | GatewayAO |
| 14 | `SERVICE` | `0x3800` | 1,024 B | 对应 task owner |
| 15 | `TLV` | `0x3C00` | 3,072 B | 沿用现有声明；静态预留不新增 writer |

**合计 18,432 B = 18 KiB**，目录覆盖 `0x0000`–`0x4800`，无缝连续。相对当前 64 KiB
**理论释放 46 KiB**，最终以链接 map 为准。

三点说明：

1. **区域 ID 与 owner 全部保持**，应用模型 / 实例 / 事件 / 门禁的 region-id 引用无需迁移；
   变的是 offset（由目录描述）与 `node` 槽步长。
2. **`NODE` 槽步长 512 → 128 B 属 ABI 变更**，必须与 `layout_version` 一并处理，不能只改区域大小。
3. 16 个区域均为 1 KiB 整数倍、offset 对齐到自身大小，便于分块校验。

### 备用（TLV 窗内的 1 KiB）

本候选将原有 TLV 分配增加 1 KiB 作为静态余量；不代表该部分已有有效字段、写权限或
协商能力。保留原 TLV 容量也可形成更小的候选，最终按实际需求决定。新增备用写权限、
fail-closed 写入口和协商机制属于后续独立设计，不随本轮 RAM 缩容实现。

| 能吸收 | 不能吸收 |
|---|---|
| 新字段（TLV 编码 + 版本号） | 新的一级区域（改 offset ⇒ `layout_version` 再涨价） |
| 既有字段扩大（本区域内） | 需要独立 snapshot 语义的常驻事实 |
| 诊断/维护期一次性扩展窗 | — |

## 6. 偏差声明

- `layout_version 1 → 2`：接受理由 = 区域 offset 与 `NODE` 槽步长变更；**保留区域 ID 与 owner
  以降低引用迁移风险**。落地前须在 `docs/check/DOCS_REGISTRY.md` 登记并完成 C11。
- **撤回上一版两条依据**：① 不再以 `Vector 不保存大块数据` 作为删除区域的理由（重分类另开轨道）；
  ② 不再以 `REFMEM-PERSIST-01` 作为迁移校准表的理由（该契约登记状态为 pending）。
- `node` 跨核一致性：本轮不改机制；`osal_critical` 是否适合实时路径另查，**不作为本轮阻断项**。
- 上一版"T0-B 只换不扩（96/100 ms）"硬规则**撤回**，依据错误（见 §4.1）。

## 7. Alternatives considered

- **方案A：保持 64 KiB 地址空间、仅稀疏化物理 backing。** 本轮不采纳：该方案可以释放
  RAM，但需增加地址转换和访问管理；直接静态缩容的实现与验证范围更小。
- **方案B：裁到 32 KiB。** 拒绝：无量化依据——实编显示 VDC/DPLL payload 仅 232 / 224 B。
- **方案C（本提案）：18 KiB / 16 区域 / 1 KiB 区块 / 保留区域 ID。** 接受。
- **方案D：更紧凑的非 1 KiB 区块布局（约 12 KiB）。** 本轮不采纳，理由修正为：1 KiB 区块使
  offset 与区域内余量的关系简单、便于分块校验；**若后续确认需要更紧凑，可另开轨道重评**。

### 对上一版 MPU 论述的更正

上一版称"RP2350 Cortex-M33 MPU 要求区域为 2 的幂且自然对齐"，**该前提错误**：Armv8-M MPU
使用基址/界限（base/limit）寄存器、粒度为 32 B，6 KiB 可单独覆盖。此外，仅增加 section 与
对齐**不会**自动启用 MPU（需显式配置）。因此 **MPU 不能作为排除紧凑布局的依据**；上一版
所称的"2 KiB 对齐余量"在布局中实际被 `ROLE` 等区域占用，并不存在。

## 8. 核验结论

- 结论: `ACCEPTED_FOR_TODO`（方向接受，实施与验收未完成；本提交单不改变契约状态）
- 核验：主控复核及独立 agent `priority_review`；待办承接容量、兼容和硬件验证。

## 9. 交叉审核记录（C11，必填）

- 审核方: 独立 agent `priority_review`（未编写本提案及本轮实施代码）
- 审核方式: 代码/ARM sizeof/最新提案只读交叉复核
- 审核结论: 接受最小执行方向；整表清零保持、TLV 权限后移、稀疏 backing 论据修正、区域占用分列等意见已收敛。实施前仍须验证实际字段边界及布局版本拒绝，不授予 RAM 收益或 P3 通过状态。
- 审核日期: 2026-09-16
- 后续登记审核: 独立 agent `priority_review` 接受 canonical 中 `REFMEM-LAYOUT-01` 的规范与 pending 登记，顶层同日可见。原件为 `out/HardwareAcceptance/20260916/refmem-static-shrink-r1/independent-review/joint-delivery-review.json`，不将本评审提案本身登记为契约；当前实现与验收结论仍由 `REFMEM-TASK-20260916-001` 承接。

## 10. 未决项

| # | 未决项 | 责任域 | 是否阻断本轮 |
|---|---|---|---|
| 1 | TDMA 运行事实（ring role / generation / overrun / deadline）是否要求跨节点可见 | TDMA | 否（另开轨道） |
| 2 | 核对 `TRIGGER` / `IO` 当前实际字段访问上界；二者目前是 byte 数组，不假定已实现每节点槽结构。确需扩容则分别计算：仅一区增加 1 KiB 为 19 KiB，两区均增加才为 20 KiB | TDMA / SYNC_IO | 仅当前已用字段越界才阻断，不为未实现的未来摘要新增前置 |
| 3 | `STATS` / `FAULT` / `GATEWAY` 的现有引用清单（本轮保留 ID，仅登记不改） | RefMem / Measure / Gateway | 否 |
| 4 | `NODE` 槽步长 512 → 128 B 后，旧部署包拒绝路径的验证方式 | RefMem | **是** |
| 5 | `osal_critical` 是否适合 `node` 的跨核实时路径 | RTOS / RefMem | 否（另查） |

## 11. 配套改动清单

| # | 项 | 说明 |
|---|---|---|
| 1 | 保留 16 个区域 ID，只改 offset / size | 引用不迁移 |
| 2 | `NODE` 槽步长 512 → 128 B（与 `layout_version` 一并） | ABI 变更，不可只改区域大小 |
| 3 | 更新 `_Static_assert(sizeof(refmem_vector_table_t) == DISTRIBUTED_REFMEM_TABLE_SIZE)` 与两处单测断言 | `test_node_capacity.c` 标注为 "RefMem ABI" |
| 4 | 保持 `refmem_vector_table_clear()` 的整结构 `memset(sizeof(*table))` | 随结构自动缩小，不为后续租约提前改写 |
| 5 | SCPI 字段映射复核（区域 ID 不变，但 offset 变） | `SYSTem:REFMEM:STATus?` / `TABle?` / `PROTection:STATus?` |
| 6 | 契约登记 + C11 + 旧部署包拒绝路径验证 | `layout_version` 1 → 2 |

## 12. 后续建议与本轮边界

1. **后续 TLV 触顶策略候选。** 扩展窗满额时不得静默扩容（扩容 = `layout_version` 再涨价），
   须显式定义"提版本"或"每 epoch 重新协商"之一。
2. **后续备用启用规则候选，本轮不实现。** 动用备用区拟要求：① 以 TLV 编码承载并带独立版本号；
   ② 在 RefMem 域文档登记用途与 writer，登记后方可写入（未登记时 fail-closed）；
   ③ 不得改变任何区域 offset 或表尺寸。三条缺一即视为破坏性变更，须回到本提交单流程重新核验。
3. **快速路径独立性。** DPLL/VDC 始终常驻；特等席快速路径不得引入 lease、TLV 解析或
   RefMem 分片依赖。

## 13. 落地顺序

1. 布局兼容测试（含旧部署包拒绝路径）
2. Release RAM 对比（以链接 map 为准）
3. 四板 P3
4. **完成后立即回到 DPLL 主线**

MPU 启用、动态租约、区域重分类：分开推进，本轮不动。
