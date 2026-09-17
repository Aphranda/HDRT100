#include "tdma_event_observer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PERIOD UINT64_C(8589934593)

static tdma_event_config_t config(void)
{
    return (tdma_event_config_t){
        .pio_hz = 125000000u,
        .epoch_limit_cycles = UINT64_MAX,
        .join_timeout_cycles = 5000u,
        .min_frame_cycles = 1000u,
        .min_tx_delay_cycles = 0u,
        .max_tx_delay_cycles = 8u
    };
}

static tdma_event_observer_t started(void)
{
    tdma_event_observer_t observer;
    tdma_event_observer_init(&observer);
    const tdma_event_config_t cfg = config();
    assert(tdma_event_observer_start(&observer, &cfg, 1u,
                                    (tdma_event_interval_t){0u, 0u}, 0u));
    return observer;
}

static tdma_event_batch_t batch(uint64_t lo, uint64_t hi)
{
    return (tdma_event_batch_t){.epoch = 1u, .observed = {lo, hi},
                              .empty_mask = TDMA_EVENT_ALL_EMPTY_MASK};
}

static uint32_t wire(uint32_t sequence)
{
    return (sequence >> 24u) | ((sequence >> 8u) & 0xff00u) |
           ((sequence << 8u) & 0xff0000u) | (sequence << 24u);
}

static void append(tdma_event_batch_t *input, uint32_t ordinal,
                   uint32_t sequence, uint32_t raw)
{
    for (size_t stream = 0u; stream < 2u; ++stream) {
        size_t count = input->count[stream];
        assert(count + 2u <= TDMA_EVENT_FIFO_WORDS);
        input->words[stream][count] = raw - (uint32_t)stream;
        input->words[stream][count + 1u] = UINT32_MAX - ordinal;
        input->count[stream] += 2u;
    }
    assert(input->count[2] < TDMA_EVENT_FIFO_WORDS);
    input->words[2][input->count[2]++] = wire(sequence);
}

static void assert_retired(const tdma_event_observer_t *observer)
{
    assert(observer->state != TDMA_EVENT_ACTIVE);
    for (size_t stream = 0; stream < TDMA_EVENT_STREAMS; ++stream) {
        assert(observer->pending_count[stream] == 0u);
        for (size_t word = 0; word < TDMA_EVENT_FIFO_WORDS; ++word) {
            assert(observer->pending[stream][word].word == 0u);
        }
    }
}

static void test_lift(void)
{
    uint64_t result = 77u;
    assert(tdma_event_observer_lift(UINT32_MAX, UINT32_MAX - 50u,
        101u, 101u, 1u, &result) == TDMA_EVENT_OK && result == 101u);
    /* Wrap crossing adds its one extra instruction. */
    assert(tdma_event_observer_lift(0u, UINT32_MAX,
        8u, 8u, 5u, &result) == TDMA_EVENT_OK && result == 8u);
    const uint64_t target = 101u + 7u * PERIOD;
    assert(tdma_event_observer_lift(UINT32_MAX, UINT32_MAX - 50u,
        target - 10u, target + 10u, 1u, &result) == TDMA_EVENT_OK && result == target);
    assert(tdma_event_observer_lift(1u, 1u, 0u, PERIOD + 5u,
        5u, &result) == TDMA_EVENT_LIFT_AMBIGUOUS);
    result = 77u;
    assert(tdma_event_observer_lift(1u, 1u, 6u, PERIOD + 4u,
        5u, &result) == TDMA_EVENT_LIFT_NO_CANDIDATE && result == 77u);
    assert(tdma_event_observer_lift(1u, 1u, 2u, 1u,
        5u, &result) == TDMA_EVENT_BAD_ARGUMENT);
    assert(tdma_event_observer_lift(1u, 1u, 0u, 1u,
        4u, &result) == TDMA_EVENT_BAD_ARGUMENT);
    assert(tdma_event_observer_lift(1u, 1u, 0u, 1u,
        1u, NULL) == TDMA_EVENT_BAD_ARGUMENT);
    const uint64_t largest = 5u + ((UINT64_MAX - 5u) / PERIOD) * PERIOD;
    assert(tdma_event_observer_lift(1u, 1u, largest, UINT64_MAX,
        5u, &result) == TDMA_EVENT_OK && result == largest);
    assert(tdma_event_observer_lift(1u, 1u, largest + 1u, UINT64_MAX,
        5u, &result) == TDMA_EVENT_LIFT_NO_CANDIDATE);
}

