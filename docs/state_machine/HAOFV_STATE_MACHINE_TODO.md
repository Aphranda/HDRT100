# 状态机与底层实时资源域待办

Status: Active
Domain: STATE_MACHINE
Canonical: `docs/state_machine/HAOFV_STATE_MACHINE_TODO.md`
Related: `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/sync/SYNC_IO_TODO.md`
Last updated: 2026-08-31

本文档维护状态机域的可执行任务；稳定语义见 `HAOFV_STATE_MACHINE_ARCHITECTURE.md`，
构建、测试、OTA/HIL 和失败证据见 `HAOFV_STATE_MACHINE_TASK_PROGRESS.md`。

## P0 当前主线（状态机升级回归）

本主线优先于普通迁移收尾。实时路径选择“单一 RX DATA SM + 单一 RX FIFO/DMA”方案：
它保留 PIO 硬件转发的确定性，不在拍级路径引入第二采样器或 FIFO 双消费者；SD/初始
波形读取只在 STOPPED/diagnostic capture 窗口执行。每一步均须可回退，并以同一 OTA
包完成闭环后才能推进下一步。

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

## 里程碑总览

| ID | 里程碑 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SM-M1 | 资源和方向契约冻结 | DONE | 三 PIO 职责、TX/RX 端口均含 IN/OUT，且 CLK/SYNC 与 DATA 的交叉方向、FIFO/DMA owner 已在架构文档登记。 |
| SM-M2 | board/profile/resource arbiter 迁移 | IN PROGRESS | PIO、SM、DMA、GPIO 和 persona 均由独立符号声明，冲突 fail-closed；flight PIO/SM claim 与 process-image ARM admission 已接入，DMA/GPIO 完整仲裁仍待完成。 |
| SM-M3 | TX/RX 交叉方向 PIO 原语 | IN PROGRESS | flight origin/follower 已按 TX/RX PIO 装载方向原语；专用原语完整验证和 maintenance persona 边界仍待完成。 |
| SM-M4 | follower forward/capture 双路径 | IN PROGRESS | 当前 flight follower 由 RX DATA SM 以 `push noblock` 同时完成 wire forward 与 RX 卸载，DMA 只消费该 FIFO；独立 sampler 不在运行态启用。仍需四板 process-image HIL 和诊断 capture 窗口验收。 |
| SM-M5 | 四板 TDMA 验收 | PENDING | build、pytest、四板异步 OTA、TDMA HIL 和 SD 原始波形通过；不要求 NO5。 |
| SM-M6 | NO5 DPLL/VDC 观测验收 | PENDING | 在 SM-M5 通过后，NO5 只读 evidence、DPLL lock 和 VDC readback 全通过；NO5 不进入 TDMA ring。 |

## 当前任务表

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SM-RES-001 | 增加 SMA、TDMA TX、TDMA RX 三组 PIO resource claim | IN PROGRESS | profile/resource arbiter 可查询并拒绝跨域 PIO/SM/DMA/GPIO 重叠。 |
| SM-RES-002 | 增加 CLK/SYNC/DATA 交叉方向 SM、FIFO、DREQ 和 DMA 字段 | IN PROGRESS | board contract、runtime resource view、flight PIO/SM claim/release、四个 DMA endpoint 及 GPIO/IRQ/DREQ resource admission 已建立；maintenance 资源统一仲裁和板端证据仍待完成。 |
| SM-RES-003 | 将 TX 端交叉方向 SM 迁移 | IN PROGRESS | flight control/CLK 输出和 origin DATA 输入路径已使用 TX PIO 方向字段；专用原语和全 persona 回归仍待完成。 |
| SM-RES-004 | 将 RX 端交叉方向 SM 迁移 | IN PROGRESS | flight DATA 输出、capture 和 follower process boundary 已使用 RX PIO 方向字段；双路径 HIL 仍待完成。 |
| SM-RES-005 | 完成 follower forward/capture 独立 FIFO/DMA | IN PROGRESS | forward 与 RX 卸载统一由 RX DATA SM 的单一 FIFO/DMA endpoint 完成，避免双消费者竞争；SD/波形 capture 仅作为停止态 diagnostic persona，仍需 endpoint 静态检查和 HIL。 |
| SM-RES-009 | 固化独立 flight RX unload / TX load 控制 | IN PROGRESS | `tdma_flight_engine_unload_rx()` 只生成 RX 位图并在 descriptor 入队后提交，`tdma_flight_engine_load_tx()` 只覆盖 TX segment；两方向可在同一 phase 并行且互不阻塞，需补四板 process-image 回归证据。 |
| SM-RES-006 | 迁移 arm/disarm、snapshot、RTT 和 DPLL evidence | IN PROGRESS | flight ARM/STOP、snapshot、RTT、SCK capture、clock-latch recovery、process-image persona admission 和 calibration persona 切换已按方向字段迁移；旧复合 maintenance 路径和完整硬件证据回归仍待完成。 |
| SM-RES-007 | 增加静态回归测试与资源冲突负测试 | IN PROGRESS | 已覆盖 PIO 指令方向、CLK/SYNC 与 DATA 语义绑定、SM/DMA 唯一性、forward/unload FIFO、capture patch、calibration directional unload 和 flight resource mask；DREQ/GPIO/persona epoch 的运行时负测试仍待完成。 |
| SM-RES-008 | 工具构建及四板闭环验证 | IN PROGRESS | `out/` 构建与 host pytest 已通过；仍需同包四板异步 OTA、四板 process-image active 和 SD 波形；NO5 hardware-latch、DPLL `LOCKED` 与 VDC vector readback 由 SM-M6 单独验收。 |

## 当前阻塞项

- maintenance/calibration persona 仍使用 `BOARD_TDMA_SPI_PIO` 的旧复合实现；flight
  persona 已开始使用 TX/RX 两个 PIO block，但迁移尚未覆盖所有 persona 和资源仲裁层，
  因此不能把 SM-M2 至 SM-M5 提前完成。
- follower 的 forward 与 capture 双消费者需要独立 RX FIFO/SM 或硬件复制语义，不能
  通过两个 DMA 直接竞争一个 FIFO。
- PIO 迁移完成前，DPLL hardware timestamp spine 和 eligible gate 不进入正式 HIL。

## 统一完成定义

TDMA 任务只有满足 HAOFV owner 边界、静态资源契约、编译/pytest、同包四板异步 OTA、
四板 TDMA HIL、SD 原始波形证据和文档门禁，才可标记为 `DONE`；DPLL/VDC 任务另外
需要 NO5 只读观测和对应 evidence。失败时
保留证据并回退到最近一个已验证 persona，不以旧复合路径掩盖迁移缺口。
