# RS485 通信域实施待办

Status: Active
Domain: Communication / RS485
Canonical: `docs/communication/COMMUNICATION_RS485_TODO.md`
Related: `docs/communication/COMMUNICATION_RS485_ARCHITECTURE.md`, `docs/communication/COMMUNICATION_RS485_TASK_PROGRESS.md`, `docs/arch/HAOFV_FLASH_TODO.md`
Last updated: 2026-08-24

本文只跟踪 RS485 的实现状态、进入条件和退出门禁。稳定语义以
`COMMUNICATION_RS485_ARCHITECTURE.md` 为准，构建号、日志和板端证据只写入
`COMMUNICATION_RS485_TASK_PROGRESS.md`。

## 当前状态

| 子项 | 状态 | 退出门禁 |
|---|---|---|
| Transport/协议边界 | `[x]` | source=RS485、session/journal/Flash owner 边界已与 Flash 域一致 |
| Host COM11 联调工具 | `[~]` | 固化 serial lifecycle、frame/ACK transcript 和负向用例 |
| 固件 RS485 adapter | `[ ]` | UART/DMA ring、DE/RE lease、bounded service、错误统计接入 |
| V2 OTA ingress | `[ ]` | OPEN/DATA/CLOSE/ABORT 通过 RS485 到 durable offset |
| DHRT100 单板验证 | `[ ]` | `*IDN?`、短帧 loopback、stream、abort/restart、错误队列 |
| 五类 ingress 总体验证 | `[ ]` | USB CDC/USBTMC/UART/RS485/SD 一致性和 C11 审核 |

## 阶段待办

### R485-01 Host 联调入口

- [~] 在现有 `tools/ota_stream_send` 基础上固化 RS485 transport profile，不复制 session/wire
  编码；COM11 仅作为显式端口参数和 transcript 标签。
- [ ] 增加 serial open/close、输入清空、发送 drain、超时和异常关闭测试。
- [ ] 增加短帧、CRC 错误、重复、乱序、truncate、超长和 abort/restart 负向回归。

### R485-02 固件 adapter

- [ ] 明确实际 UART、DMA、DE/RE GPIO 和电气 profile，全部从 board 配置读取。
- [ ] 实现 bounded RX ring、TX queue、half-duplex direction lease 和 idle timeout。
- [ ] 将 adapter 接到 `pota_stream_ingress` 的 `POTA_STREAM_INGRESS_RS485`，禁止 raw Flash 调用。
- [ ] 诊断投影 source/state/rx/tx/crc/timeout/de/RE reason，并接入 SCPI 只读查询。

### R485-03 V2 OTA

- [ ] 实现 RS485 OPEN/DATA/CLOSE/ABORT 的固定 wire framing 和 durable ACK。
- [ ] 验证 journal resume 只从 durable offset 继续，未确认尾部不得被 ACK。
- [ ] 验证错误 source、token、target slot、package hash 和 security counter fail closed。

### R485-04 DHRT100 闭环

- [ ] 使用工具先做 `*IDN?`/状态 preflight，再做短帧 loopback。
- [ ] 烧录已验证的 V2 工件后执行 RS485 OTA A/B、abort/restart 和错误队列清零。
- [ ] 保存 COM11 原始 transcript、固件 build、slot/result、journal 和回退路径。
- [ ] 真实断电放在所有非破坏性检查之后，且单独记录 power-cut gate。

## 完成定义

RS485 只有在 host、build、固件、DHRT100 HIL、回退证据和独立 C11 审核全部具备后，才能在
本文件标记 `[x]`，并由 Flash/OTA 域决定是否更新 `ARCH-OTASTREAM-01` 状态。
