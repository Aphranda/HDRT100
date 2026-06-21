# SCPI 基础命令

当前 SCPI 服务通过 Pico SDK `stdio` 通道接入，默认使用 USB CDC。命令以 `\n` 或 `\r\n` 结束。

## 标准命令

| 命令 | 说明 |
|---|---|
| `*IDN?` | 查询设备身份。 |
| `*RST` | 恢复 SCPI 当前基础配置，并停止采样和同步时钟。 |
| `*CLS` | 清除 SCPI 状态/错误。 |
| `*TST?` | 自检占位，当前返回 `0`。 |
| `SYST:ERR?` | 查询下一条 SCPI 错误。 |
| `SYST:ERR:COUN?` | 查询 SCPI 错误数量。 |
| `SYST:VERS?` | 查询 SCPI 标准版本。 |

## 触发输出

| 命令 | 说明 |
|---|---|
| `TRIG:WIDT <us>` | 设置 `GPIO20/TRIG_OUT` 脉宽，单位 us。 |
| `TRIG:WIDT?` | 查询 `TRIG_OUT` 脉宽。 |
| `TRIG:IMM` | 立即输出一次 `TRIG_OUT` 脉冲。 |

## 第二路脉冲输出

| 命令 | 说明 |
|---|---|
| `PULS:WIDT <us>` | 设置 `GPIO21/PULSE_OUT` 脉宽，单位 us。 |
| `PULS:WIDT?` | 查询 `PULSE_OUT` 脉宽。 |
| `PULS:IMM` | 立即输出一次 `PULSE_OUT` 脉冲。 |

## Marker 输出

| 命令 | 说明 |
|---|---|
| `MARK:WIDT <us>` | 设置 `GPIO23/MARKER_OUT` 脉宽，单位 us。 |
| `MARK:WIDT?` | 查询 `MARKER_OUT` 脉宽。 |
| `MARK:IMM` | 立即输出一次 `MARKER_OUT` 脉冲。 |

## 采样配置

| 命令 | 说明 |
|---|---|
| `SAMP:RATE <Hz>` | 设置输入采样率，并启动 `GPIO16..GPIO19` 采样。 |
| `SAMP:RATE?` | 查询输入采样率。 |
| `SAMP:STAT ON` | 启动输入采样。 |
| `SAMP:STAT OFF` | 停止输入采样。 |
| `SAMP:STAT?` | 查询采样状态。 |

## 同步时钟输出

| 命令 | 说明 |
|---|---|
| `OUTP:CLOC:FREQ <Hz>` | 设置 `GPIO22/SYNC_CLK_OUT` 输出频率。 |
| `OUTP:CLOC:FREQ?` | 查询同步时钟频率。 |
| `OUTP:CLOC:STAT ON` | 启动同步时钟输出。 |
| `OUTP:CLOC:STAT OFF` | 停止同步时钟输出。 |
| `OUTP:CLOC:STAT?` | 查询同步时钟输出状态。 |

## 状态查询

| 命令 | 说明 |
|---|---|
| `STAT:SYNC?` | 返回同步 IO 状态：初始化状态、采样状态、时钟状态、采样率、时钟频率、采样溢出计数。 |

## OTA 维护

OTA 命令遵循 `docs/OTA方案.md` 中的 `OtaAO + OtaFB + OtaVector` 设计。SCPI 只负责解析命令、投递 OTA 事件和读取状态快照，不直接擦写 Flash，不直接修改 OTA 状态。

| 命令 | 说明 |
|---|---|
| `SYST:OTA:STAT?` | 查询 OTA 状态摘要：状态、目标 slot、错误码、最近结果。 |
| `SYST:OTA:PROG?` | 查询 OTA 进度：已接收字节、期望字节、千分比进度。 |
| `SYST:OTA:BEGIN <size>,<crc32>` | 开始 OTA 传输，`size/crc32` 对应标准 raw firmware `.bin`。 |
| `SYST:OTA:DATA #<block>` | 发送 `.bin` 二进制块，投递 `OTA_EVENT_DATA_BLOCK`。 |
| `SYST:OTA:END` | 结束传输并请求校验，投递 `OTA_EVENT_END`。 |
| `SYST:OTA:ABOR` | 中止当前 OTA，投递 `OTA_EVENT_ABORT`。 |
| `SYST:OTA:BOOT` | 镜像 ready 后请求重启进入 pending slot。 |
| `SYST:OTA:COMM` | App 自检通过后确认当前固件。 |
| `SYST:OTA:SLOT?` | 查询 active、pending、confirmed slot。 |
| `SYST:OTA:RES?` | 查询最近一次 OTA 结果和错误码。 |

第一阶段建议 `SYST:OTA:DATA` 单块 256 B 或 512 B。OTA 期间应暂停周期日志，避免日志与 SCPI binary block 混用同一 USB CDC 通道。

## 当前限制

- SCPI 当前接入的是底层 `sync_io`，还不是完整 `sync_trigger` 状态机。
- 日志和 SCPI 响应目前共用 stdio 通道，后续产品化应拆分控制通道和日志通道，或在 SCPI 会话期间关闭周期日志。
- `SAMP:RATE` 当前会直接启动采样，但尚未接入 DMA 环形缓冲。
- OTA 命令目前仍处于方案阶段，后续实现时必须通过 `OtaAO` 投递事件，不允许 SCPI 直接调用 Flash 擦写 API。
