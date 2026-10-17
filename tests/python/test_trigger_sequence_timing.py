"""Per-action timing configuration: every number of the multi-sync diagram is settable, and the
action/position periods are derived from them (max(A,B)+C+D+E, no extra wait window)."""

import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/sync_trigger/src/trigger_sequence_timing.c"
HEADER = ROOT / "components/sync_trigger/inc"

CASES = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "trigger_sequence_timing.h"

int main(void)
{
    trigger_sequence_timing_t t;
    assert(trigger_sequence_timing_defaults(&t));
    /* The analyzer answers in about 8 ms, and that dominates the action period. */
    assert(t.vna_us == 8000u);
    assert(trigger_sequence_timing_actions(&t) == 16u);          /* 8 channels x 2 polarizations */
    assert(trigger_sequence_timing_action_us(&t) == 8003u);      /* max(1,1)+1+1+8000 */
    assert(trigger_sequence_timing_position_us(&t) == 16u * 8003u + 200000u);

    /* Each item is independent: change only B and only the max(A,B) term moves. */
    trigger_sequence_timing_t slow_b = t;
    slow_b.settle_b_us = 500u;
    assert(trigger_sequence_timing_action_us(&slow_b) == 500u + 1u + 1u + 8000u);
    /* ... and only E moves the rest. */
    trigger_sequence_timing_t slow_vna = t;
    slow_vna.vna_us = 9000u;
    assert(trigger_sequence_timing_action_us(&slow_vna) == 1u + 1u + 1u + 9000u);

    /* Counts and grid are configuration too. */
    trigger_sequence_timing_t wide = t;
    wide.channels = 4u; wide.polarizations = 3u;
    assert(trigger_sequence_timing_set(&wide) == TRIGGER_SEQUENCE_TIMING_OK);
    assert(trigger_sequence_timing_actions(&wide) == 12u);
    assert(trigger_sequence_timing_channel_of(0u, 4u, 3u) == 1u);
    assert(trigger_sequence_timing_channel_of(3u, 4u, 3u) == 2u);
    assert(trigger_sequence_timing_polarization_of(3u, 3u) == 0u);
    assert(trigger_sequence_timing_polarization_of(4u, 3u) == 1u);

    /* Rejections: a zero settle/lead/trig/vna cannot be expressed as a preset edge, oversize
     * and empty counts are refused instead of being silently clamped. */
    trigger_sequence_timing_t bad = t;
    bad.settle_a_us = 0u;
    assert(trigger_sequence_timing_set(&bad) == TRIGGER_SEQUENCE_TIMING_ZERO);
    bad = t; bad.vna_us = 0u;
    assert(trigger_sequence_timing_set(&bad) == TRIGGER_SEQUENCE_TIMING_ZERO);
    bad = t; bad.vna_us = TRIGGER_SEQUENCE_TIMING_MAX_US + 1u;
    assert(trigger_sequence_timing_set(&bad) == TRIGGER_SEQUENCE_TIMING_TOO_LARGE);
    bad = t; bad.channels = 0u;
    assert(trigger_sequence_timing_set(&bad) == TRIGGER_SEQUENCE_TIMING_BAD_COUNT);
    bad = t; bad.polarizations = TRIGGER_SEQUENCE_TIMING_MAX_POLARIZATIONS + 1u;
    assert(trigger_sequence_timing_set(&bad) == TRIGGER_SEQUENCE_TIMING_BAD_COUNT);
    bad = t; bad.grid_ns = 0u;
    assert(trigger_sequence_timing_set(&bad) == TRIGGER_SEQUENCE_TIMING_BAD_GRID);

    /* A rejected set leaves the installed values untouched. */
    assert(trigger_sequence_timing_get(&t));
    assert(t.channels == 4u && t.polarizations == 3u);
    assert(t.vna_us == 8000u);

    /* The sequence itself is configuration: an explicit code pair overrides the derived rule,
     * and the table can repeat. */
    {
        uint32_t ch=0u, pol=0u;
        assert(trigger_sequence_code_get(3u,&ch,&pol) && ch==2u && pol==0u);
        assert(trigger_sequence_code_set(3u,3u,1u) == TRIGGER_SEQUENCE_CODE_OK);
        assert(trigger_sequence_code_get(3u,&ch,&pol) && ch==3u && pol==1u);
        assert(trigger_sequence_code_set(3u,0u,0u) == TRIGGER_SEQUENCE_CODE_BAD_CHANNEL);
        assert(trigger_sequence_code_set(3u,1u,3u) == TRIGGER_SEQUENCE_CODE_BAD_POLARIZATION);
        assert(trigger_sequence_code_set(TRIGGER_SEQUENCE_TIMING_MAX_ACTIONS,1u,0u) ==
            TRIGGER_SEQUENCE_CODE_BAD_ACTION);
        assert(trigger_sequence_code_repeat() == 1u);
        assert(trigger_sequence_code_repeat_set(3u) == TRIGGER_SEQUENCE_CODE_OK);
        assert(trigger_sequence_code_repeat() == 3u);
        assert(trigger_sequence_code_repeat_set(0u) == TRIGGER_SEQUENCE_CODE_BAD_REPEAT);
        assert(trigger_sequence_timing_position_actions_total() == 3u * 12u);
        assert(trigger_sequence_code_clear());
        assert(trigger_sequence_code_get(3u,&ch,&pol) && ch==2u && pol==0u);
    }

    trigger_sequence_timing_reset();
    assert(trigger_sequence_timing_get(&t) && t.channels == 8u && t.settle_a_us == 1u);
    assert(trigger_sequence_code_repeat() == 1u);
    {
        uint32_t ch=0u, pol=0u;
        assert(trigger_sequence_code_get(0u,&ch,&pol) && ch==1u && pol==0u);
    }
    printf("sequence timing cases ok\n");
    return 0;
}
'''


def test_sequence_timing(tmp_path):
    harness = tmp_path / "sequence_timing.c"
    harness.write_text(CASES, encoding="utf-8")
    exe = tmp_path / "sequence_timing.exe"
    build = subprocess.run(
        ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         f"-I{HEADER}", str(harness), str(SOURCE), "-o", str(exe)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    assert "sequence timing cases ok" in run.stdout
