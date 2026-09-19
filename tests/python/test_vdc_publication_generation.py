"""Real Domain commits through the production publication and vector consumers.

Only the board clock, status-export sink and vector backing addresses are
stubbed. Deterministic guard-read interference models cross-core publication;
this is a host freshness regression, not hardware timing or lock evidence.
Set VDC_PUBLICATION_BASELINE_REF to a git revision to reproduce the old bug.
"""
import os
import re
import shutil
import subprocess

import pytest

from test_vdc_command_owner import ROOT, MANAGER, REFMEM
from test_vdc_command_ingress import ingress_definition
from test_vdc_follower_rate_boundary import HARNESS as DOMAIN_HARNESS
from test_vdc_priority_follow import domain_sources


def definition(source, name):
    match = re.search(
        rf"(?m)^(?:static\s+)?(?:bool|void|uint32_t)\s+"
        rf"(?:__attribute__\(\([^\n]*\)\)\s+)?"
        rf"(?:[A-Z_]+\(\s*)?{name}\s*\)?\s*\([^;{{}}]*\)\s*\{{", source)
    assert match, name
    cursor, depth = match.end(), 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor]


@pytest.fixture(scope="module")
def publication_exe(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-publication")
    baseline = os.environ.get("VDC_PUBLICATION_BASELINE_REF")

    def read(path):
        if not baseline:
            return path.read_text(encoding="utf-8")
        return subprocess.check_output(
            ["git", "show", f"{baseline}:{path.relative_to(ROOT).as_posix()}"],
            cwd=ROOT, text=True, encoding="utf-8")

    manager, refmem = read(MANAGER), read(REFMEM)
    runtime_type = re.search(
        r"typedef struct \{[^{}]*\} vdc_dpll_manager_runtime_snapshot_t;", manager)
    assert runtime_type
    have_revision = "publication_revision" in runtime_type.group()
    source = PRELUDE.replace("HAVE_REVISION_VALUE", str(int(have_revision)))
    source += runtime_type.group() + "\n"
    source += definition(manager, "vdc_dpll_manager_publish_snapshot") + "\n"
    source += INTERFERENCE
    for name in ("vdc_dpll_manager_get_runtime_snapshot",
                 "vdc_dpll_manager_get_vector_snapshot",
                 "vdc_dpll_manager_get_refmem_snapshot",
                 "vdc_dpll_manager_published_update_seq",
                 "vdc_dpll_manager_refresh_dco_consumer_status_core0"):
        source += definition(manager, name) + "\n"
    source += "\n#undef __atomic_load_n\n"
    for name in ("distributed_refmem_copy_to_volatile",
                 "distributed_refmem_next_publish_sequence",
                 "distributed_refmem_begin_vector_write",
                 "distributed_refmem_publish_vdc_vector_payload",
                 "distributed_refmem_publish_dpll_vector_payload",
                 "distributed_refmem_vector_hardware_evidence_valid",
                 "distributed_refmem_vector_flags",
                 "distributed_refmem_fill_vdc_vector_payload",
                 "distributed_refmem_fill_dpll_vector_payload",
                 "distributed_refmem_publish_runtime_vector_if_pending"):
        source += definition(refmem, name) + "\n"
    source += ingress_definition(DOMAIN_HARNESS, "fixture") + SCENARIOS
    c_file = directory / "publication.c"
    c_file.write_text(source, encoding="utf-8")
    includes = [directory, ROOT / "tests/unit/host_stubs", ROOT / "config",
                ROOT / "boards/rp2350_trig/inc", *sorted((ROOT / "components").glob("*/inc"))]
    exe = directory / ("publication.exe" if os.name == "nt" else "publication")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    result = subprocess.run([
        compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        *[f"-I{p}" for p in includes], str(c_file), *map(str, domain_sources()),
        str(ROOT / "components/distributed_refmem/src/refmem_vector_table.c"),
        "-o", str(exe)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.mark.parametrize("scenario", [
    "rate_core", "rate_vector", "phase_core", "phase_vector", "quality_ready",
    "odd", "contention", "invalid", "wrap", "initial_zero", "stale_hint",
    "core_copy_race", "unchanged",
    "lock_healthy", "lock_boundary", "lock_expired", "lock_gate", "lock_invalid",
    "lock_not_fine", "lock_quality_state", "lock_health_degraded", "lock_no_limit",
    "lock_no_sample", "lock_provisional", "lock_debug", "lock_recovery",
    "lock_healthy_but_expired", "lock_healthy_but_gate_rejected",
])
def test_publication_consumers(publication_exe, scenario):
    result = subprocess.run([str(publication_exe), scenario], capture_output=True,
                            text=True, timeout=30)
    (publication_exe.parent / f"{scenario}.log").write_text(
        result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


PRELUDE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "refmem_vector_table.h"
/* Report negative controls without opening a Windows CRT abort dialog. */
#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(1); } } while (0)
#define HAVE_REVISION HAVE_REVISION_VALUE
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) name
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) name
static vdc_domain_snapshot_t s_published_snapshot, replacement;
static uint32_t s_published_snapshot_guard, s_published_dpll_update_seq;
static bool s_published_snapshot_valid;
static vdc_dpll_manager_dpll_status_t s_dpll_status;
static vdc_dpll_manager_dco_consumer_status_t s_dco_consumer_status;
static bool s_dpll_ready = true;
#if HAVE_REVISION
static uint32_t s_dpll_consumed_publication_revision;
static bool s_dpll_have_consumed_publication;
#else
static uint32_t s_dpll_consumed_update_seq;
#endif
static uint32_t board_uptime_ms(void) { return 100u; }
static void vdc_dpll_manager_publish_dpll_status(void) {}
static refmem_vdc_vector_region_t vdc_region;
static refmem_dpll_vector_region_t dpll_region;
static uint32_t s_vdc_vector_source_update_seq = UINT32_MAX;
static uint32_t s_dpll_vector_source_update_seq = UINT32_MAX;
static uint32_t s_vdc_vector_publish_sequence, s_dpll_vector_publish_sequence;
static uint32_t s_next_runtime_vector;
static refmem_vdc_vector_region_t *distributed_refmem_vdc_vector_region(void)
{ return &vdc_region; }
static refmem_dpll_vector_region_t *distributed_refmem_dpll_vector_region(void)
{ return &dpll_region; }
'''

INTERFERENCE = r'''
static unsigned guard_reads, replace_at, contend;
static uint32_t snapshot_load(const uint32_t *value, int order)
{
    (void)order;
    if (value == &s_published_snapshot_guard) {
        ++guard_reads;
        if (replace_at && guard_reads == replace_at)
            vdc_dpll_manager_publish_snapshot(&replacement);
        if (contend) s_published_snapshot_guard += 2u;
    }
    return *value;
}
#define __atomic_load_n snapshot_load
'''

SCENARIOS = r'''
static vdc_domain_context_t domain;
static uint32_t evidence_seq;
static vdc_clock_model_t original_clock;

static void publish(void)
{
    vdc_domain_snapshot_t snapshot;
    assert(vdc_domain_get_snapshot(&domain, &snapshot));
    vdc_dpll_manager_publish_snapshot(&snapshot);
}

static uint32_t hint(void)
{
    vdc_dpll_manager_refmem_snapshot_t snapshot;
    assert(vdc_dpll_manager_get_refmem_snapshot(&snapshot));
#if HAVE_REVISION
    return snapshot.publication_revision;
#else
    return snapshot.dpll_update_seq;
#endif
}

static void vectors(void)
{
    uint32_t revision = hint();
    distributed_refmem_publish_runtime_vector_if_pending(revision);
    distributed_refmem_publish_runtime_vector_if_pending(revision);
}

static void check_vectors(void)
{
    assert(refmem_vdc_vector_payload_validate(&vdc_region.payload));
    assert(refmem_dpll_vector_payload_validate(&dpll_region.payload));
    assert(vdc_region.payload.stable_sequence == vdc_region.seqlock);
    assert(dpll_region.payload.stable_sequence == dpll_region.seqlock);
    assert(vdc_region.payload.source_update_seq == evidence_seq);
    assert(dpll_region.payload.source_update_seq == evidence_seq);
    assert(vdc_region.payload.dpll_update_seq == evidence_seq);
    assert(dpll_region.payload.dpll_update_seq == evidence_seq);
    assert(vdc_dpll_manager_published_update_seq() == evidence_seq);
    assert(dpll_region.payload.dco_update_seq == domain.dco.dco_update_seq);
    assert(dpll_region.payload.dco_base_local_tick64 == domain.dco.base_local_tick64);
    assert(dpll_region.payload.dco_base_vdc_time64_ns == domain.dco.base_vdc_time64_ns);
    assert(dpll_region.payload.dco_period_adjust_ppb == domain.dco.period_adjust_ppb);
    assert(vdc_region.payload.clock_base_local_tick64 == original_clock.base_local_tick64);
    assert(vdc_region.payload.clock_base_vdc_time64_ns == original_clock.base_vdc_time64_ns);
    assert(vdc_region.payload.clock_model_seq == original_clock.model_seq);
    assert(!(vdc_region.payload.flags & REFMEM_VECTOR_FLAG_LOCKED));
    assert(!(dpll_region.payload.flags & REFMEM_VECTOR_FLAG_LOCKED));
}

static void check_core(void)
{
    assert(s_dco_consumer_status.valid);
    assert(s_dco_consumer_status.last_dco_update_seq == domain.dco.dco_update_seq);
    assert(s_dco_consumer_status.base_local_tick64 == domain.dco.base_local_tick64);
    assert(s_dco_consumer_status.base_vdc_time64_ns == domain.dco.base_vdc_time64_ns);
    assert(s_dco_consumer_status.period_adjust_ppb == domain.dco.period_adjust_ppb);
    assert(s_dco_consumer_status.phase_offset_ns == domain.dco.phase_offset_ns);
    assert(s_dpll_status.update_seq == evidence_seq);
}

static void commit(bool phase)
{
    const vdc_dpll_local_rate_delta_t rate = {
        .source_slot_id = domain.control.profile.follow_master_slot_id,
        .target_slot_id = domain.schedule.local_slot_id,
        .expected_control_generation = domain.control.profile.generation,
        .schedule_crc32 = domain.schedule.schedule_crc32,
        .servo_profile_crc32 = domain.servo.servo_profile_crc32,
        .clock_epoch_id = domain.clock.epoch_id,
        .clock_run_id = domain.clock.run_id,
        .expected_dco_update_seq = domain.dco.dco_update_seq,
        .delta_rate_ppb = -123,
    };
    uint32_t old_dco_seq = domain.dco.dco_update_seq;
    vdc_quality_table_t old_quality = domain.quality;
    uint32_t old_lock = domain.dco.lock_state;
    if (phase) {
        const vdc_dpll_local_phase_delta_t command = {
            .source_slot_id = rate.source_slot_id, .target_slot_id = rate.target_slot_id,
            .expected_control_generation = rate.expected_control_generation,
            .schedule_crc32 = rate.schedule_crc32, .servo_profile_crc32 = rate.servo_profile_crc32,
            .clock_epoch_id = rate.clock_epoch_id, .clock_run_id = rate.clock_run_id,
            .expected_dco_update_seq = rate.expected_dco_update_seq, .delta_phase_ns = 73,
        };
        assert(vdc_domain_apply_local_follow_phase_delta(&domain, &command, 3000000000ull));
    } else {
        assert(vdc_domain_apply_local_follow_rate_delta(&domain, &rate, 3000000000ull));
    }
    assert(domain.dco.dco_update_seq == old_dco_seq + 1u);
    assert(domain.dpll.update_seq == evidence_seq);
    assert(domain.dco.lock_state == old_lock);
    assert(memcmp(&old_quality, &domain.quality, sizeof(old_quality)) == 0);
    assert(memcmp(&original_clock, &domain.clock, sizeof(original_clock)) == 0);
}

static void health_qualified_lock_fixture(void)
{
    domain.path_delay.flags &= ~VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY;
    domain.dpll.debug_continue_enabled = 0u;
    domain.dpll.state = VDC_DOMAIN_LOCK_LOCKED;
    domain.dco.lock_state = VDC_DOMAIN_LOCK_LOCKED;
    domain.gate.passed = 1u;
    domain.quality.valid = 1u;
    domain.quality.lock_state = VDC_DOMAIN_LOCK_LOCKED;
    domain.quality.lock_quality_tier = VDC_DOMAIN_LOCK_QUALITY_FINE_100NS;
    domain.quality.freshness_limit_us = 120000u;
    domain.quality.last_sample_time_ns = UINT64_C(3000000000);
    domain.quality.last_sample_age_us = 0u;
    domain.quality.health_state = VDC_DOMAIN_HEALTH_HEALTHY;
    domain.quality.accepted_sample_count = 17u;
    domain.quality.last_timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    domain.quality.last_timestamp_resolution_ns = 4u;
    domain.quality.last_timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
}

static void check_lock_vectors(bool locked, uint32_t expected_publications,
    const vdc_dco_control_t *saved_dco)
{
    assert(refmem_vdc_vector_payload_validate(&vdc_region.payload));
    assert(refmem_dpll_vector_payload_validate(&dpll_region.payload));
    assert(!!(vdc_region.payload.flags & REFMEM_VECTOR_FLAG_LOCKED) == locked);
    assert(!!(dpll_region.payload.flags & REFMEM_VECTOR_FLAG_LOCKED) == locked);
    assert(vdc_region.payload.quality_health_state == domain.quality.health_state);
    assert(dpll_region.payload.quality_health_state == domain.quality.health_state);
    assert(vdc_region.payload.quality_last_sample_age_us == domain.quality.last_sample_age_us);
    assert(dpll_region.payload.quality_last_sample_age_us == domain.quality.last_sample_age_us);
    assert(vdc_region.payload.source_update_seq == evidence_seq);
    assert(dpll_region.payload.source_update_seq == evidence_seq);
    assert(domain.dpll.update_seq == evidence_seq);
    assert(!memcmp(saved_dco, &domain.dco, sizeof(*saved_dco)));
    assert(dpll_region.payload.dco_update_seq == saved_dco->dco_update_seq);
    assert(dpll_region.payload.dco_period_adjust_ppb == saved_dco->period_adjust_ppb);
    assert(s_vdc_vector_publish_sequence == expected_publications);
    assert(s_dpll_vector_publish_sequence == expected_publications);
}

static void check_quality_lock_publication(const char *name)
{
    health_qualified_lock_fixture();
    const vdc_dco_control_t saved_dco = domain.dco;
    publish(); vectors(); check_lock_vectors(true, 1u, &saved_dco);
    if (!strcmp(name, "lock_healthy")) return;
    bool locked = false;
    if (!strcmp(name, "lock_boundary")) {
        assert(vdc_domain_age_quality(&domain.quality, domain.dpll.state, true,
            domain.quality.last_sample_time_ns + UINT64_C(120000000)));
        locked = true;
    } else if (!strcmp(name, "lock_expired") || !strcmp(name, "lock_recovery")) {
        assert(vdc_domain_age_quality(&domain.quality, domain.dpll.state, true,
            domain.quality.last_sample_time_ns + UINT64_C(120001000)));
        assert(domain.quality.health_state == VDC_DOMAIN_HEALTH_DEGRADED);
    } else if (!strcmp(name, "lock_gate")) {
        domain.gate.passed = 0u;
        assert(vdc_domain_age_quality(&domain.quality, domain.dpll.state, false,
            domain.quality.last_sample_time_ns));
    } else if (!strcmp(name, "lock_healthy_but_expired")) {
        domain.quality.last_sample_age_us = domain.quality.freshness_limit_us + 1u;
        assert(domain.quality.health_state == VDC_DOMAIN_HEALTH_HEALTHY);
    } else if (!strcmp(name, "lock_healthy_but_gate_rejected")) {
        domain.gate.passed = 0u;
        assert(domain.quality.health_state == VDC_DOMAIN_HEALTH_HEALTHY);
    } else if (!strcmp(name, "lock_invalid")) domain.quality.valid = 0u;
    else if (!strcmp(name, "lock_not_fine")) domain.quality.lock_quality_tier = VDC_DOMAIN_LOCK_QUALITY_DEBUG_1US;
    else if (!strcmp(name, "lock_quality_state")) domain.quality.lock_state = VDC_DOMAIN_LOCK_FREQ_LOCK;
    else if (!strcmp(name, "lock_health_degraded")) domain.quality.health_state = VDC_DOMAIN_HEALTH_DEGRADED;
    else if (!strcmp(name, "lock_no_limit")) domain.quality.freshness_limit_us = 0u;
    else if (!strcmp(name, "lock_no_sample")) domain.quality.last_sample_time_ns = 0u;
    else if (!strcmp(name, "lock_provisional")) domain.path_delay.flags |= VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY;
    else if (!strcmp(name, "lock_debug")) domain.dpll.debug_continue_enabled = 1u;
    else assert(!"unknown lock publication scenario");
    publish(); vectors(); check_lock_vectors(locked, 2u, &saved_dco);
    if (!strcmp(name, "lock_recovery")) {
        /* A renewed evidence timestamp is aged by the real Domain routine;
         * evidence/model sequence remains fixed to exercise quality-only publication. */
        domain.quality.last_sample_time_ns += UINT64_C(200000000);
        assert(vdc_domain_age_quality(&domain.quality, domain.dpll.state, true,
            domain.quality.last_sample_time_ns + 1000u));
        assert(domain.quality.health_state == VDC_DOMAIN_HEALTH_HEALTHY);
        publish(); vectors(); check_lock_vectors(true, 3u, &saved_dco);
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    fixture(&domain);
    /* Nonzero legacy evidence also permits the old consumer's initial read. */
    domain.dpll.update_seq = 17u;
    evidence_seq = domain.dpll.update_seq;
    original_clock = domain.clock;
    if (!strncmp(argv[1], "lock_", 5u)) {
        check_quality_lock_publication(argv[1]);
        puts("health-qualified lock publication passed"); return 0;
    }
    if (!strcmp(argv[1], "initial_zero"))
        s_published_snapshot_guard = UINT32_MAX - 1u;
    publish();
    vdc_dpll_manager_refresh_dco_consumer_status_core0();
    vectors();
    check_core(); check_vectors();
    const char *name = argv[1];
    if (!strcmp(name, "unchanged") || !strcmp(name, "initial_zero")) {
        uint32_t core_count = s_dco_consumer_status.service_count;
        for (unsigned i = 0; i < 10; ++i) {
            vectors(); vdc_dpll_manager_refresh_dco_consumer_status_core0();
        }
        assert(s_dco_consumer_status.service_count == core_count);
        assert(s_vdc_vector_publish_sequence == 1u && s_dpll_vector_publish_sequence == 1u);
        if (!strcmp(name, "initial_zero")) assert(s_published_snapshot_guard == 0u);
    } else if (!strcmp(name, "quality_ready")) {
        domain.ready = false;
        domain.quality.last_sample_age_us += 123u;
        publish(); vectors();
        assert(dpll_region.payload.ready == 0u);
        assert(vdc_region.payload.quality_last_sample_age_us == domain.quality.last_sample_age_us);
        assert(dpll_region.payload.quality_last_sample_age_us == domain.quality.last_sample_age_us);
        check_vectors();
    } else {
        commit(strstr(name, "phase") != NULL);
        if (!strcmp(name, "wrap")) s_published_snapshot_guard = UINT32_MAX - 1u;
        uint32_t old_hint = hint();
        if (!strcmp(name, "core_copy_race")) {
            /* First publish another unchanged model to open the hint gate. */
            vdc_dpll_manager_publish_snapshot(&s_published_snapshot);
            assert(vdc_domain_get_snapshot(&domain, &replacement));
            guard_reads = 0u; replace_at = 2u;
        } else {
            publish();
        }
        if (!strcmp(name, "odd") || !strcmp(name, "contention") || !strcmp(name, "invalid")) {
            uint32_t revision = hint();
            if (!strcmp(name, "odd")) ++s_published_snapshot_guard;
            else if (!strcmp(name, "invalid")) s_published_snapshot_valid = false;
            else contend = 1u;
            vdc_dpll_manager_refresh_dco_consumer_status_core0();
            distributed_refmem_publish_runtime_vector_if_pending(revision);
            assert(s_dco_consumer_status.last_dco_update_seq != domain.dco.dco_update_seq);
            assert(s_vdc_vector_publish_sequence == 1u && s_dpll_vector_publish_sequence == 1u);
            contend = 0u;
            s_published_snapshot_valid = true;
            if (s_published_snapshot_guard & 1u) ++s_published_snapshot_guard;
        }
        if (!strcmp(name, "stale_hint")) {
            /* One old vector remains pending, then the copy sees a newer model. */
            s_dpll_vector_source_update_seq = UINT32_MAX;
            distributed_refmem_publish_runtime_vector_if_pending(old_hint);
#if HAVE_REVISION
            assert(s_vdc_vector_source_update_seq == s_published_snapshot_guard);
#endif
        }
        vdc_dpll_manager_refresh_dco_consumer_status_core0();
        if (!strstr(name, "vector")) check_core();
        vectors();
        if (!strstr(name, "core")) check_vectors();
        if (!strcmp(name, "wrap")) assert(s_published_snapshot_guard == 0u);
#if HAVE_REVISION
        assert(s_dpll_have_consumed_publication);
        assert(s_dpll_consumed_publication_revision == s_published_snapshot_guard);
        assert(s_vdc_vector_source_update_seq == s_published_snapshot_guard);
        assert(s_dpll_vector_source_update_seq == s_published_snapshot_guard);
#endif
        uint32_t core_count = s_dco_consumer_status.service_count;
        uint32_t vdc_count = s_vdc_vector_publish_sequence;
        uint32_t dpll_count = s_dpll_vector_publish_sequence;
        vdc_dpll_manager_refresh_dco_consumer_status_core0(); vectors();
        assert(s_dco_consumer_status.service_count == core_count);
        assert(s_vdc_vector_publish_sequence == vdc_count);
        assert(s_dpll_vector_publish_sequence == dpll_count);
    }
    puts("publication consumers passed");
    return 0;
}
'''
