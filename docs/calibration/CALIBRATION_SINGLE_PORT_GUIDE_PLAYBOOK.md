# 单口引导期校准流程（10 kHz 载体）

Status: Draft
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`
Related: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/console/CONSOLE_WORKFLOW_ARCHITECTURE.md`
Last updated: 2026-11-05

本文是单口引导期流程的 **Draft**（v2），不登记为契约，不作为外部承诺。v1 与 v2 均经两份
独立只读审查判 `REJECT`；v2 的 B1–B9 闭合判定与新增 N1–N3 见
`docs/check/submissions/CALIBRATION_GUIDE_CROSS_REVIEW_01.md`。本文只保留审查后仍成立的部分，
未闭合项全部落在 §5；在 N1–N3 与 B2/B7/B9 取得证据前，本文不构成可执行方案。

## 1. 范围与定位

最终产品形态只接协调板单口。10 kHz 4 节点低速 TDMA 是**引导期**唯一的跨板命令载体
（`REFMEM_CMD` 走 TDMA 二等席）；引导期完成校准训练并产出 10 MHz 环所需的 staging 后，
命令载体切换为 10 MHz 二等席，USB 仍是同一个口。

本文只定义**校准侧**的引导窗口契约与接口。编排归 CONSOLE（任务 `CONSOLE-022/024`；契约
`CONSOLE-FLOW-01` 是**未登记的候选**），命令面与信封/ACK 归 REFMEM（`REFMEM-CTRL-01` 增量、
`REFMEM-TRANSPORT-01`），环生命周期与低速档归 TDMA（`TDMA_DOMAIN_ARCHITECTURE.md` 低速档
一节）。同一条语义不在三个域各写一份。

**接入点例外（v2 复核 M-3）**：`REFMEM-CTRL-01` 已冻结 CP1 的显式例外——非协调者入口不中继、
不排队、不建 relay 状态（registry 该行原文如此）。本文的"单口"因此**必须**接协调者板；
这不是"接入点无关"，而是引用该例外，不能反向推翻 CP1。

**载体状态（N3，round 2 复核）**：TDMA 架构的低速档一节仍写"当前软件不可行"，但那是 L1–L4
实现前的结论，**已过时**。逐条核对见 §9.3：四处硬门槛与免标定 ARM 都已在代码落地，且四板
10 kHz HIL 已通过（UP/DOWN、过程映像、命令/ACK）。因此本文可以把这个载体当作"功能已证、
soak 待补"的前置，而不是"未知"；仍未闭合的是 600 s soak 与 1M+ 标定（TDMA M5 进行中），
以及 TDMA 文档本身待刷新。

**不做**：不新增物理总线、不新增 payload class、不新增第二命令槽、不新增第二套代际命名空间；
不在校准域重写环生命周期，不在 CONSOLE 重写 CP1–CP9。

## 2. 五段（v1 的四段 + 补 S）

| 段 | 环状态 | 谁执行 | 产出 |
|---|---|---|---|
| C 配置 | 10 kHz RUN | 目标本地 owner 解析，写 RAM staging | staging + ACK（**不写 flash**） |
| T 触发 | 10 kHz RUN | 目标本地 owner 自置 pending | required 全 ACK 后允许停环 |
| R 运行 | 无环 | 板端 core1 定序；core0 只提交 intent | 逐 trial 证据 + 恢复状态 |
| S staging | STOP | 目标本地 owner 写 per-link staging 并过 ARM 门禁 | 可 ARM 的 staging |
| Q 查询 | 环恢复后（10 kHz 或 10 MHz） | 单口读 typed mirror 行 | 完成字 + 参数记录 + age/stale |

Q 不是最后一段才存在：`Q1` 在 R 之后经 10 kHz 读逐 trial 记录，供 S 消费；`Q2` 在 S 与 10 MHz
起环之后读环状态。两处读的是同一套 typed 记录，只是时机不同。

段顺序只是校准窗口内的**步骤 ID**，编排语义归 `CONSOLE-FLOW-01`（候选、未登记）；在该候选
登记前，本文不构成第二份编排契约（v2 复核 M-4）。

### 2.0 这是维护窗口，不是"在线配置"

`REFMEM-CTRL-01` 增量的前提是"TDMA 不停机"（`REFMEM_DOMAIN_ARCHITECTURE.md` 的扩展节）。
本流程会在 R/S 段停环，属于**显式维护窗口**，不得被描述为"在线配置"。

**但"豁免"目前没有条款依据（v2 复核 M-5）**：扩展节只写"TDMA 不停机前提下…"，没有写维护
窗口例外。因此本节不能自称"不受该前提约束"；必须在 REFMEM 扩展节补一句显式豁免，或引
TDMA 低速档条文，否则"配置期间不得停环"与"T→R 停环"仍然是冲突（v1 审查已判该冲突成立）。

### 2.1 C：只 staging，不落 flash

v1 的"C 段每板持久化并读回"被判 `BLOCKER`：环在跑时写 flash 需要 core1 park + lockout，
而 core1 一停，承载该命令的环就中断（自指死锁），且违反低速会话禁止 commit 的既有条款。

v2 固定为两步：

1. **环上**：命令只把配置写入目标 RAM staging，目标本地 owner 校验后 ACK；
2. **停环后**：由目标本地存储 owner 在 STOP 窗口把 staging commit 到 flash，写后读回。

持久化本身走既有 `REFTMEM-IDLE-01` 的 idle 窗口事务（窗口 + stage + COMMIT + owner validation），
不自造"写后读回即生效"的旁路；低速会话内一律不 commit。掉电安全性由"开机引导档"承担
（见 2.3），不依赖运行期写 flash。

**不新增代际**：v1 的"配置代际"被判 `MAJOR`（与既有 `calibration_generation/topology_generation/
profile_crc32/schedule_crc32` 形成第二套命名空间，且不进入 ARM 校验链）。v2 直接复用这四项身份；
上电后的新 epoch 按 `REFTMEM-PERSIST-01` 重建，不把持久化代际当运行期权威。

### 2.2 T：目录内一条命令，不是广播

v1 的"一条广播"被判 `BLOCKER`（CP5：不允许隐式广播/魔法零值；零掩码在
`refmem_control_submit.c` 被直接拒绝）。v2 固定为：

- 复用目录内 command_type，携带**显式** `target_mask` / `required_mask`（A0–A3）；
  **掩码必须由 active `SlotClaimMap` 解析并随命令记录**（CP5 的完整要求，不只是"带上字段"，
  v2 复核 M-2）；空掩码/非 claim 子集拒绝；
- 发起方只提交 intent，**不写**目标 active fact（CP2）；pending 由目标本地 owner 自置；
  （目录行与 handler 尚未建立，CP2/CP7 目前**没有实现证据**，见 §3、§5。）
- 去重键 `(coordinator_boot_id, command_seq, target)`：重传只返回既有终态，不重跑 R（CP7）；
  `boot_id` 当前全仓不存在，须先定义（§5）；
- `required_mask` 未全 ACK 前**不得**停环；T 失败时环保持运行，不进入 R。ACK 聚合本身已有
  通用实现（`distributed_refmem_cmd_reply.c`），但"全 ACK 才停环"的门控没有消费者。

