# 融合型系统向量黑板与功能块架构方案

Status: Active
Domain: HAOFV
Canonical: `docs/HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md`
Related: `docs/HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE_SUPPLEMENT.md`, `docs/RTOS_PORTING_PLAN.md`, `docs/PIO_RESOURCE_PLAN.md`
Last updated: 2026-07-07

本文档定义 RP2350_TRIG 后续产品化演进采用的软件架构。目标是在保持裸机/Pico SDK 工程轻量性的同时，融合 Active Object、轻量 IEC 61499 功能块、时间同步型系统向量黑板、资源仲裁和表驱动状态机，为 OTA、同步触发、SD 卡、LCD、SCPI、诊断、后续 RTOS 和更多硬件模块提供清晰边界。

> **实施指南**：[HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE_SUPPLEMENT.md](HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE_SUPPLEMENT.md) 提供 ECC 表实现示例、GPIO 迁移步骤、Flash 异步 Job 代码和完整代码附录。

## 架构名称

推荐正式名称：

```text
Hybrid Active Object Function Block Vector Architecture
```

中文名称：

```text
融合型主动对象功能块向量架构
```

工程内部可简称：

```text
HAOFV Architecture
```

该架构不是单一表驱动，也不是单一事件队列，也不是完整 IEC 61499 运行时，而是组合以下设计：

- `Active Object Layer`：每个功能域独立运行，拥有事件队列、生命周期、执行预算和对外 API。
- `IEC 61499-inspired Function Block Layer`：采用固定功能块、静态事件连接、数据输入输出和 ECC 状态机。
- `System Vector Blackboard`：系统总表，保存全局状态摘要、资源占用、错误摘要。
- `Domain Vector Tables`：各功能域独立向量表，例如 OTA、Trigger、Storage、UI。
- `Table-Driven State Machines`：状态转移、命令解析、资源冲突、错误码使用表驱动。
- `Resource Arbiter`：统一管理 Flash、SPI、PIO、DMA、USB、LCD、SD 等资源互锁。
- `RTE-like Service Layer`：上层不直接碰硬件，通过驱动和服务层访问外设。
- `Bootloader/OTA Safety Chain`：App 接收和校验，Bootloader 启动选择、搬运、回滚。

完整分层如下：

```text
SCPI / UI / SD / Bootloader Result
        ↓
Active Object Layer
        ↓
IEC 61499-style Function Block Layer
        ↓
Time-Synchronized Vector Blackboard Layer
        ↓
Hardware Service Layer

Hard Real-Time Side Path:
PIO / DMA / IRQ
```

一句话总结：

```text
Active Object 管运行，
IEC 61499 风格功能块管逻辑，
Vector Blackboard 管数据，
PIO/DMA 管硬实时。
```

## 硬件资源约束

### RP2350 平台

| 资源 | 总量 | 约束 |
|---|---|---|
| SRAM | 520 KB | 10 个 bank，架构占用远低于上限 |
| XIP Flash (W25Q32) | 4 MB | 见 Flash 分区表 |
| PIO Block | 3 (pio0/1/2) | 每 block 4 SM + 32 指令，全部预留给同步触发 |
| DMA Channel | 12 | 触发子系统使用 1-2 通道 |
| USB CDC | 1 | 与日志共享，OTA 期间暂停周期日志 |

### RAM 预算

| 模式 | 估算 RAM 占用 | 说明 |
|---|---|---|
| Baremetal | 8-15 KB | 不含 LCD 帧缓冲 |
| FreeRTOS | 35-55 KB | 含内核、任务栈、堆 |

520 KB SRAM 有充足余量。关键约束：
- LCD 显示不得分配全帧 RGB565 缓冲（~65 KB），应使用 U8G2 page buffer 模式或分片刷新
- PIO FIFO 和 DMA 缓冲应使用小尺寸环形缓冲或双缓冲
- SD 卡文件系统缓冲（FatFs）应在使用前才分配，默认不常驻

### Flash 分区

W25Q32 4 MB，XIP 基地址 `0x10000000`：

| 区域 | Flash 偏移 | XIP 地址 | 大小 | 状态 |
|---|---|---|---|---|
| Bootloader | `0x000000` | `0x10000000` | 256 KB | ✅ 已落地，当前 < 64 KB |
| App Slot A | `0x040000` | `0x10040000` | 1536 KB | ✅ 固定链接地址 |
| App Slot B | `0x1C0000` | `0x101C0000` | 1536 KB | ✅ OTA staging / direct A/B |
| OTA Metadata | `0x340000` | `0x10340000` | 64 KB | ✅ 双副本 + copy txn + A/B 扩展 |
| Product Config | `0x350000` | `0x10350000` | 64 KB | 预留 |
| Scratch/Reserved | `0x360000` | `0x10360000` | 640 KB | 预留 |

构建大小硬约束：
- 单个 App 镜像硬上限：1536 KB
- 构建告警阈值：1200 KB
- 构建失败阈值：1400 KB

## 分层职责

### Active Object Layer

Active Object 是功能域的运行容器。它负责：

- 事件队列。
- 对外 API。
- 执行预算。
- 生命周期。
- 与 `system_manager` 的资源申请和模式检查。
- 调用内部 Function Block。

建议 Active Object：

```text
OtaAO
TriggerAO
StorageAO
UiAO
DiagnosticsAO
```

Active Object 不直接暴露内部状态，不允许外部模块直接修改 DomainVector 的状态字段。

### IEC 61499-style Function Block Layer

本项目只采用轻量 IEC 61499 子集，不实现完整 IEC 61499 运行时。

采用内容：

- 固定功能块实例。
- 静态事件连接。
- Event Input / Event Output。
- Data Input / Data Output。
- ECC 状态机。
- 表驱动状态转移。

不采用内容：

