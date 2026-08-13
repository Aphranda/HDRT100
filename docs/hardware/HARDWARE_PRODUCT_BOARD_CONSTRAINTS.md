# 产品板硬件约束

Status: Draft
Domain: HARDWARE
Canonical: `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`, `docs/hardware/Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel`
Last updated: 2026-08-13

本文档是后续产品板硬件约束的入口。产品板约束由最新产品网表 `Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel`、IO 约束、BOM/Gerber/PickAndPlace 和硬件评审共同派生，冻结隔离、电源域、连接器、保护器件、网表事实、GPIO 分配和可装配选项；HAOFV 只引用本入口，不直接维护具体 pin map。

> 当前硬件事实来源：本文以 `docs/hardware/Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel`
> 为产品板最新网表，以 `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md` 为 RP2350B IO
> 约束明细。若后续 BOM、原理图或网表变更，必须同步刷新本文、board profile 和验证矩阵。

## 约束层级

| 层级 | 文档 | 作用 |
|---|---|---|
| 产品板入口 | 本文档 | 说明产品板约束范围、冻结策略和派生规则。 |
| IO 明细 | `RP2350B_QFN80_IO_CONSTRAINTS.md` | RP2350B QFN-80 GPIO、PIO、ADC、隔离域和专用引脚明细。 |
| 最新网表 | `Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` | 当前产品板约束的最新事实来源。 |
| 生产输出 | BOM、Gerber、PickAndPlace、网表 | 生产与装配依据。 |

## 产品板冻结原则

- GPIO、PIO、DMA、ADC 和连接器 pin map 以硬件域为准。
- GND/FGND、隔离器、ESD/TVS、电源入口、eFuse 和可选 0R/DNP 策略只在硬件域维护。
- 产品板约束可限制软件能力，但不能由软件临时需求绕过隔离或安全边界。
- 任一硬件变更必须同步影响到 board profile、固件 pin map、SCPI 能力字段和验证计划。

## 当前产品板基线

| 项目 | 当前入口 |
|---|---|
| 最新产品网表 | `Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` |
| 历史网表 | `Netlist_Schematic1_2026-08-04.tel` |
| IO 约束 | `RP2350B_QFN80_IO_CONSTRAINTS.md` |
| 硬件输出 | `BOM_*`、`PickAndPlace_*`、`Gerber_*` |

## 主要芯片与器件约束

