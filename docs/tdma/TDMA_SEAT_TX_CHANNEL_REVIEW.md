# 一等席独立 TX 通道方案评审（已作废：owner 定案共用快速通道）

Status: Draft
Domain: TDMA
Canonical: `docs/tdma/TDMA_SEAT_TX_CHANNEL_REVIEW.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`
Last updated: 2026-10-11

## 文档接口与范围

**定案（2026-10-11，owner 裁定）**：快速通道**只有一条**，特等席与一等席**分时复用**它（M:N 轮换）——特等席同样没有独立 TX。因此 A（专属通道）**不再是目标**，A″/B 方案作废；共用通道下的**交付关键字节保护**（`distributed_refmem_position_ack_control_available()` 准入 + `lane_diag_refused_count`/`lane_ack_override_count` 计数）是**长期机制**，不是过渡措施。

- 现状（已实现）：一等席冻结完成事实搭在出站镜像的 `CONTROL_OPCODE`/`CONTROL_SEQ8` 两个字节上，与 action probe 诊断**共享**。已实现两层保护：**诊断准入**（有一等事实要发布时，控制字节事先不授予诊断并将其计入 `lane_diag_refused_count`）+ **写入顺序仲裁**（先诊断、后一等确认，未预留时的覆盖计入 `lane_ack_override_count`）。**但通道仍非独立**：两个 owner 仍写同一对字节，只是不再可能静默丢失；`lane_ack_override_count` 在准入生效后应恒为 0，可作为"独立通道是否建立"的判据。
- 目标：一等席的交付关键事实在**物理或载荷层不再与任何其他 owner 共享可写资源**，其丢失/覆盖/超时全部有独立且可读的证据——即特等席现有的"专属 typed body + 独立保全"标准。
- 非目标：本文不建议改动一等席的**触发语义**（冻结/预观/到期判定仍归 T2）、不建议缩短一等席的服务预算（那是 `TDMA-SEAT-03` 的其它条目）。

## 一、约束与既有事实

| 约束 | 事实 |
|---|---|
| 线格式 | T2 紧凑切片 10 B，**恰好**填满一个 RefMem region（`TRIGGER_POSITION_SLICE_WIRE_SIZE` + 静态断言）；receiver 端 `TDMA_FLIGHT_SHORT_SLOT_SIZE` = 32 B，其中 `TDMA_PROCESS_IMAGE_REFMEM_OFFSET` 后是一等/VDC 载荷 |
| 控制字节 | `TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET` / `CONTROL_SEQ8_OFFSET`：T2 frozen 家族 `0xE0 \| run_id`，诊断 probe 用 `0xD1`/`0xD2` |
| 通道 | 从板经**优先级 lane** 接收（IRQ），投递前统一校验 header/identity/transport + mailbox CRC16；一等 sink 接收后再由单深度 mailbox + FREEZE latch 交给 T2 |
| 归属 | 位/字节归属与扩容须经 `EXE-SEAT-01` 一族的布局评审；`TDMA-SEAT-03` 只负责"服务等级"证据 |

## 二、候选方案

### 方案 A：载荷内专属段（**已被附录 A 否证**：短槽无空闲字节）

~~在 32 B mailbox 的未用余量里给一等席划一个专属段~~ —— 附录 A 证明 32 B 全部有主，本方案成立的前提不存在。保留此节仅作决策记录：若仍要走"载荷内专属"，只能退化为 **A″ 回收式专属**（显式把某字段所有权转给一等席并给出旧 owner 替代），见附录 A 结论。

- 原设影响面：`tdma_process_image_layout.h` 增加段偏移与长度常量 + 静态断言；receiver 的 `distributed_refmem_position_ack_receive()` 改读专属段；`action_patch_mailbox()` 不再写 `CONTROL_*`。
- 兼容性：线格式变更 → **同日必须同步升级全部节点**。
- 否证依据：偏移 0–31 逐字节均有归属，见附录 A。

### 方案 B：专属出站镜像帧

一等席使用**独立的一帧**（独立 magic/version/class 或独立 slot），与 process image 完全分离。

- 影响面：wire 上多一帧 → 每圈开销与 origin 发布机会的 M:N 轮换需要重新配平（这是 `TDMA-SEAT-03` 服务预算的输入）；IRQ 处理时间预算重算（优先级 lane 的 WCET）。
- 优点：物理隔离最彻底，诊断、VDC、一等三者互不可写。
- 验收：lane 预算重测（IRQ 最坏耗时、sequence gap）；一轮台架显示各 class 的 accept/reject 计数相互独立。

### 方案 C：保持共享 + 继续加固仲裁（已作为过渡实施，不作为终点）

共享控制字节，但把仲裁做成"诊断准入 + 一等永远优先 + 被拒计入"。

- **已实施（2026-10-11，`TDMA-PROGRESS-20261011-013`）**：镜像构建前查询准入（`distributed_refmem_position_ack_control_available()`）；有一等冻结事实要发布时，诊断只写自己的 RefMem 载荷片段、**不写**控制字节，拒绝次数计入 `lane_diag_refused_count`；未预留镜像仍按"先诊断、后一等确认"写入并计入 `lane_ack_override_count`。
- 优点：零 wire 变更，纯固件逻辑，已可主机验证。
- 缺点：**不满足"独立通道"**，只是把共享风险变成可观测风险；`lane_ack_override_count` 应恒为 0 这一点使它同时成为独立性的判据。**不作为 A' 的关闭条件。**

