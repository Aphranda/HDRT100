"""One definition of the qualified-lock verdict, separating readiness from
publication.

The verdict is driven through the real function bodies extracted from the
manager, so the SCPI page and any firmware consumer cannot drift from the ladder
that decides them. Cases cover every reason rung, the readiness/publication
split, and the provisional session that can be locked and still never publish.
"""
import subprocess

from test_trigger_grid_preview import ROOT, compile_grid
from test_vdc_publication_generation import definition

SOURCE = ROOT / "components/vdc_dpll_manager/src/vdc_dpll_manager.c"

HEAD = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* The manager places its lock-free accessors and this verdict in RAM on device;
 * on the host the wrapper is the identity. */
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) name
#include "vdc_domain.h"
#include "vdc_dpll_manager.h"
#include "vdc_priority_follow.h"
#include "vdc_priority_match.h"

static vdc_domain_snapshot_t snapshot;
static vdc_dpll_manager_sync_io_observer_status_t observer;
static bool snapshot_ok = true;
static vdc_dpll_manager_dpll_role_status_t role_status;
/* The follower's evidence is the Core1 lock word derived from the matcher's
 * residuals (VDC-LOCKPUB-001), read against the reader's own clock. */
static vdc_priority_lock_word_t lock_word;
static bool lock_ok = true;
static uint32_t match_generation;
static uint64_t now_ns_value;

void vdc_dpll_manager_get_dpll_role_status(
    vdc_dpll_manager_dpll_role_status_t *out)
{
    *out = role_status;
}

bool vdc_dpll_manager_get_priority_lock(vdc_priority_lock_word_t *out)
{
    if (!lock_ok) {
        return false;
    }
    *out = lock_word;
    return true;
}

uint32_t vdc_dpll_manager_priority_match_generation(void)
{
    return match_generation;
}

static uint64_t vdc_dpll_manager_now_ns(void)
{
    return now_ns_value;
}

/* The owner's committed declaration (s_published_lock in firmware). The stub,
 * defined after the real rule below, returns what Core1 commits after serving
 * these same inputs; declaration_ok=false models a failed guarded read. */
static bool declaration_ok = true;

bool vdc_dpll_manager_get_snapshot(vdc_domain_snapshot_t *out)
{
    if (!snapshot_ok) {
        return false;
    }
    *out = snapshot;
    return true;
}

void vdc_dpll_manager_get_sync_io_observer_status(
    vdc_dpll_manager_sync_io_observer_status_t *out)
{
    *out = observer;
}

/* The nominal qualified session: locked, healthy, fine, fresh, gate passed and
 * NOT provisional. Every case below perturbs exactly one input. */
static void healthy(void)
{
    memset(&snapshot, 0, sizeof(snapshot));
    memset(&observer, 0, sizeof(observer));
    memset(&role_status, 0, sizeof(role_status));
    memset(&lock_word, 0, sizeof(lock_word));
    snapshot_ok = true;
    lock_ok = true;
    declaration_ok = true;
    match_generation = 0u;
    now_ns_value = 0u;
    /* Default role is MASTER: the cases below exercise the historical PI
     * ladder, and the follower evidence must not leak into it. */
    role_status.mode = VDC_DPLL_CONTROL_MODE_MASTER;
    snapshot.quality.valid = 1u;
    snapshot.quality.accepted_sample_count = 32u;
    snapshot.quality.last_reject_code = VDC_DOMAIN_GATE_PASS;
    snapshot.quality.last_timestamp_source =
        VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    snapshot.quality.last_timestamp_resolution_ns = 4u;
    snapshot.quality.last_timestamp_flags =
        VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    snapshot.quality.lock_state = VDC_DOMAIN_LOCK_LOCKED;
    snapshot.quality.health_state = VDC_DOMAIN_HEALTH_HEALTHY;
    snapshot.quality.lock_quality_tier = VDC_DOMAIN_LOCK_QUALITY_FINE_100NS;
    snapshot.quality.last_sample_time_ns = 1000u;
    snapshot.quality.freshness_limit_us = 10u;
    snapshot.quality.last_sample_age_us = 1u;
    snapshot.gate.passed = 1u;
    snapshot.dpll.state = VDC_DOMAIN_LOCK_LOCKED;
    /* A live, locked quality row: the verdict's one-bit statement and the
     * published vector bit share this predicate. */
    snapshot.quality.lock_state = VDC_DOMAIN_LOCK_LOCKED;
    snapshot.schedule.schedule_crc32 = 0xABCDu;
}

