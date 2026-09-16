# 单节点序列触发待办

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-17

## 当前范围

按用户最新确认，本次最小测试系统由 DUT_LINK_CONTROL 和 VNA_GATEWAY 组成。
保留两种运行模式：独立 SP8T 由软件或所选 IN1-IN4 逐状态推进，可配置 PULSE/LEVEL 状态反馈
或 NONE 纯编码；组合模式的 DUT 只输出 SP8T 编码，VNA 网关输出测量触发并接收 READY。
组合模式按 START 首状态建立 → RJ45 TDMA LINK_APPLIED → VNA 触发 → READY 输入或 SCPI NEXT 网关 →
RJ45 TDMA READY_NEXT → DUT 后继状态循环；同一 READY 不再直接进入 DUT 外部准入。
START 预置不计触发；首次有效反馈进入第二状态。轮次默认值、有限结束和显式持续运行
语义见 NSEQ-A-01/A-03；DONE 不自动推进。有限轮次未耗尽或显式持续时才允许末状态回绕。
新增 SCPI IO 读取实际引脚电平。按最新要求，两角色控制接入本地回环 TDMA；
采用专门的 LOOPBACK 模式走已接线的 RJ45 发收物理回环；RJ45 仅保留为兼容命令别名。
复用现有 TDMA owner、调度和消息校验，不将单板回环声明为跨板通信或全节点裁决。
保留独立 `CONF:SWITCH# N` SP8T 控制和 DUT-only 的 MANUAL/外部脉冲序列，组合模式不抢占独立模式资源。
当前先按用户指定的SP8T地址序列验证：OUT1-OUT3构成地址，逐状态从全低至全高，
下一事件回绕。板端IO通过不代表开关本体射频通路已验收。

本次范围修订优先于历史多节点待办和进度记录中的依赖关系。

## 当前交付快照与接续顺序

### 受限提交 risk 与下一提交约束

用户授权为换机接续提交当前实现，包括尚未完成验证的部分；这不是正式发布或全功能验收。
代码提交必须带 `risk` 标记。紧随其后的文档提交仅保存交接信息，不视为解决风险；
**下一次代码提交必须以关闭下列当前风险为目标，不得先扩展新功能。**
关闭必须有修复、负向回归、对应固件实测和 Task Progress 证据；仅重跑得到 PASS 不能关闭间歇失败。

| 风险 ID | 当前风险与证据 | 下一代码提交的关闭条件 |
|---|---|---|
| NSEQ-RISK-01 | typed snapshot quality、一致缓存及逐查询 SCPI 错误归因已由当前 build 的单板凭证验证；旧失败保留 | 已关闭：两种固定 profile 的诊断均取得一致快照且错误队列为空；`UNAVAILABLE`、解析失败或遗留错误仍由门禁拒绝 |
| NSEQ-RISK-02 | 共享配置事务门覆盖 START 校验至入队及 SCPI/RefMem/model mutation，锁顺序已固化 | 已关闭：相关 host 并发/内部装载测试、当前 build 有限和连续板测均绑定同一 staged 指纹通过 |
| NSEQ-RISK-03 | 非零 `exchange_id`、暂停恢复和重启轮换、旧 exchange 拒绝已实现 | 已关闭：连续 profile 证明 PAUSE 静默、CONT 后 exchange 轮换并继续推进，STOP 后输出和 TDMA owner 归零；旧帧/超时恢复由确定性注入测试覆盖 |

完整动态claim/多板分配、外部波形、射频通路及严格TDMA稳定性仍在原任务中待完成，
当前受限提交仅准许同板物理回环调试，不把这些能力纳入可用范围。
进度027记录换机入口和旧报告；进度028记录受限单板凭证门禁及本次不执行P3的明确范围。

### 已验证范围

以下为 2026-09-16 工作区与验证快照，非接口或容量事实源；具体证据见 Task Progress 进度021/022及后续记录。

