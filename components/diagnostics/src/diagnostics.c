#include "diagnostics.h"

#include <stdarg.h>

#include "portable_log_port.h"
#include "pico/stdlib.h"
#include "project_config.h"

static bool s_fault_latched;

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
    portable_log_port_init();
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
