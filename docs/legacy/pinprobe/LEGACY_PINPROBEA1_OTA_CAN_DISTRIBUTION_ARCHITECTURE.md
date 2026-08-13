# PinProbe A1 OTA 固件升级与 CAN 多机分发方案

Status: Active
Domain: OTA
Canonical: `docs/legacy/pinprobe/LEGACY_PINPROBEA1_OTA_CAN_DISTRIBUTION_ARCHITECTURE.md`
Related: `docs/ota/OTA_SYSTEM_DESIGN.md`, `docs/ota/OTA_PORTABLE_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
Last updated: 2026-08-10

## 1. 目标与边界

本方案定义 PinProbe A1 的 OTA 固件升级架构，兼容当前单机控制主线，并为后续 CAN 多机组网预留完整路径。

核心目标：

- 单机阶段：上位机通过 SCPI 将固件传给当前节点，节点写入 W25Q128 暂存区，校验后由 Bootloader 刷写内部 Flash。
- 多机阶段：主机先接收并校验 OTA 镜像，再将同一固件镜像通过 CAN 分发给其他节点。
- 每个节点只刷写自己本地 W25Q128 中已完整校验的镜像。
- 运行中的 App 不直接擦写自身 App 区，内部 Flash App 区只由 Bootloader 更新。

明确边界：

- SCPI 是维护入口，不负责多机实时分发。
- CAN 是节点间 OTA 分发链路，不承载最终刷写动作。
- W25Q128 是 OTA 暂存、断点续传、掉电保护和后续日志/快照的统一存储介质。
- RamVector 不存放固件数据，只存放 OTA 状态摘要、节点状态、版本和 ACK 位图。

## 2. 总体架构

```text
PC / 上位机
    |
    | SCPI binary block
    v
主机 App OTA Manager
    |
    | 写入 W25Q128 Slot A/B
    v
主机本地 OTA 镜像
    |
    | CAN 分片分发
    v
子节点 App OTA Receiver
    |
    | 写入各自 W25Q128 Slot A/B
    v
所有目标节点本地校验
    |
    | OTA_COMMIT
    v
各节点复位进入 Bootloader
    |
    | W25Q128 -> 内部 Flash App 区
    v
新 App 启动并确认
```

模块划分：

| 模块 | 所在位置 | 职责 |
|---|---|---|
| Bootloader | 内部 Flash 前段 | 校验 OTA 镜像、擦写 App 区、跳转 App、失败回滚 |
| App OTA Manager | 当前 App | SCPI 接收、写 W25Q128、校验、CAN 分发、提交控制 |
| CAN OTA Transport | 当前 App | 多机分片发送、ACK/NACK、断点续传 |
| W25Q128 Driver | App + Bootloader 共用 | 读、写、擦除、页编程、分区访问 |
| OTA Manifest | W25Q128 metadata 区 | 镜像元数据、状态、CRC、目标节点、提交标志 |
| RamVector OTA Summary | RAM 向量表扩展区 | OTA 状态摘要和多机 ACK 位图 |

## 3. 内部 Flash 布局

当前项目使用 STM32F103RCT6，内部 Flash 256KB，最后 2KB 已保留给配置页。

建议 OTA 阶段调整为：

| 区域 | 地址范围 | 大小 | 说明 |
|---|---:|---:|---|
| Bootloader | `0x08000000 ~ 0x08005FFF` | 24KB | 常驻，禁止 OTA 覆盖 |
| App | `0x08006000 ~ 0x0803F7FF` | 230KB | 主程序区 |
| Config | `0x0803F800 ~ 0x0803FFFF` | 2KB | 现有 Flash 配置页 |

App 工程需要同步修改：

- scatter 文件 App 起始地址改为 `0x08006000`。
- App 中断向量表偏移改为 `0x6000`。
- Bootloader 跳转 App 前设置 MSP 和 VTOR。

Bootloader 跳转逻辑：

```c
#define APP_BASE_ADDR 0x08006000UL

typedef void (*app_entry_t)(void);

uint32_t app_msp   = *(uint32_t *)APP_BASE_ADDR;
uint32_t app_reset = *(uint32_t *)(APP_BASE_ADDR + 4U);

