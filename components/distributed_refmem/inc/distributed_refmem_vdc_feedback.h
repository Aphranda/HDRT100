#ifndef DISTRIBUTED_REFMEM_VDC_FEEDBACK_H
#define DISTRIBUTED_REFMEM_VDC_FEEDBACK_H

#include <stdbool.h>
#include <stdint.h>

#include "distributed_refmem.h"

bool distributed_refmem_get_vdc_feedback_rx(uint32_t source, distributed_refmem_vdc_feedback_rx_snapshot_t *out);
bool distributed_refmem_get_vdc_reference_proof(uint32_t source, distributed_refmem_vdc_feedback_rx_snapshot_t *out);
bool distributed_refmem_get_vdc_feedback_tx(distributed_refmem_vdc_feedback_tx_snapshot_t *out);
bool distributed_refmem_copy_vdc_feedback_rx(uint32_t source, distributed_refmem_vdc_feedback_rx_snapshot_t *out);
bool __attribute__((noinline)) distributed_refmem_copy_vdc_boundary_command( uint32_t source, distributed_refmem_vdc_boundary_view_t *view);
uint32_t distributed_refmem_feedback_prepare_reason(uint32_t *counts, uint32_t count);

#endif /* DISTRIBUTED_REFMEM_VDC_FEEDBACK_H */
