# VDC 内部主域待办

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_TODO.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/sync/SYNC_IO_TODO.md`, `docs/sync/SYNC_IO_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TODO.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`
Last updated: 2026-09-15

本文只维护当前 VDC 架构迁移的任务、依赖和退出门禁。稳定语义见 Architecture，实施证据
见 Task Progress，重构前内容已归档到 `docs/legacy/vdc/`。

## 状态规则

任务状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`。

- `DONE`：代码/文档、必要构建和对应硬件/集成门禁均已闭合。
- `IN PROGRESS`：当前执行切片，或已有实现但证据未闭合；后者不代表同时开放新的实现。
- `PENDING`：依赖尚未完成，不得提前实现或验收。
- `BLOCKED`：有明确外部阻塞、失败证据和下一解除条件。

构建号、板端计数、replay 结果和 HIL 路径只进入 `VDC_TASK_PROGRESS.md`，不改变任务语义。

RAM 使能切片 `SYNC-RAM-001` 已闭合，当前已返回 `VDC-TIME-002` 的连续事件共同
epoch、有界交接及失效退休。成对事件观察器的生产接入由 TDMA owner 承接，事件快照
通过版本化双缓冲交给 Core0，由 Core0 完成原生记录；普通模式的诊断连续性不能替代自主模式的完整窗口、
物理首事件与身份绑定验收。观察 FIFO 已从 RX_PREPARE 交接中解耦，并完成当前源码
普通模式门禁及自主诊断对照；最终 enable 前的 CS 准入复验已完成 source/host 和
固定轮次自主诊断验收，保留启用后 DIRTY_START 拒绝及显式重臂要求。有界事件历史
基础已完成 source/host、目标资源与普通短帧验收；随后准入诊断已通过独立 oracle、
当前源码 P3 和 STOP 拒绝负测，单次自主采集获准并完成原生窗口。旧未知 grant 拒绝
和本轮自主 startup/普通 persona 门禁失败均保留，不以新通过追认旧失败。
真实 DMA 捕获凭据到 RX station 已通过 source/host、当前源码 P3 与原生连续性
对照，资源布局恢复后保持小幅净增；自主 startup/普通 persona 失败仍保留。
Core1 READY 边界的有界历史候选查询及原生记录已完成软件、目标资源和普通模式
匹配验证；逐 capture lease 绑定 observer/ARM 代际及 DMA 复制范围，STOP 取消
退休，历史候选不能作为当前 live lease。上一轮自主准入拒绝和部分窗口已保留；
随后 foundation 专用读取解除无关诊断快照的可用性依赖，完成软件、目标资源、
普通短帧及单次获准的自主原生完整窗口对照。实际准入谓词与 Core1 再验保持，
本次成功不能确定上一轮具体失败子读取，也不能追认旧失败。
首次 observer 实际启用的 DMA 完成坐标括号及 capture 失效退休已完成软件、目标资源、
011 阶段对应源码的严格 P3 和自主完整窗口对照；STOP 后一次导出仍只提供未决诊断区间。
后继只读证明确认：区间唯一化仍依赖未完成写入总上界、独立相位和共同 idle；当前
原件不足以关闭这些前提。优先细化训练后 STOP 冻结几何、ARM 前准备 observer/
capture、复用既有全板 ARM ACK 后 origin 首发的零起点方案。owner/反例审查已完成，
已加入 process follower 首次 capture 的 CS 低电平等待；软件模型、目标构建和普通
短帧门禁通过，实板首帧验收仍未完成。一次主机 START/STOP 实验因控制响应时延，
在原始快照定界前已丢失首帧；通用 SD 保存超时后原文件成功恢复读取，失败仍保留。
owner 有限发帧已完成软件、目标构建及当前四板 P3，重复 START 不补发；三从最早
raw 前缀完整保留。固定两帧分区已得到一致的合法 packet，header 位相与本轮 A/b
一致，证明偏移不是首帧丢失；当前 ARM 身份、连续 observer 事件和物理 CS/SCK 锚仍未
闭合，不能把这项诊断结果当首帧 DATA/CS/SCK 验收。继续补齐这些证据，再推进有代际的
冻结几何和 observer 预启动；不以主机等待时间或较晚的正确帧授予零起点。
下一步排除在途 DMA 写入、启用前积压并证明首物理边界，建立唯一 packet/event 坐标；
FIFO 空、两端计数相等或候选 sequence 相等均不能授予身份。011 阶段的严格 P3
通过不追认历史失败；014 当前严格 P3 通过，013 严格校准失败及两轮通用 SD 保存
截止失败仍保留，同 job 原文件已恢复读取。自主 startup、
普通计数/persona 和调度超限继续分别追踪。
自主总门禁、身份
和物理精度仍未闭合，`VDC-TIME-003/004` 不开放。证据见
`VDC-PROGRESS-20260915-001/003/004/005/006/007/008/009/010/011/012/013/014`；OTA 保持现状。

## 长期执行目标：符合 HAOFV 的可配置多节点 DPLL 锁相闭环

目标 ID：`VDC-LONGTERM-001`。

在不破坏 HAOFV owner 边界和 TDMA resident cycle 的前提下，将四节点环形系统实现为
可配置的 DPLL 控制集群。每块板保留相同的 PI、DCO、lock 和 quality 能力；运行时由
控制 profile 选择 `MASTER` 或 `FOLLOWER`。首个基线为一主三从，后续必须能够验证多主机
组合和主机切换，而不需要复制另一套固件或修改物理拓扑。闭环必须贯通命令运输、
共同时间映射、定时应用和实际输出测量，最终达到经过评审的相位精度、稳定性及恢复
门限，不能以状态位或命令计数代替实际输出锁相。

节点容量由 `PROJECT_NODE_CAPACITY` 限定；STOP 后选择准入节点数和 operating profile，
ARM 冻结 mailbox 布局及完整静态调度表，RUN 不改变配置。Core1 整表周期以
`PROJECT_CORE1_PROFILE_1500US_CYCLES`、`PROJECT_CORE1_PROFILE_5MS_CYCLES`、
`PROJECT_CORE1_PROFILE_10MS_CYCLES`、`PROJECT_CORE1_PROFILE_15MS_CYCLES` 和
`app_realtime_profile.c` 为事实源；TDMA wire 周期另由 operating profile 描述，必须
验证两者的关系，不能混用。后续长周期及长帧分别完成准入、资源和锁相验收，已有
整表周期配置能力不代表长帧已经验收。

### 控制不变量

- `MASTER` 以本地正式 TDMA evidence 驱动鉴相、PI、积分器和 DCO，并发布受保护的频率、
  相位、锁定/质量状态、源节点、序列和共同生效时间。
- `FOLLOWER` 保留 PI 实现但旁路本地鉴相、PI、积分和本地 promotion；只在 Core1 的
  确定性 service boundary 应用指定主机的已验证命令。
- 从机命令缺失、陈旧、来源不匹配、CRC/调度/时间不合法时，保持上一稳定 DCO 输出，
  不得隐式回退到本地 PI，也不得伪造 lock、quality 或 formal lock。
- 角色切换必须原子地递增 generation、清理旧积分和连续锁定历史，并保留可审计的
  requested/applied generation、拒绝计数和最后有效源命令。
- 角色默认值最终由 Flash control profile 提供；legacy 记录只能经过显式兼容迁移，
  不得因缺少角色字段而静默改变运行角色。

### 时延和数据方向不变量

- TDMA/Calibration 继续只负责测量有向物理链路时延、bias、generation 和 freshness；
  角色改造不得改变训练、校准或矩阵生成流程。
- DPLL 消费 TDMA 数据反向路径的时延。每条边必须保持明确的 source/destination，
  不能以时钟 marker 的正向路径代替数据路径，也不能在运行时沿环临时累加路径。
- 通信传输延迟只能通过 TDMA 所有节点可推导的共同绝对生效时间消除；接收时刻、
  远端 local tick 或最后到达的 mailbox 均不能直接作为从机执行时间。
- 在 source slot、command sequence、CRC 和共同生效时间语义冻结前，禁止把
  `vdc_domain_apply_follower_command()` 接入实时 TDMA 消费路径；该跨域契约需单独登记
  并完成交叉审核。

### 本地晶振驯服不变量

