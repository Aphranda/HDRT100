"""Execute the device observer DMA helpers against a deterministic MMIO model.

The implementation is extracted verbatim from the production device branch.
The model supplies completed writes, bus faults and abort completion; it does
not implement producer accounting, ring admission or the harvest algorithm.
"""
import json
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]
EVENT = ROOT / "components/tdma/src/tdma_pio_spi_phys_event.inc"
PHYSICAL = ROOT / "components/tdma/src/tdma_pio_spi_phys.c"


def production() -> str:
    source = EVENT.read_text(encoding="utf-8")
    start = source.index("#define TDMA_EVENT_DMA_RING_WORDS")
    # This device block has no nested preprocessor alternatives.
    end = source.index("\n#else", start)
    return source[start:end]


def sdk_constants() -> str:
    sdk = Path(os.environ.get("PICO_SDK_PATH", str(Path.home() / ".pico-sdk/sdk/2.2.0")))
    registers = (sdk / "src/rp2350/hardware_regs/include/hardware/regs/dma.h").read_text(
        encoding="utf-8")
    return "\n".join(re.findall(
        r"(?m)^#define DMA_CH0_(?:CTRL_TRIG|TRANS_COUNT)[A-Z0-9_]*\s+[^\n]+$", registers))


def stopped_helpers() -> str:
    source = PHYSICAL.read_text(encoding="utf-8")
    return "\n".join(
        f"static {result} {name}({arguments}) {{\n" + c_definition_body(source, name) + "\n}\n"
        for result, name, arguments in (
            ("void", "tdma_pio_spi_phys_disable_dma_mask", "uint32_t mask"),
            ("bool", "tdma_pio_spi_phys_stop_dma_level", "uint32_t mask, uint64_t deadline"),
        )
    )


def timeout_constant() -> str:
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    return re.search(r"(?m)^#define TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US\s+[^\n]+$",
                     header).group(0) + "\n"


