#ifndef TDMA_RX_FIRST_WINDOW_H
#define TDMA_RX_FIRST_WINDOW_H

#include <stddef.h>
#include <stdint.h>
#include "tdma_flight_engine.h"
#include "tdma_rx_scan.h"

#define TDMA_RX_FIRST_WINDOW_SCHEMA 1u
#define TDMA_RX_FIRST_WINDOW_RAW_CAPACITY (TDMA_PIO_SPI_PACKET_HEADER_SIZE + \
    TDMA_FLIGHT_SHORT_PACKET_SIZE + TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES)
#define TDMA_RX_FIRST_WINDOW_RAW_STORAGE ((TDMA_RX_FIRST_WINDOW_RAW_CAPACITY + 3u) & ~3u)

enum {
    TDMA_RX_FIRST_AVAILABLE = 1u << 0,
    TDMA_RX_FIRST_PRELAUNCH_ACTIVE = 1u << 1,
    TDMA_RX_FIRST_RAW_DONE = 1u << 2,
    TDMA_RX_FIRST_RAW_ATTEMPTED = 1u << 3,
    TDMA_RX_FIRST_RAW_COPY_VALID = 1u << 4,
    TDMA_RX_FIRST_EVENT_DONE = 1u << 5,
    TDMA_RX_FIRST_EVENT_VALID = 1u << 6,
    TDMA_RX_FIRST_COLLECTION_TERMINAL = 1u << 7,
    TDMA_RX_FIRST_RETIRED = 1u << 8,
    TDMA_RX_FIRST_STOPPED = 1u << 9,
    TDMA_RX_FIRST_ENVELOPE_CROSSES_WINDOW = 1u << 10,
    TDMA_RX_FIRST_DIAGNOSTIC_ONLY = 1u << 11,
    TDMA_RX_FIRST_IDENTITY_UNPROVED = 1u << 12,
    TDMA_RX_FIRST_PHYSICAL_FIRST_UNPROVED = 1u << 13
};

typedef enum {
    TDMA_RX_FIRST_NONE = 0,
    TDMA_RX_FIRST_PRELAUNCH_REJECTED,
    TDMA_RX_FIRST_GEOMETRY_OR_CAPACITY,
    TDMA_RX_FIRST_CROSSES_WINDOW,
    TDMA_RX_FIRST_RAW_OVERWRITTEN,
    TDMA_RX_FIRST_RAW_COUNTER_OR_EPOCH,
    TDMA_RX_FIRST_RAW_DMA_CONFIG,
    TDMA_RX_FIRST_RAW_DMA_ERROR,
    TDMA_RX_FIRST_RAW_CAPTURE_FAULT,
    TDMA_RX_FIRST_OBSERVER_FAILED,
    TDMA_RX_FIRST_ORDINAL_MISSING,
    TDMA_RX_FIRST_STOP_BEFORE_COMPLETE,
    TDMA_RX_FIRST_PERSONA_OR_CONFIG_RETIRED,
    TDMA_RX_FIRST_PUBLICATION_EXHAUSTED
} tdma_rx_first_reason_t;

/* One selected-prelaunch ARM; independent diagnostic components. raw is the
 * normalized observed byte stream [0,F), never realigned. Event words are
 * first RX X/Y, TX X/Y and wire-order sequence FIFO words. Start bounds use
 * observer_base_us; copy ticks use the VDC clk_sys clock. No clock mapping,
 * packet/event identity, timestamp eligibility or DPLL admission is implied. */
typedef struct {
    uint64_t arm_epoch, observation_epoch, produced_before, produced_after;
    uint64_t copy_before_ticks, copy_after_ticks, observer_base_us;
    uint64_t rx_elapsed_cycles, tx_elapsed_cycles, start_lo_cycles, start_hi_cycles;
    /* retire_reasons is a mask: bit (1u << tdma_rx_first_reason_t). */
    uint32_t schema, flags, first_reason, retire_reasons;
    uint32_t geometry_generation, config_seq, map_generation, observer_epoch, pio_hz;
    uint32_t physical_frame_words, outer_packet_words;
    uint32_t alignment_byte_shift, alignment_bit_shift, envelope_end;
    uint32_t raw_count, raw_reason, dma_channel, dma_count_before, dma_count_after;
    uint32_t dma_ctrl_before, dma_ctrl_after, capture_fdebug_before, capture_fdebug_after;
    uint32_t event_state, event_reason, event_fault_bits, event_sequence, event_ordinal;
    uint32_t event_word_mask, prelaunch_reason, event_words[5];
    uint8_t raw[TDMA_RX_FIRST_WINDOW_RAW_STORAGE];
} tdma_rx_first_window_t;

_Static_assert(offsetof(tdma_rx_first_window_t, raw) == 228u,
               "first-window scalar layout must remain stable");
_Static_assert(sizeof(tdma_rx_first_window_t) <= 536u,
               "first-window archive stays outside the aligned workspace");

/* Availability of a coherent diagnostic, including pending/failed outcomes.
 * Three bounded guarded-copy attempts. Failure clears a non-NULL output;
 * disabled observer returns false. Output must not alias owner storage. */
bool tdma_pio_spi_phys_get_rx_first_window(tdma_rx_first_window_t *out);

/* Core1 program manager: call only when the installed program is unloaded. */
void tdma_pio_spi_phys_rx_first_window_persona_unload(void);

#endif
