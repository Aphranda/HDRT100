"""Production sparse feedback matcher: lifetimes, retained witnesses and exact math.

No hardware, wire parser or DCO is simulated. The independent Python Fraction
oracle checks outward bounds, including non-integral negative results and the
full admitted 64-bit offset/2-second interval ranges.
"""
from fractions import Fraction
import math
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
U64 = (1 << 64) - 1
INVALID, NO_REFERENCE, BASELINED, DUPLICATE, STALE, REBASED, MATCHED = range(7)


@pytest.fixture(scope="module")
def match_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-feedback-match")
    source = directory / "match_test.c"
    source.write_text(HARNESS, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = directory / ("match_test.exe" if os.name == "nt" else "match_test")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I" + str(ROOT / "components/vdc_dpll_manager/inc"), str(source),
        str(ROOT / "components/vdc_dpll_manager/src/vdc_feedback_match.c"),
        "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("scenario", ["lifecycle", "sparse", "generation", "source",
    "retention", "multiple", "arguments", "sequence", "model_cache", "model_state", "model_lifetime"])
def test_production_match_lifecycle(match_executable, scenario):
    result = subprocess.run([str(match_executable), scenario], capture_output=True,
                            text=True, timeout=10)
    (match_executable.parent / (scenario + ".log")).write_text(
        result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "feedback match: passed" in result.stdout


def exact_interval(case):
    hz, old_rx, new_rx, old_lo, old_hi, new_lo, new_hi = case
    if new_rx <= old_rx or new_lo <= old_hi:
        return INVALID, None
    source_delta = new_rx - old_rx
    ref_min = new_lo - old_hi
    ref_max = new_hi - old_lo
    if source_delta > 2 * hz or ref_max > 2 * hz:
        return REBASED, None
    lower = (Fraction(source_delta, ref_max) - 1) * 1_000_000_000
    upper = (Fraction(source_delta, ref_min) - 1) * 1_000_000_000
    return MATCHED, (math.floor(lower), math.ceil(upper))


def check_cases(executable, name, cases):
    source = "".join(" ".join(map(str, case)) + "\n" for case in cases)
    result = subprocess.run([str(executable), "math"], input=source, capture_output=True,
                            text=True, timeout=30)
    (executable.parent / (name + ".input")).write_text(source, encoding="utf-8")
    (executable.parent / (name + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    rows = result.stdout.splitlines()
    assert len(rows) == len(cases)
    for case, row in zip(cases, rows, strict=True):
        actual_result, has_pair, lo, hi, old_seq, new_seq = map(int, row.split())
        expected_result, expected_bounds = exact_interval(case)
        assert actual_result == expected_result, (case, row, expected_result)
        assert has_pair == (expected_result == MATCHED), (case, row)
        if expected_bounds is not None:
            assert (lo, hi) == expected_bounds, (case, row, expected_bounds)
            assert (old_seq, new_seq) == (0, 1)


def test_outward_rounding_zero_offsets_and_two_second_edges(match_executable):
    cases = [
        (500_000_000, 0, 1_000_000_000, 0, 0, 1, 1),
        (500_000_000, 0, 1, 0, 0, 1_000_000_000, 1_000_000_000),
        (500_000_000, 0, 1_000_000_000, 0, 0, 1_000_000_000, 1_000_000_000),
        (500_000_000, 0, 1_000_000_001, 0, 0, 1_000_000_000, 1_000_000_000),
        (500_000_000, 0, 1_000_000_000, 0, 0, 1_000_000_000, 1_000_000_001),
        (250_000_000, 1, 2, 0, 0, 3, 4),
        (250_000_000, 1, 6, 0, 1, 4, 6),
        (1, 0, 1, 0, 0, 1, 2),
        (1, 0, 2, 0, 0, 2, 2),
        (1, 0, 3, 0, 0, 2, 2),
        (1, U64 - 2, U64, U64 - 2, U64 - 2, U64, U64),
        (250_000_000, 100, 100, 10, 10, 20, 20),
        (250_000_000, 100, 99, 10, 10, 20, 20),
        (250_000_000, 100, 101, 10, 20, 20, 30),
        (250_000_000, 100, 101, 10, 20, 1, 5),
        (250_000_000, 100, 101, 10, 20, 19, 30),
    ]
    check_cases(match_executable, "arithmetic_edges", cases)


def test_arbitrary_precision_random_intervals_and_large_origins(match_executable):
    rng = random.Random(0xFEE0BA5E)
    cases = []
    for _ in range(5000):
        hz = rng.choice([1, 3, 1_000_000, 125_000_000, 250_000_000, 500_000_000])
        limit = 2 * hz
        old_width, new_width = rng.randrange(0, 1000), rng.randrange(0, 1000)
        gap = rng.choice([rng.randrange(1, limit + 2), 1, limit, limit + 1])
        delta = rng.choice([rng.randrange(1, limit + 2), 1, limit, limit + 1])
        total_reference = old_width + gap + new_width
        old_lo = rng.choice([0, rng.randrange(U64 - total_reference + 1), U64 - total_reference])
        old_rx = rng.choice([0, rng.randrange(U64 - delta + 1), U64 - delta])
        cases.append((hz, old_rx, old_rx + delta, old_lo, old_lo + old_width,
                      old_lo + old_width + gap, old_lo + total_reference))
    check_cases(match_executable, "random_intervals", cases)


def test_fixed_offsets_cancel_but_variable_reference_brackets_remain(match_executable):
    base = (250_000_000, 1000, 25_001_100, 20, 24, 25_000_020, 25_000_024)
    cases = [base]
    for rx_offset, ref_offset in [(1234, 5678), (1 << 40, 1 << 54),
                                  (U64 - base[2], U64 - base[6])]:
        hz, old_rx, new_rx, old_lo, old_hi, new_lo, new_hi = base
        cases.append((hz, old_rx + rx_offset, new_rx + rx_offset,
            old_lo + ref_offset, old_hi + ref_offset, new_lo + ref_offset, new_hi + ref_offset))
    assert len({exact_interval(case) for case in cases}) == 1
    wider = list(base)
    wider[-1] += 100
    cases.append(tuple(wider))
    assert exact_interval(cases[-1])[1][0] < exact_interval(base)[1][0]
    check_cases(match_executable, "offset_cancellation", cases)


def model_interval(case):
    _, old_rx, old_width, new_rx, new_width, old_lo, old_hi, new_lo, new_hi = case
    if new_rx + new_width > U64 or new_rx <= old_rx + old_width or new_lo <= old_hi:
        return INVALID, None
    source_min, source_max = new_rx - old_rx - old_width, new_rx + new_width - old_rx
    reference_min, reference_max = new_lo - old_hi, new_hi - old_lo
    if max(source_max, reference_max) > 2_000_000_000:
        return REBASED, None
    return MATCHED, (math.floor((Fraction(source_min, reference_max) - 1) * 1_000_000_000),
                     math.ceil((Fraction(source_max, reference_min) - 1) * 1_000_000_000))


def check_model_cases(executable, name, cases):
    source = ''.join(' '.join(map(str, case)) + '\n' for case in cases)
    result = subprocess.run([str(executable), 'model_math'], input=source, capture_output=True,
                            text=True, timeout=30)
    (executable.parent / (name + '.input')).write_text(source, encoding='utf-8')
    (executable.parent / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    rows = result.stdout.splitlines()
    assert len(rows) == len(cases)
    for case, row in zip(cases, rows, strict=True):
        code, found, lo, hi, domain, width0, width1, token0, token1 = map(int, row.split())
        expected, bounds = model_interval(case)
        assert (code, found) == (expected, int(expected == MATCHED)), (case, row)
        if bounds is not None:
            assert (lo, hi) == bounds, (case, row, bounds)
            assert (domain, width0, width1, token0, token1) == (2, case[2], case[4], 11, 12)


def test_model_interval_edges_use_nanoseconds_and_both_source_bounds(match_executable):
    cases = [
        (1, 0, 0, 2_000_000_000, 0, 0, 0, 1, 1),
        (500_000_000, 0, 0, 1, 0, 0, 0, 2_000_000_000, 2_000_000_000),
        (1, 0, 0, 2_000_000_000, 0, 0, 0, 2_000_000_000, 2_000_000_000),
        (1, 0, 0, 2_000_000_000, 1, 0, 0, 2_000_000_000, 2_000_000_000),
        (1, 0, 0, 2_000_000_000, 0, 0, 0, 2_000_000_000, 2_000_000_001),
        (250_000_000, 100, 5, 105, 0, 0, 0, 100, 100),
        (250_000_000, 100, 5, 104, 8, 0, 0, 100, 100),
        (250_000_000, 100, 5, 106, 0, 0, 0, 100, 100),
        (250_000_000, 100, 0, 99, 0, 0, 0, 100, 100),
        (250_000_000, 100, 0, 110, 5, 0, 10, 10, 20),
        (250_000_000, U64 - 10, 2, U64, 1, 10, 12, 20, 22),
        (250_000_000, U64 - 10, 2, U64 - 1, 1, U64 - 10, U64 - 8, U64 - 1, U64),
        (250_000_000, 1, 1, 6, 2, 0, 1, 4, 6),
    ]
    check_model_cases(match_executable, 'model_edges', cases)


def test_model_arbitrary_precision_random_intervals_and_large_origins(match_executable):
    rng = random.Random(0xDC02)
    cases = []
    for _ in range(5000):
        hz = rng.choice([1, 125_000_000, 250_000_000, 500_000_000])
        sw0, sw1, rw0, rw1 = [rng.randrange(1000) for _ in range(4)]
        sgap, rgap = [rng.choice([1, 2_000_000_000, rng.randrange(1, 2_000_000_002)]) for _ in range(2)]
        stotal, rtotal = sw0 + sgap + sw1, rw0 + rgap + rw1
        sbase = rng.choice([0, U64 - stotal, rng.randrange(U64 - stotal + 1)])
        rbase = rng.choice([0, U64 - rtotal, rng.randrange(U64 - rtotal + 1)])
        cases.append((hz, sbase, sw0, sbase + sw0 + sgap, sw1,
                      rbase, rbase + rw0, rbase + rw0 + rgap, rbase + rtotal))
    check_model_cases(match_executable, 'model_random', cases)


def test_model_constant_offsets_cancel_and_widths_widen_bounds(match_executable):
    base = (250_000_000, 100, 2, 1_000_100, 3, 500, 2 + 500, 1_000_500, 1_000_503)
    cases = [base]
    for source_offset, reference_offset in [(1 << 42, 1 << 50),
            (U64 - base[3] - base[4], U64 - base[8])]:
        hz, s0, sw0, s1, sw1, r0, rh0, r1, rh1 = base
        cases.append((hz, s0 + source_offset, sw0, s1 + source_offset, sw1,
                      r0 + reference_offset, rh0 + reference_offset,
                      r1 + reference_offset, rh1 + reference_offset))
    assert len({model_interval(case) for case in cases}) == 1
    wider = list(base); wider[2] += 4; wider[4] += 5
    cases.append(tuple(wider))
    assert model_interval(cases[-1])[1][0] < model_interval(base)[1][0]
    assert model_interval(cases[-1])[1][1] > model_interval(base)[1][1]
    check_model_cases(match_executable, 'model_offsets', cases)


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "vdc_feedback_match.h"

typedef vdc_feedback_match_cache_t cache_t;
typedef vdc_feedback_match_peer_t peer_t;
typedef vdc_feedback_match_sample_t sample_t;
typedef vdc_feedback_match_reference_t reference_t;
typedef vdc_feedback_match_snapshot_t snapshot_t;

static sample_t sample(uint32_t sequence, uint64_t rx)
{
    return (sample_t){.source_arm_epoch=UINT64_C(0x100000002),
        .rx_elapsed_cycles=rx, .source_clock_epoch_id=0u,
        .source_clock_run_id=0u, .observer_epoch=9u,
        .measurement_sequence=sequence, .tick_hz=250000000u};
}

static void initialize(cache_t *cache, peer_t *peer)
{
    vdc_feedback_match_cache_init(cache);
    vdc_feedback_match_peer_init(peer);
    assert(vdc_feedback_match_cache_bind(cache, 71u, 250000000u));
}

static void put(cache_t *cache, uint32_t seq, uint64_t lo, uint64_t hi)
{
    assert(vdc_feedback_match_cache_put(cache, seq, seq ^ 0x12345678u,
        (seq + 1u) * 2u, lo, hi));
}

static void failed_put(cache_t *cache, uint32_t seq, uint32_t identity,
    uint32_t version, uint64_t lo, uint64_t hi)
{
    cache_t before = *cache;
    assert(!vdc_feedback_match_cache_put(cache, seq, identity, version, lo, hi));
    assert(memcmp(cache, &before, sizeof(before)) == 0);
}

static void unchanged_update(cache_t *cache, peer_t *peer, sample_t s,
    vdc_feedback_match_result_t expected)
{
    peer_t before = *peer;
    assert(vdc_feedback_match_update(cache, peer, &s) == expected);
    assert(memcmp(peer, &before, sizeof(before)) == 0);
}

static snapshot_t establish_pair(cache_t *cache, peer_t *peer)
{
    initialize(cache, peer);
    put(cache, 10u, 1000u, 1004u);
    sample_t s = sample(10u, 500u);
    assert(vdc_feedback_match_update(cache, peer, &s) == VDC_FEEDBACK_MATCH_BASELINED);
    put(cache, 11u, 2000u, 2004u);
    s = sample(11u, 1500u);
    assert(vdc_feedback_match_update(cache, peer, &s) == VDC_FEEDBACK_MATCH_MATCHED);
    snapshot_t result;
    assert(vdc_feedback_match_peer_snapshot(peer, &result));
    assert(result.has_pair == 1u && result.pairs[0].measurement_sequence == 10u &&
        result.pairs[1].measurement_sequence == 11u);
    assert(result.source.source_arm_epoch == s.source_arm_epoch);
    assert(result.source.source_clock_epoch_id == 0u && result.source.source_clock_run_id == 0u);
    assert(result.source.observer_epoch == 9u && result.source.tick_hz == 250000000u);
    assert(result.reference_epoch == 71u && result.reference_generation == cache->generation);
    assert(result.pairs[0].reference_tx_lo == 1000u && result.pairs[0].reference_tx_hi == 1004u);
    assert(result.pairs[1].reference_tx_lo == 2000u && result.pairs[1].reference_tx_hi == 2004u);
    assert(result.pairs[0].rx_elapsed_cycles == 500u && result.pairs[1].rx_elapsed_cycles == 1500u);
    assert(result.pairs[1].reference_identity_crc32 == (11u ^ 0x12345678u));
    assert(result.reserved==0 && result.pairs[0].rx_width_ns==0 && result.pairs[1].rx_width_ns==0);
    assert(result.pairs[0].source_model_token==0 && result.pairs[1].source_model_token==0);
    return result;
}

static void test_lifecycle(void)
{
    assert(sizeof(reference_t) == 32u && sizeof(cache_t) == 4120u &&
        sizeof(vdc_feedback_match_pair_t) == 40u && sizeof(snapshot_t) == 136u && sizeof(peer_t) == 216u);
    cache_t cache; peer_t peer;
    initialize(&cache, &peer);
    put(&cache, 0u, 0u, 0u);
    reference_t ref, before;
    assert(vdc_feedback_match_cache_lookup(&cache, 0u, &ref));
    assert(ref.measurement_sequence == 0u && ref.generation == cache.generation);
    cache_t saved = cache;
    assert(vdc_feedback_match_cache_bind(&cache, 71u, 250000000u));
    assert(memcmp(&cache, &saved, sizeof(cache)) == 0);
    assert(!vdc_feedback_match_cache_bind(&cache, 0u, 250000000u));
    assert(!vdc_feedback_match_cache_bind(&cache, 72u, 0u));
    assert(!vdc_feedback_match_cache_bind(&cache, 72u, 500000001u));
    assert(memcmp(&cache, &saved, sizeof(cache)) == 0);
    assert(vdc_feedback_match_cache_bind(&cache, 72u, 250000000u));
    assert(memcmp(cache.entries, saved.entries, sizeof(cache.entries)) == 0);
    before = ref;
    assert(!vdc_feedback_match_cache_lookup(&cache, 0u, &ref));
    assert(memcmp(&ref, &before, sizeof(ref)) == 0);
    put(&cache, 0u, 50u, 51u);
    saved = cache;
    vdc_feedback_match_cache_retire(&cache);
    assert(cache.reference_epoch == 0u && cache.generation > saved.generation);
    assert(memcmp(cache.entries, saved.entries, sizeof(cache.entries)) == 0);
    assert(!vdc_feedback_match_cache_lookup(&cache, 0u, &ref));
    uint32_t retired = cache.generation;
    vdc_feedback_match_cache_retire(&cache);
    assert(cache.generation == retired);
    assert(vdc_feedback_match_cache_bind(&cache, 72u, 250000000u));
    assert(!vdc_feedback_match_cache_lookup(&cache, 0u, &ref));
    put(&cache, 0u, 60u, 60u);
    assert(vdc_feedback_match_cache_bind(&cache, 72u, 125000000u));
    assert(!vdc_feedback_match_cache_lookup(&cache, 0u, &ref));
}

static void test_sparse(void)
{
    cache_t cache; peer_t peer;
    initialize(&cache, &peer);
    put(&cache, 1u, 1000u, 1004u);
    put(&cache, 3u, 3000u, 3004u);
    reference_t r;
    assert(!vdc_feedback_match_cache_lookup(&cache, 2u, &r));
    unchanged_update(&cache, &peer, sample(2u, 10u), VDC_FEEDBACK_MATCH_NO_REFERENCE);
    sample_t s = sample(1u, 100u);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_BASELINED);
    cache_t before = cache;
    assert(vdc_feedback_match_cache_put(&cache, 3u, 3u ^ 0x12345678u, 8u, 3000u, 3004u));
    assert(memcmp(&cache, &before, sizeof(cache)) == 0);
    failed_put(&cache, 3u, 1u, 8u, 3000u, 3004u);
    failed_put(&cache, 3u, 3u ^ 0x12345678u, 8u, 3001u, 3004u);
    failed_put(&cache, 3u, 3u ^ 0x12345678u, 10u, 3000u, 3004u);
    failed_put(&cache, 2u, 2u, 10u, 2000u, 2004u);
    failed_put(&cache, 4u, 4u, 8u, 4000u, 4004u);
    failed_put(&cache, 4u, 4u, 7u, 4000u, 4004u);
    failed_put(&cache, 4u, 4u, 0u, 4000u, 4004u);
    failed_put(&cache, 4u, 4u, 10u, 4005u, 4004u);
    put(&cache, 129u, 129000u, 129004u);
    assert(!vdc_feedback_match_cache_lookup(&cache, 1u, &r));
    assert(vdc_feedback_match_cache_lookup(&cache, 129u, &r));
    /* Previous exact reference survives a sparse cache overwrite privately. */
    s = sample(129u, 128100u);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_MATCHED);
    assert(peer.snapshot.pairs[0].measurement_sequence == 1u &&
        peer.snapshot.pairs[1].measurement_sequence == 129u);
}

static void test_generation(void)
{
    cache_t cache; peer_t peer;
    snapshot_t old = establish_pair(&cache, &peer);
    cache.generation = UINT32_MAX - 1u;
    assert(vdc_feedback_match_cache_bind(&cache, 72u, 250000000u));
    assert(cache.generation == UINT32_MAX);
    put(&cache, 10u, 4000u, 4001u);
    sample_t s = sample(10u, 20u);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_BASELINED);
    assert(memcmp(&old, &peer.snapshot, sizeof(old)) == 0);
    assert(vdc_feedback_match_cache_bind(&cache, 72u, 250000000u));
    assert(cache.generation == UINT32_MAX);
    assert(!vdc_feedback_match_cache_bind(&cache, 73u, 250000000u));
    assert(cache.generation == UINT32_MAX && cache.reference_epoch == 0u);
    assert(!vdc_feedback_match_cache_bind(&cache, 72u, 250000000u));
    unchanged_update(&cache, &peer, s, VDC_FEEDBACK_MATCH_NO_REFERENCE);
    vdc_feedback_match_cache_retire(&cache);
    assert(cache.generation == UINT32_MAX);
    initialize(&cache, &peer);
    cache.generation = UINT32_MAX;
    put(&cache, 1u, 10u, 10u);
    vdc_feedback_match_cache_retire(&cache);
    assert(!vdc_feedback_match_cache_bind(&cache, 71u, 250000000u));
}

static void test_source(void)
{
    for (unsigned field = 0; field < 4u; ++field) {
        cache_t cache; peer_t peer;
        snapshot_t old = establish_pair(&cache, &peer);
        sample_t s = sample(12u, 10u);
        switch (field) {
        case 0: ++s.source_arm_epoch; break;
        case 1: ++s.source_clock_epoch_id; break;
        case 2: ++s.source_clock_run_id; break;
        default: ++s.observer_epoch; break;
        }
        unchanged_update(&cache, &peer, s, VDC_FEEDBACK_MATCH_NO_REFERENCE);
        put(&cache, 12u, 3000u, 3001u);
        assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_BASELINED);
        assert(peer.previous.rx_elapsed_cycles == 10u);
        assert(memcmp(&old, &peer.snapshot, sizeof(old)) == 0);
        put(&cache, 13u, 4000u, 4001u);
        s.measurement_sequence = 13u; s.rx_elapsed_cycles = 1010u;
        assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_MATCHED);
        assert(peer.snapshot.pairs[0].measurement_sequence == 12u);
        assert(peer.snapshot.pairs[1].measurement_sequence == 13u);
    }
    cache_t cache; peer_t peer;
    snapshot_t old = establish_pair(&cache, &peer);
    vdc_feedback_match_cache_retire(&cache);
    assert(vdc_feedback_match_cache_bind(&cache, 71u, 250000000u));
    put(&cache, 10u, 8000u, 8001u);
    sample_t s = sample(10u, 7000u);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_BASELINED);
    assert(peer.reference_epoch == old.reference_epoch &&
        peer.reference_generation != old.reference_generation);
    assert(memcmp(&old, &peer.snapshot, sizeof(old)) == 0);
    assert(vdc_feedback_match_cache_bind(&cache, 71u, 125000000u));
    put(&cache, 10u, 9000u, 9001u);
    unchanged_update(&cache, &peer, s, VDC_FEEDBACK_MATCH_INVALID);
    s.tick_hz = 125000000u;
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_BASELINED);
}

