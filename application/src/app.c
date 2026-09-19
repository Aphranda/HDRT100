#include "app.h"
#include "app_realtime_profile.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
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
#include "tdma_service_timing.h"
#include "sync_io.h"
#include "sync_io_logic_analyzer.h"
#include "ota_crc32.h"
#include "project_build_info.h"
#include "trigger_measure.h"
#include "ui_manager.h"
#include "vdc_dpll_manager.h"
#include "vdc_run_output.h"
#include "vdc_timestamp_clock.h"
#include "hardware/regs/m33.h"
#include "hardware/structs/systick.h"
#include "pico/stdlib.h"
#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
#include "usbtmc_scpi_port.h"
#endif

static bool s_app_ready;
static bool s_app_control_plane_ready;

#define APP_ANALYZER_STORAGE_SEGMENT_RECORDS 128u
#define APP_ANALYZER_STORAGE_MAGIC 0x59414C53u /* SLAY */
#define APP_ANALYZER_STORAGE_SCHEMA 2u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t schema;
    uint16_t header_size;
    uint32_t session;
    uint32_t record_count;
    uint32_t dropped_records;
    uint32_t payload_crc32;
    uint32_t source_mask;
    uint32_t profile_generation;
    uint32_t persona_generation;
    uint32_t hardware_tick_hz;
    uint32_t timestamp_resolution_ns;
    uint32_t capture_sequence;
    uint32_t segment_index;
    uint32_t first_record_sequence;
    uint32_t batch_sequence;
} app_analyzer_storage_header_t;

static union {
    sync_io_logic_analyzer_record_t records[APP_ANALYZER_STORAGE_SEGMENT_RECORDS];
    uint32_t burst_words[SYNC_IO_ANALYZER_BURST_COPY_WORDS];
} s_analyzer_storage_buffer;
#define s_analyzer_storage_records s_analyzer_storage_buffer.records
static uint32_t s_analyzer_storage_record_count;
static uint32_t s_analyzer_storage_job_id;
static uint32_t s_analyzer_storage_session;
static uint32_t s_analyzer_storage_capture_sequence;
static uint32_t s_analyzer_storage_segment_index;
static sync_io_logic_analyzer_live_batch_t s_analyzer_storage_batch;
static bool s_analyzer_storage_pending;
static bool s_analyzer_storage_job_inflight;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t schema;
    uint16_t header_size;
    uint32_t session;
    uint32_t segment_index;
    uint32_t first_word;
    uint32_t word_count;
    uint32_t payload_crc32;
    sync_io_analyzer_burst_snapshot_t capture;
    char build_id[16];
} app_analyzer_burst_header_t;

_Static_assert(sizeof(app_analyzer_burst_header_t) == 140u,
               "finite capture schema must match the host decoder");

static struct {
    uint32_t sequence;
    uint32_t session;
    uint32_t next_word;
    uint32_t words;
    uint32_t segment;
    uint32_t job;
    bool inflight;
    bool failed;
} s_analyzer_burst_storage;

/* Core0 only: packed samples stay in the owner's frozen workspace until all
 * segments reach StorageAO DONE. A failed job retains the data and original
 * job result for diagnosis instead of recycling the capture buffer. */
static bool app_analyzer_burst_storage_service(void)
{
    sync_io_analyzer_burst_snapshot_t capture;
    if (!sync_io_analyzer_burst_get_snapshot(&capture)) return false;
    if (capture.state == SYNC_IO_ANALYZER_BURST_CAPTURING) return true;
    if (capture.state != SYNC_IO_ANALYZER_BURST_FROZEN) return false;
    if (s_analyzer_burst_storage.sequence != capture.capture_sequence) {
        memset(&s_analyzer_burst_storage, 0, sizeof(s_analyzer_burst_storage));
        s_analyzer_burst_storage.sequence = capture.capture_sequence;
        s_analyzer_burst_storage.session = capture.capture_tag;
        sync_io_analyzer_burst_begin_export_core0(capture.capture_sequence);
    }
    const bool retry = sync_io_analyzer_burst_take_export_retry_core0(capture.capture_sequence);
    if (s_analyzer_burst_storage.failed) {
        if (!retry) return true;
        s_analyzer_burst_storage.failed = false;
        s_analyzer_burst_storage.inflight = false;
    }
    if (s_analyzer_burst_storage.inflight) {
        storage_manager_job_result_t result;
        storage_manager_get_job_result(&result);
        if (result.id != s_analyzer_burst_storage.job) return true;
        if (result.state == STORAGE_MANAGER_JOB_STATE_FAILED) {
            sync_io_analyzer_burst_export_failed_core0(result.id, result.error);
            s_analyzer_burst_storage.failed = true;
            if (!retry) return true;
            s_analyzer_burst_storage.failed = false;
            s_analyzer_burst_storage.inflight = false;
        } else {
            if (result.state != STORAGE_MANAGER_JOB_STATE_DONE) return true;
            s_analyzer_burst_storage.inflight = false;
            s_analyzer_burst_storage.next_word += s_analyzer_burst_storage.words;
            ++s_analyzer_burst_storage.segment;
        }
    }
    /* Even a trigger timeout with no samples emits one header-only segment. */
    if (s_analyzer_burst_storage.next_word == capture.captured_words &&
        s_analyzer_burst_storage.segment != 0u) {
        (void)sync_io_logic_analyzer_request_burst_release(capture.capture_sequence);
        return true;
    }
    s_analyzer_burst_storage.words = (uint32_t)sync_io_analyzer_burst_copy_core0(
        capture.capture_sequence, s_analyzer_burst_storage.next_word,
        s_analyzer_storage_buffer.burst_words, SYNC_IO_ANALYZER_BURST_COPY_WORDS);
    if (s_analyzer_burst_storage.words == 0u && capture.captured_words != 0u) return true;
    const size_t size = s_analyzer_burst_storage.words * sizeof(uint32_t);
    app_analyzer_burst_header_t header = {
        .magic = 0x54534241u, /* ABST, diagnostic packed burst schema */
        .schema = SYNC_IO_ANALYZER_BURST_SCHEMA,
        .header_size = (uint16_t)sizeof(header),
        .session = s_analyzer_burst_storage.session,
        .segment_index = s_analyzer_burst_storage.segment,
        .first_word = s_analyzer_burst_storage.next_word,
        .word_count = s_analyzer_burst_storage.words,
        .payload_crc32 = ota_crc32_compute(
            (const uint8_t *)s_analyzer_storage_buffer.burst_words, size),
        .capture = capture,
    };
    (void)snprintf(header.build_id, sizeof(header.build_id), "%s", g_project_build_id);
    const uint32_t file_size = (uint32_t)(sizeof(header) + size);
    const uint32_t crc = ota_crc32_update(
        ota_crc32_update(0u, (const uint8_t *)&header, sizeof(header)),
        (const uint8_t *)s_analyzer_storage_buffer.burst_words, size);
    char path[96];
    (void)snprintf(path, sizeof(path), "/traces/run/burst_%08lu_%08lu_%04lu.bin",
        (unsigned long)header.session, (unsigned long)capture.capture_sequence,
        (unsigned long)header.segment_index);
    uint32_t txn = 0u;
    if (file_size > STORAGE_MANAGER_FILE_WRITE_MAX_BYTES ||
        !storage_manager_begin_evidence_write(path, file_size, crc, &txn)) return true;
    if (!storage_manager_write_file_chunk(txn, 0u, (const uint8_t *)&header, sizeof(header)) ||
        (size != 0u && !storage_manager_write_file_chunk(txn, sizeof(header),
            (const uint8_t *)s_analyzer_storage_buffer.burst_words, size)) ||
        !storage_manager_commit_file_write(txn, &s_analyzer_burst_storage.job)) {
        storage_manager_write_snapshot_t failure;
        storage_manager_get_write_snapshot(&failure);
        sync_io_analyzer_burst_export_failed_core0(0u, failure.error);
        (void)storage_manager_abort_file_write(txn);
        s_analyzer_burst_storage.failed = true;
        return true;
    }
    s_analyzer_burst_storage.inflight = true;
    return true;
}

