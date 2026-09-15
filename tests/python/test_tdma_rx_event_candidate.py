"""Exercise real physical pin/query and event lifecycle with hardware-only hooks.

The enabled harness reuses the production start/STOP/guard regression and adds
the actual query and service definitions. The disabled harness includes the
whole production include under PROJECT_TDMA_EVENT_OBSERVER=0.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from test_tdma_event_adapter import production as adapter_production
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/tdma/src/tdma_pio_spi_phys_event.inc"


def macro(source: str, name: str) -> str:
    lines = source.splitlines()
    first = next(i for i, line in enumerate(lines) if re.match(rf"#define {name}\s", line))
    last = first
    while lines[last].endswith("\\"):
        last += 1
    return "\n".join(lines[first:last + 1])


def enabled_source(directory: Path) -> str:
    existing = adapter_production(directory)
    source = SOURCE.read_text(encoding="utf-8")
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    routines = []
    for result, name, arguments in [
        ("void", "tdma_event_candidate_increment", "uint32_t *counter"),
        ("void", "tdma_event_candidate_begin", "const tdma_rx_capture_t *capture, uint32_t pinned_epoch, uint32_t sequence, uint64_t station_age_ns"),
        ("void", "tdma_event_candidate_finish", "tdma_rx_event_reason_t reason"),
        ("tdma_rx_event_reason_t", "tdma_event_capture_current", "const tdma_pio_spi_phys_t *phys, const tdma_rx_capture_t *capture"),
        ("uint32_t", "tdma_pio_spi_phys_rx_event_pin", "void *context, const tdma_rx_capture_t *capture"),
        ("bool", "tdma_event_capture_envelope", "const tdma_rx_capture_t *capture, size_t packet_size"),
        ("void", "tdma_event_candidate_history_state", "void"),
        ("void", "tdma_pio_spi_phys_rx_event_query", "void *context, const tdma_rx_capture_t *capture, uint32_t capture_observer_epoch, uint32_t sequence, size_t packet_size, uint64_t station_age_ns"),
        ("void", "tdma_pio_spi_phys_event_service", "tdma_pio_spi_phys_t *phys"),
    ]:
        routines.append(f"static {result} {name}({arguments}) {{" + c_definition_body(source, name) + "}\n")
    definitions = "\n".join(macro(header, name) for name in
                            ("TDMA_PIO_SPI_RX_DMA_WORD_MAX", "TDMA_PIO_SPI_RX_RING_WORDS"))
    return ("#define main adapter_regression_main\n" + existing + "\n#undef main\n" +
            EXTRA_FIXTURE + definitions + "\n" + "\n".join(routines) + CASES)


EXTRA_FIXTURE = r'''
#include "tdma_rx_capture.h"
#include "tdma_rx_sequence.h"
#include "tdma_transport_frame.h"
#include "tdma_service_timing.h"
static uint64_t s_tdma_pio_spi_rx_capture_id;
static tdma_event_batch_t s_tdma_event_batch;
static tdma_event_record_t s_tdma_event_records[TDMA_EVENT_MAX_RECORDS];
static uint32_t fifo[4][8], fifo_cursor[4];
static unsigned fifo_reads;
static bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) { return pio->level[sm] == 0u; }
static uint32_t pio_sm_get(PIO pio, uint sm) {
    assert(sm > 0u && pio->level[sm] > 0u && fifo_cursor[sm] < 8u);
    --pio->level[sm]; ++fifo_reads; return fifo[sm][fifo_cursor[sm]++];
}
'''

CASES = r'''
static tdma_rx_capture_t capture;
static uint32_t pin;
static unsigned query_cases;
static tdma_rx_event_candidate_snapshot_t *last(void) { return &s_tdma_event_snapshot.candidate; }
static void setup_candidate(bool start) {
    fault_at_hz_read = 0u;
    start_gate_setup(); /* Real STOP, prepare and original start predicates. */
    physical.armed = physical.rx_capture_active = true;
    s_tdma_pio_spi_rx_arm_valid = true;
    ++s_tdma_pio_spi_rx_arm_epoch;
    ++s_tdma_pio_spi_rx_capture_id;
    s_tdma_pio_spi_rx_sequence.observation_epoch = 3u;
    capture = (tdma_rx_capture_t){
        .arm_epoch = s_tdma_pio_spi_rx_arm_epoch, .capture_id = s_tdma_pio_spi_rx_capture_id,
        .observation_epoch = 3u, .candidate = 998u, .produced_before = 1066u,
        .produced_after = 1067u, .frame_words = 68u, .persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER,
        .flags = TDMA_RX_CAPTURE_PRIVATE_COPY};
    memset(fifo, 0, sizeof(fifo)); memset(fifo_cursor, 0, sizeof(fifo_cursor));
    if (start) tdma_event_start(&physical);
    pin = tdma_pio_spi_phys_rx_event_pin(&physical, &capture);
    assert(start ? pin == s_tdma_event_observer.epoch && pin != 0u : pin == 0u);
}
static void retain(uint32_t sequence) {
    const uint32_t ordinal = (uint32_t)s_tdma_event_history.accepted;
    const tdma_event_record_t record = {
        .epoch = s_tdma_event_observer.epoch, .ordinal = ordinal, .sequence = sequence,
        .raw_rx = 100u + ordinal, .raw_tx = 101u + ordinal,
        .rx_elapsed_cycles = ((uint64_t)ordinal + 1u) * 50000u,
        .tx_elapsed_cycles = ((uint64_t)ordinal + 1u) * 50000u + 4u,
        .start_bounds = s_tdma_event_history.initial_start,
        .diagnostic_only = true, .identity_unproved = true, .physical_first_unproved = true};
    assert(tdma_event_history_append(&s_tdma_event_history, &record, 1u, (uint64_t)ordinal + 1u));
}
static void query_with(const tdma_rx_capture_t *input, uint32_t epoch,
                       uint32_t sequence, size_t size, tdma_rx_event_reason_t expected) {
    const unsigned before_hz = hz_reads, before_gpio = gpio_reads;
    const unsigned before_fifo = fifo_reads, before_level = fifo_level_reads;
    const uint64_t before_time = clock_us;
    const uint32_t before_guard = physical.flight_event_guard;
    const tdma_rx_capture_t saved = input != NULL ? *input : (tdma_rx_capture_t){0};
    tdma_pio_spi_phys_rx_event_query(&physical, input, epoch, sequence, size, 789u);
    assert(hz_reads == before_hz && gpio_reads == before_gpio && clock_us == before_time);
    assert(fifo_reads == before_fifo && fifo_level_reads == before_level);
    if (input != NULL) assert(memcmp(input, &saved, sizeof(saved)) == 0);
    assert(last()->reason == (uint32_t)expected && last()->packet_sequence == sequence);
    assert(last()->station_age_ns == 789u && last()->capture_observer_epoch == epoch);
    const uint32_t required = TDMA_RX_EVENT_DIAGNOSTIC_ONLY | TDMA_RX_EVENT_HISTORICAL |
        TDMA_RX_EVENT_HEADER_VALIDATED | TDMA_RX_EVENT_MAILBOX_UNKNOWN |
        TDMA_RX_EVENT_IDENTITY_UNPROVED | TDMA_RX_EVENT_PHYSICAL_FIRST_UNPROVED;
    assert((last()->flags & required) == required);
    assert((last()->flags & (TDMA_RX_EVENT_TIMESTAMP_VALID | TDMA_RX_EVENT_DPLL_ELIGIBLE)) == 0u);
    if (expected != TDMA_RX_EVENT_MATCHED) {
        assert((last()->flags & TDMA_RX_EVENT_MATCH_PRESENT) == 0u);
        assert(last()->rx_elapsed_cycles == 0u && last()->tx_elapsed_cycles == 0u);
        assert(last()->start_lo_cycles == 0u && last()->start_hi_cycles == 0u);
        assert(last()->event_sequence == 0u && last()->event_ordinal == UINT32_MAX);
    }
    if (physical.armed && tdma_pio_spi_phys_event_selected(&physical) &&
        s_tdma_event_observer.state == TDMA_EVENT_ACTIVE) {
        assert(physical.flight_event_guard == before_guard && s_tdma_event_candidate_dirty);
    }
    ++query_cases;
}
static void query(uint32_t sequence, tdma_rx_event_reason_t expected) {
    query_with(&capture, pin, sequence, 64u, expected);
}
static void test_independent_record_and_wrapping_sequence(void) {
    setup_candidate(true);
    retain(UINT32_MAX); retain(0u); retain(1u);
    query(0u, TDMA_RX_EVENT_MATCHED);
    assert(last()->event_sequence == 0u && last()->event_ordinal == 1u);
    assert(last()->rx_elapsed_cycles == 100000u && last()->tx_elapsed_cycles == 100004u);
    assert(last()->start_lo_cycles == s_tdma_event_history.initial_start.lo);
    assert(last()->start_hi_cycles == s_tdma_event_history.initial_start.hi);
    assert(last()->dma_candidate == 998u && last()->capture_id == capture.capture_id);
    assert(last()->observer_arm_epoch == capture.arm_epoch && last()->observation_epoch == 3u);
    assert(last()->history_count == 3u && last()->history_accepted == 3u);
    assert(last()->query_observer_epoch == pin && last()->pio_hz == 125000000u);
    /* Deliberately conflicting latest fields cannot alter the selected event. */
    s_tdma_event_snapshot.sequence = 777u;
    s_tdma_event_snapshot.rx_elapsed_cycles = 999999u;
    query(UINT32_MAX, TDMA_RX_EVENT_MATCHED);
    assert(last()->event_ordinal == 0u && last()->rx_elapsed_cycles == 50000u);
    const uint64_t first = capture.capture_id;
    capture.capture_id = ++s_tdma_pio_spi_rx_capture_id;
    pin = tdma_pio_spi_phys_rx_event_pin(&physical, &capture);
    query(1u, TDMA_RX_EVENT_MATCHED);
    assert(last()->capture_id != first && last()->event_ordinal == 2u);
}
static void test_pin_does_not_borrow_latest_or_invent_frame_identity(void) {
    setup_candidate(false);
    assert(pin == 0u);
    tdma_event_start(&physical); retain(10u);
    query(10u, TDMA_RX_EVENT_PIN_MISSING);
    assert(last()->query_observer_epoch != 0u && last()->history_query_reason == UINT32_MAX);
    /* Backlogged bytes copied after start may have a software pin, but even
     * a match stays identity_unproved; no DMA-coordinate/ordinal mapping. */
    pin = tdma_pio_spi_phys_rx_event_pin(&physical, &capture);
    query(10u, TDMA_RX_EVENT_MATCHED);
    assert(last()->event_ordinal == 0u && last()->dma_candidate != 0u);
    assert((last()->flags & TDMA_RX_EVENT_IDENTITY_UNPROVED) != 0u);
}
static void test_lifetime_mismatches_and_unavailable(void) {
    setup_candidate(true); retain(11u);
    const uint32_t good_pin = pin;
    pin = good_pin + 1u; query(11u, TDMA_RX_EVENT_PIN_STALE); pin = good_pin;
    ++s_tdma_event_history.epoch; query(11u, TDMA_RX_EVENT_PIN_STALE); --s_tdma_event_history.epoch;
    ++s_tdma_event_arm_epoch; query(11u, TDMA_RX_EVENT_ARM_STALE); --s_tdma_event_arm_epoch;
    ++s_tdma_pio_spi_rx_sequence.observation_epoch;
    assert(tdma_pio_spi_phys_rx_event_pin(&physical, &capture) == 0u);
    query(11u, TDMA_RX_EVENT_OBSERVATION_STALE); --s_tdma_pio_spi_rx_sequence.observation_epoch;
    ++s_tdma_pio_spi_rx_capture_id; query(11u, TDMA_RX_EVENT_CAPTURE_STALE); --s_tdma_pio_spi_rx_capture_id;
    ++s_tdma_pio_spi_rx_arm_epoch; query(11u, TDMA_RX_EVENT_ARM_STALE); --s_tdma_pio_spi_rx_arm_epoch;
    s_tdma_pio_spi_rx_arm_valid = false; query(11u, TDMA_RX_EVENT_ARM_STALE);
    s_tdma_pio_spi_rx_arm_valid = true;
    physical.rx_capture_active = false; query(11u, TDMA_RX_EVENT_ARM_STALE); physical.rx_capture_active = true;
    query_with(NULL, pin, 11u, 64u, TDMA_RX_EVENT_NO_CAPTURE);
    tdma_rx_capture_t bad = capture; bad.flags = 0u;
    query_with(&bad, pin, 11u, 64u, TDMA_RX_EVENT_NO_CAPTURE);
    bad = capture; bad.capture_id = 0u; query_with(&bad, pin, 11u, 64u, TDMA_RX_EVENT_NO_CAPTURE);
    bad = capture; bad.arm_epoch = 0u; query_with(&bad, pin, 11u, 64u, TDMA_RX_EVENT_NO_CAPTURE);
    bad = capture; bad.persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL;
    query_with(&bad, pin, 11u, 64u, TDMA_RX_EVENT_UNSUPPORTED_PERSONA);
    const uint32_t count = last()->query_count;
    assert(tdma_pio_spi_phys_rx_event_pin(NULL, &capture) == 0u);
    tdma_pio_spi_phys_rx_event_query(NULL, &capture, pin, 11u, 64u, 0u);
    assert(last()->query_count == count);
    query(11u, TDMA_RX_EVENT_MATCHED);
    require_retired = true; tdma_pio_spi_phys_event_stop(&physical); require_retired = false;
    assert(last()->capture_id == capture.capture_id && (last()->flags & TDMA_RX_EVENT_RETIRED) != 0u);
    assert(tdma_pio_spi_phys_rx_event_pin(&physical, &capture) == 0u);
    query(11u, TDMA_RX_EVENT_OBSERVER_UNAVAILABLE);
    const tdma_rx_capture_t old = capture;
    setup_candidate(true); retain(11u);
    query_with(&old, good_pin, 11u, 64u, TDMA_RX_EVENT_ARM_STALE);
}
static void test_initial_observation_epoch_and_loss(void) {
    setup_candidate(true); retain(1u);
    /* Execute the real counter reset used by RX ARM. Zero belongs to this
     * nonzero ARM, rather than denoting an absent capture or observer. */
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,
        physical.flight_physical_byte_count, 100u));
    capture.observation_epoch = s_tdma_pio_spi_rx_sequence.observation_epoch;
    assert(capture.observation_epoch == 0u);
    pin = tdma_pio_spi_phys_rx_event_pin(&physical, &capture);
    assert(pin == s_tdma_event_observer.epoch && pin != 0u);
    query(1u, TDMA_RX_EVENT_MATCHED);
    assert(last()->observation_epoch == 0u && last()->event_ordinal == 0u);
    const tdma_rx_capture_t old = capture;
    const uint32_t old_pin = pin;
    uint64_t produced;
    bool lost;
    const uint32_t reload = s_tdma_pio_spi_rx_sequence.reload_words;
    /* An unchanged count after a full possible wrap cannot preserve epoch 0. */
    assert(tdma_rx_dma_counter_observe(&s_tdma_pio_spi_rx_sequence, reload,
        100u + reload, 100u + reload, &produced, &lost));
    assert(lost && s_tdma_pio_spi_rx_sequence.observation_epoch == 1u);
    assert(tdma_pio_spi_phys_rx_event_pin(&physical, &capture) == 0u);
    query(1u, TDMA_RX_EVENT_OBSERVATION_STALE);
    require_retired = true; tdma_pio_spi_phys_event_stop(&physical); require_retired = false;
    assert(tdma_pio_spi_phys_rx_event_pin(&physical, &capture) == 0u);
    setup_candidate(true); retain(1u);
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,
        physical.flight_physical_byte_count, 200u));
    /* A later ARM also starts at zero; equality cannot revive the old ARM. */
    capture.observation_epoch = 0u;
    assert(tdma_pio_spi_phys_rx_event_pin(&physical, &old) == 0u);
    query_with(&old, old_pin, 1u, 64u, TDMA_RX_EVENT_ARM_STALE);
    pin = tdma_pio_spi_phys_rx_event_pin(&physical, &capture);
    assert(pin != 0u && pin != old_pin);
    query(1u, TDMA_RX_EVENT_MATCHED);
}
static void test_envelope_boundaries(void) {
    setup_candidate(true); retain(12u);
    for (uint32_t shift = 0u; shift < 8u; ++shift) {
        capture.bit_shift = shift;
        capture.produced_before = capture.candidate + capture.frame_words + (shift != 0u);
        capture.produced_after = capture.produced_before;
        query(12u, TDMA_RX_EVENT_MATCHED);
        if (shift != 0u) {
            --capture.produced_before; query(12u, TDMA_RX_EVENT_BAD_ENVELOPE);
        }
    }
    capture.bit_shift = 0u; capture.produced_before = capture.candidate + 68u;
    capture.produced_after = capture.produced_before;
    const tdma_rx_capture_t good = capture;
    for (unsigned which = 0u; which < 7u; ++which) {
        tdma_rx_capture_t bad = good;
        if (which == 0u) bad.bit_shift = 8u;
        if (which == 1u) --bad.frame_words;
        if (which == 2u) bad.candidate = bad.produced_before + 1u;
        if (which == 3u) --bad.produced_before;
        if (which == 4u) --bad.produced_after;
        if (which == 5u) bad.produced_after = bad.candidate + TDMA_PIO_SPI_RX_RING_WORDS;
        if (which == 6u) bad.frame_words = UINT32_MAX;
        query_with(&bad, pin, 12u, 64u, TDMA_RX_EVENT_BAD_ENVELOPE);
    }
    const size_t bad_sizes[] = {0u, TDMA_TRANSPORT_FRAME_HEADER_SIZE - 1u,
        TDMA_TRANSPORT_SHORT_PACKET_MAX + 1u, SIZE_MAX};
    for (unsigned i = 0u; i < 4u; ++i)
        query_with(&good, pin, 12u, bad_sizes[i], TDMA_RX_EVENT_BAD_ENVELOPE);
    capture.candidate = UINT64_MAX - 68u;
    capture.produced_before = capture.produced_after = UINT64_MAX;
    query(12u, TDMA_RX_EVENT_MATCHED);
    capture.bit_shift = 1u; query(12u, TDMA_RX_EVENT_BAD_ENVELOPE);
}
static void test_history_loss_does_not_become_latest(void) {
    setup_candidate(true);
    query(100u, TDMA_RX_EVENT_NOT_FOUND);
    retain(100u); /* A late append does not retrospectively rewrite a miss. */
    assert(last()->reason == TDMA_RX_EVENT_NOT_FOUND && last()->history_accepted == 0u);
    for (uint32_t sequence = 101u; sequence < 120u; ++sequence) retain(sequence);
    query(100u, TDMA_RX_EVENT_NOT_FOUND);
    assert(last()->history_evicted == 4u && last()->history_oldest_ordinal == 4u);
    query(110u, TDMA_RX_EVENT_MATCHED);
    assert(last()->event_ordinal == 10u && last()->rx_elapsed_cycles == 550000u);
    assert(s_tdma_event_history.active && s_tdma_event_history.count == TDMA_EVENT_HISTORY_CAPACITY);
    /* Observer ACTIVE is not sufficient when its independent history fails. */
    assert(!tdma_event_history_append(&s_tdma_event_history, NULL, 0u, 21u));
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && !s_tdma_event_history.active);
    assert(tdma_pio_spi_phys_rx_event_pin(&physical, &capture) == 0u);
    query(110u, TDMA_RX_EVENT_HISTORY_UNAVAILABLE);
    assert(last()->history_reason == TDMA_EVENT_HISTORY_INCOMPLETE_BATCH);
    setup_candidate(true); retain(200u); retain(201u);
    s_tdma_event_history.records[1].sequence = 200u; /* Inject corrupt retained state. */
    query(200u, TDMA_RX_EVENT_AMBIGUOUS);
    assert(!s_tdma_event_history.active && last()->history_reason == TDMA_EVENT_HISTORY_AMBIGUOUS);
    setup_candidate(true); retain(300u);
    s_tdma_event_history.count = TDMA_EVENT_HISTORY_CAPACITY + 1u;
    query(300u, TDMA_RX_EVENT_HISTORY_REJECTED);
    assert(last()->history_query_reason == TDMA_EVENT_HISTORY_BAD_ARGUMENT);
}
static void push_first_event(void) {
    const uint32_t raw = UINT32_MAX - 31250u;
    fifo[1][0] = raw; fifo[1][1] = UINT32_MAX;
    fifo[2][0] = raw - 1u; fifo[2][1] = UINT32_MAX;
    fifo[3][0] = UINT32_C(0x01000000); /* Wire LE sequence 1, PIO shift direction. */
    bank.level[1] = bank.level[2] = 2u; bank.level[3] = 1u;
    clock_us = s_tdma_event_base_us + 1000u;
}
static void test_real_service_final_fault_and_deferred_publication(void) {
    setup_candidate(true); push_first_event();
    tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && s_tdma_event_history.accepted == 1u);
    const unsigned harvested = fifo_reads;
    query(1u, TDMA_RX_EVENT_MATCHED);
    assert(fifo_reads == harvested && s_tdma_event_candidate_dirty);
    const uint32_t guard = physical.flight_event_guard;
    tdma_pio_spi_phys_event_service(&physical);
    assert(physical.flight_event_guard == guard + 2u && !s_tdma_event_candidate_dirty);
    assert(published_slot()->candidate.reason == TDMA_RX_EVENT_MATCHED);
    const uint32_t published = s_tdma_event_snapshot.published;
    fault_at_hz_read = hz_reads + 2u; /* Fault becomes visible only at final check. */
    tdma_pio_spi_phys_event_service(&physical);
    fault_at_hz_read = 0u;
    assert(s_tdma_event_observer.state == TDMA_EVENT_INVALID && !s_tdma_event_history.active);
    assert(s_tdma_event_arm_epoch == 0u && s_tdma_event_snapshot.published == published);
    assert((published_slot()->candidate.flags & TDMA_RX_EVENT_RETIRED) != 0u);
    query(1u, TDMA_RX_EVENT_OBSERVER_UNAVAILABLE);
    assert(published_slot()->candidate.reason == TDMA_RX_EVENT_OBSERVER_UNAVAILABLE);
    setup_candidate(true); push_first_event();
    fault_at_hz_read = hz_reads + 2u;
    const uint32_t before = s_tdma_event_snapshot.published;
    tdma_pio_spi_phys_event_service(&physical); fault_at_hz_read = 0u;
    assert(s_tdma_event_observer.state == TDMA_EVENT_INVALID);
    assert(s_tdma_event_snapshot.published == before && s_tdma_event_history.accepted == 0u);
    query(1u, TDMA_RX_EVENT_OBSERVER_UNAVAILABLE);
}
static void test_counters_saturate_and_prepare_preserves_history(void) {
    setup_candidate(true); retain(9u);
    last()->query_count = last()->matched_count = UINT32_MAX - 1u;
    last()->unavailable_count = last()->stale_count = UINT32_MAX;
    last()->missing_count = last()->ambiguous_count = UINT32_MAX;
    query(9u, TDMA_RX_EVENT_MATCHED); query(9u, TDMA_RX_EVENT_MATCHED);
    assert(last()->query_count == UINT32_MAX && last()->matched_count == UINT32_MAX);
    query(10u, TDMA_RX_EVENT_NOT_FOUND); assert(last()->missing_count == UINT32_MAX);
    query_with(NULL, pin, 9u, 64u, TDMA_RX_EVENT_NO_CAPTURE); assert(last()->unavailable_count == UINT32_MAX);
    query_with(&capture, pin + 1u, 9u, 64u, TDMA_RX_EVENT_PIN_STALE); assert(last()->stale_count == UINT32_MAX);
    retain(10u); s_tdma_event_history.records[1].sequence = 9u;
    query(9u, TDMA_RX_EVENT_AMBIGUOUS); assert(last()->ambiguous_count == UINT32_MAX);
    const tdma_rx_event_candidate_snapshot_t before = *last();
    const tdma_ring_runtime_config_t config = {.cycle_period_ns = 1500000u};
    tdma_pio_spi_phys_event_prepare(&physical, &config);
    assert(last()->query_count == before.query_count && last()->matched_count == before.matched_count);
    assert(last()->capture_id == before.capture_id && last()->reason == before.reason);
    assert((last()->flags & TDMA_RX_EVENT_RETIRED) != 0u);
}
int main(void) {
    assert(adapter_regression_main() == 0);
    test_independent_record_and_wrapping_sequence();
    test_pin_does_not_borrow_latest_or_invent_frame_identity();
    test_lifetime_mismatches_and_unavailable();
    test_initial_observation_epoch_and_loss();
    test_envelope_boundaries();
    test_history_loss_does_not_become_latest();
    test_real_service_final_fault_and_deferred_publication();
    test_counters_saturate_and_prepare_preserves_history();
    printf("physical candidate: 8 case groups, %u query oracles; snapshot=%zu; no query MMIO\n",
        query_cases, sizeof(tdma_rx_event_candidate_snapshot_t));
    return 0;
}
'''


def run(directory: Path, source: str, name: str, *, enabled: bool) -> str:
    unit = directory / f"{name}.c"
    unit.write_text(source, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = directory / f"{name}.exe"
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/tdma/inc"), str(unit)]
    if enabled:
        command += [str(ROOT / "components/tdma/src/tdma_event_observer.c"),
                    str(ROOT / "components/tdma/src/tdma_event_history.c"),
                    str(ROOT / "components/tdma/src/tdma_rx_sequence.c")]
    command += ["-o", str(executable)]
    for stage, call in (("compile", command), ("run", [str(executable)])):
        result = subprocess.run(call, capture_output=True, text=True)
        (directory / f"{name}-{stage}.json").write_text(json.dumps({
            "command": call, "returncode": result.returncode}, indent=2), encoding="utf-8")
        (directory / f"{name}-{stage}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def test_real_physical_candidate_epoch_history_and_fault_boundaries(tmp_path: Path) -> None:
    output = run(tmp_path, enabled_source(tmp_path), "candidate", enabled=True)
    assert "physical candidate: 8 case groups" in output
    assert "snapshot=176; no query MMIO" in output


def test_disabled_observer_is_explicitly_unavailable(tmp_path: Path) -> None:
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    end = header.index("} tdma_pio_spi_event_snapshot_t;") + len("} tdma_pio_spi_event_snapshot_t;")
    start = header.rfind("typedef struct {", 0, end)
    source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tdma_rx_capture.h"
#include "tdma_rx_event_candidate.h"
typedef unsigned uint;
''' + header[start:end] + r'''
typedef struct {
    struct { tdma_pio_spi_event_snapshot_t event; } snapshot;
    tdma_pio_spi_event_snapshot_t flight_event_alternate;
    uint32_t flight_event_guard;
} tdma_pio_spi_phys_t;
typedef struct { uint32_t cycle_period_ns; } tdma_ring_runtime_config_t;
static uint64_t time_us_64(void) { return 1u; }
static uint32_t save_and_disable_interrupts(void) { return 0u; }
static void restore_interrupts(uint32_t saved) { assert(saved == 0u); }
#define __dmb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define PROJECT_TDMA_EVENT_OBSERVER 0
''' + f'#include "{SOURCE.as_posix()}"\n' + r'''
int main(void) {
    tdma_pio_spi_phys_t phys = {0};
    tdma_rx_start_cut_t cut;
    memset(&cut, 0xff, sizeof(cut));
    tdma_rx_start_cut_arm_begin();
    tdma_rx_start_cut_disarmed();
    assert(!tdma_pio_spi_phys_get_rx_start_cut(&cut));
    const tdma_rx_start_cut_t empty_cut = {0};
    assert(memcmp(&cut, &empty_cut, sizeof(cut)) == 0);
    assert(!tdma_pio_spi_phys_get_rx_start_cut(NULL));
    const tdma_rx_capture_t capture = {.capture_id = 7u, .arm_epoch = 9u};
    assert(!tdma_pio_spi_phys_event_selected(&phys));
    assert(tdma_pio_spi_phys_rx_event_pin(&phys, &capture) == 0u);
    tdma_pio_spi_phys_rx_event_query(&phys, &capture, 0u, 0u, 64u, 8u);
    tdma_pio_spi_event_snapshot_t out;
    assert(tdma_pio_spi_phys_event_copy(&phys, &out));
    assert(out.candidate.reason == TDMA_RX_EVENT_OBSERVER_DISABLED);
    assert(out.candidate.capture_id == 7u && out.candidate.unavailable_count == 1u);
    assert((out.candidate.flags & (TDMA_RX_EVENT_HISTORICAL | TDMA_RX_EVENT_RETIRED)) != 0u);
    assert((out.candidate.flags & (TDMA_RX_EVENT_TIMESTAMP_VALID | TDMA_RX_EVENT_DPLL_ELIGIBLE)) == 0u);
    tdma_pio_spi_phys_event_service(&phys);
    tdma_pio_spi_phys_event_stop(&phys);
    tdma_pio_spi_phys_event_prepare(&phys, NULL);
    assert(tdma_pio_spi_phys_event_copy(&phys, &out));
    assert(out.candidate.query_count == 1u && out.candidate.capture_id == 7u);
    puts("observer disabled: historical unavailable only");
    return 0;
}
'''
    assert "observer disabled: historical unavailable only" in run(tmp_path, source, "disabled", enabled=False)
