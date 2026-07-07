# BiSS-C 协议触发节点 HAOFV 方案

本文档定义 `PROTOCOL_TRIGGER` 的第一种协议子类型：`TRIG_PROTOCOL_BISS_C`。BiSS-C 节点复用 AUX0..AUX3，不改底层硬件，用于三类能力：

- 透明透传 BiSS-C 原链路，同时监听编码器位置，并按位置阈值输出触发脉冲。
- 板间传输轻量过程数据，例如位置、事件计数、状态和校准帧。
- 在后续慢速阶段执行四板自校准，生成正式测试使用的延迟 offset。

P0 的正式测试目标收敛为：

```text
5 MHz BiSS-C
fixed position profile
PIO 最小可靠接收器：采样相位 + 帧锚点 + position/status 抽取
PIO/硬件透传：CLK_IN -> CLK_OUT，DATA_IN -> DATA_OUT，不改写 BiSS-C 帧
IRQ 快路径 crossing compare
PIO 输出 TRIG_OUT
```

## HAOFV 定位

BiSS-C 不作为独立系统架构存在，而是纳入 HAOFV 的 Trigger 域：

```text
SCPI / UI / SD profile
  -> TriggerAO
  -> TriggerFB ECC
  -> TriggerVector / BiSS profile
  -> sync_io / biss_node_io
  -> PIO / DMA / IRQ hard real-time path
```

职责边界：

| 层 | 责任 | 禁止事项 |
|---|---|---|
| SCPI/UI/SD | 投递配置、自校准、查询事件。 | 不直接操作 PIO/DMA，不进入每帧触发路径。 |
| TriggerAO | Trigger 域事件队列、执行预算、快照发布。 | 不逐 bit 解码，不做精确定时输出。 |
| TriggerFB | ECC 状态迁移、资源仲裁、profile 冻结、统计提交。 | 不参与 `FAST_RT_TEST` 每帧比较。 |
| TriggerVector | 保存当前事实：模式、role、profile、target、offset、统计。 | 不保存大块原始帧流。 |
| biss_protocol | 纯协议工具：配置校验、CRC、pack/parse。 | 不访问 GPIO/PIO/DMA/SCPI。 |
| biss_node_io | PIO/SM/DMA/IRQ 原语：解帧、字段抽取、FIFO、脉冲。 | 不做管理面状态机。 |
| PIO/DMA/IRQ | 硬实时：采样、移位、固定字段抽取、候选触发、输出脉冲。 | 不访问 SD/UI/日志，不做复杂动态协议。 |

硬实时原则：

- ARM 后，正式测试路径不得经过 SCPI/UI/SD/普通任务。
- 固定延迟可以校准，jitter 必须通过 PIO/IRQ 路径控制。
- 慢速配置和自校准必须在 DISARM 或 `SLOW_CTRL_SYNC` 阶段完成。

## 模式和角色

新增通用模式：

```c
TRIG_MODE_PROTOCOL_TRIGGER = 3
TRIG_MODE_BISS_BRIDGE = TRIG_MODE_PROTOCOL_TRIGGER  /* 兼容别名 */
TRIG_PROTOCOL_BISS_C = 0
```

BiSS-C role。数值必须与 `trigger_vector.h` 和 SD/profile 持久化保持一致：

| Value | Role | 行为 | 典型用途 |
|---:|---|---|---|
| `0` | `TAP_MONITOR` | 串联在原链路中，原样透传 `CLK/DATA`，同时旁路监听 position/status；不得主动改写帧。 | P0：透明 TAP bridge，监听编码器位置并输出触发脉冲。 |
| `1` | `SLAVE_TX` | 等上游 `CLK_IN`，返回本地事件/状态。 | P1：`RX_PULSE -> TX_BISS`。 |
| `2` | `MASTER_RX` | 本板输出 `CLK_OUT`，读取编码器或远端节点。 | P1：独立读取编码器或双板闭环。 |
| `3` | `BRIDGE_PROXY` | 显式代理桥，可转发/替换帧。 | P2：代理桥，P0 不改写帧。 |

`SELF_CAL_RING` 不作为 `trig_biss_role_t` 的固定枚举值进入 P0。它是 `SLOW_CTRL_SYNC`
下的校准子模式，后续可由独立 cal mode 或 `biss_phase` 表示，避免破坏 role 数值兼容性。

## AUX 引脚复用

底层硬件不变，BiSS-C 作为 AUX 的协议触发功能：