static void app_analyzer_storage_service(void)
{
    if (s_analyzer_storage_job_inflight) {
        storage_manager_job_result_t result;
        storage_manager_get_job_result(&result);
        if (result.id == s_analyzer_storage_job_id &&
            (result.state == STORAGE_MANAGER_JOB_STATE_DONE ||
             result.state == STORAGE_MANAGER_JOB_STATE_FAILED)) {
            s_analyzer_storage_job_inflight = false;
            if (result.state == STORAGE_MANAGER_JOB_STATE_DONE) {
                ++s_analyzer_storage_segment_index;
            } else {
                s_analyzer_storage_pending = true;
            }
        }
    }
    if (s_analyzer_storage_job_inflight) return;
    /* A legacy batch may already occupy the union even if StorageAO has not
     * accepted its write yet. Drain that pending batch before burst export. */
    if (!s_analyzer_storage_pending && app_analyzer_burst_storage_service()) return;
    sync_io_logic_analyzer_status_t analyzer;
    sync_io_logic_analyzer_get_status(&analyzer);
    if (s_analyzer_storage_job_inflight ||
        analyzer.state == SYNC_IO_LOGIC_ANALYZER_STATE_STOPPED) {
        return;
    }
    if (!s_analyzer_storage_pending) {
        s_analyzer_storage_record_count = (uint32_t)
            sync_io_logic_analyzer_drain_live_core0(
                s_analyzer_storage_records,
                APP_ANALYZER_STORAGE_SEGMENT_RECORDS,
                &s_analyzer_storage_batch);
        if (s_analyzer_storage_record_count == 0u && !analyzer.active &&
            analyzer.state == SYNC_IO_LOGIC_ANALYZER_STATE_COMPLETE) {
            s_analyzer_storage_record_count = (uint32_t)
                sync_io_logic_analyzer_drain_core0(
                    s_analyzer_storage_records,
                    APP_ANALYZER_STORAGE_SEGMENT_RECORDS);
            if (s_analyzer_storage_record_count != 0u) {
                s_analyzer_storage_batch.capture_sequence =
                    analyzer.capture_sequence;
                s_analyzer_storage_batch.first_record_sequence =
                    s_analyzer_storage_records[0].record_sequence;
                s_analyzer_storage_batch.record_count =
                    s_analyzer_storage_record_count;
                s_analyzer_storage_batch.dropped_records =
                    analyzer.dropped_records;
            }
        }
        if (s_analyzer_storage_record_count == 0u) {
            return;
        }
        if (s_analyzer_storage_capture_sequence !=
            s_analyzer_storage_batch.capture_sequence) {
            s_analyzer_storage_capture_sequence =
                s_analyzer_storage_batch.capture_sequence;
            s_analyzer_storage_session = board_uptime_ms();
            if (s_analyzer_storage_session == 0u) {
                s_analyzer_storage_session = 1u;
            }
            s_analyzer_storage_segment_index = 0u;
        }
    }

    app_analyzer_storage_header_t header = {
        .magic = APP_ANALYZER_STORAGE_MAGIC,
        .schema = APP_ANALYZER_STORAGE_SCHEMA,
        .header_size = (uint16_t)sizeof(header),
        .session = s_analyzer_storage_session,
        .record_count = s_analyzer_storage_record_count,
        .dropped_records = s_analyzer_storage_batch.dropped_records,
        .payload_crc32 = ota_crc32_compute(
            (const uint8_t *)s_analyzer_storage_records,
            (size_t)s_analyzer_storage_record_count *
                sizeof(s_analyzer_storage_records[0])),
        .source_mask = analyzer.source_mask,
        .profile_generation = analyzer.profile_generation,
        .persona_generation = analyzer.persona_generation,
        .hardware_tick_hz = analyzer.hardware_tick_hz,
        .timestamp_resolution_ns = analyzer.timestamp_resolution_ns,
        .capture_sequence = s_analyzer_storage_batch.capture_sequence,
        .segment_index = s_analyzer_storage_segment_index,
        .first_record_sequence =
            s_analyzer_storage_batch.first_record_sequence,
        .batch_sequence = s_analyzer_storage_batch.batch_sequence,
    };
    const size_t payload_size = (size_t)s_analyzer_storage_record_count *
                                sizeof(s_analyzer_storage_records[0]);
    const uint32_t file_crc32 = ota_crc32_update(
        ota_crc32_update(0u, (const uint8_t *)&header, sizeof(header)),
        (const uint8_t *)s_analyzer_storage_records, payload_size);
    const uint32_t file_size = (uint32_t)(sizeof(header) + payload_size);
    char path[96];
    if (snprintf(path, sizeof(path), "/traces/run/analyzer_%08lu_%04lu.bin",
                 (unsigned long)header.session,
                 (unsigned long)header.segment_index) <= 0 ||
        file_size > STORAGE_MANAGER_FILE_WRITE_MAX_BYTES) {
        s_analyzer_storage_pending = true;
        return;
    }
    uint32_t txn_id = 0u;
    if (!storage_manager_begin_evidence_write(path, file_size,
                                               file_crc32, &txn_id) ||
        !storage_manager_write_file_chunk(txn_id, 0u,
                                          (const uint8_t *)&header,
                                          sizeof(header)) ||
        !storage_manager_write_file_chunk(
            txn_id, (uint32_t)sizeof(header),
            (const uint8_t *)s_analyzer_storage_records, payload_size) ||
        !storage_manager_commit_file_write(txn_id,
                                            &s_analyzer_storage_job_id)) {
        if (txn_id != 0u) {
            (void)storage_manager_abort_file_write(txn_id);
        }
        s_analyzer_storage_pending = true;
        return;
    }
    s_analyzer_storage_pending = false;
    s_analyzer_storage_job_inflight = true;
}

