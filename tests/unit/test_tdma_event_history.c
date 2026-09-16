#include "tdma_event_history.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static tdma_event_history_t started(void)
{
    tdma_event_history_t history;
    tdma_event_history_init(&history);
    assert(tdma_event_history_start(&history, 1u, 125000000u,
                                   (tdma_event_interval_t){1000u, 1100u}));
    return history;
}

static tdma_event_record_t record(uint32_t ordinal, uint32_t sequence)
{
    return (tdma_event_record_t){
        .epoch = 1u, .ordinal = ordinal, .sequence = sequence,
        .raw_rx = UINT32_MAX - 50u - ordinal * 500u,
        .raw_tx = UINT32_MAX - 51u - ordinal * 500u,
        .rx_elapsed_cycles = 101u + (uint64_t)ordinal * 1005u,
        .tx_elapsed_cycles = 103u + (uint64_t)ordinal * 1005u,
        .start_bounds = {1020u, 1060u},
        .diagnostic_only = true, .physical_first_unproved = true, .identity_unproved = true
    };
}

static void assert_zero(const void *object, size_t size)
{
    const unsigned char *bytes = object;
    for (size_t i = 0u; i < size; ++i) assert(bytes[i] == 0u);
}

static void assert_unavailable(tdma_event_history_t *history, uint32_t epoch, uint32_t sequence)
{
    tdma_event_record_t out;
    memset(&out, 0xa5, sizeof(out));
    assert(!tdma_event_history_lookup(history, epoch, sequence, &out));
    assert_zero(&out, sizeof(out));
}

static void assert_retired(tdma_event_history_t *history, tdma_event_history_reason_t reason)
{
    assert(!history->active && history->count == 0u && history->next == 0u);
    assert(history->reason == reason && history->oldest_ordinal == UINT32_MAX);
    assert_zero(history->records, sizeof(history->records));
    assert_zero(&history->initial_start, sizeof(history->initial_start));
    assert(history->pio_hz == 0u);
    assert_unavailable(history, history->epoch, 1u);
    tdma_event_history_retire(history);
    assert(history->reason == reason);
}

static void test_compact_and_conservative_context(void)
{
    tdma_event_history_t history = started();
    tdma_event_record_t input[4], out;
    for (uint32_t i = 0u; i < 4u; ++i) {
        input[i] = record(i, 42u + i);
        input[i].start_bounds = (tdma_event_interval_t){1020u + i, 1060u - i};
    }
    assert(sizeof(tdma_event_history_record_t) == 32u);
    assert(tdma_event_history_append(&history, input, 4u, 4u));
    assert(history.count == 4u && history.accepted == 4u && history.evicted == 0u);
    assert(history.oldest_ordinal == 0u && history.pio_hz == 125000000u);
    for (uint32_t i = 0u; i < 4u; ++i) {
        assert(tdma_event_history_lookup(&history, 1u, 42u + i, &out));
        assert(out.epoch == 1u && out.ordinal == i && out.sequence == 42u + i);
        assert(out.raw_rx == input[i].raw_rx && out.raw_tx == input[i].raw_tx);
        assert(out.rx_elapsed_cycles == input[i].rx_elapsed_cycles);
        assert(out.tx_elapsed_cycles == input[i].tx_elapsed_cycles);
        assert(out.start_bounds.lo == 1000u && out.start_bounds.hi == 1100u);
        assert(out.diagnostic_only && out.physical_first_unproved && out.identity_unproved);
        assert(!out.timestamp_valid && !out.dpll_eligible);
    }
    assert_unavailable(&history, 2u, 42u);
    assert(history.active && history.query_reason == TDMA_EVENT_HISTORY_BAD_EPOCH);
    assert_unavailable(&history, 1u, 900u);
    assert(history.active && history.query_reason == TDMA_EVENT_HISTORY_NOT_FOUND);
}

static void test_sequence_wrap(void)
{
    tdma_event_history_t history = started();
    tdma_event_record_t input[4], out;
    const uint32_t sequences[] = {UINT32_MAX - 1u, UINT32_MAX, 0u, 1u};
    for (uint32_t i = 0u; i < 4u; ++i) input[i] = record(i, sequences[i]);
    assert(tdma_event_history_append(&history, input, 4u, 4u));
    for (uint32_t i = 0u; i < 4u; ++i) {
        assert(tdma_event_history_lookup(&history, 1u, sequences[i], &out));
        assert(out.ordinal == i && out.sequence == sequences[i]);
    }
}

