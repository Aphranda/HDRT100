# Distributed RefMem 内部主域任务进度

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_TASK_PROGRESS.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-08-14

本文档记录 Distributed Vector Blackboard / RefMem Sync Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `REFMEM_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

### REFMEM-TASK-20260814-042 - GPIO4..7 overlay 维护接口与预检工具

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为 GPIO4..7 最小模型 overlay 增加方向安全维护接口和双板预检脚本。
  - 先确保 UART1 不占用 GPIO4/5，且脚本能以串口生命周期管理方式 release、驱动、读取和退出清理。
- 完成内容：
  - `sync_io.h/.c` 增加 `sync_io_debug_model_*` 维护函数，覆盖 GPIO4..7 output enable/value、release 和 input level snapshot。
  - `scpi_realtime_io_commands.c/h` 增加 `REALtime:IO:MODel:PROFile?`、`REALtime:IO:MODel:INPut:LEVel?`、`REALtime:IO:MODel:OUTPut:MASK`、`REALtime:IO:MODel:OUTPut:MASK?` 和 `REALtime:IO:MODel:OUTPut:RELease`。
  - `REALtime:IO:MODel:PROFile?` 返回 `base_pin,pin_count,uart_conflict_mask,uart_enabled`，当前期望为 `4,4,3,0`。
  - `REALtime:IO:MODel:OUTPut:MASK <enable_mask>,<value_mask>` 显式区分输出 owner 和输出电平；未 enable 的线恢复输入下拉。
  - 新增 `tools/debug_model_overlay_validate/debug_model_overlay_validate.py`，默认验证 X->Y GPIO4/GPIO5/GPIO7 和 Y->X GPIO6，运行前后 release 双方 GPIO4..7。
  - `tools/README.md`、`REFMEM_MIN_SYSTEM_PLAYBOOK.md` 和 `REFMEM_DOMAIN_TODO.md` 同步新增工具入口和待办状态。
- 验证结果：
  - `python -m py_compile tools\debug_model_overlay_validate\debug_model_overlay_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x2DF62B6E`，build id 为 `20260814104920`。
  - `python tools\ota_send\ota_send.py COM3 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，COM3 OTA 进入 `READY_TO_REBOOT`。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM3 --expected-build 20260814104920 --out-dir build-rtos-multicore-smoke\ota_boot_commit_debug_model_COM3` 通过，COM3 启动并 commit 到 build `20260814104920`。
  - COM3 顺序查询 `REALtime:IO:MODel:PROFile?` 返回 `4,4,3,0`，确认 GPIO4..7 overlay profile 生效且 UART stdio 关闭。
  - COM3 顺序查询 `REALtime:IO:MODel:INPut:LEVel?` 返回 `4,4,0`。
  - COM4 首次 OTA 未完成：`ota_send.py COM4 ...` 打开端口失败，错误为 `PermissionError(13, '拒绝访问。', None, 5)`，确认为串口软件占用；关闭占用工具后重试通过。
  - `python tools\ota_send\ota_send.py COM4 build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，COM4 OTA 进入 `READY_TO_REBOOT`。
  - `python tools\ota_boot_commit\ota_boot_commit.py COM4 --expected-build 20260814104920 --out-dir build-rtos-multicore-smoke\ota_boot_commit_debug_model_COM4` 通过，COM4 启动并 commit 到 build `20260814104920`。
  - COM4 顺序查询 `REALtime:IO:MODel:PROFile?` 返回 `4,4,3,0`。
  - `python tools\debug_model_overlay_validate\debug_model_overlay_validate.py --port-x COM3 --port-y COM4 --out-dir build-rtos-multicore-smoke\debug_model_overlay_COM3_COM4` 通过。
  - HIL 方向结果：`TURN_POS_PULSE` X.GPIO4 -> Y.GPIO4、`VNA_READY` X.GPIO5 -> Y.GPIO5、`VNA_TRIG` Y.GPIO6 -> X.GPIO6、`LINK_SWITCH` X.GPIO7 -> Y.GPIO7 均 PASS。
- 还需完成：
  - 进入最小业务 HIL：A1 位置脉冲、A4 捕获并更新时间事实、A3 预约链路切换、A5 触发虚拟网分、A2 返回 READY。
- 关联文件：
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_io_commands.c`
  - `tools/debug_model_overlay_validate/debug_model_overlay_validate.py`
  - `tools/README.md`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 将 overlay 节点写入 NodeLoad/Capability staging，并补齐最小业务 HIL 的 RefMem snapshot / quality / evidence 读取闭环。

### REFMEM-TASK-20260814-041 - GPIO4..7 最小模型 overlay 规划

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将用户当前已连接的 `GPIO4..7` 记录为最小系统业务模型 overlay。
  - 明确 Y 板模型实例整体向后挪一个槽位，避免和 X 板 `A3` 链路控制冲突。
  - 消除最小系统 UART1 对 `GPIO4/5` 的默认占用风险。
- 完成内容：
  - `REFMEM_MIN_SYSTEM_PLAYBOOK.md` 增加 `GPIO4..7` 最小模型 overlay，定义 X 板 `A1/A2/A3` 和 Y 板 `A4/A5`。
  - `HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md` 增加 overlay 硬件约束，明确每根线的唯一输出 owner、输入 receiver 和模拟信号。
  - `REFMEM_DOMAIN_TODO.md` 增加 `P4.6 - 最小模型系统 GPIO4..7 Overlay`，把 profile、node load、RealtimeCapabilityContract、方向安全脚本和业务 HIL 拆成后续待办。
  - `board_config.h` 注明 `GPIO4/5` 的 UART1 兼容定义只在 UART stdio 启用时有效，最小模型 overlay 可复用。
  - `board.c` 将 `drv_uart.h` include 和 `board_init_uart()` 内部初始化改为受 `PROJECT_ENABLE_UART_STDIO` 控制；默认最小系统 build 不初始化 UART1。
- 当前 overlay 线束：
  - `GPIO4`: X `A1` 模拟转台输出 `TURN_POS_PULSE` -> Y `A4` 脉冲分发输入。
  - `GPIO5`: X `A2` 模拟网分输出 `VNA_READY` -> Y `A5` VNA 网关输入。
  - `GPIO6`: Y `A5` VNA 网关输出 `VNA_TRIG` -> X `A2` 模拟网分输入。
  - `GPIO7`: X `A3` 链路控制输出 `LINK_SWITCH` -> Y `A5` VNA 网关输入。
- 还需完成：
  - 增加 debug model board profile 或等价配置表，显式声明 `GPIO4..7` overlay 与 UART1 互斥。
  - 增加方向安全检测脚本，确保运行前双方非 owner 引脚 release。
  - 增加最小业务 HIL，验证位置脉冲、VDC 发布时间、预约链路切换、虚拟网分触发和 READY 回传。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `boards/rp2350_trig/src/board.c`
  - `docs/refmem/REFMEM_MIN_SYSTEM_PLAYBOOK.md`
  - `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 先做 GPIO4..7 方向安全脚本和 profile 声明，再把 overlay 节点写入 RefMem node load / realtime capability contract。

