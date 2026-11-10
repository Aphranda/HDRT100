# DHRT100 序列触发调试（WPF）

Status: Active
Domain: Tools
Canonical: `tools/sequence_trigger_debug_wpf/README.md`
Related: `tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py`, `tools/scpi_common/scpi_serial.py`
Last updated: 2026-11-11

C# / WPF 版本的序列触发调试界面，功能对齐
`tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py`，但通信层改成
**同时支持 USB CDC 串口与 USBTMC（直接 P/Invoke `visa32.dll`）**。

## 为什么重写通信层

Python 版靠 pyserial / pyvisa 已经把这些固件的怪癖处理掉了；C# 侧常见的
“一用 NI + USB TMC 就报错”基本来自下面几条，本实现逐条对应处理：

| 固件/协议行为 | 直接照抄 C# 常见写法的后果 | 本实现的处理 |
|---|---|---|
| setter（无 `?` 的命令）也会回一个数值或 `"OK"` | `Write()` 不读，ACK 留在设备缓冲里，下一次 `Read()` 拿到上一条的回包 | 每次写都读一个回包（`ScpiSession.Exchange` 统一写+读） |
| `SYST:TDMA:RING:STOP/ARM/START` 不回任何数据 | `Query()` 必然超时抛异常 | 允许超时，随后用 `SYST:TDMA:RING:STAT?` 验证真实状态 |
| 诊断日志与 SCPI 回包共用同一路字节流 | 读到 `[ 12] INF ...` 当成回包，解析失败 | 跳过日志行、剥离行内日志尾巴 |
| 回包不保证带换行 | 等 `\n` 一直等到超时 | CR/LF **或 50 ms 空闲间隔** 都作为分帧结束 |
| `tud_usbtmc` 的 bulk-in 请求在没有数据时保持挂起 | 一次超时之后整条链路错位，后面全错 | VISA 超时后 `viClear()` 中止挂起传输；且写前不做“探测式读取” |
| USBTMC 侧回包缓冲 2048 B，超长会 STALL | 大页读取报总线错误 | 单次读取限制在 2048 字节以内 |

## 实机调试中额外发现的两条固件行为

这两条是上台架跑 `--bench` 才暴露的，工具已经内置处理：

1. **启动前 SMA 输出必须全为低。** 固件 `outputs_available()` 要求
   `sync_io_sequence_read_outputs() == 0`，否则 `TRIG:START` 被拒绝**且不回包**，
   客户端只能看到“超时”，错误队列里留 `-200,"Execution error"`。
   常见原因是手动 SP8T 停在 2 位（OUT1 高）。本工具在 START 前先查
   `READ:IO:STAT?`，输出掩码非 0 时直接给出可操作提示，并在“手动开关控制”页提供
   **“释放输出”**按钮（`TRIG:STOP → CONF:SWITCH1 1`）。
2. **错误队列是 FIFO 且会跨命令残留。** 上一次被拒绝留下的 `-200` 会被下一条命令的
   `SYST:ERR?` 读到，于是“TRIG:STOP 被设备拒绝”这类误报就出现了。本工具在每个
   用户批次开始前先清空错误队列（日志里会显示被丢弃的残留错误）。

## 四位输出编码（0–15）

Python 版把编码从 3 位扩到 4 位，本实现同步。固件判据是
`scpi_sequence_commands.c` / `trigger_sequence_service_set_code()`：

```c
value > SYNC_IO_SEQUENCE_CODE_MAX                                    /* 0x0F */
((value >> SYNC_IO_SEQUENCE_CODE_SP8T_SHIFT) & ~s_io.sequence_output_mask) != 0u
```

也就是说一个编码是 **4 位**：

| 位 | 归属 |
|---|---|
| bit0 | OUT0（IN3 焊盘上的 SP2T） |
| bit1–bit3 | SP8T 位置，受 `sequence_output_mask` 约束 |

所以掩码 7（OUT1–OUT3）时 `0..15` 全部合法，而掩码 3 时只允许 `0..7`
（`8 >> 1 = 4` 已经超出掩码）。界面默认编码因此是
`0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15`。

### `CONF:TRIG` 是「通道数 × 极化」，不是状态数

16 个编码送进固件时，**不能**写成 `CONF:TRIG 16,0,1,1`：`CONF:TRIG` 的四个参数是
`chan_count,pol,freq_count,wave_count`，而 `chan_count` 被
`TRIGGER_SEQUENCE_CHANNEL_MAX = 8` 限制（`trigger_sequence_config.c:parameter_count()`）。
超限时固件走 `SCPI_RES_ERR`，**不回包**，客户端只看到超时 + 队列里的
`-200,"Execution error"`。

16 状态计划要写成 `CONF:TRIG 8,2,1,1`：`pol=2` 表示双极化，状态数 =
8 通道 × 2 极化 = 16。实机读回可验证：

```text
CONF:TRIG 8,2,1,1   => 1
READ:TRIG:PAR?      => "TRIGGER",<crc>,1,1,1,0,8,2,1,1,16,<mapcrc>
                                                   ^chan ^pol   ^state_count
```

