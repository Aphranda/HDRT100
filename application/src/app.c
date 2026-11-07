#include "app.h"
#include "app_realtime_profile.h"
#include "app_refmem_domain_config.h"
#include "refmem_function.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_config.h"
#include "calibration_manager.h"
#include "board_identity.h"
#include "diagnostics.h"
#include "drv_flash.h"
#include "drv_watchdog.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "event_bus.h"
#include "flash_transaction.h"
#include "loop_engine.h"
#include "model_turntable.h"
#include "ota_ao.h"
#include "ota_tdma_binding.h"

/* Bumped whenever the relay diagnostics change, so a retained phase read
 * after a flash proves which image is actually running. */
#define OTA_STREAM_BOOT_SENTINEL 91u
#include "product_config.h"
#include "project_config.h"
#include "resource_arbiter.h"
#include "scpi_port.h"
#include "rs485_communication.h"
#include "storage_manager.h"
#include "system_manager.h"
#include "sync_trigger.h"
#include "trigger_sequence_service.h"
#include "trigger_t2_runtime.h"
#include "tdma_runtime_owner.h"
#include "tdma_boot_plan.h"
#include "tdma_boot_profile.h"
#include "tdma_boot_profile_store.h"
#include "tdma_boot_sequence.h"
#include "tdma_guide_scan.h"
#include "tdma_boot_stage.h"
#include "calibration_training_store.h"
#include "tdma_operating_profile.h"
#include "tdma_service.h"
#include "tdma_service_timing.h"
#include "tdma_ring_phase.h"
#include "sync_io.h"
#include "sync_io_reference.h"
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
static tdma_boot_plan_t s_boot_plan;
static tdma_boot_step_t s_boot_steps[TDMA_BOOT_SEQUENCE_MAX];
static size_t s_boot_step_count;
static tdma_boot_profile_t s_boot_profile;
static bool s_boot_profile_loaded;
static bool s_boot_guide_done;
static uint32_t s_boot_fail_step;
static bool s_boot_listen_pending;
static uint32_t s_boot_listen_rx_base;
static uint64_t s_boot_listen_deadline_ms;
static bool s_boot_join_timeout;
static bool s_boot_join_frame_seen;
static bool s_boot_ring_watch_active;
static uint64_t s_boot_ring_watch_at_ms;
static bool s_boot_half_ring;
static uint32_t s_boot_topology_fail;
static uint32_t s_boot_apply_fail;
static bool s_boot_attempted;
static uint64_t s_boot_deadline_ms;
static size_t s_boot_next_step;
/* First moment this board saw the guide ring carry traffic (START success on
 * the coordinator, first decoded frame on a follower). The P0T scan anchors
 * its trial grid on it so all four boards share one wall-clock schedule. */
static uint64_t s_boot_ring_up_ms;

/* Leave a guide maintenance window: stop the ring, drop probe mode and replay
 * the frozen boot steps so the full guide ring comes back.  This is the single
 * "stop ring / re-form ring" primitive in the app: the P0T scan and the CTRQS
 * executor both close their window through it instead of owning a second
 * stop/re-form flow. */
static void app_guide_window_close(uint64_t now_ms)
{
    (void)distributed_refmem_tdma_ring_stop();
    (void)calibration_manager_set_topology_probe_mode(false, 0u);
    s_boot_guide_done = false;
    s_boot_attempted = true;
    s_boot_next_step = 0u;
    s_boot_deadline_ms = now_ms + 30000u;
    s_boot_ring_watch_active = false;
}

/* M1 P0T pair scan. The schedule is anchored on the local ring-formation
 * moment, so the four boards keep the same trial slots without a cross-board
 * command: every board derives (driver, receiver, idle) from its own slot. */
#if defined(PROJECT_TDMA_GUIDE_SCAN) && PROJECT_TDMA_GUIDE_SCAN
#define APP_GUIDE_SCAN_TRIAL_MS 5000u
/* Fixed post-reset start of the first trial slot (all boards reset together). */
#define APP_GUIDE_SCAN_EPOCH_MS 12500u
#define APP_GUIDE_SCAN_SETTLE_MS 4000u
#define APP_GUIDE_SCAN_WAIT_MS 45000u
#define APP_GUIDE_SCAN_PROBE_PHASE_CYCLES 10u
#define APP_GUIDE_SCAN_PROBE_LEVEL 7u
#define APP_GUIDE_SCAN_TRAIN_CYCLES 512u

#define APP_GUIDE_PHASE_STOP 0u
#define APP_GUIDE_PHASE_TOPOLOGY 1u
#define APP_GUIDE_PHASE_PROBE 2u
#define APP_GUIDE_PHASE_ARM 3u
#define APP_GUIDE_PHASE_ARM_WAIT 4u
#define APP_GUIDE_PHASE_TRAIN 5u
#define APP_GUIDE_PHASE_START 6u
#define APP_GUIDE_PHASE_SAMPLE 7u
#define APP_GUIDE_PHASE_FINISH 11u
#define APP_GUIDE_PHASE_APPLY 9u
#define APP_GUIDE_PHASE_SETTLE 10u
/* The phase walker advances while phase <= LAST, so FINISH must stay the
 * largest phase id; the time order lives in k_guide_phase_ms, not in the ids. */
#define APP_GUIDE_PHASE_LAST APP_GUIDE_PHASE_FINISH

/* Phase offsets inside one trial slot (milliseconds). A STOP of a running ring
 * takes over a second to retire and the topology write stages another stop, so
 * the settle phase waits for applied_config_seq to catch up before ARM; the
 * transmit/sampling window is wide and polled on every service call. */
static const uint32_t k_guide_phase_ms[] = {
    [APP_GUIDE_PHASE_STOP] = 0u,
    [APP_GUIDE_PHASE_TOPOLOGY] = 900u,
    [APP_GUIDE_PHASE_SETTLE] = 2100u,
    [APP_GUIDE_PHASE_PROBE] = 2100u,
    [APP_GUIDE_PHASE_ARM] = 2300u,
    [APP_GUIDE_PHASE_ARM_WAIT] = 2400u,
    [APP_GUIDE_PHASE_TRAIN] = 3300u,
    [APP_GUIDE_PHASE_START] = 3400u,
    [APP_GUIDE_PHASE_SAMPLE] = 3400u,
    [APP_GUIDE_PHASE_FINISH] = 4700u,
};

#define APP_GUIDE_SCAN_SETTLE_DEADLINE_MS 2300u
#define APP_GUIDE_SCAN_DRIVER_START_MS 3700u
#define APP_GUIDE_SCAN_ARM_DEADLINE_MS 3200u
#define APP_GUIDE_SCAN_SAMPLE_DEADLINE_MS 4700u
/* fail_phase values outside the per-trial phase range. */
#define APP_GUIDE_SCAN_FAIL_WAIT_RING 100u
#define APP_GUIDE_SCAN_FAIL_RESTORE 101u
#define APP_GUIDE_SCAN_FAIL_PRELUDE 102u
#define APP_GUIDE_SCAN_FAIL_PRELUDE_STAGE 103u
#define APP_GUIDE_SCAN_FAIL_PRELUDE_APPLY 104u

static struct {
    uint32_t state;
    tdma_guide_scan_table_t table;
    uint64_t epoch_ms;
    uint64_t wait_deadline_ms;
    uint64_t trial_start_ms;
    uint64_t arm_deadline_ms;
    uint64_t prelude_deadline_ms;
    uint64_t sample_deadline_ms;
    uint32_t index;
    uint32_t phase;
    uint32_t role;
    uint32_t rx_base;
    uint32_t words_base;
    uint32_t edges_base;
    uint32_t skipped;
    uint32_t failures;
    uint32_t fail_phase;
    uint32_t arm_result;
    uint32_t arm_attempts;
    uint32_t arm_gap;
    uint32_t staged_enabled;
    uint32_t trials_started;
    uint32_t trials_armed;
    uint32_t max_late_ms;
    uint32_t stall_ms;
    uint32_t frames_delta;
    uint32_t words_delta;
    uint32_t tx_delta;
    uint32_t sample_started;
    uint32_t tx_base;
    /* Per-pair evidence: role, local counter deltas, the pair snapshot taken at
     * the sample point, and when the window actually started relative to the
     * shared epoch (proves the four boards ran the same slot at the same time). */
    uint8_t pair_role[TDMA_GUIDE_SCAN_PAIR_MAX];
    uint32_t pair_frames[TDMA_GUIDE_SCAN_PAIR_MAX];
    uint32_t pair_words[TDMA_GUIDE_SCAN_PAIR_MAX];
    uint32_t pair_tx[TDMA_GUIDE_SCAN_PAIR_MAX];
    uint32_t pair_state[TDMA_GUIDE_SCAN_PAIR_MAX];
    uint32_t pair_start_offset[TDMA_GUIDE_SCAN_PAIR_MAX];
    uint32_t pair_service[TDMA_GUIDE_SCAN_PAIR_MAX];
    uint32_t pair_error[TDMA_GUIDE_SCAN_PAIR_MAX];
    uint32_t service_base;
    uint64_t last_call_ms;
    uint64_t arm_next_try_ms;
    bool trial_active;
} s_guide_scan;
#endif /* PROJECT_TDMA_GUIDE_SCAN */

#if defined(PROJECT_TDMA_GUIDE_STAGE_BOOT) && PROJECT_TDMA_GUIDE_STAGE_BOOT
/* S 段（bench）：有已提交的 TRN-03 stage 时，用它被测时的 profile level 起环，
 * 而不是引导档的 10 kHz。stage 只带 profile_crc32，因此先在目录里把 CRC 映射回
 * level；映射不到就保持引导档（fail-closed，不猜）。 */
static void app_guide_stage_boot_override(void)
{
    /* The persisted record itself is the boot-level authority: `loaded` is set
     * later by the context-bound restore path, so requiring it here would race
     * the boot (measured 2026-11-05: one of four boards loaded in time and the
     * rest fell back, leaving a mixed-level ring). A record from another
     * topology/profile is rejected by the ARM gate and by the store's context
     * check at restore time; a pre-boot CRC cross-check is a follow-up. */
    calibration_training_store_status_t status;
    calibration_manager_get_training_store_status(&status);
    if (status.persisted_valid == 0u) {
        return;
    }
    tdma_ring_calibration_stage_t stage;
    if (!calibration_training_store_get_stage(&stage)) {
        return;
    }
    tdma_boot_stage_level_t levels[32];
    size_t level_count = 0u;
    for (uint32_t level = 0u; level < 32u; level++) {
        tdma_operating_profile_t profile;
        if (!tdma_operating_profile_get(level, &profile)) {
            continue;
        }
        levels[level_count].level = level;
        levels[level_count].profile_crc32 = profile.profile_crc32;
        level_count++;
    }
    tdma_boot_stage_plan_t plan;
    tdma_boot_stage_decide(true, stage.profile_crc32, levels, level_count,
                           &plan);
    if (plan.action != TDMA_BOOT_STAGE_ACTION_STAGE) {
        return;
    }
    s_boot_profile.profile_level = plan.level;
    s_boot_profile.flags &= ~TDMA_BOOT_PROFILE_FLAG_BRINGUP_NO_CAL;
}
#endif

#if defined(PROJECT_TDMA_GUIDE_AUTOFORM) && PROJECT_TDMA_GUIDE_AUTOFORM
/* Bench-only: build the guide profile from the board number (NO.1 -> slot 0)
 * so power-up self-forming can be exercised before a provisioning command owns
 * the durable store. The topology identity is a fixed bench tag shared by all
 * boards; an empty store still means "no profile" in product images. */
static bool app_guide_synth_profile(tdma_boot_profile_t *out)
{
    static const char k_bench_topology[] = "guide-bench";
    const uint32_t no = board_identity_get_no();
    if (no < 1u || no > (uint32_t)PROJECT_TDMA_GUIDE_NODES) {
        return false;
    }
    tdma_boot_profile_t profile = {
        .version = TDMA_BOOT_PROFILE_VERSION,
        .profile_level = (uint32_t)PROJECT_TDMA_GUIDE_LEVEL,
        .node_count = (uint32_t)PROJECT_TDMA_GUIDE_NODES,
        .local_slot = no - 1u,
        .flags = TDMA_BOOT_PROFILE_FLAG_BRINGUP_NO_CAL,
        .topology_crc32 = tdma_boot_profile_crc32(
            (const uint8_t *)k_bench_topology, sizeof(k_bench_topology) - 1u),
        .generation = 1u,
    };
    if (!tdma_boot_profile_valid(&profile)) {
        return false;
    }
    *out = profile;
    return true;
}
#endif

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

    /* Single-port guide ring: load the durable boot profile once (v2 map) and
     * freeze the local formation decision. Executing the step list is a
     * separate boot stage; on the v1 map the store reports unsupported and the
     * plan stays NONE (multi-port bring-up). */
    (void)tdma_boot_profile_store_init();
    tdma_boot_profile_t boot_profile;
    bool have_boot_profile = tdma_boot_profile_store_get(&boot_profile);
#if defined(PROJECT_TDMA_GUIDE_AUTOFORM) && PROJECT_TDMA_GUIDE_AUTOFORM
    if (!have_boot_profile) {
        have_boot_profile = app_guide_synth_profile(&boot_profile);
    }
#endif
    if (have_boot_profile) {
        s_boot_profile = boot_profile;
        s_boot_profile_loaded = true;
#if defined(PROJECT_TDMA_GUIDE_STAGE_BOOT) && PROJECT_TDMA_GUIDE_STAGE_BOOT
        app_guide_stage_boot_override();
#endif
        tdma_boot_plan_decide(true, &s_boot_profile, s_boot_profile.node_count,
                              s_boot_profile.local_slot, &s_boot_plan);
    } else {
        tdma_boot_plan_decide(false, NULL, 0u, 0u, &s_boot_plan);
    }
    s_boot_step_count = tdma_boot_sequence_build(
        &s_boot_plan, s_boot_steps, TDMA_BOOT_SEQUENCE_MAX);

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

    /* N6: register the domain-config handlers after all owning components
     * exist and before SCPI commands can reach the RefMem dispatcher. */
    if (!app_refmem_domain_config_init()) {
        diagnostics_mark_fault("refmem", "domain config handler registration failed");
        return false;
    }
    refmem_function_init();

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

bool app_guide_boot_plan_get(tdma_boot_plan_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = s_boot_plan;
    return true;
}

size_t app_guide_boot_steps_get(tdma_boot_step_t *out, size_t capacity)
{
    if (out == NULL || capacity < s_boot_step_count) {
        return 0u;
    }
    for (size_t i = 0u; i < s_boot_step_count; i++) {
        out[i] = s_boot_steps[i];
    }
    return s_boot_step_count;
}

static uint32_t app_guide_apply_fail_mask(void)
{
    uint32_t mask = 0u;
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring)) {
        mask |= 1u;
    } else {
        if (ring.enabled != 0u) {
            mask |= 2u;
        }
        if (ring.adapter_started != 0u) {
            mask |= 4u;
        }
        if (ring.config_seq != ring.applied_config_seq) {
            mask |= 8u;
        }
    }
    tdma_service_ring_runtime_config_t config;
    if (!tdma_runtime_owner_get_staged_ring_config(&config) ||
        config.enabled == 0u) {
        mask |= 16u;
    }
    tdma_operating_profile_manager_t manager;
    if (!tdma_runtime_owner_get_operating_profile(&manager)) {
        mask |= 32u;
    } else {
        tdma_operating_profile_result_t result;
        if (!tdma_operating_profile_validate(&manager.staged, &result)) {
            mask |= 64u;
        }
        if (manager.staged.level != s_boot_profile.profile_level) {
            mask |= 128u;
        }
    }
    return mask;
}

static bool app_guide_run_step(tdma_boot_step_t step)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    switch (step) {
    case TDMA_BOOT_STEP_STAGE_PROFILE:
        return s_boot_profile_loaded &&
               tdma_runtime_owner_stage_operating_profile(
                   s_boot_profile.profile_level);
    case TDMA_BOOT_STEP_APPLY_PROFILE:
        if (tdma_runtime_owner_apply_operating_profile()) {
            return true;
        }
        s_boot_apply_fail = app_guide_apply_fail_mask();
        return false;
    case TDMA_BOOT_STEP_SET_BRINGUP_TOPOLOGY:
        /* The staged ring config is built against the applied profile; the
         * adapter link map is the bring-up topology the flight engine reads. */
        if (!s_boot_profile_loaded) {
            s_boot_topology_fail = 1u;
            return false;
        }
        /* Mirrors the SCPI RING:TOPology path: the setter reports "changed",
         * not "ok", and the ring/VDC/RefMem trio can still be settling right
         * after boot. Retry inside the step and accept an already-matching
         * snapshot instead of latching the first refusal. */
        {
            bool topology_ok = false;
            for (uint32_t attempt = 0u; attempt < 50u; attempt++) {
                if (distributed_refmem_set_tdma_ring_topology(
                        s_boot_profile.local_slot, 0u,
                        s_boot_profile.node_count)) {
                    topology_ok = true;
                    break;
                }
                tdma_ring_runtime_snapshot_t ring;
                if (tdma_runtime_owner_get_ring_snapshot(&ring) &&
                    ring.node_count == s_boot_profile.node_count &&
                    ring.local_slot_id == s_boot_profile.local_slot) {
                    topology_ok = true;
                    break;
                }
                sleep_ms(20u);
            }
            if (!topology_ok) {
                s_boot_topology_fail = 2u;
                return false;
            }
        }
        if (!tdma_runtime_owner_set_bringup_topology(
                s_boot_profile.node_count, 0u)) {
            s_boot_topology_fail = 3u;
            return false;
        }
        return true;
    case TDMA_BOOT_STEP_SET_BRINGUP_NO_CAL:
        return tdma_runtime_owner_set_bringup_no_cal(true);
    case TDMA_BOOT_STEP_RING_ARM:
        /* Use the same entry the SCPI RING:ARM uses: it configures the default
         * flight map from the staged node count before arming. Calling
         * tdma_service_ring_arm directly skips that map and the physical layer
         * rejects the ARM with OWNER_FLIGHT_MAP. */
        return distributed_refmem_tdma_ring_arm();
    case TDMA_BOOT_STEP_RING_START: {
        const bool started = owner != NULL && tdma_service_ring_start(owner);
        if (started) {
            s_boot_ring_up_ms = to_ms_since_boot(get_absolute_time());
        }
        return started;
    }
    case TDMA_BOOT_STEP_RING_LISTEN: {
        /* ARM only arms the hardware capture; the software data plane (and
         * therefore rx_count/up_running) only runs after START sets
         * data_enabled. A follower that waits for a frame before STARTing can
         * never hear one, so LISTEN starts immediately and keeps a bounded
         * watchdog only to record whether frames actually arrived. */
        tdma_ring_runtime_snapshot_t ring;
        if (!tdma_runtime_owner_get_ring_snapshot(&ring) ||
            ring.adapter_started == 0u) {
            return false;
        }
        s_boot_listen_rx_base = ring.adapter_rx_count;
        s_boot_listen_deadline_ms = to_ms_since_boot(get_absolute_time()) + 5000u;
        if (owner == NULL || !tdma_service_ring_start(owner)) {
            return false;
        }
        s_boot_ring_up_ms = to_ms_since_boot(get_absolute_time());
        s_boot_listen_pending = true;
        return true;
    }
    case TDMA_BOOT_STEP_STOP: {
        /* A board that boots with a persisted/staged config may already be
         * enabled; STOP is the recipe's first step. When the ring is already
         * disabled, do not issue another stop: each stop bumps config_seq and
         * would keep the later apply from ever seeing an applied config. */
        tdma_ring_runtime_snapshot_t ring;
        const bool have_ring = tdma_runtime_owner_get_ring_snapshot(&ring);
        if (have_ring && ring.enabled == 0u && ring.adapter_started == 0u) {
            return true;
        }
        if (owner != NULL && tdma_service_ring_stop(owner)) {
            return true;
        }
        return have_ring && ring.enabled == 0u && ring.adapter_started == 0u;
    }
    case TDMA_BOOT_STEP_NONE:
    default:
        return true;
    }
}

