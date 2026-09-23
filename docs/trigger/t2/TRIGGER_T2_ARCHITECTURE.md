# T2 预约触发实施架构

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`
Related: `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-09-23

## 文档接口与范围

本三件套承接“先多板实际槽位部署，再开发 T2 预约触发”的主线。本文维护设计、owner 和不变量；[TODO](TRIGGER_T2_TODO.md) 是任务状态唯一落点；[Task Progress](TRIGGER_T2_TASK_PROGRESS.md) 记录来源、实现、验证和失败。

跨域概念与时间术语沿用 [T2 总体架构](../../arch/ARCH_T2_RESERVATION_ARCHITECTURE.md)，不建立第二个时间或触发 owner。本文为实施草案，不冻结 wire、资源配额、时限或新的产品契约，不代表预约功能已实现。

## T2-SCOPE：业务目标与推进边界

先将单板承载的多个业务槽位映射到实际板卡，完成定向消息与本板动作闭环；再让动作按未来共同时间执行。部署阶段不需要先实现完整预约算法，预约开发也不以前置完成全部监控、GUI、长稳和故障矩阵为条件。

业务角色为 COUNTER、SP8T、SP2T/HV 和网分网关 VNA。各角色与板卡 UID、业务槽位、本地 IO 的实际映射由可切换的部署配置确定，不能从 NO 编号或 TDMA 环序猜测。首轮映射见 TODO，后续在同一固件下切换身份，不依赖重编译。VDC 是共享基础服务，不占一个业务槽位。

运行角色抽象为 `COUNTER / SWITCH / GATEWAY` 三类，业务槽仍保留四个独立身份。NO2 与 NO4 都是 SWITCH 实例，使用同一预约、编码、稳定期和完成语义；SP8T/SP2T 只是各自输出 mask、码宽、设备 profile 和稳定时间不同，不能复制两套状态机。当前用户确认的部署快照为：NO1 COUNTER 使用 IN1；NO2 SWITCH 使用 OUT1–OUT3 三位编码；NO3 GATEWAY 使用 OUT4 触发并从 IN1 接收网分 READY；NO4 SWITCH 使用 OUT1。相同 OUT 编号位于不同板卡的本地命名空间，不构成 IO 冲突。

GATEWAY 的 READY 使用边沿型必要反馈配置；当前部署只声明路由，不冻结电气极性，也不把初始有效电平或配置 ACK 当成本次采样完成。COUNTER 与两个 SWITCH 的 `NONE_UNOBSERVED` 表示本片尚无物理反馈配置，绝不等价于完成。后续每节点反馈接口仍需允许 SWITCH profile 接入实际反馈而不改变角色类型。

网分网关同时具有 TRIG 输出和 REDY 输入；本文将设备反馈统称 READY，设备原始名称 REDY 在接口/证据中保留。所有承载 T2 的节点均具备反馈处理生命周期，具体输入、极性、语义和 deadline 由角色配置决定，不只在网关实现一个中央反馈处理器。

用户确认的当前台架接线（2026-09-23 快照）：NO1 的 IN1 接外部 1 kHz 信号源模拟转台；NO2、NO3、NO4 各自 OUT4 回接本板 IN1 模拟 READY。该回环是诊断接线，不改写实际 SWITCH 的 OUT1–OUT3/OUT1 路由。源频率是用户给定条件，USB 电平查询不构成频率或边沿时间戳测量。

各板共享同一时间轴，但按分配的不同绝对时间片在本地执行，形成时间轴上的动作顺序。本板捕获同步基准与细分事件、维护本地格计数，提前装载预约与槽位程序，到目标格号时由本地硬件执行；VDC/TIMER1 映射用于时间规划和时间戳核对，不以主机或源板到点逐板发送即时命令代替。READY 在本地捕获并绑定本次预约，SYNC 分发 READY 与时间戳的对应关系，进行时间片重叠检查。现有 `out/t2-multi-sync-plan-500ns-20260922` 预算快照把两个 SWITCH 放在同一 q，不能直接作为本次不同时间片顺序执行的验收输入；需明确分配各角色 q 后再加载，不能只按消息到达先后排序。

SMA 使用 PIO0 热加载；各板已加载的业务槽位决定使用的 PIO 程序、输入输出和资源声明。当前同时常驻程序的指令总数不等于可热加载能力的总上限。换装必须按 persona/lease 的退休、卸载、申请、加载和武装生命周期执行，准备时间纳入预约 lead 预算，不能到目标格才软件加载。保持共同时间轴与持续输出诊断时间片是两个独立要求，热加载不能静默改变 anchor/格号身份。

交付分为两个可独立验证的闭环：

```text
部署闭环：业务槽位 → 板卡绑定 → 真实跨板运输 → 本地动作 → 具身份的回执
预约闭环：未来 VDC 目标 → PREPARE/READY/fence → 本地 deadline → 实际输出/T2 → completion
```

