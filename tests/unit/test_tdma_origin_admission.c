#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "calibration_origin_timing.h"
#include "tdma_service.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "ota_crc32.h"
#include "tdma_origin_blackout.h"
#include "tdma_service_timing.h"
#include "tdma_origin_build_job.h"
#include "tdma_origin_handoff.h"
#include "pota_types.h"

/* Host device facade; no hardware ownership is exercised in this fixture. */
enum { clk_sys, BOARD_TDMA_TX_PIO_BLOCK_ID = 1, BOARD_TDMA_RX_PIO_BLOCK_ID = 2,
       TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN = 11,
       TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN = 16,
       TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK = 3,
       TDMA_STATE_MACHINE_ORIGIN_ADDITIONAL_RESOURCE_MASK = 4 };
typedef struct { uint32_t capture_dma, output_dma, loader_dma, executor_dma; }
    tdma_state_machine_origin_dma_contract_t;
typedef struct { bool armed; uint32_t program_persona, overlay_physical_byte_count; }
    tdma_pio_spi_phys_snapshot_t;
static tdma_service_service_t s_tdma_runtime_owner;
static tdma_pio_spi_ring_adapter_t s_tdma_pio_spi_ring_adapter;
static struct {
    uint32_t flight_physical_byte_count;
    uint32_t flight_origin_record_epoch;
    struct { bool diagnostic_skip_records;
        uint32_t diagnostic_build_probe_epoch, diagnostic_build_probe_config_seq;
        uint32_t stage, reject_code, reject_observed, reject_expected;
    } flight_origin_prepare;
} s_tdma_pio_spi_phys;
static uint32_t model_epoch = 2, clock_hz = 150000000, begins, polls, stops;
static uint64_t ticks = 100;
static bool complete = true, model_valid = true, resources_valid = true, begin_ok = true;
static tdma_origin_build_result_t poll_result = TDMA_ORIGIN_BUILD_BUSY;
static bool scripted_poll, builder_ready;
static uint32_t poll_trace[TDMA_ORIGIN_HANDOFF_STAGES], poll_trace_count;
static uint32_t releases;
static bool release_ok = true, revoke_at_release;
static uint32_t poll_cost, fail_stage, mutate_stage, mutate_kind;
static tdma_ring_runtime_snapshot_t ring;
static tdma_ring_runtime_config_t config;
static uint32_t watermark_epoch = 77, watermark_version = 100;
static bool watermark_ready = true;
static bool probe_ready;
static bool tdma_pio_spi_phys_get_origin_build_probe(tdma_origin_build_probe_t *s)
{ if (s == NULL || !probe_ready) return false;
  *s = (tdma_origin_build_probe_t){.state = TDMA_ORIGIN_BUILD_PROBE_RETIRED}; return true; }
static bool tdma_pio_spi_phys_origin_record_watermark(const void *ctx, uint32_t *epoch, uint32_t *version)
{ (void)ctx; *epoch = watermark_epoch; *version = watermark_version; return watermark_ready; }
static tdma_state_machine_origin_dma_contract_t tdma_state_machine_origin_dma_contract(void)
{ return (tdma_state_machine_origin_dma_contract_t){4, 5, 6, 8}; }
static bool tdma_state_machine_origin_dma_contract_valid(const tdma_state_machine_origin_dma_contract_t *d)
{ (void)d; return resources_valid; }
static uint32_t clock_get_hz(int clock) { (void)clock; return clock_hz; }
static uint64_t vdc_timestamp_clock_read_ticks64(void) { return ticks; }
uint32_t refmem_realtime_contract_origin_model_epoch(void) { return model_epoch; }
static bool tdma_runtime_owner_get_phys_snapshot(tdma_pio_spi_phys_snapshot_t *s)
{ *s = (tdma_pio_spi_phys_snapshot_t){true, 11, 300}; return true; }
static bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *s)
{ *s = ring; return true; }
static bool tdma_runtime_owner_get_staged_ring_config(tdma_ring_runtime_config_t *s)
{ *s = config; s->owner_config_seq = 0u; return true; }
static bool tdma_runtime_owner_get_calibration_stage(tdma_ring_calibration_stage_t *s, bool *c)
{ *s = s_tdma_runtime_owner.calibration_stage; *c = complete; return true; }
static tdma_service_service_t *tdma_runtime_owner_get(void) { return &s_tdma_runtime_owner; }
bool tdma_service_get_foundation_crc32(const tdma_service_service_t *owner, uint32_t *crc)
{ *crc = owner->foundation_profile_crc32; return true; }
uint32_t ota_crc32_compute(const uint8_t *data, size_t size)
{ return pota_crc32_compute(data, size); }
bool refmem_realtime_contract_admit_origin_trial(const refmem_realtime_origin_capability_t *c,
    uint32_t crc, refmem_realtime_origin_admission_t *a)
{
    *a = (refmem_realtime_origin_admission_t){.model_epoch = model_epoch,
        .foundation_crc32 = crc, .diagnostic_valid = model_valid, .capability = *c,
        .product_reject_mask = c->product_reject_mask};
    return model_valid;
}
static bool tdma_pio_spi_phys_origin_begin(void *ctx, const tdma_ring_runtime_config_t *c,
    const uint8_t *p, size_t n, uint32_t r, uint32_t a)
{ (void)ctx; (void)c; (void)p; (void)n; (void)r; (void)a; begins++;
  s_tdma_pio_spi_phys.flight_origin_prepare.reject_code = 0u;
  s_tdma_pio_spi_phys.flight_origin_prepare.reject_observed = 0u;
  s_tdma_pio_spi_phys.flight_origin_prepare.reject_expected = 0u;
  return begin_ok; }
