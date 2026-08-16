#include "vdc_timestamp.h"

#include <string.h>

#define VDC_TIMESTAMP_CRC_OFFSET 2166136261u
#define VDC_TIMESTAMP_CRC_PRIME 16777619u

static uint32_t vdc_timestamp_hash_u32(uint32_t hash, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        hash ^= (value >> (i * 8u)) & 0xFFu;
        hash *= VDC_TIMESTAMP_CRC_PRIME;
    }
    return hash;
}

void vdc_timestamp_init_software_us_diagnostic(
    vdc_timestamp_latch_sample_t *sample,
    uint32_t event_id,
    uint32_t source_slot_id,
    uint32_t reference_slot_id,
    uint64_t software_time_ns)
{
    if (sample == NULL) {
        return;
    }

    (void)memset(sample, 0, sizeof(*sample));
    sample->valid = 1u;
    sample->source = VDC_TIMESTAMP_SOURCE_SOFTWARE_US;
    sample->resolution_ns = 1000u;
    sample->flags = VDC_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    sample->event_id = event_id;
    sample->source_slot_id = source_slot_id;
    sample->reference_slot_id = reference_slot_id;
    sample->expected_window_start_ns = software_time_ns;
    sample->observed_time_ns = software_time_ns;
}

void vdc_timestamp_init_hardware_tick_sample(
    vdc_timestamp_latch_sample_t *sample,
    uint32_t event_id,
    uint32_t source_slot_id,
    uint32_t reference_slot_id,
    uint64_t expected_window_start_ns,
    uint64_t observed_time_ns,
    uint32_t resolution_ns,
    uint32_t flags)
{
    if (sample == NULL) {
        return;
    }

    (void)memset(sample, 0, sizeof(*sample));
    sample->valid = 1u;
    sample->source = VDC_TIMESTAMP_SOURCE_HARDWARE_TICK;
    sample->resolution_ns = resolution_ns;
    sample->flags = flags;
    sample->event_id = event_id;
    sample->source_slot_id = source_slot_id;
    sample->reference_slot_id = reference_slot_id;
    sample->expected_window_start_ns = expected_window_start_ns;
    sample->observed_time_ns = observed_time_ns;
}

