#include "tdma_ring_runtime.h"

#include <string.h>

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
        config->local_slot_id >= config->node_count ||
        config->reference_slot_id >= config->node_count ||
        (config->flags & TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN) == 0u ||
        config->ring_profile_crc32 == 0u || config->schedule_crc32 == 0u) {
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
    }
    tdma_ring_runtime_write_guard(&runtime->config_guard);

    tdma_ring_runtime_write_guard(&runtime->result_guard);
    runtime->up_configured = runtime->up_group_id != 0u ? 1u : 0u;
    runtime->down_configured = runtime->down_group_id != 0u ? 1u : 0u;
    runtime->up_running = 0u;
    runtime->down_running = 0u;
    runtime->last_reason = TDMA_RING_RUNTIME_REASON_NONE;
    runtime->simultaneous_feedback_loop_evidence = 0u;
    tdma_ring_runtime_write_guard(&runtime->result_guard);
    return true;
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
    const bool up_down_ready =
        enabled != 0u && up_group != 0u && down_group != 0u &&
        up_group != down_group &&
        (flags & TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN) != 0u;

    tdma_ring_runtime_write_guard(&runtime->result_guard);
    runtime->service_seq++;
    runtime->up_configured = up_group != 0u ? 1u : 0u;
    runtime->down_configured = down_group != 0u ? 1u : 0u;
    runtime->up_running = up_down_ready ? 1u : 0u;
    runtime->down_running = up_down_ready ? 1u : 0u;
    if (up_down_ready) {
        runtime->ring_seq++;
        runtime->last_reason = TDMA_RING_RUNTIME_REASON_NONE;
    } else if (enabled != 0u) {
        runtime->last_reason = TDMA_RING_RUNTIME_REASON_BAD_CONFIG;
    } else {
        runtime->last_reason = TDMA_RING_RUNTIME_REASON_NONE;
    }
    runtime->simultaneous_feedback_loop_evidence = 0u;
    tdma_ring_runtime_write_guard(&runtime->result_guard);
}

bool tdma_ring_runtime_get_snapshot(const tdma_ring_runtime_t *runtime,
                                    tdma_ring_runtime_snapshot_t *snapshot)
{
    if (runtime == NULL || snapshot == NULL) {
        return false;
    }

    while (true) {
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
        const uint32_t guard_end =
            tdma_ring_runtime_load(&runtime->config_guard);
        if (guard_begin == guard_end && (guard_end & 1u) == 0u) {
            break;
        }
    }

    while (true) {
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
        const uint32_t guard_end =
            tdma_ring_runtime_load(&runtime->result_guard);
        if (guard_begin == guard_end && (guard_end & 1u) == 0u) {
            return true;
        }
    }
}
