# Distributed RefMem 内部主域任务进度（归档 05）

Status: Frozen
Domain: REFMEM
Canonical: `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_05.md`
Related: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Last updated: 2026-10-26

本文件接收 `REFMEM_TASK_PROGRESS.md` 轮转出的最旧条目（C14）；条目保持原文与倒排顺序。

### REFMEM-TASK-20260815-016 - Quality gate RUN gate integration

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 `refmem_quality_evaluate_deployment_gate()` 接入产品 config RUN gate。
  - 保持 `SystemManager` 只消费 `DistributedRefMemAO` 的 quality gate 结果，不直接读取 TDMA mailbox 或 physical adapter counter。
- 完成内容：
  - `DistributedRefMemAO` 增加 `distributed_refmem_quality_gate_ready()`，内部从 core1 TDMA snapshot 派生 runtime quality entry，并调用 quality gate evaluator。
  - `system_manager` 的 config gate 同时检查 distributed config、SlotClaim gate 和 RefMem quality gate；任一失败时 `ready=0`、`gate_state=2`、ACK 清空/NACK target mask。
  - 修复 `distributed_refmem.h` 与 `refmem_quality.h` 的 include 循环，公开 API 只暴露布尔 quality gate，不泄漏 quality table 内部类型。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815053147`，package CRC `0x190D53A9`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815053147"`。
  - 正向：COM5/COM6 查询 `SYSTem:CONFigure:STAT?` 均返回 `ready=1, gate_state=1, target_mask=15, ack_flags=15, nack_flags=0`。
  - 负向：COM5 执行 `SYSTem:REFMEM:SYNC:TDMA:RX 1000,25000000,16,17,18,23` 且不发 TX 后，`TDMA:STATus?` 显示 `state=5, timeout_count=1, last_result=3, last_error=3`，`SYSTem:CONFigure:STAT?` 变为 `ready=0, gate_state=2`。
  - 恢复：COM5 重新 OTA 同一 package 后恢复 `ready=1, gate_state=1`；COM6 保持 `ready=1, gate_state=1`。
- 后续闭环：
  - quality gate 负向流程已在 `REFMEM-TASK-20260815-017` 固化为 HIL 脚本。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/system_manager/src/system_manager.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`

### REFMEM-TASK-20260815-015 - Quality gate evaluator

- 状态：完成基础件，SystemManager 接入待继续
- 日期：2026-08-15
- 任务目标：
  - 在 TDMA runtime quality entry 已进入 `DistributedConnectionQualityTable` 派生视图后，提供表驱动的 `DeploymentGate.QUALITY` 消费入口。
  - 避免 RUN gate 直接遍历 adapter、core1 mailbox 或 SCPI 查询结果。
- 完成内容：
  - `refmem_quality.h/.c` 增加 `refmem_quality_gate_threshold_t`、`refmem_quality_gate_reason_t` 和 `refmem_quality_evaluate_deployment_gate()`。
  - evaluator 输入 `refmem_quality_runtime_table_t` 和阈值，输出 `refmem_deployment_gate_entry_t`，包含 `check_id=REFMEM_APP_GATE_QUALITY`、pass/reject 状态、reject reason、node/slot 和 evidence index。
  - 当前 reason 覆盖 bad argument、empty runtime table、CRC、stale、late、drop、timeout 和 last_error。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 明确 `DeploymentGate.QUALITY` 只能消费 quality evaluator 输出，不能直接读取底层 adapter 或 core1 mailbox。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815052521`，package CRC `0x346FB00F`。
- 后续闭环：
  - `system_manager` config RUN gate 接入和 TDMA timeout 板端负向验证已在 `REFMEM-TASK-20260815-016` 完成。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `tests/unit/test_refmem_quality.c`

### REFMEM-TASK-20260815-014 - TDMA quality runtime mapping

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 core1 realtime TDMA service 的 window timeout、DMA/physical overrun、missed window 和 physical adapter error 纳入 `DistributedConnectionQualityTable` 派生运行态视图。
  - 保持 active static `ConnectionQualityTable` 只作为契约/CRC 来源，不被 SCPI 或维护 bridge 热写。
