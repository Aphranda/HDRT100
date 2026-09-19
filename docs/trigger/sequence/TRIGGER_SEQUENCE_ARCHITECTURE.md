# 单节点序列触发架构

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`, `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.html`, `docs/reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.html`, `docs/sync/SYNC_IO_ARCHITECTURE.md`
Last updated: 2026-09-19

## 文档接口与范围

[TODO](TRIGGER_SEQUENCE_TODO.md) 维护待办，[Task Progress](TRIGGER_SEQUENCE_TASK_PROGRESS.md)
记录实际测试、板端事实和失败。本文为实施草案，不登记新的冻结契约。

目标为单板承载 DUT_LINK_CONTROL 与 VNA_GATEWAY 的最小测试系统。两角色协同控制接入本地回环
TDMA（RJ45 发收物理回环），使用专门的 LOOPBACK 模式，复用现有 TDMA owner、消息身份、调度与资源边界；本地结果不宣称跨板链路或全节点裁决通过。
独立 `CONF:SWITCH# N` 的 SP8T 控制及 DUT-only 的 MANUAL/IN 序列能力保留，不依赖 VNA 装载。
组合运行、独立序列与手动开关共用资源准入，不能互相抢占引脚；手动切换要求序列停止且资源空闲。
DUT 链路为：配置序列 -> START 预置首状态并等待初始建立时间 ->
SCPI NEXT 或所选 IN 的外部边沿推进到后继状态 -> 输出连接 SP8T 的编码电平 ->
等待建立时间 -> 发布本地链路完成事实 -> 等待下一事件。DUT 不需要额外状态输出。
组合模式下，VNA 网关在收到 LINK_APPLIED 后输出测量触发、接收 READY 并发布测量事实；
READY 只接入网关选定输入，经 TDMA 的 READY_NEXT 请求推进 DUT，不能再由 DUT 直接消费同一输入。
本地 TDMA 回环模式下，角色间传递带 run/generation/步骤身份的链路请求、LINK_APPLIED 和测量事实。
READY 事实须经配置的唯一推进入口转换为显式下一步请求，重复帧、旧 generation 和同一边沿的
重复来源不得增加游标；物理输入直接推进与回环调度推进不能同时消费同一 READY。
RJ45 回环须经过现有 TDMA 物理发送、帧接收校验和调度路径，不得以软件直达或直接函数调用替代。
序列 IO 继续只使用 PIO0；TDMA 的 PIO/引脚资源由既有 TDMA owner 按原分配管理，序列不得抢占或改写。
首状态的首次测量触发与序列推进计数分离，不能伪造外部事件完成启动。
旧 PULSE/LEVEL 状态输出仅为可选兼容模式，不等同于 VNA 网关角色已执行。
编码及建立时间是本地动作定义；不声明已经切换实际仪表频点、波位或完成测量。
START预置不计入accepted/completed，但须执行首状态配置的状态输出：编码稳定后等待可配置的
响应时间 delay（`settle_us`），再输出 PULSE 脉冲或置 LEVEL；NONE 不输出状态信号。
脉宽由 `pulse_us` 独立配置，不能代替响应延时。READY时current为首状态、next为
第二状态。一个被接纳的事件推进到后继状态；首次事件执行第二状态，DONE不推进，
配置轮次尚未耗尽时，末状态之后的下一事件回到首状态。轮次默认值引用
`trigger_sequence_service_init()` 的 `s_repeat_count` 初始化；零表示显式持续运行。
正轮次以完整计划计数，START 的首状态算入轮次；独立序列的推进上限为
`plan.count * repeat_count - 1`，最后状态完成后进入 IDLE，置 finished 并拉低输出、释放资源。
组合模式须完成最后状态对应的 VNA 采样 READY 后结束，不再发送下一状态请求。
先完成SCPI控制与IO读取，再按实际接线逐输入验证；执行进度与信号源参数见Task Progress。

### 独立脉冲反馈的捕获顺序

独立外部输入配合PULSE状态输出可组成OUT触发仪表、仪表完成反馈到IN的闭环。
输入捕获须在START输出首个触发之前准备好，不能等首脉冲结束后再由CPU开启。
每项编码稳定并完成`settle_us`后，PIO先开放一次反馈接纳，再输出状态脉冲；
此后收到的首个有效边沿锁存为后继请求，当前脉冲完整结束后才可写入下一项编码。
锁存关闭本项反馈窗口，额外边沿不再增加请求；编码建立期间的边沿仍不接纳。
锁存候选不提前增加软件`accepted`，执行器消费请求后才计为执行准入；
PAUSE允许已锁存请求排空，STOP撤销未消费候选并计入notready，不与已执行步骤的取消重复记账。
初始有效电平不视为新边沿，所选输入必须经历配置的非有效到有效转换。
有限轮次仍受原推进配额约束，START输出本身不增加推进计数。
MANUAL、LEVEL/NONE及组合VNA网关沿用各自入口语义；本规则不将反馈转换成无界队列。

## 统一多板 TDMA/VDC 接入目标

本节为用户确认的迁移设计，尚未作为实现完成或冻结跨域契约；执行门禁见 TODO 的
NSEQ-111 至 NSEQ-117。单板 RJ45 回环是多板模型的一种部署，业务槽位集中在一块物理板，
仍经真实运输返回后交付，不以软件直达替代。独立 SP8T 保留 PIO 自主路径。

