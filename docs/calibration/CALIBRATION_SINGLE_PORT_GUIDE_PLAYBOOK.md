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
| boot 载入 + 执行 | `application/src/app.c` + `app.h` 访问器 | ✅ 载入并冻结计划/步骤；`app_guide_boot_service()` 由 `app_diag_service` 首轮执行（stage/apply→topology→bringup→ARM→START；v1 空转）；`RING_LISTEN` = ARM 收 + 首帧后 START（5 s 窗口，超时 `join_timeout`，不假装加入） |
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

### 9.17 M0 实测（2026-11-05，v2 单口引导期）

**自组**（build `20261105032420`，`PROJECT_TDMA_GUIDE_AUTOFORM=ON`，四板同 build，无任何起环命令）：

| 板 | slot | up | down | tx | rx | half_ring |
|---|---|---|---|---|---|---|
| NO.1 | 0 | 1 | 1 | 57 | 41 | 0 |
| NO.2 | 1 | 1 | 1 | 45 | 45 | 0 |
| NO.3 | 2 | 1 | 1 | 48 | 48 | 0 |
| NO.4 | 3 | 1 | 1 | 50 | 50 | 0 |

- `SYSTem:TDMA:BOOT:PLAN?` 四板 `fail_step=0`；NO.1 = `START_REF`，其余 `LISTEN`；
  NO.2/3/4 `join_frame_seen=1`。
- **半环拒绝**：停掉 NO.2 后重启其余三块 → 三块 `half_ring=1` 且 `up=0/down=0/started=0`；
  恢复四板后回到 `up=1/down=1`、`half_ring=0`（20 s 窗口内轮询 `down_running`，不误杀）。

**关键实现事实**（写给下一位）：

- LISTEN 必须 **立即 START**：ARM 只开硬件抓取，`data_enabled`（START）才让
  `tdma_ring_runtime` 调 adapter service；"听到再 START"会死锁（`RX_NEEDS_START`）。
- ARM 必须走 `distributed_refmem_tdma_ring_arm()`：它先按 staged node_count 配默认 flight map，
  直连 `tdma_service_ring_arm` 会被物理层以 `OWNER_FLIGHT_MAP(17)` 拒绝。
- 无 join-TX 抑制门：从板 START 即参与发送；`join_frame_seen` 只是证据，不是门控。

**v2 factory 迁移的 OTA 坑**：factory UF2 声明 `full_erase_required: true`，但
`picotool load` 不擦 OTA_JOURNAL；残留 journal 跨启动被 FlashTransaction 重放，导致后续串口 OTA
在第一步擦除就报 `erase failed: state=9`。处置：按工件契约整片擦除后再 load；整片擦除会抹掉
持久化板号（NO.0），需逐板重写 `SYSTem:BOARD:NO`。清 journal 后 P3（build `20261105035001`）
恢复 `PASS_WITH_WARNINGS`。

### 9.18 M0 复跑 runbook（四板自组 + 半环拒绝）

前提：四板已迁移到 v2 地图、板号 NO.1..NO.4 已写入、环线按 NO.1→2→3→4 接好。

```powershell
# 1) 构建带引导档的台架镜像（slot = NO-1，nodes=4，level=23 = 10 kHz）
cmake -S . -B out/build/v2-guide -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=<pico-sdk>/cmake/preload/toolchains/pico_arm_cortex_m33_gcc.cmake `
  -DPICO_BOARD=pico2 -DPICO_SDK_PATH=<pico-sdk> -DPROJECT_NODE_CAPACITY=4 `
  -DPROJECT_FLASH_DEPLOYMENT_MAP=v2_candidate -DPROJECT_FACTORY_MIGRATION_BUILD=ON `
  -DPROJECT_TDMA_GUIDE_AUTOFORM=ON -DPROJECT_OTA_SECURITY_COUNTER=1 `
  -DPROJECT_OTA_SIGNING_KEY_ID=3506438145 -DPROJECT_OTA_KEY_PROFILE=v2_debug `
  -DPROJECT_OTA_TDMA_V2_BRIDGE=ON
cmake --build out/build/v2-guide -j 8

# 2) 逐板刷 factory 镜像（串口 OTA 在 factory 迁移后不可用，picotool 是正规路径）
Copy-Item out/build/v2-guide/DHRT100_V2_CANDIDATE_FACTORY.uf2 out/build/v2-guide-picotool/DHRT100_FACTORY.uf2 -Force
foreach ($s in @('0010071E65B5CB38','FB276192BEF9CCE1','2BD5090FE009FA2A','A1E549202D18ED6A')) {
  python tools/picotool_flash/picotool_flash.py out/build/v2-guide-picotool/DHRT100_FACTORY.uf2 --serial-number $s --out "out/flash/m0-$s"
}

# 3) 同步重启（不要用主机起环命令）
foreach ($s in @('0010071E65B5CB38','FB276192BEF9CCE1','2BD5090FE009FA2A','A1E549202D18ED6A')) {
  python tools/picotool_reboot/picotool_reboot.py --serial-number $s --settle 1.0 --reopen-timeout 20 --retries 2
}
```

**判据（等待 ≥26 s 后读）**：

- `SYSTem:TDMA:BOOT:PLAN?` 四板 `fail_step=0`；NO.1 `action=1`，其余 `action=2`；末尾 `half_ring=0`；
- `SYSTem:TDMA:RING:STATus?` 四板 `enabled=1 node_count=4`, `up_running=1 down_running=1`,
  `tx/rx` 计数增长，NO.2/3/4 `join_frame_seen=1`。

**半环拒绝**：对 NO.2 发 `SYSTem:TDMA:RING:STOP`（不重启它），同步重启其余三块；≥26 s 后
三块应 `half_ring=1` 且 `up=0/down=0/adapter_started=0`。随后四块全部重启即恢复 `half_ring=0`。

**注意**：整片擦除会清板号（NO.0），刷完需 `SYSTem:BOARD:NO <n>` 逐板写回；本 runbook 用普通
`picotool load`（不 `--full-erase`），保留板号与标定。

**P0T 在 workflow 里偶发 `START <timeout>`（2026-11-05，软件侧）**：同一台架、同一线序，
`workflow_steps.py run` 内的 P0T 连续 3 次在某个 pair 的 `RING:START` 上 `<timeout>`
（`error_after` 仍是 `0,"No error"`，即板端已接受），而**独立运行**
`calibration_ring_topology.py --adjacency-only --short-open --gap 0.2` 连续 2 次 4/4 通过。
物理线序不变，因此这是**工具传输/重试路径**的问题（persistent CDC + 100 ms action-timeout），
不是台架。绕过方式（已验证）：

```powershell
python tools/calibration_ring_validate/calibration_ring_topology.py `
  --board-id <NO.1..4 串口序> --anchor-id <NO.1> --adjacency-only `
  --short-open --gap 0.2 --out-dir out/m1-scan/p0t-fix
