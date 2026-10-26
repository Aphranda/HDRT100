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
from vdc_units import fragment_slice


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
    # The fragment moved into vdc_dpll_feedback.c; extract the same functions
    # from the live unit (which shares the storage this harness declares).
    model = fragment_slice("vdc_model_feedback")
    source += STORAGE
    source += '\n'.join(re.findall(r'^#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_\w+\s+\d+u', manager, re.M)) + '\n'
    for name in ('model_feedback_load', 'model_feedback_end_core1',
                 'vdc_dpll_manager_feedback_session', 'vdc_dpll_manager_get_committed_model'):
        source += definition(model, name) + '\n'
    source += DEPENDENCIES
    for name in ('vdc_dpll_manager_now_ns', 'vdc_dpll_manager_publish_runtime_snapshot_locked',
                 'vdc_dpll_manager_set_vdc_ready', 'vdc_dpll_manager_finalize_ring_evidence', 'sync_dpll_fb_step',
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
    'invalid_recovery', 'age_regression', 'fault_invalid', 'max_limit',
    'domain_service_latched', 'accepted_recovery', 'rejected_latched',
    'invalid_no_sample', 'invalid_holdover', 'invalid_repeat',
    'invalid_unfinalized', 'split_anchor',
    'pending_invalid', 'pending_rollback', 'first_pending_invalid', 'first_pending_rollback',
    'published_only_rollback',
])
def test_idle_publication(idle_exe, scenario):
    result = subprocess.run([str(idle_exe), scenario], capture_output=True, text=True, timeout=20)
    (idle_exe.parent/f'{scenario}.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout+result.stderr


STORAGE = r'''
#define s_vdc_domain domain
#define VDC_COMMITTED_MODEL_HOT(name) name
#define VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK(model) ((void)(model))
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
static bool s_vdc_ring_finalization_pending;
static vdc_tdma_evidence_preparation_t s_vdc_ring_preparation;
static vdc_tdma_timestamp_evidence_t s_vdc_ring_pending_evidence;
static vdc_dpll_manager_ring_observer_status_t s_ring_observer_status;
static vdc_dpll_manager_dpll_capture_record_t
    s_dpll_capture_records[VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES];
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=(uint64_t)audit_now_ms*(hz/1000u);return true; }
bool vdc_timestamp_clock_try_read_ns(uint32_t hz,uint64_t *out)
{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=(uint64_t)audit_now_ms*1000000u;return clock_valid; }
'''

DEPENDENCIES = r'''
/* The extracted manager code sees the unit headers, which declare these as
 * external entry points; define them with matching linkage. */
void priority_follow_health_service_core1(void) {}
static void osal_critical_enter(void) {}
static void osal_critical_exit(void) {}
bool vdc_dpll_manager_apply_pending_debug_continue(void) { return debug_pending; }
bool vdc_dpll_manager_apply_pending_dpll_role(void) { return false; }
void vdc_dpll_manager_get_dpll_role_status(vdc_dpll_manager_dpll_role_status_t *out)
{ memset(out,0,sizeof(*out));out->pending=role_pending; }
bool vdc_dpll_manager_apply_pending_debug_servo_tune(void) { return tune_pending; }
void vdc_dpll_manager_feedback_match_retire(void) {}
void vdc_dpll_manager_feedback_match_service(void) {}
void vdc_dpll_manager_consume_follower_command(void) {}
bool vdc_dpll_manager_finish_ring_observer_service(
    const vdc_dpll_manager_ring_observer_status_t *status,bool consumed)
{ assert(consumed);s_ring_observer_status=*status;return true; }
bool vdc_dpll_manager_prepare_ring_evidence(void) { return false; }
bool vdc_dpll_manager_apply_ring_evidence(void) { return false; }
void priority_trace_service_core1(void) {}
void priority_guard_service_core1(void) {}
void vdc_priority_match_core1(void) {}
void priority_trace_match_core1(void) {}
void priority_follow_prepare_core1(void) {}
void vdc_priority_ingress_core1(void) {}
void vdc_boundary_service_core1(void) {}
void priority_follow_apply_core1(void) {}
void priority_summary_service(bool end) { (void)end; }
void reference_discipline_service_core1(uint32_t session) { (void)session; }
/* Split-unit entry points the extracted service wrapper calls. */
void vdc_priority_owner_arm_core1(uint32_t session) { (void)session; }
void vdc_dpll_manager_commit_lock_declaration_core1(void) {}
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
    if(!strcmp(name,"published_only_rollback")) {
        audit_now_ms=1002u;sync_dpll_fb_service();
        assert(s_published_snapshot.quality.last_sample_age_us==2000u);
        /* Unpublished working evidence can look fresh while the committed
         * reference alone demonstrates rollback of the shared time base. */
        domain.quality.last_sample_time_ns=UINT64_C(1001000000);
        domain.quality.last_sample_age_us=0u;
        domain.control.profile.mode=VDC_DPLL_CONTROL_MODE_MASTER;
        s_vdc_ring_pending_evidence=make_hardware_sample(&domain.schedule,2u,0);
        assert(vdc_domain_prepare_active_tdma_evidence(&domain,&s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation));
        s_vdc_ring_preparation.local_apply_time_ns=UINT64_C(1001000000);
        assert(vdc_domain_apply_prepared_tdma_evidence_servo(&domain,&s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation));
        s_vdc_ring_finalization_pending=true;
        audit_now_ms=1001u;vdc_dpll_manager_age_quality_core1();
        assert(domain.quality.last_sample_age_us==UINT32_MAX);
        assert(s_published_snapshot.quality.last_sample_age_us==UINT32_MAX);
        assert(s_published_snapshot.quality.last_sample_time_ns==UINT64_C(1000000000));
        assert(s_published_snapshot.dpll.update_seq==initial_evidence_seq);
        assert(!memcmp(&s_published_snapshot.dco,&initial_dco,sizeof(initial_dco)));
        assert(!s_vdc_ring_preparation.valid);
        assert(vdc_dpll_manager_finalize_ring_evidence());
        assert(!s_vdc_ring_finalization_pending && s_ring_observer_status.rejected_count==1u);
        /* The invalid old publication must not poison genuinely new accepted
         * evidence when the clock becomes ordered again. */
        s_vdc_ring_pending_evidence=make_hardware_sample(&domain.schedule,3u,0);
        assert(vdc_domain_prepare_active_tdma_evidence(&domain,&s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation));
        s_vdc_ring_preparation.local_apply_time_ns=UINT64_C(1003000000);
        assert(vdc_domain_apply_prepared_tdma_evidence_servo(&domain,&s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation));
        s_vdc_ring_finalization_pending=true;audit_now_ms=1003u;sync_dpll_fb_service();
        assert(s_ring_observer_status.accepted_count==1u && !s_vdc_ring_finalization_pending);
        assert(domain.quality.last_sample_time_ns==UINT64_C(1003000000));
        assert(domain.quality.last_sample_age_us==0u);
        assert(s_published_snapshot.quality.last_sample_age_us==0u);
    } else if(!strcmp(name,"pending_invalid") || !strcmp(name,"pending_rollback") ||
       !strncmp(name,"first_pending_",14u)) {
        const bool first=!strncmp(name,"first_",6u);
        /* Fixture persona only: the public role setter resets evidence,
         * which would hide the retained pre-failure sample under test. */
        domain.control.profile.mode=VDC_DPLL_CONTROL_MODE_MASTER;
        domain.dpll.accepted_sample_count=domain.quality.accepted_sample_count;
        if(first) {
            domain.quality.last_sample_time_ns=0u;
            domain.quality.last_sample_age_us=0u;
            domain.quality.accepted_sample_count=0u;
            domain.dpll.accepted_sample_count=0u;
            domain.quality.health_state=VDC_DOMAIN_HEALTH_CHECKING;
        }
        vdc_dpll_manager_publish_runtime_snapshot_locked();
        s_vdc_ring_pending_evidence=make_hardware_sample(&domain.schedule,2u,0);
        assert(vdc_domain_prepare_active_tdma_evidence(&domain,&s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation));
        assert(s_vdc_ring_preparation.accepted);
        s_vdc_ring_preparation.local_apply_time_ns=UINT64_C(1002000000);
        assert(vdc_domain_apply_prepared_tdma_evidence_servo(&domain,&s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation));
        s_vdc_ring_finalization_pending=true;
        const uint32_t accepted_before=domain.quality.accepted_sample_count;
        const uint32_t observer_rejected=s_ring_observer_status.rejected_count;
        audit_now_ms=1001u;
        clock_valid=strstr(name,"rollback")!=NULL;
        sync_dpll_fb_service();
        assert(!s_vdc_ring_finalization_pending && !s_vdc_ring_preparation.valid);
        assert(s_ring_observer_status.rejected_count==observer_rejected+1u);
        assert(domain.quality.accepted_sample_count==accepted_before);
        assert(domain.quality.health_state!=VDC_DOMAIN_HEALTH_HEALTHY);
        assert(s_published_snapshot.quality.health_state!=VDC_DOMAIN_HEALTH_HEALTHY);
        assert(domain.quality.last_sample_time_ns==(first ? 0u : UINT64_C(1000000000)));
        if(!first) assert(domain.quality.last_sample_age_us==UINT32_MAX);
        clock_valid=true;audit_now_ms=1003u;sync_dpll_fb_service();
        assert(s_ring_observer_status.rejected_count==observer_rejected+1u);
        assert(domain.quality.accepted_sample_count==accepted_before);
        assert(domain.quality.health_state!=VDC_DOMAIN_HEALTH_HEALTHY);
        s_vdc_ring_pending_evidence=make_hardware_sample(&domain.schedule,3u,0);
        assert(vdc_domain_prepare_active_tdma_evidence(&domain,&s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation));
        assert(s_vdc_ring_preparation.accepted);
        s_vdc_ring_preparation.local_apply_time_ns=UINT64_C(1003000000);
        assert(vdc_domain_apply_prepared_tdma_evidence_servo(&domain,&s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation));
        s_vdc_ring_finalization_pending=true;
        sync_dpll_fb_service();
        assert(!s_vdc_ring_finalization_pending);
        assert(s_ring_observer_status.accepted_count==1u);
        /* Servo counters already include the cancelled servo operation;
         * observer acceptance and the refreshed anchor prove this recovery. */
        assert(domain.quality.accepted_sample_count>accepted_before);
        assert(domain.quality.accepted_sample_count==domain.dpll.accepted_sample_count);
        assert(domain.quality.last_sample_time_ns==UINT64_C(1003000000));
        assert(domain.quality.last_sample_age_us==0u);
        assert(s_published_snapshot.quality.last_sample_age_us==0u);
    } else if(!strcmp(name,"four_beat")) {
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
        vdc_quality_table_t q=domain.quality;
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
            assert(vdc_domain_age_quality(&q,VDC_DOMAIN_LOCK_LOCKED,true,now-1));
            assert(q.last_sample_age_us==UINT32_MAX && q.health_state==VDC_DOMAIN_HEALTH_DEGRADED);
        } else {
            assert(vdc_domain_age_quality(&q,VDC_DOMAIN_LOCK_LOCKED,true,UINT64_MAX));
            assert(q.last_sample_age_us==UINT32_MAX && q.health_state==VDC_DOMAIN_HEALTH_DEGRADED);
            clock_valid=false;audit_now_ms=1200u;sync_dpll_fb_service();
            assert(s_published_snapshot_guard!=initial_guard);
            assert(domain.quality.last_sample_age_us==UINT32_MAX);
            assert(s_published_snapshot.quality.last_sample_age_us==UINT32_MAX);
            vectors();
            /* VDC-LOCKPUB-002: the vector lock bit is the owner's committed
             * declaration, not a local verdict. Degrading quality must not
             * move it; the verdict itself is covered by test_vdc_lock_verdict. */
            assert(vdc_region.payload.flags & REFMEM_VECTOR_FLAG_LOCKED);
            assert(dpll_region.payload.flags & REFMEM_VECTOR_FLAG_LOCKED);
        }
    } else if(!strcmp(name,"invalid_recovery") || !strcmp(name,"age_regression") ||
              !strcmp(name,"fault_invalid") || !strcmp(name,"max_limit") ||
              !strcmp(name,"domain_service_latched") || !strcmp(name,"accepted_recovery") ||
              !strcmp(name,"rejected_latched") || !strcmp(name,"invalid_no_sample") ||
              !strcmp(name,"invalid_holdover") || !strcmp(name,"invalid_repeat") ||
              !strcmp(name,"invalid_unfinalized") || !strcmp(name,"split_anchor")) {
        if(!strcmp(name,"invalid_no_sample")) {
            domain.quality.last_sample_time_ns=0u;
            vdc_dpll_manager_publish_runtime_snapshot_locked();
            const vdc_quality_table_t old=domain.quality;
            const uint32_t guard=s_published_snapshot_guard;
            clock_valid=false;sync_dpll_fb_service();
            assert(!memcmp(&old,&domain.quality,sizeof(old)));
            assert(s_published_snapshot_guard==guard);return 0;
        }
        if(!strcmp(name,"split_anchor")) {
            domain.quality.last_sample_time_ns=UINT64_C(1200000000);
            audit_now_ms=1100u;sync_dpll_fb_service();
            assert(domain.quality.last_sample_age_us==UINT32_MAX);
            assert(s_published_snapshot.quality.last_sample_time_ns==UINT64_C(1000000000));
            assert(s_published_snapshot.quality.last_sample_age_us==UINT32_MAX);
            assert(s_published_snapshot.dpll.update_seq==initial_evidence_seq);return 0;
        }
        if(!strcmp(name,"invalid_holdover")) {
            domain.dpll.state=domain.dco.lock_state=VDC_DOMAIN_LOCK_HOLDOVER;
            vdc_dpll_manager_publish_runtime_snapshot_locked();
        }
        if(!strcmp(name,"fault_invalid")) {
            domain.dpll.state=domain.dco.lock_state=VDC_DOMAIN_LOCK_FAULT;
            domain.quality.health_state=VDC_DOMAIN_HEALTH_FAULT;
            vdc_dpll_manager_publish_runtime_snapshot_locked();
        }
        if(!strcmp(name,"max_limit")) {
            domain.quality.freshness_limit_us=UINT32_MAX;
            vdc_dpll_manager_publish_runtime_snapshot_locked();
        }
        if(!strcmp(name,"invalid_unfinalized")) {
            commit(false);++domain.dpll.update_seq;s_dpll_capture_armed=true;
        }
        if(!strcmp(name,"age_regression")) {
            audit_now_ms=1001u;sync_dpll_fb_service();
            assert(domain.quality.last_sample_age_us==1000u);
            audit_now_ms=1000u;
        } else clock_valid=false;
        sync_dpll_fb_service();vectors();
        assert(domain.quality.last_sample_age_us==UINT32_MAX);
        assert(s_published_snapshot.quality.last_sample_age_us==UINT32_MAX);
        const uint32_t expected_health=!strcmp(name,"fault_invalid") ? VDC_DOMAIN_HEALTH_FAULT : VDC_DOMAIN_HEALTH_DEGRADED;
        assert(domain.quality.health_state==expected_health);
        assert(s_published_snapshot.quality.health_state==expected_health);
        assert(refmem_vdc_vector_payload_validate(&vdc_region.payload));
        assert(refmem_dpll_vector_payload_validate(&dpll_region.payload));
        /* Same contract as above: the owner still declares lock, so the
         * projected bit stays set while only quality/gate bits move. */
        assert(vdc_region.payload.flags&REFMEM_VECTOR_FLAG_LOCKED);
        assert(dpll_region.payload.flags&REFMEM_VECTOR_FLAG_LOCKED);
        assert(s_published_snapshot.dpll.update_seq==initial_evidence_seq);
        if(!strcmp(name,"invalid_holdover")) {
            assert(domain.dpll.holdover_age_us==UINT32_MAX);
            assert(s_published_snapshot.dpll.holdover_age_us==UINT32_MAX);
        } else if(!strcmp(name,"invalid_unfinalized")) {
            assert(!s_dpll_capture_count);
            assert(!memcmp(&s_published_snapshot.dco,&initial_dco,sizeof(initial_dco)));
        } else if(strcmp(name,"fault_invalid")) {
            assert(!memcmp(&domain.dco,&initial_dco,sizeof(initial_dco)));
            assert(domain.dpll.update_seq==initial_evidence_seq);
        }
        const uint32_t invalid_guard=s_published_snapshot_guard;
        for(unsigned i=0u;i<3u;++i) sync_dpll_fb_service();
        assert(s_published_snapshot_guard==invalid_guard);
        clock_valid=true;audit_now_ms=1001u;sync_dpll_fb_service();
        assert(domain.quality.last_sample_age_us==UINT32_MAX);
        assert(s_published_snapshot.quality.last_sample_age_us==UINT32_MAX);
        assert(s_published_snapshot_guard==invalid_guard);
        if(!strcmp(name,"domain_service_latched") || !strcmp(name,"max_limit")) {
            vdc_domain_service(&domain,UINT64_C(1001000000));
            assert(domain.quality.last_sample_age_us==UINT32_MAX);
            assert(domain.quality.health_state==VDC_DOMAIN_HEALTH_DEGRADED);
        }
        if(!strcmp(name,"accepted_recovery") || !strcmp(name,"rejected_latched")) {
            domain.control.profile.mode=VDC_DPLL_CONTROL_MODE_MASTER;
            const uint64_t old_anchor=domain.quality.last_sample_time_ns;
            vdc_tdma_timestamp_evidence_t evidence=make_hardware_sample(&domain.schedule,2u,0);
            const bool reject=!strcmp(name,"rejected_latched");
            if(reject)evidence.timestamp_flags=0u;
            vdc_tdma_evidence_preparation_t prep;
            assert(vdc_domain_prepare_active_tdma_evidence(&domain,&evidence,&prep));
            assert(!!prep.accepted==!reject);prep.local_apply_time_ns=UINT64_C(1001000000);
            bool accepted=false;
            if(reject) {
                assert(vdc_domain_apply_prepared_tdma_evidence(&domain,&evidence,&prep,&accepted));
                assert(!accepted && domain.quality.last_sample_time_ns==old_anchor);
                assert(domain.quality.last_sample_age_us==UINT32_MAX);
                vdc_domain_service(&domain,UINT64_C(1001000000));
                assert(domain.quality.health_state!=VDC_DOMAIN_HEALTH_HEALTHY);
            } else {
                assert(vdc_domain_apply_prepared_tdma_evidence_servo(&domain,&evidence,&prep));
                assert(s_published_snapshot.quality.last_sample_age_us==UINT32_MAX);
                assert(vdc_domain_apply_prepared_tdma_evidence_state(&domain,&evidence,&prep,&accepted));
                assert(accepted);
                assert(vdc_domain_finalize_prepared_tdma_evidence(&domain,&evidence,&prep));
                assert(domain.quality.last_sample_time_ns==prep.local_apply_time_ns);
                assert(domain.quality.last_sample_age_us==0u);
                vdc_dpll_manager_publish_runtime_snapshot_locked();
                audit_now_ms=1002u;sync_dpll_fb_service();
                assert(domain.quality.last_sample_age_us==1000u);
                assert(s_published_snapshot.quality.last_sample_age_us==1000u);
            }
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