首轮按现有四板台架验证，算法容量使用活动部署表与 `PROJECT_NODE_CAPACITY` 等现行符号；业务角色数、承载板数和传输节点数不等同。单板多槽是合法部署形态，多板分布也应使用相同业务配置语义。

## T2-OWNER：职责与依赖

| owner | 负责 | 边界 |
|---|---|---|
| TriggerAO / 预约 FB | 业务计划、预约身份、状态迁移、每节点反馈策略、最终业务结论 | 每节点处理自己的反馈/超时，origin 只聚合；不写 VDC 模型或直接操作 TDMA/PIO 寄存器；沿现有 `sync_trigger_service()` 推进有界动作 |
| RefMem / 部署模型 | 业务槽位、板卡能力、实例装载、绑定代际及分布式命令/回执事实 | 配置表存在不代表真实远端在线；本地模拟 ACK 不冒充远端回执 |
| TDMA | 固定 payload 准入、运输、来源和完整性证据 | 不解释开关档位或改变预约时刻；不借用已占满的 typed VDC body |
| VDC | 稳定模型快照、质量/时基身份、正反向映射 | 每板模型独立；同一预约不要求各板本地 model token 数值相同 |
| SYNC_IO | 资源租约、本地 deadline 装载、硬件输出及可用的实际边沿证据 | 模型计划时刻、DMA 完成和 GPIO 实际边沿分别表述 |
| Core0 / SCPI | 配置事务、意图投递、停止后证据读取 | 不按 USB/串口命令到达时刻触发各板 |

## T2-DEPLOY：槽位与实际板卡绑定

以下是语义模型，具体结构、编码和保存位置在实现切片中确定：

| 对象 | 必要信息 | 约束 |
|---|---|---|
| 物理板卡 | 稳定 UID、硬件 profile、可用能力 | COM 口和 NO 编号只用于定位/显示，不作为唯一身份 |
| 业务槽位 | slot ID、角色、实例及动作配置 | 不等于 TDMA wire slot；允许同板承载多个无资源冲突的实例 |
| 部署绑定 | slot→UID、本地输出及反馈 IO、deployment generation、配置摘要 | 每个运行代内绑定冻结，切换身份创建新部署代；旧代不能授权新动作 |
| 运输映射 | UID→活动 TDMA slot、拓扑/schedule 身份 | 保持当前物理环序，业务部署不隐式重新编号或重接线 |
| 运行绑定 | run/epoch、plan generation、目标槽位与承载板集合 | 业务 READY 集合和运输节点集合分开聚合；同板多个槽不能被一次板级 ACK 隐式全满足 |

配置在 STOP 下 staging、校验、逐板安装及读回；参与板确认同一部署 generation/摘要后才激活。缺板、重复 claim、不匹配的能力/IO、部分安装和陈旧回执均显式记录，不宣称部署成功。失败保留旧有效绑定或未激活状态，由 owner 执行回退，不自动猜测替代板。

首轮从显式映射开始，但动态切换身份属于本主线必验能力。切换按 staging→停止接收受影响槽位的新预约→取消可取消项/排空不可撤销项→确认输出与反馈捕获退休→释放旧 IO→安装新角色和反馈配置→逐板读回→发布新部署代推进。不得将旧等待中的 REDY、旧计数器进度或旧 completion 交给新角色；安装失败保持未激活或恢复经核实的旧绑定，不伪称切换完成。原子热迁移并非已具备能力，不能在已 ARM 时静默改写角色。

同一固件下应验证业务身份在板卡间互换，再恢复初始映射。自动发现、抢占 claim 的通用机制不是首轮必要条件；既有 `refmem_slot_claim`、`refmem_application_model` 和命令槽是候选复用点，其跨板事实是否完整必须回到实现核验。

## T2-TIME：预约时间与证据类型

| 字段语义 | 含义 |
|---|---|
| `T_fire_target_vdc` | 业务预约的未来共同时间目标 |
| `T_fire_deadline_local` | 本板按绑定模型反算的原始 TIMER1 deadline |
| `T2_actual_local` | 按业务 profile 选定的实际 T2 边沿的硬件观测；必须声明边沿类型、来源与不确定度 |
| `trigger_actual_local` / `feedback_actual_local` | 实际 TRIG 与实际反馈各自的观测；二者分别映射，不共用一个字段覆盖，不以 REDY 时刻冒充 TRIG 时刻 |
| `T2_actual_vdc` | 用该实际事件绑定的模型映射后的共同时间 |
| completion | 预约身份、执行/测量结果、有效性及证据引用；不能由命令 ACK 自动产生 |

