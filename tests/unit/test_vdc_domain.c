#include "vdc_domain.h"
#include "vdc_sync_io_adapter.h"
#include "vdc_tdma_payload.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t rx_frame[TDMA_SERVICE_FRAME_MAX];
    size_t rx_frame_size;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t tx_count;
    uint32_t rx_count;
} fake_tdma_ops_context_t;

static bool fake_tdma_transmit(void *context,
                               const uint8_t *frame,
                               size_t frame_size,
                               tdma_service_role_t role,
                               uint32_t baud_hz,
                               const tdma_service_pin_config_t *pins,
                               uint32_t deadline_1e3ns,
                               tdma_service_exec_status_t *status)
{
    fake_tdma_ops_context_t *fake = (fake_tdma_ops_context_t *)context;
    (void)frame;
    (void)role;
    (void)baud_hz;
    (void)pins;
    (void)deadline_1e3ns;
    if (fake == NULL || status == NULL) {
        return false;
    }
    fake->tx_count++;
    status->result = tdma_service_EXEC_TX_OK;
    status->error = 0u;
    status->frame_size = frame_size;
    status->timestamp_source = fake->timestamp_source;
    status->timestamp_resolution_ns = fake->timestamp_resolution_ns;
    status->timestamp_flags = fake->timestamp_flags;
    return true;
}

static bool fake_tdma_receive(void *context,
                              uint8_t *frame,
                              size_t frame_capacity,
                              tdma_service_role_t role,
                              uint32_t baud_hz,
                              const tdma_service_pin_config_t *pins,
                              uint32_t deadline_1e3ns,
                              tdma_service_exec_status_t *status)
{
    fake_tdma_ops_context_t *fake = (fake_tdma_ops_context_t *)context;
    (void)role;
    (void)baud_hz;
    (void)pins;
    (void)deadline_1e3ns;
    if (fake == NULL || frame == NULL || status == NULL ||
        fake->rx_frame_size == 0u ||
        fake->rx_frame_size > frame_capacity) {
        return false;
    }
    memcpy(frame, fake->rx_frame, fake->rx_frame_size);
    fake->rx_count++;
    status->result = tdma_service_EXEC_RX_OK;
    status->error = 0u;
    status->frame_size = fake->rx_frame_size;
    status->timestamp_source = fake->timestamp_source;
    status->timestamp_resolution_ns = fake->timestamp_resolution_ns;
    status->timestamp_flags = fake->timestamp_flags;
    return true;
}

static const tdma_service_ops_t s_fake_tdma_ops = {
    .transmit = fake_tdma_transmit,
    .receive = fake_tdma_receive,
};

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