本地晶振驯服是独立的慢速 actuator 通道，只能调整底层 oscillator trim。它必须具备
能力报告、限幅、限速、最小更新间隔、stale/fault freeze 以及 requested/applied generation。
晶振驯服不得写 DDS phase accumulator、替换从机主机命令、改变 local-to-VDC 时间连续性，
也不得提升 DPLL lock、quality 或 formal promotion；没有硬件 actuator 时必须明确报告
unavailable 并冻结最近可信值。

### HAOFV owner 约束

Core0/SCPI 只负责配置、暂存、读回和显式 Flash store；Core1/SyncDpllFB 是角色、PI、
DCO 和从机命令的唯一实时应用者；TDMA 拥有 process image、时序、sequence、CRC 和
hardware latch；RefMem 只保存按 source slot 分离的稳定副本；Calibration 只拥有有向
时延/bias 测量；晶振 driver 只拥有 actuator；VDC Domain 只拥有控制状态和质量语义。
任何其他模块不得反写 DCO、积分器、lock 或 quality。

### 执行和完成定义

执行顺序由下方阶段表和任务依赖统一约束。`VDC-ROLE-002` 分解为 `VDC-CMD-001`
至 `VDC-CMD-005`，全部退出条件满足后才可关闭父任务。契约审计和方案准备可以提前
进行；固件接线、状态迁移和硬件验收必须等待相应前置 gate，不能以计划已写完代替准入。

每个实现切片按“相关软件测试 → 当前源码构建 → P3/TDMA 短帧闭环 → 原始证据复核
→ 代码与文档分离提交”完成后，再进入下一项。失败保留原件、原因和回退结果；
quick diagnostic、forced continue、旧 receipt、旧 build 或 NO5 外部观测不能替代
严格验收。契约冻结时另行更新域 Architecture、登记表及 C11 独立交叉审核；本 TODO
只定义执行任务，不冻结新的 wire 格式或修改登记表状态。

当前四板验证使用 SCPI 触发流程、板端 SRAM 记录、全部 RING STOP 后导出和写 SD；
首次 RING START 到最后一次 RING STOP 之间不发查询采样。共享 StorageAO 时先保存
TDMA 记录、释放冻结 lease，再保存 DPLL trace，并核对 CRC 和读回字节。长期分段
观测按独立任务验收，不能成为当前命令闭环的前置依赖。

长期目标只有在以下条件全部满足后才算完成：四节点 TDMA resident/process-image 连续；
一主三从及后续主从组合均能按配置运行；丢命令、陈旧命令、错误来源、角色切换和晶振
故障均可恢复且不启用隐式从机 PI；DCO snapshot、quality、formal gate 和 HOLDOVER/
RELOCKING/FAULT 状态可追溯；最终相位精度只由当前源码指纹下的实测正式门禁判定，不能
把任何预设精度承诺写成架构事实。

### 分阶段执行清单

下表是 `VDC-LONGTERM-001` 的执行导航；任务状态以对应任务表为准，不另建一套完成状态。

| 顺序 | 阶段 | 对应任务 | 阶段退出条件 |
|---:|---|---|---|
| 1 | 调度与角色基础 | `VDC-RESOURCE-001`、`VDC-SCHED-001`、`VDC-ROLE-001`，复核 `VDC-TDMA-001` 与 `VDC-EVID-001` 的 resident 输入 | 命令缓冲的 RAM 预算经目标链接复核；自主环路具有可关联的实际时间戳输入，真实更新路径具备静态预算证据；区分本相位超限和上游迟到；主从切换清理旧状态，从机本地 evidence 不驱动 PI/DCO，短帧连续。 |
| 2 | 冻结定时命令契约 | `VDC-CMD-001` | 来源、序列、代际、完整性、共同生效时间和过期策略在域文档落点，完成登记及独立交叉审核；固定 process image 与各业务预算可复核。 |
| 3 | 接通稳定命令运输 | `VDC-CMD-002`、`VDC-CMD-003` | Core0 按来源保留稳定命令，Core1 有界读取；从板接收计数增长；错误来源、损坏、重复、混合快照及旧会话被拒绝。 |
| 4 | 共同时间与定时应用 | `VDC-CMD-004`、`VDC-CMD-005`，关闭 `VDC-ROLE-002` | 本地与共同时间映射可追溯；三块从板实际应用命令，应用时间与目标时间可对账；丢失或迟到命令保持可信输出，不启用本地 PI。 |
| 5 | 四板锁相与优化 | `VDC-CAL-001`、`VDC-EVID-001`、`VDC-ROLE-003`、`VDC-SERVO-001/002`、`VDC-LOCK-001`、`VDC-SNAPSHOT-001` | 补齐正式 latch、directed delay/bias 和 freshness；基于真实命令更新优化主机闭环，分别证明内部状态、从机实际输出跟随和正式质量门禁。 |
| 6 | 恢复、配置扩展与长稳 | `VDC-HOLD-001`、`VDC-ROLE-004/005`、`VDC-CONFIG-001`、`VDC-RUN-001`、`VDC-VERIFY-001` | 主机切换、重启、丢命令、STOP/ARM、不同节点数和周期有正反证据；无旧命令重放、无健康 TDMA 节点隔离；长稳门限及正式 RUN 条件闭合。 |

### 当前执行入口

按本轮用户要求，先完成未提交增量对账及四板普通 TDMA 基线，再逐项恢复命令功能。
隔离点为 `DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED`；当前默认编译不包含
resident 命令构造、发片、RX 分派和私有组装状态，保留普通 compact mailbox、通用
命令校验及 follower 时间门禁。逐文件审计见 `VDC-PROGRESS-20260915-026`，非法目标
位移修复及完整隔离分别见 `VDC-PROGRESS-20260915-027/028`。这不关闭
`VDC-CMD-002` 至 `VDC-CMD-005`，也不表示
恢复旧 standalone 命令路径或已经完成从板应用。