__disable_irq();
SCB->VTOR = APP_BASE_ADDR;
__set_MSP(app_msp);
((app_entry_t)app_reset)();
```

## 4. W25Q128 分区

W25Q128 为 16MB，应与《PinProbeA1 多机架构设计方案 RAM 反射内存》共用同一份全局 Flash map，避免 OTA、快照和日志互相覆盖。

全局分区建议如下：

| 分区 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| Boot Info | `0x000000` | 4KB | Flash 布局版本、节点配置、有效区指针 |
| Snapshot Active | `0x001000` | 8KB | RamVector 快照 Active 区 |
| Snapshot Backup | `0x003000` | 8KB | RamVector 快照 Backup 区 |
| Event Log | `0x005000` | 64KB | 事件/故障日志 |
| OTA Area | `0x015000` | 4MB | OTA 元数据、Boot flags、Slot A/B |
| User Reserved | `0x415000` | 剩余 | 后续扩展 |

OTA Area 内部分区：

| 子分区 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| OTA Metadata A | `0x015000` | 4KB | Manifest 主副本 |
| OTA Metadata B | `0x016000` | 4KB | Manifest 备份副本 |
| OTA Boot Flags | `0x017000` | 4KB | pending、confirmed、rollback、result 标志 |
| OTA Control Log | `0x018000` | 4KB | OTA 状态切换和失败原因小日志 |
| OTA Slot A | `0x019000` | 512KB | 待刷写镜像 |
| OTA Slot B | `0x099000` | 512KB | 上一版本镜像或回滚镜像 |
| OTA Reserved | `0x119000` | 剩余 | 后续双包、差分包或签名扩展 |

说明：

- OTA 相关数据只允许落在 OTA Area 内，禁止占用 `0x000000 ~ 0x014FFF` 的引导信息、快照和日志区。
- Slot A/B 当前按 512KB 规划，已经大于 230KB App 区；若未来 App 区扩大，只需在 OTA Area 内调整 Slot 大小。
- Metadata 双副本使用 sequence 号和 CRC 选择最新有效副本。
- Boot flags 独立分区，避免 manifest 更新中断时影响启动决策。

## 5. OTA Manifest

建议元数据结构：

```c
#define OTA_MANIFEST_MAGIC   0x504F5441UL  /* "POTA" */
#define OTA_MANIFEST_VERSION 0x00010000UL
#define OTA_MAX_NODES        8U

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_RECEIVING,
    OTA_STATE_VERIFYING,
    OTA_STATE_READY,
    OTA_STATE_DISTRIBUTING,
    OTA_STATE_WAIT_ALL_READY,
    OTA_STATE_COMMIT_PENDING,
    OTA_STATE_BOOT_UPDATING,
    OTA_STATE_CONFIRMED,
    OTA_STATE_ABORTED,
    OTA_STATE_ERROR,
} OTA_State_t;

typedef struct {
    uint32_t magic;
    uint32_t manifest_version;
    uint32_t sequence;

    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t image_version;
    uint32_t image_build_id;

    uint32_t product_id;
    uint32_t hw_rev_mask;
    uint32_t image_type;
    uint32_t bootloader_min_version;

    uint32_t app_base_addr;
    uint32_t app_max_size;
    uint32_t slot_addr;
    uint32_t slot_size;

    uint8_t  source_node;
    uint8_t  target_mask;
    uint8_t  ready_mask;
    uint8_t  error_node;

    uint8_t  state;
    uint8_t  flags;
    uint16_t block_size;

    uint32_t received_size;
    uint32_t verified_size;
    uint32_t last_error;

    uint8_t  reserved[64];
    uint32_t manifest_crc32;
} OTA_Manifest_t;
```

Manifest 校验规则：

- `magic` 必须匹配。
- `manifest_version` 主版本必须兼容。
- `image_size` 不得超过 App 区和 Slot 区。
- `app_base_addr` 必须等于 Bootloader 编译时允许的 App 起始地址。
- `product_id` 必须匹配 PinProbe A1 产品 ID。
- `hw_rev_mask` 必须覆盖当前硬件版本。
- `image_type` 必须是 App 镜像，Bootloader 默认拒绝写入 Bootloader 镜像。
- `bootloader_min_version` 不得高于当前 Bootloader 版本。
- 镜像首字必须落在 SRAM 地址范围，复位向量必须落在 App 地址范围。
- `image_crc32` 必须匹配完整镜像。
- `manifest_crc32` 覆盖除自身外的所有字段。

## 6. 单机 OTA 流程

SCPI 命令建议：

```text
SYSTem:OTA:BEGIN <size>,<crc32>,<version>,<image_id>
SYSTem:OTA:DATA <offset>,#<binblock>
SYSTem:OTA:END
SYSTem:OTA:VERify?
SYSTem:OTA:COMMit
SYSTem:OTA:STATus?
SYSTem:OTA:ABORt
```

单机流程：

```text
IDLE
  -> BEGIN: 检查系统安全状态，擦除 OTA Slot
  -> RECEIVING: 按 offset 写 W25Q128
  -> END: 检查 received_size
  -> VERIFYING: 计算完整镜像 CRC32
  -> READY: 镜像可提交
  -> COMMIT: 写 pending 标志并软复位
  -> BOOTLOADER_UPDATING: Bootloader 刷写内部 Flash
  -> APP_RUNNING: 新 App 启动
  -> CONFIRMED: App 写确认标志
