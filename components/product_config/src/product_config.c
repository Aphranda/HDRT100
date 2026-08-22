#include "product_config.h"

#include <ctype.h>
#include <string.h>

#include "drv_flash.h"
#include "flash_transaction.h"
#include "ota_crc32.h"
#include "ota_partition.h"
#include "project_config.h"

#define PRODUCT_CONFIG_MAGIC   0x47544346u
#define PRODUCT_CONFIG_VERSION 1u
#define PRODUCT_CONFIG_MAX_BOARD_NO 8u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t usb_mode;
    uint32_t board_no;
    uint32_t reserved[10];
    uint32_t crc32;
} product_config_record_t;

static product_config_record_t s_product_config;
static uint32_t s_product_config_provider_generation;
static uint32_t s_product_config_provider_refs;

static uint32_t product_config_next_provider_generation(void)
{
    s_product_config_provider_generation++;
    if (s_product_config_provider_generation == 0u) {
        s_product_config_provider_generation = 1u;
    }
    return s_product_config_provider_generation;
}

static bool product_config_provider_retain(void *context)
{
    uint32_t *refs = context;
    if (refs == NULL || *refs == UINT32_MAX) {
        return false;
    }
    (*refs)++;
    return true;
}

static void product_config_provider_release(void *context)
{
    uint32_t *refs = context;
    if (refs != NULL && *refs != 0u) {
        (*refs)--;
    }
}

static bool product_config_flash_execute(uint32_t operation,
                                         const uint8_t *data,
                                         uint32_t length,
                                         uint32_t store_generation)
{
    const uint32_t provider_generation =
        operation == FLASH_TRANSACTION_OPERATION_PROGRAM
            ? product_config_next_provider_generation()
            : 0u;
    const flash_transaction_buffer_lease_t lease = {
        .data = data,
        .length = length,
        .generation = provider_generation,
        .context = &s_product_config_provider_refs,
        .retain = product_config_provider_retain,
        .release = product_config_provider_release,
    };
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_PRODUCT_CONFIG,
        .partition_id = FLASH_COMPAT_MAP_PRODUCT_NVS_ID,
        .operation = operation,
        .relative_offset = 0u,
        .length = length,
        .data = data,
        .provider_generation = provider_generation,
        .store_generation = store_generation,
        .buffer_lease = operation == FLASH_TRANSACTION_OPERATION_PROGRAM
                            ? &lease
                            : NULL,
        .completion_lease = flash_transaction_ao_get_completion_lease(),
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
}

static product_config_usb_mode_t product_config_default_usb_mode(void)
{
#if PROJECT_USB_DEFAULT_MODE_USBTMC
    return PRODUCT_CONFIG_USB_MODE_USBTMC;
#else
    return PRODUCT_CONFIG_USB_MODE_CDC;
#endif
}

static bool product_config_usb_mode_is_valid(uint32_t mode)
{
    return mode == (uint32_t)PRODUCT_CONFIG_USB_MODE_CDC ||
           mode == (uint32_t)PRODUCT_CONFIG_USB_MODE_USBTMC;
}

static bool product_config_board_no_is_valid(uint32_t board_no)
{
    /* Zero is the backward-compatible value for "not assigned yet". */
    return board_no <= PRODUCT_CONFIG_MAX_BOARD_NO;
}

static uint32_t product_config_crc32(const product_config_record_t *record)
{
    product_config_record_t copy = *record;
    copy.crc32 = 0u;
    return ota_crc32_compute((const uint8_t *)&copy, sizeof(copy));
}

static bool product_config_record_is_valid(const product_config_record_t *record)
{
    if (record == NULL ||
        record->magic != PRODUCT_CONFIG_MAGIC ||
        record->version != PRODUCT_CONFIG_VERSION ||
        !product_config_usb_mode_is_valid(record->usb_mode) ||
        !product_config_board_no_is_valid(record->board_no)) {
        return false;
    }

    return product_config_crc32(record) == record->crc32;
}

