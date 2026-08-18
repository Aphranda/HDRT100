# TDMA 基础件主域任务进度

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_TASK_PROGRESS.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-08-18

本文档记录 TDMA foundation 的阶段性任务进度、验证结果和后续动作。待办事项放在 `TDMA_DOMAIN_TODO.md`。

## 记录规则

每条任务记录使用以下格式：

```text
### TDMA-TASK-YYYYMMDD-NNN - 标题

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

TDMA Domain 当前目标是把公共 `components/tdma` 从 RefMem/VDC 之间的隐式基础件，升级为 HAOFV 下的显式 foundation domain：

```text
TdmaSchedulerAO
+ TdmaRuntimeFB
+ TdmaPayloadRegistry
+ TdmaRingRuntime
+ TdmaQualityVector
+ TransportAdapter
```

VDC 消费 observation evidence，RefMem 消费 data/completion evidence，二者都不拥有上/下行 TDMA 环路。

## 任务记录

### TDMA-TASK-20260818-001 - PIO SPI ring adapter host loopback evidence 模型收敛

- 状态：完成 host 测试模型修正、host 单测、文档检查和 RTOS multicore smoke 构建；两板 HIL 待后续执行。
- 日期：2026-08-18
- 任务目标：
  - 保留 `tdma_pio_spi_ring_adapter` 真实实现中的 4x RX polling 与 500 Hz emission 相位裕量策略。
  - 修复 host loopback 测试模型无限回放同一 TX 帧的问题，避免测试把同一帧重复计入 RX evidence。
  - 覆盖 500 Hz throttled round 行为：UP ready 保持，未发新帧时 down/evidence 关闭，下一发射周期再恢复。
- 完成内容：
  - `test_tdma_pio_spi_ring_adapter.c` 的 loopback physical stub 增加 `rx_pending`，每次 TX 只生成一个可消费 RX 帧。
  - 测试期望改为显式验证二分频发射周期：第二个 service 不推进 beacon/sequence/evidence，第三个 service 推进到下一帧。
  - 坏帧注入不再被同一轮 loopback 正常帧覆盖，可稳定验证 `RX_BAD_FRAME`、`down_running=0` 和 `ring_seq` 不前进。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，26/26 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 2 个 risk review 文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260818055634`，package CRC `0x32CE4358`。
- 还需完成：
  - 两块最小系统板上执行常驻 UP/DOWN TDMA HIL，验证实际 1 MHz 环路下 `ring_seq`、`idle_beacon_tx/rx`、`down_running` 和 VDC evidence 是否持续增长。