物理板身份、TDMA 物理槽位、RefMem 业务槽位和 AO/FB 实例分别管理。部署表决定业务槽位
所在物理板；业务槽位数量不决定物理环路节点数。当前单板夹具的逻辑双节点 topology 是
兼容配置，不能据此推定存在第二块板，也不能把三个业务角色直接转换成三节点 topology。
统一运输继续由 TDMA owner 管理固定帧、资源、START/STOP、回程证据、背压和去重。
START 冻结已应用的环路身份，STOP 或配置换代撤销未完成消息；旧身份不能在新环路重新授权动作。

整条环路只有一个 VDC 参考发布者，其来源绑定环路参考物理节点。同板 COUNTER、DUT、VNA
共用本板的稳定时间模型，不为角色各自运行 DPLL。多板时各板有独立硬件计数、本地锚点和
DCO，跟踪同一参考事件时间；不得复制发布板的 local anchor 作为从板计数坐标。
共同参考来源/代次、本地 clock epoch/run、序列 run/generation、部署 binding 和 transport
sequence 分层校验，本地计数不要求跨板数值相等。

以 real-flight 的 TIMER1 本地纳秒坐标及 committed DCO 发布为迁移依据。逻辑 TDMA
observation、TIMER0 uptime、TIMER1 raw 和 VDC 输出坐标不得混用；模型更新、有效性和
publication revision 一致发布。序列只消费只读模型，不写 DPLL 或改变原始硬件计数器。
有效模型绑定、来源、schedule、质量或生命周期失效时阻止依赖该绑定的新动作并记录原因。

VDC 同步与序列事件分别具有固定运输分配和预算，由 TDMA 唯一装配者交接，不允许两个域
覆盖同一邮箱。real-flight 当前 typed priority publisher 会接管参考节点邮箱，不能直接
与现有序列 compact 消息同时启用并宣称共存。具体布局/轮转必须完成容量、期限及兼容验证。

运行短步与 START/STOP、PIO 热加载、DMA 回收等生命周期分开；仅将有界回执处理、完整
消息校验和 STEP/FIRE 提交纳入声明窗口。保留 STOP 优先、预算准入、截止和关闭裕量，不借
后续 phase 或 guard。序列使用 PIO0，TDMA 的 PIO1/2 owner 分工保持。

性能目标为 READY 到下一次 VNA 采样触发低于 1 ms（用户验收目标，非已达成事实）。
分别记录硬件边沿与 Core1 观察/递交时间；毫秒字段为零不能证明达标。配置的 settle 和
脉宽单独列出；短脉宽用于响应目标验证，长脉宽用于对照，不缩短实际仪表要求来伪造通过。
单板功能和响应验收不继承上游多板相位精度结论。

## 第三模式：转台位置驱动完整采样序列

`POSITION` 在同板动态槽位装载 COUNTER、DUT、VNA。转台计数决定位置准入；
START 只预置首项编码并完成响应延时，首个位置阈值到达前不输出 VNA 采样触发。
每累计配置的 N 个脉冲，通过真实 RJ45 的 `COUNTER_NEXT` 回程启动一个位置；
位置内每项编码稳定并等待 `settle_us` 后，经 `LINK_APPLIED` 回程触发 VNA，
READY 经 `READY_NEXT` 回程推进下一项。末项 READY 完成本位置，不自行启动下一位置。
`REPEAT` 在此模式表示位置数量；默认与连续取值沿用序列 owner，有限运行须等末位置末项 READY。

原始脉冲在采样期间持续累计，未满下一个阈值的余数保留。当前位置未结束而下一个
完整阈值到达时，进入 `SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY`，不排队、不忽略位置、不补采。
位置完成后通过 Core1 `counter_rearm_count` 确认重装，不能凭单次 busy 读回推断完成。
仅在等待位置时允许暂停；暂停期间继续累计脉冲，达到完整阈值即故障。运行中配置冻结。
阈值必须为正且小于 `SYNC_IO_SEQUENCE_COUNTER_LIMIT`；累计计数接近该界限或回退时故障，
不得静默回绕。位置阈值判断受 PIO/DMA/Core1 可见性延迟影响，不声称独立物理边沿时间戳判定。

计数驱动只使用 PIO0，由 capture owner 租借空闲 SM0，配置独立 DMA，复用序列计数程序；
活动 capture、analyzer、VDC 资源冲突时拒绝。STOP、失败回滚及热加载归还租约并恢复捕获配置。
不改 PIO1/2。转台与 VNA READY 输入必须不同，且共享计数程序的边沿极性。
Core0负责SCPI/槽位配置；Core1负责组合角色运行协调与实时IO，通过有界事件邮箱和一致快照交互。
迁移期间Core0保留RJ45分片收发桥，完整消息只投递给Core1，不在接收回调中执行IO动作。

| SCPI | 语义 |
|---|---|
| `CONF:SEQ:NODE:ROLE slot,instance,COUNTER` | 在已有角色装载事务内暂存计数角色；启用后与 DUT/VNA 一起验证 |
| `CONF:SEQ:LINK POSITION,counter_slot,dut_slot,vna_slot,counter_input,N,ready_input,trigger_output,pulse_us,timeout_ms,edge` | 原子绑定位置模式；counter_input 选 IN1–IN4，ready_input 可选另一 IN 或 MANUAL |
| `READ:SEQ:COUNter?` | `enabled,slot,input,N,events,consumed_positions,partial,fault_events,history_total,history_retained,phase,error` |
| `READ:SEQ:COUNter:HISTory? ordinal` | `ordinal,run,generation,position,sequence_index,threshold_pulses,observed_pulses,outcome_flags` |

