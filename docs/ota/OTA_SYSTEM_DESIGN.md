# OTA 升级方案

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_SYSTEM_DESIGN.md`
Related: `docs/ota/OTA_TODO.md`, `docs/ota/OTA_AB_SWITCH_DESIGN.md`, `docs/ota/OTA_COPY_TRANSACTION_DESIGN.md`, `docs/ota/OTA_PORTABLE_ARCHITECTURE.md`
Last updated: 2026-07-07

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

当前版本采用：

```text
W25Q32 固定 Bootloader + Slot A 运行区 + Slot B 暂存区
  +
OTA Metadata 双副本
  +
SCPI over USB CDC 在线传输
  +
SD 卡离线升级包缓存
```

核心原则：

- W25Q32 保存 Bootloader、Slot A App 运行镜像、Slot B OTA 暂存镜像、Metadata、关键配置和少量保留区。
- SD 卡保存大文件：离线 OTA 包、日志、资源、采样摘要、测试报告。
- Bootloader 不依赖 SD 卡启动，避免可插拔介质影响基本可恢复能力。
- App 可以从 SCPI 或 SD 卡读取统一 `.pkg` 包，按当前 OTA 模式选择内部镜像；raw `.bin` 仅保留兼容/台架用途。
- App 固定链接到 Slot A 地址 `0x10040000`，Slot B 只作为 staging 区。Bootloader 校验 Slot B 后复制到 Slot A，再跳转 Slot A。
- SCPI 和 SD 卡只是传输/缓存入口，真正升级流程由 `OtaAO + OtaFB + OtaVector` 统一管理。
- OTA 按 `docs/arch/HAOFV_ARCHITECTURE.md` 的 HAOFV 架构落地：Active Object 管运行，轻量 IEC 61499 功能块管逻辑，Vector Blackboard 管数据，Resource Arbiter 管互锁。

## 当前可执行流程

### 首次烧录

首次烧录必须使用 factory image，它包含 Bootloader 和 Slot A App：

```text
build/RP2350_TRIG_FACTORY.uf2
```

不再使用 `build/RP2350_TRIG.uf2` 作为主产物。Slot A App 链接地址不是 Flash 起始地址，因此它不能作为普通 UF2 单独拖拽烧录。

### 在线 OTA

OTA 发送标准 raw firmware `.bin`：

```powershell
python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin
```

发送过程：

```text
SYST:OTA:BEGIN <size>,<crc32-decimal>
SYST:OTA:DATA #<block>
SYST:OTA:END
SYST:OTA:BOOT
```

状态预期：

```text
IDLE -> CHECK_PERMISSION -> ERASE_SLOT -> RECEIVING -> VERIFYING -> MARK_PENDING -> READY_TO_REBOOT
```

`SYST:OTA:BOOT` 会触发 watchdog reboot。复位后 Bootloader 会读取 pending metadata，校验 Slot B，复制到 Slot A，清除 pending，然后跳转 Slot A。

### 当前限制

- 当前 Bootloader 已具备 Slot B 到 Slot A 的 copy-to-active 升级路径。
- 当前只实现最小确认策略：复制成功后直接标记 Slot A confirmed。
- 后续仍需增强启动后 App 自检确认、失败回滚、升级计数、掉电恢复和签名校验。

## HAOFV 架构下的 OTA 细化设计

OTA 域按以下结构拆分：

```text
SCPI / UI / SD
  ↓
OtaAO                       # OTA 主动对象：事件队列、预算、资源申请、对外 API
  ↓
OtaFB                       # OTA 主流程 ECC 状态机
  ├─ FlashJobFB             # Flash 擦除/写入/读回校验分步任务
  ├─ MetadataFB             # metadata 双副本读写和选择
  ├─ ImageVerifyFB          # raw bin CRC/向量表校验
  └─ BootHandoffFB          # pending/commit/rollback 交接
  ↓
OtaVector                   # OTA 状态摘要、进度、错误码
  ↓
