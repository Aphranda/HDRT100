#include "vdc_dpll_manager.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_config.h"
#include "board_identity.h"
#include "ota_crc32.h"
#include "osal.h"
#include "ota_ao.h"
#include "storage_manager.h"
#include "sync_io.h"
#include "tdma_runtime_owner.h"
#include "tdma_service.h"
#include "vdc_domain.h"
#include "vdc_ring_observer.h"
#include "vdc_sync_io_adapter.h"
#include "vdc_tdma_payload.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#include "pico/time.h"
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) name
#endif

#define VDC_DPLL_MANAGER_SELF_TEST_CLEANUP_MARGIN_MS 250u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_MAGIC 0x4C504444u /* DDPL */
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_SCHEMA 1u
#define VDC_DPLL_MANAGER_WAVEFORM_MAGIC 0x57524D53u /* SMRW */
#define VDC_DPLL_MANAGER_WAVEFORM_SCHEMA 1u
#define VDC_DPLL_MANAGER_WAVEFORM_BUFFER_COUNT 2u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t schema;
    uint16_t record_size;
    uint32_t record_count;
    uint32_t dropped_count;
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t payload_crc32;
} vdc_dpll_manager_dpll_capture_header_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t schema;
    uint16_t header_size;
    uint16_t record_size;
    uint16_t reserved;
    uint32_t session_id;
    uint32_t segment_index;
    uint32_t first_record_index;
    uint32_t record_count;
    uint32_t dropped_count;
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t observed_mask;
    uint32_t payload_crc32;
} vdc_dpll_manager_waveform_header_t;

_Static_assert(
    sizeof(vdc_dpll_manager_waveform_header_t) +
        VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS *
            sizeof(vdc_dpll_manager_waveform_record_t) <= 8192u,
    "waveform segment must fit StorageAO write buffer");

static vdc_dpll_manager_vdc_status_t s_vdc_status;
static vdc_dpll_manager_dpll_status_t s_dpll_status;
static vdc_dpll_manager_sync_io_observer_config_t s_sync_io_observer_config;
static vdc_dpll_manager_sync_io_observer_status_t s_sync_io_observer_status;
static vdc_dpll_manager_vdc_status_t s_published_vdc_status;
static vdc_dpll_manager_dpll_status_t s_published_dpll_status;
static vdc_dpll_manager_dco_consumer_status_t s_dco_consumer_status;
static vdc_dpll_manager_dco_consumer_status_t s_published_dco_consumer_status;
static volatile uint32_t s_published_dpll_status_guard;
static vdc_dpll_manager_sync_io_observer_status_t
    s_published_sync_io_observer_status;
static volatile uint32_t s_published_snapshot_guard;
static vdc_domain_snapshot_t s_published_snapshot;
static bool s_published_snapshot_valid;
static volatile uint32_t s_published_dpll_update_seq;
static uint32_t s_dpll_consumed_update_seq;
static vdc_dpll_manager_observation_self_test_status_t s_observation_self_test;
static vdc_domain_context_t s_vdc_domain;
static tdma_service_service_t *s_vdc_tdma_service;
static bool s_vdc_tdma_registered;
static uint32_t s_vdc_tdma_self_test_frame_seq;
static vdc_tdma_timestamp_evidence_t s_vdc_tdma_self_test_evidence;
static uint32_t s_vdc_tdma_self_test_evidence_seq;
static uint32_t s_vdc_tdma_self_test_submitted_seq;
static bool s_vdc_ready;
static bool s_dpll_ready;
static uint32_t s_vdc_ring_observation_config_seq;
static uint32_t s_vdc_ring_observation_sequence;
static vdc_tdma_timestamp_evidence_t s_vdc_ring_pending_evidence;
static vdc_tdma_evidence_preparation_t s_vdc_ring_preparation;
static uint32_t s_vdc_ring_pending_config_seq;
static bool s_vdc_ring_evidence_pending;
static bool s_vdc_ring_preparation_pending;
static bool s_vdc_ring_finalization_pending;
static bool s_vdc_domain_service_pending;
static vdc_dpll_manager_ring_observer_status_t s_ring_observer_status;
static volatile uint32_t s_ring_observer_status_guard;
static vdc_dpll_manager_dpll_capture_record_t
    s_dpll_capture_records[VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES];
static bool s_dpll_capture_armed;
static bool s_dpll_capture_complete;
static uint32_t s_dpll_capture_count;
static uint32_t s_dpll_capture_dropped;
static uint32_t s_dpll_capture_first_update_seq;
static uint32_t s_dpll_capture_last_update_seq;
static uint32_t s_dpll_capture_start_ms;
static uint32_t s_dpll_capture_end_ms;
static vdc_dpll_manager_waveform_record_t s_waveform_buffers
    [VDC_DPLL_MANAGER_WAVEFORM_BUFFER_COUNT]
    [VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS];
static uint32_t s_waveform_buffer_count[VDC_DPLL_MANAGER_WAVEFORM_BUFFER_COUNT];
static uint32_t s_waveform_active_buffer;
static uint32_t s_waveform_pending_buffer;
static uint32_t s_waveform_pending_first_record;
static bool s_waveform_pending_valid;
static bool s_waveform_job_inflight;
static vdc_dpll_manager_waveform_capture_status_t s_waveform_status;
static uint32_t s_waveform_observed_mask;
static uint64_t s_phase_group_start_ns;
static uint32_t s_phase_edge_mask;
static uint64_t s_phase_edge_ns[4];
static bool s_phase_edge_seen[4];
static bool s_phase_has_complete_round;
static uint32_t s_phase_stable_span_min_ns;
static uint32_t s_phase_stable_span_max_ns;
static uint64_t s_phase_tx_not_before_ns;
static uint32_t s_phase_tx_scheduled_count;

typedef struct {
    vdc_tdma_schedule_profile_t schedule;
    vdc_servo_profile_t servo;
    vdc_dco_control_t dco;
    vdc_dpll_state_t dpll;
} vdc_dpll_manager_runtime_snapshot_t;

static void vdc_dpll_manager_publish_snapshot(
    const vdc_domain_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    (void)__atomic_add_fetch(&s_published_snapshot_guard,
                             1u,
                             __ATOMIC_ACQ_REL);
    s_published_snapshot = *snapshot;
    s_published_snapshot_valid = true;
    __atomic_store_n(&s_published_dpll_update_seq,
                     snapshot->dpll.update_seq,
                     __ATOMIC_RELEASE);
    (void)__atomic_add_fetch(&s_published_snapshot_guard,
                             1u,
                             __ATOMIC_RELEASE);
}

/* Schedule, servo and path matrices are immutable between configuration
 * activations.  Updating those large tables for every 4 ms clock sample made
 * the otherwise bounded VDC phase exceed its cycle budget.  The realtime
 * path publishes only fields changed by evidence/service; activation keeps
 * using the complete snapshot publisher above.  Both paths share one seqlock
 * and therefore preserve the existing reader contract. */
static void vdc_dpll_manager_publish_runtime_snapshot_locked(void)
{
    (void)__atomic_add_fetch(&s_published_snapshot_guard,
                             1u,
                             __ATOMIC_ACQ_REL);
    s_published_snapshot.ready = s_vdc_domain.ready;
    s_published_snapshot.service_count = s_vdc_domain.service_count;
    s_published_snapshot.first_service_time_ns =
        s_vdc_domain.first_service_time_ns;
    s_published_snapshot.last_service_time_ns =
        s_vdc_domain.last_service_time_ns;
    s_published_snapshot.clock = s_vdc_domain.clock;
    s_published_snapshot.dco = s_vdc_domain.dco;
    s_published_snapshot.dpll = s_vdc_domain.dpll;
    s_published_snapshot.quality = s_vdc_domain.quality;
    s_published_snapshot.error_budget = s_vdc_domain.error_budget;
    s_published_snapshot.gate = s_vdc_domain.gate;
    s_published_snapshot_valid = true;
    __atomic_store_n(&s_published_dpll_update_seq,
                     s_vdc_domain.dpll.update_seq,
                     __ATOMIC_RELEASE);
    (void)__atomic_add_fetch(&s_published_snapshot_guard,
                             1u,
                             __ATOMIC_RELEASE);

    /* This is deliberately a fixed-cost SRAM append.  No SD/FATFS call,
     * allocation, formatting, or diagnostic interpretation is allowed on
     * the Core1/DPLL path.  The record is frozen and persisted only after
     * the maintenance caller stops the capture. */
    if (s_dpll_capture_armed && s_vdc_domain.dpll.update_seq != 0u &&
        s_vdc_domain.dpll.update_seq != s_dpll_capture_last_update_seq) {
        if (s_dpll_capture_count <
            VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES) {
            const uint32_t now_ms = board_uptime_ms();
            vdc_dpll_manager_dpll_capture_record_t *record =
                &s_dpll_capture_records[s_dpll_capture_count];
            record->update_seq = s_vdc_domain.dpll.update_seq;
            record->timestamp_ms = now_ms;
            record->phase_error_ns = s_vdc_domain.dpll.last_phase_error_ns;
            record->frequency_error_ppb =
                s_vdc_domain.dpll.last_frequency_error_ppb;
            record->state_and_gate =
                (s_vdc_domain.dpll.state & 0xFFFFu) |
                ((s_vdc_domain.dpll.last_reject_code & 0xFFFFu) << 16u);
            if (s_dpll_capture_count == 0u) {
                s_dpll_capture_first_update_seq = record->update_seq;
                s_dpll_capture_start_ms = now_ms;
            }
            s_dpll_capture_count++;
            s_dpll_capture_last_update_seq = record->update_seq;
            s_dpll_capture_end_ms = now_ms;
            if (s_dpll_capture_count ==
                VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES) {
                s_dpll_capture_armed = false;
                s_dpll_capture_complete = true;
            }
        } else {
            s_dpll_capture_dropped++;
            s_dpll_capture_armed = false;
            s_dpll_capture_complete = true;
        }
    }
}

static bool vdc_dpll_manager_publish_domain_snapshot_locked(void)
{
    vdc_domain_snapshot_t snapshot;
    if (!vdc_domain_get_snapshot(&s_vdc_domain, &snapshot)) {
        return false;
    }
    vdc_dpll_manager_publish_snapshot(&snapshot);
    return true;
}

static bool vdc_dpll_manager_get_runtime_snapshot(
    vdc_dpll_manager_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_published_snapshot_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        snapshot->schedule = s_published_snapshot.schedule;
        snapshot->servo = s_published_snapshot.servo;
        snapshot->dco = s_published_snapshot.dco;
        snapshot->dpll = s_published_snapshot.dpll;
        const bool valid = s_published_snapshot_valid;
        const uint32_t end = __atomic_load_n(
            &s_published_snapshot_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            return valid;
        }
    }
    return false;
}

static void vdc_dpll_manager_publish_dpll_status(void)
{
    (void)__atomic_add_fetch(&s_published_dpll_status_guard,
                             1u,
                             __ATOMIC_ACQ_REL);
    s_published_dpll_status = s_dpll_status;
    s_published_dco_consumer_status = s_dco_consumer_status;
    (void)__atomic_add_fetch(&s_published_dpll_status_guard,
                             1u,
                             __ATOMIC_RELEASE);
}

static bool vdc_dpll_manager_finish_ring_observer_service(
    const vdc_dpll_manager_ring_observer_status_t *status,
    bool result)
{
    (void)__atomic_add_fetch(&s_ring_observer_status_guard,
                             1u,
                             __ATOMIC_ACQ_REL);
    s_ring_observer_status = *status;
    (void)__atomic_add_fetch(&s_ring_observer_status_guard,
                             1u,
                             __ATOMIC_RELEASE);
    return result;
}

