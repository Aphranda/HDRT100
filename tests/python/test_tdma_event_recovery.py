"""Execute production observer-only recovery, guarded readback and real ARM tail.

Only PIO/MMIO and clock seams are simulated. Feed, phase progression, start,
STOP and first-ARM archive bodies come from production; no recovery algorithm
is reimplemented in the fixture.
"""
from pathlib import Path

from test_tdma_observer_prelaunch import prelaunch_source
from test_tdma_rx_event_candidate import run


def test_bounded_observer_recovery_and_frozen_diagnostics(tmp_path: Path) -> None:
    source = prelaunch_source(tmp_path)
    pos = source.rindex("int main(void)")
    source = source[:pos] + source[pos:].replace("int main(void)", "int prelaunch_regression_main(void)", 1)
    source = source.replace("static uint32_t clock_get_hz(unsigned ignored) {", r'''
static uint32_t forced_hz;
static unsigned clock_change_at;
static uint32_t clock_get_hz(unsigned ignored) {
''', 1)
    source = source.replace("return 125000000u;", "return (forced_hz || (clock_change_at && hz_reads >= clock_change_at)) ? 125000001u : 125000000u;")
    # The base fixture delayed W1C emulation until enable. Recovery inspects
    # FDEBUG between a final-CS deferral and enable, so emulate this register
    # write immediately, as the real peripheral does.
    source = source.replace("pio->fdebug = TDMA_EVENT_SM_MASK << PIO_FDEBUG_RXSTALL_LSB;",
                            "pio->fdebug &= ~(TDMA_EVENT_SM_MASK << PIO_FDEBUG_RXSTALL_LSB);")
    begin = source.index("/* EVENT_RECOVERY_STORAGE_BEGIN")
    end = source.index("#define __atomic_load_n", begin)
    block = source[begin:end].replace("        __atomic_thread_fence(__ATOMIC_ACQ_REL);", "        recovery_read_fence();")
    source = source[:begin] + r'''
static void (*recovery_read_hook)(void);
static void recovery_read_fence(void) {
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (recovery_read_hook != NULL) recovery_read_hook();
}
''' + block + source[end:]
    source = source.replace('#include <assert.h>', r'''#include <assert.h>
#include <stdlib.h>
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr, "ASSERT %s:%d: %s\n", __FILE__, __LINE__, #c); fflush(stderr); _Exit(99); } } while (0)''', 1)
    assert "observer recovery: 13 production groups passed" in run(
        tmp_path, source + CASES, "event_recovery", enabled=True)