@pytest.fixture(scope="module")
def dma_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("event_dma_device")
    unit = directory / "event_dma.c"
    unit.write_text(PREFIX + sdk_constants() + "\n" + timeout_constant() + MMIO +
                    stopped_helpers() + production() + CASES,
                    encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = directory / "event_dma.exe"
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/tdma/inc"), str(unit),
               str(ROOT / "components/tdma/src/tdma_event_observer.c"), "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True)
    (directory / "compile.json").write_text(json.dumps({"command": command,
        "returncode": result.returncode,
        "source_sha256": hashlib.sha256(unit.read_bytes()).hexdigest(),
        "production_sha256": hashlib.sha256(production().encode("utf-8")).hexdigest()},
        indent=2), encoding="utf-8")
    (directory / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return directory, executable


@pytest.mark.parametrize("case", [
    "configuration", "progress_wraps", "ring_63", "ring_64", "ring_128", "ring_192",
    "copy_safe", "copy_overwrite", "copy_fault", "copy_mode", "staging_capacity",
    "fault_endless", "fault_trigger_self", "fault_reserved_mode", "fault_exhausted",
    "fault_regression", "fault_disabled", "fault_idle", "fault_read", "fault_write",
    "near_exhaustion", "claim_0", "claim_1", "claim_2", "stop_retry",
    "empty_pipeline", "strict_join_timeout", "post_fault_retires_epoch",
])
def test_device_dma(dma_executable, case):
    directory, executable = dma_executable
    result = subprocess.run([str(executable), case], capture_output=True, text=True, timeout=10)
    (directory / f"{case}.json").write_text(json.dumps({
        "case": case, "returncode": result.returncode,
        "stdout": result.stdout, "stderr": result.stderr}, indent=2), encoding="utf-8")
    assert result.returncode == 0, f"{case}: {result.stdout}{result.stderr}"
    assert result.stdout.strip() == f"PASS {case}"


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tdma_event_observer.h"
#define _u(x) x##u
#define PICO_ON_DEVICE 1
#define __not_in_flash(name)
#define TEST_STUB __attribute__((unused))
typedef unsigned int uint;
enum { NUM_DMA_CHANNELS = 16u, DMA_SIZE_32 = 2u,
       TDMA_EVENT_RX_SM = 1u, TDMA_EVENT_TX_SM = 2u,
       TDMA_EVENT_SEQUENCE_SM = 3u };
typedef struct { volatile uint32_t rxf[4], ctrl, fdebug; } fake_pio_t;
typedef fake_pio_t *PIO;
typedef struct {
    volatile uintptr_t read_addr, write_addr;
    volatile uint32_t transfer_count, ctrl_trig;
} fake_dma_channel_t;
typedef struct { fake_dma_channel_t ch[NUM_DMA_CHANNELS]; volatile uint32_t abort; } fake_dma_t;
static fake_dma_t dma_bank;
static fake_dma_t *const dma_hw = &dma_bank;
static fake_pio_t pio_bank;
typedef struct { uint32_t ctrl; } dma_channel_config;
typedef struct { uint32_t rx_level_max, tx_level_max, sequence_level_max; } tdma_pio_spi_event_snapshot_t;
static tdma_event_observer_t s_tdma_event_observer;
static bool claimed[NUM_DMA_CHANNELS];
static uint32_t claim_attempts, unclaims, configurations, start_mask;
static int fail_claim_at;
static uint32_t sticky_abort_mask, abort_checks, fence_count;
static uint64_t clock_us;
static void (*fence_hook)(void);
'''


MMIO = r'''
static TEST_STUB uint64_t tdma_pio_spi_phys_now_us(void) { return ++clock_us; }
static TEST_STUB uint64_t time_us_64(void) { return ++clock_us; }
static TEST_STUB void __dmb(void) {
    ++fence_count;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (fence_hook) fence_hook();
}
static TEST_STUB void hw_clear_bits(volatile uint32_t *address, uint32_t bits) { *address &= ~bits; }
static TEST_STUB bool dma_channel_is_busy(uint channel) {
    assert(channel < NUM_DMA_CHANNELS);
    const uint32_t bit = 1u << channel;
    if (dma_hw->abort & bit) {
        ++abort_checks;
        assert(!(dma_hw->ch[channel].ctrl_trig & DMA_CH0_CTRL_TRIG_EN_BITS));
        if (!(sticky_abort_mask & bit)) {
            dma_hw->ch[channel].ctrl_trig &= ~DMA_CH0_CTRL_TRIG_BUSY_BITS;
            dma_hw->abort &= ~bit;
        }
    }
    return (dma_hw->ch[channel].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) != 0u;
}
static TEST_STUB int dma_claim_unused_channel(bool required) {
    assert(!required);
    if ((int)claim_attempts++ == fail_claim_at) return -1;
    for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
        if (!claimed[channel]) { claimed[channel] = true; return (int)channel; }
    }
    return -1;
}
static TEST_STUB void dma_channel_unclaim(uint channel) {
    assert(channel < NUM_DMA_CHANNELS && claimed[channel]);
    assert(!(dma_hw->ch[channel].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS));
    claimed[channel] = false;
    ++unclaims;
}
static TEST_STUB dma_channel_config dma_channel_get_default_config(uint channel) {
    assert(channel < NUM_DMA_CHANNELS);
    return (dma_channel_config){DMA_CH0_CTRL_TRIG_EN_BITS |
        (channel << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB)};
}
static TEST_STUB void channel_config_set_transfer_data_size(dma_channel_config *c, uint size) {
    c->ctrl = (c->ctrl & ~DMA_CH0_CTRL_TRIG_DATA_SIZE_BITS) |
        (size << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);
}
static TEST_STUB void channel_config_set_read_increment(dma_channel_config *c, bool value) {
    if (value) c->ctrl |= DMA_CH0_CTRL_TRIG_INCR_READ_BITS;
    else c->ctrl &= ~DMA_CH0_CTRL_TRIG_INCR_READ_BITS;
}
static TEST_STUB void channel_config_set_write_increment(dma_channel_config *c, bool value) {
    if (value) c->ctrl |= DMA_CH0_CTRL_TRIG_INCR_WRITE_BITS;
    else c->ctrl &= ~DMA_CH0_CTRL_TRIG_INCR_WRITE_BITS;
}
static TEST_STUB void channel_config_set_ring(dma_channel_config *c, bool write, uint bits) {
    c->ctrl = (c->ctrl & ~(DMA_CH0_CTRL_TRIG_RING_SEL_BITS | DMA_CH0_CTRL_TRIG_RING_SIZE_BITS)) |
        (bits << DMA_CH0_CTRL_TRIG_RING_SIZE_LSB) | (write ? DMA_CH0_CTRL_TRIG_RING_SEL_BITS : 0u);
}
static TEST_STUB uint pio_get_dreq(PIO pio, uint sm, bool tx) {
    assert(pio == &pio_bank && sm >= 1u && sm <= 3u && !tx);
    return 16u + sm;
}
static TEST_STUB void channel_config_set_dreq(dma_channel_config *c, uint dreq) {
    c->ctrl = (c->ctrl & ~DMA_CH0_CTRL_TRIG_TREQ_SEL_BITS) |
        (dreq << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB);
}
static TEST_STUB void dma_channel_configure(uint channel, const dma_channel_config *c,
    volatile void *write, const volatile void *read, uint32_t count, bool trigger) {
    assert(channel < NUM_DMA_CHANNELS && claimed[channel] && !trigger);
    assert(count == DMA_CH0_TRANS_COUNT_COUNT_BITS);
    dma_hw->ch[channel] = (fake_dma_channel_t){(uintptr_t)read, (uintptr_t)write, count, c->ctrl};
    ++configurations;
}
static TEST_STUB void dma_start_channel_mask(uint32_t mask) {
    start_mask = mask;
    for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel)
        if (mask & (1u << channel)) dma_hw->ch[channel].ctrl_trig |= DMA_CH0_CTRL_TRIG_BUSY_BITS;
}
'''


CASES = r'''
static uint32_t levels[TDMA_EVENT_STREAMS];
static uint32_t *maxima[TDMA_EVENT_STREAMS] = {&levels[0], &levels[1], &levels[2]};
static uint32_t completed[TDMA_EVENT_STREAMS];

