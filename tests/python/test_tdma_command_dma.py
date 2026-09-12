"""Run the physical adapter's real DMA routines against a deterministic host bus.

The host bus models AL3 triggers, descriptor gaps and abort completion. Timing
on RP2350 and DMA arbitration still require HIL; this checks pool ownership.
"""
import os
from pathlib import Path
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_three_level_abort_late_writes_and_shared_deadline(tmp_path):
    """Execute the production STOP algorithm with in-flight AL3 writes.

    The three-level fixture is the proposed origin dependency shape; it does
    not claim that the origin persona is installed or admitted on hardware.
    """
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    routines = "\n".join(kind + " " + name + signature + " {" +
        c_definition_body(source, name) + "}\n" for kind, name, signature in [
            ("static void", "tdma_pio_spi_phys_disable_dma_mask", "(uint32_t mask)"),
            ("static bool", "tdma_pio_spi_phys_stop_dma_level", "(uint32_t mask, uint64_t deadline)"),
            ("static bool", "tdma_pio_spi_phys_stop_dma_chain", "(uint32_t loader_mask, uint32_t executor_mask, uint32_t children_mask, uint64_t deadline)")])
    fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned uint;
enum { NUM_DMA_CHANNELS = 16, DMA_CH0_CTRL_TRIG_EN_BITS = 1, BUDGET = 64,
       LOADER = 1u << 6, EXECUTOR = 1u << 8, CHILDREN = (1u << 4) | (1u << 5),
       OWNED = LOADER | EXECUTOR | CHILDREN, FOREIGN = 1u << 7 };
static struct { struct { uint32_t ctrl_trig; } ch[16]; uint32_t abort; } bus;
#define dma_hw (&bus)
static bool busy[16];
static uint now, delay[3], remaining[3], races, barriers, touched, poll_mask;
static uint pending_abort, observation_mode;
static int hung;
static uint fifo_clears, pool_releases, irq_clears, sniff_ctrl;
static void __dmb(void) { ++barriers; }
static void hw_clear_bits(uint32_t *reg, uint32_t mask) {
    for (uint ch = 0; ch < 16; ++ch) if (reg == &bus.ch[ch].ctrl_trig) {
        assert((OWNED & (1u << ch)) != 0); touched |= 1u << ch;
        *reg &= ~mask; return;
    }
    assert(!"unowned register");
}
static bool dma_channel_is_busy(uint ch) {
    assert(OWNED & (1u << ch)); poll_mask |= 1u << ch; return busy[ch];
}
static uint64_t tdma_pio_spi_phys_now_us(void) {
    ++now;
    if (bus.abort & OWNED) pending_abort = bus.abort & OWNED;
    uint mask = pending_abort;
    if (mask) {
        int level = mask == LOADER ? 0 : mask == EXECUTOR ? 1 : 2;
        assert(mask == LOADER || mask == EXECUTOR || mask == CHILDREN);
        if (observation_mode == 1) bus.abort &= ~mask;
        if (observation_mode == 2)
            for (uint ch = 0; ch < 16; ++ch) if (mask & (1u << ch)) busy[ch] = false;
        if (hung != level && remaining[level] && --remaining[level] == 0) {
            /* The last bus write retires at abort completion, even if EN
             * was already clear. The downstream AL3 CTRL contains EN=1. */
            if (level == 0 && (races & 1)) {
                bus.ch[8].ctrl_trig |= 1; busy[8] = true;
            }
            if (level == 1 && (races & 2)) {
                bus.ch[4].ctrl_trig |= 1; busy[4] = true;
                bus.ch[5].ctrl_trig |= 1; busy[5] = true;
            }
            if (level == 1 && (races & 4)) {
                /* A branch READ_ADDR_TRIG must not revive disabled loader. */
                if (bus.ch[6].ctrl_trig & 1) busy[6] = true;
            }
            if (level == 2 && (races & 8)) {
                if (bus.ch[8].ctrl_trig & 1) busy[8] = true;
            }
            for (uint ch = 0; ch < 16; ++ch) if (mask & (1u << ch)) busy[ch] = false;
            bus.abort &= ~mask;
            pending_abort = 0;
        }
    }
    /* An unrelated abort/BUSY can persist throughout our STOP. */
    bus.abort |= FOREIGN;
    return now;
}
static void setup(void) {
    memset(&bus, 0, sizeof(bus)); memset(busy, 0, sizeof(busy));
    for (uint ch = 0; ch < 16; ++ch) if ((OWNED | FOREIGN) & (1u << ch)) {
        bus.ch[ch].ctrl_trig = 1; busy[ch] = true;
    }
    memcpy(remaining, delay, sizeof(remaining));
    now = barriers = touched = poll_mask = pending_abort = 0;
    fifo_clears = pool_releases = irq_clears = 0; sniff_ctrl = 0x123456;
}
static void assert_preserved(void) {
    assert(bus.ch[7].ctrl_trig == 1 && busy[7]);
    assert(fifo_clears == 0 && pool_releases == 0 && irq_clears == 0);
    assert(sniff_ctrl == 0x123456);
    assert(touched == OWNED);
}
static void assert_quiesced(void) {
    for (uint ch = 0; ch < 16; ++ch) if (OWNED & (1u << ch)) {
        assert(!busy[ch]); assert(!(bus.ch[ch].ctrl_trig & 1));
    }
    assert(!(bus.abort & OWNED)); assert(barriers > 0);
    assert_preserved();
}
'''
    assertions = r'''
