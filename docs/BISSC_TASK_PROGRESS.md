# BiSS-C 任务进度追踪与回溯

Status: Active
Domain: BISSC
Canonical: `docs/BISSC_TASK_PROGRESS.md`
Related: `docs/BISSC_TAP_BRIDGE_DESIGN.md`, `docs/BISSC_IMPLEMENTATION_TODO.md`
Last updated: 2026-07-07

本文档用于记录 RP2350_TRIG 工程中 `TRIG_PROTOCOL_BISS_C` / BiSS-C TAP bridge
功能的正式实现进度。每完成一个阶段，都应追加任务记录，说明目标、完成内容、
验证结果、剩余工作和下一步计划，便于后续回溯 BiSS-C 设计决策、烧录闭环、
板端证据和仪器验证状态。

架构原则以 `docs/BISSC_TAP_BRIDGE_DESIGN.md` 为准，细分 TODO 以
`docs/BISSC_IMPLEMENTATION_TODO.md` 为准。

## 记录规则

- 每个正式 BiSS-C 任务使用独立编号：`BISSC-TASK-YYYYMMDD-NNN`。
- 每条记录必须写明任务目标、完成内容、验证结果、剩余工作。
- 最新记录追加在“任务记录”章节顶部。
- 如果只完成代码、工具或离线验证，状态应写为 `进行中`，不能写成完整硬件闭环完成。
- 只要修改固件代码，必须执行构建、烧录和板端基础查询验证；如未完成烧录，必须明确记录。
- BiSS-C 板端验证至少记录：固件 build id、`SYST:CORE?`、`TRIG:MODE?`、`STAT:BISS?`、关键统计字段和错误队列。
- 仪器或外部回放验证必须记录输入向量、回放工具/设备、profile 参数、期望 frame/position/trigger 计数和实测结果。
- PIO/DMA/IRQ hot path 不得新增阻塞日志、SD/FatFs 写入、SCPI 输出或非确定性操作；新增观测应优先用计数器、锁存状态、RAM trace 或 DISARM/FAULT 后处理。

## 状态定义

| 状态 | 含义 |
|---|---|
| `完成` | 当前 BiSS-C 子任务目标已经达成，并完成必要构建、烧录、板端或离线验证。 |
| `进行中` | 已完成阶段性工作，但还未完成真实 PIO/仪器/板端闭环。 |
| `阻塞` | 当前无法继续，需要硬件、仪器、资料或用户操作。 |
| `暂停` | 暂时不推进，但不是技术阻塞。 |

## 记录模板

```markdown
### BISSC-TASK-YYYYMMDD-NNN - 任务标题

- 状态：进行中 / 完成 / 阻塞 / 暂停
- 日期：YYYY-MM-DD
- 任务目标：
  - ...
- 完成内容：
  - ...
- 验证结果：
  - ...
- 还需完成：
  - ...
- 关联文件：
  - `path/to/file`
- 下一步：
  - ...
```

## 当前目标

P0 固定 profile TAP bridge 的协议层、SCPI 配置层、TriggerVector/ECC 接线、PIO
接收器骨架、`biss_node_io` runtime 路径、单核 board smoke 和 1 MHz CSV 离线向量
验证已经完成。当前固件已能旁路解码 position/status 并输出触发；串联透传路径
`CLK_IN -> CLK_OUT`、`DATA_IN -> DATA_OUT` 仍需补齐并做示波器闭环。当前验证主线
保持单核，`PROJECT_USE_MULTICORE=OFF`；双核 smoke 保留为后续独立议题。

下一步进入真实 PIO simulator 或逻辑发生器回放：使用
`build\biss_wavegen\biss_1mhz.csv` 或 `build\biss_wavegen\biss_crc_1mhz.csv`
驱动 CLK/DATA，板端配置对应 profile 后查询 `STAT:BISS?`，确认 `rx_frame_count`、
`last_position`、`trigger_count` 和 CRC 统计与离线报告一致。

## 任务记录

### BISSC-TASK-20260707-008 - TAP Bridge 串联透传语义修正