- 完成内容：
  - `refmem_quality.h/.c` 增加 TDMA service runtime entry，`quality_id=0x54444D41`，scope 为 `REFMEM_APP_QUALITY_TRANSPORT_ADAPTER`。
  - 映射规则：TDMA `timeout_count -> timeout_count`，`reject_count -> late_count`，`overrun_count -> drop_count`，`last_error -> last_error`，`intent_seq/completed_seq -> seq_expected/seq_last`。
  - `SYSTem:REFMEM:QUALity?` 查询时读取 `distributed_refmem_get_realtime_tdma()` snapshot，并把 TDMA entry 放在 index 1；index 0 仍为本地 PIO SPI adapter 诊断 entry，后续 remote QUALITY entry 依次追加。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 补齐 `TRANSPORT_ADAPTER` scope 和 TDMA 派生映射规则。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_quality_tests.ps1` 通过 host GCC 断言执行。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 host GCC 断言执行。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815051252`，package CRC `0x5B12BF64`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815051252"`。
  - COM5/COM6 查询 `SYSTem:REFMEM:QUALity? 1` 可见 TDMA runtime entry；HIL 前 entry_count 为 2，HIL 后 entry_count 为 3（追加 remote QUALITY entry）。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815051252_tdma_quality_mapping` 通过。
- 还需完成：
  - 将 TDMA quality entry 接入 DeploymentGate/RUN gate evidence 消费规则。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_quality.h`
  - `components/distributed_refmem/src/refmem_quality.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_quality.c`

### REFMEM-TASK-20260815-013 - P4.5 SPI frame legacy removal

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 在 TDMA HIL 通过后删除 `SYSTem:REFMEM:SYNC:SPI:*` 的帧级阻塞命令，避免后续继续通过 core0/SCPI 直驱 RefMem frame transport。
  - 保留 `SPI:ARM/DISarm/STATus?/LINE:*/RAW:*` 作为 bring-up 诊断入口。
- 完成内容：
  - 从 SCPI 命令表删除 `SYSTem:REFMEM:SYNC:SPI:RX?`、`SPI:HELLo`、`SPI:EPOCh`、`SPI:DELTa`、`SPI:ACK`、`SPI:FENCe`、`SPI:QUALity:FRAMe`。
  - 删除上述命令实现和 `scpi_refmem_sync_spi_send_result()`，消除 core0 直发 RefMem frame 的维护路径。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 改为 TDMA-only frame transport；RAW 仍作为线序/物理链路诊断。
  - HIL frame plan 改为直接使用 `SYSTem:REFMEM:SYNC:HELLo?/EPOCh?/DELTa?/ACK?/FENCe?/QUALity:FRAMe?` 生成 frame，再通过 `TDMA:TX/RX/FRAMe?` 完成两板交换。
- 验证结果：
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815045059`，package CRC `0x732BD11F`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815045059"`。
  - Negative check：`SYSTem:REFMEM:SYNC:SPI:HELLo` 和 `SYSTem:REFMEM:SYNC:SPI:RX?` 已返回 `Undefined header`，确认旧 SPI frame 入口不再注册。
  - Positive check：`SYSTem:REFMEM:SYNC:SPI:ARM`、`SYSTem:REFMEM:SYNC:SPI:RAW:TX` 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 仍可用。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815045059_tdma_spi_frame_removed` 通过。
  - HIL 报告路径：`build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815045059_tdma_spi_frame_removed\refmem_spi_hil_report.json`。
- 后续闭环：
  - TDMA window timeout、DMA overrun、missed window 和 physical adapter error 到 `DistributedConnectionQualityTable` 的正式映射已在 `REFMEM-TASK-20260815-014` 完成。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`