bool app_guide_boot_service(void)
{
    if (s_boot_guide_done) {
        return s_boot_fail_step == 0u;
    }
    if (s_boot_plan.action == TDMA_BOOT_ACTION_NONE) {
        s_boot_guide_done = true;
        return true;
    }
    /* Ring/profile services only accept work once their first core0 cycles have
     * run; start after a short warm-up and retry for a bounded window instead
     * of latching the first (too-early) failure. */
    const uint64_t now_ms = to_ms_since_boot(get_absolute_time());
    if (!s_boot_attempted) {
        if (now_ms < 3000u) {
            return false;
        }
        s_boot_attempted = true;
        s_boot_deadline_ms = now_ms + 30000u;
    }
    if (now_ms > s_boot_deadline_ms) {
        s_boot_guide_done = true;
        return false;
    }
    for (size_t i = s_boot_next_step; i < s_boot_step_count; i++) {
        if (!app_guide_run_step(s_boot_steps[i])) {
            s_boot_fail_step = (uint32_t)(i + 1u);
            if (s_boot_steps[i] == TDMA_BOOT_STEP_RING_ARM) {
                /* A failed adapter start leaves the ring half-armed; replay
                 * from STOP instead of hammering ARM on a stuck state. */
                s_boot_next_step = 1u;
            }
            return false;
        }
        s_boot_next_step = i + 1u;
    }
    s_boot_guide_done = true;
    s_boot_fail_step = 0u;
    /* Completeness watch: a four-node ring run by fewer boards still reaches
     * up_running on the survivors; the down direction only runs when a peer
     * answers, so a silent down path after the grace window is a half ring. */
    s_boot_ring_watch_active = true;
    s_boot_ring_watch_at_ms = to_ms_since_boot(get_absolute_time()) + 20000u;
    return true;
}

static void app_guide_ring_watch_service(void)
{
    if (!s_boot_ring_watch_active) {
        return;
    }
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring)) {
        return;
    }
    /* Peers boot and join at different times: poll the down direction for the
     * whole window instead of sampling once. */
    if (ring.down_running != 0u) {
        s_boot_ring_watch_active = false;
        return;
    }
    if (to_ms_since_boot(get_absolute_time()) < s_boot_ring_watch_at_ms) {
        return;
    }
    s_boot_ring_watch_active = false;
    /* Reject the half ring: stop TX so a partial ring cannot masquerade as a
     * complete one. */
    s_boot_half_ring = true;
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    if (owner != NULL) {
        (void)tdma_service_ring_stop(owner);
    }
}

bool app_guide_boot_profile_get(tdma_boot_profile_t *out)
{
    if (out == NULL || !s_boot_profile_loaded) {
        return false;
    }
    *out = s_boot_profile;
    return true;
}

uint32_t app_guide_boot_fail_step(void)
{
    return s_boot_fail_step;
}

bool app_guide_boot_listen_pending(void)
{
    return s_boot_listen_pending;
}

bool app_guide_boot_join_timeout(void)
{
    return s_boot_join_timeout;
}

uint32_t app_guide_boot_topology_fail(void)
{
    return s_boot_topology_fail;
}

uint32_t app_guide_boot_apply_fail(void)
{
    return s_boot_apply_fail;
}

bool app_guide_boot_join_frame_seen(void)
{
    return s_boot_join_frame_seen;
}

bool app_guide_boot_half_ring(void)
{
    return s_boot_half_ring;
}

bool app_guide_topology_accepted(uint32_t *reason)
{
    if (reason != NULL) {
        *reason = 0u;
    }
    if (!s_boot_guide_done || s_boot_fail_step != 0u ||
        !s_boot_profile_loaded) {
        if (reason != NULL) {
            *reason = 6u;
        }
        return false;
    }
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring)) {
        if (reason != NULL) {
            *reason = 1u;
        }
        return false;
    }
    if (s_boot_half_ring) {
        if (reason != NULL) {
            *reason = 2u;
        }
        return false;
    }
    if (ring.enabled == 0u || ring.node_count != s_boot_profile.node_count) {
        if (reason != NULL) {
            *reason = 3u;
        }
        return false;
    }
    /* Every member of a correct ring hears exactly one predecessor. A board
     * without its down direction is either the missing slot itself or the
     * neighbour of a missing slot; the completeness watch already turns that
     * into half_ring, so this is the second, independent check. */
    if (ring.down_running == 0u) {
        if (reason != NULL) {
            *reason = 4u;
        }
        return false;
    }
    /* A follower that joins later than its 5 s listen window still forms the
     * ring (measured 2026-11-05: join_timeout=1 together with down_running=1
     * on a healthy four-board ring), so the join flag is evidence only and must
     * not veto acceptance. The down direction is the real liveness proof. */
    return true;
}

static void app_guide_listen_service(void)
{
    if (!s_boot_listen_pending) {
        return;
    }
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring)) {
        return;
    }
    if (ring.adapter_rx_count != s_boot_listen_rx_base) {
        s_boot_join_frame_seen = true;
        if (s_boot_ring_up_ms == 0u) {
            s_boot_ring_up_ms = to_ms_since_boot(get_absolute_time());
        }
        s_boot_listen_pending = false;
        return;
    }
    if (to_ms_since_boot(get_absolute_time()) > s_boot_listen_deadline_ms) {
        /* Ring is started; no frame was decoded inside the window. Keep the
         * timeout purely as evidence, never as a reason to fake a join. */
        s_boot_join_timeout = true;
        s_boot_listen_pending = false;
    }
}

#if defined(PROJECT_TDMA_GUIDE_SCAN) && PROJECT_TDMA_GUIDE_SCAN
static uint64_t app_guide_scan_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static bool app_guide_scan_enabled(void)
{
#if defined(PROJECT_TDMA_GUIDE_SCAN) && PROJECT_TDMA_GUIDE_SCAN
    return true;
#else
    return false;
#endif
}

static void app_guide_scan_fail(uint32_t phase)
{
    s_guide_scan.failures++;
    s_guide_scan.fail_phase = phase;
}

static bool app_guide_scan_set_topology(uint32_t slot, uint32_t nodes)
{
    for (uint32_t attempt = 0u; attempt < 25u; attempt++) {
        /* Always drive the setter: a snapshot that merely matches the target
         * tuple does not prove the staged config was re-applied after the
         * previous pair's STOP, and the first pair is the only one that ever
         * produced a receiver burst when this short-circuited. */
        if (distributed_refmem_set_tdma_ring_topology(slot, 0u, nodes)) {
            /* The boot step pairs the refmem topology with the adapter
             * bring-up topology: the up/down groups and the flight map come
             * from the latter, and without it the ring service cannot arm. */
            return tdma_runtime_owner_set_bringup_topology(nodes, 0u);
        }
        sleep_ms(20u);
    }
    return false;
}

static bool app_guide_scan_ring_armed(void)
{
    tdma_ring_runtime_snapshot_t ring;
    return tdma_runtime_owner_get_ring_snapshot(&ring) &&
           ring.node_count == 2u && ring.enabled != 0u &&
           ring.adapter_started != 0u &&
           ring.config_seq == ring.applied_config_seq;
}

static void app_guide_scan_capture_baseline(void)
{
    s_guide_scan.rx_base = 0u;
    s_guide_scan.words_base = 0u;
    s_guide_scan.edges_base = 0u;
    s_guide_scan.tx_base = 0u;
    s_guide_scan.service_base = 0u;
    tdma_ring_runtime_snapshot_t ring;
    if (tdma_runtime_owner_get_ring_snapshot(&ring)) {
        s_guide_scan.rx_base = ring.adapter_rx_count;
        s_guide_scan.tx_base = ring.adapter_tx_count;
        s_guide_scan.service_base = ring.adapter_service_count;
    }
    tdma_pio_spi_phys_snapshot_t phys;
    if (tdma_runtime_owner_get_phys_snapshot(&phys)) {
        s_guide_scan.words_base = phys.rx_dma_produced_words;
        s_guide_scan.edges_base = phys.rx_edge_count;
    }
}

static bool app_guide_scan_activity_seen(void)
{
    tdma_ring_runtime_snapshot_t ring;
    tdma_pio_spi_phys_snapshot_t phys;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring) ||
        !tdma_runtime_owner_get_phys_snapshot(&phys)) {
        return false;
    }
    /* The host P0T tool accepts a pair on >=10 decoded frames or >=8 DMA
     * words. Using "any counter moved" instead turns one stray frame or a
     * neighbouring slot's tail into a false adjacency. */
    s_guide_scan.frames_delta = ring.adapter_rx_count - s_guide_scan.rx_base;
    s_guide_scan.words_delta =
        phys.rx_dma_produced_words - s_guide_scan.words_base;
    s_guide_scan.tx_delta = ring.adapter_tx_count - s_guide_scan.tx_base;
    s_guide_scan.sample_started = ring.adapter_started;
    return s_guide_scan.frames_delta >= 10u ||
           s_guide_scan.words_delta >= 8u;
}

static void app_guide_scan_leave_window(uint64_t now_ms)
{
    /* Close the dark window the same way the host tool does: stop first (the
     * probe setter only accepts a fully stopped ring), clear probe mode, then
     * re-run the frozen boot steps so the full ring comes back. */
    app_guide_window_close(now_ms);
    s_guide_scan.state = APP_GUIDE_SCAN_STATE_RESTORE;
}

static void app_guide_scan_trial_step(uint64_t now_ms)
{
    const uint64_t slot_start = s_guide_scan.trial_start_ms;
    while (s_guide_scan.phase <= APP_GUIDE_PHASE_LAST &&
           now_ms >= slot_start + k_guide_phase_ms[s_guide_scan.phase]) {
        switch (s_guide_scan.phase) {
        case APP_GUIDE_PHASE_STOP:
            (void)distributed_refmem_tdma_ring_stop();
            s_guide_scan.arm_attempts = 0u;
            s_guide_scan.arm_result = 0u;
            s_guide_scan.arm_next_try_ms = 0u;
            s_guide_scan.trials_started++;
            if (now_ms > slot_start) {
                const uint64_t late = now_ms - slot_start;
                if (late > s_guide_scan.max_late_ms) {
                    s_guide_scan.max_late_ms = (uint32_t)late;
                }
            }
            s_guide_scan.role = (uint32_t)tdma_guide_scan_role(
                &s_guide_scan.table, s_guide_scan.index,
                s_boot_profile.local_slot);
            s_guide_scan.phase = APP_GUIDE_PHASE_TOPOLOGY;
            break;
        case APP_GUIDE_PHASE_TOPOLOGY:
            if (s_guide_scan.role == TDMA_GUIDE_SCAN_ROLE_IDLE) {
                s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
                break;
            }
            {
                uint32_t pair_slot = 0u;
                if (!tdma_guide_scan_pair_slot(
                        (tdma_guide_scan_role_t)s_guide_scan.role,
                        &pair_slot) ||
                    !app_guide_scan_set_topology(pair_slot, 2u)) {
                    app_guide_scan_fail(APP_GUIDE_PHASE_TOPOLOGY);
                    s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
                    break;
                }
            }
            s_guide_scan.phase = APP_GUIDE_PHASE_SETTLE;
            break;
        case APP_GUIDE_PHASE_SETTLE: {
            /* The topology write retires the stop and stages a fresh disable;
             * ARM rejects until the runtime has applied that config. Poll
             * instead of assuming a fixed delay. */
            tdma_ring_runtime_snapshot_t ring;
            if (tdma_runtime_owner_get_ring_snapshot(&ring) &&
                ring.applied_config_seq == ring.config_seq &&
                ring.enabled == 0u && ring.adapter_started == 0u) {
                s_guide_scan.phase = APP_GUIDE_PHASE_PROBE;
                break;
            }
            if (now_ms >= slot_start + APP_GUIDE_SCAN_SETTLE_DEADLINE_MS) {
                app_guide_scan_fail(APP_GUIDE_PHASE_SETTLE);
                s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
            }
            return; /* keep polling */
        }
        case APP_GUIDE_PHASE_PROBE:
            /* The probe persona is only installed when the ring config does
             * NOT carry BRINGUP_NO_CAL (tdma_pio_spi_ring_adapter_tx.c:351),
             * and each topology write re-applies the foundation profile that
             * carries it. Clear it here, after the settle barrier (the setter
             * refuses while config_seq != applied), before arming. */
            if (!tdma_runtime_owner_set_bringup_no_cal(false) ||
                !calibration_manager_set_topology_probe_mode(
                    true, APP_GUIDE_SCAN_PROBE_PHASE_CYCLES)) {
                app_guide_scan_fail(APP_GUIDE_PHASE_PROBE);
                s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
                break;
            }
            s_guide_scan.phase = APP_GUIDE_PHASE_ARM;
            break;
        case APP_GUIDE_PHASE_ARM: {
            /* The host tool never arms blind: each attempt first proves the
             * ring is quiescent (stopped, applied) and re-stages the disable
             * when it is not. A result-8 refusal is a settled-state hint, not a
             * reason to hammer ARM. */
            if (now_ms < s_guide_scan.arm_next_try_ms) {
                return;
            }
            s_guide_scan.arm_next_try_ms = now_ms + 150u;
            tdma_ring_runtime_snapshot_t ring;
            const bool have_ring = tdma_runtime_owner_get_ring_snapshot(&ring);
            s_guide_scan.arm_gap =
                (have_ring && ring.applied_config_seq == ring.config_seq) ? 0u : 1u;
            tdma_service_ring_runtime_config_t staged;
            s_guide_scan.staged_enabled =
                (tdma_runtime_owner_get_staged_ring_config(&staged) &&
                 staged.enabled != 0u) ? 1u : 0u;
            if (!have_ring || ring.enabled != 0u ||
                ring.adapter_started != 0u || s_guide_scan.arm_gap != 0u) {
                (void)distributed_refmem_tdma_ring_stop();
                if (now_ms + 250u >=
                    slot_start + APP_GUIDE_SCAN_ARM_DEADLINE_MS) {
                    app_guide_scan_fail(APP_GUIDE_PHASE_ARM);
                    s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
                }
                return;
            }
            s_guide_scan.arm_attempts++;
            (void)distributed_refmem_tdma_ring_arm();
            s_guide_scan.arm_result =
                (uint32_t)distributed_refmem_tdma_ring_arm_last_result();
            if (s_guide_scan.arm_result !=
                (uint32_t)DISTRIBUTED_REFMEM_TDMA_ARM_OK) {
                (void)distributed_refmem_tdma_ring_stop();
                if (now_ms + 250u >=
                    slot_start + APP_GUIDE_SCAN_ARM_DEADLINE_MS) {
                    app_guide_scan_fail(APP_GUIDE_PHASE_ARM);
                    s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
                }
                return;
            }
            s_guide_scan.arm_deadline_ms =
                slot_start + APP_GUIDE_SCAN_ARM_DEADLINE_MS;
            s_guide_scan.trials_armed++;
            s_guide_scan.phase = APP_GUIDE_PHASE_ARM_WAIT;
            break;
        }
        case APP_GUIDE_PHASE_ARM_WAIT:
            if (app_guide_scan_ring_armed()) {
                s_guide_scan.phase = APP_GUIDE_PHASE_TRAIN;
                break;
            }
            if (now_ms >= s_guide_scan.arm_deadline_ms) {
                app_guide_scan_fail(APP_GUIDE_PHASE_ARM_WAIT);
                s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
            }
            return; /* poll without advancing the phase clock */
        case APP_GUIDE_PHASE_TRAIN:
            /* Deliberately no RING:TRAIN here. Training swaps the PIO persona
             * to CLOCK_COARSE, unloads FLIGHT_ORIGIN and only restores it via a
             * forced POST-TRAIN stop + re-ARM; issuing START right after leaves
             * a half-swapped PIO that emits beacons instead of the probe burst.
             * The host reference run skips training under --adjacency-only
             * (calibration_ring_topology.py:309-313), which is the case this
             * scan mirrors. */
            s_guide_scan.phase = APP_GUIDE_PHASE_START;
            break;
        case APP_GUIDE_PHASE_START:
            /* Mirror the host order: the receiver must be started before the
             * driver, otherwise the probe burst is emitted into a receiver
             * that is not yet decoding (ring_topology.py arms/starts slot 1
             * first for exactly this reason). */
            if (s_guide_scan.role == TDMA_GUIDE_SCAN_ROLE_DRIVER &&
                now_ms < slot_start + APP_GUIDE_SCAN_DRIVER_START_MS) {
                return;
            }
            app_guide_scan_capture_baseline();
            /* START is required even on the receiver: ARM alone opens the
             * hardware capture but produces no RX DMA counts at all (measured
             * 2026-11-05: receiver without START = 0 words on every pair). */
            if (!distributed_refmem_tdma_ring_start()) {
                app_guide_scan_fail(APP_GUIDE_PHASE_START);
                s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
                break;
            }
            s_guide_scan.sample_deadline_ms =
                slot_start + APP_GUIDE_SCAN_SAMPLE_DEADLINE_MS;
            s_guide_scan.phase = APP_GUIDE_PHASE_SAMPLE;
            break;
        case APP_GUIDE_PHASE_SAMPLE: {
            /* Sample every active role: the driver's TX delta is the only
             * evidence that the probe burst was actually emitted, and a
             * receiver-only sample cannot tell "driver silent" from "receiver
             * deaf". */
            const bool active =
                s_guide_scan.role != TDMA_GUIDE_SCAN_ROLE_IDLE;
            const bool detected = active && app_guide_scan_activity_seen();
            if (active && s_guide_scan.index < TDMA_GUIDE_SCAN_PAIR_MAX) {
                tdma_ring_runtime_snapshot_t sample;
                const bool have_sample =
                    tdma_runtime_owner_get_ring_snapshot(&sample);
                s_guide_scan.pair_role[s_guide_scan.index] =
                    (uint8_t)s_guide_scan.role;
                s_guide_scan.pair_frames[s_guide_scan.index] =
                    s_guide_scan.frames_delta;
                s_guide_scan.pair_words[s_guide_scan.index] =
                    s_guide_scan.words_delta;
                s_guide_scan.pair_tx[s_guide_scan.index] =
                    s_guide_scan.tx_delta;
                s_guide_scan.pair_state[s_guide_scan.index] = have_sample
                    ? ((sample.node_count & 0xffu) |
                       ((sample.local_slot_id & 0xffu) << 8u) |
                       ((sample.reference_slot_id & 0xffu) << 16u) |
                       ((sample.enabled != 0u ? 1u : 0u) << 24u) |
                       ((sample.adapter_started != 0u ? 1u : 0u) << 25u) |
                       ((sample.applied_config_seq == sample.config_seq
                             ? 0u : 1u) << 26u))
                    : 0xffffffffu;
                s_guide_scan.pair_start_offset[s_guide_scan.index] =
                    (uint32_t)(slot_start - s_guide_scan.epoch_ms);
                s_guide_scan.pair_service[s_guide_scan.index] = have_sample
                    ? (sample.adapter_service_count - s_guide_scan.service_base)
                    : 0xffffffffu;
                s_guide_scan.pair_error[s_guide_scan.index] =
                    have_sample ? sample.adapter_last_error : 0xffffffffu;
            }
            if (detected &&
                s_guide_scan.role == TDMA_GUIDE_SCAN_ROLE_RECEIVER) {
                (void)tdma_guide_scan_record(&s_guide_scan.table,
                                             s_guide_scan.index, true);
                s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
                break;
            }
            if (now_ms < s_guide_scan.sample_deadline_ms) {
                return; /* keep polling the same window */
            }
            s_guide_scan.phase = APP_GUIDE_PHASE_FINISH;
            break;
        }
        case APP_GUIDE_PHASE_FINISH:
            if (now_ms < slot_start + APP_GUIDE_SCAN_SAMPLE_DEADLINE_MS) {
                return;
            }
            (void)distributed_refmem_tdma_ring_stop();
            s_guide_scan.index++;
            s_guide_scan.phase = APP_GUIDE_PHASE_STOP;
            if (s_guide_scan.index >= s_guide_scan.table.pair_count) {
                app_guide_scan_leave_window(now_ms);
                return;
            }
            /* Every phase offset below is relative to the slot start, so the
             * next slot must move the anchor forward; without this the whole
             * phase table collapses into one call with no settle/ARM budget. */
            s_guide_scan.trial_start_ms =
                s_guide_scan.epoch_ms +
                (uint64_t)s_guide_scan.index * APP_GUIDE_SCAN_TRIAL_MS;
            return;
        default:
            return;
        }
    }
}

