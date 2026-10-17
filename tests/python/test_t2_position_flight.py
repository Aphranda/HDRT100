"""Exercise the single-frame T2 position record on the production RefMem publisher."""
import subprocess

from test_vdc_command_owner import compile_executable
from test_vdc_feedback_transport import FEEDBACK_SOURCES, make_feedback_harness


CASES = r'''
int main(void)
{
    setup(0u);
    /* T2 fills IDLE cycles only: a due ordinary/VDC frame keeps its cadence, so the
     * interval is wide enough that the cycle below is idle. */
    s_tdma_flight_sync.publish_interval_ms=10u;
    s_tdma_flight_sync.last_publish_ms=now_ms;   /* start from an idle cycle */
    const trigger_position_slice_t slice={.target_ms_index=20u,.target_fine_slice_index=7u,
        .prediction_version=5u,.position_id=3u,.stage=TRIGGER_POSITION_SLICE_PREVIEW};
    assert(trigger_position_slice_encode(&slice,s_tdma_flight_sync.position_tx_wire));
    s_tdma_flight_sync.position_tx_fragment_index=UINT32_MAX;
    s_tdma_flight_sync.position_tx_active=1u;
    s_tdma_flight_sync.position_tx_target_mask=0x0eu;
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    const uint32_t idle_mark=s_tdma_flight_sync.last_publish_ms;
    now_ms+=2;
    distributed_refmem_tdma_flight_sync_publish(&owner,&ring);
    consume(mailbox);
    assert(mailbox[3]==TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS);
    assert(tdma_process_image_flight_class_valid(mailbox[3]));
    assert(!tdma_process_image_transport_class_valid(mailbox[3]));
    assert(mailbox[5]==0x0eu);
    /* The slice now STARTS where the VDC observation used to be: 16 B = VDC area (6) +
     * RefMem area (10), with the current position first and the optional next-position
     * cache in bytes 10..14 (T2-PROVISIONAL-001). */
    assert(trigger_position_slice_tagged(mailbox[TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET]));
    assert(tdma_process_image_crc16_ccitt(mailbox,TDMA_PROCESS_IMAGE_CRC_OFFSET)==
        distributed_refmem_get_le16(mailbox+TDMA_PROCESS_IMAGE_CRC_OFFSET));
    /* An idle-cycle T2 frame must NOT move the VDC cadence: the next ordinary frame is
     * still due at its original deadline. */
    assert(s_tdma_flight_sync.last_publish_ms==idle_mark);
    trigger_position_slice_t received;
    assert(trigger_position_slice_decode(mailbox+TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET,&received));
    assert(received.position_id==3u && received.target_ms_index==20u &&
        received.target_fine_slice_index==7u && received.prediction_version==5u);
    /* One frame carries the whole record: the transport is idle again. */
    assert(!s_tdma_flight_sync.position_tx_active);
    assert(s_tdma_flight_sync.position_tx_publish_count==1u);
    now_ms+=10;   /* the ordinary frame is due again: it owns the cycle, T2 yields */
    distributed_refmem_tdma_flight_sync_publish(&owner,&ring);
    consume(mailbox);
    assert(mailbox[3]==TDMA_PROCESS_IMAGE_MESSAGE_CLASS);
    return 0;
}
'''


