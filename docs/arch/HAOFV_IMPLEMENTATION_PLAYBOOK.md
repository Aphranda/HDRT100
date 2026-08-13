# HAOFV 架构实施指南

Status: Active
Domain: HAOFV
Canonical: `docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_PORTING_PLAN.md`, `docs/sync/SYNC_IO_RESOURCE_PLAN.md`
Last updated: 2026-08-13

本文档是 `HAOFV_ARCHITECTURE.md` 的实施补充，提供具体代码示例、迁移步骤和实现细节。阅读本文档前应先通读主架构文档。

> 当前定位：本文档是实施 playbook 和历史迁移样例，不是当前产品硬件资源 canonical。
> HAOFV 顶层规则以 `docs/arch/HAOFV_ARCHITECTURE.md` 为准；RTOS + 双核产品化路径以
> `docs/arch/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md` 为准；GPIO、PIO、隔离、电源和网表事实以
> `docs/hardware/` 与 `docs/sync/SYNC_IO_RESOURCE_PLAN.md` 为准。

**内容**：

1. ECC 状态转移表实现示例
2. GPIO 迁移路径与兼容代码
3. Flash 异步 Job 实现细节
4. 附录：完整代码示例

---

## 1. ECC 状态转移表实现示例

### 1.1 ECC 表项结构

```c
typedef enum {
    FB_RESULT_IGNORED = 0,
    FB_RESULT_HANDLED,
    FB_RESULT_BUSY,
    FB_RESULT_ERROR,
} fb_result_t;

#define FB_STATE_ANY  UINT32_MAX   // 通配：匹配任意状态

typedef struct {
    uint32_t     current_state;
    uint32_t     event;
    bool         (*condition)(fb_context_t *ctx, const fb_event_t *evt);
    fb_result_t  (*action)(fb_context_t *ctx, const fb_event_t *evt);
    uint32_t     next_state;
} fb_ecc_entry_t;
```

### 1.2 OtaFB ECC 表（15 条规则）

```c
static const fb_ecc_entry_t s_ota_ecc_table[] = {
    // { 当前状态,            事件,                   条件,                       动作,                  下一状态            }

    // IDLE -> CHECK_PERMISSION
    {  OTA_STATE_IDLE,        OTA_EVENT_BEGIN,        ota_cond_begin_valid,       ota_act_begin,          OTA_STATE_CHECK_PERMISSION },

    // CHECK_PERMISSION -> ERASE_SLOT 或 FAILED
    {  OTA_STATE_CHECK_PERMISSION, OTA_EVENT_TICK,    ota_cond_resources_granted, ota_act_start_erase,    OTA_STATE_ERASE_SLOT  },
    {  OTA_STATE_CHECK_PERMISSION, OTA_EVENT_TICK,    ota_cond_resources_busy,    ota_act_fail_busy,      OTA_STATE_FAILED       },

    // ERASE_SLOT -> RECEIVING 或 FAILED
    {  OTA_STATE_ERASE_SLOT,  OTA_EVENT_FLASH_JOB_DONE,  ota_cond_erase_ok,      ota_act_init_receive,   OTA_STATE_RECEIVING    },
    {  OTA_STATE_ERASE_SLOT,  OTA_EVENT_FLASH_JOB_FAILED, NULL,                   ota_act_fail_flash,     OTA_STATE_FAILED       },

    // RECEIVING -> 保持 或 VERIFYING 或 ABORTED
    {  OTA_STATE_RECEIVING,   OTA_EVENT_DATA_BLOCK,   ota_cond_block_valid,       ota_act_write_block,    OTA_STATE_RECEIVING    },
    {  OTA_STATE_RECEIVING,   OTA_EVENT_END,          ota_cond_size_match,        ota_act_verify,         OTA_STATE_VERIFYING    },
    {  OTA_STATE_RECEIVING,   OTA_EVENT_ABORT,        NULL,                       ota_act_abort,          OTA_STATE_ABORTED      },

    // VERIFYING -> MARK_PENDING 或 FAILED
    {  OTA_STATE_VERIFYING,   OTA_EVENT_FLASH_JOB_DONE,  ota_cond_crc_vector_ok,  ota_act_mark_pending,   OTA_STATE_MARK_PENDING },
    {  OTA_STATE_VERIFYING,   OTA_EVENT_FLASH_JOB_FAILED, NULL,                    ota_act_fail_verify,    OTA_STATE_FAILED       },

    // MARK_PENDING -> READY_TO_REBOOT
    {  OTA_STATE_MARK_PENDING,OTA_EVENT_FLASH_JOB_DONE,  ota_cond_metadata_ok,    ota_act_publish_ready,  OTA_STATE_READY_TO_REBOOT },

    // READY_TO_REBOOT -> 保持
    {  OTA_STATE_READY_TO_REBOOT, OTA_EVENT_BOOT,      ota_cond_can_reboot,        ota_act_reboot,         OTA_STATE_READY_TO_REBOOT },

    // PENDING_CONFIRM -> COMMITTED
    {  OTA_STATE_PENDING_CONFIRM, OTA_EVENT_COMMIT,    ota_cond_self_test_ok,      ota_act_commit,         OTA_STATE_COMMITTED    },

    // 任意状态可 ABORT（不可中止阶段除外）
    {  FB_STATE_ANY,          OTA_EVENT_ABORT,         ota_cond_abort_allowed,     ota_act_abort,          OTA_STATE_ABORTED      },
};
```

