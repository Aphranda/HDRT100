#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdma_traffic_scheduler.h"

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: got %lu expected %lu\n",
            name, (unsigned long)actual, (unsigned long)expected);
    return 1;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    return expect_u32(name, actual ? 1u : 0u, expected ? 1u : 0u);
}

static tdma_traffic_request_t make_request(uint32_t payload_class,
                                           uint8_t marker,
                                           uint64_t now_ns)
{
    static uint8_t frames[16][128];
    static uint32_t frame_index;
    uint8_t *frame = frames[frame_index % 16u];
    frame_index++;
    memset(frame, marker, 128u);
    tdma_traffic_request_t request = {
        .intent_type = 1u,
        .role = 1u,
        .baud_hz = 10000000u,
        .frame_class = 1u,
        .payload_class = payload_class,
        .enqueue_time_ns = now_ns,
        .estimated_duration_ns = 120000u,
        .frame_size = 128u,
        .frame = frame,
    };
    return request;
}

static int test_gate_and_priority(void)
{
    int failed = 0;
    tdma_foundation_profile_t profile;
    tdma_traffic_scheduler_t scheduler;
    tdma_traffic_scheduler_slot_t slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
    tdma_traffic_dispatch_t dispatch;
    tdma_traffic_scheduler_snapshot_t snapshot;
    const uint64_t now_ns = 1000000ull;

    failed += expect_bool("default profile",
                          tdma_foundation_profile_default(
                              &profile, 1u, 0u, 0u, TDMA_ADAPTER_PIO_SPI),
                          true);
    failed += expect_bool("init",
                          tdma_traffic_scheduler_init(
                              &scheduler,
                              slots,
                              TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT),
                          true);
    failed += expect_bool("configure",
                          tdma_traffic_scheduler_configure(&scheduler, &profile),
                          true);

    tdma_traffic_request_t config =
        make_request(TDMA_PAYLOAD_CLASS_CONFIG_CONTROL, 0xC1u, now_ns);
    tdma_traffic_request_t vdc =
        make_request(TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE, 0xA1u, now_ns);
    tdma_traffic_request_t refmem =
        make_request(TDMA_PAYLOAD_CLASS_REFMEM_DELTA, 0xB1u, now_ns);
    refmem.estimated_duration_ns = 300000u;
    vdc.scheduled_window_valid = 1u;
    vdc.scheduled_guard_start_ns = now_ns + 200000u;
    vdc.scheduled_window_start_ns = now_ns + 250000u;
    vdc.scheduled_window_end_ns = now_ns + 500000u;
    vdc.scheduled_guard_end_ns = now_ns + 550000u;

    failed += expect_u32("enqueue config",
                         tdma_traffic_scheduler_enqueue(&scheduler, &config),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("enqueue vdc",
                         tdma_traffic_scheduler_enqueue(&scheduler, &vdc),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("enqueue refmem",
                         tdma_traffic_scheduler_enqueue(&scheduler, &refmem),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("maintenance closed",
                         tdma_traffic_scheduler_select(
                             &scheduler, now_ns, false, &dispatch),
                         TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED);
    failed += expect_u32("maintenance fits before guard",
                         tdma_traffic_scheduler_select(
                             &scheduler, now_ns, true, &dispatch),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("config selected",
                         dispatch.traffic_class,
                         TDMA_TRAFFIC_CONFIG_CONTROL);
    failed += expect_u32("config marker", dispatch.frame[0], 0xC1u);
    failed += expect_bool("complete config",
                          tdma_traffic_scheduler_complete(
                              &scheduler,
                              dispatch.traffic_class,
                              TDMA_TRAFFIC_COMPLETION_SENT),
                          true);
    failed += expect_u32("vdc gate selection",
                         tdma_traffic_scheduler_select(
                             &scheduler, now_ns + 200000u, false, &dispatch),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("vdc selected",
                         dispatch.traffic_class,
                         TDMA_TRAFFIC_VDC_REALTIME);
    failed += expect_u32("vdc marker", dispatch.frame[0], 0xA1u);
    (void)tdma_traffic_scheduler_complete(
        &scheduler, dispatch.traffic_class, TDMA_TRAFFIC_COMPLETION_LATE);
    failed += expect_u32("refmem follows vdc",
                         tdma_traffic_scheduler_select(
                             &scheduler, now_ns + 300000u, false, &dispatch),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("refmem selected",
                         dispatch.traffic_class,
                         TDMA_TRAFFIC_REFMEM_REALTIME);

    failed += expect_bool("snapshot",
                          tdma_traffic_scheduler_get_snapshot(&scheduler,
                                                              &snapshot),
                          true);
    failed += expect_u32("config sent",
                         snapshot.traffic[TDMA_TRAFFIC_CONFIG_CONTROL].sent_count,
                         1u);
    failed += expect_u32("vdc late",
                         snapshot.traffic[TDMA_TRAFFIC_VDC_REALTIME].late_count,
                         1u);
    return failed;
}

static int test_overflow_policies(void)
{
    int failed = 0;
    tdma_foundation_profile_t profile;
    tdma_traffic_scheduler_t scheduler;
    tdma_traffic_scheduler_slot_t slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
    tdma_traffic_dispatch_t dispatch;
    tdma_traffic_scheduler_snapshot_t snapshot;
    const uint64_t now_ns = 2000000ull;

    (void)tdma_foundation_profile_default(
        &profile, 1u, 0u, 0u, TDMA_ADAPTER_PIO_SPI);
    profile.resource.traffic[TDMA_TRAFFIC_VDC_REALTIME].queue_depth = 1u;
    profile.resource.traffic[TDMA_TRAFFIC_REFMEM_REALTIME].queue_depth = 1u;
    profile.resource.traffic[TDMA_TRAFFIC_LOG_BEST_EFFORT].queue_depth = 2u;
    profile.profile_crc32 = tdma_foundation_profile_crc32(&profile);
    (void)tdma_traffic_scheduler_init(
        &scheduler, slots, TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT);
    failed += expect_bool("configure overflow profile",
                          tdma_traffic_scheduler_configure(&scheduler, &profile),
                          true);

    tdma_traffic_request_t vdc1 =
        make_request(TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE, 1u, now_ns);
    tdma_traffic_request_t vdc2 =
        make_request(TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE, 2u, now_ns);
    failed += expect_u32("vdc first",
                         tdma_traffic_scheduler_enqueue(&scheduler, &vdc1),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("vdc overflow faults",
                         tdma_traffic_scheduler_enqueue(&scheduler, &vdc2),
                         TDMA_TRAFFIC_SCHEDULER_FAULT);

    tdma_traffic_request_t ref1 =
        make_request(TDMA_PAYLOAD_CLASS_REFMEM_DELTA, 3u, now_ns);
    tdma_traffic_request_t ref2 =
        make_request(TDMA_PAYLOAD_CLASS_REFMEM_DELTA, 4u, now_ns);
    failed += expect_u32("ref first",
                         tdma_traffic_scheduler_enqueue(&scheduler, &ref1),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("ref backpressure",
                         tdma_traffic_scheduler_enqueue(&scheduler, &ref2),
                         TDMA_TRAFFIC_SCHEDULER_BACKPRESSURE);

    tdma_traffic_request_t log1 =
        make_request(TDMA_PAYLOAD_CLASS_LOG_STREAM, 5u, now_ns);
    tdma_traffic_request_t log2 =
        make_request(TDMA_PAYLOAD_CLASS_LOG_STREAM, 6u, now_ns);
    tdma_traffic_request_t log3 =
        make_request(TDMA_PAYLOAD_CLASS_LOG_STREAM, 7u, now_ns);
    log1.frame_class = TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_LONG;
    log2.frame_class = TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_LONG;
    log3.frame_class = TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_LONG;
    (void)tdma_traffic_scheduler_enqueue(&scheduler, &log1);
    (void)tdma_traffic_scheduler_enqueue(&scheduler, &log2);
    failed += expect_u32("log drops oldest",
                         tdma_traffic_scheduler_enqueue(&scheduler, &log3),
                         TDMA_TRAFFIC_SCHEDULER_DROPPED_OLDEST);

    (void)tdma_traffic_scheduler_clear_fault(&scheduler);
    (void)tdma_traffic_scheduler_select(
        &scheduler, now_ns, false, &dispatch);
    (void)tdma_traffic_scheduler_complete(
        &scheduler, dispatch.traffic_class, TDMA_TRAFFIC_COMPLETION_SENT);
    (void)tdma_traffic_scheduler_select(
        &scheduler, now_ns, false, &dispatch);
    (void)tdma_traffic_scheduler_complete(
        &scheduler, dispatch.traffic_class, TDMA_TRAFFIC_COMPLETION_SENT);
    failed += expect_u32("first surviving log",
                         tdma_traffic_scheduler_select(
                             &scheduler, now_ns, true, &dispatch),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("log oldest removed", dispatch.frame[0], 6u);

    (void)tdma_traffic_scheduler_get_snapshot(&scheduler, &snapshot);
    failed += expect_u32("fault cleared", snapshot.fault_latched, 0u);
    failed += expect_u32("ref backpressure count",
                         snapshot.traffic[TDMA_TRAFFIC_REFMEM_REALTIME]
                             .backpressure_count,
                         1u);
    failed += expect_u32("log drop count",
                         snapshot.traffic[TDMA_TRAFFIC_LOG_BEST_EFFORT]
                             .drop_count,
                         1u);
    return failed;
}

static int test_short_long_class_gate(void)
{
    int failed = 0;
    tdma_foundation_profile_t profile;
    tdma_traffic_scheduler_t scheduler;
    tdma_traffic_scheduler_slot_t slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
    const uint64_t now_ns = 4000000ull;

    (void)tdma_foundation_profile_default(
        &profile, 1u, 0u, 0u, TDMA_ADAPTER_PIO_SPI);
    (void)tdma_traffic_scheduler_init(
        &scheduler, slots, TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT);
    (void)tdma_traffic_scheduler_configure(&scheduler, &profile);

    tdma_traffic_request_t long_vdc =
        make_request(TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE, 11u, now_ns);
    long_vdc.frame_class = TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_LONG;
    failed += expect_u32("long vdc rejected",
                         tdma_traffic_scheduler_enqueue(&scheduler, &long_vdc),
                         TDMA_TRAFFIC_SCHEDULER_CLASS_REJECTED);

    tdma_traffic_request_t short_log =
        make_request(TDMA_PAYLOAD_CLASS_LOG_STREAM, 12u, now_ns);
    failed += expect_u32("short log rejected",
                         tdma_traffic_scheduler_enqueue(&scheduler, &short_log),
                         TDMA_TRAFFIC_SCHEDULER_CLASS_REJECTED);

    tdma_traffic_request_t storage =
        make_request(TDMA_PAYLOAD_CLASS_STORAGE_BULK, 13u, now_ns);
    storage.frame_class = TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_LONG;
    failed += expect_u32("long storage accepted",
                         tdma_traffic_scheduler_enqueue(&scheduler, &storage),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    return failed;
}

static int test_budget_and_deadline(void)
{
    int failed = 0;
    tdma_foundation_profile_t profile;
    tdma_traffic_scheduler_t scheduler;
    tdma_traffic_scheduler_slot_t slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
    tdma_traffic_dispatch_t dispatch;
    tdma_traffic_scheduler_snapshot_t snapshot;
    const uint64_t now_ns = 3000000ull;

    (void)tdma_foundation_profile_default(
        &profile, 1u, 0u, 0u, TDMA_ADAPTER_PIO_SPI);
    (void)tdma_traffic_scheduler_init(
        &scheduler, slots, TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT);
    (void)tdma_traffic_scheduler_configure(&scheduler, &profile);

    tdma_traffic_request_t vdc1 =
        make_request(TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE, 8u, now_ns);
    tdma_traffic_request_t vdc2 =
        make_request(TDMA_PAYLOAD_CLASS_IDLE_BEACON, 9u, now_ns);
    vdc1.frame_size = 100u;
    vdc2.frame_size = 100u;
    vdc1.scheduled_window_valid = 1u;
    vdc2.scheduled_window_valid = 1u;
    vdc1.scheduled_guard_start_ns = now_ns;
    vdc2.scheduled_guard_start_ns = now_ns;
    vdc1.scheduled_window_end_ns = now_ns + 2000000ull;
    vdc2.scheduled_window_end_ns = now_ns + 2000000ull;
    (void)tdma_traffic_scheduler_enqueue(&scheduler, &vdc1);
    (void)tdma_traffic_scheduler_enqueue(&scheduler, &vdc2);
    failed += expect_u32("first vdc budget",
                         tdma_traffic_scheduler_select(
                             &scheduler, now_ns, false, &dispatch),
                         TDMA_TRAFFIC_SCHEDULER_OK);
    failed += expect_u32("second vdc over budget",
                         tdma_traffic_scheduler_select(
                             &scheduler, now_ns, false, &dispatch),
                         TDMA_TRAFFIC_SCHEDULER_BUDGET_EXHAUSTED);
    failed += expect_u32("next cycle resets budget",
                         tdma_traffic_scheduler_select(
                             &scheduler,
                             now_ns + profile.resource.cycle_period_ns,
                             false,
                             &dispatch),
                         TDMA_TRAFFIC_SCHEDULER_OK);

    tdma_traffic_request_t expired =
        make_request(TDMA_PAYLOAD_CLASS_CONFIG_CONTROL, 10u, now_ns);
    (void)tdma_traffic_scheduler_enqueue(&scheduler, &expired);
    failed += expect_u32("expired removed",
                         tdma_traffic_scheduler_select(
                             &scheduler, now_ns + 200000000ull, true, &dispatch),
                         TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED);
    (void)tdma_traffic_scheduler_get_snapshot(&scheduler, &snapshot);
    failed += expect_u32("deadline count",
                         snapshot.traffic[TDMA_TRAFFIC_CONFIG_CONTROL]
                             .deadline_miss_count,
                         1u);
    failed += expect_u32("vdc budget count",
                         snapshot.traffic[TDMA_TRAFFIC_VDC_REALTIME]
                             .budget_overrun_count,
                         1u);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_gate_and_priority();
    failed += test_overflow_policies();
    failed += test_budget_and_deadline();
    failed += test_short_long_class_gate();
    if (failed != 0) {
        return 1;
    }
    puts("tdma_traffic_scheduler tests passed");
    return 0;
}
