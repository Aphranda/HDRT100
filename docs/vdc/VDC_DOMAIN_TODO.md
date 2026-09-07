# VDC 内部主域待办

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_DOMAIN_TODO.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/sync/SYNC_IO_TODO.md`, `docs/sync/SYNC_IO_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TODO.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`
Last updated: 2026-09-07

本文只维护当前 VDC 架构迁移的任务、依赖和退出门禁。稳定语义见 Architecture，实施证据
见 Task Progress，重构前内容已归档到 `docs/legacy/vdc/`。

## 状态规则

任务状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`。

- `DONE`：代码/文档、必要构建和对应硬件/集成门禁均已闭合。
- `IN PROGRESS`：当前唯一允许推进的任务切片，或已有实现但证据未闭合。
- `PENDING`：依赖尚未完成，不得提前实现或验收。
- `BLOCKED`：有明确外部阻塞、失败证据和下一解除条件。

构建号、板端计数、replay 结果和 HIL 路径只进入 `VDC_TASK_PROGRESS.md`，不改变任务语义。

## P0 优先主线：长期观测与闭环证据

长期观测基础设施提升为当前 VDC 的 P0 主线。它服务于 DPLL 调参、拒绝定位和
`FORMAL_LOCKED` 的证据闭环，但不改变正式锁相的前置条件：不得用诊断采样、NO5
外部观测或 `LOCKED` 状态替代 TDMA/Calibration/formal timestamp evidence。

目标数据链路固定为：

```text
PIO/DMA EDGE_TIMESTAMP producer
  -> bounded Core1/SRAM producer
  -> Core0 bounded drain
  -> StorageAO segmented SD writer
  -> decoder/drop-interval/SVG analysis
  -> NO1-NO4 internal DPLL + NO5 external same-window correlation
  -> SCPI parameter tuning and serial feedback
  -> convergence/formal-lock decision
```

长期运行必须满足以下不变量：

- 采集、排空和 SD 写入均分段且可恢复；每段带 capture/segment sequence、硬件时间基、
  CRC、produced/consumed/dropped/overrun 计数和结束原因。
- SD 背压不得阻塞 TDMA、process-image、FIFO 或 DPLL 实时服务；不能假装连续，必须将
  drop interval、segment gap 和恢复点写入证据。
- `EDGE_TIMESTAMP` 只保存边沿上下文；实时 phase decoder 仍消费全部必要采样字，观测
  压缩不得改变 DPLL 输入或控制路径。
- NO1--NO4 内部 DPLL 是收敛判定的主要数据源，NO5 只做外部只读相关观测；二者必须
  具备同窗、sequence/time anchor 和数据完整性标记。
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
| `VDC-OBS-005` | 将 SCPI 调参、串口闭环状态、residual/frequency/reject/lock feedback 与分段观测统一记录，支持小步搜索、等待稳定窗口、评分、回退和参数 generation 对账。 | PENDING | `VDC-OBS-003`, `VDC-SERVO-002` | requested/applied generation、active profile CRC、原始命令、状态读回和回退结果齐全；异常参数在 debug profile 留证，不自动宣称 formal lock。 |
| `VDC-OBS-006` | 建立分级长期 soak 与发布验收：短时调试、工程长稳、发布级长稳均使用同一 segment/decoder/关联格式，并验证断电续采、SD 背压和 TDMA 无扰动。 | PENDING | `VDC-OBS-004`, `VDC-OBS-005`, `VDC-VERIFY-001` | 各级验收 profile 明确采样时长、允许/禁止的 drop、恢复点和退出条件；原始证据、失败事实和回退点完整，才可评估 `FORMAL_LOCKED`。 |

P0 主线不得跳过 `VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001` 的正式门禁；在正式
evidence 未闭环前，观测与调参结果只能标记为诊断或 tracking candidate。P0 观测任务完成
后，才允许用长时间数据评估 `VDC-SERVO-001/002`、`VDC-LOCK-001` 和最终 RUN gate。

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

后续只能按下面顺序迁移。前置任务的退出门禁未闭合时，后续任务保持 `PENDING`。

