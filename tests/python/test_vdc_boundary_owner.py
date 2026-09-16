"""Execute real boundary owner, committed model publisher and Domain actuator.

Only external ring/observation/RefMem inputs are controlled. This verifies
internal application and acknowledgement policy, not physical lock or HIL.
"""
import re
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_follower_rate_boundary import HARNESS as DOMAIN_HARNESS


@pytest.fixture(scope='module')
def boundary_owner_executable(tmp_path_factory):
    physical = (ROOT / 'components/tdma/inc/tdma_pio_spi_phys.h').read_text(encoding='utf-8')
    live = re.search(r'typedef struct \{[^}]*\} tdma_pio_spi_event_live_snapshot_t;', physical, re.S)
    flags = re.search(r'enum \{\s*TDMA_EVENT_LIVE_RETAINED[^}]*\};', physical, re.S)
    matcher = (ROOT / 'components/vdc_dpll_manager/src/vdc_dpll_feedback_match.inc').read_text(encoding='utf-8')
    source_type = re.search(r'typedef struct \{[^}]*\} vdc_feedback_match_source_t;', matcher, re.S)
    assert live and flags and source_type
    helpers = matcher[matcher.index('static uint32_t match_inc'):matcher.index('/* Keep authorization')]
    harness = PRELUDE.replace('EVENT_TYPES', live.group(0) + '\n' + flags.group(0))
    harness += '\n' + source_type.group(0) + MATCH_STORAGE + helpers
    harness += '\n' + (ROOT / 'components/vdc_dpll_manager/src/vdc_model_feedback.inc').read_text(encoding='utf-8')
    harness += '\n' + (ROOT / 'components/vdc_dpll_manager/src/vdc_boundary_control.inc').read_text(encoding='utf-8')
    harness += '\n' + ingress_definition(DOMAIN_HARNESS, 'fixture') + TESTS
    sources = [ROOT / f'components/vdc_domain/src/{name}.c' for name in (
        'vdc_domain', 'vdc_timestamp', 'vdc_ring_observer', 'vdc_sync_io_adapter', 'vdc_tdma_payload')]
    sources += [ROOT / f'components/tdma/src/{name}.c' for name in (
        'tdma_service', 'tdma_profile', 'tdma_operating_profile', 'tdma_payload_registry',
        'tdma_flight_fifo', 'tdma_flight_engine', 'tdma_process_image_map', 'tdma_ring_runtime',
        'tdma_traffic_scheduler', 'tdma_service_timing')]
    return compile_executable(tmp_path_factory.mktemp('boundary-owner'), 'boundary_owner', harness, sources)