static bool VDC_DPLL_MANAGER_TIME_CRITICAL(
    vdc_dpll_manager_ring_observer_service)(uint32_t *lock_state)
{
    if (lock_state == NULL) {
        return false;
    }
    *lock_state = s_vdc_domain.dpll.state;
    tdma_ring_clock_snapshot_t ring;
    vdc_dpll_manager_ring_observer_status_t status =
        s_ring_observer_status;
    status.service_count++;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) {
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_SNAPSHOT_UNAVAILABLE;
        return vdc_dpll_manager_finish_ring_observer_service(&status, false);
    }
    status.snapshot_count++;
    status.last_config_seq = ring.config_seq;
    if (ring.enabled == 0u || ring.adapter_started == 0u ||
        ring.clock_observation.valid == 0u) {
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_INACTIVE;
        return vdc_dpll_manager_finish_ring_observer_service(&status, false);
    }

    const tdma_ring_clock_observation_t *clock = &ring.clock_observation;
    if (s_vdc_ring_observation_config_seq == ring.config_seq &&
        clock->correlated_sequence == s_vdc_ring_observation_sequence) {
        status.last_sequence = clock->correlated_sequence;
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_DUPLICATE;
        return vdc_dpll_manager_finish_ring_observer_service(&status, false);
    }

    /* TDMA/VDC configuration is activated only while the ring is STOPPED.
     * Once running, core1 is the sole domain writer; a global OSAL spinlock
     * here would let core0 diagnostics block the realtime phase. */
    if (s_vdc_ring_observation_config_seq != ring.config_seq) {
        s_vdc_ring_observation_config_seq = ring.config_seq;
        s_vdc_ring_observation_sequence = 0u;
    }
    status.last_sequence = clock->correlated_sequence;
    if (clock->correlated_sequence == s_vdc_ring_observation_sequence) {
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_DUPLICATE;
        return vdc_dpll_manager_finish_ring_observer_service(&status, false);
    }
    if (clock->schedule_crc32 != ring.schedule_crc32 ||
        clock->schedule_crc32 != s_vdc_domain.schedule.schedule_crc32 ||
        clock->node_count != ring.node_count ||
        clock->source_node != ring.local_slot_id ||
        clock->reference_node != ring.reference_slot_id) {
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_IDENTITY_REJECTED;
        return vdc_dpll_manager_finish_ring_observer_service(&status, false);
    }
    status.eligible_count++;

    vdc_path_delay_entry_t path_entry;
    if (!vdc_domain_active_observation_path_delay_lookup(
            &s_vdc_domain.path_delay,
            clock->source_node,
            clock->reference_node,
            &path_entry)) {
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_PATH_REJECTED;
        return vdc_dpll_manager_finish_ring_observer_service(&status, false);
    }
    status.path_count++;

    const vdc_ring_observation_t observation = {
        .node_count = clock->node_count,
        .source_node = clock->source_node,
        .reference_node = clock->reference_node,
        .correlated_sequence = clock->correlated_sequence,
        .frame_crc32 = clock->frame_crc32,
        .schedule_crc32 = clock->schedule_crc32,
        .timestamp_resolution_ns = clock->timestamp_resolution_ns,
        .timestamp_flags = clock->timestamp_flags,
        .correlated_frame_evidence = clock->correlated_frame_evidence,
        .link_delay_ns = path_entry.delay_ns,
        .reference_tx_timestamp_ns = clock->reference_tx_timestamp_ns,
        .local_rx_timestamp_ns = clock->local_rx_timestamp_ns,
    };
    vdc_tdma_timestamp_evidence_t evidence;
    if (vdc_ring_observer_expand_active(&s_vdc_domain.schedule,
                                        &observation,
                                        &evidence)) {
        status.expand_count++;
        s_vdc_ring_observation_sequence = clock->correlated_sequence;
        if (s_vdc_ring_evidence_pending ||
            s_vdc_ring_preparation_pending ||
            s_vdc_ring_finalization_pending) {
            status.last_result =
                VDC_DPLL_MANAGER_RING_OBSERVER_DUPLICATE;
            return vdc_dpll_manager_finish_ring_observer_service(
                &status, false);
        }
        s_vdc_ring_pending_evidence = evidence;
        s_vdc_ring_pending_config_seq = ring.config_seq;
        s_vdc_ring_evidence_pending = true;
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_EVIDENCE_PENDING;
    } else {
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_EXPAND_REJECTED;
        return vdc_dpll_manager_finish_ring_observer_service(&status, false);
    }
    return vdc_dpll_manager_finish_ring_observer_service(&status, true);
}

static bool vdc_dpll_manager_prepare_ring_evidence(void)
{
    if (!s_vdc_ring_evidence_pending) {
        return false;
    }

    vdc_dpll_manager_ring_observer_status_t status = s_ring_observer_status;
    const bool configuration_matches =
        s_vdc_ring_pending_config_seq == s_vdc_ring_observation_config_seq;
    s_vdc_ring_evidence_pending = false;
    s_vdc_ring_pending_config_seq = 0u;

    status.submitted_count++;
    if (configuration_matches &&
        vdc_domain_prepare_active_tdma_evidence(
            &s_vdc_domain,
            &s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation)) {
        s_vdc_ring_preparation_pending = true;
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_EVIDENCE_PENDING;
    } else {
        status.rejected_count++;
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_SUBMIT_REJECTED;
    }
    return vdc_dpll_manager_finish_ring_observer_service(&status, true);
}

static bool vdc_dpll_manager_apply_ring_evidence(void)
{
    if (!s_vdc_ring_preparation_pending) {
        return false;
    }

    vdc_dpll_manager_ring_observer_status_t status = s_ring_observer_status;
    const bool applied = vdc_domain_apply_prepared_tdma_evidence_servo(
        &s_vdc_domain,
        &s_vdc_ring_pending_evidence,
        &s_vdc_ring_preparation);
    s_vdc_ring_preparation_pending = false;
    if (applied) {
        s_vdc_ring_finalization_pending = true;
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_EVIDENCE_PENDING;
    } else {
        memset(&s_vdc_ring_preparation, 0, sizeof(s_vdc_ring_preparation));
        status.rejected_count++;
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_SUBMIT_REJECTED;
    }
    return vdc_dpll_manager_finish_ring_observer_service(&status, true);
}

static bool vdc_dpll_manager_finalize_ring_evidence(void)
{
    if (!s_vdc_ring_finalization_pending) {
        return false;
    }

    vdc_dpll_manager_ring_observer_status_t status = s_ring_observer_status;
    bool accepted = false;
    const bool state_applied =
        vdc_domain_apply_prepared_tdma_evidence_state(
            &s_vdc_domain,
            &s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation,
            &accepted);
    const bool finalized = state_applied &&
        vdc_domain_finalize_prepared_tdma_evidence(
            &s_vdc_domain,
            &s_vdc_ring_pending_evidence,
            &s_vdc_ring_preparation);
    s_vdc_ring_finalization_pending = false;
    memset(&s_vdc_ring_preparation, 0, sizeof(s_vdc_ring_preparation));
    if (finalized && accepted) {
        status.accepted_count++;
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_SUBMIT_ACCEPTED;
    } else {
        status.rejected_count++;
        status.last_result =
            VDC_DPLL_MANAGER_RING_OBSERVER_SUBMIT_REJECTED;
    }
    s_vdc_domain_service_pending = finalized;
    return vdc_dpll_manager_finish_ring_observer_service(&status, true);
}

static bool vdc_dpll_manager_configure_sync_io_observer_tdma_mask(
    bool enabled,
    uint32_t observed_mask,
    uint32_t initial_sample_mask,
    uint32_t sample_period_ns,
    uint32_t frame_crc32,
    bool periodic,
    uint32_t start_delay_ns);
static uint64_t vdc_dpll_manager_now_ns(void);
static bool vdc_dpll_manager_compute_dco_phase_pulse_delay(
    uint64_t not_before_ns,
    uint32_t pulse_period_ns,
    uint32_t *delay_ns,
    uint64_t *target_local_ns);

static void vdc_dpll_manager_observation_self_test_service(void)
{
    vdc_dpll_manager_observation_self_test_status_t status;
    bool should_stop_rx = false;

    osal_critical_enter();
    status = s_observation_self_test;
    osal_critical_exit();

    if (!status.active ||
        status.started_ms == 0u) {
        return;
    }

    if (status.phase_only &&
        (status.role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX) != 0u &&
        (status.role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) == 0u) {
        if (sync_io_model_pulse_schedule_is_running()) {
            return;
        }
        const uint64_t now_ns = vdc_dpll_manager_now_ns();
        if (s_phase_tx_scheduled_count == 0u &&
            now_ns + status.pulse_period_ns < s_phase_tx_not_before_ns) {
            return;
        }

        uint32_t delay_ns = 0u;
        uint64_t target_ns = 0u;
        const uint64_t not_before_ns = s_phase_tx_scheduled_count == 0u
            ? s_phase_tx_not_before_ns : now_ns;
        if (!vdc_dpll_manager_compute_dco_phase_pulse_delay(
                not_before_ns, status.pulse_period_ns,
                &delay_ns, &target_ns) ||
            !sync_io_sma_observer_pulse_schedule_arm_periodic_ns(
                status.output_index, delay_ns, status.pulse_period_ns,
                status.pulse_high_ns, 1u, true, 100u)) {
            osal_critical_enter();
            if (s_observation_self_test.active &&
                s_observation_self_test.started_ms == status.started_ms) {
                s_observation_self_test.active = false;
                s_observation_self_test.last_error = 4u;
            }
            osal_critical_exit();
            return;
        }
        if (s_phase_tx_scheduled_count != UINT32_MAX) {
            s_phase_tx_scheduled_count++;
        }
        osal_critical_enter();
        if (s_observation_self_test.active &&
            s_observation_self_test.started_ms == status.started_ms) {
            s_observation_self_test.scheduled_pulse_count =
                s_phase_tx_scheduled_count;
            if (s_phase_tx_scheduled_count == 1u) {
                s_observation_self_test.first_window_start_ns = target_ns;
            }
        }
        osal_critical_exit();
        return;
    }

    if (status.phase_only &&
        (status.role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) != 0u &&
        (status.role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX) == 0u) {
        return;
    }

    if ((status.role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX) != 0u &&
        (status.role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) == 0u) {
        tdma_service_snapshot_t tdma;
        if (s_vdc_tdma_service != NULL &&
            tdma_service_get_snapshot(s_vdc_tdma_service, &tdma) &&
            s_vdc_tdma_self_test_evidence_seq != 0u &&
            tdma.traffic_scheduler_completed_seq[
                TDMA_TRAFFIC_VDC_REALTIME] ==
                s_vdc_tdma_self_test_evidence_seq) {
            const uint32_t completed_seq =
                tdma.traffic_scheduler_completed_seq[
                    TDMA_TRAFFIC_VDC_REALTIME];
            osal_critical_enter();
            if (s_vdc_tdma_self_test_submitted_seq != completed_seq) {
                vdc_tdma_timestamp_evidence_t evidence =
                    s_vdc_tdma_self_test_evidence;
                evidence.timestamp_source =
                    (vdc_domain_timestamp_source_t)
                        tdma.traffic_class_timestamp_source[
                            TDMA_TRAFFIC_VDC_REALTIME];
                evidence.timestamp_resolution_ns =
                    tdma.traffic_class_timestamp_resolution_ns[
                        TDMA_TRAFFIC_VDC_REALTIME];
                evidence.timestamp_flags =
                    tdma.traffic_class_timestamp_flags[
                        TDMA_TRAFFIC_VDC_REALTIME];
                (void)vdc_domain_submit_tdma_evidence(&s_vdc_domain,
                                                       &evidence);
                s_vdc_tdma_self_test_submitted_seq = completed_seq;
            }
            if (s_observation_self_test.active &&
                s_observation_self_test.started_ms == status.started_ms) {
                s_observation_self_test.active = false;
                const uint32_t vdc_result =
                    tdma.traffic_class_last_result[
                        TDMA_TRAFFIC_VDC_REALTIME];
                if (vdc_result != tdma_service_RESULT_FRAME_READY) {
                    s_observation_self_test.last_error = vdc_result;
                }
            }
            osal_critical_exit();
        }
        return;
    }

    if ((status.role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) == 0u) {
        return;
    }

    const uint64_t duration_ns =
        (uint64_t)status.start_delay_ns +
        (uint64_t)status.pulse_period_ns * (uint64_t)status.pulse_count;
    uint64_t timeout_ms = (duration_ns + 999999ull) / 1000000ull;
    timeout_ms += VDC_DPLL_MANAGER_SELF_TEST_CLEANUP_MARGIN_MS;
    if (timeout_ms > UINT32_MAX) {
        timeout_ms = UINT32_MAX;
    }

    const uint32_t elapsed_ms = board_uptime_ms() - status.started_ms;
    if (elapsed_ms >= (uint32_t)timeout_ms) {
        should_stop_rx = true;
    }

    if (!should_stop_rx) {
        return;
    }

    sync_io_stop_capture();
    sync_io_capture_disarm_timestamp_window();

    osal_critical_enter();
    if (s_observation_self_test.active &&
        s_observation_self_test.started_ms == status.started_ms) {
        s_observation_self_test.active = false;
    }
    osal_critical_exit();
}

static uint64_t vdc_dpll_manager_now_ns(void)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    return time_us_64() * 1000ull;
#else
    return (uint64_t)board_uptime_ms() * 1000000ull;
#endif
}

static bool vdc_dpll_manager_sync_io_observer_config_valid(
    const vdc_dpll_manager_sync_io_observer_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    if (!config->enabled) {
        return true;
    }
    if (config->max_words_per_service == 0u ||
        config->max_words_per_service >
            VDC_DPLL_MANAGER_SYNC_IO_MAX_BATCH_WORDS ||
        config->observed_mask == 0u ||
        (config->observed_mask & ~VDC_SYNC_IO_CAPTURE_SAMPLE_MASK) != 0u ||
        config->sample_period_ns == 0u ||
        config->frame_crc32 == 0u ||
        (config->rising_event_id == 0u && config->falling_event_id == 0u)) {
        return false;
    }
    if ((config->initial_sample_mask & ~VDC_SYNC_IO_CAPTURE_SAMPLE_MASK) != 0u) {
        return false;
    }
    return true;
}

static uint32_t vdc_dpll_manager_sample_hz_from_period_ns(uint32_t sample_period_ns)
{
    if (sample_period_ns == 0u) {
        return 0u;
    }
    const uint64_t sample_hz =
        (1000000000ull + (uint64_t)sample_period_ns - 1ull) /
        (uint64_t)sample_period_ns;
    return sample_hz > UINT32_MAX ? UINT32_MAX : (uint32_t)sample_hz;
}

static uint32_t vdc_dpll_manager_ns_to_ceil_us(uint32_t ns)
{
    return (ns + 999u) / 1000u;
}

static uint32_t vdc_dpll_manager_observer_window_width_ns(
    const vdc_domain_context_t *context,
    uint32_t scheduled_width_ns,
    uint32_t sample_period_ns)
{
    if (context == NULL ||
        context->dpll.state == VDC_DOMAIN_LOCK_LOCKED) {
        return scheduled_width_ns;
    }

    (void)sample_period_ns;
    if (context->schedule.period_ns == 0u) {
        return scheduled_width_ns;
    }

    return context->schedule.period_ns;
}

