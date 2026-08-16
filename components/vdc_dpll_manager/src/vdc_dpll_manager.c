#include "vdc_dpll_manager.h"

#include <string.h>

#include "board.h"
#include "osal.h"
#include "sync_io.h"
#include "vdc_domain.h"
#include "vdc_sync_io_adapter.h"

static vdc_dpll_manager_vdc_status_t s_vdc_status;
static vdc_dpll_manager_dpll_status_t s_dpll_status;
static vdc_dpll_manager_sync_io_observer_config_t s_sync_io_observer_config;
static vdc_dpll_manager_sync_io_observer_status_t s_sync_io_observer_status;
static vdc_domain_context_t s_vdc_domain;
static bool s_vdc_ready;
static bool s_dpll_ready;

static uint64_t vdc_dpll_manager_now_ns(void)
{
    return (uint64_t)board_uptime_ms() * 1000000ull;
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

static void vdc_dpll_manager_sync_io_observer_service(void)
{
    uint32_t words[VDC_DPLL_MANAGER_SYNC_IO_MAX_BATCH_WORDS];
    size_t count = 0u;
    vdc_dpll_manager_sync_io_observer_config_t config;

    osal_critical_enter();
    config = s_sync_io_observer_config;
    osal_critical_exit();

    if (!config.enabled || config.max_words_per_service == 0u) {
        return;
    }

    count = sync_io_read_capture_words(words, config.max_words_per_service);
    if (count == 0u) {
        return;
    }

    osal_critical_enter();
    s_sync_io_observer_status.service_count++;
    for (size_t i = 0u; i < count; i++) {
        vdc_compact_observation_sample_t compact;
        uint32_t last_sample_mask = s_sync_io_observer_status.previous_sample_mask;
        const uint32_t sample_seq =
            s_sync_io_observer_status.raw_word_count + 1u;
        vdc_sync_io_capture_decode_config_t decode = {
            .valid = 1u,
            .sample_seq = sample_seq,
            .rising_event_id = config.rising_event_id,
            .falling_event_id = config.falling_event_id,
            .observed_mask = config.observed_mask,
            .previous_sample_mask =
                s_sync_io_observer_status.previous_sample_mask,
            .base_time_l32_ns =
                s_sync_io_observer_status.next_base_time_l32_ns,
            .sample_period_ns = config.sample_period_ns,
            .expected_window_start_ns = config.expected_window_start_ns,
            .frame_crc32 = config.frame_crc32,
            .max_backward_ticks = config.max_backward_ticks,
            .quality_flags = config.quality_flags,
            .sample0_lsb = config.sample0_lsb,
        };
        const vdc_sync_io_capture_result_t result =
            vdc_sync_io_capture_word_to_compact_observation(&decode,
                                                            words[i],
                                                            &compact,
                                                            &last_sample_mask);

        s_sync_io_observer_status.raw_word_count++;
        s_sync_io_observer_status.last_capture_result = (uint32_t)result;
        s_sync_io_observer_status.last_raw_word = words[i];
        s_sync_io_observer_status.last_sample_seq = sample_seq;
        s_sync_io_observer_status.previous_sample_mask =
            last_sample_mask & config.observed_mask;
        s_sync_io_observer_status.next_base_time_l32_ns +=
            config.sample_period_ns * VDC_SYNC_IO_CAPTURE_SAMPLES_PER_WORD;

        switch (result) {
        case VDC_SYNC_IO_CAPTURE_OK:
            s_sync_io_observer_status.submitted_count++;
            s_sync_io_observer_status.last_event_id = compact.event_id;
            s_sync_io_observer_status.last_tick_l32 = compact.tick_l32;
            if (vdc_domain_submit_compact_observation(&s_vdc_domain, &compact)) {
                s_sync_io_observer_status.accepted_count++;
                s_sync_io_observer_status.last_gate_reject_code =
                    VDC_DOMAIN_GATE_PASS;
            } else {
                s_sync_io_observer_status.rejected_count++;
                s_sync_io_observer_status.last_gate_reject_code =
                    s_vdc_domain.gate.reject_code;
            }
            break;
        case VDC_SYNC_IO_CAPTURE_NO_EDGE:
            s_sync_io_observer_status.no_edge_count++;
            break;
        case VDC_SYNC_IO_CAPTURE_AMBIGUOUS_EDGE:
            s_sync_io_observer_status.ambiguous_edge_count++;
            break;
        case VDC_SYNC_IO_CAPTURE_BAD_ARGUMENT:
        default:
            s_sync_io_observer_status.bad_argument_count++;
            break;
        }
    }
    osal_critical_exit();
}

bool vdc_dpll_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    memset(&s_vdc_status, 0, sizeof(s_vdc_status));
    memset(&s_dpll_status, 0, sizeof(s_dpll_status));
    memset(&s_sync_io_observer_config, 0, sizeof(s_sync_io_observer_config));
    memset(&s_sync_io_observer_status, 0, sizeof(s_sync_io_observer_status));
    if (!vdc_domain_init(&s_vdc_domain)) {
        return false;
    }
    s_vdc_status.last_service_ms = now_ms;
    s_dpll_status.last_service_ms = now_ms;
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
    osal_critical_exit();
}

