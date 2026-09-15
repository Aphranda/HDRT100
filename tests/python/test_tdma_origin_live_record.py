"""Exercise the actual live-copy function with a simulated cyclic DMA writer.

Production is included unchanged. The host intercepts memcpy/atomic reads to
advance the writer at each byte or final guard; no test hooks enter firmware.
The physical owner's elapsed-time/ABA bound is a separate integration duty.
"""
from pathlib import Path
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def live_record_executable(tmp_path_factory):
    production = (ROOT / "components/tdma/src/tdma_origin_exchange.c").as_posix()
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tdma_origin_exchange.h"

static tdma_origin_plan_state_t state;
static tdma_origin_exchange_t exchange;
static tdma_origin_record_t records[TDMA_ORIGIN_RECORD_COUNT];
static tdma_origin_record_t selected;
static unsigned write_slot, scheduled_completed, cut, action, final_action;
static unsigned publish_reads, record_copies;
static bool inject;

static tdma_origin_record_t sample(uint32_t sequence)
{
    tdma_origin_record_t r;
    memset(&r, 0, sizeof(r));
    r.observation.sequence = sequence;
    r.observation.identity = sequence ^ 0x12345678u;
    r.observation.local_generation = 17u;
    r.observation.rtt_remaining = sequence + 32u;
    r.observation.rtt_present = 1u;
    r.returned_trailer = sequence ^ 0x98765432u;
    r.epoch = 73u;
    r.format = TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME;
    r.raw_time.arm_before[0] = 12u;
    r.raw_time.arm_before[1] = sequence + 101u;
    r.raw_time.arm_before[2] = 12u;
    r.raw_time.arm_after[0] = 12u;
    r.raw_time.arm_after[1] = sequence + 102u;
    r.raw_time.arm_after[2] = 12u;
    r.raw_time.latch_remaining = UINT32_MAX - sequence;
    r.raw_time.tick_hz = 250000000u;
    r.sequence_end = sequence;
    return r;
}

static void dma_complete_one(void)
{
    /* Advance the simulated graph's physical cursor, then commit. This
     * deliberately does not select a reader index from its formula. */
    records[write_slot] = sample(state.observation_sequence + 1u);
    state.observation_sequence++;
    write_slot = (write_slot + 1u) % TDMA_ORIGIN_RECORD_COUNT;
    state.record_published_version += 2u;
}

static void dma_action(unsigned which)
{
    if (which == 1u || which == 2u) {
        for (unsigned n = 0; n < scheduled_completed; ++n) dma_complete_one();
        if (which == 2u) {
            /* The next write may start BEFORE its publication. Break the
             * target body while retaining its old sequence_end sentinel. */
            const tdma_origin_record_t partial = sample(0xeeee0000u);
            memcpy(&records[write_slot], &partial, sizeof(partial) / 2u);
        }
    } else if (which == 3u) state.record_epoch++;
    else if (which == 4u) state.fault = 9u;
    else if (which == 5u) state.record_published_version |= 1u;
    else if (which == 6u) state.record_published_version -= 2u;
    else if (which == 7u) {
        /* Even observation_version does not make these mutable raw fields
         * stable; a correct implementation never uses them as the record. */
        memset(&state.record_time, 0xee, sizeof(state.record_time));
    }
}

static uint32_t controlled_load(const uint32_t *address, int order)
{
    (void)order;
    if (address == &state.record_published_version && ++publish_reads == 2u)
        dma_action(final_action);
    return *address;
}

static void *controlled_copy(void *destination, const void *source, size_t size)
{
    const uintptr_t begin = (uintptr_t)records;
    if (inject && (uintptr_t)source >= begin &&
        (uintptr_t)source < begin + sizeof(records)) {
        ++record_copies;
        for (size_t i = 0; i <= size; ++i) {
            if (i == cut) dma_action(action);
            if (i < size) ((unsigned char *)destination)[i] = ((const unsigned char *)source)[i];
        }
        return destination;
    }
    return memcpy(destination, source, size);
}
#define memcpy controlled_copy
#define __atomic_load_n controlled_load
#include "''' + production + r'''"
#undef __atomic_load_n
#undef memcpy

static void fixture(unsigned completed)
{
    memset(&state, 0, sizeof(state));
    memset(&exchange, 0, sizeof(exchange));
    memset(records, 0, sizeof(records));
    state.record_epoch = 73u;
    state.record_format = TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME;
    state.observation_sequence = 1000u;
    exchange.state = &state;
    exchange.observation_version = 456u;
    exchange.rx_version[0] = 10u; exchange.rx_version[1] = 12u;
    exchange.generation = 71u; exchange.pending = true;
    write_slot = 0u;
    for (unsigned n = 0; n < completed; ++n) dma_complete_one();
    selected = records[(write_slot + TDMA_ORIGIN_RECORD_COUNT - 1u) % TDMA_ORIGIN_RECORD_COUNT];
    scheduled_completed = cut = action = final_action = 0u;
    publish_reads = record_copies = 0u;
    inject = true;
}