int main(void) {
    uint success = 0, failures = 0;
    for (observation_mode = 0; observation_mode < 3; ++observation_mode)
    for (races = 0; races < 16; ++races)
    for (delay[0] = 1; delay[0] <= 4; ++delay[0])
    for (delay[1] = 1; delay[1] <= 4; ++delay[1])
    for (delay[2] = 1; delay[2] <= 4; ++delay[2]) {
        hung = -1; setup();
        assert(tdma_pio_spi_phys_stop_dma_chain(LOADER, EXECUTOR, CHILDREN, BUDGET));
        assert(now == delay[0] + delay[1] + delay[2]); assert_quiesced(); ++success;
    }
    observation_mode = 0; races = 15; delay[0] = delay[1] = delay[2] = 2;
    for (hung = 0; hung < 3; ++hung) {
        setup();
        assert(!tdma_pio_spi_phys_stop_dma_chain(LOADER, EXECUTOR, CHILDREN, BUDGET));
        assert(now == BUDGET && barriers == 0); assert_preserved();
        for (uint ch = 0; ch < 16; ++ch) if (OWNED & (1u << ch))
            assert(!(bus.ch[ch].ctrl_trig & 1));
        int saved = hung; hung = -1;
        memcpy(remaining, delay, sizeof(remaining));
        assert(tdma_pio_spi_phys_stop_dma_chain(LOADER, EXECUTOR, CHILDREN, now + BUDGET));
        assert_quiesced(); hung = saved; ++failures;
    }
    /* Each individual level fits, their sum exceeds the single budget. */
    hung = -1; delay[0] = 30; delay[1] = 30; delay[2] = 30; setup();
    assert(!tdma_pio_spi_phys_stop_dma_chain(LOADER, EXECUTOR, CHILDREN, BUDGET));
    assert(now == BUDGET && barriers == 0); assert_preserved(); ++failures;
    /* Negative control: child-first STOP leaves live children after the
     * executor's late CTRL write. This bus detects the original hazard. */
    delay[0] = delay[1] = delay[2] = 2; setup();
    tdma_pio_spi_phys_disable_dma_mask(OWNED);
    assert(tdma_pio_spi_phys_stop_dma_level(CHILDREN, BUDGET));
    assert(tdma_pio_spi_phys_stop_dma_level(LOADER, BUDGET));
    assert(tdma_pio_spi_phys_stop_dma_level(EXECUTOR, BUDGET));
    assert(busy[4] && busy[5] && (bus.ch[4].ctrl_trig & 1) && (bus.ch[5].ctrl_trig & 1));
    printf("three-level STOP: %u late-write cases, %u timeouts, retry and negative control passed\n", success, failures);
}
'''
    unit = tmp_path / "stop_chain.c"
    unit.write_text(fixture + routines + assertions, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = tmp_path / "stop_chain.exe"
    build = subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(unit), "-o", str(exe)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def test_descriptor_completion_and_bounded_stop(tmp_path):
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    routines = "\n".join(kind + " " + name + signature + " {" +
        c_definition_body(source, name) + "}\n" for kind, name, signature in [
            ("static void", "tdma_pio_spi_phys_disable_dma_mask", "(uint32_t mask)"),
            ("static bool", "tdma_pio_spi_phys_stop_dma_level", "(uint32_t mask, uint64_t deadline)"),
            ("static bool", "tdma_pio_spi_phys_stop_dma_chain", "(uint32_t loader_mask, uint32_t executor_mask, uint32_t children_mask, uint64_t deadline)"),
            ("static bool", "tdma_pio_spi_phys_stop_command_dma", "(tdma_pio_spi_phys_t *phys)"),
            ("static bool", "tdma_pio_spi_phys_overlay_dma_busy", "(tdma_pio_spi_phys_t *phys)"),
            ("static void", "tdma_pio_spi_phys_service_overlay_pending", "(tdma_pio_spi_phys_t *phys)"),
            ("bool", "tdma_pio_spi_phys_process_overlay_ready", "(void *context)"),
            ("static bool", "tdma_pio_spi_phys_start_overlay_script", "(tdma_pio_spi_phys_t *phys, uint32_t buffer_index)")])
    fixture = r'''