static void expect(const char *label, uint32_t want_reason,
                   uint32_t want_input_ready, uint32_t want_locked,
                   uint32_t want_provisional, uint32_t want_qualified)
{
    vdc_dpll_lock_verdict_t verdict;
    assert(vdc_dpll_manager_lock_verdict(&verdict));
    fprintf(stderr, "case %s: reason %u input_ready %u locked %u provisional %u qualified %u\n",
            label, verdict.reason, verdict.input_ready, verdict.locked,
            verdict.provisional, verdict.published_qualified);
    assert(verdict.reason == want_reason);
    assert(verdict.input_ready == want_input_ready);
    assert(verdict.locked == want_locked);
    assert(verdict.provisional == want_provisional);
    assert(verdict.published_qualified == want_qualified);
}

/* The published lock state is one bit, and the role decides the evidence:
 * a FOLLOWER is locked while its internal observation (the residual centre
 * against NO1's published event time) is in band and fresh (VDC-LOCKPUB-001). */
static void expect_follower(const char *label, uint32_t want_reason,
                            uint32_t want_input_ready, uint32_t want_locked)
{
    vdc_dpll_lock_verdict_t verdict;
    assert(vdc_dpll_manager_lock_verdict(&verdict));
    if (verdict.reason != want_reason || verdict.locked != want_locked ||
        verdict.input_ready != want_input_ready) {
        fprintf(stderr, "%s: reason %u/%u input_ready %u/%u locked %u/%u\n",
                label, verdict.reason, want_reason, verdict.input_ready,
                want_input_ready, verdict.locked, want_locked);
    }
    assert(verdict.reason == want_reason);
    assert(verdict.input_ready == want_input_ready);
    assert(verdict.locked == want_locked);
    assert(verdict.role == role_status.mode);
}

/* Follower evidence: an armed observation whose Core1 word is locked and whose
 * last match is 1 ms old. */
static void follower_healthy(void)
{
    healthy();
    role_status.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    /* The follower's own ladder never locks (single PI on NO1). */
    snapshot.dpll.state = VDC_DOMAIN_LOCK_CHECKING;
    match_generation = 7u;
    lock_word.locked = 1u;
    lock_word.in_band_run = 12u;
    lock_word.center_ns = -9;
    lock_word.width_ns = 199u;
    lock_word.last_match_ns = UINT64_C(5000000000);
    now_ns_value = lock_word.last_match_ns + UINT64_C(1000000);
}

