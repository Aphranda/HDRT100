#include "tdma_event_observer.h"

#include <limits.h>
#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define TDMA_EVENT_FEED_RAM __not_in_flash("tdma_event_feed") __attribute__((noinline, noclone))
#else
#define TDMA_EVENT_FEED_RAM
#endif

#define TDMA_EVENT_WRAP_PERIOD UINT64_C(8589934593)

static bool interval_valid(tdma_event_interval_t interval)
{
    return interval.lo <= interval.hi;
}

static uint64_t nonnegative_difference(uint64_t a, uint64_t b)
{
    return a > b ? a - b : 0u;
}

static size_t invalidate(tdma_event_observer_t *observer,
                         tdma_event_reason_t reason)
{
    observer->state = TDMA_EVENT_INVALID;
    observer->reason = reason;
    memset(observer->pending_count, 0, sizeof(observer->pending_count));
    memset(observer->pending, 0, sizeof(observer->pending));
    return 0u;
}

tdma_event_reason_t tdma_event_observer_lift(uint32_t previous, uint32_t current,
                                           uint64_t lower, uint64_t upper,
                                           uint32_t overhead, uint64_t *elapsed)
{
    if (elapsed == NULL || lower > upper || (overhead != 1u && overhead != 5u)) {
        return TDMA_EVENT_BAD_ARGUMENT;
    }
    const uint64_t decrement = (uint32_t)(previous - current);
    const uint64_t base = 2u * decrement + overhead + (current > previous ? 1u : 0u);
    if (upper < base) {
        return TDMA_EVENT_LIFT_NO_CANDIDATE;
    }
    /* If the window cannot reach base + one full period, q=0 is the only
     * possible lift. Keep the general path for longer/ambiguous windows. */
    if (upper - base < TDMA_EVENT_WRAP_PERIOD) {
        if (lower > base) {
            return TDMA_EVENT_LIFT_NO_CANDIDATE;
        }
        *elapsed = base;
        return TDMA_EVENT_OK;
    }
    const uint64_t distance = nonnegative_difference(lower, base);
    const uint64_t qlo = distance / TDMA_EVENT_WRAP_PERIOD +
                         (distance % TDMA_EVENT_WRAP_PERIOD != 0u ? 1u : 0u);
    const uint64_t qhi = (upper - base) / TDMA_EVENT_WRAP_PERIOD;
    if (qlo > qhi) {
        return TDMA_EVENT_LIFT_NO_CANDIDATE;
    }
    if (qlo != qhi) {
        return TDMA_EVENT_LIFT_AMBIGUOUS;
    }
    if (qlo > (UINT64_MAX - base) / TDMA_EVENT_WRAP_PERIOD) {
        return TDMA_EVENT_TIME_OVERFLOW;
    }
    *elapsed = base + qlo * TDMA_EVENT_WRAP_PERIOD;
    return TDMA_EVENT_OK;
}

void tdma_event_observer_init(tdma_event_observer_t *observer)
{
    if (observer != NULL) {
        memset(observer, 0, sizeof(*observer));
    }
}

bool tdma_event_observer_start(tdma_event_observer_t *observer,
                               const tdma_event_config_t *config,
                               uint32_t epoch, tdma_event_interval_t start,
                               uint32_t initial_faults)
{
    if (observer == NULL || config == NULL || observer->state != TDMA_EVENT_STOPPED) {
        return false;
    }
    if (epoch == 0u || epoch <= observer->epoch) {
        observer->reason = TDMA_EVENT_BAD_EPOCH;
        return false;
    }
    if (!interval_valid(start) || config->pio_hz == 0u ||
        config->epoch_limit_cycles == 0u || config->join_timeout_cycles == 0u ||
        config->min_frame_cycles == 0u ||
        config->min_tx_delay_cycles > config->max_tx_delay_cycles ||
        config->max_tx_delay_cycles >= config->min_frame_cycles ||
        start.hi - start.lo > config->epoch_limit_cycles) {
        observer->reason = TDMA_EVENT_BAD_ARGUMENT;
        return false;
    }
    /* Even a dirty attempted start retires its generation: stale captured
     * batches cannot become valid after retrying that generation. */
    const tdma_event_config_t requested_config = *config;
    memset(observer, 0, sizeof(*observer));
    observer->epoch = epoch;
    observer->config = requested_config;
    observer->start = start;
    observer->anchor_bounds = start;
    observer->last_observed = start;
    observer->fault_bits = initial_faults;
    for (size_t stream = 0; stream < TDMA_EVENT_STREAMS; ++stream) {
        observer->last_empty[stream] = start.lo;
    }
    if (initial_faults != 0u) {
        invalidate(observer, TDMA_EVENT_PRE_FAULT);
        return false;
    }
    observer->state = TDMA_EVENT_ACTIVE;
    return true;
}