fence 尚未实现（`REFMEM-CTRL-01` 的 C6 未闭合），因此本文只声称"ACK 聚合"，不声称 fence；
需要 fence 的读取语义不得在本流程中使用。

### 2.3 开机引导档：单口的地基

v1 的"末尾强制恢复 10 kHz"被判 `BLOCKER`：恢复要发的 `RING:TOPOLOGY` 等命令在运行期是
`NOT_ONLINE_SAFE`，环停后也没有通道可发；且默认档是 10 MHz，无 calibration stage 时 ARM 直接
拒绝，会停在不可恢复状态。

v2 固定为：

- 每板持久化一份**开机引导档**：10 kHz profile + `BRINGUP_NO_CAL` + bring-up topology（按 slot
  顺序的 directed link map）；
- 上电后各板按持久化档定序自启（从板先、参考板最后），**不需要任何跨板命令**；
- 自启前做准入：`node_count` 与持久化拓扑一致、slot 无冲突；不满足则停在 STOPPED 并置 reason，
  不允许"三块板起来就算成功"（半环）；
- 因此 R 段末尾的"恢复"不是发命令，而是**回到 STOPPED 并以引导档重建 10 kHz**；Q 在开机态
  即可回答，缺板能被定位。

该档需要一个新的持久化子区。**v2 复审修正**：`CALIBRATION_NVS` 前 4 个擦除扇区已被
calibration store（2）与 seat mount store（2）占满并有 static assert，compat map 只有 2 扇区，
因此这不是"不改 FlashMap 就能加"：必须改 deployment map（新子区或扩分区），属于 ARCH-FLASH
变更，需按该域流程评审。是否新开 TDMA 契约 id 同样由 TDMA 域与 C11 决定；本文不占用 id。

**仍未回答（N1）**：无命令时"谁触发 ARM/START"、以及单板如何判定半环（quorum/超时规则）。
上电自组不能只写"定序自启"：需要给出触发者、判定规则与超时后的停留状态；在这三点定稿前，
本节不成立。

### 2.4 R：Δ 由表派生，ARM 用有界窗口

v1 的"Δ ≥ 300 ms"被判 `BLOCKER`：没有派生下界，也没有"四板已 ARM"的依据。v2 固定为：

- `Δ_i` 按节点派生：≥（最坏帧送达时延 + STOP/ARM/清 FIFO/DMA 的 WCET + guard），逐节点不同；
  具体推导与 10K 快照见 §9.5（T 必须单 chunk，`Δ` 下限暂取 2 s），**不写死常量**；
- 每个 trial 前留一个 ARM 窗口 `Δ_arm`（≥ 最坏 ARM 时延 + guard），各板在窗口内完成 arm；
- 窗口是**有界保证，不是握手证明**：没有跨板通道，无法证明"四板已 arm"。因此
  - 每个 trial 记录本地 arm 完成时刻与是否超窗；
  - 超窗的 trial 记 FAIL，不猜测、不补测；
- `Δ` 等待与 `inject` 必须是 core1 owner 的定时/状态机动作；core0 只提交 bounded intent、
  读 guarded snapshot，不得向实时面注入时间戳或用轮询代替 core1 状态机；
- 每次 repeat 仍是独立的 `STOP → ARM → inject → capture`，沿用既有训练语义。

### 2.5 Q：完成字必须带身份与新鲜度

v1 的"done/fail + reason + 代际"被判 `MAJOR`：缺 `CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md` §6
要求的逐 trial 身份，也缺 CP4 的 `epoch/seq/age/stale`，stale 会静默混入。

v2 固定为：

- 记录绑定 build、`topology_generation/crc32`、`profile_crc32`、`schedule_crc32`、
  `calibration_generation`，以及 §6 的逐 trial 字段（marker/data lag、margin、polarity、CRC、
  `sample_period_ns`、DMA overrun、PIO stall、timeout、reject reason）；
- 逐 trial 还必须带 `delta_ms`、`arm_window_ms`、`arm_cycle`、`arm_latency_ms`、`over_window`
  （见 §9.5），否则事后无法归因"没 arm"与"arm 了但 inject 丢了"；
- 单 writer（CAL）+ seqlock 发布；
- 每行带 `age/stale`；stale 行不得用于决策；
- Q 缺任一身份字段即整轮 FAIL；
- 10 kHz 下**不得**声称波形证据：该档的 SCK 诊断捕获窗口只能看到电平，波形证据限定在
  10 MHz 训练段。

### 2.6 S：把结果变成可 ARM 的 staging

v1 缺此段，被判 `MAJOR`：四段做完也拉不起 10 MHz 环。v2 固定为：

- 与 R 在**同一个暗窗**内执行：staging 数据由 C 段预置（RAM staging），S 段在环停后本地应用、
  校验 generation/CRC/freshness，再 `ARM → START 10 MHz`；
- **不把 S 段做成"STOP 下再发命令"**（v2 复审 N2）：跨板命令唯一载体是环，环停即断；若 S 段
  需要新的跨板输入，只能并入 C 段预置，否则必须放弃该输入；
- 任一 link 缺 accepted window → 整轮拒绝并保持 STOPPED，不允许部分启动；
- 通过后才允许 10 MHz 的 `ARM → START`；失败回退到引导档，不保留半更新。

## 3. 命令目录增量（跨域，最小集）

下表是**待建**目录（当前 4 行，覆盖 9 个 command_type）；`refmem_command_catalog.c` 对
`CAL_`/`RING_`/`TDMA_BOOT`/`OPMODE` **零匹配**，所以这一节是"要建什么"，不是"已有什么"
（v2 复核 M-1）。

| 行 | command_type | owner | 约束 |
|---|---|---|---|
| 1 | `TDMA_BOOT_PROFILE` | TDMA | ≤16 word；STOPPED |
| 2 | `CAL_GUIDE_CONFIG` | CAL | ≤16 word；STOPPED；大表需 `param_kind: ref` |
| 3 | `CAL_GUIDE_TRIGGER` | CAL | 1 word（epoch）；STOPPED 执行 |
| 4 | RING `STOP/ARM/START/BRINGup/TRAIN` + `OPMode`（6 条） | TDMA | 各一条 command_type；STOPPED |

**v2 复审修正（B7/M-6）**：`CAL ...:LINK` 不在目录中，`ref` 也**没有实现**——只有宏与一个 u32
`payload_ref` 身份字段，没有 pool 容量/CRC/owner 设计；`TLV_REF` 只有枚举、无消费者；
`param_words ≤ 16` 是硬限制。因此"用既有 LINK/COMMIT 路径"这句话不成立。

并且：若 `ref` 最终走 wire 分段（R1-M5 的分段 + 每段 CRC + 整表 CRC），那就是新的 wire/布局，
与本文 §1"不新增 payload class"冲突；必须先改 §1 边界或走 ARCH 增量，不能只在目录里加一行。
另一条路是**板端本地生成**（不由 host 灌入），不触碰 wire，但要把生成逻辑放进 CAL owner。

两条路都未开工，属前置，不是"既有"。

