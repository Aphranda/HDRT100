#ifndef SYNC_IO_RATE_SCHEDULE_H
#define SYNC_IO_RATE_SCHEDULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYNC_IO_RATE_SCHEDULE_MAX_PULSES 2048u
#define SYNC_IO_RATE_SCHEDULE_WORDS_PER_PULSE 2u
#define SYNC_IO_RATE_SCHEDULE_SECTION_TICKS 4u

typedef struct {
    uint32_t request_id;
    int32_t rate_ppb;
    uint32_t period_ns;
    uint32_t high_ns;
    uint32_t pulse_count;
    uint32_t tick_period_ns;
} sync_io_rate_schedule_request_t;

typedef struct {
    uint64_t first_edge_ticks;
    uint64_t last_edge_ticks;
    uint64_t total_ticks;
    uint32_t min_period_ticks;
    uint32_t max_period_ticks;
    uint32_t high_ticks;
} sync_io_rate_schedule_encoding_t;

/* rate_ppb is elapsed DCO time gain: a positive rate shortens the physical
 * period. The local high width is fixed; only cumulative rising edges scale.
 * Bound all sections before touching the caller's words or result. */
static inline bool sync_io_rate_schedule_request_valid(
    const sync_io_rate_schedule_request_t *request)
{
    if (request == NULL || request->rate_ppb <= -1000000000 ||
        request->period_ns == 0u || request->high_ns == 0u ||
        request->pulse_count == 0u ||
        request->pulse_count > SYNC_IO_RATE_SCHEDULE_MAX_PULSES ||
        (request->tick_period_ns != 4u && request->tick_period_ns != 100u)) {
        return false;
    }
    const uint64_t gain = (uint64_t)(1000000000ll + request->rate_ppb);
    const uint64_t denominator = gain * request->tick_period_ns;
    const uint64_t numerator = (uint64_t)request->period_ns * 1000000000ull;
    const uint64_t whole = numerator / denominator;
    const uint64_t upper = whole + (numerator % denominator != 0u);
    const uint64_t high =
        ((uint64_t)request->high_ns + request->tick_period_ns - 1u) /
        request->tick_period_ns;
    /* word zero has an ambiguous legacy low-section decoder. Requiring five
     * low ticks avoids it; high word zero correctly represents four ticks. */
    return high >= SYNC_IO_RATE_SCHEDULE_SECTION_TICKS &&
           whole >= high + SYNC_IO_RATE_SCHEDULE_SECTION_TICKS + 1u &&
           upper <= UINT32_MAX;
}

/* All pointers must refer to distinct objects. On rejection no output bytes
 * change. Each edge is ceil(n * period_ns * 1e9 / (gain * tick_ns)); differencing
 * these cumulative edges, not rounded periods, preserves sub-tick rate gain.
 * The initial PIO enable phase is intentionally outside this encoding. */
static inline bool sync_io_rate_schedule_generate(
    const sync_io_rate_schedule_request_t *request,
    uint32_t *words,
    size_t word_capacity,
    sync_io_rate_schedule_encoding_t *encoding)
{
    if (words == NULL || encoding == NULL ||
        !sync_io_rate_schedule_request_valid(request) ||
        word_capacity < (size_t)request->pulse_count *
                            SYNC_IO_RATE_SCHEDULE_WORDS_PER_PULSE) {
        return false;
    }
    const uint64_t gain = (uint64_t)(1000000000ll + request->rate_ppb);
    const uint64_t denominator = gain * request->tick_period_ns;
    const uint64_t numerator = (uint64_t)request->period_ns * 1000000000ull;
    const uint64_t whole = numerator / denominator;
    const uint64_t fraction = numerator % denominator;
    const uint32_t high = (uint32_t)(
        ((uint64_t)request->high_ns + request->tick_period_ns - 1u) /
        request->tick_period_ns);
    sync_io_rate_schedule_encoding_t result = {0};
    result.high_ticks = high;
    result.min_period_ticks = UINT32_MAX;
    uint64_t whole_sum = 0u;
    uint64_t remainder = 0u;
    uint64_t previous_edge = 0u;
    /* numerator <= UINT32_MAX*1e9, denominator < 315e9, and the admitted
     * whole period <= UINT32_MAX. Across <=2048 entries the cumulative ticks
     * and their product with either supported tick fit comfortably in u64. */
    for (uint32_t index = 0u; index < request->pulse_count; ++index) {
        whole_sum += whole;
        remainder += fraction;
        if (remainder >= denominator) {
            remainder -= denominator;
            ++whole_sum;
        }
        const uint64_t edge = whole_sum + (remainder != 0u);
        const uint32_t period = (uint32_t)(edge - previous_edge);
        const uint32_t low = index == 0u ? period : period - high;
        words[index * SYNC_IO_RATE_SCHEDULE_WORDS_PER_PULSE] =
            low - SYNC_IO_RATE_SCHEDULE_SECTION_TICKS;
        words[index * SYNC_IO_RATE_SCHEDULE_WORDS_PER_PULSE + 1u] =
            high - SYNC_IO_RATE_SCHEDULE_SECTION_TICKS;
        if (index == 0u) {
            result.first_edge_ticks = edge;
        }
        if (period < result.min_period_ticks) {
            result.min_period_ticks = period;
        }
        if (period > result.max_period_ticks) {
            result.max_period_ticks = period;
        }
        previous_edge = edge;
    }
    result.last_edge_ticks = previous_edge;
    result.total_ticks = previous_edge + high;
    *encoding = result;
    return true;
}

/* A finite interval experiment: pulse zero is a wider synchronization marker.
 * Its rising edge remains on the same cumulative grid as all other pulses.
 * The first falling edge changes, so only the next low section compensates.
 * No absolute enable phase or cross-board synchronization is implied. */
