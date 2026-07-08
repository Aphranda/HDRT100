# BiSS-C TAP Bridge 与 SYNC_IO 外围电路设计

Status: Frozen
Domain: BISSC
Canonical: `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`
Related: `docs/BISSC_TAP_BRIDGE_DESIGN.md`, `docs/SYNC_IO_RESOURCE_PLAN.md`, `docs/SYNC_IO_REFACTOR_PLAN.md`
Last updated: 2026-07-08

本文档作为 RP2350_TRIG 后续硬件版本的原理图输入规格，统一定义 BiSS-C
串联 TAP bridge、双 RJ45 线缆接口、SYNC_IO 触发输入输出、收发器选型、
供电保护和验证要求。

本文档中的 RJ45 只作为 Cat5e/Cat6 四对双绞线连接器使用，不是 Ethernet，
不运行 Ethernet / PoE 协议。连接器和外壳丝印必须明确标注
`NON-ETHERNET / NO POE`。

## 冻结状态与适用范围

| 项目 | 当前结论 |
|---|---|
| 文档状态 | `Frozen`。本文档作为后续硬件版本原理图输入规格，只允许勘误、补充验证记录和消除冲突。 |
| 适用硬件 | RP2350_TRIG 后续 BiSS-C / SYNC_IO 扩展硬件版本；默认使用双 RJ45、三颗 `THVD1452` 和固定方向 AUX 两收两发。 |
| 冻结约束 | RJ45 不承载 Ethernet/PoE；`DIFF0`、`DIFF1`、`TRIG_DIFF` 独立；`AUX0/AUX1` 固定接收，`AUX2/AUX3` 固定发送；首版不使用普通模拟开关做高速差分运行时切换。 |
| 未决项 | 串联 TAP bridge 真实硬件透传、5 MHz sample window、SYNC_IO 隔离版本、12 V 取电/透传策略和长线 EMC 仍需按验证计划闭环。 |

## 最终架构结论

- 产品形态：两个 RJ45，`UPSTREAM_RJ45` 接原 BiSS-C 主站 / 控制器侧，
  `DOWNSTREAM_RJ45` 接原 BiSS-C 编码器 / 从站侧。
- BiSS-C 链路：本板串联在主站和编码器之间，透明透传 `CLK/MA` 与
  `DATA/SLO`，同时旁路解析 position/status 并从触发口输出同步脉冲。
- BiSS-C / RJ45 触发确定选型：`THVD1452`。
- 串联 TAP bridge 至少使用三颗 `THVD1452`：
  - `THVD1452_CLK`：上行 `CLK/MA` 接收，下行 `CLK/MA` 转发。
  - `THVD1452_DATA`：下行 `DATA/SLO` 接收，上行 `DATA/SLO` 转发。
  - `THVD1452_TRIG`：上行 `TRIG_DIFF` 输入，下行 `TRIG_DIFF` 输出。
- `SYNC_IO` 固定为 4 入 4 出：3 路 SMA 输入 + 1 路上行 RJ45 触发输入，
  3 路 SMA 输出 + 1 路下行 RJ45 触发输出。
- `AUX0..AUX3` 的硬件已经定型为两收两发：`AUX0/AUX1` 固定差分接收，
  `AUX2/AUX3` 固定差分发送；复用发生在固件 persona 层，不改变物理方向。
- 首版不在高速差分线上使用普通模拟开关做运行时切换；模式选择优先使用
  0 ohm 电阻、焊桥、跳帽矩阵或明确的装配版本。

## 双 RJ45 定义

两个 RJ45 使用相同 pinout，按 T568B 常见线序分配四对双绞线：

| RJ45 pin | 线对 | 默认信号 | 说明 |
|---|---|---|---|
| `1-2` | 橙对 | `DIFF0+/-` | BiSS-C `CLK/MA` 或可选 `RS485_CH0_A/B`。 |
| `3-6` | 绿对 | `DIFF1+/-` | BiSS-C `DATA/SLO` 或可选 `RS485_CH1_A/B`。 |
| `4-5` | 蓝对 | `PWR_12V/GND` | 12 V 供电 / 透传 / 注入，按系统供电策略装配。 |
| `7-8` | 棕对 | `TRIG_DIFF+/-` | 独立差分触发 / 同步触发。 |

