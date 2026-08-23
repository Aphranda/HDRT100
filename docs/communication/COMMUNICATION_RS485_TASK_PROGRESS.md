# RS485 通信域任务进度

Status: Active
Domain: Communication / RS485
Canonical: `docs/communication/COMMUNICATION_RS485_TASK_PROGRESS.md`
Related: `docs/communication/COMMUNICATION_RS485_ARCHITECTURE.md`, `docs/communication/COMMUNICATION_RS485_TODO.md`, `docs/arch/HAOFV_FLASH_TASK_PROGRESS.md`
Last updated: 2026-08-24

本文只追加 RS485 的提交、构建、host/HIL 原始证据、失败、回退和阻塞记录；不在此冻结新契约，
也不替代 Architecture/TODO 的事实边界。

## COMM-RS485-20260824-001 - 建立 RS485 三件套与 COM11 联调入口

- 状态：文档三件套建立；RS485 固件 producer、COM11 实际收发和 DHRT100 OTA 尚未宣称完成。
- 当前输入：`pota_stream_ingress` 已包含 `POTA_STREAM_INGRESS_RS485` source，现有
  `tools/ota_stream_send/ota_stream_send.py` 的 stream wire/SCPI 控制面仍固定为 USB CDC，
  因此本任务下一步是增加 transport profile，而不是复制一套 OTA session。
- 联调资源：COM11 仅表示 RS485 通讯器端口；联调脚本必须先确认 `*IDN?` 或明确的 RS485
  adapter identity，再开始数据帧验证。
- 代码/构建边界：未新增 raw Flash caller，未改变 V2 Direct A/B、FlashMap 或 journal owner。
- 下一步：先固化 host serial lifecycle 和 RS485 framing/ACK transcript，再接入实际 UART/DMA/
  DE/RE adapter，最后烧录 DHRT100 做非断电闭环。

## 证据索引

| 证据 | 状态 | 说明 |
|---|---|---|
| `tools/ota_stream_send/ota_stream_send.py` | 基线 | 现有 USB CDC stream sender，待增加 RS485 profile |
| `third_party/portable_ota/include/pota_stream_ingress.h` | 基线 | 已定义 RS485 ingress source enum |
| COM11 transcript | 未开始 | 等待 RS485 adapter/工具固化后产生 |
| DHRT100 V2 RS485 OTA | 未开始 | 必须在 host/build gate 后执行 |

## 回退与边界

- 本任务当前不执行 full erase、真实断电或未知地址 Flash 写入。
- 若 RS485 adapter 不能完成 admission，保持 USB CDC 既有回退路径，不修改 Flash owner。
- 所有新提交通过文档门禁后再推送；RS485 status 不改变现有 Registry contract status。