历史 ordinal 从首条开始递增；保存窗口以 `TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY` 为准，
查询窗口外记录返回 `COUNTER_HISTORY_NOT_RETAINED`。outcome_flags 使用
`TRIGGER_SEQUENCE_LINK_HISTORY_REQUESTED`、`TRIGGER_SEQUENCE_LINK_HISTORY_APPLIED`、
`TRIGGER_SEQUENCE_LINK_HISTORY_SAMPLE_DONE` 区分切换请求、编码完成及采样完成。
`observed_pulses` 是请求时 owner 累计计数快照，不能当作编码物理边沿的精确锁存值。
每次新运行重置窗口；有限停止保留末次记录供读回。既有 `READ:SEQ:LINK?` 字段格式保持兼容。

GUI 独立第三页配置槽位、N、位置数、编码及 VNA READY，显示计数与进度；
“最新记录”按需查询末条历史，避免连续查询全部历史占用通信时间。
单板夹具可用信号源接计数输入、VNA 触发输出回接 READY 输入；该夹具只证明功能流程，
不能替代真实网分采样、SP8T 射频通路或多板时序验收。

## NSEQ-A-01 配置模型

复用 `trigger_sequence_config.h/.c`。参数沿指令表定义chan/pol/freq/wave，
BOTH展开H/V；状态顺序为channel -> polarization -> frequency -> wave。
状态库和计划条目上限引用 `TRIGGER_SEQUENCE_STATE_MAX`，计划数引用
`TRIGGER_SEQUENCE_PLAN_MAX`，名称长度引用 `TRIGGER_SEQUENCE_PLAN_ID_MAX`。
名称为不区分大小写的ASCII标识符，规范为大写。

完整参数和计划先校验后提交；失败不改变原配置。参数成功重配递增generation，
包括相同参数重写；耗尽时拒绝，不回绕。旧计划保留查询但CHECK/激活返回stale。
重写活动计划撤销激活。子集和重复引用合法，CHECK只报告覆盖诊断，不改变active。

所有会改变序列配置、IO/code、轮次、LINK、角色装载和模型 staging 的生产入口共享
`trigger_sequence_service_configuration_begin()` / `trigger_sequence_service_configuration_end()`
配置事务门。START 从校验配置、检查 LINK 准入、复制 `s_run` 快照、冻结 store 到
`COMMAND_START` 入队始终持有同一事务门；运行快照不会混入事务中途发布的 generation。
LINK 配置需要同时写自身状态时，锁顺序固定为“service 配置事务门 -> LINK writer guard”，
禁止反向取得。RefMem 自动 RX、通用节点装载、角色 staging/activation 和模型转台 staging
也必须经过同一事务门，不能只冻结 SCPI 外层入口。

CRC按 `TRIGGER_SEQUENCE_CRC_SCHEMA`，复用 `pota_crc32_update`，显式LE32编码。
参数CRC字段chan/pol/freq/wave；映射CRC为所有状态行state_id/switch1_ch/
switch2_sel/pol/freq_idx/wave_idx；计划CRC为parameter_crc/map_crc/count/state_ids。
generation单独验证，不把名称、generation或结构体padding混入内容CRC。

本地IO配置和每状态code另绑定同一参数generation。重新配置参数令IO/code失效；
更换编码输出掩码令旧code失效。所有active引用都必须配置合法code才能START。
code 只允许设置编码掩码内的位；编码掩码与可选状态掩码必须分离。
`NONE` 模式要求状态掩码和脉宽均为零，只驱动编码电平，建立后仍产生内部执行回执。
`PULSE` 模式要求非零状态掩码及正脉宽；`LEVEL` 模式要求非零状态掩码及零脉宽。
建立时间允许零；上界引用 `SYNC_IO_SEQUENCE_TIME_MAX_US`。配置矛盾时拒绝，保持原配置。

## NSEQ-A-02 SCPI接口

既有业务配置命令沿指令表语义，长短写法均由libscpi处理。
所有写入在完整参数解析、范围和多余参数校验后才提交；拒绝负数绕回和截断。
固定样例不能作为响应。启动、暂停、恢复、停止写入返回accepted，查询owner结果。