drv_flash / bootloader
```

职责边界：

| 模块 | 职责 | 禁止事项 |
|---|---|---|
| `middleware/scpi_port` | 解析 SCPI 命令，投递 OTA 事件，返回状态快照 | 不直接擦写 Flash，不直接改 OTA 状态 |
| `storage_manager` | 后续从 SD 卡读取统一 `.pkg` 或兼容 `.bin` 固件流，投递数据块事件 | 不解析 Bootloader metadata |
| `OtaAO` | 接收事件、申请资源、限时调度内部 FB | 不直接暴露内部状态指针 |
| `OtaFB` | OTA 主状态转移、错误归因、进度发布 | 不直接调用 Pico SDK Flash API |
| `FlashJobFB` | 将擦除、写入、读回校验拆成可调度小任务 | 不跨越分区边界 |
| `MetadataFB` | 双副本 metadata 读写、sequence、CRC | 不覆盖最后一个有效副本 |
| `ImageVerifyFB` | raw bin CRC 和 App 向量表校验 | 不执行跳转 |
| `Bootloader` | 启动选择、最终镜像校验、跳转/搬运、回滚 | 第一版不读 SD，不跑 SCPI/UI |

## OtaVector 设计

`OtaVector` 只放状态摘要和进度，不保存固件数据块。

建议定义：

```c
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECK_PERMISSION,
    OTA_STATE_ERASE_SLOT,
    OTA_STATE_RECEIVING,
    OTA_STATE_VERIFYING,
    OTA_STATE_MARK_PENDING,
    OTA_STATE_READY_TO_REBOOT,
    OTA_STATE_PENDING_CONFIRM,
    OTA_STATE_COMMITTED,
    OTA_STATE_FAILED,
    OTA_STATE_ABORTED,
} ota_state_t;

typedef enum {
    OTA_SLOT_NONE = 0,
    OTA_SLOT_A,
    OTA_SLOT_B,
} ota_slot_t;

