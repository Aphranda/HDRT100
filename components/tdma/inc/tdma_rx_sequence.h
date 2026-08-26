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

#endif
