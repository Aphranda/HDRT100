# OTA TDMA 流式升级设计说明（静态维护态 v2：REFMEM 控制面 + TDMA 数据面）

Status: Active
Domain: OTA / TDMA / Flash（跨域）
Canonical: `docs/arch/ARCH_OTA_TDMA_STREAM_DESIGN.md`
Related: `docs/ota/OTA_TODO.md`, `docs/ota/OTA_V2STREAM_PLAYBOOK.md`, `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/arch/HAOFV_FLASH_TODO.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-10-31

> 本文是 `OTA-TDMA-001` 的设计说明。**2026-10-31 起以 §13「v2 架构决定」为准**：
> TDMA 维护流只承载 OTA 数据，控制面归 REFMEM，目标侧复用既有 AB 流程，
> 退役第二套 stream 会话。§4.2/§4.3/§5/§6 中与 §13 冲突的 v1 判读按 §13 执行
> （保留为历史论证，不删除）。契约：`ARCH-OTASTREAM-01`（pending）拟由
> `ARCH-OTASTREAM-02` 取代，C11 复核待办（见 §13.5）。

## 1. 目的与范围

回答一个问题：**在“只有 TDMA 环在跑”的静态维护态下，如何把镜像流式送进从板的 A/B inactive slot**，且不改变既有 OTA session/A-B 语义。

范围：2 板（协调者 + 目标）的最小闭环，作为 G1 的设计输入。

非目标：四板 cohort/滚动（四板只作传输证据，见 §13.3）、单口产品形态、掉电续传矩阵、与实时流量共存
（DPLL/VDC 在升级期间不启动）。契约登记不属本文范围：`ARCH-OTASTREAM-02` 已按流程登记为
`pending`，转 active 的前置条件见 §13.5。

## 2. 运行前提（用户 2026-10-29 裁定）

| 项 | 状态 |
|---|---|
| DPLL / VDC | **不启动**（升级是静态动作） |
| 测量 / T2 / 校准 | 不启动 |
| TDMA 环 | 启动，仅承载维护流 |
| Flash 写 | 仍由 core0 `FlashTransactionAO` 发起，core1 park ACK |
| A/B 安装 / pending / confirm | 复用 `pota_stream_session` + `portable_ota_port`，不改语义 |

## 3. 关键约束：flash lockout 与 ring 的时序

`drv_flash_lockout_core1_poll()` 在 `requested` 置位后让 core1 进入 spin，直到 core0 释放；这段时间 core1 上的 TDMA 服务不可能运行。因此“流式”不是连续管道，而是**交替**：

```text
host            coordinator                 ring                     target
 |  chunk ------> |                          |                         |
 |               |-- data frame ----------->|------------------------>| RX → ingress
 |               |                          |                         |  (RAM buffer)
 |               |<------------- receipt ACK|-------------------------|
 |               |                          |                         | park core1
 |               |        (target parked; ring cycles missed)          | erase/program
 |               |                          |                         | unpark
 |               |<------------ durable ACK |-------------------------| (offset)
 |  next chunk < |                          |                         |
