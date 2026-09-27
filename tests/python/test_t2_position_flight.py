"""Exercise T2 fragments on the production RefMem flight publisher."""
import subprocess

from test_vdc_command_owner import compile_executable
from test_vdc_feedback_transport import FEEDBACK_SOURCES, make_feedback_harness


CASES = r'''
int main(void)
{
    setup(0u);
    s_tdma_flight_sync.publish_interval_ms=1u;
    trigger_position_descriptor_t descriptor={
        .schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,.run_id=1u,.sync_epoch=2u,
        .position_id=3u,.count_epoch=4u,.prediction_version=5u,
        .target_ms_index=20u,.grid_generation=6u,.deployment_generation=7u,
        .deployment_crc=8u,.valid_from_sync=9u,.expiry_sync=30u};
    assert(trigger_position_descriptor_encode(&descriptor,
        s_tdma_flight_sync.position_tx_wire));
    s_tdma_flight_sync.position_tx_active=1u;
    s_tdma_flight_sync.position_tx_target_mask=0x0eu;
    uint8_t wire[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE]={0};
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    for(uint32_t index=0u;index<TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT;++index){
        now_ms++;
        distributed_refmem_tdma_flight_sync_publish(&owner,&ring);
        consume(mailbox);
        assert(mailbox[3]==TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS);
        assert(tdma_process_image_flight_class_valid(mailbox[3]));
        assert(!tdma_process_image_transport_class_valid(mailbox[3]));
        assert(mailbox[5]==0x0eu);
        assert(distributed_refmem_get_i16(mailbox+TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET)==20);
        assert(distributed_refmem_get_i16(mailbox+TDMA_PROCESS_IMAGE_VDC_RATE_OFFSET)==-20);
        assert(mailbox[TDMA_PROCESS_IMAGE_T2_FRAGMENT_INDEX_OFFSET]==index);
        assert(mailbox[TDMA_PROCESS_IMAGE_T2_FRAGMENT_COUNT_OFFSET]==
            TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT);
        assert(tdma_process_image_crc16_ccitt(mailbox,TDMA_PROCESS_IMAGE_CRC_OFFSET)==
            distributed_refmem_get_le16(mailbox+TDMA_PROCESS_IMAGE_CRC_OFFSET));
        memcpy(wire+index*TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_SIZE,
            mailbox+TDMA_PROCESS_IMAGE_T2_FRAGMENT_DATA_OFFSET,
            TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_SIZE);
        assert(s_tdma_flight_sync.last_publish_ms==now_ms);
    }
    trigger_position_descriptor_t received;
    assert(trigger_position_descriptor_decode(wire,&received));
    assert(received.position_id==descriptor.position_id);
    assert(!s_tdma_flight_sync.position_tx_active);
    assert(s_tdma_flight_sync.position_tx_publish_count==
        TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT);
    now_ms++;
    distributed_refmem_tdma_flight_sync_publish(&owner,&ring);
    consume(mailbox);
    assert(mailbox[3]==TDMA_PROCESS_IMAGE_MESSAGE_CLASS);
    return 0;
}
'''


