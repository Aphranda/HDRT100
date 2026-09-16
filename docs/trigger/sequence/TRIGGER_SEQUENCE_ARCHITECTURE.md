# 单节点序列触发架构

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`, `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.html`, `docs/reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.html`, `docs/sync/SYNC_IO_ARCHITECTURE.md`
Last updated: 2026-09-17

## 文档接口与范围

[TODO](TRIGGER_SEQUENCE_TODO.md) 维护待办，[Task Progress](TRIGGER_SEQUENCE_TASK_PROGRESS.md)
记录实际测试、板端事实和失败。本文为实施草案，不登记新的冻结契约。

目标为单板承载 DUT_LINK_CONTROL 与 VNA_GATEWAY 的最小测试系统。两角色协同控制接入本地回环
TDMA（RJ45 发收物理回环），使用专门的 LOOPBACK 模式，复用现有 TDMA owner、消息身份、调度与资源边界；本地结果不宣称跨板链路或全节点裁决通过。
独立 `CONF:SWITCH# N` 的 SP8T 控制及 DUT-only 的 BUS/IN 序列能力保留，不依赖 VNA 装载。
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
START预置不计入accepted/completed，也不产生完成脉冲；READY时current为首状态、next为
第二状态。一个被接纳的事件推进到后继状态；首次事件执行第二状态，DONE不推进，
配置轮次尚未耗尽时，末状态之后的下一事件回到首状态。轮次默认值引用
`trigger_sequence_service_init()` 的 `s_repeat_count` 初始化；零表示显式持续运行。
正轮次以完整计划计数，START 的首状态算入轮次；独立序列的推进上限为
`plan.count * repeat_count - 1`，最后状态完成后进入 IDLE，置 finished 并拉低输出、释放资源。
组合模式须完成最后状态对应的 VNA 采样 READY 后结束，不再发送下一状态请求。
先完成SCPI控制与IO读取，再按实际接线逐输入验证；执行进度与信号源参数见Task Progress。

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
| CONFigure:SEQuence:SOURce BUS或IN1-IN4,RISING或FALLING | 选择软件或外部入口；READ:SEQuence:SOURce?读回 |
| CONFigure:SEQuence:IO mask,OUT通道,settle_us,pulse_us | 原子配置本地动作输出；READ:SEQuence:IO?读回 |
| CONFigure:SEQuence:OUTPut code_mask,status_mask,NONE或PULSE或LEVEL,settle_us,pulse_us | 显式输出角色配置；NONE 只输出 DUT 编码；READ:SEQuence:OUTPut? 返回掩码、模式、时间、generation、valid |
| CONFigure:SEQuence:CODE state_id,value | 配置本节点状态编码；READ:SEQuence:CODE? state_id读回 |
| CONFigure:SEQuence:REPeat count / READ:SEQuence:REPeat? | 停止时配置完整计划轮次；零显式持续；查询 configured_repeat,active_repeat,finished；START 校验 `plan.count * count <= UINT32_MAX` |
| CONFigure:SEQuence:LINK OFF或LOOPBACK,dut_slot,vna_slot,IN通道,OUT通道,pulse_us,timeout_ms,RIS或FALL | 在序列和 TDMA 均停止后配置专门的同板 RJ45 物理回环与网关 IO；RJ45 仅作兼容别名；组合模式要求 BUS/NONE，网关输出不能重叠编码输出 |
| READ:SEQuence:LINK? | 返回 `scpi_sequence_link_q` 的 enabled、phase、error、binding/model epoch、run/generation、step、收发/拒绝/触发/READY/完成计数、角色/IO/时序、活动轮次及当前 exchange identity；字段顺序以该处理函数为准 |
| READ:SEQuence:LINK:TRANsport? | 返回 local-return 诊断和 `tdma_local_return_snapshot_quality_t` 质量；`FRESH`/`CACHED` 均为一致快照，`UNAVAILABLE` 是显式可观测状态，不通过 SCPI 失败或遗留错误队列表示 |
| TRIGger:STARt [plan_id] / STOP / ABORt / PAUSe / CONTinue | 真实owner控制；STOP/ABORt取消未完成步骤并安全输出 |
| TRIGger:SEQuence:NEXT | 独立 BUS 模式直接请求一步；LOOPBACK 模式只在 `LINK_WAIT_READY` 提交软件 READY，随后仍须由真实 RJ45 回环的 READY_NEXT 回帧请求 DUT 切步；外部 IN 模式拒绝，防止混源 |
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
独立 BUS 模式在 READY 时接纳一步；LOOPBACK 模式在等待 READY 时结束当前网关等待，发布
READY_NEXT，并且只有该消息经过真实 RJ45 发送、接收和完整 identity 校验后才请求 DUT 切步。
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