static void test_eviction_many_batches(void)
{
    tdma_event_history_t history = started();
    uint32_t accepted = 0u;
    const uint32_t first = UINT32_MAX - 5u;
    for (uint32_t batch = 0u; batch < 400u; ++batch) {
        const uint32_t count = batch % 4u + 1u;
        tdma_event_record_t input[4], out;
        for (uint32_t i = 0u; i < count; ++i)
            input[i] = record(accepted + i, first + accepted + i);
        accepted += count;
        assert(tdma_event_history_append(&history, input, count, accepted));
        const uint32_t retained = accepted < TDMA_EVENT_HISTORY_CAPACITY ? accepted : TDMA_EVENT_HISTORY_CAPACITY;
        assert(history.count == retained && history.accepted == accepted);
        assert(history.evicted == accepted - retained && history.oldest_ordinal == accepted - retained);
        for (uint32_t ordinal = accepted - retained; ordinal < accepted; ++ordinal) {
            assert(tdma_event_history_lookup(&history, 1u, first + ordinal, &out));
            assert(out.ordinal == ordinal && out.rx_elapsed_cycles == 101u + (uint64_t)ordinal * 1005u);
        }
        if (accepted > retained) {
            assert_unavailable(&history, 1u, first + accepted - retained - 1u);
            assert(history.active && history.query_reason == TDMA_EVENT_HISTORY_NOT_FOUND);
        }
    }
    assert(accepted == 1000u && history.evicted == 984u && history.oldest_ordinal == 984u);
}

static void test_bad_flags_entire_batch_retires(void)
{
    for (unsigned flag = 0u; flag < 5u; ++flag) {
        tdma_event_history_t history = started();
        tdma_event_record_t prefix = record(0u, 1u);
        assert(tdma_event_history_append(&history, &prefix, 1u, 1u));
        tdma_event_record_t input[4];
        for (uint32_t i = 0u; i < 4u; ++i) input[i] = record(i + 1u, i + 2u);
        if (flag == 0u) input[3].diagnostic_only = false;
        if (flag == 1u) input[3].physical_first_unproved = false;
        if (flag == 2u) input[3].identity_unproved = false;
        if (flag == 3u) input[3].timestamp_valid = true;
        if (flag == 4u) input[3].dpll_eligible = true;
        assert(!tdma_event_history_append(&history, input, 4u, 5u));
        /* Validation must not commit even the good prefix of this batch. */
        assert(history.accepted == 1u && history.evicted == 0u);
        assert_retired(&history, TDMA_EVENT_HISTORY_BAD_FLAGS);
        assert(!tdma_event_history_append(&history, &prefix, 1u, 2u));
        assert(history.reason == TDMA_EVENT_HISTORY_BAD_FLAGS);
    }
}

static void test_incomplete_batches(void)
{
    tdma_event_record_t input[4];
    for (uint32_t i = 0u; i < 4u; ++i) input[i] = record(i, i + 1u);
    for (size_t supplied = 0u; supplied < 4u; ++supplied) {
        tdma_event_history_t history = started();
        assert(!tdma_event_history_append(&history, input, supplied, 4u));
        assert(history.accepted == 0u);
        assert_retired(&history, TDMA_EVENT_HISTORY_INCOMPLETE_BATCH);
    }
    tdma_event_history_t history = started();
    assert(tdma_event_history_append(&history, NULL, 0u, 0u));
    assert(history.active && history.count == 0u);
    assert(tdma_event_history_append(&history, input, 4u, 4u));
    assert(tdma_event_history_append(&history, NULL, 0u, 4u));
    /* Lost a whole source batch: the independent source watermark exposes it. */
    for (uint32_t i = 0u; i < 4u; ++i) input[i] = record(i + 8u, i + 9u);
    assert(!tdma_event_history_append(&history, input, 4u, 12u));
    assert(history.accepted == 4u);
    assert_retired(&history, TDMA_EVENT_HISTORY_INCOMPLETE_BATCH);
}

static void test_bad_epoch_ordinal_and_sequence(void)
{
    const tdma_event_history_reason_t expected[] = {
        TDMA_EVENT_HISTORY_BAD_EPOCH, TDMA_EVENT_HISTORY_BAD_ORDINAL,
        TDMA_EVENT_HISTORY_BAD_SEQUENCE, TDMA_EVENT_HISTORY_BAD_SEQUENCE
    };
    for (uint32_t bad = 0u; bad < 4u; ++bad) {
        tdma_event_history_t history = started();
        tdma_event_record_t input[2] = {record(0u, 1u), record(1u, 2u)};
        if (bad == 0u) input[1].epoch = 2u;
        if (bad == 1u) input[1].ordinal = 2u;
        if (bad == 2u) input[1].sequence = 1u;
        if (bad == 3u) input[1].sequence = 3u;
        assert(!tdma_event_history_append(&history, input, 2u, 2u));
        assert(history.accepted == 0u);
        assert_retired(&history, expected[bad]);
    }
}