### 1.3 ECC 执行引擎

```c
fb_result_t fb_ecc_execute(fb_context_t *ctx, const fb_event_t *event,
                           const fb_ecc_entry_t *table, uint32_t table_size) {
    for (uint32_t i = 0; i < table_size; i++) {
        const fb_ecc_entry_t *entry = &table[i];

        // 状态匹配（ANY 表示通配）
        if (entry->current_state != FB_STATE_ANY &&
            entry->current_state != ctx->state) {
            continue;
        }

        // 事件匹配
        if (entry->event != event->type) continue;

        // 条件检查
        if (entry->condition && !entry->condition(ctx, event)) continue;

        // 执行动作
        fb_result_t result = FB_RESULT_HANDLED;
        if (entry->action) {
            result = entry->action(ctx, event);
            if (result == FB_RESULT_ERROR) return result;
        }

        // 状态转移
        ctx->state = entry->next_state;
        return result;
    }

    // 没有匹配的规则 — 事件被静默忽略
    return FB_RESULT_IGNORED;
}
```

### 1.4 TriggerFB 实际规模

当前 TriggerFB 实现（`components/sync_trigger/src/trigger_fb.c`）包含 **58 条 ECC 规则**，覆盖：
- 6 个状态：`IDLE`, `SEQ_CONFIGURED`, `SEQ_ARMED`, `ENC_CONFIGURED`, `ENC_ARMED`, `FAULT`
- 20 个事件类型
- 每个表项 ~24 B（const，在 Flash 中），总表大小约 1.4 KB

---

## 2. 固定 SYNC_IO 硬件定义与模式引用

本节保留 2026-07 调试最小系统阶段的 SYNC_IO 固定资源样例，用于说明“语义 IO
不能被 SCPI/UI 随意重映射”的实现方法。它不冻结当前产品板 GPIO。当前硬件资源入口为：

- `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`：当前最小系统板约束。
- `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`：产品板约束。
- `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`：RP2350B QFN-80 IO 约束明细。
- `docs/sync/SYNC_IO_RESOURCE_PLAN.md`：PIO、DMA、语义 IO 和硬实时资源规划。

本项目的 SYNC_IO 不是可运行时重映射的 GPIO 池。硬件定义应由 board profile 和硬件约束文件固定；上层模式只能引用这些定义，不能通过兼容宏把硬件目标改来改去。

### 2.1 12 个 PIO State Machine 固定定义