方向定义：

```text
UPSTREAM_RJ45   -> 原 BiSS-C 主站 / 控制器侧
DOWNSTREAM_RJ45 -> 原 BiSS-C 编码器 / 从站侧
```

产品默认链路：

```text
UPSTREAM_RJ45.DIFF0   -> BISS_CLK_IN  -> BISS_CLK_OUT  -> DOWNSTREAM_RJ45.DIFF0
DOWNSTREAM_RJ45.DIFF1 -> BISS_DATA_IN -> BISS_DATA_OUT -> UPSTREAM_RJ45.DIFF1

UPSTREAM_RJ45.TRIG_DIFF   -> RJ45_TRIG_IN  -> RP2350 IN3
RP2350 OUT3 -> RJ45_TRIG_OUT -> DOWNSTREAM_RJ45.TRIG_DIFF
```

约束：

- 差分 `+/-` 必须在同一双绞对内，不允许跨颜色线对拆分。
- `DIFF0`、`DIFF1` 和 `TRIG_DIFF` 互相独立，触发通道不得借用 BiSS-C
  通信差分对。
- 上行触发只输入，下行触发只输出；首版不在同一 `TRIG_DIFF` 对上做半双工
  方向切换。

## BiSS-C TAP Bridge

P0 产品目标是串联透明桥，不是只并联监听。透明的含义是“不主动产生、吞掉、
改写 BiSS-C 帧”，不是“不驱动”。串联桥必须重新驱动下游 `CLK/MA` 和上游
`DATA/SLO`。

### THVD1452_CLK

```text
UPSTREAM_RJ45.DIFF0
  -> 端接/保护
  -> THVD1452_CLK Receiver A/B
  -> R output -> GPIO26 / BISS_CLK_IN

GPIO28 / BISS_CLK_OUT
  -> THVD1452_CLK Driver D
  -> Y/Z -> 端接/保护
  -> DOWNSTREAM_RJ45.DIFF0
```

### THVD1452_DATA

```text
DOWNSTREAM_RJ45.DIFF1
  -> 端接/保护
  -> THVD1452_DATA Receiver A/B
  -> R output -> GPIO27 / BISS_DATA_IN

GPIO29 / BISS_DATA_OUT
  -> THVD1452_DATA Driver D
  -> Y/Z -> 端接/保护
  -> UPSTREAM_RJ45.DIFF1
```

### 固件与硬件约束

- TAP 模式下，`GPIO28/BISS_CLK_OUT` 必须固定延迟跟随
  `GPIO26/BISS_CLK_IN`。
- TAP 模式下，`GPIO29/BISS_DATA_OUT` 必须固定延迟跟随
  `GPIO27/BISS_DATA_IN`。
- 本地 position/status 解码只旁路采样 `BISS_CLK_IN/BISS_DATA_IN`，
  不允许改变 `BISS_CLK_OUT/BISS_DATA_OUT` 上的原始帧内容。
- `DE` 默认下拉到禁用，避免上电误驱动；桥接路径配置完成后再使能。
- `/RE` 可默认接收使能，用于本地采样和 echo/链路诊断；若受 GPIO 控制，
  默认态必须是安全接收态。
- 若要求本板掉电或固件未启动时原 BiSS-C 链路仍可通信，需要额外设计硬件
  bypass 或继电器旁路；不能假设禁用 driver 后链路仍透明。
- 转发延迟、skew、收发器传播延迟和线缆延迟必须进入 latency offset 标定。
- 当前固件已有 TAP 接收/解析路径，但产品硬件要求的 `AUX0 -> AUX2`、
  `AUX1 -> AUX3` 固定延迟转发仍属于 bring-up 必做项；启用真实串联链路前
  必须完成并用示波器闭环验证。

