"""Real Domain phase actuator, checked against independent arbitrary-precision arithmetic."""
from dataclasses import dataclass, replace
from pathlib import Path
import itertools
import random
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
from test_vdc_command_owner import compile_executable
import test_vdc_follower_rate_boundary as rate_tests

U64 = (1 << 64) - 1
I64_MIN, I64_MAX = -(1 << 63), (1 << 63) - 1
I32_MIN, I32_MAX = -(1 << 31), (1 << 31) - 1


@dataclass(frozen=True)
class Case:
    base_local: int = 1234
    base_output: int = 5000000000
    now: int = 3000000000
    rate: int = 321
    phase: int = -77
    delta: int = -123
    limit: int = 10000

    def line(self):
        return ' '.join(map(str, vars(self).values()))


def oracle(case):
    if (case.delta == 0 or case.now < case.base_local or
            case.rate <= -1000000000 or abs(case.rate) > case.limit):
        return None
    new_base = case.base_output + case.delta
    elapsed = case.now - case.base_local
    correction = elapsed * abs(case.rate) // 1000000000
    old_output = case.base_output + elapsed + (correction if case.rate >= 0 else -correction) + case.phase
    new_output = old_output + case.delta
    if not all(0 <= value <= U64 for value in (new_base, old_output, new_output)):
        return None
    return (new_base, new_output)


def sources():
    return [ROOT / 'components/vdc_domain/src/vdc_domain.c'] + [
        ROOT / f'components/vdc_domain/src/{name}.c' for name in
        ('vdc_timestamp', 'vdc_ring_observer', 'vdc_sync_io_adapter', 'vdc_tdma_payload')
    ] + [ROOT / f'components/tdma/src/{name}.c' for name in (
        'tdma_service', 'tdma_profile', 'tdma_operating_profile', 'tdma_payload_registry',
        'tdma_flight_fifo', 'tdma_flight_engine', 'tdma_process_image_map',
        'tdma_ring_runtime', 'tdma_traffic_scheduler', 'tdma_service_timing')]


@pytest.fixture(scope='module')
def phase_executable(tmp_path_factory):
    fixture = rate_tests.HARNESS.split('static vdc_dpll_follower_rate_delta_t command_for')[0]
    return compile_executable(tmp_path_factory.mktemp('phase-candidate'),
                              'phase_candidate', fixture + HARNESS, sources())