每次只引入一项可归因的功能变化，立即完成相关 host 验证、当前源码构建和四板 P3，
复核原始结果后才进入下一项；不得把多个未验收修复累计到同一次 P3。P3 通过只证明
其覆盖的四板短帧流程，该增量另需自己的正向完成证据。若失败，记录源码差异、原始
拒绝和预期收益；低收益原型可局部撤回或隔离，回退本身也要重新 P3。时间身份、
CRC、跨核一致性及可信输出保持仍有价值，不能通过关闭它们来宣称命令应用成功。
后续沿既有依赖先补时间输入/角色及契约，再分别验证稳定副本、分片完整接收、共同
时间及定时应用；禁止单次重新打开整个命令功能后把剩余错误混在一起调试。
重新启用的前置核对包括：context reset 的唯一写者及 guard、payload 版本兼容决策、
实际发布间隔/FIFO 背压/重复片段下的交付上界。reset/读交错和任务写者边界的独立
修复见 `VDC-PROGRESS-20260915-030`；角色回切时已接收副本的退休补强见
`VDC-PROGRESS-20260915-031`。FIFO 发布的本地 admission epoch 取消补强见
`VDC-PROGRESS-20260915-032`；DMA/station 尚未发布的旧输入、之后完整重发的旧记录
和精确角色切换边界仍需协议规则。远端代际重启、Domain 任意时钟模型换会话后的
历史退休及完整命令 STOP 取消仍待闭合；FIFO 在用 view 与 reset 的互斥补强见
`VDC-PROGRESS-20260915-033`，不替代命令/session 退休。本地 ring 配置绑定与
STOP/ARM 取消补强见
`VDC-PROGRESS-20260915-034`；该绑定不建立共同 session，也不撤回已进入
TX image/FIFO/PIO/DMA 的旧片段。不能因禁用态 P3 或本地取消通过关闭全部前置；
每项修复继续独立完成软件验证、四板 P3 和对应功能正反证据。
继续增加功能前，先闭合 TDMA 涨时归因。`VDC-PROGRESS-20260915-035` 已固定
校准配置完成当前固件重复测量；`VDC-PROGRESS-20260915-036` 已补同 reset generation
的完整 profile，并将普通发帧与从板事件服务分段。`VDC-PROGRESS-20260915-037`
已完成 transport CRC 等价优化、当前四板 P3 和同配置/同槽位的短窗收益复测，
主板短窗未再记录 TDMA 超限，但从板、整表及长期预算仍待闭合。
`VDC-PROGRESS-20260915-038` 已完成事件 lift 等价快路径及独立四板 P3，
同槽复测未证明从板整体提速；保留算术简化和代码缩小，不关闭时序任务。
`VDC-PROGRESS-20260915-039` 已将 FEED/START_CUT/counter 热点组合放入 SRAM，
付出一档 BSS 对齐成本后，从板普通模式短窗超限显著减少，严格 P3 经同固件
校准重验通过，首轮校准拒绝和一次导出不可用均保留。`VDC-PROGRESS-20260915-040`
已闭合 STOP 后 owner 几何冻结和新 ARM 显式选择。新 ARM 合法初始 observation
epoch 的误拒绝修复已独立验收，见 `VDC-PROGRESS-20260915-041`。
所选几何观察器预启动及普通 origin 限发单帧专项已闭合，见
`VDC-PROGRESS-20260915-042`：ARM 返回前完成一次预启动，START 前按相位维护真实
DMA 计数；冻结偏移保持独立诊断假设，不恢复 live alignment，不重设 epoch。
新 ARM 的首条成对原始事件、重复 START 不补发及正常 STOP 退休已有四板证据；
单帧仍不关闭连续性、在途写入、启用前积压或物理身份门禁。
自主 origin 的非发射 READY、显式首发、到期/STOP 取消及原有自动启动兼容切片已
闭合，见 `VDC-PROGRESS-20260915-043`。准备态核验 DMA 下一次传输的 reload，
不能用未触发时的 live 剩余计数代替；独立 owner 终态在 STOP 清理前保留首次拒绝。
首个自主原始计时记录的有界留存切片已闭合，见 `VDC-PROGRESS-20260915-044`：
一次性 DMA 归档保留未经运输校验的首条，并保持原循环槽映射；Core0 将有效或明确
不可用的首档写入原生记录，新 ARM 撤销旧代许可。该留存只提供本地诊断身份。
继续 `VDC-TIME-002`，结合受控首发和 observer 预启动，闭合同一物理帧的唯一
packet/event 坐标与完整自主窗口，才开放 `VDC-TIME-003/004`。本轮几何选择关闭，
三从仍经历普通 bootstrap，不能由首档与后续正确帧的序列关系推导自主零起点；
不降低身份、回绕、故障退休和严格序列连续性要求。
当前四板先
闭合真实时间输入，未知拓扑不成为这一步的额外前置。完成时间输入/命令应用后，
以 NO1/CH1 上升沿触发的四路示波器采集与板端记录共同判断锁相，不以普通 TDMA
短窗通过替代自主时序、输出或正式锁相证据。
现有 P3 gate 未覆盖 TDMA phase WCET，不能据其通过跳过这项复核。
普通四板收敛中出现的 SCK 候选不足、TOPology 准备拒绝及三轮失败见
`VDC-PROGRESS-20260915-028`；针对后者的 STOP 后有界恢复及独立验收见
`VDC-PROGRESS-20260915-029`。配置成功仍需新代际 ACK，不能由超时后的状态猜测
成功。029 的当前源码四板 quick P3 已通过，普通基线收敛切片完成；暂态拒绝的
底层分支与校准稳定性继续归入 `VDC-VERIFY-001`，命令及锁相任务不据此关闭。

快速迭代的主机扫描加速切片已完成，结果见 `VDC-PROGRESS-20260914-012`。后续保持
默认 quick 验收，复用同一配置的构建目录完成增量编译，各轮证据写入新目录；源指纹、
OTA 和硬件判据继续核验。当前回到以下时间输入任务，验收加速不改变锁相依赖。

已有基线和未闭合结果见 `VDC-PROGRESS-20260914-001/002`；任务分解及源码审计索引见
`VDC-PROGRESS-20260914-003`。当前先推进 `VDC-CMD-001` 的只读审计、协议方案及负测
设计，明确 resident 诊断数据与控制命令的区别。原函数反例和资源审计见
`VDC-PROGRESS-20260914-004`，待审方案见 `VDC_COMMAND_TRANSPORT_PLAN.md`。
`VDC-RESOURCE-001` 的当前编译容量资源切片已闭合：compact RX 专用 DELTA 状态的
行为对照、目标链接及四板短帧/SD 已复核；有限采集的 STOP 后交接通过当前源码
quick P3 验证，见 `VDC-PROGRESS-20260914-005/006`。这不关闭其他容量的目标验收、
全表 WCET 或正式锁相，也不代表间歇性 TOPOLOGY 超时根因已解决。

`VDC-PROGRESS-20260914-007` 已确认前序 DPLL 更新对照使用普通 origin；自主 origin
的 DMA 计划清零 DPLL trailer、RX 不提供有效边沿时间戳，当前没有新增 DPLL trace。
因此入口先补齐 `VDC-TDMA-001` / `VDC-EVID-001` 的自主时间戳输入，再以该模式推进
`VDC-SCHED-001`。普通 origin 的 LOCKED 和无输入的低耗时均不能关闭此依赖。
按以下检查顺序推进，结果回写原任务表：

1. 按下方 `VDC-TIME-001` 至 `VDC-TIME-004` 补齐自主 origin 的实际事件、原始计时、
   记录生命周期及输入准入。事件/资源审计已有证据；`VDC-TIME-002` 的原始记录原型、
   当前容量目标链接和四板预采见 `VDC-PROGRESS-20260914-009`；配置矩阵及 raw
   停止/重臂补测见 `VDC-PROGRESS-20260914-010`；owner 准入几何及目标容量资源
   补测见 `VDC-PROGRESS-20260914-011`。当前与上限容量的链接证据分别保存，未测
   容量和硬件配置继续保留门禁。不能将 RTT 倒数或 CPU 提取时刻升级为边沿时间戳。
   候选边界见 `VDC_COMMAND_TRANSPORT_PLAN.md`。
2. 补齐逐圈 trailer 和本地 latch 的关联后，记录自主模式下实际更新的 prepare/
   servo/finalize/publish 成本，区分上游迟到和自身超限；先处理切换连续性，再验收
   稳定窗口。每个实现切片完成当前源码 P3/四板原件复核；无输入或普通 origin
   结果不能代替自主更新预算，稀疏 last-call 样本不能代替 WCET。
3. 调度门禁闭合后复核 `VDC-ROLE-001` 的主机 PI、从机旁路、角色切换清理与保持输出
   正反测试，完成对应短帧闭环；未退出前不开始命令接线。
4. 完成 `VDC-CMD-001` 的跨域登记与独立审核后，依次执行 `VDC-CMD-002/003` 的
   稳定交接和运输，再执行 `VDC-CMD-004/005` 的共同时间应用与一主三从对账。

`VDC-CMD-001` 必须回答：现有 mailbox 如何承载完整的命令身份和共同生效时间；
如何证明接收者本地时间可映射到该时间域；如何防止 Core0/Core1 混合快照、序列回绕、
主机代际切换及 STOP 后旧命令重放。在这些条件满足前，四板调参不得作为锁相验收。

### 自主时间输入的顺序切片

