#include "../support/calibration_store_stubs.inc"

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
        link->origin_capture_offset_sample_count = (int32_t)i - 2;
        link->origin_capture_phase_delay_cycles = i + 8u;
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
    assert(decoded.links[0].origin_capture_offset_sample_count == -2);
    assert(decoded.links[7].origin_capture_phase_delay_cycles == 15u);

    /* Version 1 has exactly the old 32-word links. Preserve its original
     * DATA behavior; do not invent independent calibration evidence. */
    uint8_t legacy[CALIBRATION_TRAINING_STORE_PAYLOAD_V1_SIZE];
    memcpy(legacy, payload, 48u);
    legacy[4] = 1u;
    legacy[8] = (uint8_t)sizeof(legacy);
    legacy[9] = (uint8_t)(sizeof(legacy) >> 8u);
    for (uint32_t i = 0u; i < TDMA_RING_CALIBRATION_LINK_MAX; ++i)
        memcpy(legacy + 48u + i * 128u, payload + 48u + i * 136u, 128u);
    assert(calibration_training_store_decode_payload(legacy, sizeof(legacy), &decoded));
    assert(decoded.links[0].data_phase_delay_cycles == 15u);
    assert(decoded.links[0].origin_capture_offset_sample_count == 0);
    assert(decoded.links[0].origin_capture_phase_delay_cycles == 0u);
    legacy[4] = 2u;
    assert(!calibration_training_store_decode_payload(legacy, sizeof(legacy), &decoded));

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
