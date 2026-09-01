#ifndef TDMA_PIO_SPI_PHYS_PROGRAMS_H
#define TDMA_PIO_SPI_PHYS_PROGRAMS_H

#include <stdbool.h>

#include "pico/types.h"
#include "tdma_pio_spi_phys.h"
#include "tdma_pio_spi_persona_fsm.h"

/* Explicit owner context for PIO program persona transitions.  The physical
 * layer owns the storage; this module only mutates the supplied offsets and
 * persona while preserving the existing PIO/SM/DMA admission rules. */
typedef struct {
    tdma_pio_spi_persona_fsm_t lifecycle;
    bool *sms_claimed;
    tdma_pio_spi_program_persona_t *program_persona;
    uint *tx_offset;
    uint *rx_offset;
    uint *clk_forward_offset;
    uint *marker_forward_offset;
    uint *clk_burst_offset;
    uint *clk_capture_offset;
    uint *clk_coded_tx_offset;
    uint *clk_oversample_offset;
    uint *marker_origin_offset;
    uint *marker_capture_offset;
    uint *data_train_source_offset;
    uint *data_train_sink_offset;
    uint *sck_train_trigger_offset;
    uint *sck_train_source_offset;
    uint *sck_train_sink_offset;
    uint *cal_tx_offset;
    uint *cal_capture_offset;
    uint *p3_initiator_offset;
    uint *p3_responder_offset;
    uint *p3_capture_offset;
    uint *p3_responder_capture_offset;
    uint *flight_origin_clock_offset;
    uint *flight_origin_data_offset;
    uint *flight_data_follower_offset;
    uint *flight_process_follower_offset;
    uint *flight_control_forward_offset;
    uint *flight_clock_latch_offset;
    uint *flight_origin_rtt_offset;
    int *tx_dma_channel;
    int *rx_dma_channel;
} tdma_pio_spi_program_manager_t;

bool tdma_pio_spi_programs_select(
    tdma_pio_spi_program_manager_t *manager,
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona);

bool tdma_pio_spi_programs_ensure_sms_claimed(
    tdma_pio_spi_program_manager_t *manager);

#endif