| 接口 | 本轮行为 |
|---|---|
| CONFigure:TRIGger / READ:TRIGger:PARameter? | 真实参数、generation、CRC与状态数 |
| CONFigure:SEQuence / READ:SEQuence? [plan_id[,index]] | 完整计划替换、全量引用或单项查询；无plan选择active |
| READ:SEQuence:MAP? / CHECK? [plan_id] | 实际状态映射、校验诊断；stale不得映射成新参数 |
| CONFigure:SEQuence:ACTive / READ:SEQuence:ACTive? | 真实激活和有效性；CHECK不隐式激活 |
| CONFigure:SEQuence:SOURce MANUAL或IN1-IN4,RISING或FALLING | 选择软件或外部入口；READ:SEQuence:SOURce?读回；旧 BUS 拼写拒绝，不保留兼容别名 |
| CONFigure:SEQuence:IO mask,OUT通道,settle_us,pulse_us | 原子配置本地动作输出；READ:SEQuence:IO?读回 |
| CONFigure:SEQuence:OUTPut code_mask,status_mask,NONE或PULSE或LEVEL,settle_us,pulse_us | 显式输出角色配置；NONE 只输出 DUT 编码；READ:SEQuence:OUTPut? 返回掩码、模式、时间、generation、valid |
| CONFigure:SEQuence:CODE state_id,value | 配置本节点状态编码；READ:SEQuence:CODE? state_id读回 |
| CONFigure:SEQuence:REPeat count / READ:SEQuence:REPeat? | 停止时配置完整计划轮次；零显式持续；查询 configured_repeat,active_repeat,finished；START 校验 `plan.count * count <= UINT32_MAX` |
| CONFigure:SEQuence:LINK OFF或LOOPBACK,dut_slot,vna_slot,MANUAL或IN通道,OUT通道,pulse_us,timeout_ms,RIS或FALL | 在序列和 TDMA 均停止后配置专门的同板 RJ45 物理回环与网关 IO；RJ45 仅作 LOOPBACK 模式兼容别名；组合 DUT source 固定为 MANUAL/NONE，网关输出不能重叠编码输出 |
| READ:SEQuence:LINK? | 返回 `scpi_sequence_link_q` 的 enabled、phase、error、binding/model epoch、run/generation、step、收发/拒绝/触发/READY/完成计数、角色/IO/时序、活动轮次及当前 exchange identity；字段顺序以该处理函数为准 |
| READ:SEQuence:LINK:TRANsport? | 返回 local-return 诊断和 `tdma_local_return_snapshot_quality_t` 质量；`FRESH`/`CACHED` 均为一致快照，`UNAVAILABLE` 是显式可观测状态，不通过 SCPI 失败或遗留错误队列表示 |
| TRIGger:STARt [plan_id] / STOP / ABORt / PAUSe / CONTinue | 真实owner控制；STOP/ABORt取消未完成步骤并安全输出 |
| TRIGger:SEQuence:NEXT | 独立 MANUAL 模式直接请求一步；LOOPBACK 仅在 READY 来源为 MANUAL 且处于 `LINK_WAIT_READY` 时提交软件 READY，随后仍须由真实 RJ45 回环的 READY_NEXT 回帧请求 DUT 切步；外部 IN 模式拒绝，防止混源 |

调试 GUI 的独立 SP8T 与 RJ45 物理回环页面都必须显式配置 OUT1-OUT4 属性，不得由
界面构造器暗含固定 mask。独立模式允许每路选择编码或状态；RJ45 模式允许每路选择编码
或 VNA 触发，并要求恰好一路启用为 VNA 触发、至少一路启用为编码，两者不得重叠。
界面显示可以使用短标签，但生成的 `CONFigure:SEQuence:OUTPut` 与
`CONFigure:SEQuence:LINK` 必须携带完整 mask 和实际 `OUTx`。
| TRIGger:SEQuence:NEXT? | 返回与 READ:SEQuence:STATe? 相同的运行状态块，不推进序列 |
| READ:SEQuence:STATe? | 本地完整状态、游标、计数、时间与错误 |
| READ:TRIGger:STATe? | 保留指令表字段排列；未实施角度流程的字段不伪造角度进度 |
| READ:IO:INPut? / OUTPut? [channel] | 无参数返回实际逻辑位掩码，有通道返回该通道实际电平 |
| READ:IO:STATe? | input_mask,output_mask,sequence_owned_mask,armed,busy |
| CONFigure:SEQuence:NODE:ROLE slot_id,instance_id,DUT或VNA | 本地 RefMem 配置事务，同时暂存 NODE_LOAD 和真实 LINK_SWITCHER/INSTRUMENT_CONTROLLER 的启用、SMA IO/IP 声明；返回 STAGED，不执行硬件 |
| READ:SEQuence:NODE:ROLE? instance_id | instance_id,role,active_enabled,staged_enabled,active_resource,active_io,active_ip,staged_resource,staged_io,staged_ip；staging 已消费时读回 active 作为后续配置基线 |
| CONFigure:SEQuence:NODE:ACTivate / READ:SEQuence:NODE:LOAD? | 复用 RefMem 镜像激活与载入状态读回；激活仍受既有 owner、CRC、部署和资源门禁约束 |

IO通道和掩码按逻辑IN/OUT，不接受旧硬编码GPIO范围。第一通道对应最低位。
角色事务使用 RefMem command slot 的 post/take/ACK/NACK 串行门禁；命令槽占用时拒绝。
角色模板白名单不包括 MODEL_VNA；启用 VNA 时声明 SMA pulse capture/fire，不能继续沿用
原模板的 RJ45/UART 声明冒充测量触发。角色 IO 声明是配置意图，运行绑定和资源租约须独立验证。
SD staging 替换 inline 事务时废弃其缓存，不能把先前角色覆盖重新带入下一次配置。
`refmem_slot_claim_derive_proposals()` 按显式 slot→physical board 提案及非零 claim epoch 派生
配置候选，能力和 UUID 从真实 board 表取得；同板多槽须由对应槽位 policy 允许。
该纯派生 API 不申请租约、不覆盖活动 claim；生产 owner 仍须完成当前租约冲突校验与原子发布。
软件推进只保留 `TRIGger:SEQuence:NEXT` 和 `TRIGger:SEQuence:NEXT?`。写命令不带参数：
独立 MANUAL 模式在 READY 时接纳一步；LOOPBACK 的 MANUAL READY 模式在等待 READY 时结束当前网关等待，发布
READY_NEXT，并且只有该消息经过真实 RJ45 发送、接收和完整 identity 校验后才请求 DUT 切步。
MANUAL 只代替 VNA READY 输入，不代替 LINK_APPLIED 或 READY_NEXT 的物理回环。该 READY 等待不受
外部输入超时限制；LINK 的其他阶段仍使用配置的有界超时。选择 IN1-IN4 时只允许真实输入完成
READY，`TRIGger:SEQuence:NEXT` 必须拒绝。
查询命令返回与 `READ:SEQuence:STATe?` 相同的运行状态块且无执行副作用。旧的
`TRIGger:SEQuence:STEP`、`CONFigure:SEQuence:NEXT` 和 `READ:SEQuence:NEXT?` 不再注册。
输入映射引用board的TRIG/ARM/EXT_CLK/GATE四个输入宏，遵守反序板级布线。
输出pad读取必须取实际引脚电平；code映射和期望值不得冒充实际IO。