def test_position_fragments_preserve_vdc_cadence(tmp_path):
    harness = make_feedback_harness().replace(
        "int main(int argc,char **argv)", "int feedback_existing_main(int argc,char **argv)")
    executable = compile_executable(tmp_path, "t2-position-flight", harness + CASES,
                                    FEEDBACK_SOURCES)
    result = subprocess.run([str(executable)], capture_output=True, text=True,
                            timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


RESET_CASES = r"""
static void position_frame(uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE],uint32_t seq,
    uint32_t index,uint32_t count,const uint8_t *body)
{
    memset(image,0,TDMA_FLIGHT_SHORT_PAYLOAD_SIZE);uint8_t *m=image+32;
    distributed_refmem_put_le16(m,TDMA_FLIGHT_MAILBOX_MAGIC);
    m[2]=TDMA_FLIGHT_MAILBOX_VERSION;m[3]=TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS;
    m[4]=1;m[5]=1;distributed_refmem_put_le16(m+6,(uint16_t)seq);
    m[8]=0x45;m[9]=0x12;m[10]=0x89;m[11]=0x67;m[12]=4;m[13]=3;
    m[14]=(uint8_t)index;m[15]=(uint8_t)count;memcpy(m+16,body,4);crc(m);
}
static void queue_position(uint32_t seq,uint32_t index,uint32_t count,const uint8_t *body)
{
    uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];position_frame(image,seq,index,count,body);
    assert(tdma_flight_fifo_core1_publish_rx(&owner.flight_fifo,image,
        tdma_flight_payload_size(6),seq,seq,2,123,0));
}
int main(void)
{
    setup(0);const uint32_t admission=owner.flight_fifo.rx_admission_epoch;
    uint8_t freeze[4]={9,0,0,0},image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];
    trigger_position_descriptor_t old={.schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
        .stage=TRIGGER_POSITION_STAGE_FREEZE,.position_id=1,.prediction_version=9};
    assert(trigger_position_mailbox_stage(&s_position_rx_mailboxes[1],&old,1));
    assert(trigger_position_mailbox_stage(&s_position_tx_mailbox,&old,1));
    s_tdma_flight_sync.position_rx_fragments[1].active=1;
    s_tdma_flight_sync.position_rx_fragments[1].next_fragment=12;
    s_tdma_flight_sync.position_rx_fragments[1].accepted_fragment_count=55;
    s_tdma_flight_sync.position_tx_active=1;
    s_position_cancel_requested=1;
    position_frame(image,8,0xf1,1,freeze);
    memcpy(s_tdma_flight_sync.tx_image,image,sizeof(image));
    for(unsigned i=0;i<4;i++)queue_position(10+i,0xf1,1,freeze);
    assert(!distributed_refmem_reset_position_stopped()); /* RUN is never resettable */
    ring.enabled=ring.adapter_started=0;ring.node_count=0;
    assert(tdma_service_publish_flight_tx(&owner,image,sizeof(image),1,1,2));
    assert(!distributed_refmem_reset_position_stopped()); /* queued shared TX needs owner retirement */
    tdma_flight_tx_view_t tx;assert(tdma_flight_fifo_core1_acquire_tx(&owner.flight_fifo,&tx));
    assert(!distributed_refmem_reset_position_stopped()); /* active old image also blocks */
    tdma_flight_fifo_core1_release_tx(&owner.flight_fifo);
    assert(tdma_flight_fifo_reset_stopped(&owner.flight_fifo));
    /* Requeue old RX to prove reset's T2 filtering, separately from shared TX retirement. */
    for(unsigned i=0;i<4;i++)queue_position(20+i,0xf1,1,freeze);
    const unsigned before=ordinary_accepts;
    assert(distributed_refmem_reset_position_stopped());
    assert(ordinary_accepts==before+4); /* co-carried VDC is never skipped */
    assert(!s_position_cancel_requested&&!s_tdma_flight_sync.position_tx_active);
    assert(!trigger_position_mailbox_pending(&s_position_tx_mailbox));
    assert(!trigger_position_mailbox_pending(&s_position_rx_mailboxes[1]));
    assert(!s_tdma_flight_sync.position_rx_fragments[1].active);
    assert(s_tdma_flight_sync.position_rx_fragments[1].accepted_fragment_count==55);
    assert(owner.flight_fifo.rx_admission_epoch==admission); /* no VDC admission change */
    assert(!memcmp(s_tdma_flight_sync.tx_image+32+8,image+32+8,6));
    assert(!s_tdma_flight_sync.tx_image[32+15]);
    assert(tdma_process_image_crc16_ccitt(s_tdma_flight_sync.tx_image+32,30)==
        distributed_refmem_get_le16(s_tdma_flight_sync.tx_image+32+30));
    tdma_flight_fifo_snapshot_t f;assert(tdma_service_get_flight_fifo_snapshot(&owner,&f));
    assert(!f.rx_queued_count&&!f.rx_parse_count);
    ring.enabled=ring.adapter_started=1;ring.node_count=6;
    trigger_position_descriptor_t preview={.schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
        .stage=TRIGGER_POSITION_STAGE_PREVIEW,.run_id=2,.sync_epoch=2,.position_id=1,
        .count_epoch=5,.prediction_version=1,.target_ms_index=30,.grid_generation=6,
        .deployment_generation=7,.deployment_crc=8,.valid_from_sync=9,.expiry_sync=40};
    uint8_t wire[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE];assert(trigger_position_descriptor_encode(&preview,wire));
    for(uint32_t i=0;i<TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT;i++){
        queue_position(100+i,i,TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT,wire+4*i);
        receive();
    }
    trigger_position_descriptor_t received;
    assert(distributed_refmem_get_position_descriptor(1,&received));
    assert(received.stage==TRIGGER_POSITION_STAGE_PREVIEW&&received.run_id==2&&received.count_epoch==5);
    assert(!distributed_refmem_get_position_descriptor(1,&received));
    uint8_t current_freeze[4]={2,0,0,0};queue_position(150,0xf1,1,current_freeze);receive();
    assert(distributed_refmem_get_position_descriptor(1,&received));
    assert(received.stage==TRIGGER_POSITION_STAGE_FREEZE&&received.prediction_version==2);
    return 0;
}
"""


def test_stopped_reset_retires_queued_freeze_without_dropping_vdc(tmp_path):
    from test_vdc_command_ingress import ingress_definition
    from test_vdc_command_owner import ROOT
    source = (ROOT / 'components/distributed_refmem/src/distributed_refmem.c').read_text(encoding='utf-8')
    harness = make_feedback_harness().replace(
        'int main(int argc,char **argv)', 'int feedback_existing_main(int argc,char **argv)')
    harness = harness.replace('#include "distributed_refmem.h"', '#include "distributed_refmem.h"\n#include "trigger_position_mailbox.h"')
    harness = harness.replace('static distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;',
        'static distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;\n'
        'static trigger_position_mailbox_t s_position_tx_mailbox,s_position_rx_mailboxes[DISTRIBUTED_REFMEM_NODE_COUNT];\n'
        'static volatile uint32_t s_position_cancel_requested;')
    harness = harness.replace('static void distributed_refmem_tdma_parse_position_fragment(uint32_t slot,const uint8_t *m)\n{ (void)slot; (void)m; }',
        'static void distributed_refmem_tdma_parse_position_fragment(uint32_t slot,const uint8_t *m);')
    harness = harness.replace('static void distributed_refmem_vdc_flight_rx_accept(uint32_t slot,const uint8_t *m,const tdma_flight_rx_view_t *v)\n{ (void)slot; (void)m; (void)v; }',
        'static unsigned ordinary_accepts;\nstatic void distributed_refmem_vdc_flight_rx_accept(uint32_t slot,const uint8_t *m,const tdma_flight_rx_view_t *v)\n{ (void)slot; (void)m; (void)v; ++ordinary_accepts; }')
    for name in ('distributed_refmem_tdma_parse_position_fragment',
                 'distributed_refmem_get_position_descriptor', 'distributed_refmem_reset_position_stopped'):
        harness += '\n' + ingress_definition(source, name)
    exe = compile_executable(tmp_path, 'position-stopped-reset', harness + RESET_CASES, FEEDBACK_SOURCES)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