## AUX 差分 Persona

从硬件抽象看，`GPIO26..29` 不是 BiSS-C 专用脚，而是一组固定方向、可被不同运行
模式独占的差分 AUX 资源。硬件方向已经冻结为两收两发：

| AUX | GPIO | 固定硬件方向 | BiSS-C persona | 差分触发 persona | 通用 AUX persona |
|---|---:|---|---|---|---|
| `AUX0` | `GPIO26` | 上行 RJ45 -> RP2350 | `BISS_CLK_IN` | `DIFF_A_IN` / 外部时钟输入 | `ARM_IN` / `EXT_CLK_IN` |
| `AUX1` | `GPIO27` | 下行 RJ45 -> RP2350 | `BISS_DATA_IN` | `DIFF_B_IN` / 远端触发输入 | `EXT_REF_IN` / 诊断输入 |
| `AUX2` | `GPIO28` | RP2350 -> 下行 RJ45 | `BISS_CLK_OUT` | `DIFF_A_OUT` / 同步时钟输出 | `SYNC_CLK_OUT` |
| `AUX3` | `GPIO29` | RP2350 -> 上行 RJ45 | `BISS_DATA_OUT` | `DIFF_B_OUT` / 辅助输出 | `AUX3_TX` |

可支持的 persona：

- `BISS_TAP_BRIDGE`：`AUX0 -> AUX2` 透传 `CLK/MA`，`AUX1 -> AUX3`
  透传 `DATA/SLO`，同时旁路解析 position/status。
- `DIFF_TRIGGER_AUX`：把 `AUX0/AUX1` 作为两路差分输入，把 `AUX2/AUX3`
  作为两路差分输出，用于板间同步、外部 arm 或快速触发。
- `RS485_HD_AUX`：复用同一组两收两发资源承载半双工 RS-485 相关实验或派生版本；
  首版未冻结方向控制和总线装配矩阵前，不作为默认产品运行模式。
- `SELF_CAL_AUX`：把某一路差分输入和输出组成固定延迟校准环，用于测量线缆和节点
  转发 offset。

资源约束：

- 任意 persona 启用时必须独占 `AUX0..AUX3` 及对应 `THVD1452_*` 控制脚。
- Persona 复用只改变固件语义和资源 ownership，不表示 `AUX0/AUX1` 可变成输出，
  也不表示 `AUX2/AUX3` 可变成输入。
- `BISS_TAP_BRIDGE` 下不能再把 AUX2/AUX3 当普通辅助输出使用。
- `DIFF_TRIGGER_AUX` 下不能同时运行 BiSS-C TAP 解码，除非固件显式实现并验证
  该复合模式。
- Persona 切换只能发生在 DISARM / 安全态；FAST_RT_TEST 中禁止改变收发器 enable、
  端接或 bias 策略。

## RJ45 触发通道

`TRIG_DIFF` 是独立差分触发对，和 BiSS-C `DIFF0/DIFF1` 物理隔离。
由于产品有上行和下行两个 RJ45，可以同时提供网线触发输入和网线触发输出：

```text
UPSTREAM_RJ45.TRIG_DIFF
  -> TVS/CMC/端接
  -> THVD1452_TRIG Receiver A/B
  -> R output -> GPIO19 / SYNC_IO IN3 / RJ45_TRIG_IN

GPIO23 / SYNC_IO OUT3 / RJ45_TRIG_OUT
  -> THVD1452_TRIG Driver D
  -> Y/Z -> TVS/CMC/端接
  -> DOWNSTREAM_RJ45.TRIG_DIFF
```

要求：

- 上行触发输入和下行触发输出固定方向，不做运行时双向复用。
- `THVD1452_TRIG` 的 receiver 延迟、driver 延迟和上下行线缆延迟都必须进入
  触发 latency offset 校准。
