# TDMA 基础件主域待办

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_TODO.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_TODO.md`
Last updated: 2026-09-15

本文档维护 TDMA foundation 的独立待办。这里记录影响上/下行 TDMA、ring runtime、payload registry、adapter、completion、quality、HAOFV system node 和 HIL 验收的事项。

## 文档接口与状态规则

- Architecture：`TDMA_DOMAIN_ARCHITECTURE.md`，稳定语义、owner 边界和跨域契约。
- TODO（本文）：稳定 task ID、状态和完成/退出门禁。
- Task Progress：`TDMA_TASK_PROGRESS.md`，按日期追加构建、测试、OTA/HIL、失败和回退证据。
- 状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`；未完成 OTA/HIL 不得标为 `DONE`。

## 当前基线

- TDMA owner、ring runtime、traffic scheduler、双 FIFO、固定 process image、raw-flight persona 和异步准备基线已存在。
- 1.5 ms 默认档事实源为 `PROJECT_CORE1_PROFILE_1500US_CYCLES` 与 `PROJECT_CORE1_CYCLE_CYCLES`。
- 周期配置、STOP/config ACK、Core1 全表安装、generation 确认、ARM 冻结、四档读回、软件回归、A/B/Boot、PIO 一致性和四板 quick diagnostic 已完成，见 `TDMA-PROGRESS-20260914-013`。
- 自主全窗口、无更新稳态、service blackout、同圈多 Node 更新、完整 WCET/RAM、逐圈特等席、长档循环和 VDC/DPLL eligible 闭环仍未完成。

## 里程碑总览

当前主线仍是 `TDMA-FLIGHT-002`：先闭合硬件自主发车、异步准备、有界装卸和受控
停止，再接入四级负载。owner、特等席逐圈时间戳和固定预算语义以
`TDMA_DOMAIN_ARCHITECTURE.md` 为准；单次普通四板 P3 不关闭自主全窗口或 DPLL 锁相。

| ID | 里程碑 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-M1 | 拍级 schedule 与编译门禁 | DONE | 静态 gate、phase/WCET 和四节点基线齐全。 |
| TDMA-M2 | mandatory-first SHORT process image | DONE | 固定布局、publisher/parser、CRC、SCPI 与多板 HIL 齐全。 |
| TDMA-M3 | DPLL/VDC 最小负载 | IN PROGRESS | hardware latch、eligible sample、锁相和 VDC 发布待完成。 |
| TDMA-M4 | completion/reliability | PENDING | ACK/fence/retry/fail-closed 与长期错误率待完成。 |
| TDMA-M5 | T2 reservation 与控制 | PENDING | PREPARE/READY/fence/completion 五板闭环待完成。 |
| TDMA-M6 | resident process image flight | IN PROGRESS | 1.5 ms 配置已闭环；resident 全窗口、无更新透传、同圈 overlay 和受控退出待完成。 |