bool app_init(void)
{
    s_app_ready = false;
    s_app_control_plane_ready = false;
    diagnostics_housekeeping_init();
    if (!board_identity_init()) {
        diagnostics_mark_fault("identity", "unique board identity initialization failed");
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

    if (!resource_arbiter_init()) {
        diagnostics_mark_fault("resource_arbiter", "resource arbiter initialization failed");
        return false;
    }

    if (!flash_transaction_ao_init()) {
        diagnostics_mark_fault("flash_transaction",
                               "flash transaction initialization failed");
        return false;
    }

    /* Product configuration is read after the Flash transaction owner is
     * ready because the DPLL profile is persisted in the same journal. */
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

#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
    if (!usbtmc_scpi_port_init()) {
        diagnostics_mark_fault("usb", "USB SCPI initialization failed");
        return false;
    }
    s_app_control_plane_ready = true;
#else
    s_app_control_plane_ready = true;
#endif

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
    app_analyzer_storage_service();
    diagnostics_housekeeping_service();
}

void app_storage_service(void)
{
    app_tdma_record_service();
    storage_manager_service(250u);
}

typedef void (*app_realtime_load_service_fn)(void);

static volatile uint32_t s_realtime_schedule_guard;
static volatile uint32_t s_realtime_load_enabled_mask =
    APP_REALTIME_LOAD_FOUNDATION_MASK;
static volatile uint32_t s_realtime_load_quarantined_mask;

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
/* Lower SCRATCH_X holds realtime state below the reserved Core1 stack. The link map
 * must account for this snapshot together with the fixed ingress records. */
static app_realtime_priority_snapshot_t __scratch_x("app_priority_snapshot") s_realtime_priority = {
    .schema = 1u,
    .candidate_irq_cycles = PROJECT_CORE1_PRIORITY_RX_IRQ_CYCLES,
    .close_lead_cycles = PROJECT_CORE1_PRIORITY_RX_CLOSE_CYCLES,
    .physical_min_cycles = PROJECT_CORE1_PRIORITY_RX_MIN_PHYSICAL_CYCLES,
};
static bool __scratch_x("app_priority_active") s_realtime_priority_active;
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

bool app_realtime_get_priority_snapshot(app_realtime_priority_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 3u; ++attempt) {
        const uint32_t begin = __atomic_load_n(&s_realtime_schedule_guard, __ATOMIC_ACQUIRE);
        if (begin & 1u) continue;
        const app_realtime_priority_snapshot_t value = s_realtime_priority;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (begin == __atomic_load_n(&s_realtime_schedule_guard, __ATOMIC_ACQUIRE)) {
            *snapshot = value;
            return true;
        }
    }
    return false;
}

typedef tdma_priority_rx_counters_t app_priority_counts_t;

