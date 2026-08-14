# RefMem Sync 总线无关同步架构

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-08-14

本文档定义 Distributed Vector Blackboard / RefMem Sync Domain 的总线无关同步架构。它属于 RefMem 架构层，规定共同事实如何跨节点发布、接收、校验、提交、确认和诊断；它不记录当前最小系统板的 COM 口、GPIO 线序、脚本命令或一次性调试现象。

首版两板验证建议使用最简单的 PIO SPI 风格 transport adapter。后续迁移到 BISS-C 或其他通讯总线时，只替换 adapter，不修改 RefMem Sync 语义。具体最小系统板接线和操作放在 `REFMEM_MIN_SYSTEM_PLAYBOOK.md`。

## 设计目标

RefMem Sync 要解决的问题是：

```text
多个节点如何对同一组 RefMem 事实形成一致、可追溯、可门禁的共同认知。
```

它必须支持：

- A0-A7 通用逻辑 slot 的共同事实同步。
- `REFMEM_HELLO` 节点发现和能力摘要交换。
- `REFMEM_EPOCH` 纪元、run、table seq 和 CRC bundle 对齐。
- `REFMEM_DELTA` 小型事实增量同步。
- `REFMEM_COMMAND` / `REFMEM_ACK_NACK` 命令闭环。
- `REFMEM_FENCE` RUN gate、SYNC epoch 或配置激活前的可见性门禁。
- `REFMEM_QUALITY` stale、CRC、drop、late、timeout 和 evidence 质量闭环。
- 在不同 transport adapter 之间迁移时保持协议层不变。

它不支持：

- 任意远端地址写入。
- 远端直接调用 AO/FB。
- 把 OTA payload、日志全文、波形、SD 文件内容放进 RefMem 帧。
- 让通讯 adapter 直接修改 active fact。
- 将 BISS-C、UART、RS485 等物理总线字段泄漏进 RefMem 语义层。

## HAOFV 层级定位

RefMem Sync 不是独立通讯栈，也不是运行时总线框架。它是 HAOFV 中 `DistributedRefMemAO` 的同步子层，服务于“共同事实”而不是直接服务于业务动作。

正确层级如下：

```text
SCPI / UI / System Pack
  -> Domain AO / FB owner
       publishes intent or local fact
  -> Domain Vector / CommandSlot
       local fact, staging, command intention
  -> DistributedRefMemAO
       owner validation, SlotContract, RMA window, quality/evidence
  -> RefMemSyncProtocol
       HELLO / EPOCH / DELTA / COMMAND / ACK_NACK / FENCE / QUALITY
  -> TransportAdapter
       bus-independent frame carrier, caps, counters, optional timestamp
  -> Hardware Service / Realtime Path
       PIO SPI / BISS-C / RJ45 / UART / RS485 / future bus
```

VDC Domain 与 RefMem Domain 是 HAOFV 下并列的内部基础主域，不是 RefMem Sync 的下级模块。二者关系是侧向协作：

```text
TransportAdapter optional timestamp
  -> VDC Domain input sample
  -> VDC snapshot / quality
  -> RefMem VdcSlot / StatisticsSlot
```

因此，RefMem Sync 可以携带 `compact_time`，也可以把 adapter timestamp 作为 VDC 输入样本，但不能计算 offset/rate，不能执行 DPLL，也不能决定共同时间是否 LOCKED。

## 分层模型

| 层级 | HAOFV 角色 | 负责内容 | 禁止事项 |
|---|---|---|---|
| Domain AO / FB owner | 行为 owner | 产生业务意图、本地事实和事件结果。 | 直接拼远端地址或直接调用远端 FB。 |
| Domain Vector / CommandSlot | 本域事实入口 | 承接本地状态、staging、command intention。 | 替代 RefMemAO 做跨节点提交。 |
| DistributedRefMemAO | 共同事实 owner | 校验 owner、SlotContract、epoch、CRC、stale、quality 和 evidence。 | 执行业务动作或硬实时边沿。 |
| RefMemSyncProtocol | 同步语义层 | 定义 frame type、seq、epoch、completion、fence、ACK/NACK 和 quality。 | 绑定具体物理总线。 |
| TransportAdapter | 可替换承载层 | 帧收发、MTU、链路计数、可选 timestamp。 | 修改 active fact、决定 RUN gate 或解析业务。 |
| Hardware Service / Realtime Path | 物理执行层 | PIO/BISS/UART/RS485 等具体收发和实时采样。 | 反向污染 RefMem 协议语义。 |

