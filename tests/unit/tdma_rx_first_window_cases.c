/* Included after the production prelaunch/service host integration. */
static uint32_t first_config = 1000u;
static tdma_rx_first_window_t first_snapshot(void)
{
    tdma_rx_first_window_t out;
    cut_copying = true; cut_read_hook = NULL; time_reads = 0u; read_delay = 0u;
    assert(tdma_pio_spi_phys_get_rx_first_window(&out));
    cut_copying = false;
    return out;
}

static void first_setup(uint32_t alignment, uint32_t bits)
{
    /* No new archive has been admitted during the old-observer teardown. */
    prelaunch_setup();
    first_config += 10u;
    selected_config.owner_config_seq = first_config;
    geometry_fixture.bound_config_seq = first_config;
    geometry_fixture.dma_byte_shift = alignment; geometry_fixture.dma_bit_shift = bits;
    physical.flight_physical_byte_count = geometry_fixture.physical_bytes = 173u;
    physical.flight_payload_size = 168u - TDMA_PIO_SPI_PACKET_HEADER_SIZE - TDMA_TRANSPORT_FRAME_HEADER_SIZE;
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence, 173u, tick_now));
    dma_bank.ch[4].transfer_count = (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF <<
        DMA_CH0_TRANS_COUNT_MODE_LSB) | s_tdma_pio_spi_rx_sequence.reload_words;
    s_tdma_pio_spi_rx_dma_channel = 4;
    first_ring_reads = 0u; first_copy_hook = NULL; cut_read_hook = NULL;
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_RX_RING_WORDS; ++i)
        first_ring[i] = __rev((i * 31u + 7u) & 255u) | 0x00012345u;
    tdma_rx_first_window_admit(&selected_config);
    const tdma_rx_first_window_t admitted = first_snapshot();
    assert(admitted.arm_epoch == 0u && admitted.flags == (TDMA_RX_FIRST_AVAILABLE |
        TDMA_RX_FIRST_DIAGNOSTIC_ONLY | TDMA_RX_FIRST_IDENTITY_UNPROVED | TDMA_RX_FIRST_PHYSICAL_FIRST_UNPROVED));
    for (size_t i = 0u; i < sizeof(admitted.raw); ++i) assert(admitted.raw[i] == 0u);
    /* This is the old installed persona cleanup in a new ARM. */
    tdma_pio_spi_phys_event_stop(&physical);
    tdma_pio_spi_phys_rx_first_window_persona_unload();
    const tdma_rx_first_window_t after = first_snapshot();
    assert(memcmp(&admitted, &after, sizeof(after)) == 0);
    tdma_pio_spi_phys_event_prepare(&physical, &selected_config);
    assert(finish());
    const tdma_rx_first_window_t active = first_snapshot();
    assert(active.flags & TDMA_RX_FIRST_PRELAUNCH_ACTIVE);
    assert(!(active.flags & TDMA_RX_FIRST_RAW_DONE));
    assert(active.arm_epoch == s_tdma_pio_spi_rx_arm_epoch && active.config_seq == first_config);
    assert(active.physical_frame_words == 173u && active.outer_packet_words == 168u);
    assert(active.envelope_end == alignment + 168u + (bits != 0u));
}

static void first_produced(uint32_t produced)
{
    assert(produced < s_tdma_pio_spi_rx_sequence.reload_words);
    dma_bank.ch[4].transfer_count = (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF <<
        DMA_CH0_TRANS_COUNT_MODE_LSB) | (s_tdma_pio_spi_rx_sequence.reload_words - produced);
}

static void first_check_raw(const tdma_rx_first_window_t *w, bool valid)
{
    assert(w->flags & TDMA_RX_FIRST_RAW_DONE);
    assert(w->flags & TDMA_RX_FIRST_RAW_ATTEMPTED);
    assert(!!(w->flags & TDMA_RX_FIRST_RAW_COPY_VALID) == valid);
    assert(w->raw_count == 173u && first_ring_reads == 173u);
    for (uint32_t i = 0u; i < w->raw_count; ++i) assert(w->raw[i] == ((i * 31u + 7u) & 255u));
    for (uint32_t i = w->raw_count; i < sizeof(w->raw); ++i) assert(w->raw[i] == 0u);
}