python tools/hardware_acceptance/workflow_steps.py run `
  --preset pico2-v2-factory-candidate --tdma-only `
  --reuse-topology out/m1-scan/p0t-fix/summary.json --out-dir out/m1-scan/full-v2k
```

`--reuse-topology` 只接受**做过 NO 分配**的 summary（`--no-assign` 会被判
`known topology committed node map differs`）。

**根治方向（已试过一次，勿再走这条）**：曾在 P0T 里给 START 超时加"用 `RING:STATus?` 复核意图"的
旁路，`tests/python/test_calibration_topology_start.py` 立刻 8 条失败——该测试把"未知 ACK 必须
按失败处理、要求 STOP 后重新 ARM 并重取基线"冻结为契约，**不允许**从超时推断成功。因此正确做法
是传输层：workflow 的 P0T 改用 `--short-open`（或 START 前重连），而不是放松 ACK 判据。

### 9.19 M1 P0T 板端对表扫描（实现 + 实板状态）

**裁定（2026-11-05，用户）：M1 的拓扑接受改用 10 kHz 自组，邻接矩阵只保留为 provisioning 工具。**
理由与证据：自组本身要求每块板只从自己的前驱收到帧（`down_running=1`）且四板同时在线，
少一块板就会置 `half_ring=1` 并 STOP——这已经是"物理线序与引导档拓扑一致"的证明，不需要
12 对有向探针再证一次。下面 §9.19–9.20 的对表扫描因此只用于"尚无持久引导档、需要重新分配
slot/NO"的场景。

接受判据（新增只读出口 `SYSTem:TDMA:BOOT:TOPology?` → `accepted,reason`）：

| 返回 | 含义 |
|---|---|
| `1,0` | 引导步骤完成、环 enabled 且 `node_count` 与档一致、`down_running=1`、无 `half_ring` |
| `0,1/3` | 无快照 / 环未启用（含暗窗、STOP、刚重启） |
| `0,2` | 半环窗口已判 `half_ring=1` |
| `0,4` | 本板收不到前驱（缺板邻居或线序不符） |

实板证据（bench 镜像 `out/build/m1-self`，`PROJECT_TDMA_GUIDE_SCAN=OFF`）：

- **正**：四板同时上电 → 四板 `1,0`，`RING:STATus?` `enabled=1 node_count=4 up=1 down=1`、tx/rx 增长；
- **负**：停 NO.2 后同步重启其余三块 → 三块 `0,2`（`BOOT:PLAN?` `half_ring=1`），被停板 `0,3`；
- **恢复**：四板全部重启 → 四板回到 `1,0`。
- 附带发现：从板 `join_timeout=1` 仍可形成完整环（`join_frame_seen` 只是证据，不作否决项），
  因此接受判据**不含** join 标志。

M1 的第一段实现：引导环自组完成后，四板**不接主机命令**各自走完 12 个有向对的探针，
每板只发布"我作为接收者听到了哪些对"。执行器与纯逻辑分离：

| 组件 | 落点 | 内容 |
|---|---|---|
| 纯逻辑 | `components/tdma/{inc,src}/tdma_guide_scan.*` | 12 对表（`N×(N−1)`，driver 升序/receiver 升序）、按本地 slot 派生 `DRIVER/RECEIVER/IDLE`、活动位图、单闭环聚合与五种拒绝码 |
| 执行器 | `application/src/app.c` `app_guide_scan_service()` | boot 步骤完成后进暗窗，12 个 5 s 时隙；每时隙 `STOP → 2 节点拓扑(+bring-up topology) → 等 applied==config → PROBE 10 → ARM(带 stopped 屏障) → TRAIN 512 → START → 接收窗轮询 → STOP`；最后清 probe 并重放冻结 boot 步骤恢复 4 节点环 |
| 读回 | `SYSTem:TDMA:GUIDE:SCAN?` | 22 字段：state/pair/role/node/slot/位图/observed/skipped/failures/fail_phase/arm_result/arm_attempts/arm_gap/staged_enabled/trials_started/trials_armed/max_late_ms/stall_ms/frames_delta/words_delta |
| 构建开关 | `PROJECT_TDMA_GUIDE_SCAN`（默认 OFF） | 只在 bench 构建（同时开 `AUTOFORM`）生效；产品镜像仍是普通引导 |

**时隙对齐**：epoch 取 `reset + 12.5 s`（四板同步重启，boot 时间差 ≈0.1 s），而不是"本板首次
看到环上流量"——后者在协调板与从板之间相差 1–2 个环周期（10 kHz 下 0.6–1.2 s），会把相邻时隙
的突发算到本对头上。每个时隙的相位偏移在 `k_guide_phase_ms`，接收窗 `3400–4700 ms`。

**判据**：每板 `observed_low` 只应含本板作为接收者的 3 对；四板并集应恰为物理环的 4 条有向边；
`trials_started=12`、`trials_armed=6`、`skipped=0`、`stall_ms` 无秒级；恢复后
`SYST:TDMA:RING:STATus?` 为 `enabled=1 node_count=4`，`BOOT:PLAN?` 的 `half_ring=0`。

**已实现事实（写给下一位）**：

- 相位的推进条件是"相位号 ≤ FINISH"，因此 **FINISH 的编号必须是最大**；把 SETTLE 放在
  9/10 而没有同步抬高 FINISH，会让状态机在 SETTLE 永久停住（实测 `trials_started=12`、
  `trials_armed=0`、`skipped=6`，一个 ARM 都没发出）。
- 时隙锚点（`trial_start_ms`）必须在 FINISH 里随 index 前移；否则第 0 对之后所有相位都在
  "已过期"状态，STOP/TOPOLOGY/ARM 会在一次服务调用里连续跑完，等于没有 settle/arm 预算。
- 2 节点拓扑要同时下 **refmem 拓扑**与 `tdma_runtime_owner_set_bringup_topology(2,0)`：
  up/down group 与 flight map 来自后者；只下前者时环永远恢复不起来（实测重启后
  `node_count=0` 且 `half_ring=1`）。
- ARM 必须按宿主的屏障语义重试：先证明 `enabled=0 && adapter_started=0 &&
  applied==config`，否则重发 STOP 再等；带 gap 直接连发 ARM 会被判 `result=8`。
- `CAL:TRAINing:STAGe:CLEar` 只在进暗窗前做一次；恢复时不重做（ARM 会读标定门）。
- 判据须用与宿主相同的量级：`Δframes ≥ 10 或 Δwords ≥ 8`；"任一计数变化"会把相邻时隙的
  尾帧或串扰算成邻接。

**宿主基线（同一台架，v1 标准镜像，2026-11-05）**：`calibration_ring_topology.py
--adjacency-only --no-assign` 12 对里 4 对检出，每对 `rx_words ≈ 9.8k–11.2k`、`rx_frames=0`、
`rx_edges=0`；环序 NO.1→NO.2→NO.3→NO.4。也就是说"真检出"是**上万字**的量级，板端 0–15 字是
噪声，而不是"阈值太严"。

**probe persona 的安装条件（本轮新增事实）**：`tdma_pio_spi_ring_adapter_tx.c:351-356` 只在
`topology_probe_mode && (ring.flags & BRINGUP_NO_CAL) == 0` 时把 flight engine 停掉、走原始
探针路径；而引导档总是带 `BRINGUP_NO_CAL`。清标志的 `tdma_service_set_bringup_no_cal`
（`tdma_service.c:519-540`）又要求 `config_seq == applied_config_seq` 且 staged 已启用，
因此只能在 `SETTLE` 之后、`ARM` 之前调用（放在拓扑写入之后会恒返回 false，实测四板
`fail_phase=1`、`trials_armed=0`）。

**当前状态**：清了该标志后，四板依旧是 0–9 字（噪声级），probe burst 仍未出现；两板还在
`TOPOLOGY`（900 ms）处抢跑失败。下一步应先复刻宿主的"一次 `OPMode:STAGe`+`APPLy`+`PROBe`
前导、每对只改拓扑"的次序，再判断是 operating profile 被每对的 foundation profile 重写，
还是 `START` 后需要更长驻留。

### 9.20 M1 板端探测 vs 宿主：已知差异清单（2026-11-05 收口）

宿主 `calibration_ring_topology.py --adjacency-only` 在同一 bench 镜像、同一四板上稳定 4/4
（每对 `rx_words` 约 1.0–1.4 万），板端执行器同一套 API 只偶发命中。逐条差异如下，按已排除/未排除标注：

| # | 差异 | 状态 | 证据 |
|---|---|---|---|
| 1 | 宿主不训练；板端原先每对调 `RING:TRAIN` | **已排除（改成不训练）** | 去掉后 TX 从 0 变成 330–406 帧、接收偶发 5671/32742 字 |
| 2 | 宿主电平 level 7；板端原先用引导档 23（10 kHz） | **已排除（加 PRELUDE 一次性 apply 7）** | 改后驱动仍是 10 MHz 量级帧率 |
| 3 | 时隙对齐（四板同一对同一时刻） | **已排除** | `GUIDE:PAIR?` 的 `start_offset` 四板全为 `0,5000,…` |
| 4 | 2 节点拓扑/角色配置 | **已排除** | `packed_state`：驱动 50331650、接收 50331906（node=2/ref=0/en=1/started=1） |
| 5 | 上一对 adapter 未退休 | **已排除** | SETTLE 加 `adapter_started==0` 后结果不变 |
| 6 | 驱动 START 与接收 START 的先后 | **未成立为充分条件** | 加 300 ms 领先后反而出现 NO.1 WAIT_RING 超时、接收侧只剩固定 346 字伪影 |
| 7 | 每对前重下拓扑 | **已排除** | 强制每对都调 setter，结果不变 |
| 8 | 无跨板握手（宿主每步都确认，板端只有固定毫秒相位） | **未排除（当前首选）** | 三次运行三种结果：32742 / 5671 / 346×3 |
| 9 | `rx_preparation` job 停在 READY 导致后续 ARM 不再请求 RX 武装 | **未排除** | `tdma_rx_prepare.c:12-19` 只接受 IDLE；`tdma_pio_spi_ring_adapter_tx.c:574` 只在 stop 时 cancel |
| 10 | beacon 长度 ≠ ARM 时 `flight_physical_byte_count` | **未排除** | `tdma_pio_spi_phys_flight_io.c:61-70` 返回 BAD_ARGUMENT，arm/START 仍成功 |

**建议顺序**：先查 #9（读 `rx_preparation` 状态，或每对强制 cancel+request 再 arm），再查 #8
（把每对改成带确认的串行状态机），最后查 #10（对比 `last_error` 与 ARM 时的字节数）。

### 9.21 S 段开机决策（实现，未上板）

`tdma_boot_stage.{h,c}` 是纯决策：输入"是否有已提交 stage + stage 的 `profile_crc32` +
目录里各 level 的 `profile_crc32`"，输出 `NONE / STAGE(level) / REJECT`。规则是 fail-closed：
映射不到 level 就保持引导档，不拿 stage 里的数字猜。用例 `tests/unit/test_tdma_boot_stage.c`
（5 组：无 stage、命中、CRC 不匹配、零 CRC/空目录/level 0、NULL 输出）。

`app.c` 在 bench 开关 `PROJECT_TDMA_GUIDE_STAGE_BOOT`（**默认 OFF**）下，于载入引导档后调用
`app_guide_stage_boot_override()`：命中时把 `profile_level` 换成 stage 对应 level 并清掉
`BRINGUP_NO_CAL`（让 ARM 走标定门），未命中保持 10 kHz。

**状态**：纯逻辑已 host 验证；wiring 只在开关打开时编译进去，**尚未有实板正/负证据**——台架当前
`READ:CALibration:TRAINing:STAGe:PERSistence?` 四板均为 `persisted_valid=0`（无已提交 stage）。
正测试需要一个真实 TRN-03 stage（由 TRN-02 证据生成），负测试可在无 stage 时验证"仍按 10 kHz 起环"。

**实板结果（2026-11-05 晚，已更新）**：V2 预设 P3 产出真实 matrix 后，`trn03_stage.py --commit`
四板 `persisted_valid=1/loaded=1/reject=0`；刷 `PROJECT_TDMA_GUIDE_STAGE_BOOT=ON` 的 bench 镜像并
同步重启 → 四板活动档 **7（10 MHz/1 ms）**、`BOOT:TOPology?=1,0`、`enabled=1 node_count=4
up=1 down=1`、tx/rx ≈ 1e4，**无主机起环命令**。

**已知限制（待办）**：`STAGe:CLEar` 只清 RAM、不清 NVS 记录；开机覆盖只以 `persisted_valid` 为准
（`loaded` 在 restore 之后才置位，不能当前置条件），跨拓扑/跨 profile 的陈旧记录目前靠 ARM 门与
restore 上下文拒绝——**开机前 CRC 预校验仍是待办**，否则陈旧记录可能选错 level。

### 9.22 剩余两项的实现方案（③ 与 M2–M4）

**③-a join-TX 抑制门**（现状：LISTEN 立即 START，从板在听到参考帧之前就会发送）：

| 项 | 方案 |
|---|---|
| 语义 | `BOOT_LISTEN` 期间只允许 RX；首次解出合法帧（`adapter_rx_count` 从 0 变正）后才允许 TX |
| 落点 | `tdma_pio_spi_ring_adapter_tx.c` 的分发/信标发射入口加 `join_tx_gate` 判据；adapter 新增 `join_tx_gate` 字段 + setter；`rx_count` 已存在，无需新计数 |
| 状态机 | `ARM` 后 `gate=1` → `START` → 收到首帧 → `gate=0`（一次性，不做滞回；超窗仍停在 LISTEN 并留 `join_timeout`） |
| 判据 | 从板 `tx_count=0` 直到 `rx_count>0`；四板仍能成环；负测：只有参考板在发时不误判半环 |
| 风险 | 分发路径同时服务 RX，闸门必须只挡"发射准备"、不挡收包与完成回调 |

**③-b 缺槽 bitmap**（现状：只有本板 `down_running`，看不到"我听到了谁"）：

| 项 | 方案 |
|---|---|
| 语义 | 按帧头的 `origin_slot_id` 统计本板被接受的 RX 计数 → `rx_slot_mask/rx_slot_count[8]`，只读出口 `SYSTem:TDMA:RING:RXSLOT?` |
| 用途 | 正确环序下每板应只点亮**一个** bit（前驱）；矩阵/环序推断、缺板定位都直接读它 |
| 单口聚合 | 需要跨板汇总时走既有维护流/过程映像（C/Q 段），不在本项内；本项只保证"每板可观测" |
| 判据 | 四板各一个 bit 且并集=四条有向边；停一块后其继任者 mask 清零 |

**实测修正（2026-11-05）**：`origin_slot_id` 在 cut-through 转发下**保留发起者**，所以四板实测都是
`mask=1`（全是参考板 slot 0 的流，≈5.3–5.5k 帧），而不是"各点亮自己的前驱"。要得到前驱必须让帧头
携带上一跳 slot（当前 wire 没有）。因此该出口现在的语义是"本板收到谁的流"，可用于确认参考流覆盖，
**不能**直接当环序矩阵；缺槽定位仍用 `down_running`/半环判据。

**M2–M4（TRN-00/01/02 板端串行）**：复用已跑通的暗窗原语（`app_guide_scan_trial_step` 的
STOP→配置→等 `applied`→ARM(带屏障)→START→采样→STOP），把"候选 × 重复"固定阶梯写成板端表，
板端只做 ARM/INJECT/接受判定（这些判定本来就已在 `calibration_training_{marker,sck,data}` 里），
host 只在复环后聚合。B7 已裁定 R2b，紧凑 stage 由 `calibration_stage_derive` 派生，LINK schema v2
待接。

### 9.23 P0T 的三条硬伤与"用 10 kHz 环自证环序"（2026-11-05 专项分析）

自组 10 kHz 环在四板上稳定成立（`BOOT:TOPology?=1,0`、up/down 运行、tx/rx 增长），说明线序正确。
P0T 的失败因此**不是硬件**，而是算法/协议层面的结构问题。按严重度：

| # | 硬伤 | 证据 | 为什么重试/调参救不了 |
|---|---|---|---|
| H1 | **证明过程是破坏性且全有全无**：每对都 STOP×4 + 两次拓扑写 + 两次 ARM + 两次 START，任一步异常就丢弃整张 12 对矩阵 | `calibration_ring_topology.py:554-575`、`:649-650` | 可靠性成本随 N(N−1) 增长；今晚 3 次 `START <timeout>` 就是它 |
| H2 | **判定没有来源身份**：只看板级 PHY 计数，OR 里还包含 `rx_edges_delta>0`；真实边的唯一稳定特征是 `rx_words≈1e4`（`rx_frames=0`） | `topology.py:600-603`、`:618-631`；§9.17 | 阈值再调也只能把"字动了"当边，不能证明"是驱动方那帧到了" |
| H3 | **START 应答语义天然二义**：成功时固件不往 error 队列推任何东西，丢应答与拒绝同签名；而契约测试禁止用状态读回推断成功 | `scpi_tdma_snapshot_commands.c:1039-1048`；`tests/python/test_calibration_topology_start.py:222-237` | 只能靠 STOP+重 ARM 把破坏性循环再翻倍 |

**替代：环自证环序（零 wire 改动）**。帧头已带 `hop_count/hop_limit`（`tdma_transport_frame.c:36-37,489`），
adapter 的 `last_rx_origin_slot_id/last_rx_hop_count/last_rx_hop_limit/last_rx_flags`
已由 `SYSTem:TDMA:FLIGHT:PROcess?` 只读发布（`scpi_tdma_snapshot_commands.c:415-419`）。判据应为：

1. 四板 `enabled=1 node_count=4 up=1 down=1` 且 `down_rx_sequence` 递增；
2. 四板 `last_rx_hop_count` 的多重集恰为 `{0,1,2,3}`、无重复（重复或跳号 ⇒ 环序错乱）；
3. 从板 `origin_slot_id==ref && hop==s−1`，参考板 `origin==ref && hop==hop_limit`。

`app_guide_topology_accepted`（`app.c` 现有实现）目前**没有任何 order 项**，加上 hop 交叉校验即可把
"12 次带噪推断"换成"4 次只读测量"。注意 `last_rx_*` 是"最后一帧"锁存，需多读几次或改为逐槽聚合再判。

**落地顺序（按收益/成本）**：

- **S0（纯 host，无固件）**：① OR 改 AND（frames/words/edges 同向且连续 k 次）；② 三态判定
  `DETECTED/ABSENT/INDETERMINATE`，不确定只重测该对、不整轮 abort；③ `magic/bad_header` 变成否决项；
  ④ workflow 显式写死 `--gap`（现有 `--short-open` 已加，`--gap` 仍由 profile 隐式给）。
- **S1（极小固件）**：⑤ 补 `SYSTem:TDMA:RING:STARt:STATus?`（与 `RING:ARM:STATus?` 同构，读**意图序列**
  而非 `enabled` 效果，无竞态）；⑥ hop 交叉校验进接受判据。
- **S2**：接受判据切到 `BOOT:TOPology?`+hop；12 对矩阵降为 provisioning。注意
  `workflow_steps.py` 的 `phase_topology_gate` 仍硬要求 12 对结果、从不查 `BOOT:TOPology?`——文档已裁、
  门禁未接，需同步。

**稳定性实测（2026-11-05，S0 部分先落两处）**：workflow 的 P0T 现在显式钉住 `--short-open` 与
`--gap 0.2`（前者原来就加了，后者原先由 timing profile 隐式推导，导致"验证过的组合"与"run 执行的
组合"不一致）。同台架**连续 5 轮 standalone P0T 全部 4/4 通过**（`out/m1-scan/p0t-stab-1..5`），
此前 workflow 内是 3 连败。S0 剩余两项（OR→AND、三态判定/局部重试）与 S1 的 hop 校验仍未做。

**hop 自证的第一个实测（2026-11-05 晚）**：四板在成形的 10 kHz 环上读 `FLIGHT:PROcess?` 的
`last_rx_*`（字段序已按 handler 核对：`origin=#52, hop=#53, hop_limit=#54, flags=#55`，UInt64 占两位）：
四板全部 `origin=0, hop=3, hop_limit=5`，flags 仅 136/137/138/140 微差。**结论**：`last_rx_*` 是"最后一帧"
锁存（`rx.c:573-578`），在环上会被后续帧覆盖，不能直接当"本板位置"用——hop 交叉校验必须改成
**按 origin 的聚合**（例如每 origin 的最小 hop / hop 直方图），或在 flight engine 真正 active 时读
逐槽过程映像；仅凭单次 `last_rx_hop_count` 无法证明环序。这一条把 S1-6 从"接一个字段"改成了
"先补一个聚合量"。

