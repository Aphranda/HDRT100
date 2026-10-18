# TRIGGER T2 任务进展归档 06

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_06.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-19

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20261013-001` .. `T2-PROGRESS-20261013-001`（按新鲜度倒排，最新在前），共 1 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。

### T2-PROGRESS-20261013-001：位置邮箱契约登记完成并经 C11 独立交叉审核（`T2-AUDIT-07`）

- TODO task ID：`T2-AUDIT-07`；契约 `ARCH-TRIGGERMAILBOX-01`（status `pending`）。
- **登记**：`docs/check/DOCS_REGISTRY.md` 新增该行；clause 载体 `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`「位置邮箱交接契约」小节；code_anchor `components/sync_trigger/inc/trigger_position_mailbox.h`。
- **C11 独立交叉审核**：核验方为**独立 subagent**（与起草方不同会话、未参与起草），方式为代码与文档**只读**交叉核验（未创建/修改/删除文件，未运行构建或硬件工具）；12 项断言逐条核验，结论 **`PASS_WITH_NOTE`**；提交单 `docs/check/submissions/TRIGGER_CROSS_REVIEW_02.md`。
- **按结论修正 4 项**（均属措辞/归属，无实现缺失、无编造）：① `[1, LATCH_IDS]` 不再写作通用「有效 `position_id` 域」，改为 FREEZE latch **槽位域**并划分所有权（线上域归 `ARCH-TRIGGERSLICE-01`，本条不重复立法）；② 小节标题「冻结」→「登记，status `pending`」，并消除同文档原「本文当前仍为 Draft」的自相矛盾；③ 「载荷仅在 `WRITING` 写／`READING` 读」限定为**运行时（并发交接期）**，补记静默边界清空与 `WRITING`/`READING` 期拒绝复位；④ 显式声明 FREEZE latch 为同头文件内**独立于 mailbox** 的结构体（载荷规则「bit 清写／bit 置读」）。
- **新产出：位置域三方口径不一致（另立 `T2-IDENT-002`）**：线上／latch 寻址域 `[1, 15]`（`trigger_position_slice.h:38` 的 `0x0Fu` 与 `trigger_position_mailbox.h:74` 的 `TRIGGER_POSITION_FREEZE_LATCH_IDS`，二者**无静态断言绑定**）；`trigger_position_descriptor_valid()` 的 FREEZE 分支硬编码 `position_id <= 3u`（`trigger_position_descriptor.h:193`，**未引用宏**）；T2 运行时消费域 `[1, TRIGGER_T2_RUNTIME_POSITION_COUNT]`（`= 3u`，`trigger_t2_runtime.h:12`；拒绝见 `trigger_t2_runtime.c:403-404`）。数值当前自洽，但改宏不会带动该字面量。
- **本次核验未覆盖**：同源码构建、四板 P3、pytest 运行；内存序仅作形式配对核验，未做多核压力验证。
- **附带更正**：`docs/README.md` 中 `TRIGGER_T2_HANDOFF_CHECKLIST.md` 的索引描述仍写「用户偏好」，而该小节已于 `488a0559` 移除（该提交只改清单、未同步索引），随本次一并更正。

### 2026-10-13 自 T2 待办下沉的长记录（避免 C12 跨文件复制）

### 体量合规（2026-10-13

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### 执行交接（ROI 状态

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### ② 的台架语义验收用例

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### 判据抽取重构：P3 验收未通过

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### T2-AUDIT-04 决策输入
**台架验收基线（用户裁定）**：无转台时以 **OUT4→IN1 硬件回环模拟转台输入**；接真实转台时由转台直接驱动 **IN1**。参见 Task Progress 的两条取证。


本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### T2-AUDIT-03 前置：契约冻结清单

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。
### C14 R3 命名规则

本节长记录已按证据与任务分工下沉至 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`（证据唯一落点）。


### 2026-10-13 用户指定：后续单串口控制拓扑（其余板卡走 RefMem 分布式控制）

用户裁定（逐字要点）：**后续只有一个串口连接到 PC 上，其他板卡都用 REFMEM 进行分布式控制**。

对设计的直接含义（供后续落位使用）：
1. **RefMem 是控制面镜像，不是运输**：跨板传输仍由 TDMA 帧（SHORT process image）8 × 32 B）承担；
   写入本地 RefMem 表後由 TDMA 携带增量，对板 RefMem 表随之更新（见“八槽 compact mailbox 消费规则”与 delta 规则）。
