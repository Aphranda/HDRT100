# BiSS-C TAP Bridge 与 SYNC_IO 外围电路设计

Status: Frozen
Domain: BISSC
Canonical: `docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`
Related: `docs/communication/BISSC_TAP_BRIDGE_DESIGN.md`, `docs/sync/SYNC_IO_RESOURCE_PLAN.md`, `docs/sync/SYNC_IO_REFACTOR_PLAN.md`
Last updated: 2026-07-11

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
| 适用硬件 | RP2350_TRIG 后续 BiSS-C / SYNC_IO 扩展硬件版本；默认使用双 RJ45、三颗 `ISO1452` 隔离式 RS-485/RS-422 收发器、RJ45 前向触发下发通道和 SMA 装配矩阵。 |
| 冻结约束 | RJ45 不承载 Ethernet/PoE；`DIFF0`、`DIFF1`、`FWD_TRIG_DIFF` 独立；`AUX0/AUX1` 固定接收，`AUX2/AUX3` 固定发送；BiSS-C / RJ45 通信侧使用 `ISO1452` 隔离收发器；首版不使用普通模拟开关做高速差分运行时切换。 |
| 未决项 | 串联 TAP bridge 真实硬件透传、5 MHz sample window、SYNC_IO 隔离版本、12 V 取电/透传策略和长线 EMC 仍需按验证计划闭环。 |

## 最终架构结论

- 产品形态：两个 RJ45，`UPSTREAM_RJ45` 接原 BiSS-C 主站 / 控制器侧，
  `DOWNSTREAM_RJ45` 接原 BiSS-C 编码器 / 从站侧。
- BiSS-C 链路：本板串联在主站和编码器之间，透明透传 `CLK/MA` 与
  `DATA/SLO`，同时旁路解析 position/status 并从触发口输出同步脉冲。
- BiSS-C / RJ45 触发确定选型：`ISO1452` 隔离式 RS-485/RS-422 收发器。
- 串联 TAP bridge 至少使用三颗 `ISO1452`：
  - `ISO1452_UP_BISS`：上行 RJ45 侧接收 `CLK/MA`，并向上行 RJ45 侧发送
    `DATA/SLO`。
  - `ISO1452_DN_BISS`：下行 RJ45 侧接收 `DATA/SLO`，并向下行 RJ45 侧发送
    `CLK/MA`。
  - `ISO1452_TRIG`：上行 `FWD_TRIG_DIFF` 输入，下行 `FWD_TRIG_DIFF` 输出。
- 三颗 `ISO1452` 的逻辑侧 `VCC1/GND1` 接 RP2350 系统侧，隔离总线侧
  `VCC2/GND2` 接 RJ45 线缆侧隔离电源域，推荐使用 5 V 隔离电源驱动总线侧；
  `ISO1452` 不集成隔离电源，必须另配隔离 DC/DC 或等效隔离电源。
- `SYNC_IO` 的 SMA 外部口固定为 6 路装配矩阵，默认推荐 `3I3O`，
  可通过 0 ohm 装配版本选择 `2I4O`、`3I3O` 或 `4I2O`。
- RJ45 的 `FWD_TRIG_DIFF` 不计入 SMA 装配矩阵，独立作为通信侧差分前向触发 /
  同步下发通道。
- `AUX0..AUX3` 的硬件已经定型为两收两发：`AUX0/AUX1` 固定差分接收，
  `AUX2/AUX3` 固定差分发送；复用发生在固件 persona 层，不改变物理方向。
- BiSS-C 是 RJ45 协议 persona 的一个子集；同一硬件还冻结支持
  `HSPI_LIKE` persona，把三对差分通道解释为 `SCLK`、`MISO` 和
  `MOSI/SYNC`。
- 首版不在高速差分线上使用普通模拟开关做运行时切换；模式选择优先使用
  0 ohm 电阻、焊桥、跳帽矩阵或明确的装配版本。

## 双 RJ45 定义

两个 RJ45 使用相同 pinout，按 T568B 常见线序分配四对双绞线：

| RJ45 pin | 线对 | 默认信号 | 说明 |
|---|---|---|---|
| `1-2` | 橙对 | `DIFF0+/-` | BiSS-C `CLK/MA` 或可选 `RS485_CH0_A/B`。 |
| `3-6` | 绿对 | `DIFF1+/-` | BiSS-C `DATA/SLO` 或可选 `RS485_CH1_A/B`。 |
| `4-5` | 蓝对 | `PWR_12V/PWR_RETURN` | 12 V 供电 / 透传 / 注入；`pin 4 = +12V`，`pin 5 = RETURN/GND`。 |
| `7-8` | 棕对 | `FWD_TRIG_DIFF+/-` | 独立 RJ45 前向触发 / 同步下发。 |

方向定义：

```text
UPSTREAM_RJ45   -> 原 BiSS-C 主站 / 控制器侧
DOWNSTREAM_RJ45 -> 原 BiSS-C 编码器 / 从站侧
```

产品默认链路：

