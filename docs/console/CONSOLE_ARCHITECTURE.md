# 运维与调试控制面（CONSOLE）主域架构

Status: Active
Domain: CONSOLE
Canonical: `docs/console/CONSOLE_ARCHITECTURE.md`
Related: `docs/README.md`, `docs/interface/SCPI_HOST_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`, `docs/ota/OTA_HAOFV_ARCHITECTURE.md`, `docs/tools/DOCS_TOOLS_ARCHITECTURE.md`, `docs/console/CONSOLE_TK_ARCHITECTURE.md`
Last updated: 2026-11-04

## 1. 定位

CONSOLE 是面向操作员、维护人员和联调人员的**产品级运维/调试控制面**。它把分散在各主域的 bring-up 流程、状态回读、证据和危险操作收敛为一个可审计的应用入口，而不是再定义一套固件语义。

本域的第一个交付物是**调试 Demo**：单端口（串口或 USB TMC）连接协调板，GUI 与本地 TCP 共用同一会话，覆盖 REFMEM 统一控制面、node-sequence 应用面、常规 OTA 与证据签发。后续在同一入口下增加 T2、TDMA 启环、DPLL/VDC、校准训练和 REFMEM 装载。

**交付物路径（当前）**：`tools/domains/trigger/trigger_debug_demo/`。  
**目标路径（迁移待办）**：`tools/domains/console/debug_console/`；迁移前不删除旧路径，按仓库归档规则处理。

## 2. 域边界

| 属于 CONSOLE | 不属于 CONSOLE |
|---|---|
| GUI、CLI、本地 TCP 行协议 | 固件状态机、PIO/DMA/Flash/OTA owner |
| 串口 / USB TMC / TCP transport 抽象 | SCPI 命令定义与命令表事实源 |
| 命令目录、参数编码、字段解析 | 各域的时序/校准/调度语义 |
| 流程引擎：前置条件、顺序、超时、重试 | P3/T2 验收判定本身 |
| 每步证据与凭证（OTA/flow receipt） | 发布签名、工厂密钥 |
| 维护模式、RUN 态门禁、owner/lease 检查 | 资源仲裁和 core1 实时预算 |
| 模拟器、host 测试、打包与版本矩阵 | 硬件设计、板级约束 |

依赖方向必须单向：

```text
CONSOLE
  ├─ consumes SCPI / CTRL:EXECute / REFMEM command plane
  ├─ consumes READ:* / *_STATus? evidence pages
  └─ emits local TCP protocol + receipt JSON

interface / trigger / tdma / vdc / calibration / refmem / ota / storage
  └─ 不反向依赖 CONSOLE
```

CONSOLE 不能成为第二套 command slot、第二套 ACK 事实或第二套 active 配置 owner。

## 3. 子域划分

| 子域 | 职责 |
|---|---|
| Transport & Session | serial / USB TMC / TCP，单写者 Dispatcher，GUI/TCP 共享会话 |
| Command Catalog | node-sequence、T2、REFMEM、OTA 的命令、参数编码与字段表 |
| Flow Engine | StepSpec、前置状态、目标板、期望回读、超时/重试、顺序门禁 |
| Flow Packs | T2、TDMA 启环、DPLL/VDC、校准训练、OTA |
| Evidence & Receipt | 每步结果、PASS/FAIL、源码/镜像/板 ID 指纹、`.sha256` 旁路 |
| Safety & Permission | 维护模式、RUN 写拒绝、销毁性操作二次确认、owner/lease |
| UI & Packaging | 页面/子页、模拟器、host 测试、版本矩阵、发布包 |

## 4. 当前交付物能力

当前调试 Demo 的 tab 由 `DemoApp._build` 手工构建；`catalog.py` 的 `MODULES` 只覆盖功能模块，**不含 OTA/TCP**，因此它不是页面集合的事实源。后续用 `PAGES` 表统一（`CONSOLE-028`），当前已包含：

- 总览、运行控制、触发配置、角度、开关、序列、状态回读
- 板卡与 ACK（target/required mask、聚合与逐板 ACK）
- 统一控制面（`CTRL:EXECute`、目录、OWNer、原始命令帧；长度以 `device.py` 校验常量为准）
- OTA（常规单板 BEGIN/PBEGIN/DATA/END/BOOT/COMMIT，自动签发凭证）
- TCP 透传（本地 127.0.0.1 行协议，GUI/TCP 共用 Dispatcher）
- 诊断与原始命令

尚未交付：REFMEM 装载/表/质量页、T2 表安装与启动向导、TDMA 启环、DPLL/VDC 启动、校准训练。这些属于 `docs/console/CONSOLE_DOMAIN_TODO.md` 的 P0/P1。

## 5. 契约候选

以下契约尚未登记，冻结前不得作为外部承诺：

