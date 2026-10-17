# TRIGGER T2 任务进展归档 02

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_02.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-18

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20260927-017` .. `T2-PROGRESS-20260927-017`（按新鲜度倒排，最新在前），共 1 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。
> 轮转规则见 `docs/check/DOCS_REGRESSION_PLAN.md` §0 C14。

本文件按 C14 从 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 轮转出来的旧条目；
条目按新鲜度倒排（C15：最新鲜的在最上面）。证据原样保留，不做改写。

### T2-PROGRESS-20260927-017：异步窗口预测与本地最新镜像回退

- TODO task ID：T2-TURNTABLE-002/003、T2-TRANSPORT-001、T2-ACCEPT-001。NO1 Core1 只取得每个 VDC 基准同步的计数观察；Core0 在 `app_refmem_service()` 边界按最近完成的计数窗口异步计算预约位置时间片。距离阈值越近，窗口和计算间隔缩短；没有新结果时复用上一次完整预览。
- 阈值跨越现在提交 `TRIGGER_POSITION_STAGE_FREEZE`，TDMA 普通 T2 帧只发送 `position_id` 和 `prediction_version` index；完整预览仍沿现有 T2 descriptor 分片发送，不改变 VDC cadence 或 DPLL/SYNC owner。每个板本地保留最新预览和两份旧预览，CRC 覆盖窗口字段。
- 从板收到冻结 index 后不在临界路径重新计算：有匹配镜像则采用匹配版本；缺失或版本不一致时使用本地最新有效镜像，并在 T2 snapshot/SCPI 查询中记录远端版本、本地版本、本地镜像年龄、fallback 和 missing 计数。
- host 验证：`test_trigger_t2_runtime.py` 已改为积累完成窗口、验证预览后冻结及版本不一致回退；T2 descriptor/mailbox/flight 合计 `23 passed`。Release 增量构建、三个 link contract、docs_check 和 doc_regression 均通过；进展日志 WARN 为仓库既有轮转债务。
- 当前硬件证据：当前源码指纹 P3 build=`20260926171324` 最终 `PASS_WITH_WARNINGS`（INFO 31 / WARN 15 / ERROR 0 / FATAL 0），证据在 `out/HardwareAcceptance/20260927/p3-011317/`，四板 DPLL 60 秒和 TDMA process-image/FIFO 阶段完成。P3 结束后重新启动 T2 专用 ring 时 NO4 ARM 超时，当前仍没有共同 VDC observation/session，不能宣称真实 `position_id`、READY 或示波器动作闭环；失败原件保留在 `out/t2-ring-p3-20260927/`。

## 验证与证据索引

| 类型 | 位置 | 范围 |
|---|---|---|
| 分支参考 | 上述 commit 的 Trigger sequence 三件套（目录 `docs/trigger/sequence/`，文件前缀 `TRIGGER_SEQUENCE_`） | 仅存在于来源树的方案与单板证据，不是本分支文件或多板验收 |
| 当前基础能力 | `docs/vdc/VDC_TASK_PROGRESS.md`、`docs/tdma/TDMA_TASK_PROGRESS.md`、`docs/refmem/REFMEM_TASK_PROGRESS.md` | 各域现有实现与证据；按实际身份和配置复用 |
| 本次文档检查 | `out/doc-audit/t2-triplet-20260922/` | 三件套、导航和文档门禁，不含 T2 硬件结果 |
| OUT1 有限细分 | `out/out1-subdivision-20260922/summary.json` | 四板局部间隔、非整 tick、RAW 原件与恢复；不含共同同步起点 |
| OUT1 整窗与尾部余量 | `out/out1-window-20260922/r1/verified-summary.json` | 四板两端标记、中间细分、末格留空和恢复；不含 live RUN |
| 共享格规划 | `out/t2-grid-20260922/hil-r2/summary.json` | 四板同目标、本地 deadline 独立复算、七类负测/板和恢复；无输出、非共同参考资格证明 |

## 失败、回退与未验证项

T2 预约本身尚未运行；有限 OUT1 维护实验的真实 build、波形、脚本失败和恢复见 T2-PROGRESS-20260922-003/004。整窗增量构建替换上一版包的历史证据缺口在 004 显式记录，不能宣称旧包仍可重验。多板槽位执行、预约 wire、持续同步与细分共存、正式时钟资格、实际 T2 latch 与 completion 均未在本轮验证。失败按独立运行目录保留，不以摘要重写通过。

## 归档状态

暂无归档。

## 2026-10-13 T2 架构逐级审查（自架构文档下沉，13/13 块）

### 2026-10-13 审查结论（`T2-TIME` / `T2-PREDICT`，对照 HAOFV 顶层）

结论：**两块均与顶层强一致**，无内容性冲突；仅一项治理类待办。

- **`T2-TIME` 时间坐标**：本地网格由 `TIMER1` 经本板 committed DCO 投影到共同 anchor 后派生；明确“接收观察的 `sync_seq` 与本地毫秒格不同种”、“观察未更新时本地网格仍推进”、“本地 `sample_seq` 不能跨板直接比较”。这与顶层的 `local_tick_raw` 自由运行、`system_tick` 只读派生一致。“身份失配或本地时间回退必须显式失效，不补发越过的动作、不重复执行”对应顶层 generation fencing。“软件轮询频率不决定硬件细分分辨率”与“映射有效 ≠ 完整动作精度”对应“不得用 CPU 时刻充当边沿证据”。
- **`T2-TIME` 位置索引**：网络描述符的业务内容是**位置索引**，不是纳秒、本地 `TIMER1` tick 或主站算出的每板 deadline；身份字段只用于防旧版本复活。这是有意的分工（`T_fire_target_vdc` 属 Trigger 本地，线上只走位置索引），与顶层 T2 行不冲突。
- **`T2-PREDICT`**：预测**不在 VDC 发布路径内**；Core1 仅取有界计数观察并记阈值事实，Core0 按最近**已完成**窗口异步解算；无新窗口不伪造新版本；阈值广播只标记应采用版本，**不发全量镜像、不触发槽位热加载**。与顶层“Angle DPLL 只生成扫描预测时间、不得写 VDC offset/rate”一致。
- **`T2-PREDICT` 扫描与在途容量**：角度全用**整数毫度**；配置阶段 **STOP 阶段 fail-closed 校验**（整除、`step_pulses` 正整数、累计不溢出、STEP 时间下限），**运行期不取整、不兜底**；阈值/lead 改为**按一步比例**（避免远端 lead 线性膨胀导致数十个位置同时在途）；**K-7/K-12 已定稿**：只预测下一到两个位置、361 个位置不进常驻状态、`position_id` 保持 4 bit 作在途实例编号、绝对位置由 32 bit `target_ms_index` 确定。“按实测尺寸而非‘一位一槽’线性放大 RAM”对应顶层“不把 Vector 当全局变量”的容量纪律。

**唯一待办（治理类）**：紧凑记录的**跨域契约尚未登记**（须在 `docs/check/DOCS_REGISTRY.md` 登记并经独立交叉审核 C11），
且与 **TDMA 一等席布局的联审**仍为未决项；登记完成前不得写成冻结契约。

### 2026-10-13 审查结论（`T2-ACTION` / `T2-OFFSET`，对照 HAOFV 顶层）

结论：**与顶层强一致**；另有一项**未冻结选型决策**待定。

- **静态模板与不热加载**：模板在 **TDMA START 前**装载并冻结（template ID/generation/CRC、循环跨度、动作 kind/role/本地 IO route、相对偏移、占用与 settle 窗口、SWITCH 编码/GATEWAY 脉宽）；
  运行期位置实例**只标记时间片，不修改 PIO 程序与业务槽位**。与本文 `T2-DEPLOY` 及顶层模式声明/DeploymentGate 一致。
- **位置内多组（K-6，16 组）**：各组均锚定于同一 `q_P` 加模板偏移；**多板模式下组间不由 READY 推进**（K-1），READY 窗口只发现踩踏、不推迟下一组；组间隔按网分**最坏**响应 + settle + 余量排布。
- **预装 + 两组滑动窗口**：执行端同时只持有**两组**已解算绝对 deadline；组 k 执行后在网分测量期内用最新 DCO 解算组 k+2；**装填前比较模型 token**，不一致用新解算，赶不上**判迟并记录、不立即补发**；某组 deadline 一旦被硬件接受**不再追改**（K-2）。与顶层“guard 后不得静默改写已装载 deadline”一致。
- **前提与跨域接口**：DCO 模型切换**必须连续**（不得在模型块边界产生相位跳变），否则同一位置内相邻两组会出现等于跳变量的间隔误差；**连续性由 VDC 域证明**。
- **动作解耦**：不用“下一动作”串行提交；每个动作只负责自己的时间片/输出/证据，顺序由静态 profile 预定，**不转为运行时等待或临界期软件补救**。与顶层“core0 拥堵不得停止 core1 或已装载 PIO deadline / FB 非阻塞”一致。

**未冻结选型（待定，`T2-TURNTABLE-008` 相关）**：本节已记“现有两条源路径——`TRIGger:REServation:SOURce:*` 的 OUT4 硬件 PWM 源 与
`CONFigure:MODEl:TURNtable:*` 的加减速模型源——**选型尚未冻结**，且选型会改变**隔离验收的证据口径**”。
该决定属产品/验收口径，必须在实现前定稿；本次审查不提前预设结论。

### 2026-10-13 审查结论（对照 HAOFV 顶层架构）

本文保留**语义级**结论；实施级规则（资源准入、越界判定、验证与证据分级）已按其职责移入约束文件。

- **资源准入（PIO/SM/DMA/IO 与启动期资源）**：见 `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` §4.3 与 §6。当前实现在 arm 路径内调用 `pio_add_program()`（即运行期热加载），与本节 `T2-DEPLOY` “所有 PIO 程序…属于启动期资源；TDMA RUN 中不做热加载”相冲；整改方向与判据见该文件。
- **故障语义与错误码**：按 `docs/arch/HAOFV_ARCHITECTURE.md` 的 Trigger 域 100–199 与 F0–F3 分级执行（不在本文重述）；实测 `fault_code = 3` 即 PIO/DMA 配置失败。
- **验证顺序与证据分级**：见 `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md` §4.5。
- **证据**：A-06 / A-16 的硬件闭环（`total = 2896 = 181 × 16`、`fault = 0`）记录在 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`。

