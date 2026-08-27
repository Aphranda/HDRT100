#include "calibration_training_store.h"

#include <string.h>

#include "drv_flash.h"
#include "flash_deployment_map.h"
#include "flash_store_nvs.h"
#include "flash_transaction.h"

#define CALIBRATION_TRAINING_STORE_SECTOR_COUNT 2u
#define CALIBRATION_TRAINING_STORE_RECORD_CAPACITY 1280u
#define CALIBRATION_TRAINING_STORE_COMMIT_OFFSET 36u

enum {
    PAYLOAD_HEADER_WORDS = 4u,
    STAGE_WORDS = 8u,
    LINK_WORDS = 32u,
};

_Static_assert(CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE ==
                   4u * (PAYLOAD_HEADER_WORDS + STAGE_WORDS +
                         TDMA_RING_CALIBRATION_LINK_MAX * LINK_WORDS),
               "Calibration training payload size drifted");
_Static_assert(CALIBRATION_TRAINING_STORE_RECORD_CAPACITY <=
                   FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
               "Calibration record must fit one erase sector");
_Static_assert(FLASH_DEPLOYMENT_MAP_CALIBRATION_STORE_SIZE ==
                   CALIBRATION_TRAINING_STORE_SECTOR_COUNT *
                       FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
               "Calibration store requires exactly two erase sectors");

typedef struct {
    bool usable;
    uint32_t sector;
    flash_store_nvs_result_t result;
    flash_store_nvs_scan_t scan;
} calibration_training_sector_scan_t;

static tdma_ring_calibration_stage_t s_persisted_stage;
static calibration_training_store_status_t s_status;
static uint8_t s_payload[CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE];
static uint8_t s_scan_payload[CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE];
static uint8_t s_program[CALIBRATION_TRAINING_STORE_RECORD_CAPACITY];
static uint8_t s_commit_page[FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE];
static uint32_t s_provider_generation;
static uint32_t s_provider_refs;

static void write_le32(uint8_t **cursor, uint32_t value)
{
    (*cursor)[0] = (uint8_t)(value & 0xFFu);
    (*cursor)[1] = (uint8_t)((value >> 8u) & 0xFFu);
    (*cursor)[2] = (uint8_t)((value >> 16u) & 0xFFu);
    (*cursor)[3] = (uint8_t)((value >> 24u) & 0xFFu);
    *cursor += 4u;
}

static uint32_t read_le32(const uint8_t **cursor)
{
    const uint32_t value = ((uint32_t)(*cursor)[0]) |
                           ((uint32_t)(*cursor)[1] << 8u) |
                           ((uint32_t)(*cursor)[2] << 16u) |
                           ((uint32_t)(*cursor)[3] << 24u);
    *cursor += 4u;
    return value;
}

static void encode_link(uint8_t **cursor,
                        const tdma_ring_calibration_link_t *link)
{
#define ENCODE_LINK_FIELD(field) write_le32(cursor, (uint32_t)link->field)
    ENCODE_LINK_FIELD(valid);
    ENCODE_LINK_FIELD(link_index);
    ENCODE_LINK_FIELD(marker_source_node);
    ENCODE_LINK_FIELD(marker_destination_node);
    ENCODE_LINK_FIELD(data_source_node);
    ENCODE_LINK_FIELD(data_destination_node);
    ENCODE_LINK_FIELD(evidence_flags);
    ENCODE_LINK_FIELD(calibration_generation);
    ENCODE_LINK_FIELD(topology_generation);
    ENCODE_LINK_FIELD(topology_crc32);
    ENCODE_LINK_FIELD(profile_crc32);
    ENCODE_LINK_FIELD(schedule_crc32);
    ENCODE_LINK_FIELD(pio_persona);
    ENCODE_LINK_FIELD(clkdiv_q16);
    ENCODE_LINK_FIELD(clk_sys_hz);
    ENCODE_LINK_FIELD(instruction_period_ns);
    ENCODE_LINK_FIELD(bit_cycles);
    ENCODE_LINK_FIELD(marker_to_data_cycles);
    ENCODE_LINK_FIELD(forward_residence_cycles);
    ENCODE_LINK_FIELD(rx_arm_lead_cycles);
    ENCODE_LINK_FIELD(codeword_cycles);
    ENCODE_LINK_FIELD(guard_cycles);
    ENCODE_LINK_FIELD(link_budget_cycles);
    ENCODE_LINK_FIELD(loop_delay_cycles);
    ENCODE_LINK_FIELD(marker_offset_sample_count);
    ENCODE_LINK_FIELD(sck_offset_sample_count);
    ENCODE_LINK_FIELD(data_offset_sample_count);
    ENCODE_LINK_FIELD(sample_period_ns);
    ENCODE_LINK_FIELD(link_base_delay_ns);
    ENCODE_LINK_FIELD(marker_phase_delay_cycles);
    ENCODE_LINK_FIELD(sck_phase_delay_cycles);
    ENCODE_LINK_FIELD(data_phase_delay_cycles);
#undef ENCODE_LINK_FIELD
}