/* Admission has a fixed entry margin. Keep the dispatcher and its two
 * non-inlined helpers in main SRAM so control-plane XIP traffic cannot
 * add flash fetch stalls to every phase decision. This changes neither
 * the phase budgets nor the reject-on-late rule. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define APP_DISPATCH_RAM __not_in_flash("app_realtime_dispatch")
#else
#define APP_DISPATCH_RAM
#endif

/* A closed IRQ cannot change these producer counts between phase services.
 * Reuse the last closed-window sample as the next baseline instead of
 * copying the whole published record twice at every phase release. Offline
 * personas explicitly invalidate it before touching the physical owner. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
static app_priority_counts_t __scratch_x("app_priority_baseline") s_realtime_priority_baseline;
static bool __scratch_x("app_priority_baseline_valid") s_realtime_priority_baseline_valid;
#else
static app_priority_counts_t s_realtime_priority_baseline;
static bool s_realtime_priority_baseline_valid;
#endif

typedef struct {
    uint32_t cycle_epoch, timer_at_epoch;
    bool valid;
} app_priority_clock_map_t;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
static app_priority_clock_map_t __scratch_x("app_priority_clock") s_realtime_priority_clock;
#else
static app_priority_clock_map_t s_realtime_priority_clock;
#endif

/* Core1 reads its serialized producer directly while the source is closed;
 * cross-core diagnostic snapshots are not part of phase accounting. */
APP_DISPATCH_RAM
static __attribute__((noinline)) bool app_priority_counts(app_priority_counts_t *out)
{
    return tdma_runtime_owner_priority_rx_counters_core1(out);
}

static bool app_priority_delta(const app_priority_counts_t *before,
    const app_priority_counts_t *after, uint64_t *cycles, uint32_t *count,
    uint32_t *maximum)
{
    if (before->count == UINT32_MAX || after->count == UINT32_MAX ||
        before->cycles == UINT64_MAX || after->cycles == UINT64_MAX) return false;
    /* A single inactive -> ARM transition resets counters. Rebase while
     * active retains them, and multiple owner transitions are ambiguous;
     * fail attribution closed rather than reset the IRQ quota optimistically. */
    if (before->epoch != after->epoch &&
        (before->active || before->epoch == UINT32_MAX ||
         after->epoch != before->epoch + 1u)) return false;
    const uint64_t base_cycles = before->epoch == after->epoch ? before->cycles : 0u;
    const uint32_t base_count = before->epoch == after->epoch ? before->count : 0u;
    if (after->cycles < base_cycles || after->count < base_count) return false;
    *cycles = after->cycles - base_cycles;
    *count = after->count - base_count;
    if (maximum != NULL) *maximum = after->maximum;
    return true;
}

static void app_priority_record_phase(app_realtime_phase_id_t phase_id,
    uint32_t background_cycles, uint32_t irq_max_cycles, bool budget_miss,
    bool sample_failed, bool close_missed, bool new_run)
{
    app_realtime_schedule_write_begin();
    if (new_run) {
        s_realtime_priority.sample_failures = 0u;
        s_realtime_priority.close_misses = 0u;
        memset(s_realtime_priority.irq_max_cycles, 0, sizeof(s_realtime_priority.irq_max_cycles));
        memset(s_realtime_priority.background_max_cycles, 0,
            sizeof(s_realtime_priority.background_max_cycles));
        memset(s_realtime_priority.budget_misses, 0, sizeof(s_realtime_priority.budget_misses));
    }
    if (sample_failed) s_realtime_priority.sample_failures++;
    if (close_missed) s_realtime_priority.close_misses++;
    if (irq_max_cycles > s_realtime_priority.irq_max_cycles[phase_id])
        s_realtime_priority.irq_max_cycles[phase_id] = irq_max_cycles;
    if (background_cycles > s_realtime_priority.background_max_cycles[phase_id])
        s_realtime_priority.background_max_cycles[phase_id] = background_cycles;
    if (budget_miss) s_realtime_priority.budget_misses[phase_id]++;
    app_realtime_schedule_write_end();
}

APP_DISPATCH_RAM
static __attribute__((noinline)) bool app_priority_deadline(uint32_t cycle_epoch,
    const app_realtime_priority_contract_t *priority, uint32_t *deadline_low)
{
    if (priority == NULL || deadline_low == NULL || priority->irq_quota == 0u)
        return false;
    uint32_t elapsed;
    if (!s_realtime_priority_clock.valid ||
            s_realtime_priority_clock.cycle_epoch != cycle_epoch) {
        s_realtime_priority_clock.valid = false;
        uint64_t timer_ticks;
        if (!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &timer_ticks))
            return false;
        /* SysTick is read AFTER TIMER1, so the translated deadline is early
         * by the bounded read interval, never late. Both use immutable
         * clk_sys during this one table; repeat the binding next cycle. */
        elapsed = app_realtime_elapsed_cycles(cycle_epoch, app_realtime_cycle_now());
        s_realtime_priority_clock.timer_at_epoch = (uint32_t)timer_ticks - elapsed;
        s_realtime_priority_clock.cycle_epoch = cycle_epoch;
        s_realtime_priority_clock.valid = true;
    } else {
        elapsed = app_realtime_elapsed_cycles(cycle_epoch, app_realtime_cycle_now());
    }
    if (elapsed >= priority->close_cycle) return false;
    *deadline_low = s_realtime_priority_clock.timer_at_epoch + priority->close_cycle;
    return true;
}

static void app_realtime_wait_until(uint32_t cycle_epoch, uint32_t target_cycle)
{
    while (app_realtime_elapsed_cycles(cycle_epoch, app_realtime_cycle_now()) <
           target_cycle) {
        tight_loop_contents();
    }
}

bool app_realtime_request_period_us(uint32_t period_us, uint32_t *generation)
{
    const uint64_t cycles = (uint64_t)period_us * (BOARD_SYS_CLOCK_HZ / 1000000u);
    return app_is_ready() && cycles <= UINT32_MAX &&
        app_realtime_profile_supported((uint32_t)cycles) &&
        tdma_service_request_stopped_update(tdma_runtime_owner_get(), (uint32_t)cycles, generation);
}

