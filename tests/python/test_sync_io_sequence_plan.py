"""The group-sequence plan arithmetic, on the host.

`sync_io_sequence_plan_build()` turns absolute group deadlines into the two-word stream the
PIO consumes: [code, gap-to-previous-edge - instruction budget]. The whole point is that the
delay of every group after the first is a GAP, not "deadline - now", so the spacing does not
depend on when software happened to fill the FIFO (measured on the bench 2026-10-17: the
"deadline - now" form made every queued trigger late by the time it spent waiting).
"""

import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/sync_io/src/sync_io_sequence_plan.c"
HEADER = ROOT / "components/sync_io/inc"

CASES = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sync_io_sequence_plan.h"

int main(void)
{
    sync_io_sequence_plan_t plan;
    const uint32_t codes[3] = {0x5u, 0xAu, 0x3u};
    /* Absolute local ticks of the three groups; group 0 sits 1000 cycles after "now". */
    const uint64_t deltas[3] = {0u, 2000u, 4000u};   /* offsets from the position slice */
    const uint64_t now = 4000u;

    /* Success: first delay = (deadline - now) - 5, chained = gap - 7. */
    assert(sync_io_sequence_plan_build(&plan, now, now + 1000u, codes, deltas, 3u) ==
        SYNC_IO_SEQUENCE_PLAN_OK);
    assert(plan.count == 3u && plan.word_count == 6u);
    assert(plan.words[0] == 0x5u && plan.words[1] == 1000u - 5u);
    assert(plan.words[2] == 0xAu && plan.words[3] == (2000u - 0u) - 7u);
    assert(plan.words[4] == 0x3u && plan.words[5] == (4000u - 2000u) - 7u);
    assert(plan.first_deadline_tick == now + 1000u);
    assert(plan.last_deadline_tick == now + 1000u + 4000u);   /* base + last offset */

    /* The spacing between consecutive edges equals the requested delta, once the fixed
     * instruction budget is added back: delay + budget == gap for every chained group. */
    for (uint32_t i = 1u; i < 3u; ++i) {
        const uint64_t gap = deltas[i] - deltas[i - 1u];
        assert((uint64_t)plan.words[i * 2u + 1u] + 7u == gap);
    }

    /* Non-monotonic deltas cannot be expressed as positive delays. */
    const uint64_t bad[3] = {0u, 3000u, 2000u};
    assert(sync_io_sequence_plan_build(&plan, now, now + 1000u, codes, bad, 3u) ==
        SYNC_IO_SEQUENCE_PLAN_NON_MONOTONIC);
    /* A base deadline in the past is refused instead of being silently clamped. */
    assert(sync_io_sequence_plan_build(&plan, now, now, codes, deltas, 3u) ==
        SYNC_IO_SEQUENCE_PLAN_DEADLINE_PASSED);
    /* A gap that cannot fit the instruction budget is refused. */
    const uint64_t tight[2] = {0u, 5u};
    assert(sync_io_sequence_plan_build(&plan, now, now + 1000u, codes, tight, 2u) ==
        SYNC_IO_SEQUENCE_PLAN_TOO_CLOSE);
    /* Bounds and empty input. */
    assert(sync_io_sequence_plan_build(&plan, now, now + 1000u, codes, deltas, 0u) ==
        SYNC_IO_SEQUENCE_PLAN_INVALID);
    assert(sync_io_sequence_plan_build(&plan, now, now + 1000u, codes, deltas,
        SYNC_IO_SEQUENCE_PLAN_MAX_GROUPS + 1u) == SYNC_IO_SEQUENCE_PLAN_INVALID);
    /* A first deadline farther than a uint32 delay cannot be represented. */
    assert(sync_io_sequence_plan_build(&plan, 0u, (uint64_t)UINT32_MAX + 100u, codes, deltas, 1u) ==
        SYNC_IO_SEQUENCE_PLAN_TOO_FAR);
    printf("sequence plan cases ok\n");
    return 0;
}
'''


def test_sequence_plan_arithmetic(tmp_path):
    harness = tmp_path / "sequence_plan.c"
    harness.write_text(CASES, encoding="utf-8")
    exe = tmp_path / "sequence_plan.exe"
    build = subprocess.run(
        ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         f"-I{HEADER}", str(harness), str(SOURCE), "-o", str(exe)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    assert "sequence plan cases ok" in run.stdout
