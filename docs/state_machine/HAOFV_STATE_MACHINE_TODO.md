# 状态机与底层实时资源域待办

Status: Active
Domain: STATE_MACHINE
Canonical: `docs/state_machine/HAOFV_STATE_MACHINE_TODO.md`
Related: `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/sync/SYNC_IO_TODO.md`
Last updated: 2026-09-03

本文档维护状态机域的可执行任务；稳定语义见 `HAOFV_STATE_MACHINE_ARCHITECTURE.md`，
构建、测试、OTA/HIL 和失败证据见 `HAOFV_STATE_MACHINE_TASK_PROGRESS.md`。

## P0 当前主线（状态机升级回归）

本主线优先于普通迁移收尾。实时路径选择“单一 RX DATA SM + 单一 RX FIFO/DMA”方案：
它保留 PIO 硬件转发的确定性，不在拍级路径引入第二采样器或 FIFO 双消费者；SD/初始
波形读取只在 STOPPED/diagnostic capture 窗口执行。TDMA 的状态机目标是一次
`RESIDENT_INIT` 后持续运行 resident process image，在每个 cycle 内执行本地
`UNLOAD -> LOAD -> FORWARD`，不因物理 frame 完成而终止。每一步均须可回退，并以同一 OTA
包完成闭环后才能推进下一步。

resident cycle、资源事实源以及 maintenance/calibration persona 资源仲裁已完成四板验收。
当前唯一执行入口为 `SM-RES-003`：先收口 TX 端 control/CLK 输出和 DATA 输入的无状态
quiesce/unload/load/arm 方向原语，再进入 RX 端原语。表中其他 `IN PROGRESS` 表示已有部分
实现或证据尚未收口，不代表允许并行修改。

| ID | 优先级 | 目标 | 状态 | 完成或退出门禁 |
|---|---|---|---|---|
| SM-P0-001 | P0 | 收敛控制 SM(bit) / DATA SM(byte) 计数、PIO patch 地址和 REPLACE 字节对齐 | IN PROGRESS | PIO 生成头、静态负测、完整编译和 raw-flight HIL 无 timeout/CRC 增长 |
| SM-P0-002 | P0 | 固化 follower 单一 RX DATA FIFO/DMA，并验证 origin/follower/process persona 的 PUSH/autopush 一致性 | IN PROGRESS | DREQ/FIFO/SM 映射静态检查 + 四板 raw/process-image HIL 连续稳定 |
| SM-P0-003 | P0 | OTA 闭环：同一 package 异步更新、重启确认 build/persona、失败回退到最近验证版本 | PENDING | OTA summary 全节点成功，启动后版本/slot/TDMA snapshot 一致，回退演练通过 |
| SM-P0-004 | P0 | SD 初始波形辅助调试：停止态读取原始 capture，关联 SM PC、DMA 产出和 CRC/边界计数 | PENDING | SD 文件 CRC/长度有效，离线解码能定位首帧边沿/错位，且不改变 realtime phase |

## 状态规则与统一门禁

任务状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`。代码/host 通过但尚未
完成 OTA/HIL 的任务不得标记为 `DONE`。状态机迁移不得改变 TDMA SHORT 帧、拍级
phase、recovery 静态预算或 FreeRTOS heap；超限必须在构建或 DeploymentGate 拒绝。

## 迁移依赖序列

状态机迁移按以下依赖顺序推进，后序任务不得因为代码已经存在就提前验收：

```text
资源事实源
  -> 资源 claim / release 与冲突仲裁
  -> 无状态 quiesce / unload / load / arm 原语
  -> persona lifecycle FSM
  -> resident process image cycle FSM
  -> AO/Core1 编排、snapshot 与 SCPI
  -> 端到端运行态迁移
