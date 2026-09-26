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
