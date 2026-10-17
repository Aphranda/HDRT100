# REFMEM 任务进度归档

Status: Frozen
Domain: refmem
Canonical: `docs/legacy/refmem/LEGACY_REFMEM_TASK_PROGRESS_02.md`
Related: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Last updated: 2026-10-17

## 归档说明

由 C14 轮转从 `docs/refmem/REFMEM_TASK_PROGRESS.md` 迁入，按时间正序保存最旧条目；未删除、未改写。

### REFMEM-TASK-20260814-039 - PIO SPI adapter RX staging 前置闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为 P4.5 阶段 2 的 `REFMEM_HELLO` 双向交换建立 adapter-level RX staging 基础。
  - 先验证完整 RefMem Sync frame 可以在 adapter 层完成接收注入、缓存、poll、计数和错误归因，不接真实 PIO FIFO/DMA。
- 完成内容：
  - `refmem_pio_spi_adapter_t` 增加单帧 RX staging buffer、`rx_frame_size` 和 snapshot `rx_pending`。
  - 新增 `refmem_pio_spi_adapter_inject_rx_frame()`，作为后续 PIO/DMA RX ISR 或 HIL loopback 的受控入口。
  - `send` 和 `inject_rx_frame` 均改为调用 `refmem_sync_frame_validate()`，在 transport 边界校验 payload CRC。
  - `poll` 支持取出 pending RX frame，更新 `rx_count`、`last_rx_size`、`rx_pending` 和 `last_error`。
  - 单元测试增加 HELLO frame 注入/轮询、timestamp 保存、rx pending 清除和坏 payload CRC 拒绝路径。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x39B8BCC8`，build id 为 `20260814103131`。
  - 本轮未烧录板端，未运行 COM3/COM4 HIL；真实两板 `HELLO` 双向交换仍未完成。
- 还需完成：
  - 定义 adapter TX/RX service 的 ownership，明确 `task_refmem_sync`、core1 realtime path 和后续 PIO/DMA ISR 的边界。
  - 增加 `REFMEM_HELLO` build/layout/app/config/capability bundle 生成 helper。
  - 将 HELLO send/poll 接到 RefMem Sync 状态机，并进入两板 HIL 验证。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_pio_spi_adapter.h`
  - `components/distributed_refmem/src/refmem_pio_spi_adapter.c`
  - `tests/unit/test_refmem_pio_spi_adapter.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 `REFMEM_HELLO` bundle 生成和 adapter-level send/poll 接入，再规划两板 HIL。

### REFMEM-TASK-20260814-038 - PIO SPI adapter 能力契约映射

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将最小系统板 PIO SPI transport adapter 从“代码占位”升级为可被 `BoardCapabilityTable` 和 `RealtimeCapabilityContract` 表达的能力。
  - 保持默认业务 profile 不强制切换到 PIO SPI；PIO SPI 作为 bring-up adapter，可通过后续 board capability / instance 配置加载。
- 完成内容：
  - `refmem_application_model.h` 增加 `REFMEM_APP_IO_PIO_SPI_SYNC`，用于表达 PIO SPI 同步 adapter 的 IO 约束。
  - `refmem_application_model.h` 增加 `REFMEM_APP_IP_PIO_SPI_SYNC_DELTA`，用于表达 PIO SPI 承载 RefMem Sync delta 的类 IP 核能力。
  - `refmem_application_model.h` 增加 `REFMEM_APP_TRANSPORT_PIO_SPI`，用于后续 EventLink/adapter 选择。
  - `refmem_realtime_contract.c` 将 `PIO_SPI_SYNC` / `PIO_SPI_SYNC_DELTA` 映射到 `PIO + DMA + CORE1_RT` 能力。
  - 新增 `refmem_realtime_contract_transport_resource_claim()`、`refmem_realtime_contract_transport_io_claim()` 和 `refmem_realtime_contract_transport_ip_core_claim()`，统一由 transport 生成 resource/io/ip_core claim。
  - `refmem_application_model.c` 更新 transport linter 范围，并把 `PIO_SPI_SYNC` 纳入互斥 IO claim 检查。
  - 新增 `tests/unit/test_refmem_realtime_contract.c` 和 `tools/tests/run_refmem_realtime_contract_tests.ps1`。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x8FC6456B`，build id 为 `20260814102626`。
  - 本轮未烧录板端，未运行 COM3/COM4 HIL；变更仍属于能力契约和编译验证。
- 还需完成：
  - 将 `REFMEM_HELLO` 通过 PIO SPI adapter 完成双向交换。
  - 后续在 BoardCapability staging 或 System Pack 中增加可切换的 min-system PIO SPI profile。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/inc/refmem_realtime_contract.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `tests/unit/test_refmem_realtime_contract.c`
  - `tools/tests/run_refmem_realtime_contract_tests.ps1`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P4.5 阶段 2：在不绑定真实 PIO 时序的前提下，先建立 adapter-level `REFMEM_HELLO` loopback/queue 骨架。