static void decode_link(const uint8_t **cursor,
                        tdma_ring_calibration_link_t *link)
{
#define DECODE_LINK_FIELD(field) link->field = read_le32(cursor)
    DECODE_LINK_FIELD(valid);
    DECODE_LINK_FIELD(link_index);
    DECODE_LINK_FIELD(marker_source_node);
    DECODE_LINK_FIELD(marker_destination_node);
    DECODE_LINK_FIELD(data_source_node);
    DECODE_LINK_FIELD(data_destination_node);
    DECODE_LINK_FIELD(evidence_flags);
    DECODE_LINK_FIELD(calibration_generation);
    DECODE_LINK_FIELD(topology_generation);
    DECODE_LINK_FIELD(topology_crc32);
    DECODE_LINK_FIELD(profile_crc32);
    DECODE_LINK_FIELD(schedule_crc32);
    DECODE_LINK_FIELD(pio_persona);
    DECODE_LINK_FIELD(clkdiv_q16);
    DECODE_LINK_FIELD(clk_sys_hz);
    DECODE_LINK_FIELD(instruction_period_ns);
    DECODE_LINK_FIELD(bit_cycles);
    DECODE_LINK_FIELD(marker_to_data_cycles);
    DECODE_LINK_FIELD(forward_residence_cycles);
    DECODE_LINK_FIELD(rx_arm_lead_cycles);
    DECODE_LINK_FIELD(codeword_cycles);
    DECODE_LINK_FIELD(guard_cycles);
    DECODE_LINK_FIELD(link_budget_cycles);
    DECODE_LINK_FIELD(loop_delay_cycles);
    link->marker_offset_sample_count = (int32_t)read_le32(cursor);
    link->sck_offset_sample_count = (int32_t)read_le32(cursor);
    link->data_offset_sample_count = (int32_t)read_le32(cursor);
    DECODE_LINK_FIELD(sample_period_ns);
    DECODE_LINK_FIELD(link_base_delay_ns);
    DECODE_LINK_FIELD(marker_phase_delay_cycles);
    DECODE_LINK_FIELD(sck_phase_delay_cycles);
    DECODE_LINK_FIELD(data_phase_delay_cycles);
#undef DECODE_LINK_FIELD
}

bool calibration_training_store_encode_payload(
    const tdma_ring_calibration_stage_t *stage,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (stage == NULL || payload == NULL ||
        payload_capacity < CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE ||
        !tdma_ring_runtime_validate_calibration_stage(
            stage, stage->node_count, NULL)) {
        return false;
    }
    memset(payload, 0, CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE);
    uint8_t *cursor = payload;
    write_le32(&cursor, CALIBRATION_TRAINING_STORE_PAYLOAD_MAGIC);
    write_le32(&cursor, CALIBRATION_TRAINING_STORE_PAYLOAD_VERSION);
    write_le32(&cursor, CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE);
    write_le32(&cursor, TDMA_RING_RUNTIME_VERSION);
    write_le32(&cursor, stage->enabled);
    write_le32(&cursor, stage->node_count);
    write_le32(&cursor, stage->evidence_flags);
    write_le32(&cursor, stage->calibration_generation);
    write_le32(&cursor, stage->topology_generation);
    write_le32(&cursor, stage->topology_crc32);
    write_le32(&cursor, stage->profile_crc32);
    write_le32(&cursor, stage->schedule_crc32);
    for (uint32_t link = 0u;
         link < TDMA_RING_CALIBRATION_LINK_MAX; link++) {
        encode_link(&cursor, &stage->links[link]);
    }
    return (size_t)(cursor - payload) ==
           CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE;
}

