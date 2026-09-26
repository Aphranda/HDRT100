# T2 预约触发实施架构

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`
Related: `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`
Last updated: 2026-09-27

## 文档接口与范围

本文件定义 T2 当前主线的时间模型、owner 边界、不变量和验证语义；[TODO](TRIGGER_T2_TODO.md) 是任务状态唯一落点；[Task Progress](TRIGGER_T2_TASK_PROGRESS.md) 记录实现与证据。

之前完成的 COUNTER、SWITCH、GATEWAY、动作表安装和一次性 action ARM 只是动作基础。当前主线是同步 VDC 与 1 kHz 时间片构成的准确时间系统：VDC 基准推进共同时间，NO1 发布预测位置，从板按本地时间片执行动作。本文不把旧的 STOP 诊断执行切片描述成完整 T2。

本文仍是实施草案。位置描述符 wire、资源配额、同步质量门限和产品时限尚未冻结；跨域契约待实现和独立复核后登记。

## T2-SCOPE：主线和边界

T2 将转台位置预测映射到共享 VDC 时间轴。NO1 使用 COUNTER 观察转台输入，其他板卡使用同一 VDC 基准在各自本地执行预先定义的动作。板卡角色由 deployment 绑定到 UID、TDMA slot 和本地 IO，不能由 NO 编号或编译期板号推断。

运行角色为 `COUNTER / SWITCH / GATEWAY`。首轮台架映射为：NO1 COUNTER 使用 IN1；NO2 SWITCH_A 使用 OUT1–OUT3；NO3 GATEWAY 使用 OUT4 并从 IN1 接收 READY；NO4 SWITCH_B 使用 OUT1。相同 OUT 编号属于不同板卡的本地命名空间。

细分脉冲只用于验证细分能力、边沿完整性和示波器测量方法。T2 运行时依据 1 kHz VDC 基准同步脉冲，每次基准同步更新一次本地时间片坐标。细分位置可以作为逻辑索引的一部分，但物理细分脉冲不是运行时钟源。

硬件槽位、PIO 程序、IO 路由、动作模板和循环表在 TDMA START 前完成装载和读回。TDMA RUN 后不改变业务槽位、PIO 程序、IO 路由或动作模板；运行期只接收预测位置索引、更新本地时间片标记并执行既有动作。

## T2-TIME：共同时间坐标

### 1 kHz 基准和本地 cursor

每个运行代维护：

| 状态 | 含义 | 来源 |
|---|---|---|
| `run_id / sync_epoch` | 当前运行代和计数时间域 | TDMA/VDC 运行身份 |
| `pulse_index` | 下一共同 grid coarse 周期的绝对索引 | 同次硬件同步事件投影到共同 anchor 后派生 |
| `fine_slice_index` | 当前基准周期内的逻辑细分位置 | 本板 grid/model 解算 |
| `action_cursor` | 本板当前检查的动作模板位置 | 由上述状态派生 |

`action_cursor` 是本地状态，不是主站逐动作命令。VDC 只读同步事件快照把本地
`sync_seq` 与同帧 `carrier_sequence`、frame identity CRC、ring/config 身份和本板
硬件 `local_rx_timestamp_ns` 一起发布。T2 用事件时有效的 committed DCO 把该硬件
时刻投影到 VDC 输出时间，再相对共同 `anchor_vdc_ns` 归入 coarse q；本地 `sync_seq`
只检查连续性，不作为跨板绝对编号。归格偏差超过半个逻辑 fine slice、身份不匹配、
模型在事件时尚未生效或 q 跳格时，本板动作保持失效。动作到达自己的时间片才执行。

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

### 起始热身区、精确零位和终止校验区

转台运行配置必须显式包含 `start_position`、`zero_position`、`terminal_position`、`pre_roll_angle` 和 `post_roll_angle`。当前台架配置将预运行区和终止校验区都设为 5°（调试快照，非产品额定值）：转台从 `start_position - pre_roll_angle` 开始，在 `terminal_position + post_roll_angle` 停止。

预运行区用于建立计数 epoch、估计脉冲周期/速度、检查方向和生成起始角度预测。起始角度本身也是预测目标，必须生成 `start_position_id`、预测目标计数和 prediction version；预运行区的预测不能被误报为起始角度或零位的实际到达。进入零位时，预测器使用同一 count epoch 生成 `zero_position_id`、预测目标计数和绝对时间片；不能用预运行区的区间中心或主机到达时间替代零位预测。

终止角度同样是预测目标，必须生成独立的 `terminal_position_id`、预测目标计数和 prediction version。终止位置动作按该预测计数对应的时间片执行；终止位置之后的 post-roll 区用于继续更新/校验终止预测，确认最后位置实际边沿、检查迟到/重复/缺失和验证没有越过终点的额外动作。post-roll 的停止事件不能反向改变已执行的终止位置动作。

起始角度、零位和终止角度的验收至少保留：起点计数、预测目标计数、计数 epoch、prediction version、position ID、目标 coarse/fine index、实际输入边沿和实际动作边沿。精确位置输出的含义是动作绑定到确定的预测目标索引并按该索引执行；是否实际到位仍须由实际输入/动作边沿在声明容差内校验。预测时间区间或 READY 电平不能填充缺失的边沿证据。

NO1 通过 TDMA 分发版本化位置描述符。预览记录的业务字段是 `position_id`、`target_ms_index`、`target_fine_slice_index`；身份字段包括 `count_epoch`、`prediction_version`、`run_id/sync_epoch`、`grid_generation`、`deployment_generation/CRC` 和有效同步区间。阈值记录只携带 `position_id` 与 `prediction_version`，用于冻结已发布版本。业务语义仍只是时间片索引。

从板收到描述符后校验身份，使用本板 VDC model、grid 和 local cursor 解算目标时间片，再展开本地动作模板。阈值版本 index 到达而对应完整镜像未到时，从板使用本地最新有效预约位置时间片执行，并记录版本不一致；该回退最多落后本地已保存镜像的少数更新周期。不能用主机消息到达时间或主站逐板 TIMER1 deadline 代替本地时间轴。

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
- 本板按模板展开后的动作时间片标记。

位置实例只标记本次动作在哪个时间片执行，不修改 PIO 程序和业务槽位。

### 动作解耦

动作不使用“下一动作”串行提交模型。每个动作只负责自己的时间片、输出和执行证据：

```text
SWITCH_A(q_A)
  -> SWITCH_B(q_B)
  -> GATEWAY_TRIG(q_C)
