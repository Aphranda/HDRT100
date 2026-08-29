# 状态机与底层实时资源域待办

Status: Active
Domain: STATE_MACHINE
Canonical: `docs/state_machine/STATE_MACHINE_DOMAIN_TODO.md`
Related: `docs/state_machine/STATE_MACHINE_DOMAIN_ARCHITECTURE.md`, `docs/state_machine/STATE_MACHINE_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/sync/SYNC_IO_TODO.md`
Last updated: 2026-08-29

本文档维护状态机域的可执行任务；稳定语义见 `STATE_MACHINE_DOMAIN_ARCHITECTURE.md`，
构建、测试、OTA/HIL 和失败证据见 `STATE_MACHINE_TASK_PROGRESS.md`。

## 状态规则与统一门禁

任务状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`。代码/host 通过但尚未
完成 OTA/HIL 的任务不得标记为 `DONE`。状态机迁移不得改变 TDMA SHORT 帧、拍级
phase、recovery 静态预算或 FreeRTOS heap；超限必须在构建或 DeploymentGate 拒绝。

## 里程碑总览

| ID | 里程碑 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SM-M1 | 资源和方向契约冻结 | DONE | 三 PIO 职责、TX/RX 端口均含 IN/OUT，且 CLK/SYNC 与 DATA 的交叉方向、FIFO/DMA owner 已在架构文档登记。 |
| SM-M2 | board/profile/resource arbiter 迁移 | IN PROGRESS | PIO、SM、DMA、GPIO 和 persona 均由独立符号声明，冲突 fail-closed。 |
| SM-M3 | TX/RX 交叉方向 PIO 原语 | PENDING | TX SM 固定为 `CLK/SYNC OUT + DATA IN`，RX SM 固定为 `CLK/SYNC IN + DATA OUT`；两组逻辑 SM 均可合法使用 IN/OUT。 |
| SM-M4 | follower forward/capture 双路径 | PENDING | 两条 RX FIFO/DMA 路径不共享消费端，core0 拥塞不影响 wire forward。 |
| SM-M5 | 四板 TDMA + NO5 观测验收 | PENDING | build、pytest、异步 OTA、四板 HIL、SD 原始波形和 NO5 只读 evidence 全通过。 |

## 当前任务表

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| SM-RES-001 | 增加 SMA、TDMA TX、TDMA RX 三组 PIO resource claim | IN PROGRESS | profile/resource arbiter 可查询并拒绝跨域 PIO/SM/DMA/GPIO 重叠。 |
| SM-RES-002 | 增加 CLK/SYNC/DATA 交叉方向 SM、FIFO、DREQ 和 DMA 字段 | IN PROGRESS | board contract 与 runtime resource view 已建立；下一步接入 resource arbiter 的实际 claim/release 和 persona snapshot。 |
| SM-RES-003 | 将 TX 端交叉方向 SM 迁移 | PENDING | TX SM 的 CLK/SYNC 输出、DATA 输入、FIFO/DMA 和边沿预算固定，禁止角色运行时交换。 |
| SM-RES-004 | 将 RX 端交叉方向 SM 迁移 | PENDING | RX SM 的 CLK/SYNC 输入、DATA 输出、FIFO/DMA 和边界预算固定，禁止角色运行时交换。 |
| SM-RES-005 | 完成 follower forward/capture 独立 FIFO/DMA | PENDING | forward DMA 与 capture DMA 各有固定 endpoint，禁止双消费同一 FIFO。 |
| SM-RES-006 | 迁移 arm/disarm、snapshot、RTT 和 DPLL evidence | PENDING | 所有调用使用方向明确字段；旧复合 persona 不能作为 RUN fallback。 |
| SM-RES-007 | 增加静态回归测试与资源冲突负测试 | PENDING | 覆盖 PIO 指令方向、PIO block、SM、DMA、DREQ、GPIO 和 persona epoch。 |
| SM-RES-008 | 工具构建及四板/NO5 闭环验证 | PENDING | 使用 `out/` 产物完成 build、pytest、异步 OTA、TDMA HIL、SD 波形和 NO5 观测。 |

## 当前阻塞项

- 当前源码仍使用 `BOARD_TDMA_SPI_PIO` 的 `MASTER_SM/SLAVE_SM` 复合实现；方向分离尚未
  落地，因此不能把 SM-M1 之外的里程碑提前完成。
- follower 的 forward 与 capture 双消费者需要独立 RX FIFO/SM 或硬件复制语义，不能
  通过两个 DMA 直接竞争一个 FIFO。
- PIO 迁移完成前，DPLL hardware timestamp spine 和 eligible gate 不进入正式 HIL。

## 统一完成定义

一个任务只有同时满足 HAOFV owner 边界、静态资源契约、编译/pytest、同包异步 OTA、
四板 TDMA HIL、NO5 只读观测、SD 原始波形证据和文档门禁，才可标记为 `DONE`。失败时
保留证据并回退到最近一个已验证 persona，不以旧复合路径掩盖迁移缺口。
