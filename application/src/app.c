#include "app.h"

#include "board_config.h"
#include "calibration_manager.h"
#include "board_identity.h"
#include "diagnostics.h"
#include "drv_watchdog.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "event_bus.h"
#include "flash_transaction.h"
#include "loop_engine.h"
#include "model_turntable.h"
#include "ota_ao.h"
#include "product_config.h"
#include "project_config.h"
#include "resource_arbiter.h"
#include "scpi_port.h"
#include "rs485_communication.h"
#include "storage_manager.h"
#include "system_manager.h"
#include "sync_trigger.h"
#include "tdma_runtime_owner.h"
#include "sync_io.h"
#include "trigger_measure.h"
#include "ui_manager.h"
#include "vdc_dpll_manager.h"
#include "hardware/regs/m33.h"
#include "hardware/structs/systick.h"
#include "pico/stdlib.h"
#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
#include "usbtmc_scpi_port.h"
#endif

static bool s_app_ready;
static bool s_app_control_plane_ready;
bool app_init(void)
{
    s_app_ready = false;
    s_app_control_plane_ready = false;
    diagnostics_housekeeping_init();
    if (!board_identity_init()) {
        diagnostics_mark_fault("identity", "unique board identity initialization failed");
        return false;
    }
    if (!product_config_init()) {
        diagnostics_mark_fault("product_config", "product config initialization failed");
        return false;
    }
    const uint8_t persisted_board_no = product_config_get_board_no();
    if (persisted_board_no != 0u &&
        !board_identity_set_no(persisted_board_no)) {
        diagnostics_mark_fault("identity", "persisted board number is invalid");
        return false;
    }
    LOG_INFO("app", "application initialized");

    const sync_io_config_t sync_io_config = {
        .capture_sample_hz = 1000000u,
        .sync_clock_hz = 1000000u,
    };

    if (!sync_io_init(&sync_io_config)) {
        diagnostics_mark_fault("sync_io", "sync IO initialization failed");
        return false;
    }

    if (!scpi_port_init()) {
        diagnostics_mark_fault("scpi", "SCPI initialization failed");
        return false;
    }

    if (!rs485_communication_init()) {
        diagnostics_mark_fault("rs485", "RS485 communication initialization failed");
        return false;
    }

#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
    if (!usbtmc_scpi_port_init()) {
        diagnostics_mark_fault("usb", "USB SCPI initialization failed");
        return false;
    }
    s_app_control_plane_ready = true;
#else
    s_app_control_plane_ready = true;
#endif

    if (!resource_arbiter_init()) {
        diagnostics_mark_fault("resource_arbiter", "resource arbiter initialization failed");
        return false;
    }

    if (!flash_transaction_ao_init()) {
        diagnostics_mark_fault("flash_transaction",
                               "flash transaction initialization failed");
        return false;
    }

    if (!event_bus_init()) {
        diagnostics_mark_fault("event_bus", "event bus initialization failed");
        return false;
    }

    if (!loop_engine_init()) {
        diagnostics_mark_fault("loop_engine", "loop engine initialization failed");
        return false;
    }

    if (!model_turntable_init()) {
        diagnostics_mark_fault("model_turntable", "model turntable initialization failed");
        return false;
    }

    if (!calibration_manager_init()) {
        diagnostics_mark_fault("calibration", "calibration manager initialization failed");
        return false;
    }

    if (!tdma_runtime_owner_init()) {
        diagnostics_mark_fault("tdma", "TDMA runtime owner initialization failed");
        return false;
    }

    if (!vdc_dpll_manager_init()) {
        diagnostics_mark_fault("vdc_dpll", "VDC/DPLL manager initialization failed");
        return false;
    }

    if (!distributed_refmem_init()) {
        diagnostics_mark_fault("refmem", "distributed refmem initialization failed");
        return false;
    }

    if (!distributed_config_init()) {
        diagnostics_mark_fault("config", "distributed config initialization failed");
        return false;
    }

    if (!system_manager_init()) {
        diagnostics_mark_fault("config", "distributed config consistency check failed");
    }

    if (!ota_ao_init()) {
        diagnostics_mark_fault("ota", "OTA initialization failed");
        return false;
    }

    if (!storage_manager_init()) {
        diagnostics_mark_fault("storage", "storage manager initialization failed");
        return false;
    }

    if (!sync_trigger_init()) {
        diagnostics_mark_fault("trigger", "sync trigger initialization failed");
        return false;
    }

    if (!ui_manager_init()) {
        diagnostics_mark_fault("ui", "sync config UI initialization failed");
        return false;
    }
    s_app_ready = true;
    loop_engine_set_ready(true);
    calibration_manager_set_ready(true);
    vdc_dpll_manager_set_vdc_ready(true);
    vdc_dpll_manager_set_dpll_ready(true);

    return true;
}

