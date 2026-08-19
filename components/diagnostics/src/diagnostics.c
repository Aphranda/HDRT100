#include "diagnostics.h"

#include <stdarg.h>
#include <string.h>

#include "drv_watchdog.h"
#include "portable_log_port.h"
#include "pico/stdlib.h"
#include "project_config.h"

#define DIAGNOSTICS_LOG_SERVICE_BYTES 256u
#define DIAGNOSTICS_WATCHDOG_SERVICE_PERIOD_MS 100u

static bool s_fault_latched;
static volatile uint32_t s_core0_loop_count;
static volatile uint32_t s_core1_loop_count;
static volatile uint32_t s_core0_last_ms;
static volatile uint32_t s_core1_last_ms;
static uint32_t s_housekeeping_last_ms;
static volatile uint32_t s_watchdog_seen_mask;
static uint32_t s_watchdog_expected_mask;
static uint32_t s_watchdog_required_mask;
static uint32_t s_watchdog_last_service_ms;
static volatile bool s_watchdog_test_stall;
static bool s_watchdog_stale_logged;
static volatile uint32_t s_watchdog_status_sequence;
static diagnostics_watchdog_status_t s_watchdog_status;

static void diagnostics_watchdog_status_write_begin(void)
{
    (void)__atomic_add_fetch(&s_watchdog_status_sequence, 1u, __ATOMIC_ACQ_REL);
}

static void diagnostics_watchdog_status_write_end(void)
{
    (void)__atomic_add_fetch(&s_watchdog_status_sequence, 1u, __ATOMIC_RELEASE);
}

static void diagnostics_record_core_loop(volatile uint32_t *loop_count,
                                         volatile uint32_t *last_ms)
{
    const uint32_t count = *loop_count + 1u;
    *loop_count = count;
    if ((count & 0x3FFu) == 0u) {
        *last_ms = to_ms_since_boot(get_absolute_time());
    }
}

typedef struct {
    diag_level_t diag_level;
    portable_log_port_level_t port_level;
} diagnostics_log_level_map_t;

static const diagnostics_log_level_map_t s_log_level_map[DIAG_LEVEL_COUNT] = {
    [DIAG_LEVEL_DEBUG] = {DIAG_LEVEL_DEBUG, PORTABLE_LOG_PORT_LEVEL_DEBUG},
    [DIAG_LEVEL_INFO] = {DIAG_LEVEL_INFO, PORTABLE_LOG_PORT_LEVEL_INFO},
    [DIAG_LEVEL_WARN] = {DIAG_LEVEL_WARN, PORTABLE_LOG_PORT_LEVEL_WARN},
    [DIAG_LEVEL_ERROR] = {DIAG_LEVEL_ERROR, PORTABLE_LOG_PORT_LEVEL_ERROR},
};

static portable_log_port_level_t diagnostics_to_port_level(diag_level_t level)
{
    if ((uint32_t)level >= (uint32_t)DIAG_LEVEL_COUNT ||
        s_log_level_map[level].diag_level != level) {
        return PORTABLE_LOG_PORT_LEVEL_ERROR;
    }

    return s_log_level_map[level].port_level;
}

static diag_level_t diagnostics_from_port_level(portable_log_port_level_t level)
{
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        if (s_log_level_map[i].port_level == level) {
            return s_log_level_map[i].diag_level;
        }
    }

    return DIAG_LEVEL_INFO;
}

void diagnostics_init(void)
{
    s_fault_latched = false;
    s_core0_loop_count = 0u;
    s_core1_loop_count = 0u;
    s_core0_last_ms = 0u;
    s_core1_last_ms = 0u;
    s_housekeeping_last_ms = 0u;
    s_watchdog_seen_mask = 0u;
    s_watchdog_expected_mask = 0u;
    s_watchdog_required_mask = 0u;
    s_watchdog_last_service_ms = 0u;
    s_watchdog_test_stall = false;
    s_watchdog_stale_logged = false;
    s_watchdog_status_sequence = 0u;
    (void)memset(&s_watchdog_status, 0, sizeof(s_watchdog_status));
    portable_log_port_init();

    drv_watchdog_reset_snapshot_t reset;
    drv_watchdog_get_reset_snapshot(&reset);
    s_watchdog_status.last_reset_watchdog = reset.watchdog_caused_reboot;
    s_watchdog_status.last_reset_timeout = reset.watchdog_enable_caused_reboot;
    s_watchdog_status.reset_reason = reset.reason;
    s_watchdog_status.evidence_magic = reset.scratch[0];
    s_watchdog_status.evidence_expected_mask = reset.scratch[1];
    s_watchdog_status.evidence_seen_mask = reset.scratch[2] & 0xFFFFu;
    s_watchdog_status.evidence_stale_mask = reset.scratch[2] >> 16u;
    s_watchdog_status.evidence_core0_loop_count = reset.scratch[3] & 0xFFFFu;
    s_watchdog_status.evidence_core1_loop_count = reset.scratch[3] >> 16u;

    if (s_watchdog_status.last_reset_watchdog) {
        LOG_WARN("watchdog", "previous reset=%s reason=0x%08lx expected=0x%08lx seen=0x%08lx stale=0x%08lx",
                 s_watchdog_status.last_reset_timeout ? "TIMEOUT" : "SOFTWARE",
                 (unsigned long)s_watchdog_status.reset_reason,
                 (unsigned long)s_watchdog_status.evidence_expected_mask,
                 (unsigned long)s_watchdog_status.evidence_seen_mask,
                 (unsigned long)s_watchdog_status.evidence_stale_mask);
    }
}