### REFMEM-TASK-20260815-012 - P4.5 COM5/COM6 core1 TDMA HIL 闭环

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 将 RefMem Sync 的两板物理验证从 SCPI 阻塞帧级命令收敛到 core1 realtime TDMA service。
  - 证明 PC 只投递 RX/TX intent 和读取 snapshot，`HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 帧通过真实 PIO+DMA 物理链路完成。
- 完成内容：
  - 修正 `SYSTem:REFMEM:SYNC:TDMA:TX`：SCPI 层只解码 hex 并提交 TDMA intent，不再在 core0 直接调用 physical adapter 发送帧。
  - 固化 HIL 脚本的 TDMA hex 字符串参数，避免未加引号的 A-F hex 被 SCPI lexer 当作数字/指数解析。
  - 将 `refmem_spi_physical_adapter_receive()` 拆成 begin/poll 异步 RX 基础能力；blocking receive 保持兼容并复用同一解析逻辑。
  - 将 TDMA RX 从长时间阻塞 core1 改为每轮 core1 loop poll 一次，`PENDING` 时保持 ARMED/ACCEPTED 快照，不提前写 completed_seq。
  - 将 `REFMEM_REALTIME_TDMA_FRAME_MAX` 对齐到 RefMem Sync 协议 MTU：`REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX`。
  - HIL 脚本在 TDMA exchange 前清理两端上一次 intent，并用 `TDMA:STATus?` 做 receiver arm gate。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815043100`，package CRC `0x49E7D77C`。
  - COM5/COM6 OTA 提交通过，均返回 `SYSTem:FW:BUILD? => "20260815043100"`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815043100_tdma_mtu` 通过。
  - HIL 报告路径：`build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815043100_tdma_mtu\refmem_spi_hil_report.json`。
  - 报告中 `A_RAW_B`、`B_RAW_A`、双向 `HELLO/EPOCH/DELTA/ACK/FENCE/QUALITY` 全部 passed；最终 TDMA snapshot 显示 A/B `ready_count=12`、`timeout_count=0`、`overrun_count=0`、`reject_count=0`。
- 还需完成：
  - 将 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令降级为 legacy/diagnostic 或删除，仅保留 line/raw bring-up 必要入口。
  - 将 TDMA window timeout、DMA overrun、missed window 和 physical adapter error 正式映射到 `DistributedConnectionQualityTable`。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
- 下一步：
  - 收敛 `SYSTem:REFMEM:SYNC:SPI:*` 帧级阻塞命令，并把 TDMA 质量计数并入 RefMem quality/evidence。

### REFMEM-TASK-20260815-011 - P4.5 core1 TDMA service contract 收敛

- 状态：完成 service contract、physical ops 绑定与构建闭环；SCPI 维护路径迁移和 HIL 待继续
- 日期：2026-08-15
- 任务目标：
  - 将当前 PIO+DMA physical adapter 从“只有 SCPI 维护命令能表达状态”向 HAOFV 的 RefMemAO/core1 realtime TDMA service 边界收敛。
  - 先建立 core0 intent 与 core1 result 的字段归属，避免跨核共享状态双 writer。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_realtime_tdma.h` 和 `components/distributed_refmem/src/refmem_realtime_tdma.c`。
  - service 使用双 guard：core0 writer 只更新 intent mailbox、frame staging 和 reject counter；core1 writer 只更新 runtime/result snapshot、completed seq、ready/timeout/overrun/error counter。
  - TDMA service 增加 physical ops 边界，避免把具体总线实现写死在基础状态机中；host 单测使用 fake ops，固件使用 PIO+DMA physical adapter wrapper。
  - `DistributedRefMemAO` 绑定 `refmem_spi_physical_adapter_transmit/receive`，core1 service 可按 intent role、baud 和 deadline 调用真实 PIO+DMA TX/RX。
  - TDMA service 增加 RX result frame 缓存和只读 API；收到的 frame 不写入向量表大数据，后续由 RefMem Sync owner 读取并进入协议 decode/quality。
  - `distributed_refmem_init()` 初始化 TDMA service，`app_realtime_run_once()` 通过 `distributed_refmem_realtime_run_once()` 在 core1 循环中推进。
  - 新增维护查询 `SYSTem:REFMEM:SYNC:TDMA:STATus?`，只读取 snapshot，不直接驱动硬件动作。
  - 新增维护入口 `SYSTem:REFMEM:SYNC:TDMA:TX/RX/FRAMe?/ABORt`：`TX/RX` 只 post intent，`FRAMe?` 读取 core1 result frame 并交给 RefMem Sync receive state machine。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 增加 `--transport tdma`，在现有线序 remap 和 frame plan 基础上改走 TDMA intent 路径；RAW 仍保留为 SPI line diagnostic。
  - 新增 `tests/unit/test_refmem_realtime_tdma.c` 和 `tools/tests/run_refmem_realtime_tdma_tests.ps1`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 host gcc 断言执行。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815040106`，package CRC `0xE67D0C6D`。
- 后续：
  - 用 COM5/COM6 执行 `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --transport tdma --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_tdma`。
  - TDMA HIL 通过后再收敛或删除 `SYSTem:REFMEM:SYNC:SPI:*` 的帧级阻塞命令。

### REFMEM-TASK-20260815-010 - P4.5 PIO+DMA physical adapter 25 MHz 闭环

- 状态：完成当前 HIL；core1 TDMA service 化待继续
- 日期：2026-08-15
- 任务目标：
  - 按 VDC/TDMA 架构纠偏 P4.5 physical adapter：不得使用 RP2350 内置 SPI，也不得把 SIO bitbang 当作闭环结论。
  - 使用真实线序检测结果驱动 PIO SPI pin plan，在 COM5/COM6 两块最小系统板之间完成真实物理链路 RefMem Sync 帧闭环。
  - 目标速率提升到 25 MHz，避免继续以低速 PC 搬运或逐字节轮询掩盖架构问题。
- 完成内容：
  - 新增 `components/distributed_refmem/src/refmem_spi_physical.pio`，提供 PIO TX byte 和 PIO RX byte state machine。
  - `refmem_spi_physical_adapter.c` 从内置 `spi0`/SIO bitbang 纠偏为 PIO TX、PIO RX 和 DMA RX 缓冲；CPU 不再逐 bit 或逐字节搬运 RX FIFO。
  - `board_config.h` 增加 RefMem PIO transport 的 PIO/SM 资源定义，默认 baud 改为 25 MHz。
  - `tools/sync/two_board_io_validate/two_board_io_validate.py` 支持 auto/detect 模式，并把 observed remap 写入报告。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 默认使用 auto remap 和 25 MHz，根据实测 OUT->IN map 推导双向 PIO SPI pin plan。
  - 新增 `tools/refmem/refmem_spi_line_activity/refmem_spi_line_activity.py`，用于慢速长 RAW:TX 时观察对端输入 mask，证明 PIO TX 已在真实线束上活动。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，build `20260815031915`，package CRC `0xD5A25528`。
  - COM5/COM6 均 OTA 写入、boot 并 commit 到 build `20260815031915`。
  - `python tools\refmem_spi_line_activity\refmem_spi_line_activity.py --sender COM5 --receiver COM6 --sender-to-receiver 1,2,0,3 --baud 1000 --count 256 --seed 255` 显示 B 侧 IN0/IN2 有高低变化，证明 PIO TX 的 RX/SCK 已过线。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --baud 1000000 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_1m` 通过。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815031915_pio_dma_25m` 通过。
  - 25 MHz 报告记录：线序 preflight PASS，A->B remap `[1,2,0,3]`，B->A remap `[2,1,0,3]`；`RAW/HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 14 个 exchange 全部 PASS；`failures=[]`。
