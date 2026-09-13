#include "tdma_origin_blackout.h"
#include <stddef.h>

static void write_begin(tdma_origin_blackout_t *trial)
{
    (void)__atomic_add_fetch(&trial->guard, 1u, __ATOMIC_ACQ_REL);
}

static void write_end(tdma_origin_blackout_t *trial)
{
    (void)__atomic_add_fetch(&trial->guard, 1u, __ATOMIC_RELEASE);
}

void tdma_origin_blackout_reset(tdma_origin_blackout_t *trial, bool enabled,
    uint32_t epoch, uint32_t config_seq)
{
    write_begin(trial);
    trial->snapshot = (tdma_origin_blackout_snapshot_t){
        .schema = TDMA_ORIGIN_BLACKOUT_SCHEMA,
        .state = enabled ? TDMA_ORIGIN_BLACKOUT_WAITING : TDMA_ORIGIN_BLACKOUT_DISABLED,
        .trial_epoch = epoch, .config_seq = config_seq};
    write_end(trial);
}

tdma_origin_blackout_action_t tdma_origin_blackout_step(tdma_origin_blackout_t *trial,
    const tdma_origin_blackout_input_t *input)
{
    tdma_origin_blackout_snapshot_t *s = &trial->snapshot;
    if (s->state != TDMA_ORIGIN_BLACKOUT_WAITING && s->state != TDMA_ORIGIN_BLACKOUT_ACTIVE)
        return TDMA_ORIGIN_BLACKOUT_SERVICE;
    write_begin(trial);
    tdma_origin_blackout_action_t action = TDMA_ORIGIN_BLACKOUT_SERVICE;
    if (!input->authorized) {
        s->state = TDMA_ORIGIN_BLACKOUT_CANCELLED;
    } else if (s->state == TDMA_ORIGIN_BLACKOUT_WAITING) {
        if (!input->ready) {
            s->wait_calls = 0u;
        } else if (++s->wait_calls >= TDMA_ORIGIN_BLACKOUT_SETTLE_CALLS) {
            if (input->record_epoch == 0u || input->record_version == 0u ||
                (input->record_version & 1u) != 0u || input->clock_hz == 0u) {
                s->state = TDMA_ORIGIN_BLACKOUT_INVALID;
            } else {
                s->record_epoch = input->record_epoch;
                s->clock_hz = input->clock_hz;
                s->before_version = input->record_version;
                s->begin_ticks = input->now_ticks;
                s->skipped_calls = 1u;
                s->state = TDMA_ORIGIN_BLACKOUT_ACTIVE;
                action = TDMA_ORIGIN_BLACKOUT_SKIP;
            }
        }
    } else if (!input->ready || input->record_epoch != s->record_epoch ||
               input->clock_hz != s->clock_hz || (input->record_version & 1u) != 0u) {
        s->state = TDMA_ORIGIN_BLACKOUT_INVALID;
    } else if (input->now_ticks < s->begin_ticks || input->now_ticks - s->begin_ticks >
               (uint64_t)s->clock_hz * TDMA_ORIGIN_BLACKOUT_MAX_INTERVAL_US / 1000000u) {
        s->state = TDMA_ORIGIN_BLACKOUT_DEADLINE;
    } else if (s->skipped_calls == TDMA_ORIGIN_BLACKOUT_SKIP_CALLS) {
        s->state = TDMA_ORIGIN_BLACKOUT_COMPLETE;
    } else {
        s->skipped_calls++;
        action = TDMA_ORIGIN_BLACKOUT_SKIP;
    }
    if (s->state >= TDMA_ORIGIN_BLACKOUT_COMPLETE) {
        s->after_version = input->record_version;
        s->end_ticks = input->now_ticks;
        action = TDMA_ORIGIN_BLACKOUT_STOP;
    }
    write_end(trial);
    return action;
}

bool tdma_origin_blackout_get(const tdma_origin_blackout_t *trial,
    tdma_origin_blackout_snapshot_t *out)
{
    if (trial == NULL || out == NULL) return false;
    const uint32_t before = __atomic_load_n(&trial->guard, __ATOMIC_ACQUIRE);
    if (before == 0u || (before & 1u) != 0u) return false;
    *out = trial->snapshot;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return before == __atomic_load_n(&trial->guard, __ATOMIC_ACQUIRE) &&
        out->schema == TDMA_ORIGIN_BLACKOUT_SCHEMA &&
        out->state >= TDMA_ORIGIN_BLACKOUT_COMPLETE;
}