| AUX | GPIO | BiSS-C 逻辑 | 方向 | 说明 |
|---|---:|---|---|---|
| `AUX0` | 26 | `BISS_CLK_IN` | 输入 | TAP/SLAVE 监听上游时钟；P1 SELF_CAL 用作校准输入。 |
| `AUX1` | 27 | `BISS_DATA_IN` | 输入 | TAP/MASTER 采样下游数据。 |
| `AUX2` | 28 | `BISS_CLK_OUT` | 输出 | TAP 透传 `CLK_IN`；MASTER/BRIDGE 输出时钟。 |
| `AUX3` | 29 | `BISS_DATA_OUT` | 输出 | TAP 透传 `DATA_IN`；SLAVE/BRIDGE 返回数据或转发；P1 SELF_CAL 用作校准输出。 |

P0 `TAP_MONITOR` 是串联式透明桥，而不是只并联高阻探针：

```text
上游主站 CLK/MA  -> AUX0/BISS_CLK_IN  -> AUX2/BISS_CLK_OUT  -> 下游编码器
下游编码器 DATA/SLO -> AUX1/BISS_DATA_IN -> AUX3/BISS_DATA_OUT -> 上游主站
```

透传规则：

- `AUX2` 必须原样转发 `AUX0` 接收到的 `CLK/MA`。
- `AUX3` 必须原样转发 `AUX1` 接收到的 `DATA/SLO`。
- 本地 position/status 解码只从 `AUX0/AUX1` 旁路采样，不允许改变 `AUX2/AUX3` 上的帧内容。
- 透传路径应优先用硬件直通或 PIO 固定延迟转发实现，延迟和 skew 必须纳入 latency offset 校准。

主触发口保持：

| 信号 | GPIO | 用途 |
|---|---:|---|
| `TRIG_IN` | 16 | 本地脉冲输入或测试 strobe。 |
| `TRIG_OUT` | 20 | BiSS 解析后输出触发脉冲。 |

ARM 前资源检查：

- AUX0..AUX3 未被 ARM/EXT/SYNC/MARKER、差分触发、校准或其他 AUX owner 占用。
- TAP 模式只允许透明透传 `CLK_OUT/DATA_OUT`，禁止由管理面或本地协议栈改写 `DATA_OUT`。
- MASTER_RX 必须确认外部没有其他主站驱动 `CLK`。
- 硬件冻结后 `TRIG:ENC:APIN 26` 已关闭；增量编码器固定走 `SYNC_IO`
  `GPIO16/17/19`，不再占用 AUX。

## RS-422 物理层候选器件

`DOC\外围硬件手册` 中的 `AM26LV31E` / `AM26LV32E` 可以作为当前 BiSS-C
点对点差分物理层候选。两者都是 3.3 V、TIA/EIA-422-B / ITU V.11 级别器件，
适合固定方向的 RS-422-like 链路；它们不是半双工 RS-485 收发一体器件，因此不用于
共享总线仲裁或多主驱动。

| 器件 | 角色 | 手册关键信息 | 本项目用途 |
|---|---|---|---|
| `AM26LV32E` | 四路差分接收器 | 3.0..3.6 V 供电；最高 32 MHz；差分阈值约 ±200 mV；共模输入范围 ±7 V；开路 fail-safe 输出高；输出 VOH/VOL 兼容 3.3 V GPIO；传播延迟典型 16 ns、最大 26 ns。 | P0 TAP 的 `BISS_CLK_IN` / `BISS_DATA_IN` 差分转单端输入；P1 MASTER_RX 的 DATA 接收。 |
| `AM26LV31E` | 四路差分驱动器 | 3.0..3.6 V 供电；最高 32 MHz；输入 VIH=2 V、VIL=0.8 V，兼容 RP2350 3.3 V GPIO；100 ohm 负载差分输出典型 2.6 V；传播延迟典型 8 ns、最大 12 ns；短路保护。 | P1 `BISS_CLK_OUT` / `BISS_DATA_OUT` 单端转差分输出；MASTER/SLAVE/BRIDGE 固定方向驱动。 |

推荐连接：

- `BISS_CLK_IN+/-`、`BISS_DATA_IN+/-` 在本板接收端进入 `AM26LV32E`，再接
  `GPIO26` / `GPIO27`。
- `GPIO28` / `GPIO29` 进入 `AM26LV31E`，再输出 `BISS_CLK_OUT+/-` /
  `BISS_DATA_OUT+/-`。
