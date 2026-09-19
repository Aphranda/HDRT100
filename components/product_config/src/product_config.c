#include "product_config.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "drv_flash.h"
#include "flash_transaction.h"
#include "ota_crc32.h"
#include "ota_partition.h"
#include "project_config.h"

#define PRODUCT_CONFIG_MAGIC   0x47544346u
#define PRODUCT_CONFIG_VERSION 7u
#define PRODUCT_CONFIG_VERSION_REFERENCE 6u
#define PRODUCT_CONFIG_VERSION_OUTPUT_TIMING 5u
#define PRODUCT_CONFIG_VERSION_OUTPUT_DELAY 4u
#define PRODUCT_CONFIG_VERSION_BASELINE 3u
#define PRODUCT_CONFIG_VERSION_SERVO 2u
#define PRODUCT_CONFIG_VERSION_LEGACY 1u
#define PRODUCT_CONFIG_MAX_BOARD_NO 8u
#define PRODUCT_CONFIG_SLOT_SIZE DRV_FLASH_PAGE_SIZE
#define PRODUCT_CONFIG_SECTOR_SIZE DRV_FLASH_SECTOR_SIZE
#define PRODUCT_CONFIG_SLOTS_PER_SECTOR \
    (PRODUCT_CONFIG_SECTOR_SIZE / PRODUCT_CONFIG_SLOT_SIZE)
#define PRODUCT_CONFIG_SECTOR_COUNT \
    (FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE / \
     PRODUCT_CONFIG_SECTOR_SIZE)

_Static_assert((FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE %
                PRODUCT_CONFIG_SLOT_SIZE) == 0u,
               "Product Config store must contain whole program-page slots");
_Static_assert((FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE %
                PRODUCT_CONFIG_SECTOR_SIZE) == 0u,
               "Product Config store must contain whole erase sectors");

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t usb_mode;
    uint32_t board_no;
    uint32_t reserved[10];
    uint32_t crc32;
    uint32_t baseline_max_replacements;
    uint32_t baseline_window_ns;
    int32_t output_compensation_ns;
    uint32_t output_plan_ahead_us;
    uint32_t output_commit_ahead_us;
    uint32_t output_refill_low_us;
    uint32_t reference_input_port;
    uint32_t reference_edge;
    uint32_t reference_nominal_hz;
    uint32_t reference_window_ms;
    uint32_t reference_timeout_ms;
    uint32_t reference_discipline_slew_ppb_per_s;
    uint32_t reference_discipline_filter_divisor;
    uint32_t reference_discipline_max_ppb;
} product_config_record_t;

/* Versions 1/2 end at the existing CRC word; retain their byte-exact CRC
 * domain. Version 3 covers baseline fields, version 4 output delay, and
 * version 5 adds output timing; version 6 adds external signal parameters;
 * version 7 adds reference discipline tuning. */
#define PRODUCT_CONFIG_LEGACY_BYTES offsetof(product_config_record_t, baseline_max_replacements)
_Static_assert(PRODUCT_CONFIG_LEGACY_BYTES == 64u, "v1/v2 CRC domain remains 64 bytes");
#define PRODUCT_CONFIG_BASELINE_BYTES offsetof(product_config_record_t, output_compensation_ns)
_Static_assert(PRODUCT_CONFIG_BASELINE_BYTES == 72u, "v3 CRC domain remains 72 bytes");
#define PRODUCT_CONFIG_OUTPUT_DELAY_BYTES offsetof(product_config_record_t, output_plan_ahead_us)
_Static_assert(PRODUCT_CONFIG_OUTPUT_DELAY_BYTES == 76u, "v4 CRC domain remains 76 bytes");
#define PRODUCT_CONFIG_OUTPUT_TIMING_BYTES offsetof(product_config_record_t, reference_input_port)
_Static_assert(PRODUCT_CONFIG_OUTPUT_TIMING_BYTES == 88u, "v5 CRC domain remains 88 bytes");
#define PRODUCT_CONFIG_REFERENCE_BYTES offsetof(product_config_record_t, reference_discipline_slew_ppb_per_s)
_Static_assert(PRODUCT_CONFIG_REFERENCE_BYTES == 108u, "v6 CRC domain remains 108 bytes");
_Static_assert(sizeof(product_config_record_t) == 120u, "v7 record is 120 bytes");
_Static_assert(sizeof(product_config_record_t) <= PRODUCT_CONFIG_SLOT_SIZE,
               "Product Config record must fit one journal page");

