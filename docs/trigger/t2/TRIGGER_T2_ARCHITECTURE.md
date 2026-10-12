# T2 预约触发实施架构

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`
Related: `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/trigger/t2/TRIGGER_T2_REVIEW.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`
Last updated: 2026-10-12

## 文档接口与范围

本文件定义 T2 当前主线的时间模型、owner 边界、不变量和验证语义；[TODO](TRIGGER_T2_TODO.md) 是任务状态唯一落点；[Task Progress](TRIGGER_T2_TASK_PROGRESS.md) 记录实现与证据。

## 台架起链流程图（节点 / 期望反馈）

![T2 起链流程](T2_BRINGUP_FLOW.svg)

上图的耗时与状态是绘制时快照；当前可执行流程与逐步实测由 `tools/sync_trigger/t2_workflow.py` 维护，并写入数据驱动视图 `T2_FLOW.html`。流程的判据自顶向下：**验收**（`p3_hardware_acceptance.py t2`：四板挂载、workflow 无失败步骤、参考板 `published_mask` 覆盖全部位置）← **发布**（`POSition?` 的 `tx_publish_count`）← **预览存在**（游标有效时计数尚未越过阈值；越过阈值的位置 Core0 不再生成预览）← **T2 服务被调用**（`T2?` 的 `segment_max_cycles`，[42..44]）← **SYNC_TRIGGER 相位运行且未隔离**（`SYSTem:TDMA:SCHEDule?` 中相位 7 的 run 计数增长、`quarantined_mask` 无位 6；超出 `PROJECT_CORE1_PHASE_SYNC_TRIGGER_WCET_CYCLES` 即被隔离）← **参考游标有效**（`T2:CURSor?`；粘性故障现场在其末尾 7 字段）← 各停机态前置。排查时按此顺序自下而上定位，**先验证下层反馈，再看上层计数**。

**更正（2026-10-12，`T2-PROGRESS-20261012-001`）**：此前写的「相位 6 窗口小于 WCET、需优先级源 active 才能运行」来自 `SCHEDule?` 步长与相位号的误读，不成立；SYNC_TRIGGER 是相位 7，`close_cycle` 为静态值，与优先级源无关。

## 静态配置 / 动态配置的分野与顺序规则

起链必须严格分成两段；把"配置"混到"运行"之后会静默失败。**三个语义必须分清**：

1. **配置**：写入参数或表（环剖面、标定重放、位置表、动作表、模板、计数器、路径延时交接）。全部是停机态写入。
2. **挂载（arm）**：只把模块置为"已武装/可用"，本身不产生输出。包括 `T2:CONFigure` + `T2:STARt`（**名字里的 START 是历史命名，语义是挂载**）、`VDC:FEEDback:SESSion` + `VDC:OUTPut:RUN`（提交运行请求）、`PRIORity:SYNC/MATCH/FOLLow`（提交优先级类）、`SYNC:VDC:DPLL:ProVisional`（路径延时交接）。
3. **启动（动态）**：真正让运行时开始产出的那次使能——`RING:START`（环数据使能）与 `LOAD:MASK` 位 6（`APP_REALTIME_LOAD_SYNC_TRIGGER`，同步触发相位使能 ⇒ T2 开始发布）。**"T2 的启动"只能用掩码位 6 表达**：环一旦 `enabled`/`adapter_started`，`T2:STARt`、`SESSion`、`OUTPut:RUN`、`PRIORity:*` 等停机态写入一律被拒（`tdma_service.c:1033-1035` 与 `1079-1081`，reject 115/135），因此挂载必须在起环前完成，而动态启动由不受该限制的掩码位承担。

**静态配置（`RING:START` 之前，环必须停止）**：凡走停机态元数据路径（`tdma_service_run_stopped_maintenance` / `update_stopped_metadata`）的写入都属此类，它们**会推进环配置序号**，因此顺序由绑定新鲜度决定：

- 环：`CALibration:TOPology:PROBe` + `TDMA:OPMode:STAGe/APPLy`、`RING:GEOMetry`、`RING:TOPology`、`RING:DIAGnostic 1`。
- 位置表：`GRID:CONFigure` → `DEPloyment:STAGe/ACTivate` → `ACTion:INSTall` / `TEMPLate:INSTall` → `COUNter:LOAD`。
- **T2 配置**：`T2:CONFigure` 的前置是 diag 91/92/93——网格已配置、部署 `active.valid`、动作表/模板 `installed && resources_loaded && binding_current`（`scpi_trigger_t2_runtime_commands.inc:74-89`）。`binding_current()` 要求环停止且配置序号未变，因此 **T2 配置必须早于**后续任何停机态写入（会话/`OUTPut:RUN`/优先级）。
- VDC 时基：`OUTPut:TIMing`、`FEEDback:SESSion {generation}`、`OUTPut:RUN period,high,duration`（`duration=0` 表示连续运行；1..20000 是有限调试运行）。
- 临时训练：`SYNC:VDC:DPLL:ProVisional 1` 要求环停止、staged 配置 enabled、标定阶段 complete 且 node_count/topology/profile/schedule CRC 与 staged 逐项相等，且**反馈会话必须已关闭**（`vdc_dpll_manager.c:4363`）——故它排在开会话之前。
- 优先级类：`PRIORity:SYNC`/`MATCH`/`FOLLow`/`FOLLow:PHASe`。`FOLLow` 要求活跃会话、generation 匹配、`s_priority_match_request_session == session`，并要 boundary 请求计数为偶数；其查询页 `FOLLow?` 在通道 active 时拒答，**判据只能用 `FOLLow:STATus?`**。
- 时基掩码：`LOAD:MASK 91`（位 6 = `APP_REALTIME_LOAD_SYNC_TRIGGER`）属静态配置，在起环前设置。

**静态提交、动态生效**：`T2:STARt`、`VDC:OUTPut:RUN`、`PRIORity:* 1`、`LOAD:MASK 91`、`ProVisional 1` 只提交请求或使能；实际产出发生在运行期（VDC 输出服务的起环前 outcome 是 `RING_WAIT`/`MODEL_UNAVAILABLE`/`IDENTITY_WAIT`，属正常等待而非故障）。

**动态动作（只能在运行期）**：`RING:START`（数据使能）、`SOURce:STARt`、`CALibration:ORIGin:TRIAL`、采样与验收判据、停机 teardown。**T2 的发布建立在这三者之上**：TDMA 环在跑（`enabled && adapter_started`，`ring_seq` 递增）+ VDC 输出在产块 + 计数器在走；`T2:STARt` 只是挂载，服务在同步触发相位里被调用，条件不满足即早退（`T2?` 的 `segment_max_cycles` 保持 0）。

**计数时序约束**：参考游标必须在计数到达首个阈值之前有效。台架上推进计数的是 PWM 源（`TRIGger:REServation:SOURce:STARt`），因此源只能在游标有效之后启动；计数先于游标越过阈值的位置不会再有预览，该轮无法发布。转台模型仍须在环启动前武装（其脉冲调度在环接管调度器后会被拒，fault 3）。

之前完成的 COUNTER、SWITCH、GATEWAY、动作表安装和一次性 action ARM 只是动作基础。当前主线是同步 VDC 与 1 kHz 时间片构成的准确时间系统：VDC 基准推进共同时间，NO1 发布预测位置，从板按本地时间片执行动作。本文不把旧的 STOP 诊断执行切片描述成完整 T2。

本文仍是实施草案。位置描述符 wire、资源配额、同步质量门限和产品时限尚未冻结；跨域契约待实现和独立复核后登记。

## T2-SEMANTICS：已确认的语义口径（2026-10-10）

以下六条由用户在 [T2 评审](TRIGGER_T2_REVIEW.md) 中确认，是本文其余章节的上位口径；章节内容与之冲突时以本节为准。它们是设计口径，尚未实现的部分见 TODO，不登记为契约。

| # | 口径 |
|---|---|
| K-1 | 转台到达位置后，**先切换通道（SWITCH），再触发网分测试（GATEWAY TRIG）**。多板模式下，网分 READY 只做异步的时间片踩踏校验，不是许可门；单板模式可另行规定。 |
| K-2 | **DCO 是底层时间源，无法冻结，始终正常更新**。FREEZE 只冻结位置预测（`position_q` 与 prediction version）。单个动作的 deadline 被硬件接受后不再追改；尚未装填的动作在装填前按最新 DCO 重新解算，解算放在网分采样期间。 |
| K-3 | **VDC 进入 LOCKED 并发布之后，T2 才有输出**。未发布时 cursor 可以推进用于诊断，但不得生成或装填动作 deadline。 |
| K-4 | 扫描由起始、终止和 **STEP** 定义，**角度单位为度，分辨率 0.001°**（实现使用整数毫度）；**每个 STEP 位置都执行完整触发**。 |
| K-5 | **T2 位置一帧发完，不分片**，作为一等席负载；DPLL/VDC 同步发布走特等席。**落地方式（2026-10-11 确认）**：T2 使用独立一等邮箱，与特等轮流占用 origin 快速路径的发布机会（当前比例 1:1，编译期常量）；一等邮箱是完整 process image，保留 VDC 核心字段与 ACK，既有 RefMem 区直接承载 T2 紧缩记录，**不再压缩同帧 VDC 字段占位来腾空间**。 |
| K-6 | **每个位置运行多组序列与采样**（当前为 16 组）。16 组在位置开始前全部预装；执行端使用**包含两组的滑动窗口**：一组执行完，即用最新 DCO 解算并推入下一组，始终两组在途。 |

## T2-SCOPE：主线和边界

T2 将转台位置预测映射到共享 VDC 时间轴。NO1 使用 COUNTER 观察转台输入，其他板卡使用同一 VDC 基准在各自本地执行预先定义的动作。板卡角色由 deployment 绑定到 UID、TDMA slot 和本地 IO，不能由 NO 编号或编译期板号推断。

运行角色为 `COUNTER / SWITCH / GATEWAY`。首轮台架映射为：NO1 COUNTER 使用 IN1；NO2 SWITCH_A 使用 OUT1–OUT3；NO3 GATEWAY 使用 OUT4 并从 IN1 接收 READY；NO4 SWITCH_B 使用 OUT1。相同 OUT 编号属于不同板卡的本地命名空间。

细分脉冲只用于验证细分能力、边沿完整性和示波器测量方法。T2 运行时依据 1 kHz VDC 基准同步脉冲，每次基准同步更新一次本地时间片坐标。细分位置可以作为逻辑索引的一部分，但物理细分脉冲不是运行时钟源。

硬件槽位、PIO 程序、IO 路由、动作模板和循环表在 TDMA START 前完成装载和读回。TDMA RUN 后不改变业务槽位、PIO 程序、IO 路由或动作模板；运行期只接收预测位置索引、更新本地时间片标记并执行既有动作。

## T2-PAYLOAD：配置、实时位置和诊断的分层分配

T2 的载荷分为启动前配置事务、运行期实时位置和冻结后的诊断回传三层。三层使用不同的 owner、生命周期和失败策略，不能把本地预测结构直接当作线上载荷。

### 启动前配置事务

VDC 启动前通过现有 `CONFIG_CONTROL` / `NODE_LOAD_STAGE` 控制通道完成一次配置事务。事务允许跨多个 TDMA 物理帧，但逻辑上只有一次 `BEGIN → DATA → ACK → COMMIT`。配置内容包括 run/grid/count epoch、deployment/template/PIO/IO 绑定、位置到动作映射、source/target、阈值、有效窗口、nonce 和回退策略。

必需节点返回 nonce、generation、完成/未完成掩码和错误原因后，主站执行 STOP→ARM，原子激活 active 配置，再启动 VDC。RUN 中只读 active 配置，禁止热加载 PIO、模板或 IO route。

### 运行期实时载荷

运行期使用 `TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE` 的 node mailbox，T2 位置作为**一等席负载**（K-5）。一等邮箱是完整 process image：保留 VDC 核心字段、ACK 与 control，既有 RefMem 区直接承载 T2 紧缩记录（`TRIGGER_POSITION_SLICE_WIRE_SIZE` 恰填该区，无需压缩字段占位）；CRC 继续由原 owner 管理。一等席按发布机会与特等轮流上车（TDMA-SEAT-01），不借用同帧的 VDC 字段空间。

T2 的业务载荷只有一个 `position_q`：

```text
position_q = target_ms_index * subdivisions + target_fine_slice_index
```

一个位置记录必须**在一帧内完整送达**（K-5），不分片、不跨帧重组。完整 `trigger_position_descriptor_t` 只保留在 NO1 和从板本地预测/诊断镜像中，不作为线上载荷。位置身份必须由显式 T2 message subtype 或紧凑状态字段携带，不能依靠 q 数值或传输顺序推断。外层 source/target、transport sequence、generation 和 admission epoch 只负责路由、去重和运行代隔离。

达到外部阈值后，NO1 发出该位置的 FREEZE/版本索引，停止新的 `position_q` 发送。从板停止对该位置的**位置预测**更新，选择本地最新有效版本并准备已激活动作模板。FREEZE 不冻结 DCO 时间映射（K-2）；FREEZE 记录与 PREVIEW 使用同一套运行代身份校验。

**FREEZE 是开始冻结，全部目标收到后才是冻结完成**（2026-10-10 用户确认，`3ddeb258`）：
- 从板收到某位置的 FREEZE 即完成本板冻结，并把"run_id 低 4 位 + 已冻结位置掩码"作为状态写入其每个镜像的 control 字节（`TDMA_PROCESS_IMAGE_CONTROL_OPCODE_T2_FROZEN`）；一次环回即可把状态带回 NO1。
- NO1 对未完成位置按间隔重发同版本 FREEZE（`T2_FREEZE_RESEND_SLICES`），直到 `target_mask` 内全部从板报告该位置已冻结，置 `freeze_complete_mask`；位置窗口过期仍未完成则置 `freeze_abandoned_mask` 并停止重发。从板对重复 FREEZE 只接受第一次。
- 诊断：`TRIGger:REServation:T2?` 末尾的 `freeze_complete_mask`、`freeze_abandoned_mask`、`freeze_resend_count`，以及 2026-10-11 追加的 `freeze_abandoned_count`（放弃完成的次数）与 `late_arrival_count`（**迟到到达**：记录格式与寻址都对，但抵达时承诺窗口已关闭——它仍计入 `descriptor_reject_count`，只是不再与身份/网格/部署等 10 种原因混在一起；目标溢出导致的"无窗口"不算迟到），以及迟到的**量级** `late_last_current_ms`（到达时消费侧粗序号）与 `late_last_expiry_sync`（被错过的窗口），二者之差即迟到量；并**逐位置**公开承诺本身（`descriptors[i].target_ms_index` 低字与 `descriptors[i].expiry_sync`）——没有承诺作参照，迟到计数不构成证据。**注意**：整机驱动下原点持续重解算、窗口被不断延长，"放弃完成"分支在那里不可达；该分支改由 `tests/python/test_trigger_t2_freeze_abandon.py` 以**抽取式 harness** 直接驱动（放弃、饱和、重发、节奏、确认、暂存被拒六种情形，并有负测证明断言有效，见 `TDMA-PROGRESS-20261011-033`）。**FREEZE 完成必须只等"同伴"（2026-10-11 修复）。** 冻结确认 `distributed_refmem_position_freeze_acked()` 是**全有或全无**：`opcode = T2_FROZEN | (run_id & 0x0F)`，掩码内**任一**槽位报的 opcode 不是本 run 就整体返回 0。而确认路径**只能从"收到的镜像"填充槽位**，所以一旦 `target_mask` 含**位置来源槽位自身**（`source_slot`），该位永远不可能被确认 → 每个位置都保持"未完成" → 原点按 `T2_FREEZE_RESEND_SLICES` 一直重发到窗口关闭。现策略：原点**只等同伴**（`peers = target_mask & ~(1u << source_slot)`），自身位被忽略并置标志 `freeze_ack_own_in_mask`；**无同伴**（单板台架）视为按构造完成，不产生任何重发。主机侧 `tools/sync_trigger/t2_enable.py` 原先下发 `0x0F`（含槽 0）已改为按 `--nodes` 推导同伴掩码（4 板 → `0x0E`）。判据：一旦 `freeze_ack_own_in_mask == 1`，此前观察到的"重发很多"就是这条配置错误，而不是链路丢失。

另附 `freeze_abandoned_count`（饱和计数）：位置窗口过期仍未完成会置位掩码，但掩码按位置保存、下一次 run 会清零，**只有计数能回答"发生过多少次"**。

### 冻结后的诊断回传

冻结后允许从板通过独立的低优先级 T2 diagnostic message class 回传一至两帧压缩诊断。第一帧报告 position、采用版本、版本年龄、q 有效性、动作准备状态和 READY 重叠状态；第二帧只在缺版本、旧代、CRC、时间片冲突或 READY 异常时发送，报告原因和残差摘要。诊断不能覆盖 VDC、ACK 或已排队的 VDC image。

`tdma_flight_fifo` 的通用双槽发布/获取/释放能力继续保留，供后续 TDMA 流式 OTA、配置数据和可靠控制使用。它不属于 T2 业务语义；T2 不修改已经进入 FIFO 的 VDC image，也不使用排队帧补写 T2。

## T2-TIME：共同时间坐标

### 1 kHz 基准和本地 cursor

每个运行代维护：

| 状态 | 含义 | 来源 |
|---|---|---|
| `run_id / sync_epoch` | 当前运行代和计数时间域 | TDMA/VDC 运行身份 |
| `pulse_index` | 下一共同 grid coarse 周期的绝对索引 | 当前 TIMER1 经本板 committed DCO 投影到共同 anchor 后派生 |
| `fine_slice_index` | 当前基准周期内的逻辑细分位置 | 本板 grid/model 解算 |
| `action_cursor` | 本板当前检查的动作模板位置 | 由上述状态派生 |

`action_cursor` 是本地状态，不是主站逐动作命令。从板接收 VDC 同步信息后，
沿用既有 committed DCO 与 TIMER1 在本地产生连续时间轴；T2 读取同一模型，
将当前本地时间投影到共同 VDC 坐标，再相对 `anchor_vdc_ns` 计算 coarse/fine q。
接收观察的 `vdc_status.sync_seq` 与每个本地毫秒格不是同一种序号；接收观察
未更新时，本地网格仍推进。T2 不逐格占用 TDMA，也不要求物理细分诊断输出开启。

本地 `sample_seq` 只标记 coarse 位置推进，跨板不能直接比较。软件 service 可以
跨过多个本地格；这不等于 VDC 同步丢失。session、ring、slot、schedule、role 或
clock 身份失配，以及本地时间回退必须显式失效，不能补发越过的动作或重复执行。
当前 TIMER1 必须处于所用模型的有效期内，不把旧接收时间戳套进较新的模型。

本地网格分辨率由 grid 几何决定；已有细分能力覆盖本轮 500 ns 调试配置（测量快照
见 Task Progress 的细分验证条目）。T2 提前按位置索引解算本板硬件 deadline，
由预装 PIO 执行动作；软件轮询频率不决定硬件细分分辨率。映射有效仅表示本地
坐标与身份有效，完整动作精度仍由位置实例、硬件执行和外部同步采样共同验证。

### 位置索引、q 和相对偏移

绝对位置使用：

```text
position = (ms_index, fine_slice_index)
```

该位置可以在本地编码为绝对 grid ordinal `q`，但 q 不是 wire 上必须传输的本地 TIMER1 deadline。

- **绝对 q**：共同网格中的绝对时间片位置。
- **delta_q**：相对预测位置的动作偏移，属于静态动作模板或 profile。
- **本地 action cursor**：从板随 VDC 同步推进的执行游标，不跨板传输。

网络描述符的业务内容是位置索引，不是纳秒时间、本地 TIMER1 tick 或主站计算的每板 deadline。身份字段只用于防旧版本复活。

### grid 和细分验证

grid 保存 `epoch`、`generation`、共同 anchor、基准周期和逻辑细分数。细分输出属于独立诊断路径，不要求每个逻辑细分产生物理脉冲、IRQ、DMA 或软件 tick。

物理细分验证必须按对应索引比对边沿、保留末格空白并保存示波器原始数据。通过细分验证不等于获得 live cursor、共同 anchor 认证或业务动作完成资格。

## T2-PREDICT：NO1 预测与描述符

NO1 的预测计算不在 VDC 发布路径内。Core1 在每个 VDC 基准同步点只取得有界计数观察并记录阈值事实；Core0 使用最近完成的计数窗口异步计算预约位置时间片：

1. Core1 读取 COUNTER 的最新计数观察并把观察交给 Core0；
2. Core0 从最新完成的窗口维护周期/速度估计和 prediction version；
3. Core0 更新同一个预约位置时间片并提前发布最新有效版本；
4. 距离阈值越近，Core0 缩短窗口计算间隔，以降低样本年龄和速度变化带来的预测误差；
5. Core1 只检查是否跨过外部配置阈值，并提交冻结版本 index 的意图。

预测结果不会因为没有新窗口而伪造新版本；没有新结果时继续使用上一份完整结果。阈值广播只标记应采用的 prediction version，不发送完整预测镜像，也不触发槽位热加载。各板本地保留最新有效镜像和两份旧镜像用于诊断对比。

### 角度扫描与 STEP 位置

扫描按 K-4 由起始角度、终止角度和 STEP 定义，单位为度，分辨率 0.001°。实现中角度一律使用整数毫度：

```text
position_angle_mdeg(k) = start_mdeg + k × step_mdeg,  k = 0 … N−1
N = (terminal_mdeg − start_mdeg) / step_mdeg + 1
step_pulses = step_mdeg × pulses_per_degree / 1000
base_angle_mdeg = start_mdeg − pre_roll_mdeg
target_count(k) = origin_count + ceil((position_angle_mdeg(k) − base_angle_mdeg) × pulses_per_degree / 1000)
```

配置时以整数运算校验，不满足即在 STOP 阶段拒绝（fail-closed，运行期不取整、不兜底）：`(terminal − start)` 能被 STEP 整除；`step_pulses` 为正整数；累计计数不溢出；STEP 满足下文的时间下限。每个 STEP 位置都是独立的预测目标，各有位置序号、预测目标计数和 prediction version，并执行完整触发（K-1、K-6）。

**阈值与 lead（2026-10-11 口径）**：lead 取「一步的比例」，不再取「距 origin 的比例」：

```text
lead_pulses = ceil(step_pulses × (1000 − threshold_per_mille) / 1000)
threshold_count(k) = target_count(k) − lead_pulses
```

按距 origin 的比例在长扫描上会让远端位置的 lead 随距离线性膨胀，使数十个位置同时在途；一步比例把在途位置数限制为常数（`threshold_per_mille` 为 500 时 lead 为半步）。在途位置数上界仍须与「位置身份与在途容量」的槽位数、FREEZE 重发节奏和环回确认一起核算。

位置数由扫描参数决定，不再固定为起始/零位/终止三个。运行期只为有界个在途位置保留状态，其余按序号滚动生成；位置序号须能表示完整扫描并防回绕。

STEP 的下限由时间推出：相邻位置的时间间隔（`step_mdeg / 转速`）必须大于一个位置内全部序列的跨度（见 T2-ACTION）加余量，否则下一位置会踩到上一位置。该下限在配置时按**声明转速**校验，运行中仍由 READY 踩踏校验和位置忙故障兜底。声明转速只用于该配置校验与模拟源频率换算，不进入运行期预测；运行期速率仍由 predictor 自测。

**零位（2026-10-11 口径）**：零位不再是独立的预测目标。`zero_angle_mdeg` 只作配置一致性检查——零位必须落在 STEP 网格上（`start_mdeg` 为 0 且 STEP 整除时天然成立），否则 STOP 拒绝。内部位置 index 从 0 起（`index = k`），线上与 SCPI 暴露的 position id 保持 1 起（`position_id = index + 1`）：`position_id == 0` 在全链路都是「无效/无」哨兵，且 id 落在 tag 字节低位为 0 时会退化到冻结 opcode 家族，禁止使用。

参考实现：`feature/node-sequence-reservation-trigger` 分支已有 `CONFigure:ANGLe:SWEEp start,stop,step,speed`、`CONFigure:ANGLe:INPut INn,pulses_per_degree` 及读回命令；其 SCPI 解析/校验、READ 格式与整数格点数学可复用，Core0 浮点判整须改为整数毫度运算。该分支「READY 授权下一步」「同一时刻只允许一个位置在途」的推进模型与 K-1/K-6 冲突，不得照搬（复用边界见 `docs/trigger/t2/TRIGGER_T2_STEP_REVIEW.md`）。

### 位置身份与在途容量

位置数由扫描参数派生后，身份与容量必须先同时满足下列约束，未冻结前不得宣称支持完整扫描：

- 紧凑记录的 position id 受 tag 字节位数限制（有效范围见 `trigger_position_slice.h` 的 `trigger_position_slice_valid()`），FREEZE latch 槽数见 `TRIGGER_POSITION_FREEZE_LATCH_IDS`，冻结完成确认只占控制字节里的 `TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET` 一处；三者容量均远小于长扫描的位置数。
- FREEZE 完成确认必须能覆盖全部在途目标，不允许只覆盖前几个序号。
- descriptor 的 FREEZE 校验上限（`trigger_position_descriptor.h`）须与新的身份口径一致。
- 运行期只为有界槽位保留状态。槽位数、id 表达方式（加宽线上 id，或「槽位 + 滚动代次」两级身份）和冻结确认位宽，须与 TDMA 一等席布局一起核定，并按 `EXE-SEAT-01` 提交用户审核；RAM 预算以实测尺寸为准，不得按「一位一槽」线性放大。
- 决定落地后，紧凑记录的跨域契约须在 `docs/check/DOCS_REGISTRY.md` 登记并经独立交叉审核（C11）；登记完成前，当前实现不得写成冻结契约。

### 模拟源与配置参数

- `pulses_per_degree` 必须作为 T2 配置参数下发，并与模拟源共用同一个值，禁止两侧各自写死。
- 模拟源以转速与 ppd 为输入换算频率：`frequency_hz = speed_mdeg_per_s × pulses_per_degree / 1000`；换算须为整数，或由配置期显式定义取整并读回实际频率（源的输出频率是量化后的实测值，不是声明值）。
- 脉宽必须可配置，不使用固定占空比代替。
- 现有两条源路径——`TRIGger:REServation:SOURce:*` 的 OUT4 硬件 PWM 源与 `CONFigure:MODEl:TURNtable:*` 的加减速模型源——选型尚未冻结；选型会改变隔离验收（`T2-TURNTABLE-008`）的证据口径，须在实现前定稿。

### 起始热身区、零位格点与终止校验区

转台运行配置必须显式包含 `start_position`、`terminal_position`、`step`（角度扫描步长）、`pre_roll_angle` 和 `post_roll_angle`，以及仅用于网格一致性检查的 `zero_position`。当前台架配置将预运行区和终止校验区都设为 5°（调试快照，非产品额定值）：转台从 `start_position - pre_roll_angle` 开始，在 `terminal_position + post_roll_angle` 停止。

预运行区用于建立计数 epoch、估计脉冲周期/速度、检查方向和生成起始角度预测。起始角度本身也是预测目标，必须生成 `start_position_id`、预测目标计数和 prediction version；预运行区的预测不能被误报为起始角度或零位的实际到达。零位落在 STEP 网格上时是普通位置，由扫描派生自己的位置序号、预测目标计数和绝对时间片；不在网格上则 STOP 拒绝（见「角度扫描与 STEP 位置」）。不能用预运行区的区间中心或主机到达时间替代任何位置的预测。

终止角度同样是预测目标，必须生成独立的 `terminal_position_id`、预测目标计数和 prediction version。终止位置动作按该预测计数对应的时间片执行；终止位置之后的 post-roll 区用于继续更新/校验终止预测，确认最后位置实际边沿、检查迟到/重复/缺失和验证没有越过终点的额外动作。post-roll 的停止事件不能反向改变已执行的终止位置动作。

起始角度、各 STEP 位置和终止角度的验收至少保留：起点计数、预测目标计数、计数 epoch、prediction version、position ID、目标 coarse/fine index、实际输入边沿和实际动作边沿。精确位置输出的含义是动作绑定到确定的预测目标索引并按该索引执行；是否实际到位仍须由实际输入/动作边沿在声明容差内校验。预测时间区间或 READY 电平不能填充缺失的边沿证据。

NO1 通过 TDMA 分发单个版本化 `position_q`。预览记录的完整业务和预测字段保存在本地；线上只携带位置身份、`position_q` 和必要的紧凑版本/状态标记。`count_epoch`、`run_id/sync_epoch`、grid/deployment generation/CRC 和有效同步区间在启动前配置并在本地校验。阈值记录只携带 `position_id` 与 `prediction_version`，用于冻结已发布版本。业务语义仍只是时间片索引。

从板收到 `position_q` 后校验启动期身份和紧凑状态，使用本板 VDC model、grid 和 local cursor 绑定目标时间片，再展开本地动作模板。阈值版本 index 到达而对应最新本地结果未到时，从板使用本地最新有效预约位置时间片执行，并记录版本不一致；不能用主机消息到达时间或主站逐板 TIMER1 deadline 代替本地时间轴。

## T2-ACTION：静态模板与位置实例

动作表拆成两层。

### 静态循环动作模板

TDMA START 前装载并冻结：

- template ID、generation 和 CRC；
- 循环跨度；
- 动作 kind、业务 role 和本地 IO route；
- 相对预测位置的 offset；
- 占用、settle 和反馈检查窗口；
- SWITCH 编码或 GATEWAY 脉宽等参数。

模板是少量可循环复用的动作定义，不携带某一次转台目标的绝对位置。

### 运行时位置实例

位置提示到达后，从板本地生成：

- position ID、prediction version；
- target coarse/fine index；
- count epoch/target count；
- template ID 和 instance generation；
- 本板按模板展开后的各组动作时间片标记。

位置实例只标记本次动作在哪个时间片执行，不修改 PIO 程序和业务槽位。

### 位置内多组序列

按 K-6，每个位置运行多组序列与采样（当前为 16 组）。每组依次为“切换通道 → 触发网分 → READY 窗口”（K-1）。所有组都锚定在同一个位置 `q_P` 加模板偏移上：

```text
位置 P（q_P）:
  组 1: SWITCH(code_1) @ q_P + d_s1 -> TRIG_1 @ q_P + d_t1 -> READY 窗口_1
  组 2: SWITCH(code_2) @ q_P + d_s2 -> TRIG_2 @ q_P + d_t2 -> READY 窗口_2
  ...
  组 n: SWITCH(code_n) @ q_P + d_sn -> TRIG_n @ q_P + d_tn -> READY 窗口_n