```

允许 OTA 的运行条件：

- 系统处于 `LOCK` 或 `IDLE`。
- 门未运动，气缸无动作。
- RS485 通讯状态可靠。
- 不在急停、激光防夹、掉电风险状态。
- 当前没有正在进行的 OTA 会话。

SCPI 数据传输使用 definite-length binary block，但当前固件普通 SCPI 输入缓冲为 256B，因此阶段 B 先采用小块传输：

- 默认 `OTA_SCPI_CHUNK_SIZE = 128B`。
- 单条 `SYSTem:OTA:DATA` 的命令头、offset、binary block 头和 payload 总长度必须小于当前 SCPI 输入缓冲。
- 若后续需要 256B 或更大 payload，应新增 OTA 流式接收路径，不能继续依赖普通 `SCPI_Input()` 整包缓冲。

示例：

```text
SYSTem:OTA:DATA 4096,#3128<128 bytes>
```

其中 `#3` 表示后面 3 位长度字段，`128` 表示数据长度。

### 6.1 串口 SCPI 固件传输协议

OTA 单机接收阶段直接复用 USART1 SCPI 维护口。协议分为控制面和数据面：

- 控制面仍为普通 ASCII SCPI 命令，用于 BEGIN、END、查询状态、校验和提交。
- 数据面使用 SCPI definite-length binary block，payload 为固件原始二进制。
- 设备端按 `offset` 写入 W25Q128 OTA Slot，不把固件数据放入 RamVector，也不写内部 Flash App 区。
- 第一版采用 stop-and-wait：上位机每发送一个 DATA block，必须等待设备返回 OK 或 ERR 后再发送下一块。

推荐串口参数：

| 项目 | 建议值 |
|---|---|
| 串口 | USART1 / SCPI 维护口 |
| 波特率 | 默认沿用 GUI 当前选择，建议 `115200 8N1` 起步 |
| 传输块大小 | `128B` |
| 命令结束符 | `\r\n` |
| 单块超时 | `1s` |
| 单块最大重试 | `3` 次 |

上位机发送顺序：

```text
1. SYSTem:OTA:BEGIN <size>,<crc32>,<version>,<image_id>
2. SYSTem:OTA:DATA <offset>,#3128<128 bytes>
3. 等待 OK,NEXT:<offset> 或 ERR,<code>,<message>
4. 重复 DATA，直到全部 block 收齐
5. SYSTem:OTA:END
6. SYSTem:OTA:VERify?
7. 用户确认后 SYSTem:OTA:COMMit
```

设备端响应建议：

```text
OK
OK,NEXT:4224
STATE:READY SIZE:50760 CRC:0x12345678 VER:0x00010002 ERR:0
ERR,5,SIZE_OVERFLOW
ERR,6,CRC_FAIL
ERR,12,BLOCK_MISSING
```

传输可靠性要求：

- 设备端必须维护 block bitmap，支持重复 offset、乱序 offset 和断点续传。
- DATA 写入成功后再返回 OK；写 W25Q128 或读回校验失败必须返回 ERR。
- `END` 只能检查 block 是否收齐，不代表镜像有效；镜像有效性由 `VERify?` 的 CRC32 决定。
- `COMMit` 前必须再次检查安全状态，禁止在急停、门运动、RS485 故障或掉电风险时提交。
- 普通 SCPI 状态轮询和压力测试在 OTA 上传期间必须暂停，避免串口响应交叉。

### 6.2 OTA 上位机集成

