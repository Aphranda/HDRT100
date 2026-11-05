#ifndef REFMEM_BOOTSTRAP_TRANSPORT_H
#define REFMEM_BOOTSTRAP_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_spi_physical_adapter.h"

/* STOP-state low-speed SPI carrier for REFMEM before the TDMA ring exists
 * (CONSOLE-017/035).  This module owns the physical adapter: SCPI may only
 * arm/disarm/query it and never drive REFMEM frames (frame-level SPI
 * commands stay retired, see REFTMEM-TASK-20260815-013).  Frame carrying
 * belongs to the bootstrap relay, not to a command handler. */

typedef enum {
    REFMEM_BOOTSTRAP_ARM_OK = 0u,
    REFMEM_BOOTSTRAP_ARM_NOT_STOPPED = 1u,
    REFMEM_BOOTSTRAP_ARM_RING_READY = 2u,
    REFMEM_BOOTSTRAP_ARM_ADAPTER_ERROR = 3u,
} refmem_bootstrap_arm_result_t;

typedef struct {
    refmem_spi_physical_role_t role;
    uint32_t baud_hz;
    const refmem_spi_physical_pin_config_t *pins;
    /* Both are caller-observed facts, not inferences of this module:
     * system_stopped = model/realtime layers are idle; ring_ready = the TDMA
     * ring link currently carries commands (ring keeps priority). */
    bool system_stopped;
    bool ring_ready;
} refmem_bootstrap_arm_request_t;

refmem_bootstrap_arm_result_t refmem_bootstrap_transport_arm(
    const refmem_bootstrap_arm_request_t *request);
void refmem_bootstrap_transport_disarm(void);
bool refmem_bootstrap_transport_armed(void);
bool refmem_bootstrap_transport_snapshot(
    refmem_spi_physical_snapshot_t *snapshot);

/* Frame exchange wrappers.  The physical adapter is half-duplex and its role
 * is fixed at arm time (MASTER transmits, SLAVE receives), so every exchange
 * re-arms the role.  These are the only entry points for carrying frames. */
bool refmem_bootstrap_transport_transmit(const uint8_t *frame,
                                         size_t frame_size);
bool refmem_bootstrap_transport_receive_begin(size_t frame_capacity,
                                              uint32_t timeout_ms);
/* 1 = frame ready, 0 = pending, -1 = error/timeout. */
int refmem_bootstrap_transport_receive_poll(uint8_t *frame,
                                           size_t frame_capacity,
                                           size_t *frame_size);

/* Raw/line bring-up diagnostics only.  REFMEM command frames must go through
 * the bootstrap carrier, never through a SCPI handler. */
refmem_spi_physical_adapter_t *refmem_bootstrap_transport_adapter(void);

uint32_t refmem_bootstrap_transport_reject_not_stopped(void);
uint32_t refmem_bootstrap_transport_reject_ring_ready(void);
uint32_t refmem_bootstrap_transport_arm_error_count(void);

#endif /* REFMEM_BOOTSTRAP_TRANSPORT_H */