- P0 串联 TAP bridge 需要装配接收和转发路径：`CLK_IN` 原样转发到 `CLK_OUT`，
  `DATA_IN` 原样转发到 `DATA_OUT`；不得让本地协议栈主动改写原链路 `CLK/DATA`。
- P1/P2 若需要透明桥或代理桥，必须用明确的方向控制和原理图隔离，避免两个 driver
  同时驱动同一对线。

硬件设计约束：

- 每组差分对按线缆阻抗在接收端放置终端电阻，优先预留 100 ohm / 120 ohm 可选位。
- `AM26LV32E` 只声明 open-circuit fail-safe；断线、短路、空闲偏置和热插拔场景仍需预留
  外部 bias / bypass 电阻位，并用示波器确认 idle 状态不会误触发。
- 长线或跨设备连接应评估 TVS、共模扼流圈、数字隔离和隔离电源；不能只依赖器件
  bus-pin ESD 指标覆盖系统级 EMC。
- 31E + 32E 的典型单向传播延迟约 24 ns，最大约 38 ns，5 MHz bit cell 为 200 ns；
  对 P0/P1 5 MHz 固定 profile 有可接受的初始余量，但 sample window、skew、线缆延迟和
  温漂必须通过 1 MHz bring-up、5 MHz 示波器和逻辑发生器回放实测后冻结。
- 若未来需要 20 MHz 以上复杂 profile、长线缆或更严格 jitter，应重新评估更高速 RS-422 /
  LVDS 器件或 FPGA/CPLD/专用 BiSS 接口芯片。

## BiSS-C 协议边界

公开协议和常见实现的共性：

- BiSS-C 是同步主从协议，主站输出 `MA/CLK`，从站返回 `SLO/DATA`。
- 空闲时 `CLK/DATA` 为高电平。
- 一帧包含 `Idle -> LAT -> ACK -> STR -> CDS -> DATA -> CRC -> STP -> Timeout`。
- Header/control 位不是业务 payload。
- 过程数据通常 MSB first，可包含 position、ERR、WRN、CRC。
- CRC 位发送前取反。
- Timeout 是帧边界和错误恢复机制。

## 标准 BiSS-C 实现方法

本项目的 BiSS-C 不能按“普通 SPI/UART 字节流”实现，必须按标准同步串行传感器接口处理。软件结构采用三层：

```text
BiSS line timing
  -> BiSS frame state machine
  -> process data profile parser
```

GitHub 示例交叉检查结论：

- `Hello-FPGA/BISS-C` 的 FPGA IP 使用显式状态机接收 start/zero/position/error/warning/CRC，
  并提供 MA/SLO 延迟测量和采样时钟平移配置。
- `meetwit/reg_bissc_crc6` 使用 MCU 串行外设读取单编码器，但仍需要按首字节对齐拆包，
  并用 CRC6 查表计算取反后的校验值。
- 因此 P0 不能只做“固定 bit 数移位”。P0 的最小可靠接收器必须包含：
  采样相位参数、帧锚点校验、position/status 抽取、CRC/ERR/WARN 策略和 timeout 恢复。

### Line Timing

标准实现中的底层时序原则：

- `MA/CLK` 由主站产生；从站不能主动发送帧，只能在主站 clock polling 时驱动 `SLO/DATA`。
- `TAP_MONITOR` 只能采样 `CLK_IN/DATA_IN`，不得驱动原链路的 `CLK` 或 `DATA`。
- `MASTER_RX` 必须确认链路上没有其他主站驱动 `MA/CLK`。
- 从站侧通常在一个时钟边沿更新 `SLO/DATA`，主站侧在相反边沿或稳定窗口采样。PIO 实现必须把采样点放在 bit cell 中间附近，而不是贴边沿采样。
- 一帧结束依赖 BiSS timeout 恢复空闲，不应只靠固定 bit 数强行认为下一 bit 是新帧。

在 RP2350 上，`5 MHz` 时每 bit 有 `50` 个 PIO cycle。P0 固定 decoder 的推荐采样模型：

```text
wait CLK edge
delay sample_delay_cycles
sample DATA
advance frame_state / bit_index
```

采样相位是 P0 bring-up 的显式参数，而不是隐藏常量：

```text
sample_edge              rising/falling
sample_delay_cycles      0..49 @ 5 MHz, clk_sys=250 MHz
sample_delay_scan_enable bring-up only
line_delay_ticks         measured MA/SLO or CLK/DATA relative delay
```