void diagnostics_service(uint32_t max_bytes)
{
    portable_log_port_service(max_bytes);
}

void diagnostics_housekeeping_init(void)
{
    s_housekeeping_last_ms = to_ms_since_boot(get_absolute_time());
}

void diagnostics_housekeeping_service(void)
{
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    diagnostics_service(DIAGNOSTICS_LOG_SERVICE_BYTES);

    if ((uint32_t)(now_ms - s_housekeeping_last_ms) >= PROJECT_LOOP_PERIOD_MS) {
        s_housekeeping_last_ms = now_ms;
        diagnostics_heartbeat(PROJECT_HEALTH_LOG_PERIOD_MS);
    }
}

void diagnostics_log(diag_level_t level, const char *module, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    portable_log_port_vwrite(diagnostics_to_port_level(level), module, fmt, args);
    va_end(args);
}

bool diagnostics_set_min_level(diag_level_t level)
{
    if (level >= DIAG_LEVEL_COUNT) {
        return false;
    }

    return portable_log_port_set_min_level(diagnostics_to_port_level(level));
}

diag_level_t diagnostics_get_min_level(void)
{
    return diagnostics_from_port_level(portable_log_port_get_min_level());
}

void diagnostics_get_status(diagnostics_status_t *status)
{
    if (status == NULL) {
        return;
    }

    portable_log_port_status_t log_status;
    portable_log_port_get_status(&log_status);

    status->min_level = diagnostics_from_port_level(log_status.min_level);
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        const diag_level_t diag_level = s_log_level_map[i].diag_level;
        const portable_log_port_level_t port_level = s_log_level_map[i].port_level;
        status->emitted_count[diag_level] = log_status.emitted_count[port_level];
        status->dropped_count[diag_level] = log_status.dropped_count[port_level];
        status->truncated_count[diag_level] = log_status.truncated_count[port_level];
        status->emit_failed_count[diag_level] = log_status.emit_failed_count[port_level];
    }
    status->queue_dropped_count = log_status.queue_dropped_count;
    status->queue_bytes = log_status.queue_bytes;
    status->queue_high_watermark = log_status.queue_high_watermark;
    status->persistent_queue_dropped_count = log_status.persistent_queue_dropped_count;
    status->persistent_queue_dropped_bytes = log_status.persistent_queue_dropped_bytes;
    status->persistent_queue_bytes = log_status.persistent_queue_bytes;
    status->persistent_queue_high_watermark = log_status.persistent_queue_high_watermark;
}

void diagnostics_record_core0_loop(void)
{
    diagnostics_record_core_loop(&s_core0_loop_count, &s_core0_last_ms);
}

void diagnostics_record_core1_loop(void)
{
    diagnostics_record_core_loop(&s_core1_loop_count, &s_core1_last_ms);
}

void diagnostics_get_core_status(diagnostics_core_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->core0_loop_count = s_core0_loop_count;
    status->core1_loop_count = s_core1_loop_count;
    status->core0_last_ms = s_core0_last_ms;
    status->core1_last_ms = s_core1_last_ms;
#if PROJECT_USE_MULTICORE
    status->core1_enabled = true;
#else
    status->core1_enabled = false;
#endif
}

