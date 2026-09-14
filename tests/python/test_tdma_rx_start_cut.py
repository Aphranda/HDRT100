"""First enable/capture-loss cut using unchanged production owner functions.

PIO/DMA registers and time are hardware facades. The counter implementation,
observer, history, cut structure, startup/STOP/service/getter bodies are real.
No expected packet identity is injected into event or cut records.
"""
import hashlib
import json
from pathlib import Path

from test_tdma_rx_event_candidate import enabled_source, run

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/tdma/src/tdma_pio_spi_phys_event.inc"


CASES = r'''
static tdma_rx_start_cut_t cut_out;
static uint32_t cut_after_count, cut_after_ctrl;
static unsigned cut_case_groups;
static uint32_t encoded_count(uint32_t remaining) {
    return (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB) | remaining;
}
static void cut_at_enable(void) {
    dma_hw->ch[4].transfer_count = cut_after_count;
    dma_hw->ch[4].ctrl_trig = cut_after_ctrl;
    rx_bank.pc[2] = 19u;
    rx_bank.level[2] = 0u;
}
static void setup_cut(void) {
    cut_enable_hook = NULL; cut_read_hook = NULL; cut_copying = false;
    tick_step = 1u;
    setup_candidate(false);
    memset(&rx_bank, 0, sizeof(rx_bank)); rx_bank.ctrl = 1u << 2u;
    rx_bank.pc[2] = 7u; rx_bank.level[2] = 2u;
    s_tdma_pio_spi_rx_dma_channel = 4;
    tick_now = 100000u;
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,
        physical.flight_physical_byte_count, tick_now - 10u));
    s_tdma_pio_spi_rx_sequence.observation_epoch = 3u;
    s_tdma_pio_spi_rx_sequence.produced_words = 4096u;
    s_tdma_pio_spi_rx_sequence.position = 4096u;
    const uint32_t reload = s_tdma_pio_spi_rx_sequence.reload_words;
    dma_hw->ch[4].transfer_count = encoded_count(reload - 4200u);
    dma_hw->ch[4].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS | DMA_CH0_CTRL_TRIG_BUSY_BITS;
    cut_after_count = encoded_count(reload - 4320u);
    cut_after_ctrl = dma_hw->ch[4].ctrl_trig;
    cut_enable_hook = cut_at_enable;
    assert((s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_RECORDED) == 0u);
}
static void start_cut(void) {
    const tdma_rx_dma_counter_t original = s_tdma_pio_spi_rx_sequence;
    tdma_event_start(&physical);
    cut_enable_hook = NULL;
    assert(memcmp(&original, &s_tdma_pio_spi_rx_sequence, sizeof(original)) == 0);
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE);
    assert(s_tdma_event_history.active);
    const uint32_t required = TDMA_RX_START_CUT_DIAGNOSTIC | TDMA_RX_START_CUT_RECORDED |
        TDMA_RX_START_CUT_UNRESOLVED | TDMA_RX_START_CUT_INFLIGHT_UNKNOWN |
        TDMA_RX_START_CUT_PRESTART_BACKLOG_UNEXCLUDED;
    assert((s_tdma_rx_start_cut.flags & required) == required);
    assert((s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_STOPPED) == 0u);
}
static bool get_cut(uint64_t delay) {
    const unsigned before_hz = hz_reads, before_fifo = fifo_level_reads;
    const uint64_t before_tick = tick_now;
    const uint32_t mask = interrupt_mask;
    const unsigned saves = interrupt_saves, restores = interrupt_restores;
    copying = false; cut_copying = true; time_reads = 0u; read_delay = delay;
    cut_fences = cut_guard_reads = 0u;
    memset(&cut_out, 0xa5, sizeof(cut_out));
    const bool result = tdma_pio_spi_phys_get_rx_start_cut(&cut_out);
    assert(interrupt_mask == mask && interrupt_depth == 0u);
    assert(interrupt_saves == saves + 1u && interrupt_restores == restores + 1u);
    assert(before_hz == hz_reads && before_fifo == fifo_level_reads && before_tick == tick_now);
    cut_copying = false;
    if (!result) {
        const tdma_rx_start_cut_t empty = {0};
        assert(memcmp(&cut_out, &empty, sizeof(empty)) == 0);
    }
    return result;
}
static void assert_raw_unchanged(tdma_rx_start_cut_t original) {
    tdma_rx_start_cut_t current = s_tdma_rx_start_cut;
    current.flags = original.flags; current.retire_reasons = original.retire_reasons;
    assert(memcmp(&original, &current, sizeof(original)) == 0);
}
static void test_actual_enable_raw_bracket_and_local_lift(void) {
    setup_cut();
    const unsigned barriers = cut_mmio_barriers;
    const uint64_t start_ticks = tick_now;
    const unsigned enables = enabled_masks;
    start_cut();
    const tdma_rx_start_cut_t *cut = &s_tdma_rx_start_cut;
    assert(cut->schema == 1u && sizeof(*cut) == 128u);
    assert(cut->arm_epoch == s_tdma_pio_spi_rx_arm_epoch);
    assert(cut->observer_epoch == s_tdma_event_observer.epoch);
    assert(cut->observation_epoch_before == 3u && cut->observation_epoch_after == 3u);
    assert(cut->produced_before == 4200u && cut->produced_after == 4320u);
    assert(cut->sample_before_ticks == start_ticks && cut->sample_after_ticks > start_ticks);
    assert(cut->sample_after_ticks < tick_now); /* Closing bracket precedes later monitor. */
    assert(cut->dma_count_after == cut_after_count && cut->dma_ctrl_after == cut_after_ctrl);
    assert(cut->capture_fifo_before == 2u && cut->capture_fifo_after == 0u);
    assert(cut->capture_pc_before == 7u && cut->capture_pc_after == 19u);
    assert(cut->pads_before == UINT32_MAX && cut->pads_after == UINT32_MAX);
    assert(cut->dma_channel == 4u && cut->physical_frame_words == 240u);
    assert(cut->retire_reasons == 0u && enabled_masks == enables + 1u);
    assert(cut_mmio_barriers >= barriers + 3u);
    const tdma_rx_start_cut_t saved = *cut;
    tdma_event_start(&physical);
    assert(memcmp(&saved, cut, sizeof(saved)) == 0); /* Only first actual enable. */
    assert(!get_cut(0u));
    ++cut_case_groups;
}
static void test_equal_counts_fifo_empty_backlog_and_inflight_are_unresolved(void) {
    setup_cut(); rx_bank.level[2] = 0u;
    cut_after_count = dma_hw->ch[4].transfer_count;
    start_cut();
    const tdma_rx_start_cut_t saved = s_tdma_rx_start_cut;
    assert(saved.produced_before == saved.produced_after && saved.produced_before == 4200u);
    assert(saved.capture_fifo_before == 0u && saved.capture_fifo_after == 0u);
    /* Arrival predates enable even if the station copies it afterwards. */
    capture.candidate = 4000u;
    assert(capture.candidate < saved.produced_before);
    assert((saved.flags & TDMA_RX_START_CUT_PRESTART_BACKLOG_UNEXCLUDED) != 0u);
    /* An in-flight write completes after the closing observation. */
    dma_hw->ch[4].transfer_count = encoded_count(s_tdma_pio_spi_rx_sequence.reload_words - 4201u);
    tdma_pio_spi_phys_event_service(&physical);
    assert_raw_unchanged(saved);
    assert((s_tdma_rx_start_cut.flags & (TDMA_RX_START_CUT_UNRESOLVED |
        TDMA_RX_START_CUT_INFLIGHT_UNKNOWN)) ==
        (TDMA_RX_START_CUT_UNRESOLVED | TDMA_RX_START_CUT_INFLIGHT_UNKNOWN));
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE);
    ++cut_case_groups;
}
static void test_failed_idle_probe_and_dirty_actual_start(void) {
    setup_cut();
    const unsigned enables = enabled_masks;
    script_start_pads(UINT32_MAX, UINT32_MAX & ~(1u << physical.rx_csn_pin), 0u, 2u);
    tdma_event_start(&physical);
    assert(enabled_masks == enables && s_tdma_event_waiting);
    assert((s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_RECORDED) == 0u);
    tdma_rx_start_cut_disarmed(); assert(!get_cut(0u));
    script_start_pads(UINT32_MAX, UINT32_MAX, UINT32_MAX & ~(1u << physical.rx_csn_pin), 3u);
    tdma_event_start(&physical); cut_enable_hook = NULL; gpio_script_count = 0u;
    assert(enabled_masks == enables + 1u && s_tdma_event_observer.state == TDMA_EVENT_INVALID);
    assert((s_tdma_rx_start_cut.retire_reasons & (TDMA_RX_START_CUT_DIRTY_START |
        TDMA_RX_START_CUT_OBSERVER)) == (TDMA_RX_START_CUT_DIRTY_START | TDMA_RX_START_CUT_OBSERVER));
    assert(!get_cut(0u));
    ++cut_case_groups;
}
static void test_capture_loss_only_retires_cut_and_keeps_raw(void) {
    setup_cut(); start_cut();
    const tdma_rx_start_cut_t saved = s_tdma_rx_start_cut;
    const tdma_rx_dma_counter_t counter = s_tdma_pio_spi_rx_sequence;
    const unsigned disables = disabled_masks, reads = fifo_reads;
    rx_bank.fdebug = 1u << (2u + PIO_FDEBUG_RXSTALL_LSB);
    tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_rx_start_cut.retire_reasons == TDMA_RX_START_CUT_CAPTURE_RXSTALL);
    assert_raw_unchanged(saved);
    assert(memcmp(&counter, &s_tdma_pio_spi_rx_sequence, sizeof(counter)) == 0);
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && s_tdma_event_history.active);
    assert(disabled_masks == disables && fifo_reads == reads && bank.ctrl == 15u && rx_bank.ctrl == 4u);
    assert(rx_bank.fdebug == 4u); /* Observer never clears capture loss evidence. */
    dma_hw->ch[4].ctrl_trig |= DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    ++s_tdma_pio_spi_rx_sequence.observation_epoch;
    physical.flight_alignment_bit_shift = 1u;
    tdma_pio_spi_phys_event_service(&physical);
    const uint32_t reasons = s_tdma_rx_start_cut.retire_reasons;
    assert((reasons & (TDMA_RX_START_CUT_CAPTURE_RXSTALL | TDMA_RX_START_CUT_DMA_ERROR |
        TDMA_RX_START_CUT_OBSERVATION_EPOCH | TDMA_RX_START_CUT_GEOMETRY)) ==
        (TDMA_RX_START_CUT_CAPTURE_RXSTALL | TDMA_RX_START_CUT_DMA_ERROR |
         TDMA_RX_START_CUT_OBSERVATION_EPOCH | TDMA_RX_START_CUT_GEOMETRY));
    rx_bank.fdebug = 0u; dma_hw->ch[4].ctrl_trig = cut_after_ctrl;
    tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_rx_start_cut.retire_reasons == reasons);
    assert_raw_unchanged(saved);
    ++cut_case_groups;
}
static void test_stop_pending_ack_repeated_stop_and_other_persona_arm(void) {
    setup_cut(); start_cut();
    rx_bank.fdebug = 4u;
    tdma_pio_spi_phys_event_stop(&physical);
    const uint32_t reasons = TDMA_RX_START_CUT_CAPTURE_RXSTALL | TDMA_RX_START_CUT_STOP;
    assert(s_tdma_rx_start_cut.retire_reasons == reasons);
    assert(!get_cut(0u)); /* Observer STOP, cancellation ACK still pending. */
    const tdma_rx_start_cut_t saved = s_tdma_rx_start_cut;
    physical.flight_overlay_alignment_locked = false;
    rx_bank.ctrl = 0u; dma_hw->ch[4].ctrl_trig = 0u;
    tdma_pio_spi_phys_event_stop(&physical);
    assert_raw_unchanged(saved);
    assert(s_tdma_rx_start_cut.retire_reasons == reasons && !get_cut(0u));
    tdma_rx_start_cut_disarmed();
    assert(get_cut(0u) && cut_out.retire_reasons == reasons);
    assert((cut_out.flags & TDMA_RX_START_CUT_STOPPED) != 0u);
    const tdma_rx_start_cut_t frozen = cut_out;
    tdma_rx_start_cut_disarmed(); assert(get_cut(0u));
    assert(memcmp(&cut_out, &frozen, sizeof(frozen)) == 0);
    physical.role = TDMA_PIO_SPI_ROLE_MASTER;
    tdma_rx_start_cut_arm_begin(); /* Runs even before a failing ARM admission. */
    assert(!get_cut(0u));
    tdma_rx_start_cut_disarmed(); assert(!get_cut(0u));
    setup_cut(); start_cut();
    assert(s_tdma_rx_start_cut.retire_reasons == 0u);
    assert(s_tdma_rx_start_cut.arm_epoch != frozen.arm_epoch);
    ++cut_case_groups;
}
static void test_role_switch_does_not_read_a_different_capture_endpoint(void) {
    setup_cut(); start_cut();
    physical.role = TDMA_PIO_SPI_ROLE_MASTER;
    rx_bank.fdebug = 4u; rx_bank.ctrl = 0u; bank.fdebug = 14u;
    const uint64_t ticks = tick_now; const unsigned clocks = hz_reads;
    tdma_rx_start_cut_monitor(&physical);
    assert(s_tdma_rx_start_cut.retire_reasons == TDMA_RX_START_CUT_GEOMETRY);
    assert(tick_now == ticks && hz_reads == clocks);
    tdma_pio_spi_phys_event_stop(&physical);
    assert(s_tdma_rx_start_cut.retire_reasons == (TDMA_RX_START_CUT_GEOMETRY | TDMA_RX_START_CUT_STOP));
    ++cut_case_groups;
}
static void test_counter_wrap_ambiguous_epoch_and_invalid_counter(void) {
    setup_cut();
    const uint32_t reload = s_tdma_pio_spi_rx_sequence.reload_words;
    s_tdma_pio_spi_rx_sequence.position = reload - 2u;
    s_tdma_pio_spi_rx_sequence.produced_words = (uint64_t)reload * 3u - 2u;
    dma_hw->ch[4].transfer_count = encoded_count(1u);
    cut_after_count = encoded_count(reload - 3u);
    start_cut();
    assert(s_tdma_rx_start_cut.produced_before == (uint64_t)reload * 3u - 1u);
    assert(s_tdma_rx_start_cut.produced_after == (uint64_t)reload * 3u + 3u);
    assert((s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_COUNT_WRAP) != 0u);
    assert(s_tdma_rx_start_cut.retire_reasons == 0u);
    /* A complete SRAM-ring lap does not change the lifted coordinate meaning. */
    assert(s_tdma_rx_start_cut.produced_after > TDMA_PIO_SPI_RX_RING_WORDS);
    setup_cut();
    s_tdma_pio_spi_rx_sequence.sample_before_ticks = 0u;
    tick_now = (uint64_t)reload + 100u;
    start_cut();
    assert(s_tdma_rx_start_cut.observation_epoch_before == 4u);
    assert(s_tdma_rx_start_cut.observation_epoch_after == 4u);
    assert((s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_OBSERVATION_EPOCH) != 0u);
    assert(s_tdma_pio_spi_rx_sequence.observation_epoch == 3u);
    setup_cut(); start_cut();
    const tdma_rx_start_cut_t saved = s_tdma_rx_start_cut;
    tick_now += (uint64_t)reload;
    tdma_pio_spi_phys_event_service(&physical);
    assert((s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_OBSERVATION_EPOCH) != 0u);
    assert_raw_unchanged(saved);
    setup_cut(); s_tdma_pio_spi_rx_sequence.initialized = false; start_cut();
    assert((s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_COUNTER) != 0u);
    setup_cut(); dma_hw->ch[4].transfer_count = 7u; start_cut();
    assert((s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_DMA_CONFIG) != 0u);
    setup_cut(); cut_after_count = encoded_count(reload + 1u); start_cut();
    assert((s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_COUNTER) != 0u);
    setup_cut(); s_tdma_pio_spi_rx_sequence.produced_words = UINT64_MAX; start_cut();
    assert((s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_COUNTER) != 0u);
    ++cut_case_groups;
}
static unsigned cut_mutation;
static void mutate_cut_copy(void) {
    if (cut_mutation == 1u && cut_fences == 1u) tdma_rx_start_cut_arm_begin();
    if (cut_mutation == 2u || (cut_mutation == 3u && cut_fences == 1u)) {
        /* Two publications reuse the just-copied slot. */
        for (unsigned i = 0u; i < 2u; ++i) {
            ++s_tdma_rx_start_cut.observer_epoch;
            tdma_rx_start_cut_publish();
        }
        cut_out.produced_before ^= UINT64_C(0xffff000000000000); /* Force torn payload. */
    }
}
static void test_stop_getter_generation_torn_copy_timeout_and_mask(void) {
    setup_cut(); start_cut(); tdma_pio_spi_phys_event_stop(&physical); tdma_rx_start_cut_disarmed();
    const uint32_t generation = s_tdma_rx_start_cut.observer_epoch;
    for (uint32_t original_mask = 0u; original_mask < 2u; ++original_mask) {
        interrupt_mask = original_mask;
        assert(get_cut(1000u));
        assert(!get_cut(1001u));
        assert(!get_cut(UINT64_MAX));
    }
    interrupt_mask = 0u;
    cut_read_hook = mutate_cut_copy; cut_mutation = 3u;
    assert(get_cut(0u) && cut_fences == 2u);
    assert(cut_out.observer_epoch == generation + 2u && cut_out.produced_before == 4200u);
    cut_mutation = 2u;
    assert(!get_cut(0u) && cut_fences == 3u);
    cut_mutation = 1u;
    assert(!get_cut(0u) && cut_fences == 2u); /* STOP export overlaps new ARM. */
    cut_read_hook = NULL; cut_mutation = 0u;
    s_tdma_rx_start_cut_guard |= 1u;
    assert(!get_cut(0u) && cut_guard_reads == 3u && cut_fences == 0u);
    ++s_tdma_rx_start_cut_guard;
    assert(!tdma_pio_spi_phys_get_rx_start_cut(NULL));
    ++cut_case_groups;
}
int main(void) {
    assert(candidate_regression_main() == 0);
    test_actual_enable_raw_bracket_and_local_lift();
    test_equal_counts_fifo_empty_backlog_and_inflight_are_unresolved();
    test_failed_idle_probe_and_dirty_actual_start();
    test_capture_loss_only_retires_cut_and_keeps_raw();
    test_stop_pending_ack_repeated_stop_and_other_persona_arm();
    test_role_switch_does_not_read_a_different_capture_endpoint();
    test_counter_wrap_ambiguous_epoch_and_invalid_counter();
    test_stop_getter_generation_torn_copy_timeout_and_mask();
    printf("start cut: %u production groups; record=%zu storage=%zu; real counter; diagnostic only\n",
        cut_case_groups, sizeof(tdma_rx_start_cut_t), sizeof(s_tdma_rx_start_cut) +
        sizeof(s_tdma_rx_start_cut_slots) + sizeof(s_tdma_rx_start_cut_guard) +
        sizeof(s_tdma_rx_start_cut_data_phase) + sizeof(s_tdma_rx_start_cut_marker_phase));
    return 0;
}
'''


def test_real_owner_start_cut_and_stop_export(tmp_path: Path) -> None:
    generated = enabled_source(tmp_path)
    first, last = generated.rsplit("int main(void)", 1)
    generated = first + "int candidate_regression_main(void)" + last + CASES
    block = SOURCE.read_text(encoding="utf-8")
    block = block[block.index("/* RX_START_CUT_STORAGE_BEGIN"):block.index("/* RX_START_CUT_STORAGE_END */")]
    assert block in generated
    (tmp_path / "production-cut.json").write_text(json.dumps({
        "source": str(SOURCE.relative_to(ROOT)),
        "unchanged_cut_block_sha256": hashlib.sha256(block.encode()).hexdigest(),
        "hardware_facade": "Only register/time/atomic hooks; production cut block unchanged",
        "counter": "Links full production tdma_rx_sequence.c; local copy leaves scanner untouched",
        "raw_record": "First actual enable only; later monitor changes only flags and sticky reasons",
    }, indent=2), encoding="utf-8")
    output = run(tmp_path, generated, "start-cut", enabled=True)
    assert "start cut: 8 production groups; record=128 storage=396" in output
