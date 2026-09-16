"""Real RefMem/FIFO boundary transport, controlled manager offer/owner snapshots."""
import subprocess

import pytest

from test_vdc_command_owner import compile_executable
from test_vdc_feedback_transport import FEEDBACK_SOURCES, make_feedback_harness


@pytest.fixture(scope='module')
def boundary_transport_executable(tmp_path_factory):
    # Share the legacy production integration harness, retaining its original
    # main as another callable function. Only the offer and FIFO-failure seams
    # are controlled; all codec/RefMem preparation and receiver code is real.
    harness = make_feedback_harness(MANAGER).replace('int main(int argc,char **argv)',
                                                    'int legacy_main(int argc,char **argv)')
    harness = harness.replace('static tdma_service_service_t owner;', FIFO_SEAM +
                              '\nstatic tdma_service_service_t owner;')
    harness = harness.replace('tdma_flight_fifo_core0_publish_tx(&service->flight_fifo,',
                              'boundary_test_fifo_publish(&service->flight_fifo,')
    return compile_executable(tmp_path_factory.mktemp('boundary-transport'), 'boundary_transport',
                              harness + TESTS, FEEDBACK_SOURCES)


@pytest.mark.parametrize('scenario', [
    'tx_complete', 'tx_failure', 'tx_busy', 'tx_cancel', 'tx_changed_offer', 'tx_stop',
    'tx_gap_fifo_failure', 'tx_gap_busy', 'tx_gap_cancel', 'tx_gap_changed_bytes',
    'tx_partial_changed_bytes', 'tx_gap_stop_same_id', 'tx_gap_new_offer',
    'tx_partial_withdraw_same_id',
    'rx_complete', 'rx_duplicate', 'rx_wrong_source', 'rx_wrong_target', 'rx_wrong_session',
    'rx_wrong_schedule', 'rx_wrong_epoch', 'rx_wrong_run', 'rx_bad_crc', 'rx_bad_schema',
    'rx_expiry', 'rx_mixed_class', 'rx_old_admission', 'rx_role_aba', 'rx_copy', 'classes',
    'delivery_drop_recovered', 'delivery_reorder_recovered',
])
def test_boundary_transport(boundary_transport_executable, scenario):
    result = subprocess.run([str(boundary_transport_executable), scenario], text=True,
                            capture_output=True, timeout=10)
    (boundary_transport_executable.parent / (scenario + '.log')).write_text(
        result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr


FIFO_SEAM = r'''
static bool publication_blocked;
static bool boundary_test_fifo_publish(tdma_flight_fifo_t *fifo,const uint8_t *data,
    size_t size,uint32_t generation,uint32_t sequence,uint32_t mask)
{
    return !publication_blocked && tdma_flight_fifo_core0_publish_tx(fifo,data,size,generation,sequence,mask);
}
'''

MANAGER = r'''
static refmem_sync_vdc_boundary_command_t offer;
static uint32_t offered_id=41, done_id, done_count, offer_reads;
static int offer_result;
int vdc_dpll_manager_copy_boundary_command_offer(uint32_t config,uint32_t role,
    uint32_t source,uint32_t schedule,uint32_t sess,
    refmem_sync_vdc_boundary_command_t *command,uint32_t *offer_id)
{
    assert(config==ring.config_seq && role==profile.control_profile.generation &&
        source==ring.local_slot_id && schedule==ring.schedule_crc32 && sess==session);
    ++offer_reads;
    if(offer_result==1) {*command=offer;*offer_id=offered_id;}
    return offer_result;
}
void vdc_dpll_manager_boundary_command_tx_done_core0(uint32_t offer_id)
{ assert(offer_id);done_id=offer_id;++done_count; }
'''

TESTS = r'''
_Static_assert(sizeof(distributed_refmem_vdc_feedback_rx_snapshot_t)==192u,"RX union no growth");
_Static_assert(sizeof(distributed_refmem_vdc_boundary_view_t)==96u,"bounded Core1 view");
_Static_assert(sizeof(distributed_refmem_feedback_source_t)==276u,"source BSS no growth");
_Static_assert(REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS==3u,"three complete groups including first");

static refmem_sync_vdc_boundary_command_t command(void)
{
    return (refmem_sync_vdc_boundary_command_t){.target_arm_epoch=77u,
        .basis_source_output_ns_lo=900000000u,.control_session=123u,.command_seq=6u,
        .schedule_crc32=0xabcu,.target_clock_epoch_id=3u,.target_clock_run_id=4u,
        .target_observer_epoch=8u,.basis_measurement_sequence=123u,
        .expected_target_model_token=99u,.expected_applied_command_seq=5u,
        .signed_delta_rate_ppb=-17,.schema_version=3u,.source_slot=0u,.target_slot=2u,.flags=1u};
}

static void refresh(void)
{
    distributed_refmem_vdc_flight_rx_refresh(&owner);
    distributed_refmem_feedback_refresh(&owner);
}

static void boundary_setup(uint32_t local)
{
    session=123u;setup(local);refresh();
    offer=command();offer_result=1;
}

static void assert_no_tx(void)
{
    tdma_flight_fifo_snapshot_t fifo;assert(tdma_service_get_flight_fifo_snapshot(&owner,&fifo));
    assert(fifo.tx_ready_count==0u);
}

static void enqueue_boundary(const uint8_t *wire,uint32_t source,uint32_t seq,unsigned index,uint8_t cls)
{
    uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE]={0},*m=image+source*32;
    distributed_refmem_put_le16(m,TDMA_FLIGHT_MAILBOX_MAGIC);m[2]=TDMA_FLIGHT_MAILBOX_VERSION;
    m[3]=cls;m[4]=source;m[5]=0x3f;distributed_refmem_put_le16(m+6,(uint16_t)seq);
    m[8]=(uint8_t)index;m[9]=16;memcpy(m+10,wire+index*4u,4);
    distributed_refmem_put_le32(m+14,seq);distributed_refmem_put_le16(m+18,1);
    distributed_refmem_put_le32(m+20,123+seq);crc(m);
    assert(tdma_flight_fifo_core1_publish_rx(&owner.flight_fifo,image,
        tdma_flight_payload_size(ring.node_count),seq,seq,1u<<source,0,0));
}

static void command_group(const uint8_t *wire,uint32_t source,uint32_t seq)
{
    for(unsigned i=0;i<16;i++) {
        enqueue_boundary(wire,source,seq+i,i,TDMA_PROCESS_IMAGE_VDC_BOUNDARY_COMMAND_MESSAGE_CLASS);
        receive();
    }
}

static void publish_command_group(uint8_t sent[64])
{
    for(unsigned i=0;i<16;i++) {
        uint8_t m[32];publish();consume(m);
        assert(m[3]==0x13 && m[8]==i && m[9]==16);
        memcpy(sent+i*4u,m+10,4u);
    }
}

static void tx_case(const char *test)
{
    boundary_setup(0);uint8_t wire[64],m[32];
    assert(refmem_sync_vdc_boundary_command_encode(&offer,6,wire));
    if(!strcmp(test,"tx_complete")) {
        uint8_t sent[64];
        for(unsigned g=0;g<REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS;g++) {
            if(g) {publish();consume(m);assert(m[3]==0x10 && !done_count);}
            for(unsigned i=0;i<16;i++) {
                const uint32_t checked_before=offer_reads;
                publish();consume(m);
                assert(offer_reads>checked_before); /* Every fragment revalidates the offer. */
                assert(m[3]==0x13 && m[5]==0x3f && m[8]==i && m[9]==16);
                assert(distributed_refmem_get_le16(m+18)==TDMA_PROCESS_IMAGE_REFMEM_BASELINE_FIELD_ID);
                assert(distributed_refmem_get_le32(m+20)==s_service_count);
                memcpy(sent+i*4u,m+10,4);
                assert(done_count==(g+1==REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS && i==15));
            }
            assert(!memcmp(sent,wire,64));
        }
        assert(done_id==41 && !memcmp(sent,wire,64));
        distributed_refmem_vdc_feedback_tx_snapshot_t tx=txread();
        assert(tx.groups_published==REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS && !tx.active && !memcmp(tx.history[0],wire,64));
        assert(tx.fragments_published==16u*REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS && !memcmp(tx.history[1],wire,64));
        publish();consume(m);assert(m[3]==0x10);
        publish();consume(m);assert(m[3]==0x10 && done_count==1); /* no same-ID requeue */
        offered_id=42;offer.command_seq++;
        publish();consume(m);assert(m[3]==0x13 && m[8]==0);
        assert(done_count==1);
    } else if(!strcmp(test,"tx_failure")) {
        publication_blocked=true;const uint32_t seq=s_tdma_flight_sync.next_seq32;
        publish();assert_no_tx();assert(txread().active && txread().fragment_index==0);
        assert(s_tdma_flight_sync.next_seq32==seq && !done_count);
        publication_blocked=false;publish();consume(m);assert(m[3]==0x13 && m[8]==0);
        for(unsigned i=1;i<15;i++){publish();consume(m);assert(m[8]==i);}
        publication_blocked=true;publish();assert_no_tx();assert(txread().fragment_index==15 && !done_count);
        publication_blocked=false;publish();consume(m);assert(m[8]==15 && !done_count);
        for(unsigned g=1;g<REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS;g++) {
            publish();consume(m);assert(m[3]==0x10);
            uint8_t sent[64];publish_command_group(sent);assert(!memcmp(sent,wire,64));
        }
        assert(done_count==1 && done_id==41);
    } else if(!strcmp(test,"tx_busy")) {
        publish();consume(m);const uint32_t seq=s_tdma_flight_sync.next_seq32;
        offer_result=-1;publish();assert_no_tx();assert(txread().fragment_index==1);
        assert(s_tdma_flight_sync.next_seq32==seq && !done_count);
        offer_result=1;publish();consume(m);assert(m[3]==0x13 && m[8]==1);
    } else if(!strcmp(test,"tx_cancel") || !strcmp(test,"tx_changed_offer")) {
        publish();consume(m);assert(m[3]==0x13);
        if(!strcmp(test,"tx_cancel")) offer_result=0;
        else {offered_id=42;offer.command_seq++;}
        publication_blocked=true;publish();assert_no_tx();
        assert(!txread().active && txread().cancel_count==1 && s_feedback_ordinary_due && !done_count);
        offer_result=1;offered_id=43;offer.command_seq=8;
        publish();assert_no_tx();assert(!txread().active && s_feedback_ordinary_due);
        publication_blocked=false;publish();consume(m);assert(m[3]==0x10 && !done_count);
        publish();consume(m);assert(m[3]==0x13 && m[8]==0 && s_boundary_offer_id==43);
    } else if(!strncmp(test,"tx_gap_",7) || !strncmp(test,"tx_partial_",11)) {
        const bool partial=!strncmp(test,"tx_partial_",11);
        uint8_t sent[64];
        if(partial) {publish();consume(m);assert(m[3]==0x13 && m[8]==0);}
        else {publish_command_group(sent);assert(!memcmp(sent,wire,64));}
        assert(!done_count);
        if(!strcmp(test,"tx_gap_fifo_failure")) {
            const uint32_t seq=s_tdma_flight_sync.next_seq32;
            publication_blocked=true;publish();assert_no_tx();
            assert(s_feedback_ordinary_due && s_tdma_flight_sync.next_seq32==seq && !done_count);
            publication_blocked=false;publish();consume(m);assert(m[3]==0x10);
            publish_command_group(sent);assert(!memcmp(sent,wire,64));
            assert(txread().groups_published==2 && !done_count);
        } else if(!strcmp(test,"tx_gap_busy")) {
            const uint32_t seq=s_tdma_flight_sync.next_seq32;
            offer_result=-1;publish();assert_no_tx();
            assert(s_tdma_flight_sync.next_seq32==seq && s_feedback_ordinary_due && !done_count);
            offer_result=1;publish();consume(m);assert(m[3]==0x10);
            publish();consume(m);assert(m[3]==0x13 && m[8]==0);
        } else if(!strcmp(test,"tx_gap_new_offer")) {
            offered_id++;offer.command_seq++;offer.expected_applied_command_seq++;
            publish();consume(m);assert(m[3]==0x10 && !done_count);
            publish();consume(m);assert(m[3]==0x13 && m[8]==0 && s_boundary_offer_id==42);
        } else {
            if(!strcmp(test,"tx_gap_cancel") || !strcmp(test,"tx_partial_withdraw_same_id")) offer_result=0;
            else if(!strcmp(test,"tx_gap_stop_same_id")) {ring.enabled=0;refresh();ring.enabled=1;refresh();}
            else offer.signed_delta_rate_ppb++;
            publish();consume(m);assert(m[3]==0x10 && !done_count);
            offer_result=1;offer=command();
            for(unsigned i=0;i<20;i++) {publish();consume(m);assert(m[3]==0x10 && !done_count);}
            assert(txread().groups_published==(partial?0u:1u));
        }
    } else {
        assert(!strcmp(test,"tx_stop"));publish();consume(m);
        ring.enabled=0;refresh();assert(!txread().active && txread().cancel_count==1);
        ring.enabled=1;refresh();offered_id=42;offer.command_seq++;
        publish();consume(m);assert(m[3]==0x10);
        publish();consume(m);assert(m[3]==0x13 && m[8]==0 && !done_count);
    }
}

static void delivery_case(bool reorder)
{
    /* Capture actual production TX mailboxes, then feed those exact bytes
     * through the production follower FIFO/decoder with one damaged group. */
    boundary_setup(0);
    uint8_t groups[3][16][32],separator[2][32],wire[64];
    assert(refmem_sync_vdc_boundary_command_encode(&offer,6,wire));
    for(unsigned g=0;g<3;g++) {
        if(g) {publish();consume(separator[g-1]);assert(separator[g-1][3]==0x10);}
        for(unsigned i=0;i<16;i++) {publish();consume(groups[g][i]);assert(groups[g][i][8]==i);}
    }
    assert(done_count==1 && done_id==41);
    boundary_setup(2);
    for(unsigned g=0;g<3;g++) {
        if(g) {
            uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE]={0};memcpy(image,separator[g-1],32);
            uint32_t seq=distributed_refmem_get_le32(image+14);
            assert(tdma_flight_fifo_core1_publish_rx(&owner.flight_fifo,image,
                tdma_flight_payload_size(ring.node_count),seq,seq,1,0,0));receive();
        }
        for(unsigned i=0;i<16;i++) {
            if(!g && !reorder && i==7)continue;
            unsigned index=!g && reorder && (i==7 || i==8)?15u-i:i;
            uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE]={0};memcpy(image,groups[g][index],32);
            uint32_t seq=distributed_refmem_get_le32(image+14);
            assert(tdma_flight_fifo_core1_publish_rx(&owner.flight_fifo,image,
                tdma_flight_payload_size(ring.node_count),seq,seq,1,0,0));receive();
        }
        distributed_refmem_vdc_feedback_rx_snapshot_t rx=rxread(0);
        if(!g)assert(!rx.retained && !rx.receive_count && rx.reject_count);
        else {
            assert(rx.retained && rx.active && rx.receive_count==1 && !memcmp(rx.record,wire,64));
            assert(rx.duplicate_count==(g==2));
        }
    }
}

static void rx_case(const char *test)
{
    boundary_setup(2);uint8_t wire[64];refmem_sync_vdc_boundary_command_t cmd=command();
    if(!strcmp(test,"rx_wrong_source")) cmd.source_slot=1;
    if(!strcmp(test,"rx_wrong_target")) cmd.target_slot=3;
    if(!strcmp(test,"rx_wrong_session")) cmd.control_session++;
    if(!strcmp(test,"rx_wrong_schedule")) cmd.schedule_crc32++;
    if(!strcmp(test,"rx_wrong_epoch")) cmd.target_clock_epoch_id++;
    if(!strcmp(test,"rx_wrong_run")) cmd.target_clock_run_id++;
    assert(refmem_sync_vdc_boundary_command_encode(&cmd,6,wire));
    if(!strcmp(test,"rx_bad_crc")) wire[51]^=1;
    if(!strcmp(test,"rx_bad_schema")) wire[0]=2;
    if(!strcmp(test,"rx_complete") || !strcmp(test,"rx_duplicate") || !strcmp(test,"rx_copy")) {
        command_group(wire,0,100);
        distributed_refmem_vdc_feedback_rx_snapshot_t before=rxread(0);
        assert(before.active && before.retained && before.receive_count==1 && !memcmp(before.record,wire,64));
        assert(!memcmp(&before.command,&cmd,sizeof(cmd)));
        assert(s_tdma_flight_sync.context.mirror[0].value_u32==238u);
        assert(s_tdma_flight_sync.last_vdc_phase_offset_ns==0 && s_tdma_flight_sync.last_vdc_rate_adjust_ppb==0);
        assert(before.role_generation==9 && before.clock_epoch_id==3 && before.clock_run_id==4);
        if(!strcmp(test,"rx_duplicate")) {
            command_group(wire,0,200);
            const distributed_refmem_vdc_feedback_rx_snapshot_t after=rxread(0);
            assert(after.receive_count==1 && after.duplicate_count==1);
            assert(after.first_rx_ms==before.first_rx_ms && after.last_rx_ms==before.last_rx_ms);
            cmd.signed_delta_rate_ppb++;assert(refmem_sync_vdc_boundary_command_encode(&cmd,6,wire));
            command_group(wire,0,300);assert(rxread(0).reject_count==1);
            assert(!memcmp(rxread(0).record,before.record,64));
        } else if(!strcmp(test,"rx_copy")) {
            distributed_refmem_vdc_boundary_view_t view,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));
            const unsigned reads=full_ring_reads;
            assert(distributed_refmem_copy_vdc_boundary_command(0,&view));
            assert(view.active && !memcmp(&view.command,&cmd,sizeof(cmd)) && view.role_generation==9);
            assert(full_ring_reads==reads);
            view=sentinel;s_feedback_rx[0].guard|=1u;
            assert(!distributed_refmem_copy_vdc_boundary_command(0,&view));assert(!memcmp(&view,&sentinel,sizeof(view)));
            assert(!distributed_refmem_copy_vdc_boundary_command(6,&view));
            assert(!distributed_refmem_copy_vdc_boundary_command(0,NULL));
            s_feedback_rx[0].guard++;
            ring.enabled=0;refresh();assert(distributed_refmem_copy_vdc_boundary_command(0,&view));assert(!view.active);
            ring.enabled=1;refresh();assert(distributed_refmem_copy_vdc_boundary_command(0,&view));assert(!view.active);
        }
    } else if(!strcmp(test,"rx_expiry")) {
        enqueue_boundary(wire,0,100,0,0x13);receive();now_ms+=1000;refresh();
        assert(rxread(0).timeout_count==1 && !rxread(0).retained);
        for(unsigned i=1;i<16;i++){enqueue_boundary(wire,0,100+i,i,0x13);receive();}
        assert(!rxread(0).retained);
    } else if(!strcmp(test,"rx_mixed_class")) {
        enqueue_boundary(wire,0,100,0,0x13);receive();
        enqueue_boundary(wire,0,101,1,0x12);receive();
        assert(!(s_feedback_rx[0].assembly.state&REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE));
        for(unsigned i=2;i<16;i++){enqueue_boundary(wire,0,100+i,i,0x13);receive();}
        assert(!rxread(0).retained);
    } else if(!strcmp(test,"rx_old_admission") || !strcmp(test,"rx_role_aba")) {
        enqueue_boundary(wire,0,100,0,0x13);
        const uint32_t admission=s_vdc_flight_rx_binding.rx_admission_epoch;
        if(!strcmp(test,"rx_role_aba")) {
            profile.control_profile.mode=VDC_DPLL_CONTROL_MODE_MASTER;
            profile.control_profile.generation++;refresh();
            profile.control_profile.mode=VDC_DPLL_CONTROL_MODE_FOLLOWER;
            profile.control_profile.generation++;refresh();
        } else {
            ring.enabled=0;refresh();ring.enabled=1;profile.clock_run_id++;refresh();
        }
        assert(s_vdc_flight_rx_binding.rx_admission_epoch!=admission);
        receive();
        for(unsigned i=1;i<16;i++){enqueue_boundary(wire,0,100+i,i,0x13);receive();}
        assert(!rxread(0).retained);
    } else {
        command_group(wire,cmd.source_slot,100);
        assert(!rxread(0).retained && !rxread(1).retained);
    }
}

int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strncmp(argv[1],"tx_",3)) tx_case(argv[1]);
    else if(!strncmp(argv[1],"rx_",3)) rx_case(argv[1]);
    else if(!strncmp(argv[1],"delivery_",9)) delivery_case(!strcmp(argv[1],"delivery_reorder_recovered"));
    else {
        assert(!strcmp(argv[1],"classes"));boundary_setup(0);
        assert(tdma_process_image_transport_class_valid(0x10));
        assert(!tdma_process_image_transport_class_valid(0x11));
        assert(tdma_process_image_transport_class_valid(0x12));
        assert(tdma_process_image_transport_class_valid(0x13));
        assert(!tdma_process_image_transport_class_valid(0x14));
        uint8_t m[32],frame[128];size_t size=0;
        publish();consume(m);assert(m[3]==0x13);
        assert(distributed_refmem_tdma_flight_expand_compact_delta(m,32,frame,sizeof(frame),&size));
        m[3]=0x11;crc(m);
        assert(!distributed_refmem_tdma_flight_expand_compact_delta(m,32,frame,sizeof(frame),&size));
    }
    puts("boundary transport passed");return 0;
}
'''