static void first_test_fixed_window_and_independent_components(void)
{
    const uint32_t alignments[] = {3u, 1u, 0u, 6u};
    const uint32_t shifts[] = {0u, 5u, 2u, 1u};
    for (unsigned c = 0; c < 4; ++c) {
        first_setup(alignments[c], shifts[c]);
        first_produced(172u); tdma_rx_first_window_raw(&physical);
        tdma_rx_first_window_t w = first_snapshot();
        assert(!(w.flags & TDMA_RX_FIRST_RAW_DONE) && w.raw_count == 0u && !first_ring_reads);
        first_produced(173u); push_first_event(); tdma_pio_spi_phys_event_service(&physical);
        w = first_snapshot(); first_check_raw(&w, true);
        assert(w.flags & TDMA_RX_FIRST_EVENT_VALID);
        assert(w.flags & TDMA_RX_FIRST_COLLECTION_TERMINAL);
        assert(w.event_word_mask == 31u && w.event_ordinal == 0u && w.event_sequence == 1u);
        assert(w.event_words[0] == UINT32_MAX - 31250u && w.event_words[1] == UINT32_MAX);
        assert(w.event_words[2] == UINT32_MAX - 31251u && w.event_words[3] == UINT32_MAX);
        assert(w.event_words[4] == 0x01000000u && w.produced_before == 173u && w.produced_after == 173u);
        assert(w.copy_before_ticks <= w.copy_after_ticks && w.observer_base_us == s_tdma_event_base_us);
        assert(w.first_reason == (c == 3u ? TDMA_RX_FIRST_CROSSES_WINDOW : TDMA_RX_FIRST_NONE));
        assert(!!(w.flags & TDMA_RX_FIRST_ENVELOPE_CROSSES_WINDOW) == (c == 3u));
        memset(first_ring, 0, sizeof(first_ring)); first_produced(999u);
        tdma_pio_spi_phys_event_service(&physical);
        const tdma_rx_first_window_t same = first_snapshot();
        assert(memcmp(&w, &same, sizeof(w)) == 0); /* Never a latest-window recorder. */
        tdma_pio_spi_phys_event_stop(&physical);
        const tdma_rx_first_window_t stopped = first_snapshot();
        assert(stopped.flags & TDMA_RX_FIRST_RETIRED);
        assert(stopped.flags & TDMA_RX_FIRST_EVENT_VALID);
        assert(memcmp(w.raw, stopped.raw, sizeof(w.raw)) == 0);
        assert(w.rx_elapsed_cycles == stopped.rx_elapsed_cycles && w.event_sequence == stopped.event_sequence);
        tdma_rx_first_window_retire(TDMA_RX_FIRST_STOP_BEFORE_COMPLETE, true);
        assert(first_snapshot().flags & TDMA_RX_FIRST_STOPPED);
    }
}

static uint32_t first_post_kind;
static void first_post_mutation(void)
{
    switch (first_post_kind) {
    case 0: first_produced(1023u); break;
    case 1: first_produced(1024u); break;
    case 2: first_produced(172u); break;
    case 3: dma_bank.ch[4].transfer_count &= ~DMA_CH0_TRANS_COUNT_MODE_BITS; break;
    case 4: dma_bank.ch[4].ctrl_trig = 0u; break;
    case 5: dma_bank.ch[4].ctrl_trig |= DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS; break;
    case 6: rx_bank.fdebug |= 1u << (2u + PIO_FDEBUG_RXSTALL_LSB); break;
    case 7: rx_bank.ctrl &= ~(1u << 2u); break;
    case 8: tick_now += s_tdma_pio_spi_rx_sequence.reload_words; break;
    case 9: ++s_tdma_pio_spi_rx_arm_epoch; break;
    case 10: geometry_binding_valid = false; break;
    case 11: ++s_tdma_event_geometry.bound_map_generation; break;
    case 12: ++s_tdma_pio_spi_rx_sequence.observation_epoch; break;
    case 13: dma_bank.ch[4].ctrl_trig ^= 1u << 3u; break;
    default: assert(false);
    }
}

