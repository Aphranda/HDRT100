# 融合型系统向量黑板与功能块架构方案

本文档定义 RP2350_TRIG 后续产品化演进采用的软件架构。目标是在保持裸机/Pico SDK 工程轻量性的同时，融合 Active Object、轻量 IEC 61499 功能块、时间同步型系统向量黑板、资源仲裁和表驱动状态机，为 OTA、同步触发、SD 卡、LCD、SCPI、诊断、后续 RTOS 和更多硬件模块提供清晰边界。

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

Vector 是数据表，但“时间”由调度器保证。也就是说，Vector 按固定调度阶段更新、快照和提交。

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
    const void *payload;
    uint32_t payload_size;
} fb_event_t;

typedef struct {
    uint32_t instance_id;
    uint32_t state;
    uint32_t error_code;
} fb_context_t;

typedef fb_result_t (*fb_execute_fn)(fb_context_t *context, const fb_event_t *event);
```

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

当前裸机版本建议由主循环驱动：

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

LCD 和 SD 共享 SPI SCK/MOSI，因此 `LCD` 和 `SD` 不能同时持有 `SPI0`。

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

OTA 状态建议：

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

OTA 事件建议：

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

SCPI 命令只投递事件：

```text
SYST:OTA:STAT?
SYST:OTA:BEGIN <size>,<crc32>
SYST:OTA:DATA #<block>
SYST:OTA:END
SYST:OTA:ABOR
SYST:OTA:PROG?
```

禁止 SCPI 直接调用 Flash 擦写函数。

## Trigger 设计

同步触发使用独立 `TriggerVector`、`TriggerAO` 和 `TriggerFB`。底层确定性时序仍由 `sync_io`、PIO、DMA 和 IRQ 实现。

```text
SCPI/UI/Button
  -> trigger command/event
  -> TriggerAO
  -> TriggerFB ECC 状态表
  -> sync_io / PIO / DMA
```

Trigger 域的应用层接口必须使用稳定语义通道，而不是把任意 GPIO 暴露给 SCPI/UI：

```text
Input semantics:
  TRIG_IN
  ARM_IN
  EXT_CLK_IN
  GATE_IN

Output semantics:
  TRIG_OUT
  PULSE_OUT
  SYNC_CLK_OUT
  MARKER_OUT
```

GPIO16..GPIO23 的实际映射属于 board profile 和 `sync_io` 的职责。`TriggerAO`、
`TriggerFB`、SCPI 和 UI 只能表达“使用 ARM_IN 作为 arm qualifier”、
“打开 SYNC_CLK_OUT”这类语义意图，不能把产品功能设计成任意 GPIO 交叉开关。
需要暴露原始 GPIO 的命令只能作为开发诊断或板级 profile 配置，并且必须经过
资源仲裁。

产品接口分为主触发口和 AUX 功能口：

| 接口 | 角色 | 语义 |
|---|---|---|
| 主输入 IN0..IN3 / GPIO16..19 | 模式本地高速输入 | `TRIG_IN`、`GATE_IN`、编码器 A/B/Z、后续计数/采样输入 |
| 主输出 OUT0..OUT3 / GPIO20..23 | 模式本地高速输出 | `TRIG_OUT`、`PULSE_OUT`、`SEQ_OUT[3:0]` |
| AUX0..AUX3 / GPIO26..29 | 跨模式框架功能 | `ARM_IN`、`EXT_CLK_IN`、`SYNC_CLK_OUT`、`MARKER_OUT` |

这样主输入/输出更加纯粹：触发模式可以独占主口做硬实时闭环，框架层
ARM、参考时钟、同步输出和状态标记不再与编码器 B/Z 或序列 bit2/bit3 抢通道。

当前统一物理 IO 下，语义通道会被触发模式和 AUX 功能口共同约束：

| 模式 | 应用层资源约束 |
|---|---|
| `SEQ_STEP` | OUT0..OUT3 被序列输出总线独占；独立主总线输出应返回 busy 或在 ARM 前关闭。`ARM_IN` 位于 AUX0，后续可作为管理面/资格输入接入。`SYNC_CLK_OUT`/`MARKER_OUT` 产品目标位于 AUX2/AUX3，不应占用序列输出总线。 |
| `ENC_COUNT` | IN0/IN1/IN3 分别作为 A/B/Z；`ARM_IN` 位于 AUX0，因此不再与 B 相冲突。`GATE_IN` 如果仍定义在 IN3，则与 Z 相冲突，需要未来单独仲裁或迁移。 |
| `IDLE` | 语义输出可由即时命令使用；语义输入只做采样/诊断或配置预览。 |

当前固件仍有部分旧实现把 `ARM_IN/EXT_CLK_IN/SYNC_CLK_OUT/MARKER_OUT` 绑定在
GPIO17/18/22/23。产品化迁移应把这些框架功能搬到 AUX0..AUX3，并保留
`GPIO26..29` 编码器输入组作为开发诊断复用，而不是量产默认模式。

Trigger 域可以拒绝 OTA：

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
| LCD/SD 共享 SPI 冲突 | 所有 SPI 使用走 Resource Arbiter |
| SCPI 与日志共用 USB CDC | OTA 期间暂停周期日志或降低输出 |
| 完整 IEC 61499 过重 | 只采用固定功能块、静态事件连接和表驱动 ECC |
| 架构过度设计 | 第一阶段只落地 SystemVector、OtaVector、OtaAO/OtaFB、资源仲裁和 OTA |

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
```

这套方案比单纯表驱动更适合中型嵌入式系统，也比完整 AUTOSAR、完整 IEC 61499 运行时或重型 RTOS 架构更轻。它适合 RP2350_TRIG 后续扩展 OTA、SD 卡、LCD、SCPI、第三方库、Bootloader 和 RTOS。
