#include "pota.h"

#include <stdio.h>
#include <string.h>

static int expect_true(const char *name, bool value)
{
    if (!value) {
        (void)printf("%s: expected true\n", name);
        return 1;
    }
    return 0;
}

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

int main(void)
{
    const pota_platform_t platform = {
        .info = {
            .boot_mode = POTA_BOOT_MODE_COPY_TO_ACTIVE,
            .active_slot = POTA_SLOT_A,
            .slot_a = {
                .offset = 0x00040000u,
                .size = 0x00180000u,
                .run_offset = 0x00040000u,
            },
            .slot_b = {
                .offset = 0x001C0000u,
                .size = 0x00180000u,
                .run_offset = 0x001C0000u,
            },
        },
    };

    pota_session_t session;
    memset(&session, 0xA5, sizeof(session));

    int failed = 0;
    failed += expect_true("session init", pota_session_init(&session, &platform));

    pota_status_t status;
    memset(&status, 0, sizeof(status));
    pota_session_get_status(&session, &status);
    failed += expect_u32("initial state", status.state, (uint32_t)POTA_STATE_IDLE);
    failed += expect_u32("initial target", status.target_slot, (uint32_t)POTA_SLOT_B);
    failed += expect_u32("null init", pota_session_init(NULL, &platform), false);
    failed += expect_u32("null begin",
                         (uint32_t)pota_session_begin(NULL, NULL),
                         (uint32_t)POTA_ERR_BAD_ARGUMENT);

    return failed == 0 ? 0 : 1;
}