### REFMEM-TASK-20260814-037 - PIO SPI transport adapter skeleton

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为两块最小系统板 RefMem Sync bring-up 建立首版 PIO SPI 风格 transport adapter skeleton。
  - 保持 adapter 层只承载总线收发、MTU、能力位、计数和错误快照，不绑定 RefMem active table，也不计算 VDC/DPLL。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_pio_spi_adapter.h`。
  - 新增 `components/distributed_refmem/src/refmem_pio_spi_adapter.c`。
  - 定义 adapter id、capability mask、max payload、preferred MTU、latency class、state 和 last error。
  - 定义 counters snapshot：tx/rx、tx reject、rx empty、bad frame、drop、timeout、last tx/rx size 和 optional RX timestamp。
  - 实现 `init`、`reset_counters`、`get_caps`、`get_snapshot`、`send` 和 `poll` 首版接口。
  - `send` 当前只验证 RefMem Sync frame header 和 payload size，并更新 tx/reject/drop/bad frame 计数。
  - `poll` 当前为空接收占位，返回 no frame 并更新 rx empty/last error；真实 PIO FIFO/DMA 接入留到后续阶段。
  - 新增 `tests/unit/test_refmem_pio_spi_adapter.c` 和 `tools/tests/run_refmem_pio_spi_adapter_tests.ps1`。
  - 将 `refmem_pio_spi_adapter.c` 加入根 `CMakeLists.txt` 固件源列表。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x6267900E`，build id 为 `20260814101953`。
  - 本轮未烧录板端，未运行 COM3/COM4 HIL；新增代码仍属于 transport adapter skeleton。
- 还需完成：
  - 定义 PIO SPI adapter caps 与 `BoardCapabilityTable` / `RealtimeCapabilityContract` 的映射关系。
  - 实现真实 PIO SPI 帧定界、TX/RX FIFO、DMA 或 IRQ service，以及可选 RX timestamp 采样。
  - 接入 `REFMEM_HELLO` 双向发送与接收，进入两板最小闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_pio_spi_adapter.h`
  - `components/distributed_refmem/src/refmem_pio_spi_adapter.c`
  - `tests/unit/test_refmem_pio_spi_adapter.c`
  - `tools/tests/run_refmem_pio_spi_adapter_tests.ps1`
  - `CMakeLists.txt`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 先补 PIO SPI adapter caps 到 BoardCapabilityTable / RealtimeCapabilityContract 的映射约束，再开始 `REFMEM_HELLO` 的 adapter-level loopback。

### REFMEM-TASK-20260814-006 - 总线无关 RefMem Sync frame 基础件

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 RefMem Sync Protocol 的首版固定帧头和基础 payload 落成总线无关代码基础件。
  - 保持 RefMem 协议层不绑定 BISS-C、PIO SPI、RJ45、UART 或 RS485，后续 adapter 只承载完整协议帧。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_sync_frame.h`。
  - 新增 `components/distributed_refmem/src/refmem_sync_frame.c`。
  - 定义固定线格式帧头：magic、version、frame_type、flags、payload_size、source_slot、target_mask、epoch、run、seq、ack_seq、compact_time、header CRC 和 payload CRC。
  - 定义首版 frame type：`HELLO/EPOCH/DELTA/COMMAND/ACK_NACK/FENCE/QUALITY`。
  - 定义 `HELLO`、`EPOCH`、`DELTA`、`COMMAND`、`ACK_NACK`、`FENCE` 和 `QUALITY` 的基础 payload 结构。
  - 使用显式 little-endian encode/decode，避免直接发送 C struct padding。
  - 将 `refmem_sync_frame.c` 加入根 `CMakeLists.txt` 固件源列表。
  - 新增 `tests/unit/test_refmem_sync_frame.c` 和 `tools/tests/run_refmem_sync_frame_tests.ps1`。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x434F3DD1`，build id 为 `20260814100501`。
  - 本轮未烧录板端，未运行串口 HIL；新增代码目前只作为总线无关协议帧基础件接入构建。
- 还需完成：
  - 建立 `refmem_sync.h/.c`，承载接收侧 validate/commit/visible 状态机。
  - 建立 PIO SPI transport adapter skeleton，并接入最小系统两板 HIL。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync_frame.h`
  - `components/distributed_refmem/src/refmem_sync_frame.c`
  - `tests/unit/test_refmem_sync_frame.c`
  - `tools/tests/run_refmem_sync_frame_tests.ps1`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 补齐 `COMMAND/ACK_NACK/FENCE/QUALITY` payload，随后进入 PIO SPI adapter skeleton。

### REFMEM-TASK-20260814-007 - RefMem Sync 接收状态机骨架

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在总线无关 frame 基础上新增 RefMem Sync 接收侧状态机骨架。
  - 保持接收侧只做 validate、quality 和进入 mirror/commit 前置判断，不直接写 64 KB active fact。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_sync.h`。
  - 新增 `components/distributed_refmem/src/refmem_sync.c`。
  - 定义 `refmem_sync_context_t`，保存本地 slot、active epoch/run、peer state 和 quality counters。
  - 定义接收结果：accepted、bad argument、frame invalid、source invalid、target mismatch、epoch mismatch、duplicate seq、stale seq。
  - 实现 `refmem_sync_receive_frame()`：调用 `refmem_sync_frame_validate()`，检查 source slot、target mask、epoch/run、duplicate/stale/gap，并更新 peer/quality counter。
  - 新增 `tests/unit/test_refmem_sync.c` 和 `tools/tests/run_refmem_sync_tests.ps1`。
  - 将 `refmem_sync.c` 加入根 `CMakeLists.txt` 固件源列表。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0xFDD8D052`，build id 为 `20260814101333`。
  - 本轮未烧录板端，未运行串口 HIL；新增代码目前只作为总线无关接收状态机骨架接入构建。
- 还需完成：
  - 将 validated frame 映射到 RefMem mirror/staging view。
  - 接入 `RefMemSlotContract`，对 delta field writer、宽度、值域和生命周期做真实校验。
  - 建立 PIO SPI transport adapter skeleton，并把 adapter quality 映射到 `DistributedConnectionQualityTable`。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_sync.h`
  - `components/distributed_refmem/src/refmem_sync.c`
  - `tests/unit/test_refmem_sync.c`
  - `tools/tests/run_refmem_sync_tests.ps1`
  - `docs/refmem/REFMEM_SYNC_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P4.5 阶段 1，建立 PIO SPI adapter skeleton 的 caps/counter snapshot。

