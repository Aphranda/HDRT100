#include "tdma_ring_runtime.h"

#include "tdma_transport_frame.h"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/time.h"
#endif

#define TDMA_RING_RUNTIME_SNAPSHOT_RETRY_LIMIT 64u

static uint32_t tdma_ring_runtime_load(const volatile uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void tdma_ring_runtime_write_guard(volatile uint32_t *guard)
{
    (void)__atomic_add_fetch(guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_ring_runtime_set_reason(tdma_ring_runtime_reason_t *reason,
                                         tdma_ring_runtime_reason_t value)
{
    if (reason != NULL) {
        *reason = value;
    }
}

static uint64_t tdma_ring_runtime_now_ns(void)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    return to_us_since_boot(get_absolute_time()) * 1000ull;
#else
    static uint64_t host_now_ns;
    host_now_ns += 1000ull;
    return host_now_ns;
#endif
}

static void tdma_ring_runtime_stop_adapter(tdma_ring_runtime_t *runtime)
{
    if (runtime->adapter_started != 0u) {
        if (runtime->adapter_ops != NULL &&
            runtime->adapter_ops->stop != NULL) {
            runtime->adapter_ops->stop(runtime->adapter_context);
        }
        runtime->adapter_started = 0u;
        runtime->adapter_config_seq = 0u;
        runtime->adapter_stop_count++;
    }
}

static bool tdma_ring_runtime_feedback_correlated(
    const tdma_ring_runtime_t *runtime,
    const tdma_ring_adapter_status_t *status,
    uint32_t *round_trip_ns)
{
    if (round_trip_ns != NULL) {
        *round_trip_ns = 0u;
    }
    if (runtime == NULL || status == NULL || round_trip_ns == NULL ||
        status->up_running == 0u || status->down_running == 0u ||
        status->up_tx_sequence == 0u ||
        status->up_tx_sequence != status->down_rx_sequence ||
        status->up_tx_frame_crc32 == 0u ||
        status->up_tx_frame_crc32 != status->down_rx_frame_crc32 ||
        status->schedule_crc32 != runtime->schedule_crc32 ||
        status->timestamp_resolution_ns == 0u ||
        status->timestamp_resolution_ns > 100u ||
        (status->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) == 0u ||
        (status->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0u ||
        status->reference_tx_timestamp_ns == 0ull ||
        status->feedback_rx_timestamp_ns <
            status->reference_tx_timestamp_ns) {
        return false;
    }
    const uint64_t delta = status->feedback_rx_timestamp_ns -
                           status->reference_tx_timestamp_ns;
    const uint32_t loop_delay_lower_bound_ns =
        runtime->loop_delay_ns > runtime->loop_delay_tolerance_ns
            ? runtime->loop_delay_ns - runtime->loop_delay_tolerance_ns
            : 0u;
    if (delta > UINT32_MAX || runtime->feedback_timeout_ns == 0u ||
        delta > runtime->feedback_timeout_ns ||
        (runtime->loop_delay_ns != 0u &&
         delta < loop_delay_lower_bound_ns)) {
        return false;
    }
    *round_trip_ns = (uint32_t)delta;
    return true;
}

bool tdma_ring_runtime_init(tdma_ring_runtime_t *runtime)
{
    if (runtime == NULL) {
        return false;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->last_reason = TDMA_RING_RUNTIME_REASON_NONE;
    return true;
}

bool tdma_ring_runtime_validate_config(
    const tdma_ring_runtime_config_t *config,
    tdma_ring_runtime_reason_t *reason)
{
    tdma_ring_runtime_set_reason(reason, TDMA_RING_RUNTIME_REASON_NONE);
    if (config == NULL || config->enabled == 0u) {
        return true;
    }
    if (config->up_group_id == 0u || config->down_group_id == 0u ||
        config->up_group_id == config->down_group_id) {
        tdma_ring_runtime_set_reason(
            reason,
            TDMA_RING_RUNTIME_REASON_DIRECTION_CONFLICT);
        return false;
    }
    if (config->node_count < 2u ||
        config->node_count > TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT ||
        config->local_slot_id >= config->node_count ||
        config->reference_slot_id >= config->node_count ||
        (config->flags & TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN) == 0u ||
        config->ring_profile_crc32 == 0u || config->schedule_crc32 == 0u ||
        config->operating_profile_crc32 == 0u ||
        config->baud_hz < 1000000u || config->baud_hz > 50000000u ||
        config->cycle_period_ns == 0u ||
        (config->loop_delay_ns != 0u &&
         config->feedback_timeout_ns != 0u &&
         config->loop_delay_ns > config->feedback_timeout_ns) ||
        config->feedback_timeout_ns == 0u ||
        config->tx_dma_channel_id == TDMA_RESOURCE_ID_UNUSED ||
        config->rx_dma_channel_id == TDMA_RESOURCE_ID_UNUSED ||
        config->tx_dma_channel_id == config->rx_dma_channel_id) {
        tdma_ring_runtime_set_reason(reason,
                                     TDMA_RING_RUNTIME_REASON_BAD_CONFIG);
        return false;
    }
    return true;
}

bool tdma_ring_runtime_configure(tdma_ring_runtime_t *runtime,
                                 const tdma_ring_runtime_config_t *config)
{
    if (runtime == NULL) {
        return false;
    }

    tdma_ring_runtime_reason_t reason = TDMA_RING_RUNTIME_REASON_NONE;
    if (!tdma_ring_runtime_validate_config(config, &reason)) {
        tdma_ring_runtime_write_guard(&runtime->result_guard);
        runtime->config_reject_count++;
        runtime->last_reason = (uint32_t)reason;
        runtime->simultaneous_feedback_loop_evidence = 0u;
        tdma_ring_runtime_write_guard(&runtime->result_guard);
        return false;
    }

    tdma_ring_runtime_write_guard(&runtime->config_guard);
    runtime->config_seq++;
    if (config == NULL || config->enabled == 0u) {
        runtime->enabled = 0u;
        runtime->node_count = 0u;
        runtime->local_slot_id = 0u;
        runtime->reference_slot_id = 0u;
        runtime->up_group_id = 0u;
        runtime->down_group_id = 0u;
        runtime->flags = 0u;
        runtime->ring_profile_crc32 = 0u;
        runtime->schedule_crc32 = 0u;
        runtime->operating_profile_crc32 = 0u;
        runtime->baud_hz = 0u;
        runtime->cycle_period_ns = 0u;
        runtime->loop_delay_ns = 0u;
        runtime->loop_delay_tolerance_ns = 0u;
        runtime->feedback_timeout_ns = 0u;
        runtime->tx_dma_channel_id = TDMA_RESOURCE_ID_UNUSED;
        runtime->rx_dma_channel_id = TDMA_RESOURCE_ID_UNUSED;
    } else {
        runtime->enabled = 1u;
        runtime->node_count = config->node_count;
        runtime->local_slot_id = config->local_slot_id;
        runtime->reference_slot_id = config->reference_slot_id;
        runtime->up_group_id = config->up_group_id;
        runtime->down_group_id = config->down_group_id;
        runtime->flags = config->flags;
        runtime->ring_profile_crc32 = config->ring_profile_crc32;
        runtime->schedule_crc32 = config->schedule_crc32;
        runtime->operating_profile_crc32 = config->operating_profile_crc32;
        runtime->baud_hz = config->baud_hz;
        runtime->cycle_period_ns = config->cycle_period_ns;
        runtime->loop_delay_ns = config->loop_delay_ns;
        runtime->loop_delay_tolerance_ns = config->loop_delay_tolerance_ns;
        runtime->feedback_timeout_ns = config->feedback_timeout_ns;
        runtime->tx_dma_channel_id = config->tx_dma_channel_id;
        runtime->rx_dma_channel_id = config->rx_dma_channel_id;
    }
    tdma_ring_runtime_write_guard(&runtime->config_guard);

    tdma_ring_runtime_write_guard(&runtime->result_guard);
    runtime->up_configured = runtime->up_group_id != 0u ? 1u : 0u;
    runtime->down_configured = runtime->down_group_id != 0u ? 1u : 0u;
    runtime->up_running = 0u;
    runtime->down_running = 0u;
    runtime->last_reason = TDMA_RING_RUNTIME_REASON_NONE;
    runtime->simultaneous_feedback_loop_evidence = 0u;
    runtime->up_tx_sequence = 0u;
    runtime->down_rx_sequence = 0u;
    runtime->up_tx_frame_crc32 = 0u;
    runtime->down_rx_frame_crc32 = 0u;
    runtime->timestamp_resolution_ns = 0u;
    runtime->timestamp_flags = 0u;
    runtime->idle_beacon_tx_count = 0u;
    runtime->idle_beacon_rx_count = 0u;
    runtime->feedback_round_trip_ns = 0u;
    runtime->reference_tx_timestamp_ns = 0ull;
    runtime->feedback_rx_timestamp_ns = 0ull;
    runtime->data_enabled = 0u;
    runtime->train_request_seq =
        tdma_ring_runtime_load(&runtime->train_command_seq);
    __atomic_store_n(&runtime->train_accepted_seq,
                     runtime->train_request_seq,
                     __ATOMIC_RELEASE);
    runtime->train_request_cycles = 0u;
    runtime->train_start_count = 0u;
    runtime->train_reject_count = 0u;
    runtime->training_dirty = 0u;
    tdma_ring_runtime_write_guard(&runtime->result_guard);
    return true;
}

bool tdma_ring_runtime_set_data_enabled(tdma_ring_runtime_t *runtime,
                                        bool enabled)
{
    if (runtime == NULL || tdma_ring_runtime_load(&runtime->enabled) == 0u ||
        tdma_ring_runtime_load(&runtime->adapter_started) == 0u ||
        (enabled &&
         tdma_ring_runtime_load(&runtime->train_command_seq) !=
             tdma_ring_runtime_load(&runtime->train_accepted_seq))) {
        return false;
    }
    __atomic_store_n(&runtime->data_enabled,
                     enabled ? 1u : 0u,
                     __ATOMIC_RELEASE);
    return true;
}

bool tdma_ring_runtime_train_clock(tdma_ring_runtime_t *runtime,
                                   uint32_t cycles)
{
    if (runtime == NULL || cycles == 0u ||
        tdma_ring_runtime_load(&runtime->enabled) == 0u ||
        tdma_ring_runtime_load(&runtime->adapter_started) == 0u ||
        tdma_ring_runtime_load(&runtime->data_enabled) != 0u ||
        runtime->adapter_ops == NULL ||
        runtime->adapter_ops->train_clock == NULL ||
        runtime->adapter_ops->train_clock_service == NULL) {
        return false;
    }
    const uint32_t command_seq =
        tdma_ring_runtime_load(&runtime->train_command_seq);
    const uint32_t accepted_seq =
        tdma_ring_runtime_load(&runtime->train_accepted_seq);
    if (command_seq != accepted_seq) {
        return false;
    }
    __atomic_store_n(&runtime->train_command_cycles, cycles, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->train_command_seq,
                     command_seq + 1u,
                     __ATOMIC_RELEASE);
    return true;
}

bool tdma_ring_runtime_bind_adapter(tdma_ring_runtime_t *runtime,
                                    const tdma_ring_adapter_ops_t *ops,
                                    void *context)
{
    if (runtime == NULL || ops == NULL || ops->start == NULL ||
        ops->stop == NULL || ops->service == NULL) {
        return false;
    }
    tdma_ring_runtime_write_guard(&runtime->result_guard);
    tdma_ring_runtime_stop_adapter(runtime);
    runtime->adapter_ops = ops;
    runtime->adapter_context = context;
    runtime->last_reason = TDMA_RING_RUNTIME_REASON_NONE;
    tdma_ring_runtime_write_guard(&runtime->result_guard);
    return true;
}

void tdma_ring_runtime_unbind_adapter(tdma_ring_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    tdma_ring_runtime_write_guard(&runtime->result_guard);
    tdma_ring_runtime_stop_adapter(runtime);
    runtime->adapter_ops = NULL;
    runtime->adapter_context = NULL;
    runtime->last_reason = TDMA_RING_RUNTIME_REASON_NONE;
    tdma_ring_runtime_write_guard(&runtime->result_guard);
}

void tdma_ring_runtime_service(tdma_ring_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    const uint32_t enabled = tdma_ring_runtime_load(&runtime->enabled);
    const uint32_t up_group = tdma_ring_runtime_load(&runtime->up_group_id);
    const uint32_t down_group = tdma_ring_runtime_load(&runtime->down_group_id);
    const uint32_t flags = tdma_ring_runtime_load(&runtime->flags);
    const bool up_down_config_ready =
        enabled != 0u && up_group != 0u && down_group != 0u &&
        up_group != down_group &&
        (flags & TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN) != 0u;

    tdma_ring_adapter_status_t adapter_status;
    memset(&adapter_status, 0, sizeof(adapter_status));
    bool adapter_service_ok = false;
    tdma_ring_runtime_reason_t reason = TDMA_RING_RUNTIME_REASON_NONE;
    const uint32_t previous_down_rx_sequence = runtime->down_rx_sequence;
    const uint64_t now_ns = tdma_ring_runtime_now_ns();
    uint32_t train_request_seq = runtime->train_request_seq;
    uint32_t train_accepted_seq = runtime->train_accepted_seq;
    uint32_t train_request_cycles = runtime->train_request_cycles;
    uint32_t train_start_count = runtime->train_start_count;
    uint32_t train_reject_count = runtime->train_reject_count;
    uint32_t training_dirty = runtime->training_dirty;
    if (!up_down_config_ready) {
        tdma_ring_runtime_stop_adapter(runtime);
        if (enabled != 0u) {
            reason = TDMA_RING_RUNTIME_REASON_BAD_CONFIG;
        }
    } else if (runtime->adapter_ops == NULL) {
        reason = TDMA_RING_RUNTIME_REASON_ADAPTER_MISSING;
    } else {
        if (runtime->adapter_started == 0u ||
            runtime->adapter_config_seq != runtime->config_seq) {
            tdma_ring_runtime_stop_adapter(runtime);
            tdma_ring_runtime_config_t config = {
                .enabled = enabled,
                .node_count = runtime->node_count,
                .local_slot_id = runtime->local_slot_id,
                .reference_slot_id = runtime->reference_slot_id,
                .up_group_id = up_group,
                .down_group_id = down_group,
                .flags = flags,
                .ring_profile_crc32 = runtime->ring_profile_crc32,
                .schedule_crc32 = runtime->schedule_crc32,
                .operating_profile_crc32 = runtime->operating_profile_crc32,
                .baud_hz = runtime->baud_hz,
                .cycle_period_ns = runtime->cycle_period_ns,
                .loop_delay_ns = runtime->loop_delay_ns,
                .loop_delay_tolerance_ns = runtime->loop_delay_tolerance_ns,
                .feedback_timeout_ns = runtime->feedback_timeout_ns,
                .tx_dma_channel_id = runtime->tx_dma_channel_id,
                .rx_dma_channel_id = runtime->rx_dma_channel_id,
            };
            if (runtime->adapter_ops->start(runtime->adapter_context,
                                            &config)) {
                runtime->adapter_started = 1u;
                runtime->adapter_config_seq = runtime->config_seq;
                runtime->adapter_start_count++;
            } else {
                reason = TDMA_RING_RUNTIME_REASON_ADAPTER_MISSING;
            }
        }
        const uint32_t data_enabled =
            tdma_ring_runtime_load(&runtime->data_enabled);
        if (runtime->adapter_started != 0u && data_enabled == 0u) {
            const uint32_t command_seq =
                tdma_ring_runtime_load(&runtime->train_command_seq);
            if (command_seq != train_accepted_seq) {
                const uint32_t cycles =
                    tdma_ring_runtime_load(&runtime->train_command_cycles);
                train_request_seq = command_seq;
                train_request_cycles = cycles;
                train_accepted_seq = command_seq;
                if (runtime->adapter_ops->train_clock != NULL &&
                    runtime->adapter_ops->train_clock_service != NULL &&
                    runtime->adapter_ops->train_clock(runtime->adapter_context,
                                                      cycles)) {
                    train_start_count++;
                    training_dirty = 1u;
                } else {
                    train_reject_count++;
                }
            }
            if (training_dirty != 0u &&
                runtime->adapter_ops->train_clock_service != NULL) {
                runtime->adapter_ops->train_clock_service(
                    runtime->adapter_context, now_ns);
            }
        } else if (runtime->adapter_started != 0u &&
                   data_enabled != 0u && training_dirty != 0u) {
            /* Training replaces both PIO programs. Restore the normal DATA/CS
             * persona before cyclic service is allowed to run. */
            tdma_ring_runtime_stop_adapter(runtime);
            training_dirty = 0u;
        }
        if (runtime->adapter_started != 0u && data_enabled != 0u) {
            adapter_service_ok = runtime->adapter_ops->service(
                runtime->adapter_context,
                now_ns,
                &adapter_status);
            runtime->adapter_service_count++;
            if (!adapter_service_ok) {
                reason = TDMA_RING_RUNTIME_REASON_EVIDENCE_MISSING;
            }
        }
    }

    uint32_t round_trip_ns = 0u;
    const bool feedback_correlated = adapter_service_ok &&
        adapter_status.down_rx_sequence != previous_down_rx_sequence &&
        tdma_ring_runtime_feedback_correlated(runtime,
                                              &adapter_status,
                                              &round_trip_ns);
    if (adapter_service_ok && adapter_status.up_running != 0u &&
        adapter_status.down_running != 0u && !feedback_correlated &&
        reason == TDMA_RING_RUNTIME_REASON_NONE) {
        const bool hardware_timestamp_eligible =
            adapter_status.reference_tx_timestamp_ns != 0ull &&
            adapter_status.feedback_rx_timestamp_ns >=
                adapter_status.reference_tx_timestamp_ns &&
            adapter_status.feedback_rx_timestamp_ns -
                    adapter_status.reference_tx_timestamp_ns <=
                runtime->feedback_timeout_ns &&
            adapter_status.timestamp_resolution_ns != 0u &&
            adapter_status.timestamp_resolution_ns <= 100u &&
            (adapter_status.timestamp_flags &
             TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) != 0u &&
            (adapter_status.timestamp_flags &
             TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u;
        reason = hardware_timestamp_eligible
                     ? TDMA_RING_RUNTIME_REASON_EVIDENCE_MISSING
                     : TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING;
    }

    tdma_ring_runtime_write_guard(&runtime->result_guard);
    runtime->service_seq++;
    runtime->up_configured = adapter_service_ok
                                 ? adapter_status.up_configured
                                 : (up_group != 0u ? 1u : 0u);
    runtime->down_configured = adapter_service_ok
                                   ? adapter_status.down_configured
                                   : (down_group != 0u ? 1u : 0u);
    runtime->up_running = adapter_service_ok ? adapter_status.up_running : 0u;
    runtime->down_running = adapter_service_ok ? adapter_status.down_running : 0u;
    if (runtime->up_running != 0u && runtime->down_running != 0u) {
        runtime->ring_seq++;
    }
    runtime->last_reason = (uint32_t)reason;
    runtime->simultaneous_feedback_loop_evidence =
        feedback_correlated ? 1u : 0u;
    runtime->up_tx_sequence = adapter_status.up_tx_sequence;
    runtime->down_rx_sequence = adapter_status.down_rx_sequence;
    runtime->up_tx_frame_crc32 = adapter_status.up_tx_frame_crc32;
    runtime->down_rx_frame_crc32 = adapter_status.down_rx_frame_crc32;
    runtime->timestamp_resolution_ns = adapter_status.timestamp_resolution_ns;
    runtime->timestamp_flags = adapter_status.timestamp_flags;
    runtime->adapter_last_error = adapter_status.last_error;
    runtime->idle_beacon_tx_count = adapter_status.idle_beacon_tx_count;
    runtime->idle_beacon_rx_count = adapter_status.idle_beacon_rx_count;
    runtime->feedback_round_trip_ns = round_trip_ns;
    runtime->adapter_tx_count = adapter_status.tx_count;
    runtime->adapter_rx_count = adapter_status.rx_count;
    runtime->adapter_rx_bad_count = adapter_status.rx_bad_count;
    runtime->train_request_seq = train_request_seq;
    __atomic_store_n(&runtime->train_accepted_seq,
                     train_accepted_seq,
                     __ATOMIC_RELEASE);
    runtime->train_request_cycles = train_request_cycles;
    runtime->train_start_count = train_start_count;
    runtime->train_reject_count = train_reject_count;
    runtime->training_dirty = training_dirty;
    runtime->reference_tx_timestamp_ns =
        adapter_status.reference_tx_timestamp_ns;
    runtime->feedback_rx_timestamp_ns =
        adapter_status.feedback_rx_timestamp_ns;
    tdma_ring_runtime_write_guard(&runtime->result_guard);
}

bool tdma_ring_runtime_get_snapshot(const tdma_ring_runtime_t *runtime,
                                    tdma_ring_runtime_snapshot_t *snapshot)
{
    if (runtime == NULL || snapshot == NULL) {
        return false;
    }

    bool config_copied = false;
    for (uint32_t attempt = 0u;
         attempt < TDMA_RING_RUNTIME_SNAPSHOT_RETRY_LIMIT;
         attempt++) {
        const uint32_t guard_begin =
            tdma_ring_runtime_load(&runtime->config_guard);
        if ((guard_begin & 1u) != 0u) {
            continue;
        }
        snapshot->version = TDMA_RING_RUNTIME_VERSION;
        snapshot->enabled = runtime->enabled;
        snapshot->config_seq = runtime->config_seq;
        snapshot->node_count = runtime->node_count;
        snapshot->local_slot_id = runtime->local_slot_id;
        snapshot->reference_slot_id = runtime->reference_slot_id;
        snapshot->up_group_id = runtime->up_group_id;
        snapshot->down_group_id = runtime->down_group_id;
        snapshot->flags = runtime->flags;
        snapshot->ring_profile_crc32 = runtime->ring_profile_crc32;
        snapshot->schedule_crc32 = runtime->schedule_crc32;
        snapshot->operating_profile_crc32 = runtime->operating_profile_crc32;
        snapshot->baud_hz = runtime->baud_hz;
    snapshot->cycle_period_ns = runtime->cycle_period_ns;
    snapshot->loop_delay_ns = runtime->loop_delay_ns;
    snapshot->loop_delay_tolerance_ns = runtime->loop_delay_tolerance_ns;
    snapshot->feedback_timeout_ns = runtime->feedback_timeout_ns;
        snapshot->tx_dma_channel_id = runtime->tx_dma_channel_id;
        snapshot->rx_dma_channel_id = runtime->rx_dma_channel_id;
        const uint32_t guard_end =
            tdma_ring_runtime_load(&runtime->config_guard);
        if (guard_begin == guard_end && (guard_end & 1u) == 0u) {
            config_copied = true;
            break;
        }
    }
    if (!config_copied) {
        return false;
    }

    for (uint32_t attempt = 0u;
         attempt < TDMA_RING_RUNTIME_SNAPSHOT_RETRY_LIMIT;
         attempt++) {
        const uint32_t guard_begin =
            tdma_ring_runtime_load(&runtime->result_guard);
        if ((guard_begin & 1u) != 0u) {
            continue;
        }
        snapshot->config_reject_count = runtime->config_reject_count;
        snapshot->service_seq = runtime->service_seq;
        snapshot->up_configured = runtime->up_configured;
        snapshot->down_configured = runtime->down_configured;
        snapshot->up_running = runtime->up_running;
        snapshot->down_running = runtime->down_running;
        snapshot->ring_seq = runtime->ring_seq;
        snapshot->last_reason = runtime->last_reason;
        snapshot->simultaneous_feedback_loop_evidence =
            runtime->simultaneous_feedback_loop_evidence;
        snapshot->adapter_started = runtime->adapter_started;
        snapshot->adapter_start_count = runtime->adapter_start_count;
        snapshot->adapter_stop_count = runtime->adapter_stop_count;
        snapshot->adapter_service_count = runtime->adapter_service_count;
        snapshot->adapter_last_error = runtime->adapter_last_error;
        snapshot->up_tx_sequence = runtime->up_tx_sequence;
        snapshot->down_rx_sequence = runtime->down_rx_sequence;
        snapshot->up_tx_frame_crc32 = runtime->up_tx_frame_crc32;
        snapshot->down_rx_frame_crc32 = runtime->down_rx_frame_crc32;
        snapshot->timestamp_resolution_ns = runtime->timestamp_resolution_ns;
        snapshot->timestamp_flags = runtime->timestamp_flags;
        snapshot->idle_beacon_tx_count = runtime->idle_beacon_tx_count;
        snapshot->idle_beacon_rx_count = runtime->idle_beacon_rx_count;
        snapshot->feedback_round_trip_ns = runtime->feedback_round_trip_ns;
        snapshot->adapter_tx_count = runtime->adapter_tx_count;
        snapshot->adapter_rx_count = runtime->adapter_rx_count;
        snapshot->adapter_rx_bad_count = runtime->adapter_rx_bad_count;
        snapshot->train_request_seq = runtime->train_request_seq;
        snapshot->train_accepted_seq = runtime->train_accepted_seq;
        snapshot->train_request_cycles = runtime->train_request_cycles;
        snapshot->train_start_count = runtime->train_start_count;
        snapshot->train_reject_count = runtime->train_reject_count;
        snapshot->training_dirty = runtime->training_dirty;
        snapshot->reference_tx_timestamp_ns =
            runtime->reference_tx_timestamp_ns;
        snapshot->feedback_rx_timestamp_ns =
            runtime->feedback_rx_timestamp_ns;
        const uint32_t guard_end =
            tdma_ring_runtime_load(&runtime->result_guard);
        if (guard_begin == guard_end && (guard_end & 1u) == 0u) {
            return true;
        }
    }
    return false;
}