void tdma_event_observer_stop(tdma_event_observer_t *observer)
{
    if (observer == NULL) {
        return;
    }
    observer->state = TDMA_EVENT_STOPPED;
    memset(observer->pending_count, 0, sizeof(observer->pending_count));
    memset(observer->pending, 0, sizeof(observer->pending));
    memset(observer->previous, 0, sizeof(observer->previous));
    memset(observer->elapsed, 0, sizeof(observer->elapsed));
    observer->reads_last = 0u;
}

static uint32_t wire_sequence(uint32_t word)
{
    return (word >> 24u) | ((word >> 8u) & UINT32_C(0x0000ff00)) |
           ((word << 8u) & UINT32_C(0x00ff0000)) | (word << 24u);
}

static bool pending_expired(const tdma_event_observer_t *observer, uint64_t now)
{
    for (size_t stream = 0; stream < TDMA_EVENT_STREAMS; ++stream) {
        if (observer->pending_count[stream] != 0u &&
            nonnegative_difference(now, observer->pending[stream][0].age.hi) >
                observer->config.join_timeout_cycles) {
            return true;
        }
    }
    return false;
}

static tdma_event_reason_t lift_sample(tdma_event_observer_t *observer,
                                       size_t stream, tdma_event_word_t current)
{
    const bool first = observer->joined == 0u;
    const tdma_event_interval_t previous_age = first ? observer->start :
                                                observer->previous[stream].age;
    if (current.age.hi < previous_age.lo) {
        return TDMA_EVENT_ABSOLUTE_TIME;
    }
    uint64_t delta = 0u;
    const tdma_event_reason_t lifted = tdma_event_observer_lift(
        first ? UINT32_MAX : observer->previous[stream].word, current.word,
        nonnegative_difference(current.age.lo, previous_age.hi),
        current.age.hi - previous_age.lo, first ? 1u : 5u, &delta);
    if (lifted != TDMA_EVENT_OK) {
        return lifted;
    }
    if (!first && delta < observer->config.min_frame_cycles) {
        return TDMA_EVENT_FRAME_SPACING;
    }
    if (delta > UINT64_MAX - observer->elapsed[stream]) {
        return TDMA_EVENT_TIME_OVERFLOW;
    }
    const uint64_t elapsed = observer->elapsed[stream] + delta;
    if (current.age.hi < elapsed) {
        return TDMA_EVENT_ABSOLUTE_TIME;
    }
    const uint64_t anchor_lo = nonnegative_difference(current.age.lo, elapsed);
    const uint64_t anchor_hi = current.age.hi - elapsed;
    if (anchor_lo > observer->anchor_bounds.lo) {
        observer->anchor_bounds.lo = anchor_lo;
    }
    if (anchor_hi < observer->anchor_bounds.hi) {
        observer->anchor_bounds.hi = anchor_hi;
    }
    if (!interval_valid(observer->anchor_bounds)) {
        return TDMA_EVENT_ABSOLUTE_TIME;
    }
    observer->elapsed[stream] = elapsed;
    observer->previous[stream] = current;
    return TDMA_EVENT_OK;
}

