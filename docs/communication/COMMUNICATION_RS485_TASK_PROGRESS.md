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
- 协议决策：RS485 当前优先 Modbus RTU；SCPI 通过 USB 配置 `COMMunication:SERial:UART#:MODE`
  在 `SCPI`/`MODBUS` 间选择。默认保持 SCPI 兼容，Modbus adapter 接入前状态仍为
  `PENDING_BACKEND`。
- 联调资源：COM11 仅表示 RS485 通讯器端口；联调脚本必须先确认 `*IDN?` 或明确的 RS485
  adapter identity，再开始数据帧验证。
- 代码/构建边界：未新增 raw Flash caller，未改变 V2 Direct A/B、FlashMap 或 journal owner。
- 下一步：先固化 host serial lifecycle 和 RS485 framing/ACK transcript，再接入实际 UART/DMA/
  DE/RE adapter，最后烧录 DHRT100 做非断电闭环。

## COMM-RS485-20260824-002 - USB SCPI 模式配置与 DHRT100 复验

- 状态：USB SCPI 配置子项完成；RS485 Modbus/SCPI 数据面 adapter 仍未完成。
- 代码：新增 `COMMunication:SERial:UART#:MODE <SCPI|MODBUS>` / `MODE?`，默认 `SCPI`；setter
  返回标准 accepted ACK，非法模式 fail closed。`MODE=MODBUS` 只保留扩展选择，不宣称 Modbus
  backend 已接入。
- 构建：`cmake --build --preset pico2-v2-factory-candidate -j 4` 通过，Boot/App A/App B/
  Recovery link gates 通过；产物为该 preset 生成的 `DHRT100_V2_CANDIDATE_FACTORY.uf2`。
- DHRT100：serial `839E1AE79EA20F31` 通过固化 `tools/picotool_flash/picotool_flash.py`
  load/verify（未执行 full erase）；USB `COM8` 的 `rs485_mode_probe.scpi` 返回：
  `*IDN?=GTS,DHRT100,...`、`MODE?=1,"SCPI"`、`MODE SCPI=1`、
  `STATus?=...,"PENDING_BACKEND"`、`SYSTem:ERRor?=0,"No error"`。
- 原始证据：`out/flash_hil/dhrt100_rs485_scpi_mode_flash_fix_20260824.txt`、
  `out/flash_hil/dhrt100_rs485_scpi_mode_probe_fix_20260824.txt`。
- 边界：COM11 RS485 通讯器尚未发送 Modbus 帧；未执行 RS485 UART/DMA/DE/RE 数据面、真实
  Modbus loopback、OTA 或断电验证。

## COMM-RS485-20260824-003 - DHRT100 UART1 SCPI 短帧闭环

- 状态：SCPI 模式下的 UART1/RS485 最小 backend 完成；Modbus adapter、双端 COM11 物理回环和
  OTA 数据面仍未完成。
- 代码：新增 `drv_rs485`，初始化 DHRT100 的 UART1 与 DE 控制，提供 bounded TX/RX、方向
  lease、计数和错误统计；USB SCPI `TX:TEST` 通过该 driver 发送测试帧，`TX:TEST?`、
  `RX:COUNt?`、`ERRor?` 投影只读状态。SCPI 输入由 RS485 接收行后经 `scpi_port_execute()` 回写。
- 构建/烧录：V2 factory-candidate 构建、Boot/App A/App B/Recovery link gates 通过；使用
  固化 picotool 流程向 DHRT100 serial `839E1AE79EA20F31` load/verify，未执行 full erase。
- DHRT100：`*IDN?` 正常；`UART1:MODE SCPI` 返回 `1`，`TX:TEST 8,85` 返回 `1`，
  `TX:TEST?` 返回 `1,8,"READY"`，`RX:COUNt?`/`ERRor?` 无错误，`SYSTem:ERRor?` 为无错误。
  原始记录见 `out/flash_hil/dhrt100_rs485_scpi_loopback_service_fix_20260824.txt` 和
  `out/flash_hil/dhrt100_rs485_scpi_service_fix_flash_20260824.txt`。