- 触发输出应由 PIO 或硬实时路径产生，不经过 SCPI、日志、SD 或普通任务。
- 若现场需要同一方向上的双向触发，应增加连接器或重新分配线对，不在首版
  `TRIG_DIFF` 上做半双工抢占。
- 产品 pinout 中 `GPIO23/OUT3` 已固定为 `RJ45_TRIG_OUT`。`MARK:*` 只能作为
  历史兼容命令触发同一硬件输出；不再定义独立 `MARKER_OUT` 硬件路径。

## SYNC_IO 外部触发口

`SYNC_IO` 固定为单相、固定方向 4 入 4 出。外部连接器分配如下：

| 通道 | GPIO | 方向 | 外部连接器 | 默认语义 |
|---|---:|---|---|---|
| `IN0` | `GPIO16` | 输入 | `SMA_IN0` | `TRIG_IN` / `ENC_COUNT` A 相 / `SEQ_STEP` 触发 |
| `IN1` | `GPIO17` | 输入 | `SMA_IN1` | `ENC_COUNT` B 相 / 模式本地输入 |
| `IN2` | `GPIO18` | 输入 | `SMA_IN2` | `ENC_COUNT` Z 相 / 模式本地输入 |
| `IN3` | `GPIO19` | 输入 | `UPSTREAM_RJ45.TRIG_DIFF` | `RJ45_TRIG_IN` / `GATE_IN` |
| `OUT0` | `GPIO20` | 输出 | `SMA_OUT0` | `TRIG_OUT` / `SEQ_STEP` bit0 |
| `OUT1` | `GPIO21` | 输出 | `SMA_OUT1` | `PULSE_OUT` / `SEQ_STEP` bit1 |
| `OUT2` | `GPIO22` | 输出 | `SMA_OUT2` | `SEQ_STEP` bit2 / 模式本地输出 |
| `OUT3` | `GPIO23` | 输出 | `DOWNSTREAM_RJ45.TRIG_DIFF` | `RJ45_TRIG_OUT` / `SEQ_STEP` bit3 |

也就是：

```text
3 x SMA trigger input   -> SYNC_IO IN0..IN2
1 x RJ45 trigger input  -> SYNC_IO IN3

3 x SMA trigger output  -> SYNC_IO OUT0..OUT2
1 x RJ45 trigger output -> SYNC_IO OUT3
```

SMA 建议：

- SMA 输入预留 50 ohm 端接、限流、ESD/TVS、比较器或施密特整形选项。
- SMA 输出按接口版本选择 3.3 V/5 V TTL、50 ohm source series、开漏或工业
  24 V 驱动，不要在同一未标注接口中混用。
- 若需要系统隔离，数字隔离器应放在 RP2350 与外部接收/驱动电路之间，并把
  隔离延迟计入 latency offset。

## SYNC_IO 隔离建议

若产品要求隔离，优先使用固定方向数字隔离器：

| 版本 | 推荐器件 | 通道方向 | 说明 |
|---|---|---|---|
| 完整 4 入 4 出 | `ISO6440F` x 2 | 一颗外部侧 -> RP2350，一颗 RP2350 -> 外部侧 | 最匹配 `IN0..IN3` / `OUT0..OUT3` 固定方向。 |
| 精简 2 入 2 出 | `ISO6442F` x 1 | 2 forward / 2 reverse | 适合只引出 `TRIG_IN`、`GATE_IN`、`TRIG_OUT`、`PULSE_OUT` 的低成本版本。 |
| 最小 1 入 1 出 | `ISO6421F` x 1 | 1 forward / 1 reverse | 适合只做 `TRIG_IN` + `TRIG_OUT`。 |
| 单路验证 | `ISO7710` | 单向 1 channel | 可做样机验证，不适合作为完整 `SYNC_IO` core。 |

`F` 后缀优先，默认输出低，降低上电、掉电或隔离侧未供电时误触发风险。若选择
其他默认态后缀，必须在原理图和固件安全态中显式说明。

设计约束：

