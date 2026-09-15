#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int uint;
#include "tdma_pio_spi_phys.h"
#include "tdma_pio_spi_origin_workspace.h"

#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "ASSERT %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    fflush(stderr); _Exit(99); } } while (0)

struct host_pio_hw host_pios[3];
typedef struct { uint32_t ctrl; } dma_channel_config;
typedef struct { uint32_t ctrl_trig, read_addr, write_addr, transfer_count, al3_ctrl; } host_dma_channel;
static struct { host_dma_channel ch[16]; uint32_t abort; } host_dma;
#define dma_hw (&host_dma)
static struct { struct { uint32_t dbg_tcr; } ch[16]; } host_dma_debug;
#define dma_debug_hw (&host_dma_debug)
enum { DMA_CH0_CTRL_TRIG_EN_BITS=1, DMA_CH0_CTRL_TRIG_BUSY_BITS=1u<<24, DMA_SIZE_32=2,
       GPIO_FUNC_SIO=5, GPIO_IN=0, clk_sys=0 };
static tdma_pio_spi_phys_t physical;
static tdma_pio_spi_workspace_t s_tdma_pio_spi_workspace;
#define s_tdma_origin (s_tdma_pio_spi_workspace.origin)
static tdma_origin_build_job_t s_tdma_origin_build_job;
static tdma_ring_runtime_config_t config;
static tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
static bool s_tdma_pio_spi_flight_sms_claimed, s_tdma_pio_spi_rx_arm_valid, s_geometry_physical_stopped;
static int s_tdma_pio_spi_tx_dma_channel, s_tdma_pio_spi_rx_dma_channel;
static int s_tdma_pio_spi_command_dma_channel, s_tdma_pio_spi_executor_dma_channel;
static bool dma_claimed[16], gpio_output[64], gpio_pad[64], stop_ok;
static uint32_t sys_hz, tick_hz, starts, enables, installs, pauses, tick_reads;
static uint64_t first_tick, final_tick;
static bool change_clock_at_enable, corrupt_loader_install;
static bool authority=true;
static uint32_t authority_checks;
static bool authorized(void) { ++authority_checks;return authority; }

