#include "portable_log.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char text[512];
    size_t length;
    uint32_t now_ms;
    bool fail_emit;
    uint32_t lock_count;
    uint32_t unlock_count;
} capture_sink_t;

static uint32_t test_time_ms(void *user)
{
    capture_sink_t *sink = (capture_sink_t *)user;
    return sink->now_ms;
}

static bool test_emit(void *user, const char *text, size_t length)
{
    capture_sink_t *sink = (capture_sink_t *)user;
    if (sink == NULL || text == NULL) {
        return false;
    }
    if (sink->fail_emit) {
        return false;
    }

    const size_t remaining = sizeof(sink->text) - sink->length - 1u;
    const size_t copy_len = length < remaining ? length : remaining;
    memcpy(sink->text + sink->length, text, copy_len);
    sink->length += copy_len;
    sink->text[sink->length] = '\0';
    return true;
}

static void test_lock(void *user)
{
    capture_sink_t *sink = (capture_sink_t *)user;
    if (sink != NULL) {
        sink->lock_count++;
    }
}

static void test_unlock(void *user)
{
    capture_sink_t *sink = (capture_sink_t *)user;
    if (sink != NULL) {
        sink->unlock_count++;
    }
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
        .lock = test_lock,
        .unlock = test_unlock,
        .user = &sink,
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .min_level = PORTABLE_LOG_LEVEL_INFO,
    };

    failed += expect_true("init", portable_log_init(&log, &config));
    failed += expect_true("init rejects null log", !portable_log_init(NULL, &config));
    failed += expect_true("init rejects null config", !portable_log_init(&log, NULL));

    portable_log_config_t bad_config = config;
    bad_config.emit = NULL;
    failed += expect_true("init rejects null emit", !portable_log_init(&log, &bad_config));
    bad_config = config;
    bad_config.unlock = NULL;
    failed += expect_true("init rejects unmatched lock", !portable_log_init(&log, &bad_config));
    bad_config = config;
    bad_config.line_buffer_size = 31u;
    failed += expect_true("init rejects short buffer", !portable_log_init(&log, &bad_config));

    portable_log_write(&log, PORTABLE_LOG_LEVEL_DEBUG, "core", "hidden=%u", 1u);
    portable_log_write(&log, PORTABLE_LOG_LEVEL_INFO, "core", "hello %s", "world");

    failed += expect_text("info line",
                          sink.text,
                          "[      1234] INF core     hello world\r\n");

    portable_log_status_t status;
    portable_log_get_status(&log, &status);
    failed += expect_u32("debug dropped", status.dropped_count[PORTABLE_LOG_LEVEL_DEBUG], 1u);
    failed += expect_u32("info emitted", status.emitted_count[PORTABLE_LOG_LEVEL_INFO], 1u);
    failed += expect_u32("info truncated none", status.truncated_count[PORTABLE_LOG_LEVEL_INFO], 0u);
    failed += expect_u32("info emit failed none", status.emit_failed_count[PORTABLE_LOG_LEVEL_INFO], 0u);

    failed += expect_true("set debug", portable_log_set_min_level(&log, PORTABLE_LOG_LEVEL_DEBUG));
    portable_log_write(&log, PORTABLE_LOG_LEVEL_DEBUG, "core", "visible");
    portable_log_get_status(&log, &status);
    failed += expect_u32("debug emitted", status.emitted_count[PORTABLE_LOG_LEVEL_DEBUG], 1u);
    failed += expect_text("level name", portable_log_level_name(PORTABLE_LOG_LEVEL_WARN), "WRN");
    failed += expect_text("bad level name",
                          portable_log_level_name((portable_log_level_t)99u),
                          "UNK");

    char short_buffer[40];
    capture_sink_t short_sink = {
        .now_ms = 5u,
    };
    portable_log_t short_log;
    portable_log_config_t short_config = {
        .time_ms = NULL,
        .emit = test_emit,
        .lock = test_lock,
        .unlock = test_unlock,
        .user = &short_sink,
        .line_buffer = short_buffer,
        .line_buffer_size = sizeof(short_buffer),
        .min_level = PORTABLE_LOG_LEVEL_DEBUG,
    };
    failed += expect_true("short init", portable_log_init(&short_log, &short_config));
    portable_log_write(&short_log, PORTABLE_LOG_LEVEL_ERROR, NULL, "012345678901234567890123456789");
    portable_log_get_status(&short_log, &status);
    failed += expect_u32("short error emitted", status.emitted_count[PORTABLE_LOG_LEVEL_ERROR], 1u);
    failed += expect_u32("short error truncated", status.truncated_count[PORTABLE_LOG_LEVEL_ERROR], 1u);

    capture_sink_t fail_sink = {
        .now_ms = 7u,
        .fail_emit = true,
    };
    portable_log_t fail_log;
    char fail_buffer[96];
    portable_log_config_t fail_config = {
        .time_ms = test_time_ms,
        .emit = test_emit,
        .lock = test_lock,
        .unlock = test_unlock,
        .user = &fail_sink,
        .line_buffer = fail_buffer,
        .line_buffer_size = sizeof(fail_buffer),
        .min_level = PORTABLE_LOG_LEVEL_DEBUG,
    };
    failed += expect_true("fail init", portable_log_init(&fail_log, &fail_config));
    portable_log_write(&fail_log, PORTABLE_LOG_LEVEL_WARN, "core", "not accepted");
    portable_log_get_status(&fail_log, &status);
    failed += expect_u32("warn emitted failed", status.emitted_count[PORTABLE_LOG_LEVEL_WARN], 0u);
    failed += expect_u32("warn emit failed", status.emit_failed_count[PORTABLE_LOG_LEVEL_WARN], 1u);
    failed += expect_u32("lock unlock paired", fail_sink.lock_count, fail_sink.unlock_count);

    return failed == 0 ? 0 : 1;
}