static bool vdc_dpll_manager_compute_first_pulse_delay(
    uint64_t first_window_start_ns,
    uint32_t window_offset_ns,
    uint32_t *first_delay_ns)
{
    if (first_delay_ns == NULL) {
        return false;
    }

    const uint64_t now_ns = vdc_dpll_manager_now_ns();
    const uint64_t target_ns =
        first_window_start_ns + (uint64_t)window_offset_ns;
    const uint64_t delay_ns = target_ns > now_ns ? target_ns - now_ns : 0u;
    if (delay_ns > UINT32_MAX) {
        return false;
    }
    *first_delay_ns = (uint32_t)delay_ns;
    return true;
}

static bool vdc_dpll_manager_dco_time_at_local_ns(
    const vdc_dco_control_t *dco,
    uint64_t local_ns,
    uint64_t *dco_ns)
{
    if (dco == NULL || dco_ns == NULL || dco->valid == 0u ||
        local_ns < dco->base_local_tick64) {
        return false;
    }
    const uint64_t delta = local_ns - dco->base_local_tick64;
    const int64_t rate =
        ((int64_t)delta * (int64_t)dco->period_adjust_ppb) / 1000000000ll;
    const int64_t adjust = rate + (int64_t)dco->phase_offset_ns;
    const uint64_t base = dco->base_vdc_time64_ns + delta;
    if (adjust < 0 && (uint64_t)(-adjust) > base) {
        return false;
    }
    *dco_ns = adjust < 0
        ? base - (uint64_t)(-adjust)
        : base + (uint64_t)adjust;
    return true;
}

static bool vdc_dpll_manager_compute_dco_phase_pulse_delay(
    uint64_t not_before_ns,
    uint32_t pulse_period_ns,
    uint32_t *delay_ns,
    uint64_t *target_local_ns)
{
    vdc_dpll_manager_runtime_snapshot_t snapshot;
    const uint64_t now_ns = vdc_dpll_manager_now_ns();
    const uint64_t minimum_local_ns = not_before_ns > now_ns + 1000u
        ? not_before_ns : now_ns + 1000u;
    uint64_t phase_time_ns = 0u;
    if (delay_ns == NULL || target_local_ns == NULL || pulse_period_ns == 0u ||
        !vdc_dpll_manager_get_runtime_snapshot(&snapshot) ||
        !vdc_dpll_manager_dco_time_at_local_ns(
            &snapshot.dco, minimum_local_ns, &phase_time_ns)) {
        return false;
    }

    const uint64_t period_ns = pulse_period_ns;
    const uint64_t target_phase_ns =
        ((phase_time_ns / period_ns) + 1u) * period_ns;
    uint64_t local_ns = minimum_local_ns + target_phase_ns - phase_time_ns;
    for (uint32_t iteration = 0u; iteration < 3u; iteration++) {
        uint64_t mapped_ns = 0u;
        if (!vdc_dpll_manager_dco_time_at_local_ns(
                &snapshot.dco, local_ns, &mapped_ns)) {
            return false;
        }
        const int64_t error_ns =
            (int64_t)target_phase_ns - (int64_t)mapped_ns;
        if (error_ns < 0 && (uint64_t)(-error_ns) > local_ns) {
            return false;
        }
        local_ns = error_ns < 0
            ? local_ns - (uint64_t)(-error_ns)
            : local_ns + (uint64_t)error_ns;
    }
    if (local_ns <= now_ns || local_ns - now_ns > UINT32_MAX) {
        return false;
    }
    *delay_ns = (uint32_t)(local_ns - now_ns);
    *target_local_ns = local_ns;
    return true;
}

static void vdc_dpll_manager_sync_io_observer_reset_status_locked(void)
{
    memset(&s_sync_io_observer_status, 0, sizeof(s_sync_io_observer_status));
    s_sync_io_observer_status.enabled = s_sync_io_observer_config.enabled;
    s_sync_io_observer_status.max_words_per_service =
        s_sync_io_observer_config.max_words_per_service;
    s_sync_io_observer_status.previous_sample_mask =
        s_sync_io_observer_config.initial_sample_mask &
        s_sync_io_observer_config.observed_mask;
    s_sync_io_observer_status.next_base_time_l32_ns =
        s_sync_io_observer_config.next_base_time_l32_ns;
    s_sync_io_observer_status.phase_max_span_ns =
        s_sync_io_observer_config.phase_max_span_ns;
    s_sync_io_observer_status.phase_min_stable_rounds =
        s_sync_io_observer_config.phase_min_stable_rounds;
    s_phase_group_start_ns = 0u;
    s_phase_edge_mask = 0u;
    memset(s_phase_edge_ns, 0, sizeof(s_phase_edge_ns));
    memset(s_phase_edge_seen, 0, sizeof(s_phase_edge_seen));
    s_phase_has_complete_round = false;
    s_phase_stable_span_min_ns = UINT32_MAX;
    s_phase_stable_span_max_ns = 0u;
}

static void vdc_dpll_manager_publish_sync_io_observer_locked(void)
{
    vdc_dpll_manager_sync_io_observer_status_t status =
        s_sync_io_observer_status;

    status.enabled = s_sync_io_observer_config.enabled;
    status.max_words_per_service =
        s_sync_io_observer_config.max_words_per_service;
    if (s_sync_io_observer_config.enabled) {
        status.rising_event_id = s_sync_io_observer_config.rising_event_id;
        status.falling_event_id = s_sync_io_observer_config.falling_event_id;
        status.observed_mask = s_sync_io_observer_config.observed_mask;
        status.initial_sample_mask =
            s_sync_io_observer_config.initial_sample_mask;
        status.sample_period_ns =
            s_sync_io_observer_config.sample_period_ns;
        status.expected_window_start_lo =
            (uint32_t)(s_sync_io_observer_config.expected_window_start_ns &
                       0xFFFFFFFFull);
        status.expected_window_start_hi =
            (uint32_t)(s_sync_io_observer_config.expected_window_start_ns >>
                       32u);
        status.frame_crc32 = s_sync_io_observer_config.frame_crc32;
        status.max_backward_ticks =
            s_sync_io_observer_config.max_backward_ticks;
        status.quality_flags = s_sync_io_observer_config.quality_flags;
        status.sample0_lsb =
            s_sync_io_observer_config.sample0_lsb ? 1u : 0u;
        status.schedule_crc32 = s_vdc_domain.schedule.schedule_crc32;
        status.dictionary_crc32 =
            s_vdc_domain.timestamp_dictionary.dictionary_crc32;
        status.dictionary_entry_count =
            s_vdc_domain.timestamp_dictionary.entry_count;
        status.dictionary_profile_crc32 =
            s_vdc_domain.timestamp_dictionary.profile_crc32;
    }

    s_published_sync_io_observer_status = status;
}

static uint32_t vdc_dpll_manager_phase_sample_at(uint32_t raw_word,
                                                  uint32_t sample_index,
                                                  bool sample0_lsb)
{
    const uint32_t index = sample0_lsb ? sample_index : 7u - sample_index;
    return (raw_word >> (index * 4u)) & 0x0Fu;
}

static void vdc_dpll_manager_phase_finalize(
    vdc_dpll_manager_sync_io_observer_status_t *status,
    const vdc_dpll_manager_sync_io_observer_config_t *config)
{
    if (status == NULL || config == NULL || s_phase_group_start_ns == 0u) {
        return;
    }

    status->phase_round_count++;
    status->phase_last_edge_mask = s_phase_edge_mask;
    status->phase_last_window_start_lo =
        (uint32_t)(s_phase_group_start_ns & 0xFFFFFFFFull);
    status->phase_last_window_start_hi =
        (uint32_t)(s_phase_group_start_ns >> 32u);
    if (s_phase_edge_mask != 0x0Fu) {
        status->phase_missing_count++;
        return;
    }

    const uint64_t period_ns = config->phase_only &&
                                       config->max_backward_ticks != 0u
        ? (uint64_t)config->max_backward_ticks
        : 0u;
    if (period_ns == 0u) {
        status->phase_ambiguous_count++;
        return;
    }

    /* Treat each window as a circular phase interval.  A pulse just before
     * the period boundary and one just after it are near each other in phase,
     * even though their absolute timestamps are far apart. */
    uint64_t phase_ns[4];
    uint64_t ordered_phase_ns[4];
    for (uint32_t channel = 0u; channel < 4u; channel++) {
        phase_ns[channel] = s_phase_edge_ns[channel] % period_ns;
        ordered_phase_ns[channel] = phase_ns[channel];
    }
    for (uint32_t i = 1u; i < 4u; i++) {
        const uint64_t value = ordered_phase_ns[i];
        uint32_t j = i;
        while (j > 0u && ordered_phase_ns[j - 1u] > value) {
            ordered_phase_ns[j] = ordered_phase_ns[j - 1u];
            j--;
        }
        ordered_phase_ns[j] = value;
    }
    uint64_t largest_gap =
        period_ns - ordered_phase_ns[3] + ordered_phase_ns[0];
    for (uint32_t i = 0u; i < 3u; i++) {
        const uint64_t gap = ordered_phase_ns[i + 1u] - ordered_phase_ns[i];
        if (gap > largest_gap) {
            largest_gap = gap;
        }
    }
    const uint32_t span_ns = (uint32_t)(period_ns - largest_gap);
    const uint64_t reference = phase_ns[0];
    for (uint32_t channel = 0u; channel < 4u; channel++) {
        int64_t offset = (int64_t)(phase_ns[channel] >= reference
            ? phase_ns[channel] - reference
            : period_ns - reference + phase_ns[channel]);
        if ((uint64_t)offset > period_ns / 2u) {
            offset -= (int64_t)period_ns;
        }
        status->phase_last_offset_ns[channel] = (int32_t)offset;
    }
    status->phase_last_span_ns = span_ns;
    status->phase_complete_count++;
    if (!s_phase_has_complete_round) {
        s_phase_has_complete_round = true;
        status->phase_initial_span_ns = span_ns;
        memcpy(status->phase_initial_offset_ns,
               status->phase_last_offset_ns,
               sizeof(status->phase_initial_offset_ns));
        status->phase_peak_span_ns = span_ns;
        status->phase_min_span_ns = span_ns;
    } else {
        if (span_ns > status->phase_peak_span_ns) {
            status->phase_peak_span_ns = span_ns;
        }
        if (span_ns < status->phase_min_span_ns) {
            status->phase_min_span_ns = span_ns;
        }
    }
    status->phase_last_span_ns = span_ns;
    const bool stable = config->phase_max_span_ns != 0u &&
                        span_ns <= config->phase_max_span_ns;
    if (stable) {
        status->phase_stable_round_count++;
        status->phase_stable_streak++;
        if (status->phase_stable_streak > status->phase_max_stable_streak) {
            status->phase_max_stable_streak = status->phase_stable_streak;
        }
        if (status->phase_first_stable_round == 0u) {
            status->phase_first_stable_round = status->phase_round_count;
        }
        if (span_ns < s_phase_stable_span_min_ns) {
            s_phase_stable_span_min_ns = span_ns;
        }
        if (span_ns > s_phase_stable_span_max_ns) {
            s_phase_stable_span_max_ns = span_ns;
        }
        status->phase_stable_jitter_ns =
            s_phase_stable_span_max_ns - s_phase_stable_span_min_ns;
        if (status->phase_stable_streak >= config->phase_min_stable_rounds) {
            status->phase_converged = 1u;
        }
    } else {
        status->phase_stable_streak = 0u;
        status->phase_converged = 0u;
        status->phase_stable_jitter_ns = 0u;
        s_phase_stable_span_min_ns = UINT32_MAX;
        s_phase_stable_span_max_ns = 0u;
    }
}

static void vdc_dpll_manager_phase_reset(uint64_t window_start_ns)
{
    s_phase_group_start_ns = window_start_ns;
    s_phase_edge_mask = 0u;
    memset(s_phase_edge_ns, 0, sizeof(s_phase_edge_ns));
    memset(s_phase_edge_seen, 0, sizeof(s_phase_edge_seen));
}

static uint64_t vdc_dpll_manager_phase_expand_tick(
    const sync_io_capture_latched_word_t *word,
    uint32_t tick_l32)
{
    const uint64_t reference = word->matched_window_start_ns;
    uint64_t expanded = (reference & 0xFFFFFFFF00000000ull) |
                        (uint64_t)tick_l32;
    if (expanded + 0x80000000ull < reference) {
        expanded += 0x100000000ull;
    } else if (expanded > reference + 0x80000000ull) {
        expanded -= 0x100000000ull;
    }
    return expanded;
}

