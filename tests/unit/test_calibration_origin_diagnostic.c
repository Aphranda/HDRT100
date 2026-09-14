#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "calibration_origin_timing.h"
#include "tdma_service.h"
#include "tdma_origin_plan.h"
#include "ota_crc32.h"
#include "pota_types.h"

enum { CALL_RING, CALL_CONFIG, CALL_STAGE, CALL_CAPABILITY, CALL_OWNER,
       CALL_OWNER_SNAPSHOT, CALL_CADENCE, CALL_CLOCK, CALL_CRC, CALL_ADMISSION, CALL_MODEL };
static unsigned trace[32], trace_count, ring_calls;
static uint32_t observed_mask;
static bool require_revoked;
static void called(unsigned id);
static struct {
    tdma_ring_runtime_snapshot_t ring[2];
    tdma_ring_runtime_config_t config;
    tdma_ring_calibration_stage_t stage;
    refmem_realtime_origin_capability_t capability;
    tdma_service_snapshot_t owner;
    bool ring_available[2], config_available, stage_available, complete;
    bool capability_available, owner_available, admission_available;
    uint32_t admitted_epoch, current_epoch;
    uint64_t ticks;
} model;
static tdma_service_service_t owner;

static bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *out)
{
    called(CALL_RING);
    const unsigned index = ring_calls++ == 0u ? 0u : 1u;
    if (!model.ring_available[index]) { memset(out, 0xa5, sizeof(*out)); return false; }
    *out = model.ring[index];
    observed_mask |= index == 0u ? CALIBRATION_ORIGIN_OBS_RING : CALIBRATION_ORIGIN_OBS_RECHECK;
    return true;
}
static bool tdma_runtime_owner_get_staged_ring_config(tdma_ring_runtime_config_t *out)
{
    called(CALL_CONFIG);
    if (!model.config_available) { memset(out, 0xa5, sizeof(*out)); return false; }
    *out = model.config; observed_mask |= CALIBRATION_ORIGIN_OBS_CONFIG; return true;
}
static bool tdma_runtime_owner_get_calibration_stage(tdma_ring_calibration_stage_t *out, bool *complete)
{
    called(CALL_STAGE);
    if (!model.stage_available) { memset(out, 0xa5, sizeof(*out)); return false; }
    *out = model.stage; *complete = model.complete;
    observed_mask |= CALIBRATION_ORIGIN_OBS_STAGE; return true;
}
static bool tdma_runtime_owner_get_origin_capability(refmem_realtime_origin_capability_t *out)
{
    called(CALL_CAPABILITY);
    if (!model.capability_available) { memset(out, 0xa5, sizeof(*out)); return false; }
    *out = model.capability; observed_mask |= CALIBRATION_ORIGIN_OBS_CAPABILITY; return true;
}
static tdma_service_service_t *tdma_runtime_owner_get(void) { called(CALL_OWNER); return &owner; }
bool tdma_service_get_snapshot(const tdma_service_service_t *instance, tdma_service_snapshot_t *out)
{
    called(CALL_OWNER_SNAPSHOT); assert(instance == &owner);
    if (!model.owner_available) { memset(out, 0xa5, sizeof(*out)); return false; }
    *out = model.owner; observed_mask |= CALIBRATION_ORIGIN_OBS_OWNER; return true;
}
static uint64_t vdc_timestamp_clock_read_ticks64(void)
{ called(CALL_CLOCK); observed_mask |= CALIBRATION_ORIGIN_OBS_CLOCK; return model.ticks; }
uint32_t ota_crc32_compute(const uint8_t *data, size_t size)
{ called(CALL_CRC); return pota_crc32_compute(data, size); }
bool refmem_realtime_contract_admit_origin_trial(const refmem_realtime_origin_capability_t *capability,
    uint32_t crc, refmem_realtime_origin_admission_t *out)
{
    called(CALL_ADMISSION);
    if (!model.admission_available) { memset(out, 0xa5, sizeof(*out)); return false; }
    *out = (refmem_realtime_origin_admission_t){.model_epoch = model.admitted_epoch,
        .foundation_crc32 = crc, .diagnostic_valid = 1u, .capability = *capability,
        .product_reject_mask = capability->product_reject_mask};
    observed_mask |= CALIBRATION_ORIGIN_OBS_ADMISSION; return true;
}
uint32_t refmem_realtime_contract_origin_model_epoch(void)
{ called(CALL_MODEL); observed_mask |= CALIBRATION_ORIGIN_OBS_MODEL_EPOCH; return model.current_epoch; }
#include "origin_cadence.inc"
bool tdma_origin_cadence_calculate(uint32_t hz, uint32_t baud, uint32_t bytes, uint32_t ns,
                                  tdma_origin_cadence_t *out)
{
    called(CALL_CADENCE);
    const bool result = production_cadence(hz, baud, bytes, ns, out);
    if (result) observed_mask |= CALIBRATION_ORIGIN_OBS_CADENCE;
    return result;
}
#include "../../components/calibration_manager/src/calibration_origin_timing.inc"
static void called(unsigned id)
{
    assert(trace_count < sizeof(trace) / sizeof(trace[0]));
    trace[trace_count++] = id;
    if (require_revoked) assert(s_origin_timing.enabled == 0u);
}

