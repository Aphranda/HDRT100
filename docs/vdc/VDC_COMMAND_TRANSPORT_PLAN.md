# VDC 定时命令与共同时间传输方案

Status: Draft
Domain: VDC
Canonical: `docs/vdc/VDC_COMMAND_TRANSPORT_PLAN.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-09-16

本文是 `VDC-CMD-001` 的待审方案，服务于 `VDC-LONGTERM-001`，不冻结 wire 契约，
不允许据此启用从机控制，也不替代调度、角色、Calibration 和正式锁相门禁。
当前证据与 host 反例见 `VDC-PROGRESS-20260914-004`；自主 origin 输入缺口及
目标模式补测见 `VDC-PROGRESS-20260914-007`。

当前执行顺序以 `VDC_DOMAIN_TODO.md` 的 `VDC-FLIGHT-001` 至 `VDC-RECOVERY-001`
为准：先接通普通特等席数据接收，再将各从 internal 反馈送回主机，由主机形成各从
专属校正并在各板 SyncDpllFB 应用，随后调频率漂移和相位。本页完整共同时间与
定时命令方案保留为待审路线，不作为普通数据运输的前置。复制主机自身 DCO 快照
不等于各从闭环；现有命令原型继续默认关闭，下一控制切片须明确有效测量关联、
目标、身份、有效期与应用边界，不能直接放宽旧原型校验。
首帧诊断归 TDMA 后续优化，预热后从有效样本开始，偶发坏帧跳过并保持控制输出；
样本策略见 `VDC_STABLE_INPUT_PLAN.md`。

### 当前候选：各从反馈与有界校正

这是 `VDC-FEEDBACK-001` 的方案入口，尚未冻结或接线。源码审计索引见
`VDC-PROGRESS-20260916-003`，按以下最小切片逐项验证：

1. 保留 NO1 准备发布的确切数据版本，与从板实际接收的同序记录对账；准备成功本身
   不等于实际发出。复用固定邮箱，不增加独立帧，也不恢复首帧特殊前置。
2. 优先直接消费各从 PIO internal 观察器的 RX/TX 边沿与独立线上序列，不经过 DMA
   数据帧候选；因此 DMA 字节候选到事件 ordinal 的关联不是该入口的前置。须核实
   观察器各采样流内部的同次 CS 配对、相对 CS 的序列采样偏移和采样相位，不能直接
   使用 DMA alignment 的偏移。首个序列仅作本地基线，可以跳过；观察器失效只退休
   并重新建立自身 epoch，健康 TDMA 继续运行。此候选不更改旧 history 的资格标志。
   在各从的同次有效 internal 观测上保留原始误差、真实测量序号、指定参考和当时
   已应用的 DCO 版本，形成能反映输出校正效果的残差。现有
   `vdc_domain_corrected_phase_error_ns()` 使用 `clock`，而
   `vdc_domain_apply_follower_command()` 修改 `dco`；不能直接假定旧曲线随新命令收敛。
   对齐 NO1 信号时，参考须对应 NO1 同次发射事件的 DCO 相位；物理 TDMA 节拍不能
   静默替代 NO1 信号。无有效观测时保持反馈无效，数据运输仍继续。
   输出域转换只需先闭合本板硬件时钟锚和当时应用模型，不要求完整跨板绝对时间；
   timer uptime、PIO elapsed、逻辑环路时间与 DCO 本地锚不能直接混用。
3. Core0 经特等席发送完整反馈，NO1 按从节点分离接收、排序和保存。NO1 的
   SyncDpllFB 为每个从节点维护独立、定长的控制状态；先按实际有效测量间隔验证
   频率校正，再调相位与积分。运输 publication sequence 不替代 measurement sequence。
4. 主机在自己的 mailbox 内按有界目标轮换发送专属命令，从板 SyncDpllFB 验证后
   在约定 service boundary 应用并返回应用身份。候选采用单独定义的边界命令，
   不将本地 uptime 填入旧 `effective_vdc_time_ns`。更新 rate 时须明确本地锚重定基和
   相位连续性；重复命令不能重复叠加相位步进。

候选编码可复用既有固定 VDC 区并显式区分反馈/命令类型；测量关联、真实应用 ACK、
旧会话拒绝及长周期量程必须能容纳，无法容纳时再比较小记录分片。现有普通
phase/rate/lock/quality 语义不能暗改，RefMem 接收 ACK 不能冒充 DCO 应用 ACK。
精确字段、版本、有效期和服务边界先完成域文档及交叉审核，再进入固件。

快速验收同时保留 raw 与输出模型残差，先以可追溯的小校正证明响应方向，再看斜率。
`vdc_dpll_manager_refresh_dco_consumer_status_core0()` 的 accepted_update_count 只表示
Core0 读到并验证模型；实际应用须以 Core1 命令/应用序号、本地应用边界和输出模型
版本对账。每个切片单独完成 host、Release、四板 quick P3 及专项，最终输出精度再用
示波器复核；不要求每轮依赖外部仪器。

独立 PIO 采样入口已增加 STOP 配置、ARM 冻结和历史读回，实施证据见
`VDC-PROGRESS-20260916-006`。`SYSTem:TDMA:EVENt:TAP` 提交 enabled、相对 CS 的
prefix_bits 和 WAIT-high delay；`TAP?` 分别读取 requested、applied 和实际装载值。
Core0 发布期间持有既有 ring control guard，Core1 独占冻结与 PIO 编程；所有配置
请求要求物理 STOP 完成且未选择 frozen geometry，显式模式不读取 DMA alignment。
actual_valid 只表示指令及 OSR 已装载，STOP 后仍作为历史保留，不表示当前观察器
运行或时间戳有效。帧头实验的常量序列拒绝保留为正常诊断结果，不能赋予 DPLL 资格。
后继将保留同次有效边沿/测量序号和本板时钟锚，经固定配额发送反馈；此接口本身
未建立反馈运输、NO1 控制器或从板 DCO 应用，也不冻结新 wire 契约。

观察器自身恢复切片见 `VDC-PROGRESS-20260916-007`。显式 tap 下纯 SEQUENCE 拒绝
会先记录失败、退休当前 epoch；后继 service 分开执行局部 reset 与一次 idle/start
尝试，继续使用同一 ARM 冻结的采样配置。STOP、绑定/时钟变化、epoch 耗尽或硬件
fault 取消恢复；每个新 epoch 的序号、ordinal 和整批检查仍保持严格。恢复不重新
ARM TDMA、不修改 DMA 或转发 SM，也不重采物理 ARM 的首次 RXSTARTCUT 档案。
`SYSTem:TDMA:EVENt:RECovery?` 只读累计失败、尝试、启用、延期和取消计数，以及
最后失败与 ARM/tap 绑定。enable_count 表示成功启用观察器，持续观测仍须用恢复后
同 epoch 的 joined/sequence/ordinal 增长证明；batch_sequence_first 只是该次采集批次
首词，不能与最后发布的样本拼接成同事件记录。该切片尚不提供 DPLL 更新或输出精度。

同条观测留存切片见 `VDC-PROGRESS-20260916-008`。Core1 只在最终 fault 复验成功后
保留 batch 的最后一条完整 `tdma_event_record_t`，并将其与同 observer epoch 的
TIMER1 enable 前后区间、ARM 身份和 tick_hz 一起发布。新 epoch 锚先存入私有候选，
不能在新记录形成前覆盖旧历史记录的锚。Core0 经独立有界 getter 取得 SRAM 副本，
`SYSTem:TDMA:EVENt:LIVE?` 导出原始字段，不扩大 native EVENT ABI 或 wire 格式。

`ANCHOR_VALID` 表示所保留记录生成时本板锚获取成功；STOP、INVALID、新 ARM 或
时钟配置失效撤销 `ACTIVE` 并保留原因，历史 record/锚不被改写。当前候选消费须同时
检查 `ACTIVE`、`ANCHOR_VALID`、来源与生命周期。锚读取无懒初始化、无等待循环，
读取失败或时钟不可用不阻塞 TDMA。这里的 TIMER1 raw tick、record 的 ARM 相对
cycle 区间与 DPLL manager 的 time_us 时间原点不同，不能直接混算；当前寄存器
检查也不证明期间未发生 debug pause。所有记录保持 diagnostic-only，不授予
timestamp_valid 或 dpll_eligible；反馈运输和实际控制按后继独立切片接通。

### 原始反馈运输实施候选

`VDC-FEEDBACK-001` 的原始反馈切片直接接通各从完整原始观测到 NO1，候选审计见
`out/HardwareAcceptance/20260916/dpll-event-live-r1/next-feedback-audit-r1.json`。
本节数字是诊断实现快照，非冻结 wire 契约；实际常量以
`refmem_sync_vdc_feedback.h`、`tdma_process_image_layout.h` 为准，验收见
`VDC-PROGRESS-20260916-009`，不授予控制或锁相资格。

候选沿用固定 mailbox、广播 target mask 和全部 RefMem/ACK/Control 区，只用新的
诊断 class `0x12` 区分 VDC 区解释。普通 class 和默认关闭的旧命令 class 不变；
其他节点仍消费 RefMem，不能把反馈字节解析为 phase/rate，也不能因内层反馈仅给
主机而缩窄整个 mailbox 的 target mask，破坏其他节点的 mailbox presence/WKC。
TDMA 的 RX/overlay 准备、origin adapter 和物理 seed/publish 校验统一通过
`tdma_process_image_transport_class_valid()` 准入普通与反馈 class；仅修改 RefMem
parser 不足以接通飞行路径。旧命令 class 和未知 class 继续拒绝。

完整记录按小端显式编码为 64 B，依次为 schema/source/target/domain_flags（各 1 B）、
source clock epoch/run（各 4 B）、source ARM（8 B）、observer epoch/measurement
sequence/tick_hz（各 4 B）、RX/TX elapsed（各 8 B）、TIMER1 enable before（8 B）、
enable width（4 B）和 CRC32（4 B）。schema 为 1，flags 固定为 `0x07`：bit0 表示
本地 TIMER1/PIO clk_sys 原始域，bit1 表示历史采集锚存在，bit2 表示仅诊断；不表示
公共时间或当前控制资格。CRC32 采用 IEEE reflected `0xEDB88320`，初值及末异或均
为 `0xFFFFFFFF`，覆盖前 60 B；`123456789` 校验值为 `0xCBF43926`。
width 超出编码范围、倒序锚或不合法域必须拒绝，不截断。完整 LIVE 继续在本地保存。

每个 VDC 区传 index/count 和 4 B 数据，共 16 片；Core0 在组首冻结一次完整记录，
仅 FIFO 发布成功才推进片号及邮箱序列。组完成后成功发布一条普通 VDC mailbox，
再捕获新的 LIVE；没有新记录时继续普通数据。输入须来自当前显式 TAP，且 LIVE
为 ACTIVE、ANCHOR_VALID，配置/角色/时钟及 ARM 绑定在复制前后相符。

NO1 按编译节点容量分配独立组装区，并按运行准入节点检查索引。片段使用 mailbox
完整 seq32，保留现有跳过零的回绕规则；旧 index0 按半区序判旧，不得复位新组。
重复相同片幂等，不前进或续时；冲突、缺片及乱序只取消该源部分组装，合法新组首
可重新开始。总组装时限候选为首片后 1000 ms，空队列也检查，毫秒回绕安全；
它是取消门限，不是交付 WCET。同 source clock epoch/run、ARM、observer epoch
内 measurement sequence 必须递增；相同完整记录不更新 complete 或新鲜度，
同序不同内容拒绝并保留旧完整记录。同 observer epoch 不允许测量序号回绕。

唯一 Core0 RefMem writer 管理发布/取消；FIFO admission tag 在普通接收与反馈间
统一复用，不能相互推进而失效。确认 STOP、DATA 暂停、角色/配置/绑定变化时取消
未完成 TX/RX，保留已完成诊断历史；暂时快照竞争只跳过本次服务。各板 epoch/run
是来源命名空间，不能与主机本地编号比较为共同会话；跨重启完整旧记录拒绝及控制
有效期仍须后续共享会话/同次主机参考证明，不能由本诊断通路授予 DCO 应用资格。

四板专项须逐源将 NO1 完整接收字节与从板完整发布历史对账，记录更新计数、测量
序号推进、实际组装时间及 STOP/ARM 取消。短 TX 历史不保证一定覆盖接收记录，
无法匹配时判证据不足，不能用最新 LIVE 拼造。NO1 当前原始记录缓存也不足以
承诺延迟反馈的同序匹配；后续控制另行补齐缓存及本地输出域关联。节点容量参数化
约束 RAM，保持 Core1 无解析等待；每切片完成软件、Release、四板 P3 与专项后再提交。

### 稀疏参考配对实施候选

诊断配对切片见 `VDC-PROGRESS-20260916-010`，不修改原始反馈 wire。Core0 发布
完整已解码反馈，Core1 在现有服务中有界取得一条 TDMA owner 的原始参考，并轮询
一个来源。缓存容量及区间范围以 `vdc_feedback_match.h` 为准；缓存条目的完整
测量序号和本地主机参考代际必须精确一致，缺参考不插值，源生命周期改变先退休
旧基线。本地参考代际不等于跨板共同会话身份；当前 raw wire 未携带后者，
共同会话尚未资格化。时间差的
原始 ppb 区间向外取整，保留两条配对证据便于独立复算；STOP 保留诊断历史但撤销
当前有效性。查询 `SYSTem:VDC:FEEDback:MATCh?` 仅供 STOP 后读取或 START 前基线。

该原始区间只反映输入参考括号，未覆盖物理检测不确定度，也未结合实际生效的
DCO 输出模型，不能直接作为各从控制误差。后继优先把参考缓存、配对与差分准备
迁入 Core0 单写者，复用既有发布缓冲；Core1 复验身份和退休代际后才消费。当前
raw reference facade 在 LIVE guard 外读取同核 phys 来源，迁移前须将 epoch/SM/CS
来源纳入一致快照，不能直接跨核调用。Core0 任务延时不等于固定服务覆盖保证，
还需实测缺项率、准备时间和实际时序收益。

### 当前实现快照（仍未冻结契约）

当前源码已经有一个受限的 resident VDC 命令运输原型，用于验证固定 process image
上的分片、重组和跨核交接边界。它不是本方案中尚未完成的共同时间契约，也不能单独
开放正式从机控制或锁相验收。实现边界如下：

- 当前按 `VDC-PROGRESS-20260915-028` 完成编译隔离：
  `DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED` 默认关闭，resident command
  的构造、发片、接收分派和私有组装状态均不进入默认编译；普通 mailbox 继续发布。
  通用命令校验和 follower 时间门禁仍保留，命令 class 在普通接收路径中被拒绝。
  该开关不恢复 standalone VDC 窗口，不能作为从机 apply 已恢复的证据。恢复次序和
  每项功能修改后的四板 P3 要求以 `VDC_DOMAIN_TODO.md` 当前执行入口为准。
- `TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS` 选择命令片段格式；mailbox header
  保留 source、target mask 和 `TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET` 运输序列，VDC
  区由 `TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET`、fragment count 和固定
  `TDMA_PROCESS_IMAGE_VDC_FRAGMENT_DATA_SIZE` 组成。
- `refmem_sync_vdc_fragment_push()` 只允许一个活动 source，要求片段从零开始、
  index 连续、运输序列在 `uint16_t` 回绕下连续，缺片、乱序、重复起始或 source/
  target 改变都会清理当前组装状态。
- 完整 payload 仍由 `refmem_sync_vdc_command_payload_validate()` 和内层 CRC
  校验；通过后才构造现有 `REFMEM_SYNC_FRAME_COMMAND`，由
  `refmem_sync_vdc_receive_frame()` 按 epoch/run、source、target、frame/command
  序列再次准入。
- 命令 payload 现在同时携带 `epoch_id` 和 `run_id`，并要求它们与接收端当前
  context 一致；在 context 身份已正确更新且稳定的前提下，旧 ARM/STOP 会话即使
  重新出现相同 source 或 command sequence，也会被接收校验拒绝。这不证明下述
  跨核 reset 生命周期已经闭合。payload 尺寸和分片数量只由
  `sizeof(refmem_sync_vdc_command_payload_t)`、`REFMEM_SYNC_VDC_FRAGMENT_COUNT`
  和 mailbox layout symbols 决定。
- 命令发布与复制使用 `vdc_command_guard` 和 `refmem_sync_vdc_copy_command()`；
  忙或撕裂时复制有界失败。`VDC-PROGRESS-20260915-030` 将接收 context 的刷新和
  退休收敛到 Core0 RefMem 任务，Core1 与 SCPI 任务不再直接清空；在线
  `refmem_sync_vdc_reset()` 保留发布序列，清空及身份更新都在 guard 内完成，
  `refmem_sync_vdc_init()` 只供并发读取开始前冷初始化。RefMem 快照读取失败时
  暂停命令准入，普通 flight 服务继续。Core1 在消费序号前独立拒绝 epoch/run
  错配，并在本地会话变化时重置序号水位。`VDC-PROGRESS-20260915-031` 进一步把
  guarded copy 绑定到 Core1 当前实际本地 role generation；Core0 观察到角色变化
  时只作废 retained 值并取消组装，保留各来源已接收的 command/frame 序号水位。
  同代际重绑定不取消待执行命令；parser 要求 transport 与接收 context 的本地
  generation 一致。该边界阻止已接收命令在 A→B→A 后复活。后继 FIFO 入站取消
  实现及证据见 `VDC-PROGRESS-20260915-032`：Core0 RefMem 在身份刷新时通过
  `tdma_service_core0_advance_flight_rx_admission_epoch()` 推进独立本地代际，
  Core1 在复制 RX payload 前捕获该值，slot/view 保留原值；接收端只取消旧代际的
  command mailbox，同 view 普通数据继续处理。同身份刷新不推进；推进失败时不
  提交新绑定，命令准入保持无效，代际耗尽不回绕复用。该字段不改变 wire，也不是
  map generation、本地 role generation 或远端 command generation。
  这只覆盖 producer 已捕获旧 tag 的 FIFO 发布，含排队及复制中途；DMA/station
  尚未进入该发布边界的旧输入和之后完整重发的旧记录仍需协议有效期/切换生效规则。
  边界是 Core0 观察并刷新身份，不能宣称精确等于 Core1 角色激活瞬间。
  `tdma_flight_fifo_reset_stopped()` 保留 admission epoch。后继借用保护见
  `VDC-PROGRESS-20260915-033`：Core0 FIFO guard 覆盖 publish 及 acquire 到 release
  的完整借用；同一 FIFO 同时只许一个 Core0 RX view，忙时不回收。service reset
  持控制锁并核物理 STOP ACK，SCPI 仅对 BUSY 有界让出重试；Core1 不使用该 guard。
  这解决借用中的 FIFO 回收与 Core0 任务交错，不表示命令区/session 已随 STOP
  完整退休。`VDC-PROGRESS-20260915-034` 进一步把 resident 命令的 guarded copy
  绑定到本地角色与已 ACK 的 TDMA config sequence：普通 STOP/ARM 即使未改变
  VDC epoch/run，也取消旧 retained 值和待准备记录/组装；同 wire 会话的排序水位
  保留。Core0 暂时读不到一致快照时保留状态并暂停本次准入；确认关闭或未 ACK 时
  绑定为零，普通数据继续。所有生产命令接收入口在关闭期间拒绝写入水位，Core1
  在 guarded copy 后、消费命令序号前复验环路配置。紧凑 clock snapshot 同时提供
  applied ACK 并复验配置/结果的双 guard，命令时间映射及观察准入核对 ACK。
  本地 config sequence 不等于分布式 session，零值仅关闭命令准入，不改变普通
  TDMA 的回绕语义；最终检查之后的 STOP 在后继 Core1 owner 边界完成。
  已进入 TX image/FIFO/PIO/DMA 的旧片段及之后完整重发的旧记录仍需协议有效期
  与共同 session 取消，不能宣称所有旧 TX 或命令/session 已完整退休。远端
  control generation 重启以及 schedule/STOP 取消仍需端到端负测；本地 role
  generation 与远端 command generation 不能直接比较。
  `vdc_domain_publish_clock_model()` 任意换会话后的 Domain history 退休也未验收，
  manager 测试中的 Domain 应用 stub 不能代替该证据。
- resident master 在一条记录的全部片段发完前保持记录不可变；完成后重复发送当前
  记录，直到新的 DPLL update。FOLLOWER 只接受配置的主机 source；计数器分别记录
  fragment RX、complete、reject、command accept 和最后 command sequence。
- `effective_vdc_time_ns` 使用 VDC owner 提供的 TDMA hardware-latched
  `common_effective_time_ns + (local_now - local_rx_timestamp_ns)` 映射；映射必须满足
  当前 schedule CRC、硬件时间戳、共同时间 flags 和 `feedback_timeout_ns` 新鲜度。主机
  无有效映射时不建记录，从机无有效映射时不比较 peer deadline、不调用 DCO apply。

重新启用前还须明确 payload 扩展后的 `REFMEM_SYNC_VDC_COMMAND_VERSION` 兼容决策，
以及实际发布间隔、FIFO 背压和重复片段下的完整交付上界；仅按分片数乘周期估算
effective time 提前量尚不足以放行。

上述格式当前只完成 host/目标构建和单测验证；共同 session、local-to-common 映射、
过期/取消上界、实际三从应用和命令启用态四板功能验收仍未闭合。因此本节是实现快照，不能作为
`VDC-CMD-001` 的冻结条款或登记表事实源；独立交叉审核完成前不得把其状态改为
active。

## 1. 需要解决的实际缺口

下表记录方案起点的源码缺口，用于解释改动动机；当前原型已加入 guarded copy、
共同时间检查和模差序列判断，实现与仍待完成的硬件证据以本页顶部快照为准。

| 边界 | 方案起点的源码与缺口 | 对后续接线的要求 |
|---|---|---|
| 邮箱容量 | `TDMA_PROCESS_IMAGE_VDC_SIZE`；`refmem_sync_vdc_command_payload_t` | 现有诊断区不能直接表达完整命令；必须明确编码和完整性，不能补默认代际或接收时间。 |
| 运输到命令区 | `distributed_refmem_tdma_flight_parse_mailbox()` 与 `distributed_refmem_get_vdc_follower_command()` | resident parser 只更新诊断，getter 读取独立命令区；需要显式、受保护的交接。 |
| 跨核读取 | getter 裸复制，`refmem_sync_vdc_receive_frame()` 和 context reset 写同一区域 | 发布、读取、清空均要遵守一致性协议；Core1 读取失败有界返回并保留可信输出。 |
| 时间域 | manager 将 `effective_vdc_time_ns` 与本地 uptime 比较 | 必须先建立共同时间映射，再比较到期时间；仅调用有效的默认 identity model 不会对齐不同启动时间。 |
| 序列 | manager 使用普通大小比较，RefMem/Domain 使用模差 | 冻结无效序号、半区间界限、回绕、重复和新 session 的处理；来源 generation 与本地 role generation 分开。 |
| 逻辑时间锚 | adapter 的 `sequence * cycle_period + reference_tx_phase` | 它可作为关联标签；尚不能当作与物理时间等速的绝对纳秒时钟。相位回绕反例必须纳入时间映射设计。 |
| 主机输出生效 | MASTER servo 先改本地 DCO，RefMem 随后发布其快照 | 若命令承诺共同生效时间，主机也要按同一未来时间提交对应输出；发送“已应用快照”不构成同步提交。 |

以下容量数字均为本方案/host 探针快照，非事实源：现有 VDC 区为 6 B，完整旧命令
payload 为 52 B，连同旧 RefMem header 为 88 B。旧版本的 CRC/单目标校验不能通过
伪造一个本地 frame 来证明新 wire 编码已通过跨域审核。

## 2. 时间模型先分清两个映射

候选模型将物理计时和信号控制分开：

```text
本板硬件时间 L_i
  -> M_i：经有向路径校准的 local-to-common 映射
  -> 共同时间 T
  -> S_master：指定主机发布、定时提交的信号模型
  -> 本板信号输出 S_master(M_i(L_i))