static void vdc_dpll_manager_phase_observe_word(
    vdc_dpll_manager_sync_io_observer_status_t *status,
    const vdc_dpll_manager_sync_io_observer_config_t *config,
    const sync_io_capture_latched_word_t *word)
{
    if (status == NULL || config == NULL || word == NULL ||
        !config->phase_only ||
        (word->timestamp_flags & VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE) == 0u) {
        return;
    }

    uint32_t previous = word->previous_sample_mask & config->observed_mask;
    for (uint32_t i = 0u; i < VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD; i++) {
        const uint32_t current =
            vdc_dpll_manager_phase_sample_at(word->raw_word, i,
                                             config->sample0_lsb) &
            config->observed_mask;
        const uint32_t rising = current & ~previous;
        for (uint32_t channel = 0u; channel < 4u; channel++) {
            const uint32_t bit = 1u << channel;
            if ((rising & bit) == 0u) {
                continue;
            }
            const uint32_t tick_l32 =
                word->base_time_l32_ns + i * word->sample_period_ns;
            const uint64_t tick_ns =
                vdc_dpll_manager_phase_expand_tick(word, tick_l32);
            const uint64_t group_limit_ns =
                s_phase_group_start_ns +
                (uint64_t)config->max_backward_ticks +
                (uint64_t)word->sample_period_ns *
                    VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD;
            if (s_phase_group_start_ns != 0u &&
                s_phase_edge_mask != 0x0Fu && tick_ns > group_limit_ns) {
                if (s_phase_has_complete_round) {
                    vdc_dpll_manager_phase_finalize(status, config);
                }
                vdc_dpll_manager_phase_reset(tick_ns);
            } else if (s_phase_group_start_ns == 0u) {
                vdc_dpll_manager_phase_reset(tick_ns);
            }
            if (s_phase_edge_seen[channel]) {
                if (s_phase_has_complete_round) {
                    status->phase_ambiguous_count++;
                    vdc_dpll_manager_phase_finalize(status, config);
                }
                vdc_dpll_manager_phase_reset(tick_ns);
            }
            s_phase_edge_seen[channel] = true;
            s_phase_edge_mask |= bit;
            s_phase_edge_ns[channel] = tick_ns;
            if (s_phase_edge_mask == 0x0Fu) {
                vdc_dpll_manager_phase_finalize(status, config);
                vdc_dpll_manager_phase_reset(0u);
            }
        }
        previous = current;
    }
    if (word->dropped_before != 0u) {
        status->phase_dropped_word_count = word->dropped_before;
    }
}

static void vdc_dpll_manager_sync_io_observer_service(void)
{
    sync_io_capture_latched_word_t words[VDC_DPLL_MANAGER_SYNC_IO_MAX_BATCH_WORDS];
    size_t count = 0u;
    vdc_dpll_manager_sync_io_observer_config_t config;

    osal_critical_enter();
    config = s_sync_io_observer_config;
    osal_critical_exit();

    if (!config.enabled || config.max_words_per_service == 0u) {
        return;
    }

    count = sync_io_read_capture_latched(words, config.max_words_per_service);
    if (count == 0u) {
        return;
    }

    osal_critical_enter();
    vdc_dpll_manager_sync_io_observer_status_t status =
        s_sync_io_observer_status;
    status.service_count++;
    osal_critical_exit();

    for (size_t i = 0u; i < count; i++) {
        vdc_compact_observation_sample_t compact;
        uint32_t last_sample_mask = status.previous_sample_mask;
        const uint32_t sample_seq = words[i].sample_seq;
        vdc_sync_io_capture_decode_config_t decode = {
            .valid = 1u,
            .sample_seq = sample_seq,
            .rising_event_id = config.rising_event_id,
            .falling_event_id = config.falling_event_id,
            .observed_mask = config.observed_mask,
            .previous_sample_mask =
                words[i].previous_sample_mask & config.observed_mask,
            .base_time_l32_ns =
                (config.quality_flags &
                 VDC_DPLL_MANAGER_OBSERVER_QUALITY_TDMA_WINDOW_BASE) != 0u &&
                        (words[i].timestamp_flags &
                         VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE) == 0u
                    ? (uint32_t)(config.expected_window_start_ns &
                                 0xFFFFFFFFull)
                    : words[i].base_time_l32_ns,
            .sample_period_ns = words[i].sample_period_ns != 0u
                                    ? words[i].sample_period_ns
                                    : config.sample_period_ns,
            .expected_window_start_ns = config.expected_window_start_ns,
            .frame_crc32 = config.frame_crc32,
            .max_backward_ticks = config.max_backward_ticks,
            .quality_flags = config.quality_flags,
            .timestamp_source = words[i].timestamp_source,
            .timestamp_resolution_ns = words[i].timestamp_resolution_ns,
            .timestamp_flags = words[i].timestamp_flags,
            .sample0_lsb = config.sample0_lsb,
        };
        if ((words[i].timestamp_flags &
             VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0u) {
            decode.expected_window_start_ns =
                words[i].matched_window_start_ns;
        }
        vdc_dpll_manager_phase_observe_word(&status, &config, &words[i]);

        if (s_waveform_status.armed) {
            uint32_t active_count =
                s_waveform_buffer_count[s_waveform_active_buffer];
            if (active_count >=
                    VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS &&
                !s_waveform_pending_valid) {
                s_waveform_pending_buffer = s_waveform_active_buffer;
                s_waveform_pending_first_record =
                    s_waveform_status.record_count - active_count;
                s_waveform_pending_valid = true;
                s_waveform_active_buffer ^= 1u;
                s_waveform_buffer_count[s_waveform_active_buffer] = 0u;
                active_count = 0u;
            }
            if (active_count <
                    VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS) {
                vdc_dpll_manager_waveform_record_t *record =
                    &s_waveform_buffers[s_waveform_active_buffer][active_count];
                record->raw_word = words[i].raw_word;
                record->sample_seq = words[i].sample_seq;
                record->previous_sample_mask = words[i].previous_sample_mask;
                record->base_time_l32_ns = words[i].base_time_l32_ns;
                record->matched_window_start_ns =
                    words[i].matched_window_start_ns;
                record->sample_period_ns = words[i].sample_period_ns;
                record->timestamp_source = words[i].timestamp_source;
                record->timestamp_resolution_ns =
                    words[i].timestamp_resolution_ns;
                record->timestamp_flags = words[i].timestamp_flags;
                record->dropped_before = words[i].dropped_before;
                s_waveform_buffer_count[s_waveform_active_buffer] =
                    active_count + 1u;
                if (s_waveform_status.record_count == 0u) {
                    s_waveform_status.first_sample_seq = words[i].sample_seq;
                }
                s_waveform_status.record_count++;
                s_waveform_status.last_sample_seq = words[i].sample_seq;
                s_waveform_status.end_ms = board_uptime_ms();
                s_waveform_observed_mask = config.observed_mask;
            } else {
                s_waveform_status.dropped_count++;
            }
        }

        if (config.phase_only) {
            status.raw_word_count++;
            status.accepted_count++;
            status.last_capture_result = VDC_SYNC_IO_CAPTURE_OK;
            status.last_raw_word = words[i].raw_word;
            status.last_sample_seq = sample_seq;
            status.previous_sample_mask = last_sample_mask & config.observed_mask;
            continue;
        }

        const vdc_sync_io_capture_result_t result =
            vdc_sync_io_capture_word_to_compact_observation(&decode,
                                                            words[i].raw_word,
                                                            &compact,
                                                            &last_sample_mask);

        status.raw_word_count++;
        status.last_capture_result = (uint32_t)result;
        status.last_raw_word = words[i].raw_word;
        status.last_sample_seq = sample_seq;
        status.previous_sample_mask =
            last_sample_mask & config.observed_mask;
        status.next_base_time_l32_ns =
            words[i].base_time_l32_ns +
            decode.sample_period_ns * VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD;

        switch (result) {
        case VDC_SYNC_IO_CAPTURE_OK:
            {
                vdc_timestamp_dictionary_entry_t entry;
                status.last_edge_index =
                    (compact.quality_flags >> 16u) & 0xFFu;
                status.last_timestamp_source =
                    words[i].timestamp_source;
                status.last_timestamp_resolution_ns =
                    words[i].timestamp_resolution_ns;
                status.last_timestamp_flags =
                    words[i].timestamp_flags;
                osal_critical_enter();
                if (vdc_timestamp_dictionary_find(
                        &s_vdc_domain.timestamp_dictionary,
                        compact.event_id,
                        &entry)) {
                    status.last_source_slot_id =
                        entry.source_slot_id;
                    status.last_reference_slot_id =
                        entry.reference_slot_id;
                    status.last_payload_class =
                        entry.payload_class;
                } else {
                    status.last_source_slot_id = 0u;
                    status.last_reference_slot_id = 0u;
                    status.last_payload_class = 0u;
                }
            }
            status.submitted_count++;
            status.last_event_id = compact.event_id;
            status.last_tick_l32 = compact.tick_l32;
            if (vdc_domain_submit_compact_observation(&s_vdc_domain, &compact)) {
                status.accepted_count++;
                status.last_gate_reject_code =
                    VDC_DOMAIN_GATE_PASS;
            } else {
                status.rejected_count++;
                status.last_gate_reject_code =
                    s_vdc_domain.gate.reject_code;
            }
            osal_critical_exit();
            break;
        case VDC_SYNC_IO_CAPTURE_NO_EDGE:
            status.no_edge_count++;
            break;
        case VDC_SYNC_IO_CAPTURE_AMBIGUOUS_EDGE:
            status.ambiguous_edge_count++;
            if (config.phase_only) {
                status.phase_ambiguous_count++;
            }
            break;
        case VDC_SYNC_IO_CAPTURE_BAD_ARGUMENT:
        default:
            status.bad_argument_count++;
            break;
        }
    }

    osal_critical_enter();
    s_sync_io_observer_status = status;
    vdc_dpll_manager_publish_sync_io_observer_locked();
    osal_critical_exit();
}

bool vdc_dpll_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    memset(&s_vdc_status, 0, sizeof(s_vdc_status));
    memset(&s_dpll_status, 0, sizeof(s_dpll_status));
    memset(&s_sync_io_observer_config, 0, sizeof(s_sync_io_observer_config));
    memset(&s_sync_io_observer_status, 0, sizeof(s_sync_io_observer_status));
    memset(&s_published_vdc_status, 0, sizeof(s_published_vdc_status));
    memset(&s_published_dpll_status, 0, sizeof(s_published_dpll_status));
    memset(&s_dco_consumer_status, 0, sizeof(s_dco_consumer_status));
    memset(&s_published_dco_consumer_status,
           0,
           sizeof(s_published_dco_consumer_status));
    s_published_dpll_status_guard = 0u;
    memset(&s_published_sync_io_observer_status,
           0,
           sizeof(s_published_sync_io_observer_status));
    s_published_snapshot_guard = 0u;
    memset(&s_published_snapshot, 0, sizeof(s_published_snapshot));
    s_published_dpll_update_seq = 0u;
    s_dpll_consumed_update_seq = 0u;
    memset(&s_observation_self_test, 0, sizeof(s_observation_self_test));
    s_vdc_tdma_service = NULL;
    memset(&s_vdc_tdma_self_test_evidence,
           0,
           sizeof(s_vdc_tdma_self_test_evidence));
    s_vdc_tdma_self_test_frame_seq = 0u;
    s_vdc_tdma_self_test_evidence_seq = 0u;
    s_vdc_tdma_self_test_submitted_seq = 0u;
    s_vdc_ring_observation_config_seq = 0u;
    s_vdc_ring_observation_sequence = 0u;
    memset(&s_vdc_ring_pending_evidence,
           0,
           sizeof(s_vdc_ring_pending_evidence));
    memset(&s_vdc_ring_preparation, 0, sizeof(s_vdc_ring_preparation));
    s_vdc_ring_pending_config_seq = 0u;
    s_vdc_ring_evidence_pending = false;
    s_vdc_ring_preparation_pending = false;
    s_vdc_ring_finalization_pending = false;
    s_vdc_domain_service_pending = false;
    memset(&s_ring_observer_status, 0, sizeof(s_ring_observer_status));
    memset(s_dpll_capture_records, 0, sizeof(s_dpll_capture_records));
    s_dpll_capture_armed = false;
    s_dpll_capture_complete = false;
    s_dpll_capture_count = 0u;
    s_dpll_capture_dropped = 0u;
    s_dpll_capture_first_update_seq = 0u;
    s_dpll_capture_last_update_seq = 0u;
    s_dpll_capture_start_ms = 0u;
    s_dpll_capture_end_ms = 0u;
    memset(s_waveform_buffers, 0, sizeof(s_waveform_buffers));
    memset(s_waveform_buffer_count, 0, sizeof(s_waveform_buffer_count));
    s_waveform_active_buffer = 0u;
    s_waveform_pending_buffer = 0u;
    s_waveform_pending_first_record = 0u;
    s_waveform_pending_valid = false;
    s_waveform_job_inflight = false;
    memset(&s_waveform_status, 0, sizeof(s_waveform_status));
    s_waveform_observed_mask = 0u;
    s_phase_group_start_ns = 0u;
    s_phase_edge_mask = 0u;
    memset(s_phase_edge_ns, 0, sizeof(s_phase_edge_ns));
    memset(s_phase_edge_seen, 0, sizeof(s_phase_edge_seen));
    s_phase_has_complete_round = false;
    s_phase_stable_span_min_ns = UINT32_MAX;
    s_phase_stable_span_max_ns = 0u;
    s_phase_tx_not_before_ns = 0u;
    s_phase_tx_scheduled_count = 0u;
    if (!vdc_domain_init(&s_vdc_domain)) {
        return false;
    }
    s_vdc_tdma_service = tdma_runtime_owner_get();
    if (s_vdc_tdma_service == NULL ||
        !vdc_tdma_payload_register(s_vdc_tdma_service)) {
        return false;
    }
    s_vdc_tdma_registered = true;
    s_vdc_status.last_service_ms = now_ms;
    s_dpll_status.last_service_ms = now_ms;
    s_published_vdc_status = s_vdc_status;
    s_published_dpll_status = s_dpll_status;
    s_published_snapshot_valid = false;
    s_vdc_ready = false;
    s_dpll_ready = false;
    return true;
}

void vdc_dpll_manager_set_vdc_ready(bool ready)
{
    osal_critical_enter();
    s_vdc_ready = ready;
    s_vdc_status.ready = ready;
    vdc_domain_set_ready(&s_vdc_domain, ready);
    s_published_vdc_status = s_vdc_status;
    osal_critical_exit();
}