@pytest.mark.parametrize('scenario', [
    'follower_apply', 'master_offer', 'ack_exact', 'ack_timeout', 'txdone_aba',
    'probe_rearm', 'probe_session', 'probe_authorization', 'probe_new_round',
    'offer_age_recheck', 'offer_model_recheck', 'offer_guard_busy',
    'follower_role_at_apply', 'offer_role_at_return',
    'follower_stop_at_apply', 'follower_config_at_apply', 'follower_observer_at_apply',
    'reject_history_stop_withdraw_reset', 'reject_history_after_success', 'reject_history_saturation',
    'follower_repeated_delivery_once', 'follower_retry_original_age', 'offer_retry_original_age',
    *[f'follower_reject_{i}' for i in range(28)],
    *[f'master_reject_{i}' for i in range(25)],
    *[f'ack_reject_{i}' for i in range(7)],
])
def test_boundary_owner(boundary_owner_executable, scenario):
    result = subprocess.run([str(boundary_owner_executable), scenario], text=True,
                            capture_output=True, timeout=10)
    (boundary_owner_executable.parent / (scenario + '.log')).write_text(
        result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr


PRELUDE = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Rename only external snapshot/STOP inputs in this harness translation unit;
 * separately linked Domain/TDMA implementation remains unchanged. */
#define tdma_ring_runtime_get_clock_snapshot boundary_test_clock_snapshot
#define tdma_service_update_stopped_metadata boundary_test_stopped_metadata
#include "vdc_dpll_manager.h"
#include "distributed_refmem.h"
#include "tdma_event_observer.h"
#include "vdc_model_projection.h"
EVENT_TYPES
#define BOARD_SYS_CLOCK_HZ 250000000u
static vdc_domain_context_t s_vdc_domain;
static tdma_service_service_t owner;
static tdma_service_service_t *s_vdc_tdma_service=&owner;
static uint32_t s_dpll_role_requested_generation,s_dpll_role_applied_generation;
static tdma_ring_clock_snapshot_t ring;
static tdma_pio_spi_event_live_snapshot_t live;
static distributed_refmem_vdc_feedback_rx_snapshot_t feedback[PROJECT_NODE_CAPACITY];
static distributed_refmem_vdc_boundary_view_t incoming;
static uint32_t now_ms=100,origin_epoch=12,publication_count;
static uint64_t now_ns=3000000000ull,raw_now=1000000;
static bool stopped=true,ring_available=true,live_available=true;
static unsigned command_copy_race;
static void (*match_copy_race)(void);
static void (*now_hook)(void);
bool boundary_test_stopped_metadata(tdma_service_service_t *p,bool(*publish)(void*),void *ctx)
{ assert(p==&owner);return stopped && publish(ctx); }
bool boundary_test_clock_snapshot(const tdma_ring_runtime_t *p,tdma_ring_clock_snapshot_t *out)
{ assert(p==&owner.ring_runtime);*out=ring;return ring_available; }
static uint32_t board_uptime_ms(void) { return now_ms; }
static uint64_t vdc_dpll_manager_now_ns(void)
{
    if(now_hook) {void(*hook)(void)=now_hook;now_hook=NULL;hook();}
    return now_ns;
}
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=raw_now;return true; }
bool vdc_timestamp_clock_try_read_bridge(uint32_t hz,vdc_timestamp_clock_bridge_t *out)
{ (void)hz;(void)out;return false; }
static bool tdma_runtime_owner_get_event_live_snapshot(tdma_pio_spi_event_live_snapshot_t *out)
{ *out=live;return live_available; }
static bool tdma_runtime_owner_get_origin_reference_epoch(uint32_t *out)
{ *out=origin_epoch;if(match_copy_race)match_copy_race();return true; }
bool distributed_refmem_copy_vdc_feedback_rx(uint32_t slot,distributed_refmem_vdc_feedback_rx_snapshot_t *out)
{ assert(slot<PROJECT_NODE_CAPACITY);*out=feedback[slot];return true; }
bool distributed_refmem_copy_vdc_boundary_command(uint32_t source,distributed_refmem_vdc_boundary_view_t *out)
{
    assert(source==0);*out=incoming;
    if(command_copy_race==1)ring.enabled=0;
    if(command_copy_race==2)s_vdc_domain.control.profile.generation++;
    if(command_copy_race==3)s_vdc_domain.dco.period_adjust_ppb++;
    return true;
}
static void vdc_dpll_manager_publish_runtime_snapshot_locked(void) { ++publication_count; }
'''

MATCH_STORAGE = r'''
static vdc_feedback_match_source_t s_feedback_matches[PROJECT_NODE_CAPACITY];
static uint32_t s_feedback_match_owner_token=17,s_feedback_match_active_generation=17;
'''

TESTS = r'''
static void request_role_change(void) { ++s_dpll_role_requested_generation; }
static void stop_during_age(void) { ring.enabled=0; }
static void configure_during_age(void) { ++ring.config_seq; }
static void recover_observer_during_age(void) { ++live.record.epoch; }
static void tick(void)
{
    const uint32_t session=vdc_dpll_manager_feedback_session();
    ++s_committed_model_guard;
    vdc_boundary_service_core1();
    model_feedback_end_core1(session);
    assert(!(s_committed_model_guard&1u));
}
static void roundtrip(void) { for(unsigned i=0;i<4;i++)tick(); }
static vdc_dpll_boundary_status_t status(uint32_t slot)
{ vdc_dpll_boundary_status_t s;assert(vdc_dpll_manager_get_boundary_status(slot,&s));return s; }
static vdc_dpll_manager_committed_model_t model(void)
{ vdc_dpll_manager_committed_model_t m;assert(vdc_dpll_manager_get_committed_model(&m));return m; }