| 范围 | 当前状态 | 证据边界 / 下一步 |
|---|---|---|
| 受限代码检查点 | `ba5d2fb8`，带risk，见进度027 | 包含既有模型提交及当前单板主线；下一代码提交须关闭risk表。动态claim/租约未完成，不能宣称全功能验收 |
| 独立 SP8T 与轮次 | 已进入受限代码提交，已有单板通过证据 | 进度026当前固件BUS/IN1/手动回归通过；进度021保留百轮及持续证据。最大轮次仅配置读回，万轮推进为PIO仿真，不能写成硬件执行通过 |
| 节点 SCPI 配置 | ROLE staging/ACT 与读回已有历史单板证据 | `sequence-role-stage-hil-r2.json` 对应 build `20260916071550`；完整动态槽位路由、claim/epoch 与租约仍待接入 |
| RJ45 + VNA 组合 | 单板主线已通过，见进度025/026 | 已有单轮、十轮、连续与STOP证据；当前固件复测十轮和连续PAUSE/CONT/STOP通过。真实GUI命令路径、物理TX/RX与角色回执均有证据；历史失败保留，波形与多板未验收 |
| 当前板端 | 已 OTA build `20260916155106` | 见进度029及 `sequence-scpi-next-ota/summary.json`；使用 SCPI NEXT 模拟 READY 的有限十轮、连续 PAUSE/CONT/STOP 通过，最终序列/TDMA停止、输出零 |
| 本轮通信调试 | 回包证据、probe启动与START只读争锁已修复；诊断快照偶发失败待处理 | 见进度024/025/026；probe只递交真实TX字节证明的本地回包，不生成远端health/WKC；START已发布视图测试/构建/独立审核和板测通过，未合并real-flight整体分支 |
| 调试 GUI | 分页及命令路径已板测，已进入受限代码提交 | 见进度023/025/026；独立序列、RJ45/VNA、手动开关和维护分开，Tk布局回归通过；当前固件组合模式直接执行GUI命令通过，人工Tk点击仍未确认 |

GUI 分页批次已完成；用户已要求继续调试，并参考 real-flight 最新通信提交。
当前按以下顺序接续：

1. 组合首状态测量、READY 后继切换、末次测量后有限结束和显式持续 STOP 已闭合；
   暂停/CONT和START只读争锁修复已有当前固件证据；接着定位诊断快照间歇失败并补异常恢复，不扩大缓存窗口或超时，不伪造 hop/WKC，不绕过 RJ45。
2. 同一固件身份下独立 MANUAL/IN1 和手动 SP8T 需回归；继续检查未覆盖的故障/配置冻结入口。
3. 新版 GUI 命令构造器与批处理执行器已直接用于板端验证；人工 Tk 点击和外部波形仍未确认。
   通过的代码和文档后续按授权分别提交。短暂 `down_running` 掉落按用户要求后续优化，原失败保留。
4. 活动 claim/租约、多板动态分配、其他输入独立激励、波形与射频通路继续保留为未完成项，
   不扩大本次单板功能确认结论。当前 PAUSE 保留编码和租约，仅取消网关测量等待/脉冲。

## HAOFV 约束

- Core1/PIO0 只执行确定性的输入捕获、序列游标推进、SP8T 电平更新和 VNA 触发输出；不得访问 SCPI、USB、文件系统或动态内存。
- SCPI、RefMem 槽位分配、角色配置和读回运行在 Core0 配置面；`TRIG:START` 后绑定 generation 与资源租约，运行期间配置冻结。
- DUT/VNA 跨域状态使用唯一写者和 seqlock/原子发布；READY、DONE、LINK_APPLIED 均为事实事件，不在事件消费方隐式重复推进。
- 每个函数块动作必须立即返回；资源忙、generation 失配、IO 冲突和 READY 超时通过状态与错误码发布，不得在实时动作中阻塞等待。
- PIO0 是序列触发专用资源，PIO1/PIO2 不得被此功能改写；停止或故障时由资源所有者统一释放并拉低输出。
四节点缺失不阻塞单节点开发和验收；历史 P3 失败事实保留，不能写成通过。
用户最新明确单板调试只做功能确认，不执行 P3；本次提交使用逐文件白名单的 sequence
单板凭证门禁。白名单外源码仍要求 P3；单板凭证不扩展为多板、波形、RF或稳定性结论。