- 动态部署功能块。
- XML/FBT 解析。
- 完整分布式运行时。
- 工程工具链绑定。
- 在硬实时 PIO 路径中执行功能块调度。

建议功能块：

```text
TriggerFB
OtaFB
StorageFB
UiFB
SafetyFB
DiagnosticsFB
```

### Time-Synchronized Vector Blackboard Layer

Vector 是数据表，但"时间"由调度器保证。也就是说，Vector 按固定调度阶段更新、快照和提交。

建议阶段：

```text
1. Snapshot Input
2. Dispatch Events
3. Execute Function Blocks
4. Arbitrate Resources
5. Commit Outputs
6. Publish Diagnostics
```

### Hardware Service Layer

硬件服务层封装 MCU 和外部器件：

```text
drv_flash
sync_io
drv_spi
drv_uart
lcd_st7789
sd_card
watchdog
```

### Hard Real-Time Side Path

同步触发和高速采样的硬实时路径不进入通用功能块调度：

```text
PIO
DMA
IRQ
```

功能块只负责配置、启停、状态摘要和故障处理，真正的边沿捕获、脉冲输出、同步时钟由 PIO/DMA/IRQ 执行。

## 核心原则

### 表负责规则

适合使用表驱动的内容：

- SCPI 命令表。
- IO 引脚功能表。
- PIO/DMA 资源分配表。
- OTA 状态转移表。
- Trigger 模式和动作表。
- UI 页面和菜单表。
- 资源冲突表。
- 错误码和诊断事件表。

### Vector 负责事实

Vector 只保存当前系统事实和摘要：

- 当前状态。
- 命令槽。
- IO 快照。
- 配置快照。
- 资源占用。
- 进度。
- 错误码。
- 健康状态。

Vector 不保存大块数据。OTA 固件块、SD 文件缓存、采样环形缓冲、日志字符串不进入 Vector，只在 Vector 中记录指针、长度、CRC、进度和状态摘要。

### 事件负责意图

SCPI、UI、SD 卡、Bootloader 结果等外部入口只投递事件，不直接修改功能域状态。

示例：

```text
SYST:OTA:BEGIN
  -> ota_manager_post_event(OTA_EVENT_BEGIN)
  -> ota_manager_service()
  -> OTA 内部状态机处理
```

### 事件载荷所有权与生命周期

`fb_event_t` 携带的载荷数据必须明确所有权，防止悬垂指针。架构采用分层策略：

| 载荷大小 | 策略 | 使用场景 |
|---|---|---|
| ≤ 32 B | 内联拷贝到事件结构体内部 | 小命令、配置变更、状态查询 |
| > 32 B（OTA 数据块） | `event_bus` 层做内联拷贝到环形缓冲区 | `OTA_EVENT_DATA_BLOCK` |
| > 32 B（静态/全局） | 发送者持有静态分配内存，事件消费前不释放 | 配置表、校准数据 |
| > 32 B（ISR 产生） | ISR 写入无锁环形缓冲，AO 周期消费 | 触发统计、DMA 完成通知 |

**关键约束**：
- 发送者不得在事件投递后立即释放 payload 内存（除非使用内联模式）
- 消费者在 `fb_execute_fn` 返回后不得再访问 payload
- ISR 上下文只能使用内联模式或无锁原子写入

### Active Object 负责流程

每个复杂功能域作为一个主动对象运行：

- `sync_trigger`：同步触发域。
- `ota_manager`：OTA 域。
- `storage_manager`：SD/文件域。
- `sync_config_ui`：LCD/按键 UI 域。
- `diagnostics`：诊断域。

每个域只允许自己修改自己的运行状态。

### 功能块负责逻辑

每个 Active Object 内部可以包含一个或多个 Function Block。Function Block 只处理事件、数据和状态迁移，不直接访问底层硬件。

示例：

```text
OtaAO
  -> OtaFB
  -> FlashJobFB
  -> MetadataFB

TriggerAO
  -> TriggerFB
  -> SafetyFB
  -> PioControlFB
```

### Resource Arbiter 负责互锁

所有共享资源必须通过统一仲裁：

- Flash 擦写。
- LCD/SD 共享 SPI。
- USB CDC OTA 传输。
- PIO/DMA 资源占用。
- OTA 与同步触发实时路径冲突。

### Service Layer 负责硬件

上层组件不直接调用 Pico SDK 硬件 API。硬件访问路径为：

```text
component
  -> service/driver wrapper
  -> Pico SDK / hardware
```

## 推荐目录结构

```text
application/
  app.c                         # 主循环和调度入口

components/
  system_manager/               # 系统模式、资源仲裁、总调度
  system_vector/                # SystemVector 总表和各域摘要定义
  event_bus/                    # 轻量事件投递
  function_block/               # 轻量 IEC 61499 子集基础类型和调度接口
  sync_trigger/                 # 同步触发 Active Object
  ota_manager/                  # OTA Active Object
  storage_manager/              # SD 卡 / 文件管理
  diagnostics/                  # 诊断、错误码、运行日志
  sync_config_ui/               # LCD/按键配置界面

middleware/
  scpi_port/                    # SCPI 命令入口
  u8g2_port/                    # U8G2 显示适配
  portable_ota_port/            # portable_ota 产品适配器
  fatfs_port/                   # 后续 SD 文件系统适配

drivers/
  mcu/
    flash/
    spi/
    pio/
    dma/
    uart/
    watchdog/
  external/
    lcd/
    sd_card/

bootloader/
  inc/
  src/

third_party/
  scpi-parser/
  u8g2/
  fatfs/
  portable_ota/                 # 平台无关 OTA 核心库
  freertos/                     # FreeRTOS Kernel（可选）
```

## 功能块模型

### 基础接口

建议定义最小功能块接口：