- COM11：当前通讯器直连未得到 `*IDN?` 响应，记录为
  `out/flash_hil/dhrt100_rs485_com11_idn_after_service_20260824.txt`；这表示外部线路/协议
  回环尚未建立，不把单板 TX 成功误判为双端闭环。

## COMM-RS485-20260824-004 - COM11 本地回显处理与烧录证据

- 主工程 `pico2-v2-factory-candidate`、RS485 Python 回归和 FlashMap/link gate 均通过；固化
  工件为 `out/build/pico2-v2-factory-candidate/DHRT100_V2_CANDIDATE_FACTORY.uf2`。
- `drv_rs485` 增加 bounded TX echo candidate、SCPI 响应 echo frame 和 idle-gap 状态；
  `scpi_query.py` 增加 `TX:TEST` 可打印回环前缀过滤和 `--inter-command-delay`，避免把
  `0x55` 回环字节与 SCPI ACK 粘连误判。相应 Python 测试 5 项全通过。
- DHRT100 曾完成 picotool load/verify，COM11 仍观察到 `TX:TEST 8,85 => 1`，但随后板端
  `SYST:ERR?` 仍出现 `-101,"Invalid character"`；因此本项不宣称闭环完成。
- 最后一轮 picotool 在 98% verify 时 USB 连接中断，当前 DHRT100 未重新枚举（需要重新
  进入 BOOTSEL 后再做恢复烧录）。未执行 full erase 或断电。
- 下一步：按硬件 DE/RE 共控约束迁移到 UART RX idle event + DMA ping-pong buffer，先在 host
  做 frame/idle 回归，再重新取得 DHRT100 COM11 的 `SYST:ERR?=0,"No error"` 证据。

## COMM-RS485-20260824-005 - UART1 DMA 双缓冲骨架

- `drv_rs485` 已加入两块固定大小 RX DMA buffer、DMA 链式切换和 `DMA_IRQ_1` 完成事件；
  `DMA_IRQ_0` 保留给 SYNC_IO，不改变 HAOFV 实时 owner。无可用 DMA 通道时自动回退到 UART
  FIFO polling。
- RX service 只消费已完成 buffer，echo candidate/response frame 过滤仍在 driver owner 内，
  不向 SCPI handler 暴露 DMA/Flash 细节；主工程和 link gate 已通过。
- 当前仅有 host/build 证据，DHRT100 尚未重新枚举，尚未宣称 DMA 双缓冲板端闭环；恢复
  BOOTSEL 后必须重新取得 COM11 transcript、`SYST:ERR?` 清零和 DMA overrun 统计证据。
- 已补充未满 DMA buffer 的 idle-gap partial drain；短 SCPI 行不再等待整块 buffer 完成，
  仍由 `drv_rs485_read()` 的有界 service 消费，避免 DMA 接入后控制面无响应。

## 证据索引

| 证据 | 状态 | 说明 |
|---|---|---|
| `tools/ota_stream_send/ota_stream_send.py` | 基线 | 现有 USB CDC stream sender，待增加 RS485 profile |
| `third_party/portable_ota/include/pota_stream_ingress.h` | 基线 | 已定义 RS485 ingress source enum |
| `tools/scpi_query/rs485_mode_probe.scpi` | 已固化 | 通过 USB SCPI 选择/查询 UART1 的 SCPI 模式并读取 backend 状态 |
| COM11 transcript | 未开始 | 等待 RS485 adapter/工具固化后产生 |
| DHRT100 V2 RS485 OTA | 未开始 | 必须在 host/build gate 后执行 |

## 回退与边界

- 本任务当前不执行 full erase、真实断电或未知地址 Flash 写入。
- 若 RS485 adapter 不能完成 admission，保持 USB CDC 既有回退路径，不修改 Flash owner。
- 所有新提交通过文档门禁后再推送；RS485 status 不改变现有 Registry contract status。
