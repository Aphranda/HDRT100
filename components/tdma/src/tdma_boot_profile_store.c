#include "tdma_boot_profile_store.h"

#include <string.h>

#include "drv_flash.h"
#include "flash_deployment_map.h"
#include "flash_store_nvs.h"

#include "flash_store_ab.h"
#include "flash_store_record.h"
#include "flash_transaction.h"

#if FLASH_DEPLOYMENT_MAP_HAS_TDMA_BOOT_PROFILE

#define BOOT_PROFILE_SCHEMA_VERSION 1u
#define BOOT_PROFILE_OBJECT_TYPE 0x42505246u /* "BPRF" */
#define BOOT_PROFILE_SECTOR_COUNT 2u
#define BOOT_PROFILE_PROGRAM_SIZE FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE
#define BOOT_PROFILE_ERASE_SIZE FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE
#define BOOT_PROFILE_COMMIT_OFFSET FLASH_STORE_RECORD_COMMIT_OFFSET

typedef struct {
    bool usable;
    uint32_t sector;
    flash_store_nvs_result_t result;
    flash_store_nvs_scan_t scan;
} boot_profile_scan_t;

static tdma_boot_profile_t s_cached;
static tdma_boot_profile_store_status_t s_status;
static uint8_t s_payload[TDMA_BOOT_PROFILE_PAYLOAD_SIZE];
static uint8_t s_scan_payload[TDMA_BOOT_PROFILE_PAYLOAD_SIZE];
/* One append is one record, so the program buffer only needs the record span
 * rounded to a program page - not a whole erase sector. The v2 image has no
 * spare RAM, and the provisioning path made this buffer reachable for the
 * first time (it was garbage-collected before). */
#define BOOT_PROFILE_SPAN_MAX \
    ((((FLASH_STORE_RECORD_HEADER_SIZE + TDMA_BOOT_PROFILE_PAYLOAD_SIZE) + \
       (BOOT_PROFILE_PROGRAM_SIZE - 1u)) / BOOT_PROFILE_PROGRAM_SIZE) * \
     BOOT_PROFILE_PROGRAM_SIZE)
_Static_assert(BOOT_PROFILE_SPAN_MAX >= FLASH_STORE_RECORD_HEADER_SIZE +
                                         TDMA_BOOT_PROFILE_PAYLOAD_SIZE,
               "boot profile program span must hold one record");
static uint8_t s_program[BOOT_PROFILE_SPAN_MAX];
static uint8_t s_commit_page[BOOT_PROFILE_PROGRAM_SIZE];
static uint32_t s_provider_generation;
static uint32_t s_provider_refs;

static uint32_t sector_absolute(uint32_t sector)
{
    /* The deployment map names the carrier partition (CALIBRATION_NVS) in
     * OFFSET and the sub-store inside it in RELATIVE_OFFSET; the XIP view must
     * combine both, otherwise the scan reads the calibration store instead of
     * the boot-profile sectors (measured on hardware 2026-11-07: every
     * provision reported RECORD because the scan kept seeing TRN-03). */
    return FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_OFFSET +
           FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_RELATIVE_OFFSET +
           sector * BOOT_PROFILE_ERASE_SIZE;
}

static uint32_t sector_relative(uint32_t sector)
{
    return FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_RELATIVE_OFFSET +
           sector * BOOT_PROFILE_ERASE_SIZE;
}

static uint32_t next_provider_generation(void)
{
    s_provider_generation++;
    if (s_provider_generation == 0u) {
        s_provider_generation = 1u;
    }
    return s_provider_generation;
}

static bool provider_retain(void *context)
{
    (void)context;
    s_provider_refs++;
    return true;
}

static void provider_release(void *context)
{
    (void)context;
    if (s_provider_refs != 0u) {
        s_provider_refs--;
    }
}

static bool flash_execute(uint32_t operation,
                          uint32_t relative_offset,
                          const uint8_t *data,
                          uint32_t length,
                          uint32_t store_generation)
{
    const uint32_t generation =
        operation == FLASH_TRANSACTION_OPERATION_PROGRAM
            ? next_provider_generation() : 0u;
    const flash_transaction_buffer_lease_t lease = {
        .data = data,
        .length = length,
        .generation = generation,
        .context = &s_provider_refs,
        .retain = provider_retain,
        .release = provider_release,
    };
    return flash_transaction_ao_execute_range(
        FLASH_TRANSACTION_REQUESTER_CALIBRATION,
        FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_PARTITION_ID,
        operation, relative_offset, data, length, store_generation, generation,
        operation == FLASH_TRANSACTION_OPERATION_PROGRAM ? &lease : NULL);
}

