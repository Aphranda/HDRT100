# RP2350_TRIG 产品样板迁移方案与待办

Status: Active
Domain: Hardware / Board Bring-up
Target: RP2350B QFN-80 产品样板
Canonical: `docs/hardware/PRODUCT_BOARD_MIGRATION_PLAN.md`
Source of truth: `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`
Related: `docs/storage/SD_TODO.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-08-19

## 1. 目标与验收口径

本迁移以产品网表约束为唯一 GPIO 事实来源，按“静态映射 -> 固件构建 -> 产品样板实测”
三层收口。只有拿到产品样板或仪器证据后才标记为“已板测”；编译通过不能替代电气验收。

状态缩写：

- `M`：board profile/驱动已映射。
- `B`：A/B OTA 固件构建通过。
- `V`：产品样板已验证。
- `P`：仍待实现或待产品样板验证。

固定测试接口：USB CDC/SCPI 使用 `COM3`，CH343 UART0 调试使用 `COM7`。
产品 Flash 为 W25Q128JVSIQ 16 MiB；当前 OTA 保持低 4 MiB 兼容布局。

## 2. GPIO0..47 迁移矩阵

| GPIO | 产品功能 | 当前固件状态 | 产品样板证据 / 下一步 |
|---:|---|---|---|
| 0 | UART0_TX / CH343 | M/B | P：COM7 连续启动日志、收发与重启压力测试。 |
| 1 | UART0_RX / CH343 | M/B | P：与 GPIO0 一并验证输入路径。 |
| 2 | KEY1，低有效，面板中键 | M/B/V | 首件已确认短按、长按和重复事件。 |
| 3 | LED_SYSTEM，低有效 | M/B/V | 三灯点亮已确认；集中策略已实现，P：模式板测。 |
| 4 | UART1_TX / RS485 | M/B | P：接收、发送、DE 时序和总线回环。 |
| 5 | UART1_RX / RS485 | M/B | P：与 GPIO4/13 一并验证。 |
| 6 | KEY2，低有效，面板右键 | M/B/V | 首件已确认短按、长按和重复事件。 |
| 7 | KEY3，低有效，面板左键 | M/B/V | 首件已确认短按、长按事件。 |
| 8 | LED_ARM/TRIGGER，低有效 | M/B/V | 三灯点亮已确认；已接配置/ARM/触发事件，P：模式板测。 |
| 9 | LED_FAULT，低有效 | M/B/V | 三灯点亮已确认；已接故障和 core1 stale，P：锁存/清除板测。 |
| 10 | TF SPI1 SCK | M/B/V | COM3 已验证 SDHC/SDXC、FAT、读写。 |
| 11 | TF SPI1 MOSI | M/B/V | 同 GPIO10。 |
| 12 | TF SPI1 MISO | M/B/V | 同 GPIO10。 |
| 13 | UART1_DE | M/B | P：示波器检查发送前后保护时间，复位默认接收。 |
| 14 | TF card detect | M/B/V | 已报告 card present；P：热插拔与抖动测试。 |
| 15 | TF CS | M/B/V | 已完成目录、读取和 boot snapshot 写入。 |
| 16 | SMA_OUT1 | M/B | P：逐路输出脉冲和通道顺序测试。 |
| 17 | SMA_OUT2 | M/B | P：逐路输出脉冲和通道顺序测试。 |
| 18 | SMA_OUT3 | M/B | P：逐路输出脉冲和通道顺序测试。 |
| 19 | SMA_OUT4 | M/B | P：逐路输出脉冲和通道顺序测试。 |
| 20 | SMA_IN4（物理位0） | M/B | P：输入逐路注入；确认对外 IN1..4 bit reverse。 |
| 21 | SMA_IN3（物理位1） | M/B | P：同 GPIO20。 |
| 22 | SMA_IN2（物理位2） | M/B | P：同 GPIO20。 |
| 23 | SMA_IN1（物理位3） | M/B | P：同 GPIO20。 |
| 24 | TDMA RX / BISS_DATA1_IN | M/B | P：单板电气、双板单跳和闭环测试。 |
| 25 | TDMA TX CLK / BISS_CLK1_OUT | M/B/V | 单板差分回环已完成 15/20/25 MHz 阶梯；P：示波器占空比和边沿测试。 |
| 26 | TDMA TX CS / RJ45_TRIG_OUT | M/B | P：确认 CS 包络、TRIG_DE 时序。 |
| 27 | TDMA RX CS / RJ45_TRIG_IN | M/B | P：确认帧同步捕获与丢帧恢复。 |
| 28 | TDMA RX CLK / BISS_CLK0_IN | M/B | P：双板时序裕量和误码率。 |
| 29 | TDMA TX DATA / BISS_DATA0_OUT | M/B | P：双板帧 CRC、序号和闭环一致性。 |
| 30 | UP_BISS_DE | M/B | P：默认关闭、启用顺序和差分输出实测。 |
| 31 | DN_BISS_DE | M/B | P：默认关闭、启用顺序和差分输出实测。 |
| 32 | TRIG_DE | M/B | P：默认关闭、CS 输出前后时序实测。 |
| 33 | FAULT_IN | P | 增加 board profile、确定极性/上下拉、去抖和故障事件。 |
| 34 | LCD_RST | M/B/V | ST7735S 开机界面已完整显示。 |
| 35 | LCD_BL，低有效 | M/B/V | 背光已确认。 |
| 36 | LCD_DC | M/B/V | 显示读写已确认。 |
| 37 | LCD_CS | M/B/V | 显示读写已确认。 |
| 38 | LCD SPI0 SCK | M/B/V | 原生 80x160、offset `(24,1)` 已确认。 |
| 39 | LCD SPI0 MOSI | M/B/V | 软件旋转后开机界面无斜切。 |
| 40 | DN_BISS_/RE | M/B | P：默认接收使能和关闭接收实测。 |
| 41 | TRIG_/RE | M/B | P：默认接收使能和关闭接收实测。 |
| 42 | UP_BISS_/RE | M/B | P：默认接收使能和关闭接收实测。 |
| 43 | ADC3 / BOARD_TEMP1 | P | 增加 ADC 采样、标定、范围检查和 SCPI/Vector 摘要。 |
| 44 | ADC4 / BOARD_CUR1 | P | 增加 ADC 采样、零点/增益标定和过范围诊断。 |
| 45 | NC / ADC5 | P | 显式初始化为未用输入，禁止周期 ADC 采样。 |
| 46 | NC / ADC6 | P | 显式初始化为未用输入，禁止周期 ADC 采样。 |
| 47 | NC / ADC7 | P | 显式初始化为未用输入，禁止周期 ADC 采样。 |

专用接口状态：

- USB CDC `COM3` 已完成 OTA 和 SCPI 通信验证。
- `COM3` 当前运行并已 commit build `20260818160934`（25 MHz TDMA、集中 LED 策略）；
  `SYST:UI:KEYS?` 已证明三键观测命令和左/中/右物理映射在产品样板生效。
- `SYST:LED:STAT?` 已返回 `NORMAL / HEARTBEAT / OFF / OFF`，配置门、SD 与 core1
  健康位均正常；连续查询捕获到绿灯每秒短亮且红黄保持灭。ARM/事件/故障模式仍待板测。
- W25Q128JVSIQ 已按 16 MiB 编译并 OTA 运行；高 12 MiB 暂不纳入本轮迁移。
- LCD SPI0 与 TF SPI1 已拆分，StorageAO 不再把 SD 与 LCD SPI0 互斥。

## 3. 分阶段实施顺序

### P0：先消除阻塞和长延迟

- [x] 产品 GPIO、Flash、ST7735S、TF SPI1 基础映射。
- [x] COM3 OTA、LCD 开机界面、三 LED 和 TF 基础读写验证。
- [x] 修正 `sd_board_validate.py` 对扩展 `SYST:LOG:STAT?`、CDC 日志粘连和分类结论的解析。
- [x] 将运行日志改为 `/logs/runtime` 128 槽固定环，停止访问数千文件的遗留 `/logs/run`。
- [x] 在 INFO 日志开启时重跑 SD/boot snapshot 验证，独立报告功能失败和卡内容缺失。
- [ ] 补齐 `/update/RP2350_TRIG_UPDATE.pkg` 后完成 System Pack 内容验收。

验收：构建和静态检查通过；COM3 上 card/FAT/read/write 均成功；后台日志写入不再造成
秒级 SCPI 响应错位；连续运行期间日志目录大小有明确上限。

### P1：人机接口与基础 IO

- [x] 建立三个 KEY 的统一事件层：35 ms 去抖、按下/释放、短按、700 ms 长按和 250 ms 重复。
- [x] 固定导航语义：KEY1 上一项/长按返回，KEY2 展开/收起详情，KEY3 下一项/长按连翻。
- [x] 将主页重构为 160x80 单卡片四行布局，不再选择旧 240x135 三列渲染器。
- [x] 增加 `SYSTem:UI:KEYs?` 一致性快照，提供 raw/stable mask、最后事件和逐键
  short/long/repeat 启动累计计数；已在 COM3 确认初始快照可查询。
- [x] 首件确认网表 KEY1/KEY2/KEY3 并非面板从左到右顺序；UI/SCPI 已按实测映射为
  左 GPIO7(KEY3)、中 GPIO2(KEY1)、右 GPIO6(KEY2)，底栏改用 `L/C/R` 避免歧义。
- [x] LCD 独占 SPI0 后将周期状态刷新从 250 ms（4 Hz）提升到 100 ms（10 Hz）；
  保持已验证的 LCD SPI0 10 MHz 时钟，按键脏帧和四步切页动画继续即时刷新。
- [x] 建立三灯集中 owner 和 20 ms 模式发生器；增加 `SYSTem:LED:STATus?` 只读维护
  快照，并用 core1 循环进度检测实时核连续 3 秒无心跳的致命状态。
- [ ] 产品样板依次实测三个按键的短按、长按、重复与页面方向，确认无连击和误触。
- [ ] 验证 LED_SYSTEM、LED_ARM/TRIGGER、LED_FAULT 的状态优先级。
- [ ] SMA OUT1..4 逐路输出验证；SMA IN1..4 逐路输入和 bit reverse 验证。双板
  8x8 线序矩阵已完成 OUT1/IN1 双向链路板测：build `20260819072041`，3/3 次
  稳定检测到 `0010071E65B5CB38.OUT1 -> A1E549202D18ED6A.IN1` 和
  `A1E549202D18ED6A.OUT1 -> 0010071E65B5CB38.IN1`，其余 62 个交叉点均为 0，
  输出回落与释放正常；build `20260819091137` 又以 100 us 硬件计数门完成
  1/2/5/10/15/20/25/30/35/40/45/50 MHz 双向扫频，每点重复 10 次，A->B 与
  B->A 均稳定通过到 50 MHz（短门启停开销口径为 +/-5%）；其余通道仍待逐路接线扫描。
- [x] 增加单板 `sma_single_board_loopback.py` 验收工具，固定检查产品 GPIO profile、
  四路同名直连和全 0/全 1 电平，并在退出时恢复输出 persona。
- [ ] UART1/RS485 收发和 GPIO13 DE 时序验证。

验收：每个按键事件可由日志/SCPI 观测且无连击；页面内容完整；SMA 外部通道号与
SCPI 通道号一致；RS485 回环无方向冲突。

三键 SCPI 验收查询：

```text
SYSTem:UI:KEYs?
SYSTem:LED:STATus?
```

三灯优先级固定为 `FATAL > FAULT > OTA > ARMED > DEGRADED > NORMAL > BOOT`：

- 绿灯：BOOT/OTA 100 ms 快闪；配置门未 ready 双闪；正常或 ARMED 时每秒 100 ms 心跳。
- 黄灯：未配置熄灭；已配置未 ARM 慢闪；ARM 后常亮；检测到触发计数增加后短灭
  120 ms，未 ARM 时则短亮 120 ms；连续高速事件按 500 ms 最小间隔限频显示，计数不丢。
- 红灯：诊断锁存、Trigger FAULT、OTA FAILED 或 Storage FAILED 时常亮；core1 在启动宽限
  后连续 3 秒无循环进展时快闪并记录 `led_health/core1 heartbeat stale`。
- 可选 SD 缺卡、无文件系统和普通路径/作业错误不直接点亮红灯，避免把维护介质状态误报为
  系统硬故障；`sd_ready` 仍可从 LED 快照读取。

模式枚举：`0 OFF / 1 ON / 2 HEARTBEAT / 3 SLOW_BLINK / 4 FAST_BLINK /
5 DOUBLE_BLINK / 6 EVENT_PULSE`。策略枚举：`0 BOOT / 1 NORMAL / 2 DEGRADED /
3 ARMED / 4 OTA / 5 FAULT / 6 FATAL`。`health_flags bit0=CORE1_STALE`，该位本次启动
内锁存；当前通过重启清除。普通业务模块不得直接抢占三灯，初始化/RTOS 失效时仅保留
`app_runtime_fault_forever()` 对绿灯的最低层 100 ms 翻转兜底。

SMA 单板回环接线与命令：

```text
OUT1 -> IN1
OUT2 -> IN2
OUT3 -> IN3
OUT4 -> IN4
```

```powershell
python tools\sma_loopback_validate\sma_single_board_loopback.py COM3 `
  --expected-build 20260818155435
```

