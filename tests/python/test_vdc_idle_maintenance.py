"""Real Domain/dispatcher/publication with idle input and control transitions.

External owners and the clock are simulated; no physical timing is claimed.
VDC_IDLE_BASELINE selects an old manager revision to reproduce missed updates.
"""
import os
import re
import subprocess

import pytest

from test_vdc_command_owner import ROOT, MANAGER, compile_executable
from test_vdc_priority_follow import domain_sources
from test_vdc_publication_generation import publication_exe


def definition(source, name):
    match = re.search(
        rf'(?m)^(?:static\s+)?(?:__attribute__\(\([^\n]*\)\)\s+)?'
        rf'(?:bool|void|uint32_t|uint64_t|vdc_tdma_timestamp_evidence_t)\s+(?:__attribute__\(\([^\n]*\)\)\s+)?'
        rf'(?:[A-Z_]+\(\s*)?{name}\s*\)?\s*\([^;{{}}]*\)\s*\{{', source)
    assert match, name
    cursor, depth = match.end(), 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[match.start():cursor]


@pytest.fixture(scope='module')
def idle_exe(publication_exe, tmp_path_factory):
    source = publication_exe.with_suffix('.c').read_text(encoding='utf-8')
    source = source.replace('int main(int argc, char **argv)',
                            'int publication_regression_main(int argc, char **argv)', 1)
    source = source.replace('static uint32_t board_uptime_ms(void) { return 100u; }',
        'static uint32_t audit_now_ms=1000u;\n'
        'static uint32_t board_uptime_ms(void) { return audit_now_ms; }')
    baseline = os.environ.get('VDC_IDLE_BASELINE')
    manager = (subprocess.check_output(['git', 'show', f'{baseline}:{MANAGER.relative_to(ROOT).as_posix()}'],
        cwd=ROOT, text=True, encoding='utf-8') if baseline else MANAGER.read_text(encoding='utf-8'))
    model = (MANAGER.parent/'vdc_model_feedback.inc').read_text(encoding='utf-8')
    source += STORAGE
    source += '\n'.join(re.findall(r'^#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_\w+\s+\d+u', manager, re.M)) + '\n'
    for name in ('model_feedback_load', 'model_feedback_end_core1',
                 'vdc_dpll_manager_feedback_session', 'vdc_dpll_manager_get_committed_model'):
        source += definition(model, name) + '\n'
    source += DEPENDENCIES
    for name in ('vdc_dpll_manager_now_ns', 'vdc_dpll_manager_publish_runtime_snapshot_locked',
                 'vdc_dpll_manager_set_vdc_ready', 'sync_dpll_fb_step',
                 'vdc_dpll_manager_age_quality_core1', 'sync_dpll_fb_service'):
        if name == 'vdc_dpll_manager_age_quality_core1' and name not in manager:
            continue
        if name == 'vdc_dpll_manager_now_ns':
            source += '#define PICO_ON_DEVICE 1\n' + definition(manager, name) + '\n#undef PICO_ON_DEVICE\n'
        else:
            source += definition(manager, name) + '\n'
    source += definition((ROOT/'tests/unit/test_vdc_domain.c').read_text(encoding='utf-8'),
                         'make_hardware_sample') + '\n'
    source += CASES
    return compile_executable(tmp_path_factory.mktemp('vdc-idle'), 'idle', source,
        domain_sources() + [ROOT/'components/distributed_refmem/src/refmem_vector_table.c'])