**状态语义映射（架构级，保留于本文）**：运行时 8 态与预约状态机（canonical 见 `ARCH_T2_RESERVATION_ARCHITECTURE.md`）的对应关系为：
`STOP_CONFIGURED`↔`PREPARED`、`RUN_SYNCING`↔`PREPARED`/`WAIT_FENCE`、`PREDICTING`↔`PREPARED`、`POSITION_BROADCAST`↔`DISTRIBUTING`/`WAIT_FENCE`、
`LOCAL_MARKED`↔`ARMED`、`FIRED`↔`FIRED`、`FEEDBACK_CHECK`↔`WAIT_T2`、`COMPLETED`/`UNDETERMINED`↔`COMPLETED`；
预约态中 **本文尚无对应的 5 态**：`DRAFT`、`ARMED`（预约意义的装载提交）、`REJECTED`、`CANCELLED`、`FAULT`，属预约闭环范围。

### 2026-10-13 完善：状态语义对齐与故障分级

**（1）运行时状态 ↔ 预约状态机的映射**（两轴并存，不合并；预约态 canonical 见 `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` 的"预约状态机"）

| 本文件运行时状态 | 预约状态机（canonical） | 说明 |
|---|---|---|
| `STOP_CONFIGURED` | `PREPARED` | 配置、资源与身份读回完成；尚未进入运输 |
| `RUN_SYNCING` | `PREPARED`/`WAIT_FENCE` | 共同时间与 ring 身份就绪，等待 READY/fence 闭环 |
| `PREDICTING` | `PREPARED` | NO1 本地预测推进，尚未产生可发布位置 |
| `POSITION_BROADCAST` | `DISTRIBUTING`/`WAIT_FENCE` | 位置记录进入 TDMA 一等席运输与 fence |
| `LOCAL_MARKED` | `ARMED` | 从板已标记本地动作；对应"本地 deadline 已冻结并装载" |
| `FIRED` | `FIRED` | 输出已执行并获得输出 latch/回执 |
| `FEEDBACK_CHECK` | `WAIT_T2` | 等待设备 READY/T2 latch |
| `COMPLETED`/`UNDETERMINED` | `COMPLETED` | 完成事实与质量闭合；证据不足时保持未确定 |