#include "tdma_flight_overlay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned uint;
enum { DMA_SIZE_32 = 2, DMA_CH0_CTRL_TRIG_EN_BITS = 1, DREQ_FORCE = 63, NUM_DMA_CHANNELS = 16,
       TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES = 2,
       TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US = 64, TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY = 6,
       TDMA_PIO_SPI_OVERLAY_ERROR_DMA_START_INVALID = 4, TDMA_PIO_SPI_OVERLAY_ERROR_DMA_BUSY_TIMEOUT = 3,
       TDMA_PIO_SPI_OVERLAY_ERROR_BUILD_FAILED = 2, TDMA_PIO_SPI_OVERLAY_ERROR_NONE = 0 };
typedef struct {
    uint32_t armed;
    uint32_t last_error, overlay_last_error, overlay_tx_dma_remaining, overlay_tx_dma_busy;
    uint32_t overlay_tx_fifo_level_at_fail, overlay_prepare_wait_us;
    uint32_t overlay_published_generation, overlay_selected_generation, overlay_selection_pending;
} Snapshot;
typedef struct {
    bool armed, flight_resource_claimed, flight_overlay_dma_active, flight_overlay_pending;
    bool flight_origin_workspace_owned, flight_origin_rx_observation_ready;
    bool rx_capture_active;
    uint32_t flight_overlay_active_buffer, flight_physical_byte_count, flight_overlay_pending_buffer;
    uint32_t flight_overlay_published_generation;
    uint32_t flight_overlay_alignment_samples;
    volatile uint32_t flight_overlay_selected_generation, flight_overlay_next_address;
    Snapshot snapshot;
} tdma_pio_spi_phys_t;
typedef struct { uintptr_t read_addr; uint32_t ctrl_trig, transfer_count, al3_ctrl, al3_read_addr_trig, write_addr; } Channel;
static struct { Channel ch[16]; uint32_t abort; } bus;
#define dma_hw (&bus)
static bool busy[16], hang_abort, stopped, enabled, late_trigger, late_restart;
static unsigned now, stop_clear_output_count, start_calls;
static int s_tdma_pio_spi_command_dma_channel = 6, s_tdma_pio_spi_tx_dma_channel = 5;
static int s_tdma_pio_spi_rx_dma_channel = 4;
static int s_tdma_pio_spi_executor_dma_channel = -1;
static tdma_flight_overlay_plan_t s_tdma_pio_spi_flight_overlay_plan[2];
static uint32_t s_tdma_pio_spi_flight_live_word = TDMA_FLIGHT_OVERLAY_LIVE_WORD;
static struct { uint32_t txf[4]; } pio;
typedef struct { uint32_t ctrl; } dma_channel_config;
typedef struct { unsigned descriptor_words, descriptor_write_ring_log2, control_descriptor_count; } tdma_state_machine_command_dma_contract_t;
static tdma_state_machine_command_dma_contract_t tdma_state_machine_command_dma_contract(void) {
    return (tdma_state_machine_command_dma_contract_t){4, 4, 2};
}
static uint32_t tdma_pio_spi_phys_overlay_final_pc(void) { return 20; }
static bool dma_channel_is_busy(uint ch) { return busy[ch]; }
static void hw_clear_bits(uint32_t *reg, uint32_t mask) {
    *reg &= ~mask;
    if (reg == &bus.ch[5].ctrl_trig) ++stop_clear_output_count;
}
static uint64_t tdma_pio_spi_phys_now_us(void) {
    ++now;
    if (!hang_abort && bus.abort) {
        if ((bus.abort & (1u << 6)) && late_trigger) {
            bus.ch[5].ctrl_trig |= 1; busy[5] = true; late_trigger = false;
        }
        if ((bus.abort & (1u << 5)) && late_restart) {
            /* A late output write changes READ_ADDR_TRIG but cannot enable
             * the disabled loader. It must not restart the control chain. */
            bus.ch[6].al3_read_addr_trig = 123;
            if (bus.ch[6].ctrl_trig & 1) busy[6] = true;
            late_restart = false;
        }
        for (uint ch = 0; ch < 16; ++ch) if (bus.abort & (1u << ch)) busy[ch] = false;
        bus.abort = 0;
    }
    return now;
}
static void __dmb(void) {}
static void tdma_pio_spi_phys_set_line_drivers(bool value) { enabled = value; }
static void tdma_pio_spi_phys_pause_sm_pair(tdma_pio_spi_phys_t *phys) { (void)phys; stopped = true; }
#define tdma_pio_spi_phys_data_pio(phys) (&pio)
#define tdma_pio_spi_phys_data_sm(phys) (2u)
#define pio_sm_get_tx_fifo_level(p, s) (0u)
#define pio_get_dreq(p, s, tx) (10u)
/* Config fields are disjoint in this host bus; the firmware uses SDK fields. */
static dma_channel_config dma_channel_get_default_config(uint ch) { return (dma_channel_config){1 | (ch << 8)}; }
static void channel_config_set_transfer_data_size(dma_channel_config *c, uint v) { c->ctrl |= v << 2; }
static void channel_config_set_high_priority(dma_channel_config *c, bool v) { c->ctrl |= (uint)v << 1; }
static void channel_config_set_write_increment(dma_channel_config *c, bool v) { c->ctrl = (c->ctrl & ~(1u << 5)) | ((uint)v << 5); }
static void channel_config_set_read_increment(dma_channel_config *c, bool v) { c->ctrl = (c->ctrl & ~(1u << 4)) | ((uint)v << 4); }
static void channel_config_set_dreq(dma_channel_config *c, uint v) { c->ctrl = (c->ctrl & ~(63u << 16)) | (v << 16); }
static void channel_config_set_chain_to(dma_channel_config *c, uint v) { c->ctrl = (c->ctrl & ~(15u << 8)) | (v << 8); }
static void channel_config_set_ring(dma_channel_config *c, bool write, uint size) { assert(write); c->ctrl |= size << 24; }
static uint32_t channel_config_get_ctrl_value(const dma_channel_config *c) { return c->ctrl; }
static void dma_channel_configure(uint ch, const dma_channel_config *c, uint32_t *write, const void *read, uint n, bool trigger) {
    assert(ch == 6 && write == &bus.ch[5].al3_ctrl && n == 4 && trigger);
    assert(((c->ctrl >> 8) & 15) == ch && (c->ctrl >> 24) == 4);
    assert((c->ctrl & 0x30) == 0x30);
    bus.ch[ch].read_addr = (uintptr_t)read; bus.ch[ch].ctrl_trig = c->ctrl;
    busy[ch] = true; ++start_calls;
}
'''
    assertions = r'''
