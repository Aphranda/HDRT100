#ifndef VDC_TIMESTAMP_H
#define VDC_TIMESTAMP_H

#include <stdbool.h>
#include <stdint.h>

#define VDC_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY 0x00000001u
#define VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE   0x00000002u
#define VDC_TIMESTAMP_DICTIONARY_VERSION   1u
#define VDC_TIMESTAMP_DICTIONARY_MAX_ENTRIES 32u
#define VDC_TIMESTAMP_WRAP_HALF_RANGE      0x80000000u

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

typedef struct {
    uint32_t valid;
    uint32_t event_id;
    uint32_t source_slot_id;
    uint32_t reference_slot_id;
    uint32_t source;
    uint32_t resolution_ns;
    uint32_t default_flags;
    uint32_t port_id;
    uint32_t signal_id;
    uint32_t payload_class;
} vdc_timestamp_dictionary_entry_t;

typedef struct {
    uint32_t valid;
    uint32_t version;
    uint32_t entry_count;
    uint32_t profile_crc32;
    uint32_t dictionary_crc32;
    vdc_timestamp_dictionary_entry_t entries[VDC_TIMESTAMP_DICTIONARY_MAX_ENTRIES];
} vdc_timestamp_dictionary_t;

typedef struct {
    uint32_t valid;
    uint32_t anchor_valid;
    uint32_t last_tick_l32;
    uint64_t tick_hi64;
    uint32_t wrap_count;
    uint32_t backward_reject_count;
} vdc_wrap_tracker_t;

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
void vdc_timestamp_dictionary_init(vdc_timestamp_dictionary_t *dictionary,
                                   uint32_t profile_crc32);
uint32_t vdc_timestamp_dictionary_crc32(
    const vdc_timestamp_dictionary_t *dictionary);
bool vdc_timestamp_dictionary_validate(
    const vdc_timestamp_dictionary_t *dictionary);
bool vdc_timestamp_dictionary_find(
    const vdc_timestamp_dictionary_t *dictionary,
    uint32_t event_id,
    vdc_timestamp_dictionary_entry_t *entry);
bool vdc_timestamp_dictionary_apply(
    const vdc_timestamp_dictionary_t *dictionary,
    vdc_timestamp_latch_sample_t *sample);
void vdc_wrap_tracker_init(vdc_wrap_tracker_t *tracker,
                           uint32_t initial_tick_l32);
void vdc_wrap_tracker_init_open(vdc_wrap_tracker_t *tracker);
void vdc_wrap_tracker_reanchor(vdc_wrap_tracker_t *tracker,
                               uint32_t initial_tick_l32);
bool vdc_wrap_tracker_extend_tick(vdc_wrap_tracker_t *tracker,
                                  uint32_t tick_l32,
                                  uint32_t max_backward_ticks,
                                  uint64_t *tick64);

#endif