候选复用当前 `vdc_model_feedback.inc` 的 committed model、`vdc_timer1_coordinate.h` 的同原点转换及 Domain 整数逆映射。迁入参考分支的 reservation clock 时逐字段适配，不混用 TIMER0、TIMER1、host clock，也不把 publication revision、model token、配置 generation 当同一个计数器。

正式预约按 VDC/Calibration 现行资格准入。显式诊断试验可验证 provisional 模型下的运输和硬件执行，但必须声明诊断范围、拒绝/继续结果，不提升 `LOCKED` 或产品 RUN。资格不足不能靠写状态位消除，也不阻碍无输出的配置/运输开发。

模型在 ARM guard 前变化时重新计算、重新准备并重新取得对应 fence；guard 后不修改已经提交的边沿。各板冻结自身模型和有效期，origin 聚合的是同一预约/部署下的就绪事实，不比较不同板本地 token 是否相等。

### T2-GRID：OUT1 周期的内部细分

OUT1 保持已配置的外部周期输出，T2 使用该周期在共同 VDC 时间轴上的内部细分格。内部细分不是提高 OUT1 引脚脉冲频率，也不需要每格产生 IRQ、PIO 脉冲或软件 tick；预约仍按绝对 deadline 提前装载。

各参与板绑定同一 grid generation、VDC epoch、共同 anchor `A`、周期 `P` 和细分数 `N`。`P`、`N` 必须为正；具体数值随配置和验收确定，不在本文固定。第 `c` 个外部周期内，第 `k` 个内部边界的语义为：

```text
0 <= k < N
q = c * N + k
T_grid(q) = A + q * P / N
T_fire_target_vdc = T_grid(q_target)
```

`q` 是跨周期的绝对细分序号，`(c, k)` 是它的分解表示；`k=N` 应规范化为下一周期 `k=0`，不能把同一边界编码为两个不同目标。业务槽位 ID 与时间细分序号完全独立。各板在相同 VDC 网格上解释目标，仍通过自己的稳定模型得到本地 TIMER1 deadline，不共享本地 raw tick 或 model token。

`A` 指 OUT1 周期输出所依据的共同计划网格原点；各板输出补偿、enable 延迟和实际 pad 偏差单独记录，不能每次读到物理 OUT1 后重置本地细分时钟。实际 OUT1 可用来检查相位一致性，但不能以软件读边沿时刻偷偷替换共同 anchor。若 OUT1 被业务 TRIG 角色占用，物理周期输出由 IO owner 显式停用/切换，已定义的内部 VDC 网格可以保留；同一引脚不能同时由测试同步脉冲和业务预约驱动。

计算从绝对 `q` 求目标，使用受检整数/有理数运算，不能逐次累加截断后的 `P/N` 而产生漂移。不整除时保留分子/分母语义；若实现接口只收整数纳秒，则明确向上量化并留存误差，再选择不早于该目标的合法本地 tick。细分配置须在支持的时钟/模型范围内保证相邻格可区分；若多个格映射到同一可执行 tick，应拒绝该分辨配置或明确降低细分，不宣称虚假分辨率。乘法溢出、跨周期进位及最大未来 horizon 均须校验。

T2 的 settle、触发目标和反馈有效窗口可用细分格数表达，也可经明确的换算接口进入同一网格；不在命令解析中静默混用周期数、格数、纳秒和本地 tick。最小 lead、资源准备及 READY/fence 仍独立满足，内部格间隔不代表运输能在一格内完成。

每节点保留实际 TRIG/REDY 的原始时间或不确定区间，并映射到对应 grid generation 的周期/格位置。反馈通常不恰好落在边界，不能将其吸附到最近格后冒充实测 T2：格标签仅辅助归类，间隔/误差由绑定模型下的实际事件计算。归类采用半开格区间；若观测区间跨格或跨反馈窗口边界，报告歧义/未确定，不能强行裁定在窗内。仅有格号时只能给出相应区间，不能补造更细的时间戳。

修改 `A/P/N` 或 VDC epoch 产生新网格身份，并撤销旧的未提交预约及旧反馈等待；已装载前缀按原冻结时间执行或通过既有可取消能力处理，不因新网格重算。动态角色切换不能让新角色继承旧网格的在飞反馈。首轮细分数及界面单位待 T2-CLOCK-001 明确，示例数值不作为硬件精度承诺。

### T2-GRID-PREPARE：共享格配置与指定目标的无输出规划

`trigger_time_grid_t` 表达共同 `anchor_vdc_ns / period_ns / subdivisions / epoch / generation`，`trigger_time_grid_target()` 从绝对序号计算向上量化的目标。当前规划接口将每周期最后一格保留，遇到保留格直接返回 `RESERVED`，不自动挪到下一格；周期边界本身的业务 IO 许可仍由后续执行 owner 决定。`trigger_time_grid_prepare()` 使用真实 Domain 逆映射及 TIMER1 向上量化，检查模型适用域、提前量、最大未来范围、溢出及相邻理论格的可区分性。迟到返回 `LATE`，不能沿用周期输出规划器自动选后续 ordinal 的行为。