OTA 上位机功能集成到现有 `Tools/pinprobe_gui.py`，不新增独立工具作为首选入口。

GUI 建议新增“固件升级/OTA”页，职责如下：

- 选择固件文件，计算 `size`、`crc32`、`version` 和 `image_id`。
- 显示当前设备 OTA 状态、目标版本、传输进度、速度、剩余时间和错误信息。
- 执行 BEGIN、分块 DATA、END、VERify?、COMMit、ABORt。
- 支持暂停、继续、失败重试和从指定 offset 续传。
- 上传期间自动暂停状态监控轮询和压力测试。

`Tools/pinprobe_gui.py` 当前已有 `SerialWorker` 后台线程处理普通 SCPI 命令。OTA 上传不建议塞入普通 `cmd_queue`，而应新增专用 OTA 上传线程：

- OTA 线程直接持有 `SerialWorker._lock` 或通过专用 API 独占串口。
- 上传期间禁止普通 `_send_scpi()`、自动轮询和压力测试写串口。
- OTA 线程使用 `serial_port.write()` 发送包含 binary block 的完整 bytes，不使用字符串拼接后的普通命令队列。
- OTA 线程用 `read_until(b'\n')` 等待设备 ACK，并过滤固件调试输出。
- 上传结束或失败后释放串口独占权，并恢复普通 SCPI 功能。

## 7. 主机通过 CAN 分发自身 OTA 镜像

多机阶段采用“主机分发同一镜像”的模式：

```text
PC -> 主机 SCPI 接收固件
主机 -> 写入本机 W25Q128
主机 -> 校验完整镜像
主机 -> CAN 分发给 target_mask 中的子节点
子节点 -> 写入本机 W25Q128
子节点 -> 校验完整镜像
主机 -> 收齐 READY 后广播 COMMIT
全部节点 -> 复位进入 Bootloader 刷写
```

关键原则：

- 主机不读取自己正在运行的内部 Flash 来分发。
- 主机分发的是上位机传来的、已在 W25Q128 中完整校验过的 OTA 镜像。
- 每个子节点只信任自己本地完整校验过的 W25Q128 镜像。
- 默认不允许部分节点提交。只要目标节点中有一个未 READY，主机不广播 COMMIT。

## 8. CAN OTA 协议

经典 CAN payload 只有 8 字节，建议使用“逻辑 block + CAN frame 分片”的两层协议。

建议参数：

- 逻辑 block：256B。
- 每个 CAN frame 携带 6B 数据 + 2B header，或使用 8B 数据配合扩展 ID 编码元数据。
- 每个 block 完成后子节点 ACK/NACK。
- NACK 携带 block_index，主机重发该 block。

### 8.1 CAN ID 规划

OTA 使用标准帧 11-bit ID，并复用多机架构的统一编码：

```text
bit10..6: func      5 bit
bit5..3 : src_node  3 bit
bit2..0 : dst_node  3 bit

CAN_ID = (func << 6) | (src_node << 3) | dst_node
```

约定：

- `src_node` 为发送节点 ID，范围 `0~7`，对应最多 8 个物理节点。
- `dst_node = 0` 在 OTA 主机下行帧中表示广播；具体目标节点由 payload 内的 `target_mask` 过滤。
- OTA 大数据分发默认使用广播帧加 `target_mask`，不依赖 8 个节点全部可单播寻址。
- 子节点响应必须带自己的 `src_node`，禁止多个节点用相同 CAN ID 同时 ACK。
- 子节点上行响应的 `dst_node` 填主机节点 ID；若主机节点 ID 为 0，该帧仍可通过 OTA 上行功能码和 `src_node != host_node` 区分，不按广播处理。
- `0x00~0x07` 已分配给 RamVector 同步、心跳、ACK、节点加入和紧急帧；OTA 使用保留功能码 `0x08~0x0F`。

OTA 功能码：