### 9.24 单口引导的目标阶梯：P0–P3 + T0–T3（2026-11-05 目标定义）

单口引导的完成定义 = **四板在无主机命令的条件下，按序跑完 P0→P1→P2→P3→T0→T1→T2→T3**，
host 只在端口做触发与事后读回。当前每一项的"谁在驱动"：

| 阶段 | 内容 | 当前驱动方 | 现状 | 板端化缺什么 |
|---|---|---|---|---|
| **P0** | 拓扑/线序 | **板端** | ✅ 自组 10 kHz + `BOOT:TOPology?`（M1 裁定：自组即接受） | 环序项（hop 聚合，见 §9.23） |
| **P1** | 粗 CLK 标定 | host（`calibration_clk_train.py`） | ✅ 验收里跑通，未板端化 | 板端候选表 + 串行执行器（复用暗窗原语） |
| **P2** | 编码 MARK 标定 | host（`calibration_clk_coded.py`） | ✅ 跑通，未板端化 | 同上 + codebook 选择规则 |
| **P3** | per-link 时延/驻留 | host（`calibration_link_p3.py`） | ✅ 跑通；**active candidate 仍 BLOCKED**（endpoint bias） | 板端执行 + bias 闭合 |
| **T0** | TRN-00 marker/residence | host（`calibration_marker_train.py`） | ✅ 跑通，未板端化 | M2：候选×重复阶梯下移 |
| **T1** | TRN-01 SCK 偏移矩阵 | host（`calibration_sck_train.py`） | ✅ 跑通，未板端化 | M3 |
| **T2** | TRN-02 DATA 重复矩阵 | host（`calibration_data_train.py`） | ✅ 跑通，未板端化 | M4 |
| **T3** | TRN-03 staging + ARM | **板端（开机）** | ✅ 有 stage 即起 10 MHz（②，`GUIDE_STAGE_BOOT`） | staging 数据从环上来（R2b/单口分发） |