```

`M_i` 的建立者是 VDC owner，输入是 TDMA 的成对 hardware latch 和 Calibration 的
有向 delay/bias。它不改写 raw tick，不运行 follower 本地信号 PI，也不提升 formal
quality。`S_master` 的 phase/rate 由配置主机独占；Core1 应用时须进行时间坐标转换，
不能假定不同板的 local tick 比例及 epoch 相同而直接复制 rate/phase 数字。

共同时间轴候选以 TDMA reference 的硬件时基定义，由 session 绑定 reference 身份、
时基版本和 schedule。任意槽位的 MASTER 均在这个共同时间域发布信号命令，TDMA
reference 与 DPLL MASTER 不强制是同一角色。

共同时间的初始化与保持须具备以下过程，具体字段和界限在审核时冻结：

1. STOP/ARM 的 session fence 建立共同会话身份；禁止用各板独立递增的本地 run counter
   假定共同 run 相同。共同 session 的分配、分发/确认及重启唯一性必须有独立证据。
2. 通过既有 mailbox 提前发布未来 reference sequence 的 anchor reservation。各板完整
   验收请求后，由 TDMA owner 在对应硬件事件保留 latch；不依赖 Core0 恰好及时解析。
3. reference 通过同一路径发布该 sequence 的完整时间锚；其他板将自己保留的 RX latch
   与该锚及 directed delay/bias 配对。丢失、错圈、过期、错误 reference/session 均拒绝。
4. 经多锚点确认 epoch、频率比例和不确定度后才发布有效 `M_i`；漂移、有效期、溢出及
   连续性必须有边界。compact phase trailer 须先在自主模式补齐生产和关联，才能服务
   逐圈观测；它不能独自恢复 epoch。
5. 映射失效时冻结可信信号输出并降低质量；新映射与命令在 Core1 边界组合，避免因更新
   映射而产生未经约束的相位跳变。重新 ARM 或切换会话不能复用旧锚点。

上述时间模型尚未实现或验收；`COMMON_TIME`、`clock.valid`、主机内部 `LOCKED`
或精确匹配的序列，单独都不足以建立这个映射。

### 自主 origin 的原始计时前置

当前自主路径尚未提供上述模型需要的时间输入。事实源为
`tdma_pio_spi_ring_origin.inc` 的 `tdma_pio_spi_ring_origin_invalidate_time()`、
`tdma_pio_spi_phys_origin.inc` 的 `tdma_pio_spi_phys_origin_rx()`，以及
`tdma_origin_plan.c` 的 `L_STAGE`：交接时清理旧 clock observation，RX 返回零边沿
时间戳，DMA 每圈清零 DPLL trailer。`tdma_origin_record_t` 的 raw 格式新增 Timer1
与 TX latch 原始字段；
`tdma_origin_observation_t` 仍只描述运输事实。新增 raw 记录没有改变 RX 时间有效性或
DPLL trailer，不能直接提升为绝对时间锚。普通 origin 的鉴相更新不证明自主路径已经
具备这些能力。

下一候选切片按以下边界设计和审核，未完成前不改变 timestamp eligibility：

- TDMA owner 明确发射参考边沿与接收边沿的硬件事件、同圈身份、原始时基和不确定度。
  若用 DMA 读系统计时器，必须给出该读取与真实边沿的偏移/抖动证据及跨字回绕处理；
  “由 DMA 执行”本身不能证明读数就是边沿 latch。
- 先证明本地记录的准备、捕获、发布、退休、缺失及 STOP 取消；保留 source/session/
  schedule 绑定和 sequence 回绕。时间锚的保留不能阻塞下一圈，迟到时明确丢弃。
- 在既有 PIO/DMA owner 和静态容量内核算程序、描述符、FIFO 与 RAM；逐圈 trailer
  与原始边沿关联必须有可执行时序/负测及实板证据。若需要改变跨域格式或语义，先
  完成对应冻结与独立审核；不能借用 NO5 的 PIO/DMA 或增加独立同步帧。
- `VdcSyncAO` 只消费已验证且同圈的描述符；`SyncDpllFB` 仍独占 PI/DCO 应用。
  本地输入闭合后再测自主更新 WCET，随后由既有 mailbox 运输完整时间锚并建立 `M_i`。

#### 当前候选：预留 latch 与 Timer1 的区间关联

`tdma_pio_spi_phys_origin_configure_sms()` 的原型已配置预留 TX latch；其程序仍由
`TDMA_ORIGIN_LATCH_PC` 对应 catalog 保留，并由自主 DMA 图负责重装/使能。原型复用
该程序捕获发车 CS 边沿的首个倒计数，原有 RTT 保留为回传事件的独立原始观测。
TX CS、返回 CS 和 DATA 注入
是不同事件；将它们关联成正式边沿时间仍需验证各自延迟，不能把 RTT 加到估算 TX
时间后直接宣称 RX hardware latch。

在每圈准备时清理旧 FIFO、重装计数器，并在使能 latch 前后分别读取 Timer1 的
`TIMER_TIMERAWH_OFFSET` / `TIMER_TIMERAWL_OFFSET`，以 high/low/high 复验跨字
一致性。DMA 只存原始字；不一致、迟到或缺边沿作为无效记录保存，不能在发车路径
重试到成功。使用 raw 寄存器避免依赖共享 `TIMELR/TIMEHR` 锁存副作用；时钟频率、
初始化代际和本地 capture epoch 必须随记录上下文绑定，本地 epoch 不等于分布式 session。

两个一致的 timer 样本只给出 SM 使能事件的候选区间，还必须证明 DMA/MMIO 顺序、
SM 启动和 GPIO 同步延迟。PIO 高电平倒计数提供相对时间，将这些不确定度一起传播
为边沿区间；不能选择中点后丢弃区间宽度。DMA 向 control FIFO 写发车字时 PIO
可能仍在 guard，因此写入发车字的时间也不能替代实际 CS 边沿。

局部记录先保留原始样本、首个 FIFO 字和缺失标记，再绑定 boundary 的 sequence /
identity / epoch。记录发布必须在生产完成后进行，既有 observation/bank guard、
首尾 sequence、覆盖检查和 STOP 退休要随布局扩展一并复核；候选仅为本地诊断，
不承担共同时间初始化或修改 wire trailer。执行切片见 TODO 的 `VDC-TIME-001` 至
`VDC-TIME-004`，真实 builder 核算及离线模型见 `VDC-PROGRESS-20260914-008`，
集成原型、目标链接和板端预采见 `VDC-PROGRESS-20260914-009`；配置矩阵与 raw
生命周期补测见 `VDC-PROGRESS-20260914-010`。

资源准入以 `TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY` /
`TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY` 为边界，不能使用更大的通用构造上限代替实际
分配。集成原型在独占 sniffer lease 内去掉重复禁用写，并保持各运算显式重设
mode/seed、仅选中 executor 的 SNIFF transfer 参与计算；FAULT/资源释放仍关闭 sniffer。
run 分配保持原边界，literal 分配按对应符号调整。新增归档、producer、冻结副本和
对齐均需计入预算，不能从总 BSS 不变推断没有消耗 RAM。当前容量目标链接和节点
构造矩阵已有证据；扩展矩阵覆盖全部 active mask/有效 local slot 与选定 guard/abort
组合。后续 owner 准入 tail/prefix 探针及当前/上限容量链接见
`VDC-PROGRESS-20260914-011`，使用固定的 owner PIO/DMA 和合成地址；
`VDC-PROGRESS-20260914-013` 补齐其余编译容量的 A/B 链接，并使用各目标实际地址
重跑构造矩阵。上限容量只实测了现有四板配置，quick P3 的 RefMem 调度失败仍保留。
raw 复制交错、计数回绕与 STOP 后退休已有补测，硬件配置及有界取消证据仍由
`VDC-TIME-002` 跟踪，不能从 host 构造或目标链接推断物理边沿已经验收。

切换缺失的只读审计显示，旧环路停止之后才选择自主 persona、构建并安装 DMA 图；
普通服务与自主图共享 workspace union，不能直接在旧 DMA 运行时覆盖构建。后续
连续性方案须在安全启动边界完成准备，或证明独立 owner 存储与有界交接，保留真实
missing/迟到记录。当前快照只定位到切换区间，不提供精确停发时长；严格 SCK 校准
失败与边沿误差界也须独立闭合，不能通过裁短采集窗口或调整锁相判据绕过。

原始计时格式由 `TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME` 与 `tdma_origin_raw_time_t`
描述；它追加 timer high/low/high、首个 latch 字、FIFO 状态、arm 前 GPIO 输出和
tick rate，不赋予 COMMON_TIME 或 formal flag。缺 latch 的分支必须有界返回，运输
通过与计时有效分开；STOP 导出不能重新给旧样本打时间标签。实测读取区间只证明该次
原始读取，实际 GPIO 边沿、SM 启动、MMIO 顺序和跨域时间映射仍须独立验收。
本候选不授予 timestamp eligibility，也不冻结新的时间契约。

## 3. 固定邮箱的候选承载方式

首选验证完整记录跨多次 mailbox 更新运输。保留全局时间戳 trailer、RefMem、ACK 和
control 的静态分区；仅在已协商的新 mailbox 版本中把 VDC 区解释为记录片段。无新记录
时可重发当前片段；片段推进由 Core0 准备，PIO/DMA 的发车条件不依赖命令是否齐全。

候选片段为“kind/index + group hint + data”，完整命令在 Core0 校验后一次发布。
group hint 只用于组装，不能代替完整 session/record sequence；按来源分离组装状态，
并验证 mailbox 所声明 source 与其物理 segment slot 一致。运输序列、片段序号和主机
命令序列分别计数，不能复用 RefMem 字段的业务 generation 充当命令身份。

候选记录布局快照如下，非事实源、未冻结；所有多字节字段采用显式字节读写，禁止把
wire buffer 强转为带自然对齐的 C struct：

| 候选偏移 | 字段 | 长度 | 用途 |
|---:|---|---:|---|
| 0 | version / kind / source / target mask | 4 B | 区分 session、anchor reservation、anchor commit 和 signal command；完整记录再次绑定来源与目标。 |
| 4 | session identity | 8 B | 分布式共同会话，生成和 fence 协议待审；不是两块板碰巧相同的本地计数器。 |
| 12 | source control generation | 4 B | 发布主机的控制流代际。 |
| 16 | record/command sequence | 4 B | 同来源同 session 内排序；零值、回绕和最大跨度显式约束。 |
| 20 | schedule CRC | 4 B | 绑定冻结调度、拓扑及相关 operating profile。 |
| 24 | reference sequence | 4 B | anchor reservation/commit 或命令所依据锚点的关联身份。 |
| 28 | full reference anchor time | 8 B | 完整共同时间锚，避免只传取模 phase。 |
| 36 | effective common time | 8 B | 命令共同生效时间；非命令类型字段规则另行冻结。 |
| 44 | signal rate | 4 B | 在共同时间域定义的有符号频率修正。 |
| 48 | signal phase | 4 B | 在生效时间处定义的有符号相位修正。 |
| 52 | lock / health / tier / flags | 4 B | 受验证的源状态，不授予 follower 本地 formal 权限。 |
| 56 | maximum lateness | 4 B | 必须同时受接收端已冻结 profile 上限约束，不能由发送者放宽本地门禁。 |
| 60 | record CRC | 4 B | 覆盖前述完整记录，包括身份与时间；算法、覆盖范围和版本待独立审核。 |

该候选为 64 B；在 6 B VDC 区中给控制标签留 2 B、每片传 4 B 时，需要 16 次成功
更新。若使用旧完整 frame 则需要 22 次。这里的更新次数不是硬件环路圈数，也不能直接
乘 Core1 周期当作已经测得的到达上界。各类型记录允许的字段、保留值和 CRC 未冻结前，
该表不得进入产品 parser 或契约登记表。

不采纳的捷径：直接将现有 phase/rate/quality 当完整命令；抢占全局时间戳 trailer；
用 Core0 接收时间补生效时间；通过第二 wire 帧发送命令；从机缺命令时开启本地 PI。
快慢通道压缩可在完整记录闭环后评估，但必须保留完整身份与定时语义，不能用吞吐更高
替代错误来源、旧 session 和过期命令的拒绝条件。

## 4. 提前量、背压与提交规则

生效提前量至少覆盖 `记录片数 × 有界发布间隔 + 环路传播上界 + Core0 解析/重组上界
+ 跨核交接/输出准备上界 + guard`。各项要在当前 build/profile 下测量；目前没有这些
上界的完整证据，不能承诺候选吞吐或正式精度。anchor reservation 同样需要先发布完整
记录，再等待预定硬件事件；超过准备期限就取消，不能改为收到即执行。

Core0 合并尚未开始传输的候选更新；已经开始的记录保持不可变。丢片、乱序、冲突片段、
CRC 错、hint 回绕、超时及 STOP 取消均不得发布半条命令。接收端保留上一有效命令；
Core1 不阻塞等待片段，不在读重试中无限自旋。

MASTER 和 FOLLOWER 均在已选择的共同生效边界提交同一信号模型，并记录 planned /
prepared / applied 身份与实际时间。过期、映射无效或准备未完成时保持可信输出、记录
拒绝并降低质量；不能重写共同时间来“补执行”。源更新比传输快时的合并、限速和控制
带宽须进入闭环稳定性测试，分片运输成功不等于控制稳定。

## 5. RAM 与下一实现切片

前序 target link 余量见 `dpll-four-board-profile/layout-review.json` 及对应构建证据。
不能直接增加按节点展开的组装 buffer 或借用运行中的 DMA buffer。

本轮 native sizeof 和调用点审计得到以下数组占用快照，非已释放 RAM：

| 编译容量 | compact RX 未使用的 ACK/fence/remote-quality 数组 |
|---:|---:|
| 4 | 736 B |
| 5 | 920 B |
| 6 | 1104 B |

`VDC-RESOURCE-001` 已将 `s_tdma_flight_sync.context` 收敛为专用 compact DELTA
receiver，保留 peer、mirror 和 quality，通用 RefMem receiver 保持完整能力。当前
编译容量的实际回收和剩余链接余量见 `VDC-PROGRESS-20260914-005/006`。
`VDC-PROGRESS-20260914-011` 补充当前与上限容量的目标链接：上限容量曾出现真实
RAM 溢出，随后由 Sync Trigger owner 提供 `sync_trigger_status_t` /
`sync_trigger_get_status()`，使 UI 仅保存所消费的标量状态；完整 TriggerVector 和
序列表继续由原 owner 持有，Core1 栈不用于补足主 RAM。其余编译容量的独立目标
链接已由 `VDC-PROGRESS-20260914-013` 补齐；相应物理拓扑及运行配置仍须独立验收。
RefMem 旧向量更新改用 VDC owner 提供的受保护字段投影，目标栈帧和四板诊断对照见
`VDC-PROGRESS-20260914-014`。该副本收敛不改变旧向量布局，不分配命令缓冲，也不
代表整表栈峰值、所有调用路径或自主更新 WCET 已通过。
组装区、稳定命令、guard、预约锚点和双缓冲仍须分别完成
预算后再接线，已回收 RAM 不等于这些缓冲已经分配或准入。

## 6. 独立审核与负测清单

| 审核项 | 必须提供的证明 |
|---|---|
| session 与来源 | 同 segment/source/reference；错误目标、旧会话、重启、主机切换及多个来源互不覆盖；会话唯一性和 START/STOP fence。 |
| 完整性与排序 | 逐片 CRC/整条 CRC、缺片/重复/乱序/冲突、group hint alias、完整序列回绕与半区间拒绝；无半记录发布。 |
| 跨核生命周期 | 发布、reset、STOP、读交错及读重试上界；失败时最后可信输出保持；控制计数和诊断计数不混同。 |
| 时间初始化 | 异步启动、reference phase 回绕、预留 latch 退休、缺锚/错锚、频率偏差、时间转换溢出、旧映射失效。 |
| 定时执行 | 主机和从机共同提交；提前量不足、过期、限速/合并、不同静态周期及实际输出边沿对账。 |
| 实时与资源 | 特等席每圈时间戳通道、RefMem 准入和发车连续；目标 RAM/link map、WCET、当前源码 P3；不借用 NO5 或其他 owner 的硬件。 |
| 正式锁相 | 正式 directed delay/bias、timestamp qualification、质量窗口与真实输出测量；控制命令 ACK 和内部 LOCKED 不能代替。 |

审核结论当前为待审；作者不自行将本方案登记为已冻结契约。下一步先闭合资源、调度和
角色基础，补齐 session/时间模型/记录格式的可执行规格，再提交独立审核。
