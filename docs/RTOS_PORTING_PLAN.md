# FreeRTOS 移植方案

Status: Active
Domain: RTOS
Canonical: `docs/RTOS_PORTING_PLAN.md`
Related: `docs/HAOFV_ARCHITECTURE.md`, `docs/MULTICORE_PARTITION_PLAN.md`, `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`
Last updated: 2026-07-07

本文档定义 RP2350_TRIG 在 OTA 基本收口后引入 FreeRTOS 的迁移方案。移植必须服从
`docs/HAOFV_ARCHITECTURE.md` 中定义的 HAOFV 架构，FreeRTOS 只作为调度器和同步原语提供者，不替代 Active Object、Function Block、Vector Blackboard 和 Resource Arbiter 的边界。

## 目标

- 在不破坏现有 OTA、Bootloader、PIO 同步触发路径的前提下，引入可维护的 RTOS 运行时。
- 保留 `OSAL` 抽象，业务组件不直接依赖 FreeRTOS API。
- 让 SCPI、OTA、LCD、SD、诊断等管理域获得更清晰的任务调度和资源互锁。
- 保持 PIO/DMA/IRQ 作为硬实时旁路，避免由 RTOS 调度精确定时边沿。

## 总体架构

```text
SCPI / UI / SD / Bootloader Result
        ↓
FreeRTOS Tasks as Active Object Containers
        ↓
Active Object Layer
        ↓
IEC 61499-style Function Block Layer
        ↓
Time-Synchronized Vector Blackboard
        ↓
Hardware Service Layer

Hard Real-Time Side Path:
PIO / DMA / IRQ
```

对应关系：

```text
FreeRTOS Task        = Active Object 的运行容器
Queue/EventGroup     = Active Object 的事件入口
Mutex/Semaphore      = Resource Arbiter 的底层实现
Tick/Timer           = Vector Blackboard 的时间同步来源
PIO/DMA/IRQ          = 硬实时执行路径
```

## 核心原则

### HAOFV 不被 RTOS 替代

FreeRTOS 迁移后仍保持以下边界：

- `OtaAO`、`TriggerAO`、`StorageAO`、`UiAO`、`DiagnosticsAO` 仍是功能域边界。
- `OtaFB`、`TriggerFB`、`SafetyFB` 等 Function Block 仍负责事件、数据和 ECC 状态迁移。
- `SystemVector` 和各 `DomainVector` 仍是系统事实源。
- 外部入口只投递事件，不直接修改功能域状态字段。
- 所有共享资源仍通过 `Resource Arbiter` 仲裁。

### 硬实时路径不进入 RTOS

同步触发、高速采样和精确输出边沿继续由下列路径完成：

```text
PIO
DMA
IRQ
Timer
```

RTOS 任务只负责：

- 配置下发。
- 启停控制。
- 状态摘要。
- 故障处理。
- 统计和诊断。

禁止依赖普通 FreeRTOS task 的调度延迟来产生精确触发边沿。

### Bootloader 保持裸机

Bootloader 不引入 FreeRTOS。原因：

- 启动链路越小越可靠。
- OTA apply/rollback 必须在最小依赖下运行。
- Bootloader 不需要复杂任务调度。

FreeRTOS 只进入 App 固件。

## 推荐任务模型

不要按“每个驱动一个任务”拆分，而是按 Active Object 运行容器拆分。

| FreeRTOS Task | 承载对象 | 职责 |
|---|---|---|
| `task_system` | `SystemManagerAO` | 系统模式、资源仲裁、Vector 快照发布 |
| `task_trigger` | `TriggerAO` | 触发域配置、状态机、安全检查，硬实时仍在 PIO/DMA；单核 RTOS 路径使用 |
| `task_ota` | `OtaAO` | OTA 事件、portable OTA core、metadata、flash job |
| `task_io_frontend` | SCPI/UI 输入入口 | 解析外部意图，只投递事件，不直接改状态 |
| `task_ui` | `UiAO` | LCD/U8G2 页面渲染、按键/UI 事件汇聚、SPI 显示访问 |
| `task_storage` | `StorageAO` | SD/FatFs、离线升级文件读取 |
| `task_diag` | `DiagnosticsAO` | 健康状态、错误码、运行统计 |

建议优先级：