`trigger_grid_preview` 是 Trigger 域内独立的 Core0 STOP 暂存与数学预览 owner。配置发布使用 TDMA 已有 STOP metadata 排他入口，另以单一非阻塞 owner gate 防止管理调用相互覆盖；不写 TriggerAO active state，不增加 Core1 服务、wire 席位、PIO 或 DMA。首次配置绑定本板 session、ring configuration、role generation、local slot、schedule CRC 和本地 clock epoch/run。STOP 会清空运行快照中的拓扑字段，slot/schedule 必须取自 TDMA owner 保留的已启用 staged configuration，以前后稳定 STOP/config ACK 约束该读取，不能把清零的 runtime slot 当真实 NO1 身份；若运行快照仍有非空身份，也须与 staged 一致。每次规划重新冻结实际 committed DCO，在两次本地时钟观测和身份复核后返回指定目标与本地 deadline；rate/model token 可以在两次独立规划之间更新，但不能在同次规划中混用。

暂存配置的 `epoch` 是明确选择的共同实验时间域标签，本板 `clock_epoch_id / clock_run_id` 是独立绑定字段；标签数值一致不证明本地模型来自同一个已认证参考源。四板验证须同时核对共享配置/目标和各自模型原件，不能要求不同板的 model token 或 raw deadline 相等。查询配置只返回留存内容，当前绑定是否仍有效由每次规划复核；配置成功不发布 PREPARED/READY，也不保留输出资源或允许 ARM。

同一 generation 只允许同一配置和同一本地绑定的幂等重试；更改原点/周期/细分/epoch 或重新绑定必须递增 generation。CLEAR 释放暂存配置但保留已消费的代际上界，旧 generation 不得复活。实际网格的跨板分发、共同参考身份认证、持续 RUN 接入以及预约 fence/执行继续由后续任务承接；诊断规划不能替代这些能力。正式执行还需按目标 IO 的补偿和资源条件复验，不将 OUT1 补偿直接套用到其他业务输出。

### T2-DEPLOYMENT：四槽部署事务与本地路由

部署表固定四个业务槽身份：COUNTER、SWITCH_A、GATEWAY、SWITCH_B；槽身份与三类运行角色分开。每行携带板卡 UID、TDMA slot、本地输入/输出 mask 和反馈模式。表 CRC 使用规范整数序列，不依赖结构体 padding；它校验快照完整性，不证明远端在线或 IO 已取得租约。

Core0 在 STOP 下先 STAGE 完整表，逐板读回同一 generation/CRC，随后显式 ACTIVATE。STAGE 不改 active；错误 CRC、旧代和同代重解释均保持旧 active。ACTIVATE 重新绑定本机 UID、TDMA owner 保留的 slot/schedule 和 Trigger IDLE，防止暂存期间换板或换环。四板顺序 ACTIVATE 是可审计安装过程，不伪装为原子跨板提交；正式运行前仍需 origin 聚合各板同表回执。

本地规划先验证“请求槽身份确实部署在本机”，再消费共享 grid 的指定 q 规划，返回角色类型、IO 路由、共同目标和本地 deadline。返回前再次核对 UID/slot/ring configuration/schedule；错目标板不得取得其他角色计划。该动作仍不申请 IO、ARM 或发布 READY。动态身份切换用新 generation STAGE/ACTIVATE，旧部署计划立刻失效；CLEAR 保留代际高水位，防止旧配置复活。

### 跨板诊断动作运输切片

当前诊断入口 `TRIGger:REServation:PROBe` 在活动部署表上验证真实跨板运输。STOP 下配置新 session、向源板排入指定业务角色的 `SET_VALUE` 请求；RUN 后通过既有普通 RefMem mailbox 发送，目标 Core0 更新诊断 RAM 值并由真实返程链路返回具身份回执。主机不转发请求或生成回执。接口、布局与容量以 `distributed_action_probe.h`、`distributed_action_probe.inc`、`refmem_action_probe.h` 和 `scpi_action_probe.inc` 为实现事实源；本节记录诊断实现，不冻结预约 wire 契约。

请求绑定 session、deployment generation/CRC、来源与目标 UID/TDMA slot、业务角色和 request ID；完整记录校验后才能执行。相同请求重复到达只重发回执，变更载荷或旧请求不能重复执行。STOP 请求直接锁存撤销，即使 service 没采到停止状态，旧 session 也不能复活。普通 mailbox 的广播范围、VDC 和 ACK 字段保持，诊断记录按完整目标身份筛选；仅跳过被诊断载荷占用的普通 RefMem delta 解析，继续原 VDC/feedback 消费。该诊断占用普通载荷，不能据此推断原 RefMem delta 刷新率保持。

