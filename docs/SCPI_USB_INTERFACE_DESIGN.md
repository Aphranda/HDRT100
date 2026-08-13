# USB 接口设计记录

Status: Active
Domain: USB
Canonical: `docs/SCPI_USB_INTERFACE_DESIGN.md`
Related: `docs/SCPI_COMMANDS.md`, `docs/HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_SYSTEM_DESIGN.md`
Last updated: 2026-07-22

本文档记录 RP2350_TRIG 当前 USB 接口形态、USBTMC/USB488 接入策略和后续产品化注意事项。

## 当前结论

- 默认构建仍使用 Pico SDK `stdio` 的 USB CDC，继续兼容现有 SCPI、OTA 和调试脚本。
- 新增可选构建开关 `PROJECT_ENABLE_USBTMC`，用于启用 TinyUSB USBTMC/USB488 + SCPI 专业仪表接口。
- `PROJECT_ENABLE_USBTMC=ON` 时，App 目标会关闭 Pico USB stdio CDC，避免 CDC 默认描述符与自定义 USBTMC 描述符抢同一个 USB device。
- 调试阶段若需要同一份固件在 CDC / USBTMC 间切换，可启用 `PROJECT_ENABLE_USB_RUNTIME_SWITCH=ON`，并通过独立 Product Config 的 `SYST:USB:MODE` 持久化选择下次启动模式。
- 开发期和产品期两条 USB 路径保持同步演进；当前开发调试可优先走 CDC，产品默认出口后续切到 USBTMC。
- 这里的 USB mode 不是 OTA A/B 切换；A/B 只保留给升级镜像和回滚。
- 当前调试阶段 USB 配置描述符按 `bus-powered` 申明，配置电流为 100 mA。
- 后续成品硬件如果确认存在稳定自供电设计，再评估切换为 `self-powered` 描述符属性。
- USBTMC 是设备类接口，不会像 BOOTSEL/UF2 那样挂载成磁盘；要刷 UF2 仍需进入 BOOTSEL，或先通过可用的重启路径切回 BOOTSEL。
- `SYST:USB:MODE?` 返回 SCPI 字符串 `"CDC"` 或 `"USBTMC"`，脚本可直接按字符串内容判断当前模式。
- `*IDN?` 当前格式为 `GTS,DTC100,<SERIAL>,0.1.0`，其中 `SERIAL` 使用板子唯一 ID，避免暴露板级/芯片命名。

## 统一规则

- SCPI 命令语义只维护一套，共享同一个 `middleware/scpi_port` 命令表。
- CDC 和 USBTMC 只允许在传输层、描述符层、状态回传层有差异，不允许各自演化出两套业务命令树。
- 新增业务命令默认先加入共享 SCPI 层；只有 transport 专属能力才允许进入 USB 入口层。
- 任何需要同时改 CDC 和 USBTMC 行为的命令，必须先写入本文档并说明差异原因。
- 如果某个命令只在单一 transport 上可用，必须明确标注为 transport-specific，不得伪装成通用 SCPI 业务命令。
- `SYST:USB:*` 属于共享控制面，不是两套独立业务面；它只负责查询和切换当前 USB mode。
- 运行态只允许一个 USB transport 生效，CDC 和 USBTMC 互斥，不允许同时运行。
- 切换到另一种 mode 的标准动作是写入 `Product Config` 后重启，由启动阶段选择唯一生效的 transport。

## 结构约束方案

- `middleware/scpi_port/src/scpi_port.c` 只保留共享 SCPI 命令表和共享执行入口。
- `SYST:USB:*` 统一下沉到独立的 USB 控制模块，避免它们在主命令表里散落生长。
- USB 控制模块只负责 `Product Config`、重启和 USB mode 查询，不允许扩展成第二套业务命令树。
- 构建阶段增加 namespace 检查，限制 `SYSTem:USB:` 只出现在 USB 控制模块和本文档中。
- 后续如果出现新的 transport-specific 命令，先在这里登记，再实现代码。

## 构建开关

| 开关 | 默认值 | 说明 |
|---|---:|---|
| `PROJECT_ENABLE_USB_STDIO` | `ON` | 默认 USB CDC stdio，当前量产/验证主线仍依赖它。 |
| `PROJECT_ENABLE_USBTMC` | `OFF` | 启用 USBTMC/USB488 SCPI 设备接口。 |
| `PROJECT_ENABLE_USB_RUNTIME_SWITCH` | `OFF` | 启用 Product Config 驱动的 CDC/USBTMC 启动切换。 |
| `PROJECT_ENABLE_UART_STDIO` | `OFF` | UART stdio 调试输出。USBTMC 模式下可打开用于替代 USB CDC 日志。 |

USBTMC 构建示例：

```powershell
cmake -S . -B build-usbtmc -G Ninja -DPICO_BOARD=pico2 -DPROJECT_ENABLE_USBTMC=ON
cmake --build build-usbtmc
```

## 描述符策略

当前 USBTMC / CDC 描述符由 `middleware/usbtmc_scpi_port/src/usbtmc_scpi_port.c` 提供：