static void setup(bool master)
{
    fixture(&s_vdc_domain);
    if(master) {
        vdc_dpll_control_profile_t role=s_vdc_domain.control.profile;
        role.mode=VDC_DPLL_CONTROL_MODE_MASTER;
        assert(vdc_domain_set_dpll_control_profile(&s_vdc_domain,&role));
        vdc_domain_set_schedule_local_slot(&s_vdc_domain,0u);
    }
    s_dpll_role_requested_generation=s_dpll_role_applied_generation=s_vdc_domain.control.profile.generation;
    ring=(tdma_ring_clock_snapshot_t){.config_seq=7,.applied_config_seq=7,.node_count=4,
        .local_slot_id=s_vdc_domain.schedule.local_slot_id,.reference_slot_id=0,
        .schedule_crc32=s_vdc_domain.schedule.schedule_crc32};
    live=(tdma_pio_spi_event_live_snapshot_t){.flags=TDMA_EVENT_LIVE_RETAINED|TDMA_EVENT_LIVE_ACTIVE|
        TDMA_EVENT_LIVE_ANCHOR_VALID,.arm_epoch=77,.record={.epoch=8,.sequence=100}};
    assert(vdc_dpll_manager_set_feedback_session(123));
    assert(vdc_dpll_manager_set_boundary_probe(-17));
    ring.enabled=ring.adapter_started=ring.data_enabled=1;stopped=false;
    ++s_committed_model_guard;model_feedback_end_core1(123);
}

static void follower_input(void)
{
    const vdc_dpll_manager_committed_model_t m=model();uint64_t upper;
    assert(vdc_domain_dco_local_to_output_ns(&m.dco,now_ns+999,&upper));
    incoming=(distributed_refmem_vdc_boundary_view_t){
        .active=1,.ring_config_seq=ring.config_seq,.role_generation=m.role_generation,
        .schedule_crc32=ring.schedule_crc32,.clock_epoch_id=m.clock_epoch_id,.clock_run_id=m.clock_run_id,
        .first_rx_ms=now_ms-3,.last_rx_ms=now_ms,
        .command={.target_arm_epoch=live.arm_epoch,.basis_source_output_ns_lo=upper-50000000u,
            .control_session=123,.command_seq=m.applied_command_seq+1u,.schedule_crc32=ring.schedule_crc32,
            .target_clock_epoch_id=m.clock_epoch_id,.target_clock_run_id=m.clock_run_id,
            .target_observer_epoch=live.record.epoch,.basis_measurement_sequence=live.record.sequence,
            .expected_target_model_token=m.token,.expected_applied_command_seq=m.applied_command_seq,
            .signed_delta_rate_ppb=-17,.schema_version=3,.source_slot=0,.target_slot=1,.flags=1}};
}

static void master_input(uint32_t slot)
{
    const vdc_dpll_manager_committed_model_t m=model();uint64_t upper;
    assert(vdc_domain_dco_local_to_output_ns(&m.dco,now_ns+999,&upper));
    const uint64_t reference=upper-50000000u,source=reference+12345u;
    feedback[slot]=(distributed_refmem_vdc_feedback_rx_snapshot_t){.schema=1,.active=1,.retained=1,
        .source_slot=slot,.ring_config_seq=ring.config_seq,.role_generation=m.role_generation,
        .schedule_crc32=ring.schedule_crc32,.clock_epoch_id=m.clock_epoch_id,.clock_run_id=m.clock_run_id,
        .receive_count=19,.record={2},
        .sample={.source_arm_epoch=77,.source_clock_epoch_id=31,.source_clock_run_id=41,
            .observer_epoch=8,.measurement_sequence=100,.tick_hz=250000000,
            .schema_version=2,.source_slot=(uint8_t)slot,.target_slot=0,.domain_flags=15,
            .model={.output_ns_lo=source,.output_ns_hi=source+999,.model_token=8,
                .applied_command_seq=7,.control_session=123}}};
    vdc_dpll_manager_feedback_match_status_t matched={.schema=2,.active=1,.source_slot=slot,.target_slot=0,
        .ring_config_seq=ring.config_seq,.role_generation=m.role_generation,.schedule_crc32=ring.schedule_crc32,
        .clock_epoch_id=m.clock_epoch_id,.clock_run_id=m.clock_run_id,.receive_count=19,
        .last_result=VDC_FEEDBACK_MATCH_MATCHED,.preparation_generation=17,.control_session=123,
        .result={.has_pair=1,.reserved=VDC_FEEDBACK_MODEL_DOMAIN,.reference_epoch=origin_epoch,
            .reference_generation=17,
            .source={.source_arm_epoch=77,.source_clock_epoch_id=31,.source_clock_run_id=41,
                .observer_epoch=8,.tick_hz=250000000},
            .pairs={{.rx_elapsed_cycles=source-10000000,.reference_tx_lo=reference-10000000,
                .reference_tx_hi=reference-10000000+999,.measurement_sequence=99,
                .reference_identity_crc32=m.token,.rx_width_ns=999,.source_model_token=8},
                {.rx_elapsed_cycles=source,.reference_tx_lo=reference,.reference_tx_hi=reference+999,
                .measurement_sequence=100,.reference_identity_crc32=m.token,.rx_width_ns=999,.source_model_token=8}}}};
    match_publish(&s_feedback_matches[slot],&matched);
}