### REFMEM-TASK-20260814-040 - REFMEM_HELLO bundle helper

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 为 P4.5 阶段 2 的 `REFMEM_HELLO` 双向交换建立可复用 payload/frame 生成入口。
  - 将 board capability、adapter caps、layout/application/config CRC 和 build id CRC 收敛为标准 HELLO，而不是在后续 HIL 或 SCPI 中临时拼字段。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_transport_adapter.h`，抽出通用 adapter id、capability、state、error 和 `refmem_transport_caps_t`。
  - `refmem_pio_spi_adapter.h` 改为引用公共 transport adapter 头，避免后续 BISS-C/RJ45/UART/RS485 adapter 重复定义。
  - 新增 `components/distributed_refmem/inc/refmem_sync_hello.h`。
  - 新增 `components/distributed_refmem/src/refmem_sync_hello.c`。
  - 实现 `refmem_sync_hello_payload_from_board()`：从 board capability、adapter caps 和版本 CRC 生成 `refmem_sync_hello_payload_t`。
  - 实现 `refmem_sync_hello_encode_frame()`：把 HELLO payload 编码为标准 RefMem Sync frame。
  - 新增 `tests/unit/test_refmem_sync_hello.c` 和 `tools/tests/run_refmem_sync_hello_tests.ps1`，覆盖 payload 字段、frame validate、adapter inject/poll 和 payload byte match。
  - 将 `refmem_sync_hello.c` 加入根 `CMakeLists.txt` 固件源列表。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_hello_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_pio_spi_adapter_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_contract_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_sync_frame_tests.ps1` 通过 ARM GCC 编译检查；当前环境无 host C 编译器，未执行 host exe。
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`，package CRC 为 `0x36759853`，build id 为 `20260814103620`。
  - 本轮未烧录板端，未运行 COM3/COM4 HIL；真实两板 `REFMEM_HELLO` 双向交换仍未完成。
- 还需完成：
  - 定义 `task_refmem_sync` 内的 adapter service 调用点，接入 HELLO TX/RX 周期。
  - 增加两板 HIL 工具步骤：双方生成 HELLO、发送、接收、校验 build/layout/application/config/capability/adapter bundle。
  - 将 HELLO 结果写入 peer state 和 connection quality snapshot。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_transport_adapter.h`
  - `components/distributed_refmem/inc/refmem_sync_hello.h`
  - `components/distributed_refmem/src/refmem_sync_hello.c`
  - `components/distributed_refmem/inc/refmem_pio_spi_adapter.h`
  - `tests/unit/test_refmem_sync_hello.c`
  - `tools/tests/run_refmem_sync_hello_tests.ps1`
  - `CMakeLists.txt`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 设计 `RefMemSyncService` 的 TX/RX service 边界，并把 HELLO frame 进入 `refmem_sync_receive_frame()` 的路径固定下来。

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
  - 新增 `tools/two_board_io_validate/two_board_io_validate.py`，逐位静态驱动两块板的 active output group 并读取对端 input mask，自动识别漏接、错位和短接。
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
  - `docs/sync/SYNC_IO_RESOURCE_PLAN.md`
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
  - 新增 `tools/refmem_network_validate/refmem_network_validate.py`。
  - 工具显式管理两个串口生命周期：分别 open、settle、清输入/输出、查询、flush、close。
  - 单板 baseline 覆盖 `*IDN?`、`SYST:FW:BUILD?`、`SYST:CORE?`、`SYST:SYNC:VDC:STAT?`、`SYST:SYNC:VDC:DPLL:STAT?`、`SYST:CONFigure:STAT?`、`SYST:REFMEM:CLAIM? 0` 和 `SYST:REFMEM:CLAIM:EVIDence? 0`。
  - 双板比较默认要求 build id 和 SlotClaimMap CRC 一致，可通过 `--allow-build-mismatch` 或 `--allow-map-mismatch` 放宽。
  - 验证结果写入 `summary.json` 和 `summary.txt`。
- 验证结果：
  - `python -m py_compile tools\refmem_network_validate\refmem_network_validate.py` 通过。
  - `python tools\docs_check\docs_check.py` 通过，warnings=0。
  - `python -m pytest` 通过，18 passed、1 skipped；两板工具未实际打开硬件串口。
- 还需完成：
  - 接入真实 RJ45 `CLAIM_*` 协调后，扩展重复 slot claim、错绑、stale、overflow 和 commit 后 CRC 一致性验证。
- 关联文件：
  - `tools/refmem_network_validate/refmem_network_validate.py`
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
  - duplicate claim、disabled slot、UUID mismatch、hardware profile mismatch、invalid slot 和 active slot 容量 overflow 会记录 evidence。
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
  - 新增 `tests/unit/test_refmem_slot_claim.c`，覆盖 nominal assignment、loaded instance mask、duplicate claim、UUID mismatch 和第 9 个候选 overflow。
  - 新增 `tools/tests/run_refmem_slot_claim_tests.ps1`，沿用现有纯 C 测试风格：有 host gcc/clang 时运行 exe，否则退化为 ARM GCC 编译检查。
  - `tools/README.md`、`tests/README.md` 和 `REFMEM_DOMAIN_TODO.md` 增加测试入口和两板组网验证待办。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1` 通过；当前机器无 host C 编译器，结果为 ARM GCC 编译检查通过，未执行主机 exe。
- 还需完成：
  - 增加 stale claim、hard binding mismatch、claim CRC、9-16 全矩阵和超过 16 candidate rejected 的测试。
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
  - `tools/multicore_board_validate/multicore_board_validate.py` 增加 `test_refmem_slot_claim_gate()`。
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
  - 板端实际运行 `python -m pytest -m hil --run-hil --hil-port COMx` 或 `python tools/multicore_board_validate/multicore_board_validate.py COMx --tests refmem_slot_claim_gate`。
  - 增加负向 SlotClaim 矩阵：重复 claim、错绑、stale、9-16 候选 overflow、超过 16 候选 rejected。
- 关联文件：
  - `tools/multicore_board_validate/multicore_board_validate.py`
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
  - 让 claim 冲突、错绑、overflow 和 required slot 缺失不只可查询，还能实际阻止 ready/RUN。
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
  - `tools/refmem_pack_write/refmem_pack_write.py` 改为通过 `SYSTem:STORage:FILE:WRITe:*` 写入 `/refmem/app_model.rmtp`，再执行 `SYSTem:REFMEM:LOAD:SD`。
  - 新增 `tools/storage_scpi_validate/storage_scpi_validate.py`，固化通用 Storage 文件/目录 CRUD 验证流程。
  - `SCPI_COMMANDS.md` 和 RefMem 架构/TODO 同步为通用 Storage 文件管理接口，删除文档中的 RefMem package 专用入口。
- 验证结果：
  - `python -m py_compile tools/refmem_pack_write/refmem_pack_write.py tools/storage_scpi_validate/storage_scpi_validate.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，最终生成 build id `20260813163405`，package CRC `0x22C703CC`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813163405 --out-dir build-rtos-multicore-smoke/ota_boot_commit_storage_rename_retry` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813163405`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/storage_scpi_validate/storage_scpi_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_storage_file_mgmt_tmp_path` 通过，覆盖目录 create/catalog/rename/delete 和文件 write/info/read/rename/delete。
  - 初次写 `/refmem/app_model.rmtp` 失败定位为 FatFs 原子 rename 阶段错误；修复后 `python tools/refmem_pack_write/refmem_pack_write.py COM6 --package build-rtos-multicore-smoke/sdcard_refmem_parser/refmem/app_model.rmtp --timeout 8 --out-dir build-rtos-multicore-smoke/validation_refmem_pack_write_storage_file_rename_retry` 通过，`FILE:INFO?` 返回 704 字节，`FILE:READ?` 读回 `RMTP` header，`SYSTem:REFMEM:LOAD:SD` 返回 `STAGED`。
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
  - `tools/refmem_pack_write/refmem_pack_write.py`
  - `tools/storage_scpi_validate/storage_scpi_validate.py`
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
  - 新增 `tools/refmem_pack_write/refmem_pack_write.py`，固化分块上传、读回和 `LOAD:SD` 验证流程。
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
  - `tools/refmem_pack_write/refmem_pack_write.py`
- 下一步：
  - 完成板端闭环后，将该对象事务抽象继续推广到 profile/calibration 等受控存储对象。

### REFMEM-TASK-20260813-021 - LOAD:SD 接入 RMTP parser 首版

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 让 `SYSTem:REFMEM:LOAD:SD` 不再只依赖 manifest 摘要，而是读取并校验 RefMem table image。
  - 首版只校验 `.rmtp` 格式并写 staging snapshot，不替换 active image。
- 完成内容：
  - `refmem_table_registry.h/.c` 增加 `.rmtp` package validation API，校验 magic、format version、header size、total size、table count、payload CRC、package CRC 和每表 CRC。
  - `refmem_application_model_stage_sd_system_pack()` 增加 package CRC、package valid 和 package error 入参。
  - `SYSTem:REFMEM:LOAD:SD [path]` 默认读取 `/refmem/app_model.rmtp`，可用可选 path 覆盖；读取仍通过 StorageAO `FILE_READ` job，不直接调用 FatFs。
  - `tools/refmem_scpi_load_validate.py` 允许旧 SD 卡上缺少 `/refmem/app_model.rmtp` 时返回 `REJECTED`，并在 STAGED 时检查 package CRC 和路径。
  - `SCPI_COMMANDS.md`、`REFMEM_DOMAIN_ARCHITECTURE.md` 和 `REFMEM_DOMAIN_TODO.md` 同步 LOAD:SD parser 状态。
- 验证结果：
  - `python -m py_compile tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813152145`，package CRC `0xD5C9018D`。
  - `python tools/sd_fs_build/sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke/sdcard_refmem_parser --clean --no-zip --no-reports` 通过，生成新版 SD staging。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813152145 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_rmtp_parser` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813152145`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_rmtp_parser` 通过；当前板上 SD 卡仍是旧 System Pack，`LOAD:SD` 返回 `REJECTED`、last_error `7`、path `/refmem/app_model.rmtp`，符合缺少/无效 table image 的预期拒绝路径。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_rmtp_parser_multicore` 通过，16/16 passed。
- 还需完成：
  - 将新版 SD staging 写入实际 SD 卡后，复测 `LOAD:SD` 的 STAGED 正向路径。
  - parser 通过后仍需实现真实 table image staging buffer、owner validation callback 和 activation/rollback。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 优先实现板端 parser 通过后的 staging table image 存储和 owner validation callback，而不是直接 active 替换。

### REFMEM-TASK-20260813-020 - SD System Pack 集成 RefMem package

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 让 `tools/sd_fs_build/sd_fs_build.py` 默认生成 RefMem table image 文件。
  - 在根 `/manifest.idx` 中引用 RefMem table image，为后续 `LOAD:SD` parser 提供稳定输入。
- 完成内容：
  - `sd_fs_build.py` 固化 `/refmem` 目录，生成 `/refmem/app_model.rmtp`、`/refmem/app_model.idx` 和 `/refmem/app_model.json`。
  - 根 `manifest.idx` 增加 `default.refmem=/refmem/app_model.rmtp`。
  - 根 `manifest.idx` 的 required 列表增加 `type=refmem_table_image`。
  - `SD_TODO.md` 同步 System Pack 示例、文件格式表和固定目录说明。
  - `REFMEM_DOMAIN_TODO.md` 标记 SD 工具集成完成。
- 验证结果：
  - `python -m py_compile tools/sd_fs_build/sd_fs_build.py tools/refmem_pack_build/refmem_pack_build.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python tools/sd_fs_build/sd_fs_build.py --build-dir build-rtos-multicore-smoke --output-dir build-rtos-multicore-smoke/sdcard_refmem_pack --clean --no-zip --no-reports` 通过。
  - 生成的根 `manifest.idx` 包含 `default.refmem=/refmem/app_model.rmtp` 和 `required=/refmem/app_model.rmtp,type=refmem_table_image,size=704,crc32=9474FC98`。
  - 生成的 `/refmem/app_model.idx` 包含 8 张表的 offset/size/crc32。
- 还需完成：
  - 板端 `LOAD:SD` 仍未解析 `.rmtp`，当前只利用 StorageAO manifest scan 结果写 staging snapshot。
- 关联文件：
  - `tools/sd_fs_build/sd_fs_build.py`
  - `docs/storage/SD_TODO.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 提交推送本轮 SD System Pack 集成；随后进入板端 `.rmtp` parser 设计和实现。