static void test_four_records_and_baseline(void)
{
    tdma_event_observer_t observer = started();
    tdma_event_batch_t input = batch(3900u, 4000u);
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    for (uint32_t i = 0u; i < 4u; ++i) {
        append(&input, i, 0x01020304u + i, UINT32_MAX - 50u - 500u * i);
    }
    assert(tdma_event_observer_feed(&observer, &input, output) == 4u);
    assert(observer.reads_last == 20u && observer.joined == 4u);
    assert(observer.first_sequence == 0x01020304u);
    for (uint32_t i = 0u; i < 4u; ++i) {
        assert(output[i].epoch == 1u && output[i].ordinal == i);
        assert(output[i].sequence == 0x01020304u + i);
        assert(output[i].rx_elapsed_cycles == 101u + i * 1005u);
        assert(output[i].tx_elapsed_cycles == 103u + i * 1005u);
        assert(output[i].start_bounds.lo == 0u && output[i].start_bounds.hi == 0u);
        assert(output[i].diagnostic_only && output[i].physical_first_unproved &&
               output[i].identity_unproved);
        assert(!output[i].timestamp_valid && !output[i].dpll_eligible);
    }
}

static void test_partial_and_timeout(void)
{
    tdma_event_observer_t observer = started();
    tdma_event_batch_t input = batch(100u, 101u);
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    input.words[0][0] = UINT32_MAX - 50u;
    input.count[0] = 1u;
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.state == TDMA_EVENT_ACTIVE && observer.pending_count[0] == 1u);
    input = batch(190u, 200u);
    input.words[0][0] = UINT32_MAX;
    input.count[0] = 1u;
    input.words[1][0] = UINT32_MAX - 51u;
    input.words[1][1] = UINT32_MAX;
    input.count[1] = 2u;
    input.words[2][0] = wire(40u);
    input.count[2] = 1u;
    assert(tdma_event_observer_feed(&observer, &input, output) == 1u);
    assert(output[0].rx_elapsed_cycles == 101u);
    observer = started();
    input = batch(100u, 101u);
    input.count[0] = 1u;
    input.words[0][0] = UINT32_MAX - 50u;
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    input = batch(5101u, 5101u); /* Equality remains valid. */
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.state == TDMA_EVENT_ACTIVE);
    input = batch(5102u, 5102u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_JOIN_TIMEOUT);
    assert_retired(&observer);
}

static void test_missing_pairs(void)
{
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    for (unsigned mode = 0u; mode < 3u; ++mode) {
        tdma_event_observer_t observer = started();
        tdma_event_batch_t input = batch(190u, 200u);
        append(&input, 0u, 1u, UINT32_MAX - 50u);
        assert(tdma_event_observer_feed(&observer, &input, output) == 1u);
        input = batch(2190u, 2200u);
        append(&input, 1u, 2u, UINT32_MAX - 550u);
        append(&input, 2u, 3u, UINT32_MAX - 1050u);
        if (mode == 2u) { /* One missing raw word must not create a valid pair. */
            memmove(input.words[0], input.words[0] + 1u, 3u * sizeof(uint32_t));
            --input.count[0];
        } else {
            for (unsigned stream = 0; stream <= mode; ++stream) {
                memmove(input.words[stream], input.words[stream] + 2u, 2u * sizeof(uint32_t));
                input.count[stream] -= 2u;
            }
        }
        assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
        assert(observer.reason == TDMA_EVENT_ORDINAL);
        assert_retired(&observer);
        input = batch(3190u, 3200u);
        append(&input, 1u, 2u, UINT32_MAX - 550u);
        assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
        assert(observer.reason == TDMA_EVENT_ORDINAL);
    }
}