- 关联文件：
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`
- 下一步：
  - 回到 1 MHz TDMA/RefMem HIL 闭环，按 COM IDN 自动识别和 GPIO16-19/21-24 线序继续验证。

### TDMA-TASK-20260817-013 - 常驻双向 PIO SPI 物理层与 adapter 模块化接入

- 状态：完成常驻物理层、adapter REFERENCE/FORWARD role、adapter 注册表和 host/构建门禁；两板烧录 HIL 尚未执行。
- 日期：2026-08-17
- 任务目标：
  - 执行 P0.5-3 的固件侧：两板同时 UP/DOWN 常驻短帧，空闲持续 `IDLE_BEACON`，不依赖 host 交替维护命令。
  - 按 HAOFV adapter 边界预留模块化切换空间（当前 PIO SPI，后续 BISS-C/UART/RS485 可注册替换）。
- 完成内容：
  - 新增 `tdma_pio_spi_phys.h/.c`：无 CS 3-wire PIO SPI 常驻物理层（25 MHz，downlink master TX + uplink slave RX 双 SM 同时 arm，复用已验证的 `tx_byte/rx_byte` PIO 程序和 4B magic+length 定界）；`phys_arm` 按 ring config 的 local slot 选择 downlink SCK（slot 0 -> 22、slot 1 -> 21，对应实测线序）。
  - 新增 `tdma_pio_spi.pio`（TDMA 命名空间副本，避免 tdma 依赖 refmem 生成头）。
  - `tdma_pio_spi_ring_adapter` 增加 REFERENCE/FORWARD role：`local==reference` 时发新 `IDLE_BEACON` 并收环回帧；否则收上一板帧、`advance_hop` 转发（保持 origin/sequence/identity CRC、重算 transport CRC），`forward_count` 计入 snapshot；`set_phys_ctrl` 在 adapter start/stop 时驱动物理层 arm/disarm。
  - `tdma_service` 增加 adapter 注册表 `tdma_service_register_adapter_impl()`：`tdma_service_configure_foundation_profile()` 按 `resource.adapter_type` 绑定对应实现，未注册类型解绑并报 `ADAPTER_MISSING`；`tdma_ring_runtime_unbind_adapter()` 支持解绑。
  - `tdma_runtime_owner` 改为注册 `TDMA_ADAPTER_PIO_SPI` 实现（不再直接绑定），profile 激活时自动按 adapter_type 选择。
  - board_config.h 增加 `BOARD_TDMA_SPI_*` 宏（复用已验证 uplink/downlink 引脚）。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过：REFERENCE 发 beacon + 收环回（evidence=1 需要硬件 timestamp metadata）；FORWARD 收帧转发（hop 0->1、identity/sequence 保持、`simultaneous_feedback_loop_evidence` 保持 0）；phys_ctrl arm/disarm 由 start/stop 驱动。
  - `run_tdma_service_scheduler_tests.ps1` 通过：PIO_SPI profile 绑定 SPI 实现、BISS_C profile 切换到 BISS_C 实现、未注册 UART 解绑报 `ADAPTER_MISSING`。
  - `run_tdma_ring_runtime_tests.ps1`、`run_tdma_transport_frame_tests.ps1` 回归通过。
  - `run_host_unit_tests.ps1`（全局 GCC `D:\Embedded\GCC\mingw64\bin`）通过，26/26 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标（含 `tdma_pio_spi.pio` pioasm 生成与 `tdma_pio_spi_phys.c` 编译链接）。
  - 本轮未烧录板卡；`up/down_running=1` 与 `simultaneous_feedback_loop_evidence=1` 的板端值待两板 HIL。
- 还需完成：
  - 两板烧录后验证常驻 IDLE_BEACON：`up/down_running=1`、`ring_seq` 增长、SCPI 查询不 disarm。
  - P0.5-4/5：ring timestamp evidence（reference TX、每 hop RX/TX、feedback RX）与 PIO 硬件 timestamp latch（`HARDWARE_TICK / <=100 ns`）后置位 `simultaneous_feedback_loop_evidence`。
  - P0.5-6：HIL 只读监控脚本。
- 关联文件：
  - `components/tdma/inc/tdma_pio_spi_phys.h`、`components/tdma/src/tdma_pio_spi_phys.c`
  - `components/tdma/src/tdma_pio_spi.pio`
  - `components/tdma/inc/tdma_pio_spi_ring_adapter.h`、`components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `components/tdma/inc/tdma_service.h`、`components/tdma/src/tdma_service.c`
  - `components/tdma/inc/tdma_ring_runtime.h`、`components/tdma/src/tdma_ring_runtime.c`
  - `components/tdma/src/tdma_runtime_owner.c`
  - `boards/rp2350_trig/inc/board_config.h`
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`、`tests/unit/test_tdma_service_scheduler.c`
  - `CMakeLists.txt`
- 下一步：
  - 两板烧录常驻环 HIL；随后 P0.5-4/5 的 ring timestamp evidence 与硬件 latch。

### TDMA-TASK-20260817-012 - PIO SPI ring adapter 绑定与 ADAPTER_MISSING 消除

- 状态：完成 transport 级 ring adapter、runtime owner 绑定、host 单测和 A/B 构建；物理双向 PIO/DMA 常驻帧（P0.5-3）与两板 HIL 尚未完成。
- 日期：2026-08-17
- 任务目标：
  - 执行 P0.5-1：为最小系统 PIO SPI bring-up adapter 绑定 `TdmaRingAdapterOps`，消除 `ring_last_error=4(ADAPTER_MISSING)`。
  - 执行 P0.5-2：发布 adapter 生命周期 evidence 和 idle beacon / timestamp 元数据。
  - 不得伪造 `simultaneous_feedback_loop_evidence`。
- 完成内容：
  - 新增 `components/tdma/inc/tdma_pio_spi_ring_adapter.h` 与 `components/tdma/src/tdma_pio_spi_ring_adapter.c`，实现 `tdma_ring_adapter_ops_t` 的 start/stop/service。
  - adapter 为 transport 级：只编解码 `TdmaTransportFrame`（空闲时构建并发送 `IDLE_BEACON` 短帧，解析 RX 帧并校验 schedule/ring CRC），不接触 `refmem_sync_frame` 或 VDC/RefMem 内帧。
  - service 维护 UP sequence、DOWN RX sequence、identity CRC、idle beacon TX/RX 计数、reference TX / feedback RX timestamp 元数据（source/resolution/flags 由 `tdma_pio_spi_ring_adapter_set_timestamp_metadata()` 声明），全部投影到 `tdma_ring_adapter_status_t`。
  - 物理层以可选 `phys_tx` / `phys_rx` 钩子接入（`tdma_pio_spi_ring_adapter_set_phys()`）；未接物理 TX 时 service 返回 false，ring runtime 报告 `EVIDENCE_MISSING` 而非伪造 running；RX 也可经 `tdma_pio_spi_ring_adapter_inject_rx()` 注入（host 测试）。
  - `tdma_runtime_owner_init()` 创建 adapter 并调用 `tdma_service_bind_ring_adapter()`；新增 `tdma_runtime_owner_get_ring_adapter()` 供板端后续接入物理钩子。
  - 新增 host 单测 `tests/unit/test_tdma_pio_spi_ring_adapter.c` 和 `tools/tests/run_tdma_pio_spi_ring_adapter_tests.ps1`，并纳入 `run_host_unit_tests.ps1`。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过：未绑定 → `ADAPTER_MISSING`；绑定无物理 → `EVIDENCE_MISSING`、`adapter_started=1`、start/service 计数增长；回环 phys + 硬件 timestamp（100 ns / `HARDWARE_LATCHED`）→ `up/down_running=1`、`simultaneous_feedback_loop_evidence=1`、round trip=500 ns、beacon 计数增长；无硬件 timestamp 或 `DIAGNOSTIC_ONLY` → evidence=0 且 `TIMESTAMP_MISSING`；坏帧 → `down_running=0`、`rx_bad_count` 增长并恢复；队列溢出计数。
  - `run_tdma_ring_runtime_tests.ps1`、`run_tdma_transport_frame_tests.ps1` 回归通过。
  - `run_host_unit_tests.ps1`（全局 GCC `D:\Embedded\GCC\mingw64\bin`）通过，26/26 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817130228`，package CRC32 `0x40859A25`。
  - 本轮未烧录板卡；`up/down_running` 与 `simultaneous_feedback_loop_evidence` 的板端值取决于物理钩子接入（P0.5-3）。