**结论**：P0 与 T3 已板端化；P1/P2/P3 与 T0/T1/T2 目前仍是 **host 编排、板端执行**——
板端只缺"把候选×重复的固定阶梯搬到板内串行执行"这一层（M2–M4 的同一套原语），
其 ARM/INJECT/接受判定本来就已经在板端（`calibration_training_{marker,sck,data}`）。

**收口判据（单口引导完成）**：断开全部四口后，四板依次完成 P0→P1→P2→P3→T0→T1→T2→T3，
`accepted topology + generation/CRC + 逐阶段 reason` 可从单口读回，且任一步失败整轮 STOPPED 并留证。

**架构约束（2026-11-05 用户明确）**：**起高速环（10 MHz）之前的所有任务，都必须由低速环（10 kHz
引导环）引导执行**——引导环是这段时间唯一的命令/遥测通道，host 只做一次性触发与事后读回。对应到
§2 的段语义：

- **C（配置）**：计划/候选表/阈值随引导环下发到每板 RAM staging（不落 flash、不新增代际）；
- **T（触发）**：协调板在环上广播一个 trigger word，各板按本地 epoch 在窗口内开始执行；
- **R（执行）**：需要独占 PIO 的阶段（P1/P2/P3、T0/T1/T2 的 ARM/INJECT）在**短暗窗**里本地跑，
  其余时刻环保持 10 kHz；
