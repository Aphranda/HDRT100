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
#define TEST_PRODUCT_CONFIG_VERSION_V2 2u
#define TEST_PRODUCT_CONFIG_VERSION 3u
#define TEST_PRODUCT_CONFIG_DPLL_PROFILE_VALID 0x44504C4Cu
#define TEST_PRODUCT_CONFIG_REALTIME_PROFILE_VALID 0x52545046u

/* Deployed version 1/2 record: the 64-byte prefix the old CRC was sealed over. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t usb_mode;
    uint32_t board_no;
    uint32_t reserved[10];
    uint32_t crc32;
} test_product_config_record_t;

/* Current record and its reserved[] word map, mirrored here so the test locks
 * the stored layout instead of trusting the reader it is testing. */
#define TEST_REALTIME_MARKER_WORD 10u
#define TEST_REALTIME_CYCLE_WORD 11u
#define TEST_REALTIME_PHASE_COUNT_WORD 12u
#define TEST_REALTIME_START_WORD 13u
#define TEST_REALTIME_END_WORD 23u
#define TEST_REALTIME_WCET_WORD 33u
#define TEST_RECORD_RESERVED_WORDS 43u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t usb_mode;
    uint32_t board_no;
    uint32_t reserved[TEST_RECORD_RESERVED_WORDS];
    uint32_t crc32;
} test_product_config_record_v3_t;

_Static_assert(sizeof(test_product_config_record_v3_t) <= DRV_FLASH_PAGE_SIZE,
               "the mirrored record must fit one program-page slot");

static uint8_t s_flash[OTA_PRODUCT_CONFIG_SIZE];
static bool s_fail_program;
static bool s_corrupt_program;
static bool s_fail_after_program;
static bool s_wrong_valid_program;
static uint32_t s_program_count;
static uint32_t s_erase_count;
static uint32_t test_v3_record_crc(const test_product_config_record_v3_t *record);

