"""Reference adoption through the actual Domain, dispatcher and mirror consumers.

External hardware windows are simulated. A reference commit must be visible
without promoting unfinished evidence or appending an early capture record.
"""
import os
import subprocess

import pytest

from test_vdc_command_owner import ROOT, MANAGER, compile_executable
from test_vdc_idle_maintenance import idle_exe, definition
from test_vdc_publication_generation import publication_exe
from test_vdc_priority_follow import domain_sources


@pytest.fixture(scope='module')
def reference_publication_exe(idle_exe, tmp_path_factory):
    source = idle_exe.with_suffix('.c').read_text(encoding='utf-8')
    source = source.replace('int main(int argc,char **argv)',
                            'int idle_regression_main(int argc,char **argv)', 1)
    source = source.replace(
        'static void reference_discipline_service_core1(uint32_t session) { (void)session; }',
        'static void reference_discipline_service_core1(uint32_t session);')
    path = MANAGER.parent / 'vdc_reference_discipline.inc'
    baseline = os.environ.get('VDC_REFERENCE_PUBLICATION_BASELINE')
    fragment = (subprocess.check_output(
        ['git', 'show', f'{baseline}:{path.relative_to(ROOT).as_posix()}'],
        cwd=ROOT, text=True, encoding='utf-8') if baseline else path.read_text(encoding='utf-8'))
    source += INPUTS + fragment + '\n'
    source += definition((ROOT/'tests/unit/test_vdc_domain.c').read_text(encoding='utf-8'),
                         'install_test_path_delay') + '\n'
    source += SCENARIOS
    return compile_executable(tmp_path_factory.mktemp('reference-publication'), 'reference', source,
        domain_sources() + [ROOT/'components/distributed_refmem/src/refmem_vector_table.c'])