CASES = r'''
static tdma_pio_spi_event_recovery_snapshot_t recovery_snapshot(void) {
    tdma_pio_spi_event_recovery_snapshot_t result;
    assert(tdma_pio_spi_phys_event_recovery_get(&physical, &result));
    return result;
}
static void recovery_setup(void) {
    recovery_read_hook = NULL; forced_hz = clock_change_at = fault_at_hz_read = 0u;
    copying = false; read_delay = 0u;
    s_tdma_event_tap_requested = (tdma_pio_spi_event_tap_config_t){0};
    s_tdma_event_tap_requested_guard = 0u;
    prelaunch_setup(); selected_config.geometry_generation = 0u;
    physical.flight_physical_byte_count = 173u;
    assert(tdma_pio_spi_phys_event_tap_set(&physical, 1u, 96u, 0u));
    tdma_pio_spi_phys_event_prepare(&physical, &selected_config);
    assert(finish()); tdma_event_start(&physical);
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && physical.armed);
    assert(s_tdma_event_arm_epoch == s_tdma_pio_spi_rx_arm_epoch);
    s_tdma_event_recovery = (tdma_pio_spi_event_recovery_snapshot_t){0};
    tdma_event_recovery_publish();
    memset(fifo_cursor, 0, sizeof(fifo_cursor));
}
static uint32_t recovery_wire(uint32_t sequence) {
    return sequence >> 24u | (sequence >> 8u & 0xff00u) |
        (sequence << 8u & 0xff0000u) | sequence << 24u;
}
static void load_event(uint32_t ordinal, uint32_t sequence) {
    memset(fifo_cursor, 0, sizeof(fifo_cursor));
    const uint32_t raw = UINT32_MAX - 31250u - 93750u * ordinal;
    fifo[1][0] = raw; fifo[1][1] = UINT32_MAX - ordinal;
    fifo[2][0] = raw - 1u; fifo[2][1] = UINT32_MAX - ordinal;
    fifo[3][0] = recovery_wire(sequence);
    bank.level[1] = bank.level[2] = 2u; bank.level[3] = 1u;
    const uint64_t elapsed = 62501u + (uint64_t)ordinal * 187505u;
    clock_us = s_tdma_event_base_us + (s_tdma_event_observer.start.hi + elapsed) / 125u + 20u;
}
static void accept_event(uint32_t ordinal, uint32_t sequence) {
    load_event(ordinal, sequence);
    tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE);
    assert(s_tdma_event_snapshot.sequence == sequence && s_tdma_event_snapshot.ordinal == ordinal);
}
static void reject_duplicate(void) {
    accept_event(0u, 1u);
    const unsigned enabled_before = enabled_masks;
    load_event(1u, 1u);
    tdma_pio_spi_phys_event_service(&physical);
    const tdma_pio_spi_event_recovery_snapshot_t r = recovery_snapshot();
    assert(s_tdma_event_observer.state == TDMA_EVENT_INVALID);
    assert(r.pending == TDMA_EVENT_RECOVERY_RESET_PENDING && r.last_failure_reason == TDMA_EVENT_SEQUENCE);
    assert(r.last_failure_fault_bits == 0u && r.last_accepted_sequence == 1u && r.last_accepted_ordinal == 0u);
    assert(r.last_batch_sequence_valid && r.last_batch_sequence_first == 0x01000000u);
    assert(physical.armed && enabled_masks == enabled_before && (bank.ctrl & 1u));
}
static void reset_only_phase(void) {
    const unsigned enabled_before = enabled_masks;
    const uint32_t epoch_before = s_tdma_event_epoch;
    tdma_pio_spi_phys_event_service(&physical);
    assert(recovery_snapshot().pending == TDMA_EVENT_RECOVERY_WAIT_IDLE);
    assert(s_tdma_event_waiting && s_tdma_event_observer.state == TDMA_EVENT_STOPPED);
    assert(enabled_masks == enabled_before && s_tdma_event_epoch == epoch_before);
    assert(s_tdma_event_snapshot.sequence == 0u && s_tdma_event_snapshot.published == 0u);
    assert(s_tdma_event_snapshot.rx_elapsed_cycles == 0u && s_tdma_event_snapshot.fifo_sequence_first == 0u);
}
static void enable_only_phase(void) {
    const unsigned enabled_before = enabled_masks, reads_before = fifo_reads;
    const uint32_t epoch_before = s_tdma_event_epoch;
    tdma_pio_spi_phys_event_service(&physical);
    assert(!recovery_snapshot().pending && s_tdma_event_observer.state == TDMA_EVENT_ACTIVE);
    assert(s_tdma_event_epoch == epoch_before + 1u && enabled_masks == enabled_before + 1u);
    assert(fifo_reads == reads_before && s_tdma_event_snapshot.joined == 0u);
    assert(s_tdma_event_snapshot.sequence == 0u && s_tdma_event_snapshot.rx_elapsed_cycles == 0u);
}
static void test_duplicate_then_later_sequence(void) {
    recovery_setup(); reject_duplicate();
    const uint32_t failed = s_tdma_event_epoch;
    reset_only_phase(); enable_only_phase();
    tdma_pio_spi_event_recovery_snapshot_t r = recovery_snapshot();
    assert(r.failure_count == 1u && r.attempt_count == 1u && r.enable_count == 1u);
    assert(r.last_failure_epoch == failed && r.service_max_us != 0u);
    accept_event(0u, 903u); accept_event(1u, 904u); accept_event(2u, 905u);
    assert(s_tdma_event_history.active && s_tdma_event_history.count == 3u);
    assert(s_tdma_event_snapshot.candidate.flags & TDMA_RX_EVENT_RETIRED ||
        s_tdma_event_snapshot.candidate.query_count == 0u);
    assert((s_tdma_event_snapshot.candidate.flags &
        (TDMA_RX_EVENT_TIMESTAMP_VALID | TDMA_RX_EVENT_DPLL_ELIGIBLE)) == 0u);
}
static void test_many_bootstrap_duplicates(void) {
    recovery_setup();
    for (unsigned i = 0u; i < 12u; ++i) {
        reject_duplicate(); reset_only_phase(); enable_only_phase();
        assert(recovery_snapshot().failure_count == i + 1u);
        assert(recovery_snapshot().enable_count == i + 1u);
    }
    accept_event(0u, 0u); accept_event(1u, 1u); /* Arbitrary baseline includes zero. */
    assert(s_tdma_event_snapshot.published == 2u && physical.armed);
}
static void test_idle_deferrals_and_final_gate(void) {
    recovery_setup(); reject_duplicate(); reset_only_phase();
    const uint32_t epoch = s_tdma_event_epoch;
    const unsigned enabled_before = enabled_masks;
    gpio_script[0] = UINT32_MAX & ~(1u << physical.rx_csn_pin);
    gpio_script_count = 1u;
    for (unsigned i = 0u; i < 8u; ++i) {
        gpio_script_index = 0u;
        tdma_pio_spi_phys_event_service(&physical);
        assert(gpio_script_index == 1u && s_tdma_event_epoch == epoch && enabled_masks == enabled_before);
        assert(recovery_snapshot().deferral_count == i + 1u);
    }
    script_start_pads(UINT32_MAX, UINT32_MAX & ~(1u << physical.tx_csn_pin), 0u, 2u);
    tdma_pio_spi_phys_event_service(&physical);
    assert(gpio_script_index == 2u && s_tdma_event_epoch == epoch && enabled_masks == enabled_before);
    assert(recovery_snapshot().deferral_count == 9u && recovery_snapshot().attempt_count == 9u);
    gpio_script_count = gpio_script_index = 0u;
    enable_only_phase();
    assert(recovery_snapshot().attempt_count == 10u && recovery_snapshot().enable_count == 1u);
}
static void test_dirty_recovery_enable_is_terminal(void) {
    recovery_setup(); reject_duplicate(); reset_only_phase();
    const uint32_t epoch = s_tdma_event_epoch;
    script_start_pads(UINT32_MAX, UINT32_MAX, UINT32_MAX & ~(1u << physical.rx_csn_pin), 3u);
    tdma_pio_spi_phys_event_service(&physical);
    const tdma_pio_spi_event_recovery_snapshot_t r = recovery_snapshot();
    assert(s_tdma_event_epoch == epoch + 1u && s_tdma_event_observer.state == TDMA_EVENT_INVALID);
    assert(r.failure_count == 2u && r.enable_count == 0u && !r.pending);
    assert(r.last_failure_fault_bits & TDMA_EVENT_FAULT_DIRTY_START);
    assert(!r.last_batch_sequence_valid && r.last_batch_sequence_first == 0u);
    assert(r.cancel_reason == TDMA_EVENT_RECOVERY_CANCEL_START && physical.armed && (bank.ctrl & 1u));
    gpio_script_count = 0u;
    for (unsigned i = 0u; i < 3u; ++i) tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_event_epoch == epoch + 1u && recovery_snapshot().failure_count == 2u);
}
static void test_stop_and_new_arm_cancel(void) {
    for (unsigned phase = 0u; phase < 2u; ++phase) {
        recovery_setup(); reject_duplicate();
        if (phase) reset_only_phase();
        const uint32_t epoch = s_tdma_event_epoch;
        assert(tdma_pio_spi_phys_disarm(&physical));
        tdma_pio_spi_event_recovery_snapshot_t r = recovery_snapshot();
        assert(!r.pending && r.cancel_count == 1u && r.cancel_reason == TDMA_EVENT_RECOVERY_CANCEL_STOP);
        assert(r.failure_count == 1u && r.last_failure_epoch == epoch);
        physical.armed = true; /* A mistaken later service cannot revive STOP. */
        tdma_pio_spi_phys_event_service(&physical);
        assert(s_tdma_event_epoch == epoch && !s_tdma_event_waiting);
        physical.armed = false;
        tdma_pio_spi_phys_event_prepare(&physical, &selected_config);
        assert(recovery_snapshot().failure_count == 1u);
        tdma_event_start(&physical);
        assert(s_tdma_event_epoch == epoch + 1u && recovery_snapshot().last_failure_epoch == epoch);
    }
}
static void test_binding_clock_and_epoch_cancellation(void) {
    for (unsigned which = 0u; which < 8u; ++which) {
        recovery_setup(); reject_duplicate(); reset_only_phase();
        const uint32_t saved_epoch = s_tdma_event_epoch;
        const unsigned enabled_before = enabled_masks;
        if (which == 0u) ++s_tdma_pio_spi_rx_arm_epoch;
        if (which == 1u) ++s_tdma_event_tap_applied.generation;
        if (which == 2u) forced_hz = 1u;
        if (which == 3u) s_tdma_event_epoch = UINT32_MAX;
        if (which == 4u) physical.armed = false;
        if (which == 5u) physical.role = TDMA_PIO_SPI_ROLE_MASTER;
        if (which == 6u) s_tdma_pio_spi_rx_arm_valid = false;
        if (which == 7u) s_tdma_event_tap_applied_valid = false;
        tdma_pio_spi_phys_event_service(&physical);
        const tdma_pio_spi_event_recovery_snapshot_t r = recovery_snapshot();
        assert(!r.pending && !s_tdma_event_waiting && r.cancel_count == 1u && enabled_masks == enabled_before);
        assert(r.cancel_reason == (which == 2u ? TDMA_EVENT_RECOVERY_CANCEL_CLOCK :
            which == 3u ? TDMA_EVENT_RECOVERY_CANCEL_EPOCH : TDMA_EVENT_RECOVERY_CANCEL_BINDING));
        s_tdma_event_epoch = saved_epoch; forced_hz = 0u;
    }
}
static void test_start_cut_and_transport_untouched(void) {
    recovery_setup();
    tdma_rx_start_cut_t first = s_tdma_rx_start_cut;
    const bank_t old_capture = rx_bank;
    const uint32_t old_dma_count = dma_bank.ch[4].transfer_count;
    const uint32_t old_dma_ctrl = dma_bank.ch[4].ctrl_trig;
    const uint32_t old_control_pc = bank.pc[0], old_control_fifo = bank.level[0];
    const uint64_t old_arm = s_tdma_pio_spi_rx_arm_epoch, old_base_us = s_tdma_event_base_us;
    const tdma_pio_spi_event_tap_config_t applied = s_tdma_event_tap_applied;
    reject_duplicate(); reset_only_phase();
    /* Requested payload and guard are deliberately unreadable. Recovery
     * must use the frozen tap and must not freeze a Core0 request again. */
    s_tdma_event_tap_requested.enabled = 0u;
    s_tdma_event_tap_requested.prefix_bits = UINT32_MAX;
    s_tdma_event_tap_requested_guard |= 1u;
    enable_only_phase();
    assert(memcmp(&s_tdma_event_tap_applied, &applied, sizeof(applied)) == 0);
    assert(s_tdma_event_snapshot.prefix_bits == applied.prefix_bits);
    assert(s_tdma_event_base_us == old_base_us && s_tdma_pio_spi_rx_arm_epoch == old_arm);
    assert(memcmp(&rx_bank, &old_capture, sizeof(rx_bank)) == 0);
    assert(dma_bank.ch[4].transfer_count == old_dma_count && dma_bank.ch[4].ctrl_trig == old_dma_ctrl);
    assert(bank.pc[0] == old_control_pc && bank.level[0] == old_control_fifo && (bank.ctrl & 1u));
    assert(bank.clears[0] == 0u && bank.restarts[0] == 0u);
    tdma_rx_start_cut_t after = s_tdma_rx_start_cut;
    assert(after.retire_reasons & TDMA_RX_START_CUT_OBSERVER);
    first.flags = after.flags = first.retire_reasons = after.retire_reasons = 0u;
    assert(memcmp(&first, &after, sizeof(first)) == 0);
    assert(first.observer_epoch < s_tdma_event_epoch);
    s_tdma_event_tap_requested_guard &= ~1u;
}
static void test_late_fault_never_recovers(void) {
    for (unsigned which = 0u; which < 2u; ++which) {
        recovery_setup(); accept_event(0u, 1u); load_event(1u, 1u);
        /* FDEBUG is sampled before each clock read; inject after the post
         * read so the subsequent final FDEBUG read actually observes it. */
        if (which == 0u) fault_at_hz_read = hz_reads + 2u;
        else clock_change_at = hz_reads + 3u;
        tdma_pio_spi_phys_event_service(&physical);
        const tdma_pio_spi_event_recovery_snapshot_t r = recovery_snapshot();
        assert(s_tdma_event_observer.reason == TDMA_EVENT_SEQUENCE && s_tdma_event_observer.fault_bits == 0u);
        assert(r.failure_count == 1u && !r.pending);
        assert(r.last_failure_fault_bits & (which == 0u ? TDMA_EVENT_FAULT_STALL : TDMA_EVENT_FAULT_CONFIG));
        assert(physical.armed && (bank.ctrl & 1u));
        const uint32_t epoch = s_tdma_event_epoch;
        fault_at_hz_read = clock_change_at = 0u; bank.fdebug = 0u;
        tdma_pio_spi_phys_event_service(&physical);
        assert(s_tdma_event_epoch == epoch && recovery_snapshot().failure_count == 1u);
    }
}
static void test_hardware_fault_between_recovery_phases(void) {
    for (unsigned which = 0u; which < 2u; ++which) {
        recovery_setup(); reject_duplicate();
        const unsigned clears = bank.clears[1], enabled_before = enabled_masks;
        if (which == 0u) bank.fdebug |= 1u << TDMA_EVENT_RX_SM;
        else bank.pc[TDMA_EVENT_SEQUENCE_SM] = s_tdma_event_sequence_offset + tdma_event_sequence_offset_bad;
        tdma_pio_spi_phys_event_service(&physical);
        const tdma_pio_spi_event_recovery_snapshot_t r = recovery_snapshot();
        assert(!r.pending && r.cancel_reason == TDMA_EVENT_RECOVERY_CANCEL_START);
        assert(r.last_failure_fault_bits & (which == 0u ? TDMA_EVENT_FAULT_STALL : TDMA_EVENT_FAULT_SEQUENCE));
        assert(physical.armed && bank.clears[1] == clears && enabled_masks == enabled_before);
    }
}
static void test_batch_raw_is_not_claimed_as_offender(void) {
    recovery_setup(); accept_event(0u, 1u); load_event(1u, 2u);
    const uint32_t second = UINT32_MAX - 31250u - 93750u * 2u;
    fifo[1][2] = second; fifo[1][3] = UINT32_MAX - 2u;
    fifo[2][2] = second - 1u; fifo[2][3] = UINT32_MAX - 2u;
    fifo[3][1] = recovery_wire(2u); /* First word was valid; second duplicates it. */
    bank.level[1] = bank.level[2] = 4u; bank.level[3] = 2u;
    clock_us += 1501u;
    tdma_pio_spi_phys_event_service(&physical);
    const tdma_pio_spi_event_recovery_snapshot_t r = recovery_snapshot();
    assert(r.pending && r.last_failure_reason == TDMA_EVENT_SEQUENCE);
    assert(r.last_accepted_sequence == 1u && r.last_accepted_ordinal == 0u); /* Whole failed batch discarded. */
    assert(r.last_batch_sequence_valid && r.last_batch_sequence_first == recovery_wire(2u));
    assert(s_tdma_event_snapshot.published == 1u);
}
static void test_nonsequence_and_legacy_remain_terminal(void) {
    recovery_setup(); accept_event(0u, 1u);
    load_event(1u, 2u); fifo[1][1] = 0u;
    tdma_pio_spi_phys_event_service(&physical);
    assert(recovery_snapshot().failure_count == 1u && !recovery_snapshot().pending);
    assert(recovery_snapshot().last_failure_reason == TDMA_EVENT_ORDINAL);
    recovery_setup(); accept_event(0u, 1u);
    s_tdma_event_tap_applied.enabled = 0u;
    load_event(1u, 1u); tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_event_observer.state == TDMA_EVENT_INVALID && !recovery_snapshot().pending);
    assert(recovery_snapshot().failure_count == 0u);
}
static unsigned recovery_mutations, recovery_mutation_mode;
static void mutate_recovery(void) {
    ++recovery_mutations;
    if (recovery_mutation_mode == 1u && recovery_mutations > 1u) return;
    ++s_tdma_event_recovery_words[1];
    s_tdma_event_recovery_guard += 2u;
}
static void test_guarded_snapshot_failures(void) {
    recovery_setup(); reject_duplicate();
    tdma_pio_spi_event_recovery_snapshot_t result, sentinel;
    memset(&sentinel, 0xa5, sizeof(sentinel)); result = sentinel;
    assert(!tdma_pio_spi_phys_event_recovery_get(NULL, &result));
    assert(!tdma_pio_spi_phys_event_recovery_get(&physical, NULL));
    assert(memcmp(&result, &sentinel, sizeof(result)) == 0);
    s_tdma_event_recovery_guard |= 1u;
    assert(!tdma_pio_spi_phys_event_recovery_get(&physical, &result));
    assert(memcmp(&result, &sentinel, sizeof(result)) == 0);
    s_tdma_event_recovery_guard &= ~1u;
    recovery_mutations = 0u; recovery_mutation_mode = 1u; recovery_read_hook = mutate_recovery;
    assert(tdma_pio_spi_phys_event_recovery_get(&physical, &result));
    assert(recovery_mutations == 2u && result.failure_count == 2u);
    result = sentinel; recovery_mutations = 0u; recovery_mutation_mode = 2u;
    assert(!tdma_pio_spi_phys_event_recovery_get(&physical, &result));
    assert(recovery_mutations == 3u && memcmp(&result, &sentinel, sizeof(result)) == 0);
    recovery_read_hook = NULL;
    for (unsigned irq = 0u; irq < 2u; ++irq) {
        interrupt_mask = irq; copying = true; time_reads = 0u; read_delay = 1000u;
        assert(tdma_pio_spi_phys_event_recovery_get(&physical, &result));
        assert(interrupt_mask == irq);
        result = sentinel; time_reads = 0u; read_delay = 1001u;
        assert(!tdma_pio_spi_phys_event_recovery_get(&physical, &result));
        assert(interrupt_mask == irq && memcmp(&result, &sentinel, sizeof(result)) == 0);
        time_reads = 0u; read_delay = UINT64_MAX;
        assert(!tdma_pio_spi_phys_event_recovery_get(&physical, &result));
        assert(interrupt_mask == irq && memcmp(&result, &sentinel, sizeof(result)) == 0);
        copying = false;
    }
    interrupt_mask = 0u; read_delay = 0u;
    assert(interrupt_depth == 0u && interrupt_saves == interrupt_restores);
}
static void test_counters_saturate(void) {
    recovery_setup();
    s_tdma_event_recovery.failure_count = UINT32_MAX;
    s_tdma_event_recovery.attempt_count = UINT32_MAX;
    s_tdma_event_recovery.enable_count = UINT32_MAX;
    reject_duplicate(); reset_only_phase(); enable_only_phase();
    assert(recovery_snapshot().failure_count == UINT32_MAX);
    assert(recovery_snapshot().attempt_count == UINT32_MAX && recovery_snapshot().enable_count == UINT32_MAX);
}
int main(void) {
    test_duplicate_then_later_sequence();
    test_many_bootstrap_duplicates();
    test_idle_deferrals_and_final_gate();
    test_dirty_recovery_enable_is_terminal();
    test_stop_and_new_arm_cancel();
    test_binding_clock_and_epoch_cancellation();
    test_start_cut_and_transport_untouched();
    test_late_fault_never_recovers();
    test_hardware_fault_between_recovery_phases();
    test_batch_raw_is_not_claimed_as_offender();
    test_nonsequence_and_legacy_remain_terminal();
    test_guarded_snapshot_failures();
    test_counters_saturate();
    puts("observer recovery: 13 production groups passed");
    return 0;
}
'''