- 状态：进行中
- 日期：2026-07-07
- 任务目标：
  - 修正 BiSS-C P0 的工程语义：产品目标不是单纯并联高阻监听，而是串联在原始
    BiSS-C 链路中的透明 TAP bridge。
  - 明确 AUX0/AUX1 用于接收上游/下游 BiSS-C 信号，AUX2/AUX3 用于原样转发，
    从而允许本板从触发口对整个 BiSS-C 链路进行侧向同步控制。
- 完成内容：
  - 更新 `docs/BISSC_TAP_BRIDGE_DESIGN.md`，将 `TAP_MONITOR` 定义为
    `CLK_IN -> CLK_OUT`、`DATA_IN -> DATA_OUT` 的串联透明桥，同时旁路解析
    position/status。
  - 更新 `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`，明确串联 TAP bridge
    需要接收和透传驱动能力；`AM26LV32E` 接收-only 只适合并联高阻监听夹具。
  - 更新 `docs/BISSC_IMPLEMENTATION_TODO.md`，新增 TAP bridge 透传路径和
    示波器验证任务。
- 验证结果：
  - 本任务为架构和硬件约束文档更新，未修改固件代码，未执行构建、烧录或板端验证。
  - 设计结论：TAP 的“透明”含义是不主动改写、吞掉或产生 BiSS-C 帧；不是完全不驱动。
    串联模式下 AUX2/AUX3 必须驱动转发输出，但只能转发 AUX0/AUX1 的原始链路信号。
- 还需完成：
  - 固件或硬件中实现固定延迟透传路径，并记录 `CLK_IN -> CLK_OUT`、
    `DATA_IN -> DATA_OUT` 的延迟、skew 和 jitter。
  - 用逻辑发生器/编码器源和示波器验证 1 MHz/5 MHz 下原链路不被改写，
    同时 `TRIG_OUT` 可对整个 BiSS-C 链路进行侧向控制。
- 关联文件：
  - `docs/BISSC_TAP_BRIDGE_DESIGN.md`
  - `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`
  - `docs/BISSC_IMPLEMENTATION_TODO.md`
- 下一步：
  - 在 `sync_io` / `biss_node_io` 中评估 PIO 固定延迟转发或硬件直通方案，
    优先保证透传链路的确定性，再把转发延迟写入 latency offset。

### BISSC-TASK-20260707-007 - SYNC_IO 单相固定方向隔离方案

- 状态：进行中
- 日期：2026-07-07
- 任务目标：
  - 将 `SYNC_IO` 高速脉冲输入/输出从 BiSS-C 通信物理层中分离出来，明确其作为本地单相脉冲 IO 的外围电路约束。
  - 按已定义的 `IN0..IN3` 和 `OUT0..OUT3` 固定方向接口收敛隔离器选型，避免引入运行时双向复用。
  - 给后续原理图设计补充输入整形、输出驱动、默认态、隔离电源和 latency offset 验证要求。
- 完成内容：
  - 更新 `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`，新增 `SYNC_IO` 本地高速脉冲隔离章节。
  - 明确完整 4 入 4 出版本推荐使用两颗 `ISO6440F`：一颗用于外部侧到 RP2350 的输入隔离，一颗用于 RP2350 到外部侧的输出隔离。
  - 明确精简版本可使用 `ISO6442F` 实现 2 入 2 出，最小版本可使用 `ISO6421F` 实现 1 入 1 出；`ISO7710` 仅适合单路验证。
  - 将 `F` 默认低输出作为优先选择，降低上电、掉电或隔离侧未供电时的误触发风险。
  - 更新 `docs/BISSC_IMPLEMENTATION_TODO.md`，增加 `SYNC_IO` 隔离原理图和脉冲验证任务。
- 验证结果：
  - 本任务为外围电路设计文档更新，未修改固件代码，未执行构建、烧录或板端验证。
  - 方案结论：在已经冻结 `GPIO16..19` 为输入、`GPIO20..23` 为输出的前提下，`ISO6440F x2` 比真双向隔离或运行时方向切换更简单、更可靠，也更符合当前 HAOFV/PIO 资源所有权模型。
- 还需完成：
  - 拉取或归档 `ISO644x` 正式 datasheet，冻结传播延迟、PWD、通道 skew、CMTI、供电和封装参数。
  - 完成 `SYNC_IO` 输入接口阈值/迟滞/保护电流、输出接口电平/驱动形式、隔离电源和 ESD/TVS 原理图。
  - 使用脉冲发生器和示波器验证 `IN0..IN3` 捕获、`OUT0..OUT3` 输出、闭环 latency、P99 jitter 和上电/掉电安全态。
