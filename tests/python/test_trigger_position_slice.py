from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def test_position_slice_round_trip(tmp_path):
    source = tmp_path / "slice.c"
    source.write_text(r'''
#include <assert.h>
#include "trigger_position_slice.h"
int main(void) {
    trigger_position_slice_t in = { .position_q = 0x1020304050607080ULL,
        .prediction_version = 0x1234u, .position_id = 2u,
        .stage = TRIGGER_POSITION_SLICE_PREVIEW };
    uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE];
    trigger_position_slice_t out;
    assert(trigger_position_slice_encode(&in, 2000u, wire));
    assert(trigger_position_slice_decode(wire, 2u,
        TRIGGER_POSITION_SLICE_PREVIEW, 2000u, &out));
    assert(out.position_q == in.position_q);
    assert(out.prediction_version == in.prediction_version);
    assert(!trigger_position_slice_decode(wire, 4u,
        TRIGGER_POSITION_SLICE_PREVIEW, 1u, &out));
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