bool calibration_training_store_decode_payload(
    const uint8_t *payload,
    size_t payload_size,
    tdma_ring_calibration_stage_t *stage)
{
    if (payload == NULL || stage == NULL ||
        payload_size != CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE) {
        return false;
    }
    memset(stage, 0, sizeof(*stage));
    const uint8_t *cursor = payload;
    if (read_le32(&cursor) != CALIBRATION_TRAINING_STORE_PAYLOAD_MAGIC ||
        read_le32(&cursor) != CALIBRATION_TRAINING_STORE_PAYLOAD_VERSION ||
        read_le32(&cursor) != CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE ||
        read_le32(&cursor) != TDMA_RING_RUNTIME_VERSION) {
        return false;
    }
    stage->enabled = read_le32(&cursor);
    stage->node_count = read_le32(&cursor);
    stage->evidence_flags = read_le32(&cursor);
    stage->calibration_generation = read_le32(&cursor);
    stage->topology_generation = read_le32(&cursor);
    stage->topology_crc32 = read_le32(&cursor);
    stage->profile_crc32 = read_le32(&cursor);
    stage->schedule_crc32 = read_le32(&cursor);
    for (uint32_t link = 0u;
         link < TDMA_RING_CALIBRATION_LINK_MAX; link++) {
        decode_link(&cursor, &stage->links[link]);
    }
    return (size_t)(cursor - payload) == payload_size &&
           tdma_ring_runtime_validate_calibration_stage(
               stage, stage->node_count, NULL);
}

static uint32_t sector_absolute_offset(uint32_t sector)
{
    return FLASH_DEPLOYMENT_MAP_CALIBRATION_STORE_OFFSET +
           sector * FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE;
}

static uint32_t sector_relative_offset(uint32_t sector)
{
    return FLASH_DEPLOYMENT_MAP_CALIBRATION_STORE_RELATIVE_OFFSET +
           sector * FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE;
}

static calibration_training_sector_scan_t scan_sector(uint32_t sector)
{
    calibration_training_sector_scan_t result = {.sector = sector};
    const uint8_t *base = drv_flash_xip_ptr(sector_absolute_offset(sector));
    if (base == NULL) {
        result.result = FLASH_STORE_NVS_BAD_ARGUMENT;
        return result;
    }
    result.result = flash_store_nvs_scan(
        base, FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
        CALIBRATION_TRAINING_STORE_SCHEMA_VERSION,
        CALIBRATION_TRAINING_STORE_OBJECT_TYPE, 0u,
        FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
        s_scan_payload, sizeof(s_scan_payload), &result.scan);
    result.usable = result.result == FLASH_STORE_NVS_OK ||
                    result.result == FLASH_STORE_NVS_EMPTY ||
                    result.result == FLASH_STORE_NVS_CORRUPT;
    return result;
}

static bool scan_has_latest(const calibration_training_sector_scan_t *scan)
{
    return scan != NULL && scan->usable && scan->scan.has_latest;
}

static bool select_latest(calibration_training_sector_scan_t scans[2],
                          uint32_t *latest_sector)
{
    const bool first = scan_has_latest(&scans[0]);
    const bool second = scan_has_latest(&scans[1]);
    if (!first && !second) return false;
    if (!second || (first && !flash_store_record_is_newer(
                                  &scans[1].scan.latest,
                                  &scans[0].scan.latest))) {
        *latest_sector = 0u;
    } else {
        *latest_sector = 1u;
    }
    return true;
}

static bool decode_latest(const calibration_training_sector_scan_t *scan,
                          tdma_ring_calibration_stage_t *stage,
                          flash_store_record_header_t *header)
{
    if (!scan_has_latest(scan)) return false;
    const uint8_t *record = drv_flash_xip_ptr(
        sector_absolute_offset(scan->sector) +
        (uint32_t)scan->scan.latest_offset);
    if (record == NULL || flash_store_record_decode(
            record,
            FLASH_STORE_RECORD_HEADER_SIZE +
                scan->scan.latest.payload_length,
            CALIBRATION_TRAINING_STORE_SCHEMA_VERSION,
            CALIBRATION_TRAINING_STORE_OBJECT_TYPE, 0u,
            s_payload, sizeof(s_payload), header) != FLASH_STORE_RECORD_OK) {
        return false;
    }
    return calibration_training_store_decode_payload(
        s_payload, header->payload_length, stage);
}

