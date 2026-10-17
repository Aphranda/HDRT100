/*
 * Turning an absolute group plan into the two-word trigger stream the PIO consumes.
 *
 * The PIO program (sync_switch.pio / sync_gateway.pio) takes, per group, the output code
 * (or the pulse width) followed by a delay counted in clk_sys cycles. The delay must be the
 * GAP to the previous edge, never "deadline - now": a queued trigger is only counted after
 * the previous one has been output, so "deadline - now" makes every group after the first
 * fire late by exactly the time it spent waiting (measured 2026-10-17 on the bench).
 *
 * Cycle budget, derived from the instruction stream rather than guessed (sync_switch.pio):
 *   pull(1) out y(1) pull(1) out x(1) then the jmp loop runs X+1 cycles, then mov pins:
 *   first_edge = start + 5 + X      => X = deadline - start - 5
 *   after an edge: mov isr(1) push(1) wrap pull(1) out y(1) pull(1) out x(1) then the loop:
 *   next_edge = previous_edge + 7 + X => X = gap - 7
 * The gateway program inserts irq + set pins and carries a pulse width W, so its numbers
 * live next to it (first: -6, chained: gap - W - 8) and are NOT used here.
 */
#include "sync_io_sequence_plan.h"

#include <string.h>

#define FIRST_EDGE_CYCLES 5u
#define CHAINED_EDGE_CYCLES 7u

void sync_io_sequence_plan_reset(sync_io_sequence_plan_t *plan)
{
    if (plan == NULL) return;
    memset(plan, 0, sizeof(*plan));
    plan->error = SYNC_IO_SEQUENCE_PLAN_EMPTY;
}

sync_io_sequence_plan_error_t sync_io_sequence_plan_build(sync_io_sequence_plan_t *plan,
    uint64_t now_tick, uint64_t base_deadline_tick, const uint32_t *codes,
    const uint64_t *delta_ticks, uint32_t count)
{
    if (plan == NULL) return SYNC_IO_SEQUENCE_PLAN_INVALID;
    sync_io_sequence_plan_reset(plan);
    if (codes == NULL || delta_ticks == NULL || count == 0u ||
        count > SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS) {
        plan->error = SYNC_IO_SEQUENCE_PLAN_INVALID;
        return plan->error;
    }
    /* The groups must advance in time: a plan whose deltas do not move forward cannot be
     * expressed as positive delays, and a zero gap would queue two edges in the same tick. */
    for (uint32_t i = 0u; i < count; ++i) {
        if (delta_ticks[i] == 0u) {
            plan->error = SYNC_IO_SEQUENCE_PLAN_NON_MONOTONIC;
            return plan->error;
        }
        if (i > 0u && delta_ticks[i] <= delta_ticks[i - 1u]) {
            plan->error = SYNC_IO_SEQUENCE_PLAN_NON_MONOTONIC;
            return plan->error;
        }
    }
    if (base_deadline_tick <= now_tick) {
        plan->error = SYNC_IO_SEQUENCE_PLAN_DEADLINE_PASSED;
        return plan->error;
    }
    const uint64_t first_gap = base_deadline_tick - now_tick;
    if (first_gap <= FIRST_EDGE_CYCLES) {
        plan->error = SYNC_IO_SEQUENCE_PLAN_TOO_CLOSE;
        return plan->error;
    }
    const uint64_t first_delay = first_gap - FIRST_EDGE_CYCLES;
    if (first_delay > UINT32_MAX) {
        plan->error = SYNC_IO_SEQUENCE_PLAN_TOO_FAR;
        return plan->error;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        uint64_t delay;
        if (i == 0u) {
            delay = first_delay;
        } else {
            const uint64_t gap = delta_ticks[i] - delta_ticks[i - 1u];
            if (gap <= CHAINED_EDGE_CYCLES) {
                plan->error = SYNC_IO_SEQUENCE_PLAN_TOO_CLOSE;
                return plan->error;
            }
            delay = gap - CHAINED_EDGE_CYCLES;
        }
        if (delay > UINT32_MAX) {
            plan->error = SYNC_IO_SEQUENCE_PLAN_TOO_FAR;
            return plan->error;
        }
        plan->words[i * 2u] = codes[i];
        plan->words[i * 2u + 1u] = (uint32_t)delay;
    }
    plan->count = count;
    plan->word_count = count * 2u;
    plan->first_deadline_tick = base_deadline_tick;
    plan->last_deadline_tick = base_deadline_tick + (delta_ticks[count - 1u] - delta_ticks[0u]);
    plan->error = SYNC_IO_SEQUENCE_PLAN_OK;
    return plan->error;
}