/* Frozen production function from 5799b7f253f0b3dc4dd338483986baf15ff8f5b3,
 * components/calibration_manager/src/calibration_origin_timing.inc.
 * Only the function name differs. Do not update this oracle to match the new
 * diagnostic implementation: it independently fixes authorization semantics. */
static bool baseline_trial(uint32_t trial_id, uint32_t rearm_budget_ticks,
    uint32_t abort_poll_count, uint64_t duration_ticks, uint32_t diagnostic_flags)
{
    /* A rejected replacement must not leave the previous trial authorized. */
    calibration_manager_origin_revoke();
    tdma_ring_runtime_snapshot_t ring;
    tdma_ring_calibration_stage_t stage;
    tdma_service_ring_runtime_config_t config;
    refmem_realtime_origin_capability_t capability;
    tdma_service_snapshot_t owner;
    bool complete = false;
    /* The omission trial needs the archive: NORECORD and BLACKOUT cannot mix. */
    if ((diagnostic_flags != 0u &&
         diagnostic_flags != CALIBRATION_ORIGIN_DIAGNOSTIC_SKIP_RECORDS &&
         diagnostic_flags != CALIBRATION_ORIGIN_DIAGNOSTIC_SERVICE_BLACKOUT &&
         diagnostic_flags != CALIBRATION_ORIGIN_DIAGNOSTIC_BUILD_CANCEL) ||
        trial_id == 0u || rearm_budget_ticks == 0u || abort_poll_count == 0u ||
        abort_poll_count > UINT16_MAX || duration_ticks == 0u || duration_ticks > INT64_MAX ||
        !tdma_runtime_owner_get_ring_snapshot(&ring) || ring.enabled == 0u ||
        ring.adapter_started == 0u || ring.config_seq != ring.applied_config_seq ||
        !tdma_runtime_owner_get_staged_ring_config(&config) ||
        (config.flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) == 0u ||
        config.local_slot_id != config.reference_slot_id ||
        !tdma_runtime_owner_get_calibration_stage(&stage, &complete) || !complete ||
        !tdma_runtime_owner_get_origin_capability(&capability) ||
        !tdma_service_get_snapshot(tdma_runtime_owner_get(), &owner) ||
        stage.node_count != config.node_count || stage.profile_crc32 != config.operating_profile_crc32 ||
        stage.schedule_crc32 != config.schedule_crc32) return false;
    tdma_origin_cadence_t cadence;
    if (!tdma_origin_cadence_calculate(capability.clk_sys_hz, config.baud_hz,
            capability.physical_bytes, config.cycle_period_ns, &cadence) ||
        cadence.guard_floor_ticks <= rearm_budget_ticks) return false;
    const uint64_t now = vdc_timestamp_clock_read_ticks64();
    if (now > UINT64_MAX - duration_ticks) return false;
    calibration_origin_timing_t record = {
        .version = CALIBRATION_ORIGIN_TIMING_VERSION, .enabled = 1u, .trial_id = trial_id,
        .config_seq = ring.config_seq, .calibration_generation = stage.calibration_generation,
        .topology_generation = stage.topology_generation, .topology_crc32 = stage.topology_crc32,
        .calibration_crc32 = ota_crc32_compute((const uint8_t *)&stage, sizeof(stage)),
        .rearm_budget_ticks = rearm_budget_ticks, .abort_poll_count = abort_poll_count,
        .expires_ticks = now + duration_ticks, .config = config,
        .diagnostic_flags = diagnostic_flags};
    if (!refmem_realtime_contract_admit_origin_trial(&capability, owner.foundation_profile_crc32,
            &record.admission)) return false;
    tdma_ring_runtime_snapshot_t current;
    if (!tdma_runtime_owner_get_ring_snapshot(&current) || !current.enabled ||
        !current.adapter_started || current.config_seq != ring.config_seq ||
        current.applied_config_seq != ring.config_seq ||
        record.admission.model_epoch != refmem_realtime_contract_origin_model_epoch()) return false;
    (void)__atomic_add_fetch(&s_origin_timing_guard, 1u, __ATOMIC_ACQ_REL);
    record.epoch = s_origin_timing_guard + 1u;
    s_origin_timing = record;
    (void)__atomic_add_fetch(&s_origin_timing_guard, 1u, __ATOMIC_RELEASE);
    return true;
}