bool app_realtime_get_period_snapshot(app_realtime_period_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    app_realtime_period_snapshot_t before, after;
    app_realtime_schedule_snapshot_t schedule;
    if (!tdma_service_get_stopped_update(owner, &before.pending_cycles,
            &before.requested_generation, &before.applying) ||
        !app_realtime_get_schedule_snapshot(&schedule) ||
        !tdma_service_get_stopped_update(owner, &after.pending_cycles,
            &after.requested_generation, &after.applying) ||
        before.pending_cycles != after.pending_cycles ||
        before.requested_generation != after.requested_generation ||
        before.applying != after.applying) return false;
    after.active_cycles = schedule.cycle_cycles;
    after.applied_generation = schedule.profile_generation;
    *snapshot = after;
    return true;
}

uint32_t app_realtime_cycle_cycles_core1(void)
{
    return s_realtime_schedule.cycle_cycles;
}

bool app_realtime_apply_pending_profile_core1(void)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    if (owner == NULL || __atomic_load_n(&owner->stopped_update, __ATOMIC_ACQUIRE) == 0u)
        return false;
    if (calibration_manager_p3_offline_active_core1() ||
        calibration_manager_training_offline_active_core1() ||
        calibration_manager_ring_capture_offline_active_core1()) return false;
    uint32_t cycles, generation;
    if (!tdma_service_claim_stopped_update_core1(owner, &cycles, &generation)) return false;
    app_realtime_schedule_write_begin();
    const bool installed = app_realtime_profile_install(&s_realtime_schedule, cycles, generation);
    s_realtime_priority_baseline_valid = false;
    s_realtime_priority_clock.valid = false;
    app_realtime_schedule_write_end();
    /* Publication completes before releasing the service ARM exclusion. A
     * rejected token leaves both the table and its applied generation intact. */
    (void)tdma_service_finish_stopped_update_core1(owner, cycles, generation);
    return installed;
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

/* Core1 has one nonrecursive dispatcher. Keep its cross-service live state
 * below the fixed Core1 stack, rather than stacking it beneath VDC's deep
 * foreground path plus an interrupt. No ISR or Core0 accesses this workspace. */
typedef struct {
    app_realtime_phase_contract_t contract;
    app_realtime_priority_contract_t priority;
    app_priority_counts_t before, after_service, after_wait;
    uint64_t service_irq_cycles, wait_irq_cycles;
    uint32_t deadline_low, phase_start, runtime_cycles, load_bit;
    uint32_t service_irq_count, wait_irq_count, irq_max;
    bool eligible, sampled, clock_ok, sample_failed, previously_active;
    bool optional_load, warmup_cycle, dpll_feedback_load, disabled, start_missed;
    bool run_service, run_cached, run_planned, ran_work, overrun, deadline_missed, own_deadline_missed;
    bool close_missed, new_run;
} app_realtime_phase_work_t;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
static app_realtime_phase_work_t __scratch_x("app_priority_phase") s_realtime_phase_work;
#else
static app_realtime_phase_work_t s_realtime_phase_work;
#endif