## 记录规则

每条任务记录使用以下格式：

```text
### REFMEM-TASK-YYYYMMDD-NNN - 标题

- 状态：
- 日期：
- 任务目标：
- 完成内容：
- 验证结果：
- 还需完成：
- 关联文件：
- 下一步：
```

## 当前目标

RefMem Domain 当前目标是从 `components/distributed_refmem/` 的本地 64 KB 表骨架，升级为 HAOFV 内部基础主域：

```text
DistributedRefMemAO
+ RefMemSyncFB
+ DistributedVectorTable
+ 静态分布式应用模型
+ command / ACK-NACK
+ deployment gate
+ connection quality
```

当前实现已经落地 `refmem_vector_table.h/.c`、`refmem_application_model.h/.c`、静态模型 linter、package CRC 和 `SYSTem:REFMEM:LOAD:*` staging 骨架。下一阶段主线按以下顺序推进：

```text
RefMemTableRegistry
-> staging/active/rollbackable table image
-> SlotClaimMap + 16 candidate overflow evidence
-> RefMemSlotContract internal validation
-> command ACK/NACK
-> REFMEM_DELTA / REFMEM_EPOCH sync protocol
```

## 任务记录

### REFMEM-TASK-20260814-036 - 双板 PIO 可配置调试接线 profile

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为两块最小系统板组网验证建立方便接线且可自定义的 PIO 调试 profile。
  - 保持该接线只属于最小系统调试约束，不上升为产品板 pin map。
- 完成内容：
  - 新增 `PROJECT_SYNC_IO_INPUT_BASE_PIN` 和 `PROJECT_SYNC_IO_OUTPUT_BASE_PIN` 构建参数。
  - 当前默认 profile 为 `GPIO16..19` 输入、`GPIO21..24` 输出；后续可通过 CMake 切换整组 base pin。
  - `board_config.h` 从 active profile 派生 `TRIG_IN`、`TRIG_OUT`、`PULSE_OUT`、`RJ45_TRIG_IN/OUT` 等语义脚。
  - `sync_io_hw_profile` 的编译期断言改为检查连续 4 位、范围合法和输入/输出不重叠。
  - `trigger_fb` 默认触发源改为从 active profile 读取，不再硬编码 `GPIO16`。
  - `HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` 记录双板交叉接线表和 `GPIO12..15` 避让规则。
  - 增加 `REALtime:IO:PROFile?`、`REALtime:IO:INPut:LEVel?`、`REALtime:IO:OUTPut:MASK`、`REALtime:IO:OUTPut:MASK?` 和 `REALtime:IO:OUTPut:RELease` 维护接口。
  - 新增 `tools/sync/two_board_io_validate/two_board_io_validate.py`，逐位静态驱动两块板的 active output group 并读取对端 input mask，自动识别漏接、错位和短接。
  - `REFMEM_DOMAIN_TODO.md` 将双板 PIO 预检待办标记完成，后续真实两板 HIL 验证仍保留。
- 验证结果：
  - `python -m py_compile tools\two_board_io_validate\two_board_io_validate.py tools\realtime_scpi_validate\realtime_scpi_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260814083032`，package CRC `0x2071E6F5`。
  - `COM3` 和 `COM4` OTA 升级并 commit 到 build `20260814083032`。
  - `python tools\refmem_network_validate\refmem_network_validate.py --port-a COM3 --port-b COM4` 通过，双方 SlotClaimMap CRC 均为 `386979554`。
  - `python tools\two_board_io_validate\two_board_io_validate.py --port-a COM3 --port-b COM4` 首次按直通期望执行，测得方向性线序：A->B 为 `OUT0->IN1, OUT1->IN2, OUT2->IN0, OUT3->IN3`；B->A 为 `OUT0->IN2, OUT1->IN1, OUT2->IN0, OUT3->IN3`。
  - 自动线序检测工具默认改为按上述实测 logical remap 验收，保留 `--expect-a-to-b` / `--expect-b-to-a` 参数支持后续改线。
  - `python tools\two_board_io_validate\two_board_io_validate.py --port-a COM3 --port-b COM4 --out-dir build-rtos-multicore-smoke\two_board_io_COM3_COM4_remap` 通过，确认当前线束按 logical remap 可用。
- 还需完成：
  - 评估后续是否需要将 logical line remap 从工具参数升级到 RefMem/IO profile 表。
  - 后续产品板 profile 需要按产品板硬件约束设置独立构建参数，不沿用调试默认接线。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `components/sync_io/inc/sync_io_hw_profile.h`
  - `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`
  - `docs/sync/SYNC_IO_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 构建并烧录最小系统 profile 后，用 `SYSTem:REFMEM:*` 和后续 PIO 预检确认双板基础链路。

### REFMEM-TASK-20260814-035 - CLAIM_CONFLICT / RELEASE / RESOLVE 帧

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 补齐 SlotClaim 自组网协调消息族的静态帧定义。
  - 保持当前阶段仍为协议基础件，不接运行时 RJ45 收发。
- 完成内容：
  - `refmem_claim_protocol.h/.c` 增加 `refmem_claim_resolution_entry_t` 与 `refmem_claim_resolution_frame_t`。
  - `CLAIM_CONFLICT` 和 `CLAIM_RESOLVE` 共享 resolution entry 数组，覆盖 candidate、slot、board、assigned slot、claim_state、reason、evidence_id 和 claim_crc。
  - 增加 `refmem_claim_release_payload_t` 与 `refmem_claim_release_frame_t`，覆盖 slot、board、release_seq 和 claim_crc。
  - 增加 conflict/release/resolve 的 init/validate API。
  - SlotClaim 单元测试覆盖 payload CRC mutation、wrong frame type 和 bad payload count。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0x9623ECA9`。
