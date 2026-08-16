#include "vdc_domain.h"
#include "vdc_sync_io_adapter.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %llu got %llu\n",
                     name,
                     (unsigned long long)expected,
                     (unsigned long long)actual);
        return 1;
    }
    return 0;
}

static int expect_i32(const char *name, int32_t actual, int32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %ld got %ld\n",
                     name,
                     (long)expected,
                     (long)actual);
        return 1;
    }
    return 0;
}

static vdc_tdma_timestamp_evidence_t make_hardware_sample(
    const vdc_tdma_schedule_profile_t *schedule,
    uint32_t sample_seq,
    int32_t phase_error_ns)
{
    vdc_tdma_timestamp_evidence_t evidence;
    (void)memset(&evidence, 0, sizeof(evidence));
    evidence.sample_seq = sample_seq;
    evidence.schedule_epoch = schedule->schedule_epoch;
    evidence.slot_index = 0u;
    evidence.source_slot_id = schedule->reference_slot_id;
    evidence.reference_slot_id = schedule->reference_slot_id;
    evidence.payload_class = VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE;
    evidence.expected_window_start_ns =
        (uint64_t)(sample_seq - 1u) * schedule->period_ns +
        schedule->observation_window_offset_ns;
    evidence.arm_time_ns = evidence.expected_window_start_ns;
    evidence.start_time_ns = evidence.expected_window_start_ns;
    evidence.observed_time_ns = evidence.expected_window_start_ns +
                                (uint32_t)(phase_error_ns < 0 ? 0 : phase_error_ns);
    evidence.done_time_ns = evidence.observed_time_ns + 100u;
    evidence.apply_time_ns = evidence.done_time_ns + 100u;
    evidence.late_ns = phase_error_ns < 0 ? 0u : (uint32_t)phase_error_ns;
    evidence.jitter_ns = 5u;
    evidence.delay_ns = 0u;
    evidence.phase_error_ns = phase_error_ns;
    evidence.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    evidence.timestamp_resolution_ns = 50u;
    evidence.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    evidence.schedule_crc32 = schedule->schedule_crc32;
    evidence.frame_crc32 = 0x1000u + sample_seq;
    evidence.sample_crc32 = 0x2000u + sample_seq;
    return evidence;
}

static vdc_tdma_frame_envelope_t make_frame(
    const vdc_tdma_schedule_profile_t *schedule,
    uint32_t frame_seq,
    uint32_t window_class,
    uint32_t payload_class,
    uint64_t window_start_ns)
{
    vdc_tdma_frame_envelope_t frame;
    (void)memset(&frame, 0, sizeof(frame));
    frame.frame_version = VDC_DOMAIN_TDMA_FRAME_VERSION;
    frame.frame_seq = frame_seq;
    frame.schedule_epoch = schedule->schedule_epoch;
    frame.slot_index = schedule->local_slot_id;
    frame.source_slot_id = schedule->local_slot_id;
    frame.reference_slot_id = schedule->reference_slot_id;
    frame.window_class = window_class;
    frame.payload_class = payload_class;
    frame.window_start_ns = window_start_ns;
    frame.schedule_crc32 = schedule->schedule_crc32;
    frame.frame_crc32 = 0x3000u + frame_seq;
    frame.payload_crc32 = 0x4000u + frame_seq;
    frame.timestamp.sample_seq = frame_seq;
    frame.timestamp.schedule_epoch = schedule->schedule_epoch;
    frame.timestamp.slot_index = schedule->local_slot_id;
    frame.timestamp.source_slot_id = schedule->local_slot_id;
    frame.timestamp.reference_slot_id = schedule->reference_slot_id;
    frame.timestamp.payload_class = payload_class;
    frame.timestamp.expected_window_start_ns = window_start_ns;
    frame.timestamp.observed_time_ns = window_start_ns + 10u;
    frame.timestamp.done_time_ns = window_start_ns + 20u;
    frame.timestamp.apply_time_ns = window_start_ns + 30u;
    frame.timestamp.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US;
    frame.timestamp.timestamp_resolution_ns = 1000u;
    frame.timestamp.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    frame.timestamp.schedule_crc32 = schedule->schedule_crc32;
    frame.timestamp.frame_crc32 = frame.frame_crc32;
    frame.timestamp.sample_crc32 = 0x5000u + frame_seq;
    return frame;
}