void app_guide_scan_service(void)
{
    if (!app_guide_scan_enabled()) {
        return;
    }
    const uint64_t now_ms = app_guide_scan_now_ms();
    if (s_guide_scan.last_call_ms != 0u) {
        const uint64_t delta = now_ms - s_guide_scan.last_call_ms;
        if (delta > s_guide_scan.stall_ms) {
            s_guide_scan.stall_ms = (uint32_t)delta;
        }
    }
    s_guide_scan.last_call_ms = now_ms;
    switch (s_guide_scan.state) {
    case APP_GUIDE_SCAN_STATE_OFF:
        if (!s_boot_guide_done || s_boot_fail_step != 0u ||
            !s_boot_profile_loaded ||
            s_boot_plan.action == TDMA_BOOT_ACTION_NONE ||
            s_boot_plan.action == TDMA_BOOT_ACTION_REJECT) {
            return;
        }
        if (!tdma_guide_scan_init(&s_guide_scan.table,
                                  s_boot_profile.node_count)) {
            s_guide_scan.state = APP_GUIDE_SCAN_STATE_ABORTED;
            app_guide_scan_fail(APP_GUIDE_PHASE_STOP);
            return;
        }
        s_guide_scan.state = APP_GUIDE_SCAN_STATE_WAIT_RING;
        s_guide_scan.wait_deadline_ms = now_ms + APP_GUIDE_SCAN_WAIT_MS;
        /* The dark window below is intentional; do not let the completeness
         * watch read it as a half ring and latch half_ring=1. */
        s_boot_ring_watch_active = false;
        return;
    case APP_GUIDE_SCAN_STATE_WAIT_RING: {
        if (now_ms > s_guide_scan.wait_deadline_ms) {
            s_guide_scan.state = APP_GUIDE_SCAN_STATE_ABORTED;
            app_guide_scan_fail(APP_GUIDE_SCAN_FAIL_WAIT_RING);
            return;
        }
        tdma_ring_runtime_snapshot_t ring;
        if (!tdma_runtime_owner_get_ring_snapshot(&ring) ||
            ring.down_running == 0u) {
            return;
        }
        /* Anchor the trial grid on a fixed point after reset: the four boards
         * are power-cycled/rebooted together, so boot time agrees within the
         * reboot spread (~0.1 s), while the first-frame instant differs by a
         * ring cycle or more between the coordinator and a follower. */
        uint64_t epoch = APP_GUIDE_SCAN_EPOCH_MS;
        const uint64_t earliest = now_ms + 1500u;
        if (epoch < earliest) {
            epoch = earliest;
        }
        s_guide_scan.epoch_ms = epoch;
        /* The prelude needs the ring quiescent: stage/apply only accept a
         * stopped ring whose last config was applied. Poll that barrier here
         * instead of assuming the STOP above has already retired. */
        (void)distributed_refmem_tdma_ring_stop();
        s_guide_scan.prelude_deadline_ms = now_ms + 8000u;
        s_guide_scan.state = APP_GUIDE_SCAN_STATE_PRELUDE;
        return;
    }
    case APP_GUIDE_SCAN_STATE_PRELUDE: {
        /* One-time host-equivalent prelude. The P0T probe runs on the
         * calibration operating profile (10 MHz / 1 ms, level 7), not on the
         * 10 kHz guide profile: the host tool's --level default is 7 and that
         * is what produces the multi-thousand-word burst per pair. Stage and
         * apply it once, exactly like the host's apply_profile(); the frozen
         * boot steps re-stage the guide level when the window closes. */
        tdma_ring_runtime_snapshot_t ring;
        if (!tdma_runtime_owner_get_ring_snapshot(&ring) ||
            ring.enabled != 0u || ring.adapter_started != 0u ||
            ring.applied_config_seq != ring.config_seq) {
            if (now_ms > s_guide_scan.prelude_deadline_ms) {
                s_guide_scan.state = APP_GUIDE_SCAN_STATE_ABORTED;
                app_guide_scan_fail(APP_GUIDE_SCAN_FAIL_PRELUDE);
            }
            return;
        }
        if (!tdma_runtime_owner_stage_operating_profile(
                APP_GUIDE_SCAN_PROBE_LEVEL)) {
            s_guide_scan.state = APP_GUIDE_SCAN_STATE_ABORTED;
            app_guide_scan_fail(APP_GUIDE_SCAN_FAIL_PRELUDE_STAGE);
            return;
        }
        if (!tdma_runtime_owner_apply_operating_profile()) {
            s_guide_scan.state = APP_GUIDE_SCAN_STATE_ABORTED;
            app_guide_scan_fail(APP_GUIDE_SCAN_FAIL_PRELUDE_APPLY);
            return;
        }
        /* The probe arm branch is only taken with no calibration stage staged;
         * clear any persisted/bench stage once so the pair probe cannot be
         * rejected by the calibration gate. */
        (void)calibration_manager_clear_training_stage();
        s_guide_scan.index = 0u;
        s_guide_scan.phase = APP_GUIDE_PHASE_STOP;
        s_guide_scan.trial_active = false;
        s_guide_scan.trial_start_ms = s_guide_scan.epoch_ms;
        s_guide_scan.state = APP_GUIDE_SCAN_STATE_TRIAL;
        return;
    }
    case APP_GUIDE_SCAN_STATE_TRIAL: {
        if (s_guide_scan.index >= s_guide_scan.table.pair_count) {
            app_guide_scan_leave_window(now_ms);
            return;
        }
        if (now_ms < s_guide_scan.trial_start_ms) {
            return;
        }
        /* Stay on the shared slot grid: if this board fell behind, drop the
         * missed pairs instead of shifting every later slot. */
        const uint32_t expected =
            (uint32_t)((now_ms - s_guide_scan.epoch_ms) /
                       APP_GUIDE_SCAN_TRIAL_MS);
        if (expected > s_guide_scan.index) {
            s_guide_scan.skipped += expected - s_guide_scan.index;
            s_guide_scan.index = expected;
            if (s_guide_scan.index >= s_guide_scan.table.pair_count) {
                app_guide_scan_leave_window(now_ms);
                return;
            }
            s_guide_scan.trial_start_ms =
                s_guide_scan.epoch_ms +
                (uint64_t)s_guide_scan.index * APP_GUIDE_SCAN_TRIAL_MS;
            s_guide_scan.phase = APP_GUIDE_PHASE_STOP;
        }
        app_guide_scan_trial_step(now_ms);
        return;
    }
    case APP_GUIDE_SCAN_STATE_RESTORE:
        if (!s_boot_guide_done) {
            return;
        }
        if (s_boot_fail_step != 0u) {
            s_guide_scan.state = APP_GUIDE_SCAN_STATE_ABORTED;
            app_guide_scan_fail(APP_GUIDE_SCAN_FAIL_RESTORE);
            return;
        }
        s_guide_scan.state = APP_GUIDE_SCAN_STATE_DONE;
        return;
    case APP_GUIDE_SCAN_STATE_DONE:
    case APP_GUIDE_SCAN_STATE_ABORTED:
    default:
        return;
    }
}

bool app_guide_scan_get_pair(uint32_t index,
                             uint32_t *role,
                             uint32_t *frames_delta,
                             uint32_t *words_delta,
                             uint32_t *tx_delta,
                             uint32_t *packed_state,
                             uint32_t *start_offset_ms,
                             uint32_t *service_delta,
                             uint32_t *last_error)
{
    if (index >= s_guide_scan.table.pair_count ||
        index >= TDMA_GUIDE_SCAN_PAIR_MAX ||
        role == NULL || frames_delta == NULL || words_delta == NULL ||
        tx_delta == NULL || packed_state == NULL ||
        start_offset_ms == NULL || service_delta == NULL ||
        last_error == NULL) {
        return false;
    }
    *role = s_guide_scan.pair_role[index];
    *frames_delta = s_guide_scan.pair_frames[index];
    *words_delta = s_guide_scan.pair_words[index];
    *tx_delta = s_guide_scan.pair_tx[index];
    *packed_state = s_guide_scan.pair_state[index];
    *start_offset_ms = s_guide_scan.pair_start_offset[index];
    *service_delta = s_guide_scan.pair_service[index];
    *last_error = s_guide_scan.pair_error[index];
    return true;
}

void app_guide_scan_get_status(app_guide_scan_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->state = s_guide_scan.state;
    out->pair_index = s_guide_scan.index;
    out->pair_count = s_guide_scan.table.pair_count;
    out->role = s_guide_scan.role;
    out->node_count = s_guide_scan.table.node_count;
    out->local_slot = s_boot_profile.local_slot;
    out->observed_low = (uint32_t)(s_guide_scan.table.activity_mask & 0xffffffffu);
    out->observed_high = (uint32_t)(s_guide_scan.table.activity_mask >> 32u);
    out->observed_count =
        tdma_guide_scan_observed_count(&s_guide_scan.table);
    out->skipped = s_guide_scan.skipped;
    out->failures = s_guide_scan.failures;
    out->fail_phase = s_guide_scan.fail_phase;
    out->arm_result = s_guide_scan.arm_result;
    out->arm_attempts = s_guide_scan.arm_attempts;
    out->arm_gap = s_guide_scan.arm_gap;
    out->staged_enabled = s_guide_scan.staged_enabled;
    out->trials_started = s_guide_scan.trials_started;
    out->trials_armed = s_guide_scan.trials_armed;
    out->max_late_ms = s_guide_scan.max_late_ms;
    out->stall_ms = s_guide_scan.stall_ms;
    out->frames_delta = s_guide_scan.frames_delta;
    out->words_delta = s_guide_scan.words_delta;
    out->tx_delta = s_guide_scan.tx_delta;
    out->sample_started = s_guide_scan.sample_started;
}
#else /* !PROJECT_TDMA_GUIDE_SCAN */
void app_guide_scan_service(void)
{
}

void app_guide_scan_get_status(app_guide_scan_status_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}

bool app_guide_scan_get_pair(uint32_t index,
                             uint32_t *role,
                             uint32_t *frames_delta,
                             uint32_t *words_delta,
                             uint32_t *tx_delta,
                             uint32_t *packed_state,
                             uint32_t *start_offset_ms,
                             uint32_t *service_delta,
                             uint32_t *last_error)
{
    (void)index;
    (void)role;
    (void)frames_delta;
    (void)words_delta;
    (void)tx_delta;
    (void)packed_state;
    (void)start_offset_ms;
    (void)service_delta;
    (void)last_error;
    return false;
}
#endif /* PROJECT_TDMA_GUIDE_SCAN */

/* --------------------------------------------------------------- CTRQS -----
 * One guide action per round.  C/T arrive as REFMEM commands
 * (app_refmem_domain_config.c).  R runs inside the same maintenance window the
 * P0T scan uses and writes every trial's verdict into the local REFMEM
 * parameter pool (plain storage, no link); the window then closes so Q can read
 * the record back after the ring is re-formed.  Only P1 (CLK coarse bracket) is
 * wired here: the other six actions fail closed with CONFIG_VALIDATION_FAILED
 * until their trial body is implemented, instead of pretending to run.
 *
 * Plan mapping for P1 (the 64-byte plan is the C segment payload):
 *   candidates[0]      reference node slot for this round
 *   candidates[1..n]   TRAIN pulse counts (the candidate sweep)
 *   trial_count        number of slots; slot_ms = window_ms / trial_count
 */
#define APP_CTRQS_STOP_MS 8000u
#define APP_CTRQS_TOPO_MS 5000u
#define APP_CTRQS_ARM_MS 8000u
#define APP_CTRQS_FANOUT_MS 1000u
#define APP_CTRQS_FORWARD_MS 4000u
#define APP_CTRQS_T0_OFFSET_MS 6000u
#define APP_CTRQS_RESTORE_MS 45000u
/* The rebuild has to wait for the slowest board of the round.  At a low-rate
 * profile the four windows start up to one command delivery apart, so a board
 * that reaches REBUILD first would otherwise fail closed while the others are
 * still holding their window (measured: the origin rebuilt alone and ended
 * NOT_ONLINE_SAFE on every round where the followers were still in FORWARD). */
#define APP_CTRQS_REBUILD_MS 60000u
#define APP_CTRQS_REF_ARM_DELAY_MS 600u
#define APP_CTRQS_MAX_TRIALS 32u
#define APP_CTRQS_ACTION_P1 1u
#define APP_CTRQS_ACTION_P2 2u
#define APP_CTRQS_P2_STEP_MS 5000u
#define APP_CTRQS_P2_CODED_MS 9000u
#define APP_CTRQS_P2_STOP_MS 6000u
#define APP_CTRQS_P2_REF_DELAY_MS 1500u
#define APP_CTRQS_ACTION_P3 3u
#define APP_CTRQS_P3_STEP_MS 6000u
#define APP_CTRQS_P3_STOP_MS 5000u
#define APP_CTRQS_P3_REF_DELAY_MS 300u
#define APP_CTRQS_P3_MAX_REPEATS 4u
#define APP_CTRQS_ACTION_T0 4u
#define APP_CTRQS_T0_STEP_MS 6000u
#define APP_CTRQS_T0_STOP_MS 4000u
#define APP_CTRQS_T0_INJECT_DELAY_MS 200u
#define APP_CTRQS_ACTION_T1 5u
#define APP_CTRQS_T1_STEP_MS 6000u
#define APP_CTRQS_T1_STOP_MS 4000u
#define APP_CTRQS_T1_INJECT_DELAY_MS 200u
#define APP_CTRQS_ACTION_T2 6u
#define APP_CTRQS_T2_STEP_MS 6000u
#define APP_CTRQS_T2_STOP_MS 4000u
#define APP_CTRQS_T2_INJECT_DELAY_MS 200u
#define APP_CTRQS_ACTION_T3 7u
#define APP_CTRQS_T3_ARM_MS 20000u

typedef enum {
    APP_CTRQS_IDLE = 0,
    APP_CTRQS_FANOUT,
    APP_CTRQS_STOP,
    APP_CTRQS_TOPO,
    APP_CTRQS_ARM,
    APP_CTRQS_FORWARD,
    APP_CTRQS_TRIAL,
    APP_CTRQS_P2_PREP,
    APP_CTRQS_P2_ARM,
    APP_CTRQS_P2_STOP,
    APP_CTRQS_P2_WAIT,
    APP_CTRQS_P2_CODED_STOP,
    APP_CTRQS_P3_PREP,
    APP_CTRQS_P3_RUN,
    APP_CTRQS_P3_STOP,
    APP_CTRQS_T0_PREP,
    APP_CTRQS_T0_RUN,
    APP_CTRQS_T0_STOP,
    APP_CTRQS_T1_PREP,
    APP_CTRQS_T1_RUN,
    APP_CTRQS_T1_STOP,
    APP_CTRQS_T2_PREP,
    APP_CTRQS_T2_RUN,
    APP_CTRQS_T2_STOP,
    APP_CTRQS_T3_PREP,
    APP_CTRQS_T3_ARM,
    APP_CTRQS_RESTORE_WAIT,
    APP_CTRQS_REBUILD,
    APP_CTRQS_Q,
    APP_CTRQS_DONE,
} app_ctrqs_state_t;

typedef struct {
    app_ctrqs_state_t state;
    guide_action_plan_t plan;
    uint32_t epoch;
    uint32_t local_slot;
    uint32_t ref_slot;
    uint32_t trials;
    uint32_t slot_ms;
    uint32_t trial;
    uint32_t cycle_count;
    uint32_t cycles[GUIDE_ACTION_PLAN_MAX_CANDIDATES];
    uint64_t t0_ms;
    uint64_t t_end_ms;
    uint64_t deadline_ms;
    uint32_t arm_try_ms;
    uint32_t train_base_seq;
    bool train_pending;
    bool forward_armed;
    /* The stop phase was announced at a future ring sequence and is waiting
     * for it (see the FANOUT transition). */
    bool stop_announced;
    /* When this board first saw the origin's TRIAL phase: a follower's FORWARD
     * dwell is measured from here (see the FORWARD transition). */
    uint32_t phase_seen_ms;
    /* Arm/start refusals retried inside this round (see APP_CTRQS_ARM_RETRIES). */
    uint32_t arm_tries;
    /* Pre-window ring identity, captured before the first STOP so the close
     * path can rebuild the same ring even when the image has no boot plan. */
    uint32_t restore_node_count;
    uint32_t restore_local_slot;
    uint32_t restore_reference_slot;
    bool rebuild_topo_done;
    bool rebuild_armed;
    /* P2 (coded marker) request: the same five words the SCPI handler passes
     * to calibration_manager_request_clk_coded(). */
    uint32_t p2_prep_step;
    uint32_t p2_codebook;
    uint32_t p2_min_lag;
    uint32_t p2_max_lag;
    uint32_t p2_max_distance;
    uint32_t p2_min_margin;
    uint32_t p2_probe_phase;
    uint32_t p2_level;
    uint32_t p2_ref_start_ms;
    bool p2_started;
    bool p2_fwd_seen;
    /* P3 (per-link bidirectional ranging): links are derived from the ring
     * topology, so the C segment only carries the first link and the repeated
     * per-link parameters. */
    uint32_t p3_start_link;
    uint32_t p3_signal_group;
    uint32_t p3_pulse_count;
    uint32_t p3_capture_words;
    uint32_t p3_repeats;
    uint32_t p3_baud_hz;
    uint32_t p3_tolerance_per_mille;
    uint32_t p3_trial;
    uint64_t p3_trial_t0_ms;
    bool p3_started;
    bool p3_trial_passed;
    /* T0 (marker/residence): identity comes from the plan generation, the five
     * candidates carry origin/codebook/link delay/offset/repeats. */
    uint32_t t0_origin_node;
    uint32_t t0_codebook;
    uint32_t t0_link_base_delay_ns;
    uint32_t t0_offset_samples;
    uint32_t t0_repeats;
    uint32_t t0_trial;
    uint64_t t0_trial_t0_ms;
    bool t0_prepared;
    bool t0_injected;
    bool t0_trial_passed;
    /* T1 (per-link SCK offset training): the identity fields are the shared
     * plan generation, the candidates carry the link and search window. */
    uint32_t t1_link_index;
    uint32_t t1_link_delay_ns;
    int32_t t1_search_start;
    int32_t t1_search_end;
    uint32_t t1_launch_guard;
    uint32_t t1_max_distance;
    uint32_t t1_min_margin;
    uint32_t t1_trial;
    uint64_t t1_trial_t0_ms;
    bool t1_prepared;
    bool t1_injected;
    bool t1_trial_passed;
    /* T2 (per-link DATA repeat training). */
    uint32_t t2_link_index;
    uint32_t t2_link_delay_ns;
    int32_t t2_search_start;
    int32_t t2_search_end;
    uint32_t t2_marker_to_data;
    uint32_t t2_max_distance;
    uint32_t t2_min_margin;
    uint32_t t2_trial;
    uint64_t t2_trial_t0_ms;
    bool t2_prepared;
    bool t2_injected;
    bool t2_trial_passed;
    /* T3 (final staging + 10 MHz start): only this action may START 10 MHz. */
    bool t3_topo;
    bool t3_armed;
    /* A successful START is latched so the 150 ms retry loop cannot tear down a
     * ring that is still forming when the guard windows return a transient
     * refusal. */
    bool t3_started;
    uint32_t hard_fail_reason;
    uint64_t stop_try_ms;
    guide_action_progress_t progress;
} app_ctrqs_t;