- **Q（读回）**：复环后逐板发布身份 + 逐 trial 字段，host 单口读；
- **S（起高速环）**：只有全部前序阶段通过，才 `ARM → START 10 MHz`（当前由 TRN-03 stage 驱动，已通）。

当前实现与这条约束的差距：

| 要求 | 现状 | 缺什么 |
|---|---|---|
| P1/P2/P3、T0/T1/T2 由环引导 | **host 逐板 SCPI 编排**，环在期间被 STOP | C 段命令目录（`CAL_GUIDE_CONFIG`/`CAL_GUIDE_TRIGGER`，§3 待建）、板端阶梯执行器（M2–M4）、Q 段逐 trial 发布 |
| 暗窗只覆盖需要独占 PIO 的片段 | M1 扫描已用暗窗原语；其它阶段未接 | 统一的"窗口开关 + 复环"调度（沿用 `app_guide_scan_leave_window` 的重放 boot 步骤做法） |
| 单口读回 | 只有各阶段自己的 SCPI 快照 | Q 段 typed mirror / 位图（§2.5、§2.6） |
| 高速环只在最后起 | ✅ 已满足（T3 stage → ARM → 10 MHz） | — |

### 9.25 每个动作跑一次 C→T→R→Q→S（执行模型）

单口引导的执行单位不是"整条链一次跑完"，而是**每个动作（P1/P2/P3/T0/T1/T2/T3…）各跑一次
C→T→R→Q→S**；环在动作之间保持 10 kHz，只有 R 段进暗窗，S 段把该动作的结果变成可 ARM 的 staging。
上一动作的 Q 不通过就不进入下一动作。

**一次 = 一个动作一轮**：P1 一轮、P2 一轮、P3 一轮、T0/T1/T2/T3 各一轮，顺序执行。
阶段内部的"候选 × 重复"（例如 TRN-02 的 6 候选 × 3 repeats）仍然发生在**该动作的 R 段里**，
不是多次 CTRQS；因此"一次"约束的是**环级编排**，不是测量次数。

