#include "tdma_runtime_owner.h"

#include "tdma_pio_spi_phys.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "vdc_timestamp_clock.h"

#if defined(PROJECT_USE_FREERTOS) && PROJECT_USE_FREERTOS
#include "FreeRTOS.h"
#endif

static tdma_service_service_t s_tdma_runtime_owner;
static tdma_traffic_scheduler_t s_tdma_traffic_scheduler;
static tdma_pio_spi_ring_adapter_t s_tdma_pio_spi_ring_adapter;
static tdma_pio_spi_phys_t s_tdma_pio_spi_phys;
#if !defined(PROJECT_USE_FREERTOS) || !PROJECT_USE_FREERTOS
static tdma_traffic_scheduler_slot_t
    s_tdma_traffic_slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
#endif
static bool s_tdma_runtime_owner_initialized;

bool tdma_runtime_owner_init(void)
{
    if (s_tdma_runtime_owner_initialized) {
        return true;
    }
    tdma_traffic_scheduler_slot_t *slots = NULL;
#if defined(PROJECT_USE_FREERTOS) && PROJECT_USE_FREERTOS
    slots = pvPortMalloc(sizeof(tdma_traffic_scheduler_slot_t) *
                         TDMA_TRAFFIC_SCHEDULER_RUNTIME_SLOT_COUNT);
#else
    slots = s_tdma_traffic_slots;
#endif
    bool initialized = false;
    if (slots != NULL &&
        tdma_traffic_scheduler_init(&s_tdma_traffic_scheduler,
                                    slots,
                                    TDMA_TRAFFIC_SCHEDULER_RUNTIME_SLOT_COUNT) &&
        tdma_service_init(&s_tdma_runtime_owner) &&
        tdma_service_bind_traffic_scheduler(&s_tdma_runtime_owner,
                                            &s_tdma_traffic_scheduler) &&
        tdma_pio_spi_ring_adapter_init(&s_tdma_pio_spi_ring_adapter) &&
        tdma_service_register_adapter_impl(
            &s_tdma_runtime_owner,
            TDMA_ADAPTER_PIO_SPI,
            tdma_pio_spi_ring_adapter_ops(),
            &s_tdma_pio_spi_ring_adapter)) {
        tdma_pio_spi_ring_adapter_set_phys_ctrl(
            &s_tdma_pio_spi_ring_adapter,
            tdma_pio_spi_phys_arm,
            tdma_pio_spi_phys_disarm,
            tdma_pio_spi_phys_train_clock,
            &s_tdma_pio_spi_phys);
        tdma_pio_spi_ring_adapter_set_phys(
            &s_tdma_pio_spi_ring_adapter,
            tdma_pio_spi_phys_tx,
            tdma_pio_spi_phys_rx,
            &s_tdma_pio_spi_phys);
        tdma_pio_spi_ring_adapter_set_flight_fifo(
            &s_tdma_pio_spi_ring_adapter,
            &s_tdma_runtime_owner.flight_fifo);
        (void)vdc_timestamp_clock_init();
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &s_tdma_pio_spi_ring_adapter,
            vdc_timestamp_clock_resolution_ns(),
            TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
        initialized = true;
    }
    if (!initialized) {
#if defined(PROJECT_USE_FREERTOS) && PROJECT_USE_FREERTOS
        if (slots != NULL) {
            vPortFree(slots);
        }
#endif
        return false;
    }
    s_tdma_runtime_owner_initialized = true;
    return true;
}

tdma_service_service_t *tdma_runtime_owner_get(void)
{
    return s_tdma_runtime_owner_initialized ? &s_tdma_runtime_owner : NULL;
}

tdma_traffic_scheduler_t *tdma_runtime_owner_get_scheduler(void)
{
    return s_tdma_runtime_owner_initialized ? &s_tdma_traffic_scheduler : NULL;
}

tdma_pio_spi_ring_adapter_t *tdma_runtime_owner_get_ring_adapter(void)
{
    return s_tdma_runtime_owner_initialized ? &s_tdma_pio_spi_ring_adapter : NULL;
}

bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *snapshot)
{
    if (!s_tdma_runtime_owner_initialized || snapshot == NULL) {
        return false;
    }
    return tdma_ring_runtime_get_snapshot(&s_tdma_runtime_owner.ring_runtime,
                                          snapshot);
}

bool tdma_runtime_owner_get_phys_snapshot(tdma_pio_spi_phys_snapshot_t *snapshot)
{
    if (!s_tdma_runtime_owner_initialized || snapshot == NULL) {
        return false;
    }
    return tdma_pio_spi_phys_get_snapshot(&s_tdma_pio_spi_phys, snapshot);
}
