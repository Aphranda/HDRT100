# RP2350B QFN-80 IO 约束

Status: Draft
Domain: Hardware / Board
Target: RP2350B QFN-80
Canonical: `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`
Related: `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`, `docs/hardware/Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel`, `docs/sync/SYNC_IO_RESOURCE_PLAN.md`, `IO约束.md`
Last updated: 2026-08-13

本文档定义 RP2350_TRIG RP2350B QFN-80 硬件版本的实际 GPIO 分配与使用约束。
本版本以 `docs/hardware/Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` 为准；部分 PIO 引脚为
方便布线已偏离早期规划表，固件必须按本文档的 pin map 实现。若本文与网表不一致，
以最新网表为事实来源，并同步更新 board profile、产品板约束和验证矩阵。
现有根目录 `IO约束.md` 仍用于 RP2350A 小系统板，不由本文档替代。

## GND / FGND 地域与可选地桥

当前网表约定：

- `GND`：RP2350、USB、CH343、RS485 及本地诊断所在的 MCU 侧地。
- `FGND`：12 V 电源入口、功率级及 RS422/BiSS/Trigger 线缆侧地。
- `C37`：GND 与 FGND 之间的主高频耦合电容位置，不构成直流地连接。
- `C66/C67/C68/C69/C78/C84`：当前网表中也跨接 `GND-FGND`，应作为 EMI 调试预留或按量产装配策略复核。

为兼容调试和非隔离派生版本，可预留一个单点装配位：

```text
GND ---- R_GND_FGND 0R / DNP ---- FGND
```

约束：

- 只允许一个受控直流地桥位置，优先靠近电源/隔离边界，不能在连接器、USB 屏蔽
  和 RS422 接口处分散增加 0R。
- 跨地电容也应按单点主耦合 + EMI 调试预留管理；量产默认装配值必须由 EMI/抗扰验证决定。
- 量产隔离配置默认 `DNP`；安装 0R 后，GND 与 FGND 直流相通，ISO1452
  的系统级隔离不再成立。
- USB 调试供电若需要外部 `VCC5V` 回流，可使用独立的 `DBG_GND_LINK`
  装配位；它和量产隔离配置互斥，并必须在装配记录中标明。
- 装配 0R 后仍需检查 U20/U25、ESD/TVS、连接器屏蔽和安装孔，确认没有
  第二条未受控的 GND-FGND 直流路径。

## GPIO0..15：串口、按键、指示灯和 TF 卡

| GPIO | 方向 | 分配 | 约束 |
|---:|---|---|---|
| 0 | 输出 | `UART0_TX` | 连接 CH343P `U19` 调试串口。 |
| 1 | 输入 | `UART0_RX` | 连接 CH343P `U19` 调试串口。 |
| 2 | 输入 | `KEY1` | 接 `SW1` + `C81`，低有效，固件应开启上拉或确认外部上拉。 |
| 3 | 输出 | `LED_SYSTEM` | 接 `LED2`，指示系统运行、启动和 OTA 状态。 |
| 4 | 输出 | `UART1_TX` | 连接 MAX3485 `U21` 外部 RS485 TX。 |
| 5 | 输入 | `UART1_RX` | 连接 MAX3485 `U21` 外部 RS485 RX。 |
| 6 | 输入 | `KEY2` | 接 `SW1` + `C79`，低有效，固件应开启上拉或确认外部上拉。 |
| 7 | 输入 | `KEY3` | 接 `SW1` + `C80`，低有效，固件应开启上拉或确认外部上拉。 |
| 8 | 输出 | `LED_ARM` / `LED_ARM_TRIGGER` | 接贴片 LED `U18`，指示 ARM 状态及触发事件。 |
| 9 | 输出 | `LED_FAULT` | 接 `LED1`，指示故障状态。 |
| 10 | 输出 | `TF_SCK` / `TF_SPI1_SCK` | TF 卡 `CARD1` 独立 SPI 时钟。 |
| 11 | 输出 | `MOSI` / `TF_SPI1_MOSI` | TF 卡 `CARD1` 独立 SPI 数据输出。 |
| 12 | 输入 | `MISO` / `TF_SPI1_MISO` | TF 卡 `CARD1` 独立 SPI 数据输入。 |
| 13 | 输出 | `UART1_DE` | MAX3485 `U21` DE/RE 控制，复位默认接收。 |
| 14 | 输入 | `CD_DECT` / `TF_CARD_DETECT` | TF 卡检测脚，极性由固件配置。 |
| 15 | 输出 | `TF_CS` | 软件控制片选，上电默认拉高。 |

