"""Production RUN client timelines and integer clock-enclosure boundaries.

The backend is observable hardware bookkeeping, not a PIO emulator. These
tests qualify admission arithmetic and ownership, not waveform continuity.
"""
import random
import subprocess

import pytest

from test_vdc_run_output import CLIENT_MAIN, CLIENT_PREFIX, ROOT, compile_host


@pytest.fixture(scope='module')
def timeline_client(tmp_path_factory):
    prefix = CLIENT_PREFIX.replace('admitted[4]', 'admitted[2100]')
    prefix = prefix.replace('admitted_count[4]', 'admitted_count[2100]')
    prefix = prefix.replace('submit_calls<4u', 'submit_calls<2100u')
    prefix = prefix.replace('duration==1000u', 'duration>=1u && duration<=20000u')
    source = (ROOT / 'components/vdc_dpll_manager/src/vdc_run_output.inc').read_text(encoding='utf-8')
    return compile_host(tmp_path_factory.mktemp('timeline-client'), 'timeline',
        prefix + source + '\n#define main inherited_main\n' + CLIENT_MAIN +
        '\n#undef main\n' + TIMELINE_MAIN,
        [ROOT / 'components/vdc_domain/src/vdc_domain.c',
         ROOT / 'components/vdc_domain/src/vdc_timestamp.c',
         ROOT / 'components/tdma/src/tdma_profile.c'])