| 段 | 每个动作内做什么 | 失败处置 |
|---|---|---|
| **C** | 把该动作的计划（候选表/阈值/重复数/`(crc32,size)` 引用）随环下发到每板 RAM staging；目标本地 owner 校验后 ACK | C 未全 ACK → 不广播 T，动作记 `REJECT_CONFIG` 并停 |
| **T** | 协调板在环上广播一个 trigger word（4 B，单 chunk）+ 该动作的 epoch；各板按本地 epoch 在窗口内起跑 | 超窗 → 记 `TRIGGER_MISSED`，不猜、不补测 |
| **R** | 短暗窗内本地串行跑该动作的固定阶梯（STOP→配置→ARM→INJECT→判定→STOP），逐 trial 落本地记录 | 任一步失败 → 整动作 `STOPPED`，不进入 Q，不进下一动作 |
| **Q** | 重放冻结 boot 步骤复环 10 kHz，逐板发布该动作的身份 + 逐 trial 字段（带 age/stale） | 身份缺失或 stale → 该动作 FAIL |
| **S** | 该动作结果通过校验后变成可 ARM 的 staging（T3 的 stage 即由此而来）；**只有最后一个动作的 S** 才 `ARM → START 10 MHz` | 校验不过 → 保留上一份 staging，回到 10 kHz |

**S 段即固化（2026-11-05 用户要求）**：每个动作 Q 通过后，**立刻把它自己的参数写进 flash**
（`calibration_training_store_commit`，带 `record_generation`/`calibration_generation`/`topology_generation`
与 CRC），而不是等整条链跑完才写一次。这样：

- 断口/断电后重跑，已完成的动作可以直接复用，不必从 P1 重来（配合 §9.25 的"上一动作 Q 不通过不进下一动作"）；
- 每个动作的写入是**一次**（幂等键 = 动作 id + 计划 CRC），重跑同一动作不会重复磨损 flash；
- 固化失败（写校验不过）⇒ 该动作 FAIL，generation 不前进，staging 保持上一份；
- 只有 T3 的 S 在固化后额外允许 `ARM → START 10 MHz`。

### 9.26 统一控制面：全流程走 REFMEM，停环期间本地落盘、复环回收

**2026-11-05 用户裁定**：不再保留"host SCPI 编排 + 环只做承载"的双轨，**全部改成 REFMEM 统一控制面**；
**停环（R 暗窗）期间，训练/校准的参数与进度也写进 REFMEM 的本地槽位**，启环（Q）之后由协调者读回，
据此**回收问题与进度**。

| 段 | 与 REFMEM 的关系 | 停环期间 |
|---|---|---|
| **C** | 计划/候选表/阈值 = REFMEM 命令帧（`CAL_GUIDE_CONFIG`），落本地 param pool | 不适用（环在跑） |
| **T** | trigger word = REFMEM 命令帧（`CAL_GUIDE_TRIGGER`）+ epoch | 不适用（环在跑） |
| **R** | 每 trial 的输入参数、中间量、判定结果、失败原因**写本板 REFMEM 槽位**（本地写，不依赖链路），并置 `pending` 脏位 | ✅ 正是这条：停机不等于丢参数 |
| **Q** | 复环后 REFMEM 会话 resume，协调者按槽位读回并**对账**：进度（哪些 trial 完成）、问题（失败 reason/现场量） | 读回发生在启环后 |
| **S** | 对账通过 → `calibration_training_store_commit` 固化（见上） | 不适用 |

**回收语义（关键）**：

1. 本地槽位带 `(action_id, trial_index, plan_crc32, generation)` 身份 + `pending/flushed` 状态；
2. 复环后先做**对账**再进下一动作：槽位 `pending` 多 ⇒ 说明上一轮被打断，按其 `trial_index` 从断点继续；
3. 失败 reason 也进槽位，所以"为什么停"在启环后可读（不是只看最后一帧）；
4. 状态冲突（`plan_crc32` 变化、generation 回退）⇒ 判 `STALE_PLAN`，整动作重跑，不混用旧槽位。

**契约口径（2026-11-05 用户澄清，无需改约）**：`REFMEM-CTRL-01` 扩展节里的"TDMA 不停机"
（`docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md:1691`）指的是**高速环**（运行态 TDMA），
不是引导期的 10 kHz 低速环。因此：

- P1…T2 在**低速引导环**上跑，R 暗窗停的是低速环，不在"不停机"约束范围内；
- 一旦 T3 的 S 起了 10 MHz 高速环，**此后不得再进暗窗**（所有校准训练都在起高速环之前完成），
  这才是 `REFMEM-CTRL-01` 要保护的状态；
- 所以"停环期本地写槽位、复环对账"是引导期行为，不构成契约例外，也无需 C11 复核。

**引导环可灵活配置（2026-11-05 用户补充）**：低速环只是"引导载体"，它的**档位/周期/节点数/
帧格式/命令集都是可配的**，不需要固定成 10 kHz/level 23：

| 可配项 | 谁决定 | 现状 |
|---|---|---|
| profile 档位（决定 10 kHz/1 ms 等） | C 段计划 / NVS | **编译期默认** `PROJECT_TDMA_GUIDE_LEVEL=23`，仅 boot profile store 可覆盖 |
| 节点数 | 计划 | `PROJECT_TDMA_GUIDE_NODES=4` 编译期 |
| 采集周期/窗口 | 计划 | `k_guide_phase_ms` 常量 |
| 命令集/帧格式 | 计划 + REFMEM 信封 | 未实现（C 段命令行未落地） |

因此"灵活配置"的落地方式 = **把引导档位与窗口参数变成 C 段下发的运行期参数**（落本地 param pool，
不进 flash 代际），而不是继续加编译期宏；同一个 plan 可以按动作选不同档位（例如配置阶段用更慢的档
提高裕度、测量阶段用较快的档缩短暗窗）。



**实现增量**：

1. REFMEM 槽位在**链路未建立**时也必须可写（本地 mailbox + 脏位），不能依赖 `distributed_refmem` 的连接态；
2. 复环对账 API（按 action/trial 读 pending 槽位并汇总进度/问题）；
3. 引导执行器的 R/Q 两段改成"写槽位 / 对账"，而不是"直接返回本地结构体"；
4. 单口读回增加一个对账出口（进度 + 问题 + 身份）。

**落点：沿用既有 REFMEM 控制面，不新建组件（2026-11-05 用户要求）**。上述四件事全部由
`refmem_param_pool`（`components/distributed_refmem/{inc,src}/refmem_param_pool.[ch]`）承担——
它本来就是"调用方持有 region 的纯存储池"，**不需要链路**，天然满足"停环也能写"：

