"""Execute real boundary owner, committed model publisher and Domain actuator.

Only external ring/observation/RefMem inputs are controlled. This verifies
internal application and acknowledgement policy, not physical lock or HIL.
"""
import re
import subprocess
from fractions import Fraction

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
    harness += '\n' + ingress_definition(DOMAIN_HARNESS, 'fixture') + TESTS.replace(
        'int main(int argc,char **argv)', AUTO_TESTS + '\nint main(int argc,char **argv)').replace(
        'if(!strncmp(test,"follower_reject_",16))',
        'if(!strncmp(test,"auto_",5))auto_test(test);\n    else if(!strncmp(test,"follower_reject_",16))')
    sources = [ROOT / f'components/vdc_domain/src/{name}.c' for name in (
        'vdc_domain', 'vdc_timestamp', 'vdc_ring_observer', 'vdc_sync_io_adapter', 'vdc_tdma_payload')]
    sources += [ROOT / f'components/tdma/src/{name}.c' for name in (
        'tdma_service', 'tdma_profile', 'tdma_operating_profile', 'tdma_payload_registry',
        'tdma_flight_fifo', 'tdma_flight_engine', 'tdma_process_image_map', 'tdma_ring_runtime',
        'tdma_traffic_scheduler', 'tdma_service_timing')]
    sources += [ROOT / 'components/vdc_dpll_manager/src/vdc_feedback_match.c',
                ROOT / 'components/distributed_refmem/src/refmem_sync_vdc_feedback.c']
    return compile_executable(tmp_path_factory.mktemp('boundary-owner'), 'boundary_owner', harness, sources)


