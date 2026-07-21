# USB 接口设计记录

Status: Active
Domain: USB
Canonical: `docs/SCPI_USB_INTERFACE_DESIGN.md`
Related: `docs/SCPI_COMMANDS.md`, `docs/HAOFV_ARCHITECTURE.md`, `docs/OTA_SYSTEM_DESIGN.md`
Last updated: 2026-07-21

本文档记录 RP2350_TRIG 当前 USB 接口形态、USBTMC/USB488 接入策略和后续产品化注意事项。

## 当前结论

- 默认构建仍使用 Pico SDK `stdio` 的 USB CDC，继续兼容现有 SCPI、OTA 和调试脚本。
- 新增可选构建开关 `PROJECT_ENABLE_USBTMC`，用于启用 TinyUSB USBTMC/USB488 + SCPI 专业仪表接口。
- `PROJECT_ENABLE_USBTMC=ON` 时，App 目标会关闭 Pico USB stdio CDC，避免 CDC 默认描述符与自定义 USBTMC 描述符抢同一个 USB device。
- 当前调试阶段 USB 配置描述符按 `bus-powered` 申明，配置电流为 100 mA。
- 后续成品硬件如果确认存在稳定自供电设计，再评估切换为 `self-powered` 描述符属性。

## 构建开关

| 开关 | 默认值 | 说明 |
|---|---:|---|
| `PROJECT_ENABLE_USB_STDIO` | `ON` | 默认 USB CDC stdio，当前量产/验证主线仍依赖它。 |
| `PROJECT_ENABLE_USBTMC` | `OFF` | 启用 USBTMC/USB488 SCPI 设备接口。 |
| `PROJECT_ENABLE_UART_STDIO` | `OFF` | UART stdio 调试输出。USBTMC 模式下可打开用于替代 USB CDC 日志。 |

USBTMC 构建示例：

```powershell
cmake -S . -B build-usbtmc -G Ninja -DPICO_BOARD=pico2 -DPROJECT_ENABLE_USBTMC=ON
cmake --build build-usbtmc
```

## 描述符策略

当前 USBTMC 描述符由 `middleware/usbtmc_scpi_port/src/usbtmc_scpi_port.c` 提供：

| 字段 | 当前值/策略 |
|---|---|
| Class/Subclass/Protocol | Application Specific / USBTMC / USB488 |
| VID | `PROJECT_USB_VID`，当前默认 `0xCAFE`，仅适合开发验证 |
| PID | `PROJECT_USB_PID_USBTMC`，当前默认 `0x4030` |
| Product string | `GTS DTC100` |
| Serial string | RP2350 unique board ID |
| Configuration attributes | 当前调试阶段 `bus-powered` |
| Max power | 100 mA |
| Endpoints | Bulk OUT `0x01`、Bulk IN `0x81`、Interrupt IN `0x82` |

产品化前必须替换开发 VID/PID，避免与示例或其他设备冲突。若最终硬件是自供电，需要同步修改描述符 attribute，并验证插拔、掉电、上位机休眠恢复和 VISA 枚举行为。

## SCPI 复用方式

USBTMC 不新增一套命令表，而是复用现有 `middleware/scpi_port`：

- CDC 路径：`scpi_port_service()` 从 `stdio` 读取命令，响应仍写回 `stdio`。
- USBTMC 路径：`usbtmc_scpi_port` 收到 USBTMC bulk OUT 后调用 `scpi_port_execute()`，把 SCPI 响应捕获到内存缓冲，再通过 USBTMC bulk IN 返回。
- USBTMC 收到不带换行的命令时会补 `\n`，兼容常见 VISA `query("*IDN?")` 行为。

## VISA 预期

USBTMC 固件枚举后，NI-VISA/PyVISA 预期可发现类似资源：

```text
USB0::0xCAFE::0x4030::<SERIAL>::INSTR
```

基础验证命令：

```text
*IDN?
SYST:ERR?
SYST:ERR:COUN?
```

## 验证记录

### 2026-07-21 USBTMC smoke

- 构建目录：`build-codex-usbtmc`
- 烧录产物：`build-codex-usbtmc/RP2350_TRIG_FACTORY.uf2`
- 烧录工具：Pico SDK `picotool load -f`
- NI-VISA 枚举资源：

```text
USB0::0xCAFE::0x4030::73E940D75B406BCD::INSTR
```

PyVISA 直接打开该资源并查询通过：

```text
*IDN? -> GTS,DTC100,0,RP2350_TRIG
SYST:ERR? -> 0,"No error"
SYST:ERR:COUN? -> 0
```

备注：本机 `pyvisa.ResourceManager().list_resources()` 返回 NI-VISA system error，但直接按 NI-VISA 工具显示的 USBTMC resource 打开和查询成功。后续若要依赖自动枚举脚本，需要单独排查 NI-VISA resource manager 的列表接口。

## 待办

- [x] 用真实上位机执行 NI-VISA/PyVISA 枚举和 `*IDN?` smoke。
- [ ] 替换正式 VID/PID 或建立产品 PID 分配规则。
- [ ] 产品硬件定版后确认 `bus-powered` / `self-powered` 描述符属性。
- [ ] 若成品切换为 `self-powered`，补充 VBUS sense、断电行为和主机 suspend/resume 验证记录。
- [ ] 评估是否需要 CDC + USBTMC composite；当前第一阶段为二选一构建。
- [ ] 完整实现 IEEE 488.2 status byte/SRQ 语义；当前仅提供基础 MAV 状态。