- 还需完成：
  - P0.5-3：两板同时 UP/DOWN 常驻短帧的 PIO/SM/DMA 双向物理层，经 `set_phys()` 接入 adapter。
  - P0.5-4/5：物理 timestamp 证据（`HARDWARE_TICK`、`<=100 ns`、硬件 latch）产生真实 correlation。
  - P0.5-6：HIL 只读监控脚本。
- 关联文件：
  - `components/tdma/inc/tdma_pio_spi_ring_adapter.h`
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `components/tdma/src/tdma_runtime_owner.c`
  - `components/tdma/inc/tdma_runtime_owner.h`
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`
  - `tools/tests/run_tdma_pio_spi_ring_adapter_tests.ps1`
  - `tools/tests/run_host_unit_tests.ps1`
  - `CMakeLists.txt`
- 下一步：
  - 实现双向 PIO SPI 物理钩子（master TX+RX / slave RX+TX 双 SM 同时 arm），两板各自常驻 IDLE_BEACON，再进入 HIL 验收。

### TDMA-TASK-20260817-011 - RTOS 启动期调度槽池收敛

- 状态：完成根因修复、host 单测、A/B 构建和 COM5/COM6 板端验证。
- 日期：2026-08-17
- 问题：
  - FreeRTOS 启动阶段先为 10 个任务分配约 94 KiB stack，随后 TDMA runtime owner 又按最大 32 个 1024 B frame slot 一次申请约 36 KiB。
  - 128 KiB heap 无法同时容纳任务和 TDMA 最大槽池，`tdma_runtime_owner_init()` 失败后应用未进入 ready，表现为 USB CDC 枚举但 SCPI 写超时。
  - RefMem 初始化观测字段显示失败停在 `DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_PROFILE`；默认 foundation profile 的五类 traffic queue depth 总和为 28，超过当前 RTOS runtime active slot pool 的 8。
- 修正：
  - 保留 `TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT=32` 作为实现上限。
  - RTOS 产品运行时先使用 8 个 active slot；默认五类 traffic 收敛为 `VDC=2, RefMem=3, Config=1, Bulk=1, Log=1`，总计 8 槽。
  - 后续 System Pack profile 的 queue depth 总和不得超过 active runtime slot capacity；需要扩大时必须先通过 RTOS heap 水位门禁。
- 验证：
  - `run_tdma_profile_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过。
  - `run_tdma_traffic_scheduler_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，新增 `test_default_profile_fits_runtime_slot_pool()`，用 8-slot runtime pool 配置默认 profile。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260817104554`。
  - COM5 BOOTSEL factory UF2 恢复后 `SYSTem:FW:BUILD?` 返回 `"20260817104554"`，`SYSTem:REFMEM:STATus?` 返回 `...,1,8,0`。
  - COM6 OTA boot/commit 后 `SYSTem:FW:BUILD?` 返回 `"20260817104554"`，`SYSTem:REFMEM:STATus?` 返回 `...,1,8,0`，`SYSTem:OTA:SLOT?` 返回 `2,0,2,0,1`。
  - 两板 `SYSTem:ERRor?` 均返回 `0,"No error"`。
- 后续：
  - 如果后续要把 runtime slot pool 从 8 扩大到 16/32，必须先在 `SYSTem:RTOS:STATus?` 和长时间 HIL 中确认 heap 水位、fragmentation 和 core1 service jitter，再更新默认 System Pack profile。

### TDMA-TASK-20260817-010 - Flight-mutable 短帧与节点数据装载路径

- 状态：完成 transport flight-mutable API、RefMem realtime 容量门禁、ProcessImageMap C 契约和架构流水线；System Pack 表、双缓冲与 PIO cut-through 尚未实现。
- 日期：2026-08-17
- 任务目标：
  - 参考 EtherCAT processing-on-the-fly，明确每个节点如何把本地 VDC/RefMem 小事实装入常驻短帧，而不是周期刷新 64 KB RefMem。
  - 让 immutable ring identity 与沿途可变 process image 解耦。
- 完成内容：
  - `TdmaTransportFrame` 增加 `FLIGHT_MUTABLE`，只允许 short frame 在已授权 payload slice 上更新内容并重算 transport CRC。
  - identity CRC 改为只覆盖不可变路由字段；payload 局部完整性归 segment owner CRC/version，避免节点更新 process image 后破坏反馈相关身份。
  - RefMem TDMA realtime binding 收紧为 260 B 内帧，critical delta 净载荷上限 224 B；总线无关 RefMem 协议仍保留 292 B 理论帧能力。
  - 新增 `tdma_process_image_map.*`，校验 segment owner、payload class、offset/length、策略 flags、唯一 ID、不重叠和 map CRC，并提供 local slot publish 权限查询。
  - 新增 `CYCLIC_PROCESS_IMAGE` payload class，归入最高优先级 short-frame traffic；默认 VDC/RefMem 硬预留各调整为一帧 292 B，修复旧 128 B VDC budget 无法容纳 216 B 诊断帧的问题。
  - 文档冻结 `fact commit -> dirty descriptor -> shadow process image -> cycle swap -> PIO/DMA flight update -> feedback` 主线。