- 还需完成：
  - 接入 RJ45_SYNC_RING 收发、epoch stale 检查、SlotClaimMap 聚合与 commit。
  - 将两板 baseline 工具升级为真实 `CLAIM_*` 组网验证。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_claim_protocol.h`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 定义 RefMem claim RX/TX staging queue 或 adapter，使协议帧能进入 RefMemAO 聚合，但仍不直接修改 active fact。

### REFMEM-TASK-20260814-034 - CLAIM_HELLO 与 CLAIM_COMMIT 帧

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 在 `CLAIM_PROPOSE` 之外补齐两板发现和提交所需的最小控制帧。
  - 继续保持协议基础件只做 init/validate，不接运行时收发。
- 完成内容：
  - `refmem_claim_protocol.h/.c` 增加 `refmem_claim_hello_frame_t` 和 `refmem_claim_commit_frame_t`。
  - `CLAIM_HELLO` payload 覆盖 board id、uuid、capability、IO/IP core、hw profile、active slot、loaded instance、baseline/VDC ready 和 claim CRC。
  - `CLAIM_COMMIT` payload 覆盖 SlotClaimMap CRC、slot/assigned/conflict/overflow/evidence 计数、committed node mask 和 gate_ready。
  - 协议实现抽出 header 初始化、header 校验和 raw payload CRC helper，保持 `CLAIM_PROPOSE` 外部 API 不变。
  - SlotClaim 单元测试增加 HELLO/COMMIT payload CRC 和 frame type 错误检测。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0x36309B39`。
- 还需完成：
  - 继续补 `CLAIM_CONFLICT/RELEASE/RESOLVE` 帧。
  - 接入 RJ45_SYNC_RING 收发、epoch stale 检查和 SlotClaimMap commit。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_claim_protocol.h`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加 conflict/release/resolve 帧，完成 SlotClaim 协调消息族的静态协议定义。

### REFMEM-TASK-20260814-033 - CLAIM_PROPOSE 帧协议基础

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为后续 RJ45 `CLAIM_*` 自组网协调提供可校验的帧格式基础。
  - 当前只定义和验证 `CLAIM_PROPOSE`，不接运行时发送/接收。
- 完成内容：
  - 新增 `refmem_claim_protocol.h/.c`。
  - 定义 claim frame header：magic、version、frame_type、claim_epoch、claim_seq、source board、payload_count、payload_crc32 和 header_crc32。
  - 定义 `refmem_claim_propose_frame_t`，最多携带 16 条 `SlotClaimProposal`。
  - 增加 `refmem_claim_propose_frame_init()` 和 `refmem_claim_propose_frame_validate()`。
  - SlotClaim 单元测试扩展 payload CRC、header CRC 和超 16 candidate 拒绝检查。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xEE4E9E15`。
- 还需完成：
  - 定义并实现 `CLAIM_HELLO/CONFLICT/RELEASE/RESOLVE/COMMIT` 帧。
  - 接入 RJ45_SYNC_RING 收发、epoch stale 检查和 SlotClaimMap commit。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_claim_protocol.h`
  - `components/distributed_refmem/src/refmem_claim_protocol.c`
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `tests/unit/test_refmem_slot_claim.c`
  - `tools/tests/run_refmem_slot_claim_tests.ps1`
  - `CMakeLists.txt`
- 下一步：
  - 将 claim frame 与 RefMem Sync frame/RMA delta 分层对齐，避免后续帧类型互相挤占。

### REFMEM-TASK-20260814-032 - 两板 RefMem baseline 验证工具骨架

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为后续两块最小系统板组网验证建立专用工具入口。
  - 当前只验证基础 SCPI snapshot，不假设 RJ45 `CLAIM_*` 协调协议已完成。
- 完成内容：
  - 新增 `tools/refmem/refmem_network_validate/refmem_network_validate.py`。
  - 工具显式管理两个串口生命周期：分别 open、settle、清输入/输出、查询、flush、close。
  - 单板 baseline 覆盖 `*IDN?`、`SYST:FW:BUILD?`、`SYST:CORE?`、`SYST:SYNC:VDC:STAT?`、`SYST:SYNC:VDC:DPLL:STAT?`、`SYST:CONFigure:STAT?`、`SYST:REFMEM:CLAIM? 0` 和 `SYST:REFMEM:CLAIM:EVIDence? 0`。
  - 双板比较默认要求 build id 和 SlotClaimMap CRC 一致，可通过 `--allow-build-mismatch` 或 `--allow-map-mismatch` 放宽。
  - 验证结果写入 `summary.json` 和 `summary.txt`。
- 验证结果：
  - `python -m py_compile tools\refmem_network_validate\refmem_network_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；两板工具未实际打开硬件串口。
- 还需完成：
  - 接入真实 RJ45 `CLAIM_*` 协调后，扩展重复 slot claim、缺失 UUID、stale、overflow、owner validation 拒绝和 commit 后 CRC 一致性验证。