| 字段 | 当前值/策略 |
|---|---|
| Class/Subclass/Protocol | Application Specific / USBTMC / USB488 |
| VID | `PROJECT_USB_VID`，当前默认 `0xCAFE`，仅适合开发验证 |
| PID | `PROJECT_USB_PID_USBTMC`，当前默认 `0x4030` |
| PID (CDC) | `PROJECT_USB_PID_CDC`，当前默认 `0x402F` |
| Product string | `GTS DTC100` |
| Serial string | RP2350 unique board ID |
| Configuration attributes | 当前调试阶段 `bus-powered` |
| Max power | 100 mA |
| Endpoints | CDC: EP OUT `0x01`、EP IN `0x81`、Notif `0x82`；USBTMC: Bulk OUT `0x01`、Bulk IN `0x81`、Interrupt IN `0x82` |

产品化前必须替换开发 VID/PID，避免与示例或其他设备冲突。若最终硬件是自供电，需要同步修改描述符 attribute，并验证插拔、掉电、上位机休眠恢复和 VISA 枚举行为。

## SCPI 复用方式

USBTMC 不新增一套命令表，而是复用现有 `middleware/scpi_port`：

- CDC 路径：TinyUSB CDC 直接把原始字节喂给 `SCPI_Input()`，响应通过 CDC 回写。
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

## USBTMC OTA

USBTMC 应用态可以通过 VISA 发送现有统一 OTA package，不需要额外协议：

```powershell
python tools\visa_ota_send\visa_ota_send.py USB0::0xCAFE::0x4030::<SERIAL>::INSTR build-codex-usbtmc\RP2350_TRIG_UPDATE.pkg --boot
```

package 模式下第一个 `SYST:OTA:DATA` block 必须是 512 B package header。固件解析 header 后会进入 `ERASE_SLOT` 擦除目标 slot，发送端必须等状态从 `RECEIVING` 离开并重新回到 `RECEIVING` 后，再继续发送后续数据块。

## 调试切换

- `SYST:USB:MODE?` 查询当前 Product Config 记录的 USB mode。
- `SYST:USB:MODE CDC` / `SYST:USB:MODE USBTMC` 写入下次启动模式。
- `SYST:USB:BOOT` 立即重启，重启后按新模式枚举。

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
*IDN? -> GTS,DTC100,<SERIAL>,0.1.0
SYST:ERR? -> 0,"No error"
SYST:ERR:COUN? -> 0
```

备注：本机 `pyvisa.ResourceManager().list_resources()` 返回 NI-VISA system error，但直接按 NI-VISA 工具显示的 USBTMC resource 打开和查询成功。后续若要依赖自动枚举脚本，需要单独排查 NI-VISA resource manager 的列表接口。

### 2026-07-22 picotool flash

- 通过 `picotool load -f -v build-codex-usbtmc/RP2350_TRIG_FACTORY.uf2` 成功强制重启、烧录和验证。
- 烧录后 `PyVISA` 查询返回：

```text
*IDN? -> GTS,DTC100,<SERIAL>,0.1.0
SYST:ERR? -> 0,"No error"
SYST:ERR:COUN? -> 0
```

### 2026-07-22 USBTMC OTA package

- 通过 `tools/visa_ota_send/visa_ota_send.py` 使用 USBTMC/VISA 发送 `build-codex-usbtmc/RP2350_TRIG_UPDATE.pkg`。
- package 大小 `333188` B，CRC32 `0x24C0ED9C`。
- 发送过程确认状态序列：`RECEIVING -> ERASE_SLOT -> RECEIVING -> READY_TO_REBOOT`。
- OTA 后发送 `SYST:OTA:BOOT`，设备重新枚举为 USBTMC。
- 重启后 `PyVISA` smoke 通过：

```text
*IDN? -> GTS,DTC100,<SERIAL>,0.1.0
SYST:ERR? -> 0,"No error"
SYST:ERR:COUN? -> 0
```

### 2026-07-22 runtime switch smoke

- 通过 USBTMC 执行 `SYST:USB:MODE CDC`，随后 `SYST:USB:BOOT`。
- 设备重新枚举为 `COM9`，Windows 识别为 `USB 串行设备`。
- 串口侧 `*IDN?` 返回 `GTS,DTC100,73E940D75B406BCD,0.1.0`。
- 串口侧 `SYST:USB:MODE?` 返回 `"CDC"`。
- 再通过串口执行 `SYST:USB:MODE USBTMC` 和 `SYST:USB:BOOT`，设备重新回到 USBTMC。
- 回到 USBTMC 后，PyVISA 资源 `USB0::0xCAFE::0x4030::73E940D75B406BCD::INSTR` 重新可用，`*IDN?` 和 `SYST:USB:MODE?` 正常返回。

## 待办

- [x] 用真实上位机执行 NI-VISA/PyVISA 枚举和 `*IDN?` smoke。
- [x] 调试阶段验证 `PROJECT_ENABLE_USB_RUNTIME_SWITCH` 下的 CDC/USBTMC 双模式切换。
- [ ] 把 `SYST:USB:*` 从 `scpi_port.c` 抽到独立 USB 控制模块。
- [ ] 增加构建时 namespace 检查，阻止 `SYSTem:USB:` 在其他源码文件里扩散。
- [ ] 把 USB 专属命令的注册点收敛到单一命令块，避免主命令表里出现分叉。
- [ ] 替换正式 VID/PID 或建立产品 PID 分配规则。
- [ ] 产品硬件定版后确认 `bus-powered` / `self-powered` 描述符属性。
- [ ] 若成品切换为 `self-powered`，补充 VBUS sense、断电行为和主机 suspend/resume 验证记录。
- [ ] 评估是否需要 CDC + USBTMC composite；当前第一阶段为二选一构建。
- [ ] 完整实现 IEEE 488.2 status byte/SRQ 语义；当前仅提供基础 MAV 状态。
