#include "pota_strings.h"
#include "pota_compat.h"

#include <stdio.h>
#include <string.h>

static int expect_text(const char *name, const char *actual, const char *expected)
{
    if (strcmp(actual, expected) != 0) {
        printf("%s: expected %s, got %s\n", name, expected, actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;

    failed += expect_text("state idle", pota_state_to_string(POTA_STATE_IDLE), "IDLE");
    failed += expect_text("state ready", pota_state_to_string(POTA_STATE_READY_TO_REBOOT), "READY_TO_REBOOT");
    failed += expect_text("state unknown", pota_state_to_string((pota_state_t)99u), "UNKNOWN");

    failed += expect_text("error none", pota_error_to_string(POTA_ERR_NONE), "NONE");
    failed += expect_text("error crc", pota_error_to_string(POTA_ERR_CRC), "CRC");
    failed += expect_text("error bootloader", pota_error_to_string(POTA_ERR_BOOTLOADER_TOO_OLD), "BOOTLOADER_TOO_OLD");
    failed += expect_text("error unknown", pota_error_to_string((pota_error_t)99u), "UNKNOWN");

    failed += expect_text("result staged", pota_result_to_string(POTA_RESULT_IMAGE_STAGED), "IMAGE_STAGED");
    failed += expect_text("result aborted", pota_result_to_string(POTA_RESULT_ABORTED), "ABORTED");
    failed += expect_text("result unknown", pota_result_to_string((pota_result_t)99u), "UNKNOWN");

    failed += expect_text("boot applied", pota_boot_result_to_string(POTA_BOOT_RESULT_APPLIED), "APPLIED");
    failed += expect_text("boot copy failed", pota_boot_result_to_string(POTA_BOOT_RESULT_COPY_FAILED), "COPY_FAILED");
    failed += expect_text("boot unknown", pota_boot_result_to_string((pota_boot_result_t)99u), "UNKNOWN");

    static const pota_compat_map_entry_t error_aliases[] = {
        {POTA_ERR_PRODUCT_MISMATCH, 100u},
        {POTA_ERR_HARDWARE_MISMATCH, 100u},
    };
    static const pota_compat_text_entry_t texts[] = {
        {100u, "BOARD_MISMATCH"},
    };

    if (pota_compat_error_to_product(POTA_ERR_PRODUCT_MISMATCH,
                                     error_aliases,
                                     2u,
                                     999u) != 100u) {
        printf("compat product mismatch alias failed\n");
        failed++;
    }
    if (pota_compat_error_to_product(POTA_ERR_CRC, NULL, 0u, 999u) != (uint32_t)POTA_ERR_CRC) {
        printf("compat default crc mapping failed\n");
        failed++;
    }
    failed += expect_text("compat text alias",
                          pota_compat_text_u32(100u, texts, 1u, "UNKNOWN"),
                          "BOARD_MISMATCH");

    return failed == 0 ? 0 : 1;
}