@pytest.mark.parametrize('scenario', [
    'positive', 'negative', 'pending_finalize', 'pending_servo', 'unchanged', 'reject',
    'duplicate', 'cancel', 'no_snapshot',
    'hold_recovery', 'hold_rebind', 'hold_stop', 'invalid_window',
])
def test_reference_publication(reference_publication_exe, scenario):
    result = subprocess.run([str(reference_publication_exe), scenario], capture_output=True,
                            text=True, timeout=20)
    (reference_publication_exe.parent/f'{scenario}.log').write_text(
        result.stdout+result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout+result.stderr


INPUTS = r'''
#include "vdc_reference.h"
#include "tdma_origin_plan.h"
static tdma_service_service_t reference_owner;
static tdma_service_service_t *s_vdc_tdma_service=&reference_owner;
static tdma_ring_clock_snapshot_t reference_ring;
static tdma_origin_raw_reference_t reference_origin;
static sync_io_reference_snapshot_t monitor;
static uint32_t s_reference_discipline_request=3, s_reference_discipline_capture_generation=9;
static uint32_t s_reference_discipline_armed[5]={1,1234,100,4,10000};
bool sync_io_reference_get_snapshot(sync_io_reference_snapshot_t *out){*out=monitor;return true;}
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{*out=reference_ring;return true;}
static bool tdma_runtime_owner_get_origin_raw_reference(tdma_origin_raw_reference_t *out)
{*out=reference_origin;return true;}
'''

SCENARIOS = r'''
static void next_window(void)
{
    audit_now_ms+=1040u;
    const uint64_t raw=(uint64_t)audit_now_ms*250000u;
    ++monitor.sample_seq;monitor.completed_raw=raw-100u;
    monitor.end_raw32=(uint32_t)(raw-1000u);monitor.elapsed_ticks=250000000u;
    reference_origin.timer_lower=reference_origin.timer_upper=raw-1000u;
    ++reference_origin.sequence;
}
static void setup_reference(void)
{
    assert(vdc_domain_init(&domain));
    assert(install_test_path_delay(&domain));
    vdc_domain_set_ready(&domain,true);
    for(unsigned i=1;i<=8;++i){
        vdc_tdma_timestamp_evidence_t e=make_hardware_sample(&domain.schedule,i,0);
        assert(vdc_domain_submit_tdma_evidence(&domain,&e));
    }
    assert(domain.clock.valid && domain.dco.valid);
    reference_ring.enabled=reference_ring.adapter_started=1u;
    reference_ring.local_slot_id=domain.schedule.local_slot_id;
    reference_ring.reference_slot_id=domain.schedule.reference_slot_id;
    reference_ring.config_seq=reference_ring.applied_config_seq=8u;
    reference_ring.schedule_crc32=domain.schedule.schedule_crc32;
    reference_origin.epoch=5u;reference_origin.published_version=2u;
    reference_origin.tick_hz=BOARD_SYS_CLOCK_HZ;
    monitor=(sync_io_reference_snapshot_t){.state=SYNC_IO_REFERENCE_RUNNING,.generation=9u,.valid=1u,
        .tick_hz=BOARD_SYS_CLOCK_HZ,.frequency_error_ppb=4000,.pio_bias_ticks=1u,
        .config={.window_ms=1000u,.timeout_ms=2500u}};
    publish();
    next_window();sync_dpll_fb_service();
    assert(s_reference_discipline.session==s_model_feedback_session);
    assert(s_reference_discipline.accepted==0u);
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *scenario=argv[1];setup_reference();
    if(!strncmp(scenario,"hold_",5) || !strcmp(scenario,"invalid_window")) {
        next_window();sync_dpll_fb_service();
        assert(s_reference_discipline.accepted==1u && domain.reference_baseline_ppb==100);
        const vdc_dco_control_t held=domain.dco;
        const vdc_clock_model_t held_clock=domain.clock;
        const uint32_t accepted=s_reference_discipline.accepted;
        const uint32_t applied=s_reference_discipline.applied;
        monitor.valid=0u;
        monitor.reason=!strcmp(scenario,"invalid_window")?SYNC_IO_REFERENCE_OK:SYNC_IO_REFERENCE_TIMEOUT;
        for(unsigned gap=0;gap<5;++gap) {
            audit_now_ms+=3000u;sync_dpll_fb_service();
            assert(s_reference_discipline.state==VDC_REFERENCE_DISCIPLINE_HOLD);
            assert(s_reference_discipline.accepted==accepted && s_reference_discipline.applied==applied);
            assert(!memcmp(&held,&domain.dco,sizeof(held)));
            assert(!memcmp(&held_clock,&domain.clock,sizeof(held_clock)));
        }
        if(!strcmp(scenario,"hold_stop")) {
            s_model_feedback_session=0u;sync_dpll_fb_service();
            assert(s_reference_discipline.state==VDC_REFERENCE_DISCIPLINE_RETIRED);
            s_model_feedback_session=s_reference_discipline.session;
        }
        if(!strcmp(scenario,"hold_rebind"))++reference_origin.epoch;
        /* Re-exposing the last completed window is not restored input. */
        monitor.valid=1u;monitor.reason=SYNC_IO_REFERENCE_OK;
        sync_dpll_fb_service();
        assert(s_reference_discipline.accepted==accepted);
        assert(!memcmp(&held,&domain.dco,sizeof(held)));
        monitor.valid=1u;monitor.reason=SYNC_IO_REFERENCE_OK;next_window();sync_dpll_fb_service();
        if(!strcmp(scenario,"hold_stop") || !strcmp(scenario,"hold_rebind")) {
            assert(s_reference_discipline.state==VDC_REFERENCE_DISCIPLINE_RETIRED);
            assert(domain.reference_baseline_ppb==100 && s_reference_discipline.accepted==accepted);
            next_window();sync_dpll_fb_service();assert(!memcmp(&held,&domain.dco,sizeof(held)));
        } else {
            assert(s_reference_discipline.state==VDC_REFERENCE_DISCIPLINE_TRACK);
            assert(s_reference_discipline.accepted==accepted+1u && domain.reference_baseline_ppb==200);
            assert(domain.dco.dco_update_seq==held.dco_update_seq+1u);
            assert(!memcmp(&s_published_snapshot.dco,&domain.dco,sizeof(domain.dco)));
            vdc_dpll_manager_refresh_dco_consumer_status_core0();vectors();
            assert(s_dco_consumer_status.last_dco_update_seq==domain.dco.dco_update_seq);
            assert(dpll_region.payload.dco_update_seq==domain.dco.dco_update_seq);
        }
        return 0;
    }
    vdc_tdma_evidence_preparation_t preparation;
    vdc_tdma_timestamp_evidence_t evidence=make_hardware_sample(&domain.schedule,9u,10);
    next_window();
    const bool pending_finalize=!strcmp(scenario,"pending_finalize");
    const bool pending_servo=!strcmp(scenario,"pending_servo");
    if(pending_finalize || pending_servo){
        assert(vdc_domain_prepare_active_tdma_evidence(&domain,&evidence,&preparation));
        assert(preparation.accepted);
        preparation.local_apply_time_ns=(uint64_t)audit_now_ms*1000000u;
        if(pending_finalize)
            assert(vdc_domain_apply_prepared_tdma_evidence_servo(&domain,&evidence,&preparation));
    }
    if(!strcmp(scenario,"negative"))monitor.frequency_error_ppb=-4000;
    if(!strcmp(scenario,"unchanged"))monitor.frequency_error_ppb=0;
    if(!strcmp(scenario,"reject"))monitor.frequency_error_ppb=10001;
    if(!strcmp(scenario,"duplicate"))monitor.sample_seq=s_reference_discipline.sample_seq;
    if(!strcmp(scenario,"cancel"))s_reference_discipline_request=4;
    if(!strcmp(scenario,"no_snapshot"))s_published_snapshot_valid=false;
    /* Isolate the reference write from the independently tested age path. */
    vdc_dpll_manager_age_quality_core1();
    const vdc_domain_snapshot_t before=s_published_snapshot;
    const uint32_t revision=s_published_snapshot_guard;
    const uint32_t service_count=domain.service_count;
    const uint32_t old_seq=domain.dco.dco_update_seq;
    const vdc_quality_table_t quality=domain.quality;
    s_dpll_capture_armed=true;s_dpll_capture_last_update_seq=before.dpll.update_seq;
    sync_dpll_fb_service();
    assert(!memcmp(&domain.quality,&quality,sizeof(quality)));
    assert(domain.service_count==service_count && s_dpll_capture_count==0u);
    const bool changed=!strcmp(scenario,"positive") || !strcmp(scenario,"negative") ||
                       pending_finalize || pending_servo || !strcmp(scenario,"no_snapshot");
    assert(domain.dco.dco_update_seq==old_seq+(changed?1u:0u));
    vdc_domain_snapshot_t expected=before;
    if(changed && strcmp(scenario,"no_snapshot")){
        expected.clock=domain.clock;expected.dco=domain.dco;
        assert(s_published_snapshot_guard==revision+2u);
    }else assert(s_published_snapshot_guard==revision);
    assert(!memcmp(&s_published_snapshot,&expected,sizeof(expected)));
    if(!strcmp(scenario,"no_snapshot")){
        assert(!s_published_snapshot_valid);
        return 0;
    }
    vdc_dpll_manager_refresh_dco_consumer_status_core0();vectors();
    assert(s_dco_consumer_status.last_dco_update_seq==domain.dco.dco_update_seq);
    assert(s_dco_consumer_status.period_adjust_ppb==domain.dco.period_adjust_ppb);
    assert(refmem_dpll_vector_payload_validate(&dpll_region.payload));
    assert(refmem_vdc_vector_payload_validate(&vdc_region.payload));
    assert(dpll_region.payload.dco_update_seq==domain.dco.dco_update_seq);
    assert(dpll_region.payload.dco_period_adjust_ppb==domain.dco.period_adjust_ppb);
    assert(vdc_region.payload.clock_model_seq==domain.clock.model_seq);
    assert(vdc_region.payload.clock_period_adjust_ppb==domain.clock.period_adjust_ppb);
    assert(dpll_region.payload.source_update_seq==before.dpll.update_seq);
    assert(vdc_region.payload.source_update_seq==before.dpll.update_seq);
    vdc_dpll_manager_committed_model_t model;
    assert(vdc_dpll_manager_get_committed_model(&model));
    assert(!memcmp(&model.dco,&domain.dco,sizeof(model.dco)));
    const uint32_t vdc_sequence=vdc_region.seqlock, dpll_sequence=dpll_region.seqlock;
    const uint32_t core_services=s_dco_consumer_status.service_count;
    vdc_dpll_manager_refresh_dco_consumer_status_core0();
    assert(s_dco_consumer_status.service_count==core_services);
    vectors();
    assert(vdc_region.seqlock==vdc_sequence && dpll_region.seqlock==dpll_sequence);
    if(pending_finalize || pending_servo){
        const int32_t baseline=domain.reference_baseline_ppb;
        if(pending_servo)
            assert(vdc_domain_apply_prepared_tdma_evidence_servo(&domain,&evidence,&preparation));
        bool accepted=false;
        assert(vdc_domain_apply_prepared_tdma_evidence_state(&domain,&evidence,&preparation,&accepted));
        assert(accepted);
        assert(vdc_domain_finalize_prepared_tdma_evidence(&domain,&evidence,&preparation));
        vdc_dpll_manager_publish_runtime_snapshot_locked();
        assert(s_dpll_capture_count==1u);
        assert(s_published_snapshot.dco.period_adjust_ppb==domain.dco.period_adjust_ppb);
        assert(s_published_snapshot.quality.update_seq==before.quality.update_seq+1u);
        assert(domain.reference_baseline_ppb==baseline && baseline==100);
    }
    puts("reference publication passed");return 0;
}
'''