本实现在 `SequenceCommandBuilder.TriggerParameters()` 里按编码数量推导：
`≤8` → `(n,0)`；`9..16` 且为偶数 → `(n/2,2)`；其余直接报错不发命令。

> **Python 版已在同一轮同步**：`sequence_trigger_debug_ui.py` 现在按同一规则推导
> `CONF:TRIG`（16 编码 → `8,2,1,1`），不再发送会被固件拒绝的 `CONF:TRIG 16,0,1,1`。

## 手动开关控制

“手动开关控制”页同时提供两路：

- **SP8T**：`CONF:SWITCH1 <1..8>` / `READ:SWITCH1?`（OUT1–OUT3）
- **SP2T**：`CONF:SWITCH2 <0|1>` / `READ:SWITCH2?`（IN3 / OUT0）

两者都要求序列处于 IDLE；回读 `READ:SWITCH2?` 的第 2 个字段是电平、第 3 个字段非 0
表示被序列占用（界面显示“· 忙”）。

## 构建与运行

```powershell
cd tools\sequence_trigger_debug_wpf
dotnet build SequenceTriggerDebugWpf.sln
dotnet run --project src\DhrT100.SequenceDebug.App
```

产物：`src\DhrT100.SequenceDebug.App\bin\Debug\net8.0-windows\DhrT100.SequenceDebug.exe`

命令行开关：

| 开关 | 作用 |
|---|---|
| `--settings <path>` | 指定参数草稿文件（默认 `%LOCALAPPDATA%\DHRT100\sequence_trigger_debug_ui.json`） |
| `--no-settings` | 不读写参数草稿 |
| `--resource <name>` | 覆盖连接资源（`COM4` / `USB0::…::INSTR`） |
| `--smoke` | 显示全部窗口、收集 WPF 绑定错误后退出（0 = 干净） |
| `--probe <Serial\|"USB TMC"> <resource> [命令…]` | 只读探测；给命令则按给定命令逐条发送 |
| `--configure <independent\|rj45\|turntable>` | 重放一次“配置此模式”，打印完整命令日志与结果 |
| `--bench <independent\|rj45\|turntable>` | 重放 配置 → 启动 → 读回 → 停止 整链，退出码 0 表示全通 |

`--bench` / `--configure` 走的是界面同一套 ViewModel 与批执行器，所以能复现
“点按钮才出现”的问题；`--probe` 只发查询、不改设备状态，适合上台架先确认链路。

> PowerShell 的 `Start-Process -ArgumentList` 会把带空格的单个参数拆开，传
> 带空格的 SCPI 命令或 `"USB TMC"` 时要用一整条参数串，例如：
> `-ArgumentList '--probe Serial COM4 "TRIG:MODE 0" "READ:IO:STAT?"'`。

### USB TMC 的前提

固件侧的切换指令是存在的：

```text
SYSTem:USB:MODE?            # 查询当前模式（CDC / USBTMC）
SYSTem:USB:MODE USBTMC      # 写入模式并持久化
SYSTem:USB:BOOT             # 重启 USB，使新描述符生效
```

但这三条命令在 `middleware/scpi_port/src/scpi_port.c` 的命令表里被
`#if PROJECT_ENABLE_USB_RUNTIME_SWITCH` 包着。当前板上的 composite CDC 构建
（PID `0x0009`）没有打开该选项，所以 `SYST:USB:MODE?` 返回 `-113 Undefined header`
且无回包。要在运行时切换，必须先烧录：

```powershell
cmake --preset pico2-usb-runtime-switch      # PROJECT_ENABLE_USB_RUNTIME_SWITCH=ON, 默认 CDC
```

烧录后二选一：

- 界面：“设备维护” → “切到 USB TMC”（工具会自动查询、写入、校验、再 `SYST:USB:BOOT`）；
- 手动：在“自定义 SCPI”里依次发 `SYST:USB:MODE USBTMC`、`SYST:USB:BOOT`；
- 命令行（推荐，便于脚本化）：

```powershell
# CDC -> TMC
\.\DhrT100.SequenceDebug.exe --probe Serial COM3 "SYST:USB:MODE USBTMC" "SYST:USB:MODE?" "SYST:USB:BOOT"
# 设备以 USB0::0xCAFE::0x4030::<SERIAL>::INSTR 重新枚举，用 TMC 访问：
\.\DhrT100.SequenceDebug.exe --probe "USB TMC" "USB0::0xCAFE::0x4030::839E1AE79EA20F31::INSTR"
# TMC -> CDC（同样三步，BOOT 之后 COM 口会回来）
\.\DhrT100.SequenceDebug.exe --probe "USB TMC" "USB0::...::INSTR" "SYST:USB:MODE CDC" "SYST:USB:MODE?" "SYST:USB:BOOT"
```

