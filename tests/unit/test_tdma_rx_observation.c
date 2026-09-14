#include "tdma_rx_sequence.h"
#include "tdma_transport_frame.h"
#include "tdma_rx_scan.h"
#include "tdma_rx_capture.h"
#include "tdma_service_timing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { TDMA_PIO_SPI_RX_RING_WORDS = 1024,
    TDMA_PIO_SPI_RX_DMA_WORD_MAX = 4 + TDMA_TRANSPORT_SHORT_PACKET_MAX,
    TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES = 2,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN = 16,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER = 3,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER = 2,
    DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF = 1, DMA_CH0_TRANS_COUNT_MODE_LSB = 28 };
#define DMA_CH0_TRANS_COUNT_MODE_BITS 0xf0000000u
#define DMA_CH0_TRANS_COUNT_COUNT_BITS 0x0fffffffu
typedef struct {
    bool rx_capture_active, process_image_enabled, flight_overlay_alignment_locked;
    bool flight_origin_workspace_owned;
    bool armed;
    uint32_t role, baud_hz;
    tdma_rx_scan_t *rx_scan_preparation;
    tdma_rx_scan_hint_t rx_scan_hint;
    uint32_t flight_physical_byte_count, flight_alignment_byte_shift, flight_alignment_bit_shift;
    uint32_t flight_tail_bytes;
    uint32_t flight_overlay_alignment_samples;
    uint64_t flight_overlay_alignment_candidate;
    struct {
        uint32_t rx_dma_produced_words, rx_scan_produced_words, rx_dma_write_index, rx_dma_channel;
        uint32_t rx_ring_overrun_count, rx_magic_at_zero, rx_magic_at_shift, rx_magic_fail_count;
        uint32_t last_bad_header0, last_bad_header1, last_bad_header2, last_bad_header3, last_bad_words;
        uint32_t rx_observation_drop_count, rx_scan_yield_count;
        uint32_t rx_count, last_rx_size, rx_edge_count, last_error;
        uint64_t last_rx_timestamp_ns, last_rx_edge_timestamp_ns, last_rx_extract_timestamp_ns;
    } snapshot;
} tdma_pio_spi_phys_t;
static struct { struct { uint32_t transfer_count; } ch[16]; } hardware;
#define dma_hw (&hardware)
static int s_tdma_pio_spi_rx_dma_channel = 4;
static uint32_t s_tdma_pio_spi_program_persona;
static tdma_rx_dma_counter_t s_tdma_pio_spi_rx_sequence;
static uint64_t s_tdma_pio_spi_rx_scan_produced;
#if TDMA_TEST_ASYNC_RX
static uint64_t s_tdma_pio_spi_rx_arm_epoch, s_tdma_pio_spi_rx_capture_id;
static bool s_tdma_pio_spi_rx_arm_valid;
#endif
static uint32_t ring[TDMA_PIO_SPI_RX_RING_WORDS];
static uint8_t s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_RX_DMA_WORD_MAX];
static uint64_t tick, completed, packet_start;
static uint32_t stimulus, word_reads, clock_reads;
static bool copy_phase, stimulus_ran;
static uint64_t vdc_timestamp_clock_read_ticks64(void) { ++clock_reads; tick += 4; return tick; }
static void __dmb(void) {}
static uint32_t __rev(uint32_t v) {
    uint32_t out = 0;
    for (unsigned i = 0; i < 32; ++i) { out = (out << 1) | (v & 1); v >>= 1; }
    return out;
}
static void publish_count(void) {
    hardware.ch[4].transfer_count = (1u << 28) |
        (s_tdma_pio_spi_rx_sequence.reload_words -
            (completed % s_tdma_pio_spi_rx_sequence.reload_words));
}
static uint32_t tdma_pio_spi_phys_rx_write_index(void) { return completed & 1023; }
static uint32_t tdma_pio_spi_phys_rx_ring_word(uint64_t position) {
    ++word_reads;
    if ((copy_phase || stimulus >= 3) && position >= packet_start+12 && stimulus && !stimulus_ran) {
        const uint64_t advance = (stimulus == 1 || stimulus == 3) ? 600u : s_tdma_pio_spi_rx_sequence.reload_words;
        if (stimulus == 1 || stimulus == 3) {
            for (uint64_t i = completed; i < completed+advance; ++i) ring[i & 1023] = 0x77;
        } else {
            for (unsigned i = 0; i < 1024; ++i) ring[i] = 0x77;
        }
        completed += advance;
        tick += advance;
        publish_count();
        stimulus_ran = true;
    }
    return ring[position & 1023];
}
#if TDMA_TEST_PHYSICAL_RX
typedef unsigned uint;
typedef unsigned dma_channel_config;
static struct { uint32_t rxf[4]; } capture_pio;
typedef __typeof__(&capture_pio) PIO;
enum { DMA_SIZE_32, TDMA_PIO_SPI_RX_RING_LOG2 = 12, TDMA_PIO_SPI_ROLE_SLAVE = 1,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET,
    TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE, TDMA_PIO_SPI_PHYS_ERROR_NONE };