- 关联文件：
  - `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`
  - `docs/BISSC_IMPLEMENTATION_TODO.md`
  - `docs/PIO_RESOURCE_PLAN.md`
- 下一步：
  - 在原理图阶段按 `ISO6440F x2` 作为完整版本默认方案，同时保留 `ISO6442F` 精简 BOM 选项。

### BISSC-TASK-20260707-006 - RJ45 差分通信外围电路方案

- 状态：进行中
- 日期：2026-07-07
- 任务目标：
  - 将 BiSS-C、双路 half-duplex RS-485、12 V 供电和差分触发统一收敛到一根 Cat5e/Cat6 网线的四对线设计。
  - 明确 `THVD1452` 作为 P0/P1/P2 默认收发器，满足串联 TAP bridge 的接收和透传驱动需求。
  - 给原理图设计提供端接、bias、保护、供电、0 ohm 矩阵和验证清单。
- 完成内容：
  - 新增 `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`。
  - 定义 RJ45 T568B 四对线默认分配：`DIFF0`、`DIFF1`、`12V/GND`、`TRIG_DIFF`。
  - 定义 BiSS-C full-duplex 和双路 RS485-HD 两种装配模式。
  - 明确首版优先使用 0 ohm / 焊桥 / 跳帽矩阵，不把普通模拟开关放进高速差分路径。
  - 更新 P2 TODO，将外围电路拆分为 THVD1452、RJ45、端接、bias、供电、保护、隔离和示波器验证子项。
- 验证结果：
  - 本任务为设计文档和原理图输入，未修改固件代码，未执行构建、烧录或板端验证。
  - 方案结论：一根网线四对线可同时承载两对通信差分、一路 12 V 供电和一路独立差分触发；但必须标注非以太网/非 PoE，并完成入口保护。
- 还需完成：
  - 原理图实现、BOM 选择、PCB 差分走线、端接/bias 阻值冻结。
  - 真实线缆下的 1 MHz/5 MHz BiSS-C、RS485-HD、差分触发 latency、12 V 压降和热插拔验证。
- 关联文件：
  - `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`
  - `docs/BISSC_IMPLEMENTATION_TODO.md`
  - `docs/BISSC_TASK_PROGRESS.md`
- 下一步：
  - 按外围电路检查清单进入原理图设计，并在 P0 bring-up 时验证串联 TAP bridge
    的 `CLK_IN -> CLK_OUT`、`DATA_IN -> DATA_OUT` 透明透传。

### BISSC-TASK-20260707-005 - AM26LV31E/AM26LV32E 收发器资料评审

- 状态：进行中
- 日期：2026-07-07
- 任务目标：
  - 评审 `DOC\外围硬件手册` 中的 AM26LV31E / AM26LV32E 是否适合作为 BiSS-C 差分物理层。
  - 明确 P0 TAP bridge 与 P1 MASTER/SLAVE/BRIDGE 对 driver / receiver 的使用边界。
- 完成内容：
  - 从本地 PDF 手册抽取关键参数：两器件均为 3.3 V、TIA/EIA-422-B / ITU V.11 级别、最高 32 MHz 器件。
  - 确认 `AM26LV32E` 是四路差分接收器，具备 ±200 mV 级差分灵敏度、±7 V 共模范围、open-circuit fail-safe 和 3.3 V GPIO 兼容输出。
  - 确认 `AM26LV31E` 是四路差分驱动器，输入兼容 RP2350 3.3 V GPIO，100 ohm 负载差分输出典型 2.6 V，传播延迟典型 8 ns、最大 12 ns。
  - 在 `docs/BISSC_TAP_BRIDGE_DESIGN.md` 中新增 RS-422 物理层候选器件章节。
  - 将 P2 产品化 TODO 拆分为原理图、终端、fail-safe、隔离保护和示波器验证子项。
