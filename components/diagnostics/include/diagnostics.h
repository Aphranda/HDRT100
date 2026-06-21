#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DIAG_LEVEL_DEBUG = 0,
    DIAG_LEVEL_INFO,
    DIAG_LEVEL_WARN,
    DIAG_LEVEL_ERROR,
} diag_level_t;

void diagnostics_init(void);
void diagnostics_log(diag_level_t level, const char *module, const char *fmt, ...);
void diagnostics_heartbeat(uint32_t period_ms);
void diagnostics_mark_fault(const char *module, const char *reason);
bool diagnostics_has_fault(void);

#define LOG_DEBUG(module, fmt, ...) diagnostics_log(DIAG_LEVEL_DEBUG, module, fmt, ##__VA_ARGS__)
#define LOG_INFO(module, fmt, ...) diagnostics_log(DIAG_LEVEL_INFO, module, fmt, ##__VA_ARGS__)
#define LOG_WARN(module, fmt, ...) diagnostics_log(DIAG_LEVEL_WARN, module, fmt, ##__VA_ARGS__)
#define LOG_ERROR(module, fmt, ...) diagnostics_log(DIAG_LEVEL_ERROR, module, fmt, ##__VA_ARGS__)

#endif