```c
typedef enum {
    FB_RESULT_IGNORED = 0,
    FB_RESULT_HANDLED,
    FB_RESULT_BUSY,
    FB_RESULT_ERROR,
} fb_result_t;

typedef struct {
    uint32_t id;
    uint32_t type;
    uint32_t timestamp_ms;
    uint32_t payload_size;
    union {
        uint8_t  inline_data[32];   // ≤32B 内联拷贝
        const void *external_ptr;   // >32B 外部指针（需满足所有权约定）
    } payload;
} fb_event_t;

typedef struct {
    uint32_t instance_id;
    uint32_t state;
    uint32_t error_code;
} fb_context_t;

typedef fb_result_t (*fb_execute_fn)(fb_context_t *context, const fb_event_t *event);
```

### ECC 状态转移表

Function Block 的核心是表驱动的 ECC（Execution Control Chart）状态机。每条规则包含当前状态、触发事件、前置条件、执行动作和下一状态：

```c
typedef struct {
    uint32_t     current_state;
    uint32_t     event;
    bool         (*condition)(fb_context_t *ctx, const fb_event_t *evt);
    fb_result_t  (*action)(fb_context_t *ctx, const fb_event_t *evt);
    uint32_t     next_state;
} fb_ecc_entry_t;
```

ECC 执行引擎遍历规则表，找到首条匹配的状态+事件+条件组合，执行动作并转移状态。无匹配规则时事件被静默忽略（返回 `FB_RESULT_IGNORED`）。

当前已实现的规模：
- **OtaFB**：15 条 ECC 规则，覆盖 9 个状态 + 10 个事件
- **TriggerFB**：58 条 ECC 规则，覆盖 6 个状态 + 20 个事件

具体代码示例见实施指南。

### OTA 功能块

```text
OtaFB
  Event Inputs:
    BEGIN
    DATA_BLOCK
    END
    ABORT
    VERIFY
    COMMIT
    TICK

  Event Outputs:
    READY
    PROGRESS
    FAILED
    PENDING_REBOOT

  Data Inputs:
    SystemVector snapshot
    OtaVector command slot
    Flash job status

  Data Outputs:
    OtaVector summary
    Metadata update request
```

### Trigger 功能块

```text
TriggerFB
  Event Inputs:
    ARM
    DISARM
    FIRE
    CONFIG_SET
    SAMPLE_START
    SAMPLE_STOP

  Event Outputs:
    ARMED
    FIRED
    BUSY
    FAULT

  Data Inputs:
    TriggerVector command slot
    PIO/DMA status
    Safety status

  Data Outputs:
    TriggerVector summary
    PIO control request
```

### Safety 功能块

`SafetyFB` 负责把安全事件转换为最高优先级控制意图，例如禁止 OTA、停止采样、关闭输出、进入故障模式。

```text
SafetyFB
  Event Inputs:
    FAULT_DETECTED
    RESOURCE_CONFLICT
    WATCHDOG_WARNING
    TRIGGER_TIMEOUT

  Event Outputs:
    ENTER_FAULT
    STOP_OUTPUTS
    BLOCK_OTA
    DIAG_EVENT
```

## 数据模型

数据模型由 `SystemVector` 和各功能域 `DomainVector` 组成。功能块通过快照读取 Vector，通过对应 Active Object 的提交接口更新本域摘要，避免跨模块直接写状态字段。

### SystemVector

`SystemVector` 是系统总表，只放全局摘要和仲裁结果。

建议字段：

```c
typedef struct {
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t system_mode;
    uint32_t resource_locks;
    uint32_t fault_summary;
    uint32_t trigger_summary;
    uint32_t ota_summary;
    uint32_t storage_summary;
    uint32_t ui_summary;
    uint32_t diagnostics_summary;
} system_vector_t;
```

### TriggerVector

`TriggerVector` 描述同步触发域的事实和配置快照。

建议字段：

```c
typedef struct {
    uint32_t state;
    uint32_t input_snapshot;
    uint32_t output_state;
    uint32_t pio_status;
    uint32_t dma_status;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    uint32_t last_trigger_result;
    uint32_t error_code;
} trigger_vector_t;
```

### OtaVector

`OtaVector` 描述 OTA 域的状态、进度和错误。

建议字段：

```c
typedef struct {
    uint32_t state;
    uint32_t target_slot;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t crc32_expected;
    uint32_t crc32_running;
    uint32_t progress_permille;
    uint32_t boot_flags_summary;
    uint32_t error_code;
} ota_vector_t;
```

### StorageVector

建议字段：

```c
typedef struct {
    uint32_t sd_mounted;
    uint32_t current_file_id;
    uint32_t file_size;
    uint32_t read_offset;
    uint32_t error_code;
} storage_vector_t;
```

### UiVector

建议字段：

```c
typedef struct {
    uint32_t page_id;
    uint32_t selected_item;
    uint32_t key_event;
    uint32_t dirty_flag;
    uint32_t error_code;
} ui_vector_t;
```

### Vector 摘要字段编码规范

多个 Vector 使用单个 `uint32_t` 字段存储位掩码摘要。以 `fault_summary` 和 `trigger_summary` 为例：

```c
// fault_summary bits:
#define SYS_FAULT_TRIGGER_MASK   (1u << 0)   // 触发域故障
#define SYS_FAULT_OTA_MASK       (1u << 1)   // OTA 域故障
#define SYS_FAULT_FLASH_MASK     (1u << 2)   // Flash 操作故障
#define SYS_FAULT_WATCHDOG_MASK  (1u << 3)   // 看门狗警告
#define SYS_FAULT_RESOURCE_MASK  (1u << 4)   // 资源死锁

// trigger_summary bits:
#define TRIG_SUMMARY_ARMED       (1u << 0)
#define TRIG_SUMMARY_RUNNING     (1u << 1)
#define TRIG_SUMMARY_FAULT       (1u << 2)
#define TRIG_SUMMARY_SEQ_MODE    (1u << 3)
```

