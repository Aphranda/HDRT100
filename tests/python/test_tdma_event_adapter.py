"""Execute production event double-buffer and persona retirement with hardware hooks.

The fence hook can inject an inconsistent 64-bit copied value and a concurrent
writer generation. Acceptance/retry/timeout decisions remain the real C reader.
PIO operations assert software retirement precedes register/FIFO mutation.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def production(directory: Path) -> str:
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys_event.inc").read_text(encoding="utf-8")
    phys = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    end = header.index("} tdma_pio_spi_event_snapshot_t;") + len("} tdma_pio_spi_event_snapshot_t;")
    start = header.rfind("typedef struct {", 0, end)
    snapshot = header[start:end]
    pioasm = os.environ.get("PIOASM") or shutil.which("pioasm") or str(
        Path.home() / ".pico-sdk/tools/2.2.0/pioasm/pioasm.exe")
    generated = directory / "event.pio.h"
    subprocess.run([pioasm, "-o", "c-sdk", str(ROOT / "components/tdma/src/tdma_event.pio"),
                    str(generated)], check=True, capture_output=True)
    offsets = "\n".join(line for line in generated.read_text(encoding="utf-8").splitlines()
                        if line.startswith("#define tdma_event_"))
    contract_definitions = []
    for file, symbol in (("tdma_transport_frame.h", "TDMA_TRANSPORT_FRAME_SEQUENCE_OFFSET"),
                         ("tdma_rx_scan.h", "TDMA_PIO_SPI_PACKET_HEADER_SIZE")):
        contract = (ROOT / "components/tdma/inc" / file).read_text(encoding="utf-8")
        definition = re.search(rf"(?m)^#define {symbol}\s+[^\n]+$", contract)
        assert definition, symbol
        contract_definitions.append(definition.group(0))
    routines = []
    signatures = [
        ("bool", "tdma_pio_spi_phys_event_selected", "const tdma_pio_spi_phys_t *phys"),
        ("uint64_t", "tdma_event_cycles", "uint64_t us, bool upper"),
        ("uint32_t", "tdma_event_faults", "tdma_pio_spi_phys_t *phys"),
        ("void", "tdma_event_publish_state", "tdma_pio_spi_phys_t *phys"),
        ("bool", "tdma_pio_spi_phys_event_copy", "const tdma_pio_spi_phys_t *phys, tdma_pio_spi_event_snapshot_t *out"),
        ("void", "tdma_pio_spi_phys_event_stop", "tdma_pio_spi_phys_t *phys"),
        ("void", "tdma_pio_spi_phys_event_prepare", "tdma_pio_spi_phys_t *phys, const tdma_ring_runtime_config_t *config"),
        ("void", "tdma_event_start", "tdma_pio_spi_phys_t *phys"),
    ]
    for result, name, arguments in signatures:
        routines.append(f"static {result} {name}({arguments}) {{" + c_definition_body(source, name) + "}\n")
    name = "tdma_pio_spi_phys_select_program_persona"
    routines.append(f"static bool {name}(tdma_pio_spi_phys_t *phys, tdma_pio_spi_program_persona_t persona) {{" +
                    c_definition_body(phys, name) + "}\n")
    name = "tdma_pio_spi_phys_prepare_sm_pair"
    routines.append(f"static void {name}(tdma_pio_spi_phys_t *phys) {{" +
                    c_definition_body(phys, name) + "}\n")
    return (PREFIX + snapshot + "\n" + offsets + "\n" +
            "\n".join(contract_definitions) + "\n" + FIXTURE + "\n".join(routines) + ASSERTIONS)


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "tdma_event_observer.h"
typedef unsigned uint;
'''

FIXTURE = r'''
typedef struct {
    uint32_t ctrl, fdebug, dbg_padout, instr_mem[32];
    uint32_t level[4], pc[4], restarts[4], clears[4];
} bank_t;
typedef bank_t *PIO;
typedef struct { uint unused; } pio_sm_config;
typedef struct {
    unsigned role;
    struct { PIO tx_pio, rx_pio; } flight_resources;
    struct { tdma_pio_spi_event_snapshot_t event; } snapshot;
    tdma_pio_spi_event_snapshot_t flight_event_alternate;
    uint32_t flight_event_guard;
    bool flight_overlay_alignment_locked;
    uint32_t flight_overlay_alignment_samples, rx_csn_pin, tx_csn_pin;
    uint32_t flight_alignment_byte_shift, flight_alignment_bit_shift;
    uint32_t flight_physical_byte_count, rx_sck_pin, rx_pin;
    uint32_t flight_data_phase_delay_cycles, flight_marker_phase_delay_cycles, baud_hz;
} tdma_pio_spi_phys_t;
typedef unsigned tdma_pio_spi_program_persona_t;
typedef struct { uint32_t cycle_period_ns; } tdma_ring_runtime_config_t;
enum { TDMA_PIO_SPI_ROLE_MASTER = 0u, TDMA_PIO_SPI_ROLE_SLAVE = 1u,
       TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL = 1u,
       TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN = 11u,
       TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER = 13u,
       TDMA_EVENT_RX_SM = 1u, TDMA_EVENT_TX_SM = 2u, TDMA_EVENT_SEQUENCE_SM = 3u,
       TDMA_EVENT_SM_MASK = 14u, PIO_FDEBUG_RXSTALL_LSB = 0u,
       TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES = 3u,
       PIO_FIFO_JOIN_RX = 1u, STATUS_IRQ_SET = 1u, clk_sys = 0u,
       pio_x = 1u, pio_y = 2u, pio_null = 3u, pio_osr = 4u, pio_isr = 5u };
static tdma_event_observer_t s_tdma_event_observer;
static tdma_pio_spi_event_snapshot_t s_tdma_event_snapshot;
static uint32_t s_tdma_event_epoch, s_tdma_event_hz, s_tdma_event_period_ns;
static uint64_t s_tdma_event_base_us, s_tdma_event_last_service_us;
static bool s_tdma_event_waiting;
static uint s_tdma_event_counter_offset = 10u, s_tdma_event_sequence_offset = 20u;
static tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
static int s_tdma_pio_spi_program_manager;
static bank_t bank, rx_bank;
static tdma_pio_spi_phys_t physical;
static tdma_pio_spi_event_snapshot_t copied;
static uint64_t clock_us = 100u, read_delay;
static unsigned time_reads, guard_reads, fences, publication_stores, publication_barriers;
static unsigned mutation_mode, commit_reads;
static unsigned interrupt_saves, interrupt_restores, interrupt_depth, blocked_preemptions;
static uint32_t interrupt_mask, saved_interrupt_mask;
static bool preemption_pending;
static unsigned disabled_masks, enabled_masks, irq_clears, selectors;
static bool copying, require_retired, allow_pair_reset, inspect_commit;
static uint64_t expected_before_commit, expected_after_commit;
static const uint64_t OLD_RX = UINT64_C(0x1111111122222222);
static const uint64_t NEW_RX = UINT64_C(0x3333333344444444);
static const uint64_t TARGET_BEFORE_COPY = UINT64_C(0xdeadbeafcafef00d);
static uint32_t save_and_disable_interrupts(void) {
    assert(interrupt_depth == 0u);
    saved_interrupt_mask = interrupt_mask;
    interrupt_mask = 1u;
    ++interrupt_depth; ++interrupt_saves;
    return saved_interrupt_mask;
}
static void restore_interrupts(uint32_t previous) {
    assert(interrupt_depth == 1u && interrupt_mask == 1u);
    assert(previous == saved_interrupt_mask);
    interrupt_mask = previous;
    --interrupt_depth; ++interrupt_restores;
}
static uint64_t time_us_64(void) {
    if (copying) {
        assert(interrupt_depth == 1u && interrupt_mask == 1u);
        if (preemption_pending) {
            if (interrupt_mask == 0u) clock_us += 5000u;
            else ++blocked_preemptions;
        }
        return clock_us + (time_reads++ == 0u ? 0u : read_delay);
    }
    return clock_us++;
}
static tdma_pio_spi_event_snapshot_t *published_slot(void) {
    return ((physical.flight_event_guard >> 1u) & 1u) != 0u
        ? &physical.flight_event_alternate : &physical.snapshot.event;
}
static tdma_pio_spi_event_snapshot_t *unpublished_slot(void) {
    return ((physical.flight_event_guard >> 1u) & 1u) != 0u
        ? &physical.snapshot.event : &physical.flight_event_alternate;
}
static bool tdma_pio_spi_phys_event_copy(const tdma_pio_spi_phys_t *phys,
                                        tdma_pio_spi_event_snapshot_t *out);
static bool checked_event_copy(const tdma_pio_spi_phys_t *phys,
                                tdma_pio_spi_event_snapshot_t *out) {
    const uint32_t previous_mask = interrupt_mask;
    const unsigned saves_before = interrupt_saves, restores_before = interrupt_restores;
    const bool result = tdma_pio_spi_phys_event_copy(phys, out);
    assert(interrupt_mask == previous_mask && interrupt_depth == 0u);
    assert(interrupt_saves == saves_before + 1u && interrupt_restores == restores_before + 1u);
    return result;
}
static uint32_t load_guard(const uint32_t *ptr) { ++guard_reads; return *ptr; }
static void store_guard(uint32_t *ptr, uint32_t value) {
    assert((value & 1u) == 0u); /* Publication never makes the current slot unreadable. */
    assert(value == *ptr + 2u);
    assert(publication_barriers == publication_stores + 1u);
    if (inspect_commit) {
        tdma_pio_spi_event_snapshot_t before;
        assert(unpublished_slot()->rx_elapsed_cycles == expected_after_commit);
        assert(published_slot()->rx_elapsed_cycles == expected_before_commit);
        assert(checked_event_copy(&physical, &before));
        assert(before.rx_elapsed_cycles == expected_before_commit);
        ++commit_reads;
    }
    ++publication_stores;
    *ptr = value;
}
static void writer_barrier(void) {
    /* This is the ordering point BEFORE old-slot reuse, not the release
     * publication. A barrier moved after the target copy fails this check. */
    assert((physical.flight_event_guard & 1u) == 0u);
    assert(publication_barriers == publication_stores);
    if (inspect_commit) {
        assert(unpublished_slot()->rx_elapsed_cycles == TARGET_BEFORE_COPY);
        assert(published_slot()->rx_elapsed_cycles == expected_before_commit);
    }
    ++publication_barriers;
}
static void read_fence(void) {
    assert(interrupt_depth == 1u && interrupt_mask == 1u);
    ++fences;
    if (((mutation_mode == 1u || mutation_mode == 5u) && fences == 1u) ||
        mutation_mode == 2u || mutation_mode == 3u) {
        /* A test hook creates exactly the torn result that a 32-bit copy
         * overlapping a writer could leave. The source reader is unchanged. */
        copied.rx_elapsed_cycles = (OLD_RX & UINT64_C(0xffffffff00000000)) |
                                    (NEW_RX & UINT64_C(0xffffffff));
        for (unsigned write = 0u; write < (mutation_mode == 5u ? 2u : 1u); ++write) {
            const uint32_t next_epoch = published_slot()->epoch + 1u;
            tdma_pio_spi_event_snapshot_t *next = unpublished_slot();
            next->epoch = next_epoch;
            next->rx_elapsed_cycles = NEW_RX;
            next->tx_elapsed_cycles = NEW_RX + 8u;
            physical.flight_event_guard += 2u;
        }
        if (mutation_mode == 3u) physical.flight_event_guard -= 2u; /* Simulated guard ABA. */
    } else if (mutation_mode == 4u) {
        /* A writer may partially fill its alternate slot without committing.
         * Readers of the current slot must still complete on this attempt. */
        unpublished_slot()->rx_elapsed_cycles = UINT64_C(0xaaaaaaaa44444444);
        unpublished_slot()->epoch = 999u;
    }
}
#define __atomic_load_n(ptr, order) load_guard(ptr)
#define __atomic_store_n(ptr, value, order) store_guard(ptr, value)
#define __atomic_thread_fence(order) read_fence()
#define __dmb() writer_barrier()
static uint32_t clock_get_hz(unsigned ignored) { (void)ignored; return 125000000u; }
static uint32_t gpio_get_all(void) { return UINT32_MAX; }
static uint32_t pio_sm_get_pc(PIO pio, uint sm) { return pio->pc[sm]; }
static void check_retired(void) {
    if (!require_retired) return;
    assert(s_tdma_event_observer.state == TDMA_EVENT_STOPPED);
    assert(!s_tdma_event_waiting);
    for (uint i = 0; i < TDMA_EVENT_STREAMS; ++i)
        assert(s_tdma_event_observer.pending_count[i] == 0u);
}
static void pio_set_sm_mask_enabled(PIO pio, uint mask, bool enabled) {
    assert(mask == TDMA_EVENT_SM_MASK && !enabled);
    check_retired();
    ++disabled_masks;
    pio->ctrl &= ~mask;
}
static void pio_sm_clear_fifos(PIO pio, uint sm) {
    check_retired(); assert(sm != 0u || allow_pair_reset); pio->level[sm] = 0u; ++pio->clears[sm];
}
static void pio_sm_restart(PIO pio, uint sm) {
    check_retired(); assert(sm != 0u || allow_pair_reset); ++pio->restarts[sm];
}
static void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) {
    check_retired(); assert(allow_pair_reset && !enabled); pio->ctrl &= ~(1u << sm);
}
#define tdma_pio_spi_phys_control_pio(phys) (&bank)
#define tdma_pio_spi_phys_data_pio(phys) (&rx_bank)
#define tdma_pio_spi_phys_capture_pio(phys) (&rx_bank)
#define tdma_pio_spi_phys_evidence_pio(phys) (&bank)
#define tdma_pio_spi_phys_control_sm(phys) 0u
#define tdma_pio_spi_phys_data_sm(phys) 0u
#define tdma_pio_spi_phys_capture_sm(phys) 1u
#define tdma_pio_spi_phys_latch_sm(phys) 2u
#define tdma_pio_spi_phys_rtt_sm(phys) 3u
#define tdma_pio_spi_phys_is_flight_persona() \
    (s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)
static void pio_interrupt_clear(PIO pio, uint irq) {
    (void)pio; assert(irq == 1u || irq == 2u); ++irq_clears;
}
static pio_sm_config tdma_event_counter_program_get_default_config(uint offset) {
    assert(offset == 10u); return (pio_sm_config){0u};
}
static pio_sm_config tdma_event_sequence_program_get_default_config(uint offset) {
    assert(offset == 20u); return (pio_sm_config){0u};
}
#define sm_config_set_jmp_pin(...) ((void)0)
#define sm_config_set_in_shift(...) ((void)0)
#define sm_config_set_fifo_join(...) ((void)0)
#define sm_config_set_clkdiv_int_frac(...) ((void)0)
#define sm_config_set_in_pins(...) ((void)0)
#define sm_config_set_mov_status(...) ((void)0)
static void pio_sm_init(PIO pio, uint sm, uint offset, const pio_sm_config *config) {
    (void)config; assert(sm != 0u); pio->pc[sm] = offset; pio->level[sm] = 0u;
}
static void pio_sm_exec(PIO pio, uint sm, uint instruction) {
    (void)pio; (void)instruction; assert(sm != 0u);
}
static uint pio_encode_mov_not(uint a, uint b) { return a + b; }
static uint pio_encode_wait_gpio(bool high, uint pin) { return (uint)high + pin; }
static uint pio_encode_delay(uint delay) { return delay; }
static uint pio_encode_set(uint dest, uint value) { return dest + value; }
static uint pio_encode_in(uint src, uint count) { return src + count; }
static uint pio_encode_mov(uint dst, uint src) { return dst + src; }
static void pio_enable_sm_mask_in_sync(PIO pio, uint mask) {
    assert(mask == TDMA_EVENT_SM_MASK); ++enabled_masks;
    pio->ctrl |= mask;
    pio->fdebug = 0u; /* Emulate previous W1C write to hardware fdebug. */
}
static void tdma_pio_spi_phys_origin_record_invalidate(tdma_pio_spi_phys_t *phys) { (void)phys; }
static bool tdma_pio_spi_programs_select(int *manager, tdma_pio_spi_phys_t *phys,
                                        tdma_pio_spi_program_persona_t persona) {
    (void)manager; (void)phys; ++selectors;
    if (s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER &&
        persona != s_tdma_pio_spi_program_persona) {
        check_retired(); assert((bank.ctrl & TDMA_EVENT_SM_MASK) == 0u);
    }
    s_tdma_pio_spi_program_persona = persona;
    return true;
}
'''

ASSERTIONS = r'''
/* This wrapper is used only by the tests below. Extracted production reader
 * above remains unchanged and every return is checked for mask restoration. */
#define tdma_pio_spi_phys_event_copy checked_event_copy
static void reset_reader(unsigned mode, uint64_t delay) {
    mutation_mode = mode; read_delay = delay; time_reads = guard_reads = fences = 0u;
    physical.flight_event_guard = 0u;
    physical.snapshot.event = (tdma_pio_spi_event_snapshot_t){
        .epoch = 7u, .rx_elapsed_cycles = OLD_RX, .tx_elapsed_cycles = OLD_RX + 8u};
    physical.flight_event_alternate = (tdma_pio_spi_event_snapshot_t){0};
    memset(&copied, 0, sizeof(copied));
    copying = true;
}
static void test_reader(void) {
    s_tdma_event_base_us = 0u; s_tdma_event_hz = 125000000u;
    assert(tdma_event_cycles(UINT64_MAX, true) == UINT64_MAX);
    assert(tdma_event_cycles(UINT64_MAX, false) == UINT64_MAX);
    assert(tdma_event_cycles(10u, false) == 1250u);
    assert(tdma_event_cycles(10u, true) == 1375u);
    reset_reader(0u, 0u);
    physical.flight_event_guard = 3u;
    assert(!tdma_pio_spi_phys_event_copy(&physical, &copied));
    assert(guard_reads == 3u && fences == 0u);
    reset_reader(0u, 1000u);
    assert(tdma_pio_spi_phys_event_copy(&physical, &copied));
    assert(copied.epoch == 7u && copied.rx_elapsed_cycles == OLD_RX);
    reset_reader(0u, 1001u);
    assert(!tdma_pio_spi_phys_event_copy(&physical, &copied));
    reset_reader(0u, UINT64_MAX); /* Fake backward clock must reject. */
    assert(!tdma_pio_spi_phys_event_copy(&physical, &copied));
    reset_reader(1u, 2u);
    assert(tdma_pio_spi_phys_event_copy(&physical, &copied));
    assert(fences == 2u && copied.epoch == 8u);
    assert(copied.rx_elapsed_cycles == NEW_RX && copied.tx_elapsed_cycles == NEW_RX + 8u);
    reset_reader(2u, 2u);
    assert(!tdma_pio_spi_phys_event_copy(&physical, &copied));
    assert(fences == 3u); /* Never accept continually torn/rewritten data. */
    reset_reader(5u, 2u);
    assert(tdma_pio_spi_phys_event_copy(&physical, &copied));
    assert(fences == 2u && physical.flight_event_guard == 4u);
    assert(copied.epoch == 9u && copied.rx_elapsed_cycles == NEW_RX); /* Slot reused twice. */
    reset_reader(3u, 1001u);
    assert(!tdma_pio_spi_phys_event_copy(&physical, &copied)); /* Guard ABA cannot bypass age. */
    reset_reader(4u, 2u);
    assert(tdma_pio_spi_phys_event_copy(&physical, &copied));
    assert(fences == 1u && copied.epoch == 7u && copied.rx_elapsed_cycles == OLD_RX);
    copying = false; mutation_mode = 0u;
}
static void test_publish_alternate_before_commit(void) {
    reset_reader(0u, 0u);
    tdma_event_observer_init(&s_tdma_event_observer);
    inspect_commit = true;
    for (unsigned version = 0u; version < 2u; ++version) {
        expected_before_commit = version == 0u ? OLD_RX : NEW_RX;
        expected_after_commit = version == 0u ? NEW_RX : OLD_RX;
        s_tdma_event_observer.epoch = 8u + version;
        s_tdma_event_snapshot.rx_elapsed_cycles = expected_after_commit;
        s_tdma_event_snapshot.tx_elapsed_cycles = expected_after_commit + 8u;
        unpublished_slot()->rx_elapsed_cycles = TARGET_BEFORE_COPY;
        tdma_event_publish_state(&physical);
        assert(physical.flight_event_guard == (version + 1u) * 2u);
        assert(tdma_pio_spi_phys_event_copy(&physical, &copied));
        assert(copied.epoch == 8u + version && copied.rx_elapsed_cycles == expected_after_commit);
        assert(copied.tx_elapsed_cycles == expected_after_commit + 8u);
    }
    assert(commit_reads == 2u);
    assert(publication_barriers == 2u && publication_stores == 2u);
    inspect_commit = false; copying = false;
}
static void test_copy_cannot_be_locally_preempted(void) {
    reset_reader(0u, 0u);
    const uint64_t before = clock_us;
    const unsigned blocked_before = blocked_preemptions;
    preemption_pending = true;
    assert(tdma_pio_spi_phys_event_copy(&physical, &copied));
    assert(blocked_preemptions >= blocked_before + 2u);
    assert(clock_us == before && copied.rx_elapsed_cycles == OLD_RX);
    preemption_pending = false; copying = false;
}
static void prepare_follower(void) {
    const tdma_ring_runtime_config_t config = {.cycle_period_ns = 1500000u};
    tdma_pio_spi_phys_event_prepare(&physical, &config);
    assert(s_tdma_event_waiting && s_tdma_event_observer.state == TDMA_EVENT_STOPPED);
    assert(published_slot()->waiting == 1u);
    require_retired = false;
    tdma_event_start(&physical);
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE);
    assert(!s_tdma_event_waiting && (bank.ctrl & TDMA_EVENT_SM_MASK) == TDMA_EVENT_SM_MASK);
    assert(published_slot()->epoch == s_tdma_event_epoch);
}
static void put_pending(void) {
    /* Enter through the real core consumer: one raw word awaits its Y pair. */
    tdma_event_batch_t input = {.epoch = s_tdma_event_observer.epoch,
        .observed = {s_tdma_event_observer.start.hi, s_tdma_event_observer.start.hi},
        .count = {1u, 0u, 0u}, .empty_mask = TDMA_EVENT_ALL_EMPTY_MASK};
    input.words[0][0] = UINT32_MAX - 1u;
    tdma_event_record_t output[TDMA_EVENT_MAX_RECORDS];
    assert(tdma_event_observer_feed(&s_tdma_event_observer, &input, output) == 0u);
    assert(s_tdma_event_observer.pending_count[0] == 1u);
}
static void test_retirement_and_persona_rearm(void) {
    memset(&bank, 0, sizeof(bank));
    memset(&physical, 0, sizeof(physical));
    physical.role = TDMA_PIO_SPI_ROLE_SLAVE;
    physical.flight_resources.tx_pio = &bank;
    physical.flight_resources.rx_pio = &rx_bank;
    physical.flight_overlay_alignment_locked = true;
    physical.flight_overlay_alignment_samples = 3u;
    physical.rx_csn_pin = 27u; physical.tx_csn_pin = 24u;
    physical.rx_sck_pin = 28u; physical.rx_pin = 29u;
    physical.flight_physical_byte_count = 240u; physical.baud_hz = 8000000u;
    bank.dbg_padout = UINT32_MAX;
    bank.ctrl = 1u; bank.level[0] = 5u; bank.pc[0] = 9u;
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
    prepare_follower();
    const uint32_t first_epoch = s_tdma_event_epoch;
    put_pending();
    for (uint sm = 1u; sm <= 3u; ++sm) bank.level[sm] = 3u;
    /* Configure can change the requested role before the installed follower
     * is retired. Cleanup must follow installed ownership, not requested role. */
    physical.role = TDMA_PIO_SPI_ROLE_MASTER;
    require_retired = true;
    tdma_pio_spi_phys_event_stop(&physical);
    assert(s_tdma_event_observer.state == TDMA_EVENT_STOPPED);
    assert(published_slot()->pending_rx == 0u && published_slot()->waiting == 0u);
    assert(bank.ctrl == 1u && bank.level[0] == 5u && bank.pc[0] == 9u);
    assert(bank.restarts[0] == 0u && bank.clears[0] == 0u);
    for (uint sm = 1u; sm <= 3u; ++sm)
        assert(bank.level[sm] == 0u && bank.restarts[sm] == 1u && bank.clears[sm] == 1u);
    physical.role = TDMA_PIO_SPI_ROLE_SLAVE;
    prepare_follower();
    assert(s_tdma_event_epoch == first_epoch + 1u);
    put_pending();
    physical.role = TDMA_PIO_SPI_ROLE_MASTER;
    require_retired = true;
    assert(tdma_pio_spi_phys_select_program_persona(&physical, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL));
    assert(s_tdma_event_observer.state == TDMA_EVENT_STOPPED);
    const unsigned stopped = disabled_masks;
    tdma_pio_spi_phys_event_stop(&physical);
    assert(disabled_masks == stopped); /* Maintenance resources are not touched. */
    assert(tdma_pio_spi_phys_select_program_persona(&physical,
        TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER));
    physical.role = TDMA_PIO_SPI_ROLE_SLAVE;
    prepare_follower();
    assert(s_tdma_event_epoch == first_epoch + 2u && selectors == 2u);
    assert(s_tdma_event_observer.pending_count[0] == 0u);
    assert((physical.flight_event_guard & 1u) == 0u && publication_stores != 0u);
    assert(enabled_masks == 3u && irq_clears >= 6u);
    /* The production broad SM-pair reset must also retire the software epoch
     * before it clears both complete hardware banks, including control SM0. */
    put_pending(); require_retired = true; allow_pair_reset = true;
    physical.role = TDMA_PIO_SPI_ROLE_MASTER;
    tdma_pio_spi_phys_prepare_sm_pair(&physical);
    assert(s_tdma_event_observer.state == TDMA_EVENT_STOPPED);
    assert(s_tdma_event_observer.pending_count[0] == 0u && bank.ctrl == 0u);
    assert(bank.clears[0] == 1u && rx_bank.clears[0] == 1u);
    allow_pair_reset = false;
    physical.role = TDMA_PIO_SPI_ROLE_SLAVE;
    prepare_follower();
    assert(s_tdma_event_epoch == first_epoch + 3u && enabled_masks == 4u);
}
int main(void) {
    /* Every success and rejection path is run with caller IRQs both enabled
     * and already disabled. The latter must never be unconditionally enabled. */
    for (uint32_t original = 0u; original < 2u; ++original) {
        interrupt_mask = original;
        test_reader();
        test_copy_cannot_be_locally_preempted();
        assert(interrupt_mask == original && interrupt_saves == interrupt_restores);
    }
    interrupt_mask = 0u;
    test_publish_alternate_before_commit();
    test_retirement_and_persona_rearm();
    puts("production adapter: seqlock tearing/age, STOP order, persona rearm passed");
    return 0;
}
'''


def test_production_event_seqlock_and_persona_retirement(tmp_path: Path) -> None:
    unit = tmp_path / "adapter.c"
    unit.write_text(production(tmp_path), encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "adapter.exe"
    built = subprocess.run([
        gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I" + str(ROOT / "components/tdma/inc"), str(unit),
        str(ROOT / "components/tdma/src/tdma_event_observer.c"), "-o", str(executable)],
        capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    ran = subprocess.run([str(executable)], capture_output=True, text=True)
    assert ran.returncode == 0, ran.stdout + ran.stderr
    assert "seqlock tearing/age, STOP order, persona rearm passed" in ran.stdout
