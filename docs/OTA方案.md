# OTA 升级方案

本文档汇总当前硬件条件，并给出 RP2350_TRIG 的 OTA 工程方案。目标是让固件升级从“手动烧录”升级为“可回滚、可校验、可维护”的产品化流程。

## 已确认硬件条件

- 主控：RP2350。
- 板载启动 Flash：Winbond W25Q32，32 Mbit，即 4 MB。
- 当前版本无 RJ45，不规划网络协议栈、HTTP OTA、TLS OTA。
- 已接入 SCPI，当前可通过 USB CDC / stdio 做控制通道。
- 板上有 TF/SD 卡座，可用于大文件、日志、资源包和离线升级包。
- LCD 是写入方向 SPI：
  - `GPIO8`：LCD DC
  - `GPIO9`：LCD CS
  - `GPIO10`：SPI SCK
  - `GPIO11`：SPI MOSI
  - `GPIO25`：LCD BL
  - LCD 不使用 MISO。
- SD 卡可优先按 SPI 模式接入：
  - `GPIO10`：SPI SCK，与 LCD 共享
  - `GPIO11`：SPI MOSI，与 LCD 共享
  - `GPIO12`：SPI MISO，SD 卡独占
  - `GPIO15`：SPI CS，SD 卡独占
  - `GPIO13/GPIO14` 暂不使用，可留给未来 SDIO 模式。

## 推荐结论

当前版本推荐采用：

```text
W25Q32 内部 A/B 双 App Slot
  +
固定 Bootloader
  +
OTA Metadata 双副本
  +
SCPI over USB CDC 在线传输
  +
SD 卡离线升级包缓存
```

核心原则：

- W25Q32 只保存 Bootloader、A/B App、Metadata、关键配置和少量保留区。
- SD 卡保存大文件：离线 OTA 包、日志、资源、采样摘要、测试报告。
- Bootloader 不依赖 SD 卡启动，避免可插拔介质影响基本可恢复能力。
- App 可以从 SD 卡读取 OTA 包，再写入 W25Q32 inactive Slot。
- SCPI 和 SD 卡只是传输/缓存入口，真正升级状态机由 `ota_manager` 统一管理。

## 不采用的方案

当前阶段不建议：

- 单分区原地升级：断电后容易变砖。
- Bootloader 直接从 SD 卡运行 App：SD 卡可插拔，启动可靠性不足。
- HTTP/网络 OTA：当前硬件无 RJ45，不引入网络协议栈。
- Bootloader 第一版集成 FatFs：会明显增加 Bootloader 复杂度。
- 直接传输 UF2 作为 OTA 格式：不利于版本、签名、目标板校验和边界检查。

## W25Q32 分区表

W25Q32 容量 4 MB，XIP 基地址按 Pico SDK 默认 `0x10000000` 规划。

| 区域 | Flash 偏移 | XIP 地址 | 大小 | 说明 |
|---|---:|---:|---:|---|
| Bootloader | `0x000000` | `0x10000000` | 256 KB | 固定启动程序，普通 OTA 不覆盖。 |
| App Slot A | `0x040000` | `0x10040000` | 1536 KB | 应用固件 A。 |
| App Slot B | `0x1C0000` | `0x101C0000` | 1536 KB | 应用固件 B。 |
| OTA Metadata | `0x340000` | `0x10340000` | 64 KB | 双副本 metadata、升级状态、回滚计数。 |
| Product Config | `0x350000` | `0x10350000` | 64 KB | 产品参数、校准数据、序列号。 |
| Scratch/Reserved | `0x360000` | `0x10360000` | 640 KB | 临时数据、轻量日志、未来扩展。 |
| Flash End | `0x400000` | `0x10400000` | - | 4 MB 结束地址。 |

尺寸约束：

- Bootloader 目标小于 128 KB，分区预留 256 KB。
- 单个 App 镜像硬上限 1536 KB。
- 构建告警阈值建议 1200 KB。
- 构建失败阈值建议 1400 KB。
- 大资源、完整日志、离线升级包不进入 W25Q32 App Slot。

## SD 卡存储规划

SD 卡不参与基本启动链路，但作为大容量维护介质。

建议目录：