### REFMEM-TASK-20260813-019 - RefMem table image 格式固化

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 定义 RefMem 自己的 table image 文件格式，避免把根 `/manifest.idx` 和二进制表镜像混在一起。
  - 固化最小 PC 侧生成脚本，为后续 `LOAD:SD` 真实 parser 做输入准备。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `RefMem Table Image 格式`，定义 `/refmem/app_model.rmtp`、`app_model.idx`、`app_model.json` 三个文件的职责。
  - 定义 `.rmtp` header、table directory、payload CRC、package CRC 和 parser/owner validation 约束。
  - 新增 `tools/refmem_pack_build/refmem_pack_build.py`，生成最小 8 表 placeholder package、索引和 JSON 说明。
  - `tools/README.md` 增加新工具说明。
  - `REFMEM_DOMAIN_TODO.md` 标记格式定义完成；真实 `LOAD:SD` parser 仍保留待办。
- 验证结果：
  - `python -m py_compile tools/refmem_pack_build/refmem_pack_build.py` 通过。
  - `python tools/refmem_pack_build/refmem_pack_build.py --output-dir build-rtos-multicore-smoke/refmem_pack_format` 通过，生成 `refmem/app_model.rmtp`、`refmem/app_model.idx` 和 `refmem/app_model.json`。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
- 还需完成：
  - 将 `sd_fs_build.py` 集成 RefMem package。
  - 板端实现 `.rmtp` parser 并接入 `SYSTem:REFMEM:LOAD:SD`。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
  - `tools/refmem_pack_build/refmem_pack_build.py`
  - `tools/README.md`
- 下一步：
  - 提交推送本轮格式固化；随后评估是否把 `sd_fs_build.py` 集成 RefMem package。

### REFMEM-TASK-20260813-018 - TableRegistry 生命周期字段与 owner validation contract

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 TableRegistry 首版查询基础上补齐 table image 生命周期的可观测字段。
  - 增加 owner validation contract 首版入口，但不提前实现 active 替换。
- 完成内容：
  - `refmem_table_registry_entry_t` 增加 `image_offset` 和 `image_size`，为后续 System Pack/TLV table image 导入提供稳定位置语义。
  - 增加 registry flags：`ACTIVE_PRESENT`、`STAGING_PRESENT`、`CRC_OK`、`OWNER_OK`。
  - `refmem_table_registry_refresh_snapshot()` 改为 active/staging mask 分离，staging 状态不再覆盖 active 表存在性。
  - 新增 `refmem_table_registry_validate_staging()`，首版基于 staging snapshot 的 package CRC、lint error 和 last error 设置 owner-ok 可观测状态。
  - `SYSTem:REFMEM:TABle?` 返回字段增加 `image_offset,image_size`。
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 同步 18 字段返回格式。
- 验证结果：
  - `python -m py_compile tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813150257`，package CRC `0x5248DF3C`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813150257 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_table_lifecycle` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813150257`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_table_lifecycle` 通过，覆盖 `SYSTem:REFMEM:TABle?` 18 字段、active/staging mask 分离和 staging owner-ok flags。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_table_lifecycle_multicore` 通过，16/16 passed。
- 还需完成：
  - 接入真实 TLV/System Pack table image。
  - 实现 owner validation callback table 和 active/rollbackable 切换。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P0：定义 TLV/System Pack table image 格式，随后让 `LOAD:SD` 进入真实 parser。

### REFMEM-TASK-20260813-017 - RefMem TableRegistry 首版

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 按新 P0 待办落地 `RefMemTableRegistry` 首版。
  - 先建立可编译、可查询的 registry 基础件，用于观察 active/staging CRC、validation state 和 evidence。
- 完成内容：
  - 新增 `refmem_table_registry.h/.c`，覆盖 8 张静态应用模型表的 registry entry。
  - registry 首版从 `refmem_application_model_snapshot_t` 刷新 active CRC，从 `refmem_application_model_load_snapshot_t` 刷新 staging CRC 和 validation state。
  - 增加 `SYSTem:REFMEM:TABle? [table_id]` 查询，返回 registry snapshot 加指定表 entry。
  - 根 `CMakeLists.txt` 纳入 `refmem_table_registry.c`。
  - `SCPI_COMMANDS.md` 和 `REFMEM_DOMAIN_TODO.md` 同步新命令与完成状态。