typedef struct {
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t state;
    uint32_t target_slot;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t crc32_expected;
    uint32_t crc32_running;
    uint32_t image_version;
    uint32_t progress_permille;
    uint32_t boot_flags_summary;
    uint32_t error_code;
    uint32_t last_event;
    uint32_t last_result;
} ota_vector_t;
```

写权限：

- `OtaVector.state` 只能由 `OtaFB` 更新。
- `OtaVector.progress_permille` 只能由 `OtaAO/OtaFB` 根据接收进度更新。
- SCPI/UI/Storage 只能通过 `ota_ao_post_event()` 投递命令。
- `SystemVector` 只读取 `OtaVector` 快照并汇总，不反向修改。

## OTA 事件模型

OTA 事件只描述意图或数据到达，不直接执行重操作。

```c
typedef enum {
    OTA_EVENT_BEGIN = 0,
    OTA_EVENT_DATA_BLOCK,
    OTA_EVENT_END,
    OTA_EVENT_ABORT,
    OTA_EVENT_VERIFY,
    OTA_EVENT_COMMIT,
    OTA_EVENT_BOOT,
    OTA_EVENT_TICK,
    OTA_EVENT_FLASH_JOB_DONE,
    OTA_EVENT_FLASH_JOB_FAILED,
    OTA_EVENT_BOOT_RESULT,
} ota_event_type_t;
```

事件载荷：

| 事件 | 载荷 | 来源 |
|---|---|---|
| `BEGIN` | `size, crc32, image_version, flags` | SCPI/UI/SD |
| `DATA_BLOCK` | `data pointer, length, block_index` | SCPI/Storage |
| `END` | 无或最终 size/crc 摘要 | SCPI/Storage |
| `ABORT` | 用户原因码 | SCPI/UI |
| `VERIFY` | 可选强制校验标志 | SCPI/UI |
| `COMMIT` | 当前 App 自检结果 | App/System Manager |
| `BOOT` | 重启请求 | SCPI/UI |
| `TICK` | 周期调度 | System Manager |
| `FLASH_JOB_DONE` | job id/result | FlashJobFB |
| `BOOT_RESULT` | pending/rollback/confirmed | Bootloader handoff |

## OtaFB ECC 状态转移表

OTA 主状态机采用表驱动 ECC。表项只描述状态、事件、条件和动作，耗时动作通过 Flash Job 分步执行。

| 当前状态 | 事件 | 条件 | 动作 | 下一状态 |
|---|---|---|---|---|
| `IDLE` | `BEGIN` | 系统允许 OTA，Trigger 空闲，镜像大小合法 | 选择 inactive slot，申请 `FLASH+USB` 或 `FLASH+SPI0+SD` | `CHECK_PERMISSION` |
| `CHECK_PERMISSION` | `TICK` | 资源申请成功 | 创建擦除任务 | `ERASE_SLOT` |
| `CHECK_PERMISSION` | `TICK` | 资源被占用或系统非维护模式 | 记录 busy | `FAILED` |
| `ERASE_SLOT` | `FLASH_JOB_DONE` | 擦除完成 | 初始化 CRC 和接收游标 | `RECEIVING` |
| `ERASE_SLOT` | `FLASH_JOB_FAILED` | 任意失败 | 释放资源，记录错误 | `FAILED` |
| `RECEIVING` | `DATA_BLOCK` | 块连续且边界合法 | 创建写入/读回校验 job，更新 received | `RECEIVING` |
| `RECEIVING` | `END` | 接收大小等于 expected | 创建整镜像校验任务 | `VERIFYING` |
| `RECEIVING` | `ABORT` | 任意 | 释放资源，保留 confirmed slot | `ABORTED` |
| `VERIFYING` | `FLASH_JOB_DONE` | raw bin CRC 和向量表通过 | 写 metadata pending | `MARK_PENDING` |
| `VERIFYING` | `FLASH_JOB_FAILED` | 校验失败 | 释放资源，记录错误 | `FAILED` |
| `MARK_PENDING` | `FLASH_JOB_DONE` | metadata 写入并读回通过 | 发布 ready | `READY_TO_REBOOT` |
| `READY_TO_REBOOT` | `BOOT` | 用户确认或策略允许 | 设置重启请求 | `READY_TO_REBOOT` |
| `PENDING_CONFIRM` | `COMMIT` | App 自检通过 | 清 pending，写 confirmed | `COMMITTED` |
| 任意非空闲 | `ABORT` | 未进入不可中止阶段 | 释放资源，记录中止 | `ABORTED` |

不可中止阶段：

- Metadata 正在擦写时不能立即中断，只能设置 abort pending，等待当前 metadata 写入事务完成。
- Bootloader 已接管后 App 不再处理 abort。

## FlashJobFB 设计

Flash 操作必须从 OTA 主状态机中拆出去，避免表项回调隐藏阻塞。

建议 job 类型：

```c
typedef enum {
    FLASH_JOB_NONE = 0,
    FLASH_JOB_ERASE_SLOT,
    FLASH_JOB_PROGRAM_BLOCK,
    FLASH_JOB_READBACK_VERIFY,
    FLASH_JOB_VERIFY_IMAGE_CRC,
    FLASH_JOB_WRITE_METADATA,
} flash_job_type_t;
```

每次 `ota_ao_service(budget_us)` 最多执行一个可控片段：

| Job | 分片建议 |
|---|---|
| `ERASE_SLOT` | 每次擦 1 个 4 KB sector |
| `PROGRAM_BLOCK` | 每次写 256 B 或 512 B |
| `READBACK_VERIFY` | 每次读回 256 B 或 1 KB |
| `VERIFY_IMAGE_CRC` | 每次计算 1 KB 到 4 KB |
| `WRITE_METADATA` | 一个 metadata 副本作为原子事务处理 |

预算建议：

- 正常运行模式：不允许执行 Flash 写任务。
- 维护模式：`ota_ao_service()` 单次预算建议 `500 us` 到 `2000 us`。
- OTA 专用模式：可以放宽预算，但仍应周期喂狗和服务 USB CDC。

## 资源仲裁规则

OTA 开始前必须通过 `system_manager/resource_arbiter` 检查。

允许 OTA 的最低条件：

- `system_mode == SYSTEM_MODE_MAINTENANCE` 或 `SYSTEM_MODE_OTA`。
- `sync_trigger` 未 armed。
- 输入采样未运行，或已明确停止。
- 同步时钟未运行，或已明确停止。
- 没有正在进行的 SD/LCD SPI 大块传输。
- Watchdog 已启用且 OTA 调度能周期喂狗。

资源申请：

| OTA 来源 | 资源 |
|---|---|
| SCPI 在线 OTA | `FLASH + USB` |
| SD 离线 OTA | `FLASH + SPI0 + SD` |
| metadata commit | `FLASH` |

资源释放：

- `FAILED`、`ABORTED`、`READY_TO_REBOOT` 均必须释放运行期资源。
- `READY_TO_REBOOT` 只保留 metadata 中的 pending 状态，不继续占用 Flash。

## SCPI 到 OTA 事件映射

SCPI 不直接改状态，只投递事件或读取 `OtaVector` 快照。

| SCPI 命令 | OTA 事件/API | 返回 |
|---|---|---|
| `SYST:OTA:STAT?` | `ota_ao_get_vector()` | `state,target_slot,error,last_result` |
| `SYST:OTA:PROG?` | `ota_ao_get_vector()` | `received,expected,progress_permille` |
| `SYST:OTA:BEGIN <size>,<crc32>` | `OTA_EVENT_BEGIN` | `OK` 或 SCPI error |
| `SYST:OTA:DATA #<block>` | `OTA_EVENT_DATA_BLOCK` | `OK` 或 SCPI error |
| `SYST:OTA:END` | `OTA_EVENT_END` | `OK` |
| `SYST:OTA:ABOR` | `OTA_EVENT_ABORT` | `OK` |
| `SYST:OTA:BOOT` | `OTA_EVENT_BOOT` | `OK` |
| `SYST:OTA:COMM` | `OTA_EVENT_COMMIT` | `OK` |
| `SYST:OTA:SLOT?` | metadata snapshot | `active,pending,confirmed` |
| `SYST:OTA:RES?` | result snapshot | `last_result,last_error` |

