"""T2 fast projection agrees with the actual Domain on ordinary and wide math."""
import itertools
import random
import subprocess

import pytest

from test_trigger_grid_preview import ROOT, compile_grid

U64 = (1 << 64) - 1
I32_MIN, I32_MAX = -(1 << 31), (1 << 31) - 1


@pytest.fixture(scope="module")
def projection(tmp_path_factory):
    d = tmp_path_factory.mktemp("trigger-time-projection")
    return compile_grid(d, "projection", HARNESS, [
        ROOT / "components/sync_trigger/src/trigger_time_projection.c",
        ROOT / "components/vdc_domain/src/vdc_domain.c",
        ROOT / "components/vdc_domain/src/vdc_timestamp.c",
        ROOT / "components/tdma/src/tdma_profile.c",
    ])


def run_cases(exe, cases):
    rows = list(cases)
    r = subprocess.run([str(exe)], input="\n".join(" ".join(map(str, row)) for row in rows)+"\n",
        capture_output=True, text=True, timeout=20)
    assert r.returncode == 0, r.stdout + r.stderr
    assert int(r.stdout.strip()) == len(rows)


def case(base_local=0, base_vdc=0, local=0, rate=0, phase=0, valid=1, period=1000000, lock=1, flags=0):
    return base_local, base_vdc, local, rate, phase, valid, period, lock, flags


def test_qualification_and_output_preservation(projection):
    run_cases(projection, [case(flags=1), case(flags=2), case(flags=3), case(valid=0),
        case(period=0), case(lock=9), case(base_local=2, local=1), case(),
        case(valid=2), case(lock=0), case(lock=8)])


def test_extremes_overflow_and_cancellation(projection):
    rows = [case(base_local=b, base_vdc=v, local=b+d, rate=r, phase=p)
        for b, v, d, r, p in itertools.product(
            (0, 1, 10**12, U64), (0, 1, U64//2, U64-1, U64),
            (0, 1, 999999999, 1000000000, 1000000001, U64//2, U64),
            (I32_MIN, -1000000001, -1000000000, -1, 0, 1, 1000000000, I32_MAX),
            (I32_MIN, -1, 0, 1, I32_MAX)) if b+d <= U64]
    # Individually overflowing base+delta can be cancelled by a negative
    # rate or phase; underflow from rate can be rescued by a positive phase.
    rows += [case(base_vdc=U64, local=100, rate=-1000000000),
             case(base_vdc=U64, local=1, phase=-1),
             case(local=1, rate=I32_MIN, phase=I32_MAX)]
    for rate in (I32_MIN, -1000000000, -7, 7, 1000000000, I32_MAX):
        threshold = U64//abs(rate)
        rows += [case(local=threshold+off, rate=rate, base_vdc=U64//2)
                 for off in (-1, 0, 1) if 0 <= threshold+off <= U64]
    run_cases(projection, rows)


def test_seeded_ten_thousand_models(projection):
    rng = random.Random(0x527270)
    rows = []
    for i in range(10000):
        if i%2:
            # Frequent model updates: ordinary T2 deltas and both signed
            # drift/phase directions, with independently offset origins.
            b = rng.randrange(10**13)
            row = case(base_local=b, base_vdc=rng.randrange(10**13),
                local=b+rng.randrange(10**10), rate=rng.randint(-100000, 100000),
                phase=rng.randint(-10000000, 10000000))
        else:
            b = rng.getrandbits(64)
            row = case(base_local=b, base_vdc=rng.getrandbits(64),
                local=rng.randrange(b, U64+1), rate=rng.randint(I32_MIN, I32_MAX),
                phase=rng.randint(I32_MIN, I32_MAX), lock=rng.randrange(9))
        rows.append(row)
    run_cases(projection, rows)


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "trigger_time_projection.h"
int main(void){
    unsigned cases=0;
    for(;;){
        vdc_dco_control_t model={0};uint64_t local;uint32_t flags;
        const int scanned=scanf("%"SCNu64" %"SCNu64" %"SCNu64" %"SCNd32" %"SCNd32" %"SCNu32" %"SCNu32" %"SCNu32" %"SCNu32,
            &model.base_local_tick64,&model.base_vdc_time64_ns,&local,
            &model.period_adjust_ppb,&model.phase_offset_ns,&model.valid,
            &model.nominal_period_ns,&model.lock_state,&flags);
        if(scanned==EOF)break;
        assert(scanned==9);
        const vdc_dco_control_t original=model;
        const uint64_t sentinel=UINT64_C(0xfedcba9876543210);
        uint64_t oracle=sentinel,fast=sentinel;
        const vdc_dco_control_t *m=flags&1 ? NULL:&model;
        bool expected=vdc_domain_dco_local_to_output_ns(m,local,flags&2 ? NULL:&oracle);
        bool actual=trigger_time_projection_fast(m,local,flags&2 ? NULL:&fast);
        if(expected!=actual||oracle!=fast)fprintf(stderr,"case %u mismatch: expected %u/%"PRIu64" actual %u/%"PRIu64"\n",cases,expected,oracle,actual,fast);
        assert(expected==actual&&oracle==fast);
        assert(actual||fast==sentinel);
        assert(!memcmp(&model,&original,sizeof(model)));
        ++cases;
    }
    printf("%u\n",cases);return 0;
}
'''