static boot_profile_scan_t scan_sector(uint32_t sector)
{
    boot_profile_scan_t result = {.sector = sector};
    const uint8_t *base = drv_flash_xip_ptr(sector_absolute(sector));
    if (base == NULL) {
        result.result = FLASH_STORE_NVS_BAD_ARGUMENT;
        return result;
    }
    result.result = flash_store_nvs_scan(
        base, BOOT_PROFILE_ERASE_SIZE, BOOT_PROFILE_SCHEMA_VERSION,
        BOOT_PROFILE_OBJECT_TYPE, 0u, BOOT_PROFILE_PROGRAM_SIZE,
        s_scan_payload, sizeof(s_scan_payload), &result.scan);
    result.usable = result.result == FLASH_STORE_NVS_OK ||
                    result.result == FLASH_STORE_NVS_EMPTY ||
                    result.result == FLASH_STORE_NVS_CORRUPT;
    return result;
}

static bool scan_has_latest(const boot_profile_scan_t *scan)
{
    return scan != NULL && scan->usable && scan->scan.has_latest;
}

static bool select_latest(boot_profile_scan_t scans[BOOT_PROFILE_SECTOR_COUNT],
                          uint32_t *latest_sector)
{
    const flash_store_nvs_scan_t pair[2] = {scans[0].scan, scans[1].scan};
    const bool usable[2] = {scans[0].usable, scans[1].usable};
    return flash_store_ab_select_latest(pair, usable, latest_sector);
}

static bool decode_latest(const boot_profile_scan_t *scan,
                          tdma_boot_profile_t *profile,
                          flash_store_record_header_t *header)
{
    if (!scan_has_latest(scan)) {
        return false;
    }
    const uint8_t *record = drv_flash_xip_ptr(
        sector_absolute(scan->sector) + (uint32_t)scan->scan.latest_offset);
    if (record == NULL || flash_store_record_decode(
            record,
            FLASH_STORE_RECORD_HEADER_SIZE +
                scan->scan.latest.payload_length,
            BOOT_PROFILE_SCHEMA_VERSION, BOOT_PROFILE_OBJECT_TYPE, 0u,
            s_payload, sizeof(s_payload), header) != FLASH_STORE_RECORD_OK) {
        return false;
    }
    return tdma_boot_profile_decode(s_payload, header->payload_length, profile);
}

static bool program_record(uint32_t sector,
                           uint32_t append_offset,
                           uint32_t program_size,
                           uint32_t store_generation)
{
    if (program_size == 0u ||
        (program_size % BOOT_PROFILE_PROGRAM_SIZE) != 0u) {
        return false;
    }
    memcpy(s_commit_page, s_program, sizeof(s_commit_page));
    memset(&s_program[BOOT_PROFILE_COMMIT_OFFSET], 0xFF, sizeof(uint32_t));
    const uint32_t base = sector_relative(sector) + append_offset;
    for (uint32_t offset = 0u; offset < program_size;
         offset += BOOT_PROFILE_PROGRAM_SIZE) {
        if (!flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM,
                           base + offset, &s_program[offset],
                           BOOT_PROFILE_PROGRAM_SIZE, store_generation)) {
            return false;
        }
    }
    /* The commit marker shares the first program page: after every body page
     * verified, re-programming that page only clears erased bits. */
    return flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM, base,
                         s_commit_page, BOOT_PROFILE_PROGRAM_SIZE,
                         store_generation);
}

bool tdma_boot_profile_store_init(void)
{
    memset(&s_cached, 0, sizeof(s_cached));
    memset(&s_status, 0, sizeof(s_status));
    s_status.supported = true;
    s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_EMPTY;
    s_provider_generation = 0u;
    s_provider_refs = 0u;

    boot_profile_scan_t scans[BOOT_PROFILE_SECTOR_COUNT] = {
        scan_sector(0u), scan_sector(1u)};
    if (!scans[0].usable || !scans[1].usable) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_RECORD;
        return true;
    }
    uint32_t latest_sector = 0u;
    if (!select_latest(scans, &latest_sector)) {
        return true;
    }
    flash_store_record_header_t header;
    if (!decode_latest(&scans[latest_sector], &s_cached, &header)) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_RECORD;
        return true;
    }
    s_status.loaded = true;
    s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_NONE;
    s_status.generation = header.generation;
    s_status.sequence = header.sequence;
    s_status.payload_crc32 = header.payload_crc32;
    return true;
}

bool tdma_boot_profile_store_get(tdma_boot_profile_t *out)
{
    if (out == NULL || !s_status.loaded) {
        return false;
    }
    *out = s_cached;
    return true;
}