三个状态 LED 的实际有效电平应由 board profile 明确，串联限流电阻工作
电流建议控制在约 1 mA 至 3 mA。触发显示由软件延长 30 ms 至 100 ms，不得把 LED
直接并联到高速触发信号。

## GPIO16..29：同步 IO、RJ45 触发和 BiSS

| GPIO | PIO | 方向 | MCU 网络 | 逻辑分配 | 网表链路 |
|---:|---|---|---|---|---|
| 16 | PIO1 | 输出 | `PIO_16` | `SMA_OUT1` | `R33 -> PIO_OUT1 -> ISO7740 U9 -> SYNC_TRIG_OUT1/RF5` |
| 17 | PIO1 | 输出 | `PIO_17` | `SMA_OUT2` | `R31 -> PIO_OUT2 -> ISO7740 U9 -> SYNC_TRIG_OUT2/RF6` |
| 18 | PIO1 | 输出 | `PIO_18` | `SMA_OUT3` | `R32 -> PIO_OUT3 -> ISO7740 U9 -> SYNC_TRIG_OUT3/RF7` |
| 19 | PIO1 | 输出 | `PIO_19` | `SMA_OUT4` | `R34 -> PIO_OUT4 -> ISO7740 U9 -> SYNC_TRIG_OUT4/RF8` |
| 20 | PIO0 | 输入 | `PIO_20` | `SMA_IN4` | `SYNC_TRIG_IN4/RF4 -> ISO7740 U2/U4 -> PIO_IN4 -> R36` |
| 21 | PIO0 | 输入 | `PIO_21` | `SMA_IN3` | `SYNC_TRIG_IN3/RF3 -> ISO7740 U2/U4 -> PIO_IN3 -> R35` |
| 22 | PIO0 | 输入 | `PIO_22` | `SMA_IN2` | `SYNC_TRIG_IN2/RF2 -> ISO7740 U2/U3 -> PIO_IN2 -> R37` |
| 23 | PIO0 | 输入 | `PIO_23` | `SMA_IN1` | `SYNC_TRIG_IN1/RF1 -> ISO7740 U2/U3 -> PIO_IN1 -> R38` |
| 24 | PIO2 | 输入 | `PIO_24` | `BISS_DATA1_IN` | `RJ2 DATA1 -> ISO1452 U1 -> R64` |
| 25 | PIO2 | 输出 | `PIO_25` | `BISS_CLK1_OUT` | `R63 -> ISO1452 U1 -> RJ2 CLK1` |
| 26 | PIO1 | 输出 | `PIO_26` | `RJ45_FWD_TRIG_OUT` | `R66 -> ISO1452 U12 -> TRIG_OUT_P/N -> RJ2` |
| 27 | PIO0 | 输入 | `PIO_27` | `RJ45_FWD_TRIG_IN` | `RJ1 TRIG_IN_P/N -> ISO1452 U12 -> R65` |
| 28 | PIO2 | 输入 | `PIO_28` | `BISS_CLK0_IN` | `RJ1 CLK0 -> ISO1452 U14 -> R61` |
| 29 | PIO2 | 输出 | `PIO_29` | `BISS_DATA0_OUT` | `R62 -> ISO1452 U14 -> RJ1 DATA0` |

约束：

- SMA 固定为 4 路输入和 4 路输出。
- 删除原 `2I4O`、`3I3O`、`4I2O` 零欧姆交叉装配矩阵。
- GPIO16..19 保持连续输出，支持 PIO `OUT PINS, 4`。
- GPIO20..23 保持连续输入，但逻辑顺序为 `IN4, IN3, IN2, IN1`；固件读取
  `IN PINS, 4` 后必须按 pin map 解释或做 bit reverse。
- 输入和输出隔离器保持固定方向，不通过固件反转物理方向。
- GPIO 输出端串联/选配电阻应靠近驱动源；PIO 输入串联/选配电阻应靠近隔离器
  或接收器输出端。当前网表中 MCU 侧多处为 0R 装配位，线缆侧仍需按边沿速度和 EMI 复核。

## GPIO30..32：ISO1452 收发器控制

| GPIO | 控制器 | 方向 | 分配 |
|---:|---|---|---|
| 30 | SIO | 输出 | `GPIO_30 -> R67 -> UP_BISS_DE` |
| 31 | SIO | 输出 | `GPIO_31 -> R68 -> DN_BISS_DE` |
| 32 | SIO | 输出 | `GPIO_32 -> R69 -> TRIG_DE` |

