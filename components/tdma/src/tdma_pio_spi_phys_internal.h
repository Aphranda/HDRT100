#ifndef TDMA_PIO_SPI_PHYS_INTERNAL_H
#define TDMA_PIO_SPI_PHYS_INTERNAL_H

#include "tdma_pio_spi_phys.h"
#include "tdma_rx_sequence.h"
#include "tdma_pio_spi_phys_programs.h"

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

#ifndef TDMA_PIO_SPI_ALIGNMENT_FITS_DEFINED
#define TDMA_PIO_SPI_ALIGNMENT_FITS_DEFINED
/* Shared pure predicate used by the host unit and the async RX-scan code. */
static inline bool tdma_pio_spi_phys_alignment_fits(
    const tdma_pio_spi_phys_t *phys, uint32_t byte_shift, uint32_t bit_shift)
{
    if (phys == NULL || phys->flight_physical_byte_count == 0u) return false;
    const uint64_t packet_size =
        (uint64_t)TDMA_TRANSPORT_FRAME_HEADER_SIZE + (uint64_t)phys->flight_payload_size;
    if ((uint64_t)phys->flight_physical_byte_count <= packet_size) return false;
    const uint64_t room_bits =
        ((uint64_t)phys->flight_physical_byte_count - packet_size) * 8u;
    return ((uint64_t)byte_shift + TDMA_PIO_SPI_PACKET_HEADER_SIZE) * 8u +
               (uint64_t)bit_shift <=
           room_bits;
}
#endif

/* ---- Split-unit surface (2026-10-25) -------------------------------------
 * The phys fragments used to share these through one translation unit. They
 * stay plain externs and direct memory accesses: several of these paths run on
 * the Core1 realtime side, so no accessor indirection is introduced. */

/* RX service state owned by tdma_pio_spi_phys.c. */
extern uint64_t s_tdma_pio_spi_rx_arm_epoch;
extern uint64_t s_tdma_pio_spi_rx_capture_id;
extern uint64_t s_tdma_pio_spi_rx_scan_produced;
extern bool s_tdma_pio_spi_rx_arm_valid;
extern tdma_rx_dma_counter_t s_tdma_pio_spi_rx_sequence;
void tdma_pio_spi_phys_set_error(tdma_pio_spi_phys_t *phys, uint32_t error);

/* Frozen-geometry unit (tdma_pio_spi_phys_geometry.c). */
extern bool s_geometry_request_pending;
extern bool s_geometry_physical_stopped;
void tdma_geometry_publish(void);
void tdma_geometry_retire(uint32_t reason);
void tdma_geometry_arm_failed(void);
bool tdma_geometry_arm_begin(tdma_pio_spi_phys_t *phys,
                             const tdma_ring_runtime_config_t *config,
                             uint32_t persona);
bool tdma_geometry_arm_bound(const tdma_pio_spi_phys_t *phys);
void tdma_geometry_persona(uint32_t persona);
void tdma_geometry_stop_begin(const tdma_pio_spi_phys_t *phys);
void tdma_geometry_trained(const tdma_pio_spi_phys_t *phys);
bool tdma_geometry_observer_current(const tdma_pio_spi_phys_t *phys,
                                    const tdma_frozen_geometry_snapshot_t *geometry);
void tdma_geometry_observer_record(uint32_t generation, uint32_t epoch,
                                   uint32_t state, uint32_t reason,
                                   uint32_t prefix_bits);
bool tdma_geometry_observer_select(const tdma_pio_spi_phys_t *phys,
                                   tdma_frozen_geometry_snapshot_t *out);
void tdma_rx_first_window_admit(const tdma_ring_runtime_config_t *config);
void tdma_rx_first_window_arm_failed(void);

/* RX-scan unit (tdma_pio_spi_phys_rx_scan.c). */
extern uint8_t s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_RX_DMA_WORD_MAX];
bool tdma_pio_spi_phys_capture_words_async(tdma_pio_spi_phys_t *phys,
                                          size_t max_words,
                                          size_t *received_words);
bool tdma_pio_spi_phys_capture_words_async_ex(tdma_pio_spi_phys_t *phys,
                                             size_t max_words,
                                             size_t *received_words,
                                             tdma_rx_capture_t *capture);
bool tdma_pio_spi_phys_rx_scan_cancel(tdma_pio_spi_phys_t *phys);
uint32_t tdma_pio_spi_phys_rx_write_index(void);
uint64_t tdma_pio_spi_phys_rx_produced_words(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_rx_ring_copy(uint8_t *destination, uint64_t produced,
                                    uint32_t count, uint32_t bit_shift);


/* Priority-lane unit (tdma_pio_spi_phys_priority.c). */
bool tdma_priority_start(tdma_pio_spi_phys_t *phys,
                         const tdma_ring_runtime_config_t *config);
void tdma_priority_stop(void);
void tdma_priority_boundary_service(tdma_pio_spi_phys_t *phys);

#endif /* TDMA_PIO_SPI_PHYS_INTERNAL_H */