- 验证结果：
  - `run_tdma_transport_frame_tests.ps1` 覆盖 flight payload patch 后 identity 不变、transport CRC 更新、payload 可见和后续 hop 转发。
  - `run_tdma_process_image_map_tests.ps1` 覆盖有效 map、owner publish、越权、重叠、重复 ID 和 CRC 拒绝。
  - `run_refmem_realtime_tdma_tests.ps1` 覆盖 260 B realtime delta 接纳和 261 B 拒绝。
- 还需完成：
  - `TdmaProcessImageMap` 正式 System Pack 表、active/shadow 双缓冲、compact VDC segment 和 dirty RefMem segment。
  - PIO SPI ring adapter 的 RX/TX overlap、固定 offset 更新和每 hop pipeline delay 实测。
- 下一步：
  - 先把 ProcessImageMap 接入 System Pack / DeploymentGate 并冻结 segment wire contract，再接 TDMA 自有 PIO SPI adapter；不能让 adapter 读取业务域内部对象。

### TDMA-TASK-20260817-009 - 通用 Transport Envelope 与长短帧门禁

- 状态：完成 32 B transport envelope、SHORT/LONG 容量和 traffic-class 门禁的代码/host 单测；尚未接入 PIO SPI ring adapter。
- 日期：2026-08-17
- 任务目标：
  - 解除物理 adapter 对 `refmem_sync_frame` 的绑定，使 VDC、RefMem、OTA、SD 和 LOG 可复用同一 transport。
  - 自动同步阶段保持短帧和确定性；宽松同步或维护窗口允许可靠长帧。
- 完成内容：
  - 新增 `tdma_transport_frame.*`，使用固定 32 B、小端 wire header；编码不依赖 C struct padding。
  - transport header 包含 frame class、origin、sequence、payload class、schedule/ring CRC、hop count/limit、identity CRC 和 transport CRC。
  - identity CRC 不随 hop 改变；transport CRC 覆盖当前 hop 和完整 packet，每次转发重算。
  - `SHORT` 总长上限 292 B、净载荷 260 B；`LONG` 总长上限 1024 B、净载荷 992 B。
  - scheduler 冻结 VDC/RefMem realtime 只允许短帧，reliable bulk/LOG 只允许长帧，配置流可选择两者但仍受 maintenance gate 控制。
  - 新增 `STORAGE_BULK` payload class，与 OTA 共同归入 reliable bulk traffic，不创建 SD 私有总线。
- 验证结果：
  - `run_tdma_transport_frame_tests.ps1` 通过，覆盖编解码、短/长帧、CRC、hop、origin feedback、hop limit 和短帧容量拒绝。
  - `run_tdma_traffic_scheduler_tests.ps1` 通过，覆盖 long VDC 拒绝、short LOG 拒绝和 long STORAGE 接纳。
  - `run_tdma_profile_tests.ps1` 通过，System Pack 生成器同步扩展 payload whitelist 和 reliable bulk mask。
  - `run_host_unit_tests.ps1` 使用 Vivado MinGW GCC 全量通过，24/24 host scripts passed。
  - `python -m pytest tests/python/test_refmem_pack_build.py -q` 通过，2 passed；仅保留 OneDrive `.pytest_cache` 权限 warning。
  - `python tools/docs_check/docs_check.py` 通过，保留既有两项 risk review 文件名 warning。
  - A/B 双目标构建通过；build id `20260817083335`，package CRC32 `0x0D7C870E`。
- 还需完成：
  - RefMem realtime binding 的 260 B 内帧和 224 B critical delta 上限已完成；仍需实现分片与 background/bulk 路径。
  - 建立 TDMA 所有的 PIO SPI ring adapter，同时常驻 RX/TX SM，并让 adapter 只处理外层 transport。
  - 将 idle beacon、origin feedback 和硬件 timestamp evidence 接入 `TdmaRingAdapterOps`。
- 关联文件：
  - `components/tdma/inc/tdma_transport_frame.h`
  - `components/tdma/src/tdma_transport_frame.c`
  - `components/tdma/src/tdma_traffic_scheduler.c`
  - `components/tdma/inc/tdma_profile.h`
  - `tests/unit/test_tdma_transport_frame.c`
- 下一步：
  - 先完成 RefMem critical delta 的短帧分片边界，再实现 TDMA PIO SPI ring adapter，避免物理层继续理解业务帧。

### TDMA-TASK-20260817-008 - 双向 adapter 与硬件反馈证据契约

- 状态：完成 ring adapter/runtime 证据基础件和 host 测试；PIO SPI 双向物理 adapter、常驻 IDLE beacon 和两板 HIL 尚未完成。
- 日期：2026-08-17
- 任务目标：
  - 消除 ring profile 配置成功后直接报告 `up_running/down_running=1` 的假运行状态。
  - 为同时 UP/DOWN runtime 冻结 adapter lifecycle 和 reference TX / feedback RX 硬件时间戳相关条件。