static tdma_origin_build_result_t tdma_pio_spi_phys_origin_poll(void *ctx)
{
    (void)ctx; polls++;
    if (!scripted_poll) return poll_result;
    const uint32_t stage = s_tdma_pio_spi_phys.flight_origin_prepare.stage;
    assert(poll_trace_count < sizeof(poll_trace) / sizeof(poll_trace[0]));
    poll_trace[poll_trace_count++] = stage;
    ticks += poll_cost;
    if (stage == mutate_stage) {
        if (mutate_kind == 0u) calibration_manager_origin_revoke();
        if (mutate_kind == 1u) model_epoch += 2u;
        if (mutate_kind == 2u) clock_hz++;
        if (mutate_kind == 3u) s_tdma_runtime_owner.ring_runtime.config_seq++;
        if (mutate_kind == 4u) ticks += 1000000u;
        if (mutate_kind == 5u) s_tdma_runtime_owner.ring_runtime.enabled = 0u;
    }
    if (stage == fail_stage) return TDMA_ORIGIN_BUILD_FAILED;
    if (stage == TDMA_ORIGIN_PREPARE_BUILD_STEP && !builder_ready)
        return TDMA_ORIGIN_BUILD_BUSY;
    if (stage == TDMA_ORIGIN_PREPARE_INSTALL)
        s_tdma_pio_spi_phys.flight_origin_prepare.stage = TDMA_ORIGIN_PREPARE_READY;
    else if (stage != TDMA_ORIGIN_PREPARE_READY)
        ++s_tdma_pio_spi_phys.flight_origin_prepare.stage;
    return TDMA_ORIGIN_BUILD_BUSY;
}
static bool tdma_pio_spi_phys_origin_release(void *ctx, uint64_t expires, bool (*authorized)(void))
{
    (void)ctx;
    assert(s_tdma_pio_spi_phys.flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_READY);
    if (revoke_at_release) calibration_manager_origin_revoke();
    if (!release_ok || !authorized() || (expires && ticks >= expires)) return false;
    ++releases;
    s_tdma_pio_spi_phys.flight_origin_prepare.stage = TDMA_ORIGIN_PREPARE_COMPLETE;
    return true;
}
static bool tdma_pio_spi_phys_origin_healthy(const void *ctx) { (void)ctx; return true; }
bool tdma_ring_runtime_configure(tdma_ring_runtime_t *runtime, const tdma_ring_runtime_config_t *c)
{ assert(c == NULL); stops++; runtime->enabled = 0; return true; }

static void admission_snapshot_fence(int order);
#define __atomic_thread_fence(order) admission_snapshot_fence(order)
#include "../../components/tdma/src/tdma_runtime_origin.inc"
#include "../../components/calibration_manager/src/calibration_origin_timing.inc"
#undef __atomic_thread_fence

static bool tear_request;
static void admission_snapshot_fence(int order)
{
    __atomic_thread_fence(order);
    if (tear_request) {
        tear_request = false;
        s_origin_release_request.sequence += 2u;
        s_origin_release_request.trial_epoch += 2u;
    }
}

typedef int scpi_result_t;
enum { SCPI_RES_OK = 1, SCPI_RES_ERR = -1, TRUE = 1 };
typedef struct {
    uint64_t parameters[4], results[24];
    unsigned count, position, result_count;
    char text[32], error[64];
} scpi_t;
static bool SCPI_ParamUInt32(scpi_t *ctx, uint32_t *out, bool required)
{ (void)required; if (ctx->position >= ctx->count || ctx->parameters[ctx->position] > UINT32_MAX) return false;
  *out = (uint32_t)ctx->parameters[ctx->position++]; return true; }
static bool SCPI_ParamUInt64(scpi_t *ctx, uint64_t *out, bool required)
{ (void)required; if (ctx->position >= ctx->count) return false; *out = ctx->parameters[ctx->position++]; return true; }
static void SCPI_ResultUInt32(scpi_t *ctx, uint32_t value)
{ assert(ctx->result_count < 24u); ctx->results[ctx->result_count++] = value; }
static void SCPI_ResultUInt64(scpi_t *ctx, uint64_t value)
{ assert(ctx->result_count < 24u); ctx->results[ctx->result_count++] = value; }
static void SCPI_ResultText(scpi_t *ctx, const char *text)
{ snprintf(ctx->text, sizeof(ctx->text), "%s", text); }
static void scpi_port_push_exec_error(scpi_t *ctx, const char *text)
{ snprintf(ctx->error, sizeof(ctx->error), "%s", text); }
#include "origin_release_scpi.inc"

static tdma_service_service_t *s_vdc_tdma_service = &s_tdma_runtime_owner;
static uint32_t full_service_calls[4];
static bool ota_active;
static bool ota_ao_is_active(void) { return ota_active; }
static uint32_t board_uptime_ms(void) { return (uint32_t)(ticks / 1000000u); }
static void tdma_runtime_owner_service_observer(void) { }
static void tdma_runtime_owner_service_phys_tx(uint64_t now)
{ (void)now; full_service_calls[0]++; tdma_runtime_owner_origin_lifetime_core1(); }
void tdma_service_core1_service(tdma_service_service_t *owner)
{ assert(owner == s_vdc_tdma_service); full_service_calls[1]++; }
void distributed_refmem_tdma_publish_service(void) { full_service_calls[2]++; }
static void tdma_runtime_owner_update_training_gate(void) { full_service_calls[3]++; }
#include "tdma_component_service.inc"

static void setup(void)
{
    config = (tdma_ring_runtime_config_t){.baud_hz = 10000000, .cycle_period_ns = 1000000,
        .node_count = 4, .flags = TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE, .owner_config_seq = 4};
    ring = (tdma_ring_runtime_snapshot_t){.enabled = 1, .adapter_started = 1,
        .config_seq = 4, .applied_config_seq = 4};
    s_tdma_runtime_owner.ring_runtime.enabled = 1;
    s_tdma_runtime_owner.ring_runtime.config_seq = 4;
    s_tdma_runtime_owner.foundation_profile_crc32 = 23;
    s_tdma_runtime_owner.calibration_stage.node_count = 4;
    s_tdma_runtime_owner.calibration_stage.calibration_generation = 7;
    s_tdma_pio_spi_phys.flight_physical_byte_count = 300;
}
static tdma_origin_admission_result_t admit(void)
{ uint32_t rearm, abort; return tdma_runtime_owner_origin_admit(&s_tdma_pio_spi_phys, &config, &rearm, &abort); }
static void publish(void)
{ assert(calibration_manager_origin_trial(1, 100, 8, 1000000)); }