```text
UPSTREAM_RJ45.DIFF0   -> BISS_CLK_IN  -> BISS_CLK_OUT  -> DOWNSTREAM_RJ45.DIFF0
DOWNSTREAM_RJ45.DIFF1 -> BISS_DATA_IN -> BISS_DATA_OUT -> UPSTREAM_RJ45.DIFF1

UPSTREAM_RJ45.FWD_TRIG_DIFF   -> RJ45_FWD_TRIG_IN  -> GPIO19
GPIO23 -> RJ45_FWD_TRIG_OUT -> DOWNSTREAM_RJ45.FWD_TRIG_DIFF
```

闭环方向语义：

```text
DIFF0 / CLK/MA   : 前向，主站 / 控制器侧 -> 编码器 / 从站侧。
DIFF1 / DATA/SLO : 后向，编码器 / 从站侧 -> 主站 / 控制器侧。
FWD_TRIG_DIFF    : 前向触发 / 同步下发，控制器侧 -> DUT / 远端侧。
```

约束：

- 差分 `+/-` 必须在同一双绞对内，不允许跨颜色线对拆分。
- `DIFF0`、`DIFF1` 和 `FWD_TRIG_DIFF` 互相独立，触发通道不得借用 BiSS-C
  通信差分对。
- 上行触发只输入，下行触发只输出；首版不在同一 `FWD_TRIG_DIFF` 对上做半双工
  方向切换。
- `FWD_TRIG_DIFF` 定义为 RJ45 通信侧前向触发 / 同步下发通道，不承载 BiSS-C
  position/status 数据；后向 position/status/feedback 优先由 BiSS-C `DATA/SLO`
  承载。若产品需要独立反向硬线反馈，应增加反向装配版本或独立差分对。
- 新原理图和线束图使用 `FWD_TRIG_DIFF` 作为正式网络名；旧讨论中的
  `TRIG_DIFF` 仅表示同一前向触发线对的历史简称。

## BiSS-C TAP Bridge

P0 产品目标是串联透明桥，不是只并联监听。透明的含义是“不主动产生、吞掉、
改写 BiSS-C 帧”，不是“不驱动”。串联桥必须重新驱动下游 `CLK/MA` 和上游
`DATA/SLO`。

BiSS-C / RJ45 通信侧使用 `ISO1452` 做系统隔离。`ISO1452` 是全双工器件，
每颗器件包含 1 路 receiver 和 1 路 driver；BiSS-C TAP bridge 按 RJ45 端口侧
放置两颗 `ISO1452`，而不是按 `CLK` / `DATA` 两个信号各放一颗。这样每颗
BiSS-C 收发器都只连接同一个 RJ45 端口侧的一收一发差分对，原理图、隔离电源域
和端接保护更清晰。

`ISO1452_UP_BISS`、`ISO1452_DN_BISS` 和 `ISO1452_TRIG` 的逻辑侧直接接
RP2350 GPIO，总线侧连接 RJ45 差分线缆。`ISO1452` 自身提供信号隔离，但不提供
隔离电源；总线侧 `VCC2/GND2` 必须来自隔离电源域，默认按 5 V 供电设计，以获得
更好的差分输出幅度和线缆驱动裕量。转发延迟、skew、隔离传播延迟和线缆延迟都
必须进入 latency offset 标定。

ISO1452BDWR 功能脚按以下语义使用：

| 引脚组 | 方向 | 连接规则 |
|---|---|---|
| `A/B -> R` | 总线侧差分输入到逻辑侧单端输出 | `A` 为非反相 / `+`，`B` 为反相 / `-`；`R` 接 RP2350 输入。 |
| `D -> Y/Z` | 逻辑侧单端输入到总线侧差分输出 | `D` 接 RP2350 输出；`Y` 为非反相 / `+`，`Z` 为反相 / `-`。 |
| `/RE` | receiver enable，低有效 | BiSS-C TAP 默认直接接收使能，可接 `GND1`；如受 GPIO 控制，默认必须为接收使能。 |
| `DE` | driver enable，高有效 | 默认下拉禁用，上电和 PIO 配置完成后再由 GPIO 拉高。 |

如果原理图网络名使用 `_P/_N` 或 `+/-`，必须保持 `_P/+ -> A/Y`、
`_N/- -> B/Z`。只有在明确需要反相并已在固件/线束定义中记录时，才允许交换极性。

`DE` 默认下拉建议使用 10 kOhm 到 100 kOhm；`/RE` 若常开接收可直接接 `GND1`
或通过 0 ohm 配置位接地。22 ohm 只适合作为 GPIO 串联阻尼电阻，不适合作为
`DE` / `/RE` 的下拉电阻，否则 GPIO 拉高时会形成不必要的大电流。

### ISO1452_UP_BISS

```text
UPSTREAM_RJ45.DIFF0+
  -> 端接/保护
  -> ISO1452_UP_BISS A
UPSTREAM_RJ45.DIFF0-
  -> ISO1452_UP_BISS B
  -> isolation barrier
  -> R output -> GPIO26 / BISS_CLK_IN

GPIO29 / BISS_DATA_OUT
  -> ISO1452_UP_BISS logic-side Driver D
  -> isolation barrier
  -> Y -> 端接/保护 -> UPSTREAM_RJ45.DIFF1+
  -> Z -> 端接/保护 -> UPSTREAM_RJ45.DIFF1-
```

