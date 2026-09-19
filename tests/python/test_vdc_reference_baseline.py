"""Run external-reference composition against the production Domain owner."""
from pathlib import Path
import os
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def baseline_host(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-reference-baseline")
    source = directory / "baseline.c"
    source.write_text(HARNESS, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "Host C compiler required"
    sources = [ROOT / f"components/vdc_domain/src/{name}.c" for name in (
        "vdc_domain", "vdc_timestamp", "vdc_ring_observer", "vdc_sync_io_adapter", "vdc_tdma_payload")]
    sources += [ROOT / f"components/tdma/src/{name}.c" for name in (
        "tdma_service", "tdma_profile", "tdma_operating_profile", "tdma_payload_registry",
        "tdma_flight_fifo", "tdma_flight_engine", "tdma_process_image_map", "tdma_ring_runtime",
        "tdma_traffic_scheduler", "tdma_service_timing")]
    executable = directory / ("baseline.exe" if os.name == "nt" else "baseline")
    result = subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        f"-I{ROOT}", f"-I{ROOT / 'components/vdc_domain/inc'}", f"-I{ROOT / 'components/tdma/inc'}",
        str(source), *map(str, sources), "-o", str(executable)],
        capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("mode", ["continuity", "pi_positive", "pi_negative", "reject",
                                  "lifetime", "saturation", "legacy"])
def test_reference_baseline_real_domain(baseline_host, mode):
    result = subprocess.run([str(baseline_host), mode], capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#define main existing_vdc_domain_tests
#include "tests/unit/test_vdc_domain.c"
#undef main

static void fixture(vdc_domain_context_t *c)
{
    assert(vdc_domain_init(c));
    assert(install_test_path_delay(c));
    vdc_domain_set_ready(c, true);
    for (unsigned i=1; i<=8; ++i) {
        vdc_tdma_timestamp_evidence_t e=make_hardware_sample(&c->schedule,i,0);
        assert(vdc_domain_submit_tdma_evidence(c,&e));
    }
    assert(c->clock.valid && c->dco.valid);
}
static uint64_t output(const vdc_dco_control_t *d,uint64_t at)
{
    uint64_t n;
    assert(vdc_domain_dco_local_to_output_ns(d,at,&n));
    return n;
}
static void reject_unchanged(vdc_domain_context_t *c,int32_t target,uint64_t at)
{
    vdc_domain_context_t before=*c;
    assert(!vdc_domain_apply_reference_baseline(c,target,at));
    assert(!memcmp(c,&before,sizeof(*c)));
}
static void continuity(void)
{
    vdc_domain_context_t c;
    fixture(&c);
    c.clock.phase_offset_ns=c.dco.phase_offset_ns=37;
    c.dpll.loop_filter_integrator_ppb=29;
    c.dpll.last_frequency_error_ppb=-11;
    c.clock.period_adjust_ppb=c.dco.period_adjust_ppb=-18;
    const int32_t targets[]={4000,4000,-4000,-4000,0,0};
    for (unsigned i=0;i<sizeof(targets)/sizeof(*targets);++i) {
        const uint64_t at=c.dco.base_local_tick64+1000000000ull;
        const vdc_domain_context_t before=c;
        const uint64_t old=output(&c.dco,at);
        uint64_t old_clock,new_clock;
        assert(vdc_domain_clock_model_local_to_vdc_ns(&c.clock,at,&old_clock));
        assert(vdc_domain_apply_reference_baseline(&c,targets[i],at));
        assert(c.dco.period_adjust_ppb==targets[i]-18);
        assert(c.clock.period_adjust_ppb==c.dco.period_adjust_ppb);
        assert(output(&c.dco,at)==old);
        assert(vdc_domain_clock_model_local_to_vdc_ns(&c.clock,at,&new_clock));
        assert(old_clock==new_clock);
        assert(c.clock.phase_offset_ns==37 && c.dco.phase_offset_ns==37);
        assert(!memcmp(&c.dpll,&before.dpll,sizeof(c.dpll)));
        if (targets[i]==before.reference_baseline_ppb) assert(!memcmp(&c,&before,sizeof(c)));
        else {
            assert(c.clock.model_seq==before.clock.model_seq+1);
            assert(c.dco.dco_update_seq==before.dco.dco_update_seq+1);
            assert(c.dco.source_model_seq==c.clock.model_seq);
        }
        assert(output(&c.dco,at+1000000000ull)-old==(uint64_t)(1000000000+targets[i]-18));
    }
    const int32_t phases[]={-37,INT32_MIN,INT32_MAX};
    for(unsigned i=0;i<sizeof(phases)/sizeof(*phases);++i) {
        fixture(&c);
        c.clock.phase_offset_ns=c.dco.phase_offset_ns=phases[i];
        c.clock.base_vdc_time64_ns=c.dco.base_vdc_time64_ns=1000000000000ull;
        uint64_t at=c.dco.base_local_tick64+1000000000ull;
        uint64_t old=output(&c.dco,at);
        assert(vdc_domain_apply_reference_baseline(&c,4000,at));
        assert(output(&c.dco,at)==old);
        assert(c.clock.phase_offset_ns==phases[i] && c.dco.phase_offset_ns==phases[i]);
    }
}
static void pi_case(int32_t baseline)
{
    vdc_domain_context_t c,plain;
    fixture(&c);plain=c;
    assert(vdc_domain_apply_reference_baseline(&c,baseline,c.dco.base_local_tick64));
    for (unsigned i=9;i<=600;++i) {
        /* Same raw TDMA evidence drives both actual Domain state machines. */
        const int32_t phase=i<300 ? 0 : 10;
        vdc_tdma_timestamp_evidence_t e=make_hardware_sample(&c.schedule,i,phase);
        assert(vdc_domain_submit_tdma_evidence(&c,&e));
        assert(vdc_domain_submit_tdma_evidence(&plain,&e));
        assert(c.reference_baseline_ppb==baseline);
        assert(c.dco.period_adjust_ppb==plain.dco.period_adjust_ppb+baseline);
        assert(c.clock.period_adjust_ppb==c.dco.period_adjust_ppb);
        assert(c.dpll.loop_filter_integrator_ppb==plain.dpll.loop_filter_integrator_ppb);
        assert(c.dpll.last_phase_error_ns==plain.dpll.last_phase_error_ns);
        assert(c.dpll.last_frequency_error_ppb==plain.dpll.last_frequency_error_ppb);
    }
    /* A zero total rate can still have nonzero PI residual. */
    fixture(&c);
    c.clock.period_adjust_ppb=c.dco.period_adjust_ppb=-baseline;
    plain=c;
    assert(vdc_domain_apply_reference_baseline(&c,baseline,c.dco.base_local_tick64));
    assert(c.dco.period_adjust_ppb==0);
    vdc_tdma_timestamp_evidence_t e=make_hardware_sample(&c.schedule,9,0);
    assert(vdc_domain_submit_tdma_evidence(&c,&e));
    assert(vdc_domain_submit_tdma_evidence(&plain,&e));
    assert(c.dpll.last_phase_error_ns==plain.dpll.last_phase_error_ns);
}
static void rejects(void)
{
    vdc_domain_context_t good,c;
    fixture(&good);
    const uint64_t at=good.dco.base_local_tick64;
    for(unsigned mode=0;mode<16;++mode) {
        c=good;
        switch(mode) {
        case 0:c.ready=0;break;
        case 1:c.control.profile.mode=VDC_DPLL_CONTROL_MODE_FOLLOWER;break;
        case 2:c.schedule.local_slot_id=1;break;
        case 3:c.clock.valid=0;break;
        case 4:c.dco.valid=0;break;
        case 5:c.clock.run_id++;break;
        case 6:c.dco.epoch_id++;break;
        case 7:c.clock.model_seq=UINT32_MAX;break;
        case 8:c.dco.dco_update_seq=UINT32_MAX;break;
        case 9:c.schedule.enabled=0;break;
        case 10:c.servo.enabled=0;break;
        case 11:c.clock.tdma_schedule_crc32^=1;break;
        case 12:c.dco.servo_profile_crc32^=1;break;
        case 13:c.clock.base_local_tick64=at+1;break;
        case 14:c.dco.base_local_tick64=at+1;break;
        case 15:c.clock.period_adjust_ppb=c.dco.period_adjust_ppb=(int32_t)c.servo.sanity_freq_limit_ppb;break;
        }
        reject_unchanged(&c,4000,at);
    }
    c=good;
    reject_unchanged(&c,INT32_MAX,at);
    reject_unchanged(&c,INT32_MIN,at);
    c.servo.sanity_freq_limit_ppb=UINT32_MAX;
    c.clock.period_adjust_ppb=c.dco.period_adjust_ppb=INT32_MAX;
    reject_unchanged(&c,1,at);
    c.clock.period_adjust_ppb=c.dco.period_adjust_ppb=-999999999;
    reject_unchanged(&c,-1,at);
    c=good;
    c.dco.base_vdc_time64_ns=UINT64_MAX;
    c.dco.phase_offset_ns=1;
    reject_unchanged(&c,4000,at);
}
static void lifetime(void)
{
    vdc_domain_context_t c;
    fixture(&c);
    assert(vdc_domain_apply_reference_baseline(&c,4000,c.dco.base_local_tick64));
    const vdc_dco_control_t old=c.dco;
    vdc_dpll_control_profile_t role=c.control.profile;
    role.mode=VDC_DPLL_CONTROL_MODE_FOLLOWER;role.follow_master_slot_id=1;
    assert(vdc_domain_set_dpll_control_profile(&c,&role));
    assert(c.reference_baseline_ppb==0);
    assert(c.dco.period_adjust_ppb==old.period_adjust_ppb);
    assert(output(&c.dco,old.base_local_tick64+1000000)==output(&old,old.base_local_tick64+1000000));
    fixture(&c);
    assert(vdc_domain_apply_reference_baseline(&c,-4000,c.dco.base_local_tick64));
    c.path_delay.entry_count=c.schedule.ring_binding.node_count;
    for(unsigned i=0;i<c.path_delay.entry_count;++i)
        c.path_delay.entries[i].reference_slot_id=(i+1)%c.path_delay.entry_count;
    for(unsigned i=c.path_delay.entry_count;i<VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT;++i)
        c.path_delay.entries[i].valid=0;
    assert(vdc_domain_load_observation_path_matrix(&c.path_delay,c.schedule.ring_binding.node_count));
    c.path_delay.table_crc32=vdc_domain_path_delay_table_crc32(&c.path_delay);
    assert(vdc_domain_activate_tdma_configuration(&c,&c.schedule,&c.timestamp_dictionary,&c.path_delay));
    assert(c.reference_baseline_ppb==0 && c.dco.period_adjust_ppb==0);
}
static void saturation(void)
{
    vdc_domain_context_t c;
    fixture(&c);
    assert(vdc_domain_apply_reference_baseline(&c,(int32_t)c.servo.sanity_freq_limit_ppb,c.dco.base_local_tick64));
    for(unsigned i=9;i<100;++i) {
        vdc_tdma_timestamp_evidence_t e=make_hardware_sample(&c.schedule,i,-10);
        assert(vdc_domain_submit_tdma_evidence(&c,&e));
        assert(c.dco.period_adjust_ppb<=(int32_t)c.servo.sanity_freq_limit_ppb);
        assert(c.dpll.loop_filter_integrator_ppb>=0);
    }
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"continuity"))continuity();
    else if(!strcmp(argv[1],"pi_positive"))pi_case(4000);
    else if(!strcmp(argv[1],"pi_negative"))pi_case(-4000);
    else if(!strcmp(argv[1],"reject"))rejects();
    else if(!strcmp(argv[1],"lifetime"))lifetime();
    else if(!strcmp(argv[1],"saturation"))saturation();
    else if(!strcmp(argv[1],"legacy"))return existing_vdc_domain_tests();
    else assert(!"unknown test");
    return 0;
}
'''