static void test_sequence_rejection(void)
{
    const uint32_t first[] = {1u, 1u, UINT32_MAX};
    const uint32_t second[] = {1u, 3u, 0u};
    for (size_t test = 0u; test < 3u; ++test) {
        tdma_event_observer_t observer = started();
        tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
        tdma_event_batch_t input = batch(1190u, 1200u);
        append(&input, 0u, first[test], UINT32_MAX - 50u);
        append(&input, 1u, second[test], UINT32_MAX - 550u);
        /* A good earlier pair in this SAME batch cannot publish on failure. */
        assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
        assert(observer.reason == TDMA_EVENT_SEQUENCE);
        assert_retired(&observer);
    }
}

static void test_fault_snapshots(void)
{
    const uint32_t faults[] = {TDMA_EVENT_FAULT_STALL, TDMA_EVENT_FAULT_OVERFLOW,
        TDMA_EVENT_FAULT_SEQUENCE, TDMA_EVENT_FAULT_DISABLED, TDMA_EVENT_FAULT_CONFIG};
    for (size_t i = 0u; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        for (unsigned post = 0; post < 2u; ++post) {
            tdma_event_observer_t observer = started();
            tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
            tdma_event_batch_t input = batch(190u, 200u);
            append(&input, 0u, 1u, UINT32_MAX - 50u);
            if (post) input.post_faults = faults[i];
            else input.pre_faults = faults[i];
            assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
            assert(observer.reason == (post ? TDMA_EVENT_POST_FAULT : TDMA_EVENT_PRE_FAULT));
            assert(observer.fault_bits == faults[i]);
            assert_retired(&observer);
        }
    }
}

static void test_stop_rearm_epochs(void)
{
    tdma_event_observer_t observer = started();
    const tdma_event_config_t cfg = config();
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    tdma_event_batch_t old = batch(100u, 101u);
    old.words[0][0] = UINT32_MAX - 50u;
    old.count[0] = 1u;
    assert(tdma_event_observer_feed(&observer, &old, output) == 0u);
    assert(!tdma_event_observer_start(&observer, &cfg, 2u,
                                     (tdma_event_interval_t){0u, 0u}, 0u));
    tdma_event_observer_stop(&observer);
    assert_retired(&observer);
    assert(tdma_event_observer_feed(&observer, &old, output) == 0u);
    assert(observer.state == TDMA_EVENT_STOPPED);
    assert(!tdma_event_observer_start(&observer, &cfg, 1u,
                                     (tdma_event_interval_t){0u, 0u}, 0u));
    assert(tdma_event_observer_start(&observer, &cfg, 2u,
                                    (tdma_event_interval_t){1000u, 1000u}, 0u));
    assert(tdma_event_observer_feed(&observer, &old, output) == 0u);
    assert(observer.reason == TDMA_EVENT_BAD_EPOCH);
    tdma_event_observer_stop(&observer);
    assert(!tdma_event_observer_start(&observer, &cfg, 3u,
        (tdma_event_interval_t){2000u, 2000u}, TDMA_EVENT_FAULT_DIRTY_START));
    assert(observer.state == TDMA_EVENT_INVALID && observer.epoch == 3u);
    tdma_event_observer_stop(&observer);
    assert(!tdma_event_observer_start(&observer, &cfg, 3u,
                                     (tdma_event_interval_t){2000u, 2000u}, 0u));
    assert(tdma_event_observer_start(&observer, &cfg, UINT32_MAX,
                                    (tdma_event_interval_t){2000u, 2000u}, 0u));
    tdma_event_observer_stop(&observer);
    assert(!tdma_event_observer_start(&observer, &cfg, 0u,
                                     (tdma_event_interval_t){0u, 0u}, 0u));
}