| 位号 | 器件 | 功能 | 约束 |
|---|---|---|---|
| `U15` | RP2350B QFN-80 | 主 MCU，USB、PIO、ADC、双核和 QSPI master | 固件 pin map 以 `RP2350B_QFN80_IO_CONSTRAINTS.md` 为准；HAOFV 不直接冻结 GPIO。 |
| `U17` | W25Q128JVSIQ | 16 MB QSPI Flash | QSPI SCK/SD0..3/CSn 为专用引脚；BOOTSEL 与 Flash CS 相关，不能作普通 GPIO。 |
| `U19` | CH343P | USB2 转 UART0 调试串口 | 与 RP2350 同 `GND/VDDISO_3V3` 域；USB2 ESD 接 `GND`。 |
| `U21` | MAX3485ESA | UART1 RS485 收发器 | 近端非隔离接口；`UART1_DE` 必须默认接收态。 |
| `U1/U12/U14` | ISO1452BDWR | RS422/BiSS/Trigger 隔离收发器 | 控制侧接 `GND/VDDISO_3V3`，线缆侧接 `FGND/VCC5V`；`DE` 和 `/RE` 默认态必须受控。 |
| `U2/U9` | ISO7740FDBQR | 4 通道数字隔离器 | 用于 SMA PIO 输入/输出隔离；方向固定，不通过软件反转物理方向。 |
| `U3/U4` | SN74LVC2G17 | 控制侧缓冲/整形 | 作为隔离器前后的逻辑整形，供电域必须与所在信号侧一致。 |
| `U29/U30` | SN74LVC2G34 | 控制侧缓冲/驱动 | 用于低延迟逻辑缓冲；不得替代隔离边界。 |
| `U24` | AMC1301DWVR | 12 V 输出电流隔离采样 | 输入侧测 `VCC12V/C_OUT` 分流电阻，输出侧 `BOARD_CUR1` 到 RP2350 ADC；外围不得绕过隔离。 |
| `U23` | TMP235A2DBZR | 板载温度传感器 | 输出到 `BOARD_TEMP1`，低速 ADC 遥测。 |
| `U13` | TPS259241DRCT | 12 V 前端 eFuse / 电子保险丝 | 前端需配合 TVS；FAULT/PGOOD 类状态不得跨域直连 MCU。 |
| `U6` | LMR33630ADDAR | 12 V 到 5 V DC/DC | 输入在 `PWR_IN/VCC12V/FGND` 域，开关节点远离 ADC、USB 和晶振。 |
| `U5` | B0505MT-1WR4 | 5 V 隔离电源 | 提供隔离侧/本地域 5 V 转换链，必须保持 creepage/clearance 与地桥策略一致。 |
| `U8` | TPS7A2033PDBVR | 3.3 V LDO | 生成 `VDDISO_3V3`；供 RP2350、Flash、LCD、CH343、RS485、AMC1301 输出侧等。 |
| `U20/U25` | LM66100DCKT | USB 调试供电理想二极管/电源 ORing | 仅用于 USB 给外部 5 V 调试供电；启用时可能旁路隔离，量产隔离配置应受装配控制。 |
| `Q1` | CSD16411Q3 | 12 V 功率 MOS / 负载开关相关 | 属于功率/FGND 域；栅极控制和故障状态必须按隔离边界检查。 |
| `D16` | SMBJ15A | 12 V 输入 TVS | 接 `PWR_IN -> FGND`，靠近 DC 输入；与 eFuse 耐压和钳位配合评估。 |
| `D9..D14` | ESDS302DBVR | RJ45/BiSS/Trigger 差分 ESD | 接线缆侧 `FGND`，靠近 RJ45。 |
| `D1..D8` | TPD1E05U06DPYR | SMA 触发输入/输出 ESD | 接 `FGND`，靠近 SMA。 |
| `U10/U11/U31..U34` | RCLAMP0521T-ES | USB DP/DM/VBUS ESD | USB1/USB2 保护接 `GND`，靠近 USB-C 连接器。 |
| `CARD1` | TF-01A | TF / microSD 卡座 | SPI1，`TF_CS` 上电默认拉高，`CD_DECT` 极性由固件配置。 |
| `U22` | HS096T01H13 | LCD | SPI0，MOSI/SCK/CS/DC/RST/BL，未使用 MISO。 |
| `USB1/USB2` | Type-C 16pin | USB1 原生 USB，USB2 CH343 调试口 | CC、VBUS 和 ESD 分别归属各自接口；外壳默认接 `GND`/屏蔽策略，不接 `FGND`。 |
| `RF1..RF8` | SMA | 4 路输入、4 路输出触发 | SMA 外壳和 ESD 接 `FGND`；信号经 ISO7740 隔离进入/离开 MCU 域。 |
| `RJ1/RJ2` | RJ45 | BiSS/RS422/Trigger 链路 | 线缆侧 `FGND/VCC12V/VCC5V`；与 MCU 侧只通过隔离器和受控耦合件交互。 |

## 电源域与地域划分

| 网络 | 地域 | 主要对象 | 约束 |
|---|---|---|---|
| `GND` | MCU/USB/本地域 | RP2350、USB1/USB2 ESD、CH343、MAX3485、LCD、TF、Flash、AMC1301 输出侧 | 本地域地；USB/CH343/RS485 无需隔离。 |
| `FGND` | 线缆/功率域 | DC 输入、12 V、SMA/RJ45 外壳、线缆侧 ESD、ISO1452/ISO7740 线缆侧 | 与 MCU 域通过隔离器或单点可控耦合件交互。 |
| `PWR_IN` | 12 V 输入前端 | DC1、TVS D16、TPS259241 输入 | TVS 靠近输入，eFuse 前端浪涌能力需评估。 |
| `VCC12V` | 12 V 受控输出/总线 | RJ45 供电、分流电阻、功率链路 | 当前电流测量通过 AMC1301 隔离采样。 |
| `VCC5V` | 线缆侧/功率侧 5 V | ISO1452、ISO7740 线缆侧、USB 调试供电输出 | 不得默认反灌 USB；调试供电需装配标记。 |
| `VCCISO_5V` | 隔离电源 5 V | B0505MT 输出、TPS7A2033 输入 | 命名保留 ISO，必须结合实际电源路径核对。 |
| `VDDISO_3V3` | MCU 本地域 3.3 V | RP2350、W25Q128、CH343、MAX3485、LCD、TF、AMC1301 输出侧 | 作为 MCU IO/ADC 参考域。 |
| `1V1` | RP2350 内核电源 | RP2350 DVDD/LX 周边 | 按 RP2350 去耦和开关节点布局要求处理。 |

地桥约束：

- `GND` 与 `FGND` 默认保持直流隔离。
- `C37` 是主跨地耦合位置；其他跨地电容若存在，应作为 EMI 调试预留，量产默认需复核。
- 任何 0R 地桥只能作为单点装配选项，安装后系统级隔离不再成立。
- USB 调试供电和 GND-FGND 地桥必须作为互斥装配状态记录。

## RP2350B IO 分配摘要