`allowed_states`、`required_capabilities`、`idempotent` 目前只在目录表中生成、执行侧不消费
（`refmem_command_catalog.c` 只校验 `param_words/param_kind`）。本流程依赖这三项生效，故列为
前置工作项，而不是假设已存在。目录现状另有两处空洞：`refmem_command.h` 已有 `CAL_START`/
`CAL_SAVE_LOAD_ACTIVATE` 枚举值，但没有对应目录行、全仓零引用；全仓也没有 `boot_id` 概念，
而 `command_seq` 由 `osal_tick_ms()` 播种，跨重启不唯一（B8）。

### 3.1 登记计划（不新开 id，C11 前不登记）

| 面 | 复用条款 | 现状（v2 复核 B-1） |
|---|---|---|
| 编排/单口 workflow | `CONSOLE-FLOW-01` | **候选，未登记**：CONSOLE 架构明写"以下契约尚未登记"，`DOCS_REGISTRY.md` 无 `CONSOLE-*` 行；依赖 C11 + 登记 |
| 命令/信封/去重/ACK 聚合 | `REFMEM-CTRL-01` 增量 | 方向允许（该文档已有"作为增量条款"先例），但增量尚未写进扩展节、registry check 文本未同步 |
| 环停/启与 10 kHz 引导档 | `TDMA_DOMAIN_ARCHITECTURE.md` 低速档节 | 由 TDMA 域登记；该节当前仍判"不可行" |
| 训练序列与测量参数 | `CALIBRATION-PHASE-01` | 本域既有条款 |

任何一行都不得在 C11 完成前写成 `active`；本文保持 `Draft`。段顺序（§2）只是校准窗口内的
步骤 ID，编排语义归 `CONSOLE-FLOW-01`（候选）；在该候选登记前，本文不另立第二份编排契约
（v2 复核 M-4）。

## 4. 失败归属

| 失败 | 归属 | 结果 |
|---|---|---|
| 恢复/自启失败 | TDMA | 该板 STOPPED + reason；整轮 FAIL；可被 Q 定位 |
| 测量/记录失败 | CAL | 记录带 reason；整轮 FAIL |
| 命令未 ACK / 去重命中 | REFMEM | 不进入 R；环保持运行 |
| 编排/证据不齐 | CONSOLE | receipt 不签发 |

不自动重试、不半启动、不把超时当完成。

## 5. 前置与未决（未取得证据前不得宣布可用）

- **N1 自组触发未定**：上电后"谁触发 ARM/START"、单板如何判定半环（quorum/超时）没有定稿；
  现有代码只有 RAM 内 bring-up 标志，boot 只按编译期档位恢复，ARM 仍是显式本地调用。
- **N2 S 段通道已修正**：S 段不得再依赖"STOP 下发命令"，只能消费 C 段预置；若有新跨板输入，
  必须并入 C 段。
- **N3 10 kHz 载体：功能已证、soak 未跑**（round 2 复核，见 §9.3）：四处硬门槛与免标定 ARM
  都已在代码落地，四板 10K HIL `passed=true`；不再阻塞 M0。残留是 600 s soak、1M+ 标定，
  以及 TDMA 文档的低速档结论待刷新（当前仍写"不可行"，属文档滞后）。
- **B9 存储落点未定**：默认构建用 v1 compat 地图，`PRODUCT_NVS` 已满、`SEAT_MOUNT` 正好用完、
  `FUTURE_POOL` 无权限；v2 地图有空间但只在 factory-migration 预设。四个候选见 §9.2，
  未裁定前不得开工。
- **B7 `ref` 载荷不存在**：只有宏与 u32 `payload_ref`，无 pool 容量/CRC/owner；`CAL ...:LINK`
  目录项也不存在。
- **B8 无 `boot_id`**：去重三元组缺第一项；`command_seq` 由 `osal_tick_ms()` 播种，跨重启不唯一。
- **B3 Δ 输入与字段**：`Δ` 依赖的最坏帧时延、STOP/ARM/清 FIFO/DMA WCET 未实测；Q 的逐 trial
  字段还没有 arm 完成时刻/超窗位，事后无法区分"没 arm"与"arm 了但 inject 丢了"。
- 单口控制面产品化前置：`REFMEM-CTRL-01` 的 C4/C5 未闭合，非协调者入口的远端发号仍被拒；
  本流程以"单口必接协调者板"为前提。
- `P0T-05/06` 未完成：板内 topology snapshot 与重复 probe 门禁缺，引导档的 topology 身份只能
  由 host 生成。
- 10 kHz 会话的相位 WCET、迟到、IRQ 预算未实测；低速档打破了编译期"线时 ≤ TDMA 相位 WCET"
  断言，必须补同源码实测。
- SD 写时延无实测上界；`T_trial` 必须 ≥ 锁存 + 落盘上界，否则丢 trial。core1 不写 SD，
  由 core0 storage owner 写，双缓冲 + 有界队列 + 溢出拒绝码。
- REFMEM mirror 的 CAL 区当前是 1024 B 不透明 buffer，无 schema/epoch/seq/age/stale；
  typed 行是 Q 段的前置。

## 6. 验收判据

分三段关闭，任一段不过不得进入下一段：

1. **单口自启**：拔掉其余三口，上电后四板按引导档自组 10 kHz；半环必须被拒绝；Q 能定位缺板。
   （N1 未定稿前，本段不可执行。）
2. **单口一轮**：C（staging）→ T（显式掩码、去重、required 全 ACK）→ R（Δ 派生、ARM 窗口、
   逐 trial 记录）→ 恢复 → Q（身份齐全、stale 显式）；全程无 2–4 号口。
3. **S + 10 MHz**：S 与 R 同一暗窗执行，逐 link staging 过 ARM 门禁，10 MHz 环 `up/down` 与
   计数增长；任一 link 缺窗即整轮 STOPPED。

## 7. 证据与批次

- B1 文档（本文 + 审查记录 + TODO 行）：文档门禁；
- B2 TDMA 开机引导档持久化与自组：P3 凭证；
- B3 REFMEM 目录 7 类 + dispatch 校验 + 去重：P3 凭证；
- B4 CAL 引导 runner（含 core1 定时 inject）：host + 四板 HIL；
- B5 CONSOLE 单口 workflow：host 凭证；
- B6 四板拔三口验收：P3 + HIL。

B2/B3 文件域不相交，可并行；B4 依赖两者；B6 是唯一总门禁。

## 8. 夜间自动流程里程碑（P0T → TRN-03）

目标：把 P0T 到 TRN-03 做成四板自动流程，单端口写配置、单端口触发、板端本地执行、
复环后查询；夜间无人连续运行，失败即停并留证。每个里程碑的通过证据必须绑定
build + `topology/profile/schedule/calibration` generation，且不得把 `DIAGNOSTIC_ONLY`
结果升级为 active。

