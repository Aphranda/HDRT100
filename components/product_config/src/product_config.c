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
#define PRODUCT_CONFIG_VERSION 3u
/* Records written before the realtime phase table existed keep the deployed
 * 64-byte prefix and the CRC domain they were sealed with.  They are still
 * read, and promoted to PRODUCT_CONFIG_VERSION in RAM only. */
#define PRODUCT_CONFIG_VERSION_V2     2u
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

/* reserved[] word map.  Words [0..9] are the version-2 prefix and keep their
 * meaning; the core1 realtime phase table owns every word above them:
 *
 *   [0]      DPLL servo profile marker (PRODUCT_CONFIG_DPLL_PROFILE_VALID)
 *   [1]      DPLL servo kp_q16
 *   [2]      DPLL servo ki_q16
 *   [3]      DPLL servo update_period_us
 *   [4]      DPLL servo step_threshold_ns
 *   [5]      DPLL servo sanity_freq_limit_ppb
 *   [6]      DPLL control profile marker (PRODUCT_CONFIG_DPLL_CONTROL_PROFILE_VALID)
 *   [7]      DPLL control mode
 *   [8]      DPLL control follow_master_slot_id
 *   [9]      DPLL control generation
 *   [10]     realtime profile marker (0 = no table stored)
 *   [11]     realtime cycle_cycles
 *   [12]     realtime phase_count
 *   [13..22] realtime start_cycle[0..9]
 *   [23..32] realtime end_cycle[0..9]
 *   [33..42] realtime wcet_cycles[0..9]
 *
 * Marker, cycle and phase_count plus the three window arrays are 33 words
 * ([10..42]), so the last window word lands in reserved[42] and reserved[]
 * holds 43 words: a 42-word array could not address wcet_cycles[9].  With the
 * five identity words and the CRC the record is 196 bytes, still well inside
 * one PRODUCT_CONFIG_SLOT_SIZE program page. */
#define PRODUCT_CONFIG_REALTIME_PROFILE_MARKER_WORD      10u
#define PRODUCT_CONFIG_REALTIME_PROFILE_CYCLE_WORD       11u
#define PRODUCT_CONFIG_REALTIME_PROFILE_PHASE_COUNT_WORD 12u
#define PRODUCT_CONFIG_REALTIME_PROFILE_START_WORD       13u
#define PRODUCT_CONFIG_REALTIME_PROFILE_END_WORD                          \
    (PRODUCT_CONFIG_REALTIME_PROFILE_START_WORD +                         \
     PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES) /* 23 */
#define PRODUCT_CONFIG_REALTIME_PROFILE_WCET_WORD                         \
    (PRODUCT_CONFIG_REALTIME_PROFILE_END_WORD +                           \
     PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES) /* 33 */
#define PRODUCT_CONFIG_RESERVED_WORDS                                     \
    (PRODUCT_CONFIG_REALTIME_PROFILE_WCET_WORD +                          \
     PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES) /* 43 */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t usb_mode;
    uint32_t board_no;
    uint32_t reserved[PRODUCT_CONFIG_RESERVED_WORDS];
    uint32_t crc32;
} product_config_record_t;

/* Deployed record of versions 1 and 2: the same five identity words, ten
 * reserved words, and a CRC that covers exactly these 64 bytes.  Its crc32 word
 * occupies the offset that the current record uses for the realtime profile
 * marker, so a slot image is decoded through this type and is never re-read as
 * a current record. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t usb_mode;
    uint32_t board_no;
    uint32_t reserved[10];
    uint32_t crc32;
} product_config_legacy_record_t;

_Static_assert(sizeof(product_config_record_t) <= PRODUCT_CONFIG_SLOT_SIZE,
               "Product Config record must fit one program-page slot");
_Static_assert(PRODUCT_CONFIG_REALTIME_PROFILE_WCET_WORD +
                       PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES ==
                   sizeof(((product_config_record_t *)0)->reserved) /
                       sizeof(uint32_t),
               "reserved[] must address every realtime window word");
_Static_assert(sizeof(product_config_legacy_record_t) == 64u,
               "the legacy Product Config record is the deployed 64-byte layout");
_Static_assert(offsetof(product_config_record_t, reserved) ==
                   offsetof(product_config_legacy_record_t, reserved),
               "the legacy prefix must overlay the current reserved[] words");
_Static_assert(offsetof(product_config_record_t, reserved) +
                       PRODUCT_CONFIG_REALTIME_PROFILE_MARKER_WORD *
                           sizeof(uint32_t) ==
                   offsetof(product_config_legacy_record_t, crc32),
               "the legacy crc32 word overlays the realtime profile marker word");

#define PRODUCT_CONFIG_DPLL_PROFILE_VALID 0x44504C4Cu /* DPLL */
#define PRODUCT_CONFIG_DPLL_CONTROL_PROFILE_VALID 0x44524F4Cu /* DROL */
#define PRODUCT_CONFIG_DPLL_MAX_SOURCE_SLOT 7u
#define PRODUCT_CONFIG_REALTIME_PROFILE_VALID 0x52545046u /* RTPF */

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
    return ota_crc32_compute((const uint8_t *)&copy, sizeof(copy));
}