bool app_is_ready(void)
{
    return s_app_ready;
}

bool app_is_control_plane_ready(void)
{
    return s_app_control_plane_ready;
}

void app_usb_device_service(void)
{
#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
    usbtmc_scpi_port_service();
#endif
}

void app_scpi_service(void)
{
    rs485_communication_service();
#if !PROJECT_ENABLE_USB_RUNTIME_SWITCH
    scpi_port_service();
#endif
}

void app_refmem_service(void)
{
    distributed_refmem_service();
}

void app_config_gate_service(void)
{
    system_manager_service();
}

void app_ota_service(void)
{
    ota_ao_service(500u);
}

void app_diag_service(void)
{
    vdc_dpll_manager_core0_service();
    diagnostics_housekeeping_service();
}

void app_storage_service(void)
{
    storage_manager_service(250u);
}

typedef void (*app_realtime_load_service_fn)(void);

static volatile uint32_t s_realtime_schedule_guard;
static volatile uint32_t s_realtime_load_enabled_mask =
    APP_REALTIME_LOAD_FOUNDATION_MASK;
static volatile uint32_t s_realtime_load_quarantined_mask;

#define APP_REALTIME_PHASE_CONTRACT_INIT(name, start, end, wcet) \
    [APP_REALTIME_PHASE_##name] = {start, end, wcet},
static const app_realtime_phase_contract_t
    s_realtime_phase_contract[APP_REALTIME_PHASE_COUNT] = {
        APP_REALTIME_PHASE_TABLE(APP_REALTIME_PHASE_CONTRACT_INIT)
    };
#undef APP_REALTIME_PHASE_CONTRACT_INIT

#define APP_REALTIME_PHASE_VALUE_INIT(name, start, end, wcet) \
    [APP_REALTIME_PHASE_##name] = start,
#define APP_REALTIME_PHASE_END_INIT(name, start, end, wcet) \
    [APP_REALTIME_PHASE_##name] = end,
#define APP_REALTIME_PHASE_WCET_INIT(name, start, end, wcet) \
    [APP_REALTIME_PHASE_##name] = wcet,
static app_realtime_schedule_snapshot_t s_realtime_schedule = {
    .version = APP_REALTIME_SCHEDULE_VERSION,
    .sys_clock_hz = BOARD_SYS_CLOCK_HZ,
    .cycle_cycles = PROJECT_CORE1_CYCLE_CYCLES,
    .phase_count = APP_REALTIME_PHASE_COUNT,
    .phase_start_cycle = {
        APP_REALTIME_PHASE_TABLE(APP_REALTIME_PHASE_VALUE_INIT)
    },
    .phase_end_cycle = {
        APP_REALTIME_PHASE_TABLE(APP_REALTIME_PHASE_END_INIT)
    },
    .phase_wcet_cycles = {
        APP_REALTIME_PHASE_TABLE(APP_REALTIME_PHASE_WCET_INIT)
    },
};
#undef APP_REALTIME_PHASE_VALUE_INIT
#undef APP_REALTIME_PHASE_END_INIT
#undef APP_REALTIME_PHASE_WCET_INIT

static void app_realtime_schedule_write_begin(void)
{
    (void)__atomic_add_fetch(&s_realtime_schedule_guard,
                             1u,
                             __ATOMIC_ACQ_REL);
}

static void app_realtime_schedule_write_end(void)
{
    (void)__atomic_add_fetch(&s_realtime_schedule_guard,
                             1u,
                             __ATOMIC_RELEASE);
}

void app_realtime_cycle_counter_init(void)
{
    /* SysTick is core-local on RP2350. Core0's FreeRTOS tick is therefore not
     * touched by this core1-only free-running clk_sys cycle counter. */
    systick_hw->csr = 0u;
    systick_hw->rvr = M33_SYST_RVR_RELOAD_BITS;
    systick_hw->cvr = 0u;
    systick_hw->csr = M33_SYST_CSR_CLKSOURCE_BITS |
                      M33_SYST_CSR_ENABLE_BITS;
}

static uint32_t app_realtime_cycle_now(void)
{
    return (M33_SYST_RVR_RELOAD_BITS - systick_hw->cvr) &
           M33_SYST_RVR_RELOAD_BITS;
}

static uint32_t app_realtime_elapsed_cycles(uint32_t start_cycle,
                                            uint32_t end_cycle)
{
    return (end_cycle - start_cycle) & M33_SYST_RVR_RELOAD_BITS;
}

bool app_realtime_set_load_mask(uint32_t enabled_mask)
{
    if ((enabled_mask & ~APP_REALTIME_LOAD_ALL_MASK) != 0u) {
        return false;
    }
    const uint32_t previous_mask = __atomic_exchange_n(
        &s_realtime_load_enabled_mask, enabled_mask, __ATOMIC_ACQ_REL);
    /* Only a disabled -> enabled transition is an explicit decision to
     * release that load from quarantine.  Enabling a bounded diagnostic
     * phase must never revive unrelated loads that were isolated after a
     * WCET/deadline violation. */
    const uint32_t newly_enabled = enabled_mask & ~previous_mask;
    (void)__atomic_fetch_and(&s_realtime_load_quarantined_mask,
                             ~newly_enabled,
                             __ATOMIC_ACQ_REL);
    return true;
}

bool app_realtime_get_schedule_snapshot(
    app_realtime_schedule_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_realtime_schedule_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        *snapshot = s_realtime_schedule;
        const uint32_t end = __atomic_load_n(
            &s_realtime_schedule_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            snapshot->enabled_mask = __atomic_load_n(
                &s_realtime_load_enabled_mask, __ATOMIC_ACQUIRE);
            snapshot->quarantined_mask = __atomic_load_n(
                &s_realtime_load_quarantined_mask, __ATOMIC_ACQUIRE);
            return true;
        }
    }
    return false;
}

static void app_realtime_record_skip(app_realtime_phase_id_t phase_id,
                                     bool start_missed)
{
    app_realtime_schedule_write_begin();
    s_realtime_schedule.phase_skip_count[phase_id]++;
    if (start_missed) {
        s_realtime_schedule.phase_start_miss_count[phase_id]++;
        s_realtime_schedule.schedule_miss_count++;
    }
    app_realtime_schedule_write_end();
}

static bool app_realtime_run_phase(
    uint32_t cycle_epoch,
    app_realtime_phase_id_t phase_id,
    int32_t load_id,
    app_realtime_load_service_fn service)
{
    if (phase_id >= APP_REALTIME_PHASE_COUNT || service == NULL) {
        return false;
    }
    const app_realtime_phase_contract_t *contract =
        &s_realtime_phase_contract[phase_id];
    uint32_t phase_start = app_realtime_elapsed_cycles(
        cycle_epoch, app_realtime_cycle_now());
    while (phase_start < contract->start_cycle) {
        tight_loop_contents();
        phase_start = app_realtime_elapsed_cycles(
            cycle_epoch, app_realtime_cycle_now());
    }

    const bool optional_load = load_id >= 0;
    const bool warmup_cycle =
        s_realtime_schedule.cycle_count <=
        PROJECT_CORE1_SCHEDULE_WARMUP_CYCLES;
    const uint32_t load_bit = optional_load
        ? 1u << (uint32_t)load_id : 0u;
    const uint32_t enabled_mask = __atomic_load_n(
        &s_realtime_load_enabled_mask, __ATOMIC_ACQUIRE);
    const uint32_t quarantined_mask = __atomic_load_n(
        &s_realtime_load_quarantined_mask, __ATOMIC_ACQUIRE);
    if (optional_load && ((enabled_mask & load_bit) == 0u ||
                          (quarantined_mask & load_bit) != 0u)) {
        app_realtime_record_skip(phase_id, false);
        return true;
    }

    /* A phase may consume only its own [start,end) interval. If its declared
     * WCET no longer fits, it is skipped instead of borrowing a later phase. */
    if (phase_start >= contract->end_cycle ||
        contract->wcet_cycles > contract->end_cycle - phase_start) {
        app_realtime_record_skip(phase_id, true);
        /* A phase-start miss is inherited lateness, not proof that this
         * load exceeded its own WCET.  Skip it without borrowing time from
         * the next phase; quarantine only after this load actually runs and
         * overruns its own contract below. */
        return false;
    }

    const uint32_t start_counter = app_realtime_cycle_now();
    service();
    const uint32_t end_counter = app_realtime_cycle_now();
    const uint32_t runtime_cycles = app_realtime_elapsed_cycles(
        start_counter, end_counter);
    const uint32_t phase_end = app_realtime_elapsed_cycles(
        cycle_epoch, end_counter);
    const bool overrun = runtime_cycles > contract->wcet_cycles;
    const bool deadline_missed = phase_end > contract->end_cycle;
    const bool inherited_lateness = phase_start > contract->start_cycle;
    const bool own_deadline_missed = deadline_missed && !inherited_lateness;

    app_realtime_schedule_write_begin();
    s_realtime_schedule.phase_last_start_cycle[phase_id] = phase_start;
    s_realtime_schedule.phase_last_runtime_cycles[phase_id] = runtime_cycles;
    s_realtime_schedule.phase_run_count[phase_id]++;
    if (runtime_cycles >
        s_realtime_schedule.phase_max_runtime_cycles[phase_id]) {
        s_realtime_schedule.phase_max_runtime_cycles[phase_id] =
            runtime_cycles;
    }
    if (overrun) {
        s_realtime_schedule.phase_overrun_count[phase_id]++;
    }
    if (deadline_missed) {
        s_realtime_schedule.phase_deadline_miss_count[phase_id]++;
    }
    if (overrun || deadline_missed) {
        s_realtime_schedule.schedule_miss_count++;
    }
    app_realtime_schedule_write_end();

    if ((overrun || own_deadline_missed) && optional_load && !warmup_cycle) {
        (void)__atomic_fetch_or(&s_realtime_load_quarantined_mask,
                                load_bit,
                                __ATOMIC_ACQ_REL);
    }
    if (overrun || deadline_missed) {
        return false;
    }
    return true;
}

static void app_realtime_tdma_phase(void)
{
    tdma_component_core1_service();
    drv_watchdog_mark_progress(1u, 0x0101u);
    diagnostics_record_core1_loop();
    diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_CORE1);
    drv_watchdog_mark_progress(1u, 0x0103u);
}

static void app_realtime_vdc_phase(void)
{
    vdc_sync_ao_service();
    drv_watchdog_mark_progress(1u, 0x0111u);
}

static void app_realtime_dpll_phase(void)
{
    drv_watchdog_mark_progress(1u, 0x0102u);
    sync_dpll_fb_service();
    drv_watchdog_mark_progress(1u, 0x0112u);
}

static void app_realtime_calibration_phase(void)
{
    calibration_manager_service_core1();
    drv_watchdog_mark_progress(1u, 0x0104u);
}

static void app_realtime_sync_capture_phase(void)
{
    sync_io_capture_latch_service_core1();
    drv_watchdog_mark_progress(1u, 0x0105u);
}

static void app_realtime_refmem_phase(void)
{
    distributed_refmem_realtime_run_once();
    drv_watchdog_mark_progress(1u, 0x0106u);
}

static void app_realtime_model_phase(void)
{
    model_turntable_service();
    drv_watchdog_mark_progress(1u, 0x0107u);
}

static void app_realtime_sync_trigger_phase(void)
{
    sync_trigger_service();
    drv_watchdog_mark_progress(1u, 0x0108u);
}

static void app_realtime_trigger_measure_phase(void)
{
    trigger_measure_service(); /* 同步自检: 门控测量非阻塞服务 */
    drv_watchdog_mark_progress(1u, 0x0109u);
}

void app_realtime_run_once(void)
{
    /* P3 is an offline physical-calibration session: TDMA is stopped and the
     * calibration owner temporarily owns the shared PIO/DMA persona.  Keep
     * it outside the TDMA realtime phase/load-mask contract and advance one
     * bounded transition per core1 cycle. */
    if (calibration_manager_p3_offline_active_core1()) {
        calibration_manager_p3_service_core1();
        drv_watchdog_mark_progress(1u, 0x0104u);
        diagnostics_record_core1_loop();
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_CORE1);
        return;
    }
    /* All stopped-ring calibration/training personas run outside the online
     * TDMA phase table.  Their PIO/DMA persona transitions may take longer
     * than the optional online snapshot phase, but they cannot perturb a
     * running short-frame cycle or be hidden by its quarantine mechanism. */
    if (calibration_manager_training_offline_active_core1()) {
        calibration_manager_service_core1();
        drv_watchdog_mark_progress(1u, 0x0104u);
        diagnostics_record_core1_loop();
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_CORE1);
        return;
    }
    const uint32_t cycle_epoch = app_realtime_cycle_now();
    app_realtime_schedule_write_begin();
    s_realtime_schedule.cycle_count++;
    app_realtime_schedule_write_end();

    /* Every call is released only inside its own fixed phase. Early finish
     * waits for the next start; late work is quarantined and cannot change a
     * later phase's declared start/end/WCET contract. */
    (void)app_realtime_run_phase(cycle_epoch,
                                 APP_REALTIME_PHASE_TDMA,
                                 -1,
                                 app_realtime_tdma_phase);
    (void)app_realtime_run_phase(cycle_epoch,
                                 APP_REALTIME_PHASE_VDC,
                                 APP_REALTIME_LOAD_VDC,
                                 app_realtime_vdc_phase);
    (void)app_realtime_run_phase(cycle_epoch,
                                 APP_REALTIME_PHASE_DPLL,
                                 APP_REALTIME_LOAD_DPLL,
                                 app_realtime_dpll_phase);
    (void)app_realtime_run_phase(cycle_epoch,
                                 APP_REALTIME_PHASE_CALIBRATION,
                                 APP_REALTIME_LOAD_CALIBRATION,
                                 app_realtime_calibration_phase);
    (void)app_realtime_run_phase(cycle_epoch,
                                 APP_REALTIME_PHASE_SYNC_CAPTURE,
                                 APP_REALTIME_LOAD_SYNC_CAPTURE,
                                 app_realtime_sync_capture_phase);
    (void)app_realtime_run_phase(cycle_epoch,
                                 APP_REALTIME_PHASE_REFMEM,
                                 APP_REALTIME_LOAD_REFMEM,
                                 app_realtime_refmem_phase);
    (void)app_realtime_run_phase(cycle_epoch,
                                 APP_REALTIME_PHASE_MODEL,
                                 APP_REALTIME_LOAD_MODEL,
                                 app_realtime_model_phase);
    (void)app_realtime_run_phase(cycle_epoch,
                                 APP_REALTIME_PHASE_SYNC_TRIGGER,
                                 APP_REALTIME_LOAD_SYNC_TRIGGER,
                                 app_realtime_sync_trigger_phase);
    (void)app_realtime_run_phase(cycle_epoch,
                                 APP_REALTIME_PHASE_TRIGGER_MEASURE,
                                 APP_REALTIME_LOAD_TRIGGER_MEASURE,
                                 app_realtime_trigger_measure_phase);
}