void vdc_dpll_manager_set_dpll_ready(bool ready)
{
    osal_critical_enter();
    s_dpll_ready = ready;
    s_dpll_status.ready = ready;
    vdc_dpll_manager_publish_dpll_status();
    osal_critical_exit();
}

void VDC_DPLL_MANAGER_TIME_CRITICAL(vdc_sync_ao_service)(void)
{
    const uint32_t now_ms = board_uptime_ms();
    uint32_t lock_state = VDC_DOMAIN_LOCK_OFF;

    if (!vdc_dpll_manager_ring_observer_service(&lock_state)) {
        return;
    }
    if (s_vdc_status.service_count == 0u) {
        s_vdc_status.first_service_ms = now_ms;
    }
    s_vdc_status.service_count++;
    s_vdc_status.last_service_ms = now_ms;
    s_vdc_status.ready = s_vdc_ready;
    s_vdc_status.lock_state = lock_state;
    s_vdc_status.sync_seq++;
    s_published_vdc_status = s_vdc_status;
}

void vdc_dpll_manager_vdc_service(void)
{
    vdc_sync_ao_service();
}

bool vdc_dpll_manager_configure_sync_io_observer(
    const vdc_dpll_manager_sync_io_observer_config_t *config)
{
    if (!vdc_dpll_manager_sync_io_observer_config_valid(config)) {
        return false;
    }
    if (!config->enabled) {
        sync_io_capture_disarm_timestamp_window();
    }

    osal_critical_enter();
    s_sync_io_observer_config = *config;
    vdc_dpll_manager_sync_io_observer_reset_status_locked();
    vdc_dpll_manager_publish_sync_io_observer_locked();
    osal_critical_exit();
    return true;
}

bool vdc_dpll_manager_configure_sync_io_observer_tdma(
    bool enabled,
    uint32_t initial_sample_mask,
    uint32_t sample_period_ns,
    uint32_t frame_crc32)
{
    return vdc_dpll_manager_configure_sync_io_observer_tdma_mask(enabled,
                                                                 1u,
                                                                 initial_sample_mask,
                                                                 sample_period_ns,
                                                                 frame_crc32,
                                                                 false,
                                                                 0u);
}

static bool vdc_dpll_manager_configure_sync_io_observer_tdma_mask(
    bool enabled,
    uint32_t observed_mask,
    uint32_t initial_sample_mask,
    uint32_t sample_period_ns,
    uint32_t frame_crc32,
    bool periodic,
    uint32_t start_delay_ns)
{
    vdc_dpll_manager_sync_io_observer_config_t config = {0};
    vdc_tdma_window_plan_t plan;
    vdc_gate_result_t gate;
    uint64_t now_ns = vdc_dpll_manager_now_ns();
    const uint32_t sanitized_sample_period_ns =
        sample_period_ns != 0u ? sample_period_ns : 1000u;

    if (!enabled) {
        sync_io_capture_disarm_timestamp_window();
        config.enabled = false;
        return vdc_dpll_manager_configure_sync_io_observer(&config);
    }

    if (!vdc_dpll_manager_plan_tdma_window(
            VDC_DOMAIN_WINDOW_VDC_OBSERVATION,
            now_ns,
            &plan,
            &gate)) {
        return false;
    }

    const uint64_t window_start_ns =
        plan.window_start_ns + (uint64_t)start_delay_ns;
    const uint32_t scheduled_width_ns =
        (uint32_t)(plan.window_end_ns - plan.window_start_ns);
    const uint32_t window_width_ns =
        vdc_dpll_manager_observer_window_width_ns(&s_vdc_domain,
                                                  scheduled_width_ns,
                                                  sanitized_sample_period_ns);

    config.enabled = true;
    config.max_words_per_service = VDC_DPLL_MANAGER_SYNC_IO_MAX_BATCH_WORDS;
    config.rising_event_id = 1u;
    config.falling_event_id = 2u;
    config.observed_mask = observed_mask;
    config.initial_sample_mask = initial_sample_mask;
    config.next_base_time_l32_ns =
        (uint32_t)(window_start_ns & 0xFFFFFFFFull);
    config.sample_period_ns = sanitized_sample_period_ns;
    config.expected_window_start_ns = window_start_ns;
    config.frame_crc32 = frame_crc32 != 0u ? frame_crc32 : plan.schedule_crc32;
    config.max_backward_ticks = s_vdc_domain.schedule.period_ns;
    config.quality_flags =
        VDC_DPLL_MANAGER_OBSERVER_QUALITY_TDMA_WINDOW_BASE;

    if (!vdc_dpll_manager_configure_sync_io_observer(&config)) {
        return false;
    }

    osal_critical_enter();
    vdc_wrap_tracker_init(&s_vdc_domain.wrap_tracker,
                          (uint32_t)(window_start_ns & 0xFFFFFFFFull));
    s_vdc_domain.wrap_tracker.tick_hi64 =
        window_start_ns & 0xFFFFFFFF00000000ull;
    osal_critical_exit();

    const bool armed = periodic
        ? sync_io_capture_arm_periodic_timestamp_window(
              window_start_ns,
              window_width_ns,
              s_vdc_domain.schedule.period_ns,
              sanitized_sample_period_ns,
              config.observed_mask,
              initial_sample_mask)
        : sync_io_capture_arm_timestamp_window(
              window_start_ns,
              window_width_ns,
              sanitized_sample_period_ns,
              config.observed_mask,
              initial_sample_mask);
    if (!armed) {
        sync_io_capture_disarm_timestamp_window();
        (void)vdc_dpll_manager_configure_sync_io_observer(
            &(vdc_dpll_manager_sync_io_observer_config_t){0});
        return false;
    }

    return true;
}

bool vdc_dpll_manager_start_observation_self_test(
    const vdc_dpll_manager_observation_self_test_config_t *config)
{
    vdc_tdma_window_plan_t plan;
    vdc_gate_result_t gate;
    uint64_t now_ns = vdc_dpll_manager_now_ns();
    vdc_dpll_manager_observation_self_test_status_t status = {0};
    bool plan_available;

    if (config != NULL && config->phase_only &&
        config->role == VDC_DPLL_MANAGER_SELF_TEST_ROLE_NONE) {
        vdc_dpll_manager_observation_self_test_status_t previous;
        osal_critical_enter();
        previous = s_observation_self_test;
        osal_critical_exit();
        if (previous.phase_only &&
            (previous.role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX) != 0u) {
            sync_io_model_pulse_schedule_disarm();
        }
        if (previous.phase_only &&
            (previous.role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) != 0u) {
            sync_io_stop_capture();
            sync_io_capture_disarm_timestamp_window();
            (void)vdc_dpll_manager_configure_sync_io_observer(
                &(vdc_dpll_manager_sync_io_observer_config_t){0});
        }
        s_phase_tx_not_before_ns = 0u;
        s_phase_tx_scheduled_count = 0u;
        osal_critical_enter();
        memset(&s_observation_self_test, 0,
               sizeof(s_observation_self_test));
        osal_critical_exit();
        return true;
    }

    if (config == NULL ||
        config->role == VDC_DPLL_MANAGER_SELF_TEST_ROLE_NONE ||
        config->role > VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX_RX ||
        config->sample_period_ns == 0u ||
        config->observed_mask == 0u ||
        (config->observed_mask & ~VDC_SYNC_IO_CAPTURE_SAMPLE_MASK) != 0u ||
        config->pulse_count > VDC_DPLL_MANAGER_SELF_TEST_MAX_PULSES) {
        return false;
    }

    const uint32_t sample_hz =
        vdc_dpll_manager_sample_hz_from_period_ns(config->sample_period_ns);
    const uint32_t pulse_period_ns =
        config->pulse_period_ns != 0u ? config->pulse_period_ns : 2000u;
    const uint32_t pulse_high_ns =
        config->pulse_high_ns != 0u ? config->pulse_high_ns : 1000u;
    const uint32_t pulse_count =
        config->pulse_count != 0u ? config->pulse_count : 256u;
    const uint32_t phase_max_span_ns =
        config->phase_max_span_ns != 0u ? config->phase_max_span_ns : 500u;
    const uint32_t phase_min_stable_rounds =
        config->phase_min_stable_rounds != 0u
            ? config->phase_min_stable_rounds : 3u;
    if (sample_hz == 0u ||
        pulse_high_ns == 0u ||
        pulse_high_ns >= pulse_period_ns ||
        pulse_count > VDC_DPLL_MANAGER_SELF_TEST_MAX_PULSES) {
        return false;
    }

    plan_available = vdc_dpll_manager_plan_tdma_window(
        VDC_DOMAIN_WINDOW_VDC_OBSERVATION, now_ns, &plan, &gate);
    if (!plan_available && !config->phase_only) {
        return false;
    }
    if (!plan_available) {
        memset(&plan, 0, sizeof(plan));
    }

    status.active = true;
    status.role = config->role;
    status.output_index = config->output_index;
    status.observed_mask = config->observed_mask;
    status.initial_sample_mask = config->initial_sample_mask;
    status.sample_period_ns = config->sample_period_ns;
    status.pulse_period_ns = pulse_period_ns;
    status.pulse_high_ns = pulse_high_ns;
    status.pulse_count = pulse_count;
    status.frame_crc32 =
        config->frame_crc32 != 0u ? config->frame_crc32 : plan.schedule_crc32;
    status.schedule_crc32 = plan.schedule_crc32;
    status.started_ms = board_uptime_ms();
    status.start_delay_ns = config->start_delay_ns;
    status.first_window_start_ns = now_ns + (uint64_t)config->start_delay_ns;
    status.phase_only = config->phase_only;
    status.phase_max_span_ns = phase_max_span_ns;
    status.phase_min_stable_rounds = phase_min_stable_rounds;

    /* The external phase-evidence persona is intentionally independent of
     * TDMA admission and DPLL lock state.  TX uses PIO0/SM1; RX uses the
     * existing PIO0/SM0 four-bit sampler. */
    if (config->phase_only) {
        if (config->role == VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX_RX) {
            return false;
        }
        if ((config->role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) != 0u) {
            vdc_dpll_manager_sync_io_observer_config_t observer = {0};
            observer.enabled = true;
            observer.max_words_per_service =
                VDC_DPLL_MANAGER_SYNC_IO_MAX_BATCH_WORDS;
            observer.rising_event_id = 1u;
            observer.falling_event_id = 2u;
            observer.observed_mask = config->observed_mask;
            observer.initial_sample_mask = config->initial_sample_mask;
            observer.next_base_time_l32_ns =
                (uint32_t)status.first_window_start_ns;
            observer.sample_period_ns = config->sample_period_ns;
            observer.expected_window_start_ns = status.first_window_start_ns;
            observer.frame_crc32 = config->frame_crc32 != 0u
                ? config->frame_crc32 : 1u;
            observer.max_backward_ticks = pulse_period_ns;
            observer.sample0_lsb = false;
            observer.phase_only = true;
            observer.phase_max_span_ns = phase_max_span_ns;
            observer.phase_min_stable_rounds = phase_min_stable_rounds;
            if (!sync_io_start_capture(sample_hz) ||
                !vdc_dpll_manager_configure_sync_io_observer(&observer) ||
                !sync_io_capture_arm_periodic_timestamp_window(
                    status.first_window_start_ns,
                    pulse_period_ns,
                    pulse_period_ns,
                    config->sample_period_ns,
                    config->observed_mask,
                    config->initial_sample_mask)) {
                sync_io_stop_capture();
                sync_io_capture_disarm_timestamp_window();
                status.last_error = 1u;
                osal_critical_enter();
                s_observation_self_test = status;
                osal_critical_exit();
                return false;
            }
        }
        if ((config->role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX) != 0u) {
            sync_io_model_pulse_schedule_disarm();
            s_phase_tx_not_before_ns = status.first_window_start_ns;
            s_phase_tx_scheduled_count = 0u;
        }
        osal_critical_enter();
        s_observation_self_test = status;
        osal_critical_exit();
        return true;
    }

    if ((config->role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) != 0u) {
        if (!sync_io_start_capture(sample_hz)) {
            status.last_error = 1u;
            osal_critical_enter();
            s_observation_self_test = status;
            osal_critical_exit();
            return false;
        }

        if (!vdc_dpll_manager_configure_sync_io_observer_tdma_mask(
                true,
                config->observed_mask,
                config->initial_sample_mask,
                config->sample_period_ns,
                config->frame_crc32,
                true,
                config->start_delay_ns)) {
            sync_io_stop_capture();
            sync_io_capture_disarm_timestamp_window();
            status.last_error = 1u;
            osal_critical_enter();
            s_observation_self_test = status;
            osal_critical_exit();
            return false;
        }
    }

    if ((config->role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX) != 0u) {
        uint8_t frame[VDC_TDMA_PAYLOAD_FRAME_SIZE];
        size_t frame_size = 0u;
        vdc_tdma_window_plan_t tx_plan = plan;
        vdc_tdma_frame_envelope_t envelope;
        vdc_tdma_payload_status_t payload_status;
        const uint32_t pulse_window_offset_ns = 2000u;
        uint32_t first_pulse_delay_ns = 0u;

        tx_plan.window_start_ns += (uint64_t)config->start_delay_ns;
        tx_plan.window_end_ns += (uint64_t)config->start_delay_ns;
        tx_plan.guard_start_ns += (uint64_t)config->start_delay_ns;
        tx_plan.guard_end_ns += (uint64_t)config->start_delay_ns;

        s_vdc_tdma_self_test_frame_seq++;
        if (s_vdc_tdma_self_test_frame_seq == 0u) {
            s_vdc_tdma_self_test_frame_seq = 1u;
        }

        if (!vdc_tdma_payload_build_frame(&s_vdc_domain.schedule,
                                          &tx_plan,
                                          VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE,
                                          s_vdc_tdma_self_test_frame_seq,
                                          NULL,
                                          frame,
                                          sizeof(frame),
                                          &frame_size,
                                          &envelope,
                                          &payload_status)) {
            (void)payload_status;
            if ((config->role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) != 0u) {
                sync_io_stop_capture();
                sync_io_capture_disarm_timestamp_window();
            }
            status.last_error = 2u;
            osal_critical_enter();
            s_observation_self_test = status;
            osal_critical_exit();
            return false;
        }

        if (!vdc_dpll_manager_compute_first_pulse_delay(
                tx_plan.window_start_ns,
                pulse_window_offset_ns,
                &first_pulse_delay_ns) ||
            !sync_io_model_pulse_schedule_arm_periodic_ns(
                config->output_index,
                first_pulse_delay_ns,
                pulse_period_ns,
                pulse_high_ns,
                pulse_count,
                true,
                100u)) {
            if ((config->role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) != 0u) {
                sync_io_stop_capture();
                sync_io_capture_disarm_timestamp_window();
            }
            status.last_error = 4u;
            osal_critical_enter();
            s_observation_self_test = status;
            osal_critical_exit();
            return false;
        }
        const tdma_service_intent_config_t tdma_config = {
            .window_epoch = tx_plan.schedule_epoch,
            .window_index = tx_plan.slot_index,
            .deadline_us = vdc_dpll_manager_ns_to_ceil_us(
                (uint32_t)(tx_plan.window_end_ns - tx_plan.window_start_ns)),
            .role = TDMA_SERVICE_ROLE_MASTER,
            .baud_hz = 25000000u,
            .pins = {
                .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
                .csn_pin = UINT32_MAX,
                .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
                .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
            },
            .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
            .payload_class = TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE,
            .scheduled_window_valid = 1u,
            .scheduled_window_class = tx_plan.window_class,
            .schedule_crc32 = tx_plan.schedule_crc32,
            .scheduled_window_start_ns = tx_plan.window_start_ns,
            .scheduled_window_end_ns = tx_plan.window_end_ns,
            .scheduled_guard_start_ns = tx_plan.guard_start_ns,
            .scheduled_guard_end_ns = tx_plan.guard_end_ns,
            .frame = frame,
            .frame_size = frame_size,
        };
        if (!tdma_service_submit_tx(s_vdc_tdma_service, &tdma_config)) {
            sync_io_model_pulse_schedule_disarm();
            if ((config->role & VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX) != 0u) {
                sync_io_stop_capture();
                sync_io_capture_disarm_timestamp_window();
            }
            status.last_error = 3u;
            osal_critical_enter();
            s_observation_self_test = status;
            osal_critical_exit();
            return false;
        }

        osal_critical_enter();
        s_vdc_tdma_self_test_evidence = envelope.timestamp;
        tdma_service_snapshot_t tdma_snapshot;
        if (tdma_service_get_snapshot(s_vdc_tdma_service, &tdma_snapshot)) {
            s_vdc_tdma_self_test_evidence_seq = tdma_snapshot.intent_seq;
        }
        osal_critical_exit();
    }

    osal_critical_enter();
    s_observation_self_test = status;
    osal_critical_exit();
    return true;
}