当前验收限定单源依次向各目标发送；多源相互发送时的回执公平性尚未实现。RAM 动作不申请物理 IO、不消费同步格 deadline，也不接收设备 READY。设备 READY 仍由本地接收，后续由 SYNC 分发 READY 与时间戳的对应关系并检查时间片重叠；诊断动作回执不能充当 READY、fence、物理执行或实际 T2 latch。

### COUNTER 槽位的首个硬件切片

`trigger_counter_service` 按活动部署的 COUNTER 角色、本机 UID、输入路由和代际申请 `sync_io_pulse_counter`，不按板号授予硬件访问。`TRIGger:REServation:COUNter:LOAD` 接收 deployment generation/CRC，START 前再次核对绑定。LOAD、START、STOP、CLEAR、UNLOAD 经 TDMA 的 STOP maintenance 门进入 Core0 硬件 owner；持有租约期间不能 ACTIVATE/CLEAR 部署。输入可在公开 IN1–IN4 中选择，使用部署表的单输入 mask；实际 GPIO 映射以 backend 与 board profile 为准，加载态查询读取实际 SM 的输入选择寄存器。

PIO0 热加载累计上升沿计数程序。运行观察不会暂停计数；FIFO 满可丢快照，累计值继续保留。STOP 冻结，START 保留累计，停止态 CLEAR 改变计数 epoch，UNLOAD 释放资源并保留最后读回事实。查询同时给出输入、状态、lease/clear generation、计数和 TIMER1 观察区间；区间标记一次计数快照的观察时间，不是输入边沿 latch。计算频率必须使用同计数代际的硬件时差与计数差，保留相位量化和区间宽度。

该输入只读 persona 与 reference 资源兼容，不使用共享 DMA arena 或逐脉冲 CPU 中断；其他运行 persona 的共存仍由实际资源和 admission 门判定。已验收的是停止态维护入口下的单槽计数，不授权在线热换、预约格动作或完整四板业务闭环。

### GATEWAY 动态加载与可配置 IO

参考 `node-sequence` 分支 `tools/sequence_trigger_debug_ui` 的槽位/端口选择语义：网关 READY 输入与触发输出独立选择，计数输入独立配置。当前分支以活动部署表为唯一 IO 配置来源，GATEWAY 的单输入 mask 和单输出 mask 分别选择公开 IN1–IN4、OUT1–OUT4；LOAD 不另存一份可能漂移的端口配置。

`trigger_gateway_service` 在 STOP maintenance 下核对活动 GATEWAY 角色、UID、slot、generation/CRC 和 schedule，随后由 `sync_io_gateway` 热加载输出与 READY 程序。`TRIGger:REServation:GATEway` 提供 LOAD、STOP、UNLOAD、查询；LOAD 参数为 deployment generation/CRC 与脉宽。已加载的 COUNTER 或 GATEWAY 租约阻止部署 ACTIVATE/CLEAR，换装先退休与释放。

`sync_io_persona_manager_claim_io` 只允许已定义的 COUNTER/GATEWAY 改写其 GPIO 路由，程序、SM、IRQ、DMA 和 workspace 预算仍来自原 descriptor；副本由 lease 持有，避免栈地址失效。加载态读回实际 PIO 引脚选择、SM enable、FIFO 深度与私有 grant。网关保存所选输出的 GPIO CTRL/PAD、SIO 以及 PIO0 输出/OE，安全低和隔离 PAD 后恢复，其他引脚不被修改。

本片只交付动态装载生命周期：两个 SM 保持禁止，FIFO 不装入触发字，STOP 保留安全低和租约，UNLOAD 恢复并释放。没有即时 FIRE/ARM 接口；查询的 READY 电平不等于新边沿或本次预约完成。参考分支的 IRQ 授权握手可继续用于本地预约执行，但其即时 fire 不含目标 q，不能直接当成共同时间轴上的预约执行。

### SWITCH 槽位的本地编码控制

SWITCH_A 和 SWITCH_B 复用 `trigger_switch_service`，按活动部署的 UID、角色、slot、generation/CRC 与非零 output mask 授权，板号不决定控制能力。掩码允许任意公开 OUT 子集，编码按原输出位编号表示，未选位必须为零；例如只选择 OUT2/OUT4 时，编码的对应位仍是逻辑位 1/3，不压缩为连续编号。

`sync_io_switch` 将所选输出拆成连续组，按组配置 PIO0 OUT_BASE/OUT_COUNT，共用有限程序并同步启用，避免一次写入覆盖未选输出。STAGE 预装不可变编码但不改变输出，APPLY 在 STOP maintenance 下启动有限硬件事务并立即返回；查询有界消费实际 PIO 回显，全部组匹配后标记 COMPLETE。该完成只证明 PIO 写码，不代表外部开关已稳定、READY、物理 RF 通路或预约格到达。