| Task | 优先级 | 说明 |
|---|---:|---|
| `task_trigger` | 5 | 只做控制面和状态处理，不做长耗时动作 |
| `task_system` | 4 | 资源仲裁、模式切换、系统向量发布 |
| `task_ota` | 3 | Flash 写入分步执行，受系统模式限制 |
| `task_io_frontend` | 3 | SCPI/UI 输入，禁止长阻塞 |
| `task_ui` | 2 | 显示刷新与本地 UI 状态汇总，受 `SPI0/LCD` 互锁约束 |
| `task_storage` | 2 | SD 文件访问，可阻塞但必须有超时 |
| `task_diag` | 1 | 低频诊断 |
| idle | 0 | 空闲、低功耗、后台统计 |

## Vector Blackboard 调度

裸机 HAOFV 中的调度阶段为：

```text
1. Snapshot Input
2. Dispatch Events
3. Execute Function Blocks
4. Arbitrate Resources
5. Commit Outputs
6. Publish Diagnostics
```

FreeRTOS 后保留该节拍，由 `task_system` 驱动全局同步点：

```text
task_system periodic tick:
  snapshot_inputs()
  update_system_vector_sequence()
  publish_resource_grants()
  notify_active_object_tasks()
  collect_domain_summaries()
  publish_diagnostics_snapshot()
```

各 AO task 内部执行：

```text
wait event or system tick
read SystemVector snapshot
process local event queue
execute Function Block ECC
commit only own DomainVector
notify SystemManager if resource/state changed
```

写权限仍遵守 HAOFV 规则：

| 数据 | 允许写入者 |
|---|---|
| `TriggerVector.state` | 只能 `sync_trigger` 写 |
| `OtaVector.state` | 只能 `ota_manager` 写 |
| `SystemVector.system_mode` | 只能 `system_manager` 写 |
| `resource_locks` | 只能 `resource_arbiter` 写 |
| 查询快照 | 任意入口可读，不可直接写 |

RTOS + 双核 AMP 路径中，core0 运行 FreeRTOS 管理任务，core1 作为 TriggerAO
运行容器负责状态机推进。此时不创建 core0 `task_trigger`，避免两个执行上下文同时
消费 Trigger 事件队列或执行 TriggerFB ECC。SCPI/UI/Storage/OTA 仍通过事件投递
进入 TriggerAO，TriggerVector 只允许 TriggerAO owner 写入。

## 事件入口规则

SCPI、UI、SD、Bootloader result 等入口只投递事件：

```text
SCPI command
  -> parse
  -> build domain event
  -> ota_ao_post_event() / trigger_ao_post_event()
  -> return command accepted
```

禁止：

```text
SCPI -> ota_vector.state = ...
SCPI -> drv_flash_erase()
SCPI -> sync_io_start()
```

允许：

```text
SCPI -> OTA_EVENT_BEGIN
task_ota -> OtaAO -> OtaFB -> Resource Arbiter -> FlashJob
```

## OSAL 扩展

`osal/inc/osal.h` 需要在保持 baremetal port 可用的同时，补齐 FreeRTOS 所需接口。

基础接口：

```c
osal_task_create()
osal_task_delay_ms()
osal_tick_ms()
osal_queue_create()
osal_queue_send()
osal_queue_recv()
osal_event_flags_create()
osal_event_flags_set()
osal_event_flags_wait()
osal_mutex_create()
osal_mutex_lock()
osal_mutex_unlock()
osal_critical_enter()
osal_critical_exit()
osal_timer_create()
osal_timer_start()
```

Active Object 友好接口可后续增加：

```c
osal_event_queue_t
osal_mailbox_t
osal_periodic_notify_t
```

约束：

- 应用层和组件层禁止直接包含 `FreeRTOS.h`。
- FreeRTOS 头文件只允许出现在 `osal/port/freertos/` 和极少数平台适配文件中。
- 所有阻塞等待必须有 timeout。
- ISR 只投递事件或设置标志，不执行复杂业务。

## Resource Arbiter 的 FreeRTOS 实现

HAOFV 中定义的资源位保持不变：

```text
FLASH
SPI0
USB
PIO0
PIO1
PIO2
DMA
LCD
SD
```

FreeRTOS 底层实现建议：

| 资源类型 | 实现 |
|---|---|
| 独占资源 | Mutex |
| 状态资源 | EventGroup bit |
| ISR 到 AO 通知 | Queue 或 direct task notification |
| Flash 临界段 | critical section，必要时暂停调度 |
| PIO/DMA 配置 | mutex + 禁止运行态修改 |