| 候选 ID | 内容 | 状态 |
|---|---|---|
| `CONSOLE-SESSION-01` | 单端口/单会话/单写者；GUI 与 TCP 共享 Dispatcher | 候选 |
| `CONSOLE-TCP-01` | 本地 TCP 行协议：一行一条 SCPI，一行响应，`BUSY OTA` 语义 | 候选 |
| `CONSOLE-RECEIPT-01` | OTA/flow receipt schema、字段、`receipt_sha256`、`.sha256` 旁路 | 候选 |
| `CONSOLE-FLOW-01` | 流程步骤 ID、前置条件、证据字段、允许的 transport 模式 | 候选 |

登记前必须走 C11 独立审核；本文不把候选写成已冻结事实。

## 6. 单端口目标与过渡期路径

- **最终目标：所有流程单端口**。GUI、TCP、OTA、T2、TDMA、DPLL、校准、REFMEM 都共用同一个串口或 USB TMC 会话。
- 单端口跨板写操作必须走 REFMEM 统一控制面（`SYSTem:CTRL:EXECute` / `SYSTem:CTRL:FUNCtion:RUN`）；新增功能默认按单端口设计，不得先做多端口再等以后收敛。`target_transport` 枚举仍是候选契约 `CONSOLE-FLOW-01`。
- **单端口可用前提（硬 gate）**：带远程 target 的 REFMEM 命令要求 `distributed_refmem_ring_transport_ready()` 为真；该谓词的 ring 部分是 `ring.enabled && adapter_started && config_seq == applied_config_seq`（`refmem_control_submit.c:68-75`、`refmem_position_reset_policy.c:15-21`、`distributed_refmem_plane.c:736-738`）；`CONSOLE-035` 落地后语义扩展为 **ring ready 或 bootstrap carrier ready**（§6.1）。两者都不成立时命令被**直接拒绝**，不会排队等待。
- 因此 ring 未 ready 前只有三条合法路径：
  1. 只对本地 target 下命令（单板）；
  2. 显式维护多端口；
  3. 固件 control-transport 准入：准入/路由接口已落（`CONSOLE-035`：ring 或 carrier，其余 fail-closed），carrier 本体（adapter 所有权迁移 + relay）见 `CONSOLE-017`；产品线序是否覆盖见 §6.1 的 profile 边界。
- **低速承载不等于 ring ready**：它只是另一条被显式准入的承载通道；carrier 未注册或未握手前，不能宣称“单端口可建环”。
- 命令面必须按**三层口径**判定（本地 SCPI / REFMEM 类型+目录 / 目标 handler），不能把“本地有”写成“单端口可用”；矩阵见 §6.3。
  - **已有本地 SCPI、Console 未封装**：`LOAD:BOARD/NODE/SD`、`LEASE:TAKE/RELEASE`、`DPLL:ROLE/TUNE/OVERRIDE`、`VDC:REFERENCE/OUTPUT:TIMING/DELAY/RUN/STOP`、`T2:CONFIGURE/MOUNT/UNMOUNT`；
  - **本地 SCPI 有、REFMEM 承载层缺失**：`LOAD:ACTIVATE`、`TABle?`、`QUALity?`、`RING:ARM/TRAIN/START`、`OPMode`、`VDC:FEEDBACK/PRIORITY/LOCK:READINESS`、T2 表安装、校准 persona/stage。
- 过渡期多端口必须登记：当前/目标 transport、阻塞命令、迁移任务、退出标准（单端口回读 + ACK 证据）。UI 必须显示 `过渡 · 多端口`、占用端口和退出条件。
- `ring_not_ready` 时，runner 允许 `maintenance_multiport` 并标记 `reason=RING_NOT_READY`，不得把它当成长期路径；ring ready 或 bootstrap 准入通过后，默认 workflow 必须回到单端口。
- `CONSOLE-017/018/035` 是 §6.2 所有行的**共同前置**，不是并列可选项；单端口 wrapper 通过后，多端口路径从默认 workflow 退役。

### 6.1 引导期低速 SPI 承载（bootstrap transport）

校准、T2 与 TDMA 建环之前，四板没有可用的高速数据面。为了让 REFMEM 统一控制面先工作，引导期需要一条低速控制链路，承载 REFMEM command/ACK/NACK/fragment/table/status。**该链路不新增 TDMA persona program**：复用 REFMEM 已有的 PIO SPI 物理 adapter（`refmem_spi_physical`，PIO/引脚由 `BOARD_REFMEM_SPI_*` 给出，baud 由调用方传入），在 STOP/维护边界重新 arm。链路与状态：

```text
STOP all
  → 释放 SYNC_IO 调试组（pio0 指令空间/SM 归还）
  → REFMEM bootstrap owner: arm(MASTER/SLAVE, low baud, profile pins)
  → 逐 target 握手（同一 REFMEM frame/CRC/seq 语义）
  → 校准、拓扑、时钟训练
  → handover: bootstrap disarm → TDMA persona LOADING → ACTIVE
  → 回读 ring/adapter/cursor，确认高速链路
  → 失败则回退：ring STOP → bootstrap re-arm（恢复/取证）
```