二进制块约束：

- 第一阶段建议 SCPI `DATA` 块大小为 256 B 或 512 B。
- 上位机可按 1 KB 或 4 KB 文件读取，但发送到设备时拆成设备可控块。
- 每个 `DATA` 事件必须带长度，后续可扩展 block index 和 block CRC。
- OTA 期间暂停周期日志，避免日志与 SCPI binary block 混在同一 USB CDC 通道。

## Bootloader 交接细化

Bootloader 与 App 通过 metadata 和可选 SRAM boot request 交接。

### App 写入 pending

App 校验镜像成功后，`MetadataFB` 写入：

- `pending_slot = target_slot`
- `pending_size`
- `pending_crc32`
- `pending_version`
- `boot_attempts = 0`
- `sequence = old_sequence + 1`
- `metadata_crc32`

### Bootloader 启动

Bootloader 上电后：

1. 读取 metadata 双副本。
2. 选择 CRC 正确且 sequence 最新的副本。
3. 如果存在 pending slot，先校验 pending slot。
4. pending 校验通过且尝试次数未超限，则启动 pending。
5. pending 失败或尝试次数超限，则回滚 confirmed slot。
6. 没有 pending 时启动 confirmed slot。

### App commit

新 App 第一次启动后，必须完成最小自检再 commit：

- 系统时钟正常。
- Flash 可读。
- Product Config 可读。
- 看门狗可用。
- SCPI 或最小诊断接口可用。
- 当前 App 版本和 metadata 记录一致。

commit 后：