### 错误码空间划分

每个域使用独立错误码范围，避免跨域错误混淆：

| 域 | 错误码范围 | 示例 |
|---|---|---|
| 通用 | 0 | `NONE` |
| OTA | 1-99 | 见 `docs/OTA方案.md` 15 种 OTA 错误码 |
| Trigger | 100-199 | 1=非法参数, 2=资源忙, 3=PIO/DMA 配置失败, 10=ENC target=0, 11=非法编码器引脚 |
| Flash | 200-299 | 擦除/写入/读回校验失败 |
| Storage | 300-399 | SD 卡挂载/读写/文件系统错误 |
| UI | 400-499 | LCD 初始化/刷新失败 |
| System | 500-599 | 资源死锁、模式切换冲突 |

多故障同时发生时：`error_code` 记录最严重错误，`fault_summary` 位掩码保留所有故障位。

### 字段值域约定

| 字段 | 类型 | 值域 | 说明 |
|---|---|---|---|
| `progress_permille` | `uint32_t` | 0-1000 | 千分比进度 |
| `state` | `uint32_t` | 对应域状态枚举 | 不跨域混用 |
| `error_code` | `uint32_t` | 见错误码空间 | 0 = NONE |
| `sequence` | `uint32_t` | 单调递增 | metadata transaction sequence |
| `timestamp_ms` | `uint32_t` | 系统启动后的毫秒数 | 约 49 天回绕 |

## 写权限规则

| 数据 | 允许写入者 |
|---|---|
| `TriggerVector.state` | 只能 `sync_trigger` 写 |
| `OtaVector.state` | 只能 `ota_manager` 写 |
| `SystemVector.system_mode` | 只能 `system_manager` 写 |
| `resource_locks` | 只能 `resource_arbiter` 写 |
| `command_slot` | SCPI/UI/Storage 可通过 API 写入 |
| `summary` | 对应功能域写，SystemVector 汇总读取 |
| 大块数据 | 不进入 Vector |

禁止跨模块直接写结构体字段。所有写入通过 API 完成，例如：

```c
bool ota_manager_post_event(const ota_event_t *event);
bool sync_trigger_post_event(const trigger_event_t *event);
void system_vector_get_snapshot(system_vector_t *snapshot);
```

## 调度模型

### 主循环驱动

当前裸机版本由主循环驱动：

```text
app_run_once()
  scpi_port_service()
  event_bus_service()

  system_manager_service()
    snapshot_inputs()
    dispatch_events()
    trigger_ao_service(high_priority_budget)
    ota_ao_service(low_priority_budget)
    storage_ao_service(low_priority_budget)
    ui_ao_service(low_priority_budget)
    diagnostics_ao_service()
    publish_system_vector()
```

关键要求：

- 同步触发实时路径继续交给 PIO/DMA/IRQ。
- OTA、UI、SD 卡属于管理域，必须限时运行。
- OTA 写 Flash 前必须进入维护允许状态。
- 查询类命令读快照，不触发现场硬件访问。

### 调度预算定义

`ao_service(budget)` 中的 budget 是在单次调用中允许消耗的最大时间，以**微秒（μs）**为单位。每个 AO 在 service 入口记录时间戳，在耗时操作后检查是否超出预算。

| 等级 | 预算 | 适用 AO | 说明 |
|---|---|---|---|
| `HIGH_PRIORITY` | 500 μs | TriggerAO | 只做状态处理，不阻塞 |
| `NORMAL` | 1000 μs | SystemAO, IoFrontendAO | 一般管理操作 |
| `LOW_PRIORITY` | 2000 μs | OtaAO (维护模式), UiAO | Flash 分片、LCD 刷新 |
| `OTA_DEDICATED` | 5000 μs | OtaAO (OTA 专用模式) | 大块 Flash 操作，必须周期喂狗 |
| `BACKGROUND` | 500 μs | DiagnosticsAO | 低频诊断 |

### Flash 异步操作模型

W25Q32 扇区擦除需 60-300ms，页编程需 0.7-3ms。架构要求所有 Flash 操作拆分为异步 job，每个 job 在单次 `ao_service()` 内只执行一个可控分片，立即返回：

```
AO 创建 job → flash_job_scheduler → 执行一个分片
  → FLASH_JOB_BUSY（还有后续分片）
  → FLASH_JOB_DONE（全部完成）
  → FLASH_JOB_FAILED（失败）
```

| Job 类型 | 单次分片 | 估算耗时 | 说明 |
|---|---|---|---|
| `ERASE_SLOT` | 1 个 4 KB sector | < 300ms | 单次预算内完成一个 sector |
| `PROGRAM_BLOCK` | 256 B (1 page) | < 3ms | W25Q32 页编程 |
| `READBACK_VERIFY` | 256 B - 1 KB | < 1ms | 纯读操作 |
| `VERIFY_IMAGE_CRC` | 1 KB - 4 KB | < 1ms | 软件 CRC32 |
| `WRITE_METADATA` | 1 个 metadata 副本 | < 400ms | 擦+写+读回，原子事务 |

**不可中止阶段**：Metadata 正在擦写时不能立即中止（会导致双副本全部损坏）。处理方式为设置 `abort_pending` 标志，当前写入事务完成后检查并进入 ABORTED 状态。

**XIP 安全约束**：RP2350 从 W25Q32 XIP 执行代码时，擦写 Flash 需在进入 Flash 操作前完成预算检查，USB CDC 服务不应在 Flash 临界区内执行。

## 系统模式

建议定义：

```c
typedef enum {
    SYSTEM_MODE_BOOT = 0,
    SYSTEM_MODE_RUN,
    SYSTEM_MODE_MAINTENANCE,
    SYSTEM_MODE_OTA,
    SYSTEM_MODE_FAULT,
} system_mode_t;
```

