#include "calibration_training_sck.h"

#include <stdio.h>

static int failed;

static void expect_true(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("FAIL %s: actual=%d expected=%d\n",
                     name, actual ? 1 : 0, expected ? 1 : 0);
        failed++;
    }
}

static void expect_i32(const char *name, int32_t actual, int32_t expected)
{
    if (actual != expected) {
        (void)printf("FAIL %s: actual=%ld expected=%ld\n",
                     name, (long)actual, (long)expected);
        failed++;
    }
}

static void expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("FAIL %s: actual=%lu expected=%lu\n",
                     name, (unsigned long)actual, (unsigned long)expected);
        failed++;
    }
}

static calibration_training_sck_request_t make_request(void)
{
    const calibration_training_sck_request_t request = {
        .board_unique_id = 1u,
        .build_id = 2u,
        .source_node = 0u,
        .destination_node = 1u,
        .train_epoch = 7u,
        .train_sequence = 8u,
        .sck_codebook_id = 1u,
        .sck_crc32 = 9u,
        .calibration_generation = 10u,
        .topology_generation = 11u,
        .topology_crc32 = 12u,
        .profile_crc32 = 13u,
        .schedule_crc32 = 14u,
        .sample_period_ns = 4u,
        .sck_launch_guard_sample_count = 32u,
        .link_base_delay_ns = 40u,
        .configured_sck_offset_sample_count = -1,
        .search_start_offset_sample = -3,
        .search_end_offset_sample = 3,
        .guard_sample_count = 1u,
        .expected_polarity = 0u,
        .max_best_distance = 5u,
        .min_margin = 2u,
    };
    return request;
}

static calibration_training_sck_evidence_t make_evidence(void)
{
    const calibration_training_sck_evidence_t evidence = {
        .train_epoch = 7u,
        .train_sequence = 8u,
        .observed_crc32 = 9u,
        .calibration_generation = 10u,
        .topology_generation = 11u,
        .topology_crc32 = 12u,
        .profile_crc32 = 13u,
        .schedule_crc32 = 14u,
        .flags = CALIBRATION_TRAINING_SCK_REQUIRED_FLAGS,
        .polarity = 0u,
        .best_lag_sample = 23u,
        .best_distance = 1u,
        .second_lag_sample = 24u,
        .second_distance = 4u,
        .margin = 3u,
        .captured_sample_count = 100u,
        .expected_sample_count = 94u,
        .sck_capture_origin_tick = 1u,
        .sck_code_capture_tick = 9u,
    };
    return evidence;
}

int main(void)
{
    calibration_training_sck_store_t store;
    calibration_training_sck_snapshot_t snapshot;
    calibration_training_sck_request_t request = make_request();
    calibration_training_sck_evidence_t evidence = make_evidence();
    uint32_t source_phase = 0u;
    uint32_t destination_phase = 0u;

    expect_true("map negative offset",
                calibration_training_sck_map_offset_to_phase_cycles(
                    40u, 4u, -10, &source_phase, &destination_phase), true);
    expect_u32("negative source phase", source_phase, 1u);
    expect_u32("negative destination phase", destination_phase, 1u);
    expect_true("map zero offset",
                calibration_training_sck_map_offset_to_phase_cycles(
                    40u, 4u, 0, &source_phase, &destination_phase), true);
    expect_u32("zero source phase", source_phase, 1u);
    expect_u32("zero destination phase", destination_phase, 11u);
    expect_true("map positive offset",
                calibration_training_sck_map_offset_to_phase_cycles(
                    40u, 4u, 10, &source_phase, &destination_phase), true);
    expect_u32("positive source phase", source_phase, 1u);
    expect_u32("positive destination phase", destination_phase, 21u);

    calibration_training_sck_store_init(&store);
    expect_true("prepare",
                calibration_training_sck_prepare_core1(&store, &request),
                true);
    expect_true("accept",
                calibration_training_sck_evaluate_core1(
                    &store, &request, &evidence),
                true);
    expect_true("snapshot",
                calibration_training_sck_get_snapshot(&store, &snapshot),
                true);
    expect_i32("resolved offset", snapshot.resolved_offset_sample_count, 1);
    expect_i32("configured window start", snapshot.training_window_start_ns,
               36);
    expect_i32("configured window end", snapshot.training_window_end_ns, 44);

    evidence.sck_capture_origin_tick = 0u;
    expect_true("missing SCK origin rejected",
                calibration_training_sck_evaluate_core1(
                    &store, &request, &evidence),
                false);

    request.sck_launch_guard_sample_count = 0u;
    expect_true("zero launch guard rejected",
                calibration_training_sck_prepare_core1(&store, &request),
                false);

    if (failed != 0) {
        (void)printf("calibration_training_sck tests failed: %d\n", failed);
        return 1;
    }
    puts("calibration_training_sck tests passed");
    return 0;
}