static fake_dma_channel_t *channel_for(uint stream) {
    assert(stream < TDMA_EVENT_STREAMS && s_tdma_event_dma[stream].channel >= 0);
    return &dma_hw->ch[(uint)s_tdma_event_dma[stream].channel];
}

static void setup(void) {
    fail_claim_at = -1;
    /* Other components already own discontiguous channels. */
    claimed[0] = claimed[2] = claimed[7] = true;
    assert(tdma_event_dma_start(&pio_bank));
    assert(s_tdma_event_dma_active);
}

/* External bus model: the finite count changes only AFTER an SRAM write.
 * No production ring accounting or admission logic is replicated here. */
static void complete_word(uint stream, uint32_t word) {
    fake_dma_channel_t *channel = channel_for(stream);
    assert(channel->transfer_count > 1u);
    volatile uint32_t *destination = (volatile uint32_t *)channel->write_addr;
    *destination = word;
    const uintptr_t base = (uintptr_t)s_tdma_event_dma_ring[stream];
    uintptr_t next = channel->write_addr + sizeof(uint32_t);
    if (next == base + sizeof(s_tdma_event_dma_ring[stream])) next = base;
    channel->write_addr = next;
    --channel->transfer_count;
    ++completed[stream];
}

static void complete_many(uint stream, uint count) {
    for (uint i = 0u; i < count; ++i)
        complete_word(stream, (stream + 1u) * 1000000u + completed[stream]);
}

static tdma_event_batch_t harvest(void) {
    tdma_event_batch_t batch = {0};
    tdma_event_dma_harvest(&batch, &s_tdma_event_observer, maxima);
    return batch;
}

static void assert_clean(const tdma_event_batch_t *batch) {
    assert(batch->pre_faults == 0u && batch->post_faults == 0u);
    assert(batch->empty_mask == 0u);
}

