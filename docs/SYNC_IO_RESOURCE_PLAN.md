# RP2350 同步触发 PIO 资源规划

Status: Active
Domain: SYNC_IO
Canonical: `docs/SYNC_IO_RESOURCE_PLAN.md`
Related: `docs/SYNC_IO_REFACTOR_PLAN.md`, `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`, `docs/SCPI_COMMANDS.md`
Last updated: 2026-07-07

本文档为同步触发系统预留 RP2350 的 PIO、状态机和高速 IO 资源。目标是把确定性的输入捕获、触发判定和输出生成，与 UI、日志、存储、通信等非实时服务隔离开。

## 冻结状态与适用范围

| 项目 | 当前结论 |
|---|---|
| 文档状态 | `Active`。PIO/SM/DMA ownership 仍随固件实现演进；外部高速 IO pinout 和 AUX 两收两发方向按当前产品目标冻结。 |
| 适用硬件 | RP2350_TRIG 当前开发板和后续以 `GPIO16..23` 主触发口、`GPIO26..29` AUX 口为基础的硬件版本。 |
| 冻结约束 | `GPIO16..19` 为主输入组，`GPIO20..23` 为主输出组；`AUX0/AUX1` 固定输入，`AUX2/AUX3` 固定输出；非同步功能不得占用 PIO 状态机。 |
| 未决项 | 旧 `BOARD_SYNC_*` 宏仍需迁移到 AUX 语义通道；`ARM_IN`、`EXT_CLK_IN`、`SYNC_CLK_OUT`、`MARKER_OUT` 运行路径需要由资源仲裁器统一拒绝或迁移。 |

## 硬件资源预算

RP2350 提供 3 个 PIO block。每个 PIO block 有 4 个状态机和独立指令存储。

| 资源 | 总量 | 同步触发规划预留 |
|---|---:|---:|
| PIO block | 3 | 3 |
| 状态机 | 12 | 12 个全部预留给同步触发 IO |
| PIO 指令存储 | 3 x 32 条指令 | 按 PIO block 管理 |

状态 LED、LCD、UART、I2C、watchdog 和诊断功能不得占用同步触发 PIO 状态机。板载状态 LED 使用普通 GPIO 驱动。

## PIO Block 分配

| PIO block | 角色 | 原因 |
|---|---|---|
| `pio0` | 高速输入捕获和触发资格判定 | 输入采样、资格判定和 DMA/IRQ 所有权放在一起，减少跨域耦合。 |
| `pio1` | 确定性输出波形和触发生成 | 输出时序与输入捕获停顿、DMA 压力隔离。 |
| `pio2` | AUX 功能、辅助路由、协议辅助和后续扩展 | 为产品变体和跨模式功能留出独立资源，不扰动主触发时序。 |

## 状态机分配

状态机分配同时记录当前实现和产品目标。当前固件里，部分主口功能仍走旧路径；产品化迁移时应按 AUX 功能接口收口。

