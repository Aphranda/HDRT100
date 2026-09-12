/* Reuse the advancing DMA bus, with the real asynchronous capture routines. */
#define main legacy_scanner_main
#include "test_tdma_rx_observation.c"
#undef main

static void install(uint64_t start, unsigned shift, uint32_t sequence)
{
    uint8_t payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
    memset(payload, 0x45, sizeof(payload));
    uint8_t packet[TDMA_PIO_SPI_RX_DMA_WORD_MAX] = {0x54,0x44,0x24,1};
    size_t size;
    tdma_transport_result_t result;
    tdma_transport_frame_build_t build = {.frame_class=TDMA_TRANSPORT_FRAME_CLASS_SHORT,
        .origin_slot_id=0,.transport_sequence=sequence,.payload_class=1,.flags=1,
        .schedule_crc32=0x12345678,.ring_profile_crc32=0x87654321,.hop_limit=3,
        .payload=payload,.payload_size=sizeof(payload)};
    assert(tdma_transport_frame_encode(&build,packet+4,sizeof(packet)-4,&size,&result));
    for (unsigned i=0;i<sizeof(packet)+1u;++i) ring[(start+i)&1023]=0;
    for (unsigned i=0;i<sizeof(packet);++i) {
        ring[(start+i)&1023] |= packet[i] >> shift;
        if (shift) ring[(start+i+1)&1023] |= (packet[i] << (8-shift)) & 255;
    }
}

static void assert_packet(uint32_t sequence, size_t received)
{
    assert(received==TDMA_PIO_SPI_RX_DMA_WORD_MAX);
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    for (unsigned i=0;i<sizeof(packet);++i) packet[i]=(uint8_t)s_tdma_pio_spi_rx_frame[i+4];
    tdma_transport_frame_view_t view;tdma_transport_result_t result;
    assert(tdma_transport_frame_decode(packet,sizeof(packet),&view,&result));
    assert(view.transport_sequence==sequence);
}