`TAP_MONITOR` 没有能力改变上游 `CLK`，只能选择采样边沿和延迟窗口。1 MHz bring-up
必须先扫描并记录稳定窗口，再切到 5 MHz；如果稳定窗口小于工程阈值，应拒绝 ARM 或进入
FAULT，而不是继续按默认延迟触发。

### Frame State Machine

标准 BiSS-C 接收端按帧状态机实现，而不是直接把全帧当成 position：

```text
WAIT_IDLE_HIGH
  -> WAIT_FRAME_START
  -> LAT
  -> ACK
  -> STR
  -> CDS
  -> PROCESS_DATA
  -> CRC
  -> STP
  -> WAIT_TIMEOUT
```

各状态的项目内处理：

| 状态 | 标准含义 | P0 处理 |
|---|---|---|
| `WAIT_IDLE_HIGH` | 等待链路空闲和 timeout 后恢复。 | PIO/IRQ 清 frame tag，准备下一帧。 |
| `LAT` | 传感器锁存测量值的阶段。 | TAP 只跳过；MASTER 负责产生对应 clock。 |
| `ACK` | 从站响应。 | 检查是否出现有效响应，异常计 timeout/frame error。 |
| `STR` | start 位，标记数据段开始。 | 用作字段对齐锚点。 |
| `CDS` | control data slave bit。 | P0 只跳过或记录；慢速阶段可用于控制通道。 |
| `PROCESS_DATA` | 过程数据，含 position/status。 | P0 只抽取固定 `position_offset/position_bits`。 |
| `CRC` | 过程数据 CRC。 | P0 可统计/慢速复核；P1 再决定是否阻塞输出。 |
| `STP` | stop/end bit。 | 记录 frame complete。 |
| `WAIT_TIMEOUT` | 帧间 timeout，恢复 idle。 | 作为下一帧边界，检测 timeout storm。 |

P0 的 PIO 不做完整可变状态机配置，而是把上述状态机在 ARM 前编译为固定 profile 参数。
固定 profile 不等于裸跳位；它仍必须检查最小帧锚点，避免一次毛刺后把 CRC/status 位误当成 position：

```text
skip_bits_before_position
position_bits
skip_bits_after_position
crc_bits
timeout_us
anchor_start_expected
anchor_zero_or_ack_expected
status_gate_policy
```

这样与标准帧结构一致，同时避免在 5 MHz hot path 中做 EDS/Profile 识别。若锚点不匹配，
PIO/IRQ 必须丢弃该帧并递增 frame_error/timeout 统计，不允许进入 crossing compare。

### Process Data Profile

标准 BiSS-C 的过程数据不是固定一种编码器格式。不同编码器可能改变：

- position 位宽。
- position 在 process data 内的偏移。
- ERR/WRN 位是否存在及位置。
- CRC 位宽、初值、多项式和覆盖范围。
- 是否存在多从站级联或附加 process data。

因此项目内 profile 必须显式配置，禁止 P0 依赖“默认 32bit position + 6bit CRC”的隐含假设。P0 固定 position profile 至少需要：

```text
frame_bits
position_offset
position_bits
status_offset
status_bits
crc_offset
crc_bits
crc_cover_offset
crc_cover_bits
error_bit_offset or disabled
warning_bit_offset or disabled
sample_edge
sample_delay_cycles
timeout_us
```

### CRC Strategy

BiSS-C 常见 position profile 使用 CRC6，且 CRC 字段在线上发送的是取反后的 CRC。项目实现规则：

- `biss_protocol` 提供 profile 化 CRC：polynomial、init、xor/invert、bit order、覆盖起点和覆盖长度都来自 profile。
- 单元测试必须包含 golden vector，避免 PIO/ARM 位序理解错误。
- `FAST_RT_TEST` P0 默认不把完整 CRC 计算放进触发 hot path；可先输出触发，同时把 CRC late check 计入统计。
- P0 必须明确风险：late CRC 模式可能在 position 损坏但尚未发现 CRC 错误时误触发。
- P0 默认至少支持 `ERR/WRN gate`：若 profile 中存在错误位或告警位，可配置为禁止触发、只统计或忽略。
- 如果产品要求“CRC 错误禁止触发”，需要升级为 P1，并明确由 PIO 轻量 CRC、IRQ 快速 CRC 或外部逻辑承担；也可以选择把触发决策延后到 CRC 字段结束之后，接受固定 latency 增加。

### Control Channel

BiSS-C 除 cyclic process data 外，还有低速控制/寄存器能力。标准做法是通过每帧的控制位跨多个周期传输控制信息。项目内分配：