`READ:SEQuence:REJections?`返回run_id,generation,busy_rejected,notready_rejected,pending。
PIO外部运行期间输入计数与执行回执来自异步DMA，拒绝数保留最近稳定结算值，pending为true；
暂停或停止完成排空后结算。pending为true时，零拒绝不能作为无丢失或无忙时拒绝的证据。
软件模式的指令拒绝直接计数。外部验收需在脉冲源结束后暂停并读取pending=false及最终计数。

## NSEQ-A-03 状态与握手

| 状态或事件 | 行为 |
|---|---|
| IDLE -> STARTING -> READY | 校验并冻结计划/code/IO，申请资源，输出首状态并等待初始建立时间；期间关闭事件准入，不计accepted/completed且不发完成脉冲 |
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
START成功取得资源后由PIO owner预置首状态，初始建立时间到期且executor就绪后才启动输入
counter/ingress；DMA循环从第二状态开始，末状态后回到首状态。该预置不伪造执行回执。
有限外部模式由 `sequence_finite_ingress` 的 Y 寄存器执行步数配额；忙时不扣配额，
最后一枚接纳事件后进入 parked，不依赖 CPU 轮询停机。暂停边界结算尚未扣除的接纳事件，
恢复不能清空配额或重启已 parked 的 ingress。BUS 模式由 owner 检查同一上限。
零推进运行仍建立 START 首状态并等待 settle，随后发布 finished。
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
只读核验。任何白名单外源码仍使用 P3。该凭证只表示单板 RJ45 功能，不表示 P3、多板、
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

当前实现使用 `sync_io_sequence.pio` 的 ingress 或 finite_ingress、executor、counter 程序。
组合模式把未使用的 BUS ingress 替换成 gateway 脉冲程序，counter 捕获网关 READY；
每次 fire 先排空旧 DMA/FIFO 后重新设定捕获边界，已为有效电平的输入不冒充新边沿。
单次触发最多发布一枚 READY 回执，脉冲拉低后由 PIO FIFO 发布独立完成凭证，
READY 和脉冲完成前均不得切换 DUT。PAUSE 取消网关等待/脉冲，恢复由协调器重建当前测量；
当前 PAUSE 仍保留编码和统一 SMA 租约，安全释放/恢复策略的后续工作在 TODO 中显式保留。
指令空间预算引用
`SYNC_IO_SEQUENCE_INSTRUCTION_WORDS`。保留常驻capture；序列占用的SM与analyzer有冲突，
即使指令空间有余也不能并发申请同一SM。DMA按实际SDK已claim资源过滤，私有回执环容量
引用`sync_io_sequence.c`的`RECEIPT_WORDS`，满环或DMA错误转FAULT，不覆盖未消费回执后继续报成功。

时基为`SYNC_IO_SEQUENCE_TICK_NS`；sysclk不能精确整除目标PIO频率时拒绝ARM。
建立时间和脉宽按ARM中倒计数公式补偿固定指令周期。零建立时间仍有固定指令延迟，
不是同一时刻完成编码和完成输出；具体延迟需实测，不将PIO模型当作示波器证据。
暂停/停止读取计数边界时短暂冻结输入SM；边界窗口和可接纳最小脉冲宽度尚未用波形验收，
不得宣称任意频率无遗漏。独立外部输入模式的单步准入和输出时序不依赖 CPU 逐步发车；
组合模式经 TDMA/协调器发出显式步骤命令，触发脉宽与 READY 捕获仍由 PIO 执行。