关键互锁：

- OTA 申请 `FLASH` 前，`SystemManagerAO` 必须确认 Trigger 不在 `ARMED/RUNNING`。
- LCD 和 SD 共享 `SPI0`，必须通过 Resource Arbiter 串行化。
- SCPI 只负责传输和事件投递，不直接持有 `FLASH`。
- Flash erase/program 期间必须喂狗，并保证 USB/SCPI 不误判死机。

## OTA 迁移要求

OTA 已经具备 `OtaAO/OtaFB/OtaVector` 和 portable OTA core 的基础，FreeRTOS 后只改变运行容器。

当前裸机路径：

```text
app_run_once()
  -> ota_ao_service()
```

FreeRTOS 路径：

```text
task_ota()
  wait OTA event or periodic tick
  ota_ao_service()
```

SCPI 命令：

```text
task_io_frontend
  parse SYST:OTA:*
  post OTA event
  return accepted / rejected
```

只有 `task_ota` 可以持有并推进 OTA core context。其他任务必须通过事件队列发送意图。

OTA RTOS adapter 需要提供：

```text
flash_read(offset, buffer, size)
flash_erase(offset, size)
flash_program(offset, data, size)
mark_pending(slot, image_size, image_crc32)
confirm_active()
validate_vector(slot_offset, image_size, run_offset)
ota_lock()
ota_unlock()
ota_yield_or_delay()
feed_watchdog()
invalidate_cache()
reboot()
time_ms()
log(level, message)
```

`ota_lock()` 只保护平台适配层共享资源，例如 metadata 和 flash service，不允许多个任务同时修改同一个 OTA context。

## RP2350 风险

| 风险 | 约束 |
|---|---|
| XIP flash 擦写影响代码取指 | Flash driver 保持临界区策略，必要函数放 RAM |
| USB CDC 在长擦除中饥饿 | OTA flash job 分步执行，擦写间隙 yield/feed watchdog |
| Watchdog tick 变化 | task watchdog 和硬件 watchdog 分层管理 |
| Vector 退化成全局变量 | 所有写入通过 AO API 提交 |
| 表项回调隐藏阻塞 | 表只描述规则，耗时动作由 service 分步执行 |
| LCD/SD 共享 SPI 冲突 | Resource Arbiter 管理 `SPI0 + LCD/SD` |
| Trigger 运行中 OTA | 系统模式和资源仲裁拒绝 OTA |

## 迁移步骤

### Step 1 - 引入 FreeRTOS 但不改变 HAOFV

- 新增 `third_party/freertos/FreeRTOS-Kernel/`。
- 新增 `osal/port/freertos/`。
- 新增 `config/freertos/FreeRTOSConfig.h` 或等效配置目录。
- CMake 增加 `PROJECT_USE_FREERTOS=ON/OFF`。
- 默认保持 `OFF`，保证当前 release 路径稳定。

验证：

- baremetal release 构建和 OTA 一键验证仍通过。
- FreeRTOS 最小 target 可编译。
- RP2350 板端可以完成 RTOS smoke factory flash、SCPI baseline、正向 OTA、boot commit 和负向矩阵验证。

### Step 2 - 单任务运行当前 App

入口变为：

```text
main()
  board_init()
  osal_kernel_init()
  osal_task_create(task_system)
  osal_kernel_start()
```

`task_system` 临时运行：

```text
while (1) {
  app_run_once();
  osal_task_delay_ms(1);
}
```

实现经验：

- RP2350 FreeRTOS smoke 的 `app_bringup()` 更稳妥的做法是放入 `task_system` 内执行，而不是在 `main()` 中先完成全部 bring-up 再启动 scheduler。
- 在早期 RTOS 板端调试阶段，建议临时关闭 board watchdog enable/feed，并在 `configASSERT`、malloc failed、stack overflow hook 中保留可观测故障态；待任务拆分稳定后再恢复正式 watchdog 策略。

验证：

- LCD 正常。
- SCPI 查询正常。
- OTA 一键验证通过。

### Step 3 - RTOS 版 Event Bus

- 当前事件 API 不变。
- 先以独立 `event_bus` / mailbox 组件收口各 AO 的事件入口，再逐步切换到底层 FreeRTOS queue。
- ISR 事件使用 FromISR 或 OSAL ISR 包装。

