# TDMA 基础件主域任务进度

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_TASK_PROGRESS.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-08-20

本文档记录 TDMA foundation 的阶段性任务进度、验证结果和后续动作。待办事项放在 `TDMA_DOMAIN_TODO.md`。

### TDMA-TASK-20260820-001 - 八槽 RX 位图快路径

- 状态：完成首版代码、主机/ARM 验证和两板 HIL；待 3..8 板扩展验证及契约交叉审核。
- 日期：2026-08-20
- 任务目标：按最大 8 板统一 process image，把 core0 RTOS 从无变化 slot 的快速过滤路径移出，同时保持单 writer、多方读取和 FIFO 非阻塞语义。
- 完成内容：
  - SHORT process image 固定为 8 × 32 B；每 slot 前 8 B 为 `magic/version/source_slot/target_mask/seq16` 快速头，后 24 B 对 core1 opaque。
  - core1 只扫描 remote slot 快速头并生成 `input_segment_mask`；core0 RefMem 只解析命中 slot，空 mask 不再回退全帧扫描。
  - TX/RX descriptor 同时校验 `slot_index/generation/sequence`；损坏项被丢弃并推进 ring，不阻塞 wire path。
  - RX seq16 去重改为 classify/commit 两阶段：RX FIFO 发布成功后才提交，FIFO 满时同一 mailbox 可以重试。
  - `SYSTem:TDMA:FLIGHT:PROCess?` 追加 bitmap scan/hit/duplicate 计数。
  - 新增只读 `SYSTem:REFMEM:SYNC:FLIGHT?` 和 `tools/tdma_ring_monitor/flight_bitmap_validate.py`，按 `*IDN?` 唯一地址完成 2..8 板 core1/FIFO/core0 计数关联验收。
- 验证结果：
  - MinGW host unit test scripts 27/27 通过，包含 flight FIFO、payload registry、ring runtime、service scheduler 和 PIO SPI ring adapter。
  - 双核 FreeRTOS A/B、Factory UF2 与 OTA package 构建通过。
  - 两板均通过 `*IDN?` 唯一地址识别，ring 上下行进入 running，`ring_adapter_rx_bad_count=0`。
  - 30 s 位图闭环中，转发节点 `map_apply +14921`、`bitmap_hit +14921`、`refmem_rx_accept +14922`；参考节点按设计不执行 map apply，`bitmap_hit +14916`、`refmem_rx_accept +14914`。
  - 两板 `fifo_rx_drop=0`、`refmem_rx_reject=0`、`refmem_rx_bad=0`、`bitmap_duplicate=0`。
  - 文档 strict names 和 doc regression 均通过。
- 后续验证：扩展到 3..8 板，并在 1 ms/100 us/10 us 周期和更高 SPI 速率下记录负载、FIFO 水位、端到端延迟与丢弃计数。

### TDMA-TASK-20260819-002 - EtherCAT-style fixed-offset flight processing

- 状态：完成 V1 完整帧软件 flight engine；未宣称 PIO/DMA 真正 cut-through。
- 日期：2026-08-19
- 任务目标：在严格 HAOFV 边界下，把 core0 发布的 active TX image 与 core1 的 cyclic frame boundary 连接起来，用冻结的 `TdmaProcessImageMap` 执行本地 input/output block 交换。
- 完成内容：
  - 新增 `tdma_flight_engine`，只依赖 `TdmaProcessImageMap` 和 `tdma_flight_fifo`，不调用 VDC、RefMem 或 Trigger 解码器。
  - map 只能在 ring STOP 状态由 TDMA service 配置；adapter START 时按 local slot 激活，RUN 中不允许改 offset。
  - follower 收到 `CYCLIC_PROCESS_IMAGE + FLIGHT_MUTABLE` 后复制完整 payload，提取本地 input mask，并替换本地 `FLIGHT_WRITE` 固定段；随后使用现有 V1 API 更新 transport CRC 和 hop。
  - active ring 明确限制为 2 至 8 节点；reference `hop_limit=node_count-1`，8 节点部署对应 7 hop。
  - TX image 在同一 frame 的所有本地 output segment 中复用同一个 generation；没有新 generation 时复用上一版，首帧无 active image 时原样旁路并记录 `TX_UNAVAILABLE`。
  - RX FIFO 满时只丢弃 core0 mirror，不影响 wire forward；新增 `SYSTem:TDMA:FLIGHT:PROCess?` 查询 map/byte/counter 证据。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过，覆盖多 segment replacement、nonlocal byte preservation、generation reuse、长度拒绝和 RX mirror 满队列继续 forwarding。
  - `run_tdma_process_image_map_tests.ps1` 通过。
  - `RP2350_TRIG_UPDATE.pkg` 已用 OTA 写入 COM3 并重启/commit；build `20260819152706`，package CRC32 `0x81C762C4`。
  - COM3 `TRAIN 4096` + `START` + 25 MHz 单板回环通过；坏帧、物理 RX 错误、ring overrun 增量均为 0。`TIMESTAMP_MISSING` 仍符合尚未接硬件 edge latch 的架构边界。