参数查询采用指令表通用param block：id,crc,version,modified,epoch,run_id，
后跟chan,pol,freq,wave,state_count,map_crc。id为TRIGGER，epoch为参数generation；
配置不属于运行，run_id为零。sequence全量查询以UINT32_MAX索引和false区分单项。
active的PASS/FAIL是当前校验结果，不伪称历史校验时间或远端应用ACK。

IO配置读回为output_mask,completion_channel,settle_us,pulse_us,generation,valid。
运行详读字段顺序以 `scpi_sequence_status_q` 为准，含所选/已写出/已完成索引及状态、
下一索引、循环、接纳、完成、busy拒绝、非就绪拒绝、中止、故障及兼容时间字段。
PIO迁移版增加`READ:SEQuence:TIMing?`，返回backend,tick_ns,physical_timestamps_valid。
PIO0回执无物理时间戳，valid为false，原写出/完成时间及alarm_late字段为零；
CPU收取回执时间不得冒充PIO latch或TDMA共同时间。未建立运行时backend为NONE。
此前GPIO版本的时间字段含义仅保留在进度记录中。

分段诊断另用`READ:SEQuence:HISTory:TIMing? ordinal`，保留原后端查询语义。
版本引用`TRIGGER_SEQUENCE_LINK_TIMING_VERSION`，字段顺序为version、clock_hz、ordinal、
run、generation、binding_epoch、exchange_id、position、sequence_index、flags，再按
`trigger_sequence_link_time_t`输出request、applied、offered、returned、fire_queued、done。
阶段值为相对该位置接纳时刻的无符号64位TIMER1原始拍数，flags按该枚举的位表示有效性；
零偏移是有效值，不用零表示缺失。原历史查询与诊断查询须核对完整记录身份，不能拼接覆盖前后的记录。
request为状态请求接纳，applied为LINK观察到IO稳定完成，offered为片段交给RefMem运输，
returned为RJ45回程交入收件箱，fire_queued为采样命令入owner邮箱，done为观察到READY且脉冲已结束。
这些观察/递交边界不是PIO物理边沿；首位置的首项可能已在START预置，不把其零切换耗时推广至后续位置。
`sequence_position_validate.py --timing-evidence`流式采集，`sequence_timing_analyze.py --csv`导出
逐位置/状态分段和未取整原始拍；旧毫秒历史继续兼容，但不能支撑微秒分段精度。

历史记录作为Trigger域本地诊断向量发布，容量引用`TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY`，
布局版本引用`TRIGGER_SEQUENCE_LINK_HISTORY_VECTOR_VERSION`。`READ:SEQuence:HISTory:STATus?`
按`trigger_sequence_link_history_status_t`返回version、clock_hz、capacity、run_id、generation、
binding_epoch、threshold、total、retained、overwritten；快照暂不可用返回`"BUSY"`，不得伪造零记录。
Core1为唯一运行期写者，以原子字和sequence发布完整视图；Core0读者有界重试，仅访问诊断向量，
不调用运行态owner、不取得实时临界区锁、不回写读游标或让生产者等待。每个新run重新开始
可见窗口，STOP/故障保留已有记录和未完成标志，配置后读回的旧run不能误认作新run结果。
向量是本地诊断事实，不是TDMA/VDC同步载荷；后续跨板采集由RefMem按独立接口映射摘要。
其字段writer/value domain/lifecycle/snapshot-needed分别为Core1、运行身份及TIMER1偏移、
新run重置且停止保留、必须取得一致sequence；不能将软件边界提升为物理边沿事实。

静默验收先验证容量足以保存完整计划，再发一次START，期间不轮询LINK/COUNTER/TDMA/PHY或
流式读历史；有限轮次结束后再读回向量。超容量、记录覆盖、身份改变或部分记录均不能通过。
主机完整命令记录须证明静默区间内无命令；硬件计数和RAM发布仍有有界开销，不能命名为绝对
无干扰。普通在线历史采集与`--diagnostic-stress`压力探针分别记录观察负载，禁止混作静默基线。

`READ:SEQuence:REJections?`返回run_id,generation,busy_rejected,notready_rejected,pending。
PIO外部运行期间输入计数与执行回执来自异步DMA，拒绝数保留最近稳定结算值，pending为true；
暂停或停止完成排空后结算。pending为true时，零拒绝不能作为无丢失或无忙时拒绝的证据。
软件模式的指令拒绝直接计数。外部验收需在脉冲源结束后暂停并读取pending=false及最终计数。

## NSEQ-A-03 状态与握手

