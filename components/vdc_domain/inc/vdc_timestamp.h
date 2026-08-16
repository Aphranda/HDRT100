#ifndef VDC_TIMESTAMP_H
#define VDC_TIMESTAMP_H

#include <stdbool.h>
#include <stdint.h>

#define VDC_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY 0x00000001u
#define VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE   0x00000002u

typedef enum {
    VDC_TIMESTAMP_SOURCE_NONE = 0u,
    VDC_TIMESTAMP_SOURCE_SOFTWARE_US = 1u,
    VDC_TIMESTAMP_SOURCE_HARDWARE_TICK = 2u,
} vdc_timestamp_source_t;

typedef enum {
    VDC_TIMESTAMP_ADMISSION_PASS = 0u,
    VDC_TIMESTAMP_ADMISSION_BAD_ARGUMENT = 1u,
    VDC_TIMESTAMP_ADMISSION_NOT_ELIGIBLE = 2u,
    VDC_TIMESTAMP_ADMISSION_RESOLUTION = 3u,
    VDC_TIMESTAMP_ADMISSION_WINDOW_BOUND = 4u,
} vdc_timestamp_admission_code_t;

typedef struct {
    uint32_t valid;
    uint32_t source;
    uint32_t resolution_ns;
    uint32_t flags;
    uint32_t event_id;
    uint32_t source_slot_id;
    uint32_t reference_slot_id;
    uint64_t expected_window_start_ns;
    uint64_t observed_time_ns;
} vdc_timestamp_latch_sample_t;

void vdc_timestamp_init_software_us_diagnostic(
    vdc_timestamp_latch_sample_t *sample,
    uint32_t event_id,
    uint32_t source_slot_id,
    uint32_t reference_slot_id,
    uint64_t software_time_ns);

void vdc_timestamp_init_hardware_tick_sample(
    vdc_timestamp_latch_sample_t *sample,
    uint32_t event_id,
    uint32_t source_slot_id,
    uint32_t reference_slot_id,
    uint64_t expected_window_start_ns,
    uint64_t observed_time_ns,
    uint32_t resolution_ns,
    uint32_t flags);

bool vdc_timestamp_is_diagnostic_only(uint32_t flags);
bool vdc_timestamp_is_hardware_tick(uint32_t source);
bool vdc_timestamp_dpll_admission_check(
    uint32_t source,
    uint32_t resolution_ns,
    uint32_t flags,
    uint32_t resolution_limit_ns,
    vdc_timestamp_admission_code_t *code);
bool vdc_timestamp_observed_in_window(uint64_t expected_window_start_ns,
                                      uint64_t observed_time_ns,
                                      uint32_t window_width_ns,
                                      uint32_t guard_before_ns,
                                      uint32_t guard_after_ns);

#endif