## 当前任务表

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-DET-001 | 拍级 phase table 与唯一时间单位 | DONE | `APP_REALTIME_PHASE_TABLE` 和编译期邻接/周期闭合检查存在。 |
| TDMA-DET-002 | schedule 与最大 wire 编译门禁 | DONE | phase、WCET、SPI 整拍和最大 SHORT wire 超限均拒绝构建。 |
| TDMA-DET-003 | 拍级 runtime/SCPI evidence | DONE | 拍级 runtime/SCPI 与多板 HIL 证据已归档；新周期完整验收见 TDMA-HIL-001。 |
| TDMA-DET-004 | 拆分 prepare/preload/hardware-launch/wire/feedback | PENDING | 首边沿由 PIO/硬件事件触发，各子 phase 有独立拍级合同。 |
| TDMA-DET-005 | active topology/baud/tail 动态容量门禁 | PENDING | profile 激活前重算 wire，超出 TDMA WCET fail closed。 |
| TDMA-PAYLOAD-001 | mandatory-first Node body 预算与固定布局 | DONE | `tdma_process_image_layout.h`、编译断言和预算工具一致。 |
| TDMA-PAYLOAD-002 | 固定 DPLL observation 与 compact VDC/DPLL output | IN PROGRESS | 固定 trailer、Node output 与 host gate 已有基线；待 bitmap、hardware latch 与 VDC eligible 闭环。 |
| TDMA-PAYLOAD-003 | critical RefMem 与 ACK/fence/quality | IN PROGRESS | baseline delta 与 ACK 摘要已上 wire；待正式 commit/fence 闭环。 |
| TDMA-PAYLOAD-004 | 最小控制 token | IN PROGRESS | 固定 token 已预留；待 owner、opcode 与 completion 接入。 |
| TDMA-PAYLOAD-005 | optional 静态余量准入门禁 | DONE | optional 只使用 mandatory 后余量，layout 不保留 runtime-free 字节。 |
| TDMA-PAYLOAD-006 | 短帧基础诊断压缩与实时路径隔离 | IN PROGRESS | 短帧只保留 CRC/sequence/FIFO/bitmap/WKC/profile/deadline 基础摘要；SD、SVG、原始波形和详细归因不进入 Core1 或 recovery frame。 |
| TDMA-FLIGHT-001 | 常驻循环过程映像与单轮多 Node overlay | IN PROGRESS | 一次初始化、cycle boundary 持续循环、同圈多 Node overlay、无更新透传与受控退出全部通过多板验证。 |
| TDMA-FLIGHT-002 | 真飞行处理：wire-self-clocked resident flight（**最高优先级主线**） | IN PROGRESS | F1–F5、owner/resource/seqlock、完整 WCET/RAM 与周期映射通过；TDMA-RESIDENT-01 激活须完成 C11。 |
| TDMA-FLIGHT-002A | B0 前置门禁：RX/TX 重叠与固定 pipeline delay 实测 | DONE | 当前夹具 byte-level 流水已取证，见 TDMA-PROGRESS-20260911-006；PIO/profile 变更须复验，cycle-level 另行验收。 |
| TDMA-FLIGHT-002B | B1 自激发车时钟：Core1 退出发车门控 | IN PROGRESS | 已有自主运行与主站有限 service 屏蔽证据；待从站屏蔽、逐帧物理节拍、切换/故障恢复及完整预算闭环。 |
| TDMA-FLIGHT-002C | B2 预装双缓冲与无更新零 Core1 稳态 | PENDING | 双槽边界切换、tx_reuse_count 与无更新波形一致；Core1 稳态不改变线节拍。 |
| TDMA-FLIGHT-002D | B3 overlay 非阻塞注入与单轮多 Node LOAD/UNLOAD | PENDING | 迟到更新顺延、就绪更新命中且节拍不变；同圈多 Node LOAD/UNLOAD 有逐 hop owner/CRC 波形。 |
| TDMA-FLIGHT-002E | B4 契约收敛与 byte/cycle 分级飞行声明 | PENDING | F1–F5 证据齐全后完成 C11、registry 状态收敛及顶层刷新；byte/cycle 声明分别留证。 |
| TDMA-FLIGHT-002F | 异步候车平台：TX 准备与 RX 解析移出实时服务路径 | IN PROGRESS | TX 构图/描述符/邮箱与普通 RX 解析已有异步实现；待完整预算/RAM、迟到复用、拥塞与 STOP 交接验收。 |
| TDMA-FLIGHT-002G | 特等席快速通道：逐圈同步样本生成、装载与独立卸载 | PENDING | 每圈硬件 latch、编码与独立卸载有界无损；lag/epoch/sequence/quality、消费停顿、溢出负测和 VDC 时钟映射通过。 |
| TDMA-FLIGHT-002H | 四级准备与服务隔离、固定配额准入 | PENDING | 各域独立准备、唯一装配者与固定配额准入；普通流饱和不影响特等逐圈交付和一等 freshness，RefMem ACK/fence 可靠。 |
| TDMA-FLIGHT-002I | 编译容量与运行时邮箱布局 | IN PROGRESS | 编译容量、STOP 选节点数及 ARM 冻结布局已有基线；待正式 RAM、完整预算、扩容及混合容量实环验收。 |
| TDMA-FLIGHT-003 | 发车节拍预算显式化与 fail-closed 准入（Track A，B1 的前置使能） | PENDING | 符号声明可达节拍下界并提供只读证据；低于下界必须拒绝，窗口量消费 Calibration 发布值。 |
| TDMA-FLIGHT-004 | 2026-09-11 四节点闭环回归归因 | DONE | 改动族回归归因与四板恢复完成，见 TDMA-PROGRESS-20260911-003；不外推电气根因。 |
| TDMA-FLIGHT-005 | `process_follower` 补丁下标与指令插入位置绑定 | DONE | PIO WAIT 补丁绑定 public label，变异回归及多板验证完成；见 TDMA-PROGRESS-20260911-004。 |
| TDMA-FLIGHT-006 | process-image byte 边界时序与 phase 准入 | IN PROGRESS | byte/terminal 路径准入已有模型与同钟证据；待最坏 DMA 仲裁/断粮、环境和严格校准覆盖。 |
| TDMA-FLIGHT-007 | 非字节对齐 overlay 的邻接 owner 保护 | IN PROGRESS | 非字节对齐 bit 所有权已有 C/逐 hop 证据；待自主同圈准备、generation handoff 与最坏供给压力。 |
| TDMA-HIL-001 | 四板 TDMA 环路的 WCET/频率/占空比/SD 波形基线 | PENDING | 归档当前源码四板完整 WCET、物理频率/占空比、原始波形与零错误全窗口；NO5 保持环外。 |
| TDMA-HIL-002 | 逐 phase 开载且 TDMA 零回归 | PENDING | 依次启用 VDC/DPLL/RefMem/control，TDMA deadline/error 不增加。 |
| TDMA-DPLL-001 | PIO/DMA hardware latch correlation | IN PROGRESS | reference TX latch 已作为固定 process-image trailer 关联上一帧 sequence；仍需 active PATH_DELAY、四板同圈 eligible sample 和 wrap/失配 HIL。 |
| TDMA-DPLL-002 | 节点 DPLL lock 与 VDC 发布 | IN PROGRESS | TDMA 接受合格样本后验证锁相、VDC 发布及 NO5 指定间隔/同时触发；当前执行证据见 VDC 域三件套。 |
| TDMA-DPLL-003 | active calibration path matrix 前置门禁 | PENDING | NO1..NO4 线序、loop/link/node 顺序、path-delay/offset matrix、generation/freshness 全部一致；运行态禁止按物理环序推算或 fallback 累加。 |
| TDMA-DPLL-004 | 硬件 timestamp spine 与 SCK 独立校准 | PENDING | CS/SCK/DATA 硬件 latch、量化/lag/wrap/失配拒绝及独立 SCK 校准通过 host/C/HIL；软件时间戳只作诊断。 |
| TDMA-DPLL-005 | DPLL eligible evidence parser/quality gate | PENDING | 只接受 hardware tick、分辨率门禁、active matrix identity 匹配且 sequence 连续的样本；invalid/out-of-order/duplicate/diagnostic-only 样本只增加 quality 计数，不推进 LOCKED。 |
| TDMA-DPLL-006 | 最小 DPLL servo 与 VDC compact publish | PENDING | 仅在 DPLL/VDC phase 执行锁相所需 servo、path-delay compensation、lock/holdover 和 compact phase/rate/quality 发布；WCET 和 TDMA 错误计数无回归。 |
| TDMA-HIL-003 | 四节点闭环与 NO5 观测验收 | PENDING | 先以四节点短帧稳定作为 TDMA gate；随后 NO5 只读观测指定间隔/同时触发，VDC 接收 compact output，DPLL lock evidence 可追溯；波形诊断不侵入实时 phase。 |
| TDMA-DPLL-007 | DPLL 故障注入、holdover、relock 与长稳 | PENDING | 单链路错误、timestamp invalid、matrix generation 变化和 recovery 注入均 fail-closed；恢复后重新取得 eligible 样本并锁相，长稳无 TDMA 时序回归。 |
| TDMA-DPLL-008 | DPLL residual SD 原始证据与离线 SVG | IN PROGRESS | SRAM 追加、STOP 后 Core0/StorageAO 导出及离线解码链路通过四板/NO5 验收；不进入实时负载。 |
| TDMA-DPLL-009 | DPLL 启动 profile 持久化与保守默认 | IN PROGRESS | Flash profile CRC、缺省写入/读回及失败拒绝启动验证通过；运行时参数仅显式 STORe 后持久化。 |
| TDMA-REL-001 | ACK/fence/retry 和长期稳定性策略 | PENDING | 原始错误率先收敛，再以有界重发/修复完成 EtherCAT-style 验收。 |
| TDMA-REL-002 | 原 Node 位置 recovery 双 buffer 与固定预算 | IN PROGRESS | 原 Node offset、独立预算、双 buffer、ACK/有界 retry/backpressure/fail-closed 全链路通过多拓扑 HIL。 |
| TDMA-T2-001 | REFMEM + 部分控制后的 T2 最小载荷预算 | PENDING | 不超固定 SHORT/body 和 phase WCET，编译期拒绝 overcommit。 |