#define PRODUCT_CONFIG_DPLL_PROFILE_VALID 0x44504C4Cu /* DPLL */
#define PRODUCT_CONFIG_DPLL_CONTROL_PROFILE_VALID 0x44524F4Cu /* DROL */
#define PRODUCT_CONFIG_DPLL_MAX_SOURCE_SLOT 7u

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
                                         uint32_t relative_offset,
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
        .partition_id = FLASH_DEPLOYMENT_MAP_PRODUCT_NVS_ID,
        .operation = operation,
        .relative_offset = relative_offset,
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
    const size_t length = copy.version == PRODUCT_CONFIG_VERSION
        ? sizeof(copy) : copy.version == PRODUCT_CONFIG_VERSION_REFERENCE
            ? PRODUCT_CONFIG_REFERENCE_BYTES : copy.version == PRODUCT_CONFIG_VERSION_OUTPUT_TIMING
            ? PRODUCT_CONFIG_OUTPUT_TIMING_BYTES : copy.version == PRODUCT_CONFIG_VERSION_OUTPUT_DELAY
            ? PRODUCT_CONFIG_OUTPUT_DELAY_BYTES : copy.version == PRODUCT_CONFIG_VERSION_BASELINE
            ? PRODUCT_CONFIG_BASELINE_BYTES : PRODUCT_CONFIG_LEGACY_BYTES;
    return ota_crc32_compute((const uint8_t *)&copy, length);
}

static bool product_config_record_is_valid(const product_config_record_t *record)
{
    if (record == NULL ||
        record->magic != PRODUCT_CONFIG_MAGIC ||
        (record->version != PRODUCT_CONFIG_VERSION &&
         record->version != PRODUCT_CONFIG_VERSION_REFERENCE &&
         record->version != PRODUCT_CONFIG_VERSION_OUTPUT_TIMING &&
         record->version != PRODUCT_CONFIG_VERSION_OUTPUT_DELAY &&
         record->version != PRODUCT_CONFIG_VERSION_BASELINE &&
         record->version != PRODUCT_CONFIG_VERSION_SERVO &&
         record->version != PRODUCT_CONFIG_VERSION_LEGACY) ||
        !product_config_usb_mode_is_valid(record->usb_mode) ||
        !product_config_board_no_is_valid(record->board_no)) {
        return false;
    }

    return product_config_crc32(record) == record->crc32;
}

static product_config_dpll_servo_profile_t
product_config_default_dpll_servo_profile(void)
{
    return (product_config_dpll_servo_profile_t){
        .kp_q16 = PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16,
        .ki_q16 = PRODUCT_CONFIG_DPLL_DEFAULT_KI_Q16,
        .update_period_us = PRODUCT_CONFIG_DPLL_DEFAULT_UPDATE_PERIOD_US,
        .step_threshold_ns = PRODUCT_CONFIG_DPLL_DEFAULT_STEP_THRESHOLD_NS,
        .sanity_freq_limit_ppb =
            PRODUCT_CONFIG_DPLL_DEFAULT_SANITY_FREQ_LIMIT_PPB,
    };
}

static product_config_dpll_control_profile_t
product_config_default_dpll_control_profile(void)
{
    return (product_config_dpll_control_profile_t){
        .mode = 0u, /* VDC_DPLL_CONTROL_MODE_MASTER */
        .follow_master_slot_id = 0u,
        .generation = 1u,
    };
}