void diagnostics_watchdog_configure(uint32_t expected_mask)
{
    s_watchdog_expected_mask = expected_mask;
    s_watchdog_required_mask = (1u << DIAGNOSTICS_WATCHDOG_TASK_SYSTEM) |
                               (1u << DIAGNOSTICS_WATCHDOG_TASK_CORE1);
    s_watchdog_seen_mask = 0u;
    s_watchdog_last_service_ms = to_ms_since_boot(get_absolute_time());
    diagnostics_watchdog_status_write_begin();
    s_watchdog_status.last_seen_mask = 0u;
    s_watchdog_status.last_stale_mask = expected_mask;
    s_watchdog_status.gate_required_mask = s_watchdog_required_mask;
    diagnostics_watchdog_status_write_end();
}

void diagnostics_watchdog_enable(uint32_t timeout_ms)
{
    diagnostics_watchdog_status_write_begin();
    s_watchdog_status.timeout_ms = timeout_ms;
    s_watchdog_status.enabled = true;
    diagnostics_watchdog_status_write_end();
    drv_watchdog_enable(timeout_ms);
}

void diagnostics_watchdog_task_heartbeat(diagnostics_watchdog_task_t task)
{
    if ((uint32_t)task >= (uint32_t)DIAGNOSTICS_WATCHDOG_TASK_COUNT) {
        return;
    }

    (void)__atomic_fetch_or(&s_watchdog_seen_mask, 1u << (uint32_t)task, __ATOMIC_RELEASE);
}

void diagnostics_watchdog_service(void)
{
    if (!s_watchdog_status.enabled) {
        return;
    }

    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now_ms - s_watchdog_last_service_ms) <
        DIAGNOSTICS_WATCHDOG_SERVICE_PERIOD_MS) {
        return;
    }
    s_watchdog_last_service_ms = now_ms;

    const uint32_t seen_mask = __atomic_exchange_n(&s_watchdog_seen_mask, 0u, __ATOMIC_ACQ_REL);
    uint32_t stale_mask = s_watchdog_expected_mask & ~seen_mask;
    if (s_watchdog_test_stall) {
        /* Validation-only path: leave a deterministic core1 marker in the
         * retained evidence, then let the hardware watchdog expire. */
        stale_mask |= 1u << DIAGNOSTICS_WATCHDOG_TASK_CORE1;
    }
    diagnostics_core_status_t core;
    diagnostics_get_core_status(&core);
    drv_watchdog_write_evidence(DIAGNOSTICS_WATCHDOG_EVIDENCE_MAGIC,
                                s_watchdog_expected_mask,
                                seen_mask,
                                stale_mask,
                                core.core0_loop_count,
                                core.core1_loop_count);
    diagnostics_watchdog_status_write_begin();
    s_watchdog_status.last_seen_mask = seen_mask;
    s_watchdog_status.last_stale_mask = stale_mask;
    s_watchdog_status.supervisor_count++;
    diagnostics_watchdog_status_write_end();

    if ((stale_mask & s_watchdog_required_mask) == 0u) {
        s_watchdog_stale_logged = false;
        drv_watchdog_feed();
        return;
    }

    if (!s_watchdog_stale_logged) {
        s_watchdog_stale_logged = true;
        LOG_ERROR("watchdog", "health gate stale=0x%08lx seen=0x%08lx; reset pending",
                  (unsigned long)stale_mask,
                  (unsigned long)seen_mask);
    }
}

void diagnostics_watchdog_request_test_stall(void)
{
#if PROJECT_ENABLE_WATCHDOG_TEST
    s_watchdog_test_stall = true;
    LOG_WARN("watchdog", "validation stall requested; hardware reset expected");
#endif
}

void diagnostics_get_watchdog_status(diagnostics_watchdog_status_t *status)
{
    if (status == NULL) {
        return;
    }

    for (;;) {
        const uint32_t sequence_before =
            __atomic_load_n(&s_watchdog_status_sequence, __ATOMIC_ACQUIRE);
        if ((sequence_before & 1u) != 0u) {
            continue;
        }
        *status = s_watchdog_status;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t sequence_after =
            __atomic_load_n(&s_watchdog_status_sequence, __ATOMIC_RELAXED);
        if (sequence_before == sequence_after && (sequence_after & 1u) == 0u) {
            return;
        }
    }
}

void diagnostics_heartbeat(uint32_t period_ms)
{
#if PROJECT_ENABLE_HEALTH_LOG
    static uint32_t last_ms;
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    if ((uint32_t)(now_ms - last_ms) >= period_ms) {
        last_ms = now_ms;
        LOG_INFO("health", "alive fault=%u", s_fault_latched ? 1u : 0u);
    }
#else
    (void)period_ms;
#endif
}

void diagnostics_mark_fault(const char *module, const char *reason)
{
    s_fault_latched = true;
    LOG_ERROR(module, "%s", reason);
}

bool diagnostics_has_fault(void)
{
    return s_fault_latched;
}