- 还需完成：
  - 将正式 System Pack/DeploymentGate process-image map、generation/dirty/target/segment CRC 头接入 TDMA owner。
  - 实现尾部 CRC/WKC V2，并在 PIO/DMA 上证明 RX/TX overlap、固定 hop pipeline delay、无 underflow/overrun 后，才可称为严格 cut-through flight mode。
  - 接入硬件 TX/RX edge timestamp latch。
- 关联文件：
  - `components/tdma/inc/tdma_flight_engine.h`
  - `components/tdma/src/tdma_flight_engine.c`
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
- 下一步：先定义正式 System Pack process-image table 的 wire contract，再把 map staged/active 双缓冲接入 DeploymentGate；不要在运行态开放串口 map 写入口。

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

### TDMA-TASK-20260819-003 - TDMA service 接管 flight FIFO 只读快照

- 状态：完成 `tdma_service` 级 FIFO 挂载、只读 snapshot 入口和编译门修正；resident ring 行为不变。
- 日期：2026-08-19
- 任务目标：
  - 把 `tdma_flight_fifo_t` 作为 TDMA owner 的内部固定池对象纳入 `tdma_service` 生命周期。
  - 只提供 snapshot 读回，不改变现有 ring runtime/adapter fast path。
  - 让编译/测试脚本明确把 `tdma_flight_fifo.c` 作为 `tdma_service.c` 的联动源文件。
- 完成内容：
  - `tdma_service_service_t` 新增 `flight_fifo` 成员，`tdma_service_init()` 内部执行 `tdma_flight_fifo_init()`。
  - 新增 `tdma_service_get_flight_fifo_snapshot()`，由 TDMA owner 统一暴露 FIFO 状态。
  - `run_tdma_service_scheduler_tests.ps1`、`run_refmem_realtime_tdma_tests.ps1`、`run_vdc_domain_tests.ps1` 补齐 `tdma_flight_fifo.c` 联动编译。
  - `test_tdma_service_scheduler.c` 增加 FIFO snapshot 断言，确认 TDMA owner 已持有该基础件。
- 验证结果：
  - `run_tdma_service_scheduler_tests.ps1` host 通过。
  - `run_refmem_realtime_tdma_tests.ps1` host 通过。
  - `run_vdc_domain_tests.ps1` host 通过。
  - `run_host_unit_tests.ps1` 全量通过 `27/27`。
- 还需完成：
  - 在 core1 cyclic frame boundary 接入 TX acquire/reuse，在 frame end 接入 RX publish。
  - 之后再把 FIFO snapshot 挂到 SCPI/diagnostic path 的适当位置，继续保持只读和 owner 单写规则。