- 完成内容：
  - `TdmaRingRuntime` 新增 `TdmaRingAdapterOps.start/stop/service`，core1 只接受 adapter 返回的 configured/running 和事件证据。
  - 未绑定 adapter 时保持两条 leg 停止并报告 `ADAPTER_MISSING`；profile 只表示配置，不表示物理运行。
  - 闭环相关同时检查 sequence、frame CRC、schedule CRC、timestamp 顺序、feedback timeout、`HARDWARE_LATCHED`、非诊断标志和 `<=100 ns` 分辨率。
  - snapshot 增加 adapter start/stop/service 计数、adapter 原始错误码、idle beacon TX/RX 计数、reference/feedback sequence、CRC、timestamp 和 round-trip。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 只在原响应末尾追加上述维护字段，不改变旧字段顺序。
- 验证结果：
  - `run_tdma_ring_runtime_tests.ps1` 通过：无 adapter 不运行；诊断时间戳不产生闭环证据；匹配硬件证据产生闭环；sequence mismatch 立即撤销证据。
  - `run_tdma_service_scheduler_tests.ps1`、`run_vdc_domain_tests.ps1` 和 `run_refmem_realtime_tdma_tests.ps1` 通过。
  - 修复 `tdma_service_get_snapshot()` 未清零输出结构的问题，避免未绑定 traffic scheduler 时读取未初始化的兼容字段。
  - `run_host_unit_tests.ps1 -HostGccDir D:\\Xilinx\\2025.2\\tps\\mingw\\10.0.0\\win64.o\\nt\\bin` 通过，23/23 host scripts passed。
  - A/B 双目标构建通过；build id `20260817075745`，package CRC32 `0x8AEF6A19`。
- 还需完成：
  - 为最小系统 PIO SPI adapter 增加独立 RX/TX pin group、双 SM 同时 arm 和异步 DMA service。
  - 由 adapter 常驻生成/接收 `IDLE_BEACON`，并发布真实 PIO/DMA timestamp evidence。
  - 两板 HIL 通过后才允许 TDMA snapshot 报告物理反馈闭环成立。
- 关联文件：
  - `components/tdma/inc/tdma_ring_runtime.h`
  - `components/tdma/src/tdma_ring_runtime.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_ring_runtime.c`
- 下一步：
  - 将可配置的 GPIO16-24 PIO SPI 双向 adapter 挂到 `TdmaRingAdapterOps`，先取得 running/idle evidence，再补硬件 timestamp correlation。

### TDMA-TASK-20260817-007 - 唯一 runtime owner 与三级 traffic scheduler

- 状态：完成软件调度基础件、公共 owner、三级门禁、per-class completion token、23/23 host 门禁和 A/B 固件构建；尚未形成两板物理闭环 evidence。
- 日期：2026-08-17
- 任务目标：
  - 判断当前 TDMA 是否足以支撑 VDC 闭环，并先关闭软件调度与 owner 层的阻断项。
  - 冻结 `VDC_REALTIME > REFMEM_REALTIME > maintenance`，低优先级流不抢占实时短帧。
  - 消除 VDC/RefMem 各自维护 TDMA service 的双 runtime 现象。
- 完成内容：
  - 新增 `TdmaTrafficScheduler`，实现五类固定队列、周期预算、deadline、time-aware gate、maintenance gate、fault/backpressure/drop-oldest/drop-newest 和基础质量计数。
  - maintenance gate 默认关闭；配置、OTA、LOG 只有 TDMA owner 确认不同步或进入显式维护窗口后才可执行。
  - 新增 `tdma_runtime_owner.*`；VDC 注册 observation payload，RefMem 注册 data payload 并绑定物理 adapter，二者共享唯一 service，core1 每轮只推进一次。
  - 调度帧池从 128 KiB FreeRTOS heap 一次性申请，避免 32 个 long-frame 槽进入 `.bss`；申请失败直接阻止应用初始化。
  - 修正优先级重排下的序号语义：service 内部执行序号与 scheduler enqueue token 分离，并为五类流发布持久 completion token，避免 VDC 后入队先完成时误确认 RefMem intent。
  - result/error/timestamp/frame completion 按 traffic class 独立持久化；VDC 读取 VDC completion metadata，RefMem 读取 RefMem completion frame，后完成的低优先级流不再覆盖实时流证据。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 在旧字段末尾追加 scheduler 配置、enqueue/dispatch、队列水位、fault、last result/class 和五类 completion token。
  - 新增 service/scheduler 集成测试，证明配置先入队时仍按 VDC、RefMem、配置顺序执行；maintenance gate 关闭时配置帧保持排队。
- 验证结果：
  - `run_tdma_traffic_scheduler_tests.ps1` 通过。
  - `run_tdma_service_scheduler_tests.ps1` 通过。
  - `run_refmem_realtime_tdma_tests.ps1` 和 `run_vdc_domain_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，23/23 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817073126`，package CRC `0xEDF7B9AF`。
  - 本轮未烧录板卡；`simultaneous_feedback_loop_evidence` 仍为 0，不能据此进入产品 DPLL 闭环调参。
- 还需完成：
  - 实现同时 UP/DOWN adapter runtime、IDLE_BEACON 和硬件 RX/TX timestamp correlation。
  - 发布正式 `TdmaQualityVector` 并完成两板 HIL。
