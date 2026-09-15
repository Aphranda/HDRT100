/* Reuse the advancing DMA bus, with the real asynchronous capture routines. */
#include <stdint.h>
static void refresh_copy_after(uint64_t start, uint32_t count, uint32_t shift);
#define TDMA_TEST_RX_COPY_AFTER(start, count, shift) refresh_copy_after(start, count, shift)
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

/* Use the four-node wire size from the failed selected-prelaunch run. The
 * existing full-capacity fixture spans 301 words and cannot model three
 * short frames inside a single 1024-word live DMA ring. */
enum { REFRESH_PACKET_WORDS = 168u, REFRESH_FRAME_WORDS = 173u };
static unsigned refresh_copy_count, refresh_copy_sizes[4], refresh_fault;
static uint64_t refresh_copy_starts[4];

static void refresh_copy_after(uint64_t start, uint32_t count, uint32_t shift)
{
    (void)shift;
    if (refresh_copy_count < 4u) {
        refresh_copy_sizes[refresh_copy_count] = count;
        refresh_copy_starts[refresh_copy_count] = start;
    }
    ++refresh_copy_count;
    if (refresh_fault == 0u || (count != 341u && count != 342u)) return;
    if (refresh_fault == 3u) tick += s_tdma_pio_spi_rx_sequence.reload_words;
    else {
        const uint64_t after = start + (refresh_fault == 1u ? 1024u : 1023u);
        assert(after >= completed);
        tick += after - completed;
        completed = after;
        publish_count();
    }
    refresh_fault = 0u;
}

static void refresh_install(uint64_t start, unsigned shift, uint32_t sequence)
{
    uint8_t payload[132]; memset(payload, 0x45, sizeof(payload));
    uint8_t packet[REFRESH_PACKET_WORDS] = {0x54, 0x44};
    size_t size; tdma_transport_result_t result;
    const tdma_transport_frame_build_t build = {
        .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT, .origin_slot_id = 0u,
        .transport_sequence = sequence, .payload_class = 1u, .flags = 1u,
        .schedule_crc32 = 0x12345678u, .ring_profile_crc32 = 0x87654321u,
        .hop_limit = 3u, .payload = payload, .payload_size = sizeof(payload)};
    assert(tdma_transport_frame_encode(&build, packet + 4u, sizeof(packet) - 4u,
                                       &size, &result));
    assert(size + 4u == REFRESH_PACKET_WORDS);
    packet[2] = (uint8_t)size; packet[3] = (uint8_t)(size >> 8u);
    for (unsigned i = 0u; i < REFRESH_FRAME_WORDS; ++i) ring[(start + i) & 1023u] = 0u;
    for (unsigned i = 0u; i < sizeof(packet); ++i) {
        ring[(start + i) & 1023u] |= packet[i] >> shift;
        if (shift != 0u) ring[(start + i + 1u) & 1023u] |= (packet[i] << (8u - shift)) & 255u;
    }
}

static void refresh_publish(uint64_t produced)
{
    assert(produced >= completed);
    tick += produced - completed;
    completed = produced;
    publish_count();
}

static bool refresh_receive(tdma_pio_spi_phys_t *phys)
{
    refresh_copy_count = 0u;
    size_t received = 0u;
    const bool ok = tdma_pio_spi_phys_capture_words(phys, TDMA_PIO_SPI_RX_DMA_WORD_MAX, &received);
    if (ok) {
        tdma_transport_frame_view_t view; tdma_transport_result_t result;
        assert(received == REFRESH_PACKET_WORDS);
        assert(tdma_transport_frame_decode(s_tdma_pio_spi_rx_frame + 4u, received - 4u,
                                           &view, &result));
    } else assert(received == 0u);
    assert(refresh_copy_count <= 2u);
    for (unsigned i = 0u; i < refresh_copy_count; ++i)
        assert(refresh_copy_sizes[i] <= TDMA_RX_SCAN_WINDOW_BYTES);
    return ok;
}