| PIO | SM | 固定名称 | 固定/保留职责 |
|---|---:|---|---|
| `pio0` | `sm0` | `CAPTURE` | `GPIO16..19` 4 bit 输入采样。 |
| `pio0` | `sm1` | `TIMESTAMP_RESERVED` | 预留给时间戳、捕获 strobe 或外部参考输入处理。 |
| `pio0` | `sm2` | `RJ45_TRIGGER_IN` | 固定对应 `GPIO19/RJ45_TRIG_IN`；gate/inhibit 是模式层解释。 |
| `pio0` | `sm3` | `ARM_RESERVED` | 预留给硬件 ARM/DISARM 握手和捕获窗口控制。 |
| `pio1` | `sm0` | `MAIN_OUTPUT` | 主确定性输出；`TRIG_OUT`、`SEQ_STEP`、`ENC_COUNT` 互斥复用。 |
| `pio1` | `sm1` | `MAIN_OUT2_RESERVED` | 已释放为 OUT2/模式本地输出；不再承载框架层 `SYNC_CLK_OUT`。 |
| `pio1` | `sm2` | `MAIN_PULSE` | `GPIO21/PULSE_OUT` 第二路脉冲输出。 |
| `pio1` | `sm3` | `MAIN_OUT3_RJ45_TRIGGER` | `GPIO23/RJ45_TRIG_OUT`；`MARK:*` 仅为历史兼容入口。 |
| `pio2` | `sm0` | `AUX0_ARM` | `GPIO26/AUX0` 固定输入；`ARM_IN` 或 BiSS `CLK_IN` persona。 |
| `pio2` | `sm1` | `AUX1_EXT_CLK` | `GPIO27/AUX1` 固定输入；`EXT_CLK_IN` 或 BiSS `DATA_IN` persona。 |
| `pio2` | `sm2` | `AUX2_SYNC_CLK` | `GPIO28/AUX2` 固定输出；`SYNC_CLK_OUT` 或 BiSS `CLK_OUT` persona。 |
| `pio2` | `sm3` | `AUX3_TX` | `GPIO29/AUX3` 固定输出；BiSS `DATA_OUT` 或 AUX TX persona。 |

### 2.2 Board Profile 写死定义

`board_config.h` 和 `sync_io_hw_profile.h` 应表达同一套固定硬件事实。示例：

```c
#define BOARD_SYNC_PIO_FAST pio0
#define BOARD_SYNC_PIO_WAVE pio1
#define BOARD_SYNC_PIO_AUX  pio2

#define BOARD_SYNC_CAPTURE_SM    0u
#define BOARD_SYNC_TIMESTAMP_SM  1u
#define BOARD_SYNC_RJ45_TRIG_IN_SM 2u
#define BOARD_SYNC_QUALIFIER_SM    BOARD_SYNC_RJ45_TRIG_IN_SM
#define BOARD_SYNC_ARM_SM        3u

#define BOARD_SYNC_OUTPUT_SM 0u
#define BOARD_SYNC_CLOCK_SM  1u
#define BOARD_SYNC_GATE_SM   2u
#define BOARD_SYNC_MARKER_SM 3u

#define BOARD_SYNC_AUX0_SM 0u
#define BOARD_SYNC_AUX1_SM 1u
#define BOARD_SYNC_AUX2_SM 2u
#define BOARD_SYNC_AUX3_SM 3u

#define BOARD_SYNC_INPUT_BASE_PIN  16u
#define BOARD_SYNC_OUTPUT_BASE_PIN 20u
#define BOARD_SYNC_TRIG_IN_PIN     16u
#define BOARD_SYNC_RJ45_TRIG_IN_PIN  19u
#define BOARD_SYNC_GATE_IN_PIN       BOARD_SYNC_RJ45_TRIG_IN_PIN
#define BOARD_SYNC_TRIG_OUT_PIN    20u
#define BOARD_SYNC_PULSE_OUT_PIN   21u
#define BOARD_SYNC_RJ45_TRIG_OUT_PIN 23u

#define BOARD_SYNC_AUX0_PIN 26u
#define BOARD_SYNC_AUX1_PIN 27u
#define BOARD_SYNC_AUX2_PIN 28u
#define BOARD_SYNC_AUX3_PIN 29u

#define BOARD_SYNC_AUX_ARM_IN_PIN       BOARD_SYNC_AUX0_PIN
#define BOARD_SYNC_AUX_EXT_CLK_IN_PIN   BOARD_SYNC_AUX1_PIN
#define BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN BOARD_SYNC_AUX2_PIN
#define BOARD_SYNC_AUX3_OUT_PIN         BOARD_SYNC_AUX3_PIN

/* Deprecated compatibility aliases only. They must not redefine hardware. */
#define BOARD_SYNC_RJ45_TRIGGER_SM BOARD_SYNC_MARKER_SM
#define BOARD_SYNC_MARKER_OUT_PIN  BOARD_SYNC_RJ45_TRIG_OUT_PIN
```

规则：

