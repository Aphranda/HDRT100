#include "vdc_domain.h"
#include "vdc_ring_observer.h"
#include "vdc_sync_io_adapter.h"
#include "vdc_tdma_payload.h"
#include "tdma_operating_profile.h"

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
                               uint32_t deadline_us,
                               tdma_service_exec_status_t *status)
{
    fake_tdma_ops_context_t *fake = (fake_tdma_ops_context_t *)context;
    (void)frame;
    (void)role;
    (void)baud_hz;
    (void)pins;
    (void)deadline_us;
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
                              uint32_t deadline_us,
                              tdma_service_exec_status_t *status)
{
    fake_tdma_ops_context_t *fake = (fake_tdma_ops_context_t *)context;
    (void)role;
    (void)baud_hz;
    (void)pins;
    (void)deadline_us;
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

static bool install_fast_test_servo(vdc_domain_context_t *context)
{
    vdc_servo_profile_t profile;
    if (context == NULL) {
        return false;
    }

    profile = context->servo;
    profile.kp_q16 = 65536;
    profile.ki_q16 = 4096;
    profile.sanity_freq_limit_ppb = 50000u;
    return vdc_domain_apply_debug_servo_profile(context, &profile);
}

static bool install_test_path_delay(vdc_domain_context_t *context)
{
    vdc_path_delay_table_t table;
    if (context == NULL) return false;
    table = context->path_delay;
    table.valid = 1u;
    table.entry_count = VDC_DOMAIN_NODE_COUNT;
    table.calibration_generation = 1u;
    table.topology_generation = 1u;
    table.bias_generation = 1u;
    table.freshness_us = 1u;
    table.flags = VDC_PATH_DELAY_FLAG_ACCEPTED |
                  VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                  VDC_PATH_DELAY_FLAG_BIAS_VALID |
                  VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH;
    for (uint32_t i = 0u; i < VDC_DOMAIN_NODE_COUNT; i++) {
        table.entries[i].valid = 1u;
        table.entries[i].source_slot_id = i;
        table.entries[i].reference_slot_id = context->schedule.reference_slot_id;
        table.entries[i].cal_crc32 = table.schedule_crc32;
        table.entries[i].freshness_us = table.freshness_us;
        table.entries[i].update_seq = table.update_seq;
    }
    table.table_crc32 = vdc_domain_path_delay_table_crc32(&table);
    return vdc_domain_publish_path_delay_table(context, &table);
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
    if (payload_class == VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE ||
        payload_class == VDC_DOMAIN_PAYLOAD_IDLE_BEACON) {
        frame.reference_sync_valid = 1u;
        frame.reference_seq_id = frame_seq;
        frame.reference_frame_id = frame_seq;
        frame.reference_sync_slot_id = schedule->reference_slot_id;
        frame.reference_time_ns = window_start_ns;
        frame.next_frame_start_ns = window_start_ns + schedule->period_ns;
        frame.reference_schedule_crc32 = schedule->schedule_crc32;
    }
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

static int test_tdma_ring_profile_contract(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_domain_context_t context;
    uint32_t original_ring_crc = 0u;
    uint32_t original_schedule_crc = 0u;

    vdc_domain_default_schedule(&schedule, 1u, 0u);
    failed += expect_bool("factory two-node schedule valid",
                          vdc_domain_schedule_validate(&schedule),
                          true);
    failed += expect_u32("factory active node count",
                         schedule.ring_binding.node_count,
                         VDC_DOMAIN_DEFAULT_ACTIVE_NODE_COUNT);
    failed += expect_u32("factory two-node upstream",
                         schedule.ring_binding.upstream_slot_id,
                         0u);
    failed += expect_u32("factory two-node downstream",
                         schedule.ring_binding.downstream_slot_id,
                         0u);

    failed += expect_bool("four node default schedule",
                          vdc_domain_default_schedule_for_topology(
                              &schedule, 2u, 0u, 4u),
                          true);
    original_ring_crc = schedule.ring_binding.profile_crc32;
    original_schedule_crc = schedule.schedule_crc32;

    failed += expect_bool("ring schedule valid",
                          vdc_domain_schedule_validate(&schedule),
                          true);
    failed += expect_u32("ring profile version",
                         schedule.ring_binding.version,
                         TDMA_RING_PROFILE_VERSION);
    failed += expect_u32("ring simultaneous flag",
                         schedule.ring_binding.flags &
                             TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN,
                         TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN);
    failed += expect_u32("ring node count",
                         schedule.ring_binding.node_count,
                         4u);
    failed += expect_u32("ring local index", schedule.ring_binding.local_index, 2u);
    failed += expect_u32("ring reference index",
                         schedule.ring_binding.reference_index,
                         0u);
    failed += expect_u32("ring upstream", schedule.ring_binding.upstream_slot_id, 1u);
    failed += expect_u32("ring downstream", schedule.ring_binding.downstream_slot_id, 3u);
    failed += expect_u32("ring feedback", schedule.ring_binding.feedback_slot_id, 0u);
    failed += expect_bool("ring crc nonzero",
                          schedule.ring_binding.profile_crc32 != 0u,
                          true);

    (void)memset(&context, 0, sizeof(context));
    context.schedule = schedule;
    failed += expect_bool("set eight-node topology",
                          vdc_domain_set_schedule_ring_topology(
                              &context, 7u, 0u, 8u),
                          true);
    failed += expect_u32("eight-node local slot",
                         context.schedule.local_slot_id,
                         7u);
    failed += expect_u32("eight-node upstream",
                         context.schedule.ring_binding.upstream_slot_id,
                         6u);
    failed += expect_u32("eight-node downstream wraps",
                         context.schedule.ring_binding.downstream_slot_id,
                         0u);
    failed += expect_bool("nine-node topology rejected",
                          vdc_domain_set_schedule_ring_topology(
                              &context, 0u, 0u, 9u),
                          false);

    schedule.ring_binding.downstream_slot_id = 4u;
    failed += expect_bool("stale ring crc rejected",
                          vdc_domain_schedule_validate(&schedule),
                          false);
    schedule.ring_binding.profile_crc32 =
        vdc_domain_ring_profile_crc32(&schedule);
    schedule.schedule_crc32 = vdc_domain_schedule_crc32(&schedule);
    failed += expect_bool("bad ring topology rejected",
                          vdc_domain_schedule_validate(&schedule),
                          false);

    vdc_domain_default_schedule(&schedule, 1u, 0u);
    schedule.ring_binding.node_count = 1u;
    schedule.ring_binding.profile_crc32 =
        vdc_domain_ring_profile_crc32(&schedule);
    schedule.schedule_crc32 = vdc_domain_schedule_crc32(&schedule);
    failed += expect_bool("one node ring rejected",
                          vdc_domain_schedule_validate(&schedule),
                          false);

    vdc_domain_default_schedule(&schedule, 1u, 0u);
    schedule.ring_binding.up_group_id = schedule.ring_binding.down_group_id;
    schedule.ring_binding.profile_crc32 =
        vdc_domain_ring_profile_crc32(&schedule);
    schedule.schedule_crc32 = vdc_domain_schedule_crc32(&schedule);
    failed += expect_bool("same leg group rejected",
                          vdc_domain_schedule_validate(&schedule),
                          false);

    (void)vdc_domain_default_schedule_for_topology(&schedule, 2u, 0u, 4u);
    schedule.ring_binding.node_count = 5u;
    schedule.ring_binding.local_index = 2u;
    schedule.local_slot_id = 2u;
    schedule.ring_binding.reference_index = 0u;
    schedule.reference_slot_id = 0u;
    schedule.ring_binding.upstream_slot_id = 1u;
    schedule.ring_binding.downstream_slot_id = 3u;
    schedule.ring_binding.feedback_slot_id = 0u;
    schedule.ring_binding.profile_crc32 =
        vdc_domain_ring_profile_crc32(&schedule);
    schedule.schedule_crc32 = vdc_domain_schedule_crc32(&schedule);
    failed += expect_bool("five node ring valid",
                          vdc_domain_schedule_validate(&schedule),
                          true);
    failed += expect_bool("ring crc changes",
                          schedule.ring_binding.profile_crc32 != original_ring_crc,
                          true);
    failed += expect_bool("schedule crc changes",
                          schedule.schedule_crc32 != original_schedule_crc,
                          true);

    schedule.ring_binding.downstream_slot_id = 5u;
    schedule.ring_binding.profile_crc32 =
        vdc_domain_ring_profile_crc32(&schedule);
    schedule.schedule_crc32 = vdc_domain_schedule_crc32(&schedule);
    failed += expect_bool("out of active ring slot rejected",
                          vdc_domain_schedule_validate(&schedule),
                          false);
    return failed;
}

static int test_tdma_ring_plan_contract(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_tdma_ring_plan_t plan;

    failed += expect_bool("four node ring plan schedule",
                          vdc_domain_default_schedule_for_topology(
                              &schedule, 2u, 0u, 4u),
                          true);
    failed += expect_bool("ring plan default",
                          vdc_domain_plan_tdma_ring(&schedule, &plan),
                          true);
    failed += expect_u32("ring plan valid", plan.valid, 1u);
    failed += expect_u32("ring plan node count",
                         plan.ring_node_count,
                         4u);
    failed += expect_u32("ring plan cycle period",
                         plan.cycle_period_ns,
                         schedule.period_ns);
    failed += expect_u32("ring plan local", plan.local_slot_id, 2u);
    failed += expect_u32("ring plan reference", plan.reference_slot_id, 0u);
    failed += expect_u32("ring plan upstream", plan.upstream_slot_id, 1u);
    failed += expect_u32("ring plan downstream", plan.downstream_slot_id, 3u);
    failed += expect_u32("ring plan from reference",
                         plan.from_reference_hops,
                         2u);
    failed += expect_u32("ring plan to feedback",
                         plan.to_feedback_hops,
                         2u);
    failed += expect_u32("ring plan not reference",
                         plan.is_reference_slot,
                         0u);

    vdc_domain_default_schedule(&schedule, 0u, 0u);
    failed += expect_bool("ring plan reference slot",
                          vdc_domain_plan_tdma_ring(&schedule, &plan),
                          true);
    failed += expect_u32("ring plan reference flag",
                         plan.is_reference_slot,
                         1u);
    failed += expect_u32("ring plan reference hops",
                         plan.from_reference_hops,
                         0u);

    (void)vdc_domain_default_schedule_for_topology(&schedule, 2u, 0u, 4u);
    schedule.ring_binding.node_count = 5u;
    schedule.ring_binding.local_index = 2u;
    schedule.local_slot_id = 2u;
    schedule.ring_binding.reference_index = 0u;
    schedule.reference_slot_id = 0u;
    schedule.ring_binding.upstream_slot_id = 1u;
    schedule.ring_binding.downstream_slot_id = 3u;
    schedule.ring_binding.feedback_slot_id = 0u;
    schedule.ring_binding.profile_crc32 =
        vdc_domain_ring_profile_crc32(&schedule);
    schedule.schedule_crc32 = vdc_domain_schedule_crc32(&schedule);
    failed += expect_bool("ring plan five node",
                          vdc_domain_plan_tdma_ring(&schedule, &plan),
                          true);
    failed += expect_u32("five node from reference",
                         plan.from_reference_hops,
                         2u);
    failed += expect_u32("five node to feedback",
                         plan.to_feedback_hops,
                         3u);

    schedule.ring_binding.node_count = 1u;
    schedule.ring_binding.profile_crc32 =
        vdc_domain_ring_profile_crc32(&schedule);
    schedule.schedule_crc32 = vdc_domain_schedule_crc32(&schedule);
    failed += expect_bool("ring plan rejects invalid",
                          vdc_domain_plan_tdma_ring(&schedule, &plan),
                          false);
    failed += expect_u32("ring plan invalid zeroed", plan.valid, 0u);
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
    failed += expect_u32("default diagnostic gate rejects missing calibration",
                         snapshot.quality.gate_reject_code,
                         VDC_DOMAIN_GATE_BAD_FRAME);
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
    failed += expect_bool("compact path calibration",
                          install_test_path_delay(&context), true);
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

static int test_path_delay_table_drives_compact_phase(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_timestamp_dictionary_t dictionary;
    vdc_compact_observation_sample_t compact;
    vdc_path_delay_table_t table;
    vdc_path_delay_entry_t entry;

    failed += expect_bool("path table init",
                          vdc_domain_init(&context),
                          true);
    failed += expect_bool("default path table invalid until calibration",
                          vdc_domain_path_delay_table_validate(
                              &context.path_delay),
                          false);
    failed += expect_bool("default path lookup rejected",
                          vdc_domain_path_delay_lookup(&context.path_delay,
                                                       0u,
                                                       0u,
                                                       &entry),
                          false);

    table = context.path_delay;
    table.valid = 1u;
    table.entry_count = VDC_DOMAIN_NODE_COUNT;
    table.calibration_generation = 1u;
    table.topology_generation = 1u;
    table.bias_generation = 1u;
    table.freshness_us = 1u;
    table.flags = VDC_PATH_DELAY_FLAG_ACCEPTED |
                  VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                  VDC_PATH_DELAY_FLAG_BIAS_VALID |
                  VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH;
    for (uint32_t i = 0u; i < VDC_DOMAIN_NODE_COUNT; i++) {
        table.entries[i].valid = 1u;
        table.entries[i].source_slot_id = i;
        table.entries[i].reference_slot_id = context.schedule.reference_slot_id;
        table.entries[i].cal_crc32 = table.schedule_crc32;
        table.entries[i].freshness_us = table.freshness_us;
        table.entries[i].update_seq = table.update_seq;
    }
    table.update_seq++;
    table.entries[0].delay_ns = 7u;
    table.entries[0].jitter_ns = 2u;
    table.entries[0].stddev_ns = 1u;
    table.entries[0].cal_crc32 = 0xCA1B0001u;
    table.entries[0].freshness_us = 3u;
    table.entries[0].writer = context.schedule.reference_slot_id;
    table.entries[0].update_seq++;
    table.table_crc32 = vdc_domain_path_delay_table_crc32(&table);
    failed += expect_bool("publish path delay table",
                          vdc_domain_publish_path_delay_table(&context,
                                                              &table),
                          true);

    table.table_crc32 ^= 1u;
    failed += expect_bool("reject bad path crc",
                          vdc_domain_publish_path_delay_table(&context,
                                                              &table),
                          false);

    vdc_domain_set_ready(&context, true);
    make_timestamp_dictionary_for_schedule(&context.schedule,
                                           &dictionary,
                                           41u,
                                           VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE);
    failed += expect_bool("publish path dictionary",
                          vdc_domain_publish_timestamp_dictionary(&context,
                                                                  &dictionary,
                                                                  0u),
                          true);

    (void)memset(&compact, 0, sizeof(compact));
    compact.valid = 1u;
    compact.sample_seq = 1u;
    compact.event_id = 41u;
    compact.tick_l32 = 20u;
    compact.expected_window_start_ns =
        context.schedule.observation_window_offset_ns;
    compact.frame_crc32 = 0x5555u;
    compact.sample_crc32 = 0x6666u;
    compact.jitter_ns = 2u;
    compact.delay_ns = 99u;
    compact.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    compact.timestamp_resolution_ns = 50u;
    compact.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;

    failed += expect_bool("submit path corrected compact",
                          vdc_domain_submit_compact_observation(&context,
                                                                &compact),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("path corrected phase",
                         snapshot.dpll.last_phase_error_ns,
                         13);
    failed += expect_u32("path delay from table",
                         snapshot.error_budget.path_delay_ns,
                         7u);
    failed += expect_u32("snapshot path crc",
                         snapshot.path_delay.table_crc32,
                         context.path_delay.table_crc32);
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
    failed += expect_bool("sync io path calibration",
                          install_test_path_delay(&context), true);
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
    frame.reference_sync_valid = 0u;
    failed += expect_bool("observation frame rejects missing reference sync",
                          vdc_domain_validate_tdma_frame_envelope(&schedule,
                                                                   &frame,
                                                                   true,
                                                                   &gate),
                          false);
    failed += expect_u32("missing reference sync gate",
                         gate.reject_code,
                         VDC_DOMAIN_GATE_BAD_FRAME);
    return failed;
}

static int test_tdma_window_plan_contract(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_tdma_window_plan_t plan;
    vdc_gate_result_t gate;

    failed += expect_bool("window plan topology schedule",
                          vdc_domain_default_schedule_for_topology(
                              &schedule, 3u, 0u, 4u),
                          true);

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
    failed += expect_u32("vdc reference sync valid",
                         envelope.reference_sync_valid,
                         1u);
    failed += expect_u32("vdc reference seq",
                         envelope.reference_seq_id,
                         1u);
    failed += expect_u64("vdc reference time",
                         envelope.reference_time_ns,
                         plan.window_start_ns);

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
        .deadline_us = 25u,
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
        .deadline_us = 25u,
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
    failed += expect_u32("parsed reference sync valid",
                         parsed.reference_sync_valid,
                         1u);
    failed += expect_u64("parsed next frame",
                         parsed.next_frame_start_ns,
                         plan.window_start_ns + schedule.period_ns);
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
            .deadline_us = 25u,
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
        failed += expect_u32("hardware parse reference valid",
                             parsed.reference_sync_valid,
                             1u);
        failed += expect_u64("hardware parse next frame",
                             parsed.next_frame_start_ns,
                             window_start_ns + schedule.period_ns);
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
    failed += expect_bool("install fast test servo",
                          install_fast_test_servo(&context), true);
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

    failed += expect_bool("init provisional path tier",
                          vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    context.servo.kp_q16 = 0;
    context.servo.ki_q16 = 0;
    context.path_delay.flags |= VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY;
    for (uint32_t i = 1u; i <= context.servo.lock_sample_count; i++) {
        vdc_tdma_timestamp_evidence_t evidence =
            make_hardware_sample(&context.schedule, i, 5000);
        failed += expect_bool("submit provisional path tier",
                              vdc_domain_submit_tdma_evidence(&context,
                                                              &evidence),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("provisional path coarse locked",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_LOCKED);
    failed += expect_u32("provisional path remains coarse quality",
                         snapshot.quality.lock_quality_tier,
                         VDC_DOMAIN_LOCK_QUALITY_COARSE_10US);
    failed += expect_u32("provisional path effective threshold",
                         snapshot.quality.lock_acceptance_threshold_ns,
                         VDC_DOMAIN_LOCK_TIER_COARSE_NS);
    vdc_tdma_timestamp_evidence_t provisional_margin =
        make_hardware_sample(&context.schedule,
                             context.servo.lock_sample_count + 1u,
                             10500);
    failed += expect_bool("provisional tracking accepts correction margin",
                          vdc_domain_submit_tdma_evidence(
                              &context, &provisional_margin),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("provisional phase loss enters relocking",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_RELOCKING);

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
    vdc_tdma_timestamp_evidence_t formal_outlier =
        make_hardware_sample(&context.schedule,
                             context.servo.lock_sample_count + 1u,
                             10500);
    failed += expect_bool("formal tracking consumes phase margin",
                          vdc_domain_submit_tdma_evidence(&context,
                                                          &formal_outlier),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("formal phase margin keeps gate pass",
                         snapshot.dpll.last_reject_code,
                         VDC_DOMAIN_GATE_PASS);

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

static int test_dpll_rate_estimator_waits_and_slews(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_tdma_timestamp_evidence_t evidence;

    failed += expect_bool("init rate slew", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);

    evidence = make_hardware_sample(&context.schedule, 1u, 0);
    failed += expect_bool("submit rate anchor",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    evidence = make_hardware_sample(&context.schedule, 2u, 1000);
    failed += expect_bool("submit short rate sample",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("short sample holds rate",
                         snapshot.dpll.last_frequency_error_ppb,
                         0);
    failed += expect_i32("short sample holds adjust",
                         snapshot.clock.period_adjust_ppb,
                         0);

    evidence = make_hardware_sample(&context.schedule, 17u, 16000);
    failed += expect_bool("submit slewed rate sample",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("rate slew limited",
                         snapshot.dpll.last_frequency_error_ppb,
                         1250);
    failed += expect_i32("rate adjust slew limited",
                         snapshot.clock.period_adjust_ppb,
                         -1250);
    return failed;
}

static int test_dpll_continues_through_large_phase_error(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_tdma_timestamp_evidence_t outlier;

    failed += expect_bool("init outlier", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    context.servo.phase_diagnostic_threshold_ns = 100u;
    context.servo.kp_q16 = 65536;
    context.servo.ki_q16 = 0;

    for (uint32_t i = 1u; i <= context.servo.lock_sample_count; i++) {
        vdc_tdma_timestamp_evidence_t first =
            make_hardware_sample(&context.schedule, i, 0);
        failed += expect_bool("submit lock before outlier",
                              vdc_domain_submit_tdma_evidence(&context, &first),
                              true);
    }
    outlier = make_hardware_sample(&context.schedule, 2u, 20000);
    outlier.sample_seq = context.servo.lock_sample_count + 1u;
    outlier.expected_window_start_ns =
        (uint64_t)(outlier.sample_seq - 1u) * context.schedule.period_ns +
        context.schedule.observation_window_offset_ns;
    outlier.observed_time_ns = outlier.expected_window_start_ns + 20000u;
    outlier.done_time_ns = outlier.observed_time_ns + 100u;
    outlier.apply_time_ns = outlier.done_time_ns + 100u;
    failed += expect_bool("submit large phase sample",
                          vdc_domain_submit_tdma_evidence(&context, &outlier),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("large phase increments accepted count",
                         snapshot.dpll.accepted_sample_count,
                         context.servo.lock_sample_count + 1u);
    failed += expect_u32("large phase does not reject",
                         snapshot.dpll.rejected_sample_count,
                         0u);
    failed += expect_u32("large phase gate pass",
                         snapshot.dpll.last_reject_code,
                         VDC_DOMAIN_GATE_PASS);
    failed += expect_u32("large phase enters relocking",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_RELOCKING);
    failed += expect_bool("large phase records out of lock",
                          (snapshot.quality.quality_flags &
                           VDC_DOMAIN_QUALITY_FLAG_PHASE_OUT_OF_LOCK) != 0u,
                          true);

    vdc_tdma_timestamp_evidence_t recovery =
        make_hardware_sample(&context.schedule,
                             context.servo.lock_sample_count + 2u,
                             0);
    failed += expect_bool("submit recovery after outlier",
                          vdc_domain_submit_tdma_evidence(&context, &recovery),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("phase recovery keeps history",
                         snapshot.dpll.accepted_sample_count,
                         context.servo.lock_sample_count + 2u);
    failed += expect_u32("phase recovery remains relocking until stable",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_RELOCKING);

    for (uint32_t i = 0u; i < context.servo.lock_sample_count; i++) {
        outlier.sample_seq++;
        outlier.expected_window_start_ns += context.schedule.period_ns;
        outlier.phase_error_ns = 50;
        outlier.observed_time_ns = outlier.expected_window_start_ns + 50u;
        outlier.done_time_ns = outlier.observed_time_ns + 100u;
        outlier.apply_time_ns = outlier.done_time_ns + 100u;
        failed += expect_bool("submit consecutive large phase",
                              vdc_domain_submit_tdma_evidence(&context,
                                                              &outlier),
                              true);
    }
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("consecutive large phase recovers after stable samples",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_LOCKED);
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
    failed += expect_bool("install fast acquisition test servo",
                          install_fast_test_servo(&context), true);

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
    failed += expect_u32("acquisition enters phase lock after slew",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_PHASE_LOCK);
    failed += expect_bool("acquisition final residual within resolution",
                          snapshot.dpll.last_phase_error_ns >=
                                  -(int32_t)strict.timestamp_resolution_ns &&
                              snapshot.dpll.last_phase_error_ns <=
                                  (int32_t)strict.timestamp_resolution_ns,
                          true);
    failed += expect_u32("acquisition remains lock candidate",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_LOCK_CANDIDATE);
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

    vdc_tdma_timestamp_evidence_t centered =
        make_hardware_sample(&context.schedule, 10u,
                             initial_phase_ns - 6000);
    failed += expect_bool("tracking window accepts negative residual",
                          vdc_domain_submit_tdma_evidence(&context,
                                                          &centered),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("centered tracking gate passes",
                         snapshot.gate.reject_code,
                         VDC_DOMAIN_GATE_PASS);
    return failed;
}

static int test_dpll_acquisition_continues_through_phase_innovation(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;

    failed += expect_bool("init acquisition innovation",
                          vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);

    vdc_tdma_timestamp_evidence_t initial =
        make_hardware_sample(&context.schedule, 1u, 400000);
    failed += expect_bool("accept initial acquisition offset",
                          vdc_domain_submit_tdma_evidence(&context, &initial),
                          true);

    vdc_tdma_timestamp_evidence_t outlier =
        make_hardware_sample(&context.schedule, 2u, 0);
    failed += expect_bool("process acquisition innovation",
                          vdc_domain_submit_tdma_evidence(&context, &outlier),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("acquisition innovation retains accepted",
                         snapshot.dpll.accepted_sample_count, 2u);
    failed += expect_u32("acquisition innovation not rejected",
                         snapshot.dpll.rejected_sample_count, 0u);
    failed += expect_u32("acquisition innovation gate pass",
                         snapshot.dpll.last_reject_code,
                         VDC_DOMAIN_GATE_PASS);

    vdc_tdma_timestamp_evidence_t recovery =
        make_hardware_sample(&context.schedule, 3u, 400000);
    failed += expect_bool("accept acquisition recovery",
                          vdc_domain_submit_tdma_evidence(&context, &recovery),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("acquisition recovery accepted",
                         snapshot.dpll.accepted_sample_count, 3u);
    return failed;
}

static int test_default_servo_is_conservative(void)
{
    int failed = 0;
    vdc_servo_profile_t profile;

    vdc_domain_default_servo(&profile);
    failed += expect_i32("default servo Kp", profile.kp_q16, 16384);
    failed += expect_i32("default servo Ki", profile.ki_q16, 256);
    failed += expect_u32("default servo update period",
                         profile.update_period_us, 1000u);
    failed += expect_u32("default servo step threshold",
                         profile.step_threshold_ns, 10000u);
    failed += expect_u32("default servo frequency limit",
                         profile.sanity_freq_limit_ppb, 10000u);
    failed += expect_u32("default servo Kp constant", profile.kp_q16,
                         VDC_DOMAIN_DEFAULT_SERVO_KP_Q16);
    failed += expect_u32("default servo Ki constant", profile.ki_q16,
                         VDC_DOMAIN_DEFAULT_SERVO_KI_Q16);
    failed += expect_u32("default servo limit constant",
                         profile.sanity_freq_limit_ppb,
                         VDC_DOMAIN_DEFAULT_SANITY_FREQ_LIMIT_PPB);
    failed += expect_u32("default servo CRC", profile.servo_profile_crc32,
                         VDC_DOMAIN_DEFAULT_SERVO_PROFILE_CRC32);
    return failed;
}

static int test_tracking_gate_ignores_stale_phase_model(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_tdma_timestamp_evidence_t evidence;

    failed += expect_bool("init stale phase tracking",
                          vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    context.dpll.state = VDC_DOMAIN_LOCK_LOCKED;
    context.dpll.accepted_sample_count = context.servo.lock_sample_count;
    context.clock.valid = 1u;
    context.clock.phase_offset_ns = 8000000;

    evidence = make_hardware_sample(&context.schedule, 100u, -5000);
    evidence.observed_time_ns = evidence.expected_window_start_ns - 5000u;
    evidence.arm_time_ns = evidence.observed_time_ns;
    evidence.start_time_ns = evidence.observed_time_ns;
    evidence.done_time_ns = evidence.observed_time_ns + 100u;
    evidence.apply_time_ns = evidence.done_time_ns + 100u;
    failed += expect_bool("tracking accepts stale model residual",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("tracking stale model gate passes",
                         snapshot.gate.reject_code,
                         VDC_DOMAIN_GATE_PASS);
    failed += expect_i32("tracking stale model keeps residual",
                         snapshot.dpll.last_phase_error_ns,
                         -5000);
    return failed;
}

static int test_acquisition_gate_covers_full_cycle(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_tdma_timestamp_evidence_t evidence;

    failed += expect_bool("init full cycle acquisition",
                          vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    evidence = make_hardware_sample(&context.schedule, 100u, 900000);
    evidence.observed_time_ns = evidence.expected_window_start_ns + 900000u;
    evidence.arm_time_ns = evidence.observed_time_ns;
    evidence.start_time_ns = evidence.observed_time_ns;
    evidence.done_time_ns = evidence.observed_time_ns + 100u;
    evidence.apply_time_ns = evidence.done_time_ns + 100u;
    failed += expect_bool("acquisition accepts late half-cycle sample",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    failed += expect_u32("full cycle acquisition gate passes",
                         context.gate.reject_code,
                         VDC_DOMAIN_GATE_PASS);
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
    failed += expect_bool("install fast step test servo",
                          install_fast_test_servo(&context), true);
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
    failed += expect_u32("large step remains lock candidate",
                         snapshot.quality.health_state,
                         VDC_DOMAIN_HEALTH_LOCK_CANDIDATE);
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

static int test_debug_servo_tune_accepts_extreme_profile_and_integrator(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_servo_profile_t profile;

    failed += expect_bool("init debug servo tune",
                          vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    profile = context.servo;
    profile.kp_q16 = 0;
    profile.ki_q16 = 65536;
    profile.update_period_us = 1000u;
    profile.sanity_freq_limit_ppb = 100u;
    profile.lock_sample_count = 1u;
    profile.step_threshold_ns = 0u;
    profile.first_step_threshold_ns = 0u;
    failed += expect_bool("apply debug profile",
                          vdc_domain_apply_debug_servo_profile(
                              &context, &profile), true);
    failed += expect_u32("debug profile restarts checking",
                         context.dpll.state, VDC_DOMAIN_LOCK_CHECKING);
    failed += expect_bool("debug profile crc assigned",
                          context.servo.servo_profile_crc32 != 0u, true);

    vdc_tdma_timestamp_evidence_t evidence =
        make_hardware_sample(&context.schedule, 1u, 1000);
    failed += expect_bool("debug integral first sample",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("integrator first pull",
                         snapshot.dpll.loop_filter_integrator_ppb, 100);
    evidence = make_hardware_sample(&context.schedule, 2u, 1000);
    failed += expect_bool("debug integral saturated sample",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("anti windup holds positive limit",
                         snapshot.dpll.loop_filter_integrator_ppb, 100);
    evidence = make_hardware_sample(&context.schedule, 3u, -1000);
    failed += expect_bool("debug integral unwinds",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("integrator unwinds",
                         snapshot.dpll.loop_filter_integrator_ppb, 0);

    profile.kp_q16 = INT32_MIN;
    profile.ki_q16 = INT32_MAX;
    profile.update_period_us = UINT32_MAX;
    profile.step_threshold_ns = UINT32_MAX;
    profile.sanity_freq_limit_ppb = UINT32_MAX;
    failed += expect_bool("extreme debug profile accepted",
                          vdc_domain_apply_debug_servo_profile(
                              &context, &profile), true);
    return failed;
}

static int test_debug_admission_continues_recoverable_gate(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_tdma_timestamp_evidence_t evidence;

    failed += expect_bool("init debug admission", vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    evidence = make_hardware_sample(&context.schedule, 1u, 10);
    failed += expect_bool("debug admission baseline accepted",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    const uint32_t accepted_before = snapshot.dpll.accepted_sample_count;
    const uint32_t clock_before = snapshot.clock.model_seq;
    const uint32_t dco_before = snapshot.dco.dco_update_seq;

    failed += expect_bool("enable debug admission continue",
                          vdc_domain_set_debug_continue(&context, true), true);
    evidence = make_hardware_sample(&context.schedule, 2u, 0);
    evidence.expected_window_start_ns++;
    failed += expect_bool("window bound continues during debug",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("debug admission enabled",
                         snapshot.dpll.debug_continue_enabled, 1u);
    failed += expect_u32("debug window continues once",
                         snapshot.dpll.debug_continue_count, 1u);
    failed += expect_u32("debug window raw gate retained",
                         snapshot.dpll.last_debug_gate_code,
                         VDC_DOMAIN_GATE_WINDOW_BOUND);
    failed += expect_u32("debug window is not product reject",
                         snapshot.dpll.rejected_sample_count, 0u);
    failed += expect_u32("debug window reports pass",
                         snapshot.dpll.last_reject_code,
                         VDC_DOMAIN_GATE_PASS);
    failed += expect_u32("debug window leaves accepted count",
                         snapshot.dpll.accepted_sample_count, accepted_before);
    failed += expect_u32("debug window leaves clock untouched",
                         snapshot.clock.model_seq, clock_before);
    failed += expect_u32("debug window leaves dco untouched",
                         snapshot.dco.dco_update_seq, dco_before);

    evidence = make_hardware_sample(&context.schedule, 3u, 0);
    evidence.reference_slot_id = VDC_DOMAIN_NODE_COUNT;
    failed += expect_bool("identity mismatch remains rejected in debug",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          false);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("identity mismatch records reject",
                         snapshot.dpll.rejected_sample_count, 1u);
    failed += expect_u32("identity mismatch remains visible",
                         snapshot.dpll.last_reject_code,
                         VDC_DOMAIN_GATE_REFERENCE_MISMATCH);

    failed += expect_bool("disable debug admission continue",
                          vdc_domain_set_debug_continue(&context, false), true);
    evidence = make_hardware_sample(&context.schedule, 4u, 0);
    evidence.expected_window_start_ns++;
    failed += expect_bool("window bound rejects after debug disabled",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          false);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("strict window increments rejects",
                         snapshot.dpll.rejected_sample_count, 2u);
    failed += expect_u32("strict window visible",
                         snapshot.dpll.last_reject_code,
                         VDC_DOMAIN_GATE_WINDOW_BOUND);
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
    failed += expect_u32("sample age us",
                         snapshot.quality.last_sample_age_us,
                         50u);
    return failed;
}

static int test_dpll_rate_correction_enters_next_phase_prediction(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_tdma_timestamp_evidence_t evidence;

    failed += expect_bool("init rate prediction",
                          vdc_domain_init(&context), true);
    vdc_domain_set_ready(&context, true);
    failed += expect_bool("install fast prediction test servo",
                          install_fast_test_servo(&context), true);

    evidence = make_hardware_sample(&context.schedule, 1u, 0);
    failed += expect_bool("submit rate prediction anchor",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    evidence = make_hardware_sample(&context.schedule, 9u, 400);
    failed += expect_bool("submit rate prediction estimate",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    evidence = make_hardware_sample(&context.schedule, 17u, 800);
    failed += expect_bool("submit rate corrected phase",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("rate correction reduces next residual",
                         snapshot.dpll.last_phase_error_ns,
                         350);
    failed += expect_i32("raw phase anchor follows estimator",
                         snapshot.dpll.last_raw_phase_error_ns,
                         800);
    return failed;
}

static int test_observation_path_matrix_is_explicit(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_path_delay_table_t table;
    vdc_path_delay_entry_t entry;

    failed += expect_bool("matrix init", vdc_domain_init(&context), true);
    failed += expect_bool("matrix topology",
                          vdc_domain_set_schedule_ring_topology(
                              &context, 0u, 0u, 4u), true);
    table = context.path_delay;
    table.valid = 1u;
    table.schedule_crc32 = context.schedule.schedule_crc32;
    table.entry_count = VDC_DOMAIN_NODE_COUNT;
    table.calibration_generation = 1u;
    table.topology_generation = 1u;
    table.bias_generation = 1u;
    table.freshness_us = 1u;
    table.flags = VDC_PATH_DELAY_FLAG_ACCEPTED |
                  VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                  VDC_PATH_DELAY_FLAG_BIAS_VALID |
                  VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH |
                  VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID;
    for (uint32_t i = 0u; i < VDC_DOMAIN_NODE_COUNT; i++) {
        table.entries[i].valid = 1u;
        table.entries[i].source_slot_id = i;
        table.entries[i].reference_slot_id = 0u;
        table.entries[i].cal_crc32 = table.schedule_crc32;
        table.entries[i].freshness_us = table.freshness_us;
        table.entries[i].update_seq = table.update_seq;
    }
    table.observation_matrix.valid = 1u;
    table.observation_matrix.node_count = 4u;
    table.observation_matrix.entry_count = 16u;
    for (uint32_t source = 0u; source < 4u; source++) {
        for (uint32_t reference = 0u; reference < 4u; reference++) {
            const uint32_t index = source * VDC_DOMAIN_NODE_COUNT + reference;
            table.observation_matrix.delay_ns[index] =
                100u + source * 10u + reference;
            table.observation_matrix.valid_bitmap[index / 32u] |=
                1u << (index % 32u);
        }
    }
    table.table_crc32 = vdc_domain_path_delay_table_crc32(&table);
    failed += expect_bool("publish explicit observation matrix",
                          vdc_domain_publish_path_delay_table(&context,
                                                              &table), true);
    failed += expect_bool("matrix direct lookup",
                          vdc_domain_observation_path_delay_lookup(
                              &context.path_delay, 2u, 0u, &entry), true);
    failed += expect_u32("matrix delay value", entry.delay_ns, 120u);
    failed += expect_bool("matrix self loop path",
                          vdc_domain_observation_path_delay_lookup(
                              &context.path_delay, 2u, 2u, &entry), true);
    failed += expect_u32("matrix self loop delay", entry.delay_ns, 122u);
    table.observation_matrix.valid_bitmap[0] |= 1u << 4u;
    table.table_crc32 = vdc_domain_path_delay_table_crc32(&table);
    failed += expect_bool("matrix inactive reference rejected",
                          vdc_domain_publish_path_delay_table(&context,
                                                              &table), false);
    table.observation_matrix.valid_bitmap[0] &= ~(1u << 4u);
    table.observation_matrix.valid_bitmap[0] &= ~(1u << 8u);
    table.table_crc32 = vdc_domain_path_delay_table_crc32(&table);
    failed += expect_bool("incomplete matrix rejected",
                          vdc_domain_publish_path_delay_table(&context,
                                                              &table), false);

    vdc_path_delay_table_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    loaded.valid = 1u;
    loaded.version = VDC_DOMAIN_PATH_DELAY_TABLE_VERSION;
    loaded.update_seq = 1u;
    loaded.entry_count = 4u;
    loaded.schedule_crc32 = 0x1234u;
    loaded.calibration_generation = 1u;
    loaded.topology_generation = 1u;
    loaded.bias_generation = 1u;
    loaded.freshness_us = 1u;
    loaded.flags = VDC_PATH_DELAY_FLAG_ACCEPTED |
                   VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                   VDC_PATH_DELAY_FLAG_BIAS_VALID |
                   VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH;
    /* MARK follows 0->1->2->3, while DPLL samples the process image on the
     * reverse DATA ring: 0->3->2->1->0.  The observer path from a reference
     * to a local source must therefore accumulate DATA-directed links. */
    const uint32_t data_next[4] = {3u, 0u, 1u, 2u};
    const uint32_t data_delay[4] = {10u, 20u, 30u, 40u};
    for (uint32_t i = 0u; i < 4u; i++) {
        loaded.entries[i].valid = 1u;
        loaded.entries[i].source_slot_id = i;
        loaded.entries[i].reference_slot_id = data_next[i];
        loaded.entries[i].direction =
            VDC_PATH_DELAY_DIRECTION_TDMA_DATA_REVERSE;
        loaded.entries[i].delay_ns = data_delay[i];
        loaded.entries[i].cal_crc32 = loaded.schedule_crc32;
        loaded.entries[i].freshness_us = loaded.freshness_us;
        loaded.entries[i].update_seq = loaded.update_seq;
    }
    loaded.table_crc32 = vdc_domain_path_delay_table_crc32(&loaded);
    failed += expect_bool("load matrix from directed links",
                          vdc_domain_load_observation_path_matrix(
                              &loaded, 4u), true);
    loaded.table_crc32 = vdc_domain_path_delay_table_crc32(&loaded);
    failed += expect_bool("loaded reverse-data matrix 0 from 2",
                          vdc_domain_observation_path_delay_lookup(
                              &loaded, 0u, 2u, &entry), true);
    failed += expect_u32("loaded reverse-data matrix 0 from 2 delay",
                         entry.delay_ns, 50u);
    failed += expect_bool("loaded reverse-data matrix 2 from 0",
                          vdc_domain_observation_path_delay_lookup(
                              &loaded, 2u, 0u, &entry), true);
    failed += expect_u32("loaded reverse-data matrix 2 from 0 delay",
                         entry.delay_ns, 50u);
    failed += expect_bool("loaded reverse-data reference loop",
                          vdc_domain_observation_path_delay_lookup(
                              &loaded, 0u, 0u, &entry), true);
    failed += expect_u32("loaded reverse-data reference loop delay",
                         entry.delay_ns, 100u);

    loaded.entries[3].reference_slot_id = 3u;
    failed += expect_bool("non-ring directed links rejected",
                          vdc_domain_load_observation_path_matrix(
                              &loaded, 4u), false);
    return failed;
}

static int test_tdma_configuration_activation_is_atomic(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_tdma_schedule_profile_t runtime_schedule;
    vdc_timestamp_dictionary_t dictionary;
    vdc_path_delay_table_t path_delay;
    tdma_operating_profile_t operating;

    failed += expect_bool("runtime activation init",
                          vdc_domain_init(&context), true);
    failed += expect_bool("runtime activation topology",
                          vdc_domain_set_schedule_ring_topology(
                              &context, 2u, 0u, 4u), true);
    failed += expect_bool("runtime activation profile",
                          tdma_operating_profile_get(7u, &operating), true);
    const uint32_t base_schedule_crc32 = context.schedule.schedule_crc32;
    const uint32_t effective_schedule_crc32 =
        tdma_operating_profile_schedule_crc32(base_schedule_crc32,
                                               &operating);
    const vdc_tdma_runtime_binding_t binding = {
        .node_count = 4u,
        .local_slot_id = 2u,
        .reference_slot_id = 0u,
        .ring_profile_crc32 = context.schedule.ring_binding.profile_crc32,
        .operating_profile_crc32 = operating.profile_crc32,
        .cycle_period_ns = operating.cycle_period_ns,
        .effective_schedule_crc32 = effective_schedule_crc32,
    };
    failed += expect_bool("runtime schedule build",
                          vdc_domain_build_tdma_runtime_schedule(
                              &context.schedule, &binding,
                              &runtime_schedule), true);
    failed += expect_u32("runtime effective schedule",
                         runtime_schedule.schedule_crc32,
                         effective_schedule_crc32);
    failed += expect_u32("runtime operating profile",
                         runtime_schedule.operating_profile_crc32,
                         operating.profile_crc32);

    vdc_domain_default_timestamp_dictionary(&dictionary, &runtime_schedule);
    memset(&path_delay, 0, sizeof(path_delay));
    path_delay.valid = 1u;
    path_delay.version = VDC_DOMAIN_PATH_DELAY_TABLE_VERSION;
    path_delay.update_seq = 2u;
    path_delay.entry_count = 4u;
    path_delay.schedule_crc32 = effective_schedule_crc32;
    path_delay.calibration_generation = 210u;
    path_delay.topology_generation = 3u;
    path_delay.bias_generation = 1u;
    path_delay.freshness_us = 1000000u;
    path_delay.flags = VDC_PATH_DELAY_FLAG_ACCEPTED |
                       VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                       VDC_PATH_DELAY_FLAG_BIAS_VALID |
                       VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH;
    for (uint32_t i = 0u; i < 4u; i++) {
        path_delay.entries[i].valid = 1u;
        path_delay.entries[i].source_slot_id = i;
        path_delay.entries[i].reference_slot_id = (i + 1u) % 4u;
        path_delay.entries[i].delay_ns = 80u + i;
        path_delay.entries[i].cal_crc32 = 0x12340000u + i;
        path_delay.entries[i].freshness_us = path_delay.freshness_us;
        path_delay.entries[i].writer = i;
        path_delay.entries[i].update_seq = path_delay.update_seq;
    }
    failed += expect_bool("runtime matrix load",
                          vdc_domain_load_observation_path_matrix(
                              &path_delay, 4u), true);
    path_delay.table_crc32 =
        vdc_domain_path_delay_table_crc32(&path_delay);

    vdc_domain_set_ready(&context, true);
    failed += expect_bool("runtime configuration activate",
                          vdc_domain_activate_tdma_configuration(
                              &context, &runtime_schedule, &dictionary,
                              &path_delay), true);
    failed += expect_u32("runtime context schedule",
                         context.schedule.schedule_crc32,
                         effective_schedule_crc32);
    failed += expect_u32("runtime dictionary schedule",
                         context.timestamp_dictionary.profile_crc32,
                         effective_schedule_crc32);
    failed += expect_u32("runtime path schedule",
                         context.path_delay.schedule_crc32,
                         effective_schedule_crc32);
    failed += expect_u32("runtime activation checking",
                         context.dpll.state,
                         VDC_DOMAIN_LOCK_CHECKING);

    const uint32_t accepted_schedule_crc32 =
        context.schedule.schedule_crc32;
    path_delay.schedule_crc32 ^= 1u;
    path_delay.table_crc32 =
        vdc_domain_path_delay_table_crc32(&path_delay);
    failed += expect_bool("runtime partial activation rejected",
                          vdc_domain_activate_tdma_configuration(
                              &context, &runtime_schedule, &dictionary,
                              &path_delay), false);
    failed += expect_u32("runtime rejected activation unchanged",
                         context.schedule.schedule_crc32,
                         accepted_schedule_crc32);

    vdc_tdma_runtime_binding_t mismatched = binding;
    mismatched.effective_schedule_crc32 ^= 1u;
    failed += expect_bool("runtime effective crc mismatch rejected",
                          vdc_domain_build_tdma_runtime_schedule(
                              &context.schedule, &mismatched,
                              &runtime_schedule), false);
    return failed;
}

static int test_ring_observer_expands_correlated_feedback(void)
{
    int failed = 0;
    vdc_tdma_schedule_profile_t schedule;
    vdc_tdma_timestamp_evidence_t evidence;
    vdc_ring_observation_t observation;

    vdc_domain_default_schedule(&schedule, 1u, 0u);
    memset(&observation, 0, sizeof(observation));
    observation.node_count = schedule.ring_binding.node_count;
    observation.source_node = schedule.local_slot_id;
    observation.reference_node = schedule.reference_slot_id;
    observation.correlated_sequence = 17u;
    observation.frame_crc32 = 0x12345678u;
    observation.schedule_crc32 = schedule.schedule_crc32;
    observation.timestamp_resolution_ns = 4u;
    observation.timestamp_flags =
        VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    observation.correlated_frame_evidence = 1u;
    observation.link_delay_ns = 480u;
    observation.reference_tx_timestamp_ns = 2000000ull;
    observation.local_rx_timestamp_ns = 2000496ull;

    failed += expect_bool("ring feedback expands",
                          vdc_ring_observer_expand(&schedule,
                                                   &observation,
                                                   &evidence),
                          true);
    failed += expect_u32("ring feedback sequence",
                         evidence.sample_seq,
                         observation.correlated_sequence);
    failed += expect_u32("ring feedback payload",
                         evidence.payload_class,
                         VDC_DOMAIN_PAYLOAD_IDLE_BEACON);
    failed += expect_i32("ring feedback residual",
                         evidence.phase_error_ns,
                         16);
    failed += expect_u32("ring feedback delay",
                         evidence.delay_ns,
                         observation.link_delay_ns);
    failed += expect_u32("ring feedback source",
                         evidence.timestamp_source,
                         VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK);
    failed += expect_u32("ring feedback window",
                         (uint32_t)evidence.expected_window_start_ns,
                         2000000u + schedule.observation_window_offset_ns);

    failed += expect_bool("active ring feedback expands",
                          vdc_ring_observer_expand_active(&schedule,
                                                         &observation,
                                                         &evidence),
                          true);
    const uint32_t active_period_ns = schedule.period_ns;
    schedule.period_ns = 0u;
    failed += expect_bool("active ring feedback rejects invalid period",
                          vdc_ring_observer_expand_active(&schedule,
                                                         &observation,
                                                         &evidence),
                          false);
    schedule.period_ns = active_period_ns;

    observation.reference_tx_timestamp_ns = 4000123000ull;
    observation.local_rx_timestamp_ns = 2000123496ull;
    failed += expect_bool("ring feedback asynchronous epoch expands",
                          vdc_ring_observer_expand(&schedule,
                                                   &observation,
                                                   &evidence),
                          true);
    failed += expect_i32("ring feedback asynchronous epoch residual",
                         evidence.phase_error_ns,
                         16);

    vdc_tdma_schedule_profile_t reference_schedule;
    failed += expect_bool("reference schedule topology",
                          vdc_domain_default_schedule_for_topology(
                              &reference_schedule,
                              schedule.reference_slot_id,
                              schedule.reference_slot_id,
                              schedule.ring_binding.node_count),
                          true);
    observation.source_node = observation.reference_node;
    observation.schedule_crc32 = reference_schedule.schedule_crc32;
    failed += expect_bool("reference loop observation expands",
                          vdc_ring_observer_expand(&reference_schedule,
                                                   &observation,
                                                   &evidence),
                          true);
    observation.source_node = schedule.local_slot_id;
    observation.timestamp_flags |=
        VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    failed += expect_bool("diagnostic ring feedback rejected",
                          vdc_ring_observer_expand(&schedule,
                                                   &observation,
                                                   &evidence),
                          false);
    return failed;
}

static int test_provisional_path_matrix_is_servo_only(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_path_delay_table_t table;
    vdc_timestamp_dictionary_t dictionary;
    vdc_path_delay_entry_t entry;

    failed += expect_bool("provisional context init",
                          vdc_domain_init(&context), true);
    vdc_tdma_timestamp_evidence_t active_evidence =
        make_hardware_sample(&context.schedule, 1u, 0);
    failed += expect_bool("active submit rejects unactivated context",
                          vdc_domain_submit_active_tdma_evidence(
                              &context, &active_evidence), false);
    memset(&table, 0, sizeof(table));
    table.valid = 1u;
    table.version = VDC_DOMAIN_PATH_DELAY_TABLE_VERSION;
    table.update_seq = 2u;
    table.entry_count = context.schedule.ring_binding.node_count;
    table.schedule_crc32 = context.schedule.schedule_crc32;
    table.calibration_generation = 210u;
    table.topology_generation = 3u;
    table.bias_generation = 0u;
    table.freshness_us = 1u;
    table.flags = VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                  VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY;
    for (uint32_t i = 0u; i < table.entry_count; i++) {
        table.entries[i].valid = 1u;
        table.entries[i].source_slot_id = i;
        table.entries[i].reference_slot_id = (i + 1u) % table.entry_count;
        table.entries[i].delay_ns = 80u;
        table.entries[i].jitter_ns = 4u;
        table.entries[i].stddev_ns = 4u;
        table.entries[i].cal_crc32 = 0x12345678u;
        table.entries[i].freshness_us = 1u;
        table.entries[i].writer = i;
        table.entries[i].update_seq = table.update_seq;
    }
    failed += expect_bool("provisional observation matrix build",
                          vdc_domain_load_observation_path_matrix(
                              &table, table.entry_count), true);
    table.table_crc32 = vdc_domain_path_delay_table_crc32(&table);
    failed += expect_bool("formal validator rejects provisional",
                          vdc_domain_path_delay_table_validate(&table), false);
    failed += expect_bool("provisional validator accepts matrix",
                          vdc_domain_path_delay_table_validate_provisional(
                              &table), true);
    failed += expect_u32("provisional matrix node count",
                         table.observation_matrix.node_count,
                         table.entry_count);
    failed += expect_u32("provisional source1 reference0 bitmap",
                         table.observation_matrix.valid_bitmap[0] &
                             (1u << VDC_DOMAIN_NODE_COUNT),
                         1u << VDC_DOMAIN_NODE_COUNT);
    failed += expect_bool("formal lookup rejects provisional",
                          vdc_domain_path_delay_lookup(
                              &table, 1u, 0u, &entry), false);
    failed += expect_bool("observation lookup accepts provisional",
                          vdc_domain_observation_path_delay_lookup(
                              &table, 1u, 0u, &entry), true);
    failed += expect_u32("provisional cumulative delay", entry.delay_ns, 80u);
    failed += expect_bool("active observation lookup accepts frozen matrix",
                          vdc_domain_active_observation_path_delay_lookup(
                              &table, 1u, 0u, &entry), true);
    failed += expect_bool("active observation lookup accepts loop path",
                          vdc_domain_active_observation_path_delay_lookup(
                              &table, 1u, 1u, &entry), true);

    vdc_domain_default_timestamp_dictionary(&dictionary, &context.schedule);
    failed += expect_bool("formal activation rejects provisional",
                          vdc_domain_activate_tdma_configuration(
                              &context, &context.schedule, &dictionary,
                              &table), false);
    failed += expect_bool("provisional activation accepted",
                          vdc_domain_activate_tdma_provisional_configuration(
                              &context, &context.schedule, &dictionary,
                              &table), true);
    failed += expect_u32("provisional activation remains diagnostic",
                         context.path_delay.flags &
                             VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY,
                         VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY);
    vdc_domain_set_ready(&context, true);
    active_evidence = make_hardware_sample(&context.schedule, 2u, 0);
    vdc_tdma_evidence_preparation_t preparation;
    const uint32_t update_seq_before_prepare = context.dpll.update_seq;
    failed += expect_bool("active evidence prepare accepted",
                          vdc_domain_prepare_active_tdma_evidence(
                              &context, &active_evidence, &preparation), true);
    failed += expect_u32("active prepare is read only",
                         context.dpll.update_seq,
                         update_seq_before_prepare);
    failed += expect_u32("active preparation accepts sample",
                         preparation.accepted, 1u);
    context.dpll.update_seq++;
    bool prepared_accepted = false;
    failed += expect_bool("stale active preparation rejected",
                          vdc_domain_apply_prepared_tdma_evidence(
                              &context, &active_evidence, &preparation,
                              &prepared_accepted), false);
    failed += expect_bool("stale active preparation not accepted",
                          prepared_accepted, false);
    context.dpll.update_seq = update_seq_before_prepare;
    const uint32_t quality_seq_before_core = context.quality.update_seq;
    const uint32_t error_budget_seq_before_core =
        context.error_budget.update_seq;
    failed += expect_bool("fresh active preparation servo applied",
                          vdc_domain_apply_prepared_tdma_evidence_servo(
                              &context, &active_evidence, &preparation), true);
    failed += expect_u32("servo marks preparation applied",
                         preparation.servo_applied, 1u);
    failed += expect_u32("servo defers state application",
                         preparation.applied, 0u);
    failed += expect_u32("servo records post update sequence",
                         preparation.post_servo_dpll_update_seq,
                         context.dpll.update_seq);
    failed += expect_u32("servo defers quality update",
                         context.quality.update_seq,
                         quality_seq_before_core);
    context.dpll.update_seq++;
    failed += expect_bool("stale post servo sequence rejects state",
                          vdc_domain_apply_prepared_tdma_evidence_state(
                              &context, &active_evidence, &preparation,
                              &prepared_accepted), false);
    context.dpll.update_seq = preparation.post_servo_dpll_update_seq;
    failed += expect_bool("fresh active preparation state applied",
                          vdc_domain_apply_prepared_tdma_evidence_state(
                              &context, &active_evidence, &preparation,
                              &prepared_accepted), true);
    failed += expect_bool("fresh active preparation state accepted",
                          prepared_accepted, true);
    failed += expect_u32("state marks preparation applied",
                         preparation.applied, 1u);
    failed += expect_u32("state records post apply sequence",
                         preparation.post_apply_dpll_update_seq,
                         context.dpll.update_seq);
    failed += expect_u32("state defers quality update",
                         context.quality.update_seq,
                         quality_seq_before_core);
    failed += expect_u32("state defers error budget update",
                         context.error_budget.update_seq,
                         error_budget_seq_before_core);
    context.dpll.update_seq++;
    failed += expect_bool("stale post apply sequence rejects finalize",
                          vdc_domain_finalize_prepared_tdma_evidence(
                              &context, &active_evidence, &preparation),
                          false);
    context.dpll.update_seq = preparation.post_apply_dpll_update_seq;
    failed += expect_bool("fresh post apply sequence finalizes",
                          vdc_domain_finalize_prepared_tdma_evidence(
                              &context, &active_evidence, &preparation),
                          true);
    failed += expect_u32("finalize updates quality",
                         context.quality.update_seq,
                         quality_seq_before_core + 1u);
    failed += expect_u32("finalize updates error budget",
                         context.error_budget.update_seq,
                         error_budget_seq_before_core + 1u);
    active_evidence = make_hardware_sample(&context.schedule, 3u, 0);
    failed += expect_bool("active submit accepts activated context",
                          vdc_domain_submit_active_tdma_evidence(
                              &context, &active_evidence), true);
    return failed;
}

static int test_dpll_role_boundary_and_oscillator_discipline(void)
{
    int failed = 0;
    vdc_domain_context_t context;
    vdc_domain_snapshot_t snapshot;
    vdc_dpll_control_profile_t control_profile;
    vdc_dpll_follower_command_t command;
    vdc_oscillator_discipline_profile_t discipline_profile;
    vdc_oscillator_discipline_actuator_report_t actuator_report;

    failed += expect_bool("role boundary init", vdc_domain_init(&context), true);
    failed += expect_u32("role default master",
                         context.control.profile.mode,
                         VDC_DPLL_CONTROL_MODE_MASTER);
    failed += expect_u32("discipline default disabled",
                         context.oscillator_discipline.profile.enabled,
                         0u);
    failed += expect_u32("discipline default unavailable",
                         context.oscillator_discipline.actuator_available,
                         0u);
    vdc_domain_set_ready(&context, true);

    vdc_tdma_timestamp_evidence_t evidence =
        make_hardware_sample(&context.schedule, 1u, 100);
    failed += expect_bool("master evidence drives local PI",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    const vdc_clock_model_t master_clock = snapshot.clock;
    const vdc_dco_control_t master_dco = snapshot.dco;
    failed += expect_u32("master accepted sample",
                         snapshot.dpll.accepted_sample_count,
                         1u);
    failed += expect_bool("master DCO updated",
                          snapshot.dco.dco_update_seq > 1u,
                          true);

    control_profile = context.control.profile;
    control_profile.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    control_profile.follow_master_slot_id = 1u;
    failed += expect_bool("install follower role",
                          vdc_domain_set_dpll_control_profile(
                              &context, &control_profile),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("role change retains DCO rate",
                         snapshot.dco.period_adjust_ppb,
                         master_dco.period_adjust_ppb);
    failed += expect_i32("role change retains DCO phase",
                         snapshot.dco.phase_offset_ns,
                         master_dco.phase_offset_ns);
    failed += expect_u32("role switch resets local accepted count",
                         snapshot.dpll.accepted_sample_count,
                         0u);

    const uint32_t follower_quality_seq = snapshot.quality.update_seq;
    const uint32_t follower_dpll_seq = snapshot.dpll.update_seq;
    evidence = make_hardware_sample(&context.schedule, 2u, -500);
    failed += expect_bool("follower evidence remains diagnostic",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("follower gate still observable",
                         snapshot.gate.passed,
                         1u);
    failed += expect_u32("follower local PI bypass count",
                         snapshot.control.follower_local_evidence_bypass_count,
                         1u);
    failed += expect_u32("follower evidence leaves DPLL sequence",
                         snapshot.dpll.update_seq,
                         follower_dpll_seq);
    failed += expect_u32("follower evidence leaves quality history",
                         snapshot.quality.update_seq,
                         follower_quality_seq);
    failed += expect_u32("follower evidence leaves local accepted count",
                         snapshot.dpll.accepted_sample_count,
                         0u);
    failed += expect_u32("follower evidence leaves local clock sequence",
                         snapshot.clock.model_seq,
                         master_clock.model_seq);
    failed += expect_i32("follower evidence leaves DCO rate",
                         snapshot.dco.period_adjust_ppb,
                         master_dco.period_adjust_ppb);
    failed += expect_i32("follower evidence leaves DCO phase",
                         snapshot.dco.phase_offset_ns,
                         master_dco.phase_offset_ns);

    (void)memset(&command, 0, sizeof(command));
    command.valid = 1u;
    command.source_slot_id = 1u;
    /* Command generation is owned by the publishing master, not by this
     * follower's local role profile generation. */
    command.control_generation = 77u;
    command.command_seq = 1u;
    command.schedule_crc32 = context.schedule.schedule_crc32;
    command.effective_vdc_time_ns = 2000000u;
    command.period_adjust_ppb = 321;
    command.phase_offset_ns = -77;
    command.lock_state = VDC_DOMAIN_LOCK_LOCKED;
    command.quality = VDC_DOMAIN_HEALTH_HEALTHY;
    const uint64_t local_tick_before_command = snapshot.dco.base_local_tick64;
    const uint64_t local_vdc_before_command = snapshot.dco.base_vdc_time64_ns;
    failed += expect_bool("follower accepts selected master command",
                          vdc_domain_apply_follower_command(&context, &command),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("follower applies command count",
                         snapshot.control.follower_apply_count,
                         1u);
    failed += expect_u32("follower records command source",
                         snapshot.control.last_follower_source_slot_id,
                         1u);
    failed += expect_u32("follower records command sequence",
                         snapshot.control.last_follower_command_seq,
                         1u);
    failed += expect_i32("follower command sets DCO rate",
                         snapshot.dco.period_adjust_ppb,
                         321);
    failed += expect_i32("follower command sets DCO phase",
                         snapshot.dco.phase_offset_ns,
                         -77);
    failed += expect_u32("follower command exposes peer lock only on DCO",
                         snapshot.dco.lock_state,
                         VDC_DOMAIN_LOCK_LOCKED);
    failed += expect_u32("follower local state remains checking",
                         snapshot.dpll.state,
                         VDC_DOMAIN_LOCK_CHECKING);
    failed += expect_u32("follower never imports peer quality samples",
                         snapshot.quality.accepted_sample_count,
                         0u);
    failed += expect_u64("follower preserves local DCO tick anchor",
                         snapshot.dco.base_local_tick64,
                         local_tick_before_command);
    failed += expect_u64("follower preserves local DCO VDC anchor",
                         snapshot.dco.base_vdc_time64_ns,
                         local_vdc_before_command);

    command.command_seq = 2u;
    command.effective_vdc_time_ns = 2000000u;
    failed += expect_bool("follower rejects non-monotonic effective time",
                          vdc_domain_apply_follower_command(&context, &command),
                          false);

    const vdc_dco_control_t applied_follower_dco = snapshot.dco;
    command.source_slot_id = 2u;
    command.command_seq = 2u;
    failed += expect_bool("follower rejects wrong source",
                          vdc_domain_apply_follower_command(&context, &command),
                          false);
    command.source_slot_id = 1u;
    command.schedule_crc32 ^= 1u;
    failed += expect_bool("follower rejects wrong schedule",
                          vdc_domain_apply_follower_command(&context, &command),
                          false);
    command.schedule_crc32 = context.schedule.schedule_crc32;
    command.command_seq = 1u;
    failed += expect_bool("follower rejects stale sequence",
                          vdc_domain_apply_follower_command(&context, &command),
                          false);
    vdc_domain_note_follower_command_missing(&context);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("wrong source counted",
                         snapshot.control.follower_wrong_source_count,
                         1u);
    failed += expect_u32("invalid command counted",
                         snapshot.control.follower_invalid_command_count,
                         1u);
    failed += expect_u32("stale commands counted",
                         snapshot.control.follower_stale_command_count,
                         2u);
    failed += expect_u32("missing command counted",
                         snapshot.control.follower_no_command_count,
                         1u);
    failed += expect_i32("bad follower commands retain DCO rate",
                         snapshot.dco.period_adjust_ppb,
                         applied_follower_dco.period_adjust_ppb);
    failed += expect_i32("bad follower commands retain DCO phase",
                         snapshot.dco.phase_offset_ns,
                         applied_follower_dco.phase_offset_ns);

    control_profile = context.control.profile;
    control_profile.mode = VDC_DPLL_CONTROL_MODE_MASTER;
    control_profile.follow_master_slot_id = 0u;
    failed += expect_bool("restore master role",
                          vdc_domain_set_dpll_control_profile(
                              &context, &control_profile),
                          true);
    evidence = make_hardware_sample(&context.schedule, 3u, 100);
    failed += expect_bool("restored master resumes local PI",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("restored master accepts evidence",
                         snapshot.dpll.accepted_sample_count,
                         1u);

    discipline_profile = context.oscillator_discipline.profile;
    discipline_profile.enabled = 1u;
    discipline_profile.minimum_update_interval_us = 1000u;
    discipline_profile.max_abs_trim_ppb = 1000u;
    discipline_profile.max_step_ppb = 100u;
    discipline_profile.actuator_polarity = 1;
    failed += expect_bool("enable bounded oscillator discipline",
                          vdc_domain_set_oscillator_discipline_profile(
                              &context, &discipline_profile),
                          true);
    (void)memset(&actuator_report, 0, sizeof(actuator_report));
    actuator_report.valid = 1u;
    const vdc_dco_control_t dco_before_report = snapshot.dco;
    failed += expect_bool("report unavailable actuator",
                          vdc_domain_report_oscillator_discipline_actuator(
                              &context, &actuator_report),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("actuator report cannot change DCO rate",
                         snapshot.dco.period_adjust_ppb,
                         dco_before_report.period_adjust_ppb);
    failed += expect_i32("actuator report cannot change DCO phase",
                         snapshot.dco.phase_offset_ns,
                         dco_before_report.phase_offset_ns);

    evidence = make_hardware_sample(&context.schedule, 4u, 100);
    failed += expect_bool("master evidence freezes unavailable actuator",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("unavailable actuator reason",
                         snapshot.oscillator_discipline.freeze_reason,
                         VDC_OSCILLATOR_DISCIPLINE_FREEZE_UNAVAILABLE);
    failed += expect_bool("unavailable actuator counted",
                          snapshot.oscillator_discipline.unavailable_count > 0u,
                          true);

    actuator_report.available = 1u;
    actuator_report.healthy = 1u;
    failed += expect_bool("report healthy actuator",
                          vdc_domain_report_oscillator_discipline_actuator(
                              &context, &actuator_report),
                          true);
    evidence = make_hardware_sample(&context.schedule, 5u, 100);
    failed += expect_bool("master issues bounded trim request",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_bool("trim request generation issued",
                          snapshot.oscillator_discipline.request_generation > 0u,
                          true);
    failed += expect_bool("trim request is step limited",
                          snapshot.oscillator_discipline.requested_trim_ppb <= 100 &&
                              snapshot.oscillator_discipline.requested_trim_ppb >= -100,
                          true);
    const vdc_dco_control_t dco_before_ack = snapshot.dco;
    actuator_report.applied_generation =
        snapshot.oscillator_discipline.request_generation;
    actuator_report.applied_trim_ppb =
        snapshot.oscillator_discipline.requested_trim_ppb;
    failed += expect_bool("acknowledge trim request",
                          vdc_domain_report_oscillator_discipline_actuator(
                              &context, &actuator_report),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_i32("trim acknowledgement cannot change DCO rate",
                         snapshot.dco.period_adjust_ppb,
                         dco_before_ack.period_adjust_ppb);
    failed += expect_i32("trim acknowledgement cannot change DCO phase",
                         snapshot.dco.phase_offset_ns,
                         dco_before_ack.phase_offset_ns);
    failed += expect_u32("trim acknowledgement recorded",
                         snapshot.oscillator_discipline.applied_generation,
                         actuator_report.applied_generation);

    actuator_report.healthy = 0u;
    failed += expect_bool("report actuator fault",
                          vdc_domain_report_oscillator_discipline_actuator(
                              &context, &actuator_report),
                          true);
    evidence = make_hardware_sample(&context.schedule, 6u, 100);
    failed += expect_bool("master freezes faulty actuator",
                          vdc_domain_submit_tdma_evidence(&context, &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&context, &snapshot);
    failed += expect_u32("faulty actuator reason",
                         snapshot.oscillator_discipline.freeze_reason,
                         VDC_OSCILLATOR_DISCIPLINE_FREEZE_FAULT);
    failed += expect_bool("faulty actuator counted",
                          snapshot.oscillator_discipline.fault_count > 0u,
                          true);
    return failed;
}

#define VDC_ROLE_MATRIX_NODE_COUNT 4u

static bool init_role_matrix_node(vdc_domain_context_t *context,
                                  uint32_t local_slot_id)
{
    vdc_tdma_schedule_profile_t schedule;
    vdc_timestamp_dictionary_t dictionary;
    vdc_path_delay_table_t path_delay;
    if (context == NULL || local_slot_id >= VDC_ROLE_MATRIX_NODE_COUNT ||
        !vdc_domain_init(context)) {
        return false;
    }

    if (!vdc_domain_default_schedule_for_topology(
            &schedule, local_slot_id, 0u, VDC_ROLE_MATRIX_NODE_COUNT)) {
        return false;
    }
    vdc_domain_default_timestamp_dictionary(&dictionary, &schedule);
    (void)memset(&path_delay, 0, sizeof(path_delay));
    path_delay.valid = 1u;
    path_delay.version = VDC_DOMAIN_PATH_DELAY_TABLE_VERSION;
    path_delay.update_seq = 1u;
    path_delay.entry_count = VDC_ROLE_MATRIX_NODE_COUNT;
    path_delay.schedule_crc32 = schedule.schedule_crc32;
    path_delay.calibration_generation = 1u;
    path_delay.topology_generation = 1u;
    path_delay.bias_generation = 1u;
    path_delay.freshness_us = 1000000u;
    path_delay.flags = VDC_PATH_DELAY_FLAG_ACCEPTED |
                       VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                       VDC_PATH_DELAY_FLAG_BIAS_VALID |
                       VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH;
    for (uint32_t slot = 0u; slot < VDC_ROLE_MATRIX_NODE_COUNT; slot++) {
        path_delay.entries[slot].valid = 1u;
        path_delay.entries[slot].source_slot_id = slot;
        path_delay.entries[slot].reference_slot_id =
            (slot + 1u) % VDC_ROLE_MATRIX_NODE_COUNT;
        path_delay.entries[slot].delay_ns = 80u + slot;
        path_delay.entries[slot].cal_crc32 = 0xA000u + slot;
        path_delay.entries[slot].freshness_us = path_delay.freshness_us;
        path_delay.entries[slot].writer = slot;
        path_delay.entries[slot].update_seq = path_delay.update_seq;
    }
    if (!vdc_domain_load_observation_path_matrix(
            &path_delay, VDC_ROLE_MATRIX_NODE_COUNT)) {
        return false;
    }
    path_delay.table_crc32 = vdc_domain_path_delay_table_crc32(&path_delay);
    vdc_domain_set_ready(context, true);
    return vdc_domain_activate_tdma_configuration(context, &schedule,
                                                  &dictionary, &path_delay);
}

static bool set_role_matrix_profile(vdc_domain_context_t *context,
                                    uint32_t mode,
                                    uint32_t follow_master_slot_id)
{
    if (context == NULL) {
        return false;
    }
    vdc_dpll_control_profile_t profile = context->control.profile;
    profile.mode = mode;
    profile.follow_master_slot_id = follow_master_slot_id;
    return vdc_domain_set_dpll_control_profile(context, &profile);
}

static vdc_dpll_follower_command_t make_role_matrix_command(
    const vdc_domain_context_t *master,
    uint32_t command_seq,
    uint64_t effective_vdc_time_ns)
{
    vdc_dpll_follower_command_t command;
    (void)memset(&command, 0, sizeof(command));
    command.valid = 1u;
    command.source_slot_id = master->schedule.local_slot_id;
    command.control_generation = master->control.profile.generation;
    command.command_seq = command_seq;
    command.schedule_crc32 = master->schedule.schedule_crc32;
    command.effective_vdc_time_ns = effective_vdc_time_ns;
    command.period_adjust_ppb = master->dco.period_adjust_ppb;
    command.phase_offset_ns = master->dco.phase_offset_ns;
    command.lock_state = master->dco.lock_state;
    command.quality = master->quality.health_state;
    return command;
}

static int test_dpll_role_matrix_and_source_switch(void)
{
    int failed = 0;
    vdc_domain_context_t nodes[VDC_ROLE_MATRIX_NODE_COUNT];
    vdc_domain_snapshot_t snapshot;
    const uint64_t effective_time_ns = 2000000u;

    for (uint32_t slot = 0u; slot < VDC_ROLE_MATRIX_NODE_COUNT; slot++) {
        failed += expect_bool("role matrix node init",
                              init_role_matrix_node(&nodes[slot], slot), true);
    }

    /* 1M3F: all followers must copy NO1 (slot 0), not calculate locally. */
    for (uint32_t slot = 1u; slot < VDC_ROLE_MATRIX_NODE_COUNT; slot++) {
        failed += expect_bool("1M3F follower profile",
                              set_role_matrix_profile(&nodes[slot],
                                                      VDC_DPLL_CONTROL_MODE_FOLLOWER,
                                                      0u), true);
    }
    vdc_tdma_timestamp_evidence_t evidence =
        make_hardware_sample(&nodes[0].schedule, 1u, 100);
    failed += expect_bool("1M3F master computes local DCO",
                          vdc_domain_submit_tdma_evidence(&nodes[0], &evidence),
                          true);
    const vdc_dpll_follower_command_t no1_command =
        make_role_matrix_command(&nodes[0], 1u, effective_time_ns);
    for (uint32_t slot = 1u; slot < VDC_ROLE_MATRIX_NODE_COUNT; slot++) {
        failed += expect_bool("1M3F follower applies NO1 command",
                              vdc_domain_apply_follower_command(
                                  &nodes[slot], &no1_command), true);
        (void)vdc_domain_get_snapshot(&nodes[slot], &snapshot);
        failed += expect_i32("1M3F rate copied from NO1",
                             snapshot.dco.period_adjust_ppb,
                             nodes[0].dco.period_adjust_ppb);
        failed += expect_i32("1M3F phase copied from NO1",
                             snapshot.dco.phase_offset_ns,
                             nodes[0].dco.phase_offset_ns);
        const uint32_t follower_dco_seq = snapshot.dco.dco_update_seq;
        evidence = make_hardware_sample(&nodes[slot].schedule, 2u, -500);
        (void)vdc_domain_submit_tdma_evidence(&nodes[slot], &evidence);
        (void)vdc_domain_get_snapshot(&nodes[slot], &snapshot);
        failed += expect_u32("1M3F local evidence leaves DCO unchanged",
                             snapshot.dco.dco_update_seq, follower_dco_seq);
    }

    /* 2M2F: independent follower sources must not be cross-coupled. */
    failed += expect_bool("2M2F NO1 restored master",
                          set_role_matrix_profile(&nodes[1],
                                                  VDC_DPLL_CONTROL_MODE_MASTER,
                                                  0u), true);
    failed += expect_bool("2M2F NO3 restored master",
                          set_role_matrix_profile(&nodes[2],
                                                  VDC_DPLL_CONTROL_MODE_MASTER,
                                                  0u), true);
    failed += expect_bool("2M2F NO2 follows NO1",
                          set_role_matrix_profile(&nodes[1],
                                                  VDC_DPLL_CONTROL_MODE_FOLLOWER,
                                                  0u), true);
    failed += expect_bool("2M2F NO4 follows NO3",
                          set_role_matrix_profile(&nodes[3],
                                                  VDC_DPLL_CONTROL_MODE_FOLLOWER,
                                                  2u), true);
    evidence = make_hardware_sample(&nodes[0].schedule, 3u, 200);
    failed += expect_bool("2M2F NO1 computes",
                          vdc_domain_submit_tdma_evidence(&nodes[0], &evidence),
                          true);
    evidence = make_hardware_sample(&nodes[2].schedule, 3u, -200);
    failed += expect_bool("2M2F NO3 computes",
                          vdc_domain_submit_tdma_evidence(&nodes[2], &evidence),
                          true);
    const vdc_dpll_follower_command_t no1_command_2 =
        make_role_matrix_command(&nodes[0], 2u, effective_time_ns + 1u);
    const vdc_dpll_follower_command_t no3_command =
        make_role_matrix_command(&nodes[2], 1u, effective_time_ns + 1u);
    failed += expect_bool("2M2F NO2 applies NO1",
                          vdc_domain_apply_follower_command(&nodes[1],
                                                            &no1_command_2), true);
    failed += expect_bool("2M2F NO4 applies NO3",
                          vdc_domain_apply_follower_command(&nodes[3],
                                                            &no3_command), true);
    (void)vdc_domain_get_snapshot(&nodes[1], &snapshot);
    failed += expect_i32("2M2F NO2 only takes NO1 rate",
                         snapshot.dco.period_adjust_ppb,
                         nodes[0].dco.period_adjust_ppb);
    (void)vdc_domain_get_snapshot(&nodes[3], &snapshot);
    failed += expect_i32("2M2F NO4 only takes NO3 rate",
                         snapshot.dco.period_adjust_ppb,
                         nodes[2].dco.period_adjust_ppb);

    /* 3M1F and a source change use the newly selected command stream. */
    failed += expect_bool("3M1F NO2 master",
                          set_role_matrix_profile(&nodes[1],
                                                  VDC_DPLL_CONTROL_MODE_MASTER,
                                                  0u), true);
    evidence = make_hardware_sample(&nodes[1].schedule, 4u, 300);
    failed += expect_bool("3M1F NO2 computes",
                          vdc_domain_submit_tdma_evidence(&nodes[1], &evidence),
                          true);
    failed += expect_bool("source switch NO4 follows NO2",
                          set_role_matrix_profile(&nodes[3],
                                                  VDC_DPLL_CONTROL_MODE_FOLLOWER,
                                                  1u), true);
    const vdc_dpll_follower_command_t old_no3_command =
        make_role_matrix_command(&nodes[2], 2u, effective_time_ns + 2u);
    failed += expect_bool("source switch rejects old source",
                          vdc_domain_apply_follower_command(&nodes[3],
                                                            &old_no3_command), false);
    const vdc_dpll_follower_command_t no2_command =
        make_role_matrix_command(&nodes[1], 1u, effective_time_ns + 2u);
    failed += expect_bool("source switch accepts new source sequence",
                          vdc_domain_apply_follower_command(&nodes[3],
                                                            &no2_command), true);
    (void)vdc_domain_get_snapshot(&nodes[3], &snapshot);
    failed += expect_i32("3M1F NO4 takes NO2 rate",
                         snapshot.dco.period_adjust_ppb,
                         nodes[1].dco.period_adjust_ppb);

    failed += expect_bool("NO4 promotion to master",
                          set_role_matrix_profile(&nodes[3],
                                                  VDC_DPLL_CONTROL_MODE_MASTER,
                                                  0u), true);
    evidence = make_hardware_sample(&nodes[3].schedule, 5u, 400);
    failed += expect_bool("promoted NO4 resumes local PI",
                          vdc_domain_submit_tdma_evidence(&nodes[3], &evidence),
                          true);
    (void)vdc_domain_get_snapshot(&nodes[3], &snapshot);
    failed += expect_u32("promoted NO4 accepts local evidence",
                         snapshot.dpll.accepted_sample_count, 1u);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_default_schedule_and_clock();
    failed += test_default_servo_is_conservative();
    failed += test_tdma_ring_profile_contract();
    failed += test_tdma_ring_plan_contract();
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
    failed += test_path_delay_table_drives_compact_phase();
    failed += test_observation_path_matrix_is_explicit();
    failed += test_tdma_configuration_activation_is_atomic();
    failed += test_sync_io_adapter_to_vdc_submit();
    failed += test_quality_age_updates_on_service();
    failed += test_dpll_updates_clock_rate_from_sample_period();
    failed += test_dpll_rate_estimator_waits_and_slews();
    failed += test_dpll_rate_correction_enters_next_phase_prediction();
    failed += test_dpll_continues_through_large_phase_error();
    failed += test_dpll_slews_phase_and_pulls_rate_after_lock();
    failed += test_dpll_acquisition_accepts_large_initial_phase();
    failed += test_dpll_acquisition_continues_through_phase_innovation();
    failed += test_tracking_gate_ignores_stale_phase_model();
    failed += test_acquisition_gate_covers_full_cycle();
    failed += test_dpll_large_step_does_not_fine_lock_same_sample();
    failed += test_debug_servo_tune_accepts_extreme_profile_and_integrator();
    failed += test_debug_admission_continues_recoverable_gate();
    failed += test_ring_observer_expands_correlated_feedback();
    failed += test_provisional_path_matrix_is_servo_only();
    failed += test_dpll_role_boundary_and_oscillator_discipline();
    failed += test_dpll_role_matrix_and_source_switch();
    if (failed != 0) {
        (void)printf("vdc_domain tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("vdc_domain tests passed\n");
    return 0;
}
