#include "drv_flash_lockout.h"

#include <stdbool.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#endif

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

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static int test_offline_supported_blocks_write(void)
{
    int failed = 0;
    drv_flash_lockout_status_t status;

    drv_flash_lockout_init(true);
    failed += expect_bool("offline begin blocks",
                          drv_flash_lockout_begin(4u),
                          false);
    drv_flash_lockout_get_status(&status);
    failed += expect_u32("offline park offline",
                         status.park_state,
                         DRV_FLASH_LOCKOUT_PARK_OFFLINE);
    failed += expect_u32("offline no request",
                         status.core1_lockout_requested ? 1u : 0u,
                         0u);
    drv_flash_lockout_end(4u);
    return failed;
}

#if defined(_WIN32)
static DWORD WINAPI core1_poll_thread(LPVOID argument)
{
    (void)argument;
    drv_flash_lockout_status_t status;
    do {
        drv_flash_lockout_get_status(&status);
        Sleep(0);
    } while (!status.core1_lockout_requested);

    drv_flash_lockout_core1_poll();
    return 0;
}

static int test_ack_release_handshake(void)
{
    int failed = 0;
    drv_flash_lockout_status_t status;
    HANDLE thread;

    drv_flash_lockout_init(true);
    drv_flash_lockout_core1_poll();
    failed += expect_u32("online after idle poll",
                         (drv_flash_lockout_get_status(&status), status.core1_lockout_online ? 1u : 0u),
                         1u);

    thread = CreateThread(NULL, 0u, core1_poll_thread, NULL, 0u, NULL);
    if (thread == NULL) {
        (void)printf("core1 poll thread create failed\n");
        return failed + 1;
    }

    failed += expect_bool("begin gets ack",
                          drv_flash_lockout_begin(100000u),
                          true);
    drv_flash_lockout_get_status(&status);
    failed += expect_u32("parked state",
                         status.park_state,
                         DRV_FLASH_LOCKOUT_PARK_PARKED);
    failed += expect_u32("ack seq follows request",
                         status.ack_seq,
                         status.request_seq);
    failed += expect_u32("acknowledged",
                         status.core1_lockout_acknowledged ? 1u : 0u,
                         1u);

    drv_flash_lockout_end(100000u);
    (void)WaitForSingleObject(thread, 1000u);
    (void)CloseHandle(thread);
    drv_flash_lockout_get_status(&status);
    failed += expect_u32("release state idle",
                         status.park_state,
                         DRV_FLASH_LOCKOUT_PARK_IDLE);
    failed += expect_u32("released ack",
                         status.core1_lockout_acknowledged ? 1u : 0u,
                         0u);
    failed += expect_u32("release seq",
                         status.release_seq,
                         1u);
    return failed;
}
#endif

static int test_no_ack_fault_blocks_write(void)
{
    int failed = 0;
    drv_flash_lockout_status_t status;

    drv_flash_lockout_init(true);
    drv_flash_lockout_core1_poll();
    drv_flash_lockout_set_fault_injection(DRV_FLASH_LOCKOUT_FAULT_CORE1_NO_ACK);

    failed += expect_bool("no ack blocks begin",
                          drv_flash_lockout_begin(4u),
                          false);
    drv_flash_lockout_get_status(&status);
    failed += expect_u32("no ack state fault",
                         status.park_state,
                         DRV_FLASH_LOCKOUT_PARK_FAULT);
    failed += expect_u32("no ack timeout count",
                         status.timeout_count,
                         1u);
    failed += expect_u32("no ack request cleared",
                         status.core1_lockout_requested ? 1u : 0u,
                         0u);
    failed += expect_u32("no ack last result",
                         status.last_result,
                         DRV_FLASH_LOCKOUT_RESULT_FAULT_INJECTED);

    drv_flash_lockout_clear_fault_injection();
    return failed;
}

int main(void)
{
    int failed = 0;

    failed += test_offline_supported_blocks_write();
#if defined(_WIN32)
    failed += test_ack_release_handshake();
#else
    (void)printf("drv_flash_lockout ack thread test skipped on this host\n");
#endif
    failed += test_no_ack_fault_blocks_write();

    if (failed != 0) {
        (void)printf("drv_flash_lockout tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("drv_flash_lockout tests passed\n");
    return 0;
}