- `confirmed_slot = current_slot`
- `pending_slot = OTA_SLOT_NONE`
- `boot_attempts = 0`
- `rollback_count` 不变
- 写入新的 metadata 副本

## OTA 错误码建议

错误码需要可通过 SCPI、LCD 和日志统一解释。

| 错误码 | 含义 |
|---|---|
| `OTA_ERR_NONE` | 无错误 |
| `OTA_ERR_BUSY` | 资源忙或系统不允许 OTA |
| `OTA_ERR_INVALID_STATE` | 当前状态不接受该事件 |
| `OTA_ERR_IMAGE_TOO_LARGE` | 镜像超过 slot 大小 |
| `OTA_ERR_BAD_HEADER` | 镜像格式错误或保留错误码 |
| `OTA_ERR_BOARD_MISMATCH` | board id 不匹配 |
| `OTA_ERR_VERSION_REJECTED` | 版本策略拒绝 |
| `OTA_ERR_FLASH_ERASE` | Flash 擦除失败 |
| `OTA_ERR_FLASH_PROGRAM` | Flash 写入失败 |
| `OTA_ERR_READBACK` | 读回校验失败 |
| `OTA_ERR_CRC` | 镜像 CRC 校验失败 |
| `OTA_ERR_VECTOR` | App 向量表校验失败 |
| `OTA_ERR_METADATA` | metadata 写入或校验失败 |
| `OTA_ERR_ABORTED` | 用户中止 |
| `OTA_ERR_BOOT_ROLLBACK` | Bootloader 回滚 |

## 模块与文件规划

第一阶段建议文件：

```text
components/ota_manager/
  inc/
    ota_ao.h
    ota_event.h
    ota_fb.h
    ota_vector.h
    ota_image.h
    ota_metadata.h
    ota_partition.h
    ota_error.h
  src/
    ota_ao.c
    ota_fb.c
    ota_image.c
    ota_metadata.c
    ota_flash_job.c
    ota_crc32.c

drivers/mcu/flash/
  inc/drv_flash.h
  src/drv_flash.c

tools/ota_bin_info/
  ota_bin_info.py

tools/ota_send/
  ota_send.py
```

标准 `.bin` 信息查询示例：

```powershell
python tools/ota_bin_info/ota_bin_info.py build/RP2350_TRIG.bin
```

`SYST:OTA:BEGIN <size>,<crc32>` 中的 `size` 和 `crc32` 对应标准 raw firmware `.bin` 文件本身。设备端 `END` 阶段会校验整包 CRC 和 App 向量表。

在线发送示例：

```powershell
python tools/ota_send/ota_send.py COM7 build/RP2350_TRIG.bin --block-size 512
```

无硬件时可先验证发送计划：

```powershell
python tools/ota_send/ota_send.py COM7 build/RP2350_TRIG.bin --dry-run
```

Bootloader 阶段新增：

```text
bootloader/
  inc/
    boot_metadata.h
    boot_slot.h
    boot_handoff.h
  src/
    boot_main.c
    boot_metadata.c
    boot_slot.c
    boot_handoff.c
```

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
/update/RP2350_TRIG_UPDATE.pkg
/update/compat/rp2350_trig_x.y.z.bin
/update/last_result.txt
```

SD 卡用途边界：

- 可以保存统一 OTA `.pkg` 包。
- 可以保留 raw `.bin` 兼容升级文件。
- 可以保存较大 UI/日志/数据文件。
- 不能作为唯一 App 存储。
- 不能作为唯一 metadata 存储。
- 不能替代 Product Config 的关键参数区。

## 升级入口

系统支持两个升级入口，但共用同一套 `OtaAO + OtaFB + OtaVector`。

### 在线升级：SCPI over USB CDC

```text
PC ota_send.py
  ↓ USB CDC / SCPI
middleware/scpi_port
  ↓
