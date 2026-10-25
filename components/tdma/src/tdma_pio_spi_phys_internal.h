#ifndef TDMA_PIO_SPI_PHYS_INTERNAL_H
#define TDMA_PIO_SPI_PHYS_INTERNAL_H

#include "tdma_pio_spi_phys.h"
#include "tdma_rx_sequence.h"
#include "tdma_rx_start_cut.h"
#include "tdma_pio_spi_phys_programs.h"
#include "tdma_pio_spi_origin_workspace.h"
#include "tdma_origin_build_job.h"

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


/* Phys helpers the flight-IO unit calls (direct calls, Core1 hot). */
PIO tdma_pio_spi_phys_capture_pio(const tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_capture_restore_step(
    tdma_pio_spi_phys_t *phys, bool *complete);
uint tdma_pio_spi_phys_capture_sm(const tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_capture_words_ex(tdma_pio_spi_phys_t *phys,
    size_t max_words, size_t *received_words, tdma_rx_capture_t *capture);
bool tdma_pio_spi_phys_clock_latch_read_and_rearm(
    tdma_pio_spi_phys_t *phys,
    uint64_t *timestamp_ns);
bool tdma_pio_spi_phys_clock_latch_rearm(
    tdma_pio_spi_phys_t *phys);
PIO tdma_pio_spi_phys_control_pio(const tdma_pio_spi_phys_t *phys);
uint tdma_pio_spi_phys_control_sm(const tdma_pio_spi_phys_t *phys);
PIO tdma_pio_spi_phys_data_pio(const tdma_pio_spi_phys_t *phys);
uint tdma_pio_spi_phys_data_sm(const tdma_pio_spi_phys_t *phys);
PIO tdma_pio_spi_phys_evidence_pio(const tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_flight_origin_recover(
    tdma_pio_spi_phys_t *phys);
uint tdma_pio_spi_phys_latch_sm(const tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_restore_clock_latch(
    tdma_pio_spi_phys_t *phys,
    bool rearm);
uint tdma_pio_spi_phys_rtt_sm(const tdma_pio_spi_phys_t *phys);
uint8_t tdma_pio_spi_phys_rx_ring_byte(uint64_t produced);
bool tdma_pio_spi_phys_tx_put(tdma_pio_spi_phys_t *phys,
                                     uint32_t word);


/* Workspace alias shared by the host unit and the flight-IO unit. */
extern tdma_pio_spi_workspace_t s_tdma_pio_spi_workspace;
#define s_tdma_pio_spi_flight_tx_words (s_tdma_pio_spi_workspace.service.tx_words)

/* Event/origin helpers called by the flight-IO unit. */
void tdma_pio_spi_phys_event_service(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_event_copy(const tdma_pio_spi_phys_t *phys,
                                        tdma_pio_spi_event_snapshot_t *out);
bool tdma_pio_spi_phys_origin_rx(tdma_pio_spi_phys_t *phys, uint8_t *packet,
    size_t capacity, size_t *packet_size, uint64_t *rx_timestamp_ns);

/* Helpers and state the origin unit calls (Core1 direct access). */
extern bool s_tdma_pio_spi_flight_sms_claimed;
void tdma_pio_spi_phys_disable_dma_mask(uint32_t mask);
void tdma_pio_spi_phys_pause_sm_pair(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_prepare_sm_pair(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_stop_dma_chain(uint32_t loader_mask,
                                            uint32_t executor_mask,
                                            uint32_t children_mask,
                                            uint64_t deadline);
void tdma_pio_spi_phys_origin_record_invalidate(tdma_pio_spi_phys_t *phys);

extern tdma_origin_build_job_t s_tdma_origin_build_job;


/* Event sub-cluster exports (2026-10-25). */
extern tdma_rx_start_cut_t s_tdma_rx_start_cut;
extern tdma_rx_start_cut_t s_tdma_rx_start_cut_slots[2];
extern uint32_t s_tdma_rx_start_cut_guard;
extern uint32_t s_tdma_rx_start_cut_data_phase;
extern uint32_t s_tdma_rx_start_cut_marker_phase;
void tdma_rx_start_cut_publish(void);
void tdma_rx_start_cut_arm_begin(void);
void tdma_rx_start_cut_retire(uint32_t reasons);
void tdma_rx_start_cut_disarmed(void);
bool tdma_rx_start_cut_dma_available(void);
void tdma_rx_start_cut_before(tdma_pio_spi_phys_t *phys);
uint32_t tdma_rx_start_cut_lift(tdma_rx_start_cut_t *cut,
                                      tdma_rx_dma_counter_t *counter);
void tdma_rx_start_cut_after(tdma_pio_spi_phys_t *phys,
                                   tdma_rx_dma_counter_t *counter,
                                   uint32_t pads_before, uint32_t pads_after);
void tdma_rx_start_cut_monitor(tdma_pio_spi_phys_t *phys);


/* start-cut DMA descriptor masks (shared by event and start-cut units). */
#define TDMA_RX_START_CUT_DMA_ERRORS (DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS | \
    DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS)
#define TDMA_RX_START_CUT_DMA_CONFIG_MASK (DMA_CH0_CTRL_TRIG_BITS & \
    ~(TDMA_RX_START_CUT_DMA_ERRORS | DMA_CH0_CTRL_TRIG_BUSY_BITS))



void tdma_event_live_retire(uint32_t reason);
extern uint32_t s_tdma_event_epoch;
extern uint32_t s_tdma_event_hz;
extern bool s_tdma_event_prelaunch;
extern tdma_frozen_geometry_snapshot_t s_tdma_event_geometry;
extern tdma_event_observer_t s_tdma_event_observer;
uint32_t tdma_event_prelaunch_geometry(const tdma_pio_spi_phys_t *phys);
void tdma_event_live_anchor_reset(void);
#endif /* TDMA_PIO_SPI_PHYS_INTERNAL_H */
