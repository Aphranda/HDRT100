# TDMA 基础件主域待办

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_TODO.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-09-19

## 文档接口与状态规则

Architecture 维护 owner 与稳定语义，运行细则维护代码接口与预算读法，本文维护任务状态，Task Progress 保存原件、失败和验收。旧清单见[历史快照](../legacy/tdma/LEGACY_TDMA_DOMAIN_TODO.md)，原 task ID 保留。

状态限 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`。已接线或局部实测可计进行中，完整退出门禁满足才为 DONE；父任务不因子切片通过而关闭。代码与实测更新状态，文档重排不改变契约登记。

## 已有基线

| 能力 | 已有范围 | 证据入口 |
|---|---|---|
| 四板基本运输 | 固定 SHORT、process follower、局部 overlay、普通/自主路径及固定范围 quick P3。 | TDMA 进展；最新同源码凭证见 `VDC-PROGRESS-20260919-024`。 |
| 异步候车平台 | 普通 TX/RX/overlay 及 origin 构图有固定 Core0 工位，Core1 保留采用/退休/取消。 | `TDMA-PROGRESS-20260914-011/012`；012 候选已撤回，不计生产收益。 |
| 节点与周期 | 编译容量、STOP 后选择邮箱、ARM 冻结及整表周期请求/确认已实现。 | `TDMA-PROGRESS-20260913-062/063`、`20260914-013`。 |
| 同步快速通道 | typed 固定邮箱、Core1 TX/provider、RX IRQ 保全及 VDC 本地 MATCH/FOLLOW 已实际工作。 | VDC 的 `VDC-FAST-*`、`VDC-LOCAL-*` 及进展。 |
| 观测与时间 | observer 回绕修复、统一 TIMER1、内部探针和可选示波器长窗已有证据。 | `TDMA-PROGRESS-20260917-001`；VDC 进展 `20260918-040/041`、`20260919-018/020`。 |

当前以四板验证，NO5 为可选扩展。quick P3 的 PASS_WITH_WARNINGS 与原始严格失败字段并存，不能写成完整 WCET、零缺帧或 resident 全窗口通过。DPLL 已有真实闭环和物理输出；正式质量、恢复及 VDC 发布仍由 VDC 待办管理。

## 当前主线

优先收敛 **实时预算与飞行处理**，保留已工作的特等席。预算以当前 `PROJECT_CORE1_*`、`app_realtime_profile_*` 和实际 profile generation 为准；历史 380/500/850 us 仅是对应轮次的门限，不作为现行统一判据。

| 顺序 | 工作 | 可评审的退出证据 |
|---|---|---|
| 1 | 完整 TDMA phase 与 IRQ 预算核对 | 同源码/同配置的前台、IRQ、关窗、skip/late、RUN/RESET/OTHER 和物理进展分别对账。 |
| 2 | 异步准备与自主运行 | no-update 复用、selection 退休、迟到更新、STOP 取消及主从 blackout；同圈 overlay 不破坏节拍。 |
| 3 | 特等席期限与负载隔离 | 事件生成到实际送达/采用的身份、最长间隔、覆盖/缺口及饱和负测；不依赖普通解析。 |
| 4 | 产品化扩展 | 资源/WCET、全拓扑、长档运行、recovery/fence 和多板 T2 分别验收。 |

首帧唯一身份、首次发车及首窗口诊断继续作为 TDMA 精细优化，不是 DPLL 使用后续稳态有效样本的前置。只有具体 TDMA 缺陷已破坏有效输入时，才把对应修复调回当前切片；不重开全部历史校准与启动证明。

## 未完成

### 里程碑总览

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-M6 | resident process image flight | IN PROGRESS | 自主全窗口、同圈装卸、无更新透传、受控退出及完整预算；不是只有周期可配置。 |
| TDMA-M3 | DPLL/VDC 最小负载 | IN PROGRESS | typed 运输和本地控制已有，补齐期限、正式 evidence 与 VDC 一致发布。 |
| TDMA-M4 | completion/reliability | PENDING | ACK/fence/retry/fail-closed 与长期错误率完整验证。 |
| TDMA-M5 | T2 reservation 与控制 | PENDING | 当前四板预约、READY/fence/completion 集成；其他节点规模另验。 |

### 实时预算、飞行与异步准备

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-FLIGHT-002 | 真飞行处理总目标 | IN PROGRESS | F1–F5、owner/resource/snapshot、完整 CPU/RAM 和物理周期映射；契约激活须 C11。 |
| TDMA-FLIGHT-001 | 常驻映像与同圈多 Node overlay | IN PROGRESS | 一次初始化、持续 cycle、授权装卸、无更新透传和受控退出。 |
| TDMA-FLIGHT-002B | 自主发车与硬件进展 | IN PROGRESS | 已有 origin 有限 blackout；补主从全窗口、物理节拍、切换/故障及最坏预算。 |
| TDMA-FLIGHT-002C | 双缓冲与无更新稳态 | PENDING | 原子选择、旧槽退休与复用波形一致；无更新不靠 Core1 改写线行为。 |
| TDMA-FLIGHT-002D | 非阻塞 overlay 与同圈装卸 | PENDING | 就绪采用、迟到顺延、逐 hop owner/CRC 与节拍不变的独立证据。 |
| TDMA-FLIGHT-002E | byte/cycle 飞行声明与契约收敛 | PENDING | F1–F5 完成后独立 C11、registry 及顶层刷新；现有 pending 不自动提升。 |
| TDMA-FLIGHT-002F | TX/RX 异步候车平台 | IN PROGRESS | 已有后台准备；补完整预算、峰值归因、拥塞、迟到复用、selection 退休与 STOP 取消。 |
| TDMA-FLIGHT-003 | 发车节拍下界与准入 | PENDING | 依据物理计划与 Calibration 输入给出可达下界；过快配置拒绝及只读证据齐全。 |
| TDMA-DET-004 | prepare/preload/launch/wire/feedback 分解 | PENDING | 普通与自主路径分别定义子阶段、硬件边界与拍级合同。 |
| TDMA-DET-005 | topology/baud/tail 动态容量门禁 | PENDING | 激活前完整 wire 与资源预算复核，超预算 fail closed。 |
| TDMA-FLIGHT-006 | byte 边界时序与准入 | IN PROGRESS | 现有模型补最坏 DMA 仲裁/断粮、环境与严格校准。 |
| TDMA-FLIGHT-007 | 非字节对齐 overlay owner 保护 | IN PROGRESS | 现有逐 hop 基线补 generation handoff、自主同圈与供给压力。 |
| TDMA-FLIGHT-002I | 编译容量与运行时布局 | IN PROGRESS | 两层配置已实现；补全拓扑/混合容量、资源和完整预算；减在线节点不等于释放静态 RAM。 |
| TDMA-HIL-001 | 四板时序与物理基线 | IN PROGRESS | 已有 P3/波形；补当前 profile 完整 WCET、频率/占空比、原始证据和零错误全窗口。 |
| TDMA-HIL-002 | 逐 phase 加载无回归 | PENDING | 分别启用 VDC/DPLL/RefMem/control，保留 TDMA deadline/error、IRQ 及最坏重叠。 |

### 特等席与普通载荷

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-FLIGHT-002G | 同步特等席逐圈交付 | IN PROGRESS | typed TX、独立 RX 保全与 Core1 消费已有；补每圈/最长间隔、覆盖期限、溢出/旧代/STOP 和时钟映射。 |
| TDMA-FLIGHT-002H | 四级服务与固定配额 | PENDING | 普通流饱和不破坏特等期限与一等 freshness，RefMem 可靠确认、无座有界限流。 |
| TDMA-PAYLOAD-002 | 同步记录与 compact 输出 | IN PROGRESS | 区分 typed body、普通 Node body 和兼容 trailer；完整运输身份、正式资格及共存通过。 |
| TDMA-PAYLOAD-003 | critical RefMem 与 ACK/fence | IN PROGRESS | 基础 delta/ACK 已运输；补业务 commit/fence 与失败恢复。 |
| TDMA-PAYLOAD-004 | 最小控制 token | IN PROGRESS | 已预留布局；补 owner、opcode、deadline 与 completion。 |
| TDMA-PAYLOAD-006 | 实时诊断压缩与隔离 | IN PROGRESS | 只保留基础计数/身份；原始波形、SD/SVG 和详细归因不侵入实时 payload。 |

### DPLL 交接与观测

算法、精度和正式 VDC 发布的唯一任务源为 [VDC TODO](../vdc/VDC_DOMAIN_TODO.md)；下列旧 ID 保留 TDMA 接口责任，不再维护第二套独立锁相路线。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-DPLL-001 | 硬件事件与运输关联 | IN PROGRESS | typed/原始事件和兼容 trailer 已接线；补 active path 身份、换代、回绕与失配 HIL。 |
| TDMA-DPLL-002 | 本地闭环与 VDC 发布交接 | IN PROGRESS | 四板真实采用已有；跟随 VDC 的持续性、formal quality 与发布任务，不强制 NO5。 |
| TDMA-DPLL-003 | 正式 active calibration matrix | PENDING | 有向 path/offset、generation/freshness 与拓扑完整一致；运行态不按环序猜测或累加 fallback。 |
| TDMA-DPLL-004 | timestamp spine 与校准 | IN PROGRESS | TIMER1/observer/history 已有；补 CS/SCK/DATA 完整误差界、独立 SCK 与负测。 |
| TDMA-DPLL-005 | eligible evidence 准入 | IN PROGRESS | typed CRC/身份/MATCH 已有；正式校准和质量门禁另验，坏样本拒绝并留缺口而非阻断后续有效样本。 |
| TDMA-DPLL-006 | Core1 servo 与 compact 发布 | IN PROGRESS | MATCH/FOLLOW/delay/DCO 已工作；补完整锁定/保持、质量发布与 WCET。 |
| TDMA-DPLL-007 | 故障、holdover、relock | PENDING | 单链路/旧代/无效输入/recovery 注入有明确行为，恢复后重新准入，TDMA 无回归。 |
| TDMA-DPLL-008 | 原生记录与离线分析 | IN PROGRESS | SRAM/STOP 导出/SVG 已有；补长期分段/保存恢复，SD 和 NO5 非每轮必需。 |
| TDMA-DPLL-009 | 启动 profile 与持久化 | IN PROGRESS | 已有多组 SCPI/Flash/重启证据；补完整启动 profile，默认 RAM 迁移与显式 STOR 分开。 |
| TDMA-HIL-003 | 四板内部/外部观测 | IN PROGRESS | 内部探针及可选示波器已有联合长窗；补正式精度、生命周期与运输期限资格。 |

### 可靠性与后续集成

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-REL-001 | ACK/fence/retry 策略 | PENDING | 原始错误率、可靠交付、背压和明确失败分开验收。 |
| TDMA-REL-002 | 原 Node recovery 双 buffer | IN PROGRESS | 固定 offset/预算已有；补 ACK、retry exhaustion、背压和多拓扑故障 HIL。 |
| TDMA-T2-001 | T2 最小载荷预算 | PENDING | 固定 SHORT/body 与 phase 不超限；主线四板预约集成另验，单板分支结果不代替。 |
| TDMA-QUALITY-001 | 逐流质量与背压 | PENDING | per-class、超预算及质量发布闭合。 |
| TDMA-MAP-001 | 正式 System Pack map | PENDING | owner/generation、active/shadow 在明确 cycle 边界切换。 |
| TDMA-ADAPTER-001 | PIO SPI 两级 flight gate | PENDING | byte/cycle 分级证据、完整性、FIFO 和 map apply 全范围验证。 |
| TDMA-ADAPTER-002 | BISS-C/UART/RS485 adapter | PENDING | MTU、latency、timeout、资源与质量接口及真实承载验证。 |
| TDMA-BULK-001 | bulk 与 critical 配额 | PENDING | 显式维护窗口、准入/背压，不侵占 Core1/guard；OTA 实现不纳入本次整理。 |
| TDMA-REL-003 | 多环消重 | PENDING | 多路径方案冻结后验证 sequence/replication/duplicate elimination。 |
| TDMA-BOARD-001 | 产品 IO 与硬件资格 | PENDING | 对应产品 IO、单跳及多板完整 HIL。 |

### 启动精细优化与训练承接

保留原 P0–P7/P0.5 未完成范围；Calibration 已有可用训练输入不等于以下所有生命周期和故障门禁已完成。兼容线序复用、四板基础验收和 DPLL 稳态推进不等待这些项目全部关闭。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-REFINE-001 | 首次发车与首描述符 | PENDING | 实际 builder/原件对应，首失败不被后成功替换；不要求 DPLL 使用首帧。 |
| TDMA-REFINE-002 | capture/sequence/control 偏移 | PENDING | 分频、stall 与采样时差分解，以误差预算确定收益。 |
| TDMA-REFINE-003 | 首 RX 窗口身份 | PENDING | 不跨 CS 拼接的独立诊断，后续有效帧正常消费。 |
| TDMA-TRAIN-001 | RX 扫描诊断 | PENDING | candidate/idle/real magic 分类可追溯。 |
| TDMA-TRAIN-002 | 训练状态机与 coded persona | PENDING | owner、epoch/seq、资源及有界恢复。 |
| TDMA-TRAIN-003 | 校准投影与 TRAIN frame | PENDING | snapshot/freshness、ACK/commit 及原始拒绝点。 |
| TDMA-TRAIN-004 | 拓扑/窗口/evidence | PENDING | active topology/generation/freshness 接入 schedule。 |
| TDMA-TRAIN-005 | 编排、故障与恢复 | PENDING | STOP→APPLY→ARM→TRAIN→restore→STOP 全流程可复核。 |

## 已完成

以下保留原已验收范围；它们不覆盖新增 profile、最坏工况或当前未完成父任务。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-M1 | 拍级 schedule 与编译门禁 | DONE | 静态表、phase/WCET 结构和四板基线已建立；当前完整实测由 HIL-001 继续收敛。 |
| TDMA-M2 | mandatory-first SHORT | DONE | 原固定布局、publisher/parser、CRC、SCPI 与多板基线齐全；typed 共存另验。 |
| TDMA-DET-001 | 拍级表与唯一单位 | DONE | `APP_REALTIME_PHASE_TABLE`、邻接及周期闭合检查。 |
| TDMA-DET-002 | schedule/wire 编译门禁 | DONE | phase、WCET、SPI 与最大 SHORT wire 构建拒绝。 |
| TDMA-DET-003 | 拍级 runtime/SCPI | DONE | 既有 runtime/SCPI 多板证据归档；新周期运行不在原范围。 |
| TDMA-PAYLOAD-001 | mandatory body 固定布局 | DONE | layout、编译断言与预算工具一致。 |
| TDMA-PAYLOAD-005 | optional 静态准入 | DONE | 仅使用 mandatory 后的固定余量，无 runtime-free 字节。 |
| TDMA-FLIGHT-002A | byte-level RX/TX 重叠 | DONE | 当前夹具固定 pipeline 取证，见 `TDMA-PROGRESS-20260911-006`；PIO/profile 变更复验。 |
| TDMA-FLIGHT-004 | 四板回归归因与恢复 | DONE | `TDMA-PROGRESS-20260911-003`，不外推电气根因。 |
| TDMA-FLIGHT-005 | follower PIO 补丁下标 | DONE | public label 绑定、变异与多板验证，见 `TDMA-PROGRESS-20260911-004`。 |

## 当前阻塞项

| 验收目标 | 尚缺的关键证据 | 对现有主线的影响 |
|---|---|---|
| 严格实时与 resident 完整资格 | 当前完整 WCET、主从 blackout、同圈多节点更新、切换/拥塞/恢复、F1–F5。 | 不能宣布全配置真飞行完成；不否定已工作的四板运输。 |
| 特等席确定性交付 | 实际逐圈/最长更新间隔、覆盖上界、最坏 IRQ/前台与普通流隔离。 | 不能由固定 FIFO 或单次锁相直接推出无损期限。 |
| 正式 VDC/可靠业务发布 | calibration/quality、ACK/fence、正式 map 与 T2 集成。 | 按所属域任务推进，不重建 NO5 或首帧前置。 |
| 架构偏差关闭 | `TDMA-RESIDENT-01` C11 与 `HAOFV-879` 全写者/读者审核。 | 保留 registry 状态；局部 guard 实现不自动关闭条款。 |

## 统一完成定义

每项先固定配置、目标与退出门禁，再核对 owner/状态/负测及相应原件。实现改动按 host → Release/资源 → 同源码四板 quick P3 → 所需专项 → 复核执行；P3 基础范围不随调试扩张。纯文档切片走文档门禁。

SD、外部示波器或其他夹具按专项需要选择；证据须完整可追溯，但不要求每个任务都重复全部夹具。quick P3、物理精度、resident 连续性与正式产品质量分别关闭。失败/回退保留原件，不用新预算、较晚成功窗口或报告改写旧失败。详细证据统一查 [Task Progress](TDMA_TASK_PROGRESS.md) 及其归档索引。