static void check(bool expected)
{
    const tdma_origin_exchange_t before_exchange = exchange;
    tdma_origin_record_frozen_t out, before;
    memset(&out, 0xa5, sizeof(out)); before = out;
    const uint32_t version = state.record_published_version;
    const bool ok = tdma_origin_exchange_copy_live_record(&exchange, records, 73u, &out);
    assert(ok == expected);
    assert(memcmp(&exchange, &before_exchange, sizeof(exchange)) == 0);
    if (ok) {
        assert(out.epoch == 73u && out.fault == 0u && out.published_version == version);
        assert(memcmp(&out.record, &selected, sizeof(selected)) == 0);
    } else assert(memcmp(&out, &before, sizeof(out)) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const unsigned scenario = (unsigned)strtoul(argv[1], NULL, 10);
    unsigned cases = 0;
    if (scenario == 0u) {
        for (unsigned completed = 1; completed <= 17u; ++completed) {
            fixture(completed); check(true); assert(record_copies == 1u); ++cases;
            /* Non-consuming read: same complete record can be read again. */
            publish_reads = 0u; check(true); ++cases;
        }
    } else if (scenario == 1u) {
        /* Every byte boundary, including before/after the whole copy, sees
         * 0..9 newly committed records plus an optionally active next write. */
        for (unsigned completed = 1; completed <= 9u; ++completed)
        for (unsigned advance = 0; advance <= 9u; ++advance)
        for (unsigned partial = 0; partial <= 1u; ++partial)
        for (unsigned byte = 0; byte <= sizeof(tdma_origin_record_t); ++byte) {
            fixture(completed); cut = byte; scheduled_completed = advance;
            action = partial ? 2u : 1u;
            check(advance < TDMA_ORIGIN_RECORD_COUNT - 1u); ++cases;
        }
    } else if (scenario == 2u) {
        for (unsigned kind = 3; kind <= 7u; ++kind)
        for (unsigned byte = 0; byte <= sizeof(tdma_origin_record_t); ++byte) {
            fixture(9u); cut = byte; action = kind; check(kind == 7u); ++cases;
        }
        for (unsigned kind = 1; kind <= 7u; ++kind) {
            fixture(9u); final_action = kind; scheduled_completed = 7u;
            check(kind == 7u); ++cases;
        }
    } else if (scenario == 3u) {
        for (unsigned kind = 0; kind < 8u; ++kind) {
            fixture(9u);
            if (kind == 0u) state.record_published_version = 0u;
            else if (kind == 1u) state.record_published_version |= 1u;
            else if (kind == 2u) state.record_epoch++;
            else if (kind == 3u) state.fault = 1u;
            else if (kind == 4u) records[0].epoch++;
            else if (kind == 5u) records[0].format = TDMA_ORIGIN_RECORD_FORMAT_RTT;
            else if (kind == 6u) records[0].sequence_end++;
            else exchange.state = NULL;
            check(false); ++cases;
        }
        fixture(9u);
        tdma_origin_record_frozen_t out, before;
        memset(&out, 0xa5, sizeof(out)); before = out;
        assert(!tdma_origin_exchange_copy_live_record(NULL, records, 73u, &out));
        assert(!tdma_origin_exchange_copy_live_record(&exchange, NULL, 73u, &out));
        assert(!tdma_origin_exchange_copy_live_record(&exchange, records, 0u, &out));
        assert(!tdma_origin_exchange_copy_live_record(&exchange, records, 73u, NULL));
        assert(memcmp(&out, &before, sizeof(out)) == 0); cases += 4u;
    } else if (scenario == 4u) {
        fixture(7u); state.record_published_version = UINT32_MAX - 1u;
        check(true); ++cases;
        for (unsigned advance = 1; advance <= 9u; ++advance) {
            fixture(7u); state.record_published_version = UINT32_MAX - 1u;
            scheduled_completed = advance; cut = sizeof(selected) / 2u; action = 1u;
            check(false); ++cases;
        }
        /* A zero publication at wrap is conservatively skipped, and the
         * next nonzero complete publication becomes readable again. */
        fixture(8u); state.record_published_version = 0u; check(false); ++cases;
        dma_complete_one(); selected = records[0]; publish_reads = 0u; check(true); ++cases;
    } else if (scenario == 5u) {
        fixture(9u);
        /* Missing return, unqualified latch and non-first sequence zero are
         * still complete diagnostic records. Do not invent eligibility here. */
        selected = sample(0u); selected.flags = 0u;
        selected.observation.rtt_present = 0u;
        selected.observation.output_remaining = 11u;
        selected.raw_time.tick_hz = 0u;
        records[0] = selected; check(true); ++cases;
    } else assert(!"unknown scenario");
    printf("scenario=%u cases=%u\n", scenario, cases);
    return 0;
}
'''
    return compile_executable(tmp_path_factory.mktemp("origin-live-record"),
                              "origin_live_record", harness)


@pytest.mark.parametrize("scenario", range(6), ids=[
    "normal-and-repeat", "all-byte-dma-interleavings", "mid-copy-and-final-guards",
    "invalid-inputs", "publication-wrap", "raw-record-not-eligibility"])
def test_actual_live_record_copy(live_record_executable, scenario):
    result = subprocess.run([str(live_record_executable), str(scenario)],
                            capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    (live_record_executable.parent / f"scenario-{scenario}.stdout.txt").write_text(
        result.stdout, encoding="utf-8")
    (live_record_executable.parent / f"scenario-{scenario}.stderr.txt").write_text(
        result.stderr, encoding="utf-8")