### RefMemSyncProtocol

协议层负责：

- 定义帧头、帧类型、payload 格式和 CRC。
- 定义 seq、epoch、run、target mask 和 compact timestamp。
- 定义接收侧 validate / commit / visible 的状态机。
- 定义 delta completion、fence 和 quality 语义。
- 定义哪些字段可以作为 RMA-style atomic 小字段更新。

协议层不负责：

- 引脚、PIO 程序、SPI 时钟、UART 波特率、RS485 方向控制。
- 物理层重传细节。
- BISS-C 编码/解码细节。
- VDC offset/rate 计算。

### TransportAdapter

adapter 负责：

- 把协议帧定界、编码和发送。
- 从物理总线收取完整协议帧。
- 提供 MTU、能力、链路计数和可选 RX timestamp。
- 把 CRC 错误、超时、溢出、方向冲突等链路错误上报 quality。

adapter 不得：

- 解析产品业务语义。
- 直接写 64 KB active fact。
- 生成 A0-A7 slot 装载关系。
- 决定 RUN gate 是否通过。

## TransportAdapter 架构模型

TransportAdapter 是 RefMem Sync 的可替换承载边界。它不是 RefMem 的 owner，也不是业务域的一部分；它只把完整协议帧送到对端，并把链路质量、错误计数和可选 timestamp 交回 RefMem/VDC。

首版两板验证可以采用 PIO SPI 风格 adapter，目的是快速验证 RefMem Sync Protocol，而不是完成最终通讯协议。PIO SPI 在这里是参考 adapter 类，不是架构绑定。当前最小系统板具体接线、COM 口和脚本放在 `REFMEM_MIN_SYSTEM_PLAYBOOK.md`。

adapter 能力模型：

| 项目 | 架构含义 |
|---|---|
| 总线形态 | 可以是点对点、环路、半双工、全双工或 stream + framer。 |
| 方向能力 | 描述是否需要方向控制、是否支持同时收发、是否支持广播。 |
| MTU | 决定 delta compact 策略和大字段拒绝策略。 |
| 帧定界 | adapter 内部实现，RefMem Sync 只消费完整协议帧。 |
| 时间戳能力 | 可选提供 RX/TX timestamp，作为 VDC 输入样本或 quality 证据。 |
| 错误上报 | CRC、length、timeout、overrun、drop、direction conflict 统一转 quality。 |
| 迁移边界 | 后续 BISS-C/RJ45/UART/RS485 adapter 复用同一协议语义。 |

adapter 替换关系：

| Adapter 类 | 适用阶段 | HAOFV 约束 |
|---|---|---|
| PIO SPI | 最小系统两板 bring-up 和协议闭环验证。 | 只证明 RefMem Sync 语义，不冻结产品通讯。 |
| BISS-C | 后续复杂实时 codec 和同步链路。 | 作为类 IP 核能力和 adapter，不改变共同事实语义。 |
| RJ45_SYNC_RING | 后续多板环路或差分同步承载。 | 不暴露远端裸内存，只承载协议帧和 timestamp。 |
| UART / RS485 | 维护、低速节点或扩展节点。 | 可降级参与 sync，但必须服从同一 fence/quality 规则。 |

## 维护面 SCPI Bridge

`SYSTem:REFMEM:SYNC:*` 是 RefMem Sync 的调试/维护 bridge，只用于构造协议帧、把外部工具搬运来的 frame 注入 adapter RX staging、查询 peer/quality/counter snapshot。它仍归属系统维护命名空间，不建立裸顶级 `REFMEM` SCPI 域。

该 bridge 的边界：

- `HELLo?` / `EPOCh?` 只生成总线无关 RefMem Sync frame，返回 header 摘要和 hex payload。
- `DELTa?` 只生成最小 u32 test field 的 `REFMEM_DELTA` frame，用于验证 mirror commit 和 visible 语义。
- `ACK?` 只基于本板最近一次 RX snapshot 生成 `REFMEM_ACK_NACK` frame，用于验证 ACK/NACK 位图、reason 和 evidence 语义；它不是业务命令完成 API。
- `FENCe?` 只生成 `REFMEM_FENCE` frame，用于验证 required slot、source mirror visible 和 min seq 的可见性收束语义；它不直接触发产品 RUN gate。
- `RX` 只执行 `hex -> adapter RX staging -> adapter poll -> refmem_sync_receive_frame()`。
- `MIRRor?` / `ACK:STATus?` / `FENCe:STATus?` / `PEER?` / `QUALity?` / `ADAPter?` 只读取本地 sync context 和 adapter snapshot。
- 维护 bridge 不直接修改 active ApplicationModel、SlotClaimMap、DataLink 或 64 KB RefMem active fact。
- 维护 bridge 不替代真实 transport adapter；真实 PIO SPI、BISS-C、RJ45、UART 或 RS485 接入后必须复用相同 frame validate / receive / quality 语义。