static void configuration(void) {
    setup();
    assert(configurations == 3u && claim_attempts == 3u);
    assert(start_mask == ((1u << 1u) | (1u << 3u) | (1u << 4u)));
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
        fake_dma_channel_t *channel = channel_for(stream);
        const uint32_t ctrl = channel->ctrl_trig;
        assert(channel->transfer_count == UINT32_C(0x0fffffff));
        assert((channel->transfer_count & DMA_CH0_TRANS_COUNT_MODE_BITS) == 0u);
        assert(channel->read_addr == (uintptr_t)&pio_bank.rxf[stream + 1u]);
        assert(channel->write_addr == (uintptr_t)s_tdma_event_dma_ring[stream]);
        assert((channel->write_addr & 255u) == 0u);
        assert((ctrl & DMA_CH0_CTRL_TRIG_DATA_SIZE_BITS) ==
               (DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB));
        assert(!(ctrl & DMA_CH0_CTRL_TRIG_INCR_READ_BITS));
        assert(ctrl & DMA_CH0_CTRL_TRIG_INCR_WRITE_BITS);
        assert(ctrl & DMA_CH0_CTRL_TRIG_RING_SEL_BITS);
        assert((ctrl & DMA_CH0_CTRL_TRIG_RING_SIZE_BITS) ==
               (8u << DMA_CH0_CTRL_TRIG_RING_SIZE_LSB));
        assert((ctrl & DMA_CH0_CTRL_TRIG_TREQ_SEL_BITS) ==
               ((17u + stream) << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB));
        assert((ctrl & DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS) ==
               ((uint)s_tdma_event_dma[stream].channel << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB));
    }
    /* An active epoch must never leak channels or reset its read cursor. */
    assert(!tdma_event_dma_start(&pio_bank));
    assert(configurations == 3u && claim_attempts == 3u);
    assert(tdma_event_dma_stop());
    assert(unclaims == 3u && claimed[0] && claimed[2] && claimed[7]);
}

static void progress_wraps(void) {
    setup();
    uint32_t expected[TDMA_EVENT_STREAMS] = {0};
    /* Non-divisor increments make copies straddle every ring boundary. */
    for (uint round = 0u; round < 80u; ++round) {
        for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream)
            complete_many(stream, 5u + stream);
        tdma_event_batch_t batch = harvest();
        assert_clean(&batch);
        for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
            assert(batch.count[stream] == 5u + stream);
            for (uint word = 0u; word < batch.count[stream]; ++word)
                assert(batch.words[stream][word] ==
                       (stream + 1u) * 1000000u + expected[stream]++);
            assert(s_tdma_event_dma[stream].produced == expected[stream]);
            assert(s_tdma_event_dma[stream].consumed == expected[stream]);
        }
    }
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream)
        assert(levels[stream] == 5u + stream);
}

static void ring_lapse(uint count) {
    setup();
    complete_many(0u, count);
    bool overrun = false;
    assert(tdma_event_dma_available(0u, &overrun) == count);
    assert(overrun == (count >= 64u));
    if (count % 64u == 0u)
        assert(channel_for(0u)->write_addr == (uintptr_t)s_tdma_event_dma_ring[0]);
    tdma_event_batch_t batch = harvest();
    assert(levels[0] == count && batch.empty_mask == 0u);
    if (count >= 64u) {
        assert(batch.pre_faults == TDMA_EVENT_FAULT_OVERFLOW);
        assert(batch.count[0] == 0u && s_tdma_event_dma[0].consumed == 0u);
    } else {
        assert_clean(&batch);
        assert(batch.count[0] == TDMA_EVENT_FIFO_WORDS);
        assert(s_tdma_event_dma[0].consumed == TDMA_EVENT_FIFO_WORDS);
    }
}

enum race_kind { RACE_SAFE, RACE_OVERWRITE, RACE_FAULT, RACE_MODE };
static enum race_kind race;
static void copy_hook(void) {
    if (fence_count != 2u) return;
    fence_hook = NULL;
    if (race == RACE_SAFE) complete_many(0u, 3u);
    else if (race == RACE_OVERWRITE) complete_many(0u, 62u);
    else if (race == RACE_FAULT) channel_for(0u)->ctrl_trig |= DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    else channel_for(0u)->transfer_count |= 15u << DMA_CH0_TRANS_COUNT_MODE_LSB;
}