static void test_retention(void)
{
    cache_t cache; peer_t peer;
    snapshot_t saved = establish_pair(&cache, &peer);
    unchanged_update(&cache, &peer, sample(11u, 1500u), VDC_FEEDBACK_MATCH_DUPLICATE);
    unchanged_update(&cache, &peer, sample(11u, 1501u), VDC_FEEDBACK_MATCH_INVALID);
    unchanged_update(&cache, &peer, sample(10u, 500u), VDC_FEEDBACK_MATCH_STALE);
    unchanged_update(&cache, &peer, sample(12u, 2500u), VDC_FEEDBACK_MATCH_NO_REFERENCE);
    put(&cache, 12u, 3000u, 3001u);
    unchanged_update(&cache, &peer, sample(12u, 1500u), VDC_FEEDBACK_MATCH_INVALID);
    unchanged_update(&cache, &peer, sample(12u, 1499u), VDC_FEEDBACK_MATCH_INVALID);
    put(&cache, 13u, 4000u, 4001u);
    sample_t s = sample(13u, 3500u);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_MATCHED);
    assert(peer.snapshot.pairs[0].measurement_sequence == 11u);
    saved = peer.snapshot;
    put(&cache, 14u, 600000000u, 600000001u);
    s = sample(14u, 600000000u);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_INTERVAL_REBASED);
    assert(memcmp(&saved, &peer.snapshot, sizeof(saved)) == 0);
    assert(peer.previous.measurement_sequence == 14u);
    put(&cache, 15u, 600001000u, 600001001u);
    s = sample(15u, 600001000u);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_MATCHED);
    assert(peer.snapshot.pairs[0].measurement_sequence == 14u);
    put(&cache, 16u, 600001001u, 600002000u);
    unchanged_update(&cache, &peer, sample(16u, 600002000u), VDC_FEEDBACK_MATCH_INVALID);
    saved = peer.snapshot;
    put(&cache, 17u, 600002001u, 600002002u);
    s = sample(17u, 1200000000u);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_INTERVAL_REBASED);
    assert(memcmp(&saved, &peer.snapshot, sizeof(saved)) == 0);
}