- 关联文件：
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tools/tests/run_refmem_realtime_tdma_tests.ps1`
  - `tools/tests/run_vdc_domain_tests.ps1`
  - `tools/tests/run_tdma_service_scheduler_tests.ps1`
  - `tests/unit/test_tdma_service_scheduler.c`
- 下一步：
  - 保持 resident ring 不变，把 FIFO 接入 core1 flight engine 的 frame 生命周期。

### TDMA-TASK-20260819-002 - COM3 单板 TDMA 闭环烧录与训练验证

- 状态：完成 OTA 烧录、boot/commit、单板 ring 准备流程自动化和 25 MHz 单板回环验证。
- 日期：2026-08-19
- 任务目标：
  - 将当前 HAOFV 双 FIFO 基础件固件烧录到产品样板，并跑单板 RJ45 输出回接输入验证。
  - 把单板闭环工具从只读检查升级为包含 `STOP -> LOCAL -> ARM -> TRAIN -> START` 的完整准备流程。
  - 保持 HAOFV evidence 边界：电气/数据回环 PASS 不冒充正式 hardware timestamp closed-loop evidence。
- 完成内容：
  - `ota_multi_update.py` 对 COM3 产品样板升级 `RP2350_TRIG_UPDATE.pkg`，板端 build 从 `20260819123859` 切到 `20260819130134` 并完成 commit。
  - `tdma_single_board_loopback.py` 默认自动执行 ring setup：`STOP`、`LOCAL 0`、`ARM`、`TRAIN 4096`、`START`，并在 summary 中记录每步响应和 armed/started 状态。
  - 新增工具参数 `--skip-ring-setup`、`--local-slot`、`--train-cycles`、`--arm-wait`、`--start-wait`。
  - COM7 调试串口已接入并识别；本轮控制和验证仍只使用 COM3 USB CDC，避免占用调试 UART。
- 验证结果：
  - OTA package：`build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260819130134`，package CRC `0x86934747`。
  - 单板自动流程输出目录：`build-rtos-multicore-smoke/tdma_single_board_loopback_20260819_auto`。
  - 自动流程确认：`ARM` 后 `ring_enabled=1`、`adapter_started=1`、up/down 尚未跑；`START` 后 `up_running=1`、`down_running=1`。
  - 15 s / 25 MHz 回环 PASS：`ring_seq +14963`，`up_tx_sequence +7482`，`down_rx_sequence +7482`，`adapter_tx_count +7482`，`adapter_rx_count +7482`。
  - `adapter_rx_bad_count`、`phys_rx_bad_count`、`phys_rx_magic_fail_count`、`phys_rx_ring_overrun_count` 增量均为 0。
  - `ring_last_error=5(TIMESTAMP_MISSING)`、`simultaneous_feedback_loop_evidence=0`，符合当前未接 PIO/DMA 边沿硬件 timestamp latch 的边界。
- 还需完成：
  - 将 `TDMA_TX_IMAGE_FIFO`/`TDMA_RX_FRAME_FIFO` 接入 core1 flight engine 的 frame boundary/frame end。
  - 补真实 PIO/DMA edge timestamp latch，满足 `timestamp_resolution_ns <= 100` 且 `HARDWARE_LATCHED` 后再要求 `simultaneous_feedback_loop_evidence=1`。
  - 后续在双板/多板上验证 `TRAIN`、START 顺序、长线缆和干扰条件。
- 关联文件：
  - `tools/tdma_ring_monitor/tdma_single_board_loopback.py`
  - `build-rtos-multicore-smoke/tdma_single_board_loopback_20260819_auto/summary.json`
  - `build-rtos-multicore-smoke/ota_closed_loop_20260819/summary.json`
- 下一步：
  - 在不破坏现有 resident ring 的前提下，将双 FIFO 挂入 TDMA owner/flight engine，并补相应 SCPI snapshot。

### TDMA-TASK-20260819-001 - HAOFV 双 FIFO 基础件首版

- 状态：完成 core0/core1 双 FIFO 基础件、单元测试、ARM 编译门和固件构建；尚未接入 PIO/DMA 飞行引擎 fast path。
- 日期：2026-08-19
- 任务目标：
  - 按 HAOFV/TDMA Foundation 架构先实现 core0/core1 之间的两个 SPSC FIFO。
  - `TDMA_TX_IMAGE_FIFO` 支持 core0 提前发布完整 TX image，core1 在 frame boundary 锁定完整 generation，无新 generation 时复用上一版。
  - `TDMA_RX_FRAME_FIFO` 支持 core1 非阻塞发布 RX frame/input slice 镜像，满队列或缓冲池耗尽时只丢弃 core0 解析副本并增加计数，不影响 wire forwarding。
- 完成内容：
  - 新增 `tdma_flight_fifo` 模块，使用固定 TX 双缓冲、RX 固定缓冲池和 descriptor ring，不使用动态内存、mutex、RTOS 阻塞队列或 core0 ACK。
  - 冻结 TX ownership：`CORE0_INACTIVE -> CORE0_FILL -> CORE0_READY -> CORE1_ACTIVE -> CORE0_INACTIVE`。
  - 冻结 RX ownership：`FREE -> CORE1_FILL -> CORE0_PARSE -> FREE`。
  - snapshot 采用架构术语 `tx_image_stale_count` 和 `rx_mirror_drop_count`，并保留旧字段别名用于过渡。
- 验证结果：
  - Vivado/AMD MinGW GCC `D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe` host 单元测试通过：`tdma_flight_fifo tests passed`。
  - `run_tdma_process_image_map_tests.ps1` ARM 编译门通过。
  - `run_tdma_ring_runtime_tests.ps1` ARM 编译门通过。
  - `pico2-rtos-multicore-smoke` 固件构建通过并生成 `RP2350_TRIG_UPDATE.pkg`。
- 还需完成：
  - 将 `tdma_flight_fifo_t` 挂到 TDMA owner/flight engine 生命周期中，只允许 TDMA Foundation 暴露 snapshot 和受控 publish/acquire API。
  - 在 core1 cyclic frame boundary 接入 TX acquire/reuse，在 frame end 接入 RX publish，保持 core1 不调用 VDC/RefMem/Trigger 业务解码器。
  - 后续再补 PIO/DMA 固定 offset 飞行替换、尾部 CRC/WKC 和硬件 timestamp latch。
- 关联文件：
  - `components/tdma/inc/tdma_flight_fifo.h`
  - `components/tdma/src/tdma_flight_fifo.c`
  - `tests/unit/test_tdma_flight_fifo.c`
  - `tools/tests/run_tdma_flight_fifo_tests.ps1`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
- 下一步：
  - 按 HAOFV owner 边界把 FIFO 接入 TDMA runtime，而不是让 VDC、RefMem 或 Trigger 域直接触碰跨核缓冲。

### TDMA-TASK-20260818-008 - 产品样板 IO 首轮迁移

- 状态：软件映射、资源隔离、LCD 开机横屏板测和 COM3 OTA 完成；主页 UI 与其余产品 IO 待继续验证。
- 日期：2026-08-18
- 任务目标：
  - 按 `RP2350B_QFN80_IO_CONSTRAINTS` 将现有 TDMA PIO-SPI 与板级 IO 迁移到产品样板。
  - 暂时将 BiSS + RJ45 Trigger 作为三线 SPI 使用，其中 RJ45 Trigger 作为 CS/frame-sync，复用既有 PIO 实现。
  - 接入 3 个 KEY、3 个 LED、4 路 SMA 输入和 4 路 SMA 输出，并避免旧调试 persona 抢占产品 IO。
- 完成内容：
  - TDMA 改用 PIO2 SM0/SM1：TX `CS=GPIO26/SCK=GPIO25/TX=GPIO29`，RX `CS=GPIO27/SCK=GPIO28/RX=GPIO24`；保持现有 CS/frame-sync PIO 协议，初始默认 10 MHz。
  - ISO1452 控制迁移到 `DE=GPIO30/31/32`、`/RE=GPIO40/41/42`；上电关闭 driver、使能 receiver，TDMA arm 完成 PIO 配置后才开启 driver。
  - KEY1/2/3 映射到 GPIO2/6/7，低有效并上拉；LED SYSTEM/ARM/FAULT 映射到 GPIO3/8/9。
  - 板载 CH343 调试 stdio 迁移到 UART0 GPIO0/1；UART1 GPIO4/5 与 DE GPIO13 保留给外部 RS485，默认接收。
  - SMA OUT1..4 映射 GPIO16..19；SMA IN4..1 映射 GPIO20..23。采集边界对每个 4-bit sample 做 bit reverse，使公共 mask bit0..3 始终表示逻辑 IN1..4。
  - SEQ gated PIO 移除 GPIO16..19 和 gate offset 3 的硬编码，按产品输入组与 `GATE_IN=GPIO20` patch 实际 wait-pin offset。
  - 禁用占用 PIO2/GPIO24..29 的 legacy AUX/BiSS tap/sync clock/RJ45 marker persona；相关兼容 API 保留但返回不可用。
  - 禁用占用 GPIO4..7、会与 UART1/KEY2/KEY3 冲突的 legacy debug-model overlay。
  - TF 迁移到 SPI1 GPIO10..12/15（card detect GPIO14）；LCD 迁移到独立 SPI0 GPIO34..39，并按样板新规格改为 ST7735S、原生 RAM `80x160`、offset `(24,1)` 与硬复位。
  - 产品样板 COM3 已确认 TF 卡 `CARD_READY`、SDHC/SDXC、FAT 挂载、目录/INFO/64 B 读回和递增 boot snapshot 写入；StorageAO 的资源 claim 从旧 `SPI0|SD` 收敛为产品板专用 `SD`（SPI1），不再与 LCD SPI0 互斥。
  - ST7735S 的硬件 `MV` 横屏在 offset `(1,26)` 和 `(1,24)` 下均出现逐行回绕斜切，因此控制器保持已验证稳定的原生竖屏扫描；刷新层将逻辑 `160x80` UI 顺时针软件旋转写入 `80x160` RAM。开机动画已适配横屏并经产品样板确认完整、无斜切；旧主页仍沿用大屏坐标，显示不全已转入正式待办。
  - 新增三键纯事件层：35 ms 去抖、press/release、short、700 ms long 和 250 ms repeat；UI 产品路径切换为 160x80 单卡片四行布局，KEY1 上一页/长按返回、KEY2 详情、KEY3 下一页/长按连翻。
  - 新增 `tdma_single_board_loopback.py`，只读检查单板 RJ45 输出回接输入后的预期频率/pin profile、UP/DOWN、sequence、TX/RX、坏帧和 overrun 增量；电气/数据 PASS 与硬件 timestamp feedback evidence 分层报告。
  - 产品 W25Q128JV 容量固定为 16 MiB；为兼容现有 bootloader/OTA 元数据，A/B 分区暂保持在低 4 MiB，剩余 12 MiB 待版本化分区迁移后启用。
- 验证结果：
  - `build-rtos-multicore-smoke` A/B 固件链接和 update package 均成功；软件横屏最终 build id `20260818141125`，package CRC `0x294FCB21`。
  - `ota_multi_update.py` 对产品样板 COM3 OTA PASS，板端运行 build `20260818141125`；用户确认开机界面显示正常，主页显示不全作为后续 UI 重构输入。
  - `run_tdma_profile_tests.ps1` ARM/host 测试通过。
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` ARM/host 测试通过。
  - 三键 UI build `20260818151639`、package CRC `0x643A2D6A` 已通过 COM3 OTA boot/commit。
  - 未接回环网线的 3 s 基线：profile 为 TX CS/SCK/DATA `26/25/29`、RX `27/28/24`、10 MHz；TX sequence 增长 1116，RX 保持 0，工具按预期报告 `down_running=0`。
  - 产品样板 build `20260818154324` 单板网线回环连续两个 15 s 窗口通过电气/数据层验证：
    第二轮 TX/RX 均增长 7230 帧，UP/DOWN 均运行，adapter/phys bad、magic fail 和
    overrun 全部零增长。`ring_last_error=5(TIMESTAMP_MISSING)` 与
    `simultaneous_feedback_loop_evidence=0` 符合当前 diagnostic-only timestamp 边界，
    不作为 CS/CLK/DATA 回环失败，也不冒充正式时间戳闭环证据。
  - 产品差分回环完成 `15/20/25 MHz` 阶梯：15 MHz build `20260818154958`
    为 7344/7344 帧，20 MHz build `20260818155222` 为 7272/7272 帧，25 MHz
    build `20260818155435` 的 15 s 为 7359/7359 帧；各档 adapter/phys bad、
    magic fail、overrun 均零增长。25 MHz 追加 60 s 窗口为 29721/29721 帧且
    全部错误仍为 0。当前样板保留 25 MHz 测试固件，干净构建默认仍为 10 MHz，
    待长线缆、干扰和多板 HIL 后选择 20 或 25 MHz 量产档。
