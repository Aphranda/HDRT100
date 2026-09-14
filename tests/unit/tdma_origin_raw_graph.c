/* Expose the actual immutable graph and its typed layout to the DMA model. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tdma_pio_spi_origin_workspace.h"
#include "../../components/tdma/src/tdma_origin_plan.c"

static unsigned active_count(unsigned mask)
{
    unsigned count=0;
    for (;mask;mask>>=1) count+=mask&1u;
    return count;
}

static tdma_origin_plan_config_t make_config(unsigned nodes,unsigned mask,unsigned local,unsigned guard)
{
    tdma_origin_plan_config_t c = {
        .address = {.capture_bank={0x20000000,0x20000800},.stage=0x20001000,
            .tx_header=0x20002000,.rx_packet=0x20003000,.state=0x20004000,
            .local_shadow={0x20005000,0x20005800},.scratch=0x20006000,
            .runs=0x20008000,.literals=0x2000a000,.records=0x2000b000},
        .physical_bytes=nodes*32+51,.packet_size=nodes*32+36,
        .outer_header_bytes=4,.capture_prefix_bits=36,.guard_count=guard,.abort_poll_count=8,
        .local_slot=local,.active_slot_mask=mask,
        .returned_route_word=0x502|((active_count(mask)-1)*0x1010000),
        .capture_dma=4,.output_dma=5,.loader_dma=6,.executor_dma=8,
        .control_pio=1,.data_pio=2,.control_sm=0,.capture_sm=3,.data_sm=2,.helper_sm=0,
        .control_pc=TDMA_ORIGIN_CONTROL_PC,.capture_pc=TDMA_ORIGIN_CAPTURE_PC,
        .data_pc=TDMA_ORIGIN_DATA_PC,.helper_pc=TDMA_ORIGIN_HELPER_PC,
        .compare_pc=TDMA_ORIGIN_COMPARE_PC,.rtt_sm=1,.rtt_pc=TDMA_ORIGIN_RTT_PC};
    return c;
}

static void configuration_matrix(void)
{
    tdma_flight_overlay_dma_run_t runs[TDMA_ORIGIN_PLAN_RUN_MAX];
    uint32_t literals[TDMA_ORIGIN_PLAN_LITERAL_MAX];
    const unsigned guards[]={1,2,8,256,257,65535,65536};
    const unsigned aborts[]={1,8,65535};
    unsigned cases=0,max_runs=0,max_literals=0,max_step=0;
    for (unsigned nodes=2;nodes<=TDMA_FLIGHT_SHORT_SLOT_COUNT;++nodes) {
        for (unsigned mask=1;mask<(1u<<nodes);++mask) {
            if (active_count(mask)<2) continue;
            for (unsigned local=0;local<nodes;++local) {
                if (!(mask&(1u<<local))) continue;
                for (unsigned g=0;g<sizeof(guards)/sizeof(*guards);++g) {
                    for (unsigned a=0;a<sizeof(aborts)/sizeof(*aborts);++a) {
                        for (unsigned skip=0;skip<2;++skip) {
                            tdma_origin_plan_config_t c=make_config(nodes,mask,local,guards[g]);
                            c.abort_poll_count=aborts[a];
                            c.diagnostic_skip_records=(uint8_t)skip;
                            tdma_origin_plan_t p={.runs=runs,.literals=literals,
                                .run_capacity=TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY,
                                .literal_capacity=TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY};
                            tdma_origin_plan_builder_t b={0};
                            assert(tdma_origin_plan_begin(&b,&c,&p));
                            tdma_origin_build_result_t result;
                            do {
                                unsigned before=p.run_count;
                                result=tdma_origin_plan_step(&b);
                                if (p.run_count>before && p.run_count-before>max_step)
                                    max_step=p.run_count-before;
                            } while(result==TDMA_ORIGIN_BUILD_BUSY);
                            if (result!=TDMA_ORIGIN_BUILD_DONE) {
                                fprintf(stderr,"matrix failed nodes=%u mask=%u local=%u guard=%u abort=%u skip=%u runs=%u literals=%u\n",
                                    nodes,mask,local,guards[g],aborts[a],skip,p.run_count,p.literal_count);
                                assert(result==TDMA_ORIGIN_BUILD_DONE);
                            }
                            if (p.run_count>max_runs) max_runs=p.run_count;
                            if (p.literal_count>max_literals) max_literals=p.literal_count;
                            ++cases;
                        }
                    }
                }
            }
        }
    }
    printf("{\"capacity\":%u,\"cases\":%u,\"max_runs\":%u,\"max_literals\":%u,\"max_step_runs\":%u,\"run_capacity\":%u,\"literal_capacity\":%u}\n",
        TDMA_FLIGHT_SHORT_SLOT_COUNT,cases,max_runs,max_literals,max_step,
        TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY,TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY);
}

int main(int argc, char **argv)
{
    assert(argc==2 || argc==5);
    if (strcmp(argv[1],"matrix")==0) {configuration_matrix();return 0;}
    unsigned nodes=(unsigned)strtoul(argv[1],NULL,0);
    unsigned mask=argc==5?(unsigned)strtoul(argv[2],NULL,0):(1u<<nodes)-1u;
    unsigned local=argc==5?(unsigned)strtoul(argv[3],NULL,0):0;
    unsigned guard=argc==5?(unsigned)strtoul(argv[4],NULL,0):256;
    tdma_origin_plan_config_t c=make_config(nodes,mask,local,guard);
    tdma_flight_overlay_dma_run_t runs[TDMA_ORIGIN_PLAN_RUN_MAX];
    uint32_t literals[TDMA_ORIGIN_PLAN_LITERAL_MAX];
    tdma_origin_plan_t p={.runs=runs,.literals=literals,
        .run_capacity=TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY,
        .literal_capacity=TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY};
    tdma_origin_plan_builder_t b={0};
    assert(tdma_origin_plan_begin(&b,&c,&p));
    tdma_origin_build_result_t result;
    do {result=tdma_origin_plan_step(&b);}while(result==TDMA_ORIGIN_BUILD_BUSY);
    assert(result==TDMA_ORIGIN_BUILD_DONE);
    printf("{\"nodes\":%u,\"run_capacity\":%u,\"literal_capacity\":%u,\"record_size\":%zu,\"state_size\":%zu,",
        nodes,p.run_capacity,p.literal_capacity,sizeof(tdma_origin_record_t),sizeof(tdma_origin_plan_state_t));
    printf("\"mask\":%u,\"local\":%u,\"guard\":%u,",mask,local,guard);
#define VALUE(name, value) printf("\"" name "\":%u,",(unsigned)(value))
#define OFFSET(member) VALUE(#member,offsetof(tdma_origin_plan_state_t,member))
    printf("\"offsets\":{");
    OFFSET(observation_version); OFFSET(observation_sequence); OFFSET(record_capture_remaining);
    OFFSET(record_flags); OFFSET(record_epoch); OFFSET(record_format); OFFSET(record_time);
    OFFSET(record_sequence_end); OFFSET(record_published_version); OFFSET(record_next_address);
    OFFSET(capture_bank); OFFSET(good_bank); OFFSET(bank_version); OFFSET(local_next_address);
    OFFSET(local_selected_generation); OFFSET(fault); OFFSET(bank_observation);
    printf("\"end\":0},\"registers\":{");
    VALUE("dma",DMA_BASE); VALUE("abort",DMA_BASE+DMA_CHAN_ABORT_OFFSET);
    VALUE("sniff_ctrl",DMA_BASE+DMA_SNIFF_CTRL_OFFSET); VALUE("sniff_data",DMA_BASE+DMA_SNIFF_DATA_OFFSET);
    VALUE("tx",PIO1_BASE); VALUE("rx",PIO2_BASE);
    VALUE("ctrl_tx",b.ctrl_tx); VALUE("ctrl_rx",b.ctrl_rx);
    VALUE("help_tx",b.help_tx); VALUE("help_rx",b.help_rx);
    VALUE("fstat",PIO1_BASE+PIO_FSTAT_OFFSET); VALUE("padout",PIO1_BASE+PIO_DBG_PADOUT_OFFSET);
    VALUE("latch_fifo",PIO1_BASE+PIO_RXF0_OFFSET+8); VALUE("rtt_fifo",PIO1_BASE+PIO_RXF0_OFFSET+4);
    VALUE("latch_shift",sm_reg(PIO1_BASE,2,PIO_SM0_SHIFTCTRL_OFFSET));
    VALUE("latch_instr",sm_reg(PIO1_BASE,2,PIO_SM0_INSTR_OFFSET));
    VALUE("timer_hi",TIMER1_BASE+TIMER_TIMERAWH_OFFSET); VALUE("timer_lo",TIMER1_BASE+TIMER_TIMERAWL_OFFSET);
    VALUE("cap_ctrl",b.cap_ctrl); VALUE("out_ctrl",b.out_ctrl);
    VALUE("sniff_sum",b.sum); VALUE("sniff_crc",b.crc); VALUE("sniff_crc16",b.crc16);
    printf("\"end\":0},\"entries\":{");
    VALUE("seed",p.seed_entry); VALUE("boundary",p.boundary_entry); VALUE("record",p.record_entry);
    VALUE("local",p.local_entry[0]); VALUE("record_done",b.label[L_RECORD_DONE]);
    printf("\"end\":0},\"runs\":[");
    for (unsigned i=0;i<p.run_count;++i) printf("%s[%u,%u,%u,%u]",i?",":"",
        runs[i].control,runs[i].write_address,runs[i].transfer_count,runs[i].read_address);
    printf("],\"literals\":[");
    for (unsigned i=0;i<p.literal_count;++i) printf("%s%u",i?",":"",literals[i]);
    puts("]}");
}