- 验证结果：
  - 资料评审结论：这对器件可作为当前 5 MHz 固定 profile BiSS-C P0/P1 的首版物理层候选，尤其适合点对点、固定方向、RS-422-like 链路。
  - 修正后结论：P0 串联 TAP bridge 也需要透传驱动路径；`AM26LV32E` 接收-only
    只适合并联高阻监听夹具，不满足产品串联透传目标。
  - 31E/32E 不是半双工 RS-485 收发一体器件，不应用于共享总线仲裁、多主驱动或自动方向控制。
- 还需完成：
  - 尚未完成原理图级端接、bias、隔离、TVS、共模抑制和 bypass 设计。
  - 尚未完成真实线缆、逻辑发生器、示波器下的传播延迟、透传 skew、sample window、
    idle/fail-safe 和 TAP bridge 透明性验证。
- 关联文件：
  - `DOC\外围硬件手册\am26lv31e.pdf`
  - `DOC\外围硬件手册\am26lv32e.pdf`
  - `docs/BISSC_TAP_BRIDGE_DESIGN.md`
  - `docs/BISSC_IMPLEMENTATION_TODO.md`
  - `docs/BISSC_TASK_PROGRESS.md`
- 下一步：
  - 在原理图中按接收端终端、外部 bias 预留、driver enable 固定/可控、保护与隔离器件完成硬件方案。
  - 用 1 MHz CSV 回放先验证 `AM26LV32E -> RP2350` 采样链，再进入 5 MHz 示波器 sample window 验证。

### BISSC-TASK-20260707-004 - 1 MHz CSV 向量离线分析与绘图工具

- 状态：完成
- 日期：2026-07-07
- 任务目标：
  - 为后续 PIO simulator / 逻辑发生器回放准备可复用的 1 MHz CLK/DATA CSV 向量。
  - 在没有外部仪器的情况下，先用 Python 对 CSV 做边沿级离线解码，确认 frame、position、CRC 和 crossing 逻辑正确。
  - 增加 SVG 绘图能力，便于人工检查 CLK/DATA 时序、frame 区间和 bit 采样位置。
- 完成内容：
  - 增强 `tools/biss_wavegen/biss_wavegen.py`，支持 CRC profile 参数并生成真实 CRC 字段。
  - 新增 `tools/biss_wavegen/biss_wave_validate.py`，按 CLK active edge 重组 frame，校验 anchor、position、CRC、status gate 和 crossing。
  - 新增 `tools/biss_wavegen/biss_wave_plot.py`，输出自包含 SVG 阶梯波图，显示 CLK、DATA、frame 分区和 bit index。
  - 生成默认 48-bit profile 与带 CRC6 的 40-bit profile 两组 1 MHz CSV、summary、validate report 和 SVG。
- 验证结果：
  - `python tools\biss_wavegen\biss_wavegen.py --out build\biss_wavegen\biss_1mhz.csv` 通过。
  - `python tools\biss_wavegen\biss_wave_validate.py --csv build\biss_wavegen\biss_1mhz.csv --target 100` 通过。
  - 默认 48-bit profile 离线结果：`frames=3`、positions=`90,110,120`、frames=`0x8000005A0000/0x8000006E0000/0x800000780000`、`crossing_count=1`。
  - CRC6 40-bit profile 生成命令通过：
    `python tools\biss_wavegen\biss_wavegen.py --out build\biss_wavegen\biss_crc_1mhz.csv --frame-bits 40 --position-offset 4 --position-bits 20 --positions 90,110,120 --error-bit 24 --warning-bit 25 --crc-offset 26 --crc-bits 6 --crc-cover-offset 4 --crc-cover-bits 22 --crc-invert`。
  - `python tools\biss_wavegen\biss_wave_validate.py --csv build\biss_wavegen\biss_crc_1mhz.csv --target 100 --position-modulo 1048576 --status-gate ignore` 通过。
  - CRC6 40-bit profile 离线结果：`frames=3`、positions=`90,110,120`、frames=`0x80005AD000/0x80006EEF00/0x800078C200`、`crc_error_count=0`、`crossing_count=1`。
  - `python tools\biss_wavegen\biss_wave_plot.py build\biss_wavegen\biss_1mhz.csv --title "BiSS-C TAP 48-bit 1MHz"` 生成 `build\biss_wavegen\biss_1mhz.svg`。
  - `python tools\biss_wavegen\biss_wave_plot.py build\biss_wavegen\biss_crc_1mhz.csv --title "BiSS-C TAP CRC6 1MHz"` 生成 `build\biss_wavegen\biss_crc_1mhz.svg`。
  - `python -m py_compile tools\biss_wavegen\biss_wavegen.py tools\biss_wavegen\biss_wave_validate.py tools\biss_wavegen\biss_wave_plot.py tools\biss_board_validate\biss_board_validate.py` 通过。
  - `cmake --build build-biss-integration --target RP2350_TRIG --parallel` 通过。
