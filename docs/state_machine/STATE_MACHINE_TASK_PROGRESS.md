# 状态机与底层实时资源域任务进度

Status: Active
Domain: STATE_MACHINE
Canonical: `docs/state_machine/STATE_MACHINE_TASK_PROGRESS.md`
Related: `docs/state_machine/STATE_MACHINE_DOMAIN_ARCHITECTURE.md`, `docs/state_machine/STATE_MACHINE_DOMAIN_TODO.md`
Last updated: 2026-08-29

本文档只记录状态机域的实施、构建、测试、OTA/HIL、失败和回退证据；任务状态以
`STATE_MACHINE_DOMAIN_TODO.md` 为唯一事实源，稳定语义以架构文档为准。

## 当前 checkpoint

### SM-PROGRESS-20260829-006 — flight runtime 方向资源迁移继续

- TODO task ID：`SM-RES-002`、`SM-RES-003`、`SM-RES-004`、`SM-RES-006`。
- 日期：2026-08-29。
- 变更：flight ARM/STOP、process-image boundary IRQ、RTT evidence FIFO、SCK 原始
  采集、运行 snapshot 和 origin recovery 已改用方向化 TX/RX/evidence PIO、SM；
  clock-latch 在 recovery 后显式 rearm。flight SM claim 在释放时同步 unclaim，避免
  失败 ARM 遗留硬件所有权。
- 保留边界：normal、calibration、P3 和训练 maintenance persona 继续使用旧 PIO2
  复合实现；它们不能与 flight persona 在同一 RUN epoch 混用。
- 构建：`out/build/state-machine-runtime-split-20260829/`；App/A/B、Boot、Flash
  link contract 和 OTA package 生成通过。
- 验证：状态机资源、文档回归和 docs_check 定向 pytest 20 项通过；尚未执行异步
  OTA、四板 HIL、NO5 观测和 SD 波形验收。
- 结论：flight runtime 迁移已从资源视图进入核心运行路径，但 SM-RES-002/003/004/006
  仍为 `IN PROGRESS`，SM-RES-005/007/008 仍需后续完成。

### SM-PROGRESS-20260829-002 — CLK/SYNC 与 DATA 交叉方向定义修正

- TODO task ID：`SM-RES-001`。
- 日期：2026-08-29。
- 变更：明确 `CLK`/`SYNC(CS)` 由 TX 端输出、RX 端输入；`DATA` 由 RX 端输出、TX
  端输入。TX/RX 两个逻辑 SM 都必须同时具备 IN/OUT；这是交叉收发的固定语义，
  不再错误要求单个 SM 只能使用一种 pin 指令方向。
- 依据：CS/SCK 是帧与位时序控制，DATA 是反向 payload/recovery 控制；三类信号必须
  分别拥有可追溯的 SM、FIFO、DREQ 和 DMA。
- 当前事实：源码仍为旧 `MASTER_SM/SLAVE_SM` 同向三线复合实现，本 checkpoint 只修正
  稳定契约，不证明运行时迁移已完成。
- 下一步：按 `SM-RES-002` 更新 board/profile/resource arbiter 字段，再迁移四类方向
  原语和 FIFO/DMA endpoint。

### SM-PROGRESS-20260829-003 — 交叉方向资源契约与构建回归

- TODO task ID：`SM-RES-002`（契约字段更新，运行时迁移仍未完成）。
- 日期：2026-08-29。
- 变更：`board_config.h` 与 `tdma_state_machine_resources.h` 增加 TX 端
  `CLK/SYNC OUT + DATA IN`、RX 端 `CLK/SYNC IN + DATA OUT` 的独立 SM/DMA 字段；
  保留旧宏仅作迁移期兼容别名。资源检查工具同步检查四类方向原语。
- 构建：`out/build/state-machine-cross-direction-20260829/`；`cmake_build_auto`
  完成 713/713，Boot、A/B 和 Flash link contract 均通过，生成
  `DHRT100_UPDATE.pkg`。
- 验证：方向资源工具通过；定向 pytest 2 通过；全量 pytest 首轮发现既有 latch 注释
  断言缺失，补齐注释后回归用例通过（完整 pytest 待本 checkpoint 结束前复跑）。
