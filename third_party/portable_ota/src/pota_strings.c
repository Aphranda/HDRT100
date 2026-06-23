#include "pota_strings.h"

const char *pota_state_to_string(pota_state_t state)
{
    switch (state) {
    case POTA_STATE_IDLE:
        return "IDLE";
    case POTA_STATE_CHECK_PERMISSION:
        return "CHECK_PERMISSION";
    case POTA_STATE_ERASE_SLOT:
        return "ERASE_SLOT";
    case POTA_STATE_RECEIVING:
        return "RECEIVING";
    case POTA_STATE_VERIFYING:
        return "VERIFYING";
    case POTA_STATE_MARK_PENDING:
        return "MARK_PENDING";
    case POTA_STATE_READY_TO_REBOOT:
        return "READY_TO_REBOOT";
    case POTA_STATE_PENDING_CONFIRM:
        return "PENDING_CONFIRM";
    case POTA_STATE_COMMITTED:
        return "COMMITTED";
    case POTA_STATE_FAILED:
        return "FAILED";
    case POTA_STATE_ABORTED:
        return "ABORTED";
    default:
        return "UNKNOWN";
    }
}

const char *pota_error_to_string(pota_error_t error)
{
    switch (error) {
    case POTA_ERR_NONE:
        return "NONE";
    case POTA_ERR_BUSY:
        return "BUSY";
    case POTA_ERR_INVALID_STATE:
        return "INVALID_STATE";
    case POTA_ERR_IMAGE_TOO_LARGE:
        return "IMAGE_TOO_LARGE";
    case POTA_ERR_BAD_HEADER:
        return "BAD_HEADER";
    case POTA_ERR_PRODUCT_MISMATCH:
        return "PRODUCT_MISMATCH";
    case POTA_ERR_HARDWARE_MISMATCH:
        return "HARDWARE_MISMATCH";
    case POTA_ERR_BOOTLOADER_TOO_OLD:
        return "BOOTLOADER_TOO_OLD";
    case POTA_ERR_FLASH_ERASE:
        return "FLASH_ERASE";
    case POTA_ERR_FLASH_PROGRAM:
        return "FLASH_PROGRAM";
    case POTA_ERR_READBACK:
        return "READBACK";
    case POTA_ERR_CRC:
        return "CRC";
    case POTA_ERR_VECTOR:
        return "VECTOR";
    case POTA_ERR_METADATA:
        return "METADATA";
    case POTA_ERR_ABORTED:
        return "ABORTED";
    case POTA_ERR_BAD_ARGUMENT:
        return "BAD_ARGUMENT";
    default:
        return "UNKNOWN";
    }
}

const char *pota_result_to_string(pota_result_t result)
{
    switch (result) {
    case POTA_RESULT_NONE:
        return "NONE";
    case POTA_RESULT_ACCEPTED:
        return "ACCEPTED";
    case POTA_RESULT_IMAGE_STAGED:
        return "IMAGE_STAGED";
    case POTA_RESULT_COMMITTED:
        return "COMMITTED";
    case POTA_RESULT_FAILED:
        return "FAILED";
    case POTA_RESULT_ABORTED:
        return "ABORTED";
    default:
        return "UNKNOWN";
    }
}

const char *pota_boot_result_to_string(pota_boot_result_t result)
{
    switch (result) {
    case POTA_BOOT_RESULT_NONE:
        return "NONE";
    case POTA_BOOT_RESULT_APPLIED:
        return "APPLIED";
    case POTA_BOOT_RESULT_NO_PENDING:
        return "NO_PENDING";
    case POTA_BOOT_RESULT_MAX_ATTEMPTS:
        return "MAX_ATTEMPTS";
    case POTA_BOOT_RESULT_STAGE_VALIDATE_FAILED:
        return "STAGE_VALIDATE_FAILED";
    case POTA_BOOT_RESULT_COPY_FAILED:
        return "COPY_FAILED";
    case POTA_BOOT_RESULT_ACTIVE_VALIDATE_FAILED:
        return "ACTIVE_VALIDATE_FAILED";
    default:
        return "UNKNOWN";
    }
}