bool tdma_boot_profile_store_commit(const tdma_boot_profile_t *profile,
                                    uint32_t nonce)
{
    if (!s_status.supported) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_UNSUPPORTED;
        return false;
    }
    if (!tdma_boot_profile_encode(profile, s_payload, sizeof(s_payload))) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_PROFILE;
        return false;
    }
    boot_profile_scan_t scans[BOOT_PROFILE_SECTOR_COUNT] = {
        scan_sector(0u), scan_sector(1u)};
    for (uint32_t sector = 0u; sector < BOOT_PROFILE_SECTOR_COUNT; sector++) {
        s_status.scan_result[sector] = (uint32_t)scans[sector].result;
        s_status.scan_usable[sector] = scans[sector].usable ? 1u : 0u;
    }
    /* A sector we cannot parse (old schema/object, bad region) must not block
     * provisioning for ever: erase it and rescan instead of refusing. */
    bool reclaimed = false;
    for (uint32_t sector = 0u; sector < BOOT_PROFILE_SECTOR_COUNT; sector++) {
        if (scans[sector].usable) {
            continue;
        }
        if (!flash_execute(FLASH_TRANSACTION_OPERATION_ERASE,
                           sector_relative(sector), NULL,
                           BOOT_PROFILE_ERASE_SIZE,
                           profile->generation * 2654435761u + nonce)) {
            s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_FLASH;
            return false;
        }
        reclaimed = true;
    }
    if (reclaimed) {
        scans[0] = scan_sector(0u);
        scans[1] = scan_sector(1u);
        if (!scans[0].usable || !scans[1].usable) {
            s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_RECORD;
            return false;
        }
    }
    uint32_t latest_sector = 0u;
    const bool has_latest = select_latest(scans, &latest_sector);
    if (has_latest &&
        (int32_t)(profile->generation -
                  scans[latest_sector].scan.latest.generation) < 0) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_RECORD;
        return false;
    }
    uint32_t record_sequence = 1u;
    if (has_latest) {
        record_sequence = flash_store_ab_next_sequence(
            true, profile->generation,
            scans[latest_sector].scan.latest.generation,
            scans[latest_sector].scan.latest.sequence);
    }
    const size_t required_span = flash_store_nvs_record_span(
        FLASH_STORE_RECORD_HEADER_SIZE + sizeof(s_payload),
        BOOT_PROFILE_PROGRAM_SIZE);
    uint32_t target_sector = has_latest ? latest_sector : 0u;
    size_t append_offset = has_latest
                               ? scans[latest_sector].scan.append_offset : 0u;
    const bool rotate = has_latest &&
        (scans[latest_sector].scan.saw_torn_tail ||
         scans[latest_sector].scan.needs_rotation ||
         required_span > BOOT_PROFILE_ERASE_SIZE - append_offset);
    if (rotate) {
        target_sector = latest_sector ^ 1u;
        append_offset = 0u;
    }
    const bool target_erased = drv_flash_is_erased(
        sector_absolute(target_sector), BOOT_PROFILE_ERASE_SIZE);
    if ((rotate || (!has_latest && !target_erased)) &&
        !flash_execute(FLASH_TRANSACTION_OPERATION_ERASE,
                       sector_relative(target_sector), NULL,
                       BOOT_PROFILE_ERASE_SIZE,
                       profile->generation * 2654435761u + nonce)) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_FLASH;
        return false;
    }
    size_t program_size = 0u;
    if (flash_store_nvs_plan_append(
            BOOT_PROFILE_SCHEMA_VERSION, BOOT_PROFILE_OBJECT_TYPE,
            profile->generation, record_sequence, 0u, s_payload,
            sizeof(s_payload), append_offset, BOOT_PROFILE_ERASE_SIZE,
            BOOT_PROFILE_PROGRAM_SIZE, s_program, sizeof(s_program),
            &program_size) != FLASH_STORE_NVS_OK ||
        !program_record(target_sector, (uint32_t)append_offset,
                        (uint32_t)program_size,
                        profile->generation * 2654435761u + nonce)) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_FLASH;
        return false;
    }

    boot_profile_scan_t verify = scan_sector(target_sector);
    flash_store_record_header_t header;
    tdma_boot_profile_t readback;
    if (!scan_has_latest(&verify) ||
        !decode_latest(&verify, &readback, &header) ||
        header.generation != profile->generation ||
        header.sequence != record_sequence ||
        memcmp(&readback, profile, sizeof(readback)) != 0) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_VERIFY;
        return false;
    }
    s_cached = readback;
    s_status.loaded = true;
    s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_NONE;
    s_status.generation = header.generation;
    s_status.sequence = header.sequence;
    s_status.payload_crc32 = header.payload_crc32;
    return true;
}