static void test_multiple(void)
{
    cache_t cache; peer_t peers[3];
    vdc_feedback_match_cache_init(&cache);
    assert(vdc_feedback_match_cache_bind(&cache, 123u, 250000000u));
    put(&cache, 100u, 10000u, 10000u);
    put(&cache, 200u, 250010000u, 250010000u);
    const uint64_t increments[] = {250000000u, 250001000u, 249999000u};
    const int64_t expected[] = {0, 4000, -4000};
    for (unsigned i = 0; i < 3u; ++i) {
        vdc_feedback_match_peer_init(&peers[i]);
        sample_t s = sample(100u, 100000u * i);
        s.source_arm_epoch += i;
        assert(vdc_feedback_match_update(&cache, &peers[i], &s) == VDC_FEEDBACK_MATCH_BASELINED);
    }
    for (unsigned j = 0; j < 3u; ++j) {
        const unsigned i = 2u - j;
        sample_t s = sample(200u, 100000u * i + increments[i]);
        s.source_arm_epoch += i;
        assert(vdc_feedback_match_update(&cache, &peers[i], &s) == VDC_FEEDBACK_MATCH_MATCHED);
        assert(peers[i].snapshot.raw_ppb_lo == expected[i] &&
            peers[i].snapshot.raw_ppb_hi == expected[i]);
    }
    peer_t saved[3]; memcpy(saved, peers, sizeof(saved));
    unchanged_update(&cache, &peers[1], sample(300u, 500000000u), VDC_FEEDBACK_MATCH_NO_REFERENCE);
    assert(memcmp(peers, saved, sizeof(saved)) == 0);
}