**预约态中本文件尚无对应项的 5 个状态**（属预约闭环实现范围，按顶层"最后开放预约执行"顺序推进）：`DRAFT`、`ARMED`（预约意义上的装载提交）、`REJECTED`、`CANCELLED`、`FAULT`。

**（2）故障分级与错误码空间**（对齐 `docs/arch/HAOFV_ARCHITECTURE.md`）

- 错误码空间：**Trigger 域 100–199**；示例语义 `1=非法参数`、`2=资源忙`、**`3=PIO/DMA 配置失败`**、`10=ENC target=0`、`11=非法编码器引脚`。模型状态页的 `fault_code` 使用该空间（实测 `fault_code = 3` 即 PIO/DMA 配置失败）。
- 故障分级：**F0 可恢复**（记 `error_code` 后继续，可自动清除或 SCPI `*CLS`）；**F1 域级故障**（本域进入 FAULT，其他域正常，**须显式 CLEAR 事件**，`*CLS`/`*RST` 无效）；**F2 系统级**（`SYSTEM_MODE_FAULT`，停危险输出，需复位或维护模式）；**F3 致命**（Watchdog 复位）。
- 多故障并发：`error_code` 记最严重，`fault_summary` 位掩码保留全部故障位（资源类用 `SYS_FAULT_RESOURCE_MASK`）。