static void copy_race(enum race_kind kind) {
    setup();
    complete_many(0u, 2u);
    race = kind;
    fence_hook = copy_hook;
    tdma_event_batch_t batch = {0};
    /* Sticky evidence from the surrounding adapter must survive the copy. */
    batch.post_faults = TDMA_EVENT_FAULT_SEQUENCE;
    tdma_event_dma_harvest(&batch, &s_tdma_event_observer, maxima);
    assert(fence_hook == NULL && batch.empty_mask == 0u);
    assert(batch.pre_faults == 0u);
    assert(batch.post_faults & TDMA_EVENT_FAULT_SEQUENCE);
    if (kind == RACE_SAFE) {
        assert(batch.post_faults == TDMA_EVENT_FAULT_SEQUENCE);
        assert(batch.count[0] == 2u && s_tdma_event_dma[0].consumed == 2u);
        assert(batch.words[0][0] == 1000000u && batch.words[0][1] == 1000001u);
        batch = harvest();
        assert_clean(&batch);
        assert(batch.count[0] == 3u && batch.words[0][0] == 1000002u);
    } else {
        assert(batch.post_faults & TDMA_EVENT_FAULT_OVERFLOW);
        assert(batch.count[0] == 0u && s_tdma_event_dma[0].consumed == 0u);
    }
}

static void staging_capacity(void) {
    setup();
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) complete_many(stream, 16u);
    for (uint pending = 0u; pending <= TDMA_EVENT_FIFO_WORDS; ++pending) {
        for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
            s_tdma_event_observer.pending_count[stream] = (uint8_t)pending;
            complete_many(stream, TDMA_EVENT_FIFO_WORDS - pending);
        }
        tdma_event_batch_t batch = harvest();
        assert_clean(&batch);
        for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream)
            assert(batch.count[stream] == TDMA_EVENT_FIFO_WORDS - pending);
    }
    s_tdma_event_observer.pending_count[1] = TDMA_EVENT_FIFO_WORDS + 1u;
    const uint32_t consumed = s_tdma_event_dma[1].consumed;
    tdma_event_batch_t batch = harvest();
    assert(batch.pre_faults == TDMA_EVENT_FAULT_OVERFLOW);
    assert(batch.count[1] == 0u && s_tdma_event_dma[1].consumed == consumed);
    assert(levels[0] == 24u && levels[1] == 24u && levels[2] == 24u);
}

static void invalid_state(const char *name) {
    setup();
    complete_many(0u, 5u);
    assert(tdma_event_dma_produced(0u) == 5u);
    fake_dma_channel_t *channel = channel_for(0u);
    if (!strcmp(name, "fault_endless")) channel->transfer_count |= 15u << DMA_CH0_TRANS_COUNT_MODE_LSB;
    else if (!strcmp(name, "fault_trigger_self")) channel->transfer_count |= 1u << DMA_CH0_TRANS_COUNT_MODE_LSB;
    else if (!strcmp(name, "fault_reserved_mode")) channel->transfer_count |= 2u << DMA_CH0_TRANS_COUNT_MODE_LSB;
    else if (!strcmp(name, "fault_exhausted")) channel->transfer_count = 0u;
    else if (!strcmp(name, "fault_regression")) ++channel->transfer_count;
    else if (!strcmp(name, "fault_disabled")) channel->ctrl_trig &= ~DMA_CH0_CTRL_TRIG_EN_BITS;
    else if (!strcmp(name, "fault_idle")) channel->ctrl_trig &= ~DMA_CH0_CTRL_TRIG_BUSY_BITS;
    else if (!strcmp(name, "fault_read")) channel->ctrl_trig |= DMA_CH0_CTRL_TRIG_READ_ERROR_BITS;
    else if (!strcmp(name, "fault_write")) channel->ctrl_trig |= DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    else abort();
    bool overrun = false;
    assert(tdma_event_dma_available(0u, &overrun) == 5u);
    assert(overrun && !s_tdma_event_dma[0].running);
    tdma_event_batch_t batch = harvest();
    assert(batch.pre_faults == TDMA_EVENT_FAULT_OVERFLOW);
    assert(batch.count[0] == 0u && s_tdma_event_dma[0].consumed == 0u);
    /* A later healthy observation does not resurrect the same DMA epoch. */
    channel->transfer_count = TDMA_EVENT_DMA_TRANSFERS - 6u;
    channel->ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS | DMA_CH0_CTRL_TRIG_BUSY_BITS;
    assert(tdma_event_dma_produced(0u) == 5u && !s_tdma_event_dma[0].running);
}

