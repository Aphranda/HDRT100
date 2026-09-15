"""Execute the real STOP tap/prepare/start/ARM-tail path with PIO register seams.

The OSR seed instructions execute in a small SET/IN/MOV model. Only the
atomic fence and MMIO seams are instrumented; configuration/start decisions
are extracted unchanged from production. This does not prove wire identity.
"""
from pathlib import Path

from test_tdma_observer_prelaunch import prelaunch_source
from test_tdma_rx_event_candidate import run


def test_independent_event_tap_config_freeze_and_real_start(tmp_path: Path) -> None:
    source = prelaunch_source(tmp_path)
    pos = source.rindex("int main(void)")
    source = source[:pos] + source[pos:].replace("int main(void)", "int prelaunch_regression_main(void)", 1)
    # Instrument the two tap reader fences without changing decision bodies.
    source = source.replace("/* EVENT_TAP_STORAGE_BEGIN", r'''
static void (*tap_read_hook)(void);
static void tap_test_fence(void) {
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (tap_read_hook != NULL) tap_read_hook();
}
/* EVENT_TAP_STORAGE_BEGIN''', 1)
    source = source.replace("        __atomic_thread_fence(__ATOMIC_ACQ_REL);", "        tap_test_fence();")
    source = source.replace("static void pio_sm_exec(PIO pio, uint sm, uint instruction) {", r'''
static uint32_t tap_x, tap_isr, tap_osr;
static void pio_sm_exec(PIO pio, uint sm, uint instruction) {
    if (pio == &bank && sm == TDMA_EVENT_SEQUENCE_SM) {
        switch (instruction >> 16u) {
        case 1u: assert(((instruction >> 8u) & 255u) == pio_x); tap_x = instruction & 31u; break;
        case 2u: {
            assert(((instruction >> 8u) & 255u) == pio_x);
            const uint32_t count = instruction & 63u;
            tap_isr = (tap_isr << count) | (tap_x & ((1u << count) - 1u)); break;
        }
        case 3u:
            assert(((instruction >> 8u) & 255u) == pio_osr && (instruction & 255u) == pio_isr);
            tap_osr = tap_isr; break;
        default: break;
        }
    }
''', 1)
    source = source.replace("return dest + value;", "return 0x10000u | (dest << 8u) | value;")
    source = source.replace("return src + count;", "return 0x20000u | (src << 8u) | count;")
    source = source.replace("return dst + src;", "return 0x30000u | (dst << 8u) | src;")
    source = source.replace("return delay;", "return delay << 8u;")
    source = source.replace('#include <assert.h>', r'''#include <assert.h>
#include <stdlib.h>
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr, "ASSERT %s:%d: %s\n", __FILE__, __LINE__, #c); fflush(stderr); _Exit(99); } } while (0)''', 1)
    assert "independent tap: 160 start combinations and 5 boundary groups passed" in run(
        tmp_path, source + CASES, "event_tap", enabled=True)


