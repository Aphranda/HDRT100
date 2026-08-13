# RP2350B Netlist Review

Status: Active
Domain: Hardware / PCB
Canonical: `docs/hardware/RP2350B_NETLIST_REVIEW_2026-08-04.md`
Related: `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`, `docs/hardware/Netlist_Schematic1_2026-08-04.tel`
Last updated: 2026-08-13
Target: RP2350_TRIG RP2350B QFN-80
Netlist: `docs/hardware/Netlist_Schematic1_2026-08-04.tel`

## 1. 评审结论

当前网表相较 2026-07-27 版本已有明显收敛，可以进入 PCB 布局前的详细检查阶段，但不建议在未闭环本文清单前直接冻结投板。

核心结论：

- USB、CH343、RS485 已与 RP2350 处于同一 `GND / VDDISO_3V3` 本地域，前版跨域直连问题已闭环。
- USB1/USB2 的 CC 与 VBUS 已拆分，避免 Type-C 识别和双口供电互灌问题。
- ISO1452 的 `DE` / `/RE` 默认态较前版改善，`DN_BISS_DE` 下拉地错误已修正。
- U24/AMC1301 的输出侧与 RP2350 ADC 同域，隔离电流采样链路成立。
- 当前主要剩余风险集中在 `GND-FGND` 多点电容耦合、USB 调试供电旁路隔离、U24/AMC1301 输入量程与隔离边界确认、以及固件必须同步新版 PIO pin map。

## 2. 设计域划分

当前网表的地域关系建议按如下方式理解：

| 地域 | 网络 | 主要对象 | 说明 |
|---|---|---|---|
| MCU 本地域 | `GND` | RP2350、USB、CH343、RS485、LCD、TF、U24/AMC1301 输出侧 | USB/调试/本地通信均在此域。 |
| 线缆/功率域 | `FGND` | 12 V 入口、功率级、RS422/BiSS/Trigger 线缆侧 | 与 MCU 域通过隔离器或可选耦合件交互。 |
| MCU 3.3 V | `VDDISO_3V3` | RP2350、Flash、LCD、CH343、RS485、U24 | 命名保留 `ISO`，但功能上是 MCU 本地域 3.3 V。 |
| 线缆侧 5 V | `VCC5V` | ISO1452 线缆侧、USB 调试供电输出 | 调试供电时可能旁路隔离。 |

建议在原理图注释或文档中明确：

```text
GND  = MCU/USB/RS485 local ground
FGND = field/power/cable-side ground
```

## 3. 已闭环问题

### 3.1 USB-C 双口拆分

当前网表中：

```text
U1_CC1 / U1_CC2 -> USB1
U2_CC1 / U2_CC2 -> USB2
VBUS1           -> USB1
VBUS2           -> USB2 / CH343
```

结论：通过。两个 Type-C 口不再共用 CC 与 VBUS，方向正确。

注意事项：

- CC 下拉电阻必须接 `GND`。
- USB D+/D-/CC/VBUS 的 ESD 均接 `GND`，靠近连接器。
- USB 连接器外壳若需要独立处理，建议使用 `CHASSIS/SHIELD` 概念，不要接到 RS422 线缆侧地。

### 3.2 CH343 / RS485 本地域

当前 `U19/CH343`、`U21/MAX3485`、`U15/RP2350` 均参考 `GND`，且供电在 `VDDISO_3V3` 或同域电源内。

结论：通过。USB/CH343/RS485 作为近端非隔离接口是合理的。

### 3.3 ISO1452 控制默认态

当前 `/RE` 增加下拉：

```text
DN_BISS_RE -> R7  -> GND
UP_BISS_RE -> R16 -> GND
TRIG_RE    -> R17 -> GND
```

`DN_BISS_DE` 的 R44 也已接到 `GND`，不再接 `FGND`。

结论：通过。复位和启动阶段默认态更可控。

仍需检查：

- 三路 `DE` 是否均为低有效关闭驱动，且固件初始化完成前不误使能。
- `/RE` 的逻辑极性是否与 ISO1452 数据手册一致；若低有效接收，则 10 kOhm 下拉表示默认接收。

## 4. 重点风险与建议

### 4.1 GND-FGND 跨接电容数量偏多

当前跨 `GND-FGND` 的器件包括：

```text
C37, C66, C67, C68, C69, C78, C84
```

新增 `C66/C67/C68/C69/C78/C84` 均为 220 nF，全部跨在 `FGND-GND`。

风险：

- 多个分散跨地电容会形成多条高频回流路径，削弱隔离边界的可预测性。
- 如果这些位置后续被改成多个 0R，会形成多点直流短接和地环路。
- 220 nF 对两地交流耦合较强，更接近把两个地域在高频上大面积绑在一起。

建议：

- 只定义一个主跨接位置，支持 `C / 0R / DNP` 三选一。
- 其他跨地位置作为 EMI 调试预留，量产默认 DNP。
- 若目标仍是系统隔离，默认不要装 0R。
- 若装跨地电容，优先从 `1 nF ~ 4.7 nF` 高耐压器件开始做 EMI 验证；220 nF 作为强耦合调试选项，不建议默认全贴。

推荐装配策略：

| 配置 | GND-FGND 主跨接 | 其他跨地电容 | 用途 |
|---|---|---|---|
| 隔离量产 | DNP 或小电容 | DNP | 保持隔离边界。 |
| EMI 调试 | 1 nF / 4.7 nF / 220 nF 试验 | 单点试装 | 评估辐射和抗扰。 |
| 非隔离调试 | 单点 0R | DNP | 明确旁路隔离。 |

### 4.2 USB 调试供电会旁路隔离

U20/U25 是理想保险丝，用于 USB 给外部 `VCC5V` 调试供电。