- 验证结果：
  - `python -m py_compile tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py` 通过。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813145717`，package CRC `0xFF8303A3`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813145717 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_table_registry_fix` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813145717`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_table_registry_fix` 通过，覆盖 `SYSTem:REFMEM:TABle?` 初始 active mask、NodeLoad staging 后 active/staging mask 共存、非法 NodeLoad rejected 和 SD staging。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_table_registry_multicore` 通过，16/16 passed。
- 还需完成：
  - `RefMemTableRegistry` 后续接入真实 active/staging table image、owner validation callback 和 rollbackable 状态。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_table_registry.h`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P0：定义真实 active/staging table image 生命周期和 owner validation callback。

### REFMEM-TASK-20260813-016 - RefMem 文档主线重排

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 Distributed RefMem 发生较大架构变动后，重新审查 `docs/refmem` 内容。
  - 把待办从早期 P0-P8 历史阶段重排为当前可执行优先级。
  - 修正 README 和架构文档中过时的实现状态。
- 完成内容：
  - `README.md` 从 Draft 更新为 Active，并补充 RefMemAO、A0-A7 通用逻辑插槽、NodeLoad、SlotClaim、表镜像加载等当前主线。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加“当前 Canonical Model”，明确 RefMemAO owner、A0-A7 通用插槽、GenericNode/NodeLoad 分层、SlotClaimMap、RefMemSlotContract 和 load staging 边界。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 的“当前实现现状”更新为当前代码真实状态，列出已实现组件和未完成模块。
  - `REFMEM_DOMAIN_TODO.md` 整体重构为当前执行队列：P0 表镜像与加载闭环、P1 SlotClaimMap 与自组网协调、P2 SlotContract 与 AO/FB owner API、P3 Command/ACK/NACK、P4 Sync/RMA、P5 组件化、P6 接口接入、P7 验证。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
- 还需完成：
  - 按新 P0 从 `RefMemTableRegistry` 和 table image 生命周期开始实现。
- 关联文件：
  - `docs/refmem/README.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 提交推送本轮文档重排；随后进入 P0-TableRegistry 首个实现闭环。

### REFMEM-TASK-20260813-015 - RefMem SCPI staging load 首版

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 RefMem 自身状态机 `mode=IDLE` 时支持 SCPI 发起 SD/System Pack 加载。
  - 支持通过 SCPI 直接提交节点装载配置候选到 RefMem staging，用于节点实例化和后续自组网协调验证。
  - 加载只进入 staging snapshot，不直接覆盖 active NodeLoadTable 或 live NodeSlot fact。
- 完成内容：
  - `refmem_application_model.h/.c` 增加 RefMem load 状态机枚举：`IDLE / LOAD_TO_STAGING / VALIDATING / ACTIVATING / FAULT`。
  - 增加 staging 状态枚举：`EMPTY / STAGED / VALIDATED / FAILED`，并建立 `refmem_application_model_load_snapshot_t`。
  - 增加 `refmem_application_model_stage_sd_system_pack()`：接收 Storage manifest 结果，当前用已编译静态应用模型 package CRC 写入 staging snapshot，占位等待真实 TLV/System Pack parser。
  - 增加 `refmem_application_model_stage_scpi_node_config()`：通过 SCPI inline 参数提交一条 NodeLoad 候选，校验 A0-A7 node 范围、instance 范围和基础 enable/required 位。
  - `SYSTem:REFMEM:LOAD:SD [path]` 接入 StorageAO `MANIFEST_SCAN` job，只在 RefMem load mode 为 `IDLE` 且底层实时触发状态 `TRIG_STATE_IDLE` 时允许。
  - `SYSTem:REFMEM:LOAD:NODE <node_id>,<instance_id>,<role_mask>,<persona_mask>[,<enabled>,<required>,<load_order>]` 接入 SCPI 节点候选 staging。
  - `SYSTem:REFMEM:LOAD:STATus?` 固定返回 load snapshot，覆盖 load_seq、source、mode、staging_state、manifest、active/staging CRC、lint/error 和候选节点字段。
  - 新增 `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`，固化 CDC/USBTMC RefMem load 命令验证。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 和 `SCPI_COMMANDS.md` 同步 RefMem load 状态机与 SCPI 命令说明。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813143521`，package CRC `0xE86659C5`。
  - `python tools/product_scpi_validate/product_scpi_validate.py --dry-run --skip-mode` 通过，生成 119 条产品 SCPI 固定响应用例；该脚本不覆盖 `scpi_system_snapshot_commands.h`，RefMem load 使用新增专用脚本验证。
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `python -m py_compile tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py tools/product_scpi_validate/product_scpi_validate.py` 通过。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813143521 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_scpi_load` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813143521`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_scpi_load` 通过：`LOAD:STATus?` 初始 `mode=0`；合法 `LOAD:NODE 5,9,32,32,1,0,0` 返回 `STAGED`；非法 `LOAD:NODE 8,9,32,32,1,0,0` 返回 `REJECTED` 且 `last_error=4`；`LOAD:SD` 返回 `STAGED`，manifest build id `20260812074528`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_scpi_load_multicore` 通过，16/16 passed。
- 还需完成：
  - 将 SD manifest 占位导入升级为真实 TLV/System Pack parser。
  - 将 SCPI inline 单条 NodeLoad 候选升级为 staging NodeLoadTable image，支持多条候选、CRC、owner validation 和 activation。
  - 增加类似 OTA 的 `BEGIN/DATA/END/ABORT` 分块传输完整 RefMem application/node package 到 staging。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `middleware/scpi_port/inc/scpi_system_snapshot_commands.h`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tools/refmem_scpi_load_validate/refmem_scpi_load_validate.py`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/interface/SCPI_COMMANDS.md`
- 下一步：
  - OTA 烧录本轮固件，使用 CDC/USBTMC 查询 `SYSTem:REFMEM:LOAD:STATus?`、`LOAD:NODE` 和 `LOAD:SD`，确认 staging 快照闭环。