| GPIO | 网络/功能 | 约束 |
|---:|---|---|
| 0 | `UART0_TX` | CH343P 调试串口 TX。 |
| 1 | `UART0_RX` | CH343P 调试串口 RX。 |
| 2 | `KEY1` | 按键输入，默认态必须确定。 |
| 3 | `LED_SYSTEM` | 系统状态 LED。 |
| 4 | `UART1_TX` | 外部 RS485 TX，连接 MAX3485。 |
| 5 | `UART1_RX` | 外部 RS485 RX，连接 MAX3485。 |
| 6 | `KEY2` | 按键输入，默认态必须确定。 |
| 7 | `KEY3` | 按键输入，默认态必须确定。 |
| 8 | `LED_ARM_TRIGGER` | ARM/触发状态 LED。 |
| 9 | `LED_FAULT` | 故障 LED。 |
| 10 | `TF_SPI1_SCK` | TF SPI1 SCK。 |
| 11 | `TF_SPI1_MOSI` | TF SPI1 MOSI。 |
| 12 | `TF_SPI1_MISO` | TF SPI1 MISO。 |
| 13 | `UART1_DE` | RS485 DE/RE 控制，复位默认接收。 |
| 14 | `TF_CARD_DETECT` | TF 卡检测，极性由固件配置。 |
| 15 | `TF_CS` | TF CS，上电默认高。 |
| 16..19 | `PIO_16..19` -> `SMA_OUT1..4` | PIO1 连续输出，经 ISO7740 到 SMA。 |
| 20..23 | `PIO_20..23` -> `SMA_IN4..1` | PIO0 连续输入，逻辑顺序反序，固件需 bit mapping。 |
| 24 | `BISS_DATA1_IN` | PIO2 输入，经 ISO1452。 |
| 25 | `BISS_CLK1_OUT` | PIO2 输出，经 ISO1452。 |
| 26 | `RJ45_FWD_OUT` | PIO1 输出，经 ISO1452。 |
| 27 | `RJ45_FWD_IN` | PIO0 输入，经 ISO1452。 |
| 28 | `BISS_CLK0_IN` | PIO2 输入，经 ISO1452。 |
| 29 | `BISS_DATA0_OUT` | PIO2 输出，经 ISO1452。 |
| 30..32 | `UP_BISS_DE` / `DN_BISS_DE` / `TRIG_DE` | ISO1452 driver enable，必须独立下拉默认关闭。 |
| 33 | `FAULT_IN` | 电源/保护故障输入，默认态必须确定。 |
| 34..39 | `LCD_RST/BL/DC/CS/SCK/MOSI` | LCD 使用 SPI0，不接 MISO。 |
| 40..42 | `DN_BISS_RE` / `TRIG_RE` / `UP_BISS_RE` | ISO1452 receive enable，默认态确定。 |
| 43 | `BOARD_TEMP1` | TMP235 温度 ADC。 |
| 44 | `BOARD_CUR1` | AMC1301 输出 ADC。 |
| 45..47 | NC / ADC spare | 当前最新网表未连接有效网络，不得周期采样浮空 ADC。 |

完整 GPIO 表以 `RP2350B_QFN80_IO_CONSTRAINTS.md` 为准。上表只作为产品板约束摘要，固件实现必须以 board profile 的机器可读 pin map 生成或校验。

## 接口约束

### USB1 原生 USB

- `USB_DP/USB_DM` 通过 27 Ohm 串联电阻连接 USB1。
- `USB_DP_1/USB_DM_1` ESD 接 `GND`，靠近 USB1。
- CC 下拉接 `GND`；VBUS1 不与 USB2 VBUS 混接。
- USB 连接器外壳默认按 `GND/SHIELD` 策略处理，不接 `FGND`。

### USB2 + CH343P 调试串口

- USB2 数据线只连接 CH343P，不接 RP2350 原生 USB。
- CH343P `UART0_TX/RX` 接 RP2350 UART0。
- USB2 ESD 和 CC 下拉接 `GND`。
- 该接口是近端调试接口，无需隔离。

### UART1 + MAX3485 RS485

- `UART1_TX/RX/DE` 连接 MAX3485。
- RS485 属近端外部设备连接，不做隔离；走线、ESD、端接和共模范围仍需按外部接口设计。
- `DE/RE` 上电默认接收，避免复位阶段驱动总线。

### SMA 触发接口

- 4 路 SMA 输入：`SYNC_TRIG_IN1..4`，经 TPD1E05U06 到 `FGND`，再经 ISO7740 到 PIO 输入。
- 4 路 SMA 输出：`SYNC_TRIG_OUT1..4`，经 ISO7740 输出到线缆侧，再经 TPD1E05U06 到 `FGND`。
- SMA 方向固定，不保留 2I4O/3I3O/4I2O 交叉装配矩阵。
- PIO 输出串联 22 Ohm 阻尼电阻靠近驱动源；输入串阻靠近隔离器/接收输出端。