CASES = r'''
static void reset_tap(void) {
    tap_read_hook = NULL;
    s_tdma_event_tap_requested = (tdma_pio_spi_event_tap_config_t){0};
    s_tdma_event_tap_requested_guard = 0u;
    prelaunch_setup();
    selected_config.geometry_generation = 0u;
    physical.flight_physical_byte_count = 173u;
}
static tdma_pio_spi_event_tap_snapshot_t tap_snapshot(void) {
    tdma_pio_spi_event_tap_snapshot_t result;
    assert(tdma_pio_spi_phys_event_tap_get(&physical, &result));
    return result;
}
static void prepare_tap(uint32_t prefix, uint32_t delay) {
    assert(tdma_pio_spi_phys_event_tap_set(&physical, 1u, prefix, delay));
    tdma_pio_spi_phys_event_prepare(&physical, &selected_config);
}
static void test_explicit_start(void) {
    const uint32_t prefixes[] = {0u, 2u, 8u, 13u, 16u, 24u, 96u, 109u, 120u, 1352u};
    const uint32_t delays[] = {0u, 1u, 15u, 31u};
    for (unsigned p = 0u; p < 10u; ++p) for (unsigned d = 0u; d < 4u; ++d)
        for (unsigned offset = 0u; offset < 4u; ++offset) {
            reset_tap(); prepare_tap(prefixes[p], delays[d]);
            tdma_pio_spi_event_tap_snapshot_t snap = tap_snapshot();
            assert(snap.requested.generation == 1u && snap.applied.generation == 1u);
            assert(snap.applied_valid && !snap.actual_valid);
            physical.flight_alignment_byte_shift = offset == 3u ? UINT32_MAX : offset * 77u;
            physical.flight_alignment_bit_shift = offset == 3u ? UINT32_MAX : offset;
            physical.flight_data_phase_delay_cycles = 12345u; /* Must not be used. */
            physical.flight_overlay_alignment_locked = false;
            physical.flight_overlay_alignment_samples = 0u;
            assert(finish()); /* Complete actual final ARM tail without old geometry prelaunch. */
            assert(!s_tdma_event_prelaunch && s_tdma_event_waiting && physical.armed);
            tdma_event_start(&physical);
            assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE);
            assert(!s_tdma_event_waiting && physical.armed);
            assert(tap_osr == prefixes[p] + 31u);
            assert(bank.instr_mem[s_tdma_event_sequence_offset + tdma_event_sequence_offset_sample] ==
                (pio_encode_wait_gpio(true, physical.rx_sck_pin) | (delays[d] << 8u)));
            snap = tap_snapshot();
            assert(snap.actual_valid && snap.actual_prefix_bits == prefixes[p]);
            assert(snap.actual_sample_delay_cycles == delays[d]);
            assert((s_tdma_event_snapshot.candidate.flags &
                (TDMA_RX_EVENT_TIMESTAMP_VALID | TDMA_RX_EVENT_DPLL_ELIGIBLE)) == 0u);
        }
}
static void test_frozen_history_and_disabled_default(void) {
    reset_tap(); prepare_tap(98u, 2u); assert(finish()); tdma_event_start(&physical);
    const tdma_pio_spi_event_tap_snapshot_t started = tap_snapshot();
    assert(!tdma_pio_spi_phys_event_tap_set(&physical, 1u, 109u, 3u));
    assert(tap_snapshot().requested.generation == started.requested.generation);
    assert(tdma_pio_spi_phys_disarm(&physical));
    assert(tdma_pio_spi_phys_event_tap_set(&physical, 1u, 109u, 3u));
    tdma_pio_spi_event_tap_snapshot_t snap = tap_snapshot();
    assert(snap.requested.generation == 2u && snap.applied.generation == 1u);
    assert(snap.actual_valid && snap.actual_prefix_bits == 98u && snap.actual_sample_delay_cycles == 2u);
    tdma_pio_spi_phys_event_prepare(&physical, &selected_config);
    assert(tap_snapshot().applied.generation == 2u && !tap_snapshot().actual_valid);
    /* The facade normally excludes a later request during prepare. Even a
     * direct caller error cannot move an already frozen sampling point. */
    assert(tdma_pio_spi_phys_event_tap_set(&physical, 1u, 120u, 4u));
    tdma_event_start(&physical);
    assert(tap_osr == 140u && tap_snapshot().actual_prefix_bits == 109u);
    tdma_pio_spi_phys_event_stop(&physical);
    assert(tdma_pio_spi_phys_event_tap_set(&physical, 0u, 0u, 0u));
    tdma_pio_spi_phys_event_prepare(&physical, &selected_config);
    physical.flight_alignment_byte_shift = 3u;
    physical.flight_alignment_bit_shift = 5u;
    physical.flight_data_phase_delay_cycles = 7u;
    tdma_event_start(&physical);
    assert(s_tdma_event_waiting && !tap_snapshot().actual_valid); /* Training gate remains. */
    physical.flight_overlay_alignment_locked = true;
    physical.flight_overlay_alignment_samples = TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES;
    tdma_event_start(&physical);
    const uint32_t prefix = (3u + TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_SEQUENCE_OFFSET) * 8u + 5u;
    snap = tap_snapshot();
    assert(!snap.applied.enabled && snap.applied.generation == 4u);
    assert(snap.actual_valid && snap.actual_prefix_bits == prefix && snap.actual_sample_delay_cycles == 7u);
    assert(tap_osr == prefix + 31u);
}
static void test_configuration_boundaries(void) {
    reset_tap();
    assert(!tdma_pio_spi_phys_event_tap_set(NULL, 1u, 0u, 0u));
    assert(!tdma_pio_spi_phys_event_tap_set(&physical, 2u, 0u, 0u));
    assert(!tdma_pio_spi_phys_event_tap_set(&physical, 0u, 1u, 0u));
    assert(!tdma_pio_spi_phys_event_tap_set(&physical, 0u, 0u, 1u));
    assert(!tdma_pio_spi_phys_event_tap_set(&physical, 1u, 0u, 32u));
    assert(!tdma_pio_spi_phys_event_tap_set(&physical, 1u, UINT32_MAX - 31u, 0u));
    assert(tap_snapshot().requested.generation == 0u);
    prepare_tap(UINT32_MAX - 32u, 31u);
    assert(!tap_snapshot().applied_valid && !s_tdma_event_waiting);
    assert(!finish() && !physical.armed);
    reset_tap(); prepare_tap(1353u, 0u);
    assert(!tap_snapshot().applied_valid && !finish());
    reset_tap(); selected_config.geometry_generation = 37u; prepare_tap(96u, 0u);
    assert(!tap_snapshot().applied_valid && !finish() && !s_tdma_event_prelaunch);
    reset_tap(); physical.role = TDMA_PIO_SPI_ROLE_MASTER; prepare_tap(96u, 0u);
    assert(!tap_snapshot().applied_valid);
    assert(!tdma_pio_spi_phys_event_prelaunch(&physical, &selected_config));
    reset_tap(); physical.baud_hz = 0u; prepare_tap(96u, 0u);
    assert(!tap_snapshot().applied_valid && !finish());
    reset_tap(); prepare_tap(96u, 0u);
    tdma_pio_spi_phys_event_prepare(&physical, NULL);
    assert(!tap_snapshot().applied_valid && !s_tdma_event_waiting);
    assert(!tdma_pio_spi_phys_event_prelaunch(&physical, NULL));
}
static unsigned tap_mutations, tap_mutation_mode;
static void mutate_tap_read(void) {
    ++tap_mutations;
    if (tap_mutation_mode == 1u && tap_mutations > 1u) return;
    if (tap_mutation_mode == 3u) {
        ++s_tdma_event_tap_requested.generation;
        ++s_tdma_event_tap_requested.prefix_bits;
        s_tdma_event_tap_requested_guard += 2u;
    } else {
        ++s_tdma_event_tap_applied.generation;
        ++s_tdma_event_tap_applied.prefix_bits;
        s_tdma_event_tap_applied_guard += 2u;
    }
}
static void test_guards_and_failure_outputs(void) {
    reset_tap(); prepare_tap(96u, 0u);
    tdma_pio_spi_event_tap_snapshot_t snap, unchanged;
    memset(&unchanged, 0xa5, sizeof(unchanged)); snap = unchanged;
    assert(!tdma_pio_spi_phys_event_tap_get(NULL, &snap));
    assert(!tdma_pio_spi_phys_event_tap_get(&physical, NULL));
    assert(memcmp(&snap, &unchanged, sizeof(snap)) == 0);
    s_tdma_event_tap_requested_guard |= 1u;
    assert(!tdma_pio_spi_phys_event_tap_get(&physical, &snap));
    assert(!tdma_pio_spi_phys_event_tap_set(&physical, 1u, 0u, 0u));
    assert(memcmp(&snap, &unchanged, sizeof(snap)) == 0);
    s_tdma_event_tap_requested_guard &= ~1u;
    s_tdma_event_tap_applied_guard |= 1u;
    assert(!tdma_pio_spi_phys_event_tap_get(&physical, &snap));
    assert(memcmp(&snap, &unchanged, sizeof(snap)) == 0);
    s_tdma_event_tap_applied_guard &= ~1u;
    tap_mutations = 0u; tap_mutation_mode = 1u; tap_read_hook = mutate_tap_read;
    assert(tdma_pio_spi_phys_event_tap_get(&physical, &snap));
    assert(tap_mutations == 2u && snap.applied.generation == 2u && snap.applied.prefix_bits == 97u);
    snap = unchanged; tap_mutations = 0u; tap_mutation_mode = 2u;
    assert(!tdma_pio_spi_phys_event_tap_get(&physical, &snap));
    assert(tap_mutations == 3u && memcmp(&snap, &unchanged, sizeof(snap)) == 0);
    tap_mutations = 0u; tap_mutation_mode = 3u;
    assert(!tdma_pio_spi_phys_event_tap_get(&physical, &snap));
    assert(tap_mutations == 3u && memcmp(&snap, &unchanged, sizeof(snap)) == 0);
    tdma_pio_spi_phys_event_prepare(&physical, &selected_config);
    tap_read_hook = NULL;
    assert(!tap_snapshot().applied_valid && !s_tdma_event_waiting);
    assert(tap_snapshot().applied.generation == 0u); /* Never keep a stale successful ARM snapshot. */
    assert(!tdma_pio_spi_phys_event_prelaunch(&physical, &selected_config));
    assert(interrupt_depth == 0u && interrupt_saves == interrupt_restores);
}
static void test_age_irq_and_generation_limits(void) {
    reset_tap(); prepare_tap(96u, 0u);
    tdma_pio_spi_event_tap_snapshot_t snap, unchanged;
    memset(&unchanged, 0xa5, sizeof(unchanged));
    for (unsigned irq = 0u; irq < 2u; ++irq) {
        interrupt_mask = irq; copying = true; time_reads = 0u; read_delay = 1000u;
        assert(tdma_pio_spi_phys_event_tap_get(&physical, &snap));
        assert(interrupt_mask == irq);
        time_reads = 0u; read_delay = 1001u; snap = unchanged;
        assert(!tdma_pio_spi_phys_event_tap_get(&physical, &snap));
        assert(interrupt_mask == irq && memcmp(&snap, &unchanged, sizeof(snap)) == 0);
        time_reads = 0u; read_delay = UINT64_MAX;
        assert(!tdma_pio_spi_phys_event_tap_get(&physical, &snap));
        assert(interrupt_mask == irq && memcmp(&snap, &unchanged, sizeof(snap)) == 0);
        copying = false;
    }
    interrupt_mask = 0u; read_delay = 0u;
    s_tdma_event_tap_requested.generation = UINT32_MAX;
    assert(!tdma_pio_spi_phys_event_tap_set(&physical, 1u, 0u, 0u));
    s_tdma_event_tap_requested.generation = 1u;
    s_tdma_event_tap_requested_guard = UINT32_MAX - 1u;
    assert(!tdma_pio_spi_phys_event_tap_set(&physical, 1u, 0u, 0u));
    assert(s_tdma_event_tap_requested_guard == UINT32_MAX - 1u);
}
static void test_sample_fault_keeps_wire_running(void) {
    reset_tap(); prepare_tap(96u, 0u); assert(finish()); tdma_event_start(&physical);
    bank.pc[3] = s_tdma_event_sequence_offset + tdma_event_sequence_offset_bad;
    const uint32_t old_epoch = s_tdma_event_epoch;
    tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_event_observer.state == TDMA_EVENT_INVALID && physical.armed);
    assert((bank.ctrl & 1u) != 0u && (bank.ctrl & TDMA_EVENT_SM_MASK) == 0u);
    bank.pc[3] = s_tdma_event_sequence_offset;
    tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_event_epoch == old_epoch && s_tdma_event_observer.state == TDMA_EVENT_INVALID);
    assert(tap_snapshot().actual_valid && !s_tdma_event_waiting);
}
int main(void) {
    test_explicit_start();
    test_frozen_history_and_disabled_default();
    test_configuration_boundaries();
    test_guards_and_failure_outputs();
    test_age_irq_and_generation_limits();
    test_sample_fault_keeps_wire_running();
    puts("independent tap: 160 start combinations and 5 boundary groups passed");
    return 0;
}
'''