## 状态规则

仅使用 DONE、IN PROGRESS、PENDING、BLOCKED。
软件模型、SCPI 解析、实际引脚动作与外部输入验证分别留证；
命令 accepted 不等于执行完成。只在本行退出门禁通过后标为 DONE。
架构维护接口，本文维护顺序，[Task Progress](TRIGGER_SEQUENCE_TASK_PROGRESS.md)
维护实际证据和失败记录。

## 当前任务

| ID | 任务 | 状态 | 退出门禁 |
|---|---|---|---|
| NSEQ-001 | 建立标准三件套和索引 | DONE | 文档门禁、独立复核通过，见进度 003 |
| NSEQ-002 | 审计指令、配置、运行和硬件入口 | DONE | 识别占位回包、PIO冲突、捕获单owner，见进度 001/002/003 |
| NSEQ-003 | 明确单节点语义和接口 | DONE | 文档落实用户确认的循环、busy拒绝、TRIG输入/完成OUT、编码加建立时间及IO读取；独立复核见进度008 |
| NSEQ-010 | 参数展开、计划、CHECK、ACTive纯C模型 | DONE | 真实C测试、独立CRC期望、构建与软件复核通过，见进度004/005；单板验证并入NSEQ-011 |
| NSEQ-011 | 真实SCPI配置与查询 | DONE | 长短命令、完整参数解析、范围/CRC/失效/冻结/回滚，真实libscpi测试和板端读写通过；进度008 |
| NSEQ-015 | 单节点GPIO动作后端与资源独占 | IN PROGRESS | 输入边沿、DUT编码电平、VNA触发脉冲、停止释放；冲突明确拒绝，不抢占既有任务 |
| NSEQ-020 | 单步状态机与控制命令 | DONE | START等待首步、busy拒绝、循环、暂停/恢复/停止/故障、游标与完成回调，模型及软件控制板测通过；进度008 |
| NSEQ-021 | 软件单步板端闭环 | DONE | SP8T地址全状态及回绕、真实IO编码/完成输出和时间读回通过；进度008；示波器精度留NSEQ-031 |
| NSEQ-022 | 固化SP8T验收工具及外部观察入口 | DONE | 正式工具记录身份、逐步IO和失败清理，外部源不发送软件STEP；CLI回归、BUS实测及无输入负向验证通过，见进度009 |
| NSEQ-030 | IN1-IN4源选择和IO查询 | IN PROGRESS | 实际board映射，双边沿、未选输入隔离、暂停/换源/停止无事件重放；SCPI读真实IN/OUT |
| NSEQ-031 | 外部TRIG单板验收 | IN PROGRESS | IN1低频功能见NSEQ-032；其余输入实际脉冲及输入/编码/完成共同波形仍需补齐 |
| NSEQ-032 | IN1低频SP8T流程验收 | DONE | 接线后的双沿、循环、忙时拒绝、暂停/恢复、停止取消、重启首状态及未选源隔离通过；进度010，独立波形仍留NSEQ-031 |
| NSEQ-050 | 专项回归与独立复核 | IN PROGRESS | 软件/单板SCPI与IO证据和独立复核已通过；外部脉冲和实测波形仍待完成 |
| NSEQ-051 | 提交和交付 | IN PROGRESS | 受限代码ba5d2fb8与配套文档分离提交，供换机拉取，见进度027；正式交付仍受NSEQ-085风险关闭约束，不伪称P3通过 |
| NSEQ-060 | 后续提速前核对SYNC域PIO分配 | DONE | 用户确认仅使用PIO0；board/SYNC分配已核对，PIO1/PIO2不改，见进度011 |
| NSEQ-061 | 序列PIO执行与热加载 | DONE | 生产失败回滚测试、重复装卸、暂停/恢复及STOP单板验证通过；进度012，按用户要求不以P3前置 |
| NSEQ-062 | PIO提速验收 | IN PROGRESS | 进度012完成首档提速功能，后续按用户设置继续压力测试；独立波形、单步延迟和极限吞吐仍需另测 |
| NSEQ-063 | START首状态预置与后继循环 | IN PROGRESS | START取得资源后输出首状态，初始建立期间禁止准入且不计数/不发完成脉冲；首次触发进入第二状态，剩余轮次允许时回绕；host、Release及BUS单板IO通过，OUT4外部波形待验收；本次单板不执行P3 |

