"""Typed Core1 follow: production controller and real Domain rate commit.

Only external owner publications and clocks are simulated. Python Fraction
provides an independent exact rational oracle; the controller algorithm is
never copied into the test harness. Internal DCO adoption does not prove GPIO
lock, timestamp endpoint calibration, real-time deadlines or hardware precision.
"""
from fractions import Fraction
import json
import math
from pathlib import Path
import random
import re
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_follower_rate_boundary import HARNESS as DOMAIN_HARNESS
from test_vdc_boundary_owner import PRELUDE as OWNER_PRELUDE, MATCH_STORAGE, TESTS as OWNER_TESTS

U64 = (1 << 64) - 1
I64_MIN, I64_MAX = -(1 << 63), (1 << 63) - 1


def domain_sources():
    return [ROOT / f"components/vdc_domain/src/{name}.c" for name in (
        "vdc_domain", "vdc_timestamp", "vdc_ring_observer", "vdc_sync_io_adapter", "vdc_tdma_payload")
    ] + [ROOT / f"components/tdma/src/{name}.c" for name in (
        "tdma_service", "tdma_profile", "tdma_operating_profile", "tdma_payload_registry",
        "tdma_flight_fifo", "tdma_flight_engine", "tdma_process_image_map", "tdma_ring_runtime",
        "tdma_traffic_scheduler", "tdma_service_timing")]


def ratio_oracle(local_lower, local_upper, reference_lower, reference_upper):
    """Extrema of (local/reference - 1) * 1e9 over positive intervals."""
    if not (0 < local_lower <= local_upper <= U64 and
            0 < reference_lower <= reference_upper <= U64):
        return None
    low = math.floor((Fraction(local_lower, reference_upper) - 1) * 10**9)
    high = math.ceil((Fraction(local_upper, reference_lower) - 1) * 10**9)
    if not I64_MIN <= low <= high <= I64_MAX:
        return None
    return low, high