以下子任务细化 `VDC-TDMA-001` / `VDC-EVID-001` 的当前缺口，不替代两个父任务的
完整退出条件。只读审计、离线模型、板端原始记录和正式输入分别验收；模型中假定的
总线、GPIO 和 SM 启动延迟必须经实板测量或硬件边界证明，不能直接成为产品参数。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| `VDC-TIME-001` | 核对自主 DMA/PIO 发车与回传事件、原始时钟来源及当前资源。 | DONE | 已用当前真实 builder 核算编译容量/运行节点矩阵，核对预留 latch、Timer1 和完整记录边界；结果见 `VDC-PROGRESS-20260914-008`。仅关闭只读审计，不代表新增时间路径或实板验收。 |
| `VDC-TIME-002` | 完成 TDMA owner 内的原始计时记录原型及资源收敛。 | IN PROGRESS | 依赖 `VDC-TIME-001`。真实 builder 原型、当前容量目标链接和 raw 预采见 `VDC-PROGRESS-20260914-009`；全部 active mask/有效 local slot、选定 guard/abort/记录开关的 host 矩阵，以及 raw 复制交错、回绕、STOP/重臂和 persona 退休补测见 `VDC-PROGRESS-20260914-010`。owner 固定 PIO/DMA 下的 tail/prefix 几何和 UI 状态副本资源修复见 `VDC-PROGRESS-20260914-011`；其余编译容量的 A/B 链接、全部目标实际地址的构造矩阵和上限容量四板预采见 `VDC-PROGRESS-20260914-013`。`VDC-PROGRESS-20260915-015` 明确 scan backlog/clamp 与首帧交接计数仍需分层；目标链接缺口已补齐，对应物理节点拓扑、硬件配置和有界取消仍待验收；上限容量 quick P3 的 RefMem 调度失败保留。全部准入配置的描述符、literal、单步构造上界、记录布局、FIFO 和目标 RAM/link map 均通过后关闭；实际地址 host 构造不证明总线或边沿精度。跨字回绕、缺边沿、旧 FIFO、身份错配及覆盖分别验证，timer 读取区间不能作为实际边沿精度。 |
| `VDC-TIME-003` | 验收四板自主模式的原始计时与完整窗口连续性。 | PENDING | 依赖 `VDC-TIME-002`。原型预采见 `VDC-PROGRESS-20260914-009`，未开放正式验收。当前源码构建/P3及四板有限许可证采集，原始记录按 epoch/sequence/identity 对账；约束实际边沿到计时的偏移和抖动，保留并解决切换期间的 missing 和迟到，不以预选稳定窗口替代整窗。SCPI 仅控制，全部 STOP 后顺序保存 TDMA/DPLL 并核对 CRC/SD；区分板端原生记录与主机导出后写入 SD 的副本。健康 TDMA 不因局部计时拒绝而隔离。 |
| `VDC-TIME-004` | 将已验证的同圈计时接入 trailer 和 VDC evidence 准入。 | PENDING | 依赖 `VDC-TIME-003` 及相关契约门禁。同圈发射/回传事件可关联，旧 session/映射失效不能残留有效 flag；原始时间、共同时间及 formal qualification 分开。跨域语义变化须经独立审核；实板确实产生自主 DPLL 更新后才转入 `VDC-SCHED-001`，正式锁相继续由 Calibration/quality 和实际输出验收。 |

RefMem 向量更新的快照收敛和当前/上限容量四板复核见 `VDC-PROGRESS-20260914-014`；
本轮未再出现该相位自身超限，仍保留上游迟到和严格校准的失败边界。真实构造器每块
前后取消、最终发布及退休后复用的软件补证见 `VDC-PROGRESS-20260914-015`；不等于
芯片上的取消竞态已测量。固定 emitting 块上的实板暂停取消及新代际重臂已补证，见
`VDC-PROGRESS-20260914-016`；仅覆盖当前四板拓扑的 NO1 origin，其他物理配置与
微秒级交接时延上界继续由 `VDC-TIME-002` 跟踪，不能推广为任意指令边界竞态已验收。
两轮主动取消的连续性失败和启动屏障超时原件保留；普通 origin 恢复短帧已通过，
采集 STOP 提前取消末样本的失败及脚本修正对照均已记录。交接阶段计时与自主切换
全窗反证见 `VDC-PROGRESS-20260914-017`：已区分 Core1 调用体和跨周期体外时间，
但体外时间包含 Core0 构造，不能当作纯等待；软件 STOP/INSTALL 边界不等于物理边沿。
冻结邮箱有界合批、准入容量负测、当前拓扑局部耗时及完整窗口对照见
`VDC-PROGRESS-20260914-018`。合批缩短了跨周期准备，但 STOP/INSTALL 间隙仍在，
NO1 完整 TDMA 相位仍有超限，不能由局部检查低耗时关闭静态预算。
完整校准 CRC 的 SRAM 实现、当前源码 quick P3 恢复及普通/自主相位分离复核见
`VDC-PROGRESS-20260914-019`。保留的峰值未覆盖完整 CRC，不以零 calls 代表零成本；
需独立捕获成功准入，继续定位普通 origin 的 RefMem 发布与完整相位超限。
下一步压缩 STOP→INSTALL 的有界调度间隙，补齐边沿/SD 波形证据及其他物理配置；
继续验证 CRC/grant 拒绝、DMA 退休与 STOP 生命周期，保留初始化/校准超时和
transport missing 失败原件。同源码故障恢复可复用已验证 OTA 的正式 resume 入口，
恢复耗时与包含 build/OTA 的全流程耗时分开记录。
就绪阶段有界合批、真实构造取消后重臂及交接对照见 `VDC-PROGRESS-20260914-020`。
当前拓扑两轮 missing 未增长，仍需独立准备/准入相位和物理发车证据；不能用该结果
关闭完整窗口。现有检查器对普通 origin persona/软件 TX 计数的假设需按已授权自主
模式的硬件事实补齐，保留原失败并验证停滞负例，不通过删除检查或放宽任意 persona
使门禁变绿。普通 origin 的 RefMem 发布及全表 WCET 继续独立跟踪。
原生记录逐槽离线诊断及当前四板复核见 `VDC-PROGRESS-20260914-021`。工具已将
成功交接上下文中的自主软件 TX 平台与回传停滞区分，保留全部启动/中途失败；
不改变既有 TRN03/P3 门禁。新旧原始窗口均检出从板 RX ring overrun，不能以
process-image missing 未增长代替无覆盖证明。下一步定位从板 RX 消费积压和
启动拒收，并继续补齐逐样本 grant、逐圈 raw 身份与实际边沿证据；当前 schema
及 STOP 后有限尾部记录不满足这些完整窗口条件，不能据此接通正式时间输入。
RX 接受结果后同相位再捕获的试验与回退见 `VDC-PROGRESS-20260914-022`。
该合并增加接收速率，同时引入从板完整 TDMA 相位超限，当前实现不予采纳。
RX/TX latch 直接初始化倒数寄存器的切片已闭合，见 `VDC-PROGRESS-20260914-023`。
该变更消除重装中的阻塞 FIFO 传送，不增加 resident PIO 指令或 RAM；两轮分项波动
尚不能证明稳定时延收益，RX ring 覆盖和普通 origin 超限仍保留。下一步先定位
DMA 初次观察与复制后复验的成本、跨 service 的消费间隔及可恢复 APPLY 拒绝；
如再次尝试合并，
须由 owner 对整相位剩余预算和后续工作保留量做准入，保留 worker 退休、数据身份、
DMA 复制后复验和 STOP 取消。诊断计时关闭时返回零的时钟不能驱动运行时预算策略。
RX 消费能力与完整原生窗口的离线对账见 `VDC-PROGRESS-20260914-024`。单站台的
捕获/接受分属不同 service，形成与局部 WCET 不同的吞吐限制；无相位超限仍可发生
观察覆盖。下一切片先在已有资源预算内补齐 station 等待、初次观察间隔的有界最大值
及 clamp/epoch/复制复验拒绝原因，再决定有界捕获节奏和载荷合并策略；时间戳快速
通道不得依赖整帧解析。保留原有数据准入、复制复验和 STOP/重臂边界，不能把
observation drop 总数当作丢帧数或单一原因，也不能仅靠缩短重装来关闭吞吐门禁。
`TDMA_CROSS_REVIEW_06.md` 的逐字段复核见 `VDC-PROGRESS-20260914-025`。采纳其
关联、时间输入、调度与生产准入分别验证的检查清单，但所引窗口不支持“origin 的
bitmap/reject 持续增长”或“自主硬件时间戳前提已闭合”。保持当前 RX/原始计时入口，
不按未经同圈身份与普通/自主模式区分的快照直接实施关联修复或关闭诊断许可。
`VDC-TIME-002` 继续 IN PROGRESS，后续时间输入门禁不提前开放。
RX 站台与 DMA 初次观察的实板诊断见 `VDC-PROGRESS-20260914-026`。两轮从板汇总的
drop 全部来自窗口 clamp，REQUESTED/BUILDING 等待轮询占比很低；完整相位无新增
超限仍伴随 RX ring 覆盖，不能只靠压缩 Core0 构造或 latch 重装关闭该缺口。
下一切片优先评估最新完整帧选择及旧观察退休，保持新数据准入、身份一致性、DMA
复制后复验、hint 失效和 STOP/重臂；时间戳 raw 快速通道独立验证，不依赖整帧解析。
STOP 后 SRAM 汇总与原生 SD 全窗记录的覆盖范围不同，分别引用，不把裁剪 words
当作丢帧数。当前诊断的资源/P3 切片已通过，完整自主计时及全表 WCET 仍未关闭。
最新帧策略的前置身份复核见 `VDC-PROGRESS-20260914-027`：真实 TX helper 会把
调用者 expected 身份赋给没有帧身份的 latch，当前 clamp 也可能使所选帧前跳。
因此先实现并验证硬件事件侧产生的 epoch/事件序号或 DMA 位置与 raw 计时联合记录，
再由 owner 独立关联所选帧；不能用 CRC 正确、BOUND 标志或 earliest 选择代替证明。
最新镜像退休必须等待特等边沿保全及可靠命令/ACK 的独立消费边界，且不得破坏
bootstrap 对齐或重同步。下一切片以真实旧 FIFO/新 packet、丢边沿、覆盖、STOP
与重臂负测为门禁，保留 TDMA 连续运行，不能仅通过清标志关闭当前任务。
从板连续事件观察原型见 `VDC-PROGRESS-20260914-028`：实际控制程序与共用连续
计数、wire sequence 程序可在 TX PIO 内完成静态布局，未进入生产 persona。
共同 epoch、有界 harvest 及无需 wrap 真值的时间提升原型见
`VDC-PROGRESS-20260915-002`。新候选由同一 SM 成对输出 raw 与事件倒计数，补上
旧三路按下标联合在双路 raw 缺失时的错配；PIO 实际布局及离线负测已有证据。
FIFO 等长无 stall 不能证明所保留物理帧相同；sequence-only 不等于完整 identity
绑定。下一步核验当前发车路径的序号唯一性、完整身份候选、同启动/CS 高宽及真实
DATA/前缀准入，并核算成对记录容量下的最坏服务间隔，再接目标 loader/STOP 和
四板原始计时。模型 Y 计数不替代物理事件保全证明。
原型模型的回绕/截断通过不代替真实 STOP 退休、加载器、目标 RAM、P3 或四板精度
验收。一次性 raw+sequence 捕获不能替代该逐圈保全路径，正式时间输入仍不开放。
已通过的目标链接不重复冷构建。日常切片继续
默认 quick 验收，复用匹配配置的可写增量构建目录，容量矩阵补测独立留证；封存目录
仅供读取。快速流程耗时与冷构建边界见 `VDC-PROGRESS-20260914-012/013/014`。
保持 Core1 栈和其他 owner 的资源边界。UI 只读状态副本
回收的 RAM 尚未分配给命令组装、时间锚或 guard，新增缓冲仍须逐项核算。随后处理
`VDC-TIME-003` 的切换连续性及物理边沿误差界。切换审计已
定位到旧 DMA 停止后才准备自主 persona/共享 workspace 的边界，仍须测量实际停发
区间并验证安全准备/有界交接；不以屏蔽 missing 计数解决停发。当前 quick P3 的严格
SCK 校准失败已有本轮成功对照，见 `VDC-PROGRESS-20260914-010/011`；间歇失败
根因和正式计时配置仍需独立闭合。
首次原型预采和当前容量资源通过不关闭这两个任务；缺失 FIFO 只拒绝该 raw 时间输入，
运输校验状态仍独立判断。不能只利用
当前实接节点较少而忽略已准入的更大节点配置，也不能关闭记录以掩盖新增计时的资源成本。