```

顺序由模板中的相对 offset 和 settle 预算预先确定。动作到达自身时间片即执行；上一级时间分析通过静态 profile 约束下一级位置，不变成运行时等待或临界期软件补救。

## T2-OFFSET：相对位置和调试默认值

以下 profile 可外部配置并读回：

- `switch_settle_offset_q`
- `gateway_trig_offset_q`
- `ready_check_offset_q`
- `ready_window_q`

调试默认值采用审核快照：SWITCH 在预测到位位置前两个逻辑时间片执行；GATEWAY TRIG 使用预测到位位置；READY 从上一级动作后一个逻辑时间片开始，在两个逻辑时间片窗口内检查。以上数值是调试快照、不是产品事实源；实现必须符号化、带 generation/profile CRC，并用示波器和设备响应时间校准。

## T2-READY：上一动作重叠校验

READY 只校验上一级动作的实际响应是否与计划时间片重叠：

1. 记录上一级动作实例身份和计划窗口；
2. 在 profile 指定窗口内采集 READY；
3. 映射回共同时间坐标；
4. 检查早到、迟到、缺失、重复和相邻动作重叠；
5. 生成诊断和业务证据。

READY 不作为当前动作执行许可，不形成当前动作 fence，不替代 PIO 执行回执，也不自动表示 VNA 完成。没有硬件时间戳时，只报告状态观察或不确定区间。

## T2-START：启动和运行生命周期

### TDMA START 前

deployment、PIO/SM/FIFO/GPIO persona、静态模板、grid 和 profile 完成装载、CRC/generation 读回，并建立 VDC sync、local cursor 和预测状态。

### TDMA RUN 中

- VDC 每个 1 kHz 基准同步更新一次 local cursor；
- NO1 在每次同步取得计数观察，Core0 按完成窗口异步更新预测；阈值跨越时通过 TDMA 广播一次位置提示；
- 从板解析描述符并标记本地动作时间片；
- 本地硬件到达时间片后执行动作；
- READY 只做上一动作的重叠校验；
- 不通过 STOP/START 重新加载槽位或改变 IO route；
- 不在目标时间片由主机发送即时命令。

## T2-OWNER：职责与边界

T2 的 Core1 运行入口固定为 `APP_REALTIME_PHASE_SYNC_TRIGGER` / `sync_trigger_service()`。
位置描述符运输属于 TDMA 现有 flight owner 的一等位置负载；普通帧继续按原
节拍携带 VDC 状态，T2 片段使用该帧中较低优先级的 RefMem 区域。它不能调用
或扩展 VDC/DPLL phase，也不能改变 typed SYNC 发布路径。
通用挂载流程、四级 seat 和越界处理见
[实时负载挂载约束](../../arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md)。

当前代码的挂载和绑定按以下顺序执行：

| 边界 | 当前入口 | 接受条件或结果 |
|---|---|---|
| STOP 资源 | `trigger_action_template_activate_resources()`、T2 SCPI configure | 活动 deployment、模板 CRC 和本板资源装载读回；未装载拒绝配置 |
| 共同时间 | `vdc_sync_ao_service()` → `trigger_time_cursor_service()` | T2 只读 `vdc_status.sync_seq`、同次硬件事件和 committed model，要求本板 LOCKED；共同 anchor 解算绝对 q，缺序号、缺锁、错身份或跳格不执行动作 |
| NO1 预测 | `trigger_t2_runtime_service()` + `trigger_t2_runtime_prepare_core0()` | Core1 取得 COUNTER 观察并处理阈值；Core0 按最新完成窗口异步更新位置索引，阈值跨越后只冻结版本 index |
| 一等运输 | `distributed_refmem_stage_position_descriptor()` → TDMA flight owner | typed SYNC/专用 VDC 命令优先；普通 VDC 状态与 T2 fragment 同帧，T2 占用二等 RefMem 区域；独立 message class、CRC、来源和序号 |
| 从板绑定 | `accept_remote_position()` | `run_id`、grid/sync epoch、count epoch、deployment generation/CRC、预测版本与有效同步区间全部匹配 |
| 本地动作 | `trigger_action_runtime_stage()` / `advance()` | 先检查 action q 不早于本板 cursor，再提交静态模板实例；已发出位置不接受不同版本回写 |

T2 的异步运输交接使用固定单槽：Core1 在 `SYNC_TRIGGER` phase 提交完整位置描述符
到 TX mailbox；Core0 的 RefMem service 在自己的边界领取、编码和分片发布。普通
VDC 状态保持原字段，T2 的 index/count/data 位于 `TDMA_PROCESS_IMAGE_T2_FRAGMENT_*`；
每个 mailbox 仍须通过 CRC，普通帧允许插在相邻 T2 片段之间，同一 resident
片段可重复观测而不重复推进。RX 由
Core0 复验本板 target mask 并重组，完整描述符经每个 source slot 的单槽 mailbox
交给 Core1。单槽忙时拒绝并计数，不覆盖旧请求。T2 STOP 只发布撤销意图，Core0
下一次 service 退休尚未发送的请求和片段；已经进入 TDMA FIFO 的片段仍须由接收方
通过 run/epoch/generation 门禁拒绝，STOP ACK 不能冒充物理送达或撤销证明。

目前 `trigger_time_cursor` 只在 VDC 基准同步更新时推进到绝对 coarse q，`fine_slice_index`
仍为零；非零 fine q 在本地 staging 时明确拒绝，不允许下一次 coarse service 迟到补发。
非零 fine q 的运行中硬件 deadline 与实际 latch 尚未接通。因此当前实现证明的是
预约描述符与 coarse cursor 的软件绑定，不能宣称细分时间片精确动作。各板 `sync_seq`
是否代表同一个跨板同步事件仍需通过来源身份、epoch 和外部硬件证据确认；不能仅因四板
计数都增长，就认定相同序号对应相同物理时刻。

当前源码已用共同 anchor 和事件硬件时间戳计算绝对 q，但四板相同物理事件的
`carrier_sequence`、身份 CRC、每板 DCO 投影和实际边沿尚未在同一运行窗口验收。
不同板的本地 `sync_seq` 仍不得相互比较；只有同一事件身份与 q 的四板读回、
外部波形和后续 position_id 动作证据相互吻合，才能关闭共同时间绑定。

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