三路 `DE` 必须独立控制，并分别配置下拉。当前网表中 `UP_BISS_DE`、
`DN_BISS_DE`、`TRIG_DE` 分别通过 `R43/R44/R42` 下拉到 `GND`。复位、
Bootloader 和固件初始化阶段必须保持驱动器关闭。PIO 和输出数据脚初始化完成后，
固件才允许使能对应驱动器。

三颗 ISO1452 的 `/RE` 由 RP2350 GPIO 控制，用于调试和派生版本配置。默认策略为
接收使能；每路 `/RE` 必须有确定的上电默认态，推荐下拉到 RP2350 所在的 `GND`：

```text
GPIO42 -> ISO1452_UP_BISS_/RE
GPIO40 -> ISO1452_DN_BISS_/RE
GPIO41 -> ISO1452_TRIG_/RE
```

每路 `/RE` 应保留测试点，便于样机诊断和派生版本修改。`DE` 的下拉电阻必须接
RP2350 控制侧 `GND`，不得接线缆侧 `FGND`。

## GPIO33..39：LCD 和备用控制

| GPIO | 方向 | 分配 |
|---:|---|---|
| 33 | 输入 | `FAULT_IN` |
| 34 | 输出 | `LCD_RST` |
| 35 | 输出 | `LCD_BL` |
| 36 | 输出 | `LCD_DC` |
| 37 | 输出 | `LCD_CS` |
| 38 | 输出 | `LCD_SPI0_SCK` |
| 39 | 输出 | `LCD_SPI0_MOSI` |

LCD 不使用 MISO。LCD 使用 SPI0，TF 卡使用 SPI1，两套 SPI 时钟不共用，允许
独立设置频率并使用 DMA。

## GPIO40..47：收发器控制与本地域模拟诊断

| GPIO | ADC | 分配 | 用途 |
|---:|---:|---|---|
| 40 | - | `DN_BISS_RE` | ISO1452 DN_BISS 接收使能控制，当前网表 `R7 -> GND` 下拉。 |
| 41 | - | `TRIG_RE` | ISO1452 TRIG 接收使能控制，当前网表 `R17 -> GND` 下拉。 |
| 42 | - | `UP_BISS_RE` | ISO1452 UP_BISS 接收使能控制，当前网表 `R16 -> GND` 下拉。 |
| 43 | ADC3 | `BOARD_TEMP1` | 板区温度监测。 |
| 44 | ADC4 | `BOARD_CUR1` | 外部 12 V 输出电流监测，AMC1301 隔离放大器输出。 |
| 45 | ADC5 | NC | 当前最新网表未连接有效网络，不得周期采样。 |
| 46 | ADC6 | NC | 当前最新网表未连接有效网络，不得周期采样。 |
| 47 | ADC7 | NC | 当前最新网表未连接有效网络，不得周期采样。 |

模拟约束：

- ADC 只连接 RP2350 所在的 `GND` / `VDDISO_3V3` 参考域内信号。
- 所有正常和故障条件下，ADC 输入必须保持在 GND 至 `ADC_AVDD` 范围内。
- 非隔离侧 `12V_IN`、`5V`、`PWR_RETURN` 不得通过分压或保护网络连接到 RP2350 ADC。
- 12 V 入口健康状态由 eFuse、比较器或电源 PG 生成数字信号，再经光耦或数字隔离送入 RP2350 GPIO。
- `BOARD_CUR1` 使用 AMC1301 隔离放大器输出，ADC 侧 `VDD/GND/OUT` 必须与 RP2350 ADC
  同域；被测侧参考 `FGND`，不得通过普通分压或非隔离放大器直接送入 RP2350 ADC。
- GPIO45..47 当前最新网表未连接有效网络；固件不得周期性采样这些浮空 ADC，
  除非后续装配或改版明确接入同域模拟前端并刷新本文。
- ADC 走线远离 QSPI、SPI 时钟、`VREG_LX` 和功率电感。
- RP2350 内部温度传感器不占用 GPIO40..47。

### ADC 前端电路

ADC 用于读取实际电压、电流和温度并记录趋势。过压、欠压、过流等保护不得只
依赖 ADC 和固件，必须由窗口比较器、电流检测比较器或 eFuse 形成独立硬件保护
路径。比较器输出可汇总到 `GPIO34/FAULT_IN`。

| ADC 信号 | 推荐前端 | 约束 |
|---|---|---|
| `BOARD_TEMP1` | TMP235 类模拟温度传感器 + 1 kOhm/10 nF RC | 靠近 RP2350 或板内代表性热区，并就近放置 100 nF 电源去耦。 |
| `BOARD_CUR1` | `VCC12V/C_OUT` 分流电阻 + AMC1301 + 1 kOhm/10 nF RC | AMC1301 输出侧供电、地和 ADC 同域；输入侧参考 `FGND`。 |
| GPIO45..47 / ADC5..7 | 当前最新网表未连接有效前端 | 固件初始化为未用 GPIO/ADC，不参与周期采样；后续若启用必须补充同域前端约束。 |