## 近期证据变化

| 日期 | Progress ID | 证据变化 | 结论 |
|---|---|---|---|
| 2026-09-14 | TDMA-PROGRESS-20260914-013 | 周期配置、四档 generation、ARM 冻结和四板 quick diagnostic 完成。 | 1.5 ms 默认档主路径接近完成；长档仅有配置/冻结证据。 |
| 2026-09-14 | TDMA-PROGRESS-20260914-012 | RX 后台准备候选撤回，源码恢复异步邮箱基线。 | 候选未采纳；完整 phase、峰值因果和 RAM/WCET 未闭合。 |
| 2026-09-14 | TDMA-PROGRESS-20260914-011 | 自主主站邮箱异步准备完成并采纳。 | 异步交接基线保留；完整预算和从站峰值未闭合。 |
| 2026-09-14 | TDMA-PROGRESS-20260914-005 | 主站有限 service blackout 取得硬件进展。 | 仅覆盖有限区间，不等同完整 blackout。 |
| 2026-09-13 | TDMA-PROGRESS-20260913-062/063 | 编译容量和 STOP 后节点布局切换完成。 | 正式 RAM、扩容及混合容量实环待验收。 |

## 当前阻塞项

- `TDMA-FLIGHT-001/002/002B/002F`：resident cycle、无更新透传、同圈 overlay、自激发车、完整 WCET/RAM、切换/拥塞/故障恢复。
- `TDMA-FLIGHT-006/007`：最坏 DMA/环境准入、沿途 owner 保护和 generation handoff。
- `TDMA-RESIDENT-01` 与 `HAOFV-879`：C11 收敛和 snapshot seqlock 条款。
- `TDMA-DPLL-001/002/004/005`、`TDMA-HIL-001`：hardware latch、eligible parser、VDC/DPLL、NO5 和四板全窗口。
- formal ACK/fence、control owner、正式 System Pack map、动态质量向量和长档反馈窗口仍未闭合。