static void test_arguments(void)
{
    cache_t cache; peer_t peer;
    initialize(&cache, &peer);
    snapshot_t out, saved;
    memset(&out, 0xa5, sizeof(out)); saved = out;
    assert(!vdc_feedback_match_peer_snapshot(&peer, &out));
    assert(memcmp(&out, &saved, sizeof(out)) == 0);
    assert(!vdc_feedback_match_peer_snapshot(NULL, &out));
    assert(!vdc_feedback_match_peer_snapshot(&peer, NULL));
    assert(!vdc_feedback_match_cache_bind(NULL, 1u, 1u));
    assert(!vdc_feedback_match_cache_put(NULL, 1u, 1u, 2u, 1u, 1u));
    assert(!vdc_feedback_match_cache_lookup(NULL, 1u, NULL));
    assert(!vdc_feedback_match_cache_lookup(&cache, 1u, NULL));
    vdc_feedback_match_cache_init(NULL);
    vdc_feedback_match_peer_init(NULL);
    vdc_feedback_match_cache_retire(NULL);
    sample_t s = sample(1u, 100u);
    put(&cache, 1u, 100u, 101u);
    unchanged_update(NULL, &peer, s, VDC_FEEDBACK_MATCH_NO_REFERENCE);
    assert(vdc_feedback_match_update(&cache, NULL, &s) == VDC_FEEDBACK_MATCH_INVALID);
    assert(vdc_feedback_match_update(&cache, &peer, NULL) == VDC_FEEDBACK_MATCH_INVALID);
    s.source_arm_epoch = 0u;
    unchanged_update(&cache, &peer, s, VDC_FEEDBACK_MATCH_INVALID);
    s = sample(1u, 100u); s.observer_epoch = 0u;
    unchanged_update(&cache, &peer, s, VDC_FEEDBACK_MATCH_INVALID);
    s = sample(1u, 100u); s.tick_hz = 0u;
    unchanged_update(&cache, &peer, s, VDC_FEEDBACK_MATCH_INVALID);
    s.tick_hz = 500000001u;
    unchanged_update(&cache, &peer, s, VDC_FEEDBACK_MATCH_INVALID);
    s.tick_hz = 125000000u;
    unchanged_update(&cache, &peer, s, VDC_FEEDBACK_MATCH_INVALID);
}