- 还需完成：
  - 当前只完成 CSV 离线验证，尚未用真实 PIO simulator 或逻辑发生器驱动板端 AUX0/AUX1。
  - 需要将离线报告中的 frame/position/crossing 与板端 `STAT:BISS?` 实测统计对齐。
- 关联文件：
  - `tools/biss_wavegen/biss_wavegen.py`
  - `tools/biss_wavegen/biss_wave_validate.py`
  - `tools/biss_wavegen/biss_wave_plot.py`
  - `build\biss_wavegen\biss_1mhz.csv`
  - `build\biss_wavegen\biss_1mhz_validate.json`
  - `build\biss_wavegen\biss_1mhz.svg`
  - `build\biss_wavegen\biss_crc_1mhz.csv`
  - `build\biss_wavegen\biss_crc_1mhz_validate.json`
  - `build\biss_wavegen\biss_crc_1mhz.svg`
  - `docs/BISSC_IMPLEMENTATION_TODO.md`
  - `docs/BISSC_TASK_PROGRESS.md`
- 下一步：
  - 接入 PIO simulator 或逻辑发生器回放 CSV，执行板端 `TRIG:MODE 3` / `TRIG:ARM` / `STAT:BISS?` 验证。

### BISSC-TASK-20260707-003 - 单核 factory 回退与 BiSS board smoke 闭环

- 状态：完成
- 日期：2026-07-07
- 任务目标：
  - 按单核主线继续 BiSS-C 验证，暂停双核 smoke。
  - 将板子烧回单核 factory 固件。
  - 完成 SCPI 单核状态确认、SEQ_STEP ARM/DISARM smoke 和 BiSS board smoke。
- 完成内容：
  - 构建单核 factory/update 包。
  - 烧录 `build-biss-integration\RP2350_TRIG_FACTORY.uf2`。
  - 为 SCPI DISARM 增加兼容别名：`TRIG:DIS`、`TRIG:DISA`、`TRIG:DISarm` 均进入同一 DISARM 回调。
  - 更新 `docs/SCPI_COMMANDS.md`、`docs/BISSC_IMPLEMENTATION_TODO.md` 和 `docs/MULTICORE_PARTITION_PLAN.md`，记录单核主线和双核暂停原因。
- 验证结果：
  - 单核 factory build id：`20260707081355`。
  - `*IDN? -> RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`。
  - `SYST:FW:BUILD? -> "20260707081355"`。
  - `SYST:CORE? -> 0,9908,0,16184,0`，第一字段为 `0`，确认 core1 关闭。
  - `STAT:TRIG? -> "IDLE",0,16,0,0,0,0,0,0`。
  - `TRIG:MODE 1 -> "OK"`。
  - `TRIG:ARM -> "OK"`。
  - `TRIG:DISA -> "OK"`。
  - DISARM 后 `STAT:TRIG? -> "SEQ_STEP",0,16,0,0,0,0,0,0`。
  - `SYST:ERR? -> 0,"No error"`。
  - `python tools\biss_board_validate\biss_board_validate.py COM4 --out-dir build-biss-integration\biss_validation_singlecore` 通过，结果 `PASS`。
  - BiSS smoke 中 `TRIG:MODE 3` 后 `STAT:BISS?` state 为 `6`，即 `BISS_CONFIGURED`。
  - `TRIG:ARM` 后 `STAT:BISS?` state 为 `7`，即 `BISS_ARMED`。
  - 软件注入 positions `99,101,102` 后，`rx_frame_count`、`trigger_count`、`pulse_out_count` 按预期增长。
  - `cmake --build build-biss-integration --target RP2350_TRIG --parallel` 通过。
  - `cmake --build build-biss-integration --parallel` 通过。
  - `python -m py_compile tools\biss_board_validate\biss_board_validate.py tools\sd_trace_decode\sd_trace_decode.py` 通过。