```text
/update/                 # 离线 OTA 包
/logs/                   # 运行日志和故障日志
/config/                 # 配置导入导出
/capture/                # 采样摘要或导出文件
/resource/               # UI 图片、扩展字库、帮助文件
/factory/                # 生产测试报告、维护包
```

OTA 相关文件：

```text
/update/rp2350_trig_x.y.z.ota
/update/last_result.txt
```

SD 卡用途边界：

- 可以保存 OTA 包。
- 可以保存较大 UI/日志/数据文件。
- 不能作为唯一 App 存储。
- 不能作为唯一 metadata 存储。
- 不能替代 Product Config 的关键参数区。

## 升级入口

系统支持两个升级入口，但共用同一套 `ota_manager`。

### 在线升级：SCPI over USB CDC

```text
PC ota_send.py
  ↓ USB CDC / SCPI
middleware/scpi_port
  ↓
components/ota_manager
  ↓
drivers/mcu/flash
  ↓
W25Q32 inactive App Slot
```

适合：

- 开发调试。
- 产线升级。
- 现场接电脑维护。
- 没有 SD 卡时升级。

### 离线升级：SD 卡 OTA 包

```text
SD 卡 /update/*.ota
  ↓
storage_manager 扫描和读取
  ↓
components/ota_manager
  ↓
drivers/mcu/flash
  ↓
W25Q32 inactive App Slot
```

适合：

- 现场无专用烧录工具。
- 维护人员拷贝升级包到 SD 卡。
- 大文件传输不占用 USB CDC 长时间链路。

## OTA 状态机

```text
IDLE
  ↓ begin
RECEIVING
  ↓ end
VERIFYING
  ↓ 校验通过
READY_TO_REBOOT
  ↓ reboot
PENDING_CONFIRM
  ↓ app commit
COMMITTED
```

异常路径：

```text
RECEIVING -> ABORTED
RECEIVING -> FAILED
VERIFYING -> FAILED
PENDING_CONFIRM -> ROLLBACK
```

状态定义：

- `IDLE`：无升级任务。
- `RECEIVING`：正在接收镜像并写入 inactive Slot。
- `VERIFYING`：正在校验写入后的 Slot 镜像。
- `READY_TO_REBOOT`：镜像有效，metadata 已标记 pending。
- `PENDING_CONFIRM`：Bootloader 已试运行新 Slot，等待 App commit。
- `COMMITTED`：新 Slot 已确认可用。
- `FAILED`：升级失败，保留原 confirmed Slot。

## Bootloader 行为

Bootloader 职责保持最小：

1. 初始化最小硬件。
2. 读取 OTA Metadata 双副本。
3. 选择启动 Slot。
4. 校验待启动 Slot 的 header 和 CRC32。
5. 跳转 App。
6. 处理 pending Slot 的启动次数和回滚。

第一版 Bootloader 不做：

- SD 卡文件系统。
- U8G2 UI。
- SCPI。
- 复杂日志。
- 网络。

Bootloader 启动策略：

```text
如果存在 pending slot:
    如果 pending slot 校验通过且尝试次数未超限:
        启动 pending slot
    否则:
        回滚 confirmed slot
否则:
    启动 confirmed slot
```

## App Commit 机制

新 App 第一次启动后不能立刻认为升级成功。建议 App 完成以下检查后再 commit：

- 系统时钟正常。
- W25Q32 可读。
- Product Config 可读。
- 看门狗已启用并可喂狗。
- LCD/SCPI/关键外设初始化完成。
- 当前固件版本和 board id 与 metadata 一致。

commit 后：

- `pending_slot` 清空。
- `confirmed_slot` 更新为当前 Slot。
- `boot_attempts` 清零。

如果新 App 未 commit：

- 下次复位时 Bootloader 增加 `boot_attempts`。
- 超过最大尝试次数，例如 3 次，回滚到 confirmed Slot。

## Metadata 设计

Metadata 位于 `0x340000`，使用双副本和 sequence 号。每次写入只更新一个副本，启动时选择 CRC 正确且 sequence 最新的副本。

建议结构：

```c
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t confirmed_slot;
    uint32_t boot_attempts;
    uint32_t rollback_count;
    uint32_t slot_a_size;
    uint32_t slot_a_crc32;
    uint8_t  slot_a_sha256[32];
    uint32_t slot_b_size;
    uint32_t slot_b_crc32;
    uint8_t  slot_b_sha256[32];
    uint32_t metadata_crc32;
} ota_metadata_t;
```

