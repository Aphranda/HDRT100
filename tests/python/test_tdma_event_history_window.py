"""Execute the real guarded history bridge with owner lifecycle/race seams."""
from pathlib import Path

from test_tdma_event_recovery import recovery_source
from test_tdma_rx_event_candidate import run
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_history_window_cursor_anchor_and_races(tmp_path: Path) -> None:
    source = recovery_source(tmp_path)
    pos = source.rindex("int main(void)")
    source = source[:pos] + source[pos:].replace("int main(void)", "int recovery_regression_main(void)", 1)
    production = (ROOT / "components/tdma/src/tdma_pio_spi_phys_event.inc").read_text(encoding="utf-8")
    block = production[production.index("/* EVENT_HISTORY_WINDOW_BEGIN"):production.index("/* EVENT_HISTORY_WINDOW_END */")]
    assert block.count("__atomic_thread_fence(__ATOMIC_ACQ_REL);") == 1
    block = block.replace("__atomic_thread_fence(__ATOMIC_ACQ_REL);", "window_read_fence();")
    owner = (ROOT / "components/tdma/src/tdma_runtime_owner.c").read_text(encoding="utf-8")
    wrapper = """
static bool s_tdma_runtime_owner_initialized;
#define s_tdma_pio_spi_phys physical
tdma_event_window_result_t tdma_runtime_owner_copy_event_history_window(
    uint32_t expected_observer_epoch, uint64_t next_ordinal,
    tdma_pio_spi_event_window_t *out) {
""" + c_definition_body(owner, "tdma_runtime_owner_copy_event_history_window") + "}\n"
    fixture = r'''
#undef __atomic_load_n
#undef __atomic_store_n
#undef __atomic_thread_fence
static void (*window_hook)(void);
static void window_read_fence(void) {
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (window_hook != NULL) window_hook();
}
'''
    assert "history window: 7 production groups passed" in run(
        tmp_path, source + fixture + block + wrapper + CASES, "history_window", enabled=True)


