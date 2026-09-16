# 单节点序列触发待办

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/sequence/TRIGGER_SEQUENCE_TODO.md`
Related: `docs/trigger/sequence/TRIGGER_SEQUENCE_ARCHITECTURE.md`, `docs/trigger/sequence/TRIGGER_SEQUENCE_TASK_PROGRESS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-16

## 当前范围

按用户最新确认，本次独立节点调试：配置序列后由 TRIG:START 预置首状态，
软件或所选 IN1-IN4 TRIG 推进到后继状态，输出本地编码，等待建立时间，
从独立 OUT 输出完成脉冲。START 预置不计触发且不发完成脉冲；首次触发进入第二状态。
忙时拒绝并计数；末状态之后的下一事件回到首状态。DONE 不自动推进。
新增 SCPI IO 读取实际引脚电平。TDMA 仅预留接口，不实现通信或全节点裁决。
当前先按用户指定的SP8T地址序列验证：OUT1-OUT3构成地址，逐状态从全低至全高，
下一事件回绕；OUT4独立输出完成脉冲。板端IO通过不代表开关本体射频通路已验收。

本次范围修订优先于历史多节点待办和进度记录中的依赖关系。
四节点缺失不阻塞单节点开发和验收；历史 P3 失败事实保留，不能写成通过。
用户此前指定以单板功能验证作为产品行为验收条件，该历史决策与P3失败记录继续保留；
当前仓库硬规则仍要求固件、工具和测试改动在提交前取得同源码指纹P3凭证，不能绕过。

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
| NSEQ-015 | 单节点GPIO动作后端与资源独占 | IN PROGRESS | 输入边沿、编码输出、建立定时、完成脉冲、停止释放；冲突明确拒绝，不抢占既有任务 |
| NSEQ-020 | 单步状态机与控制命令 | DONE | START等待首步、busy拒绝、循环、暂停/恢复/停止/故障、游标与完成回调，模型及软件控制板测通过；进度008 |
| NSEQ-021 | 软件单步板端闭环 | DONE | SP8T地址全状态及回绕、真实IO编码/完成输出和时间读回通过；进度008；示波器精度留NSEQ-031 |
| NSEQ-022 | 固化SP8T验收工具及外部观察入口 | DONE | 正式工具记录身份、逐步IO和失败清理，外部源不发送软件STEP；CLI回归、BUS实测及无输入负向验证通过，见进度009 |
| NSEQ-030 | IN1-IN4源选择和IO查询 | IN PROGRESS | 实际board映射，双边沿、未选输入隔离、暂停/换源/停止无事件重放；SCPI读真实IN/OUT |
| NSEQ-031 | 外部TRIG单板验收 | IN PROGRESS | IN1低频功能见NSEQ-032；其余输入实际脉冲及输入/编码/完成共同波形仍需补齐 |
| NSEQ-032 | IN1低频SP8T流程验收 | DONE | 接线后的双沿、循环、忙时拒绝、暂停/恢复、停止取消、重启首状态及未选源隔离通过；进度010，独立波形仍留NSEQ-031 |
| NSEQ-050 | 专项回归与独立复核 | IN PROGRESS | 软件/单板SCPI与IO证据和独立复核已通过；外部脉冲和实测波形仍待完成 |
| NSEQ-051 | 提交和交付 | PENDING | 按用户最新范围完成单板功能验收与交付；代码文档分离提交，不伪称P3通过 |
| NSEQ-060 | 后续提速前核对SYNC域PIO分配 | DONE | 用户确认仅使用PIO0；board/SYNC分配已核对，PIO1/PIO2不改，见进度011 |
| NSEQ-061 | 序列PIO执行与热加载 | DONE | 生产失败回滚测试、重复装卸、暂停/恢复及STOP单板验证通过；进度012，按用户要求不以P3前置 |
| NSEQ-062 | PIO提速验收 | IN PROGRESS | 进度012完成首档提速功能，后续按用户设置继续压力测试；独立波形、单步延迟和极限吞吐仍需另测 |
| NSEQ-063 | START首状态预置与后继循环 | IN PROGRESS | START取得资源后输出首状态，初始建立期间禁止准入且不计数/不发完成脉冲；首次触发进入第二状态，末状态后回绕；host、Release及BUS单板IO通过，P3与OUT4外部波形待验收 |

## RefMem 动态槽位与同板双角色规划

目标是允许一台物理设备动态承载“DUT 链路控制节点”和“网分/VNA 节点”，
角色通过 RefMem 槽位装载和激活决定，不把逻辑角色永久绑定到固定板号。
本节先定义实施顺序和验收边界；具体字段冻结前须与 RefMem canonical 文档及登记表交叉审核。