| PIO | 状态机 | 名称 | 当前/目标功能 |
|---|---:|---|---|
| `pio0` | `sm0` | `CAPTURE` | 当前用于 `GPIO16..GPIO19` 4 bit 输入采样，写入 RX FIFO/DMA。 |
| `pio0` | `sm1` | `TIMESTAMP_RESERVED` | 预留给粗/细边沿计时、事件 tick、捕获 strobe 或后续外部参考输入处理；当前未在 `sync_io` 中启用。 |
| `pio0` | `sm2` | `QUALIFIER_RESERVED` | 预留给触发滤波、去抖、门控资格和边沿选择；当前 `SEQ_STEP` gate 逻辑在 `pio1/sm0` 的模式程序内完成。 |
| `pio0` | `sm3` | `ARM_RESERVED` | 预留给硬件 ARM/DISARM 握手和捕获窗口控制；当前 `ARM_IN` 尚未接入 TriggerFB/PIO。 |
| `pio1` | `sm0` | `MAIN_OUTPUT` | 当前由主输出模式独占：即时 `TRIG_OUT`、`SEQ_STEP` 序列输出和 `ENC_COUNT` 比较触发都复用该 SM。 |
| `pio1` | `sm1` | `MAIN_OUT2_LEGACY_CLOCK` | 当前旧路径用于 `GPIO22` 同步时钟输出；产品目标是释放为主口 OUT2/`SEQ_STEP` bit2，将 `SYNC_CLK_OUT` 迁移到 AUX2/`pio2/sm2`。 |
| `pio1` | `sm2` | `MAIN_PULSE` | 当前用于 `GPIO21/PULSE_OUT` 第二路脉冲输出。代码宏名为 `BOARD_SYNC_GATE_SM`，实际用途是 pulse 输出，不是 `GATE_IN` 输入资格机。 |
| `pio1` | `sm3` | `MAIN_OUT3_RJ45_TRIGGER` | 当前用于 `GPIO23/OUT3`，产品硬件语义为 `RJ45_TRIG_OUT`；旧 `MARK:*` 软件 marker 命令临时复用该 SM，产品目标是将 `MARKER_OUT` 迁移到 AUX3/`pio2/sm3`。 |
| `pio2` | `sm0` | `AUX0_ARM` | 产品目标为 AUX0/GPIO26 `ARM_IN`；当前作为通用 AUX IO 初始化。 |
| `pio2` | `sm1` | `AUX1_EXT_CLK` | 产品目标为 AUX1/GPIO27 `EXT_CLK_IN`；当前作为通用 AUX IO 初始化。 |
| `pio2` | `sm2` | `AUX2_SYNC_CLK` | 产品目标为 AUX2/GPIO28 `SYNC_CLK_OUT`；当前作为通用 AUX IO 初始化，尚未承载同步时钟程序。 |
| `pio2` | `sm3` | `AUX3_MARKER` | 产品目标为 AUX3/GPIO29 `MARKER_OUT`；当前作为通用 AUX IO 初始化，尚未承载 marker 脉冲程序。 |

## GPIO 分配

外部连接器上最适合同步触发的连续 GPIO 组为 J2/J1 触发侧 `GPIO16..GPIO23`，以及 J1 辅助侧 `GPIO26..GPIO29`。这些引脚统一预留为同步触发高速 IO 区。

| GPIO | 方向 | 信号 | PIO owner | 说明 |
|---:|---|---|---|---|
| 16 | 输入 | `TRIG_IN` | `pio0/sm0`, `pio0/sm2` | 主外部触发输入。 |
| 17 | 输入 | `ENC_B_IN` / `MODE_IN1` | `pio0/sm3` | 主触发输入通道；产品映射中作为 `ENC_COUNT` B 相，也可作为后续模式本地输入。 |
| 18 | 输入 | `MODE_IN2` | `pio0/sm1` | 主触发输入备用通道，保留给后续模式本地功能或编码器扩展。 |
| 19 | 输入 | `RJ45_TRIG_IN` / `GATE_IN` | `pio0/sm2` | 上行 RJ45 差分触发输入，也可在模式内解释为 gate 或 inhibit。 |
| 20 | 输出 | `TRIG_OUT` | `pio1/sm0` | 主确定性触发输出。 |
| 21 | 输出 | `PULSE_OUT` | `pio1/sm2` | 第二路可编程脉冲或 burst 输出。 |
| 22 | 输出 | `MODE_OUT2` | `pio1/sm1` | 主触发输出通道；产品映射中作为 `SEQ_STEP` bit2。 |
| 23 | 输出 | `RJ45_TRIG_OUT` / `MODE_OUT3` | `pio1/sm3` | 下行 RJ45 差分触发输出，也可在模式内解释为 `SEQ_STEP` bit3。 |
| 26 | 输入 | `AUX0_ARM_IN` | `pio2/sm0` | 产品 AUX 固定接收：外部 ARM 资格/请求，也可在 BiSS persona 中作为 `BISS_CLK_IN`。 |
| 27 | 输入 | `AUX1_EXT_CLK_IN` | `pio2/sm1` | 产品 AUX 固定接收：外部参考或采样时钟，也可在 BiSS persona 中作为 `BISS_DATA_IN`。 |
| 28 | 输出 | `AUX2_SYNC_CLK_OUT` | `pio2/sm2` | 产品 AUX 固定发送：参考/分频同步时钟，也可在 BiSS persona 中作为 `BISS_CLK_OUT`。 |
| 29 | 输出 | `AUX3_MARKER_OUT` | `pio2/sm3` | 产品 AUX 固定发送：marker、状态或调试时序，也可在 BiSS persona 中作为 `BISS_DATA_OUT`。 |

`GPIO24` 保留为未来板级功能或调试备用 GPIO。

## 统一物理 IO 策略

产品硬件应在不同触发模式之间保持物理触发 IO pinout 稳定。这样输入侧可以固定增加施密特触发、保护、隔离，输出侧可以固定增加驱动器，不需要用户为不同固件模式重新接线。