- 还需完成：
  - 产品样板已确认三个 LED 均为低有效；ST7735S 背光 GPIO35 已确认低有效。
  - 逐项实测 KEY、SMA OUT1..4、SMA IN1..4 的通道编号和输入反序，再进行 TDMA 单跳与闭环 HIL。
  - 实测 ISO1452 DE 与 `/RE` 时序，确认空闲、arm、disarm 和复位期间不存在总线争用。
  - 产品样板人工确认 160x80 页面无裁切，并逐项确认三个按键短按/长按/重复的方向和手感。
  - 单板 CS/CLK/DATA 电气与帧回环已通过；后续补 PIO/DMA 边沿硬件 timestamp latch，
    再验收 `simultaneous_feedback_loop_evidence=1` 和正式 round-trip correlation。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `boards/rp2350_trig/src/board.c`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `components/sync_io/src/sync_io.c`
  - `components/sync_io/src/seq_step.pio`
  - `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`
- 下一步：
  - 继续产品样板 KEY/SMA GPIO 冒烟验证，再接 BiSS/RJ45 线做 TDMA CS/CLK/TX 逻辑分析仪验证；主页 UI 按三键、小屏单卡片方案另行重构。

### TDMA-TASK-20260818-007 - PIO-SPI 外层包头假锁修正与 60 s HIL 回归