## P0A 最高优先级：可配置 DPLL 控制角色

前置执行条件由 `VDC-SCHED-001` 验证；这项调度修复不提升角色、命令运输或正式锁定
任务的状态。现有角色/Flash/SCPI 和从机消费代码的存在不等于对应退出门禁已通过，
须按当前源码与实板证据继续复核。

| ID | 任务 | 状态 | 依赖 | 完成或退出门禁 |
|---|---|---|---|---|
| `VDC-RESOURCE-001` | 收敛 compact RX 专用状态，回收此实例中未使用的通用 RefMem ACK/fence/remote-quality 数组，为命令组装和时间锚提供预算。 | DONE | 现有 compact DELTA wire/peer/mirror/quality 行为；审计与验收见 `VDC-PROGRESS-20260914-004/005/006` | 当前编译容量的原/新行为对照、目标 link map、当前源码四板 quick P3/短帧及 STOP 后原始记录闭合，确认实际 RAM 回收；通用 receiver 保持完整能力。其他容量目前只有 host 对照，目标配置验收由 `VDC-CONFIG-001` 跟踪；不以资源回收关闭调度或正式锁相门禁。 |
| `VDC-SCHED-001` | 给 DPLL 静态相位保留入口余量，消除窗口恰好等于 WCET 引起的调度饥饿。 | IN PROGRESS | `TDMA-DET-01` 完整静态表；`VDC-TDMA-001` / `VDC-EVID-001` 自主输入 | 全部周期目录闭合；保留原 WCET 与准入检查；当前源码 P3、短帧闭环和四板记录对照 start miss/执行/超限，并证明采集窗口确实运行自主模式且有 DPLL 更新；SCPI 仅控制，STOP 后保存 SD；不以调度通过代替命令准入或 formal lock。 |

入口余量的实现和有限对照已记录于 `VDC-PROGRESS-20260914-001`；四板锁相复测与
owner 快照复制优化见 `VDC-PROGRESS-20260914-002`。继续针对真实更新路径分解
prepare/servo/finalize/publish 成本，区分本相位超限与上游继承迟到，并完成严格
校准/控制配置复核。无更新路径的执行恢复、主板的内部 LOCKED 状态以及 resident
邮箱诊断字段更新均不能关闭本任务，也不能替代从板实际命令应用和正式锁相证据。

角色与命令闭环优先于新的长期观测能力和自动调参；晶振驯服在阶段表指定的扩展阶段
验收。目标是让每块板保留 PI 能力，但可独立配置为 `MASTER` 或跟随一个显式指定的
`FOLLOWER` source；该目标不改变 TDMA/Calibration 的训练与测量职责。

| ID | 任务 | 状态 | 依赖 | 完成或退出门禁 |
|---|---|---|---|---|
| `VDC-ROLE-001` | 在 VDC Domain 建立 control profile、generation 和角色切换边界：主机维持 local evidence 到 PI/DCO 的既有路径；从机旁路 local PI、清理旧积分/连续锁定状态，且无 peer command 时保持上一稳定输出。 | IN PROGRESS | `VDC-EVID-001` 当前可观测接口 | host C 覆盖主机 PI 不退化、从机 local evidence 不改 DCO/积分、角色切换清理旧状态、非法 profile 拒绝；不会改变 TDMA/Calibration 训练路径。 |
| `VDC-ROLE-002` | 通过 `VDC-CMD-001` 至 `VDC-CMD-005` 闭合 resident 命令、按来源稳定副本和定时 follower apply。 | PENDING | `VDC-SCHED-001`, `VDC-ROLE-001`, `VDC-CMD-005` | 契约审核、运输、跨核一致性、共同时间映射及四板应用证据全部闭合；从机绝不回退 local PI。 |
| `VDC-ROLE-003` | 完成 Flash control profile、manager Core0/Core1 staging mailbox 和 SCPI `ROLE` 配置/读回/显式存储。 | PENDING | `VDC-ROLE-001`, `VDC-ROLE-002` | legacy record 安全取得默认角色；任一节点可选主机或从机并显式选择 source；requested/applied generation、当前角色和拒绝计数可读，Flash 只由显式 store 写入。 |
| `VDC-ROLE-004` | 建立独立的本地晶振驯服通道：capability/trim profile、慢速限幅 actuator、clock-model 连续性、stale/fault freeze 和 snapshot/SCPI 可观测性。 | PENDING | `VDC-ROLE-001`, `VDC-ROLE-002` | host C 覆盖 master/follower trim、无 actuator、限幅、stale/fault freeze 和 clock-model 连续性；follower DDS phase/rate 只随指定主机 command，trim 不提升 lock/quality。 |
| `VDC-ROLE-005` | 执行角色与晶振驯服矩阵验证：单主机跟随基线、多个主机与从机组合、主机 source 切换、命令丢失/陈旧/错误来源、trim actuator fault、角色切换和回退。 | PENDING | `VDC-ROLE-003`, `VDC-ROLE-004`, `VDC-HOLD-001` | host/replay、当前源码 build 和 P3/HIL 证据分别闭合；从机 output 只随配置主机，trim 保持共同时间连续，TDMA 连续性不受角色实验影响，精度结论只依据实测正式门禁；结果输入 `VDC-VERIFY-001` 总验收。 |