| 里程碑 | 内容 | 退出门禁 | 依赖 |
|---|---|---|---|
| M0 | 开机引导档 + 定序自组（WP-1） | 拔掉 2–4 号口上电，四板自组 10 kHz；半环/冲突拒绝并留 reason | **被 N1/B9 阻塞**：触发者/quorum 未定，FlashMap 需变更 |
| M1 | P0T 自动化：单口触发 topology probe（隔离 persona），出 accepted topology（ring_order/node_map/CRC/generation），按 accepted topology 写 NO 并落引导档 | 单闭环才接受；开链/分叉/反向/重复 Node 拒绝；跨轮一致 | M0；P0T-05/06；可行性见 §9.6（host-free，暗窗 ≈48 s） |
| M2 | TRN-00 自动化：marker capture + cut-through | 四板同 epoch、顺序正确、forward residence 有界、PIO/DMA fault 为 0 | M1；固定阶梯（§9.7） |
| M3 | TRN-01 自动化：独立 SCK offset matrix | 每个 destination Node 独立 repeat 通过；residual 门禁；identity 一致 | M2；固定阶梯（§9.7） |
| M4 | TRN-02 自动化：marker 锚定 DATA 窗口 | 四条 directed link 同 generation/profile 收敛；窗口/guard/skew 证据齐全 | M3；固定阶梯，禁自适应缩窗（§9.7） |
| M5 | TRN-03A/B 自动化：per-link staging + ARM gate；raw-flight → process-image | 写后读回、缺 link 拒绝、STOPPED 回退；四板 FIFO/sequence 同时增长 | M4；B7/R2（§9.8，不依赖 N1） |
| M6 | 单口全链一轮（C/T/R/Q/S）并起 10 MHz | 拔三口完成一轮；10 MHz 由 staging 过 ARM 起环；失败整轮 STOPPED | M5；P3-03 bias |
| M7 | 夜间无人值守 | 连续 N 轮（N 由用户定）失败即停、证据落盘、次日按同一脚本复跑一致 | M6 |

M6 的"10 MHz 由 staging 起环"依赖 `P3-03/P3-LB-06` 的有效 endpoint bias；该前置未闭合时，
M6 只能先以 `BRINGUP_NO_CAL` 起环证明流程，不能把 bring-up 结果当作 active 标定。

## 9. 待裁定：N1 触发方式 / B9 存储落点

本节只给证据与取舍，不替用户做决定；裁定后回填 §2.3 与 WP-1。

### 9.1 N1：无命令时谁触发 ARM/START、半环如何判定

现状事实：板号已持久化并在 boot 载入（`app.c` 的 `board_identity_set_no(persisted_board_no)`），
所以"身份"不是问题；缺的是 ring 启动档与自启触发。core1 在 `app_runtime.c` 里等 `app_is_ready()`
后进入实时循环，core0 可在 app ready 后用任务做一次性自启；`tdma_service_ring_arm/start` 是本地
API，不需要环上有命令。难点是**半环判定**：单板在环起来之前看不到其他板。

| 方案 | 机制 | 半环可判定性 | 单口可观测 | 代价 |
|---|---|---|---|---|
| A 对等定序 | 各板 boot 后按 slot 固定延时自行 `arm→start` | ❌ 四板各自空转，谁也不知道缺谁 | 只能看协调板自己的 reason | 低 |
| B 主从成环 | 协调板先发；从板 boot 进"RX 监听"态，收到首个合法帧后加入并按 slot 定时发 | ✅ 协调板从帧/bitmap 看出缺槽；缺板停在监听态并留 reason | 协调板可读缺槽 | 中：adapter 需 RX-join 态 + join 窗口 + 新 reason |
| C 人工按一次 | 现场用服务口逐板触发 | ✅ | — | 与"只接单口"冲突，终态不可用 |

**建议 B**。A 只在 B 的 RX-join 被证明不可行时作为降级；C 不作为产品形态。

**B 的接口规格（草案，待评审）**：

- 状态：`BOOT_LISTEN → JOINED → ARMED → RUNNING → STOPPED/FAULT`；
- 协调板：boot 后载入引导档 → `arm/start`（slot 0 先发）；
- 从板：boot 后只进 **RX 监听**（不启 TX）；收到首个合法帧（magic/CRC/slot 匹配）后，按 slot
  偏移在下一圈加入 TX；
- join 窗口：2 圈（10K 下约 1.2 s）；超窗留在 LISTEN 并置 `JOIN_TIMEOUT`；
- 缺槽判定：协调板从 process image 的 bitmap/WKC 读缺槽；缺槽记 `MISSING_SLOT`，按策略
  "整环停"或"降级继续"，不得静默；
- 新增 reason：`JOIN_TIMEOUT` / `SLOT_CONFLICT` / `MISSING_SLOT`；
- adapter 需支持：RX-only arm（不启 TX）、帧内 slot 身份校验、join 后动态启 TX（不重装 persona）。

该规格已落成可执行模型：`tests/python/test_cal_guide_formation.py`（6 个用例，覆盖全员加入、
join 超窗、窗口边界、缺槽降级、slot 冲突、单板不得报 READY）。它不是固件，只作为 adapter
改动的验收基线；固件实现必须让同样的状态/原因码可复现。


### 9.2 B9：开机引导档落在哪里

现状事实：默认构建用 **v1 compat 地图**（`flash_deployment_map.h` 注释：v2 只由 factory-migration
预设打开）。compat 地图里 `PRODUCT_NVS` 64 KiB 已全部占满（校准 store 取末尾 2 扇区、其余给
product config），`SEAT_MOUNT` 8 KiB 正好被 seat mount store 用完，`FUTURE_POOL` 按设计不给权限；
唯一 app 可写的 `SCRATCH` 640 KiB 是诊断/自检的临时区。v2 地图的 `CALIBRATION_NVS` 有 1 MiB，
但只存在于 factory-migration 预设。

| 方案 | 落点 | 需要改什么 | 迁移 | 风险 |
|---|---|---|---|---|
| 1 v2 地图 | `CALIBRATION_NVS` offset 4×erase | 打开 v2 预设 + 加子区 + static assert | 全板 factory migration | 干净，但产线成本高 |
| 2 扩/新分区 | compat map 里加分区 | 改 `flash_map_v1_compat.h` + deployment map | 既有校准 store 偏移变化 → 数据迁移 | 中高 |
| 3 共享 seat mount store | 同 2 扇区、第二个 object type | 把 store 泛化为多 object type | 无 | 容量/轮换风险，需算最坏记录跨度 |
| 4 SCRATCH 子区 | 划一段 scratch | 只加 fence，防诊断清区 | 无 | 与"临时区"语义冲突，不推荐 |
| 5 塞进 product_config 记录 | 现有 page ring | 改记录布局/CRC | 有 | 把 TDMA 启动耦合到产品配置，不推荐 |

**建议**：若接受一次 factory migration → 选 1（最干净）；否则选 3，先做容量分析（seat mount
表最坏记录跨度 + 引导档记录，两扇区是否够轮换）。2/4/5 不推荐。

**方案 3 的容量测算**（供裁定）：seat mount 的 payload 是 `4 + 8×2 = 20 B`，而 store 的
program 单位是 512 B，所以**每条记录占 512 B**；两扇区 8 KiB 共约 16 条记录，轮换前约 14–15 次
append。再加一个 object type 会各占一条 512 B 记录，轮换预算减半，但引导档只在 provisioning 时
写，写入频度低，容量本身不是问题；真正的成本是把单类型 store 泛化为多 object type（scan/
select/commit 都要带类型），属中等重构。