- `SYNC_IO` 输入侧和输出侧固定方向，不使用 I2C 类双向隔离器承载高速脉冲。
- 隔离侧必须有隔离电源；如果两侧共地供电，只能算电平缓冲，不能算系统隔离。
- 输入整形阈值、迟滞、最大输入电压和保护电流必须在接口规格中冻结。
- 隔离器传播延迟、pulse width distortion 和通道 skew 必须进入 PIO latency
  offset 标定。

## 可选 RS485-HD 复用模式

RJ45 的 `DIFF0/DIFF1` 可在派生装配版本中复用为两路 half-duplex RS-485：

```text
DIFF0: RS485_CH0_A/B
DIFF1: RS485_CH1_A/B
PWR  : 12V/GND
TRIG : TRIG_DIFF+/-
```

约束：

- 每一路 RS-485 都是一对半双工 A/B 总线。
- `DE` / `/RE` 由 GPIO 控制，方向切换只能发生在协议规定的空闲窗口。
- `DE` 默认下拉到接收安全态，避免上电误驱动总线。
- 推荐主从轮询或时隙协议，禁止多节点自由抢占发送。
- RS485-HD 是装配/固件 persona 复用模式，不是 BiSS-C TAP bridge 正常运行时的
  动态切换模式。
- 首版硬件默认按 BiSS-C TAP bridge 装配；若要发布 RS485-HD 派生版本，必须单独
  冻结 A/B 极性、端接、bias、DE 控制脚、协议空闲态和线缆兼容性测试。

## 端接、Bias 与保护

每个差分通道预留：

- 接收端 `RT_100`，默认 100 ohm。
- 兼容位 `RT_120`，用于 120 ohm 线缆或传统 RS-485 环境。
- RS485-HD 模式的 failsafe bias：`R_BIAS_PU`、`R_BIAS_PD`。
- bias 默认不装或按系统级空闲电流预算选择，避免多节点重复 bias 造成负载过重。

BiSS-C TAP bridge：

- 串联桥会形成四段点对点差分链路：主站到本板、本板到编码器、编码器到本板、
  本板到主站。
- 每段只在接收端放置终端电阻，禁止在同一段线缆上重复强端接。
- 必须用示波器确认上行和下行链路的幅度、边沿、idle 状态、转发延迟和 skew。

RJ45 入口建议从连接器向内依次考虑：

```text
RJ45
  -> ESD/TVS
  -> 共模扼流圈，可选
  -> 端接 / bias 网络
  -> THVD1452 / 隔离收发器
  -> 隔离器，可选
  -> RP2350 GPIO
```

保护与 EMC：

- TVS 电容要与 50 Mbps 边沿兼容，避免过大电容破坏差分波形。
- 共模扼流圈选型要检查差模插损，不能只看 EMI 指标。
- 屏蔽网线的 shield 优先接机壳地 / 保护地，通过 RC、Y 电容或放电器件与
  数字地策略连接。
- 长线、跨设备或强共模环境建议评估 `ISO1452` 等隔离收发器，并配隔离电源。
- 如果现场存在误插 Ethernet 或 PoE 风险，应增加更强输入保护和限流，或改用
  防呆连接器。

## 12 V 供电

RJ45 蓝对建议定义为：

```text
Pin 4: +12V
Pin 5: GND_RET
```

供电策略需要在系统图中冻结，可选：

- 上行口输入 12 V，本板取电，并可经保护后透传到下行口。
- 本板本地供电，同时向下行口注入 12 V。
- 不使用 RJ45 供电，蓝对保留或改为系统定义的低速辅助信号。

反灌约束：

- 上行取电、本板本地注入和下行透传不能硬并联到同一 12 V net。
- 如果允许多个 12 V 来源同时存在，必须使用 ideal diode、eFuse、OR-ing MOSFET、
  防反灌电源开关或等效保护方案。
- 下行口对外供电时必须有独立限流和故障检测；下游短路不能拉垮本板逻辑电源。

入口建议：