static void test_sequence(void)
{
    cache_t cache; peer_t peer;
    initialize(&cache, &peer);
    assert(vdc_feedback_match_cache_put(&cache, UINT32_MAX-1u, 1u,
        UINT32_MAX-3u, 100u, 100u));
    sample_t s = sample(UINT32_MAX-1u, ULLONG_MAX-1u);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_BASELINED);
    assert(vdc_feedback_match_cache_put(&cache, UINT32_MAX, 2u,
        UINT32_MAX-1u, 101u, 101u));
    s = sample(UINT32_MAX, ULLONG_MAX);
    assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_MATCHED);
    assert(peer.snapshot.raw_ppb_lo == 0 && peer.snapshot.raw_ppb_hi == 0);
    failed_put(&cache, 0u, 3u, 2u, 102u, 102u);
    failed_put(&cache, UINT32_MAX, 2u, UINT32_MAX-1u, 102u, 102u);
    initialize(&cache, &peer);
    assert(vdc_feedback_match_cache_put(&cache, 0u, 0u, UINT32_MAX-1u, 1u, 1u));
    failed_put(&cache, 1u, 1u, 2u, 2u, 2u);
}

static void model_put(cache_t *cache,uint32_t seq,uint32_t token,uint64_t lo,uint64_t hi)
{ assert(vdc_feedback_model_cache_put(cache,seq,token,(seq+1)*2,lo,hi,seq*100)); }

