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

#if TDMA_TEST_PHYSICAL_RX
static void assert_capture_zero(const tdma_rx_capture_t *capture)
{
    const uint8_t *bytes = (const uint8_t *)capture;
    for (size_t i = 0u; i < sizeof(*capture); ++i) assert(bytes[i] == 0u);
}

static tdma_pio_spi_phys_t ready_capture(tdma_rx_scan_t *job, uint32_t shift,
                                        uint64_t first, uint64_t produced)
{
    tdma_pio_spi_phys_t phys = setup(shift, first, produced);
    request(&phys, job);
    tdma_rx_scan_core0_service(job);
    assert(job->result.valid);
    return phys;
}

static bool delivered(tdma_pio_spi_phys_t *phys, tdma_rx_capture_t *capture, uint32_t sequence)
{
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t size = 123u; uint64_t stamp = 0u;
    memset(capture, 0xa5, sizeof(*capture));
    const bool result = tdma_pio_spi_phys_rx_ex(phys, packet, sizeof(packet), &size, &stamp, capture);
    if (result) {
        tdma_transport_frame_view_t view; tdma_transport_result_t decoded;
        assert(tdma_transport_frame_decode(packet, size, &view, &decoded));
        assert(view.transport_sequence == sequence && size == TDMA_TRANSPORT_SHORT_PACKET_MAX);
        assert(stamp == 456u);
    } else {
        assert(size == 0u); assert_capture_zero(capture);
    }
    return result;
}