| 段 | 复用的 REFMEM 面 | 调用 | 关键约束 |
|---|---|---|---|
| C | 参数池 slot | `refmem_param_pool_write(slot, plan_seq, epoch, plan, size)` | 目标端按 `command_seq` 校验 |
| T | 命令帧 | `REFMEM_CMD` trigger + `command_seq`/`epoch` | 该序号即后续读回键 |
| R | 同一 slot 回写进度 | 板端同 `plan_seq` 写 progress 记录（state=VALID） | 纯存储写；池有 3 slot × 1024 B |
| Q | 参数池读 | `refmem_param_pool_read(slot, plan_seq, …)` + CRC | seq/CRC 不符 ⇒ STALE，不采信 |
| S | flash | `calibration_training_store_commit` | 仅 Q 通过 |

**progress 记录格式**（紧凑，≤ 992 B，属于数据不是新组件）：

```
header: { action_id, plan_crc32, generation, trials_done, deepest_trial, reason, status }
rows[]: { trial_index, verdict_code, measured_u32 }        // 每 trial 一行
```

`reason` 就是"为什么停"（例如 `TRIGGER_MISSED`/`ARM_TIMEOUT`/`LINK_LOSS`），
`deepest_trial` 让下一轮从断点继续；`plan_crc32`/`generation` 与 C 段一致才被 Q 采信，
否则整动作重跑（不混用旧槽位）。

**HAOFV 合规（必须逐条满足，`docs/arch/HAOFV_FLASH_ARCHITECTURE.md` §2.1）**：

| 门 | 本设计怎么满足 |
|---|---|
| **① 不改 wire** | 只复用既有 `REFMEM_CMD` 信封与 `refmem_param_pool` 槽位；不新增 payload class、不加第二命令槽/ACK、不改帧长 |
| **② core0/core1 边界** | C/T/Q 全是 RAM 与命令面操作，不写 flash；R 段只写本板 param pool（RAM）。**S 段的 flash 落入必须由 core0 `FlashTransactionAO` 以 intent 发起**（§2.1 规则 1），业务侧不得直接调 raw flash |
| **③ 单写者** | 每块板的 param pool region 只有本板 owner 写；flash 只有 `FlashTransactionAO` 写（`ARCH-FLASHOWNER-01`）；R 期间环已停，PIO 无第二使用者 |
| **④ fail-closed** | Q 的 `command_seq`/CRC 不符 ⇒ `STALE` 整动作重跑；S 段写校验不过 ⇒ 该动作 FAIL、generation 不前进、保留上一份 staging |
| **⑤ 不改 map/错误码** | 不改 FlashMap、不新增错误码；`reason` 复用既有 reason 集合（`TRIGGER_MISSED`/`ARM_TIMEOUT` 等都是既有分类） |
| **⑥ 证据** | 每项落 host 单测 + 四板 HIL + P3 凭证；无证据不得标 ✅ |

**额外硬约束**：§2.1 规则 7 要求"**RUN、Calibration training 进行中不得开始普通写事务**"——
所以每个动作的 **S 段固化必须在该动作训练会话关闭之后**（R 结束、Q 完成、训练 session 已 close）
才可提交，且提交失败要按 fail-closed 处理，不能在训练窗口内抢写。

### 9.27 复用清单：同一概念/功能只允许一份实现（硬约束）

**规则**：动手前先在本表找到 canonical 实现并调用；找不到才允许新增，且新增时必须在本表登记。
典型反例（本次已发生并撤回）：为"停环期间记住每 trial 结果"另写一套 `guide_slot_ledger`——
REFMEM 参数池本来就是"调用方持有 region 的纯存储"，已经覆盖该功能。

| 概念 | canonical（唯一实现） | 禁止的重复写法 |
|---|---|---|
| REFMEM 命令面/信封/ACK | `components/distributed_refmem/src/refmem_command.c`、`refmem_cmd_*`、`distributed_refmem_*` | 自建第二套命令帧/第二 ACK |
| 大载荷/参数存储 | `refmem_param_pool.[ch]`（3 slot×1024 B，含 `command_seq`/`epoch`/CRC 校验） | 自建 slot/ledger/环形缓冲 |
| 触发会话生命周期（C/T/R） | `refmem_trigger_session.[ch]`（IDLE→CONFIGURED→ARMED→RUNNING→STOPPING…） | 自建 CTRQS 相位机（本次已撤回） |
| CRC-32/ISO-HDLC（含 raw 寄存器变体） | `components/algo/inc/algo_checksum.h`：`algo_crc32_iso_hdlc_update/_compute`（zlib 语义）、`algo_crc32_raw_update`（原始寄存器，seed 与终结由调用点显式给出）；golden `"123456789"→0xCBF43926`，raw 从 0 起 → `0x2DFD2D88` | 自写第二份按位/查表 CRC-32；raw 与 zlib 两种约定混用而不注明 |
| CRC-16/CCITT-FALSE | 同一头文件：`algo_crc16_ccitt_false_update/_compute`（inline，poly 0x1021 / init 0xFFFF）；golden `0x29B1` | 自写第二份 CCITT；改多项式或初值 |
| FNV-1a 32 | 同一头文件：`algo_fnv1a32_update/_bytes/_str/_u32_le/_word`；golden `"123456789"→0xBB86B11C`。**注意**：`refmem_param_pool_crc`、`refmem_vector_fast_crc32` 是 FNV-1a，不是 CRC | 自写 FNV；继续用 `*_crc` 命名指代 FNV |
| 协议专用 CRC | MODBUS `rs485_modbus_crc16`（0xA001/0xFFFF）、CRC-8 `calibration_clk_marker_crc8`（0x07/0x00）、BiSS `biss_crc_*`（配置化） | 把不同参数合并成一个入口；自造多项式 |
| 字节序打包 | LE16/LE32 wire helper（现状各域重复 ~30 处，应收敛为单一 inline 头，见审计 `out/audit/ALGORITHM_REUSE_AUDIT_20261105.md`） | 新增第四份 `put_u32/get_u32` |
| popcount / 饱和运算 | `vdc_domain_increment_saturating`、`sync_io_model_saturate_u64_to_u32` 暂为域内实现 | 新增第三份 popcount/饱和 |
| 时间坐标换算 | `vdc_timer1_ticks_to_ns` / `vdc_timer1_ns_to_ticks`（`vdc_timer1_coordinate.h`） | 各域自写 ns/ticks 换算 |
| flash 提交 | core0 `FlashTransactionAO`（`flash_transaction.h`）+ `calibration_training_store_commit` | 业务侧直接 raw flash / 自建 journal |
| boot 计划与档位 | `tdma_boot_plan`、`tdma_boot_profile_store`、`tdma_boot_stage` | 另写一套开机决策 |
| 训练/标定判定 | `calibration_training_{marker,sck,data}`、`calibration_clk_{marker,coded}`、`calibration_link_p3` | 在引导执行器里重写判定 |
| 环窗口进出 | `app_guide_scan_leave_window` / 冻结 boot 步骤重放 | 另写"停环-复环"流程 |
| **动作计划载荷（C 段）** | `components/calibration_manager/{inc,src}/guide_action_plan.[ch]`（64 B 定长，magic/version/size/候选数/trial 数全部校验，CRC 仍由 `refmem_param_pool_crc` 统一算） | 另写一份计划编码/第二个候选表格式 |