### 9.3 N3 复核矩阵：10 kHz 载体到底缺什么

| TDMA 文档的硬门槛 | 代码落点 | 证据 | 状态 |
|---|---|---|---|
| baud 区间拒绝低速 | `tdma_operating_profile_admitted_for_ring` + `slot_min_ns`（`tdma_operating_profile.c`），两处准入 `tdma_ring_runtime.c`、`tdma_pio_spi_ring_adapter_tx.c` | 代码已改；host 用例覆盖 | ✅ 代码 |
| 档位目录无 10 kHz | level 23 `{10000, 150000000, 4096, 0, 4}`（`tdma_operating_profile.c`） | 代码 + `OPMode:STAGe 23/APPLy` 读回 | ✅ 代码 |
| TX 期限误判 | `tdma_pio_spi_phys_tx_completion_margin_ns(flight_cycle_period_ns)`（`tdma_pio_spi_phys_flight_io.c`、`tdma_pio_spi_phys_arm.c`） | host 断言 150 ms→300 ms 等 | ✅ 代码 |
| 维护 ACK 50 ms 超时 | `tdma_maintenance_ack_timeout_ms` 由 `feedback_timeout_ns × 2` 派生（`tdma_maintenance_runtime.c`） | host 用例 + 10K 命令 HIL | ✅ 代码 |
| 免标定 ARM 语义断点 | `TDMA_RING_FLAG_BRINGUP_NO_CAL` + `tdma_service_set_bringup_no_cal` + adapter TX 路径判定 | build + 四板 HIL | ✅ 代码 |
| 四板 10 kHz 物理 | — | `out/tdma_lowrate/wf-10k-run3/summary.json`（`passed=true`，4 节点、UP/DOWN、tx/rx>0、`rx_bad=0`）、`ring-10k-run7`、`wf-10k-pi2`（过程映像 `active=1`） | ✅ 功能 |
| 600 s soak / 1M+ 标定 | — | `TDMA-LOWRATE-M5` 进行中 | ⏳ 未闭合 |

**结论**：N3 不是"设计不成立"，而是**文档滞后 + soak 未跑**。引导期只需 10 kHz 功能可用，
这一点已有四板证据；因此 N3 从"阻塞 M0"降为"残余（soak）"，并应在 TDMA 域刷新低速档结论。

### 9.4 B7 复核：`ref` 到底有没有载体

先前审查说"`ref` 只有宏"，这一条需要修正：**池已经存在**——`refmem_param_pool.h` 定义了
3 个 1024 B 的 slot（TLV 区正好 3072 B），命令带 `payload_kind=REF` + `payload_ref=slot`，
目标按 `command_seq/epoch/size/CRC` 校验；模块有 host 用例 `tests/unit/test_refmem_param_pool.c`。

但跨板仍不成立：池初始化在 `refmem_param_pool_init(s_distributed_refmem_table.tlv, ...)`，
而 `REFTMEM_VECTOR_REGION_TLV` 在全仓**零消费者**——TLV 不参与同步/镜像。也就是说池是
**板内**的：协调板写进去的 slot，目标板上不存在。56 B wire 原生只带 envelope，不带 payload。

| 方案 | 机制 | 改动面 | 10K 代价 | 风险 |
|---|---|---|---|---|
| R1 镜像 TLV | 把 REGION_TLV 纳入同步 | 同步覆盖 + 带宽 | 3 KiB × 每轮，远超 ~200 B/s | 排除 |
| R2a 维护流分片 | payload 走维护流新 type（10 B/chunk、8 chunk/帧 = 80 B/帧） | 新 type + 重组 + CRC | 544 B ≈ 7 帧 ≈ 8.4 s（配置一次） | 触碰 wire/分段，需 ARCH 增量 |
| R2b 紧凑 stage | 只传测量量（≈152 B），其余板端派生；走既有 `0x55`（256 B 上限） | 板端派生 + LINK schema 版本 | 152 B ≈ 5 帧 ≈ 6 s，**无新 type** | 派生公式要与 validate 保持一致 |
| R3 板端生成 | stage 由板端从本地 TRN 结果生成 | CAL 算法下沉 | 0 | 与现有 host 工具分工冲突，工作量大 |
| R4 host 直连 | 多口写 staging | 无 | — | 与"只接单口"目标冲突 |

**建议**：优先 **R2b**（不新增 wire、容量已验证）；若板端派生不可接受，再退 **R2a**（要 ARCH
增量）；R3 作为长期方向。R1 因带宽排除，R4 与目标冲突。任何方案落地前都要先改 §1 的边界表述
或走 ARCH 增量，不能只加一个目录行。

### 9.5 Δ 与 arm 窗口的派生（B3 部分闭合）

10K/4 节点下，由代码符号算出的快照（非事实源）：

- 物理帧 174 B（`tdma_operating_profile_slot_min_ns` 的 `physical` 公式，`loop_delay = 1000 ns`），
  wire 时间 139.2 ms；
- guard 量子 533 µs（32 个 PIO 控制拍），slot 150 ms，整圈 600 ms；
- HIL 快照 `wf-10k-run3`：STOP 四板 1.296 s、ARM 四板 0.407 s、START 四板 0.625 s、
  等待 `adapter_started` 8.437 s。

推导：

1. **T 必须压成单 chunk**（`CAL_GUIDE_TRIGGER` 只有 1 word）：一帧穿过环时四板都能收到，
   送达跨度 ≤ 1 个 cycle（600 ms）。若 T 需要多帧，跨度按"2 cycles/帧"（工具口径）放大
   数倍，Δ 随之后移。
2. `Δ_i` ≥ 送达（1 cycle）+ 本地停环/arm 上界（暂取 1 s，待 WCET 实测）+ guard（1 slot）
   ≈ 1.75 s → 取 **2 s 下限**；在实测 WCET 前不得低于此值，也不得写成常量而不校验。
3. `Δ_arm`（每个 trial 前的 arm 窗口）≥ 1 s（四板 ARM 实测 0.407 s + 清 FIFO/DMA 余量）。
4. 以上是**快照**：`Δ` 的三个输入（本地停环、arm、清 FIFO/DMA 的 WCET）必须实测后才冻结；
   B3 保持 PARTIAL，直到实测与负测齐全。
5. Q 的逐 trial 字段必须新增：`delta_ms`、`arm_window_ms`、`arm_cycle`、`arm_latency_ms`、
   `over_window`。缺 `over_window` 就无法区分"没 arm"与"arm 了但 inject 丢了"。

### 9.6 M1 可行性：P0T 能做成单口自动流程

`calibration_ring_topology.py` 的 probe 是**逐对、host 驱动**的：每对
`STOP 全环 → 两板写 RING:TOPology 2,<slot>,0 → CALibration:TOPology:PROBe → ARM → START(driver)
→ 接收侧采样 ≥3 次 → STOP`。判定只看接收侧计数增量（帧/字/边沿任一 > 0），`magic/header` 只记录。

关键结论：**probe 的中间决策不需要 host**。每对的"谁驱动、谁接收、谁不参与"可由静态表算出，
接收侧判定是本地计数比较。因此 M1 可以复用 C/R/Q 机制：