2. **每帧预算有限**：控制命令必须适配每帧预算（每 slot 前 8 B 元数据 + 后等待 core0 内容；T2 切片为 10 B 子区），
   否则需分块；因此 **TLV 区（3072 B）适合保留态/控制面，不适合批量流式数据**。
3. **需要命令转发契约**：单串口下，其余板卡的 SCPI/配置必须由“接 PC 的那块板”转发；
   该转发的**寻址/路由/应答与权限**属跨域契约，需登记并走 C11。
4. **与现有机制的关系（待核）**：本仓已有 `refmem_node_load_sync`（节点装载同步）与 TDMA 一等席机制；
   其能否直接承担该转发职责，尚需逐条比对后才能声明。


### 2026-10-13 台架接线已确认：OUT4 ↔ IN1（模拟转台）

用户确认：**OUT4 与 IN1 已接在一起**（先前一条提到的 "OUT5" 已更正为 OUT4）。

核验（本仓现状）：
- `boards/rp2350_trig/inc/board_config.h:314` ：`BOARD_SYNC_OUTPUT_PIN_COUNT = 4u`，即本地同步输出为 **OUT1..OUT4**；
  `:316` `BOARD_SYNC_PULSE_OUT_PIN = BOARD_SYNC_OUTPUT_BASE_PIN + 1u`，`:317` `BOARD_SYNC_MODE_OUT2_PIN = +2u`。
- 全仓对 **"OUT5" 零引用** （`git grep` 无命中），故 "OUT5" 不存在；用户已更正为 OUT4，与此一致。
- 输入侧：`:300` `BOARD_SYNC_INPUT_BASE_PIN`；`:305` `BOARD_SYNC_GATE_IN_PIN = +0u`、`:304` `EXT_CLK_IN = +1u`、`:303` `ARM_IN = +2u`、`:302` `TRIG_IN = +3u`。

**结论**：台架无转台时，由 **OUT4 发脉冲经硬件回环到 IN1**，作为转台输入的模拟；
接实际转台时则由转台直接驱动 **IN1**。因此 `T2-TURNTABLE-008` 的台架验收基线为
"**OUT4 → IN1 回环模拟**"，而非真实转台输入。此前关于接线方式的待确认项已闭合。


### 2026-10-13 T2-AUDIT-04 口径已定（用户裁定）：生产接 IN1、台架用 OUT4 模拟转台

用户裁定（逐字要点）：**后续接实际转台时，输入 IN1；现在没有转台的情况下，用 OUT4 模拟转台**。

两条推论：
1. **生产/实际转台** ⇒ 转台输入走 **IN1**，与参考实现的 **COUNTER 角色 / 输入 IN1** 一致（见前条取证）。
2. **台架（无转台）** ⇒ 用 **OUT4 模拟转台**，作为本地激励；因此台架验收的基线是“**OUT4 模拟**”，而非真实转台。

**待确认的实现细节（用户未说明，不推测）**：OUT4 与 IN1 的**物理连接方式**——是 OUT4 回环接入 IN1，
还是 OUT4 作为独立激励而由另一路取样；该细节影响 `T2-TURNTABLE-008` 验收的接线描述与证据口径。

**对后续工作的影响**：T2-AUDIT-04 的“选型”问题已结束；
接下来只剩把台架验收以“OUT4 模拟转台”为基线写清，并在接真实转台时切到 IN1。


### 2026-10-13 T2-AUDIT-03 决策单（五项冻结，可逐项批准）

用法：每项给出「现状锚点 / 可选取值 / 影响」；你只需按编号选一个取值。

| # | 项 | 现状锚点 | 可选取值 | 影响 |
|---|---|---|---|---|
| 1 | **wire 布局** | K-12 已定 10 B 单帧（tag + 4 bit `position_id` + 16 bit 版本 + 32 bit `target_ms_index` + 24 bit fine slice） | A：按此布局冻结并定义 RefMem 精确偏移/长度；B：改为可变长帧 | A 可直接登记；B 需重开 wire 设计与兼容性评估 |
| 2 | **与 TDMA 一等席布局联审** | 一等邮箱位置、`SYSTem:TDMA:SEAT:ROTation`（默认 1:1）、两板 effective schedule CRC 一致性门 | A：按现状联审并冻结；B：先调整轮换比或邮箱位置 | A 可直接推进；B 会触及 TDMA 域契约与 CRC 门提価 |
| 3 | **调试 profile 默认值** | 四个 offset/window 、`threshold_per_mille`（当前 950/1000）、`pre_roll_mdeg`/`post_roll_mdeg`（台架 5°，调试快照非产品额定值） | A：按台架快照冻结为**调试默认**；B：另行给出产品额定值 | A 可登记为调试契约；B 需额定依据与新台架验收 |
| 4 | **资源配额** | `TRIGGER_T2_RUNTIME_POSITION_COUNT`、`TRIGGER_POSITION_FREEZE_LATCH_IDS`、控制字节偏移、RAM 按实测 | A：按当前实测值冻结；B：预留余量后冻结 | A 可登记；B 可降低后续调整风险 |
| 5 | **正式时限** | STEP 下限（由转速与位置内序列跨度推出）、Core1 预算（顶层 `HIGH_PRIORITY` 500 µs）、`T2_FREEZE_RESEND_SLICES`、窗口到期判据 | A：按现状冻结；B：按高速扫描另计 | A 可登记；B 需重新取证时限 |