## 附录 A：32 B 短槽逐字节占用（2026-10-11 由布局常量导出，供评审）

下表由 [tdma_process_image_layout.h](../../components/tdma/inc/tdma_process_image_layout.h) 与 [tdma_flight_engine.h](../../components/tdma/inc/tdma_flight_engine.h) 的符号直接导出（布局版本 `TDMA_PROCESS_IMAGE_LAYOUT_VERSION`），不是实测值；偏移符号是唯一事实源。

| 偏移 | 长度 | 归属（符号） |
|---|---|---|
| 0–1 | 2 | mailbox magic（`TDMA_FLIGHT_MAILBOX_MAGIC`） |
| 2 | 1 | mailbox version（`TDMA_FLIGHT_MAILBOX_VERSION_OFFSET`） |
| 3 | 1 | message class（owner 选择器） |
| 4 | 1 | source slot（`TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET`） |
| 5 | 1 | target mask（`TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET`） |
| 6–7 | 2 | transport sequence |
| 8–13 | 6 | VDC typed 区（`TDMA_PROCESS_IMAGE_VDC_OFFSET/SIZE`；命令分片复用同区） |
| 14–23 | 10 | RefMem 区（`TDMA_PROCESS_IMAGE_REFMEM_OFFSET/SIZE`）；class=0x15 时**整段**由 T2 紧凑切片占用 |
| 24–25 | 2 | ACK seq16（`TDMA_PROCESS_IMAGE_ACK_SEQ16_OFFSET`） |
| 26 | 1 | ACK flags（bit0..2 VALID/NACK/FENCE）+ quality 高半字节 |
| 27 | 1 | CONTROL opcode（`..._CONTROL_OPCODE_OFFSET`；`0x00` 无 / `0xD1`·`0xD2` 诊断 / `0xE0\|run_id` 一等冻结） |
| 28 | 1 | CONTROL seq8（诊断分片号 或 冻结位置掩码） |
| 29 | 1 | optional diagnostic（`..._OPTIONAL_DIAGNOSTIC_OFFSET`，由 `distributed_refmem.c` 镜像构建写入并由 `last_optional_diagnostic` 读出） |
| 30–31 | 2 | CRC16（`TDMA_PROCESS_IMAGE_CRC_OFFSET/SIZE`） |

**结论（影响方案取舍）**：短槽 **没有空闲字节**——方案 A 原文假设的"未用余量"不存在。可行的独立化路径只剩三条：

1. **A″ 回收式专属**：把某个已用字段的所有权显式转给一等席（候选：偏移 29 的 optional diagnostic，其数据可由诊断自身载荷携带或改为派生；或偏移 26 的 1 个空闲标志位——**1 bit 不足以承载 run_id + 掩码**）。任何回收都是**所有权变更**，需 `EXE-SEAT-01` 评审 + 登记，且必须给出旧 owner 的替代方案。
2. **B 专属帧**：不回收任何字节，代价是每圈多一帧 → 需重算 origin 发布机会 M:N 与优先级 lane 的 IRQ WCET（评审问题 3）。
3. **C 现状**：准入 + 仲裁（已实施），把 `lane_ack_override_count` 当告警位，明确记录"未独立"。

补充事实：opcode **命名空间已是分离的**（一等 `0xE0|run_id`、诊断 `0xD1`/`0xD2`），因此接收侧不会把两者混淆；真正的共享只发生在**同一对字节的写权**上。若选 C 长期化，建议把"命名空间分离 + 准入 + 覆盖计数为 0"写成显式验收条款。

## 三、推荐与待评审问题

鉴于附录 A 的结论（短槽无空闲字节），推荐顺序修正为：**A″（回收式专属，需明确旧 owner 替代） 或 B（专属帧） → 过渡期与长期兜底为 C，但必须标注"未独立"**。A 原文（"使用未用余量"）**不成立**，应从候选中移除。

待评审问题（须在实施前给出结论）：

1. ~~余量是否存在~~ → **已答（附录 A）：不存在**。改为：若选 A″，回收哪个字段（建议偏移 29 的 optional diagnostic），其旧 owner（`last_optional_diagnostic` 读回）的替代方案是什么？
2. 线格式变更的**版本兼容策略**：是否保留旧字段回退读取？多板混版本是否允许（若允许，需给出混版本行为定义）？
3. 若走方案 B：新增一帧对 origin 发布机会 M:N 轮换与 IRQ WCET 的具体影响数字。
4. 一等段被非一等 owner 写入的检测方式与计数命名（拟为 `lane_alien_write_count`；与现有 `lane_ack_override_count` 的关系需明确，避免重复计数）。
5. 该变更的验收是否必须包含**四板台架**证据（本目标其余条目目前只有主机证据），以及是否与 `TDMA-SEAT-03` 的 P3 门禁合并提交。
6. 若长期停留在 C：把"opcode 命名空间分离 + 诊断准入 + `lane_ack_override_count` 恒为 0"写成显式验收条款，是否足够作为 `TDMA-SEAT-03` 的关闭条件？

## 四、与其它条目的关系

- 本方案落地后，`lane_ack_override_count` 应从"常态可能非零"退化为"应当恒为 0 的告警"；若仍非零，说明独立通道未真正建立。
- 一等席接收侧（缺口①/④-RX）已完成：有界 FIFO（深度 4，满则丢弃并计数）、按到达顺序补投、单条重试有界、已投递副本标记消费。TX 侧独立后，一等席在"准入/失败记录/完整性/期限/通道"五个维度均达到特等席口径。