/* CRC of the deployed 64-byte prefix, taken over exactly those bytes with the
 * legacy crc32 word zeroed. */
static uint32_t product_config_legacy_crc32(const uint8_t *slot)
{
    product_config_legacy_record_t legacy;
    memcpy(&legacy, slot, sizeof(legacy));
    legacy.crc32 = 0u;
    return ota_crc32_compute((const uint8_t *)&legacy, sizeof(legacy));
}

static bool product_config_legacy_record_is_valid(const uint8_t *slot)
{
    product_config_legacy_record_t legacy;
    memcpy(&legacy, slot, sizeof(legacy));
    if (legacy.magic != PRODUCT_CONFIG_MAGIC ||
        (legacy.version != PRODUCT_CONFIG_VERSION_LEGACY &&
         legacy.version != PRODUCT_CONFIG_VERSION_V2) ||
        !product_config_usb_mode_is_valid(legacy.usb_mode) ||
        !product_config_board_no_is_valid(legacy.board_no)) {
        return false;
    }

    return product_config_legacy_crc32(slot) == legacy.crc32;
}

/* RAM-only promotion of a version 1/2 slot image: the identity words and the
 * already-present DPLL markers and fields survive verbatim, while everything
 * the old layout never carried (the realtime profile words) stays absent. */
static void product_config_legacy_record_promote(
    const uint8_t *slot, product_config_record_t *record)
{
    product_config_legacy_record_t legacy;
    memcpy(&legacy, slot, sizeof(legacy));
    memset(record, 0, sizeof(*record));
    record->magic = PRODUCT_CONFIG_MAGIC;
    record->version = PRODUCT_CONFIG_VERSION;
    record->sequence = legacy.sequence;
    record->usb_mode = legacy.usb_mode;
    record->board_no = legacy.board_no;
    memcpy(record->reserved, legacy.reserved, sizeof(legacy.reserved));
    record->crc32 = product_config_crc32(record);
}

/* Validates exactly the current layout: a legacy prefix is accepted only by
 * product_config_legacy_record_is_valid() and is promoted before use. */
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
    return record != NULL && record->version == PRODUCT_CONFIG_VERSION &&
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
    return record != NULL && record->version == PRODUCT_CONFIG_VERSION &&
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

static bool product_config_realtime_profile_is_valid(
    const product_config_record_t *record)
{
    return record != NULL && record->version == PRODUCT_CONFIG_VERSION &&
           record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_MARKER_WORD] ==
               PRODUCT_CONFIG_REALTIME_PROFILE_VALID &&
           record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_CYCLE_WORD] != 0u &&
           record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_PHASE_COUNT_WORD] ==
               PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES;
}

/* The record stores a whole table or nothing, and it never judges the phase
 * arithmetic: admission is the caller's job.  A zero cycle is refused here
 * because it is the absence value the marker cannot express. */
static bool product_config_realtime_profile_is_storable(
    const product_config_realtime_profile_t *profile)
{
    return profile != NULL &&
           profile->phase_count == PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES &&
           profile->cycle_cycles != 0u;
}

static void product_config_record_set_realtime_profile(
    product_config_record_t *record,
    const product_config_realtime_profile_t *profile)
{
    record->version = PRODUCT_CONFIG_VERSION;
    record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_MARKER_WORD] =
        PRODUCT_CONFIG_REALTIME_PROFILE_VALID;
    record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_CYCLE_WORD] =
        profile->cycle_cycles;
    record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_PHASE_COUNT_WORD] =
        profile->phase_count;
    for (uint32_t phase = 0u;
         phase < PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES; phase++) {
        record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_START_WORD + phase] =
            profile->start_cycle[phase];
        record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_END_WORD + phase] =
            profile->end_cycle[phase];
        record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_WCET_WORD + phase] =
            profile->wcet_cycles[phase];
    }
}

/* The marker is cleared and nothing else: the window words keep their previous
 * bytes so the stored layout stays deterministic and a stale table can never be
 * mistaken for a live one. */
static void product_config_record_clear_realtime_profile(
    product_config_record_t *record)
{
    record->version = PRODUCT_CONFIG_VERSION;
    record->reserved[PRODUCT_CONFIG_REALTIME_PROFILE_MARKER_WORD] = 0u;
}

