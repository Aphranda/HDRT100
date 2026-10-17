"""Second position (T2-PROVISIONAL-001): the next position rides in the SAME 16 B slice.

This is the controlled demonstration that the second position exists end to end on the wire
side: one slice carries the authoritative position plus the next position's target, the
existing encoder/decoder round-trips both, and the decoded cache is what the runtime reads
when a FREEZE never arrives. No second format, no second decoder.
"""
import subprocess

from test_vdc_command_owner import compile_executable


CASES = r'''
#include <assert.h>
#include <string.h>
#include "trigger_position_slice.h"

int main(void)
{
    /* One record: the authoritative position plus the next position's target. */
    const trigger_position_slice_t out = {
        .target_ms_index = 12345u,
        .target_fine_slice_index = 77u,
        .prediction_version = 9u,
        .position_id = 1u,
        .stage = TRIGGER_POSITION_SLICE_FREEZE,
        .next_position_id = 2u,
        .next_target_ms_index = 13000u,
    };
    uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE];
    memset(wire, 0xA5, sizeof(wire));                 /* poison: every byte must be written */
    assert(trigger_position_slice_encode(&out, wire));
    assert(sizeof(wire) == 16u);                      /* 10 B legacy + 6 B extension */

    trigger_position_slice_t in;
    assert(trigger_position_slice_decode(wire, &in));
    assert(in.position_id == 1u && in.stage == TRIGGER_POSITION_SLICE_FREEZE);
    assert(in.target_ms_index == 12345u && in.target_fine_slice_index == 77u);
    assert(in.prediction_version == 9u);
    /* The second position survived the SAME slice. */
    assert(in.next_position_id == 2u && in.next_target_ms_index == 13000u);

    /* A record that carries no next position stays compatible (all zero). */
    const trigger_position_slice_t solo = {
        .target_ms_index = 1000u, .prediction_version = 1u, .position_id = 3u,
        .stage = TRIGGER_POSITION_SLICE_PREVIEW,
    };
    assert(trigger_position_slice_encode(&solo, wire));
    assert(trigger_position_slice_decode(wire, &in));
    assert(in.next_position_id == 0u && in.next_target_ms_index == 0u);

    /* The runtime reads exactly this cache when a FREEZE is missing. */
    trigger_position_next_cache_t *cache = trigger_position_next_cache();
    cache->position_id = in.position_id;
    cache->target_ms_index = in.target_ms_index;
    assert(trigger_position_next_cache()->position_id == 3u);
    assert(trigger_position_next_cache()->target_ms_index == 1000u);
    return 0;
}
'''


def test_slice_carries_next_position(tmp_path):
    executable = compile_executable(tmp_path, "next-position", CASES, [])
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