### ISO1452_DN_BISS

```text
DOWNSTREAM_RJ45.DIFF1+
  -> 端接/保护
  -> ISO1452_DN_BISS A
DOWNSTREAM_RJ45.DIFF1-
  -> ISO1452_DN_BISS B
  -> isolation barrier
  -> R output -> GPIO27 / BISS_DATA_IN

GPIO28 / BISS_CLK_OUT
  -> ISO1452_DN_BISS logic-side Driver D
  -> isolation barrier
  -> Y -> 端接/保护 -> DOWNSTREAM_RJ45.DIFF0+
  -> Z -> 端接/保护 -> DOWNSTREAM_RJ45.DIFF0-
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
- 转发延迟、skew、隔离收发器传播延迟和线缆延迟必须进入 latency offset 标定。
- 当前固件已有 TAP 接收/解析路径，但产品硬件要求的 `AUX0 -> AUX2`、
  `AUX1 -> AUX3` 固定延迟转发仍属于 bring-up 必做项；启用真实串联链路前
  必须完成并用示波器闭环验证。

## AUX 差分 Persona

从硬件抽象看，`GPIO26..29` 不是 BiSS-C 专用脚，而是一组固定方向、可被不同运行
模式独占的差分 AUX 资源。`GPIO19/GPIO23` 是同一 RJ45 协议链路中的前向触发 /
同步下发资源。硬件方向已经冻结为：

| 资源 | GPIO | 固定硬件方向 | BiSS-C persona | HSPI-like persona | 差分触发 persona |
|---|---:|---|---|---|---|
| `AUX0` | `GPIO26` | 上行 RJ45 -> RP2350 | `BISS_CLK_IN` | `HSPI_SCLK_IN` | `DIFF_A_IN` / 外部时钟输入 |
| `AUX1` | `GPIO27` | 下行 RJ45 -> RP2350 | `BISS_DATA_IN` | `HSPI_MISO_IN` | `DIFF_B_IN` / 远端触发输入 |
| `AUX2` | `GPIO28` | RP2350 -> 下行 RJ45 | `BISS_CLK_OUT` | `HSPI_SCLK_OUT` | `DIFF_A_OUT` / 同步时钟输出 |
| `AUX3` | `GPIO29` | RP2350 -> 上行 RJ45 | `BISS_DATA_OUT` | `HSPI_MISO_OUT` | `DIFF_B_OUT` / 辅助输出 |
| `RJ45_FWD_TRIG_IN` | `GPIO19` | 上行 RJ45 -> RP2350 | `BISS_FWD_TRIG_IN` | `HSPI_MOSI_IN` / `HSPI_SYNC_IN` | 前向触发输入 |
| `RJ45_FWD_TRIG_OUT` | `GPIO23` | RP2350 -> 下行 RJ45 | `BISS_FWD_TRIG_OUT` | `HSPI_MOSI_OUT` / `HSPI_SYNC_OUT` | 前向触发输出 |

可支持的 persona：

- `BISS_TAP_BRIDGE`：`AUX0 -> AUX2` 透传 `CLK/MA`，`AUX1 -> AUX3`
  透传 `DATA/SLO`，同时旁路解析 position/status。
- `HSPI_LIKE_AUX`：`AUX0 -> AUX2` 透传或生成 `SCLK`，
  `AUX1 -> AUX3` 透传或采样 `MISO`，`RJ45_FWD_TRIG_IN -> RJ45_FWD_TRIG_OUT`
  透传或生成 `MOSI/SYNC`。该 persona 用 PIO 实现高速同步串行，不声明为标准
  SPI 外设；片选可由帧间空闲、时钟停顿、`MOSI/SYNC` 编码或额外 SMA/底板 IO
  承载。
- `DIFF_TRIGGER_AUX`：把 `AUX0/AUX1` 作为两路差分输入，把 `AUX2/AUX3`
  作为两路差分输出，用于板间同步、外部 arm 或快速触发。
- `RS485_HD_AUX`：复用同一组两收两发资源承载半双工 RS-485 相关实验或派生版本；
  首版未冻结方向控制和总线装配矩阵前，不作为默认产品运行模式。
- `SELF_CAL_AUX`：把某一路差分输入和输出组成固定延迟校准环，用于测量线缆和节点
  转发 offset。

资源约束：

- 任意 persona 启用时必须独占 `AUX0..AUX3`、`RJ45_FWD_TRIG_IN/OUT`
  及对应 `ISO1452_*` 控制脚。
- Persona 复用只改变固件语义和资源 ownership，不表示 `AUX0/AUX1` 可变成输出，
  也不表示 `AUX2/AUX3` 可变成输入。
- `BISS_TAP_BRIDGE` 下不能再把 AUX2/AUX3 当普通辅助输出使用。
- `HSPI_LIKE_AUX` 与 `BISS_TAP_BRIDGE` 互斥；两者都占用同一组 RJ45 差分通道和
  `ISO1452_UP_BISS`、`ISO1452_DN_BISS`、`ISO1452_TRIG`。
- `DIFF_TRIGGER_AUX` 下不能同时运行 BiSS-C TAP 解码，除非固件显式实现并验证
  该复合模式。
- Persona 切换只能发生在 DISARM / 安全态；FAST_RT_TEST 中禁止改变收发器 enable、
  端接或 bias 策略。

## RJ45 前向触发下发通道

`FWD_TRIG_DIFF` 是独立 RJ45 差分前向触发 / 同步下发对，和 BiSS-C
`DIFF0/DIFF1` 物理隔离，也和 SMA 触发装配矩阵物理隔离。由于产品有上行和
下行两个 RJ45，可以接收控制器侧触发并转发到远端 / DUT 侧：

```text
UPSTREAM_RJ45.FWD_TRIG_DIFF+
  -> TVS/CMC/端接
  -> ISO1452_TRIG A