- 还需完成：
  - 当前 HIL 仍由 SCPI 在帧级触发 TX/RX。产品化下一步必须把 PIO+DMA physical adapter 接入 core1 realtime TDMA service，core0 只配置窗口和帧意图，core1 发布 frame-ready/timeout/overrun snapshot。
  - PIO/SM/DMA 资源需要接入 ResourceArbiter 和 RealtimeCapabilityContract，避免维护 HIL 直接抢占现有 `sync_io` 资源。
- 关联文件：
  - `components/distributed_refmem/src/refmem_spi_physical.pio`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `boards/rp2350_trig/inc/board_config.h`
  - `tools/sync/two_board_io_validate/two_board_io_validate.py`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `tools/refmem/refmem_spi_line_activity/refmem_spi_line_activity.py`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`

### REFMEM-TASK-20260815-009 - P4.5 SPI physical adapter 首次接入与线级阻塞定位

- 状态：部分完成，HIL 阻塞在物理线级 preflight
- 日期：2026-08-15
- 任务目标：
  - 按 P4.5 要求，把 RefMem Sync 从 PC/SCPI hex 搬运推进到真实两板 SPI 物理链路。
  - 保持 HAOFV 边界：SCPI 只作为维护触发和诊断入口，RefMem frame 仍由 RefMem Sync validate/receive 处理，不让 Vector 承载数据。
  - 在 COM5/COM6 两块最小系统板上验证 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 真实过线。
- 完成内容：
  - 新增 `refmem_spi_physical_adapter.h/.c`，使用 `spi0` 的 debug bring-up profile：RX16、CS17、SCK18、TX19，默认 500 kHz。
  - `SYSTem:REFMEM:SYNC:SPI:*` 增加 ARM/DISarm/STATus、frame TX/RX、raw byte TX/RX 和 line release/drive/status 维护入口。
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 固化三层验证：先扫线，再 raw byte，再 RefMem frame；PC 只触发 TX/RX，不搬运 frame hex。
  - build `20260815022459` 已通过 OTA 写入并在 COM5/COM6 上 commit，双板 `SYSTem:FW:BUILD?` 均返回该 build。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，package CRC `0xAE0FFEB2`。
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - OTA COM5/COM6 正常进入 `READY_TO_REBOOT` 并完成 `SYSTem:OTA:COMMit`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_COM5_COM6_20260815022459_linefull` 失败在 line preflight。
  - 六条线级检查均读 0：A_CS_B、A_SCK_B、A_TX_B_RX、B_CS_A、B_SCK_A、B_TX_A_RX。因此当前不是 RefMem frame/CRC/target 语义问题，也不是 raw SPI 负载问题，而是 GPIO16-19 互联未成立或 COM5/COM6 对应板未按该线序连接。
