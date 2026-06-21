#include "diagnostics.h"

#include <stdarg.h>
#include <stdio.h>

#include "pico/stdlib.h"

static bool s_fault_latched;

static const char *level_name(diag_level_t level)
{
    switch (level) {
    case DIAG_LEVEL_DEBUG:
        return "DBG";
    case DIAG_LEVEL_INFO:
        return "INF";
    case DIAG_LEVEL_WARN:
        return "WRN";
    case DIAG_LEVEL_ERROR:
        return "ERR";
    default:
        return "UNK";
    }
}

void diagnostics_init(void)
{
    s_fault_latched = false;
}

void diagnostics_log(diag_level_t level, const char *module, const char *fmt, ...)
{
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    printf("[%10lu] %-3s %-8s ", (unsigned long)now_ms, level_name(level), module);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\r\n");
}

void diagnostics_heartbeat(uint32_t period_ms)
{
    static uint32_t last_ms;
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    if ((uint32_t)(now_ms - last_ms) >= period_ms) {
        last_ms = now_ms;
        LOG_INFO("health", "alive fault=%u", s_fault_latched ? 1u : 0u);
    }
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
