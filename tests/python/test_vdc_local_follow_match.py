"""Local/NO1 full-interval ratio against an independent rational oracle."""
from fractions import Fraction
import math
import random
import subprocess

from test_vdc_command_owner import ROOT, compile_executable


def test_local_ratio_bounds_sign_window_and_rebase(tmp_path):
    executable = compile_executable(tmp_path, "local_ratio", HARNESS,
        [ROOT / "components/vdc_dpll_manager/src/vdc_feedback_match.c"])
    rng = random.Random(916417)
    cases = [(1_000_001_000, 1_000_000_000, 3, 5, 7, 11),
             (999_999_000, 1_000_000_000, 3, 5, 7, 11),
             (500_000_000, 500_000_000, 2, 2, 2, 2)]
    cases += [(rng.randrange(900_000_000, 1_300_000_000), 1_100_000_000,
               *(rng.randrange(0, 2000) for _ in range(4))) for _ in range(150)]
    result = subprocess.run([str(executable)], input="\n".join(" ".join(map(str, c)) for c in cases),
                            text=True, capture_output=True)
    (tmp_path / "oracle.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stderr
    rows = result.stdout.splitlines()
    assert len(rows) == len(cases)
    for case, line in zip(cases, rows, strict=True):
        sd, rd, lw0, lw1, rw0, rw1 = case
        actual = tuple(map(int, line.split()))
        if rd - rw0 < 1_000_000_000:
            assert actual == (7, 0, 0), (case, actual)
        else:
            expected = (6, math.floor(Fraction((sd - lw0) * 10**9, rd + rw1)) - 10**9,
                        math.ceil(Fraction((sd + lw1) * 10**9, rd - rw0)) - 10**9)
            assert actual == expected, (case, actual, expected)


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "vdc_local_follow.h"
static void boundaries(void) {
    vdc_feedback_match_peer_t p={0};
    vdc_feedback_match_lifetime_t life={.source_arm_epoch=10,.observer_epoch=20,.tick_hz=250000000};
    vdc_feedback_match_pair_t a={.rx_elapsed_cycles=100,.reference_tx_lo=1000,.reference_tx_hi=1005,
        .source_model_token=3,.reference_identity_crc32=4,.rx_width_ns=2,.measurement_sequence=10};
    assert(vdc_feedback_local_follow_update(&p,20,30,&life,&a)==VDC_FEEDBACK_MATCH_BASELINED);
    const vdc_feedback_match_peer_t old=p;
    assert(vdc_feedback_local_follow_update(&p,20,30,&life,&a)==VDC_FEEDBACK_MATCH_DUPLICATE);
    a.reference_tx_hi=999;
    assert(vdc_feedback_local_follow_update(&p,20,30,&life,&a)==VDC_FEEDBACK_MATCH_INVALID);
    assert(!memcmp(&old,&p,sizeof(p)));
    a.reference_tx_hi=1005;a.measurement_sequence=11;a.rx_elapsed_cycles+=1100000000;
    a.reference_tx_lo+=1100000000;a.reference_tx_hi+=1100000000;
    ++a.source_model_token;
    assert(vdc_feedback_local_follow_update(&p,20,30,&life,&a)==VDC_FEEDBACK_MATCH_BASELINED);
    ++a.measurement_sequence;++a.reference_identity_crc32;
    assert(vdc_feedback_local_follow_update(&p,20,30,&life,&a)==VDC_FEEDBACK_MATCH_BASELINED);
    ++life.source_arm_epoch;
    assert(vdc_feedback_local_follow_update(&p,20,30,&life,&a)==VDC_FEEDBACK_MATCH_BASELINED);
    assert(vdc_feedback_local_follow_update(&p,20,31,&life,&a)==VDC_FEEDBACK_MATCH_BASELINED);
    a.rx_elapsed_cycles=UINT64_MAX;a.rx_width_ns=1;
    assert(vdc_feedback_local_follow_update(&p,20,31,&life,&a)==VDC_FEEDBACK_MATCH_INVALID);
    assert(vdc_feedback_local_follow_update(NULL,20,31,&life,&a)==VDC_FEEDBACK_MATCH_INVALID);
}
int main(void) {
    boundaries();uint64_t sd,rd;uint32_t lw0,lw1,rw0,rw1;
    while(scanf("%"SCNu64" %"SCNu64" %"SCNu32" %"SCNu32" %"SCNu32" %"SCNu32,
        &sd,&rd,&lw0,&lw1,&rw0,&rw1)==6) {
        vdc_feedback_match_peer_t p={0};
        vdc_feedback_match_lifetime_t life={.source_arm_epoch=10,.observer_epoch=20,.tick_hz=250000000};
        const uint64_t local=UINT64_C(0x1122334400000000),remote=UINT64_C(0x5566778800000000),delay=12345;
        vdc_feedback_match_pair_t a={.rx_elapsed_cycles=local,.reference_tx_lo=remote+delay,
            .reference_tx_hi=remote+delay+rw0,.source_model_token=3,.reference_identity_crc32=4,
            .rx_width_ns=lw0,.measurement_sequence=100};
        assert(vdc_feedback_local_follow_update(&p,20,30,&life,&a)==VDC_FEEDBACK_MATCH_BASELINED);
        a.rx_elapsed_cycles+=sd;a.rx_width_ns=lw1;a.reference_tx_lo+=rd;
        a.reference_tx_hi=a.reference_tx_lo+rw1;++a.measurement_sequence;
        unsigned result=vdc_feedback_local_follow_update(&p,20,30,&life,&a);
        printf("%u %"PRId64" %"PRId64"\n",result,p.snapshot.raw_ppb_lo,p.snapshot.raw_ppb_hi);
        if(result==VDC_FEEDBACK_MATCH_MATCHED) {
            assert(p.snapshot.reserved==VDC_FEEDBACK_LOCAL_FOLLOW_DOMAIN);
            assert(p.snapshot.pairs[0].reference_tx_lo==remote+delay);
        }
    }
    return 0;
}
'''