UPSTREAM_RJ45.FWD_TRIG_DIFF-
  -> ISO1452_TRIG B
  -> isolation barrier
  -> R output -> GPIO19 / RJ45_FWD_TRIG_IN

GPIO23 / RJ45_FWD_TRIG_OUT
  -> ISO1452_TRIG logic-side Driver D
  -> isolation barrier
  -> Y -> TVS/CMC/端接 -> DOWNSTREAM_RJ45.FWD_TRIG_DIFF+
  -> Z -> TVS/CMC/端接 -> DOWNSTREAM_RJ45.FWD_TRIG_DIFF-
```

要求：

- `FWD_TRIG_DIFF` 的系统语义固定为通信侧差分前向触发 / 同步下发通道，用于
  BiSS-C / RJ45 链路相关的硬实时触发、同步启动或测试同步。
- `FWD_TRIG_DIFF` 不计入 `SYNC_IO` SMA 输入/输出通道数量，不参与
  `2I4O` / `3I3O` / `4I2O` 装配矩阵。
- 上行触发输入和下行触发输出固定方向，不做运行时双向复用。
- `ISO1452_TRIG` 的 receiver 延迟、driver 延迟、隔离延迟和上下行线缆延迟都必须进入
  触发 latency offset 校准。
- 触发输出应由 PIO 或硬实时路径产生，不经过 SCPI、日志、SD 或普通任务。
- 若现场需要同一方向上的双向触发，应增加连接器或重新分配线对，不在首版
  `FWD_TRIG_DIFF` 上做半双工抢占。
- 产品 pinout 中 `GPIO23` 已固定为 `RJ45_FWD_TRIG_OUT`。`MARK:*` 只能作为
  历史兼容命令触发同一硬件输出；不再定义独立 `MARKER_OUT` 硬件路径。

## SYNC_IO 外部触发口

`SYNC_IO` 的 SMA 外部触发口固定为 6 路装配矩阵，不包含 RJ45 的
`FWD_TRIG_DIFF`。`GPIO16..18` 是默认 SMA 输入资源池，`GPIO20..22` 是默认
SMA 输出资源池；`GPIO19/GPIO23` 固定给 RJ45 前向触发下发，不参与 SMA 矩阵。
若装配版本需要第 4 路 SMA 输入或第 4 路 SMA 输出，可在释放 UART1 后分别使用
`GPIO4` / `GPIO5`。外部 `SMA_PORT0..5` 通过 0 ohm 装配版本选择输入/输出比例：

| 装配版本 | SMA 输入数量 | SMA 输出数量 | 输入 GPIO | 输出 GPIO | 说明 |
|---|---:|---:|---|---|---|
| `SMA_2I4O` | 2 | 4 | `GPIO16/GPIO17` | `GPIO20/GPIO21/GPIO22/GPIO5` | 输出资源优先版本；第 4 路输出使用 `GPIO5`，需释放 UART1。 |
| `SMA_3I3O` | 3 | 3 | `GPIO16/GPIO17/GPIO18` | `GPIO20/GPIO21/GPIO22` | 默认推荐版本，输入/输出均衡，不占用 `GPIO4/GPIO5`。 |
| `SMA_4I2O` | 4 | 2 | `GPIO16/GPIO17/GPIO18/GPIO4` | `GPIO20/GPIO21` | 输入资源优先版本；第 4 路输入使用 `GPIO4`，需释放 UART1。 |

0 ohm 装配矩阵约束：

- 任一 `SMA_PORTx` 只能装配为输入或输出之一，不能同时连接输入 GPIO 和输出 GPIO。
- 任一外部 net 上只允许一个 GPIO driver，禁止两个输出 GPIO 通过 0 ohm 硬短。
- 输入 GPIO 和输出 GPIO 不得通过 0 ohm 硬短。
- 每个装配版本必须在 BOM 中给出 fitted / DNP 表，禁止现场任意混装。
- `FWD_TRIG_DIFF` 是 RJ45 通信侧通道，不参与该装配矩阵。
- `GPIO19/GPIO23` 不得被 SMA 装配矩阵借用；它们分别固定为
  `RJ45_FWD_TRIG_IN` / `RJ45_FWD_TRIG_OUT`。

SMA 输入默认按普通 CMOS/TTL 触发源设计。虽然外部线缆使用 50 ohm 同轴和 SMA，
但默认不做 49.9 ohm 并联端接，避免对普通 3.3 V / 5 V TTL 输出形成过重负载。

每路 `SMA_INx` 默认原理图：

```text
SMA_INx center
  -> D_ESD_INx: TPD1E05U06DPYR to GND_EXT
  -> 50 ohm controlled single-ended trace
  -> R_SER_INx: 22R, placed close to U_BUF input
  -> U_BUF_INx: SN74LVC2G17QDCKRQ1 Schmitt input
  -> U_BUF_OUTx
  -> R_ISO_INx: 22R, placed close to U_BUF output
  -> ISO6440F_INx / ISO7740F_INx
  -> isolation barrier
  -> RP2350 SMA input GPIO selected by assembly variant