void vdc_dpll_manager_get_observation_self_test_status(
    vdc_dpll_manager_observation_self_test_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_observation_self_test;
    osal_critical_exit();
}

void vdc_dpll_manager_get_sync_io_observer_status(
    vdc_dpll_manager_sync_io_observer_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_sync_io_observer_status;
    status->enabled = s_sync_io_observer_config.enabled;
    status->max_words_per_service =
        s_sync_io_observer_config.max_words_per_service;
    if (s_sync_io_observer_config.enabled) {
        status->rising_event_id = s_sync_io_observer_config.rising_event_id;
        status->falling_event_id = s_sync_io_observer_config.falling_event_id;
        status->observed_mask = s_sync_io_observer_config.observed_mask;
        status->initial_sample_mask =
            s_sync_io_observer_config.initial_sample_mask;
        status->sample_period_ns =
            s_sync_io_observer_config.sample_period_ns;
        status->expected_window_start_lo =
            (uint32_t)(s_sync_io_observer_config.expected_window_start_ns &
                       0xFFFFFFFFull);
        status->expected_window_start_hi =
            (uint32_t)(s_sync_io_observer_config.expected_window_start_ns >>
                       32u);
        status->frame_crc32 = s_sync_io_observer_config.frame_crc32;
        status->max_backward_ticks =
            s_sync_io_observer_config.max_backward_ticks;
        status->quality_flags = s_sync_io_observer_config.quality_flags;
        status->sample0_lsb =
            s_sync_io_observer_config.sample0_lsb ? 1u : 0u;
        status->schedule_crc32 = s_vdc_domain.schedule.schedule_crc32;
        status->dictionary_crc32 =
            s_vdc_domain.timestamp_dictionary.dictionary_crc32;
        status->dictionary_entry_count =
            s_vdc_domain.timestamp_dictionary.entry_count;
        status->dictionary_profile_crc32 =
            s_vdc_domain.timestamp_dictionary.profile_crc32;
    }
    osal_critical_exit();
}

static void vdc_dpll_manager_refresh_dco_consumer_status_core0(void)
{
    const uint32_t now_ms = board_uptime_ms();
    vdc_dpll_manager_runtime_snapshot_t snapshot;
    const uint32_t published_update_seq =
        vdc_dpll_manager_published_update_seq();
    if (published_update_seq == 0u ||
        published_update_seq == s_dpll_consumed_update_seq) {
        return;
    }
    const bool snapshot_ok =
        vdc_dpll_manager_get_runtime_snapshot(&snapshot);

    if (s_dpll_status.service_count == 0u) {
        s_dpll_status.first_service_ms = now_ms;
    }
    s_dpll_status.service_count++;
    s_dpll_status.last_service_ms = now_ms;
    s_dpll_status.ready = s_dpll_ready;
    s_dpll_status.state = snapshot_ok ? snapshot.dpll.state : 0u;
    s_dpll_status.update_seq = snapshot_ok ? snapshot.dpll.update_seq : 0u;

    s_dco_consumer_status.service_count++;
    s_dco_consumer_status.last_service_ms = now_ms;
    if (!snapshot_ok) {
        s_dco_consumer_status.valid = false;
        s_dco_consumer_status.last_error = 1u;
    } else if (!vdc_domain_dco_control_validate(&snapshot.schedule,
                                                &snapshot.servo,
                                                &snapshot.dco)) {
        s_dco_consumer_status.valid = false;
        s_dco_consumer_status.invalid_count++;
        s_dco_consumer_status.last_error = 2u;
    } else {
        if (snapshot.dco.dco_update_seq ==
            s_dco_consumer_status.last_dco_update_seq) {
            s_dco_consumer_status.unchanged_count++;
        } else {
            s_dco_consumer_status.accepted_update_count++;
        }

        s_dco_consumer_status.valid = true;
        s_dco_consumer_status.last_error = 0u;
        s_dco_consumer_status.last_dco_update_seq =
            snapshot.dco.dco_update_seq;
        s_dco_consumer_status.source_model_seq =
            snapshot.dco.source_model_seq;
        s_dco_consumer_status.lock_state = snapshot.dco.lock_state;
        s_dco_consumer_status.phase_offset_ns =
            snapshot.dco.phase_offset_ns;
        s_dco_consumer_status.period_adjust_ppb =
            snapshot.dco.period_adjust_ppb;
        s_dco_consumer_status.base_local_tick64 =
            snapshot.dco.base_local_tick64;
        s_dco_consumer_status.base_vdc_time64_ns =
            snapshot.dco.base_vdc_time64_ns;
        s_dco_consumer_status.nominal_period_ns =
            snapshot.dco.nominal_period_ns;
        s_dco_consumer_status.slew_limit_ppb =
            snapshot.dco.slew_limit_ppb;
        s_dco_consumer_status.tdma_schedule_crc32 =
            snapshot.dco.tdma_schedule_crc32;
        s_dco_consumer_status.servo_profile_crc32 =
            snapshot.dco.servo_profile_crc32;
    }

    if (snapshot_ok) {
        s_dpll_consumed_update_seq = snapshot.dpll.update_seq;
    }
    vdc_dpll_manager_publish_dpll_status();
}

static void vdc_dpll_manager_waveform_capture_service(void)
{
    if (s_waveform_job_inflight) {
        storage_manager_job_result_t job;
        storage_manager_get_job_result(&job);
        if (job.id == s_waveform_status.last_job_id &&
            (job.state == STORAGE_MANAGER_JOB_STATE_DONE ||
             job.state == STORAGE_MANAGER_JOB_STATE_FAILED)) {
            s_waveform_job_inflight = false;
            if (job.state == STORAGE_MANAGER_JOB_STATE_DONE) {
                s_waveform_status.segment_count++;
            } else {
                s_waveform_status.last_error =
                    job.error != 0u ? job.error : 1u;
                s_waveform_status.dropped_count +=
                    s_waveform_buffer_count[s_waveform_pending_buffer];
                s_waveform_status.armed = false;
                s_waveform_status.stopping = true;
            }
            s_waveform_buffer_count[s_waveform_pending_buffer] = 0u;
            s_waveform_pending_valid = false;
        }
    }

    if (!s_waveform_pending_valid &&
        s_waveform_buffer_count[s_waveform_active_buffer] >=
            VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS) {
        s_waveform_pending_buffer = s_waveform_active_buffer;
        s_waveform_pending_first_record =
            s_waveform_status.record_count -
            s_waveform_buffer_count[s_waveform_active_buffer];
        s_waveform_pending_valid = true;
        s_waveform_active_buffer ^= 1u;
        s_waveform_buffer_count[s_waveform_active_buffer] = 0u;
    }

    if (s_waveform_status.stopping && !s_waveform_pending_valid &&
        s_waveform_buffer_count[s_waveform_active_buffer] != 0u) {
        s_waveform_pending_buffer = s_waveform_active_buffer;
        s_waveform_pending_first_record =
            s_waveform_status.record_count -
            s_waveform_buffer_count[s_waveform_active_buffer];
        s_waveform_pending_valid = true;
        s_waveform_active_buffer ^= 1u;
        s_waveform_buffer_count[s_waveform_active_buffer] = 0u;
    }

    if (s_waveform_pending_valid && !s_waveform_job_inflight) {
        vdc_dpll_manager_waveform_header_t header = {0};
        const uint32_t record_count =
            s_waveform_buffer_count[s_waveform_pending_buffer];
        const size_t record_bytes =
            (size_t)record_count * sizeof(vdc_dpll_manager_waveform_record_t);
        const uint8_t *records =
            (const uint8_t *)s_waveform_buffers[s_waveform_pending_buffer];
        uint32_t txn_id = 0u;
        uint32_t job_id = 0u;
        uint32_t file_crc32;
        char path[96];

        header.magic = VDC_DPLL_MANAGER_WAVEFORM_MAGIC;
        header.schema = VDC_DPLL_MANAGER_WAVEFORM_SCHEMA;
        header.header_size = (uint16_t)sizeof(header);
        header.record_size =
            (uint16_t)sizeof(vdc_dpll_manager_waveform_record_t);
        header.session_id = s_waveform_status.session_id;
        header.segment_index = s_waveform_status.segment_count;
        header.first_record_index = s_waveform_pending_first_record;
        header.record_count = record_count;
        header.dropped_count = s_waveform_status.dropped_count;
        header.start_ms = s_waveform_status.start_ms;
        header.end_ms = s_waveform_status.end_ms;
        header.observed_mask = s_waveform_observed_mask;
        header.payload_crc32 = ota_crc32_compute(records, record_bytes);
        file_crc32 = ota_crc32_update(
            0u, (const uint8_t *)&header, sizeof(header));
        file_crc32 = ota_crc32_update(file_crc32, records, record_bytes);

        if (snprintf(path, sizeof(path),
                     "/traces/run/sma_%08lu_%04lu.bin",
                     (unsigned long)s_waveform_status.session_id,
                     (unsigned long)s_waveform_status.segment_count) > 0 &&
            sizeof(header) + record_bytes <= 8192u &&
            storage_manager_begin_file_write(
                path, (uint32_t)(sizeof(header) + record_bytes),
                file_crc32, &txn_id) &&
            storage_manager_write_file_chunk(
                txn_id, 0u, (const uint8_t *)&header, sizeof(header)) &&
            storage_manager_write_file_chunk(
                txn_id, (uint32_t)sizeof(header), records, record_bytes) &&
            storage_manager_commit_file_write(txn_id, &job_id)) {
            s_waveform_status.last_job_id = job_id;
            (void)snprintf(s_waveform_status.last_path,
                           sizeof(s_waveform_status.last_path), "%s", path);
            s_waveform_job_inflight = true;
        } else if (txn_id != 0u) {
            (void)storage_manager_abort_file_write(txn_id);
        }
    }

    if (s_waveform_status.stopping && !s_waveform_job_inflight &&
        !s_waveform_pending_valid &&
        s_waveform_buffer_count[s_waveform_active_buffer] == 0u) {
        s_waveform_status.stopping = false;
        s_waveform_status.complete = true;
    }
}