bool calibration_training_store_init(void)
{
    memset(&s_persisted_stage, 0, sizeof(s_persisted_stage));
    memset(&s_status, 0, sizeof(s_status));
    s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_EMPTY;
    s_provider_generation = 0u;
    s_provider_refs = 0u;

    calibration_training_sector_scan_t scans[2] = {
        scan_sector(0u), scan_sector(1u)};
    uint32_t latest_sector = 0u;
    if (!scans[0].usable || !scans[1].usable) {
        s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_RECORD;
        return true;
    }
    if (!select_latest(scans, &latest_sector)) return true;

    flash_store_record_header_t header;
    if (!decode_latest(&scans[latest_sector],
                       &s_persisted_stage, &header)) {
        s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_PAYLOAD;
        return true;
    }
    s_status.persisted_valid = 1u;
    s_status.restore_pending = 1u;
    s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_CONTEXT;
    s_status.record_generation = header.generation;
    s_status.record_sequence = header.sequence;
    s_status.payload_crc32 = header.payload_crc32;
    s_status.calibration_generation =
        s_persisted_stage.calibration_generation;
    s_status.topology_generation = s_persisted_stage.topology_generation;
    s_status.topology_crc32 = s_persisted_stage.topology_crc32;
    s_status.profile_crc32 = s_persisted_stage.profile_crc32;
    s_status.schedule_crc32 = s_persisted_stage.schedule_crc32;
    return true;
}

bool calibration_training_store_get_stage(
    tdma_ring_calibration_stage_t *stage)
{
    if (stage == NULL || s_status.persisted_valid == 0u) return false;
    *stage = s_persisted_stage;
    return true;
}

static uint32_t next_provider_generation(void)
{
    s_provider_generation++;
    if (s_provider_generation == 0u) s_provider_generation = 1u;
    return s_provider_generation;
}

static bool provider_retain(void *context)
{
    uint32_t *refs = context;
    if (refs == NULL || *refs == UINT32_MAX) return false;
    (*refs)++;
    return true;
}

static void provider_release(void *context)
{
    uint32_t *refs = context;
    if (refs != NULL && *refs != 0u) (*refs)--;
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
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_CALIBRATION,
        .partition_id =
            FLASH_DEPLOYMENT_MAP_CALIBRATION_STORE_PARTITION_ID,
        .operation = operation,
        .relative_offset = relative_offset,
        .length = length,
        .data = data,
        .provider_generation = generation,
        .store_generation = store_generation,
        .buffer_lease = operation == FLASH_TRANSACTION_OPERATION_PROGRAM
                            ? &lease : NULL,
        .completion_lease = flash_transaction_ao_get_completion_lease(),
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
}

static bool program_record(uint32_t sector,
                           uint32_t append_offset,
                           uint32_t program_size,
                           uint32_t store_generation)
{
    if (program_size == 0u ||
        (program_size % FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE) != 0u) {
        return false;
    }
    memcpy(s_commit_page, s_program, sizeof(s_commit_page));
    memset(&s_program[CALIBRATION_TRAINING_STORE_COMMIT_OFFSET],
           0xFF, sizeof(uint32_t));
    const uint32_t base = sector_relative_offset(sector) + append_offset;
    for (uint32_t offset = 0u; offset < program_size;
         offset += FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE) {
        if (!flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM,
                           base + offset, &s_program[offset],
                           FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
                           store_generation)) {
            return false;
        }
    }
    /* The commit marker shares the first program page. Re-programming that
     * page changes only erased 1 bits to 0 after every body page verified. */
    return flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM,
                         base, s_commit_page,
                         FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
                         store_generation);
}

