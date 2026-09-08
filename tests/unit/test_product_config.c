#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drv_flash.h"
#include "flash_transaction.h"
#include "ota_partition.h"
#include "product_config.h"

#define TEST_PRODUCT_CONFIG_MAGIC 0x47544346u
#define TEST_PRODUCT_CONFIG_VERSION_LEGACY 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t usb_mode;
    uint32_t board_no;
    uint32_t reserved[10];
    uint32_t crc32;
} test_product_config_record_t;

static uint8_t s_flash[OTA_PRODUCT_CONFIG_SIZE];
static bool s_fail_program;
static bool s_corrupt_program;
static uint32_t s_program_count;
static uint32_t s_erase_count;

static void test_reset_flash(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(s_flash + FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE, 0xA5,
           OTA_PRODUCT_CONFIG_SIZE -
               FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE);
    s_fail_program = false;
    s_corrupt_program = false;
    s_program_count = 0u;
    s_erase_count = 0u;
}

static bool test_product_offset(uint32_t offset, size_t length,
                                uint32_t *relative_offset)
{
    if (offset < OTA_PRODUCT_CONFIG_OFFSET) {
        return false;
    }
    const uint32_t relative = offset - OTA_PRODUCT_CONFIG_OFFSET;
    if (relative > OTA_PRODUCT_CONFIG_SIZE ||
        length > OTA_PRODUCT_CONFIG_SIZE - relative) {
        return false;
    }
    if (relative_offset != NULL) {
        *relative_offset = relative;
    }
    return true;
}

bool drv_flash_read(uint32_t offset, void *data, size_t length)
{
    uint32_t relative = 0u;
    if (data == NULL || !test_product_offset(offset, length, &relative)) {
        return false;
    }
    memcpy(data, s_flash + relative, length);
    return true;
}

bool drv_flash_is_erased(uint32_t offset, size_t length)
{
    uint32_t relative = 0u;
    if (!test_product_offset(offset, length, &relative)) {
        return false;
    }
    for (size_t index = 0u; index < length; ++index) {
        if (s_flash[relative + index] != 0xFFu) {
            return false;
        }
    }
    return true;
}

const uint8_t *drv_flash_xip_ptr(uint32_t offset)
{
    uint32_t relative = 0u;
    return test_product_offset(offset, 0u, &relative) ? s_flash + relative
                                                       : NULL;
}

const flash_transaction_completion_lease_t *
flash_transaction_ao_get_completion_lease(void)
{
    return NULL;
}

