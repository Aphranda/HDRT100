"""Production Domain boundary application: continuity, identity and atomicity.

No manager, transport, oscillator or lock acquisition is exercised here.
"""
from dataclasses import dataclass, replace
import itertools
import random
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable

U64 = (1 << 64) - 1
I32_MIN, I32_MAX = -(1 << 31), (1 << 31) - 1


@dataclass(frozen=True)
class Case:
    base_local: int = 1234
    base_output: int = 5_000_000_000
    now: int = 3_000_000_000
    rate: int = 321
    phase: int = -77
    delta: int = -123
    limit: int = 10_000

    def line(self):
        return ' '.join(str(value) for value in vars(self).values())


def oracle(case):
    rate = case.rate + case.delta
    if (case.now < case.base_local or case.rate <= -1_000_000_000 or
            not I32_MIN <= rate <= I32_MAX or rate <= -1_000_000_000 or
            abs(rate) > case.limit):
        return None
    elapsed = case.now - case.base_local
    adjustment = elapsed * abs(case.rate) // 1_000_000_000
    if case.rate < 0:
        adjustment = -adjustment
    output = case.base_output + elapsed + adjustment + case.phase
    return (output, rate) if 0 <= output <= U64 else None


@pytest.fixture(scope='module')
def boundary_executable(tmp_path_factory):
    sources = [ROOT / f'components/vdc_domain/src/{name}.c' for name in (
        'vdc_domain', 'vdc_timestamp', 'vdc_ring_observer', 'vdc_sync_io_adapter',
        'vdc_tdma_payload')]
    sources += [ROOT / f'components/tdma/src/{name}.c' for name in (
        'tdma_service', 'tdma_profile', 'tdma_operating_profile', 'tdma_payload_registry',
        'tdma_flight_fifo', 'tdma_flight_engine', 'tdma_process_image_map',
        'tdma_ring_runtime', 'tdma_traffic_scheduler', 'tdma_service_timing')]
    return compile_executable(tmp_path_factory.mktemp('follower-rate-boundary'),
                              'follower_rate_boundary', HARNESS, sources)


