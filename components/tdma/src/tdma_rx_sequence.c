#include "tdma_rx_sequence.h"

#include <stddef.h>

bool tdma_rx_dma_counter_reset(tdma_rx_dma_counter_t *counter,
                               uint32_t physical_frame_words, uint64_t now_ticks)
{
    if (counter == NULL || physical_frame_words == 0u ||
        physical_frame_words > TDMA_RX_DMA_COUNT_MAX / TDMA_RX_DMA_RELOAD_FRAMES)
        return false;
    *counter = (tdma_rx_dma_counter_t){
        .reload_words = physical_frame_words * TDMA_RX_DMA_RELOAD_FRAMES,
        .sample_before_ticks = now_ticks, .initialized = true};
    return true;
}

bool tdma_rx_dma_counter_observe(tdma_rx_dma_counter_t *counter,
                                 uint32_t remaining_words,
                                 uint64_t before_ticks,
                                 uint64_t after_ticks,
                                 uint64_t *produced_words,
                                 bool *discontinuity)
{
    if (counter == NULL || produced_words == NULL || discontinuity == NULL ||
        !counter->initialized || remaining_words > counter->reload_words ||
        after_ticks < before_ticks || before_ticks < counter->sample_before_ticks)
        return false;
    const uint32_t reload = counter->reload_words;
    const uint32_t position = remaining_words == 0u ? 0u : reload - remaining_words;
    /* One DMA channel cannot retire more than one write per clk_sys tick.
     * Use this hardware ceiling, not baud or a software frame estimate. */
    const bool lost = after_ticks - counter->sample_before_ticks >= reload;
    const uint32_t advance = position >= counter->position
        ? position - counter->position : reload - counter->position + position;
    if (UINT64_MAX - counter->produced_words < advance ||
        (lost && counter->observation_epoch == UINT64_MAX)) return false;
    if (lost) counter->observation_epoch++;
    counter->produced_words += advance;
    counter->sample_before_ticks = before_ticks;
    counter->position = position;
    *produced_words = counter->produced_words;
    *discontinuity = lost;
    return true;
}

bool tdma_rx_sequence_reset(tdma_rx_sequence_tracker_t *tracker,
                            uint32_t ring_words,
                            uint32_t initial_write_index,
                            uint32_t initial_complete_frames)
{
    if (tracker == NULL || ring_words < 2u ||
        initial_write_index >= ring_words) {
        return false;
    }
    tracker->produced_words = 0u;
    tracker->last_write_index = initial_write_index;
    tracker->last_complete_frames = initial_complete_frames;
    tracker->ring_words = ring_words;
    tracker->initialized = true;
    return true;
}

bool tdma_rx_sequence_observe(tdma_rx_sequence_tracker_t *tracker,
                              uint32_t write_index,
                              uint32_t complete_frames,
                              uint32_t fixed_frame_words,
                              uint64_t *produced_words)
{
    if (tracker == NULL || produced_words == NULL ||
        !tracker->initialized || write_index >= tracker->ring_words ||
        (fixed_frame_words != 0u &&
         (uint64_t)fixed_frame_words * 2u >= tracker->ring_words)) {
        return false;
    }

    const uint32_t modulo_delta =
        write_index >= tracker->last_write_index
            ? write_index - tracker->last_write_index
            : tracker->ring_words - tracker->last_write_index + write_index;
    uint64_t observed_delta = modulo_delta;

    if (fixed_frame_words != 0u) {
        const uint32_t frame_delta =
            complete_frames - tracker->last_complete_frames;
        const uint64_t expected_delta =
            (uint64_t)frame_delta * fixed_frame_words;

        /* A modulo write index is exact until more than one complete DMA ring
         * elapses between observations.  A fixed-frame counter can prove the
         * missing full rings, but its edge may lead or trail the DMA tail by
         * one frame.  Only accept the nearest lifted candidate when it is
         * within that physical uncertainty.  This prevents an early frame
         * boundary from manufacturing a 1024-word wrap. */
        if (expected_delta > modulo_delta) {
            const uint64_t missing = expected_delta - modulo_delta;
            const uint64_t complete_rings =
                (missing + tracker->ring_words / 2u) / tracker->ring_words;
            const uint64_t lifted_delta =
                modulo_delta + complete_rings * tracker->ring_words;
            const uint64_t error = lifted_delta >= expected_delta
                ? lifted_delta - expected_delta
                : expected_delta - lifted_delta;
            if (complete_rings != 0u && error <= fixed_frame_words) {
                observed_delta = lifted_delta;
            }
        }
    }

    tracker->produced_words += observed_delta;
    tracker->last_write_index = write_index;
    tracker->last_complete_frames = complete_frames;
    *produced_words = tracker->produced_words;
    return true;
}
