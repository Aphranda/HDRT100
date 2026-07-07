#ifndef PORTABLE_LOG_H
#define PORTABLE_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PORTABLE_LOG_LEVEL_DEBUG = 0,
    PORTABLE_LOG_LEVEL_INFO,
    PORTABLE_LOG_LEVEL_WARN,
    PORTABLE_LOG_LEVEL_ERROR,
    PORTABLE_LOG_LEVEL_COUNT,
} portable_log_level_t;

typedef uint32_t (*portable_log_time_ms_fn)(void *user);
typedef bool (*portable_log_emit_fn)(void *user, const char *text, size_t length);
typedef void (*portable_log_lock_fn)(void *user);

typedef struct {
    portable_log_level_t min_level;
    uint32_t emitted_count[PORTABLE_LOG_LEVEL_COUNT];
    uint32_t dropped_count[PORTABLE_LOG_LEVEL_COUNT];
    uint32_t truncated_count[PORTABLE_LOG_LEVEL_COUNT];
    uint32_t emit_failed_count[PORTABLE_LOG_LEVEL_COUNT];
} portable_log_status_t;

typedef struct {
    portable_log_time_ms_fn time_ms;
    portable_log_emit_fn emit;
    portable_log_lock_fn lock;
    portable_log_lock_fn unlock;
    void *user;
    char *line_buffer;
    size_t line_buffer_size;
    portable_log_level_t min_level;
} portable_log_config_t;

typedef struct {
    portable_log_config_t config;
    portable_log_status_t status;
    bool initialized;
} portable_log_t;

bool portable_log_init(portable_log_t *log, const portable_log_config_t *config);
bool portable_log_set_min_level(portable_log_t *log, portable_log_level_t level);
portable_log_level_t portable_log_get_min_level(const portable_log_t *log);
void portable_log_get_status(const portable_log_t *log, portable_log_status_t *status);
void portable_log_write(portable_log_t *log,
                        portable_log_level_t level,
                        const char *module,
                        const char *fmt,
                        ...);
void portable_log_vwrite(portable_log_t *log,
                         portable_log_level_t level,
                         const char *module,
                         const char *fmt,
                         va_list args);
const char *portable_log_level_name(portable_log_level_t level);

#endif