static void assert_follower_unchanged(const vdc_domain_context_t *before)
{
    assert(!memcmp(before,&s_vdc_domain,sizeof(*before)));
    assert(status(1).apply_count==0 && !status(1).consumed_mask);
    assert(publication_count==0);
}

static void follower_reject(unsigned flaw)
{
    setup(false);follower_input();
    switch(flaw) {
    case 0:incoming.active=0;break;
    case 1:incoming.ring_config_seq++;break;
    case 2:incoming.role_generation++;break;
    case 3:incoming.command.control_session++;break;
    case 4:incoming.command.target_slot=2;break;
    case 5:incoming.command.source_slot=2;break;
    case 6:incoming.command.target_clock_epoch_id++;break;
    case 7:incoming.command.target_clock_run_id++;break;
    case 8:incoming.command.target_arm_epoch++;break;
    case 9:incoming.command.target_observer_epoch++;break;
    case 10:incoming.command.expected_target_model_token++;break;
    case 11:incoming.command.expected_applied_command_seq++;break;
    case 12:incoming.command.command_seq++;break;
    case 13:incoming.command.signed_delta_rate_ppb++;break;
    case 14:incoming.command.basis_measurement_sequence++;break;
    case 15:incoming.command.basis_source_output_ns_lo-=VDC_BOUNDARY_COMMAND_MAX_AGE_NS;break;
    case 16:incoming.command.basis_source_output_ns_lo=UINT64_MAX;break;
    case 17:incoming.command.flags=0;break;
    case 18:incoming.command.schema_version=2;break;
    case 19:incoming.command.reserved=1;break;
    case 20:live.flags&=~TDMA_EVENT_LIVE_ACTIVE;break;
    case 21:ring.enabled=0;break;
    case 22:s_dpll_role_requested_generation++;break;
    case 23:s_vdc_domain.dco.period_adjust_ppb++;break; /* same-beat model retired */
    case 24:command_copy_race=1;break;
    case 25:incoming.command.schedule_crc32++;break;
    case 26:s_vdc_domain.dco.period_adjust_ppb=-10000;
        ++s_committed_model_guard;model_feedback_end_core1(123);follower_input();break;
    case 27:now_ns=UINT64_MAX;break;
    default:assert(0);
    }
    vdc_domain_context_t before;memcpy(&before,&s_vdc_domain,sizeof(before));
    tick();assert_follower_unchanged(&before);
}