static inline bool sync_io_rate_schedule_marked_valid(
    const sync_io_rate_schedule_request_t *request, uint32_t marker_high_ns)
{
    if (!sync_io_rate_schedule_request_valid(request) ||
        request->pulse_count < 2u || marker_high_ns <= request->high_ns) {
        return false;
    }
    const uint64_t marker =
        ((uint64_t)marker_high_ns + request->tick_period_ns - 1u) /
        request->tick_period_ns;
    const uint64_t high =
        ((uint64_t)request->high_ns + request->tick_period_ns - 1u) /
        request->tick_period_ns;
    const uint64_t gain = (uint64_t)(INT64_C(1000000000) + request->rate_ppb);
    const uint64_t minimum_period = (uint64_t)request->period_ns * UINT64_C(1000000000) /
        (gain * request->tick_period_ns);
    return marker > high && marker <= UINT32_MAX &&
        minimum_period >= marker + SYNC_IO_RATE_SCHEDULE_SECTION_TICKS + 1u;
}

static inline bool sync_io_rate_schedule_generate_marked(
    const sync_io_rate_schedule_request_t *request, uint32_t marker_high_ns,
    uint32_t *words, size_t word_capacity,
    sync_io_rate_schedule_encoding_t *encoding)
{
    if (!sync_io_rate_schedule_marked_valid(request, marker_high_ns) ||
        !sync_io_rate_schedule_generate(request, words, word_capacity, encoding)) {
        return false;
    }
    const uint32_t marker = (uint32_t)(((uint64_t)marker_high_ns +
        request->tick_period_ns - 1u) / request->tick_period_ns);
    words[1] = marker - SYNC_IO_RATE_SCHEDULE_SECTION_TICKS;
    words[2] -= marker - encoding->high_ticks;
    return true;
}

/* Compute one absolute grid edge without multiplying the large period
 * numerator by its index. Caller validates the request and bounds the index
 * by MAX_PULSES + 1; both quotient and remainder products then fit u64. */
static inline uint64_t sync_io_rate_schedule_edge_ticks(
    const sync_io_rate_schedule_request_t *request, uint32_t index)
{
    const uint64_t denominator =
        (uint64_t)(INT64_C(1000000000) + request->rate_ppb) * request->tick_period_ns;
    const uint64_t numerator = (uint64_t)request->period_ns * UINT64_C(1000000000);
    return (numerator / denominator) * index +
        ((numerator % denominator) * index + denominator - 1u) / denominator;
}

/* One finite window between two local markers. For N=request.pulse_count,
 * grid positions relative to the first marker are 0,1,...,N-2,N: position
 * N-1 is deliberately silent. Both markers use marker_high_ns; the remaining
 * N-2 pulses use high_ns. No shared phase or live RUN attachment is implied. */
static inline bool sync_io_rate_schedule_window_valid(
    const sync_io_rate_schedule_request_t *request, uint32_t marker_high_ns)
{
    if (!sync_io_rate_schedule_marked_valid(request, marker_high_ns) ||
        request->pulse_count < 3u) {
        return false;
    }
    const uint64_t tail_period =
        sync_io_rate_schedule_edge_ticks(request, request->pulse_count + 1u) -
        sync_io_rate_schedule_edge_ticks(request, request->pulse_count - 1u);
    /* Period statistics and the PIO low word must remain representable. */
    return tail_period <= UINT32_MAX;
}

static inline bool sync_io_rate_schedule_generate_window(
    const sync_io_rate_schedule_request_t *request, uint32_t marker_high_ns,
    uint32_t *words, size_t word_capacity,
    sync_io_rate_schedule_encoding_t *encoding)
{
    if (!sync_io_rate_schedule_window_valid(request, marker_high_ns) ||
        words == NULL || encoding == NULL ||
        word_capacity < (size_t)request->pulse_count *
                            SYNC_IO_RATE_SCHEDULE_WORDS_PER_PULSE) {
        return false;
    }
    const uint32_t high = (uint32_t)(
        ((uint64_t)request->high_ns + request->tick_period_ns - 1u) /
        request->tick_period_ns);
    const uint32_t marker = (uint32_t)(
        ((uint64_t)marker_high_ns + request->tick_period_ns - 1u) /
        request->tick_period_ns);
    sync_io_rate_schedule_encoding_t result = {0};
    result.high_ticks = high;
    result.min_period_ticks = UINT32_MAX;
    uint64_t previous_edge = 0u;
    uint32_t previous_high = 0u;
    for (uint32_t index = 0u; index < request->pulse_count; ++index) {
        const bool last = index + 1u == request->pulse_count;
        const uint32_t grid_index = last ? index + 2u : index + 1u;
        const uint64_t edge = sync_io_rate_schedule_edge_ticks(request, grid_index);
        const uint32_t period = (uint32_t)(edge - previous_edge);
        const uint32_t current_high = index == 0u || last ? marker : high;
        words[index * SYNC_IO_RATE_SCHEDULE_WORDS_PER_PULSE] =
            period - previous_high - SYNC_IO_RATE_SCHEDULE_SECTION_TICKS;
        words[index * SYNC_IO_RATE_SCHEDULE_WORDS_PER_PULSE + 1u] =
            current_high - SYNC_IO_RATE_SCHEDULE_SECTION_TICKS;
        if (index == 0u) result.first_edge_ticks = edge;
        if (period < result.min_period_ticks) result.min_period_ticks = period;
        if (period > result.max_period_ticks) result.max_period_ticks = period;
        previous_edge = edge;
        previous_high = current_high;
    }
    result.last_edge_ticks = previous_edge;
    result.total_ticks = previous_edge + marker;
    *encoding = result;
    return true;
}

#endif