隔离侧低速模拟输入参考连接：

```text
ISO_DOMAIN_SIGNAL ---- R_DIV / R_LIMIT ----+---- 1k ---- GPIO43/44 ADC
                                           |               |
                                      optional R/C      10nF..100nF
                                           |               |
                                        ISO_GND        ISO_GND
```

该前端仅用于隔离侧板内慢速遥测，通常不需要运放。若提高分压电阻阻值、信号源
阻抗较高、需要更快连续采样或需要精密测量，应在分压和保护之后增加 3.3 V
单电源、轨到轨输入输出、单位增益稳定的运放缓冲器。

隔离侧可选负载电流检测参考连接：

```text
FGND-side current sense / shunt
              |
              +--- AMC1301 isolation amplifier ---> 1k ---> GPIO44 ADC
                                                        |
                                                       10nF
                                                        |
                                                       GND

12V_IN/eFuse PGOOD or FAULT
  -> optocoupler / digital isolator
  -> RP2350 GPIO status input
```

分流电阻功耗按 `P = I^2 * R` 计算，并留足温升和脉冲功率裕量。12 V 入口保护
和过流关断必须由 eFuse、比较器或电源开关独立完成，固件 ADC 只做遥测。

## 专用引脚

| 引脚组 | 分配 | 约束 |
|---|---|---|
| QSPI SCK/SD0..3/CSn | W25Q128JV 16 MB | 不作为普通 GPIO。 |
| QSPI CSn | `FLASH_CS/BOOTSEL` | BOOTSEL 按键连接 GND。 |
| USB DP/DM | USB Full-Speed | 按 90 Ohm 差分阻抗布线。 |
| RUN | RESET 按键 | 按键连接 GND。 |
| SWCLK/SWDIO | 调试下载 | 保留连接器或测试点。 |
| XIN/XOUT | 12 MHz 晶振 | 短走线并远离开关节点。 |
| ADC_AVDD | ADC 模拟 3.3 V | 滤波并就近去耦。 |
| DVDD | 1.1 V 核心电源 | 每个电源脚就近放置 100 nF。 |

RJ45 连接器自带 LED 只显示 `12V_IN_PRESENT`、`12V_OUT_PRESENT` 或隔离电源
Power Good，不占用 RP2350 GPIO。

## PIO 资源约束

```text
PIO0 input:  GPIO20..23 SMA_IN4..1 + GPIO27 RJ45_FWD_TRIG_IN
PIO1 output: GPIO16..19 SMA_OUT1..4 + GPIO26 RJ45_FWD_TRIG_OUT
PIO2 BiSS:   GPIO24 DATA1_IN, GPIO25 CLK1_OUT, GPIO28 CLK0_IN, GPIO29 DATA0_OUT
```

三个 PIO 的业务引脚均位于 GPIO0..31 窗口，可保持 `GPIO_BASE=0` 并同时运行。
任何 GPIO 只能有一个输出驱动源。运行期间禁止切换对应 PIO 的 GPIO Base，也禁止
将已经分配给 PIO 的输出 GPIO 切换到 SIO、PWM 或其他输出功能。固件中的 PIO
位序、mask 和 persona 定义必须以本表为唯一依据。

## 原理图检查项

- [ ] 三个按键均具有确定的上电默认电平。
- [ ] 三个状态 LED 不直接加载高速触发网络。
- [ ] TF 卡和 LCD 分别使用 SPI1 和 SPI0，网络名未误合并。
- [ ] SMA 输入和输出均为固定方向，不再保留交叉装配短接风险。
- [ ] 固件 PIO pin map 已按 GPIO16..29 实际布线更新，特别是 GPIO20..23 输入反序。
- [ ] 三颗 ISO1452 的 `DE` 独立下拉，`/RE` 具有确定默认态。
- [ ] GPIO40..47 的模拟/控制网络只位于 RP2350 `GND` 侧，不跨越 `FGND` 边界。
- [ ] GPIO45..47 当前不参与周期 ADC 采样，未用 ADC 状态由固件明确初始化。
- [ ] GND-FGND 只通过一个明确的 `0R/DNP` 装配位连接，且隔离版本默认不装。
- [ ] USB 调试供电的地桥与量产隔离配置互斥，装配状态已记录。
- [ ] QSPI、USB、晶振、SWD、RUN 和电源专用引脚未计入普通 GPIO 分配。
