# IO约束

来源文件：`DOC/RaspberryPi-RP2350A小系统板IO引脚分配表.xlsx`

本文档由 `DOC/RaspberryPi-RP2350A小系统板IO引脚分配表.xlsx` 整理生成，用于固件开发时确认 RP2350A 小系统板 IO 占用和使用限制。

## 字段说明

- `引脚编号`：对应 RaspberryPi-RP2350A 小系统板的引脚编号。
- `IO`：对应 RaspberryPi-RP2350A 的 IO 口或电源/专用引脚。
- `连接资源`：板上已经连接的外设、信号或默认功能。
- `完全独立`：`Y` 表示该 IO 可达到完全悬空效果；`N` 表示存在固定连接、专用功能、电源用途或板载外设占用。
- `连接关系说明`：每个 IO 与外设或芯片内部功能的连接关系。
- `使用提示`：开发板使用注意事项。

## 使用约束摘要

- 完全独立 GPIO：`GPIO26_ADC0, GPIO27_ADC1, GPIO27_ADC2, GPIO27_ADC3, GPIO1, GPIO2, GPIO3, GPIO4, GPIO5, GPIO6, GPIO7, GPIO16, GPIO17, GPIO18, GPIO19, GPIO20, GPIO21, GPIO22, GPIO23, GPIO24`
- 非完全独立 GPIO：`GPIO0, GPIO8, GPIO9, GPIO10, GPIO11, GPIO12, GPIO13, GPIO14, GPIO15, GPIO25`
- QSPI、XIN/XOUT、RUN、SWD/SWCLK、USB、电源相关引脚属于专用或非自由资源，固件设计中不要作为普通 GPIO 规划。
- GPIO0/GPIO1 连接 CH343 UART0；如果启用串口下载、日志或通信，需要避免与其他功能冲突。
- GPIO2 连接 KEY0，GPIO3 连接 LED；如作为普通 IO 使用，需要考虑板载按键/LED 电路影响。
- GPIO8/GPIO9/GPIO25 与 LCD 相关；GPIO10 至 GPIO15 与 SD 卡接口相关；如复用为普通 IO，需要确认对应外设未装配或未启用。

## IO资源分配表

