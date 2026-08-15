#ifndef REFMEM_VDC_BRIDGE_H
#define REFMEM_VDC_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_realtime_tdma.h"
#include "vdc_domain.h"

typedef enum {
    REFMEM_VDC_BRIDGE_OK = 0u,
    REFMEM_VDC_BRIDGE_BAD_ARGUMENT = 1u,
    REFMEM_VDC_BRIDGE_BAD_SCHEDULE = 2u,
    REFMEM_VDC_BRIDGE_BAD_TDMA_SNAPSHOT = 3u,
    REFMEM_VDC_BRIDGE_BAD_REFMEM_FRAME = 4u,
    REFMEM_VDC_BRIDGE_UNSUPPORTED_FRAME_TYPE = 5u,
    REFMEM_VDC_BRIDGE_VDC_GATE_REJECTED = 6u,
} refmem_vdc_bridge_result_t;

typedef struct {
    refmem_vdc_bridge_result_t result;
    uint32_t frame_type;
    uint32_t source_slot;
    uint32_t payload_class;
    uint32_t frame_crc32;
    uint32_t payload_crc32;
    vdc_gate_result_t gate;
} refmem_vdc_bridge_status_t;

bool refmem_vdc_bridge_build_data_envelope(
    const vdc_tdma_schedule_profile_t *schedule,
    const refmem_realtime_tdma_snapshot_t *tdma,
    const uint8_t *frame,
    size_t frame_size,
    vdc_tdma_frame_envelope_t *envelope,
    refmem_vdc_bridge_status_t *status);

#endif
