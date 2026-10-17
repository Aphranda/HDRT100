#ifndef SYNC_IO_SEQUENCE_PLAN_H
#define SYNC_IO_SEQUENCE_PLAN_H
#include <stdbool.h>
#include <stdint.h>

/* One position's group sequence expressed as the two-word trigger stream the PIO consumes:
 * [code0, delay0, code1, delay1, ...]. The delay of every group is the GAP to the previous
 * edge minus the fixed instruction budget (see sync_io_sequence_plan.c for the derivation),
 * which is what makes the spacing between groups independent of software timing. */
#define SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS 8u
#define SYNC_IO_SEQUENCE_PLAN_MAX_WORDS (SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS * 2u)

typedef enum {
    SYNC_IO_SEQUENCE_PLAN_OK = 0,
    SYNC_IO_SEQUENCE_PLAN_EMPTY,
    SYNC_IO_SEQUENCE_PLAN_INVALID,
    SYNC_IO_SEQUENCE_PLAN_NON_MONOTONIC,
    SYNC_IO_SEQUENCE_PLAN_DEADLINE_PASSED,
    SYNC_IO_SEQUENCE_PLAN_TOO_CLOSE,
    SYNC_IO_SEQUENCE_PLAN_TOO_FAR
} sync_io_sequence_plan_error_t;

typedef struct {
    uint32_t words[SYNC_IO_SEQUENCE_PLAN_MAX_WORDS];
    uint32_t count;      /* groups */
    uint32_t word_count; /* count * 2, the number of words the SM will pull */
    uint64_t first_deadline_tick, last_deadline_tick;
    sync_io_sequence_plan_error_t error;
} sync_io_sequence_plan_t;

/* Build the stream for `count` groups. `delta_ticks[i]` is the offset of group i from the
 * position's base tick (strictly increasing); the base deadline is the absolute local tick
 * of group 0. Returns the first error found and leaves `plan->error` set. */
/* Delay word for group 0 measured from `now_tick`: the edge budget is the same constant the
 * plan builder uses. Returns false when the deadline is already too close to express. */
bool sync_io_sequence_plan_first_delay(uint64_t now_tick, uint64_t base_tick, uint32_t *delay);

sync_io_sequence_plan_error_t sync_io_sequence_plan_build(sync_io_sequence_plan_t *plan,
    uint64_t now_tick, uint64_t base_deadline_tick, const uint32_t *codes,
    const uint64_t *delta_ticks, uint32_t count);

void sync_io_sequence_plan_reset(sync_io_sequence_plan_t *plan);

#endif