static void first_test_post_observation_and_no_parser_effect(void)
{
    for (first_post_kind = 0u; first_post_kind < 14u; ++first_post_kind) {
        first_setup(3u, 0u); first_produced(173u);
        const tdma_rx_dma_counter_t counter = s_tdma_pio_spi_rx_sequence;
        const uint64_t scan = s_tdma_pio_spi_rx_scan_produced, capture_id = s_tdma_pio_spi_rx_capture_id;
        const uint32_t drops = physical.snapshot.rx_observation_drop_count;
        first_copy_hook = first_post_mutation;
        tdma_rx_first_window_raw(&physical);
        const tdma_rx_first_window_t w = first_snapshot(); first_check_raw(&w, first_post_kind == 0u);
        if (first_post_kind == 0u) assert(w.produced_after == 1023u);
        else if (first_post_kind == 1u) assert(w.produced_after == 1024u && w.raw_reason == TDMA_RX_FIRST_RAW_OVERWRITTEN);
        else assert(w.raw_reason != TDMA_RX_FIRST_NONE);
        assert(w.dma_count_before != 0u && w.copy_after_ticks >= w.copy_before_ticks);
        assert(scan == s_tdma_pio_spi_rx_scan_produced && capture_id == s_tdma_pio_spi_rx_capture_id);
        assert(drops == physical.snapshot.rx_observation_drop_count);
        if (first_post_kind != 12u) assert(memcmp(&counter, &s_tdma_pio_spi_rx_sequence, sizeof(counter)) == 0);
        tdma_rx_first_window_raw(&physical);
        const tdma_rx_first_window_t same = first_snapshot(); assert(memcmp(&w, &same, sizeof(w)) == 0);
    }
}

static void first_test_too_late_and_initial_failures(void)
{
    for (unsigned which = 0u; which < 5u; ++which) {
        first_setup(3u, 0u); first_produced(which == 0u ? 1024u : 173u);
        if (which == 1u) dma_bank.ch[4].ctrl_trig = 0u;
        if (which == 2u) dma_bank.ch[4].transfer_count &= ~DMA_CH0_TRANS_COUNT_MODE_BITS;
        if (which == 3u) tick_now += s_tdma_pio_spi_rx_sequence.reload_words;
        if (which == 4u) s_tdma_pio_spi_rx_dma_channel = -1;
        tdma_rx_first_window_raw(&physical);
        const tdma_rx_first_window_t w = first_snapshot();
        assert(w.flags & TDMA_RX_FIRST_RAW_DONE);
        assert(!(w.flags & TDMA_RX_FIRST_RAW_ATTEMPTED) && w.raw_count == 0u && !first_ring_reads);
        assert(w.raw_reason != TDMA_RX_FIRST_NONE);
        first_produced(173u); s_tdma_pio_spi_rx_dma_channel = 4;
        tdma_rx_first_window_raw(&physical);
        const tdma_rx_first_window_t same = first_snapshot(); assert(memcmp(&w, &same, sizeof(w)) == 0);
    }
}