static void near_exhaustion(void) {
    setup();
    channel_for(0u)->transfer_count = 1u;
    assert(tdma_event_dma_produced(0u) == TDMA_EVENT_DMA_TRANSFERS - 1u);
    assert(s_tdma_event_dma[0].running);
    channel_for(0u)->transfer_count = 0u;
    assert(tdma_event_dma_produced(0u) == TDMA_EVENT_DMA_TRANSFERS - 1u);
    assert(!s_tdma_event_dma[0].running && channel_for(0u)->transfer_count == 0u);
}

static void claim_failure(uint attempt) {
    fail_claim_at = (int)attempt;
    claimed[0] = claimed[2] = claimed[7] = true;
    assert(!tdma_event_dma_start(&pio_bank));
    assert(!s_tdma_event_dma_active && configurations == 0u && start_mask == 0u);
    assert(unclaims == attempt && claim_attempts == attempt + 1u);
    for (uint i = 0u; i < NUM_DMA_CHANNELS; ++i)
        assert(claimed[i] == (i == 0u || i == 2u || i == 7u));
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream)
        assert(s_tdma_event_dma[stream].channel == -1 && !s_tdma_event_dma[stream].running);
    fail_claim_at = -1;
    assert(tdma_event_dma_start(&pio_bank));
    assert(tdma_event_dma_stop() && unclaims == attempt + 3u);
}

static void stop_retry(void) {
    setup();
    complete_many(0u, 5u);
    tdma_event_batch_t batch = harvest();
    assert_clean(&batch);
    const tdma_event_dma_stream_t saved = s_tdma_event_dma[0];
    sticky_abort_mask = 1u << (uint)s_tdma_event_dma[1].channel;
    assert(!tdma_event_dma_stop());
    assert(clock_us <= TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US + 1u);
    assert(abort_checks != 0u && unclaims == 0u && s_tdma_event_dma_active);
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
        assert(claimed[(uint)s_tdma_event_dma[stream].channel]);
        assert(!(channel_for(stream)->ctrl_trig & DMA_CH0_CTRL_TRIG_EN_BITS));
    }
    assert(s_tdma_event_dma[0].channel == saved.channel &&
           s_tdma_event_dma[0].produced == saved.produced &&
           s_tdma_event_dma[0].consumed == saved.consumed);
    assert(!tdma_event_dma_start(&pio_bank));
    assert(claim_attempts == 3u);
    sticky_abort_mask = 0u;
    assert(tdma_event_dma_stop());
    assert(unclaims == 3u && !s_tdma_event_dma_active && dma_hw->abort == 0u);
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
        assert(s_tdma_event_dma[stream].channel == -1 && !s_tdma_event_dma[stream].running);
        assert(s_tdma_event_dma[stream].produced == 0u && s_tdma_event_dma[stream].consumed == 0u);
    }
    assert(tdma_event_dma_stop() && unclaims == 3u);
    assert(claimed[0] && claimed[2] && claimed[7]);
    assert(tdma_event_dma_start(&pio_bank));
}

static void observer_start(void) {
    tdma_event_observer_init(&s_tdma_event_observer);
    const tdma_event_config_t config = {
        .pio_hz = 250000000u, .epoch_limit_cycles = 10000u,
        .join_timeout_cycles = 1000u, .min_frame_cycles = 100u,
        .min_tx_delay_cycles = 0u, .max_tx_delay_cycles = 20u};
    assert(tdma_event_observer_start(&s_tdma_event_observer, &config, 1u,
                                  (tdma_event_interval_t){0u, 0u}, 0u));
}