### 2026-10-13 审查结论（`T2-READY` / `T2-START`，对照 HAOFV 顶层）

- **`T2-READY`**：多板下只做**异步时间片踩踏校验**（逐组关联本组 TRIG）；五步（记录上一动作身份与计划窗口 → 窗口内采集 → 映射回共同坐标 → 检早到/迟到/缺失/重复/相邻重叠 → 生成诊断与业务证据）；
  **READY 不作当前动作执行许可、不形成当前动作 fence、不替代 PIO 执行回执、不自动表示 VNA 完成**；无硬件时间戳时只报告状态观察或不确定区间。与本文 owner 表与顶层“不把 READY 当当前授权 / 不填缺失 latch”完全一致。
- **`T2-START`**：启动前装载（deployment、PIO/SM/FIFO/GPIO persona、静态模板含每位置全部组、角度扫描参数、grid/profile）并校验 CRC/generation；
  运行中：VDC 每 1 kHz 同步更新一次 local cursor，**未 LOCKED 并发布时 T2 不生成也不装填 deadline（K-3）**；NO1 取观察、Core0 异步更新预测、阈值跨越后一帧广播；从板解析并标记各组时间片；
  两组滑动窗口装填，**DCO 更新只作用于尚未装填的组**；**不通过 STOP/START 重新加载槽位或改变 IO route**；**不在目标时间片由主机发送即时命令**。与顶层“SCPI 不能驱动硬实时边沿 / core0 拥堵不得停 core1 或已装载 deadline”一致。
- **跨域契约与验证映射**：本文仍为 **Draft**；位置描述符 wire、调试 profile 默认值、资源配额与正式时限**在实现与独立复核后登记**，登记前不把数值快照当产品契约。
  该声明与 AGENTS.md 的文档自回归纪律（契约登记 + C11 交叉审核 + 硬数字不手写）一致。

### 审查收尾（2026-10-13，13/13 块完成）