static void first_fault_after_feed(void) { bank.fdebug |= 1u << TDMA_EVENT_RX_SM; }
static void first_test_first_fifo_and_event_rejection(void)
{
    first_setup(3u, 0u); first_produced(173u); push_first_event();
    bank.level[1] = 1u; bank.level[2] = 1u; bank.level[3] = 1u;
    tdma_pio_spi_phys_event_service(&physical);
    tdma_rx_first_window_t w = first_snapshot();
    assert(w.event_word_mask == 21u && !(w.flags & TDMA_RX_FIRST_EVENT_DONE));
    assert(w.flags & TDMA_RX_FIRST_RAW_COPY_VALID);
    bank.level[1] = 1u; bank.level[2] = 1u; bank.level[3] = 0u;
    tdma_pio_spi_phys_event_service(&physical);
    w = first_snapshot(); assert(w.event_word_mask == 31u && (w.flags & TDMA_RX_FIRST_EVENT_VALID));
    const uint32_t first_rx = w.event_words[0];
    bank.fdebug |= 1u << TDMA_EVENT_RX_SM;
    tdma_pio_spi_phys_event_service(&physical);
    w = first_snapshot(); assert(w.flags & TDMA_RX_FIRST_RETIRED);
    assert(w.event_words[0] == first_rx && (w.flags & TDMA_RX_FIRST_EVENT_VALID));

    for (unsigned failure = 0u; failure < 3u; ++failure) {
        first_setup(3u, 0u); first_produced(173u); push_first_event();
        if (failure == 0u) {
            fifo[1][2] = 1u; fifo[1][3] = 1u;
            fifo[2][2] = 1u; fifo[2][3] = 1u;
            fifo[3][1] = 0x03000000u;
            bank.level[1] = bank.level[2] = 4u; bank.level[3] = 2u;
        } else if (failure == 1u) first_copy_hook = first_fault_after_feed;
        else bank.fdebug |= 1u << TDMA_EVENT_RX_SM;
        tdma_pio_spi_phys_event_service(&physical);
        w = first_snapshot();
        assert((w.flags & TDMA_RX_FIRST_EVENT_DONE) && !(w.flags & TDMA_RX_FIRST_EVENT_VALID));
        if (failure != 2u) {
            assert(w.event_word_mask == 31u && w.event_words[0] == UINT32_MAX - 31250u);
            assert(w.flags & TDMA_RX_FIRST_RAW_COPY_VALID);
        }
        assert(w.first_reason != TDMA_RX_FIRST_NONE);
        bank.fdebug = 0u; tdma_pio_spi_phys_event_service(&physical);
        const tdma_rx_first_window_t same = first_snapshot(); assert(memcmp(&w, &same, sizeof(w)) == 0);
    }
    /* An observer output with missing ordinal zero never substitutes ordinal1. */
    first_setup(3u, 0u);
    const tdma_event_batch_t words = {.epoch=s_tdma_event_observer.epoch,
        .count={2u,2u,1u}, .words={{1u,2u},{3u,4u},{5u}}};
    tdma_rx_first_window_words(&words);
    s_tdma_event_records[0] = (tdma_event_record_t){.epoch=s_tdma_event_observer.epoch, .ordinal=1u};
    tdma_rx_first_window_event(&physical, 1u);
    w = first_snapshot(); assert(w.first_reason == TDMA_RX_FIRST_ORDINAL_MISSING);
    assert(!(w.flags & TDMA_RX_FIRST_EVENT_VALID));
}