```

布局顺序必须保持：

```text
SMA -> ESD -> controlled trace -> series resistor -> Schmitt buffer -> ISO -> RP2350
```

输入侧约束：

- `TPD1E05U06DPYR` 必须贴近 SMA 中心脚，回到 `GND_EXT` / 入口地的路径最短。
- `R_SER_INx` 默认 22 ohm，靠近施密特触发器输入脚，用于限流、阻尼和减小输入
  管脚振铃；可按实测改为 33 ohm、49.9 ohm 或 68 ohm。
- 不默认装 49.9 ohm 到地端接；若派生版本明确只接 50 ohm 仪器触发源，可增加
  `R_TERM_INx = 49.9 ohm` 的 DNP 位并靠近 SMA 放置。
- `SN74LVC2G17QDCKRQ1` 作为默认双路施密特整形器；四路输入版本使用两颗。
  输入数量由 `SMA_2I4O` / `SMA_3I3O` / `SMA_4I2O` 装配版本决定。
- 施密特输出到 `ISO6440F/ISO7740F` 输入的走线小于 2 cm 时，`R_ISO_INx`
  默认装 22 ohm；若后续实测需要最小化延迟，可改 0 ohm，若过冲明显可改
  33 ohm。
  该段走线应避免与 SMA 输出驱动线平行长距离走线。
- SMA 输出默认按普通 CMOS/TTL 高阻触发输入驱动，不定义为 50 ohm 负载驱动。
  若需要 50 ohm 终端负载，应改用专用线驱动器并单独冻结输出幅度、电流和热设计。
- 每路 `SMA_OUTx` 默认原理图：

```text
RP2350 SMA output GPIO selected by assembly variant
  -> isolation barrier
  -> ISO6440F_OUTx / ISO7740F_OUTx
  -> U_DRV_INx: SN74LVC2G34
  -> U_DRV_OUTx
  -> R_SRC_OUTx: 22R, placed close to U_DRV output
  -> 50 ohm controlled single-ended trace
  -> D_ESD_OUTx: TPD1E05U06DPYR to GND_EXT, placed close to SMA
  -> SMA_OUTx center
```

- `SN74LVC2G34` 作为默认 SMA 输出驱动器，隔离 ISO 输出与外部 10 m 同轴线缆负载。
- `R_SRC_OUTx` 默认 22 ohm，靠近 `SN74LVC2G34` 输出脚；若 10 m 线缆实测振铃明显，
  可改 33 ohm、49.9 ohm 或 68 ohm。
- `D_ESD_OUTx` 使用 `TPD1E05U06DPYR`，靠近 SMA 输出中心脚，回到 `GND_EXT` / 入口地的
  路径最短。
- 若需要系统隔离，数字隔离器应放在 RP2350 与外部接收/驱动电路之间，并把隔离器、
  施密特触发器和保护网络的延迟计入 latency offset。

## SYNC_IO 隔离建议

若产品要求隔离，优先使用固定方向数字隔离器：

| 版本 | 推荐器件 | 通道方向 | 说明 |
|---|---|---|---|
| SMA 6 路装配矩阵 | `ISO6440F` x 2 或 `ISO7740F` x 2 | 一颗外部侧 -> RP2350，一颗 RP2350 -> 外部侧 | 提供最多 4 路隔离输入和最多 4 路隔离输出能力，按 `2I4O` / `3I3O` / `4I2O` 装配版本使用其中 6 路；`ISO6440F` 延迟/skew/CMTI 更优，`ISO7740F` 100 Mbps 版本也满足 5 MHz 触发需求。 |
| 精简 2 入 2 出 | `ISO6442F` x 1 | 2 forward / 2 reverse | 适合只引出 `TRIG_IN`、`GATE_IN`、`TRIG_OUT`、`PULSE_OUT` 的低成本版本。 |
| 最小 1 入 1 出 | `ISO6421F` x 1 | 1 forward / 1 reverse | 适合只做 `TRIG_IN` + `TRIG_OUT`。 |
| 单路验证 | `ISO7710` | 单向 1 channel | 可做样机验证，不适合作为完整 `SYNC_IO` core。 |

`F` 后缀优先，默认输出低，降低上电、掉电或隔离侧未供电时误触发风险。若选择
其他默认态后缀，必须在原理图和固件安全态中显式说明。

SMA 隔离版本最终器件族冻结为 `ISO6440F/ISO7740F` 二选一。两颗隔离器分别承载
SMA 装配矩阵的输入侧和输出侧固定方向隔离；方向由装配版本决定，不做运行时双向
切换。RJ45 `FWD_TRIG_DIFF` 由 `ISO1452_TRIG` 隔离，不占用这两颗 SYNC_IO 隔离器。

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
TRIG : FWD_TRIG_DIFF+/-
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
  -> ESD/TVS: ESDS302-Q1, close to RJ45
  -> 共模扼流圈，可选
  -> 端接 / bias 网络
  -> 10R series damping per A/B/Y/Z, close to ISO1452 pins
  -> ISO1452 bus side
  -> isolation barrier
  -> RP2350 GPIO
```