static bool product_config_dpll_profile_is_valid(
    const product_config_record_t *record)
{
    return record != NULL && record->version >= PRODUCT_CONFIG_VERSION_SERVO &&
           record->version <= PRODUCT_CONFIG_VERSION &&
           record->reserved[0] == PRODUCT_CONFIG_DPLL_PROFILE_VALID;
}

static void product_config_record_set_dpll_profile(
    product_config_record_t *record,
    const product_config_dpll_servo_profile_t *profile)
{
    record->version = PRODUCT_CONFIG_VERSION;
    record->reserved[0] = PRODUCT_CONFIG_DPLL_PROFILE_VALID;
    record->reserved[1] = (uint32_t)profile->kp_q16;
    record->reserved[2] = (uint32_t)profile->ki_q16;
    record->reserved[3] = profile->update_period_us;
    record->reserved[4] = profile->step_threshold_ns;
    record->reserved[5] = profile->sanity_freq_limit_ppb;
}

static bool product_config_dpll_control_profile_is_valid(
    const product_config_record_t *record)
{
    return record != NULL && record->version >= PRODUCT_CONFIG_VERSION_SERVO &&
           record->version <= PRODUCT_CONFIG_VERSION &&
           record->reserved[6] == PRODUCT_CONFIG_DPLL_CONTROL_PROFILE_VALID &&
           record->reserved[7] <= 1u &&
           record->reserved[8] <= PRODUCT_CONFIG_DPLL_MAX_SOURCE_SLOT &&
           record->reserved[9] != 0u;
}

static void product_config_record_set_dpll_control_profile(
    product_config_record_t *record,
    const product_config_dpll_control_profile_t *profile)
{
    record->version = PRODUCT_CONFIG_VERSION;
    record->reserved[6] = PRODUCT_CONFIG_DPLL_CONTROL_PROFILE_VALID;
    record->reserved[7] = profile->mode;
    record->reserved[8] = profile->follow_master_slot_id;
    record->reserved[9] = profile->generation == 0u ? 1u : profile->generation;
}

static bool product_config_dpll_profiles_equal(
    const product_config_dpll_servo_profile_t *left,
    const product_config_dpll_servo_profile_t *right)
{
    return left != NULL && right != NULL &&
           left->kp_q16 == right->kp_q16 &&
           left->ki_q16 == right->ki_q16 &&
           left->update_period_us == right->update_period_us &&
           left->step_threshold_ns == right->step_threshold_ns &&
           left->sanity_freq_limit_ppb == right->sanity_freq_limit_ppb;
}

static bool product_config_output_timing_valid(const product_config_vdc_output_timing_profile_t *profile)
{
    return profile && profile->refill_low_us >= PRODUCT_CONFIG_VDC_OUTPUT_TIMING_MIN_US &&
        profile->refill_low_us < profile->plan_ahead_us &&
        profile->plan_ahead_us <= profile->commit_ahead_us &&
        profile->commit_ahead_us <= PRODUCT_CONFIG_VDC_OUTPUT_TIMING_MAX_US;
}

static void product_config_default_output_timing(product_config_record_t *record)
{
    record->output_plan_ahead_us = PRODUCT_CONFIG_VDC_OUTPUT_PLAN_DEFAULT_US;
    record->output_commit_ahead_us = PRODUCT_CONFIG_VDC_OUTPUT_COMMIT_DEFAULT_US;
    record->output_refill_low_us = PRODUCT_CONFIG_VDC_OUTPUT_REFILL_DEFAULT_US;
}