模式约束：

| 模式 | 允许行为 |
|---|---|
| `BOOT` | 上电初始化、自检、配置加载 |
| `RUN` | 同步触发、采样、SCPI 查询和轻量配置 |
| `MAINTENANCE` | 停止实时任务后允许 SD、OTA、配置写入 |
| `OTA` | 允许 Flash 擦写和升级状态机运行 |
| `FAULT` | 停止危险输出，保留诊断和有限维护入口 |

## 错误恢复与故障处理

### 故障分级

| 级别 | 名称 | 行为 | 恢复方式 |
|---|---|---|---|
| **F0** | 可恢复错误 | 记录 error_code，继续运行 | 自动清除或 SCPI `*CLS` |
| **F1** | 域级故障 | 域进入 FAULT 状态，其他域正常运行 | 显式 CLEAR 事件 |
| **F2** | 系统级故障 | 进入 `SYSTEM_MODE_FAULT`，停止危险输出 | 需复位或维护模式恢复 |
| **F3** | 致命故障 | Watchdog 复位 | 硬件自动复位 |

### FAULT 模式行为

```
进入条件：
  1. SafetyFB 发出 ENTER_FAULT
  2. Resource Arbiter 检测到不可恢复的资源冲突
  3. Watchdog 警告累积超过阈值

FAULT 模式下：
  - 所有危险输出停止（Trigger DISARM）
  - PIO/DMA 停用
  - OTA 禁止（Flash 操作中止）
  - 保留 SCPI 和诊断接口

恢复方式：
  1. 硬件复位（最可靠）
  2. 通过 SCPI 清除故障（如故障原因已消除）
  3. Bootloader 启动后自动进入 RUN 模式
```

### 事件队列溢出处理

环形队列满时丢弃最旧事件，记录 `overflow_count`，确保系统不会因队列满而死锁。

### Watchdog 策略

| 层次 | 机制 | 超时 | 行为 |
|---|---|---|---|
| 硬件 WDT | RP2350 硬件看门狗 | 1-2 s | 系统复位 |
| Task WDT (RTOS) | 每个 AO task 周期性标记 alive | 500 ms | 记录超时 task，可选系统复位 |
| OTA WDT | Flash 操作分片间喂狗 | 每次分片后 | 防止 Flash 操作饥饿看门狗 |
| CPU 心跳 (Trigger) | 可选，周期性检查 ARM 态 | 1 s | 超时自动 DISARM |

## 资源仲裁

建议资源位：

```c
typedef enum {
    SYS_RESOURCE_FLASH = 1u << 0,
    SYS_RESOURCE_SPI0  = 1u << 1,
    SYS_RESOURCE_USB   = 1u << 2,
    SYS_RESOURCE_PIO0  = 1u << 3,
    SYS_RESOURCE_PIO1  = 1u << 4,
    SYS_RESOURCE_PIO2  = 1u << 5,
    SYS_RESOURCE_DMA   = 1u << 6,
    SYS_RESOURCE_LCD   = 1u << 7,
    SYS_RESOURCE_SD    = 1u << 8,
} system_resource_t;
```

典型互锁：

| 操作 | 需要资源 |
|---|---|
| SCPI 在线 OTA | `FLASH + USB` |
| SD 卡离线 OTA | `FLASH + SPI0 + SD` |
| LCD 刷新 | `SPI0 + LCD` |
| SD 文件读取 | `SPI0 + SD` |
| PIO 输入采样 | `PIO0 + DMA` |
| PIO 输出触发 | `PIO1` |
| AUX PIO | `PIO2` |

### SPI 总线共享仲裁

LCD 和 SD 共用 SPI0（GPIO10=SCK, GPIO11=MOSI, GPIO12=MISO），CS 分别为 GPIO9 和 GPIO15。LCD 为仅写设备（无 MISO）。

| 操作 | 持有资源 | 冲突操作 | 处理 |
|---|---|---|---|
| LCD 刷新 | `SPI0 + LCD` | SD 卡读写 | SD 等待 LCD 释放 |
| SD 卡读写 | `SPI0 + SD` | LCD 刷新 | LCD 跳过本轮刷新，保持 dirty |
| OTA Flash 写 | `FLASH` | LCD 大块刷新 + SD 读写 | 二者均暂停 |
| OTA SD 离线 | `FLASH + SPI0 + SD` | LCD 刷新 | LCD 暂停 |

硬件约束：任意时刻只能有一个设备的 CS 有效。切换设备前必须等待上一个设备的最后一个 SCK 周期完成。

## OTA 设计

OTA 使用独立 `OtaVector`、`OtaAO` 和 `OtaFB`。

```text
SCPI/UI/SD
  -> ota_ao_post_event()
  -> OtaVector command slot
  -> OtaAO
  -> OtaFB ECC 状态转移表
  -> flash_job_scheduler
  -> drv_flash
  -> metadata / inactive slot
```

### portable_ota 库集成

项目已实现独立的 `third_party/portable_ota/` 库，作为 OTA 域的底层引擎被 `OtaAO/OtaFB` 调用，分层关系为：

```text
HAOFV 层:  OtaAO / OtaFB / OtaVector
             ↓ 调用
middleware: portable_ota_port/  (产品适配器，类型转换 + 布局断言)
             ↓ 调用
third_party: portable_ota/  (平台无关 OTA 核心)
             ├─ pota_core      — 状态机 + 公共 API
             ├─ pota_package   — manifest 解析
             ├─ pota_metadata  — 双副本 metadata 读写
             ├─ pota_crc32     — CRC32 计算
             └─ pota_compat    — 状态/错误/结果映射表
```

关键集成约束：
- `OtaFB` 不直接调用 `pota_*` 函数，通过 `middleware/portable_ota_port/` 适配
- 产品层 API 使用产品自有类型，不暴露 `pota_*` 类型
- `portable_ota_port` 使用 `_Static_assert` 保证布局兼容
- Bootloader 只链接 metadata/crc32/package 子集，不链接 session 接收路径

