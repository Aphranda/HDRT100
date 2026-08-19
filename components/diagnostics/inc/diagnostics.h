#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#define DIAGNOSTICS_WATCHDOG_EVIDENCE_MAGIC 0x57445445u

typedef enum {
    DIAG_LEVEL_DEBUG = 0,
    DIAG_LEVEL_INFO,
    DIAG_LEVEL_WARN,
    DIAG_LEVEL_ERROR,
    DIAG_LEVEL_COUNT,
} diag_level_t;

typedef struct {
    diag_level_t min_level;
    uint32_t emitted_count[DIAG_LEVEL_COUNT];
    uint32_t dropped_count[DIAG_LEVEL_COUNT];
    uint32_t truncated_count[DIAG_LEVEL_COUNT];
    uint32_t emit_failed_count[DIAG_LEVEL_COUNT];
    uint32_t queue_dropped_count;
    uint32_t queue_bytes;
    uint32_t queue_high_watermark;
    uint32_t persistent_queue_dropped_count;
    uint32_t persistent_queue_dropped_bytes;
    uint32_t persistent_queue_bytes;
    uint32_t persistent_queue_high_watermark;
} diagnostics_status_t;

typedef struct {
    uint32_t core0_loop_count;
    uint32_t core1_loop_count;
    uint32_t core0_last_ms;
    uint32_t core1_last_ms;
    bool core1_enabled;
} diagnostics_core_status_t;

typedef enum {
    DIAGNOSTICS_WATCHDOG_TASK_SYSTEM = 0,
    DIAGNOSTICS_WATCHDOG_TASK_USB_DEVICE,
    DIAGNOSTICS_WATCHDOG_TASK_SCPI,
    DIAGNOSTICS_WATCHDOG_TASK_REFMEM_SYNC,
    DIAGNOSTICS_WATCHDOG_TASK_LOOP_ENGINE,
    DIAGNOSTICS_WATCHDOG_TASK_CALIBRATION,
    DIAGNOSTICS_WATCHDOG_TASK_CONFIG_GATE,
    DIAGNOSTICS_WATCHDOG_TASK_OTA,
    DIAGNOSTICS_WATCHDOG_TASK_STORAGE,
    DIAGNOSTICS_WATCHDOG_TASK_UI,
    DIAGNOSTICS_WATCHDOG_TASK_CORE1,
    DIAGNOSTICS_WATCHDOG_TASK_COUNT,
} diagnostics_watchdog_task_t;

typedef struct {
    bool enabled;
    bool last_reset_watchdog;
    bool last_reset_timeout;
    uint32_t timeout_ms;
    uint32_t reset_reason;
    uint32_t evidence_magic;
    uint32_t evidence_expected_mask;
    uint32_t evidence_seen_mask;
    uint32_t evidence_stale_mask;
    uint32_t evidence_core0_loop_count;
    uint32_t evidence_core1_loop_count;
    uint32_t gate_required_mask;
    uint32_t last_seen_mask;
    uint32_t last_stale_mask;
    uint32_t supervisor_count;
} diagnostics_watchdog_status_t;

void diagnostics_init(void);
void diagnostics_service(uint32_t max_bytes);
void diagnostics_housekeeping_init(void);
void diagnostics_housekeeping_service(void);
void diagnostics_log(diag_level_t level, const char *module, const char *fmt, ...);
bool diagnostics_set_min_level(diag_level_t level);
diag_level_t diagnostics_get_min_level(void);
void diagnostics_get_status(diagnostics_status_t *status);
void diagnostics_record_core0_loop(void);
void diagnostics_record_core1_loop(void);
void diagnostics_get_core_status(diagnostics_core_status_t *status);
void diagnostics_watchdog_configure(uint32_t expected_mask);
void diagnostics_watchdog_enable(uint32_t timeout_ms);
void diagnostics_watchdog_task_heartbeat(diagnostics_watchdog_task_t task);
void diagnostics_watchdog_service(void);
void diagnostics_watchdog_request_test_stall(void);
void diagnostics_get_watchdog_status(diagnostics_watchdog_status_t *status);
void diagnostics_heartbeat(uint32_t period_ms);
void diagnostics_mark_fault(const char *module, const char *reason);
bool diagnostics_has_fault(void);

#define LOG_DEBUG(module, fmt, ...) diagnostics_log(DIAG_LEVEL_DEBUG, module, fmt, ##__VA_ARGS__)
#define LOG_INFO(module, fmt, ...) diagnostics_log(DIAG_LEVEL_INFO, module, fmt, ##__VA_ARGS__)
#define LOG_WARN(module, fmt, ...) diagnostics_log(DIAG_LEVEL_WARN, module, fmt, ##__VA_ARGS__)
#define LOG_ERROR(module, fmt, ...) diagnostics_log(DIAG_LEVEL_ERROR, module, fmt, ##__VA_ARGS__)

#endif