@pytest.mark.parametrize('scenario', [
    'idle', 'early_role', 'early_debug', 'early_tune', 'pending_service',
    'ready', 'ready_no_session', 'ready_early_role', 'no_reference',
    'threshold', 'saturation', 'backwards', 'clock_invalid', 'holdover',
    'unfinalized', 'unpublished_reference', 'unchanged', 'four_beat',
])
def test_idle_publication(idle_exe, scenario):
    result = subprocess.run([str(idle_exe), scenario], capture_output=True, text=True, timeout=20)
    (idle_exe.parent/f'{scenario}.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout+result.stderr


STORAGE = r'''
#define s_vdc_domain domain
#define VDC_COMMITTED_MODEL_HOT(name) name
#define BOARD_SYS_CLOCK_HZ 250000000u
static uint32_t s_model_feedback_session=123u;
static uint32_t s_committed_model_guard, s_committed_model_serial;
static uint32_t s_committed_model_words[sizeof(vdc_dpll_manager_committed_model_t)/4u];
static bool s_vdc_ready=true;
static vdc_dpll_manager_vdc_status_t s_vdc_status, s_published_vdc_status;
static bool s_vdc_domain_service_pending;
static bool debug_pending, role_pending, tune_pending;
static bool clock_valid=true;
static bool s_dpll_capture_armed, s_dpll_capture_complete, s_dpll_capture_auto_only;
static uint32_t s_dpll_capture_count, s_dpll_capture_dropped;
static uint32_t s_dpll_capture_first_update_seq, s_dpll_capture_last_update_seq;
static uint32_t s_dpll_capture_start_ms, s_dpll_capture_end_ms;
static uint32_t s_vdc_follower_capture_kind_hint;
static vdc_dpll_manager_dpll_capture_record_t
    s_dpll_capture_records[VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES];
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=(uint64_t)audit_now_ms*(hz/1000u);return true; }
bool vdc_timestamp_clock_try_read_ns(uint32_t hz,uint64_t *out)
{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=(uint64_t)audit_now_ms*1000000u;return clock_valid; }
'''

DEPENDENCIES = r'''
static void priority_follow_health_service_core1(void) {}
static void osal_critical_enter(void) {}
static void osal_critical_exit(void) {}
static bool vdc_dpll_manager_apply_pending_debug_continue(void) { return debug_pending; }
static bool vdc_dpll_manager_apply_pending_dpll_role(void) { return false; }
void vdc_dpll_manager_get_dpll_role_status(vdc_dpll_manager_dpll_role_status_t *out)
{ memset(out,0,sizeof(*out));out->pending=role_pending; }
static bool vdc_dpll_manager_apply_pending_debug_servo_tune(void) { return tune_pending; }
static void vdc_dpll_manager_feedback_match_retire(void) {}
static void vdc_dpll_manager_feedback_match_service(void) {}
static void vdc_dpll_manager_consume_follower_command(void) {}
static bool vdc_dpll_manager_finalize_ring_evidence(void) { return false; }
static bool vdc_dpll_manager_prepare_ring_evidence(void) { return false; }
static bool vdc_dpll_manager_apply_ring_evidence(void) { return false; }
static void priority_trace_service_core1(void) {}
static void priority_guard_service_core1(void) {}
static void vdc_priority_match_core1(void) {}
static void priority_trace_match_core1(void) {}
static void priority_follow_prepare_core1(void) {}
static void vdc_priority_ingress_core1(void) {}
static void vdc_boundary_service_core1(void) {}
static void priority_follow_apply_core1(void) {}
static void priority_summary_service(bool end) { (void)end; }
static void reference_discipline_service_core1(uint32_t session) { (void)session; }
'''

CASES = r'''
static void initial_state(void)
{
    fixture(&domain);
    domain.dpll.state=domain.dco.lock_state=VDC_DOMAIN_LOCK_LOCKED;
    domain.dpll.update_seq=17u;
    domain.quality.accepted_sample_count=32u;
    domain.quality.consecutive_fine_samples=UINT32_MAX;
    domain.quality.last_sample_time_ns=UINT64_C(1000000000);
    domain.quality.freshness_limit_us=1000u;
    domain.gate.passed=1u;
    vdc_domain_service(&domain,UINT64_C(1000000000));
    assert(domain.quality.health_state==VDC_DOMAIN_HEALTH_HEALTHY);
    evidence_seq=domain.dpll.update_seq;
    original_clock=domain.clock;
    publish();
    vdc_dpll_manager_publish_runtime_snapshot_locked();
    sync_dpll_fb_service();
    vdc_dpll_manager_refresh_dco_consumer_status_core0();vectors();
}

int main(int argc,char **argv)
{
    assert(argc==2);const char *name=argv[1];initial_state();
    const uint32_t initial_guard=s_published_snapshot_guard;
    const uint32_t initial_service_count=domain.service_count;
    const uint32_t initial_evidence_seq=domain.dpll.update_seq;
    const vdc_dco_control_t initial_dco=domain.dco;
    const vdc_clock_model_t initial_clock=domain.clock;
    vdc_dpll_manager_committed_model_t initial_model;
    assert(vdc_dpll_manager_get_committed_model(&initial_model));
    if(!strcmp(name,"four_beat")) {
        vdc_dpll_control_profile_t role=domain.control.profile;
        role.mode=VDC_DPLL_CONTROL_MODE_MASTER;
        assert(vdc_domain_set_dpll_control_profile(&domain,&role));
        vdc_dpll_manager_publish_runtime_snapshot_locked();
        const uint32_t published_seq=s_published_snapshot.dpll.update_seq;
        const uint32_t quality_seq=domain.quality.update_seq;
        vdc_tdma_timestamp_evidence_t evidence=make_hardware_sample(&domain.schedule,2u,0);
        vdc_tdma_evidence_preparation_t preparation;
        assert(vdc_domain_prepare_active_tdma_evidence(&domain,&evidence,&preparation));
        assert(preparation.accepted);
        preparation.local_apply_time_ns=UINT64_C(1200000000);
        assert(vdc_domain_apply_prepared_tdma_evidence_servo(&domain,&evidence,&preparation));
        assert(domain.quality.update_seq==quality_seq);
        s_dpll_capture_armed=true;s_dpll_capture_last_update_seq=published_seq;
        audit_now_ms=1200u;sync_dpll_fb_service();
        assert(s_published_snapshot.dpll.update_seq==published_seq);
        assert(s_published_snapshot.quality.update_seq==quality_seq);
        assert(s_dpll_capture_count==0u);
        bool accepted=false;
        assert(vdc_domain_apply_prepared_tdma_evidence_state(&domain,&evidence,&preparation,&accepted));
        assert(accepted);
        assert(vdc_domain_finalize_prepared_tdma_evidence(&domain,&evidence,&preparation));
        vdc_dpll_manager_publish_runtime_snapshot_locked();
        assert(domain.quality.update_seq==quality_seq+1u);
        assert(s_dpll_capture_count==1u);
        s_vdc_domain_service_pending=true;
        audit_now_ms=1202u;sync_dpll_fb_service();
        assert(s_dpll_capture_count==1u);
        assert(domain.service_count==initial_service_count+1u);
    } else if(!strncmp(name,"ready",5)) {
        if(!strcmp(name,"ready_no_session"))s_model_feedback_session=0u;
        role_pending=!strcmp(name,"ready_early_role");
        vdc_dpll_manager_set_vdc_ready(false);
        ++audit_now_ms;sync_dpll_fb_service();
        vdc_dpll_manager_refresh_dco_consumer_status_core0();vectors();
        assert(!domain.ready && domain.dco.lock_state==VDC_DOMAIN_LOCK_OFF);
        assert(domain.quality.lock_state==VDC_DOMAIN_LOCK_OFF);
        assert(domain.quality.health_state==VDC_DOMAIN_HEALTH_CHECKING);
        assert(s_published_snapshot.ready==0u);
        assert(s_published_snapshot.dco.lock_state==VDC_DOMAIN_LOCK_OFF);
        assert(s_published_snapshot.quality.health_state==VDC_DOMAIN_HEALTH_CHECKING);
        assert(s_dco_consumer_status.lock_state==VDC_DOMAIN_LOCK_OFF);
        assert(dpll_region.payload.ready==0u);
        role_pending=false;
        uint32_t stopped_guard=s_published_snapshot_guard;
        vdc_dpll_manager_set_vdc_ready(false);sync_dpll_fb_service();
        assert(stopped_guard==s_published_snapshot_guard);
    } else if(!strcmp(name,"threshold") || !strcmp(name,"saturation") ||
              !strcmp(name,"backwards") || !strcmp(name,"clock_invalid")) {
        vdc_quality_table_t q=domain.quality, old=q;
        uint64_t now=UINT64_C(1000000000);
        if(!strcmp(name,"threshold")) {
            assert(vdc_domain_age_quality(&q,VDC_DOMAIN_LOCK_LOCKED,true,now+999999u));
            assert(q.last_sample_age_us==999u && q.health_state==VDC_DOMAIN_HEALTH_HEALTHY);
            assert(vdc_domain_age_quality(&q,VDC_DOMAIN_LOCK_LOCKED,true,now+1000000u));
            assert(q.last_sample_age_us==1000u && q.health_state==VDC_DOMAIN_HEALTH_HEALTHY);
            assert(vdc_domain_age_quality(&q,VDC_DOMAIN_LOCK_LOCKED,true,now+1001000u));
            assert(q.health_state==VDC_DOMAIN_HEALTH_DEGRADED);
        } else if(!strcmp(name,"saturation")) {
            assert(vdc_domain_age_quality(&q,VDC_DOMAIN_LOCK_LOCKED,true,UINT64_MAX-1));
            assert(q.last_sample_age_us==UINT32_MAX);
        } else if(!strcmp(name,"backwards")) {
            (void)vdc_domain_age_quality(&q,VDC_DOMAIN_LOCK_LOCKED,true,now-1);
            assert(q.last_sample_age_us==0);
        } else {
            assert(!vdc_domain_age_quality(&q,VDC_DOMAIN_LOCK_LOCKED,true,UINT64_MAX));
            assert(!memcmp(&q,&old,sizeof(q)));
            clock_valid=false;audit_now_ms=1200u;sync_dpll_fb_service();
            assert(s_published_snapshot_guard==initial_guard);
            assert(domain.quality.last_sample_age_us==0u);
        }
    } else if(!strcmp(name,"no_reference") || !strcmp(name,"unchanged")) {
        if(!strcmp(name,"no_reference")) {
            domain.quality.last_sample_time_ns=0;
            vdc_dpll_manager_publish_runtime_snapshot_locked();
        }
        uint32_t guard=s_published_snapshot_guard;
        for(unsigned i=0;i<100;++i) {
            if(!strcmp(name,"no_reference"))audit_now_ms+=2;
            sync_dpll_fb_service();
        }
        assert(s_published_snapshot_guard==guard);
        assert(domain.service_count==initial_service_count);
    } else {
        role_pending=!strcmp(name,"early_role");
        debug_pending=!strcmp(name,"early_debug");
        tune_pending=!strcmp(name,"early_tune");
        if(!strcmp(name,"holdover")) {
            domain.dpll.state=domain.dco.lock_state=VDC_DOMAIN_LOCK_HOLDOVER;
            vdc_domain_service(&domain,UINT64_C(1000000000));
            vdc_dpll_manager_publish_runtime_snapshot_locked();
        }
        if(!strcmp(name,"unfinalized")) {
            commit(false); /* Actual Domain DCO change, not yet runtime-published. */
            ++domain.dpll.update_seq;
            s_dpll_capture_armed=true;
        }
        if(!strcmp(name,"unpublished_reference")) {
            domain.quality.last_sample_time_ns=UINT64_C(1199000000);
        }
        audit_now_ms+=200u;
        s_vdc_domain_service_pending=!strcmp(name,"pending_service");
        sync_dpll_fb_service();
        vdc_dpll_manager_refresh_dco_consumer_status_core0();vectors();
        assert(s_published_snapshot_guard!=initial_guard);
        assert(s_published_snapshot.quality.last_sample_age_us==200000u);
        assert(s_published_snapshot.quality.health_state==VDC_DOMAIN_HEALTH_DEGRADED);
        assert(dpll_region.payload.quality_last_sample_age_us==200000u);
        assert(vdc_region.payload.quality_last_sample_age_us==200000u);
        assert(domain.quality.last_sample_age_us==(!strcmp(name,"unpublished_reference")?1000u:200000u));
        if(!strcmp(name,"unfinalized")) {
            assert(s_dpll_capture_count==0);
            assert(s_published_snapshot.dpll.update_seq==initial_evidence_seq);
            assert(!memcmp(&s_published_snapshot.dco,&initial_dco,sizeof(initial_dco)));
        } else if(!strcmp(name,"holdover")) {
            assert(domain.dpll.state==VDC_DOMAIN_LOCK_HOLDOVER);
            assert(domain.dpll.holdover_age_us==200000u);
            assert(s_published_snapshot.dpll.holdover_age_us==200000u);
        } else {
            assert(!memcmp(&initial_dco,&domain.dco,sizeof(initial_dco)));
            assert(!memcmp(&initial_clock,&domain.clock,sizeof(initial_clock)));
            assert(initial_evidence_seq==domain.dpll.update_seq);
            vdc_dpll_manager_committed_model_t current;
            assert(vdc_dpll_manager_get_committed_model(&current));
            assert(current.token==initial_model.token);
        }
        assert(domain.service_count==initial_service_count+
            (!strcmp(name,"pending_service") || !strcmp(name,"holdover") ? 1u:0u));
    }
    puts("idle maintenance passed");return 0;
}
'''