保护与 EMC：

- `ISO1452` 的 A/B/Y/Z 差分线默认使用 `ESDS302-Q1` 作为高速低电容 ESD/TVS，
  靠近 RJ45 入口放置，回流到 RJ45 线缆侧隔离地 / 入口保护地策略节点。
- `PESD2CANFD24V-Q` 可作为增强保护备选；其电容约 6 pF，适合速率裕量不极限
  的版本。`PSM712/SM712` 保护能力强但电容偏大，不作为 50 Mbps 默认，建议仅作
  DNP/备用封装或低速强保护版本。
- `TPD1E05U06DPYR` 只用于 SMA CMOS/TTL 触发口，不用于 `ISO1452` 高速差分线。
- A/B/Y/Z 每根线建议串 10 ohm 阻尼电阻，靠近 `ISO1452` 引脚放置；该电阻不是
  端接，接收端仍按 100 ohm / 120 ohm 终端策略处理。
- TVS 电容要与 50 Mbps 边沿兼容，避免过大电容破坏差分波形。
- 共模扼流圈选型要检查差模插损，不能只看 EMI 指标。
- 屏蔽网线的 shield 优先接机壳地 / 保护地，通过 RC、Y 电容或放电器件与
  数字地策略连接。
- `ISO1452` 自身不提供隔离电源；RJ45 总线侧必须有独立的隔离电源域，并和
  RP2350 系统地保持隔离边界。
- 如果现场存在误插 Ethernet 或 PoE 风险，应增加更强输入保护和限流，或改用
  防呆连接器。

## 12 V 供电

RJ45 蓝对建议定义为：

```text
Pin 4: PWR_12V / +12V
Pin 5: PWR_RETURN / GND_RET / GND
```

上行口和下行口必须保持同一极性：`pin 4` 始终为 `+12V`，`pin 5` 始终为
`PWR_RETURN/GND`。若做 12 V 透传，`UPSTREAM_RJ45 pin 4` 只能透传到
`DOWNSTREAM_RJ45 pin 4`，`pin 5` 只能透传到 `pin 5`，不得在任一端反接。

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
| `SMA_PORT0..5` | 装配矩阵 | 通过 0 ohm 版本选择 `2I4O` / `3I3O` / `4I2O`。 |
| `RJ45_FWD_TRIG_IN` | `GPIO19` | 上行 `FWD_TRIG_DIFF` 前向触发输入。 |
| `RJ45_FWD_TRIG_OUT` | `GPIO23` | 下行 `FWD_TRIG_DIFF` 前向触发输出。 |

当前板级已占用或不建议复用：

| GPIO | 板级连接 / 固件规划 | 约束 |
|---:|---|---|
| `GPIO0` | `UART0_TX` / CH343 | 不建议复用，除非确认不使用板载 USB-UART。 |
| `GPIO1` | `UART0_RX` / CH343 | 不建议复用，除非确认不使用板载 USB-UART。 |
| `GPIO2` | `KEY0` / 底板按钮复用 | 引到底板；仅作为按键、strap 或低速诊断输入，不作为高速/关键外设 IO。 |
| `GPIO3` | `LED` / 底板按钮复用 | 引到底板；可与板载状态 LED 做低速复用，不作为关键控制脚。 |
| `GPIO8` | `LCD_DC` | LCD 占用。 |
| `GPIO9` | `LCD_CS` | LCD / SPI CS 占用。 |
| `GPIO10` | `SDIO_SCK` / SPI SCK | SD/LCD SPI 时钟，占用。 |
| `GPIO11` | `SDIO_CMD` / SPI MOSI | SD/LCD SPI MOSI，占用。 |
| `GPIO12` | `SDIO_D0` / SPI MISO | SD/LCD SPI MISO，占用。 |
| `GPIO13` | `SDIO_D1` | SDIO 资源线，产品化不建议作为关键控制脚。 |
| `GPIO14` | `SDIO_D2` | SDIO 资源线，产品化不建议作为关键控制脚。 |
| `GPIO15` | `SDIO_D3` / SD CS | SD 卡 CS，占用。 |
| `GPIO16..18` | `SYNC_IO` SMA input pool | 默认 SMA 输入资源池。 |
| `GPIO19` | `RJ45_FWD_TRIG_IN` | 固定为 RJ45 前向触发输入，不参与 SMA 矩阵。 |
| `GPIO20..22` | `SYNC_IO` SMA output pool | 默认 SMA 输出资源池。 |
| `GPIO23` | `RJ45_FWD_TRIG_OUT` | 固定为 RJ45 前向触发输出，不参与 SMA 矩阵。 |
| `GPIO25` | `LCD_BL` | LCD 背光，占用。 |
| `GPIO26..29` | `BISS_*` / `RS485_*` / AUX | BiSS-C、RS485 和 AUX 复用区。 |

