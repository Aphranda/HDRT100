#ifndef TDMA_PIO_SPI_PHYS_INTERNAL_H
#define TDMA_PIO_SPI_PHYS_INTERNAL_H

#include "tdma_pio_spi_phys.h"

/* Program state and helpers shared by the maintenance/calibration/training
 * units that used to be textually included into tdma_pio_spi_phys.c. They
 * stay direct memory accesses (no accessor calls): several of these paths
 * run on the Core1 realtime side. */
typedef union {
    uint32_t coded[TDMA_PIO_SPI_CODED_BUFFER_WORDS];
    uint32_t marker[TDMA_PIO_SPI_MARKER_BUFFER_WORDS];
    uint32_t data_train[TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS];
} tdma_pio_spi_cal_tx_workspace_t;
typedef union {
    uint32_t coded[TDMA_PIO_SPI_CODED_BUFFER_WORDS];
    uint32_t marker[TDMA_PIO_SPI_MARKER_BUFFER_WORDS];
    uint32_t data_train[TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS];
} tdma_pio_spi_cal_rx_workspace_t;

extern tdma_pio_spi_cal_tx_workspace_t s_tdma_pio_spi_cal_tx_workspace;
extern tdma_pio_spi_cal_rx_workspace_t s_tdma_pio_spi_cal_rx_workspace;
extern uint s_tdma_pio_spi_clk_coded_tx_offset;
extern uint s_tdma_pio_spi_clk_forward_offset;
extern uint s_tdma_pio_spi_clk_oversample_offset;
extern int s_tdma_pio_spi_command_dma_channel;
extern uint s_tdma_pio_spi_data_train_sink_offset;
extern uint s_tdma_pio_spi_data_train_source_offset;
extern int s_tdma_pio_spi_executor_dma_channel;
extern uint s_tdma_pio_spi_marker_capture_offset;
extern uint s_tdma_pio_spi_marker_forward_offset;
extern uint s_tdma_pio_spi_marker_origin_offset;
extern uint s_tdma_pio_spi_p3_capture_offset;
extern uint s_tdma_pio_spi_p3_initiator_offset;
extern uint s_tdma_pio_spi_p3_responder_capture_offset;
extern uint s_tdma_pio_spi_p3_responder_offset;
extern tdma_pio_spi_program_manager_t s_tdma_pio_spi_program_manager;
extern tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
extern int s_tdma_pio_spi_rx_dma_channel;
extern uint s_tdma_pio_spi_rx_offset;
extern uint32_t s_tdma_pio_spi_sck_train_inject_word;
extern uint s_tdma_pio_spi_sck_train_sink_offset;
extern uint s_tdma_pio_spi_sck_train_source_offset;
extern uint s_tdma_pio_spi_sck_train_trigger_offset;
extern int s_tdma_pio_spi_tx_dma_channel;
extern uint s_tdma_pio_spi_tx_offset;

/* Persona workspaces alias into the shared TX/RX union storage. */
#define s_tdma_pio_spi_coded_tx (s_tdma_pio_spi_cal_tx_workspace.coded)
#define s_tdma_pio_spi_marker_tx (s_tdma_pio_spi_cal_tx_workspace.marker)
#define s_tdma_pio_spi_data_train_tx \
    (s_tdma_pio_spi_cal_tx_workspace.data_train)
#define s_tdma_pio_spi_coded_rx (s_tdma_pio_spi_cal_rx_workspace.coded)
#define s_tdma_pio_spi_marker_rx (s_tdma_pio_spi_cal_rx_workspace.marker)
#define s_tdma_pio_spi_data_train_rx \
    (s_tdma_pio_spi_cal_rx_workspace.data_train)

void tdma_pio_spi_phys_cal_cleanup(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_cal_decode_step(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_ensure_rx_dma(void);
bool tdma_pio_spi_phys_ensure_sms_claimed(void);
bool tdma_pio_spi_phys_ensure_tx_dma(void);
bool tdma_pio_spi_phys_is_flight_persona(void);
bool tdma_pio_spi_phys_leave_flight_for_maintenance(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_set_line_drivers(bool enabled);
bool tdma_pio_spi_phys_stop_command_dma(tdma_pio_spi_phys_t *phys);

extern uint32_t s_tdma_pio_spi_cal_ring[TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS];

void tdma_pio_spi_phys_cal_reject(tdma_pio_spi_phys_t *phys, uint32_t epoch, uint32_t reason);

void tdma_pio_spi_phys_cal_write_begin(tdma_pio_spi_phys_t *phys);

void tdma_pio_spi_phys_cal_write_end(tdma_pio_spi_phys_t *phys);

void tdma_pio_spi_phys_prepare_maintenance_mapping( tdma_pio_spi_phys_t *phys);

void tdma_pio_spi_phys_cal_mark_persona( tdma_pio_spi_phys_t *phys, tdma_pio_spi_program_persona_t persona);

uint32_t tdma_pio_spi_cal_sample_byte(uint32_t word, uint32_t index);

bool tdma_pio_spi_phys_cal_persona_switch_ready(void);

#endif /* TDMA_PIO_SPI_PHYS_INTERNAL_H */