| 顺序 | Task ID | 任务 | Owner | 状态 | 依赖 | 退出门禁 |
|---:|---|---|---|---|---|---|
| 1 | `VDC-TDMA-001` | 对接 STATE_MACHINE 的 resident lifecycle 和 TDMA Foundation 的固定 process image、UP/DOWN cycle、sequence/CRC、hardware latch。 | TDMA Foundation + STATE_MACHINE | IN PROGRESS | active TDMA profile | `RUNNING` cycle evidence 连续有效；资源/方向/persona 无冲突；VDC 不拥有 transport。 |
| 2 | `VDC-CAL-001` | 导入 active path delay、bias、generation/freshness 和完整 observation matrix。 | Calibration + VdcSyncAO | IN PROGRESS | `VDC-TDMA-001` profile/CRC | matrix 完整、table CRC/generation/freshness 通过；缺项 fail-closed。 |
| 3 | `VDC-EVID-001` | 将同圈 latch descriptor 展开为正式 DPLL evidence。 | VdcSyncAO + Timestamp service | IN PROGRESS | `VDC-CAL-001` | sequence/CRC/window/payload/dictionary/timestamp gate 全通过；diagnostic-only 不得 formal。 |
| 4 | `VDC-SERVO-001` | 实现 FLL-assisted acquisition：多周期 phase slope、受限初始 step/feed-forward、frequency sanity/slew。 | SyncDpllFB | PENDING | `VDC-EVID-001` 连续合格样本 | 进入 `FREQ_LOCK`，只发布 coarse/tracking candidate。 |
| 5 | `VDC-SERVO-002` | 实现二阶 Type-II PI tracking：`kp_q16/ki_q16`、显式积分状态、anti-windup、phase/rate slew、input residual 统计，以及 debug SCPI generation/mailbox 和自动回退工具。 | SyncDpllFB + System/maintenance | PENDING | `VDC-SERVO-001` | `PHASE_LOCK` 稳定，DCO snapshot 完整可消费；debug 异常参数不被数值范围门禁拒绝，格式/资源错误仍留证。 |
| 6 | `VDC-LOCK-001` | 建立粗锁、tracking candidate、formal lock promotion gate。 | VdcQualityGateFB + VdcSyncAO | PENDING | `VDC-SERVO-002` | fine tier、连续窗口、RMS/peak/jitter、freshness、active calibration、formal timestamp 和非 provisional path 全通过。 |
| 7 | `VDC-SNAPSHOT-001` | 重建 VdcVector/DCO guarded snapshot，接入 core1 stable read。 | VdcVector + core1 realtime | PENDING | `VDC-LOCK-001` | seqlock/双缓冲/等价 guard 通过；stale/late/半更新 fail-closed。 |
| 8 | `VDC-HOLD-001` | 实现 HOLDOVER aging、dispersion/drift bound、RELOCKING 和 FAULT。 | VdcSyncAO + VdcQualityGateFB | PENDING | `VDC-SNAPSHOT-001` | 丢样本不伪造锁；超预算禁止 RUN；恢复重新 acquisition。 |
| 9 | `VDC-RUN-001` | 将 formal VDC gate 接入 RefMem mirror、T2/READY、FIRE_LOAD/RUN。 | Trigger + RefMem + SystemManager | PENDING | `VDC-LOCK-001`, `VDC-SNAPSHOT-001`, `VDC-HOLD-001` | coarse/provisional/holdover 超预算/unlocked 全部拒绝正式 FIRE_LOAD。 |
| 10 | `VDC-VERIFY-001` | 完成 replay、故障注入、两板/四板 HIL、NO5 只读观测和长稳报告。 | 主控验收 | IN PROGRESS | 前九项按阶段开放 | 诊断证据与正式硬件验收分离；每个失败保留原始证据和回退点。 |

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

- 入口：正式 evidence 连续有效。
- 粗锁：FLL-assisted acquisition 进入 `FREQ_LOCK`，只用于 bring-up/tracking candidate。
- 目标锁：Type-II PI tracking 满足 formal promotion，才允许 `FORMAL_LOCKED`。
- 禁止：用 state=`LOCKED`、累计 sample count、单向 leg 或 replay passed 替代 formal lock。

### M3：snapshot、故障恢复与 RUN

- 入口：formal promotion 可重复产生，DCO snapshot guard 已闭合。
- 必须完成：HOLDOVER aging、RELOCKING、FAULT、source/calibration generation 变化和 RUN gate。
- 禁止：RefMem/SCPI/Trigger 反写 VDC owner；holdover 超预算继续 FIRE_LOAD。

## 当前阻塞与统一完成定义

当前主线阻塞集中在 `VDC-TDMA-001`/`VDC-EVID-001` 的正式 hardware-latch、path matrix
和同圈 evidence 闭环；在它们完成前，`VDC-SERVO-001/002` 只能做隔离 replay 和 host
单测，不能用来宣称板端目标锁。

VDC 迁移完成必须同时满足：

- HAOFV owner 边界和 STATE_MACHINE/TDMA resident lifecycle 不被破坏；
- host/build、资源/状态机门禁和必要 OTA/HIL 通过；
- active Calibration/path matrix、formal timestamp evidence、FLL/PI、promotion、snapshot 和 failure recovery 全链路可追溯；
- coarse lock、formal lock、HOLDOVER、RELOCKING、FAULT 语义在 snapshot/SCPI/report 中分离；
- 失败时回退到最近已验证状态，不以旧复合路径、旧 receipt 或旧 build 掩盖缺口。