static tdma_pio_spi_phys_t phys;
/* Descriptors contain RP2350 32-bit addresses. Resolve only registered host
 * objects and reject every unmapped address; never dereference a truncation. */
static void *resolve(uint32_t address, size_t size) {
    struct Region { void *base; size_t size; } regions[] = {
        {&phys, sizeof(phys)}, {&bus, sizeof(bus)}, {&pio, sizeof(pio)},
        {s_tdma_pio_spi_flight_overlay_plan, sizeof(s_tdma_pio_spi_flight_overlay_plan)},
        {&s_tdma_pio_spi_flight_live_word, sizeof(s_tdma_pio_spi_flight_live_word)}
    };
    for (unsigned i = 0; i < sizeof(regions)/sizeof(regions[0]); ++i) {
        uint32_t offset = address - (uint32_t)(uintptr_t)regions[i].base;
        if (offset <= regions[i].size && size <= regions[i].size - offset)
            return (char *)regions[i].base + offset;
    }
    assert(!"unmapped DMA address"); return NULL;
}
static uint wire_words, completed_plans, words_this_plan;
static void bus_step(void) {
    if (busy[5]) {
        Channel *out = &bus.ch[5];
        assert(out->ctrl_trig & 1);
        uint32_t value = *(uint32_t *)resolve((uint32_t)out->read_addr, 4);
        if (out->write_addr == (uint32_t)(uintptr_t)&pio.txf[2]) {
            assert(((out->ctrl_trig >> 16) & 63) == 10);
            ++wire_words; ++words_this_plan;
        } else if (out->write_addr == (uint32_t)(uintptr_t)&bus.ch[6].al3_read_addr_trig) {
            assert(((out->ctrl_trig >> 16) & 63) == DREQ_FORCE);
            assert(words_this_plan == phys.flight_physical_byte_count * 4);
            words_this_plan = 0; ++completed_plans;
            assert(!busy[6]);
            bus.ch[6].read_addr = (uintptr_t)resolve(value, 16);
            busy[6] = (bus.ch[6].ctrl_trig & 1) != 0;
        } else {
            assert(out->write_addr == (uint32_t)(uintptr_t)&phys.flight_overlay_selected_generation);
            assert(((out->ctrl_trig >> 16) & 63) == DREQ_FORCE);
            *(uint32_t *)resolve(out->write_addr, 4) = value;
        }
        if (out->ctrl_trig & (1u << 4)) out->read_addr += 4;
        if (--out->transfer_count == 0) {
            busy[5] = false;
            uint chain = (out->ctrl_trig >> 8) & 15;
            if (chain != 5) {
                assert(chain == 6 && !busy[6]);
                busy[6] = (bus.ch[6].ctrl_trig & 1) != 0;
            }
        }
    } else if (busy[6]) {
        const tdma_flight_overlay_dma_run_t *run =
            resolve((uint32_t)bus.ch[6].read_addr, 16);
        bus.ch[5].ctrl_trig = run->control;
        bus.ch[5].write_addr = run->write_address;
        bus.ch[5].transfer_count = run->transfer_count;
        bus.ch[5].read_addr = (uintptr_t)resolve(run->read_address, 4);
        bus.ch[6].read_addr += 16;
        busy[6] = false; busy[5] = (run->control & 1) != 0;
    } else assert(!"recurrence stopped");
}
static void until_selected(void) {
    unsigned bound = 2 * 307 * 4 + 64;
    while (phys.flight_overlay_selected_generation != phys.flight_overlay_published_generation && bound--)
        bus_step();
    assert(bound != 0);
    assert(tdma_pio_spi_phys_process_overlay_ready(&phys));
}
static void stop_race(bool hang, bool loader_write, bool restart_write) {
    bus.ch[5].ctrl_trig = bus.ch[6].ctrl_trig = 1;
    busy[5] = busy[6] = true;
    late_trigger = loader_write; late_restart = restart_write;
    hang_abort = hang; enabled = true; phys.armed = true;
    unsigned started = now;
    if (hang) {
        assert(!tdma_pio_spi_phys_stop_command_dma(&phys));
        assert(now - started <= TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US + 1);
        assert(phys.flight_overlay_dma_active && !phys.armed && stopped && !enabled);
        hang_abort = false;
    }
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(!phys.flight_overlay_dma_active && !busy[5] && !busy[6]);
    assert(!(bus.ch[5].ctrl_trig & 1) && !(bus.ch[6].ctrl_trig & 1));
}
int main(void) {
    phys.flight_physical_byte_count = 307;
    phys.flight_overlay_alignment_samples = 2;
    phys.armed = true;
    tdma_flight_overlay_plan_t *plan = &s_tdma_pio_spi_flight_overlay_plan[1];
    assert(tdma_flight_overlay_build_pass_plan(307, 20, plan));
    assert(!tdma_pio_spi_phys_start_overlay_script(&phys, 1));
    assert(start_calls == 0);
    phys.flight_resource_claimed = true;
    uint8_t before[292] = {0}, after[292] = {0};
    memset(after + 96, 0x5a, 32); after[14] = 1;
    tdma_flight_overlay_config_t config = {307, 4, 0, 5, 2, 1u << 14, 20};
    assert(tdma_flight_overlay_build_plan(before, after, 292, NULL, 0, &config, plan));
    tdma_flight_overlay_plan_t generic = *plan;
    plan->run[0].transfer_count++;
    assert(!tdma_pio_spi_phys_start_overlay_script(&phys, 1) && start_calls == 0);
    *plan = generic;
    assert(tdma_pio_spi_phys_start_overlay_script(&phys, 1));
    assert(start_calls == 1 && phys.flight_overlay_pending);
    assert(!tdma_pio_spi_phys_process_overlay_ready(&phys));
    assert(!tdma_flight_overlay_plan_valid(plan, 20)); /* no rebinding */
    for (uint i = 0; i < generic.run_count; ++i) {
        const tdma_flight_overlay_dma_run_t *run = &plan->run[i + 1];
        const tdma_flight_overlay_dma_run_t *old = &generic.run[i];
        assert(run->write_address == (uint32_t)(uintptr_t)&pio.txf[2]);
        assert(run->read_address == (uint32_t)(uintptr_t)(old->control ?
            &plan->token[old->read_address] : &s_tdma_pio_spi_flight_live_word));
        assert(((run->control >> 4) & 1) == old->control);
        assert(((run->control >> 8) & 15) == 6);
    }
    until_selected();
    phys.flight_overlay_alignment_samples = 1;
    assert(!tdma_pio_spi_phys_process_overlay_ready(&phys));
    phys.flight_overlay_alignment_samples = 2;
    assert(tdma_pio_spi_phys_process_overlay_ready(&phys));
    assert(phys.flight_overlay_active_buffer == 1);
    assert(!tdma_pio_spi_phys_start_overlay_script(&phys, 1));
    /* CPU absence and PIO backpressure do not require another configure.
     * Selection precedes the first data word and is NOT wire completion. */
    assert(wire_words == 0 && phys.snapshot.overlay_selected_generation == 1);
    for (unsigned cycle = 0; cycle < 200; ++cycle) {
        while (completed_plans <= cycle) bus_step();
    }
    assert(start_calls == 1 && wire_words == 200 * 307 * 4);
    /* Inactive pool publication during any descriptor/word phase. */
    for (unsigned phase = 0; phase < 48; ++phase) {
        for (unsigned n = 0; n < phase; ++n) bus_step();
        uint free_pool = phys.flight_overlay_active_buffer ^ 1;
        assert(tdma_flight_overlay_build_pass_plan(307, 20,
            &s_tdma_pio_spi_flight_overlay_plan[free_pool]));
        assert(tdma_pio_spi_phys_start_overlay_script(&phys, free_pool));
        tdma_flight_overlay_plan_t frozen = s_tdma_pio_spi_flight_overlay_plan[free_pool];
        assert(!tdma_pio_spi_phys_start_overlay_script(&phys, free_pool ^ 1));
        assert(memcmp(&frozen, &s_tdma_pio_spi_flight_overlay_plan[free_pool], sizeof(frozen)) == 0);
        /* A transient all-idle register observation never retires a pool. */
        bool b5 = busy[5], b6 = busy[6]; busy[5] = busy[6] = false;
        assert(tdma_pio_spi_phys_overlay_dma_busy(&phys));
        busy[5] = b5; busy[6] = b6;
        until_selected();
        assert(phys.flight_overlay_active_buffer == free_pool && start_calls == 1);
    }
    /* Counter wrap skips zero. A single pending successor ensures that the
     * previous selected value cannot equal the next published value. */
    phys.flight_overlay_published_generation = UINT32_MAX;
    phys.flight_overlay_selected_generation = UINT32_MAX;
    uint free_pool = phys.flight_overlay_active_buffer ^ 1;
    assert(tdma_flight_overlay_build_pass_plan(307, 20,
        &s_tdma_pio_spi_flight_overlay_plan[free_pool]));
    assert(tdma_pio_spi_phys_start_overlay_script(&phys, free_pool));
    assert(phys.flight_overlay_published_generation == 1 && phys.flight_overlay_pending);
    until_selected();
    stop_race(false, true, true);
    assert(stop_clear_output_count == 2);
    stop_race(true, true, true);
    stop_race(false, false, true);
    /* Bootstrap preparation can be cancelled before a command DMA exists.
     * Its old capture/output children still require the common deadline. */
    s_tdma_pio_spi_command_dma_channel = -1;
    s_tdma_pio_spi_executor_dma_channel = -1;
    busy[4] = busy[5] = true;
    bus.ch[4].ctrl_trig = bus.ch[5].ctrl_trig = 1;
    hang_abort = true;
    unsigned child_stop_started = now;
    assert(!tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(now - child_stop_started <= TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US + 1);
    assert(busy[4] && busy[5] && phys.flight_overlay_dma_active);
    hang_abort = false;
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(!busy[4] && !busy[5] && !phys.flight_overlay_dma_active);
    puts("DMA: 200 autonomous plans, 48 publication phases, pool retirement, wrap, late triggers, bounded STOP/retry passed");
}
'''
    unit = tmp_path / "command_dma.c"
    unit.write_text(fixture + routines + assertions, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = tmp_path / "command_dma.exe"
    subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "components/tdma/inc"), str(unit),
                    str(ROOT / "components/tdma/src/tdma_flight_overlay.c"),
                    str(ROOT / "components/tdma/src/tdma_transport_frame.c"), "-o", str(exe)],
                   check=True, capture_output=True)
    subprocess.run([str(exe)], check=True, capture_output=True)


def test_persona_release_retains_all_resources_until_capture_abort_retires(tmp_path):
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c").read_text(encoding="utf-8")
    routines = "\n".join(kind + " " + name + signature + " {" +
        c_definition_body(source, name) + "}\n" for kind, name, signature in [
            ("static bool", "tdma_pio_spi_programs_dma_quiesced", "(const tdma_pio_spi_program_manager_t *manager)"),
            ("void", "tdma_pio_spi_programs_release_resources", "(tdma_pio_spi_program_manager_t *manager, tdma_pio_spi_phys_t *phys, tdma_pio_spi_program_persona_t persona)")])
    fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef unsigned uint;
typedef uint tdma_pio_spi_program_persona_t;
enum { TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY = 6,
       TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK = 3,
       TDMA_STATE_MACHINE_ORIGIN_ADDITIONAL_RESOURCE_MASK = 12,
       TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK = 1 };
static const char *TDMA_FLIGHT_RESOURCE_OWNER = "TDMA_FLIGHT_PIO";
static const char *TDMA_MAINTENANCE_RESOURCE_OWNER = "TDMA_MAINTENANCE_PIO";
typedef struct {
    int *command_dma_channel, *tx_dma_channel, *rx_dma_channel;
    bool *maintenance_resources_claimed;
    int *executor_dma_channel;
} tdma_pio_spi_program_manager_t;
typedef struct {
    bool flight_overlay_dma_active, flight_resource_claimed, flight_origin_resource_claimed;
    struct { uint last_error; } snapshot;
} tdma_pio_spi_phys_t;
static struct { uint32_t abort, sniff_ctrl; } bus;
#define dma_hw (&bus)
static bool busy[16];
static uint dma_releases, sm_releases, arbiter_releases;
static bool dma_channel_is_busy(uint ch) { return busy[ch]; }
static void dma_channel_unclaim(uint ch) { assert(ch == 6 || ch == 8); ++dma_releases; }
static bool tdma_pio_spi_programs_is_flight_persona(uint p) { return p == 1; }
static void tdma_pio_spi_programs_release_flight_sms(tdma_pio_spi_program_manager_t *m) { (void)m; ++sm_releases; }
static void tdma_pio_spi_programs_release_maintenance_sms(tdma_pio_spi_program_manager_t *m) { (void)m; ++sm_releases; }
static void resource_arbiter_release_owned(uint mask, const char *owner) {
    assert(mask == TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK ||
           mask == TDMA_STATE_MACHINE_ORIGIN_ADDITIONAL_RESOURCE_MASK);
    assert(owner == TDMA_FLIGHT_RESOURCE_OWNER); ++arbiter_releases;
}
'''
    assertions = r'''
int main(void) {
    int loader = 6, tx = 5, rx = 4; bool maintenance = false;
    int executor = -1;
    tdma_pio_spi_program_manager_t manager = {&loader, &tx, &rx, &maintenance, &executor};
    tdma_pio_spi_phys_t phys = {.flight_resource_claimed = true};
    for (uint ch = 4; ch <= 6; ++ch) {
        for (uint phase = 0; phase < 2; ++phase) {
            busy[ch] = phase == 0; bus.abort = phase == 1 ? 1u << ch : 0;
            tdma_pio_spi_programs_release_resources(&manager, &phys, 1);
            assert(phys.flight_resource_claimed && loader == 6);
            assert(phys.snapshot.last_error == TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY);
            assert(!dma_releases && !sm_releases && !arbiter_releases);
            busy[ch] = false; bus.abort = 0;
        }
    }
    /* BUSY may briefly be zero in a recurring descriptor gap. Only a
     * successful full STOP clears the physical owner's lifetime latch. */
    phys.flight_overlay_dma_active = true;
    tdma_pio_spi_programs_release_resources(&manager, &phys, 1);
    assert(phys.flight_resource_claimed && !dma_releases && !sm_releases && !arbiter_releases);
    phys.flight_overlay_dma_active = false;
    /* Origin/raw personas may have capture work even without a loader. */
    loader = -1; busy[4] = true;
    tdma_pio_spi_programs_release_resources(&manager, &phys, 1);
    assert(phys.flight_resource_claimed && !sm_releases && !arbiter_releases);
    busy[4] = false; loader = 6;
    busy[7] = true; bus.abort = 1u << 7;
    tdma_pio_spi_programs_release_resources(&manager, &phys, 1);
    assert(!phys.flight_resource_claimed && loader == -1);
    assert(dma_releases == 1 && sm_releases == 1 && arbiter_releases == 1);
    assert(busy[7] && bus.abort == (1u << 7));
    /* The actual origin resource tree retains its global sniffer even if
     * only executor ABORT (or only BUSY) remains after loader quiescence. */
    loader = 6; executor = 8;
    phys.flight_resource_claimed = phys.flight_origin_resource_claimed = true;
    bus.sniff_ctrl = 0x12345;
    dma_releases = sm_releases = arbiter_releases = 0;
    for (uint phase = 0; phase < 2; ++phase) {
        busy[8] = phase == 0;
        bus.abort = (1u << 7) | (phase == 1 ? 1u << 8 : 0);
        tdma_pio_spi_programs_release_resources(&manager, &phys, 1);
        assert(executor == 8 && loader == 6 && phys.flight_origin_resource_claimed);
        assert(bus.sniff_ctrl == 0x12345 && !dma_releases && !sm_releases && !arbiter_releases);
    }
    busy[8] = false; bus.abort = 1u << 7;
    tdma_pio_spi_programs_release_resources(&manager, &phys, 1);
    assert(executor == -1 && loader == -1 && !phys.flight_origin_resource_claimed);
    assert(!bus.sniff_ctrl && dma_releases == 2 && sm_releases == 1 && arbiter_releases == 2);
    assert(busy[7] && bus.abort == (1u << 7));
}
'''
    unit = tmp_path / "release_resources.c"
    unit.write_text(fixture + routines + assertions, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = tmp_path / "release_resources.exe"
    build = subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(unit), "-o", str(exe)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def test_observation_copy_realign_does_not_move_published_wire_slots(tmp_path):
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
enum { TDMA_PIO_SPI_RX_RING_WORDS = 1024, TDMA_PIO_SPI_RX_DMA_WORD_MAX = 64,
       TDMA_RX_OBSERVATION_SCAN_WORDS = 320,
       TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES = 2,
       TDMA_PIO_SPI_PACKET_HEADER_SIZE = 4, TDMA_TRANSPORT_FRAME_HEADER_SIZE = 32,
       TDMA_PIO_SPI_PACKET_MAGIC0 = 0xa5, TDMA_PIO_SPI_PACKET_MAGIC1 = 0x5a };
enum { TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN = 16 };
static unsigned s_tdma_pio_spi_program_persona;
typedef struct {
    bool rx_capture_active, process_image_enabled, flight_overlay_alignment_locked;
    bool flight_origin_workspace_owned;
    uint32_t flight_physical_byte_count, flight_alignment_byte_shift, flight_alignment_bit_shift;
    uint32_t flight_overlay_alignment_samples;
    uint64_t flight_overlay_alignment_candidate;
    struct {
        uint32_t rx_dma_produced_words, rx_scan_produced_words, rx_dma_write_index, rx_dma_channel;
        uint32_t rx_ring_overrun_count, rx_magic_at_zero, rx_magic_at_shift, rx_magic_fail_count;
        uint32_t last_bad_header0, last_bad_header1, last_bad_header2, last_bad_header3, last_bad_words;
        uint32_t rx_observation_drop_count, rx_scan_yield_count;
    } snapshot;
} tdma_pio_spi_phys_t;
static uint64_t s_tdma_pio_spi_rx_scan_produced, produced, packet_start;
static struct { uint64_t observation_epoch; } s_tdma_pio_spi_rx_sequence;
static unsigned alignment;
static void __dmb(void) {}
static int s_tdma_pio_spi_rx_dma_channel = 4;
static uint32_t s_tdma_pio_spi_rx_frame[64];
static uint64_t tdma_pio_spi_phys_rx_produced_words(tdma_pio_spi_phys_t *p) { (void)p; return produced; }
static uint32_t tdma_pio_spi_phys_rx_write_index(void) { return produced % 128; }
static uint32_t tdma_pio_spi_phys_rx_ring_word(uint64_t p) { (void)p; return 0; }
/* Model a valid decoded packet found in an observation stream whose words
 * have been dropped. The real scanner must recover it without retargeting
 * a previously published plan. Bit extraction itself has separate PIO tests. */
static uint8_t tdma_pio_spi_phys_rx_ring_aligned_byte(uint64_t p, uint32_t shift) {
    if (shift != alignment || p < packet_start || p >= packet_start + 36) return 0;
    const uint8_t header[] = {0xa5, 0x5a, 32, 0};
    return p < packet_start + 4 ? header[p - packet_start] : 0x33;
}
static bool tdma_pio_spi_phys_transport_header_matches(uint64_t p, uint32_t s, uint16_t n) {
    return p == packet_start + 4 && s == alignment && n == 32;
}
'''
    routine = "static bool tdma_pio_spi_phys_capture_words(tdma_pio_spi_phys_t *phys, size_t max_words, size_t *received_words) {" + c_definition_body(source, "tdma_pio_spi_phys_capture_words") + "}\n"
    assertions = r'''
int main(void) {
    tdma_pio_spi_phys_t phys = {.rx_capture_active = true, .process_image_enabled = true,
                              .flight_physical_byte_count = 307};
    size_t received;
    packet_start = 5; produced = 42; alignment = 1;
    assert(tdma_pio_spi_phys_capture_words(&phys, 64, &received) && received == 36);
    assert(phys.flight_overlay_alignment_samples == 1);
    assert(phys.flight_alignment_bit_shift == 1);
    /* HIL negative: first frame differed by one bit. Do not freeze it. */
    packet_start += 307; produced += 307; alignment = 0;
    assert(tdma_pio_spi_phys_capture_words(&phys, 64, &received));
    assert(phys.flight_overlay_alignment_samples == 1);
    packet_start += 307; produced += 307;
    assert(tdma_pio_spi_phys_capture_words(&phys, 64, &received));
    assert(phys.flight_overlay_alignment_samples == 2);
    assert(phys.flight_alignment_byte_shift == 5 && phys.flight_alignment_bit_shift == 0);
    phys.flight_overlay_alignment_locked = true;
    packet_start = 2058; produced = 2095; alignment = 3;
    assert(!tdma_pio_spi_phys_capture_words(&phys, 64, &received) && received == 0);
    assert(phys.snapshot.rx_scan_yield_count == 1);
    assert(tdma_pio_spi_phys_capture_words(&phys, 64, &received) && received == 36);
    assert(phys.snapshot.rx_ring_overrun_count == 1 && phys.snapshot.rx_magic_at_shift == 2);
    assert(phys.flight_alignment_byte_shift == 5 && phys.flight_alignment_bit_shift == 0);
    assert(s_tdma_pio_spi_rx_scan_produced == 2094);
    /* A quiesced ARM epoch permits a new initial alignment. */
    phys.flight_overlay_alignment_locked = false; s_tdma_pio_spi_rx_scan_produced = 0;
    phys.flight_overlay_alignment_samples = 0;
    packet_start = 11; produced = 48; alignment = 2;
    assert(tdma_pio_spi_phys_capture_words(&phys, 64, &received));
    assert(phys.flight_alignment_byte_shift == 11 && phys.flight_alignment_bit_shift == 2);
    /* Compact origin banks overlay the legacy ring storage. Neither an
     * active graph nor a stopped origin persona may enter the word scanner. */
    const uint64_t cursor = s_tdma_pio_spi_rx_scan_produced;
    phys.flight_origin_workspace_owned = true;
    assert(!tdma_pio_spi_phys_capture_words(&phys, 64, &received) && received == 0);
    phys.flight_origin_workspace_owned = false;
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN;
    assert(!tdma_pio_spi_phys_capture_words(&phys, 64, &received) && received == 0);
    assert(s_tdma_pio_spi_rx_scan_produced == cursor);
}
'''
    unit = tmp_path / "capture_alignment.c"
    unit.write_text(fixture + routine + assertions, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = tmp_path / "capture_alignment.exe"
    build = subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(unit), "-o", str(exe)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