static snapshot_t model_pair(cache_t *cache,peer_t *peer)
{
    initialize(cache,peer);model_put(cache,10,71,1000,1004);
    sample_t s=sample(10,500);
    assert(vdc_feedback_model_update(cache,peer,&s,4,21)==VDC_FEEDBACK_MATCH_BASELINED);
    model_put(cache,11,72,2000,2004);s=sample(11,1500);
    assert(vdc_feedback_model_update(cache,peer,&s,4,22)==VDC_FEEDBACK_MATCH_MATCHED);
    assert(peer->snapshot.reserved==2 && peer->reserved==2);
    assert(peer->snapshot.pairs[0].source_model_token==21 && peer->snapshot.pairs[1].source_model_token==22);
    assert(peer->snapshot.pairs[0].reference_identity_crc32==71 && peer->snapshot.pairs[1].reference_identity_crc32==72);
    return peer->snapshot;
}

static void unchanged_model(cache_t *cache,peer_t *peer,sample_t s,uint32_t width,uint32_t token,unsigned expected)
{
    const peer_t saved=*peer;
    assert(vdc_feedback_model_update(cache,peer,&s,width,token)==expected);
    assert(!memcmp(&saved,peer,sizeof(saved)));
}

static void test_model_cache(void)
{
    cache_t cache;peer_t peer;initialize(&cache,&peer);
    assert(vdc_feedback_model_cache_put(&cache,1,5,2,100,104,UINT32_MAX));
    reference_t ref;assert(vdc_feedback_match_cache_lookup(&cache,1,&ref));
    assert(ref.prepared_ms==UINT32_MAX && cache.latest_published_version==2);
    const cache_t saved=cache;
    assert(vdc_feedback_model_cache_put(&cache,1,5,2,100,104,0));
    assert(!memcmp(&cache,&saved,sizeof(cache))); /* Duplicate never renews age. */
    for(unsigned kind=0;kind<7;kind++) {
        uint32_t seq=1,token=5,version=2;uint64_t lo=100,hi=104;
        if(kind==0)token=0;
        if(kind==1)token=6;
        if(kind==2)version=4;
        if(kind==3)lo=101;
        if(kind==4)seq=0;
        if(kind==5)seq=2;
        if(kind==6){seq=2;version=3;}
        assert(!vdc_feedback_model_cache_put(&cache,seq,token,version,lo,hi,1234));
        assert(!memcmp(&cache,&saved,sizeof(cache)));
    }
    assert(vdc_feedback_model_cache_put(&cache,2,6,4,200,204,0));
    assert(vdc_feedback_match_cache_lookup(&cache,2,&ref) && ref.prepared_ms==0);
    assert(cache.latest_published_version==4);
    assert(vdc_feedback_model_cache_put(&cache,129,7,6,300,304,1));
    assert(!vdc_feedback_match_cache_lookup(&cache,1,&ref));
    assert(vdc_feedback_match_cache_lookup(&cache,129,&ref) && ref.prepared_ms==1);
    const cache_t before=cache;
    vdc_feedback_match_cache_retire(&cache);
    assert(!memcmp(cache.entries,before.entries,sizeof(cache.entries)));
    assert(vdc_feedback_match_cache_bind(&cache,71,250000000));
    assert(!vdc_feedback_match_cache_lookup(&cache,129,&ref));
    assert(vdc_feedback_model_cache_put(&cache,0,1,2,0,0,0));
    assert(!vdc_feedback_model_cache_put(NULL,0,1,2,0,0,0));
}