### OTA 状态

```text
IDLE
CHECK_PERMISSION
ERASE_SLOT
RECEIVING
VERIFYING
MARK_PENDING
READY_TO_REBOOT
FAILED
ABORTED
```

### OTA 事件

```text
BEGIN
DATA_BLOCK
END
ABORT
VERIFY
COMMIT
ROLLBACK
TICK
```

### SCPI OTA 命令

SCPI 命令只投递事件，不直接操作 Flash：

```text
SYST:OTA:STAT?
SYST:OTA:BEGIN <size>,<crc32>
SYST:OTA:PBEGIN <size>,<crc32>    # 统一 package 传输
SYST:OTA:DATA #<block>
SYST:OTA:END
SYST:OTA:ABOR
SYST:OTA:PROG?
SYST:OTA:BOOT
SYST:OTA:COMM
SYST:OTA:SLOT?
SYST:OTA:RES?
SYST:OTA:TXN?                     # copy transaction 状态
SYST:OTA:MODE?                    # COPY_TO_ACTIVE 或 DIRECT_AB
SYST:OTA:TARG?                    # 下一次 OTA 目标 slot
SYST:OTA:CAP?                     # Bootloader/OTA 能力位
```

禁止 SCPI 直接调用 Flash 擦写函数。

### Bootloader 启动策略

```text
如果存在 pending slot:
    如果 pending slot 校验通过且尝试次数未超限:
        启动 pending slot
    否则:
        回滚 confirmed slot
否则:
    启动 confirmed slot
```

Bootloader 支持两种模式：
- **COPY_TO_ACTIVE**（默认）：校验 Slot B，复制到 Slot A，从 Slot A 启动
- **DIRECT_AB**（已验证）：直接从 pending slot 启动，App 自检后 commit

### 写保护策略

| 区域 | 保护级别 | 机制 |
|---|---|---|
| Bootloader | **绝对保护** | OTA code 硬编码拒绝擦除 Bootloader 区域（`ota_partition.h` 边界检查） |
| App Slot A | **条件保护** | 仅当 Slot A 不是 confirmed 时才允许擦写 |
| App Slot B | 正常擦写 | OTA 目标 slot |
| Metadata | **规则保护** | 双副本，绝不擦除最后一个有效副本 |
| Product Config | **预留保护** | 独立写入 API，与 OTA 流程分离 |

### Golden Image 策略

- 当前：Factory UF2 = Bootloader + Slot A App + 空白 metadata；板端通过 BOOTSEL + UF2 做终极恢复
- 规划：Scratch 区预留 640 KB 可存放最小 Golden Image，或通过 SD 卡 `/factory/` 提供恢复包

## Trigger 设计

同步触发使用独立 `TriggerVector`、`TriggerAO` 和 `TriggerFB`。底层确定性时序仍由 `sync_io`、PIO、DMA 和 IRQ 实现。

```text
SCPI/UI/Button
  -> trigger command/event
  -> TriggerAO
  -> TriggerFB ECC 状态表
  -> sync_io / PIO / DMA
```

### 语义通道接口

Trigger 域的应用层接口必须使用稳定语义通道，而不是把任意 GPIO 暴露给 SCPI/UI：

```text
Input semantics:   TRIG_IN / ARM_IN / EXT_CLK_IN / GATE_IN
Output semantics:  TRIG_OUT / PULSE_OUT / SYNC_CLK_OUT / MARKER_OUT
```

GPIO16..GPIO23 的实际映射属于 board profile 和 `sync_io` 的职责。`TriggerAO`、`TriggerFB`、SCPI 和 UI 只能表达语义意图，不能把产品功能设计成任意 GPIO 交叉开关。

### 主触发口与 AUX 功能口

| 接口 | 角色 | 语义 |
|---|---|---|
| 主输入 IN0..IN3 / GPIO16..19 | 模式本地高速输入 | `TRIG_IN`、`GATE_IN`、编码器 A/B/Z、后续计数/采样输入 |
| 主输出 OUT0..OUT3 / GPIO20..23 | 模式本地高速输出 | `TRIG_OUT`、`PULSE_OUT`、`SEQ_OUT[3:0]` |
| AUX0..AUX3 / GPIO26..29 | 跨模式框架功能 | `ARM_IN`、`EXT_CLK_IN`、`SYNC_CLK_OUT`、`MARKER_OUT` |

### 模式资源约束

| 模式 | 应用层资源约束 |
|---|---|
| `SEQ_STEP` | OUT0..OUT3 被序列输出总线独占；独立主总线输出应返回 busy 或在 ARM 前关闭。`ARM_IN` 位于 AUX0。`SYNC_CLK_OUT`/`MARKER_OUT` 位于 AUX2/AUX3，不占用序列输出总线。 |
| `ENC_COUNT` | IN0/IN1/IN3 分别作为 A/B/Z；`ARM_IN` 位于 AUX0，不再与 B 相冲突。`GATE_IN` 如果仍定义在 IN3，则与 Z 相冲突，需单独仲裁或迁移。 |
| `IDLE` | 语义输出可由即时命令使用；语义输入只做采样/诊断或配置预览。 |

### GPIO 迁移约束

当前固件仍有旧实现把 `ARM_IN/EXT_CLK_IN/SYNC_CLK_OUT/MARKER_OUT` 绑定在 GPIO17/18/22/23。硬件冻结后，增量编码器固定使用 `SYNC_IO` 的 GPIO16/17/19；`GPIO26..29` 不再作为编码器输入组，而是固定两收两发 AUX 资源。产品化迁移应把框架功能收口到 AUX0..AUX3 的固定方向语义，或在冲突时由资源仲裁器拒绝命令。

