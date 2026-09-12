#include "tdma_rx_sequence.h"
#include "tdma_transport_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { TDMA_PIO_SPI_RX_RING_WORDS = 1024, TDMA_PIO_SPI_PACKET_HEADER_SIZE = 4,
    TDMA_PIO_SPI_RX_DMA_WORD_MAX = 4 + TDMA_TRANSPORT_SHORT_PACKET_MAX,
    TDMA_PIO_SPI_PACKET_MAGIC0 = 0x54, TDMA_PIO_SPI_PACKET_MAGIC1 = 0x44,
    TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES = 2,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN = 16,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER = 3,
    DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF = 1, DMA_CH0_TRANS_COUNT_MODE_LSB = 28 };
#define DMA_CH0_TRANS_COUNT_MODE_BITS 0xf0000000u
#define DMA_CH0_TRANS_COUNT_COUNT_BITS 0x0fffffffu
typedef struct {
    bool rx_capture_active, process_image_enabled, flight_overlay_alignment_locked;
    bool flight_origin_workspace_owned;
    uint32_t flight_physical_byte_count, flight_alignment_byte_shift, flight_alignment_bit_shift;
    uint32_t flight_overlay_alignment_samples;
    uint64_t flight_overlay_alignment_candidate;
    struct {
        uint32_t rx_dma_produced_words, rx_scan_produced_words, rx_dma_write_index, rx_dma_channel;
        uint32_t rx_ring_overrun_count, rx_magic_at_zero, rx_magic_at_shift, rx_magic_fail_count;
        uint32_t last_bad_header0, last_bad_header1, last_bad_header2, last_bad_header3, last_bad_words;
        uint32_t rx_observation_drop_count, rx_scan_yield_count;
    } snapshot;
} tdma_pio_spi_phys_t;
static struct { struct { uint32_t transfer_count; } ch[16]; } hardware;
#define dma_hw (&hardware)
static int s_tdma_pio_spi_rx_dma_channel = 4;
static uint32_t s_tdma_pio_spi_program_persona;
static tdma_rx_dma_counter_t s_tdma_pio_spi_rx_sequence;
static uint64_t s_tdma_pio_spi_rx_scan_produced;
static uint32_t ring[TDMA_PIO_SPI_RX_RING_WORDS], s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_RX_DMA_WORD_MAX];
static uint64_t tick, completed, packet_start;
static uint32_t stimulus, word_reads;
static bool copy_phase, stimulus_ran;
static uint64_t vdc_timestamp_clock_read_ticks64(void) { tick += 4; return tick; }
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
    if (copy_phase && position >= packet_start+12 && stimulus && !stimulus_ran) {
        const uint64_t advance = stimulus == 1 ? 600u : s_tdma_pio_spi_rx_sequence.reload_words;
        if (stimulus == 1) {
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
#include "capture_routines.inc"

static tdma_pio_spi_phys_t setup(unsigned shift, uint64_t first, uint64_t produced) {
    memset(ring,0,sizeof(ring));
    memset(s_tdma_pio_spi_rx_frame,0,sizeof(s_tdma_pio_spi_rx_frame));
    tick = 0; copy_phase = stimulus_ran = false; stimulus = word_reads = 0;
    s_tdma_pio_spi_rx_scan_produced = 0; s_tdma_pio_spi_program_persona = 0;
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,301,0));
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
    return (tdma_pio_spi_phys_t){.rx_capture_active=true,.process_image_enabled=true,
        .flight_physical_byte_count=301,.flight_alignment_byte_shift=17,.flight_alignment_bit_shift=5,
        .flight_overlay_alignment_locked=true};
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
    } else assert(!"unknown case");
    printf("RX observation %s passed\n",argv[1]);
}