- 结论：静态契约和构建通过，但 `tdma_pio_spi_phys` 仍使用旧复合 persona，不能将
  `SM-RES-002` 或 SM-M2 标记为完成；下一步继续运行时资源迁移。

### SM-PROGRESS-20260829-004 — 方向性 runtime resource view

- TODO task ID：`SM-RES-002`。
- 日期：2026-08-29。
- 变更：新增 `tdma_state_machine_resource_contract_t`，由
  `tdma_state_machine_resource_contract()` 从 board-owned 宏生成 TX/RX PIO、四类
  控制/数据 SM 和四个 DMA endpoint 的运行时视图；`tdma_pio_spi_phys_t` 保存该视图
  供后续 persona snapshot 使用。
- 兼容边界：旧 `BOARD_TDMA_SPI_*_SM` 宏恢复为原有 PIO2 编号，仅作为迁移期维护路径，
  不再与新交叉方向字段混用。
- 构建：`out/build/state-machine-cross-direction-20260829/` 目标 `DHRT100` 已通过；
  PIO 头文件、Flash map/link contract 均通过。
- 当前事实：resource view 目前是声明与追踪接口，尚未执行实际 PIO1/PIO2 claim 或替换
  flight persona；SM-RES-002 仍为 `IN PROGRESS`。

### SM-PROGRESS-20260829-005 — flight PIO resource-arbiter admission

- TODO task ID：`SM-RES-002`。
- 日期：2026-08-29。
- 变更：flight ARM 在资源和容量校验完成后，通过 `resource_arbiter` 以
  `TDMA_FLIGHT_PIO` owner 原子申请 PIO1/PIO2；任一资源被其他 persona 持有即
  fail-closed。所有 ARM 失败路径和 disarm 均释放该 owner，避免资源泄漏。
- 验证：定向资源检查、文档门禁通过；`cmake_build_auto` 重新生成 PIO 头、App/A/B
  ELF、OTA package 和 Flash link contract。
- 当前事实：该 admission 已接入，但 flight 的 PIO 程序和 FIFO/DMA 仍使用旧
  `BOARD_TDMA_SPI_PIO` 复合路径；因此 SM-RES-002 仍为 `IN PROGRESS`，不得宣称
  PIO1/PIO2 运行时迁移完成。

### SM-PROGRESS-20260829-001 — 三 PIO 方向隔离方案冻结

- TODO task ID：`SM-RES-001`。
- 日期：2026-08-29。
- 变更：新建状态机域三件套，冻结 PIO0 专用 SMA、PIO1 专用 TDMA TX、PIO2 专用
  TDMA RX/evidence 的迁移目标；上行和下行使用不同 PIO block 的方向专用状态机。
- 关键边界：DATA TX 不执行 `in pins`，DATA RX 不执行 `out pins`；follower forward 与
  capture 不共享被多个 DMA 消费的 RX FIFO；Core0/1、PIO/DMA owner 边界保持 HAOFV。
- 当前事实：源码仍是 `BOARD_TDMA_SPI_PIO` 上的 `MASTER_SM/SLAVE_SM` 复合实现，
  因而本记录只证明架构和迁移任务已建立，不证明代码迁移、构建、OTA 或 HIL 完成。
- 验证：本次尚未执行 firmware build、pytest、异步 OTA、四板 TDMA HIL 或 NO5 观测。
- 下一步：执行 `SM-RES-002`，先加入 board/profile/resource arbiter 的独立资源声明，
  再拆分纯方向 PIO 原语和 DMA endpoint。

## 验证与证据索引

| progress ID | TODO task ID | 证据 |
|---|---|---|
| SM-PROGRESS-20260829-001 | SM-RES-001 | 本文档与 `STATE_MACHINE_DOMAIN_ARCHITECTURE.md`；暂无板端证据。 |

## 失败与回退

当前没有新的构建或板端失败记录。迁移过程中若出现资源冲突、PIO FIFO 竞争、DMA
stall、TDMA deadline/CRC/bitmap/WKC 回归，必须停止新 persona，恢复最近的已验证
TDMA 方向配置并在本文件追加失败证据；不得在运行态回退到未声明的复合 TX/RX 资源。