注：本单不改变任何实现；你逐项选 A/B（或给出第三种）后，我起草契约行并送 C11 交叉审核。


### 2026-10-13 T2-AUDIT-04 参考实现取证：转台输入与测量脉冲的已定角色

取证来源：分支 `origin/feature/node-sequence-reservation-trigger`（commit `19b9c5ff`）内的
《TRIGGER_SEQUENCE_ARCHITECTURE》第 **NSEQ-RSV-01** 节。

该节的槽位职责表（逐字要点）：

| 槽位职责 | 本地端 IO | 执行事实 |
|---|---|---|
| **COUNTER** | **IN1（转台）** | 累计计数达到位/阈值后才进入业务位 |
| SP8T 通路控制 | OUT1、OUT2 | 记录所选状态 |
| SP2T 极化控制 | OUT3（H/V） | 记录对应状态 |
| **VNA_GATEWAY** | **OUT4（测量脉冲）、IN2（READY）** | 只对应本地预约 |

两条推论：
1. **转台侧的含义已定**：转台作为 **COUNTER 角色、输入 IN1**；测量脉冲在 **OUT4**。
   后者与现有 `TRIGger:REServation:SOURce:*` 的 OUT4 硬件 PWM 路径**一致**。
2. **模式化的做法已定**（A-04/A-06）：通过 **SYNC_IO owner/persona 机制**实施热加载，
   常驻 SM 角色为 `TURNTABLE_SM`、`INGRESS_SM`、`EXECUTOR_SM`、`COUNTER_SM`；
   资源以 `sync_io_persona_resources.c` 与 `SYNC_IO_PERSONA_ID_SEQUENCE_COUNTER` 为准；
   切换顺序：停 SM → 停 DMA/IRQ → 清 FIFO → 卸载/装载 → 初始化执行位置 → **原子发布 generation**。

本条与本仓库现状的差异：本仓 `components/sync_io` 尚无 `sync_io_sequence.*`，
也尚无对应 persona；因此 T2-AUDIT-01 落地时应**按上述机制**实现，而非自制薄配置。


### 2026-10-13 T2-AUDIT-04 补充：“模型源”API 命名跨两条物理通道

取证（逐字）：

| 入口 | 实际转发到 | 物理通道 |
|---|---|---|
| `sync_io_model_pulse_schedule_arm` / `…_arm_ns` | `sync_io_output_pulse_schedule_arm[_ns]` | **波形输出**（PIO0/**SM2**，`BOARD_SYNC_PIO0_WAVE_OUTPUT_SM`） |
| `sync_io_model_pulse_schedule_arm_periodic_ns` | `sync_io_sma_observer_pulse_schedule_arm_periodic_ns` | **证据脉冲**（PIO0/**SM1**；`sync_io.h` 原文“External DPLL evidence pulses use PIO0/SM1 and SMA OUT1”） |

两条推论：
1. T2-AUDIT-04（转台源路径选型）必须声明验收所需的是**波形输出（SM2）**还是**证据脉冲（SM1）**。
2. T2-AUDIT-01 的模式声明只能声明 **SM2**（SM1 归证据通道），否则资源声明与物理不符（`ARCH-PIOPARTITION-01`）。

地址取证：`sync_io.h:216-233`（API 签名）、`sync_io.h:234`（SM1 注释）、`settle`——`sync_io_model_sched.c:1120-1131`（periodic 转发）、
`boards/rp2350_trig/inc/board_config.h:254/275`（`BOARD_SYNC_PIO_FAST=pio0`、`BOARD_SYNC_PIO0_WAVE_OUTPUT_SM=2u`）。







