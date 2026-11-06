#include "tdma_seat_mount_store.h"

#include <string.h>

#include "drv_flash.h"
#include "flash_deployment_map.h"
#include "flash_store_nvs.h"
#include "flash_store_record.h"
#include "flash_transaction.h"

#define SEAT_MOUNT_OBJECT_TYPE 0x53454154u
#define SEAT_MOUNT_COMMIT_OFFSET FLASH_STORE_RECORD_COMMIT_OFFSET
#define SEAT_MOUNT_SECTOR_COUNT 2u
#define SEAT_MOUNT_PROGRAM_SIZE 512u
#define SEAT_MOUNT_SCAN_SIZE 64u

typedef struct {
    bool usable;
    uint32_t sector;
    flash_store_nvs_result_t result;
    flash_store_nvs_scan_t scan;
} seat_mount_scan_t;

static uint32_t s_provider_generation;
static uint32_t s_provider_refs;
static uint32_t s_persisted_valid;
static uint32_t s_last_reject;
static uint32_t s_record_generation;
static tdma_seat_mount_table_t s_mount;
static uint8_t s_payload[TDMA_SEAT_MOUNT_PAYLOAD_SIZE];
static uint8_t s_program[SEAT_MOUNT_PROGRAM_SIZE];
static uint8_t s_commit_page[FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE];
static uint8_t s_scan_payload[SEAT_MOUNT_SCAN_SIZE];

static uint32_t next_provider_generation(void)
{
    if (++s_provider_generation == 0u) ++s_provider_generation;
    return s_provider_generation;
}

static bool provider_retain(void *context)
{
    (void)context;
    ++s_provider_refs;
    return true;
}

static void provider_release(void *context)
{
    (void)context;
    if (s_provider_refs != 0u) --s_provider_refs;
}

static bool flash_execute(uint32_t operation, uint32_t relative_offset,
    const uint8_t *data, uint32_t length, uint32_t store_generation)
{
    const uint32_t generation =
        operation == FLASH_TRANSACTION_OPERATION_PROGRAM ? next_provider_generation() : 0u;
    const flash_transaction_buffer_lease_t lease = {.data = data, .length = length,
        .generation = generation, .context = &s_provider_refs,
        .retain = provider_retain, .release = provider_release};
    return flash_transaction_ao_execute_range(
        FLASH_TRANSACTION_REQUESTER_SEAT_MOUNT,
        FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_PARTITION_ID,
        operation, relative_offset, data, length, store_generation, generation,
        operation == FLASH_TRANSACTION_OPERATION_PROGRAM ? &lease : NULL);
}

static uint32_t sector_absolute_offset(uint32_t sector)
{
    return FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_OFFSET +
        sector * FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE;
}

static uint32_t sector_relative_offset(uint32_t sector)
{
    return FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_RELATIVE_OFFSET +
        sector * FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE;
}

static seat_mount_scan_t scan_sector(uint32_t sector)
{
    seat_mount_scan_t result = {.sector = sector};
    const uint8_t *base = drv_flash_xip_ptr(sector_absolute_offset(sector));
    if (base == NULL) {
        result.result = FLASH_STORE_NVS_BAD_ARGUMENT;
        return result;
    }
    result.result = flash_store_nvs_scan(base, FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
        TDMA_SEAT_MOUNT_STORE_SCHEMA_VERSION, SEAT_MOUNT_OBJECT_TYPE, 0u,
        FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE, s_scan_payload, sizeof(s_scan_payload),
        &result.scan);
    result.usable = result.result == FLASH_STORE_NVS_OK ||
        result.result == FLASH_STORE_NVS_EMPTY || result.result == FLASH_STORE_NVS_CORRUPT;
    return result;
}

static bool scan_has_latest(const seat_mount_scan_t *scan)
{
    return scan != NULL && scan->usable && scan->scan.has_latest;
}

static bool select_latest(seat_mount_scan_t scans[SEAT_MOUNT_SECTOR_COUNT],
    uint32_t *latest_sector)
{
    const bool first = scan_has_latest(&scans[0]);
    const bool second = scan_has_latest(&scans[1]);
    if (!first && !second) return false;
    *latest_sector = (!second || (first &&
        !flash_store_record_is_newer(&scans[1].scan.latest, &scans[0].scan.latest))) ? 0u : 1u;
    return true;
}

static bool decode_latest(const seat_mount_scan_t *scan, flash_store_record_header_t *header)
{
    if (!scan_has_latest(scan)) return false;
    const uint8_t *record = drv_flash_xip_ptr(sector_absolute_offset(scan->sector) +
        (uint32_t)scan->scan.latest_offset);
    if (record == NULL || flash_store_record_decode(record,
            FLASH_STORE_RECORD_HEADER_SIZE + scan->scan.latest.payload_length,
            TDMA_SEAT_MOUNT_STORE_SCHEMA_VERSION, SEAT_MOUNT_OBJECT_TYPE, 0u,
            s_payload, sizeof(s_payload), header) != FLASH_STORE_RECORD_OK)
        return false;
    return tdma_seat_mount_decode(s_payload, header->payload_length, &s_mount);
}