void VDC_DPLL_MANAGER_TIME_CRITICAL(sync_dpll_fb_service)(void)
{
    /* Complete already-admitted domain work before accepting another ring
     * sample. This keeps one bounded four-beat pipeline at the 4 ms evidence
     * cadence: prepare, servo, state/finalize, service/publish. */
    if (s_vdc_domain_service_pending) {
        s_vdc_domain_service_pending = false;
        vdc_domain_service(&s_vdc_domain, vdc_dpll_manager_now_ns());
        vdc_dpll_manager_publish_runtime_snapshot_locked();
        return;
    }
    if (vdc_dpll_manager_finalize_ring_evidence()) {
        return;
    }
    if (vdc_dpll_manager_prepare_ring_evidence()) {
        return;
    }
    (void)vdc_dpll_manager_apply_ring_evidence();
}

void vdc_dpll_manager_dpll_service(void)
{
    sync_dpll_fb_service();
}

void tdma_component_core1_service(void)
{
    /* During an active OTA session the config plane erases/writes flash.
     * Skip the TDMA service so the core1 lockout poll stays tight and the
     * flash protocol never times out behind the ring beacon. */
    if (ota_ao_is_active()) {
        return;
    }
    /* The PIO/DMA flight origin is submitted without waiting for wire
     * completion.  This bounded poll only harvests a completed launch/latch
     * token before the scheduler advances the next TDMA window. */
    tdma_runtime_owner_service_phys_tx(vdc_dpll_manager_now_ns());
    if (s_vdc_tdma_service != NULL) {
        tdma_service_core1_service(s_vdc_tdma_service);
    }
    tdma_runtime_owner_update_training_gate();
}

void vdc_dpll_manager_tdma_core1_service(void)
{
    tdma_component_core1_service();
}

void vdc_dpll_manager_get_vdc_status(vdc_dpll_manager_vdc_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_published_vdc_status;
    osal_critical_exit();
}

void vdc_dpll_manager_get_dpll_status(vdc_dpll_manager_dpll_status_t *status)
{
    if (status == NULL) {
        return;
    }

    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_published_dpll_status_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        *status = s_published_dpll_status;
        const uint32_t end = __atomic_load_n(
            &s_published_dpll_status_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            return;
        }
    }
    memset(status, 0, sizeof(*status));
}

void vdc_dpll_manager_get_dco_consumer_status(
    vdc_dpll_manager_dco_consumer_status_t *status)
{
    if (status == NULL) {
        return;
    }

    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_published_dpll_status_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        *status = s_published_dco_consumer_status;
        const uint32_t end = __atomic_load_n(
            &s_published_dpll_status_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            return;
        }
    }
    memset(status, 0, sizeof(*status));
}

bool VDC_DPLL_MANAGER_TIME_CRITICAL(vdc_dpll_manager_get_snapshot)(
    vdc_domain_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    for (uint32_t attempt = 0u; attempt < 8u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_published_snapshot_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        const bool valid = s_published_snapshot_valid;
        *snapshot = s_published_snapshot;
        const uint32_t end = __atomic_load_n(
            &s_published_snapshot_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            return valid;
        }
    }
    return false;
}

uint32_t VDC_DPLL_MANAGER_TIME_CRITICAL(
    vdc_dpll_manager_published_update_seq)(void)
{
    return __atomic_load_n(&s_published_dpll_update_seq, __ATOMIC_ACQUIRE);
}

bool vdc_dpll_manager_get_tdma_snapshot(tdma_service_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    /* Both values are published once by vdc_dpll_manager_init(), before core1
     * starts, and never changed during runtime.  The service fields themselves
     * use seqlock guards, so the read path must not contend on the global OSAL
     * lock with the realtime writer. */
    return s_vdc_tdma_registered && s_vdc_tdma_service != NULL &&
           tdma_service_get_snapshot(s_vdc_tdma_service, snapshot);
}

bool vdc_dpll_manager_plan_tdma_window(uint32_t window_class,
                                       uint64_t now_ns,
                                       vdc_tdma_window_plan_t *plan,
                                       vdc_gate_result_t *gate)
{
    bool result = false;
    if (plan == NULL) {
        return false;
    }

    if (now_ns == VDC_DPLL_MANAGER_PLAN_NOW_NS) {
        now_ns = vdc_dpll_manager_now_ns();
    }

    osal_critical_enter();
    result = vdc_domain_plan_tdma_window(&s_vdc_domain.schedule,
                                         window_class,
                                         now_ns,
                                         plan,
                                         gate);
    osal_critical_exit();
    return result;
}

bool vdc_dpll_manager_plan_tdma_ring(vdc_tdma_ring_plan_t *plan)
{
    bool result = false;
    if (plan == NULL) {
        return false;
    }

    osal_critical_enter();
    result = vdc_domain_plan_tdma_ring(&s_vdc_domain.schedule, plan);
    osal_critical_exit();
    return result;
}

bool vdc_dpll_manager_set_tdma_ring_local_slot(uint32_t local_slot_id)
{
    osal_critical_enter();
    const bool changed = vdc_domain_set_schedule_ring_topology(
        &s_vdc_domain,
        local_slot_id,
        s_vdc_domain.schedule.reference_slot_id,
        s_vdc_domain.schedule.ring_binding.node_count);
    osal_critical_exit();
    return changed;
}

bool vdc_dpll_manager_set_tdma_ring_topology(uint32_t local_slot_id,
                                             uint32_t reference_slot_id,
                                             uint32_t node_count)
{
    osal_critical_enter();
    const bool changed = vdc_domain_set_schedule_ring_topology(
        &s_vdc_domain, local_slot_id, reference_slot_id, node_count);
    osal_critical_exit();
    return changed;
}

bool vdc_dpll_manager_publish_timestamp_dictionary(
    const vdc_timestamp_dictionary_t *dictionary,
    uint32_t initial_tick_l32)
{
    bool result = false;
    if (dictionary == NULL) {
        return false;
    }

    osal_critical_enter();
    result = vdc_domain_publish_timestamp_dictionary(&s_vdc_domain,
                                                     dictionary,
                                                     initial_tick_l32);
    osal_critical_exit();
    return result;
}

bool vdc_dpll_manager_publish_calibration_path_delay(
    const vdc_path_delay_table_t *table)
{
    if (table == NULL || !vdc_domain_path_delay_table_validate(table)) {
        return false;
    }
    osal_critical_enter();
    const bool accepted = vdc_domain_publish_path_delay_table(
        &s_vdc_domain, table);
    osal_critical_exit();
    return accepted;
}

void vdc_dpll_manager_get_ring_observer_status(
    vdc_dpll_manager_ring_observer_status_t *status)
{
    if (status == NULL) {
        return;
    }
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_ring_observer_status_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        *status = s_ring_observer_status;
        const uint32_t end = __atomic_load_n(
            &s_ring_observer_status_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            return;
        }
    }
    memset(status, 0, sizeof(*status));
}

bool vdc_dpll_manager_dpll_capture_arm(void)
{
    bool accepted = false;
    osal_critical_enter();
    if (!s_dpll_capture_armed) {
        memset(s_dpll_capture_records, 0, sizeof(s_dpll_capture_records));
        s_dpll_capture_complete = false;
        s_dpll_capture_count = 0u;
        s_dpll_capture_dropped = 0u;
        s_dpll_capture_first_update_seq = 0u;
        s_dpll_capture_last_update_seq = 0u;
        s_dpll_capture_start_ms = 0u;
        s_dpll_capture_end_ms = 0u;
        s_dpll_capture_armed = true;
        accepted = true;
    }
    osal_critical_exit();
    return accepted;
}

bool vdc_dpll_manager_dpll_capture_stop(void)
{
    osal_critical_enter();
    s_dpll_capture_armed = false;
    s_dpll_capture_complete = true;
    osal_critical_exit();
    return true;
}

void vdc_dpll_manager_get_dpll_capture_status(
    vdc_dpll_manager_dpll_capture_status_t *status)
{
    if (status == NULL) {
        return;
    }
    osal_critical_enter();
    status->armed = s_dpll_capture_armed;
    status->complete = s_dpll_capture_complete;
    status->sample_count = s_dpll_capture_count;
    status->dropped_count = s_dpll_capture_dropped;
    status->first_update_seq = s_dpll_capture_first_update_seq;
    status->last_update_seq = s_dpll_capture_last_update_seq;
    status->start_ms = s_dpll_capture_start_ms;
    status->end_ms = s_dpll_capture_end_ms;
    osal_critical_exit();
}

bool vdc_dpll_manager_dpll_capture_save(uint32_t *job_id,
                                        char *path,
                                        size_t path_size)
{
    vdc_dpll_manager_dpll_capture_status_t status;
    vdc_dpll_manager_dpll_capture_header_t header;
    uint32_t txn_id = 0u;
    uint32_t file_job_id = 0u;
    uint32_t payload_crc32;
    uint32_t file_crc32;
    size_t record_bytes;
    size_t file_size;
    char capture_path[96];

    if (job_id == NULL || path == NULL || path_size == 0u) {
        return false;
    }
    vdc_dpll_manager_get_dpll_capture_status(&status);
    if (status.armed || !status.complete || status.sample_count == 0u ||
        status.sample_count > VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES) {
        return false;
    }

    record_bytes = (size_t)status.sample_count *
                   sizeof(vdc_dpll_manager_dpll_capture_record_t);
    header.magic = VDC_DPLL_MANAGER_DPLL_CAPTURE_MAGIC;
    header.schema = VDC_DPLL_MANAGER_DPLL_CAPTURE_SCHEMA;
    header.record_size =
        (uint16_t)sizeof(vdc_dpll_manager_dpll_capture_record_t);
    header.record_count = status.sample_count;
    header.dropped_count = status.dropped_count;
    header.start_ms = status.start_ms;
    header.end_ms = status.end_ms;
    payload_crc32 = ota_crc32_compute((const uint8_t *)s_dpll_capture_records,
                                      record_bytes);
    header.payload_crc32 = payload_crc32;
    file_crc32 = ota_crc32_update(0u,
                                  (const uint8_t *)&header,
                                  sizeof(header));
    file_crc32 = ota_crc32_update(
        file_crc32,
        (const uint8_t *)s_dpll_capture_records,
        record_bytes);
    file_size = sizeof(header) + record_bytes;
    if (file_size > 8192u ||
        snprintf(capture_path, sizeof(capture_path),
                 "/traces/run/dpll_%08lu.bin",
                 (unsigned long)status.start_ms) <= 0) {
        return false;
    }

    if (!storage_manager_begin_file_write(capture_path,
                                          (uint32_t)file_size,
                                          file_crc32,
                                          &txn_id) ||
        !storage_manager_write_file_chunk(txn_id,
                                          0u,
                                          (const uint8_t *)&header,
                                          sizeof(header)) ||
        !storage_manager_write_file_chunk(
            txn_id,
            (uint32_t)sizeof(header),
            (const uint8_t *)s_dpll_capture_records,
            record_bytes) ||
        !storage_manager_commit_file_write(txn_id, &file_job_id)) {
        if (txn_id != 0u) {
            (void)storage_manager_abort_file_write(txn_id);
        }
        return false;
    }

    if (snprintf(path, path_size, "%s", capture_path) < 0) {
        return false;
    }
    *job_id = file_job_id;
    return true;
}

bool vdc_dpll_manager_waveform_capture_arm(void)
{
    if (s_waveform_status.armed || s_waveform_status.stopping ||
        s_waveform_job_inflight || s_waveform_pending_valid) {
        return false;
    }

    osal_critical_enter();
    memset(s_waveform_buffers, 0, sizeof(s_waveform_buffers));
    memset(s_waveform_buffer_count, 0, sizeof(s_waveform_buffer_count));
    memset(&s_waveform_status, 0, sizeof(s_waveform_status));
    s_waveform_active_buffer = 0u;
    s_waveform_pending_buffer = 0u;
    s_waveform_pending_first_record = 0u;
    s_waveform_pending_valid = false;
    s_waveform_job_inflight = false;
    s_waveform_observed_mask = 0u;
    s_waveform_status.session_id = board_uptime_ms();
    if (s_waveform_status.session_id == 0u) {
        s_waveform_status.session_id = 1u;
    }
    s_waveform_status.start_ms = s_waveform_status.session_id;
    s_waveform_status.end_ms = s_waveform_status.session_id;
    s_waveform_status.armed = true;
    osal_critical_exit();
    return true;
}

bool vdc_dpll_manager_waveform_capture_stop(void)
{
    osal_critical_enter();
    s_waveform_status.armed = false;
    s_waveform_status.stopping = true;
    s_waveform_status.complete = false;
    s_waveform_status.end_ms = board_uptime_ms();
    osal_critical_exit();
    return true;
}

void vdc_dpll_manager_get_waveform_capture_status(
    vdc_dpll_manager_waveform_capture_status_t *status)
{
    if (status == NULL) {
        return;
    }
    osal_critical_enter();
    *status = s_waveform_status;
    status->pending_record_count =
        s_waveform_buffer_count[s_waveform_active_buffer] +
        (s_waveform_pending_valid
             ? s_waveform_buffer_count[s_waveform_pending_buffer] : 0u);
    osal_critical_exit();
}

