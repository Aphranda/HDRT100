#include "portable_log.h"

#include <stdio.h>

typedef struct {
    portable_log_level_t level;
    const char *name;
} portable_log_level_meta_t;

static const portable_log_level_meta_t s_level_table[PORTABLE_LOG_LEVEL_COUNT] = {
    [PORTABLE_LOG_LEVEL_DEBUG] = {PORTABLE_LOG_LEVEL_DEBUG, "DBG"},
    [PORTABLE_LOG_LEVEL_INFO] = {PORTABLE_LOG_LEVEL_INFO, "INF"},
    [PORTABLE_LOG_LEVEL_WARN] = {PORTABLE_LOG_LEVEL_WARN, "WRN"},
    [PORTABLE_LOG_LEVEL_ERROR] = {PORTABLE_LOG_LEVEL_ERROR, "ERR"},
};

static bool portable_log_valid_level(portable_log_level_t level)
{
    return (uint32_t)level < (uint32_t)PORTABLE_LOG_LEVEL_COUNT;
}

const char *portable_log_level_name(portable_log_level_t level)
{
    if (!portable_log_valid_level(level) || s_level_table[level].level != level) {
        return "UNK";
    }

    return s_level_table[level].name;
}

bool portable_log_init(portable_log_t *log, const portable_log_config_t *config)
{
    if (log == NULL || config == NULL ||
        config->emit == NULL ||
        config->line_buffer == NULL ||
        config->line_buffer_size < 32u ||
        !portable_log_valid_level(config->min_level)) {
        return false;
    }

    log->config = *config;
    log->status.min_level = config->min_level;
    for (uint32_t i = 0u; i < (uint32_t)PORTABLE_LOG_LEVEL_COUNT; i++) {
        log->status.emitted_count[i] = 0u;
        log->status.dropped_count[i] = 0u;
    }
    log->initialized = true;
    return true;
}

bool portable_log_set_min_level(portable_log_t *log, portable_log_level_t level)
{
    if (log == NULL || !log->initialized || !portable_log_valid_level(level)) {
        return false;
    }

    log->status.min_level = level;
    log->config.min_level = level;
    return true;
}

portable_log_level_t portable_log_get_min_level(const portable_log_t *log)
{
    if (log == NULL || !log->initialized) {
        return PORTABLE_LOG_LEVEL_INFO;
    }

    return log->status.min_level;
}

void portable_log_get_status(const portable_log_t *log, portable_log_status_t *status)
{
    if (log == NULL || status == NULL) {
        return;
    }

    *status = log->status;
}

void portable_log_vwrite(portable_log_t *log,
                         portable_log_level_t level,
                         const char *module,
                         const char *fmt,
                         va_list args)
{
    if (log == NULL || !log->initialized || !portable_log_valid_level(level)) {
        return;
    }

    if (level < log->status.min_level) {
        log->status.dropped_count[level]++;
        return;
    }

    char *buffer = log->config.line_buffer;
    const size_t buffer_size = log->config.line_buffer_size;
    const uint32_t timestamp_ms = log->config.time_ms != NULL
                                      ? log->config.time_ms(log->config.user)
                                      : 0u;
    const char *safe_module = module != NULL ? module : "-";
    const char *safe_fmt = fmt != NULL ? fmt : "";

    int written = snprintf(buffer,
                           buffer_size,
                           "[%10lu] %-3s %-8s ",
                           (unsigned long)timestamp_ms,
                           portable_log_level_name(level),
                           safe_module);
    size_t used = 0u;
    if (written > 0) {
        used = (size_t)written;
        if (used >= buffer_size) {
            used = buffer_size - 1u;
        }
    }

    if (used < buffer_size) {
        written = vsnprintf(buffer + used, buffer_size - used, safe_fmt, args);
        if (written > 0) {
            used += (size_t)written;
            if (used >= buffer_size) {
                used = buffer_size - 1u;
            }
        }
    }

    if (buffer_size >= 3u) {
        if (used + 2u >= buffer_size) {
            used = buffer_size - 3u;
        }
        buffer[used++] = '\r';
        buffer[used++] = '\n';
        buffer[used] = '\0';
    }

    log->status.emitted_count[level]++;
    log->config.emit(log->config.user, buffer, used);
}

void portable_log_write(portable_log_t *log,
                        portable_log_level_t level,
                        const char *module,
                        const char *fmt,
                        ...)
{
    va_list args;
    va_start(args, fmt);
    portable_log_vwrite(log, level, module, fmt, args);
    va_end(args);
}