剩余可规划 IO：

| GPIO | 当前状态 | 推荐用途 |
|---:|---|---|
| `GPIO4` | 当前固件配置为 `BOARD_UART_TX_PIN` | 若释放 UART1，优先作为 `SMA_4I2O` 的第 4 路输入；否则可作为低速控制脚。 |
| `GPIO5` | 当前固件配置为 `BOARD_UART_RX_PIN` | 若释放 UART1，优先作为 `SMA_2I4O` 的第 4 路输出；否则可作为低速控制脚。 |
| `GPIO6` | 干净备用 | 冻结为 `ISO1452_UP_BISS_DE`。 |
| `GPIO7` | 干净备用 | 冻结为 `ISO1452_DN_BISS_DE`。 |
| `GPIO24` | 干净备用 | 冻结为 `ISO1452_TRIG_DE`。 |

三颗 `ISO1452` 的 `DE` 必须独立控制，冻结方案是：

```text
GPIO6  -> ISO1452_UP_BISS_DE
GPIO7  -> ISO1452_DN_BISS_DE
GPIO24 -> ISO1452_TRIG_DE
```

RJ45 连接器自带 LED 不占用 RP2350 GPIO，固定作为电源状态指示，例如
`12V_IN_PRESENT`、`12V_OUT_PRESENT`、`5V_OK` 或 `3V3_OK`。LED 由电源轨、
限流电阻、比较器或 power-good 信号驱动，不作为 `READY`、`ARMED`、
`TRIG`、`FAULT` 等固件状态 IO。

`GPIO2/GPIO3` 虽然引到底板，但只定义为低速按钮 / strap / 人机接口复用脚；
不得分配给 `ISO1452_*_DE`、`SYNC_IO` 实时触发、BiSS-C TAP 透传、隔离电源
enable 或其他 armed 状态下必须保持确定时序的功能。

如果产品版本需要 `SMA_4I2O` 或 `SMA_2I4O`，优先评估是否释放 `GPIO4/GPIO5`
的 UART1 功能。
不要优先占用 `GPIO13/GPIO14`；它们属于 SDIO 连接资源，可能被卡座、走线和未来
4-bit SD 模式影响。

## 固件迁移约束

硬件定型后，以下冲突必须在固件 bring-up 阶段消除：

- `GPIO23` 是 `RJ45_FWD_TRIG_OUT`，旧 `MARKER_OUT` 运行路径必须收敛为
  `RJ45_FWD_TRIG_OUT` 兼容入口，不能再迁移或声明为 AUX3 独立硬件信号。
- SMA 6 路装配矩阵必须有固件 board profile 标识；固件不得假设固定
  `SMA_INx` / `SMA_OUTx` 映射，也不得把 `GPIO19/GPIO23` 借给 SMA。
- `SYNC_CLK_OUT` 固件运行路径已迁移到 `AUX2/GPIO28`，并通过 `PIO2 + AUX`
  资源仲裁与 BiSS/AUX persona 互斥，不能占用 SMA 装配矩阵。
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
- [ ] `ISO1452` 作为 RJ45/BiSS 通信和 `FWD_TRIG_DIFF` 的确定隔离收发器选型。
- [ ] 原理图和固件配置中明确 `AUX0..AUX3` 当前 persona：
      `BISS_TAP_BRIDGE`、`DIFF_TRIGGER_AUX`、`RS485_HD_AUX` 或 `SELF_CAL_AUX`。
- [ ] `AUX0/AUX1` 固定为差分输入，`AUX2/AUX3` 固定为差分输出；文档、丝印、
      SCPI/UI 不再暗示 AUX 可运行时反向。
- [ ] 串联 TAP bridge 至少包含 `ISO1452_UP_BISS`、`ISO1452_DN_BISS`、
      `ISO1452_TRIG` 三颗隔离收发器。
- [ ] 三颗 `ISO1452` 的 `VCC1/GND1` 连接 RP2350 系统侧，`VCC2/GND2`
      连接 RJ45 总线侧隔离电源域；两侧地没有被 12 V return 或屏蔽策略硬短。
- [ ] `CLK_IN -> CLK_OUT`、`DATA_IN -> DATA_OUT` 透传路径已画入原理图。
- [ ] 闭环方向语义已在原理图标注：`CLK/MA` 前向，`DATA/SLO` 后向，
      `FWD_TRIG_DIFF` 作为 RJ45 前向触发 / 同步下发。