static void clear_trace(void) { trace_count = ring_calls = observed_mask = 0u; }
static void setup(void)
{
    memset(&model, 0, sizeof(model));
    model.ring[0] = model.ring[1] = (tdma_ring_runtime_snapshot_t){
        .enabled = 1u, .adapter_started = 1u, .config_seq = 14u, .applied_config_seq = 14u};
    model.config = (tdma_ring_runtime_config_t){.node_count = 4u,
        .local_slot_id = 2u, .reference_slot_id = 2u, .baud_hz = 10000000u,
        .cycle_period_ns = 1500000u, .flags = TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE,
        .operating_profile_crc32 = 101u, .schedule_crc32 = 202u};
    model.stage = (tdma_ring_calibration_stage_t){.node_count = 4u,
        .profile_crc32 = 101u, .schedule_crc32 = 202u, .calibration_generation = 303u,
        .topology_generation = 404u, .topology_crc32 = 505u};
    model.capability = (refmem_realtime_origin_capability_t){
        .clk_sys_hz = 150000000u, .physical_bytes = 300u, .product_reject_mask = 7u};
    model.owner.foundation_profile_crc32 = 606u;
    model.ring_available[0] = model.ring_available[1] = model.config_available = true;
    model.stage_available = model.complete = model.capability_available = true;
    model.owner_available = model.admission_available = true;
    model.admitted_epoch = model.current_epoch = 808u;
    model.ticks = 100u;
    clear_trace(); require_revoked = true;
}
static void fresh(void)
{
    setup(); s_origin_timing_guard = 0u;
    memset(&s_origin_timing, 0, sizeof(s_origin_timing));
    memset(&s_origin_attempt, 0, sizeof(s_origin_attempt));
}
static void authorized(void)
{
    fresh(); assert(calibration_manager_origin_trial(1u, 100u, 8u, 1000000u));
    assert(s_origin_timing.enabled && s_origin_timing.epoch == 4u);
    setup();
}

typedef struct {
    uint32_t trial, rearm, polls, flags, reason;
    uint64_t duration, observed, expected;
} request_t;
enum { CASE_GOOD, CASE_FLAGS_MIX, CASE_FLAGS_UNKNOWN, CASE_TRIAL_ZERO, CASE_REARM_ZERO,
    CASE_POLLS_ZERO, CASE_POLLS_HIGH, CASE_DURATION_ZERO, CASE_DURATION_HIGH,
    CASE_RING_UNAVAILABLE, CASE_RING_DISABLED, CASE_RING_NOT_STARTED, CASE_CONFIG_PENDING,
    CASE_CONFIG_UNAVAILABLE, CASE_DIAGNOSTIC_REQUIRED, CASE_ROLE, CASE_STAGE_UNAVAILABLE,
    CASE_STAGE_INCOMPLETE, CASE_CAPABILITY_UNAVAILABLE, CASE_OWNER_UNAVAILABLE,
    CASE_STAGE_NODES, CASE_STAGE_PROFILE, CASE_STAGE_SCHEDULE, CASE_CADENCE_HZ,
    CASE_CADENCE_BAUD, CASE_CADENCE_BYTES, CASE_CADENCE_PERIOD, CASE_CADENCE_DIVIDER,
    CASE_CADENCE_GUARD, CASE_REARM_EQUAL, CASE_REARM_EXCEEDS, CASE_EXPIRY,
    CASE_MODEL_REJECTED, CASE_RECHECK_UNAVAILABLE, CASE_RECHECK_DISABLED,
    CASE_RECHECK_NOT_STARTED, CASE_RECHECK_CONFIG, CASE_RECHECK_APPLIED, CASE_MODEL_CHANGED,
    CASE_NONBOOLEAN_TRUE, CASE_FLAGS_NORECORD, CASE_FLAGS_BLACKOUT, CASE_FLAGS_BUILDCANCEL,
    CASE_POLLS_MAX, CASE_DURATION_MAX, CASE_EXPIRY_EXACT, CASE_REARM_BELOW, CASE_COUNT };