Metadata 写入规则：

- 写入前先构造完整结构体。
- 计算 `metadata_crc32`。
- 擦除目标 metadata 副本所在扇区。
- 写入新副本。
- 读回校验。
- 不直接覆盖最后一个有效副本。

## OTA 镜像格式

不直接传输 UF2，使用项目自定义 OTA 镜像：

```text
rp2350_trig_x.y.z.ota
├─ ota_image_header_t
└─ app raw binary
```

建议头部：

```c
typedef struct {
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t board_id;
    uint32_t image_version;
    uint32_t image_size;
    uint32_t load_offset;
    uint32_t entry_offset;
    uint32_t crc32;
    uint8_t  sha256[32];
    uint32_t flags;
    uint32_t header_crc32;
} ota_image_header_t;
```

第一阶段必须校验：

- magic。
- header size。
- board id。
- image size。
- slot 边界。
- CRC32。
- header CRC32。

第二阶段增强：

- SHA256。
- Ed25519 或 ECDSA 签名。
- 防降级版本号。
- Bootloader 兼容版本。
- 产品型号/硬件版本匹配。

## SCPI OTA 命令

SCPI 只作为传输层，不直接操作 Flash 分区细节。

第一阶段命令：

| 命令 | 说明 |
|---|---|
| `SYST:OTA:STAT?` | 查询 OTA 状态。 |
| `SYST:OTA:BEGIN <size>,<crc32>` | 开始 OTA 传输。 |
| `SYST:OTA:DATA #<block>` | 发送二进制数据块。 |
| `SYST:OTA:END` | 结束传输并触发校验。 |
| `SYST:OTA:ABOR` | 中止升级。 |
| `SYST:OTA:PROG?` | 查询接收进度。 |

Bootloader 完成后启用：

| 命令 | 说明 |
|---|---|
| `SYST:OTA:BOOT` | 重启进入 pending Slot。 |
| `SYST:OTA:COMM` | App 自检通过后确认当前 Slot。 |
| `SYST:OTA:SLOT?` | 查询 active/pending/confirmed Slot。 |
| `SYST:OTA:RES?` | 查询最近一次升级结果。 |

OTA 期间建议：

- 暂停周期日志，避免和 SCPI 二进制块混在同一 CDC 通道。
- 降低 LCD 刷新频率，避免占用共享 SPI 总线。
- 禁止启动高负载采样任务。

## SD 卡离线升级流程

第一版建议由 App 扫描 SD 卡，不让 Bootloader 直接读 SD。

流程：

1. 用户把 `.ota` 文件放入 SD 卡 `/update/`。
2. App 启动后或收到 SCPI 命令后扫描 `/update/`。
3. 选择最新且 board id 匹配的 OTA 包。
4. `ota_manager` 读取文件流。
5. 写入 inactive Slot。
6. 校验通过后设置 pending。
7. 用户确认或命令触发重启。
8. Bootloader 启动新 Slot。
9. App 自检通过后 commit。

建议新增 SCPI 命令：

| 命令 | 说明 |
|---|---|
| `MMEM:CAT? "/update"` | 列出升级目录。 |
| `SYST:OTA:FILE "<path>"` | 从 SD 卡指定文件执行 OTA。 |
| `SYST:OTA:FILE?` | 查询当前选中的 OTA 文件。 |

## SPI 总线策略

LCD 和 SD 卡共享 SPI SCK/MOSI：

| 设备 | SCK | MOSI | MISO | CS |
|---|---:|---:|---:|---:|
| LCD | `GPIO10` | `GPIO11` | 未使用 | `GPIO9` |
| SD 卡 SPI 模式 | `GPIO10` | `GPIO11` | `GPIO12` | `GPIO15` |

要求：

- 任意时刻只能有一个设备 CS 有效。
- SD 卡访问期间 LCD CS 必须保持无效。
- LCD 刷新期间 SD CS 必须保持无效。
- 后续应增加共享 SPI bus lock。
- OTA 写 Flash 期间应暂停 SD 文件读取和 LCD 大块刷新。

## 软件模块规划

建议新增：

```text
bootloader/
components/ota_manager/
components/storage_manager/
drivers/mcu/flash/
drivers/external/sd_card/
middleware/fatfs_port/
tools/ota_packager/
tools/ota_send/
```