static void first_reader_rewrite(void)
{
    s_tdma_rx_first_guard += 2u;
    s_tdma_rx_first_window.event_sequence = 777u;
    if (cut_fences == 1u) cut_read_hook = NULL;
}
static void first_reader_continuous(void) { s_tdma_rx_first_guard += 2u; }
static void first_test_getter_and_lifecycle(void)
{
    first_setup(3u, 0u);
    tdma_pio_spi_phys_event_stop(&physical);
    tdma_rx_first_window_t w = first_snapshot();
    assert((w.flags & TDMA_RX_FIRST_COLLECTION_TERMINAL) && w.first_reason == TDMA_RX_FIRST_STOP_BEFORE_COMPLETE);
    tdma_rx_first_window_admit(&selected_config);
    tdma_rx_first_window_t same = first_snapshot(); assert(memcmp(&w, &same, sizeof(w)) == 0);
    ++selected_config.owner_config_seq; tdma_rx_first_window_admit(&selected_config);
    tdma_rx_first_window_arm_failed(); w = first_snapshot();
    assert(w.arm_epoch == 0u && w.observer_epoch == 0u && w.raw_count == 0u);
    assert(w.flags & TDMA_RX_FIRST_COLLECTION_TERMINAL);
    assert(w.first_reason == TDMA_RX_FIRST_PRELAUNCH_REJECTED);
    tdma_rx_first_window_admit(&selected_config);
    same = first_snapshot(); assert(memcmp(&w, &same, sizeof(w)) == 0);
    tdma_pio_spi_phys_rx_first_window_persona_unload();
    memset(&w, 0xff, sizeof(w)); assert(!tdma_pio_spi_phys_get_rx_first_window(&w));
    const tdma_rx_first_window_t zero = {0}; assert(memcmp(&w, &zero, sizeof(w)) == 0);

    first_setup(3u, 0u);
    for (unsigned failure = 0u; failure < 4u; ++failure) {
        const uint32_t guard = s_tdma_rx_first_guard;
        cut_copying = true; time_reads = 0u; read_delay = failure == 2u ? 1001u : 0u;
        cut_fences = 0u; cut_read_hook = failure == 1u ? first_reader_continuous : NULL;
        if (failure == 0u) s_tdma_rx_first_guard |= 1u;
        if (failure == 3u) read_delay = UINT64_MAX;
        memset(&w, 0xff, sizeof(w));
        assert(!tdma_pio_spi_phys_get_rx_first_window(&w));
        assert(memcmp(&w, &zero, sizeof(w)) == 0);
        assert(cut_fences <= 3u && interrupt_depth == 0u);
        s_tdma_rx_first_guard = guard; cut_copying = false;
    }
    cut_copying = true; time_reads = 0u; read_delay = 0u; cut_fences = 0u; cut_read_hook = first_reader_rewrite;
    assert(tdma_pio_spi_phys_get_rx_first_window(&w) && w.event_sequence == 777u && cut_fences == 2u);
    cut_copying = false; cut_read_hook = NULL;
    assert(!tdma_pio_spi_phys_get_rx_first_window(NULL));
    s_tdma_rx_first_guard = UINT32_MAX - 1u;
    ++selected_config.owner_config_seq; tdma_rx_first_window_admit(&selected_config);
    assert(s_tdma_rx_first_guard == UINT32_MAX);
    assert(!tdma_pio_spi_phys_get_rx_first_window(&w) && memcmp(&w, &zero, sizeof(w)) == 0);
    s_tdma_rx_first_guard = 0u;
}