- 关联文件：
  - `tools/refmem/refmem_network_validate/refmem_network_validate.py`
  - `tools/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加 SlotClaim stale/CRC 字段，再规划 RJ45 claim frame。

### REFMEM-TASK-20260814-031 - SlotClaimEvidence 诊断视图

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 SlotClaim 负向结果从计数升级为可查询 evidence，支撑两板组网冲突诊断。
- 完成内容：
  - `refmem_slot_claim_map_t` 增加 `evidence_count` 和最多 16 条 `refmem_slot_claim_evidence_t`。
  - duplicate claim、disabled slot、缺失稳定 UUID、invalid slot 和 active slot 容量 overflow 会记录 evidence；UUID/profile 不再作为 A slot 硬绑定依据。
  - 新增 `refmem_slot_claim_find_evidence()`。
  - 新增维护查询 `SYSTem:REFMEM:CLAIM:EVIDence? [evidence_id]`，不改变既有 `SYSTem:REFMEM:CLAIM?` 字段顺序。
  - SlotClaim 单元测试增加 evidence_count、reason、board_id 和 candidate_id 断言。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0x117B81F7`。
- 还需完成：
  - stale claim、claim CRC 和跨板 `CLAIM_*` 协调 evidence 仍待接入。
  - 两块最小系统板组网 HIL 需要验证双方 `SlotClaimMap CRC` 一致和 evidence 能闭环定位冲突来源。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加两板组网验证工具骨架，串口生命周期必须由统一 helper 管理。

### REFMEM-TASK-20260814-030 - SlotClaim 负向单元测试基础

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 先用纯 C 单元测试固定 SlotClaimMap / gate 的负向基础行为。
  - 为后续两块最小系统板组网验证建立可复用的算法判定地基。
- 完成内容：
  - `BoardCapabilityTable` 容量从 8 个 active slot 扩为 16 个候选容量，默认 active profile 的 `board_count` 仍保持 8，不改变当前板端默认行为。
  - `refmem_slot_claim_derive_map()` 增加 active slot 容量边界：candidate 数量超过 A0-A7 slot 数量后，后续候选进入 overflow，而不是被误判为普通重复 claim。
  - 新增 `tests/unit/test_refmem_slot_claim.c`，覆盖 nominal assignment、loaded instance mask、duplicate claim、缺失 UUID 和第 9 个候选 overflow。
  - 新增 `tools/tests/run_refmem_slot_claim_tests.ps1`，沿用现有纯 C 测试风格：有 host gcc/clang 时运行 exe，否则退化为 ARM GCC 编译检查。
  - `tools/README.md`、`tests/README.md` 和 `REFMEM_DOMAIN_TODO.md` 增加测试入口和两板组网验证待办。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
- 还需完成：
  - 增加 stale claim、claim CRC、任意 slot claim、9-16 全矩阵和超过 16 candidate rejected 的测试。
  - 后续在两块最小系统板上验证 `CLAIM_*` 协调消息、slot 冲突处理、RefMem snapshot 一致性和 VDC baseline。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `tests/unit/test_refmem_slot_claim.c`
  - `tools/tests/run_refmem_slot_claim_tests.ps1`
  - `tools/README.md`
  - `tests/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 补齐 SlotClaimEvidence 诊断视图，并把两板最小系统组网验证拆成 HIL 工具任务。

### REFMEM-TASK-20260814-029 - SlotClaim gate HIL 验证入口固化

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 把 SlotClaim 本地 RUN gate 的正向板端验证固化到标准工具和 pytest HIL。
  - 继续保持串口生命周期集中管理，pytest 默认不打开串口。
- 完成内容：
  - `tools/rtos/multicore_board_validate/multicore_board_validate.py` 增加 `test_refmem_slot_claim_gate()`。
  - 验证内容覆盖 `SYSTem:CONFigure:STAT?` ready/ACK/NACK、`SYSTem:REFMEM:CLAIM? 0/2/7` 的 map header、gate_ready、error counters、assignment、claim_state、online_required 和 claim_crc。
  - `ALL_TESTS` 加入 `refmem_slot_claim_gate`，CLI 全量验证会自动覆盖该项。
  - `tests/hil/test_multicore_board_validate.py` 将 `refmem_slot_claim_gate` 加入共享 `hil_serial` fixture 的只读 smoke；默认 pytest 仍跳过 HIL，不会自行打开串口。
  - `tools/README.md` 和 `REFMEM_DOMAIN_TODO.md` 同步记录验证入口。
- 验证结果：
  - `python -m py_compile tools\multicore_board_validate\multicore_board_validate.py tests\hil\test_multicore_board_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xB77FAB04`。
- 还需完成：
  - 板端实际运行 `python -m pytest -m hil --run-hil --hil-port COMx` 或 `python tools/rtos/multicore_board_validate/multicore_board_validate.py COMx --tests refmem_slot_claim_gate`。
  - 增加负向 SlotClaim 矩阵：重复 claim、缺失 UUID、stale、9-16 候选 overflow、超过 16 候选 rejected。
- 关联文件：
  - `tools/rtos/multicore_board_validate/multicore_board_validate.py`
  - `tests/hil/test_multicore_board_validate.py`
  - `tools/README.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加 SlotClaim 负向测试所需的 staging/fault injection 入口，避免直接修改 active profile。

### REFMEM-TASK-20260814-028 - SlotClaim 本地 RUN gate 接入

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 SlotClaimMap 首版结果接入 RefMem DeploymentGate 和系统 config RUN gate。
  - 让 claim 冲突、缺失 UUID、overflow、stale 和 required slot 缺失不只可查询，还能实际阻止 ready/RUN。