对照 `docs/arch/HAOFV_ARCHITECTURE.md` 与 `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` 逐块审查完毕：
**语义与 owner 边界层面无内容性冲突**；遗留项为：
① 实现偏离“运行期 PIO 热加载”（整改方向见约束文件）；② “每位置一组”已由硬件闭环（A-06/A-16）；
③ 紧凑记录跨域契约尚未登记与 TDMA 一等席布局联审；④ 转台源路径选型（T2-TURNTABLE-008 隔离验收口径）未冻结。

## 2026-10-13 T2 待办长记录（自待办下沉）

### A-2a 判别规则（2026-10-13 实测得出，搬移前必读）

**不能按标题层级机械切分** ✗：`A.4` 段内既有**接线事实小节**（如 `A.4.2 STEP 如何抵达运行时（定案：独立 setter，不动结构体）`），
也有**按时间追加的日志条目**，两者**同为四级标题、同为两段编号** ✗（如 `A.4.0` / `A.4.1` / `A.4.3`），
仅凭 `#{2,4}\s+A\.\d+\.\d+` 无法区分 ✗✓。

**安全判别（择一，且须逐条列出后再搬）**：

1. **日期后缀**：日志条目标题通常含 `20xx-xx-xx`（如"…（2026-10-13 定）"）；接线事实小节不含 ✗；
2. **显式清单**：搬移前先输出"待搬条目 + 行号"清单，经人工确认后再执行 ✓；
3. **只搬不改**：搬移仅改**归属**（移入冻结归档章节），**不得改写或删除正文** ✗，以便任何一步都可回滚 ✓。

**结论**：A-2a 的正确顺序是 ① 生成清单 → ② 人工确认 → ③ 只改归属的机械搬移 → ④ 更新块地图行区间 ✓。

### 体量合规（2026-10-13 实测：TODO 376 KB = 上限 1.88 倍，必须把日志移出）

**约束依据**：`docs/check/DOCS_REGRESSION_PLAN.md` C14（`:56`）与 R1（`:106`）——**Task Progress 单文件 ≤ 200 KB（204800 字节）**，超限 FAIL；R2（`:112-114`）明确**字节上限是硬约束**、保留窗口只是指导值，故归档用"**最旧优先连续**"。三件套文件接口见 `DOCS-TRIPLETFORMAT-01`。

**实测（2026-10-13）**：

| 文件 | 体量 | 行数 | 判定 |
|---|---|---|---|
| `TRIGGER_T2_ARCHITECTURE.md` | 45 KB | 441 | 合规 |
| `TRIGGER_T2_TASK_PROGRESS.md` | 121 KB | 536 | 合规（余量 79 KB） |
| **`TRIGGER_T2_TODO.md`** | **376 KB** | 5072 | **超限 1.88 倍，必须瘦身** |

**根因**：14x 条按时间追加的日志条目（`A.3.x / A.4.x`）以四级标题形式**挂在 `A.4 接线落点` 之下**，使该章节名义范围达 **4748 行**；日志与"任务清单"混居是体量超限的直接原因。

**处置（只改归属，不删证据）**：

1. 日志条目**移出 TODO** ⇒ 迁入 `TRIGGER_T2_TASK_PROGRESS.md`（本就以"执行证据"为唯一职责）；若该文件亦将超限 ⇒ 按 C14 以"最旧优先连续"归档到 `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_<NN>.md` 并维护归档索引（ID 闭包、条目数相符）；
2. TODO **只保留**：K 条、M0–M5、本结论区、已验证证据页，以及体量很小的迁移附录（`A.1` 实施约束 4 行 / `A.2` K-10 偏差 11 行 / `A.3` 迁移清单 47 行 / `A.5` 已结案 8 行）；
3. **验收判据**：TODO **不含探索日志**且体量降至任务清单量级（建议 < 100 KB）；Task Progress 接收后仍 ≤ 200 KB 或已按 C14 归档；文档门禁（含 R1 字节检查与 C14 轮转）全绿。