void vdc_dpll_manager_set_dpll_ready(bool ready)
{
    osal_critical_enter();
    s_dpll_ready = ready;
    s_dpll_status.ready = ready;
    osal_critical_exit();
}

void vdc_dpll_manager_vdc_service(void)
{
    const uint32_t now_ms = board_uptime_ms();
    vdc_domain_snapshot_t snapshot;

    vdc_dpll_manager_sync_io_observer_service();

    osal_critical_enter();
    vdc_domain_service(&s_vdc_domain, vdc_dpll_manager_now_ns());
    (void)vdc_domain_get_snapshot(&s_vdc_domain, &snapshot);
    if (s_vdc_status.service_count == 0u) {
        s_vdc_status.first_service_ms = now_ms;
    }
    s_vdc_status.service_count++;
    s_vdc_status.last_service_ms = now_ms;
    s_vdc_status.ready = s_vdc_ready;
    s_vdc_status.lock_state = snapshot.dpll.state;
    s_vdc_status.sync_seq++;
    osal_critical_exit();
}

bool vdc_dpll_manager_configure_sync_io_observer(
    const vdc_dpll_manager_sync_io_observer_config_t *config)
{
    if (!vdc_dpll_manager_sync_io_observer_config_valid(config)) {
        return false;
    }

    osal_critical_enter();
    s_sync_io_observer_config = *config;
    vdc_dpll_manager_sync_io_observer_reset_status_locked();
    osal_critical_exit();
    return true;
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
    osal_critical_exit();
}

void vdc_dpll_manager_dpll_service(void)
{
    const uint32_t now_ms = board_uptime_ms();
    vdc_domain_snapshot_t snapshot;

    osal_critical_enter();
    (void)vdc_domain_get_snapshot(&s_vdc_domain, &snapshot);
    if (s_dpll_status.service_count == 0u) {
        s_dpll_status.first_service_ms = now_ms;
    }
    s_dpll_status.service_count++;
    s_dpll_status.last_service_ms = now_ms;
    s_dpll_status.ready = s_dpll_ready;
    s_dpll_status.state = snapshot.dpll.state;
    s_dpll_status.update_seq = snapshot.dpll.update_seq;
    osal_critical_exit();
}

void vdc_dpll_manager_get_vdc_status(vdc_dpll_manager_vdc_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_vdc_status;
    status->ready = s_vdc_ready;
    osal_critical_exit();
}

void vdc_dpll_manager_get_dpll_status(vdc_dpll_manager_dpll_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_dpll_status;
    status->ready = s_dpll_ready;
    osal_critical_exit();
}

bool vdc_dpll_manager_get_snapshot(vdc_domain_snapshot_t *snapshot)
{
    bool result = false;
    if (snapshot == NULL) {
        return false;
    }

    osal_critical_enter();
    result = vdc_domain_get_snapshot(&s_vdc_domain, snapshot);
    osal_critical_exit();
    return result;
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