结论：设计意图可接受，但必须作为调试装配选项管理。

要求：

- U20/U25 必须具备反向截止，避免 `VCC5V` 倒灌到 `VBUS1/VBUS2`。
- USB 调试供电时必须有回流路径；若通过地桥连接 `GND-FGND`，系统隔离不再成立。
- 量产隔离配置应默认不启用 USB 给外部 5 V 供电。
- 丝印和装配记录建议标注：`USB DEBUG POWER - ISOLATION BYPASS`。

### 4.3 U24/AMC1301 电流采样链路需确认输入量程

当前 U24：

```text
VS   -> VDDISO_3V3
GND  -> GND
OUT  -> BOARD_CUR1 -> RP2350 ADC
IN+  -> VCC12V
IN-  -> C_OUT
```

结论：AMC1301 输出侧成立；ADC 看到的是 MCU `GND` 域信号。

必须确认：

- `VCC12V/C_OUT` 差分输入是否始终落在 AMC1301 允许输入范围内。
- AMC1301 输入侧参考 `FGND`，输出侧参考 `GND`，隔离边界不能被外围 RC、保护或测试点绕过。

### 4.4 PIO pin map 已改为实际布线版本

当前实际映射：

```text
GPIO16 -> SMA_OUT1
GPIO17 -> SMA_OUT2
GPIO18 -> SMA_OUT3
GPIO19 -> SMA_OUT4

GPIO20 -> SMA_IN4
GPIO21 -> SMA_IN3
GPIO22 -> SMA_IN2
GPIO23 -> SMA_IN1

GPIO24 -> BISS_DATA1_IN
GPIO25 -> BISS_CLK1_OUT
GPIO26 -> RJ45_FWD_TRIG_OUT
GPIO27 -> RJ45_FWD_TRIG_IN
GPIO28 -> BISS_CLK0_IN
GPIO29 -> BISS_DATA0_OUT
```

结论：硬件可接受，但固件必须以新版 IO 约束为准。

固件要求：

- `GPIO20..23` 输入逻辑顺序为 `IN4, IN3, IN2, IN1`，不能按旧表直接解释。
- PIO state machine 的 `in_base/out_base/set_base/sideset_base`、mask、persona 表必须同步更新。
- 测试程序需要覆盖每个 SMA/RJ45/BiSS 通道的物理端口到软件通道映射。

### 4.5 BOARD_CUR2 / ANA_SPARE 单针网

单针网：

```text
BOARD_CUR2
ANA_SPARE0
ANA_SPARE1
```

结论：可接受。

约束：

- `BOARD_CUR2` 当前未连接硬件前端，不能作为有效电流采样通道。
- 固件不得周期性采样浮空 ADC。
- 未用 ADC/GPIO 应初始化为确定状态，例如输入下拉或模拟禁用。

## 5. 电源和防护建议

### 5.1 12 V 输入 TVS

当前 `D16` 已接在：

```text
PWR_IN -> FGND
```

结论：方向正确，TVS 应靠近 DC 输入连接器。

注意：

- TVS 选型需保证正常 12 V 输入范围内不误导通。
- 钳位电压需低于 eFuse 和后级器件可承受范围。
- TPS259241 一类低耐压 eFuse 前端不建议使用动作过晚的 18 V TVS 作为唯一保护。

### 5.2 USB ESD

USB1/USB2 的 D+/D- 已加入保护器件网络：

```text
USB_DM_1 / USB_DP_1
CH343_DM_1 / CH343_DP_1
```

建议：

- USB ESD 参考 `GND`，靠近 USB 连接器。
- USB1 与 USB2 的 VBUS 保护和限流路径保持独立。

## 6. 投板前检查清单

- [ ] `GND-FGND` 仅保留一个明确的 0R/DNP 主地桥；其他跨地电容默认装配状态已定义。
- [ ] `C37/C66/C67/C68/C69/C78/C84` 的 BOM 属性明确为 DNP、EMI 调试或量产贴装，不能默认全贴 220 nF。
- [ ] U20/U25 USB 调试供电的默认装配状态已定义；隔离量产配置默认不旁路隔离。
- [ ] U20/U25 已确认具备反向截止，USB1/USB2 不会互相倒灌。
- [ ] U24/AMC1301 被测 `VCC12V/C_OUT` 差分输入量程和隔离边界已确认。
- [ ] 三颗 ISO1452 的 `DE` 和 `/RE` 上电默认态已按极性验证。
- [ ] `DN_BISS_DE`、`UP_BISS_DE`、`TRIG_DE` 的下拉均接 RP2350 控制侧 `GND`。
- [ ] 固件 pin map 已同步 `GPIO16..29` 实际映射，尤其是 `GPIO20..23` 输入反序。
- [ ] `BOARD_CUR2` 当前不参与周期 ADC 采样，未用 ADC 状态由固件明确初始化。
- [ ] USB CC/VBUS/D+/D- 的 ESD 和回流路径全部参考 `GND`。
- [ ] RS422/BiSS/Trigger 线缆侧 TVS 参考 `FGND`，不跨到 `GND`。
- [ ] 22 Ohm 串联阻尼按驱动源放置：PIO 输出靠 RP2350，PIO 输入靠隔离器/接收器输出。

## 7. 评审判定

判定：有条件通过。

进入 PCB 布局前必须闭环：

1. 定义 `GND-FGND` 跨接件的默认装配策略。
2. 明确 USB 调试供电是否默认 DNP，以及其隔离旁路标识。
3. 确认 U24/AMC1301 外部电流检测输入量程和隔离边界。
4. 固件和测试文档同步新版 PIO pin map。

闭环后，本版网表可以作为 PCB 布局输入。