static void test_model_state(void)
{
    cache_t cache;peer_t peer;snapshot_t saved=model_pair(&cache,&peer);
    unchanged_model(&cache,&peer,sample(11,1500),4,22,VDC_FEEDBACK_MATCH_DUPLICATE);
    unchanged_model(&cache,&peer,sample(11,1500),5,22,VDC_FEEDBACK_MATCH_INVALID);
    unchanged_model(&cache,&peer,sample(11,1500),4,23,VDC_FEEDBACK_MATCH_INVALID);
    unchanged_model(&cache,&peer,sample(11,1501),4,22,VDC_FEEDBACK_MATCH_INVALID);
    unchanged_model(&cache,&peer,sample(10,500),4,21,VDC_FEEDBACK_MATCH_STALE);
    unchanged_model(&cache,&peer,sample(12,2500),4,22,VDC_FEEDBACK_MATCH_NO_REFERENCE);
    model_put(&cache,12,73,3000,3004);
    unchanged_model(&cache,&peer,sample(12,2500),4,21,VDC_FEEDBACK_MATCH_STALE);
    unchanged_model(&cache,&peer,sample(12,2500),4,0,VDC_FEEDBACK_MATCH_INVALID);
    unchanged_model(&cache,&peer,sample(12,UINT64_MAX),1,23,VDC_FEEDBACK_MATCH_INVALID);
    unchanged_model(&cache,&peer,sample(12,1504),0,23,VDC_FEEDBACK_MATCH_INVALID);
    model_put(&cache,13,71,4000,4004);
    unchanged_model(&cache,&peer,sample(13,3500),4,23,VDC_FEEDBACK_MATCH_STALE);
    assert(!memcmp(&saved,&peer.snapshot,sizeof(saved)));
    model_put(&cache,139,74,5000,5004); /* Overwrites entry 11, but not peer baseline. */
    sample_t s=sample(139,4500);
    assert(vdc_feedback_model_update(&cache,&peer,&s,4,23)==VDC_FEEDBACK_MATCH_MATCHED);
    assert(peer.snapshot.pairs[0].measurement_sequence==11 && peer.snapshot.pairs[1].measurement_sequence==139);
    saved=peer.snapshot;
    model_put(&cache,140,75,UINT64_C(3000000000),UINT64_C(3000000004));
    s=sample(140,UINT64_C(3000000000));
    assert(vdc_feedback_model_update(&cache,&peer,&s,4,24)==VDC_FEEDBACK_MATCH_INTERVAL_REBASED);
    assert(!memcmp(&saved,&peer.snapshot,sizeof(saved)));
    assert(peer.previous.measurement_sequence==140 && peer.previous.source_model_token==24);
    model_put(&cache,141,76,UINT64_C(3000001000),UINT64_C(3000001004));s=sample(141,UINT64_C(3000001000));
    assert(vdc_feedback_model_update(&cache,&peer,&s,4,25)==VDC_FEEDBACK_MATCH_MATCHED);
    assert(peer.snapshot.pairs[0].measurement_sequence==140);
}

static void test_model_lifetime(void)
{
    for(unsigned kind=0;kind<5;kind++) {
        cache_t cache;peer_t peer;snapshot_t saved=model_pair(&cache,&peer);
        sample_t s=sample(12,10);
        if(kind==0)s.source_arm_epoch++;
        if(kind==1)s.source_clock_epoch_id++;
        if(kind==2)s.source_clock_run_id++;
        if(kind==3)s.observer_epoch++;
        if(kind==4){vdc_feedback_match_cache_retire(&cache);assert(vdc_feedback_match_cache_bind(&cache,71,250000000));}
        unchanged_model(&cache,&peer,s,0,1,VDC_FEEDBACK_MATCH_NO_REFERENCE);
        model_put(&cache,12,1,10,10);
        assert(vdc_feedback_model_update(&cache,&peer,&s,0,1)==VDC_FEEDBACK_MATCH_BASELINED);
        assert(!memcmp(&saved,&peer.snapshot,sizeof(saved)));
    }
    cache_t cache;peer_t peer;snapshot_t saved=model_pair(&cache,&peer);
    /* A wrong-domain caller cannot silently pair a model and raw endpoint. */
    sample_t s=sample(11,1500);
    assert(vdc_feedback_match_update(&cache,&peer,&s)==VDC_FEEDBACK_MATCH_BASELINED);
    assert(peer.reserved==0 && peer.previous.source_model_token==0 && peer.previous.rx_width_ns==0);
    assert(!memcmp(&saved,&peer.snapshot,sizeof(saved)));
    assert(vdc_feedback_model_update(&cache,&peer,&s,4,22)==VDC_FEEDBACK_MATCH_BASELINED);
    assert(peer.reserved==2 && peer.previous.source_model_token==22);
    s.source_arm_epoch=0;unchanged_model(&cache,&peer,s,0,1,VDC_FEEDBACK_MATCH_INVALID);
    s=sample(11,100);s.observer_epoch=0;unchanged_model(&cache,&peer,s,0,1,VDC_FEEDBACK_MATCH_INVALID);
    s=sample(11,100);s.tick_hz=1;unchanged_model(&cache,&peer,s,0,1,VDC_FEEDBACK_MATCH_INVALID);
    s=sample(11,100);unchanged_model(NULL,&peer,s,0,1,VDC_FEEDBACK_MATCH_NO_REFERENCE);
    assert(vdc_feedback_model_update(&cache,NULL,&s,0,1)==VDC_FEEDBACK_MATCH_INVALID);
    assert(vdc_feedback_model_update(&cache,&peer,NULL,0,1)==VDC_FEEDBACK_MATCH_INVALID);
}