- 完成内容：
  - `refmem_slot_claim_assignment_t` 增加 `online_required`，用于区分 required slot 与 spare/dynamic slot。
  - 新增 `refmem_slot_claim_gate_status_t` 和 `refmem_slot_claim_gate_evaluate()`，输出 gate ready、first_bad_slot、first_reason、conflict/overflow/required_missing/mismatch 统计和 map CRC。
  - RefMem application model 的 `DeploymentGate` 静态验证接入 claim gate；本地 claim gate fail 时 model validation fail。
  - `system_manager` config gate 接入 claim gate；分布式配置 CRC 合法但 claim gate fail 时，`SYSTem:CONFigure:STAT?` 的 `ready=0`、`gate_state=2`、`nack_flags=target_mask`。
  - `SYSTem:REFMEM:CLAIM? [slot_id]` 返回值扩展 gate 字段和 `online_required` 字段，便于解释 RUN gate 拒绝原因。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xCF7DDAB2`。
- 还需完成：
  - 板端/HIL 验证 `SYSTem:REFMEM:CLAIM?` 与 `SYSTem:CONFigure:STAT?` 的 gate 行为一致。
  - 增加第 9 到第 16 个候选的 overflow evidence，并接入 DeploymentGate evidence。
  - 接入 RJ45 `CLAIM_*` 协调消息后的跨板 claim gate。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/system_manager/src/system_manager.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 增加 SlotClaim gate 的 Python/板端验证脚本，然后推进 overflow evidence 和 RJ45 claim 协调。

### REFMEM-TASK-20260814-027 - SlotClaimMap 首版本地派生

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 建立 SlotClaimMap 首版代码组件，把 B0-Bx board/profile 节点映射到 A0-A7 resolved assignment。
  - 让 RealtimeCapabilityContract 使用 SlotClaimMap resolved assignment，不再依赖 `active_default_slot` 直接查 BoardCapability。
- 完成内容：
  - 新增 `refmem_slot_claim.h/.c`，定义 `SlotClaimProposal`、`SlotClaimAssignment` 和 `SlotClaimMap` 首版结构。
  - `refmem_slot_claim_derive_map()` 从 GenericNode、BoardCapability、NodeLoad 和 FB instance 派生本地 claim map，记录 candidate_count、assigned_count、conflict_count、overflow_count、disabled_count、loaded_instance_mask、claim state、reason 和 CRC。
  - `refmem_realtime_contract_derive_from_claim_map()` 接入 SlotClaimMap resolved assignment，application model linter 已改用该入口验证资源、IO 和类 IP 核能力。
  - 增加 `SYSTem:REFMEM:CLAIM? [slot_id]` 维护查询，返回 map 摘要和指定 A slot assignment。
  - 文档同步 SlotClaimMap 首版能力边界：当前从 active default profile 本地派生，`claim_epoch=1`；RJ45 `CLAIM_*` 自组网消息、overflow evidence 和 DeploymentGate 接入仍是后续项。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xAFB84E5B`。
- 还需完成：
  - 接入 RJ45 `CLAIM_HELLO/PROPOSE/CONFLICT/RESOLVE/COMMIT` 协调消息。
  - 增加第 9 到第 16 个未分配候选的 overflow evidence，并将未解决冲突接入 DeploymentGate node_check。
  - 增加 HIL 验证：`SYSTem:REFMEM:CLAIM?` 与 BoardCapability、NodeLoad 和 realtime contract 结果一致。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_slot_claim.h`
  - `components/distributed_refmem/src/refmem_slot_claim.c`
  - `components/distributed_refmem/inc/refmem_realtime_contract.h`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 将 SlotClaimMap 接入 DeploymentGate/RUN gate，先让本地未解决 conflict/mismatch 可以拒绝 RUN，再推进 RJ45 自组网协调。

### REFMEM-TASK-20260814-026 - RealtimeCapabilityContract 首版组件

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 把“加载节点实例时必须同时加载实时能力”的规则从文档和 linter 私有函数中抽成 RefMem 内部基础组件。
  - 为后续 DeploymentGate、SlotClaimMap 和板端 HIL 验证提供统一的资源/IO/类 IP 核 contract 派生入口。
- 完成内容：
  - 新增 `refmem_realtime_contract.h/.c`，定义 `refmem_realtime_contract_t` 和 `refmem_realtime_contract_derive()`。
  - 首版 contract 从 NodeLoad、FB instance、GenericNode 和 BoardCapability 派生 `resource_claim`、`io_claim`、`ip_core_claim`、目标 capability、目标 IO constraint、目标 IP core mask、缺失掩码和结果码。
  - application model linter 改为调用 `refmem_realtime_contract_derive()`，能力校验错误归入 `REFMEM_APP_LINT_BAD_REALTIME_CONTRACT`。
  - 当前没有 SlotClaimMap resolved assignment，因此临时通过 `BoardCapabilityTable.active_default_slot == node_id` 关联 B 节点和 A slot；文档中已标记后续替换点。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xCD54C6D1`。
- 还需完成：
  - 将 contract 输入从 `active_default_slot` 替换为 SlotClaimMap resolved assignment。
  - 接入 DeploymentGate/RUN gate，并增加 time budget、IP core version、PIO program id、DMA channel policy、IRQ source 和 fallback policy 校验。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_contract.h`
  - `components/distributed_refmem/src/refmem_realtime_contract.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 推进 SlotClaimMap 首版，让板卡能力、逻辑 slot 和实时 contract 的关联不再依赖 default slot。

### REFMEM-TASK-20260814-025 - BoardCapability SCPI staging 闭环

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 让物理板能力可以通过 SCPI 受控提交到 RefMem staging，支撑 SD System Pack 之外的调试加载和能力实例化验证。
  - 保持 A0-A7 逻辑 slot 与 B0-Bx 物理/profile 节点解耦，SCPI 不直接修改 active BoardCapabilityTable。