`TRIGger:REServation:SWITch` 提供 LOAD、STAGE、APPLY、STOP、UNLOAD 和查询。LOAD/STAGE/APPLY 重新核验部署；旧代、错角色、越 mask、在飞重写与未重新 STAGE 的重复 APPLY 均不得误执行。STOP 取消未完成项并令所选输出低，不伪造完成；UNLOAD 恢复所选 GPIO/PIO 原状态并释放资源。所有公开 OUT 的电平只读用于对比未选位，未选位不取得写权限。正式按预约 q 的执行仍需独立准入入口，不使用本维护 APPLY 替代。

### T2-GRID-PROBE：细分间隔的物理预验证

可在 STOP 维护态独占 OUT1，输出一个较宽的起始标记，随后输出有限个窄细分脉冲。间隔定义为相邻上升沿之差，包含标记上升沿到首个细分上升沿；标记高宽不额外叠加到间隔中。间隔、细分数量、高宽和硬件 tick 显式配置，经当前冻结模型的 rate 换算后一次预装有限 DMA，不通过软件逐个发脉冲。

该实验复用既有有限输出 owner 的租约、模型校验、取消和终态退休。维护标记的起点是本地 PIO 启动后的任意相位，不能充当 T2-GRID 的共同 `A`，也不证明已接入持续同步 RUN。正式周期 OUT1 与维护实验互斥；结束后确认低电平终态及资源释放，不自动恢复 RUN。

原始证据同时保留请求间隔、模型 rate、累计边沿的编码 tick 和示波器测量值。验收检查标记/窄脉冲数量及高宽、相邻间隔与相对标记的累计误差。参数可编码和批次 COMPLETE 不代表 DMA 带宽或全参数范围精度通过；物理结论仅适用于实际采集档位，示波器采样量化及未校准时基的不确定度单列。多板共同起点与 live RUN 后继细分仍独立验证。

有限窗口模式可在两端都输出宽标记，中间铺满细分格，并省略下一标记前的最后一格。令同步周期 `P=N*Δ`（本模式要求整除），输出相对格位置 `q=0,1,…,N−2,N`；`q=0,N` 为宽标记，其余为窄脉冲，`q=N−1` 保持低电平。最后窄脉冲与末标记的上升沿相隔两个格，实际低电平余量还须扣除末次窄脉冲高宽并计入量化。

首尾标记和内部细分共用一份预装硬件字流，按绝对位置经冻结 rate 换算；不能由主机分别发送两个标记或在末尾临时软件停止。实际脉冲数量为 `N`，容量依据 `SYNC_IO_RATE_SCHEDULE_MAX_PULSES`，不为实验隐式扩容。末标记的真实下降沿也必须纳入完成/退休证明。本模式仍是本地维护窗口，不声明两端标记已绑定持续同步 RUN 的共同 anchor。

### 同步 RUN 的可选物理时间片输出

`vdc_run_output` 增加 RAM 配置 `vdc_run_output_slices_t`，由 Core0 在 STOP 且无在飞
RUN request 时修改，下一次 `SYST:VDC:OUTP:RUN` 锁存。默认关闭：仍输出原周期同步脉冲；
开启后在同一 OUT1 同步标记之后输出窄时间片脉冲，并保留末格空白。此开关不控制
`SUBD:WINdow` 的独立维护实验，不发布预约 READY/fence 或业务 completion。

接口如下，参数均为整数，时间单位为纳秒；命令实现与字段顺序以
`middleware/scpi_port/src/scpi_vdc_output_slices.inc` 为准：

| 命令 | 作用 |
|---|---|
| `SYST:VDC:OUTP:SUBD:AFTER:ENAB <enabled>` | 关闭或开启下一次 RUN 的时间片输出；同步标记继续输出 |
| `SYST:VDC:OUTP:SUBD:AFTER:CONF <interval_ns>,<high_ns>,<marker_ns>` | 配置时间片间隔、窄脉冲高宽、同步标记高宽；完整编码校验通过后整体更新 |
| `SYST:VDC:OUTP:SUBD:AFTER:STAT?` | 读取请求配置，依次返回 enabled、interval、high、marker |
| `SYST:VDC:OUTP:SUBD:AFTER:RUN?` | STOP 后读取 schema、request、锁存配置、周期、时钟、终态/reason、末同步上升沿及末时间片下降沿的计划 tick |

物理输出复用现有 RUN 的 SYNC_IO lease、SM、DMA 与 arena，由
`sync_pulse_sliced_out1.pio` 在每个同步 descriptor 内完成有限重复；不为每格增加
Core1 调用或 TDMA 席位。PIO 实际指令占用经资源表及运行时空闲空间验证。
同步标记沿原绝对 DCO deadline 规划；周期内细分采用固定整数 tick，支持几何与
首尾开销以 `sync_pulse_sliced_configure()` / `sync_pulse_sliced_encode()` 为准。
DPLL 模型修正后的可编码周期变化保留为诊断观测，不新增质量拒绝；脉冲重叠、
倒退、溢出和不可编码字流仍不得装载。实际跨板偏差必须用示波器逐对应时间片测量。
默认参数只代表当前诊断档位，不承诺任意分辨率、时钟或调参范围均可编码。