SMA 双板线序矩阵命令（COM 仅作为临时传输端点，板卡始终按 `*IDN?` 唯一地址识别）：

```powershell
python tools\sma_loopback_validate\sma_two_board_matrix.py `
  --board-a-id 0010071E65B5CB38 `
  --board-b-id A1E549202D18ED6A `
  --expected-build 20260819072041 --cycles 3
```

SMA OUT1/IN1 双板双向频率扫描命令：

```powershell
python tools\sma_loopback_validate\sma_two_board_frequency_sweep.py `
  --board-a-id 0010071E65B5CB38 `
  --board-b-id A1E549202D18ED6A `
  --expected-build 20260819091137 --gate-us 100 --repeats 10 `
  --max-error-ppm 50000
```

### P2：同步链路与板级诊断

- [ ] 验证 GPIO30..32 DE 和 GPIO40..42 `/RE` 的安全默认态与切换时序。
- [x] TDMA 单板网线回环：产品板输出 RJ45 回接输入 RJ45，验证
  GPIO26 CS -> GPIO27 CS、GPIO25 CLK -> GPIO28 CLK、GPIO29 TX -> GPIO24 RX。
- [x] 单板回环已确认 PIO SPI 帧/序号和持续收发：15 s 内 TX/RX 各增长 7230，
  adapter/phys bad、magic fail、overrun 零增长。
- [x] 速率阶梯到 25 MHz：60 s 内 TX/RX 各增长 29721，所有链路错误零增长；
  当前仅作为短线单板裕量证据，量产默认频率待长线缆/干扰/多板测试后确定。
- [ ] 继续检查 TRIG/UP/DN 三组 DE 与 `/RE` 的方向切换；完成硬件 timestamp
  latch 后再要求 `simultaneous_feedback_loop_evidence=1`，随后进入双板测试。
- [x] 增加 `tdma_single_board_loopback.py` 单板闭环验收工具；默认先执行
  `STOP -> LOCAL 0 -> ARM -> TRAIN 4096 -> START` 再做只读检查。未接网线基线
  已确认 TX 约 500 frame/s、RX=0、`down_running=0`，且 10 MHz 与
  GPIO26/25/29 -> 27/28/24 profile 正确。
- [ ] 两板单跳验证：10 MHz、帧 CRC、序号、超时和恢复。
- [ ] 两板/多板闭环验证：转发、身份、时戳相关和长期误码。
- [ ] 接入 GPIO33 `FAULT_IN`，验证故障锁存、快照、trace 和恢复。
- [ ] 接入 GPIO43/44 ADC 遥测；明确 GPIO45..47 disabled。

验收：上电和复位期间所有差分驱动器关闭；PIO 初始化后才允许 DE；TDMA 长稳无资源
冲突；FAULT/温度/电流可观测且不会进入硬实时阻塞路径。

单板网线回环命令：

```powershell
python tools\tdma_ring_monitor\tdma_single_board_loopback.py COM3 `
  --duration-s 60 --expected-build 20260819130134 --expected-baud-hz 25000000
```

## 4. 风险、保护与回滚

- GPIO24..29 只能由 TDMA PIO2 persona 使用；legacy AUX/BiSS tap/RJ45 marker 保持禁用。
- GPIO4..7 的 legacy debug overlay 保持禁用，避免覆盖 UART1 和 KEY2/KEY3。
- GPIO20..23 物理顺序为 IN4..IN1；任何修改都必须保留对外 bit reverse。
- ISO1452 DE 必须默认低，`/RE` 默认低；切换 PIO/引脚功能前先关闭 DE。
- SD 和 LCD 分别独占 SPI1、SPI0；修改资源仲裁后必须同时跑 LCD 刷新与 SD 写入回归。
- Flash 分区本轮不扩展到高 12 MiB；若回滚，使用已推送基线
  `6fdc505 Migrate product board IO and ST7735S display`。
- 禁止自动删除样板卡现有日志。日志布局升级先停止向旧大目录追加，再由 PC 工具离线归档。

## 5. 板测证据记录模板

每项板测至少记录：固件 build id、Git commit、样板编号、端口、接线、命令、测量值、
PASS/FAIL 和原始输出路径。实测通过后同时更新本矩阵及对应域的 task progress，禁止只在
聊天记录中声明完成。