static bool test_capture_case(const char *name)
{
    if (!strcmp(name, "capture_coordinates")) {
        for (unsigned reverse = 0u; reverse < 2u; ++reverse)
        for (unsigned shift = 0u; shift < 8u; ++shift) {
            tdma_rx_scan_t job = {0};
            tdma_pio_spi_phys_t phys = setup(shift, 980u, 1280u);
            if (reverse) {
                s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
                for (unsigned i = 0u; i < 1024u; ++i) ring[i] = __rev(ring[i]);
            }
            request(&phys, &job); tdma_rx_scan_core0_service(&job);
            const uint32_t reads = word_reads, clocks = clock_reads;
            const uint64_t id = s_tdma_pio_spi_rx_capture_id;
            tdma_rx_capture_t capture;
            assert(delivered(&phys, &capture, 1234u));
            assert(capture.flags == TDMA_RX_CAPTURE_PRIVATE_COPY);
            assert(capture.arm_epoch == s_tdma_pio_spi_rx_arm_epoch && capture.arm_epoch != 0u);
            assert(capture.capture_id == id + 1u && capture.observation_epoch == 0u);
            assert(capture.candidate == 980u && capture.frame_words == TDMA_PIO_SPI_RX_DMA_WORD_MAX);
            assert(capture.frame_words == TDMA_TRANSPORT_SHORT_PACKET_MAX + TDMA_PIO_SPI_PACKET_HEADER_SIZE);
            assert(capture.produced_before == 1280u && capture.produced_after == 1280u);
            assert(capture.bit_shift == shift && capture.persona == s_tdma_pio_spi_program_persona);
            assert(word_reads - reads == capture.frame_words + (shift != 0u));
            assert(clock_reads - clocks == 4u); /* Exactly two bracketed DMA observations. */
        }
        tdma_rx_scan_t job = {0};
        tdma_pio_spi_phys_t phys = ready_capture(&job, 3u, 384u, 1000u);
        tdma_rx_capture_t first, second;
        assert(delivered(&phys, &first, 1234u));
        install(685u, 3u, 4321u);
        assert(delivered(&phys, &second, 4321u));
        assert(first.arm_epoch == second.arm_epoch && first.capture_id + 1u == second.capture_id);
        assert(first.candidate == 384u && second.candidate == 685u);
        /* The compatibility wrapper consumes a distinct successful copy too. */
        install(986u, 3u, 4322u); completed = 1300u; publish_count();
        uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX]; size_t size; uint64_t stamp;
        assert(tdma_pio_spi_phys_rx(&phys, packet, sizeof(packet), &size, &stamp));
        assert(s_tdma_pio_spi_rx_capture_id == second.capture_id + 1u);
    } else if (!strcmp(name, "capture_failures")) {
        for (unsigned failure = 1u; failure <= 4u; ++failure) {
            tdma_rx_scan_t job = {0};
            tdma_pio_spi_phys_t phys = setup(0u, 384u, 1000u);
            phys.rx_scan_preparation = &job;
            if (failure <= 2u) { request(&phys, &job); tdma_rx_scan_core0_service(&job); }
            stimulus = failure;
            const uint64_t id = s_tdma_pio_spi_rx_capture_id;
            tdma_rx_capture_t capture;
            assert(!delivered(&phys, &capture, 0u));
            assert(stimulus_ran && s_tdma_pio_spi_rx_capture_id == id);
        }
        tdma_rx_scan_t job = {0};
        tdma_pio_spi_phys_t phys = ready_capture(&job, 0u, 384u, 1000u);
        ring[384u] = 0u; /* Private header changed after discovery. */
        const uint64_t id = s_tdma_pio_spi_rx_capture_id;
        tdma_rx_capture_t capture;
        assert(!delivered(&phys, &capture, 0u));
        assert(s_tdma_pio_spi_rx_capture_id == id);
    } else if (!strcmp(name, "capture_delivery")) {
        for (unsigned bad = 0u; bad < 6u; ++bad) {
            tdma_pio_spi_phys_t phys = setup(0u, 384u, 1000u);
            if (bad == 5u) phys.armed = false;
            tdma_rx_capture_t capture; memset(&capture, 0xa5, sizeof(capture));
            uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX]; size_t size = 123u; uint64_t stamp = 0u;
            const uint32_t reads = word_reads, clocks = clock_reads;
            assert(!tdma_pio_spi_phys_rx_ex(bad == 0u ? NULL : &phys,
                bad == 1u ? NULL : packet, bad == 2u ? 0u : sizeof(packet),
                bad == 3u ? NULL : &size, bad == 4u ? NULL : &stamp, &capture));
            assert_capture_zero(&capture);
            assert(word_reads == reads && clock_reads == clocks);
            if (bad != 3u) assert(size == 0u);
        }
        tdma_rx_scan_t job = {0};
        tdma_pio_spi_phys_t phys = ready_capture(&job, 0u, 384u, 1000u);
        const uint64_t id = s_tdma_pio_spi_rx_capture_id;
        tdma_rx_capture_t capture; memset(&capture, 0xa5, sizeof(capture));
        uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX]; memset(packet, 0x77, sizeof(packet));
        size_t size = 123u; uint64_t stamp = 0u;
        assert(!tdma_pio_spi_phys_rx_ex(&phys, packet, 1u, &size, &stamp, &capture));
        assert(size == 0u && packet[0] == 0x77u);
        assert(s_tdma_pio_spi_rx_capture_id == id + 1u); /* Copy succeeded, delivery did not. */
        assert_capture_zero(&capture);
        assert(phys.snapshot.last_error == TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE);
        /* An idle/discovery return cannot reuse the last success's token. */
        assert(!delivered(&phys, &capture, 0u));
    } else if (!strcmp(name, "capture_arm")) {
        tdma_rx_scan_t job = {0};
        tdma_pio_spi_phys_t phys = ready_capture(&job, 0u, 384u, 1000u);
        tdma_rx_capture_t first, next;
        assert(delivered(&phys, &first, 1234u));
        job = (tdma_rx_scan_t){0};
        phys = ready_capture(&job, 0u, 384u, 1000u);
        assert(delivered(&phys, &next, 1234u));
        assert(next.arm_epoch == first.arm_epoch + 1u && next.capture_id == first.capture_id + 1u);
        assert(next.observation_epoch == 0u && first.observation_epoch == 0u);
        assert(next.candidate == first.candidate); /* A fresh struct/reset cannot alias the old ARM. */
        job = (tdma_rx_scan_t){0}; phys = ready_capture(&job, 0u, 384u, 1000u);
        const uint64_t epoch = s_tdma_pio_spi_rx_arm_epoch;
        const unsigned starts = arm_starts;
        arm_backend_available = false;
        assert(!tdma_pio_spi_phys_rx_arm(&phys));
        assert(!s_tdma_pio_spi_rx_arm_valid && s_tdma_pio_spi_rx_arm_epoch == epoch && arm_starts == starts);
        assert(delivered(&phys, &next, 1234u)); assert_capture_zero(&next);
        arm_backend_available = true;
        phys.flight_physical_byte_count = 0u;
        assert(!tdma_pio_spi_phys_rx_arm(&phys));
        assert(!s_tdma_pio_spi_rx_arm_valid && s_tdma_pio_spi_rx_arm_epoch == epoch && arm_starts == starts);
        assert(!tdma_pio_spi_phys_rx_arm(NULL));
        assert(!s_tdma_pio_spi_rx_arm_valid && s_tdma_pio_spi_rx_arm_epoch == epoch);
    } else if (!strcmp(name, "capture_exhaustion")) {
        /* Direct boundary fixtures avoid UINT64_MAX physical iterations. */
        s_tdma_pio_spi_rx_capture_id = UINT64_MAX - 1u;
        tdma_rx_scan_t job = {0};
        tdma_pio_spi_phys_t phys = ready_capture(&job, 0u, 384u, 1000u);
        tdma_rx_capture_t capture;
        assert(delivered(&phys, &capture, 1234u));
        assert(capture.capture_id == UINT64_MAX && capture.flags == TDMA_RX_CAPTURE_PRIVATE_COPY);
        install(685u, 0u, 4321u);
        assert(delivered(&phys, &capture, 4321u)); assert_capture_zero(&capture);
        assert(s_tdma_pio_spi_rx_capture_id == UINT64_MAX);
        s_tdma_pio_spi_rx_capture_id = 123u; /* Independent ARM-limit fixture. */
        s_tdma_pio_spi_rx_arm_epoch = UINT64_MAX - 1u;
        job = (tdma_rx_scan_t){0}; phys = ready_capture(&job, 0u, 384u, 1000u);
        assert(delivered(&phys, &capture, 1234u));
        assert(capture.arm_epoch == UINT64_MAX && capture.flags == TDMA_RX_CAPTURE_PRIVATE_COPY);
        const unsigned starts = arm_starts;
        job = (tdma_rx_scan_t){0}; phys = ready_capture(&job, 0u, 384u, 1000u);
        assert(arm_starts == starts + 1u && !s_tdma_pio_spi_rx_arm_valid);
        assert(delivered(&phys, &capture, 1234u)); assert_capture_zero(&capture);
        assert(s_tdma_pio_spi_rx_arm_epoch == UINT64_MAX);
    } else if (!strcmp(name, "capture_legacy")) {
        tdma_pio_spi_phys_t phys = setup(0u, 384u, 1000u);
        const uint64_t id = s_tdma_pio_spi_rx_capture_id;
        tdma_rx_capture_t capture;
        assert(delivered(&phys, &capture, 1234u)); /* No async station: actual legacy scanner. */
        assert_capture_zero(&capture); assert(s_tdma_pio_spi_rx_capture_id == id);
        for (unsigned selector = 0u; selector < 2u; ++selector) {
            phys = setup(0u, 384u, 1000u);
            if (selector == 0u) phys.flight_origin_workspace_owned = true;
            else s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN;
            uint8_t packet[8]; size_t size; uint64_t stamp;
            const unsigned origin_before = origin_calls, reads = word_reads;
            memset(&capture, 0xa5, sizeof(capture));
            assert(tdma_pio_spi_phys_rx_ex(&phys, packet, sizeof(packet), &size, &stamp, &capture));
            assert(size == 3u && memcmp(packet, "abc", 3u) == 0 && stamp == 0u);
            assert_capture_zero(&capture);
            assert(origin_calls == origin_before + 1u && word_reads == reads && s_tdma_pio_spi_rx_capture_id == id);
        }
    } else return false;
    printf("physical RX %s passed; capture=%zu bytes\n", name, sizeof(tdma_rx_capture_t));
    return true;
}
#endif