## RefMem 动态槽位与同板双角色规划

目标是允许一台物理设备动态承载“DUT 链路控制节点”和“网分/VNA 节点”，
角色通过 RefMem 槽位装载和激活决定，不把逻辑角色永久绑定到固定板号。
本节先定义实施顺序和验收边界；具体字段冻结前须与 RefMem canonical 文档及登记表交叉审核。

| ID | 任务 | 状态 | 退出门禁 |
|---|---|---|---|
| NSEQ-070 | RefMem 槽位角色模型 | IN PROGRESS | 纯角色模型已校验槽位、实际 FB、稀疏 instance ID、claim_epoch、active_generation 和 IO 声明，见进度018；仍须接入动态激活及运行快照 |
| NSEQ-071 | DUT 链路控制槽位 | IN PROGRESS | NONE 模式的纯编码电平输出已通过软件及单板验证，见进度018；仍须绑定 DUT_LINK_CONTROL 槽位，发布 requested/applied、link index、settle 和 fault，落实唯一写者 |
| NSEQ-072 | 网分/VNA 槽位 | IN PROGRESS | ROLE配置、PIO0 trigger/READY及实际RJ45首末测量在进度025通过；继续接入完整动态激活/运行快照，事实不得隐式重复推进游标 |
| NSEQ-073 | 同板资源冲突校验 | IN PROGRESS | 纯模型已拒绝重复角色、槽位与 IO 冲突，见进度018；仍须在 CONFIG_VALIDATE 接入实际 PIO/DMA/SMA/RefMem 资源声明，双角色组合使用唯一实时 owner |
| NSEQ-074 | 动态槽位装载事务 | IN PROGRESS | NODE:ROLE 的真实 FB/装载表联合 staging、命令槽串行及镜像激活已实现；显式同板多槽提案模型已补，见进度019；仍须接入生产空闲槽位选择、活动 claim/epoch 与租约发布 |
| NSEQ-075 | 序列与槽位绑定 | IN PROGRESS | 同板 LINK 绑定真实角色/model epoch 与 run/generation/步骤身份，见进度021；活动 claim/epoch/lease 与完整动态路由尚待接入，运行时配置冻结不得回退 |
| NSEQ-076 | 链路切换事件链 | IN PROGRESS | LINK_APPLIED→PIO0网关触发→READY→READY_NEXT真实RJ45首末测量、有限/持续及PAUSE/CONT在进度025通过；仍须补故障恢复，DONE不自动推进 |
| NSEQ-077 | 槽位回收与旧事件隔离 | PENDING | STOP、FAULT 或重新装载时回收 lease，清除旧 generation 的事件和数据写权限；旧槽位事件不能污染新运行 |
| NSEQ-078 | RefMem/SCPI/GUI 读回 | IN PROGRESS | REP/LINK 配置与运行读回、GUI 模式/轮次入口及分页已接入，见进度021/023；完整动态租约及物理板/逻辑槽位关系仍须随 owner 绑定补齐 |
| NSEQ-079 | 同板单板验收 | IN PROGRESS | 进度025实际加载DUT_LINK_CONTROL及VNA_GATEWAY双槽，通过首末步、后继循环、有限结束、连续STOP、PAUSE/CONT与独立回归；继续补故障恢复及完整动态租约回收，保留RefMem快照 |
| NSEQ-080 | 多板动态分配验收 | PENDING | 多设备按不同槽位组合装载同一角色模型，验证 generation、资源冲突、ACK/NACK、回滚和重新分配；不把单板结果升级为全局裁决通过 |
| NSEQ-081 | DUT/VNA RJ45 回环 TDMA 控制 | IN PROGRESS | 进度025/026单板组合首末测量、真实回帧、有限/持续、STOP及PAUSE/CONT通过，START只读争锁已修；继续定位诊断快照偶发失败、补异常恢复、动态claim和owner冻结；严格稳定性失败保留，不伪造hop/WKC或软件直达 |
| NSEQ-082 | 独立与组合有限轮次 | DONE | REP配置、PIO配额和BUS上限测试通过；进度021独立有限/持续、进度025组合单轮/十轮末次测量后结束及连续STOP通过，同固件独立回归通过；GUI构造/执行路径已板测，超长轮次不外推为硬件执行通过 |
| NSEQ-083 | GUI 按模式分页与配置分组 | DONE | 独立序列、RJ45/VNA、手动开关、设备维护分开；独立草稿、切页不发指令、公共连接/IO/日志、设备模式与草稿分离及启动配置失效保护完成；真实 Tk 布局与命令/工具回归通过，见进度023，当前固件硬件联调仍随 NSEQ-081/082 验收 |
| NSEQ-084 | 异步 RX 的本地回环 TX 证据保留 | DONE | 捕获后覆盖复现及RX job.expected修复通过；进度025补probe process/无拓扑启动与远端抑制，容量矩阵、Release、独立复核和新固件真实组合回环通过；捕获前过期仍拒收，不将本地回包计入远端通信成功 |
| NSEQ-085 | 受限提交后关闭当前risk | DONE | 当前源码已完成单板 OTA、SCPI NEXT 有限十轮、连续 PAUSE/CONT 和匹配 staged 凭证，见进度029；只关闭单板功能 risk，不表示 P3、多板、波形、RF、独立输入边沿或严格 TDMA 稳定性通过 |
| NSEQ-086 | MANUAL 来源统一与 RJ45 READY 分流 | DONE | 对外移除 BUS；独立 MANUAL 直接 NEXT，LOOPBACK 的 MANUAL 仅代替 VNA READY 且不启动输入捕获/READY 超时，真实 READY_NEXT 回环仍为 DUT 切步门禁；host、Release、严格单板 OTA、有限十轮及连续 PAUSE/CONT gate 均通过，见进度030；不外推为 P3、多板、波形、RF、独立输入边沿或严格 TDMA 稳定性通过 |

