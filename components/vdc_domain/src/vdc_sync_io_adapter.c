#include "vdc_sync_io_adapter.h"

#include <string.h>

#define VDC_SYNC_IO_ADAPTER_CRC_OFFSET 2166136261u
#define VDC_SYNC_IO_ADAPTER_CRC_PRIME 16777619u

static uint32_t vdc_sync_io_adapter_hash_u32(uint32_t hash, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        hash ^= (value >> (i * 8u)) & 0xFFu;
        hash *= VDC_SYNC_IO_ADAPTER_CRC_PRIME;
    }
    return hash;
}

static uint32_t vdc_sync_io_adapter_sample_at(uint32_t raw_word,
                                              uint32_t sample_index,
                                              bool sample0_lsb)
{
    const uint32_t index = sample0_lsb
                               ? sample_index
                               : (VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD -
                                  1u - sample_index);
    const uint32_t shift = index * VDC_SYNC_IO_CAPTURE_SAMPLE_BITS;
    return (raw_word >> shift) & VDC_SYNC_IO_CAPTURE_SAMPLE_MASK;
}

vdc_sync_io_capture_result_t vdc_sync_io_capture_word_to_compact_observation(
    const vdc_sync_io_capture_decode_config_t *config,
    uint32_t raw_word,
    vdc_compact_observation_sample_t *compact,
    uint32_t *last_sample_mask)
{
    if (compact != NULL) {
        (void)memset(compact, 0, sizeof(*compact));
    }
    if (config == NULL || compact == NULL || last_sample_mask == NULL ||
        config->valid == 0u ||
        config->observed_mask == 0u ||
        config->sample_period_ns == 0u ||
        config->frame_crc32 == 0u ||
        config->timestamp_source == 0u ||
        config->timestamp_resolution_ns == 0u) {
        return VDC_SYNC_IO_CAPTURE_BAD_ARGUMENT;
    }

    uint32_t previous = config->previous_sample_mask & config->observed_mask;
    uint32_t found_index = VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD;
    uint32_t found_event = 0u;
    bool ambiguous = false;

    for (uint32_t i = 0u; i < VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD; i++) {
        const uint32_t current =
            vdc_sync_io_adapter_sample_at(raw_word, i, config->sample0_lsb) &
            config->observed_mask;
        const uint32_t rising = current & ~previous;
        const uint32_t falling = previous & ~current;
        if (rising != 0u || falling != 0u) {
            if ((rising != 0u && falling != 0u) ||
                (rising != 0u && config->rising_event_id == 0u) ||
                (falling != 0u && config->falling_event_id == 0u)) {
                ambiguous = true;
                break;
            }
            if (found_index != VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD) {
                ambiguous = true;
                break;
            }
            found_index = i;
            found_event =
                rising != 0u ? config->rising_event_id : config->falling_event_id;
        }
        previous = current;
    }

    *last_sample_mask = previous;
    if (ambiguous) {
        return VDC_SYNC_IO_CAPTURE_AMBIGUOUS_EDGE;
    }
    if (found_index == VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD) {
        return VDC_SYNC_IO_CAPTURE_NO_EDGE;
    }

    const uint32_t tick_l32 =
        config->base_time_l32_ns + found_index * config->sample_period_ns;
    const uint32_t sample_crc32 =
        vdc_sync_io_adapter_hash_u32(
            vdc_sync_io_adapter_hash_u32(
                vdc_sync_io_adapter_hash_u32(VDC_SYNC_IO_ADAPTER_CRC_OFFSET,
                                             raw_word),
                config->sample_seq),
            found_index);

    compact->valid = 1u;
    compact->sample_seq = config->sample_seq;
    compact->event_id = found_event;
    compact->tick_l32 = tick_l32;
    compact->max_backward_ticks = config->max_backward_ticks;
    compact->expected_window_start_ns = config->expected_window_start_ns;
    compact->frame_crc32 = config->frame_crc32;
    compact->sample_crc32 = sample_crc32;
    compact->quality_flags =
        config->quality_flags |
        ((found_index & 0xFFu) << 16) |
        ((*last_sample_mask & VDC_SYNC_IO_CAPTURE_SAMPLE_MASK) << 24);
    compact->timestamp_source = config->timestamp_source;
    compact->timestamp_resolution_ns = config->timestamp_resolution_ns;
    compact->timestamp_flags = config->timestamp_flags;
    return VDC_SYNC_IO_CAPTURE_OK;
}
