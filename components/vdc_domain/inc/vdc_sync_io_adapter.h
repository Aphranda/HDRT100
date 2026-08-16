#ifndef VDC_SYNC_IO_ADAPTER_H
#define VDC_SYNC_IO_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "vdc_domain.h"

#define VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD 8u
#define VDC_SYNC_IO_CAPTURE_SAMPLE_BITS      4u
#define VDC_SYNC_IO_CAPTURE_SAMPLE_MASK      0x0Fu

typedef enum {
    VDC_SYNC_IO_CAPTURE_OK = 0u,
    VDC_SYNC_IO_CAPTURE_BAD_ARGUMENT = 1u,
    VDC_SYNC_IO_CAPTURE_NO_EDGE = 2u,
    VDC_SYNC_IO_CAPTURE_AMBIGUOUS_EDGE = 3u,
} vdc_sync_io_capture_result_t;

typedef struct {
    uint32_t valid;
    uint32_t sample_seq;
    uint32_t rising_event_id;
    uint32_t falling_event_id;
    uint32_t observed_mask;
    uint32_t previous_sample_mask;
    uint32_t base_time_l32_ns;
    uint32_t sample_period_ns;
    uint64_t expected_window_start_ns;
    uint32_t frame_crc32;
    uint32_t max_backward_ticks;
    uint32_t quality_flags;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    bool sample0_lsb;
} vdc_sync_io_capture_decode_config_t;

vdc_sync_io_capture_result_t vdc_sync_io_capture_word_to_compact_observation(
    const vdc_sync_io_capture_decode_config_t *config,
    uint32_t raw_word,
    vdc_compact_observation_sample_t *compact,
    uint32_t *last_sample_mask);

#endif
