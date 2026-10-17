#ifndef TRIGGER_SEQUENCE_TIMING_H
#define TRIGGER_SEQUENCE_TIMING_H
#include <stdbool.h>
#include <stdint.h>

/* Per-action timing of the multi-sync sequence. Every value is an independent configuration
 * item; none of them changes the executor, only where the sequence places its edges.
 *
 *   action = switch A (settle A) + switch B (settle B) on the same preset edge
 *          -> wait max(A,B)
 *          -> lead C -> TRIG pulse D -> analyzer response E -> READY
 *   READY IS the next action's preset edge (there is no extra wait window).
 *
 * The sequence itself (which channel/polarization each action selects, and how many times the
 * whole table repeats) is derived here as well, so a configuration change never needs code. */
#define TRIGGER_SEQUENCE_TIMING_MAX_US 1000000u
#define TRIGGER_SEQUENCE_TIMING_MAX_CHANNELS 16u
#define TRIGGER_SEQUENCE_TIMING_MAX_POLARIZATIONS 8u
#define TRIGGER_SEQUENCE_TIMING_MAX_GRID_NS 100000u

typedef enum {
    TRIGGER_SEQUENCE_TIMING_OK = 0,
    TRIGGER_SEQUENCE_TIMING_INVALID,
    TRIGGER_SEQUENCE_TIMING_ZERO,
    TRIGGER_SEQUENCE_TIMING_TOO_LARGE,
    TRIGGER_SEQUENCE_TIMING_BAD_COUNT,
    TRIGGER_SEQUENCE_TIMING_BAD_GRID
} trigger_sequence_timing_error_t;

typedef struct {
    uint32_t settle_a_us;   /* A: SWITCH_A channel-change settle */
    uint32_t settle_b_us;   /* B: SWITCH_B polarization-change settle */
    uint32_t lead_us;       /* C: settle end -> TRIG */
    uint32_t trig_us;       /* D: GATEWAY TRIG pulse width */
    uint32_t vna_us;        /* E: analyzer response (about 8 ms on the bench) */
    uint32_t move_us;       /* G: turntable move between positions (measured) */
    uint32_t channels;      /* N通道 */
    uint32_t polarizations; /* N极化 */
    uint32_t grid_ns;       /* grid slice, configurable (500 ns was only a drawing preset) */
} trigger_sequence_timing_t;

void trigger_sequence_timing_reset(void);
bool trigger_sequence_timing_defaults(trigger_sequence_timing_t *out);
bool trigger_sequence_timing_get(trigger_sequence_timing_t *out);
trigger_sequence_timing_error_t trigger_sequence_timing_set(const trigger_sequence_timing_t *in);

/* Derived quantities; they are the numbers the group table needs. */
uint32_t trigger_sequence_timing_action_us(const trigger_sequence_timing_t *t);
uint32_t trigger_sequence_timing_actions(const trigger_sequence_timing_t *t);
uint32_t trigger_sequence_timing_position_us(const trigger_sequence_timing_t *t);
uint32_t trigger_sequence_timing_channel_of(uint32_t action, uint32_t channels,
                                            uint32_t polarizations);
uint32_t trigger_sequence_timing_polarization_of(uint32_t action, uint32_t polarizations);

#endif