static bool product_config_realtime_profiles_equal(
    const product_config_realtime_profile_t *left,
    const product_config_realtime_profile_t *right)
{
    if (left == NULL || right == NULL ||
        left->cycle_cycles != right->cycle_cycles ||
        left->phase_count != right->phase_count) {
        return false;
    }
    for (uint32_t phase = 0u;
         phase < PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES; phase++) {
        if (left->start_cycle[phase] != right->start_cycle[phase] ||
            left->end_cycle[phase] != right->end_cycle[phase] ||
            left->wcet_cycles[phase] != right->wcet_cycles[phase]) {
            return false;
        }
    }
    return true;
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
        if (product_config_record_is_valid(&candidate)) {
            /* Current layout, used as stored. */
        } else if (product_config_legacy_record_is_valid(
                       (const uint8_t *)&candidate)) {
            /* Version 1/2 image: promote it in RAM so every reader above sees
             * the current layout. The slot itself is left untouched; the
             * migration reaches flash only through an explicit store command. */
            product_config_record_t promoted;
            product_config_legacy_record_promote((const uint8_t *)&candidate,
                                                 &promoted);
            candidate = promoted;
        } else {
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

    /* A completion/lockout-release error can occur after PROGRAM committed.
     * Reconcile the actual durable bytes even on that path; otherwise callers
     * retain the old RAM value while the next boot adopts the new record. */
    (void)product_config_flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM,
                                       slot * PRODUCT_CONFIG_SLOT_SIZE,
                                       page, sizeof(page),
                                       record->sequence);

    product_config_record_t readback;
    if (!drv_flash_read(OTA_PRODUCT_CONFIG_OFFSET +
                            slot * PRODUCT_CONFIG_SLOT_SIZE,
                        &readback, sizeof(readback)) ||
        !product_config_record_is_valid(&readback) ||
        memcmp(&readback, record, sizeof(readback)) != 0) {
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

    if (found_latest &&
        product_config_dpll_profile_is_valid(&s_product_config)) {
        return true;
    }

    /* Startup must remain read-only. FlashTransaction parks core1 before any
     * erase/program operation, but product_config_init() runs before the
     * realtime core is launched. Persisting a v1/v2-to-v3/default migration
     * here would therefore fail the boot of a freshly installed OTA image.
     * Keep the compatible identity fields, the promoted DPLL markers and the
     * absent realtime table, and seed only the missing DPLL profile in RAM;
     * the explicit DPLL:STORE command persists it after bring-up. */
    const product_config_dpll_servo_profile_t default_profile =
        product_config_default_dpll_servo_profile();
    const product_config_dpll_control_profile_t default_control =
        product_config_default_dpll_control_profile();
    product_config_record_set_dpll_profile(&s_product_config,
                                           &default_profile);
    product_config_record_set_dpll_control_profile(&s_product_config,
                                                   &default_control);
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

bool product_config_get_realtime_profile(
    product_config_realtime_profile_t *profile)
{
    if (profile == NULL || !product_config_record_is_valid(&s_product_config) ||
        !product_config_realtime_profile_is_valid(&s_product_config)) {
        return false;
    }

    /* Decode into a local so a refused profile leaves the caller's value
     * untouched. */
    product_config_realtime_profile_t decoded;
    decoded.cycle_cycles =
        s_product_config.reserved[PRODUCT_CONFIG_REALTIME_PROFILE_CYCLE_WORD];
    decoded.phase_count =
        s_product_config
            .reserved[PRODUCT_CONFIG_REALTIME_PROFILE_PHASE_COUNT_WORD];
    for (uint32_t phase = 0u;
         phase < PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES; phase++) {
        const uint32_t start_word =
            PRODUCT_CONFIG_REALTIME_PROFILE_START_WORD + phase;
        const uint32_t end_word =
            PRODUCT_CONFIG_REALTIME_PROFILE_END_WORD + phase;
        const uint32_t wcet_word =
            PRODUCT_CONFIG_REALTIME_PROFILE_WCET_WORD + phase;
        decoded.start_cycle[phase] = s_product_config.reserved[start_word];
        decoded.end_cycle[phase] = s_product_config.reserved[end_word];
        decoded.wcet_cycles[phase] = s_product_config.reserved[wcet_word];
    }
    *profile = decoded;
    return true;
}

bool product_config_set_realtime_profile(
    const product_config_realtime_profile_t *profile)
{
    if (!product_config_realtime_profile_is_storable(profile)) {
        return false;
    }

    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) {
        product_config_set_default(&record);
    }
    product_config_record_set_realtime_profile(&record, profile);
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    product_config_realtime_profile_t readback;
    return product_config_store(&record) &&
           product_config_get_realtime_profile(&readback) &&
           product_config_realtime_profiles_equal(&readback, profile);
}

bool product_config_clear_realtime_profile(void)
{
    product_config_record_t record = s_product_config;
    if (!product_config_record_is_valid(&record)) {
        product_config_set_default(&record);
    }
    product_config_record_clear_realtime_profile(&record);
    record.sequence++;
    record.crc32 = product_config_crc32(&record);
    product_config_realtime_profile_t readback;
    return product_config_store(&record) &&
           !product_config_get_realtime_profile(&readback);
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