- 状态：完成根因定位、代码修正、host/unit 全量验证、A/B 构建、两板 OTA 和 60 s HIL 回归。
- 日期：2026-08-18
- 任务目标：
  - 解决 10 MHz / 500 Hz 长时间运行时偶发 adapter `RX_BAD_FRAME` 和 feedback RX 低于最佳基线的问题。
  - 保持当前 CS+DATA+CLK 三线单向腿和 500 Hz reference baseline 不变，只修正物理层切帧假锁。
  - 继续保持 timestamp evidence 边界：当前 timestamp 仍为 diagnostic-only，不置位 closed-loop evidence。
- 完成内容：
  - 定位到外层 PIO-SPI packet magic `54 44` 与内层 `TdmaTransportFrame` magic `54 44` 相同；当连续 DMA 扫描指针错过真实外层头时，可能错误锁定到内层 transport magic，并把后续约 257 B 数据当作一帧交给 adapter，导致 transport CRC 失败并成批吞掉后续短帧。
  - `tdma_pio_spi_phys_capture_words()` 增加二级 header 校验：外层 `frame_size` 必须与内层 `TdmaTransportFrame.packet_size` 一致，且内层 magic/version/class/header_size 必须可信，才接受该 candidate。
  - 增加 `rx_magic_fail_count` 的扫描失败诊断含义：它现在记录“DMA 有新字节但未找到可信外层包头”的次数，不能单独等价为 bit-level 坏帧；adapter `rx_bad` 和 phys `rx_bad/stall/overrun` 仍是主要故障指标。
  - 更新 host 单元测试中 `tdma_pio_spi_ring_adapter` 的 500 Hz 语义：reference 节点每两个 core1 service 发一次 beacon，forward 节点使用注入 RX + TX 捕获物理桩，避免测试 stub 自回灌导致重复转发。
- 验证结果：
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 全量通过，26/26 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke --target RP2350_TRIG_UPDATE -- -j 8` 通过，build id `20260818111944`，package CRC `0x72F2FD91`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818111944`。
  - `ring_rate_measure.py --window-s 15`：COM5 TX `499.9/s`、feedback RX `497.7/s`；COM6 RX/TX `497.8/s`；adapter `rx_bad=0`。
  - `ring_rate_measure.py --window-s 60`：COM5 TX `500.1/s`、feedback RX `498.0/s`；COM6 RX/TX `498.4/s`；adapter `rx_bad=0`，phys `rx_bad=0`、`stall=0`、`tx_timeout=0`、`ring_overrun=0`。
- 还需完成：
  - `rx_magic_fail_count` 需要后续拆成更清晰的 `candidate_reject` / `idle_scan_miss` / `real_magic_miss`，避免维护人员误读。
  - 继续 P0.5-4/5：在 PIO/DMA 边界补真实 TX/RX edge latch，只有 non-diagnostic 硬件 timestamp 才允许进入 `simultaneous_feedback_loop_evidence`。