职责：

- `bootloader/`：slot 选择、metadata 读取、镜像校验、跳转、回滚。
- `components/ota_manager/`：统一 OTA 状态机、接收数据、写 inactive Slot、校验、设置 pending。
- `components/storage_manager/`：管理 SD 卡挂载、文件枚举、日志和离线升级包。
- `drivers/mcu/flash/`：封装 W25Q32/XIP Flash erase/program/read/verify。
- `drivers/external/sd_card/`：SD 卡 SPI 模式底层驱动。
- `middleware/fatfs_port/`：FatFs 或等价文件系统适配。
- `middleware/scpi_port/`：增加 OTA 命令，只负责传输和命令入口。
- `tools/ota_packager/`：将 App binary 打包为 `.ota`。
- `tools/ota_send/`：通过 USB CDC SCPI 发送 `.ota`。

## Flash 写入注意事项

- RP2350 对 XIP Flash 擦写时，需要遵循 Pico SDK flash 安全流程。
- 擦写 Flash 的关键代码需要在 RAM 中执行或使用 SDK 提供的安全 API。
- 写 Flash 前需要暂停或协调中断、DMA、第二核、USB CDC 和共享 SPI 活动。
- 每次写入后必须读回校验。
- 擦除和写入必须按扇区/页边界处理。
- 不允许 OTA 擦除 Bootloader、Product Config 和当前 confirmed Slot。

## 第一阶段交付范围

第一阶段目标是“USB CDC SCPI OTA 可用，带 A/B 回滚基础能力”。

必须完成：

- W25Q32 分区宏和文档固化。
- `drivers/mcu/flash/` 最小擦写封装。
- `components/ota_manager/` 状态机。
- OTA header 和 CRC32 校验。
- SCPI `SYST:OTA:*` 基础命令。
- App 写 inactive Slot。
- Metadata 双副本。
- 最小 Bootloader：读 metadata、选 Slot、校验、跳转、回滚。
- App commit。
- PC 端 `ota_packager.py` 和 `ota_send.py`。

暂不做：

- SD 卡离线升级。
- 签名验签。
- 防降级。
- Bootloader UI。
- Bootloader 读 SD 卡。

## 第二阶段交付范围

第二阶段目标是“现场维护更友好”。

- 接入 SD 卡 SPI 模式。
- 接入 FatFs。
- 支持从 `/update/*.ota` 离线升级。
- 支持日志写 SD 卡。
- 支持配置导入导出。
- 增加 SCPI 文件管理命令。

## 第三阶段交付范围

第三阶段目标是“安全 OTA”。

- OTA 镜像 SHA256。
- Ed25519/ECDSA 签名。
- 防降级版本策略。
- Bootloader 兼容版本策略。
- 生产密钥管理流程。
- 升级审计记录。

## 当前风险和对策

| 风险 | 对策 |
|---|---|
| 4 MB Flash 空间有限 | App Slot 限制 1.5 MB，大资源放 SD 卡。 |
| SCPI 和日志共用 USB CDC | OTA 期间暂停周期日志，后续拆分日志通道。 |
| LCD 和 SD 共享 SPI | 增加 SPI bus lock 和 CS 互斥。 |
| Bootloader 过复杂 | 第一版 Bootloader 不集成 SD/FatFs/UI/SCPI。 |
| OTA 中断电 | A/B Slot + metadata 双副本 + inactive Slot 写入。 |
| 新固件启动失败 | pending/commit/rollback 机制。 |
| Flash 写入影响运行 | 使用 SDK 安全擦写流程，暂停相关中断和高负载任务。 |

## 推荐落地顺序

1. 增加分区常量和 App 大小检查。
2. 增加 OTA 镜像格式和 `ota_packager.py`。
3. 增加 `drivers/mcu/flash/`。
4. 增加 `components/ota_manager/`。
5. 增加 SCPI `SYST:OTA:*` 接收命令。
6. 增加 Metadata 双副本。
7. 拆出 Bootloader 工程。
8. 修改 App 链接地址，支持 Slot A/B。
9. 完成 USB CDC SCPI OTA 闭环。
10. 增加 SD 卡 SPI + FatFs。
11. 增加 SD 卡离线 OTA。
12. 增加签名、防降级和升级审计。