两板 bring-up 可以先由 PC 工具在两块板之间搬运 hex frame，以便隔离验证协议状态机：

```text
Board A HELLo? -> PC tool -> Board B RX
Board B HELLo? -> PC tool -> Board A RX
Board A EPOCh? -> PC tool -> Board B RX
Board B EPOCh? -> PC tool -> Board A RX
Board A DELTa? -> PC tool -> Board B RX -> Board B MIRRor?
Board B DELTa? -> PC tool -> Board A RX -> Board A MIRRor?
Board B ACK? -> PC tool -> Board A RX -> Board A ACK:STATus?
Board A ACK? -> PC tool -> Board B RX -> Board B ACK:STATus?
Board A FENCe? -> PC tool -> Board B RX -> Board B FENCe:STATus?
Board B FENCe? -> PC tool -> Board A RX -> Board A FENCe:STATus?
```

这个阶段证明的是 HELLO/EPOCH/DELTA/ACK_NACK/FENCE 语义、target mask、epoch/run gate、seq、mirror visible、ACK/NACK reason、FENCE pass/fail snapshot 和 quality 计数。它不证明最终物理链路时序，也不证明 FENCE 已接入产品 RUN gate。

注意：`ACK?` 是维护 bridge 命令，确认对象来自本板最近一次 RX snapshot。脚本验证双向 DELTA ACK 时，应先在两端各自生成对 DELTA 的 ACK frame，再互相注入；否则先收到的 ACK frame 会成为新的 `last_rx`，后续 `ACK?` 会确认 ACK frame 本身。真实 command slot 完成语义后续由 `refmem_command.h/.c` 承接。

首版语义验证路径：

```text
Domain AO/FB fact changed
  -> DistributedRefMemAO validates writer and SlotContract
  -> RefMemSyncProtocol encodes REFMEM_DELTA
  -> selected adapter sends frame
  -> peer adapter receives frame
  -> peer RefMemSyncProtocol validates seq/epoch/CRC
  -> mirror commit
  -> snapshot visible
  -> ACK_NACK / QUALITY published
```

## 固定帧头

所有多字节字段使用 little-endian。帧头之后是 payload，payload 的格式由 `frame_type` 决定。

| 字段 | 宽度 | 说明 |
|---|---:|---|
| `magic` | u16 | 固定协议帧识别。 |
| `protocol_version` | u8 | 首版为 1。 |
| `frame_type` | u8 | 帧类型。 |
| `header_size` | u8 | 固定头长度。 |
| `flags` | u8 | ACK request、timestamp valid、fragment 等标志。 |
| `payload_size` | u16 | payload 字节数，不含帧头。 |
| `source_slot` | u8 | 发起方 A0-A7 slot。 |
| `target_mask` | u8 | 目标 A0-A7 位图。 |
| `epoch_id` | u32 | RefMem 事实纪元。 |
| `run_id` | u32 | 当前运行批次。 |
| `seq32` | u32 | source 协议流单调序号。 |
| `ack_seq32` | u32 | 对端最近确认序号，可为 0。 |
| `compact_time` | u32 | compact timestamp 或 local tick。 |
| `header_crc16` | u16 | 帧头 CRC，不包含自身。 |
| `payload_crc32` | u32 | payload CRC。 |

建议固定头大小为 36 字节。若后续需要更大头部，必须提升 `protocol_version` 或通过 `header_size` 扩展。

## 帧类型