static bool product_config_reference_valid(const product_config_vdc_reference_profile_t *profile)
{
    if (profile == NULL ||
        profile->input_port < PRODUCT_CONFIG_VDC_REFERENCE_MIN_INPUT_PORT ||
        profile->input_port > PRODUCT_CONFIG_VDC_REFERENCE_MAX_INPUT_PORT ||
        profile->edge > 1u ||
        profile->nominal_hz < PRODUCT_CONFIG_VDC_REFERENCE_MIN_NOMINAL_HZ ||
        profile->nominal_hz > PRODUCT_CONFIG_VDC_REFERENCE_MAX_NOMINAL_HZ ||
        profile->window_ms < PRODUCT_CONFIG_VDC_REFERENCE_MIN_WINDOW_MS ||
        profile->window_ms > PRODUCT_CONFIG_VDC_REFERENCE_MAX_WINDOW_MS ||
        profile->timeout_ms <= profile->window_ms ||
        profile->timeout_ms > PRODUCT_CONFIG_VDC_REFERENCE_MAX_TIMEOUT_MS) return false;
    const uint64_t product = (uint64_t)profile->nominal_hz * profile->window_ms;
    return product % 1000u == 0u && product / 1000u != 0u && product / 1000u <= UINT32_MAX;
}

static product_config_vdc_reference_profile_t product_config_record_reference(
    const product_config_record_t *record)
{
    return (product_config_vdc_reference_profile_t){record->reference_input_port,
        record->reference_edge, record->reference_nominal_hz,
        record->reference_window_ms, record->reference_timeout_ms};
}

static void product_config_default_reference(product_config_record_t *record)
{
    record->reference_input_port = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_INPUT_PORT;
    record->reference_edge = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_EDGE;
    record->reference_nominal_hz = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_NOMINAL_HZ;
    record->reference_window_ms = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_WINDOW_MS;
    record->reference_timeout_ms = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_TIMEOUT_MS;
}

static void product_config_default_reference_discipline(product_config_record_t *record)
{
    record->reference_discipline_slew_ppb_per_s = PRODUCT_CONFIG_VDC_REFERENCE_DISCIPLINE_DEFAULT_SLEW_PPB_PER_S;
    record->reference_discipline_filter_divisor = PRODUCT_CONFIG_VDC_REFERENCE_DISCIPLINE_DEFAULT_FILTER_DIVISOR;
    record->reference_discipline_max_ppb = PRODUCT_CONFIG_VDC_REFERENCE_DISCIPLINE_DEFAULT_MAX_PPB;
}

static void product_config_set_default(product_config_record_t *record)
{
    const product_config_dpll_servo_profile_t default_profile =
        product_config_default_dpll_servo_profile();
    const product_config_dpll_control_profile_t default_control =
        product_config_default_dpll_control_profile();
    memset(record, 0, sizeof(*record));
    record->magic = PRODUCT_CONFIG_MAGIC;
    record->version = PRODUCT_CONFIG_VERSION;
    record->sequence = 0u;
    record->usb_mode = (uint32_t)product_config_default_usb_mode();
    record->board_no = 0u;
    record->baseline_max_replacements = PRODUCT_CONFIG_DPLL_BASELINE_DEFAULT_REPLACEMENTS;
    record->baseline_window_ns = PRODUCT_CONFIG_DPLL_BASELINE_DEFAULT_WINDOW_NS;
    product_config_default_output_timing(record);
    product_config_default_reference(record);
    product_config_default_reference_discipline(record);
    product_config_record_set_dpll_profile(record, &default_profile);
    product_config_record_set_dpll_control_profile(record, &default_control);
    record->crc32 = product_config_crc32(record);
}

static bool product_config_slot_is_erased(uint32_t slot)
{
    uint8_t page[PRODUCT_CONFIG_SLOT_SIZE];
    return drv_flash_read(OTA_PRODUCT_CONFIG_OFFSET +
                              slot * PRODUCT_CONFIG_SLOT_SIZE,
                          page, sizeof(page)) &&
           drv_flash_is_erased(OTA_PRODUCT_CONFIG_OFFSET +
                                   slot * PRODUCT_CONFIG_SLOT_SIZE,
                               sizeof(page));
}