- `SLOW_CTRL_SYNC`：允许使用控制通道做配置、识别、诊断和校准。
- `FAST_RT_TEST`：不使用控制通道修改任何实时参数，只允许 process data 位置触发。

这条边界用于保证 HAOFV 的实时性：正式测试期间，SCPI/UI/SD/控制通道都不能进入每帧触发决策。

P0 不实现：

- 完整 BiSS 寄存器通信。
- EDS/Profile 自动识别。
- Safety Profile。
- 100 MHz BiSS-C。
- 多主 RS485 总线仲裁。

参考：

- BiSS Association, `BiSS C Protocol Description Rev D2`: https://biss-interface.com/download/biss-c-protocol-description-english/
- BiSS Interface overview: https://en.wikipedia.org/wiki/BiSS_interface
- Hello-FPGA `BISS-C` FPGA IP example: https://github.com/Hello-FPGA/BISS-C
- meetwit `reg_bissc_crc6` MCU CRC6 example: https://github.com/meetwit/reg_bissc_crc6

## 运行阶段

### SLOW_CTRL_SYNC

用于上电识别、配置、同步、自校准、诊断。

允许：

- SCPI/UI/SD profile 参与。
- TriggerFB 修改 BiSS profile。
- 低速 BiSS/control 帧。
- 线延迟测量、CRC 统计清零、offset 写入。

禁止：

- 输出正式测试触发。
- 将慢速配置逻辑混入 `FAST_RT_TEST` 每帧 hot path。

### FAST_RT_TEST

正式测试模式。P0 只承诺 fixed position profile：

```text
CLK/DATA
  -> PIO fixed profile receiver
       sample phase
       frame anchor check
       position/status extract
  -> RX FIFO: position/status/frame_tag
  -> IRQ fast crossing compare
  -> TRIG_OUT PIO pulse
```

目标参数：

| 参数 | P0 目标 |
|---|---:|
| BiSS clock | `5 MHz` |
| bring-up clock | `1 MHz` |
| position bits | profile 固定，典型 24/32 bit |
| sample phase | bring-up 扫描并冻结 |
| frame anchor | P0 必须校验最小 start/ack/zero 锚点 |
| output path | PIO decode + IRQ compare + PIO pulse |
| latency offset | 可测、可查询、可校准 |
| P99 jitter | `< 5 us` 首版目标 |

转台 `3 deg/s`、`15 us` 延迟对应：

```text
angle_offset = 3 deg/s * 15 us = 0.000045 deg = 0.162 arcsec
```

固定 offset 写入校准参数。jitter 按下式评估：

```text
angle_jitter_deg = 3 deg/s * latency_jitter_s
```

### 状态流

```text
IDLE
  -> SLOW_CTRL_SYNC
  -> BISS_CONFIGURED
  -> BISS_ARMED
  -> FAST_RT_TEST
  -> DISARM
  -> SLOW_CTRL_SYNC / IDLE
```

规则：

- `BISS_ARMED/FAST_RT_TEST` 下禁止修改 clock/profile/target/offset/pin/role。
- 修改实时参数必须先 DISARM。
- FAST 前必须冻结 PIO 所需字段偏移、位宽、CRC 策略和 target。
- profile mismatch、timeout storm、资源冲突进入 FAULT 或自动 DISARM。

## P0 主路径：位置解码触发

P0 只做固定 profile 下的位置字段解码和阈值触发，不做通用协议栈。

PIO 职责：

- 监听 `CLK_IN` 边沿。
- 按冻结的 `sample_edge/sample_delay_cycles` 采样 `DATA_IN`。
- 检查最小帧锚点，例如 ACK/STR/zero 或 profile 指定的 start pattern。
- 按固定 offset/bit width 抽取 `position`。
- 抽取 ERR/WRN 或 profile 指定的 status 位。
- 推入 RX FIFO。
- 记录基础 frame/status/error tag。

IRQ 快路径职责：

```c
if (frame_ok(frame_tag) &&
    status_gate_allows(status) &&
    crossed_position(last_position, position, target, modulo)) {
    sync_io_fire_pulse_cycles(width_cycles);
}
last_position = position;
```

必须处理：

- crossing 判断，避免过阈值后每帧重复触发。
- modulo 回绕，例如 359.999 deg -> 0 deg。
- armed window 或 one-shot 策略。
- 锚点错误、ERR/WRN gate、CRC late check 统计。
- late/overflow/timeout 统计。

不放入 P0 hot path：