| 状态或事件 | 行为 |
|---|---|
| IDLE -> STARTING -> READY | 校验并冻结计划/code/IO，申请资源，输出首状态编码；编码稳定后等待响应时间，再执行配置的 PULSE/LEVEL 状态输出（NONE 无状态输出）；完成前关闭事件准入，不计accepted/completed |
| READY -> BUSY | 只接纳一枚事件，选择next_index，写code；实际写出后记录executed |
| BUSY | 等待建立时间；NONE 直接记录本地 completed，PULSE 完整输出状态脉冲后记录，LEVEL 保持状态电平后记录；未达到有限运行末步则返回 READY |
| 忙时TRIG/STEP | 拒绝并计数，不排队、不覆盖在途步骤、不恢复后重放 |
| 末步完成 | 未耗尽轮次时等待下一枚事件回绕；有限运行末步完成则置 finished、进入 IDLE 并安全释放；组合模式等待该状态的最后测量 READY |
| PAUSE | 立即停止新准入，已接纳步骤完成后进入PAUSED |
| CONTINUE | 返回READY等待新事件，保持后继索引，不自动执行 |
| STOP/ABORT | 优先取消待执行命令和定时，完成/编码输出拉低，释放资源；保留中止事实 |
| 再次START | 新run身份，重新预置首状态，初始建立完成后READY；重置本轮触发计数 |
| FAULT | 不发成功完成通知、不自动推进；保留失败原因，STOP后才能重新START |

建立时间以及所配置状态动作结束前均视为 BUSY；NONE 没有额外脉冲等待。
外部发送方不得用内部 completed 冒充网分测量 READY，是否接纳由实时入口的就绪状态决定。
启动时已有电平不作为新边沿；输入切换只能在停止边界，旧事件不得重放。
运行、暂停、故障或待处理START/STOP期间配置冻结；owner停止完成后才允许配置。PAUSE 取消
当前网关等待，CONT 为当前步骤建立新的 exchange；STOP、超时和故障关闭 TX、取消待处理测量并
经 owner 清理输出/租约。恢复或再次 START 不接受旧 exchange 的 READY。

## NSEQ-A-04 Owner与硬件

Core0唯一拥有可写配置和SCPI解析。START复制有界运行快照，通过受保护命令槽
发布给现有Trigger服务；Core1唯一拥有本地执行状态。Core0不能直接写GPIO或推进游标。
状态查询使用受保护副本；借用配置指针不得跨核。FB/服务动作有界返回，无阻塞等待。
组合协调器 `trigger_sequence_link` 位于 Core0：处理逻辑角色消息和有限测量轮次，
通过 Trigger 命令槽请求 Core1 fire/step/finish；不得直接驱动 GPIO 或伪造 RX 完成。
序列命令服务在应用Core1的强制调度阶段处理，在线时位于既有TDMA和analyzer之后、
阶段计量结束之前；离线维护提前返回分支也处理邮箱。不可依赖可禁用或隔离的
legacy Trigger负载槽，也不能在周期计时之前插入未计量的硬件动作。

当前迁移后端归sync_io，按`SYNC_IO_PERSONA_ID_SEQUENCE`在PIO0热加载。
整计划在 ARM 前准备，PIO 处理输入准入、编码建立及可选状态动作，DMA 传递计划和真实执行回执。
NONE 与 LEVEL 使用相同的有界建立/回执路径并跳过脉冲计时；NONE 的计划状态位为零，
不改变 PIO 指令容量或借用其他 PIO owner。编码外的 GPIO 不申请为该运行的输出位。
CPU汇总回执并处理控制邮箱，不消费VDC的破坏性capture队列，不参与逐步输出门控。
START成功取得资源后由PIO owner预置首状态，首项复用executor的编码写出、建立等待与状态输出路径。
首项真实written/completed回执单独校验，不计入推进计数；状态动作完成且executor就绪后才启动输入
counter/ingress；DMA循环从第二状态开始，末状态后回到首状态。
RJ45组合DUT使用NONE，首项建立完成后的LINK_APPLIED经真实回环通知VNA网关输出触发，不额外生成独立状态脉冲。
有限外部模式由 `sequence_finite_ingress` 的 Y 寄存器执行步数配额；忙时不扣配额，
最后一枚接纳事件后进入 parked，不依赖 CPU 轮询停机。暂停边界结算尚未扣除的接纳事件，
恢复不能清空配额或重启已 parked 的 ingress。MANUAL 模式由 owner 检查同一上限。
零推进运行仍建立 START 首状态、等待响应时间并完成配置的状态动作，随后发布 finished。
executor 使用 `IN Y,32` autopush 产生 written 回执；completed 仍在建立和可选脉冲结束后发布。
倒计数补偿与指令数以 `sync_io_sequence.c/.pio` 为准，不通过修改主机延时模拟硬件边界。
回执异常、溢出和硬件资源冲突必须如实报告，不能用DMA预填数量代替执行或完成。
此前GPIO raw IRQ/alarm版只作为进度008/010的历史验收对象；PIO版验收状态见TODO。

SMA资源从START管理预留起独占，现有输出任务活跃时拒绝启动。
新运行存在时，legacy输出/波形/PWM/SEQ/ENC相关写操作拒绝，不抢占输出。
申请失败、STOP和异常均停止并回收本persona的SM、程序、DMA和资源租约；
只清理自身资源，输出恢复安全电平。不得清空PIO0其他persona的程序或capture队列。
组合角色控制复用 TDMA owner 的既有协议与资源分区；不因单板回环绕过 VDC/实时准入或改变总线布局。

