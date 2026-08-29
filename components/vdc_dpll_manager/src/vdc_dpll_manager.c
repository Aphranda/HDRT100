#include "vdc_dpll_manager.h"

#include <limits.h>
#include <string.h>

#include "board.h"
#include "board_config.h"
#include "osal.h"
#include "ota_ao.h"
#include "sync_io.h"
#include "tdma_runtime_owner.h"
#include "tdma_service.h"
#include "vdc_domain.h"
#include "vdc_ring_observer.h"
#include "vdc_sync_io_adapter.h"
#include "vdc_tdma_payload.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/time.h"
#endif

#define VDC_DPLL_MANAGER_SELF_TEST_CLEANUP_MARGIN_MS 250u

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

static bool vdc_dpll_manager_ring_observer_service(uint32_t *lock_state)
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

void vdc_sync_ao_service(void)
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
    if (sample_hz == 0u ||
        pulse_high_ns == 0u ||
        pulse_high_ns >= pulse_period_ns ||
        pulse_count > VDC_DPLL_MANAGER_SELF_TEST_MAX_PULSES) {
        return false;
    }

    if (!vdc_dpll_manager_plan_tdma_window(VDC_DOMAIN_WINDOW_VDC_OBSERVATION,
                                           now_ns,
                                           &plan,
                                           &gate)) {
        return false;
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
    status.first_window_start_ns =
        plan.window_start_ns + (uint64_t)config->start_delay_ns;

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

void sync_dpll_fb_service(void)
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

bool vdc_dpll_manager_get_snapshot(vdc_domain_snapshot_t *snapshot)
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

uint32_t vdc_dpll_manager_published_update_seq(void)
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

void vdc_dpll_manager_core0_service(void)
{
    vdc_dpll_manager_observation_self_test_service();
    vdc_dpll_manager_sync_io_observer_service();
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