static int test_default_timestamp_dictionary_contract(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_compact_observation_sample_t compact;
    vdc_domain_snapshot_t snapshot;
    vdc_timestamp_dictionary_entry_t entry;

    failed += expect_bool("default dictionary init",
                          vdc_domain_init(&context),
                          true);
    failed += expect_bool("default dictionary valid",
                          vdc_timestamp_dictionary_validate(
                              &context.timestamp_dictionary),
                          true);
    failed += expect_u32("default dictionary count",
                         context.timestamp_dictionary.entry_count,
                         2u);
    failed += expect_bool("default dictionary event 1",
                          vdc_timestamp_dictionary_find(
                              &context.timestamp_dictionary,
                              1u,
                              &entry),
                          true);
    failed += expect_u32("default event 1 source",
                         entry.source,
                         VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK);
    failed += expect_u32("default event 1 payload",
                         entry.payload_class,
                         VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE);
    failed += expect_bool("default dictionary event 2",
                          vdc_timestamp_dictionary_find(
                              &context.timestamp_dictionary,
                              2u,
                              &entry),
                          true);
    failed += expect_u32("default event 2 source",
                         entry.source,
                         VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK);
    failed += expect_u32("default event 2 payload",
                         entry.payload_class,
                         VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE);

    vdc_domain_set_ready(&context, true);
    (void)memset(&compact, 0, sizeof(compact));
    compact.valid = 1u;
    compact.sample_seq = 1u;
    compact.event_id = 2u;
    compact.tick_l32 = 0xF0000000u;
    compact.expected_window_start_ns = 0u;
    compact.frame_crc32 = 0x1111u;
    compact.sample_crc32 = 0x2222u;
    compact.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    compact.timestamp_resolution_ns = 4u;
    compact.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    failed += expect_bool("default dictionary diagnostic rejected",
                          vdc_domain_submit_compact_observation(&context,
                                                                &compact),
                          false);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("default diagnostic gate",
                         snapshot.quality.gate_reject_code,
                         VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE);
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

    vdc_wrap_tracker_init_open(&tracker);
    failed += expect_bool("open anchor accepts high first tick",
                          vdc_wrap_tracker_extend_tick(&tracker,
                                                       0xF0000000u,
                                                       0u,
                                                       &tick64),
                          true);
    failed += expect_u64("open anchor high tick", tick64, 0xF0000000ull);
    failed += expect_u32("open anchor valid",
                         tracker.anchor_valid,
                         1u);
    failed += expect_bool("open anchor forward wrap",
                          vdc_wrap_tracker_extend_tick(&tracker,
                                                       0x00000010u,
                                                       0u,
                                                       &tick64),
                          true);
    failed += expect_u64("open anchor wrapped tick", tick64, 0x100000010ull);
    vdc_wrap_tracker_reanchor(&tracker, 0x00000020u);
    failed += expect_bool("reanchor preserves high word",
                          vdc_wrap_tracker_extend_tick(&tracker,
                                                       0x00000030u,
                                                       0u,
                                                       &tick64),
                          true);
    failed += expect_u64("reanchor extended tick",
                         tick64,
                         0x100000030ull);
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
    failed += expect_u32("sync io adapter ambiguous final mask",
                         last_sample_mask,
                         0u);
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

static int test_vdc_tdma_payload_mounts_on_common_tdma(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t domain_snapshot;
    vdc_tdma_schedule_profile_t schedule;
    vdc_tdma_window_plan_t plan;
    vdc_tdma_frame_envelope_t envelope;
    vdc_tdma_frame_envelope_t parsed;
    vdc_tdma_payload_status_t payload_status;
    vdc_tdma_timestamp_evidence_t evidence;
    vdc_gate_result_t gate;
    tdma_service_service_t service;
    tdma_service_snapshot_t snapshot;
    fake_tdma_ops_context_t fake = {0};
    uint8_t frame[VDC_TDMA_PAYLOAD_FRAME_SIZE];
    size_t frame_size = 0u;

    vdc_domain_default_schedule(&schedule, 0u, 0u);
    failed += expect_bool("vdc observation plan",
                          vdc_domain_plan_tdma_window(
                              &schedule,
                              VDC_DOMAIN_WINDOW_VDC_OBSERVATION,
                              0u,
                              &plan,
                              &gate),
                          true);
    evidence = make_hardware_sample(&schedule, 1u, 0);
    evidence.expected_window_start_ns = plan.window_start_ns;
    evidence.arm_time_ns = plan.guard_start_ns;
    evidence.start_time_ns = plan.window_start_ns;
    evidence.observed_time_ns = plan.window_start_ns;
    evidence.done_time_ns = plan.window_start_ns + 100u;
    evidence.apply_time_ns = evidence.done_time_ns;

    failed += expect_bool("build vdc tdma frame",
                          vdc_tdma_payload_build_frame(
                              &schedule,
                              &plan,
                              VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE,
                              1u,
                              &evidence,
                              frame,
                              sizeof(frame),
                              &frame_size,
                              &envelope,
                              &payload_status),
                          true);
    failed += expect_u32("vdc frame size",
                         (uint32_t)frame_size,
                         VDC_TDMA_PAYLOAD_FRAME_SIZE);
    failed += expect_u32("vdc payload status",
                         payload_status.result,
                         VDC_TDMA_PAYLOAD_OK);

    failed += expect_bool("common tdma init", tdma_service_init(&service), true);
    failed += expect_bool("vdc payload register",
                          vdc_tdma_payload_register(&service),
                          true);
    failed += expect_bool("common tdma bind",
                          tdma_service_bind_ops(&service,
                                                &s_fake_tdma_ops,
                                                &fake),
                          true);

    const tdma_service_intent_config_t tx_config = {
        .window_epoch = schedule.schedule_epoch,
        .window_index = 1u,
        .deadline_1e3ns = 25u,
        .role = TDMA_SERVICE_ROLE_MASTER,
        .baud_hz = 25000000u,
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .payload_class = TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE,
        .frame = frame,
        .frame_size = frame_size,
    };
    failed += expect_bool("submit vdc payload",
                          tdma_service_submit_tx(&service, &tx_config),
                          true);
    tdma_service_core1_service(&service);
    (void)tdma_service_get_snapshot(&service, &snapshot);
    failed += expect_u32("vdc tdma ready",
                         snapshot.last_result,
                         tdma_service_RESULT_FRAME_READY);
    failed += expect_u32("vdc tdma payload class",
                         snapshot.payload_class,
                         TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE);
    failed += expect_u32("vdc tx count", fake.tx_count, 1u);
    failed += expect_u32("default tdma timestamp source",
                         snapshot.timestamp_source,
                         tdma_service_TIMESTAMP_SOURCE_SOFTWARE_US);
    failed += expect_u32("default tdma timestamp resolution",
                         snapshot.timestamp_resolution_ns,
                         1000u);
    failed += expect_u32("default tdma timestamp flags",
                         snapshot.timestamp_flags,
                         tdma_service_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);

    fake.timestamp_source = tdma_service_TIMESTAMP_SOURCE_HARDWARE_TICK;
    fake.timestamp_resolution_ns = 50u;
    fake.timestamp_flags = tdma_service_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    failed += expect_bool("rebuild unwindowed hardware frame",
                          vdc_tdma_payload_build_frame(
                              &schedule,
                              &plan,
                              VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE,
                              2u,
                              &evidence,
                              frame,
                              sizeof(frame),
                              &frame_size,
                              &envelope,
                              &payload_status),
                          true);
    failed += expect_bool("submit unwindowed hardware vdc payload",
                          tdma_service_submit_tx(&service, &tx_config),
                          true);
    tdma_service_core1_service(&service);
    (void)tdma_service_get_snapshot(&service, &snapshot);
    failed += expect_u32("unwindowed hardware source kept",
                         snapshot.timestamp_source,
                         tdma_service_TIMESTAMP_SOURCE_HARDWARE_TICK);
    failed += expect_u32("unwindowed dpll flag cleared",
                         snapshot.timestamp_flags &
                             tdma_service_TIMESTAMP_FLAG_DPLL_ELIGIBLE,
                         0u);
    snapshot.core1_start_time_ns_lo =
        (uint32_t)(plan.window_start_ns & 0xFFFFFFFFull);
    snapshot.core1_start_time_ns_hi =
        (uint32_t)(plan.window_start_ns >> 32u);
    snapshot.core1_done_time_ns_lo =
        (uint32_t)((plan.window_start_ns + 50u) & 0xFFFFFFFFull);
    snapshot.core1_done_time_ns_hi =
        (uint32_t)((plan.window_start_ns + 50u) >> 32u);
    failed += expect_bool("unwindowed hardware vdc not dpll eligible",
                          vdc_tdma_payload_parse_frame(
                              &schedule,
                              &snapshot,
                              frame,
                              frame_size,
                              true,
                              &parsed,
                              &payload_status),
                          false);
    failed += expect_u32("unwindowed hardware gate",
                         payload_status.gate.reject_code,
                         VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE);
    failed += expect_bool("rebuild baseline vdc tdma frame",
                          vdc_tdma_payload_build_frame(
                              &schedule,
                              &plan,
                              VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE,
                              1u,
                              &evidence,
                              frame,
                              sizeof(frame),
                              &frame_size,
                              &envelope,
                              &payload_status),
                          true);
    fake.timestamp_source = 0u;
    fake.timestamp_resolution_ns = 0u;
    fake.timestamp_flags = 0u;

    tdma_service_service_t window_service;
    failed += expect_bool("windowed tdma init",
                          tdma_service_init(&window_service),
                          true);
    failed += expect_bool("windowed vdc payload register",
                          vdc_tdma_payload_register(&window_service),
                          true);
    failed += expect_bool("windowed tdma bind",
                          tdma_service_bind_ops(&window_service,
                                                &s_fake_tdma_ops,
                                                &fake),
                          true);
    const tdma_service_intent_config_t windowed_tx_config = {
        .window_epoch = schedule.schedule_epoch,
        .window_index = 1u,
        .deadline_1e3ns = 25u,
        .role = TDMA_SERVICE_ROLE_MASTER,
        .baud_hz = 25000000u,
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .payload_class = TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE,
        .scheduled_window_valid = 1u,
        .scheduled_window_class = VDC_DOMAIN_WINDOW_VDC_OBSERVATION,
        .schedule_crc32 = schedule.schedule_crc32,
        .scheduled_window_start_ns = 1000000000ull,
        .scheduled_window_end_ns = 1000010000ull,
        .scheduled_guard_start_ns = 999900000ull,
        .scheduled_guard_end_ns = 1000011000ull,
        .frame = frame,
        .frame_size = frame_size,
    };
    failed += expect_bool("submit windowed vdc payload",
                          tdma_service_submit_tx(&window_service,
                                                 &windowed_tx_config),
                          true);
    tdma_service_core1_service(&window_service);
    (void)tdma_service_get_snapshot(&window_service, &snapshot);
    failed += expect_u32("windowed tdma waits",
                         snapshot.last_result,
                         tdma_service_RESULT_WAITING_FOR_WINDOW);
    failed += expect_u32("windowed tdma not ready early",
                         fake.tx_count,
                         2u);
    for (uint32_t i = 0u; i < 12000u; i++) {
        tdma_service_core1_service(&window_service);
        (void)tdma_service_get_snapshot(&window_service, &snapshot);
        if (snapshot.state == tdma_service_STATE_DONE) {
            break;
        }
    }
    failed += expect_u32("windowed tdma done",
                         snapshot.state,
                         tdma_service_STATE_DONE);
    failed += expect_u32("windowed tdma ready",
                         snapshot.last_result,
                         tdma_service_RESULT_FRAME_READY);
    failed += expect_u32("windowed tdma tx after window",
                         fake.tx_count,
                         3u);

    const tdma_service_intent_config_t unregistered_refmem = {
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .payload_class = TDMA_SERVICE_PAYLOAD_CLASS_REFMEM_DELTA,
        .frame = frame,
        .frame_size = 4u,
    };
    failed += expect_bool("unregistered refmem rejected on vdc tdma",
                          tdma_service_submit_tx(&service,
                                                 &unregistered_refmem),
                          false);

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.intent_seq = 1u;
    snapshot.completed_seq = 1u;
    snapshot.last_result = tdma_service_RESULT_FRAME_READY;
    snapshot.payload_class = TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE;
    snapshot.timestamp_source = tdma_service_TIMESTAMP_SOURCE_SOFTWARE_US;
    snapshot.timestamp_resolution_ns = 1000u;
    snapshot.timestamp_flags = tdma_service_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    snapshot.core1_start_time_ns_lo = 1u;
    snapshot.core1_done_time_ns_lo = 2u;
    failed += expect_bool("parse diagnostic vdc tdma frame",
                          vdc_tdma_payload_parse_frame(
                              &schedule,
                              &snapshot,
                              frame,
                              frame_size,
                              false,
                              &parsed,
                              &payload_status),
                          true);
    failed += expect_u32("parsed diagnostic source",
                         parsed.timestamp.timestamp_source,
                         VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US);
    failed += expect_bool("diagnostic tdma not dpll eligible",
                          vdc_tdma_payload_parse_frame(
                              &schedule,
                              &snapshot,
                              frame,
                              frame_size,
                              true,
                              &parsed,
                              &payload_status),
                          false);
    failed += expect_u32("diagnostic tdma gate",
                         payload_status.gate.reject_code,
                         VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE);

    failed += expect_bool("vdc domain init for tdma evidence",
                          vdc_domain_init(&context),
                          true);
    vdc_domain_set_ready(&context, true);
    fake.timestamp_source = tdma_service_TIMESTAMP_SOURCE_HARDWARE_TICK;
    fake.timestamp_resolution_ns = 50u;
    fake.timestamp_flags = tdma_service_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    for (uint32_t seq = 1u; seq <= 4u; seq++) {
        const uint64_t window_start_ns =
            2000000000ull +
            (uint64_t)(seq - 1u) * schedule.period_ns +
            schedule.observation_window_offset_ns;
        evidence = make_hardware_sample(&schedule, seq, 0);
        evidence.expected_window_start_ns = window_start_ns;
        evidence.arm_time_ns = window_start_ns;
        evidence.start_time_ns = window_start_ns;
        evidence.observed_time_ns = window_start_ns;
        evidence.done_time_ns = window_start_ns + 50u;
        evidence.apply_time_ns = evidence.done_time_ns;
        failed += expect_bool("build hardware vdc tdma frame",
                              vdc_tdma_payload_build_frame(
                                  &schedule,
                                  &(vdc_tdma_window_plan_t){
                                      .valid = 1u,
                                      .window_class =
                                          VDC_DOMAIN_WINDOW_VDC_OBSERVATION,
                                      .schedule_epoch = schedule.schedule_epoch,
                                      .slot_index = schedule.local_slot_id,
                                      .source_slot_id = schedule.local_slot_id,
                                      .reference_slot_id =
                                          schedule.reference_slot_id,
                                      .window_start_ns = window_start_ns,
                                      .window_end_ns = window_start_ns +
                                          schedule.observation_window_width_ns,
                                      .guard_start_ns = window_start_ns,
                                      .guard_end_ns = window_start_ns +
                                          schedule.observation_window_width_ns,
                                      .schedule_crc32 = schedule.schedule_crc32,
                                  },
                                  VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE,
                                  seq,
                                  &evidence,
                                  frame,
                                  sizeof(frame),
                                  &frame_size,
                                  &envelope,
                                  &payload_status),
                              true);
        const tdma_service_intent_config_t hardware_tx_config = {
            .window_epoch = schedule.schedule_epoch,
            .window_index = seq,
            .deadline_1e3ns = 25u,
            .role = TDMA_SERVICE_ROLE_MASTER,
            .baud_hz = 25000000u,
            .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
            .payload_class = TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE,
            .scheduled_window_valid = 1u,
            .scheduled_window_class = VDC_DOMAIN_WINDOW_VDC_OBSERVATION,
            .schedule_crc32 = schedule.schedule_crc32,
            .scheduled_window_start_ns = window_start_ns,
            .scheduled_window_end_ns = window_start_ns +
                schedule.observation_window_width_ns,
            .scheduled_guard_start_ns = window_start_ns,
            .scheduled_guard_end_ns = window_start_ns +
                schedule.observation_window_width_ns,
            .frame = frame,
            .frame_size = frame_size,
        };
        failed += expect_bool("submit hardware vdc payload",
                              tdma_service_submit_tx(&service,
                                                     &hardware_tx_config),
                              true);
        for (uint32_t i = 0u; i < 12000u; i++) {
            tdma_service_core1_service(&service);
            (void)tdma_service_get_snapshot(&service, &snapshot);
            if (snapshot.completed_seq == snapshot.intent_seq &&
                snapshot.last_result == tdma_service_RESULT_FRAME_READY) {
                break;
            }
        }
        snapshot.core1_arm_time_ns_lo = (uint32_t)(window_start_ns & 0xFFFFFFFFull);
        snapshot.core1_arm_time_ns_hi = (uint32_t)(window_start_ns >> 32u);
        snapshot.core1_start_time_ns_lo = (uint32_t)(window_start_ns & 0xFFFFFFFFull);
        snapshot.core1_start_time_ns_hi = (uint32_t)(window_start_ns >> 32u);
        snapshot.core1_done_time_ns_lo =
            (uint32_t)((window_start_ns + 50u) & 0xFFFFFFFFull);
        snapshot.core1_done_time_ns_hi =
            (uint32_t)((window_start_ns + 50u) >> 32u);
        failed += expect_u32("hardware tdma timestamp source",
                             snapshot.timestamp_source,
                             tdma_service_TIMESTAMP_SOURCE_HARDWARE_TICK);
        failed += expect_u32("hardware tdma timestamp resolution",
                             snapshot.timestamp_resolution_ns,
                             50u);
        failed += expect_u32("hardware tdma timestamp flags",
                             snapshot.timestamp_flags,
                             tdma_service_TIMESTAMP_FLAG_DPLL_ELIGIBLE);
        failed += expect_bool("parse hardware vdc tdma frame",
                              vdc_tdma_payload_parse_frame(
                                  &schedule,
                                  &snapshot,
                                  frame,
                                  frame_size,
                                  true,
                                  &parsed,
                                  &payload_status),
                              true);
        failed += expect_bool("submit parsed tdma evidence",
                              vdc_domain_submit_tdma_evidence(
                                  &context,
                                  &parsed.timestamp),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &domain_snapshot);
    failed += expect_u32("tdma evidence locks dpll",
                         domain_snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_LOCKED);
    failed += expect_u32("tdma evidence accepted",
                         domain_snapshot.dpll.accepted_sample_count,
                         4u);
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
    model.slew_limit_ppb = context.servo.sanity_freq_limit_ppb + 1000u;
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
    failed += expect_u32("clock slew follows servo",
                         snapshot.clock.slew_limit_ppb,
                         context.servo.sanity_freq_limit_ppb);
    failed += expect_u32("dco slew follows servo",
                         snapshot.dco.slew_limit_ppb,
                         context.servo.sanity_freq_limit_ppb);

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

    const uint32_t seq_before_evidence = snapshot.dco.dco_update_seq;
    vdc_domain_set_ready(&context, true);
    vdc_tdma_timestamp_evidence_t evidence =
        make_hardware_sample(&context.schedule, 1u, 0);
    failed += expect_bool("submit evidence updates dco",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_bool("dco evidence seq monotonic",
                          snapshot.dco.dco_update_seq > seq_before_evidence,
                          true);
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
                         0);
    failed += expect_i32("clock phase from dpll",
                         snapshot.clock.phase_offset_ns,
                         -10);
    failed += expect_i32("dco phase from dpll",
                         snapshot.dco.phase_offset_ns,
                         -10);
    failed += expect_u32("budget root distance",
                         snapshot.error_budget.root_distance_ns,
                         5u);

    vdc_tdma_timestamp_evidence_t bad = make_hardware_sample(&context.schedule, 5u, 0);
    bad.reference_slot_id = 7u;
    failed += expect_bool("bad evidence rejected",
                          vdc_domain_submit_tdma_evidence(&context, &bad),
                          false);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("rejected", snapshot.dpll.rejected_sample_count, 1u);
    failed += expect_u32("accepted streak reset",
                         snapshot.dpll.accepted_sample_count,
                         0u);
    failed += expect_u32("quality accepted streak reset",
                         snapshot.quality.accepted_sample_count,
                         0u);
    failed += expect_u32("checking after reject",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_CHECKING);
    failed += expect_u32("dco checking after reject",
                         snapshot.dco.lock_state,
                         VDC_DOMAIN_LOCK_CHECKING);
    failed += expect_u32("quality bad count",
                         snapshot.quality.consecutive_bad_samples,
                         1u);
    failed += expect_u32("quality reject code",
                         snapshot.quality.gate_reject_code,
                         VDC_DOMAIN_GATE_REFERENCE_MISMATCH);

    vdc_tdma_timestamp_evidence_t recovery =
        make_hardware_sample(&context.schedule, 6u, 0);
    failed += expect_bool("recovery evidence accepted",
                          vdc_domain_submit_tdma_evidence(&context, &recovery),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("recovery starts new streak",
                         snapshot.dpll.accepted_sample_count,
                         1u);
    failed += expect_bool("no immediate relock after reject",
                          snapshot.dpll.state != VDC_DOMAIN_LOCK_LOCKED,
                          true);
    return failed;
}

static int test_dpll_lock_quality_tiers(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;

    failed += expect_bool("init coarse tier", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    context.servo.kp_q16 = 0;
    context.servo.ki_q16 = 0;
    context.servo.lock_acceptance_threshold_ns = VDC_DOMAIN_LOCK_TIER_COARSE_NS;
    for (uint32_t i = 1u; i <= context.servo.lock_sample_count; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, 5000);
        failed += expect_bool("submit coarse tier",
                              vdc_domain_submit_tdma_evidence(&context, &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("coarse tier locked",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_LOCKED);
    failed += expect_u32("coarse tier quality",
                         snapshot.quality.lock_quality_tier,
                         VDC_DOMAIN_LOCK_QUALITY_COARSE_10US);
    failed += expect_u32("coarse tier degraded",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_DEGRADED);
    failed += expect_u32("coarse acceptance threshold",
                         snapshot.quality.lock_acceptance_threshold_ns,
                         VDC_DOMAIN_LOCK_TIER_COARSE_NS);

    failed += expect_bool("init debug tier", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    context.servo.kp_q16 = 0;
    context.servo.ki_q16 = 0;
    for (uint32_t i = 1u; i <= context.servo.lock_sample_count; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, 500);
        failed += expect_bool("submit debug tier",
                              vdc_domain_submit_tdma_evidence(&context, &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("debug tier locked",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_LOCKED);
    failed += expect_u32("debug tier quality",
                         snapshot.quality.lock_quality_tier,
                         VDC_DOMAIN_LOCK_QUALITY_DEBUG_1US);
    failed += expect_u32("debug tier degraded",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_DEGRADED);
    failed += expect_u32("debug acceptance threshold",
                         snapshot.quality.lock_acceptance_threshold_ns,
                         VDC_DOMAIN_LOCK_TIER_DEBUG_NS);

    failed += expect_bool("init fine tier", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    context.servo.kp_q16 = 0;
    context.servo.ki_q16 = 0;
    for (uint32_t i = 1u; i <= context.servo.lock_sample_count; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, 50);
        failed += expect_bool("submit fine tier",
                              vdc_domain_submit_tdma_evidence(&context, &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("fine tier locked",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_LOCKED);
    failed += expect_u32("fine tier quality",
                         snapshot.quality.lock_quality_tier,
                         VDC_DOMAIN_LOCK_QUALITY_FINE_100NS);
    failed += expect_u32("fine tier healthy",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_HEALTHY);

    failed += expect_bool("init product threshold", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    context.servo.kp_q16 = 0;
    context.servo.ki_q16 = 0;
    context.servo.lock_acceptance_threshold_ns = VDC_DOMAIN_LOCK_TIER_FINE_NS;
    for (uint32_t i = 1u; i <= context.servo.lock_sample_count; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, 500);
        failed += expect_bool("submit product threshold",
                              vdc_domain_submit_tdma_evidence(&context, &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("product threshold phase lock",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_PHASE_LOCK);
    failed += expect_u32("product threshold tier",
                         snapshot.quality.lock_quality_tier,
                         VDC_DOMAIN_LOCK_QUALITY_DEBUG_1US);
    failed += expect_u32("product acceptance threshold",
                         snapshot.quality.lock_acceptance_threshold_ns,
                         VDC_DOMAIN_LOCK_TIER_FINE_NS);
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

static int test_dpll_rejects_servo_outlier(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_tdma_timestamp_evidence_t outlier;

    failed += expect_bool("init outlier", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    context.servo.outlier_threshold_ns = 100u;
    context.servo.kp_q16 = 0;
    context.servo.ki_q16 = 0;

    for (uint32_t i = 1u; i <= context.servo.lock_sample_count; i++) {
        vdc_tdma_timestamp_evidence_t first =
            make_hardware_sample(&context.schedule, i, 0);
        failed += expect_bool("submit lock before outlier",
                              vdc_domain_submit_tdma_evidence(&context, &first),
                              true);
    }
    outlier = make_hardware_sample(&context.schedule, 2u, 200);
    outlier.sample_seq = context.servo.lock_sample_count + 1u;
    outlier.expected_window_start_ns =
        (uint64_t)(outlier.sample_seq - 1u) * context.schedule.period_ns +
        context.schedule.observation_window_offset_ns;
    outlier.observed_time_ns = outlier.expected_window_start_ns + 200u;
    outlier.done_time_ns = outlier.observed_time_ns + 100u;
    outlier.apply_time_ns = outlier.done_time_ns + 100u;
    failed += expect_bool("submit outlier sample",
                          vdc_domain_submit_tdma_evidence(&context, &outlier),
                          false);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("outlier accepted count",
                         snapshot.dpll.accepted_sample_count,
                         0u);
    failed += expect_u32("outlier rejected count",
                         snapshot.dpll.rejected_sample_count,
                         1u);
    failed += expect_u32("outlier reject code",
                         snapshot.dpll.last_reject_code,
                         VDC_DOMAIN_GATE_SERVO_OUTLIER);
    failed += expect_u32("outlier quality code",
                         snapshot.quality.gate_reject_code,
                         VDC_DOMAIN_GATE_SERVO_OUTLIER);

    vdc_tdma_timestamp_evidence_t recovery =
        make_hardware_sample(&context.schedule,
                             context.servo.lock_sample_count + 2u,
                             0);
    failed += expect_bool("submit recovery after outlier",
                          vdc_domain_submit_tdma_evidence(&context, &recovery),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("outlier recovery streak",
                         snapshot.dpll.accepted_sample_count,
                         1u);
    failed += expect_bool("outlier no immediate relock",
                          snapshot.dpll.state != VDC_DOMAIN_LOCK_LOCKED,
                          true);
    return failed;
}

static int test_dpll_slews_phase_and_pulls_rate_after_lock(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_tdma_timestamp_evidence_t evidence;

    failed += expect_bool("init slew", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    context.servo.step_threshold_ns = 10u;

    evidence = make_hardware_sample(&context.schedule, 1u, 0);
    failed += expect_bool("submit zero phase",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    evidence = make_hardware_sample(&context.schedule, 2u, 50);
    failed += expect_bool("submit slewed phase",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("slewed phase offset",
                         snapshot.clock.phase_offset_ns,
                         -10);

    for (uint32_t i = 3u; i <= context.servo.lock_sample_count; i++) {
        evidence = make_hardware_sample(&context.schedule, i, 10);
        failed += expect_bool("submit lock phase",
                              vdc_domain_submit_tdma_evidence(&context, &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("slew lock state",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_LOCKED);
    failed += expect_i32("ki rate pull",
                         snapshot.clock.period_adjust_ppb,
                         0);
    failed += expect_i32("budget applied rate",
                         snapshot.error_budget.freq_offset_ppb,
                         0);
    return failed;
}

static int test_dpll_acquisition_accepts_large_initial_phase(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    const int32_t initial_phase_ns = 400000;

    failed += expect_bool("init acquisition", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);

    vdc_tdma_timestamp_evidence_t strict =
        make_hardware_sample(&context.schedule, 1u, initial_phase_ns);
    failed += expect_bool("strict window rejects large phase",
                          vdc_domain_validate_tdma_timestamp_evidence(
                              &context.schedule,
                              &strict,
                              true,
                              NULL),
                          false);

    for (uint32_t i = 1u; i <= 3u; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, initial_phase_ns);
        failed += expect_bool("acquisition accepts large phase",
                              vdc_domain_submit_tdma_evidence(&context,
                                                              &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("acquisition accepted",
                         snapshot.dpll.accepted_sample_count,
                         3u);
    failed += expect_u32("acquisition not locked early",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_PHASE_LOCK);
    failed += expect_i32("acquisition residual slews",
                         snapshot.dpll.last_phase_error_ns,
                         200000);

    for (uint32_t i = 4u; i <= 6u; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, initial_phase_ns);
        failed += expect_bool("acquisition converges quickly",
                              vdc_domain_submit_tdma_evidence(&context,
                                                              &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("acquisition locked after slew",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_LOCKED);
    failed += expect_i32("acquisition final residual",
                         snapshot.dpll.last_phase_error_ns,
                         0);
    failed += expect_u32("acquisition waits for fine stability",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_DEGRADED);
    failed += expect_u32("acquisition not fine stable yet",
                         snapshot.quality.lock_quality_tier,
                         VDC_DOMAIN_LOCK_QUALITY_NONE);

    for (uint32_t i = 7u; i <= 9u; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, initial_phase_ns);
        failed += expect_bool("acquisition reaches fine",
                              vdc_domain_submit_tdma_evidence(&context,
                                                              &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("acquisition fine tier",
                         snapshot.quality.lock_quality_tier,
                         VDC_DOMAIN_LOCK_QUALITY_FINE_100NS);
    failed += expect_u32("acquisition healthy after fine stability",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_HEALTHY);
    return failed;
}

static int test_dpll_large_step_does_not_fine_lock_same_sample(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;

    failed += expect_bool("init one step quality",
                          vdc_domain_init(&context),
                          true);
    vdc_domain_set_ready(&context, true);
    context.servo.lock_acceptance_threshold_ns =
        VDC_DOMAIN_LOCK_TIER_COARSE_NS;
    context.servo.first_step_threshold_ns = 100000u;

    for (uint32_t i = 1u; i <= context.servo.lock_sample_count; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, 90000);
        failed += expect_bool("submit one step phase",
                              vdc_domain_submit_tdma_evidence(&context,
                                                              &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("large step no fine tier",
                         snapshot.quality.lock_quality_tier,
                         VDC_DOMAIN_LOCK_QUALITY_NONE);
    failed += expect_u32("large step no healthy",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_DEGRADED);
    failed += expect_u32("large step peak retained",
                         snapshot.dpll.max_abs_offset_ns,
                         90000);

    for (uint32_t i = context.servo.lock_sample_count + 1u;
         i <= context.servo.lock_sample_count * 2u;
         i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, 90000);
        failed += expect_bool("submit stable after step",
                              vdc_domain_submit_tdma_evidence(&context,
                                                              &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("large step reaches fine after stability",
                         snapshot.quality.lock_quality_tier,
                         VDC_DOMAIN_LOCK_QUALITY_FINE_100NS);
    failed += expect_u32("large step healthy after stability",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_HEALTHY);
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
    failed += test_default_timestamp_dictionary_contract();
    failed += test_wrap_tracker_contract();
    failed += test_compact_observation_contract();
    failed += test_sync_io_adapter_contract();
    failed += test_gate_rejects_diagnostic_timestamp();
    failed += test_gate_rejects_schedule_and_window_mismatch();
    failed += test_frame_envelope_window_contract();
    failed += test_tdma_window_plan_contract();
    failed += test_vdc_tdma_payload_mounts_on_common_tdma();
    failed += test_dco_control_contract();
    failed += test_context_accepts_samples_until_locked();
    failed += test_dpll_lock_quality_tiers();
    failed += test_context_submits_compact_observation();
    failed += test_sync_io_adapter_to_vdc_submit();
    failed += test_quality_age_updates_on_service();
    failed += test_dpll_updates_clock_rate_from_sample_period();
    failed += test_dpll_rejects_servo_outlier();
    failed += test_dpll_slews_phase_and_pulls_rate_after_lock();
    failed += test_dpll_acquisition_accepts_large_initial_phase();
    failed += test_dpll_large_step_does_not_fine_lock_same_sample();
    if (failed != 0) {
        (void)printf("vdc_domain tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("vdc_domain tests passed\n");
    return 0;
}
