# HAOFV 架构实施指南

本文档是 `HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md` 的实施补充，提供具体代码示例、迁移步骤和实现细节。阅读本文档前应先通读主架构文档。

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

## 2. GPIO 迁移路径与兼容代码

### 2.1 迁移状态

| 信号 | 旧 GPIO (当前代码) | 目标 GPIO (产品) | 迁移状态 |
|---|---|---|---|
| `ARM_IN` | GPIO17 | GPIO26 (AUX0) | ❌ 未迁移 |
| `EXT_CLK_IN` | GPIO18 | GPIO27 (AUX1) | ❌ 未迁移 |
| `SYNC_CLK_OUT` | GPIO22 | GPIO28 (AUX2) | ❌ 未迁移 |
| `MARKER_OUT` | GPIO23 | GPIO29 (AUX3) | ❌ 未迁移 |

### 2.2 迁移步骤

```
阶段 1：新增 AUX 语义别名宏 (✅ 已完成)
  board_config.h 新增 BOARD_AUX_ARM_IN_PIN=26,
  BOARD_AUX_EXT_CLK_IN_PIN=27,
  BOARD_AUX_SYNC_CLK_OUT_PIN=28,
  BOARD_AUX_MARKER_OUT_PIN=29

阶段 2：迁移 sync_io 输出路径
  SYNC_CLK_OUT: pio1/sm1 → pio2/sm2, GPIO22 → GPIO28
  MARKER_OUT:   pio1/sm3 → pio2/sm3, GPIO23 → GPIO29

阶段 3：接入 ARM_IN 管理面
  pio2/sm0 配置为 ARM_IN 资格输入
  TriggerFB 增加 AUX owner/arbiter

阶段 4：释放主输出总线
  GPIO22/23 变为纯主输出总线 OUT2/OUT3
  GPIO17/18 用于 ENC_COUNT 模式内主输入组
```

### 2.3 向后兼容代码

```c
// board_config.h 中的兼容层
// 产品 pinout 冻结后移除此兼容层，防止配置漂移
#if defined(BOARD_REV_A)  // 旧版开发板
  #define BOARD_SYNC_CLK_OUT_PIN  22
  #define BOARD_MARKER_OUT_PIN    23
  #define BOARD_ARM_IN_PIN        17
  #define BOARD_EXT_CLK_IN_PIN    18
#else  // 新版量产板 (默认)
  #define BOARD_SYNC_CLK_OUT_PIN  28  // AUX2
  #define BOARD_MARKER_OUT_PIN    29  // AUX3
  #define BOARD_ARM_IN_PIN        26  // AUX0
  #define BOARD_EXT_CLK_IN_PIN    27  // AUX1
#endif
```

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
- **主架构文档**：[HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md](HYBRID_VECTOR_BLACKBOARD_ARCHITECTURE.md)
- **关联文档**：`docs/OTA方案.md`、`docs/RTOS_PORTING_PLAN.md`、`docs/PIO_RESOURCE_PLAN.md`、`docs/SYNC_TRIGGER_TODO.md`
