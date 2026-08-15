#include "refmem_vdc_bridge.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "refmem_sync_frame.h"

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    if (data == NULL && length != 0u) {
        return crc;
    }
    for (size_t i = 0u; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? 0xEDB88320u : 0u);
        }
    }
    return crc;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static void make_ready_tdma(refmem_realtime_tdma_snapshot_t *tdma)
{
    (void)memset(tdma, 0, sizeof(*tdma));
    tdma->intent_seq = 4u;
    tdma->completed_seq = 4u;
    tdma->window_epoch = 1u;
    tdma->window_index = 4u;
    tdma->last_result = REFMEM_REALTIME_TDMA_RESULT_FRAME_READY;
    tdma->timestamp_source = REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_SOFTWARE_US;
    tdma->timestamp_resolution_ns = 1000u;
    tdma->timestamp_flags = REFMEM_REALTIME_TDMA_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    tdma->core1_arm_time_ns_lo = 20000u;
    tdma->core1_start_time_ns_lo = 30000u;
    tdma->core1_done_time_ns_lo = 40000u;
    tdma->core1_elapsed_ns = 10000u;
}

static bool make_refmem_frame(uint8_t frame_type,
                              uint8_t source_slot,
                              uint32_t seq32,
                              uint8_t *frame,
                              size_t frame_capacity,
                              size_t *frame_size)
{
    const uint32_t payload[] = {
        0x12345678u,
        0x9ABCDEF0u,
    };
    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(&header,
                                       frame_type,
                                       0u,
                                       source_slot,
                                       0xFFu,
                                       1u,
                                       1u,
                                       seq32,
                                       0u,
                                       0u,
                                       payload,
                                       sizeof(payload))) {
        return false;
    }
    return refmem_sync_frame_encode(&header,
                                    payload,
                                    sizeof(payload),
                                    frame,
                                    frame_capacity,
                                    frame_size);
}