## NSEQ-A-05 后续与验收

TDMA 通过带 run/generation/step/state 的请求和完成结果接口衔接 DUT 与 VNA 角色，
专门的单板 LOOPBACK 复用寻址、重复抑制与 generation 校验，为后续多板扩展保留协议边界；
LOOPBACK 本身不表示远端多板模式已实现或验证。
角色槽位与 TDMA 物理拓扑槽位分开：按“角色槽位 → claimed physical board → topology slot”
解析路由。同板多个角色共用物理板身份和 TX mailbox，迁移到多板时修改 claim/路由，
不把两个逻辑角色伪装成两块物理设备。本地 IO 绑定与远端角色路由分别校验。
物理回环角色递交必须由既有 TDMA owner 验证真实 RX 回程后产生，不能由 TX 提交成功代替。
设计独立的 self-return receipt/有效掩码时，保留远端 WKC、freshness 和统计语义；
链路断开后不得产生本地角色成功回执。单板本地递交不添加远端站点或伪造 hop/WKC；
真实 raw echo 的身份、TX 历史及本机邮箱字节必须可关联，丢失回帧通过超时报告失败。
现有 process-image 允许 latest-value 合并；逐步请求需持久保持至 ACK，或采用有界队列，
不能用会被覆盖的瞬时 READY 值表达每一步。协议须先核算既有 mailbox 字节预算，
显式携带请求/回执身份并处理重复、旧代际、缺片、溢出与超时，不覆盖 VDC 或绕过 admission。
报告中的A0编排、A1链路、A2馈源、A3测量分工仍作为后续背景；
本地 completed 只表示配置的编码/建立/可选状态动作，不能声明 VNA 测量或全节点完成。
组合协议以 `trigger_sequence_link_protocol.h` 的消息、`TRIGGER_SEQUENCE_LINK_WIRE_SIZE`
和分片符号为准，携带完整 run/generation/binding epoch/step、源/目标逻辑槽位及
`exchange_id`。每个新 LINK_APPLIED，包括暂停恢复和重启，分配新的非零 exchange；READY_NEXT
必须回显该 identity。通过既有 CONTROL mailbox 分片传送时，重发保留身份，缺片、重复、旧
run/generation/step 或旧 exchange 均不能额外推进。
本次同板绑定以真实启用的角色装载行和 model epoch 为边界；完整动态 claim/lease 路由的
目标仍按 TODO 推进，不能将这一本地绑定等同于多板 claim resolver 已全面接入。

本轮逐层验证：纯C模型、真实libscpi解析、运行时与IRQ模拟、Release构建、
单板SCPI与实际IO读回、接线后的IN1-IN4脉冲与波形。
外部验证按实际接线逐输入推进，不用SCPI汇总计数替代独立输入/输出波形证据。
旧P3缺板失败和提交门禁结果如实保留。sequence 单板替代门禁由
`sequence_single_board_gate.py` 的 `SOURCE_ALLOWLIST` 限定，`run` 固定执行有限轮次和连续
PAUSE/CONT profile，并绑定 staged 源码、固件包、OTA 摘要和原始报告摘要；`check-staged`
只读核验。三模式扩展同时固定运行独立IN1有限轮次、START状态/热加载、POSITION完整位置/
忙边界/生命周期和RefMem布局产包回归；绑定各验证器、报告及软件测试日志/JUnit摘要，
输入频率必须显式记录，功能观察窗口不充当200ms性能条件。新增必要文件逐项列入白名单，
不放行整个目录；任何白名单外源码仍使用 P3。该凭证只表示单板 RJ45 功能，不表示 P3、多板、
外部波形、RF、独立边沿计数或严格 TDMA 稳定性通过。

## NSEQ-A-06 后续PIO执行与热加载边界

序列后端使用 PIO0 加速，资源分配和热加载均经 SYNC_IO owner 管理。
此前 GPIO IRQ/alarm 版只作为历史记录，不用于证明当前 PIO 后端的时序或吞吐。

执行资源以`docs/sync/SYNC_IO_ARCHITECTURE.md`的`ARCH-PIOPARTITION-01`、
`boards/rp2350_trig/inc/board_config.h`的`BOARD_TDMA_SMA_PIO_BLOCK_ID`、
`BOARD_TDMA_TX_PIO_BLOCK_ID`和`BOARD_TDMA_RX_PIO_BLOCK_ID`为起点核对。
用户已明确快速序列触发使用PIO0，PIO1/PIO2保持现有TDMA职责。
不把历史 AUX alias 当作空闲资源，不修改 board 分区。GPIO 版与当前 PIO 版证据分别记录。

热加载通过SYNC_IO owner/persona生命周期实施，复用`sync_io_persona_manager`：

- 描述并仲裁PIO/SM、instruction words、GPIO读写、FIFO、DMA/DREQ、IRQ及workspace。
- 切换前关闭新事件准入，按暂停/停止语义完成或取消在途工作，进入quiesced边界。
- 停止旧SM及其DMA/IRQ，清理私有FIFO和旧边沿，再卸载/装载程序并初始化执行位置。
- 完成全部准备后原子发布新generation并重新准入；失败恢复旧persona或保持STOPPED，
  不留下部分claim、活动DMA或未归属的引脚驱动。