新增登记格式：概念 | 新 canonical 路径 | 为什么现有实现不适用（必须写清，不能写"更方便"）。

### 9.28 引导命令面：命令登记与"重复算法合并"约定

**C/T 两段用哪两条命令**（已登记进 REFMEM 命令目录，编号 50/51）：

| id | 名称 | class | payload | 语义 |
|---|---|---|---|---|
| 50 | `CAL_GUIDE_CONFIG` | `CALIBRATION`(4) | `SLOT_REF` | 计划放在参数池 slot，命令只带引用；idempotent（同 seq 可重发） |
| 51 | `CAL_GUIDE_TRIGGER` | `CALIBRATION`(4) | `INLINE_SMALL` | 2 words：`action_id`、`epoch` |

**生成链（不要手改生成物）**：`config/refmem/command_catalog.yaml`
→ `tools/refmem/refmem_command_catalog_gen.py`
→ `components/distributed_refmem/inc/refmem_command_catalog.h` 与 `src/refmem_command_catalog_table.c`、`out/refmem/SCPI_REFMEM_CATALOG.csv`。
`refmem_command_type_t` 枚举仍**手写**在 `refmem_command.h`，新增命令要两处同时改（枚举 + YAML）再生成。

**重复算法合并（已落地）**：`components/algo/inc/algo_checksum.h` 是工程内已迁移校验站点的唯一实现
（header-only：CRC-32/ISO-HDLC 含 raw 寄存器变体、CRC-16/CCITT-FALSE、FNV-1a 的 bytes/str/u32_le/word
四种喂入）。golden 见 `tests/python/test_algo_checksum.py`。规则：

1. `refmem_param_pool_crc` 已改为调用 `algo_fnv1a32_bytes`，不得恢复第二份；
2. 计划身份测试（`tests/python/test_guide_action_plan.py`）继续保证"同字节同值、异内容异值"，值不变；
3. 任何新代码不得自带 CRC/校验和/FNV 实现；数值必须与上述 golden 对齐；
4. 迁移必须逐字节等价（值进 Flash、线上帧与会话身份）；raw 与 zlib 两种约定不得混用。

**例外（保留但必须注释）**：`tdma_transport_crc32_update` 是 core1/DMA 窗口的 SRAM nibble 表性能特化，
数值与 `algo_crc32_iso_hdlc_compute` 相同；不得再新增第二份性能特化。`third_party/portable_ota` 的
`pota_crc32` 是 `ota_crc32_*` 适配器的底层内核（第三方库，不属工程重复实现）。

**并发纪律（实测教训）**：另一会话在改 `components/**`、`config/**` 或构建文件时**不要跑硬件验收**——
验收期间源码变化会让指纹失效（`source changed while hardware acceptance was running`）。验收必须等
两边改动都落地后**一次覆盖**（命令面扩展 + 算法合并）。本轮另暴露 `p3_dpll_short.validate_summary`
对工作流注入字段做精确等值的自检缺陷，已在 `tools/hardware_acceptance/p3_dpll_short.py` 修复（只忽略
工作流注记字段，任何证据字段被改仍拒绝），需随接受会话提交并复核。







**执行器形状**：一个通用 `guide_action_runner`，输入 `(action_id, plan_ref, epoch)`，
按 `C→T→R→Q→S` 五个阶段推进，每阶段有**完成条件 + 超时 + reason**；动作表是数据
（类似 `k_guide_phase_ms`），不是五段各自写死。M1 扫描的暗窗原语（`STOP→配置→等 applied→
ARM(带屏障)→START→采样→STOP` + 重放 boot 步骤复环）可直接作为 R/Q 的实现骨架。

**与 §9.24 的关系**：§9.24 定"谁驱动"，本节定"一次动作怎么跑"。两者合起来 = 断开四口后，
四板对每个动作各跑一轮 CTRQS，全部通过后最后一轮的 S 才起 10 MHz。





**为什么自组稳、P0T 不稳（第四条结论）**：自组的稳定性来自"不做测量、幂等收敛"——USB 不在环里、
10 kHz 每秒上万次独立判决、认成功要求 `RESIDENT|SEEDED|ORIGIN|HOP|REQUIRED|SEQUENCE|IDENTITY`
七位闭环（`tdma_pio_spi_ring_adapter.h:42-56`、`rx.c:550-581`）、错误自愈不升级、启动零决策
（map 硬编码 slot→slot+1）。P0T 恰好相反：每对 1.5 s 一次判决、出错整轮丢弃。因此"把 P0T 搬到板端"
并不自动变稳——板端版的时基是"复位后 12.5 s + k×5 s"（`app.c:98-100`，注释里写明前提
`all boards reset together`），而逐板看到环流量的偏差实测 0.6–1.2 s，采样窗只有 1.6 s，
一次复位偏移就能把邻对突发算成本对（假阳性）。**若要板端化，先改成事件驱动开窗**（收到前驱帧/边沿
再开窗），否则只是把"可复现的 USB 丢 ACK"换成"更难复现的复位偏移"。



**S 段实板测试步骤（待 matrix 就绪后执行）**：

1. 完整验收链产出 `trn03-matrix.json`（`workflow_steps.py run` 的 21 步）；
2. 刷 bench 镜像变体 `PROJECT_TDMA_GUIDE_STAGE_BOOT=ON`（其余同 `m1-self`：v2 + AUTOFORM、
   `GUIDE_SCAN=OFF`），逐板写回 `SYST:BOARD:NO`；
3. `trn03_stage.py --board-id <NO.1..4 串口序> --config <matrix> --commit`，
   复核 `READ:CALibration:TRAINing:STAGe:PERSistence?` 为 `persisted_valid=1, loaded=1, reject=0`；
4. 同步重启四板 → 期望 `SYST:TDMA:OPMode?` 的活动档 = matrix 的 level（不是 23）、
   `BOOT:TOPology?` = `1,0`、`RING:STATus?` `enabled=1 node_count=4`；
5. 负测试：`trn03_stage.py --clear` 后重启 → 活动档回到 23、`BOOT:TOPology?` 仍 `1,0`。























