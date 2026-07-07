#include "portable_log_port.h"

#include "osal.h"
#include "pico/stdlib.h"
#include "portable_log.h"

enum {
    PORTABLE_LOG_PORT_LINE_BUFFER_SIZE = 192,
    PORTABLE_LOG_PORT_QUEUE_SIZE = 2048,
    PORTABLE_LOG_PORT_SERVICE_BYTES_DEFAULT = 256,
};

static bool s_log_initialized;
static portable_log_t s_log;
static char s_log_line_buffer[PORTABLE_LOG_PORT_LINE_BUFFER_SIZE];
static char s_log_queue[PORTABLE_LOG_PORT_QUEUE_SIZE];
static uint32_t s_log_queue_head;
static uint32_t s_log_queue_tail;
static uint32_t s_log_queue_count;
static uint32_t s_log_queue_high_watermark;
static uint32_t s_log_queue_dropped_count;

typedef struct {
    portable_log_port_level_t port_level;
    portable_log_level_t core_level;
} portable_log_port_level_map_t;

static const portable_log_port_level_map_t s_level_map[PORTABLE_LOG_PORT_LEVEL_COUNT] = {
    [PORTABLE_LOG_PORT_LEVEL_DEBUG] = {PORTABLE_LOG_PORT_LEVEL_DEBUG, PORTABLE_LOG_LEVEL_DEBUG},
    [PORTABLE_LOG_PORT_LEVEL_INFO] = {PORTABLE_LOG_PORT_LEVEL_INFO, PORTABLE_LOG_LEVEL_INFO},
    [PORTABLE_LOG_PORT_LEVEL_WARN] = {PORTABLE_LOG_PORT_LEVEL_WARN, PORTABLE_LOG_LEVEL_WARN},
    [PORTABLE_LOG_PORT_LEVEL_ERROR] = {PORTABLE_LOG_PORT_LEVEL_ERROR, PORTABLE_LOG_LEVEL_ERROR},
};

static portable_log_level_t port_to_core_level(portable_log_port_level_t level)
{
    if ((uint32_t)level >= (uint32_t)PORTABLE_LOG_PORT_LEVEL_COUNT ||
        s_level_map[level].port_level != level) {
        return PORTABLE_LOG_LEVEL_ERROR;
    }

    return s_level_map[level].core_level;
}

static portable_log_port_level_t port_from_core_level(portable_log_level_t level)
{
    for (uint32_t i = 0u; i < (uint32_t)PORTABLE_LOG_PORT_LEVEL_COUNT; i++) {
        if (s_level_map[i].core_level == level) {
            return s_level_map[i].port_level;
        }
    }

    return PORTABLE_LOG_PORT_LEVEL_INFO;
}

static uint32_t portable_log_port_time_ms(void *user)
{
    (void)user;
    return to_ms_since_boot(get_absolute_time());
}

static uint32_t portable_log_port_queue_space(void)
{
    return (uint32_t)PORTABLE_LOG_PORT_QUEUE_SIZE - s_log_queue_count;
}

static bool portable_log_port_queue_push(const char *text, size_t length)
{
    if (length > (size_t)portable_log_port_queue_space()) {
        s_log_queue_dropped_count++;
        return false;
    }

    for (size_t i = 0u; i < length; i++) {
        s_log_queue[s_log_queue_head] = text[i];
        s_log_queue_head++;
        if (s_log_queue_head >= (uint32_t)PORTABLE_LOG_PORT_QUEUE_SIZE) {
            s_log_queue_head = 0u;
        }
    }

    s_log_queue_count += (uint32_t)length;
    if (s_log_queue_count > s_log_queue_high_watermark) {
        s_log_queue_high_watermark = s_log_queue_count;
    }
    return true;
}

static bool portable_log_port_queue_pop(char *ch)
{
    if (s_log_queue_count == 0u || ch == NULL) {
        return false;
    }

    *ch = s_log_queue[s_log_queue_tail];
    s_log_queue_tail++;
    if (s_log_queue_tail >= (uint32_t)PORTABLE_LOG_PORT_QUEUE_SIZE) {
        s_log_queue_tail = 0u;
    }
    s_log_queue_count--;
    return true;
}