static tdma_pio_spi_phys_t refresh_setup_variant(tdma_rx_scan_t *job, unsigned shift,
                                               bool delayed, unsigned bad_second)
{
    refresh_fault = refresh_copy_count = 0u;
    tdma_pio_spi_phys_t phys = setup(shift, 0u, 0u);
    memset(ring, 0, sizeof(ring));
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence, REFRESH_FRAME_WORDS, tick));
    phys.flight_physical_byte_count = REFRESH_FRAME_WORDS;
    phys.flight_overlay_alignment_locked = false;
    phys.flight_overlay_alignment_samples = 0u;
    phys.flight_overlay_alignment_candidate = 0u;
    phys.rx_scan_preparation = job;
    refresh_install(3u, shift, 1u);
    refresh_publish(3u + REFRESH_PACKET_WORDS + (shift != 0u));
    request(&phys, job);
    /* Hold the initial one-frame private discovery while three more wire
     * frames arrive. Its later delivery must not accidentally give the live
     * scanner two adjacent station captures before the high-rate case starts. */
    if (delayed) {
        for (unsigned frame = 1u; frame <= 3u; ++frame)
            refresh_install(3u + frame * REFRESH_FRAME_WORDS, shift, frame + 1u);
        refresh_publish(3u + 3u * REFRESH_FRAME_WORDS + REFRESH_PACKET_WORDS + (shift != 0u));
        if (bad_second != 0u) {
            const uint64_t second = 3u + 3u * REFRESH_FRAME_WORDS;
            /* Damage either the outer marker or the stable identity CRC. */
            ring[(second + (bad_second == 2u ? 28u : 0u)) & 1023u] ^= 0x80u >> shift;
            if (bad_second == 3u) ring[(second - REFRESH_FRAME_WORDS) & 1023u] ^= 0x80u >> shift;
        }
    }
    const uint32_t reads = word_reads;
    tdma_rx_scan_core0_service(job);
    assert(word_reads == reads); /* Only Core1 touches the live DMA ring. */
    assert(job->result.valid && job->result.stable_frames == 1u);
    assert(refresh_receive(&phys));
    assert(phys.rx_scan_hint.valid && phys.flight_overlay_alignment_samples == 1u);
    return phys;
}

static tdma_pio_spi_phys_t refresh_setup(tdma_rx_scan_t *job, unsigned shift)
{
    return refresh_setup_variant(job, shift, true, 0u);
}

static void refresh_advance(unsigned latest, unsigned shift)
{
    for (unsigned frame = latest - 2u; frame <= latest; ++frame)
        refresh_install(3u + frame * REFRESH_FRAME_WORDS, shift, frame + 1u);
    refresh_publish(3u + latest * REFRESH_FRAME_WORDS + REFRESH_PACKET_WORDS + (shift != 0u));
}

static void refresh_worker(tdma_rx_scan_t *job)
{
    const uint32_t reads = word_reads;
    tdma_rx_scan_core0_service(job);
    assert(word_reads == reads);
}