#define s_tdma_pio_spi_rx_ring ring
static bool arm_backend_available = true;
static unsigned arm_starts, origin_calls, latch_calls;
static bool tdma_pio_spi_phys_ensure_rx_dma(void) { return arm_backend_available; }
static void dma_channel_abort(uint channel) { assert(channel == 4u); }
static void tdma_pio_spi_phys_rx_prepare(tdma_pio_spi_phys_t *phys) { (void)phys; }
static dma_channel_config dma_channel_get_default_config(uint channel) { assert(channel == 4u); return 0u; }
static void channel_config_set_transfer_data_size(dma_channel_config *cfg, uint v) { (void)cfg; assert(v == DMA_SIZE_32); }
static void channel_config_set_read_increment(dma_channel_config *cfg, bool v) { (void)cfg; assert(!v); }
static void channel_config_set_write_increment(dma_channel_config *cfg, bool v) { (void)cfg; assert(v); }
static void channel_config_set_ring(dma_channel_config *cfg, bool write, uint bits) { (void)cfg; assert(write && bits == 12u); }
static void channel_config_set_irq_quiet(dma_channel_config *cfg, bool v) { (void)cfg; assert(v); }
static PIO tdma_pio_spi_phys_capture_pio(tdma_pio_spi_phys_t *phys) { (void)phys; return &capture_pio; }
static uint tdma_pio_spi_phys_capture_sm(tdma_pio_spi_phys_t *phys) { (void)phys; return 0u; }
static uint pio_get_dreq(PIO pio, uint sm, bool tx) { assert(pio == &capture_pio && sm == 0u && !tx); return 0u; }
static void channel_config_set_dreq(dma_channel_config *cfg, uint dreq) { (void)cfg; assert(dreq == 0u); }
static uint32_t dma_encode_transfer_count_with_self_trigger(uint32_t words) { return (1u << 28u) | words; }
static void dma_channel_configure(uint channel, const dma_channel_config *cfg, void *dst,
    const void *src, uint32_t count, bool start) {
    (void)cfg; assert(channel == 4u && dst == ring && src == capture_pio.rxf && !start);
    hardware.ch[channel].transfer_count = count;
}
static void dma_start_channel_mask(uint32_t mask) { assert(mask == (1u << 4u)); ++arm_starts; }
static bool tdma_pio_spi_phys_is_flight_persona(void) { return s_tdma_pio_spi_program_persona != 0u; }
static void tdma_pio_spi_phys_set_error(tdma_pio_spi_phys_t *phys, uint32_t error) { phys->snapshot.last_error = error; }
static bool tdma_pio_spi_phys_origin_rx(tdma_pio_spi_phys_t *phys, uint8_t *packet,
    size_t capacity, size_t *size, uint64_t *stamp) {
    (void)phys; assert(capacity >= 3u); ++origin_calls;
    memcpy(packet, "abc", 3u); *size = 3u; *stamp = 0u; return true;
}
static uint64_t vdc_timestamp_clock_now_ns(void) { return 1000000u; }
static uint64_t tdma_pio_spi_phys_wire_time_ns(uint32_t baud, size_t size, uint32_t header) {
    assert(baud != 0u && size != 0u && header == 4u); return 1000u;
}
static bool tdma_pio_spi_phys_clock_latch_read_and_rearm(tdma_pio_spi_phys_t *phys, uint64_t *stamp) {
    (void)phys; ++latch_calls; *stamp = 456u; return true;
}
static void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys) { (void)phys; }
#endif
#include "capture_routines.inc"

static tdma_pio_spi_phys_t setup(unsigned shift, uint64_t first, uint64_t produced) {
    memset(ring,0,sizeof(ring));
    memset(s_tdma_pio_spi_rx_frame,0,sizeof(s_tdma_pio_spi_rx_frame));
    tick = 0; copy_phase = stimulus_ran = false; stimulus = word_reads = clock_reads = 0;
    s_tdma_pio_spi_rx_scan_produced = 0; s_tdma_pio_spi_program_persona = 0;
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,301,0));
    tdma_pio_spi_phys_t phys = {.rx_capture_active=true,.process_image_enabled=true,
        .flight_physical_byte_count=301,.flight_tail_bytes=5,.flight_alignment_byte_shift=17,.flight_alignment_bit_shift=5,
        .flight_overlay_alignment_locked=true, .armed=true, .role=1u, .baud_hz=10000000u};