bool tdma_seat_mount_store_init(void)
{
    memset(&s_mount, 0, sizeof(s_mount));
    s_persisted_valid = 0u;
    s_record_generation = 0u;
    s_provider_generation = 0u;
    s_provider_refs = 0u;
    s_last_reject = 0u;
    seat_mount_scan_t scans[SEAT_MOUNT_SECTOR_COUNT] = {scan_sector(0u), scan_sector(1u)};
    if (!scans[0].usable || !scans[1].usable) return true;
    uint32_t latest_sector = 0u;
    if (!select_latest(scans, &latest_sector)) return true;
    flash_store_record_header_t header;
    if (!decode_latest(&scans[latest_sector], &header)) return true;
    s_persisted_valid = 1u;
    s_record_generation = header.generation;
    return true;
}

bool tdma_seat_mount_store_get(tdma_seat_mount_table_t *table)
{
    if (table == NULL) return false;
    *table = s_mount;
    return s_persisted_valid != 0u;
}

bool tdma_seat_mount_store_save(const tdma_seat_mount_table_t *table, uint32_t generation)
{
    if (table == NULL || generation == 0u) return false;
    if (!tdma_seat_mount_encode(table, s_payload, sizeof(s_payload))) {
        s_last_reject = 1u; return false; }
    seat_mount_scan_t scans[SEAT_MOUNT_SECTOR_COUNT] = {scan_sector(0u), scan_sector(1u)};
    if (!scans[0].usable || !scans[1].usable) {
        s_last_reject = 2u; return false; }
    uint32_t latest_sector = 0u;
    const bool has_latest = select_latest(scans, &latest_sector);
    if (has_latest && (int32_t)(generation - scans[latest_sector].scan.latest.generation) < 0) {
        s_last_reject = 3u; return false; }
    uint32_t sequence = 1u;
    if (has_latest && generation == scans[latest_sector].scan.latest.generation) {
        sequence = scans[latest_sector].scan.latest.sequence + 1u;
        if (sequence == 0u) sequence = 1u;
    }
    const size_t required = flash_store_nvs_record_span(
        FLASH_STORE_RECORD_HEADER_SIZE + sizeof(s_payload),
        FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE);
    uint32_t target = has_latest ? latest_sector : 0u;
    size_t append_offset = has_latest ? scans[latest_sector].scan.append_offset : 0u;
    const bool rotate = has_latest && (scans[latest_sector].scan.saw_torn_tail ||
        scans[latest_sector].scan.needs_rotation ||
        required > FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE - append_offset);
    if (rotate) {
        target = latest_sector ^ 1u;
        append_offset = 0u;
    }
    const bool erased = drv_flash_is_erased(sector_absolute_offset(target),
        FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE);
    if ((rotate || (!has_latest && !erased)) &&
        !flash_execute(FLASH_TRANSACTION_OPERATION_ERASE, sector_relative_offset(target),
            NULL, FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE, generation)) {
        s_last_reject = 4u; return false; }
    size_t program_size = 0u;
    if (flash_store_nvs_plan_append(TDMA_SEAT_MOUNT_STORE_SCHEMA_VERSION, SEAT_MOUNT_OBJECT_TYPE,
            generation, sequence, 0u, s_payload, sizeof(s_payload), append_offset,
            FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE, FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
            s_program, sizeof(s_program), &program_size) != FLASH_STORE_NVS_OK) {
        s_last_reject = 5u; return false; }
    if (program_size == 0u || (program_size % FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE) != 0u ||
        program_size > sizeof(s_commit_page)) {
        s_last_reject = 6u; return false; }
    memcpy(s_commit_page, s_program, sizeof(s_commit_page));
    memset(&s_program[SEAT_MOUNT_COMMIT_OFFSET], 0xFF, sizeof(uint32_t));
    const uint32_t base = sector_relative_offset(target) + (uint32_t)append_offset;
    for (uint32_t offset = 0u; offset < program_size;
         offset += FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE)
        if (!flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM, base + offset,
                &s_program[offset], FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE, generation)) {
            s_last_reject = 7u;
            return false;
        }
    if (!flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM, base, s_commit_page,
            FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE, generation)) {
        s_last_reject = 8u;
        return false;
    }
    /* Same write-verify as the boot profile store: re-scan the sector, decode
     * the committed record and compare it with what we meant to persist. */
    seat_mount_scan_t verify = scan_sector(target);
    flash_store_record_header_t header;
    if (!decode_latest(&verify, &header) || header.generation != generation ||
        header.sequence != sequence ||
        memcmp(&s_mount, table, sizeof(*table)) != 0) {
        s_last_reject = 9u;
        return false;
    }
    s_mount = *table;
    s_persisted_valid = 1u;
    s_record_generation = generation;
    return true;
}

bool tdma_seat_mount_store_clear(void)
{
    memset(&s_mount, 0, sizeof(s_mount));
    s_persisted_valid = 0u;
    s_record_generation = 0u;
    return true;
}

void tdma_seat_mount_store_status(uint32_t *last_reject, uint32_t *record_generation)
{
    if (last_reject != NULL) *last_reject = s_last_reject;
    if (record_generation != NULL) *record_generation = s_record_generation;
}

void tdma_seat_mount_store_note_reject(uint32_t code)
{
    s_last_reject = code;
}