static void batch_setup(uint32_t stage)
{
    setup(); ticks = 100u; clock_hz = 150000000u;
    complete = model_valid = resources_valid = begin_ok = true;
    publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
    assert(tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
    scripted_poll = builder_ready = true;
    poll_trace_count = 0u; poll_cost = 10u;
    fail_stage = mutate_stage = UINT32_MAX;
    s_tdma_pio_spi_phys.flight_origin_prepare.stage = stage;
    s_tdma_pio_spi_phys.flight_origin_record_epoch = 31u;
    s_tdma_pio_spi_ring_adapter.origin.active = 1u;
    s_tdma_pio_spi_ring_adapter.comm_fsm.state = TDMA_ADAPTER_COMM_STATE_RESIDENT_PREPARING;
    s_tdma_pio_spi_ring_adapter.origin.returned.sequence = 345u;
    s_tdma_pio_spi_ring_adapter.origin.returned.identity = 0x12345678u;
}

static tdma_origin_build_result_t batch_poll(void)
{
    poll_trace_count = 0u;
    return tdma_runtime_owner_origin_poll(&s_tdma_pio_spi_phys);
}

static void deferred_setup(void)
{
    batch_setup(TDMA_ORIGIN_PREPARE_INSTALL);
    assert(calibration_manager_origin_trial_configured(7, 100, 8, 1000000,
        CALIBRATION_ORIGIN_DIAGNOSTIC_DEFER_RELEASE));
    assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
    assert(tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
}

static void stop_ack(void)
{
    s_tdma_runtime_owner.ring_runtime.enabled = 0u;
    tdma_runtime_owner_origin_lifetime_core1();
    ring.enabled = ring.adapter_started = 0u;
}

static void blackout_start(void)
{
    setup(); ticks = 100; clock_hz = 150000000; watermark_ready = true;
    watermark_epoch = 77; watermark_version = 100;
    s_tdma_pio_spi_ring_adapter.origin.active = 1;
    s_tdma_pio_spi_ring_adapter.comm_fsm.state = TDMA_ADAPTER_COMM_STATE_AUTONOMOUS;
    assert(calibration_manager_origin_trial_configured(9, 100, 8, clock_hz,
        CALIBRATION_ORIGIN_DIAGNOSTIC_SERVICE_BLACKOUT));
    assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
    memset(full_service_calls, 0, sizeof(full_service_calls));
    for (uint32_t i = 1; i < TDMA_ORIGIN_BLACKOUT_SETTLE_CALLS; ++i) {
        tdma_component_core1_service(); ticks += clock_hz / 1000; watermark_version += 2;
        for (unsigned j = 0; j < 4; ++j) assert(full_service_calls[j] == i);
    }
    tdma_component_core1_service();
    assert(s_origin_blackout.snapshot.state == TDMA_ORIGIN_BLACKOUT_ACTIVE);
    for (unsigned j = 0; j < 4; ++j) assert(full_service_calls[j] == TDMA_ORIGIN_BLACKOUT_SETTLE_CALLS - 1);
}

int main(int argc, char **argv)
{
    assert(argc == 2); setup();
    if (!strcmp(argv[1], "publish")) {
        calibration_origin_timing_t record;
        assert(!calibration_manager_origin_get_timing(&record));
        assert(admit() == TDMA_ORIGIN_ADMISSION_NONE);
        publish(); assert(calibration_manager_origin_get_timing(&record));
        assert(record.admission.product_valid == 0 && record.admission.product_reject_mask == 7);
        s_origin_timing_guard++;
        assert(!calibration_manager_origin_get_timing(&record));
        s_origin_timing_guard++;
        for (unsigned bad = 0; bad < 8; bad++) {
            publish();
            if (bad == 0) complete = false;
            if (bad == 1) model_valid = false;
            if (bad == 2) resources_valid = false;
            if (bad == 3) ring.applied_config_seq++;
            if (bad == 4) config.flags = 0;
            if (bad == 5) config.reference_slot_id = 1;
            assert(!calibration_manager_origin_trial(2, bad == 6 ? UINT32_MAX : 100,
                bad == 7 ? UINT32_MAX : 8, 1000000));
            assert(calibration_manager_origin_get_timing(&record) && !record.enabled);
            complete = model_valid = resources_valid = true; setup();
        }
    } else if (!strcmp(argv[1], "stale")) {
        for (unsigned bad = 0; bad < 12; bad++) {
            setup(); publish();
            if (bad == 0) s_tdma_runtime_owner.ring_runtime.config_seq++;
            if (bad == 1) s_origin_timing.config.baud_hz++;
            if (bad == 2) s_origin_timing.admission.capability.persona = 11;
            if (bad == 3) s_origin_timing.admission.capability.dma_mask |= 1u << 7;
            if (bad == 4) s_origin_timing.admission.capability.clk_sys_hz++;
            if (bad == 5) model_epoch += 2;
            if (bad == 6) s_origin_timing.calibration_crc32++;
            if (bad == 7) s_origin_timing.admission.product_valid = 1;
            if (bad == 8) ticks = s_origin_timing.expires_ticks;
            if (bad == 9) s_tdma_runtime_owner.calibration_stage.links[TDMA_RING_CALIBRATION_LINK_MAX - 1u].guard_cycles++;
            if (bad == 10) s_tdma_runtime_owner.foundation_profile_crc32++;
            if (bad == 11) config.owner_config_seq++;
            assert(admit() == TDMA_ORIGIN_ADMISSION_REJECTED);
            assert(admit() == TDMA_ORIGIN_ADMISSION_NONE);
        }
        assert(begins == 0);
    } else if (!strcmp(argv[1], "continuous")) {
        for (unsigned fault = 0; fault < 5; ++fault) {
            setup(); stops=0u; clock_hz=150000000u;
            assert(calibration_manager_origin_trial(9, 100, 8, 0));
            assert(s_origin_timing.version == 3 && !s_origin_timing.expires_ticks);
            assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
            s_tdma_pio_spi_ring_adapter.origin.active = 1;
            for (unsigned second = 0; second <= 601; ++second) {
                ticks = (uint64_t)clock_hz * second;
                assert(tdma_runtime_owner_origin_healthy(&s_tdma_pio_spi_phys));
                tdma_runtime_owner_origin_lifetime_core1(); assert(!stops);
            }
            if (fault == 0) calibration_manager_origin_revoke();
            if (fault == 1) s_tdma_runtime_owner.ring_runtime.enabled = 0;
            if (fault == 2) ++clock_hz;
            if (fault == 3) ++model_epoch;
            if (fault == 4) ++s_tdma_runtime_owner.ring_runtime.config_seq;
            assert(!tdma_runtime_owner_origin_healthy(&s_tdma_pio_spi_phys));
            tdma_runtime_owner_origin_lifetime_core1(); assert(stops == (fault == 1 ? 0u : 1u));
            assert(admit() == TDMA_ORIGIN_ADMISSION_NONE);
        }
    } else if (!strcmp(argv[1], "expiry")) {
        publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
        assert(tdma_runtime_owner_origin_healthy(&s_tdma_pio_spi_phys));
        s_tdma_pio_spi_ring_adapter.origin.active = 1;
        ticks = s_origin_trial.expires_ticks;
        tdma_runtime_owner_origin_lifetime_core1(); assert(stops == 1);
        assert(admit() == TDMA_ORIGIN_ADMISSION_NONE);
    } else if (!strcmp(argv[1], "prepare")) {
        for (unsigned bad = 0; bad < 6; bad++) {
            setup(); publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
            assert(tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
            assert(tdma_runtime_owner_origin_poll(&s_tdma_pio_spi_phys) == TDMA_ORIGIN_BUILD_BUSY);
            uint32_t before = polls;
            if (bad == 0) calibration_manager_origin_revoke();
            if (bad == 1) model_epoch += 2;
            if (bad == 2) clock_hz++;
            if (bad == 3) s_tdma_runtime_owner.ring_runtime.config_seq++;
            if (bad == 4) ticks = s_origin_trial.expires_ticks;
            if (bad == 5) s_tdma_runtime_owner.foundation_profile_crc32++;
            assert(tdma_runtime_owner_origin_poll(&s_tdma_pio_spi_phys) == TDMA_ORIGIN_BUILD_FAILED);
            assert(polls == before);
        }
    } else if (!strcmp(argv[1], "record-mode")) {
        for (uint32_t mode = 0; mode < 2; ++mode) {
            assert(calibration_manager_origin_trial_configured(3, 100, 8, 1000000, mode));
            assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
            assert(tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
            assert(s_tdma_pio_spi_phys.flight_origin_prepare.diagnostic_skip_records == (mode != 0));
            /* A later Core0 publication cannot modify the already consumed request. */
            assert(calibration_manager_origin_trial_configured(4, 100, 8, 1000000, mode ^ 1u));
            assert(s_tdma_pio_spi_phys.flight_origin_prepare.diagnostic_skip_records == (mode != 0));
            assert(tdma_runtime_owner_origin_poll(&s_tdma_pio_spi_phys) == TDMA_ORIGIN_BUILD_FAILED);
        }
        assert(!calibration_manager_origin_trial_configured(5, 100, 8, 1000000, 3));
        assert(!calibration_manager_origin_trial_configured(5, 100, 8, 1000000, 16));
        assert(!s_origin_timing.enabled);
        publish();
        assert(s_origin_timing.diagnostic_flags == 0);
    } else if (!strcmp(argv[1], "build-cancel")) {
        tdma_origin_build_probe_t out;
        for (uint32_t mode = 0u; mode < 2u; ++mode) {
            assert(calibration_manager_origin_trial_configured(9, 100, 8, 1000000,
                mode ? CALIBRATION_ORIGIN_DIAGNOSTIC_BUILD_CANCEL : 0u));
            assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
            const uint32_t epoch = s_origin_trial.epoch;
            assert(tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
            assert(s_tdma_pio_spi_phys.flight_origin_prepare.diagnostic_build_probe_epoch == (mode ? epoch : 0u));
            assert(s_tdma_pio_spi_phys.flight_origin_prepare.diagnostic_build_probe_config_seq == ring.config_seq);
            assert(!s_tdma_pio_spi_phys.flight_origin_prepare.diagnostic_skip_records);
            calibration_manager_origin_revoke();
            assert(tdma_runtime_owner_origin_poll(&s_tdma_pio_spi_phys) == TDMA_ORIGIN_BUILD_FAILED);
            assert(s_tdma_pio_spi_phys.flight_origin_prepare.diagnostic_build_probe_epoch == (mode ? epoch : 0u));
        }
        for (uint32_t bad = 5u; bad <= 7u; ++bad)
            assert(!calibration_manager_origin_trial_configured(9, 100, 8, 1000000, bad));
        probe_ready = true;
        assert(!tdma_runtime_owner_get_origin_build_probe(&out));
        ring.enabled = 0u;
        assert(!tdma_runtime_owner_get_origin_build_probe(&out));
        ring.adapter_started = 0u; ring.applied_config_seq++;
        assert(!tdma_runtime_owner_get_origin_build_probe(&out));
        ring.applied_config_seq = ring.config_seq;
        assert(tdma_runtime_owner_get_origin_build_probe(&out));
        probe_ready = false;
        assert(!tdma_runtime_owner_get_origin_build_probe(&out));
    } else if (!strcmp(argv[1], "handoff")) {
        tdma_origin_handoff_snapshot_t out;
        assert(!tdma_runtime_owner_get_origin_handoff(&out));
        publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
        assert(tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
        ticks += 1500u;
        s_tdma_pio_spi_phys.flight_origin_prepare.stage = TDMA_ORIGIN_PREPARE_MAILBOX;
        assert(tdma_runtime_owner_origin_poll(&s_tdma_pio_spi_phys) == TDMA_ORIGIN_BUILD_BUSY);
        assert(!tdma_runtime_owner_get_origin_handoff(&out));
        s_tdma_pio_spi_phys.flight_origin_prepare.stage = TDMA_ORIGIN_PREPARE_INSTALL;
        ticks += 3000u; poll_result = TDMA_ORIGIN_BUILD_DONE;
        assert(tdma_runtime_owner_origin_poll(&s_tdma_pio_spi_phys) == TDMA_ORIGIN_BUILD_DONE);
        assert(!tdma_runtime_owner_get_origin_handoff(&out));
        ring.enabled = ring.adapter_started = 0u;
        assert(tdma_runtime_owner_get_origin_handoff(&out));
        assert(out.result == TDMA_ORIGIN_BUILD_DONE && out.invalid_count == 0u);
        assert(out.trial_epoch == s_origin_trial.epoch && out.config_seq == s_origin_trial.config_seq);
        assert(out.clock_hz == clock_hz && out.elapsed_ticks == 4500u);
        assert(out.calls[TDMA_ORIGIN_PREPARE_MAILBOX] == 1u && out.calls[TDMA_ORIGIN_PREPARE_INSTALL] == 1u);
        assert(out.first_ticks[TDMA_ORIGIN_PREPARE_MAILBOX] == 1500u);
        ring.applied_config_seq++;
        assert(!tdma_runtime_owner_get_origin_handoff(&out));
        setup(); poll_result = TDMA_ORIGIN_BUILD_BUSY;
        publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
        assert(tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
        s_tdma_pio_spi_phys.flight_origin_prepare.stage = TDMA_ORIGIN_PREPARE_BUILD_STEP;
        calibration_manager_origin_revoke();
        assert(tdma_runtime_owner_origin_poll(&s_tdma_pio_spi_phys) == TDMA_ORIGIN_BUILD_FAILED);
        ring.enabled = ring.adapter_started = 0u;
        assert(tdma_runtime_owner_get_origin_handoff(&out) && out.result == TDMA_ORIGIN_BUILD_FAILED);
        assert(out.calls[TDMA_ORIGIN_PREPARE_MAILBOX] == 0u);
    } else if (!strcmp(argv[1], "batch")) {
        batch_setup(TDMA_ORIGIN_PREPARE_MAILBOX);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && poll_trace_count == 1u);
        assert(s_tdma_pio_spi_phys.flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_STOP);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && poll_trace_count == 3u);
        assert(poll_trace[0] == TDMA_ORIGIN_PREPARE_STOP && poll_trace[1] == TDMA_ORIGIN_PREPARE_PERSONA &&
            poll_trace[2] == TDMA_ORIGIN_PREPARE_BUILD_BEGIN);
        assert(s_tdma_pio_spi_phys.flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_BUILD_STEP);
        builder_ready = false;
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && poll_trace_count == 1u);
        assert(s_tdma_pio_spi_phys.flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_BUILD_STEP);
        builder_ready = true;
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && poll_trace_count == 4u);
        for (uint32_t i = 0u; i < poll_trace_count; ++i)
            assert(poll_trace[i] == TDMA_ORIGIN_PREPARE_BUILD_STEP + i);
        assert(releases == 0u && s_origin_release_ready_epoch == 0u);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_DONE && poll_trace_count == 1u && releases == 1u);
        tdma_origin_handoff_snapshot_t out;
        ring.enabled = ring.adapter_started = 0u;
        assert(tdma_runtime_owner_get_origin_handoff(&out));
        assert(out.result == TDMA_ORIGIN_BUILD_DONE && out.invalid_count == 0u);
        for (uint32_t stage = TDMA_ORIGIN_PREPARE_MAILBOX; stage <= TDMA_ORIGIN_PREPARE_INSTALL; ++stage) {
            uint32_t calls = stage == TDMA_ORIGIN_PREPARE_BUILD_STEP ? 2u : 1u;
            assert(out.calls[stage] == calls && out.work_ticks[stage] == calls * poll_cost);
        }
    } else if (!strcmp(argv[1], "batch-yield")) {
        batch_setup(TDMA_ORIGIN_PREPARE_STOP);
        poll_cost = TDMA_ORIGIN_PREPARE_BATCH_YIELD_CYCLES;
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && poll_trace_count == 1u);
        assert(s_tdma_pio_spi_phys.flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_PERSONA);
        batch_setup(TDMA_ORIGIN_PREPARE_STOP);
        poll_cost = TDMA_ORIGIN_PREPARE_BATCH_YIELD_CYCLES / 2u;
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && poll_trace_count == 2u);
        assert(s_tdma_pio_spi_phys.flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_BUILD_BEGIN);
        poll_cost = 0u;
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && poll_trace_count == 1u);
        assert(s_tdma_pio_spi_phys.flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_BUILD_STEP);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && poll_trace_count == TDMA_ORIGIN_PREPARE_BATCH_MAX_STEPS);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_DONE && releases == 1u);
    } else if (!strcmp(argv[1], "release")) {
        tdma_origin_release_snapshot_t out;
        uint32_t seq;
        deferred_setup();
        const uint32_t epoch = s_origin_trial.epoch;
        assert(!tdma_runtime_owner_request_origin_release(epoch, ring.config_seq, &seq));
        assert(s_origin_release_attempt.reason == TDMA_ORIGIN_RELEASE_ATTEMPT_NOT_READY);
        assert(s_origin_release_request.sequence == 0u);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && s_origin_release_ready_epoch == epoch);
        for (unsigned i = 0; i < 20; ++i) assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
        assert(releases == 0u && s_origin_release_result.ready_checks == 21u);
        assert(!tdma_runtime_owner_get_origin_release(&out));
        assert(!tdma_runtime_owner_request_origin_release(epoch, ring.config_seq + 1u, &seq));
        assert(!tdma_runtime_owner_request_origin_release(epoch + 2u, ring.config_seq, &seq));
        assert(tdma_runtime_owner_request_origin_release(epoch, ring.config_seq, &seq));
        assert(seq == 2u && releases == 0u);
        assert(!tdma_runtime_owner_request_origin_release(epoch, ring.config_seq, &seq));
        assert(s_origin_release_attempt.reason == TDMA_ORIGIN_RELEASE_ATTEMPT_DUPLICATE);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_DONE && releases == 1u);
        assert(s_origin_release_consumed_seq == 2u && s_origin_release_ready_epoch == 0u);
        assert(s_origin_release_result.state == TDMA_ORIGIN_RELEASED);
        assert(s_origin_release_result.seed_sequence == 345u && s_origin_release_result.seed_identity == 0x12345678u);
        assert(s_origin_release_result.record_epoch == 31u && s_origin_release_result.request_seq == 2u);
        assert(s_origin_release_result.release_ticks >= s_origin_release_result.last_ready_ticks);
        stop_ack(); assert(tdma_runtime_owner_get_origin_release(&out));
        assert(out.result.state == TDMA_ORIGIN_RELEASED && out.result.request_seq == 2u);
        assert(!tdma_runtime_owner_request_origin_release(epoch, 4u, &seq));
        deferred_setup(); assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
        assert(!tdma_runtime_owner_request_origin_release(epoch, 4u, &seq));
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && releases == 1u);
        assert(tdma_runtime_owner_request_origin_release(s_origin_trial.epoch, 4u, &seq) && seq == 4u);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_DONE && releases == 2u);
    } else if (!strcmp(argv[1], "release-cancel")) {
        for (unsigned bad = 0u; bad < 8u; ++bad) {
            deferred_setup(); assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
            uint32_t seq; const uint32_t epoch = s_origin_trial.epoch;
            assert(tdma_runtime_owner_request_origin_release(epoch, 4u, &seq));
            if (bad == 0u) calibration_manager_origin_revoke();
            if (bad == 1u) s_tdma_runtime_owner.ring_runtime.enabled = 0u;
            if (bad == 2u) ticks = s_origin_trial.expires_ticks;
            if (bad == 3u) model_epoch += 2u;
            if (bad == 4u) s_tdma_runtime_owner.ring_runtime.config_seq++;
            if (bad == 5u) clock_hz++;
            if (bad == 6u) revoke_at_release = true;
            if (bad == 7u) release_ok = false;
            assert(batch_poll() == TDMA_ORIGIN_BUILD_FAILED && releases == 0u);
            assert(s_origin_release_ready_epoch == 0u && s_origin_release_consumed_seq == seq);
            stop_ack(); tdma_origin_release_snapshot_t out;
            assert(tdma_runtime_owner_get_origin_release(&out) && out.result.release_ticks == 0u);
            assert(out.result.request_seq == seq);
            assert(out.result.state == TDMA_ORIGIN_RELEASE_CANCELLED || out.result.state == TDMA_ORIGIN_RELEASE_FAILED);
            assert(!tdma_runtime_owner_request_origin_release(epoch, 4u, &seq));
            revoke_at_release = false; release_ok = true;
        }
        deferred_setup(); assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
        /* Interrupted Core0 publication cannot hold up STOP. When the old
         * publication completes after STOP it cannot release a new trial. */
        s_origin_release_request.sequence += 1u;
        const uint32_t old_epoch = s_origin_trial.epoch;
        stop_ack();
        s_origin_release_request.trial_epoch = old_epoch;
        s_origin_release_request.config_seq = 4u;
        ++s_origin_release_request.sequence;
        deferred_setup(); assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && releases == 0u);
        assert(s_origin_release_consumed_seq == s_origin_release_request.sequence);
    } else if (!strcmp(argv[1], "release-expiry")) {
        deferred_setup(); assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
        uint32_t seq; ticks = s_origin_trial.expires_ticks;
        assert(!tdma_runtime_owner_request_origin_release(s_origin_trial.epoch, 4u, &seq));
        assert(s_origin_release_attempt.reason == TDMA_ORIGIN_RELEASE_ATTEMPT_EXPIRED);
        tdma_runtime_owner_origin_lifetime_core1();
        assert(stops == 1u && s_origin_release_ready_epoch == 0u && releases == 0u);
        assert(s_origin_release_result.state == TDMA_ORIGIN_RELEASE_CANCELLED);
        assert(s_origin_handoff.snapshot.result == TDMA_ORIGIN_BUILD_FAILED);
    } else if (!strcmp(argv[1], "release-scpi")) {
        batch_setup(TDMA_ORIGIN_PREPARE_INSTALL);
        scpi_t ctx = {.parameters = {8u, 100u, 8u, 1000000u}, .count = 4u};
        assert(scpi_calibration_origin_trial_ready(&ctx) == SCPI_RES_OK);
        const uint32_t epoch = (uint32_t)ctx.results[0];
        assert(epoch == calibration_manager_origin_epoch());
        assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
        assert(s_origin_trial.diagnostic_flags == CALIBRATION_ORIGIN_DIAGNOSTIC_DEFER_RELEASE);
        assert(tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
        ctx = (scpi_t){.parameters = {epoch}, .count = 1u};
        assert(scpi_calibration_origin_release(&ctx) == SCPI_RES_ERR);
        assert(s_origin_release_request.sequence == 0u);
        ctx = (scpi_t){.parameters = {epoch, 4u}, .count = 2u};
        assert(scpi_calibration_origin_release(&ctx) == SCPI_RES_OK && ctx.results[0] == 2u);
        assert(releases == 0u);
        ctx = (scpi_t){0};
        assert(scpi_calibration_origin_release_q(&ctx) == SCPI_RES_OK);
        assert(!strcmp(ctx.text, "UNAVAILABLE") && ctx.result_count == 0u);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_DONE); stop_ack();
        ctx = (scpi_t){0};
        assert(scpi_calibration_origin_release_q(&ctx) == SCPI_RES_OK);
        assert(!strcmp(ctx.text, "ORIGINRELEASE") && ctx.result_count == 21u);
        assert(ctx.results[0] == 2u && ctx.results[1] == TDMA_ORIGIN_RELEASED);
        assert(ctx.results[18] == 0u && ctx.results[19] == 0u && ctx.results[20] == 0u);
        assert(ctx.results[2] == epoch && ctx.results[3] == 4u && ctx.results[8] == 2u);
        assert(ctx.results[12] == 1u && ctx.results[14] == TDMA_ORIGIN_RELEASE_ATTEMPT_ACCEPTED);
        ctx = (scpi_t){.parameters = {epoch, 4u}, .count = 2u};
        assert(scpi_calibration_origin_release(&ctx) == SCPI_RES_ERR);
        assert(!strcmp(ctx.error, "CAL_ORIGIN_RELEASE_REJECTED"));
    } else if (!strcmp(argv[1], "release-mixed")) {
        deferred_setup(); assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
        uint32_t seq;
        assert(tdma_runtime_owner_request_origin_release(s_origin_trial.epoch, 4u, &seq));
        tear_request = true;
        assert(!tdma_runtime_owner_origin_take_release());
        assert(s_origin_release_consumed_seq == 0u && s_origin_release_result.request_seq == 0u);
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && releases == 0u);
        assert(s_origin_release_consumed_seq == seq + 2u);
    } else if (!strcmp(argv[1], "release-stop-race")) {
        tdma_origin_release_snapshot_t out;
        uint32_t seq;
        assert(!tdma_runtime_owner_request_origin_release(0u, 4u, &seq));
        ring.enabled = ring.adapter_started = 0u;
        assert(tdma_runtime_owner_get_origin_release(&out));
        assert(out.result.state == TDMA_ORIGIN_RELEASE_NONE && out.result.trial_epoch == 0u);
        assert(out.attempt.attempts == 1u && out.attempt.rejected == 1u);
        for (unsigned ready = 0u; ready < 2u; ++ready) {
            batch_setup(TDMA_ORIGIN_PREPARE_INSTALL);
            if (ready) assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
            assert(s_origin_release_ready_epoch == 0u); /* legacy trial */
            tdma_runtime_owner_origin_lifetime_core1();
            /* STOP arrives AFTER lifetime service, before runtime ACK. */
            s_tdma_runtime_owner.ring_runtime.enabled = 0u;
            ring.enabled = ring.adapter_started = 0u;
            assert(!tdma_runtime_owner_get_origin_release(&out));
            tdma_runtime_owner_origin_lifetime_core1();
            assert(tdma_runtime_owner_get_origin_release(&out));
            assert(out.result.state == TDMA_ORIGIN_RELEASE_CANCELLED);
            const tdma_origin_release_result_t frozen = out.result;
            /* A late old request cannot mutate the published terminal. */
            s_origin_release_request.sequence += 2u;
            s_origin_release_request.trial_epoch = s_origin_trial.epoch;
            s_origin_release_request.config_seq = 4u;
            tdma_runtime_owner_origin_cancel_release();
            assert(tdma_runtime_owner_get_origin_release(&out));
            assert(!memcmp(&frozen, &out.result, sizeof(frozen)));
        }
    } else if (!strcmp(argv[1], "release-physical-reject")) {
        for (unsigned failure = 0u; failure < 3u; ++failure) {
            deferred_setup();
            tdma_origin_release_snapshot_t out;
            assert(s_origin_release_result.physical_reject == 0u);
            if (failure != 0u) assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
            if (failure == 2u) {
                uint32_t seq;
                assert(tdma_runtime_owner_request_origin_release(s_origin_trial.epoch, 4u, &seq));
                release_ok = false;
            } else {
                fail_stage = failure == 0u ? TDMA_ORIGIN_PREPARE_INSTALL : TDMA_ORIGIN_PREPARE_READY;
            }
            /* The physical seam supplies a first rejection. Exercise the real
             * owner publication and STOP-only SCPI path independently. */
            const uint32_t code = (13u << 16) | (failure + 1u);
            s_tdma_pio_spi_phys.flight_origin_prepare.reject_code = code;
            s_tdma_pio_spi_phys.flight_origin_prepare.reject_observed = 0x80000005u;
            s_tdma_pio_spi_phys.flight_origin_prepare.reject_expected = 5u;
            assert(batch_poll() == TDMA_ORIGIN_BUILD_FAILED && releases == 0u);
            assert(!tdma_runtime_owner_get_origin_release(&out));
            stop_ack();
            assert(tdma_runtime_owner_get_origin_release(&out));
            assert(out.result.physical_reject == code);
            assert(out.result.physical_observed == 0x80000005u);
            assert(out.result.physical_expected == 5u && out.result.release_ticks == 0u);
            const tdma_origin_release_result_t frozen = out.result;
            /* Cleanup/late polling must not rewrite a published terminal,
             * even if physical workspace has since changed. */
            s_tdma_pio_spi_phys.flight_origin_prepare.reject_code = UINT32_MAX;
            s_tdma_pio_spi_phys.flight_origin_prepare.reject_observed = 0u;
            s_tdma_pio_spi_phys.flight_origin_prepare.reject_expected = UINT32_MAX;
            assert(batch_poll() == TDMA_ORIGIN_BUILD_FAILED);
            tdma_runtime_owner_origin_lifetime_core1();
            assert(tdma_runtime_owner_get_origin_release(&out));
            assert(!memcmp(&frozen, &out.result, sizeof(frozen)));
            scpi_t query = {0};
            assert(scpi_calibration_origin_release_q(&query) == SCPI_RES_OK);
            assert(query.result_count == 21u && query.results[0] == 2u);
            assert(query.results[18] == code && query.results[19] == 0x80000005u && query.results[20] == 5u);
            release_ok = true;
        }
        deferred_setup();
        stop_ack();
        tdma_origin_release_snapshot_t out;
        assert(tdma_runtime_owner_get_origin_release(&out));
        assert(out.result.state == TDMA_ORIGIN_RELEASE_CANCELLED);
        assert(out.result.physical_reject == 0u && out.result.physical_observed == 0u && out.result.physical_expected == 0u);
    } else if (!strcmp(argv[1], "release-exhaustion")) {
        deferred_setup(); assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY);
        uint32_t seq;
        s_origin_release_request.sequence = UINT32_MAX - 1u;
        assert(!tdma_runtime_owner_request_origin_release(s_origin_trial.epoch, 4u, &seq));
        assert(s_origin_release_attempt.reason == TDMA_ORIGIN_RELEASE_ATTEMPT_EXHAUSTED);
        s_origin_release_request.sequence = UINT32_MAX;
        assert(!tdma_runtime_owner_request_origin_release(s_origin_trial.epoch, 4u, &seq));
        assert(batch_poll() == TDMA_ORIGIN_BUILD_BUSY && releases == 0u);
        s_origin_timing_guard = UINT32_MAX - 3u;
        assert(!calibration_manager_origin_trial_configured(7, 100, 8, 1000000, 8u));
        assert(s_origin_attempt.reason == CALIBRATION_ORIGIN_ATTEMPT_EPOCH_EXHAUSTED);
        calibration_manager_origin_revoke();
        assert(s_origin_timing_guard == UINT32_MAX);
        for (unsigned i = 0u; i < 3u; ++i) {
            calibration_manager_origin_revoke();
            assert(!calibration_manager_origin_trial(1, 100, 8, 1000000));
            assert(s_origin_timing_guard == UINT32_MAX);
            calibration_origin_timing_t timing;
            assert(!calibration_manager_origin_get_timing(&timing));
        }
    } else if (!strcmp(argv[1], "batch-revoke")) {
        const uint32_t stages[] = {TDMA_ORIGIN_PREPARE_STOP, TDMA_ORIGIN_PREPARE_PERSONA,
            TDMA_ORIGIN_PREPARE_BUILD_STEP, TDMA_ORIGIN_PREPARE_SEED, TDMA_ORIGIN_PREPARE_SMS};
        for (uint32_t i = 0u; i < sizeof(stages) / sizeof(stages[0]); ++i) {
            for (uint32_t kind = 0u; kind < 6u; ++kind) {
                batch_setup(stages[i]); mutate_stage = stages[i]; mutate_kind = kind;
                /* Advancing beyond expiry also exceeds the batch time limit:
                 * yield now, then reject before the next physical operation. */
                assert(batch_poll() == (kind == 4u ? TDMA_ORIGIN_BUILD_BUSY : TDMA_ORIGIN_BUILD_FAILED));
                assert(poll_trace_count == 1u && poll_trace[0] == stages[i]);
                assert(s_tdma_pio_spi_phys.flight_origin_prepare.stage == stages[i] + 1u);
                uint32_t before = polls;
                assert(batch_poll() == TDMA_ORIGIN_BUILD_FAILED && polls == before);
                assert(s_origin_trial_fault);
            }
        }
    } else if (!strcmp(argv[1], "batch-failure")) {
        for (uint32_t stage = TDMA_ORIGIN_PREPARE_STOP; stage <= TDMA_ORIGIN_PREPARE_INSTALL; ++stage) {
            batch_setup(stage <= TDMA_ORIGIN_PREPARE_BUILD_BEGIN ? TDMA_ORIGIN_PREPARE_STOP : TDMA_ORIGIN_PREPARE_BUILD_STEP);
            fail_stage = stage;
            assert(batch_poll() == TDMA_ORIGIN_BUILD_FAILED);
            assert(poll_trace[poll_trace_count - 1u] == stage && s_origin_trial_fault);
        }
        batch_setup(TDMA_ORIGIN_PREPARE_STOP);
        assert(tdma_runtime_owner_origin_poll(NULL) == TDMA_ORIGIN_BUILD_FAILED && poll_trace_count == 0u);
    } else if (!strcmp(argv[1], "blackout")) {
        tdma_origin_blackout_snapshot_t out;
        assert(!tdma_runtime_owner_get_origin_blackout(&out));
        /* An ordinary trial cannot omit the service. */
        publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
        for (unsigned i = 0; i < 80; ++i) tdma_component_core1_service();
        for (unsigned j = 0; j < 4; ++j) assert(full_service_calls[j] == 80);
        blackout_start();
        assert(!tdma_runtime_owner_get_origin_blackout(&out));
        for (unsigned i = 1; i < TDMA_ORIGIN_BLACKOUT_SKIP_CALLS; ++i) {
            ticks += clock_hz / 1000; watermark_version += 2;
            tdma_component_core1_service();
            for (unsigned j = 0; j < 4; ++j) assert(full_service_calls[j] == TDMA_ORIGIN_BLACKOUT_SETTLE_CALLS - 1);
        }
        ticks += clock_hz / 1000; watermark_version += 2;
        tdma_component_core1_service();
        assert(stops == 1 && !s_tdma_runtime_owner.ring_runtime.enabled);
        for (unsigned j = 0; j < 4; ++j) assert(full_service_calls[j] == TDMA_ORIGIN_BLACKOUT_SETTLE_CALLS);
        assert(tdma_runtime_owner_get_origin_blackout(&out));
        assert(out.state == TDMA_ORIGIN_BLACKOUT_COMPLETE && out.skipped_calls == TDMA_ORIGIN_BLACKOUT_SKIP_CALLS);
        assert(out.after_version - out.before_version == 2 * TDMA_ORIGIN_BLACKOUT_SKIP_CALLS);
        assert(out.end_ticks - out.begin_ticks == (uint64_t)clock_hz / 1000 * TDMA_ORIGIN_BLACKOUT_SKIP_CALLS);
        for (unsigned i = 0; i < 80; ++i) tdma_component_core1_service();
        assert(tdma_runtime_owner_get_origin_blackout(&out) && out.skipped_calls == TDMA_ORIGIN_BLACKOUT_SKIP_CALLS);
        s_origin_blackout.guard++;
        assert(!tdma_runtime_owner_get_origin_blackout(&out));
        s_origin_blackout.guard++;
    } else if (!strcmp(argv[1], "blackout-cancel")) {
        for (unsigned bad = 0; bad < 5; ++bad) {
            blackout_start();
            if (bad == 0) calibration_manager_origin_revoke();
            if (bad == 1) s_tdma_runtime_owner.ring_runtime.config_seq++;
            if (bad == 2) model_epoch += 2;
            if (bad == 3) ticks = s_origin_trial.expires_ticks;
            if (bad == 4) s_tdma_runtime_owner.ring_runtime.enabled = 0;
            tdma_component_core1_service();
            assert(s_origin_blackout.snapshot.state == TDMA_ORIGIN_BLACKOUT_CANCELLED);
            for (unsigned j = 0; j < 4; ++j) assert(full_service_calls[j] == TDMA_ORIGIN_BLACKOUT_SETTLE_CALLS);
        }
    } else if (!strcmp(argv[1], "blackout-deadline")) {
        for (unsigned bad = 0; bad < 2; ++bad) {
            blackout_start();
            ticks = bad ? s_origin_blackout.snapshot.begin_ticks - 1 :
                s_origin_blackout.snapshot.begin_ticks + (uint64_t)clock_hz * TDMA_ORIGIN_BLACKOUT_MAX_INTERVAL_US / 1000000 + 1;
            tdma_component_core1_service();
            assert(s_origin_blackout.snapshot.state == TDMA_ORIGIN_BLACKOUT_DEADLINE);
            assert(s_origin_blackout.snapshot.skipped_calls == 1);
        }
    } else if (!strcmp(argv[1], "blackout-invalid")) {
        for (unsigned bad = 0; bad < 3; ++bad) {
            blackout_start();
            if (bad == 0) watermark_epoch++;
            if (bad == 1) watermark_version |= 1;
            if (bad == 2) watermark_ready = false;
            tdma_component_core1_service();
            assert(s_origin_blackout.snapshot.state == TDMA_ORIGIN_BLACKOUT_INVALID);
        }
    } else if (!strcmp(argv[1], "fault")) {
        publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
        s_tdma_pio_spi_ring_adapter.origin.active = 1;
        s_tdma_pio_spi_ring_adapter.comm_fsm.state = TDMA_ADAPTER_COMM_STATE_FAULT;
        tdma_runtime_owner_origin_lifetime_core1(); assert(stops == 1);
        setup(); publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
        begin_ok = false;
        assert(!tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
        tdma_runtime_owner_origin_lifetime_core1(); assert(stops == 2);
    } else return 2;
    puts("origin admission lifecycle passed"); return 0;
}