static bool portable_log_port_emit(void *user, const char *text, size_t length)
{
    (void)user;

    if (text == NULL) {
        return false;
    }

    return portable_log_port_queue_push(text, length);
}

static void portable_log_port_lock(void *user)
{
    (void)user;
    osal_critical_enter();
}

static void portable_log_port_unlock(void *user)
{
    (void)user;
    osal_critical_exit();
}

static void portable_log_port_init_once(void)
{
    if (s_log_initialized) {
        return;
    }

    const portable_log_config_t config = {
        .time_ms = portable_log_port_time_ms,
        .emit = portable_log_port_emit,
        .lock = portable_log_port_lock,
        .unlock = portable_log_port_unlock,
        .user = NULL,
        .line_buffer = s_log_line_buffer,
        .line_buffer_size = sizeof(s_log_line_buffer),
        .min_level = PORTABLE_LOG_LEVEL_INFO,
    };

    s_log_initialized = portable_log_init(&s_log, &config);
}

void portable_log_port_init(void)
{
    s_log_initialized = false;
    osal_critical_enter();
    s_log_queue_head = 0u;
    s_log_queue_tail = 0u;
    s_log_queue_count = 0u;
    s_log_queue_high_watermark = 0u;
    s_log_queue_dropped_count = 0u;
    osal_critical_exit();
    portable_log_port_init_once();
}

void portable_log_port_service(uint32_t max_bytes)
{
    portable_log_port_init_once();

    uint32_t budget = max_bytes != 0u ? max_bytes : PORTABLE_LOG_PORT_SERVICE_BYTES_DEFAULT;
    while (budget > 0u) {
        char ch;
        osal_critical_enter();
        const bool has_char = portable_log_port_queue_pop(&ch);
        osal_critical_exit();

        if (!has_char) {
            break;
        }

        putchar_raw(ch);
        budget--;
    }
}

void portable_log_port_vwrite(portable_log_port_level_t level,
                              const char *module,
                              const char *fmt,
                              va_list args)
{
    portable_log_port_init_once();
    portable_log_vwrite(&s_log, port_to_core_level(level), module, fmt, args);
}

void portable_log_port_write(portable_log_port_level_t level,
                             const char *module,
                             const char *fmt,
                             ...)
{
    va_list args;
    va_start(args, fmt);
    portable_log_port_vwrite(level, module, fmt, args);
    va_end(args);
}

bool portable_log_port_set_min_level(portable_log_port_level_t level)
{
    if (level >= PORTABLE_LOG_PORT_LEVEL_COUNT) {
        return false;
    }

    portable_log_port_init_once();
    return portable_log_set_min_level(&s_log, port_to_core_level(level));
}

portable_log_port_level_t portable_log_port_get_min_level(void)
{
    portable_log_port_init_once();
    return port_from_core_level(portable_log_get_min_level(&s_log));
}

void portable_log_port_get_status(portable_log_port_status_t *status)
{
    if (status == NULL) {
        return;
    }

    portable_log_port_init_once();

    portable_log_status_t core_status;
    portable_log_get_status(&s_log, &core_status);

    status->min_level = port_from_core_level(core_status.min_level);
    for (uint32_t i = 0u; i < (uint32_t)PORTABLE_LOG_PORT_LEVEL_COUNT; i++) {
        const portable_log_port_level_t port_level = s_level_map[i].port_level;
        const portable_log_level_t core_level = s_level_map[i].core_level;
        status->emitted_count[port_level] = core_status.emitted_count[core_level];
        status->dropped_count[port_level] = core_status.dropped_count[core_level];
        status->truncated_count[port_level] = core_status.truncated_count[core_level];
        status->emit_failed_count[port_level] = core_status.emit_failed_count[core_level];
    }

    osal_critical_enter();
    status->queue_dropped_count = s_log_queue_dropped_count;
    status->queue_bytes = s_log_queue_count;
    status->queue_high_watermark = s_log_queue_high_watermark;
    osal_critical_exit();
}