硬件尾部定义为最后细分脉冲下降沿；FIFO 源退休与物理边沿退休分开。STOP 后才能
切换几何或模式；开关关闭后重新 RUN 回到同步脉冲模式。运行锁存配置由独立读回
绑定 request，不能用后续修改过的请求配置代替运行证据。四通道同窗验收按宽标记
分段、对应格序号比对并检查末格空白；普通同步标记相位通过不代表细分时间片通过。

## T2-FLOW：运输、READY 与执行

1. Trigger 产生带 run/epoch、reservation ID、plan/deployment generation、目标槽位集合和网格身份/绝对细分序号的意图，按 T2-GRID 形成未来 VDC 目标时刻。
2. RefMem/TDMA 通过已准入的真实跨板通道分发 PREPARE；各板只消费属于本板承载槽位的动作。
3. 目标检查身份、配置、时钟、资源与 lead time，记录本地准备结果及 READY/NACK。
4. origin 收齐必需业务槽位的同代 READY 和规定的运输证据，发布该预约的 fence。仅 transport ACK 或旧 READY 不允许 ARM。
5. 各板在有效 guard 内消费匹配 fence，通过 SYNC_IO 提前装载本地 deadline，到点由硬件执行，不由主机同步发命令。
6. 各板处理本角色配置的反馈资格/反馈等待，分别报告装载、实际输出、实际反馈与 T2 证据；origin 聚合必需完成集合，区分执行完成、测量缺失、设备未 READY 和超时。已完成一个节点不能推断其他节点完成。

设备 READY（例如 VNA 可进行下一次测量）、运输 offer ready、预约 READY 是不同事实，必须分开命名和消费。重复消息不得重复输出；更换身份不抹掉前次失败。部署与预约 payload 的字节数、片数、CRC、调度等级、期限均待实测预算后冻结。

同一业务采样中的各角色可以有不同目标时刻/相对偏移：SP8T、SP2T 各自完成本次切换，并满足各自配置的 settle 与所需反馈条件后，才允许 VNA TRIG；不是把全部动作排在同一个时刻。预约 READY 表示“能够按计划执行”，不证明开关已经稳定。若设备反馈时刻不可预知，首轮采用分阶段预约：先完成切换阶段，再取得对应反馈并选择仍有足够 lead 的未来 TRIG 目标，经新一阶段 READY/fence 后装载；不得先装载无条件 VNA TRIG，再依靠主机或临近 deadline 的软件等待补救。未稳定、反馈超时或新目标迟到时，不发该次 TRIG，保留失败并按策略恢复；已装载的不可取消项仍按 T2-STATE 的真实结果处理。

## T2-FEEDBACK：每节点反馈与网分 TRIG/REDY

所有 T2 节点使用统一的反馈处理接口，角色可以配置不同策略，不能因物理板号不同而缺失反馈状态机。反馈 profile 至少描述输入/来源、有效极性/边沿、启动基线、去重方式、相对预约的有效窗口、超时和恢复策略，以及哪些业务步骤必须等待反馈。

| 角色 | 输出/事件 | 本地反馈责任 |
|---|---|---|
| COUNTER | 位置/计数事件与后继预约意图 | 区分位置输入与本次动作反馈；保持各自身份和状态，不把累计脉冲自动解释为其他节点已完成 |
| SP8T | 编码/切换动作 | 处理所配置的状态/完成反馈，核验其属于本次切换；若设备无物理反馈，必须显式配置并标明仅有执行回执或稳定时间推断，不能声称物理完成 |
| SP2T/HV | 极化切换动作 | 独立反馈配置和等待，不继承 SP8T 的 READY；缺反馈的证据边界同上 |
| VNA 网分网关 | 按预约输出 TRIG | 本板接收 REDY，核对本次反馈资格/状态变化及超时，回传本节点结果；不能发完 TRIG 就报告业务完成 |

REDY 究竟是触发前“可接受测量”的电平，还是触发后“本次测量完成”的边沿/电平转换，须按设备协议和接线确认。profile 可以表达前置许可、后置完成或两者；未确认前不得假定高电平/上升沿语义。前置许可纳入本节点预约 READY，后置反馈在 TRIG 后进入独立等待状态。

单线 REDY 不天然携带 reservation ID。无硬件标签时，同一反馈通道按一次在飞动作隔离：ARM 前取得基线、按设备协议观察必要转换、窗口内只消费一次，超时后须重新取得可辨认基线或完成协议重置，不能让迟到反馈命中新预约。仅加软件 generation 无法识别未带标签的迟到物理边沿；无法区分时反馈无效，必须如实记录。