- STOP/故障清理只处理自身资源；重复加载/卸载、资源不足、装载失败、换源及跨周期回绕
  必须分别验证，旧generation事件不能在新程序恢复后重放。
- 硬件承担单步触发与输出时序，CPU处理配置和汇总；执行/完成游标仍来自真实执行事实，
  不使用DMA预填数量冒充切换完成。提速结果需输入/输出共同波形及吞吐证据。

独立模式使用 `sync_io_sequence.pio` 的 ingress 或 finite_ingress、executor、counter 程序。
组合模式按需加载gateway、none_executor、ready_counter；有转台时另载counter并借用capture
的SM，保持其程序驻留。各程序预算由实际汇编长度及`sync_io_sequence.c`的静态断言校验。
IN1-IN4 READY 使用grant门控的累计counter/DMA；MANUAL READY不启动输入counter或边沿DMA，
将gateway握手指令替换为带相同前置延迟的NOP，只经命令槽完成当前READY等待。
gateway与READY在ARM/CONT同步启动并保持运行，FIRE仅校验状态和TX FIFO后递交倒计数，
不再每次abort DMA或重启SM。PIO内grant/ACK先让READY进入边沿等待，再输出触发脉冲；
输入初始已有效不阻止触发输出，但仍须经历inactive→active才能接纳，不能把旧电平当新边沿。
单次触发最多发布一枚 READY 回执，脉冲拉低后由 PIO FIFO 发布独立完成凭证，
READY 和脉冲完成前均不得切换 DUT。完成PUSH保留FIFO背压，不静默丢弃凭证。
PAUSE取消网关等待/脉冲，先退役DMA再清旧grant/FIFO和累计baseline；恢复由协调器重建当前测量；
当前 PAUSE 仍保留编码和统一 SMA 租约，安全释放/恢复策略的后续工作在 TODO 中显式保留。
保守persona指令空间预算引用`SYNC_IO_SEQUENCE_INSTRUCTION_WORDS`，不把各模式程序长度相加
作为同时驻留需求。保留常驻capture；序列占用的SM与analyzer有冲突，
即使指令空间有余也不能并发申请同一SM。DMA按实际SDK已claim资源过滤，私有回执环容量
引用`sync_io_sequence.c`的`RECEIPT_WORDS`，满环或DMA错误转FAULT，不覆盖未消费回执后继续报成功。

时基为`SYNC_IO_SEQUENCE_TICK_NS`；sysclk不能精确整除目标PIO频率时拒绝ARM。
PIO0序列基础拍与`BOARD_SYS_CLOCK_HZ`驱动的TIMER1对齐，当前源码配置快照为250MHz、4ns，
以这两个代码符号为事实源。延时换算使用`SYNC_IO_SEQUENCE_TICKS_PER_US`，单段上界使用
`SYNC_IO_SEQUENCE_TIME_MAX_US`，SCPI和GUI仍按微秒配置；PIO1/PIO2保持TDMA分工。
更细的基础拍只减少量化误差，不等于VDC同步精度、输入捕获或输出物理边沿已达到同等精度。
建立时间和脉宽按ARM中倒计数公式补偿固定指令周期。零建立时间仍有固定指令延迟，
不是同一时刻完成编码和完成输出；具体延迟需实测，不将PIO模型当作示波器证据。
暂停/停止读取计数边界时短暂冻结输入SM；边界窗口和可接纳最小脉冲宽度尚未用波形验收，
不得宣称任意频率无遗漏。独立外部输入模式的单步准入和输出时序不依赖 CPU 逐步发车；
组合模式经 TDMA/协调器发出显式步骤命令，触发脉宽与 READY 捕获仍由 PIO 执行。

## NSEQ-A-07 组合角色事件流水线与所有权

运行期LINK状态、位置历史及IO动作决策由Core1唯一推进。Core0在停止配置事务内验证角色表，
START冻结运行配置；Core1只检查原子model epoch，不借用可变角色表指针。
配置写入与Core1运行入口使用有界尝试的writer guard互斥；普通运输回调不取得这个guard，
也不直接更改LINK游标或调用网关FIRE/切步。

运输桥由Core1的RefMem flight阶段拥有重组器和发送游标；LINK发布不可变发送offer，
运输阶段递交片段。Core0负责配置，不参与运行期逐片发送。
完整真实RX消息通过`TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY`限定的事件邮箱投递，
Core1每次服务最多消费一个消息。接纳前检查run/generation/binding/step/exchange、
逻辑路由和当前phase，旧事件、重复事件及非授权来源不能触发IO。邮箱满必须显式故障，
不能静默覆盖或丢弃已接纳动作；STOP和新运行撤销旧发送权限及待执行事件。

Core1的mandatory调用按PIO/owner事实更新、LINK事件迁移、已提交IO意图执行的顺序进行；
没有LINK意图时不增加独立SP8T的执行调用。普通命令槽暂忙时保留当前事件供下一次有界服务
重试，STOP优先。此所有权结构不等于已经具备跨phase特等席或完整优先收发，优化范围和
完成条件由TODO追踪，不能以主机轮询周期或软件观察时间代替硬件时限。

独立SP8T、DUT/VNA与COUNTER/DUT/VNA按模式动态装载PIO persona；不按所有功能同时常驻
预留程序。已由PIO/DMA自主完成的编码、建立时间、脉宽和计数不迁回CPU轮询；进一步硬件
握手下沉须遵守NSEQ-A-06资源、热加载及异常恢复边界，跨PIO消息仍须完整身份校验。