### 触发模式扩展表

| 模式 | 输入占用 | 输出占用 | PIO | CPU | 状态 |
|---|---|---|---|---|---|
| `SEQ_STEP` (mode=1) | IN0 + 可选 IN3 | OUT0-3 | pio1/sm0 + DMA | ARM 后为零 | ✅ |
| `ENC_COUNT` (mode=2) | IN0/IN1/IN3 | OUT0 | pio1/sm0 + DMA | ARM 后为零 | ✅ |
| `GATE_LEVEL` (mode=3) | IN0 + IN3 | OUT0 | pio0/sm2 + pio1/sm0 | ARM 后为零 | 规划 |
| `ARM_SINGLE` (mode=4) | AUX0 | OUT0 | pio2/sm0 + pio1/sm0 | 每次触发 IRQ | 规划 |
| `FREE_BURST` (mode=5) | IN0 | OUT0-1 | pio1/sm0/sm2 | ARM 后为零 | 规划 |

新增模式只需在静态模式表中追加一行：

```c
static const trig_mode_entry_t s_mode_table[] = {
    { TRIG_MODE_SEQ_STEP,  SYS_RESOURCE_PIO1 | SYS_RESOURCE_DMA,
      (1u<<TRIG_STATE_IDLE), TRIG_CFG_SEQ_TABLE },
    { TRIG_MODE_ENC_COUNT, SYS_RESOURCE_PIO1 | SYS_RESOURCE_DMA,
      (1u<<TRIG_STATE_IDLE), TRIG_CFG_ENC_TARGET },
    // 新模式的入口：追加一行即可
};
```

### Trigger 域拒绝 OTA 条件

```text
如果 capture_running == true
或 sync_clock_running == true
或 trigger_armed == true
则 SYST:OTA:BEGIN 返回 busy
```

## Bootloader 与 OTA 安全链

推荐第一阶段采用：

```text
App 接收 OTA 包
  -> 写 inactive/staging 区
  -> App 校验 raw bin CRC 和向量表
  -> 写 metadata pending
  -> Bootloader 校验向量表和镜像
  -> 启动或搬运 App
  -> 新 App 自检通过后 commit
```

Bootloader 负责：

- 读取 metadata 双副本。
- 选择启动 Slot。
- 校验 raw bin CRC 和 App 向量表。
- 设置 VTOR/MSP 并跳转 App，或搬运 staging 到固定运行区。
- 处理 pending、commit、rollback。

App 负责：

- 接收固件。
- 分块写入。
- 计算 CRC。
- 设置 pending。
- 自检通过后 commit。

Bootloader 不引入 FreeRTOS。启动链路越小越可靠，OTA apply/rollback 必须在最小依赖下运行。Bootloader 不集成 SD/FatFs/UI/SCPI/网络。

## 表驱动使用范围

| 模块 | 表 |
|---|---|
| SCPI | 命令表 |
| IO | 引脚功能表 |
| PIO | PIO 资源分配表 |
| OTA | 状态转移表、错误码表 |
| Trigger | 模式表、动作表、状态转移表 |
| Function Block | Event/Input/Output 映射表、ECC 状态表 |
| Resource Arbiter | 资源冲突表 |
| UI | 页面表、菜单项表 |
| Diagnostics | 错误码/事件码表 |

## 配置管理

### 配置层次

| 层次 | 存储位置 | 内容 | 修改方式 |
|---|---|---|---|
| **编译期** | `config/project_config.h` | 版本号、循环周期、看门狗超时 | 代码修改 + 重新构建 |
| **板级** | `boards/rp2350_trig/inc/board_config.h` | 引脚映射、外设实例 | 代码修改 + 重新构建 |
| **产品参数** | W25Q32 Product Config 区 `0x350000` | 校准数据、序列号 | 专用 SCPI 命令或生产工具 |
| **运行时** | TriggerVector 配置快照 | 触发参数、序列表 | SCPI/UI 配置命令 |
| **OTA 策略** | OTA Metadata | slot 状态、版本策略、启动模式 | OTA 流程自动管理 |

Product Config 区（64 KB）当前预留，后续存放校准系数、序列号、硬件版本号和默认触发参数。

## 诊断数据流

### 数据分类

| 数据类别 | 存储位置 | 访问方式 |
|---|---|---|
| 当前错误码 | `SystemVector.error_code` + 各 DomainVector | SCPI `SYST:ERR?` / `STAT:*?` |
| 错误历史 | 诊断环形缓冲（RAM, ~128 B） | SCPI `SYST:ERR:COUN?` |
| 运行统计 | 各 DomainVector 计数字段 | SCPI `STAT:TRIG?` / `STAT:SYNC?` |
| OTA 审计 | metadata（Flash） | SCPI `SYST:OTA:RES?` / `SYST:OTA:TXN?` |
| 固件标识 | build id（Flash, 32 B） | SCPI `SYST:FW:BUILD?` |
| 故障日志 | Scratch 区（预留） | 后续实现 |

### DiagnosticsAO 接口

```c
void diagnostics_report_event(uint32_t domain, uint32_t error_code,
                              uint32_t timestamp_ms, const char *context);
void diagnostics_publish_heartbeat(void);
void diagnostics_get_summary(diagnostics_summary_t *summary);
```

系统心跳由 DiagnosticsAO 周期发布（1 Hz），包含运行时间、各域状态摘要、资源锁占用和故障锁存状态。RTOS 模式下各 AO task 周期性标记 alive，SystemAO 监控超时任务。

## FreeRTOS 集成约束

### 架构关系

```
HAOFV 是架构，
FreeRTOS 是调度器，
OSAL 是隔离层，
PIO/DMA/IRQ 是实时底座。
```

### 任务模型

