"""Reference-only schema2 forwarding through the real RefMem FIFO and codec."""
import subprocess

import pytest

from test_vdc_boundary_command_transport import FIFO_SEAM, MANAGER, TESTS
from test_vdc_command_owner import compile_executable
from test_vdc_feedback_transport import FEEDBACK_SOURCES, make_feedback_harness


def make_reference_harness():
    harness = make_feedback_harness(MANAGER).replace(
        'int main(int argc,char **argv)', 'int feedback_existing_main(int argc,char **argv)')
    harness = harness.replace('static tdma_service_service_t owner;', FIFO_SEAM +
                              '\nstatic tdma_service_service_t owner;')
    harness = harness.replace('tdma_flight_fifo_core0_publish_tx(&service->flight_fifo,',
                              'boundary_test_fifo_publish(&service->flight_fifo,')
    harness = harness.replace('static bool binding_available=true,',
                              'static bool ordinary_available=true;\nstatic bool binding_available=true,')
    needle = 'return binding_available;\n}\nbool vdc_dpll_manager_get_snapshot'
    assert needle in harness
    harness = harness.replace(needle,
        'return binding_available && ordinary_available;\n}\nbool vdc_dpll_manager_get_snapshot')
    harness += TESTS.replace('int main(int argc,char **argv)',
                            'int boundary_existing_main(int argc,char **argv)')
    return harness + REFERENCE_TESTS


@pytest.fixture(scope='module')
def reference_executable(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp('reference-publish'), 'reference_publish',
                              make_reference_harness(), FEEDBACK_SOURCES)


