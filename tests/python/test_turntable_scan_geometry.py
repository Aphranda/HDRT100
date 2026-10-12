"""Scan geometry validation: a 360-degree/1-degree scan is 361 positions, and a
scan that cannot be run must be refused at STOP time instead of being started.

This is the fail-closed half of the scan-parameterisation item
(docs/trigger/t2/TRIGGER_T2_TODO.md): the position count, the whole pulses per
STEP and the span the per-position groups need are derived from the scan numbers,
and every unusable combination has its own refusal reason.
"""
import subprocess

from test_trigger_grid_preview import compile_grid

MAIN = r'''
#include <assert.h>
#include <string.h>
#include "trigger_turntable_predictor.h"

/* The step is an argument, not a config field: a new config field breaks every
 * partial initializer under -Werror -Wextra, so it is carried separately. */
static int32_t g_step;

static trigger_turntable_predictor_config_t scan(int32_t start, int32_t stop,
                                                int32_t step, uint32_t ppd)
{
    trigger_turntable_predictor_config_t c = {0};
    c.pulses_per_degree = ppd;
    c.start_angle_mdeg = start;
    c.terminal_angle_mdeg = stop;
    c.pre_roll_mdeg = 1000u;
    c.post_roll_mdeg = 1000u;
    c.threshold_per_mille = 50u;
    g_step = step;
    return c;
}

int main(void)
{
    trigger_turntable_scan_geometry_t g = {0};

    /* The product target: 0..360 degrees at 1 degree, 100 pulses/degree. */
    trigger_turntable_predictor_config_t c = scan(0, 360000, 1000, 100u);
    assert(trigger_turntable_scan_validate(&c, g_step, 10u, &g) == TRIGGER_TURNTABLE_SCAN_OK);
    assert(g.positions == 361u);
    assert(g.step_pulses == 100u);
    assert(g.total_pulses == 36000u);

    /* Nothing to check yet is not a refusal. */
    assert(trigger_turntable_scan_validate(&c, g_step, 0u, &g) == TRIGGER_TURNTABLE_SCAN_OK);

    /* A zero step or a zero pulses-per-degree is an argument error. */
    trigger_turntable_predictor_config_t bad = scan(0, 360000, 0, 100u);
    assert(trigger_turntable_scan_validate(&bad, g_step, 0u, &g) ==
           TRIGGER_TURNTABLE_SCAN_ARGUMENT);
    bad = scan(0, 360000, 1000, 0u);
    assert(trigger_turntable_scan_validate(&bad, g_step, 0u, &g) ==
           TRIGGER_TURNTABLE_SCAN_ARGUMENT);

    /* Backwards scan. */
    bad = scan(360000, 0, 1000, 100u);
    assert(trigger_turntable_scan_validate(&bad, g_step, 0u, &g) ==
           TRIGGER_TURNTABLE_SCAN_BACKWARD);

    /* A step that does not divide the span would leave a partial last position. */
    bad = scan(0, 360000, 700, 100u);
    assert(trigger_turntable_scan_validate(&bad, g_step, 0u, &g) ==
           TRIGGER_TURNTABLE_SCAN_STEP_NOT_DIVISIBLE);

    /* 1.5 degrees at 3 pulses/degree is 4.5 pulses: not a whole STEP. */
    bad = scan(0, 360000, 1500, 3u);
    assert(trigger_turntable_scan_validate(&bad, g_step, 0u, &g) ==
           TRIGGER_TURNTABLE_SCAN_PULSES_NOT_INTEGER);

    /* A STEP that cannot hold the position's groups is refused, and the same
     * scan passes once the groups fit. */
    c = scan(0, 360000, 1000, 100u);
    assert(trigger_turntable_scan_validate(&c, g_step, 100u, &g) == TRIGGER_TURNTABLE_SCAN_OK);
    assert(trigger_turntable_scan_validate(&c, g_step, 101u, &g) ==
           TRIGGER_TURNTABLE_SCAN_SPAN_TOO_SHORT);

    /* An absurd grid is refused instead of walked. */
    bad = scan(0, 3600000, 100, 100u);               /* 36001 positions */
    assert(trigger_turntable_scan_validate(&bad, g_step, 0u, &g) ==
           TRIGGER_TURNTABLE_SCAN_TOO_MANY_POSITIONS);

    /* A refusal clears the outputs: a caller cannot act on a half-derived scan. */
    g.positions = 7u;
    bad = scan(0, 360000, 700, 100u);
    assert(trigger_turntable_scan_validate(&bad, g_step, 0u, &g) ==
           TRIGGER_TURNTABLE_SCAN_STEP_NOT_DIVISIBLE);
    assert(g.positions == 0u && g.step_pulses == 0u && g.total_pulses == 0u);

    /* Fractional start angles work as long as the arithmetic stays whole. */
    c = scan(-2500, 357500, 1000, 100u);             /* 361 positions from -2.5 deg */
    assert(trigger_turntable_scan_validate(&c, g_step, 10u, &g) == TRIGGER_TURNTABLE_SCAN_OK);
    assert(g.positions == 361u);
    return 0;
}
'''


def test_scan_geometry_is_derived_and_refused_when_unusable(tmp_path):
    executable = compile_grid(tmp_path, "t2-scan-geometry", MAIN)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