`VDC-ROLE-001` 是 `VDC-SERVO-001/002` 的运行架构前置：在没有明确角色边界前，禁止
继续以四节点同时 PI 的结果作为 PID 参数稳定性结论。`VDC-OBS-005` 的调参记录也必须
记录 role profile、follow source 和 control generation。

### 定时命令子任务与配置验收

| ID | 任务 | 状态 | 依赖 | 完成或退出门禁 |
|---|---|---|---|---|
| `VDC-CMD-001` | 审计并冻结 resident 定时命令契约；明确 mailbox 编码、来源/目标、序列、代际、CRC、共同时间和过期行为。 | IN PROGRESS | 审计可先行；冻结与接线前闭合 `VDC-SCHED-001`、`VDC-ROLE-001` | 明确 source generation 与本地 role generation 的区别，绑定会话/拓扑/调度身份；若需跨圈组装，冻结片段关联、完整性、取消和超时规则；区分命令更新序列与运输序列；时间映射的建立者和准入证据明确；wire/RAM/WCET 预算、负测方案、Architecture 落点、登记及 C11 审核齐全，不增加独立同步帧。 |
| `VDC-CMD-002` | 建立 RefMem 命令区的跨核一致性交接及生命周期清理。 | PENDING | `VDC-CMD-001` | 既有禁用原型的任务写者、在线 reset/读交错、已接收命令角色退休、FIFO admission 取消、借用/reset 互斥及本地 ring 配置取消见 `VDC-PROGRESS-20260915-030/031/032/033/034`，不关闭本任务；上游旧输入、已入站旧 TX、完整重发及命令/session 的完整 STOP 退休仍待闭合。完整门禁：单写者和按 source slot 稳定副本明确；seqlock/双缓冲/等价 guard 覆盖发布、读取、reset；Core1 有界读取，忙时保留可信输出；撕裂读取、generation 切换、STOP/reset 交错负测通过；新增静态 RAM 和代码放置经链接 map 核验。 |
| `VDC-CMD-003` | 将已审核命令编码接入固定 process image 的 Core0 准备及接收路径。 | PENDING | `VDC-CMD-002`, TDMA mailbox receiver | 准备/解析/必要重组在实时环路外；特等席时间戳通道和 mandatory-first 分配保留；未就绪时透传上一版，解析/CRC/复制成本有预算证据；按来源的接收计数实际增长；重复、乱序、回绕、CRC 损坏、错误来源/目标、旧会话及 STOP 取消均有负测；本阶段接收成功不代表允许定时应用。 |
| `VDC-CMD-004` | 建立代际绑定的共同时间映射、提前准备和 Core1 确定边界应用。 | PENDING | `VDC-CMD-003`，契约要求的时间锚和映射 evidence | 明确共同时间初始化，不以 clock.valid 或异步启动的本地 uptime 代替共同时间证明；校验 schedule/session/source generation 及映射 freshness；未来命令等待、过期命令拒绝、无映射时保持输出；记录目标时间、实际应用时间和偏差；不同启动时刻、延迟、时钟偏差、回绕、重启及 STOP/ARM 负测通过。 |
| `VDC-CMD-005` | 完成一主三从的命令接收、应用和回退闭环。 | PENDING | `VDC-CMD-004` | 当前源码四板 P3/短帧及板端记录中，三块从板接收与应用增量均非零，并按 source/sequence/generation/effective time 对账；确认实际 DCO 输出采用对应命令；缺失、错误及迟到保持可信输出且无本地 PI；SCPI 仅控制，STOP 后 SD 完整性核验；不据此单独宣布正式锁相。 |
| `VDC-CONFIG-001` | 验证 STOP 后节点数、mailbox 数量和完整静态调度周期配置，以及后续长帧配置。 | PENDING | `VDC-ROLE-005`, TDMA 对应配置准入 | 编译容量与运行节点数分离；每个准入配置的 map/trailer/DMA 长度、schedule CRC、时间映射、DPLL/触发时基一致；RUN 配置拒绝，重新 ARM 清除旧命令和旧映射；短帧、长帧及不同周期分别给出预算、P3/HIL 和锁相结果，未测配置不继承通过。 |

## P0B：长期观测与闭环证据扩展

长期观测基础设施服务于后续长稳、调参和拒绝定位，当前优先完成上述命令闭环。
现有 `IN PROGRESS` 记录表示已有实现仍待验收，不与当前执行入口竞争。四板短时验证
使用既有 SRAM/STOP 后 SD 路径；NO5 外部观测和分段流式存储另行按对应资源准入推进，
不能阻塞命令交接或替代 TDMA/Calibration/formal timestamp evidence。

当前只有四板，外部输出改由已接入示波器观测：CH1 至 CH4 对应 NO1 至 NO4，
用户指定 NO1/CH1 为触发源。`VDC-PROGRESS-20260915-035` 仅验证四路诊断输出和
同次停止帧 RAW 导出；后续需校验触发身份、完整共同时间轴、探头/通道延迟及
同窗板端应用记录，持续分析相位差、漂移、缺脉冲和恢复后再按评审门限判断锁相。
既有滚动采集工具仍需独立修正和验收，单帧、诊断 fallback 或内部 LOCKED 均不能
关闭正式锁相任务；无需为当前四板验证等待 NO5。

目标数据链路固定为：

```text
PIO/DMA EDGE_TIMESTAMP producer
  -> bounded Core1/SRAM producer
  -> Core0 bounded drain
  -> StorageAO segmented SD writer
  -> decoder/drop-interval/SVG analysis
  -> NO1-NO4 internal DPLL + external same-window correlation (scope now; NO5 separately)
  -> STOP/readback and next-run SCPI parameter tuning
  -> convergence/formal-lock decision
```

长期运行必须满足以下不变量：

- 采集、排空和 SD 写入均分段且可恢复；每段带 capture/segment sequence、硬件时间基、
  CRC、produced/consumed/dropped/overrun 计数和结束原因。
- SD 背压不得阻塞 TDMA、process-image、FIFO 或 DPLL 实时服务；不能假装连续，必须将
  drop interval、segment gap 和恢复点写入证据。
- `EDGE_TIMESTAMP` 只保存边沿上下文；实时 phase decoder 仍消费全部必要采样字，观测
  压缩不得改变 DPLL 输入或控制路径。
- NO1--NO4 内部 DPLL 与外部实际输出共同用于收敛判定；当前示波器和后续 NO5
  都只做外部观测，不参与控制。关联证据必须具备同窗、sequence/time anchor 和
  数据完整性标记，不能用内部状态替代物理相位证明。
- DPLL 失锁、残差振荡或调参反馈不能屏蔽节点；只要 TDMA 基础收发连续，节点继续参与
  环路。调试参数可通过 SCPI 小步试探、等待新样本、评分并回退。
- 快速验收默认不采 T0--T3 SD waveform；只有显式全量验收或异常诊断路径才启用原始
  波形采集。长期观测属于独立的分段观测任务，不改变快速验收默认值。

### P0 任务表