static void product_config_set_default(product_config_record_t *record)
{
    memset(record, 0, sizeof(*record));
    record->magic = PRODUCT_CONFIG_MAGIC;
    record->version = PRODUCT_CONFIG_VERSION;
    record->sequence = 0u;
    record->usb_mode = (uint32_t)product_config_default_usb_mode();
    record->board_no = 0u;
    record->crc32 = product_config_crc32(record);
}

static bool product_config_store(const product_config_record_t *record)
{
    uint8_t page[DRV_FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, record, sizeof(*record));

    if (!product_config_flash_execute(FLASH_TRANSACTION_OPERATION_ERASE,
                                      NULL, DRV_FLASH_SECTOR_SIZE,
                                      record->sequence) ||
        !product_config_flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM,
                                      page, sizeof(page),
                                      record->sequence)) {
        return false;
    }

    product_config_record_t readback;
    if (!drv_flash_read(OTA_PRODUCT_CONFIG_OFFSET, &readback, sizeof(readback)) ||
        !product_config_record_is_valid(&readback) ||
        readback.sequence != record->sequence) {
        return false;
    }

    s_product_config = readback;
    return true;
}

bool product_config_init(void)
{
    product_config_record_t record;
    if (drv_flash_read(OTA_PRODUCT_CONFIG_OFFSET, &record, sizeof(record)) &&
        product_config_record_is_valid(&record)) {
        s_product_config = record;
        return true;
    }

    product_config_set_default(&s_product_config);
    return true;
}

bool product_config_get_usb_mode(product_config_usb_mode_t *mode)
{
    if (mode == NULL) {
        return false;
    }

    if (!product_config_usb_mode_is_valid(s_product_config.usb_mode)) {
        product_config_set_default(&s_product_config);
    }

    *mode = (product_config_usb_mode_t)s_product_config.usb_mode;
    return true;
}

bool product_config_set_usb_mode(product_config_usb_mode_t mode)
{
    if (!product_config_usb_mode_is_valid((uint32_t)mode)) {
        return false;
    }
    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) {
        product_config_set_default(&record);
    }

    record.magic = PRODUCT_CONFIG_MAGIC;
    record.version = PRODUCT_CONFIG_VERSION;
    record.usb_mode = (uint32_t)mode;
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    return product_config_store(&record) &&
           s_product_config.usb_mode == (uint32_t)mode;
}

uint8_t product_config_get_board_no(void)
{
    if (!product_config_record_is_valid(&s_product_config)) {
        product_config_set_default(&s_product_config);
    }
    return (uint8_t)s_product_config.board_no;
}

bool product_config_set_board_no(uint32_t board_no)
{
    if (!product_config_board_no_is_valid(board_no)) {
        return false;
    }

    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) {
        product_config_set_default(&record);
    }
    if (record.board_no == board_no) {
        return true;
    }

    record.board_no = board_no;
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    return product_config_store(&record) &&
           s_product_config.board_no == board_no;
}

const char *product_config_usb_mode_to_string(product_config_usb_mode_t mode)
{
    switch (mode) {
    case PRODUCT_CONFIG_USB_MODE_CDC:
        return "CDC";
    case PRODUCT_CONFIG_USB_MODE_USBTMC:
        return "USBTMC";
    default:
        return "UNKNOWN";
    }
}

static bool product_config_text_equals(const char *text, uint32_t length, const char *expected)
{
    const size_t expected_len = strlen(expected);
    if (text == NULL || length != expected_len) {
        return false;
    }

    for (uint32_t i = 0u; i < length; i++) {
        if (toupper((unsigned char)text[i]) != (unsigned char)expected[i]) {
            return false;
        }
    }

    return true;
}

bool product_config_usb_mode_from_text(const char *text, uint32_t length, product_config_usb_mode_t *mode)
{
    if (mode == NULL) {
        return false;
    }

    if (product_config_text_equals(text, length, "CDC")) {
        *mode = PRODUCT_CONFIG_USB_MODE_CDC;
        return true;
    }

    if (product_config_text_equals(text, length, "USBTMC") ||
        product_config_text_equals(text, length, "TMC")) {
        *mode = PRODUCT_CONFIG_USB_MODE_USBTMC;
        return true;
    }

    return false;
}
