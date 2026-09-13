#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "calibration_origin_timing.h"
#include "tdma_service.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "ota_crc32.h"
#include "tdma_origin_blackout.h"
#include "tdma_service_timing.h"

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
    struct { bool diagnostic_skip_records; } flight_origin_prepare;
} s_tdma_pio_spi_phys;
static uint32_t model_epoch = 2, clock_hz = 150000000, begins, polls, stops;
static uint64_t ticks = 100;
static bool complete = true, model_valid = true, resources_valid = true, begin_ok = true;
static tdma_origin_build_result_t poll_result = TDMA_ORIGIN_BUILD_BUSY;
static tdma_ring_runtime_snapshot_t ring;
static tdma_ring_runtime_config_t config;
static uint32_t watermark_epoch = 77, watermark_version = 100;
static bool watermark_ready = true;
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
{ *s = config; return true; }
static bool tdma_runtime_owner_get_calibration_stage(tdma_ring_calibration_stage_t *s, bool *c)
{ *s = s_tdma_runtime_owner.calibration_stage; *c = complete; return true; }
static tdma_service_service_t *tdma_runtime_owner_get(void) { return &s_tdma_runtime_owner; }
bool tdma_service_get_snapshot(const tdma_service_service_t *owner, tdma_service_snapshot_t *s)
{ memset(s, 0, sizeof(*s)); s->foundation_profile_crc32 = owner->foundation_profile_crc32; return true; }
uint32_t ota_crc32_compute(const uint8_t *data, size_t size)
{ uint32_t hash = 2166136261u; while (size--) hash = (hash ^ *data++) * 16777619u; return hash; }
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
{ (void)ctx; (void)c; (void)p; (void)n; (void)r; (void)a; begins++; return begin_ok; }
static tdma_origin_build_result_t tdma_pio_spi_phys_origin_poll(void *ctx)
{ (void)ctx; polls++; return poll_result; }
static bool tdma_pio_spi_phys_origin_healthy(const void *ctx) { (void)ctx; return true; }
bool tdma_ring_runtime_configure(tdma_ring_runtime_t *runtime, const tdma_ring_runtime_config_t *c)
{ assert(c == NULL); stops++; runtime->enabled = 0; return true; }

#include "../../components/tdma/src/tdma_runtime_origin.inc"
#include "../../components/calibration_manager/src/calibration_origin_timing.inc"

static tdma_service_service_t *s_vdc_tdma_service = &s_tdma_runtime_owner;
static uint32_t full_service_calls[4];
static bool ota_active;
static bool ota_ao_is_active(void) { return ota_active; }
static uint64_t vdc_dpll_manager_now_ns(void) { return ticks; }
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
        .node_count = 4, .flags = TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE};
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
        for (unsigned bad = 0; bad < 9; bad++) {
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
            assert(admit() == TDMA_ORIGIN_ADMISSION_REJECTED);
            assert(admit() == TDMA_ORIGIN_ADMISSION_NONE);
        }
        assert(begins == 0);
    } else if (!strcmp(argv[1], "expiry")) {
        publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
        assert(tdma_runtime_owner_origin_healthy(&s_tdma_pio_spi_phys));
        s_tdma_pio_spi_ring_adapter.origin.active = 1;
        ticks = s_origin_trial.expires_ticks;
        tdma_runtime_owner_origin_lifetime_core1(); assert(stops == 1);
        assert(admit() == TDMA_ORIGIN_ADMISSION_NONE);
    } else if (!strcmp(argv[1], "prepare")) {
        for (unsigned bad = 0; bad < 5; bad++) {
            setup(); publish(); assert(admit() == TDMA_ORIGIN_ADMISSION_READY);
            assert(tdma_runtime_owner_origin_begin(&s_tdma_pio_spi_phys, &config, NULL, 0, 100, 8));
            assert(tdma_runtime_owner_origin_poll(&s_tdma_pio_spi_phys) == TDMA_ORIGIN_BUILD_BUSY);
            uint32_t before = polls;
            if (bad == 0) calibration_manager_origin_revoke();
            if (bad == 1) model_epoch += 2;
            if (bad == 2) clock_hz++;
            if (bad == 3) s_tdma_runtime_owner.ring_runtime.config_seq++;
            if (bad == 4) ticks = s_origin_trial.expires_ticks;
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
        assert(!calibration_manager_origin_trial_configured(5, 100, 8, 1000000, 4));
        assert(!s_origin_timing.enabled);
        publish();
        assert(s_origin_timing.diagnostic_flags == 0);
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