- 自恢复保险丝或电子保险丝。
- 反接保护 MOSFET。
- TVS 管到地。
- LC 或 pi 滤波。
- 本地 DC/DC：12 V -> 5 V / 3.3 V。
- 电源好信号或欠压检测，避免通信收发器在棕色区间误动作。

压降估算：

```text
Cat5e 24AWG 单芯约 0.08 ohm/m
一对线供电回路约 0.16 ohm/m

10 m, 0.3 A: 约 0.48 V 压降
10 m, 0.5 A: 约 0.80 V 压降
```

一对线走 12 V 适合低功耗节点。若节点电流较大，应提高输入电压、并联更多线对、
缩短线缆或改用独立电源线。

## RP2350 引脚与控制脚

主高速资源：

| 逻辑信号 | GPIO | 说明 |
|---|---:|---|
| `BISS_CLK_IN` | `GPIO26` | 上行 `CLK/MA` 接收。 |
| `BISS_DATA_IN` | `GPIO27` | 下行 `DATA/SLO` 接收。 |
| `BISS_CLK_OUT` | `GPIO28` | 下行 `CLK/MA` 透传输出。 |
| `BISS_DATA_OUT` | `GPIO29` | 上行 `DATA/SLO` 透传输出。 |
| `SMA_IN0..2` / `RJ45_TRIG_IN` | `GPIO16..19` | `SYNC_IO IN0..IN3`。 |
| `SMA_OUT0..2` / `RJ45_TRIG_OUT` | `GPIO20..23` | `SYNC_IO OUT0..OUT3`。 |

当前板级已占用或不建议复用：

| GPIO | 板级连接 / 固件规划 | 约束 |
|---:|---|---|
| `GPIO0` | `UART0_TX` / CH343 | 不建议复用，除非确认不使用板载 USB-UART。 |
| `GPIO1` | `UART0_RX` / CH343 | 不建议复用，除非确认不使用板载 USB-UART。 |
| `GPIO2` | `KEY0` | 可作为按键或低速 strap，不作为高速/关键外设 IO。 |
| `GPIO3` | `LED` | 保留为状态 LED，不作为关键控制脚。 |
| `GPIO8` | `LCD_DC` | LCD 占用。 |
| `GPIO9` | `LCD_CS` | LCD / SPI CS 占用。 |
| `GPIO10` | `SDIO_SCK` / SPI SCK | SD/LCD SPI 时钟，占用。 |
| `GPIO11` | `SDIO_CMD` / SPI MOSI | SD/LCD SPI MOSI，占用。 |
| `GPIO12` | `SDIO_D0` / SPI MISO | SD/LCD SPI MISO，占用。 |
| `GPIO13` | `SDIO_D1` | SDIO 资源线，产品化不建议作为关键控制脚。 |
| `GPIO14` | `SDIO_D2` | SDIO 资源线，产品化不建议作为关键控制脚。 |
| `GPIO15` | `SDIO_D3` / SD CS | SD 卡 CS，占用。 |
| `GPIO16..19` | `SYNC_IO IN0..IN3` | 主高速输入组，固定方向输入。 |
| `GPIO20..23` | `SYNC_IO OUT0..OUT3` | 主高速输出组，固定方向输出。 |
| `GPIO25` | `LCD_BL` | LCD 背光，占用。 |
| `GPIO26..29` | `BISS_*` / `RS485_*` / AUX | BiSS-C、RS485 和 AUX 复用区。 |

剩余可规划 IO：

| GPIO | 当前状态 | 推荐用途 |
|---:|---|---|
| `GPIO4` | 当前固件配置为 `BOARD_UART_TX_PIN` | 若释放 UART1，可作为低速控制脚。 |
| `GPIO5` | 当前固件配置为 `BOARD_UART_RX_PIN` | 若释放 UART1，可作为低速控制脚。 |
| `GPIO6` | 干净备用 | 推荐 `RS485_DE0`、`THVD1452_CLK_DE` 或模式控制。 |
| `GPIO7` | 干净备用 | 推荐 `RS485_DE1`、`THVD1452_DATA_DE` 或隔离电源 enable。 |
| `GPIO24` | 干净备用 | 推荐 `HW_MODE`、`HW_REV_DETECT`、`FAULT_IN` 或全局收发器 enable。 |

