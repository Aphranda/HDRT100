# RefMem 主域风险评审报告

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_RISK_REVIEW.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`
Last updated: 2026-08-15

本文档是 `components/distributed_refmem` 代码与 `docs/refmem` 文档的一次风险评审记录，用于收敛当前实现中的正确性缺陷、定义但未接线的死代码、以及文档与代码的漂移。所有文件:行号以评审当日（2026-08-15）的工作树为准。

---

## 1. 评审范围与方法

- 范围：`components/distributed_refmem`（16 .c + 16 .h，约 11,550 行）+ `docs/refmem`（6 篇，约 5,510 行）。
- 方法：四组并行代码审查（core / model+registry / claim+command+contract / sync+adapter+quality），叠加文档与代码交叉核对、以及全量 host 单测复跑。
- 基线：固定四板 A0–A3 环，HAOFV 架构，RefMem 作为内部基础主域（共同事实 owner，不执行业务动作、不驱动硬实时边沿）。

**一句话结论**：核心生效路径（vector 表、SlotClaimMap 派生 + RUN gate、command slot、table registry staging/activation、TDMA 物理 transport）是扎实且有测试的。8 槽 + 预留扩展 + 动态装载 / claim 协调这套「基础件」是**有意的平台投资**（先搭基础件、预留扩展头寸，避免后续返工技术债），不属于「过度工程」，不纳入缺陷。真正的 P0 风险集中在跨核并发、线格式、门禁与 owner 校验三处，其中已纠偏 4 项（§3.1/§3.2/§3.3/§3.10，复核属实）。当前唯一需收敛的是「半接线」路径——以「成功」姿态暴露却未真正生效的接口，应闭环或显式标 DRAFT。

---

## 2. 风险总览

| 级别 | 数量 | 主题 |
|---|---|---|
| P0 高 | 11 | 跨核并发、线格式、门禁失效、假传输、缓冲区/状态破坏 |
| P1 中 | 10 | 单位混用、回绕、端序、PIO 运行时补丁、死分支 |
| P2 低 | 6 | 命名误导、冗余校验、严格顺序 |
| 平台预留基础件 | 8 组 | claim 协调协议、4/9 表、并行假 adapter 等（有意预留，见 §6） |
| 文档漂移 | 5 | 架构文档过期、头泄漏、装饰性 guard 等 |

---

## 3. P0 高风险项

> 复核记录（2026-08-15）：§3.1 / §3.2 / §3.3 / §3.10 四处「已纠偏」已逐条核对当前工作树代码，属实（见各节纠偏结论）。

### 3.1 跨核 seqlock 写入非原子、无 release fence（已纠偏）

写侧用普通 `guard++`，读侧用 `__atomic_load_n(..., __ATOMIC_ACQUIRE)`。RP2350 双核下读核可能先看到偶数 guard（误判「写完成」）而数据尚未可见。同一模式出现在三处：

- [refmem_realtime_tdma.c:13-31](components/distributed_refmem/src/refmem_realtime_tdma.c#L13-L31) — intent/result 双 guard。
- [refmem_command.c:12-17](components/distributed_refmem/src/refmem_command.c#L12-L17) — command slot guard。

纠偏：`refmem_realtime_tdma` 的 intent/result guard 和 `refmem_command` 的 command guard 写侧已统一通过本地 helper 调用 `__atomic_add_fetch(..., __ATOMIC_RELEASE)`；读侧继续使用 `__atomic_load_n(..., __ATOMIC_ACQUIRE)`，形成跨核 seqlock guard 的 acquire/release 配对。

### 3.2 板能力数组线格式 16 条 vs 语义 8（已纠偏）

[refmem_application_model.h:17-18](components/distributed_refmem/inc/refmem_application_model.h#L17-L18) 定义 `BOARD_CAPABILITY_COUNT = CLAIM_CANDIDATE_MAX = 16`，而 [refmem_application_model.c:544-545](components/distributed_refmem/src/refmem_application_model.c#L544-L545) 又要求 `board_count == 8`。后果：

- 结构体固定 584 字节（8 真实 + 8 全零备用，全部参与 CRC）。
- 一个正确的 8 板包（296 字节）被当作 size 不匹配拒绝。
- 16 板包结构上被接受，但语义与 `board_count == 8` 矛盾。

纠偏结论：当前产品 `BoardCapabilityTable` 固定 8 条 wire payload，与 A0-A7 active slot 容量一致；`CLAIM_CANDIDATE_MAX = 16` 只属于 `SlotClaimProposal` / overflow evidence，不复用为 BoardCapability 表容量。未来如果需要扩展 BoardCapability，必须通过新的 table schema/layout version，而不是改变当前 v1 表的隐含大小。

### 3.3 部署门禁结果被丢弃——门禁从未真正 gate（已纠偏）

[refmem_application_model.c:1178-1189](components/distributed_refmem/src/refmem_application_model.c#L1178-L1189)：

```c
refmem_slot_claim_gate_status_t claim_gate;
if (!refmem_slot_claim_gate_evaluate(&claim_map, &claim_gate)) return false;
return true;
```

纠偏：当前 `refmem_slot_claim_gate_evaluate()` 返回值已经表示 `ready`，但调用端仍改为显式检查 `evaluate && claim_gate.ready`。`refmem_application_model` 的静态表 validation、`DistributedRefMemAO` activation gate 和 `SystemManager` RUN gate 均不再依赖 evaluator 返回值的隐含语义。

### 3.4 假 TX：SCPI 同步链路「成功」却不发一个 bit

[refmem_pio_spi_adapter.c:97-137](components/distributed_refmem/src/refmem_pio_spi_adapter.c#L97-L137) 的 `send()` 只校验帧、`tx_count++`、置 `IDLE`、返回 `true`，不触碰 PIO/SPI/GPIO/DMA。`SYSTem:REFMEM:SYNC:TX` 每次「成功」都没有传输任何数据。唯一的真实行为是坏帧拒绝。

### 3.5 Fence 超时逻辑反转

[refmem_sync.c:140-142](components/distributed_refmem/src/refmem_sync.c#L140-L142)：

```c
if (fence->passed == 0u && fence_payload.deadline_us == 0u)
    fence->timed_out = 1u;