static void test_bad_time_and_start_bounds(void)
{
    for (unsigned bad = 0u; bad < 8u; ++bad) {
        tdma_event_history_t history = started();
        tdma_event_record_t input[2] = {record(0u, 1u), record(1u, 2u)};
        if (bad == 0u) input[1].rx_elapsed_cycles = input[0].rx_elapsed_cycles;
        if (bad == 1u) input[1].tx_elapsed_cycles = input[0].tx_elapsed_cycles;
        if (bad == 2u) input[1].tx_elapsed_cycles = input[1].rx_elapsed_cycles - 1u;
        if (bad == 3u) input[1].tx_elapsed_cycles = UINT64_MAX;
        if (bad == 4u) input[0].rx_elapsed_cycles = 0u;
        if (bad == 5u) input[1].start_bounds.lo = 999u;
        if (bad == 6u) input[1].start_bounds.hi = 1101u;
        if (bad == 7u) input[1].start_bounds = (tdma_event_interval_t){1080u, 1070u};
        assert(!tdma_event_history_append(&history, input, 2u, 2u));
        assert(history.accepted == 0u);
        assert_retired(&history, bad < 5u ? TDMA_EVENT_HISTORY_BAD_TIME : TDMA_EVENT_HISTORY_BAD_START_BOUNDS);
    }
    tdma_event_history_t history = started();
    tdma_event_record_t input = record(0u, 1u), out;
    input.rx_elapsed_cycles = UINT64_MAX - 1102u;
    input.tx_elapsed_cycles = UINT64_MAX - 1100u;
    assert(tdma_event_history_append(&history, &input, 1u, 1u));
    assert(tdma_event_history_lookup(&history, 1u, 1u, &out));
    assert(out.start_bounds.hi + out.tx_elapsed_cycles == UINT64_MAX);
}

static void test_retire_rearm_and_old_epoch(void)
{
    tdma_event_history_t history = started();
    tdma_event_record_t input = record(0u, 7u), out;
    assert(tdma_event_history_append(&history, &input, 1u, 1u));
    tdma_event_history_retire(&history);
    assert(history.epoch == 1u && history.accepted == 1u);
    assert_retired(&history, TDMA_EVENT_HISTORY_RETIRED);
    assert(!tdma_event_history_start(&history, 1u, 125000000u, (tdma_event_interval_t){0u, 0u}));
    assert(history.epoch == 1u);
    assert(tdma_event_history_start(&history, 2u, 150000000u, (tdma_event_interval_t){1000u, 1100u}));
    assert(history.accepted == 0u && history.evicted == 0u);
    assert_unavailable(&history, 1u, 7u);
    assert(history.active);
    input.epoch = 2u;
    assert(tdma_event_history_append(&history, &input, 1u, 1u));
    assert(tdma_event_history_lookup(&history, 2u, 7u, &out) && out.epoch == 2u);
    tdma_event_record_t stale = record(1u, 8u);
    assert(!tdma_event_history_append(&history, &stale, 1u, 2u));
    assert_retired(&history, TDMA_EVENT_HISTORY_BAD_EPOCH);
    assert(tdma_event_history_start(&history, UINT32_MAX, 1u, (tdma_event_interval_t){0u, 0u}));
    tdma_event_history_retire(&history);
    assert(!tdma_event_history_start(&history, 0u, 1u, (tdma_event_interval_t){0u, 0u}));
    assert(history.epoch == UINT32_MAX);
}

static void test_corrupt_duplicate_is_ambiguous(void)
{
    tdma_event_history_t history = started();
    tdma_event_record_t input[2] = {record(0u, 1u), record(1u, 2u)};
    assert(tdma_event_history_append(&history, input, 2u, 2u));
    /* Unreachable through valid sequential append, but query must never choose
     * arbitrarily if corrupted/duplicated stored candidates share a sequence. */
    history.records[1].sequence = history.records[0].sequence;
    assert_unavailable(&history, 1u, 1u);
    assert(history.query_reason == TDMA_EVENT_HISTORY_AMBIGUOUS);
    assert_retired(&history, TDMA_EVENT_HISTORY_AMBIGUOUS);
}