- 关联文件：
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`
  - `tools/tests/run_host_unit_tests.ps1`
  - `tools/tdma_ring_monitor/ring_rate_measure.py`
- 下一步：
  - 以 build `20260818111944` 的 `10 MHz / 500 Hz / adapter rx_bad=0` 作为当前 TDMA resident ring 基线，进入 PIO/DMA edge latch 方案落地。

### TDMA-TASK-20260818-006 - 共享硬件 tick 诊断时间戳接入

- 状态：完成共享时钟基础件、SyncIO 复用、TDMA PIO-SPI 诊断 timestamp 接入、A/B 构建、两板 OTA 和 HIL 验证。
- 日期：2026-08-18
- 任务目标：
  - 把 `timer1/CLK_SYS` 从 `sync_io` 私有实现抽成共享 timestamp clock provider，避免 TDMA 和 SyncIO 重复初始化同一个硬件计数器。
  - 让 TDMA ring snapshot 能看到非零硬件 tick timestamp 和真实分辨率，为后续 PIO/DMA 边沿 latch 铺路。
  - 保持安全边界：阶段一 timestamp 仍为 CPU 读取诊断值，必须保留 `DIAGNOSTIC_ONLY`，不得置 `HARDWARE_LATCHED`。
- 完成内容：
  - 新增 `vdc_timestamp_clock.h/.c`，提供 idempotent `timer1/CLK_SYS` 初始化、tick 读取、tick-to-ns 转换和 resolution 查询。
  - `sync_io` 的 capture latch timebase 改为复用共享 `VdcTimestampClock`，不再私有重置 `timer1_hw`。
  - `tdma_pio_spi_phys_tx/rx` 改为从共享硬件 tick clock 读取 TX/RX 诊断 timestamp。
  - `tdma_runtime_owner_init()` 为 PIO-SPI ring adapter 设置 timestamp metadata：resolution 来自 `VdcTimestampClock`，flags 保持 `DIAGNOSTIC_ONLY`。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke --target RP2350_TRIG_UPDATE -j 8` 通过，build id `20260818104829`，package CRC `0x7CBEE2DC`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818104829`。
  - `ring_rate_measure.py --window-s 15`：COM5 reference TX `499.7 frame/s`、feedback RX `497.5 frame/s`；COM6 forward RX/TX `498.2 frame/s`；物理层 `phys_bad/magic_fail/shift/stall/ring_overrun` 均为 0 增长。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?`：COM5 `ring_timestamp_resolution_ns=4`、`ring_timestamp_flags=1`、reference TX / feedback RX timestamp 均非零，`simultaneous_feedback_loop_evidence=0`、`ring_last_error=TIMESTAMP_MISSING`。
- 还需完成：
  - 阶段二实现 PIO/DMA 边沿 latch：TX timestamp 应绑定 CS/frame-sync 起始边沿或首 bit 边沿；RX timestamp 应绑定接收 CS/frame-sync/首 bit 边沿，而不是 CPU 抽取完整包的时间。
  - 只有边沿 latch 验证通过后，才允许 TDMA ring adapter 设置 `HARDWARE_LATCHED` 且清除 `DIAGNOSTIC_ONLY`。
- 关联文件：
  - `components/vdc_domain/inc/vdc_timestamp_clock.h`
  - `components/vdc_domain/src/vdc_timestamp_clock.c`
  - `components/sync_io/src/sync_io.c`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `components/tdma/src/tdma_runtime_owner.c`
- 下一步：
  - 设计 PIO-SPI frame-sync 边沿 latch：优先评估 IRQ/core1 快速采样、PIO side-set timestamp token、DMA completion timestamp 三种路径，明确哪一种能满足 `<=100 ns` evidence。

### TDMA-TASK-20260818-005 - 10 MHz / 500 Hz TDMA 环路基线回归

- 状态：完成代码修正、A/B 构建、两板 OTA 和 15 s HIL 验证；作为 VDC/DPLL 下一阶段基线。
- 日期：2026-08-18
- 任务目标：
  - 回归到 10 MHz PIO-SPI 下误差最小的 500 Hz 两板环路状态。
  - 解决 1 kHz 试验后 reference 发包节拍被 wall-clock 相位和 core1 service 抖动影响的问题。
  - 保持 HAOFV evidence 边界：允许 `up/down_running` 证明 resident ring 运行，不允许用软件时间戳伪造 `simultaneous_feedback_loop_evidence`。
- 完成内容：
  - `tdma_pio_spi_ring_adapter` 将 reference beacon 从 wall-clock cycle 奇偶门控改为 core1 service 二分频：当前 core1 TDMA service 约 1 kHz，因此 reference 稳定约 500 Hz 发帧。
  - follower 保持“收到一帧立即转发一帧”，避免批量 RX 时只转发最后一帧。
  - `distributed_refmem_service()` 在 OTA 会话中跳过 node-load auto service 和 TDMA 维护日志；`distributed_refmem_log_tdma_ring_service()` 增加 OTA active 静默门，避免 CDC OTA 响应污染。
  - 保留 RX 连续 DMA ring、magic+length 扫描、DMA channel 4 和 `SYSTem:SYNC:VDC:TDMA:PHYS?` 诊断字段。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke --target RP2350_TRIG_UPDATE -j 8` 通过，build id `20260818101157`，package CRC `0x354CA0F3`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818101157`。
  - COM6 通过 `SYSTem:TDMA:RING:LOCAL 1` 切为 forward slot1。
  - `ring_rate_measure.py --window-s 15` 干净窗口结果：COM5 reference TX `499.7 frame/s`、feedback RX `498.1 frame/s`；COM6 forward RX/TX `499.2 frame/s`。
  - 物理层 `phys_bad=0`、`magic_fail=0`、`shift=0`、`stall=0`、`ring_overrun=0`，core1 loop 约 `999 frame/s`。
  - 当前 `ring_last_error=5`，即 `TIMESTAMP_MISSING`；这是预期状态，表示尚未接入 PIO/DMA 硬件 timestamp latch。