```

设计含义：

1. **数据帧必须在 park 之前收完并确认**——否则帧落在停车窗口里，靠 `TDMA_MAINTENANCE_MAX_ATTEMPTS` 重试会打空。
2. **durable ACK 在解锁之后回**——它表示“已落盘”，不是“已收到”。
3. **源在两次 ACK 之间不发帧**（stop-and-wait），把“停车窗口里无帧在途”变成协议保证，而不是时序巧合。
4. 停车造成的周期丢失由环自身吸收；G1 的判据是“**协议不损坏**”，不是“零丢周期”。
5. **服务门（`OTA-TDMA-002` 已实现）**：`tdma_component_core1_service()` 仅对 stream 会话放宽早退（`ota_ao_is_active() && !ota_ao_tdma_maintenance_allowed()`），legacy raw-data 会话保持原“整段跳过”，因为后者不需要环；core1 侧仍不触碰 flash API。

## 4. 数据路径与接口

| 环节 | 载体 | 现状 | 本设计动作 |
|---|---|---|---|
| host → 协调者 | USB CDC + 现有 package/session 入口 | 已有（`portable_ota_port_stream_*`） | 新增 `--method tdma` 走同一入口 |
| 协调者 → 环 | maintenance stream（`tdma_maintenance_runtime`） | 已有分片/ACK/重试 | 新增 OTA 帧编解码与发送泵 |
| 环 → 目标 | maintenance stream RX | 已有接收与重组 | `ota_tdma_ingress`（已实现）：offset 校验 → `portable_ota_port_stream_write(source=TDMA, …)`；不复制 session |
| 目标 → Flash | `FlashTransactionAO`（park 协议） | 已有 | 不改；由 session 驱动 |
| 源侧调度 | 无 | 缺 | 新增 stop-and-wait 泵（一次一块） |

**v1 载波决定**：控制帧与数据帧都走既有 maintenance stream。理由：ACK/超时/重试/重复检测已经存在，静态态下 REFMEM 服务不运行，无竞争。若 N2 测出吞吐不可接受，G3 再评估 `TDMA_TRAFFIC_RELIABLE_BULK` + long frame（`TDMA_PROFILE_DEFAULT_LONG_CAPACITY`），**v1 不引入新的调度机制**。

### 4.1 源侧取包路径（v1 决策，2026-10-30）

问题：协调者必须拿到 package 才能作为 source 发号；目标镜像 2.2 MB 级，放不进 RAM，也不能凭空出现在板上。

| 方案 | 做法 | 代价 / 风险 |
|---|---|---|
| A. SD 中转 | source 从 SD 卡读包（`storage_manager` 异步 4 KB 读） | 依赖 SD 可挂载与读吞吐；多一个文件状态机；台架需插卡 |
| **B. USB 边收边转（选定）** | host 仍用既有 STREAM 命令把 package 发给协调者；`OPEN.destination_slot` 指向目标槽，协调者**不写自身 flash**，按 64 B 分帧转发到环 | 复用既有命令/流控；需把“relay 模式”的 durable 定义为“目标 ACK 的 offset” |
| C. 先刷 source 再发号 | source 从自身 inactive slot 回读镜像 | package 的 manifest/hash 不在槽内连续区域，需要额外布局支持 |

**选定 B 的理由**：不引入新存储依赖、不新增命令槽（仍是 `SYST:OTA:STREAM:*`）、协调者自身不擦写 flash（C1 暴露面更小），且 host 已有 durable/consumed 背压机制可复用。

**协议约定（v1）**
1. host 发 `OPEN`，置 `POTA_STREAM_CAP_RELAY` 且描述符字节 37 = 目标节点号；协调者发现 `relay_node != 本节点` → 进入 relay：不打开本地 port 会话，而用同一 88 B 描述符初始化 source 会话。**注意**：`destination_slot` 是 flash A/B 槽（1/2），不用于 relay 路由。
2. host 的 `DATA` 只用于喂 relay 窗口（≤4 KB 缓冲），协调者立即按 68 B 分帧转发；**host 的 consumed 以目标 durable ACK 为准**（不是协调者本地写盘）。
3. 目标 `OPEN`/`DATA`/`ACK`/`CLOSE` 走维护流；协调者只做转发与 durable 映射，不产生第二 ACK 事实。
4. 任何一侧失败：协调者向 host 回 `ABORT(reason)`，host 走既有失败分类；目标侧保留其本地失败快照。

**验收判据（G1 relay）**
- 2 板：host 经 USB relay 发 ≥300 KB package → 目标 `READY_TO_REBOOT`，环 `running` 不损坏；
- 协调者 `SYST:OTA:STAT?` 显示 relay 且**擦除/编程计数不增长**；
- 目标 park 窗口内丢帧可重发、durable 不倒退（`006a` 判据）；
- 证据：UART 日志、`SYST:OTA:STAT?`、`SYST:TDMA:RING:STAT?`、同指纹 build。

**残余（G2 再评）**：relay 吞吐受 64 B 分帧 + stop-and-wait 限制；四板 fan-out 需先解决 `take()` 不返回 source_slot 的接口债。

**relay 路由字段（2026-10-30 定稿）**：`destination_slot` 是 flash A/B 槽（1/2），**不能**用作环节点号；v1 用新增 capability 位 `POTA_STREAM_CAP_RELAY`（bit2）+ OPEN 描述符保留字节 `37`（`relay_node`，0..31）路由。解码要求：置位时允许 37 非零、38/39 仍必须为 0；未置位时 37 必须为 0（旧描述符行为不变）。目标侧只在 `relay_node == 本节点` 时打开 ingress，否则整帧丢弃（不判失败）。

**v1 relay 实现约束（2026-10-30）**：`ota_tdma_relay` 一次只驻留一个 4 KiB 块，块必须整块 durable 后才接受下一块；分帧 64 B 使 4 KiB 恰为 64 块，避免块边界跨帧；窗口不是 chunk 整数倍时 `relay_open` 直接拒绝（fail closed）。

## 4.2 独立复核结论（2026-10-30）与改版方向

只读复核（子代理）给出 28 条 findings，其中 8 个 blocker。**已确认 v1 载体选择（maintenance stream 80 B 帧）与 portable OTA 的写入约束冲突**：

| 约束 | 代码位置 | 与 80 B 帧的冲突 |
|---|---|---|
| 非末次写入必须是 flash 页（256 B）的整数倍 | `third_party/portable_ota/src/pota_core.c`（页对齐校验） | 64/68 B 帧无法被会话接受 |
| package 模式首个写入必须整 512 B 头 | `pota_core.c` / `pota_stream_session.c` | 80 B 帧无法一次交付包头 |
| 维护流单帧上限 80 B | `tdma_maintenance_stream` 常量 | 上述两项都要求 ≥256/512 B |

**改版方向（2026-10-30 修正）**：long frame 上限只有 **96 B**（`TDMA_LONG_FRAME_MAX`），同样装不下 256 B 页，因此不改载体；改为 **ingress 侧页/包头暂存**：首块 512 B、后续 256 B 页、或图像末字节时才调用 `portable_ota_port_stream_write`，ACK 以块为粒度（块内逐帧发、块结束等 durable ACK，重复帧幂等）。这样 C1（整页 program、core0 发起）与会话契约都不变。仍需解决：
1. 目标侧服务门（当前只在本地 stream 会话/relay 活动时轮询 `take()` → 目标永不收帧）；
2. `tdma_maintenance_runtime_take()` 的单一 owner/按 stream_id 分发（现被 refmem/OTA/SCPI 三处并发调用）；
3. relay 背压（host 必须等目标 durable）与 CLOSE 确认（当前 sender 发完即报 READY）；
4. 节点 id 单一来源（board identity vs ring slot 存在两套推导）。

### 4.3 maintenance RX 单一 owner（决定，待实现）

现状（复核 #2/#8）：`tdma_maintenance_runtime_take()` 内部只有一个静态重组器 `s_rx`，却被三处调用：
- `task_refmem_sync → refmem_cmd_load`（命令面）；
- `task_ota → ota_tdma_binding_service`（OTA）；
- SCPI 快照命令（诊断）。

三者在同一 core0 上以任务/命令上下文并发，谁先 `take()` 谁拿走整帧，导致“对方的帧被丢弃/被 NACK”。同时目标板在本地没有 stream 会话时根本进不到 OTA 服务门（复核 #2），因此现在的绑定无法 bootstrap。

**决定（v1.1）**：把 `take()` 收敛为**唯一 drainer + 按 stream_id 分发**：
1. runtime 只提供一个 `tdma_maintenance_runtime_service()`：一次性把适配器里的可用 chunk 收进重组器，完成的帧压入**有界完成队列**（建议 4 项，堆分配，避免再撞 RAM 上限）；
2. 各消费者改为注册 sink：`register_sink(stream_id, fn, ctx)`；REFMEM 用命令 stream，OTA 用 `0x4F`，SCPI 只读计数不再 take；
3. `take()` 保留为“兼容取帧”，内部改为从完成队列弹出，禁止再直接碰适配器（消除双 drain）；
4. 目标板服务门改为“runtime ready 即服务”，不再依赖本地 stream 会话。

**验收判据**：三方同时活动时零丢帧（构造 refmem 命令 + OTA 帧并发注入的 host 用例）；目标板在无本地会话时可被 OPEN 打开。

**落地方式（2026-10-30 修订）**：采用平台既有的**二等席解析器注册**扩展点，而不是新增完成队列：
- `tdma_second_class_parser_register(0x4F, ota_parser)`；REFMEM 命令任务仍是 `take()` 的唯一 drainer，收到 `0x4F` 即分发到 OTA 处理器（不再有第二个 `take()` 调用者）；
- OTA 处理器按角色分发：relay 活动 → relay 会话（ACK 回程），否则 → target 会话（OPEN/DATA/CLOSE）；
- 帧级 ACK 仍由 REFMEM 的既有流程按 `accepted` 回写，OTA 不另立 ACK 事实；
- 目标服务门因此不再依赖本地 stream 会话：只要环上有 `0x4F` 帧，就由 REFMEM 任务送入 OTA 处理器。

### 4.4 N2 bring-up 配方与部署前提（2026-10-30 实测）

**起环配方（已验证可跑，复用 P0 工具函数而非新写）**：
1. `apply_profile(board)`：`RING:STOP` → 等 stopped → `OPMode:STAGe 7` → `_apply_stopped_opmode` → `CALibration:TOPology:PROBe 1,<cycles>`（拉起校准 persona）；
2. `configure_pair_topology(board, slot)`：清错误队列、`VDC:FEEDback:SESSion?==0`，再 `_set_stopped_topology((2,slot,0))`；
3. `start_pair(driver(slot0), receiver(slot1))`：receiver-first `ARM`（`ARM:STATus?==1`）→ `_wait_pair_state(started=True)` → START。
   实测结果：两板 `enabled=1, node_count=2, adapter_started=1, down_running=1`，slot0 侧 TX 计数持续增长 —— **环真正 running**。

**部署前提（阻断 N2）**：`SYST:OTA:STREAM:CAP?` 在台架四板上返回 `4096,0,1`（stream 能力=0、map=v1_compat）。
即当前台架没有 V2 durable-checkpoint stream 传输，而 TDMA 流式 OTA 的会话/durable ACK 全部建立在 stream 会话之上：
- 选项 A：把台架迁移到 **V2 部署映射**（factory migration，属部署变更，需要单独验收）；
- 选项 B：为 v1_compat 设计一条无 durable checkpoint 的 TDMA 路径（与“durable ACK”目标冲突，需重新评审 G1 判据）。

在 (A)/(B) 决策前，N2 不能执行；这不是 OTA 代码问题，而是**部署前提缺失**。

**补充事实（2026-10-30 现场验证）**：
- 直接在 v1 板上发 `STREAM:OPEN`（caps=3、map=1、partition 由 `app_partition_id(1,1)` 计算）被固件以 `-200` 拒绝；`STREAM:STAT?` 保持 `last_result=5`、状态 0。即 **v1 固件结构上无法建立 stream 会话**，不是 host 工具的策略限制。
- V2 不是普通构建开关：`CMakeLists.txt` 要求 `PROJECT_FLASH_DEPLOYMENT_MAP=v2_candidate` **且** `PROJECT_FACTORY_MIGRATION_BUILD=ON`，并要求源状态保持 `FLASH_MAP_DEPLOYMENT_STATE=target_not_deployed`、`PROJECT_OTA_DEFAULT_BOOT_MODE=DIRECT_AB`；普通 release/live 构建被强制留在 v1_compat。
- 因此选项 A 是一次**工厂迁移项目**（迁移构建 + 迁移包 + 验收），不是测试时的临时切换。

## 5. 帧草案（v1，承载于 maintenance stream）

| 帧 | 字段 | 复用/新增 |
|---|---|---|
| `OPEN` | `session_id`、`generation`、`map_version`、`partition_id`、`destination_slot`、`object_id`、`total_size`、`package_crc32`、`identity`、`package_hash` | 复用 `pota_stream_open_decode_le()`（`POTA_STREAM_OPEN_WIRE_SIZE`） |
| `DATA` | `offset`、`len`、`crc32`、payload | 新增 |
| `ACK` | `durable_offset`、`result` | 新增；`result` 复用 `pota_stream_ingress_result_t` |
| `CLOSE` | `final_offset`、`crc32`、`result` | 新增 |

约束：不新增 payload class、不新增物理总线、不改 command slot；帧总长受既有 maintenance 帧上限约束（`TDMA_MAINTENANCE_STREAM_MAX`，由 chunk 常量派生），超限必须在 OPEN 阶段拒绝，不得截断。

**v1 实际布局（`ota_tdma_codec.h`，2026-10-30）**：帧头 4 B（type/flags/seq/total），帧上限 80 B（= `TDMA_MAINTENANCE_STREAM_MAX`）。`OPEN` 承载 88 B 描述符，按 76 B 分 2 片（乱序可重组、重复即重置）；`DATA` = offset/u32 + crc32/u32 + payload ≤ 68 B；`ACK` = durable_offset/u32 + result/u8 + reason/u8（10 B）；`CLOSE` = offset/u32 + crc32/u32（12 B）；`ABORT` = reason/u8（5 B）。编码器容量不足或参数非法一律返回 0（fail closed，不截断）。

### 5.1 CLOSE 确认（2026-10-30 定稿）

源在末块 ACK 后发送 `CLOSE`，但**不再立即报 READY**：进入 `WAIT_CLOSE_ACK`，只有收到目标回的 `ACK(offset == image_size, result == 0)` 才 `DONE`；超时按块级重试预算重发 `CLOSE`，用尽则失败。目标在 `CLOSE` 时先 flush 暂存、关闭会话，再用 durable 编码该 ACK 回发。

意义：`READY_TO_REBOOT` 只表示“目标已确认收完”，不是“协调者发完”。host 的 relay 背压也以目标 durable 为准（每块等 `STREAM:STAT?`）。

## 6. 状态机

- 目标侧：`pota_stream_state_t`（`IDLE → OPEN → RECEIVING → … → READY_TO_REBOOT`）**原样复用**，环侧只做 pass-through，不新增第二状态机。
- 协调者侧：`IDLE → SENDING → WAIT_DURABLE → SENDING … → CLOSED`；任何 `FAILED/ABORTED` 立即停止发送，不得继续灌帧。
- 失败转移：`park 超时 / ACK 超时 / CRC 冲突 / offset 不连续` → 目标侧 `ABORTED`，协调者停泵并上报原因。

## 7. 时序与吞吐（待测，不写死数字）

```text
每块耗时 ≈ ACK 往返 + flash 写（program/erase）
整包时间 ≈ 块数 × 每块耗时
```

- 写 flash 主导，因此 stop-and-wait 与窗口化在 2 板规模差别有限；
- N1 必须实测：park 时长分布、ACK 往返、erase 触发频率；
- chunk 大小由 N1 数据决定，**初值取 maintenance 帧上限**，不预设更大。

## 8. 失败与恢复矩阵（设计视角）

| 失败 | 检测点 | 自动动作 | 终止条件 | 证据 |
|---|---|---|---|---|
| 帧丢失/ACK 超时 | 协调者计时 | 退避重发同一帧 | 用尽 `TDMA_MAINTENANCE_MAX_ATTEMPTS` | 帧序号、重试次数、原始应答 |
| 目标 park 超时 | `SYSTem:PROTection:STATus?` | 等待并重读；不重发 | 连续两次超时 | park 状态、elapsed |
| offset 不连续 | 目标 session | 拒绝块并 ACK 当前 durable offset | 同一 offset 两次 | 期望/实际 offset |
| CRC 冲突 | 目标 session | fail closed，不写 Flash | 立即 | crc 期望/实际 |
| 环掉线 | `TDMA:RING:STATus?` | 重读；`ARM→TRAIN→START` | ≤2 次后隔离 | ring 快照 |
| 目标重启/回滚 | 重启后读 build/slot | 只读 boot result，不重刷 | 保留现场 | build/槽位/BCB |
| 串口消失 | 端口枚举 | 按 UID 重扫 | 限时后停 | 枚举记录 |
| 板挂死 | 无应答 | 恢复梯子（picotool app UF2） | USB 不可见 | `recovery.json` |

## 9. C1 / C2 判定表

| 约束 | 设计如何满足 | 证据 |
|---|---|---|
| **C1-1** erase/program 只由 core0 发起 | 目标侧只调 `portable_ota_port_stream_write`，内部走 `FlashTransactionAO`；ingress 不碰 flash API | host 静态断言（禁止 core1 侧 flash 调用） |
| **C1-2** core1 park ACK 有效 | 沿用 `drv_flash_lockout_*`；本设计不改 park 协议 | park 状态与 ack 计数读回 |
| **C1-3** 临界区内无 XIP/USB/SD/LCD | 帧接收在 park 之前完成；park 期间源不发帧（stop-and-wait） | 时序日志 + 停车窗口内无 RX 事件 |
| **C2-1** raw tick 不回写 | 不改任何时间路径 | diff 审查 |
| **C2-2** `int32_t` 回绕安全 | 不新增时间差计算 | diff 审查 |

## 10. 未决问题（N1/N2 必须回答）

1. maintenance 帧上限是否足以承载 2 板可接受吞吐？若不足，G3 才动 long frame。
2. park 期间环是“丢周期后自然恢复”，还是需要显式的 maintenance gate 暂停？两者对协议的影响不同。
3. reference 节点被升级时，环是否还能维持（还是必须先把参考角色迁走）？
4. reboot 后 resume token（hash/map/partition/identity/generation）是否仍有效？
5. 目标在 `READY_TO_REBOOT` 前的 verify 阶段是否需要额外一次读回窗口？

## 11. 与里程碑的映射

| 设计章节 | 对应任务 | 门 |
|---|---|---|
| §3 时序、§9 C1/C2 | `OTA-TDMA-001`、`002`、`003` | G1 |
| §5 帧、§6 状态机 | `OTA-TDMA-004`、`005` | G1 |
| §4 载波、§7 吞吐 | `OTA-TDMA-006a`、`014` | G1 |
| §8 失败矩阵（重启/回滚/resume） | `OTA-TDMA-007`、`009`、`015` | G2 |
| §10 未决问题（reference/多板） | `OTA-TDMA-008`、`010`、`011` | G3 |

## 12. 变更记录

- 2026-10-29：初稿（静态维护态 v1；复用 session/A-B 与 maintenance stream；stop-and-wait；picotool 恢复梯子边界）。

## 13. v2 架构决定（2026-10-31，用户确认）

### 13.1 决定

1. **控制面归 REFMEM**：OTA 的 begin / commit / switch / abort 由 REFMEM 统一控制面发起；
   stream 的 88 B OPEN / CLOSE 不再承担控制语义（过渡期保留 SCPI `SYST:OTA:STREAM:*` 作为
   数据入口，退出条件见 §13.4）。
2. **TDMA 只承载数据**：`0x4F` 维护流只运 OTA 数据块；relay / pump / carrier 保留
   （含“部分窗口续传”与“超时游标续传 + 每 3 次整块重放”）。
3. **目标侧复用既有 AB 流程**：维护帧 → `ota_tdma_v2_bridge` → `ota_ao` 事件 →
   `portable_ota_port_core_begin/write/end`，与 SCPI 本地路径**同一套** flash/BCB/切换逻辑。
4. **退役第二套会话**：目标侧不再以 `pota_stream_session`/`ingress`/`checkpoint` 为主路径；
   过渡期保留 USB 直连 stream 作为对照，完成后按域归档。

### 13.2 依据

- 既有 AB 流程在本机与四板已验证（P3 4/4、历史本机 stream OTA 全链路）；
- stream 路径在目标侧 flash 事务 completion journal 上阻塞（`FLASH_TRANSACTION_ERROR_COMPLETION=22`），
  且叠了第二套会话状态机 + checkpoint + completion lease；
- 台架实测：残留的 RECEIVING stream 会话会挡住 legacy `SYST:OTA:BEGIN`（第二套污染第一套）。

### 13.3 当前符合性（HAOFV 六门 + C1/C2）

| 项 | 判定 | 依据 / 缺口 |
|---|---|---|
| ① 无新增总线/payload/第二命令槽/第二 ACK | ✅ | 仅用 `0x4F` 与既有帧类型；目标只在块消费后回一个 ACK |
| ② 不占 core1 硬实时窗口/席位 | ⚠️ | 未新增席位；但堆两次下调（`config/freertos/FreeRTOSConfig.h`）未用工具复核下限；OTA 依赖低优先级维护窗口，训练下需配额 |
| ③ 单写者/owner | ✅（已修） | adapter maintenance mailbox 双注册已改“首个注册者生效”；bridge/adapter 单实例 |
| ④ fail-closed + reason/证据 | ⚠️ | sink 失败→FAILED、背压/重复忽略、NO_PROGRESS/INVALID_STATE 有 reason；未做 F0–F3 分级 |
| ⑤ 布局/版本/契约同步 | ⚠️ | 未改 flash 布局/DeploymentGate CRC；本契约条目需 C11 复核；build id 不随构建变化（证据不可追溯） |
| ⑥ host + 四板证据 | ❌ | host 49/49；四板传输已打通（chunks 7656 / frames 953），但 durable 未递增、无 P3 凭证 |
| **C1** erase/program 仅 core0 | ✅ | bridge 不碰 flash；写仍由 AO/`FlashTransactionAO` 发起 |
| **C2** 时间事实 | ✅ | 未触碰 raw tick / 时间基准 |

### 13.4 迁移阶段

| 阶段 | 内容 | 验证判据 |
|---|---|---|
| V2-A | 目标侧 bridge（已完成） | host 用例 + BEGIN 被 AB 流程接受 |
| V2-B | 控制帧/REFMEM 驱动 BEGIN/END/ABORT（部分完成） | abort 回 IDLE；payload 白名单登记 |
| V2-C | N2 单跳闭环 | 目标 `READY_TO_REBOOT`、durable 单调、环 `tx≈rx` |
| V2-D | 退役旧 stream 目标路径（保留 USB 直连） | 回归用例 + P3 |

**控制面退出条件（SCPI → REFMEM）**：V2-C 判据通过且 REFMEM begin/commit/switch 覆盖
`STREAM:OPEN/CLOSE` 的全部语义后，删除 SCPI 的 relay 分支（保留只读诊断）。

### 13.5 契约与待办

- `ARCH-OTASTREAM-01`（pending）描述“所有传输共用 OtaStreamSession、ACK 只确认 durable offset”；
  `ARCH-OTASTREAM-02` 已登记为 **pending**，只取代其 TDMA 承载与控制语义子句，其余五类本地
  transport 与 durable 语义仍由 01 覆盖；**01 保持 pending、暂不 superseded**（避免“旧契约已废、
  新契约未生效”空档）。
- **C11 复核（2026-10-31）**：有条件通过，结论与逐项处置见
  `docs/check/submissions/ARCH_OTA_CROSS_REVIEW_01.md`。转 active 的前置：
  ①条款已限定 `0x4F`、改“不再作主路径、按 V2-D 分期退役”、补过渡期 SCPI 入口与 §13.4 退出条件；
  ②`code_anchor` 改为真实落点（`ota_fb.c`）；③durable 递增 + 覆盖当前源码指纹的 P3；
  ④`HAOFV_FLASH_ARCHITECTURE.md` §7.2/§7.4 与本表同步。
- 待办：①维护流承载配额（训练下块收不齐）；②build id 每次构建必变（已实现，待 P3 提交）；
  ③未提交源码（含 v2 桥、pump、port、TDMA 适配器）的 P3 同指纹凭证。
- 2026-10-31：新增 §13（v2 决定、符合性、迁移阶段、契约待办）；标题与状态更新为 v2 Active；
  登记 `ARCH-OTASTREAM-02`（pending）并完成 C11 复核落档。
