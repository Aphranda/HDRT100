# OTA HAOFV 实施待办

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_TODO.md`
Related: `docs/ota/OTA_HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_TASK_PROGRESS.md`, `docs/arch/HAOFV_FLASH_TODO.md`
Last updated: 2026-11-05

本文维护 OTA 域当前主线、稳定任务 ID、状态和退出门禁。历史完成清单与单次验证快照已迁入 `docs/legacy/ota/`；实现证据只追加到 `OTA_TASK_PROGRESS.md`。

## 文档接口

| 文件 | 唯一职责 |
|---|---|
| `OTA_HAOFV_ARCHITECTURE.md` | owner、ECC、Vector、Boot Control 和失败恢复稳定语义。 |
| `OTA_TODO.md` | 任务状态、依赖、退出门禁和阻塞项。 |
| `OTA_TASK_PROGRESS.md` | 提交、测试、构建、HIL、失败和回退证据。 |

跨域 Flash v2、Recovery、signature、TDMA stream 和 durable journal 总依赖仍由 `HAOFV_FLASH_TODO.md` 跟踪。本文件只维护 OTA 域内落地，不重复宣布跨域里程碑完成。

## 状态规则

状态只使用：

- `DONE`：代码、相关软件测试、构建和要求的硬件证据闭合。
- `IN PROGRESS`：当前唯一主线，已有可复核进展。
- `PENDING`：前置条件未完成或尚未开始。
- `BLOCKED`：存在明确外部阻塞，且进度文件记录了失败证据和恢复条件。

## 已有基线

| 基线 | 当前事实源 |
|---|---|
| Direct A/B、test/confirm/revert 和统一 package | `HAOFV_FLASH_ARCHITECTURE.md`、BootControlStore/portable OTA 代码及历史 HIL |
| App Flash 唯一 writer | `FlashTransactionAO`/`FlashTransactionFB` 和 `ARCH-FLASHOWNER-01` pending 契约 |
| BCB 双 lane append/commit/GC | `BootControlStore` 和 `ARCH-BOOTCTRL-01` pending 契约 |
| Stream ingress/session | `OtaStreamSession` 和 `ARCH-OTASTREAM-01` pending 契约 |
| 当前目标板验证 | `OTA_TASK_PROGRESS.md` 与 `out/ota/` 原始证据 |

## 当前主线

将 OTA pending 路径收敛为符合 HAOFV 的单 owner、非阻塞 FB、显式 selection、固定 workspace、Flash intent/completion 和 Vector snapshot 流程。公共 SCPI 状态保持兼容，内部逐步移除同步扫描、hidden hint、现场查询和临时 fault 语义混放。

> V1 compat 自 2026-11-05 起已归档为 `ARCHIVED-REFERENCE + RECOVERY-BASELINE`。V1 没有独立
> `CALIBRATION_NVS`，其校准/引导档结果不得外推到 V2；factory migration、四板 P3 FULL、
> A/B/revert/Recovery 按历史记录完成，真实掉电矩阵按用户裁定不纳入本轮门禁。V1 factory/runbook
> 只读保留，代码/构建入口归档为 legacy/reference；产品回退决策不再是归档门禁。规则见
> `docs/ota/OTA_V1_LEGACY_PLAYBOOK.md`，记录见 `docs/arch/HAOFV_FLASH_TODO.md` M4-06。

## 里程碑总览

| 阶段 | 目标 | 退出门禁 |
|---|---|---|
| P0 根因修复 | 消除 BCB CRC 深栈 HardFault，保留可复核故障证据。 | portable tests、App/Boot build、COM7 A/B 双向 OTA。 |
| P1 selection 收敛 | 一次 pending 只生成并消费一个显式 selection。 | 无 hidden hint、无 const mutation、host fault matrix 通过。 |
| P2 非阻塞 scan | BCB scan 每 tick 推进一个有界步骤，执行预算生效。 | OtaFB 返回 BUSY/DONE/FAILED，满 lane 仍不阻塞 AO。 |
| P3 owner/Vector 收敛 | BCB append 由 FlashTransactionAO typed intent 执行；SCPI 只读 Vector。 | raw writer/link gate、seqlock snapshot、无查询时 Flash IO。 |
| P4 发布验收 | 完整 fault/power-cut/stack/HIL/P3 证据。 | 当前源码指纹的 acceptance receipt 和 C11 审核。 |
| M5 TDMA 流式 OTA（跨域，2026-10-29 重新规划） | 静态维护态下，经 TDMA 环把镜像流式装到从板 inactive slot（升级期间只启动 TDMA）。 | G1–G3 决策门全部通过 + `ARCH-OTASTREAM-01` 转 `active` + C11；详见下节。 |

## 分阶段任务表

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| OTA-HAOFV-001 | 将 `docs/ota` 收敛为 Architecture/TODO/Task Progress 三件套，旧文件迁入 legacy，修复全库引用。 | DONE | `docs/ota` 仅三文件；严格命名、索引、链接、registry 和 pytest 全绿，pre-commit 在最终代码切片统一执行。 |
| OTA-HAOFV-002 | 修复 BCB body/seal CRC 的 page-sized 深栈副本并建立 fault-frame 证据。 | DONE | PC/LR 定位、分段 CRC、portable tests、RTOS multicore build、COM7 A→B→A PASS。 |
| OTA-HAOFV-003 | 用显式 `pota_bcb_selection_t` 和 from-selection transaction API 替换 selection hint。 | DONE | 删除 hidden hint/const mutation；selection generation/lifecycle 明确；正反单测与 COM7 A→B→A 通过。 |
| OTA-HAOFV-004 | 实现 `pota_bcb_scan_begin/step/result` 固定 workspace。 | DONE | 每 step 最多处理一个 page；full lane、torn/CRC/schema/map、no-valid 和 stale-generation 测试通过。 |
| OTA-HAOFV-005 | 将 `MARK_PENDING` 接入 OtaFB 内部 ECC 子状态并真正执行 `budget_us`。 | DONE | scan/prepare/transaction 每 tick 有界；AO service 每次只推进一个动作；COM7 A/B 通过。 |
| OTA-HAOFV-006 | 增加 versioned Boot Control append intent，由 FlashTransactionAO 内部推进 transaction。 | DONE | App BCB program/erase 使用 submit + service + committed Vector；Boot 保持同步 adapter；COM7 A/B 通过。 |
| OTA-HAOFV-007 | 扩展 OtaVector/DiagnosticsVector 并将 SCPI 查询迁为 snapshot-only。 | DONE | OtaAO 双缓冲 Vector/metadata snapshot；SCPI 查询不触发 BCB/Flash IO；COM7 A/B 通过。 |
| OTA-HAOFV-008 | 移除旧同步 App scan、selection service hook 和完成定位后的临时 fault instrumentation。 | DONE | 主 App pending 路径仅走 HAOFV step/async owner；临时 fault/page trace 已移除；portable/RTOS build 与 COM7 A/B 通过。非主线历史同步 wrapper 保留在兼容边界并单独审计。 |
| OTA-HAOFV-009 | 建立 stack-usage、CRC 等价、BCB 满 lane/GC/torn 和查询无 IO 回归门禁。 | DONE | OTA HAOFV static gate、固定 workspace 栈帧 gate、portable BCB/FlashTransaction/Journal reset matrix、CRC/BCB tests、query wiring 全部通过。 |
| OTA-HAOFV-010 | 执行 COM7 多轮 A/B、负向矩阵、power-cut 边界和仓库 P3 hardware acceptance。 | IN PROGRESS | 当前源码构建、COM7 positive/negative matrix、NO1–NO4 UF2 和 P3 staged 检查通过；真实 power-cut、正式 P3 receipt/C11 尚未闭合。 |
| OTA-HAOFV-011 | 隔离 `pota_program_target()` 的大栈缓冲，并按新水位重算 OTA 任务栈。 | IN PROGRESS | `program_buffer` 不再占用任务栈（AO 私有 workspace 或调用方提供）；`APP_OTA_TASK_STACK_WORDS` 依据实测水位重算；`SYSTem:RTOS:STATus?` 的 `ota` free words 达到书面门限；host 用例与 build 回归通过。已实现：整页按页暂存（对齐安全）、块大小帧消失；实测峰值 1428 words（全包传输后 high-water），任务栈 1921→**1792 words**（+25% 余量），`ota` free=364 words。**注意**：曾按公式压到 1152 words，P3 四板 OTA 在 BEGIN/erase 阶段全部溢出复位，已回退（见 `OTA-TDMA-PROGRESS-20261029-005`）。 |
| OTA-HAOFV-012 | 为 `RECEIVING`/`ERASE_SLOT` 增加无进展超时，并自动清理会话与 OTA admission。 | IN PROGRESS | 超时后确定进入 `FAILED`（带 reason）、释放 admission，下一次 `BEGIN` 可被接受；host 用例覆盖“主机中断后重连”和“无进展超时”两条路径。已实现 `ota_progress_watch` + `NO_PROGRESS` reason + “先 abort 会话再置 FAILED”的收尾顺序；host 单测覆盖超时判定/回绕/禁用/erase 进度，**待 HIL 证明 FAILED 后 BEGIN 可再接受**（见 `OTA-TDMA-PROGRESS-20261029-002`）。 |
| OTA-HAOFV-013 | 修正 `SYSTem:OTA:DATA` 在拒绝路径上未消费 arbitrary block 的顺序缺陷。 | IN PROGRESS | RUN/ARMED、stream-active 和参数错误分支都在解析数据块之后返回（静态顺序断言）；行为级用台架负向证明：RUN 态下发 DATA 后，下一条查询应答不被污染。host 侧因整行缓冲 + libscpi 跳过未消费参数，症状不可复现（详见 `OTA-TDMA-PROGRESS-20261029-001`）。 |
| OTA-HAOFV-014 | 收敛主机 OTA 工具的等待预算与吞吐策略。 | IN PROGRESS | `ota_send` 能区分“块被拒/块未消费/链路断”并对失败立即 `ABORT`；CAP 查询失败不再静默退回兼容块大小；单板与四板 OTA 在既有验收窗口内完成或明确失败并保留原始证据。已实现：`OtaTransferError` 分类（`capability_unavailable`/`open_rejected`/`open_timeout`/`block_rejected`/`block_not_consumed`/`link_lost`）+ 失败即 `SYST:OTA:ABOR`（尽力而为并记 `abort_status`）+ CAP 失败默认 fail-closed、需 `--allow-compat-block-size` 显式覆盖。四板实测：P3 运行 `20261030/p3-001332` 的 `ota-four-board` 4/4 通过、175.95 s；P3 最终凭证被 DPLL 外置示波器窗口未命中阻塞（台架，非 OTA 路径）。 |

## TDMA 流式 OTA 里程碑（M5，2026-10-29 重新规划：决策门 + 台架夜次）

### 0. 目标与非目标

**目标**：单一上位机串口下，协调者板读取 package，经 `TDMA_PAYLOAD_CLASS_OTA_BULK` / `TDMA_TRAFFIC_RELIABLE_BULK` 把镜像流式装进从板 inactive slot；A/B 安装、pending/confirm/rollback 仍由既有 `pota_stream_session` + `portable_ota_port` 负责。

**非目标**：`ARCH-OTASTREAM-01` 的其余本地 transport（USBTMC/UART/RS485/SD）；Recovery 分发；产线量产门禁；与实时流量共存。

### 1. 前提（用户 2026-10-29 裁定，不可协商）

1. **OTA 是静态动作**：升级期间只启动 TDMA 环，不启动 DPLL/VDC，不做测量/T2/校准；结束重启后按正常流程恢复并复测 P3/T2。
2. **owner 不变**：`TdmaSchedulerAO` 独占 gate/window/adapter，`OtaAO` 独占 session/distribution，`FlashTransactionAO` 独占 durable write。
3. **C1 不变**：erase/program 只由 core0 发起、core1 park ACK 有效；core1 侧不得触碰 flash API。
4. **先 2 板再四板**：四板、单口与契约都在 G1/G2 通过后才做。

### 2. 现状账（2026-10-29 只读核对，快照，非事实源）

| 面 | 现状 | 结论 |
|---|---|---|
| session / A-B | `pota_stream_session` / `pota_stream_ingress` 与 transport 无关；source 仅作准入/诊断；durable offset、checkpoint/journal、pending/confirm/rollback 齐备 | 可直接复用 |
| 接收入口 | `portable_ota_port_stream_write(source, offset, data, size, has_crc32, crc32)` 是唯一入口，`source_mask` 覆盖全部 source | 加一个 TDMA 源即可 |
| 环可靠通道 | `tdma_maintenance_runtime` 已有分片、按 target/required mask 的 ACK、超时/重试/重复计数（现用于 REFMEM 命令分片） | v1 载体；每帧载荷很小 |
| TDMA 服务 | `tdma_component_core1_service()` 在 `ota_ao_is_active()` 时整段返回 | 必须改为“静态态只服务维护流” |
| OTA bulk 生产/消费 | 无 `TdmaOtaIngress`、无 producer、无 distribution；`TDMA_PAYLOAD_CLASS_OTA_BULK` 仅有类与常量预留 | 本里程碑主体 |
| host 工具 | `--method tdma` 是预留位，`method_reserved()` 在 Step 3/4 直接失败 | 需实现 |
| 契约 | `ARCH-OTASTREAM-01` = `pending` | 最后收口 |

### 3. 形态结论（计划为什么这样切）

flash program/erase 期间 core1 必须 park（`drv_flash_lockout_core1_poll()` 进入 spin），core1 上的 TDMA 在写窗口内必然停摆。因此：

- 传输 = **分块 + 容忍停车**：收块入 RAM → park 写 → 解锁 → 回 durable ACK → 下一块；
- **stop-and-wait 是 v1 的正确选择**（写 flash 时间主导）；credit/窗口只在吞吐不足时启用；
- 静态态下没有实时流量要保护，②门退化为“升级期间确实没起 DPLL/VDC”（可 SCPI 读回）；
- 既有 maintenance stream 可直接承载 v1 的块，吞吐受“每帧载荷很小 + stop-and-wait”限制，只作可行性证明；v2 再评估 long frame（`TDMA_PROFILE_DEFAULT_LONG_CAPACITY`）与窗口化 ACK。

### 4. 三个决策门（每门可独立失败）

| 门 | 要回答的问题 | 通过判据 | 失败处理 |
|---|---|---|---|
| **G1 可行性** | 停车窗口内环协议是否仍成立？2 板能否装完？ | park 时长与 `schedule_miss` 增量有量化；2 板装好 inactive slot 到 `READY_TO_REBOOT`；环不损坏 | 反复重同步/数据损坏 → 判**不可行**，契约保持 `pending` |
| **G2 可重复** | 连续多次、断链/掉电/回滚是否可恢复？ | 连续 N 次通过；drop/duplicate/reset/超时注入后能续传或明确 abort；回滚可复现 | 收窄到已知可恢复集，未过项不得进 G3 |
| **G3 产品形态** | 单口四板、reference 迁移、契约 | 四板滚动成功、每节点可观测、同指纹凭证、C11 复核 | 停在 2 板形态，不宣称“单口可升级” |

### 5. 夜次编排（每晚只推进一个门的一步；白天写代码与评审）

| 夜次 | 目标 | 前置 | 判据 | 失败分支 |
|---|---|---|---|---|
| **N1 探针** | 不改产品代码：1–2 次现有 OTA，采集 park/miss/吞吐 | 离场前置全绿 + 恢复演练完成 | 三个数齐全且可复算 | 数值离谱 → 先改设计，不进 N2 |
| **N2 G1** | 2 板最小闭环：降级服务 + codec + ingress + stop-and-wait | N1 可行；host 用例绿 | 2 板 `READY_TO_REBOOT`，环不坏 | 失败按签名表处置，不硬刷 |
| **N3 G2** | 重复性与故障注入 + 回滚 | N2 通过 | 连续多次 + 恢复矩阵 | 未过项收窄，不进 G3 |
| **N4 G3** | 单口四板 + reference 迁移 | N3 通过、四板在位 | 四板滚动 + 证据包 | 停在 2 板形态 |

### 6. 任务表

| ID | 门 | 内容 | 依赖 | 退出判据 |
|---|---|---|---|---|
| OTA-TDMA-000 | 前置 | 关闭 `OTA-HAOFV-010`–`014`（011 栈隔离、012 无进展超时、013 DATA 拒绝路径消费、014 host 等待预算） | — | 010 收口、011–014 DONE |
| OTA-TDMA-001 | G1 | 设计说明（初稿见 `docs/arch/ARCH_OTA_TDMA_STREAM_DESIGN.md`）：静态态 TDMA/flash 时序、park 边界、失败与恢复、C1/C2 逐条判定（不落代码） | 000 | 设计说明 + 用户审核记录 |
| OTA-TDMA-002 | G1 | `tdma_component_core1_service()` 改为“静态态只服务维护流”（VDC/RefMem/T2 仍按既有门退出） | 001 | host 静态断言；2 板环保持 running。**已实现**（2026-10-30）：早退条件改为 `ota_ao_is_active() && !ota_ao_tdma_maintenance_allowed()`，后者只在 stream 会话（静态维护传输）为真；legacy raw-data 会话保持原行为（环空闲、lockout poll 紧凑）。core1 仍不触碰 flash API（静态断言）；build `20261029163715`。**待两板 HIL**验证环在 OTA 期间保持 running |
| OTA-TDMA-003 | G1 | 探针与环协议验证：park 时长、`schedule_miss` 增量、帧完整性（`SYSTem:PROTection:STATus?` / `TDMA:RING:STATus?`） | 002 | 数据齐全 + 环不损坏 |
| OTA-TDMA-004 | G1 | 四帧 codec：`OPEN/DATA/ACK/CLOSE`（golden + overflow + 未知 flags fail closed） | 002 | host roundtrip + fuzz。**已实现**（2026-10-30）`components/ota_manager/{inc,src}/ota_tdma_codec.*`：帧上限 80 B（= 维护流单帧上限）、OPEN 按 76 B 分 2 片（可乱序、重复即重置）、DATA 载荷 ≤68 B、ACK/CLOSE/ABORT 定长，编码器容量不足或参数非法返回 0；golden vectors + 20k 随机解码 + 5k 往返 fuzz 通过。**待 005/006a 接入后**台架复验 |
| OTA-TDMA-005 | G1 | `pota_stream_ingress_source_t` 加 TDMA 源；`TdmaOtaIngress` 只调 `portable_ota_port_stream_write` | 004 | host 契约；不复制 session。**已实现**（2026-10-30）：`POTA_STREAM_INGRESS_TDMA` 入枚举；`components/ota_manager/{inc,src}/ota_tdma_ingress.*` 只做 offset 期望校验 + 转发 + durable 镜像，端口返回错误即映射为 `REJECTED`、不推进期望；host 用例覆盖 open/转发/offset 失配/坏帧/端口拒绝/close 失败保持 open/状态镜像；build `20261029163141`。**待 006a 接入载波后**台架复验 |
| OTA-TDMA-006a | G1 | durable ACK + stop-and-wait（ACK 在 program + readback 后回） | 005 | 2 板装 slot 到 `READY_TO_REBOOT`。**源侧已实现**（2026-10-30）`ota_tdma_pump`：单帧在途、只认本帧覆盖的 durable offset、陈旧 ACK 忽略、越界/拒绝 fail closed、超时重发同帧、次数用尽 FAILED、末帧 ACK 后发 CLOSE；host 用例覆盖 happy path/重试耗尽/陈旧与越界 ACK/发送失败。**待 002 载波接入 + 两板 HIL**。目标侧 ACK 已实现（2026-10-30）：`ota_tdma_ingress_ack_ready` 只在 durable ≥ 已接收字节时为真，`ota_tdma_ingress_encode_ack` 用当前 durable 编码 ACK；host 回环用例（pump→帧→ingress→伪会话→ACK→pump）20 B 三分片全部走通；并新增 `ota_tdma_session`（角色会话：source 发 OPEN+DATA、target 收 OPEN 开 ingress 并回 durable ACK、CLOSE/ABORT 收口），两会话经队列传输的 host 联调用例通过；载波接缝 `ota_tdma_carrier` 已实现（submit/take/ack 注入，submit 每次换 generation，poll 分发后回帧级 ACK），host 用例覆盖发送/代数递增/超长拒绝/ACK 推进/坏帧 NACK（2026-10-30）。**已知 v1 简化**：`tdma_maintenance_runtime_take()` 不返回 source_slot，两板载波用固定 `peer_slot`，多目标分发需 TDMA 侧扩展该 API。AO 入口 `ota_tdma_carrier_service()` 已实现（有界 drain + 重发推进，不阻塞/不碰 flash）；固件绑定 `application/src/ota_tdma_binding.c` 已接线（`app_ota_service` 仅在 stream 会话下调、v1 限 2 节点、状态放 FreeRTOS 堆以避开 RAM 上限），host 用例覆盖 slot→peer/mask、越界 slot 拒绝、OPEN/DATA/CLOSE 全链与 ack 计数（2026-10-30） |
| OTA-TDMA-013 | G1 | 夜间执行器：状态机 + 超时 + 重试预算 + resume + `attempt-XX/` 证据包 + `--recover` | 000 | host 覆盖四类恢复；到点安全收尾。**部分完成**（2026-10-30）：`tools/ota/ota_stream_send/night_runner.py` 实现 attempt 目录/`result.json`/`summary.json`、wall-clock 预算、失败即恢复钩子与 `--recover` CLI、runner 异常记录；host 5 用例通过。**待** resume 语义与真实 transfer 串联（需要 N2）。已串 relay：`tdma_night.py`（dry-run/attempt/预算/恢复），见 `OTA-TDMA-PROGRESS-20261030-027` |
| OTA-TDMA-014 | G1 | host `--method tdma` 最小实现（单口 → 协调者 → 环，不并行） | 005 | 与 2 板 HIL 同批跑通。**部分完成**（2026-10-30）：`ota_stream_send.py --relay-node 0..31`——OPEN 置 `CAP_RELAY` + 描述符字节 37，协调者据此转发；禁与 `--resume`/`--boot-and-commit` 组合；host 用例 15 passed。**待** 2 板实跑（`--recover` 已由 `tdma_night` 提供） |
| OTA-TDMA-006b | G1 | 源侧 relay：host 经既有 STREAM 把包发给协调者；`OPEN.destination_slot != 本板` 时协调者不写自身 flash，按 68 B 分帧转发到环；host consumed 以目标 durable ACK 为准（设计 §4.1） | 005/006a | 2 板 ≥300 KB relay → 目标 `READY_TO_REBOOT`、环不损坏、协调者擦写计数不增长；失败回 ABORT 并走既有分类。**host 已实现**（2026-10-30）`ota_tdma_relay`：4 KiB 单块窗口 + 64 B 分帧（窗口必须是 chunk 整数倍，否则拒绝）、整块 durable 才收下一块、durable 映射；host 用例 3×256 B 全链 + 非连续块拒绝通过。**SCPI 已接线**（2026-10-30）：`STREAM:OPEN` 按 `destination_slot != 本板槽` 进入 relay（binding 内 heap 分配），`DATA`→`relay_feed`、`CLOSE`/`ABORT` 收口、`STREAM:STAT?` 上报 relay durable/state；build `20261029171950`。**待** 两板 HIL。路由字段已定稿（2026-10-30）：`POTA_STREAM_CAP_RELAY`(bit2) + 描述符字节 37 = 节点号；SCPI OPEN 按该字段分流、目标侧只接受本节点 OPEN；wire 解码/编码与 host 用例已覆盖。剩余前置：`RING:STARt` 停机态返回 `-200`，环需由标定/剖面流程带起，当前仍缺“起环后注入 relay”的台架入口 |
| OTA-TDMA-006c | G1 | maintenance RX 单一 owner：runtime 完成队列 + `register_sink(stream_id)`；REFMEM/OTA/SCPI 迁移到 sink；`take()` 只弹队列 | 006a | 三方并发零丢帧（host 注入）；目标板无本地会话时可被 OPEN 打开（设计 §4.3）。**已按解析器注册实现**（2026-10-30）：REFMEM 任务仍是唯一 drainer，`0x4F` 分发到 OTA；binding 不再调用 `take()`；host 用例走 `handle_maintenance_frame` 路径。**待** 并发注入 HIL |
| OTA-TDMA-007 | G2 | resume：journal + readback 重建 durable offset | 006a | 两板 reset / 掉电重放 |
| OTA-TDMA-009 | G2 | 两板全链：装 slot → boot/confirm/revert → 重入环 | 007 | 同指纹凭证 + 两板摘要 + 升级后 P3/T2 恢复 |
| OTA-TDMA-015 | G2 | 重复性 soak + 注入矩阵（drop/duplicate/超时/park） | 007 | 连续 N 次通过；失败可分类。**host 矩阵已实现**（2026-10-30）：`tools/ota/ota_stream_send/tdma_matrix.py`（计划 JSON `OTA_TDMA_MATRIX_PLAN_V1`、`clean`×3 + `drop-1/3/8` 分类、可注入 `SYST:REFMEM:CMD:DROP`）+ `tests/python/test_ota_tdma_matrix.py` 4 项通过；**待** HIL 实跑（依赖 N2）。 |
| OTA-TDMA-006b | G3 | credit/背压（仅当 G1/G2 吞吐不足时启用） | 006a | 零信用 / gate closed 用例 |
| OTA-TDMA-008 | G3 | `OtaDistributionFB`：per-node 向量、stage-all→verify→cohort commit、reference 先迁移 | 006b 或 006a | host 状态机 + 三/四板负测 |
| OTA-TDMA-010 | G3 | 四板滚动：每节点可观测、失败隔离、掉线重加入 | 009、008 | 四板 HIL + 每节点证据 |
| OTA-TDMA-011 | G3 | 单口产品形态：协调者单口发起、四板全部升级（REFMEM 单口收敛后的唯一通道） | 010 | 机器捕获（`port` + 四板 build 回读）+ 失败路径留证 |
| OTA-TDMA-012 | G3 | 契约收口：`ARCH-OTASTREAM-01` 转 `active` | 011 | C11 独立复核；六类 transport 证据边界写清 |

**进度（2026-10-30）**：`OTA-HAOFV-011–014` 代码均已落（见 `OTA_TASK_PROGRESS.md` 条目 001–014）；**正式 P3 QUICK 凭证已产出**（`config/hardware_acceptance/p3_acceptance_receipt.json`：build `20261029165133`、`d3997aa9…`/1632 files、`passed=true`、`strict_gates_passed=false`、DPLL PASS、OTA 四板 4/4 174 s）——这是本批改动的同指纹凭证；`010` 的 power-cut 与提交仍未闭合（提交被工作区里与本批无关的 T2 工具改动阻塞，见 `OTA-TDMA-PROGRESS-20261030-015`）。

**N2 阻塞点（2026-10-31）**：relay 目标开流失败根因 = OPEN 描述符指针悬垂——`relay_start()` 的
88 B wire 位于 SCPI 栈，`ota_tdma_session_init_source()` 只存指针，OPEN 在 `relay_tick()` 的另一任务栈
编码时栈帧已被复用，目标 decode 得到全零/垃圾 → `MISMATCH`。已改为 relay 自持副本，并加 host 生命周期回归
（见 `OTA-TDMA-PROGRESS-20261031-080`）；**上板 N2 判据（目标 `READY_TO_REBOOT`、durable 递增、环不坏）
尚未复测**，`OTA-TDMA-006b` 仍按未完成记。

### 7. 夜间执行规则（配 `OTA-TDMA-013`）

**离场前置（任一不满足不启动）**：台架锁（确认无其它 T2/P3/OTA 在跑）→ 四板 STOP、VDC/DPLL 未启动、环 STOP、`ABORt` 清会话 → package sha256/build 写 `preflight.json`（夜间不重构建）→ host 用例绿 + 执行器 dry-run → 端口按 UID/VID-PID 绑定 → 证据目录与日志上限 → 恢复演练完成。

**运行循环**：一轮 = 一次可判定尝试，必须有 PASS/FAIL(reason) 与独立目录；每轮先读状态再决定；`--wall-clock` 到点即收尾（写 `final.json`、全 STOP、释放锁）；同一时刻只允许一块板写 Flash。

**失败签名 → 处置**：

| # | 签名 | 处置（有界） | 立即停 |
|---|---|---|---|
| 1 | `BEGIN` 无应答 | 重开端口 + `ABORt`，重试 1 次 | 连续 2 次 |
| 2 | `FAILED,1,"INVALID_STATE",4` | `ABORt` → `BEGIN`，重试 1 次 | `ABORt` 失败 |
| 3 | 中段停滞（块未消费） | `ABORt` → 按 durable offset 续传；无 resume 则重开 | 同一 offset 两次 |
| 4 | 维护帧 ACK 超时 | 退避重发；用尽 `TDMA_MAINTENANCE_MAX_ATTEMPTS` 判失败 | 同一帧 3 轮 |
| 5 | 环掉线/节点缺失 | 重读；必要时 `ARM→TRAIN→START`（≤2 次） | 两次失败 → 隔离该板 |
| 6 | 重启后 build 未变/回滚 | 只读 boot result 与槽位，不重刷 | 保留现场并停 |
| 7 | 任意命令无响应 | 恢复梯子（见下） | USB 不可见 |
| 8 | 串口消失/重枚举 | 按 UID 重扫，限时 | 超时 |
| 9 | 执行器崩溃 | supervisor 拉起 + resume，同阶段 ≤2 次 | 第三次 |
| 10 | 同错在等待/降温后重复 | 记录并停 | 立即 |
| 11 | 标定 generation 变化 | 立即停并标注 | 立即 |
| 12 | 证据写入失败 | 立即停 | 立即 |

**恢复梯子（picotool，允许但有界）**：台架已验证可强制进 BOOTSEL（`out/picotool_reset_a.json`）。顺序：串口可答 → `SYSTem:OTA:ABORt`；串口不答但 USB 在 → `picotool reboot -f -u`；已在 BOOTSEL → `picotool load` 同 build **app UF2**；USB 不可见 → 停（物理 BOOTSEL 属人工）。四条红线：① 只刷 app UF2，禁止 factory 镜像与 `--full-erase`（丢标定）；② 必须同被测 build；③ 每次写 `recovery.json`（原因/命令/前后 build/标定 generation/槽位/BCB/耗时），恢复不计 PASS、不掩盖原失败；④ 每板每晚 ≤2 次，第 3 次或与 preflight 不符即停。

**夜间硬禁止**：factory 镜像与 `--full-erase`；`--full-bench`（TRN-03 预算必失败）；夜间改产品代码后继续刷写；靠放大 timeout 或改终态判断“让测试通过”（`EXE-OTA-01`）；并行刷两块板；跳过 STOP 与恢复。

**晨间判读**：只回答三件事——park 时长分布、重试是否集中在 erase 窗口、吞吐是否随轮次退化；结论只允许“可行 / 需调参（写参数与依据）/ 不可行（写判据）”；`attempt-XX/` 与 `final.json` 以紧凑条目进 `OTA_TASK_PROGRESS.md`。

### 8. 硬门禁与不可行判据

**硬门禁**（任一不成立不得进入下一阶段，不得降级为 ⚠️）：

1. **C1**：erase/program 只由 core0 `FlashTransactionAO` 发起，core1 park ACK 有效，临界区内无 XIP/USB/SD/LCD；core1 调 flash API 判失败。
2. **C2**：不改 raw tick 回写与 `int32_t` 回绕安全语义。
3. **②**：升级期间只启动 TDMA（SCPI 读回为证），不得在测量/实时负载运行中发起 OTA；结束后按正常流程恢复并复测。席位/相位改动仍须工具核算 + `EXE-SEAT-01`。
4. **③**：三 owner 边界不变（见 §1 第 2 条）。
5. **④**：新失败路径有 reason/计数/快照，F0–F3 成立；gate closed 时保持 session，不形成重试风暴。
6. **⑤**：wire 版本/layout/DeploymentGate CRC/错误码段同步；契约未转 `active` 前不得出现“已支持流式 OTA”。
7. **⑥**：每阶段凭证绑同源码指纹；旧 build 与 scope 不覆盖的凭证不得充当本轮证据。

**不可行判据（fail-closed）**：若 G1 证明 park 窗口长到环无法维持协议（反复重同步或数据损坏），或 codec 无法在既有 payload class 与 `REFMEM-260B-01` 容量约束内成立，则判不可行，契约保持 `pending`，不降级验收、不以逐板接串口冒充“单口支持”。

### 9. 影响面

REFMEM 统一控制面按单串口收敛后，从板不再有独立升级串口；本里程碑是该形态下四板升级的唯一通道。未落地前，四板升级必须逐板接串口（`ota_multi_update`）或用 picotool（丢已存标定），不得写成“单口可升级”。

## OTA 性能与稳定性 TODO（2026-11-03 评审新增）

本节是 2026-11-03 对“SCPI 二进制块 fast path、流式 RX ring、16KiB 块、journal 合并、erase-ahead”方案的架构/RAM 评审结论与任务拆解。容量数字均为指定构建快照，非事实源；以 `tools/project/ram_budget_check/ram_budget_check.py` 和 `SYST:RTOS:STAT?` 的当前实跑为准。

**2026-11-03 用户裁定**：RAM 不足，暂不扩 8/16KiB；数据面保持 **4KiB**；先增加稳定性，再用 4KiB fast path、journal 合并和 256KiB 擦除尽量加速。`OTA-PERF-002/003` 的 8/16KiB 任务保持 `BLOCKED`，直到 RAM 回收、栈重构和 owner 评审通过；不得用“流式/吞吐”为由先扩 RAM。

### 1. 评审结论

| 结论 | 证据/约束 | 对方案的影响 |
|---|---|---|
| 现构建 RAM 已到链接硬底 | `ram_budget_check.py --profile release`：`link_free_bytes=2048`（净余量 0，快照）；`SYST:RTOS:STAT?` 空闲堆 832 B（快照） | 禁止新增常驻 buffer；任何新增先过 RAM 门禁 |
| 16KiB RX ring + 16KiB staging（32KiB）不可行 | 现无 32KiB 连续余量；`ucHeap` 已近耗尽，不得靠下调 heap 解决（`ARCH_RAM_REVIEW.md` §6） | 流水线 ring 降级或延后 |
| 16KiB 双缓冲（32KiB）不可行 | 同上 | 最多 8KiB 双缓冲（复用 `s_ota_fast_blocks` 16KiB） |
| 16KiB 单 staging 可复用现有池 | `s_ota_fast_blocks` 16,384 B（4×4KiB，`event_bus.c`）；V2 stream 期间 legacy `SYST:OTA:DATA` 被拒绝 | 需 EventBus/OtaAO owner 租约，不能与 legacy OTA 并发 |
| 16KiB 一次 FlashTransaction 不符合预算/C1 | `OTA_DEDICATED` 预算 5000 μs；一次 4KiB 事务已约 0.26–0.30 s；16KiB 放在一个 XIP-disabled section 会拉长临界区 | 必须 4KiB 分段 program/verify，journal 合并，不做 16KiB 单事务 |
| 当前 stream DATA 在 SCPI/USB 任务同步写 flash | `scpi_cmd_ota_stream_data()` → `portable_ota_port_stream_write()`；OtaAO 同时在 service 同一 session | 违反 OtaAO 单 owner/单写者边界；流水线必须先迁到 OtaAO event/lease |
| 提高 `POTA_MAX_DATA_BLOCK_SIZE` 有栈风险 | `pota_core.c` raw resume 的 `uint8_t buffer[POTA_MAX_DATA_BLOCK_SIZE]` 位于任务栈；OTA task 1792 words | 先改为 AO workspace/静态 buffer，再提高块大小 |

### 2. 方案评审

| 方案项 | HAOFV 合规性 | RAM | 结论 |
|---|---|---|---|
| 4KiB SCPI 二进制块 fast path（绕过通用 parser，超时/重同步） | 合规；仍由 FlashTransactionAO 写 flash | 不新增；复用现有 4KiB 输入/块缓冲 | **先做** |
| 8KiB 双缓冲（复用 `s_ota_fast_blocks` 16KiB） | 需 OtaAO 单 owner + EventBus 租约 | 不新增 | 可做，但必须先完成 owner/租约 |
| 16KiB 单 staging（复用 `s_ota_fast_blocks`） | 需 4KiB 分段 + 栈重构 | 不新增 | 有条件做 |
| 16KiB RX ring / 双缓冲 | 预算/C1 风险 | 32KiB 不可行 | **不做/延后** |
| 16KiB 单 FlashTransaction | 违反 OtaFB 非阻塞/预算/C1 | — | **不做** |
| 新开 writer task | 违反 OtaAO 唯一 owner / FlashTransactionAO 唯一 writer | — | **不做** |
| journal 合并（每 16KiB 只写 1 条终态记录） | 合规；需掉电矩阵 | 不新增 | 可做 |
| 擦除 256KiB/事务 | 合规；`drv_flash_erase_parked` 仍逐 4KiB 扇区、逐扇区喂狗 | 不新增 | 可做，需 HIL 复核 park/watchdog |

### 3. 任务表

| ID | 任务 | 状态 | 依赖与退出门禁 |
|---|---|---|---|
| OTA-PERF-001 | SCPI `STREAM:DATA` **4KiB** 二进制块 fast path：专用长度/CRC/seq 状态机、半包超时、RESYNC；不扩块大小 | PENDING | 不新增常驻 RAM；host/unit + NO.1 硬杀/半包；解析 <5 ms/4KiB；8/16KiB 不纳入本任务 |
| OTA-PERF-002 | 从 EventBus OTA fast pool 租借 staging（16KiB 单缓冲或 2×8KiB），V2 stream 与 legacy OTA 互斥 | BLOCKED | 用户 2026-11-03 裁定 RAM 不足先不做；需 RAM 回收 + EventBus/OtaAO owner 评审；`ram_budget_check` 不下降 |
| OTA-PERF-003 | 16KiB 块：`pota_core.c` raw resume 栈缓冲改 AO workspace；flash 仍 4KiB 分段 program/verify + 喂狗 | BLOCKED | 依赖 002；OTA task stack high-water 门禁；C1 复核；RAM 未回收前保持 4KiB |
| OTA-PERF-004 | journal 合并：每个 **4KiB 事务**只写 1 条终态 completion；checkpoint 保持 64KiB | PENDING | NO.1 已验（117.5/114.7s）；flash 事务 PERF 约 69/990ms；掉电/复位矩阵未做 |
| OTA-PERF-005 | 擦除 256KiB/事务（`pota_erase_one_step` 分块） | BLOCKED | 2026-11-04 试 256KiB：首个 soak 第 1 轮 12.8KiB DATA 超时；已回退 64KiB；需先查 OTA 任务心跳/擦除事务预算 |
| OTA-PERF-006 | CRC32 表驱动/硬件 CRC，替换 `pota_crc32` 逐位实现 | PENDING | 代码+host 全绿；最终 build **20/20**（124.1–129.4s，mean 127.5s）；四板未复跑 |
| OTA-PERF-007 | 4KiB EventBus QUEUED/window：SCPI 只入队，OtaAO 唯一写；host 窗口 4 | PENDING | NO.1 **20/20 soak**（121.0–123.6s）+ 硬杀/复位重试 1/1；四板/P3 未闭合，不得标 DONE |
| OTA-STAB-001 | stream session 单写者：DATA + CLOSE 已迁 OtaAO event/lease；OPEN/ABORT 仍由 SCPI 调用 | PENDING | 数据面/收尾单写者已落；OPEN/ABORT 控制面仍双入口；HAOFV-01/02/05；并发压力 + 20× soak |
| OTA-STAB-002 | 掉电/复位矩阵：擦除中、写入中、finalize 中、commit 后 | PENDING | 传输中 `picotool reboot -f` 已验证（active 可启动、重试 117.1s）；真实掉电/擦除/finalize 时点未做 |
| OTA-STAB-003 | 四板/单口 P3 FULL：OTA 4/4、TDMA/DPLL 不回归 | PENDING | 同指纹 P3 FULL receipt；四板 HIL |
| OTA-STAB-004 | `SLOT?` confirmed 快照发布；boot confirm 后刷新 metadata | DONE | NO.1 手动 `CONFirm` OK，OTA 切槽后 boot confirm OK（active==confirmed）；四板 P3 仍待 |
| OTA-TEST-001 | 20× commit+boot soak、kill -9 ×10、半包 ×10、power-cut ×10、四板 ×10 | PENDING | **20× soak 已过**；kill/半包/真实 power-cut/四板未全量 |

### 4. 4KiB-first 执行顺序（2026-11-03 裁定）

1. `OTA-STAB-001`：先消除 USB/SCPI 与 OtaAO 双写 session，保证并发/断连下不变砖。
2. `OTA-STAB-002` / `OTA-STAB-004`：掉电/复位矩阵、confirmed 快照；稳定性门禁。
3. `OTA-PERF-001`：4KiB SCPI fast path；不扩块、不新增 RAM。
4. `OTA-PERF-004` / `OTA-PERF-006`：4KiB 事务 journal 合并、CRC 表驱动；把单块从 ~0.58s 压向 ≤0.45s。
5. `OTA-PERF-005`：256KiB 擦除，缩短首块等待。
6. `OTA-STAB-003` / `OTA-TEST-001`：四板 P3 FULL、20× soak、kill/半包/掉电矩阵。
7. `OTA-PERF-002/003` 保持 `BLOCKED`；只有 RAM 回收、栈重构和 owner 评审通过后，才允许 8/16KiB。

### 5. 已落地但未达 DONE 门禁（NO.1 单板，快照）

以下改动已在 build `20261103140913` 的 NO.1 上验证，但未做四板/P3/power-cut，按状态规则不得记 DONE：

- BUSY/`ERASE?` 非阻塞擦除握手；CDC 250ms 有界写；USB 侧半包 2s 超时；host `*IDN?` 重试。
- 64KiB 擦除/事务、64KiB checkpoint、`SYST:OTA:PERF?`/`STREAM:INIT?`/`STREAM:ERASE?` 诊断、pending 已就绪时跳过 `SYST:OTA:COMM`。
- 证据见 `OTA_TASK_PROGRESS.md` 与 `out/ota_verify/`；硬杀 2/2 自动恢复、正常 OTA 6/6。

2026-11-03 新增（build `20261103144039`，NO.1）：

- STREAM DATA 改为 SCPI 只入 EventBus `QUEUED`，由 OtaAO 唯一写 flash；host 使用 4 块窗口，不新增常驻 RAM。
- 实测：完整 OTA **121.1s / 119.1s**（两次），`data_queued` 均值约 **0.317s**，`durable_wait` 总计约 13.2s；硬杀后自动恢复并重试 1/1 通过。
- RAM 门禁：`link_free_bytes=2048` 与改动前一致，未因队列/窗口下降。
- 仍未闭合：OPEN/CLOSE/ABORT 控制面仍由 SCPI 调用；OtaFB 5ms 预算尚未分步；四板/P3/20× soak/power-cut 未做。

2026-11-03 第二轮（build `20261103150915`，NO.1）：

- STREAM CLOSE 也改为 OtaAO event/lease；OPEN/ABORT 仍在 SCPI 入口。
- OTA_IMAGE completion journal 合并为 1 条终态记录；flash 事务 PERF 约 69/990ms；完整 OTA **117.5s**。
- 5× commit+boot soak：**5/5**，125.3–126.3s；硬杀恢复并重试 **114.7s**。
- RAM 仍为 `link_free_bytes=2048`；`FlashTransaction`/`OTA journal adapter` host 测试全绿。
- 已知未闭合：`SLOT?` confirmed 快照仍显示旧槽（如 `2,0,1,2,0`），但下一次 OPEN 已成功；`SLOT?` 发布修复未生效，`OTA-STAB-004` 继续 PENDING。

2026-11-03 第三轮（build `20261103152032`，NO.1）：

- 新增 `SYST:OTA:CONFirm` 手动确认诊断；但手发该命令无应答，`SLOT?` 仍为 `1,0,2,1,0`（active 1 / confirmed 2），confirm store 未更新快照，`OTA-STAB-004` 保持 PENDING。
- **20× commit+boot soak 完成**（两组各 10 次，同一 build）：**20/20**，121.0–123.6s，erase 9.1–11.4s。
- 复位矩阵（传输中）：在 durable≈70 KiB 时 `picotool reboot -f`；复位后 active slot 仍可启动、`SLOT?` 一致（`1,0,1,0,0`），随后完整重试 **117.1s** 通过。真实掉电仍未做（无程控电源）。
- 仍未闭合：OPEN/ABORT 控制面、OtaFB 5ms 预算、真实 power-cut/四板 P3、`OTA-PERF-001` parser fast path。

2026-11-04 第四轮（build `20261103172035`，NO.1）：

- `pota_crc32` 改为 16-entry nibble 表（`.rodata` 64 B，不占 RAM）；host portable tests 全绿。
- NO.1 完整 OTA 安装 + **20/20** commit+boot soak：118.6–129.3s，mean 127.4s，erase 8.1–10.9s；无 CRC/包校验回归。
- 20× 已补齐；四板/P3 尚未复跑。

2026-11-04 第五轮（build `20261103183228`，NO.1）：

- confirmed 根因：`FlashTransactionAO` 对 `OTA_METADATA` 要求 OTA admission；boot/手动 confirm 无 admission → `FLASH_TRANSACTION_ERROR_MODE`（`FT?` last_error=17）。
- 修复：boot/manual confirm 临时借用 maintenance admission，store 后立即释放；不创建 OTA 会话。
- 验证：手动 `SYST:OTA:CONFirm` 返回 `OK`；OTA 切槽后 boot confirm 使 `SLOT?` 从 `2,0,1` 变为 **`2,0,2`**；BCB 从 1 lane/246 records 变为 2 lanes/254 records。
- 新增只读诊断 `SYST:OTA:BCB?` / `SYST:OTA:FT?`。
- 最终 **20/20** soak（build `20261103184145`）：124.1–129.4s，mean 127.5s；boot confirm 从 5 s 提前到 1 s 且仅在 OTA IDLE 时执行，避免与主机 OTA 抢 Flash。
- 20 次后 `SLOT?` 为 `2,0,2,0,0`（active==confirmed），BCB 双 lane 正常。
- 最终 build 硬杀恢复：`ABORT OK`，重试 **114.0s** 通过。
- 最终 build 传输中 `picotool reboot -f`：复位后 active 可启动、`SLOT? 2,0,2,0,1`，重试 **122.2s** 通过；真实掉电仍未做。
- 该 build 为当前最终 OTA 候选；四板/P3 仍未复跑。

2026-11-04 第六轮（256KiB 擦除试验/恢复）：

- 试 `POTA_ERASE_CHUNK_SIZE=256KiB`：首个 5× soak 第 1 轮在 12.8KiB DATA 超时；判定 256KiB 擦除事务超过 OTA 心跳/预算，已回退 64KiB，任务转 BLOCKED。
- 回退过程中为恢复 NO.1，曾尝试 raw app 写入导致进入 recovery；最终用 factory UF2 恢复，**NO.1 标定/BCB 被重置**（已披露）。
- 恢复后 OTA 安装 64KiB final build `20261103193953`：active 2 / confirmed 2，BCB 新写 4 records；后续 P3 必须重新标定。

2026-11-04 第七轮（P3 重标定尝试）：

- full P3 重新构建/OTA；step4 异步 OTA 报 OK，但 step24 实测四板仍为旧 build（NO.1 `193953`、NO.2–4 `152032`），expected `195548` → `validate_recorded_builds` 直接 FAIL。
- step8–23 在混合 build 上“通过”，不能作为有效标定/凭证；NO.1 标定仍视为丢失。
- 结论：四板 OTA 的 step4 成功判定与 build 回读不一致，是当前 P3 的首要阻塞；step29 follower priority RX 仍未解。

2026-11-04 第八轮（统一重刷/重标定）：

- 按用户授权，四板分别 OTA 重刷到 `20261103195548`；preflight 四板 build 一致通过。
- `--reuse-flashed` 旧 OTA summary 带旧 build_id，导致 step24 误报 mismatch；已修为复用现场 build 时直接合成当前 summary。
- 重新跑完 step8–23 标定；**step24 OK 77.4s**；step25/26 OK。
- step29 仍失败：follower `match last_reason=18`、`final_follow mode=4/state=1`，与重刷/重标定前同型；不是 build/标定漂移。
- 结论：NO.1 标定已恢复；step29 是独立固件/TAP-RX 问题。

补充（event_snapshot 解码）：

- follower `event_snapshot` 显示 `publish_count≈1141`、`latest_sequence≈733`，说明 priority RX 实际收到并发布了帧；
- 但 matcher `matched=0`、`last_reason=18`，其 `ring.enabled/adapter_started` 快照与实际 ring 状态不一致；
- 根因方向从“无帧”修正为“帧到达但 matcher 的 owner ring 快照为 STOP”。

**2026-11-04 交叉复核更正（快照，非事实源）**：上述“TAP-RX/帧已到但 matcher 的 ring 快照为 STOP”判读不适用于 2026-10-31、11-03、11-04 三轮。直接证据：四板 `SYSTem:SYNC:VDC:DPLL:ROLE?` 均为 `0,0,1,1,0`（MASTER），三从 `MATCH:STATus?` 的 `first_reject_reason=4 (MATCH_ROLE)`、`matched=0`；`event_live` 非零。失败链是产品配置重置后的角色门，不是 RX 无帧。另：主板 `guard.reason_mask` 的 `SUCCESS_GAP (1<<17)` 是独立第二道门；`restore_original_ram` 的 SCPI 队列错误是独立缺陷。工具侧修复与证据见 `docs/tools/DOCS_TOOLS_TASK_PROGRESS.md` 的 `TOOLS-PROGRESS-20261104-001`。

### 6. 硬门禁补充

1. 新增常驻 buffer 必须先跑 `ram_budget_check.py`；`link_free_bytes` 低于 2048 B 直接链接失败，净余量不得为负。
2. 不得通过下调 `ucHeap` 换取 OTA buffer（`ARCH_RAM_REVIEW.md` §6）。
3. 不得新开 writer task/第二 Flash writer；stream session 只能由 OtaAO 修改。
4. 16KiB 块不得对应 16KiB 单 FlashTransaction 或 16KiB XIP-disabled section；C1 仍按 4KiB 扇区/段。
5. 任何“流式/窗口化”实现必须先有 seq/offset/CRC/超时/RESYNC 与幂等重发，否则不得以吞吐为由放宽。



`OTA-HAOFV-010` 剩余真实 power-cut 控制、当前源码指纹的正式 P3 acceptance receipt 和 C11 独立审核；已有 host/reset matrix、COM7 negative HIL 与 NO1–NO4 UF2 证据不能替代这些门禁。另有 2026-10-29 评审发现的会话稳定性待办 `OTA-HAOFV-011`–`014`；这些条目关闭前，四板 OTA 的绕行通过不得计为稳定性完成。

TDMA 流式 OTA（M5，`OTA-TDMA-000`–`015`）是新增工作线，按 G1 可行性 → G2 可重复 → G3 产品形态推进；其 G1 前置同为 `OTA-HAOFV-010`–`014`；在该线落地前，"单口四板升级"属能力缺口，不得用逐板接串口或 picotool 的结果替代。

## 2026-10-29 稳定性评审记录（评审快照，非契约）

- 范围：只读 `out/n8/*.log`、`out/n8/com5_state.txt`、`out/HardwareAcceptance/20261029/`、`docs/refmem/REFMEM_TASK_PROGRESS.md` 与 OTA 代码；未上板、未改固件。
- 结论：OTA 主链功能在（四板并发可达 `READY_TO_REBOOT`，见 `out/n8/ota_multi_n57/summary.json`、`out/n8/ota_multi_n85/summary.json`，快照）；失败集中在会话状态、拒绝路径和等待预算，不是镜像、CRC 或 Bootloader 功能缺失。
- 失败签名（原始日志，快照）：

| 签名 | 证据 | 判读 |
|---|---|---|
| `PBEGIN` 后长时间无任何应答（`last response` 为空） | `out/n8/ota_com4.log`、`out/n8/ota_com3.log` | 会话未建立，或 SCPI 解析已失步 |
| `"FAILED",1,"INVALID_STATE",4` | `out/n8/diag_ota_com4.log` | 上一会话停在 `RECEIVING`，被 `ota_fb_state_accepts_begin()` 拒绝 |
| 传输中段停止且无错误行 | `out/n8/ota_com5_n84.log`（停在 627‰） | 单个 DATA 块未被消费；主机卡在 `wait_for_received_offset()` |

- 现场约束（快照，非事实源）：单板 2.2 MB 级 package 的传输耗时为分钟级（见 `out/n8/ota_multi_n57/summary.json` 的 `elapsed_s`）；台架当轮以 `--skip-pre-reboot --settle 2.0 --block-size 512` 绕行，见 `REFMEM_TASK_PROGRESS.md` 的 20261029-005/006/007 条目。
- 静态定位（代码符号，非契约）：`pota_program_target()` 的 `program_buffer` 由 `POTA_MAX_DATA_BLOCK_SIZE + POTA_MAX_FLASH_PAGE_SIZE` 决定且位于任务栈；`APP_OTA_TASK_STACK_WORDS` 由 `APP_OTA_TASK_BASE_STACK_WORDS` 加块大小增量构成；`ota_fb_state_accepts_begin()` 只接受 `IDLE/ABORTED/FAILED/COMMITTED`；`scpi_cmd_ota_data()` 在解析任意块之前做 RUN/stream 判定。
- 待办：`OTA-HAOFV-011`–`014`。

## 统一完成定义

- Architecture、TODO、Task Progress 事实边界无复制冲突。
- OtaAO 是 session/ECC/Vector 唯一运行 owner。
- OtaFB action 立即返回，所有 scan/write/wait 分步推进。
- App erase/program 仅由 FlashTransactionAO 执行，Boot 使用 BootFlashService。
- 一次 pending 只有一个显式 selection，没有 hidden hint 或 const mutation。
- CRC/scan/transaction 使用固定 workspace并满足栈门禁。
- SCPI/UI/Diagnostics 只读取版本化 Vector，不现场扫描 Flash。
- A→B、B→A、回滚、torn/power-cut、BCB 损坏和 fault matrix 有确定结果。
- 文档门禁、软件测试、构建、P3 硬件验收和 C11 审核全部闭合。