- 还需完成：
  - P0.5-4/5：在 PIO/DMA 边界补 reference TX / feedback RX 硬件 latch，形成 sequence、identity CRC、schedule CRC、TX timestamp、RX timestamp 同一圈 ring 的闭环证据。
  - P0.5-6：长时间只读 HIL 需要输出 summary + SVG，并区分 TDMA ring runtime 与 VDC lock quality。
  - 1 kHz 升频暂不作为当前基线；待硬件 timestamp/DPLL 闭环后再评估 pipeline 最坏情况延迟。
- 关联文件：
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `tools/tdma_ring_monitor/ring_rate_measure.py`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
- 下一步：
  - 进入硬件 timestamp latch 方案拆解：先只增加真实硬件时间源与诊断字段，验证无误后再允许 `HARDWARE_LATCHED` 进入 `simultaneous_feedback_loop_evidence`。

### TDMA-TASK-20260818-003 - PIO SPI 下行闲置数据线改作 FRAME_SYNC/CS

- 状态：完成代码与文档更新，A/B 构建通过，两板 OTA 与方向性丢帧 HIL 通过；正式 closed-loop evidence 仍待硬件 timestamp latch。
- 日期：2026-08-18
- 任务目标：
  - 修正 1 MHz 下 `COM5->COM6` 方向性丢帧的物理层对齐风险。
  - 将发送端未用 RX/MISO 与接收端未用 TX/MISO 的互连线改作点对点 `FRAME_SYNC/CS`，让 RX PIO 在帧有效期间采样，而不是完全依赖无 CS 连续流 + magic 扫描恢复对齐。
- 完成内容：
  - `board_config.h` 冻结当前最小系统 TDMA 三线单向腿：发送端闲置 RX/CS `GPIO21`、TX/DATA `GPIO23`、CLK `GPIO24`，连接到对端闲置 TX/CS `GPIO16`、RX/DATA `GPIO18`、CLK `GPIO19`。
  - `tdma_pio_spi_phys` 增加 `tx_csn_pin/rx_csn_pin`，TX 发帧前拉低 CS，尾部字节移出后拉高 CS；RX PIO 使用已有 `csn_pin` patch，等待 CS 有效后按 SCK 采样。
  - RX DMA rearm 前等待 `RX_CSN` 回到空闲高电平，避免在一帧 CS 低尾部清 FIFO / 重新开 DMA。
  - 保留 magic/header 扫描作为保险和诊断，不再把它作为唯一帧同步来源。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260818072932`，package CRC `0x15177089`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818072932`。
  - `SYSTem:SYNC:VDC:TDMA:PHYS?` 确认 TDMA PHYS 末尾为 `tx_csn=21, rx_csn=16`。
  - `ring_rate_measure.py --window-s 15`：COM5 reference TX `494.9/s`、RX `473.7/s`、`rx_bad=0`；COM6 forward RX/TX `473.6/s`、`rx_bad=0`。与前一版 `COM5->COM6` 约 15% 丢帧、回传方向跟随丢帧相比，方向性丢帧已明显收敛。
- 还需完成：
  - 补 PIO/DMA 硬件 timestamp latch 后重新跑 5 min HIL，让 `simultaneous_feedback_loop_evidence` 从 `TIMESTAMP_MISSING` 进入正式闭环。
  - 继续评估剩余 `~4%` TX/RX 速率差是否来自 500 Hz 发射节流、core1 service 相位、DMA rearm 窗口或后续需要的 ping-pong/ring DMA。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `components/tdma/inc/tdma_pio_spi_phys.h`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
- 下一步：
  - 在当前 `rx_bad=0` 基础上推进硬件 timestamp latch；若长时间 HIL 仍出现丢帧，再从“真实连续 DMA ring / DMA IRQ ping-pong / PIO frame-length RX 程序”方向继续收敛。

### TDMA-TASK-20260818-004 - PIO SPI CS/frame-sync 速率阶梯验证

- 状态：完成 2/5/10/25 MHz 构建、两板 OTA 和方向性 HIL；当前稳定档回落为 10 MHz。
- 日期：2026-08-18
- 任务目标：
  - 在 `GPIO21->16` CS/frame-sync、`GPIO23->18` DATA、`GPIO24->19` CLK 的最小系统接线下，逐步提高 TDMA PIO SPI bring-up adapter 速率。
  - 找到当前跳线环境中可作为后续 VDC/DPLL HIL 的稳定物理层速率。
- 完成内容：
  - 依次修改 `BOARD_TDMA_SPI_BAUD_HZ` 为 2 MHz、5 MHz、10 MHz、25 MHz，每一档均重新构建、OTA COM5/COM6，并设置 COM6 为 slot1 后执行同一方向统计。
  - 25 MHz 出现坏帧后，将代码回到 10 MHz 稳定档，避免后续闭环验证建立在不稳定 transport 上。