### REFMEM-TASK-20260813-014 - 全局逻辑插槽 claim 与自组网协调

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 明确 A0-A7 通用插槽在 active profile / epoch 内是全环唯一逻辑地址。
  - 支持一块物理板同时承载多个不同逻辑插槽，同时禁止多个物理板提交同一个 active slot owner。
  - 增加自组网协调机制：重复 claim 优先尝试迁移到空闲通用插槽，只有插槽满、实例化溢出或硬绑定 required slot 不匹配时才失败。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加全局逻辑插槽 `SlotClaim`、`SlotClaimMap` 和自组网协调状态机。
  - 协调状态覆盖 `DISCOVER -> CLAIM_PROPOSE -> CLAIM_COLLECT -> CONFLICT_DETECTED -> RESOLVE_PLAN -> RESOLVE_COMMIT -> CLAIM_ACTIVE`，失败进入 `CLAIM_STALE / CLAIM_FAULT`。
  - 明确协调消息：`CLAIM_HELLO`、`CLAIM_PROPOSE`、`CLAIM_CONFLICT`、`CLAIM_RELEASE`、`CLAIM_RESOLVE`、`CLAIM_COMMIT`。
  - 明确候选节点实例与 active slot assignment 分层：一块物理板最多可上报 16 个候选节点实例用于自组网协调和反向验证，但 active assignment 只能映射到 A0-A7，第 9 到第 16 个未分配候选必须进入 `OVERFLOW` evidence。
  - `refmem_application_model.h` 增加 `REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX = 16`，作为后续 `SlotClaimProposal` 运行态上限。
  - `HAOFV_ARCHITECTURE.md` 和 `RTOS_HAOFV_ARCHITECTURE.md` 同步全环 slot claim 唯一性与 DeploymentGate 拒绝规则。
  - `refmem_application_model.h/.c` 增加 `REFMEM_APP_CLAIM_*` 策略、`claim_policy` 和 `claim_priority` 字段。
  - 静态模型 linter 增加 slot claim 策略检查：required slot 不允许 disabled/dynamic claim，dynamic spare 必须 report-only 且非 required。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813141942`，package CRC `0x0978A193`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813141942 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_slot_claim16` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813141942`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_slot_claim16` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,107645,0,8,107644,107644,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,107650,107651,112568,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,107656,2,0,1,15,3840,2,7,2,24714159,0,7`；`SYSTem:PROTection:STATus? => 1,107661,1,1,1,0,0,0,2,11,2,177242018,0,7`。
- 还需完成：
  - 在 `DistributedRefMemAO` 中实现运行态 `SlotClaimMap` 聚合、重复 claim 检测、迁移计划和 claim epoch commit。
  - 增加单板 16 候选节点反向验证，确认不会生成第 9 个隐式插槽、不会覆盖已有 active slot，超过 16 个 proposal 会被拒绝。
  - 将 `SlotClaimMap` 暴露到 DeploymentGate evidence 和后续维护查询。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/arch/HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
- 下一步：
  - 进入 `DistributedRefMemAO` 运行态 `SlotClaimMap` 聚合、协调消息和 16 候选反向验证实现。

### REFMEM-TASK-20260813-013 - GenericNode capability 与应用 role 分离

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 继续纠偏 RefMem 静态模型，避免 GenericNodeTable 从当前节点应用范围反推出通用节点属性。
  - 将通用节点能力定义为硬件/基础能力上限，应用 role/persona/instance 只通过 NodeLoadTable 装载。
  - 固化“RefMem A0-A7 是 8 个通用插槽，node_id 是同步协议中的 slot id”这一模型。
  - 综合用户提出的 RM Slot 能力模型，评估如何在不破坏 HAOFV 与既有静态模型表的前提下形成通用 RefMem 基础件。
- 完成内容：
  - `refmem_application_map_t` 删除 `node_count` 和 `node[]`，只保留 application/profile/layout/target mask 元数据。
  - 新增 `refmem_generic_node_table_t`，独立维护 A0-A7 通用插槽基座。
  - `refmem_application_model_snapshot_t` 增加 `generic_node_crc32`，package CRC 和 table mask 纳入 GenericNodeTable。
  - 增加独立 `REFMEM_APP_CAP_*` 能力位，GenericNodeTable 不再复用 `REFMEM_APP_ROLE_*`。
  - 增加 capability gate：enabled load 的实例 resource/IO claim 必须被目标 GenericNode 的 `capability_mask` 覆盖。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 明确 GenericNode capability 不能从当前装载实例反推，必须来自 board profile、硬件约束或 System Pack 的硬件 profile。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 和 `RTOS_HAOFV_ARCHITECTURE.md` 将 A0-A7 明确描述为通用插槽，实例化节点/逻辑功能通过 NodeLoadTable 装入插槽。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加通用 RefMemAO 基础件模型：现有静态模型表、Header/Directory、SlotGuard、DeploymentGate 和 QualityTable 共同生成 `DistributedRefMemAO` 内部 `RefMemSlotContract` 契约视图。
  - 明确 `RefMemSlotContract` 不是绕过 AO/FB 或 RefMemAO 的第二套业务 API，而是 `DistributedRefMemAO` 接收、校验、发布和订阅分发反射内存事实时使用的内部契约。
  - `HAOFV_ARCHITECTURE.md` 与 `RTOS_HAOFV_ARCHITECTURE.md` 同步表读写规范：业务行为入口仍归 AO/FB owner、ConfigGate、CommandSlot owner 和 RefMem Sync owner；裸 RefMem 字段不得被业务代码直接写入。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813140220`，package CRC `0xC6404998`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813140220 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_slot_contract` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813140220`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_slot_contract` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,89430,0,8,89429,89429,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,89434,89435,94354,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,89440,2,0,1,15,3840,2,7,2,4138206416,0,7`；`SYSTem:PROTection:STATus? => 1,89445,1,1,1,0,0,0,2,11,2,2207849873,0,7`。
- 还需完成：
  - 把 GenericNode capability 暴露到后续 table registry / System Pack / DeploymentGate evidence。
  - 将 resource/IO 到 capability 的映射从当前代码 helper 升级为表驱动资源能力矩阵。
  - 后续实现 `DistributedRefMemAO` 内部 `RefMemSlotContract` 派生与 linter，不建立对外业务读写 API。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 进入 `DistributedRefMemAO` 内部 `RefMemSlotContract` 派生规则和 linter 实现，或继续 table registry / System Pack / DeploymentGate evidence。

### REFMEM-TASK-20260813-012 - 通用节点与实例加载架构纠偏

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 RefMem 静态模型从“节点直接绑定产品实例”纠偏为“通用节点基座 + 应用实例加载表”。
  - 支持同一块板卡同时加载多个逻辑实例，例如调试阶段一块板同时模拟转台和网分。
  - 避免 `ApplicationMap.node[]` 通过 `instance_first/count` 绑定连续实例范围，阻碍后续多节点、多 profile 和多实例加载。
- 完成内容：
  - `REFMEM_DOMAIN_TODO.md` 增加架构纠偏待办：A0-A7 通用节点基座必须与应用实例装载拆开。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 将 `DistributedApplicationMap` 改为应用/profile 元数据和 CRC bundle，不再作为节点目录。
  - 新增 `DistributedGenericNodeTable` 和 `DistributedNodeLoadTable` 架构说明。
  - `HAOFV_ARCHITECTURE.md` 和 `RTOS_HAOFV_ARCHITECTURE.md` 同步 GenericNode / NodeLoad / FbInstance 三层边界。
  - `refmem_application_model.h/.c` 增加 `refmem_node_load_table_t`，把实例实际装载关系从节点表移出。
  - `refmem_app_node_entry_t` 改为通用节点基座字段：`capability_mask`、`default_persona_mask`、`hw_profile_crc32`、`online_required`、`fail_policy`。
  - `refmem_fb_instance_entry_t.node_id` 改为 `default_node_id`，实际 active 节点以 NodeLoadTable 为准。
  - linter 改为校验 NodeLoadTable 的 `node_id -> instance_id` 装载关系，资源/IO 冲突按加载到同一节点的 enabled 实例组合检查。
  - package CRC 和 `table_mask` 纳入 NodeLoadTable CRC。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813133647`，package CRC `0xD148A39A`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813133647 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_node_load` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813133647`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_node_load` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,128516,0,8,128515,128515,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,128521,128522,133442,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,128527,2,0,1,15,3840,2,7,2,405916318,0,7`；`SYSTem:PROTection:STATus? => 1,128532,1,1,1,0,0,0,2,11,2,2731132301,0,7`。
- 还需完成：
  - 定义 binary/TLV 存储格式、版本兼容和 System Pack 导入策略。
  - 把 package CRC、lint 结果和 table mask 暴露到维护查询或 DeploymentGate evidence。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P2 的 binary/TLV 存储格式和 System Pack 导入策略。

### REFMEM-TASK-20260813-011 - 静态模型 linter 首版

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 P2 静态应用模型从基础字段合法性检查升级为关系一致性 linter。
  - 为后续 RUN gate、owner 写权限和 System Pack 导入提供统一的模型验证结果。
- 完成内容：
  - `refmem_application_model_snapshot_t` 增加 `lint_error_count` 和 `first_lint_error`。
  - 增加 `refmem_app_lint_error_t`，首版覆盖表版本、节点范围、实例范围、实例引用、资源冲突、IO 冲突、重复 writer、事件链路、数据链路、gate/quality 错误。
  - linter 检查节点声明的 instance range 是否确实归属该节点。
  - linter 检查同节点启用实例的独占资源冲突；首版将 Flash、SD、USB、LCD 作为硬独占资源，RJ45/PIO/DMA/core1 保留给后续按实例细化。
  - linter 检查同节点启用实例的独占 IO 冲突；首版将 link-control、BiSS-C、UART/RS485 作为硬独占 IO，SMA/RJ45_SYNC 暂按分布式链路能力处理。
  - linter 检查 `slot_path` 的 writer 唯一性，禁止同一字段被不同实例声明为 writer。
  - linter 检查 START、STOP、FIRE_LOAD、DONE、FAULT 必需事件链路存在。
  - linter 检查关键 slot 的 data link 覆盖：System、Role、VDC、Loop、DPLL、Node、Trigger、IO、Calibration、AckCommand、Gateway。
  - `refmem_application_model_validate()` 改为使用 linter 汇总结果；RefMem status 的 `APP_MODEL_VALID` 位继续由 snapshot valid 派生。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813130200`，package CRC `0xA3A4BDE6`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813130200 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_app_linter` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813130200`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_app_linter` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,96185,0,8,96184,96184,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,96189,96190,101105,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,96196,2,0,1,15,3840,2,7,2,2463022334,0,7`；`SYSTem:PROTection:STATus? => 1,96202,1,1,1,0,0,0,2,11,2,737992584,0,7`。