| 段 | 内容 |
|---|---|
| C | 下发 12 对（4×3）的有向对表：pair_id、driver slot、receiver slot、两板 topology slot、`pair_min_observations=3`、`pair_wait` |
| R | 板端按固定步长逐对执行：STOP→topology(2,slot,0)→PROBe→ARM→START→采样→STOP；非本对参与的板保持 STOPPED |
| Q | 每板发布本对的角色与计数增量；host 聚合出有向邻接，只接受"单闭环"（每点出度=入度=1），否则拒绝并留 reason |

代价与容量：

- 暗窗时长 ≈ 12 对 × 4 s ≈ 48 s（对间要确认 STOP，不能并行）；
- 对表 12 × 8 B ≈ 96 B，单 REF slot（992 B）或两帧 inline 都能装，不新增 wire；
- 现有工具要求 `VDC:FEEDback:SESSion? == 0`，引导期满足。

因此 M1 不是新的硬点；它和 M2–M4 一样走"静态表 + 板端串行 + 事后聚合"。但 M1 的两个下游仍被
N1/B9 卡住：接受后的 topology 要写 NO 并落开机引导档（B9），而引导档的自启方式未定（N1）。

对表与判定已落成可执行模型：`tests/python/test_cal_guide_topology_table.py`（8 用例：12 个有向对、
正环接受、开链/分叉/多环/重复边拒绝、反向环接受、anchor 旋转、无活动拒绝）。它只固定判定语义，
不替代实板 probe。

### 9.7 M2–M4 可行性：相关与判定已在板端

读三个训练工具后，结论比预期好：**per-trial 的相关与接受判定由设备快照给出，不需要 host 在暗窗内
做信号处理**。

- `calibration_marker_train.py` 解析的字段含 `best_lag_sample`、`correlation_reject_reason`、
  `margin` 等，全部来自板端 snapshot；
- `calibration_data_train.py` 的 `parse_data_status` 同：`best_lag_sample`、`best_distance`、
  `second_lag_sample`、`margin`、`resolved_offset_sample_count`、`resolved_offset_ns`；
  host 只做范围/矩阵/统计；
- `run_offset_matrix` 是"候选 × repeat"的固定表，host 在**全部 trial 结束后**才选推荐行，
  不在暗窗内决策；
- raw capture 回读（`parse_storage_read`）只用于证据，不是判定输入 → 原始波形可留 SD，
  只把摘要带回 10K。

因此 M2–M4 与 M1 同构：预置"候选 × repeat"固定阶梯 → 板端串行执行 → 复环后 host 聚合。
唯一约束是 **TRN-02 的自适应缩窗不能保留**：那会让 host 回到环内；v2 用固定阶梯替代自适应，
直到板端能自己缩窗（后续项，不在 M2–M4 范围）。

### 9.8 M5 可行性：staging/ARM 同窗完成，跨板输入只有矩阵

`trn03_stage.py` 的实际流程是：`RING:STOP` → `OPMode:STAGe <level>` → `CAL:TRAIN:STAGe:BEGin`
→ 4×`LINK` → 读回逐字段核对 → 可选 `COMMit` → `RING:ARM`（失败回 `RING:STOP`）。

三个结论：

1. **整段都在 STOP 下、且都是本地动作**：stage/ARM 不需要环上命令；矩阵是唯一的跨板输入
   （每板一份完整 stage，约 544 B）。
2. **不需要 reboot**：stage 先写进 RAM，`ARM` 直接用 RAM stage；`COMMit` 只是持久化，
   boot 读回是另一个验证动作。因此 M5 的"起 10M"不依赖 N1（不需要先自组再重启）。
3. **跨板输入落在 B7**：544 B 必须在下暗窗之前进到每块板（R2 分片广播 / R3 板端生成）。
   这是 M5 唯一的硬耦合；B7 不决，M5 不能开工。

M5 的暗窗序列（草案）：

```
STOP 10K → apply stage(RAM) → 读回 4 link → COMMIT(flash) → ARM(10M) → START(10M)
         └ 任意一步失败 → 恢复 10K 引导档 → 回执 reason
```

`COMMIT` 必须在 STOP（flash 事务 park core1，与 B1 一致）；`ARM` 的输入是矩阵里的 per-link
window/guard/budget，`runtime_is_armed` 提供读回。

### 9.9 引导计划 schema（C 段数据面）

C 段下发的计划记录固定为：

| 字段 | 约束 |
|---|---|
| `build` | 非空；与板端一致 |
| `topology_generation/crc32`、`profile_crc32`、`schedule_crc32`、`calibration_generation` | 全部非零；缺一即拒 |
| `segments` | 必须恰为 `C,T,R,S,Q` |
| `trigger_words` | 必须 = 1（4 B，单 chunk，见 §9.5） |
| `delta_ms` / `arm_window_ms` | ≥ 2000 / ≥ 1000（§9.5 下限） |
| `pairs` | = N×(N−1)（4 节点为 12） |
| `candidates × repeats` | 阶梯规模；超 ref slot 即拒 |
| `stage_links` | = N；stage 本身不进计划，用 `(crc32,size)` 引用（走 R2） |

4 节点、6 候选 × 3 repeats 时计划约 304 B → 走 `ref` slot（≤992 B）；`inline`（64 B）只够极小的
计划。校验码：`MISSING_IDENTITY`、`SEGMENT_ORDER`、`TRIGGER_NOT_SINGLE_WORD`、
`DELTA_BELOW_FLOOR`、`ARM_WINDOW_BELOW_FLOOR`、`TABLE_TOO_LARGE`、`STAGE_LINKS_INCOMPLETE`、
`PAYLOAD_OVERFLOW`。

已落成可执行模型：`tests/python/test_cal_guide_plan.py`（9 用例，覆盖上述全部拒绝码与合法计划）。

### 9.10 全链时长预算（M7 可行性）

假设（**估算，待实测替换**）：维护流 80 B/帧（8 chunk × 10 B）、1.2 s/帧（10K 二等席）；
每段边界重启一次环 5 s；每 trial 3 s（arm + inject + capture + poll + save）。

| 段 | trial 数 | 小计 |
|---|---|---|
| M1 P0T | 12 对 × 4 s | 48 s |
| M2 TRN-00 | 4 link × 3 repeat × 3 s | 36 s |
| M3 TRN-01 | 4 dest × 3 repeat × 3 s | 36 s |
| M4 TRN-02 | 4 link × 6 候选 × 3 repeat × 3 s | 216 s |
| M5 stage/ARM | 1 × 30 s | 30 s |
| C 段计划下发 | 5 × 4.8 s（304 B = 4 帧/计划） | 24 s |
| 环重启 | 5 × 5 s | 25 s |
| **合计** | | **415 s ≈ 6.9 min** |

10 轮夜间 ≈ 69 min；8 h 可容约 69 轮。10K 维护 ACK 预算 2.4 s ≥ 2×1.2 s，覆盖单帧重试。
所有 per-trial 数字都是估算：`T_trial` 必须实测（与 B3 的 WCET、SD 时延同一批证据），
否则 M7 的"夜间连续跑"不可承诺。

已落成预算守卫：`tests/python/test_cal_guide_campaign_budget.py`（5 用例，基线 415 s、
计划下发 4.8 s、ACK 覆盖、超大阶梯超 900 s、10 轮进 8 h）。