- 下一步：
  - 按 playbook 接线后重跑同一 HIL：`SCK18-SCK18`、`CS17-CS17`、`A_TX19 -> B_RX16`、`B_TX19 -> A_RX16`、`GND-GND`。
  - 线级 preflight 通过后，再进入 raw byte 和 `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` frame 层闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_spi_physical_adapter.h`
  - `components/distributed_refmem/src/refmem_spi_physical_adapter.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

### REFMEM-TASK-20260815-008 - Host GCC 单元测试断言门禁

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 使用 `D:\Embedded\GCC\mingw64\bin\gcc.exe` 让现有 C 单元测试在 host 上真实执行断言。
  - 纠正此前多处测试脚本在无 host C 编译器时只做 ARM GCC compile-only 的验证口径。
  - 将该门禁作为继续推进 RefMem 表模型、S0 flash/core1、真实 transport 和 PIO 预约输出前的基础验证。
- 完成内容：
  - 新增 `tools/tests/run_host_unit_tests.ps1`，默认将 `D:\Embedded\GCC\mingw64\bin` 加入 `PATH`，顺序运行 BiSS、portable LOG/OTA 和 RefMem 相关测试脚本。
  - 修正 `run_refmem_application_contract_tests.ps1` 的链接源，补入 `refmem_slot_claim.c`。
  - 修复 `refmem_claim_propose_frame_init()` 在设置 `payload_count` 前计算 payload CRC 的协议 bug。
  - 扩大 `test_refmem_table_registry.c` 的测试构包容量，避免当前 `.rmtp` P0-P3 表 payload 超过旧 `1536` 字节缓冲导致 host 栈破坏。
  - 修正 `test_portable_ota_core.c` 的 package-mode 测试夹具，使 header package CRC 字段保持与当前 packager 一致的 `0` 语义。
  - 修正 `test_biss_protocol.c` 中 CRC6 golden vector 的参与位拼接，改为明确的 18-bit payload/status 和 24-bit payload 向量。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1` 通过。
  - S0 补入 Flash/Core1 lockout 断言后，覆盖 13 个测试脚本、18 个 C 单元测试文件，期望输出 `host unit test scripts passed: 13/13`。
  - Host GCC：`D:\Embedded\GCC\mingw64\bin\gcc.exe`。
- 还需完成：
  - 下一轮优先进入 S0：Flash erase/program 前 core1 park/lockout/RAM-resident 入口完整握手和故障注入。
  - 然后接入真实最小 physical transport，让 `DELTA/ACK/FENCE` 在两板真实链路上跑通。
  - `ModelTurntableAO` 需要补真实 PIO scheduled fire 输出路径，验证“到点出边沿”。
- 关联文件：
  - `tools/tests/run_host_unit_tests.ps1`
  - `tools/tests/run_refmem_application_contract_tests.ps1`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `tests/unit/test_refmem_table_registry.c`
  - `tests/unit/test_portable_ota_core.c`
  - `tests/unit/test_biss_protocol.c`
  - `docs/validation/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 不继续扩大 RefMem 静态表模型，先按 `REFMEM_DOMAIN_TODO.md` 的近期主线完成 S0、真实 transport 和 PIO 预约输出闭环。