```

每一层必须先完成自己的 host/build 门禁和快速硬件验收，再允许下一层进入实现。
如果前置层的字段、owner 或失败语义发生变化，后置层只在前置验收通过后复核一次，
不重复无边界地返工。当前 persona lifecycle FSM 是迁移中的已完成切片，但它的资源
事实和仲裁前置条件仍由 `SM-RES-001/002/007` 负责；后续不得把本切片误当成完整
运行时迁移完成。

## 里程碑总览

| ID | 里程碑 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SM-M1 | 资源和方向契约冻结 | DONE | 三 PIO 职责、TX/RX 端口均含 IN/OUT，且 CLK/SYNC 与 DATA 的交叉方向、FIFO/DMA owner 已在架构文档登记。 |
| SM-M2 | board/profile/resource arbiter 迁移 | DONE | PIO、SM、DMA、GPIO 和 persona 均由独立符号声明；maintenance 与 flight owner、PIO SM claim/release、persona 转移、运行时冲突快照和无泄漏恢复负测已收口，并完成 build、四板闭环与 SD 波形验收。 |
| SM-M3 | TX/RX 交叉方向 PIO 原语 | IN PROGRESS | flight origin/follower 已按 TX/RX PIO 装载方向原语；专用原语完整验证和 maintenance persona 边界仍待完成。 |
| SM-M4 | follower forward/capture 双路径 | IN PROGRESS | 当前 flight follower 由 RX DATA SM 以 `push noblock` 同时完成 wire forward 与 RX 卸载，DMA 只消费该 FIFO；独立 sampler 不在运行态启用。四板 process-image HIL、停止态诊断 capture 和 maintenance owner 边界已取得，仍需收口 endpoint 静态检查。 |
| SM-M7 | resident process image cycle FSM | DONE | `RESIDENT_INIT` 只注入一次；`RUNNING` 持续执行 cycle boundary、本地 UNLOAD/LOAD 和 FORWARD；无更新透传；物理 frame completion 回到下一 cycle；STOP、复位、故障或重新配置受控退出；host/build、四板闭环和 SD 波形证据已归档。 |
| SM-M5 | 四板 TDMA 验收 | PENDING | build、pytest、四板异步 OTA、TDMA HIL 和 SD 原始波形通过；不要求 NO5。 |
| SM-M6 | NO5 DPLL/VDC 观测验收 | PENDING | 在 SM-M5 通过后，NO5 只读 evidence、DPLL lock 和 VDC readback 全通过；NO5 不进入 TDMA ring。 |

## 当前任务表

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SM-RES-001 | 增加 SMA、TDMA TX、TDMA RX 三组 PIO resource claim | DONE | profile/resource arbiter 已按 owner 查询和拒绝 PIO/SM/DMA/GPIO/DREQ 重叠；错误 owner 不能释放租约，冲突恢复不留下部分资源。 |
| SM-RES-002 | 增加 CLK/SYNC/DATA 交叉方向 SM、FIFO、DREQ 和 DMA 字段 | DONE | board contract、runtime resource view、四个 DMA endpoint 及 GPIO/IRQ/DREQ admission 已建立；maintenance 与 flight 使用独立 owner，program manager 统一 PIO SM claim/release、persona 转移和失败回滚；host/build、同包四板 OTA、短帧闭环与四板 SD 波形通过。 |
| SM-RES-007 | 增加静态回归测试与资源冲突负测试 | DONE | PIO 指令方向、CLK/SYNC 与 DATA 语义绑定、SM/DMA 唯一性、forward/unload FIFO、capture patch、calibration directional unload、两类 resource mask、GPIO/DREQ/persona owner 冲突、错误快照、错误 owner 释放和无泄漏恢复均有可执行负测；host/build、四板闭环与 SD 波形通过。 |
| SM-RES-003 | 将 TX 端交叉方向 SM 迁移 | IN PROGRESS | flight control/CLK 输出和 origin DATA 输入路径已使用 TX PIO 方向字段；专用原语和全 persona 回归仍待完成。 |
| SM-RES-004 | 将 RX 端交叉方向 SM 迁移 | IN PROGRESS | flight DATA 输出、capture 和 follower process boundary 已使用 RX PIO 方向字段；双路径 HIL 仍待完成。 |
| SM-RES-005 | 完成 follower forward/capture 独立 FIFO/DMA | IN PROGRESS | forward 与 RX 卸载统一由 RX DATA SM 的单一 FIFO/DMA endpoint 完成，避免双消费者竞争；SD/波形 capture 仅作为停止态 diagnostic persona，仍需 endpoint 静态检查和 HIL。 |
| SM-RES-009 | 固化独立 flight RX unload / TX load 逻辑边界 | IN PROGRESS | 当前 `inspect_input()` / `commit_input()` / `apply*()` 的输入识别、提交和局部 overlay 职责可追溯；后续方向化接口不得改变语义；两方向可在同一 phase 并行且互不阻塞，需补四板 process-image 回归证据。 |
| SM-FSM-001 | persona lifecycle FSM 与 program manager 接入 | DONE | host FSM 单测设计已加入；三镜像构建、五板同包 OTA/软件复位、四板 TDMA/NO5 快速诊断流程完成，保留 strict gate 失败证据；五板 `TDMA:PHYS?` 读回 NO1-NO4 为 `ACTIVE` 且 lifecycle error 为 0，NO5 为 `STOPPED`。 |
| SM-RES-010 | 将 adapter/FSM 从 per-frame completion 迁移为 resident cycle | DONE | 初始 process image 只在 `RESIDENT_INIT` 装载一次；每个物理 frame 完成后回到 `CYCLE_BOUNDARY`，不得要求重新 ARM；单轮多 Node overlay、无更新透传、cycle sequence/segment generation 和受控退出均有 host/build/TDMA 短帧闭环证据。 |
| SM-RES-006 | 迁移 arm/disarm、snapshot、RTT 和 DPLL evidence | IN PROGRESS | flight ARM/STOP、snapshot、RTT、SCK capture、clock-latch recovery、process-image persona admission 和 calibration persona 切换已按方向字段迁移；旧复合 maintenance 路径和完整硬件证据回归仍待完成。 |
| SM-RES-008 | 工具构建及四板闭环验证 | IN PROGRESS | `out/` 构建与 host pytest 已通过；仍需同包四板异步 OTA、四板 process-image active 和 SD 波形；NO5 hardware-latch、DPLL `LOCKED` 与 VDC vector readback 由 SM-M6 单独验收。 |

## Resident cycle 当前执行顺序

`SM-RES-010` 是本阶段的总任务，下面的子任务必须按顺序推进，不并行改写上下层语义。
实现按 reference 角色选择，当前拓扑中该角色是 NO1，但不得把 resident cycle 硬编码到
NO1 板号。NO2--NO4 默认复用现有 PIO 飞行转发和本地 process-image overlay；只有
`SM-CYCLE-004` 的证据证明 follower 不满足契约时，才允许修改 follower PIO 算法。

代码改动面分成两类：`SM-CYCLE-000` 修复当前 reference origin 的物理发送生命周期；
`SM-CYCLE-003` 已完成 resident 数据面的 reference 回收再发改动，只替换 reference 角色逻辑。
transport helper 和 communication FSM 是角色无关的边界件，不改变 NO2--NO4 的飞行转发
算法。

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SM-CYCLE-000 | 恢复 reference origin 单帧发送生命周期基线 | DONE | reference 只在上一物理 TX completion 已消费后提交下一次发送，不重复撞入 pending DMA；快速 TDMA-only 验收中 reference 与 follower sequence 持续增长、`TX_BUSY` 不增长，并取得 NO1--NO4 可用 SD capture 窗口。 |
| SM-CYCLE-001 | 冻结 returned frame 到 next cycle 的 transport boundary helper | DONE | helper 的 host test 和固件 build 通过；在 `SM-CYCLE-000` 恢复基线后补跑快速 TDMA-only，证明未接 runtime 的 helper 不改变现有短帧行为。返回 payload 保留，reference 本地 segment 可更新，cycle sequence 推进，hop count 重置，完整性按协议重算。 |
| SM-CYCLE-002 | 将 adapter communication FSM 改为 resident cycle 语义 | DONE | `FRAME_COMPLETE` 终止态被非终止 `CYCLE_BOUNDARY` 取代；一次 ARM 后每轮清理物理 window 标志并保持 `RUNNING`，STOP、RESET、ERROR 转移、纯 C 单测及快速 TDMA-only 闭环通过。 |
| SM-CYCLE-003 | 仅将 reference runtime 接入 resident FSM 和 transport helper | DONE | reference 启动时只 seed 一次；返回帧到达后执行 `LOCAL_UNLOAD/LOAD -> begin next cycle -> phys_tx`，发送由 completion/backpressure 驱动；丢失返回帧时从最后有效 resident image 生成 stale cycle；RTT 观测 FIFO 背压不得阻塞实时发送；host/build、四板快速闭环、SD 原始波形和标准快速 P3 receipt 已收口，角色选择未写死 NO1 板号。 |
| SM-CYCLE-004 | 验证四节点同轮 overlay 和 follower 兼容性 | DONE | 链式 host 回归证明同一 resident image 依 DATA 物理顺序保留 NO1--NO4 segment；无新 generation 时旧值透传且 cycle sequence 独立推进。四板闭环、SD 原始 capture 和当前源码指纹 receipt 已归档，未修改 follower PIO 算法。 |
| SM-CYCLE-005 | 增加 cycle 状态、计数和异常恢复 evidence | DONE | Core0 只读查询可获得 cycle count、last completed cycle、segment bitmap、fault/reseed reason；异步 TX 只在 FSM 到达 cycle boundary 后提交完成信息，丢帧和 bootstrap 耗尽均只复用最后有效 resident image。host/build、四板闭环与 SD 波形证据已归档。 |
| SM-CYCLE-006 | 完成 resident cycle 的构建与四板短帧验收 | DONE | 编译/pytest、同包四板异步 OTA、快速 TDMA-only 短帧闭环及 NO1--NO4 SD 原始波形通过；新鲜快速训练按链路自适应覆盖量化候选并自动选择稳定 replay 行，不依赖人工复用旧矩阵；NO5 不参与本 gate，完整证据已归档到任务进度文档。 |

每个 `SM-CYCLE-*` 代码切片都必须先通过对应 host/build 检查，再运行快速 TDMA-only
短帧闭环后才能进入下一项；失败也要保存参数、串口状态和 NO1--NO4 SD 原始波形，
不得因调试门禁拒绝执行后续观测。`SM-CYCLE-006` 和 `SM-RES-002` 完成后按依赖序列继续
`SM-RES-007` 的资源冲突负测，不提前进入方向原语或 AO/Core1 编排。

## 当前阻塞项

- 资源事实源和冲突仲裁已收口；当前按依赖序列只推进 `SM-RES-003` TX 方向无状态原语，
  在其 host/build/快速四板门禁完成前不得并行进入 `SM-RES-004` RX 原语或 AO/Core1 编排。
- follower 运行态继续保持单一 RX DATA FIFO/DMA；任何后续 capture 扩展都不得引入两个
  DMA 竞争同一 FIFO，只允许停止态 diagnostic capture 或显式硬件复制语义。
- PIO 迁移完成前，DPLL hardware timestamp spine 和 eligible gate 不进入正式 HIL。

## 统一完成定义

TDMA 任务只有满足 HAOFV owner 边界、静态资源契约、编译/pytest、同包四板异步 OTA、
四板 TDMA HIL、SD 原始波形证据和文档门禁，才可标记为 `DONE`；resident cycle 任务
另外必须证明一次初始化、持续循环、单轮多 Node overlay、无更新透传和受控退出；
DPLL/VDC 任务另外需要 NO5 只读观测和对应 evidence。失败时
保留证据并回退到最近一个已验证 persona，不以旧复合路径掩盖迁移缺口。