static void test_ordinal_limit_without_four_billion_iterations(void)
{
    tdma_event_history_t history = started();
    /* Boundary fixture describes a valid already-retained terminal window;
     * the transition at UINT32_MAX itself runs the production append. */
    history.accepted = UINT32_MAX;
    history.count = TDMA_EVENT_HISTORY_CAPACITY;
    history.next = 0u;
    history.evicted = history.oldest_ordinal = UINT32_MAX - TDMA_EVENT_HISTORY_CAPACITY;
    for (uint32_t i = 0u; i < TDMA_EVENT_HISTORY_CAPACITY; ++i) {
        const tdma_event_record_t source = record(history.oldest_ordinal + i, i);
        history.records[i] = (tdma_event_history_record_t){
            .rx_elapsed_cycles = source.rx_elapsed_cycles, .tx_elapsed_cycles = source.tx_elapsed_cycles,
            .raw_rx = source.raw_rx, .raw_tx = source.raw_tx,
            .sequence = source.sequence, .ordinal = source.ordinal
        };
    }
    tdma_event_record_t terminal = record(UINT32_MAX, 16u), out;
    assert(tdma_event_history_append(&history, &terminal, 1u, (uint64_t)UINT32_MAX + 1u));
    assert(tdma_event_history_lookup(&history, 1u, 16u, &out) && out.ordinal == UINT32_MAX);
    assert(history.accepted == (uint64_t)UINT32_MAX + 1u);
    assert(history.evicted == UINT32_MAX - TDMA_EVENT_HISTORY_CAPACITY + 1u);
    terminal.ordinal = 0u; terminal.sequence = 17u;
    assert(!tdma_event_history_append(&history, &terminal, 1u, (uint64_t)UINT32_MAX + 2u));
    assert_retired(&history, TDMA_EVENT_HISTORY_BAD_ORDINAL);
}

static void test_arguments_and_invalid_counts(void)
{
    tdma_event_record_t out, input = record(0u, 1u);
    tdma_event_history_init(NULL); tdma_event_history_retire(NULL);
    memset(&out, 0xa5, sizeof(out));
    assert(!tdma_event_history_lookup(NULL, 1u, 1u, &out));
    assert_zero(&out, sizeof(out));
    assert(!tdma_event_history_append(NULL, &input, 1u, 1u));
    assert(!tdma_event_history_start(NULL, 1u, 1u, (tdma_event_interval_t){0u, 0u}));
    tdma_event_history_t history = started();
    assert(!tdma_event_history_lookup(&history, 1u, 1u, NULL));
    assert(history.active && history.query_reason == TDMA_EVENT_HISTORY_BAD_ARGUMENT);
    assert(!tdma_event_history_append(&history, &input, SIZE_MAX, 1u));
    assert_retired(&history, TDMA_EVENT_HISTORY_BAD_COUNT);
    history = started();
    assert(!tdma_event_history_append(&history, NULL, 1u, 1u));
    assert_retired(&history, TDMA_EVENT_HISTORY_BAD_ARGUMENT);
    history = started();
    assert(!tdma_event_history_start(&history, 2u, 1u, (tdma_event_interval_t){0u, 0u}));
    assert_retired(&history, TDMA_EVENT_HISTORY_BAD_STATE);
    assert(!tdma_event_history_start(&history, 2u, 0u, (tdma_event_interval_t){0u, 0u}));
    assert_retired(&history, TDMA_EVENT_HISTORY_BAD_ARGUMENT);
    assert(!tdma_event_history_start(&history, 2u, 1u, (tdma_event_interval_t){1u, 0u}));
    assert_retired(&history, TDMA_EVENT_HISTORY_BAD_ARGUMENT);
    history = started(); history.count = TDMA_EVENT_HISTORY_CAPACITY + 1u;
    assert_unavailable(&history, 1u, 1u);
    assert_retired(&history, TDMA_EVENT_HISTORY_BAD_ARGUMENT);
}

int main(void)
{
    test_compact_and_conservative_context();
    test_sequence_wrap();
    test_eviction_many_batches();
    test_bad_flags_entire_batch_retires();
    test_incomplete_batches();
    test_bad_epoch_ordinal_and_sequence();
    test_bad_time_and_start_bounds();
    test_retire_rearm_and_old_epoch();
    test_corrupt_duplicate_is_ambiguous();
    test_ordinal_limit_without_four_billion_iterations();
    test_arguments_and_invalid_counts();
    printf("event history: 11 production case groups passed; compact=%zu history=%zu output=%zu\n",
           sizeof(tdma_event_history_record_t), sizeof(tdma_event_history_t), sizeof(tdma_event_record_t));
    return 0;
}