static uint32_t clock_get_hz(int source) { assert(source == clk_sys); return sys_hz; }
static uint32_t vdc_timestamp_clock_tick_hz(void) { return tick_hz; }
static uint64_t vdc_timestamp_clock_read_ticks64(void) { return tick_reads++ == 0u ? first_tick : final_tick; }
static uint64_t tdma_pio_spi_phys_now_us(void) { return 10u; }
static void __dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static bool gpio_get_out_level(uint pin) { assert(pin < 64u); return gpio_output[pin]; }
static bool gpio_get(uint pin) { assert(pin < 64u); return gpio_pad[pin]; }
static bool dma_channel_is_claimed(uint channel) { assert(channel < 16u); return dma_claimed[channel]; }
static bool pio_sm_is_claimed(PIO pio, uint sm) { assert(sm < 4u); return pio->claimed[sm]; }
static bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm) {
    assert(sm < 4u); const uint32_t bit=1u<<(PIO_FSTAT_TXEMPTY_LSB+sm);
    if(pio->tx_words[sm])pio->fstat &= ~bit;else pio->fstat |= bit;
    return (pio->fstat & bit)!=0u;
}
static bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) {
    assert(sm < 4u); const uint32_t bit=1u<<(PIO_FSTAT_RXEMPTY_LSB+sm);
    if(pio->rx_words[sm])pio->fstat &= ~bit;else pio->fstat |= bit;
    return (pio->fstat & bit)!=0u;
}
static uint pio_sm_get_pc(PIO pio, uint sm) { assert(sm < 4u); return pio->pc[sm]; }
static bool pio_interrupt_get(PIO pio, uint irq) { assert(irq == 1u); return pio->irq; }
static void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) {
    assert(sm < 4u); if (enabled) { pio->ctrl |= 1u << sm; ++enables; }
    else pio->ctrl &= ~(1u << sm);
}
static void pio_sm_clear_fifos(PIO pio, uint sm) { pio->tx_words[sm] = pio->rx_words[sm] = 0u; }
static void tdma_pio_spi_phys_pause_sm_pair(tdma_pio_spi_phys_t *phys) {
    (void)phys; ++pauses; BOARD_TDMA_TX_PIO->ctrl = BOARD_TDMA_RX_PIO->ctrl = 0u;
}
static void tdma_pio_spi_phys_set_line_drivers(bool enabled) {
    gpio_output[BOARD_UP_BISS_DE_PIN] = gpio_output[BOARD_DN_BISS_DE_PIN] = gpio_output[BOARD_TRIG_DE_PIN] = enabled;
    if (enabled && change_clock_at_enable) ++sys_hz;
}
static void tdma_pio_spi_phys_disable_dma_mask(uint32_t mask) {
    for (uint i=0u; i<16u; ++i) if (mask & (1u<<i)) dma_hw->ch[i].ctrl_trig &= ~DMA_CH0_CTRL_TRIG_EN_BITS;
}
static dma_channel_config dma_channel_get_default_config(uint channel) {
    assert(channel < 16u); return (dma_channel_config){.ctrl=DMA_CH0_CTRL_TRIG_EN_BITS | (channel<<11)};
}
static void channel_config_set_transfer_data_size(dma_channel_config *c, uint n) { c->ctrl |= n<<2; }
static void channel_config_set_read_increment(dma_channel_config *c, bool on) { if(on)c->ctrl |= 1u<<4; }
static void channel_config_set_write_increment(dma_channel_config *c, bool on) { if(on)c->ctrl |= 1u<<5; }
static void channel_config_set_ring(dma_channel_config *c, bool write, uint n) { assert(write);c->ctrl |= n<<6; }
static void channel_config_set_high_priority(dma_channel_config *c, bool on) { if(on)c->ctrl |= 1u<<1; }
static void channel_config_set_chain_to(dma_channel_config *c, uint n) { c->ctrl = (c->ctrl & ~(15u<<11)) | n<<11; }
static void channel_config_set_enable(dma_channel_config *c, bool on) {
    if(on)c->ctrl |= DMA_CH0_CTRL_TRIG_EN_BITS;else c->ctrl &= ~DMA_CH0_CTRL_TRIG_EN_BITS;
}
static void dma_channel_set_config(uint channel,const dma_channel_config *c,bool trigger) {
    /* The SDK uses AL1_CTRL for false. A CTRL_TRIG enable is an early launch. */
    assert(!trigger);dma_hw->ch[channel].ctrl_trig=c->ctrl;
}
static void dma_channel_configure(uint channel,const dma_channel_config *c,void *to,const void *from,uint32_t count,bool trigger) {
    assert(!trigger && !(c->ctrl & DMA_CH0_CTRL_TRIG_EN_BITS)); ++installs;
    dma_hw->ch[channel].ctrl_trig=c->ctrl; dma_hw->ch[channel].read_addr=(uint32_t)(uintptr_t)from;
    dma_hw->ch[channel].write_addr=(uint32_t)(uintptr_t)to;
    /* RP2350 TRANS_COUNT writes update RELOAD, not the live remaining counter.
     * SDK dma.h configure(false) performs no trigger. DBG_TCR reads RELOAD. */
    dma_debug_hw->ch[channel].dbg_tcr=count;
    if(corrupt_loader_install)++dma_debug_hw->ch[channel].dbg_tcr;
}
static void dma_start_channel_mask(uint32_t mask) {
    assert(mask == 1u<<BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL);
    assert(dma_hw->ch[BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL].ctrl_trig & DMA_CH0_CTRL_TRIG_EN_BITS);
    assert(physical.flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_FAILED);
    assert(gpio_output[BOARD_TRIG_DE_PIN] && final_tick < 1000u); ++starts;
    /* A trigger is the sole operation that copies RELOAD to the live count. */
    dma_hw->ch[BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL].transfer_count=
        dma_debug_hw->ch[BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL].dbg_tcr;
}
static void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys) { phys->snapshot.armed=phys->armed; }

/* Complete common STOP is compiled. Actual DMA retirement and resource
 * release are controlled seams; failed retirement must preserve ownership. */
static bool tdma_pio_spi_phys_stop_command_dma(tdma_pio_spi_phys_t *phys);
static bool tdma_pio_spi_phys_stop_dma_chain(uint32_t loader,uint32_t executor,uint32_t children,uint64_t deadline) {
    assert(loader==(1u<<BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL));
    assert(executor==(1u<<BOARD_TDMA_ORIGIN_EXECUTOR_DMA_CHANNEL));
    assert(children==((1u<<BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL)|(1u<<BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL)));
    assert(deadline==10u+TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US); if(!stop_ok)return false;
    tdma_pio_spi_phys_disable_dma_mask(loader|executor|children); dma_hw->abort=0u; return true;
}
static void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *phys) { (void)phys; }
static void tdma_geometry_stop_begin(tdma_pio_spi_phys_t *phys) { (void)phys; }
bool tdma_overlay_prepare_cancel(tdma_overlay_prepare_t *job) { (void)job; return true; }
static bool tdma_pio_spi_phys_rx_scan_cancel(tdma_pio_spi_phys_t *phys) { (void)phys; return true; }
static bool tdma_pio_spi_phys_restore_clock_latch(tdma_pio_spi_phys_t *phys,bool rearm) { (void)phys; assert(!rearm); return true; }
static bool tdma_pio_spi_phys_is_flight_persona(void) { return true; }
static void tdma_pio_spi_phys_prepare_sm_pair(tdma_pio_spi_phys_t *phys) {
    tdma_pio_spi_phys_pause_sm_pair(phys);
    for(uint sm=0u;sm<4u;++sm) { pio_sm_clear_fifos(BOARD_TDMA_TX_PIO,sm);pio_sm_clear_fifos(BOARD_TDMA_RX_PIO,sm); }
}
static void gpio_set_function(uint pin,uint function) { (void)pin;assert(function==GPIO_FUNC_SIO); }
static void gpio_set_dir(uint pin,bool out) { (void)pin;assert(!out); }
static void tdma_pio_spi_phys_clk_train_reset(tdma_pio_spi_phys_t *phys) { (void)phys; }
static void tdma_pio_spi_phys_release_flight_resources(tdma_pio_spi_phys_t *phys) {
    if(tdma_pio_spi_phys_stop_command_dma(phys)) phys->flight_resource_claimed=phys->flight_origin_resource_claimed=false;
}
static void tdma_rx_start_cut_disarmed(void) { }
tdma_origin_build_result_t tdma_origin_plan_step(tdma_origin_plan_builder_t *builder) { (void)builder;return TDMA_ORIGIN_BUILD_FAILED; }
void tdma_origin_plan_cancel(tdma_origin_plan_builder_t *builder) { builder->active=builder->complete=false;builder->failed=true; }
#include "origin_launch_impl.inc"

