"""Exact common-clock constraints against independent rational latent time.

Bridge observations are TIMER0 integer microseconds. The event's coordinate
is floor(X) in nanoseconds, not TIMER0's microsecond staircase. Real Domain
mapping remains linked; no physical pad accuracy or clock admission is implied.
"""
from dataclasses import dataclass, replace
from fractions import Fraction
import itertools
import json
import math
import random
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_priority_follow import domain_sources

M = 10**9
U64 = (1 << 64) - 1
LO_SENTINEL, HI_SENTINEL = U64 - 1234, U64 - 5678
# Independent acceptance expectation; never derive this from the C macro.
EXPECTED_RETAINED_CONSTRAINTS = 64


@dataclass(frozen=True)
class Bridge:
    before: int
    local: int
    after: int
    hz: int = 250_000_000

    def constraint(self):
        step = Fraction(M, self.hz)
        return self.local - self.after * step, self.local + 1000 - self.before * step


@dataclass(frozen=True)
class MappingCase:
    bridges: tuple
    event_lo: int
    event_hi: int
    latent_local: int = 0
    valid: int = 1
    nominal: int = 1_500_000
    lock: int = 1
    base: int = 0
    output: int = 0
    rate: int = 0
    phase: int = 0
    flags: int = 0

    def line(self):
        head = [len(self.bridges), self.event_lo, self.event_hi, self.latent_local,
                self.valid, self.nominal, self.lock, self.base, self.output,
                self.rate, self.phase, self.flags]
        return " ".join(map(str, head + [n for b in self.bridges for n in
            (b.before, b.local, b.after, b.hz)]))


def domain_oracle(case, local):
    if (not case.valid or not case.nominal or case.lock > 8 or
            not 0 <= local <= U64 or local < case.base):
        return None
    elapsed = local - case.base
    value = case.output + elapsed + int(Fraction(elapsed * case.rate, M)) + case.phase
    return value if 0 <= value <= U64 else None


def exact_intersection(bridges):
    constraints = [bridge.constraint() for bridge in bridges]
    return max(pair[0] for pair in constraints), min(pair[1] for pair in constraints)


def mapping_oracle(case):
    """No copy of production scaled-integer arithmetic: all bounds are Fraction."""
    cap = EXPECTED_RETAINED_CONSTRAINTS
    used = case.bridges[:cap] + case.bridges[-1:] if len(case.bridges) > cap else case.bridges
    low, high = exact_intersection(used)
    if low >= high or case.flags or case.rate <= -M or case.event_lo > case.event_hi:
        return None
    step = Fraction(M, case.bridges[0].hz)
    # Upper coordinate is strictly excluded, including integral endpoints.
    lo = math.floor(case.event_lo * step + low)
    hi = math.ceil(case.event_hi * step + high) - 1
    a, b = domain_oracle(case, lo), domain_oracle(case, hi)
    return None if a is None or b is None or a > b else (a, b)


def from_latent(hz, raw_anchor, local_anchor, offsets, brackets=None, rate=0, phase=0):
    """Actual enclosed read instant fixes TIMER0 floor(us); raw brackets may vary."""
    step = Fraction(M, hz)
    latent_offset = Fraction(local_anchor) - raw_anchor * step
    if brackets is None:
        brackets = [(0, 0)] * len(offsets)
    bridges = []
    for offset, (before, after) in zip(offsets, brackets, strict=True):
        reading = raw_anchor + offset
        x = latent_offset + reading * step
        local = 1000 * math.floor(x / 1000)
        bridges.append(Bridge(reading - before, local, reading + after, hz))
    return MappingCase(tuple(bridges), raw_anchor, raw_anchor,
                       math.floor(local_anchor), rate=rate, phase=phase)


@pytest.fixture(scope="module")
def mapping_executable(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp("clock-mapping"), "clock_mapping", HARNESS,
                              domain_sources())