- 任意 CRC 完整实时计算。
- 任意 profile 自动识别。
- 任意复杂表达式比较。
- SD/日志/SCPI 查询。

## PIO + DMA 预算

`clk_sys=250 MHz` 时：

| BiSS clock | PIO cycles/bit | 判断 |
|---:|---:|---|
| `5 MHz` | 50 | P0 正式目标，足够 fixed profile receiver。 |
| `10 MHz` | 25 | P1 提速目标，需要实测 receiver + CRC/status 策略。 |
| `20 MHz` | 12.5 | 紧张，适合 raw capture 或极薄 decoder。 |
| `50 MHz` | 5 | 只能做简单同步移位/转发。 |
| `100 MHz` | 2.5 | 不适合完整 BiSS-C 解码。 |

DMA 只负责搬运 FIFO/RAM，不负责 CRC、比较、profile 解析。P0 不追求“PIO+DMA 直接非常高速”，先把 `5 MHz FAST_RT_TEST` 做稳。

## Process Data Profile

### Encoder Position Profile

正式测试优先支持：

| 字段 | P0/P1 | 说明 |
|---|---|---|
| `frame_bits` | P0 | 固定 profile 帧长上限，用于 FIFO/tag 校验。 |
| `position_offset` | P0 | process data 内位置字段起始 bit。 |
| `position_bits` | P0 | 典型 24/32 bit。 |
| `modulo` | P0 | 位置计数回绕值。 |
| `anchor_offset` / `anchor_mask` / `anchor_value` | P0 | 最小帧锚点校验，防止错位触发。 |
| `sample_edge` | P0 | 采样边沿。 |
| `sample_delay_cycles` | P0 | 采样延迟，bring-up 后冻结。 |
| `error_bit_offset` | P0 | 可选 ERR 位，disabled 表示不存在。 |
| `warning_bit_offset` | P0 | 可选 WRN 位，disabled 表示不存在。 |
| `status_gate_policy` | P0 | `IGNORE` / `COUNT_ONLY` / `BLOCK_TRIGGER`。 |
| `crc_offset` | P0 | CRC 字段位置。 |
| `crc_bits` | P0 | 常见 6，P0 可 late check。 |
| `crc_cover_offset` / `crc_cover_bits` | P0 | CRC 覆盖范围，通常覆盖 position/status。 |
| `crc_polynomial` / `crc_init` / `crc_xor` | P0 | `biss_protocol` 软件计算参数。 |
| `crc_gate_policy` | P1 | `LATE_COUNT` / `BLOCK_TRIGGER`；P0 默认 `LATE_COUNT`。 |

### Event Profile

板间事件可后续支持：

| 字段 | 位宽 | 说明 |
|---|---:|---|
| `profile_id` | 4 | `0x1`。 |
| `seq_id` | 8 | 丢帧/重复帧检测。 |
| `event_count` | 24 | 脉冲累计计数。 |
| `status` | 4 | ready/armed/overflow/fault。 |
| `crc6` | 6 | BiSS-C 风格 CRC6，发送取反。 |

### Self-Cal Profile

自校准短帧：

| 字段 | 位宽 | 说明 |
|---|---:|---|
| `profile_id` | 4 | `0xC`。 |
| `seq_id` | 12 | 校准轮次。 |
| `node_id` | 4 | 当前节点。 |
| `hop_count` | 4 | 环路跳数，防自激。 |
| `status` | 8 | ready/crc_error/timeout/late/overflow。 |
| `crc6` | 6 | 可选。 |

## 四板自校准

`SELF_CAL_RING` 是 P1 之后的 `SLOW_CTRL_SYNC` 子模式。P0 不实现四板自校准环路；
P0 只保留手工/外部测量得到的 `biss_latency_offset_ns` 写入和查询能力。

```text
A0 -> A1 -> A2 -> A3 -> A0
```

每段都是点对点、固定方向、常使能差分链路。A0 发起，A1/A2/A3 固定延迟转发。

可校准：

- 每段链路固定延迟。
- 节点 PIO 转发延迟。
- 本地 `TRIG_OUT`/开关控制输出 offset。
- round-trip delay。
- jitter 分布。

流程：

```text
1. DISARM，进入 SLOW_CTRL_SYNC。
2. A0 发 SELF_CAL_BEGIN(seq, rounds, bit_rate)。
3. A0 发校准边沿/短帧。
4. A1/A2/A3 PIO 捕获 rx_tick，固定延迟后 tx_tick 转发。
5. A0 收回并记录 round_trip_tick。
6. 各节点慢速回报 rx_tick/tx_tick/status。
7. A0/上位机剔除错误样本，计算 offset/jitter。
8. 写入 latency profile，下一次 FAST_RT_TEST 使用。
```

