#include "tdma_event_history.h"

#include <string.h>

_Static_assert(sizeof(tdma_event_history_record_t) == 32u,
               "Diagnostic event history must remain compact");
_Static_assert((TDMA_EVENT_HISTORY_CAPACITY & (TDMA_EVENT_HISTORY_CAPACITY - 1u)) == 0u,
               "History capacity must be a power of two");

void tdma_event_history_init(tdma_event_history_t *history)
{
    if (history == NULL) return;
    memset(history, 0, sizeof(*history));
    history->oldest_ordinal = UINT32_MAX;
    history->query_reason = TDMA_EVENT_HISTORY_INACTIVE;
}

void tdma_event_history_retire(tdma_event_history_t *history)
{
    if (history == NULL) return;
    history->active = false;
    memset(history->records, 0, sizeof(history->records));
    history->count = 0u;
    history->next = 0u;
    history->oldest_ordinal = UINT32_MAX;
    history->initial_start = (tdma_event_interval_t){0u, 0u};
    history->pio_hz = 0u;
    if (history->reason == TDMA_EVENT_HISTORY_OK)
        history->reason = TDMA_EVENT_HISTORY_RETIRED;
    history->query_reason = TDMA_EVENT_HISTORY_INACTIVE;
}

static bool reject(tdma_event_history_t *history, tdma_event_history_reason_t reason)
{
    history->reason = reason;
    tdma_event_history_retire(history);
    return false;
}

bool tdma_event_history_start(tdma_event_history_t *history, uint32_t epoch,
                              uint32_t pio_hz, tdma_event_interval_t initial_start)
{
    if (history == NULL) return false;
    if (history->active) return reject(history, TDMA_EVENT_HISTORY_BAD_STATE);
    if (epoch == 0u || epoch <= history->epoch)
        return reject(history, TDMA_EVENT_HISTORY_BAD_EPOCH);
    if (pio_hz == 0u || initial_start.lo > initial_start.hi)
        return reject(history, TDMA_EVENT_HISTORY_BAD_ARGUMENT);
    tdma_event_history_init(history);
    history->epoch = epoch;
    history->pio_hz = pio_hz;
    history->initial_start = initial_start;
    history->active = true;
    history->query_reason = TDMA_EVENT_HISTORY_OK;
    return true;
}

bool tdma_event_history_append(tdma_event_history_t *history,
                               const tdma_event_record_t *records, size_t count,
                               uint64_t source_joined)
{
    if (history == NULL) return false;
    if (!history->active) return false;
    if (count > TDMA_EVENT_MAX_RECORDS)
        return reject(history, TDMA_EVENT_HISTORY_BAD_COUNT);
    if ((records == NULL && count != 0u) ||
        history->count > TDMA_EVENT_HISTORY_CAPACITY ||
        history->next >= TDMA_EVENT_HISTORY_CAPACITY)
        return reject(history, TDMA_EVENT_HISTORY_BAD_ARGUMENT);
    const uint64_t ordinal_limit = (uint64_t)UINT32_MAX + 1u;
    if (history->accepted > ordinal_limit - count)
        return reject(history, TDMA_EVENT_HISTORY_BAD_ORDINAL);
    const uint64_t accepted = history->accepted + count;
    if (source_joined != accepted)
        return reject(history, TDMA_EVENT_HISTORY_INCOMPLETE_BATCH);

    const tdma_event_history_record_t *previous = history->count != 0u
        ? &history->records[(history->next + TDMA_EVENT_HISTORY_CAPACITY - 1u) &
                            (TDMA_EVENT_HISTORY_CAPACITY - 1u)] : NULL;
    uint64_t previous_rx = previous != NULL ? previous->rx_elapsed_cycles : 0u;
    uint64_t previous_tx = previous != NULL ? previous->tx_elapsed_cycles : 0u;
    uint32_t previous_sequence = previous != NULL ? previous->sequence : 0u;
    /* First pass changes no available record, cursor or acceptance count.
     * A bad trailing record cannot leave a good-looking prefix published. */
    for (size_t i = 0u; i < count; ++i) {
        const tdma_event_record_t *record = &records[i];
        if (record->epoch != history->epoch)
            return reject(history, TDMA_EVENT_HISTORY_BAD_EPOCH);
        if (!record->diagnostic_only || !record->physical_first_unproved ||
            !record->identity_unproved || record->timestamp_valid || record->dpll_eligible)
            return reject(history, TDMA_EVENT_HISTORY_BAD_FLAGS);
        if ((uint64_t)record->ordinal != history->accepted + i)
            return reject(history, TDMA_EVENT_HISTORY_BAD_ORDINAL);
        if ((history->accepted != 0u || i != 0u) &&
            record->sequence != (uint32_t)(previous_sequence + UINT32_C(1)))
            return reject(history, TDMA_EVENT_HISTORY_BAD_SEQUENCE);
        if (record->start_bounds.lo > record->start_bounds.hi ||
            record->start_bounds.lo < history->initial_start.lo ||
            record->start_bounds.hi > history->initial_start.hi)
            return reject(history, TDMA_EVENT_HISTORY_BAD_START_BOUNDS);
        if (record->rx_elapsed_cycles <= previous_rx ||
            record->tx_elapsed_cycles <= previous_tx ||
            record->tx_elapsed_cycles < record->rx_elapsed_cycles ||
            history->initial_start.hi > UINT64_MAX - record->tx_elapsed_cycles)
            return reject(history, TDMA_EVENT_HISTORY_BAD_TIME);
        previous_rx = record->rx_elapsed_cycles;
        previous_tx = record->tx_elapsed_cycles;
        previous_sequence = record->sequence;
    }

    for (size_t i = 0u; i < count; ++i) {
        const tdma_event_record_t *record = &records[i];
        history->records[history->next] = (tdma_event_history_record_t){
            .rx_elapsed_cycles = record->rx_elapsed_cycles,
            .tx_elapsed_cycles = record->tx_elapsed_cycles,
            .raw_rx = record->raw_rx,
            .raw_tx = record->raw_tx,
            .sequence = record->sequence,
            .ordinal = record->ordinal
        };
        history->next = (history->next + 1u) & (TDMA_EVENT_HISTORY_CAPACITY - 1u);
        if (history->count == TDMA_EVENT_HISTORY_CAPACITY) ++history->evicted;
        else ++history->count;
    }
    history->accepted = accepted;
    if (history->count != 0u)
        history->oldest_ordinal = (uint32_t)(accepted - history->count);
    return true;
}