- 完成内容：
  - 增加 `SYSTem:REFMEM:LOAD:BOARD <board_id>,<board_uuid_crc32>,<capability_mask>,<io_constraint_mask>,<ip_core_mask>,<default_persona_mask>,<hw_profile_crc32>,<active_default_slot>,<online_required>`。
  - 增加 `SYSTem:REFMEM:LOAD:BOARD:STATus?`，返回 board capability staging snapshot，覆盖 load_seq、mode、active/staging CRC、lint/error 和当前候选字段。
  - `refmem_application_model_stage_scpi_board_capability()` 校验 board 范围、`REFMEM+VDC` baseline、默认 slot 范围和基础字段后，只写 staging snapshot 与 TableRegistry staging 状态。
  - `SCPI_COMMANDS.md` 和 `REFMEM_DOMAIN_ARCHITECTURE.md` 同步说明：板卡能力可由 SD System Pack 或 SCPI staging 提交，但 active profile 仍必须等待后续 CRC、owner validation、IO/IP 核检查、DeploymentGate 和 activation。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；HIL 串口测试未启用。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0x8AB7A16E`。
- 还需完成：
  - 将 BoardCapabilityTable 接入真实多条 staging image、owner validation callback、active/rollbackable 切换和 RUN gate。
  - 增加 HIL 验证：加载 link-control/BISS-C 候选后，确认对应 PIO/DMA/core1_rt 类 IP 核能力可以由 DeploymentGate 检查并通过 RefMem snapshot 闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 推进 `RealtimeCapabilityContract` 派生和 DeploymentGate 接入，让 board capability 不只可加载，还能约束实际 RUN。

### REFMEM-TASK-20260814-024 - NodeLoad 实时能力契约补齐

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 补齐“RefMem 加载节点实例时必须同时加载 core1 实时能力、IO 约束和类 IP 核能力”的架构与代码表达。
  - 将 A0-A7 统一收敛为 RefMem 逻辑槽位，把当前项目物理/实例标签改为 B0-B4，避免 slot 与板卡定位混淆。
- 完成内容：
  - `refmem_fb_instance_entry_t` 增加 `ip_core_claim` 字段，首版覆盖 `PULSE_CAPTURE`、`PULSE_FIRE`、`LINK_SEQUENCE`、`BISS_C_CODEC`、`RJ45_SYNC_DELTA` 和 `VDC_DPLL`。
  - 静态模型 linter 将 `ip_core_claim` 映射为 capability gate，确保链路控制、BISS-C 编解码等类 IP 核不会被当作普通 GPIO。
  - 默认 profile 中 `B2.LinkSwitcherAO` 明确声明 `CORE1_RT + PIO + DMA + LINK_CONTROL`，并补齐 FIRE_LOAD、DONE、FAULT、link timestamp、link sequence state 等事件/数据连接。
  - BISS-C 模型节点声明为 `BISS_C_CODEC` 类 IP 核，要求 PIO、DMA、core1_rt 和 BISS-C IO。
  - 增加 `REFMEM_APP_CAP_REFMEM` 和 `REFMEM_APP_CAP_VDC`，当前静态表所有 A0-A7 slot 候选都具备 `REFMEM + VDC` baseline，linter 对 baseline 做硬检查。
  - 明确 `VDC` 是每个物理节点参与虚拟 DC 时间语义的基础能力，`VDC_DPLL` 才是运行 DPLL owner 的类 IP 核能力。
  - 增加 `BoardCapabilityTable` 首版代码结构，描述 B0-Bx 物理/模型节点能力、IO 约束、类 IP 核、默认 persona 和默认 slot，并参与 package CRC 与 linter。
  - 将 `BoardCapabilityTable` 升级为 TableRegistry 正式表，table id 为 1；`.rmtp` / SD System Pack table count 从 8 增加到 9，表顺序为 ApplicationMap、BoardCapability、GenericNode、NodeLoad、FbInstance、EventLink、DataLink、DeploymentGate、ConnectionQuality。
  - 增加 `SYSTem:REFMEM:BOARD? [board_id]`，可读取 active BoardCapabilityTable 的 B 节点能力、IO 约束、类 IP 核、默认 slot 和 CRC。
  - 文档明确板卡能力必须支持 SD System Pack 和受控 SCPI staging 加载；固件内置表只作为 default/factory profile，active 表必须由 CRC、owner validation、IO 约束、类 IP 核和 DeploymentGate 验证后激活。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `RealtimeCapabilityContract`，明确 RefMem 只验证和发布实时能力事实，实际执行仍由 core1/PIO/DMA/域状态机 owner 完成。
  - `REFMEM_DOMAIN_TODO.md` 增加 `BoardCapabilityTable`、动态 SlotClaim、realtime contract 派生和 HIL 验证待办。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC `0xD66CCA10`。
- 还需完成：
  - 将 `BoardCapabilityTable` 接入真实 staging/active table image 切换和受控 SCPI 写入 staging。
  - 实现 `RealtimeCapabilityContract` 派生组件，并接入 DeploymentGate 和 RUN gate。
  - 做板端/HIL 验证：加载 link-control 候选后确认 FIRE_LOAD、脉冲捕获、链路序列状态与 RefMem snapshot 闭环。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 `BoardCapabilityTable` / `SlotClaimProposal` 细化，让 B0-B4 物理能力与 A0-A7 slot assignment 完全解耦。

### REFMEM-TASK-20260814-023 - StorageAO 通用文件管理基础件

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 `app_model.rmtp` 写入 SD 的能力从 RefMem 专用 package 命令纠偏为 HAOFV StorageAO 通用文件管理基础件。
  - 支持文件和目录增删改查，并保持 SCPI 不直接调用 FatFs、RefMem 向量表不承载文件数据。
- 完成内容：
  - `SYSTem:STORage:FILE:*` 增加通用文件事务写入、info、read、delete、rename。
  - `SYSTem:STORage:DIRectory:*` 增加 create、delete、rename、catalog。
  - StorageManager 增加 `FILE_DELETE`、`FILE_RENAME`、`DIRECTORY_CREATE`、`DIRECTORY_DELETE`、`DIRECTORY_RENAME` job，并通过 StorageAO service 和资源仲裁访问 SD。
  - FatFs port 增加 delete 和 rename 封装；文件写入仍使用 tmp + sync + rename 原子替换。
  - `tools/refmem/refmem_pack_write/refmem_pack_write.py` 改为通过 `SYSTem:STORage:FILE:WRITe:*` 写入 `/refmem/app_model.rmtp`，再执行 `SYSTem:REFMEM:LOAD:SD`。
  - 新增 `tools/storage/storage_scpi_validate/storage_scpi_validate.py`，固化通用 Storage 文件/目录 CRUD 验证流程。
  - `SCPI_COMMANDS.md` 和 RefMem 架构/TODO 同步为通用 Storage 文件管理接口，删除文档中的 RefMem package 专用入口。
- 验证结果：
  - `python -m py_compile tools/refmem/refmem_pack_write/refmem_pack_write.py tools/storage/storage_scpi_validate/storage_scpi_validate.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，最终生成 build id `20260813163405`，package CRC `0x22C703CC`。
  - `python tools/ota/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools/ota/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813163405 --out-dir build-rtos-multicore-smoke/ota_boot_commit_storage_rename_retry` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813163405`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/storage/storage_scpi_validate/storage_scpi_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_storage_file_mgmt_tmp_path` 通过，覆盖目录 create/catalog/rename/delete 和文件 write/info/read/rename/delete。
  - 初次写 `/refmem/app_model.rmtp` 失败定位为 FatFs 原子 rename 阶段错误；修复后 `python tools/refmem/refmem_pack_write/refmem_pack_write.py COM6 --package build-rtos-multicore-smoke/sdcard_refmem_parser/refmem/app_model.rmtp --timeout 8 --out-dir build-rtos-multicore-smoke/validation_refmem_pack_write_storage_file_rename_retry` 通过，`FILE:INFO?` 返回 704 字节，`FILE:READ?` 读回 `RMTP` header，`SYSTem:REFMEM:LOAD:SD` 返回 `STAGED`。