验证：

- SCPI/UI 只投递事件，不直接改状态。
- OTA begin/data/end/boot/comm 语义不变。
- RTOS smoke 一键 OTA 闭环继续通过。

### Step 4 - RTOS 版 Resource Arbiter

- 先以轻量 `resource_arbiter` 组件落地共享资源位和最小申请/释放接口，再逐步演进到底层 mutex/event group。
- 对外资源申请 API 不变。
- 先覆盖 `FLASH`、`SPI0`、`USB`、`LCD`、`SD`。

验证：

- LCD 刷新和 SD 访问互斥。
- OTA 期间拒绝 Trigger 运行态冲突操作。
- RTOS smoke 一键 OTA 闭环继续通过。

### Step 5 - 拆出 `task_io_frontend`

- SCPI 解析独立运行。
- 查询类命令读 Vector snapshot。
- 控制类命令投递事件；当前 Trigger SCPI 已切到 `sync_trigger` 事件接口。

验证：

- SCPI 响应稳定。
- OTA 指令语义不变。
- Trigger SCPI 配置/启停/立即触发真机 smoke 通过。

### Step 6 - 拆出 `task_ota`

- `OtaAO` 独立任务运行。
- 等待 OTA queue 或周期 tick。
- Flash job 分步执行。
- 只有 `task_ota` 可以推进 OTA context。

验证：

- 正向统一 OTA package 通过。
- Bootloader `APPLIED` 通过。
- App `COMM` 通过。
- 负向矩阵通过。

### Step 7 - 拆出 UI 和 Storage

- `task_ui` 独立运行 LCD/U8G2 渲染和按键/UI 事件聚合，不再与 `task_io_frontend` 合并。
- `task_storage` 管理 SD/FatFs 和离线 OTA 文件读取。
- `LCD` 与 `SD` 通过 `SPI0` 互锁。

验证：

- LCD 刷新无明显卡顿。
- SD 文件读取不破坏 SCPI/OTA。

### Step 8 - 拆出 `task_trigger`

- 先以独立 `task_trigger` 运行 `sync_trigger` 运行态摘要和仲裁发布骨架，再逐步迁移完整触发控制面。
- PIO/DMA/IRQ 继续硬实时执行。
- IRQ 通过 queue/notification 上报事件。

验证：

- 示波器验证触发输出抖动不因 RTOS 增大。
- Trigger 运行态下 OTA 被正确拒绝或延后。

### Step 9 - 监控和长稳

必须增加：

- stack high water mark。
- heap 剩余量。
- queue depth。
- AO 执行预算超时统计。
- task watchdog。
- 24h 长稳测试。

## 验证门禁

每个阶段都必须至少执行：

- `pico2-release` 构建。
- `release_check=OK`。
- factory UF2 烧录。
- SCPI 基础查询。
- OTA 一键验证。

RTOS 等价 release 必须通过：

- Factory boot。
- 正向统一 package OTA。
- Bootloader apply 结果为 `APPLIED`。
- App commit 结果为 `COMMITTED`。
- release 固件不包含 destructive validation command。
- transport CRC failure。
- image CRC failure。
- App vector failure。
- package magic/version/size failure。
- slot mismatch failure。
- run-offset mismatch failure。
- 接收中复位后保留旧 confirmed image。
- Bootloader apply 中复位后可恢复或回滚。
- 未确认回滚。
- OTA 期间并发 UI/SCPI/Diagnostics 不饿死 watchdog。

## 最终形态

```text
FreeRTOS
  task_system
    SystemManagerAO
    SystemVector publish
    ResourceArbiter

  task_trigger
    TriggerAO
    TriggerFB
    SafetyFB
    PIO/DMA config only

  task_ota
    OtaAO
    OtaFB
    FlashJobFB
    MetadataFB

  task_io_frontend
    SCPI parser
    UI input event
    Query snapshot

  task_storage
    StorageAO
    FatFs
    Offline OTA source

  task_diag
    DiagnosticsAO
```

结论：

```text
HAOFV 是架构，
FreeRTOS 是调度器，
OSAL 是隔离层，
PIO/DMA/IRQ 是实时底座。
```

该方案可在不破坏当前 OTA 安全链和同步触发硬实时路径的前提下，逐步把项目演进到适合工业产品维护的 RTOS 架构。
