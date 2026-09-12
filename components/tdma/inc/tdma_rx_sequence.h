#ifndef TDMA_RX_SEQUENCE_H
#define TDMA_RX_SEQUENCE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t produced_words;
    uint32_t last_write_index;
    uint32_t last_complete_frames;
    uint32_t ring_words;
    bool initialized;
} tdma_rx_sequence_tracker_t;

bool tdma_rx_sequence_reset(tdma_rx_sequence_tracker_t *tracker,
                            uint32_t ring_words,
                            uint32_t initial_write_index,
                            uint32_t initial_complete_frames);

bool tdma_rx_sequence_observe(tdma_rx_sequence_tracker_t *tracker,
                              uint32_t write_index,
                              uint32_t complete_frames,
                              uint32_t fixed_frame_words,
                              uint64_t *produced_words);

/* RP2350 self-trigger count: physical frame words times this factor.
 * The factor preserves the SRAM ring index; whole physical frames preserve
 * wire alignment even when an unobserved complete count period is lost. */
#define TDMA_RX_DMA_RELOAD_FRAMES (1u << 16u)
#define TDMA_RX_DMA_COUNT_MAX 0x0fffffffu
#define TDMA_RX_OBSERVATION_SCAN_WORDS 320u

typedef struct {
    uint64_t produced_words;
    uint64_t sample_before_ticks;
    uint64_t observation_epoch;
    uint32_t reload_words;
    uint32_t position;
    bool initialized;
} tdma_rx_dma_counter_t;

bool tdma_rx_dma_counter_reset(tdma_rx_dma_counter_t *counter,
                               uint32_t physical_frame_words, uint64_t now_ticks);
/* Bracket the MMIO count read with clk_sys ticks. An interval long enough
 * for an entire count period invalidates continuity and advances only the
 * observation_epoch. produced_words accumulates the observed modular delta,
 * a lower bound after loss, never invented bytes. Discard earlier candidates
 * and reject copies spanning epochs even when produced_words is unchanged. */
bool tdma_rx_dma_counter_observe(tdma_rx_dma_counter_t *counter,
                                 uint32_t remaining_words,
                                 uint64_t before_ticks,
                                 uint64_t after_ticks,
                                 uint64_t *produced_words,
                                 bool *discontinuity);

#endif