static void setup(void) {
    memset(&physical,0,sizeof(physical));memset(&s_tdma_origin,0,sizeof(s_tdma_origin));
    memset(host_pios,0,sizeof(host_pios));memset(&host_dma,0,sizeof(host_dma));
    memset(&host_dma_debug,0,sizeof(host_dma_debug));
    memset(gpio_output,0,sizeof(gpio_output));memset(gpio_pad,0,sizeof(gpio_pad));
    memset(&s_tdma_origin_build_job,0,sizeof(s_tdma_origin_build_job));
    for(uint i=0u;i<16u;++i)dma_claimed[i]=true;
    for(uint sm=0u;sm<4u;++sm)BOARD_TDMA_TX_PIO->claimed[sm]=BOARD_TDMA_RX_PIO->claimed[sm]=true;
    BOARD_TDMA_TX_PIO->fstat=BOARD_TDMA_RX_PIO->fstat=0x0f000f00u;
    sys_hz=tick_hz=250000000u;starts=enables=installs=pauses=tick_reads=0u;
    first_tick=final_tick=100u;change_clock_at_enable=false;corrupt_loader_install=false;stop_ok=true;
    authority=true;authority_checks=0u;
    s_tdma_pio_spi_flight_sms_claimed=true;
    s_tdma_pio_spi_program_persona=TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN;
    s_tdma_pio_spi_tx_dma_channel=BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL;
    s_tdma_pio_spi_rx_dma_channel=BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL;
    s_tdma_pio_spi_command_dma_channel=BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL;
    s_tdma_pio_spi_executor_dma_channel=BOARD_TDMA_ORIGIN_EXECUTOR_DMA_CHANNEL;
    physical.flight_resources=tdma_state_machine_resource_contract();
    physical.tx_sm=BOARD_TDMA_RX_DATA_FLIGHT_SM;physical.rx_sm=BOARD_TDMA_TX_DATA_CAPTURE_SM;
    physical.tx_sck_pin=BOARD_TDMA_TX_CLK_OUT_PIN;physical.tx_csn_pin=BOARD_TDMA_TX_SYNC_OUT_PIN;
    physical.tx_pin=BOARD_TDMA_RX_DATA_OUT_PIN;physical.rx_pin=BOARD_TDMA_TX_DATA_IN_PIN;
    physical.rx_sck_pin=BOARD_TDMA_RX_CLK_IN_PIN;physical.rx_csn_pin=BOARD_TDMA_RX_SYNC_IN_PIN;
    physical.flight_origin_workspace_owned=physical.flight_resource_claimed=physical.flight_origin_resource_claimed=true;
    physical.process_image_enabled=true;physical.role=TDMA_PIO_SPI_ROLE_MASTER;
    physical.baud_hz=12000000u;physical.node_count=4u;physical.flight_payload_size=160u;
    physical.flight_physical_byte_count=256u;
    physical.flight_marker_phase_delay_cycles=physical.flight_sck_phase_delay_cycles=2u;
    physical.flight_data_phase_delay_cycles=physical.flight_origin_capture_phase_delay_cycles=3u;
    physical.flight_origin_cadence=(tdma_origin_cadence_t){256,100,10000,10001,5000};
    config=(tdma_ring_runtime_config_t){.enabled=1u,.baud_hz=physical.baud_hz,.node_count=physical.node_count,
        .cycle_period_ns=1500000u,.owner_config_seq=77u};
    physical.flight_origin_prepare=(tdma_origin_prepare_t){.config=config,.live_config=&config,
        .stage=TDMA_ORIGIN_PREPARE_INSTALL,.clk_sys_hz=sys_hz,.prefix_bits=TDMA_PIO_SPI_PACKET_HEADER_SIZE*8u,
        .cadence=physical.flight_origin_cadence,.physical_bytes=physical.flight_physical_byte_count,
        .marker_phase=2u,.sck_phase=2u,.data_phase=3u,.capture_phase=3u,
        .packet_size=TDMA_TRANSPORT_FRAME_HEADER_SIZE+physical.flight_payload_size};
    BOARD_TDMA_TX_PIO->pc[BOARD_TDMA_TX_CONTROL_OUT_SM]=TDMA_ORIGIN_CONTROL_PC;
    BOARD_TDMA_TX_PIO->pc[BOARD_TDMA_TX_DATA_CAPTURE_SM]=TDMA_ORIGIN_CAPTURE_PC;
    BOARD_TDMA_TX_PIO->pc[BOARD_TDMA_TX_RTT_EVIDENCE_SM]=TDMA_ORIGIN_RTT_PC;
    BOARD_TDMA_TX_PIO->pc[BOARD_TDMA_TX_CLOCK_LATCH_SM]=TDMA_ORIGIN_LATCH_PC;
    BOARD_TDMA_RX_PIO->pc[BOARD_TDMA_RX_DATA_FLIGHT_SM]=TDMA_ORIGIN_DATA_PC;
    BOARD_TDMA_RX_PIO->pc[BOARD_TDMA_ORIGIN_HELPER_SM]=TDMA_ORIGIN_HELPER_PC;
    gpio_pad[physical.tx_csn_pin]=gpio_pad[physical.rx_csn_pin]=true;
    const uint32_t entry=(uint32_t)(uintptr_t)s_tdma_origin.runs;
    s_tdma_origin.plan=(tdma_origin_plan_t){.runs=s_tdma_origin.runs,.literals=s_tdma_origin.literals,
        .run_capacity=TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY,.literal_capacity=TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY,
        .run_count=10u,.literal_count=8u,.seed_entry=entry,.boundary_entry=entry,.fault_entry=entry,
        .local_entry={entry,entry},.record_entry=entry};
}
static void ready(void) {
    const tdma_origin_build_result_t result=tdma_pio_spi_phys_origin_poll(&physical);
    if(result!=TDMA_ORIGIN_BUILD_BUSY)fprintf(stderr,"INSTALL reject: %u,%u,%u\n",
        physical.flight_origin_prepare.reject_code,physical.flight_origin_prepare.reject_observed,
        physical.flight_origin_prepare.reject_expected);
    assert(result==TDMA_ORIGIN_BUILD_BUSY);
    assert(physical.flight_origin_prepare.stage==TDMA_ORIGIN_PREPARE_READY);
    assert(tdma_pio_spi_phys_origin_prepare_ready(&physical));
    assert(installs==1u && starts==0u && enables==0u && !physical.armed);
}
static void no_launch(void) {
    assert(starts==0u && !physical.armed && !gpio_output[BOARD_TRIG_DE_PIN]);
    assert(physical.flight_origin_workspace_owned);
}
static void mutate(uint n) {
    tdma_state_machine_resource_contract_t *r=&physical.flight_resources;
    switch(n) {
    case 0:++config.owner_config_seq;break;
    case 1:++sys_hz;break;
    case 2:++tick_hz;break;
    case 3:physical.flight_origin_prepare.live_config=NULL;break;
    case 4:++physical.flight_payload_size;break;
    case 5:++physical.flight_physical_byte_count;break;
    case 6:++physical.flight_marker_phase_delay_cycles;break;
    case 7:++physical.flight_sck_phase_delay_cycles;break;
    case 8:++physical.flight_data_phase_delay_cycles;break;
    case 9:++physical.flight_origin_capture_phase_delay_cycles;break;
    case 10:++physical.flight_alignment_byte_shift;break;
    case 11:++physical.flight_alignment_bit_shift;break;
    case 12:++physical.flight_origin_cadence.divider256;break;
    case 13:++physical.flight_origin_cadence.guard_count;break;
    case 14:++physical.flight_origin_cadence.period_floor_ticks;break;
    case 15:++physical.flight_origin_cadence.period_ceiling_ticks;break;
    case 16:++physical.flight_origin_cadence.guard_floor_ticks;break;
    case 17:++physical.baud_hz;break;
    case 18:++physical.node_count;break;
    case 19:++physical.flight_local_slot_id;break;
    case 20:physical.process_image_enabled=false;break;
    case 21:physical.role=TDMA_PIO_SPI_ROLE_SLAVE;break;
    case 22:physical.flight_resource_claimed=false;break;
    case 23:physical.flight_origin_resource_claimed=false;break;
    case 24:s_tdma_pio_spi_flight_sms_claimed=false;break;
    case 25:s_tdma_pio_spi_program_persona=TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN;break;
    case 26:++s_tdma_pio_spi_tx_dma_channel;break;
    case 27:++s_tdma_pio_spi_rx_dma_channel;break;
    case 28:++s_tdma_pio_spi_command_dma_channel;break;
    case 29:++s_tdma_pio_spi_executor_dma_channel;break;
    case 30:r->tx_pio=pio0;break;
    case 31:r->rx_pio=pio0;break;
    case 32:++r->tx_control_out_sm;break;
    case 33:++r->tx_data_capture_sm;break;
    case 34:++r->tx_rtt_evidence_sm;break;
    case 35:++r->tx_clock_latch_sm;break;
    case 36:++r->rx_reserved_control_sm;break;
    case 37:++r->rx_reserved_evidence_sm;break;
    case 38:++r->rx_data_flight_sm;break;
    case 39:++r->rx_clock_latch_sm;break;
    case 40:++r->rx_endpoints.data_output.sm;break;
    case 41:++r->rx_endpoints.data_unload.owner;break;
    case 42:++r->rx_endpoints.clock_evidence.dma_channel;break;
    case 43:++r->rx_endpoints.business_rx_consumer_count;break;
    case 44:++physical.tx_sm;break;
    case 45:++physical.rx_sm;break;
    case 46:++physical.tx_pin;break;
    case 47:++physical.rx_pin;break;
    case 48:++physical.tx_sck_pin;break;
    case 49:++physical.tx_csn_pin;break;
    case 50:++physical.rx_sck_pin;break;
    case 51:++physical.rx_csn_pin;break;
    case 52:dma_claimed[BOARD_TDMA_ORIGIN_EXECUTOR_DMA_CHANNEL]=false;break;
    case 53:BOARD_TDMA_TX_PIO->claimed[0]=false;break;
    case 54:BOARD_TDMA_RX_PIO->claimed[0]=false;break;
    case 55:BOARD_TDMA_TX_PIO->ctrl=1u;break;
    case 56:BOARD_TDMA_RX_PIO->ctrl=1u;break;
    case 57:gpio_output[BOARD_UP_BISS_DE_PIN]=true;break;
    case 58:gpio_output[BOARD_DN_BISS_DE_PIN]=true;break;
    case 59:gpio_output[BOARD_TRIG_DE_PIN]=true;break;
    case 60:gpio_pad[physical.tx_csn_pin]=false;break;
    case 61:gpio_pad[physical.rx_csn_pin]=false;break;
    case 62:gpio_pad[physical.tx_sck_pin]=true;break;
    case 63:dma_hw->abort=1u<<BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL;break;
    case 64:dma_hw->ch[BOARD_TDMA_ORIGIN_EXECUTOR_DMA_CHANNEL].ctrl_trig=DMA_CH0_CTRL_TRIG_EN_BITS;break;
    case 65:dma_hw->ch[BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL].ctrl_trig=DMA_CH0_CTRL_TRIG_BUSY_BITS;break;
    case 66:BOARD_TDMA_TX_PIO->tx_words[0]=1u;break;
    case 67:BOARD_TDMA_TX_PIO->rx_words[0]=1u;break;
    case 68:BOARD_TDMA_RX_PIO->tx_words[0]=1u;break;
    case 69:BOARD_TDMA_RX_PIO->rx_words[0]=1u;break;
    case 70:++BOARD_TDMA_TX_PIO->pc[BOARD_TDMA_TX_CONTROL_OUT_SM];break;
    case 71:++BOARD_TDMA_RX_PIO->pc[BOARD_TDMA_RX_DATA_FLIGHT_SM];break;
    case 72:++s_tdma_origin.plan.seed_entry;break;
    case 73:s_tdma_origin.plan.run_count=0u;break;
    case 74:++dma_hw->ch[BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL].read_addr;break;
    case 75:++dma_hw->ch[BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL].write_addr;break;
    case 76:++dma_debug_hw->ch[BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL].dbg_tcr;break;
    case 77:s_tdma_origin.state.fault=1u;break;
    case 78:BOARD_TDMA_TX_PIO->irq=true;break;
    case 79:physical.flight_tx_pending=true;break;
    default:assert(false);
    }
}
static uint32_t mutation_code(uint n) {
#define REJECT(reason, detail) TDMA_ORIGIN_REJECT_CODE(TDMA_ORIGIN_REJECT_##reason, detail)
    static const uint32_t codes[] = {
        REJECT(CONFIG,2), REJECT(CONFIG,3), REJECT(CONFIG,4), REJECT(CONFIG,1),
        REJECT(CONFIG,11), REJECT(CONFIG,12), REJECT(CONFIG,13), REJECT(CONFIG,14),
        REJECT(CONFIG,15), REJECT(CONFIG,16), REJECT(CONFIG,18), REJECT(CONFIG,18),
        REJECT(CONFIG,19), REJECT(CONFIG,20), REJECT(CONFIG,21), REJECT(CONFIG,22),
        REJECT(CONFIG,23), REJECT(CONFIG,8), REJECT(CONFIG,9), REJECT(CONFIG,10),
        REJECT(CONFIG,6), REJECT(CONFIG,7),
        REJECT(RESOURCE_LEASE,3), REJECT(RESOURCE_LEASE,2), REJECT(RESOURCE_LEASE,4), REJECT(PERSONA,0),
        REJECT(DMA_BINDING,BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL),
        REJECT(DMA_BINDING,BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL),
        REJECT(DMA_BINDING,BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL),
        REJECT(DMA_BINDING,BOARD_TDMA_ORIGIN_EXECUTOR_DMA_CHANNEL),
        REJECT(BOARD_BINDING,1), REJECT(BOARD_BINDING,2), REJECT(BOARD_BINDING,3), REJECT(BOARD_BINDING,6),
        REJECT(BOARD_BINDING,4), REJECT(BOARD_BINDING,5), REJECT(BOARD_BINDING,7), REJECT(BOARD_BINDING,8),
        REJECT(BOARD_BINDING,9), REJECT(BOARD_BINDING,10),
        REJECT(RX_CONTRACT,0), REJECT(RX_CONTRACT,0), REJECT(RX_CONTRACT,0), REJECT(RX_CONTRACT,0),
        REJECT(PHYSICAL_BINDING,1), REJECT(PHYSICAL_BINDING,2), REJECT(PHYSICAL_BINDING,5), REJECT(PHYSICAL_BINDING,6),
        REJECT(PHYSICAL_BINDING,3), REJECT(PHYSICAL_BINDING,4), REJECT(PHYSICAL_BINDING,7), REJECT(PHYSICAL_BINDING,8),
        REJECT(DMA_CLAIM,BOARD_TDMA_ORIGIN_EXECUTOR_DMA_CHANNEL),
        REJECT(SM_CLAIM,0), REJECT(SM_CLAIM,256), REJECT(SM_ENABLED,0), REJECT(SM_ENABLED,256),
        REJECT(DRIVER,BOARD_UP_BISS_DE_PIN), REJECT(DRIVER,BOARD_DN_BISS_DE_PIN), REJECT(DRIVER,BOARD_TRIG_DE_PIN),
        REJECT(PAD,BOARD_TDMA_TX_SYNC_OUT_PIN), REJECT(PAD,BOARD_TDMA_RX_SYNC_IN_PIN), REJECT(PAD,BOARD_TDMA_TX_CLK_OUT_PIN),
        REJECT(DMA_ABORT,BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL),
        REJECT(DMA_CONTROL,BOARD_TDMA_ORIGIN_EXECUTOR_DMA_CHANNEL),
        REJECT(DMA_CONTROL,BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL),
        REJECT(FIFO,0), REJECT(FIFO,1), REJECT(FIFO,256), REJECT(FIFO,257),
        REJECT(PC,BOARD_TDMA_TX_CONTROL_OUT_SM<<1), REJECT(PC,256|(BOARD_TDMA_RX_DATA_FLIGHT_SM<<1)),
        REJECT(PLAN_ENTRY,0), REJECT(PLAN_COUNT,1), REJECT(LOADER,2), REJECT(LOADER,3), REJECT(LOADER,4),
        REJECT(FAULT,0), REJECT(IRQ,1), REJECT(SOFTWARE,4)
    };
#undef REJECT
    assert(n<sizeof(codes)/sizeof(codes[0]));return codes[n];
}
static void rejection(tdma_origin_reject_reason_t reason,uint detail,uint32_t observed,uint32_t expected) {
    assert(physical.flight_origin_prepare.reject_code==TDMA_ORIGIN_REJECT_CODE(reason,detail));
    assert(physical.flight_origin_prepare.reject_observed==observed);
    assert(physical.flight_origin_prepare.reject_expected==expected);
}
int main(int argc,char **argv) {
    assert(argc==2);setup();
    if(!strcmp(argv[1],"hold")) {
        ready();for(uint i=0;i<500u;++i)assert(tdma_pio_spi_phys_origin_poll(&physical)==TDMA_ORIGIN_BUILD_BUSY);
        no_launch();assert(enables==0u && installs==1u && tdma_pio_spi_phys_origin_prepare_ready(&physical));
        assert(physical.flight_origin_prepare.reject_code==TDMA_ORIGIN_REJECT_NONE);
    } else if(!strcmp(argv[1],"release")) {
        ready();assert(tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));assert(starts==1u && physical.armed);
        assert(physical.flight_origin_prepare.stage==TDMA_ORIGIN_PREPARE_COMPLETE && !physical.flight_origin_prepare.live_config);
        assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));assert(starts==1u);
        assert(tdma_pio_spi_phys_origin_poll(&physical)==TDMA_ORIGIN_BUILD_DONE);
    } else if(!strcmp(argv[1],"expiry") || !strcmp(argv[1],"expiry_during_enable") || !strcmp(argv[1],"clock_during_enable")) {
        ready();if(!strcmp(argv[1],"expiry"))first_tick=1000u;
        else if(!strcmp(argv[1],"expiry_during_enable"))final_tick=1000u;
        else change_clock_at_enable=true;
        assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));no_launch();
        assert(physical.flight_origin_prepare.stage==TDMA_ORIGIN_PREPARE_FAILED);
        assert(!(dma_hw->ch[BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL].ctrl_trig & DMA_CH0_CTRL_TRIG_EN_BITS));
        if(change_clock_at_enable)rejection(TDMA_ORIGIN_REJECT_RELEASE_CLOCK,1u,250000001u,250000000u);
        else rejection(TDMA_ORIGIN_REJECT_EXPIRY,first_tick==1000u?1u:2u,1000u,1000u);
    } else if(!strcmp(argv[1],"stop") || !strcmp(argv[1],"stop_retry")) {
        ready();if(!strcmp(argv[1],"stop_retry")) { stop_ok=false;assert(!tdma_pio_spi_phys_disarm(&physical));
            assert(physical.flight_origin_workspace_owned && physical.flight_resource_claimed);stop_ok=true; }
        assert(tdma_pio_spi_phys_disarm(&physical));assert(!physical.flight_origin_workspace_owned && !physical.flight_resource_claimed);
        assert(physical.flight_origin_prepare.stage==TDMA_ORIGIN_PREPARE_IDLE);
        assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));assert(starts==0u);
    } else if(!strcmp(argv[1],"skip_records")) {
        physical.flight_origin_prepare.diagnostic_skip_records=true;s_tdma_origin.plan.record_entry=0u;
        ready();assert(tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));assert(starts==1u);
    } else if(!strcmp(argv[1],"install_dirty")) {
        BOARD_TDMA_TX_PIO->tx_words[0]=1u;assert(tdma_pio_spi_phys_origin_poll(&physical)==TDMA_ORIGIN_BUILD_FAILED);
        no_launch();assert(installs==0u && BOARD_TDMA_TX_PIO->tx_words[0]==1u);
        rejection(TDMA_ORIGIN_REJECT_FIFO,0u,0x0e000f00u,1u<<PIO_FSTAT_TXEMPTY_LSB);
    } else if(!strcmp(argv[1],"null")) {
        assert(!tdma_pio_spi_phys_origin_prepare_ready(NULL));assert(!tdma_pio_spi_phys_origin_release(NULL,1000u,authorized));
        assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));no_launch();
        rejection(TDMA_ORIGIN_REJECT_STAGE,1u,TDMA_ORIGIN_PREPARE_INSTALL,TDMA_ORIGIN_PREPARE_READY);
        setup();ready();assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,NULL));no_launch();
        rejection(TDMA_ORIGIN_REJECT_AUTHORITY,0u,0u,1u);
    } else if(!strcmp(argv[1],"revoke")) {
        ready();authority=false;assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));
        no_launch();assert(authority_checks==1u && enables==2u);
        rejection(TDMA_ORIGIN_REJECT_AUTHORITY,1u,0u,1u);
        assert(tdma_pio_spi_phys_disarm(&physical));assert(!physical.flight_origin_workspace_owned);
    } else if(!strcmp(argv[1],"stop_after_trigger")) {
        ready();assert(tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));authority=false;
        assert(authority_checks==1u && starts==1u && physical.armed);
        assert(tdma_pio_spi_phys_disarm(&physical));assert(!physical.armed && !physical.flight_origin_workspace_owned);
        assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));assert(starts==1u);
    } else if(!strcmp(argv[1],"phase_numbers")) {
        assert(TDMA_ORIGIN_PREPARE_INSTALL==8 && TDMA_ORIGIN_PREPARE_COMPLETE==9 && TDMA_ORIGIN_PREPARE_FAILED==10);
        assert(TDMA_ORIGIN_PREPARE_READY==11 && TDMA_ORIGIN_HANDOFF_STAGES==12 && TDMA_ORIGIN_HANDOFF_SCHEMA==2);
    } else if(!strcmp(argv[1],"first_reject")) {
        ready();BOARD_TDMA_RX_PIO->pc[BOARD_TDMA_RX_DATA_FLIGHT_SM]=7u;
        assert(!tdma_pio_spi_phys_origin_prepare_ready(&physical));
        rejection(TDMA_ORIGIN_REJECT_PC,256u|(BOARD_TDMA_RX_DATA_FLIGHT_SM<<1),7u,TDMA_ORIGIN_DATA_PC);
        /* Later faults and cleanup must not replace the first observation. */
        BOARD_TDMA_TX_PIO->ctrl=1u;gpio_pad[physical.tx_csn_pin]=false;
        assert(tdma_pio_spi_phys_origin_poll(&physical)==TDMA_ORIGIN_BUILD_FAILED);
        assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));
        rejection(TDMA_ORIGIN_REJECT_PC,256u|(BOARD_TDMA_RX_DATA_FLIGHT_SM<<1),7u,TDMA_ORIGIN_DATA_PC);
        assert(tdma_pio_spi_phys_disarm(&physical));
        assert(physical.flight_origin_prepare.reject_code==0u);
        assert(physical.flight_origin_prepare.reject_observed==0u && physical.flight_origin_prepare.reject_expected==0u);
    } else if(!strcmp(argv[1],"builder_reject")) {
        s_tdma_origin_build_job.state=TDMA_ORIGIN_JOB_BUILDING;
        assert(tdma_pio_spi_phys_origin_poll(&physical)==TDMA_ORIGIN_BUILD_FAILED);
        rejection(TDMA_ORIGIN_REJECT_BUILDER,0u,TDMA_ORIGIN_JOB_BUILDING,TDMA_ORIGIN_JOB_IDLE);no_launch();
    } else if(!strcmp(argv[1],"loader_reject")) {
        ready();const uint channel=BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL;
        const uint32_t before=dma_hw->ch[channel].ctrl_trig;
        dma_hw->ch[channel].ctrl_trig ^= 1u<<2;
        assert(tdma_pio_spi_phys_origin_poll(&physical)==TDMA_ORIGIN_BUILD_FAILED);
        rejection(TDMA_ORIGIN_REJECT_LOADER,1u,before^(1u<<2),before);no_launch();
    } else if(!strcmp(argv[1],"post_install_reject")) {
        corrupt_loader_install=true;
        assert(tdma_pio_spi_phys_origin_poll(&physical)==TDMA_ORIGIN_BUILD_FAILED);
        assert(installs==1u);
        rejection(TDMA_ORIGIN_REJECT_LOADER,4u,5u,4u);no_launch();
    } else if(!strcmp(argv[1],"zero_expiry")) {
        ready();assert(!tdma_pio_spi_phys_origin_release(&physical,0u,authorized));
        rejection(TDMA_ORIGIN_REJECT_EXPIRY,0u,0u,1u);assert(tick_reads==0u);no_launch();
    } else if(!strcmp(argv[1],"reload_before_trigger") || !strcmp(argv[1],"reload_with_residual_count")) {
        const uint channel=BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL;
        const uint32_t previous=!strcmp(argv[1],"reload_with_residual_count")?3u:0u;
        dma_hw->ch[channel].transfer_count=previous;
        ready();assert(dma_debug_hw->ch[channel].dbg_tcr==4u);
        assert(dma_hw->ch[channel].transfer_count==previous);
        for(uint i=0u;i<20u;++i)assert(tdma_pio_spi_phys_origin_poll(&physical)==TDMA_ORIGIN_BUILD_BUSY);
        assert(dma_hw->ch[channel].transfer_count==previous);no_launch();
        assert(physical.flight_origin_prepare.reject_code==0u);
        assert(tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));
        assert(starts==1u && dma_hw->ch[channel].transfer_count==4u);
    } else if(!strncmp(argv[1],"reload_",7)) {
        const uint channel=BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL;
        ready();uint32_t bad_reload;
        if(!strcmp(argv[1],"reload_zero"))bad_reload=0u;
        else if(!strcmp(argv[1],"reload_short"))bad_reload=3u;
        else if(!strcmp(argv[1],"reload_long"))bad_reload=5u;
        else if(!strcmp(argv[1],"reload_self_trigger"))bad_reload=0x10000004u;
        else if(!strcmp(argv[1],"reload_endless"))bad_reload=0xf0000004u;
        else { assert(false);bad_reload=0u; }
        dma_debug_hw->ch[channel].dbg_tcr=bad_reload;
        /* A stale live count equal to four must not mask a bad next transfer. */
        dma_hw->ch[channel].transfer_count=4u;
        assert(!tdma_pio_spi_phys_origin_prepare_ready(&physical));
        rejection(TDMA_ORIGIN_REJECT_LOADER,4u,bad_reload,4u);
        assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));no_launch();
        assert(enables==0u && dma_hw->ch[channel].transfer_count==4u);
        rejection(TDMA_ORIGIN_REJECT_LOADER,4u,bad_reload,4u);
    } else if(!strncmp(argv[1],"mutation_",9)) {
        const uint n=(uint)strtoul(argv[1]+9,NULL,10);ready();mutate(n);
        assert(!tdma_pio_spi_phys_origin_prepare_ready(&physical));
        assert(physical.flight_origin_prepare.reject_code==mutation_code(n));
        const uint32_t observed=physical.flight_origin_prepare.reject_observed;
        const uint32_t expected=physical.flight_origin_prepare.reject_expected;
        assert(!tdma_pio_spi_phys_origin_release(&physical,1000u,authorized));no_launch();
        assert(physical.flight_origin_prepare.stage==TDMA_ORIGIN_PREPARE_FAILED);
        assert(physical.flight_origin_prepare.reject_code==mutation_code(n));
        assert(physical.flight_origin_prepare.reject_observed==observed && physical.flight_origin_prepare.reject_expected==expected);
        setup();ready();mutate(n);assert(tdma_pio_spi_phys_origin_poll(&physical)==TDMA_ORIGIN_BUILD_FAILED);no_launch();
        assert(physical.flight_origin_prepare.reject_code==mutation_code(n));
    } else assert(false);
    printf("%s: passed\n",argv[1]);return 0;
}