**依据（先例，不是本轮新设计）：**

- `docs/refmem/REFMEM_DOMAIN_TODO.md` 的 P4.5 阶段 2 已在两板最小系统上用真实 PIO+DMA 物理链路完成 `RAW/HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 双向闭环。
- `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_05.md` 的 `REFTMEM-TASK-20260815-013` 在 TDMA HIL 通过后删除了 `SYSTem:REFMEM:SYNC:SPI:*` 的帧级阻塞命令，只保留 `ARM/DISarm/STATus?/LINE:*/RAW:*`，目的正是禁止 core0/SCPI 直驱 frame transport。本方案**不恢复**帧级 SCPI 直发。
- 因此 carrier 的 owner 是 REFMEM 的 core0 bootstrap service；SCPI 只做 arm/status，Console 只通过 REFMEM 命令面与回读页观测。

**profile 边界（必须先说清）**：`BOARD_REFMEM_SPI_*` 属于 P4.5 最小系统/调试 profile（`board_config.h` 注明它 overlaps the debug SYNC_IO groups，且只能在 RefMem SPI HIL 占有期间 arm）。产品四板链路的物理引脚是另一组（TDMA persona 的 BiSS/RJ45 组），两者不是同一套线序：

- **最小系统/调试台**：可直接复用该 adapter 作为建环前控制面；前提是 STOP 且 SYNC_IO 调试组已释放。这是本轮可执行的路径。
- **产品四板台**：产品线序上没有现成的建环前控制链路。要达到同一目标，需要（a）产品线序提供控制对，或（b）在 TDMA persona 引脚上新增低速控制 mode（即原 `CONSOLE-017` 的 persona 工作量，受 PIO 指令空间约束）。该选择由 TDMA/RefMem/校准三方 owner 决定，Console 不能单方面假定。

**准入与路由（`CONSOLE-035`）**：

- 远程命令 gate 仍是 `distributed_refmem_ring_transport_ready()`；语义扩展为 **ring ready 或 bootstrap carrier 已注册且握手 ready**，取或，其余 fail-closed。
- 路由由 `refmem_control_transport_route()` 判定：ring 优先；ring 不在时，只有 carrier 的 `submit` 非空且 `ready()` 为真才允许；否则拒绝，不排队、不自动降级。
- readiness 是 carrier 的运行时事实（握手/计数/超时），**不是** SCPI `ARM` 的成功返回；`ARM` 只准备物理层。
- 同一时刻只有一个控制 transport active；切换必须走 STOP 边界并留证据。
- **交接**：ring 变为 ready 时，core0 服务自动 `handover`（撤销 readiness 并 disarm adapter），记录 `RING_HANDOVER` reason；ring stop 后**不自动 re-arm**，需操作员/Console 显式 arm，避免两条路径来回切。

**半双工约束（代码实测）**：`refmem_spi_physical_adapter_transmit()` 要求 `MASTER`，`receive_begin/receive_poll` 要求 `SLAVE`，角色只在 arm 时固定。因此 bootstrap 的“握手”不是常驻双向连接，而是 **TX 窗口(MASTER) → 角色切换 → RX 窗口(SLAVE)**；对端必须在 guard 之后应答，readiness 以**收到对端帧**为准，不以 ARM 成功或本地 TX 成功为准。多 target 需要逐 link / 逐窗口会话；当前先按两板点对点实现，四板拓扑另议。

**当前接入面**：`SYSTem:REFMEM:SYNC:SPI:ARM/DISarm/STATus?`（只准备/撤销物理层与诊断，不直发 REFMEM 帧）与 `SYSTem:REFMEM:BOOTstrap:STATus?`（armed/ready/state/peer、timeout/retry/invalid/build_fail/tx_fail 计数、last_reason）。**carrier 已注册**：`bootstrap_start` 注册，`ready` 门控命令准入；命令帧/reply 已绑定到 relay envelope，reply 超时以 `TIMED_OUT/TIMEOUT` fail-closed。**镜像开关**：`PROJECT_REFMEM_BOOTSTRAP_ENABLED` 默认 OFF；ON 镜像已可链接（RX DMA 收缩 −888B 后余量 **1252B**，OFF 1504B），默认启用仍待 HIL。**HIL 现状**：NO.1/NO.2/NO.4 已刷 ON 镜像并 ARM 成功；但 `two_board_io_validate` 对 NO.1↔NO.4 与 NO.1↔NO.2 均报零连通（IN GPIO20..23 / OUT GPIO16..19，mask 全 0），16..19 与 TDMA 24..29 映射下 physical rx 均为 0，链路尚未承载任何字节。

低速承载**不会自动让** `distributed_refmem_ring_transport_ready()` 为真：该 gate 只在 ring ready 或 carrier 已注册且握手 ready 时为真。准入/路由接口按 `CONSOLE-035` 落地；carrier 本体（adapter 所有权迁移 + core0 relay）见 `CONSOLE-017`。未落地前，环未 ready 时只能走维护多端口。

约束：

- **只在 STOP/维护边界切换**。RUN 中禁止改变 PIO 归属、GPIO route 或时间片动作表（`ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS` §6）。
- **不是第二条总线、第二个命令槽或第二个 ACK 事实**。低速承载的仍是同一条 REFMEM 命令/ACK 语义；CRC、seq、fence、quality、ACK/NACK 含义不变，同一时刻只有一个 command slot。
- **PIO/引脚仲裁**：REFTMEM SPI adapter 用 `pio0` + 最小系统/调试组引脚（`BOARD_REFMEM_SPI_*`），**与 TDMA ring 的 `pio1/pio2` 及产品环引脚不共享指令空间/引脚**；真正冲突的是 `pio0` 上的 SYNC_IO 调试组。产品台若复用该 adapter，需要产品线序提供控制对（当前没有），不需要改 TDMA persona。
- **fail-closed + 可回滚**：握手超时、readiness 丢失或回读不一致时立即撤销 readiness 并拒绝新的远程命令；保留 reason、tx/rx/timeout/retry 计数与 build/fingerprint 证据。
- **低速只是引导与恢复通道**：高速 TDMA 建立并验证后应退役或只保留恢复入口，不作为产品数据面。
- 速率、MTU、DMA/SM 预算用符号和资源工具核算并写入 receipt，不在文档写死。

### 6.2 单端口路径优化登记

每个尚未单端口的流程都必须在 §6.2 保留一行登记；没有目标 transport、阻塞项和退出标准的步骤不得进入默认 workflow。**§6.2 是登记源**，`CONSOLE-026` 只维护其执行状态，不另存一份事实。

| 流程 | 当前 transport | 目标 transport | 主要阻塞（命令面 / Console） | 迁移任务 | 退出标准（单端口回读+ACK） |
|---|---|---|---|---|---|
| REFMEM 装载/激活/表/质量 | 多端口 | 单端口 REFMEM | `LOAD:BOARD/NODE` 有类型+handler；`LOAD:SD` 类型在但目标 handler 未注册；`LOAD:ACTIVATE` 无 REFMEM 类型；`TABle?/QUALity?` 仅本地查询 | CONSOLE-005/026 | LOAD/ACTIVATE/TABLE/QUALITY 全链路单端口 ACK + 回读 |
| T2 表安装 | 多端口 | 单端口 REFMEM | `T2:CONFIGURE/MOUNT/UNMOUNT` 有类型+handler；`TEMPLATE/DEPLOYMENT/GRID/ACTION/COUNTER` 表安装只有本地 SCPI，无 REFMEM 类型（还需 table package 载荷决策） | CONSOLE-008/021 | 表安装 + T2 config/mount 单端口 ACK，表 generation/CRC 回读一致 |
| TDMA 启环 | 多端口 | 单端口 REFMEM | `TOPOLOGY/SEAT/PERIOD/LOAD_MASK` 有类型+handler；`OPMode`、`RING:ARM/TRAIN/START` 只有本地 SCPI，无 REFMEM 类型 | CONSOLE-010/022 | 启环每步单端口 ACK + ring/arm/train status 回读 |
| DPLL/VDC 启动 | 混合 | 单端口 REFMEM | `ROLE/TUNE/OVERRIDE`、`OUTPUT:TIMING/DELAY/RUN/STOP` 有类型+handler；`FEEDBACK/PRIORITY/LOCK:READINESS` 只有本地 SCPI，且流程未冻结（HAOFV_VDC_DPLL §11） | CONSOLE-011/023 | output/feedback/priority/lock 全链路单端口回读 |
| 校准训练 | 多端口 | 单端口 REFMEM | CAL 类型未实现/未注册；stage 语义未冻结（`CALIBRATION_DOMAIN_TODO`）；建环前还依赖 §6.1 的低速承载 | CONSOLE-012/024 | stage/link + persistence 单端口回读，失败 trial 证据齐全 |
| 常规 OTA | 单端口顺序 | 单端口顺序 | 单板直连已具备；跨板远程仍受 ring-ready gate 约束 | 已具备（单板） | 单板顺序 OTA + receipt；远程 OTA 需 ring ready |
| TDMA 流式 OTA | 单端口 relay | 单端口 relay | 固件开发中 | CONSOLE-033 | durable wait/close/boot + receipt 可复跑 |

### 6.3 命令面三层口径（§6.2 的判定依据）

“命令存在”不等于“单端口可用”。每一步必须三层同时成立：

1. **本地 SCPI 入口**：该板自己的命令表里有 handler；
2. **REFMEM 命令面**：`refmem_command_type_t` 有类型、`config/refmem/command_catalog.yaml` 有目录行；
3. **目标 handler**：目标板在 `app_refmem_domain_config_init()` 注册了 handler，且 dispatch 的 idle/run 门禁允许。

任一缺失，单端口远程就断；Console 不得用 raw SCPI 转发绕过（§7.12）。

当前矩阵（快照，以代码/目录为准）：

| 流程步骤 | 本地 SCPI | REFMEM 类型+目录 | 目标 handler | 单端口 |
|---|---|---|---|---|
| LOAD:BOARD / NODE | ✅ | ✅ | ✅ | ✅（受 ring/bootstrap gate） |
| LOAD:SD 表包 | ✅ | ✅ | ❌ 未注册 | ❌ |
| LOAD:ACTIVATE | ✅ | ❌ | ❌ | ❌ |
| TABle? / QUALity? | ✅ | —（查询） | — | ❌ 只能逐板读 |
| RING:TOPology/SEAT/PERIod/LOAD:MASK | ✅ | ✅ | ✅ | ✅（需 ring ready） |
| RING:ARM / TRAIN / STARt | ✅ | ❌ | ❌ | ❌ |
| OPMode | ✅ | ❌ | ❌ | ❌ |
| DPLL ROLE/TUNE/OVERRIDE | ✅ | ✅ | ✅ | ✅ |
| VDC REF/DELAY/TIMING/STOP/START | ✅ | ✅ | ✅ | ✅ |
| VDC FEEDBACK/PRIORITY/LOCK | ✅ | ❌ | ❌ | ❌ |
| T2 CONFIGURE/MOUNT/UNMOUNT | ✅ | ✅ | ✅ | ✅ |
| T2 表安装（TEMPLATE/GRID/DEPLOYMENT/ACTION/COUNTER） | ✅ | ❌ | ❌ | ❌ |
| 校准训练 | ✅ | ❌（枚举遗留类型未实现） | ❌ | ❌ |

结论：建环前单端口受限的根因不是“SCPI 没实现”，而是**承载层与目标 handler 缺失 + ring-ready 循环依赖**；因此先做 §6.1 的 bootstrap carrier 与 `CONSOLE-035` 准入，再逐类补类型/目录/handler。

## 7. 工作流约束（Workflow-first）

**长期流程不得以“按钮 + 线程 + 内存状态”实现。** 满足以下任一条即必须定义为 workflow：

- 多步，或需要等待设备/重启/人工确认；
- 需要中断、恢复、重试或跳过；
- 跨多个板卡/角色；
- 写 active 配置，或产生凭证；
- 依赖前置状态（STOP、cursor、calibration、ring、lease）。

判定阈值由 Console 的 `WorkflowPolicy` 统一定义（步骤数、时长、可恢复性、跨板数），不在文档里写死；UI 不得各自解释。

### 7.1 WorkflowSpec

每个 workflow 至少声明：

| 字段 | 含义 |
|---|---|
| `id` / `version` | 稳定标识与版本；与固件 build、命令目录版本做兼容矩阵 |
| `inputs` | 操作员输入、镜像、板卡目标、transport 模式 |
| `preconditions` | STOP/RUN、ring、cursor、calibration、lease、build 匹配 |
| `steps` | 有序步骤；每步有 id、标题、动作、期望回读、超时/重试、失败策略 |
| `transport` | `single_port_refmem` / `maintenance_multiport` / `offline`；必须显式 |
| `receipt_schema` | 每步证据与最终凭证的 schema |

每个 Step 至少声明：`id`、`precondition`、`action`、`expect`、`timeout/retry`、`evidence`、`on_failure`（abort / rollback / skip / manual）。

### 7.2 形式与定义

**Workflow 是可版本化、可恢复、可取证的多步设备操作状态机。** 它不是脚本、宏或“大按钮”；它是控制台执行长期流程的唯一合法形式。

```text
WorkflowSpec（声明式定义）
        ↓