def check(executable, name, cases, containment=False, single=False):
    data = "\n".join(case.line() for case in cases) + "\n"
    result = subprocess.run([str(executable), "numeric"], input=data,
        text=True, capture_output=True, timeout=60)
    (executable.parent / f"{name}.input").write_text(data, encoding="utf-8")
    (executable.parent / f"{name}.json").write_text(json.dumps(dict(returncode=result.returncode,
        stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    rows = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    for case, row in zip(cases, rows, strict=True):
        status, lo, hi, original_lo, original_hi, valid, actual, count, epoch, count_before, retained, reset = row
        expected = mapping_oracle(case)
        if expected is None:
            assert status and (lo, hi) == (LO_SENTINEL, HI_SENTINEL), (case, row)
        else:
            assert (status, lo, hi) == (0, *expected), (case, row, expected)
            assert original_lo <= lo <= hi <= original_hi
            assert count == min(len(case.bridges), EXPECTED_RETAINED_CONSTRAINTS) and epoch == 1
            assert count_before == min(len(case.bridges)-1, EXPECTED_RETAINED_CONSTRAINTS)
            assert retained == int(len(case.bridges) <= EXPECTED_RETAINED_CONSTRAINTS)
            assert reset == int(len(case.bridges) == 1)
            if single:
                assert (lo, hi) == (original_lo, original_hi)
        point = domain_oracle(case, case.latent_local)
        assert (valid, actual) == ((0, LO_SENTINEL) if point is None else (1, point)), (case, row)
        if containment and expected is not None:
            assert valid and lo <= actual <= hi, (case, row)
    return rows


def test_single_bridge_is_original_projector(mapping_executable):
    cases = [from_latent(hz, 10**10, Fraction(5*M) + phase, [12], [(1, 2)], rate=rate)
        for hz, phase, rate in itertools.product(
            [3, 7, 125_000_000, 250_000_000, 333_333_333, 499_999_999, 500_000_000],
            [Fraction(0), Fraction(1, 3), Fraction(999999, 1000)], [-10000, -1, 0, 1, 10000])
        if 15 <= 2*hz]
    check(mapping_executable, "single", cases, containment=True, single=True)


def test_phase_diversity_and_all_timer0_bins_preserve_real_event(mapping_executable):
    cases = [from_latent(250_000_000, 10**10, Fraction(5*M + phase) + Fraction(1, 3),
                        [10, 49, 91, 129, 170, 211, 247, 289], [(1, 2)]*8, rate=rate)
             for phase, rate in itertools.product(range(1000), [-10000, 0, 10000])]
    rows = check(mapping_executable, "all_phases", cases, containment=True)
    assert max(row[2] - row[1] for row in rows) < 200


def test_repeated_same_phase_does_not_manufacture_resolution(mapping_executable):
    cases = [from_latent(250_000_000, 10**10, 5*M+333, offsets, [(1, 2)]*len(offsets))
             for offsets in ([10], [10+250*i for i in range(64)], [10+250*i for i in range(96)])]
    rows = check(mapping_executable, "same_phase", cases, containment=True)
    assert len({(row[1], row[2]) for row in rows}) == 1
    assert rows[0][2] - rows[0][1] > 999
    repeated = from_latent(250_000_000, 10**10, 5*M+333, [10]*96)
    repeated_row = check(mapping_executable, "same_instant", [repeated], containment=True)[0]
    assert repeated_row[2] - repeated_row[1] == 999


def test_latent_ns_is_distinct_from_timer0_floor_us(mapping_executable):
    case = from_latent(250_000_000, 10**10, Fraction(5*M+733) + Fraction(1, 7),
                       [10, 49, 91, 129, 170, 211, 247, 289], [(0, 0)]*8)
    row = check(mapping_executable, "semantic_distinction", [case], containment=True)[0]
    floor_us = 1000 * (case.latent_local // 1000)
    assert row[1] <= case.latent_local <= row[2]
    assert floor_us < row[1]


def test_fractional_frequency_negative_offsets_and_large_raw_origin(mapping_executable):
    rng = random.Random(0xC10C4)
    cases = []
    for _ in range(2000):
        hz = rng.choice([1, 3, 7, 125_000_000, 250_000_000, 333_333_333, 499_999_999, 500_000_000])
        count = rng.choice([1, 8, 9, 63, 64, 65, 66, 96])
        offsets = sorted(rng.randrange(0, min(2*hz, 10000)) for _ in range(count))
        raw_anchor = rng.choice([10**10, U64 - 1000000])
        local_anchor = Fraction(rng.choice([5*M, 10**15])) + Fraction(rng.randrange(1000), 1000)
        case = from_latent(hz, raw_anchor, local_anchor, offsets,
            rate=rng.choice([-999999999, -10000, -1, 0, 1, 10000, 2147483647]),
            phase=rng.choice([-2147483648, -1, 0, 1, 2147483647]))
        cases.append(replace(case, output=3*M))
    check(mapping_executable, "random_latent", cases, containment=True)


def test_open_upper_endpoint_and_signed_domain_truncation(mapping_executable):
    cases = []
    for hz, rate in itertools.product([3, 250_000_000, 333_333_333], [-999999999, -10000, -1, 0, 1, 10000]):
        case = from_latent(hz, 10**10, Fraction(5*M+999999, 1000), [1, 2])
        cases.append(replace(case, rate=rate, output=3*M, base=1))
    check(mapping_executable, "half_open", cases, containment=True)


def test_local_uint64_anchor_cancellation_without_absolute_products(mapping_executable):
    cases=[]
    for hz,rate in itertools.product([3,250_000_000,333_333_333,500_000_000],[-10000,0,10000]):
        case=from_latent(hz,U64-100000, Fraction(U64-3*M)+Fraction(1,3),[1,2,3,4],rate=rate)
        cases.append(replace(case,base=U64-4*M,output=3*M))
    check(mapping_executable,"local_uint64",cases,containment=True)


@pytest.mark.parametrize("flag", [1, 2, 4, 8])
def test_null_inputs_leave_whole_result_unchanged(mapping_executable, flag):
    case = replace(from_latent(250_000_000, 10000, 5*M, [10]), flags=flag)
    check(mapping_executable, f"null_{flag}", [case])


def test_real_mapper_failures_preserve_output(mapping_executable):
    base = from_latent(250_000_000, 10000, 5*M, [10])
    cases = [replace(base, **change) for change in [dict(valid=0), dict(nominal=0), dict(lock=9),
        dict(rate=-M), dict(rate=-2147483648), dict(base=6*M), dict(output=U64),
        dict(base=5*M, phase=-2147483648)]]
    check(mapping_executable, "mapper_rejections", cases)


def test_ninth_through_sixty_fourth_constraints_keep_narrowing(mapping_executable):
    cases = [from_latent(250_000_000, 10**10, 5*M+333, list(range(10, 10+count)))
             for count in range(1, 65)]
    rows = check(mapping_executable, "all_retained_prefixes", cases, containment=True)
    widths = [row[2]-row[1] for row in rows]
    assert widths == [999-4*i for i in range(64)]
    assert rows[7][7] == 8 and rows[8][7] == 9 and rows[63][7] == 64


def test_cap_uses_current_sixty_fifth_constraint_without_retaining_it(mapping_executable):
    first = list(range(10, 74))
    cases = [from_latent(250_000_000, 10**10, 5*M+333, offsets)
             for offsets in [first, first+[135], first+[135, 260]]]
    rows = check(mapping_executable, "cap", cases, containment=True)
    assert rows[1][2] - rows[1][1] < rows[0][2] - rows[0][1]
    assert rows[2][1:3] == rows[0][1:3]
    assert [row[7] for row in rows] == [64, 64, 64]
    assert [row[10] for row in rows] == [1, 0, 0]


@pytest.mark.parametrize("case", ["horizon", "model", "exhausted", "empty", "raw_rollback",
    "local_rollback", "clock_change", "invalid_cache", "invalid_bin", "overflow_local",
    "invalid_bridge", "event_future", "source_age", "offset_min", "offset_max",
    "invalid_epoch", "invalid_model", "corrupt_anchor", "full_horizon", "full_model",
    "full_exhausted", "full_empty", "full_raw_rollback", "full_local_rollback",
    "full_clock_change", "full_invalid_cache", "full_corrupt_anchor"])
def test_cache_lifetime_and_empty_results_are_transactional(mapping_executable, case):
    command = [str(mapping_executable), case]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    (mapping_executable.parent / f"{case}.json").write_text(json.dumps(dict(command=command,
        returncode=result.returncode, stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vdc_clock_mapping.h"
_Static_assert(sizeof(vdc_clock_mapping_cache_t)==64u,"Mapping cache stays one fixed-size intersection");
#undef assert
#define assert(condition) do { if(!(condition)) { \
    fprintf(stderr,"assertion failed at %s:%d: %s\n",__FILE__,__LINE__,#condition);exit(1); \
} } while(0)
static void numeric(void)
{
    uint32_t n,flags;uint64_t event_lo,event_hi,latent;
    vdc_dco_control_t dco;
    while(memset(&dco,0,sizeof(dco)),scanf("%"SCNu32" %"SCNu64" %"SCNu64" %"SCNu64
        " %"SCNu32" %"SCNu32" %"SCNu32" %"SCNu64" %"SCNu64" %"SCNd32" %"SCNd32" %"SCNu32,
        &n,&event_lo,&event_hi,&latent,&dco.valid,&dco.nominal_period_ns,&dco.lock_state,
        &dco.base_local_tick64,&dco.base_vdc_time64_ns,&dco.period_adjust_ppb,&dco.phase_offset_ns,&flags)==12) {
        vdc_clock_mapping_cache_t cache={0};
        vdc_clock_mapping_result_t result={0};
        unsigned status=1u;
        for(uint32_t i=0;i<n;++i) {
            vdc_timestamp_clock_bridge_t b={0};
            assert(scanf("%"SCNu64" %"SCNu64" %"SCNu64" %"SCNu32,
                &b.raw_before,&b.local_ns,&b.raw_after,&b.tick_hz)==4);
            const vdc_clock_mapping_cache_t saved=cache;
            const vdc_dco_control_t saved_dco=dco;
            const vdc_timestamp_clock_bridge_t saved_b=b;
            memset(&result,0xa5,sizeof(result));
            result.output_lo=UINT64_MAX-1234;result.output_hi=UINT64_MAX-5678;
            const vdc_clock_mapping_result_t sentinel=result;
            status=vdc_clock_mapping_project(flags&1?NULL:&cache,flags&2?NULL:&dco,
                flags&4?NULL:&b,19u,event_lo,event_hi,flags&8?NULL:&result);
            assert(!memcmp(&cache,&saved,sizeof(cache)) && !memcmp(&dco,&saved_dco,sizeof(dco)) &&
                !memcmp(&b,&saved_b,sizeof(b)));
            if(status)assert(!memcmp(&result,&sentinel,sizeof(result)));
            else cache=result.next;
        }
        uint64_t actual=UINT64_MAX-1234;
        const bool valid=vdc_domain_dco_local_to_output_ns(&dco,latent,&actual);
        printf("%u %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %u %"PRIu64" %u %u %u %u %u\n",
            status,result.output_lo,result.output_hi,result.original_output_lo,result.original_output_hi,
            (unsigned)valid,actual,cache.count,cache.epoch,result.count_before,result.retained,result.reset_reason);
    }
}
static void lifecycle(const char *name)
{
    vdc_clock_mapping_cache_t cache={0};
    vdc_dco_control_t dco={.valid=1u,.nominal_period_ns=1500000u};
    vdc_timestamp_clock_bridge_t b={.raw_before=1000u,.raw_after=1000u,.local_ns=10000000u,.tick_hz=250000000u};
    vdc_clock_mapping_result_t out;
    assert(vdc_clock_mapping_project(&cache,&dco,&b,19u,1000u,1000u,&out)==VDC_CLOCK_MAPPING_OK);
    assert(out.reset_reason==VDC_CLOCK_MAPPING_RESET_START && out.next.epoch==1u && out.next.count==1u);
    cache=out.next;
    const bool full=!strncmp(name,"full_",5u);
    if(full) {
        name+=5;
        for(uint32_t i=1u;i<64u;++i) {
            const vdc_clock_mapping_cache_t before=cache;
            assert(vdc_clock_mapping_project(&cache,&dco,&b,19u,1000u,1000u,&out)==VDC_CLOCK_MAPPING_OK);
            assert(!memcmp(&cache,&before,sizeof(cache)));
            assert(out.count_before==i && out.retained==1u && out.next.count==i+1u &&
                out.reset_reason==VDC_CLOCK_MAPPING_RESET_NONE && out.next.epoch==1u);
            cache=out.next;
        }
        assert(cache.count==64u);
    }
    uint32_t token=19u;uint64_t event=1000u;
    vdc_clock_mapping_status_t expected=VDC_CLOCK_MAPPING_INVALID;
    if(!strcmp(name,"horizon")) {
        b.raw_before=b.raw_after=500001000u;b.local_ns=2010000000u;event=b.raw_before-1u;
        assert(vdc_clock_mapping_project(&cache,&dco,&b,token,event,event,&out)==VDC_CLOCK_MAPPING_OK);
        assert(out.reset_reason==VDC_CLOCK_MAPPING_RESET_NONE && out.next.epoch==1u &&
            out.next.count==(full?64u:2u) && out.count_before==(full?64u:1u) && out.retained==(full?0u:1u));
        cache=out.next;++b.raw_before;++b.raw_after;++event;
        assert(vdc_clock_mapping_project(&cache,&dco,&b,token,event,event,&out)==VDC_CLOCK_MAPPING_OK);
        assert(out.reset_reason==VDC_CLOCK_MAPPING_RESET_HORIZON && out.next.epoch==2u && out.next.count==1u &&
            out.count_before==(full?64u:2u) && out.retained==1u);
        return;
    }
    if(!strcmp(name,"model")) {
        assert(vdc_clock_mapping_project(&cache,&dco,&b,20u,event,event,&out)==VDC_CLOCK_MAPPING_OK);
        assert(out.reset_reason==VDC_CLOCK_MAPPING_RESET_MODEL && out.next.epoch==2u && out.next.count==1u &&
            out.count_before==(full?64u:1u) && out.retained==1u);return;
    }
    if(!strcmp(name,"exhausted")){cache.epoch=UINT32_MAX;token=20u;expected=VDC_CLOCK_MAPPING_EXHAUSTED;}
    else if(!strcmp(name,"empty")){b.raw_before=b.raw_after=1250u;b.local_ns+=2000u;expected=VDC_CLOCK_MAPPING_CONTRADICTION;}
    else if(!strcmp(name,"raw_rollback")){b.raw_before=b.raw_after=999u;event=999u;token=20u;}
    else if(!strcmp(name,"local_rollback")){b.local_ns-=1000u;token=20u;}
    else if(!strcmp(name,"clock_change"))b.tick_hz=125000000u;
    else if(!strcmp(name,"invalid_cache"))cache.count=65u;
    else if(!strcmp(name,"offset_min"))cache.offset_lo=INT64_MIN;
    else if(!strcmp(name,"offset_max"))cache.offset_hi_open=INT64_MAX;
    else if(!strcmp(name,"invalid_epoch"))cache.epoch=0u;
    else if(!strcmp(name,"invalid_model"))cache.model_token=0u;
    else if(!strcmp(name,"corrupt_anchor"))++cache.anchor_raw;
    else if(!strcmp(name,"invalid_bin"))++b.local_ns;
    else if(!strcmp(name,"overflow_local"))b.local_ns=UINT64_MAX-UINT64_MAX%1000u;
    else if(!strcmp(name,"invalid_bridge"))--b.raw_after;
    else if(!strcmp(name,"event_future"))++event;
    else if(!strcmp(name,"source_age")){b.raw_before=b.raw_after=500001001u;b.local_ns+=2000000000u;}
    const vdc_clock_mapping_cache_t saved=cache;
    memset(&out,0xa5,sizeof(out));const vdc_clock_mapping_result_t sentinel=out;
    assert(vdc_clock_mapping_project(&cache,&dco,&b,token,event,event,&out)==expected);
    assert(!memcmp(&out,&sentinel,sizeof(out)) && !memcmp(&cache,&saved,sizeof(cache)));
}
int main(int argc,char **argv)
{ assert(argc==2);if(!strcmp(argv[1],"numeric"))numeric();else lifecycle(argv[1]);return 0; }
'''