APP_DISPATCH_RAM
static bool app_realtime_run_phase(
    uint32_t cycle_epoch,
    app_realtime_phase_id_t phase_id,
    int32_t load_id,
    app_realtime_load_service_fn service)
{
    if (phase_id >= APP_REALTIME_PHASE_COUNT) {
        return false;
    }
    /* A previous phase's lease never carries across its static boundary.
     * Only this IRQ is masked; the transport's sticky source is retained. */
    if (phase_id == APP_REALTIME_PHASE_TDMA ||
        !s_realtime_priority_baseline_valid || s_realtime_priority_baseline.active) {
        tdma_runtime_owner_priority_rx_window_core1(false, 0u, 0u);
    }
    app_realtime_phase_work_t *const work = &s_realtime_phase_work;
    memset(work, 0, sizeof(*work));
    work->contract = (app_realtime_phase_contract_t){
        s_realtime_schedule.phase_start_cycle[phase_id],
        s_realtime_schedule.phase_end_cycle[phase_id],
        s_realtime_schedule.phase_wcet_cycles[phase_id]};
    const app_realtime_phase_contract_t *contract = &work->contract;
    work->priority = app_realtime_phase_priority(phase_id, contract);
    if (work->priority.irq_cycles > contract->wcet_cycles) return false;
    work->eligible = work->priority.irq_quota != 0u;
    /* Prepare in the previous close lead where available, before the phase
     * release. This includes disabled optional phases: only their foreground
     * service is optional, never the separately budgeted ingress window. */
    if (work->eligible && phase_id != APP_REALTIME_PHASE_TDMA &&
        s_realtime_priority_baseline_valid) {
        work->before = s_realtime_priority_baseline;
        work->sampled = true;
    } else {
        work->sampled = !work->eligible || app_priority_counts(&work->before);
    }
    work->previously_active = s_realtime_priority_active;
    if (work->eligible && work->sampled) s_realtime_priority_active = work->before.active != 0u;
    /* Only the TDMA service can ARM/STOP the source in this online table.
     * An inactive source has no per-phase IRQ accounting or closing wait;
     * keep the TDMA before/after observation to detect a new ARM below. */
    if (work->eligible && phase_id != APP_REALTIME_PHASE_TDMA &&
        work->sampled && !work->before.active) work->eligible = false;
    work->clock_ok = !work->eligible || !work->before.active ||
        app_priority_deadline(cycle_epoch, &work->priority, &work->deadline_low);
    work->sample_failed = work->eligible && (!work->sampled || !work->clock_ok);
    work->optional_load = load_id >= 0;
    work->warmup_cycle =
        s_realtime_schedule.cycle_count <=
        PROJECT_CORE1_SCHEDULE_WARMUP_CYCLES;
    work->load_bit = work->optional_load
        ? 1u << (uint32_t)load_id : 0u;
    const uint32_t enabled_mask = __atomic_load_n(
        &s_realtime_load_enabled_mask, __ATOMIC_ACQUIRE);
    const uint32_t quarantined_mask = __atomic_load_n(
        &s_realtime_load_quarantined_mask, __ATOMIC_ACQUIRE);
    work->dpll_feedback_load =
        work->optional_load && load_id == (int32_t)APP_REALTIME_LOAD_DPLL;
    work->disabled = service == NULL ||
        (work->optional_load && ((enabled_mask & work->load_bit) == 0u ||
            (!work->dpll_feedback_load && (quarantined_mask & work->load_bit) != 0u)));
    app_realtime_wait_until(cycle_epoch, contract->start_cycle);
    work->phase_start = app_realtime_elapsed_cycles(
        cycle_epoch, app_realtime_cycle_now());

    /* A phase may consume only its own [start,end) interval. If its declared
     * WCET no longer fits, it is skipped instead of borrowing a later phase. */
    work->start_missed = work->phase_start >= work->priority.close_cycle ||
        contract->wcet_cycles > work->priority.close_cycle - work->phase_start;
    work->run_service = !work->disabled && !work->start_missed;
    if (!work->run_service) app_realtime_record_skip(phase_id, !work->disabled && work->start_missed);
    const uint32_t start_counter = app_realtime_cycle_now();
    uint32_t cached_request = 0u;
    uint32_t planned_request = 0u;
    /* Skip accounting can consume the remaining slack. Recheck immediately
     * before this independent bounded handoff, with priority ingress closed.
     * Failed IRQ sampling/clock translation cannot reopen that source, but
     * does not invalidate this core-local phase clock or hide its failure. */
    const uint32_t handoff_start = app_realtime_elapsed_cycles(cycle_epoch, start_counter);
    work->run_planned = !work->run_service && !work->disabled && work->start_missed &&
        phase_id == APP_REALTIME_PHASE_TDMA && handoff_start < work->priority.close_cycle &&
        PROJECT_CORE1_RUN_OUTPUT_PLAN_WCET_CYCLES <= work->priority.close_cycle - handoff_start;
    work->run_cached = !work->run_planned && !work->run_service && !work->disabled && work->start_missed &&
        phase_id == APP_REALTIME_PHASE_TDMA && handoff_start < work->priority.close_cycle &&
        PROJECT_CORE1_RUN_OUTPUT_HANDOFF_WCET_CYCLES <= work->priority.close_cycle - handoff_start;
    if (work->run_planned) {
        work->phase_start = handoff_start;
        planned_request = vdc_run_output_service_planned_core1();
    }
    if (work->run_cached) {
        work->phase_start = handoff_start;
        cached_request = vdc_run_output_service_cached_core1();
    }
    if (work->run_service) {
        if (work->eligible && work->before.active && work->sampled && work->clock_ok)
            tdma_runtime_owner_priority_rx_window_core1(true, work->priority.irq_quota, work->deadline_low);
        service();
        /* Closing before the end clock prevents a later wait IRQ from being
         * subtracted from a service interval that did not contain it. The
         * complete gate/ISR wall cost stays in the original runtime gate. */
        if (work->eligible) tdma_runtime_owner_priority_rx_window_core1(false, 0u, 0u);
    }
    const uint32_t end_counter = app_realtime_cycle_now();
    work->ran_work = work->run_service || work->run_cached || work->run_planned;
    work->runtime_cycles = work->ran_work ?
        app_realtime_elapsed_cycles(start_counter, end_counter) : 0u;
    if (work->run_cached)
        vdc_run_output_note_cached_wall_core1(cached_request, work->runtime_cycles,
            PROJECT_CORE1_RUN_OUTPUT_HANDOFF_WCET_CYCLES);
    if (work->run_planned)
        vdc_run_output_note_planned_wall_core1(planned_request, work->runtime_cycles,
            PROJECT_CORE1_RUN_OUTPUT_PLAN_WCET_CYCLES);
    const uint32_t phase_end = app_realtime_elapsed_cycles(
        cycle_epoch, end_counter);
    if (work->run_service && phase_id == APP_REALTIME_PHASE_TDMA)
        tdma_service_timing_scheduler_end(work->runtime_cycles);
    work->overrun = work->runtime_cycles > (work->run_planned ?
        PROJECT_CORE1_RUN_OUTPUT_PLAN_WCET_CYCLES : work->run_cached ?
        PROJECT_CORE1_RUN_OUTPUT_HANDOFF_WCET_CYCLES : contract->wcet_cycles);
    work->deadline_missed = work->ran_work && phase_end > contract->end_cycle;
    const bool inherited_lateness = work->phase_start > contract->start_cycle;
    work->own_deadline_missed = work->deadline_missed && !inherited_lateness;

    work->close_missed = false;
    work->new_run = false;
    if (work->eligible) {
        const bool after_service_valid = app_priority_counts(&work->after_service);
        if (after_service_valid) s_realtime_priority_active = work->after_service.active != 0u;
        work->new_run = work->sampled && after_service_valid && !work->before.active && work->after_service.active &&
            work->before.epoch != UINT32_MAX && work->after_service.epoch == work->before.epoch + 1u;
        work->sampled = work->sampled && after_service_valid &&
            app_priority_delta(&work->before, &work->after_service,
                &work->service_irq_cycles, &work->service_irq_count, &work->irq_max);
        if (!work->sampled || work->service_irq_cycles > work->runtime_cycles) {
            work->sample_failed = true;
            work->sampled = false;
        }
        if (work->sampled && !work->before.active && !work->after_service.active &&
            work->service_irq_count == 0u) {
            /* A stopped/origin TDMA beat only needs its two small owner
             * reads. Do not spend the phase tail preparing an absent ISR. */
            work->eligible = false;
            s_realtime_priority_baseline = work->after_service;
            s_realtime_priority_baseline_valid = true;
        } else if (work->sampled && !work->before.active && work->after_service.active) {
            /* ARM happened with the source closed inside this TDMA service.
             * Admit only this phase's remaining window using a fresh clock. */
            work->clock_ok = app_priority_deadline(cycle_epoch, &work->priority, &work->deadline_low);
            if (!work->clock_ok) work->sample_failed = true;
        }
    }
    if (work->eligible) {
        const uint32_t remaining = work->sampled && work->service_irq_count < work->priority.irq_quota ?
            work->priority.irq_quota - work->service_irq_count : 0u;
        const uint32_t now = app_realtime_elapsed_cycles(cycle_epoch, app_realtime_cycle_now());
        if (work->clock_ok && remaining != 0u && now < work->priority.close_cycle)
            tdma_runtime_owner_priority_rx_window_core1(true, remaining, work->deadline_low);
        app_realtime_wait_until(cycle_epoch, work->priority.close_cycle);
        tdma_runtime_owner_priority_rx_window_core1(false, 0u, 0u);
        const uint32_t closed = app_realtime_elapsed_cycles(cycle_epoch, app_realtime_cycle_now());
        /* Entry may begin just before cutoff and consume its complete C.
         * The explicit control margin then remains before the phase end. */
        work->close_missed = closed >
            contract->end_cycle - PROJECT_CORE1_PRIORITY_RX_CLOSE_MARGIN_CYCLES;
        work->deadline_missed = work->deadline_missed || closed > contract->end_cycle;
        const bool after_wait_valid = app_priority_counts(&work->after_wait);
        s_realtime_priority_baseline_valid = after_wait_valid;
        if (after_wait_valid) s_realtime_priority_baseline = work->after_wait;
        if (after_wait_valid) s_realtime_priority_active = work->after_wait.active != 0u;
        work->sampled = work->sampled && after_wait_valid &&
            app_priority_delta(&work->after_service, &work->after_wait,
                &work->wait_irq_cycles, &work->wait_irq_count, &work->irq_max);
        if (!work->sampled) work->sample_failed = true;
    }
    /* Body-only IRQ measurements are attribution, not permission to weaken
     * the wall gate. Unmeasured entry/finish/return remain charged to the
     * foreground; a separate candidate tail is also reserved for wait IRQs. */
    bool priority_miss = false;
    if (work->eligible) {
        const uint32_t background_cycles = work->sampled ?
            work->runtime_cycles - (uint32_t)work->service_irq_cycles : work->runtime_cycles;
        const uint64_t irq_count = (uint64_t)work->service_irq_count + work->wait_irq_count;
        const uint64_t irq_charge = work->service_irq_cycles + work->wait_irq_cycles +
            irq_count * PROJECT_CORE1_PRIORITY_RX_TAIL_CYCLES;
        const uint64_t total_charge = work->runtime_cycles + work->wait_irq_cycles +
            (uint64_t)work->wait_irq_count * PROJECT_CORE1_PRIORITY_RX_TAIL_CYCLES;
        priority_miss = ((work->run_cached || work->run_planned) && work->overrun) ||
            !work->sampled || !work->clock_ok || work->close_missed ||
            background_cycles > work->priority.background_cycles || irq_count > work->priority.irq_quota ||
            irq_charge > work->priority.irq_cycles || total_charge > contract->wcet_cycles ||
            (irq_count != 0u && (uint64_t)work->irq_max + PROJECT_CORE1_PRIORITY_RX_TAIL_CYCLES >
                PROJECT_CORE1_PRIORITY_RX_IRQ_CYCLES);
        /* Retain the last ARM lifetime through STOP; only a new observed
         * ARM resets it. No inactive maintenance phase publishes budgets. */
        const bool observed_run = work->previously_active || work->before.active || work->after_service.active ||
            work->after_wait.active || irq_count;
        if (observed_run) app_priority_record_phase(phase_id, background_cycles,
            irq_count != 0u ? work->irq_max : 0u, priority_miss, work->sample_failed, work->close_missed, work->new_run);
    }

    app_realtime_schedule_write_begin();
    if (work->ran_work) {
        s_realtime_schedule.phase_last_start_cycle[phase_id] = work->phase_start;
        s_realtime_schedule.phase_last_runtime_cycles[phase_id] = work->runtime_cycles;
        if (work->run_service) s_realtime_schedule.phase_run_count[phase_id]++;
        if (work->runtime_cycles > s_realtime_schedule.phase_max_runtime_cycles[phase_id])
            s_realtime_schedule.phase_max_runtime_cycles[phase_id] = work->runtime_cycles;
    }
    if (work->overrun) {
        s_realtime_schedule.phase_overrun_count[phase_id]++;
    }
    if (work->deadline_missed) {
        s_realtime_schedule.phase_deadline_miss_count[phase_id]++;
    }
    if (work->overrun || work->deadline_missed || priority_miss) {
        s_realtime_schedule.schedule_miss_count++;
    }
    app_realtime_schedule_write_end();

    /* DPLL is a diagnostic/control load carried by a healthy TDMA node.  Its
     * loss of lock or local timing overrun must remain visible as feedback,
     * but must never quarantine the node's TDMA service. */
    if ((work->overrun || work->own_deadline_missed) && work->optional_load && !work->warmup_cycle &&
        !work->dpll_feedback_load) {
        (void)__atomic_fetch_or(&s_realtime_load_quarantined_mask,
                                work->load_bit,
                                __ATOMIC_ACQ_REL);
    }
    /* Count the bounded dispatcher tail as part of this phase's elapsed
     * interval too. A slow snapshot/accounting path cannot be hidden merely
     * because the foreground and IRQ both returned before the deadline. */
    if (app_realtime_elapsed_cycles(cycle_epoch, app_realtime_cycle_now()) >
            contract->end_cycle && !work->deadline_missed) {
        work->deadline_missed = true;
        app_realtime_schedule_write_begin();
        s_realtime_schedule.phase_deadline_miss_count[phase_id]++;
        s_realtime_schedule.schedule_miss_count++;
        app_realtime_schedule_write_end();
    }
    if (work->overrun || work->deadline_missed || priority_miss || (!work->disabled && work->start_missed)) {
        return false;
    }
    return true;
}