bool calibration_training_store_commit(
    const tdma_ring_calibration_stage_t *stage)
{
    if (!calibration_training_store_encode_payload(
            stage, s_payload, sizeof(s_payload))) {
        s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_STAGE;
        return false;
    }

    calibration_training_sector_scan_t scans[2] = {
        scan_sector(0u), scan_sector(1u)};
    if (!scans[0].usable || !scans[1].usable) {
        s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_RECORD;
        return false;
    }
    uint32_t latest_sector = 0u;
    const bool has_latest = select_latest(scans, &latest_sector);
    if (has_latest && (int32_t)(stage->calibration_generation -
                                scans[latest_sector].scan.latest.generation) <
                          0) {
        s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_REPLAY;
        return false;
    }
    uint32_t record_sequence = 1u;
    if (has_latest && stage->calibration_generation ==
                          scans[latest_sector].scan.latest.generation) {
        record_sequence = scans[latest_sector].scan.latest.sequence + 1u;
        if (record_sequence == 0u) record_sequence = 1u;
    }
    const size_t required_span = flash_store_nvs_record_span(
        FLASH_STORE_RECORD_HEADER_SIZE + sizeof(s_payload),
        FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE);
    uint32_t target_sector = has_latest ? latest_sector : 0u;
    size_t append_offset = has_latest
                               ? scans[latest_sector].scan.append_offset : 0u;
    const bool rotate = has_latest &&
        (scans[latest_sector].scan.saw_torn_tail ||
         scans[latest_sector].scan.needs_rotation ||
         required_span > FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE -
                             append_offset);
    if (rotate) {
        target_sector = latest_sector ^ 1u;
        append_offset = 0u;
    }
    const bool target_erased = drv_flash_is_erased(
        sector_absolute_offset(target_sector),
        FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE);
    if ((rotate || (!has_latest && !target_erased)) &&
        !flash_execute(FLASH_TRANSACTION_OPERATION_ERASE,
                       sector_relative_offset(target_sector), NULL,
                       FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
                       stage->calibration_generation)) {
        s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_FLASH;
        return false;
    }
    size_t program_size = 0u;
    if (flash_store_nvs_plan_append(
            CALIBRATION_TRAINING_STORE_SCHEMA_VERSION,
            CALIBRATION_TRAINING_STORE_OBJECT_TYPE,
            stage->calibration_generation, record_sequence, 0u,
            s_payload, sizeof(s_payload), append_offset,
            FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
            FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
            s_program, sizeof(s_program), &program_size) !=
            FLASH_STORE_NVS_OK ||
        !program_record(target_sector, (uint32_t)append_offset,
                        (uint32_t)program_size,
                        stage->calibration_generation)) {
        s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_FLASH;
        return false;
    }

    calibration_training_sector_scan_t verify = scan_sector(target_sector);
    flash_store_record_header_t header;
    tdma_ring_calibration_stage_t readback;
    if (!scan_has_latest(&verify) ||
        !decode_latest(&verify, &readback, &header) ||
        header.generation != stage->calibration_generation ||
        header.sequence != record_sequence ||
        memcmp(&readback, stage, sizeof(readback)) != 0) {
        s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_RECORD;
        return false;
    }
    s_persisted_stage = readback;
    s_status.persisted_valid = 1u;
    s_status.loaded = 1u;
    s_status.restore_pending = 0u;
    s_status.reject_reason = CALIBRATION_TRAINING_STORE_REJECT_NONE;
    s_status.record_generation = header.generation;
    s_status.record_sequence = header.sequence;
    s_status.payload_crc32 = header.payload_crc32;
    s_status.calibration_generation = readback.calibration_generation;
    s_status.topology_generation = readback.topology_generation;
    s_status.topology_crc32 = readback.topology_crc32;
    s_status.profile_crc32 = readback.profile_crc32;
    s_status.schedule_crc32 = readback.schedule_crc32;
    return true;
}

void calibration_training_store_set_loaded(bool loaded,
                                           uint32_t reject_reason)
{
    s_status.loaded = loaded ? 1u : 0u;
    s_status.restore_pending =
        !loaded && s_status.persisted_valid != 0u ? 1u : 0u;
    s_status.reject_reason = loaded
        ? CALIBRATION_TRAINING_STORE_REJECT_NONE : reject_reason;
}

void calibration_training_store_get_status(
    calibration_training_store_status_t *status)
{
    if (status != NULL) *status = s_status;
}