static void empty_pipeline(void) {
    setup();
    observer_start();
    /* Old FIFO words have been read by DMA but their writes are still pending. */
    tdma_event_batch_t batch = harvest();
    assert_clean(&batch);
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream)
        assert(batch.count[stream] == 0u);
    batch.epoch = 1u;
    batch.observed = (tdma_event_interval_t){190u, 200u};
    tdma_event_record_t out[TDMA_EVENT_MAX_RECORDS];
    assert(tdma_event_observer_feed(&s_tdma_event_observer, &batch, out) == 0u);
    assert(s_tdma_event_observer.state == TDMA_EVENT_ACTIVE);
    complete_word(0u, UINT32_MAX - 50u);
    complete_word(0u, UINT32_MAX);
    complete_word(1u, UINT32_MAX - 51u);
    complete_word(1u, UINT32_MAX);
    complete_word(2u, 40u << 24u);
    batch = harvest();
    assert_clean(&batch);
    batch.epoch = 1u;
    batch.observed = (tdma_event_interval_t){201u, 210u};
    assert(tdma_event_observer_feed(&s_tdma_event_observer, &batch, out) == 1u);
    assert(out[0].sequence == 40u && out[0].rx_elapsed_cycles == 101u &&
           out[0].tx_elapsed_cycles == 103u);
    assert(!out[0].timestamp_valid && !out[0].dpll_eligible);
    batch = harvest();
    assert_clean(&batch);
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream)
        assert(batch.count[stream] == 0u);
}

static void strict_join_timeout(void) {
    setup();
    observer_start();
    complete_word(0u, UINT32_MAX - 50u);
    tdma_event_batch_t batch = harvest();
    batch.epoch = 1u;
    batch.observed = (tdma_event_interval_t){190u, 200u};
    tdma_event_record_t out[TDMA_EVENT_MAX_RECORDS];
    assert(tdma_event_observer_feed(&s_tdma_event_observer, &batch, out) == 0u);
    assert(s_tdma_event_observer.pending_count[0] == 1u);
    batch = harvest();
    batch.epoch = 1u;
    batch.observed = (tdma_event_interval_t){2000u, 2010u};
    assert(tdma_event_observer_feed(&s_tdma_event_observer, &batch, out) == 0u);
    assert(s_tdma_event_observer.state == TDMA_EVENT_INVALID &&
           s_tdma_event_observer.reason == TDMA_EVENT_JOIN_TIMEOUT);
}

static void post_fault_retires_epoch(void) {
    setup();
    observer_start();
    complete_many(0u, 2u);
    race = RACE_OVERWRITE;
    fence_hook = copy_hook;
    tdma_event_batch_t batch = harvest();
    assert(batch.post_faults == TDMA_EVENT_FAULT_OVERFLOW);
    batch.epoch = 1u;
    batch.observed = (tdma_event_interval_t){190u, 200u};
    tdma_event_record_t out[TDMA_EVENT_MAX_RECORDS];
    assert(tdma_event_observer_feed(&s_tdma_event_observer, &batch, out) == 0u);
    assert(s_tdma_event_observer.state == TDMA_EVENT_INVALID &&
           s_tdma_event_observer.reason == TDMA_EVENT_POST_FAULT);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *name = argv[1];
    if (!strcmp(name, "configuration")) configuration();
    else if (!strcmp(name, "progress_wraps")) progress_wraps();
    else if (!strncmp(name, "ring_", 5u)) ring_lapse((uint)strtoul(name + 5, NULL, 10));
    else if (!strcmp(name, "copy_safe")) copy_race(RACE_SAFE);
    else if (!strcmp(name, "copy_overwrite")) copy_race(RACE_OVERWRITE);
    else if (!strcmp(name, "copy_fault")) copy_race(RACE_FAULT);
    else if (!strcmp(name, "copy_mode")) copy_race(RACE_MODE);
    else if (!strcmp(name, "staging_capacity")) staging_capacity();
    else if (!strncmp(name, "fault_", 6u)) invalid_state(name);
    else if (!strcmp(name, "near_exhaustion")) near_exhaustion();
    else if (!strncmp(name, "claim_", 6u)) claim_failure((uint)strtoul(name + 6, NULL, 10));
    else if (!strcmp(name, "stop_retry")) stop_retry();
    else if (!strcmp(name, "empty_pipeline")) empty_pipeline();
    else if (!strcmp(name, "strict_join_timeout")) strict_join_timeout();
    else if (!strcmp(name, "post_fault_retires_epoch")) post_fault_retires_epoch();
    else abort();
    printf("PASS %s\n", name);
    return 0;
}
'''
