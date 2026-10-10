from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def test_position_slice_round_trip(tmp_path):
    source = tmp_path / "slice.c"
    source.write_text(r'''
#include <assert.h>
#include "trigger_position_slice.h"
int main(void) {
    trigger_position_slice_t in = { .target_ms_index = 0x10203040u,
        .target_fine_slice_index = 0x0507A1u, .prediction_version = 0x1234u,
        .position_id = 2u, .stage = TRIGGER_POSITION_SLICE_PREVIEW };
    uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE];
    trigger_position_slice_t out;
    assert(trigger_position_slice_encode(&in, wire));
    assert(wire[0] == (TRIGGER_POSITION_SLICE_TAG_PREVIEW | 2u));
    assert(trigger_position_slice_decode(wire, &out));
    assert(out.target_ms_index == in.target_ms_index);
    assert(out.target_fine_slice_index == in.target_fine_slice_index);
    assert(out.prediction_version == in.prediction_version);
    assert(out.position_id == 2u && out.stage == TRIGGER_POSITION_SLICE_PREVIEW);
    in.stage = TRIGGER_POSITION_SLICE_FREEZE;
    assert(trigger_position_slice_encode(&in, wire) &&
           trigger_position_slice_decode(wire, &out) &&
           out.stage == TRIGGER_POSITION_SLICE_FREEZE);
    wire[0] = 3u;  /* a legacy fragment index is never a compact tag */
    assert(!trigger_position_slice_decode(wire, &out));
    in.prediction_version = 0u;
    assert(!trigger_position_slice_encode(&in, wire));
    in.prediction_version = 1u; in.target_fine_slice_index = 0x1000000u;
    assert(!trigger_position_slice_encode(&in, wire));
    return 0;
}
''', encoding="utf-8")
    exe = tmp_path / "slice.exe"
    cc = "D:/Microsoft/mingw64/bin/gcc.exe"
    result = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
        f"-I{ROOT / 'components/sync_trigger/inc'}", str(source), "-o", str(exe)],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