bool vdc_dpll_manager_waveform_capture_manifest(char *path_prefix,
                                                size_t path_prefix_size,
                                                uint32_t *segment_count)
{
    vdc_dpll_manager_waveform_capture_status_t status;
    if (path_prefix == NULL || path_prefix_size == 0u ||
        segment_count == NULL) {
        return false;
    }
    vdc_dpll_manager_get_waveform_capture_status(&status);
    if (!status.complete || status.segment_count == 0u ||
        status.pending_record_count != 0u || status.last_error != 0u) {
        return false;
    }
    if (snprintf(path_prefix, path_prefix_size,
                 "/traces/run/sma_%08lu_",
                 (unsigned long)status.session_id) <= 0) {
        return false;
    }
    *segment_count = status.segment_count;
    return true;
}

static bool vdc_dpll_manager_phase_capture_owned_by_core0(void)
{
    bool owned;
    osal_critical_enter();
    owned = s_sync_io_observer_config.enabled &&
            s_sync_io_observer_config.phase_only;
    osal_critical_exit();
    return owned;
}

void vdc_dpll_manager_sync_io_capture_service_core1(void)
{
    if (!vdc_dpll_manager_phase_capture_owned_by_core0()) {
        sync_io_capture_latch_service_core1();
    }
}

void vdc_dpll_manager_core0_service(void)
{
    vdc_dpll_manager_observation_self_test_service();
    /* NO5 phase-only capture can scan a sustained 10 MHz DMA stream.  Keep
     * that diagnostic work off Core1; the PIO/DMA timestamp source remains
     * hardware-driven while Core0 performs edge qualification and SD enqueue. */
    if (vdc_dpll_manager_phase_capture_owned_by_core0()) {
        sync_io_capture_latch_service_core1();
    }
    vdc_dpll_manager_sync_io_observer_service();
    vdc_dpll_manager_waveform_capture_service();
    vdc_dpll_manager_refresh_dco_consumer_status_core0();
}

static bool vdc_dpll_manager_build_calibration_path_table(
    const calibration_path_snapshot_t *snapshot,
    const vdc_tdma_schedule_profile_t *schedule,
    uint32_t previous_update_seq,
    vdc_path_delay_table_t *table)
{
    if (snapshot == NULL || schedule == NULL || table == NULL ||
        !calibration_path_snapshot_validate(snapshot) ||
        snapshot->link_count > VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT ||
        snapshot->link_count != schedule->ring_binding.node_count ||
        snapshot->schedule_crc32 != schedule->schedule_crc32) {
        return false;
    }

    memset(table, 0, sizeof(*table));
    table->valid = 1u;
    table->version = VDC_DOMAIN_PATH_DELAY_TABLE_VERSION;
    table->update_seq = previous_update_seq + 1u;
    if (table->update_seq == 0u) {
        table->update_seq = 1u;
    }
    table->entry_count = snapshot->link_count;
    table->schedule_crc32 = schedule->schedule_crc32;
    table->calibration_generation = snapshot->calibration_generation;
    table->topology_generation = snapshot->topology_generation;
    table->bias_generation = snapshot->bias_generation;
    table->freshness_us = snapshot->freshness_us;
    table->flags = VDC_PATH_DELAY_FLAG_ACCEPTED |
                   VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                   VDC_PATH_DELAY_FLAG_BIAS_VALID |
                   VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH;

    for (uint32_t i = 0u; i < snapshot->link_count; i++) {
        const calibration_path_link_evidence_t *link = &snapshot->links[i];
        const int64_t delay_ns = link->measurement.delay_estimate_ns;
        const uint32_t source_slot_id = link->source_node;
        const uint32_t reference_slot_id = link->destination_node;
        if (delay_ns < 0 || (uint64_t)delay_ns > UINT32_MAX ||
            source_slot_id >= VDC_DOMAIN_NODE_COUNT ||
            reference_slot_id >= VDC_DOMAIN_NODE_COUNT) {
            return false;
        }
        vdc_path_delay_entry_t *entry = &table->entries[i];
        entry->valid = 1u;
        entry->source_slot_id = source_slot_id;
        entry->reference_slot_id = reference_slot_id;
        entry->direction = 0u;
        entry->delay_ns = (uint32_t)delay_ns;
        entry->jitter_ns = link->jitter_ns;
        entry->stddev_ns = link->jitter_ns;
        entry->cal_crc32 = snapshot->table_crc32;
        entry->freshness_us = snapshot->freshness_us;
        entry->writer = source_slot_id;
        entry->update_seq = table->update_seq;
    }
    if (!vdc_domain_load_observation_path_matrix(
            table, schedule->ring_binding.node_count)) {
        return false;
    }
    table->table_crc32 = vdc_domain_path_delay_table_crc32(table);
    return vdc_domain_path_delay_table_validate(table);
}

bool vdc_dpll_manager_publish_calibration_path_snapshot(
    const calibration_path_snapshot_t *snapshot)
{
    if (snapshot == NULL || !calibration_path_snapshot_validate(snapshot) ||
        snapshot->link_count > VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT) {
        return false;
    }

    osal_critical_enter();
    vdc_path_delay_table_t table;
    if (!vdc_dpll_manager_build_calibration_path_table(
            snapshot,
            &s_vdc_domain.schedule,
            s_vdc_domain.path_delay.update_seq,
            &table)) {
        osal_critical_exit();
        return false;
    }
    const bool accepted = vdc_domain_publish_path_delay_table(
        &s_vdc_domain, &table);
    osal_critical_exit();
    return accepted;
}

bool vdc_dpll_manager_activate_tdma_calibration(
    const calibration_path_snapshot_t *snapshot)
{
    tdma_service_ring_runtime_config_t runtime;
    if (snapshot == NULL || !calibration_path_snapshot_validate(snapshot) ||
        !tdma_runtime_owner_get_staged_ring_config(&runtime) ||
        runtime.enabled == 0u || runtime.schedule_crc32 == 0u ||
        runtime.operating_profile_crc32 == 0u ||
        snapshot->link_count != runtime.node_count ||
        snapshot->topology_crc32 != runtime.ring_profile_crc32 ||
        snapshot->profile_crc32 != runtime.operating_profile_crc32 ||
        snapshot->schedule_crc32 != runtime.schedule_crc32) {
        return false;
    }

    const vdc_tdma_runtime_binding_t binding = {
        .node_count = runtime.node_count,
        .local_slot_id = runtime.local_slot_id,
        .reference_slot_id = runtime.reference_slot_id,
        .ring_profile_crc32 = runtime.ring_profile_crc32,
        .operating_profile_crc32 = runtime.operating_profile_crc32,
        .cycle_period_ns = runtime.cycle_period_ns,
        .effective_schedule_crc32 = runtime.schedule_crc32,
    };

    osal_critical_enter();
    vdc_tdma_schedule_profile_t schedule;
    vdc_timestamp_dictionary_t dictionary;
    vdc_path_delay_table_t path_delay;
    bool prepared = vdc_domain_build_tdma_runtime_schedule(
        &s_vdc_domain.schedule, &binding, &schedule);
    if (prepared) {
        vdc_domain_default_timestamp_dictionary(&dictionary, &schedule);
        prepared = vdc_timestamp_dictionary_validate(&dictionary);
    }
    if (prepared) {
        prepared = vdc_dpll_manager_build_calibration_path_table(
            snapshot, &schedule, s_vdc_domain.path_delay.update_seq,
            &path_delay);
    }
    const bool accepted = prepared &&
        vdc_domain_activate_tdma_configuration(
            &s_vdc_domain, &schedule, &dictionary, &path_delay);
    if (accepted) {
        s_vdc_ring_observation_config_seq = 0u;
        s_vdc_ring_observation_sequence = 0u;
        s_vdc_ring_pending_config_seq = 0u;
        s_vdc_ring_evidence_pending = false;
        s_vdc_ring_preparation_pending = false;
        s_vdc_ring_finalization_pending = false;
        memset(&s_vdc_ring_preparation, 0,
               sizeof(s_vdc_ring_preparation));
        s_vdc_domain_service_pending = false;
        (void)vdc_dpll_manager_publish_domain_snapshot_locked();
    }
    osal_critical_exit();
    return accepted;
}

static bool vdc_dpll_manager_build_provisional_training_path_table(
    const tdma_ring_calibration_stage_t *stage,
    const vdc_tdma_schedule_profile_t *schedule,
    uint32_t previous_update_seq,
    vdc_path_delay_table_t *table)
{
    tdma_ring_runtime_reason_t reason;
    if (stage == NULL || schedule == NULL || table == NULL ||
        !tdma_ring_runtime_validate_calibration_stage(
            stage, schedule->ring_binding.node_count, &reason) ||
        stage->node_count != schedule->ring_binding.node_count ||
        stage->schedule_crc32 != schedule->schedule_crc32 ||
        stage->profile_crc32 != schedule->operating_profile_crc32) {
        return false;
    }

    memset(table, 0, sizeof(*table));
    table->valid = 1u;
    table->version = VDC_DOMAIN_PATH_DELAY_TABLE_VERSION;
    table->update_seq = previous_update_seq + 1u;
    if (table->update_seq == 0u) {
        table->update_seq = 1u;
    }
    table->entry_count = stage->node_count;
    table->schedule_crc32 = schedule->schedule_crc32;
    table->calibration_generation = stage->calibration_generation;
    table->topology_generation = stage->topology_generation;
    table->bias_generation = 0u;
    table->freshness_us = 1u;
    table->flags = VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                   VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY;

    for (uint32_t i = 0u; i < stage->node_count; i++) {
        const tdma_ring_calibration_link_t *link = &stage->links[i];
        if (link->link_base_delay_ns > UINT32_MAX / 2u) {
            return false;
        }
        vdc_path_delay_entry_t *entry = &table->entries[i];
        entry->valid = 1u;
        entry->source_slot_id = link->marker_source_node;
        entry->reference_slot_id = link->marker_destination_node;
        entry->direction = 0u;
        /* TRN-01/02 freezes link_base_delay as half of the measured
         * directed link delay. P4-LIVE uses that quantized value only as a
         * provisional servo input; endpoint bias remains deliberately absent. */
        entry->delay_ns = link->link_base_delay_ns * 2u;
        entry->jitter_ns = link->sample_period_ns;
        entry->stddev_ns = link->sample_period_ns;
        entry->cal_crc32 = stage->topology_crc32;
        entry->freshness_us = table->freshness_us;
        entry->writer = link->marker_source_node;
        entry->update_seq = table->update_seq;
    }
    if (!vdc_domain_load_observation_path_matrix(table, stage->node_count)) {
        return false;
    }
    table->table_crc32 = vdc_domain_path_delay_table_crc32(table);
    return vdc_domain_path_delay_table_validate_provisional(table);
}

bool vdc_dpll_manager_activate_tdma_provisional_training(void)
{
    tdma_service_ring_runtime_config_t runtime;
    tdma_ring_runtime_snapshot_t ring;
    tdma_ring_calibration_stage_t stage;
    bool complete = false;
    if (!tdma_runtime_owner_get_staged_ring_config(&runtime) ||
        !tdma_runtime_owner_get_ring_snapshot(&ring) || ring.enabled != 0u ||
        !tdma_runtime_owner_get_calibration_stage(&stage, &complete) ||
        !complete || runtime.enabled == 0u ||
        stage.node_count != runtime.node_count ||
        stage.topology_crc32 != runtime.ring_profile_crc32 ||
        stage.profile_crc32 != runtime.operating_profile_crc32 ||
        stage.schedule_crc32 != runtime.schedule_crc32) {
        return false;
    }

    const vdc_tdma_runtime_binding_t binding = {
        .node_count = runtime.node_count,
        .local_slot_id = runtime.local_slot_id,
        .reference_slot_id = runtime.reference_slot_id,
        .ring_profile_crc32 = runtime.ring_profile_crc32,
        .operating_profile_crc32 = runtime.operating_profile_crc32,
        .cycle_period_ns = runtime.cycle_period_ns,
        .effective_schedule_crc32 = runtime.schedule_crc32,
    };

    osal_critical_enter();
    vdc_tdma_schedule_profile_t schedule;
    vdc_timestamp_dictionary_t dictionary;
    vdc_path_delay_table_t path_delay;
    bool prepared = vdc_domain_build_tdma_runtime_schedule(
        &s_vdc_domain.schedule, &binding, &schedule);
    if (prepared) {
        vdc_domain_default_timestamp_dictionary(&dictionary, &schedule);
        prepared = vdc_timestamp_dictionary_validate(&dictionary);
    }
    if (prepared) {
        prepared = vdc_dpll_manager_build_provisional_training_path_table(
            &stage, &schedule, s_vdc_domain.path_delay.update_seq,
            &path_delay);
    }
    const bool accepted = prepared &&
        vdc_domain_activate_tdma_provisional_configuration(
            &s_vdc_domain, &schedule, &dictionary, &path_delay);
    if (accepted) {
        s_vdc_ring_observation_config_seq = 0u;
        s_vdc_ring_observation_sequence = 0u;
        s_vdc_ring_pending_config_seq = 0u;
        s_vdc_ring_evidence_pending = false;
        s_vdc_ring_preparation_pending = false;
        s_vdc_ring_finalization_pending = false;
        memset(&s_vdc_ring_preparation, 0,
               sizeof(s_vdc_ring_preparation));
        s_vdc_domain_service_pending = false;
        (void)vdc_dpll_manager_publish_domain_snapshot_locked();
    }
    osal_critical_exit();
    return accepted;
}

bool vdc_dpll_manager_submit_compact_observation(
    const vdc_compact_observation_sample_t *compact)
{
    bool result = false;
    if (compact == NULL) {
        return false;
    }

    osal_critical_enter();
    result = vdc_domain_submit_compact_observation(&s_vdc_domain, compact);
    osal_critical_exit();
    return result;
}