def run_case(executable, case, data=None):
    command = [str(executable), case]
    result = subprocess.run(command, input=data, text=True, capture_output=True, timeout=30)
    (executable.parent / f"run-{case}.json").write_text(json.dumps(dict(
        command=command, input=data, returncode=result.returncode,
        stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


@pytest.fixture(scope="module")
def follow_executable(tmp_path_factory):
    manager = ROOT / "components/vdc_dpll_manager/src"
    physical = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    begin = physical.index("enum {\n    TDMA_EVENT_LIVE_RETAINED")
    end = physical.index("} tdma_pio_spi_event_exact_t;", begin) + len("} tdma_pio_spi_event_exact_t;")
    event_types = '#include "tdma_event_history.h"\n' + physical[begin:end]
    matcher = (manager / "vdc_dpll_feedback_match.inc").read_text(encoding="utf-8")
    source_type = re.search(r'typedef struct \{\s*uint64_t next_ordinal.*?\} vdc_feedback_match_source_t;', matcher, re.S)
    assert source_type
    helpers = matcher[matcher.index("static uint32_t match_inc"):matcher.index("/* Keep authorization")]
    prelude = OWNER_PRELUDE.replace("EVENT_TYPES", event_types).replace(
        '#include <assert.h>', '#include <assert.h>\n#include <inttypes.h>\n#include "vdc_priority_follow.h"\n#include "vdc_priority_phase.h"\n'
        '#include "vdc_priority_rx.h"\n#include "vdc_priority_match.h"\n'
        'static unsigned core;\nstatic unsigned get_core_num(void) { return core; }')
    old_bridge = "{ (void)hz;(void)out;return false; }"
    assert old_bridge in prelude
    prelude = prelude.replace(old_bridge,
        '{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=(vdc_timestamp_clock_bridge_t){'
        '.tick_hz=hz,.raw_before=raw_now,.raw_after=raw_now+1,.local_ns=(now_ns/1000u)*1000u};return true; }')
    harness = prelude + EXTERNAL_INPUTS + source_type.group(0) + MATCH_STORAGE + helpers
    harness += '\nstatic void phase_model_committed_core1(const vdc_dpll_manager_committed_model_t *model);\n#define VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK(model) phase_model_committed_core1(model)\n'
    for name in ("vdc_model_feedback.inc", "vdc_boundary_control.inc", "vdc_priority_match.inc", "vdc_priority_follow.inc"):
        included = (manager / name).read_text(encoding="utf-8")
        if name == "vdc_priority_follow.inc":
            included = included.replace('#include "vdc_priority_phase.inc"',
                (manager / "vdc_priority_phase.inc").read_text(encoding="utf-8"))
        harness += "\n" + included
    harness += "\n" + ingress_definition(DOMAIN_HARNESS, "fixture") + SCENARIOS.replace(
        "OWNER_REMOTE_INPUT", ingress_definition(OWNER_TESTS, "follower_input"))
    sources = domain_sources() + [
        manager / "vdc_feedback_match.c",
        ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c"]
    return compile_executable(tmp_path_factory.mktemp("priority-follow"), "priority_follow", harness, sources)


def test_exact_outward_ratio_oracle(follow_executable):
    limit = int(run_case(follow_executable, "interval_limit").strip())
    cases = [
        (1, 1, 3, 3), (3, 3, 1, 1), (999_999_999, 1_000_000_001, 10**9, 10**9),
        (10**9, 10**9, 999_999_999, 1_000_000_001),
        (U64, U64, U64, U64), (1, 1, U64, U64), (U64, U64, 1, 1),
        (0, 1, 1, 1), (1, 0, 1, 1), (1, 1, 0, 1), (1, 1, 2, 1),
    ]
    rng = random.Random(0x51C0D)
    for _ in range(500):
        local = rng.randrange(1, limit + 1)
        reference = rng.randrange(1, limit + 1)
        cases.append((local, rng.randrange(local, limit + 1), reference, rng.randrange(reference, limit + 1)))
    for _ in range(500):
        local = rng.randrange(1, U64 + 1)
        reference = rng.randrange(1, U64 + 1)
        cases.append((local, rng.randrange(local, U64 + 1), reference, rng.randrange(reference, U64 + 1)))
    for _ in range(200):
        local = rng.randrange(1, 10**15)
        reference = rng.randrange(1, 10**4)
        cases.append((local, local + 1, reference, reference + 1))
    output = run_case(follow_executable, "ratio", "\n".join(" ".join(map(str, row)) for row in cases) + "\n")
    rows = [tuple(map(int, row.split())) for row in output.splitlines()]
    assert len(rows) == len(cases)
    for case, (reason, lo, hi) in zip(cases, rows, strict=True):
        expected = ratio_oracle(*case) if max(case) <= limit else None
        if expected is None:
            assert reason != 0, (case, reason, lo, hi)
        else:
            assert (reason, lo, hi) == (0, *expected), (case, reason, lo, hi, expected)


@pytest.mark.parametrize("case", ["fast", "slow", "uncertain", "duplicate", "pending", "busy", "missing"])
def test_real_match_to_continuous_domain_commit(follow_executable, case):
    output = run_case(follow_executable, case)
    rows = [tuple(map(int, line.split()[1:])) for line in output.splitlines() if line.startswith("DECISION ")]
    assert len(rows) == 1
    llo, lhi, rlo, rhi, lo, hi, delta, before, after = rows[0]
    assert (lo, hi) == ratio_oracle(llo, lhi, rlo, rhi)
    assert after == before + delta


@pytest.mark.parametrize("stage", ["baseline", "pending", "at_apply"])
@pytest.mark.parametrize("change", ["stop", "session", "arm", "observer", "rx_epoch", "path", "role", "config", "clock_run"])
def test_owner_lifetime_changes_cancel(follow_executable, stage, change):
    run_case(follow_executable, f"cancel_{stage}_{change}")


@pytest.mark.parametrize("case", ["model_revision", "apply_dco_changed", "apply_model_changed",
                                 "apply_role_request", "apply_age", "domain_rejection", "mode_exclusive",
                                 "remote_command_control"])
def test_model_validity_and_mode_exclusion(follow_executable, case):
    run_case(follow_executable, case)


def test_negative_feedback_bounded_delta(follow_executable):
    # Explicit policy examples plus extreme intervals. This checks observable
    # sign, deadband, per-step clamp and total-limit behavior independently.
    cases = [
        (800, 1200, 0, 10_000, -400), (-1200, -800, 0, 10_000, 400),
        (0, 2000, 0, 10_000, 0), (-2000, 0, 0, 10_000, 0),
        (-2000, 2000, 0, 10_000, 0), (10, 20, 0, 10_000, 0),
        (-20, -10, 0, 10_000, 0), (11, 100, 0, 10_000, -5),
        (-100, -11, 0, 10_000, 5), (I64_MAX, I64_MAX, 0, 10_000, -1000),
        (I64_MIN, I64_MIN, 0, 10_000, 1000),
        (800, 1200, -9950, 10_000, -50), (-1200, -800, 9950, 10_000, 50),
        (800, 1200, -10_000, 10_000, 0), (-1200, -800, 10_000, 10_000, 0),
        (800, 1200, 0, 0, 0), (800, 1200, 0, 100, -100),
        (1999, 1999, 0, 10_000, -999), (-1999, -1999, 0, 10_000, 999),
        (2000, 2000, 0, 10_000, -1000), (-2000, -2000, 0, 10_000, 1000),
    ]
    output = run_case(follow_executable, "delta", "\n".join(" ".join(map(str, row[:4])) for row in cases) + "\n")
    assert [int(row) for row in output.splitlines()] == [row[-1] for row in cases]


def delta_rows(executable, cases):
    output = run_case(executable, "delta", "\n".join(" ".join(map(str, row)) for row in cases) + "\n")
    values = [int(row) for row in output.splitlines()]
    assert len(values) == len(cases)
    return values


def bounded_fraction_policy(lo, hi, current, limit, fraction=Fraction(1, 2)):
    """Project a rational correction into the admitted gain/step interval."""
    ceiling = min(limit, (1 << 31) - 1)
    floor = max(-ceiling, -999_999_999)
    if lo > hi or not floor <= current <= ceiling:
        return 0
    # Zero or the deadband intersects the error interval: no proved direction.
    if lo <= 10 and hi >= -10:
        return 0
    nearest = min((lo, hi), key=abs)
    target = current + math.trunc(-nearest * fraction)
    admissible_lo = max(floor, current - 1000)
    admissible_hi = min(ceiling, current + 1000)
    return min(max(target, admissible_lo), admissible_hi) - current


def test_measured_nearest_boundary_steps(follow_executable):
    # These are measured nearest-zero boundaries, not interval midpoints.
    cases = [(-1000, -131, 1134, 10_000), (-1000, -185, 1281, 10_000),
             (-1000, -147, 3422, 10_000)]
    assert delta_rows(follow_executable, cases) == [65, 92, 73]
    reflected = [(-hi, -lo, -current, limit) for lo, hi, current, limit in cases]
    assert delta_rows(follow_executable, reflected) == [-65, -92, -73]


def test_fraction_policy_integer_extremes_and_clamps(follow_executable):
    cases = []
    intervals = [(I64_MIN, I64_MIN), (I64_MIN, -11), (-11, -11), (-10, -10),
                 (-1000, 1000), (10, 10), (11, 11), (11, I64_MAX), (I64_MAX, I64_MAX), (2, 1)]
    for lo, hi in intervals:
        for current in [-(1 << 31), -1_000_000_000, -999_999_999, -10_000, -1, 0, 1, 10_000, (1 << 31)-1]:
            for limit in [0, 1, 10_000, 999_999_999, 1_000_000_000, (1 << 31)-1, (1 << 32)-1]:
                cases.append((lo, hi, current, limit))
    rng = random.Random(0xF01102)
    for _ in range(500):
        lo, hi = sorted((rng.randrange(I64_MIN, I64_MAX+1), rng.randrange(I64_MIN, I64_MAX+1)))
        cases.append((lo, hi, rng.randrange(-(1 << 31), 1 << 31), rng.randrange(1 << 32)))
    for case, actual in zip(cases, delta_rows(follow_executable, cases), strict=True):
        assert actual == bounded_fraction_policy(*case), case
        lo, hi, current, limit = case
        lower, upper = max(-limit, -999_999_999), min(limit, (1 << 31)-1)
        assert -1000 <= actual <= 1000
        if lower <= current <= upper:
            assert lower <= current + actual <= upper
        else:
            assert actual == 0


def test_exact_gain_direction_without_crossing_zero_in_servo_range(follow_executable):
    """Physical gain arithmetic only; this does not simulate transport or lock."""
    cases = []
    for current in [-10_000, -9999, -5000, 0, 5000, 9999, 10_000]:
        for nearest in [11, 31, 131, 185, 147, 800, 1999, 2000, 6000, 10_000]:
            for sign in [-1, 1]:
                bounds = sorted((sign * nearest, sign * (nearest + 200)))
                cases.append((*bounds, current, 10_000))
    for (lo, hi, current, limit), delta in zip(cases, delta_rows(follow_executable, cases), strict=True):
        for error in [Fraction(lo), Fraction(hi), Fraction(lo+hi, 2)]:
            ratio = 1 + error / 10**9
            updated = ratio * Fraction(10**9 + current + delta, 10**9 + current)
            after = (updated - 1) * 10**9
            if error > 0:
                assert 0 <= after <= error
            else:
                assert error <= after <= 0
            if delta:
                assert abs(after) < abs(error)
        assert -limit <= current + delta <= limit


def test_stationary_gain_response_with_interval_uncertainty(follow_executable):
    """Exact stationary arithmetic comparison; never evidence of physical lock."""
    states = []
    for error in [-6000, -800, -185, 131, 800, 6000]:
        for initial in [-1000, 0, 1000]:
            states.append({"initial_error_ppb": error, "initial_rate_ppb": initial,
                           "base_ratio": Fraction(10**9 + error, 10**9 + initial),
                           "half_rate": initial, "quarter_rate": initial,
                           "half_steps": 0, "quarter_steps": 0, "history": []})
    for iteration in range(40):
        cases = []
        for state in states:
            error = (state["base_ratio"] * Fraction(10**9 + state["half_rate"], 10**9) - 1) * 10**9
            # The independent measurement oracle encloses the exact rate
            # with outward integer rounding and an explicit uncertainty.
            cases.append((math.floor(error - 20), math.ceil(error + 20), state["half_rate"], 10_000))
        deltas = delta_rows(follow_executable, cases)
        for state, case, delta in zip(states, cases, deltas, strict=True):
            before = (state["base_ratio"] * Fraction(10**9 + state["half_rate"], 10**9) - 1) * 10**9
            quarter_error = (state["base_ratio"] * Fraction(10**9 + state["quarter_rate"], 10**9) - 1) * 10**9
            quarter_delta = bounded_fraction_policy(math.floor(quarter_error - 20), math.ceil(quarter_error + 20),
                                                      state["quarter_rate"], 10_000, Fraction(1, 4))
            state["half_rate"] += delta
            state["quarter_rate"] += quarter_delta
            state["half_steps"] += bool(delta)
            state["quarter_steps"] += bool(quarter_delta)
            after = (state["base_ratio"] * Fraction(10**9 + state["half_rate"], 10**9) - 1) * 10**9
            assert abs(after) <= abs(before)
            assert before * after >= 0
            assert -10_000 <= state["half_rate"] <= 10_000
            state["history"].append({"iteration": iteration, "interval": list(case[:2]), "delta": delta,
                                     "error_before": str(before), "error_after": str(after),
                                     "quarter_delta": quarter_delta})
    for state in states:
        assert state["half_steps"] < state["quarter_steps"]
        assert all(row["delta"] == row["quarter_delta"] == 0 for row in state["history"][-5:])
        assert abs(Fraction(state["history"][-1]["error_after"])) <= 31
        state["base_ratio"] = str(state["base_ratio"])
    (follow_executable.parent / "stationary-response.json").write_text(
        json.dumps({"qualification": "Stationary rational arithmetic, not physical or real-time lock evidence",
                    "states": states}, indent=2), encoding="utf-8")


@pytest.mark.parametrize("case", ["pending_busy_retry", "stop_rx_busy", "age_rx_busy", "observer_rx_busy",
                                 "raw_age", "unguarded_apply", "domain_owner_rejection",
                                 "request_authorization", "snapshot_busy"])
def test_contention_does_not_hide_cancellation(follow_executable, case):
    run_case(follow_executable, case)


@pytest.mark.parametrize("case", ["two_replacements", "half_exact", "not_half", "same_width",
    "odd_half_reject", "odd_half_accept", "zero_width", "window_before", "window_exact",
    "window_after", "total_delay_bound", "after_first", "busy_no_refund", "model_reset",
    "stop_cleanup", "apply_cleanup", "final_baseline_reset", "missing", "busy", "pending"])
def test_bounded_early_baseline_quality(follow_executable, case):
    run_case(follow_executable, "quality_" + case)


@pytest.mark.parametrize("change", ["stop", "session", "arm", "observer", "rx_epoch", "path", "role", "config", "clock_run"])
def test_quality_baseline_does_not_survive_owner_change(follow_executable, change):
    run_case(follow_executable, "quality_owner_" + change)


def test_quality_private_storage_and_public_abi(follow_executable):
    sizes = tuple(map(int, run_case(follow_executable, "sizes").split()))
    assert sizes == (784, 216, 408)


EXTERNAL_INPUTS = r'''
/* Isolated controller/recorder tests use factory requested configuration.
 * The dedicated configuration suite links the actual config owner instead. */
#ifndef VDC_PRIORITY_CONFIG_REAL
bool vdc_dpll_manager_get_priority_follow_baseline(vdc_priority_follow_baseline_config_t *out)
{
    if(!out)return false;
    *out=(vdc_priority_follow_baseline_config_t){VDC_PRIORITY_FOLLOW_BASELINE_REPLACEMENTS,
        (uint32_t)VDC_PRIORITY_FOLLOW_BASELINE_WINDOW_NS};
    return true;
}
#endif
/* Avoid Windows crash reporting dialogs for a failed host expectation. */
#undef assert
#define assert(condition) do { if(!(condition)) { \
    fprintf(stderr,"assertion failed at %s:%d: %s\n",__FILE__,__LINE__,#condition);exit(1); \
} } while(0)
static tdma_pio_spi_event_exact_t exact;
static vdc_priority_rx_snapshot_t priority_rx;
static tdma_event_exact_result_t exact_result=TDMA_EVENT_EXACT_OK;
static bool priority_rx_available=true;
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{ if(!ring_available)return false;*out=ring;return true; }
bool vdc_priority_rx_copy_live(vdc_priority_rx_snapshot_t *out)
{ if(!priority_rx_available || !priority_rx.active || !priority_rx.have_record)return false;
  *out=priority_rx;return true; }
tdma_event_exact_result_t tdma_runtime_owner_copy_event_history_exact(uint64_t arm,
    uint32_t observer,uint32_t sequence,tdma_pio_spi_event_exact_t *out)
{
    assert(arm==live.arm_epoch && observer==live.record.epoch);
    assert(sequence==priority_rx.typed_record.event_sequence);
    if(exact_result!=TDMA_EVENT_EXACT_OK)return exact_result;
    *out=exact;return TDMA_EVENT_EXACT_OK;
}
'''


SCENARIOS = r'''
static vdc_priority_follow_snapshot_t status(void)
{
    vdc_priority_follow_snapshot_t result;
    assert(vdc_dpll_manager_get_priority_follow(&result));
    return result;
}
static vdc_dpll_manager_committed_model_t model(void)
{
    vdc_dpll_manager_committed_model_t result;
    assert(vdc_dpll_manager_get_committed_model(&result));
    return result;
}
OWNER_REMOTE_INPUT
static void publish(void)
{
    assert(!(s_committed_model_guard&1u));
    ++s_committed_model_guard;
    model_feedback_end_core1(vdc_dpll_manager_feedback_session());
}
static void prepare(void)
{
    vdc_priority_match_core1();
    priority_follow_prepare_core1();
}
static void apply(void)
{
    assert(!(s_committed_model_guard&1u));
    ++s_committed_model_guard;
    vdc_boundary_service_core1();
    priority_follow_apply_core1();
    model_feedback_end_core1(vdc_dpll_manager_feedback_session());
}
static void tick(void) { prepare();apply(); }
static void event(uint32_t sequence,uint64_t elapsed_ns)
{
    now_ns=UINT64_C(3000000000)+elapsed_ns;
    now_ms=(uint32_t)(now_ns/1000000u);
    raw_now=now_ns/4u;
    exact=(tdma_pio_spi_event_exact_t){.arm_epoch=live.arm_epoch,.tick_hz=live.tick_hz,
        .observer_epoch=live.record.epoch,.flags=live.flags,
        .timer1_enable_before=live.timer1_enable_before,.timer1_enable_after=live.timer1_enable_after,
        .record={.sequence=sequence,.rx_elapsed_cycles=raw_now-live.timer1_enable_before-100u}};
    live.record.sequence=sequence;live.record.epoch=exact.observer_epoch;
    priority_rx.typed_record.event_sequence=sequence;
    priority_rx.typed_record.event_time_lower=UINT64_C(12000000000)+elapsed_ns;
    priority_rx.carrier_sequence=sequence+3u;
}
static void setup(int32_t rate)
{
    fixture(&s_vdc_domain);
    for(uint32_t i=0;i<s_vdc_domain.path_delay.entry_count;++i)
        s_vdc_domain.path_delay.entries[i].direction=VDC_PATH_DELAY_DIRECTION_TDMA_DATA_REVERSE;
    assert(vdc_domain_load_observation_path_matrix(&s_vdc_domain.path_delay,4u));
    s_vdc_domain.path_delay.table_crc32=vdc_domain_path_delay_table_crc32(&s_vdc_domain.path_delay);
    s_vdc_domain.dco.period_adjust_ppb=rate;
    s_vdc_domain.control.last_follower_command_seq=19u;
    s_vdc_domain.control.follower_apply_count=7u;
    s_dpll_role_requested_generation=s_dpll_role_applied_generation=s_vdc_domain.control.profile.generation;
    ring=(tdma_ring_clock_snapshot_t){.config_seq=7,.applied_config_seq=7,.node_count=4,
        .local_slot_id=1,.reference_slot_id=0,.schedule_crc32=s_vdc_domain.schedule.schedule_crc32};
    owner.ring_runtime.ring_profile_crc32=0x456u;
    live=(tdma_pio_spi_event_live_snapshot_t){.flags=TDMA_EVENT_LIVE_RETAINED|TDMA_EVENT_LIVE_ACTIVE|
        TDMA_EVENT_LIVE_ANCHOR_VALID,.arm_epoch=77,.tick_hz=BOARD_SYS_CLOCK_HZ,
        .timer1_enable_before=1000,.timer1_enable_after=1001,.record={.epoch=8}};
    priority_rx=(vdc_priority_rx_snapshot_t){.schema=1,.active=1,.epoch=5,.have_record=1,
        .source_slot=0,.target_mask=14,.typed_record={.binding_generation=101,.uncertainty_width=7}};
    core=0;stopped=true;
    assert(vdc_dpll_manager_set_feedback_session(123));
    assert(vdc_dpll_manager_set_priority_match(101));
    assert(vdc_dpll_manager_set_priority_follow(true));
    ring.enabled=ring.adapter_started=ring.data_enabled=1;stopped=false;core=1;
    raw_now=500000000;publish();
    event(100,0);
}
static void assert_remote_metadata(void)
{
    assert(s_vdc_domain.control.last_follower_command_seq==19u);
    assert(s_vdc_domain.control.follower_apply_count==7u);
    assert(s_vdc_domain.control.last_follower_control_generation==17u);
    assert(s_vdc_domain.control.last_follower_quality==3u);
    assert(s_vdc_domain.control.last_follower_effective_vdc_time_ns==987654321u);
}
static void flow_test(const char *name)
{
    const int32_t rate=!strcmp(name,"slow")?-6000:!strcmp(name,"uncertain")?0:6000;
    setup(rate);
    vdc_domain_context_t before=s_vdc_domain;
    tick();
    fprintf(stderr,"baseline state=%u reason=%u baselines=%u matchreason=%u matched=%u domain_change=%d\n",
        status().state,status().last_reason,status().baselines,s_priority_match_work.status.last_reason,
        s_priority_match_work.status.matched,memcmp(&before,&s_vdc_domain,sizeof(before)));
    assert(status().baselines==1u && !status().applied && !memcmp(&before,&s_vdc_domain,sizeof(before)));
    /* A gap of 100 source events is intentional; no invented consecutive
     * sample or remote model token is needed to compute the output secant. */
    if(!strcmp(name,"pending") || !strcmp(name,"busy") || !strcmp(name,"missing")) {
        /* A recent valid waiting ticket preserves the one-second baseline.
         * A wholly missing 1.5s publication would correctly age out instead. */
        event(150,990000000u);tick();assert(!status().applied);
        event(200,1050000000u);
        if(!strcmp(name,"pending"))exact_result=TDMA_EVENT_EXACT_PENDING;
        if(!strcmp(name,"busy"))exact_result=TDMA_EVENT_EXACT_BUSY;
        if(!strcmp(name,"missing"))priority_rx_available=false;
        tick();assert(!status().applied && !memcmp(&before,&s_vdc_domain,sizeof(before)));
        exact_result=TDMA_EVENT_EXACT_OK;priority_rx_available=true;
    } else event(200,1500000000u);
    const vdc_dpll_manager_committed_model_t old_model=model();
    uint64_t continuity;
    assert(vdc_domain_dco_local_to_output_ns(&before.dco,now_ns,&continuity));
    tick();
    vdc_priority_follow_snapshot_t s=status();
    fprintf(stderr,"state=%u reason=%u prepared=%u applied=%u delta=%d error=%" PRId64 "..%" PRId64 "\n",
        s.state,s.last_reason,s.prepared,s.applied,s.selected_delta_ppb,s.error_lo_ppb,s.error_hi_ppb);
    printf("DECISION %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRId64 " %" PRId64 " %d %d %d\n",
        s.local_interval_lo,s.local_interval_hi,s.interval_lo,s.interval_hi,
        s.error_lo_ppb,s.error_hi_ppb,s.selected_delta_ppb,s.before_ppb,s.after_ppb);
    if(!strcmp(name,"uncertain")) {
        assert(s.no_adjust==1 && !s.applied && !s.selected_delta_ppb);
        assert(!memcmp(&before,&s_vdc_domain,sizeof(before)));
    } else {
        assert(s.applied==1u && s.selected_delta_ppb && (int64_t)s.selected_delta_ppb*rate<0);
        assert(s_vdc_domain.dco.dco_update_seq==before.dco.dco_update_seq+1u);
        assert(s_vdc_domain.dco.period_adjust_ppb==rate+s.selected_delta_ppb);
        assert(s_vdc_domain.dco.lock_state==before.dco.lock_state);
        uint64_t after;
        assert(vdc_domain_dco_local_to_output_ns(&s_vdc_domain.dco,now_ns,&after));
        assert(after==continuity && s_vdc_domain.dco.base_local_tick64==now_ns);
        assert(model().token!=old_model.token && model().valid_from_raw==raw_now);
        assert(s.before_dco_seq==before.dco.dco_update_seq && s.after_dco_seq==s_vdc_domain.dco.dco_update_seq);
        assert(s.before_ppb==rate && s.after_ppb==s_vdc_domain.dco.period_adjust_ppb);
    }
    assert_remote_metadata();
    if(!strcmp(name,"duplicate")) {
        before=s_vdc_domain;
        for(unsigned i=0;i<8;++i)tick();
        assert(status().applied==1u && !memcmp(&before,&s_vdc_domain,sizeof(before)));
    }
}
static const char *change_kind;
static void change_owner(void)
{
    if(!strcmp(change_kind,"stop"))ring.enabled=0u;
    else if(!strcmp(change_kind,"session"))++s_model_feedback_session;
    else if(!strcmp(change_kind,"arm"))++live.arm_epoch;
    else if(!strcmp(change_kind,"observer"))++live.record.epoch;
    else if(!strcmp(change_kind,"rx_epoch"))++priority_rx.epoch;
    else if(!strcmp(change_kind,"path"))++s_vdc_domain.path_delay.table_crc32;
    else if(!strcmp(change_kind,"role"))++s_vdc_domain.control.profile.generation;
    else if(!strcmp(change_kind,"config"))++ring.config_seq;
    else if(!strcmp(change_kind,"clock_run"))++s_vdc_domain.clock.run_id;
    else assert(0);
}
static void cancellation_test(const char *name)
{
    setup(6000);tick();
    const char *stage=name+7;
    bool pending=!strncmp(stage,"pending_",8),at_apply=!strncmp(stage,"at_apply_",9);
    change_kind=stage+(pending?8:at_apply?9:9);
    event(200,1500000000u);
    if(pending || at_apply) {
        prepare();assert(status().prepared==1u && !status().applied);
    }
    vdc_dco_control_t dco_before=s_vdc_domain.dco;
    if(at_apply)now_hook=change_owner;else change_owner();
    if(pending || at_apply)apply();else tick();
    assert(!status().applied && !memcmp(&dco_before,&s_vdc_domain.dco,sizeof(dco_before)));
    assert_remote_metadata();
    /* Restoring the apparent owner values must not resurrect the old ticket. */
    if(!strcmp(change_kind,"stop"))ring.enabled=1;
    else if(!strcmp(change_kind,"session"))--s_model_feedback_session;
    else if(!strcmp(change_kind,"arm"))--live.arm_epoch;
    else if(!strcmp(change_kind,"observer"))--live.record.epoch;
    else if(!strcmp(change_kind,"rx_epoch"))--priority_rx.epoch;
    else if(!strcmp(change_kind,"path"))--s_vdc_domain.path_delay.table_crc32;
    else if(!strcmp(change_kind,"role"))--s_vdc_domain.control.profile.generation;
    else if(!strcmp(change_kind,"config"))--ring.config_seq;
    else if(!strcmp(change_kind,"clock_run"))--s_vdc_domain.clock.run_id;
    event(300,1800000000u);tick();
    assert(!status().applied);
}
static void validity_test(const char *name)
{
    setup(6000);tick();
    event(200,1500000000u);
    if(!strcmp(name,"model_revision")) {
        ++s_vdc_domain.dco.period_adjust_ppb;publish();
        tick();assert(!status().applied);
        event(300,1600000000u);tick();
        assert(!status().applied && status().baselines>=2u);
        event(400,3100000000u);tick();assert(status().applied==1u);
        assert(priority_rx.typed_record.binding_generation==101u);
        return;
    }
    if(!strcmp(name,"domain_rejection")) {
        const vdc_dpll_manager_committed_model_t m=model();
        vdc_dpll_local_rate_delta_t cmd={.source_slot_id=0,.target_slot_id=1,
            .expected_control_generation=m.role_generation,.schedule_crc32=ring.schedule_crc32,
            .servo_profile_crc32=s_vdc_domain.servo.servo_profile_crc32,
            .clock_epoch_id=m.clock_epoch_id,.clock_run_id=m.clock_run_id,
            .expected_dco_update_seq=m.dco.dco_update_seq,.delta_rate_ppb=-100};
        /* A real invalid Domain ticket must reject without changing any byte. */
        ++cmd.expected_dco_update_seq;
        vdc_domain_context_t before=s_vdc_domain;
        assert(!vdc_domain_apply_local_follow_rate_delta(&s_vdc_domain,&cmd,now_ns));
        assert(!memcmp(&before,&s_vdc_domain,sizeof(before)));
        return;
    }
    if(!strcmp(name,"remote_command_control")) {
        core=0;stopped=true;assert(vdc_dpll_manager_set_boundary_probe(-17));core=1;stopped=false;
        follower_input();const vdc_dco_control_t before=s_vdc_domain.dco;
        ++s_committed_model_guard;vdc_boundary_service_core1();model_feedback_end_core1(123);
        assert(s_vdc_domain.dco.dco_update_seq==before.dco_update_seq+1u);
        assert(s_vdc_domain.dco.period_adjust_ppb==before.period_adjust_ppb-17);
        return;
    }
    if(!strcmp(name,"mode_exclusive")) {
        bool typed=false,old=true,automatic=true;
        assert(vdc_dpll_manager_try_priority_follow_enabled(&typed) && typed);
        assert(vdc_dpll_manager_try_local_follow_enabled(&old) && !old);
        assert(vdc_dpll_manager_try_boundary_auto_enabled(&automatic) && !automatic);
        assert(!vdc_dpll_manager_set_priority_follow(false));
        core=0;stopped=true;
        assert(vdc_dpll_manager_set_local_follow(true));
        assert(vdc_dpll_manager_try_priority_follow_enabled(&typed) && !typed);
        assert(vdc_dpll_manager_try_local_follow_enabled(&old) && old);
        assert(vdc_dpll_manager_set_boundary_auto(true));
        assert(vdc_dpll_manager_try_local_follow_enabled(&old) && !old);
        assert(vdc_dpll_manager_try_boundary_auto_enabled(&automatic) && automatic);
        assert(vdc_dpll_manager_set_priority_follow(true));
        assert(vdc_dpll_manager_try_boundary_auto_enabled(&automatic) && !automatic);
        core=1;stopped=false;
        vdc_domain_context_t before=s_vdc_domain;
        /* An old candidate/remote command cannot be adopted by the real
         * boundary owner while this mode owns the local DCO. */
        local_candidate_available=true;local_candidate.active=1;
        follower_input();
        ++s_committed_model_guard;vdc_boundary_service_core1();model_feedback_end_core1(123);
        assert(!memcmp(&before,&s_vdc_domain,sizeof(before)) && !captured_applies);
        return;
    }
    prepare();assert(status().prepared==1u);
    if(!strcmp(name,"apply_dco_changed"))++s_vdc_domain.dco.period_adjust_ppb;
    else if(!strcmp(name,"apply_model_changed")){++s_vdc_domain.dco.dco_update_seq;publish();}
    else if(!strcmp(name,"apply_role_request"))++s_dpll_role_requested_generation;
    else if(!strcmp(name,"apply_age"))now_ms+=VDC_PRIORITY_FOLLOW_MAX_AGE_MS+1u;
    else assert(0);
    vdc_domain_context_t before=s_vdc_domain;
    apply();assert(!status().applied && !memcmp(&before,&s_vdc_domain,sizeof(before)));
}
static void contention_test(const char *name)
{
    setup(6000);
    if(!strcmp(name,"request_authorization")) {
        core=0;stopped=true;
        assert(vdc_dpll_manager_set_priority_follow(false));
        assert(vdc_dpll_manager_set_priority_match(0));
        assert(!vdc_dpll_manager_set_priority_follow(true));
        assert(vdc_dpll_manager_set_priority_match(102));
        assert(vdc_dpll_manager_set_priority_follow(true));
        assert(vdc_dpll_manager_set_feedback_session(124));
        assert(!vdc_dpll_manager_set_priority_follow(true));
        return;
    }
    if(!strcmp(name,"domain_owner_rejection")) {
        s_vdc_domain.dco.dco_update_seq=UINT32_MAX;
        raw_now=500000000;publish();event(100,0);
    }
    tick();event(200,1500000000u);prepare();
    assert(status().prepared==1u && !status().applied);
    const vdc_domain_context_t before=s_vdc_domain;
    if(!strcmp(name,"snapshot_busy")) {
        vdc_priority_follow_snapshot_t sentinel;
        memset(&sentinel,0xa5,sizeof(sentinel));
        vdc_priority_follow_snapshot_t out=sentinel;
        ++s_priority_follow_guard;
        assert(!vdc_dpll_manager_get_priority_follow(&out) && !memcmp(&out,&sentinel,sizeof(out)));
        --s_priority_follow_guard;
        assert(!vdc_dpll_manager_get_priority_follow(NULL));
        bool enabled=true;
        ++s_boundary_request;
        assert(!vdc_dpll_manager_try_priority_follow_enabled(&enabled) && enabled);
        --s_boundary_request;
        return;
    }
    if(!strcmp(name,"pending_busy_retry"))ring_available=false;
    else if(!strcmp(name,"stop_rx_busy")){ring.enabled=0;priority_rx_available=false;}
    else if(!strcmp(name,"age_rx_busy")){now_ms+=VDC_PRIORITY_FOLLOW_MAX_AGE_MS+1u;priority_rx_available=false;}
    else if(!strcmp(name,"observer_rx_busy")){++live.record.epoch;priority_rx_available=false;}
    else if(!strcmp(name,"raw_age"))raw_now+=(uint64_t)(VDC_PRIORITY_FOLLOW_MAX_AGE_MS+1u)*BOARD_SYS_CLOCK_HZ/1000u;
    else if(strcmp(name,"unguarded_apply") && strcmp(name,"domain_owner_rejection"))assert(0);
    if(!strcmp(name,"unguarded_apply"))priority_follow_apply_core1();else apply();
    assert(!status().applied && !memcmp(&before,&s_vdc_domain,sizeof(before)));
    if(!strcmp(name,"pending_busy_retry")) {
        assert(status().last_reason==VDC_PRIORITY_FOLLOW_BUSY);
        ring_available=true;apply();assert(status().applied==1u);
    } else if(!strcmp(name,"stop_rx_busy"))assert(status().last_reason==VDC_PRIORITY_FOLLOW_STOP);
    else if(!strcmp(name,"age_rx_busy") || !strcmp(name,"raw_age"))assert(status().last_reason==VDC_PRIORITY_FOLLOW_AGE);
    else if(!strcmp(name,"observer_rx_busy"))assert(status().last_reason==VDC_PRIORITY_FOLLOW_BINDING);
    else if(!strcmp(name,"unguarded_apply"))assert(status().last_reason==VDC_PRIORITY_FOLLOW_MODE);
    else assert(status().last_reason==VDC_PRIORITY_FOLLOW_DOMAIN && status().rejected==1u);
}
static void quality_event(uint32_t seq,uint64_t elapsed,uint32_t width)
{ event(seq,elapsed);priority_rx.typed_record.uncertainty_width=width; }
static void quality_two_seeds(void)
{
    quality_event(200u,100000000u,800u);tick();
    assert(status().baselines==2u && status().last_reason==VDC_PRIORITY_FOLLOW_BASELINE_QUALITY);
    assert(s_priority_follow_work.previous.sequence==200u && s_priority_follow_work.baseline_replacements==1u);
    quality_event(300u,200000000u,400u);tick();
    assert(status().baselines==3u && status().last_reason==VDC_PRIORITY_FOLLOW_BASELINE_QUALITY);
    assert(s_priority_follow_work.previous.sequence==300u && s_priority_follow_work.baseline_replacements==2u);
    assert(!status().prepared && !status().applied && !s_priority_follow_work.next_evaluation);
}
static void quality_test(const char *name)
{
    const char *kind=name+8;
    setup(!strcmp(kind,"apply_cleanup")?6000:0);
    priority_rx.typed_record.uncertainty_width=(!strncmp(kind,"odd_half",8)?1599u:1600u);
    tick();
    assert(status().baselines==1u && !s_priority_follow_work.baseline_replacements);
    const vdc_domain_context_t initial=s_vdc_domain;
    if(!strcmp(kind,"half_exact") || !strcmp(kind,"not_half") || !strcmp(kind,"same_width") ||
       !strcmp(kind,"odd_half_reject") || !strcmp(kind,"odd_half_accept") || !strcmp(kind,"zero_width")) {
        const uint32_t width=!strcmp(kind,"not_half")?801u:!strcmp(kind,"same_width")?1600u:
            !strcmp(kind,"odd_half_accept")?799u:!strcmp(kind,"zero_width")?0u:800u;
        quality_event(200u,100000000u,width);tick();
        const bool replace=!strcmp(kind,"half_exact") || !strcmp(kind,"odd_half_accept");
        assert(status().baselines==(replace?2u:1u));
        assert(s_priority_follow_work.previous.sequence==(replace?200u:100u));
        assert(s_priority_follow_work.baseline_replacements==(replace?1u:0u));
        assert(!status().prepared && !memcmp(&initial,&s_vdc_domain,sizeof(initial)));
        if(!replace) {
            /* No qualifying improvement is not a prerequisite for normal evaluation. */
            quality_event(300u,1010000000u,1600u);tick();
            assert(status().prepared==1u && status().baseline_sequence==100u);
        }
        return;
    }
    if(!strncmp(kind,"window_",7)) {
        quality_event(200u,250000000u,400u);
        priority_rx.typed_record.event_time_lower=UINT64_C(12000000000)+250000000u-400u+
            (!strcmp(kind,"window_after")?1u:0u)-(!strcmp(kind,"window_before")?1u:0u);
        tick();
        const bool replace=strcmp(kind,"window_after")!=0;
        assert(status().baselines==(replace?2u:1u));
        assert(s_priority_follow_work.previous.sequence==(replace?200u:100u));
        assert(!status().prepared && !memcmp(&initial,&s_vdc_domain,sizeof(initial)));return;
    }
    if(!strcmp(kind,"total_delay_bound")) {
        /* Two exact upper-window bounds, including source uncertainty. */
        quality_event(200u,249999200u,800u);tick();
        assert(status().baselines==2u && s_priority_follow_work.baseline_replacements==1u);
        quality_event(300u,499998800u,400u);tick();
        assert(status().baselines==3u && s_priority_follow_work.baseline_replacements==2u);
        quality_event(400u,749998600u,200u);tick();
        assert(status().baselines==3u && s_priority_follow_work.previous.sequence==300u);
        quality_event(500u,1499999200u,400u);tick();
        assert(status().prepared==1u && status().baseline_sequence==300u);
        assert(status().interval_lo==1000000000u);
        assert(priority_rx.typed_record.event_time_lower-UINT64_C(12000000000)<=UINT64_C(1500000000));
        return;
    }
    if(!strcmp(kind,"after_first") || !strcmp(kind,"busy_no_refund")) {
        quality_event(200u,1010000000u,1600u);prepare();
        assert(status().prepared==1u && s_priority_follow_work.next_evaluation==1u);
        if(!strcmp(kind,"busy_no_refund")) {
            ring_available=false;apply();assert(status().last_reason==VDC_PRIORITY_FOLLOW_BUSY);
            ring_available=true;
        } else apply();
        quality_event(300u,1100000000u,100u);tick();
        assert(status().baselines==1u && status().prepared==1u && s_priority_follow_work.previous.sequence==100u);
        assert(s_priority_follow_work.next_evaluation==1u && !s_priority_follow_work.baseline_replacements);
        return;
    }
    if(!strcmp(kind,"missing") || !strcmp(kind,"busy") || !strcmp(kind,"pending")) {
        quality_event(200u,100000000u,800u);
        if(!strcmp(kind,"missing"))priority_rx_available=false;
        else exact_result=!strcmp(kind,"busy")?TDMA_EVENT_EXACT_BUSY:TDMA_EVENT_EXACT_PENDING;
        tick();
        assert(status().baselines==1u && !s_priority_follow_work.baseline_replacements && !status().prepared);
        priority_rx_available=true;exact_result=TDMA_EVENT_EXACT_OK;tick();
        assert(status().baselines==2u && s_priority_follow_work.baseline_replacements==1u);
        assert(s_priority_follow_work.previous.sequence==200u);return;
    }
    quality_two_seeds();
    if(!strncmp(kind,"owner_",6)) {
        change_kind=kind+6;change_owner();
        quality_event(400u,240000000u,100u);tick();
        assert(!s_priority_follow_work.have_baseline && !s_priority_follow_work.baseline_replacements && !status().applied);
        assert(!memcmp(&initial.dco,&s_vdc_domain.dco,sizeof(initial.dco)));return;
    }
    if(!strcmp(kind,"two_replacements")) {
        quality_event(400u,240000000u,100u);tick();
        assert(status().baselines==3u && s_priority_follow_work.previous.sequence==300u &&
            s_priority_follow_work.baseline_replacements==2u);
        quality_event(500u,1210000000u,400u);tick();
        assert(status().prepared==1u && status().baseline_sequence==300u);
        assert(!memcmp(&initial,&s_vdc_domain,sizeof(initial)));return;
    }
    if(!strcmp(kind,"model_reset")) {
        ++s_vdc_domain.dco.period_adjust_ppb;publish();
        quality_event(400u,300000000u,1600u);tick();
        assert(status().baselines==4u && !s_priority_follow_work.baseline_replacements &&
            status().last_reason==VDC_PRIORITY_FOLLOW_MODEL);
        quality_event(500u,400000000u,800u);tick();
        assert(status().baselines==5u && s_priority_follow_work.baseline_replacements==1u &&
            status().last_reason==VDC_PRIORITY_FOLLOW_BASELINE_QUALITY);return;
    }
    if(!strcmp(kind,"stop_cleanup")) {
        ring.enabled=0u;tick();
        assert(!s_priority_follow_work.have_baseline && !s_priority_follow_work.baseline_replacements &&
            status().last_reason==VDC_PRIORITY_FOLLOW_STOP && !status().applied);
        ring.enabled=1u;quality_event(400u,300000000u,100u);tick();
        assert(!s_priority_follow_work.have_baseline && !status().applied);return;
    }
    if(!strcmp(kind,"apply_cleanup")) {
        quality_event(400u,1210000000u,400u);tick();
        assert(status().applied==1u && status().baseline_sequence==300u);
        assert(!s_priority_follow_work.have_baseline && !s_priority_follow_work.baseline_replacements);return;
    }
    if(!strcmp(kind,"final_baseline_reset")) {
        const uint64_t times[]={UINT64_C(1210000000),UINT64_C(1710000000),UINT64_C(2010000000),
            UINT64_C(4210000000),UINT64_C(8210000000)};
        for(unsigned i=0;i<5u;++i) {
            quality_event(400u+i,times[i],400u);tick();
            assert(status().prepared==i+1u && status().baseline_sequence==300u && !status().applied);
        }
        assert(s_priority_follow_work.previous.sequence==404u && !s_priority_follow_work.baseline_replacements);
        quality_event(500u,8310000000u,200u);tick();
        assert(s_priority_follow_work.previous.sequence==500u && s_priority_follow_work.baseline_replacements==1u &&
            status().last_reason==VDC_PRIORITY_FOLLOW_BASELINE_QUALITY);return;
    }
    assert(0);
}
int main(int argc,char **argv)
{
    (void)match_publish;
    assert(argc==2);
    if(!strcmp(argv[1],"sizes"))printf("%zu %zu %zu\n",sizeof(vdc_priority_follow_work_t),
        sizeof(vdc_priority_follow_snapshot_t),sizeof(vdc_priority_follow_ticket_t));
    else if(!strcmp(argv[1],"interval_limit"))printf("%" PRIu64 "\n",VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS);
    else if(!strcmp(argv[1],"ratio")) {
        uint64_t llo,lhi,rlo,rhi;
        while(scanf("%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,&llo,&lhi,&rlo,&rhi)==4) {
            int64_t lo=123,hi=456;
            uint32_t reason=priority_follow_rate_interval(llo,lhi,rlo,rhi,&lo,&hi);
            printf("%u %" PRId64 " %" PRId64 "\n",reason,lo,hi);
        }
    } else if(!strcmp(argv[1],"delta")) {
        int64_t lo,hi;int32_t current;uint32_t limit;
        while(scanf("%" SCNd64 " %" SCNd64 " %" SCNd32 " %" SCNu32,&lo,&hi,&current,&limit)==4)
            printf("%" PRId32 "\n",priority_follow_delta(lo,hi,current,limit));
    } else if(!strncmp(argv[1],"quality_",8))quality_test(argv[1]);
    else if(!strncmp(argv[1],"cancel_",7))cancellation_test(argv[1]);
    else if(!strncmp(argv[1],"apply_",6) || !strcmp(argv[1],"model_revision") ||
            !strcmp(argv[1],"domain_rejection") || !strcmp(argv[1],"mode_exclusive") ||
            !strcmp(argv[1],"remote_command_control"))validity_test(argv[1]);
    else if(!strcmp(argv[1],"pending_busy_retry") || !strcmp(argv[1],"stop_rx_busy") ||
            !strcmp(argv[1],"age_rx_busy") || !strcmp(argv[1],"observer_rx_busy") ||
            !strcmp(argv[1],"raw_age") || !strcmp(argv[1],"unguarded_apply") ||
            !strcmp(argv[1],"domain_owner_rejection") || !strcmp(argv[1],"request_authorization") ||
            !strcmp(argv[1],"snapshot_busy"))contention_test(argv[1]);
    else flow_test(argv[1]);
    return 0;
}
'''