- 还需完成：
  - 单核 smoke 使用软件帧注入路径，不等价于真实 AUX0/AUX1 CLK/DATA PIO 采样。
  - 真实 PIO/逻辑发生器回放仍需完成。
  - 双核 smoke 暂停；此前发现 core1 loop count 后续停止增长，需要后续独立定位跨核队列/critical section。
- 关联文件：
  - `middleware/scpi_port/src/scpi_port.c`
  - `docs/SCPI_COMMANDS.md`
  - `docs/BISSC_IMPLEMENTATION_TODO.md`
  - `docs/MULTICORE_PARTITION_PLAN.md`
  - `build-biss-integration\biss_validation_singlecore\summary.txt`
  - `build-biss-integration\biss_validation_singlecore\summary.json`
  - `build-biss-integration\biss_validation_singlecore\queries.txt`
- 下一步：
  - 进入 1 MHz CSV / PIO simulator / 逻辑发生器回放验证，确认真实采样路径的 `rx_frame_count`、`last_position` 和 `trigger_count`。

### BISSC-TASK-20260707-002 - P0 固定 profile TAP monitor 固件骨架

- 状态：进行中
- 日期：2026-07-07
- 任务目标：
  - 实现固定 profile 的 BiSS-C TAP monitor P0 骨架。
  - 让 BiSS-C 能通过 SCPI 配置 profile、进入 `BISS_CONFIGURED` / `BISS_ARMED`，并具备 position crossing 触发路径。
  - 保持 P0 范围收敛：只做 TAP monitor，不实现 `SLAVE_TX`、`MASTER_RX`、`BRIDGE_PROXY` 实时角色。
- 完成内容：
  - 新增 `biss_protocol.h/.c` 纯协议工具层。
  - 定义 `biss_profile_t`，覆盖 frame、position、anchor、status、CRC、sample delay、timeout 和 gate 策略。
  - 实现 profile validation、CRC helper、MSB-first bit extraction、status extraction、anchor match、CRC match、position crossing helper。
  - 扩展 TriggerVector 和 ECC 事件，增加 BiSS profile 与统计字段。
  - 新增 BiSS SCPI setter/getter 和 `STAT:BISS?`。
  - 新增 `biss_tap_rx.pio`，实现 CLK edge wait、DATA sample、RX FIFO chunk 输出骨架。
  - 新增 `biss_node_io`，覆盖 PIO claim、init、arm、disarm、poll、FIFO callback 和 frame processing。
  - 接入资源互斥，BiSS 占用 AUX0..AUX3 时拒绝冲突路径。
  - DISARM、FAULT、RESET 释放 BiSS 资源。
- 验证结果：
  - `tools/run_biss_protocol_tests.ps1`：ARM GCC 编译通过；当前环境无 host C compiler，host 执行跳过。
  - 后续单核 factory + board smoke 已在 `BISSC-TASK-20260707-003` 中完成。
- 还需完成：
  - 真实 PIO 输入采样尚未通过外部回放验证。
  - 5 MHz sample window、latency、jitter、TAP 透明性尚未测量。
  - P1 CRC blocking、SLAVE_TX、MASTER_RX、SELF_CAL_RING 尚未实现。
- 关联文件：
  - `components/sync_trigger/inc/biss_protocol.h`
  - `components/sync_trigger/src/biss_protocol.c`
  - `components/sync_trigger/inc/biss_node_io.h`
  - `components/sync_trigger/src/biss_node_io.c`
  - `components/sync_io/src/biss_tap_rx.pio`
  - `components/sync_io/src/sync_io.c`
  - `middleware/scpi_port/src/scpi_port.c`
  - `tests/unit/test_biss_protocol.c`
  - `tools/run_biss_protocol_tests.ps1`
- 下一步：
  - 使用板端 smoke 和 CSV 回放逐步确认 PIO 采样路径，而不是只停留在软件注入路径。

### BISSC-TASK-20260707-001 - BiSS-C 收发一体三通桥方案