static bool product_config_find_latest(product_config_record_t *latest,
                                       uint32_t *latest_slot,
                                       uint32_t *next_slot,
                                       bool *found_latest)
{
    bool found = false;
    uint32_t found_slot = 0u;
    uint32_t first_erased = UINT32_MAX;
    const uint32_t slot_count =
        FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE /
        PRODUCT_CONFIG_SLOT_SIZE;
    for (uint32_t slot = 0u; slot < slot_count; slot++) {
        product_config_record_t candidate;
        if (!drv_flash_read(OTA_PRODUCT_CONFIG_OFFSET +
                                 slot * PRODUCT_CONFIG_SLOT_SIZE,
                             &candidate, sizeof(candidate))) {
            return false;
        }
        if (product_config_slot_is_erased(slot)) {
            if (first_erased == UINT32_MAX) {
                first_erased = slot;
            }
            continue;
        }
        if (!product_config_record_is_valid(&candidate)) {
            continue;
        }
        if (!found || (int32_t)(candidate.sequence - latest->sequence) > 0) {
            *latest = candidate;
            found = true;
            found_slot = slot;
        }
    }
    if (latest_slot != NULL) {
        *latest_slot = found ? found_slot : UINT32_MAX;
    }
    if (next_slot != NULL) {
        *next_slot = first_erased;
    }
    if (found_latest != NULL) {
        *found_latest = found;
    }
    return true;
}

static bool product_config_store(const product_config_record_t *record)
{
    product_config_record_t latest;
    uint32_t latest_slot = UINT32_MAX;
    uint32_t slot = UINT32_MAX;
    bool found_latest = false;
    if (!product_config_find_latest(&latest, &latest_slot, &slot,
                                    &found_latest)) {
        return false;
    }
    if (slot == UINT32_MAX) {
        uint32_t rotate_sector = 0u;
        if (found_latest) {
            const uint32_t latest_sector =
                latest_slot / PRODUCT_CONFIG_SLOTS_PER_SECTOR;
            rotate_sector =
                (latest_sector + 1u) % PRODUCT_CONFIG_SECTOR_COUNT;
        }
        /* With no CRC-valid configuration there is no journal anchor to
         * preserve. Reclaim only the dedicated product-config sector so the
         * conservative startup profile can establish a new anchor. */
        if (!product_config_flash_execute(FLASH_TRANSACTION_OPERATION_ERASE,
                                          rotate_sector * PRODUCT_CONFIG_SECTOR_SIZE,
                                          NULL, PRODUCT_CONFIG_SECTOR_SIZE,
                                          record->sequence)) {
            return false;
        }
        slot = rotate_sector * PRODUCT_CONFIG_SLOTS_PER_SECTOR;
    }

    uint8_t page[PRODUCT_CONFIG_SLOT_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, record, sizeof(*record));

    if (!product_config_flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM,
                                      slot * PRODUCT_CONFIG_SLOT_SIZE,
                                      page, sizeof(page),
                                      record->sequence)) {
        return false;
    }

    product_config_record_t readback;
    if (!drv_flash_read(OTA_PRODUCT_CONFIG_OFFSET +
                            slot * PRODUCT_CONFIG_SLOT_SIZE,
                        &readback, sizeof(readback)) ||
        !product_config_record_is_valid(&readback) ||
        readback.sequence != record->sequence) {
        return false;
    }

    s_product_config = readback;
    return true;
}