边界：

- 只靠整环总延迟不能唯一拆分每段单向延迟，必须收集各节点 `rx_tick/tx_tick`，或增加反向/旁路/示波器参考。
- 正式测试不等待校准环路串行传递。
- 温漂、线缆、收发器变更后必须重新校准。
- 校准结果可进入 SD/System Pack profile，ARM 后只读。

## TriggerVector 建议

新增字段建议：

```c
trig_protocol_t  protocol;
trig_biss_role_t biss_role;
uint32_t         biss_phase;          /* SLOW_CTRL_SYNC / FAST_RT_TEST */
uint32_t         biss_clock_hz;
uint32_t         biss_frame_bits;
uint32_t         biss_crc_bits;
uint32_t         biss_timeout_us;
uint32_t         biss_position_offset;
uint32_t         biss_position_bits;
uint32_t         biss_position_modulo;
uint32_t         biss_anchor_offset;
uint32_t         biss_anchor_mask;
uint32_t         biss_anchor_value;
uint32_t         biss_sample_edge;
uint32_t         biss_sample_delay_cycles;
uint32_t         biss_error_bit_offset;     /* UINT32_MAX = disabled */
uint32_t         biss_warning_bit_offset;   /* UINT32_MAX = disabled */
uint32_t         biss_status_gate_policy;
uint32_t         biss_crc_offset;
uint32_t         biss_crc_cover_offset;
uint32_t         biss_crc_cover_bits;
uint32_t         biss_crc_gate_policy;
uint32_t         biss_target;
uint32_t         biss_latency_offset_ns;
uint32_t         biss_last_position;
uint32_t         biss_last_seq;
uint32_t         biss_rx_frame_count;
uint32_t         biss_frame_error_count;
uint32_t         biss_crc_error_count;
uint32_t         biss_status_block_count;
uint32_t         biss_timeout_count;
uint32_t         biss_trigger_count;
uint32_t         biss_cal_round_trip_ns;
uint32_t         biss_cal_jitter_p99_ns;
uint32_t         biss_cal_valid;
```

状态建议：

```c
TRIG_STATE_BISS_CONFIGURED
TRIG_STATE_BISS_ARMED
TRIG_STATE_BISS_CALIBRATING  /* P1+, do not insert before existing values */
```

保持现有 `TRIG_STATE_FAULT` 数值不变，避免破坏 trace/SD 解码。

## PIO 资源建议

| 功能 | PIO/SM | AUX ownership | 说明 |
|---|---|---|---|
| BiSS TAP position receiver | `pio2/sm1` | AUX0=`CLK_IN`, AUX1=`DATA_IN` | P0：监听 `CLK_IN/DATA_IN`，校验锚点并抽取 position/status。 |
| BiSS MASTER clock/data | `pio2/sm1/sm2` | AUX1=`DATA_IN`, AUX2=`CLK_OUT` | P1：MASTER_RX 与 TAP 互斥。 |
| BiSS SLAVE data out | `pio2/sm0` | AUX0=`CLK_IN`, AUX3=`DATA_OUT` | P1：上游时钟驱动移出 DATA。 |
| SELF_CAL forward | `pio2/sm0/sm3` | AUX0=`CAL_IN`, AUX3=`CAL_OUT` | P1：慢速校准固定延迟转发。 |
| TRIG_OUT pulse | `pio1/sm0` | OUT0=`TRIG_OUT` | 复用现有 `sync_pulse`。 |
| 本地 pulse capture | `pio0/sm1` 或 `pio0/sm2` | IN0=`TRIG_IN` | P1：后续 RX_PULSE。 |

启用任意 BiSS role 时必须通过 resource arbiter 独占对应 AUX owner：

- 与其他 AUX persona 互斥；`TRIG:ENC:APIN 26` 已因硬件冻结关闭。
- 与 AUX0=`ARM_IN`、AUX1=`EXT_CLK_IN`、AUX2=`SYNC_CLK_OUT`、AUX3=`MARKER_OUT` 的产品语义互斥。
- 与其他占用 `pio2` 的协议/校准功能互斥。

## SCPI 草案