bool flash_transaction_ao_execute(const flash_transaction_request_t *request,
                                  flash_transaction_completion_t *completion)
{
    if (request == NULL ||
        request->requester != FLASH_TRANSACTION_REQUESTER_PRODUCT_CONFIG ||
        request->partition_id != FLASH_DEPLOYMENT_MAP_PRODUCT_NVS_ID ||
        request->relative_offset > FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE ||
        request->length > FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE -
                              request->relative_offset) {
        return false;
    }

    if (request->operation == FLASH_TRANSACTION_OPERATION_ERASE) {
        memset(s_flash + request->relative_offset, 0xFF, request->length);
        s_erase_count++;
    } else if (request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM) {
        if (s_fail_program || request->data == NULL) {
            return false;
        }
        for (uint32_t index = 0u; index < request->length; ++index) {
            s_flash[request->relative_offset + index] &= request->data[index];
        }
        if (s_corrupt_program) {
            s_flash[request->relative_offset] ^= 1u;
        }
        s_program_count++;
    } else {
        return false;
    }

    if (completion != NULL) {
        memset(completion, 0, sizeof(*completion));
        completion->level = FLASH_TRANSACTION_COMPLETION_COMMITTED;
        completion->result = FLASH_TRANSACTION_RESULT_COMMITTED;
        completion->processed_bytes = request->length;
        completion->verified_bytes = request->length;
    }
    return true;
}

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    crc = ~crc;
    for (size_t index = 0u; index < length; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint32_t ota_crc32_compute(const uint8_t *data, size_t length)
{
    return ota_crc32_update(0u, data, length);
}

static uint32_t test_record_crc(const test_product_config_record_t *record)
{
    test_product_config_record_t copy = *record;
    copy.crc32 = 0u;
    return ota_crc32_compute((const uint8_t *)&copy, sizeof(copy));
}

static void test_seed_legacy_record(product_config_usb_mode_t usb_mode,
                                    uint32_t board_no)
{
    test_product_config_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = TEST_PRODUCT_CONFIG_MAGIC;
    record.version = TEST_PRODUCT_CONFIG_VERSION_LEGACY;
    record.sequence = 41u;
    record.usb_mode = (uint32_t)usb_mode;
    record.board_no = board_no;
    record.crc32 = test_record_crc(&record);
    memcpy(s_flash, &record, sizeof(record));
}

static void test_profile_equals(
    const product_config_dpll_servo_profile_t *profile,
    int32_t kp_q16, int32_t ki_q16, uint32_t update_period_us,
    uint32_t step_threshold_ns, uint32_t sanity_freq_limit_ppb)
{
    assert(profile != NULL);
    assert(profile->kp_q16 == kp_q16);
    assert(profile->ki_q16 == ki_q16);
    assert(profile->update_period_us == update_period_us);
    assert(profile->step_threshold_ns == step_threshold_ns);
    assert(profile->sanity_freq_limit_ppb == sanity_freq_limit_ppb);
}

static void test_blank_flash_seeds_conservative_profile_without_write(void)
{
    test_reset_flash();
    assert(product_config_init());

    product_config_dpll_servo_profile_t profile;
    assert(product_config_get_dpll_servo_profile(&profile));
    test_profile_equals(&profile, PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16,
                        PRODUCT_CONFIG_DPLL_DEFAULT_KI_Q16,
                        PRODUCT_CONFIG_DPLL_DEFAULT_UPDATE_PERIOD_US,
                        PRODUCT_CONFIG_DPLL_DEFAULT_STEP_THRESHOLD_NS,
                        PRODUCT_CONFIG_DPLL_DEFAULT_SANITY_FREQ_LIMIT_PPB);
    assert(s_program_count == 0u);
    assert(s_erase_count == 0u);
    for (uint32_t index = FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE;
         index < OTA_PRODUCT_CONFIG_SIZE; ++index) {
        assert(s_flash[index] == 0xA5u);
    }
}

static void test_legacy_record_migrates_in_ram_without_losing_identity(void)
{
    test_reset_flash();
    test_seed_legacy_record(PRODUCT_CONFIG_USB_MODE_USBTMC, 3u);
    assert(product_config_init());

    product_config_usb_mode_t usb_mode = PRODUCT_CONFIG_USB_MODE_CDC;
    assert(product_config_get_usb_mode(&usb_mode));
    assert(usb_mode == PRODUCT_CONFIG_USB_MODE_USBTMC);
    assert(product_config_get_board_no() == 3u);

    product_config_dpll_servo_profile_t profile;
    assert(product_config_get_dpll_servo_profile(&profile));
    test_profile_equals(&profile, PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16,
                        PRODUCT_CONFIG_DPLL_DEFAULT_KI_Q16,
                        PRODUCT_CONFIG_DPLL_DEFAULT_UPDATE_PERIOD_US,
                        PRODUCT_CONFIG_DPLL_DEFAULT_STEP_THRESHOLD_NS,
                        PRODUCT_CONFIG_DPLL_DEFAULT_SANITY_FREQ_LIMIT_PPB);
    assert(s_program_count == 0u);
}

static void test_corrupt_record_seeds_profile_without_write(void)
{
    test_reset_flash();
    test_seed_legacy_record(PRODUCT_CONFIG_USB_MODE_USBTMC, 3u);
    s_flash[sizeof(test_product_config_record_t) - 1u] ^= 1u;
    assert(product_config_init());

    product_config_dpll_servo_profile_t profile;
    assert(product_config_get_dpll_servo_profile(&profile));
    test_profile_equals(&profile, PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16,
                        PRODUCT_CONFIG_DPLL_DEFAULT_KI_Q16,
                        PRODUCT_CONFIG_DPLL_DEFAULT_UPDATE_PERIOD_US,
                        PRODUCT_CONFIG_DPLL_DEFAULT_STEP_THRESHOLD_NS,
                        PRODUCT_CONFIG_DPLL_DEFAULT_SANITY_FREQ_LIMIT_PPB);
    assert(product_config_get_board_no() == 0u);
    assert(s_program_count == 0u);
}

static void test_flash_write_fault_does_not_block_read_only_initialization(void)
{
    test_reset_flash();
    s_fail_program = true;
    assert(product_config_init());
    assert(s_program_count == 0u);

    test_reset_flash();
    s_corrupt_program = true;
    assert(product_config_init());
    assert(s_program_count == 0u);
}

static void test_explicit_profile_survives_other_product_updates(void)
{
    test_reset_flash();
    assert(product_config_init());

    const product_config_dpll_servo_profile_t saved = {
        .kp_q16 = -17,
        .ki_q16 = 911,
        .update_period_us = 777u,
        .step_threshold_ns = 12345u,
        .sanity_freq_limit_ppb = 23456u,
    };
    assert(product_config_set_dpll_servo_profile(&saved));
    assert(product_config_set_usb_mode(PRODUCT_CONFIG_USB_MODE_USBTMC));
    assert(product_config_set_board_no(4u));
    assert(product_config_init());

    product_config_dpll_servo_profile_t restored;
    assert(product_config_get_dpll_servo_profile(&restored));
    test_profile_equals(&restored, saved.kp_q16, saved.ki_q16,
                        saved.update_period_us, saved.step_threshold_ns,
                        saved.sanity_freq_limit_ppb);
    assert(product_config_get_board_no() == 4u);
}

int main(void)
{
    test_blank_flash_seeds_conservative_profile_without_write();
    test_legacy_record_migrates_in_ram_without_losing_identity();
    test_corrupt_record_seeds_profile_without_write();
    test_flash_write_fault_does_not_block_read_only_initialization();
    test_explicit_profile_survives_other_product_updates();
    puts("product config host unit tests passed");
    return 0;
}