- 还需完成：
  - 将 StorageAO 写事务从当前 4096 字节 RAM buffer 升级为分片落盘或后端流式事务，用于更大的 System Pack/RefMem table image。
  - 继续实现 staging/active/rollbackable table image 切换与 owner validation callback。
- 关联文件：
  - `components/storage_manager/inc/storage_manager.h`
  - `components/storage_manager/src/storage_manager.c`
  - `middleware/fatfs_port/inc/fatfs_port.h`
  - `middleware/fatfs_port/src/fatfs_port.c`
  - `middleware/scpi_port/inc/scpi_storage_commands.h`
  - `middleware/scpi_port/src/scpi_storage_commands.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_pack_write/refmem_pack_write.py`
  - `tools/storage/storage_scpi_validate/storage_scpi_validate.py`
- 下一步：
  - 完成板端 CRUD 和 RefMem load 正向闭环后，继续实现 staging/active/rollbackable table image 切换与 owner validation callback。

### REFMEM-TASK-20260813-022 - StorageAO RefMem package 对象事务

- 状态：已被 20260814-023 纠偏为通用 Storage 文件管理基础件
- 日期：2026-08-13
- 任务目标：
  - 允许通过 SCPI 把 `app_model.rmtp` 写入 SD，同时保持 HAOFV 边界：SCPI 不直接写 FatFs，RefMem 向量表不承载文件数据。
  - 把写入能力做成 StorageAO object transaction 基础件，后续可扩展到其他存储对象和后端。
- 完成内容：
  - StorageManager 增加 object contract 和写事务 API：`begin_object_write`、`write_object_chunk`、`commit_object_write`、`abort_object_write`、`get_write_snapshot`。
  - 首个对象为 `REFMEM_PACKAGE`，固定映射 `/refmem/app_model.rmtp`，支持 create/update/read/delete/info；该专用入口已在 20260814-023 中迁移为通用 `SYSTem:STORage:FILE:*` 路径接口。
  - FatFs port 增加 `fatfs_port_delete()`；原子写入支持替换已有目标文件。
  - 原 `SYSTem:REFMEM:PACKage:*` 接入 StorageAO 对象事务：`BEGIN/DATA/END/ABORt/STATus?/INFO?/READ?/DELete`；该命令树已删除，不再作为正式接口。
  - 新增 `tools/refmem/refmem_pack_write/refmem_pack_write.py`，固化分块上传、读回和 `LOAD:SD` 验证流程。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 已通过，生成 build id `20260813154434`，package CRC `0x8E5DC49A`。
- 还需完成：
  - 运行 Python 脚本静态检查、文档检查。
  - OTA 烧录到 COM6 后，用 `refmem_pack_write.py` 完成板端上传、读回和 `LOAD:SD` 正向闭环。
- 关联文件：
  - `components/storage_manager/inc/storage_manager.h`
  - `components/storage_manager/src/storage_manager.c`
  - `middleware/fatfs_port/inc/fatfs_port.h`
  - `middleware/fatfs_port/src/fatfs_port.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem/refmem_pack_write/refmem_pack_write.py`
- 下一步：
  - 完成板端闭环后，将该对象事务抽象继续推广到 profile/calibration 等受控存储对象。