```text
TRIG:MODE PROT
TRIG:PROT BISS
TRIG:BISS:ROLE TAP|SLAVE|MASTER|BRIDGE
TRIG:BISS:PHAS FAST|SLOW
TRIG:BISS:CLOCK <Hz>
TRIG:BISS:PBIT <bits>
TRIG:BISS:POFF <bit_offset>
TRIG:BISS:PMOD <modulo>
TRIG:BISS:TARG <position>
TRIG:BISS:SAMP:EDGE RIS|FALL
TRIG:BISS:SAMP:DELAY <cycles>
TRIG:BISS:ANCH:MASK <mask>
TRIG:BISS:ANCH:VAL <value>
TRIG:BISS:STAT:GATE IGN|COUNT|BLOCK
TRIG:BISS:LAT:OFFS <ns>
TRIG:BISS:STAT?
TRIG:BISS:LAST?

TRIG:BISS:CAL:MODE RING|OFF
TRIG:BISS:CAL:BEGIN <rounds>
TRIG:BISS:CAL:STAT?
TRIG:BISS:CAL:LAST?
TRIG:BISS:CAL:APPL

TRIG:ARM
TRIG:DIS
```

规则：

- `FAST` 状态下配置命令返回 busy。
- `CAL:*` 只允许 DISARM/SLOW。
- 查询命令只读 TriggerVector 快照。

## 验证路线

P0：

1. `biss_protocol` 单元测试：字段 pack/parse、CRC6 golden vector。
2. PIO receiver 自测：模拟 1 MHz BiSS DATA，确认锚点、position/status FIFO 和 frame_error。
3. 采样相位扫描：1 MHz 下扫 `sample_delay_cycles`，记录稳定窗口。
4. 5 MHz position profile：示波器确认 CLK/DATA 采样裕量。
5. ERR/WRN gate：验证 `IGNORE/COUNT_ONLY/BLOCK_TRIGGER` 行为。
6. IRQ crossing compare：验证只在 crossing 时输出一次。
7. modulo crossing：验证回绕点不误触发。
8. TAP_MONITOR 透明接入：不驱动 DATA，不影响原主站。
9. 延迟测量：`field_done/status_gate -> TRIG_OUT` offset 与 jitter。

P0 验收：

- 1 MHz bring-up 稳定。
- 1 MHz 采样相位窗口可扫描、可冻结、可查询。
- 5 MHz fixed profile receiver 稳定。
- 锚点错误不会进入 crossing compare。
- ERR/WRN gate 可按 profile 阻塞或统计。
- position crossing 输出正确，支持 one-shot/window/modulo。
- `FAST_RT_TEST` P99 jitter `< 5 us`。
- 固定 latency offset 可测、可查、可写入 profile。
- TAP 模式不影响原始编码器链路。

## 实施路线

### P0 - HAOFV 骨架与 5MHz 位置触发

- [ ] 将 mode 固定为 `TRIG_MODE_PROTOCOL_TRIGGER`，BiSS-C 作为 protocol subtype。
- [ ] 整理 TriggerVector 字段：position profile、sample phase、anchor、target、latency offset、统计。
- [ ] 新增 `biss_protocol.h/.c`：配置校验、CRC6、position/event/cal profile pack/parse。
- [ ] 新增 `biss_node_io` 骨架：PIO 资源申请、ARM/DISARM、FIFO/IRQ 回调。
- [ ] 实现 `TAP_MONITOR_RT` PIO fixed profile receiver：采样相位、锚点、position/status。
- [ ] 实现 IRQ crossing compare + `TRIG_OUT` PIO pulse。
- [ ] 实现 SCPI 配置和只读状态。
- [ ] 实现手工/外部测量 latency offset 写入和查询。

### P1 - 可靠性与提速

- [ ] CRC 复核策略：阻塞输出或 late error 统计。
- [ ] 5 MHz 长稳、错误注入、timeout storm 处理。
- [ ] 评估 10 MHz fixed profile receiver。
- [ ] 增加 event profile 和 `SLAVE_TX` 双板闭环。
- [ ] 实现 `SELF_CAL_RING` 骨架、round-trip 统计和 offset profile 写入。
- [ ] 增加 SD/System Pack profile 持久化。

### P2 - 产品化

- [ ] 以 `AM26LV31E` / `AM26LV32E` 为首版 RS-422 物理层候选，完成原理图级端接、偏置、隔离、保护和失效旁路电路定义。
- [ ] 多编码器 profile 支持和设备描述慢速读取。
- [ ] 若要求 20 MHz 以上复杂解码或接近 100 MHz，引入 FPGA/CPLD/专用 BiSS 接口。
