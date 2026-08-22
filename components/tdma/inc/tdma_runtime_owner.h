#ifndef TDMA_RUNTIME_OWNER_H
#define TDMA_RUNTIME_OWNER_H

#include <stdbool.h>

#include "tdma_pio_spi_phys.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_service.h"
#include "tdma_operating_profile.h"

/* Product firmware has one TDMA owner. Domain wrappers register payloads and
 * adapter operations against it; they do not create parallel runtimes. */
bool tdma_runtime_owner_init(void);
tdma_service_service_t *tdma_runtime_owner_get(void);
tdma_traffic_scheduler_t *tdma_runtime_owner_get_scheduler(void);
tdma_pio_spi_ring_adapter_t *tdma_runtime_owner_get_ring_adapter(void);

/* Read-only ring snapshot for low-frequency maintenance logging on core0
 * (the resident ring itself is driven by the core1 TDMA service). */
bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *snapshot);
/* Last accepted ring configuration retained by the TDMA service while the
 * live runtime is STOPPED.  Calibration may bind maintenance evidence to it,
 * but may not modify or arm the ring through this snapshot. */
bool tdma_runtime_owner_get_staged_ring_config(
    tdma_service_ring_runtime_config_t *snapshot);

/* Read-only physical-layer snapshot (RX capture stall/partial counters and
 * TX timeout counters) for bring-up diagnostics. */
bool tdma_runtime_owner_get_phys_snapshot(tdma_pio_spi_phys_snapshot_t *snapshot);
bool tdma_runtime_owner_get_clk_train_snapshot(
    tdma_pio_spi_clk_train_snapshot_t *snapshot);
bool tdma_runtime_owner_train_clock(uint32_t cycles);
void tdma_runtime_owner_update_training_gate(void);
bool tdma_runtime_owner_get_operating_profile(
    tdma_operating_profile_manager_t *snapshot);
bool tdma_runtime_owner_stage_operating_profile(uint32_t level);
bool tdma_runtime_owner_apply_operating_profile(void);
/* Stage the measured P1/P2 full-ring receive window while the ring is
 * stopped.  Core1 consumes it on the next ARM/START. */
bool tdma_runtime_owner_set_loop_delay_ns(uint32_t loop_delay_ns,
                                          uint32_t tolerance_ns);
/* Core0-facing guarded intent publication.  Only
 * tdma_runtime_owner_cal_loopback_service() on core1 may touch PIO/SM/DMA. */
bool tdma_runtime_owner_cal_loopback_start(uint32_t sample_hz,
                                           uint32_t sample_words,
                                           uint32_t epoch);
void tdma_runtime_owner_cal_loopback_stop(void);
void tdma_runtime_owner_cal_loopback_service(void);
bool tdma_runtime_owner_get_cal_loopback_snapshot(
    tdma_pio_spi_cal_loopback_snapshot_t *snapshot);

/* Core1-only raw CLOCK_CODED transport.  Calibration owns marker meaning and
 * correlation; the TDMA owner only switches persona and moves packed samples
 * through its declared SM/DMA resources. */
bool tdma_runtime_owner_coded_start_core1(
    const tdma_pio_spi_coded_request_t *request);
void tdma_runtime_owner_coded_stop_core1(void);
void tdma_runtime_owner_coded_service_core1(void);
bool tdma_runtime_owner_get_coded_snapshot(
    tdma_pio_spi_coded_snapshot_t *snapshot);
bool tdma_runtime_owner_copy_coded_capture_core1(
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count);
bool tdma_runtime_owner_p3_start_core1(
    const tdma_pio_spi_p3_request_t *request);
void tdma_runtime_owner_p3_stop_core1(void);
void tdma_runtime_owner_p3_service_core1(void);
bool tdma_runtime_owner_get_p3_snapshot(
    tdma_pio_spi_p3_snapshot_t *snapshot);

#endif