#undef APP_DISPATCH_RAM

static void app_realtime_tdma_phase(void)
{
    tdma_service_timing_phase_begin();
    tdma_service_timing_context(tdma_runtime_owner_timing_context(), true);
    /* NO5 is a ring-external read-only observer.  Keep its phase-only
     * SyncIO/VDC path alive, but never service the TDMA owner on that board;
     * this prevents accidental PIO/SM/DMA/GPIO/IRQ/DREQ activity while the
     * NO1..NO4 state-machine ring is running independently. */
    if (board_identity_get_no() != 5u) {
        tdma_component_core1_service();
    }
    vdc_run_output_service_core1();
    /* The analyzer intent mailbox is a mandatory bounded Core1 service.
     * It must not live behind an optional/quarantinable load, otherwise an
     * accepted ARM/STOP could remain pending forever.  TDMA remains first;
     * analyzer work is capped to a small record budget afterward. */
    uint64_t timing_start = tdma_service_timing_now();
    sync_io_logic_analyzer_service_core1(8u);
    tdma_service_timing_record(TDMA_TIMING_ANALYZER, timing_start);
    timing_start = tdma_service_timing_now();
    drv_watchdog_mark_progress(1u, 0x0101u);
    diagnostics_record_core1_loop();
    diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_CORE1);
    drv_watchdog_mark_progress(1u, 0x0103u);
    tdma_service_timing_record(TDMA_TIMING_ACCOUNTING, timing_start);
    tdma_service_timing_context(tdma_runtime_owner_timing_context(), false);
    tdma_service_timing_phase_end();
}

