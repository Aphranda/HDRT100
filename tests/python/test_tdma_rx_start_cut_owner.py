"""Run the real bounded cut with SDK registers as the test boundary.

Counter arithmetic and publication/retirement execute production code. These
cases do not prove physical edge identity or the absence of in-flight writes.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_real_cut_counter_lifecycle_and_copy(tmp_path):
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys_event.inc").read_text(encoding="utf-8")
    block = source.split("/* RX_START_CUT_STORAGE_BEGIN:", 1)[1].split("/* RX_START_CUT_STORAGE_END */", 1)[0]
    block = block.split("*/", 1)[1]
    sdk = Path.home() / ".pico-sdk/sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs"
    defines = []
    for filename, pattern in (("dma.h", r"DMA_CH0_(?:CTRL_TRIG|TRANS_COUNT)_\w+"),
                              ("pio.h", r"PIO_FDEBUG_RXSTALL_LSB")):
        raw = (sdk / filename).read_text(encoding="utf-8")
        defines += re.findall(r"(?m)^#define (?:"+pattern+r")\s+[^\n]+", raw)
    assert len(defines) > 10
    unit = tmp_path / "cut_owner.c"
    unit.write_text(PREFIX + "\n" + "\n".join(defines) + FIXTURE + block + CASES, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = tmp_path / "cut_owner.exe"
    cmd = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
           "-I"+str(ROOT / "components/tdma/inc"), str(unit),
           str(ROOT / "components/tdma/src/tdma_rx_sequence.c"), "-o", str(exe)]
    for label, command in (("compile", cmd), ("run", [str(exe)])):
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (tmp_path / f"{label}.log").write_text(result.stdout+result.stderr, encoding="utf-8")
        (tmp_path / f"{label}.json").write_text(json.dumps(dict(command=command, returncode=result.returncode)), encoding="utf-8")
        assert result.returncode == 0, result.stdout+result.stderr
    assert "6 production cut groups passed" in result.stdout


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "tdma_rx_start_cut.h"
#include "tdma_rx_sequence.h"
#include "tdma_event_observer.h"
#define _u(value) value##u
typedef unsigned uint;
'''

FIXTURE = r'''
enum { NUM_DMA_CHANNELS=16, clk_sys=0, TDMA_PIO_SPI_ROLE_SLAVE=2,
       TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER=13 };
typedef struct { volatile uint32_t fdebug, ctrl, pc, level; } bank_t;
typedef bank_t *PIO;
typedef struct {
    uint32_t role, flight_physical_byte_count, flight_alignment_byte_shift, flight_alignment_bit_shift;
    uint32_t flight_data_phase_delay_cycles, flight_marker_phase_delay_cycles, rx_csn_pin, tx_csn_pin;
    bool flight_overlay_alignment_locked;
} tdma_pio_spi_phys_t;
static tdma_pio_spi_phys_t phys;
static bank_t rx_bank;
static struct { struct { volatile uint32_t transfer_count, ctrl_trig; } ch[16]; } dma_bank;
#define dma_hw (&dma_bank)
static int s_tdma_pio_spi_rx_dma_channel=4;
static tdma_rx_dma_counter_t s_tdma_pio_spi_rx_sequence;
static uint64_t s_tdma_pio_spi_rx_arm_epoch=9;
static bool s_tdma_pio_spi_rx_arm_valid=true;
static uint32_t s_tdma_event_epoch=8, s_tdma_event_hz=125000000;
static unsigned s_tdma_pio_spi_program_persona=13;
static tdma_event_observer_t s_tdma_event_observer;
static uint64_t ticks=200, us=100, us_delay;
static uint32_t hz=125000000, irq_mask;
static unsigned irq_saves, irq_restores, bank_reads, reads, acquire_hooks;
static unsigned injection;
static bool reading;
static uint64_t vdc_timestamp_clock_read_ticks64(void) { return ticks++; }
static uint64_t time_us_64(void) { return us + (reads++ ? us_delay : 0u); }
static uint32_t save_and_disable_interrupts(void) {
    uint32_t previous=irq_mask; irq_mask=1; ++irq_saves; return previous;
}
static void restore_interrupts(uint32_t previous) { irq_mask=previous; ++irq_restores; }
static uint32_t clock_get_hz(unsigned clock) { (void)clock; return hz; }
static PIO tdma_pio_spi_phys_capture_pio(const tdma_pio_spi_phys_t *p) {
    assert(p->role == TDMA_PIO_SPI_ROLE_SLAVE); ++bank_reads; return &rx_bank;
}
static uint tdma_pio_spi_phys_capture_sm(const tdma_pio_spi_phys_t *p) { (void)p; return 2; }
static uint32_t pio_sm_get_rx_fifo_level(PIO pio, uint sm) { assert(sm==2); return pio->level; }
static uint32_t pio_sm_get_pc(PIO pio, uint sm) { assert(sm==2); return pio->pc; }
static void fence_hook(int order);
#define __atomic_thread_fence(order) fence_hook(order)
'''

CASES = r'''
#undef __atomic_thread_fence
static void fence_hook(int order) {
    __atomic_thread_fence(order);
    if (order != __ATOMIC_ACQUIRE || !reading) return;
    ++acquire_hooks;
    if (injection == 1 && acquire_hooks == 1) tdma_rx_start_cut_arm_begin();
    if (injection == 2) {
        ++s_tdma_rx_start_cut.produced_after;
        tdma_rx_start_cut_publish();
    }
}
static uint32_t mode(void) {
    return DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB;
}
static bool get(tdma_rx_start_cut_t *out) {
    reads=0; acquire_hooks=0; reading=true;
    unsigned saves=irq_saves, restores=irq_restores;
    uint32_t mask=irq_mask;
    bool result=tdma_pio_spi_phys_get_rx_start_cut(out);
    reading=false;
    assert(irq_mask==mask && irq_saves==saves+1 && irq_restores==restores+1);
    return result;
}
static void empty(const tdma_rx_start_cut_t *out) {
    tdma_rx_start_cut_t zero={0}; assert(memcmp(out,&zero,sizeof(zero))==0);
}
static void reset(void) {
    phys=(tdma_pio_spi_phys_t){.role=2,.flight_physical_byte_count=288,
        .flight_alignment_byte_shift=3,.flight_alignment_bit_shift=1,
        .flight_data_phase_delay_cycles=9,.flight_marker_phase_delay_cycles=2,
        .rx_csn_pin=3,.tx_csn_pin=4,.flight_overlay_alignment_locked=true};
    rx_bank=(bank_t){.ctrl=4,.pc=7};
    s_tdma_pio_spi_rx_dma_channel=4; s_tdma_pio_spi_rx_arm_epoch=9;
    s_tdma_pio_spi_rx_arm_valid=true; s_tdma_pio_spi_program_persona=13;
    s_tdma_event_observer=(tdma_event_observer_t){.state=TDMA_EVENT_ACTIVE,.epoch=9,.joined=123};
    ticks=200; hz=125000000; us_delay=0; injection=0;
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,288,100));
    s_tdma_pio_spi_rx_sequence.observation_epoch=7;
    s_tdma_pio_spi_rx_sequence.position=50;
    s_tdma_pio_spi_rx_sequence.produced_words=UINT64_C(0x100000400);
    dma_bank.ch[4].transfer_count=mode()|(s_tdma_pio_spi_rx_sequence.reload_words-50);
    dma_bank.ch[4].ctrl_trig=DMA_CH0_CTRL_TRIG_EN_BITS|DMA_CH0_CTRL_TRIG_BUSY_BITS;
    tdma_rx_start_cut_arm_begin();
}
static void record(void) {
    tdma_rx_dma_counter_t original=s_tdma_pio_spi_rx_sequence, local=original;
    tdma_rx_start_cut_before(&phys);
    tdma_rx_start_cut_after(&phys,&local,UINT32_MAX,UINT32_MAX);
    assert(memcmp(&s_tdma_pio_spi_rx_sequence,&original,sizeof(original))==0);
}
static void raw_and_unresolved(void) {
    reset(); record();
    assert(s_tdma_rx_start_cut.produced_before==UINT64_C(0x100000400));
    assert(s_tdma_rx_start_cut.produced_after==s_tdma_rx_start_cut.produced_before);
    assert(s_tdma_rx_start_cut.flags==(TDMA_RX_START_CUT_DIAGNOSTIC|TDMA_RX_START_CUT_RECORDED|
        TDMA_RX_START_CUT_UNRESOLVED|TDMA_RX_START_CUT_INFLIGHT_UNKNOWN|
        TDMA_RX_START_CUT_PRESTART_BACKLOG_UNEXCLUDED));
    assert(s_tdma_rx_start_cut.retire_reasons==0);
    assert(s_tdma_rx_start_cut.sample_after_ticks>s_tdma_rx_start_cut.sample_before_ticks);
    assert(s_tdma_rx_start_cut.capture_pc_before==7 && s_tdma_rx_start_cut.capture_fifo_before==0);
    tdma_rx_start_cut_t out; assert(!get(&out)); empty(&out);
    /* FIFO empty and equal counts still leave both pending writes and a
     * packet entirely before observer enable unexcluded. */
    tdma_rx_start_cut_disarmed(); assert(get(&out));
    assert(out.flags & TDMA_RX_START_CUT_UNRESOLVED);
    tdma_rx_start_cut_arm_begin(); assert(!get(&out)); empty(&out);
}
static void wraps_and_lost_epoch(void) {
    reset();
    tdma_rx_dma_counter_t local=s_tdma_pio_spi_rx_sequence;
    uint32_t reload=local.reload_words;
    local.position=reload-2;
    tdma_rx_start_cut_t cut={.physical_frame_words=288,.dma_count_before=mode()|2,.dma_count_after=mode()|(reload-3),
        .sample_before_ticks=200,.sample_after_ticks=210};
    assert(tdma_rx_start_cut_lift(&cut,&local)==0);
    assert(cut.produced_after-cut.produced_before==5 && (cut.flags & TDMA_RX_START_CUT_COUNT_WRAP));
    local=s_tdma_pio_spi_rx_sequence;
    cut=(tdma_rx_start_cut_t){.physical_frame_words=288,.dma_count_before=dma_bank.ch[4].transfer_count,
        .dma_count_after=dma_bank.ch[4].transfer_count,
        .sample_before_ticks=(uint64_t)reload+200,.sample_after_ticks=(uint64_t)reload+210};
    assert(tdma_rx_start_cut_lift(&cut,&local)==TDMA_RX_START_CUT_OBSERVATION_EPOCH);
    assert(cut.observation_epoch_before==8 && cut.observation_epoch_after==8);
    local=s_tdma_pio_spi_rx_sequence; local.produced_words=UINT64_MAX;
    cut.sample_before_ticks=200; cut.sample_after_ticks=210;
    --cut.dma_count_before;
    assert(tdma_rx_start_cut_lift(&cut,&local)==TDMA_RX_START_CUT_COUNTER);
    cut.dma_count_before=0;
    assert(tdma_rx_start_cut_lift(&cut,&local)==TDMA_RX_START_CUT_DMA_CONFIG);
}
static void faults_leave_wire_observer_and_raw_alone(void) {
    reset(); record();
    tdma_event_observer_t observer=s_tdma_event_observer;
    tdma_rx_start_cut_t raw=s_tdma_rx_start_cut;
    rx_bank.fdebug=1u<<(2+PIO_FDEBUG_RXSTALL_LSB);
    tdma_rx_start_cut_monitor(&phys);
    assert(s_tdma_rx_start_cut.retire_reasons==TDMA_RX_START_CUT_CAPTURE_RXSTALL);
    assert(rx_bank.fdebug==(1u<<(2+PIO_FDEBUG_RXSTALL_LSB)) && rx_bank.ctrl==4);
    assert(memcmp(&observer,&s_tdma_event_observer,sizeof(observer))==0);
    raw.flags=s_tdma_rx_start_cut.flags; raw.retire_reasons=s_tdma_rx_start_cut.retire_reasons;
    assert(memcmp(&raw,&s_tdma_rx_start_cut,sizeof(raw))==0);
    dma_bank.ch[4].ctrl_trig |= DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    tdma_rx_start_cut_monitor(&phys);
    assert(s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_DMA_ERROR);
    ++s_tdma_pio_spi_rx_sequence.observation_epoch;
    tdma_rx_start_cut_monitor(&phys);
    assert(s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_OBSERVATION_EPOCH);
}
static void stop_ack_and_role_change(void) {
    reset(); record();
    tdma_rx_start_cut_retire(TDMA_RX_START_CUT_STOP);
    tdma_rx_start_cut_t out; assert(!get(&out)); empty(&out);
    /* Hardware cleanup before a delayed cancellation ACK cannot manufacture
     * running faults on the next STOP pass. */
    rx_bank.ctrl=0; rx_bank.fdebug=UINT32_MAX; phys.flight_overlay_alignment_locked=false;
    dma_bank.ch[4].ctrl_trig=0;
    tdma_rx_start_cut_monitor(&phys);
    assert(s_tdma_rx_start_cut.retire_reasons==TDMA_RX_START_CUT_STOP);
    tdma_rx_start_cut_disarmed(); assert(get(&out));
    assert(out.retire_reasons==TDMA_RX_START_CUT_STOP);
    reset(); record(); phys.role=1; unsigned reads_before=bank_reads;
    tdma_rx_start_cut_monitor(&phys);
    assert(bank_reads==reads_before && s_tdma_rx_start_cut.retire_reasons==TDMA_RX_START_CUT_GEOMETRY);
}
static void monitor_config_and_clock(void) {
    reset(); record();
    ++phys.flight_alignment_bit_shift; ++hz; ++s_tdma_pio_spi_rx_arm_epoch;
    dma_bank.ch[4].ctrl_trig=0; rx_bank.ctrl=0;
    s_tdma_event_observer.state=TDMA_EVENT_INVALID;
    tdma_rx_start_cut_monitor(&phys);
    uint32_t needed=TDMA_RX_START_CUT_GEOMETRY|TDMA_RX_START_CUT_CLOCK|TDMA_RX_START_CUT_ARM|
        TDMA_RX_START_CUT_DMA_CONFIG|TDMA_RX_START_CUT_CAPTURE_DISABLED|TDMA_RX_START_CUT_OBSERVER;
    assert((s_tdma_rx_start_cut.retire_reasons & needed)==needed);
    reset(); record(); ticks=s_tdma_pio_spi_rx_sequence.reload_words+1000u;
    tdma_rx_start_cut_monitor(&phys);
    assert(s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_OBSERVATION_EPOCH);
}
static void concurrent_copy(void) {
    tdma_rx_start_cut_t out;
    assert(!tdma_pio_spi_phys_get_rx_start_cut(NULL));
    reset(); record(); tdma_rx_start_cut_disarmed();
    injection=1; assert(!get(&out)); empty(&out);
    assert(acquire_hooks==2); /* Detect fresh ARM between payload and guard. */
    reset(); record(); tdma_rx_start_cut_disarmed();
    injection=2; assert(!get(&out)); empty(&out); assert(acquire_hooks==3);
    injection=0; assert(get(&out));
    us_delay=1001; assert(!get(&out)); empty(&out);
    us_delay=0; __atomic_store_n(&s_tdma_rx_start_cut_guard,1,__ATOMIC_RELEASE);
    assert(!get(&out)); empty(&out); assert(acquire_hooks==0);
    __atomic_store_n(&s_tdma_rx_start_cut_guard,UINT32_MAX-1,__ATOMIC_RELEASE);
    tdma_rx_start_cut_publish(); assert(get(&out));
}
int main(void) {
    raw_and_unresolved(); wraps_and_lost_epoch(); faults_leave_wire_observer_and_raw_alone();
    stop_ack_and_role_change(); monitor_config_and_clock(); concurrent_copy();
    puts("6 production cut groups passed"); return 0;
}
'''
