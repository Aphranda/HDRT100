#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "calibration_training_store.h"
#include "drv_flash.h"
#include "flash_store_nvs.h"
#include "flash_transaction.h"

const uint8_t *drv_flash_xip_ptr(uint32_t offset)
{
    (void)offset;
    return NULL;
}

bool drv_flash_is_erased(uint32_t offset, size_t length)
{
    (void)offset;
    (void)length;
    return false;
}

flash_store_nvs_result_t flash_store_nvs_scan(
    const uint8_t *sector, size_t sector_size,
    uint32_t expected_schema_version, uint32_t expected_object_type,
    uint32_t known_flags_mask, size_t program_alignment,
    uint8_t *scratch_payload, size_t scratch_capacity,
    flash_store_nvs_scan_t *scan)
{
    (void)sector;
    (void)sector_size;
    (void)expected_schema_version;
    (void)expected_object_type;
    (void)known_flags_mask;
    (void)program_alignment;
    (void)scratch_payload;
    (void)scratch_capacity;
    (void)scan;
    return FLASH_STORE_NVS_EMPTY;
}

bool flash_store_record_is_newer(
    const flash_store_record_header_t *candidate,
    const flash_store_record_header_t *current)
{
    (void)candidate;
    (void)current;
    return false;
}

flash_store_record_result_t flash_store_record_decode(
    const uint8_t *record, size_t record_size,
    uint32_t expected_schema_version, uint32_t expected_object_type,
    uint32_t known_flags_mask, uint8_t *payload, size_t payload_capacity,
    flash_store_record_header_t *header)
{
    (void)record;
    (void)record_size;
    (void)expected_schema_version;
    (void)expected_object_type;
    (void)known_flags_mask;
    (void)payload;
    (void)payload_capacity;
    (void)header;
    return FLASH_STORE_RECORD_BAD_ARGUMENT;
}

size_t flash_store_nvs_record_span(size_t record_size, size_t alignment)
{
    return alignment == 0u ? 0u :
        (record_size + alignment - 1u) / alignment * alignment;
}

flash_store_nvs_result_t flash_store_nvs_plan_append(
    uint32_t schema_version, uint32_t object_type, uint32_t generation,
    uint32_t sequence, uint32_t flags, const uint8_t *payload,
    uint32_t payload_length, size_t append_offset, size_t sector_size,
    size_t program_alignment, uint8_t *program_buffer,
    size_t program_capacity, size_t *program_size)
{
    (void)schema_version;
    (void)object_type;
    (void)generation;
    (void)sequence;
    (void)flags;
    (void)payload;
    (void)payload_length;
    (void)append_offset;
    (void)sector_size;
    (void)program_alignment;
    (void)program_buffer;
    (void)program_capacity;
    (void)program_size;
    return FLASH_STORE_NVS_BAD_ARGUMENT;
}

const flash_transaction_completion_lease_t *
flash_transaction_ao_get_completion_lease(void)
{
    return NULL;
}

bool flash_transaction_ao_execute(
    const flash_transaction_request_t *request,
    flash_transaction_completion_t *completion)
{
    (void)request;
    (void)completion;
    return false;
}

bool tdma_ring_runtime_validate_calibration_stage(
    const tdma_ring_calibration_stage_t *stage,
    uint32_t expected_node_count,
    tdma_ring_runtime_reason_t *reason)
{
    if (reason != NULL) *reason = TDMA_RING_RUNTIME_REASON_NONE;
    return stage != NULL && stage->enabled == 1u &&
           stage->node_count == expected_node_count &&
           expected_node_count >= 2u &&
           expected_node_count <= TDMA_RING_CALIBRATION_LINK_MAX;
}

static tdma_ring_calibration_stage_t make_stage(void)
{
    tdma_ring_calibration_stage_t stage = {
        .enabled = 1u,
        .node_count = TDMA_RING_CALIBRATION_LINK_MAX,
        .evidence_flags = TDMA_RING_CALIBRATION_REQUIRED_FLAGS,
        .calibration_generation = 210u,
        .topology_generation = 3u,
        .topology_crc32 = 3816963506u,
        .profile_crc32 = 1383759744u,
        .schedule_crc32 = 2993488091u,
    };
    for (uint32_t i = 0u; i < stage.node_count; i++) {
        tdma_ring_calibration_link_t *link = &stage.links[i];
        link->valid = 1u;
        link->link_index = i;
        link->marker_source_node = i;
        link->marker_destination_node = (i + 1u) % stage.node_count;
        link->data_source_node = link->marker_destination_node;
        link->data_destination_node = i;
        link->evidence_flags = stage.evidence_flags;
        link->calibration_generation = stage.calibration_generation;
        link->topology_generation = stage.topology_generation;
        link->topology_crc32 = stage.topology_crc32;
        link->profile_crc32 = stage.profile_crc32;
        link->schedule_crc32 = stage.schedule_crc32;
        link->pio_persona = 5u;
        link->clkdiv_q16 = 65536u;
        link->clk_sys_hz = 250000000u;
        link->instruction_period_ns = 4u;
        link->bit_cycles = 25u;
        link->marker_to_data_cycles = 10u;
        link->forward_residence_cycles = 5u;
        link->rx_arm_lead_cycles = 2u;
        link->codeword_cycles = 20u;
        link->guard_cycles = 2u;
        link->link_budget_cycles = 64u;
        link->loop_delay_cycles = 8u;
        link->marker_offset_sample_count = (int32_t)i - 4;
        link->sck_offset_sample_count = 1 - (int32_t)i;
        link->data_offset_sample_count = (int32_t)i + 5;
        link->sample_period_ns = 4u;
        link->link_base_delay_ns = 40u;
        link->marker_phase_delay_cycles = i + 6u;
        link->sck_phase_delay_cycles = 11u - i;
        link->data_phase_delay_cycles = i + 15u;
    }
    return stage;
}

int main(void)
{
    uint8_t payload[CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE];
    tdma_ring_calibration_stage_t source = make_stage();
    tdma_ring_calibration_stage_t decoded;
    assert(calibration_training_store_encode_payload(
        &source, payload, sizeof(payload)));
    assert(calibration_training_store_decode_payload(
        payload, sizeof(payload), &decoded));
    assert(memcmp(&source, &decoded, sizeof(source)) == 0);
    assert(decoded.links[0].marker_offset_sample_count == -4);
    assert(decoded.links[7].sck_offset_sample_count == -6);
    assert(decoded.links[7].data_offset_sample_count == 12);

    uint8_t corrupt[sizeof(payload)];
    memcpy(corrupt, payload, sizeof(corrupt));
    corrupt[0] ^= 1u;
    assert(!calibration_training_store_decode_payload(
        corrupt, sizeof(corrupt), &decoded));
    assert(!calibration_training_store_decode_payload(
        payload, sizeof(payload) - 1u, &decoded));

    puts("calibration training store codec tests passed");
    return 0;
}
