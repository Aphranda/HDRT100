# RP2350B QFN-80 IO 约束

Status: Draft
Domain: Hardware / Board
Target: RP2350B QFN-80

本文档定义 RP2350_TRIG 后续 RP2350B QFN-80 硬件版本的 GPIO 分配与使用约束。
现有根目录 `IO约束.md` 仍用于 RP2350A 小系统板，不由本文档替代。

## GPIO0..15：串口、按键、指示灯和 TF 卡

| GPIO | 方向 | 分配 | 约束 |
|---:|---|---|---|
| 0 | 输出 | `UART0_TX` | 连接 CH343 调试串口。 |
| 1 | 输入 | `UART0_RX` | 连接 CH343 调试串口。 |
| 2 | 输入 | `KEY1` | 低有效，外部 10 kOhm 上拉。 |
| 3 | 输出 | `LED_SYSTEM` | 绿色，指示系统运行、启动和 OTA 状态。 |
| 4 | 输出 | `UART1_TX` | 外部通信串口。 |
| 5 | 输入 | `UART1_RX` | 外部通信串口。 |
| 6 | 输入 | `KEY2` | 低有效，外部 10 kOhm 上拉。 |
| 7 | 输入 | `KEY3` | 低有效，外部 10 kOhm 上拉。 |
| 8 | 输出 | `LED_ARM_TRIGGER` | 黄色，指示 ARM 状态及触发事件。 |
| 9 | 输出 | `LED_FAULT` | 红色，指示故障状态。 |
| 10 | 输出 | `TF_SPI1_SCK` | TF 卡独立 SPI 时钟。 |
| 11 | 输出 | `TF_SPI1_MOSI` | TF 卡独立 SPI 数据输出。 |
| 12 | 输入 | `TF_SPI1_MISO` | TF 卡独立 SPI 数据输入。 |
| 13 | 输入 | `TF_CARD_DETECT` | 按卡座检测脚极性配置上拉。 |
| 14 | 输出 | `TF_CS` | 软件控制片选，上电默认拉高。 |
| 15 | 可配置 | `GPIO_SPARE0` | TF 卡 SPI 模式不使用 DAT2，本引脚释放为备用 GPIO。 |

三个状态 LED 建议使用低有效连接，串联 1 kOhm 至 2.2 kOhm 限流电阻，工作
电流控制在约 1 mA 至 3 mA。触发显示由软件延长 30 ms 至 100 ms，不得把 LED
直接并联到高速触发信号。

## GPIO16..25：同步 IO 和 RJ45 触发

| GPIO | PIO | 方向 | 分配 |
|---:|---|---|---|
| 16 | PIO0 | 输入 | `SMA_IN1` |
| 17 | PIO0 | 输入 | `SMA_IN2` |
| 18 | PIO0 | 输入 | `SMA_IN3` |
| 19 | PIO0 | 输入 | `SMA_IN4` |
| 20 | PIO1 | 输出 | `SMA_OUT1` |
| 21 | PIO1 | 输出 | `SMA_OUT2` |
| 22 | PIO1 | 输出 | `SMA_OUT3` |
| 23 | PIO1 | 输出 | `SMA_OUT4` |
| 24 | PIO0 | 输入 | `RJ45_FWD_TRIG_IN` |
| 25 | PIO1 | 输出 | `RJ45_FWD_TRIG_OUT` |

约束：

- SMA 固定为 4 路输入和 4 路输出。
- 删除原 `2I4O`、`3I3O`、`4I2O` 零欧姆交叉装配矩阵。
- GPIO16..19 保持连续，支持 PIO `IN PINS, 4`。
- GPIO20..23 保持连续，支持 PIO `OUT PINS, 4`。
- 输入和输出隔离器保持固定方向，不通过固件反转物理方向。
- GPIO 输出端可保留 22 Ohm 至 33 Ohm 串联阻尼电阻。

## GPIO26..32：BiSS/AUX 和收发器控制

| GPIO | 控制器 | 方向 | 分配 |
|---:|---|---|---|
| 26 | PIO2 | 输入 | `AUX0/BISS_CLK_IN` |
| 27 | PIO2 | 输入 | `AUX1/BISS_DATA_IN` |
| 28 | PIO2 | 输出 | `AUX2/BISS_CLK_OUT` |
| 29 | PIO2 | 输出 | `AUX3/BISS_DATA_OUT` |
| 30 | SIO | 输出 | `ISO1452_UP_BISS_DE` |
| 31 | SIO | 输出 | `ISO1452_DN_BISS_DE` |
| 32 | SIO | 输出 | `ISO1452_TRIG_DE` |

三路 `DE` 必须独立控制，并分别配置 10 kOhm 至 47 kOhm 下拉。复位、
Bootloader 和固件初始化阶段必须保持驱动器关闭。PIO 和输出数据脚初始化完成后，
固件才允许使能对应驱动器。

三颗 ISO1452 的 `/RE` 固定为接收使能，并通过独立 0 Ohm 电阻接地：

```text
ISO1452_UP_BISS_/RE -> 0R -> GND
ISO1452_DN_BISS_/RE -> 0R -> GND
ISO1452_TRIG_/RE    -> 0R -> GND
```

每路 `/RE` 应保留测试点，便于样机诊断和派生版本修改。

## GPIO33..39：LCD 和备用控制