static void app_realtime_vdc_phase(void)
{
    tdma_runtime_owner_service_observer();
    vdc_sync_ao_service();
    drv_watchdog_mark_progress(1u, 0x0111u);
}

static void app_realtime_dpll_phase(void)
{
    tdma_runtime_owner_service_observer();
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
    vdc_dpll_manager_sync_io_capture_service_core1();
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
    /* Offline personas, cycle sleep and GUARD have no ingress reservation. */
    tdma_runtime_owner_priority_rx_window_core1(false, 0u, 0u);
    /* A stopped maintenance persona may have touched the timer. No clock
     * mapping survives a table boundary, even if SysTick's low24 repeats. */
    s_realtime_priority_clock.valid = false;
    /* P3 is an offline physical-calibration session: TDMA is stopped and the
     * calibration owner temporarily owns the shared PIO/DMA persona.  Keep
     * it outside the TDMA realtime phase/load-mask contract and advance one
     * bounded transition per core1 cycle. */
    if (calibration_manager_p3_offline_active_core1()) {
        s_realtime_priority_baseline_valid = false;
        calibration_manager_p3_service_core1();
        drv_watchdog_mark_progress(1u, 0x0104u);
        diagnostics_record_core1_loop();
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_CORE1);
        return;
    }
    /* Raw ring capture temporarily repurposes the resident capture SM while
     * the autonomous TDMA PIO/DMA persona remains armed.  It is a diagnostic
     * maintenance lifecycle, not an online calibration load.  Give it one
     * bounded transition before the online table, then continue into the
     * complete TDMA/VDC/DPLL cycle so the diagnostic job cannot starve the
     * realtime path. */
    const bool ring_capture_maintenance =
        calibration_manager_ring_capture_offline_active_core1();
    if (ring_capture_maintenance) {
        s_realtime_priority_baseline_valid = false;
        calibration_manager_service_core1();
        drv_watchdog_mark_progress(1u, 0x0104u);
    }
    /* All stopped-ring calibration/training personas run outside the online
     * TDMA phase table.  Their PIO/DMA persona transitions may take longer
     * than the optional online snapshot phase, but they cannot perturb a
     * running short-frame cycle or be hidden by its quarantine mechanism. */
    if (calibration_manager_training_offline_active_core1()) {
        s_realtime_priority_baseline_valid = false;
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
    /* The maintenance beat above owns the calibration-manager service for
     * this cycle.  Do not invoke it again through the online load slot when
     * that slot happens to be enabled by a diagnostic configuration. */
    if (!ring_capture_maintenance) {
        (void)app_realtime_run_phase(cycle_epoch,
                                     APP_REALTIME_PHASE_CALIBRATION,
                                     APP_REALTIME_LOAD_CALIBRATION,
                                     app_realtime_calibration_phase);
    } else {
        /* Maintenance already serviced the foreground owner. Its static
         * online interval still admits the independent priority ingress. */
        (void)app_realtime_run_phase(cycle_epoch,
                                     APP_REALTIME_PHASE_CALIBRATION,
                                     APP_REALTIME_LOAD_CALIBRATION,
                                     NULL);
    }
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