int main(int argc,char **argv)
{
    assert(argc==2);
#if TDMA_TEST_PHYSICAL_RX
    if (test_capture_case(argv[1])) return 0;
#endif
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
    } else if (!strcmp(argv[1],"private_header")) {
        /* A previously discovered geometry cannot authorize a different
         * physical/transport header. Check every byte of both fixed prefixes,
         * across all phases/directions; the live copy reads each word once. */
        for (unsigned reverse=0;reverse<2;++reverse) for (unsigned shift=0;shift<8;++shift)
        for (unsigned corrupt=0;corrupt<=11;++corrupt) {
            tdma_rx_scan_t job={0};tdma_pio_spi_phys_t phys=setup(shift,980,1280);
            if (reverse) {
                s_tdma_pio_spi_program_persona=TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
                for (unsigned i=0;i<1024;++i) ring[i]=__rev(ring[i]);
            }
            request(&phys,&job);tdma_rx_scan_core0_service(&job);
            assert(job.result.valid);
            /* Flip the high bit of one decoded header byte in the completed
             * DMA image, after discovery. Never alter the worker's copy. */
            if (corrupt<11) {
                uint32_t mask=0x80u>>shift;
                if (reverse) mask=__rev(mask);
                ring[(980+corrupt)&1023]^=mask;
            }
            const uint64_t cursor=s_tdma_pio_spi_rx_scan_produced;
            const uint32_t reads=word_reads;
            const bool accepted=tdma_pio_spi_phys_capture_words(&phys,TDMA_PIO_SPI_RX_DMA_WORD_MAX,&received);
            assert(accepted==(corrupt==11));
            assert(phys.flight_alignment_byte_shift==17 && phys.flight_alignment_bit_shift==5);
            if (accepted) {
                assert_packet(1234,received);
                assert(word_reads-reads==TDMA_PIO_SPI_RX_DMA_WORD_MAX+(shift!=0));
            } else {
                assert(received==0 && !phys.rx_scan_hint.valid);
                assert(s_tdma_pio_spi_rx_scan_produced==cursor);
                assert(phys.snapshot.rx_magic_at_zero==0 && phys.snapshot.rx_magic_at_shift==0);
            }
        }
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
