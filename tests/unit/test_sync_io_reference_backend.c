/* Execute the production backend against a DMA chain model. The model reloads
 * each trigger from its programmed transfer count (RP2350 TRANS_COUNT_COUNT).
 * Peripheral timing and arbitration latency remain HIL obligations. */
#include "reference_mocks.h"
#include REFERENCE_SOURCE

/* Exit instead of opening a Windows CRT dialog on negative-control failures. */
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr,"assertion line %d: %s\n",__LINE__,#c);exit(1); } } while(0)

static void retire_release(uint32_t generation)
{
    mock_core=1;sync_io_reference_cancel();sync_io_reference_service_core1();
    assert(s_aborting && hw_dma.abort==SYNC_IO_REFERENCE_DMA_MASK);
    mock_core=0;assert(!sync_io_reference_release(generation));
    mock_core=1;sync_io_reference_service_core1();
    assert(s_state!=SYNC_IO_REFERENCE_RETIRED);
    abort_ack();sync_io_reference_service_core1();
    assert(s_state==SYNC_IO_REFERENCE_RETIRED);
    mock_core=0;assert(!sync_io_reference_release(generation+1));
    assert(sync_io_reference_release(generation));
    assert(!claimed[REF_POP0_DMA]&&!claimed[REF_STAMP0_DMA]&&!sm_claimed&&!gate);
    assert(claimed[0]&&claimed[1]);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    const sync_io_reference_config_t c={4,0,10000000,1000,2500};
    uint32_t generation=0;
    hw_timer.source=TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS;
    claimed[0]=claimed[1]=true; /* Board RS485 remains resident. */
    sync_io_reference_snapshot_t snap;
    if(!strcmp(argv[1],"admission")) {
        mock_core=1;assert(!sync_io_reference_prepare(&c,&generation));mock_core=0;
        gate=true;assert(!sync_io_reference_prepare(&c,&generation));gate=false;
        assert(sync_io_reference_get_snapshot(&snap)&&snap.reason==SYNC_IO_REFERENCE_ADMISSION_BUSY);
        hw_timer.pause=1;assert(!sync_io_reference_prepare(&c,&generation));hw_timer.pause=0;
        assert(sync_io_reference_get_snapshot(&snap)&&snap.reason==SYNC_IO_REFERENCE_CLOCK_ERROR);
        deny_add=true;assert(!sync_io_reference_prepare(&c,&generation));deny_add=false;
        assert(!gate&&!claimed[REF_POP0_DMA]&&!claimed[REF_STAMP0_DMA]&&!sm_claimed);
        assert(sync_io_reference_get_snapshot(&snap)&&snap.reason==SYNC_IO_REFERENCE_PERSONA_UNAVAILABLE);
        for(uint ch=REF_POP0_DMA;ch<=REF_STAMP0_DMA;ch++) {
            claimed[ch]=true;
            assert(!sync_io_reference_prepare(&c,&generation));
            assert(claimed[ch]&&!sm_claimed&&!gate);claimed[ch]=false;
            busy[ch]=true;
            assert(!sync_io_reference_prepare(&c,&generation));
            assert(busy[ch]&&!claimed[ch]&&!sm_claimed&&!gate);busy[ch]=false;
        }
    }
    assert(sync_io_reference_prepare(&c,&generation)&&generation==1);
    assert(sync_io_reference_get_snapshot(&snap)&&snap.state==SYNC_IO_REFERENCE_PREPARED);
    assert(snap.input_pin==20&&snap.reference_cycles==10000000);
    assert(!sync_io_reference_prepare(&c,&generation));
    if(!strcmp(argv[1],"cancel_prepared")){retire_release(generation);return 0;}
    mock_core=1;raw(100);sync_io_reference_service_core1();
    assert(s_state==SYNC_IO_REFERENCE_RUNNING&&enabled&&busy[REF_POP0_DMA]);
    assert(cfg[REF_POP0_DMA].dreq==2 && cfg[REF_STAMP0_DMA].dreq==DREQ_FORCE);
    assert(reload[REF_POP0_DMA]==1 && reload[REF_STAMP0_DMA]==1);
    if(!strncmp(argv[1],"timeout_",8)) {
        if(!strcmp(argv[1],"timeout_after_success") || !strcmp(argv[1],"timeout_bad_token") ||
            !strcmp(argv[1],"timeout_late_complete") || !strcmp(argv[1],"timeout_delayed_service")) {
            raw(1000);fifo_push(c.nominal_hz-(!strcmp(argv[1],"timeout_bad_token")?2u:1u));pump();
            raw(!strcmp(argv[1],"timeout_late_complete")?s_ref.deadline_raw+2000u:250001001u);
            fifo_push(UINT32_MAX);pump();
            if(!strcmp(argv[1],"timeout_after_success")) {
                sync_io_reference_service_core1();assert(s_ref.valid && s_ref.sample_seq==1u);
                abort_ack();sync_io_reference_service_core1();
            }
        }
        const uint32_t before_seq=s_ref.sample_seq;
        if(!strcmp(argv[1],"timeout_partial")) {
            raw(1000);fifo_push(c.nominal_hz-1);pump();
            assert(hw_dma.ch[REF_STAMP0_DMA].write_addr==(uintptr_t)(s_stamps+1));
        }
        raw(s_ref.deadline_raw+2001u);
        if(!strcmp(argv[1],"timeout_bad_address"))
            hw_dma.ch[REF_STAMP0_DMA].write_addr=(uintptr_t)s_stamps+12u;
        if(!strcmp(argv[1],"timeout_fault"))
            hw_dma.ch[REF_POP0_DMA].ctrl_trig|=DMA_CH0_CTRL_TRIG_READ_ERROR_BITS;
        sync_io_reference_service_core1();
        if(!strcmp(argv[1],"timeout_bad_address") || !strcmp(argv[1],"timeout_bad_token")) {
            assert(s_ref.reason==SYNC_IO_REFERENCE_BAD_RECORD && !s_restart);
            abort_ack();sync_io_reference_service_core1();
            assert(s_state==SYNC_IO_REFERENCE_RETIRED);
            mock_core=0;assert(sync_io_reference_release(generation));return 0;
        }
        if(!strcmp(argv[1],"timeout_fault")) {
            assert(s_ref.reason==SYNC_IO_REFERENCE_DMA_ERROR && !s_restart);
            abort_ack();sync_io_reference_service_core1();
            assert(s_state==SYNC_IO_REFERENCE_RETIRED);
            mock_core=0;assert(sync_io_reference_release(generation));return 0;
        }
        assert(s_aborting && s_restart && s_ref.reason==SYNC_IO_REFERENCE_TIMEOUT);
        assert(!s_ref.valid && !enabled && s_ref.sample_seq==before_seq);
        sync_io_reference_service_core1();assert(s_aborting && !enabled);
        if(!strcmp(argv[1],"timeout_late_fault")) {
            hw_dma.ch[REF_STAMP0_DMA].ctrl_trig|=DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
            abort_ack();sync_io_reference_service_core1();
            assert(s_ref.reason==SYNC_IO_REFERENCE_DMA_ERROR && !enabled && !s_restart);
            abort_ack();sync_io_reference_service_core1();
            assert(s_state==SYNC_IO_REFERENCE_RETIRED);
            mock_core=0;assert(sync_io_reference_release(generation));return 0;
        }
        if(!strcmp(argv[1],"timeout_cancel")) {
            sync_io_reference_cancel();abort_ack();sync_io_reference_service_core1();
            assert(s_state==SYNC_IO_REFERENCE_RETIRED && s_ref.reason==SYNC_IO_REFERENCE_CANCELLED);
            mock_core=0;assert(sync_io_reference_release(generation));return 0;
        }
        abort_ack();busy[REF_STAMP0_DMA]=true;
        sync_io_reference_service_core1();assert(s_aborting && !enabled);
        busy[REF_STAMP0_DMA]=false;sync_io_reference_service_core1();
        for(unsigned retry=0;retry<3;++retry) {
            assert(s_state==SYNC_IO_REFERENCE_RUNNING && enabled);
            assert(s_ref.generation==generation && s_ref.sample_seq==before_seq && !s_ref.valid);
            assert(s_ref.reason==SYNC_IO_REFERENCE_TIMEOUT && s_stamps[0]==0u && s_stamps[1]==0u);
            assert(sm_claimed && claimed[REF_POP0_DMA] && claimed[REF_STAMP0_DMA] && gate);
            raw(s_ref.deadline_raw+1);sync_io_reference_service_core1();
            abort_ack();sync_io_reference_service_core1();
        }
        const uint64_t start=s_started+1000u;
        raw(start);fifo_push(c.nominal_hz-1);pump();
        sync_io_reference_service_core1();assert(!s_ref.valid && s_ref.sample_seq==before_seq);
        raw(start+250000001u);fifo_push(UINT32_MAX);pump();
        sync_io_reference_service_core1();
        assert(s_ref.valid && s_ref.sample_seq==before_seq+1u && s_ref.reason==SYNC_IO_REFERENCE_OK);
        assert(s_ref.elapsed_ticks==250000000u && s_ref.frequency_error_ppb==0);
        assert(s_ref.generation==generation);
        abort_ack();sync_io_reference_service_core1();
        retire_release(generation);return 0;
    }
    if(!strcmp(argv[1],"timeout")) {
        raw(s_ref.deadline_raw+1);sync_io_reference_service_core1();
        assert(s_ref.reason==SYNC_IO_REFERENCE_TIMEOUT&&!s_ref.valid);
        abort_ack();sync_io_reference_service_core1();
        assert(s_state==SYNC_IO_REFERENCE_RUNNING && s_ref.reason==SYNC_IO_REFERENCE_TIMEOUT);
        retire_release(generation);return 0;
    }
    if(!strcmp(argv[1],"clock")) {
        mock_hz--;sync_io_reference_service_core1();
        assert(s_ref.reason==SYNC_IO_REFERENCE_CLOCK_ERROR&&!s_ref.valid);
        abort_ack();sync_io_reference_service_core1();
        mock_core=0;assert(sync_io_reference_release(generation));return 0;
    }
    if(!strcmp(argv[1],"dma")) {
        hw_dma.ch[REF_POP0_DMA].ctrl_trig|=DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS|DMA_CH0_CTRL_TRIG_READ_ERROR_BITS;
        hw_dma.ch[REF_STAMP0_DMA].ctrl_trig|=DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS|DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
        sync_io_reference_service_core1();assert(s_ref.reason==SYNC_IO_REFERENCE_DMA_ERROR);
        abort_ack();sync_io_reference_service_core1();
        mock_core=0;assert(sync_io_reference_release(generation));
        assert(hw_dma.ch[REF_POP0_DMA].ctrl_trig&DMA_CH0_CTRL_TRIG_READ_ERROR_BITS);
        assert(hw_dma.ch[REF_STAMP0_DMA].ctrl_trig&DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS);
        assert(sync_io_reference_prepare(&c,&generation)&&generation==2);
        assert(!hw_dma.ch[REF_POP0_DMA].ctrl_trig&&!hw_dma.ch[REF_STAMP0_DMA].ctrl_trig);
        mock_core=1;sync_io_reference_service_core1();
        assert(s_state==SYNC_IO_REFERENCE_RUNNING&&s_ref.reason==SYNC_IO_REFERENCE_OK);
    }
    for(unsigned window=0;window<3;++window) {
        raw(1000ull+window*300000000ull);fifo_push(c.nominal_hz-1);pump();
        assert(fifo_count==0 && busy[REF_POP0_DMA] && !busy[REF_STAMP0_DMA]);
        assert(hw_dma.ch[REF_POP0_DMA].write_addr==(uintptr_t)(s_tokens+1));
        assert(hw_dma.ch[REF_STAMP0_DMA].write_addr==(uintptr_t)(s_stamps+1));
        assert(s_ref.sample_seq==window);
        sync_io_reference_service_core1();assert(s_ref.sample_seq==window);
        /* Both tokens arrive independently; a delayed second stamp must not
         * be published merely because its FIFO pop already completed. */
        raw(250001001ull+window*300000000ull);fifo_push(UINT32_MAX);
        step_dma(REF_POP0_DMA);assert(fifo_count==0&&busy[REF_STAMP0_DMA]);
        sync_io_reference_service_core1();assert(s_ref.sample_seq==window);
        step_dma(REF_STAMP0_DMA);pump();assert(busy[REF_POP0_DMA]&&!busy[REF_STAMP0_DMA]&&fifo_count==0);
        assert(hw_dma.ch[REF_POP0_DMA].write_addr==(uintptr_t)(s_tokens+2));
        assert(hw_dma.ch[REF_STAMP0_DMA].write_addr==(uintptr_t)(s_stamps+2));
        for(unsigned i=0;i<10;++i)pump();
        assert(hw_dma.ch[REF_POP0_DMA].write_addr==(uintptr_t)(s_tokens+2));
        sync_io_reference_service_core1();
        assert(sync_io_reference_get_snapshot(&snap)&&snap.valid&&snap.sample_seq==window+1);
        assert(snap.elapsed_ticks==250000000&&snap.frequency_error_ppb==0);
        assert(snap.measurement_flags&SYNC_IO_REFERENCE_DMA_LATENCY_UNBOUNDED);
        assert(s_aborting&&!enabled);
        abort_ack();sync_io_reference_service_core1();
        assert(s_state==SYNC_IO_REFERENCE_RUNNING&&enabled);
    }
    retire_release(generation);
    return 0;
}