### 9.11 M0 基线测量 runbook（实板，只读优先）

目的：在 N1/B9 裁定前，用现有固件测出"M0 到底缺什么"与"停环→ARM 的真实时延"，给裁定和
实现提供基线。**优先只读观测，不做 `COMMit`、不写标定、不改 flash。**

前置：四板 USB 都接上（这一轮是观测，不是产品形态）；四板同 build；`SYSTem:ERR?` 干净；
不跑 T2/VDC；记录每板 UID。

| # | 动作 | 记录 | 判据 |
|---|---|---|---|
| 1 | 断电重上电，四口只读 `SYSTem:TDMA:STATus?` 与 `SYSTem:TDMA:OPMode?` | 每板 `adapter_started`/`up`/`down`/`config_seq`/`applied_config_seq`/active level | 预期全部未自启 → 证实 N1 现状 |
| 2 | 只留协调板口，读 `SYSTem:CTRL:STATus?`、`SYSTem:REFMEM:CMD:REPLY?` | 能看到几块板、哪些字段 | 记录单口可观测边界（预期只看协调板） |
| 3 | 四口：`RING:STOP` 后逐板轮询 `STATus?` | STOP→`up/down=0` 的毫秒数 | ≤ 2 s；超时记失败 |
| 4 | 四口：`OPMode:STAGe 23`+`APPLy`、`TOPology 4,<slot>,0`、`BRINGup 1`、`RING:ARM` | 每板 STAGe/APPLy/TOPOLOGY/BRINGup/ARM 的时刻与读回 | STAGe/APPLy 读回 23；ARM 无 `-200` |
| 5 | 四口：`RING:START`，等 `adapter_started` | START→started 毫秒数、`last_error`、tx/rx 增量 | ≤ 5 s；up/down=1；tx/rx>0 |
| 6 | 四口：`RING:STOP` → 立刻再 `ARM`（同一批） | 第二次 ARM 的拒绝码/时延 | 记录"停环→ARM"真实上界，用于校 §9.5 的 2 s 下限 |

记录模板（每板一行）：`uid, build, step, t0_mono, t1_mono, delta_ms, adapter_started, up, down,
config_seq, applied_config_seq, last_error, active_level, raw_response`。

判据与处置：

- 步骤 1 若出现任何板自启 → N1 的"无法自组"前提被推翻，先复现再改设计；
- 步骤 3/5 超时 → 记 `-` 并保留原始响应，不当成通过；
- 步骤 6 的 `delta_ms` 若 > 2000 → §9.5 的 Δ 下限要上调，并同步计划 schema 的 `DELTA_BELOW_FLOOR`；
- 全程不触发 flash 写；若工具或脚本需要 `COMMit`，改为只读路径或跳过。

这一轮只产生基线，不改变任何实现；输出放 `out/guide_baseline/<stamp>/`，作为 N1/B9 裁定的输入。

### 9.12 R2 设计：544 B 载荷怎么过维护流

既有事实：维护流 chunk 22 B（12 B 头 + 10 B 数据），一帧 8 chunk = 80 B；`0x55` 分片的 record
只有 4 B、上限 256 B，**装不下** 544 B 的 stage。所以 R2 不是"复用 0x55"，而是给维护流加一个
type。

协议（新 type `0x56` = `REFMEM_PAYLOAD`）：

| 帧 | 字段 | 语义 |
|---|---|---|
| BEGIN | `transfer_id`、`slot`、`total_bytes`、`crc32`、`target_mask`、`command_seq` | 开一次传输；目标按 mask 参与 |
| CHUNK | `transfer_id`、`chunk_index`、`data[10]`、`crc16` | 每帧 8 chunk；重复 chunk 丢弃 |
| END | `transfer_id`、整包 `crc32` | 目标校验后写 param pool slot 并 ACK |

规则：

- 重组目标就是 `refmem_param_pool` 的一个 slot（992 B），写时带 `command_seq`，复用池既有的
  `seq/epoch/size/CRC` 校验；不需要新缓冲。
- 去重：`(transfer_id, chunk_index)` 重复即忽略；新 `transfer_id` 使旧传输作废并释放 slot。
- 超时：`2 × ceil(bytes/80) × 1.2 s + 1 s`；未完成 → `PAYLOAD_TIMEOUT`，slot 释放。
- 失败码：`PAYLOAD_TIMEOUT`、`PAYLOAD_CRC`、`PAYLOAD_SLOT_BUSY`、`PAYLOAD_TOO_LARGE`、
  `PAYLOAD_MASK`。
- 广播语义：帧穿过整环，四板都能看到；非目标按 mask 忽略——不需要为每个目标单独发一遍。

代价：544 B → 55 chunk → 7 帧 ≈ 8.4 s；计划 304 B → 4 帧 ≈ 4.8 s。两者都是一次性配置，
可接受。

边界：这是**新 wire 类型 + 重组**，不是"既有能力"。落地前必须改 §1 的"不新增 payload class"
表述或走 ARCH 增量，并做 C11；若这条不被接受，只能转 R3（板端生成，完全不新增 wire）。

**复用核查结论（round 14）**：chunk 头是 12 B——magic(2) + `stream_id`(1) + `generation`(1) +
`index`(1) + `count`(1) + `total`(2) + `crc32`(4)；收发两侧都按"单流 80 B"校验（`frame_size`/
`count`/`crc` 必须一致），**不能跨帧聚合**。所以 544 B 无法用现有头表达，R2a 确实要新 type。

因此新增更省的方案 **R2b：紧凑 stage**——只传"测量量"，其余由板端派生：

| 每 link 传 | 字节 |
|---|---|
| `marker/sck/data_offset_sample_count`（3×4） | 12 |
| `sample_period_ns`、`link_base_delay_ns` | 8 |
| `marker/sck/data_phase_delay_cycles`（3×4） | 12 |
| `forward_residence_cycles`（实测） | 4 |
| 合计 | **36 B/link → 4 link + 头 ≈ 152 B** |

152 B 落在既有 `0x55` 分片的 256 B 上限内 → **不需要新 type**；按 32 B/帧计约 5 帧 ≈ 6 s。
代价：板端要把 `pio_persona/clkdiv/instruction_period/bit_cycles/codeword/guard/budget` 这些
**由 profile+persona 派生**的字段自己算出来（现有 validate 路径已有这些公式），并让 LINK 接受
"紧凑 + 派生"的 schema 版本。

### 9.13 R2b 最小字段集：哪些必须传、哪些能派生

把 LINK 的 34 个字段按来源分类：

| 来源 | 字段 | 结论 |
|---|---|---|
| profile + persona 派生 | `valid/link_index`、四个 node、`evidence_flags`、三代 identity + 三个 CRC、`pio_persona`、`clkdiv_q16`、`clk_sys_hz`、`instruction_period_ns`、`bit_cycles`、`marker_to_data_cycles`、`rx_arm_lead_cycles`、`codeword_cycles`、`guard_cycles`、`loop_delay_cycles` | 板端可算，不必传 |
| 由 offset/base 派生 | `marker/sck/data_phase_delay_cycles`、`origin_capture_phase_delay_cycles`、`link_budget_cycles` | 板端可算，但公式必须与 host 一致 |
| **实测，必须传** | `marker/sck/data_offset_sample_count`、`origin_capture_offset_sample_count`、`link_base_delay_ns`、`forward_residence_cycles` | 4 link × 6 word = 96 B |
| 常量 | `sample_period_ns` | 由 clkdiv/clk_sys 派生 |