static void test_bounds_and_staging(void)
{
    tdma_event_observer_t observer = started();
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    tdma_event_batch_t input = batch(190u, 200u);
    input.count[0] = 9u;
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_STAGING_OVERFLOW);
    observer = started();
    input = batch(100u, 101u);
    input.count[0] = 1u;
    input.words[0][0] = UINT32_MAX - 50u;
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    input = batch(190u, 200u);
    input.count[0] = 8u;
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_STAGING_OVERFLOW);
    observer = started();
    observer.config.epoch_limit_cycles = 200u;
    input = batch(200u, 201u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_SERVICE_AGE);
    observer = started();
    input = batch(200u, 201u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    input = batch(199u, 202u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_SERVICE_AGE);
}

static void test_full_read_budget_and_start_validation(void)
{
    tdma_event_observer_t observer = started();
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    tdma_event_batch_t input = batch(3900u, 4000u);
    for (uint32_t i = 0u; i < 4u; ++i) {
        append(&input, i, i + 1u, UINT32_MAX - 50u - i * 500u);
    }
    for (uint32_t i = 4u; i < 8u; ++i) {
        input.words[2][input.count[2]++] = wire(i + 1u);
    }
    assert(tdma_event_observer_feed(&observer, &input, output) == TDMA_EVENT_MAX_RECORDS);
    assert(observer.reads_last == TDMA_EVENT_STREAMS * TDMA_EVENT_FIFO_WORDS);
    assert(observer.pending_count[2] == 4u);
    assert(observer.pending_count[0] == 0u && observer.pending_count[1] == 0u);
    tdma_event_observer_stop(&observer);
    /* Reusing the saved config in the observer must survive the reset. */
    assert(tdma_event_observer_start(&observer, &observer.config, 2u,
                                    (tdma_event_interval_t){10u, 10u}, 0u));
    assert(observer.config.pio_hz == 125000000u);
    tdma_event_observer_stop(&observer);
    tdma_event_config_t bad = config();
    bad.pio_hz = 0u;
    assert(!tdma_event_observer_start(&observer, &bad, 3u,
                                     (tdma_event_interval_t){10u, 10u}, 0u));
    bad = config();
    bad.max_tx_delay_cycles = bad.min_frame_cycles;
    assert(!tdma_event_observer_start(&observer, &bad, 3u,
                                     (tdma_event_interval_t){10u, 10u}, 0u));
    bad = config();
    bad.min_tx_delay_cycles = bad.max_tx_delay_cycles + 1u;
    assert(!tdma_event_observer_start(&observer, &bad, 3u,
                                     (tdma_event_interval_t){10u, 10u}, 0u));
    assert(observer.state == TDMA_EVENT_STOPPED && observer.epoch == 2u);
}