static request_t configure(unsigned id)
{
    request_t request = {.trial = 11u, .rearm = 100u, .polls = 8u,
        .duration = 1000000u, .reason = CALIBRATION_ORIGIN_ATTEMPT_ACCEPTED};
    tdma_origin_cadence_t cadence;
    assert(production_cadence(model.capability.clk_sys_hz, model.config.baud_hz,
        model.capability.physical_bytes, model.config.cycle_period_ns, &cadence));
    assert(cadence.guard_floor_ticks < UINT32_MAX);
#define REJECT(reason_, observed_, expected_) do { \
    request.reason = CALIBRATION_ORIGIN_ATTEMPT_##reason_; \
    request.observed = (observed_); request.expected = (expected_); } while (0)
    switch (id) {
    case CASE_GOOD: break;
    case CASE_FLAGS_MIX: request.flags = 3u; REJECT(ARGUMENT, 0u, 0u); break;
    case CASE_FLAGS_UNKNOWN: request.flags = 8u; REJECT(ARGUMENT, 0u, 0u); break;
    case CASE_TRIAL_ZERO: request.trial = 0u; REJECT(ARGUMENT, 0u, 0u); break;
    case CASE_REARM_ZERO: request.rearm = 0u; REJECT(ARGUMENT, 0u, 0u); break;
    case CASE_POLLS_ZERO: request.polls = 0u; REJECT(ARGUMENT, 0u, 0u); break;
    case CASE_POLLS_HIGH: request.polls = UINT16_MAX + 1u; REJECT(ARGUMENT, 0u, 0u); break;
    case CASE_DURATION_ZERO: request.duration = 0u; REJECT(ARGUMENT, 0u, 0u); break;
    case CASE_DURATION_HIGH: request.duration = (uint64_t)INT64_MAX + 1u; REJECT(ARGUMENT, 0u, 0u); break;
    case CASE_RING_UNAVAILABLE: model.ring_available[0] = false; REJECT(RING_UNAVAILABLE, 0u, 0u); break;
    case CASE_RING_DISABLED: model.ring[0].enabled = 0u; REJECT(RING_DISABLED, 0u, 1u); break;
    case CASE_RING_NOT_STARTED: model.ring[0].adapter_started = 0u; REJECT(RING_NOT_STARTED, 0u, 1u); break;
    case CASE_CONFIG_PENDING: model.ring[0].applied_config_seq = 12u; REJECT(CONFIG_PENDING, 12u, 14u); break;
    case CASE_CONFIG_UNAVAILABLE: model.config_available = false; REJECT(CONFIG_UNAVAILABLE, 0u, 0u); break;
    case CASE_DIAGNOSTIC_REQUIRED: model.config.flags = 0u; REJECT(DIAGNOSTIC_REQUIRED, 0u, TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE); break;
    case CASE_ROLE: model.config.local_slot_id = 1u; REJECT(ROLE, 1u, 2u); break;
    case CASE_STAGE_UNAVAILABLE: model.stage_available = false; REJECT(STAGE_UNAVAILABLE, 0u, 0u); break;
    case CASE_STAGE_INCOMPLETE: model.complete = false; REJECT(STAGE_INCOMPLETE, 0u, 1u); break;
    case CASE_CAPABILITY_UNAVAILABLE: model.capability_available = false; REJECT(CAPABILITY_UNAVAILABLE, 0u, 0u); break;
    case CASE_OWNER_UNAVAILABLE: model.owner_available = false; REJECT(OWNER_UNAVAILABLE, 0u, 0u); break;
    case CASE_STAGE_NODES: model.stage.node_count = 6u; REJECT(STAGE_NODES, 6u, 4u); break;
    case CASE_STAGE_PROFILE: model.stage.profile_crc32 = 99u; REJECT(STAGE_PROFILE, 99u, 101u); break;
    case CASE_STAGE_SCHEDULE: model.stage.schedule_crc32 = 199u; REJECT(STAGE_SCHEDULE, 199u, 202u); break;
    case CASE_CADENCE_HZ: model.capability.clk_sys_hz = 0u; REJECT(CADENCE, 0u, 0u); break;
    case CASE_CADENCE_BAUD: model.config.baud_hz = 0u; REJECT(CADENCE, 0u, 0u); break;
    case CASE_CADENCE_BYTES: model.capability.physical_bytes = 8193u; REJECT(CADENCE, 0u, 0u); break;
    case CASE_CADENCE_PERIOD: model.config.cycle_period_ns = 0u; REJECT(CADENCE, 0u, 0u); break;
    case CASE_CADENCE_DIVIDER: model.config.baud_hz = model.capability.clk_sys_hz; REJECT(CADENCE, 0u, 0u); break;
    case CASE_CADENCE_GUARD: model.config.cycle_period_ns = 1000u; REJECT(CADENCE, 0u, 0u); break;
    case CASE_REARM_EQUAL: request.rearm = (uint32_t)cadence.guard_floor_ticks;
        REJECT(REARM_MARGIN, cadence.guard_floor_ticks, request.rearm); break;
    case CASE_REARM_EXCEEDS: request.rearm = (uint32_t)cadence.guard_floor_ticks + 1u;
        REJECT(REARM_MARGIN, cadence.guard_floor_ticks, request.rearm); break;
    case CASE_EXPIRY: model.ticks = UINT64_MAX - request.duration + 1u;
        REJECT(EXPIRY, model.ticks, UINT64_MAX - request.duration); break;
    case CASE_MODEL_REJECTED: model.admission_available = false; REJECT(MODEL_REJECTED, 0u, 0u); break;
    case CASE_RECHECK_UNAVAILABLE: model.ring_available[1] = false; REJECT(RECHECK_UNAVAILABLE, 0u, 0u); break;
    case CASE_RECHECK_DISABLED: model.ring[1].enabled = 0u; REJECT(RECHECK_DISABLED, 0u, 1u); break;
    case CASE_RECHECK_NOT_STARTED: model.ring[1].adapter_started = 0u; REJECT(RECHECK_NOT_STARTED, 0u, 1u); break;
    case CASE_RECHECK_CONFIG: model.ring[1].config_seq = 16u; REJECT(RECHECK_CONFIG, 16u, 14u); break;
    case CASE_RECHECK_APPLIED: model.ring[1].applied_config_seq = 12u; REJECT(RECHECK_APPLIED, 12u, 14u); break;
    case CASE_MODEL_CHANGED: model.current_epoch = 810u; REJECT(MODEL_CHANGED, 810u, 808u); break;
    case CASE_NONBOOLEAN_TRUE: model.ring[0].enabled = model.ring[1].enabled = 2u;
        model.ring[0].adapter_started = model.ring[1].adapter_started = 3u;
        model.config.flags |= 0x80000000u; break;
    case CASE_FLAGS_NORECORD: request.flags = CALIBRATION_ORIGIN_DIAGNOSTIC_SKIP_RECORDS; break;
    case CASE_FLAGS_BLACKOUT: request.flags = CALIBRATION_ORIGIN_DIAGNOSTIC_SERVICE_BLACKOUT; break;
    case CASE_FLAGS_BUILDCANCEL: request.flags = CALIBRATION_ORIGIN_DIAGNOSTIC_BUILD_CANCEL; break;
    case CASE_POLLS_MAX: request.polls = UINT16_MAX; break;
    case CASE_DURATION_MAX: request.duration = INT64_MAX; break;
    case CASE_EXPIRY_EXACT: model.ticks = UINT64_MAX - request.duration; break;
    case CASE_REARM_BELOW: request.rearm = (uint32_t)cadence.guard_floor_ticks - 1u; break;
    default: assert(false);
    }
