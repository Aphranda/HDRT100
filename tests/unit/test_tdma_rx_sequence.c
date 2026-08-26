#include "tdma_rx_sequence.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) return 0;
    (void)fprintf(stderr, "FAIL %s: got %u expected %u\n",
                  name, actual ? 1u : 0u, expected ? 1u : 0u);
    return 1;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
    if (actual == expected) return 0;
    (void)fprintf(stderr, "FAIL %s: got %llu expected %llu\n",
                  name,
                  (unsigned long long)actual,
                  (unsigned long long)expected);
    return 1;
}

int main(void)
{
    int failed = 0;
    tdma_rx_sequence_tracker_t tracker = {0};
    uint64_t produced = 0u;

    failed += expect_bool(
        "reject invalid reset",
        tdma_rx_sequence_reset(&tracker, 0u, 0u, 0u), false);
    failed += expect_bool(
        "reset",
        tdma_rx_sequence_reset(&tracker, 1024u, 0u, 0u), true);
    failed += expect_bool(
        "first observation retains initial words",
        tdma_rx_sequence_observe(&tracker, 12u, 0u, 297u, &produced), true);
    failed += expect_u64("first produced", produced, 12u);
    failed += expect_bool(
        "no new data",
        tdma_rx_sequence_observe(&tracker, 12u, 0u, 297u, &produced), true);
    failed += expect_u64("no new produced", produced, 12u);

    failed += expect_bool(
        "reset early boundary scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 0u, 0u), true);
    failed += expect_bool(
        "receive before boundary",
        tdma_rx_sequence_observe(&tracker, 290u, 0u, 297u, &produced), true);
    failed += expect_u64("before boundary produced", produced, 290u);
    failed += expect_bool(
        "boundary leads returned tail",
        tdma_rx_sequence_observe(&tracker, 290u, 1u, 297u, &produced), true);
    failed += expect_u64("early boundary does not add ring", produced, 290u);
    failed += expect_bool(
        "returned tail arrives",
        tdma_rx_sequence_observe(&tracker, 297u, 1u, 297u, &produced), true);
    failed += expect_u64("tail advances exactly", produced, 297u);

    failed += expect_bool(
        "reset modulo wrap scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 900u, 3u), true);
    failed += expect_bool(
        "single modulo wrap below ring capacity",
        tdma_rx_sequence_observe(&tracker, 100u, 4u, 297u, &produced), true);
    failed += expect_u64("modulo wrap delta", produced, 224u);

    failed += expect_bool(
        "reset missed ring scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 0u, 10u), true);
    failed += expect_bool(
        "frame evidence restores missed ring",
        tdma_rx_sequence_observe(&tracker, 164u, 14u, 297u, &produced), true);
    failed += expect_u64("missed ring restored", produced, 1188u);
    failed += expect_bool(
        "next frame remains continuous",
        tdma_rx_sequence_observe(&tracker, 461u, 15u, 297u, &produced), true);
    failed += expect_u64("post-wrap continuity", produced, 1485u);

    failed += expect_bool(
        "reset inconsistent evidence scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 0u, 0u), true);
    failed += expect_bool(
        "two early boundaries observed",
        tdma_rx_sequence_observe(&tracker, 0u, 2u, 297u, &produced), true);
    failed += expect_u64("inconsistent counter cannot invent ring", produced, 0u);

    failed += expect_bool(
        "reset modulo-only scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 1000u, 0u), true);
    failed += expect_bool(
        "variable frame modulo accumulation",
        tdma_rx_sequence_observe(&tracker, 20u, 999u, 0u, &produced), true);
    failed += expect_u64("variable frame delta", produced, 44u);

    if (failed != 0) {
        (void)fprintf(stderr, "tdma_rx_sequence tests failed: %d\n", failed);
        return 1;
    }
    (void)puts("tdma_rx_sequence tests passed");
    return 0;
}
