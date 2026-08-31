# 状态机与底层实时资源域任务进度

Status: Active
Domain: STATE_MACHINE
Canonical: `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Related: `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TODO.md`
Last updated: 2026-08-31

本文档只记录状态机域的实施、构建、测试、OTA/HIL、失败和回退证据；任务状态以
`HAOFV_STATE_MACHINE_TODO.md` 为唯一事实源，稳定语义以架构文档为准。

### SM-PROGRESS-20260830-016 - origin FIFO/DMA 启动竞态修复与 raw-flight 复验

- 根因修复：origin DATA SM 的物理字节计数控制字改为在启动 TX DMA **之前**写入
  DATA FIFO。DMA DREQ 在 FIFO 为空时立即有效，原顺序会让首个 payload 抢在控制字
  前面，PIO 将 payload 装入 `Y` 后无法完成帧，最终表现为 CS 长低和 `TX_BUSY`。
- 代码/测试：`tdma_pio_spi_phys_transport.c` 固化 bit/byte 计数和 FIFO 先置数顺序；
  `test_trn03_closed_loop.py` 保留顺序静态门禁。状态机与 TRN-03 定向回归为 `107 passed`。
- 构建/OTA：`out/build/p0-sm-20260830/` 的 PIO、App/A/B、Boot、package 和
  Flash link contract 通过；随后四板同包 OTA 目录为
  `out/ota/sm-p0-20260830-r1/`，`passed=true`、`updated_count=4`，启动 build 为
  `20260830054337`。
- raw-flight 复验：`out/training/sm-p0-raw-flight-20260830-r1/summary.json` 保留失败
  原始证据。启动屏障仍未通过：origin `last_error=TX_BUSY`、`rx_count=0`，followers
  `rx_dma_produced_words=0`；origin 的 `rx_dma_produced_words` 有增长但未形成有效帧。
  因此不能进入 process-image gate，也不宣称当前 PIO 修复已通过硬件验收。
- SD 诊断：四板均报告 `CARD_READY`；根目录 catalog 仅有系统目录/manifest，当前没有
  可下载的 TDMA capture 文件。读取接口保持 STOPPED-only，待下一次明确 diagnostic
  capture 生成文件后再执行分段 `FILE:READ?` 和波形解码。
- 下一步：针对 origin 返回 DATA/CS-SCK 物理边界继续采集 `PHYS?`（含 SM PC、PIO
  FDEBUG、GPIO 电平、DMA 写指针）并与单帧逻辑分析仪/SD capture 对齐；修复后重新走
  raw-flight，再放行 process-image。

### SM-PROGRESS-20260830-015 - P0 TDMA 环路回归主线建立

- TODO task ID：`SM-P0-001`、`SM-P0-002`、`SM-P0-003`、`SM-P0-004`。
- 架构决策：实时路径采用单一 RX DATA SM 同时完成 wire forward 和 RX unload，单一
  RX FIFO/DMA 作为唯一业务数据源；独立采样器不进入 RUNNING 拍级路径。SD/初始波形
  读取限定在 STOPPED 或明确 diagnostic capture 窗口，由 Core0/StorageAO 处理，避免
  FatFs、USB 或离线解码进入 core1/PIO 实时路径。
- 已开始修复：process follower 的 SCK patch 索引已按生成程序移动到实际 WAIT 指令；
  REPLACE 字节路径补充 MSB 对齐；raw/process follower DATA SM 恢复 `push noblock`；
  origin TX 控制/数据 FIFO 计数单位统一为 bit/byte 语义。
- 尚未完成：PIO 头重新生成后的完整 build、同包异步 OTA、四板 raw/process-image
  HIL，以及通过 SD 读取初始波形并与 `rx_dma_produced_words`、SM PC、CRC/边界计数
  关联。当前工作区改动未宣称硬件有效，失败时回退到最近验证 persona。
- 下一 gate：先完成 host 静态/编译门禁，再执行 OTA；OTA 启动确认通过后才进行四板
  raw-flight，raw 稳定后再进行 process-image 和 SD 波形分析。

### SM-PROGRESS-20260830-014 - 方向语义静态门禁

- TODO task ID：`SM-RES-007`。
- 变更：`tools/state_machine_resource_check/state_machine_resource_check.py` 新增
  `DIRECTION_REQUIRED` 语义校验，明确 TX `CLK/SYNC` 必须绑定 DOWNLINK 输出、TX
  DATA 必须绑定 UPLINK 输入，RX `CLK/SYNC` 必须绑定 UPLINK 输入、RX DATA 必须绑定
  DOWNLINK 输出；不再只比较偶然相同的 GPIO 数值。
- 负测试：新增交换 DATA 端口的失败用例，确保线序语义被反向修改时在构建前报告，
  不进入 OTA/HIL。
- 验证：方向资源工具通过；状态机资源与 TRN-03 定向 pytest `103 passed`。
- 结论：控制腿与 DATA 返回腿的交叉方向已具备静态回归保护；DREQ/GPIO/persona epoch
  的运行时冲突注入仍属于 `SM-RES-007` 后续门禁。

### SM-PROGRESS-20260830-013 - 独立 flight RX unload / TX load 控制

- TODO task ID：`SM-RES-005`、`SM-RES-009`。
- 变更：新增 `tdma_flight_engine_unload_rx()` 与
  `tdma_flight_engine_load_tx()` 两个方向化接口。前者只检查上行输入、生成
  `present/new/expected` 位图，RX descriptor 成功发布后再由既有 commit 接口提交序列；
  后者只把本节点已发布 TX generation 覆盖到下行 wire image，不读取或修改 RX 去重状态。
  `tdma_flight_engine_apply*()` 保留为兼容封装。ring adapter 的 origin、follower 和
  process-overlay 路径已切换到 TX load；RX polling 在未配置 health 时切换到 RX unload。
- 运行约束：Core1 在固定 phase 内可同时推进两个方向；RX FIFO 满、TX 无新 generation
  或单向操作失败都不得阻塞另一方向。PIO/DMA、FIFO ownership 和 SHORT 帧预算未改变。
- Host 回归：`tests/python/test_trn03_closed_loop.py` 与
  `tests/python/test_state_machine_resource_check.py` 共 `102 passed`。
- 构建：`out/build/flight-txrx-independent-20260830/`，App/A/B、Boot、PIO 生成、
  OTA package 和 Flash link contract 均通过；`tdma_pio_spi_phys.c` 仍按既定规则输出
  超过 1000 行 warning，实际为 2957 行。
- 板端：本记录只固化方向边界并完成 build/host gate，四板 process-image active、
  异步 OTA 和 NO5/SD evidence 仍按 `SM-RES-008` 后续门禁执行。

### SM-PROGRESS-20260830-012 - process-image persona admission for DPLL evidence

- TODO task ID: `SM-RES-006`、`SM-RES-007`、`SM-RES-008`。
- 变更：`distributed_refmem_tdma_ring_arm()` 在 STOPPED 状态完成 flight map、staged
  config 与 calibration gate 后，先调用
  `tdma_runtime_owner_set_flight_process_image_mode(true)`，再进入
  `tdma_service_ring_arm()`。这样 process-image persona 与固定 DPLL trailer 成为同一
  次 ARM admission，不会以 raw-flight persona 启动后再遗漏 trailer。persona 选择路径
  同时要求 flight resource owner 与方向化 SM claim 成对成立，失败时回收已取得的资源。
- Host 回归：状态机、TRN-03 closed-loop、TDMA cycle/process-image budget 和 DPLL
  observation 定向测试 `122 passed`。
- 构建：`out/build/dpll-state-machine-20260830/`（构建完成后记录 package/build id；
  source-size report 保留在同目录，`tdma_pio_spi_phys.c` 低于 3000 行但仍触发既定
  超过 1000 行 warning）。
- 板端状态：本记录不提前宣称 DPLL `LOCKED` 或 VDC 正式发布；待同包五板异步 OTA、
  四板 process-image active、NO5 observation、hardware-latch eligible 样本和 VDC
  vector readback。无 COM 端口或任一 gate 失败时保留失败证据并回退到最近已验证 persona。

### SM-PROGRESS-20260830-011 - ARM failure evidence and snapshot extraction

- TODO task ID: `SM-RES-005`、`SM-RES-006`、`SM-RES-007`、`SM-RES-008`。
- 变更：将 marker/data/SCK/clock snapshot 读取和 capture buffer copy 拆到
  `components/tdma/src/tdma_pio_spi_phys_snapshots.c`；为 physical ARM 和 persona
  切换增加 fail-closed 错误分类（phase admission、persona busy/resource、program
  load、flight config、RX arm、overlay prepare、clock latch 等），保留原有实时路径和
  recovery 预算不变。
- 构建：`out/build/phys-snapshots-20260830/`；App/A/B、Boot、PIO 生成、OTA package
  和 Flash link contract 均通过，package 为 `DHRT100_UPDATE.pkg`。
- Host 验证：全量 pytest `546 passed, 1 skipped`；TDMA/TRN-03 定向回归 `116 passed`。
- OTA：`out/ota/arm-error-evidence-20260830/summary.json`，五板异步 OTA
  `passed=true`、`updated_count=5`、`failed_count=0`。
- 四板 HIL：`out/tdma/arm-error-evidence-20260830-ring/`；按物理环序执行
  `STOP -> TOPOLOGY -> ARM`，NO2（`0010071E65B5CB38`/COM3）仍 ARM timeout，
  `ring_adapter_started=0`、`up_running=0`、`down_running=0`，尚未进入 TDMA active。
  当前失败证据应从各板 `SYSTem:SYNC:VDC:TDMA:PHYS?` 的新 `last_error` 和
  `program_switch_fail_count` 读取；本记录不将该失败归因于 DPLL。
- 结论：快照边界和 ARM 失败可观测性已完成 host/build/OTA 闭环；flight 与 legacy
  maintenance persona 的完整资源迁移、四板 active HIL 和 NO5/SD 验收仍未完成，
  `SM-RES-005/006/007/008` 继续保持进行中。

### SM-PROGRESS-20260829-010 — flight resource admission projection

- TODO task ID：`SM-RES-002`、`SM-RES-007`。
- 日期：2026-08-29。
- 变更：resource arbiter 新增四个方向化 DMA endpoint、TDMA GPIO、PIO IRQ 和 DREQ
  类资源位；`TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK` 由 board-owned directional
  contract 派生，flight ARM/STOP 使用同一掩码申请和释放。DREQ 不复制 SDK 数值，而按
  PIO/SM/方向端点类别保留，避免后续 profile 改号时失去冲突门禁。
- 构建：`out/build/state-machine-runtime-split-20260829-r2/`；App/A/B、Boot、PIO
  头文件、OTA package 和 Flash link contract 均通过，`build_id=20260829131051`。
- 验证：状态机资源 pytest `8 passed`；尚未执行异步 OTA、四板 HIL、NO5 观测和 SD
  波形验收。
- 结论：flight admission 已覆盖声明的 PIO/SM/DMA/GPIO/IRQ/DREQ 类资源；maintenance
  persona 的统一 arbiter 接入和板端资源冲突负测试仍未完成，SM-RES-002/007 保持
  `IN PROGRESS`。

### SM-PROGRESS-20260829-009 — calibration persona directional unload

- TODO task ID：`SM-RES-006`、`SM-RES-007`。
- 日期：2026-08-29。
- 变更：校准异步切换器现在按当前 flight persona 检查 TX/RX 两个 PIO block 的 SM
  enable 状态和 DMA busy 状态；origin 卸载 5 个已加载的方向化原语，follower/process
  follower 卸载 4 个原语，包含独立 DATA capture。卸载完成后释放 flight SM/resource
  claim，再进入 maintenance calibration persona，避免残留程序或旧 PIO2 地址被复用。
- 构建：`out/build/state-machine-runtime-split-20260829-r1/`；PIO 头文件、App/A/B、
  Boot、Flash link contract 和 OTA package 生成通过，`DHRT100_UPDATE.pkg` 的
  `build_id=20260829130150`。
- 验证：定向状态机/TRN-03 pytest `95 passed`；全量 pytest `540 passed, 1 skipped`，
  结果缓存位于 `out/pytest/state-machine-runtime-split-20260829-r1/`；状态机资源工具
  通过。跳过项为未启用硬件端口的四板 HIL，尚无异步 OTA、四板 HIL、NO5 观测和 SD
  波形证据。
- 结论：校准切换的方向化卸载回归已在 host/build 层收敛；SM-RES-006/007 仍保持
  `IN PROGRESS`，不得据此宣称板端迁移完成。

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

### SM-PROGRESS-20260829-007 — follower forward/capture FIFO 分离

- TODO task ID：`SM-RES-005`、`SM-M4`。
- 日期：2026-08-29。
- 变更：新增 `tdma_pio_spi_flight_data_capture` PIO 原语，使用 TX PIO 的
  `DATA_IN_CAPTURE_SM` 独立采样返回 DATA；raw/process follower 的 wire forward SM
  不再向自身 RX FIFO push，避免 forward 与 capture 双消费者竞争同一 FIFO。RX ring DMA
  改由专用 capture SM 的 FIFO/DREQ 提供数据。
- 构建：`out/build/state-machine-runtime-split-20260829/`；PIO header、App/A/B、Boot、
  Flash link contract 和 OTA package 生成通过。
- 当前事实：独立 SM/FIFO 已落地到 flight code；DMA channel 静态 endpoint 负测试、异步
  OTA、四板 HIL、NO5 观测和 SD 波形验收尚未执行，因此 SM-RES-005/SM-M4 仍为
  `IN PROGRESS`。

### SM-PROGRESS-20260829-008 — 方向化回归与 capture patch 校正

- TODO task ID：`SM-RES-005`、`SM-RES-006`、`SM-RES-007`。
- 日期：2026-08-29。
- 变更：修正 `tdma_pio_spi_flight_data_capture_program_init()` 对独立 capture 原语
  的指令地址映射，使 CS gate、SCK rising WAIT、SCK falling WAIT 分别写入实际的
  `offset+0/+2/+4`；新增静态检查，禁止 raw/process forward SM 恢复 `PUSH`，并要求
  capture 原语具备独立 `IN/PUSH/CS gate`。更新 TRN-03 closed-loop 断言以验证
  control/data PIO helper，而非迁移前的旧 PIO2 符号。
- 构建：`out/build/state-machine-runtime-split-20260829/`；PIO header、App/A/B、Boot、
  Flash link contract 和 OTA package 生成通过。
- 验证：全量 pytest `537 passed, 1 skipped`；状态机资源与 TRN-03 关键回归 `93 passed`；
  `test_state_machine_resource_check.py` 已包含 forward FIFO 和 capture patch 负测试。
  跳过项是未启用硬件端口的 HIL，不构成 OTA/HIL 证据。
- 当前事实：静态回归和方向化 flight runtime 继续收敛；DMA/DREQ/GPIO/persona epoch
  完整负测试、异步 OTA、四板 HIL、NO5 观测和 SD 波形验收仍未执行，SM-RES-002/003/004/
  005/006/007 和 SM-M2/SM-M3/SM-M4 继续保持 `IN PROGRESS`。

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
| SM-PROGRESS-20260829-001 | SM-RES-001 | 本文档与 `HAOFV_STATE_MACHINE_ARCHITECTURE.md`；暂无板端证据。 |

## 失败与回退

当前没有新的构建或板端失败记录。迁移过程中若出现资源冲突、PIO FIFO 竞争、DMA
stall、TDMA deadline/CRC/bitmap/WKC 回归，必须停止新 persona，恢复最近的已验证
TDMA 方向配置并在本文件追加失败证据；不得在运行态回退到未声明的复合 TX/RX 资源。