#undef REJECT
    return request;
}

static bool invoke(request_t request, bool baseline)
{
    return baseline ? baseline_trial(request.trial, request.rearm, request.polls, request.duration, request.flags) :
        calibration_manager_origin_trial_configured(request.trial, request.rearm, request.polls, request.duration, request.flags);
}
static void assert_timing_equal(const calibration_origin_timing_t *a, const calibration_origin_timing_t *b)
{
#define EQUAL(field) assert(a->field == b->field)
    EQUAL(version); EQUAL(epoch); EQUAL(enabled); EQUAL(trial_id); EQUAL(config_seq);
    EQUAL(calibration_generation); EQUAL(topology_generation); EQUAL(topology_crc32);
    EQUAL(calibration_crc32); EQUAL(rearm_budget_ticks); EQUAL(abort_poll_count);
    EQUAL(expires_ticks); EQUAL(diagnostic_flags);
#undef EQUAL
    assert(memcmp(&a->config, &b->config, sizeof(a->config)) == 0);
    assert(memcmp(&a->admission, &b->admission, sizeof(a->admission)) == 0);
}
static void assert_attempt(const request_t *request, const calibration_origin_attempt_t *attempt)
{
    assert(attempt->attempt == 2u && attempt->trial_id == request->trial);
    assert(attempt->reason == request->reason);
    assert(attempt->observed == request->observed && attempt->expected == request->expected);
    assert(attempt->observed_mask == observed_mask);
    assert(attempt->trial_epoch == s_origin_timing.epoch);
    assert(attempt->requested_rearm_ticks == request->rearm);
    assert(attempt->requested_abort_polls == request->polls);
    assert(attempt->requested_flags == request->flags);
    assert(attempt->requested_duration_ticks == request->duration);
    assert(attempt->config_seq == ((observed_mask & CALIBRATION_ORIGIN_OBS_RING) ? model.ring[0].config_seq : 0u));
    assert(attempt->applied_config_seq == ((observed_mask & CALIBRATION_ORIGIN_OBS_RING) ? model.ring[0].applied_config_seq : 0u));
    assert(attempt->recheck_config_seq == ((observed_mask & CALIBRATION_ORIGIN_OBS_RECHECK) ? model.ring[1].config_seq : 0u));
    assert(attempt->recheck_applied_config_seq == ((observed_mask & CALIBRATION_ORIGIN_OBS_RECHECK) ? model.ring[1].applied_config_seq : 0u));
    assert(attempt->admitted_model_epoch == ((observed_mask & CALIBRATION_ORIGIN_OBS_ADMISSION) ? model.admitted_epoch : 0u));
    assert(attempt->observed_model_epoch == ((observed_mask & CALIBRATION_ORIGIN_OBS_MODEL_EPOCH) ? model.current_epoch : 0u));
}
static void test_oracle(void)
{
    for (unsigned id = 0u; id < CASE_COUNT; ++id) {
        authorized(); request_t request = configure(id);
        const bool old_result = invoke(request, true);
        calibration_origin_timing_t old_timing = s_origin_timing;
        const unsigned old_count = trace_count;
        unsigned old_trace[32]; memcpy(old_trace, trace, sizeof(trace));
        authorized(); request = configure(id);
        const bool result = invoke(request, false);
        assert(result == old_result);
        assert(result == (request.reason == CALIBRATION_ORIGIN_ATTEMPT_ACCEPTED));
        assert_timing_equal(&old_timing, &s_origin_timing);
        assert(trace_count == old_count && memcmp(trace, old_trace, trace_count * sizeof(trace[0])) == 0);
        assert(s_origin_timing_guard == (result ? 8u : 6u));
        assert(s_origin_timing.enabled == (result ? 1u : 0u));
        calibration_origin_attempt_t attempt;
        memset(&attempt, 0xa5, sizeof(attempt));
        assert(calibration_manager_origin_get_attempt(&attempt));
        assert_attempt(&request, &attempt);
    }
    printf("origin diagnostic: %u frozen-oracle cases, unchanged grant/epoch/helper order, exact reasons/masks passed\n", CASE_COUNT);
}