量产高速触发主接口定义如下：

| 通道 | GPIO | 方向 | 物理前端 | 模式相关含义 |
|---|---:|---|---|---|
| IN0 | 16 | 输入 | 施密特/保护输入 | `SEQ_STEP` 触发输入 / `ENC_COUNT` A 相 |
| IN1 | 17 | 输入 | 施密特/保护输入 | `ENC_COUNT` B 相 / 模式本地输入通道 |
| IN2 | 18 | 输入 | 施密特/保护输入 | 模式本地输入通道 / 编码器备用 |
| IN3 | 19 | 输入 | RJ45 差分接收 / 施密特保护输入 | `RJ45_TRIG_IN` / `SEQ_STEP` gate 输入 / `ENC_COUNT` Z 相 |
| OUT0 | 20 | 输出 | 线路驱动 | `TRIG_OUT` / `SEQ_STEP` bit0 |
| OUT1 | 21 | 输出 | 线路驱动 | `PULSE_OUT` / `SEQ_STEP` bit1 |
| OUT2 | 22 | 输出 | 线路驱动 | `SEQ_STEP` bit2 / 模式本地输出通道 |
| OUT3 | 23 | 输出 | RJ45 差分驱动 / 线路驱动 | `RJ45_TRIG_OUT` / `SEQ_STEP` bit3 / 模式本地输出通道 |

固件模式可以重新解释各通道的逻辑含义，但正常产品使用不应要求移动外部线缆。

## 量产 AUX 功能接口

AUX 连接器是跨模式功能信号的稳定产品位置。主触发输入/输出口只承载模式本地的高速触发、编码器、门控和动作信号；AUX 口承载 ARM、参考时钟、同步输出、marker 等应跨触发模式保持稳定的框架层信号。

产品硬件已经定型为两收两发：`AUX0/AUX1` 固定输入，`AUX2/AUX3` 固定输出。
AUX 仍然可以按 persona 复用为 BiSS-C TAP、差分触发、校准或普通框架信号，但复用只改变固件语义和资源 ownership，不改变物理方向。

| AUX 通道 | GPIO | 方向 | 产品语义功能 | 说明 |
|---|---:|---|---|---|
| AUX0 | 26 | 输入 | `ARM_IN` | 外部 ARM 资格/请求。不再与主口 IN1 上的 `ENC_COUNT` B 相冲突。 |
| AUX1 | 27 | 输入 | `EXT_CLK_IN` | 外部参考/采样时钟，或后续协议参考。 |
| AUX2 | 28 | 输出 | `SYNC_CLK_OUT` | 参考/分频同步时钟输出。不再占用主口 OUT2。 |
| AUX3 | 29 | 输出 | `MARKER_OUT` | Marker、状态或调试时序输出。不再占用主口 OUT3。 |

`GPIO26..GPIO29` 不再作为备用 `ENC_COUNT` A/B/Z 输入组整体使用。若确需开发诊断，
只能临时复用 `AUX0/AUX1` 两个固定输入；`AUX2/AUX3` 是固定输出，不能作为编码器
输入采样脚。

## 框架/应用层接口契约

应用层、SCPI、UI 和 TriggerVector 应使用稳定语义通道描述触发 IO，而不是直接暴露任意 GPIO。GPIO 映射属于 board profile、`sync_io` 和板级配置头文件的职责。

量产语义通道如下：

| 语义通道 | 物理通道 | GPIO | 应用层含义 | 契约 |
|---|---|---:|---|---|
| `TRIG_IN` | IN0 | 16 | 主触发、事件或计数输入。 | 默认外部触发源。 |
| `ARM_IN` | AUX0 | 26 | 外部 ARM 资格或 ARM 请求。 | 应用层资格信号，不属于主触发输入通道。 |
| `EXT_CLK_IN` | AUX1 | 27 | 外部参考或采样时钟。 | 预留给显式外部时钟模式。 |
| `GATE_IN` | IN3 | 19 | gate、inhibit 或捕获窗口资格。 | 仅在当前模式未占用 IN3 时可用；`ENC_COUNT` 使用 Z 相时会冲突。 |
| `RJ45_TRIG_OUT` | OUT3 | 23 | RJ45 差分触发硬件输出。 | 硬件端口语义；BiSS crossing 和线缆触发输出使用该通道。 |
| `TRIG_OUT` | OUT0 | 20 | 主确定性动作输出。 | 默认比较/触发输出。 |
| `PULSE_OUT` | OUT1 | 21 | 第二路脉冲或 burst 输出。 | `SEQ_STEP` 将 OUT1 用作序列 bit1 时不可独立使用。 |
| `SYNC_CLK_OUT` | AUX2 | 28 | 参考/分频同步时钟输出。 | 框架层同步输出，不应占用主输出总线。 |
| `MARKER_OUT` | AUX3 | 29 | Marker、帧、调试或状态输出。 | 框架层 marker/status 输出，不应占用主输出总线。 |