- [ ] 固件已实现并验证 `AUX0 -> AUX2`、`AUX1 -> AUX3` 固定延迟转发。
- [ ] 上行 `FWD_TRIG_DIFF` 只输入，下行 `FWD_TRIG_DIFF` 只输出，作为 RJ45
      通信侧前向触发 / 同步下发通道，不计入 SMA 矩阵。
- [ ] SMA 6 路装配矩阵已冻结为 `SMA_2I4O`、`SMA_3I3O` 或 `SMA_4I2O`
      之一，并有 fitted / DNP 表。
- [ ] 若装配 `SMA_2I4O` 或 `SMA_4I2O`，`GPIO5` 或 `GPIO4` 的 UART1 功能已释放，
      且固件 board profile 与 BOM fitted / DNP 表一致。
- [ ] `GPIO23` 旧 `MARKER_OUT` 路径已收敛为 `RJ45_FWD_TRIG_OUT` 兼容入口，不会与硬件定义冲突。
- [x] `GPIO22` 旧 `SYNC_CLK_OUT` 路径已迁移/仲裁，不会与 SMA 装配矩阵冲突。
- [ ] 0 ohm / 焊桥 / 跳帽矩阵不会把同一 SMA 口同时接到输入和输出资源。
- [ ] 接收端端接 100 ohm 默认，120 ohm 兼容位预留。
- [ ] RS485-HD bias 网络预留，默认装配策略明确。
- [ ] `DE` / `/RE` / enable 有默认安全电平。
- [ ] ISO1452BDWR 的差分极性一致：`_P/+` 接 `A/Y`，`_N/-` 接 `B/Z`，
      未经记录不得用原理图符号位置反接抵消。
- [ ] 12 V 入口具备限流、反接、TVS、滤波和 DC/DC。
- [ ] RJ45 蓝对极性已冻结为 `pin 4 = +12V`、`pin 5 = PWR_RETURN/GND`，
      上行和下行透传不反接。
- [ ] 12 V 在上行、本板、下行之间的取电/注入/透传策略已经冻结。
- [ ] 多个 12 V 来源不会硬并联；已设计 ideal diode、eFuse、OR-ing 或等效
      防反灌保护。
- [ ] `SYNC_IO` SMA 主口按 6 路装配矩阵装配，输入侧和输出侧不运行时反向。
- [ ] SMA 隔离版本使用两颗 `ISO6440F` 或两颗 `ISO7740F`；若在低成本版本中
      降级为 `ISO6442F` / `ISO6421F`，必须明确减少的 SMA 输入/输出通道。
- [ ] `SYNC_IO` 隔离侧电源、默认输出态和上电安全态已经在原理图中标注。
- [ ] SMA 输入整形和输出驱动电平与外部接口规格一致。
- [ ] 所有外部入口具备 ESD/TVS 策略。
- [ ] RJ45/BiSS 隔离电源、`ISO1452` 默认输出态和上电安全态已经在原理图中标注。

## 验证计划

### P0 硬件 bring-up

1. 只验证接收路径，禁用所有 driver，用 1 MHz CSV 回放 `CLK/DATA`，确认
   RP2350 `STAT:BISS?` 与离线报告一致。
2. 启用串联 TAP bridge 透传：`CLK_IN -> CLK_OUT`、`DATA_IN -> DATA_OUT`。
3. 用示波器确认串联 TAP bridge 的转发延迟、skew、idle 状态，以及上行/下行
   各段链路的幅度和边沿。
4. 验证上行 `FWD_TRIG_DIFF` 输入到 `RJ45_FWD_TRIG_IN`，下行
   `RJ45_FWD_TRIG_OUT` 到 `FWD_TRIG_DIFF` 输出。

### 5 MHz BiSS-C 验证

1. 使用 100 ohm 端接，回放 5 MHz BiSS-C fixed profile。
2. 扫描 `sample_delay_cycles`，记录稳定窗口。
3. 测量 `CLK active edge -> receiver output -> RJ45_FWD_TRIG_OUT` latency。
4. 测量 `CLK_IN -> CLK_OUT` 与 `DATA_IN -> DATA_OUT` 的转发延迟和 skew。
5. 记录 P99 jitter 和固定 latency offset。

### SYNC_IO 验证

1. 按当前 `SMA_2I4O` / `SMA_3I3O` / `SMA_4I2O` 装配版本，对所有 SMA 输入通道
   注入 1 kHz、100 kHz、1 MHz、5 MHz 单相脉冲，确认 PIO 捕获计数与输入边沿一致。
2. 对所有 SMA 输出通道与 `RJ45_FWD_TRIG_OUT` 输出固定宽度脉冲，测量输出宽度、
   传播延迟和通道间 skew。
3. 测量 `SMA_IN edge -> SMA_OUT pulse` 与
   `RJ45_FWD_TRIG_IN -> RJ45_FWD_TRIG_OUT` 闭环延迟，
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