bool vdc_timestamp_is_diagnostic_only(uint32_t flags)
{
    return (flags & VDC_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0u;
}

bool vdc_timestamp_is_hardware_tick(uint32_t source)
{
    return source == VDC_TIMESTAMP_SOURCE_HARDWARE_TICK;
}

bool vdc_timestamp_dpll_admission_check(
    uint32_t source,
    uint32_t resolution_ns,
    uint32_t flags,
    uint32_t resolution_limit_ns,
    vdc_timestamp_admission_code_t *code)
{
    if (code != NULL) {
        *code = VDC_TIMESTAMP_ADMISSION_PASS;
    }

    if (resolution_limit_ns == 0u) {
        if (code != NULL) {
            *code = VDC_TIMESTAMP_ADMISSION_BAD_ARGUMENT;
        }
        return false;
    }
    if ((flags & VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE) == 0u ||
        vdc_timestamp_is_diagnostic_only(flags) ||
        !vdc_timestamp_is_hardware_tick(source)) {
        if (code != NULL) {
            *code = VDC_TIMESTAMP_ADMISSION_NOT_ELIGIBLE;
        }
        return false;
    }
    if (resolution_ns == 0u || resolution_ns > resolution_limit_ns) {
        if (code != NULL) {
            *code = VDC_TIMESTAMP_ADMISSION_RESOLUTION;
        }
        return false;
    }
    return true;
}

bool vdc_timestamp_observed_in_window(uint64_t expected_window_start_ns,
                                      uint64_t observed_time_ns,
                                      uint32_t window_width_ns,
                                      uint32_t guard_before_ns,
                                      uint32_t guard_after_ns)
{
    const uint64_t window_min =
        expected_window_start_ns > guard_before_ns
            ? expected_window_start_ns - guard_before_ns
            : 0u;
    const uint64_t window_max =
        expected_window_start_ns + (uint64_t)window_width_ns +
        (uint64_t)guard_after_ns;
    return observed_time_ns >= window_min && observed_time_ns <= window_max;
}

void vdc_timestamp_dictionary_init(vdc_timestamp_dictionary_t *dictionary,
                                   uint32_t profile_crc32)
{
    if (dictionary == NULL) {
        return;
    }

    (void)memset(dictionary, 0, sizeof(*dictionary));
    dictionary->valid = 1u;
    dictionary->version = VDC_TIMESTAMP_DICTIONARY_VERSION;
    dictionary->profile_crc32 = profile_crc32;
    dictionary->dictionary_crc32 = vdc_timestamp_dictionary_crc32(dictionary);
}

uint32_t vdc_timestamp_dictionary_crc32(
    const vdc_timestamp_dictionary_t *dictionary)
{
    uint32_t hash = VDC_TIMESTAMP_CRC_OFFSET;
    if (dictionary == NULL) {
        return 0u;
    }

    hash = vdc_timestamp_hash_u32(hash, dictionary->valid);
    hash = vdc_timestamp_hash_u32(hash, dictionary->version);
    hash = vdc_timestamp_hash_u32(hash, dictionary->entry_count);
    hash = vdc_timestamp_hash_u32(hash, dictionary->profile_crc32);
    for (uint32_t i = 0u; i < dictionary->entry_count &&
                         i < VDC_TIMESTAMP_DICTIONARY_MAX_ENTRIES;
         i++) {
        const vdc_timestamp_dictionary_entry_t *entry =
            &dictionary->entries[i];
        hash = vdc_timestamp_hash_u32(hash, entry->valid);
        hash = vdc_timestamp_hash_u32(hash, entry->event_id);
        hash = vdc_timestamp_hash_u32(hash, entry->source_slot_id);
        hash = vdc_timestamp_hash_u32(hash, entry->reference_slot_id);
        hash = vdc_timestamp_hash_u32(hash, entry->source);
        hash = vdc_timestamp_hash_u32(hash, entry->resolution_ns);
        hash = vdc_timestamp_hash_u32(hash, entry->default_flags);
        hash = vdc_timestamp_hash_u32(hash, entry->port_id);
        hash = vdc_timestamp_hash_u32(hash, entry->signal_id);
        hash = vdc_timestamp_hash_u32(hash, entry->payload_class);
    }
    return hash;
}

bool vdc_timestamp_dictionary_validate(
    const vdc_timestamp_dictionary_t *dictionary)
{
    if (dictionary == NULL ||
        dictionary->valid == 0u ||
        dictionary->version != VDC_TIMESTAMP_DICTIONARY_VERSION ||
        dictionary->entry_count > VDC_TIMESTAMP_DICTIONARY_MAX_ENTRIES ||
        dictionary->dictionary_crc32 !=
            vdc_timestamp_dictionary_crc32(dictionary)) {
        return false;
    }

    for (uint32_t i = 0u; i < dictionary->entry_count; i++) {
        const vdc_timestamp_dictionary_entry_t *entry =
            &dictionary->entries[i];
        if (entry->valid == 0u ||
            entry->event_id == 0u ||
            entry->source == VDC_TIMESTAMP_SOURCE_NONE ||
            entry->resolution_ns == 0u) {
            return false;
        }
        for (uint32_t j = i + 1u; j < dictionary->entry_count; j++) {
            if (entry->event_id == dictionary->entries[j].event_id) {
                return false;
            }
        }
    }
    return true;
}

bool vdc_timestamp_dictionary_find(
    const vdc_timestamp_dictionary_t *dictionary,
    uint32_t event_id,
    vdc_timestamp_dictionary_entry_t *entry)
{
    if (!vdc_timestamp_dictionary_validate(dictionary) || event_id == 0u) {
        return false;
    }

    for (uint32_t i = 0u; i < dictionary->entry_count; i++) {
        if (dictionary->entries[i].event_id == event_id) {
            if (entry != NULL) {
                *entry = dictionary->entries[i];
            }
            return true;
        }
    }
    return false;
}

bool vdc_timestamp_dictionary_apply(
    const vdc_timestamp_dictionary_t *dictionary,
    vdc_timestamp_latch_sample_t *sample)
{
    vdc_timestamp_dictionary_entry_t entry;

    if (sample == NULL ||
        sample->valid == 0u ||
        !vdc_timestamp_dictionary_find(dictionary, sample->event_id, &entry)) {
        return false;
    }

    sample->source_slot_id = entry.source_slot_id;
    sample->reference_slot_id = entry.reference_slot_id;
    sample->source = entry.source;
    sample->resolution_ns = entry.resolution_ns;
    sample->flags = entry.default_flags;
    return true;
}

void vdc_wrap_tracker_init(vdc_wrap_tracker_t *tracker,
                           uint32_t initial_tick_l32)
{
    if (tracker == NULL) {
        return;
    }

    (void)memset(tracker, 0, sizeof(*tracker));
    tracker->valid = 1u;
    tracker->last_tick_l32 = initial_tick_l32;
}

bool vdc_wrap_tracker_extend_tick(vdc_wrap_tracker_t *tracker,
                                  uint32_t tick_l32,
                                  uint32_t max_backward_ticks,
                                  uint64_t *tick64)
{
    if (tracker == NULL || tracker->valid == 0u || tick64 == NULL) {
        return false;
    }

    if (tick_l32 < tracker->last_tick_l32) {
        const uint32_t backward_delta = tracker->last_tick_l32 - tick_l32;
        if (backward_delta > VDC_TIMESTAMP_WRAP_HALF_RANGE) {
            tracker->tick_hi64 += 1ull << 32u;
            tracker->wrap_count++;
        } else if (backward_delta > max_backward_ticks) {
            tracker->backward_reject_count++;
            return false;
        }
    } else if (tick_l32 - tracker->last_tick_l32 >
               VDC_TIMESTAMP_WRAP_HALF_RANGE) {
        tracker->backward_reject_count++;
        return false;
    }

    tracker->last_tick_l32 = tick_l32;
    *tick64 = tracker->tick_hi64 | (uint64_t)tick_l32;
    return true;
}