- 验证结果：
  - 2 MHz，build `20260818074001`：COM5 RX `486.6/s`、COM6 RX/TX `486.8/s`，`rx_bad=0`。
  - 5 MHz，build `20260818074327`：COM5 RX `491.5/s`、COM6 RX `491.6/s`、COM6 TX `491.0/s`，`rx_bad=0`。
  - 10 MHz，build `20260818074618`：COM5 RX `490.7/s`、COM6 RX/TX `490.9/s`，`rx_bad` 不增长；COM5 一次 core query 被日志干扰为 `-1`，不影响 TDMA 字段判断。
  - 25 MHz，build `20260818075043`：COM5 RX `452.8/s`，COM6 RX `461.8/s`、TX `452.7/s`；COM6 `rx_bad` 从 `3` 到 `25`，PHYS `rx_bad_count=4`，不作为稳定档。
- 还需完成：
  - 后续若要冲 25 MHz，需要先评估跳线信号完整性、PIO RX 采样相位、GPIO drive/slew、CS setup/hold、以及 DMA rearm 方案。
  - 10 MHz 稳定档下继续推进 P0.5-4/5 硬件 timestamp latch 和正式 `simultaneous_feedback_loop_evidence`。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
- 下一步：
  - 重新构建并 OTA 10 MHz 稳定固件到 COM5/COM6，随后进入 timestamp latch / DPLL 闭环验证。

### TDMA-TASK-20260818-002 - 两板 resident ring UP/DOWN HIL 与 freshness 语义收敛

- 状态：完成代码修正、host 全量门禁、A/B 构建、两板 OTA 和短窗口 HIL；正式 closed-loop evidence 仍待硬件 timestamp latch。
- 日期：2026-08-18
- 任务目标：
  - 回答当前 TDMA 是否已经是环路测试：每块板是否同时运行上行 RX 和下行 TX。
  - 修正 `down_running` 只表示“本轮 service 恰好收到包”的瞬时语义，避免 500 Hz 发帧 / 1 kHz service 的间隔轮把实际运行中的 DOWN leg 清零。
  - 保持 HAOFV evidence 边界：运行状态可以有 freshness，`simultaneous_feedback_loop_evidence` 仍只认新序列、CRC 和硬件 timestamp correlation。
- 完成内容：
  - `tdma_pio_spi_ring_adapter` 增加 `last_rx_service_ns`，`down_running` 改为 RX freshness window 内保持运行；收到坏帧仍撤销 DOWN running。
  - `tdma_ring_runtime` 的 closed-loop evidence 增加“DOWN RX sequence 必须相对上一轮变化”的门禁，防止 throttled round 重用上一帧 feedback。
  - 清理残留时间单位命名：`1e3ns` 代码/工具字段恢复为 `us`，SCPI 指令表中的 `last_sample_age_1e3ns` 改为 `last_sample_age_us`。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过。
  - `run_tdma_ring_runtime_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，26/26。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 2 个 risk review 文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260818063242`，package CRC `0x4A379A27`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818063242`。
  - `SYSTem:TDMA:RING:LOCAL 1` 将 COM6 切为 forward slot1 后，`ring_rate_measure.py --window-s 10` 显示：COM5 reference TX `493.0/s`、RX `416.3/s`、rx_bad `+1`；COM6 forward RX/TX `492.5/s`、rx_bad `+0`。
  - `tdma_ring_monitor.py --duration-s 10` 显示 COM5/COM6 均 `up_running=1/down_running=1`；COM5 仍 `simultaneous_feedback_loop_evidence=0`、reason=`TIMESTAMP_MISSING`，因为当前 `tdma_pio_spi_phys_tx()` 还未产生硬件 TX timestamp。
- 还需完成：
  - P0.5-4/5：在 PIO/DMA 边界补 reference TX / feedback RX 硬件 latch，发布 `HARDWARE_TICK / <=100 ns / HARDWARE_LATCHED` timestamp，形成正式 `simultaneous_feedback_loop_evidence=1`。
  - 将本地 slot 选择从手工 `SYSTem:TDMA:RING:LOCAL` 迁入 System Pack / SlotClaim / board UUID 派生流程，避免两板重启后都回到 slot0。
- 关联文件：
  - `components/tdma/inc/tdma_pio_spi_ring_adapter.h`
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `components/tdma/src/tdma_ring_runtime.c`
  - `components/tdma/inc/tdma_pio_spi_phys.h`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`
  - `tests/unit/test_tdma_ring_runtime.c`
  - `tools/tdma_ring_monitor/tdma_field_parse.py`
- 下一步：
  - 进入 P0.5-5：先实现 PIO SPI bring-up adapter 的硬件 timestamp latch / metadata，再跑 5 min HIL 和 DPLL lock quality SVG。

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
  - 新增 `tdma_pio_spi_phys.h/.c`：首版为无 CS 3-wire PIO SPI 常驻物理层（downlink master TX + uplink slave RX 双 SM 同时 arm，复用已验证的 `tx_byte/rx_byte` PIO 程序和 4B magic+length 定界）；后续 TDMA-TASK-20260818-003 已将 bring-up 物理层修正为 `FRAME_SYNC/CS + DATA + CLK` 三线单向腿。
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