bool tdma_boot_profile_store_next_generation(uint32_t *out)
{
    if (out == NULL || !s_status.supported) {
        return false;
    }
    boot_profile_scan_t scans[BOOT_PROFILE_SECTOR_COUNT] = {
        scan_sector(0u), scan_sector(1u)};
    uint32_t latest = 0u;
    uint32_t latest_sector = 0u;
    if (scans[0].usable && scans[1].usable &&
        select_latest(scans, &latest_sector)) {
        latest = scans[latest_sector].scan.latest.generation;
    } else {
        /* One sector may be unreadable; the other still carries the newest
         * record we must stay above. */
        for (uint32_t sector = 0u; sector < BOOT_PROFILE_SECTOR_COUNT;
             sector++) {
            if (scans[sector].usable && scan_has_latest(&scans[sector]) &&
                scans[sector].scan.latest.generation > latest) {
                latest = scans[sector].scan.latest.generation;
            }
        }
    }
    if (latest == 0xFFFFFFFFu) {
        return false;
    }
    *out = latest + 1u;
    return true;
}

bool tdma_boot_profile_store_erase(uint32_t nonce)
{
    if (!s_status.supported) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_UNSUPPORTED;
        return false;
    }
    /* The flash completion journal deduplicates by request fingerprint, and the
     * fingerprint includes store_generation. A constant here would make every
     * clear after the first a COMPLETED replay with no physical erase (measured
     * 2026-11-08: CLEARED returned, the record reloaded after reboot). Use the
     * generation being removed; an already-empty store falls back to 1. */
    const uint32_t store_generation =
        (s_status.generation != 0u ? s_status.generation : 1u) * 2654435761u +
        nonce;
    for (uint32_t sector = 0u; sector < BOOT_PROFILE_SECTOR_COUNT; sector++) {
        if (!flash_execute(FLASH_TRANSACTION_OPERATION_ERASE,
                           sector_relative(sector), NULL,
                           BOOT_PROFILE_ERASE_SIZE, store_generation)) {
            s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_FLASH;
            return false;
        }
    }
    /* Never report CLEARED on a deduplicated no-op or on a readable-but-garbage
     * sector: require both sectors to be physically all-0xFF. */
    for (uint32_t sector = 0u; sector < BOOT_PROFILE_SECTOR_COUNT; sector++) {
        if (!drv_flash_is_erased(sector_absolute(sector),
                                 BOOT_PROFILE_ERASE_SIZE)) {
            s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_VERIFY;
            return false;
        }
    }
    const boot_profile_scan_t after0 = scan_sector(0u);
    const boot_profile_scan_t after1 = scan_sector(1u);
    if (!after0.usable || !after1.usable ||
        scan_has_latest(&after0) || scan_has_latest(&after1)) {
        s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_VERIFY;
        return false;
    }
    memset(&s_cached, 0, sizeof(s_cached));
    s_status.loaded = false;
    s_status.generation = 0u;
    s_status.sequence = 0u;
    s_status.payload_crc32 = 0u;
    s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_EMPTY;
    return true;
}

void tdma_boot_profile_store_status(tdma_boot_profile_store_status_t *out)
{
    if (out != NULL) {
        *out = s_status;
    }
}

#else /* !FLASH_DEPLOYMENT_MAP_HAS_TDMA_BOOT_PROFILE */

/* The deployed v1 compatibility map has no calibration partition: no durable
 * boot profile, so the guide ring keeps its multi-port bring-up. */
static tdma_boot_profile_store_status_t s_status = {
    .supported = false,
    .loaded = false,
    .reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_UNSUPPORTED,
};

bool tdma_boot_profile_store_init(void) { return true; }
bool tdma_boot_profile_store_get(tdma_boot_profile_t *out)
{
    (void)out;
    return false;
}
bool tdma_boot_profile_store_commit(const tdma_boot_profile_t *profile,
                                    uint32_t nonce)
{
    (void)profile;
    s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_UNSUPPORTED;
    return false;
}
bool tdma_boot_profile_store_next_generation(uint32_t *out)
{
    (void)out;
    s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_UNSUPPORTED;
    return false;
}
bool tdma_boot_profile_store_erase(uint32_t nonce)
{
    s_status.reject_reason = TDMA_BOOT_PROFILE_STORE_REJECT_UNSUPPORTED;
    return false;
}
void tdma_boot_profile_store_status(tdma_boot_profile_store_status_t *out)
{
    if (out != NULL) {
        *out = s_status;
    }
}

#endif /* FLASH_DEPLOYMENT_MAP_HAS_TDMA_BOOT_PROFILE */