static int test_delta_frame_builds_refmem_data_envelope(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    refmem_realtime_tdma_snapshot_t tdma;
    vdc_tdma_frame_envelope_t envelope;
    refmem_vdc_bridge_status_t status;
    uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
    size_t frame_size = 0u;

    vdc_domain_default_schedule(&schedule, 1u, 0u);
    make_ready_tdma(&tdma);
    failed += expect_bool("make delta",
                          make_refmem_frame(REFMEM_SYNC_FRAME_DELTA,
                                            1u,
                                            4u,
                                            frame,
                                            sizeof(frame),
                                            &frame_size),
                          true);

    failed += expect_bool("bridge delta",
                          refmem_vdc_bridge_build_data_envelope(&schedule,
                                                                 &tdma,
                                                                 frame,
                                                                 frame_size,
                                                                 &envelope,
                                                                 &status),
                          true);
    failed += expect_u32("bridge ok", status.result, REFMEM_VDC_BRIDGE_OK);
    failed += expect_u32("window class",
                         envelope.window_class,
                         VDC_DOMAIN_WINDOW_REFMEM_DATA);
    failed += expect_u32("payload class",
                         envelope.payload_class,
                         VDC_DOMAIN_PAYLOAD_REFMEM_DELTA);
    failed += expect_u32("frame seq follows TDMA",
                         envelope.frame_seq,
                         tdma.completed_seq);
    failed += expect_u32("timestamp seq follows TDMA",
                         envelope.timestamp.sample_seq,
                         tdma.completed_seq);
    failed += expect_u32("timestamp source",
                         envelope.timestamp.timestamp_source,
                         VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US);
    failed += expect_u32("timestamp resolution",
                         envelope.timestamp.timestamp_resolution_ns,
                         1000u);
    failed += expect_u32("timestamp flags",
                         envelope.timestamp.timestamp_flags,
                         VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
    failed += expect_bool("not a dpll sample",
                          vdc_domain_validate_tdma_frame_envelope(&schedule,
                                                                   &envelope,
                                                                   true,
                                                                   &status.gate),
                          false);
    failed += expect_u32("dpll reject",
                         status.gate.reject_code,
                         VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE);
    return failed;
}

static int test_ack_fence_quality_mapping(void)
{
    int failed = 0;
    const uint8_t frame_types[] = {
        REFMEM_SYNC_FRAME_ACK_NACK,
        REFMEM_SYNC_FRAME_FENCE,
        REFMEM_SYNC_FRAME_QUALITY,
    };
    vdc_tdma_schedule_profile_t schedule;
    refmem_realtime_tdma_snapshot_t tdma;

    vdc_domain_default_schedule(&schedule, 2u, 0u);
    make_ready_tdma(&tdma);
    for (uint32_t i = 0u; i < sizeof(frame_types); i++) {
        uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
        size_t frame_size = 0u;
        vdc_tdma_frame_envelope_t envelope;
        refmem_vdc_bridge_status_t status;

        failed += expect_bool("make control frame",
                              make_refmem_frame(frame_types[i],
                                                2u,
                                                i + 1u,
                                                frame,
                                                sizeof(frame),
                                                &frame_size),
                              true);
        failed += expect_bool("bridge control frame",
                              refmem_vdc_bridge_build_data_envelope(&schedule,
                                                                     &tdma,
                                                                     frame,
                                                                     frame_size,
                                                                     &envelope,
                                                                     &status),
                              true);
        failed += expect_u32("control payload class",
                             envelope.payload_class,
                             VDC_DOMAIN_PAYLOAD_ACK_NACK_FENCE_QUALITY);
        failed += expect_u32("control frame seq follows TDMA",
                             envelope.frame_seq,
                             tdma.completed_seq);
    }
    return failed;
}

static int test_off_window_diagnostic_frame_is_gate_rejected(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    refmem_realtime_tdma_snapshot_t tdma;
    vdc_tdma_frame_envelope_t envelope;
    refmem_vdc_bridge_status_t status;
    uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
    size_t frame_size = 0u;

    vdc_domain_default_schedule(&schedule, 1u, 0u);
    make_ready_tdma(&tdma);
    tdma.core1_arm_time_ns_lo = 830000u;
    tdma.core1_start_time_ns_lo = 840000u;
    tdma.core1_done_time_ns_lo = 850000u;
    failed += expect_bool("make off-window delta",
                          make_refmem_frame(REFMEM_SYNC_FRAME_DELTA,
                                            1u,
                                            3u,
                                            frame,
                                            sizeof(frame),
                                            &frame_size),
                          true);

    failed += expect_bool("bridge rejects off-window diagnostic",
                          refmem_vdc_bridge_build_data_envelope(&schedule,
                                                                 &tdma,
                                                                 frame,
                                                                 frame_size,
                                                                 &envelope,
                                                                 &status),
                          false);
    failed += expect_u32("off-window result",
                         status.result,
                         REFMEM_VDC_BRIDGE_VDC_GATE_REJECTED);
    failed += expect_u32("off-window gate",
                         status.gate.reject_code,
                         VDC_DOMAIN_GATE_BAD_FRAME);
    failed += expect_u32("off-window frame seq is TDMA seq",
                         envelope.frame_seq,
                         tdma.completed_seq);
    failed += expect_u32("off-window timestamp flags",
                         envelope.timestamp.timestamp_flags,
                         VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
    return failed;
}

static int test_unsupported_frame_type_is_rejected(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    refmem_realtime_tdma_snapshot_t tdma;
    vdc_tdma_frame_envelope_t envelope;
    refmem_vdc_bridge_status_t status;
    uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
    size_t frame_size = 0u;

    vdc_domain_default_schedule(&schedule, 1u, 0u);
    make_ready_tdma(&tdma);
    failed += expect_bool("make hello",
                          make_refmem_frame(REFMEM_SYNC_FRAME_HELLO,
                                            1u,
                                            9u,
                                            frame,
                                            sizeof(frame),
                                            &frame_size),
                          true);
    failed += expect_bool("bridge rejects hello",
                          refmem_vdc_bridge_build_data_envelope(&schedule,
                                                                 &tdma,
                                                                 frame,
                                                                 frame_size,
                                                                 &envelope,
                                                                 &status),
                          false);
    failed += expect_u32("unsupported result",
                         status.result,
                         REFMEM_VDC_BRIDGE_UNSUPPORTED_FRAME_TYPE);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_delta_frame_builds_refmem_data_envelope();
    failed += test_ack_fence_quality_mapping();
    failed += test_off_window_diagnostic_frame_is_gate_rejected();
    failed += test_unsupported_frame_type_is_rejected();
    if (failed != 0) {
        (void)printf("refmem_vdc_bridge tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("refmem_vdc_bridge tests passed\n");
    return 0;
}