NSEQ-081 的后续完整性检查如下；当前实施顺序以“当前交付快照与接续顺序”为准，
先完成单板角色主线，再扩展动态 claim/租约。不将基础环路计数代替角色请求/回执证据：

用户已要求先跑通主线，再优化接收短暂掉落。`rj45-loopback-r3` 仍为严格稳定性失败，
不再阻塞本轮功能实现；原始数据保留，组合工具明确标记稳定性未验收，严格 TDMA 工具判据不放宽。

1. 接入活动 proposals/claim epoch 与一致路由快照，区分 RefMem role slot 和 TDMA topology slot；
   同板多角色共用真实板身份。封闭自动 RX 等 owner 层配置冻结入口。
2. 核算现有 mailbox/调度容量，定义有界角色请求/回执编码及 ACK、去重、旧代际、超时处理；
   latest-value 合并不可丢弃已接纳步骤，不占用 VDC 字段。
3. 在 TDMA owner 内递交经过真实 RJ45 RX 验证的 self-return payload，保留远端 WKC/freshness；
   验证回程、断线、重帧和旧帧，不使用 TX 成功或函数直达伪造接收。
4. 统一 PIO0/SMA owner 接入 DUT 编码、VNA trigger/READY，验证首次测量和唯一事件推进，
   验证现有 PAUSE 保留编码/租约并取消网关测量的行为，以及 STOP、故障及 GUI；
   PAUSE 安全释放/重取留作后续完善。独立 SP8T、DUT-only MANUAL/IN 必须保持可用。

### 角色和数据边界

| 区域 | 唯一写者 | 主要内容 |
|---|---|---|
| `TriggerRegion` | 序列触发节点 | run、generation、plan CRC、current/next、accepted/completed、运行状态 |
| `IoRegion` | DUT_LINK_CONTROL 槽位 | requested/applied link code、SP8T/SP2T 值、link state、settle、fault |
| `MeasurementRegion` | VNA_MEASUREMENT 槽位 | trigger sequence、READY/DONE sequence、时间戳、测量状态和质量 |
| `AckCommandRegion` | 配置/网关节点 | stage、validate、activate、ACK/NACK、回滚原因 |
| `RoleRegion` | 配置阶段 | 槽位角色、persona、资源和 IO 声明；RUN 后冻结 |

