# 单节点序列触发架构

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`, `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.html`, `docs/reports/distributed-trigger/相控阵测试系统RP分布式触发方案技术报告0804.html`, `docs/sync/SYNC_IO_ARCHITECTURE.md`
Last updated: 2026-09-15

## 文档接口与范围

[TODO](TRIGGER_SEQUENCE_TODO.md) 维护待办，[Task Progress](TRIGGER_SEQUENCE_TASK_PROGRESS.md)
记录实际测试、板端事实和失败。本文为实施草案，不登记新的冻结契约。

用户明确本轮独立节点调试，不触碰TDMA，全节点裁决后续实现。
本轮链路为：配置序列 -> START等待 -> 软件STEP或所选IN的TRIG ->
输出本节点编码 -> 等待建立时间 -> 独立OUT完成脉冲 -> 等待下一事件。
编码及建立时间是当前本地动作定义；不声明已经切换实际仪表频点、波位或完成测量。
一个被接纳的事件推进一个状态；DONE不推进。末步之后的下一事件回到首状态。
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

CRC按 `TRIGGER_SEQUENCE_CRC_SCHEMA`，复用 `pota_crc32_update`，显式LE32编码。
参数CRC字段chan/pol/freq/wave；映射CRC为所有状态行state_id/switch1_ch/
switch2_sel/pol/freq_idx/wave_idx；计划CRC为parameter_crc/map_crc/count/state_ids。
generation单独验证，不把名称、generation或结构体padding混入内容CRC。

本地IO配置和每状态code另绑定同一参数generation。重新配置参数令IO/code失效；
更换编码输出掩码令旧code失效。所有active引用都必须配置合法code才能START。
code只允许设置编码掩码内的位；编码掩码与完成通道必须分离。
建立时间允许零，完成脉宽必须正数；上界引用 `SYNC_IO_SEQUENCE_TIME_MAX_US`。

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
| CONFigure:SEQuence:CODE state_id,value | 配置本节点状态编码；READ:SEQuence:CODE? state_id读回 |
| TRIGger:STARt [plan_id] / STOP / ABORt / PAUSe / CONTinue | 真实owner控制；STOP/ABORt取消未完成步骤并安全输出 |
| TRIGger:SEQuence:STEP | 仅BUS模式推进一次；外部模式拒绝软件STEP，防止混源 |
| READ:SEQuence:STATe? | 本地完整状态、游标、计数、时间与错误 |
| READ:TRIGger:STATe? | 保留指令表字段排列；未实施角度流程的字段不伪造角度进度 |
| READ:IO:INPut? / OUTPut? [channel] | 无参数返回实际逻辑位掩码，有通道返回该通道实际电平 |
| READ:IO:STATe? | input_mask,output_mask,sequence_owned_mask,armed,busy |

IO通道和掩码按逻辑IN/OUT，不接受旧硬编码GPIO范围。第一通道对应最低位。
`CONFigure:SEQuence:NEXT`与`TRIGger:SEQuence:STEP`共用单步入口，BUS且READY时接纳一步。
`READ:SEQuence:NEXT?`返回与`READ:SEQuence:STATe?`相同的运行状态块，供查询单步结果；
查询无执行副作用。两者均不带参数，不提供`CONFigure:SEQuence:NEXT?`。
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
| IDLE -> STARTING -> READY | 校验并冻结计划/code/IO，申请资源，等待首个事件；无初始输出动作 |
| READY -> BUSY | 只接纳一枚事件，选择next_index，写code；实际写出后记录executed |
| BUSY | 等待建立时间，输出完整完成脉冲；脉冲下降后记录completed并恢复READY |
| 忙时TRIG/STEP | 拒绝并计数，不排队、不覆盖在途步骤、不恢复后重放 |
| 末步完成 | 保持READY，next_index回到首状态，下一枚事件才执行首状态 |
| PAUSE | 立即停止新准入，已接纳步骤完成后进入PAUSED |
| CONTINUE | 返回READY等待新事件，保持后继索引，不自动执行 |
| STOP/ABORT | 优先取消待执行命令和定时，完成/编码输出拉低，释放资源；保留中止事实 |
| 再次START | 新run身份，首状态等待，重置本轮游标和计数 |
| FAULT | 不发成功完成通知、不自动推进；保留失败原因，STOP后才能重新START |

完成脉冲结束前均视为BUSY。外部发送方应等待完整完成脉冲再发下一TRIG。
启动时已有电平不作为新边沿；输入切换只能在停止边界，旧事件不得重放。
运行、暂停、故障或待处理START/STOP期间配置冻结；owner停止完成后才允许配置。

## NSEQ-A-04 Owner与硬件

Core0唯一拥有可写配置和SCPI解析。START复制有界运行快照，通过受保护命令槽
发布给现有Trigger服务；Core1唯一拥有本地执行状态。Core0不能直接写GPIO或推进游标。
状态查询使用受保护副本；借用配置指针不得跨核。FB/服务动作有界返回，无阻塞等待。
序列命令服务在应用Core1的强制调度阶段处理，在线时位于既有TDMA和analyzer之后、
阶段计量结束之前；离线维护提前返回分支也处理邮箱。不可依赖可禁用或隔离的
legacy Trigger负载槽，也不能在周期计时之前插入未计量的硬件动作。

当前迁移后端归sync_io，按`SYNC_IO_PERSONA_ID_SEQUENCE`在PIO0热加载。
整计划在ARM前准备，PIO处理输入准入、编码建立及完成脉冲，DMA传递计划和真实执行回执。
CPU汇总回执并处理控制邮箱，不消费VDC的破坏性capture队列，不参与逐步输出门控。
回执异常、溢出和硬件资源冲突必须如实报告，不能用DMA预填数量代替执行或完成。
此前GPIO raw IRQ/alarm版只作为进度008/010的历史验收对象；PIO版验收状态见TODO。

SMA资源从START管理预留起独占，现有输出任务活跃时拒绝启动。
新运行存在时，legacy输出/波形/PWM/SEQ/ENC相关写操作拒绝，不抢占输出。
申请失败、STOP和异常均停止并回收本persona的SM、程序、DMA和资源租约；
只清理自身资源，输出恢复安全电平。不得清空PIO0其他persona的程序或capture队列。
本轮不改TDMA、VDC运行逻辑或总线布局。

## NSEQ-A-05 后续与验收

未来TDMA通过带run/generation/step/state的本地请求和完成结果接口衔接。
报告中的A0编排、A1链路、A2馈源、A3测量分工仍作为后续背景；
当前completed只表示本节点配置的编码/建立/完成脉冲动作，不能声明全节点完成。

本轮逐层验证：纯C模型、真实libscpi解析、运行时与IRQ模拟、Release构建、
单板SCPI与实际IO读回、接线后的IN1-IN4脉冲与波形。
外部验证按实际接线逐输入推进，不用SCPI汇总计数替代独立输入/输出波形证据。
旧P3缺板失败和提交门禁结果如实保留，不修改验收工具或伪造凭证放行。

## NSEQ-A-06 后续PIO执行与热加载边界

用户要求后续使用PIO加速，先参考SYNC域分配，并注意PIO热加载。
低频外部触发流程已在此前GPIO IRQ/alarm版验证，当前开始PIO0迁移。
后续不能只降低等待参数或扩大CPU调度配额就宣称已经完成PIO迁移。

执行资源以`docs/sync/SYNC_IO_ARCHITECTURE.md`的`ARCH-PIOPARTITION-01`、
`boards/rp2350_trig/inc/board_config.h`的`BOARD_TDMA_SMA_PIO_BLOCK_ID`、
`BOARD_TDMA_TX_PIO_BLOCK_ID`和`BOARD_TDMA_RX_PIO_BLOCK_ID`为起点核对。
用户已明确快速序列触发使用PIO0，PIO1/PIO2保持现有TDMA职责。
不把历史AUX alias当作空闲资源，不修改board分区。PIO0迁移正在实施，
此前低频验收仍只证明GPIO IRQ/alarm版本，不能替代新后端验收。

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

当前实现使用`sync_io_sequence.pio`的ingress/executor/counter程序，预算引用
`SYNC_IO_SEQUENCE_INSTRUCTION_WORDS`。保留常驻capture；序列占用的SM与analyzer有冲突，
即使指令空间有余也不能并发申请同一SM。DMA按实际SDK已claim资源过滤，私有回执环容量
引用`sync_io_sequence.c`的`RECEIPT_WORDS`，满环或DMA错误转FAULT，不覆盖未消费回执后继续报成功。

时基为`SYNC_IO_SEQUENCE_TICK_NS`；sysclk不能精确整除目标PIO频率时拒绝ARM。
建立时间和脉宽按ARM中倒计数公式补偿固定指令周期。零建立时间仍有固定指令延迟，
不是同一时刻完成编码和完成输出；具体延迟需实测，不将PIO模型当作示波器证据。
暂停/停止读取计数边界时短暂冻结输入SM；边界窗口和可接纳最小脉冲宽度尚未用波形验收，
不得宣称任意频率无遗漏。正常RUN的单步时序不依赖CPU逐步发车。