| func | 方向 | 消息 |
|---:|---|---|
| `0x08` | 主机 -> 目标/广播 | `OTA_ANNOUNCE` / `OTA_ANNOUNCE_CRC` |
| `0x09` | 目标 -> 主机 | `OTA_ACCEPT` / `OTA_REJECT` |
| `0x0A` | 主机 -> 目标/广播 | `OTA_BLOCK_BEGIN` |
| `0x0B` | 主机 -> 目标/广播 | `OTA_BLOCK_DATA` |
| `0x0C` | 目标 -> 主机 | `OTA_BLOCK_ACK` / `OTA_BLOCK_NACK` |
| `0x0D` | 主机 -> 目标/广播 | `OTA_VERIFY` / `OTA_COMMIT` / `OTA_ABORT` |
| `0x0E` | 目标 -> 主机 | `OTA_READY` / `OTA_VERIFY_FAIL` |
| `0x0F` | 双向 | `OTA_QUERY` / `OTA_STATUS` |

### 8.2 消息定义

`OTA_ANNOUNCE`

```text
byte0: image_id
byte1: target_mask
byte2: version_low
byte3: version_high
byte4: size[7:0]
byte5: size[15:8]
byte6: size[23:16]
byte7: size[31:24]
```

随后主机发送 CRC：

```text
OTA_ANNOUNCE_CRC
byte0..3: image_crc32
byte4..5: block_size
byte6: flags
byte7: reserved
```

`OTA_BLOCK_BEGIN`

```text
byte0: image_id
byte1: target_mask
byte2: block_index[7:0]
byte3: block_index[15:8]
byte4: block_len[7:0]
byte5: block_len[15:8]
byte6: block_crc16[7:0]
byte7: block_crc16[15:8]
```

`OTA_BLOCK_DATA`

```text
byte0: image_id
byte1: seq_in_block
byte2..7: data[6]
```

`OTA_BLOCK_ACK`

```text
CAN ID src_node: responding node_id
CAN ID dst_node: host node_id
byte0: image_id
byte1: flags        /* bit0: 0=ACK, 1=NACK */
byte2: block_index[7:0]
byte3: block_index[15:8]
byte4: error_code
byte5: received_crc16[7:0]
byte6: received_crc16[15:8]
byte7: reserved
```

`OTA_READY`

```text
CAN ID src_node: responding node_id
CAN ID dst_node: host node_id
byte0: image_id
byte1: status       /* 0=READY, 1=VERIFY_FAIL */
byte2: error_code
byte3: reserved
byte4..7: image_crc32
```

## 9. 多机状态机

主机状态：

```text
IDLE
  -> RECEIVE_FROM_PC
  -> LOCAL_VERIFY
  -> CAN_ANNOUNCE
  -> CAN_DISTRIBUTE
  -> CAN_VERIFY
  -> WAIT_ALL_READY
  -> COMMIT_ALL
  -> DONE
  -> ERROR / ABORTED
```

子节点状态：

```text
IDLE
  -> WAIT_ANNOUNCE
  -> RECEIVING
  -> VERIFYING
  -> READY
  -> COMMIT_PENDING
  -> BOOT_UPDATING
  -> CONFIRMED
  -> ERROR / ROLLBACK
```

超时策略：

- `ANNOUNCE` 后等待 `ACCEPT`，超时则标记节点失败。
- block ACK 超时则重发当前 block。
- 单 block 最大重发次数建议 3 次。
- 任一节点达到最大重试仍失败，默认整体 OTA 失败，不进入 COMMIT。

## 10. Bootloader 规则

Bootloader 启动时按如下顺序判断：

1. 读取 Boot flags。
2. 若无 pending update，跳转当前 App。
3. 若有 pending update，读取 Manifest。
4. 校验 Manifest。
5. 校验 W25Q128 Slot 中完整镜像 CRC32。
6. 检查 image size 和 app_base_addr 合法性。
7. 擦除 App 区。
8. 从 W25Q128 分页写入内部 Flash。
9. 写入 update result。
10. 跳转新 App。

回滚/确认策略：

- 新 App 启动后在自检通过、任务启动正常后写 `APP_CONFIRM`。
- 如果 Bootloader 发现 pending 已刷写但 App 未确认，优先回滚到 Slot B；若 Slot B 无有效旧镜像，则保持 Bootloader 错误状态等待维护。
- 当前硬件只保留一个 App 区，因此 Slot B 必须保存上一版本完整镜像，支持重新刷回。

Boot flags 状态表：