若三颗 `THVD1452` 的 `DE` 都需要独立控制，优先方案是：

```text
GPIO6  -> THVD1452_CLK_DE / RS485_DE0
GPIO7  -> THVD1452_DATA_DE / RS485_DE1
GPIO24 -> THVD1452_TRIG_DE / ISO_PWR_EN / 全局收发器使能
```

如果产品版本需要更多控制脚，优先评估是否释放 `GPIO4/GPIO5` 的 UART1 功能。
不要优先占用 `GPIO13/GPIO14`；它们属于 SDIO 连接资源，可能被卡座、走线和未来
4-bit SD 模式影响。

## 固件迁移约束

硬件定型后，以下冲突必须在固件 bring-up 阶段消除：

- `GPIO23/OUT3` 是 `RJ45_TRIG_OUT`，旧 `MARKER_OUT` 运行路径必须收敛为
  `RJ45_TRIG_OUT` 兼容入口，不能再迁移或声明为 AUX3 独立硬件信号。
- `GPIO22/OUT2` 是 `SMA_OUT2`；`SYNC_CLK_OUT` 固件运行路径已迁移到
  `AUX2/GPIO28`，并通过 `PIO2 + AUX` 资源仲裁与 BiSS/AUX persona 互斥。
- `BISS_TAP_BRIDGE` 启用后，`AUX0/AUX1` 只作为接收采样，`AUX2/AUX3`
  只作为固定延迟转发输出；普通 AUX sync/辅助输出功能必须被锁定。
- 资源仲裁器需要区分 `BISS_TAP_BRIDGE`、`DIFF_TRIGGER_AUX`、`RS485_HD_AUX`
  和普通 `SYNC_IO` ownership，禁止在 armed 状态切换 persona。
- SCPI/UI 应显示当前硬件 persona、收发器 enable 状态、端接/bias 装配版本和
  latency offset 校准状态，避免现场误判接线方向。

## 原理图检查清单

- [ ] 两个 RJ45 均使用相同 pinout，并标注 `UPSTREAM` / `DOWNSTREAM`。
- [ ] RJ45 丝印明确 `NON-ETHERNET / NO POE`。
- [ ] 所有差分 `+/-` 在同一双绞对内。
- [ ] `THVD1452` 作为 P0/P1/P2 确定收发器选型。
- [ ] 原理图和固件配置中明确 `AUX0..AUX3` 当前 persona：
      `BISS_TAP_BRIDGE`、`DIFF_TRIGGER_AUX`、`RS485_HD_AUX` 或 `SELF_CAL_AUX`。
- [ ] `AUX0/AUX1` 固定为差分输入，`AUX2/AUX3` 固定为差分输出；文档、丝印、
      SCPI/UI 不再暗示 AUX 可运行时反向。
- [ ] 串联 TAP bridge 至少包含 `THVD1452_CLK`、`THVD1452_DATA`、
      `THVD1452_TRIG` 三颗收发器。
- [ ] `CLK_IN -> CLK_OUT`、`DATA_IN -> DATA_OUT` 透传路径已画入原理图。
- [ ] 固件已实现并验证 `AUX0 -> AUX2`、`AUX1 -> AUX3` 固定延迟转发。
- [ ] 上行 `TRIG_DIFF` 只输入，下行 `TRIG_DIFF` 只输出。
- [ ] 3 路 SMA 输入、3 路 SMA 输出和 1 入 1 出 RJ45 触发映射与固件 GPIO 一致。
- [ ] `GPIO23` 旧 `MARKER_OUT` 路径已收敛为 `RJ45_TRIG_OUT` 兼容入口，不会与硬件定义冲突。
- [x] `GPIO22` 旧 `SYNC_CLK_OUT` 路径已迁移/仲裁，不会与 `SMA_OUT2` 冲突。
- [ ] 0 ohm / 焊桥 / 跳帽矩阵不会同时把 receiver 接到两个不同差分对。
- [ ] 接收端端接 100 ohm 默认，120 ohm 兼容位预留。
- [ ] RS485-HD bias 网络预留，默认装配策略明确。
- [ ] `DE` / `/RE` / enable 有默认安全电平。
- [ ] 12 V 入口具备限流、反接、TVS、滤波和 DC/DC。
- [ ] 12 V 在上行、本板、下行之间的取电/注入/透传策略已经冻结。
- [ ] 多个 12 V 来源不会硬并联；已设计 ideal diode、eFuse、OR-ing 或等效
      防反灌保护。