```

- 多板模式下，组与组之间**不由 READY 推进**（K-1），只按模板中预先排好的偏移执行；因此网分响应时间的波动不会逐组累积。
- 组间隔按网分**最坏**响应时间加切换 settle 和余量排布；READY 窗口用于发现踩踏，不用于推迟下一组。
- 各板分工：SWITCH 板只执行各组的切换步，GATEWAY 板执行各组的触发与 READY 采集；各板共享同一个 `q_P`，偏移来自各自的模板。
- 一个位置的跨度约为 `n × 组间隔`，它决定 STEP 的时间下限（见 T2-PREDICT）。

### 预装与两组滑动窗口

- **预装**：一个位置的全部 n 组序列内容（通道编码、相对 `q_P` 的偏移、网分脉宽等）在位置开始前全部就位，属于静态模板或位置实例，RUN 中不热加载。
- **滑动窗口**：执行端同一时刻只持有两组已解算的绝对 deadline。第 k 组执行完后，网分进入测量；在这段测量时间内，Core0 用最新 DCO 解算第 k+2 组，Core1 在其到期前装填，窗口前移一格。装填前比较模型 token，不一致时使用新解算，赶不上则判为迟到并记录，不立即补发。
- **不追改**：某组 deadline 一旦被硬件接受，不再因 DCO 更新而修改（K-2）；DCO 继续正常更新，供窗口内尚未装填的组使用。
- **前提**：DCO 模型切换必须连续，不得在模型块边界产生相位跳变，否则同一位置内相邻两组之间会出现等于跳变量的间隔误差。连续性由 VDC 域证明。
- 执行端需要深度为 2 的 deadline 队列，每组保留独立的序号、回执与取消结果。

DCO 新鲜度评估见 [T2 评审](TRIGGER_T2_REVIEW.md) E-01：VDC LOCKED 时，即使整个位置沿用同一模型，偏移也只有 ns 量级；滑动窗口使每组模型年龄不超过约一个 Core1 整表周期。

### 动作解耦

动作不使用“下一动作”串行提交模型。每个动作只负责自己的时间片、输出和执行证据。顺序由模板中的相对 offset 和 settle 预算预先确定；动作到达自身时间片即执行；上一级时间分析通过静态 profile 约束下一级位置，不变成运行时等待或临界期软件补救。

## T2-OFFSET：相对位置和调试默认值

以下 profile 可外部配置并读回：

- `switch_settle_offset_q`
- `gateway_trig_offset_q`
- `ready_check_offset_q`
- `ready_window_q`

按 K-1，到位后先切换、再触发：SWITCH 在预测到位位置或其后执行（`switch_settle_offset_q ≥ 0`），GATEWAY TRIG 在 SWITCH 加 settle 之后，READY 窗口在 TRIG 之后。多组序列下每组各有一套偏移。具体默认值待按网分最坏响应时间和开关 settle 重新标定。以上数值是调试快照、不是产品事实源；实现必须符号化、带 generation/profile CRC，并用示波器和设备响应时间校准。

## T2-READY：上一动作重叠校验

多板模式下（K-1），READY 只做异步的时间片踩踏校验：校验上一级动作的实际响应是否与计划时间片重叠。多组序列下，每组 READY 与该组的 TRIG 关联，踩踏判定逐组进行：

1. 记录上一级动作实例身份和计划窗口；
2. 在 profile 指定窗口内采集 READY；
3. 映射回共同时间坐标；
4. 检查早到、迟到、缺失、重复和相邻动作重叠；
5. 生成诊断和业务证据。

READY 不作为当前动作执行许可，不形成当前动作 fence，不替代 PIO 执行回执，也不自动表示 VNA 完成。没有硬件时间戳时，只报告状态观察或不确定区间。

## T2-START：启动和运行生命周期

### TDMA START 前

deployment、PIO/SM/FIFO/GPIO persona、静态模板（含每位置全部组序列）、角度扫描参数（起始、终止、STEP）、grid 几何和 profile 完成装载、CRC/generation 读回，并设置新的 VDC session。STOP 配置不能要求只有 RUN 后才产生的同步事件或锁定模型；本地 cursor 和预测状态在 RUN 中按真实事件建立，不能由静态配置读回授予动作资格。

### TDMA RUN 中

- VDC 每个 1 kHz 基准同步更新一次 local cursor；VDC 未 LOCKED 并发布时，T2 不生成也不装填 deadline（K-3）；
- NO1 在每次同步取得计数观察，Core0 按完成窗口异步更新预测；阈值跨越时通过 TDMA 一等席单帧广播一次位置提示；
- 从板解析位置记录并标记本地各组动作时间片；
- 执行端按两组滑动窗口装填，本地硬件到达时间片后执行动作；DCO 更新在网分采样期间作用于尚未装填的组；
- READY 只做上一动作的重叠校验；
- 不通过 STOP/START 重新加载槽位或改变 IO route；
- 不在目标时间片由主机发送即时命令。

## T2-OWNER：职责与边界

T2 的 Core1 运行入口固定为 `APP_REALTIME_PHASE_SYNC_TRIGGER` / `sync_trigger_service()`。
位置记录运输属于 TDMA 现有 flight owner 的一等席负载；按 K-5，T2 位置记录一帧发完，
经独立一等邮箱与特等轮流占用发布机会。它不能调用
或扩展 VDC/DPLL phase，也不能改变 typed SYNC 发布路径。
通用挂载流程、四级 seat 和越界处理见
[实时负载挂载约束](../../arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md)。

实现方式（2026-10-11 用户确认）：运行期 T2 记录写入 origin 快速路径的**一等邮箱**，与特等按发布机会轮流占用（TDMA-SEAT-01），不与该帧的 VDC 状态共用字段空间、也不压缩 VDC 字段占位。轮换比例是 STOP-only 的每板设置（`SYSTem:TDMA:SEAT:ROTation <M>,<N>`，默认 1:1），席位计数见 `SYSTem:TDMA:SEAT:COUNters?`；一等席与特等席的服务等级对齐见 TDMA 侧 `TDMA-SEAT-03`。

当前代码的挂载和绑定按以下顺序执行：

| 边界 | 当前入口 | 接受条件或结果 |
|---|---|---|
| STOP 资源 | `trigger_grid_preview_configure()`、`trigger_action_template_activate_resources()`、T2 SCPI configure | grid 几何和 staged ring 身份、活动 deployment、模板 CRC、本板资源装载读回；无 RUN model 时只保存未绑定几何，精确规划仍拒绝 |
| 共同时间 | 既有 VDC committed DCO / TIMER1 → `trigger_time_cursor_service()` | T2 本地计算 coarse/fine q；复用会话、ring、slot、schedule、role/clock 身份和模型有效期，不以接收观察频次代替本地网格；不修改 VDC 服务。动作输出以 VDC 进入 LOCKED 并发布为前提（K-3），发布判据由 VDC 提供（`VDC-RUN-001`） |
| NO1 预测 | `trigger_t2_runtime_service()` + `trigger_t2_runtime_prepare_core0()` | Core1 取得 COUNTER 观察并处理阈值；Core0 按最新完成窗口异步更新位置索引，阈值跨越后只冻结版本 index |
| 一等运输 | `distributed_refmem_stage_position_descriptor()` → TDMA flight owner → origin 一等邮箱（`tdma_first_class.h`） | typed SYNC/专用 VDC 命令优先；T2 单 `position_q` 记录作为一等席负载一帧发完（紧缩记录，`TRIGGER_POSITION_SLICE_WIRE_SIZE`）；独立 message subtype、来源与序号；与特等按发布机会轮换（当前比例 1:1，编译期常量）。改写已排队 VDC image 的补丁路径已删除；从板侧由优先级通道第二接收端接收 |
| 从板绑定 | `accept_remote_position()` | 启动期 run/grid/deployment 上下文匹配，position subtype、q 范围、版本状态和有效同步区间通过 |
| 本地动作 | `prepare_deadlines_core0()` / `service_deadlines()` | Core0 按最新 DCO 解算本板各组 deadline（FREEZE 后仍随 DCO 更新），Core1 按两组滑动窗口装填预装硬件；匹配执行回执后才报告完成，已发出位置不接受不同版本回写。当前实现每位置一组、FREEZE 后停止重算，属偏离（评审 A-06、A-16） |

T2 的异步运输交接使用固定单槽：Core1 在 `SYNC_TRIGGER` phase 提交单个 `position_q` 到 TX mailbox；Core0 的 RefMem service 在自己的边界领取，编码为紧缩单帧后交给 origin 一等邮箱。运行期不使用完整描述符的 index/count/data 分片重组（分片路径仅保留为迁移/回退，见评审 A-15 的闭环记录）。每个记录仍须通过 CRC（由运输层承担），RX 由 Core0 复验 target mask、启动期上下文和 q 状态后交给 Core1。单槽忙时拒绝并计数，不覆盖旧请求。冻结后停止新的位置发送；诊断回传不能冒充物理送达或动作完成。

T2 的游标观察与动作硬件执行分别验证。游标从当前模型时间派生完整 coarse/fine
位置，硬件执行必须提前接收本地 deadline；立即调用 SWITCH/GATEWAY 的软件路径
和旧 STOP 单次 ARM 均不能替代运行期细分动作。尚未接通的硬件能力与退出条件
在 TODO 跟踪，不因已有细分波形或软件游标通过而自动关闭。

从板的 deadline 准备使用 Core0 单 writer 的本地原子镜像。阈值到达后采用最新
已完成解算的**位置版本**；刚收到但尚未解算的较新 PREVIEW 不引发临界期计算。
冻结只锁定位置，不锁定时间映射（K-2）：尚未装填的组在 DCO 更新后重新解算；
已被硬件接受的 deadline 不再改写。运行身份变化、迟到或无法确认执行结果须显式
记录，不以即时补发伪装原预约位置。

调试转台源在 STOP 预装本板 OUT4 的独立 PWM，RUN 只切换已装载波形使能。
源与 COUNTER 输入可共存，不由 Core1 定时发脉冲，不占用 VDC/TDMA 运行服务。
它提供名义转台输入，不授予输入边沿时间戳或位置到位精度。

四板绑定比较同一 grid 身份、共同位置及实际边沿，不比较各板本地观察计数是否相等。
T2 只读 VDC 模型与本地 TIMER1；游标、解算、预算优化和动作执行不新增 TDMA 载荷。

| owner | 负责 | 边界 |
|---|---|---|
| VDC | 1 kHz 同步事件、sync epoch、pulse index、共同时间解释 | 不解释业务动作，不直接写 PIO 动作寄存器 |
| COUNTER/NO1 | 计数 epoch、位置预测、prediction version、阈值跨越和描述符源 | 不替从板计算本地 TIMER1，不在目标时刻逐板发命令 |
| TDMA | 位置描述符运输、来源、CRC、代际和有效区间 | 不改变动作位置语义，不把 host 到达时间当动作时刻 |
| Template/Action | 静态模板、profile offset、位置实例和本地动作标记 | 不运行中热加载 PIO，不把 READY 当当前授权 |
| Local executor | local cursor、时间片匹配、PIO 输出、回执和错误事实 | 不等待 USB/SCPI，不依赖主站 deadline |
| READY owner | 基线、边沿/电平、上一动作重叠校验和反馈结果 | 不授予当前动作执行许可，不填缺失 latch |
| Core0/SCPI | STOP 配置、读回、证据导出和恢复 | 不按串口到达时刻触发动作 |

## T2-DEPLOY：槽位和资源

部署表固定四个业务身份：COUNTER、SWITCH_A、GATEWAY、SWITCH_B。CRC 证明配置内容一致，不证明远端在线或动作获准。

所有 PIO 程序、IO route 和动作模板属于启动期资源。TDMA RUN 中不做 persona/lease 退休、申请、热加载、卸载或路由改写。变更角色或模板必须停止运行、处理在飞实例、发布新 generation、重新装载并读回。

资源按同时驻留的业务角色、模板和诊断同步输出分别核算。细分诊断能力不等于业务动作资源；不能在运行中为动作腾出 GPIO 而静默停掉 VDC 同步。

## T2-STATE：状态和失败恢复

| 状态 | 进入依据 | 失败或退出 |
|---|---|---|
| STOP_CONFIGURED | deployment、grid、模板和 profile 已读回 | 配置、CRC/代际或资源冲突 |
| RUN_SYNCING | TDMA RUN 且 VDC 基准有效 | sync 丢失、model 失效、epoch 换代 |
| PREDICTING | NO1 取得有效计数观察，Core0 以完成窗口产生有效预测 | 计数无效、回绕、方向、速度估计或窗口新鲜度失效 |
| POSITION_BROADCAST | 阈值跨越产生位置描述符 | 丢片、旧版本、错目标、过期 |
| LOCAL_MARKED | 从板按本地 model/grid 标记动作 | 映射失败、动作重叠、模板/部署换代 |
| FIRED | cursor 到片且执行回执有效 | 缺输出、PIO fault、边沿缺测 |
| FEEDBACK_CHECK | READY 进入上一动作窗口 | 早到、迟到、缺失、重复、重叠或不确定 |
| COMPLETED/UNDETERMINED | 所需动作和反馈证据齐全 | 证据不足保持未确定，不伪造完成 |

运行中计数观察每次同步更新，但预测只由 Core0 按最新完成窗口异步计算；没有新结果时复用上一份完整镜像。已执行动作不能被新描述符回写；新版本只作用于尚未标记的位置实例。STOP、复位或代际换更使旧描述符、旧实例和旧 READY 失效。

## T2-VERIFY：验证映射

| 验证对象 | 最小证据 |
|---|---|
| 启动静态装载 | 四板模板/PIO/IO/资源读回、CRC、generation、安全电平 |
| 1 kHz 时间片 | VDC 基准序号、每板 cursor、丢同步和换代负测 |
| NO1 预测 | count epoch、阈值一次跨越、prediction version、位置误差区间 |
| TDMA 描述符 | 位置索引、身份元数据、CRC、乱序/重复/旧版本/过期拒绝 |
| 从板解算 | 各板按自己的 model/grid 得到位置标记，不接收主站 deadline |
| 本地动作 | cursor 到片执行、不同 q 顺序、PIO 回执、示波器边沿 |
| READY 校验 | 上一级关联、早到/迟到/缺失/重复/重叠和恢复 |
| 资源共存 | TDMA RUN 中 COUNTER/VDC/template/executor 不通过 STOP/START 切换 |
| 真实 T2 | 转台输入、预测位置、实际动作、READY 和共同时间证据绑定 |

当前源码的 STOP action install/一次性 deadline 执行是迁移材料；它不能关闭上述 RUN 语义。示波器细分验证只能关闭细分能力和测量方法，不能替代 1 kHz cursor、位置描述符、从板解算或实际 T2 完成证据。

## 跨域契约与验证映射

本文当前仍为 Draft。位置描述符 wire、调试 profile 默认值、资源配额和正式时限在实现与独立复核完成后登记；登记前不把本文数值快照当产品契约。