### 最小闭环接线

独立模式下 `DUT_LINK_CONTROL` 接收推进脉冲并输出 SP8T 编码电平和可选状态反馈。
组合模式下 `VNA_GATEWAY` 接收网分 READY，经过真实 RJ45 TDMA 回程请求 DUT 后继状态；
DUT 使用 MANUAL/NONE，禁止直接复用同一 READY 输入推进。READY/DONE 事实不能在消费方重复推进。
节点装载包装入口为 `CONF:SEQ:NODE:LOAD`、`CONF:SEQ:NODE:ACT` 和
`READ:SEQ:NODE:LOAD?`，复用 RefMem 事务；运行期间拒绝修改。
`CONF:SEQ:NODE:ROLE slot_id,instance_id,DUT或VNA` 联合暂存装载行与实际 FB 声明，
`READ:SEQ:NODE:ROLE? instance_id` 分别读 active/staging；不能用普通 NODE:LOAD 的 enabled
代替真实 FB 启用。原始 RefMem SCPI、SCPI SYNC delta、模型转台载入及内部自动 RX 的
`distributed_refmem_apply_node_load_sync_payload_internal()` 均已接入共享配置事务门；
运行快照冻结不再只依赖 SCPI 外层检查。
完整动态 claim/lease 生命周期及多板重新分配仍未验证。
LINK 入口已提供同板角色/IO 绑定与运行状态，完整动态 claim/lease 快照仍未接入；
这些包装入口不等于后续多板动态分配已完成。

当前接线的可复现基础验证使用 `tools/hardware_acceptance/sequence_feedback_validate.py`：
信号源模拟 READY 接 IN1，OUT4 回接 IN2；先检查 MANUAL 编码全组合及回接，再观察持续 IN1 输入、
暂停和恢复。该工具不把旧 status pulse 路径升级为 VNA 角色执行事实；原始证据见进度017。

## 后续接口预留

| 原任务 | 后续范围 | 本次处理 |
|---|---|---|
| NSEQ-012 | A3/A0/目标节点配置事务 | 不实施；配置generation和运行快照保留未来接入边界 |
| NSEQ-040 | TDMA预约、fence、全节点DONE裁决 | 全节点裁决不实施；本地回环角色控制按 NSEQ-081 实施，不能伪造全局完成 |

## 验证与阻塞

- 最新设备身份、build、端口和单板结果见 Task Progress 进度025及后续记录；历史 COM10 记录仅对应进度008。
- IN1 已接信号源；DUT-only NONE 模式的软件来源与持续输入、暂停恢复及 STOP 在历史对应 build 通过；旧报告中的 BUS 是历史命名，当前接口只接受 MANUAL。验收结束时 IDLE、输出低；不作为当前诊断 build 的实时读回。配置在 RAM 中，重启后需重新下发。
- IN2-IN4实际脉冲、独立测量波形及SP8T实体射频通路尚缺证据，NSEQ-031不能关闭。
- PIO低频及首档提速功能已通过，后续压力档与证据边界见进度012；保持停止后再调整参数。
- START 首状态的 host、构建及单板 IO 已通过；外部源现已接通，见进度018/021。独立波形仍未取得，本次按用户要求不执行 P3。
- 双角色本地绑定、真实RJ45首末测量、有限/持续及PAUSE/CONT已有进度025/026旧固件证据；本次 typed snapshot、配置事务、exchange identity 和 SCPI NEXT 已在进度029由当前固件单板凭证验证，完整动态claim resolver仍未接入。当前PAUSE保留租约与编码电平。
- 每个实现批次完成对应host测试、受影响构建和单节点硬件验证。
- 本地回环 TDMA 角色控制按 NSEQ-081 推进；本次按用户明确授权仅做单板功能确认、不执行 P3。受限替代凭证须由 `sequence_single_board_gate.py run` 对当前 staged 源码和当前固件重新生成，旧报告不能放行。