| 类型 | 方向 | 用途 |
|---|---|---|
| `REFMEM_HELLO` | 双向 | 节点发现、能力摘要、adapter 能力和 CRC 摘要交换。 |
| `REFMEM_EPOCH` | owner 到 peers | 发布 epoch、run、table seq 和 active CRC bundle。 |
| `REFMEM_DELTA` | writer 到 readers | 发布一个或多个小型事实变化。 |
| `REFMEM_COMMAND` | command owner 到 targets | 发布命令槽摘要或 command flag。 |
| `REFMEM_ACK_NACK` | target 到 command owner | 回写 ACK/NACK/busy/timeout/reason/evidence。 |
| `REFMEM_FENCE` | owner 到 required nodes | 要求一批 delta 在门禁前完成可见性切换。 |
| `REFMEM_QUALITY` | 双向 | 发布链路质量、错误计数和最新 evidence。 |

## Payload 契约

### REFMEM_HELLO

| 字段 | 说明 |
|---|---|
| `build_id_crc32` | 固件 build id 摘要。 |
| `layout_version` | 64 KB RefMem layout 版本。 |
| `application_crc32` | active ApplicationMap CRC。 |
| `config_crc32` | active config CRC。 |
| `capability_mask` | 当前节点基础能力。 |
| `io_constraint_mask` | 当前节点 IO 能力摘要。 |
| `ip_core_mask` | 当前节点类 IP 核能力摘要。 |
| `adapter_id` | 当前 adapter 类型，例如 PIO_SPI。 |
| `adapter_caps` | half/full duplex、timestamp、crc offload 等能力。 |
| `max_payload_size` | 可接收 payload 最大长度。 |

### REFMEM_EPOCH

| 字段 | 说明 |
|---|---|
| `table_seq` | 当前 RefMem 全表事实序号。 |
| `layout_crc32` | Header/Directory CRC。 |
| `application_crc32` | ApplicationMap CRC。 |
| `config_crc32` | 配置 CRC。 |
| `calibration_crc32` | active calibration CRC。 |
| `sync_profile_crc32` | active sync profile CRC。 |
| `quality_epoch` | quality 统计窗口纪元。 |

### REFMEM_DELTA

`REFMEM_DELTA` 只允许同步小型共同事实，不允许携带任意地址写入。

| 字段 | 说明 |
|---|---|
| `delta_id` | delta 类型或字段组编号。 |
| `slot_id` | 64 KB 大 slot id。 |
| `slot_seq` | 被发布 slot 的版本。 |
| `field_id` | 字段级 contract id。 |
| `field_offset` | slot 内偏移；必须匹配 contract。 |
| `field_width` | 字段宽度。 |
| `dirty_mask` | 字段组 dirty 位。 |
| `payload_kind` | inline、bitset、counter、guard、slot fragment。 |
| `payload[]` | compact payload。 |

接收侧必须用 `RefMemSlotContract` 校验 writer、reader、宽度、值域、生命周期、snapshot policy 和 sync policy。

### REFMEM_COMMAND

`REFMEM_COMMAND` 是 `AckCommandSlot` 的跨节点同步视图，不替代本地命令槽。

| 字段 | 说明 |
|---|---|
| `command_seq` | 命令序号。 |
| `command_type` | 命令类型枚举。 |
| `command_class` | 权限/资源类别。 |
| `source_instance` | 发起实例。 |
| `target_mask` | 目标 slot 位图。 |
| `required_mask` | 必须 ACK 的目标位图。 |
| `payload_kind` | inline/ref/staging/tlv。 |
| `payload_ref` | payload 引用。 |
| `payload_size` | payload 长度。 |
| `payload_crc32` | payload 摘要。 |
| `timeout_us` | 超时。 |

### REFMEM_ACK_NACK

| 字段 | 说明 |
|---|---|
| `command_seq` | 对应命令序号。 |
| `delta_seq32` | 可选，对应 delta 序号。 |
| `taken_flags` | 已 take 位图。 |
| `ack_flags` | ACK 位图。 |
| `nack_flags` | NACK 位图。 |
| `busy_flags` | busy 位图。 |
| `timeout_flags` | timeout 位图。 |
| `last_reason` | 最近拒绝原因。 |
| `last_reason_slot` | 最近拒绝节点。 |
| `evidence_index` | evidence 索引。 |

### REFMEM_FENCE

| 字段 | 说明 |
|---|---|
| `fence_seq` | fence 序号。 |
| `fence_scope` | RUN gate、SYNC epoch、config activate、manual flush。 |
| `required_mask` | 必须完成可见性切换的节点。 |
| `min_table_seq` | 需要达到的最小 table seq。 |
| `crc_bundle` | 需要一致的 CRC 摘要。 |
| `deadline_us` | 超时。 |