static void observe(vdc_priority_lock_word_t *word, int64_t lo, int64_t hi,
                    uint64_t now_ns)
{
    vdc_priority_lock_observe(word, lo, hi, word->event_sequence + 1u, now_ns);
}
'''

MAIN = r'''
int main(void)
{
    assert(!vdc_dpll_manager_lock_verdict(NULL));

    /* C1: no snapshot at all, then a snapshot that is present but not valid. */
    healthy();
    snapshot_ok = false;
    expect("C1a", VDC_DPLL_LOCK_VERDICT_SNAPSHOT_UNAVAILABLE, 0u, 0u, 0u, 0u);
    healthy();
    snapshot.quality.valid = 0u;
    expect("C1b", VDC_DPLL_LOCK_VERDICT_SNAPSHOT_UNAVAILABLE, 0u, 0u, 0u, 0u);

    /* C2: no input path at all. `locked` is the owner's declaration, so the
     * ladder cases keep their quality row consistent with the missing input: no
     * sample was ever accepted, hence no sample time either. */
    healthy();
    snapshot.quality.accepted_sample_count = 0u;
    snapshot.quality.last_sample_time_ns = 0u;
    observer.enabled = 0u;
    expect("C2", VDC_DPLL_LOCK_VERDICT_OBSERVER_DISABLED, 0u, 0u, 0u, 0u);

    /* C3/C4: the sync-IO path is the only input and its dictionary is unbound,
     * first empty and then bound to a different schedule profile. */
    healthy();
    snapshot.quality.accepted_sample_count = 0u;
    snapshot.quality.last_sample_time_ns = 0u;
    observer.enabled = 1u;
    observer.dictionary_entry_count = 0u;
    expect("C3", VDC_DPLL_LOCK_VERDICT_DICTIONARY_EMPTY, 0u, 0u, 0u, 0u);
    observer.dictionary_entry_count = 4u;
    observer.dictionary_crc32 = 0x1234u;
    observer.dictionary_profile_crc32 = 0xFFFFu;
    expect("C4", VDC_DPLL_LOCK_VERDICT_DICTIONARY_EMPTY, 0u, 0u, 0u, 0u);

    /* C5: bound, but the timestamp is not DPLL eligible (software source). */
    observer.dictionary_profile_crc32 = snapshot.schedule.schedule_crc32;
    observer.last_timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US;
    observer.last_timestamp_resolution_ns = 4u;
    observer.last_timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    expect("C5", VDC_DPLL_LOCK_VERDICT_TIMESTAMP_NOT_ELIGIBLE, 0u, 0u, 0u, 0u);

    /* C6: the accepted-sample rung comes BEFORE the gate rung, so the gate case
     * needs an accepted sample whose gate code was refused. */
    healthy();
    snapshot.quality.last_reject_code = 9u;
    snapshot.gate.passed = 0u;
    expect("C6", VDC_DPLL_LOCK_VERDICT_GATE_REJECTED, 0u, 0u, 0u, 0u);

    /* C6b: with no accepted sample at all the count rung fires first, whatever
     * the gate code says - the ladder order is part of the contract. */
    healthy();
    snapshot.quality.accepted_sample_count = 0u;
    snapshot.quality.last_sample_time_ns = 0u;
    snapshot.quality.last_reject_code = 9u;
    observer.enabled = 1u;
    observer.dictionary_entry_count = 4u;
    observer.dictionary_crc32 = 0x1234u;
    observer.dictionary_profile_crc32 = snapshot.schedule.schedule_crc32;
    observer.last_timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    observer.last_timestamp_resolution_ns = 4u;
    observer.last_timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    observer.last_gate_reject_code = 9u;
    expect("C6b", VDC_DPLL_LOCK_VERDICT_NO_ACCEPTED_SAMPLE, 0u, 0u, 0u, 0u);

    /* C7: every input path ready while the DPLL is not locked. This is the case
     * the readiness flag alone cannot separate from a usable lock. */
    healthy();
    snapshot.dpll.state = VDC_DOMAIN_LOCK_PHASE_LOCK;
    expect("C7", VDC_DPLL_LOCK_VERDICT_NOT_LOCKED, 1u, 0u, 0u, 0u);

    /* C8: locked and qualified - the only shape a formal consumer may use. */
    healthy();
    expect("C8", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 0u, 1u);

    /* C9: a provisional (diagnostic-only path delay) session can be locked and
     * still never publish as qualified. This is the bench situation. */
    healthy();
    snapshot.path_delay.flags |= VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY;
    expect("C9", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 1u, 0u);

    /* C10: the tier and health are NOT part of the one-bit lock state (the tier
     * is the master's internal acquisition process), but a dead sample window or
     * a failed gate IS: the DPLL is dynamic, so those report UNLOCK. The
     * published_qualified flag is what withholds formal qualification. */
    healthy();
    snapshot.quality.lock_quality_tier = VDC_DOMAIN_LOCK_QUALITY_COARSE_10US;
    expect("C10a", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 0u, 0u);
    healthy();
    snapshot.quality.health_state = VDC_DOMAIN_HEALTH_DEGRADED;
    expect("C10b", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 0u, 0u);
    healthy();
    snapshot.quality.last_sample_age_us = 11u;
    expect("C10c", VDC_DPLL_LOCK_VERDICT_READY, 1u, 0u, 0u, 0u);
    healthy();
    snapshot.gate.passed = 0u;
    expect("C10d", VDC_DPLL_LOCK_VERDICT_READY, 1u, 0u, 0u, 0u);

    /* C11: debug continuation records the observation without promoting it, and
     * the PI/DCO is not driving the loop, so the one-bit lock state is 0 here -
     * the same thing the vector bit's own "lock_debug" case asserts. */ 
    healthy();
    snapshot.dpll.debug_continue_enabled = 1u;
    expect("C11", VDC_DPLL_LOCK_VERDICT_READY, 1u, 0u, 0u, 0u);

    /* D: the FOLLOWER role reads the Core1 lock word only; its own CHECKING
     * ladder and the gate/age of the master half are irrelevant. */
    follower_healthy();
    expect_follower("D1", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u);
    follower_healthy();
    match_generation = 0u;
    expect_follower("D2", VDC_DPLL_LOCK_VERDICT_FOLLOW_DISABLED, 0u, 0u);
    follower_healthy();
    lock_word.last_match_ns = 0u;
    expect_follower("D3", VDC_DPLL_LOCK_VERDICT_FOLLOW_DISABLED, 0u, 0u);
    follower_healthy();
    lock_ok = false;
    expect_follower("D4", VDC_DPLL_LOCK_VERDICT_FOLLOW_DISABLED, 0u, 0u);
    /* D5: the DPLL is dynamic - a lock whose last match is older than the
     * window is reported lost, even though the Core1 word still says locked
     * (Core1 stopped serving). Exactly at the window it is still live. */
    follower_healthy();
    now_ns_value = lock_word.last_match_ns + VDC_PRIORITY_LOCK_STALE_NS;
    expect_follower("D5a", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u);
    now_ns_value += 1u;
    expect_follower("D5b", VDC_DPLL_LOCK_VERDICT_FOLLOW_STALE, 0u, 0u);
    follower_healthy();
    now_ns_value = UINT64_MAX;
    expect_follower("D5c", VDC_DPLL_LOCK_VERDICT_FOLLOW_STALE, 0u, 0u);
    follower_healthy();
    now_ns_value = lock_word.last_match_ns - 1u;
    expect_follower("D5d", VDC_DPLL_LOCK_VERDICT_FOLLOW_STALE, 0u, 0u);
    /* D6: observing and fresh, but the centre is not (yet / any longer) in band. */
    follower_healthy();
    lock_word.locked = 0u;
    expect_follower("D6", VDC_DPLL_LOCK_VERDICT_NOT_LOCKED, 1u, 0u);
    /* D8: the word is in band but no committed declaration can be read (or it
     * was made for another role): this reader never re-derives the lock. */
    follower_healthy();
    declaration_ok = false;
    expect_follower("D8", VDC_DPLL_LOCK_VERDICT_NOT_LOCKED, 1u, 0u);
    /* D9: a MASTER whose declaration cannot be read is not locked either. */
    healthy();
    declaration_ok = false;
    expect("D9", VDC_DPLL_LOCK_VERDICT_READY, 1u, 0u, 0u, 0u);
    /* D7: the MASTER half never consults the follower word. */
    healthy();
    match_generation = 7u;
    lock_word.locked = 1u;
    lock_word.last_match_ns = 1u;
    expect("D7", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 0u, 1u);

    /* E: the Core1 reduction itself. */
    vdc_priority_lock_word_t w;
    uint64_t t = UINT64_C(1000000000);
    memset(&w, 0, sizeof(w));
    /* E1: the bench interval [-108, +91] has centre -9 and width 199. */
    observe(&w, -108, 91, t);
    assert(w.center_ns == -9 && w.width_ns == 199u && w.in_band_run == 1u);
    assert(w.last_match_ns == t && !w.locked);
    /* E2: ENTER_COUNT consecutive in-band observations lock. */
    for (uint32_t i = 1u; i < VDC_PRIORITY_LOCK_ENTER_COUNT; ++i) {
        assert(!w.locked);
        observe(&w, -108, 91, t += 1000000u);
    }
    assert(w.locked && vdc_priority_lock_is_live(&w, t));
    /* E3: between the bands the state holds and both runs break. */
    observe(&w, 50, 250, t += 1000000u);           /* centre 150 */
    assert(w.locked && w.in_band_run == 0u && w.out_band_run == 0u);
    /* E4: EXIT_COUNT consecutive out-of-band observations unlock; a mid-band
     * sample in between restarts the count. */
    observe(&w, 200, 400, t += 1000000u);          /* centre 300 */
    observe(&w, 50, 250, t += 1000000u);
    for (uint32_t i = 1u; i < VDC_PRIORITY_LOCK_EXIT_COUNT; ++i) {
        observe(&w, -400, -200, t += 1000000u);    /* centre -300 */
        assert(w.locked);
    }
    observe(&w, -400, -200, t += 1000000u);
    assert(!w.locked && w.out_band_run == VDC_PRIORITY_LOCK_EXIT_COUNT);
    /* E5: re-entry needs the full run again. */
    for (uint32_t i = 1u; i < VDC_PRIORITY_LOCK_ENTER_COUNT; ++i) {
        observe(&w, -10, 10, t += 1000000u);
        assert(!w.locked);
    }
    observe(&w, -10, 10, t += 1000000u);
    assert(w.locked);
    /* E6: a centred but too-wide interval is a broken measurement: out of band. */
    for (uint32_t i = 0u; i < VDC_PRIORITY_LOCK_EXIT_COUNT; ++i) {
        observe(&w, -(int64_t)VDC_PRIORITY_LOCK_WIDTH_LIMIT_NS,
                (int64_t)VDC_PRIORITY_LOCK_WIDTH_LIMIT_NS, t += 1000000u);
    }
    assert(!w.locked && w.center_ns == 0);
    /* E7: a gap longer than the window restarts acquisition and is not live. */
    for (uint32_t i = 0u; i < VDC_PRIORITY_LOCK_ENTER_COUNT; ++i) {
        observe(&w, -10, 10, t += 1000000u);
    }
    assert(w.locked);
    assert(!vdc_priority_lock_is_live(&w, t + VDC_PRIORITY_LOCK_STALE_NS + 1u));
    observe(&w, -10, 10, t += VDC_PRIORITY_LOCK_STALE_NS + 1u);
    assert(!w.locked && w.in_band_run == 1u);
    /* E8: an inverted interval or an unreadable clock fails closed. */
    for (uint32_t i = 1u; i < VDC_PRIORITY_LOCK_ENTER_COUNT; ++i) {
        observe(&w, -10, 10, t += 1000000u);
    }
    assert(w.locked);
    observe(&w, 10, -10, t += 1000000u);
    assert(!w.locked && w.last_match_ns == 0u && w.in_band_run == 0u);
    observe(&w, -10, 10, UINT64_MAX);
    assert(w.last_match_ns == 0u && !vdc_priority_lock_is_live(&w, t));
    /* E9: extremes neither overflow nor pass as in band. */
    memset(&w, 0, sizeof(w));
    observe(&w, INT64_MIN, INT64_MAX, t);
    assert(w.center_ns == -1 && w.width_ns == UINT32_MAX && w.out_band_run == 1u);
    observe(&w, INT64_MAX - 1, INT64_MAX, t += 1u);
    assert(w.center_ns == INT32_MAX && w.width_ns == 1u && w.out_band_run == 2u);

    return 0;
}
'''


DECLARATION_STUB = r'''
bool vdc_dpll_manager_get_lock_declaration(vdc_dpll_lock_declaration_t *out)
{
    if (!declaration_ok) {
        return false;
    }
    *out = vdc_dpll_manager_lock_declaration(role_status.mode, match_generation,
                                             &lock_word, &snapshot, snapshot_ok);
    return true;
}
'''


def _build(directory):
    source = SOURCE.read_text(encoding="utf-8")
    definitions = (
        definition(source, "vdc_dpll_manager_timestamp_dpll_eligible") + "\n"
        # The owner's real declaration rule, then the guarded-read stub that
        # returns what Core1 would have committed for these inputs.
        + definition(source, "vdc_dpll_manager_lock_declaration") + "\n"
        + DECLARATION_STUB
        # The follower reason reads the Core1 lock word (stubbed above); the
        # reduction itself is the header's vdc_priority_lock_observe().
        + definition(source, "vdc_dpll_manager_lock_verdict") + "\n")
    return compile_grid(directory, "vdc-lock-verdict", HEAD + definitions + MAIN)


def test_qualified_lock_verdict(tmp_path):
    executable = _build(tmp_path)
    result = subprocess.run([str(executable)], capture_output=True, text=True,
                            timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