def test_position_record_is_one_frame_and_preserves_vdc_cadence(tmp_path):
    harness = make_feedback_harness().replace(
        "int main(int argc,char **argv)", "int feedback_existing_main(int argc,char **argv)")
    executable = compile_executable(tmp_path, "t2-position-flight", harness + CASES,
                                    FEEDBACK_SOURCES)
    result = subprocess.run([str(executable)], capture_output=True, text=True,
                            timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


RESET_CASES = r"""
static void position_frame(uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE],uint32_t seq,
    const trigger_position_slice_t *slice)
{
    memset(image,0,TDMA_FLIGHT_SHORT_PAYLOAD_SIZE);uint8_t *m=image+32;
    distributed_refmem_put_le16(m,TDMA_FLIGHT_MAILBOX_MAGIC);
    m[2]=TDMA_FLIGHT_MAILBOX_VERSION;m[3]=TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS;
    m[4]=1;m[5]=1;distributed_refmem_put_le16(m+6,(uint16_t)seq);
    assert(trigger_position_slice_encode(slice,m+TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET));crc(m);
}
static void queue_position(uint32_t seq,const trigger_position_slice_t *slice)
{
    uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];position_frame(image,seq,slice);
    assert(tdma_flight_fifo_core1_publish_rx(&owner.flight_fifo,image,
        tdma_flight_payload_size(6),seq,seq,2,123,0));
}
int main(void)
{
    setup(0);const uint32_t admission=owner.flight_fifo.rx_admission_epoch;
    const trigger_position_slice_t freeze={.position_id=1,.prediction_version=9,
        .stage=TRIGGER_POSITION_SLICE_FREEZE};
    uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];
    trigger_position_descriptor_t old={.schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
        .stage=TRIGGER_POSITION_STAGE_FREEZE,.position_id=1,.prediction_version=9};
    assert(trigger_position_mailbox_stage(&s_position_rx_mailboxes[1],&old,1));
    assert(distributed_refmem_stage_position_descriptor(&old,1));
    s_tdma_flight_sync.position_rx_fragments[1].active=1;
    s_tdma_flight_sync.position_rx_fragments[1].next_fragment=12;
    s_tdma_flight_sync.position_rx_fragments[1].accepted_fragment_count=55;
    s_tdma_flight_sync.position_tx_active=1;
    s_position_cancel_requested=1;
    position_frame(image,8,&freeze);
    memcpy(s_tdma_flight_sync.tx_image,image,sizeof(image));
    for(unsigned i=0;i<4;i++)queue_position(10+i,&freeze);
    assert(!distributed_refmem_reset_position_stopped()); /* RUN is never resettable */
    ring.enabled=ring.adapter_started=0;ring.node_count=0;
    assert(tdma_service_publish_flight_tx(&owner,image,sizeof(image),1,1,2));
    assert(!distributed_refmem_reset_position_stopped()); /* queued shared TX needs owner retirement */
    tdma_flight_tx_view_t tx;assert(tdma_flight_fifo_core1_acquire_tx(&owner.flight_fifo,&tx));
    assert(!distributed_refmem_reset_position_stopped()); /* active old image also blocks */
    tdma_flight_fifo_core1_release_tx(&owner.flight_fifo);
    assert(tdma_flight_fifo_reset_stopped(&owner.flight_fifo));
    /* Requeue old RX to prove reset's T2 filtering, separately from shared TX retirement. */
    for(unsigned i=0;i<4;i++)queue_position(20+i,&freeze);
    const unsigned before=ordinary_accepts;
    uint8_t kept[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    memcpy(kept,s_tdma_flight_sync.tx_image+32,sizeof(kept)); /* retained slot 0 before reset */
    assert(distributed_refmem_reset_position_stopped());
    assert(ordinary_accepts==before+4); /* co-carried VDC is never skipped */
    assert(!s_position_cancel_requested&&!s_tdma_flight_sync.position_tx_active);
    assert(!distributed_refmem_position_descriptor_pending());
    assert(!trigger_position_mailbox_pending(&s_position_rx_mailboxes[1]));
    assert(!s_tdma_flight_sync.position_rx_fragments[1].active);
    assert(s_tdma_flight_sync.position_rx_fragments[1].accepted_fragment_count==55);
    assert(owner.flight_fifo.rx_admission_epoch==admission); /* no VDC admission change */
    /* Outside the 16 B slice the retained image is untouched: header (0..7) and the
     * ACK/control/diagnostic tail (24..29) keep their pre-reset bytes. */
    assert(!memcmp(s_tdma_flight_sync.tx_image+32,kept,TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET));
    assert(!memcmp(s_tdma_flight_sync.tx_image+32+TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET,
        kept+TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET,
        TDMA_PROCESS_IMAGE_CRC_OFFSET-TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET));
    /* The whole slice is cleared, so no decodable old FREEZE survives. */
    for(unsigned i=0;i<TRIGGER_POSITION_SLICE_WIRE_SIZE;i++)
        assert(!s_tdma_flight_sync.tx_image[32+TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET+i]);
    assert(tdma_process_image_crc16_ccitt(s_tdma_flight_sync.tx_image+32,30)==
        distributed_refmem_get_le16(s_tdma_flight_sync.tx_image+32+30));
    tdma_flight_fifo_snapshot_t f;assert(tdma_service_get_flight_fifo_snapshot(&owner,&f));
    assert(!f.rx_queued_count&&!f.rx_parse_count);
    ring.enabled=ring.adapter_started=1;ring.node_count=6;
    const trigger_position_slice_t preview={.target_ms_index=30,.target_fine_slice_index=1,
        .prediction_version=1,.position_id=1,.stage=TRIGGER_POSITION_SLICE_PREVIEW};
    queue_position(100,&preview);receive();
    trigger_position_descriptor_t received;
    assert(distributed_refmem_get_position_descriptor(1,&received));
    assert(received.stage==TRIGGER_POSITION_STAGE_PREVIEW&&received.target_ms_index==30&&
        received.target_fine_slice_index==1&&received.prediction_version==1);
    assert(!distributed_refmem_get_position_descriptor(1,&received));
    const trigger_position_slice_t current_freeze={.position_id=1,.prediction_version=2,
        .stage=TRIGGER_POSITION_SLICE_FREEZE};
    queue_position(150,&current_freeze);receive();
    assert(distributed_refmem_get_position_descriptor(1,&received));
    assert(received.stage==TRIGGER_POSITION_STAGE_FREEZE&&received.prediction_version==2);
    /* A PREVIEW still waiting for T2 never drops a FREEZE; FREEZE overtakes it. */
    const trigger_position_slice_t waiting={.target_ms_index=40,.prediction_version=3,
        .position_id=2,.stage=TRIGGER_POSITION_SLICE_PREVIEW};
    const trigger_position_slice_t final3={.position_id=3,.prediction_version=4,
        .stage=TRIGGER_POSITION_SLICE_FREEZE};
    const unsigned rejects=s_tdma_flight_sync.position_rx_reject_count;
    queue_position(160,&waiting);queue_position(161,&final3);queue_position(162,&final3);receive();
    assert(s_tdma_flight_sync.position_rx_reject_count==rejects);
    assert(distributed_refmem_get_position_descriptor(1,&received));
    assert(received.stage==TRIGGER_POSITION_STAGE_FREEZE&&received.position_id==3&&
        received.prediction_version==4);
    assert(distributed_refmem_get_position_descriptor(1,&received));
    assert(received.stage==TRIGGER_POSITION_STAGE_PREVIEW&&received.position_id==2);
    assert(!distributed_refmem_get_position_descriptor(1,&received));
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
        'static trigger_position_mailbox_t s_position_rx_mailboxes[DISTRIBUTED_REFMEM_NODE_COUNT];\n'
        'static trigger_position_freeze_latch_t s_position_freeze_latches[DISTRIBUTED_REFMEM_NODE_COUNT];\n'
        'static volatile uint32_t s_position_cancel_requested;\n'
        'typedef struct {\n'
        '    uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE];\n'
        '    uint8_t target_mask, stage, position_id, pending;\n'
        '} distributed_refmem_position_pending_t;\n'
        'static distributed_refmem_position_pending_t '
        's_position_pending[DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS];\n'
        'static uint32_t s_position_drain_ms __attribute__((unused));\n'
        'static uint32_t s_position_last_drain_ms __attribute__((unused));\n'
        'static uint32_t s_position_coalesced_count __attribute__((unused));\n'
        'static uint8_t s_position_drain_cursor __attribute__((unused)), '
        's_position_wait_counted __attribute__((unused));')
    # The hold/drain receiver lives in the lane .inc, which this harness does not
    # compile: stub it so the extracted get path links (held records are absent
    # here, matching "nothing staged" for this scenario).
    harness = harness.replace('static distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;',
        'static distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;\n'
        'bool distributed_refmem_position_priority_drain(uint32_t source_slot) { (void)source_slot; return false; }\n'
        'static void distributed_refmem_position_note_freeze_taken(void) {}')
    harness = harness.replace('static void distributed_refmem_tdma_parse_position_fragment(uint32_t slot,const uint8_t *m)\n{ (void)slot; (void)m; }',
        'static void distributed_refmem_tdma_parse_position_fragment(uint32_t slot,const uint8_t *m);')
    harness = harness.replace('static void distributed_refmem_vdc_flight_rx_accept(uint32_t slot,const uint8_t *m,const tdma_flight_rx_view_t *v)\n{ (void)slot; (void)m; (void)v; }',
        'static unsigned ordinary_accepts;\nstatic void distributed_refmem_vdc_flight_rx_accept(uint32_t slot,const uint8_t *m,const tdma_flight_rx_view_t *v)\n{ (void)slot; (void)m; (void)v; ++ordinary_accepts; }')
    for name in ('distributed_refmem_tdma_parse_position_fragment',
                 'distributed_refmem_get_position_descriptor', 'distributed_refmem_reset_position_stopped',
                 'distributed_refmem_stage_position_descriptor',
                 'distributed_refmem_position_descriptor_pending'):
        harness += '\n' + ingress_definition(source, name)
    exe = compile_executable(tmp_path, 'position-stopped-reset', harness + RESET_CASES, FEEDBACK_SOURCES)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