@pytest.mark.parametrize('case', [
    'tx_freeze', 'tx_round_robin', 'tx_busy', 'tx_fifo_failure', 'tx_stop',
    'tx_epoch_change', 'tx_session_change', 'tx_mode_change', 'tx_no_reference',
    'tx_no_reprojection', 'tx_nonzero_master', 'follower_no_feedback_tx',
    'rx_complete', 'rx_duplicate', 'rx_conflict', 'rx_stale', 'rx_wrong_target',
    'rx_wrong_master', 'rx_wrong_session', 'rx_bad_crc', 'rx_rate_rejected',
    'rx_command_rejected', 'rx_pending_mode_busy', 'rx_pending_ordinary_busy',
    'rx_pending_stop', 'rx_pending_mode_change', 'rx_pending_session_change',
    'rx_pending_expiry', 'rx_old_admission', 'rx_partial_stop', 'master_no_feedback_rx',
])
def test_reference_transport(reference_executable, case):
    result = subprocess.run([str(reference_executable), case], capture_output=True,
                            text=True, timeout=10)
    (reference_executable.parent / (case + '.log')).write_text(
        result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr


REFERENCE_TESTS = r'''
_Static_assert(sizeof(distributed_refmem_feedback_binding_t)==44u,"binding no growth");
_Static_assert(sizeof(distributed_refmem_feedback_source_t)==276u,"RX no growth");
_Static_assert(sizeof(s_feedback_tx_record)==64u,"one frozen TX group");
_Static_assert(sizeof(s_boundary_pending_admission)+sizeof(s_boundary_pending_completed_ms)==8u,
               "reference pending reuses command pending storage");
static void reference_setup(uint32_t local)
{
    reference_mode=true;session=123;setup(local);
    ring.node_count=s_tdma_flight_sync.node_count=4;s_tdma_flight_sync.active_mask=15;
    origin_reference=(tdma_origin_raw_reference_t){.timer_lower=UINT64_C(0x1234567890),
        .timer_upper=UINT64_C(0x1234567897),.epoch=12,.sequence=100,
        .identity=0x2345,.published_version=2,.tick_hz=250000000};
    refresh();offer=command();offer_result=1;
}
static void advance_reference(void)
{ origin_reference.sequence++;origin_reference.published_version+=2;
  origin_reference.timer_lower+=100000;origin_reference.timer_upper+=100000; }
static void ordinary(void)
{ uint8_t mailbox[32];publish();consume(mailbox);assert(mailbox[3]==0x10); }
static refmem_sync_vdc_feedback_record_t reference_record(uint32_t target)
{
    refmem_sync_vdc_feedback_record_t value={.schema_version=2,.domain_flags=15,
        .source_slot=0,.target_slot=target,.source_clock_epoch_id=31,.source_clock_run_id=41,
        .source_arm_epoch=12,.observer_epoch=12,.measurement_sequence=100,.tick_hz=250000000,
        .model={.output_ns_lo=UINT64_C(0x123456789abcdef0),
            .output_ns_hi=UINT64_C(0x123456789abcdef9),.model_token=7,
            .applied_command_seq=0,.control_session=123}};
    return value;
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *test=argv[1];uint8_t wire[64],mailbox[32];
    if(!strncmp(test,"tx_",3)) {
        reference_setup(0);
        if(!strcmp(test,"tx_no_reference")) {
            origin_reference.epoch=0;ordinary();assert(!txread().groups_published && !offer_reads);
        } else if(!strcmp(test,"tx_round_robin") || !strcmp(test,"tx_nonzero_master")) {
            const bool nonzero=!strcmp(test,"tx_nonzero_master");
            if(nonzero) {
                ring.local_slot_id=ring.reference_slot_id=2;profile.schedule.local_slot_id=2;
                s_tdma_flight_sync.local_slot=2;refresh();
            }
            for(unsigned i=0;i<9;i++) {
                group(wire);refmem_sync_vdc_feedback_record_t out;
                const unsigned target=nonzero?(i%3==0?3:i%3-1):1+i%3;
                assert(refmem_sync_vdc_feedback_decode(wire,4,nonzero?2:0,target,&out));
                assert(out.schema_version==2 && out.model.control_session==123 &&
                    out.model.applied_command_seq==0 && out.source_arm_epoch==12 && out.observer_epoch==12);
                ordinary();advance_reference();
            }
            assert(txread().groups_published==9 && !offer_reads && !done_count);
        } else if(!strcmp(test,"tx_no_reprojection")) {
            group(wire);ordinary();origin_reference.timer_lower++;origin_reference.timer_upper++;
            ordinary();assert(txread().groups_published==1);
        } else {
            const uint64_t first_lo=origin_reference.timer_lower*4,first_hi=origin_reference.timer_upper*4+999;
            publish();consume(mailbox);assert(mailbox[8]==0);memcpy(wire,mailbox+10,4);
            if(!strcmp(test,"tx_stop") || !strcmp(test,"tx_session_change") ||
               !strcmp(test,"tx_mode_change") || !strcmp(test,"tx_epoch_change")) {
                if(!strcmp(test,"tx_stop"))ring.enabled=0;
                if(!strcmp(test,"tx_session_change"))session++;
                if(!strcmp(test,"tx_mode_change"))reference_mode=false;
                if(!strcmp(test,"tx_epoch_change"))origin_reference.epoch++;
                refresh();publish();assert(!s_feedback_tx_active && !txread().groups_published);
            } else {
                if(!strcmp(test,"tx_busy") || !strcmp(test,"tx_fifo_failure")) {
                    if(!strcmp(test,"tx_busy"))origin_available=false;else publication_blocked=true;
                    const uint32_t seq=s_tdma_flight_sync.next_seq32;publish();assert_no_tx();
                    assert(s_feedback_tx_index==1 && s_tdma_flight_sync.next_seq32==seq);
                    origin_available=true;publication_blocked=false;
                }
                advance_reference();
                for(unsigned i=1;i<16;i++){publish();consume(mailbox);assert(mailbox[8]==i);memcpy(wire+i*4,mailbox+10,4);}
                refmem_sync_vdc_feedback_record_t out;
                assert(refmem_sync_vdc_feedback_decode(wire,4,0,1,&out));
                assert(out.measurement_sequence==100 && out.model.output_ns_lo==first_lo &&
                    out.model.output_ns_hi==first_hi && txread().groups_published==1 && !offer_reads);
            }
        }
    } else {
        reference_setup(!strcmp(test,"master_no_feedback_rx")?0:2);
        refmem_sync_vdc_feedback_record_t value=reference_record(2);
        if(!strcmp(test,"follower_no_feedback_tx")) {
            for(unsigned i=0;i<20;i++)ordinary();
            assert(!txread().groups_published && !offer_reads);return 0;
        }
        if(!strcmp(test,"rx_wrong_target"))value.target_slot=3;
        if(!strcmp(test,"rx_wrong_master"))value.source_slot=1;
        if(!strcmp(test,"rx_wrong_session"))value.model.control_session++;
        if(!strcmp(test,"master_no_feedback_rx")){value.source_slot=1;value.target_slot=0;}
        if(!strcmp(test,"rx_rate_rejected")){value.schema_version=4;value.domain_flags=31;}
        assert(refmem_sync_vdc_feedback_encode(&value,4,wire));
        if(!strcmp(test,"rx_bad_crc"))wire[38]^=1;
        if(!strcmp(test,"rx_command_rejected")) {
            assert(refmem_sync_vdc_boundary_command_encode(&offer,4,wire));command_group(wire,0,100);
            assert(!rxread(0).retained);return 0;
        }
        if(!strncmp(test,"rx_pending_",11)) {
            for(unsigned i=0;i<15;i++){enqueue(wire,0,100+i,i,0);receive();}
            if(!strcmp(test,"rx_pending_ordinary_busy"))ordinary_available=false;else mode_available=false;
            enqueue(wire,0,115,15,0);receive();assert(!rxread(0).retained);
            assert(!memcmp(s_feedback_rx[0].assembly.payload,wire,64));
            const uint32_t completed=now_ms,first=s_feedback_rx[0].assembly.first_ms;
            receive_group(wire,0,200);assert(!rxread(0).retained);
            assert(!memcmp(s_feedback_rx[0].assembly.payload,wire,64));
            mode_available=ordinary_available=true;
            const bool cancelled=strcmp(test,"rx_pending_mode_busy") && strcmp(test,"rx_pending_ordinary_busy");
            if(!strcmp(test,"rx_pending_stop")){ring.enabled=0;refresh();ring.enabled=1;}
            if(!strcmp(test,"rx_pending_mode_change"))reference_mode=false;
            if(!strcmp(test,"rx_pending_session_change"))session++;
            if(!strcmp(test,"rx_pending_expiry"))now_ms=first+1000;
            refresh();distributed_refmem_vdc_feedback_rx_snapshot_t received=rxread(0);
            if(cancelled)assert(!received.retained && !received.receive_count);
            else {
                assert(received.retained && received.active && received.receive_count==1);
                assert(received.first_rx_ms==first && received.last_rx_ms==completed);
                assert(!memcmp(received.record,wire,64));refresh();assert(rxread(0).receive_count==1);
            }
        } else if(!strcmp(test,"rx_old_admission") || !strcmp(test,"rx_partial_stop")) {
            enqueue(wire,0,100,0,0);
            if(!strcmp(test,"rx_partial_stop"))receive();
            ring.enabled=0;refresh();ring.enabled=1;refresh();receive();
            for(unsigned i=1;i<16;i++){enqueue(wire,0,100+i,i,0);receive();}
            assert(!rxread(0).retained);
        } else {
            receive_group(wire,value.source_slot,100);
            if(!strcmp(test,"rx_complete") || !strcmp(test,"rx_duplicate") ||
               !strcmp(test,"rx_conflict") || !strcmp(test,"rx_stale")) {
                distributed_refmem_vdc_feedback_rx_snapshot_t received=rxread(0);
                assert(received.active && received.retained && received.receive_count==1);
                assert(!memcmp(received.record,wire,64) && !memcmp(&received.sample,&value,sizeof(value)));
                assert(received.clock_epoch_id==3 && received.sample.source_clock_epoch_id==31);
                assert(received.clock_run_id==4 && received.sample.source_clock_run_id==41);
                if(strcmp(test,"rx_complete")) {
                    if(!strcmp(test,"rx_conflict"))value.model.output_ns_hi++;
                    if(!strcmp(test,"rx_stale"))value.measurement_sequence--;
                    assert(refmem_sync_vdc_feedback_encode(&value,4,wire));receive_group(wire,0,200);
                    assert(rxread(0).receive_count==1 && !memcmp(rxread(0).record,received.record,64));
                    assert(!strcmp(test,"rx_duplicate")?rxread(0).duplicate_count==1:rxread(0).reject_count==1);
                }
                distributed_refmem_vdc_boundary_view_t command_view;
                assert(!distributed_refmem_copy_vdc_boundary_command(0,&command_view));
                ring.enabled=0;refresh();assert(!rxread(0).active && rxread(0).retained);
            } else assert(!rxread(0).retained && !rxread(1).retained);
        }
    }
    puts("reference transport scenario passed");return 0;
}
'''
