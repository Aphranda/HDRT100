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
    c.pre_roll_mdeg = 1000u;
    c.post_roll_mdeg = 1000u;
    c.threshold_per_mille = 50u;
    return c;
}

int main(void)
{
    const trigger_turntable_predictor_config_t c = scan();
    const int32_t step = 1000;                           /* 1 degree */
    assert(TRIGGER_TURNTABLE_WINDOW_MAX == 2u);          /* accuracy bound */
    assert(trigger_turntable_window_position_count(&c, step) == 361u);

    /* begin() enters at most two positions out of 361: the rest of the scan is
     * NOT predicted up front. */
    trigger_turntable_predictor_snapshot_t s = {0};
    s.active = 1u;
    s.configured = 1u;
    trigger_turntable_window_configure(&s, step);
    assert(s.window_step_mdeg == 1000);
    assert(trigger_turntable_window_begin(&s, &c, 0u));
    assert(s.window_entered_count == 2u);
    assert(s.window_next_index == 2u);                   /* next ordinal to enter */
    assert(s.window[0].valid && s.window[0].absolute_index == 0u);
    assert(s.window[1].valid && s.window[1].absolute_index == 1u);
    /* Targets are exact (no rate needed): base = start - pre_roll = -1 degree. */
    /* lead = 95% of one 100-pulse STEP, so every position is frozen equally
     * early regardless of where it sits in the scan. */
    assert(s.window[0].target_count == 100u && s.window[0].threshold_count == 5u);
    assert(s.window[1].target_count == 200u && s.window[1].threshold_count == 105u);

    /* Not ready to time anything yet: no rate has been observed (fail-closed). */
    assert(!trigger_turntable_window_time_ready(&s));

    /* Crossing the oldest entry's threshold announces it and rolls the window. The
     * newly entered position's target is ABSOLUTE (scan origin + angle delta), so
     * it is 300 whatever count the window happened to roll at - a target re-based
     * on the entry moment would have said 305 and been wrong by the distance
     * already travelled. */
    uint32_t mask = 0u;
    assert(trigger_turntable_window_advance(&s, &c, 5u, &mask) == TRIGGER_TURNTABLE_OK);
    assert(mask == 0x1u);                                /* slot 0 announced */
    assert(s.window_recomputed_count == 1u);
    assert(s.window_next_index == 3u && s.window_entered_count == 3u);
    assert(s.window[0].absolute_index == 1u);            /* the old slot 1 */
    assert(s.window[1].absolute_index == 2u && s.window[1].valid);
    assert(s.window[1].target_count == 300u);            /* scan origin + 3 deg */
    assert(s.window[1].threshold_count == 205u);         /* 300 - 95 (one STEP) */
    assert(s.origin_count == 0u);                        /* anchored at begin */
    assert(s.window_late_entry_count == 0u);             /* entered in time */

    /* The same position entered at a very different moment gets the SAME target:
     * the scan decides it, not the entry moment. */
    trigger_turntable_predictor_snapshot_t late = {0};
    late.active = 1u;
    trigger_turntable_window_configure(&late, step);
    assert(trigger_turntable_window_begin(&late, &c, 0u));
    assert(trigger_turntable_window_advance(&late, &c, 200u, &mask) ==
           TRIGGER_TURNTABLE_OK);
    /* Both early entries were already announced at count 200, so the window
     * drained to ordinal 2 - whose target is still the absolute 300 (a target
     * re-based on the entry moment would have said 500). */
    assert(late.window[0].absolute_index == 2u);
    assert(late.window[0].target_count == 300u);

    /* A position whose target is already behind the counter is DUE NOW, not a
     * wrapped threshold: entering index 5 (target 600) at count 1000 clamps. */
    trigger_turntable_predictor_snapshot_t behind = {0};
    behind.active = 1u;
    behind.origin_count = 0u;
    behind.window_step_mdeg = step;
    assert(trigger_turntable_window_enter(&behind, &c, 0u, 5u, 1000u));
    assert(behind.window[0].target_count == 600u);
    assert(behind.window[0].threshold_count == 1000u);   /* due immediately */
    assert(behind.window_late_entry_count == 1u);

    /* The mask is per window slot, and an already announced entry is not
     * re-announced while it is still the oldest. */
    mask = 0xFu;
    assert(trigger_turntable_window_advance(&s, &c, 6u, &mask) == TRIGGER_TURNTABLE_OK);
    assert(mask == 0u);

    /* A backward count is a hard error and changes nothing. */
    assert(trigger_turntable_window_advance(&s, &c, 1u, &mask) ==
           TRIGGER_TURNTABLE_BACKWARD);

    /* Announcing a position without a usable rate loses its time prediction, and
     * that loss is counted (the FREEZE itself still goes out). */
    assert(s.window_rate_stale_count == 1u);
    {
        trigger_turntable_predictor_snapshot_t rated = {0};
        rated.active = 1u;
        rated.rate_valid = 1u;
        rated.pulses_per_sync_q16 = 1u;
        trigger_turntable_window_configure(&rated, step);
        assert(trigger_turntable_window_begin(&rated, &c, 0u));
        assert(trigger_turntable_window_advance(&rated, &c, 5u, &mask) ==
               TRIGGER_TURNTABLE_OK);
        assert(mask == 0x1u);
        assert(rated.window_rate_stale_count == 0u);
    }

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
