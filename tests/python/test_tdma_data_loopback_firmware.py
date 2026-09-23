"""Execute the four-SM DATA FSM with separate finite DMA/arbiter ownership."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest
import test_tdma_p3_loopback_firmware as p3

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def data_executable(tmp_path_factory):
    base = p3.firmware_executable.__wrapped__(tmp_path_factory)
    source = base.with_name("p3_firmware.c").read_text(encoding="utf-8")
    source = source[:source.index("\nint main(")]
    begin = source.index("/* DATA_LOOP_STUBS_BEGIN */")
    end = source.index("/* DATA_LOOP_STUBS_END */", begin) + len("/* DATA_LOOP_STUBS_END */")
    source = source[:begin] + r'''
static bool tdma_pio_spi_phys_data_loop_start(tdma_pio_spi_phys_t *,const tdma_pio_spi_p3_request_t *);
static void tdma_pio_spi_phys_data_loop_stop(tdma_pio_spi_phys_t *);
static void tdma_pio_spi_phys_data_loop_service(tdma_pio_spi_phys_t *);
''' + source[end:]
    source = source.replace('struct { uint32_t transfer_count; } ch[2];',
                            'uint32_t abort; struct { uint32_t transfer_count; } ch[9];')
    source = source.replace('s_tdma_pio_spi_rx_dma_channel = 0;', 's_tdma_pio_spi_rx_dma_channel = 4;')
    source = source.replace('s_tdma_pio_spi_tx_dma_channel = 1;', 's_tdma_pio_spi_tx_dma_channel = 5;')
    source = source.replace('void *source, uint count, bool start)', 'const void *source, uint count, bool start)')
    source = source.replace('if (start) { assert(ch == 1); tx_fifo_empty = false; }',
                            'if (start) { assert(ch == 5 || ch == 6); tx_fifo_empty = false; }')
    source = source.replace('assert(mask == 1);', 'assert(mask == ((1u << 4) | (1u << 8)));')
    source = source.replace('assert(mode == 1);', 'assert(mode == 1 || mode == 2);')
    source += r'''
#define BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL 5u
#define BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL 6u
#define BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL 4u
#define BOARD_TDMA_ORIGIN_EXECUTOR_DMA_CHANNEL 8u
#define BOARD_TDMA_SPI_RTT_SM 3u
#define PIO_FIFO_JOIN_TX 2u
#define TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS 256u
#define TDMA_STATE_MACHINE_DATA_LOOP_ADDITIONAL_RESOURCE_MASK ((1u<<12)|(1u<<17))
static int s_tdma_pio_spi_command_dma_channel=-1,s_tdma_pio_spi_executor_dma_channel=-1;
static struct { struct { uint32_t marker[512],data[256]; } data_loop; } s_tdma_pio_spi_cal_tx_workspace;
static struct { uint32_t data_train[256]; } s_tdma_pio_spi_cal_rx_workspace;
static const pio_program_t tdma_pio_spi_data_train_source_program={13,8};
static const pio_program_t tdma_pio_spi_data_train_sink_program={14,7};
static const uint16_t tdma_pio_spi_data_train_source_program_instructions[8];
static const uint16_t tdma_pio_spi_data_train_sink_program_instructions[7];
static bool claimed[9],lease,lease_conflict;
static uint32_t disabled_dma,configured_channels;
static bool dma_channel_is_claimed(uint ch){return claimed[ch];}
static void dma_channel_claim(uint ch){assert(!claimed[ch]);claimed[ch]=true;}
static void dma_channel_unclaim(uint ch){assert(claimed[ch]);assert(!dma_channel_is_busy(ch));claimed[ch]=false;}
static bool resource_arbiter_acquire_owned(uint32_t mask,const char *owner){
    assert(mask==TDMA_STATE_MACHINE_DATA_LOOP_ADDITIONAL_RESOURCE_MASK);
    assert(strcmp(owner,"TDMA_MAINTENANCE_PIO")==0);if(lease_conflict)return false;
    assert(!lease);lease=true;return true;
}
static void resource_arbiter_release_owned(uint32_t mask,const char *owner){
    assert(mask==TDMA_STATE_MACHINE_DATA_LOOP_ADDITIONAL_RESOURCE_MASK);assert(owner&&lease);
    assert(!claimed[6]&&!claimed[8]);lease=false;
}
static void tdma_pio_spi_phys_disable_dma_mask(uint32_t mask){disabled_dma|=mask;}
static void __dmb(void){}
static void tdma_pio_spi_phys_set_line_drivers(bool on){drivers[30]=drivers[31]=drivers[32]=on;}
static void pio_sm_set_pins_with_mask64(void *p,uint sm,uint64_t pins,uint64_t mask){(void)p;(void)sm;(void)pins;(void)mask;}
static void sm_config_set_out_pins(pio_sm_config *c,uint pin,uint count){(void)c;assert(pin==29&&count==1);}
static void sm_config_set_out_shift(pio_sm_config *c,bool right,bool pull,uint n){(void)c;assert(right&&pull&&n==32);}
static pio_sm_config tdma_pio_spi_data_train_source_program_get_default_config(uint off){(void)off;return (pio_sm_config){0};}
static pio_sm_config tdma_pio_spi_data_train_sink_program_get_default_config(uint off){(void)off;return (pio_sm_config){0};}
static void channel_config_set_chain_to(dma_channel_config *c,uint ch){assert(*c==ch);configured_channels|=1u<<ch;}
static void channel_config_set_ring(dma_channel_config *c,bool w,uint n){(void)c;assert(!w&&n==0);}
static void channel_config_set_sniff_enable(dma_channel_config *c,bool on){(void)c;assert(!on);}
static void channel_config_set_irq_quiet(dma_channel_config *c,bool on){(void)c;assert(on);}
static uint32_t tdma_pio_spi_data_train_source_program_init(void *p,uint sm,uint off,uint cs,uint data,uint phase){
    (void)p;assert(sm==1&&off==13&&cs==27&&data==29&&phase>=1&&phase<=32);
    configured|=1u<<sm;return 0xa042u|((phase-1)<<8);
}
static uint32_t tdma_pio_spi_data_train_sink_program_init(void *p,uint sm,uint off,uint cs,uint data,uint phase){
    (void)p;assert(sm==2&&off==14&&cs==26&&data==24&&phase>=1&&phase<=32);
    configured|=1u<<sm;return 0xa042u|((phase-1)<<8);
}
'''
    pio = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(encoding="utf-8")
    for kind in ("source", "sink"):
        name = f"tdma_pio_spi_data_train_{kind}_program_init"
        initializer = p3._definition(pio, f"static inline uint32_t {name}(", "\n}")
        source += initializer.replace(name, f"actual_data_{kind}_init").replace("PIO pio", "typeof(&mock_pio) pio")
    source += (ROOT / "components/tdma/src/tdma_pio_spi_phys_data_loop.inc").read_text(encoding="utf-8")
    source += r'''
static uint data_copy_race;
static bool tdma_runtime_owner_copy_data_loop_capture(uint32_t epoch,uint32_t kind,uint32_t offset,
    uint32_t count,uint32_t generation,uint32_t *words,uint32_t *total){
    bool copied=tdma_pio_spi_phys_copy_data_loop_capture(manager_phys,epoch,kind,offset,count,generation,words,total);
    if(data_copy_race==1) s_p3_intent.guard+=2;
    if(data_copy_race==2) ++s_p3_intent.sequence;
    if(data_copy_race==3) s_marker_loopback_accepted_sequence=0;
    if(data_copy_race==4) s_p3_intent_consumed_sequence=0;
    return copied;
}
'''
    manager = (ROOT / "components/calibration_manager/src/calibration_manager.c").read_text(encoding="utf-8")
    source += p3._definition(manager, "bool calibration_manager_copy_data_loop_capture(", "\n}")
    source += r'''
static void tick(tdma_pio_spi_phys_t *p,bool complete_abort){
    if(complete_abort&&p->data_loop_abort_issued){
        const uint32_t mask=dma_hw->abort;
        for(uint ch=0;ch<9;++ch)if(mask&(1u<<ch))dma_hw->ch[ch].transfer_count=0;
        dma_hw->abort=0;
    }
    tdma_pio_spi_phys_p3_service(p);
}
static void until(tdma_pio_spi_phys_t *p,uint state){
    for(uint i=0;i<200&&p->data_loop.fields[56]!=state;++i)tick(p,true);
    assert(p->data_loop.fields[56]==state);
}
static void common_wave(uint fault){
    for(uint i=0;i<8192;++i){
        uint32_t byte=(1u<<2)|(1u<<3);
        if(fault!=1){if(i>=260)byte&=~(1u<<2);if(i>=273)byte&=~(1u<<3);}
        if(fault==0){if(i>=1280)byte|=1u<<5;if(i>=1293)byte|=1u;}
        const uint shift=(i%4)*8;
        s_tdma_pio_spi_marker_loopback_capture[i/4]=(s_tdma_pio_spi_marker_loopback_capture[i/4]&~(255u<<shift))|(byte<<shift);
    }
}
int main(int argc,char **argv){
    assert(argc==2);const uint which=(uint)strtoul(argv[1],NULL,10);
    tdma_pio_spi_phys_t p;initialize(&p);
    for(uint phase=1;phase<=32;++phase){
        assert(actual_data_source_init(&mock_pio,1,0,27,29,phase)==(0xa042u|((phase-1u)<<8)));
        assert(mock_pio.instr_mem[3]==pio_encode_wait_gpio(true,27)&&mock_pio.instr_mem[4]==pio_encode_wait_gpio(false,27));
        assert(mock_pio.instr_mem[6]==(0xa042u|((phase-1u)<<8)));
        assert(actual_data_sink_init(&mock_pio,2,8,26,24,phase)==(0xa042u|((phase-1u)<<8)));
        assert(mock_pio.instr_mem[10]==pio_encode_wait_gpio(true,26)&&mock_pio.instr_mem[11]==pio_encode_wait_gpio(false,26));
        assert(mock_pio.instr_mem[13]==(0xa042u|((phase-1u)<<8)));
    }
    uint32_t marker[201],data[201];for(uint i=0;i<201;++i){marker[i]=0xf1234567u^i;data[i]=0x81234567u+i;}
    const uint fault=which==1?1u:which==2?2u:0u;
    tdma_pio_spi_p3_request_t r={.role=8,.epoch=51,.marker_codebook=1,.marker_generation=91,
        .marker_words=marker,.data_loop_words=data,.marker_word_count=201,.marker_sample_count=6420,
        .rxphase_base_ns=26,.data_loop_mark_offset=-1,.sck_launch_offset_samples=0,
        .data_loop_gap=1000,.marker_fault=fault};
    tdma_pio_spi_p3_request_t bad=r;bad.data_loop_words=NULL;assert(!tdma_pio_spi_phys_p3_start(&p,&bad));
    bad=r;bad.data_loop_gap=1001;assert(!tdma_pio_spi_phys_p3_start(&p,&bad));
    bad=r;bad.data_loop_mark_offset=-8;assert(!tdma_pio_spi_phys_p3_start(&p,&bad));
    bad=r;bad.sck_launch_offset_samples=12;assert(!tdma_pio_spi_phys_p3_start(&p,&bad));
    bad=r;bad.data_loop_mark_offset=11;bad.sck_launch_offset_samples=-10;assert(!tdma_pio_spi_phys_p3_start(&p,&bad));
    bad=r;bad.rxphase_base_ns=44;bad.data_loop_mark_offset=-11;assert(!tdma_pio_spi_phys_p3_start(&p,&bad));
    assert(tdma_pio_spi_phys_p3_start(&p,&r));
    assert(p.data_loop.fields[18]==7&&p.data_loop.fields[19]==21&&p.data_loop.fields[17]==886);
    assert(p.data_loop.fields[24]==211&&p.data_loop.fields[25]==203);
    assert(s_tdma_pio_spi_cal_tx_workspace.data_loop.marker[8]==(fault==1?UINT32_MAX:marker[0]));
    assert(s_tdma_pio_spi_cal_tx_workspace.data_loop.data[0]==(fault==2?0u:data[0]));
    if(which>=10&&which<14)fail_program=(uint[]){6,13,14,4}[which-10];
    if(which==14)lease_conflict=true;
    if(which==15)claimed[6]=true;
    if(which==16)claimed[8]=true;
    if(which==17)fail_dma=true;
    if(which==18)fail_resources=true;
    if(which>=10&&which<=18){
        until(&p,0);assert(p.p3.state==3&&p.data_loop.fields[4]==2);
        assert(!lease&&claimed[6]==(which==15)&&claimed[8]==(which==16));
        assert_restored(&p);puts("data loop passed");return 0;
    }
    if(which>=30&&which<55){
        for(uint i=0;i<which-30;++i)tick(&p,true);
        tdma_pio_spi_phys_p3_stop(&p);until(&p,0);
        assert(p.p3.state==0);assert_restored(&p);assert(!lease&&!claimed[6]&&!claimed[8]);
        puts("data loop passed");return 0;
    }
    until(&p,DATA_LOOP_RUN);
    assert(enabled==15&&used_words==18&&lease&&claimed[6]&&claimed[8]);
    assert(configured_channels==((1u<<4)|(1u<<5)|(1u<<6)|(1u<<8)));
    assert(drivers[30]&&!drivers[31]&&drivers[32]);
    common_wave(fault);
    dma_hw->ch[5].transfer_count=0;
    dma_hw->ch[6].transfer_count=fault==1?196:0;
    dma_hw->ch[4].transfer_count=fault==1?256:which==3?240:0;
    dma_hw->ch[8].transfer_count=0;
    tx_fifo_empty=true;
    tick(&p,true);mock_pio.fdebug=(1u<<24)|(1u<<25);tick(&p,true);
    if(fault==1||which==3)now_us=p.data_loop_deadline;
    tick(&p,true);until(&p,DATA_LOOP_ABORT);
    assert(p.data_loop.fields[31]==(fault==1?0u:which==3?16u:256u));
    assert(p.data_loop.fields[34]==2048);
    tick(&p,false);const uint loaded_before=loaded;
    for(uint i=0;i<4;++i)tick(&p,false);
    assert(loaded==loaded_before&&lease&&p.data_loop.fields[56]==DATA_LOOP_ABORT);
    assert(p.data_loop.fields[58]!=0);assert(enabled==0);
    if(which>=55&&which<70){
        until(&p,(uint[]){DATA_LOOP_QUIESCED,DATA_LOOP_DECODE,DATA_LOOP_REMOVE,DATA_LOOP_RELEASE,DATA_LOOP_RESTORE,DATA_LOOP_PUBLISH}[((which-55)%6)]);
        tdma_pio_spi_phys_p3_stop(&p);
    }
    until(&p,0);assert_restored(&p);assert(!lease&&!claimed[6]&&!claimed[8]);
    assert(p.data_loop.fields[54]==0&&p.data_loop.fields[55]==0&&p.data_loop.fields[57]==0&&p.data_loop.fields[58]==0);
    if(which>=55){assert(p.p3.state==0);puts("data loop passed");return 0;}
    assert(p.p3.state==(fault||which==3?3u:2u));
    assert(p.data_loop.fields[4]==(fault==1||which==3?4u:fault==2?1u:0u));
    assert(p.data_loop.fields[48]==(fault==1?0u:fault==2?3u:15u));
    if(fault==0){assert(p.data_loop.fields[49]==260&&p.data_loop.fields[52]==1293);}
    uint32_t words[32],total;
    assert(tdma_pio_spi_phys_copy_data_loop_capture(&p,51,0,0,1,91,words,&total)==(fault!=1));
    assert(tdma_pio_spi_phys_copy_data_loop_capture(&p,51,1,0,1,91,words,&total));
    assert(!tdma_pio_spi_phys_copy_data_loop_capture(&p,51,1,0,1,90,words,&total));
    assert(!tdma_pio_spi_phys_copy_data_loop_capture(&p,51,2,0,1,91,words,&total));
    const uint32_t saved_role=p.p3.role;p.p3.role=7;
    tdma_pio_spi_data_loop_snapshot_t foreign_snapshot;
    assert(!tdma_pio_spi_phys_get_data_loop_snapshot(&p,&foreign_snapshot));
    assert(!tdma_pio_spi_phys_copy_data_loop_capture(&p,51,1,0,1,91,words,&total));p.p3.role=saved_role;
    manager_raw=p.p3;manager_stops=0;
    s_p3_intent=(calibration_p3_intent_t){.guard=2,.sequence=91,.opcode=1,.request={.role=8}};
    s_p3_intent_consumed_sequence=91;
    assert(!calibration_manager_stop_codeword_loopback(7));assert(manager_stops==0);
    assert(calibration_manager_stop_codeword_loopback(8));assert(manager_stops==1);
    manager_phys=&p;
    for(uint mode=0;mode<10;++mode){
        s_p3_intent=(calibration_p3_intent_t){.guard=2,.sequence=91,.opcode=1,.request={.role=8}};
        s_p3_intent_consumed_sequence=s_marker_loopback_accepted_sequence=91;data_copy_race=0;
        if(mode==1)s_p3_intent.request.role=7;
        if(mode==2)s_p3_intent.sequence=92;
        if(mode==3)s_marker_loopback_accepted_sequence=0;
        if(mode==4)s_p3_intent.opcode=2;
        if(mode>=5&&mode<=8)data_copy_race=mode-4;
        assert(calibration_manager_copy_data_loop_capture(51,1,0,1,mode==9?90:91,words,&total)==(mode==0));
    }
    tdma_pio_spi_phys_p3_stop(&p);tdma_pio_spi_data_loop_snapshot_t snapshot;
    p.data_loop.fields[55]=1;assert(tdma_pio_spi_phys_get_data_loop_snapshot(&p,&snapshot));
    assert(snapshot.fields[55]==1); /* Never erase residual resource evidence at IDLE. */
    assert(!tdma_pio_spi_phys_copy_data_loop_capture(&p,51,1,0,1,91,words,&total));
    puts("data loop passed");return 0;
}
'''
    target = tmp_path_factory.mktemp("data-loop")
    cfile = target / "data_loop.c"
    cfile.write_text(source, encoding="utf-8")
    executable = target / "data_loop.exe"
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe"
    result = subprocess.run([compiler, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                             "-Wno-unused-function", str(cfile), "-o", str(executable)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("case", [0, 1, 2, 3] + list(range(10, 19)) + list(range(30, 70)))
def test_data_loop_firmware(data_executable, case):
    result = subprocess.run([str(data_executable), str(case)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "data loop passed" in result.stdout