def execute(exe, mode, cases=None):
    data = None if cases is None else '\n'.join(case.line() for case in cases) + '\n'
    result = subprocess.run([str(exe), mode], input=data, text=True,
                            capture_output=True, timeout=30)
    (exe.parent / (mode + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    if cases is not None:
        (exe.parent / (mode + '.input')).write_text(data, encoding='utf-8')
        rows = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
        assert len(rows) == len(cases)
        for case, row in zip(cases, rows, strict=True):
            expected = oracle(case)
            assert row == ((0, 0, 0) if expected is None else (1, *expected)), (case, row, expected)


def test_phase_identity_and_invalid_model_leave_all_context_bytes_unchanged(phase_executable):
    execute(phase_executable, 'gates')


def test_same_event_phase_correction_and_future_translation_are_exact(phase_executable):
    execute(phase_executable, 'same_event')


def test_phase_changes_do_not_fabricate_frequency_slope(phase_executable):
    execute(phase_executable, 'frequency')


def test_stale_replay_rate_race_role_aba_and_exhaustion_are_rejected(phase_executable):
    execute(phase_executable, 'lifecycle')


def test_negative_phase_alignment_is_explicitly_nonmonotonic_without_lock_claim(phase_executable):
    execute(phase_executable, 'monotonic')


def test_zero_delta_has_no_update_identity(phase_executable):
    execute(phase_executable, 'no_op', [Case(delta=0)])


def test_int64_delta_unsigned_base_and_output_boundaries(phase_executable):
    cases = [replace(Case(), rate=rate, phase=phase, delta=delta)
             for rate, phase, delta in itertools.product(
                 [-10000, -321, 0, 321, 10000],
                 [I32_MIN, -1, 0, 1, I32_MAX],
                 [I64_MIN, -5000000001, -5000000000, -1, 0, 1, I64_MAX])]
    cases += [
        Case(base_local=0, base_output=1 << 63, now=0, rate=0, phase=0, delta=I64_MIN),
        Case(base_local=0, base_output=0, now=0, rate=0, phase=0, delta=I64_MAX),
        Case(base_local=0, base_output=U64, now=0, rate=0, phase=0, delta=I64_MIN),
        Case(base_local=0, base_output=U64, now=0, rate=0, phase=0, delta=1),
        Case(base_local=0, base_output=0, now=1000, rate=0, phase=0, delta=-1),
        Case(base_local=0, base_output=U64 - 100, now=100, rate=0, phase=0, delta=1),
        Case(base_local=0, base_output=U64, now=1000, rate=-900000000, phase=-100, delta=-1,
             limit=I32_MAX),
        Case(base_local=0, base_output=0, now=U64, rate=-999999999, phase=0,
             delta=1, limit=I32_MAX),
        Case(base_local=0, base_output=U64, now=U64, rate=I32_MAX, phase=0,
             delta=I64_MIN, limit=I32_MAX),
        Case(base_local=1, now=0), Case(rate=-1000000000, limit=I32_MAX),
        Case(rate=I32_MIN, limit=U64 & 0xffffffff),
        Case(rate=10001), Case(rate=0, limit=0),
    ]
    execute(phase_executable, 'boundaries', cases)


def test_big_integer_oracle_2000_random_unsigned_coordinates(phase_executable):
    rng = random.Random(0xFA5E0917)
    cases = []
    for _ in range(2000):
        base_local = rng.randrange(U64 + 1)
        cases.append(Case(base_local=base_local, base_output=rng.randrange(U64 + 1),
                          now=rng.randrange(base_local, U64 + 1),
                          rate=rng.randrange(-999999999, I32_MAX + 1),
                          phase=rng.randrange(I32_MIN, I32_MAX + 1),
                          delta=rng.randrange(I64_MIN, I64_MAX + 1), limit=I32_MAX))
    assert sum(oracle(case) is not None for case in cases) > 500
    execute(phase_executable, 'random', cases)


@pytest.fixture(scope='module')
def unchanged_rate_executable(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp('rate-regression'),
                              'rate_regression', rate_tests.HARNESS, sources())


@pytest.mark.parametrize('mode', ['gates', 'lifecycle', 'servo_accepted', 'servo_provisional'])
def test_shared_guard_keeps_real_rate_lifecycle_unchanged(unchanged_rate_executable, mode):
    rate_tests.run(unchanged_rate_executable, mode)


def test_shared_guard_keeps_rate_arithmetic_unchanged(unchanged_rate_executable):
    rate_tests.test_signed_rates_phase_limits_and_long_uptime(unchanged_rate_executable)
    rate_tests.test_exact_arithmetic_oracle_across_u64_uptime(unchanged_rate_executable)


HARNESS = r'''
static vdc_dpll_local_phase_delta_t phase_for(const vdc_domain_context_t *ctx)
{
    const vdc_dpll_local_phase_delta_t cmd = {
        .source_slot_id = ctx->control.profile.follow_master_slot_id,
        .target_slot_id = ctx->schedule.local_slot_id,
        .expected_control_generation = ctx->control.profile.generation,
        .schedule_crc32 = ctx->schedule.schedule_crc32,
        .servo_profile_crc32 = ctx->servo.servo_profile_crc32,
        .clock_epoch_id = ctx->clock.epoch_id,
        .clock_run_id = ctx->clock.run_id,
        .expected_dco_update_seq = ctx->dco.dco_update_seq,
        .delta_phase_ns = -123,
    };
    return cmd;
}

static void reject(vdc_domain_context_t *ctx,
                   const vdc_dpll_local_phase_delta_t *cmd, uint64_t now)
{
    vdc_domain_context_t before;
    memcpy(&before, ctx, sizeof(before));
    assert(!vdc_domain_apply_local_follow_phase_delta(ctx, cmd, now));
    assert(memcmp(&before, ctx, sizeof(before)) == 0);
}

static bool apply(vdc_domain_context_t *ctx,
                  const vdc_dpll_local_phase_delta_t *cmd, uint64_t now)
{
    vdc_domain_context_t expected;
    memcpy(&expected, ctx, sizeof(expected));
    if (!vdc_domain_apply_local_follow_phase_delta(ctx, cmd, now)) {
        assert(memcmp(&expected, ctx, sizeof(expected)) == 0);
        return false;
    }
    /* Exactly two fields may change. No PI/lock/quality/clock/counters/remote
     * metadata or geometric rebase is allowed by a local phase application. */
    expected.dco.base_vdc_time64_ns = ctx->dco.base_vdc_time64_ns;
    expected.dco.dco_update_seq++;
    assert(memcmp(&expected, ctx, sizeof(expected)) == 0);
    return true;
}

static uint64_t project(const vdc_dco_control_t *dco, uint64_t when)
{
    uint64_t result;
    assert(vdc_domain_dco_local_to_output_ns(dco, when, &result));
    return result;
}

static void gates(void)
{
    vdc_domain_context_t ctx;
    fixture(&ctx);
    vdc_dpll_local_phase_delta_t cmd = phase_for(&ctx);
    assert(!vdc_domain_apply_local_follow_phase_delta(NULL, &cmd, 3000000000ull));
    reject(&ctx, NULL, 3000000000ull);
#define REJECT(change) do { fixture(&ctx); cmd = phase_for(&ctx); change; \
                           reject(&ctx, &cmd, 3000000000ull); } while (0)
    REJECT(ctx.ready = 0u);
    REJECT(ctx.control.profile.valid = 0u);
    REJECT(ctx.control.profile.mode = VDC_DPLL_CONTROL_MODE_MASTER);
    REJECT(ctx.schedule.enabled = 0u);
    REJECT(ctx.servo.enabled = 0u);
    REJECT(ctx.schedule.ring_binding.node_count = 0u);
    REJECT(ctx.schedule.ring_binding.node_count = VDC_DOMAIN_NODE_COUNT + 1u);
    REJECT(cmd.source_slot_id = ctx.schedule.ring_binding.node_count);
    REJECT(cmd.target_slot_id = ctx.schedule.ring_binding.node_count);
    REJECT(cmd.source_slot_id = cmd.target_slot_id);
    REJECT(cmd.source_slot_id = 2u);
    REJECT(cmd.target_slot_id = 2u);
    REJECT(cmd.expected_control_generation = 0u);
    REJECT(cmd.expected_control_generation++);
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
    REJECT(ctx.dco.base_local_tick64 = 3000000001ull);
    REJECT(ctx.dco.period_adjust_ppb = -1000000000);
    REJECT(ctx.dco.period_adjust_ppb = 10001);
    REJECT(ctx.dco.valid = 0u);
    REJECT(ctx.dco.nominal_period_ns = 0u);
    REJECT(ctx.dco.lock_state = VDC_DOMAIN_LOCK_FAULT + 1u);
    REJECT(ctx.dco.tdma_schedule_crc32++);
    REJECT(ctx.dco.servo_profile_crc32++);
    REJECT(ctx.dco.slew_limit_ppb = ctx.servo.sanity_freq_limit_ppb + 1u);
    REJECT(cmd.delta_phase_ns = 0);
#undef REJECT
}

static void same_event(void)
{
    for (int polarity = -1; polarity <= 1; polarity += 2) {
        vdc_domain_context_t ctx;
        fixture(&ctx);
        const vdc_dco_control_t old = ctx.dco;
        const uint64_t event = 3000000000ull, now = event + 432109ull;
        const uint64_t observed_local = project(&old, event);
        const int64_t correction = (int64_t)polarity * 4000000000ll;
        const uint64_t same_event_reference = polarity > 0
            ? observed_local + 4000000000ull : observed_local - 4000000000ull;
        vdc_dpll_local_phase_delta_t cmd = phase_for(&ctx);
        cmd.delta_phase_ns = correction;
        assert(apply(&ctx, &cmd, now));
        /* Independent captured reference event, NOT a continuously rebased
         * local output. This is a geometric oracle, not admission of an old
         * event under the new valid_from identity in the manager. */
        assert(project(&ctx.dco, event) == same_event_reference);
        for (uint64_t dt = 0; dt <= 10000000000ull; dt += 10000000ull) {
            const uint64_t earlier = project(&old, now + dt);
            const uint64_t after = project(&ctx.dco, now + dt);
            assert(after == (polarity > 0 ? earlier + 4000000000ull : earlier - 4000000000ull));
        }
        reject(&ctx, &cmd, now); /* Original event/update cannot be replayed. */
    }
}

static void frequency(void)
{
    vdc_domain_context_t ctx;
    fixture(&ctx);
    const vdc_dco_control_t origin = ctx.dco;
    const uint64_t first_time = 3000000000ull;
    const uint64_t last_time = first_time + 5123456789ull;
    const uint64_t retained_first = project(&origin, first_time);
    int64_t cumulative = 0;
    const int64_t corrections[] = {-1000, 617, -23, 805};
    for (unsigned i = 0; i < sizeof(corrections) / sizeof(corrections[0]); ++i) {
        vdc_dpll_local_phase_delta_t cmd = phase_for(&ctx);
        cmd.delta_phase_ns = corrections[i];
        assert(apply(&ctx, &cmd, first_time + 1000000ull * (i + 1u)));
        cumulative += corrections[i];
    }
    const uint64_t admitted_last = project(&ctx.dco, last_time);
    const int64_t translated_last = (int64_t)admitted_last - cumulative;
    assert(translated_last - (int64_t)retained_first ==
           (int64_t)project(&origin, last_time) - (int64_t)retained_first);
    assert(ctx.dco.period_adjust_ppb == origin.period_adjust_ppb);
    assert(ctx.dco.base_local_tick64 == origin.base_local_tick64);
    assert(ctx.dco.phase_offset_ns == origin.phase_offset_ns);
}

static void lifecycle(void)
{
    vdc_domain_context_t ctx;
    fixture(&ctx);
    vdc_dpll_local_phase_delta_t cmd = phase_for(&ctx);
    vdc_dpll_local_rate_delta_t rate = {
        .source_slot_id = cmd.source_slot_id, .target_slot_id = cmd.target_slot_id,
        .expected_control_generation = cmd.expected_control_generation,
        .schedule_crc32 = cmd.schedule_crc32, .servo_profile_crc32 = cmd.servo_profile_crc32,
        .clock_epoch_id = cmd.clock_epoch_id, .clock_run_id = cmd.clock_run_id,
        .expected_dco_update_seq = cmd.expected_dco_update_seq, .delta_rate_ppb = 123,
    };
    assert(apply(&ctx, &cmd, 3000000000ull));
    vdc_domain_context_t before;
    memcpy(&before, &ctx, sizeof(before));
    assert(!vdc_domain_apply_local_follow_rate_delta(&ctx, &rate, 3000000000ull));
    assert(memcmp(&before, &ctx, sizeof(before)) == 0);
    cmd = phase_for(&ctx);
    rate.expected_dco_update_seq = ctx.dco.dco_update_seq;
    const uint64_t before_rate = project(&ctx.dco, 3000001000ull);
    assert(vdc_domain_apply_local_follow_rate_delta(&ctx, &rate, 3000001000ull));
    assert(project(&ctx.dco, 3000001000ull) == before_rate);
    reject(&ctx, &cmd, 3000001000ull);

    cmd = phase_for(&ctx);
    vdc_dpll_control_profile_t role = ctx.control.profile;
    role.mode = VDC_DPLL_CONTROL_MODE_MASTER;
    assert(vdc_domain_set_dpll_control_profile(&ctx, &role));
    role.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    assert(vdc_domain_set_dpll_control_profile(&ctx, &role));
    cmd.expected_dco_update_seq = ctx.dco.dco_update_seq;
    reject(&ctx, &cmd, 3000001000ull);

    fixture(&ctx);
    ctx.dco.dco_update_seq = UINT32_MAX - 1u;
    cmd = phase_for(&ctx);
    assert(apply(&ctx, &cmd, 3000000000ull));
    assert(ctx.dco.dco_update_seq == UINT32_MAX);
    cmd = phase_for(&ctx);
    reject(&ctx, &cmd, 3000000000ull);
}

static void monotonic(void)
{
    vdc_domain_context_t ctx;
    fixture(&ctx);
    const uint64_t now = 3000000000ull;
    const uint64_t old_now = project(&ctx.dco, now);
    vdc_dpll_local_phase_delta_t cmd = phase_for(&ctx);
    cmd.delta_phase_ns = -1000;
    assert(apply(&ctx, &cmd, now));
    uint64_t previous = project(&ctx.dco, now);
    assert(previous < old_now); /* Do NOT claim cross-update monotonicity. */
    for (uint64_t dt = 1; dt < 10000; ++dt) {
        const uint64_t current = project(&ctx.dco, now + dt);
        assert(current >= previous);
        previous = current;
    }
    assert(ctx.dco.lock_state != VDC_DOMAIN_LOCK_LOCKED);
}

static void arithmetic(void)
{
    uint64_t base_local, base_output, now;
    int32_t rate, phase;
    int64_t delta;
    uint32_t limit;
    while (scanf("%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNd32 " %" SCNd32
                 " %" SCNd64 " %" SCNu32,
                 &base_local, &base_output, &now, &rate, &phase, &delta, &limit) == 7) {
        vdc_domain_context_t ctx;
        fixture(&ctx);
        ctx.servo.sanity_freq_limit_ppb = ctx.dco.slew_limit_ppb = limit;
        ctx.dco.base_local_tick64 = base_local;
        ctx.dco.base_vdc_time64_ns = base_output;
        ctx.dco.period_adjust_ppb = rate;
        ctx.dco.phase_offset_ns = phase;
        vdc_dpll_local_phase_delta_t cmd = phase_for(&ctx);
        cmd.delta_phase_ns = delta;
        if (!apply(&ctx, &cmd, now)) puts("0 0 0");
        else printf("1 %" PRIu64 " %" PRIu64 "\n",
                    ctx.dco.base_vdc_time64_ns, project(&ctx.dco, now));
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "gates")) gates();
    else if (!strcmp(argv[1], "same_event")) same_event();
    else if (!strcmp(argv[1], "frequency")) frequency();
    else if (!strcmp(argv[1], "lifecycle")) lifecycle();
    else if (!strcmp(argv[1], "monotonic")) monotonic();
    else arithmetic();
    return 0;
}
'''
