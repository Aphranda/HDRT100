"""Execute complete live record retention through the real observer lifecycle."""
from pathlib import Path

from test_tdma_event_recovery import recovery_source
from test_tdma_rx_event_candidate import run


def test_complete_live_record_anchor_and_retirement(tmp_path: Path) -> None:
    source = recovery_source(tmp_path)
    pos = source.rindex("int main(void)")
    source = source[:pos] + source[pos:].replace("int main(void)", "int recovery_regression_main(void)", 1)
    begin = source.index("/* EVENT_LIVE_STORAGE_BEGIN")
    end = source.index("#define __atomic_load_n", begin)
    block = source[begin:end].replace("        __atomic_thread_fence(__ATOMIC_ACQ_REL);",
                                     "        live_read_fence();")
    source = source[:begin] + r'''
static void (*live_read_hook)(void);
static void live_read_fence(void) {
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (live_read_hook != NULL) live_read_hook();
}
''' + block + source[end:]
    assert "live event: 10 production groups passed" in run(
        tmp_path, source + CASES, "event_live", enabled=True)


CASES = r'''
static tdma_pio_spi_event_live_snapshot_t live_snapshot(void) {
    tdma_pio_spi_event_live_snapshot_t out;
    const unsigned checks = live_clock_checks, ticks = live_tick_reads;
    assert(tdma_pio_spi_phys_event_get_live_snapshot(&physical, &out));
    assert(checks == live_clock_checks && ticks == live_tick_reads); /* SRAM-only. */
    return out;
}
static void live_setup(void) {
    live_read_hook = NULL; live_clock_available = true; live_tick_fail_at = live_clock_fail_at = 0u;
    live_tick_step = 5u; live_tick_now = UINT64_C(0x1234567800000000);
    recovery_setup();
    memset(s_tdma_event_live_words, 0, sizeof(s_tdma_event_live_words));
    s_tdma_event_live_guard = 0u;
}
static void live_same_body(const tdma_pio_spi_event_live_snapshot_t *a,
                           const tdma_pio_spi_event_live_snapshot_t *b) {
    assert(memcmp(a, b, offsetof(tdma_pio_spi_event_live_snapshot_t, flags)) == 0);
}
static void live_good(const tdma_pio_spi_event_live_snapshot_t *a) {
    const uint32_t required = TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID;
    assert(a->flags == required && a->arm_epoch == s_tdma_pio_spi_rx_arm_epoch);
    assert(a->tick_hz == 125000000u && a->timer1_enable_after - a->timer1_enable_before == 5u);
    assert(a->record.diagnostic_only && a->record.physical_first_unproved && a->record.identity_unproved);
    assert(!a->record.timestamp_valid && !a->record.dpll_eligible);
}
static void test_live_empty_and_zero_sequence(void) {
    live_setup();
    tdma_pio_spi_event_live_snapshot_t a = live_snapshot();
    assert(a.flags == 0u && a.arm_epoch == 0u);
    accept_event(0u, 0u); a = live_snapshot(); live_good(&a);
    assert(a.record.sequence == 0u && a.record.ordinal == 0u);
    assert(a.record.epoch == s_tdma_event_observer.epoch);
    assert(a.timer1_enable_before != a.record.start_bounds.lo);
    assert(a.record.start_bounds.lo == s_tdma_event_records[0].start_bounds.lo);
    assert(s_tdma_event_snapshot.candidate.query_count == 0u); /* No DMA candidate prerequisite. */
}
static void test_live_whole_last_record(void) {
    live_setup(); accept_event(0u, 900u); load_event(1u, 901u);
    const uint32_t raw = UINT32_MAX - 31250u - 93750u * 2u;
    fifo[1][2] = raw; fifo[1][3] = UINT32_MAX - 2u;
    fifo[2][2] = raw - 1u; fifo[2][3] = UINT32_MAX - 2u;
    fifo[3][1] = recovery_wire(902u);
    bank.level[1] = bank.level[2] = 4u; bank.level[3] = 2u; clock_us += 1501u;
    tdma_pio_spi_phys_event_service(&physical);
    tdma_pio_spi_event_live_snapshot_t a = live_snapshot(); live_good(&a);
    assert(a.record.sequence == 902u && a.record.ordinal == 2u);
    assert(a.record.raw_rx == raw && a.record.raw_tx == raw - 1u);
    assert(a.record.raw_rx != s_tdma_event_snapshot.fifo_rx_first);
    assert(memcmp(&a.record, &s_tdma_event_records[1], sizeof(a.record)) == 0);
}
static void test_live_final_fault_preserves_last(void) {
    live_setup(); accept_event(0u, 11u);
    const tdma_pio_spi_event_live_snapshot_t old = live_snapshot();
    /* Inject after the post-harvest FDEBUG read; the final check sees it. */
    load_event(1u, 12u); fault_at_hz_read = hz_reads + 2u;
    tdma_pio_spi_phys_event_service(&physical); fault_at_hz_read = 0u;
    const tdma_pio_spi_event_live_snapshot_t a = live_snapshot();
    assert(s_tdma_event_observer.state == TDMA_EVENT_INVALID);
    live_same_body(&a, &old);
    assert((a.flags & TDMA_EVENT_LIVE_ACTIVE) == 0u && (a.flags & TDMA_EVENT_LIVE_INVALID));
    assert(a.flags & TDMA_EVENT_LIVE_ANCHOR_VALID);
}
static void test_live_recovery_new_anchor(void) {
    live_setup(); reject_duplicate();
    const tdma_pio_spi_event_live_snapshot_t old = live_snapshot();
    assert(old.flags & TDMA_EVENT_LIVE_INVALID);
    reset_only_phase();
    tdma_pio_spi_event_live_snapshot_t a = live_snapshot(); live_same_body(&a, &old);
    live_tick_now += UINT64_C(0x100000000);
    enable_only_phase(); a = live_snapshot(); live_same_body(&a, &old);
    assert(!(a.flags & TDMA_EVENT_LIVE_ACTIVE)); /* Enable alone publishes no replacement. */
    accept_event(0u, 70001u); a = live_snapshot(); live_good(&a);
    assert(a.record.epoch > old.record.epoch && a.record.sequence == 70001u);
    assert(a.timer1_enable_before > old.timer1_enable_after);
    assert(a.arm_epoch == old.arm_epoch && s_tdma_event_snapshot.candidate.query_count == 0u);
}
static void test_live_stop_and_arm_history(void) {
    live_setup(); accept_event(0u, 51u);
    const tdma_pio_spi_event_live_snapshot_t old = live_snapshot();
    tdma_pio_spi_phys_event_stop(&physical);
    tdma_pio_spi_event_live_snapshot_t a = live_snapshot(); live_same_body(&a, &old);
    assert((a.flags & (TDMA_EVENT_LIVE_STOP | TDMA_EVENT_LIVE_ANCHOR_VALID)) ==
           (TDMA_EVENT_LIVE_STOP | TDMA_EVENT_LIVE_ANCHOR_VALID));
    assert(!(a.flags & TDMA_EVENT_LIVE_ACTIVE));
    const tdma_pio_spi_event_live_snapshot_t stopped = a;
    physical.armed = false; live_clock_available = false;
    for (unsigned i = 0u; i < 8u; ++i) tdma_pio_spi_phys_event_service(&physical);
    a = live_snapshot(); assert(memcmp(&a, &stopped, sizeof(a)) == 0);
    live_clock_available = true;
    /* The common physical ARM entry retires before any later admission error. */
    tdma_rx_start_cut_arm_begin(); a = live_snapshot(); live_same_body(&a, &old);
    assert(a.flags & TDMA_EVENT_LIVE_ARM);
    assert(s_tdma_event_live_anchor.epoch == 0u);
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL;
    tdma_pio_spi_phys_event_prepare(&physical, &selected_config);
    a = live_snapshot(); live_same_body(&a, &old);
}
static void test_live_clock_cancel_is_sticky(void) {
    live_setup(); accept_event(0u, 70u);
    const tdma_pio_spi_event_live_snapshot_t old = live_snapshot();
    const uint32_t enabled = bank.ctrl;
    live_clock_available = false; tdma_pio_spi_phys_event_service(&physical);
    tdma_pio_spi_event_live_snapshot_t a = live_snapshot(); live_same_body(&a, &old);
    assert((a.flags & TDMA_EVENT_LIVE_CLOCK) && !(a.flags & TDMA_EVENT_LIVE_ACTIVE));
    assert((a.flags & TDMA_EVENT_LIVE_ANCHOR_VALID) && bank.ctrl == enabled && physical.armed);
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE);
    live_clock_available = true; accept_event(1u, 71u);
    a = live_snapshot(); live_same_body(&a, &old); assert(!(a.flags & TDMA_EVENT_LIVE_ACTIVE));
    live_setup(); accept_event(0u, 80u);
    const tdma_pio_spi_event_live_snapshot_t before = live_snapshot();
    load_event(1u, 81u); live_clock_fail_at = live_clock_checks + 2u;
    tdma_pio_spi_phys_event_service(&physical); a = live_snapshot();
    live_same_body(&a, &before);
    assert((a.flags & TDMA_EVENT_LIVE_CLOCK) && !(a.flags & TDMA_EVENT_LIVE_ACTIVE));
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && physical.armed);
}
static void test_live_unavailable_anchor(void) {
    for (unsigned which = 0u; which < 3u; ++which) {
        live_setup();
        tdma_pio_spi_phys_event_prepare(&physical, &selected_config);
        if (which < 2u) live_tick_fail_at = live_tick_reads + which + 1u;
        else live_tick_now = UINT64_MAX - 2u; /* Nonmonotonic bracket. */
        tdma_event_start(&physical); accept_event(0u, 85u);
        const tdma_pio_spi_event_live_snapshot_t a = live_snapshot();
        assert((a.flags & (TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE |
            TDMA_EVENT_LIVE_ANCHOR_UNAVAILABLE)) == (TDMA_EVENT_LIVE_RETAINED |
            TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_UNAVAILABLE));
        assert(!(a.flags & TDMA_EVENT_LIVE_ANCHOR_VALID));
        assert(!a.record.dpll_eligible && a.record.sequence == 85u);
    }
}
static void test_live_idle_binding_cancel(void) {
    live_setup(); accept_event(0u, 99u);
    const tdma_pio_spi_event_live_snapshot_t old = live_snapshot();
    ++s_tdma_pio_spi_rx_arm_epoch; tdma_pio_spi_phys_event_service(&physical);
    const tdma_pio_spi_event_live_snapshot_t a = live_snapshot(); live_same_body(&a, &old);
    assert((a.flags & TDMA_EVENT_LIVE_BINDING) && !(a.flags & TDMA_EVENT_LIVE_ACTIVE));
}
static unsigned live_mutations, live_mutation_mode;
static void mutate_live(void) {
    ++live_mutations;
    if (live_mutation_mode == 1u && live_mutations > 1u) return;
    /* Advance a full publication generation, never patch a read destination. */
    s_tdma_event_live_guard += 2u;
}
static void test_live_guard_and_copy_lifetime(void) {
    live_setup(); accept_event(0u, 100u);
    tdma_pio_spi_event_live_snapshot_t result, sentinel;
    memset(&sentinel, 0xa5, sizeof(sentinel)); result = sentinel;
    assert(!tdma_pio_spi_phys_event_get_live_snapshot(NULL, &result));
    assert(!tdma_pio_spi_phys_event_get_live_snapshot(&physical, NULL));
    assert(memcmp(&result, &sentinel, sizeof(result)) == 0);
    s_tdma_event_live_guard |= 1u;
    assert(!tdma_pio_spi_phys_event_get_live_snapshot(&physical, &result));
    assert(memcmp(&result, &sentinel, sizeof(result)) == 0);
    s_tdma_event_live_guard &= ~1u;
    live_mutations = 0u; live_mutation_mode = 1u; live_read_hook = mutate_live;
    assert(tdma_pio_spi_phys_event_get_live_snapshot(&physical, &result));
    assert(live_mutations == 2u && result.record.sequence == 100u);
    result = sentinel; live_mutations = 0u; live_mutation_mode = 2u;
    assert(!tdma_pio_spi_phys_event_get_live_snapshot(&physical, &result));
    assert(live_mutations == 3u && memcmp(&result, &sentinel, sizeof(result)) == 0);
    live_read_hook = NULL;
    for (unsigned irq = 0u; irq < 2u; ++irq) {
        interrupt_mask = irq; copying = true; time_reads = 0u; read_delay = 1000u;
        assert(tdma_pio_spi_phys_event_get_live_snapshot(&physical, &result));
        assert(interrupt_mask == irq);
        result = sentinel; time_reads = 0u; read_delay = 1001u;
        assert(!tdma_pio_spi_phys_event_get_live_snapshot(&physical, &result));
        assert(interrupt_mask == irq && memcmp(&result, &sentinel, sizeof(result)) == 0);
        time_reads = 0u; read_delay = UINT64_MAX;
        assert(!tdma_pio_spi_phys_event_get_live_snapshot(&physical, &result));
        assert(interrupt_mask == irq && memcmp(&result, &sentinel, sizeof(result)) == 0);
        copying = false;
    }
    interrupt_mask = 0u; read_delay = 0u;
    assert(interrupt_depth == 0u && interrupt_saves == interrupt_restores);
}
static void test_live_failed_batch_is_not_partial(void) {
    live_setup(); accept_event(0u, 1u); const tdma_pio_spi_event_live_snapshot_t old = live_snapshot();
    load_event(1u, 2u);
    const uint32_t raw = UINT32_MAX - 31250u - 93750u * 2u;
    fifo[1][2] = raw; fifo[1][3] = UINT32_MAX - 2u;
    fifo[2][2] = raw - 1u; fifo[2][3] = UINT32_MAX - 2u; fifo[3][1] = recovery_wire(2u);
    bank.level[1] = bank.level[2] = 4u; bank.level[3] = 2u; clock_us += 1501u;
    tdma_pio_spi_phys_event_service(&physical);
    const tdma_pio_spi_event_live_snapshot_t a = live_snapshot(); live_same_body(&a, &old);
    assert((a.flags & TDMA_EVENT_LIVE_INVALID) && !(a.flags & TDMA_EVENT_LIVE_ACTIVE));
}
int main(void) {
    test_live_empty_and_zero_sequence(); test_live_whole_last_record();
    test_live_final_fault_preserves_last(); test_live_recovery_new_anchor();
    test_live_stop_and_arm_history(); test_live_clock_cancel_is_sticky();
    test_live_unavailable_anchor(); test_live_idle_binding_cancel();
    test_live_guard_and_copy_lifetime(); test_live_failed_batch_is_not_partial();
    puts("live event: 10 production groups passed"); return 0;
}
'''
