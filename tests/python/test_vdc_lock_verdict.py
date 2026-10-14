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

static vdc_domain_snapshot_t snapshot;
static vdc_dpll_manager_sync_io_observer_status_t observer;
static bool snapshot_ok = true;

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
    snapshot_ok = true;
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

    /* C2: no input path at all. */
    healthy();
    snapshot.quality.accepted_sample_count = 0u;
    observer.enabled = 0u;
    expect("C2", VDC_DPLL_LOCK_VERDICT_OBSERVER_DISABLED, 0u, 0u, 0u, 0u);

    /* C3/C4: the sync-IO path is the only input and its dictionary is unbound,
     * first empty and then bound to a different schedule profile. */
    healthy();
    snapshot.quality.accepted_sample_count = 0u;
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
    expect("C6", VDC_DPLL_LOCK_VERDICT_GATE_REJECTED, 0u, 0u, 0u, 0u);

    /* C6b: with no accepted sample at all the count rung fires first, whatever
     * the gate code says - the ladder order is part of the contract. */
    healthy();
    snapshot.quality.accepted_sample_count = 0u;
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

    /* C10: publication also needs fine quality, health, freshness and a passed
     * gate; any single one withheld is enough. */
    healthy();
    snapshot.quality.lock_quality_tier = VDC_DOMAIN_LOCK_QUALITY_COARSE_10US;
    expect("C10a", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 0u, 0u);
    healthy();
    snapshot.quality.health_state = VDC_DOMAIN_HEALTH_DEGRADED;
    expect("C10b", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 0u, 0u);
    healthy();
    snapshot.quality.last_sample_age_us = 11u;
    expect("C10c", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 0u, 0u);
    healthy();
    snapshot.gate.passed = 0u;
    expect("C10d", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 0u, 0u);

    /* C11: debug continuation records the observation without promoting it. */
    healthy();
    snapshot.dpll.debug_continue_enabled = 1u;
    expect("C11", VDC_DPLL_LOCK_VERDICT_READY, 1u, 1u, 0u, 0u);

    return 0;
}
'''


def _build(directory):
    source = SOURCE.read_text(encoding="utf-8")
    definitions = (
        definition(source, "vdc_dpll_manager_timestamp_dpll_eligible") + "\n"
        + definition(source, "vdc_dpll_manager_lock_verdict") + "\n")
    return compile_grid(directory, "vdc-lock-verdict", HEAD + definitions + MAIN)


def test_qualified_lock_verdict(tmp_path):
    executable = _build(tmp_path)
    result = subprocess.run([str(executable)], capture_output=True, text=True,
                            timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