- [ ] `SYNC_IO` 主口按固定方向装配：`IN0..IN3` 只输入，`OUT0..OUT3` 只输出。
- [ ] 完整隔离版本使用两颗 `ISO6440F`，或在低成本版本中明确降级为
      `ISO6442F` / `ISO6421F`。
- [ ] `SYNC_IO` 隔离侧电源、默认输出态和上电安全态已经在原理图中标注。
- [ ] SMA 输入整形和输出驱动电平与外部接口规格一致。
- [ ] 所有外部入口具备 ESD/TVS 策略。
- [ ] 长线/跨设备版本预留隔离方案。

## 验证计划

### P0 硬件 bring-up

1. 只验证接收路径，禁用所有 driver，用 1 MHz CSV 回放 `CLK/DATA`，确认
   RP2350 `STAT:BISS?` 与离线报告一致。
2. 启用串联 TAP bridge 透传：`CLK_IN -> CLK_OUT`、`DATA_IN -> DATA_OUT`。
3. 用示波器确认串联 TAP bridge 的转发延迟、skew、idle 状态，以及上行/下行
   各段链路的幅度和边沿。
4. 验证上行 `TRIG_DIFF` 输入到 `IN3`，下行 `OUT3` 到 `TRIG_DIFF` 输出。

### 5 MHz BiSS-C 验证

1. 使用 100 ohm 端接，回放 5 MHz BiSS-C fixed profile。
2. 扫描 `sample_delay_cycles`，记录稳定窗口。
3. 测量 `CLK active edge -> receiver output -> RJ45_TRIG_OUT` latency。
4. 测量 `CLK_IN -> CLK_OUT` 与 `DATA_IN -> DATA_OUT` 的转发延迟和 skew。
5. 记录 P99 jitter 和固定 latency offset。

### SYNC_IO 验证

1. 对 `SMA_IN0..2` 与 `RJ45_TRIG_IN` 注入 1 kHz、100 kHz、1 MHz、5 MHz
   单相脉冲，确认 PIO 捕获计数与输入边沿一致。
2. 对 `SMA_OUT0..2` 与 `RJ45_TRIG_OUT` 输出固定宽度脉冲，测量输出宽度、
   传播延迟和通道间 skew。
3. 测量 `IN0 edge -> OUT0 pulse` 与 `RJ45_TRIG_IN -> RJ45_TRIG_OUT` 闭环延迟，
   记录最小值、最大值、P99 jitter 和固定 latency offset。
4. 分别测试逻辑侧先上电、隔离侧先上电、单侧掉电和热插拔，确认输出不产生误触发。

### RS485-HD 验证

1. CH0 / CH1 低速 UART 或 PIO 帧互通。
2. 验证 `DE` 方向切换 guard time。
3. 注入 bus idle、断线、短路和冲突发送，确认 fault 统计。
4. 长帧 soak，记录 CRC error、timeout 和重发统计。

### 供电和 EMC 初测

1. 按 5 m / 10 m / 20 m 线缆测量 12 V 压降和 DC/DC 输入纹波。
2. 热插拔测试，确认收发器和 RP2350 不 latch-up。
3. ESD 预扫或手持枪初测，确认通信不中断或可恢复。