| ID | 任务 | 状态 | 退出门禁 |
|---|---|---|---|
| NSEQ-070 | RefMem 槽位角色模型 | PENDING | 明确 `physical_board_id`、`logical_node_id`、`slot_id`、`instance_id`、`role_mask`、`persona_mask`、`resource_claim`、`io_claim` 和 generation 的关系；同板多槽位使用现有 `REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT` |
| NSEQ-071 | DUT 链路控制槽位 | PENDING | 槽位负责 `DUT_LINK_CONTROL`；发布 requested/applied link code、SP8T/SP2T 目标与实际值、link sequence index、settle 状态和 fault；OUT 电平只能由该槽位写入 |
| NSEQ-072 | 网分/VNA 槽位 | PENDING | 槽位负责 VNA trigger、READY、DONE、测量状态和质量；READY/DONE 只写测量事实，不直接推进序列游标 |
| NSEQ-073 | 同板资源冲突校验 | PENDING | 同一物理设备的多个槽位允许共存，但 PIO、DMA、SMA、输出位、输入位和 RefMem 写权限不得重叠；冲突在 CONFIG_VALIDATE 阶段拒绝 |
| NSEQ-074 | 动态槽位装载事务 | PENDING | `CONFIG_STAGE` 扫描空闲槽位并生成 load plan；校验通过后 `CONFIG_ACTIVATE` 原子发布；失败回滚不影响当前 active generation |
| NSEQ-075 | 序列与槽位绑定 | PENDING | START 绑定 `run_id`、sequence generation、plan CRC 和槽位清单；运行期间槽位清单冻结，STOP 后才允许重新分配 |
| NSEQ-076 | 链路切换事件链 | PENDING | 每步按“请求链路电平→等待 settle→发布 LINK_APPLIED→触发 VNA→接收 READY/DONE”执行；DONE 不自动推进下一状态 |
| NSEQ-077 | 槽位回收与旧事件隔离 | PENDING | STOP、FAULT 或重新装载时回收 lease，清除旧 generation 的事件和数据写权限；旧槽位事件不能污染新运行 |
| NSEQ-078 | RefMem/SCPI/GUI 读回 | PENDING | 提供活动槽位、角色、资源租约、generation、requested/applied 值和 READY/DONE 状态的只读检查；GUI 显示物理设备与逻辑槽位关系 |
| NSEQ-079 | 同板单板验收 | PENDING | 一台设备加载 DUT_LINK_CONTROL + VNA_MEASUREMENT 两个槽位，完成首步、后继步、暂停、停止、故障恢复和槽位回收；保留原始 RefMem 快照 |
| NSEQ-080 | 多板动态分配验收 | PENDING | 多设备按不同槽位组合装载同一角色模型，验证 generation、资源冲突、ACK/NACK、回滚和重新分配；不把单板结果升级为全局裁决通过 |

### 角色和数据边界

| 区域 | 唯一写者 | 主要内容 |
|---|---|---|
| `TriggerRegion` | 序列触发节点 | run、generation、plan CRC、current/next、accepted/completed、运行状态 |
| `IoRegion` | DUT_LINK_CONTROL 槽位 | requested/applied link code、SP8T/SP2T 值、link state、settle、fault |
| `MeasurementRegion` | VNA_MEASUREMENT 槽位 | trigger sequence、READY/DONE sequence、时间戳、测量状态和质量 |
| `AckCommandRegion` | 配置/网关节点 | stage、validate、activate、ACK/NACK、回滚原因 |
| `RoleRegion` | 配置阶段 | 槽位角色、persona、资源和 IO 声明；RUN 后冻结 |

## 后续接口预留

| 原任务 | 后续范围 | 本次处理 |
|---|---|---|
| NSEQ-012 | A3/A0/目标节点配置事务 | 不实施；配置generation和运行快照保留未来接入边界 |
| NSEQ-040 | TDMA预约、fence、全节点DONE裁决 | 不实施；本地请求与完成身份接口可供后续消费，不能伪造全局完成 |

## 验证与阻塞

- 当前设备COM10已完成新固件身份、SP8T软件切步和IO验证，记录见Task Progress进度008。
- IN1已接信号源并完成低频流程；当前SP8T/IN1上升沿配置，IDLE、输出低。配置在RAM中，重启后需重新下发。
- IN2-IN4实际脉冲、独立测量波形及SP8T实体射频通路尚缺证据，NSEQ-031不能关闭。
- PIO低频及首档提速功能已通过，后续压力档与证据边界见进度012；保持停止后再调整参数。
- START首状态预置的host回归、Release构建及BUS单板IO已通过；当前无外部脉冲源，P3凭证和OUT4外部波形证据尚待完成，见进度014。
- 每个实现批次完成对应host测试、受影响构建和单节点硬件验证。
- 本任务不修改TDMA；产品行为按单板功能判断，提交仍服从仓库当前P3硬门禁。
