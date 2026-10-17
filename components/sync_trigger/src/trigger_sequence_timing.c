/*
 * Per-action timing of the multi-sync sequence, as the diagram describes it:
 *
 *   SWITCH_A switch ─ A ─┬─ (channel change settle)
 *   SWITCH_B switch ─ B ─┤   (polarization change settle, independent of A)
 *                        └─ C ─ TRIG(D) ─ E ─ READY↑ = next action's switch
 *
 * Every number is a separate configuration item because the two switches need not settle in
 * the same time and the analyzer answer (E) dominates the action period. G is the turntable
 * move between positions and contains no action at all.
 *
 * Nothing here touches the executor: these values only decide where the sequence places its
 * edges, so changing them never needs new firmware.
 */
#include "trigger_sequence_timing.h"

#include <string.h>

static const trigger_sequence_timing_t s_default = {
    .settle_a_us = 1u,
    .settle_b_us = 1u,
    .lead_us = 1u,
    .trig_us = 1u,
    .vna_us = 8000u,      /* the analyzer answers in about 8 ms */
    .move_us = 200000u,   /* assumed turntable move until a measurement replaces it */
    .channels = 8u,
    .polarizations = 2u,
    .grid_ns = 500u,
};

static trigger_sequence_timing_t s_timing;
static bool s_loaded;
/* Explicit code pair per action; 0 means "derive it from the rule". */
static uint16_t s_code_a[TRIGGER_SEQUENCE_TIMING_MAX_ACTIONS];
static uint16_t s_code_b[TRIGGER_SEQUENCE_TIMING_MAX_ACTIONS];
static uint32_t s_repeat = 1u;

static bool loaded(void)
{
    if (!s_loaded) {
        s_timing = s_default;
        s_loaded = true;
    }
    return true;
}

void trigger_sequence_timing_reset(void)
{
    s_timing = s_default;
    s_loaded = true;
    (void)trigger_sequence_code_clear();
    s_repeat = 1u;
}

bool trigger_sequence_timing_defaults(trigger_sequence_timing_t *out)
{
    if (out == NULL) return false;
    *out = s_default;
    return true;
}

bool trigger_sequence_timing_get(trigger_sequence_timing_t *out)
{
    if (out == NULL) return false;
    (void)loaded();
    *out = s_timing;
    return true;
}

trigger_sequence_timing_error_t trigger_sequence_timing_set(
    const trigger_sequence_timing_t *in)
{
    if (in == NULL) return TRIGGER_SEQUENCE_TIMING_INVALID;
    /* A settle of zero would place the switch and the trigger on the same edge; the schedule
     * could not express that without lying about the settle time. */
    if (in->settle_a_us == 0u || in->settle_b_us == 0u || in->lead_us == 0u ||
        in->trig_us == 0u || in->vna_us == 0u)
        return TRIGGER_SEQUENCE_TIMING_ZERO;
    if (in->settle_a_us > TRIGGER_SEQUENCE_TIMING_MAX_US ||
        in->settle_b_us > TRIGGER_SEQUENCE_TIMING_MAX_US ||
        in->lead_us > TRIGGER_SEQUENCE_TIMING_MAX_US ||
        in->trig_us > TRIGGER_SEQUENCE_TIMING_MAX_US ||
        in->vna_us > TRIGGER_SEQUENCE_TIMING_MAX_US ||
        in->move_us > TRIGGER_SEQUENCE_TIMING_MAX_US)
        return TRIGGER_SEQUENCE_TIMING_TOO_LARGE;
    if (in->channels == 0u || in->channels > TRIGGER_SEQUENCE_TIMING_MAX_CHANNELS ||
        in->polarizations == 0u || in->polarizations > TRIGGER_SEQUENCE_TIMING_MAX_POLARIZATIONS)
        return TRIGGER_SEQUENCE_TIMING_BAD_COUNT;
    if (in->grid_ns == 0u || in->grid_ns > TRIGGER_SEQUENCE_TIMING_MAX_GRID_NS)
        return TRIGGER_SEQUENCE_TIMING_BAD_GRID;
    s_timing = *in;
    s_loaded = true;
    return TRIGGER_SEQUENCE_TIMING_OK;
}