| GPIO | 方向 | 分配 |
|---:|---|---|
| 33 | 可配置 | `CONTROL_SPARE0` |
| 34 | 输入/可配置 | `FAULT_IN/CONTROL_SPARE1` |
| 35 | 输出 | `LCD_BL` |
| 36 | 输出 | `LCD_DC` |
| 37 | 输出 | `LCD_CS` |
| 38 | 输出 | `LCD_SPI0_SCK` |
| 39 | 输出 | `LCD_SPI0_MOSI` |

LCD 不使用 MISO。LCD 使用 SPI0，TF 卡使用 SPI1，两套 SPI 时钟不共用，允许
独立设置频率并使用 DMA。

## GPIO40..47：隔离侧模拟诊断

| GPIO | ADC | 分配 | 用途 |
|---:|---:|---|---|
| 40 | ADC0 | `BOARD_TEMP_MON` | RP2350 / 板区温度监测。 |
| 41 | ADC1 | `POWER_TEMP_MON` | 隔离 DC/DC、LDO 或 ISO1452 附近温度监测。 |
| 42 | ADC2 | `HW_ID_MON` | 硬件版本识别电阻。 |
| 43 | ADC3 | `ANALOG_AUX0` | 隔离侧低带宽模拟量。 |
| 44 | ADC4 | `ANALOG_AUX1` | 隔离侧低带宽模拟量。 |
| 45 | ADC5 | `ISO_LOAD_CURRENT_MON` | 可选隔离侧负载电流监测；默认可 DNP。 |
| 46 | ADC6 | `ANALOG_SPARE0` | 模拟备用或测试点。 |
| 47 | ADC7 | `ANALOG_SPARE1` | 模拟备用或测试点。 |

模拟约束：

- ADC 只连接 RP2350 所在的 `ISO_GND` / `ISO_3V3` 参考域内信号。
- 所有正常和故障条件下，ADC 输入必须保持在 GND 至 `ADC_AVDD` 范围内。
- 非隔离侧 `12V_IN`、`5V`、`PWR_RETURN` 不得通过分压或保护网络连接到 RP2350 ADC。
- 12 V 入口健康状态由 eFuse、比较器或电源 PG 生成数字信号，再经光耦或数字隔离送入 RP2350 GPIO。
- 隔离侧电流检测如需装配，使用同域电流检测放大器，不直接跨隔离边界连接 ADC。
- ADC 走线远离 QSPI、SPI 时钟、`VREG_LX` 和功率电感。
- RP2350 内部温度传感器不占用 GPIO40..47。

### ADC 前端电路

ADC 用于读取实际电压、电流和温度并记录趋势。过压、欠压、过流等保护不得只
依赖 ADC 和固件，必须由窗口比较器、电流检测比较器或 eFuse 形成独立硬件保护
路径。比较器输出可汇总到 `GPIO34/FAULT_IN`。

| ADC 信号 | 推荐前端 | 约束 |
|---|---|---|
| `BOARD_TEMP_MON` | TMP235 类模拟温度传感器 + 1 kOhm/10 nF RC | 靠近 RP2350 或板内代表性热区，并就近放置 100 nF 电源去耦。 |
| `POWER_TEMP_MON` | TMP235/NTC 温度传感器 + 1 kOhm/10 nF RC | 电气上仍位于 `ISO_GND` 侧；只测隔离 DC/DC、LDO 或 ISO1452 附近温升。 |
| `HW_ID_MON` | 固定电阻分压 + 100 nF | 不同硬件版本装配不同电阻，固件按互不重叠的电压窗口识别。 |
| `ANALOG_AUX0/1` | ESD + 分压/限流 + 低漏电钳位 + RC；高源阻抗时增加轨到轨运放 | 仅允许同隔离域低带宽模拟量；外部接口必须按负压、过压和 ESD 条件设计。 |
| `ISO_LOAD_CURRENT_MON` | ISO 侧分流电阻 + INA180/INA181 + 1 kOhm/10 nF RC | 仅在需要监测隔离侧外设负载时装配；不作为 12 V 入口保护依据。 |
| `ANALOG_SPARE0/1` | 预留串联电阻、对地电容和保护器件焊盘 | 未使用时不得悬空进入周期采样；可配置为数字 GPIO。 |

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
ISO_5V ---- RSHUNT ---- ISO_LOAD
              | |
              +--- INA180/INA181 ---> 1k ---> GPIO45 ADC
                                           |
                                          10nF
                                           |
                                        ISO_GND

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
PIO0: GPIO16..19 SMA input + GPIO24 RJ45 trigger input
PIO1: GPIO20..23 SMA output + GPIO25 RJ45 trigger output
PIO2: GPIO26..29 BiSS/AUX
```

三个 PIO 的业务引脚均位于 GPIO0..31 窗口，可保持 `GPIO_BASE=0` 并同时运行。
任何 GPIO 只能有一个输出驱动源。运行期间禁止切换对应 PIO 的 GPIO Base，也禁止
将已经分配给 PIO 的输出 GPIO 切换到 SIO、PWM 或其他输出功能。

## 原理图检查项

- [ ] 三个按键均具有确定的上电默认电平。
- [ ] 三个状态 LED 不直接加载高速触发网络。
- [ ] TF 卡和 LCD 分别使用 SPI1 和 SPI0，网络名未误合并。
- [ ] SMA 输入和输出均为固定方向，不再保留交叉装配短接风险。
- [ ] 三颗 ISO1452 的 `DE` 独立下拉，`/RE` 经独立 0 Ohm 接地。
- [ ] GPIO40..47 的模拟网络只位于 RP2350 `ISO_GND` 侧，不跨越隔离边界。
- [ ] QSPI、USB、晶振、SWD、RUN 和电源专用引脚未计入普通 GPIO 分配。