- `GPIO16..19` 是固定主输入组；`ENC_COUNT` 软件定义为 A/B/Z=`16/17/18`。
- `GPIO19` 的硬件语义是 `RJ45_TRIG_IN`；`GATE_IN` 是模式层解释。
- `pio0/sm2` 的硬件 owner 是 `RJ45_TRIGGER_IN`；资格判定、gate/inhibit 不能重新定义该 SM 的硬件归属。
- `GPIO20..23` 是固定主输出组；`GPIO23` 的硬件语义是 `RJ45_TRIG_OUT`。
- `GPIO26..29` 是固定 AUX 两收两发；AUX0/1 输入，AUX2/3 输出。
- `MARKER_OUT` 不再作为独立硬件定义；历史 `MARK:*` 命令只能兼容到 `RJ45_TRIG_OUT`。

### 2.3 分模式引用规则

| 模式/功能 | 引用的固定资源 | 说明 |
|---|---|---|
| `IDLE` 即时 `TRIG:IMM` | `pio1/sm0`, `GPIO20/TRIG_OUT` | 只在主输出总线未被 mode 占用时允许。 |
| `IDLE` 即时 `PULS:IMM` | `pio1/sm2`, `GPIO21/PULSE_OUT` | 第二路脉冲输出，armed 冲突时应拒绝。 |
| `MARK:*` 兼容命令 | `pio1/sm3`, `GPIO23/RJ45_TRIG_OUT` | deprecated 兼容入口，不代表独立 marker 硬件。 |
| `SEQ_STEP` | `pio1/sm0`, `GPIO16..19`, `GPIO20..23`, DMA | 独占主输入/输出总线；IN3 硬件为 `RJ45_TRIG_IN`，模式内可解释为 gate/inhibit。 |
| `ENC_COUNT` | `pio1/sm0`, A/B/Z=`GPIO16/GPIO17/GPIO18`, OUT=`GPIO20`, DMA | ENC 软件定义固定为 3-pin A/B/Z，不占用 `GPIO19/RJ45_TRIG_IN`，不再支持 `TRIG:ENC:APIN 26`。 |
| `BISS_TAP` | `pio2/sm0/sm2/sm3`, AUX0..AUX3 | AUX0->AUX2 透传 `CLK`，AUX1->AUX3 透传 `DATA`，独占 AUX persona。 |
| `SYNC_CLK_OUT` 路径 | `pio2/sm2`, `GPIO28/AUX2` | 当前固件已迁移；通过 `PIO2 + AUX` 资源仲裁与 BiSS/AUX persona 互斥。 |
| `ARM_IN` 目标路径 | `pio2/sm0`, `GPIO26/AUX0` | 作为管理面资格输入，不占用主输入组。 |
| `EXT_CLK_IN` 目标路径 | `pio2/sm1`, `GPIO27/AUX1` | 作为外部参考/采样时钟输入。 |

### 2.4 迁移策略

迁移不再改变硬件定义，只改变“哪个功能开始引用已冻结的定义”：

1. 先把 board profile 和 `sync_io_hw_profile.h` 固定为上述硬件事实。
2. mode driver 通过 `sync_io_mode_ops_t.resources` 和 `.hw` 声明固定资源占用。
3. TriggerFB 在 ARM 前按 mode ops 派生资源 owner；底层 `sync_io_*_arm()` 不重复 acquire。
4. 旧 SCPI/UI 字段若必须保留，只能作为 deprecated alias，最终仍引用固定硬件语义。

---

## 3. Flash 异步 Job 实现细节

### 3.1 Job 状态机

```c
typedef enum {
    FLASH_JOB_NONE = 0,
    FLASH_JOB_ERASE_SLOT,
    FLASH_JOB_PROGRAM_BLOCK,
    FLASH_JOB_READBACK_VERIFY,
    FLASH_JOB_VERIFY_IMAGE_CRC,
    FLASH_JOB_WRITE_METADATA,
} flash_job_type_t;

typedef enum {
    FLASH_JOB_IDLE = 0,
    FLASH_JOB_BUSY,
    FLASH_JOB_DONE,
    FLASH_JOB_FAILED,
} flash_job_result_t;

typedef struct {
    flash_job_type_t  type;
    flash_job_result_t state;
    uint32_t          target_slot;
    uint32_t          offset;           // 当前分片偏移
    uint32_t          total_size;       // 总工作量
    uint32_t          slice_done;       // 已完成分片
    const uint8_t    *data;             // 写入数据指针
    uint32_t          data_len;
    uint32_t          crc32_accum;      // CRC 累积值
    uint32_t          last_error;
} flash_job_t;
```