bool product_config_init(void)
{
    product_config_record_t latest;
    bool found_latest = false;
    if (!product_config_find_latest(&latest, NULL, NULL, &found_latest)) {
        return false;
    }
    if (found_latest) {
        s_product_config = latest;
    } else {
        product_config_set_default(&s_product_config);
    }

    /* Startup must remain read-only. FlashTransaction parks core1 before any
     * erase/program operation, but product_config_init() runs before the
     * realtime core is launched. Persisting a v1-to-v2/default migration here
     * would therefore fail the boot of a freshly installed OTA image. Keep
     * the compatible identity fields and seed the missing DPLL profile only
     * in RAM; the explicit DPLL:STORE command persists it after bring-up. */
    const product_config_dpll_servo_profile_t default_profile =
        product_config_default_dpll_servo_profile();
    const product_config_dpll_control_profile_t default_control =
        product_config_default_dpll_control_profile();
    /* Determine both legacy flags before promoting version: v1 reserved
     * bytes must never become a valid v2 profile merely through migration. */
    const bool have_servo = product_config_dpll_profile_is_valid(&s_product_config);
    const bool have_control = product_config_dpll_control_profile_is_valid(&s_product_config);
    if (s_product_config.version < PRODUCT_CONFIG_VERSION_BASELINE ||
        s_product_config.baseline_max_replacements > PRODUCT_CONFIG_DPLL_BASELINE_MAX_REPLACEMENTS ||
        s_product_config.baseline_window_ns == 0u ||
        s_product_config.baseline_window_ns > PRODUCT_CONFIG_DPLL_BASELINE_MAX_WINDOW_NS) {
        s_product_config.baseline_max_replacements = PRODUCT_CONFIG_DPLL_BASELINE_DEFAULT_REPLACEMENTS;
        s_product_config.baseline_window_ns = PRODUCT_CONFIG_DPLL_BASELINE_DEFAULT_WINDOW_NS;
    }
    if (s_product_config.version < PRODUCT_CONFIG_VERSION_OUTPUT_DELAY)
        s_product_config.output_compensation_ns = PRODUCT_CONFIG_DPLL_OUTPUT_COMPENSATION_DEFAULT_NS;
    const product_config_vdc_output_timing_profile_t timing = {
        s_product_config.output_plan_ahead_us,
        s_product_config.output_commit_ahead_us,
        s_product_config.output_refill_low_us};
    if (s_product_config.version < PRODUCT_CONFIG_VERSION_OUTPUT_TIMING || !product_config_output_timing_valid(&timing))
        product_config_default_output_timing(&s_product_config);
    const product_config_vdc_reference_profile_t reference = product_config_record_reference(&s_product_config);
    if (s_product_config.version < PRODUCT_CONFIG_VERSION_REFERENCE || !product_config_reference_valid(&reference))
        product_config_default_reference(&s_product_config);
    if (s_product_config.version < PRODUCT_CONFIG_VERSION)
        product_config_default_reference_discipline(&s_product_config);
    if (!have_servo)
        product_config_record_set_dpll_profile(&s_product_config, &default_profile);
    if (!have_control)
        product_config_record_set_dpll_control_profile(&s_product_config, &default_control);
    s_product_config.version = PRODUCT_CONFIG_VERSION;
    s_product_config.crc32 = product_config_crc32(&s_product_config);
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
    if (record.usb_mode == (uint32_t)mode) {
        s_product_config = record;
        return true;
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
    record.version = PRODUCT_CONFIG_VERSION;
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    return product_config_store(&record) &&
           s_product_config.board_no == board_no;
}

bool product_config_get_dpll_servo_profile(
    product_config_dpll_servo_profile_t *profile)
{
    if (profile == NULL || !product_config_record_is_valid(&s_product_config) ||
        !product_config_dpll_profile_is_valid(&s_product_config)) {
        return false;
    }

    profile->kp_q16 = (int32_t)s_product_config.reserved[1];
    profile->ki_q16 = (int32_t)s_product_config.reserved[2];
    profile->update_period_us = s_product_config.reserved[3];
    profile->step_threshold_ns = s_product_config.reserved[4];
    profile->sanity_freq_limit_ppb = s_product_config.reserved[5];
    return true;
}

bool product_config_set_dpll_servo_profile(
    const product_config_dpll_servo_profile_t *profile)
{
    if (profile == NULL) {
        return false;
    }

    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) {
        product_config_set_default(&record);
    }
    product_config_record_set_dpll_profile(&record, profile);
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    product_config_dpll_servo_profile_t readback;
    return product_config_store(&record) &&
           product_config_get_dpll_servo_profile(&readback) &&
           product_config_dpll_profiles_equal(&readback, profile);
}

