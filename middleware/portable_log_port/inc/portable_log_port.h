#ifndef PORTABLE_LOG_PORT_H
#define PORTABLE_LOG_PORT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PORTABLE_LOG_PORT_LEVEL_DEBUG = 0,
    PORTABLE_LOG_PORT_LEVEL_INFO,
    PORTABLE_LOG_PORT_LEVEL_WARN,
    PORTABLE_LOG_PORT_LEVEL_ERROR,
    PORTABLE_LOG_PORT_LEVEL_COUNT,
} portable_log_port_level_t;

typedef struct {
    portable_log_port_level_t min_level;
    uint32_t emitted_count[PORTABLE_LOG_PORT_LEVEL_COUNT];
    uint32_t dropped_count[PORTABLE_LOG_PORT_LEVEL_COUNT];
    uint32_t truncated_count[PORTABLE_LOG_PORT_LEVEL_COUNT];
    uint32_t emit_failed_count[PORTABLE_LOG_PORT_LEVEL_COUNT];
    uint32_t queue_dropped_count;
    uint32_t queue_bytes;
    uint32_t queue_high_watermark;
} portable_log_port_status_t;

void portable_log_port_init(void);
void portable_log_port_service(uint32_t max_bytes);
void portable_log_port_write(portable_log_port_level_t level,
                             const char *module,
                             const char *fmt,
                             ...);
void portable_log_port_vwrite(portable_log_port_level_t level,
                              const char *module,
                              const char *fmt,
                              va_list args);
bool portable_log_port_set_min_level(portable_log_port_level_t level);
portable_log_port_level_t portable_log_port_get_min_level(void);
void portable_log_port_get_status(portable_log_port_status_t *status);

#endif