static void test_lifetime(void)
{
    fresh(); calibration_origin_attempt_t attempt;
    assert(!calibration_manager_origin_get_attempt(NULL));
    assert(!calibration_manager_origin_get_attempt(&attempt));
    authorized(); request_t bad = configure(CASE_RECHECK_APPLIED);
    assert(!invoke(bad, false));
    assert(calibration_manager_origin_get_attempt(&attempt));
    calibration_origin_attempt_t saved = attempt;
    calibration_manager_origin_revoke();
    calibration_manager_origin_revoke();
    model.ring[0].enabled = model.ring[0].adapter_started = 0u;
    assert(calibration_manager_origin_get_attempt(&attempt));
    assert(memcmp(&saved, &attempt, sizeof(attempt)) == 0);
    assert(attempt.trial_epoch != calibration_manager_origin_epoch() && !s_origin_timing.enabled);
    setup();
    assert(calibration_manager_origin_trial(99u, 100u, 8u, 1000000u));
    assert(calibration_manager_origin_get_attempt(&attempt));
    assert(attempt.attempt == saved.attempt + 1u && attempt.trial_id == 99u);
    assert(attempt.reason == CALIBRATION_ORIGIN_ATTEMPT_ACCEPTED);
    assert(attempt.observed == 0u && attempt.expected == 0u);
    assert(attempt.recheck_applied_config_seq == 14u);
    saved = attempt; calibration_manager_origin_revoke();
    assert(calibration_manager_origin_get_attempt(&attempt));
    assert(memcmp(&saved, &attempt, sizeof(attempt)) == 0 && !s_origin_timing.enabled);
    setup(); s_origin_attempt.attempt = UINT32_MAX - 1u;
    assert(!calibration_manager_origin_trial(0u, 100u, 8u, 1000000u));
    assert(s_origin_attempt.attempt == UINT32_MAX);
    assert(calibration_manager_origin_trial(77u, 100u, 8u, 1000000u));
    assert(s_origin_attempt.attempt == UINT32_MAX && s_origin_attempt.trial_id == 77u);
    puts("origin diagnostic lifetime: replacement/revoke/new-attempt, prior-authority separation, saturation passed");
}