CASES = r'''
static void window_setup(void) {
    window_hook = NULL; live_clock_available = true;
    live_tick_fail_at = live_clock_fail_at = 0u;
    live_tick_step = 5u; live_tick_now = UINT64_C(0x1234567800000000);
    recovery_setup();
    memset(s_tdma_event_live_words, 0, sizeof(s_tdma_event_live_words));
    s_tdma_event_live_guard = 0u;
    s_tdma_runtime_owner_initialized = true;
}
static tdma_pio_spi_event_window_t window_copy(uint32_t epoch, uint64_t cursor) {
    tdma_pio_spi_event_window_t out;
    const unsigned checks = live_clock_checks, ticks = live_tick_reads;
    const unsigned hz = hz_reads, gpio = gpio_reads, fifo_count = fifo_reads, levels = fifo_level_reads;
    assert(tdma_runtime_owner_copy_event_history_window(epoch, cursor, &out) == TDMA_EVENT_WINDOW_OK);
    assert(checks == live_clock_checks && ticks == live_tick_reads && hz == hz_reads && gpio == gpio_reads);
    assert(fifo_count == fifo_reads && levels == fifo_level_reads);
    assert(out.diagnostic_only && out.physical_first_unproved && out.identity_unproved);
    assert(!out.timestamp_valid && !out.dpll_eligible);
    assert(out.observer_epoch == s_tdma_event_history.epoch && out.tick_hz == s_tdma_event_hz);
    assert(out.arm_epoch == s_tdma_pio_spi_rx_arm_epoch);
    assert(out.timer1_enable_before != s_tdma_event_history.initial_start.lo);
    assert(out.timer1_enable_after - out.timer1_enable_before == 5u);
    assert(out.count <= TDMA_EVENT_HISTORY_WINDOW_CAPACITY);
    return out;
}
static void window_reject(uint32_t epoch, uint64_t cursor, tdma_event_window_result_t expected) {
    tdma_pio_spi_event_window_t result, sentinel;
    memset(&sentinel, 0xa5, sizeof(sentinel)); result = sentinel;
    assert(tdma_runtime_owner_copy_event_history_window(epoch, cursor, &result) == expected);
    assert(memcmp(&result, &sentinel, sizeof(result)) == 0);
}
static void test_window_arguments_and_binding(void) {
    window_setup();
    window_reject(0u, 0u, TDMA_EVENT_WINDOW_ANCHOR_UNAVAILABLE);
    window_reject(0u, 1u, TDMA_EVENT_WINDOW_BAD_ARGUMENT);
    window_reject(1u, (uint64_t)UINT32_MAX + 2u, TDMA_EVENT_WINDOW_BAD_ARGUMENT);
    assert(tdma_runtime_owner_copy_event_history_window(0u, 0u, NULL) == TDMA_EVENT_WINDOW_BAD_ARGUMENT);
    tdma_pio_spi_event_window_t out, sentinel; memset(&sentinel, 0xaa, sizeof(sentinel)); out = sentinel;
    assert(tdma_pio_spi_phys_event_copy_history_window(NULL, 0u, 0u, &out) == TDMA_EVENT_WINDOW_BAD_ARGUMENT);
    assert(memcmp(&out, &sentinel, sizeof(out)) == 0);
    s_tdma_runtime_owner_initialized = false;
    window_reject(0u, 0u, TDMA_EVENT_WINDOW_UNAVAILABLE);
    s_tdma_runtime_owner_initialized = true;
    accept_event(0u, 0u); out = window_copy(0u, 0u);
    assert(out.count == 1u && out.records[0].sequence == 0u && out.next_ordinal == 1u);
    window_reject(out.observer_epoch, 2u, TDMA_EVENT_WINDOW_BAD_ARGUMENT);
    window_reject(out.observer_epoch + 1u, 0u, TDMA_EVENT_WINDOW_EPOCH_CHANGED);
}
static void test_window_chunks_eviction_and_sequence_wrap(void) {
    window_setup(); accept_event(0u, UINT32_MAX - 1u);
    for (uint32_t i = 1u; i < 12u; ++i) retain((uint32_t)(UINT32_MAX - 1u + i));
    /* LIVE remains ordinal zero: its immutable anchor is valid for this epoch. */
    tdma_pio_spi_event_window_t out = window_copy(0u, 0u);
    const uint32_t epoch = out.observer_epoch;
    for (uint32_t first = 0u; first < 12u; first += 4u) {
        out = window_copy(epoch, first);
        assert(out.count == 4u && out.lost_count == 0u && out.available_end_ordinal == 12u);
        assert(out.next_ordinal == first + 4u);
        for (uint32_t i = 0u; i < 4u; ++i) {
            assert(out.records[i].ordinal == first + i);
            assert(out.records[i].sequence == (uint32_t)(UINT32_MAX - 1u + first + i));
        }
    }
    out = window_copy(epoch, 12u); assert(out.count == 0u && out.next_ordinal == 12u);
    for (uint32_t i = 12u; i < 20u; ++i) retain((uint32_t)(UINT32_MAX - 1u + i));
    out = window_copy(epoch, 1u);
    assert(out.oldest_ordinal == 4u && out.lost_count == 3u && out.records[0].ordinal == 4u);
    assert(out.next_ordinal == 8u && out.available_end_ordinal == 20u);
}
static void test_window_stop_rearm_and_clock(void) {
    window_setup(); accept_event(0u, 10u);
    const tdma_pio_spi_event_window_t old = window_copy(0u, 0u);
    tdma_pio_spi_phys_event_stop(&physical);
    window_reject(old.observer_epoch, 1u, TDMA_EVENT_WINDOW_UNAVAILABLE);
    recovery_setup();
    window_reject(old.observer_epoch, 1u, TDMA_EVENT_WINDOW_EPOCH_CHANGED);
    window_reject(0u, 0u, TDMA_EVENT_WINDOW_ANCHOR_UNAVAILABLE);
    accept_event(0u, 100u);
    const tdma_pio_spi_event_window_t next = window_copy(0u, 0u);
    assert(next.observer_epoch != old.observer_epoch);
    live_clock_available = false; tdma_event_live_monitor();
    window_reject(next.observer_epoch, 1u, TDMA_EVENT_WINDOW_ANCHOR_UNAVAILABLE);
    live_clock_available = true;
}
static unsigned window_race_mode, window_races;
static void window_mutate(void) {
    ++window_races;
    if (window_race_mode == 0u) retain(101u);
    else if (window_race_mode == 1u) tdma_event_history_retire(&s_tdma_event_history);
    else if (window_race_mode == 2u) tdma_event_live_retire(TDMA_EVENT_LIVE_STOP);
    else {
        const uint32_t epoch = s_tdma_event_history.epoch;
        tdma_event_history_retire(&s_tdma_event_history);
        assert(tdma_event_history_start(&s_tdma_event_history, epoch + 1u,
            s_tdma_event_hz, (tdma_event_interval_t){0u, 0u}));
        s_tdma_event_epoch = epoch + 1u; /* Keep the synthetic owner epoch aligned. */
    }
}
static void test_window_concurrent_publications(void) {
    for (window_race_mode = 0u; window_race_mode < 4u; ++window_race_mode) {
        window_setup(); accept_event(0u, 100u); window_races = 0u;
        window_hook = window_mutate;
        window_reject(s_tdma_event_history.epoch, 0u, TDMA_EVENT_WINDOW_BUSY);
        assert(window_races == 1u); /* No retry/wait and no cursor advancement. */
        window_hook = NULL;
        if (window_race_mode == 0u) {
            const tdma_pio_spi_event_window_t out = window_copy(0u, 0u);
            assert(out.count == 2u && out.records[1].sequence == 101u);
        }
    }
}
static void test_window_guard_wrap_and_time_bound(void) {
    window_setup(); accept_event(0u, 100u);
    s_tdma_event_history.publication_guard |= 1u;
    window_reject(0u, 0u, TDMA_EVENT_WINDOW_BUSY);
    s_tdma_event_history.publication_guard = UINT32_MAX - 1u;
    retain(101u); assert(s_tdma_event_history.publication_guard == 0u);
    s_tdma_event_live_guard = UINT32_MAX - 1u;
    tdma_event_live_record(&s_tdma_event_records[0]);
    assert(s_tdma_event_live_guard == 0u);
    (void)window_copy(0u, 0u);
    s_tdma_event_live_guard = 1u; window_reject(0u, 0u, TDMA_EVENT_WINDOW_BUSY);
    s_tdma_event_live_guard = 0u;
    for (uint32_t irq = 0u; irq < 2u; ++irq) {
        interrupt_mask = irq; copying = true; time_reads = 0u; read_delay = 1000u;
        (void)window_copy(0u, 0u); assert(interrupt_mask == irq);
        time_reads = 0u; read_delay = 1001u;
        window_reject(0u, 0u, TDMA_EVENT_WINDOW_BUSY); assert(interrupt_mask == irq);
        time_reads = 0u; read_delay = UINT64_MAX;
        window_reject(0u, 0u, TDMA_EVENT_WINDOW_BUSY); assert(interrupt_mask == irq);
        copying = false;
    }
    interrupt_mask = 0u; read_delay = 0u;
    assert(interrupt_depth == 0u && interrupt_saves == interrupt_restores);
}
static void test_window_corruption_and_anchor_overflow(void) {
    for (unsigned mode = 0u; mode < 8u; ++mode) {
        window_setup(); accept_event(0u, 100u);
        union { tdma_pio_spi_event_live_snapshot_t snapshot; uint32_t words[24]; } live;
        memcpy(live.words, s_tdma_event_live_words, sizeof(live.words));
        tdma_event_window_result_t reason = TDMA_EVENT_WINDOW_ANCHOR_UNAVAILABLE;
        if (mode == 0u) ++live.snapshot.record.epoch;
        if (mode == 1u) ++live.snapshot.tick_hz;
        if (mode == 2u) live.snapshot.arm_epoch = 0u;
        if (mode == 3u) live.snapshot.timer1_enable_before = live.snapshot.timer1_enable_after + 1u;
        if (mode == 4u) live.snapshot.flags &= ~TDMA_EVENT_LIVE_ACTIVE;
        if (mode == 5u) { live.snapshot.timer1_enable_after = UINT64_MAX; reason = TDMA_EVENT_WINDOW_BAD_STATE; }
        if (mode == 6u) { s_tdma_event_history.count = 17u; reason = TDMA_EVENT_WINDOW_BAD_STATE; }
        if (mode == 7u) { s_tdma_event_history.records[0].ordinal = 1u; reason = TDMA_EVENT_WINDOW_BAD_STATE; }
        memcpy(s_tdma_event_live_words, live.words, sizeof(live.words));
        window_reject(0u, 0u, reason);
    }
}
static void test_window_terminal_ordinal(void) {
    window_setup(); accept_event(0u, 100u);
    /* Seed the last ordinal boundary; reading/append/retirement are production. */
    s_tdma_event_history.accepted = UINT32_MAX;
    s_tdma_event_history.oldest_ordinal = UINT32_MAX - 1u;
    s_tdma_event_history.records[0].ordinal = UINT32_MAX - 1u;
    tdma_event_record_t terminal = s_tdma_event_records[0];
    terminal.ordinal = UINT32_MAX; terminal.sequence = 101u;
    terminal.rx_elapsed_cycles += 100u; terminal.tx_elapsed_cycles += 100u;
    assert(tdma_event_history_append(&s_tdma_event_history, &terminal, 1u, (uint64_t)UINT32_MAX + 1u));
    tdma_pio_spi_event_window_t out = window_copy(0u, 0u);
    assert(out.count == 2u && out.lost_count == UINT32_MAX - 1u);
    assert(out.next_ordinal == (uint64_t)UINT32_MAX + 1u && out.records[1].ordinal == UINT32_MAX);
    out = window_copy(out.observer_epoch, out.next_ordinal);
    assert(out.count == 0u && out.next_ordinal == (uint64_t)UINT32_MAX + 1u);
    assert(!tdma_event_history_append(&s_tdma_event_history, &terminal, 1u, (uint64_t)UINT32_MAX + 2u));
    window_reject(0u, 0u, TDMA_EVENT_WINDOW_UNAVAILABLE);
}
int main(void) {
    test_window_arguments_and_binding(); test_window_chunks_eviction_and_sequence_wrap();
    test_window_stop_rearm_and_clock(); test_window_concurrent_publications();
    test_window_guard_wrap_and_time_bound(); test_window_corruption_and_anchor_overflow();
    test_window_terminal_ordinal();
    puts("history window: 7 production groups passed"); return 0;
}
'''