| 字段 | 含义 |
|---|---|
| `magic` | Boot flags 有效标识 |
| `sequence` | 单调递增序号 |
| `pending_slot` | `0=none, 1=slotA, 2=slotB` |
| `previous_slot` | 上一版本镜像所在 Slot |
| `update_state` | `IDLE/PENDING/WRITING/WRITTEN/CONFIRMED/ROLLBACK_REQUIRED/FAILED` |
| `attempt_count` | 当前镜像启动确认尝试次数 |
| `max_attempts` | 默认 1 次，超过后回滚 |
| `last_error` | 最近一次 Bootloader 错误 |
| `flags_crc32` | 覆盖除自身外字段 |

Boot flags 原子更新顺序：

1. App 接收并校验新镜像到 Slot A。
2. 若当前 App 镜像可导出或已有上一版本镜像，确认 Slot B 为有效 previous slot。
3. App 写 Manifest A/B，校验双副本至少一个有效。
4. App 写 Boot flags：`pending_slot=SlotA`，`previous_slot=SlotB`，`update_state=PENDING`。
5. App 软复位进入 Bootloader。
6. Bootloader 将 `update_state` 改为 `WRITING` 后擦写 App 区。
7. 刷写和回读校验通过后写 `WRITTEN`，然后跳转新 App。
8. 新 App 自检通过后写 `CONFIRMED` 并清除 pending。
9. 下次 Bootloader 若看到 `WRITTEN` 但未 `CONFIRMED`，且 `attempt_count >= max_attempts`，则刷回 `previous_slot`。

Bootloader 最小依赖：

- HAL Flash。
- W25Q128 SPI 轮询驱动。
- CRC32。
- IWDG 刷新或关闭策略。
- 简单状态 LED 可选。

Bootloader 不依赖：

- FreeRTOS。
- SCPI。
- ModBus。
- RamVector。
- AppLog。

## 11. RamVector OTA 状态摘要

RamVector 不承载固件数据，只增加状态摘要，建议放入 `ext_params` 或未来版本化扩展区。

```c
typedef struct {
    uint8_t  ota_state;
    uint8_t  ota_role;        /* 0=none, 1=host, 2=target */
    uint8_t  target_mask;
    uint8_t  ready_mask;
    uint8_t  error_node;
    uint8_t  error_code;
    uint16_t progress_permille;
    uint32_t image_version;
    uint32_t image_size;
    uint32_t image_crc32;
} Vector_OTASummary_t;
```

用途：

- SCPI 查询 OTA 状态。
- defaultTask/集群健康监控扫描节点升级进度。
- CAN 多机阶段同步节点状态摘要。

## 12. SCPI 命令扩展

单机：

```text
SYSTem:OTA:STATus?
SYSTem:OTA:BEGIN <size>,<crc32>,<version>,<image_id>
SYSTem:OTA:DATA <offset>,#<binblock>
SYSTem:OTA:END
SYSTem:OTA:VERify?
SYSTem:OTA:COMMit
SYSTem:OTA:ABORt
```

多机主机：

```text
SYSTem:OTA:TARGET <mask>
SYSTem:OTA:DISTRibute
SYSTem:OTA:NODEs?
SYSTem:OTA:COMMit:ALL
SYSTem:OTA:ABORt:ALL
```

查询返回建议：

```text
STATE:READY ROLE:HOST TARGET:0x0F READY:0x0F SIZE:50760 CRC:0x12345678 VER:0x00010002 ERR:0
```

## 13. 错误码

| 错误码 | 名称 | 说明 |
|---:|---|---|
| 0 | OK | 无错误 |
| 1 | BUSY | OTA 会话已存在 |
| 2 | BAD_STATE | 当前系统状态不允许 OTA |
| 3 | BAD_PARAM | 参数错误 |
| 4 | FLASH_FAIL | W25Q128 写/擦失败 |
| 5 | SIZE_OVERFLOW | 镜像超过允许范围 |
| 6 | CRC_FAIL | CRC 校验失败 |
| 7 | CAN_TIMEOUT | CAN ACK 超时 |
| 8 | CAN_NACK | 子节点拒绝或请求失败 |
| 9 | NODE_NOT_READY | 目标节点未全部 READY |
| 10 | BOOT_FAIL | Bootloader 更新失败 |
| 11 | APP_NOT_CONFIRMED | 新 App 未确认 |

## 14. 安全策略

