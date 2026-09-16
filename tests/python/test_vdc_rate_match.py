"""RATE held-window ratios against an independent Fraction oracle."""
from fractions import Fraction
import math
import random
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable

U64 = (1 << 64) - 1


@pytest.fixture(scope="module")
def rate_match_executable(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp("rate-match"), "rate_match", r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include "vdc_feedback_match.h"
int main(void) {
    uint64_t source_base,reference_base,sd,rd;uint32_t w0,w1;
    while(scanf("%"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu32" %"SCNu32,
        &source_base,&reference_base,&sd,&rd,&w0,&w1)==6) {
        vdc_feedback_match_cache_t cache;vdc_feedback_match_peer_t peer;
        vdc_feedback_match_cache_init(&cache);vdc_feedback_match_peer_init(&peer);
        assert(vdc_feedback_match_cache_bind(&cache,1,250000000));
        vdc_feedback_match_sample_t sample={.source_arm_epoch=1,.observer_epoch=1,
            .tick_hz=250000000,.rx_elapsed_cycles=source_base};
        assert(vdc_feedback_model_cache_put(&cache,0,3,2,reference_base,reference_base+w0,0));
        assert(vdc_feedback_rate_update(&cache,&peer,&sample,2)==VDC_FEEDBACK_MATCH_BASELINED);
        sample.measurement_sequence=1;sample.rx_elapsed_cycles+=sd;
        assert(vdc_feedback_model_cache_put(&cache,1,3,4,reference_base+rd,reference_base+rd+w1,0));
        const unsigned result=vdc_feedback_rate_update(&cache,&peer,&sample,2);
        printf("%u %u %"PRId64" %"PRId64" %"PRIu32"\n",result,peer.snapshot.has_pair,
            peer.snapshot.raw_ppb_lo,peer.snapshot.raw_ppb_hi,peer.previous.measurement_sequence);
    }
    return 0;
}
''', [ROOT / "components/vdc_dpll_manager/src/vdc_feedback_match.c"])


def test_window_bounds_sign_and_large_origins(rate_match_executable):
    rng = random.Random(16091631)
    cases = [(0, 0, sd, rd, w0, w1) for sd, rd, w0, w1 in [
        (1_000_001_000, 1_000_000_253, 253, 253),
        (999_999_000, 1_000_000_253, 253, 253),
        (1_000_000_000, 1_000_000_252, 253, 253),
        (1_999_999_999, 1_999_999_999, 0, 0),
        (2_000_000_000, 1_000_000_000, 0, 0),
        (1_000_000_000, 2_000_000_000, 0, 1),
        (1, 1_000_000_000, 0, 0), (1_000_000_000, 500, 500, 0)]]
    for _ in range(1200):
        rd = rng.randrange(1_000_000, 2_010_000_000)
        sd = max(2, rd + rng.randrange(-100000, 100001))
        w0, w1 = rng.randrange(3000), rng.randrange(3000)
        sb = rng.choice([0, 1 << 52, U64 - sd - 1])
        rb = rng.choice([0, 1 << 54, U64 - rd - w1])
        cases.append((sb, rb, sd, rd, w0, w1))
    result = subprocess.run([str(rate_match_executable)], input="\n".join(
        " ".join(map(str, c)) for c in cases) + "\n", capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    rows = [tuple(map(int, row.split())) for row in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    for case, actual in zip(cases, rows, strict=True):
        _, _, sd, rd, w0, w1 = case
        if sd <= 1 or rd <= w0:
            expected = (0, 0, 0, 0, 0)
        elif sd + 1 > 2_000_000_000 or rd + w1 > 2_000_000_000:
            expected = (5, 0, 0, 0, 1)
        elif rd - w0 < 1_000_000_000:
            expected = (7, 0, 0, 0, 0)
        else:
            lo = math.floor((Fraction(sd - 1, rd + w1) - 1) * 1_000_000_000)
            hi = math.ceil((Fraction(sd + 1, rd - w0) - 1) * 1_000_000_000)
            expected = (6, 1, lo, hi, 1)
        assert actual == expected, (case, actual, expected)