typedef int scpi_result_t;
enum { TRUE = 1, SCPI_RES_OK = 1, SCPI_RES_ERR = -1 };
typedef struct {
    uint64_t parameters[4];
    unsigned available, parsed, fields, numbers, errors;
    uint64_t values[32];
    char output[2048], error[128];
} scpi_t;
static bool SCPI_ParamUInt32(scpi_t *context, uint32_t *value, int required)
{
    assert(required == TRUE);
    if (context->parsed >= context->available || context->parameters[context->parsed] > UINT32_MAX) return false;
    *value = (uint32_t)context->parameters[context->parsed++]; return true;
}
static bool SCPI_ParamUInt64(scpi_t *context, uint64_t *value, int required)
{
    assert(required == TRUE);
    if (context->parsed >= context->available) return false;
    *value = context->parameters[context->parsed++]; return true;
}
static void SCPI_ResultText(scpi_t *context, const char *text)
{
    const size_t size = strlen(context->output);
    const int wrote = snprintf(context->output + size, sizeof(context->output) - size,
        "%s%s", context->fields++ ? "," : "", text);
    assert(wrote >= 0 && (size_t)wrote < sizeof(context->output) - size);
}
static void SCPI_ResultUInt64(scpi_t *context, uint64_t value)
{
    assert(context->numbers < sizeof(context->values) / sizeof(context->values[0]));
    context->values[context->numbers++] = value;
    char number[32]; snprintf(number, sizeof(number), "%" PRIu64, value);
    SCPI_ResultText(context, number);
}
static void SCPI_ResultUInt32(scpi_t *context, uint32_t value) { SCPI_ResultUInt64(context, value); }
static void scpi_port_push_exec_error(scpi_t *context, const char *message)
{ ++context->errors; snprintf(context->error, sizeof(context->error), "%s", message); }
static scpi_result_t scpi_port_result_ok(scpi_t *context)
{ SCPI_ResultText(context, "OK"); return SCPI_RES_OK; }
#include "origin_scpi.inc"