- 状态：进行中
- 日期：2026-07-07
- 任务目标：
  - 定义一种收发一体的小板架构：本地脉冲输入、BiSS-C/SSI-like 帧发送、BiSS-C/SSI-like 帧接收解析、本地触发脉冲输出。
  - 明确“小板类似三通”的输入、输出、监听角色，避免把它误实现成单一编码器解析器。
  - 为后续 `RX_PULSE -> TX_BISS -> RX_BISS -> TX_PULSE` 闭环实现提供 HAOFV 边界和验证路线。
- 完成内容：
  - 新增 `docs/BISSC_TAP_BRIDGE_DESIGN.md`，定义 BiSS-C 收发一体三通桥方案。
  - 明确四类节点能力：`RX_PULSE`、`TX_BISS`、`RX_BISS`、`TX_PULSE`。
  - 定义源端、目的端、透明监听端、代理桥端四种配置方式。
  - 明确标准 BiSS-C 从站不能主动发送，`TX_BISS` 必须绑定到上游主站 clock polling。
  - 将 P0 收敛为固定 profile TAP monitor，P1/P2 再扩展从站发送、主站接收和 bridge proxy。
- 验证结果：
  - 本任务只完成方案和 TODO 更新，未修改固件代码，未执行构建、烧录或板端验证。
- 还需完成：
  - 后续实现低速固定帧 `SLAVE_TX` 与 `MASTER_RX`。
  - 后续根据实测决定是否继续用 RP2350 PIO 提速，或引入 FPGA/CPLD/专用 BiSS 接口芯片。
- 关联文件：
  - `docs/BISSC_TAP_BRIDGE_DESIGN.md`
  - `docs/BISSC_IMPLEMENTATION_TODO.md`
  - `docs/BISSC_TASK_PROGRESS.md`
- 下一步：
  - 先做固件 P0 骨架：模式、角色、SCPI 配置、状态计数和低速固定 profile TAP 接收。

## 当前已知风险

- AM26LV31E/AM26LV32E 已完成资料级可用性评审，但真实 BiSS-C 线缆、电平、终端匹配、隔离和 fail-safe bypass 尚未板级验证。
- 真实 PIO simulator 或逻辑发生器回放尚未完成，当前 1 MHz CSV 只证明向量和协议解析口径一致。
- 5 MHz sample window、`TRIG_OUT` latency、P99 jitter 尚未示波器验证。
- TAP 透明性尚未通过电气测量确认，即尚未确认固件/硬件完全不驱动上游 CLK/DATA。
- CRC blocking path 尚未实现；当前 CRC 能做 late/count，P1 需评估是否延迟触发直到 CRC 字段可用。
- 双核 smoke 暂停；release/validation 继续使用单核。此前双核 smoke 观察到 core1 loop count 后续停止增长，需要后续独立定位跨核队列/critical section。

## 下一步计划

### P0 剩余硬件验证

1. 将 `build\biss_wavegen\biss_1mhz.csv` 或 `build\biss_wavegen\biss_crc_1mhz.csv` 接入 PIO simulator 或逻辑发生器。
2. 板端配置对应 profile，执行 `TRIG:MODE 3` 和 `TRIG:ARM`。
3. 回放 CSV CLK/DATA。
4. 查询 `STAT:BISS?`，确认：
   - `rx_frame_count` 等于回放帧数。
   - `last_position` 等于最后一帧 position。
   - `trigger_count` 与离线 `crossing_count` 一致。
   - CRC profile 下 `crc_error_count=0`。
5. 生成硬件回放归档目录，例如 `build-biss-integration\biss_validation_pio_replay_1mhz`。

### P0 产品化前验证

1. 用示波器验证 5 MHz sample window。
2. 测量 `CLK active edge -> DATA sample -> TRIG_OUT` latency。
3. 记录 fixed latency offset 和 P99 jitter。
4. 验证 TAP 透明性：确认 AUX0/AUX1 只输入，AUX2/AUX3 未误驱动上游通道。

### P1 功能方向

1. 增加 CRC late-check worker 和统计发布。
2. 评估并实现可选 CRC-blocking 路径。
3. 增加 5 MHz 长时间 soak 和 timeout storm fault 策略。
4. 评估 10 MHz 固定 profile 接收器预算。
5. 启动 `SLAVE_TX` event profile、`MASTER_RX` 最小读取器和 `SELF_CAL_RING` 骨架。
