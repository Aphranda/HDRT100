/* Real admission, storage codec and per-node receive boundary tests. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_identity.h"
#include "calibration_path_snapshot.h"
#include "calibration_training_store.h"
#include "refmem_sync.h"
#include "refmem_vector_table.h"
#include "tdma_flight_engine.h"
#include "tdma_transport_frame.h"
#include "vdc_domain.h"
#include "pota_types.h"
#include "../support/calibration_store_stubs.inc"

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    return pota_crc32_update(crc, data, size);
}

uint32_t ota_crc32_compute(const uint8_t *data, size_t size)
{
    return pota_crc32_compute(data, size);
}

void pico_get_unique_board_id_string(char *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    assert(!"Hardware identity must not be queried by this test");
}

_Static_assert(sizeof(refmem_vector_table_t) == 65536u, "RefMem ABI");
_Static_assert(offsetof(refmem_vector_table_t, trigger) == 16384u,
               "Fixed RefMem region must not move with local capacity");
_Static_assert(TDMA_TRANSPORT_SHORT_PACKET_MAX == 292u, "SHORT ABI");
_Static_assert(TDMA_FLIGHT_SHORT_SLOT_COUNT == 8u, "Fixed wire slots");
_Static_assert(CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE == 1072u,
               "Persisted training ABI");

static tdma_ring_calibration_stage_t stage_for(uint32_t count)
{
    tdma_ring_calibration_stage_t stage = {
        .enabled = 1u, .node_count = count,
        .evidence_flags = TDMA_RING_CALIBRATION_REQUIRED_FLAGS,
        .calibration_generation = 61u, .topology_generation = 13u,
        .topology_crc32 = 0x11223344u, .profile_crc32 = 0x55667788u,
        .schedule_crc32 = 0x99AABBCCu,
    };
    for (uint32_t i = 0; i < count; ++i) {
        stage.links[i] = (tdma_ring_calibration_link_t){
            .valid = 1u, .link_index = i, .marker_source_node = i,
            .marker_destination_node = (i + 1u) % count,
            .data_source_node = (i + 1u) % count, .data_destination_node = i,
            .evidence_flags = stage.evidence_flags,
            .calibration_generation = stage.calibration_generation,
            .topology_generation = stage.topology_generation,
            .topology_crc32 = stage.topology_crc32,
            .profile_crc32 = stage.profile_crc32,
            .schedule_crc32 = stage.schedule_crc32,
            .pio_persona = 1u, .clkdiv_q16 = 65536u,
            .clk_sys_hz = 250000000u, .instruction_period_ns = 4u,
            .bit_cycles = 6u, .marker_to_data_cycles = 10u,
            .forward_residence_cycles = 1u, .rx_arm_lead_cycles = 1u,
            .codeword_cycles = 32u, .guard_cycles = 2u,
            .link_budget_cycles = 64u, .loop_delay_cycles = 8u,
            .sample_period_ns = 4u, .link_base_delay_ns = 40u,
            .data_offset_sample_count = 5,
            .marker_phase_delay_cycles = 10u, .sck_phase_delay_cycles = 10u,
            .data_phase_delay_cycles = 15u,
        };
    }
    return stage;
}

static void check_admission(void)
{
    tdma_ring_profile_t ring;
    tdma_profile_result_t result;
    assert(tdma_ring_profile_default(&ring, PROJECT_NODE_CAPACITY - 1u,
                                    0u, PROJECT_NODE_CAPACITY));
    const tdma_ring_profile_t original = ring;
    assert(!tdma_ring_profile_default(&ring, 0u, 0u,
                                     PROJECT_NODE_CAPACITY + 1u));
    assert(memcmp(&ring, &original, sizeof(ring)) == 0);
    ring.node_count = PROJECT_NODE_CAPACITY + 1u;
    ring.profile_crc32 = tdma_ring_profile_crc32(&ring);
    assert(!tdma_ring_profile_validate(&ring, &result));
    assert(result == TDMA_PROFILE_BAD_TOPOLOGY);
    assert(board_identity_set_no(PROJECT_NODE_CAPACITY));
    assert(!board_identity_set_no(PROJECT_NODE_CAPACITY + 1u));
    assert(board_identity_get_no() == PROJECT_NODE_CAPACITY);

    tdma_ring_runtime_config_t config = {
        .enabled = 1u, .node_count = PROJECT_NODE_CAPACITY,
        .local_slot_id = PROJECT_NODE_CAPACITY - 1u, .reference_slot_id = 0u,
        .up_group_id = 1u, .down_group_id = 2u,
        .flags = TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN,
        .ring_profile_crc32 = 1u, .schedule_crc32 = 2u,
        .operating_profile_crc32 = 3u, .baud_hz = 10000000u,
        .cycle_period_ns = 1000u, .feedback_timeout_ns = 10000u,
        .tx_dma_channel_id = 5u, .rx_dma_channel_id = 4u,
    };
    tdma_ring_runtime_reason_t reason;
    assert(tdma_ring_runtime_validate_config(&config, &reason));
    config.node_count++;
    assert(!tdma_ring_runtime_validate_config(&config, &reason));
    assert(reason == TDMA_RING_RUNTIME_REASON_BAD_CONFIG);
    config.node_count--;
    config.local_slot_id = PROJECT_NODE_CAPACITY;
    assert(!tdma_ring_runtime_validate_config(&config, &reason));
    for (uint32_t count = 2u; count <= TDMA_RING_CALIBRATION_LINK_MAX; ++count) {
        tdma_ring_calibration_stage_t stage = stage_for(count);
        assert(tdma_ring_runtime_validate_calibration_stage(&stage, count, &reason)
               == (count <= PROJECT_NODE_CAPACITY));
    }
}

static void check_receive(void)
{
    struct {
        uint32_t before;
        refmem_sync_context_t context;
        uint32_t after;
    } guarded = {.before = 0x12345678u, .after = 0x87654321u};
    assert(refmem_sync_init(&guarded.context, 0u, 1u, 1u));
    assert(!refmem_sync_init(&guarded.context, PROJECT_NODE_CAPACITY, 1u, 1u));
    refmem_sync_context_t original = guarded.context;
    uint8_t packet[128];
    size_t size = 0u;
    refmem_sync_frame_header_t header;
    refmem_sync_rx_snapshot_t received;
    for (uint32_t source = PROJECT_NODE_CAPACITY - 1u; source < 8u; ++source) {
        assert(refmem_sync_frame_header_init(&header, REFMEM_SYNC_FRAME_HELLO,
            0u, (uint8_t)source, 1u, 1u, 1u, source + 1u, 0u, 0u, NULL, 0u));
        assert(refmem_sync_frame_encode(&header, NULL, 0u, packet, sizeof(packet), &size));
        assert(refmem_sync_receive_frame(&guarded.context, packet, size, &received)
               == (source < PROJECT_NODE_CAPACITY ? REFMEM_SYNC_RX_ACCEPTED
                                                  : REFMEM_SYNC_RX_SOURCE_SLOT_INVALID));
        if (source < PROJECT_NODE_CAPACITY) original = guarded.context;
        else assert(memcmp(guarded.context.peer, original.peer,
                           sizeof(original.peer)) == 0);
        assert(guarded.before == 0x12345678u && guarded.after == 0x87654321u);
    }
    assert(refmem_sync_get_peer(&guarded.context, PROJECT_NODE_CAPACITY) == NULL);
    refmem_sync_vdc_context_t vdc;
    assert(refmem_sync_vdc_init(&vdc, PROJECT_NODE_CAPACITY - 1u, 1u, 1u));
    assert(!refmem_sync_vdc_init(&vdc, PROJECT_NODE_CAPACITY, 1u, 1u));
    assert(refmem_sync_vdc_get_command(&vdc, PROJECT_NODE_CAPACITY) == NULL);
}

static uint32_t check_vdc(uint32_t count)
{
    vdc_tdma_schedule_profile_t schedule;
    assert(vdc_domain_default_schedule_for_topology(&schedule, 0u, 0u, count));
    vdc_tdma_schedule_profile_t original = schedule;
    assert(!vdc_domain_default_schedule_for_topology(&schedule, 0u, 0u,
                                                    PROJECT_NODE_CAPACITY + 1u));
    assert(memcmp(&schedule, &original, sizeof(schedule)) == 0);
    vdc_path_delay_table_t table = {
        .valid = 1u, .version = VDC_DOMAIN_PATH_DELAY_TABLE_VERSION,
        .update_seq = 2u, .entry_count = count,
        .schedule_crc32 = schedule.schedule_crc32,
        .calibration_generation = 1u, .topology_generation = 1u,
        .freshness_us = 1u,
        .flags = VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY |
                 VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED,
    };
    for (uint32_t i = 0; i < count; ++i) {
        table.entries[i] = (vdc_path_delay_entry_t){
            .valid = 1u, .source_slot_id = i,
            .reference_slot_id = (i + 1u) % count,
            .direction = VDC_PATH_DELAY_DIRECTION_TDMA_DATA_REVERSE,
            .delay_ns = 80u, .jitter_ns = 1u, .cal_crc32 = 42u,
            .freshness_us = 1u, .writer = i, .update_seq = 2u,
        };
    }
    assert(vdc_domain_load_observation_path_matrix(&table, count));
    table.table_crc32 = vdc_domain_path_delay_table_crc32(&table);
    assert(vdc_domain_path_delay_table_validate_provisional(&table));
    vdc_path_delay_entry_t entry;
    assert(vdc_domain_observation_path_delay_lookup(&table, count - 1u, count - 1u, &entry));
    assert(entry.delay_ns == 80u * count);
    assert(!vdc_domain_active_observation_path_delay_lookup(&table, PROJECT_NODE_CAPACITY, 0u, &entry));
    assert(!vdc_domain_load_observation_path_matrix(&table, PROJECT_NODE_CAPACITY + 1u));
    return schedule.schedule_crc32;
}

static uint32_t path_crc(uint32_t count)
{
    calibration_path_link_evidence_t links[CALIBRATION_PATH_MAX_LINKS] = {0};
    for (uint32_t i = 0u; i < count; ++i) {
        links[i] = (calibration_path_link_evidence_t){
            .source_node = i, .destination_node = (i + 1u) % count,
            .profile_crc32 = 11u, .topology_generation = 12u,
            .bias_generation = 13u, .sample_count = 1u, .accepted_count = 1u,
            .measurement = {.reference_accepted = true, .active_eligible = true,
                            .delay_estimate_ns = 80, .corrected_path_sum_ns = 160},
        };
    }
    calibration_path_gate_t gate = {
        .freshness_us = 10u, .calibration_generation = 14u,
        .expected_topology_crc32 = 15u, .expected_schedule_crc32 = 16u,
    };
    calibration_path_snapshot_t snapshot;
    assert(calibration_path_snapshot_build(links, count, 80u * count, 0u, &gate, &snapshot));
    assert(calibration_path_snapshot_validate_candidate(&snapshot));
    assert(!calibration_path_snapshot_build(links, PROJECT_NODE_CAPACITY + 1u,
                                            80u * count, 0u, &gate, &snapshot));
    return snapshot.table_crc32;
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    const uint32_t count = (uint32_t)strtoul(argv[2], NULL, 10);
    uint8_t payload[CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE];
    tdma_ring_calibration_stage_t stage;
    if (strcmp(argv[1], "read") == 0) {
        FILE *file = fopen(argv[3], "rb");
        assert(file != NULL && fread(payload, sizeof(payload), 1u, file) == 1u);
        assert(fgetc(file) == EOF && fclose(file) == 0);
        assert(calibration_training_store_decode_payload(payload, sizeof(payload), &stage)
               == (count <= PROJECT_NODE_CAPACITY));
        return 0;
    }
    assert(count >= 2u && count <= PROJECT_NODE_CAPACITY);
    check_admission();
    check_receive();
    const uint32_t schedule_crc = check_vdc(count);
    stage = stage_for(count);
    assert(calibration_training_store_encode_payload(&stage, payload, sizeof(payload)));
    tdma_ring_calibration_stage_t decoded;
    assert(calibration_training_store_decode_payload(payload, sizeof(payload), &decoded));
    assert(memcmp(&stage, &decoded, sizeof(stage)) == 0);
    FILE *file = fopen(argv[3], "wb");
    assert(file != NULL && fwrite(payload, sizeof(payload), 1u, file) == 1u);
    assert(fclose(file) == 0);
    printf("{\"capacity\":%u,\"nodes\":%lu,\"schedule_crc\":%lu,\"path_crc\":%lu}\n",
           PROJECT_NODE_CAPACITY, (unsigned long)count,
           (unsigned long)schedule_crc, (unsigned long)path_crc(count));
    return 0;
}