static void request(tdma_pio_spi_phys_t *phys, tdma_rx_scan_t *job)
{
    size_t received=42;
    phys->rx_scan_preparation=job;
    assert(!tdma_pio_spi_phys_capture_words(phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
    assert(received==0 && tdma_rx_scan_state(job)==TDMA_RX_SCAN_REQUESTED);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    size_t received=0;
    if (!strcmp(argv[1],"phases")) {
        for (unsigned reverse=0;reverse<2;++reverse) for (unsigned shift=0;shift<8;++shift) {
            tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(shift,980,1280);
            if (reverse) {
                s_tdma_pio_spi_program_persona=TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
                for (unsigned i=0;i<1024;++i) ring[i]=__rev(ring[i]);
            }
            request(&phys,&job);
            uint32_t reads=word_reads;
            tdma_rx_scan_core0_service(&job);
            assert(word_reads==reads); /* Core0 never touches the DMA bus. */
            assert(tdma_rx_scan_state(&job)==TDMA_RX_SCAN_READY && job.result.valid);
            assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
            assert(word_reads-reads<=2u*TDMA_PIO_SPI_RX_DMA_WORD_MAX+40u);
            assert_packet(1234,received);
            assert(phys.flight_alignment_byte_shift==17 && phys.flight_alignment_bit_shift==5);
        }
    } else if (!strcmp(argv[1],"worker_pause")) {
        tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(0,384,1000);
        request(&phys,&job);tdma_rx_scan_t original=job;
        for (unsigned i=0;i<5;++i) {
            assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
            assert(memcmp(&job,&original,sizeof(job))==0);
        }
        assert(tdma_rx_scan_core0_claim(&job));
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        for (unsigned n=1;n<=6;++n) install(384+301ull*n,0,5678);
        completed=384+301ull*6+296;tick+=301*6;publish_count();
        tdma_rx_scan_core0_build_claimed(&job);
        assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert_packet(5678,received); /* Worker never supplied receive bytes. */
    } else if (!strcmp(argv[1],"live_overwrite") || !strcmp(argv[1],"live_epoch")) {
        tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(0,384,1000);
        request(&phys,&job);tdma_rx_scan_core0_service(&job);
        stimulus=!strcmp(argv[1],"live_overwrite")?1:2;
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(stimulus_ran && received==0 && !phys.rx_scan_hint.valid);
        assert(phys.flight_alignment_byte_shift==17 && phys.flight_alignment_bit_shift==5);
    } else if (!strcmp(argv[1],"snapshot_overwrite") || !strcmp(argv[1],"snapshot_epoch")) {
        tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(0,384,1000);
        phys.rx_scan_preparation=&job;stimulus=!strcmp(argv[1],"snapshot_overwrite")?3:4;
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(stimulus_ran && received==0 && tdma_rx_scan_state(&job)==TDMA_RX_SCAN_IDLE);
    } else if (!strcmp(argv[1],"cancel")) {
        for (unsigned state=0;state<3;++state) {
            tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(0,384,1000);
            request(&phys,&job);
            if (state==1) assert(tdma_rx_scan_core0_claim(&job));
            if (state==2) tdma_rx_scan_core0_service(&job);
            uint32_t epoch=job.epoch;
            assert(tdma_pio_spi_phys_rx_scan_cancel(&phys)==(state!=1));
            assert(job.epoch==epoch+1 && !phys.rx_scan_hint.valid);
            if (state==1) {
                assert(tdma_rx_scan_state(&job)==TDMA_RX_SCAN_CANCELLED);
                assert(!tdma_rx_scan_request(&job));
                tdma_rx_scan_core0_build_claimed(&job);
            }
            assert(tdma_rx_scan_state(&job)==TDMA_RX_SCAN_IDLE);
            request(&phys,&job);tdma_rx_scan_core0_service(&job);
            assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        }
    } else if (!strcmp(argv[1],"resync")) {
        tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(0,384,1000);
        request(&phys,&job);tdma_rx_scan_core0_service(&job);
        assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        /* Incoming parser phase changes. Locked wire position cannot follow it. */
        memset(ring,0,sizeof(ring));install(1100,3,5678);completed=1400;tick+=400;publish_count();
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(!phys.rx_scan_hint.valid);tdma_rx_scan_core0_service(&job);
        assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert_packet(5678,received);
        assert(phys.flight_alignment_byte_shift==17 && phys.flight_alignment_bit_shift==5);
    } else if (!strcmp(argv[1],"incomplete")) {
        tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(3,11,100);
        request(&phys,&job);tdma_rx_scan_core0_service(&job);
        assert(!job.result.valid);
        completed=400;publish_count();
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        tdma_rx_scan_core0_service(&job);
        assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert_packet(1234,received);
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
    } else if (!strcmp(argv[1],"geometry")) {
        tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(0,384,1000);
        phys.flight_overlay_alignment_locked=false;install(384+301,0,5678);
        request(&phys,&job);tdma_rx_scan_core0_service(&job);
        assert(job.result.stable_frames==2);
        assert(tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(phys.flight_overlay_alignment_samples==2 && phys.flight_alignment_byte_shift==384%301);
    } else if (!strcmp(argv[1],"age") || !strcmp(argv[1],"epoch") ||
               !strcmp(argv[1],"persona") || !strcmp(argv[1],"config")) {
        tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(0,384,1000);
        request(&phys,&job);tdma_rx_scan_core0_service(&job);
        if (!strcmp(argv[1],"age")) job.request_epoch++;
        else if (!strcmp(argv[1],"epoch")) job.observation_epoch++;
        else if (!strcmp(argv[1],"persona")) job.persona++;
        else job.physical_frame_words++;
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
        assert(received==0 && !phys.rx_scan_hint.valid && phys.snapshot.rx_observation_drop_count>0);
    } else if (!strcmp(argv[1],"false_magic")) {
        tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(0,384,1000);
        /* A fully shaped packet with a corrupt CRC is not an alignment grant. */
        ring[(384+32)&1023]^=1;
        request(&phys,&job);tdma_rx_scan_core0_service(&job);
        assert(!job.result.valid);
        assert(!tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received));
    } else if (!strcmp(argv[1],"capacity")) {
        tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(0,384,1000);
        request(&phys,&job);tdma_rx_scan_core0_service(&job);
        assert(tdma_rx_scan_cancel(&job));
        job.byte_count=sizeof(job.bytes)+1u;assert(!tdma_rx_scan_request(&job));
        job.byte_count=sizeof(job.bytes);job.window_start=UINT64_MAX;assert(!tdma_rx_scan_request(&job));
        job.window_start=0;job.tail_words=UINT32_MAX;assert(!tdma_rx_scan_request(&job));
        tdma_rx_scan_hint_t hint={.valid=true,.frame_words=296,.stride_words=UINT32_MAX};
        uint64_t candidate=0;
        assert(!tdma_rx_scan_locate(&hint,1000,0,&candidate));
    } else assert(!"unknown async case");
    puts("async scanner test passed");return 0;
}
