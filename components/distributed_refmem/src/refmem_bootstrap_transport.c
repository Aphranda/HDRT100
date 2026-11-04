#include "refmem_bootstrap_transport.h"

#include <stddef.h>

/* Owner state: a single adapter instance and a single writer (the REFMEM
 * bootstrap service / SCPI arm path on core0).  The TDMA ring never touches
 * this adapter; arm is refused while the ring link is ready. */
static refmem_spi_physical_adapter_t s_adapter;
static uint32_t s_baud_hz;
static refmem_spi_physical_pin_config_t s_pins;
static bool s_has_pins;
static volatile uint32_t s_armed;
static volatile uint32_t s_reject_not_stopped;
static volatile uint32_t s_reject_ring_ready;
static volatile uint32_t s_arm_error;

refmem_bootstrap_arm_result_t refmem_bootstrap_transport_arm(
    const refmem_bootstrap_arm_request_t *request)
{
    if (request == NULL) {
        s_arm_error++;
        return REFMEM_BOOTSTRAP_ARM_ADAPTER_ERROR;
    }
    if (!request->system_stopped) {
        s_reject_not_stopped++;
        return REFMEM_BOOTSTRAP_ARM_NOT_STOPPED;
    }
    if (request->ring_ready) {
        s_reject_ring_ready++;
        return REFMEM_BOOTSTRAP_ARM_RING_READY;
    }
    if (!refmem_spi_physical_adapter_arm(&s_adapter, request->role,
                                         request->baud_hz, request->pins)) {
        s_arm_error++;
        __atomic_store_n(&s_armed, 0u, __ATOMIC_RELEASE);
        return REFMEM_BOOTSTRAP_ARM_ADAPTER_ERROR;
    }
    __atomic_store_n(&s_armed, 1u, __ATOMIC_RELEASE);
    s_baud_hz = request->baud_hz;
    s_has_pins = request->pins != NULL;
    if (s_has_pins) {
        s_pins = *request->pins;
    }
    return REFMEM_BOOTSTRAP_ARM_OK;
}

void refmem_bootstrap_transport_disarm(void)
{
    refmem_spi_physical_adapter_disarm(&s_adapter);
    __atomic_store_n(&s_armed, 0u, __ATOMIC_RELEASE);
}

bool refmem_bootstrap_transport_armed(void)
{
    return __atomic_load_n(&s_armed, __ATOMIC_ACQUIRE) != 0u;
}

bool refmem_bootstrap_transport_snapshot(
    refmem_spi_physical_snapshot_t *snapshot)
{
    return refmem_spi_physical_adapter_get_snapshot(&s_adapter, snapshot);
}

bool refmem_bootstrap_transport_transmit(const uint8_t *frame,
                                         size_t frame_size)
{
    if (frame == NULL || frame_size == 0u ||
        !refmem_bootstrap_transport_armed()) {
        return false;
    }
    /* The adapter role is fixed at arm time; a TX burst re-arms MASTER so the
     * relay can alternate TX/RX on the same half-duplex link. */
    if (!refmem_spi_physical_adapter_arm(
            &s_adapter, REFMEM_SPI_PHYSICAL_ROLE_MASTER, s_baud_hz,
            s_has_pins ? &s_pins : NULL)) {
        s_arm_error++;
        return false;
    }
    return refmem_spi_physical_adapter_transmit(&s_adapter, frame, frame_size);
}

bool refmem_bootstrap_transport_receive_begin(size_t frame_capacity,
                                              uint32_t timeout_ms)
{
    if (frame_capacity == 0u || !refmem_bootstrap_transport_armed()) {
        return false;
    }
    if (!refmem_spi_physical_adapter_arm(
            &s_adapter, REFMEM_SPI_PHYSICAL_ROLE_SLAVE, s_baud_hz,
            s_has_pins ? &s_pins : NULL)) {
        s_arm_error++;
        return false;
    }
    return refmem_spi_physical_adapter_receive_begin(&s_adapter, frame_capacity,
                                                     timeout_ms);
}

int refmem_bootstrap_transport_receive_poll(uint8_t *frame,
                                           size_t frame_capacity,
                                           size_t *frame_size)
{
    if (frame == NULL || frame_size == NULL ||
        !refmem_bootstrap_transport_armed()) {
        return -1;
    }
    const refmem_spi_physical_rx_poll_result_t result =
        refmem_spi_physical_adapter_receive_poll(&s_adapter, frame,
                                                 frame_capacity, frame_size);
    if (result == REFMEM_SPI_PHYSICAL_RX_POLL_DONE) {
        return 1;
    }
    if (result == REFMEM_SPI_PHYSICAL_RX_POLL_PENDING) {
        return 0;
    }
    return -1;
}

refmem_spi_physical_adapter_t *refmem_bootstrap_transport_adapter(void)
{
    return &s_adapter;
}

uint32_t refmem_bootstrap_transport_reject_not_stopped(void)
{
    return s_reject_not_stopped;
}

uint32_t refmem_bootstrap_transport_reject_ring_ready(void)
{
    return s_reject_ring_ready;
}

uint32_t refmem_bootstrap_transport_arm_error_count(void)
{
    return s_arm_error;
}