```

等于「没给 deadline 就判超时」，且根本没有做时间比较。`deadline_us` 字段其余地方从不读取。

### 3.6 `receive_raw` 无目标容量 → 潜在缓冲区溢出

[refmem_spi_physical_adapter.c:652-688](components/distributed_refmem/src/refmem_spi_physical_adapter.c#L652-L688)：签名 `(adapter, buffer, expected_size, received_size, timeout_ms)` 没有 `buffer_capacity`。`expected_size` 只挡了内部 `WORD_MAX`，复制到调用方 buffer 处完全信任调用方，是 API 边界的潜在越界。

### 3.7 `arm()` 可在采集进行中抹掉状态

[refmem_spi_physical_adapter.c:381-411](components/distributed_refmem/src/refmem_spi_physical_adapter.c#L381-L411)：只保存 5 个计数器就 `memset(adapter, 0, ...)`，不检查 `rx_capture_active`。采集进行中 re-arm 会重置 `rx_capture_*`，使 DMA/状态不一致。

### 3.8 activation 的 staging descriptor 是 TOCTOU

[distributed_refmem.c:878-882](components/distributed_refmem/src/distributed_refmem.c#L878-L882) 读 staging descriptor、`:931-959` 再执行 `activate_staging`，中间无锁。并发 `LOAD:SD` / `LOAD:NODE` 可在两次访问之间重 stage，导致 gate 校验的对象与实际激活的对象不一致。

### 3.9 读接口有写副作用

[distributed_refmem.c:1164](components/distributed_refmem/src/distributed_refmem.c#L1164) 的 `distributed_refmem_get_runtime_protection()` 内部调用 `publish_runtime_locked()`（`:341-394`），重读 flash lockout 状态、重算 directory flag、改写 `header->header_crc32`。文档标注的「snapshot」API 实际每次读都做一次 1 KB FNV 重算并写共享状态，其消费者（`flash_activation_safe`、SCPI）在读路径上触发 header 变更。

### 3.10 `OWNER_OK` 未做 owner validation 就置位（已纠偏）

[refmem_table_registry.c:949-956](components/distributed_refmem/src/refmem_table_registry.c#L949-L956)：

```c
if ((model->table_mask & table_bit) != 0u && entry->active_crc32 != 0u) {
    entry->flags |= REFMEM_TABLE_FLAG_ACTIVE_PRESENT |
                    REFMEM_TABLE_FLAG_CRC_OK |
                    REFMEM_TABLE_FLAG_OWNER_OK;
}
```

纠偏：`refresh_active()` 的编译内置 active entry 不再把 `present + CRC` 升级为 `OWNER_OK`，只置 `ACTIVE_PRESENT|CRC_OK`。`OWNER_OK` 继续只由 staging package validation 的 `owner_validated_table_mask` 或 activation 后的已验证 image provenance 产生；新增 table registry 单测覆盖该语义。

### 3.11 SCPI `LOAD:NODE` / `LOAD:BOARD` staging 是死胡同（已纠偏）

[refmem_application_model.c:1974](components/distributed_refmem/src/refmem_application_model.c#L1974) 附近：`stage_scpi_node_config` 只写 metadata-only registry 条目，`s_staging_image_size` 始终为 0；activation 再 parse 时 `access_table` 必然失败。结果：这些接口报成功、更新 registry flag，但改动永远进不了 runtime 模型。`stage_scpi_board_capability` 同理。

纠偏：`LOAD:NODE` 已形成私有 `DistributedNodeLoadTable` draft；`LOAD:BOARD` 已形成私有系统级 `BoardCapabilityTable` draft。任一 inline draft 通过单表 contract 后，ApplicationModel owner 会将当前 active 9 表叠加所有 draft，生成完整 9 表 inline RMTP package image，并调用 `refmem_table_registry_stage_package_image()`。后续 `LOAD:ACTivate` 继续走 staging stable view pre-parse、activation gate、active/rollbackable 切换和 runtime getter commit；产品路径不再存在可激活的单表 staging。

---

## 4. P1 中风险项

| 项 | 位置 | 说明 |
|---|---|---|
| 整数回绕 | [distributed_refmem.c:222](components/distributed_refmem/src/distributed_refmem.c#L222) | `deadline_us + 999u` 在 `deadline_us > UINT32_MAX-999` 时回绕，产生近似 0 的超时。 |
| 单位混用 | [refmem_command.c:275-277](components/distributed_refmem/src/refmem_command.c#L275-L277) | `mark_timeout` 用 tick 差值比较 `timeout_us`，仅当 1 tick == 1 µs 才成立，未在代码中确立。 |
| 不可清除 | [refmem_command.c:71-74](components/distributed_refmem/src/refmem_command.c#L71-L74) | `required_mask==0` 不被 post 拒绝，但 `is_complete` 永远不判完成 → `clear` 必然失败。 |
| 覆盖 NACK reason | [refmem_command.c:238](components/distributed_refmem/src/refmem_command.c#L238) | `ack()` 把 `last_reason` 置 `NONE`，覆盖其他 target 早先记录的 NACK/epoch 原因。 |
| 门禁硬编码 | [distributed_refmem.c:944](components/distributed_refmem/src/distributed_refmem.c#L944) | `command_ack_ok = 1u`，activation gate 的 command-ACK 条件恒真。 |
| re-init 不清 | [distributed_refmem.c:31-32](components/distributed_refmem/src/distributed_refmem.c#L31-L32) | `s_node_load_owners[16]` 仅 BSS 清零一次，二次 `init()` 留下 stale owner/context 回调。 |
| 计数器回绕 | [refmem_sync.c:352,366](components/distributed_refmem/src/refmem_sync.c#L352) | `expected_seq32 = seq32 + 1u` 在 0xFFFFFFFF 回绕到 0，drop/重复计数可能误判；计数器均为无回绕处理的 uint32。 |
| RX PIO 运行时补丁 | [refmem_spi_physical.pio:67-69](components/distributed_refmem/src/refmem_spi_physical.pio#L67-L69) | 因 `.pio` 源硬编码 gpio 0/1，运行期改写 `instr_mem`；SDK 指令宽度变化会静默损坏。 |
| 端序不一致 | [refmem_sync_frame.c:7-32](components/distributed_refmem/src/refmem_sync_frame.c#L7-L32) | header 显式小端，payload 结构体原始 memcpy；仅在 RP2350 小端成立，非自描述线格式。 |
| COMMAND 帧 epoch 策略 | [refmem_sync.c:13-16](components/distributed_refmem/src/refmem_sync.c#L13-L16) | 仅 HELLO 豁免 epoch 匹配；若 COMMAND 帧共享 sync RX 路径，会在建 run 前被 epoch 不匹配拒绝。 |

---

## 5. P2 低风险项

| 项 | 位置 | 说明 |
|---|---|---|
| 「CRC32」命名误导 | [refmem_vector_table.c:128-141](components/distributed_refmem/src/refmem_vector_table.c#L128-L141) / [distributed_refmem.c:37-51](components/distributed_refmem/src/distributed_refmem.c#L37-L51) | FNV-1a 被命名为 `crc32`，与真实 CRC-32（sync 帧）、CRC-16 并存，三套完整性算法。 |
| CRC 返回 0 与 NULL 冲突 | [distributed_refmem.c:40-42](components/distributed_refmem/src/distributed_refmem.c#L40-L42) | `crc32(NULL, 0)` 返回 0，与合法 FNV 结果 0 无法区分。 |
| 严格表顺序 | [refmem_table_registry.c:1490](components/distributed_refmem/src/refmem_table_registry.c#L1490) | 要求 `table_id == i` 严格递增，非顺序 directory 的合法包被拒为 `TABLE_DIR`。 |
| seq 冗余 | [refmem_table_registry.c:1036](components/distributed_refmem/src/refmem_table_registry.c#L1036) / `:959` | refresh_staging 不递增 table_seq 而 refresh_active 递增，active/staging 可共享 seq，seq 对完整性无贡献。 |
| fb/data_link CRC 语义不一致 | [refmem_application_model.c:847-894](components/distributed_refmem/src/refmem_application_model.c#L847-L894) | 静态 CRC 哈希可读字符串，线格式用 `name_hash`/`slot_path_hash`，同一逻辑表两处 CRC 不可比。 |
| 重复校验层 | [refmem_application_model.c:1580-1584](components/distributed_refmem/src/refmem_application_model.c#L1580-L1584) | parse 阶段重跑 registry 已跑过的 9 表校验，且两套 validator 结论可能不一致。 |

---

## 6. 平台预留基础件（有意为之，非过度工程）

以下清单是**有意预留的扩展基础件**（8 槽 + 预留扩展 + 动态装载 / claim 协调，先搭基础件避免后续返工技术债），不是待删除的死代码。清单仅用于标记「哪些尚处于预留态」，以便后续扩展时知道头寸已就位。

| 预留基础件 | 规模 | 状态 |
|---|---|---|
| Claim 自组网协议（HELLO/PROPOSE/CONFLICT/RELEASE/RESOLVE/COMMIT 六帧） | [refmem_claim_protocol.c](components/distributed_refmem/src/refmem_claim_protocol.c) ≈380 行 | **0 个运行时调用者**，仅单测引用；TODO 自认「尚未接 RJ45」。 |
| 9 张 canonical 表中的 4 张：EventLink / DataLink / DeploymentGate / ConnectionQuality | 各自 parser + validator + CRC + wire 格式 | **0 个读取者**，`get_event_link_table` / `get_data_link_table` / `get_deployment_gate` / `get_connection_quality` 全无调用方。 |
| 8 状态 claim 机 | [refmem_slot_claim.h:12-21](components/distributed_refmem/inc/refmem_slot_claim.h#L12-L21) | 仅 UNCLAIMED/CLAIMED/CONFLICT/MISMATCH/DISABLED 被真实产出；RESOLVING/STALE/OVERFLOW 从不赋值或仅读。 |
| 8 节点 / 16 候选 / 16 directory slot | 多个头文件 | 固定四板系统，一半 node slot 和大部分 sub-slot buffer 从不写。 |
| 并行假 adapter | [refmem_pio_spi_adapter.c](components/distributed_refmem/src/refmem_pio_spi_adapter.c) | skeleton 与 physical adapter 并存，前者 `send()` 是 stub，只服务 SCPI loopback 诊断。 |
| 16 项 owner registry | [distributed_refmem.c:31-32](components/distributed_refmem/src/distributed_refmem.c#L31-L32) | 仅 `model_turntable` 一个注册者。 |
| 「TDMA」 | [refmem_realtime_tdma.c](components/distributed_refmem/src/refmem_realtime_tdma.c) | 单发 mailbox 而非调度器，`window_epoch/index` 不用于排程；TX 逐字节 blocking，仅 RX 用 DMA。 |
| 死函数 / 死枚举 | 多处 | `command_mark_timeout`、`set_epoch`、blocking `receive`、`application_model_validate` 全 0 调用者；`NODE_STALE/INVALID/FAULT`、`NODE_TYPE_MODEL_VNA/...`、`FAIL_HOLDOVER/DEGRADE`、`SLOT_CLAIM_REASON_HW_PROFILE_MISMATCH` 等只定义从不赋值或读取。 |

**关键区分**：`SlotClaimMap` 的派生 + RUN gate 检查是**活的**（`distributed_refmem.c`、`system_manager.c`、`scpi_system_snapshot_commands.c` 均在用）。`refmem_claim_protocol` 跨板协调协议、8 状态 claim 机、4/9 表、16 项 owner registry 等是**有意预留的扩展基础件**——当前产品主路径用预规划（SlotClaimMap 派生），这些作为未来多应用 / 跨平台扩展的头寸先搭好，避免后续返工技术债。它们不是缺陷，但**未接线的部分不得以「成功」姿态对外暴露**：半接线项（如假 TX §3.4、假 staging §3.11）仍需闭环或显式标 DRAFT。

---

## 7. 文档与代码漂移

1. 架构文档两节过期。[REFMEM_DOMAIN_ARCHITECTURE.md:1367](docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md#L1367) 把 `refmem_command.h/.c` 列为「尚未」，但已存在且测试通过；「目标代码形态」（`:1370-1397`）遗漏当前实际存在的约 9 个文件（`refmem_sync_frame`、`refmem_sync_hello`、`refmem_claim_protocol`、`refmem_realtime_contract`、`refmem_application_contract`、`refmem_realtime_tdma`、`refmem_pio_spi_adapter`、`refmem_spi_physical_adapter`、`refmem_transport_adapter`）。

2. `distributed_refmem.c` 不是文档说的「正在拆空的兼容 wrapper」（[REFMEM_DOMAIN_TODO.md:265](docs/refmem/REFMEM_DOMAIN_TODO.md#L265)）。它实际是 de-facto 的 `DistributedRefMemAO` owner——vector 发布、flash lockout、TDMA 绑定、command slot、node-load、staging/activation 全挤在一个约 1,200 行 TU 里。

3. 公共头泄漏内部类型。[distributed_refmem.h:54-55](components/distributed_refmem/inc/distributed_refmem.h#L54-L55) 直接 include `refmem_command.h` / `refmem_realtime_tdma.h`，公开签名暴露 raw `refmem_command_request_t` / `refmem_realtime_tdma_*`，与「对外只暴露 snapshot/validated publish helper」（[TODO:69](docs/refmem/REFMEM_DOMAIN_TODO.md#L69)）相悖。

4. 「seqlock/双缓冲 guard」是装饰性的。[TODO:154](docs/refmem/REFMEM_DOMAIN_TODO.md#L154) 仍为未完成项，但代码已填充 `slot_guard` 字段，却无任何读侧校验 CRC/staleness。

5. 多节点 command 寻址是虚构的。`stage_sd_system_pack` / `activate_staging` / `stage_board_capability` 均 post `target_mask = (1u << LOCAL_NODE_ID)`，`try_take` 也运行在本地 `s_refmem_command_slot` 上；跨环的 `target_mask`/`required_mask`/多节点 ACK 机制从未被真正使用。

---

## 8. 测试状态

评审当日全量复跑 16 个 `tools/tests/run_*.ps1` host 单测 runner 的相关 suite，**全部通过**。

历史记录中 `refmem_slot_claim` 与 `refmem_application_contract` 两个失败是真 bug，但已在当前树修复（commit `9840730`，HEAD 的祖先）：

- slot_claim 的 payload-CRC 错配，根因是 `refmem_claim_propose_frame_init` 在 `header.payload_count` 尚未写入时就用它计算 CRC（算成了 0 个 proposal 的 CRC）。
- application_contract 的链接错误是 runner 遗漏 `refmem_slot_claim.c`。

`biss_protocol`、`portable_ota` 的首轮失败为 transient，现亦通过。

---

## 9. 处置建议与优先级

| 顺序 | 事项 | 理由 |
|---|---|---|
| 1 | ~~修 P0 跨核并发（§3.1）~~ ✅ 已纠偏（复核通过） | 写侧已统一 `__atomic_add_fetch(..., __ATOMIC_RELEASE)`，读侧 `__ATOMIC_ACQUIRE`，三处配对成立。 |
| 2 | ~~修线格式 16→8（§3.2）~~ ✅ 已纠偏（复核通过） | `BOARD_CAPABILITY_COUNT` 已 = `NODE_COUNT` = 8，与 `CLAIM_CANDIDATE_MAX=16` 解耦。 |
| 3 | ~~让部署门禁真正 gate（§3.3）+ 修 OWNER_OK（§3.10）~~ ✅ 已纠偏（复核通过） | gate 现显式检查 `claim_gate.ready == 0u`；`refresh_active` 不再升级 `OWNER_OK`。 |
| 4 | 保留 4 张 dormant 表 + claim 协调协议为平台基础件（§6） | 有意预留扩展头寸、先搭基础件避免后续返工；**不删除**。 |
| 5 | 闭环或显式标 DRAFT：假 TX / 假 staging（§3.4、§3.11） | 基础件不得以「成功」姿态暴露却不生效；要么闭环、要么标 `DRAFT/UNSUPPORTED`。 |
| 6 | 修 P1 项（§4） | 单位混用、回绕、PIO 运行时补丁等，批量处理。 |
| 7 | 更新架构文档两节（§7.1） | 让「当前实现现状」「目标代码形态」与实际文件集一致。 |
