#ifndef VDC_TDMA_PAYLOAD_H
#define VDC_TDMA_PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_service.h"
#include "vdc_domain.h"

#define VDC_TDMA_PAYLOAD_MAGIC 0x54434456u
#define VDC_TDMA_PAYLOAD_VERSION 1u
#define VDC_TDMA_PAYLOAD_FRAME_SIZE 216u

typedef enum {
    VDC_TDMA_PAYLOAD_OK = 0u,
    VDC_TDMA_PAYLOAD_BAD_ARGUMENT = 1u,
    VDC_TDMA_PAYLOAD_BAD_WINDOW = 2u,
    VDC_TDMA_PAYLOAD_BAD_PAYLOAD = 3u,
    VDC_TDMA_PAYLOAD_BAD_FRAME = 4u,
    VDC_TDMA_PAYLOAD_CRC_MISMATCH = 5u,
    VDC_TDMA_PAYLOAD_TDMA_MISMATCH = 6u,
    VDC_TDMA_PAYLOAD_GATE_REJECTED = 7u,
} vdc_tdma_payload_result_t;

typedef struct {
    vdc_tdma_payload_result_t result;
    uint32_t payload_class;
    uint32_t frame_crc32;
    uint32_t payload_crc32;
    vdc_gate_result_t gate;
} vdc_tdma_payload_status_t;

bool vdc_tdma_payload_register(tdma_service_service_t *service);
bool vdc_tdma_payload_build_frame(
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_tdma_window_plan_t *plan,
    uint32_t payload_class,
    uint32_t frame_seq,
    const vdc_tdma_timestamp_evidence_t *timestamp,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size,
    vdc_tdma_frame_envelope_t *envelope,
    vdc_tdma_payload_status_t *status);
bool vdc_tdma_payload_parse_frame(
    const vdc_tdma_schedule_profile_t *schedule,
    const tdma_service_snapshot_t *tdma,
    const uint8_t *frame,
    size_t frame_size,
    bool require_dpll_eligible,
    vdc_tdma_frame_envelope_t *envelope,
    vdc_tdma_payload_status_t *status);

#endif