uint32_t trigger_sequence_timing_action_us(const trigger_sequence_timing_t *t)
{
    if (t == NULL) return 0u;
    /* The two switches change on the same preset edge, so the action starts moving only when
     * the slower one has settled: the action period is max(A,B) + C + D + E. There is no extra
     * wait window - READY IS the next action's preset edge. */
    const uint32_t settle = t->settle_a_us > t->settle_b_us ? t->settle_a_us : t->settle_b_us;
    return settle + t->lead_us + t->trig_us + t->vna_us;
}

uint32_t trigger_sequence_timing_actions(const trigger_sequence_timing_t *t)
{
    if (t == NULL) return 0u;
    return t->channels * t->polarizations;
}

uint32_t trigger_sequence_timing_position_us(const trigger_sequence_timing_t *t)
{
    if (t == NULL) return 0u;
    return trigger_sequence_timing_actions(t) * trigger_sequence_timing_action_us(t) +
        t->move_us;
}

uint32_t trigger_sequence_timing_channel_of(uint32_t action, uint32_t channels,
                                            uint32_t polarizations)
{
    (void)polarizations;
    if (channels == 0u) return 0u;
    /* H then V for every channel, channel advancing every `polarizations` actions. */
    return action / (polarizations ? polarizations : 1u) + 1u;
}

uint32_t trigger_sequence_timing_polarization_of(uint32_t action, uint32_t polarizations)
{
    if (polarizations == 0u) return 0u;
    return action % polarizations;
}

trigger_sequence_code_error_t trigger_sequence_code_set(uint32_t action, uint32_t channel,
                                                        uint32_t polarization)
{
    (void)loaded();
    if (action >= TRIGGER_SEQUENCE_TIMING_MAX_ACTIONS)
        return TRIGGER_SEQUENCE_CODE_BAD_ACTION;
    if (channel == 0u || channel > s_timing.channels)
        return TRIGGER_SEQUENCE_CODE_BAD_CHANNEL;
    if (polarization >= s_timing.polarizations)
        return TRIGGER_SEQUENCE_CODE_BAD_POLARIZATION;
    s_code_a[action] = (uint16_t)channel;
    s_code_b[action] = (uint16_t)polarization;
    return TRIGGER_SEQUENCE_CODE_OK;
}

bool trigger_sequence_code_get(uint32_t action, uint32_t *channel, uint32_t *polarization)
{
    (void)loaded();
    if (action >= TRIGGER_SEQUENCE_TIMING_MAX_ACTIONS) return false;
    uint32_t a = s_code_a[action];
    uint32_t b = s_code_b[action];
    if (a == 0u) {
        a = trigger_sequence_timing_channel_of(action, s_timing.channels,
                                               s_timing.polarizations);
        b = trigger_sequence_timing_polarization_of(action, s_timing.polarizations);
    }
    if (channel) *channel = a;
    if (polarization) *polarization = b;
    return true;
}

bool trigger_sequence_code_clear(void)
{
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_TIMING_MAX_ACTIONS; ++i) {
        s_code_a[i] = 0u;
        s_code_b[i] = 0u;
    }
    return true;
}

trigger_sequence_code_error_t trigger_sequence_code_repeat_set(uint32_t repeat)
{
    if (repeat == 0u || repeat > TRIGGER_SEQUENCE_TIMING_MAX_REPEAT)
        return TRIGGER_SEQUENCE_CODE_BAD_REPEAT;
    s_repeat = repeat;
    return TRIGGER_SEQUENCE_CODE_OK;
}

uint32_t trigger_sequence_code_repeat(void)
{
    (void)loaded();
    return s_repeat;
}

uint32_t trigger_sequence_timing_position_actions_total(void)
{
    (void)loaded();
    return trigger_sequence_timing_actions(&s_timing) * s_repeat;
}
