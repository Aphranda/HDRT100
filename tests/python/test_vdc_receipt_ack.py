"""Real receipt codec, RefMem and FIFO; no EVENT/DCO prerequisite or hardware."""
import subprocess

import pytest

from test_vdc_command_owner import compile_executable
from test_vdc_feedback_transport import FEEDBACK_SOURCES
from test_vdc_reference_publish import make_reference_harness


@pytest.fixture(scope='module')
def receipt_executable(tmp_path_factory):
    harness = make_reference_harness().replace(
        'int main(int argc,char **argv)', 'int reference_existing_main(int argc,char **argv)')
    return compile_executable(tmp_path_factory.mktemp('receipt'), 'receipt',
                              harness + RECEIPT_TESTS, FEEDBACK_SOURCES)


@pytest.mark.parametrize('case', [
    'codec_exact', 'codec_mutation', 'codec_invalid', 'codec_type', 'codec_wrap',
    'follower_receipt', 'follower_freeze', 'follower_busy', 'follower_fifo',
    'follower_pending', 'follower_stop', 'follower_session', 'follower_mode',
    'follower_epoch', 'follower_source_clock', 'master_all_content_fields',
    'master_exact', 'master_duplicate', 'master_wrong_content', 'master_wrong_session',
    'master_bad_crc', 'master_feedback', 'master_no_proof', 'master_partial_proof',
    'master_evicted', 'master_sources', 'master_pending_busy', 'master_pending_stop',
    'master_pending_session', 'master_pending_epoch', 'master_pending_expiry',
    'master_pending_overwrite', 'master_old_admission', 'master_snapshot_busy',
    'master_stop_history', 'master_consumer_isolation', 'master_proof_fifo',
    'master_watermark_accept', 'master_watermark_reject', 'master_watermark_expiry',
    'master_watermark_wrap', 'master_to_model', 'master_to_rate',
])
def test_receipt_transport(receipt_executable, case):
    result = subprocess.run([str(receipt_executable), case], capture_output=True,
                            text=True, timeout=10)
    (receipt_executable.parent / (case + '.log')).write_text(
        result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr


RECEIPT_TESTS = r'''
_Static_assert(sizeof(distributed_refmem_vdc_feedback_rx_snapshot_t)==192,"same snapshot");
_Static_assert(sizeof(distributed_refmem_feedback_source_t)==276,"same per-source storage");
_Static_assert(sizeof(s_feedback_rx)==276*PROJECT_NODE_CAPACITY,"no ACK array");
static void wire_crc(uint8_t wire[64])
{ distributed_refmem_put_le32(wire+60,refmem_sync_vdc_feedback_crc32(wire,60)); }
static void receipt_group(const uint8_t wire[64],uint32_t source,uint32_t start)
{
    for(unsigned i=0;i<16;i++) {
        enqueue(wire,source,start,i,0);receive();
        start=refmem_sync_vdc_feedback_next_sequence(start);
    }
}
static distributed_refmem_vdc_feedback_rx_snapshot_t proofread(unsigned source)
{
    distributed_refmem_vdc_feedback_rx_snapshot_t out;
    assert(distributed_refmem_get_vdc_reference_proof(source,&out));return out;
}
static void master_reference(uint8_t original[64])
{ group(original);assert(proofread(original[2]).retained); }
static void follower_reference(uint8_t original[64],uint32_t sequence)
{
    refmem_sync_vdc_feedback_record_t value=reference_record(2);value.measurement_sequence=sequence;
    assert(refmem_sync_vdc_feedback_encode(&value,4,original));
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *test=argv[1];
    uint8_t original[64],ack[64],restored[64],mailbox[32],next[64];
    if(!strncmp(test,"codec_",6)) {
        refmem_sync_vdc_feedback_record_t value=reference_record(2);
        assert(refmem_sync_vdc_feedback_encode(&value,4,original));
        assert(refmem_sync_vdc_receipt_ack_encode(original,4,ack));
        assert(ack[0]==5 && ack[1]==2 && ack[2]==0 && ack[3]==1 && !memcmp(ack+4,original+4,56));
        assert(refmem_sync_vdc_receipt_ack_restore(ack,4,2,0,restored) && !memcmp(original,restored,64));
        if(!strcmp(test,"codec_exact")) {
            memcpy(restored,original,64);assert(refmem_sync_vdc_receipt_ack_encode(restored,4,restored));
            assert(!memcmp(restored,ack,64));assert(refmem_sync_vdc_receipt_ack_restore(restored,4,2,0,restored));
            assert(!memcmp(restored,original,64));
        } else if(!strcmp(test,"codec_mutation")) {
            for(unsigned i=0;i<64;i++) {
                memcpy(next,ack,64);next[i]^=1;memset(restored,0xa5,64);
                assert(!refmem_sync_vdc_receipt_ack_restore(next,4,2,0,restored));
                for(unsigned j=0;j<64;j++)assert(restored[j]==0xa5);
            }
        } else if(!strcmp(test,"codec_invalid")) {
            for(unsigned i=0;i<4;i++) {
                memcpy(next,ack,64);next[i]^=1;wire_crc(next);
                assert(!refmem_sync_vdc_receipt_ack_restore(next,4,2,0,restored));
            }
            const unsigned required[]={12,20,28,48,56};
            for(unsigned i=0;i<sizeof(required)/sizeof(required[0]);i++) {
                memcpy(next,ack,64);memset(next+required[i],0,required[i]==12?8:4);wire_crc(next);
                assert(!refmem_sync_vdc_receipt_ack_restore(next,4,2,0,restored));
            }
            memcpy(next,ack,64);memset(next+40,0,8);wire_crc(next);
            assert(!refmem_sync_vdc_receipt_ack_restore(next,4,2,0,restored));
            assert(!refmem_sync_vdc_receipt_ack_encode(NULL,4,ack));
            assert(!refmem_sync_vdc_receipt_ack_encode(original,4,NULL));
            assert(!refmem_sync_vdc_receipt_ack_restore(NULL,4,2,0,restored));
            assert(!refmem_sync_vdc_receipt_ack_restore(ack,4,2,0,NULL));
            assert(!refmem_sync_vdc_receipt_ack_restore(ack,4,0,2,restored));
        } else if(!strcmp(test,"codec_type")) {
            refmem_sync_vdc_feedback_assembly_t a={0};
            assert(refmem_sync_vdc_feedback_push(&a,2,0,4,1,0,16,ack,0,restored)==REFMEM_VDC_FEEDBACK_BAD_RECORD);
            refmem_sync_vdc_feedback_reset(&a);
            assert(refmem_sync_vdc_boundary_command_push(&a,2,0,4,1,0,16,ack,0,restored)==REFMEM_VDC_FEEDBACK_BAD_RECORD);
            refmem_sync_vdc_feedback_reset(&a);
            assert(refmem_sync_vdc_receipt_ack_push(&a,0,2,4,1,0,16,original,0,restored)==REFMEM_VDC_FEEDBACK_BAD_RECORD);
            assert(!refmem_sync_vdc_feedback_decode(ack,4,2,0,&value));
            assert(!refmem_sync_vdc_receipt_ack_encode(ack,4,restored));
        } else {
            refmem_sync_vdc_feedback_assembly_t a={0};uint32_t seq=UINT32_MAX-7;
            for(unsigned i=0;i<16;i++) {
                const int r=refmem_sync_vdc_receipt_ack_push(&a,2,0,4,seq,i,16,ack+i*4,i,restored);
                assert(r==(i==15?REFMEM_VDC_FEEDBACK_COMPLETE:REFMEM_VDC_FEEDBACK_PROGRESS));
                seq=refmem_sync_vdc_feedback_next_sequence(seq);
            }
            assert(!memcmp(restored,ack,64));
        }
    } else if(!strncmp(test,"follower_",9)) {
        reference_setup(2);live_available=tap_available=false;live.flags=0;
        follower_reference(original,100);receipt_group(original,0,100);
        assert(rxread(0).receive_count==1);assert(!done_count && !offer_reads);
        if(!strcmp(test,"follower_receipt")) {
            group(ack);assert(refmem_sync_vdc_receipt_ack_restore(ack,4,2,0,restored));
            assert(!memcmp(original,restored,64));ordinary();ordinary();assert(txread().groups_published==1);
        } else {
            publish();consume(mailbox);assert(mailbox[8]==0);memcpy(ack,mailbox+10,4);
            if(!strcmp(test,"follower_stop") || !strcmp(test,"follower_session") || !strcmp(test,"follower_mode")) {
                if(!strcmp(test,"follower_stop"))ring.enabled=0;
                if(!strcmp(test,"follower_session"))session++;
                if(!strcmp(test,"follower_mode"))reference_mode=false;
                refresh();assert(!s_feedback_tx_active && !txread().groups_published);
            } else {
                if(!strcmp(test,"follower_epoch") || !strcmp(test,"follower_source_clock")) {
                    refmem_sync_vdc_feedback_record_t changed=reference_record(2);
                    if(!strcmp(test,"follower_epoch")){changed.source_arm_epoch++;changed.observer_epoch++;}
                    else {changed.source_clock_epoch_id++;changed.source_clock_run_id++;}
                    assert(refmem_sync_vdc_feedback_encode(&changed,4,next));receipt_group(next,0,200);
                    publish();consume(mailbox);assert(mailbox[3]==0x10 && !s_feedback_tx_active);
                    assert(!txread().groups_published && txread().cancel_count==1);return 0;
                }
                if(!strcmp(test,"follower_fifo") || !strcmp(test,"follower_busy")) {
                    if(!strcmp(test,"follower_fifo"))publication_blocked=true;else mode_available=false;
                    publish();assert_no_tx();assert(s_feedback_tx_index==1);
                    publication_blocked=false;mode_available=true;
                }
                follower_reference(next,101);receipt_group(next,0,200);
                assert(rxread(0).receive_count==2 && !memcmp(rxread(0).record,next,64));
                if(!strcmp(test,"follower_pending")) {
                    follower_reference(restored,102);receipt_group(restored,0,300);
                    assert(rxread(0).receive_count==2 && (s_feedback_rx[0].assembly.state&4));
                    receipt_group(restored,0,400);assert(rxread(0).reject_count);
                }
                for(unsigned i=1;i<16;i++) {publish();consume(mailbox);memcpy(ack+i*4,mailbox+10,4);}
                assert(refmem_sync_vdc_receipt_ack_restore(ack,4,2,0,restored) && !memcmp(original,restored,64));
                ordinary();group(ack);
                assert(refmem_sync_vdc_receipt_ack_restore(ack,4,2,0,restored) && !memcmp(next,restored,64));
                if(!strcmp(test,"follower_pending")) {
                    refresh();assert(rxread(0).receive_count==3 && !(s_feedback_rx[0].assembly.state&4));
                    ordinary();group(ack);assert(refmem_sync_vdc_receipt_ack_restore(ack,4,2,0,restored));
                    assert(distributed_refmem_get_le32(restored+24)==102);
                }
            }
        }
    } else {
        reference_setup(0);
        if(!strcmp(test,"master_partial_proof") || !strcmp(test,"master_proof_fifo")) {
            for(unsigned i=0;i<15;i++){publish();consume(mailbox);assert(!proofread(1).retained);}
            if(!strcmp(test,"master_proof_fifo")) {
                publication_blocked=true;publish();assert_no_tx();assert(!proofread(1).retained);publication_blocked=false;
            }
            publish();consume(mailbox);assert(proofread(1).retained);return 0;
        }
        if(!strcmp(test,"master_no_proof")) {
            refmem_sync_vdc_feedback_record_t v=reference_record(1);assert(refmem_sync_vdc_feedback_encode(&v,4,original));
        } else master_reference(original);
        assert(refmem_sync_vdc_receipt_ack_encode(original,4,ack));
        if(!strcmp(test,"master_all_content_fields")) {
            for(unsigned offset=4;offset<60;offset++) {
                memcpy(next,ack,64);next[offset]^=1;wire_crc(next);
                receipt_group(next,1,100+32*offset);
                assert(!rxread(1).receive_count && !memcmp(proofread(1).record,original,64));
            }
            assert(rxread(1).reject_count>=56);return 0;
        }
        if(!strcmp(test,"master_consumer_isolation")) {
            distributed_refmem_vdc_feedback_rx_snapshot_t out;memset(&out,0xa5,sizeof(out));
            const distributed_refmem_vdc_feedback_rx_snapshot_t before=out;
            assert(!distributed_refmem_copy_vdc_feedback_rx(1,&out) && !memcmp(&out,&before,sizeof(out)));
            distributed_refmem_vdc_boundary_view_t command_out;
            assert(!distributed_refmem_copy_vdc_boundary_command(1,&command_out));return 0;
        }
        if(!strcmp(test,"master_snapshot_busy")) {
            distributed_refmem_vdc_feedback_rx_snapshot_t out;memset(&out,0xa5,sizeof(out));
            const distributed_refmem_vdc_feedback_rx_snapshot_t before=out;
            s_feedback_rx[1].guard|=1;assert(!distributed_refmem_get_vdc_reference_proof(1,&out));
            assert(!memcmp(&out,&before,sizeof(out)));s_feedback_rx[1].guard++;
            race_guard=&s_feedback_rx[1].guard;assert(!distributed_refmem_get_vdc_reference_proof(1,&out));
            assert(!memcmp(&out,&before,sizeof(out)));return 0;
        }
        if(!strcmp(test,"master_wrong_content")){ack[32]^=1;wire_crc(ack);}
        if(!strcmp(test,"master_wrong_session")){ack[56]^=1;wire_crc(ack);}
        if(!strcmp(test,"master_bad_crc"))ack[32]^=1;
        if(!strcmp(test,"master_feedback"))memcpy(ack,original,64);
        if(!strcmp(test,"master_evicted") || !strcmp(test,"master_sources")) {
            for(unsigned i=0;i<(!strcmp(test,"master_sources")?2u:3u);i++) {
                ordinary();advance_reference();master_reference(next);
            }
        }
        if(!strncmp(test,"master_pending_",15)) {
            for(unsigned i=0;i<15;i++){enqueue(ack,1,100+i,i,0);receive();}
            mode_available=false;enqueue(ack,1,115,15,0);receive();
            assert(!rxread(1).receive_count && (s_feedback_rx[1].assembly.state&4));
            const uint32_t first=s_feedback_rx[1].assembly.first_ms;
            mode_available=true;
            if(!strcmp(test,"master_pending_stop"))ring.enabled=0;
            if(!strcmp(test,"master_pending_session"))session++;
            if(!strcmp(test,"master_pending_epoch"))origin_reference.epoch++;
            if(!strcmp(test,"master_pending_expiry"))now_ms=first+1000;
            if(!strcmp(test,"master_pending_overwrite")) {
                for(unsigned i=0;i<3;i++){ordinary();advance_reference();master_reference(next);}
            }
            refresh();assert(!(s_feedback_rx[1].assembly.state&4));
            assert(rxread(1).receive_count==(!strcmp(test,"master_pending_busy")?1u:0u));return 0;
        }
        if(!strncmp(test,"master_watermark_",17)) {
            const bool wrap=!strcmp(test,"master_watermark_wrap");
            s_feedback_admission=77;owner.flight_fifo.rx_admission_epoch=77;
            const uint32_t start=wrap?UINT32_MAX-7:20;
            if(!strcmp(test,"master_watermark_reject")){ack[32]^=1;wire_crc(ack);}
            if(!strcmp(test,"master_watermark_expiry"))mode_available=false;
            receipt_group(ack,1,start);
            if(!strcmp(test,"master_watermark_expiry")) {
                now_ms=s_feedback_rx[1].assembly.first_ms+1000;mode_available=true;refresh();
            }
            assert(s_feedback_rx[1].assembly.first_sequence==start);
            assert(refmem_sync_vdc_receipt_ack_encode(original,4,ack));
            receipt_group(ack,1,77);assert(rxread(1).receive_count==1);return 0;
        }
        if(!strcmp(test,"master_old_admission")) {
            enqueue(ack,1,100,0,0);ring.enabled=0;refresh();ring.enabled=1;refresh();receive();
            for(unsigned i=1;i<16;i++){enqueue(ack,1,100+i,i,0);receive();}
            assert(!rxread(1).receive_count);return 0;
        }
        receipt_group(ack,1,100);
        const bool rejected=!strcmp(test,"master_wrong_content") || !strcmp(test,"master_wrong_session") ||
            !strcmp(test,"master_bad_crc") || !strcmp(test,"master_feedback") ||
            !strcmp(test,"master_no_proof") || !strcmp(test,"master_evicted");
        if(rejected){assert(!rxread(1).receive_count && rxread(1).reject_count);return 0;}
        assert(rxread(1).receive_count==1 && rxread(1).retained && !memcmp(rxread(1).record,ack,64));
        if(!strcmp(test,"master_to_model") || !strcmp(test,"master_to_rate")) {
            ring.enabled=0;refresh();reference_mode=false;
            auto_mode=!strcmp(test,"master_to_rate");ring.enabled=1;refresh();
            assert(!rxread(1).active && !proofread(1).active && proofread(1).retained);
            assert(!memcmp(proofread(1).record,original,64) && !memcmp(rxread(1).record,ack,64));
            refmem_sync_vdc_feedback_record_t value=reference_record(0);value.source_slot=1;
            if(auto_mode) {
                value.schema_version=4;value.domain_flags=31;memset(&value.rate,0,sizeof(value.rate));
                value.rate.absolute_output_ns_lo=1000;value.rate.coordinate_ns=2000;
                value.rate.model_token=7;value.rate.control_session=session+1;
            } else value.model.control_session=session+1;
            assert(refmem_sync_vdc_feedback_encode(&value,4,next));receipt_group(next,1,200);
            assert(rxread(1).receive_count==1 && rxread(1).reject_count && !rxread(1).active);
            assert(proofread(1).retained && !memcmp(proofread(1).record,original,64));
            assert(!memcmp(rxread(1).record,ack,64));
            distributed_refmem_vdc_feedback_rx_snapshot_t decoded;memset(&decoded,0xa5,sizeof(decoded));
            const distributed_refmem_vdc_feedback_rx_snapshot_t before=decoded;
            assert(!distributed_refmem_copy_vdc_feedback_rx(1,&decoded) && !memcmp(&decoded,&before,sizeof(decoded)));
            if(auto_mode)value.rate.control_session=session;else value.model.control_session=session;
            assert(refmem_sync_vdc_feedback_encode(&value,4,next));receipt_group(next,1,300);
            assert(rxread(1).receive_count==2 && rxread(1).active && !proofread(1).retained);
            assert(distributed_refmem_copy_vdc_feedback_rx(1,&decoded));
            assert(decoded.sample.schema_version==(auto_mode?4u:2u) && !memcmp(decoded.record,next,64));
            return 0;
        }
        if(!strcmp(test,"master_duplicate")) {
            receipt_group(ack,1,200);assert(rxread(1).receive_count==1 && rxread(1).duplicate_count==1);
        }
        if(!strcmp(test,"master_sources")) {
            for(unsigned source=2;source<=3;source++) {
                const distributed_refmem_vdc_feedback_rx_snapshot_t proof=proofread(source);
                assert(proof.retained && refmem_sync_vdc_receipt_ack_encode(proof.record,4,ack));
                receipt_group(ack,source,100);assert(rxread(source).receive_count==1);
            }
        }
        if(!strcmp(test,"master_stop_history")) {
            const distributed_refmem_vdc_feedback_rx_snapshot_t proof=proofread(1),receipt=rxread(1);
            ring.enabled=0;refresh();assert(!proofread(1).active && proofread(1).retained);
            assert(!memcmp(proofread(1).record,proof.record,64) && !memcmp(rxread(1).record,receipt.record,64));
            assert(!rxread(1).active && rxread(1).receive_count==1);
        }
    }
    puts("receipt scenario passed");return 0;
}
'''