static void assert_unavailable(void)
{
    clear_trace(); require_revoked = false;
    scpi_t context = {0}; assert(scpi_calibration_origin_diagnostic_q(&context) == SCPI_RES_OK);
    assert(!strcmp(context.output, "UNAVAILABLE") && context.numbers == 0u && context.errors == 0u);
}
static void test_scpi(void)
{
    fresh(); model.ring[0].enabled = model.ring[0].adapter_started = 0u;
    assert_unavailable();
    scpi_result_t (*callbacks[])(scpi_t *) = {scpi_calibration_origin_trial,
        scpi_calibration_origin_trial_no_record, scpi_calibration_origin_trial_blackout,
        scpi_calibration_origin_trial_build_cancel};
    const uint32_t flags[] = {0u, 1u, 2u, 4u};
    for (unsigned i = 0u; i < 4u; ++i) {
        fresh(); scpi_t context = {.parameters = {17u, 100u, 8u, 1000000u}, .available = 4u};
        assert(callbacks[i](&context) == SCPI_RES_OK);
        assert(context.errors == 0u && context.numbers == 1u && context.fields == 1u);
        assert(context.values[0] == calibration_manager_origin_epoch() && !strcmp(context.output, "4"));
        assert(s_origin_attempt.requested_flags == flags[i] && s_origin_timing.diagnostic_flags == flags[i]);
        assert_unavailable(); /* Running admission is not a STOP/ACK diagnostic query. */
    }
    for (unsigned missing = 0u; missing < 4u; ++missing) {
        authorized(); calibration_origin_attempt_t before = s_origin_attempt;
        const uint32_t epoch = calibration_manager_origin_epoch();
        scpi_t context = {.parameters = {9u, 100u, 8u, 1000000u}, .available = missing};
        assert(scpi_calibration_origin_trial(&context) == SCPI_RES_ERR);
        assert(context.fields == 0u && context.errors == 0u && trace_count == 0u);
        assert(calibration_manager_origin_epoch() == epoch && s_origin_timing.enabled);
        assert(memcmp(&before, &s_origin_attempt, sizeof(before)) == 0);
    }
    authorized(); setup(); model.ticks = UINT64_MAX - 10u;
    scpi_t context = {.parameters = {UINT32_MAX, 100u, 8u, UINT64_C(1) << 40}, .available = 4u};
    assert(scpi_calibration_origin_trial(&context) == SCPI_RES_ERR);
    assert(context.fields == 0u && context.numbers == 0u && context.errors == 1u);
    assert(!strcmp(context.error, "CAL_ORIGIN_TRIAL_REJECTED") && !s_origin_timing.enabled);
    calibration_origin_attempt_t saved = s_origin_attempt;
    assert(saved.reason == CALIBRATION_ORIGIN_ATTEMPT_EXPIRY && saved.attempt == 2u);
    require_revoked = false; clear_trace(); memset(&context, 0, sizeof(context));
    assert(scpi_calibration_origin_revoke(&context) == SCPI_RES_OK && !strcmp(context.output, "OK"));
    assert(memcmp(&saved, &s_origin_attempt, sizeof(saved)) == 0);
    setup(); require_revoked = false;
    model.ring_available[0] = false; assert_unavailable();
    setup(); model.ring[0].enabled = 0u; assert_unavailable(); /* Adapter still live. */
    setup(); model.ring[0].adapter_started = 0u; assert_unavailable(); /* Ring still live. */
    setup(); model.ring[0].enabled = model.ring[0].adapter_started = 0u;
    model.ring[0].applied_config_seq = 12u; assert_unavailable();
    setup(); require_revoked = false;
    model.ring[0].enabled = model.ring[0].adapter_started = 0u;
    model.ring[0].config_seq = model.ring[0].applied_config_seq = 900u;
    memset(&context, 0, sizeof(context));
    assert(scpi_calibration_origin_diagnostic_q(&context) == SCPI_RES_OK);
    const uint64_t expected[] = {1u, saved.attempt, saved.trial_id, saved.trial_epoch,
        saved.reason, 14u, 14u, 0u, 0u, 0u, 0u, UINT64_MAX - 10u,
        UINT64_MAX - (UINT64_C(1) << 40), saved.observed_mask, 100u, 8u, 0u, UINT64_C(1) << 40};
    assert(context.numbers == sizeof(expected) / sizeof(expected[0]) && context.fields == 19u);
    assert(memcmp(context.values, expected, sizeof(expected)) == 0);
    assert(strncmp(context.output, "ORIGINADMISSION,1,", 18u) == 0);
    assert(s_origin_timing.enabled == 0u && saved.trial_epoch != calibration_manager_origin_epoch());
    assert(memcmp(&saved, &s_origin_attempt, sizeof(saved)) == 0);
    printf("origin diagnostic SCPI wire: %s\n", context.output);
    puts("origin diagnostic SCPI: callbacks, parse failures, ERR without epoch, STOP/ACK gates, original-attempt 64-bit wire passed");
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "oracle")) test_oracle();
    else if (!strcmp(argv[1], "lifetime")) test_lifetime();
    else if (!strcmp(argv[1], "scpi")) test_scpi();
    else return 2;
    return 0;
}
