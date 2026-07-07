#include "portable_log.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char text[512];
    size_t length;
    uint32_t now_ms;
} capture_sink_t;

static uint32_t test_time_ms(void *user)
{
    capture_sink_t *sink = (capture_sink_t *)user;
    return sink->now_ms;
}

static void test_emit(void *user, const char *text, size_t length)
{
    capture_sink_t *sink = (capture_sink_t *)user;
    if (sink == NULL || text == NULL) {
        return;
    }

    const size_t remaining = sizeof(sink->text) - sink->length - 1u;
    const size_t copy_len = length < remaining ? length : remaining;
    memcpy(sink->text + sink->length, text, copy_len);
    sink->length += copy_len;
    sink->text[sink->length] = '\0';
}

static int expect_true(const char *name, bool value)
{
    if (!value) {
        printf("%s: expected true\n", name);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        printf("%s: expected %lu, got %lu\n",
               name,
               (unsigned long)expected,
               (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_text(const char *name, const char *actual, const char *expected)
{
    if (strcmp(actual, expected) != 0) {
        printf("%s: expected <%s>, got <%s>\n", name, expected, actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;
    capture_sink_t sink = {
        .now_ms = 1234u,
    };
    char line_buffer[96];
    portable_log_t log;

    const portable_log_config_t config = {
        .time_ms = test_time_ms,
        .emit = test_emit,
        .user = &sink,
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .min_level = PORTABLE_LOG_LEVEL_INFO,
    };

    failed += expect_true("init", portable_log_init(&log, &config));

    portable_log_write(&log, PORTABLE_LOG_LEVEL_DEBUG, "core", "hidden=%u", 1u);
    portable_log_write(&log, PORTABLE_LOG_LEVEL_INFO, "core", "hello %s", "world");

    failed += expect_text("info line",
                          sink.text,
                          "[      1234] INF core     hello world\r\n");

    portable_log_status_t status;
    portable_log_get_status(&log, &status);
    failed += expect_u32("debug dropped", status.dropped_count[PORTABLE_LOG_LEVEL_DEBUG], 1u);
    failed += expect_u32("info emitted", status.emitted_count[PORTABLE_LOG_LEVEL_INFO], 1u);

    failed += expect_true("set debug", portable_log_set_min_level(&log, PORTABLE_LOG_LEVEL_DEBUG));
    portable_log_write(&log, PORTABLE_LOG_LEVEL_DEBUG, "core", "visible");
    portable_log_get_status(&log, &status);
    failed += expect_u32("debug emitted", status.emitted_count[PORTABLE_LOG_LEVEL_DEBUG], 1u);
    failed += expect_text("level name", portable_log_level_name(PORTABLE_LOG_LEVEL_WARN), "WRN");

    return failed == 0 ? 0 : 1;
}