static void test_reset_flash(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(s_flash + FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE, 0xA5,
           OTA_PRODUCT_CONFIG_SIZE -
               FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE);
    s_fail_program = false;
    s_corrupt_program = false;
    s_fail_after_program = false;
    s_wrong_valid_program = false;
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
        if (s_wrong_valid_program) {
            /* A valid CRC/sequence alone is not proof that the requested
             * identity + budgets were saved. Return a different valid record. */
            test_product_config_record_v3_t wrong;
            memcpy(&wrong, s_flash + request->relative_offset, sizeof(wrong));
            wrong.board_no = wrong.board_no == 0u ? 1u : 0u;
            wrong.crc32 = test_v3_record_crc(&wrong);
            memcpy(s_flash + request->relative_offset, &wrong, sizeof(wrong));
        }
        s_program_count++;
        if (s_fail_after_program) {
            /* Completion journaling or release can fail after durable bytes
             * already exist; model that result without undoing the program. */
            if (completion != NULL) {
                memset(completion, 0, sizeof(*completion));
                completion->result = FLASH_TRANSACTION_RESULT_FAILED;
                completion->processed_bytes = request->length;
            }
            return false;
        }
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

static uint32_t test_v3_record_crc(const test_product_config_record_v3_t *record)
{
    test_product_config_record_v3_t copy = *record;
    copy.crc32 = 0u;
    return ota_crc32_compute((const uint8_t *)&copy, sizeof(copy));
}

static void test_read_v3_record(uint32_t slot,
                                test_product_config_record_v3_t *record)
{
    assert(slot * DRV_FLASH_PAGE_SIZE + sizeof(*record) <=
           FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE);
    memcpy(record, s_flash + slot * DRV_FLASH_PAGE_SIZE, sizeof(*record));
}

static void test_seed_versioned_record(uint32_t version,
                                       product_config_usb_mode_t usb_mode,
                                       uint32_t board_no, uint32_t sequence,
                                       const uint32_t *reserved,
                                       uint32_t reserved_words)
{
    test_product_config_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = TEST_PRODUCT_CONFIG_MAGIC;
    record.version = version;
    record.sequence = sequence;
    record.usb_mode = (uint32_t)usb_mode;
    record.board_no = board_no;
    assert(reserved_words <= 10u);
    if (reserved != NULL && reserved_words != 0u) {
        memcpy(record.reserved, reserved,
               reserved_words * sizeof(uint32_t));
    }
    record.crc32 = test_record_crc(&record);
    memcpy(s_flash, &record, sizeof(record));
}

static void test_seed_legacy_record(product_config_usb_mode_t usb_mode,
                                    uint32_t board_no)
{
    test_seed_versioned_record(TEST_PRODUCT_CONFIG_VERSION_LEGACY, usb_mode,
                               board_no, 41u, NULL, 0u);
}

/* Seals a current-layout record straight into the first journal slot, so the
 * reader is exercised on record shapes a writer would never produce. */
static void test_seed_v3_record(const product_config_realtime_profile_t *table,
                                uint32_t marker, uint32_t cycle_cycles,
                                uint32_t phase_count)
{
    test_product_config_record_v3_t record;
    memset(&record, 0, sizeof(record));
    record.magic = TEST_PRODUCT_CONFIG_MAGIC;
    record.version = TEST_PRODUCT_CONFIG_VERSION;
    record.sequence = 3u;
    record.usb_mode = (uint32_t)PRODUCT_CONFIG_USB_MODE_CDC;
    record.board_no = 1u;
    record.reserved[TEST_REALTIME_MARKER_WORD] = marker;
    record.reserved[TEST_REALTIME_CYCLE_WORD] = cycle_cycles;
    record.reserved[TEST_REALTIME_PHASE_COUNT_WORD] = phase_count;
    if (table != NULL) {
        for (uint32_t phase = 0u;
             phase < PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES; ++phase) {
            record.reserved[TEST_REALTIME_START_WORD + phase] =
                table->start_cycle[phase];
            record.reserved[TEST_REALTIME_END_WORD + phase] =
                table->end_cycle[phase];
            record.reserved[TEST_REALTIME_WCET_WORD + phase] =
                table->wcet_cycles[phase];
        }
    }
    record.crc32 = test_v3_record_crc(&record);
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

static void test_dpll_role_profile_defaults_and_survives_restart(void)
{
    test_reset_flash();
    assert(product_config_init());

    product_config_dpll_control_profile_t role;
    assert(product_config_get_dpll_control_profile(&role));
    assert(role.mode == 0u);
    assert(role.follow_master_slot_id == 0u);

    const product_config_dpll_control_profile_t saved = {
        .mode = 1u,
        .follow_master_slot_id = 0u,
        .generation = 9u,
    };
    assert(product_config_set_dpll_control_profile(&saved));
    assert(product_config_init());
    assert(product_config_get_dpll_control_profile(&role));
    assert(role.mode == saved.mode);
    assert(role.follow_master_slot_id == saved.follow_master_slot_id);
}

static product_config_realtime_profile_t test_realtime_profile_fixture(void)
{
    product_config_realtime_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.cycle_cycles = 375000u;
    profile.phase_count = PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES;
    for (uint32_t phase = 0u;
         phase < PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES; ++phase) {
        profile.start_cycle[phase] = 1000u + 3000u * phase;
        profile.end_cycle[phase] = 3999u + 3000u * phase;
        profile.wcet_cycles[phase] = 500u + 17u * phase;
    }
    return profile;
}

static void test_realtime_profile_equals(
    const product_config_realtime_profile_t *left,
    const product_config_realtime_profile_t *right)
{
    assert(left != NULL);
    assert(right != NULL);
    assert(left->cycle_cycles == right->cycle_cycles);
    assert(left->phase_count == right->phase_count);
    for (uint32_t phase = 0u;
         phase < PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES; ++phase) {
        assert(left->start_cycle[phase] == right->start_cycle[phase]);
        assert(left->end_cycle[phase] == right->end_cycle[phase]);
        assert(left->wcet_cycles[phase] == right->wcet_cycles[phase]);
    }
}

static void test_realtime_profile_is_absent_until_stored(void)
{
    test_reset_flash();
    assert(product_config_init());

    /* No stored table: the caller keeps its compiled default, and the refused
     * read leaves its output untouched. */
    product_config_realtime_profile_t absent;
    memset(&absent, 0xA5, sizeof(absent));
    assert(!product_config_get_realtime_profile(&absent));
    assert(absent.cycle_cycles == 0xA5A5A5A5u);
    assert(absent.phase_count == 0xA5A5A5A5u);
    assert(absent.start_cycle[0] == 0xA5A5A5A5u);
    assert(absent.wcet_cycles[PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES - 1u] ==
           0xA5A5A5A5u);
    assert(!product_config_get_realtime_profile(NULL));
    assert(s_program_count == 0u);
}

static void test_realtime_profile_round_trips_through_flash(void)
{
    test_reset_flash();
    assert(product_config_init());

    const product_config_realtime_profile_t saved =
        test_realtime_profile_fixture();
    assert(product_config_set_realtime_profile(&saved));

    product_config_realtime_profile_t restored;
    assert(product_config_get_realtime_profile(&restored));
    test_realtime_profile_equals(&restored, &saved);

    /* The identity and DPLL words of the record are untouched by the table. */
    product_config_usb_mode_t usb_mode = PRODUCT_CONFIG_USB_MODE_USBTMC;
    assert(product_config_get_usb_mode(&usb_mode));
    assert(usb_mode == PRODUCT_CONFIG_USB_MODE_CDC);
    product_config_dpll_servo_profile_t dpll;
    assert(product_config_get_dpll_servo_profile(&dpll));
    test_profile_equals(&dpll, PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16,
                        PRODUCT_CONFIG_DPLL_DEFAULT_KI_Q16,
                        PRODUCT_CONFIG_DPLL_DEFAULT_UPDATE_PERIOD_US,
                        PRODUCT_CONFIG_DPLL_DEFAULT_STEP_THRESHOLD_NS,
                        PRODUCT_CONFIG_DPLL_DEFAULT_SANITY_FREQ_LIMIT_PPB);

    assert(product_config_init());
    assert(product_config_get_realtime_profile(&restored));
    test_realtime_profile_equals(&restored, &saved);
}

static void test_realtime_profile_stored_layout_and_clear(void)
{
    test_reset_flash();
    assert(product_config_init());

    const product_config_realtime_profile_t saved =
        test_realtime_profile_fixture();
    assert(product_config_set_realtime_profile(&saved));

    /* The first store lands in the first erased slot of the journal. */
    test_product_config_record_v3_t stored;
    test_read_v3_record(0u, &stored);
    assert(stored.magic == TEST_PRODUCT_CONFIG_MAGIC);
    assert(stored.version == TEST_PRODUCT_CONFIG_VERSION);
    assert(stored.reserved[TEST_REALTIME_MARKER_WORD] ==
           TEST_PRODUCT_CONFIG_REALTIME_PROFILE_VALID);
    assert(stored.reserved[TEST_REALTIME_CYCLE_WORD] == saved.cycle_cycles);
    assert(stored.reserved[TEST_REALTIME_PHASE_COUNT_WORD] == saved.phase_count);
    for (uint32_t phase = 0u;
         phase < PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES; ++phase) {
        assert(stored.reserved[TEST_REALTIME_START_WORD + phase] ==
               saved.start_cycle[phase]);
        assert(stored.reserved[TEST_REALTIME_END_WORD + phase] ==
               saved.end_cycle[phase]);
        assert(stored.reserved[TEST_REALTIME_WCET_WORD + phase] ==
               saved.wcet_cycles[phase]);
    }
    assert(test_v3_record_crc(&stored) == stored.crc32);

    assert(product_config_clear_realtime_profile());

    test_product_config_record_v3_t cleared;
    test_read_v3_record(1u, &cleared);
    assert(cleared.version == TEST_PRODUCT_CONFIG_VERSION);
    assert(cleared.sequence == stored.sequence + 1u);
    assert(cleared.reserved[TEST_REALTIME_MARKER_WORD] == 0u);
    /* Clear keeps every window word: only the marker decides "no table". */
    for (uint32_t phase = 0u;
         phase < PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES; ++phase) {
        assert(cleared.reserved[TEST_REALTIME_START_WORD + phase] ==
               saved.start_cycle[phase]);
        assert(cleared.reserved[TEST_REALTIME_END_WORD + phase] ==
               saved.end_cycle[phase]);
        assert(cleared.reserved[TEST_REALTIME_WCET_WORD + phase] ==
               saved.wcet_cycles[phase]);
    }
    assert(test_v3_record_crc(&cleared) == cleared.crc32);

    product_config_realtime_profile_t refused = saved;
    assert(!product_config_get_realtime_profile(&refused));
    test_realtime_profile_equals(&refused, &saved);
    assert(product_config_init());
    assert(!product_config_get_realtime_profile(&refused));

    /* Clearing an already-absent table still bumps and stores the record. */
    assert(product_config_clear_realtime_profile());
    assert(!product_config_get_realtime_profile(&refused));
}

static void test_realtime_profile_rejects_incomplete_or_empty_table(void)
{
    test_reset_flash();
    assert(product_config_init());

    assert(!product_config_set_realtime_profile(NULL));

    product_config_realtime_profile_t partial =
        test_realtime_profile_fixture();
    partial.phase_count = PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES - 1u;
    assert(!product_config_set_realtime_profile(&partial));

    product_config_realtime_profile_t empty = test_realtime_profile_fixture();
    empty.cycle_cycles = 0u;
    assert(!product_config_set_realtime_profile(&empty));

    product_config_realtime_profile_t zero_count =
        test_realtime_profile_fixture();
    zero_count.phase_count = 0u;
    assert(!product_config_set_realtime_profile(&zero_count));

    /* A refused table never reaches the journal. */
    assert(s_program_count == 0u);
    assert(s_erase_count == 0u);

    const product_config_realtime_profile_t saved =
        test_realtime_profile_fixture();
    assert(product_config_set_realtime_profile(&saved));
    product_config_realtime_profile_t restored;
    assert(product_config_get_realtime_profile(&restored));
    test_realtime_profile_equals(&restored, &saved);
}

static void test_realtime_profile_never_judges_phase_arithmetic(void)
{
    test_reset_flash();
    assert(product_config_init());

    /* Deliberately impossible windows: the record stores the whole table and
     * leaves admission to the caller, so nothing here may be re-scaled or
     * rejected by the store. */
    product_config_realtime_profile_t absurd =
        test_realtime_profile_fixture();
    absurd.start_cycle[0] = 999999u;
    absurd.end_cycle[0] = 1u;
    absurd.start_cycle[1] = 0u;
    absurd.end_cycle[1] = UINT32_MAX;
    absurd.wcet_cycles[0] = UINT32_MAX;
    assert(product_config_set_realtime_profile(&absurd));

    product_config_realtime_profile_t restored;
    assert(product_config_get_realtime_profile(&restored));
    test_realtime_profile_equals(&restored, &absurd);
}

static void test_realtime_record_guards_reject_malformed_tables(void)
{
    const product_config_realtime_profile_t table =
        test_realtime_profile_fixture();
    product_config_realtime_profile_t refused;

    for (uint32_t variant = 0u; variant < 3u; ++variant) {
        test_reset_flash();
        uint32_t marker = TEST_PRODUCT_CONFIG_REALTIME_PROFILE_VALID;
        uint32_t cycle_cycles = table.cycle_cycles;
        uint32_t phase_count = table.phase_count;
        if (variant == 0u) {
            marker = 0u; /* no table stored */
        } else if (variant == 1u) {
            phase_count = PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES - 1u;
        } else {
            cycle_cycles = 0u;
        }
        test_seed_v3_record(&table, marker, cycle_cycles, phase_count);

        /* CRC-valid record: it anchors the journal without any startup write. */
        assert(product_config_init());
        assert(s_program_count == 0u);
        assert(s_erase_count == 0u);
        assert(product_config_get_board_no() == 1u);

        memset(&refused, 0x7E, sizeof(refused));
        assert(!product_config_get_realtime_profile(&refused));
        assert(refused.cycle_cycles == 0x7E7E7E7Eu);
        assert(refused.phase_count == 0x7E7E7E7Eu);
        assert(refused.start_cycle[0] == 0x7E7E7E7Eu);
    }

    /* Control: the same seam does read back a well-formed seeded table. */
    test_reset_flash();
    test_seed_v3_record(&table, TEST_PRODUCT_CONFIG_REALTIME_PROFILE_VALID,
                        table.cycle_cycles, table.phase_count);
    assert(product_config_init());
    assert(product_config_get_realtime_profile(&refused));
    test_realtime_profile_equals(&refused, &table);
}

static void test_corrupt_realtime_record_is_not_accepted(void)
{
    test_reset_flash();
    assert(product_config_init());
    const product_config_realtime_profile_t saved =
        test_realtime_profile_fixture();
    assert(product_config_set_realtime_profile(&saved));

    /* The CRC covers the whole current record, including the table. */
    assert(sizeof(test_product_config_record_v3_t) <
           FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE);
    s_flash[sizeof(test_product_config_record_v3_t) - 1u] ^= 1u;
    assert(product_config_init());

    product_config_realtime_profile_t refused;
    memset(&refused, 0x5A, sizeof(refused));
    assert(!product_config_get_realtime_profile(&refused));
    assert(refused.cycle_cycles == 0x5A5A5A5Au);
    assert(product_config_get_board_no() == 0u);
    assert(s_program_count == 1u);
}

static void test_v2_record_is_accepted_and_keeps_realtime_absent(void)
{
    test_reset_flash();
    const uint32_t reserved[6] = {
        TEST_PRODUCT_CONFIG_DPLL_PROFILE_VALID, (uint32_t)-17, 911u,
        777u, 12345u, 23456u,
    };
    test_seed_versioned_record(TEST_PRODUCT_CONFIG_VERSION_V2,
                               PRODUCT_CONFIG_USB_MODE_USBTMC, 5u, 77u,
                               reserved, 6u);
    assert(product_config_init());

    /* Startup stays read-only and the deployed slot keeps its own CRC domain. */
    assert(s_program_count == 0u);
    assert(s_erase_count == 0u);
    test_product_config_record_t prefix;
    memcpy(&prefix, s_flash, sizeof(prefix));
    assert(prefix.version == TEST_PRODUCT_CONFIG_VERSION_V2);
    assert(prefix.crc32 == test_record_crc(&prefix));

    product_config_usb_mode_t usb_mode = PRODUCT_CONFIG_USB_MODE_CDC;
    assert(product_config_get_usb_mode(&usb_mode));
    assert(usb_mode == PRODUCT_CONFIG_USB_MODE_USBTMC);
    assert(product_config_get_board_no() == 5u);

    /* The v2 DPLL fields are usable unchanged; the table was never stored. */
    product_config_dpll_servo_profile_t dpll;
    assert(product_config_get_dpll_servo_profile(&dpll));
    test_profile_equals(&dpll, -17, 911, 777u, 12345u, 23456u);

    product_config_realtime_profile_t table;
    memset(&table, 0x33, sizeof(table));
    assert(!product_config_get_realtime_profile(&table));
    assert(table.cycle_cycles == 0x33333333u);

    /* An explicit store writes the promoted record in the current layout. */
    assert(product_config_set_board_no(6u));
    test_product_config_record_v3_t stored;
    test_read_v3_record(1u, &stored);
    assert(stored.magic == TEST_PRODUCT_CONFIG_MAGIC);
    assert(stored.version == TEST_PRODUCT_CONFIG_VERSION);
    assert(stored.sequence == 78u);
    assert(stored.board_no == 6u);
    assert(stored.reserved[0] == TEST_PRODUCT_CONFIG_DPLL_PROFILE_VALID);
    assert(stored.reserved[1] == (uint32_t)-17);
    assert(stored.reserved[TEST_REALTIME_MARKER_WORD] == 0u);
    assert(test_v3_record_crc(&stored) == stored.crc32);

    /* The v2 slot is left alone, so a downgrade still reads its own record. */
    memcpy(&prefix, s_flash, sizeof(prefix));
    assert(prefix.version == TEST_PRODUCT_CONFIG_VERSION_V2);
    assert(prefix.crc32 == test_record_crc(&prefix));
}

static void test_realtime_profile_accepts_durable_write_after_completion_error(void)
{
    test_reset_flash();
    assert(product_config_init());
    product_config_realtime_profile_t saved = test_realtime_profile_fixture();
    assert(product_config_set_realtime_profile(&saved));
    saved.cycle_cycles = 500000u;
    saved.wcet_cycles[1] += 37u;
    s_fail_after_program = true;
    assert(product_config_set_realtime_profile(&saved));
    assert(s_program_count == 2u);
    product_config_realtime_profile_t actual;
    assert(product_config_get_realtime_profile(&actual));
    test_realtime_profile_equals(&actual, &saved);
    assert(product_config_init());
    assert(product_config_get_realtime_profile(&actual));
    test_realtime_profile_equals(&actual, &saved);

    /* CLEAR has the same durable commit point as APPLY. */
    assert(product_config_clear_realtime_profile());
    assert(!product_config_get_realtime_profile(&actual));
    assert(product_config_init());
    assert(!product_config_get_realtime_profile(&actual));
    assert(s_program_count == 3u);
}

static void test_realtime_profile_failed_or_corrupt_write_keeps_last_record(void)
{
    for (unsigned mode = 0u; mode < 3u; ++mode) {
        test_reset_flash();
        assert(product_config_init());
        const product_config_realtime_profile_t saved = test_realtime_profile_fixture();
        assert(product_config_set_realtime_profile(&saved));
        product_config_realtime_profile_t requested = saved;
        requested.cycle_cycles += 1000u;
        s_fail_program = mode == 0u;
        s_corrupt_program = mode != 0u;
        s_fail_after_program = mode == 2u;
        assert(!product_config_set_realtime_profile(&requested));
        product_config_realtime_profile_t actual;
        assert(product_config_get_realtime_profile(&actual));
        test_realtime_profile_equals(&actual, &saved);
        assert(product_config_init());
        assert(product_config_get_realtime_profile(&actual));
        test_realtime_profile_equals(&actual, &saved);
    }
}

static void test_realtime_profile_requires_full_record_readback_match(void)
{
    test_reset_flash();
    assert(product_config_init());
    const product_config_realtime_profile_t saved = test_realtime_profile_fixture();
    assert(product_config_set_realtime_profile(&saved));
    const uint8_t board_no = product_config_get_board_no();
    product_config_realtime_profile_t requested = saved;
    requested.cycle_cycles += 1000u;
    s_wrong_valid_program = true;
    assert(!product_config_set_realtime_profile(&requested));
    assert(product_config_get_board_no() == board_no);
    product_config_realtime_profile_t actual;
    assert(product_config_get_realtime_profile(&actual));
    test_realtime_profile_equals(&actual, &saved);
}

int main(void)
{
    test_blank_flash_seeds_conservative_profile_without_write();
    test_legacy_record_migrates_in_ram_without_losing_identity();
    test_corrupt_record_seeds_profile_without_write();
    test_flash_write_fault_does_not_block_read_only_initialization();
    test_explicit_profile_survives_other_product_updates();
    test_dpll_role_profile_defaults_and_survives_restart();
    test_realtime_profile_is_absent_until_stored();
    test_realtime_profile_round_trips_through_flash();
    test_realtime_profile_stored_layout_and_clear();
    test_realtime_profile_rejects_incomplete_or_empty_table();
    test_realtime_profile_never_judges_phase_arithmetic();
    test_realtime_record_guards_reject_malformed_tables();
    test_corrupt_realtime_record_is_not_accepted();
    test_v2_record_is_accepted_and_keeps_realtime_absent();
    test_realtime_profile_accepts_durable_write_after_completion_error();
    test_realtime_profile_failed_or_corrupt_write_keeps_last_record();
    test_realtime_profile_requires_full_record_readback_match();
    puts("product config host unit tests passed");
    return 0;
}