| ID | 任务 | 状态 | 依赖 | 完成或退出门禁 |
|---|---|---|---|---|
| `VDC-OBS-001` | 收敛 `SYNC-LA-003` `EDGE_TIMESTAMP` producer 与 Core0 bounded drain：明确 active/shadow ownership、sequence/timestamp wrap、re-arm/stop、overrun/drop accounting，并为 StorageAO 提供稳定批次接口。 | IN PROGRESS | `SYNC-LA-003`, `SYNC-LA-005` | host/C 测试覆盖 edge-only、wrap、重臂、停止和 buffer 不覆盖；真实 TDMA 短帧无扰动；生产者与消费者所有权可审计。 |
| `VDC-OBS-002` | 实现 StorageAO 分段 SD 流式写入与恢复：段头、连续性、CRC、落盘确认、背压、drop interval、segment gap 和断电/重启恢复。 | PENDING | `VDC-OBS-001` | 连续写入不会进入 Core1 实时路径；每次丢样都有原始计数和区间；可从最后完整段恢复并继续编号。 |
| `VDC-OBS-003` | 完成离线 decoder、缺口审计、NO1--NO4 收敛曲线和 SVG：图例必须绑定 node/channel/edge mask/timebase，缺失数据不得被插值伪装。 | PENDING | `VDC-OBS-002`, `SYNC-LA-006` | decoder 可重放所有完整段；输出曲线、缺口、dropped count、质量等级和输入指纹一致；坏段可定位且不影响其他段。 |
| `VDC-OBS-004` | 建立 NO1--NO4 内部 DPLL 与 NO5 外部观测的同窗关联：共同时间基、TDMA sequence anchor、capture generation、SD segment sequence 和外部线缆观测边界。 | PENDING | `VDC-OBS-002`, `VDC-OBS-003`, `SYNC-LA-008` | 关联结果能区分内部环路收敛、外部链路异常、SD 背压和观测缺口；NO5 不进入 DPLL 控制或 formal promotion。 |
| `VDC-OBS-005` | 将 SCPI 调参、串口闭环状态、residual/frequency/reject/lock feedback 与分段观测统一记录，支持小步搜索、等待稳定窗口、评分、回退和参数 generation 对账。 | PENDING | `VDC-OBS-003`, `VDC-SERVO-002`, `VDC-ROLE-003` | requested/applied generation、active profile CRC、role profile/follow source/control generation、原始命令、状态读回、raw debug gate/continuation count 和回退结果齐全；异常参数或可恢复 admission 在 debug profile 留证，不自动宣称 formal lock。 |
| `VDC-OBS-006` | 建立分级长期 soak 与发布验收：短时调试、工程长稳、发布级长稳均使用同一 segment/decoder/关联格式，并验证断电续采、SD 背压和 TDMA 无扰动。 | PENDING | `VDC-OBS-004`, `VDC-OBS-005`, `VDC-RUN-001` | 各级验收 profile 明确采样时长、允许/禁止的 drop、恢复点和退出条件；原始证据、失败事实和回退点完整，才可评估长期 `FORMAL_LOCKED`；结果输入 `VDC-VERIFY-001`，不反向依赖总验收关闭。 |

观测扩展不得跳过 `VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001` 的正式门禁；在正式
evidence 未闭环前，观测与调参结果只能标记为诊断或 tracking candidate。分段长期观测
完成后才可声明连续长稳能力；当前短时锁相验证可使用已验收的 SRAM/STOP 后 SD 路径，
仍须满足相应完整性、窗口长度和 formal gate。

## P0C：统一内部/外部观测算法

长期算法目标 ID：`VDC-OBS-ALG-001`。

统一 NO1--NO4 内部 DPLL 与 NO5 外部只读观测的物理语义和证据链。两类观测必须回答同一
问题：指定有向路径上，发送相位经过已验收的有向链路时延后，到达相位相对共同 TDMA
时间锚的残差是多少。MASTER 与 FOLLOWER 使用相同的采样、路径、延迟扣除、固定 bias
分离和 jitter 计算；FOLLOWER 只旁路 PI、积分器、DCO 和本地 lock promotion，不得因为
不调 PI 而减少自身观测点或改用命令应用记录冒充相位残差。

算法不变量：

- 每个样本必须带 `source_slot_id`、`reference_slot_id`、有向 delay/bias generation、
  evidence/sample sequence 和共同绝对生效时间；字段缺失、方向不匹配、序列断裂、CRC/
  schedule 不合法或跨 segment 缺口时，样本只能进入 raw diagnostic，不能进入 corrected
  jitter 或 formal lock 统计。
- 固定 path bias、真实相位 jitter、频率斜率、控制命令应用和观测缺口必须分开统计；
  接收时刻、本地重建时刻或零默认 delay 不得被当作共同时间锚。
- delay correction 是否可用、实际修正样本数和拒绝原因必须出现在 JSON/CSV/SVG 报告中；
  不能用字段名或默认值宣称“已扣除传输延迟”。
- NO5 只能与同一 `sample_seq`/共同时间窗口的内部证据做关联，不能驱动 DPLL、替代
  TDMA formal evidence 或提升 `LOCKED`/`FORMAL_LOCKED`。

实施阶段与退出门禁：

| 阶段 | 任务 | 状态 | 退出门禁 |
|---|---|---|---|
| A | 离线 residual 语义：显式校验 path/delay 元数据，区分 raw、corrected、bias、jitter 和 command-apply 记录。 | IN PROGRESS | 缺元数据/错误方向不再报告 delay 已扣除；固定 delay 改变不改变 corrected jitter；MASTER/FOLLOWER 计算路径一致。 |
| B | C 端观测审计：确认 `reference_tx_phase`、`local_rx_phase`、有向反向 delay 和共同绝对生效时间的物理方向与 owner。 | IN PROGRESS | 每个 evidence 可追溯 source/reference、sequence、delay generation、共同时间锚和 phase-domain flag；跨板 raw local phase 只能诊断，MASTER 必须 fail-closed，FOLLOWER 保留同语义 observation。 |
| C | NO1--NO4 与 NO5 同窗关联：按 sequence、capture generation、segment continuity 和窗口完整性关联。 | PENDING | 坏帧、丢样、跨 segment 缺口和外部线缆异常可分别定位；不完整窗口不得生成锁定结论。 |
| D | 长期验证：`1M3F`、`2M2F`、`3M1F`、主机切换、丢命令/陈旧/错误来源、方向错误和观测背压故障注入。 | PENDING | 当前源码指纹下的软件测试、构建、P3/HIL 和长期观测证据闭合后，才评估正式锁定。 |

当前切片完成阶段 A 的主机侧离线工具和回归测试，并完成阶段 B 的 logical TDMA
common-time anchor 第一段；它不证明任何板端节点已经锁相，也不改变
TDMA/Calibration 的训练与有向时延测量流程。

## HAOFV owner 边界

| Owner | VDC 任务边界 |
|---|---|
| STATE_MACHINE | PIO/SM/DMA/FIFO/GPIO/IRQ lifecycle、resource claim/release、quiesce 和 fault。 |
| TDMA Foundation | resident process image、UP/DOWN cycle、sequence/CRC、hardware latch 和 completion。 |
| Calibration | directed delay/bias、generation/freshness、observation path matrix。 |
| VdcSyncAO | profile/dictionary/calibration binding、evidence admission、同步动作。 |
| SyncDpllFB | phase/frequency servo、offset/rate/lock/DCO 唯一写入。 |
| VdcQualityGateFB/VdcVector | quality、coarse/formal promotion、guarded snapshot。 |
| RefMem/Trigger/core1 | 只读镜像、时间映射、FIRE_LOAD/RUN 消费。 |

## Canonical migration roadmap

下表保留既有任务 ID 和各域责任，执行阶段以上方长期目标清单为准。前置任务的退出
门禁未闭合时，后续实现保持 `PENDING`；已有实现的 `IN PROGRESS` 不表示允许越过 gate。