components/ota_manager/OtaAO
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
SD 卡 /update/*.pkg
  ↓
storage_manager 扫描和读取
  ↓
components/ota_manager/OtaAO
  ↓
drivers/mcu/flash
  ↓
W25Q32 inactive App Slot
```

适合：

- 现场无专用烧录工具。
- 维护人员拷贝升级包到 SD 卡。
- 大文件传输不占用 USB CDC 长时间链路。

## OTA 状态机概览

完整状态转移以本文前面的 `OtaFB ECC 状态转移表` 为准。这里保留生命周期概览，便于快速理解升级闭环。

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

当前不使用自定义 `.ota` 后缀，也不传输 UF2。OTA 传输格式采用标准 raw firmware `.bin`：

```text
rp2350_trig_x.y.z.bin
└─ app raw binary
```

第一阶段必须校验：

- `.bin` 文件大小。
- `.bin` 文件 CRC32。
- Slot 边界。
- App 向量表。

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

1. 用户把统一 `.pkg` 文件放入 SD 卡 `/update/`。
2. App 启动后或收到 SCPI 命令后扫描 `/update/`。
3. 优先选择最新且 board id 匹配的 `.pkg` 包；兼容模式可从 `/update/compat/` 选择 raw `.bin`。
4. `storage_manager` 读取文件流，并向 `OtaAO` 投递数据块事件。
5. 写入 inactive Slot。
6. 校验通过后设置 pending。
7. 用户确认或命令触发重启。
8. Bootloader 启动新 Slot。
9. App 自检通过后 commit。

建议新增 SCPI 命令：

| 命令 | 说明 |
|---|---|
| `MMEM:CAT? "/update"` | 列出升级目录。 |
| `SYST:OTA:FILE "<path>"` | 从 SD 卡指定 `.pkg` 或兼容 `.bin` 文件执行 OTA。 |
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
tools/ota_bin_info/
tools/ota_send/
```

职责：

- `bootloader/`：slot 选择、metadata 读取、镜像校验、跳转、回滚。
- `components/ota_manager/`：实现 `OtaAO/OtaFB/OtaVector`、接收事件、写 inactive Slot、校验、设置 pending。
- `components/storage_manager/`：管理 SD 卡挂载、文件枚举、日志和离线升级包。
- `drivers/mcu/flash/`：封装 W25Q32/XIP Flash erase/program/read/verify。
- `drivers/external/sd_card/`：SD 卡 SPI 模式底层驱动。
- `middleware/fatfs_port/`：FatFs 或等价文件系统适配。
- `middleware/scpi_port/`：增加 OTA 命令，只负责传输和命令入口。
- `tools/ota_bin_info/`：输出标准 `.bin` 的 size 和 CRC32。
- `tools/ota_send/`：通过 USB CDC SCPI 发送标准 `.bin`。

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
- `components/ota_manager/` 的 `OtaAO/OtaFB` 状态机。
- raw `.bin` CRC32 和 App 向量表校验。
- SCPI `SYST:OTA:*` 基础命令。
- App 写 inactive Slot。
- Metadata 双副本。
- 最小 Bootloader：读 metadata、选 Slot、校验、跳转、回滚。
- App commit。
- PC 端 `ota_bin_info.py` 和 `ota_send.py`。

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
- 支持从 `/update/*.pkg` 离线升级，并保留 `/update/compat/*.bin` 兼容路径。
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
2. 增加 raw `.bin` 校验流程和 `ota_bin_info.py`。
3. 增加 `drivers/mcu/flash/`。
4. 增加 `components/ota_manager/`，先落地 `OtaVector/OtaAO/OtaFB`。
5. 增加 SCPI `SYST:OTA:*` 接收命令，命令只投递事件。
6. 增加 Metadata 双副本。
7. 拆出 Bootloader 工程。
8. 修改 App 链接地址，支持 Slot A/B。
9. 完成 USB CDC SCPI OTA 闭环。
10. 增加 SD 卡 SPI + FatFs。
11. 增加 SD 卡离线 OTA。
12. 增加签名、防降级和升级审计。