- 还需完成：
  - 把 `lint_error_count/first_lint_error` 暴露到后续维护查询或 RUN gate evidence。
  - 将 PIO/DMA/core1/RJ45 的共享/独占策略从硬编码升级为 resource class 表。
  - 增加 System Pack 导入前的离线 linter 和故障注入测试。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 继续 P2 的 binary/TLV 存储格式和 System Pack 导入策略，或将 linter 结果接入 DeploymentGate / RUN gate。

### REFMEM-TASK-20260813-010 - P2 六张静态应用模型表落代码

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 P2 文档定义的六张静态分布式应用模型表落到 `refmem_application_model.h/.c`。
  - 首版建立 static const 表、CRC bundle、getter 和 validate，暂不做动态加载、不新增 SCPI 命令面。
- 完成内容：
  - 新增 `refmem_application_model.h/.c`，包含 `DistributedApplicationMap`、`DistributedFbInstanceTable`、`DistributedEventLinkTable`、`DistributedDataLinkTable`、`DistributedDeploymentGate` 和 `DistributedConnectionQualityTable` 的首版结构与静态实例。
  - `DistributedApplicationMap` 覆盖 A0-A7 八个通用节点；A0-A3 为产品链路节点，A4 为调试期 model_vna/model_turntable/test_agent 组合节点，A5-A7 保留为 spare board。
  - 表内实例覆盖 SystemAO、RefMemSyncFB、LoopEngineAO、TriggerAO、LinkSwitcherAO、GatewayAO、CalibrationAO、ModelVnaAO 和 ModelTurntableAO。
  - 增加每张表的 CRC，以及汇总 `package_crc32`；对含字符串指针的表按字段和字符串内容逐项计算 CRC，避免指针地址污染。
  - 增加 `refmem_application_model_validate()`，首版检查 node id、instance id、event/data link 引用、slot ref、target mask、gate check 和 quality scope 的基本一致性。
  - `distributed_refmem_init()` 接入 `refmem_application_model_init()`，并通过 `DISTRIBUTED_REFMEM_FLAG_APP_MODEL_VALID` 把模型有效状态并入 `SYSTem:REFMEM:STATus?` flags。
  - 根 `CMakeLists.txt` 纳入 `refmem_application_model.c`。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813125333`，package CRC `0x7E02FB60`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813125333 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_app_model` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813125333`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_app_model` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,99552,0,8,99551,99551,7`，其中 flags `7` 表示 directory valid、directory CRC valid 和 application model valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,99557,99558,104473,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,99562,2,0,1,15,3840,2,7,2,3508053401,0,7`；`SYSTem:PROTection:STATus? => 1,99567,1,1,1,0,0,0,2,11,2,1780001644,0,7`。
- 还需完成：
  - 定义静态模型表 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略。
  - 增加更完整的静态模型 linter，检查资源/IO claim 冲突、writer 唯一性和 event/data link 完整性。
  - 将 DeploymentGate 输出映射到 RUN gate、诊断 evidence 和更细的维护查询。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `CMakeLists.txt`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 在 P2 继续补静态模型 linter 与 System Pack/TLV 存储格式，或回到 P3 用模型表支撑 slot guard/owner 检查。

### REFMEM-TASK-20260813-009 - Directory CRC 与 slot map 校验

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 为 `DistributedVectorTable` 增加 directory CRC 和 slot directory 自检，防止 slot map 半更新或 layout 边界错误进入运行事实。
  - 保持本轮为 RefMem 内部实现，不新增顶级 SCPI 域，不改变既有 `SYSTem:REFMEM:*` 查询格式。
- 完成内容：
  - `refmem_vector_header_slot_t` 增加 `directory_crc32` 字段，header size 继续由 static assert 冻结为 1 KB。
  - `refmem_vector_table.c` 增加 `refmem_vector_directory_crc()` 和 `refmem_vector_table_validate_directory()`。
  - directory 校验覆盖 slot_count、offset 顺序、非零 size、64 KB 边界和表尾精确覆盖。
  - `distributed_refmem.c` 初始化 directory 后固化 CRC，并在 runtime publish 时刷新 directory valid / CRC valid flags。
  - `distributed_refmem.h` 增加 `DISTRIBUTED_REFMEM_FLAG_DIRECTORY_VALID` 和 `DISTRIBUTED_REFMEM_FLAG_DIRECTORY_CRC_VALID`，由 `SYSTem:REFMEM:STATus?` 的 flags 字段暴露维护状态。
  - `refmem_vector_header_crc()` 改为排除 `header_crc32` 字段自身的分段 CRC，避免把该字段值或占位零错误纳入 header CRC。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813123640`，package CRC `0xC037DE57`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813123640 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_directory_crc` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813123640`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_directory_crc` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,123715,0,8,123714,123714,3`，其中 flags `3` 表示 directory valid 与 directory CRC valid 均置位；`SYSTem:REFMEM:NODE? => 0,1,123720,123721,128635,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,123726,2,0,1,15,3840,2,3,2,2158064260,0,3`；`SYSTem:PROTection:STATus? => 1,123732,1,1,1,0,0,0,2,11,2,1346783678,0,3`。
- 还需完成：
  - 为全部 slot 增加统一 guard 或等价兼容结构。
  - 实现 slot owner 写权限检查和 seqlock/双缓冲。
- 关联文件：
  - `components/distributed_refmem/inc/distributed_refmem.h`
  - `components/distributed_refmem/inc/refmem_vector_table.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/distributed_refmem/src/refmem_vector_table.c`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P3 下一项，设计并落地 slot 统一 guard。

### REFMEM-TASK-20260813-008 - Vector Table layout 拆分

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 `distributed_refmem.c` 内部的 64 KB `DistributedVectorTable` layout、slot enum、header/node slot、directory 初始化和 header CRC 拆出为独立 `refmem_vector_table.h/.c`。
  - 让 `distributed_refmem.c` 只保留 RefMem runtime 发布、节点状态和维护 snapshot 逻辑。
- 完成内容：
  - 新增 `components/distributed_refmem/inc/refmem_vector_table.h`，集中定义 `REFMEM_VECTOR_MAGIC`、slot id、directory、header slot、node slot 和 64 KB table layout。
  - 新增 `components/distributed_refmem/src/refmem_vector_table.c`，集中实现 table clear、header/node accessor、slot directory 初始化和 header CRC。
  - 将 table/header/node 结构体 size static assert 移入 `refmem_vector_table.c`，继续冻结 1 KB header、512 B node slot 和 64 KB table。
  - 修改 `distributed_refmem.c`，通过 `refmem_vector_table_*` API 访问向量表，移除本文件内的 layout 私有定义。
  - 修改根 `CMakeLists.txt`，把 `refmem_vector_table.c` 纳入当前构建。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，warnings=0。
  - 旧布局内部符号清理检查通过：`DISTRIBUTED_REFMEM_MAGIC`、`DISTRIBUTED_REFMEM_SLOT_COUNT`、`distributed_vector_table_t`、`distributed_refmem_*_slot_t`、`distributed_refmem_fast_crc32` 在 `components/distributed_refmem/` 中无残留。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260813122753`，package CRC `0x614E2152`。
  - `python tools/ota_send/ota_send.py COM6 build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT` 通过，OTA 进入 `READY_TO_REBOOT`。
  - `python tools/ota_boot_commit/ota_boot_commit.py COM6 --expected-build 20260813122753 --out-dir build-rtos-multicore-smoke/ota_boot_commit_refmem_vector_split` 通过，启动后 `SYSTem:FW:BUILD?` 返回 `20260813122753`，并完成 `SYSTem:OTA:COMMit`。
  - `python tools/multicore_board_validate/multicore_board_validate.py COM6 --out-dir build-rtos-multicore-smoke/validation_refmem_vector_split` 通过，16/16 passed。
  - 板端维护查询通过：`SYSTem:REFMEM:STATus? => 65536,1,116625,0,8,116624,116624,0`；`SYSTem:REFMEM:NODE? => 0,1,116629,116630,121542,0,0,0,0`；`SYSTem:CORE:VECTOR? => 1,116634,2,0,1,15,3840,2,0,2,619701535,0,0`；`SYSTem:PROTection:STATus? => 1,116639,1,1,1,0,0,0,2,11,2,3143599354,0,0`。
  - 默认 `build` 目录不适用于当前分支：该分支要求 `PROJECT_USE_FREERTOS=ON` 和 `PROJECT_USE_MULTICORE=ON`。