**搬移判别**：四级标题内**接线事实小节**与**日志条目**形状相同（同为两段编号）⇒ **禁止机械切分**；以"标题含 `20xx-xx-xx` 日期"为首要判据，并**先出清单、只改归属、可回滚**。

### 执行交接（ROI 状态，2026-10-13）

按用户指定的 ROI 规则记录下一步执行的状态、未验证范围与重启条件。

| 项 | 状态 | 预计时间 | 价值 | 改变的决策 | 最早停止点 |
|---|---|---|---|---|---|
| ① PIO 模式化 + STOP 装载 | `EXECUTE`（待启动） | 1.5 / 2.5 / 4 h | 5 | 执行端能否声明**架构合规** | 模式注册完成 + 连续 `START/STOP` **槽位不减** |
| ② K-2 重算判据 | `EXECUTE`（待启动） | 0.5 / 1 / 2 h | 5 | FREEZE 后未装填组是否仍随最新 DCO 重算 | `:1048`/`:1082` 改用既有掩码后，扰动 DCO 用例通过 |

**本会话不执行的原因**（`DEFER_OR_RECOVER`，不改状态）：两项均为**固件范围**，
按 AGENTS.md 硬约束，提交前必须执行 `python tools/hardware_acceptance/p3_hardware_acceptance.py run`
（四板、约 25 分钟）；在无法完成该验收的会话内提交固件会违反约束，故**不提交未验证改动**。

**未验证范围**：上述两项改动本身（均尚未实施）；已验证部分不受影响（本会话已硬件证实：16 组 `total=2896=181×16`、
六次循环 ARM OK）。

**重启条件**：获得执行许可且可安排一次四板 P3 验收窗口；建议两项**合并一批**（同源码指纹 ⇒ 一次 `run`）。

### ② 的台架语义验收用例（K-2，2026-10-13 设计定稿）

**现状**：已提交的 K-2 改动（`73dcd61a`）已过**回归**验收（P3 四板 PASS_WITH_WARNINGS，ERROR=0/FATAL=0），
但**语义本身无测试覆盖**：`tests/**` 中 `action_armed_mask`/`action_completed_mask`/`prepare_deadlines` **零命中**，
`tools/sync_trigger/**` 也**无 DCO 扰动工具**。

**采用方式（照既有先例，2026-10-13 已核实机制）**：`tests/python/test_trigger_t2_freeze_abandon.py` 的做法是——用 `tools.state_machine_resource_check` 的 **`c_definition_body` 抽取真实函数体**，配一个 C harness（**同名复刻 runtime 静态存储** + **为依赖提供桩函数**），编译运行后断言（见该文件 `:14-15/:17/:19-49`）。

**前提（新增，必须先做）**：本次 K-2 改动是**大函数内部的判据行**，**无法直接抽取**；需先把判据**重构为小函数**（如 `static bool t2_deadline_recompute_needed(uint32_t frozen_bit_accepted, uint32_t prev_version, uint32_t prev_token, uint32_t descriptor_version, uint32_t model_token)`），`prepare_deadlines_core0()` 只调用它；然后 harness 抽取**该小函数**并驱动 C1–C6。

**约束**：该重构属**固件范围**（行为等价），仍需独立走“改动 → 编译 → 烧录 → 四板验收 → 提交”周期，**不得与其他改动并行**。

**原则**：：`tests/python/test_trigger_t2_freeze_abandon.py` 已证明**抽取式 harness**
可直接驱动 freeze 分支（无需硬件）。K-2 同理：把 `prepare_deadlines_core0()` 的判据行抽取为可测函数，
在 host 侧直接驱动**谓词真值表**（无需真实 DCO 扰动）。

| 用例 | 输入（snapshot 掩码） | 期望 |
|---|---|---|
| C1 已冻结且**未装填** | `frozen=1`, `armed=0`, `completed=0` | **不跳过** ⇒ 按最新 DCO 重算并发布（K-2 前半） |
| C2 已冻结且**已接受** | `frozen=1`, `armed=1`, `completed=0` | **跳过** ⇒ 不追改（K-2 后半） |
| C3 已完成 | `frozen=1`, `armed=1`, `completed=1` | **跳过** |
| C4 未冻结 | `frozen=0`, `armed=0` | 重算（原行为不变） |
| C5 token 未变 | `prediction_version` 与 `model_token` 均未变 | 跳过（原行为保留） |
| C6 token 已变 | `model_token` 变 | 重算（原行为保留） |