### 3.2 分片执行

```c
flash_job_result_t flash_job_execute_slice(flash_job_t *job) {
    switch (job->type) {
    case FLASH_JOB_ERASE_SLOT: {
        uint32_t sector_addr = job->offset + job->slice_done * 4096;
        if (drv_flash_erase_sector(sector_addr) != 0) {
            job->last_error = OTA_ERR_FLASH_ERASE;
            return job->state = FLASH_JOB_FAILED;
        }
        job->slice_done++;
        if (sector_addr + 4096 >= job->offset + job->total_size) {
            return job->state = FLASH_JOB_DONE;
        }
        return job->state = FLASH_JOB_BUSY;
    }

    case FLASH_JOB_PROGRAM_BLOCK: {
        uint32_t addr = job->offset + job->slice_done * 256;
        uint32_t len = MIN(256, job->data_len - job->slice_done * 256);
        if (drv_flash_program_page(addr, &job->data[job->slice_done * 256], len) != 0) {
            job->last_error = OTA_ERR_FLASH_PROGRAM;
            return job->state = FLASH_JOB_FAILED;
        }
        job->slice_done++;
        if (job->slice_done * 256 >= job->data_len) {
            return job->state = FLASH_JOB_DONE;
        }
        return job->state = FLASH_JOB_BUSY;
    }

    case FLASH_JOB_VERIFY_IMAGE_CRC: {
        uint32_t block_start = job->slice_done * 1024;
        uint32_t block_size = MIN(1024, job->total_size - block_start);
        job->crc32_accum = crc32_update(job->crc32_accum,
                          (const void *)(XIP_BASE + job->offset + block_start),
                          block_size);
        job->slice_done++;
        if (block_start + block_size >= job->total_size) {
            return job->state = FLASH_JOB_DONE;
        }
        return job->state = FLASH_JOB_BUSY;
    }

    // ... 其他 job 类型类似
    }
    return FLASH_JOB_FAILED;
}
```

### 3.3 预算检查宏

```c
// AO service 内部的预算检查
#define AO_CHECK_BUDGET_RETURN(budget)  do { \
    budget.elapsed_us = time_us_since(budget.last_entry_us); \
    if (budget.elapsed_us >= budget.budget_us) return AO_BUSY; \
} while(0)

// Flash job 分片 + 预算检查 + 喂狗
#define FLASH_SLICE_AND_CHECK(budget, job)  do { \
    flash_job_result_t _r = flash_job_execute_slice(job); \
    if (_r == FLASH_JOB_FAILED) return FB_RESULT_ERROR; \
    feed_watchdog_if_needed(); \
    AO_CHECK_BUDGET_RETURN(budget); \
} while(0)
```

---

## 4. 附录：完整代码示例

### A.1 应用初始化序列

```c
// application/src/app.c
void app_init(void) {
    // 1. 硬件初始化
    board_init();

    // 2. 基础设施初始化（顺序敏感）
    event_bus_init();
    resource_arbiter_init();
    system_vector_init();

    // 3. AO 初始化（先被依赖者先初始化）
    diagnostics_ao_init();
    storage_ao_init();
    ui_ao_init();
    trigger_ao_init();
    ota_ao_init();  // 最后：依赖 resource_arbiter + event_bus

    // 4. 系统就绪
    system_manager_set_mode(SYSTEM_MODE_RUN);
}

// baremetal 主循环
void app_run_once(void) {
    scpi_port_service();           // 处理 USB CDC 输入
    event_bus_service();           // 分发积压事件

    system_manager_service();       // 快照输入、仲裁资源

    trigger_ao_service(500);       // 高优先级，小预算 (500 μs)
    ota_ao_service(2000);          // 低优先级，限时 (2000 μs)
    ui_ao_service(1000);           // 中优先级 (1000 μs)
    storage_ao_service(1000);      // 中优先级 (1000 μs)
    diagnostics_ao_service(500);   // 低频诊断 (500 μs)

    system_manager_publish();      // 提交输出、发布系统向量
}
```

### A.2 资源申请完整流程