static int test_default_schedule_and_clock(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_clock_model_t model;
    uint64_t vdc_ns = 0u;

    vdc_domain_default_schedule(&schedule, 1u, 0u);
    failed += expect_bool("schedule valid",
                          vdc_domain_schedule_validate(&schedule),
                          true);
    failed += expect_u32("reference slot", schedule.reference_slot_id, 0u);
    failed += expect_u32("local slot", schedule.local_slot_id, 1u);
    failed += expect_u32("refmem window offset",
                         schedule.refmem_data_window_offset_ns,
                         VDC_DOMAIN_DEFAULT_REFMEM_WINDOW_OFFSET_NS);
    failed += expect_u32("idle window offset",
                         schedule.idle_beacon_window_offset_ns,
                         VDC_DOMAIN_DEFAULT_IDLE_WINDOW_OFFSET_NS);

    vdc_domain_default_clock_model(&model,
                                   schedule.schedule_epoch,
                                   9u,
                                   1000u,
                                   2000000u,
                                   schedule.schedule_crc32);
    model.period_adjust_ppb = 1000;
    model.phase_offset_ns = -10;
    failed += expect_bool("clock map",
                          vdc_domain_clock_model_local_to_vdc_ns(&model,
                                                                  2000u,
                                                                  &vdc_ns),
                          true);
    failed += expect_u64("clock mapped ns", vdc_ns, 2000990u);
    failed += expect_bool("clock rejects reverse tick",
                          vdc_domain_clock_model_local_to_vdc_ns(&model,
                                                                  999u,
                                                                  &vdc_ns),
                          false);
    return failed;
}

static int test_gate_rejects_diagnostic_timestamp(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_tdma_timestamp_evidence_t evidence;
    vdc_gate_result_t gate;

    vdc_domain_default_schedule(&schedule, 0u, 0u);
    evidence = make_hardware_sample(&schedule, 1u, 0);
    evidence.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US;
    evidence.timestamp_resolution_ns = 1000u;
    evidence.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;

    failed += expect_bool("diagnostic rejected",
                          vdc_domain_validate_tdma_timestamp_evidence(&schedule,
                                                                       &evidence,
                                                                       true,
                                                                       &gate),
                          false);
    failed += expect_u32("diagnostic reject code",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE);
    failed += expect_bool("diagnostic accepted as quality only",
                          vdc_domain_validate_tdma_timestamp_evidence(&schedule,
                                                                       &evidence,
                                                                       false,
                                                                       &gate),
                          true);
    return failed;
}