### RJ45 / BiSS / RS422 / Trigger 链路

- `RJ1/RJ2` 承载 BiSS、Trigger 和 12 V/5 V 相关线缆侧网络。
- 差分 ESD `D9..D14` 接 `FGND` 并靠近 RJ45。
- `ISO1452` 控制侧与 RP2350 同域，线缆侧与 `FGND/VCC5V` 同域。
- `UP_BISS`、`DN_BISS` 和 `TRIG` 三组 `DE`/`/RE` 必须分别控制，不能共用一个方向控制。

### TF 与 LCD

- TF 使用 SPI1：`TF_SCK/MOSI/MISO/TF_CS/CD_DECT`。
- LCD 使用 SPI0：`LCD_SCK/LCD_MOSI/LCD_CS/LCD_DC/LCD_RST/LCD_BL`，不使用 MISO。
- TF 与 LCD 分属两套 SPI，允许独立频率和 DMA；软件资源仲裁仍需管理 SPI/SD/LCD 访问。

## ADC 与测量约束

| ADC 信号 | 前端 | 约束 |
|---|---|---|
| `BOARD_TEMP1` | TMP235A2DBZR | 仅用于低速温度遥测。 |
| `BOARD_CUR1` | AMC1301DWVR 输出 | 只测隔离放大器输出侧同域信号；输入侧分流电阻功耗和量程必须按最大 2 A 与实际 shunt 复核。 |
| GPIO45..47 / ADC5..7 | 当前最新网表未连接有效前端 | 固件默认 disabled，不周期采样。 |

保护和故障判断不得只依赖 ADC。12 V 过压、过流、短路必须由 eFuse、比较器或电源器件提供硬件保护，ADC 只做趋势记录、模块数量估计或报告证据。

## 软件/固件联动约束

- board profile 必须包含产品板版本、网表版本、pin map CRC、IO capability 和装配选项。
- SCPI 只能通过反射内存、命令槽和 owner 状态机表达配置/动作/读取，不能直接操作 GPIO。
- `TRIGger` 域表达产品业务动作；底层 `SEQ/ENC/PCNT/BiSS` 验证能力应收敛到 `REALtime` 或 maintenance/validation 域。
- 产品板 capability 中必须报告 USB1/USB2、CH343、RS485、SMA、RJ45、TF、LCD、CAL/SYNC、ADC 测量链是否装配和可用。
- 任一硬件装配选项改变，如 GND-FGND 0R、USB 调试供电、跨地电容、未装 SMA/RJ45，必须反映到 System Pack、board profile 和验证报告。

## 与调试最小系统板的关系

调试最小系统板用于软件闭环，产品板用于冻结最终边界。两者差异必须通过 board profile 和验证矩阵显式管理，不能让临时调试连接成为产品默认约束。

## 产品板 bring-up 验证矩阵

| 阶段 | 验证项 | 通过标准 |
|---|---|---|
| 上电前 | GND-FGND 阻值、USB VBUS 隔离、12 V 输入短路、3.3 V/1.1 V 阻值 | 与装配选项一致，无非预期短路。 |
| 电源 | `PWR_IN/VCC12V/VCC5V/VDDISO_3V3/1V1` 时序和纹波 | 在负载范围内稳定，eFuse 不误触发。 |
| MCU | SWD、Flash、USB1 枚举、`*IDN?` | 能烧录并查询 DTC100 身份。 |
| 调试口 | USB2/CH343、UART0、RS485 UART1 | 收发稳定，脚本生命周期受控。 |
| 存储/UI | TF mount、System Pack、LCD 刷新 | 不阻塞 SCPI 和 core1 heartbeat。 |
| PIO/SMA | 4 入 4 出物理通道映射 | 每个 SMA 与软件通道一致，GPIO20..23 反序处理正确。 |
| RJ45/BiSS/Trigger | UP/DN/TRIG DE/RE、差分收发、ESD 接地 | 默认态安全，方向控制独立。 |
| ADC | 温度、电流、未用 ADC 状态 | `BOARD_CUR1` 量程合理，未用通道不浮空采样。 |
| 隔离 | ISO1452/ISO7740/AMC1301/B0505MT 边界 | 无未受控跨域直流路径，地桥装配记录一致。 |
| 长稳 | RTOS heap/stack、水位、heartbeat、日志 | 24h 或阶段性压力测试无死锁/异常复位。 |

## 后续待补

- [ ] 基于 `Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` 刷新网表评审和硬件约束。
- [ ] 建立产品板版本号、网表版本、BOM/Gerber 版本和固件 board profile 的对应表。
- [ ] 将本文 IO 摘要固化为 board profile 的机器可读 pin map，并生成 pin map CRC。
- [ ] 把隔离、电源、ESD、连接器和装配选项形成发布冻结 checklist。