`SYST:USB:BOOT` 的输出通常读不到（设备立刻重启 USB），工具把
`IOException` / `OperationCanceledException` / `VI_ERROR_SYSTEM_ERROR` 都视为“已重启”，
不会误判为切换失败。

### TMC 的写入延迟（实测）

同一台设备、同一固件（`20261110164245`）实测：

| 命令 | CDC | USBTMC |
|---|---|---|
| `*CLS`（只写不读） | ~0 ms | ~0 ms |
| `*IDN?` / `SYST:ERR?`（查询） | ~0 ms | ~0 ms |
| `CONF:SEQ:REPEAT 1`（写 + 回包） | ~0 ms | **~1.9–2.0 s** |
| `CONF:SWITCH1 2`（写 + 回包） | ~0 ms | **~1.9–2.0 s** |
| `CAL:TOPOLOGY:PROBE 1,10`（写 + 回包） | ~0 ms | **~1.9–2.0 s** |
| 16 编码完整配置 | ~2 s | **~127 s** |
| 完整 bench（配置→启动→读回→停止） | ~10 s | **~226 s** |

规律：**TMC 下“写命令且带回包”约 2 s，查询和只写命令不受影响**，与具体子系统
（sequence / sync_io / calibration）无关，说明延迟在 USBTMC 回包投递路径而不是命令处理。
`configuration_end()` 只清原子标志、`set_frozen()` 只赋值，序列服务本身是微秒级的。

因此：

- 工具在 TMC 下把单命令超时放宽到 5 s（CDC 仍 2 s），CLI 的 `--configure/--bench`
  等待上限 180 s；界面里 TMC 配置会明显“转圈”，进度以命令日志为准。
- 需要对时序敏感的操作（手动开关、预算、回环观察）优先走 CDC；TMC 适合
  “仪器库统一访问”的场景。

## 结构

```
src/DhrT100.SequenceDebug.Core/       纯逻辑：命令构造、解析、校验、批执行器、预算
src/DhrT100.SequenceDebug.Transport/  CDC（System.IO.Ports）与 USBTMC（visa32.dll P/Invoke）
src/DhrT100.SequenceDebug.App/        WPF 界面（5 个页签 + 5 个对话框）
tests/DhrT100.SequenceDebug.Tests/    xUnit，对照 Python 测试的期望值
```

Core 层不依赖 UI 与设备，`dotnet test` 可在无板卡机器上全部跑通。

## 与 Python 版的行为对应

- **参数草稿文件格式一致**：同一个 `version`/`values` 结构、同一批字段名，
  两个工具可以共用一份草稿文件（后保存的覆盖先保存的）。
- 序列构造、`SYST:ERR?` 校验、ring ACK 等待、owner 应用等待、ANGLE 读回校验、
  回环观察、TDMA/VDC 预算事务都按 Python 版逐条移植。
- 界面文案、页签、按钮语义保持一致；差异仅在布局实现（WPF 控件与样式）。

## 已知边界

- USBTMC 依赖已安装的 VISA 运行库（NI-VISA 或 IVI VISA），与 Python 版一致；
  串口模式不需要任何 VISA。
- 单板 OTA 仍调用仓库内 Python 工具（`tools/ota_multi_update`、
  `tools/visa_ota_send`），因为那是验收产物的唯一实现。
- 波形级/多板验收不在本工具范围内，和 Python 版相同。

## 验证

```powershell
dotnet test tests\DhrT100.SequenceDebug.Tests\DhrT100.SequenceDebug.Tests.csproj
dotnet run --project src\DhrT100.SequenceDebug.App -- --smoke
.\verify-bench.ps1 -Resource COM3            # 一键实机验收：probe/budget/三模式 configure+bench
```

`verify-bench.ps1` 的每一步都打印 PASS/FAIL，失败时贴出日志尾部；`-Build`/`-Test`
会先编译/跑测试，`-Only independent` 可只跑单个模式。

实机（DHRT100 `0.1.0` / build `20261110164245`，VID `0xCAFE`、CDC PID `0x402F`、TMC PID `0x4030`）验证记录：

| 项目 | 结果 |
|---|---|
| probe / budget（CDC） | 通过，无超时 |
| 独立 SP8T 配置 + bench（16 编码，`CONF:TRIG 8,2,1,1`） | 通过 |
| RJ45 物理回环 配置 + bench | 通过（ring `up_running=1`，`LINK` phase=3 等 READY） |
| 转台计数 配置 + bench | 通过（三槽位 `ACTIVE`，`COUNTER` phase=9 等阈值） |
| CDC → USBTMC → CDC 切换 | 双向成功，`SYST:USB:MODE?` 可读回 |
| USBTMC probe | 通过（8 条查询全部有回包，含 40 字段 ring 行） |
| USBTMC 配置独立 SP8T | 通过，127 s（每条写 ≈2 s） |
| USBTMC bench 独立 SP8T | 通过，226 s，ERROR 0 条 |
| 手动开关 SP8T/SP2T | `CONF:SWITCH1/2` + `READ:SWITCH1/2?` 读写一致 |
