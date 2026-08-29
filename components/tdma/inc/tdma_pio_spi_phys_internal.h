#ifndef TDMA_PIO_SPI_PHYS_INTERNAL_H
#define TDMA_PIO_SPI_PHYS_INTERNAL_H

/* Internal linkage contract shared by the physical-layer feature modules.
 * This header is not part of the application API; it keeps timing, snapshot,
 * and DMA ownership helpers out of the protocol/state-machine modules while
 * preserving one owner for their static runtime state. */
#include "tdma_pio_spi_phys.h"
#include "hardware/pio.h"

extern tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
extern int s_tdma_pio_spi_tx_dma_channel;
extern int s_tdma_pio_spi_rx_dma_channel;
extern uint32_t s_tdma_pio_spi_flight_tx_words[
    TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS];
extern uint32_t s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_RX_DMA_WORD_MAX];
extern uint32_t s_tdma_pio_spi_cal_ring[TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS];
extern uint s_tdma_pio_spi_p3_initiator_offset;
extern uint s_tdma_pio_spi_p3_responder_offset;
extern uint s_tdma_pio_spi_p3_capture_offset;
extern uint s_tdma_pio_spi_p3_responder_capture_offset;
extern uint s_tdma_pio_spi_clk_forward_offset;
extern uint s_tdma_pio_spi_clk_coded_tx_offset;
extern uint s_tdma_pio_spi_clk_oversample_offset;
extern uint s_tdma_pio_spi_tx_offset;
extern uint s_tdma_pio_spi_rx_offset;
extern uint s_tdma_pio_spi_clk_burst_offset;
extern uint s_tdma_pio_spi_clk_capture_offset;
extern uint s_tdma_pio_spi_marker_forward_offset;
extern uint s_tdma_pio_spi_marker_origin_offset;
extern uint s_tdma_pio_spi_marker_capture_offset;
extern uint s_tdma_pio_spi_data_train_source_offset;
extern uint s_tdma_pio_spi_data_train_sink_offset;
extern uint s_tdma_pio_spi_sck_train_trigger_offset;
extern uint s_tdma_pio_spi_sck_train_source_offset;
extern uint s_tdma_pio_spi_sck_train_sink_offset;
extern uint s_tdma_pio_spi_cal_tx_offset;
extern uint s_tdma_pio_spi_cal_capture_offset;
extern uint s_tdma_pio_spi_flight_clock_latch_offset;
extern uint s_tdma_pio_spi_flight_origin_clock_offset;
extern uint s_tdma_pio_spi_flight_origin_data_offset;
extern uint s_tdma_pio_spi_flight_data_capture_offset;
extern uint s_tdma_pio_spi_flight_data_follower_offset;
extern uint s_tdma_pio_spi_flight_process_follower_offset;
extern uint s_tdma_pio_spi_flight_control_forward_offset;
extern uint s_tdma_pio_spi_flight_origin_rtt_offset;

