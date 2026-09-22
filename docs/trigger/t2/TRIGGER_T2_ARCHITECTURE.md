# T2 预约触发实施架构

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`
Related: `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-09-22

## 文档接口与范围

本三件套承接“先多板实际槽位部署，再开发 T2 预约触发”的主线。本文维护设计、owner 和不变量；[TODO](TRIGGER_T2_TODO.md) 是任务状态唯一落点；[Task Progress](TRIGGER_T2_TASK_PROGRESS.md) 记录来源、实现、验证和失败。

跨域概念与时间术语沿用 [T2 总体架构](../../arch/ARCH_T2_RESERVATION_ARCHITECTURE.md)，不建立第二个时间或触发 owner。本文为实施草案，不冻结 wire、资源配额、时限或新的产品契约，不代表预约功能已实现。

## T2-SCOPE：业务目标与推进边界

先将单板承载的多个业务槽位映射到实际板卡，完成定向消息与本板动作闭环；再让动作按未来共同时间执行。部署阶段不需要先实现完整预约算法，预约开发也不以前置完成全部监控、GUI、长稳和故障矩阵为条件。

业务角色为 COUNTER、SP8T、SP2T/HV 和网分网关 VNA。各角色与板卡 UID、业务槽位、本地 IO 的实际映射由可切换的部署配置确定，不能从 NO 编号或 TDMA 环序猜测。首轮映射见 TODO，后续在同一固件下切换身份，不依赖重编译。VDC 是共享基础服务，不占一个业务槽位。

运行角色抽象为 `COUNTER / SWITCH / GATEWAY` 三类，业务槽仍保留四个独立身份。NO2 与 NO4 都是 SWITCH 实例，使用同一预约、编码、稳定期和完成语义；SP8T/SP2T 只是各自输出 mask、码宽、设备 profile 和稳定时间不同，不能复制两套状态机。当前用户确认的部署快照为：NO1 COUNTER 使用 IN1；NO2 SWITCH 使用 OUT1–OUT3 三位编码；NO3 GATEWAY 使用 OUT4 触发并从 IN1 接收网分 READY；NO4 SWITCH 使用 OUT1。相同 OUT 编号位于不同板卡的本地命名空间，不构成 IO 冲突。

GATEWAY 的 READY 使用边沿型必要反馈配置；当前部署只声明路由，不冻结电气极性，也不把初始有效电平或配置 ACK 当成本次采样完成。COUNTER 与两个 SWITCH 的 `NONE_UNOBSERVED` 表示本片尚无物理反馈配置，绝不等价于完成。后续每节点反馈接口仍需允许 SWITCH profile 接入实际反馈而不改变角色类型。

网分网关同时具有 TRIG 输出和 REDY 输入；本文将设备反馈统称 READY，设备原始名称 REDY 在接口/证据中保留。所有承载 T2 的节点均具备反馈处理生命周期，具体输入、极性、语义和 deadline 由角色配置决定，不只在网关实现一个中央反馈处理器。

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

### T2-GRID-PROBE：细分间隔的物理预验证

可在 STOP 维护态独占 OUT1，输出一个较宽的起始标记，随后输出有限个窄细分脉冲。间隔定义为相邻上升沿之差，包含标记上升沿到首个细分上升沿；标记高宽不额外叠加到间隔中。间隔、细分数量、高宽和硬件 tick 显式配置，经当前冻结模型的 rate 换算后一次预装有限 DMA，不通过软件逐个发脉冲。

该实验复用既有有限输出 owner 的租约、模型校验、取消和终态退休。维护标记的起点是本地 PIO 启动后的任意相位，不能充当 T2-GRID 的共同 `A`，也不证明已接入持续同步 RUN。正式周期 OUT1 与维护实验互斥；结束后确认低电平终态及资源释放，不自动恢复 RUN。

原始证据同时保留请求间隔、模型 rate、累计边沿的编码 tick 和示波器测量值。验收检查标记/窄脉冲数量及高宽、相邻间隔与相对标记的累计误差。参数可编码和批次 COMPLETE 不代表 DMA 带宽或全参数范围精度通过；物理结论仅适用于实际采集档位，示波器采样量化及未校准时基的不确定度单列。多板共同起点与 live RUN 后继细分仍独立验证。

有限窗口模式可在两端都输出宽标记，中间铺满细分格，并省略下一标记前的最后一格。令同步周期 `P=N*Δ`（本模式要求整除），输出相对格位置 `q=0,1,…,N−2,N`；`q=0,N` 为宽标记，其余为窄脉冲，`q=N−1` 保持低电平。最后窄脉冲与末标记的上升沿相隔两个格，实际低电平余量还须扣除末次窄脉冲高宽并计入量化。

首尾标记和内部细分共用一份预装硬件字流，按绝对位置经冻结 rate 换算；不能由主机分别发送两个标记或在末尾临时软件停止。实际脉冲数量为 `N`，容量依据 `SYNC_IO_RATE_SCHEDULE_MAX_PULSES`，不为实验隐式扩容。末标记的真实下降沿也必须纳入完成/退休证明。本模式仍是本地维护窗口，不声明两端标记已绑定持续同步 RUN 的共同 anchor。

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