def run(executable, name, cases=None):
    data = None if cases is None else '\n'.join(case.line() for case in cases) + '\n'
    result = subprocess.run([str(executable), name], input=data, text=True,
                            capture_output=True, timeout=30)
    (executable.parent / (name + '.log')).write_text(result.stdout + result.stderr,
                                                   encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    if cases is None:
        return
    (executable.parent / (name + '.input')).write_text(data, encoding='utf-8')
    rows = [tuple(map(int, row.split())) for row in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    for case, row in zip(cases, rows, strict=True):
        expected = oracle(case)
        assert row == ((0, 0, 0) if expected is None else (1, *expected)), (case, row, expected)


def test_identity_and_invalid_candidate_rejections_are_byte_atomic(boundary_executable):
    run(boundary_executable, 'gates')


def test_duplicate_exhaustion_role_aba_and_real_rearm(boundary_executable):
    run(boundary_executable, 'lifecycle')


def test_signed_rates_phase_limits_and_long_uptime(boundary_executable):
    cases = [replace(Case(), rate=rate, phase=phase, delta=delta)
             for rate, phase, delta in itertools.product(
                 [-10_000, -321, 0, 321, 10_000], [I32_MIN, -1, 0, 1, I32_MAX],
                 [-10_001, -1, 0, 1, 10_001])]
    cases += [
        Case(base_local=0, base_output=0, now=U64, rate=-999_999_999,
             phase=I32_MAX, delta=1, limit=1_000_000_000),
        Case(base_local=0, base_output=U64, now=U64, rate=-999_999_999,
             phase=I32_MIN, delta=-1, limit=1_000_000_000),
        Case(base_local=0, base_output=U64 - 100, now=1000, rate=-900_000_000,
             phase=-1, delta=1, limit=1_000_000_000),
        Case(base_local=U64 - 10, base_output=U64 - 30, now=U64,
             rate=I32_MAX, phase=-1, delta=0, limit=I32_MAX),
        Case(base_local=1, now=0),
        Case(base_output=U64, now=1235, rate=0, phase=0),
        Case(base_output=0, now=1234, rate=0, phase=-1),
        Case(rate=I32_MAX, delta=1, limit=(1 << 32) - 1),
        Case(rate=-999_999_999, delta=I32_MIN, limit=(1 << 32) - 1),
        Case(rate=-999_999_999, delta=-1, limit=(1 << 32) - 1),
        Case(rate=-1_000_000_000, delta=1, limit=(1 << 32) - 1),
        Case(rate=I32_MIN, delta=I32_MAX, limit=(1 << 32) - 1),
        Case(rate=0, delta=0, limit=0),
    ]
    run(boundary_executable, 'boundaries', cases)


def test_exact_arithmetic_oracle_across_u64_uptime(boundary_executable):
    rng = random.Random(0xDCC016)
    cases = []
    for _ in range(800):
        base = rng.randrange(U64 + 1)
        cases.append(Case(base_local=base, base_output=rng.randrange(U64 + 1),
                          now=rng.randrange(base, U64 + 1),
                          rate=rng.randint(-999_999_999, I32_MAX),
                          phase=rng.randint(I32_MIN, I32_MAX),
                          delta=rng.randint(I32_MIN, I32_MAX), limit=I32_MAX))
    assert sum(oracle(case) is not None for case in cases) > 100
    run(boundary_executable, 'random', cases)


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "vdc_domain.h"

static void fixture(vdc_domain_context_t *ctx)
{
    assert(vdc_domain_init(ctx));
    vdc_tdma_schedule_profile_t schedule;
    vdc_timestamp_dictionary_t dictionary;
    vdc_path_delay_table_t path;
    assert(vdc_domain_default_schedule_for_topology(&schedule, 1u, 0u, 4u));
    vdc_domain_default_timestamp_dictionary(&dictionary, &schedule);
    memset(&path, 0, sizeof(path));
    path.valid = 1u;
    path.version = VDC_DOMAIN_PATH_DELAY_TABLE_VERSION;
    path.update_seq = 1u;
    path.entry_count = 4u;
    path.schedule_crc32 = schedule.schedule_crc32;
    path.calibration_generation = path.topology_generation = path.bias_generation = 1u;
    path.freshness_us = 1000000u;
    path.flags = VDC_PATH_DELAY_FLAG_ACCEPTED | VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                 VDC_PATH_DELAY_FLAG_BIAS_VALID | VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH;
    for (uint32_t slot = 0; slot < 4u; ++slot) {
        vdc_path_delay_entry_t *entry = &path.entries[slot];
        entry->valid = 1u;
        entry->source_slot_id = entry->writer = slot;
        entry->reference_slot_id = (slot + 1u) % 4u;
        entry->delay_ns = 80u + slot;
        entry->cal_crc32 = 0xA000u + slot;
        entry->freshness_us = path.freshness_us;
        entry->update_seq = 1u;
    }
    assert(vdc_domain_load_observation_path_matrix(&path, 4u));
    path.table_crc32 = vdc_domain_path_delay_table_crc32(&path);
    assert(vdc_domain_activate_tdma_configuration(ctx, &schedule, &dictionary, &path));
    vdc_domain_set_ready(ctx, true);
    vdc_dpll_control_profile_t role = ctx->control.profile;
    role.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    role.follow_master_slot_id = 0u;
    assert(vdc_domain_set_dpll_control_profile(ctx, &role));
    ctx->dco.base_local_tick64 = 1234u;
    ctx->dco.base_vdc_time64_ns = 5000000000ull;
    ctx->dco.period_adjust_ppb = 321;
    ctx->dco.phase_offset_ns = -77;
    /* Sentinel metadata must be cleared on success, unchanged on failure. */
    ctx->control.last_follower_control_generation = 17u;
    ctx->control.last_follower_quality = 3u;
    ctx->control.last_follower_effective_vdc_time_ns = 987654321ull;
}

static vdc_dpll_follower_rate_delta_t command_for(const vdc_domain_context_t *ctx)
{
    vdc_dpll_follower_rate_delta_t cmd = {
        .source_slot_id = ctx->control.profile.follow_master_slot_id,
        .target_slot_id = ctx->schedule.local_slot_id,
        .expected_control_generation = ctx->control.profile.generation,
        .schedule_crc32 = ctx->schedule.schedule_crc32,
        .servo_profile_crc32 = ctx->servo.servo_profile_crc32,
        .clock_epoch_id = ctx->clock.epoch_id,
        .clock_run_id = ctx->clock.run_id,
        .expected_dco_update_seq = ctx->dco.dco_update_seq,
        .expected_applied_command_seq = ctx->control.last_follower_command_seq,
        .command_seq = ctx->control.last_follower_command_seq + 1u,
        .delta_rate_ppb = -123,
    };
    return cmd;
}

static void reject(vdc_domain_context_t *ctx,
                   const vdc_dpll_follower_rate_delta_t *cmd, uint64_t now)
{
    vdc_domain_context_t before;
    memcpy(&before, ctx, sizeof(before));
    assert(!vdc_domain_apply_follower_rate_delta(ctx, cmd, now));
    assert(memcmp(&before, ctx, sizeof(before)) == 0);
}

static bool apply(vdc_domain_context_t *ctx,
                  const vdc_dpll_follower_rate_delta_t *cmd, uint64_t now)
{
    vdc_domain_context_t before;
    memcpy(&before, ctx, sizeof(before));
    uint64_t old_output = 0, new_output = 0;
    const bool old_valid = vdc_domain_dco_local_to_output_ns(&before.dco, now, &old_output);
    if (!vdc_domain_apply_follower_rate_delta(ctx, cmd, now)) {
        assert(memcmp(&before, ctx, sizeof(before)) == 0);
        return false;
    }
    assert(old_valid);
    assert(vdc_domain_dco_local_to_output_ns(&ctx->dco, now, &new_output));
    assert(new_output == old_output);
    assert(ctx->dco.base_local_tick64 == now);
    assert(ctx->dco.base_vdc_time64_ns == old_output);
    assert(ctx->dco.phase_offset_ns == 0);
    assert((int64_t)ctx->dco.period_adjust_ppb ==
           (int64_t)before.dco.period_adjust_ppb + cmd->delta_rate_ppb);
    assert(ctx->dco.dco_update_seq == before.dco.dco_update_seq + 1u);
    before.dco.base_local_tick64 = now;
    before.dco.base_vdc_time64_ns = old_output;
    before.dco.phase_offset_ns = 0;
    before.dco.period_adjust_ppb = ctx->dco.period_adjust_ppb;
    before.dco.dco_update_seq++;
    before.control.last_follower_source_slot_id = cmd->source_slot_id;
    before.control.last_follower_command_seq = cmd->command_seq;
    before.control.last_follower_control_generation = 0u;
    before.control.last_follower_quality = 0u;
    before.control.last_follower_effective_vdc_time_ns = 0u;
    if (before.control.follower_apply_count != UINT32_MAX)
        before.control.follower_apply_count++;
    /* Also proves all DCO identities/lock, clock, DPLL, quality, gate and
     * oscillator discipline remain unchanged. */
    assert(memcmp(&before, ctx, sizeof(before)) == 0);
    return true;
}

static void gates(void)
{
    vdc_domain_context_t ctx;
    fixture(&ctx);
    vdc_dpll_follower_rate_delta_t cmd = command_for(&ctx);
    assert(!vdc_domain_apply_follower_rate_delta(NULL, &cmd, 3000000000ull));
    reject(&ctx, NULL, 3000000000ull);
#define REJECT(change) do { fixture(&ctx); cmd = command_for(&ctx); change; \
                           reject(&ctx, &cmd, 3000000000ull); } while (0)
    REJECT(ctx.ready = 0u);
    REJECT(ctx.control.profile.valid = 0u);
    REJECT(ctx.control.profile.version++);
    REJECT(ctx.control.profile.mode = VDC_DPLL_CONTROL_MODE_MASTER);
    REJECT(ctx.schedule.enabled = 0u);
    REJECT(ctx.servo.enabled = 0u);
    REJECT(ctx.schedule.ring_binding.node_count = 0u);
    REJECT(ctx.schedule.ring_binding.node_count = VDC_DOMAIN_NODE_COUNT + 1u);
    REJECT(cmd.source_slot_id = 4u);
    REJECT(cmd.target_slot_id = 4u);
    REJECT(cmd.target_slot_id = 2u);
    REJECT(cmd.source_slot_id = 2u);
    REJECT(cmd.source_slot_id = 1u; ctx.control.profile.follow_master_slot_id = 1u);
    REJECT(cmd.expected_control_generation++);
    REJECT(cmd.expected_control_generation = ctx.control.profile.generation = 0u);
    REJECT(cmd.schedule_crc32++);
    REJECT(cmd.servo_profile_crc32++);
    REJECT(ctx.clock.valid = 0u);
    REJECT(cmd.clock_epoch_id++);
    REJECT(cmd.clock_run_id++);
    REJECT(ctx.dco.epoch_id++);
    REJECT(ctx.dco.run_id++);
    REJECT(cmd.expected_dco_update_seq++);
    REJECT(ctx.dco.dco_update_seq = cmd.expected_dco_update_seq = 0u);
    REJECT(ctx.dco.dco_update_seq = cmd.expected_dco_update_seq = UINT32_MAX);
    REJECT(cmd.expected_applied_command_seq++);
    REJECT(cmd.command_seq = 0u);
    REJECT(ctx.control.last_follower_command_seq = cmd.expected_applied_command_seq = 5u;
           cmd.command_seq = 5u);
    REJECT(ctx.control.last_follower_command_seq = cmd.expected_applied_command_seq = 5u;
           cmd.command_seq = 4u);
    REJECT(ctx.dco.base_local_tick64 = 3000000001ull);
    REJECT(ctx.dco.valid = 0u);
    REJECT(ctx.dco.nominal_period_ns = 0u);
    REJECT(ctx.dco.lock_state = VDC_DOMAIN_LOCK_FAULT + 1u);
    REJECT(ctx.dco.slew_limit_ppb = ctx.servo.sanity_freq_limit_ppb + 1u);
    REJECT(ctx.dco.tdma_schedule_crc32++);
    REJECT(ctx.dco.servo_profile_crc32++);
#undef REJECT
}

static void lifecycle(void)
{
    vdc_domain_context_t ctx;
    fixture(&ctx);
    vdc_dpll_follower_rate_delta_t cmd = command_for(&ctx);
    assert(apply(&ctx, &cmd, 3000000000ull));
    reject(&ctx, &cmd, 3000000001ull);
    cmd.expected_dco_update_seq = ctx.dco.dco_update_seq;
    cmd.expected_applied_command_seq = ctx.control.last_follower_command_seq;
    reject(&ctx, &cmd, 3000000001ull); /* Duplicate even with refreshed expected state. */
    cmd = command_for(&ctx);
    cmd.delta_rate_ppb = 456;
    assert(apply(&ctx, &cmd, 5000000000ull));

    cmd = command_for(&ctx);
    const uint32_t old_run = ctx.clock.run_id;
    vdc_domain_set_ready(&ctx, false);
    reject(&ctx, &cmd, 5000000001ull);
    assert(vdc_domain_activate_tdma_configuration(&ctx, &ctx.schedule,
        &ctx.timestamp_dictionary, &ctx.path_delay));
    vdc_domain_set_ready(&ctx, true);
    assert(ctx.clock.run_id == old_run + 1u);
    /* Neutralize unrelated gates to independently prove old run rejection. */
    cmd.expected_dco_update_seq = ctx.dco.dco_update_seq;
    cmd.expected_applied_command_seq = ctx.control.last_follower_command_seq;
    reject(&ctx, &cmd, 5000000001ull);
    cmd = command_for(&ctx);
    assert(apply(&ctx, &cmd, 5000000001ull));

    cmd = command_for(&ctx);
    vdc_dpll_control_profile_t role = ctx.control.profile;
    role.mode = VDC_DPLL_CONTROL_MODE_MASTER;
    assert(vdc_domain_set_dpll_control_profile(&ctx, &role));
    role.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    assert(vdc_domain_set_dpll_control_profile(&ctx, &role));
    cmd.expected_dco_update_seq = ctx.dco.dco_update_seq;
    cmd.expected_applied_command_seq = ctx.control.last_follower_command_seq;
    reject(&ctx, &cmd, 5000000002ull);
    cmd = command_for(&ctx);
    assert(apply(&ctx, &cmd, 5000000002ull));

    ctx.control.follower_apply_count = UINT32_MAX;
    ctx.dco.dco_update_seq = UINT32_MAX - 1u;
    cmd = command_for(&ctx);
    cmd.command_seq = UINT32_MAX;
    assert(apply(&ctx, &cmd, 5000000003ull));
    assert(ctx.control.follower_apply_count == UINT32_MAX);
    cmd = command_for(&ctx);
    cmd.command_seq = 1u;
    reject(&ctx, &cmd, 5000000004ull);
    ctx.dco.dco_update_seq = 2u;
    cmd.expected_dco_update_seq = 2u;
    reject(&ctx, &cmd, 5000000004ull); /* Applied seq alone blocks wrap. */
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "gates") == 0) { gates(); return 0; }
    if (strcmp(argv[1], "lifecycle") == 0) { lifecycle(); return 0; }
    uint64_t base_local, base_output, now;
    int32_t rate, phase, delta;
    uint32_t limit;
    while (scanf("%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNd32 " %" SCNd32
                 " %" SCNd32 " %" SCNu32,
                 &base_local, &base_output, &now, &rate, &phase, &delta, &limit) == 7) {
        vdc_domain_context_t ctx;
        fixture(&ctx);
        ctx.dco.base_local_tick64 = base_local;
        ctx.dco.base_vdc_time64_ns = base_output;
        ctx.dco.period_adjust_ppb = rate;
        ctx.dco.phase_offset_ns = phase;
        ctx.servo.sanity_freq_limit_ppb = limit;
        ctx.dco.slew_limit_ppb = 0u;
        vdc_dpll_follower_rate_delta_t cmd = command_for(&ctx);
        cmd.delta_rate_ppb = delta;
        if (apply(&ctx, &cmd, now)) {
            printf("1 %" PRIu64 " %" PRId32 "\n",
                   ctx.dco.base_vdc_time64_ns, ctx.dco.period_adjust_ppb);
        } else {
            puts("0 0 0");
        }
    }
    return 0;
}
'''
