# VDC 内部主域待办

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_TODO.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/sync/SYNC_IO_TODO.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`
Last updated: 2026-09-19

## 文档接口与状态规则

本表维护任务、状态、依赖和退出门禁；稳定语义见 [Architecture](VDC_DOMAIN_ARCHITECTURE.md)，实施与失败证据见 [Task Progress](VDC_TASK_PROGRESS.md)及其归档索引。历史调试过程不在本表重复。

| 状态 | 含义 |
|---|---|
| IN PROGRESS | 正在推进，或已有实现但完整验收未闭合。 |
| PENDING | 尚待实施或满足对应前置；暂停路线也保留此状态。 |
| BLOCKED | 有明确外部阻塞、失败证据和解除条件。 |
| DONE | 该任务声明的范围已完成，不代表父任务或全部配置通过。 |

本次仅重组与精简，保留已有任务 ID 和状态。任务表为状态唯一落点；长期目标和阶段门禁不因子项通过自动关闭。

## 当前主线与里程碑

**目标：四板 DPLL 稳定锁相与 VDC 一致发布。** NO1 自主 PI 并发布闭环事件时间戳；NO2–NO4 接收、返回 ACK，加本地有向 delay 后由各自 Core1 跟踪。NO1 不计算逐从校正，接收也不依赖从板已经锁相。

席位边界：新增或扩容特等席须先由用户审核（`EXE-SEAT-01`），明确当前占用和后续 SYNC 余量。健康镜像、统计与查询由 Core0 异步处理，不新增 Core1 发布工作，不以诊断新鲜度替代实时控制授权。

| 目标 ID | 范围 | 状态 | 退出门禁 |
|---|---|---|---|
| `VDC-LONGTERM-001` | HAOFV 下可配置多节点 DPLL 闭环 | IN PROGRESS | 四板主线先闭合；后续角色、节点数、周期和帧型各自验收，不继承未测配置的通过结论。 |
| `VDC-LONGTERM-002` | 四板共同物理输出时间轴与 VDC 发布 | IN PROGRESS | 下列 A–D 门禁闭合，实际输出、控制收敛和发布视图分别留证。 |

| 里程碑 | 完成判据 |
|---|---|
| `VDC-LONGTERM-002-A` 路径与输出坐标 | 独立 forward-CS 证据及 delay 方向、单位、代际、CRC 一致；路径 delay 与输出补偿分离，结束恢复已知 STOP 配置。 |
| `VDC-LONGTERM-002-B` 持续运行 | 同一会话输出无未解释断流；DMA 退休、后缀补给和实际 GPIO 边沿均有证据。 |
| `VDC-LONGTERM-002-C` 相位与频差 | 三从持续采用 DCO，内部斜率与外部波形可对照；固定参数跨启动、持续窗口及测量误差预算通过。 |
| `VDC-LONGTERM-002-D` 恢复与发布 | 坏帧、失联、STOP/ARM 后恢复通过；Core1/Core0 的 time、valid、quality、generation 一致，资源及静态调度预算可核验。 |

用户阶段目标（目标快照，非已验收能力或固件状态门限）：粗锁定 ≤10 µs、精锁定 ≤1 µs、完全锁定 ≤100 ns；相位优化争取 ±50 ns，频差优化目标 ±50 ppb。频差优化不前置阻塞相位闭环。

判级使用预先声明窗口内、三从相对 NO1 的全部有效同序边沿最大绝对相差；同时报告中心、峰峰值、RMS、斜率与收敛时间。四板等级取最低已证明等级；缺脉冲、身份不明或测量不确定度未闭合时，不宣称正式锁定。

### 已有基线

当前试验为四板环路、5 m 网线、10 Mbit/s、250 MHz / 4 ns TIMER1（配置快照，非硬件契约）。TIMER0 保留 SDK 系统计时；DPLL/VDC/SYNC 使用 TIMER1，PIO/DMA 执行已提交边沿。

已具备 typed 参考发布、三从匹配/ACK/本地 DCO 采用、独立输出 delay、有限及持续输出、内部 GUARD 和可选示波器联合采集。外参补偿下十分钟及相同配置两轮 STOP/ARM 验证通过，见 `VDC-PROGRESS-20260919-025`；稀疏外部窗口不证明未采样区间精度，也不替代复位恢复和 VDC 有效发布。启动预检、异步保存及后续发布/恢复修复已提交，源码指纹及提交边界见进度 029。

## 未完成

### 下次接续入口与当前阻塞项

<a id="下次接续入口先释放-ram再返回特等席闭环"></a>

RAM 释放已完成，历史接续链接在此保留；实现及验收见 `VDC-PROGRESS-20260916-043`。当前按以下顺序推进，每个功能切片独立验收：

| 顺序 | 下一步 | 对应任务 / 退出条件 |
|---|---|---|
| 1 | 验证缺参考时模型保持、质量降级及恢复发布 | `VDC-SNAPSHOT-001`、`VDC-HOLD-001`；局部发布/恢复见 026–028，typed 新鲜度见 030，停更/再 ARM 见 031，Core0 诊断镜像见 032，GUARD 启动等待修复见 033，健康 LOCKED 投影见 034。下一步处理正式 aging 无效/反向读钟，再推进正式输入和逐事件保持/恢复证据。032 首轮源失效/停机链因果未证，保留原失败；补偿 HOLD 与 Domain HOLDOVER 分开判定。 |
| 2 | 保持已验证 delay，扩展恢复及输出证据 | `VDC-OUTPUT-001`、`VDC-PRECISION-001`、`VDC-DRIFT-001`；已有同配置 STOP/ARM 证据，补单板复位恢复、脉冲身份和测量误差。 |
| 3 | 验证失联、保持、恢复及旧会话退休 | `VDC-RECOVERY-001`、`VDC-HOLD-001`；区分偶发坏样本与持续失效，恢复后重新收敛。 |
| 4 | 闭合正式精度及 VDC RUN 发布 | `VDC-CAL-001`、`VDC-EVID-001`、`VDC-LOCK-001`、`VDC-RUN-001`、`VDC-VERIFY-001`；校准、同事件身份、freshness、快照及完整调度证据齐全。 |

基础调试只由 TDMA 有效运输和 DPLL 输入/采用/收敛问题阻塞。首帧对应、重复 P0T 寻优、多角色、NO5 和分段 SD 观测不作为本地主线前置；正式发布仍须闭合各自质量门禁。

### 输出、控制与确定性快速通道

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| `VDC-OUTPUT-001` | 共同未来边沿与独立输出补偿 | IN PROGRESS | 已有持续输出和 TIMER1 基线；补齐首沿/同 ordinal 对账、跨启动重复性、长期脉冲连续性及相位误差。模型和块计数不替代实际边沿。 |
| `VDC-PRECISION-001` | 实际输出相位精度 | IN PROGRESS | 依本页物理分级，核验全部有效边沿、窗口、探头/通道时延及测量不确定度；不独立平移曲线消除偏差。 |
| `VDC-DRIFT-001` | 持续频率与相位斜率收敛 | IN PROGRESS | 对账校正方向、量纲、限幅、更新间隔和实际 DCO；扩展同模型/事件窗口，短窗均频不冒充长期精度。 |
| `VDC-FAST-001` | 最小预编码同步记录方案 | IN PROGRESS | 固定字段及事件/圈身份、单位、有效性、代际、误差界可审查；生成、装载、保全、匹配、回收的 owner、期限、容量及资源预算明确。不得截宽隐藏不确定度。 |
| `VDC-FAST-002` | 确定性 TX 装载与 RX 保全 | IN PROGRESS | 依赖 `VDC-FAST-001`；普通 RX/FIFO 前保全固定记录，证明本圈送达、DMA 覆盖期限和消费能力；缺口、溢出、旧代和 STOP 取消有证据。现有交接计数不代替物理期限。 |
| `VDC-FAST-003` | Core1 同事件直接匹配与控制 | IN PROGRESS | 依赖 `VDC-FAST-002`；事件索引后复验完整身份，固定偏移解码、有界 delay/控制，无历史遍历/分片重组/Core0 等待；验证回绕、缺失、换代及 STOP/ARM，实测新增和完整 WCET。 |
| `VDC-FLIGHT-001` | NO1 时间戳持续运输 | IN PROGRESS | 由本地闭环子项落地；同轮逐从核验来源、会话、事件序号、编码字节及持续接收，其他来源不可覆盖。 |
| `VDC-FEEDBACK-001` | 接收、ACK 与本地实际采用 | IN PROGRESS | 依赖 `VDC-LOCAL-001` 至 `VDC-LOCAL-004`；收到、ACK、DCO 采用分别对账，不依赖集中式 AUTO。 |
| `VDC-LOCAL-001` | NO1 自主 PI 与事件发布 | IN PROGRESS | 参考发布已有验收；补齐自主窗口有效输入、PI/DCO 持续更新与已编码事件对账。允许后续帧携带实测事件，不要求首帧可用。 |
| `VDC-LOCAL-003` | 从板本地 delay 跟踪 | IN PROGRESS | 依赖 `VDC-LOCAL-002`，沿 `VDC-FAST-001/002/003` 收敛；三从已有真实采用，补齐持续性与生命周期门禁。使用 NO1 对应事件加有向 delay，与集中式控制互斥。 |
| `VDC-LOCAL-004` | 四板最小闭环联合验收 | PENDING | 依赖 `VDC-LOCAL-003`；同轮对账 NO1 PI/发布、三从接收/ACK/采用及 internal 斜率，不以 ACK 或诊断 LOCKED 代替物理精度。 |
| `VDC-RECOVERY-001` | 坏帧、失联和重启恢复 | IN PROGRESS | typed 新鲜度负测见 030；暂停新参考、三从恢复及清除计划后再 ARM 已实测，见 031。待补逐事件保持/恢复证据、物理失联及单板复位；旧 DMA 重复不等于物理断链，STOP 尾段超时不替代 RUN 断流证据。 |
| `VDC-SAMPLE-001` | 拒绝单次坏样本但保持可信状态 | IN PROGRESS | host/P3 拒绝分类已验收；补实板坏样本恢复和独立 clock/path 换代清理。保持 DCO、积分及有效时间锚；Ki 和持续超时 HOLDOVER 另验。 |

### 正式时间、质量与发布

本组是正式资格门禁，允许先修局部发布缺陷；完整门禁未闭合时不得提升 formal quality。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| `VDC-SNAPSHOT-001` | guarded snapshot 与一致发布 | IN PROGRESS | 完整关闭依赖 `VDC-LOCK-001`；DCO-only、age/ready 及外参 clock/DCO 发布见 026、027，健康 LOCKED 投影见 034。下一步修正正式 aging 的读钟失败/反向时间处理，避免旧 HEALTHY 保留或年龄归零；继续 quality/valid/freshness 与恢复验收。从板 STOP 无正式参考样本不算实板老化证明，stale/late/半更新仍须 fail-closed。 |
| `VDC-TDMA-001` | resident lifecycle 与固定 process image | IN PROGRESS | 依 active TDMA profile，关联 UP/DOWN、sequence/CRC、latch 与 RUNNING evidence；资源、方向、persona 无冲突，VDC 不接管运输。 |
| `VDC-CAL-001` | 有向 delay/bias 与完整路径矩阵 | IN PROGRESS | 依 TDMA profile/CRC；active matrix、table CRC、代际和 freshness 齐全，缺项拒绝。独立验证 forward-CS；provisional reverse-DATA 转置值和输出补偿不替代路径校准。 |
| `VDC-EVID-001` | 正式 DPLL evidence 准入 | IN PROGRESS | 依赖 `VDC-CAL-001`；复用既有 typed 事件身份/区间，明确合格样本接入及从调试相位 step 到正式控制的迁移（当前 step 在 LOCKED 时拒绝）。sequence/CRC/window/payload/dictionary/timestamp gate 全通过，typed FRESH 不直接升级 formal；若需要扩容特等席先用户审核。此项不阻塞已有诊断闭环实验。 |
| `VDC-TIME-002` | 自主原始计时与资源收敛 | IN PROGRESS | 依赖 `VDC-TIME-001`；所有准入配置的描述符/literal/构造上界、FIFO、记录布局、目标 RAM/link map 和取消生命周期闭合；覆盖回绕、缺边沿、旧 FIFO、错配和覆盖。已完成矩阵补测见历史进度，不以 host 地址验证代替硬件误差。 |
| `VDC-TIME-003` | 自主有效计时与丢样本验收 | PENDING | 依赖当前四板所需 `VDC-TIME-002` 交接；从预热后有效帧按 epoch/sequence/identity 对账，保留错误/恢复和边沿误差。首帧专项与其他容量不阻塞本项；STOP 后导出并核验完整性。 |
| `VDC-TIME-004` | 正式 trailer/evidence 接线 | PENDING | 依赖 `VDC-TIME-003` 和契约门禁；有效事件关联、旧数据退休、session/映射切换可证明。raw/common time/formal qualification 分开，实测真实更新预算。 |
| `VDC-SCHED-001` | DPLL 静态窗口与真实更新预算 | IN PROGRESS | 依完整静态表及自主输入；全部准入周期保留入口余量、WCET 和准入校验。按同工况实测 miss/执行/超限，不以空路径或 quick P3 通过替代严格预算。 |
| `VDC-SERVO-001` | FLL-assisted acquisition | PENDING | 依赖 `VDC-EVID-001`、`VDC-ROLE-001`；多周期 slope、受限初始 step/feed-forward、frequency sanity/slew 验收；FREQ_LOCK 只授予 coarse/tracking candidate。 |
| `VDC-SERVO-002` | 完整 Type-II PI 与调参边界 | PENDING | 依赖 `VDC-SERVO-001`、`VDC-ROLE-003`；积分、anti-windup、phase/rate slew、残差统计、配置 mailbox/回退可验证。既有局部 PI 不等于本任务完成。 |
| `VDC-LOCK-001` | coarse/tracking/formal promotion | PENDING | 依赖 `VDC-SERVO-002`；fine tier、连续有效窗口、RMS/peak/jitter、freshness、active calibration、formal timestamp 及非 provisional path 全通过。累计计数和 LOCKED 位不替代连续样本证据。 |
| `VDC-HOLD-001` | HOLDOVER、RELOCKING、FAULT | PENDING | 依赖 `VDC-SNAPSHOT-001`；aging、dispersion/drift bound、失效保持及重新 acquisition 通过。丢样本不伪造锁定，超预算禁止正式 RUN。 |
| `VDC-RUN-001` | 正式 VDC 消费门禁 | PENDING | 依赖 `VDC-LOCK-001`、`VDC-SNAPSHOT-001`、`VDC-HOLD-001`；RefMem、T2/READY、FIRE_LOAD/RUN 对 coarse/provisional/unlocked/超预算 holdover 明确拒绝。 |
| `VDC-VERIFY-001` | 汇总发布验收 | IN PROGRESS | 分阶段执行；最终关闭需 RUN、角色/配置矩阵及对应长稳。replay、故障注入、物理输出、失败和回退绑定源码；诊断、采用、内部锁定与正式锁相分别判定。 |

### 观测与长期验证

当前采用四板 SRAM 内部探针和可选示波器。NO5、分段 SD 和多角色观测属于后续扩展，其依赖只约束相应扩展完成，不要求当前四板等待额外硬件。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| `VDC-OBS-007` | 内部长期探针及可选外部复核 | IN PROGRESS | summary/GUARD 封存、十分钟和重复 STOP/ARM 联合窗口已通过，见进度 025；继续质量发布及恢复对账。完整覆盖、服务/成功最大间隔、拒绝/缺口及计数完整性分别判定；空段非零残差，零调频非故障。零 RUN 查询，尾段/退休独立核验；内部残差不替代 GPIO 精度。 |
| `VDC-OBS-001` | EDGE_TIMESTAMP 与 bounded drain | IN PROGRESS | 依赖 `SYNC-LA-003/005`；active/shadow ownership、wrap、重臂、STOP、overrun/drop 和批次交接可审计，TDMA 短帧无扰动。 |
| `VDC-OBS-002` | StorageAO 分段 SD 与恢复 | PENDING | 依赖 `VDC-OBS-001`；段号/时间基/CRC、落盘 ACK、背压、缺口、断电恢复齐全。写入不进入 Core1，背压不阻塞实时环路。 |
| `VDC-OBS-003` | 分段解码、缺口审计与 SVG | PENDING | 依赖 `VDC-OBS-002`、`SYNC-LA-006`；完整段可重放，坏段可定位，图例绑定节点/通道/边沿/时基，缺失不插值伪装。 |
| `VDC-OBS-004` | 内外同窗关联及 NO5 扩展 | PENDING | 依赖 `VDC-OBS-002/003`、`SYNC-LA-008`；共同时间锚、TDMA sequence、capture generation、segment 连续性和外部线缆边界明确。NO5 只观察，不参与控制或 promotion。 |
| `VDC-OBS-005` | 调参与观测统一记录 | PENDING | 依赖 `VDC-OBS-003`、`VDC-SERVO-002`、`VDC-ROLE-003`；原始 SCPI、requested/applied generation、profile CRC、角色/来源、残差/拒绝、debug continuation 和回退结果齐全。 |
| `VDC-OBS-006` | 分级 soak 与发布级长稳 | IN PROGRESS | 完整扩展依赖 `VDC-OBS-004/005`、`VDC-RUN-001`；各 profile 明确时长、允许缺口、恢复和退出条件，验证 SD 背压/断电续采及 TDMA 无扰动；向总验收提供结果。 |

统一观测算法目标 `VDC-OBS-ALG-001` 保留以下子阶段状态；适用于本地跟踪，集中式模式只旁路其控制、不旁路观测。

| 阶段 | 状态 | 剩余工作与退出门禁 |
|---|---|---|
| A：残差口径 | IN PROGRESS | 显式路径/delay 元数据；区分 raw、corrected、bias、jitter、频率 slope 与 command-apply。缺元数据不得声称已扣延迟。 |
| B：C 端观测 | IN PROGRESS | reference TX/local RX、source/reference、sequence、delay generation、共同时间锚和 phase-domain 可追溯；无效 raw 只诊断，正式输入 fail-closed。 |
| C：内外同窗关联 | PENDING | sequence、capture generation、segment 和传输延迟可关联；坏帧、错配、缺段及外部故障分开定位。 |
| D：扩展矩阵 | PENDING | 多主从组合、来源切换、旧/错命令、抖动及背压故障注入；当前源码 host/build/P3/HIL 和长稳证据闭合。 |

### 后续参数、角色与配置扩展

外部参考包含 MONITOR 与显式慢速补偿，代码入口 `vdc_reference.h`、`sync_io_reference.h`。
NO1 IN4 已接示波器参考输出；以下为本轮配置示例快照，非频率精度承诺：

```text
SYST:VDC:REF:CONF 4,10000000,0,1000,2500
SYST:VDC:REF:STOR
SYST:VDC:REF:ENAB 1
SYST:VDC:REF:STAT?
SYST:VDC:REF:ENAB 0
```

`CONF` 顺序为端口、标称 Hz、边沿（0 上升/1 下降）、窗口 ms、超时 ms；
参数边界见 `PRODUCT_CONFIG_VDC_REFERENCE_*`。`CONF?` 查询请求，`ENAB?` 查询使能意图；
关闭后须等 `STAT?` 的 `resource_held=0` 才能重配/保存。`DEFA` 恢复工厂 RAM 参数，
`REC` 召回保存值；上电只恢复参数，不自动使能。没有软件可调的电气阈值/幅度接口。

`STAT?` 当前 schema 的字段顺序：
`schema,config_generation,enabled,resource_held,state,reason,generation,sample_seq,valid,`
`port,nominal_hz,edge,window_ms,timeout_ms,input_pin,tick_hz,reference_cycles,`
`start_raw32,end_raw32,elapsed_ticks,pio_bias_ticks,frequency_error_ppb,measurement_flags,`
`completed_raw_hi,completed_raw_lo`。枚举与标志以 `sync_io_reference.h` 为准。
频偏为“按本地标称时钟测得的输入频率相对配置标称值”，不是本地晶振的同号误差；
DMA 仲裁延迟尚未定界，不据此宣称绝对 ppb 精度，也不自动修正 DCO。
RUN 时不轮询 SCPI 采样；硬件自主测量，STOP 后统一读回末态。

typed 来源另用 STOP-only `SYST:VDC:PRIOR:FOLL:HEAL?` 读取，字段按
`vdc_priority_follow_health_t`：schema、state、reason、request、session、generation、
event_sequence、tick_hz、age_us、freshness_limit_us、accepted、stale_transitions、
recoveries、reserved、raw_lo 低/高字、raw_hi 低/高字。state 枚举见
`VDC_PRIORITY_HEALTH_*`；reason 沿用 FOLLOW。FRESH 不等于锁定；退休后保留末态，
旧正式 quality 年龄仍是原口径。此接口供 STOP 留证，不用于 RUN 实时采样。

参考停更诊断：STOP 下先安装 `PRIOR:SYNC` generation，再配置
`SYST:VDC:PRIOR:TX:GAP <generation>,<after_ms>,<duration_ms>`；时间从本代首次
有效源观测起算。`GAP?` 读配置，`GAP:STAT?` 读 `vdc_priority_tx_gap_snapshot_t`
（宽时间字段按低/高字输出），均要求 STOP。`GAP 0,0,0` 清除计划；不保存 Flash。
旧邮箱在暂停期可重复，不能把该诊断记为物理断接测试；恢复与保持精度分别验收。

补偿调参已完成 SCPI/Flash 与四板短窗验收，见 `VDC-PROGRESS-20260919-024`。
以下为配置示例快照，依次为斜率 ppb/s、滤波分母、测量准入限幅 ppb：

```text
SYST:VDC:REF:DISC 0
SYST:VDC:REF:ENAB 0
SYST:VDC:REF:STAT?
SYST:VDC:REF:DISC:CONF 100,4,10000
SYST:VDC:REF:DISC:CONF?
SYST:VDC:REF:DISC:STOR
```

必须先 STOP，保存前确认参考资源已释放；重枚举后可查询不等于配置已准入，
拒绝需读错误并有界重试 RAM 配置，不盲重试 Flash。`DISC:DEFA/REC` 分别恢复
工厂 RAM/已保存值；`DISC:ACT?` 返回 schema、request、config_generation、
config_crc32、上述三个参数，用于核对 ARM 锁存。50 ppb/s 两分钟末态仍在追赶，
未证明稳态优于默认 100 ppb/s；默认值以 `PRODUCT_CONFIG_VDC_REFERENCE_DISCIPLINE_DEFAULT_*` 为准。

本组保留既有扩展任务及依赖；集中式命令依赖仅约束旧模式，不能反向成为当前从板本地跟踪前置。恢复旧模式或调整契约时另行审查。

节点容量以 `PROJECT_NODE_CAPACITY` 为准，Core1 周期目录以 `app_realtime_profile.c` 为准，TDMA wire 周期另按 operating profile 验证。传统 DPLL `TUNE`/`COEFficient?` 与 typed-follow 参数分开；`BASEline:DEFAult` 为工厂 RAM 值，`BASEline:RECall` 召回已保存值，旧 DPLL 默认命令语义不变。Flash 不保存积分、当前 DCO/锁状态或运行会话，资源上限仍受构建约束。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| `VDC-TUNE-002` | 按需开放其他 DPLL 参数 | IN PROGRESS | 本轮开放外参调频斜率、滤波分母及测量准入限幅的 SCPI/Flash；STOP 配置、ARM 锁存，提供采用代次/CRC，默认/召回/保存分别验收。其他参数逐组推进，不要求先全部开放。 |
| `VDC-FREQ-001` | 残余频差与外部基准补偿 | IN PROGRESS | 显式补偿、绝对基线与 PI 残差分离已实现。超时同代重试、HOLD/限速恢复已过 host；实板持续 TIMEOUT 状态与显式重新使能恢复通过，见进度 028。物理断接后同会话恢复、同启动频率对照及独立频率复核待验，不以补偿量代替实际频率精度。 |
| `VDC-ROLE-001` | Domain 控制 profile 与角色边界 | IN PROGRESS | 依 evidence 接口；角色切换清理积分/锁历史、generation 可见。旧命令 follower 旁路本地控制；当前本地跟踪显式启用，模式互斥，主机 PI 不退化。 |
| `VDC-ROLE-002` | 集中式定时 follower apply | PENDING | 暂停旧路线；依赖 `VDC-SCHED-001`、`VDC-ROLE-001`、`VDC-CMD-001` 至 `VDC-CMD-005` 完整门禁，不隐式回退本地 PI。 |
| `VDC-ROLE-003` | Flash/SCPI 角色配置 | PENDING | 既有扩展依赖 `VDC-ROLE-001/002`；任意节点可配置角色/来源，legacy 默认迁移明确，requested/applied generation 可读，Flash 仅显式保存。 |
| `VDC-ROLE-004` | 独立晶振 trim | PENDING | 既有扩展依赖 `VDC-ROLE-001/002`；capability、慢速限幅、最小间隔、stale/fault freeze 和连续 clock model 可验证。无 actuator 明示不可用；不改写 DDS phase 或提升质量。 |
| `VDC-ROLE-005` | 角色/trim 故障矩阵 | PENDING | 依赖 `VDC-ROLE-003/004`、`VDC-HOLD-001`；多主从、来源切换、错/旧/缺命令、trim fault、角色切换与回退不破坏 TDMA，各配置独立验收。 |
| `VDC-CONFIG-001` | 节点数、邮箱、周期和长帧 | PENDING | 依角色矩阵及 TDMA 准入；编译容量与运行节点数分离，STOP 配置/ARM 锁存/RUN 拒改。map/trailer/DMA 长度、schedule CRC、时间映射和预算逐配置通过，未测配置不继承。 |

### 集中式旧命令路线（暂停，未完成）

已完成的旧路线成果在“已完成”保留。以下任务不阻塞当前从板本地跟踪，暂停不等于 DONE。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| `VDC-FEEDBACK-004` | 逐从自动频率控制 | PENDING | 用户重新确认范围后恢复；同模型测量、命令、采用及精确 ACK 对账，未决先核实，不叠加校正或刷新旧命令寿命。 |
| `VDC-CMD-001` | 定时命令契约 | IN PROGRESS | 接线前闭合调度/角色门禁；来源/目标、command 与 transport 序号、source/local generation、CRC、共同时间、分片/过期/取消规则和 wire/RAM/WCET 经 Architecture、登记及 C11 审查。 |
| `VDC-CMD-002` | 跨核稳定命令与退休 | PENDING | 依赖 `VDC-CMD-001`；单写者、按源副本及 guard 覆盖发布/读/reset；Core1 有界读取，暂忙保持输出。上游旧输入、已入站 TX、重发及完整 STOP/session 退休负测通过。 |
| `VDC-CMD-003` | 固定 process image 命令接收 | PENDING | 依赖 `VDC-CMD-002`；准备/解析/重组在实时环路外，保留特等席配额；CRC、乱序/回绕、错源/目标、旧会话、STOP 及复制预算验证。收到不等于准予定时应用。 |
| `VDC-CMD-004` | 共同时间与确定边界应用 | PENDING | 依赖 `VDC-CMD-003` 及时间锚；映射建立者、身份/代际/freshness 明确，未来等待、迟到拒绝、无映射保持。跨启动、偏差、回绕、重启/STOP 负测及目标/实际时间对账通过。 |
| `VDC-CMD-005` | 一主三从命令闭环 | PENDING | 依赖 `VDC-CMD-004`；同源码四板接收/采用均推进，source/sequence/generation/effective-time 可对账；实际 DCO 采用、缺失/迟到回退及 STOP 原件完整，不单独授予正式锁相。 |

## 已完成

只列已关闭的任务范围。对应父任务、全配置覆盖和正式发布仍按“未完成”验收。

| ID | 任务 | 状态 | 完成范围与证据 |
|---|---|---|---|
| `VDC-TIMEBASE-001` | DPLL/VDC/SYNC 统一 TIMER1 坐标 | DONE | 本地时间、DCO、事件、RUN 反解及原生 schema 迁移；host、Release、四板 quick P3、两轮零补偿复采通过。TIMER0 系统语义保留，物理精度由输出任务验收；见 `VDC-PROGRESS-20260918-040/041`。 |
| `VDC-REFERENCE-002` | 外部参考 MONITOR 的 SCPI/Flash 与硬件采样 | DONE | NO1 IN4 上升/下降沿、无信号超时、取消释放、参数保存/召回/软复位恢复及四板一分钟联合观测通过，见进度 021。上电不自动使能，MONITOR 不改 DCO；绝对测量精度和基准补偿仍属于 `VDC-FREQ-001` 后续切片。 |
| `VDC-TUNE-001` | 早期基线次数/窗口配置 | DONE | STOP 设置/查询/默认/召回/显式保存、legacy/失败回退和新 FOLLOW 锁存通过；非默认保存/重启实测在 NO2，见 `VDC-PROGRESS-20260917-027`。 |
| `VDC-TUNE-003` | 独立有符号输出 delay | DONE | SCPI/Flash、STOP 应用、默认/召回/保存及 NO2 重启恢复通过；正延后、负提前，输出侧应用一次，不重复加 MATCH delay；见 `VDC-PROGRESS-20260917-028`。 |
| `VDC-LOCAL-002` | 三从基础时间戳接收与 ACK | DONE | 多次匹配 ACK 与末条 TX 原件逐字节一致；重复/旧代/STOP 及模式撤销验证。有限记录不证明每次发布必达，该历史轮完整 P3 未通过；见 `VDC-PROGRESS-20260916-030`。 |
| `VDC-RESOURCE-001` | compact RX 回收 RAM | DONE | compact DELTA 行为、目标 map、同源码四板 quick P3/短帧及 STOP 原件闭合；通用 receiver 保留。其他容量目标验收归配置任务；见 `VDC-PROGRESS-20260914-004/005/006`。 |
| `VDC-TIME-001` | 自主 DMA/PIO 时间资源审计 | DONE | 真实 builder 的容量/节点矩阵、预留 latch、TIMER1 及记录边界核验；仅关闭只读审计，不代表新增时间路径验收；见 `VDC-PROGRESS-20260914-008`。 |
| `VDC-FEEDBACK-002` | 旧路线收到命令但未应用修复 | DONE | active servo CRC 重配置丢失的反例、修复与实板采用闭合；见 `VDC-PROGRESS-20260916-016/017`。 |
| `VDC-FEEDBACK-003` | 旧路线受限单次采用与 ACK | DONE | 同命令有界重复、当轮四板 P3、三从采用/ACK 及独审通过；不代表任意丢包交付或 AUTO 完成，见 `VDC-PROGRESS-20260916-018`。 |

## 验证入口

当前联合入口及参数为已验证台架快照；适配器依赖证据目录内的配置/校准脚本，不用于任意新设备自动发现。

```powershell
python tools/vdc_priority_trace/vdc_priority_joint_capture.py --bench-adapter out/HardwareAcceptance/20260919/internal-seal-r1/capture.py --scope on --scope-trigger CHAN1 --output-delays 0 -8 -68 -116 --seconds 60 --out out/HardwareAcceptance/20260919/joint-next
```

- 使用新证据目录；`--seconds` 选择支持的整分钟窗口，具体范围见工具 `parse_args()`。长窗不拼接重启段。
- `--scope off` 仅内部 GUARD，外部为 SKIPPED；`on` 每五秒新触发短窗、每分钟判定，规则见 `scope_checkpoint()`。漏采、超时或超相位门限按当前分钟失败收尾，不能静默降级通过。
- CH1–CH4 对应 NO1–NO4、1× 探头；当前 RUN 仅 OUT1 有脉冲，默认 CHAN1。OUT4 虽接 EXT，只有确认其实际驱动后才能选 EXT。
- `--output-delays` 显式选择已验证候选；STOP 后应用/读回，结束恢复，不自动写 Flash；省略仍用工具旧基线。路径 delay 与 output delay 分开。
- 板端在 STOP 配置新会话、ARM/ACK 后再启动，运行不轮询；有界 SRAM 记录，全部 STOP 后冻结、分页、CRC 和 RELEASE。封存之后不外推参考覆盖，输出尾段单独核验。
- 内部 GUARD 可在失败时请求本板 STOP，目标 PASS 不停止输出；零查询模式不承诺主机即时获知或全板同时停止。内部与外部各自判定，不宣称严格同事件配对。
- 内置 RRDELay 尚不替代 RAW；无效大数和 timeout 保留。采样批量保存降低写盘成本，异常保留部分证据；强杀造成的不完整记录不能通过。

## 外部参考调试入口

仅在 STOP 后依次配置 `SYST:VDC:REF:CONF port,hz,edge,window_ms,timeout_ms`、
`SYST:VDC:REF:ENAB 1`，需要补偿时另发 `SYST:VDC:REF:DISC 1`。
`CONF?`/`STAT?` 保留测量读回；`DISC?` 独立返回补偿状态，RUN 期间不作串口采样。
参数由 `STOR` 显式保存；补偿使能不写 Flash，每轮须重新授权。
`DISC 0` 或 `ENAB 0` 停止新增补偿但保留最后已应用基线，不能理解为频偏清零；
新 TDMA 配置激活重建模型。限幅、滤波和调频斜率通过 `DISC:CONF` 配置，
经 `DISC:STOR` 显式保存；默认值与 ARM 锁存读回见上文“后续参数、角色与配置扩展”。

`DISC?` 字段顺序以 `vdc_reference_discipline_status_t`/SCPI callback 为准：
schema、已确认 request、当前 requested enable、state/reason、reference generation、sample sequence、
accepted/rejected/applied、measured/filtered/baseline ppb、session、role generation、clock epoch/run、
origin epoch/sequence、DCO update sequence。禁用后保留历史计数用于 STOP 对账，不能当作仍在调节。

## HAOFV owner 与执行约束

| Owner | 边界 |
|---|---|
| STATE_MACHINE / SYNC_IO | PIO、SM、DMA、FIFO、GPIO、IRQ 资源与生命周期；IN/OUT 统一归 SYNC_IO，硬件执行已提交边沿。 |
| TDMA Foundation | process image、UP/DOWN、sequence/CRC、硬件事件和同步记录运输；VDC 不抢读 DMA 的 PIO FIFO，不增加普通 FIFO 的第二 consumer。 |
| Calibration | 测量并维护有向 delay/bias、路径矩阵和校准代际/freshness；不得用默认零值、沿环临时累加或 scope 偏差反推正式路径。 |
| VdcSyncAO | 绑定 profile/dictionary/calibration，负责 evidence 准入和同步动作，不代替 Calibration 测量。 |
| Core1 / SyncDpllFB | 唯一实时 PI/DCO 写者；固定编码/事件索引匹配、有界控制和后缀准备，无 RTOS、动态分配、SD、SCPI 或 Core0 等待。 |
| VdcQualityGateFB / VdcVector | quality、promotion 及 guarded snapshot；Core0/RefMem/Trigger 只消费，不反写控制状态。 |
| Core0 / System / StorageAO | 配置、mailbox、显式 Flash 保存、低频诊断及导出；Flash 仅 Core0 写且 Core1 park/ACK。 |

特等席为 DPLL/VDC/SYNC 最小同步记录；其余 VDC、RefMem、控制/log 分别为一等、二等、无座。普通解析和分片不是特等席前置。事件序号直接索引后仍须核对完整身份；时间分辨率不等于物理精度，运输圈与所载事件不能混同。

新模型只作用于未提交输出；源端已编码事件不随后续模型回算。首帧可无效，稳态有效帧即可推进，坏样本跳过但须记录；DPLL 局部拒绝或超限不隔离健康 TDMA。

每项实现遵循 host → Release/资源 → 同源码固定四板 quick P3 → 专项原件复核 → 代码/文档分离提交。P3 基础范围不随调试扩张；失败、forced continue 和旧凭证不得冒充严格通过。复用已确认线序，OTA 实现保持现状。调试参数按 `DOCS_EXECUTION_CONSTRAINTS.md` 接受可解析值并留证，实时算术仍饱和防溢出，格式/资源错误不得绕过。

## 迁移阶段门禁与统一完成定义

| 阶段 | 进入与退出条件 |
|---|---|
| M0：资源/运输 | profile 和资源契约明确；STOPPED→STAGED→ARMED→RESIDENT_INIT→RUNNING 及圈边界装卸/转发通过，completion 不终止 resident loop，不靠 host 续装。 |
| M1：校准/evidence | cycle sequence/CRC 可关联；完整有向矩阵、dictionary、正式 timestamp 和 freshness 通过，软件计时不提升 DPLL_ELIGIBLE。 |
| M2：控制/锁定 | 正式 evidence 持续有效，FLL/PI 与 promotion 分别验收；本地跟踪不依赖旧 CMD 路线，集中式模式若恢复则独立完成定时命令门禁。 |
| M3：发布/恢复 | snapshot guard、quality aging、HOLDOVER/RELOCKING/FAULT、代际切换和正式 RUN 拒绝门禁可重复验证。 |

完成需同时具备：真实运输与持续控制、声明窗口内物理输出精度、有效校准/时间资格、一致发布、失效恢复及对应配置的资源/WCET/静态表证据。internal、基础 P3、示波器和正式质量各自证明其范围，不互相替代；失败回退到已验证状态并保留原件。