| FreeRTOS Task | 承载对象 | 优先级 |
|---|---|---|
| `task_trigger` | `TriggerAO` | 5（最高） |
| `task_system` | `SystemManagerAO` | 4 |
| `task_ota` | `OtaAO` | 3 |
| `task_io_frontend` | SCPI/UI 输入入口 | 3 |
| `task_ui` | `UiAO` | 2 |
| `task_storage` | `StorageAO` | 2 |
| `task_diag` | `DiagnosticsAO` | 1 |

### 必须遵守的约束

- 业务组件禁止直接包含 `FreeRTOS.h`，只能通过 `osal/` 接口
- Bootloader 永远裸机，不引入 FreeRTOS
- 所有阻塞等待必须有 timeout
- ISR 只投递事件或设置标志，不执行复杂业务
- 硬实时路径（PIO/DMA/IRQ）不进入 RTOS 任务调度
- Vector Blackboard 调度阶段和写权限规则在 RTOS 下保持不变
- Flash 操作临界区内可能需要暂停 FreeRTOS 调度器

### 当前进度

已执行到 RTOS 移植方案 Step 4/8：CMake 开关、OSAL port 骨架、单任务入口、Event Bus 收口、Resource Arbiter、独立 task_trigger/task_ui 预留和 Trigger SCPI 事件收口均已完成。Release 构建仍保持 baremetal 默认。详见 `docs/RTOS_PORTING_PLAN.md`。

## 测试策略

### 测试分层

| 层次 | 框架 | 范围 | 示例 |
|---|---|---|---|
| **单元测试** | ARM GCC compile/object-build gate | portable_ota 库 | `test_portable_ota_core.c` |
| **集成测试** | 板端 SCPI smoke | AO 状态转移 | `STAT:TRIG?` 查询验证 |
| **OTA 闭环测试** | `ota_board_validate.py` | 完整 OTA + 负向矩阵 | 8+ 负向注入用例 |
| **时序验证** | 示波器 + `loopback_test.py` | PIO 延迟、脉宽精度 | GPIO22→GPIO16 回环 |
| **发布门禁** | `release_check.py` | 构建产物、安全检查 | 14 项检查 |
| **长稳测试** | 24h 连续运行 | 内存泄漏、看门狗 | RTOS Step 9 |

### 资源仲裁死锁检测

编译期通过资源依赖分析检查潜在死锁环。运行时资源申请带超时：超时后报告 `RESOURCE_ACQUIRE_TIMEOUT` 诊断事件并返回失败，不无限阻塞。

### AO 单元测试隔离

AO 单元测试通过 mock OSAL 和驱动层实现隔离：注入事件 → 执行 `ao_service()` → 验证状态转移和域向量变化。测试模式下可不限预算。

## 第一阶段落地顺序

1. 新增 `components/system_vector/`，先放 `SystemVector`、`OtaVector`、`TriggerVector` 摘要。
2. 新增 `components/system_manager/`，实现系统模式和资源仲裁。
3. 新增 `components/function_block/`，实现轻量功能块基础类型。
4. 新增 `components/event_bus/`，实现轻量事件投递。
5. 改造 `ota_manager` 为 `OtaAO + OtaFB`，使用事件投递和 ECC 状态转移表。
6. 改造同步触发上层为 `TriggerAO + TriggerFB`，底层继续复用 `sync_io`。
7. SCPI 的 OTA 命令只写事件，不直接改 OTA 状态。
8. `app_run_once()` 中加入 `system_manager_service()` 和各 AO service。
9. 增加 `drivers/mcu/flash/`。
10. 增加 OTA metadata 和 raw bin 校验。
11. 拆出 Bootloader 工程。
12. 接入 SD 卡和 FatFs 后扩展离线 OTA。

## 风险与约束

| 风险 | 约束 |
|---|---|
| Vector 退化成全局变量 | 严格写权限，所有写入走 API |
| 表项回调隐藏阻塞 | 表只描述规则，耗时动作由 service 分步执行 |
| OTA 影响实时触发 | OTA 只能在维护/OTA 模式运行，Flash 操作限时调度 |
| LCD/SD 共享 SPI 冲突 | 所有 SPI 使用走 Resource Arbiter + CS 互斥 |
| SCPI 与日志共用 USB CDC | OTA 期间暂停周期日志或降低输出 |
| 完整 IEC 61499 过重 | 只采用固定功能块、静态事件连接和表驱动 ECC |
| 架构过度设计 | 第一阶段只落地 SystemVector、OtaVector、OtaAO/OtaFB、资源仲裁和 OTA |
| Flash 擦写阻塞主循环 | Flash job 分步异步执行，XIP 临界区保护，操作前后喂狗 |
| 事件 payload 悬垂指针 | 内联拷贝 + 所有权约定 + ISR 只用无锁原子 |
| Bootloader 被意外覆盖 | 写保护 + 边界硬编码检查 |
| FreeRTOS 破坏硬实时 | PIO/DMA/IRQ 永远不进入 RTOS 任务调度 |
| GPIO 配置漂移 | 产品 pinout 冻结后移除开发板兼容宏 |

## 最终结论

当前项目推荐采用：

```text
整体：融合型主动对象功能块向量架构
上层：Active Object / Domain Service
中层：轻量 IEC 61499 Function Block 子集
底层：Time-Synchronized Vector Blackboard
局部：表驱动
硬件访问：RTE-like Service Layer
安全边界：Resource Arbiter
实时保证：PIO/DMA + 管理域限时调度
OTA 引擎：portable_ota 平台无关库
RTOS 隔离：OSAL 抽象层
```

这套方案比单纯表驱动更适合中型嵌入式系统，也比完整 AUTOSAR、完整 IEC 61499 运行时或重型 RTOS 架构更轻。它适合 RP2350_TRIG 后续扩展 OTA、SD 卡、LCD、SCPI、第三方库、Bootloader 和 RTOS。