### REFMEM-TASK-20260815-007 - RMTP NodeLoadTable 真实表镜像

- 状态：完成
- 日期：2026-08-15
- 任务目标：
  - 继续 P0 逐表真实化，将 `.rmtp` table 3 `NodeLoadTable` 从占位 payload 升级为固定 u32 表镜像。
  - 让 NodeLoad 的基础字段合法性由公共 application contract 校验，为后续 `LOAD:NODE` staging image 和动态实例装载做准备。
- 完成内容：
  - `tools/refmem/refmem_table_image/refmem_table_image.py` 新增 `build_node_load_payload()`，按当前 11 条默认 NodeLoad 模板输出固定 u32 payload。
  - `refmem_application_contract.h/.c` 新增 `refmem_application_contract_validate_node_load_table()`，校验 version、load count、load id、application/profile、node 范围、instance 范围、fail policy、enabled/required 和 target node mask。
  - `refmem_application_model.c` 的静态 NodeLoad linter 复用公共 contract，并保留对当前内置 FB instance 表的实例存在性、重复 enabled instance 和 required enabled instance 覆盖检查。
  - `refmem_table_registry.c` 新增 NodeLoad parser，并在 `.rmtp` package owner validation 中把 table 3 纳入 owner-validated mask。
  - `tests/python/test_refmem_pack_build.py` 增加 table 3 size/count 断言；`tests/unit/test_refmem_table_registry.c` 增加 NodeLoad package 构造和 owner mask/staging 状态断言。
- 验证结果：
  - `python -m pytest tests\python\test_refmem_pack_build.py` 通过，2 passed。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_application_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_table_registry_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\refmem_pack_build\refmem_pack_build.py --output-dir build-rtos-multicore-smoke\refmem_pack_nodeload` 通过，生成 `.rmtp` size `1924`、CRC `3A20B868`，table 3 CRC `7D7762D1`。
  - `python tools\sd_fs_build\sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke\sdcard_nodeload --clean` 通过。
  - 独立打包输出与 SD staging 输出的 `/refmem/app_model.rmtp` SHA256 均为 `C2EC768B52E284403D655BF60309E5AE102AE974909763A3B078B5FA79CE75CD`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814172116`，package CRC `0x5B9C1C34`。
- 还需完成：
  - 继续将 FbInstance、EventLink、DataLink、DeploymentGate 和 ConnectionQuality 五张表升级为真实表镜像。
  - `SYSTem:REFMEM:LOAD:NODE` 仍需从单条候选 snapshot 升级为真正的 NodeLoad staging image；本轮只是让 RMTP package 中的完整 NodeLoad 表具备 canonical payload 和 contract。
- 关联文件：
  - `tools/refmem/refmem_table_image/refmem_table_image.py`
  - `tools/refmem/refmem_pack_build/refmem_pack_build.py`
  - `components/distributed_refmem/inc/refmem_application_contract.h`
  - `components/distributed_refmem/src/refmem_application_contract.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tests/python/test_refmem_pack_build.py`
  - `tests/unit/test_refmem_table_registry.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`

