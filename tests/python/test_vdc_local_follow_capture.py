"""Real bounded local append and independent interval/adoption decoding."""
import struct
import pytest
from test_vdc_auto_capture import PREAMBLE, read_records, run_capture
from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope='module')
def local_capture_exe(tmp_path_factory):
    source=(ROOT/'components/vdc_dpll_manager/src/vdc_boundary_capture.inc').read_text(encoding='utf-8')
    return compile_executable(tmp_path_factory.mktemp('local-capture'),'local_capture',
        PREAMBLE+source+CASES,
        [ROOT/'components/distributed_refmem/src/refmem_sync_vdc_feedback.c'])


def test_full_width_local_interval_and_real_commit(local_capture_exe,tmp_path):
    rows=read_records(tmp_path,run_capture(local_capture_exe,'flow'))['samples']['NO1']
    assert [r['capture_kind'] for r in rows]==['local_observation','local_commit']
    o,c=[r['local_control'] for r in rows]
    assert (o['error_ppb_lo'],o['error_ppb_hi'])==(896,1204)
    assert (c['before_dco_seq'],c['after_dco_seq'],c['delta_rate_ppb'])==(17,18,-224)
    assert c['remote_command_seq']==19 and c['directed_delay_ns']==242
    assert c['applied'] and c['source_first_width_ns']==100 and c['source_last_width_ns']==200


@pytest.mark.parametrize('case',['hold','rejected','capacity','disabled'])
def test_hold_rejection_and_bounded_recording(local_capture_exe,tmp_path,case):
    records=run_capture(local_capture_exe,case)
    if case in ('capacity','disabled'):
        assert not records
    else:
        c=read_records(tmp_path,records)['samples']['NO1'][1]['local_control']
        assert not c['applied'] and c['before_dco_seq']==c['after_dco_seq']


@pytest.mark.parametrize('offset,value,fmt',[
    (8,0,'q'),(96,3,'I'),(100+16,100,'I'),(100+28,19,'I'),
    (100+24,0,'i'),(100+32,0,'I'),(100+36,0,'I'),(100+52,99,'I'),
    (100+64,0,'I'),(100+68,0,'I'),(100+72,224,'i'),(100+80,0,'I'),
    (100+88,0,'Q'),(100+20,11|(1<<8),'I')])
def test_crc_valid_but_false_local_evidence_rejected(local_capture_exe,tmp_path,offset,value,fmt):
    records=bytearray(run_capture(local_capture_exe,'flow'))
    struct.pack_into('<'+fmt,records,offset,value)
    with pytest.raises(ValueError): read_records(tmp_path,records)


def test_orphan_and_repeated_candidate_rejected(local_capture_exe,tmp_path):
    records=run_capture(local_capture_exe,'flow')
    with pytest.raises(ValueError,match='orphan'): read_records(tmp_path,records[:100])
    repeated=bytearray(records+records)
    struct.pack_into('<I',repeated,200,3);struct.pack_into('<I',repeated,300,4)
    with pytest.raises(ValueError,match='twice'): read_records(tmp_path,repeated)


CASES=r'''
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
int main(int argc,char **argv){
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    assert(argc==2);(void)vdc_boundary_capture_offer_core1;(void)vdc_boundary_capture_apply_core1;
    (void)vdc_boundary_capture_ack_core1;(void)vdc_boundary_capture_hold_core1;
    (void)s_vdc_domain;(void)s_published_snapshot;(void)s_published_snapshot_guard;
    (void)s_published_dpll_update_seq;(void)s_published_snapshot_valid;(void)s_dpll_capture_auto_only;
    (void)s_vdc_follower_capture_kind_hint;(void)s_dpll_capture_first_update_seq;
    (void)s_dpll_capture_last_update_seq;(void)osal_critical_enter;(void)osal_critical_exit;
    vdc_local_follow_candidate_t c={.owner_token=17,.serial=2,.session=99,.role_generation=5,
        .ring_config_seq=7,.schedule_crc32=33,.local_slot=1,.reference_slot=0,
        .local_model_token=3,.reference_model_token=9,.reference_receive_count=4,
        .path_table_crc32=1234,.directed_delay_ns=242,
        .result={.has_pair=1,.reserved=4,.raw_ppb_lo=896,.raw_ppb_hi=1204,
            .source={.source_arm_epoch=77,.observer_epoch=8},
            .pairs={{.rx_elapsed_cycles=100,.rx_width_ns=100,.reference_tx_lo=342,.reference_tx_hi=345,
                     .source_model_token=3,.reference_identity_crc32=9,.measurement_sequence=10},
                    {.rx_elapsed_cycles=1000001100,.rx_width_ns=200,
                     .reference_tx_lo=1000000342,.reference_tx_hi=1000000345,
                     .source_model_token=3,.reference_identity_crc32=9,.measurement_sequence=20}}}};
    bool applied=true;int32_t step=-224;
    if(!strcmp(argv[1],"hold")){applied=false;step=0;}
    if(!strcmp(argv[1],"rejected"))applied=false;
    if(!strcmp(argv[1],"capacity"))s_dpll_capture_count=VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES-1;
    if(!strcmp(argv[1],"disabled"))s_dpll_capture_armed=false;
    vdc_boundary_capture_local_core1(&c,step,0,17,applied?-224:0,applied?18:17,19,applied);
    if(!strcmp(argv[1],"capacity")){
        assert(s_dpll_capture_count==VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES-1 && s_dpll_capture_dropped==2);
    }else if(!strcmp(argv[1],"disabled"))assert(!s_dpll_capture_count);
    else {assert(s_dpll_capture_count==2);fwrite(s_dpll_capture_records,100,2,stdout);}
    return 0;
}
'''