bool tdma_pio_spi_phys_load_normal_programs(void);
bool tdma_pio_spi_phys_load_coarse_programs(void);
bool tdma_pio_spi_phys_load_cal_loopback_programs(void);
bool tdma_pio_spi_phys_load_coded_programs(void);
bool tdma_pio_spi_phys_load_marker_programs(void);
bool tdma_pio_spi_phys_load_data_train_programs(void);
bool tdma_pio_spi_phys_load_sck_train_programs(void);
bool tdma_pio_spi_phys_ensure_sms_claimed(void);
void tdma_pio_spi_phys_release_sms_claimed(void);
bool tdma_pio_spi_phys_ensure_flight_sms_claimed(void);
bool tdma_pio_spi_phys_flight_sms_claimed(void);
bool tdma_pio_spi_phys_claim_flight_resources(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_release_flight_resources(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_rx_prepare(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_configure(tdma_pio_spi_phys_t *phys);
uint32_t tdma_pio_spi_phys_flight_tail_bytes(
    const tdma_ring_runtime_config_t *config);
void tdma_pio_spi_phys_prepare_sm_pair(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_enable_sm_pair(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_flight_origin_recover(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_configure_flight(
    tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config);

bool tdma_pio_spi_phys_is_flight_persona(void);
PIO tdma_pio_spi_phys_tx_sm_pio(const tdma_pio_spi_phys_t *phys);
PIO tdma_pio_spi_phys_rx_sm_pio(const tdma_pio_spi_phys_t *phys);
PIO tdma_pio_spi_phys_evidence_pio(const tdma_pio_spi_phys_t *phys);
uint tdma_pio_spi_phys_latch_sm(const tdma_pio_spi_phys_t *phys);
PIO tdma_pio_spi_phys_control_pio(const tdma_pio_spi_phys_t *phys);
uint tdma_pio_spi_phys_control_sm(const tdma_pio_spi_phys_t *phys);
PIO tdma_pio_spi_phys_data_pio(const tdma_pio_spi_phys_t *phys);
uint tdma_pio_spi_phys_data_sm(const tdma_pio_spi_phys_t *phys);
PIO tdma_pio_spi_phys_capture_pio(const tdma_pio_spi_phys_t *phys);
uint tdma_pio_spi_phys_capture_sm(const tdma_pio_spi_phys_t *phys);

void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_set_error(tdma_pio_spi_phys_t *phys, uint32_t error);
uint64_t tdma_pio_spi_phys_now_us(void);
uint32_t tdma_pio_spi_phys_frame_tail_us(
    const tdma_pio_spi_phys_t *phys, size_t packet_size);
uint64_t tdma_pio_spi_phys_wire_time_ns(
    const tdma_pio_spi_phys_t *phys, size_t packet_size);
uint32_t tdma_pio_spi_phys_txstall_mask(uint32_t sm);
bool tdma_pio_spi_phys_clock_latch_rearm(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_clock_latch_read_and_rearm(
    tdma_pio_spi_phys_t *phys, uint64_t *timestamp_ns);
bool tdma_pio_spi_phys_capture_restore_step(
    tdma_pio_spi_phys_t *phys, bool *complete);
bool tdma_pio_spi_phys_restore_clock_latch(
    tdma_pio_spi_phys_t *phys, bool rearm);
bool tdma_pio_spi_phys_capture_words(tdma_pio_spi_phys_t *phys,
                                     size_t max_words,
                                     size_t *received_words);
bool tdma_pio_spi_phys_tx_put(tdma_pio_spi_phys_t *phys, uint32_t word);
void tdma_pio_spi_phys_record_complete_tx_frame(
    const uint8_t *header, const uint8_t *packet, size_t packet_size);
bool tdma_pio_spi_phys_ensure_rx_dma(void);
bool tdma_pio_spi_phys_ensure_tx_dma(void);
void tdma_pio_spi_phys_cal_cleanup(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_prepare_maintenance_pins(tdma_pio_spi_phys_t *phys);
uint32_t *tdma_pio_spi_phys_coded_tx_buffer(void);
uint32_t *tdma_pio_spi_phys_coded_rx_buffer(void);
uint32_t *tdma_pio_spi_phys_marker_rx_buffer(void);
uint32_t *tdma_pio_spi_phys_data_train_rx_buffer(void);
void tdma_pio_spi_phys_marker_decode_edges(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_data_train_write_begin(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_data_train_write_end(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_data_train_publish_error(
    tdma_pio_spi_phys_t *phys,
    uint32_t epoch,
    tdma_pio_spi_data_train_reject_t reason);
void tdma_pio_spi_phys_data_train_set_drivers(uint32_t role);
uint32_t tdma_pio_spi_phys_sck_train_inject_word(void);
uint32_t tdma_pio_spi_cal_sample_byte(uint32_t word, uint32_t index);
void tdma_pio_spi_phys_p3_decode(tdma_pio_spi_phys_t *phys);
uint32_t tdma_pio_spi_phys_rx_write_index(void);
uint64_t tdma_pio_spi_phys_rx_produced_words(
    const tdma_pio_spi_phys_t *phys);
uint32_t tdma_pio_spi_phys_rx_ring_word(uint64_t produced);
uint8_t tdma_pio_spi_phys_rx_ring_byte(uint64_t produced);
uint8_t tdma_pio_spi_phys_rx_ring_aligned_byte(uint64_t produced,
                                                uint32_t bit_shift);

#endif