static app_ctrqs_t s_ctrqs;
/* Seven-action ladder state (RAM only): 0 = P1 is the expected next action.
 * It advances only when the expected action's Q reconciles COMPLETE, so a
 * failed action keeps the ladder where it was and a repeated action never
 * advances it. */
static guide_action_ladder_t s_ctrqs_ladder;

static uint64_t app_ctrqs_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static void app_ctrqs_publish(void)
{
    (void)app_cal_guide_publish_progress(&s_ctrqs.progress, s_ctrqs.epoch);
}

/* The bench diagnostic ring mirrors the P3 TDMA step (diagnostic_continue):
 * set_tdma_ring_topology() rebuilds the staged config and clears the flag, so
 * re-assert it after every staging.  Product sessions never set it. */
static void app_ctrqs_ring_diagnostic_after_topo(void)
{
    (void)tdma_runtime_owner_set_ring_diagnostic_mode(true);
}

/* The low-rate guide carrier (10 kHz / 100 kHz) has no calibrated phase table;
 * it starts through the existing bring-up no-cal path.  The flag lives on the
 * staged config and can only be set after the topology/config retire has
 * completed, so callers invoke this at the quiescent barrier just before ARM.
 * The final T3 product start at 10 MHz must never use it. */
static void app_ctrqs_low_rate_bringup(void)
{
    tdma_service_ring_runtime_config_t staged;
    if (s_ctrqs.plan.action_id != APP_CTRQS_ACTION_T3 &&
        tdma_runtime_owner_get_staged_ring_config(&staged) &&
        staged.baud_hz != 0u && staged.baud_hz < 1000000u) {
        (void)tdma_runtime_owner_set_bringup_no_cal(true);
    }
}

/* Wall-clock budgets must follow the carrier rate: one ring cycle is 1 ms at
 * 10 MHz but 150 ms at 10 kHz, so a 10 MHz-sized constant (t0 offset, forward
 * window, trial slot) is 150x too small at the low-rate profile. */
static uint32_t app_ctrqs_staged_period_ms(void)
{
    tdma_service_ring_runtime_config_t staged;
    if (!tdma_runtime_owner_get_staged_ring_config(&staged) ||
        staged.cycle_period_ns == 0u) {
        return 1u;
    }
    const uint32_t period_ms =
        (uint32_t)(staged.cycle_period_ns / UINT64_C(1000000));
    return period_ms != 0u ? period_ms : 1u;
}

/* Marker-family training identity.  The wire field is 8 bits and the manager
 * rejects train_epoch > UINT8_MAX, but the plan generation is 32 bits and grows
 * monotonically; passing it through made T0/T1/T2 fail with
 * CONFIG_VALIDATION_FAILED as soon as the generation left that range.  Every
 * board derives the same bounded epoch from the shared generation, so the
 * identity stays consistent across the ring while the full generation is still
 * what the plan, the evidence and the progress record carry. */
static uint32_t app_ctrqs_marker_epoch(void)
{
    return (uint32_t)(s_ctrqs.plan.generation % 255u) + 1u;
}

static uint32_t app_ctrqs_scaled_ms(uint32_t base_ms, uint32_t cycles){
    const uint64_t scaled =
        (uint64_t)cycles * app_ctrqs_staged_period_ms() + UINT64_C(500);
    return scaled > base_ms ? (uint32_t)scaled : base_ms;
}

static bool app_ctrqs_ring_quiescent(void)
{
    tdma_ring_runtime_snapshot_t ring;
    return tdma_runtime_owner_get_ring_snapshot(&ring) &&
           ring.enabled == 0u && ring.adapter_started == 0u &&
           ring.applied_config_seq == ring.config_seq;
}

/* The coded persona only reprograms SCK; DATA/CS pin directions are inherited
 * from phys->armed, which the ring teardown clears.  The host tool waits for
 * ring_node_count==0 before starting coded, so require the same full teardown
 * instead of just enabled==0/adapter_started==0. */
static bool app_ctrqs_ring_torn_down(void)
{
    tdma_ring_runtime_snapshot_t ring;
    return tdma_runtime_owner_get_ring_snapshot(&ring) &&
           ring.enabled == 0u && ring.adapter_started == 0u &&
           ring.node_count == 0u &&
           ring.applied_config_seq == ring.config_seq;
}

/* tdma_ring_runtime_configure() bumps config_seq on every call, including the
 * NULL STOP (tdma_ring_runtime.c).  Issuing STOP from the 1 ms service tick
 * therefore keeps applied_config_seq chasing config_seq forever and the
 * quiescent barrier never closes.  Throttle the request so the runtime gets a
 * generation-stable pass to retire it. */
static void app_ctrqs_stop_throttled(uint64_t now)
{
    if (s_ctrqs.stop_try_ms != 0u && now < s_ctrqs.stop_try_ms + 150u) {
        return;
    }
    s_ctrqs.stop_try_ms = now;
    (void)distributed_refmem_tdma_ring_stop();
}

static bool app_ctrqs_ring_formed(void)
{
    tdma_ring_runtime_snapshot_t ring;
    return tdma_runtime_owner_get_ring_snapshot(&ring) &&
           ring.enabled != 0u && ring.adapter_started != 0u &&
           ring.node_count == (uint32_t)PROJECT_TDMA_GUIDE_NODES &&
           ring.local_slot_id == s_ctrqs.local_slot &&
           ring.reference_slot_id == s_ctrqs.ref_slot;
}

/* Some bench images run without a frozen boot plan (BOOT:PLAN? first field 0).
 * app_guide_boot_service() then marks the replay done without forming any ring,
 * so the shared window-close primitive cannot restore the ring by itself. */
static bool app_ctrqs_boot_plan_present(void)
{
    tdma_boot_plan_t plan;
    return app_guide_boot_plan_get(&plan) &&
           plan.action != TDMA_BOOT_ACTION_NONE &&
           plan.action != TDMA_BOOT_ACTION_REJECT;
}

static bool app_ctrqs_restore_formed(void)
{
    tdma_ring_runtime_snapshot_t ring;
    return tdma_runtime_owner_get_ring_snapshot(&ring) &&
           ring.enabled != 0u && ring.adapter_started != 0u &&
           ring.up_running != 0u && ring.down_running != 0u &&
           ring.node_count == s_ctrqs.restore_node_count &&
           ring.local_slot_id == s_ctrqs.restore_local_slot &&
           ring.reference_slot_id == s_ctrqs.restore_reference_slot;
}

/* T3 is the only action allowed to run at the measurement profile, so its
 * success must be bound to that profile: a ring formed at the guide carrier
 * would otherwise be indistinguishable from the real T3 result in PLAN?. */
static bool app_ctrqs_t3_profile_formed(void)
{
    tdma_ring_runtime_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring) ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        staged.cycle_period_ns == 0u) {
        return false;
    }
    return ring.cycle_period_ns == staged.cycle_period_ns;
}

/* Abort the running window: keep the reason in the record, stop the ring and
 * let the frozen boot steps rebuild it.  Fail-closed: the action never reaches
 * Q with a masked failure. */
static uint32_t s_ctrqs_abort_site;
/* Sub-step reached inside APP_CTRQS_T3_ARM.  Published through abort_site as
 * 100 + stage when T3 aborts there: that path has several waits and otherwise
 * the only visible outcome is "stuck in T3_ARM". */
static uint32_t s_ctrqs_t3_stage;

static void app_ctrqs_abort(uint32_t reason)
{
    s_ctrqs_abort_site = s_ctrqs.state;    if (s_ctrqs.state == APP_CTRQS_T3_ARM) {
        s_ctrqs_abort_site = 100u + s_ctrqs_t3_stage;
    }
    s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_STOPPED;
    s_ctrqs.progress.reason = reason;
    app_ctrqs_publish();
    app_guide_window_close(app_ctrqs_now_ms());
    s_ctrqs.state = APP_CTRQS_RESTORE_WAIT;
    s_ctrqs.deadline_ms = app_ctrqs_now_ms() + APP_CTRQS_RESTORE_MS;
}

/* Ring-state negotiation: every board advertises the phase it is in, and the
 * executor crosses a handoff only after all boards report the same phase for
 * the same round.  Stopping or forming the ring is a shared transition, so a
 * board that moves first cannot tear the ring out from under the others. */
#define APP_CTRQS_PHASE_STALE_MS 15000u
/* How long a follower keeps its forward window open for the origin's handoff.
 * It must outlast the origin's own window (the origin starts up to one command
 * delivery later, then trains trials x slot) but end early enough that the
 * follower is rebuilding while the origin's rebuild can still succeed: the
 * origin's rebuild deadline is APP_CTRQS_REBUILD_MS, so a longer hold leaves it
 * alone and it fails closed with NOT_ONLINE_SAFE (measured: 90 s hold -> 3/4
 * boards complete, origin restore fails; the hold has to land just after the
 * origin's window). */
#define APP_CTRQS_FOLLOWER_PHASE_WAIT_MS 60000u
/* The arm/start handshake is flaky at a low-rate profile (measured on the
 * bench: the reference's START occasionally returns -200 and succeeds on a
 * retry), so a negotiated sequence must retry it rather than fail the round. */
#define APP_CTRQS_ARM_RETRIES 4u
/* Lead, in ring cycles, between announcing a ring transition and the shared
 * sequence at which every board performs it.  The announcement must be on the
 * wire before the ring stops (the maintenance channel has no carrier
 * afterwards), so the lead covers one frame exchange plus a margin. */
#define APP_CTRQS_PHASE_LEAD_CYCLES 24u

static void app_ctrqs_publish_phase(uint8_t phase)
{
    /* The maintenance plane is origin-broadcast: a follower's submit never
     * reaches the ring and would only fill its own TX queue, so only the
     * reference advertises.  Followers adopt the broadcast instead. */
    if (s_ctrqs.local_slot != s_ctrqs.ref_slot) {
        return;
    }
    (void)tdma_ring_phase_publish(phase, (uint16_t)s_ctrqs.plan.generation);
}

/* Abort-site evidence.  `progress_reason` is intentionally coarse (several
 * sites share TIMEOUT / NOT_ONLINE_SAFE), so a round that fails in under a
 * second could not be attributed from the outside.  The executor state at the
 * moment of the abort distinguishes them exactly (4=ARM, 5=FORWARD, 6=TRIAL,
 * 26=RESTORE_WAIT, 27=REBUILD), and it is published as an appended PLAN? field
 * instead of widening the reason enum. */
static uint32_t s_ctrqs_abort_site;

static void app_ctrqs_abort(uint32_t reason);

uint32_t app_cal_guide_abort_site(void)
{
    return s_ctrqs_abort_site;
}

static void app_ctrqs_finalize(void);

/* A P1 follower's whole contribution is the forwarding persona it holds while
 * the origin trains.  Completing its trials only after the origin's window
 * (TRIAL_DONE) is what keeps that persona alive; the bookkeeping itself is the
 * same check the per-slot follower path already performs. */
static void app_ctrqs_follower_complete_all(void)
{
    const uint32_t trials = s_ctrqs.trials;
    if (trials == 0u) {
        app_ctrqs_finalize();
        return;
    }
    tdma_pio_spi_clk_train_snapshot_t train;
    const bool forwarding =
        tdma_runtime_owner_get_clk_train_snapshot(&train) &&
        train.state == TDMA_PIO_SPI_CLK_TRAIN_FORWARDING;
    const uint32_t mask = (1u << trials) - 1u;
    if (forwarding) {
        s_ctrqs.progress.passed_mask |= mask;
    } else {
        s_ctrqs.progress.failed_mask |= mask;
    }
    s_ctrqs.progress.trials_done = trials;
    s_ctrqs.progress.deepest_trial = trials - 1u;
    s_ctrqs.trial = trials;
    app_ctrqs_finalize();
}

static void app_ctrqs_finalize(void)
{
    if (s_ctrqs.hard_fail_reason != 0u) {
        s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_STOPPED;
        s_ctrqs.progress.reason = s_ctrqs.hard_fail_reason;
    } else if (s_ctrqs.progress.trials_done < s_ctrqs.trials) {
        /* A short slot window (ring formed late / missed slots) is not a
         * completed action: fail closed instead of reporting COMPLETE with a
         * partial trial count. */
        s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_STOPPED;
        s_ctrqs.progress.reason = (uint32_t)REFMEM_COMMAND_REASON_TIMEOUT;
    } else if (s_ctrqs.progress.failed_mask != 0u) {
        /* Any action that ran a failed trial must not report COMPLETE, and the
         * ladder must not advance on it.  P1 was the only action checked here,
         * so P3 could finish with every real trial failed and still advance.
         * Both masks are published through PLAN? so the caller can tell the
         * difference instead of trusting the status word.  When the action
         * recorded a specific hard failure (coded or marker reject reason), that
         * reason is published instead of a generic timeout. */
        s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_STOPPED;
        s_ctrqs.progress.reason =
            s_ctrqs.hard_fail_reason != 0u
                ? s_ctrqs.hard_fail_reason
                : (uint32_t)REFMEM_COMMAND_REASON_TIMEOUT;
    } else {
        s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_COMPLETE;
        s_ctrqs.progress.reason = 0u;
    }
    app_ctrqs_publish();
    app_guide_window_close(app_ctrqs_now_ms());
    s_ctrqs.state = APP_CTRQS_RESTORE_WAIT;
    s_ctrqs.deadline_ms = app_ctrqs_now_ms() + APP_CTRQS_RESTORE_MS;
}

/* S segment: HAOFV flash rule 2.1 #7 forbids a normal write transaction while
 * calibration training is in progress.  By the time Q runs the R window is
 * closed and the ring is re-formed, so the training flag is clear; the write
 * still goes through the canonical manager/store path, and an already
 * committed generation is skipped instead of appending a duplicate record.
 * P1 has no training-store payload, so it records the decision without a
 * write. */