@pytest.mark.parametrize('scenario', [
    'default', 'count_zero', 'count_over', 'schedule_missing', 'schedule_zero',
    'schedule_too_slow', 'plan_capacity', 'low_capacity', 'latched',
    'table_prepared', 'table_running', 'bridge_jitter', 'long_wrap',
    'partial_model', 'watermarks', 'failed_time', 'horizon',
    'first_stop', 'first_cancel', 'cached_pristine',
])
def test_actual_client_timeline(timeline_client, scenario):
    result = subprocess.run([str(timeline_client), scenario], capture_output=True,
                            text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


TIMELINE_MAIN = r'''
static void step(void) { vdc_run_output_service_core1(); }
static void begin(void) { prepare(); arm(true); }
static void initial_block(void)
{
    step(); assert(!submit_calls && s_run_output_pending.planned==4u);
    step(); assert(!submit_calls && s_run_output_pending.planned==8u);
    step(); assert(submit_calls==1u && admitted_count[0]==10u);
    assert(s_run_output.partial_plan_steps==3u && !s_run_output_pending.planned);
}
static void assert_edges(unsigned block,int64_t phase)
{
    assert(admitted_count[block]==10u);
    for(unsigned i=0;i<10u;++i) {
        const sync_io_run_output_edge_t *e=&admitted[block][i];
        /* Independent zero-frequency model: local = ordinal*1 ms-phase+delay.
         * Fixed initial bridge gives upper raw = ceil(local/4)+3. */
        const uint64_t local=e->ordinal*UINT64_C(1000000)-phase+100u;
        assert(e->rising_tick==(local+3u)/4u+3u);
        assert(e->falling_tick==e->rising_tick+250u);
        if(i)assert(e->ordinal==admitted[block][i-1u].ordinal+1u);
        if(block)assert(e->rising_tick>admitted[block-1u][9].falling_tick);
    }
}
int main(int argc,char **argv)
{
    assert(argc==2); const char *kind=argv[1]; initialize();
    timing=(vdc_output_timing_profile_t){12000u,16000u,6000u};
    if(!strncmp(kind,"count_",6u) || !strncmp(kind,"schedule_",9u) ||
       !strcmp(kind,"plan_capacity") || !strcmp(kind,"low_capacity")) {
        uint32_t period=1000000u,request=0x12345678u;
        if(!strcmp(kind,"count_zero"))period=100000000u;
        else if(!strcmp(kind,"count_over"))timing.commit_ahead_us=23000u;
        else if(!strcmp(kind,"schedule_missing"))schedule_ok=false;
        else if(!strcmp(kind,"schedule_zero"))table_cycles=0u;
        else if(!strcmp(kind,"schedule_too_slow"))++table_cycles;
        else if(!strcmp(kind,"plan_capacity"))--timing.plan_ahead_us;
        else if(!strcmp(kind,"low_capacity"))
            timing=(vdc_output_timing_profile_t){8000u,9000u,1000u};
        else assert(false);
        assert(!vdc_run_output_prepare(period,1000u,1000u,&request));
        assert(request==0x12345678u && !prepare_calls && !submit_calls && !cancels);
        assert(!s_run_output_request && !s_run_output_busy);return 0;
    }
    if(!strcmp(kind,"long_wrap")) {
        uint32_t request=0u;
        assert(vdc_run_output_prepare(1000000u,1000u,20000u,&request));arm(true);
    } else begin();
    if(!strcmp(kind,"cached_pristine")) {
        assert(vdc_run_output_service_cached_core1()==generation);
        assert(!bridge_calls && !s_run_output.partial_plan_steps && !submit_calls);
        assert(hardware.state==SYNC_IO_RUN_OUTPUT_PREPARED);return 0;
    }
    if(!strcmp(kind,"table_prepared")) {
        ++table_cycles;step();assert(cancelled && !submit_calls && !bridge_calls);return 0;
    }
    if(!strcmp(kind,"latched")) {
        timing=(vdc_output_timing_profile_t){1u,2u,3u};delay=50000;
        assert(s_run_output.plan_ahead_us==12000u && s_run_output.commit_ahead_us==16000u);
        assert(s_run_output.refill_low_us==6000u && s_run_output.block_edges==10u);
    }
    if(!strcmp(kind,"first_stop") || !strcmp(kind,"first_cancel")) {
        step();assert(s_run_output_pending.planned==4u && !submit_calls);
        if(!strcmp(kind,"first_stop")) { ring.enabled=0u;++ring.config_seq; }
        else vdc_run_output_cancel();
        step();assert(cancelled && !submit_calls && !s_run_output_pending.planned);
        assert(!s_run_output_timeline_valid);return 0;
    }
    initial_block();assert_edges(0u,0);
    assert(bridge_calls==1u && s_run_output.timeline_bridge_samples==1u);
    if(!strcmp(kind,"default") || !strcmp(kind,"latched"))return 0;
    const sync_io_run_output_snapshot_t saved=hardware;
    sync_io_run_output_edge_t prefix[10];memcpy(prefix,admitted[0],sizeof(prefix));
    if(!strcmp(kind,"table_running")) {
        ++table_cycles;step();assert(cancelled && submit_calls==1u);
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));return 0;
    }
    if(!strcmp(kind,"bridge_jitter")) {
        bridge.raw_before+=9000000u;bridge.raw_after+=19000000u;bridge.local_ns+=70000000u;
        raw_override=hardware.last_falling_tick-1500000u;
        step();step();step();assert(submit_calls==2u && bridge_calls==1u);
        assert_edges(1u,0);return 0;
    }
    if(!strcmp(kind,"long_wrap")) {
        bool passed_two=false,wrapped=false;
        for(unsigned n=1u;n<2000u;++n) {
            raw_override=hardware.last_falling_tick-1500000u;
            if(raw_override>UINT64_C(501000002))passed_two=true;
            if(raw_override>UINT32_MAX)wrapped=true;
            step();step();step();assert(submit_calls==n+1u && !cancelled);
            assert_edges(n,0);
        }
        assert(passed_two && wrapped && hardware.last_falling_tick>UINT64_C(5000000000));
        assert(bridge_calls==1u && s_run_output.timeline_bridge_samples==1u);
        assert(s_run_output.partial_plan_steps==6000u);return 0;
    }
    if(!strcmp(kind,"partial_model")) {
        ready=false;raw_override=hardware.last_falling_tick-1500000u;
        step();assert(s_run_output_pending.planned==4u);
        ++model.token;model.dco.phase_offset_ns=2000;
        step();assert(s_run_output_pending.planned==4u && s_run_output.cache_invalidations==1u);
        step();step();assert(s_run_output_pending.valid && submit_calls==1u);
        assert(!memcmp(&saved,&hardware,sizeof(saved)));
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));
        ready=true;vdc_run_output_service_cached_core1();assert(submit_calls==2u);
        for(unsigned i=0;i<10u;++i)assert(admitted[1][i].model_token==model.token);
        assert_edges(1u,2000);assert(bridge_calls==1u);return 0;
    }
    if(!strcmp(kind,"watermarks")) {
        const uint64_t tail=hardware.last_falling_tick;
        raw_override=tail-3000001u;step();
        assert(s_run_output.plan_waits==1u && !s_run_output_pending.planned);
        raw_override=tail-3000000u;step();assert(s_run_output_pending.planned==4u);
        step();step();assert(s_run_output_pending.valid && submit_calls==1u);
        raw_override=tail-1500001u;vdc_run_output_service_cached_core1();
        assert(submit_calls==1u && s_run_output.refill_waits==2u);
        /* One tick of enable uncertainty must count against commitment,
         * without postponing planning/refill based on a later tail. */
        hardware.anchor_before=100u;hardware.anchor_after=101u;
        raw_override=tail-1500000u;vdc_run_output_service_cached_core1();
        assert(submit_calls==1u && s_run_output.commit_waits==1u);
        ++raw_override;vdc_run_output_service_cached_core1();
        assert(submit_calls==2u && !s_run_output_pending.valid);return 0;
    }
    if(!strcmp(kind,"failed_time")) {
        raw_override=hardware.last_falling_tick-1500000u;
        step();assert(s_run_output_pending.planned==4u);
        raw_ok=false;step();
        assert(s_run_output_pending.planned==4u && s_run_output_timeline_valid);
        assert(!memcmp(&saved,&hardware,sizeof(saved)) && bridge_calls==1u);
        raw_ok=true;step();step();assert(submit_calls==2u && bridge_calls==1u);return 0;
    }
    if(!strcmp(kind,"horizon")) {
        raw_override=hardware.last_falling_tick-1500000u;
        step();step();
        hardware.anchor_after=2000000u; /* 8 ms enable uncertainty. */
        step();assert(s_run_output_pending.valid && submit_calls==1u && s_run_output.commit_waits==1u);
        const unsigned plans=s_run_output.partial_plan_steps;
        raw_ok=false;vdc_run_output_service_cached_core1();
        assert(submit_calls==1u && s_run_output_pending.valid && bridge_calls==1u);
        raw_ok=true;raw_override+=2000000u;vdc_run_output_service_cached_core1();
        assert(submit_calls==2u && s_run_output.partial_plan_steps==plans);
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));return 0;
    }
    assert(false);return 1;
}
'''


HELPER_MAIN = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "vdc_future_raw.h"
int main(void)
{
    unsigned operation;uint64_t rb,ra,local,argument;uint32_t hz;
    while(scanf("%u %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu32" %"SCNu64,
        &operation,&rb,&ra,&local,&hz,&argument)==6) {
        const vdc_timestamp_clock_bridge_t b={.raw_before=rb,.raw_after=ra,
            .local_ns=local,.tick_hz=hz};
        vdc_local_raw_bracket_t out={123u,456u};uint64_t now=789u;
        bool ok;
        if(operation==2u) {
            ok=vdc_timestamp_timeline_local_now_upper(&b,argument,&now);
            if(ok)printf("1 %"PRIu64"\n",now);else { assert(now==789u);puts("0"); }
        } else {
            ok=operation==0u ? vdc_timestamp_bridge_local_to_raw(&b,argument,&out) :
                vdc_timestamp_timeline_local_to_raw(&b,argument,&out);
            if(ok)printf("1 %"PRIu64" %"PRIu64"\n",out.lo,out.hi);
            else { assert(out.lo==123u && out.hi==456u);puts("0"); }
        }
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def timeline_helpers(tmp_path_factory):
    return compile_host(tmp_path_factory.mktemp('timeline-helpers'), 'helpers', HELPER_MAIN)


U64 = (1 << 64) - 1


def helper_oracle(row):
    operation, before, after, local, hz, argument = row
    if not 0 < hz <= 500_000_000 or after < before or local > U64 - 1000:
        return (0,)
    if operation == 2:
        if argument < after or (argument - before) > 32 * hz:
            return (0,)
        # Unbounded Python integers, one rational ceiling; no C decomposition.
        result = local + 1000 + ((argument - before) * 1_000_000_000 + hz - 1) // hz
        return (1, result) if result <= U64 else (0,)
    horizon = 2_000_000_000 if operation == 0 else 32_000_000_000
    delta = argument - local
    if after == U64 or not 1000 <= delta <= horizon:
        return (0,)
    lo = before + (delta - 1000) * hz // 1_000_000_000 + 1
    hi = after + (delta * hz + 999_999_999) // 1_000_000_000 + 1
    return (1, lo, hi) if lo <= hi <= U64 else (0,)


def check_helper_rows(executable, rows):
    result = subprocess.run([str(executable)],
        input=''.join(' '.join(map(str, row)) + '\n' for row in rows),
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    actual = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    assert len(actual) == len(rows)
    for row, got in zip(rows, actual):
        assert got == helper_oracle(row), (row, got, helper_oracle(row))


def test_timeline_exact_horizons_overflow_and_legacy_two_seconds(timeline_helpers):
    rows = []
    for hz in (1, 12_000_000, 249_999_937, 250_000_000, 500_000_000):
        for delta in (999, 1000, 2_000_000_000, 2_000_000_001,
                      20_000_000_000, 32_000_000_000, 32_000_000_004):
            for operation in (0, 1):
                rows.append((operation, 100, 103, 4000, hz, 4000 + delta))
        for ticks in (0, 1, 2 * hz, 20 * hz, 32 * hz, 32 * hz + 1):
            rows.append((2, 100, 100, 4000, hz, 100 + ticks))
    rows += [(op, before, after, local, hz, argument)
             for op in (0, 1, 2)
             for before, after, local, hz, argument in (
                 (U64 - 10, U64 - 1, 0, 500_000_000, U64),
                 (0, 0, U64 - 1000, 1, U64),
                 (0, 0, U64 - 999, 1, U64),
                 (101, 100, 0, 250_000_000, 1000),
                 (0, U64, 0, 250_000_000, U64),
                 (0, 0, 0, 500_000_001, 1000),
                 (0, 0, 0, 0, 1000))]
    check_helper_rows(timeline_helpers, rows)


def test_timeline_integer_oracle_full_width_randomized(timeline_helpers):
    rng = random.Random(0x32_20_02)
    rows = []
    for _ in range(1500):
        hz = rng.choice((1, 12_000_000, 249_999_937, 250_000_000, 500_000_000))
        before = rng.choice((rng.randrange(1 << 33), U64 - rng.randrange(1 << 33)))
        after = min(U64, before + rng.randrange(1000))
        local = rng.choice((rng.randrange(1 << 33), U64 - rng.randrange(1 << 36)))
        target = min(U64, local + rng.randrange(33_000_000_000))
        rows.append((rng.randrange(2), before, after, local, hz, target))
        rows.append((2, before, after, local, hz, min(U64, before + rng.randrange(33 * hz))))
    check_helper_rows(timeline_helpers, rows)