- 关联文件：
  - `components/tdma/inc/tdma_traffic_scheduler.h`
  - `components/tdma/src/tdma_traffic_scheduler.c`
  - `components/tdma/inc/tdma_runtime_owner.h`
  - `components/tdma/src/tdma_runtime_owner.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_service_scheduler.c`
- 下一步：
  - 先完成 per-class completion metadata，再接 adapter/timestamp correlation；硬件证据通过后才进入 DPLL 闭环。

### TDMA-TASK-20260817-006 - TdmaRingRuntime 基础件拆分

- 状态：完成独立基础件、service 聚合接入、reason code、21/21 host 门禁和 A/B 固件构建；硬件 HIL 尚未执行。
- 日期：2026-08-17
- 任务目标：
  - 将 ring config、core1 runtime 推进和 ring snapshot 从 `tdma_service.c` 单体拆出。
  - 保持现有 `tdma_service_configure_ring_runtime()` 和维护 snapshot 字段兼容。
  - 冻结 adapter/scheduler/timestamp 后续共用的 ring reason code。
- 完成内容：
  - 新增 `tdma_ring_runtime.h/.c`，独立维护 config/result seqlock、config/reject/service/ring seq 和双向运行状态。
  - `tdma_service` 改为聚合 `tdma_ring_runtime_t`，配置、core1 service 和 snapshot 均通过基础件 API 完成。
  - 保留原有 ring snapshot 字段，并新增 `ring_config_reject_count`；维护查询只在末尾追加新字段。
  - 配置校验把 UP/DOWN group 缺失或相同明确映射为 `DIRECTION_CONFLICT`，其余 topology/flag/CRC 错误映射为 `BAD_CONFIG`。
  - 冻结 `EVIDENCE_MISSING`、`ADAPTER_MISSING`、`TIMESTAMP_MISSING`、`PAYLOAD_STARVATION`、`WINDOW_MISSED` 和 `RESOURCE_CONFLICT` 编号，供后续 owner 接入。
  - runtime service 仍强制保持 `simultaneous_feedback_loop_evidence=0`，未提供软件直接置位接口。
  - 新增独立 host 单测及测试脚本，并纳入全量 host 列表。
- 验证结果：
  - `run_tdma_ring_runtime_tests.ps1` 通过。
  - `run_refmem_realtime_tdma_tests.ps1` 通过，包含 config reject 和兼容 snapshot 断言。
  - `run_vdc_domain_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，21/21 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817063552`，package CRC `0xF15A8DA6`。
  - 本轮没有烧录板卡，不形成 UP/DOWN 同时物理运行或硬件闭环 evidence。
- 还需完成：
  - 建立五类固定队列、time-aware gate 和 reason-to-quality 映射。
  - 由 adapter/timestamp correlation 提供其余 reason 的真实发布源。
- 关联文件：
  - `components/tdma/inc/tdma_ring_runtime.h`
  - `components/tdma/src/tdma_ring_runtime.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_ring_runtime.c`
  - `tools/tests/run_tdma_ring_runtime_tests.ps1`
- 下一步：
  - 全量闭环后进入五类固定队列和 time-aware gate 的数据结构设计。

### TDMA-TASK-20260817-005 - TdmaPayloadRegistry 基础件拆分

- 状态：完成独立基础件、service 聚合接入、维护快照、20/20 host 门禁和 A/B 固件构建；未执行硬件 HIL。
- 日期：2026-08-17
- 任务目标：
  - 将 payload binding、whitelist、capacity 和 admission 从 `tdma_service.c` 单体拆出。
  - 保持 VDC/RefMem 既有注册 API 和 RX-window 语义不变。
  - 为后续逐类队列、policing 和 DeploymentGate 提供可查询的 registry 水位。
- 完成内容：
  - 新增 `tdma_payload_registry.h/.c`，支持固定 8-entry registry、binding 注册/替换、active whitelist、short/long capacity 和 frame admission。
  - registry 使用独立 seqlock snapshot，发布 config seq、registration seq、used、admitted、reject、last result 和 last payload class。
  - `tdma_service_register_payload()` 和 submit admission 改为委托 registry；对外 API 名称和 producer 调用方式保持不变。
  - foundation profile 激活时同步配置 registry whitelist 和 capacity；如果现有 binding 与候选 profile 不兼容，runtime profile 配置拒绝。
  - 保留 RX window 的 `frame_size=0` 语义，它表示接收窗口尚无 payload，不按空 TX frame 拒绝。
  - RefMem 兼容 snapshot 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 在末尾追加 registry 水位字段。
  - 新增独立 host 单测及测试脚本，并纳入全量门禁。
- 验证结果：
  - `run_tdma_payload_registry_tests.ps1` 通过。
  - `run_refmem_realtime_tdma_tests.ps1` 通过，包含 profile 配置后 registry seq/binding 集成断言。
  - `run_vdc_domain_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，20/20 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817061911`，package CRC `0x75320FF1`。
  - 本轮没有烧录板卡，不形成 UP/DOWN 同时运行或硬件闭环 evidence。
- 还需完成：
  - ring runtime 已由 TDMA-TASK-20260817-006 拆出。
  - 在 registry admission 之上建立五类固定队列和 time-aware gate。
  - 将 registry reject/deadline/budget 统计并入正式 `TdmaQualityVector`。
- 关联文件：
  - `components/tdma/inc/tdma_payload_registry.h`
  - `components/tdma/src/tdma_payload_registry.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_payload_registry.c`
  - `tools/tests/run_tdma_payload_registry_tests.ps1`