static void app_ctrqs_commit(guide_action_reconcile_t reconcile)
{
    tdma_ring_calibration_stage_t stage;
    calibration_training_store_status_t status;
    bool complete = false;
    const bool action_ok = reconcile == GUIDE_ACTION_RECONCILE_COMPLETE;
    const bool have_stage =
        calibration_manager_get_training_stage(&stage, &complete);
    calibration_manager_get_training_store_status(&status);
    const bool session_closed =
        !calibration_manager_training_offline_active_core1();
    const bool already =
        have_stage && complete && status.persisted_valid != 0u &&
        status.record_generation != 0u &&
        status.calibration_generation == stage.calibration_generation;
    const guide_action_commit_t decision =
        guide_action_commit_decision(session_closed, already);
    const bool applicable = action_ok && have_stage && complete;
    uint32_t ok = 0u;
    /* An aborted/reconciled-failed action must not persist the (unrelated)
     * training stage: the S segment only runs for a completed Q. */
    if (decision == GUIDE_ACTION_COMMIT_ALLOW && applicable) {
        ok = calibration_manager_commit_training_stage() ? 1u : 0u;
        if (ok == 0u) {
            s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_FAILED;
            s_ctrqs.progress.reason =
                (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
            app_ctrqs_publish();
        }
    }
    app_cal_guide_set_commit_result((uint32_t)decision, applicable ? 1u : 0u,
                                    ok);
}

static void app_ctrqs_start(const guide_action_plan_t *plan, uint32_t epoch)
{
    const uint64_t now = app_ctrqs_now_ms();
    const uint32_t nodes = (uint32_t)PROJECT_TDMA_GUIDE_NODES;
    memset(&s_ctrqs, 0, sizeof(s_ctrqs));
    /* Per-round diagnostics: the abort site is a plain static (not part of the
     * struct), so without this reset a previous round's site is read back as if
     * it belonged to the current one. */
    s_ctrqs_abort_site = 0u;
    s_ctrqs_t3_stage = 0u;
    s_ctrqs.plan = *plan;
    s_ctrqs.epoch = epoch;
    s_ctrqs.local_slot = distributed_refmem_local_slot();
    /* Capture the ring we are about to stop so the close path can rebuild the
     * same ring even on images without a frozen boot plan. */
    {
        tdma_ring_runtime_snapshot_t ring;
        if (tdma_runtime_owner_get_ring_snapshot(&ring) &&
            ring.node_count >= 2u) {
            s_ctrqs.restore_node_count = ring.node_count;
            s_ctrqs.restore_local_slot = ring.local_slot_id;
            s_ctrqs.restore_reference_slot = ring.reference_slot_id;
        } else {
            s_ctrqs.restore_node_count = nodes;
            s_ctrqs.restore_local_slot = s_ctrqs.local_slot;
            s_ctrqs.restore_reference_slot = 0u;
        }
    }
    s_ctrqs.progress = (guide_action_progress_t){
        .action_id = plan->action_id,
        .plan_crc32 = plan->plan_crc32,
        .generation = plan->generation,
        .trials_done = 0u,
        .deepest_trial = 0u,
        .reason = 0u,
        .status = (uint32_t)GUIDE_ACTION_PROGRESS_RUNNING,
        .passed_mask = 0u,
        .failed_mask = 0u,
    };
    const uint32_t expected_action = s_ctrqs_ladder.action_index + 1u;
    const bool repeat_last_action =
        s_ctrqs_ladder.action_index != 0u &&
        plan->action_id == s_ctrqs_ladder.action_index;
    s_ctrqs.ref_slot = nodes != 0u ? plan->candidates[0] % nodes : 0u;
    bool plan_valid =
        plan->trial_count != 0u &&
        plan->trial_count <= APP_CTRQS_MAX_TRIALS &&
        plan->window_ms != 0u &&
        plan->candidate_count <= GUIDE_ACTION_PLAN_MAX_CANDIDATES &&
        epoch != 0u && nodes != 0u &&
        (plan->action_id == expected_action || repeat_last_action);
    if (plan->action_id == APP_CTRQS_ACTION_P1) {
        plan_valid = plan_valid && plan->candidate_count >= 2u;
        s_ctrqs.cycle_count = plan->candidate_count - 1u;
        for (uint32_t i = 0u; i < s_ctrqs.cycle_count; i++) {
            s_ctrqs.cycles[i] = plan->candidates[i + 1u];
        }
        s_ctrqs.trials = plan->trial_count;
        s_ctrqs.slot_ms = plan->window_ms / plan->trial_count;
        if (s_ctrqs.slot_ms < 300u) {
            s_ctrqs.slot_ms = 300u;
        }
        /* Empirical bound only: the phys meters one bit per ring cycle, so the
         * slot must scale with the profile. 3x period per cycle (measured: a
         * 16-cycle trial occasionally needs more than the 2x estimate, and the
         * failing trial then aborts at the slot edge).  High-rate profiles keep
         * their window-derived slot because 3x1msx16 stays far below it. */
        tdma_service_ring_runtime_config_t staged_p1;
        if (tdma_runtime_owner_get_staged_ring_config(&staged_p1) &&
            staged_p1.cycle_period_ns != 0u) {
            uint32_t max_cycles = 0u;
            for (uint32_t i = 0u; i < s_ctrqs.cycle_count; i++) {
                if (s_ctrqs.cycles[i] > max_cycles) {
                    max_cycles = s_ctrqs.cycles[i];
                }
            }
            const uint64_t need_ms =
                ((uint64_t)max_cycles * 3ull * staged_p1.cycle_period_ns +
                 UINT64_C(999999)) /
                    UINT64_C(1000000) +
                UINT64_C(500);
            if ((uint64_t)s_ctrqs.slot_ms < need_ms) {
                s_ctrqs.slot_ms = (uint32_t)need_ms;
            }
        }
    } else if (plan->action_id == APP_CTRQS_ACTION_P2) {
        /* P2 candidate layout: ref_slot, codebook, min_lag, max_lag,
         * min_margin; threshold_low = probe phase cycles, threshold_high =
         * max best-distance. */
        s_ctrqs.p2_codebook = plan->candidates[1];
        s_ctrqs.p2_min_lag = plan->candidates[2];
        s_ctrqs.p2_max_lag = plan->candidates[3];
        s_ctrqs.p2_min_margin = plan->candidates[4];
        s_ctrqs.p2_max_distance = plan->threshold_high;
        s_ctrqs.p2_probe_phase = plan->threshold_low;
        s_ctrqs.p2_level = plan->profile_level;
        plan_valid = plan_valid && plan->candidate_count == 5u &&
                     plan->trial_count == 1u &&
                     s_ctrqs.p2_codebook <= 3u &&
                     s_ctrqs.p2_min_lag < s_ctrqs.p2_max_lag &&
                     s_ctrqs.p2_max_lag - s_ctrqs.p2_min_lag + 1u <= 256u &&
                     s_ctrqs.p2_probe_phase != 0u &&
                     s_ctrqs.p2_max_distance != 0u &&
                     s_ctrqs.p2_level != 0u;
        s_ctrqs.trials = 1u;
        s_ctrqs.slot_ms = plan->window_ms;
    } else if (plan->action_id == APP_CTRQS_ACTION_P3) {
        /* P3 candidate layout (v2): start_link, signal_group, pulse_count,
         * capture_words, repeats_per_link; threshold_low = baud_hz and
         * threshold_high = frequency tolerance in per-mille.  Links are
         * derived from the ring topology: link L = source L, destination
         * (L+1)%N, so the C segment never has to carry a link table. */
        s_ctrqs.p3_start_link = plan->candidates[0];
        s_ctrqs.p3_signal_group = plan->candidates[1];
        s_ctrqs.p3_pulse_count = plan->candidates[2];
        s_ctrqs.p3_capture_words = plan->candidates[3];
        s_ctrqs.p3_repeats = plan->candidates[4];
        s_ctrqs.p3_baud_hz = plan->threshold_low;
        s_ctrqs.p3_tolerance_per_mille =
            plan->threshold_high != 0u ? plan->threshold_high : 5000u;
        plan_valid = plan_valid && plan->candidate_count == 5u &&
                     s_ctrqs.p3_signal_group <= 1u &&
                     s_ctrqs.p3_pulse_count >= 1u &&
                     s_ctrqs.p3_pulse_count <= 4096u &&
                     s_ctrqs.p3_capture_words >= 1u &&
                     s_ctrqs.p3_capture_words <= 1024u &&
                     s_ctrqs.p3_repeats >= 1u &&
                     s_ctrqs.p3_repeats <= APP_CTRQS_P3_MAX_REPEATS &&
                     s_ctrqs.p3_baud_hz >= 1000u &&
                     s_ctrqs.p3_baud_hz <= 50000000u &&
                     s_ctrqs.p3_start_link < nodes &&
                     s_ctrqs.p3_tolerance_per_mille <= 50000u;
        s_ctrqs.trials =
            (uint32_t)PROJECT_TDMA_GUIDE_NODES * s_ctrqs.p3_repeats;
        s_ctrqs.slot_ms = plan->window_ms / s_ctrqs.trials;
        if (s_ctrqs.slot_ms < 1000u) {
            s_ctrqs.slot_ms = 1000u;
        }
    } else if (plan->action_id == APP_CTRQS_ACTION_T0) {
        /* T0 candidate layout (v2): origin_node, codebook, link_base_delay_ns,
         * offset_sample_count, repeats.  The marker identity (epoch/sequence/
         * marker_id/generation) is the shared plan generation, and no
         * diagnostic faults are injected. */
        s_ctrqs.t0_origin_node = plan->candidates[0];
        s_ctrqs.t0_codebook = plan->candidates[1];
        s_ctrqs.t0_link_base_delay_ns = plan->candidates[2];
        s_ctrqs.t0_offset_samples = plan->candidates[3];
        s_ctrqs.t0_repeats = plan->candidates[4];
        plan_valid = plan_valid && plan->candidate_count == 5u &&
                     s_ctrqs.t0_origin_node < nodes &&
                     /* Measured on the bench with the host marker tool: codebooks
                      * 1 and 3 correlate (correlation_reject NONE) while 0 and 2
                      * are rejected as MANCHESTER.  The manager only range-checks
                      * 0..M255_MANCHESTER_32, so the action must keep the narrower
                      * set that the phys actually generates. */
                     s_ctrqs.t0_codebook >= 1u &&
                     s_ctrqs.t0_codebook <= 3u &&
                     s_ctrqs.t0_link_base_delay_ns <= 1000000u &&
                     s_ctrqs.t0_offset_samples <=
                         CALIBRATION_TRAINING_MARKER_MAX_OFFSET_SAMPLES &&
                     s_ctrqs.t0_repeats >= 1u &&
                     s_ctrqs.t0_repeats <= APP_CTRQS_P3_MAX_REPEATS;
        s_ctrqs.trials = s_ctrqs.t0_repeats;
        s_ctrqs.slot_ms = plan->window_ms / s_ctrqs.trials;
        if (s_ctrqs.slot_ms < 500u) {
            s_ctrqs.slot_ms = 500u;
        }
    } else if (plan->action_id == APP_CTRQS_ACTION_T1) {
        /* T1 candidate layout (v2): link_index, link_base_delay_ns,
         * search_start_offset, search_end_offset, sck_launch_guard_samples;
         * threshold_low = max_best_distance, threshold_high = min_margin.
         * Identity = plan generation; configured offset starts at 0 and the
         * per-capture guard is fixed at 4 samples (tool default shape). */
        s_ctrqs.t1_link_index = plan->candidates[0];
        s_ctrqs.t1_link_delay_ns = plan->candidates[1];
        s_ctrqs.t1_search_start = (int32_t)plan->candidates[2];
        s_ctrqs.t1_search_end = (int32_t)plan->candidates[3];
        s_ctrqs.t1_launch_guard = plan->candidates[4];
        s_ctrqs.t1_max_distance = plan->threshold_low;
        s_ctrqs.t1_min_margin = plan->threshold_high;
        plan_valid = plan_valid && plan->candidate_count == 5u &&
                     s_ctrqs.t1_link_index < nodes &&
                     s_ctrqs.t1_link_delay_ns >= 1u &&
                     s_ctrqs.t1_link_delay_ns <= 1000000u &&
                     s_ctrqs.t1_search_start < s_ctrqs.t1_search_end &&
                     (uint32_t)(s_ctrqs.t1_search_end -
                                s_ctrqs.t1_search_start) <= 256u &&
                     s_ctrqs.t1_launch_guard <= 64u &&
                     s_ctrqs.t1_max_distance != 0u;
        s_ctrqs.trials = plan->trial_count;
        s_ctrqs.slot_ms = plan->window_ms / s_ctrqs.trials;
        if (s_ctrqs.slot_ms < 500u) {
            s_ctrqs.slot_ms = 500u;
        }
    } else if (plan->action_id == APP_CTRQS_ACTION_T2) {
        /* T2 candidate layout (v2): link_index, link_base_delay_ns,
         * search_start_offset, search_end_offset, marker_to_data_samples;
         * threshold_low = max_best_distance, threshold_high = min_margin. */
        s_ctrqs.t2_link_index = plan->candidates[0];
        s_ctrqs.t2_link_delay_ns = plan->candidates[1];
        s_ctrqs.t2_search_start = (int32_t)plan->candidates[2];
        s_ctrqs.t2_search_end = (int32_t)plan->candidates[3];
        s_ctrqs.t2_marker_to_data = plan->candidates[4];
        s_ctrqs.t2_max_distance = plan->threshold_low;
        s_ctrqs.t2_min_margin = plan->threshold_high;
        plan_valid = plan_valid && plan->candidate_count == 5u &&
                     s_ctrqs.t2_link_index < nodes &&
                     s_ctrqs.t2_link_delay_ns >= 1u &&
                     s_ctrqs.t2_link_delay_ns <= 1000000u &&
                     s_ctrqs.t2_search_start < s_ctrqs.t2_search_end &&
                     (uint32_t)(s_ctrqs.t2_search_end -
                                s_ctrqs.t2_search_start) <= 256u &&
                     s_ctrqs.t2_marker_to_data >= 1u &&
                     s_ctrqs.t2_marker_to_data <= 1000000u &&
                     s_ctrqs.t2_max_distance != 0u;
        s_ctrqs.trials = plan->trial_count;
        s_ctrqs.slot_ms = plan->window_ms / s_ctrqs.trials;
        if (s_ctrqs.slot_ms < 500u) {
            s_ctrqs.slot_ms = 500u;
        }
    } else if (plan->action_id == APP_CTRQS_ACTION_T3) {
        /* T3 is the final action: any candidate table is ignored, the stage
         * already carries the trained phases and only this action may start
         * the ring at 10 MHz (profile_level). */
        plan_valid = plan_valid && plan->profile_level != 0u;
        s_ctrqs.trials = 1u;
        s_ctrqs.slot_ms = plan->window_ms;
    } else {
        plan_valid = false;
    }
    if (!plan_valid) {
        s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_FAILED;
        s_ctrqs.progress.reason =
            (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
        app_ctrqs_publish();
        /* Record the rejection as this round's Q result too: without it the
         * single-port readback would keep showing the previous round's Q. */
        guide_action_reconcile_result_t rejected;
        memset(&rejected, 0, sizeof(rejected));
        rejected.decision = GUIDE_ACTION_RECONCILE_FAILED;
        rejected.reason = (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
        app_cal_guide_set_q_result(&s_ctrqs.progress, true, &rejected);
        app_cal_guide_set_commit_result(0u, 0u, 0u);
        app_cal_guide_set_ladder_result(s_ctrqs_ladder.action_index, 0u);
        s_ctrqs.state = APP_CTRQS_DONE;
        return;
    }
    /* One common schedule for all four boards: the trigger frame reaches them
     * within a ring cycle, so a fixed offset from receipt is a shared epoch. */
    s_ctrqs.t0_ms = now + APP_CTRQS_T0_OFFSET_MS +
                    (uint64_t)80u * app_ctrqs_staged_period_ms();
    s_ctrqs.t_end_ms =
        s_ctrqs.t0_ms + ((uint64_t)s_ctrqs.trials * s_ctrqs.slot_ms);
    /* The schedule above is only an upper bound now: the FORWARD->TRIAL
     * handoff is a negotiated barrier and re-bases t0 when it passes. */
    /* The phase is sticky across rounds: a follower that still holds TRIAL from
     * the previous round would start its FORWARD dwell immediately, finish a
     * few seconds into the origin's window and drop the forwarding persona for
     * the remaining trials (measured as "first trials pass, last ones fail").
     * Every board therefore starts the round at READY. */
    tdma_ring_phase_set_local((uint8_t)TDMA_RING_PHASE_READY,
                              (uint16_t)plan->generation);
    app_ctrqs_publish_phase((uint8_t)TDMA_RING_PHASE_READY);
    s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_RUNNING;
    app_ctrqs_publish();
    /* The dark window below is intentional: do not let the ring completeness
     * watch read it as a half ring. */
    s_boot_ring_watch_active = false;
    /* The T command travels as maintenance fragments.  The issuer consumes its
     * own copy from the local node-control record immediately, so stopping the
     * ring right away would cut the fragments off before the followers see
     * them.  Give the maintenance transport one fanout window first. */
    s_ctrqs.state = APP_CTRQS_FANOUT;
    s_ctrqs.deadline_ms = now + APP_CTRQS_FANOUT_MS;
}

/* A trial that ran but produced no usable return is a failed trial, not a
 * reason to abort the whole round.
 *
 * The reference only accepts RETURN_OVERLAP, and whether a return overlaps is a
 * hardware ordering race (the burst program latches IRQ2 before it raises
 * IRQ1), not something a longer deadline can repair.  Aborting on the first
 * miss made trials_done unreachable at a low-rate profile, while the follower
 * path has always recorded failed_mask and moved on.  The window therefore
 * always completes its trial count and the masks tell the truth; finalize()
 * refuses to report P1 COMPLETE while failed_mask is set. */
static void app_ctrqs_trial_fail_and_advance(void)
{
    const uint32_t bit = 1u << s_ctrqs.trial;
    s_ctrqs.progress.failed_mask |= bit;
    s_ctrqs.progress.trials_done++;
    s_ctrqs.progress.deepest_trial = s_ctrqs.trial;
    s_ctrqs.trial++;
    s_ctrqs.train_pending = false;
    app_ctrqs_publish();
}

static void app_ctrqs_trial(void)
{
    const uint64_t now = app_ctrqs_now_ms();
    /* The first train right after entering TRIAL is the one that fails when the
     * round is not green: the phys has just been re-armed and the followers are
     * still settling their forwarding persona, so the very first burst can miss
     * its return (measured: every non-green round had failed_mask=0x1, i.e.
     * trial 0 only).  Give the first trial one short settle scaled from the
     * ring period before requesting its train. */
    if (s_ctrqs.trial == 0u &&
        now < s_ctrqs.t0_ms +
                  (uint64_t)3u * app_ctrqs_staged_period_ms()) {
        return;
    }
    if (now >= s_ctrqs.t_end_ms) {
        app_ctrqs_finalize();
        return;
    }
    const uint64_t slot_start =
        s_ctrqs.t0_ms + ((uint64_t)s_ctrqs.trial * s_ctrqs.slot_ms);
    if (now < slot_start) {
        return;
    }
    if (s_ctrqs.local_slot != s_ctrqs.ref_slot) {
        /* Followers only have to hold the forwarding persona; that is their
         * measured contribution to this P1 round. */
        tdma_pio_spi_clk_train_snapshot_t train;
        const bool forwarding =
            tdma_runtime_owner_get_clk_train_snapshot(&train) &&
            train.state == TDMA_PIO_SPI_CLK_TRAIN_FORWARDING;
        const uint32_t bit = 1u << s_ctrqs.trial;
        if (forwarding) {
            s_ctrqs.progress.passed_mask |= bit;
        } else {
            s_ctrqs.progress.failed_mask |= bit;
        }
        s_ctrqs.progress.trials_done++;
        s_ctrqs.progress.deepest_trial = s_ctrqs.trial;
        s_ctrqs.trial++;
        app_ctrqs_publish();
        return;
    }
    if (!s_ctrqs.train_pending) {
        tdma_pio_spi_clk_train_snapshot_t train;
        if (!tdma_runtime_owner_get_clk_train_snapshot(&train) ||
            !distributed_refmem_tdma_ring_train(
                s_ctrqs.cycles[s_ctrqs.trial % s_ctrqs.cycle_count])) {
            app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
            return;
        }
        s_ctrqs.train_base_seq = train.request_seq;
        s_ctrqs.train_pending = true;
        return;
    }
    tdma_pio_spi_clk_train_snapshot_t train;
    if (!tdma_runtime_owner_get_clk_train_snapshot(&train)) {
        app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
        return;
    }
    if (train.state == TDMA_PIO_SPI_CLK_TRAIN_ERROR) {
        app_ctrqs_trial_fail_and_advance();
        return;
    }
    if (train.request_seq == s_ctrqs.train_base_seq ||
        train.state != TDMA_PIO_SPI_CLK_TRAIN_MASTER_COMPLETE) {
        if (now >= slot_start + s_ctrqs.slot_ms) {
            app_ctrqs_trial_fail_and_advance();
        }
        return;
    }
    s_ctrqs.train_pending = false;
    const uint32_t bit = 1u << s_ctrqs.trial;
    if (train.result == TDMA_PIO_SPI_CLK_TRAIN_RESULT_RETURN_OVERLAP) {
        s_ctrqs.progress.passed_mask |= bit;
    } else {
        s_ctrqs.progress.failed_mask |= bit;
    }
    s_ctrqs.progress.trials_done++;
    s_ctrqs.progress.deepest_trial = s_ctrqs.trial;
    s_ctrqs.trial++;
    app_ctrqs_publish();
}

/* --- P3 (per-link bidirectional ranging) ----------------------------------
 * The links are derived from the ring topology: link L = source L, destination
 * (L+1)%N, L = 0..N-1.  Each board derives its own role per trial from its
 * local slot, so no board needs a link table from the C segment. */
static bool app_ctrqs_p3_evaluate(const calibration_manager_p3_snapshot_t *snap)
{
    const tdma_pio_spi_p3_snapshot_t *raw = &snap->raw;
    if (raw->state != TDMA_PIO_SPI_P3_COMPLETE || snap->result_valid == 0u ||
        (raw->flags & 0x0Fu) != 0x0Fu ||
        raw->dma_overrun_count != 0u || raw->pio_stall_count != 0u ||
        raw->baud_hz != s_ctrqs.p3_baud_hz) {
        return false;
    }
    const uint64_t period_ns = UINT64_C(1000000000) / s_ctrqs.p3_baud_hz;
    const uint64_t measured_ns =
        (uint64_t)raw->clock_high_ns + (uint64_t)raw->clock_low_ns;
    const uint64_t delta_ns = measured_ns > period_ns
                                  ? measured_ns - period_ns
                                  : period_ns - measured_ns;
    return delta_ns * 1000ull <=
           (uint64_t)s_ctrqs.p3_tolerance_per_mille * period_ns;
}

static void app_ctrqs_p3_advance(bool passed, uint64_t now)
{
    const uint32_t bit = 1u << s_ctrqs.p3_trial;
    if (passed) {
        s_ctrqs.progress.passed_mask |= bit;
    } else {
        s_ctrqs.progress.failed_mask |= bit;
    }
    s_ctrqs.progress.trials_done++;
    s_ctrqs.progress.deepest_trial = s_ctrqs.p3_trial;
    s_ctrqs.p3_trial++;
    app_ctrqs_publish();
    if (s_ctrqs.p3_trial >= s_ctrqs.trials) {
        app_ctrqs_finalize();
        return;
    }
    s_ctrqs.p3_started = false;
    s_ctrqs.p3_trial_passed = false;
    s_ctrqs.p3_trial_t0_ms =
        s_ctrqs.t0_ms + (uint64_t)s_ctrqs.p3_trial * s_ctrqs.slot_ms;
    s_ctrqs.deadline_ms = s_ctrqs.p3_trial_t0_ms + APP_CTRQS_P3_STEP_MS;
    /* Back to the running state: APP_CTRQS_P3_RUN is otherwise only assigned by
     * P3_PREP, so a trial that ended in P3_STOP left the machine latched there
     * and every later service tick stamped the remaining trials with the same
     * verdict (a pure prefix mask). T0/T1/T2 already restore their run state
     * here; P3 was the only one missing it. */
    s_ctrqs.state = APP_CTRQS_P3_RUN;
    (void)now;
}

static void app_ctrqs_p3_step(uint64_t now)
{
    const uint32_t nodes = (uint32_t)PROJECT_TDMA_GUIDE_NODES;
    if (nodes < 2u) {
        app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
        return;
    }
    const uint32_t link =
        (s_ctrqs.p3_start_link + (s_ctrqs.p3_trial / s_ctrqs.p3_repeats)) % nodes;
    const uint32_t source = link;
    const uint32_t destination = (link + 1u) % nodes;
    const bool is_responder = s_ctrqs.local_slot == destination;
    const bool is_initiator = s_ctrqs.local_slot == source;
    if (!is_responder && !is_initiator) {
        /* This board owns neither end of the current link: mark the trial as a
         * no-op pass and move on.  It reports no hardware evidence. */
        app_ctrqs_p3_advance(true, now);
        return;
    }
    if (now >= s_ctrqs.deadline_ms) {
        if (s_ctrqs.p3_started) {
            calibration_manager_stop_p3();
            s_ctrqs.p3_trial_passed = false;
            s_ctrqs.state = APP_CTRQS_P3_STOP;
            s_ctrqs.deadline_ms = now + APP_CTRQS_P3_STOP_MS;
            return;
        }
        app_ctrqs_p3_advance(false, now);
        return;
    }
    if (now < s_ctrqs.p3_trial_t0_ms) {
        return;
    }
    if (!s_ctrqs.p3_started) {
        const uint64_t start_ms = is_responder
                                      ? s_ctrqs.p3_trial_t0_ms
                                      : s_ctrqs.p3_trial_t0_ms +
                                            APP_CTRQS_P3_REF_DELAY_MS;
        if (now < start_ms) {
            return;
        }
        if (!calibration_manager_request_p3(
                is_responder ? TDMA_PIO_SPI_P3_ROLE_RESPONDER
                             : TDMA_PIO_SPI_P3_ROLE_INITIATOR,
                s_ctrqs.p3_baud_hz, s_ctrqs.p3_pulse_count,
                s_ctrqs.p3_capture_words, s_ctrqs.plan.generation,
                s_ctrqs.p3_signal_group)) {
            app_ctrqs_abort(
                (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
            return;
        }
        s_ctrqs.p3_started = true;
        return;
    }
    calibration_manager_p3_snapshot_t snap;
    if (!calibration_manager_get_p3_snapshot(&snap)) {
        return;
    }
    if (snap.raw.state == TDMA_PIO_SPI_P3_COMPLETE ||
        snap.raw.state == TDMA_PIO_SPI_P3_ERROR) {
        s_ctrqs.p3_trial_passed = app_ctrqs_p3_evaluate(&snap);
        calibration_manager_stop_p3();
        s_ctrqs.state = APP_CTRQS_P3_STOP;
        s_ctrqs.deadline_ms = now + APP_CTRQS_P3_STOP_MS;
    }
}

static void app_ctrqs_t0_advance(bool passed, uint64_t now)
{
    const uint32_t bit = 1u << s_ctrqs.t0_trial;
    if (passed) {
        s_ctrqs.progress.passed_mask |= bit;
    } else {
        s_ctrqs.progress.failed_mask |= bit;
    }
    s_ctrqs.progress.trials_done++;
    s_ctrqs.progress.deepest_trial = s_ctrqs.t0_trial;
    s_ctrqs.t0_trial++;
    app_ctrqs_publish();
    if (s_ctrqs.t0_trial >= s_ctrqs.trials) {
        app_ctrqs_finalize();
        return;
    }
    s_ctrqs.t0_prepared = false;
    s_ctrqs.t0_injected = false;
    /* Same anchor rule as the first trial: each repeat gets a full step budget
     * from the moment it can start, bounded by the window itself. */
    s_ctrqs.t0_trial_t0_ms = now;
    s_ctrqs.deadline_ms = now + APP_CTRQS_T0_STEP_MS;
    s_ctrqs.state = APP_CTRQS_T0_RUN;
    (void)now;
}

static void app_ctrqs_t0_step(uint64_t now)
{
    if (now >= s_ctrqs.deadline_ms) {
        calibration_manager_stop_marker_training();
        s_ctrqs.t0_trial_passed = false;
        s_ctrqs.state = APP_CTRQS_T0_STOP;
        s_ctrqs.deadline_ms = now + APP_CTRQS_T0_STOP_MS;
        return;
    }
    if (now < s_ctrqs.t0_trial_t0_ms) {
        return;
    }
    if (!s_ctrqs.t0_prepared) {
        if (!calibration_manager_request_marker_training(
                s_ctrqs.t0_codebook, app_ctrqs_marker_epoch(),
                app_ctrqs_marker_epoch(), app_ctrqs_marker_epoch(),
                s_ctrqs.plan.generation, s_ctrqs.t0_link_base_delay_ns,
                (int32_t)s_ctrqs.t0_offset_samples, s_ctrqs.t0_origin_node,
                0u)) {
            app_ctrqs_abort(
                (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
            return;
        }
        s_ctrqs.t0_prepared = true;
        return;
    }
    if (s_ctrqs.local_slot == s_ctrqs.t0_origin_node && !s_ctrqs.t0_injected) {
        if (now < s_ctrqs.t0_trial_t0_ms + APP_CTRQS_T0_INJECT_DELAY_MS) {
            return;
        }
        if (!calibration_manager_inject_marker_training()) {
            app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            return;
        }
        s_ctrqs.t0_injected = true;
        return;
    }
    calibration_training_marker_snapshot_t snap;
    if (!calibration_manager_get_marker_training_snapshot(&snap)) {
        return;
    }
    if (snap.state == CALIBRATION_TRAINING_MARKER_ACCEPTED ||
        snap.state == CALIBRATION_TRAINING_MARKER_REJECTED) {
        s_ctrqs.t0_trial_passed =
            snap.state == CALIBRATION_TRAINING_MARKER_ACCEPTED;
        /* Carry the manager's reject reason out through prog_reason: without it
         * a rejected marker training is indistinguishable from a timeout. */
        if (!s_ctrqs.t0_trial_passed && snap.reject_reason != 0u) {
            s_ctrqs.hard_fail_reason = (uint32_t)snap.reject_reason;
        }
        calibration_manager_stop_marker_training();
        s_ctrqs.state = APP_CTRQS_T0_STOP;
        s_ctrqs.deadline_ms = now + APP_CTRQS_T0_STOP_MS;
    }
}

static void app_ctrqs_t1_advance(bool passed, uint64_t now)
{
    const uint32_t bit = 1u << s_ctrqs.t1_trial;
    if (passed) {
        s_ctrqs.progress.passed_mask |= bit;
    } else {
        s_ctrqs.progress.failed_mask |= bit;
    }
    s_ctrqs.progress.trials_done++;
    s_ctrqs.progress.deepest_trial = s_ctrqs.t1_trial;
    s_ctrqs.t1_trial++;
    app_ctrqs_publish();
    if (s_ctrqs.t1_trial >= s_ctrqs.trials) {
        app_ctrqs_finalize();
        return;
    }
    s_ctrqs.t1_prepared = false;
    s_ctrqs.t1_injected = false;
    s_ctrqs.t1_trial_t0_ms =
        s_ctrqs.t0_ms + (uint64_t)s_ctrqs.t1_trial * s_ctrqs.slot_ms;
    s_ctrqs.deadline_ms = s_ctrqs.t1_trial_t0_ms + APP_CTRQS_T1_STEP_MS;
    s_ctrqs.state = APP_CTRQS_T1_RUN;
    (void)now;
}

static void app_ctrqs_t1_step(uint64_t now)
{
    const uint32_t nodes = (uint32_t)PROJECT_TDMA_GUIDE_NODES;
    const uint32_t source = s_ctrqs.t1_link_index % nodes;
    const uint32_t destination = (source + 1u) % nodes;
    const bool participant = s_ctrqs.local_slot == source ||
                             s_ctrqs.local_slot == destination;
    if (now >= s_ctrqs.deadline_ms) {
        calibration_manager_stop_sck_training();
        s_ctrqs.t1_trial_passed = false;
        s_ctrqs.state = APP_CTRQS_T1_STOP;
        s_ctrqs.deadline_ms = now + APP_CTRQS_T1_STOP_MS;
        return;
    }
    if (now < s_ctrqs.t1_trial_t0_ms) {
        return;
    }
    if (!participant) {
        /* Only the two endpoints of the trained link may arm the SCK
         * training; the other boards no-op this trial. */
        app_ctrqs_t1_advance(true, now);
        return;
    }
    if (!s_ctrqs.t1_prepared) {
        if (!calibration_manager_request_sck_training(
                source, destination, 1u, app_ctrqs_marker_epoch(),
                app_ctrqs_marker_epoch(), app_ctrqs_marker_epoch(),
                s_ctrqs.t1_launch_guard, s_ctrqs.t1_link_delay_ns, 0,
                s_ctrqs.t1_search_start, s_ctrqs.t1_search_end, 4u,
                s_ctrqs.t1_max_distance, s_ctrqs.t1_min_margin)) {
            app_ctrqs_abort(
                (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
            return;
        }
        s_ctrqs.t1_prepared = true;
        return;
    }
    if (s_ctrqs.local_slot == source && !s_ctrqs.t1_injected) {
        if (now < s_ctrqs.t1_trial_t0_ms + APP_CTRQS_T1_INJECT_DELAY_MS) {
            return;
        }
        if (!calibration_manager_inject_sck_training()) {
            app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            return;
        }
        s_ctrqs.t1_injected = true;
        return;
    }
    calibration_training_sck_snapshot_t snap;
    if (!calibration_manager_get_sck_training_snapshot(&snap)) {
        return;
    }
    if (snap.state == CALIBRATION_TRAINING_SCK_ACCEPTED ||
        snap.state == CALIBRATION_TRAINING_SCK_REJECTED) {
        s_ctrqs.t1_trial_passed =
            snap.state == CALIBRATION_TRAINING_SCK_ACCEPTED;
        calibration_manager_stop_sck_training();
        s_ctrqs.state = APP_CTRQS_T1_STOP;
        s_ctrqs.deadline_ms = now + APP_CTRQS_T1_STOP_MS;
    }
}

static void app_ctrqs_t2_advance(bool passed, uint64_t now)
{
    const uint32_t bit = 1u << s_ctrqs.t2_trial;
    if (passed) {
        s_ctrqs.progress.passed_mask |= bit;
    } else {
        s_ctrqs.progress.failed_mask |= bit;
    }
    s_ctrqs.progress.trials_done++;
    s_ctrqs.progress.deepest_trial = s_ctrqs.t2_trial;
    s_ctrqs.t2_trial++;
    app_ctrqs_publish();
    if (s_ctrqs.t2_trial >= s_ctrqs.trials) {
        app_ctrqs_finalize();
        return;
    }
    s_ctrqs.t2_prepared = false;
    s_ctrqs.t2_injected = false;
    s_ctrqs.t2_trial_t0_ms =
        s_ctrqs.t0_ms + (uint64_t)s_ctrqs.t2_trial * s_ctrqs.slot_ms;
    s_ctrqs.deadline_ms = s_ctrqs.t2_trial_t0_ms + APP_CTRQS_T2_STEP_MS;
    s_ctrqs.state = APP_CTRQS_T2_RUN;
    (void)now;
}

static void app_ctrqs_t2_step(uint64_t now)
{
    const uint32_t nodes = (uint32_t)PROJECT_TDMA_GUIDE_NODES;
    const uint32_t source = s_ctrqs.t2_link_index % nodes;
    const uint32_t destination = (source + 1u) % nodes;
    const bool participant = s_ctrqs.local_slot == source ||
                             s_ctrqs.local_slot == destination;
    if (now >= s_ctrqs.deadline_ms) {
        calibration_manager_stop_data_training();
        s_ctrqs.t2_trial_passed = false;
        s_ctrqs.state = APP_CTRQS_T2_STOP;
        s_ctrqs.deadline_ms = now + APP_CTRQS_T2_STOP_MS;
        return;
    }
    if (now < s_ctrqs.t2_trial_t0_ms) {
        return;
    }
    if (!participant) {
        app_ctrqs_t2_advance(true, now);
        return;
    }
    if (!s_ctrqs.t2_prepared) {
        if (!calibration_manager_request_data_training(
                source, destination, 1u, app_ctrqs_marker_epoch(),
                app_ctrqs_marker_epoch(), app_ctrqs_marker_epoch(),
                s_ctrqs.t2_marker_to_data, s_ctrqs.t2_link_delay_ns, 0, 0,
                s_ctrqs.t2_search_start, s_ctrqs.t2_search_end, 4u,
                s_ctrqs.t2_max_distance, s_ctrqs.t2_min_margin, 0u, 0u, 0u)) {
            app_ctrqs_abort(
                (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
            return;
        }
        s_ctrqs.t2_prepared = true;
        return;
    }
    if (s_ctrqs.local_slot == source && !s_ctrqs.t2_injected) {
        if (now < s_ctrqs.t2_trial_t0_ms + APP_CTRQS_T2_INJECT_DELAY_MS) {
            return;
        }
        if (!calibration_manager_inject_data_training()) {
            app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            return;
        }
        s_ctrqs.t2_injected = true;
        return;
    }
    calibration_training_data_snapshot_t snap;
    if (!calibration_manager_get_data_training_snapshot(&snap)) {
        return;
    }
    if (snap.state == CALIBRATION_TRAINING_DATA_ACCEPTED ||
        snap.state == CALIBRATION_TRAINING_DATA_REJECTED) {
        s_ctrqs.t2_trial_passed =
            snap.state == CALIBRATION_TRAINING_DATA_ACCEPTED;
        calibration_manager_stop_data_training();
        s_ctrqs.state = APP_CTRQS_T2_STOP;
        s_ctrqs.deadline_ms = now + APP_CTRQS_T2_STOP_MS;
    }
}

static void app_ctrqs_t3_advance(bool passed)
{
    if (passed) {
        s_ctrqs.progress.passed_mask |= 1u;
    } else {
        s_ctrqs.progress.failed_mask |= 1u;
    }
    s_ctrqs.progress.trials_done = 1u;
    s_ctrqs.progress.deepest_trial = 0u;
    app_ctrqs_publish();
    app_ctrqs_finalize();
}

static void app_ctrqs_service(void)
{    const uint64_t now = app_ctrqs_now_ms();
    /* A trigger arriving as one compact ring message is posted here, for the
     * origin and the targets alike: the executor then picks it up through the
     * same app_cal_guide_take_trigger() path the command plane used. */
    {
        uint8_t trigger_action = 0u;
        uint32_t trigger_epoch = 0u;
        if (tdma_ring_trigger_take(&trigger_action, &trigger_epoch)) {
            (void)app_cal_guide_post_trigger(trigger_action, trigger_epoch);
        }
    }
    /* Apply any negotiated phase whose ring-sequence target has been reached.
     * This is the shared instant: every board switches on the same ring
     * sequence, not on its own receipt time. */
    if (s_ctrqs.state != APP_CTRQS_IDLE && s_ctrqs.state != APP_CTRQS_DONE) {
        tdma_ring_runtime_snapshot_t phase_ring;
        if (tdma_runtime_owner_get_ring_snapshot(&phase_ring)) {
            tdma_ring_phase_service(phase_ring.ring_seq);
        }
    }
    switch (s_ctrqs.state) {
    case APP_CTRQS_IDLE: {
        guide_action_plan_t plan;
        uint32_t epoch = 0u;
        if (!app_cal_guide_take_trigger(&plan, &epoch)) {
            return;
        }
        app_ctrqs_start(&plan, epoch);
        return;
    }
    case APP_CTRQS_FANOUT:
        /* Each board runs its own stop -> topo -> arm -> forward sequence; the
         * ring-identity handshake is what arms the phys for the trial.  The
         * shared-sequence announce was measured to be unusable here (the
         * maintenance channel has no carrier once the ring stops, and ring_seq
         * is local, not shared), so the window keeps its local schedule and the
         * followers hold FORWARD afterwards instead of racing the origin. */
        if (now >= s_ctrqs.deadline_ms) {
            app_ctrqs_publish_phase((uint8_t)TDMA_RING_PHASE_STOPPING);
            app_ctrqs_stop_throttled(now);
            s_ctrqs.state =
                (s_ctrqs.plan.action_id == APP_CTRQS_ACTION_P2)
                    ? APP_CTRQS_P2_PREP
                    : (s_ctrqs.plan.action_id == APP_CTRQS_ACTION_P3)
                          ? APP_CTRQS_P3_PREP
                          : (s_ctrqs.plan.action_id == APP_CTRQS_ACTION_T0)
                                ? APP_CTRQS_T0_PREP
                                : (s_ctrqs.plan.action_id ==
                                   APP_CTRQS_ACTION_T1)
                                      ? APP_CTRQS_T1_PREP
                                      : (s_ctrqs.plan.action_id ==
                                         APP_CTRQS_ACTION_T2)
                                            ? APP_CTRQS_T2_PREP
                                            : (s_ctrqs.plan.action_id ==
                                               APP_CTRQS_ACTION_T3)
                                                  ? APP_CTRQS_T3_PREP
                                                  : APP_CTRQS_STOP;
            s_ctrqs.deadline_ms = now + APP_CTRQS_STOP_MS;
        }
        return;
    case APP_CTRQS_STOP:
        if (app_ctrqs_ring_quiescent()) {
            app_ctrqs_publish_phase((uint8_t)TDMA_RING_PHASE_STOPPED);
            s_ctrqs.state = APP_CTRQS_TOPO;
            s_ctrqs.deadline_ms = now + APP_CTRQS_TOPO_MS;
            return;
        }
        if (now >= s_ctrqs.deadline_ms) {
            app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
        }
        return;
    case APP_CTRQS_TOPO:
        if (distributed_refmem_set_tdma_ring_topology(
                s_ctrqs.local_slot, s_ctrqs.ref_slot,
                (uint32_t)PROJECT_TDMA_GUIDE_NODES)) {
            app_ctrqs_publish_phase((uint8_t)TDMA_RING_PHASE_FORMING);
            app_ctrqs_ring_diagnostic_after_topo();
            s_ctrqs.state = APP_CTRQS_ARM;
            s_ctrqs.deadline_ms =
                now + app_ctrqs_scaled_ms(APP_CTRQS_ARM_MS, 8u);
            /* Followers arm first; the reference releases the ring start last,
             * the same handshake the host training tools use. */
            s_ctrqs.arm_try_ms = (s_ctrqs.local_slot == s_ctrqs.ref_slot)
                                     ? (uint32_t)(now + APP_CTRQS_REF_ARM_DELAY_MS)
                                     : 0u;
            return;
        }
        /* The owner can refuse a topology write while the STOP/config handoff
         * is still settling; retry inside the window instead of aborting on the
         * first transient (the SCPI handler retries for the same reason). */
        if (now >= s_ctrqs.deadline_ms) {
            app_ctrqs_abort(
                (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
        }
        return;
    case APP_CTRQS_ARM:
        if (!app_ctrqs_ring_quiescent()) {
            app_ctrqs_stop_throttled(now);
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        app_ctrqs_low_rate_bringup();
        if (now < s_ctrqs.arm_try_ms) {
            return;
        }
        s_ctrqs.arm_try_ms = (uint32_t)now + 150u;
        if (!distributed_refmem_tdma_ring_arm() ||
            distributed_refmem_tdma_ring_arm_last_result() !=
                DISTRIBUTED_REFMEM_TDMA_ARM_OK) {
            app_ctrqs_stop_throttled(now);
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        s_ctrqs.state = APP_CTRQS_FORWARD;
        s_ctrqs.deadline_ms =
            now + app_ctrqs_scaled_ms(APP_CTRQS_FORWARD_MS, 6u);
        /* The shared trial epoch is anchored on the trigger receipt, so the
         * forward barrier must stay open at least until that epoch; at a slow
         * carrier the adapter needs several ring cycles to report started, so
         * the guard also scales with the cycle period. */
        const uint64_t forward_guard =
            (uint64_t)8u * app_ctrqs_staged_period_ms() + UINT64_C(1000);
        if ((uint64_t)s_ctrqs.deadline_ms <
            s_ctrqs.t0_ms + forward_guard) {
            s_ctrqs.deadline_ms =
                s_ctrqs.t0_ms + (uint32_t)forward_guard;
        }
        /* A follower no longer opens on its own t0: it waits for the origin's
         * phase broadcast, and the origin starts as much as one command
         * delivery later (measured 16-52 s at 10 kHz).  The local schedule is
         * therefore only an upper bound for the origin; followers keep the
         * forward window open long enough to see the handoff. */
        if (s_ctrqs.local_slot != s_ctrqs.ref_slot) {
            const uint64_t follower_wait =
                now + (uint64_t)APP_CTRQS_FOLLOWER_PHASE_WAIT_MS;
            if ((uint64_t)s_ctrqs.deadline_ms < follower_wait) {
                s_ctrqs.deadline_ms = follower_wait;
            }
        }
        return;
    case APP_CTRQS_FORWARD: {
        /* A P1 follower's hold ending is a completion, not a failure: its
         * persona is the whole contribution, and the phase channel has no
         * carrier while the ring is being rebuilt.  This must come before the
         * generic deadline check, which is the origin's fail-closed rule. */
        if (s_ctrqs.local_slot != s_ctrqs.ref_slot &&
            s_ctrqs.plan.action_id == APP_CTRQS_ACTION_P1 &&
            now >= s_ctrqs.deadline_ms) {
            app_ctrqs_follower_complete_all();
            return;
        }
        if (!app_ctrqs_ring_formed()) {
            tdma_ring_runtime_snapshot_t forward_ring;
            const bool have_ring =
                tdma_runtime_owner_get_ring_snapshot(&forward_ring);
            if (have_ring && forward_ring.adapter_last_error != 0u &&
                forward_ring.enabled == 0u) {
                /* The arm/start handshake is known to be flaky at a low-rate
                 * profile (the bench retries it), and a negotiated sequence
                 * makes it more likely that this board arms while the others
                 * are between phases.  Retry inside the window budget instead
                 * of failing the whole round on the first refusal. */
                if (now < s_ctrqs.deadline_ms) {
                    s_ctrqs.arm_tries++;
                    s_ctrqs.arm_try_ms = (uint32_t)now + 300u;
                    s_ctrqs.state = APP_CTRQS_ARM;
                    return;
                }
                /* The adapter start itself failed (ADAPTER_START_FAILED):
                 * waiting longer cannot help, so surface the code now. */
                app_ctrqs_abort(
                    (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE);
                return;
            }
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        if (s_ctrqs.local_slot != s_ctrqs.ref_slot &&
            !s_ctrqs.forward_armed) {
            tdma_pio_spi_clk_train_snapshot_t train;
            const bool have_train =
                tdma_runtime_owner_get_clk_train_snapshot(&train);
            if (!s_ctrqs.train_pending) {
                if (!have_train || !distributed_refmem_tdma_ring_train(1u)) {
                    app_ctrqs_abort(
                        (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
                    return;
                }
                s_ctrqs.train_base_seq = train.request_seq;
                s_ctrqs.train_pending = true;
                return;
            }
            if (!have_train) {
                return;
            }
            if (train.state == TDMA_PIO_SPI_CLK_TRAIN_FORWARDING) {
                s_ctrqs.train_pending = false;
                s_ctrqs.forward_armed = true;
            } else if (train.state == TDMA_PIO_SPI_CLK_TRAIN_ERROR) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
                return;
            } else if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
                return;
            } else {
                return;
            }
        }
        if (now >= s_ctrqs.deadline_ms) {
            app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            return;
        }
        /* The ring is formed on this board.  The origin now drives the shared
         * handoff: its phase broadcast crosses the whole ring in one
         * revolution, so every follower enters the trial phase within about
         * one revolution of the origin instead of at its own receipt offset
         * (measured 16-52 s apart).  The old "wait until t0" rule was exactly
         * that per-board offset, which is why the first board to move tore the
         * ring down under the others. */
        if (s_ctrqs.local_slot == s_ctrqs.ref_slot) {
            app_ctrqs_publish_phase((uint8_t)TDMA_RING_PHASE_TRIAL);
        } else {
            /* Hold FORWARD for the origin's whole window: entering TRIAL here
             * re-initialises the physical layer and would tear down the very
             * persona the origin is training against (measured: its later
             * trials then return nothing).  The dwell equals the origin's own
             * trial window, so both sides close at about the same moment and
             * the ring can be rebuilt by all four boards together.  A second
             * "done" message cannot be used here: it would need the ring that
             * this handoff is fixing. */
            if (now >= s_ctrqs.deadline_ms) {
                /* The hold is over.  Measuring the persona now is the whole of
                 * this board's contribution, so a late/absent broadcast must
                 * complete the round rather than fail it: the phase channel has
                 * no carrier while the ring is being rebuilt. */
                app_ctrqs_follower_complete_all();
                return;
            }
            if (tdma_ring_phase_local() != (uint8_t)TDMA_RING_PHASE_TRIAL) {
                return;
            }
            if (s_ctrqs.phase_seen_ms == 0u) {
                s_ctrqs.phase_seen_ms = (uint32_t)now;
            }
            const uint64_t dwell =
                (uint64_t)s_ctrqs.phase_seen_ms +
                (uint64_t)s_ctrqs.trials * s_ctrqs.slot_ms;
            if ((uint64_t)now < dwell) {
                return;
            }
            app_ctrqs_follower_complete_all();
            return;
        }
        s_ctrqs.t0_ms = now;
        s_ctrqs.t_end_ms =
            now + ((uint64_t)s_ctrqs.trials * s_ctrqs.slot_ms);
        s_ctrqs.state = APP_CTRQS_TRIAL;
        return;
    }
    case APP_CTRQS_TRIAL:
        app_ctrqs_trial();
        return;
    case APP_CTRQS_P2_PREP:
        if (s_ctrqs.p2_prep_step == 0u) {
            if (!app_ctrqs_ring_torn_down()) {
                app_ctrqs_stop_throttled(now);
                if (now >= s_ctrqs.deadline_ms) {
                    app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
                }
                return;
            }
            /* Host-equivalent prep: clear the stale training stage so the
             * coded persona can acquire the PIO resources.  The persisted
             * record stays in flash; P2_CODED_STOP re-stages it before the
             * window close so the guide ring can be re-formed. */
            (void)calibration_manager_clear_training_stage();
            if (!calibration_manager_set_topology_probe_mode(
                    true, s_ctrqs.p2_probe_phase)) {
                app_ctrqs_abort(
                    (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
                return;
            }
            /* The ring is formed at the carrier the window was entered with
             * (the guide level, with BRINGUP), not at the measurement profile:
             * the measurement profile is applied after the teardown, when the
             * coded link measurement actually runs.  Applying it here would ask
             * a low-rate guide window to form a 10 MHz ring. */
            s_ctrqs.p2_prep_step = 1u;
            s_ctrqs.deadline_ms = now + APP_CTRQS_P2_STEP_MS;
            return;
        }
        if (!distributed_refmem_set_tdma_ring_topology(
                s_ctrqs.local_slot, s_ctrqs.ref_slot,
                (uint32_t)PROJECT_TDMA_GUIDE_NODES)) {
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort(
                    (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
            }
            return;
        }
        app_ctrqs_ring_diagnostic_after_topo();
        s_ctrqs.state = APP_CTRQS_P2_ARM;
        s_ctrqs.deadline_ms = now + APP_CTRQS_P2_STEP_MS;
        s_ctrqs.arm_try_ms = (s_ctrqs.local_slot == s_ctrqs.ref_slot)
                                 ? (uint32_t)(now + APP_CTRQS_REF_ARM_DELAY_MS)
                                 : 0u;
        return;
    case APP_CTRQS_P2_ARM:
        if (!app_ctrqs_ring_formed()) {
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
                return;
            }
            if (now < s_ctrqs.arm_try_ms) {
                return;
            }
            s_ctrqs.arm_try_ms = (uint32_t)now + 150u;
            if (!app_ctrqs_ring_quiescent()) {
                app_ctrqs_stop_throttled(now);
            } else {
                app_ctrqs_low_rate_bringup();
                if (distributed_refmem_tdma_ring_arm() &&
                    distributed_refmem_tdma_ring_arm_last_result() !=
                        DISTRIBUTED_REFMEM_TDMA_ARM_OK) {
                    app_ctrqs_stop_throttled(now);
                }
            }
            return;
        }
        s_ctrqs.state = APP_CTRQS_P2_STOP;
        s_ctrqs.deadline_ms = now + APP_CTRQS_P2_STOP_MS;
        return;
    case APP_CTRQS_P2_STOP:
        app_ctrqs_stop_throttled(now);
        if (!app_ctrqs_ring_torn_down()) {
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        (void)calibration_manager_set_topology_probe_mode(false, 0u);
        /* Ring torn down: switch to the measurement profile.  The coded link
         * measurement needs its own carrier (10 MHz), while the ring that had
         * to arm and align the four boards was formed at the guide level. */
        if (s_ctrqs.p2_level != 0u &&
            (!tdma_runtime_owner_stage_operating_profile(s_ctrqs.p2_level) ||
             !tdma_runtime_owner_apply_operating_profile())) {
            app_ctrqs_abort(
                (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
            return;
        }
        s_ctrqs.state = APP_CTRQS_P2_WAIT;
        s_ctrqs.deadline_ms = s_ctrqs.t0_ms + APP_CTRQS_P2_CODED_MS;
        s_ctrqs.p2_ref_start_ms = (uint32_t)(s_ctrqs.t0_ms +
                                             APP_CTRQS_P2_REF_DELAY_MS);
        return;
    case APP_CTRQS_P2_WAIT: {
        calibration_clk_coded_snapshot_t coded;
        const bool have = calibration_manager_get_clk_coded_snapshot(&coded);
        if (!s_ctrqs.p2_started) {
            if (now < s_ctrqs.t0_ms) {
                return;
            }
            if (s_ctrqs.local_slot == s_ctrqs.ref_slot &&
                now < s_ctrqs.p2_ref_start_ms) {
                return;
            }
            if (!calibration_manager_request_clk_coded_at_epoch(
                    s_ctrqs.p2_codebook, s_ctrqs.p2_min_lag,
                    s_ctrqs.p2_max_lag, s_ctrqs.p2_max_distance,
                    s_ctrqs.p2_min_margin, s_ctrqs.plan.generation)) {
                app_ctrqs_abort(
                    (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
                return;
            }
            s_ctrqs.p2_started = true;
            return;
        }
        if (!have) {
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        if (s_ctrqs.local_slot == s_ctrqs.ref_slot) {
            bool done = false;
            bool pass = false;
            if (coded.state == CALIBRATION_CLK_CODED_ACCEPTED) {
                done = true;
                pass = true;
            } else if (coded.state == CALIBRATION_CLK_CODED_REJECTED) {
                done = true;
                s_ctrqs.hard_fail_reason =
                    coded.reject_reason != 0u
                        ? coded.reject_reason
                        : (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
            } else if (now >= s_ctrqs.deadline_ms) {
                done = true;
                s_ctrqs.hard_fail_reason =
                    (uint32_t)REFMEM_COMMAND_REASON_TIMEOUT;
            }
            if (!done) {
                return;
            }
            s_ctrqs.progress.trials_done = 1u;
            s_ctrqs.progress.deepest_trial = 0u;
            if (pass) {
                s_ctrqs.progress.passed_mask = 1u;
            } else {
                s_ctrqs.progress.failed_mask = 1u;
            }
            app_ctrqs_publish();
        } else {
            /* A follower's manager state stays CLOCK_COARSE by design; a
             * matching sequence with the coarse flag proves its request was
             * accepted (the raw forward SM is the TDMA layer's own evidence). */
            const bool coded_started =
                coded.state == CALIBRATION_CLK_CODED_CLOCK_COARSE &&
                (coded.flags &
                 CALIBRATION_CLK_CODED_FLAG_COARSE_BRACKET_VALID) != 0u &&
                (s_ctrqs.plan.generation == 0u ||
                 coded.train_sequence == s_ctrqs.plan.generation);
            if (!s_ctrqs.p2_fwd_seen &&
                (coded_started ||
                 coded.state == CALIBRATION_CLK_CODED_CLOCK_CODED ||
                 coded.state == CALIBRATION_CLK_CODED_ACCEPTED)) {
                s_ctrqs.p2_fwd_seen = true;
            }
            if (now < s_ctrqs.deadline_ms) {
                return;
            }
            s_ctrqs.progress.trials_done = 1u;
            s_ctrqs.progress.deepest_trial = 0u;
            if (s_ctrqs.p2_fwd_seen) {
                s_ctrqs.progress.passed_mask = 1u;
            } else {
                s_ctrqs.progress.failed_mask = 1u;
                s_ctrqs.hard_fail_reason =
                    (uint32_t)REFMEM_COMMAND_REASON_TIMEOUT;
            }
            app_ctrqs_publish();
        }
        s_ctrqs.state = APP_CTRQS_P2_CODED_STOP;
        s_ctrqs.deadline_ms = now + APP_CTRQS_P2_STOP_MS;
        return;
    }
    case APP_CTRQS_P2_CODED_STOP: {
        calibration_clk_coded_snapshot_t coded;
        (void)calibration_manager_stop_clk_coded();
        const bool idle = calibration_manager_get_clk_coded_snapshot(&coded) &&
                          coded.state == CALIBRATION_CLK_CODED_IDLE;
        if (!idle && now < s_ctrqs.deadline_ms) {
            return;
        }
        if (!idle && s_ctrqs.hard_fail_reason == 0u) {
            s_ctrqs.hard_fail_reason =
                (uint32_t)REFMEM_COMMAND_REASON_TIMEOUT;
        }
        /* Re-stage the persisted calibration record cleared by P2_PREP so the
         * window close can pass the ring phase admission again. */
        if (!calibration_manager_restore_training_stage() &&
            s_ctrqs.hard_fail_reason == 0u) {
            s_ctrqs.hard_fail_reason =
                (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE;
        }
        app_ctrqs_finalize();
        return;
    }
    case APP_CTRQS_P3_PREP:
        if (!app_ctrqs_ring_torn_down()) {
            app_ctrqs_stop_throttled(now);
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        /* Same split as P2: the ring was formed at the guide carrier, but the
         * per-link ranging is a raw measurement that needs the plan's
         * measurement profile.  Applying it before the teardown would ask a
         * low-rate guide window to form a 10 MHz ring. */
        if (s_ctrqs.plan.profile_level != 0u &&
            (!tdma_runtime_owner_stage_operating_profile(
                 s_ctrqs.plan.profile_level) ||
             !tdma_runtime_owner_apply_operating_profile())) {
            app_ctrqs_abort(
                (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
            return;
        }
        s_ctrqs.p3_trial = 0u;
        s_ctrqs.p3_started = false;
        s_ctrqs.p3_trial_t0_ms = s_ctrqs.t0_ms;
        s_ctrqs.deadline_ms = s_ctrqs.p3_trial_t0_ms + APP_CTRQS_P3_STEP_MS;
        s_ctrqs.state = APP_CTRQS_P3_RUN;
        return;
    case APP_CTRQS_P3_RUN:
        app_ctrqs_p3_step(now);
        return;
    case APP_CTRQS_P3_STOP: {
        /* The P3 program must be fully released before the window close tries
         * to arm the ring again: the p3 restore machine unloads its pio2
         * programs asynchronously, so wait for both the idle state and the
         * NORMAL persona (a stopped-but-still-loaded persona makes the rebuild
         * ARM fail with PROGRAM_LOAD). */
        calibration_manager_p3_snapshot_t snap;
        tdma_pio_spi_phys_snapshot_t phys;
        const bool idle = calibration_manager_get_p3_snapshot(&snap) &&
                          snap.raw.state == TDMA_PIO_SPI_P3_IDLE;
        const bool persona_clean =
            tdma_runtime_owner_get_phys_snapshot(&phys) &&
            phys.program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL;
        if ((!idle || !persona_clean) && now < s_ctrqs.deadline_ms) {
            return;
        }
        app_ctrqs_p3_advance(idle && persona_clean && s_ctrqs.p3_trial_passed,
                             now);
        return;
    }
    case APP_CTRQS_T0_PREP:
        if (!app_ctrqs_ring_torn_down()) {
            app_ctrqs_stop_throttled(now);
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        s_ctrqs.t0_trial = 0u;
        s_ctrqs.t0_prepared = false;
        s_ctrqs.t0_injected = false;
        /* Anchor the trial where it can actually start: the wait above is the
         * ring teardown, which takes seconds at a low-rate profile.  Anchoring
         * at the window start left almost no budget, so the first service tick
         * of T0_RUN already saw an expired deadline and every trial "timed out"
         * without the marker ever being injected. */
        s_ctrqs.t0_trial_t0_ms = now;
        s_ctrqs.deadline_ms = now + APP_CTRQS_T0_STEP_MS;
        s_ctrqs.state = APP_CTRQS_T0_RUN;
        return;
    case APP_CTRQS_T0_RUN:
        app_ctrqs_t0_step(now);
        return;
    case APP_CTRQS_T0_STOP: {
        calibration_training_marker_snapshot_t snap;
        const bool idle = calibration_manager_get_marker_training_snapshot(
                              &snap) &&
                          snap.state == CALIBRATION_TRAINING_MARKER_IDLE;
        if (!idle && now < s_ctrqs.deadline_ms) {
            return;
        }
        app_ctrqs_t0_advance(idle && s_ctrqs.t0_trial_passed, now);
        return;
    }
    case APP_CTRQS_T1_PREP:
        if (!app_ctrqs_ring_torn_down()) {
            app_ctrqs_stop_throttled(now);
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        s_ctrqs.t1_trial = 0u;
        s_ctrqs.t1_prepared = false;
        s_ctrqs.t1_injected = false;
        s_ctrqs.t1_trial_t0_ms = s_ctrqs.t0_ms;
        s_ctrqs.deadline_ms = s_ctrqs.t1_trial_t0_ms + APP_CTRQS_T1_STEP_MS;
        s_ctrqs.state = APP_CTRQS_T1_RUN;
        return;
    case APP_CTRQS_T1_RUN:
        app_ctrqs_t1_step(now);
        return;
    case APP_CTRQS_T1_STOP: {
        calibration_training_sck_snapshot_t snap;
        const bool idle =
            calibration_manager_get_sck_training_snapshot(&snap) &&
            snap.state == CALIBRATION_TRAINING_SCK_IDLE;
        if (!idle && now < s_ctrqs.deadline_ms) {
            return;
        }
        app_ctrqs_t1_advance(idle && s_ctrqs.t1_trial_passed, now);
        return;
    }
    case APP_CTRQS_T2_PREP:
        if (!app_ctrqs_ring_torn_down()) {
            app_ctrqs_stop_throttled(now);
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        s_ctrqs.t2_trial = 0u;
        s_ctrqs.t2_prepared = false;
        s_ctrqs.t2_injected = false;
        s_ctrqs.t2_trial_t0_ms = s_ctrqs.t0_ms;
        s_ctrqs.deadline_ms = s_ctrqs.t2_trial_t0_ms + APP_CTRQS_T2_STEP_MS;
        s_ctrqs.state = APP_CTRQS_T2_RUN;
        return;
    case APP_CTRQS_T2_RUN:
        app_ctrqs_t2_step(now);
        return;
    case APP_CTRQS_T2_STOP: {
        calibration_training_data_snapshot_t snap;
        const bool idle =
            calibration_manager_get_data_training_snapshot(&snap) &&
            snap.state == CALIBRATION_TRAINING_DATA_IDLE;
        if (!idle && now < s_ctrqs.deadline_ms) {
            return;
        }
        app_ctrqs_t2_advance(idle && s_ctrqs.t2_trial_passed, now);
        return;
    }
    case APP_CTRQS_T3_PREP:
        if (!app_ctrqs_ring_torn_down()) {
            app_ctrqs_stop_throttled(now);
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
            }
            return;
        }
        /* Only T3 may start the ring at the measurement profile, and it has a
         * dedicated arm path: apply the profile here (after the guide ring is
         * down) and go to T3_ARM.  The generic REBUILD path used before closes
         * the window instead of running the final action, so T3 never armed,
         * never reached 10 MHz and never recorded a result. */
        s_ctrqs_t3_stage = 8u;
        if (s_ctrqs.plan.profile_level != 0u &&
            (!tdma_runtime_owner_stage_operating_profile(
                 s_ctrqs.plan.profile_level) ||
             !tdma_runtime_owner_apply_operating_profile())) {
            app_ctrqs_abort(
                (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
            return;
        }
        s_ctrqs.t3_topo = false;
        s_ctrqs.t3_armed = false;
        s_ctrqs.arm_try_ms = 0u;
        s_ctrqs.deadline_ms = now + APP_CTRQS_REBUILD_MS;
        s_ctrqs.state = APP_CTRQS_T3_ARM;
        return;
    case APP_CTRQS_T3_ARM:
        if (app_ctrqs_restore_formed()) {
            s_ctrqs_t3_stage = 7u;
            app_ctrqs_t3_advance(true);
            return;
        }
        if (!s_ctrqs.t3_topo) {
            s_ctrqs_t3_stage = 1u;
            if (!distributed_refmem_set_tdma_ring_topology(
                    s_ctrqs.local_slot, s_ctrqs.ref_slot,
                    (uint32_t)PROJECT_TDMA_GUIDE_NODES)) {
                if (now >= s_ctrqs.deadline_ms) {
                    app_ctrqs_abort(
                        (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE);
                }
                return;
            }
            app_ctrqs_ring_diagnostic_after_topo();
            s_ctrqs.t3_topo = true;
            return;
        }
        /* The stop-and-wait gate only applies before the ARM.  ARM sets
         * enabled=1 synchronously, so re-entering this gate afterwards stopped
         * the ring we had just armed and left every later START refused
         * (measured: ARM_OK, enabled=0 for the remaining 60 s).  APP_CTRQS_ARM
         * avoids the same trap by leaving the state in the same call. */
        if (!s_ctrqs.t3_armed) {
            s_ctrqs_t3_stage = 3u;
            if (!app_ctrqs_ring_quiescent()) {
                app_ctrqs_stop_throttled(now);
                if (now >= s_ctrqs.deadline_ms) {
                    app_ctrqs_abort(
                        (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE);
                }
                return;
            }
            s_ctrqs_t3_stage = 4u;
            if (now < s_ctrqs.arm_try_ms) {
                return;
            }
            s_ctrqs_t3_stage = 5u;
            s_ctrqs.arm_try_ms = (uint32_t)now + 300u;
            if (distributed_refmem_tdma_ring_arm() &&
                distributed_refmem_tdma_ring_arm_last_result() ==
                    DISTRIBUTED_REFMEM_TDMA_ARM_OK) {
                s_ctrqs.t3_armed = true;
            } else if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort(
                    (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE);
            }
            return;
        }
        /* Armed: wait until Core1 has actually started the adapter for this
         * configuration.  set_data_enabled() requires adapter_started and
         * applied==config, so START before that is refused and would look like
         * a start failure. */
        s_ctrqs_t3_stage = 9u;
        tdma_ring_runtime_snapshot_t armed_ring;
        if (!tdma_runtime_owner_get_ring_snapshot(&armed_ring) ||
            armed_ring.enabled == 0u || armed_ring.adapter_started == 0u ||
            armed_ring.config_seq != armed_ring.applied_config_seq) {
            if (now >= s_ctrqs.deadline_ms) {
                app_ctrqs_abort(
                    (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE);
            }
            return;
        }
        s_ctrqs_t3_stage = 10u;
        if (!s_ctrqs.t3_started) {
            if (now < s_ctrqs.arm_try_ms) {
                return;
            }
        s_ctrqs.arm_try_ms = (uint32_t)now + 150u;
        const bool started = distributed_refmem_tdma_ring_start();
        /* Report which way START failed: the API only returns a bool and has no
         * status readback, so the sub-step code carries both the call result and
         * whether the ring actually became enabled (60 = call refused,
         * 61 = call accepted but the ring stayed disabled, 6 = enabled). */
        tdma_ring_runtime_snapshot_t start_ring;
        const bool have_start_ring =
            tdma_runtime_owner_get_ring_snapshot(&start_ring);
        s_ctrqs_t3_stage =
            started ? ((have_start_ring && start_ring.enabled != 0u) ? 6u : 61u)
                    : 60u;
        if (started) {
            /* Latch: a successful START must not be repeated every 150 ms, or a
             * transient refusal from the guard windows would tear down a ring
             * that is still forming. */
            s_ctrqs.t3_started = true;
        } else {
            /* START refused.  Do NOT drop t3_topo: the topology step runs
             * before the quiescent gate and requires enabled==0, while a refused
             * START leaves enabled==1, so re-entering it would spin there until
             * the deadline and never stop the ring.  Dropping the arm instead
             * lets the existing !t3_armed gate stop to full quiescence, after
             * which the staged 10 MHz config can be armed again. */
            s_ctrqs.t3_armed = false;
            s_ctrqs.arm_try_ms = 0u;
            app_ctrqs_stop_throttled(now);
            /* Bounded retries: without a cap the 60 s window is spent repeating
             * the same stop/arm cycle and the only visible outcome is a generic
             * NOT_ONLINE_SAFE. */
            s_ctrqs.arm_tries++;
            if (s_ctrqs.arm_tries >= APP_CTRQS_ARM_RETRIES) {
                app_ctrqs_abort(
                    (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE);
                return;
            }
        }
        }
        if (app_ctrqs_restore_formed() && app_ctrqs_t3_profile_formed()) {
            app_ctrqs_t3_advance(true);
            return;
        }
        if (now >= s_ctrqs.deadline_ms) {
            app_ctrqs_abort((uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE);
        }
        return;
    case APP_CTRQS_RESTORE_WAIT:
        if (s_boot_guide_done && s_boot_fail_step == 0u) {
            if (app_ctrqs_restore_formed()) {
                s_ctrqs.state = APP_CTRQS_Q;
                return;
            }
            if (!app_ctrqs_boot_plan_present()) {
                /* No frozen boot plan in this image: the replay marked itself
                 * done without forming a ring.  Rebuild the captured ring with
                 * the same TOPO/ARM/START primitives instead. */
                s_ctrqs.rebuild_topo_done = false;
                s_ctrqs.arm_try_ms = 0u;
                s_ctrqs.deadline_ms = now + APP_CTRQS_REBUILD_MS;
                s_ctrqs.state = APP_CTRQS_REBUILD;
                return;
            }
        }
        if (now >= s_ctrqs.deadline_ms) {
            s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_FAILED;
            s_ctrqs.progress.reason =
                (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE;
            app_ctrqs_publish();
            s_ctrqs.state = APP_CTRQS_Q;
        }
        return;
    case APP_CTRQS_REBUILD:
        if (app_ctrqs_restore_formed()) {
            /* T3's result must come from its own arm/start path
             * (app_ctrqs_t3_advance); this rebuild is the post-abort fallback
             * that merely restores the ring.  Marking T3 complete here reported
             * a failed action as COMPLETE (and made a rerun pass on evidence
             * that came from the recovery path). */
            s_ctrqs.state = APP_CTRQS_Q;
            return;
        }
        if (!s_ctrqs.rebuild_topo_done) {
            if (!app_ctrqs_ring_quiescent()) {
                app_ctrqs_stop_throttled(now);
                if (now >= s_ctrqs.deadline_ms) {
                    goto ctrqs_rebuild_failed;
                }
                return;
            }
            if (!distributed_refmem_set_tdma_ring_topology(
                    s_ctrqs.restore_local_slot, s_ctrqs.restore_reference_slot,
                    s_ctrqs.restore_node_count)) {
                if (now >= s_ctrqs.deadline_ms) {
                    goto ctrqs_rebuild_failed;
                }
                return;
            }
            app_ctrqs_ring_diagnostic_after_topo();
            s_ctrqs.rebuild_topo_done = true;
            s_ctrqs.arm_try_ms = (uint32_t)now + 300u;
            return;
        }
        if (now < s_ctrqs.arm_try_ms) {
            return;
        }
        s_ctrqs.arm_try_ms = (uint32_t)now + 150u;
        if (!s_ctrqs.rebuild_armed) {
            if (!app_ctrqs_ring_quiescent()) {
                app_ctrqs_stop_throttled(now);
            } else {
                app_ctrqs_low_rate_bringup();
                if (distributed_refmem_tdma_ring_arm() &&
                    distributed_refmem_tdma_ring_arm_last_result() ==
                        DISTRIBUTED_REFMEM_TDMA_ARM_OK) {
                    s_ctrqs.rebuild_armed = true;
                }
            }
        } else {
            /* ARM starts the adapter; START releases the ring traffic.  A
             * rejected/ignored START is retried on the next tick; the formed
             * check at the top of this case is the barrier. */
            (void)distributed_refmem_tdma_ring_start();
        }
        if (now >= s_ctrqs.deadline_ms) {
            goto ctrqs_rebuild_failed;
        }
        return;
    ctrqs_rebuild_failed:
        s_ctrqs.progress.status = (uint32_t)GUIDE_ACTION_PROGRESS_FAILED;
        s_ctrqs.progress.reason =
            (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE;
        app_ctrqs_publish();
        s_ctrqs.state = APP_CTRQS_Q;
        return;
    case APP_CTRQS_Q: {
        guide_action_progress_t readback;
        guide_action_reconcile_result_t reconcile;
        const bool have = app_cal_guide_read_progress(&readback, s_ctrqs.epoch);
        guide_action_reconcile(&readback, have, s_ctrqs.plan.action_id,
                               s_ctrqs.plan.plan_crc32,
                               s_ctrqs.plan.generation, &reconcile);
        app_cal_guide_set_q_result(&readback, have, &reconcile);
        app_ctrqs_commit(reconcile.decision);
        uint32_t ladder_step = 0u;
        if (reconcile.decision == GUIDE_ACTION_RECONCILE_COMPLETE &&
            s_ctrqs.plan.action_id == s_ctrqs_ladder.action_index + 1u) {
            guide_action_ladder_t next;
            ladder_step = (uint32_t)guide_action_ladder_advance(
                &s_ctrqs_ladder, &reconcile, &next);
            s_ctrqs_ladder = next;
        } else if (reconcile.decision == GUIDE_ACTION_RECONCILE_FAILED) {
            ladder_step = (uint32_t)GUIDE_ACTION_LADDER_STOPPED;
        }
        app_cal_guide_set_ladder_result(s_ctrqs_ladder.action_index, ladder_step);
        s_ctrqs.state = APP_CTRQS_DONE;
        return;
    }
    case APP_CTRQS_DONE:
    default:
        s_ctrqs.state = APP_CTRQS_IDLE;
        return;
    }
}

uint32_t app_guide_ctrqs_state(void)
{
    return (uint32_t)s_ctrqs.state;
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
    refmem_function_service();
    trigger_t2_runtime_prepare_core0();
}

void app_config_gate_service(void)
{
    system_manager_service();
}

static bool s_ota_tdma_binding_ready;
static bool s_ota_tdma_parser_registered;

void app_ota_service(void)
{
    ota_ao_service(500u);
    /* The OTA frame parser must exist before the first ring frame arrives;
     * registration is independent of the local stream session. */
    if (!s_ota_tdma_parser_registered) {
        s_ota_tdma_parser_registered = ota_tdma_binding_register_parser();
    }
    /* Static maintenance mode: the TDMA ring carries OTA frames, so the AO
     * task drains the maintenance transport while a stream session is active.
     * The binding is created lazily (it needs the TDMA runtime local slot) and
     * never runs outside that mode. */
    const bool relay_active = ota_tdma_binding_relay_active();
    if (!ota_ao_tdma_maintenance_allowed() && !relay_active) {
        return;
    }
    if (!s_ota_tdma_binding_ready) {
        s_ota_tdma_binding_ready = ota_tdma_binding_init();
    }
    /* A relay started from the SCPI path may have initialised the binding
     * itself; the service must still run so relay_tick can start/retry the
     * source pump. */
    if (relay_active || s_ota_tdma_binding_ready) {
        ota_tdma_binding_service(board_uptime_ms(), 2u);
    }
}

void app_diag_service(void)
{
    (void)app_guide_boot_service();
    app_guide_listen_service();
    app_guide_ring_watch_service();
    app_guide_scan_service();
    app_ctrqs_service();
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
     * WCET/deadline violation.  Foundation phases are never quarantined at
     * all (see the isolation rule in the phase dispatcher), so any stale
     * isolation of one is released here rather than keeping the base's own
     * input/publication path switched off. */
    const uint32_t newly_enabled = enabled_mask & ~previous_mask;
    (void)__atomic_fetch_and(&s_realtime_load_quarantined_mask,
                             ~(newly_enabled | APP_REALTIME_LOAD_FOUNDATION_MASK),
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
        /* The window stays open until the phase close point so a late priority
         * RX interrupt is still charged here. That deliberate wait is TDMA
         * cycle occupancy and is recorded in clk_sys ticks. */
        const uint64_t wait_start = tdma_service_timing_now();
        app_realtime_wait_until(cycle_epoch, work->priority.close_cycle);
        tdma_service_timing_record(TDMA_TIMING_PHASE_WAIT, wait_start);
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
    if (work->run_service && phase_id == APP_REALTIME_PHASE_TDMA) {
        /* Close the TDMA phase record only after the deliberate wait so the
         * record covers the whole cycle occupancy, waiting included. A skipped
         * or handed-off phase never opened a record; both calls no-op then. */
        tdma_service_timing_phase_end();
        tdma_service_timing_scheduler_end(work->runtime_cycles);
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

    /* Isolation rule: quarantine exists to isolate opportunistic loads from the
     * node's own input/publication path, never to switch that path off.  The
     * foundation phases (VDC, DPLL, SYNC_CAPTURE, REFMEM, SYNC_TRIGGER) carry
     * the base's event capture and publication, and the measured behaviour made
     * their isolation self-defeating: one overrun sets the bit, the bit is only
     * released on a disabled -> enabled transition of the load mask, and
     * SYNC_CAPTURE's declared WCET sits below its measured worst case, so it
     * re-quarantined immediately after every release
     * (VDC-PROGRESS-20261014-035).  A foundation overrun is still recorded as a
     * schedule miss and stays visible in the schedule/alarm evidence; it just no
     * longer removes the base's observation lane. */
    const uint32_t quarantinable = ~APP_REALTIME_LOAD_FOUNDATION_MASK;
    if ((work->overrun || work->own_deadline_missed) && work->optional_load && !work->warmup_cycle &&
        (work->load_bit & quarantinable) != 0u) {
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

__attribute__((aligned(32)))
static void app_realtime_tdma_phase(void)
{
    tdma_service_timing_phase_begin();
    tdma_service_timing_context(tdma_runtime_owner_timing_context(), true);
    /* NO5 is a ring-external read-only observer.  Keep its phase-only
     * SyncIO/VDC path alive, but never service the TDMA owner on that board;
     * this prevents accidental PIO/SM/DMA/GPIO/IRQ/DREQ activity while the
     * NO1..NO4 state-machine ring is running independently. */
    if (board_identity_get_no() != 5u) {
        /* The owner harvests the physical observer FIFOs once per phase inside
         * tdma_runtime_owner_service_phys_tx(). Calling the observer again here
         * serviced the same finite FIFOs twice and spent the TDMA window twice;
         * the blackout branch keeps its own explicit harvest because it skips
         * the full service body. */
        tdma_component_core1_service();
    }
    vdc_run_output_service_core1();
    /* Reference intent/retirement must progress even when the optional SYNC
     * capture load is disabled. Hardware counts edges; this is one bounded
     * state transition, inside the mandatory static TDMA phase budget. */
    sync_io_reference_service_core1();
    /* The analyzer intent mailbox is a mandatory bounded Core1 service.
     * It must not live behind an optional/quarantinable load, otherwise an
     * accepted ARM/STOP could remain pending forever.  TDMA remains first;
     * analyzer work is capped to a small record budget afterward. */
    uint64_t timing_start = tdma_service_timing_now();
    sync_io_logic_analyzer_service_core1(8u);
    tdma_service_timing_record(TDMA_TIMING_ANALYZER, timing_start);
    if (trigger_sequence_service_pending()) {
        trigger_sequence_service_service();
    }
    timing_start = tdma_service_timing_now();
    drv_watchdog_mark_progress(1u, 0x0101u);
    diagnostics_record_core1_loop();
    diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_CORE1);
    drv_watchdog_mark_progress(1u, 0x0103u);
    tdma_service_timing_record(TDMA_TIMING_ACCOUNTING, timing_start);
    tdma_service_timing_context(tdma_runtime_owner_timing_context(), false);
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
    /* C1 flash park, first thing in the cycle: when core0 owns a flash
     * transaction it raises a park request, and core1 must acknowledge it
     * BEFORE any ring work.  The existing poll in app_runtime.c sits outside
     * this call, so a long ring cycle could swallow it and every erase/program
     * would time out (field: FLASH_ERASE whenever the ring was running, while
     * the same path succeeded with the ring stopped).  The poll is a no-op
     * unless a request is outstanding; when one is, core1 spins here until
     * core0 releases the session, which is the agreed short ring stop. */
    drv_flash_core1_lockout_poll();
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

/* ---- REFMEM control plane: flash park window ----------------------------- */

/* The TDMA ring runs on core1 and keeps it away from its flash-park poll, so a
 * flash erase/program issued while the ring is up times out in the lockout
 * handshake.  These two hooks are the agreed "short stop": core0 asks REFMEM to
 * stop the ring, parks core1, performs the flash operation and restarts the
 * ring.  The OTA session state lives in the AO vector (and is mirrored to the
 * REFMEM control plane), so nothing is lost while the ring is briefly down. */
bool app_flash_park_pause_ring(void)
{
    return distributed_refmem_tdma_ring_stop();
}

void app_flash_park_resume_ring(void)
{
    (void)distributed_refmem_tdma_ring_start();
}/* ---- REFMEM control plane: transaction-scoped ring window ---------------- */

/* Every flash program/erase needs core1 out of flash, and the OTA journal also
 * programs a completion record after the block itself is written.  The whole
 * transaction therefore runs with the TDMA ring stopped; it is restarted right
 * after, so the ring sees one short gap per OTA block instead of a broken
 * transfer.  Only stop a ring that is actually running: boot-time journal work
 * must never start one. */
bool app_flash_window_stop_ring(void)
{
    tdma_ring_runtime_snapshot_t snapshot;
    if (!tdma_runtime_owner_get_ring_snapshot(&snapshot) || snapshot.enabled == 0u) {
        return false;
    }
    return distributed_refmem_tdma_ring_stop();
}

void app_flash_window_start_ring(void)
{
    (void)distributed_refmem_tdma_ring_start();
}