节点 completion 保留预约/部署/run 身份、实际 TRIG/反馈证据各自的有效性、反馈状态和 timeout/error，不将 transport ACK 填充为 device READY。缺少原始 latch 时可发布“观察到状态”或“执行回执”，不计算虚构的 `T2_error_ns`；只有业务 profile 所需结果齐全才聚合 COMPLETED。超时不自动重发 TRIG，重试必须按明确策略创建新预约。

## T2-STATE：状态与失败恢复

沿用总体架构的业务状态语义，精确枚举及 ECC 表待实现：

| 状态/边界 | 进入依据 | 失败或退出 |
|---|---|---|
| DRAFT → PREPARED | 部署、未来目标、模型/质量与资源准入 | REJECTED/CANCELLED，保留原因 |
| DISTRIBUTING → WAIT_FENCE | PREPARE 已提交，等待真实目标响应 | 缺板、CRC/代际不符或超时不 ARM |
| WAIT_FENCE → ARMED | 必需 READY、有效 fence、本地 lead/guard 满足 | 迟到拒绝，不补发过期触发 |
| ARMED → FIRED | 硬件实际执行证据满足定义 | 缺 latch 时只能报告执行状态/测量无效，不能伪造 FIRED 的实际时间 |
| FIRED → WAIT_T2 / COMPLETED | 各节点依据反馈 profile 检查本次反馈及质量 | VNA 要求后置 REDY 时必须等待；无物理反馈配置只能给出相应等级的结果，超时保留输出证据，不重发原预约 |

ARM 后的取消仅使用已声明、已验证的硬件能力；未证明可取消时不得回报“已取消”。链路中断可能造成部分节点已 ARM/已输出，READY/fence 不自动提供网络分区下的全有或全无保证；逐板记录真实结果并执行约定恢复策略。STOP/复位/换代使旧 pending、READY、fence 和 completion 失效，重新运行必须取得新绑定。

## T2-RESOURCE：资源与无扰边界

每种实际部署只核算同时驻留的角色；不能把单板多槽模式的 PIO 占用直接照搬到多板，也不能假设拆板后资源自动足够。DPLL 控制、预约输出和诊断输出对同一 SYNC_IO capability 的使用须显式协调，不能为腾出 GPIO 而隐式停掉时间同步。

复用既有功能块/静态服务及固定内存，不加无限队列、逐事件 USB 查询或无界 Core1 操作。新增/扩容 DPLL/VDC/SYNC 特等席遵循 `EXE-SEAT-01`，提前提交载荷、频率、owner、内存、CPU/IRQ 最坏耗时和剩余预算；本设计不授予资源。

## 跨域契约与验证映射

本页 `T2-*` 是草案章节锚点，不是已登记契约。既有依据为 `VDC-PRIORITY-01`、`VDC-PUBLICATION-01`、`TDMA-PROCESSIMAGE-01`、`TDMA-FLIGHTBITMAP-01`，完整状态以 [登记表](../../check/DOCS_REGISTRY.md) 为准。真正冻结的新部署/wire/执行接口先下沉对应域，再登记及 C11 交叉审核。

| 验证对象 | 最小证据 |
|---|---|
| 多板部署 | UID/槽位/实例/输出与反馈 IO 读回、同代摘要、真实目的板收发及本地动作；身份互换/恢复、缺板/错映射/旧代反例 |
| 预约语义 | 真实代码的身份、重复/乱序、缺 READY、旧 fence、late、取消及换代测试 |
| 内部细分 | 周期/格序号规范化、整除/非整除、跨周期、溢出、deadline 向上量化与相邻格可区分；网格换代及反馈区间跨格歧义；不提高 OUT1 外部频率 |
| 本地执行 | 指定未来 deadline、硬件输出、示波器边沿、STOP/资源恢复；模型测试不代替 HIL |
| 多板 T2 | 同预约各板准备/装载/实际边沿/回执，内外证据分别绑定，实际缺测显式无效 |
| 每节点反馈 | VNA TRIG/REDY 及其他角色本地反馈；常高/常低、重复、早到、迟到、无反馈、切换身份后旧反馈不得满足新预约；缺物理反馈不能标成已实测 |
| 实现回归 | 相关 host、受影响 Release、状态机切片所需 TDMA 短帧、当前源码 P3；额外专项只围绕本批问题 |

先判定当前目标闭环；成功后停止不相关的上游递归。失败从最近边界回溯，已有兼容证据按规则复用。NO5、独立 GPIO 内部观测器、通用 GUI、全量角色/长期矩阵不作为首个多板部署切片的前置；但缺真实 latch 时不能关闭最终 T2 实际回读任务。
