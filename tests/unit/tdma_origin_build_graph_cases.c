#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tdma_origin_build_job.h"

int main(void)
{
    unsigned cases = 0;
    for (unsigned nodes = 2; nodes <= 8; ++nodes) {
        for (unsigned skip = 0; skip <= 1; ++skip) {
            tdma_origin_plan_config_t c = {
                .address = {.capture_bank = {0x20000000, 0x20000800},
                    .stage = 0x20001000, .tx_header = 0x20002000,
                    .rx_packet = 0x20003000, .state = 0x20004000,
                    .local_shadow = {0x20005000, 0x20005800}, .scratch = 0x20006000,
                    .runs = 0x20008000, .literals = 0x2000a000, .records = 0x2000b000},
                .physical_bytes = 307, .outer_header_bytes = 4, .capture_prefix_bits = 36,
                .guard_count = 1, .abort_poll_count = 8,
                .local_slot = 0, .active_slot_mask = (1u << nodes) - 1u,
                .returned_route_word = 0x502 | ((nodes - 1) * 0x1010000),
                .capture_dma = 4, .output_dma = 5, .loader_dma = 6, .executor_dma = 8,
                .control_pio = 1, .data_pio = 2, .control_sm = 0, .capture_sm = 3,
                .data_sm = 2, .helper_sm = 0, .control_pc = TDMA_ORIGIN_CONTROL_PC,
                .capture_pc = TDMA_ORIGIN_CAPTURE_PC, .data_pc = TDMA_ORIGIN_DATA_PC,
                .helper_pc = TDMA_ORIGIN_HELPER_PC, .compare_pc = TDMA_ORIGIN_COMPARE_PC,
                .rtt_sm = 1, .rtt_pc = TDMA_ORIGIN_RTT_PC,
                .diagnostic_skip_records = skip};
            tdma_flight_overlay_dma_run_t expected_runs[TDMA_ORIGIN_PLAN_RUN_MAX] = {0};
            tdma_flight_overlay_dma_run_t actual_runs[TDMA_ORIGIN_PLAN_RUN_MAX] = {0};
            uint32_t expected_literals[TDMA_ORIGIN_PLAN_LITERAL_MAX] = {0};
            uint32_t actual_literals[TDMA_ORIGIN_PLAN_LITERAL_MAX] = {0};
            tdma_origin_plan_t expected = {.runs = expected_runs, .literals = expected_literals,
                .run_capacity = TDMA_ORIGIN_PLAN_RUN_MAX, .literal_capacity = TDMA_ORIGIN_PLAN_LITERAL_MAX};
            tdma_origin_plan_t actual = {.runs = actual_runs, .literals = actual_literals,
                .run_capacity = TDMA_ORIGIN_PLAN_RUN_MAX, .literal_capacity = TDMA_ORIGIN_PLAN_LITERAL_MAX};
            assert(tdma_origin_plan_build(&c, &expected));
            tdma_origin_plan_builder_t b = {0};
            tdma_origin_build_job_t job = {0};
            assert(tdma_origin_plan_begin(&b, &c, &actual));
            assert(tdma_origin_build_job_request(&job, &b));
            assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_BUSY);
            assert(actual.seed_entry == 0);
            tdma_origin_build_job_core0_service(&job);
            assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_DONE);
            assert(memcmp(expected_runs, actual_runs, sizeof(actual_runs)) == 0);
            assert(memcmp(expected_literals, actual_literals, sizeof(actual_literals)) == 0);
            expected.runs = actual.runs;
            expected.literals = actual.literals;
            assert(memcmp(&expected, &actual, sizeof(actual)) == 0);
            assert(tdma_origin_build_job_cancel(&job));
            ++cases;
        }
    }
    printf("%u actual-builder graph pairs identical\n", cases);
    return 0;
}