@pytest.mark.parametrize('scenario', [
    'follower_apply', 'master_offer', 'ack_exact', 'ack_timeout', 'txdone_aba',
    'probe_rearm', 'probe_session', 'probe_authorization', 'probe_new_round',
    'offer_age_recheck', 'offer_model_recheck', 'offer_guard_busy',
    'follower_role_at_apply', 'offer_role_at_return',
    'follower_stop_at_apply', 'follower_config_at_apply', 'follower_observer_at_apply',
    'reject_history_stop_withdraw_reset', 'reject_history_after_success', 'reject_history_saturation',
    'follower_repeated_delivery_once', 'follower_retry_original_age', 'offer_retry_original_age',
    'reference_owner_master', 'reference_owner_follower',
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


@pytest.mark.parametrize('scenario', [
    'auto_closed_loop', 'auto_loss_and_reconcile', 'auto_uncertain', 'auto_revoke',
    'auto_reference_change', 'auto_stale_window', 'auto_age_domains',
    'auto_disable_modes', 'auto_delta_limits', 'auto_pending_immutable',
    'auto_wrong_domain',
    'auto_mode_contention',
])
def test_automatic_owner_actual_roundtrips(boundary_owner_executable, scenario):
    result = subprocess.run([str(boundary_owner_executable), scenario], text=True,
                            capture_output=True, timeout=10)
    (boundary_owner_executable.parent / (scenario + '.log')).write_text(
        result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    if scenario == 'auto_closed_loop':
        rows = [list(map(int, line.split()[1:])) for line in result.stdout.splitlines()
                if line.startswith('UPDATE ')]
        assert {row[0] for row in rows} == {1, 2, 3}
        for slot, oscillator in ((1, 4500), (2, -3500), (3, 1700)):
            selected = [row for row in rows if row[0] == slot]
            assert len(selected) >= 3
            previous_rate = 0
            for _, sequence, rate, delta in selected:
                before = Fraction((10**9 + oscillator) * (10**9 + previous_rate), 10**9) - 10**9
                after = Fraction((10**9 + oscillator) * (10**9 + rate), 10**9) - 10**9
                assert delta * before < 0 and abs(after) < abs(before)
                assert rate == previous_rate + delta and sequence >= 1
                previous_rate = rate
            assert abs(after) < 300  # Independently computed plant; 252 ns bracket floor remains.


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
static uint32_t captured_offers,captured_applies,captured_acks;
static void vdc_boundary_capture_offer_core1(const refmem_sync_vdc_boundary_command_t *c,
    const vdc_feedback_match_snapshot_t *m)
{ assert(c && m);++captured_offers; }
static void vdc_boundary_capture_apply_core1(const refmem_sync_vdc_boundary_command_t *c,
    int32_t rate,uint32_t seq)
{ assert(c && seq);(void)rate;++captured_applies; }
static void vdc_boundary_capture_ack_core1(const uint8_t *wire,uint32_t seq,uint32_t token)
{ assert(wire && seq && token);++captured_acks; }
static void vdc_boundary_capture_hold_core1(uint32_t session,uint32_t source,uint32_t reference,
    const vdc_feedback_match_snapshot_t *match)
{ assert(session && source!=reference && match); }
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
    else if(!strncmp(test,"reference_owner_",16)) {
        const bool master=!strcmp(test,"reference_owner_master");setup(master);
        if(master)master_input(1);else follower_input();
        assert(!vdc_dpll_manager_set_reference_publish(true)); /* RUN refuses write. */
        ring.enabled=0;stopped=true;
        assert(vdc_dpll_manager_set_reference_publish(true));
        bool enabled=false;
        assert(vdc_dpll_manager_try_reference_publish_enabled(&enabled) && enabled);
        assert(!vdc_dpll_manager_boundary_auto_enabled());
        ring.enabled=1;stopped=false;
        const vdc_dco_control_t before=s_vdc_domain.dco;
        for(unsigned i=0;i<20;i++)tick();
        assert(!memcmp(&before,&s_vdc_domain.dco,sizeof(before)));
        assert(!status(1).apply_count && !status(1).offer_id && !captured_offers && !captured_applies);
        ring.enabled=0;stopped=true;
        assert(vdc_dpll_manager_set_boundary_auto(true));
        assert(vdc_dpll_manager_try_reference_publish_enabled(&enabled) && !enabled);
        assert(vdc_dpll_manager_boundary_auto_enabled());
        assert(vdc_dpll_manager_set_reference_publish(true));
        assert(!vdc_dpll_manager_boundary_auto_enabled());
        assert(vdc_dpll_manager_set_boundary_probe(-1));
        assert(vdc_dpll_manager_try_reference_publish_enabled(&enabled) && !enabled);
        assert(vdc_dpll_manager_set_reference_publish(true));
        ++s_boundary_request;enabled=true;
        assert(!vdc_dpll_manager_try_reference_publish_enabled(&enabled) && enabled);
        ++s_boundary_request;
        assert(!vdc_dpll_manager_try_reference_publish_enabled(NULL));
        assert(vdc_dpll_manager_set_reference_publish(false));
        assert(vdc_dpll_manager_try_reference_publish_enabled(&enabled) && !enabled);
        assert(vdc_dpll_manager_set_feedback_session(0));
        assert(!vdc_dpll_manager_set_reference_publish(true));
        assert(vdc_dpll_manager_set_reference_publish(false));
    }
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


AUTO_TESTS = r'''
/* Each simulated board runs the actual same owner/Domain code with its own
 * static state; only external clocks, ring, live input and transport delivery
 * are controlled. The pure production matcher creates the frequency bounds. */
typedef struct {
    vdc_domain_context_t domain;
    tdma_ring_clock_snapshot_t ring;
    tdma_pio_spi_event_live_snapshot_t live;
    uint32_t model_words[sizeof(s_committed_model_words)/4u];
    uint32_t model_guard,model_serial,session,last_session;
    uint32_t meta_words[sizeof(s_boundary_meta_words)/4u];
    uint32_t peer_words[PROJECT_NODE_CAPACITY][sizeof(vdc_boundary_peer_t)/4u];
    uint32_t guard,request,request_session,request_mode,tx_done;
    int32_t request_delta;
} auto_board_t;
static auto_board_t auto_boards[4];
static int32_t oscillator[4]={0,4500,-3500,1700};
static uint64_t physical_ns=3000000000ull;
static uint32_t auto_measurement=1000,transport_sequence=1;
static refmem_sync_vdc_feedback_assembly_t command_assembly[4];

static uint64_t local_at(unsigned slot,uint64_t physical)
{ return (uint64_t)(((__uint128_t)physical*(uint64_t)(1000000000ll+oscillator[slot]))/1000000000u); }
static void auto_save(unsigned slot)
{
    auto_board_t *b=&auto_boards[slot];b->domain=s_vdc_domain;b->ring=ring;b->live=live;
    memcpy(b->model_words,s_committed_model_words,sizeof(b->model_words));
    b->model_guard=s_committed_model_guard;b->model_serial=s_committed_model_serial;
    b->session=s_model_feedback_session;b->last_session=s_model_feedback_last_session;
    memcpy(b->meta_words,s_boundary_meta_words,sizeof(b->meta_words));
    memcpy(b->peer_words,s_boundary_peer_words,sizeof(b->peer_words));
    b->guard=s_boundary_guard;b->request=s_boundary_request;b->request_session=s_boundary_request_session;
    b->request_mode=s_boundary_request_mode;b->request_delta=s_boundary_request_delta;b->tx_done=s_boundary_tx_done;
}
static void auto_load(unsigned slot)
{
    const auto_board_t *b=&auto_boards[slot];s_vdc_domain=b->domain;ring=b->ring;live=b->live;
    memcpy(s_committed_model_words,b->model_words,sizeof(b->model_words));
    s_committed_model_guard=b->model_guard;s_committed_model_serial=b->model_serial;
    s_model_feedback_session=b->session;s_model_feedback_last_session=b->last_session;
    memcpy(s_boundary_meta_words,b->meta_words,sizeof(b->meta_words));
    memcpy(s_boundary_peer_words,b->peer_words,sizeof(b->peer_words));
    s_boundary_guard=b->guard;s_boundary_request=b->request;s_boundary_request_session=b->request_session;
    s_boundary_request_mode=b->request_mode;s_boundary_request_delta=b->request_delta;s_boundary_tx_done=b->tx_done;
    s_dpll_role_requested_generation=s_dpll_role_applied_generation=s_vdc_domain.control.profile.generation;
    now_ns=local_at(slot,physical_ns);raw_now=now_ns/4u;now_ms=(uint32_t)(now_ns/1000000u);
    stopped=false;ring_available=live_available=true;
}
static void auto_initialize(void)
{
    for(unsigned slot=0;slot<4;slot++) {
        memset(s_committed_model_words,0,sizeof(s_committed_model_words));
        memset(s_boundary_meta_words,0,sizeof(s_boundary_meta_words));
        memset(s_boundary_peer_words,0,sizeof(s_boundary_peer_words));
        s_model_feedback_session=s_model_feedback_last_session=s_committed_model_guard=s_committed_model_serial=0;
        s_boundary_guard=s_boundary_request=s_boundary_request_session=s_boundary_request_mode=s_boundary_tx_done=0;
        s_boundary_request_delta=0;stopped=true;setup(slot==0);
        if(slot>1) {vdc_domain_set_schedule_local_slot(&s_vdc_domain,slot);ring.local_slot_id=slot;}
        s_vdc_domain.dco.period_adjust_ppb=0;
        s_vdc_domain.dco.base_local_tick64=0;s_vdc_domain.dco.base_vdc_time64_ns=0;
        s_vdc_domain.dco.phase_offset_ns=0;
        ++s_committed_model_guard;model_feedback_end_core1(123);
        ring.enabled=0;stopped=true;assert(vdc_dpll_manager_set_boundary_auto(true));
        ring.enabled=1;stopped=false;tick();assert(vdc_dpll_manager_boundary_auto_enabled());
        assert(status(slot?slot:1).schema==3);auto_save(slot);
    }
    memset(feedback,0,sizeof(feedback));memset(s_feedback_matches,0,sizeof(s_feedback_matches));
    memset(&incoming,0,sizeof(incoming));
    captured_offers=captured_applies=captured_acks=0;auto_load(0);
}
static vdc_dpll_manager_committed_model_t auto_model(unsigned slot)
{
    vdc_dpll_manager_committed_model_t m;
    memcpy(&m,auto_boards[slot].model_words,sizeof(m));return m;
}
static uint64_t ideal_coordinate(unsigned slot,uint64_t physical)
{
    const int64_t rate=auto_boards[slot].domain.dco.period_adjust_ppb;
    return (uint64_t)(((__uint128_t)physical*(uint64_t)(1000000000ll+oscillator[slot])*
        (uint64_t)(1000000000ll+rate))/((__uint128_t)1000000000u*1000000000u));
}
static void auto_feedback(unsigned slot,uint32_t seq,uint64_t sample_time)
{
    const vdc_dpll_manager_committed_model_t m=auto_model(slot);uint64_t absolute;
    assert(vdc_domain_dco_local_to_output_ns(&m.dco,local_at(slot,sample_time),&absolute));
    distributed_refmem_vdc_feedback_rx_snapshot_t *rx=&feedback[slot];
    const uint32_t count=rx->receive_count+1u;
    *rx=(distributed_refmem_vdc_feedback_rx_snapshot_t){.schema=1,.active=1,.retained=1,
        .source_slot=slot,.ring_config_seq=ring.config_seq,.role_generation=s_vdc_domain.control.profile.generation,
        .schedule_crc32=ring.schedule_crc32,.clock_epoch_id=s_vdc_domain.clock.epoch_id,
        .clock_run_id=s_vdc_domain.clock.run_id,.receive_count=count,
        .sample={.source_arm_epoch=77,.source_clock_epoch_id=m.clock_epoch_id,.source_clock_run_id=m.clock_run_id,
            .observer_epoch=8,.measurement_sequence=seq,.tick_hz=BOARD_SYS_CLOCK_HZ,
            .schema_version=REFMEM_VDC_FEEDBACK_RATE_SCHEMA,.source_slot=(uint8_t)slot,.target_slot=0,
            .domain_flags=REFMEM_VDC_FEEDBACK_RATE_FLAGS,.rate={.absolute_output_ns_lo=absolute,
                .coordinate_ns=ideal_coordinate(slot,sample_time),.model_token=m.token,
                .applied_command_seq=m.applied_command_seq,.control_session=123}}};
    assert(refmem_sync_vdc_feedback_encode(&rx->sample,4,rx->record));
    refmem_sync_vdc_feedback_record_t decoded;
    assert(refmem_sync_vdc_feedback_decode(rx->record,4,slot,0,&decoded));rx->sample=decoded;
}
static void auto_window(unsigned slot,int64_t force_lo,int64_t force_hi)
{
    auto_load(0);const vdc_dpll_manager_committed_model_t master=model(),peer=auto_model(slot);
    vdc_feedback_match_cache_t cache;vdc_feedback_match_peer_t match_peer;
    vdc_feedback_match_cache_init(&cache);vdc_feedback_match_peer_init(&match_peer);
    assert(vdc_feedback_match_cache_bind(&cache,origin_epoch,BOARD_SYS_CLOCK_HZ));
    const uint64_t start=physical_ns;uint32_t seq=auto_measurement;
    for(unsigned endpoint=0;endpoint<3;endpoint++) {
        const uint64_t at=start+(endpoint==0?0:endpoint==1?70000000ull:1100000000ull);
        const uint64_t ref=ideal_coordinate(0,at);
        assert(vdc_feedback_model_cache_put(&cache,seq,master.token,2u*(endpoint+1),ref-126,ref+126,now_ms));
        const vdc_feedback_match_sample_t input={.source_arm_epoch=77,
            .rx_elapsed_cycles=ideal_coordinate(slot,at),.source_clock_epoch_id=peer.clock_epoch_id,
            .source_clock_run_id=peer.clock_run_id,.observer_epoch=8,.measurement_sequence=seq,.tick_hz=BOARD_SYS_CLOCK_HZ};
        const vdc_feedback_match_result_t result=vdc_feedback_rate_update(&cache,&match_peer,&input,peer.token);
        assert(result==(endpoint==0?VDC_FEEDBACK_MATCH_BASELINED:endpoint==1?VDC_FEEDBACK_MATCH_WAIT_WINDOW:VDC_FEEDBACK_MATCH_MATCHED));
        if(endpoint==2) {
            physical_ns=at+40000000u;auto_load(0);auto_feedback(slot,seq,at);
            vdc_dpll_manager_feedback_match_status_t matched={.schema=3,.active=1,.source_slot=slot,
                .target_slot=0,.ring_config_seq=ring.config_seq,.role_generation=master.role_generation,
                .schedule_crc32=ring.schedule_crc32,.clock_epoch_id=master.clock_epoch_id,
                .clock_run_id=master.clock_run_id,.receive_count=feedback[slot].receive_count,
                .last_result=VDC_FEEDBACK_MATCH_MATCHED,.preparation_generation=17,.control_session=123};
            assert(vdc_feedback_match_peer_snapshot(&match_peer,&matched.result));
            matched.result.reference_generation=17;
            if(force_lo<=force_hi) {matched.result.raw_ppb_lo=force_lo;matched.result.raw_ppb_hi=force_hi;}
            match_publish(&s_feedback_matches[slot],&matched);
        }
        seq++;
    }
    auto_measurement=seq+1;auto_save(0);
}
static refmem_sync_vdc_boundary_command_t auto_offer(unsigned slot)
{
    auto_load(0);roundtrip();const vdc_dpll_boundary_status_t s=status(slot);
    assert(s.offer_id && s.peer_state==VDC_BOUNDARY_PENDING && s.command.flags==REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS);
    refmem_sync_vdc_boundary_command_t command;uint32_t id;
    assert(vdc_dpll_manager_copy_boundary_command_offer(ring.config_seq,
        s_vdc_domain.control.profile.generation,0,ring.schedule_crc32,123,&command,&id)==1);
    assert(id==s.offer_id && command.command_seq==auto_model(slot).applied_command_seq+1u);
    auto_save(0);return command;
}
static bool auto_fragments(unsigned slot,const refmem_sync_vdc_boundary_command_t *cmd,bool drop)
{
    uint8_t wire[64],complete[64];assert(refmem_sync_vdc_boundary_command_encode(cmd,4,wire));bool done=false;
    for(unsigned i=0;i<16;i++) {
        if(drop && i==7) {transport_sequence++;continue;}
        const refmem_sync_vdc_feedback_result_t result=refmem_sync_vdc_boundary_command_push(
            &command_assembly[slot],0,slot,4,transport_sequence++,(uint8_t)i,16,wire+4*i,now_ms+i,complete);
        if(result==REFMEM_VDC_FEEDBACK_COMPLETE)done=true;
    }
    if(!done)return false;
    refmem_sync_vdc_boundary_command_t decoded;assert(refmem_sync_vdc_boundary_command_decode(complete,4,0,slot,&decoded));
    auto_load(slot);const vdc_dpll_manager_committed_model_t m=model();
    incoming=(distributed_refmem_vdc_boundary_view_t){.active=1,.ring_config_seq=ring.config_seq,
        .role_generation=m.role_generation,.schedule_crc32=ring.schedule_crc32,
        .clock_epoch_id=m.clock_epoch_id,.clock_run_id=m.clock_run_id,.command=decoded};
    live.record.sequence=cmd->basis_measurement_sequence+1u;
    uint64_t before,after;assert(vdc_domain_dco_local_to_output_ns(&s_vdc_domain.dco,now_ns,&before));
    const uint32_t count=s_vdc_domain.control.follower_apply_count;
    tick();assert(vdc_domain_dco_local_to_output_ns(&s_vdc_domain.dco,now_ns,&after));assert(before==after);
    assert(s_vdc_domain.control.follower_apply_count==count+(m.applied_command_seq<cmd->command_seq?1u:0u));
    assert(model().applied_command_seq==cmd->command_seq);auto_save(slot);return true;
}
static void auto_ack(unsigned slot)
{
    auto_load(0);auto_feedback(slot,auto_measurement++,physical_ns);roundtrip();
    assert(status(slot).peer_state==VDC_BOUNDARY_ACKED || status(slot).peer_state==VDC_BOUNDARY_LATE_APPLIED);
    assert(!(status(slot).consumed_mask&(1u<<slot)));auto_save(0);
}
static void auto_test(const char *name)
{
    auto_initialize();
    if(!strcmp(name,"auto_closed_loop")) {
        for(unsigned round=0;round<24;round++)for(unsigned slot=1;slot<4;slot++) {
            auto_window(slot,1,0);auto_load(0);roundtrip();
            if(!status(slot).offer_id) {auto_save(0);continue;}
            auto_save(0);const refmem_sync_vdc_boundary_command_t cmd=auto_offer(slot);
            assert(auto_fragments(slot,&cmd,false));auto_ack(slot);
            printf("UPDATE %u %u %d %d\n",slot,cmd.command_seq,
                auto_boards[slot].domain.dco.period_adjust_ppb,cmd.signed_delta_rate_ppb);
        }
        assert(captured_applies==captured_acks && captured_offers==captured_applies && captured_applies>=9);
    } else if(!strcmp(name,"auto_loss_and_reconcile")) {
        auto_window(1,1,0);const refmem_sync_vdc_boundary_command_t cmd=auto_offer(1);
        assert(!auto_fragments(1,&cmd,true));assert(auto_fragments(1,&cmd,false));
        const uint32_t applied=auto_boards[1].domain.control.follower_apply_count;
        assert(auto_fragments(1,&cmd,false));assert(auto_boards[1].domain.control.follower_apply_count==applied);
        auto_load(0);now_ms+=VDC_BOUNDARY_ACK_TIMEOUT_MS;roundtrip();
        assert(status(1).peer_state==VDC_BOUNDARY_EXPIRED_UNRESOLVED && !status(1).offer_id);auto_save(0);
        auto_window(2,1,0);const refmem_sync_vdc_boundary_command_t other=auto_offer(2);
        assert(auto_fragments(2,&other,false));auto_ack(2);
        auto_ack(1);auto_window(1,1,0);const refmem_sync_vdc_boundary_command_t next=auto_offer(1);
        assert(next.command_seq==cmd.command_seq+1 && next.expected_target_model_token>cmd.expected_target_model_token);
        assert(auto_fragments(1,&next,false));auto_ack(1);
    } else if(!strcmp(name,"auto_uncertain")) {
        auto_window(1,-100,100);auto_load(0);roundtrip();
        assert(!status(1).offer_id && !status(1).consumed_mask && !captured_offers);
        auto_save(0);auto_window(1,10,10);auto_load(0);roundtrip();assert(!status(1).offer_id);
    } else if(!strcmp(name,"auto_revoke")) {
        auto_window(1,1,0);(void)auto_offer(1);auto_load(0);
        assert(!vdc_dpll_manager_set_boundary_auto(false));ring.enabled=0;stopped=true;tick();
        assert(!vdc_dpll_manager_boundary_auto_enabled() && !status(1).offer_id);
        ring.enabled=1;ring.config_seq++;ring.applied_config_seq++;stopped=false;roundtrip();assert(!status(1).offer_id);
        ring.enabled=0;stopped=true;assert(vdc_dpll_manager_set_boundary_auto(false));tick();
        assert(!vdc_dpll_manager_boundary_auto_enabled());assert(vdc_dpll_manager_set_feedback_session(0));
        assert(!vdc_dpll_manager_set_boundary_auto(true));
    } else if(!strcmp(name,"auto_reference_change")) {
        auto_window(1,1,0);(void)auto_offer(1);auto_load(0);
        s_vdc_domain.dco.period_adjust_ppb++;
        ++s_committed_model_guard;model_feedback_end_core1(123);
        refmem_sync_vdc_boundary_command_t out;uint32_t id;
        assert(vdc_dpll_manager_copy_boundary_command_offer(ring.config_seq,
            s_vdc_domain.control.profile.generation,0,ring.schedule_crc32,123,&out,&id)==0);
        now_ms+=VDC_BOUNDARY_ACK_TIMEOUT_MS;roundtrip();assert(status(1).peer_state==VDC_BOUNDARY_EXPIRED_UNRESOLVED);
    } else if(!strcmp(name,"auto_stale_window")) {
        auto_window(1,1,0);const refmem_sync_vdc_boundary_command_t first=auto_offer(1);
        assert(auto_fragments(1,&first,false));auto_ack(1);auto_load(0);
        const uint32_t serial=status(1).offer_serial;roundtrip();
        assert(!status(1).offer_id && status(1).offer_serial==serial);auto_save(0);
        auto_window(1,1,0);auto_load(0);vdc_dpll_manager_feedback_match_status_t m;
        match_load_words(s_feedback_matches[1].words,&m,sizeof(m));
        m.result.pairs[0].source_model_token=first.expected_target_model_token;
        match_publish(&s_feedback_matches[1],&m);roundtrip();assert(!status(1).offer_id);
    } else if(!strcmp(name,"auto_age_domains")) {
        auto_window(1,1,0);const refmem_sync_vdc_boundary_command_t c=auto_offer(1);
        auto_load(0);uint64_t rate_now;
        assert(vdc_model_rate_coordinate_ns(s_vdc_domain.dco.period_adjust_ppb,BOARD_SYS_CLOCK_HZ,raw_now,true,&rate_now));
        assert(rate_now>c.basis_source_output_ns_lo-1000000000ull);
        physical_ns+=VDC_BOUNDARY_COMMAND_MAX_AGE_NS;auto_load(0);
        refmem_sync_vdc_boundary_command_t out;uint32_t id;
        assert(vdc_dpll_manager_copy_boundary_command_offer(ring.config_seq,
            s_vdc_domain.control.profile.generation,0,ring.schedule_crc32,123,&out,&id)==0);
        assert(!auto_fragments(1,&c,true));
        auto_load(1);const vdc_domain_context_t before=s_vdc_domain;follower_input();
        incoming.command=c;incoming.command.flags=REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS;
        live.record.sequence=c.basis_measurement_sequence+1;tick();
        assert(!memcmp(&before,&s_vdc_domain,sizeof(before)) && status(1).last_reject==BOUNDARY_REJECT_AGE);
    } else if(!strcmp(name,"auto_mode_contention")) {
        auto_load(0);bool enabled=false;
        assert(vdc_dpll_manager_try_boundary_auto_enabled(&enabled) && enabled);
        ++s_boundary_guard;
        assert(!vdc_dpll_manager_try_boundary_auto_enabled(&enabled) && enabled);
        ++s_boundary_guard;++s_boundary_request;
        assert(!vdc_dpll_manager_try_boundary_auto_enabled(&enabled) && enabled);
        ++s_boundary_request;ring.enabled=0;stopped=true;
        assert(vdc_dpll_manager_set_boundary_auto(false));tick();
        assert(vdc_dpll_manager_try_boundary_auto_enabled(&enabled) && !enabled);
    } else if(!strcmp(name,"auto_disable_modes")) {
        auto_window(1,1,0);const refmem_sync_vdc_boundary_command_t c=auto_offer(1);
        auto_load(1);ring.enabled=0;stopped=true;assert(vdc_dpll_manager_set_boundary_probe(0));tick();
        assert(!vdc_dpll_manager_boundary_auto_enabled());auto_save(1);
        auto_load(1);ring.enabled=1;stopped=false;incoming=(distributed_refmem_vdc_boundary_view_t){
            .active=1,.ring_config_seq=ring.config_seq,.role_generation=s_vdc_domain.control.profile.generation,
            .schedule_crc32=ring.schedule_crc32,.clock_epoch_id=s_vdc_domain.clock.epoch_id,
            .clock_run_id=s_vdc_domain.clock.run_id,.command=c};
        live.record.sequence=c.basis_measurement_sequence+1;tick();assert(!status(1).apply_count);
        auto_load(0);ring.enabled=0;stopped=true;assert(vdc_dpll_manager_set_feedback_session(0));tick();
        assert(!vdc_dpll_manager_boundary_auto_enabled() && !status(1).offer_id);
        assert(vdc_dpll_manager_set_feedback_session(124));ring.enabled=1;stopped=false;roundtrip();
        assert(!status(1).offer_id && !vdc_dpll_manager_boundary_auto_enabled());
    } else if(!strcmp(name,"auto_delta_limits")) {
        auto_window(1,INT64_MAX,INT64_MAX);const refmem_sync_vdc_boundary_command_t c=auto_offer(1);
        assert(c.signed_delta_rate_ppb==-VDC_BOUNDARY_AUTO_MAX_DELTA_PPB);
        assert(auto_fragments(1,&c,false));auto_ack(1);
        auto_window(2,INT64_MIN,INT64_MIN);const refmem_sync_vdc_boundary_command_t p=auto_offer(2);
        assert(p.signed_delta_rate_ppb==VDC_BOUNDARY_AUTO_MAX_DELTA_PPB);
        auto_load(2);follower_input();incoming.command=p;incoming.command.signed_delta_rate_ppb=1001;
        incoming.command.target_slot=2;live.record.sequence=p.basis_measurement_sequence+1;
        vdc_domain_context_t before=s_vdc_domain;tick();assert(!memcmp(&before,&s_vdc_domain,sizeof(before)));
        incoming.command.signed_delta_rate_ppb=0;tick();assert(!memcmp(&before,&s_vdc_domain,sizeof(before)));
    } else if(!strcmp(name,"auto_pending_immutable")) {
        auto_window(1,1,0);const refmem_sync_vdc_boundary_command_t c=auto_offer(1);
        auto_load(0);const uint32_t serial=status(1).offer_serial;
        vdc_dpll_manager_feedback_match_status_t pair;
        match_load_words(s_feedback_matches[1].words,&pair,sizeof(pair));
        pair.result.raw_ppb_lo=-2000;pair.result.raw_ppb_hi=-1000;
        match_publish(&s_feedback_matches[1],&pair);roundtrip();
        assert(status(1).offer_serial==serial);
        const vdc_dpll_boundary_status_t unchanged=status(1);assert(!memcmp(&unchanged.command,&c,sizeof(c)));
        auto_save(0);assert(auto_fragments(1,&c,false));auto_ack(1);
        auto_load(0);roundtrip();
        assert(!status(1).offer_id && status(1).offer_serial==serial);
    } else if(!strcmp(name,"auto_wrong_domain")) {
        auto_window(1,1,0);auto_load(0);
        feedback[1].sample.schema_version=REFMEM_VDC_FEEDBACK_MODEL_SCHEMA;roundtrip();assert(!status(1).offer_id);
        feedback[1].sample.schema_version=REFMEM_VDC_FEEDBACK_RATE_SCHEMA;
        vdc_dpll_manager_feedback_match_status_t pair;
        match_load_words(s_feedback_matches[1].words,&pair,sizeof(pair));
        pair.result.reserved=VDC_FEEDBACK_MODEL_DOMAIN;match_publish(&s_feedback_matches[1],&pair);
        roundtrip();assert(!status(1).offer_id && !status(1).consumed_mask);
    } else assert(0);
}
'''