- 还需完成：
  - 为 DistributedVectorTable 实现 directory CRC、slot directory 校验、统一 guard、owner 写权限和 seqlock/双缓冲。
  - 继续将 `distributed_refmem` 拆成 RefMem Domain 子模块。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_vector_table.h`
  - `components/distributed_refmem/src/refmem_vector_table.c`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `CMakeLists.txt`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 P3 下一项，实现 directory CRC 和 slot directory 校验。

### REFMEM-TASK-20260813-007 - 虚拟反射内存参考框架补足

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 基于 NASA cFS Table Services、OpenSHMEM / MPI RMA、MPI RMA 和 IEC 61499 的一手机制，补足 RefMem 主域框架。
  - 将参考项目落到可实现的表生命周期、受控 RMA window、completion、fence/quiet 和静态应用模型检查。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加虚拟反射内存参考机制矩阵。
  - 增加 `RefMemTableRegistry` 框架，覆盖 table id、owner、offset/size、layout version、active/staging CRC、validation state、validator id、last result 和 evidence。
  - 增加 staging/active 表生命周期：`LOAD_TO_STAGING -> CRC_CHECK -> OWNER_VALIDATE -> ACTIVATE -> ACTIVE -> ROLLBACKABLE/FAILED`。
  - 增加 `RefMemRmaWindow` 受控子集，限制为 slot delta、command flag、dirty bitmap、heartbeat/seq、quality counter 等白名单字段。
  - 增加 RMA completion 语义：`origin_encoded -> ring_sent -> target_received -> target_crc_ok -> target_owner_validated -> target_committed -> visible_in_snapshot`。
  - `REFMEM_DOMAIN_TODO.md` 补充 TableRegistry、staging/active/rollback、owner validation、RMA window 和 completion 实现项。
- 验证结果：
  - 本任务为文档框架补足，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将 `RefMemTableRegistry` 和 `RefMemRmaWindow` 落到代码组件。
  - 补充 staging/active load/dump 的 System Pack 存储格式。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 进入 RefMem P3/P5 代码前，冻结 registry table id 和 RMA atomic 白名单。

### REFMEM-TASK-20260813-006 - 外部参考机制收敛到 RefMem 待办

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将 NASA cFS Table Services、OpenSHMEM / MPI RMA、IEC 61499 的可借鉴机制收敛到 RefMem Domain 待办。
  - 明确 RefMem 只吸收表驱动、CRC、owner validation、RMA completion、atomic/fence 和静态 FB 图，不引入完整外部协议栈或动态分布式运行时。
- 完成内容：
  - `REFMEM_DOMAIN_TODO.md` 增加“参考项目收敛原则”矩阵，定义每个外部参考项目的借鉴机制和本项目落地边界。
  - 新增 P1.5 外部参考机制工程化收敛章节，列出 cFS、RMA、IEC 61499 到文档和实现的映射任务。
  - P2 增加静态模型 linter、package CRC 和 FB 图版本门禁待办。
  - P3 增加 RefMem Table Registry、active/inactive image 生命周期、owner validation callback 和 dump/load 镜像规则待办。
  - P4 增加 command slot atomic API、completion 语义和 memory order / fence 规则待办。
  - P5 增加 RefMem RMA Window、delta completion、远端原子更新白名单、RMA-style fence 和 compact timestamp / delta frame 分层待办。
  - P8 增加 cFS-style 和 RMA-style 故障注入验证项；VDC/DPLL 类参考拆分到 VDC Domain 待办维护。
- 验证结果：
  - 本任务为文档待办推进，未修改代码，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 在 `REFMEM_DOMAIN_ARCHITECTURE.md` 增加虚拟反射内存参考机制矩阵。
  - 在 VDC Domain 中补齐 offset/rate/quality 与 initial sync/drift compensation/holdover 的字段映射。
  - 后续按 P3/P4/P5/P8 把参考机制转成代码和验证闭环。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 继续完善 `REFMEM_DOMAIN_ARCHITECTURE.md` 的外部参考机制章节，避免 TODO 和架构正文脱节。

### REFMEM-TASK-20260813-005 - Command / ACK / NACK 契约定义

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 进入 P4，定义 RefMem `AckCommandSlot` 的命令意图、ACK/NACK、busy、timeout、reason 和 evidence 契约。
  - 将现有 `SYSTem:CONFigure:ACK? / NACK?` 收敛为底层 command slot 的配置门禁视图。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 Command / ACK / NACK 契约，明确写命令返回 accepted 不代表动作完成。
  - 定义 `AckCommandSlot` 字段：`command_seq`、source、target、required mask、command type/class、payload ref/CRC、epoch/run_id、timeout、taken/ack/nack/busy/timeout 位图、reason、evidence 和 clear_seq。
  - 定义命令类型首版集合：`CONFIG_STAGE`、`CONFIG_ACTIVATE`、`START/STOP`、`ARM`、`FIRE_LOAD`、`CAL_START`、`SYNC_START_STOP`、`FAULT_CLEAR`、`RESOURCE_JOB` 等。
  - 定义命令状态机：`IDLE -> POSTED -> TAKEN -> EXECUTING/BUSY -> ACKED/NACKED/TIMED_OUT/FAULTED -> CLEAR_PENDING -> IDLE`。
  - 定义重复 `command_seq`、payload CRC mismatch、epoch mismatch、timeout、clear_seq 和 stale 策略。
  - 定义产品化 NACK reason 扩展列表。
  - `REFMEM_DOMAIN_TODO.md` 与 `RTOS_HAOFV_TODO.md` 将 P4 文档定义项标记完成，并拆出 `refmem_command.h/.c`、system_manager 映射和通用 `SYSTem:COMMand:*` 评估待办。
  - `SCPI_COMMAND_PLAN.md` 和 `SCPI_COMMANDS.md` 同步 ACK/NACK 单事实源规则。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本任务为文档契约定义，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将 AckCommandSlot 字段落到 `refmem_command.h/.c`。
  - 将现有 `system_manager` 配置 ACK 迁移或映射到 RefMem AckCommandSlot snapshot。
  - 扩展 NACK reason 表并评估通用 `SYSTem:COMMand:ACK? / NACK?`。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/arch/RTOS_HAOFV_TODO.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/interface/SCPI_COMMANDS.md`
- 下一步：
  - 进入 P5，定义 `REFMEM_DELTA` / `REFMEM_EPOCH` 帧格式、slot delta CRC、seq、timestamp、RJ45_SYNC_RING stale 和重放策略。