触发模式 armed 期间，语义通道采用独占 ownership：

| 模式 | armed 时占用输入 | armed 时占用输出 | 说明 |
|---|---|---|---|
| `SEQ_STEP` | IN0=`TRIG_IN`；可选 IN3=`GATE_IN`；AUX0=`ARM_IN` 后续可接入 | OUT0..OUT3=`SEQ_OUT[3:0]`；AUX2/AUX3 保持框架输出 | 独立主输出总线命令应返回 busy 或在 ARM 前关闭。代码迁移到 `pio2` 后，AUX sync/marker 可独立支持。 |
| `ENC_COUNT` | IN0=A，IN1=B，IN3=Z；AUX0=`ARM_IN` 后续可接入 | OUT0=`TRIG_OUT`；AUX2/AUX3 保持框架输出 | `ARM_IN` 放到 AUX0 后不再与 B 相冲突。若 `GATE_IN` 仍在 IN3，则会与 Z 相冲突。 |
| `IDLE` / 管理态 | 无 | 按命令临时占用输出原语 | 即时脉冲、时钟和 marker 命令可使用各自语义输出，前提是没有触发模式占用。 |

原始 GPIO 选择命令只能作为 board profile 配置或开发诊断入口。产品 SCPI/UI 应优先使用语义通道，由 Trigger 资源仲裁器决定当前模式下请求是否可用。

当前固件仍保留一些旧低层宏：`BOARD_SYNC_ARM_IN_PIN`=`GPIO17`、`BOARD_SYNC_EXT_CLK_IN_PIN`=`GPIO18`、`BOARD_SYNC_SYNC_CLK_OUT_PIN`=`GPIO22`、`BOARD_SYNC_MARKER_OUT_PIN`=`GPIO23`。硬件 pinout 已冻结，`GPIO23` 的硬件语义是 `RJ45_TRIG_OUT`；`MARKER_OUT` 是软件/框架层标记语义，产品固件需要将 marker 运行路径迁移到 AUX3，或由资源仲裁器在冲突时拒绝命令。

## 实用性能目标

以下目标假设默认 `clk_sys` 约 150 MHz，量产固件使用 DMA 支撑持续捕获/输出流。

| 功能 | 理论上限 | 产品目标 |
|---|---:|---:|
| 单 bit 输入采样 | 最高 150 MS/s | 10-50 MS/s 持续，信号完整性验证后可提高 |
| 4 bit 并行输入采样 | 最高 150 MS/s 每采样 | 10-50 MS/s 持续 |
| 简单方波输出 | 约 75 MHz | 1-50 MHz，取决于抖动和负载要求 |
| 脉冲输出分辨率 | 1 个 PIO cycle | 150 MHz 下约 6.7 ns |
| DMA 支撑输出流 | 最高取决于 PIO pull 节拍 | 按波形格式实测确认 |

这些上限是 PIO 执行层面的理论值，不是板级电气保证。最终带宽必须结合目标 IO 电压、走线长度、负载、探头电容和固件 DMA 配置进行验证。

## 实现规则

- 不得在同步触发子系统外分配 `pio0`、`pio1` 或 `pio2` 状态机；如确需修改，必须同步更新本文档。
- LED heartbeat 和 UI 定时使用 GPIO/软件定时器，不占用 PIO。
- LCD 保持 SPI 接口；不得复用 `GPIO8..GPIO11` 或 `GPIO25` 做同步时序。
- `GPIO12..GPIO15` 保留给 TF/SD 卡接口。
- 高速同步 IO 尽量保持连续 GPIO，确保 PIO `in pins,n` 和 `out pins,n` 指令高效。
- PIO 程序放在 `drivers/mcu/pio/` 或所属 sync component 下，并通过 CMake `pico_generate_pio_header` 生成头文件。
- 任何预期运行超过短 burst 的捕获或输出模式都应使用 DMA。