static void test_absolute_anchor_contradiction(void)
{
    tdma_event_observer_t observer = started();
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    tdma_event_batch_t input = batch(190u, 200u);
    append(&input, 0u, 1u, UINT32_MAX - 50u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 1u);
    /* A raw counter delta would put RX at 1106, while a proved-empty RX FIFO
     * at 1150 excludes it. The known previous event now rules out the lift
     * before the later absolute-anchor validation. */
    input = batch(1150u, 1150u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    input = batch(1190u, 1200u);
    append(&input, 1u, 2u, UINT32_MAX - 550u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_LIFT_NO_CANDIDATE);
    assert_retired(&observer);
}

static void test_shared_anchor_between_streams(void)
{
    tdma_event_observer_t observer;
    tdma_event_observer_init(&observer);
    const tdma_event_config_t cfg = config();
    assert(tdma_event_observer_start(&observer, &cfg, 1u,
                                    (tdma_event_interval_t){1000u, 1100u}, 0u));
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    tdma_event_batch_t input = batch(1101u, 1101u);
    input.count[0] = 2u;
    input.words[0][0] = UINT32_MAX - 50u;
    input.words[0][1] = UINT32_MAX;
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    /* RX requires anchor<=1000; a TX FIFO proved empty at 1190 requires
     * anchor>=1087. Per-lane first lifts exist but no COMMON start exists. */
    input = batch(1190u, 1190u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    input = batch(1200u, 1200u);
    input.words[1][0] = UINT32_MAX - 51u;
    input.words[1][1] = UINT32_MAX;
    input.count[1] = 2u;
    input.words[2][0] = wire(1u);
    input.count[2] = 1u;
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_ABSOLUTE_TIME);
}

static void test_large_absolute_clock_and_multiwrap(void)
{
    tdma_event_observer_t observer;
    tdma_event_observer_init(&observer);
    const tdma_event_config_t cfg = config();
    const uint64_t start = UINT64_MAX - 5000u;
    assert(tdma_event_observer_start(&observer, &cfg, 1u,
                                    (tdma_event_interval_t){start, start}, 0u));
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    tdma_event_batch_t input = batch(start + 190u, start + 200u);
    append(&input, 0u, 1u, UINT32_MAX - 50u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 1u);
    assert(output[0].start_bounds.lo == start && output[0].rx_elapsed_cycles == 101u);

    observer = started();
    const uint64_t elapsed = 101u + 12u * PERIOD;
    input = batch(elapsed - 10u, elapsed - 10u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    input = batch(elapsed + 10u, elapsed + 10u);
    append(&input, 0u, 42u, UINT32_MAX - 50u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 1u);
    assert(output[0].rx_elapsed_cycles == elapsed);
    assert(output[0].tx_elapsed_cycles == elapsed + 2u);
}

static void test_spacing_skew_and_overflow(void)
{
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    tdma_event_observer_t observer = started();
    tdma_event_batch_t input = batch(190u, 200u);
    append(&input, 0u, 1u, UINT32_MAX - 50u);
    input.words[1][0] -= 10u;
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_PAIR_SKEW);
    observer = started();
    input = batch(190u, 200u);
    append(&input, 0u, 1u, UINT32_MAX - 50u);
    append(&input, 1u, 2u, UINT32_MAX - 51u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_FRAME_SPACING);

    observer = started();
    /* Establish a near-U64 lifetime through actual public inputs, then ask
     * for a locally possible next delta whose accumulation would overflow. */
    const uint64_t near_end = 101u + ((UINT64_MAX - 101u) / PERIOD) * PERIOD;
    input = batch(near_end - 2000u, near_end - 2000u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    input = batch(UINT64_MAX, UINT64_MAX);
    append(&input, 0u, 1u, UINT32_MAX - 50u);
    assert(tdma_event_observer_feed(&observer, &input, output) == 1u);
    /* The broad previous read bracket alone admits this delta, but its
     * intersection with the already proved elapsed time cannot: rejection
     * now occurs before the overflowing elapsed-time addition. */
    const uint64_t room = UINT64_MAX - near_end;
    const uint32_t decrement = (uint32_t)((room + 1u) / 2u);
    input = batch(UINT64_MAX, UINT64_MAX);
    append(&input, 1u, 2u, UINT32_MAX - 50u - decrement);
    assert(tdma_event_observer_feed(&observer, &input, output) == 0u);
    assert(observer.reason == TDMA_EVENT_LIFT_NO_CANDIDATE);
    assert_retired(&observer);
}

int main(void)
{
    test_lift();
    test_four_records_and_baseline();
    test_partial_and_timeout();
    test_missing_pairs();
    test_sequence_rejection();
    test_fault_snapshots();
    test_stop_rearm_epochs();
    test_bounds_and_staging();
    test_full_read_budget_and_start_validation();
    test_absolute_anchor_contradiction();
    test_shared_anchor_between_streams();
    test_large_absolute_clock_and_multiwrap();
    test_spacing_skew_and_overflow();
    puts("tdma_event_observer: 13 production C case groups passed");
    return 0;
}