所以紧凑 stage 的最小集是 **6 word/link（24 B）**，4 link + 头 = **104 B**（比 9.12 的 152 B 更小），
`0x55` 下约 4 帧 ≈ 4.8 s。

落地要求：

1. 新增板端派生函数（暂名 `calibration_stage_derive`），输入 = 紧凑记录 + profile + persona，
   输出完整 stage；公式必须复用现有 validate/预算的符号，不另写一份；
2. `CAL:TRAIN:STAGe:LINK` 增加 schema v2（紧凑 + 派生），旧 v1（34 字段）保留；
3. **双端一致性测试**：同一组紧凑记录，host 计算与板端派生必须逐字段相等（golden case），
   否则 ARM 会因 budget/identity 不一致而拒；
4. 派生失败的 reason 要能区分"缺输入"与"派生越界"。

已落成守卫：`tests/python/test_cal_guide_compact_stage.py`（4 用例：4 节点 104 B/4 帧、
6 节点 152 B 仍在上限内、全量 544 B 超限故需 R2a、每 link 恰 6 个测量字）。

### 9.14 R2b 派生来源核查：公式现在只在 Python 里

逐字段追到实现后确认：**17 个"可由 profile+persona 派生"的字段，C 侧没有派生函数**。
C 只做校验（非零、`budget > link_budget_cycles` 即拒），真正的计算在 host 工具
`trn03_matrix.py` 的 `pio_facts()`/`ns_to_pio_cycles()` 里：

- `clkdiv_q16 = (BOARD_SYS_CLOCK_HZ × 256 / (baud × bit_cycles))` 取整后左移 8；
- `instruction_period_ns = ceil(clkdiv_q16 × 1e9 / (65536 × BOARD_SYS_CLOCK_HZ))`；
- `bit_cycles`、`marker_to_data`、`rx_arm_lead`、`codeword`、`guard`、`loop_delay`、budget
  由 persona/profile 常量与上两式组合。

因此 R2b 的真实成本是：**把这些公式从 Python 移植到 C**（单一来源），再加"host 与板端逐字段
相等"的 golden 向量测试；不是"接线已有函数"。风险是两份实现漂移——缓解办法是像码本 golden
向量那样，把一组固定输入/期望字段进仓库，host 与 C 都跑同一组。

这一步做完，R2b 才成立；在此之前 M5 仍以 R2a 为备选。

**进展（2026-11-05）**：`calibration_stage_derive.{h,c}` 已落地为三个纯函数（pio/base/phase），
数值与 §9.13 的 golden 向量逐项相等，host 侧 gcc 编译并运行通过；正式 test runner 尚未登记
（`tools/tests/run_*.ps1` + tool registry/triage 待补），因此还不进 host 聚合。

### 9.15 决策备忘录（阻塞实现的三项裁定）

到 round 16，决策无关的工作已到边界：四个可执行模型（编队/拓扑/计划/预算）+ 可行性分析 +
R2b 字段集 + 公式来源都已落文档。下面三项是 WP-1 与 M5 的唯一入口，**未裁定则无法开工**。

| # | 决策 | 选项 | 推荐 | 不裁定的后果 |
|---|---|---|---|---|
| 1 | N1 成环方式 | A 对等定序 / B 主从成环（RX-join） | **B**：半环可从 bitmap 判定；A 在单口下不可判定 | M0 无法实现；M6 无法证明缺板 |
| 2 | B9 引导档落点 | 1 v2 地图（factory migration）/ 3 共享 seat mount store | **1**（最干净）；不接受迁移则 3 | 单口形态断电即失效 |
| 3 | B7 载荷路径 | R2b 紧凑 stage + 派生移植 / R2a 新 type `0x56` / R3 板端生成 | **R2b**；不可接受则 R2a | M5 无法写 staging，10 MHz 起不来 |

每项的选择都会写回 §2.3/§9.1/§9.2/§9.4 与 `CAL_GUIDE_CHANGE_LIST.md`；裁定前目标保持
`blocked`，不产生半成品实现。

**裁定（2026-11-05，用户确认"可以，继续吧"）**：

- N1 = **B**（主从成环：协调板先发、从板 RX-join、缺槽由 bitmap 判定）；
- B9 = **1**（v2 地图 + factory migration；代码侧先按 v2 分支实现引导档子区）；
- B7 = **R2b**（紧凑 stage 104 B + 派生公式从 Python 移植到 C，golden 向量对齐）。

自本轮起 WP-1 与 B7 解锁；B9 的 factory migration 是产线动作（不在本会话执行），代码侧只做
v2 分支的引导档子区与 boot 载入。

### 9.16 实现现状（2026-11-05 收口快照）

| 组件 | 文件 | 状态 |
|---|---|---|
| 引导档记录 | `components/tdma/{inc,src}/tdma_boot_profile.*` | ✅ 32 B/CRC/字段校验；host 测试通过 |
| 引导档存储 | `tdma_boot_profile_store.*` | ✅ v2 双扇区 NVS、轮换、写后重扫；v1 返回 UNSUPPORTED；host 通过 |
| 自组决策 | `tdma_boot_plan.*` | ✅ N1=B 四动作/四原因；host 通过 |
| 自组步骤 | `tdma_boot_sequence.*` | ✅ START_REF/LISTEN/REJECT/NONE；容量不足不截断 |
| boot 载入 | `application/src/app.c` + `app.h` 访问器 | ✅ 载入并冻结计划/步骤；**不执行** ARM/START |
| v2 子区 | `config/flash_deployment_map.h` | ✅ offset 4×erase / 2 扇区 + static assert；v1 `HAS=0` |
| R2b 派生 | `calibration_stage_derive.*` | ✅ pio/base/phase 三纯函数；golden 对齐；LINK v2 未接 |
| 构建 | `CMakeLists.txt` + flash 白名单 | ✅ v1 157/157、v2 164/164，`flash_link_contract=OK` |
| P3 | build `20261105002955` | ✅ `FOUR_NODE_DPLL_QUICK_DIAGNOSTIC` PASS（INFO 42/0/0/0，sources 981，trials 8） |
| 工具修正 | `workflow_steps.py:2807` | ✅ run 计划补 step 6，消除 stale-build 误判 |

**未实现**（M0/M1 的必要项）：序列执行（ARM/START）、`RING_LISTEN`/RX-join、SCPI 只读出口、
store 写者（provisioning 命令面）、M2–M5 的 C/T/R 编排、mirror typed 行。

**提交状态**：本目标的 32 个文件已暂存、门禁全绿（`check-staged` build 20261105002955、
host 112 passed），但整索引里另有 20 条既有 staged 批次（`archive/bootstrap_spi_v1`），
pathspec 提交会被 hook 判"未暂存差异"、整索引提交会带上该批次；等待归属处置。





