static void model_math_cases(void)
{
    uint32_t hz,aw,bw;uint64_t a,b,alo,ahi,blo,bhi;
    while(scanf("%"SCNu32" %"SCNu64" %"SCNu32" %"SCNu64" %"SCNu32
        " %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64,&hz,&a,&aw,&b,&bw,&alo,&ahi,&blo,&bhi)==9) {
        cache_t cache;peer_t peer;vdc_feedback_match_cache_init(&cache);vdc_feedback_match_peer_init(&peer);
        assert(vdc_feedback_match_cache_bind(&cache,71,hz));
        assert(vdc_feedback_model_cache_put(&cache,0,1,2,alo,ahi,0));
        sample_t s=sample(0,a);s.tick_hz=hz;
        assert(vdc_feedback_model_update(&cache,&peer,&s,aw,11)==VDC_FEEDBACK_MATCH_BASELINED);
        assert(vdc_feedback_model_cache_put(&cache,1,2,4,blo,bhi,100));s=sample(1,b);s.tick_hz=hz;
        unsigned result=vdc_feedback_model_update(&cache,&peer,&s,bw,12);
        snapshot_t out={0};unsigned found=vdc_feedback_match_peer_snapshot(&peer,&out);
        printf("%u %u %"PRId64" %"PRId64" %u %u %u %u %u\n",result,found,out.raw_ppb_lo,out.raw_ppb_hi,
            out.reserved,out.pairs[0].rx_width_ns,out.pairs[1].rx_width_ns,
            out.pairs[0].source_model_token,out.pairs[1].source_model_token);
    }
}

static void math_cases(void)
{
    uint32_t hz;
    uint64_t a, b, lo_a, hi_a, lo_b, hi_b;
    while (scanf("%" SCNu32 " %" SCNu64 " %" SCNu64 " %" SCNu64
        " %" SCNu64 " %" SCNu64 " %" SCNu64,
        &hz, &a, &b, &lo_a, &hi_a, &lo_b, &hi_b) == 7) {
        cache_t cache; peer_t peer;
        vdc_feedback_match_cache_init(&cache);
        vdc_feedback_match_peer_init(&peer);
        assert(vdc_feedback_match_cache_bind(&cache, 71u, hz));
        assert(vdc_feedback_match_cache_put(&cache, 0u, 123u, 2u, lo_a, hi_a));
        sample_t s = sample(0u, a); s.tick_hz = hz;
        assert(vdc_feedback_match_update(&cache, &peer, &s) == VDC_FEEDBACK_MATCH_BASELINED);
        assert(vdc_feedback_match_cache_put(&cache, 1u, 124u, 4u, lo_b, hi_b));
        s.measurement_sequence = 1u; s.rx_elapsed_cycles = b;
        const vdc_feedback_match_result_t result = vdc_feedback_match_update(&cache, &peer, &s);
        snapshot_t out = {0};
        const bool found = vdc_feedback_match_peer_snapshot(&peer, &out);
        printf("%u %u %" PRId64 " %" PRId64 " %" PRIu32 " %" PRIu32 "\n",
            (unsigned)result, (unsigned)found, out.raw_ppb_lo, out.raw_ppb_hi,
            out.pairs[0].measurement_sequence, out.pairs[1].measurement_sequence);
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "math") == 0) { math_cases(); return 0; }
    if (strcmp(argv[1], "model_math") == 0) { model_math_cases(); return 0; }
    if (strcmp(argv[1], "lifecycle") == 0) test_lifecycle();
    else if (strcmp(argv[1], "sparse") == 0) test_sparse();
    else if (strcmp(argv[1], "generation") == 0) test_generation();
    else if (strcmp(argv[1], "source") == 0) test_source();
    else if (strcmp(argv[1], "retention") == 0) test_retention();
    else if (strcmp(argv[1], "multiple") == 0) test_multiple();
    else if (strcmp(argv[1], "arguments") == 0) test_arguments();
    else if (strcmp(argv[1], "sequence") == 0) test_sequence();
    else if (strcmp(argv[1], "model_cache") == 0) test_model_cache();
    else if (strcmp(argv[1], "model_state") == 0) test_model_state();
    else if (strcmp(argv[1], "model_lifetime") == 0) test_model_lifetime();
    else assert(!"unknown scenario");
    puts("feedback match: passed");
    return 0;
}
'''