bool product_config_get_dpll_control_profile(
    product_config_dpll_control_profile_t *profile)
{
    if (profile == NULL || !product_config_record_is_valid(&s_product_config)) {
        return false;
    }
    if (!product_config_dpll_control_profile_is_valid(&s_product_config)) {
        *profile = product_config_default_dpll_control_profile();
        return true;
    }
    profile->mode = s_product_config.reserved[7];
    profile->follow_master_slot_id = s_product_config.reserved[8];
    profile->generation = s_product_config.reserved[9];
    return true;
}

bool product_config_set_dpll_control_profile(
    const product_config_dpll_control_profile_t *profile)
{
    if (profile == NULL || profile->mode > 1u ||
        profile->follow_master_slot_id > PRODUCT_CONFIG_DPLL_MAX_SOURCE_SLOT) {
        return false;
    }

    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) {
        product_config_set_default(&record);
    }
    product_config_record_set_dpll_control_profile(&record, profile);
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    product_config_dpll_control_profile_t readback;
    return product_config_store(&record) &&
           product_config_get_dpll_control_profile(&readback) &&
           readback.mode == profile->mode &&
           readback.follow_master_slot_id == profile->follow_master_slot_id;
}

bool product_config_get_dpll_baseline_profile(product_config_dpll_baseline_profile_t *profile)
{
    if (profile == NULL || !product_config_record_is_valid(&s_product_config) ||
        s_product_config.version != PRODUCT_CONFIG_VERSION ||
        s_product_config.baseline_max_replacements > PRODUCT_CONFIG_DPLL_BASELINE_MAX_REPLACEMENTS ||
        s_product_config.baseline_window_ns == 0u ||
        s_product_config.baseline_window_ns > PRODUCT_CONFIG_DPLL_BASELINE_MAX_WINDOW_NS) return false;
    *profile = (product_config_dpll_baseline_profile_t){
        .max_replacements = s_product_config.baseline_max_replacements,
        .window_ns = s_product_config.baseline_window_ns};
    return true;
}

bool product_config_set_dpll_baseline_profile(const product_config_dpll_baseline_profile_t *profile)
{
    if (profile == NULL || profile->max_replacements > PRODUCT_CONFIG_DPLL_BASELINE_MAX_REPLACEMENTS ||
        profile->window_ns == 0u || profile->window_ns > PRODUCT_CONFIG_DPLL_BASELINE_MAX_WINDOW_NS)
        return false;
    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) product_config_set_default(&record);
    record.version = PRODUCT_CONFIG_VERSION;
    record.baseline_max_replacements = profile->max_replacements;
    record.baseline_window_ns = profile->window_ns;
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    product_config_dpll_baseline_profile_t readback;
    return product_config_store(&record) && product_config_get_dpll_baseline_profile(&readback) &&
        readback.max_replacements == profile->max_replacements && readback.window_ns == profile->window_ns;
}

bool product_config_get_dpll_output_compensation_ns(int32_t *value)
{
    if (value == NULL || !product_config_record_is_valid(&s_product_config) ||
        s_product_config.version != PRODUCT_CONFIG_VERSION) return false;
    *value = s_product_config.output_compensation_ns;
    return true;
}

bool product_config_get_vdc_output_timing_profile(product_config_vdc_output_timing_profile_t *profile)
{
    if (!profile || !product_config_record_is_valid(&s_product_config) ||
        s_product_config.version != PRODUCT_CONFIG_VERSION) return false;
    const product_config_vdc_output_timing_profile_t value = {
        s_product_config.output_plan_ahead_us,
        s_product_config.output_commit_ahead_us,
        s_product_config.output_refill_low_us};
    if (!product_config_output_timing_valid(&value)) return false;
    *profile = value;
    return true;
}