## 旧清单承接

原 P0–P7/P0.5 清单的未验收事项压缩为稳定 ID；详细过程只在 Task Progress 维护。

| ID | 承接范围 | 状态 | 退出门禁 |
|---|---|---|---|
| TDMA-TRAIN-001 | P0.5-8 RX 扫描诊断 | PENDING | candidate/idle/real magic 分类可追溯。 |
| TDMA-TRAIN-002 | P0.5-9a/9b 训练状态机与 coded persona | PENDING | owner 单写、epoch/seq、PIO/SM/DMA 资源和有界恢复。 |
| TDMA-TRAIN-003 | P0.5-9c/9d/9e 校准投影与 TRAIN frame | PENDING | guarded/seqlock、freshness、ACK/commit 和失败原因。 |
| TDMA-TRAIN-004 | P0.5-9f/9g/9h 拓扑、窗口与 evidence 边界 | PENDING | active topology/generation/freshness 接入 schedule。 |
| TDMA-TRAIN-005 | P0.5-9i/9j host 编排、故障注入和恢复 | PENDING | STOP→APPLY→ARM→TRAIN→restore→STOP 可回放。 |
| TDMA-QUALITY-001 | 逐流质量向量与 backpressure | PENDING | per-class 计数、预算超限和 quality 发布。 |
| TDMA-MAP-001 | 正式 System Pack map 与 active/shadow swap | PENDING | owner/generation 在 cycle boundary 原子切换。 |
| TDMA-ADAPTER-001 | PIO SPI 两级 flight gate | PENDING | byte/cycle 分级 HIL、WKC/CRC/FIFO/map apply。 |
| TDMA-ADAPTER-002 | BISS-C、UART/RS485 与统一 adapter contract | PENDING | MTU、latency、timeout、quality 语义齐全。 |
| TDMA-BULK-001 | OTA/LOG/RefMem bulk 与 critical 配额 | PENDING | 不阻塞 Core1、不侵占 guard。 |
| TDMA-REL-003 | 多环 sequence/duplicate elimination | PENDING | 多路径方案冻结后再验证。 |
| TDMA-BOARD-001 | 产品样板 IO 与 TDMA 产品 HIL | PENDING | 完整 IO、单跳和闭环 HIL。 |

## 统一完成定义

任务同时满足架构 owner、不变量、编译/pytest、OTA 多板、SD 原始波形/分析、失败回退证据和文档门禁，才可标为 `DONE`。resident process image 还须证明一次初始化、持续 cycle、单轮多 Node overlay、无更新透传和受控退出。

详细证据入口：`docs/tdma/TDMA_TASK_PROGRESS.md`。