### REFMEM-TASK-20260813-004 - DistributedVectorTable 契约冻结

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 进入 P3，冻结 64 KB `DistributedVectorTable` 的 slot offset、slot size、layout version、slot owner、snapshot 和时间字段契约。
  - 以当前 `components/distributed_refmem/` P0 实现为基线，明确文档冻结内容和后续代码实现项。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 将 64 KB 表格从“建议大小”升级为固定 offset/size，表尾固定 `0x10000`。
  - 增加 Header/Directory 契约，定义 `magic/end_magic`、`layout_version`、`table_seq`、`epoch_id`、`run_id`、slot directory、directory CRC 和兼容版本。
  - 增加 slot guard 契约，定义 `slot_seq`、owner、writer、crc、stale、flags、write_epoch、write_tick32。
  - 增加 owner 与写权限表，明确各 slot 的唯一 writer 和禁止事项。
  - 增加 snapshot 与并发契约，定义 `DIRECT_ATOMIC`、`SEQLOCK`、`DOUBLE_BUFFER`、`EVIDENCE_REF` 四类策略。
  - 增加 Version Bundle，统一 layout、application、config、calibration、sync、loop、action、permission、storage、build 和 hw profile 版本。
  - 增加时间字段与回绕规则，区分 `tick32`、`epoch_id + tick32` 和 `dc_time64_ns`。
  - `RTOS_HAOFV_ARCHITECTURE.md` 同步固定 offset/size 表格，并把详细契约指向 RefMem canonical。
  - `REFMEM_DOMAIN_TODO.md` 与 `RTOS_HAOFV_TODO.md` 将 P3 文档冻结项标记完成，并拆出代码实现项。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本任务为文档契约冻结，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将 `distributed_refmem.h/.c` 拆出 `refmem_vector_table.h/.c`。
  - 实现 directory CRC、slot directory 校验、统一 guard、owner 写权限、seqlock/双缓冲和运行上下文字段。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_TODO.md`
- 下一步：
  - 进入 P4，定义 Command / ACK / NACK 槽原子 Take/Clear、command_seq、target mask、busy/timeout/reason 和 SCPI ACK/NACK 对齐。

### REFMEM-TASK-20260813-003 - 静态分布式应用模型细化

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 按 A0-A7 通用节点模型细化 RefMem 的静态分布式应用模型。
  - 明确脉冲分发、链路切换、仪表控制、模型网分、模拟转台、网关和测试代理都是加载到通用节点上的 role/persona/instance。
  - 为后续 `refmem_application_model.h/.c` 和 RUN gate 实现提供字段契约。
- 完成内容：
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedApplicationMap` 字段、规则和实例类型约束。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedFbInstanceTable`，覆盖 instance、domain、版本、资源/IO claim、预算、状态 slot 和冲突分类。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedEventLinkTable`，覆盖 START/STOP/FIRE_LOAD/DONE/FAULT/ACK/NACK 的静态事件路径。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedDataLinkTable`，定义 slot writer/reader、类型、单位、值域、生命周期、snapshot 和 stale 策略。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedDeploymentGate`，把 layout、node、instance、resource、IO、writer、event、data、config、cal/sync quality 纳入 RUN 门禁。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 增加 `DistributedConnectionQualityTable`，覆盖 seq、CRC、stale、late、drop、timeout、p99/p999 和 evidence。
  - `RTOS_HAOFV_ARCHITECTURE.md` 同步实例类型，避免 RTOS 文档仍只描述模型节点和网关。
  - `REFMEM_DOMAIN_TODO.md` 与 `RTOS_HAOFV_TODO.md` 将 P2 文档定义标记为完成，并拆出代码落地、TLV/CRC/System Pack 和 RUN gate 接入待办。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本任务为文档模型细化，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将六张静态模型表落到 `refmem_application_model.h/.c`。
  - 定义静态模型表的 binary/TLV 存储格式、CRC、版本兼容和 System Pack 导入策略。
  - 将 DeploymentGate 输出映射到 `SYSTem:REFMEM:STATus?`、诊断 evidence 和 RUN gate。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_TODO.md`
- 下一步：
  - 进入 P3，冻结 `DistributedVectorTable` 64 KB layout、slot directory、slot owner、snapshot 和回绕安全时间字段契约。

### REFMEM-TASK-20260813-002 - RefMem 内部主域 P0/P1 同步

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 继续进行 RefMem 主域升级，把 RefMem 从文档目录和架构 layer 明确同步为 HAOFV 内部基础主域。
  - 明确 A0-A7 是八个通用节点，模型网分、模拟转台、网关和测试代理只是加载到通用节点上的实例。
  - 明确不冲突时同一通用节点支持同时载入多个逻辑实例。
- 完成内容：
  - `HAOFV_ARCHITECTURE.md` 将 `Distributed Vector Blackboard / RefMem Sync` 表述为内部主域。
  - `RTOS_HAOFV_ARCHITECTURE.md` 将 `task_refmem_sync` 描述为当前任务壳承载 `DistributedRefMemAO / RefMemSyncFB`。
  - `SCPI_COMMAND_PLAN.md` 和 `SCPI_COMMANDS.md` 明确 `SYSTem:REFMEM:*` 是 RefMem 内部主域的系统维护入口，不建立裸顶级 `REFMEM` SCPI 域。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 补充节点模型硬规则：RefMem 底座只固定 A0-A7 八个通用节点。
  - `REFMEM_DOMAIN_ARCHITECTURE.md` 补充多实例共存规则：在资源、IO、时序、owner、slot writer、事件连接和数据连接不冲突时，同一通用节点允许同时载入多个逻辑实例。
  - `arch/README.md` 和 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` 将 RefMem 主域加入阅读顺序和内部架构域目录规划。
- 验证结果：
  - `python tools/docs_check/docs_check.py` 通过，保留 7 个既有文件命名 warning。
  - 本任务为文档主域同步，未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 将 `RTOS_HAOFV_TODO.md` 的 P1 从 RTOS 总待办进一步收敛到 RefMem Domain 子待办。
  - 建立 ApplicationMap / FbInstanceTable / EventLinkTable / DataLinkTable / DeploymentGate / ConnectionQualityTable 的详细设计。
  - 后续进入代码组件化前，先冻结 `DistributedVectorTable` slot 字段契约。
- 关联文件：
  - `docs/arch/HAOFV_ARCHITECTURE.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
  - `docs/arch/README.md`
  - `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/arch/HAOFV_MAINTENANCE_TODO.md`
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
- 下一步：
  - 按 `REFMEM_DOMAIN_TODO.md` 的 P2/P3，先定义静态分布式应用模型和 VectorTable slot 契约。

### REFMEM-TASK-20260813-001 - RefMem 三份标准文档建立

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 `docs/refmem/` 中建立 RefMem 内部主域的三份标准文件。
  - 将“反射内存向量表升格为 Distributed Vector Blackboard / RefMem Sync 内部主域”的影响面写入文档。
- 完成内容：
  - 新增 `REFMEM_DOMAIN_ARCHITECTURE.md`，定义 RefMem Domain 的定位、职责边界、HAOFV 层级、静态分布式模型、核心数据面、SCPI 边界、当前实现现状和目标代码形态。
  - 新增 `REFMEM_DOMAIN_TODO.md`，把需要修改的现有文件、建议新增的文档、建议新增的代码文件和实施阶段拆成 P0-P8 待办。
  - 新增 `REFMEM_TASK_PROGRESS.md`，作为 RefMem 主域独立任务进度入口。
- 验证结果：
  - 本任务为文档生成，尚未执行 docs check、构建、烧录或板端 SCPI。
- 还需完成：
  - 更新 `docs/refmem/README.md`，加入三份标准文档入口。
  - 更新 `docs/arch/README.md` 和 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`，把 RefMem 明确为内部主域。
  - 将 HAOFV/RTOS/SCPI 文档中 RefMem 的定位同步为内部主域。
- 关联文件：
  - `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 按 `REFMEM_DOMAIN_TODO.md` 的 P0/P1 更新索引和架构入口。