bool tdma_event_history_lookup(tdma_event_history_t *history, uint32_t epoch,
                               uint32_t sequence, tdma_event_record_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (history == NULL) return false;
    if (out == NULL) {
        history->query_reason = TDMA_EVENT_HISTORY_BAD_ARGUMENT;
        return false;
    }
    if (!history->active) {
        history->query_reason = TDMA_EVENT_HISTORY_INACTIVE;
        return false;
    }
    if (epoch != history->epoch) {
        history->query_reason = TDMA_EVENT_HISTORY_BAD_EPOCH;
        return false;
    }
    if (history->count > TDMA_EVENT_HISTORY_CAPACITY ||
        history->next >= TDMA_EVENT_HISTORY_CAPACITY) {
        reject(history, TDMA_EVENT_HISTORY_BAD_ARGUMENT);
        history->query_reason = TDMA_EVENT_HISTORY_BAD_ARGUMENT;
        return false;
    }
    const tdma_event_history_record_t *match = NULL;
    const uint32_t oldest = (history->next + TDMA_EVENT_HISTORY_CAPACITY - history->count) &
                              (TDMA_EVENT_HISTORY_CAPACITY - 1u);
    for (uint32_t i = 0u; i < history->count; ++i) {
        const tdma_event_history_record_t *record =
            &history->records[(oldest + i) & (TDMA_EVENT_HISTORY_CAPACITY - 1u)];
        if (record->sequence != sequence) continue;
        if (match != NULL) {
            reject(history, TDMA_EVENT_HISTORY_AMBIGUOUS);
            history->query_reason = TDMA_EVENT_HISTORY_AMBIGUOUS;
            return false;
        }
        match = record;
    }
    if (match == NULL) {
        history->query_reason = TDMA_EVENT_HISTORY_NOT_FOUND;
        return false;
    }
    *out = (tdma_event_record_t){
        .epoch = history->epoch,
        .ordinal = match->ordinal,
        .sequence = match->sequence,
        .raw_rx = match->raw_rx,
        .raw_tx = match->raw_tx,
        .rx_elapsed_cycles = match->rx_elapsed_cycles,
        .tx_elapsed_cycles = match->tx_elapsed_cycles,
        .start_bounds = history->initial_start,
        .diagnostic_only = true,
        .physical_first_unproved = true,
        .identity_unproved = true,
        .timestamp_valid = false,
        .dpll_eligible = false
    };
    history->query_reason = TDMA_EVENT_HISTORY_OK;
    return true;
}