static void master_reject(unsigned flaw)
{
    setup(true);master_input(1);
    vdc_dpll_manager_feedback_match_status_t matched;
    match_load_words(s_feedback_matches[1].words,&matched,sizeof(matched));
    switch(flaw) {
    case 0:matched.active=0;break;
    case 1:matched.receive_count--;break;
    case 2:matched.last_result=VDC_FEEDBACK_MATCH_NO_REFERENCE;break;
    case 3:matched.result.pairs[0].source_model_token++;break;
    case 4:matched.result.pairs[0].reference_identity_crc32++;break;
    case 5:matched.result.pairs[1].source_model_token++;break;
    case 6:matched.result.pairs[1].reference_identity_crc32++;break;
    case 7:matched.result.pairs[1].measurement_sequence--;break;
    case 8:matched.result.pairs[1].rx_elapsed_cycles++;break;
    case 9:matched.result.pairs[1].rx_width_ns++;break;
    case 10:matched.result.source.source_arm_epoch++;break;
    case 11:matched.result.source.source_clock_run_id++;break;
    case 12:matched.result.source.observer_epoch++;break;
    case 13:matched.result.source.tick_hz++;break;
    case 14:matched.result.pairs[1].reference_tx_lo-=VDC_BOUNDARY_COMMAND_MAX_AGE_NS;break;
    case 15:matched.result.pairs[1].reference_tx_lo=UINT64_MAX;break;
    case 16:matched.preparation_generation++;break;
    case 17:feedback[1].sample.model.applied_command_seq=UINT32_MAX;break;
    case 18:feedback[1].sample.model.control_session++;break;
    case 19:feedback[1].clock_run_id++;break;
    case 20:matched.result.reference_epoch++;break;
    case 21:s_feedback_match_active_generation++;break;
    case 22:matched.schema=1;break;
    case 23:matched.result.has_pair=0;break;
    case 24:matched.result.reserved=0;break;
    default:assert(0);
    }
    match_publish(&s_feedback_matches[1],&matched);
    vdc_domain_context_t before;memcpy(&before,&s_vdc_domain,sizeof(before));
    roundtrip();assert(!memcmp(&before,&s_vdc_domain,sizeof(before)));
    assert(!status(1).offer_id && !status(1).consumed_mask && status(1).peer_state==VDC_BOUNDARY_UNTRIED);
}

static void good_ack(void)
{
    feedback[1].sample.model.applied_command_seq=8;
    feedback[1].sample.model.model_token=9;
    feedback[1].sample.measurement_sequence=101;
}

static void ack_reject(unsigned flaw)
{
    setup(true);master_input(1);roundtrip();assert(status(1).peer_state==VDC_BOUNDARY_PENDING);good_ack();
    switch(flaw) {
    case 0:feedback[1].sample.model.applied_command_seq=9;break;
    case 1:feedback[1].sample.model.model_token=8;break;
    case 2:feedback[1].sample.measurement_sequence=100;break;
    case 3:feedback[1].sample.source_arm_epoch++;break;
    case 4:feedback[1].sample.source_clock_run_id++;break;
    case 5:feedback[1].sample.observer_epoch++;break;
    case 6:feedback[1].sample.model.control_session++;break;
    default:assert(0);
    }
    roundtrip();assert(status(1).peer_state==VDC_BOUNDARY_PENDING && status(1).consumed_mask==2u);
}