| 引脚编号 | IO | 连接资源 | 完全独立 | 连接关系说明 | 使用提示 |
|---:|---|---|:---:|---|---|
| 46 | VREG_AVDD |  | N | 用于内部核心电压调节器的模拟电源，标称电压3.3V | 未引出， 标称电压3.3V |
| 49 | VREG_VIN |  | N | 内部核心电压调节器的电源输入 | 未引出，标称电压2.7V至5.5V |
| 50 | VREG_FB |  | N | 内部核心电压调节器的电压反馈，连接至滤波后的VREG输出 | 未引出 |
| 48 | VREG_LX |  | N | 内部核心电压调节器的开关模式输出,连接至外部电感器 | 未引出，最大电流200 mA，滤波后标称电压1.1V |
| 47 | VREG_PGND |  | N | 内部核心电压调节器的电源地连接，需在外部接地 | 未引出 |
| 60 | QSPI_SS | QSPI_SS / BOOT | N | 非自由IO，作为QSPI接口的片选引脚，连接至板载Flash的CS引脚 | 未引出，用于连接SPI、Dual-SPI或Quad-SPI闪存/PSRAM设备的接口 |
| 57 | QSPI_SD0 | QSPI_SD0 | N | 非自由IO，作为QSPI接口的SD0引脚，连接至板载Flash的MOSI引脚 |  |
| 59 | QSPI_SD1 | QSPI_SD1 | N | 非自由IO，作为QSPI接口的SD1引脚，连接至板载Flash的MISO引脚 |  |
| 58 | QSPI_SD2 | QSPI_SD2 | N | 非自由IO，作为QSPI接口的SD2引脚，连接至板载Flash的WP引脚 |  |
| 55 | QSPI_SD3 | QSPI_SD3 | N | 非自由IO，作为QSPI接口的SD3引脚，连接至板载Flash的HOLD引脚 |  |
| 56 | QSPI_SCLK | QSPI_SCLK | N | 非自由IO，作为QSPI接口的SCLK引脚，连接至板载Flash的CLK引脚 |  |
| 21 | XIN | XIN | N | 非自由IO，用于连接外部12MHz无源晶振 | 未引出 |
| 22 | XOUT | XOUT | N | 非自由IO，用于连接外部12MHz无源晶振 | 未引出 |
| 26 | RUN | RESET | N | 连接复位按键 | 未引出 |
| 24 | SWCLK | SWCLK | N | 接入内部串行线调试多分支总线 | 已引出，提供对两个处理器的调试访问，并可用于代码下载 |
| 25 | SWD | SWDIO | N | 接入内部串行线调试多分支总线 | 已引出 |
| 61 | GND | GND | N | 单一外部接地连接点，连接至RP2350芯片内部多个地焊盘 | 未引出 |
| 40 | GPIO26_ADC0 | GPIO26_ADC0 | Y | 连接芯片的ADC0 | 已引出 |
| 41 | GPIO27_ADC1 | GPIO26_ADC1 | Y | 连接芯片的ADC1 | 已引出 |
| 42 | GPIO27_ADC2 | GPIO26_ADC2 | Y | 连接芯片的ADC2 | 已引出 |
| 43 | GPIO27_ADC3 | GPIO26_ADC3 | Y | 连接芯片的ADC3 | 已引出 |
| 2 | GPIO0 | UART0 TX / GPIO0 | N | 连接芯片的GPIO0与CH343上的UART0 TX引脚 | 已引出，如果不使用串口功能，可当作普通IO使用 |
| 3 | GPIO1 | UART0 RX / GPIO1 | Y | 连接芯片的GPIO1与CH343上的UART0 RX引脚 | 已引出，如果不使用串口功能，可当作普通IO使用 |
| 4 | GPIO2 | KEY0 / GPIO2 | Y | 连接芯片的GPIO2与KEY0按键 | 已引出，如果不使用按键功能，可当作普通IO使用 |
| 5 | GPIO3 | LED / GPIO3 | Y | 连接芯片的GPIO3与LED灯 | 已引出，如果不使用LED功能，可当作普通IO使用 |
| 7 | GPIO4 | GPIO4 | Y | 完全独立 | 已引出 |
| 8 | GPIO5 | GPIO5 | Y | 完全独立 | 已引出 |
| 9 | GPIO6 | GPIO6 | Y | 完全独立 | 已引出 |
| 10 | GPIO7 | GPIO7 | Y | 完全独立 | 已引出 |
| 12 | GPIO8 | LCD_DC / GPIO8 | N | 连接芯片的GPIO8与LCD的RS引脚 | 已引出，如果不使用LCD功能，可当作普通IO使用 |
| 13 | GPIO9 | LCD_CS / GPIO9 | N | 连接芯片的GPIO9与LCD的CS引脚 | 已引出，如果不使用LCD功能，可当作普通IO使用 |
| 14 | GPIO10 | SDIO_SCK / GPIO10 | N | 连接芯片的GPIO10与SD卡模块接口的CLK引脚 | 已引出，如果不使用SD卡功能，可当作普通IO使用 |
| 15 | GPIO11 | SDIO_CMD / GPIO11 | N | 连接芯片的GPIO11与SD卡模块接口的CMD引脚 | 已引出，如果不使用SD卡功能，可当作普通IO使用 |
| 16 | GPIO12 | SDIO_D0 / GPIO12 | N | 连接芯片的GPIO12与SD卡模块接口的D0引脚 | 已引出，如果不使用SD卡功能，可当作普通IO使用 |
| 17 | GPIO13 | SDIO_D1 / GPIO13 | N | 连接芯片的GPIO13与SD卡模块接口的D1引脚 | 已引出，如果不使用SD卡功能，可当作普通IO使用 |
| 18 | GPIO14 | SDIO_D2 / GPIO14 | N | 连接芯片的GPIO14与SD卡模块接口的D2引脚 | 已引出，如果不使用SD卡功能，可当作普通IO使用 |
| 19 | GPIO15 | SDIO_D3 / GPIO15 | N | 连接芯片的GPIO15与SD卡模块接口的D3引脚 | 已引出，如果不使用SD卡功能，可当作普通IO使用 |
| 27 | GPIO16 | GPIO16 | Y | 完全独立 | 已引出 |
| 28 | GPIO17 | GPIO17 | Y | 完全独立 | 已引出 |
| 29 | GPIO18 | GPIO18 | Y | 完全独立 | 已引出 |
| 31 | GPIO19 | GPIO19 | Y | 完全独立 | 已引出 |
| 32 | GPIO20 | GPIO20 | Y | 完全独立 | 已引出 |
| 33 | GPIO21 | GPIO21 | Y | 完全独立 | 已引出 |
| 34 | GPIO22 | GPIO22 | Y | 完全独立 | 已引出 |
| 35 | GPIO23 | GPIO23 | Y | 完全独立 | 已引出 |
| 36 | GPIO24 | GPIO24 | Y | 完全独立 | 已引出 |
| 37 | GPIO25 | LCD_BL / GPIO25 | N | 连接芯片的GPIO25与LCD的BL引脚 | 已引出 |
| 52 | USB_DP | USB_D+ | N | 连接芯片的USB_DP引脚 | 未引出，作为USB控制器，如果不使用USB功能，可当作普通IO使用 |
| 51 | USB_DM | USB_D- | N | 连接芯片的USB_DM引脚 | 未引出，作为USB控制器，如果不使用USB功能，可当作普通IO使用 |
| 39 | DVDD |  | N | 非自由IO，在外部连接至电压调节器输出或外部板级电源供应 | 未引出，标称电压1.1V |
| 23 | DVDD |  | N | 数字核心电源供应，在外部连接至电压调节器输出或外部板级电源供应 | 未引出，标称电压1.1V |
| 6 | DVDD |  | N | 数字核心电源供应，在外部连接至电压调节器输出或外部板级电源供应 | 未引出，标称电压1.1V |
| 54 | QSPI_IOVDD |  | N | QSPI输入/输出端口的电源供应 | 未引出，标称电压1.8V至3.3V |
| 45 | IOVDD |  | N | 数字GPIO的电源供应 | 未引出，标称电压1.8V至3.3V |
| 38 | IOVDD |  | N | 连接摄像头模块接口的D0引脚 | 未引出，标称电压1.8V至3.3V |
| 30 | IOVDD |  | N | 连接摄像头模块接口的D1引脚 | 未引出，标称电压1.8V至3.3V |
| 20 | IOVDD |  | N | 连接摄像头模块接口的D2引脚 | 未引出，标称电压1.8V至3.3V |
| 11 | IOVDD |  | N | 连接摄像头模块接口的D3引脚 | 未引出，标称电压1.8V至3.3V |
| 1 | IOVDD |  | N | 连接摄像头模块接口的D4引脚 | 未引出，标称电压1.8V至3.3V |
| 53 | USB_OTP_VDD |  | N | 内部USB全速PHY及OTP存储器的电源供应 | 未引出，标称电压3.3V |
| 44 | ADC_AVDD |  | N | 模数转换器的电源供应 | 未引出，标称电压3.3V |

## 原表备注

- 引脚编号：对应RaspberryPi-RP2350A小系统板的引脚编号
- IO：对应RaspberryPi-RP2350A的IO口
- 完全独立：指该IO通过一定的方法，可以达到完全悬空的效果（即不接任何其他外设，且不接任何上拉/下拉电阻）
- 连接关系说明：说明每个IO口与外设的连接关系
- 使用提示：介绍每个IO的特点和使用方法，方便大家掌握开发板每一个IO口的使用。