**判据**：C1 必须**重算/发布**（本次修正的直接目标）；C2/C3 必须**跳过**；C4–C6 保持原行为。
该用例集**免硬件凭证**，属 host 测试，可纳入 `pytest` 常规门禁。

### 判据抽取重构：P3 验收未通过（分支 B，2026-10-13）

**事实**：`
- 首次：`FAIL Latency Cal hardware acceptance: step failed`；子步 `child_capture` 报 `RuntimeError('Capture exited 2')`。
- **重试（1/1，预算已尽）**：编译与四板 OTA 完成后**卡住**；最后一次写入为 `16:04:04`，
  此后阶段目录始终只有 `ota-four-board`，产物年龄与经过时间 **1:1 增长**（56→70→85→106→119→132→143→… s）
  ⇒ 直接证据表明**并非缓慢推进而是无任何新写入**。
- 分级凭证始终未写入（现存为 `HAOFV_P3_JOINT_PROVISIONAL_V1`，非 `..._GRADED_V1`）。

**未验证范围**：该重构的硬件回归（编译已过：`build exit=0`）；**行为等价性仅由代码审阅保证**。

**处置（按用户指令与 ROI）**：
- 重构**保留在工作树且已暂存**，**未提交**；不影响已提交的 K-2 语义修复（`73dcd61a`）。
- **不并行修补其他部分**；不连累其他改动。
- **重启条件**：台架恢复（排查 `Latency Cal`/`child_capture` 与 OTA 后卡住）后重跑一次 `run`；
  若仍卡在同一步，则先排查环境而非本次改动（本次为**纯抽取、零顺序变化**）。

### T2-AUDIT-04 决策输入：两条转台源路径的对照（2026-10-13，不预设结论）

供用户定稿；本表只罗列架构条款与现有代码事实，不替代决策。

| 维度 | **A. `TRIGger:REServation:SOURce:*`（OUT4 硬件 PWM 源）** | **B. `CONFigure:MODEl:TURNtable:*`（加减速模型源）** |
|---|---|---|
| 架构描述 | 本文 `T2-OWNER`：“STOP 预装本板 **OUT4** 的独立 PWM，RUN 只切换已装载波形使能；**不由 Core1 定时发脉冲**；不占用 VDC/TDMA 运行服务”。它**提供名义转台输入，不授予输入边沿时间戳或位置到位精度** | 模型组件 `components/model_turntable/`（本会话已实现并硬件验证：16 组 `total=2896=181×16`、分块续装订、`GROups`、PIO 一次装载 + 六次循环 ARM OK）；经 `sync_io_model_sched` 驱动 **PIO 波形输出** |
| 边沿与证据 | 仅名义输入（不授予边沿时间戳/到位精度）⇒ 适合**驱动计数/预测链路的名义激励** | 产生**真实 PIO 边沿**（可作为 sync_io 的实际输出事实）；但当前状态属**未冻结选型**，且其 PIO 声明尚未纳入模式设施 |
| 资源与模式 | 作为 `SOURce:*` 现有路径，与 OUT4 本地命名空间绑定 | 需成为**已声明模式**（`sync_io_mode_ops_t.hw` + 派发表），否则违反 `T2-DEPLOY`与 `ARCH-PIOCAT-01`/`ARCH-PIOPARTITION-01`（即 T2-AUDIT-01） |
| 隔离验收（`T2-TURNTABLE-008`） | 选型决定后，验收证据口径随之变化（本文原文已声明） | 同上 |
| 与约束的关系 | 不需新增 PIO 资源（现有路径） | 必须先完成 **T2-AUDIT-01**（模式化 + STOP 装载）才能声称合规 |

**待用户明确的三个问题**（本任务不预设答案）：
1. 验收（`T2-TURNTABLE-008`）要求的是**名义转台输入**还是**可作为事实的硬件边沿**？
2. 若选 B，是否同意先完成 **T2-AUDIT-01**（否则不具备声明合规的前提）？
3. 是否允许两路并存（A 作台架驱动、B 作产品输出），还是只保留一条？

### T2-AUDIT-03 前置：契约冻结清单（2026-10-13，免台架）

依据：`TRIGGER_T2_ARCHITECTURE.md` 尾节“位置描述符 wire、调试 profile 默认值、资源配额和正式时限
在实现与独立复核后登记”与 `T2-PREDICT`“紧凑记录的跨域契约须在 `docs/check/DOCS_REGISTRY.md` 登记并经独立交叉审核（C11）；
**登记完成前，当前实现不得写成冻结契约**”。

**必须先冻结的五项**（每项需明确到“可作为契约条款”的精度）：

| # | 项 | 当前状态与待定义点 |
|---|---|---|
| 1 | **位置描述符 wire 布局** | K-12 已定：**10 B 单帧**（tag + 4 bit `position_id` + 16 bit 版本 + 32 bit `target_ms_index` + 24 bit fine slice）；待定义：它在既有 RefMem 区的**精确偏移/长度**（`TRIGGER_POSITION_SLICE_WIRE_SIZE` 恰填）、**message subtype/身份字段**、CRC 归属与版本化 |
| 2 | **与 TDMA 一等席布局的联审** | 待定：一等邮箱在 process image 的位置、轮换比例（`SYSTem:TDMA:SEAT:ROTation`，默认 1:1）、与两板 **effective schedule CRC** 一致性门的关联（见 K-5 增补） |
| 3 | **调试 profile 默认值** | `switch_settle_offset_q` / `gateway_trig_offset_q` / `ready_check_offset_q` / `ready_window_q`；`threshold_per_mille`（当前 950/1000）；`pre_roll_mdeg`/`post_roll_mdeg`（台架 5°，**调试快照非产品额定值**） |
| 4 | **资源配额** | `TRIGGER_T2_RUNTIME_POSITION_COUNT`、`TRIGGER_POSITION_FREEZE_LATCH_IDS`、控制字节偏移、**RAM 按实测尺寸**（K-7/K-12）；以及 **T2-AUDIT-01 落地后**的 mode 声明与 PIO/SM/DMA/GPIO claims |
| 5 | **正式时限** | STEP 时间下限（由位置内多组跨度推出）、Core1 服务预算（顶层 `HIGH_PRIORITY` 500 µs）、`T2_FREEZE_RESEND_SLICES` 节奏、窗口到期判据 |

**流程（不可跳步）**：决定上表 → 起草契约行（`contract_id` 唯一、**锚点必须真实存在**）→ **C11 交叉审核**（禁自审自批）→ 登记 → 此后方可将实现描述为已冻结。
**护栏**：登记前不得把任何**数值快照**写成产品契约（AGENTS.md 硬约束 2）。

### C14 R3 命名规则（实测教训，2026-10-13）

`doc_regression` 的 **C14 R3** 要求：写在 `*_TASK_PROGRESS.md` 的 `归档索引`（章节） 行，
**必须指向** `docs/legacy/<domain>/LEGACY_<DOMAIN>_TASK_PROGRESS_<NN>.md`。
因此：**归档的是「进度日志」时**，才在 Task Progress 里登记索引；
**归档的是「待办附录/历史快照」时**，只在 `docs/README.md` 登记，并在本文保留指针，
不得写入 Task Progress 的 `归档索引`（章节）（否则 FAIL）。

当前实际登记：`docs/README.md` 中的 `legacy/trigger/LEGACY_TRIGGER_T2_TODO_01.md` 行（已归档、已登记）；
若今后需轮转 `TRIGGER_T2_TASK_PROGRESS.md`，则按 R3 命名为 `LEGACY_TRIGGER_T2_TASK_PROGRESS_<NN>.md` 并在该文件内登记。