```c
// OTA 开始时的资源申请（OtaFB action 函数）
fb_result_t ota_act_begin(fb_context_t *ctx, const fb_event_t *event) {
    ota_vector_t *v = ota_ao_get_vector(ctx);

    // 1. 检查系统模式
    system_mode_t mode = system_manager_get_mode();
    if (mode != SYSTEM_MODE_MAINTENANCE && mode != SYSTEM_MODE_OTA) {
        v->error_code = OTA_ERR_BUSY;
        return FB_RESULT_ERROR;
    }

    // 2. 检查触发域冲突
    trigger_summary_t ts;
    trigger_ao_get_summary(&ts);
    if (ts.state >= TRIG_STATE_SEQ_ARMED) {
        v->error_code = OTA_ERR_BUSY;
        return FB_RESULT_ERROR;
    }

    // 3. 申请资源
    uint32_t resources = SYS_RESOURCE_FLASH | SYS_RESOURCE_USB;
    if (!resource_arbiter_try_acquire(resources)) {
        v->error_code = OTA_ERR_BUSY;
        return FB_RESULT_ERROR;
    }

    // 4. 选择 target slot
    v->target_slot = (mode == SYSTEM_MODE_OTA && boot_mode == DIRECT_AB)
                     ? ota_get_inactive_slot() : OTA_SLOT_B;
    v->expected_size = event->payload.inline_data.size;
    v->crc32_expected = event->payload.inline_data.crc32;
    v->state = OTA_STATE_CHECK_PERMISSION;
    v->error_code = OTA_ERR_NONE;
    return FB_RESULT_HANDLED;
}
```

### A.3 Flash 分步写入

```c
// OTA DATA_BLOCK handler（OtaFB action 函数）
fb_result_t ota_act_write_block(fb_context_t *ctx, const fb_event_t *event) {
    ota_vector_t *v = ota_ao_get_vector(ctx);
    flash_job_t *job = ota_ao_get_flash_job(ctx);

    // 初始化 job（首次调用）
    if (job->state == FLASH_JOB_IDLE) {
        flash_job_init_program(job, v->target_slot,
                               v->received_size,
                               event->payload.inline_data,
                               event->payload_size);
    }

    // 执行一个分片
    flash_job_result_t res = flash_job_execute_slice(job);

    switch (res) {
    case FLASH_JOB_BUSY:
        return FB_RESULT_BUSY;  // 还有分片，下次 ao_service() 继续
    case FLASH_JOB_DONE:
        v->received_size += event->payload_size;
        v->progress_permille = (uint32_t)((uint64_t)v->received_size * 1000
                                          / v->expected_size);
        return FB_RESULT_HANDLED;
    case FLASH_JOB_FAILED:
        v->error_code = OTA_ERR_FLASH_PROGRAM;
        return FB_RESULT_ERROR;
    }
    return FB_RESULT_IGNORED;
}
```

### A.4 LCD 刷新中的 SPI 资源仲裁

```c
// UI service（已在实际代码中实现）
void app_ui_service(void) {
    if (!ui_dirty) return;
    if (!resource_arbiter_try_acquire(SYS_RESOURCE_SPI0 | SYS_RESOURCE_LCD)) {
        // 资源不可用：保持 dirty 标志，下次主循环重试
        return;
    }
    sync_config_ui_render();                                    // 执行渲染
    resource_arbiter_release(SYS_RESOURCE_SPI0 | SYS_RESOURCE_LCD);
    ui_dirty = false;
}
```

### A.5 Product Config 结构体（预留）

```c
// 位于 W25Q32 0x350000，当前预留未实现
typedef struct {
    uint32_t magic;              // 魔数
    uint32_t version;            // 配置结构版本
    char     serial_number[32];  // 产品序列号
    char     hardware_rev[8];    // 硬件版本号
    uint32_t calibration_date;   // 校准日期 (Unix timestamp)

    // 校准数据
    float    adc_calib[4];       // ADC 校准系数
    uint32_t clock_trim;         // 时钟微调值

    // 触发默认参数
    uint32_t default_trigger_width_us;
    uint32_t default_pulse_width_us;

    uint32_t config_crc32;       // 配置 CRC
} product_config_t;
```

---

## 文档版本

- **创建日期**：2026-06-29
- **主架构文档**：[HAOFV_ARCHITECTURE.md](HAOFV_ARCHITECTURE.md)
- **关联文档**：`docs/ota/OTA_SYSTEM_DESIGN.md`、`docs/arch/RTOS_PORTING_PLAN.md`、`docs/sync/SYNC_IO_RESOURCE_PLAN.md`、`docs/trigger/TRIGGER_SYNC_TODO.md`