- 下一步：
  - ring runtime 与 reason code 已完成；继续 scheduler queue runtime。

### TDMA-TASK-20260817-004 - Active Profile 到公共 Runtime 的激活闭环

- 状态：完成配置事务闭环、维护可观测性、定向 host 单测和 A/B 固件构建；未执行两板硬件闭环。
- 日期：2026-08-17
- 任务目标：
  - 让 RMTP 第 10 张 `TdmaFoundationProfile` 在激活后真正配置公共 TDMA runtime，而不是只停留在 active table view。
  - 在 TableRegistry 切换前拒绝与当前 VDC ring/schedule 不一致的 candidate，避免 active 表和 runtime 半提交。
  - 保持维护查询只读，并为后续 HIL 暴露 profile、ring 和 feedback evidence。
- 完成内容：
  - `refmem_realtime_tdma` 增加正式 foundation-profile 配置入口，业务上层不再访问内部 `scheduler` 成员。
  - prepared table views 增加 TDMA profile 的受控 copy getter；commit/discard 后 getter 立即失效。
  - VDC ring plan 增加 `cycle_period_ns`，作为 TDMA profile 的跨域只读调度契约。
  - 激活门禁比较 node count、local/reference、upstream/downstream、feedback、ring flags、ring/profile CRC、schedule CRC 和 cycle period。
  - factory profile 在 `distributed_refmem_init()` 中通过同一门禁装入 runtime；System Pack candidate 在 registry 激活和 model commit 后自动装入 runtime。
  - 新增 `REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE`，不一致候选映射为配置验证失败 NACK。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 在原字段末尾追加 profile CRC、owner、adapter、whitelist、ring config/runtime 和 feedback evidence。
- 验证结果：
  - `run_refmem_realtime_tdma_tests.ps1` 使用 Vivado MinGW GCC 通过。
  - `run_refmem_table_registry_tests.ps1` 使用 Vivado MinGW GCC 通过。
  - `run_vdc_domain_tests.ps1` 使用 Vivado MinGW GCC 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817060622`，package CRC `0x0271E168`。
  - 本轮没有烧录两板，也没有产生同时 UP/DOWN、硬件 timestamp correlation 或 `simultaneous_feedback_loop_evidence=1` 的 HIL 证据。
- 还需完成：
  - payload registry 已由 TDMA-TASK-20260817-005 拆出；ring runtime 仍待拆分。
  - 实现五类固定队列、time-aware gate、policing/backpressure 和 `TdmaQualityVector`。
  - 在两板固件中同时常驻运行 UP/DOWN leg，再进入闭环 timestamp HIL。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
- 下一步：
  - 先拆分 `TdmaPayloadRegistry` 并建立可查询的 admission snapshot，再实现逐类固定队列。

### TDMA-TASK-20260817-003 - RMTP Foundation Profile 与 DeploymentGate 闭环

- 状态：完成正式表镜像、跨表资源门禁、host 全量验证和固件构建；profile activation hook 已由 TDMA-TASK-20260817-004 完成。
- 日期：2026-08-17
- 任务目标：
  - 将 TDMA foundation profile 从独立 C contract 升级为可由 SD/System Pack staging、激活和回滚的正式 RMTP 表。
  - 在激活前完成 owner、NodeLoad、SlotClaim、RealtimeCapabilityContract、物理 IO、payload 和容量一致性检查。
  - 冻结可供后续 time-aware scheduler 使用的周期容量、guard band 与 queue RAM 契约。
- 完成内容：
  - RMTP 表数量由 9 扩展为 10，新增 table id 9 `TdmaFoundationProfile`，owner 标记为 `TDMA_AO`。
  - 新增 71-word 固定 `u32 little-endian` profile row；编码/解码逐字段执行，不依赖 C struct padding。
  - profile 增加 cycle period、cycle capacity、guard band 和 queue RAM capacity；validator 拒绝总预留预算、单类 MTU 和队列 RAM overcommit。
  - staged candidate 必须恰好存在一个已加载 `TdmaSchedulerAO`，profile owner 必须匹配 NodeLoad local slot、owner resource claim、IO/IP claim 和 SlotClaim/RealtimeCapabilityContract。
  - TDMA adapter IO 改由 foundation owner 独占；业务 FB 只能通过 payload/intent 使用 TDMA，不能重复声明 adapter IO。
  - Python System Pack 生成器、pack builder、registry HIL validator 和 SCPI load validator 全部改为从统一 10 表定义派生 table count、mask 和 stage payload size。
  - active profile 到公共 runtime 的自动配置与 VDC schedule 交叉门禁已由 TDMA-TASK-20260817-004 补齐。
- 验证结果：
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，19/19 host scripts passed。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；仅保留 OneDrive `.pytest_cache` warning。
  - `python tools\docs_check\docs_check.py` 通过，保留既有两项 risk review 文件名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过；最终 build id 和 package CRC 见本次提交记录。
- 还需完成：
  - 建立五类固定队列、time-aware gate、policing/backpressure 和 `TdmaQualityVector`。
  - 在两板硬件上同时运行 UP/DOWN leg，并用硬件 timestamp correlation 置位真实闭环 evidence。
- 关联文件：
  - `components/tdma/inc/tdma_profile.h`
  - `components/tdma/src/tdma_profile.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tools/refmem_table_image/refmem_table_image.py`
- 下一步：
  - profile activation hook 已闭环；继续逐类固定队列与 time-aware gate。

### TDMA-TASK-20260817-002 - Foundation profile、HAOFV owner 与 TSN-style 流治理

- 状态：完成首版代码契约与 host 单元验证；RMTP profile 表已由 TDMA-TASK-20260817-003 补齐，真实逐流 scheduler 尚待实现。
- 日期：2026-08-17
- 任务目标：
  - 将 `TDMARingProfile` owner 从 VDC 迁入 TDMA Foundation。
  - 将 TDMA 表达为 HAOFV 可装载 system node，并冻结运行期资源声明。
  - 面向 VDC、RefMem、配置、OTA 和 LOG，吸收 TSN 的流分类、准入、time-aware gate、整形和背压思想。
- 完成内容：
  - 新增 `tdma_profile.h/.c`，定义 `tdma_ring_profile_t`、`tdma_foundation_profile_t`、adapter、PIO/SM/DMA/core1 resource、frame capacity、payload whitelist 和 profile CRC。
  - `vdc_tdma_schedule_profile_t` 改为只读嵌入 `ring_binding`；VDC schedule CRC 绑定 TDMA ring CRC，ring topology/CRC 校验由 TDMA owner 实现。
  - `tdma_service_configure_foundation_profile()` 将 owner、adapter、PIO/SM/DMA、core1 service、capacity、IO/IP claim、payload whitelist 和 profile CRC 冻结到 runtime snapshot。
  - payload registry 按 active whitelist 做 admission，未登记 payload 被拒绝。
  - 固定五类 traffic class：VDC realtime、RefMem realtime、config control、OTA bulk、LOG best effort；每类定义周期预算、帧数、队列深度、deadline、gate/shaping/preemption 和 overflow policy。
  - RefMem Application Model 增加 TDMA baseline capability、`TdmaSchedulerAO` instance、NodeLoad、resource/IP claim 和 DeploymentGate check；默认模型只允许一个 active TDMA owner。
- 验证结果：
  - `run_tdma_profile_tests.ps1` 使用 Vivado MinGW GCC host 执行通过。
  - `run_vdc_domain_tests.ps1` 使用 Vivado MinGW GCC host 执行通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 全部通过，19/19 脚本均执行 host 测试。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；仅有 OneDrive `.pytest_cache` 无法写入 warning，不影响测试结果。
  - `python tools\docs_check\docs_check.py` 通过，保留既有两项 risk review 文件名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，最终 build id `20260817051719`，package CRC `0x086043EE`。
- 还需完成：
  - 正式 RMTP/System Pack 表镜像和 DeploymentGate staged-candidate 校验已由 TDMA-TASK-20260817-003 完成。
  - 实现 core1 逐类队列、time-aware gate、policing/backpressure 和质量计数。
- 关联文件：
  - `components/tdma/inc/tdma_profile.h`
  - `components/tdma/src/tdma_profile.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