- OTA BEGIN 前必须检查系统处于安全静止状态。
- OTA COMMIT 前必须再次检查系统处于安全静止状态。
- 急停、激光防夹、门运动中、RS485 故障、掉电风险时禁止 COMMIT。
- 多机默认禁止部分提交，避免集群版本不一致。
- Manifest 和固件镜像都必须 CRC 校验。
- CRC32 仅用于传输完整性和掉电恢复判定；Manifest 还必须校验产品 ID、硬件版本、镜像类型、Bootloader 最小版本和向量表合法性。
- Manifest 预留签名字段，现场维护链路开放前应启用镜像签名或至少启用受控工具生成的包格式。
- Bootloader 必须限制写入地址，禁止写 Bootloader 自身和 Config 页。
- App 更新成功后必须确认，否则下次启动进入回滚/维护策略。

## 15. 实施步骤

建议分阶段落地：

### 阶段 A：存储和数据结构

- 新增 `Hardware/OTA/Inc/ota_manifest.h`。
- 新增 `Hardware/OTA/Src/ota_manifest.c`。
- 新增 W25Q128 驱动：`Hardware/Flash/Inc/w25q128.h`、`Hardware/Flash/Src/w25q128.c`。
- 实现分区擦除、写入、读取、CRC32。
- 固化全局 W25Q128 分区表，保证 OTA Area 从 `0x015000` 开始，不覆盖 Boot Info、Snapshot 和 Event Log。
- 实现 Boot flags 双校验或 CRC 校验。

### 阶段 B：App 单机接收

- 增加 SCPI OTA 命令。
- 支持 `128B` binary block 写入 W25Q128；如需更大块，先实现 OTA 流式接收，不直接扩大普通 SCPI 命令语义。
- 支持 `OTA:STATus?`、`OTA:VERify?`。
- 暂不刷内部 Flash。
- 验收：连续乱序/重复 offset 写入不会破坏已收数据，断点续传后 CRC32 能通过。

### 阶段 C：Bootloader

- 新建 Bootloader 工程。
- App 起始地址迁移到 `0x08006000`。
- Bootloader 支持 W25Q128 -> 内部 Flash。
- App 启动后写确认标志。
- 验收：App scatter、VTOR、启动文件、调试下载地址和产物命名全部迁移完成；Bootloader 拒绝写 Bootloader 区和 Config 页。

### 阶段 D：单机 OTA 闭环

- `OTA:COMMit` 写 pending 标志并软复位。
- Bootloader 刷写 App。
- 新 App 确认。
- 失败回滚/错误状态验证。
- 验收：刷写中断电、CRC 错包、App 不确认、Slot B 无效四类故障均有确定状态和可维护路径。

### 阶段 E：CAN 多机分发

- 增加 CAN OTA 消息，复用 `func/src_node/dst_node` 标准 ID 编码。
- 主机分发本地已校验镜像。
- 子节点 W25Q128 暂存和校验。
- 主机收齐 READY 后统一 COMMIT。
- 验收：多个子节点同时 ACK 时不会发生同 ID 数据冲突；任一目标节点失败时默认不 COMMIT。

### 阶段 F：产线和维护工具

- 在 `Tools/pinprobe_gui.py` 中新增“固件升级/OTA”页，作为默认 OTA 上位机入口。
- GUI 支持通过 USART1 SCPI 维护口上传固件，采用 `128B` definite-length binary block。
- GUI 上传线程独占串口，上传期间暂停状态监控轮询和压力测试。
- 输出 OTA 进度、速度、剩余时间、节点状态和错误信息。
- 增加断点续传和失败重试策略。
- 保留命令行脚本作为后续产线自动化可选项，但不作为第一阶段主入口。

## 16. 首批建议文件

```text
Hardware/OTA/Inc/ota_manifest.h
Hardware/OTA/Src/ota_manifest.c
Hardware/OTA/Inc/ota_manager.h
Hardware/OTA/Src/ota_manager.c
Hardware/OTA/Inc/ota_can.h
Hardware/OTA/Src/ota_can.c
Hardware/Flash/Inc/w25q128.h
Hardware/Flash/Src/w25q128.c
Bootloader/
```

## 17. 结论

PinProbe A1 的 OTA 不应设计成 App 自擦写，而应采用“App 接收/分发，W25Q128 暂存，Bootloader 刷写”的分层结构。

单机和多机可以共用同一套镜像格式和 Manifest。后续多机只是在主机校验完成后增加 CAN 分发层，子节点仍执行相同的本地校验和 Bootloader 更新流程。这样能最大限度降低现场升级风险，并兼容后续 1~8 节点 CAN 组网。