size_t TDMA_EVENT_FEED_RAM tdma_event_observer_feed(tdma_event_observer_t *observer,
                                const tdma_event_batch_t *batch,
                                tdma_event_record_t out[TDMA_EVENT_MAX_RECORDS])
{
    if (observer == NULL || observer->state != TDMA_EVENT_ACTIVE) {
        return 0u;
    }
    observer->reads_last = 0u;
    if (batch == NULL || out == NULL || !interval_valid(batch->observed) ||
        (batch->empty_mask & ~TDMA_EVENT_ALL_EMPTY_MASK) != 0u) {
        return invalidate(observer, TDMA_EVENT_BAD_ARGUMENT);
    }
    if (batch->epoch != observer->epoch) {
        return invalidate(observer, TDMA_EVENT_BAD_EPOCH);
    }
    observer->fault_bits |= batch->pre_faults | batch->post_faults;
    if (batch->pre_faults != 0u) {
        return invalidate(observer, TDMA_EVENT_PRE_FAULT);
    }
    if (batch->post_faults != 0u) {
        return invalidate(observer, TDMA_EVENT_POST_FAULT);
    }
    if (batch->observed.lo < observer->last_observed.lo ||
        batch->observed.hi < observer->last_observed.hi ||
        batch->observed.hi < observer->start.lo ||
        batch->observed.hi - observer->start.lo > observer->config.epoch_limit_cycles) {
        return invalidate(observer, TDMA_EVENT_SERVICE_AGE);
    }
    for (size_t stream = 0; stream < TDMA_EVENT_STREAMS; ++stream) {
        const size_t count = batch->count[stream];
        const size_t pending = observer->pending_count[stream];
        if (count > TDMA_EVENT_FIFO_WORDS || count + pending > TDMA_EVENT_FIFO_WORDS) {
            return invalidate(observer, TDMA_EVENT_STAGING_OVERFLOW);
        }
        for (size_t word = 0; word < count; ++word) {
            observer->pending[stream][pending + word] = (tdma_event_word_t){
                .word = batch->words[stream][word],
                .age = {observer->last_empty[stream], batch->observed.hi}
            };
            ++observer->reads_last;
        }
        observer->pending_count[stream] = (uint8_t)(count + pending);
        if ((batch->empty_mask & (1u << stream)) != 0u) {
            observer->last_empty[stream] = batch->observed.lo;
        }
    }
    observer->last_observed = batch->observed;
    if (pending_expired(observer, batch->observed.lo)) {
        return invalidate(observer, TDMA_EVENT_JOIN_TIMEOUT);
    }

    size_t produced = 0u;
    while (produced < TDMA_EVENT_MAX_RECORDS &&
           observer->pending_count[0] >= 2u && observer->pending_count[1] >= 2u &&
           observer->pending_count[2] >= 1u) {
        if (observer->joined > UINT32_MAX) {
            return invalidate(observer, TDMA_EVENT_ORDINAL);
        }
        const uint32_t ordinal = (uint32_t)observer->joined;
        const uint32_t expected_y = UINT32_MAX - ordinal;
        if (observer->pending[0][1].word != expected_y ||
            observer->pending[1][1].word != expected_y) {
            return invalidate(observer, TDMA_EVENT_ORDINAL);
        }
        const uint32_t sequence = wire_sequence(observer->pending[2][0].word);
        if (observer->joined != 0u &&
            (observer->last_sequence == UINT32_MAX || sequence != observer->last_sequence + 1u)) {
            return invalidate(observer, TDMA_EVENT_SEQUENCE);
        }
        for (size_t stream = 0; stream < 2u; ++stream) {
            const tdma_event_reason_t result = lift_sample(observer, stream,
                                                           observer->pending[stream][0]);
            if (result != TDMA_EVENT_OK) {
                return invalidate(observer, result);
            }
        }
        if (observer->elapsed[1] < observer->elapsed[0]) {
            return invalidate(observer, TDMA_EVENT_PAIR_SKEW);
        }
        const uint64_t skew = observer->elapsed[1] - observer->elapsed[0];
        if (skew < observer->config.min_tx_delay_cycles ||
            skew > observer->config.max_tx_delay_cycles) {
            return invalidate(observer, TDMA_EVENT_PAIR_SKEW);
        }
        out[produced] = (tdma_event_record_t){
            .epoch = observer->epoch,
            .ordinal = ordinal,
            .sequence = sequence,
            .raw_rx = observer->pending[0][0].word,
            .raw_tx = observer->pending[1][0].word,
            .rx_elapsed_cycles = observer->elapsed[0],
            .tx_elapsed_cycles = observer->elapsed[1],
            .start_bounds = observer->anchor_bounds,
            .diagnostic_only = true,
            .physical_first_unproved = true,
            .identity_unproved = true,
            .timestamp_valid = false,
            .dpll_eligible = false
        };
        for (size_t stream = 0; stream < TDMA_EVENT_STREAMS; ++stream) {
            const size_t stride = stream < 2u ? 2u : 1u;
            observer->pending_count[stream] = (uint8_t)(observer->pending_count[stream] - stride);
            memmove(observer->pending[stream], observer->pending[stream] + stride,
                    observer->pending_count[stream] * sizeof(observer->pending[stream][0]));
        }
        if (observer->joined == 0u) {
            observer->first_sequence = sequence;
        }
        observer->last_sequence = sequence;
        ++observer->joined;
        ++produced;
    }
    if (pending_expired(observer, batch->observed.lo)) {
        return invalidate(observer, TDMA_EVENT_JOIN_TIMEOUT);
    }
    return produced;
}