- 下一步：
  - RMTP 表项和 activation hook 已完成；继续 scheduler queue runtime。

### TDMA-TASK-20260817-001 - 上/下行 TDMA 边界升格为基础件

- 状态：完成首版边界升级、文档主域、单元测试和构建验证；后续进入 TDMA system node / 同时 UP-DOWN runtime。
- 日期：2026-08-17
- 任务目标：
  - 根据架构纠偏，将上行/下行 TDMA 从 VDC 描述中拆出，作为独立基础件主域。
  - 保持 HAOFV 边界：TDMA owner 管 runtime、payload、adapter 和 evidence；VDC 只消费 timestamp，RefMem 只消费 payload completion。
- 完成内容：
  - 新建 `docs/tdma/` 标准三件套和 README。
  - `tdma_service` 已增加 ring runtime config/snapshot 字段，用于表达 active node、local/reference slot、UP/DOWN group、ring seq、running state、profile CRC 和 schedule CRC。
  - `tdma_service_configure_ring_runtime()` 拒绝 bad config：节点数不足、slot 越界、UP/DOWN group 相同、缺少 simultaneous flag、CRC 为 0。
  - 单元测试增加公共 TDMA ring runtime contract，验证 `up_running/down_running` 可由 runtime 发布，但 `simultaneous_feedback_loop_evidence` 仍保持 0，防止把配置就绪误判为闭环。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 ARM GCC 编译门禁；当前环境未找到 host gcc，因此 host 执行跳过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，输出 `vdc_domain tests passed`。
  - `python tools\docs_check\docs_check.py` 通过，`files=90 warnings=2`；仅保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 和 `VDC_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python -m py_compile tools\docs_check\docs_check.py` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260817043820`，package CRC `0xB91E63B5`。
- 还需完成：
  - 更新 `docs/README.md`、HAOFV 架构和 VDC 文档。
  - 后续把 TDMA 作为 HAOFV system node 接入 NodeLoad / DeploymentGate。
  - 后续建立真实 core1 同时 UP/DOWN runtime 和硬件 timestamp correlation。
- 关联文件：
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_refmem_realtime_tdma.c`
  - `docs/tdma/README.md`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
- 下一步：
  - 按 HAOFV 索引和 VDC 文档收敛 TDMA ownership，再运行 host/doc 验证。
