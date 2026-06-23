#include "portable_ota_port.h"

#include "ota_error.h"
#include "pota.h"

static pota_error_t portable_strings_map_error(uint32_t error_code)
{
    switch ((ota_error_t)error_code) {
    case OTA_ERR_NONE:
        return POTA_ERR_NONE;
    case OTA_ERR_BUSY:
        return POTA_ERR_BUSY;
    case OTA_ERR_INVALID_STATE:
        return POTA_ERR_INVALID_STATE;
    case OTA_ERR_IMAGE_TOO_LARGE:
        return POTA_ERR_IMAGE_TOO_LARGE;
    case OTA_ERR_BAD_HEADER:
        return POTA_ERR_BAD_HEADER;
    case OTA_ERR_FLASH_ERASE:
        return POTA_ERR_FLASH_ERASE;
    case OTA_ERR_FLASH_PROGRAM:
        return POTA_ERR_FLASH_PROGRAM;
    case OTA_ERR_READBACK:
        return POTA_ERR_READBACK;
    case OTA_ERR_CRC:
        return POTA_ERR_CRC;
    case OTA_ERR_VECTOR:
        return POTA_ERR_VECTOR;
    case OTA_ERR_METADATA:
        return POTA_ERR_METADATA;
    case OTA_ERR_ABORTED:
        return POTA_ERR_ABORTED;
    case OTA_ERR_BAD_ARGUMENT:
        return POTA_ERR_BAD_ARGUMENT;
    default:
        return (pota_error_t)UINT32_MAX;
    }
}

static pota_result_t portable_strings_map_result(ota_result_t result)
{
    switch (result) {
    case OTA_RESULT_NONE:
        return POTA_RESULT_NONE;
    case OTA_RESULT_ACCEPTED:
        return POTA_RESULT_ACCEPTED;
    case OTA_RESULT_IMAGE_STAGED:
        return POTA_RESULT_IMAGE_STAGED;
    case OTA_RESULT_COMMITTED:
        return POTA_RESULT_COMMITTED;
    case OTA_RESULT_FAILED:
        return POTA_RESULT_FAILED;
    case OTA_RESULT_ABORTED:
        return POTA_RESULT_ABORTED;
    default:
        return (pota_result_t)UINT32_MAX;
    }
}

const char *portable_ota_port_state_to_string(ota_state_t state)
{
    return pota_state_to_string((pota_state_t)state);
}

const char *portable_ota_port_error_to_string(uint32_t error_code)
{
    switch ((ota_error_t)error_code) {
    case OTA_ERR_BOARD_MISMATCH:
        return "BOARD_MISMATCH";
    case OTA_ERR_VERSION_REJECTED:
        return "VERSION_REJECTED";
    case OTA_ERR_BOOT_ROLLBACK:
        return "BOOT_ROLLBACK";
    case OTA_ERR_QUEUE_FULL:
        return "QUEUE_FULL";
    default:
        return pota_error_to_string(portable_strings_map_error(error_code));
    }
}

const char *portable_ota_port_result_to_string(ota_result_t result)
{
    return pota_result_to_string(portable_strings_map_result(result));
}

const char *portable_ota_port_boot_result_to_string(uint32_t result)
{
    return pota_boot_result_to_string((pota_boot_result_t)result);
}