bool product_config_get_vdc_reference_profile(product_config_vdc_reference_profile_t *profile)
{
    if (profile == NULL || !product_config_record_is_valid(&s_product_config) ||
        s_product_config.version != PRODUCT_CONFIG_VERSION) return false;
    const product_config_vdc_reference_profile_t value = product_config_record_reference(&s_product_config);
    if (!product_config_reference_valid(&value)) return false;
    *profile = value;
    return true;
}

bool product_config_set_vdc_reference_profile(const product_config_vdc_reference_profile_t *profile)
{
    if (!product_config_reference_valid(profile)) return false;
    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) product_config_set_default(&record);
    record.version = PRODUCT_CONFIG_VERSION;
    record.reference_input_port = profile->input_port;
    record.reference_edge = profile->edge;
    record.reference_nominal_hz = profile->nominal_hz;
    record.reference_window_ms = profile->window_ms;
    record.reference_timeout_ms = profile->timeout_ms;
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    product_config_vdc_reference_profile_t readback;
    return product_config_store(&record) && product_config_get_vdc_reference_profile(&readback) &&
        readback.input_port == profile->input_port && readback.edge == profile->edge &&
        readback.nominal_hz == profile->nominal_hz && readback.window_ms == profile->window_ms &&
        readback.timeout_ms == profile->timeout_ms;
}

bool product_config_get_vdc_reference_discipline_profile(product_config_vdc_reference_discipline_profile_t *profile)
{
    if (profile == NULL || !product_config_record_is_valid(&s_product_config) ||
        s_product_config.version != PRODUCT_CONFIG_VERSION) return false;
    *profile = (product_config_vdc_reference_discipline_profile_t){
        s_product_config.reference_discipline_slew_ppb_per_s,
        s_product_config.reference_discipline_filter_divisor,
        s_product_config.reference_discipline_max_ppb};
    return true;
}

bool product_config_set_vdc_reference_discipline_profile(const product_config_vdc_reference_discipline_profile_t *profile)
{
    if (profile == NULL) return false;
    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) product_config_set_default(&record);
    record.version = PRODUCT_CONFIG_VERSION;
    record.reference_discipline_slew_ppb_per_s = profile->slew_ppb_per_s;
    record.reference_discipline_filter_divisor = profile->filter_divisor;
    record.reference_discipline_max_ppb = profile->max_ppb;
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    product_config_vdc_reference_discipline_profile_t readback;
    return product_config_store(&record) && product_config_get_vdc_reference_discipline_profile(&readback) &&
        readback.slew_ppb_per_s == profile->slew_ppb_per_s &&
        readback.filter_divisor == profile->filter_divisor && readback.max_ppb == profile->max_ppb;
}

bool product_config_set_vdc_output_timing_profile(const product_config_vdc_output_timing_profile_t *profile)
{
    if (!product_config_output_timing_valid(profile)) return false;
    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) product_config_set_default(&record);
    record.version = PRODUCT_CONFIG_VERSION;
    record.output_plan_ahead_us = profile->plan_ahead_us;
    record.output_commit_ahead_us = profile->commit_ahead_us;
    record.output_refill_low_us = profile->refill_low_us;
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    product_config_vdc_output_timing_profile_t readback;
    return product_config_store(&record) && product_config_get_vdc_output_timing_profile(&readback) &&
        readback.plan_ahead_us == profile->plan_ahead_us &&
        readback.commit_ahead_us == profile->commit_ahead_us &&
        readback.refill_low_us == profile->refill_low_us;
}

bool product_config_set_dpll_output_compensation_ns(int32_t value)
{
    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) product_config_set_default(&record);
    record.version = PRODUCT_CONFIG_VERSION;
    record.output_compensation_ns = value;
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    int32_t readback;
    return product_config_store(&record) &&
        product_config_get_dpll_output_compensation_ns(&readback) && readback == value;
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