#if TDMA_TEST_PHYSICAL_RX
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER;
    arm_backend_available = true;
    assert(tdma_pio_spi_phys_rx_arm(&phys));
#endif
    completed = produced; packet_start = first; publish_count();
    uint8_t payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
    memset(payload,0x33,sizeof(payload));
    uint8_t packet[TDMA_PIO_SPI_RX_DMA_WORD_MAX] = {0x54,0x44,0x24,1};
    size_t size; tdma_transport_result_t result;
    tdma_transport_frame_build_t build = {.frame_class=TDMA_TRANSPORT_FRAME_CLASS_SHORT,
        .origin_slot_id=0,.transport_sequence=1234,.payload_class=1,.flags=1,
        .schedule_crc32=0x12345678,.ring_profile_crc32=0x87654321,.hop_limit=3,
        .payload=payload,.payload_size=sizeof(payload)};
    assert(tdma_transport_frame_encode(&build,packet+4,sizeof(packet)-4,&size,&result));
    for (unsigned i = 0; i < sizeof(packet); ++i) {
        ring[(first+i)&1023] |= packet[i] >> shift;
        if (shift) ring[(first+i+1)&1023] |= (packet[i] << (8-shift)) & 255;
    }
    return phys;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    size_t received = 0;
    if (!strcmp(argv[1],"stable")) {
        for (unsigned shift = 0; shift < 8; ++shift) {
            tdma_pio_spi_phys_t phys=setup(shift,384,1000);
            assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
            assert(received==TDMA_PIO_SPI_RX_DMA_WORD_MAX);
            uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
            for (unsigned i=0; i<sizeof(packet); ++i) packet[i]=(uint8_t)s_tdma_pio_spi_rx_frame[i+4];
            tdma_transport_frame_view_t view; tdma_transport_result_t result;
            assert(tdma_transport_frame_decode(packet,sizeof(packet),&view,&result));
            assert(view.transport_sequence==1234);
            assert(phys.flight_alignment_byte_shift==17 && phys.flight_alignment_bit_shift==5);
        }
    } else if (!strcmp(argv[1],"overwrite") || !strcmp(argv[1],"counter_alias")) {
        tdma_pio_spi_phys_t phys=setup(0,384,1000);
        stimulus=!strcmp(argv[1],"overwrite") ? 1 : 2;
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(stimulus_ran && received==0 && phys.snapshot.rx_observation_drop_count>0);
        assert(phys.flight_alignment_byte_shift==17 && phys.flight_alignment_bit_shift==5);
    } else if (!strcmp(argv[1],"bounded")) {
        tdma_pio_spi_phys_t phys=setup(0,0,900);
        memset(ring,0,sizeof(ring));
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(phys.snapshot.rx_scan_yield_count==1 && received==0);
        assert(word_reads<=TDMA_RX_OBSERVATION_SCAN_WORDS*3);
        assert(s_tdma_pio_spi_rx_scan_produced==900-TDMA_PIO_SPI_RX_DMA_WORD_MAX);
    } else if (!strcmp(argv[1],"prefix")) {
        /* Search repeated false magic prefixes before a real frame across
         * SRAM wrap, for every bit phase and both physical ISR directions. */
        for (unsigned follower=0; follower<2; ++follower) {
            for (unsigned shift=0; shift<8; ++shift) {
                tdma_pio_spi_phys_t phys=setup(shift,980,1280);
                for (unsigned i=664;i<980;++i) ring[i&1023]=0x54;
                if (follower) {
                    s_tdma_pio_spi_program_persona=TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
                    for (unsigned i=0;i<1024;++i) ring[i]=__rev(ring[i]);
                }
                assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
                uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
                for (unsigned i=0;i<sizeof(packet);++i) packet[i]=(uint8_t)s_tdma_pio_spi_rx_frame[i+4];
                tdma_transport_frame_view_t view; tdma_transport_result_t result;
                assert(tdma_transport_frame_decode(packet,sizeof(packet),&view,&result));
                assert(view.transport_sequence==1234 && received==TDMA_PIO_SPI_RX_DMA_WORD_MAX);
            }
        }
    } else if (!strcmp(argv[1],"incomplete")) {
        tdma_pio_spi_phys_t phys=setup(3,11,100);
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(s_tdma_pio_spi_rx_scan_produced==11 && received==0);
        completed=400; publish_count();
        assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(received==TDMA_PIO_SPI_RX_DMA_WORD_MAX);
    } else if (!strcmp(argv[1],"idle_gap")) {
        tdma_pio_spi_phys_t phys=setup(0,0,0);
        tick += s_tdma_pio_spi_rx_sequence.reload_words;
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        /* P0T uses this public delta to infer a cable. No DMA writes must
         * never manufacture an edge after a long idle observation gap. */
        assert(phys.snapshot.rx_dma_produced_words==0 && word_reads==0);
        assert(phys.snapshot.rx_observation_drop_count>0);
    } else if (!strcmp(argv[1],"phase_gap")) {
        tdma_pio_spi_phys_t phys=setup(0,317,617);
        phys.flight_overlay_alignment_locked=false;
        phys.flight_overlay_alignment_samples=1;
        /* Lose an entire hardware period before either initial alignment
         * sample. The public count is a lower bound, but SRAM/frame phases
         * must match the true physical byte coordinate. */
        completed += s_tdma_pio_spi_rx_sequence.reload_words;
        tick += completed;
        publish_count();
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(phys.snapshot.rx_dma_produced_words==617);
        assert(phys.flight_overlay_alignment_samples==0);
        uint32_t frame[TDMA_PIO_SPI_RX_DMA_WORD_MAX];
        for (unsigned i=0;i<TDMA_PIO_SPI_RX_DMA_WORD_MAX;++i) frame[i]=ring[(317+i)&1023];
        for (unsigned n=1;n<=2;++n) {
            const uint64_t start=317+n*301ull+s_tdma_pio_spi_rx_sequence.reload_words;
            for (unsigned i=0;i<TDMA_PIO_SPI_RX_DMA_WORD_MAX;++i) ring[(start+i)&1023]=frame[i];
            completed=start+TDMA_PIO_SPI_RX_DMA_WORD_MAX;
            tick+=301;publish_count();
            assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
            assert(phys.flight_alignment_byte_shift==start%301);
            assert(phys.flight_alignment_bit_shift==0);
            assert(phys.flight_overlay_alignment_samples==n);
        }
    } else if (!strcmp(argv[1],"wrong_mode")) {
        tdma_pio_spi_phys_t phys=setup(0,11,400);
        hardware.ch[4].transfer_count=UINT32_MAX;
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(received==0 && word_reads==0);
    } else if (!strcmp(argv[1],"copy_window")) {
        /* Compare against individual wire bits, across SRAM and sequence
         * wrap, both ISR directions, and short/full discovery intervals. */
        const uint64_t starts[] = {0, 1023, (1ull<<32)-19u, UINT64_MAX-19u};
        const unsigned counts[] = {0, 1, 2, TDMA_PIO_SPI_RX_DMA_WORD_MAX,
            TDMA_RX_SCAN_WINDOW_BYTES};
        for (unsigned start=0;start<sizeof(starts)/sizeof(starts[0]);++start)
        for (unsigned size=0;size<sizeof(counts)/sizeof(counts[0]);++size)
        for (unsigned reverse=0;reverse<2;++reverse) for (unsigned shift=0;shift<8;++shift) {
            (void)setup(0,0,0);
            const uint64_t first=starts[start];
            const unsigned count=counts[size];
            uint8_t raw[TDMA_RX_SCAN_WINDOW_BYTES+1];
            uint8_t output[TDMA_RX_SCAN_WINDOW_BYTES+2];
            memset(output,0xcc,sizeof(output));
            for (unsigned i=0;i<sizeof(raw);++i) {
                raw[i]=(uint8_t)(i*37u+13u);
                const uint32_t word=0xabcdef00u|raw[i];
                ring[(first+i)&1023]=reverse ? __rev(word) : word;
            }
            if (reverse) s_tdma_pio_spi_program_persona=TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
            tdma_pio_spi_phys_rx_ring_copy(output+1,first,count,shift);
            assert(output[0]==0xcc);
            for (unsigned i=count+1;i<sizeof(output);++i) assert(output[i]==0xcc);
            for (unsigned i=0;i<count;++i) {
                uint8_t expected=0;
                for (unsigned bit=0;bit<8;++bit) {
                    const unsigned position=i*8u+shift+bit;
                    expected=(uint8_t)((expected<<1)|((raw[position/8]>>(7-position%8))&1));
                }
                assert(output[i+1]==expected);
            }
            assert(word_reads==count+(count!=0 && shift!=0));
            const uint32_t before=word_reads;
            tdma_pio_spi_phys_rx_ring_copy(NULL,UINT64_MAX,0,shift);
            assert(word_reads==before);
        }
    } else assert(!"unknown case");
    printf("RX observation %s passed\n",argv[1]);
    return 0;
}