Planner（前置检查 / 展开步骤）
        ↓
WorkflowRunner（单 writer 执行）
        ↓
State Store（out/workflows/*.json）
        ↓
Evidence / Receipt（每步 + 最终凭证）
        ↓
GUI / CLI / headless / TCP（只是视图）
```

### 7.3 步骤模型

每个 Step 至少声明：`id`、`precondition`、`action`、`expect`、`timeout/retry`、`evidence`、`on_failure`、`idempotent`。

| 步骤类型 | 用途 |
|---|---|
| `command` | 单条 SCPI / `CTRL:EXECute` 写操作 |
| `query` | 只读查询与字段断言 |
| `wait` | 轮询状态直到谓词成立或超时 |
| `manual` | 等待操作员确认（接线、换板、按 BOOT） |
| `subworkflow` | 复用子流程（如 TDMA bootstrap 子流程） |
| `foreach` | 对已声明、有上限的集合迭代（OTA 分块、逐板命令） |
| `parallel` | 仅允许独立只读/无写冲突的并行组，必须带 barrier |
| `evidence` | 写 receipt / 摘要 / 日志哈希 |
| `compensate` | 失败/取消时的安全收尾（STOP/ABORT/释放 lease） |

`foreach` 不是通用循环：必须有上限和可恢复 cursor；禁止在 workflow 中执行任意代码。

状态机：

```text
CREATED → PRECHECK → RUNNING
                         ↔ WAITING（设备/人工）
                         ↔ PAUSED
                       → ABORTING
                       → SUCCEEDED / FAILED / CANCELLED
```

### 7.4 能力范围

**能做：**

- 多步编排：命令、查询、等待、人工确认、子流程、有界迭代。
- 前置门禁：STOP/RUN、ring、cursor、calibration、lease、build/命令目录版本。
- 恢复：从最后成功步骤继续；恢复前重新校验前置/版本。
- 取消/中止：在步骤边界安全退出，执行 compensate。
- 重试：仅幂等步骤；非幂等必须人工决策。
- 证据：每步结果、原始响应、reason、hash；最终 receipt。
- 多 transport：目标态是单端口 REFMEM；维护多端口仅作为过渡路径；workflow 显式声明 `target_transport` 与可选的 `transitional_transport`。
- 多入口：GUI、CLI、headless；TCP 默认只读状态，写操作需显式开启。
- 版本矩阵：workflow version ↔ firmware build ↔ command catalog ↔ receipt schema。

**不能做：**

- 不能绕过固件 owner/lease/RUN 门禁。
- 不能定义新的 SCPI 语义；命令必须来自命令目录/契约。
- 不能替代 TDMA/VDC 的实时调度。
- 不能替代 P3/T2 验收判定；receipt 是证据，不是资格。
- 不能隐藏多端口；多端口必须显式声明并显示。
- 不能执行任意 Python/YAML 代码；只允许注册过的 action 类型。
- 不能并行写同一个 owner/同一板；并行只允许无冲突只读。
- 不能把 GUI 内存状态当唯一状态；页面关闭后 workflow 仍可恢复。

与其他形式的关系：

| 形式 | 特点 | 适用 |
|---|---|---|
| 命令卡 | 单条命令/查询 | 调试、探索 |
| 宏/Macro | 固定命令序列，无状态/证据 | 临时重复操作 |
| 脚本 | 命令式，难恢复/难审计 | 一次性维护 |
| Workflow | 声明式、可恢复、有门禁/证据 | 长期流程、生产运维 |
| 验收工具 | 独立判定/凭证 | P3/T2/DPLL 验收，不塞进 workflow |

### 7.5 流程能力矩阵

目标态均为单端口；括号内是当前过渡能力，必须按 §6.2 的迁移登记收敛。

| 流程 | 典型步骤 | transport | 恢复单位 | 人工门 | 证据 |
|---|---|---|---|---|---|
| 常规 OTA | snapshot→begin→data→end→wait→receipt→boot/commit | 单端口顺序 | chunk / END | BOOT、COMMIT | OTA receipt |
| TDMA 流式 OTA | open→data→durable wait→close→boot | 单端口 relay | durable offset | BOOT | stream receipt |
| T2 停止态 | STOP→OPMode→topology→clear→grid→deploy→template/action→counter→T2 config/mount | 单端口（当前过渡多端口） | step / table | MOUNT、START | T2 receipt |
| T2 运行 | VDC→priority→ring→cursor→PWM→sample→teardown | 单端口（当前过渡多端口） | step | START、STOP | 运行证据 |
| TDMA 启环 | bootstrap→OPMode→topology→TRAIN→ARM→START→status | 单端口（当前过渡多端口） | step / board | ARM、START | ring evidence |
| DPLL/VDC | role→tune→provisional→feedback→output→priority→lock | 单端口（当前过渡多端口） | step | RUN、RELOCK | lock/quality evidence |
| 校准训练 | BIAS→MARK→SCK→DATA→CLK→STAGE→ACTIVE | 单端口（当前过渡多端口） | stage / link | SAVE、COMMIT | 矩阵/trial evidence |
| REFMEM 装载 | stage→activate→table→quality | 单端口（当前过渡多端口） | table | ACTIVATE | table/quality evidence |

### 7.6 成熟度与第一版边界

| 级别 | 能力 | 当前 |
|---|---|---|
| L0 | 命令卡/宏，无状态 | 当前大部分页面 |
| L1 | 线性 workflow，step 恢复，receipt | 目标第一批（OTA），当前未达 |
| L2 | 多板/parallel/barrier，transport 切换 | T2/TDMA/DPLL |
| L3 | 版本矩阵、HIL 证据、SUPPORTED | 收口后 |

**v1 只做线性 workflow：**

- 顺序步骤、step cursor、JSON 状态、resume/cancel、receipt。
- 只允许 `command/query/wait/manual/foreach/evidence/compensate`。
- 不做 parallel，不做超过 1 层的嵌套。
- 目标态单端口 REFMEM；ring 未 ready 时 `maintenance_multiport` 是唯一合法路径，必须带 `reason=RING_NOT_READY` 并在 UI 标记过渡。
- 第一个迁移对象：常规单板 OTA；第二个：T2 停止态 + 运行。

**现行例外登记**（C1 未完成前不得把 §10 的全量 workflow 断言当已满足）：

| 流程 | 当前形态 | 不满足条款 | 迁移任务 | 失效时点 |
|---|---|---|---|---|
| 常规 OTA | `OtaPage` ad-hoc 线程 + `after` 收尾 | 无 cursor/resume；§7.12 禁止 ad-hoc 长任务 | CONSOLE-020 | OTA workflow 通过验收 |
| T2/TDMA/DPLL/校准 | 未实现 workflow | 无 step state/证据/恢复 | CONSOLE-021..024 | 各自 workflow 通过验收 |
| 多端口 bring-up | 维护脚本/显式多口 | 非单端口目标 | CONSOLE-026/027/035 | ring ready 或 bootstrap 准入 |

### 7.7 待定设计选择

1. 定义格式：YAML（易读、可 diff）还是 Python dataclass（类型强）？建议 YAML + 注册 action 类型，禁止 eval。
2. TCP 权限：只允许 `status`，还是允许 `start/pause/resume/cancel`？建议默认只读。
3. parallel 范围：v1 是否只允许只读并行？建议是。
4. OTA 恢复：依赖固件 durable offset，还是每次从头传？建议先从头传，固件有 journal 再续传。
5. 人工门在 headless：是否允许 `--yes` 自动通过？建议只允许非破坏性步骤，BOOT/COMMIT 必须显式 flag。
6. workflow 与多端口：transport 不匹配直接 fail-closed，不做自动降级。

### 7.8 运行语义

- **状态唯一事实源是 workflow state**，不是 Tk 变量、线程局部变量或日志文本。
- 状态持久化到 `out/workflows/<workflow_id>.json`：`workflow_id`、`version`、`cursor`、`inputs`、`started_at`、`last_error`、`step_results`、`evidence_hashes`。该布局是候选契约 `CONSOLE-FLOW-01`，登记前不作为外部承诺。
- 支持从最后一个成功步骤恢复；恢复前重新校验前置条件和版本，不满足则 fail-closed，不得静默从第 0 步重跑。
- 步骤必须尽量幂等；非幂等步骤（BOOT/COMMIT/CAL:SAVE）必须有明确的“已执行”证据和二次确认。
- 取消/中止必须回到安全态（STOP/ABORT/释放 lease），并记录取消点。
- 同一 workflow 只允许一个 runner；GUI、CLI、TCP 不能同时驱动同一条 workflow。
- Workflow 必须可 headless 运行；GUI 只是视图，不是执行器。

### 7.9 证据与版本

- 每步输出结构化结果；最终输出 receipt（复用 `CONSOLE-RECEIPT-01` 候选；登记前不作为验收判据）。
- 凭证包含 workflow id/version、固件 build、命令目录版本、板卡 ID、步骤结果、失败 reason、日志/镜像 hash。
- workflow 版本与固件 build/命令目录不匹配时 fail-closed，不允许“试一下”。

### 7.10 UI 约束

- 长期流程页面只显示：步骤列表、当前步骤、阻塞原因、下一步、最近一次证据。
- 不允许在页面里再写一套“开始/停止/恢复”的业务逻辑；按钮只调用 workflow runner。
- 一次性查询/单条写命令可以保留命令卡；一旦需要多步/恢复/证据，必须升级为 workflow。

### 7.11 必须工作流化的流程

- OTA（常规单板、TDMA 流式）
- T2 停止态配置 + 运行 + teardown
- TDMA 启环（含低速 SPI bootstrap → 高速 TDMA 交接）
- DPLL/VDC 启动、锁定、采集
- 校准训练（BIAS/MARK/SCK/DATA/CLK/STAGE）
- REFMEM 装载/激活/表/质量（多板）
- 多板验收/证据收集

### 7.12 禁止

- GUI-only workflow state；
- ad-hoc 线程 + 无 checkpoint 的“长任务”；
- 每个 tab/CLI 各写一份步骤定义；
- 用通用 raw SCPI 转发冒充 workflow engine；
- 失败后静默从第 0 步重跑；
- 无版本匹配检查的 resume。

## 8. 安全与证据

- 默认只读；写操作按 STOP/RUN 状态和维护模式分级。
- `STOP`、`BOOT`、`COMMIT`、`CLEAR`、`OTA`、`CAL:SAVE` 等操作必须二次确认并记录操作者/模式。
- RUN 中禁止写 active 配置；维护动作必须经过 owner/lease 或控制面 ACK。
- 每个流程步骤输出结构化证据；OTA 已实现自动凭证，后续 T2/calibration/TDMA/DPLL 复用同一 receipt 规范。
- 凭证是自查摘要，不是私钥签名；发布级签名仍由发布流程完成。

## 9. 阶段

| 阶段 | 目标 | 出口 |
|---|---|---|
| C0 立项 | 三件套、索引、命名白名单、边界 | 文档门禁全绿 |
| C1 底座 | Transport/Dispatcher、WorkflowSpec/WorkflowRunner、持久化状态、receipt、安全门；低速 SPI bootstrap persona 承载 REFMEM 控制面 | host 用例全绿；单端口/多端口模式可见；bootstrap 状态可读；workflow 可恢复 [候选 CONSOLE-FLOW-01]；CONSOLE-002/003/004 已登记；CONSOLE-035 有 host 模型 + 资源核算表 |
| C2 运行面 | REFMEM 页、T2 停止态与运行、TDMA 启环、DPLL 状态；高速 TDMA persona 交接；白名单长期流程以 workflow 形式交付；按 §6.2 收敛单端口 | 单端口可执行步骤全部有回读与证据；persona 切换/回滚有 evidence；无 GUI-only 状态；多端口仅剩登记中的过渡项 |
| C3 测量面 | 校准训练、TDMA/DPLL 采集、存储/trace | 台架证据可复跑 |
| C4 收口 | HIL smoke、版本矩阵、打包 | 可升 SUPPORTED |

## 10. 验收判据

- 文档：`docs_check --strict-names`、`doc_regression_check` 全绿。
- 代码：host 用例覆盖 transport、TCP、OTA、流程前置与 receipt；模拟器可复现。
- 台架：至少一次四板 smoke，覆盖 REFMEM 装载、T2 配置/启动、TDMA 启环、DPLL 锁定、常规 OTA；scope 与版本矩阵写入 receipt。
- 安全：RUN 态写拒绝、维护模式确认、销毁性操作确认均有负向用例。
- 工作流：`WORKFLOW-COMPLIANT` 白名单内的流程必须以 `WorkflowSpec` 交付并满足状态持久化/恢复/取消；现行例外（OTA/T2/TDMA/DPLL/校准）按 §7.6 登记，不得把全量断言当已满足。相关 state/receipt 布局是候选契约 `CONSOLE-FLOW-01` / `CONSOLE-RECEIPT-01`，登记前不作为验收判据。
- 单端口：默认 workflow 的 `target_transport` 必须是 `single_port_refmem`；ring 未 ready 时允许 `maintenance_multiport`，但必须带 `reason=RING_NOT_READY`、UI 标识和退出标准；不得成为长期双路径。
- 交付：调试 Demo 可在 `--demo` 下完整演示，在真实串口/USB TMC 下完成同一流程。

## 11. Tk 应用框架

CONSOLE 的 Tk 层采用 **MVP/Passive View + Redux-lite Store/Intent + Hexagonal ControlPlane + Statechart/ECC + 可逆补偿** 的混合架构，详细定义见 [CONSOLE_TK_ARCHITECTURE.md](CONSOLE_TK_ARCHITECTURE.md)。

分层固定为：

```text
Tk Shell（root/mainloop/theme/DPI/close guard）
  ├─ Router（PageSpec / Page 生命周期 / 导航守卫）
  ├─ Pages（Passive View，只读 Store、只发 Intent）
  ├─ Store（ConsoleUiVector 只读快照）
  ├─ Intent Bus
  ├─ Presenters / Controllers
  ├─ Workflow AO
  ├─ UiPump（worker/queue → Tk 主线程）
  └─ Services（Transport / Dialog / Settings / Log / Theme）
```

硬约束：

- 只有 Tk 主线程能碰 widget；worker 只能 post `UiEvent`。
- Page 不直接调用 Dispatcher/Transport，不直接改业务状态，不直接 `messagebox`。
- Page 协议含 `render/cancel/dispose`；懒加载；workflow 运行/表单 dirty 时 `can_close` 返回 `NavigationDecision(allowed=False, reason=...)`。
- Store 是 UI 摘要的唯一写者目标；workflow 字段只是投影，权威事实仍是 workflow state。Tk 变量只是只读适配器。
- DialogService 负责确认/错误/进度；危险操作由 Presenter 调 `confirm`。
- 单端口必须定义 OTA 独占期：独占期内非 OTA 命令静默丢弃并写 status，不弹模态。
- WidgetKit 统一表单/表格/日志/错误条/进度；主题与 DPI 用 token/scaling；首版不引入 ttkbootstrap。
- 每页有错误边界；日志有上限；关闭窗口先 cancel/join 再 destroy；设置 schema 校验 + 原子写。
- 长期流程页面只显示 workflow step/阻塞/证据，不写第二套执行逻辑。

当前 Demo 仍是 legacy：Page 直接继承 `ttk.Frame`，状态散在 Tk 变量，对话框直接 `messagebox`，OTA 是 ad-hoc 线程。迁移按 `CONSOLE-028..032` 执行，不做大爆炸重写。