static void first_test_real_admission_and_persona_lifetime(void)
{
    first_setup(3u,0u);
    ++selected_config.owner_config_seq; tdma_rx_first_window_admit(&selected_config);
    s_tdma_event_observer.state = TDMA_EVENT_INVALID;
    s_tdma_event_observer.reason = TDMA_EVENT_SEQUENCE;
    s_tdma_event_observer.fault_bits = TDMA_EVENT_FAULT_STALL;
    /* Actual STOP prefix runs before the hardware/worker ACK result. */
    assert(tdma_pio_spi_phys_disarm(&physical));
    tdma_rx_first_window_t sparse = first_snapshot();
    assert(sparse.flags & TDMA_RX_FIRST_COLLECTION_TERMINAL);
    assert(!(sparse.flags & TDMA_RX_FIRST_STOPPED));
    assert(sparse.arm_epoch == 0u && sparse.observer_epoch == 0u);
    assert(sparse.event_state == 0u && sparse.event_reason == 0u && sparse.event_fault_bits == 0u);
    tdma_rx_first_window_admit(&selected_config);
    tdma_rx_first_window_t same_sparse = first_snapshot(); assert(memcmp(&sparse,&same_sparse,sizeof(sparse)) == 0);
    assert(!first_physical_stop_tail(false,true));
    assert(!(first_snapshot().flags & TDMA_RX_FIRST_STOPPED));
    assert(first_physical_stop_tail(true,true));
    assert(first_snapshot().flags & TDMA_RX_FIRST_STOPPED);
    for (unsigned refusal = 0u; refusal < 3u; ++refusal) {
        first_setup(3u,0u);
        const uint32_t old = first_snapshot().config_seq;
        ++selected_config.owner_config_seq;
        s_geometry = (tdma_frozen_geometry_snapshot_t){.generation=37u,
            .state=refusal == 0u ? TDMA_GEOMETRY_CANDIDATE : TDMA_GEOMETRY_FROZEN};
        if (refusal == 1u) ++selected_config.geometry_generation;
        const bool accepted = tdma_pio_spi_phys_geometry_arm_requested(&physical,&selected_config);
        assert(accepted == (refusal == 2u));
        if (accepted) tdma_geometry_arm_failed(); /* Physical early ARM refusal. */
        const tdma_rx_first_window_t w = first_snapshot();
        assert(w.config_seq != old && w.arm_epoch == 0u && w.observer_epoch == 0u);
        assert(w.first_reason == TDMA_RX_FIRST_PRELAUNCH_REJECTED && (w.flags & TDMA_RX_FIRST_COLLECTION_TERMINAL));
        tdma_rx_first_window_admit(&selected_config);
        const tdma_rx_first_window_t same = first_snapshot(); assert(memcmp(&w,&same,sizeof(w)) == 0);
    }
    for (unsigned scenario = 0u; scenario < 6u; ++scenario) {
        first_setup(3u,0u); tdma_pio_spi_phys_event_stop(&physical);
        const tdma_rx_first_window_t before = first_snapshot();
        bool yes=true; int channel=-1; uint32_t offset=0u;
        tdma_pio_spi_program_manager_t manager = {.program_persona=&s_tdma_pio_spi_program_persona,
            .sms_claimed=&yes,.flight_sms_claimed=&yes,.maintenance_resources_claimed=&yes,
            .tx_dma_channel=&channel,.rx_dma_channel=&channel,.command_dma_channel=&channel,
            .event_sequence_offset=&offset,.event_counter_offset=&offset};
        tdma_pio_spi_persona_fsm_init(&manager.lifecycle,s_tdma_pio_spi_program_persona);
        first_resources = scenario != 5u; first_quiesced = scenario != 2u;
        first_load = scenario != 4u; first_unloads = first_loads = first_releases = 0u;
        const tdma_pio_spi_program_persona_t requested = scenario == 0u ? 999u :
            scenario == 1u ? TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER : TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL;
        const bool accepted = first_real_select(&manager,&physical,requested);
        assert(accepted == (scenario == 1u || scenario == 3u));
        if (scenario < 3u) {
            const tdma_rx_first_window_t same = first_snapshot(); assert(memcmp(&before,&same,sizeof(before)) == 0);
            assert(first_unloads == 0u);
        } else {
            tdma_rx_first_window_t w; const tdma_rx_first_window_t zero={0};
            assert(!tdma_pio_spi_phys_get_rx_first_window(&w) && memcmp(&w,&zero,sizeof(w)) == 0);
            assert(first_unloads == 5u);
            assert(s_tdma_pio_spi_program_persona == (scenario == 3u ?
                TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL : TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER));
            if (scenario >= 4u) assert(first_releases >= 1u);
        }
    }
}

int main(void)
{
    assert(prelaunch_regression_main() == 0);
    first_test_fixed_window_and_independent_components();
    first_test_post_observation_and_no_parser_effect();
    first_test_too_late_and_initial_failures();
    first_test_first_fifo_and_event_rejection();
    first_test_getter_and_lifecycle();
    first_test_real_admission_and_persona_lifetime();
    assert(offsetof(tdma_rx_first_window_t, raw) == 228u);
    assert(TDMA_RX_FIRST_WINDOW_RAW_CAPACITY == (PROJECT_NODE_CAPACITY == 6 ? 243u : 307u));
    assert(sizeof(tdma_rx_first_window_t) == (PROJECT_NODE_CAPACITY == 6 ? 472u : 536u));
    puts("first-window: all production groups passed");
    return 0;
}
