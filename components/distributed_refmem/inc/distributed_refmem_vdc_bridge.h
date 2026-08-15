#ifndef DISTRIBUTED_REFMEM_VDC_BRIDGE_H
#define DISTRIBUTED_REFMEM_VDC_BRIDGE_H

#include "refmem_vdc_bridge.h"

bool distributed_refmem_build_realtime_tdma_vdc_envelope(
    const vdc_tdma_schedule_profile_t *schedule,
    vdc_tdma_frame_envelope_t *envelope,
    refmem_vdc_bridge_status_t *status);

#endif