### REFMEM_QUALITY

| 字段 | 说明 |
|---|---|
| `quality_id` | 质量记录编号。 |
| `scope` | node、adapter、slot、event link、data link。 |
| `seq_expected` | 期望序号。 |
| `seq_last` | 最近接收序号。 |
| `crc_error_count` | CRC 错误计数。 |
| `stale_count` | stale 次数。 |
| `drop_count` | 丢帧或丢弃计数。 |
| `late_count` | late 计数。 |
| `timeout_count` | timeout 计数。 |
| `last_error` | 最近错误。 |
| `p99_us` / `p999_us` | 可选延迟分布。 |
| `evidence_index` | evidence 索引。 |

## 接收提交状态机

```text
FRAME_RX
  -> HEADER_OK
  -> PAYLOAD_CRC_OK
  -> EPOCH_OK
  -> TARGET_MATCH
  -> CONTRACT_OK
  -> MIRROR_COMMITTED
  -> SNAPSHOT_VISIBLE
  -> ACK_OR_QUALITY_PUBLISHED
```

失败处理：

| 失败点 | 处理 |
|---|---|
| header CRC 错误 | 丢弃帧，递增 adapter CRC counter。 |
| payload CRC 错误 | 丢弃帧，发布 quality CRC error。 |
| epoch/run 不匹配 | 不提交，记录 epoch mismatch。 |
| target 不匹配 | 不提交 active fact，可计入观察统计。 |
| seq 重复 | 同 payload 可重复 ACK，不重复执行动作。 |
| seq 倒退 | stale/drop counter 增加。 |
| seq 跳变 | 允许独立事实提交，但 fence 必须等待或超时。 |
| contract 失败 | NACK 或 quality error，指向 evidence。 |

## Completion 与 Fence

delta completion 分为：

```text
origin_encoded
adapter_queued
transport_sent
target_received
target_crc_ok
target_owner_validated
target_committed
visible_in_snapshot
```

RUN gate、SYNC epoch、配置激活和需要一致性的命令只能消费 `visible_in_snapshot` 后的事实。任何只到达 `target_received` 或 `target_committed` 之前的 delta 都不能改变对外 READ 结果和 RUN 判据。

`REFMEM_FENCE` 用来把一批 delta 或命令收束成一致性点：

- required node 全部 visible 后 fence passed。
- required node 出现 NACK、timeout 或 stale 后 fence failed。
- 非 required node 可进入 degraded/report-only，不阻塞 RUN，前提是 DeploymentGate 允许。

## RMA Window 白名单

RefMem 不暴露裸地址，只暴露受控 slot mirror 和小字段 atomic 更新。

允许的 RMA-style 更新：

- `slot_seq`、`table_seq` 摘要。
- node heartbeat、freshness 和 dirty bitmap。
- command taken/ack/nack/busy/timeout bit。
- quality counter。
- 小型 enum/state/counter 字段。

禁止的 RMA-style 更新：

- 任意地址写入。
- 大 payload。
- active configuration 表直接覆盖。
- calibration delay table 大块写入。
- OTA、Storage、Report 数据。
- 绕过 owner 的业务状态写入。

## 与 VDC 的关系

RefMem Sync Protocol 使用 `compact_time` 作为帧时间标记，但不解释共同时间算法。compact timestamp 的展开、offset/rate、DPLL、HOLDOVER 和锁定质量由 VDC Domain 管理。

关系如下：

- adapter 可提供 RX hardware timestamp。
- VDC Domain 可消费该 timestamp 作为 DPLL 或质量输入。
- RefMem 只保存 VDC snapshot、quality 和 evidence。
- RefMem fence 和 stale 计算可以读取 VDC snapshot，但不写 VDC owner 字段。

## 首版实现顺序

1. 冻结 `refmem_sync_frame.h/.c` 的帧头、CRC、encode/decode 和 payload 长度检查。
2. 建立 host 单元测试，覆盖 hello、epoch、delta、command、ack、quality 的 encode/decode。
3. 建立 PIO SPI adapter skeleton，只暴露 send/poll/caps/counters。
4. 两板 HIL：A 发 HELLO/EPOCH/DELTA，B 提交 mirror 并回 ACK/QUALITY。
5. 接入 command slot、RefMemSlotContract 和 fence。
6. 后续迁移 BISS-C/RJ45/UART/RS485 adapter，协议层保持不变。
