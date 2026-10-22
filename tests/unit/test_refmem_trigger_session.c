#include "refmem_trigger_session.h"

#include <stdio.h>
#include <string.h>

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static refmem_trigger_session_config_t make_config(void)
{
    refmem_trigger_session_config_t c;
    (void)memset(&c, 0, sizeof(c));
    c.session_id = 7u;
    c.run_id = 9u;
    c.source_slot = 0u;
    c.target_mask = 0x07u;
    c.required_mask = 0x05u;
    return c;
}

static int test_happy_path(void)
{
    refmem_trigger_session_t s;
    refmem_trigger_session_config_t c = make_config();
    int rc = 0;

    refmem_trigger_session_init(&s);
    rc |= expect_u32("configure",
                     (uint32_t)refmem_trigger_session_configure(&s, &c),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("arm_begin",
                     (uint32_t)refmem_trigger_session_arm_begin(&s),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("source",
                     (uint32_t)refmem_trigger_session_arm_source(&s, true),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("arm 0",
                     (uint32_t)refmem_trigger_session_arm_report(&s, 0u, true),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("arm 2",
                     (uint32_t)refmem_trigger_session_arm_report(&s, 2u, true),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("state armed", s.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_ARMED);
    rc |= expect_u32("start",
                     (uint32_t)refmem_trigger_session_start(&s),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("state running", s.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_RUNNING);
    rc |= expect_u32("stop begin",
                     (uint32_t)refmem_trigger_session_stop_begin(&s),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("stop complete",
                     (uint32_t)refmem_trigger_session_stop_complete(&s),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("state idle", s.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_IDLE);
    return rc;
}

static int test_arm_failure_disarms(void)
{
    refmem_trigger_session_t s;
    refmem_trigger_session_config_t c = make_config();
    int rc = 0;

    refmem_trigger_session_init(&s);
    (void)refmem_trigger_session_configure(&s, &c);
    (void)refmem_trigger_session_arm_begin(&s);
    (void)refmem_trigger_session_arm_source(&s, true);
    (void)refmem_trigger_session_arm_report(&s, 0u, true);
    rc |= expect_u32("arm fail",
                     (uint32_t)refmem_trigger_session_arm_report(&s, 2u, false),
                     (uint32_t)REFMEM_TRIGGER_SESSION_TARGET_FAILED);
    rc |= expect_u32("state configured", s.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_CONFIGURED);
    rc |= expect_u32("armed cleared", s.armed_mask, 0u);
    rc |= expect_u32("failed set", s.failed_mask, 0x04u);
    rc |= expect_u32("start rejected",
                     (uint32_t)refmem_trigger_session_start(&s),
                     (uint32_t)REFMEM_TRIGGER_SESSION_INVALID_STATE);
    return rc;
}

static int test_source_not_ready(void)
{
    refmem_trigger_session_t s;
    refmem_trigger_session_config_t c = make_config();
    int rc = 0;

    refmem_trigger_session_init(&s);
    (void)refmem_trigger_session_configure(&s, &c);
    (void)refmem_trigger_session_arm_begin(&s);
    rc |= expect_u32("source fail",
                     (uint32_t)refmem_trigger_session_arm_source(&s, false),
                     (uint32_t)REFMEM_TRIGGER_SESSION_SOURCE_NOT_READY);
    rc |= expect_u32("state configured", s.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_CONFIGURED);
    return rc;
}

static int test_run_failure_and_stop_timeout(void)
{
    refmem_trigger_session_t s;
    refmem_trigger_session_config_t c = make_config();
    int rc = 0;

    refmem_trigger_session_init(&s);
    (void)refmem_trigger_session_mount(&s, &c);
    rc |= expect_u32("mount running", s.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_RUNNING);
    rc |= expect_u32("target failed",
                     (uint32_t)refmem_trigger_session_target_failed(&s, 1u),
                     (uint32_t)REFMEM_TRIGGER_SESSION_TARGET_FAILED);
    rc |= expect_u32("degraded", s.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_DEGRADED);
    rc |= expect_u32("stop from degraded",
                     (uint32_t)refmem_trigger_session_stop_begin(&s),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    return rc;
}

static int test_stop_timeout(void)
{
    refmem_trigger_session_t s;
    refmem_trigger_session_config_t c = make_config();
    int rc = 0;

    refmem_trigger_session_init(&s);
    (void)refmem_trigger_session_mount(&s, &c);
    (void)refmem_trigger_session_stop_begin(&s);
    rc |= expect_u32("stop timeout",
                     (uint32_t)refmem_trigger_session_stop_timeout(&s),
                     (uint32_t)REFMEM_TRIGGER_SESSION_STOP_TIMEOUT);
    rc |= expect_u32("degraded after timeout", s.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_DEGRADED);
    return rc;
}

static int test_abort_idempotent(void)
{
    refmem_trigger_session_t s;
    refmem_trigger_session_config_t c = make_config();
    int rc = 0;

    refmem_trigger_session_init(&s);
    (void)refmem_trigger_session_mount(&s, &c);
    rc |= expect_u32("abort",
                     (uint32_t)refmem_trigger_session_abort(&s),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("abort again",
                     (uint32_t)refmem_trigger_session_abort(&s),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("aborted", s.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_ABORTED);
    return rc;
}

int main(void)
{
    int rc = 0;
    rc |= test_happy_path();
    rc |= test_arm_failure_disarms();
    rc |= test_source_not_ready();
    rc |= test_run_failure_and_stop_timeout();
    rc |= test_stop_timeout();
    rc |= test_abort_idempotent();
    if (rc == 0) {
        (void)printf("refmem_trigger_session tests passed\n");
    }
    return rc;
}
