"""Rolling prediction window: T2 predicts only the next one or two positions.

The 2026-10-12 decisions recorded in docs/trigger/t2/TRIGGER_T2_TODO.md are:
  * a 360-degree/1-degree scan is 361 positions and NOTHING may be predicted while
    the scan has barely started (begin() must not enumerate the whole scan);
  * a prediction more than two positions ahead is not accurate - the rate estimate
    is a single sample, so its error grows with the horizon;
  * a window entry's target/threshold is computed when the position ENTERS the
    window, from the count observed then;
  * without a usable rate the time prediction is unavailable (fail-closed).

The window API is `static inline` in components/sync_trigger/inc/
trigger_turntable_predictor.h, so the test includes the real header and drives it.
"""
import subprocess

from test_trigger_grid_preview import compile_grid

MAIN = r'''
#include <assert.h>
#include <string.h>
#include "trigger_turntable_predictor.h"

static trigger_turntable_predictor_config_t scan(void)
{
    /* 0..360 degrees at 1 degree: 361 positions, 100 pulses/degree, 50 per mille
     * threshold, 1 degree pre-roll (base angle = -1 degree). */
    trigger_turntable_predictor_config_t c = {0};
    c.pulses_per_degree = 100u;
    c.start_angle_mdeg = 0;
    c.terminal_angle_mdeg = 360000;
    c.step_mdeg = 1000;
    c.pre_roll_mdeg = 1000u;
    c.post_roll_mdeg = 1000u;
    c.threshold_per_mille = 50u;
    return c;
}

int main(void)
{
    const trigger_turntable_predictor_config_t c = scan();
    assert(TRIGGER_TURNTABLE_WINDOW_MAX == 2u);          /* accuracy bound */
    assert(trigger_turntable_window_position_count(&c) == 361u);

    /* begin() enters at most two positions out of 361: the rest of the scan is
     * NOT predicted up front. */
    trigger_turntable_predictor_snapshot_t s = {0};
    s.active = 1u;
    s.configured = 1u;
    assert(trigger_turntable_window_begin(&s, &c, 0u));
    assert(s.window_entered_count == 2u);
    assert(s.window_next_index == 2u);                   /* next ordinal to enter */
    assert(s.window[0].valid && s.window[0].absolute_index == 0u);
    assert(s.window[1].valid && s.window[1].absolute_index == 1u);
    /* Targets are exact (no rate needed): base = start - pre_roll = -1 degree. */
    assert(s.window[0].target_count == 100u && s.window[0].threshold_count == 5u);
    assert(s.window[1].target_count == 200u && s.window[1].threshold_count == 10u);

    /* Not ready to time anything yet: no rate has been observed (fail-closed). */
    assert(!trigger_turntable_window_time_ready(&s));

    /* Crossing the oldest entry's threshold announces it and rolls the window; the
     * newly entered position is computed from the count observed NOW (5), so its
     * target is 305, not the 300 a begin-time extrapolation would give. */
    uint32_t mask = 0u;
    assert(trigger_turntable_window_advance(&s, &c, 5u, &mask) == TRIGGER_TURNTABLE_OK);
    assert(mask == 0x1u);                                /* slot 0 announced */
    assert(s.window_recomputed_count == 1u);
    assert(s.window_next_index == 3u && s.window_entered_count == 3u);
    assert(s.window[0].absolute_index == 1u);            /* the old slot 1 */
    assert(s.window[1].absolute_index == 2u && s.window[1].valid);
    assert(s.window[1].target_count == 305u);            /* 5 + (3 deg * 100) */
    assert(s.window[1].threshold_count == 20u);          /* 305 - 285 */

    /* The mask is per window slot, and an already announced entry is not
     * re-announced while it is still the oldest. */
    mask = 0xFu;
    assert(trigger_turntable_window_advance(&s, &c, 6u, &mask) == TRIGGER_TURNTABLE_OK);
    assert(mask == 0u);

    /* A backward count is a hard error and changes nothing. */
    assert(trigger_turntable_window_advance(&s, &c, 1u, &mask) ==
           TRIGGER_TURNTABLE_BACKWARD);

    /* A rate makes the time prediction available again. */
    s.rate_valid = 1u;
    s.pulses_per_sync_q16 = 1u;
    assert(trigger_turntable_window_time_ready(&s));

    /* Walk the whole scan to the end: the window must never fabricate a position
     * past the terminal one (ordinal 360), and it must stop on its own. */
    uint64_t count = 6u;
    for (uint32_t i = 0u; i < 400u; ++i) {
        count += 100000u;                                /* cross every threshold */
        assert(trigger_turntable_window_advance(&s, &c, count, &mask) ==
               TRIGGER_TURNTABLE_OK);
        for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot)
            if (s.window[slot].valid)
                assert(s.window[slot].absolute_index < 361u);
    }
    assert(s.window_entered_count <= 361u);
    assert(!s.window[1].valid || s.window[1].absolute_index < 361u);
    /* Entering past the scan end is refused outright. */
    assert(!trigger_turntable_window_enter(&s, &c, 0u, 361u, count));
    assert(!trigger_turntable_window_enter(&s, &c, 0u, 4000u, count));
    return 0;
}
'''


def test_rolling_window_predicts_only_the_next_positions(tmp_path):
    executable = compile_grid(tmp_path, "t2-rolling-window", MAIN)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
