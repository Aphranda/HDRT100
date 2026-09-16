"""Exercise paced production publication through the real FIFO and codec.

These tests model Core0 time and observer phases, not DMA wire residence.
Hardware acceptance must measure delivery and LOCAL_FOLLOW freshness.
"""
import subprocess

import pytest

from test_vdc_command_owner import compile_executable
from test_vdc_feedback_transport import FEEDBACK_SOURCES
from test_vdc_reference_publish import make_reference_harness


@pytest.fixture(scope='module')
def executable(tmp_path_factory):
    harness = make_reference_harness().replace(
        'int main(int argc,char **argv)', 'int reference_existing_main(int argc,char **argv)')
    harness = harness.replace('role==9 && epoch==3 && run==4',
                              'role==profile.control_profile.generation && '
                              'epoch==profile.clock_epoch_id && run==profile.clock_run_id')
    return compile_executable(tmp_path_factory.mktemp('spacing'), 'spacing',
                              harness + CASES, FEEDBACK_SOURCES)


@pytest.mark.parametrize('case', [
    'boundaries', 'tick_wrap', 'fifo_pending', 'fifo_failure',
    'busy', 'stop', 'session', 'role', 'clock', 'arm', 'ring_interval',
    'observer_phases', 'observer_jitter',
    'ack_spacing', 'deferred_freeze', 'pending_arm_cancel',
])
def test_reference_spacing(executable, case):
    result = subprocess.run([str(executable), case], capture_output=True, text=True, timeout=10)
    (executable.parent / (case + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr


CASES = r'''
static void attempt(void) { distributed_refmem_tdma_flight_sync_publish(&owner,&ring); }
int main(int argc,char **argv)
{
    assert(argc==2);const char *test=argv[1];
    const bool ack=!strcmp(test,"ack_spacing");reference_setup(ack?2:0);
    uint8_t mailbox[32],wire[64],frozen[64];
    if(ack) {
        refmem_sync_vdc_feedback_record_t value=reference_record(2);
        assert(refmem_sync_vdc_feedback_encode(&value,4,wire));
        receive_group(wire,0,100);assert(rxread(0).receive_count==1);
    }
    if(!strcmp(test,"deferred_freeze")) {
        s_tdma_flight_sync.last_publish_ms=now_ms;
        const uint32_t before=now_ms;
        for(unsigned i=0;i<6;i++) {
            now_ms=before+i;advance_reference();attempt();assert_no_tx();
            assert(!s_feedback_tx_active && !txread().groups_published);
        }
        now_ms++;advance_reference();attempt();consume(mailbox);
        refmem_sync_vdc_feedback_record_t latest;
        assert(refmem_sync_vdc_feedback_decode(s_feedback_tx_record,4,0,1,&latest));
        assert(latest.measurement_sequence==origin_reference.sequence);return 0;
    }
    if(!strcmp(test,"tick_wrap"))now_ms=UINT32_MAX-2u;
    attempt();consume(mailbox);assert(mailbox[8]==0);
    memcpy(wire,mailbox+10,4);memcpy(frozen,s_feedback_tx_record,64);
    const uint32_t first_ms=now_ms,next_seq=s_tdma_flight_sync.next_seq32;
    if(!strcmp(test,"pending_arm_cancel")) {
        now_ms+=6;attempt();assert(s_feedback_tx_index==2);
        origin_reference.epoch++;now_ms++;attempt();
        assert(!s_feedback_tx_active && txread().cancel_count==1);
        /* Cancellation does not steal Core1's queued immutable FIFO lease. */
        consume(mailbox);assert(mailbox[8]==1);
        now_ms=first_ms+12;attempt();consume(mailbox);assert(mailbox[3]==0x10);
        now_ms+=6;attempt();consume(mailbox);assert(mailbox[8]==0);return 0;
    }
    if(!strcmp(test,"stop") || !strcmp(test,"session") || !strcmp(test,"role") ||
       !strcmp(test,"clock") || !strcmp(test,"arm")) {
        if(!strcmp(test,"stop"))ring.enabled=0;
        if(!strcmp(test,"session"))session++;
        if(!strcmp(test,"role"))profile.control_profile.generation++;
        if(!strcmp(test,"clock"))profile.clock_run_id++;
        if(!strcmp(test,"arm"))origin_reference.epoch++;
        now_ms++;refresh();attempt();
        assert(!s_feedback_tx_active && !txread().groups_published);
        assert(s_tdma_flight_sync.next_seq32==next_seq);assert_no_tx();
        if(!strcmp(test,"stop")) {ring.enabled=1;refresh();}
        now_ms=first_ms+6;attempt();consume(mailbox);assert(mailbox[3]==0x10);
        now_ms+=6;advance_reference();attempt();consume(mailbox);
        assert(mailbox[3]==TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS && mailbox[8]==0);return 0;
    }
    if(!strcmp(test,"fifo_pending")) {
        now_ms+=6;attempt();assert(s_feedback_tx_index==2);
        now_ms+=20;attempt();assert(s_feedback_tx_index==2);
        consume(mailbox);assert(mailbox[8]==1);
        attempt();consume(mailbox);assert(mailbox[8]==2);return 0;
    }
    if(!strcmp(test,"fifo_failure") || !strcmp(test,"busy")) {
        now_ms+=6;
        if(!strcmp(test,"busy"))origin_available=false;else publication_blocked=true;
        attempt();assert_no_tx();assert(s_feedback_tx_index==1);
        assert(s_tdma_flight_sync.next_seq32==next_seq && !memcmp(frozen,s_feedback_tx_record,64));
        origin_available=true;publication_blocked=false;now_ms+=6;
        attempt();consume(mailbox);assert(mailbox[8]==1);return 0;
    }
    if(!strcmp(test,"ring_interval")) {
        ring.feedback_timeout_ns=9000001u;
        now_ms+=9;attempt();assert_no_tx();assert(s_feedback_tx_index==1);
        now_ms++;attempt();consume(mailbox);assert(mailbox[8]==1);return 0;
    }
    uint8_t images[16][32];memcpy(images[0],mailbox,32);
    uint32_t published[16]={0};
    for(unsigned index=1;index<16;index++) {
        for(unsigned early=1;early<6;early++) {
            now_ms=first_ms+published[index-1]+early;advance_reference();attempt();assert_no_tx();
            assert(s_feedback_tx_index==index && !memcmp(frozen,s_feedback_tx_record,64));
        }
        const uint32_t jitter=!strcmp(test,"observer_jitter")?(index%3==0):0;
        published[index]=published[index-1]+6+jitter;
        now_ms=first_ms+published[index];attempt();consume(mailbox);
        assert(mailbox[8]==index && distributed_refmem_get_le32(mailbox+14)==index+1);
        memcpy(images[index],mailbox,32);memcpy(wire+index*4,mailbox+10,4);
    }
    assert(!memcmp(wire,frozen,64) && txread().groups_published==1);
    assert(published[15]<=95); /* Synthetic timing only; real event age remains gated. */
    for(unsigned early=1;early<6;early++) {
        now_ms=first_ms+published[15]+early;attempt();assert_no_tx();
        assert(s_tdma_flight_sync.next_seq32==17);
    }
    now_ms++;attempt();consume(mailbox);assert(mailbox[3]==0x10);
    if(!strncmp(test,"observer_",9)) {
        /* Observer samples a latest image with three possible offsets. Duplicate
         * copies must not consume sequence numbers or break strict assembly. */
        for(unsigned phase=0;phase<3;phase++) {
            refmem_sync_vdc_feedback_assembly_t assembly={0};unsigned complete=0;
            for(unsigned t=phase;t<published[15]+6;t+=3) {
                unsigned i=0;while(i<15 && published[i+1]<=t)i++;
                uint8_t received[64];
                const int result=refmem_sync_vdc_feedback_push(&assembly,0,1,4,
                    distributed_refmem_get_le32(images[i]+14),images[i][8],images[i][9],
                    images[i]+10,100+t,received);
                if(result==REFMEM_VDC_FEEDBACK_COMPLETE) {
                    complete++;assert(!memcmp(received,wire,64));
                }
            }
            assert(complete==1 && !memcmp(assembly.payload,wire,64));
        }
    }
    return 0;
}
'''
