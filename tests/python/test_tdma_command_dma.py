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


def test_descriptor_completion_and_bounded_stop(tmp_path):
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    routines = "\n".join("static bool " + name + signature + " {" +
        c_definition_body(source, name) + "}\n" for name, signature in [
            ("tdma_pio_spi_phys_stop_command_dma", "(tdma_pio_spi_phys_t *phys)"),
            ("tdma_pio_spi_phys_overlay_dma_busy", "(tdma_pio_spi_phys_t *phys)"),
            ("tdma_pio_spi_phys_start_overlay_script", "(tdma_pio_spi_phys_t *phys, uint32_t buffer_index)")])
    fixture = r'''
#include "tdma_flight_overlay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned uint;
enum { DMA_SIZE_32 = 2, DMA_CH0_CTRL_TRIG_EN_BITS = 1,
       TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US = 64, TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY = 6,
       TDMA_PIO_SPI_OVERLAY_ERROR_DMA_START_INVALID = 4, TDMA_PIO_SPI_OVERLAY_ERROR_DMA_BUSY_TIMEOUT = 3,
       TDMA_PIO_SPI_OVERLAY_ERROR_BUILD_FAILED = 2, TDMA_PIO_SPI_OVERLAY_ERROR_NONE = 0 };
typedef struct {
    uint32_t last_error, overlay_last_error, overlay_tx_dma_remaining, overlay_tx_dma_busy;
    uint32_t overlay_tx_fifo_level_at_fail, overlay_prepare_wait_us;
} Snapshot;
typedef struct {
    bool armed, flight_resource_claimed, flight_overlay_dma_active, flight_overlay_pending;
    uint32_t flight_overlay_active_buffer, flight_physical_byte_count, flight_overlay_pending_words;
    Snapshot snapshot;
} tdma_pio_spi_phys_t;
typedef struct { uintptr_t read_addr; uint32_t ctrl_trig, transfer_count, al3_ctrl; } Channel;
static struct { Channel ch[8]; uint32_t abort; } bus;
#define dma_hw (&bus)
static bool busy[8], hang_abort, stopped, enabled, late_trigger;
static unsigned now, stop_clear_output_count, start_calls;
static int s_tdma_pio_spi_command_dma_channel = 6, s_tdma_pio_spi_tx_dma_channel = 5;
static tdma_flight_overlay_plan_t s_tdma_pio_spi_flight_overlay_plan[2];
static uint32_t s_tdma_pio_spi_flight_live_word = TDMA_FLIGHT_OVERLAY_LIVE_WORD;
static struct { uint32_t txf[4]; } pio;
typedef struct { uint32_t ctrl; } dma_channel_config;
typedef struct { unsigned descriptor_words, descriptor_write_ring_log2; } tdma_state_machine_command_dma_contract_t;
static tdma_state_machine_command_dma_contract_t tdma_state_machine_command_dma_contract(void) {
    return (tdma_state_machine_command_dma_contract_t){4, 4};
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
        for (uint ch = 0; ch < 8; ++ch) if (bus.abort & (1u << ch)) busy[ch] = false;
        bus.abort = 0;
    }
    return now;
}
static void __dmb(void) {}
static void tdma_pio_spi_phys_set_line_drivers(bool value) { enabled = value; }
static void tdma_pio_spi_phys_prepare_sm_pair(tdma_pio_spi_phys_t *phys) { (void)phys; stopped = true; }
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
static void channel_config_set_dreq(dma_channel_config *c, uint v) { c->ctrl |= v << 16; }
static void channel_config_set_chain_to(dma_channel_config *c, uint v) { c->ctrl = (c->ctrl & ~(15u << 8)) | (v << 8); }
static void channel_config_set_ring(dma_channel_config *c, bool write, uint size) { assert(write); c->ctrl |= size << 24; }
static uint32_t channel_config_get_ctrl_value(const dma_channel_config *c) { return c->ctrl; }
static void dma_channel_configure(uint ch, const dma_channel_config *c, uint32_t *write, const void *read, uint n, bool trigger) {
    assert(ch == 6 && write == &bus.ch[5].al3_ctrl && n == 4 && trigger);
    assert(((c->ctrl >> 8) & 15) == ch && (c->ctrl >> 24) == 4);
    assert((c->ctrl & 0x30) == 0x30);
    bus.ch[ch].read_addr = (uintptr_t)read; busy[ch] = true; ++start_calls;
}
'''
    assertions = r'''
int main(void) {
    tdma_pio_spi_phys_t phys = {.flight_physical_byte_count = 307};
    tdma_flight_overlay_plan_t *plan = &s_tdma_pio_spi_flight_overlay_plan[1];
    assert(tdma_flight_overlay_build_pass_plan(307, 20, plan));
    assert(!tdma_pio_spi_phys_start_overlay_script(&phys, 1));
    assert(start_calls == 0); /* No register writes before claim. */
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
    assert(start_calls == 1 && phys.flight_overlay_dma_active);
    uint words = 0;
    for (uint i = 0; i < plan->run_count; ++i) {
        const tdma_flight_overlay_dma_run_t *run = &plan->run[i];
        const tdma_flight_overlay_dma_run_t *old = &generic.run[i];
        assert(run->write_address == (uint32_t)(uintptr_t)&pio.txf[2]);
        assert(run->read_address == (uint32_t)(uintptr_t)(old->control ? &plan->token[old->read_address] : &s_tdma_pio_spi_flight_live_word));
        assert(((run->control >> 4) & 1) == old->control && (run->control & (1u << 5)) == 0);
        assert(((run->control >> 8) & 15) == (i + 1 == plan->run_count ? 5 : 6));
        words += run->transfer_count;
        /* Output idle during every AL3 gap must retain the active pool. */
        busy[5] = busy[6] = false;
        bus.ch[6].read_addr = (uintptr_t)&plan->run[i];
        assert(tdma_pio_spi_phys_overlay_dma_busy(&phys));
        bus.ch[6].read_addr = (uintptr_t)&plan->run[i + 1];
        busy[6] = true;
        assert(tdma_pio_spi_phys_overlay_dma_busy(&phys));
        busy[6] = false; busy[5] = true;
        assert(tdma_pio_spi_phys_overlay_dma_busy(&phys));
    }
    assert(words == 307 * 4);
    busy[5] = false;
    assert(!tdma_pio_spi_phys_overlay_dma_busy(&phys));
    /* Loader's final AL3 write races the first output disable. */
    phys.flight_overlay_dma_active = phys.armed = true;
    bus.ch[5].ctrl_trig = bus.ch[6].ctrl_trig = 1;
    busy[5] = busy[6] = late_trigger = true;
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(!phys.flight_overlay_dma_active && !busy[5] && !busy[6]);
    assert(stop_clear_output_count == 2 && !(bus.ch[5].ctrl_trig & 1));
    /* A stuck abort retains the pool and shuts down the physical outputs. */
    hang_abort = busy[6] = true; enabled = true;
    assert(!tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(now <= 2 * TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US);
    assert(phys.flight_overlay_dma_active && !phys.armed && stopped && !enabled);
    hang_abort = false;
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(!phys.flight_overlay_dma_active);
    puts("DMA: descriptor gaps, final self-chain, claim/validation, late trigger, timeout and retry passed");
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