static int test_timestamp_contract_helpers(void)
{
    int failed = 0;
    vdc_timestamp_latch_sample_t sample;
    vdc_timestamp_admission_code_t code = VDC_TIMESTAMP_ADMISSION_PASS;

    vdc_timestamp_init_software_us_diagnostic(&sample,
                                              7u,
                                              1u,
                                              0u,
                                              123000u);
    failed += expect_u32("software diagnostic valid", sample.valid, 1u);
    failed += expect_u32("software diagnostic source",
                         sample.source,
                         VDC_TIMESTAMP_SOURCE_SOFTWARE_US);
    failed += expect_u32("software diagnostic resolution",
                         sample.resolution_ns,
                         1000u);
    failed += expect_u32("software diagnostic flags",
                         sample.flags,
                         VDC_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
    failed += expect_bool("software diagnostic rejected for dpll",
                          vdc_timestamp_dpll_admission_check(
                              sample.source,
                              sample.resolution_ns,
                              sample.flags,
                              VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS,
                              &code),
                          false);
    failed += expect_u32("software diagnostic reject code",
                         code,
                         VDC_TIMESTAMP_ADMISSION_NOT_ELIGIBLE);

    vdc_timestamp_init_hardware_tick_sample(
        &sample,
        8u,
        1u,
        0u,
        1000000u,
        1000010u,
        VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS + 1u,
        VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE);
    failed += expect_bool("hardware resolution rejected",
                          vdc_timestamp_dpll_admission_check(
                              sample.source,
                              sample.resolution_ns,
                              sample.flags,
                              VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS,
                              &code),
                          false);
    failed += expect_u32("hardware resolution reject code",
                         code,
                         VDC_TIMESTAMP_ADMISSION_RESOLUTION);

    sample.resolution_ns = VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS;
    sample.flags = VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE |
                   VDC_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    failed += expect_bool("hardware diagnostic flag rejected",
                          vdc_timestamp_dpll_admission_check(
                              sample.source,
                              sample.resolution_ns,
                              sample.flags,
                              VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS,
                              &code),
                          false);
    failed += expect_u32("hardware diagnostic reject code",
                         code,
                         VDC_TIMESTAMP_ADMISSION_NOT_ELIGIBLE);

    sample.flags = VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    failed += expect_bool("hardware eligible accepted",
                          vdc_timestamp_dpll_admission_check(
                              sample.source,
                              sample.resolution_ns,
                              sample.flags,
                              VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS,
                              &code),
                          true);
    failed += expect_u32("hardware eligible pass",
                         code,
                         VDC_TIMESTAMP_ADMISSION_PASS);
    failed += expect_bool("timestamp inside guarded window",
                          vdc_timestamp_observed_in_window(1000u,
                                                           900u,
                                                           100u,
                                                           100u,
                                                           50u),
                          true);
    failed += expect_bool("timestamp outside guarded window",
                          vdc_timestamp_observed_in_window(1000u,
                                                           1151u,
                                                           100u,
                                                           100u,
                                                           50u),
                          false);
    return failed;
}

static int test_timestamp_dictionary_contract(void)
{
    int failed = 0;
    vdc_timestamp_dictionary_t dictionary;
    vdc_timestamp_dictionary_entry_t entry;
    vdc_timestamp_latch_sample_t sample;

    vdc_timestamp_dictionary_init(&dictionary, 0x12345678u);
    failed += expect_bool("empty dictionary valid",
                          vdc_timestamp_dictionary_validate(&dictionary),
                          true);

    dictionary.entry_count = 2u;
    dictionary.entries[0].valid = 1u;
    dictionary.entries[0].event_id = 10u;
    dictionary.entries[0].source_slot_id = 1u;
    dictionary.entries[0].reference_slot_id = 0u;
    dictionary.entries[0].source = VDC_TIMESTAMP_SOURCE_HARDWARE_TICK;
    dictionary.entries[0].resolution_ns = 50u;
    dictionary.entries[0].default_flags = VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    dictionary.entries[0].port_id = 3u;
    dictionary.entries[0].signal_id = 4u;
    dictionary.entries[0].payload_class = VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE;
    dictionary.entries[1] = dictionary.entries[0];
    dictionary.entries[1].event_id = 11u;
    dictionary.entries[1].payload_class = VDC_DOMAIN_PAYLOAD_IDLE_BEACON;
    dictionary.dictionary_crc32 =
        vdc_timestamp_dictionary_crc32(&dictionary);

    failed += expect_bool("dictionary valid",
                          vdc_timestamp_dictionary_validate(&dictionary),
                          true);
    failed += expect_bool("dictionary find",
                          vdc_timestamp_dictionary_find(&dictionary,
                                                        10u,
                                                        &entry),
                          true);
    failed += expect_u32("dictionary port", entry.port_id, 3u);
    failed += expect_u32("dictionary signal", entry.signal_id, 4u);

    (void)memset(&sample, 0, sizeof(sample));
    sample.valid = 1u;
    sample.event_id = 10u;
    sample.expected_window_start_ns = 1000u;
    sample.observed_time_ns = 1008u;
    failed += expect_bool("dictionary apply",
                          vdc_timestamp_dictionary_apply(&dictionary, &sample),
                          true);
    failed += expect_u32("expanded source slot", sample.source_slot_id, 1u);
    failed += expect_u32("expanded reference slot", sample.reference_slot_id, 0u);
    failed += expect_u32("expanded source",
                         sample.source,
                         VDC_TIMESTAMP_SOURCE_HARDWARE_TICK);
    failed += expect_u32("expanded resolution", sample.resolution_ns, 50u);
    failed += expect_u32("expanded flags",
                         sample.flags,
                         VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE);

    dictionary.entries[1].event_id = 10u;
    dictionary.dictionary_crc32 =
        vdc_timestamp_dictionary_crc32(&dictionary);
    failed += expect_bool("duplicate event rejected",
                          vdc_timestamp_dictionary_validate(&dictionary),
                          false);

    dictionary.entries[1].event_id = 11u;
    dictionary.dictionary_crc32 ^= 1u;
    failed += expect_bool("dictionary crc rejected",
                          vdc_timestamp_dictionary_validate(&dictionary),
                          false);
    return failed;
}

static int test_wrap_tracker_contract(void)
{
    int failed = 0;
    vdc_wrap_tracker_t tracker;
    uint64_t tick64 = 0u;

    vdc_wrap_tracker_init(&tracker, 0xFFFFFFF0u);
    failed += expect_bool("wrap initial forward",
                          vdc_wrap_tracker_extend_tick(&tracker,
                                                       0xFFFFFFF8u,
                                                       0u,
                                                       &tick64),
                          true);
    failed += expect_u64("wrap initial tick", tick64, 0xFFFFFFF8ull);
    failed += expect_bool("wrap forward",
                          vdc_wrap_tracker_extend_tick(&tracker,
                                                       0x00000008u,
                                                       0u,
                                                       &tick64),
                          true);
    failed += expect_u64("wrap extended tick", tick64, 0x100000008ull);
    failed += expect_u32("wrap count", tracker.wrap_count, 1u);
    failed += expect_bool("stale pre-wrap tick rejected",
                          vdc_wrap_tracker_extend_tick(&tracker,
                                                       0xFFFFFF00u,
                                                       0u,
                                                       &tick64),
                          false);
    failed += expect_u32("stale reject count",
                         tracker.backward_reject_count,
                         1u);
    failed += expect_bool("small backward rejected by zero tolerance",
                          vdc_wrap_tracker_extend_tick(&tracker,
                                                       0x00000004u,
                                                       0u,
                                                       &tick64),
                          false);
    failed += expect_u32("backward reject count",
                         tracker.backward_reject_count,
                         2u);
    return failed;
}

static void make_timestamp_dictionary_for_schedule(
    const vdc_tdma_schedule_profile_t *schedule,
    vdc_timestamp_dictionary_t *dictionary,
    uint32_t event_id,
    uint32_t payload_class)
{
    vdc_timestamp_dictionary_init(dictionary, schedule->schedule_crc32);
    dictionary->entry_count = 1u;
    dictionary->entries[0].valid = 1u;
    dictionary->entries[0].event_id = event_id;
    dictionary->entries[0].source_slot_id = schedule->reference_slot_id;
    dictionary->entries[0].reference_slot_id = schedule->reference_slot_id;
    dictionary->entries[0].source = VDC_TIMESTAMP_SOURCE_HARDWARE_TICK;
    dictionary->entries[0].resolution_ns = 50u;
    dictionary->entries[0].default_flags = VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    dictionary->entries[0].port_id = 1u;
    dictionary->entries[0].signal_id = 2u;
    dictionary->entries[0].payload_class = payload_class;
    dictionary->dictionary_crc32 =
        vdc_timestamp_dictionary_crc32(dictionary);
}

static int test_compact_observation_contract(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_timestamp_dictionary_t dictionary;
    vdc_wrap_tracker_t tracker;
    vdc_compact_observation_sample_t compact;
    vdc_tdma_timestamp_evidence_t evidence;
    vdc_gate_result_t gate;

    vdc_domain_default_schedule(&schedule, 0u, 0u);
    make_timestamp_dictionary_for_schedule(&schedule,
                                           &dictionary,
                                           21u,
                                           VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE);
    vdc_wrap_tracker_init(&tracker, 0u);

    (void)memset(&compact, 0, sizeof(compact));
    compact.valid = 1u;
    compact.sample_seq = 1u;
    compact.event_id = 21u;
    compact.tick_l32 = 10u;
    compact.expected_window_start_ns = schedule.observation_window_offset_ns;
    compact.frame_crc32 = 0x1111u;
    compact.sample_crc32 = 0x2222u;
    compact.jitter_ns = 3u;
    compact.delay_ns = 4u;
    compact.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    compact.timestamp_resolution_ns = 50u;
    compact.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;

    failed += expect_bool("compact observation expands",
                          vdc_domain_expand_compact_observation(&schedule,
                                                                 &dictionary,
                                                                 &tracker,
                                                                 &compact,
                                                                 &evidence,
                                                                 &gate),
                          true);
    failed += expect_u32("compact gate pass",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_PASS);
    failed += expect_u32("compact evidence source",
                         evidence.timestamp_source,
                         VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK);
    failed += expect_u32("compact evidence resolution",
                         evidence.timestamp_resolution_ns,
                         50u);
    failed += expect_u32("compact evidence flags",
                         evidence.timestamp_flags,
                         VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE);
    failed += expect_i32("compact phase error",
                         evidence.phase_error_ns,
                         10);

    dictionary.dictionary_crc32 ^= 1u;
    failed += expect_bool("compact rejects bad dictionary crc",
                          vdc_domain_expand_compact_observation(&schedule,
                                                                 &dictionary,
                                                                 &tracker,
                                                                 &compact,
                                                                 &evidence,
                                                                 &gate),
                          false);
    failed += expect_u32("bad dictionary gate",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_BAD_FRAME);
    dictionary.dictionary_crc32 ^= 1u;

    compact.tick_l32 = 20u;
    compact.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US;
    compact.timestamp_resolution_ns = 1000u;
    compact.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    failed += expect_bool("compact rejects diagnostic source elevation",
                          vdc_domain_expand_compact_observation(&schedule,
                                                                 &dictionary,
                                                                 &tracker,
                                                                 &compact,
                                                                 &evidence,
                                                                 &gate),
                          false);
    failed += expect_u32("diagnostic source gate",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_BAD_FRAME);

    compact.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    compact.timestamp_resolution_ns = 50u;
    compact.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    failed += expect_bool("compact rejects diagnostic hardware flag",
                          vdc_domain_expand_compact_observation(&schedule,
                                                                 &dictionary,
                                                                 &tracker,
                                                                 &compact,
                                                                 &evidence,
                                                                 &gate),
                          false);
    failed += expect_u32("diagnostic flag gate",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE);

    compact.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    compact.tick_l32 = 0xFFFFFF00u;
    failed += expect_bool("compact rejects stale tick",
                          vdc_domain_expand_compact_observation(&schedule,
                                                                 &dictionary,
                                                                 &tracker,
                                                                 &compact,
                                                                 &evidence,
                                                                 &gate),
                          false);
    failed += expect_u32("stale tick gate",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_BAD_FRAME);
    return failed;
}

static int test_context_submits_compact_observation(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_timestamp_dictionary_t dictionary;
    vdc_compact_observation_sample_t compact;

    failed += expect_bool("compact context init",
                          vdc_domain_init(&context),
                          true);
    vdc_domain_set_ready(&context, true);
    make_timestamp_dictionary_for_schedule(&context.schedule,
                                           &dictionary,
                                           31u,
                                           VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE);
    failed += expect_bool("publish timestamp dictionary",
                          vdc_domain_publish_timestamp_dictionary(&context,
                                                                  &dictionary,
                                                                  0u),
                          true);

    (void)memset(&compact, 0, sizeof(compact));
    compact.valid = 1u;
    compact.sample_seq = 1u;
    compact.event_id = 31u;
    compact.tick_l32 = 5u;
    compact.expected_window_start_ns =
        context.schedule.observation_window_offset_ns;
    compact.frame_crc32 = 0x3333u;
    compact.sample_crc32 = 0x4444u;
    compact.jitter_ns = 2u;
    compact.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    compact.timestamp_resolution_ns = 50u;
    compact.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    failed += expect_bool("submit compact observation",
                          vdc_domain_submit_compact_observation(&context,
                                                                &compact),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("compact accepted count",
                         snapshot.dpll.accepted_sample_count,
                         1u);
    failed += expect_u32("compact gate pass snapshot",
                         snapshot.gate.reject_code,
                         VDC_DOMAIN_GATE_PASS);
    failed += expect_u32("compact quality source",
                         snapshot.quality.last_timestamp_source,
                         VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK);

    compact.sample_seq = 2u;
    compact.event_id = 99u;
    compact.tick_l32 = 10u;
    failed += expect_bool("submit compact bad event",
                          vdc_domain_submit_compact_observation(&context,
                                                                &compact),
                          false);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("compact rejected count",
                         snapshot.dpll.rejected_sample_count,
                         1u);
    failed += expect_u32("compact reject gate",
                         snapshot.gate.reject_code,
                         VDC_DOMAIN_GATE_BAD_FRAME);
    return failed;
}

static int test_sync_io_adapter_contract(void)
{
    int failed = 0;
    vdc_sync_io_capture_decode_config_t config;
    vdc_compact_observation_sample_t compact;
    uint32_t last_sample_mask = 0u;

    (void)memset(&config, 0, sizeof(config));
    config.valid = 1u;
    config.sample_seq = 12u;
    config.rising_event_id = 41u;
    config.falling_event_id = 42u;
    config.observed_mask = 0x1u;
    config.previous_sample_mask = 0u;
    config.base_time_l32_ns = 100u;
    config.sample_period_ns = 40u;
    config.expected_window_start_ns = 0u;
    config.frame_crc32 = 0x5555u;
    config.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    config.timestamp_resolution_ns = 50u;
    config.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    config.sample0_lsb = true;

    const uint32_t raw_rising_at_sample2 =
        (1u << 8) | (1u << 12) | (1u << 16) | (1u << 20) |
        (1u << 24) | (1u << 28);
    failed += expect_u32("sync io adapter rising result",
                         vdc_sync_io_capture_word_to_compact_observation(
                             &config,
                             raw_rising_at_sample2,
                             &compact,
                             &last_sample_mask),
                         VDC_SYNC_IO_CAPTURE_OK);
    failed += expect_u32("sync io adapter valid", compact.valid, 1u);
    failed += expect_u32("sync io adapter event", compact.event_id, 41u);
    failed += expect_u32("sync io adapter tick", compact.tick_l32, 180u);
    failed += expect_u32("sync io adapter frame crc",
                         compact.frame_crc32,
                         0x5555u);
    failed += expect_bool("sync io adapter sample crc nonzero",
                          compact.sample_crc32 != 0u,
                          true);
    failed += expect_u32("sync io adapter last mask", last_sample_mask, 1u);
    failed += expect_u32("sync io adapter timestamp source",
                         compact.timestamp_source,
                         VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK);
    failed += expect_u32("sync io adapter timestamp resolution",
                         compact.timestamp_resolution_ns,
                         50u);
    failed += expect_u32("sync io adapter timestamp flags",
                         compact.timestamp_flags,
                         VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE);

    config.previous_sample_mask = last_sample_mask;
    failed += expect_u32("sync io adapter no edge",
                         vdc_sync_io_capture_word_to_compact_observation(
                             &config,
                             0x11111111u,
                             &compact,
                             &last_sample_mask),
                         VDC_SYNC_IO_CAPTURE_NO_EDGE);

    config.previous_sample_mask = 0u;
    failed += expect_u32("sync io adapter ambiguous",
                         vdc_sync_io_capture_word_to_compact_observation(
                             &config,
                             0x00000001u,
                             &compact,
                             &last_sample_mask),
                         VDC_SYNC_IO_CAPTURE_AMBIGUOUS_EDGE);
    return failed;
}

static int test_sync_io_adapter_to_vdc_submit(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_timestamp_dictionary_t dictionary;
    vdc_sync_io_capture_decode_config_t config;
    vdc_compact_observation_sample_t compact;
    vdc_domain_snapshot_t snapshot;
    uint32_t last_sample_mask = 0u;

    failed += expect_bool("sync io vdc context init",
                          vdc_domain_init(&context),
                          true);
    vdc_domain_set_ready(&context, true);
    make_timestamp_dictionary_for_schedule(&context.schedule,
                                           &dictionary,
                                           51u,
                                           VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE);
    failed += expect_bool("sync io vdc dictionary publish",
                          vdc_domain_publish_timestamp_dictionary(&context,
                                                                  &dictionary,
                                                                  0u),
                          true);

    (void)memset(&config, 0, sizeof(config));
    config.valid = 1u;
    config.sample_seq = 1u;
    config.rising_event_id = 51u;
    config.falling_event_id = 52u;
    config.observed_mask = 0x1u;
    config.previous_sample_mask = 0u;
    config.base_time_l32_ns = 0u;
    config.sample_period_ns = 40u;
    config.expected_window_start_ns = 0u;
    config.frame_crc32 = 0x6666u;
    config.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    config.timestamp_resolution_ns = 50u;
    config.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    config.sample0_lsb = true;
    failed += expect_u32("sync io vdc adapter result",
                         vdc_sync_io_capture_word_to_compact_observation(
                             &config,
                             0x11111110u,
                             &compact,
                             &last_sample_mask),
                         VDC_SYNC_IO_CAPTURE_OK);
    failed += expect_bool("sync io vdc submit",
                          vdc_domain_submit_compact_observation(&context,
                                                                &compact),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("sync io vdc accepted",
                         snapshot.dpll.accepted_sample_count,
                         1u);
    failed += expect_i32("sync io vdc phase",
                         snapshot.dpll.last_phase_error_ns,
                         40);
    return failed;
}

static int test_gate_rejects_schedule_and_window_mismatch(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_tdma_timestamp_evidence_t evidence;
    vdc_gate_result_t gate;

    vdc_domain_default_schedule(&schedule, 0u, 0u);
    evidence = make_hardware_sample(&schedule, 1u, 0);
    evidence.schedule_crc32 ^= 0x1u;
    failed += expect_bool("crc mismatch rejected",
                          vdc_domain_validate_tdma_timestamp_evidence(&schedule,
                                                                       &evidence,
                                                                       true,
                                                                       &gate),
                          false);
    failed += expect_u32("crc reject code",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH);

    evidence = make_hardware_sample(&schedule, 1u, 0);
    evidence.observed_time_ns =
        evidence.expected_window_start_ns +
        schedule.observation_window_width_ns +
        schedule.guard_after_ns +
        1u;
    failed += expect_bool("window mismatch rejected",
                          vdc_domain_validate_tdma_timestamp_evidence(&schedule,
                                                                       &evidence,
                                                                       true,
                                                                       &gate),
                          false);
    failed += expect_u32("window reject code",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_WINDOW_BOUND);

    schedule.refmem_data_window_offset_ns = 0u;
    schedule.schedule_crc32 = vdc_domain_schedule_crc32(&schedule);
    failed += expect_bool("overlap rejected",
                          vdc_domain_schedule_validate(&schedule),
                          false);
    return failed;
}

static int test_frame_envelope_window_contract(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_tdma_frame_envelope_t frame;
    vdc_gate_result_t gate;

    vdc_domain_default_schedule(&schedule, 1u, 0u);
    frame = make_frame(&schedule,
                       1u,
                       VDC_DOMAIN_WINDOW_REFMEM_DATA,
                       VDC_DOMAIN_PAYLOAD_REFMEM_DELTA,
                       schedule.refmem_data_window_offset_ns);
    failed += expect_bool("refmem data frame accepted as quality frame",
                          vdc_domain_validate_tdma_frame_envelope(&schedule,
                                                                   &frame,
                                                                   false,
                                                                   &gate),
                          true);
    failed += expect_bool("refmem data frame rejected as dpll sample",
                          vdc_domain_validate_tdma_frame_envelope(&schedule,
                                                                   &frame,
                                                                   true,
                                                                   &gate),
                          false);
    failed += expect_u32("refmem reject dpll code",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE);

    frame.payload_class = VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE;
    frame.timestamp.payload_class = VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE;
    failed += expect_bool("sync sample forbidden in refmem window",
                          vdc_domain_validate_tdma_frame_envelope(&schedule,
                                                                   &frame,
                                                                   false,
                                                                   &gate),
                          false);
    failed += expect_u32("payload window reject code",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_PAYLOAD_WINDOW_FORBIDDEN);

    frame = make_frame(&schedule,
                       2u,
                       VDC_DOMAIN_WINDOW_VDC_OBSERVATION,
                       VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE,
                       schedule.observation_window_offset_ns);
    frame.source_slot_id = schedule.reference_slot_id;
    frame.slot_index = schedule.reference_slot_id;
    frame.timestamp.source_slot_id = schedule.reference_slot_id;
    frame.timestamp.slot_index = schedule.reference_slot_id;
    frame.timestamp.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    frame.timestamp.timestamp_resolution_ns = 50u;
    frame.timestamp.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    failed += expect_bool("observation frame accepted as dpll sample",
                          vdc_domain_validate_tdma_frame_envelope(&schedule,
                                                                   &frame,
                                                                   true,
                                                                   &gate),
                          true);
    return failed;
}

static int test_tdma_window_plan_contract(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_tdma_window_plan_t plan;
    vdc_gate_result_t gate;

    vdc_domain_default_schedule(&schedule, 3u, 0u);

    failed += expect_bool("plan before data window",
                          vdc_domain_plan_tdma_window(&schedule,
                                                       VDC_DOMAIN_WINDOW_REFMEM_DATA,
                                                       15000u,
                                                       &plan,
                                                       &gate),
                          true);
    failed += expect_u32("plan valid", plan.valid, 1u);
    failed += expect_u32("plan pass", gate.reject_code, VDC_DOMAIN_GATE_PASS);
    failed += expect_u64("plan window start", plan.window_start_ns, 20000u);
    failed += expect_u64("plan window end", plan.window_end_ns, 820000u);
    failed += expect_u64("plan guard start", plan.guard_start_ns, 19000u);
    failed += expect_u64("plan guard end", plan.guard_end_ns, 821000u);
    failed += expect_u32("plan wait", plan.wait_ns, 5000u);
    failed += expect_u32("plan guarded", plan.in_guarded_window, 0u);
    failed += expect_u32("plan payload", plan.inside_payload_window, 0u);

    failed += expect_bool("plan inside data window",
                          vdc_domain_plan_tdma_window(&schedule,
                                                       VDC_DOMAIN_WINDOW_REFMEM_DATA,
                                                       21000u,
                                                       &plan,
                                                       &gate),
                          true);
    failed += expect_u32("inside wait", plan.wait_ns, 0u);
    failed += expect_u32("inside late", plan.late_ns, 1000u);
    failed += expect_u32("inside guarded", plan.in_guarded_window, 1u);
    failed += expect_u32("inside payload", plan.inside_payload_window, 1u);

    failed += expect_bool("plan after guard moves to next cycle",
                          vdc_domain_plan_tdma_window(&schedule,
                                                       VDC_DOMAIN_WINDOW_REFMEM_DATA,
                                                       840000u,
                                                       &plan,
                                                       &gate),
                          true);
    failed += expect_u64("next window start", plan.window_start_ns, 1020000u);
    failed += expect_u32("next wait", plan.wait_ns, 180000u);
    failed += expect_u32("missed window", plan.missed_current_window, 1u);

    failed += expect_bool("plan rejects bad window",
                          vdc_domain_plan_tdma_window(&schedule,
                                                       99u,
                                                       0u,
                                                       &plan,
                                                       &gate),
                          false);
    failed += expect_u32("bad window reject",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_BAD_WINDOW_CLASS);
    return failed;
}

static int test_dco_control_contract(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_clock_model_t model;
    vdc_dco_control_t dco;

    failed += expect_bool("init for dco", vdc_domain_init(&context), true);
    failed += expect_bool("dco snapshot",
                          vdc_domain_get_snapshot(&context, &snapshot),
                          true);
    failed += expect_u32("dco valid", snapshot.dco.valid, 1u);
    failed += expect_u32("dco seq", snapshot.dco.dco_update_seq, 1u);
    failed += expect_u32("dco lock state",
                         snapshot.dco.lock_state,
                         VDC_DOMAIN_LOCK_OFF);
    failed += expect_u32("dco schedule crc",
                         snapshot.dco.tdma_schedule_crc32,
                         context.schedule.schedule_crc32);

    model = context.clock;
    model.phase_offset_ns = -25;
    model.period_adjust_ppb = 100;
    model.base_local_tick64 = 500u;
    model.base_vdc_time64_ns = 1000000u;
    failed += expect_bool("publish clock model",
                          vdc_domain_publish_clock_model(&context, &model),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("dco seq after clock publish",
                         snapshot.dco.dco_update_seq,
                         2u);
    failed += expect_i32("dco phase mirrors clock",
                         snapshot.dco.phase_offset_ns,
                         -25);
    failed += expect_i32("dco rate mirrors clock",
                         snapshot.dco.period_adjust_ppb,
                         100);
    failed += expect_u32("dco source model seq",
                         snapshot.dco.source_model_seq,
                         snapshot.clock.model_seq);

    dco = snapshot.dco;
    dco.slew_limit_ppb = context.servo.sanity_freq_limit_ppb + 1u;
    failed += expect_bool("invalid dco rejected",
                          vdc_domain_publish_dco_control(&context, &dco),
                          false);

    dco = snapshot.dco;
    dco.phase_offset_ns = 33;
    failed += expect_bool("publish dco",
                          vdc_domain_publish_dco_control(&context, &dco),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("dco seq after publish",
                         snapshot.dco.dco_update_seq,
                         3u);
    failed += expect_i32("published dco phase",
                         snapshot.dco.phase_offset_ns,
                         33);
    return failed;
}

static int test_context_accepts_samples_until_locked(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;

    failed += expect_bool("init", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    vdc_domain_service(&context, 1000000u);
    failed += expect_bool("snapshot",
                          vdc_domain_get_snapshot(&context, &snapshot),
                          true);
    failed += expect_u32("checking", snapshot.dpll.state, VDC_DOMAIN_LOCK_CHECKING);

    for (uint32_t i = 1u; i <= 4u; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, 10);
        failed += expect_bool("submit evidence",
                              vdc_domain_submit_tdma_evidence(&context, &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("locked", snapshot.dpll.state, VDC_DOMAIN_LOCK_LOCKED);
    failed += expect_u32("accepted", snapshot.dpll.accepted_sample_count, 4u);
    failed += expect_u32("gate pass", snapshot.gate.passed, 1u);
    failed += expect_u32("last pass seq", snapshot.gate.last_pass_seq, 4u);
    failed += expect_u32("quality valid", snapshot.quality.valid, 1u);
    failed += expect_u32("quality healthy",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_HEALTHY);
    failed += expect_u32("quality accepted",
                         snapshot.quality.accepted_sample_count,
                         4u);
    failed += expect_u32("quality timestamp resolution",
                         snapshot.quality.last_timestamp_resolution_ns,
                         50u);
    failed += expect_i32("budget last offset",
                         snapshot.error_budget.last_offset_ns,
                         10);
    failed += expect_i32("clock phase from dpll",
                         snapshot.clock.phase_offset_ns,
                         -10);
    failed += expect_i32("dco phase from dpll",
                         snapshot.dco.phase_offset_ns,
                         -10);
    failed += expect_u32("budget root distance",
                         snapshot.error_budget.root_distance_ns,
                         15u);

    vdc_tdma_timestamp_evidence_t bad = make_hardware_sample(&context.schedule, 5u, 0);
    bad.reference_slot_id = 7u;
    failed += expect_bool("bad evidence rejected",
                          vdc_domain_submit_tdma_evidence(&context, &bad),
                          false);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("rejected", snapshot.dpll.rejected_sample_count, 1u);
    failed += expect_u32("checking after reject",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_CHECKING);
    failed += expect_u32("quality bad count",
                         snapshot.quality.consecutive_bad_samples,
                         1u);
    failed += expect_u32("quality reject code",
                         snapshot.quality.gate_reject_code,
                         VDC_DOMAIN_GATE_REFERENCE_MISMATCH);
    return failed;
}

static int test_dpll_updates_clock_rate_from_sample_period(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_tdma_timestamp_evidence_t first;
    vdc_tdma_timestamp_evidence_t second;

    failed += expect_bool("init rate", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);

    first = make_hardware_sample(&context.schedule, 1u, 0);
    second = make_hardware_sample(&context.schedule, 11u, 1);
    failed += expect_bool("submit first rate sample",
                          vdc_domain_submit_tdma_evidence(&context, &first),
                          true);
    failed += expect_bool("submit second rate sample",
                          vdc_domain_submit_tdma_evidence(&context, &second),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("frequency error ppb",
                         snapshot.dpll.last_frequency_error_ppb,
                         100);
    failed += expect_i32("clock period adjust ppb",
                         snapshot.clock.period_adjust_ppb,
                         -100);
    failed += expect_i32("budget frequency ppb",
                         snapshot.error_budget.freq_offset_ppb,
                         100);
    return failed;
}

static int test_quality_age_updates_on_service(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_tdma_timestamp_evidence_t evidence;

    failed += expect_bool("init age", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    evidence = make_hardware_sample(&context.schedule, 1u, 0);
    evidence.apply_time_ns = 1000u;
    failed += expect_bool("submit age evidence",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    vdc_domain_service(&context, 51000u);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("sample age 1e3ns",
                         snapshot.quality.last_sample_age_1e3ns,
                         50u);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_default_schedule_and_clock();
    failed += test_timestamp_contract_helpers();
    failed += test_timestamp_dictionary_contract();
    failed += test_wrap_tracker_contract();
    failed += test_compact_observation_contract();
    failed += test_sync_io_adapter_contract();
    failed += test_gate_rejects_diagnostic_timestamp();
    failed += test_gate_rejects_schedule_and_window_mismatch();
    failed += test_frame_envelope_window_contract();
    failed += test_tdma_window_plan_contract();
    failed += test_dco_control_contract();
    failed += test_context_accepts_samples_until_locked();
    failed += test_context_submits_compact_observation();
    failed += test_sync_io_adapter_to_vdc_submit();
    failed += test_quality_age_updates_on_service();
    failed += test_dpll_updates_clock_rate_from_sample_period();
    if (failed != 0) {
        (void)printf("vdc_domain tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("vdc_domain tests passed\n");
    return 0;
}
