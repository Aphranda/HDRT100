# RS485 通信域架构

Status: Active
Domain: Communication / RS485
Canonical: `docs/communication/COMMUNICATION_RS485_ARCHITECTURE.md`
Related: `docs/communication/COMMUNICATION_RS485_TODO.md`, `docs/communication/COMMUNICATION_RS485_TASK_PROGRESS.md`, `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/interface/SCPI_COMMANDS.md`
Last updated: 2026-08-24

本文定义 DHRT100 的 RS485 半双工通信边界和后续 OTA 联调入口。它只冻结 owner、生命周期和
fail-closed 原则；波特率、引脚、收发器 DE/RE 时序等板级数值必须来自实际 board 配置或硬件
评审，不能在本文中猜测。

## 1. 范围与非目标

- RS485 是 `OtaStreamSession` 的本地 ingress 之一，与 USB CDC、USBTMC、UART、SD 共用
  `pota_stream_ingress`、wire、journal 和 FlashTransaction sink。
- RS485 adapter 只负责帧收发、半双工方向切换、CRC/长度检查、ACK/timeout 和 backpressure；
  不直接调用 Flash erase/program，也不绕过 `FlashTransactionAO`。
- TDMA、BiSS-C 和 SYNC_IO 的实时 owner 不迁移到 RS485。需要实时确定性的业务必须经相应域
  的 mailbox/traffic scheduler 申请资源。
- COM11 是当前外部 RS485 通讯器的主机端口标签，不是产品身份；板卡身份始终由 `*IDN?`
  返回的 DHRT100 identity 确认。
- RS485/UART# 的维护协议由 USB 上的 SCPI 配置：`COMMunication:SERial:UART#:MODE SCPI`
  或 `MODE MODBUS`。默认保持 SCPI 兼容；选择 `MODBUS` 后才允许 Modbus RTU adapter 接管
  RS485 数据面，不能靠总线上的业务帧隐式切换。

## 2. HAOFV owner 与生命周期

```text
RS485 UART/DMA + DE/RE owner
        -> RS485 frame adapter
        -> pota_stream_ingress (source=RS485)
        -> OtaStreamSession / OTA_JOURNAL
        -> FlashTransactionAO(core0)
        -> Boot/BCB Direct A/B
```

- 通信 AO/driver 只拥有 UART/DMA、接收 ring、发送队列和 DE/RE lease。
- `OtaAO` 拥有 session admission、durable ACK 和 close/abort 状态；FlashTransactionAO 拥有
  所有 App erase/program。
- 发送 ACK 前必须确认对应 offset 已达到 durable completion；收到 abort、超时、冲突 source 或
  CRC 错误时 fail closed，并释放 DE/RE lease。
- core1 park、watchdog supervisor 和实时 TDMA owner 不得被 RS485 阻塞；每次 service 必须是
  有界步骤并返回调度。

## 3. 帧与会话原则

- 控制面沿用 SCPI 行命令；二进制 OPEN/DATA 负载沿用固定 little-endian stream wire，RS485
  只改变 transport framing，不改变 session token、generation、package/object、target slot
  或 durable offset 语义。
- 当前联调阶段优先实现 Modbus RTU；SCPI 仍是 USB 配置和诊断入口。未完成 Modbus adapter
  前，`MODE?` 可以报告选择，但不得宣称 RS485 数据面已 ready。
- 每个 RS485 数据帧包含可校验的长度、序号/offset、payload CRC 和 ACK 关联信息；具体 wire
  字段以 `pota_stream_wire.h` 和生成测试为事实源。
- 半双工总线上同一时刻只允许一个发送 owner。DE 拉高到首字节前、最后一个停止位后释放的
  时序必须由 driver 的 bounded state machine 管理，禁止在 SCPI handler 中 busy-wait。
- 重复帧只能得到幂等 ACK；乱序、超长、CRC 错误、错误 session/token、错误 target 或非
  durable offset ACK 必须拒绝并记录 reason。

## 4. 验证分层

1. Host：frame codec、CRC、重传/重复/乱序、半双工 ACK 和 serial lifecycle。
2. Build：RS485 adapter 不引入 raw Flash caller，V2 构建保持 Direct A/B 和 FlashMap gate。
3. DHRT100：使用确认身份的板卡验证 `*IDN?`、通信状态、短帧 loopback、stream OPEN/DATA/
   CLOSE、durable ACK、abort/restart 和错误队列。
4. COM11 联调：仅作为当前 RS485 通讯器端口；脚本必须显式打开、清空、收尾并输出原始 transcript。

在 host/build/板端证据和独立 C11 审核齐全前，不将 `ARCH-OTASTREAM-01` 或任何 RS485
实现契约从 `pending` 改为 `active`。
