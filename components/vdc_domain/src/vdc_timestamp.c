#include "vdc_timestamp.h"

#include <string.h>

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