| 索引 | Task ID | 任务 | Owner | 状态 | 依赖 | 退出门禁 |
|---:|---|---|---|---|---|---|
| 1 | `VDC-TDMA-001` | 对接 STATE_MACHINE 的 resident lifecycle 和 TDMA Foundation 的固定 process image、UP/DOWN cycle、sequence/CRC、hardware latch。 | TDMA Foundation + STATE_MACHINE | IN PROGRESS | active TDMA profile | `RUNNING` cycle evidence 连续有效；资源/方向/persona 无冲突；VDC 不拥有 transport。 |
| 2 | `VDC-CAL-001` | 导入 active path delay、bias、generation/freshness 和完整 observation matrix。 | Calibration + VdcSyncAO | IN PROGRESS | `VDC-TDMA-001` profile/CRC | matrix 完整、table CRC/generation/freshness 通过；缺项 fail-closed。 |
| 3 | `VDC-EVID-001` | 将同圈 latch descriptor 展开为正式 DPLL evidence。 | VdcSyncAO + Timestamp service | IN PROGRESS | `VDC-CAL-001` | sequence/CRC/window/payload/dictionary/timestamp gate 全通过；diagnostic-only 不得 formal。 |
| 4 | `VDC-ROLE-001` | 建立主机 local PI 与从机 bypass 的 Domain 角色边界。 | SyncDpllFB | IN PROGRESS | `VDC-EVID-001` 当前可观测接口 | role switch 清理 PI/lock history；从机 local evidence 不更新 DCO。 |
| 5 | `VDC-ROLE-002` | 按 `VDC-CMD-001` 至 `VDC-CMD-005` 完成契约、稳定命令运输及定时 follower apply。 | RefMem + SyncDpllFB | PENDING | `VDC-SCHED-001`, `VDC-ROLE-001`, `VDC-CMD-005` | 交叉审核、跨核一致性、来源/序列/代际、共同时间和四板实际应用全部闭合。 |
| 6 | `VDC-ROLE-003` | 建立 Flash、manager mailbox 与 SCPI 的角色配置和显式持久化。 | System/maintenance | PENDING | `VDC-ROLE-002` | 任意节点角色/来源可配置并可读回 requested/applied generation。 |
| 7 | `VDC-ROLE-004` | 建立独立的本地晶振 trim、连续 clock-model 和故障 freeze。 | SyncDpllFB + clock owner | PENDING | `VDC-ROLE-001`, `VDC-ROLE-002` | trim 不得改写 DDS phase owner 或提高 lock/quality。 |
| 8 | `VDC-ROLE-005` | 完成角色/晶振驯服矩阵故障注入、当前源码 P3/HIL 和回退证据。 | 主控验收 | PENDING | `VDC-ROLE-003`, `VDC-ROLE-004`, `VDC-HOLD-001` | 多角色实验与 trim 不破坏 TDMA，精度由实测门禁判定；结果输入总验收。 |
| 9 | `VDC-SERVO-001` | 实现 FLL-assisted acquisition：多周期 phase slope、受限初始 step/feed-forward、frequency sanity/slew。 | SyncDpllFB | PENDING | `VDC-EVID-001`, `VDC-ROLE-001` | 进入 `FREQ_LOCK`，只发布 coarse/tracking candidate。 |
| 10 | `VDC-SERVO-002` | 实现二阶 Type-II PI tracking：`kp_q16/ki_q16`、显式积分状态、anti-windup、phase/rate slew、input residual 统计，以及 debug SCPI generation/mailbox 和自动回退工具。 | SyncDpllFB + System/maintenance | PENDING | `VDC-SERVO-001`, `VDC-ROLE-003` | `PHASE_LOCK` 稳定，DCO snapshot 完整可消费；debug 异常参数不被数值范围门禁拒绝，格式/资源错误仍留证。 |
| 11 | `VDC-LOCK-001` | 建立粗锁、tracking candidate、formal lock promotion gate。 | VdcQualityGateFB + VdcSyncAO | PENDING | `VDC-SERVO-002` | fine tier、连续窗口、RMS/peak/jitter、freshness、active calibration、formal timestamp 和非 provisional path 全通过。 |
| 12 | `VDC-SNAPSHOT-001` | 重建 VdcVector/DCO guarded snapshot，接入 core1 stable read。 | VdcVector + core1 realtime | PENDING | `VDC-LOCK-001` | seqlock/双缓冲/等价 guard 通过；stale/late/半更新 fail-closed。 |
| 13 | `VDC-HOLD-001` | 实现 HOLDOVER aging、dispersion/drift bound、RELOCKING 和 FAULT。 | VdcSyncAO + VdcQualityGateFB | PENDING | `VDC-SNAPSHOT-001` | 丢样本不伪造锁；超预算禁止 RUN；恢复重新 acquisition。 |
| 14 | `VDC-RUN-001` | 将 formal VDC gate 接入 RefMem mirror、T2/READY、FIRE_LOAD/RUN。 | Trigger + RefMem + SystemManager | PENDING | `VDC-LOCK-001`, `VDC-SNAPSHOT-001`, `VDC-HOLD-001` | coarse/provisional/holdover 超预算/unlocked 全部拒绝正式 FIRE_LOAD。 |
| 15 | `VDC-VERIFY-001` | 汇总阶段 replay、故障注入、四板实际输出测量、配置矩阵和长稳报告；NO5 关联见观测扩展任务。 | 主控验收 | IN PROGRESS | 分阶段开放；最终关闭需 `VDC-RUN-001`、`VDC-ROLE-005`、`VDC-CONFIG-001` 及对应长稳证据 | 诊断、命令应用、内部锁定和正式锁相分别判定；各配置绑定当前源码及原始证据，失败和回退可追溯。 |

## 迁移阶段门禁

### M0：资源与 resident cycle

- 入口：`HAOFV_STATE_MACHINE_ARCHITECTURE.md` 的 `TDMA-RESIDENT-01`、PIO resource contract 和当前 TDMA profile 已固定。
- 必须完成：`STOPPED -> STAGED -> ARMED -> RESIDENT_INIT -> RUNNING`；每 cycle 执行 `CYCLE_BOUNDARY -> LOCAL_UNLOAD -> LOCAL_LOAD -> FORWARD`。
- 禁止：VDC 在 resource/PIO phase 内计算 DPLL；frame completion 终止 resident loop；host 续装窗口。

### M1：Calibration 与 evidence

- 入口：TDMA `RUNNING` cycle evidence 可关联 sequence/CRC，Calibration snapshot 已可读取。
- 必须完成：完整 directed path matrix、dictionary、formal timestamp gate 和 path freshness。
- 禁止：默认零延迟、沿物理环临时累加、软件 timestamp 抬高 `DPLL_ELIGIBLE`。

### M2：粗锁与目标锁

- 入口：正式 evidence 连续有效；一主三从验证另需 `VDC-ROLE-002` 命令运输、时间映射和定时应用闭环。
- 粗锁：FLL-assisted acquisition 进入 `FREQ_LOCK`，只用于 bring-up/tracking candidate。
- 目标锁：Type-II PI tracking 满足 formal promotion，才允许 `FORMAL_LOCKED`。
- 禁止：用 state=`LOCKED`、累计 sample count、单向 leg 或 replay passed 替代 formal lock。

### M3：snapshot、故障恢复与 RUN

- 入口：formal promotion 可重复产生，DCO snapshot guard 已闭合。
- 必须完成：HOLDOVER aging、RELOCKING、FAULT、source/calibration generation 变化和 RUN gate。
- 禁止：RefMem/SCPI/Trigger 反写 VDC owner；holdover 超预算继续 FIRE_LOAD。

## 当前阻塞与统一完成定义

当前缺口分别由以下任务承接，证据只引用 Task Progress，不在本表抄录单次采样数字：

| 缺口 | 对应任务 | 解除条件 |
|---|---|---|
| 自主 raw 配置/生命周期已有补测，其余准入几何、目标容量、切换连续性和边沿误差界尚未闭合 | `VDC-TIME-002/003/004` | 先补齐资源/取消与目标验收，闭合严格校准，再验收全窗和物理误差，最后才开放同圈 trailer/evidence；DMA timer 区间不直接授予有效时间戳。 |
| 真实更新路径的 DPLL 超限、上游 TDMA 迟到和严格配置/校准验收仍待闭合 | `VDC-SCHED-001` | 用相同配置的板端窗口计数与当前源码 P3 复核，不能以无更新路径代替；`VDC-PROGRESS-20260915-034` 记录当前 P3 通过但 TDMA 相位仍超预算，需同工况对照，不能从现有实时 gate 推断全部静态预算通过。 |
| resident VDC 诊断字段与从机命令区尚未形成已验收交接 | `VDC-CMD-001/003/005` | 先审核协议，再证明三块从板有效接收及实际应用。 |
| 命令快照一致性、序列回绕和会话取消需要闭合 | `VDC-CMD-002/003` | 有界跨核稳定读取和 reset/STOP/重启负测通过。 |
| 共同生效时间与本地时钟映射尚未闭合 | `VDC-CMD-004` | 映射身份、建立过程和有效期可验证，实际应用时间可对账。 |
| 正式 hardware-latch、directed delay/bias、完整 matrix 和同圈 evidence 仍待闭合 | `VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001` | 正式 evidence 连续有效；此前 host/replay 和板端诊断均不能宣称目标锁。 |

VDC 迁移完成必须同时满足：

- HAOFV owner 边界和 STATE_MACHINE/TDMA resident lifecycle 不被破坏；
- host/build、资源/状态机门禁和必要 OTA/HIL 通过；
- active Calibration/path matrix、formal timestamp evidence、FLL/PI、promotion、snapshot 和 failure recovery 全链路可追溯；
- 主机闭环、从机命令应用、实际输出跟随和正式锁相分别有证据；不同节点数、周期和帧型分别验收；
- coarse lock、formal lock、HOLDOVER、RELOCKING、FAULT 语义在 snapshot/SCPI/report 中分离；
- 失败时回退到最近已验证状态，不以旧复合路径、旧 receipt 或旧 build 掩盖缺口。