static bool test_refresh_case(const char *name)
{
    if (!strcmp(name, "refresh_high_rate")) for (unsigned shift = 0u; shift < 8u; ++shift) {
        tdma_rx_scan_t job = {0};
        tdma_pio_spi_phys_t phys = refresh_setup(&job, shift);
        unsigned accepted = 0u;
        for (unsigned iteration = 1u; iteration <= 8u; ++iteration) {
            const unsigned latest = 3u + 3u * iteration;
            refresh_advance(latest, shift);
            accepted += refresh_receive(&phys);
            refresh_worker(&job);
        }
        printf("refresh shift=%u accepted=%u samples=%u hint_stable=%u drops=%u\n",
               shift, accepted, phys.flight_overlay_alignment_samples,
               phys.rx_scan_hint.stable_frames, phys.snapshot.rx_observation_drop_count);
        fflush(stdout);
        assert(accepted != 0u && phys.rx_scan_hint.valid);
        assert(phys.flight_overlay_alignment_samples == TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES);
        assert(phys.flight_alignment_byte_shift == 3u && phys.flight_alignment_bit_shift == shift);
        /* Obtaining sufficient proof stops refresh even before the overlay
         * consumer marks its successful commit as locked. */
        assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_IDLE);
        for (unsigned locked = 0u; locked < 2u; ++locked) {
            phys.flight_overlay_alignment_locked = locked != 0u;
            refresh_advance(30u + 3u * locked, shift);
            assert(refresh_receive(&phys));
            assert(refresh_copy_count == 1u && refresh_copy_sizes[0] == REFRESH_PACKET_WORDS);
            assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_IDLE);
        }
    } else if (!strcmp(name, "refresh_busy")) for (unsigned building = 0u; building < 2u; ++building) {
        tdma_rx_scan_t job = {0}; tdma_pio_spi_phys_t phys = refresh_setup(&job, 5u);
        assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_REQUESTED);
        if (building) assert(tdma_rx_scan_core0_claim(&job));
        tdma_rx_scan_t saved = job;
        for (unsigned iteration = 1u; iteration <= 3u; ++iteration) {
            refresh_advance(3u + 3u * iteration, 5u);
            assert(refresh_receive(&phys) && phys.rx_scan_hint.valid);
            assert(refresh_copy_count == 1u && refresh_copy_sizes[0] == REFRESH_PACKET_WORDS);
            assert(memcmp(&job, &saved, sizeof(job)) == 0);
            assert(phys.flight_overlay_alignment_samples == 1u);
        }
        /* The captured pair has now left SRAM. The worker still owns its
         * unchanged private copy and must not touch the advancing DMA bus. */
        const uint32_t reads = word_reads;
        if (building) tdma_rx_scan_core0_build_claimed(&job); else refresh_worker(&job);
        assert(word_reads == reads && job.result.valid && job.result.stable_frames == 2u);
        refresh_advance(15u, 5u);
        assert(refresh_receive(&phys) && phys.flight_overlay_alignment_samples == 2u);
    } else if (!strcmp(name, "refresh_incomplete")) for (unsigned shift = 0u; shift < 8u; ++shift) {
        tdma_rx_scan_t job = {0};
        tdma_pio_spi_phys_t phys = refresh_setup_variant(&job, shift, false, 0u);
        assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_IDLE);
        assert(!refresh_receive(&phys) && refresh_copy_count == 0u);
        refresh_install(3u + REFRESH_FRAME_WORDS, shift, 2u);
        refresh_publish(3u + REFRESH_FRAME_WORDS + REFRESH_PACKET_WORDS + (shift != 0u) - 1u);
        assert(!refresh_receive(&phys));
        assert(refresh_copy_count == 0u && tdma_rx_scan_state(&job) == TDMA_RX_SCAN_IDLE);
        assert(phys.rx_scan_hint.valid && phys.flight_overlay_alignment_samples == 1u);
    } else if (!strcmp(name, "refresh_bad_pair")) for (unsigned shift = 0u; shift < 8u; ++shift)
        for (unsigned bad = 1u; bad <= 3u; ++bad) {
        tdma_rx_scan_t job = {0};
        tdma_pio_spi_phys_t phys = refresh_setup_variant(&job, shift, true, bad);
        assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_REQUESTED);
        assert(refresh_copy_count == 2u);
        assert(refresh_copy_sizes[0] == REFRESH_FRAME_WORDS + REFRESH_PACKET_WORDS + (shift != 0u));
        assert(refresh_copy_starts[0] == 3u + 2u * REFRESH_FRAME_WORDS);
        refresh_worker(&job);
        assert(job.result.valid == (bad != 3u));
        assert(job.result.stable_frames < 2u);
        refresh_advance(6u, shift);
        assert(refresh_receive(&phys));
        assert(phys.rx_scan_hint.valid && phys.flight_overlay_alignment_samples == 1u);
    } else if (!strcmp(name, "refresh_epoch")) for (unsigned changed = 0u; changed < 2u; ++changed) {
        tdma_rx_scan_t job = {0}; tdma_pio_spi_phys_t phys = refresh_setup(&job, 5u);
        refresh_worker(&job); assert(job.result.stable_frames == 2u);
        refresh_advance(6u, 5u);
        if (changed) tick += s_tdma_pio_spi_rx_sequence.reload_words;
        else ++job.observation_epoch;
        (void)refresh_receive(&phys);
        assert(phys.flight_overlay_alignment_samples < 2u);
        assert(phys.snapshot.rx_observation_drop_count != 0u);
    } else if (!strcmp(name, "refresh_stop")) for (unsigned state = 0u; state < 3u; ++state) {
        tdma_rx_scan_t job = {0}; tdma_pio_spi_phys_t phys = refresh_setup(&job, 0u);
        if (state == 1u) assert(tdma_rx_scan_core0_claim(&job));
        if (state == 2u) refresh_worker(&job);
        const uint32_t epoch = job.epoch;
        assert(tdma_pio_spi_phys_rx_scan_cancel(&phys) == (state != 1u));
        phys.rx_capture_active = false;
        assert(job.epoch == epoch + 1u && !phys.rx_scan_hint.valid);
        if (state == 1u) {
            assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_CANCELLED);
            tdma_rx_scan_t saved = job;
            assert(!refresh_receive(&phys) && memcmp(&job, &saved, sizeof(job)) == 0);
            assert(!tdma_rx_scan_request(&job));
            tdma_rx_scan_core0_build_claimed(&job);
        }
        assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_IDLE);
        assert(!refresh_receive(&phys) && refresh_copy_count == 0u);
        assert(phys.flight_overlay_alignment_samples < 2u);
    } else if (!strcmp(name, "refresh_copy_recheck")) for (unsigned fault = 1u; fault <= 3u; ++fault) {
        tdma_rx_scan_t job = {0}; tdma_pio_spi_phys_t phys = refresh_setup(&job, 5u);
        /* Withdraw the unclaimed refresh only; ordinary geometry remains. */
        assert(tdma_rx_scan_cancel(&job) && phys.rx_scan_hint.valid);
        refresh_advance(6u, 5u); refresh_fault = fault;
        (void)refresh_receive(&phys);
        assert(refresh_fault == 0u && refresh_copy_count <= 2u);
        assert(phys.flight_overlay_alignment_samples < 2u);
        assert(tdma_rx_scan_state(&job) == (fault == 2u ? TDMA_RX_SCAN_REQUESTED : TDMA_RX_SCAN_IDLE));
        if (fault == 2u) { refresh_worker(&job); assert(job.result.stable_frames == 2u); }
    } else if (!strcmp(name, "refresh_retry_bound")) {
        tdma_rx_scan_t job = {0}; tdma_pio_spi_phys_t phys = refresh_setup(&job, 5u);
        assert(tdma_rx_scan_cancel(&job));
        refresh_advance(6u, 5u);
        /* A caught-up ordinary cursor may be newer than the two-frame
         * refresh start. Expire that refresh while the ordinary frame stays
         * in range, then damage its marker: no third discovery copy here. */
        s_tdma_pio_spi_rx_scan_produced = 3u + 6u * REFRESH_FRAME_WORDS;
        ring[s_tdma_pio_spi_rx_scan_produced & 1023u] ^= 0x80u >> 5u;
        refresh_fault = 1u;
        assert(!refresh_receive(&phys));
        assert(refresh_copy_count == 2u && !phys.rx_scan_hint.valid);
        assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_IDLE);
        assert(!refresh_receive(&phys));
        assert(refresh_copy_count == 1u && tdma_rx_scan_state(&job) == TDMA_RX_SCAN_REQUESTED);
    } else if (!strcmp(name, "refresh_metadata")) for (unsigned changed = 0u; changed < 6u; ++changed) {
        tdma_rx_scan_t job = {0}; tdma_pio_spi_phys_t phys = refresh_setup(&job, 0u);
        refresh_worker(&job); assert(job.result.stable_frames == 2u);
        if (changed == 0u) ++job.request_epoch;
        if (changed == 1u) ++job.persona;
        if (changed == 2u) --job.max_frame_words;
        if (changed == 3u) ++job.physical_frame_words;
        if (changed == 4u) ++job.tail_words;
        if (changed == 5u) ++job.observation_epoch;
        refresh_advance(6u, 0u);
        assert(!refresh_receive(&phys));
        assert(!phys.rx_scan_hint.valid && phys.flight_overlay_alignment_samples < 2u);
        assert(phys.snapshot.rx_observation_drop_count != 0u);
    } else if (!strcmp(name, "refresh_maximum")) for (unsigned shift = 0u; shift < 8u; ++shift) {
        tdma_rx_scan_t job = {0}; tdma_pio_spi_phys_t phys = setup(shift, 3u, 299u + (shift != 0u));
        phys.flight_physical_byte_count = 307u; phys.flight_tail_bytes = 11u;
        phys.flight_overlay_alignment_locked = false;
        assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence, 307u, tick));
        publish_count(); request(&phys, &job);
        for (unsigned frame = 1u; frame <= 3u; ++frame) install(3u + frame * 307u, shift, frame + 1u);
        refresh_publish(3u + 3u * 307u + TDMA_PIO_SPI_RX_DMA_WORD_MAX + (shift != 0u));
        refresh_worker(&job); assert(job.result.stable_frames == 1u);
        refresh_copy_count = 0u;
        const uint32_t reads = word_reads;
        size_t received = 0u;
        assert(tdma_pio_spi_phys_capture_words(&phys, TDMA_PIO_SPI_RX_DMA_WORD_MAX, &received));
        assert_packet(3u, received);
        assert(refresh_copy_count == 2u && refresh_copy_sizes[0] == 603u + (shift != 0u));
        assert(refresh_copy_sizes[1] == TDMA_PIO_SPI_RX_DMA_WORD_MAX);
        assert(word_reads - reads == 603u + TDMA_PIO_SPI_RX_DMA_WORD_MAX + 2u * (shift != 0u));
        assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_REQUESTED);
        assert(phys.flight_overlay_alignment_samples == 1u);
        refresh_worker(&job); assert(job.result.valid && job.result.stable_frames == 2u);
    } else if (!strcmp(name, "refresh_no_candidate")) {
        /* Direct coordinate fixtures cover arithmetic boundaries that cannot
         * be reached by billions of physical DMA iterations in a host test. */
        const uint64_t anchors[] = {3u, 3u, 400u, UINT64_MAX, UINT64_MAX - 100u};
        const uint64_t produced[] = {0u, 341u, 741u, UINT64_MAX, UINT64_MAX};
        for (unsigned i = 0u; i < sizeof(anchors) / sizeof(anchors[0]); ++i) {
            tdma_rx_scan_t job = {0}; tdma_pio_spi_phys_t phys = refresh_setup_variant(&job, 5u, false, 0u);
            phys.rx_scan_hint.candidate = anchors[i];
            const uint64_t cursor = s_tdma_pio_spi_rx_scan_produced;
            refresh_copy_count = 0u;
            tdma_pio_spi_phys_rx_scan_refresh(&phys, produced[i], TDMA_PIO_SPI_RX_DMA_WORD_MAX);
            assert(refresh_copy_count == 0u && tdma_rx_scan_state(&job) == TDMA_RX_SCAN_IDLE);
            assert(s_tdma_pio_spi_rx_scan_produced == cursor && phys.flight_overlay_alignment_samples == 1u);
        }
    } else return false;
    printf("training refresh %s passed\n", name);
    return true;
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
    if (test_refresh_case(argv[1])) return 0;
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