int main(int argc,char **argv)
{
    assert(argc==2);const char *test=argv[1];
    if(!strncmp(test,"follower_reject_",16))follower_reject((unsigned)atoi(test+16));
    else if(!strncmp(test,"master_reject_",14))master_reject((unsigned)atoi(test+14));
    else if(!strncmp(test,"ack_reject_",11))ack_reject((unsigned)atoi(test+11));
    else if(!strcmp(test,"follower_role_at_apply") || !strcmp(test,"follower_stop_at_apply") ||
            !strcmp(test,"follower_config_at_apply") || !strcmp(test,"follower_observer_at_apply")) {
        setup(false);follower_input();vdc_domain_context_t before;
        memcpy(&before,&s_vdc_domain,sizeof(before));
        /* This clock read occurs after both identity snapshots, immediately
         * before age projection and the final Domain commit admission. */
        now_hook=!strcmp(test,"follower_role_at_apply")?request_role_change:
            !strcmp(test,"follower_stop_at_apply")?stop_during_age:
            !strcmp(test,"follower_config_at_apply")?configure_during_age:recover_observer_during_age;
        tick();assert_follower_unchanged(&before);
    } else if(!strcmp(test,"reject_history_stop_withdraw_reset")) {
        setup(false);follower_input();incoming.command.target_slot=2;tick();
        vdc_dpll_boundary_status_t first=status(1);
        assert(first.schema==2 && first.first_reject==BOUNDARY_REJECT_IDENTITY);
        assert(first.last_reject==BOUNDARY_REJECT_IDENTITY && first.reject_mask==(1u<<BOUNDARY_REJECT_IDENTITY));
        follower_input();incoming.command.basis_source_output_ns_lo-=VDC_BOUNDARY_COMMAND_MAX_AGE_NS;tick();
        const uint32_t observed=(1u<<BOUNDARY_REJECT_IDENTITY)|(1u<<BOUNDARY_REJECT_AGE);
        assert(status(1).first_reject==BOUNDARY_REJECT_IDENTITY && status(1).reject_mask==observed);
        assert(status(1).last_reject==BOUNDARY_REJECT_AGE && status(1).reject_count==2u);
        ring.enabled=0;stopped=true;tick();
        const vdc_dpll_boundary_status_t retired=status(1);
        assert(retired.last_reject==BOUNDARY_REJECT_BINDING && retired.first_reject==BOUNDARY_REJECT_IDENTITY);
        assert(retired.reject_mask==(observed|(1u<<BOUNDARY_REJECT_BINDING)) && retired.reject_count==3u);
        assert(vdc_dpll_manager_set_boundary_probe(0));tick();
        assert(status(1).first_reject==retired.first_reject && status(1).reject_mask==retired.reject_mask);
        assert(vdc_dpll_manager_set_feedback_session(0));tick();
        assert(status(1).first_reject==retired.first_reject && status(1).reject_mask==retired.reject_mask);
        assert(status(1).reject_count==retired.reject_count && status(1).last_reject==retired.last_reject);
        assert(vdc_dpll_manager_set_feedback_session(124));
        assert(vdc_dpll_manager_set_boundary_probe(-17));tick();
        const vdc_dpll_boundary_status_t fresh=status(1);
        assert(!fresh.first_reject && !fresh.reject_mask && !fresh.reject_count && !fresh.last_reject);
    } else if(!strcmp(test,"reject_history_after_success")) {
        setup(false);follower_input();incoming.command.target_slot=2;tick();
        follower_input();tick();
        assert(status(1).apply_count==1 && status(1).last_reject==BOUNDARY_REJECT_NONE);
        assert(status(1).first_reject==BOUNDARY_REJECT_IDENTITY && status(1).reject_mask==(1u<<BOUNDARY_REJECT_IDENTITY));
    } else if(!strcmp(test,"reject_history_saturation")) {
        vdc_boundary_meta_t m={.reject_count=UINT32_MAX};
        for(uint32_t reason=BOUNDARY_REJECT_BINDING;reason<=BOUNDARY_REJECT_EXHAUSTED;reason++)
            boundary_reject(&m,reason);
        assert(m.reject_count==UINT32_MAX && m.first_reject==BOUNDARY_REJECT_BINDING);
        assert(m.last_reject==BOUNDARY_REJECT_EXHAUSTED && m.reject_mask==0x1feu);
    } else if(!strcmp(test,"follower_repeated_delivery_once")) {
        setup(false);follower_input();
        const distributed_refmem_vdc_boundary_view_t original=incoming;
        tick();assert(status(1).apply_count==1 && publication_count==1);
        const vdc_dco_control_t applied=s_vdc_domain.dco;
        const uint32_t token=model().token;
        for(unsigned retry=0;retry<3;retry++) {
            incoming=original;now_ns+=100000000u;now_ms+=100;tick();
            assert(status(1).apply_count==1 && publication_count==1);
            assert(!memcmp(&applied,&s_vdc_domain.dco,sizeof(applied)) && model().token==token);
        }
    } else if(!strcmp(test,"follower_retry_original_age")) {
        setup(false);follower_input();
        const distributed_refmem_vdc_boundary_view_t original=incoming;
        const vdc_dco_control_t before=s_vdc_domain.dco;
        now_ns+=VDC_BOUNDARY_COMMAND_MAX_AGE_NS;
        for(unsigned retry=0;retry<3;retry++) {
            incoming=original;incoming.last_rx_ms=++now_ms;tick();
            assert(!status(1).apply_count && !memcmp(&before,&s_vdc_domain.dco,sizeof(before)));
            assert(status(1).last_reject==BOUNDARY_REJECT_AGE);
        }
    } else if(!strcmp(test,"follower_apply")) {
        setup(false);follower_input();const vdc_dco_control_t old=s_vdc_domain.dco;
        uint64_t before,after;assert(vdc_domain_dco_local_to_output_ns(&old,now_ns,&before));
        const uint32_t token=model().token;tick();
        assert(vdc_domain_dco_local_to_output_ns(&s_vdc_domain.dco,now_ns,&after));assert(before==after);
        assert(s_vdc_domain.dco.period_adjust_ppb==old.period_adjust_ppb-17 && s_vdc_domain.dco.phase_offset_ns==0);
        assert(status(1).apply_count==1 && status(1).peer_state==VDC_BOUNDARY_APPLIED);
        assert(status(1).schema==2 && !status(1).first_reject && !status(1).reject_mask);
        assert(s_vdc_domain.control.last_follower_command_seq==1 && model().token>token);
        const vdc_dco_control_t applied=s_vdc_domain.dco;roundtrip();
        assert(!memcmp(&applied,&s_vdc_domain.dco,sizeof(applied)) && status(1).apply_count==1);
    } else if(!strcmp(test,"probe_authorization")) {
        setup(false);follower_input();stopped=false;
        assert(!vdc_dpll_manager_set_boundary_probe(1));
        stopped=true;assert(!vdc_dpll_manager_set_boundary_probe(VDC_BOUNDARY_PROBE_MAX_DELTA_PPB+1));
        assert(vdc_dpll_manager_set_boundary_probe(0));tick();assert(status(1).apply_count==0);
        assert(vdc_dpll_manager_set_feedback_session(0));assert(!vdc_dpll_manager_set_boundary_probe(1));
    } else {
        setup(true);master_input(1);
        if(!strcmp(test,"offer_age_recheck")) {
            vdc_dpll_manager_feedback_match_status_t pair;
            match_load_words(s_feedback_matches[1].words,&pair,sizeof(pair));
            pair.result.pairs[1].reference_tx_lo-=449000000u;
            pair.result.pairs[1].reference_tx_hi-=449000000u;
            match_publish(&s_feedback_matches[1],&pair);
        }
        roundtrip();vdc_dpll_boundary_status_t initial=status(1);
        assert(initial.offer_id && initial.peer_state==VDC_BOUNDARY_PENDING && initial.consumed_mask==2u);
        assert(initial.command.command_seq==8 && initial.command.expected_applied_command_seq==7);
        assert(initial.command.target_clock_epoch_id==31 && initial.command.target_clock_run_id==41);
        if(!strcmp(test,"offer_retry_original_age")) {
            refmem_sync_vdc_boundary_command_t original=initial.command,out;
            const uint32_t offered_ms=initial.offered_ms;uint32_t id;
            for(unsigned retry=0;retry<3;retry++) {
                assert(vdc_dpll_manager_copy_boundary_command_offer(ring.config_seq,
                    s_vdc_domain.control.profile.generation,0,ring.schedule_crc32,123,&out,&id)==1);
                assert(id==initial.offer_id && !memcmp(&original,&out,sizeof(out)));
                assert(status(1).offered_ms==offered_ms);
                now_ns+=100000000u;
            }
            now_ns+=VDC_BOUNDARY_COMMAND_MAX_AGE_NS;
            assert(vdc_dpll_manager_copy_boundary_command_offer(ring.config_seq,
                s_vdc_domain.control.profile.generation,0,ring.schedule_crc32,123,&out,&id)==0);
            assert(status(1).offered_ms==offered_ms && status(1).offer_serial==initial.offer_serial);
        } else if(!strcmp(test,"master_offer")) {
            refmem_sync_vdc_boundary_command_t out;uint32_t id;
            assert(vdc_dpll_manager_copy_boundary_command_offer(ring.config_seq,
                s_vdc_domain.control.profile.generation,0,ring.schedule_crc32,123,&out,&id)==1);
            assert(id==initial.offer_id && !memcmp(&out,&initial.command,sizeof(out)));
            roundtrip();assert(status(1).offer_serial==initial.offer_serial);
        } else if(!strncmp(test,"offer_",6)) {
            refmem_sync_vdc_boundary_command_t out;uint32_t id;
            assert(vdc_dpll_manager_copy_boundary_command_offer(ring.config_seq,
                s_vdc_domain.control.profile.generation,0,ring.schedule_crc32,123,&out,&id)==1);
            if(!strcmp(test,"offer_age_recheck")) now_ns+=2000000u;
            else if(!strcmp(test,"offer_model_recheck")) {
                s_vdc_domain.dco.period_adjust_ppb++;
                ++s_committed_model_guard;model_feedback_end_core1(123);
            } else if(!strcmp(test,"offer_role_at_return")) now_hook=request_role_change;
            else {assert(!strcmp(test,"offer_guard_busy"));++s_committed_model_guard;}
            const int result=vdc_dpll_manager_copy_boundary_command_offer(ring.config_seq,
                s_vdc_domain.control.profile.generation,0,ring.schedule_crc32,123,&out,&id);
            assert(result==(!strcmp(test,"offer_guard_busy")?-1:0));
            assert(status(1).consumed_mask==2u && status(1).offer_serial==initial.offer_serial);
        } else if(!strcmp(test,"ack_exact")) {
            good_ack();roundtrip();assert(status(1).peer_state==VDC_BOUNDARY_ACKED);
            assert(!status(1).offer_id && !status(1).tx_count);
            refmem_sync_vdc_boundary_command_t out;uint32_t id;
            assert(vdc_dpll_manager_copy_boundary_command_offer(ring.config_seq,
                s_vdc_domain.control.profile.generation,0,ring.schedule_crc32,123,&out,&id)==0);
            roundtrip();assert(status(1).consumed_mask==2u && status(1).offer_serial==initial.offer_serial);
        } else if(!strcmp(test,"ack_timeout")) {
            now_ms+=VDC_BOUNDARY_ACK_TIMEOUT_MS;roundtrip();
            assert(status(1).peer_state==VDC_BOUNDARY_EXPIRED_UNRESOLVED && !status(1).offer_id);
            const uint32_t serial=status(1).offer_serial;
            for(unsigned i=0;i<3;i++)roundtrip();
            assert(status(1).peer_state==VDC_BOUNDARY_EXPIRED_UNRESOLVED && status(1).offer_serial==serial);
            good_ack();roundtrip();assert(status(1).peer_state==VDC_BOUNDARY_LATE_APPLIED);
            assert(status(1).consumed_mask==2u && status(1).offer_serial==initial.offer_serial);
        } else if(!strcmp(test,"txdone_aba") || !strcmp(test,"probe_new_round")) {
            ring.enabled=0;stopped=true;assert(vdc_dpll_manager_set_boundary_probe(-17));
            ring.enabled=1;stopped=false;roundtrip();
            const uint32_t newer=status(1).offer_id;assert(newer>initial.offer_id);
            vdc_dpll_manager_boundary_command_tx_done_core0(initial.offer_id);roundtrip();
            assert(status(1).offer_id==newer && !status(1).tx_count);
            vdc_dpll_manager_boundary_command_tx_done_core0(newer);roundtrip();
            assert(!status(1).offer_id && status(1).tx_count==1 && status(1).peer_state==VDC_BOUNDARY_PENDING);
        } else if(!strcmp(test,"probe_rearm") || !strcmp(test,"probe_session")) {
            ring.enabled=0;tick();assert(!status(1).offer_id);stopped=true;
            if(!strcmp(test,"probe_session"))assert(vdc_dpll_manager_set_feedback_session(124));
            ring.enabled=1;ring.config_seq++;ring.applied_config_seq++;stopped=false;roundtrip();
            assert(!status(1).offer_id && status(1).consumed_mask==2u && status(1).offer_serial==initial.offer_serial);
        } else assert(0);
    }
    puts("boundary owner passed");return 0;
}
'''
